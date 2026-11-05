// Tools (MD5 / SHA-1 / SHA-256 / SHA-512), View > Summary / Monitoring, Run > Validate shortcuts.xml,
// Language > UDL folder / collection, ? menu (command line arguments, debug info, web pages).
#include "MainWindow.h"

#include "DialogTranslator.h"
#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "PlatformShell.h"

#include "core/util/Hash.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageAuthenticationCode>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

const char* dialogTag(HashType t, bool files)
{
    switch (t) {
    case HashType::Md5: return files ? "MD5FromFilesDlg" : "MD5FromTextDlg";
    case HashType::Sha1: return files ? "SHA1FromFilesDlg" : "SHA1FromTextDlg";
    case HashType::Sha256: return files ? "SHA256FromFilesDlg" : "SHA256FromTextDlg";
    case HashType::Sha512: return files ? "SHA512FromFilesDlg" : "SHA512FromTextDlg";
    }
    return "";
}

// upstream md5Dlgs_rc.h
constexpr int IDC_HASH_PATH_EDIT = 1921, IDC_HASH_FILEBROWSER_BUTTON = 1922, IDC_HASH_RESULT_EDIT = 1923,
              IDC_HASH_TOCLIPBOARD_BUTTON = 1924, IDC_HASH_TEXT_EDIT = 1931, IDC_HASH_EACHLINE_CHECK = 1932,
              IDC_HASH_RESULT_FOMTEXT_EDIT = 1933, IDC_HASH_FROMTEXT_TOCLIPBOARD_BUTTON = 1934, IDCANCEL = 2;

QPlainTextEdit* monoEdit(bool readOnly)
{
    auto* e = new QPlainTextEdit;
    e->setReadOnly(readOnly);
    e->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f(QStringLiteral("Courier New"));
    f.setStyleHint(QFont::Monospace);
    e->setFont(f);
    return e;
}

// upstream utflen-based counting: code points, continuation bytes and EOL characters not counted
size_t countUtf8(const char* p, size_t begin, size_t end)
{
    size_t n = 0;
    for (size_t i = begin; i < end; ++i) {
        const auto c = static_cast<unsigned char>(p[i]);
        if ((c & 0xC0) == 0x80 || c == '\n' || c == '\r')
            continue;
        ++n;
    }
    return n;
}

} // namespace

// --- hash dialogs ---------------------------------------------------------------------------------

QDialog* MainWindow::hashDialog(int type, bool fromFiles)
{
    const auto t = static_cast<HashType>(type);
    const QString key = QStringLiteral("%1-%2").arg(type).arg(fromFiles);
    if (QPointer<QDialog> d = m_hashDialogs.value(key)) {
        d->show();
        d->raise();
        return d;
    }
    auto* dlg = new QDialog(this);
    dlg->setObjectName(QString::fromLatin1(dialogTag(t, fromFiles)));
    const QString name = QString::fromLatin1(hashName(t));
    auto* layout = new QVBoxLayout(dlg);
    auto* copy = new QPushButton(tr("Co&py to Clipboard"));
    auto* close = new QPushButton(tr("&Close"));
    setControlId(close, IDCANCEL);
    connect(close, &QPushButton::clicked, dlg, &QDialog::hide);
    if (fromFiles) {
        dlg->setWindowTitle(tr("Generate %1 digest from files").arg(name));
        auto* choose = new QPushButton(tr("Choose files to &generate %1...").arg(name));
        setControlId(choose, IDC_HASH_FILEBROWSER_BUTTON);
        auto* paths = monoEdit(true);
        setControlId(paths, IDC_HASH_PATH_EDIT);
        auto* result = monoEdit(true);
        setControlId(result, IDC_HASH_RESULT_EDIT);
        setControlId(copy, IDC_HASH_TOCLIPBOARD_BUTTON);
        layout->addWidget(choose, 0, Qt::AlignLeft);
        layout->addWidget(paths, 1);
        layout->addWidget(result, 2);
        connect(choose, &QPushButton::clicked, dlg, [this, dlg, t, paths, result] {
            const QStringList files = QFileDialog::getOpenFileNames(dlg, tr("Choose files"));
            if (files.isEmpty())
                return;
            std::vector<std::string> list;
            QString shown;
            for (const QString& f : files) {
                list.push_back(f.toStdString());
                shown += QDir::toNativeSeparators(f) + QLatin1Char('\n');
            }
            paths->setPlainText(shown);
            result->setPlainText(q(hashFiles(t, list)).replace(QStringLiteral("\r\n"), QStringLiteral("\n")));
        });
        connect(copy, &QPushButton::clicked, dlg, [result] {
            if (!result->toPlainText().isEmpty())
                QApplication::clipboard()->setText(result->toPlainText());
        });
    } else {
        dlg->setWindowTitle(tr("Generate %1 digest").arg(name));
        auto* text = monoEdit(false);
        setControlId(text, IDC_HASH_TEXT_EDIT);
        text->setObjectName(QString::number(IDC_HASH_TEXT_EDIT));
        auto* perLine = new QCheckBox(tr("Treat each line as a &separate string"));
        setControlId(perLine, IDC_HASH_EACHLINE_CHECK);
        auto* result = monoEdit(true);
        setControlId(result, IDC_HASH_RESULT_FOMTEXT_EDIT);
        setControlId(copy, IDC_HASH_FROMTEXT_TOCLIPBOARD_BUTTON);
        layout->addWidget(text, 2);
        layout->addWidget(perLine);
        layout->addWidget(result, 1);
        auto update = [t, text, perLine, result] {
            const std::string s = text->toPlainText().toStdString();
            if (s.empty())
                result->clear();
            else if (perLine->isChecked())
                result->setPlainText(q(hashPerLine(t, s)).replace(QStringLiteral("\r\n"), QStringLiteral("\n")));
            else
                result->setPlainText(q(hashHex(t, s)));
        };
        connect(text, &QPlainTextEdit::textChanged, dlg, update);
        connect(perLine, &QCheckBox::toggled, dlg, update);
        connect(copy, &QPushButton::clicked, dlg, [result] {
            if (!result->toPlainText().isEmpty())
                QApplication::clipboard()->setText(result->toPlainText());
        });
    }
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(copy);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    translateDialog(dlg, dialogLanguage(), dialogTag(t, fromFiles));
    dlg->resize(640, 420);
    m_hashDialogs.insert(key, dlg);
    dlg->show();
    return dlg;
}

// --- Summary ----------------------------------------------------------------------------------------

QString MainWindow::summaryText()
{
    Buffer* b = currentBuffer();
    ScintillaEditBase* e = currentPane()->editor();
    if (!b)
        return {};
    auto str = [this](const char* id, const char* fallback) { return miscString(id, fallback); };
    QString out;
    if (!b->isUntitled()) {
        const QFileInfo fi(q(b->path));
        const QString fmt = QStringLiteral("yyyy-MM-dd HH:mm:ss");
        out += str("summary-filepath", "Full file path: ") + QDir::toNativeSeparators(q(b->path)) + QLatin1Char('\n');
        out += str("summary-filecreatetime", "Created: ") + fi.birthTime().toString(fmt) + QLatin1Char('\n');
        out += str("summary-filemodifytime", "Modified: ") + fi.lastModified().toString(fmt) + QLatin1Char('\n');
    }
    const auto len = static_cast<size_t>(e->send(SCI_GETLENGTH));
    const char* text = reinterpret_cast<const char*>(e->send(SCI_GETCHARACTERPOINTER));
    const size_t chars = countUtf8(text, 0, len);
    // upstream wordCount: regular expression count
    const QString all = QString::fromUtf8(text, static_cast<qsizetype>(len));
    static const QRegularExpression word(QStringLiteral("[^ \\t\\\\.,;:!?()+\\r\\n\\-*/=\\]\\[{}&~\"'`|@$%<>^]+"));
    size_t words = 0;
    for (auto it = word.globalMatch(all); it.hasNext(); it.next())
        ++words;
    size_t selChars = 0, selBytes = 0;
    const auto nSel = e->send(SCI_GETSELECTIONS);
    for (intptr_t i = 0; i < nSel; ++i) {
        const auto s = e->send(SCI_GETSELECTIONNSTART, static_cast<uptr_t>(i));
        const auto en = e->send(SCI_GETSELECTIONNEND, static_cast<uptr_t>(i));
        selBytes += static_cast<size_t>(en - s);
        const auto l1 = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(s));
        const auto l2 = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(en));
        for (auto l = l1; l <= l2; ++l) {
            const auto st = e->send(SCI_GETLINESELSTARTPOSITION, static_cast<uptr_t>(l));
            if (st == INVALID_POSITION)
                continue;
            const auto stop = e->send(SCI_GETLINESELENDPOSITION, static_cast<uptr_t>(l));
            if (nSel == 1 && !e->send(SCI_SELECTIONISRECTANGLE)) { // stream selection: count once
                selChars = countUtf8(text, static_cast<size_t>(s), static_cast<size_t>(en));
                break;
            }
            selChars += countUtf8(text, static_cast<size_t>(st), static_cast<size_t>(stop));
        }
    }
    size_t ranges = 0;
    if (nSel == 1)
        ranges = e->send(SCI_GETSELECTIONNSTART, 0) == e->send(SCI_GETSELECTIONNEND, 0) ? 0 : 1;
    else
        ranges = e->send(SCI_SELECTIONISRECTANGLE) ? 1 : static_cast<size_t>(nSel);
    auto n = [](size_t v) { return q(commafy(v)); };
    out += str("summary-nbchar", "Characters (without line endings): ") + n(chars) + QLatin1Char('\n');
    out += str("summary-nbword", "Words: ") + n(words) + QLatin1Char('\n');
    out += str("summary-nbline", "Lines: ") + n(static_cast<size_t>(e->send(SCI_GETLINECOUNT))) + QLatin1Char('\n');
    out += str("summary-nbbyte", "Document length: ") + n(len) + QLatin1Char('\n');
    out += n(selChars) + str("summary-nbsel1", " selected characters (") + n(selBytes) + str("summary-nbsel2", " bytes) in ")
           + n(ranges) + str("summary-nbrange", " ranges") + QLatin1Char('\n');
    return out;
}

// --- Monitoring (tail -f) -----------------------------------------------------------------------------

void MainWindow::setMonitoring(Buffer* b, bool on)
{
    if (!b)
        return;
    b->monitoring = on;
    for (EditorPane* p : m_panes)
        if (p->current() == b->id) {
            p->editor()->send(SCI_SETREADONLY, on || b->isReadOnly());
            if (on)
                p->editor()->send(SCI_DOCUMENTEND);
        }
    if (on && !m_monitorTimer) {
        m_monitorTimer = new QTimer(this);
        m_monitorTimer->setInterval(500);
        connect(m_monitorTimer, &QTimer::timeout, this, &MainWindow::pollMonitoredFiles);
    }
    if (m_monitorTimer) {
        bool any = false;
        for (const auto& buf : m_files->buffers())
            any |= buf->monitoring;
        any ? m_monitorTimer->start() : m_monitorTimer->stop();
    }
    refreshBuffer(b);
}

void MainWindow::pollMonitoredFiles()
{
    std::vector<BufferId> ids;
    for (const auto& b : m_files->buffers())
        if (b->monitoring)
            ids.push_back(b->id);
    for (BufferId id : ids) {
        Buffer* b = buffer(id);
        if (!b)
            continue;
        const FileChange ch = m_files->checkFileState(*b);
        if (ch == FileChange::Modified) {
            reloadBuffer(b);
            for (EditorPane* p : m_panes)
                if (p->current() == id) {
                    p->editor()->send(SCI_SETREADONLY, 1);
                    p->editor()->send(SCI_DOCUMENTEND);
                }
        } else if (ch == FileChange::Deleted) {
            setMonitoring(b, false);
        }
    }
}

// --- shortcuts.xml integrity (upstream MISC/hmac: HMAC-SHA256 keyed with the machine GUID) ------------

std::string MainWindow::shortcutsXmlHmac() const
{
    const auto content = readFileContent(m_paths.shortcutsXml());
    if (!content)
        return {};
    // Windows: HKLM\SOFTWARE\Microsoft\Cryptography MachineGuid (the upstream key); Linux: machine-id
    const QByteArray key = QSysInfo::machineUniqueId();
    return QMessageAuthenticationCode::hash(QByteArray::fromStdString(*content), key, QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

void MainWindow::recordShortcutsXmlHmac()
{
    m_config.gui.shortcutsXmlHmac = shortcutsXmlHmac();
}

bool MainWindow::shortcutsXmlTrusted()
{
    const bool missing = m_config.gui.shortcutsXmlHmac.empty();
    if (!missing && shortcutsXmlHmac() == m_config.gui.shortcutsXmlHmac)
        return true;
    // open shortcuts.xml read-only for review (upstream Notepad_plus::command, ID_MACRO / ID_USER_CMD)
    if (Buffer* b = openFile(q(m_paths.shortcutsXml()))) {
        b->userReadOnly = true;
        refreshBuffer(b);
    }
    const auto t = missing
                       ? m_nativeLang.messageBox("ShortcutsXmlHMACMissing",
                                                 "The security information for shortcuts.xml is missing in config.xml.\n\nFor security reasons, the integrity of shortcuts.xml will be checked. To run your customized command, please review the opened shortcuts.xml. If the file content is OK, use \"Validate shortcuts.xml\" from the \"Run\" menu to confirm it.",
                                                 "Security Warning")
                       : m_nativeLang.messageBox("ShortcutsXmlTampered",
                                                 "The shortcuts.xml file appears to have been modified manually.\n\nFor security reasons, please review the opened shortcuts.xml. If the file content is OK, use \"Validate shortcuts.xml\" from the \"Run\" menu to confirm it.",
                                                 "Security Warning");
    if (!m_quietSecurityWarnings)
        QMessageBox::warning(this, q(t.title), q(t.message));
    return false;
}

// --- commands --------------------------------------------------------------------------------------------

void MainWindow::registerToolCommands()
{
    ActionRegistry* r = m_actions;
    const struct {
        HashType type;
        int text, files, clip;
    } hashes[] = {
        {HashType::Md5, IDM_TOOL_MD5_GENERATE, IDM_TOOL_MD5_GENERATEFROMFILE, IDM_TOOL_MD5_GENERATEINTOCLIPBOARD},
        {HashType::Sha1, IDM_TOOL_SHA1_GENERATE, IDM_TOOL_SHA1_GENERATEFROMFILE, IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD},
        {HashType::Sha256, IDM_TOOL_SHA256_GENERATE, IDM_TOOL_SHA256_GENERATEFROMFILE, IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD},
        {HashType::Sha512, IDM_TOOL_SHA512_GENERATE, IDM_TOOL_SHA512_GENERATEFROMFILE, IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD},
    };
    for (const auto& h : hashes) {
        const int type = static_cast<int>(h.type);
        r->setHandler(h.text, {[this, type] { hashDialog(type, false); }});
        r->setHandler(h.files, {[this, type] { hashDialog(type, true); }});
        r->setHandler(h.clip, {[this, t = h.type] {
            ScintillaEditBase* e = currentPane()->editor();
            if (e->send(SCI_GETSELECTIONS) != 1 || e->send(SCI_GETSELECTIONEMPTY))
                return;
            const auto s = e->send(SCI_GETSELECTIONSTART), en = e->send(SCI_GETSELECTIONEND);
            const std::string sel = currentPane()->adapter().textRange(s, en);
            QApplication::clipboard()->setText(q(hashHex(t, sel)));
        }, [this] { return !currentPane()->editor()->send(SCI_GETSELECTIONEMPTY); }});
    }

    r->setHandler(IDM_VIEW_SUMMARY, {[this] {
        QMessageBox::information(this, miscString("summary", "Summary"), summaryText());
    }});
    r->setHandler(IDM_VIEW_MONITORING, {[this] {
        Buffer* b = currentBuffer();
        if (!b)
            return;
        if (b->monitoring) {
            setMonitoring(b, false);
            return;
        }
        if (b->isUntitled() || !QFileInfo::exists(q(b->path))) {
            const auto t = m_nativeLang.messageBox("DocNoExistToMonitor", "The file should exist to be monitored.", "Monitoring problem");
            QMessageBox::warning(this, q(t.title), q(t.message));
            return;
        }
        if (b->dirty) {
            const auto t = m_nativeLang.messageBox("DocTooDirtyToMonitor",
                                                   "The document is dirty. Please save the modification before monitoring it.",
                                                   "Monitoring problem");
            QMessageBox::warning(this, q(t.title), q(t.message));
            return;
        }
        setMonitoring(b, true);
    }, {}, [this] { const Buffer* b = currentBuffer(); return b && b->monitoring; }});

    r->setHandler(IDM_EXECUTE_VALIDATE_SHORTCUTSXML, {[this] {
        recordShortcutsXmlHmac();
        statusBar()->showMessage(tr("shortcuts.xml validated"), 3000);
    }});

    r->setHandler(IDM_LANG_OPENUDLDIR, {[this] {
        const QString dir = q(m_paths.userDefineLangsDir());
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }});
    r->setHandler(IDM_LANG_UDLCOLLECTION_PROJECT_SITE, {[] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/notepad-plus-plus/userDefinedLanguages")));
    }});
    r->setHandler(IDM_ONLINEDOCUMENT, {[] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://npp-user-manual.org/"))); }});
    r->setHandler(IDM_FORUM, {[] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://community.notepad-plus-plus.org/"))); }});
    // No auto-updater in this port (phase 6 packaging): the releases page instead; proxy settings are
    // only used by the upstream updater (WinGup).
    r->setHandler(IDM_UPDATE_NPP, {[] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/asprenas/notepad-plus-plus/releases")));
    }});
    r->setHandler(IDM_CONFUPDATERPROXY, {[this] {
        QMessageBox::information(this, tr("Set Updater Proxy"),
                                 tr("This version has no built-in updater: updates come from the release page or the system package manager, "
                                    "which use the system proxy settings."));
    }});
    r->setHandler(IDM_CMDLINEARGUMENTS, {[this] { showCommandLineHelp(); }});
    r->setHandler(IDM_DEBUGINFO, {[this] { showDebugInfo(); }});
}

QString MainWindow::commandLineHelpText()
{
    // upstream COMMAND_ARG_HELP (supported options of this port)
    return QStringLiteral(
        "Usage :\n"
        "\n"
        "notepad++ [--help] [-multiInst] [-noPlugin] [-lLanguage] [-udl=\"My UDL Name\"] [-LlangCode] [-nLineNumber] "
        "[-cColumnNumber] [-pPosition] [-xLeftPos] [-yTopPos] [-monitor] [-nosession] [-notabbar] [-ro] [-systemtray] "
        "[-loadingTime] [-alwaysOnTop] [-openSession] [-r] [-qn=\"Easter egg name\" | -qt=\"a text to display.\" | "
        "-qf=\"D:\\my quote.txt\"] [-qSpeed1|2|3] [-quickPrint] [-settingsDir=\"d:\\your settings dir\\\"] "
        "[-openFoldersAsWorkspace] [-titleAdd=\"additional title bar text\"] [filePath]\n"
        "\n"
        "--help : This help message\n"
        "-multiInst : Launch another Notepad++ instance\n"
        "-l : Open file or Ghost type with syntax highlighting of choice\n"
        "-n : Scroll to indicated line on filePath\n"
        "-c : Scroll to indicated column on filePath\n"
        "-p : Scroll to indicated position on filePath\n"
        "-x : Move Notepad++ to indicated left side position on the screen\n"
        "-y : Move Notepad++ to indicated top position on the screen\n"
        "-nosession : Launch Notepad++ without previous session\n"
        "-ro : Make the filePath read only\n"
        "-alwaysOnTop : Make Notepad++ always on top\n"
        "-openSession : Open a session. filePath must be a session file\n"
        "-settingsDir : Override the default settings dir\n"
        "filePath : file or folder name to open (absolute or relative path name)\n");
}

void MainWindow::showCommandLineHelp()
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("cmdLineArgs"));
    dlg.setWindowTitle(tr("Command Line Arguments"));
    auto* text = monoEdit(true);
    text->setPlainText(commandLineHelpText());
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    auto* ok = new QPushButton(tr("OK"));
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(text);
    layout->addWidget(ok, 0, Qt::AlignRight);
    dlg.resize(640, 480);
    QTimer::singleShot(0, &dlg, [this, &dlg] { m_openedDialog = &dlg; });
    dlg.exec();
    m_openedDialog = nullptr;
}

QString MainWindow::debugInfoText() const
{
    // upstream DebugInfoDlg
    QString s;
    s += QStringLiteral("Notepad++ Qt v%1   (%2 bit)\n").arg(QCoreApplication::applicationVersion()).arg(QSysInfo::WordSize);
    s += QStringLiteral("Build time : %1 - %2\n").arg(QStringLiteral(__DATE__), QStringLiteral(__TIME__));
    s += QStringLiteral("Upstream base : Notepad++ 8.9.8.1\n");
    s += QStringLiteral("Qt : %1 (built with %2)\n").arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR));
#if defined(__clang__)
    s += QStringLiteral("Built with : Clang %1\n").arg(QStringLiteral(__clang_version__));
#elif defined(_MSC_VER)
    s += QStringLiteral("Built with : MSVC %1\n").arg(_MSC_VER);
#elif defined(__GNUC__)
    s += QStringLiteral("Built with : GCC %1\n").arg(QStringLiteral(__VERSION__));
#endif
    s += QStringLiteral("Path : %1\n").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    s += QStringLiteral("Command Line : %1\n").arg(QCoreApplication::arguments().mid(1).join(QLatin1Char(' ')));
    s += QStringLiteral("Admin mode : %1\n").arg(QStringLiteral("OFF"));
    s += QStringLiteral("Local Conf mode : %1\n").arg(m_paths.isPortable() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    s += QStringLiteral("Cloud Config : %1\n").arg(m_paths.isCloud() ? q(m_paths.cloudDir()) : QStringLiteral("OFF"));
    s += QStringLiteral("Settings dir : %1\n").arg(QDir::toNativeSeparators(q(m_paths.userDir())));
    s += QStringLiteral("Docking backend : %1\n").arg(dockManager()->backendName());
    s += QStringLiteral("Platform : %1\n").arg(QGuiApplication::platformName());
    s += QStringLiteral("OS Name : %1\n").arg(QSysInfo::prettyProductName());
    s += QStringLiteral("OS Version : %1 (%2)\n").arg(QSysInfo::productVersion(), QSysInfo::kernelVersion());
    s += QStringLiteral("OS Architecture : %1\n").arg(QSysInfo::currentCpuArchitecture());
    s += QStringLiteral("Dark Mode : %1\n").arg(isDarkMode() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    QStringList plugins;
    for (const QFileInfo& d : QDir(q(pluginsDir())).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        plugins << d.fileName();
    s += QStringLiteral("Plugins : %1\n").arg(plugins.isEmpty() ? QStringLiteral("none") : plugins.join(QLatin1Char(' ')));
    return s;
}

void MainWindow::showDebugInfo()
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("DebugInfo"));
    dlg.setWindowTitle(tr("Debug Info"));
    auto* text = monoEdit(true);
    text->setPlainText(debugInfoText());
    auto* copy = new QPushButton(tr("Copy debug info into clipboard"));
    connect(copy, &QPushButton::clicked, &dlg, [text] { QApplication::clipboard()->setText(text->toPlainText()); });
    auto* ok = new QPushButton(tr("OK"));
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(copy);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(text);
    layout->addLayout(buttons);
    translateDialog(&dlg, dialogLanguage(), "DebugInfo");
    dlg.resize(640, 440);
    QTimer::singleShot(0, &dlg, [this, &dlg] { m_openedDialog = &dlg; });
    dlg.exec();
    m_openedDialog = nullptr;
}

} // namespace npp::ui
