#include "FileManager.h"

#include "core/encoding/CharsetDetector.h"
#include "core/encoding/Codepage.h"
#include "core/encoding/UnicodeConverter.h"
#include "core/lang/LanguageRegistry.h"
#include "core/lang/UserLanguage.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <Scintilla.h>

#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>

#include <algorithm>
#include <ctime>
#include <set>

namespace npp::core {

// --- Buffer helpers --------------------------------------------------------------------------

EolType detectEol(const char* data, size_t length, EolType fallback)
{
    for (size_t i = 0; i < length; ++i) {
        if (data[i] == '\r')
            return (i + 1 < length && data[i + 1] == '\n') ? EolType::Windows : EolType::Mac;
        if (data[i] == '\n')
            return EolType::Unix;
    }
    return fallback;
}

const char* eolString(EolType eol) noexcept
{
    switch (eol) {
    case EolType::Windows: return "\r\n";
    case EolType::Mac:     return "\r";
    default:               return "\n";
    }
}

int scintillaEolMode(EolType eol) noexcept
{
    switch (eol) {
    case EolType::Windows: return SC_EOL_CRLF;
    case EolType::Mac:     return SC_EOL_CR;
    default:               return SC_EOL_LF;
    }
}

FileTime fileLastWriteTime(const std::string& utf8Path)
{
    const QFileInfo fi(QString::fromStdString(utf8Path));
    if (!fi.exists())
        return {};
    return FileTime::fromUnixMs(fi.lastModified().toMSecsSinceEpoch());
}

std::string Buffer::displayName() const
{
    return path.empty() ? untitledName : fileNameOf(path);
}

// --- FileManager -------------------------------------------------------------------------------

namespace {

// Temporarily points the scratch editor to `doc`, restoring its own document afterwards.
class DocSwitch {
public:
    DocSwitch(const IEditor& e, intptr_t doc) : m_e(e)
    {
        m_prev = m_e.send(SCI_GETDOCPOINTER);
        m_e.send(SCI_ADDREFDOCUMENT, 0, m_prev);
        m_e.send(SCI_SETDOCPOINTER, 0, doc);
    }
    ~DocSwitch()
    {
        m_e.send(SCI_SETDOCPOINTER, 0, m_prev);
        m_e.send(SCI_RELEASEDOCUMENT, 0, m_prev);
    }
    DocSwitch(const DocSwitch&) = delete;
    DocSwitch& operator=(const DocSwitch&) = delete;
private:
    const IEditor& m_e;
    intptr_t m_prev = 0;
};

constexpr size_t kDetectionBlock = 128 * 1024; // upstream reads files in 128 KiB blocks

} // namespace

FileManager::FileManager(const IEditor& scratch, FileManagerSettings settings, const LanguageRegistry* langs,
                         const UserLanguageRegistry* udls)
    : m_scratch(scratch), m_settings(std::move(settings)), m_langs(langs), m_udls(udls)
{
}

FileManager::~FileManager()
{
    for (auto& b : m_buffers)
        if (b->document)
            m_scratch.send(SCI_RELEASEDOCUMENT, 0, b->document);
}

Buffer* FileManager::createBuffer()
{
    auto b = std::make_unique<Buffer>();
    b->id = m_nextId++;
    b->document = m_scratch.send(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    {
        DocSwitch sw(m_scratch, b->document);
        m_scratch.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    }
    m_buffers.push_back(std::move(b));
    return m_buffers.back().get();
}

std::string FileManager::nextUntitledName() const
{
    std::set<int> used;
    const std::string& prefix = m_settings.untitledPrefix;
    for (const auto& b : m_buffers)
        if (b->isUntitled() && b->untitledName.rfind(prefix, 0) == 0)
            if (const auto n = parseInt(b->untitledName.substr(prefix.size())))
                used.insert(static_cast<int>(*n));
    int n = 1;
    while (used.count(n))
        ++n;
    return prefix + std::to_string(n);
}

Buffer* FileManager::newDocument()
{
    Buffer* b = createBuffer();
    b->untitledName = nextUntitledName();
    b->status = DocStatus::Unnamed;
    b->uniMode = m_settings.newDocUniMode;
    b->encoding = m_settings.newDocEncoding;
    b->eol = m_settings.newDocEol;
    b->lang = m_settings.newDocLang;
    return b;
}

LangType FileManager::detectLanguage(std::string_view utf8Path, std::string_view firstBlock, std::string* udlName) const
{
    if (m_udls)
        if (const UserLanguage* u = m_udls->findForFile(utf8Path)) {
            if (udlName)
                *udlName = u->name;
            return L_USER;
        }
    LangType lang = m_langs ? m_langs->languageForFile(utf8Path) : L_TEXT;
    if (lang == L_TEXT)
        lang = detectLanguageFromTextBeginning(firstBlock);
    return lang;
}

bool FileManager::fillDocument(Buffer& b, const std::string& bytes, const LoadOptions& o)
{
    // --- encoding decision (upstream loadFileData + setLoadedBufferEncodingAndEol) -----------
    int encoding = o.encodingOverride;
    UniMode mode = UniMode::Utf8;
    std::string utf8;
    const UniMode bom = detectBom(bytes);
    if (o.uniModeOverride) {
        // "Encode in ..." : reinterpret the bytes in the given mode, no detection
        mode = *o.uniModeOverride;
        utf8 = mode == UniMode::Ansi ? codePageToUtf8(bytes, systemAnsiCodePage()).value_or(bytes)
             : (mode == UniMode::Utf8 || mode == UniMode::Ascii7) ? bytes
                                                                   : decodeToUtf8(bytes, mode);
        encoding = -1;
    } else if (bom != UniMode::Ansi) {
        encoding = -1;
    } else if (encoding == -1 && m_settings.detectEncoding) {
        encoding = detectCodePage(std::string_view(bytes).substr(0, kDetectionBlock));
    }

    if (!o.uniModeOverride && encoding != -1) {
        if (encoding == kCodePageUtf8) {
            utf8 = bytes;
        } else if (auto u = codePageToUtf8(bytes, encoding)) {
            utf8 = std::move(*u);
        } else {
            encoding = -1; // unsupported code page on this platform: fall back to detection below
        }
    }
    if (o.uniModeOverride) {
        // decided above
    } else if (encoding != -1) {
        mode = UniMode::Utf8;
        if (encoding == kCodePageUtf8)
            encoding = -1;
    } else {
        mode = detectUnicodeMode(std::string_view(bytes).substr(0, kDetectionBlock));
        if (bom != UniMode::Ansi)
            mode = bom;
        switch (mode) {
        case UniMode::Ascii7:
            utf8 = bytes;
            mode = m_settings.openAnsiAsUtf8 ? UniMode::Utf8 : UniMode::Ansi;
            break;
        case UniMode::Ansi:
            utf8 = codePageToUtf8(bytes, systemAnsiCodePage()).value_or(bytes);
            break;
        default:
            utf8 = decodeToUtf8(bytes, mode);
            break;
        }
    }

    b.encoding = encoding;
    b.uniMode = mode;
    b.eol = detectEol(utf8.data(), std::min(utf8.size(), kDetectionBlock * 8), m_settings.newDocEol);
    b.fileSize = bytes.size();
    b.largeFile = bytes.size() >= m_settings.largeFileSizeBytes;

    // --- fill the Scintilla document --------------------------------------------------------
    DocSwitch sw(m_scratch, b.document);
    m_scratch.send(SCI_SETSTATUS, SC_STATUS_OK);
    m_scratch.send(SCI_SETREADONLY, 0);
    m_scratch.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    m_scratch.send(SCI_SETUNDOCOLLECTION, 0);
    m_scratch.send(SCI_CLEARALL);
    m_scratch.send(SCI_ALLOCATE, utf8.size() + std::min<size_t>(1u << 20, utf8.size() / 6));
    m_scratch.send(SCI_APPENDTEXT, utf8.size(), reinterpret_cast<intptr_t>(utf8.data()));
    const int status = static_cast<int>(m_scratch.send(SCI_GETSTATUS));
    m_scratch.send(SCI_SETUNDOCOLLECTION, 1);
    m_scratch.send(SCI_EMPTYUNDOBUFFER);
    m_scratch.send(SCI_SETSAVEPOINT);
    return status == SC_STATUS_OK || status >= SC_STATUS_WARN_START;
}

Buffer* FileManager::loadFile(std::string_view utf8PathIn, const LoadOptions& o, LoadError* error)
{
    if (error)
        *error = LoadError::None;
    const std::string utf8Path = absolutePath(utf8PathIn);
    if (Buffer* existing = findByPath(utf8Path))
        return existing;
    if (!pathExists(utf8Path, PathKind::File)) {
        if (error) *error = LoadError::NotFound;
        return nullptr;
    }
    const auto bytes = readFileContent(utf8Path);
    if (!bytes) {
        if (error) *error = LoadError::CannotRead;
        return nullptr;
    }

    Buffer* b = createBuffer();
    b->path = utf8Path;
    b->status = DocStatus::Regular;
    if (!fillDocument(*b, *bytes, o)) {
        close(*b);
        if (error) *error = LoadError::OutOfMemory;
        return nullptr;
    }
    if (o.langOverride) {
        b->lang = *o.langOverride;
        b->userLangName = o.userLangOverride;
    } else {
        b->lang = detectLanguage(utf8Path, b->largeFile ? std::string_view() : std::string_view(*bytes).substr(0, kDetectionBlock),
                                 &b->userLangName);
    }
    b->timestamp = fileLastWriteTime(utf8Path);
    b->fileReadOnly = !QFileInfo(QString::fromStdString(utf8Path)).isWritable();
    return b;
}

Buffer* FileManager::loadBackup(std::string_view backupPath, std::string_view originalPath, std::string_view untitledName,
                                int encoding, LoadError* error)
{
    if (error)
        *error = LoadError::None;
    const auto bytes = readFileContent(backupPath);
    if (!bytes) {
        if (error) *error = LoadError::CannotRead;
        return nullptr;
    }
    Buffer* b = createBuffer();
    LoadOptions o;
    o.encodingOverride = encoding;
    if (!fillDocument(*b, *bytes, o)) {
        close(*b);
        if (error) *error = LoadError::OutOfMemory;
        return nullptr;
    }
    b->backupPath = std::string(backupPath);
    b->dirty = true;
    if (originalPath.empty()) {
        b->status = DocStatus::Unnamed;
        b->untitledName = untitledName.empty() ? nextUntitledName() : std::string(untitledName);
        b->lang = detectLanguageFromTextBeginning(std::string_view(*bytes).substr(0, kDetectionBlock));
    } else {
        b->path = absolutePath(originalPath);
        const bool exists = pathExists(b->path, PathKind::File);
        b->status = exists ? DocStatus::Regular : DocStatus::Deleted;
        b->timestamp = fileLastWriteTime(b->path);
        b->lang = detectLanguage(b->path, std::string_view(*bytes).substr(0, kDetectionBlock), &b->userLangName);
    }
    return b;
}

bool FileManager::reload(Buffer& b, int encodingOverride, std::optional<UniMode> uniModeOverride)
{
    if (b.isUntitled())
        return false;
    const auto bytes = readFileContent(b.path);
    if (!bytes)
        return false;
    LoadOptions o;
    o.encodingOverride = encodingOverride;
    o.uniModeOverride = uniModeOverride;
    if (!fillDocument(b, *bytes, o))
        return false;
    b.dirty = false;
    b.status = DocStatus::Regular;
    b.timestamp = fileLastWriteTime(b.path);
    b.fileReadOnly = !QFileInfo(QString::fromStdString(b.path)).isWritable();
    return true;
}

void FileManager::close(Buffer& b)
{
    deleteBackup(b);
    if (b.document)
        m_scratch.send(SCI_RELEASEDOCUMENT, 0, b.document);
    m_buffers.erase(std::remove_if(m_buffers.begin(), m_buffers.end(), [&b](const auto& p) { return p.get() == &b; }),
                    m_buffers.end());
}

Buffer* FileManager::find(BufferId id) const
{
    for (const auto& b : m_buffers)
        if (b->id == id)
            return b.get();
    return nullptr;
}

Buffer* FileManager::findByPath(std::string_view utf8Path) const
{
    if (utf8Path.empty())
        return nullptr;
    for (const auto& b : m_buffers)
        if (!b->path.empty() && samePath(b->path, utf8Path))
            return b.get();
    return nullptr;
}

std::string FileManager::utf8Content(const Buffer& b) const
{
    DocSwitch sw(m_scratch, b.document);
    return m_scratch.text();
}

std::string FileManager::encodedContent(const Buffer& b) const
{
    const std::string utf8 = utf8Content(b);
    if (b.encoding != -1)
        return utf8ToCodePage(utf8, b.encoding).value_or(utf8);
    switch (b.uniMode) {
    case UniMode::Ansi:   return utf8ToCodePage(utf8, systemAnsiCodePage()).value_or(utf8);
    case UniMode::Ascii7:
    case UniMode::Utf8:   return utf8;
    default:              return encodeFromUtf8(utf8, b.uniMode);
    }
}

SaveStatus FileManager::save(Buffer& b, std::string_view utf8PathIn, bool asCopy)
{
    const std::string utf8Path = absolutePath(utf8PathIn);
    const std::string data = encodedContent(b);

    QSaveFile f(QString::fromStdString(utf8Path));
    f.setDirectWriteFallback(true); // directory not writable but file is
    if (!f.open(QIODevice::WriteOnly))
        return SaveStatus::OpenFailed;
    if (f.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size()) || !f.commit())
        return SaveStatus::WriteFailed;

    if (!asCopy) {
        b.path = utf8Path;
        b.untitledName.clear();
        b.status = DocStatus::Regular;
        b.dirty = false;
        b.timestamp = fileLastWriteTime(utf8Path);
        b.fileReadOnly = false;
        b.fileSize = data.size();
        DocSwitch sw(m_scratch, b.document);
        m_scratch.send(SCI_SETSAVEPOINT);
        deleteBackup(b);
    }
    return SaveStatus::Ok;
}

FileChange FileManager::checkFileState(Buffer& b)
{
    if (b.isUntitled())
        return FileChange::None;
    const bool exists = pathExists(b.path, PathKind::File);
    if (!exists) {
        if (b.status != DocStatus::Deleted) {
            b.status = DocStatus::Deleted;
            return FileChange::Deleted;
        }
        return FileChange::None;
    }
    const FileTime t = fileLastWriteTime(b.path);
    if (b.status == DocStatus::Deleted) {
        b.status = DocStatus::Regular;
        b.timestamp = t;
        return FileChange::Restored;
    }
    if (t.value != b.timestamp.value) {
        b.timestamp = t;
        b.status = DocStatus::Modified;
        return FileChange::Modified;
    }
    const bool ro = !QFileInfo(QString::fromStdString(b.path)).isWritable();
    if (ro != b.fileReadOnly) {
        b.fileReadOnly = ro;
        return FileChange::ReadOnlyChanged;
    }
    return FileChange::None;
}

std::string FileManager::backupFileName(const Buffer& b, std::time_t now)
{
    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &now);
#else
    localtime_r(&now, &tmLocal);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", &tmLocal);
    return b.displayName() + "@" + stamp;
}

bool FileManager::backup(Buffer& b, std::string_view backupDir)
{
    if (!b.dirty)
        return deleteBackup(b);
    if (b.backupPath.empty()) {
        std::error_code ec;
        fs::create_directories(pathFromUtf8(backupDir), ec);
        b.backupPath = pathAppend(backupDir, backupFileName(b, std::time(nullptr)));
    }
    return writeFileContent(b.backupPath, encodedContent(b));
}

bool FileManager::deleteBackup(Buffer& b)
{
    if (b.backupPath.empty())
        return true;
    std::error_code ec;
    fs::remove(pathFromUtf8(b.backupPath), ec);
    b.backupPath.clear();
    return !ec;
}

} // namespace npp::core
