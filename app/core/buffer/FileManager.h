// Loads / saves / reloads / backs up documents (port of upstream FileManager, Buffer.cpp).
#pragma once

#include "core/buffer/Buffer.h"
#include "core/editor/IEditor.h"

#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

class LanguageRegistry;
class UserLanguageRegistry;

struct FileManagerSettings {
    bool     detectEncoding = true;        // GuiConfig::detectEncoding
    bool     openAnsiAsUtf8 = true;        // GuiConfig::newDocOpenAnsiAsUtf8
    UniMode  newDocUniMode = UniMode::Utf8;
    int      newDocEncoding = -1;          // code page for new documents, -1 none
    EolType  newDocEol =
#if defined(_WIN32)
        EolType::Windows;
#else
        EolType::Unix;
#endif
    LangType newDocLang = L_TEXT;
    uint64_t largeFileSizeBytes = 200ull * 1024 * 1024; // GuiConfig::largeFileSizeMB
    std::string untitledPrefix = "new ";   // localized "tab-untitled-string"
};

enum class LoadError { None, NotFound, CannotRead, TooBig, OutOfMemory };
// AccessDenied: the file or its folder is protected (save again through the elevated helper)
enum class SaveStatus { Ok, OpenFailed, WriteFailed, ReadOnlyForbidden, AccessDenied };

struct LoadOptions {
    int encodingOverride = -1;              // forced code page (Encoding > Character sets), -1 = detect
    std::optional<UniMode> uniModeOverride;  // forced Unicode mode (Encoding > Encode in ...), no detection
    std::optional<LangType> langOverride;
    std::string userLangOverride;           // UDL name
};

enum class FileChange { None, Modified, Deleted, Restored, ReadOnlyChanged };

class FileManager {
public:
    // `scratch` is an editor used to fill / read Scintilla documents (upstream _pscratchTilla).
    FileManager(const IEditor& scratch, FileManagerSettings settings, const LanguageRegistry* langs = nullptr,
                const UserLanguageRegistry* udls = nullptr);
    ~FileManager();
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    const FileManagerSettings& settings() const noexcept { return m_settings; }
    const IEditor& scratchEditor() const noexcept { return m_scratch; }
    void setSettings(const FileManagerSettings& s) { m_settings = s; }

    // --- documents -------------------------------------------------------------------------
    Buffer* newDocument();
    // Opens a file; returns the existing buffer if already open (same path).
    Buffer* loadFile(std::string_view utf8Path, const LoadOptions& options = {}, LoadError* error = nullptr);
    // Restores an untitled / dirty document from its snapshot backup (session restore).
    Buffer* loadBackup(std::string_view backupPath, std::string_view originalPath, std::string_view untitledName,
                       int encoding, LoadError* error = nullptr);
    bool reload(Buffer& buffer, int encodingOverride = -1, std::optional<UniMode> uniModeOverride = {});
    void close(Buffer& buffer);

    Buffer* find(BufferId id) const;
    Buffer* findByPath(std::string_view utf8Path) const;
    const std::vector<std::unique_ptr<Buffer>>& buffers() const noexcept { return m_buffers; }

    // --- saving ----------------------------------------------------------------------------
    // Writes the document to `utf8Path` in the buffer's encoding / Unicode mode. When asCopy is
    // false the buffer adopts the new path, becomes clean and its timestamp is refreshed.
    SaveStatus save(Buffer& buffer, std::string_view utf8Path, bool asCopy = false);
    // The buffer was written to `utf8Path` by someone else (elevated save helper): adopt the
    // path, become clean, refresh the timestamp (what save does after writing).
    void markSaved(Buffer& buffer, std::string_view utf8Path, std::size_t size);
    // Bytes that would be written (encoding applied); used by save and tests.
    std::string encodedContent(const Buffer& buffer) const;
    std::string utf8Content(const Buffer& buffer) const;

    // --- external changes (upstream checkFilesystemChanges, per buffer) ----------------------
    FileChange checkFileState(Buffer& buffer);

    // --- snapshot backups (upstream backupCurrentBuffer) --------------------------------------
    // Writes (dirty) or deletes (clean) the backup of `buffer` in `backupDir`.
    bool backup(Buffer& buffer, std::string_view backupDir);
    bool deleteBackup(Buffer& buffer);
    // Backup file name: "<name>@YYYY-MM-DD_HHMMSS" (local time)
    static std::string backupFileName(const Buffer& buffer, std::time_t now);

    // Language for a path using langs.xml, UDL extensions and the first line of `data`.
    LangType detectLanguage(std::string_view utf8Path, std::string_view firstBlock, std::string* udlName) const;

private:
    Buffer* createBuffer();
    bool fillDocument(Buffer& b, const std::string& bytes, const LoadOptions& o);
    std::string nextUntitledName() const;

    const IEditor& m_scratch;
    FileManagerSettings m_settings;
    const LanguageRegistry* m_langs;
    const UserLanguageRegistry* m_udls;
    std::vector<std::unique_ptr<Buffer>> m_buffers;
    BufferId m_nextId = 1;
};

} // namespace npp::core
