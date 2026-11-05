#include "MainWindow.h"

#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "search/FindReplaceDialog.h"

#include "core/editor/Representations.h"
#include "core/editor/AutoCompletion.h"

#include "core/encoding/EncodingMapper.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeView>

#include <algorithm>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

constexpr char kPanelDocList[] = "npp.documentList";
constexpr char kPanelFolder[] = "npp.folderAsWorkspace";
constexpr int kMarginLineNumber = 0, kMarginBookmark = 1, kMarginFold = 2;
constexpr int kMarkBookmark = 20; // upstream MARK_BOOKMARK
constexpr int kMarkHideBegin = 19, kMarkHideEnd = 18; // MARK_HIDELINESBEGIN / END

QString q(const std::string& s) { return QString::fromStdString(s); }

std::string substituteFont(const std::string& name)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(name);
    return {};
#else
    if (name == "Courier New" || name == "Consolas" || name == "Lucida Console")
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family().toStdString();
    return {};
#endif
}

QString encodingLabel(const Buffer& b)
{
    if (b.encoding != -1) {
        const int idx = EncodingMapper::indexFromEncoding(b.encoding);
        if (idx >= 0) {
            const std::string_view aliases = EncodingMapper::table()[static_cast<size_t>(idx)].aliasList;
            return q(std::string(aliases.substr(0, aliases.find(' '))));
        }
        return QStringLiteral("CP%1").arg(b.encoding);
    }
    switch (b.uniMode) {
    case UniMode::Ansi:         return QStringLiteral("ANSI");
    case UniMode::Utf8Bom:      return QStringLiteral("UTF-8-BOM");
    case UniMode::Utf16BE:      return QStringLiteral("UTF-16 BE BOM");
    case UniMode::Utf16LE:      return QStringLiteral("UTF-16 LE BOM");
    case UniMode::Utf16BENoBom: return QStringLiteral("UTF-16 Big Endian");
    case UniMode::Utf16LENoBom: return QStringLiteral("UTF-16 Little Endian");
    default:                    return QStringLiteral("UTF-8");
    }
}

QString eolLabel(EolType e)
{
    switch (e) {
    case EolType::Windows: return QStringLiteral("Windows (CR LF)");
    case EolType::Mac:     return QStringLiteral("Macintosh (CR)");
    default:               return QStringLiteral("Unix (LF)");
    }
}

// Win32 VK code -> Scintilla key code for SCI_ASSIGNCMDKEY
int scintillaKey(int vk)
{
    switch (vk) {
    case 0x08: return SCK_BACK;   case 0x09: return SCK_TAB;    case 0x0D: return SCK_RETURN;
    case 0x1B: return SCK_ESCAPE; case 0x21: return SCK_PRIOR;  case 0x22: return SCK_NEXT;
    case 0x23: return SCK_END;    case 0x24: return SCK_HOME;   case 0x25: return SCK_LEFT;
    case 0x26: return SCK_UP;     case 0x27: return SCK_RIGHT;  case 0x28: return SCK_DOWN;
    case 0x2D: return SCK_INSERT; case 0x2E: return SCK_DELETE; case 0x6B: return SCK_ADD;
    case 0x6D: return SCK_SUBTRACT; case 0x6F: return SCK_DIVIDE;
    case 0xBA: return ';'; case 0xBB: return '='; case 0xBC: return ','; case 0xBD: return '-';
    case 0xBE: return '.'; case 0xBF: return '/'; case 0xC0: return '`'; case 0xDB: return '[';
    case 0xDC: return '\\'; case 0xDD: return ']'; case 0xDE: return '\'';
    default:
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == ' ')
            return vk;
        return 0;
    }
}

struct SciKey {
    int sci;
    bool ctrl, alt, shift;
    int vk;
};

const std::vector<SciKey>& scintillaKeyDefaults()
{
    static const std::vector<SciKey> keys = {
#define NPP_SCIKEY(name, sci, c, a, s, vk, id) {sci, c, a, s, vk},
#include "menu/ScintillaKeys.inc"
#undef NPP_SCIKEY
    };
    return keys;
}

// Status bar cell with a double click action (upstream: language / EOL / encoding menus, ...)
class StatusCell final : public QLabel {
public:
    std::function<void(QPoint)> onDoubleClick;

protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (onDoubleClick)
            onDoubleClick(e->globalPosition().toPoint());
    }
};

// Menu that contains the action of a command (to pop it up from the status bar)
QMenu* menuOf(QAction* a)
{
    for (QObject* o : a->associatedObjects())
        if (auto* m = qobject_cast<QMenu*>(o))
            return m;
    return nullptr;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Command line (upstream CmdLineParams)
// ------------------------------------------------------------------------------------------------

CommandLine parseCommandLine(const QStringList& args)
{
    CommandLine c;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        auto num = [&a](int prefix) { return a.mid(prefix).toInt(); };
        if (a.startsWith(QLatin1String("-n")) && a.size() > 2 && a[2].isDigit()) c.line = num(2);
        else if (a.startsWith(QLatin1String("-c")) && a.size() > 2 && a[2].isDigit()) c.column = num(2);
        else if (a.startsWith(QLatin1String("-p")) && a.size() > 2 && a[2].isDigit()) c.position = num(2);
        else if (a.startsWith(QLatin1String("-l")) && a.size() > 2) c.language = a.mid(2);
        else if (a.startsWith(QLatin1String("-x")) && a.size() > 2) c.x = num(2);
        else if (a.startsWith(QLatin1String("-y")) && a.size() > 2) c.y = num(2);
        else if (a == QLatin1String("-multiInst")) c.multiInstance = true;
        else if (a == QLatin1String("-nosession")) c.noSession = true;
        else if (a == QLatin1String("-ro")) c.readOnly = true;
        else if (a == QLatin1String("-alwaysOnTop")) c.alwaysOnTop = true;
        else if (a == QLatin1String("-openSession") && i + 1 < args.size()) c.sessionFile = args[++i];
        else if (a.startsWith(QLatin1String("-settingsDir="))) c.settingsDir = a.mid(13);
        else if (a.startsWith(QLatin1Char('-')) && !QFileInfo::exists(a)) continue; // unknown / unsupported option
        else c.files << a;
    }
    return c;
}

// ------------------------------------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------------------------------------

MainWindow::MainWindow(const CommandLine& cmd, QWidget* parent) : QMainWindow(parent), m_cmd(cmd)
{
    setAcceptDrops(true);
    loadSettings();
    createUi();
    registerCommands();
    restoreWindowState();
    connectSystemTheme();
    applyTheme();

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
        QTimer::singleShot(200, this, &MainWindow::checkFilesystemChanges); // writers often touch twice
    });
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive)
            checkFilesystemChanges();
    });

    m_backupTimer = new QTimer(this);
    connect(m_backupTimer, &QTimer::timeout, this, &MainWindow::backupDirtyBuffers);
    if (m_config.gui.snapshotMode)
        m_backupTimer->start(std::max(1000, m_config.gui.snapshotBackupTiming));

    if (!cmd.sessionFile.isEmpty())
        loadSessionFrom(cmd.sessionFile);
    else if (m_config.gui.rememberLastSession && !cmd.noSession)
        if (const auto s = loadSession(m_paths.sessionXml()))
            restoreSession(*s);
    if (cmd.alwaysOnTop)
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    openFromCommandLine(cmd);
    ensureDocument();
}

MainWindow::~MainWindow()
{
    for (auto& ac : m_autoCompletion)
        ac.reset();
    // Views drop their documents before the FileManager releases them.
    for (EditorPane* p : m_panes)
        delete p;
    m_panes = {};
    m_files.reset();
    m_scratch.reset();
    delete m_scratchWidget;
}

void MainWindow::loadSettings()
{
    // Application data (model files, UDLs shipped next to the executable)
    QStringList candidates;
    if (const QString env = qEnvironmentVariable("NPP_DATA_DIR"); !env.isEmpty())
        candidates << env;
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/data")
               << QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/data");
    for (const QString& d : candidates)
        if (QFileInfo::exists(d + QStringLiteral("/langs.model.xml"))) {
            m_dataDir = QDir(d).absolutePath().toStdString();
            break;
        }

    AppPaths::Options po;
    po.appDir = QCoreApplication::applicationDirPath().toStdString();
    po.settingsDirArg = m_cmd.settingsDir.toStdString();
    if (const QString test = qEnvironmentVariable("NPP_QT_USER_DIR"); !test.isEmpty())
        po.userDirOverride = test.toStdString(); // tests
    m_paths = AppPaths::resolve(po);
    importNotepadPlusPlusSettings();
    m_config.load(m_paths.configXml());

    auto pick = [this](const std::string& user, const char* model) {
        return pathExists(user, PathKind::File) ? user : pathAppend(m_dataDir, model);
    };
    m_currentThemePath = pick(m_paths.stylersXml(), "stylers.model.xml");
    if (!m_langs.load(pick(m_paths.langsXml(), "langs.model.xml")) || !m_styles.load(m_currentThemePath))
        QMessageBox::warning(this, QStringLiteral("Notepad++"), tr("langs.xml / stylers.xml could not be loaded."));
    for (const auto& ls : m_styles.lexers())
        if (!ls.ext.empty())
            if (const auto t = langTypeFromName(ls.name))
                m_langs.addUserExtensions(*t, ls.ext);
    m_udls.loadAll(m_paths.userDefineLangXml(), m_paths.userDefineLangsDir());
    if (m_udls.languages().empty())
        m_udls.loadAll({}, pathAppend(m_dataDir, "userDefineLangs"));
    m_nativeLang.loadFile(m_paths.nativeLangXml());
    if (m_nativeLang.isLoaded()) // which localization file (nativeLang.xml is a copy of it)
        for (const auto& l : NativeLanguage::availableLanguages(pathAppend(m_dataDir, "localization")))
            if (l.name == m_nativeLang.languageName())
                m_currentLanguageFile = l.fileName;
    m_shortcuts.load(m_paths.shortcutsXml());
    loadQtTranslation();
    ensureContextMenuFile();

    const GuiConfig& g = m_config.gui;
    m_applyOptions.sqlBackslashEscape = g.backslashIsEscapeForSql;
    m_applyOptions.showCcUniEol = g.ccUniEolShow;
    m_applyOptions.override = {g.globalOverrideFg, g.globalOverrideBg, g.globalOverrideFont, g.globalOverrideFontSize,
                               g.globalOverrideBold, g.globalOverrideItalic, g.globalOverrideUnderline};
    m_applyOptions.fontSubstitute = &substituteFont;

    m_scratchWidget = new ScintillaEditBase;
    m_scratch = std::make_unique<EditorAdapter>(m_scratchWidget);
    FileManagerSettings fs;
    fs.detectEncoding = g.detectEncoding;
    fs.openAnsiAsUtf8 = g.newDocOpenAnsiAsUtf8;
    fs.newDocUniMode = static_cast<UniMode>(g.newDocEncoding);
    fs.newDocEncoding = g.newDocCodepage;
    fs.newDocEol = static_cast<EolType>(std::clamp(g.newDocFormat, 0, 2));
    fs.newDocLang = langTypeFromName(langInfo(static_cast<LangType>(std::clamp(g.newDocLang, 0, kLangCount - 1))).name)
                        .value_or(L_TEXT);
    fs.largeFileSizeBytes = static_cast<uint64_t>(std::max(1, g.largeFileSizeMB)) * 1024 * 1024;
    fs.untitledPrefix = m_nativeLang.localizedString("tab-untitled-string", "new ");
    m_files = std::make_unique<FileManager>(*m_scratch, fs, &m_langs, &m_udls);
}

// First start: copy the settings of an installed Notepad++ so the user keeps languages, styles,
// shortcuts, UDLs and translation (session/backups are not copied: they point to Notepad++ data).
void MainWindow::importNotepadPlusPlusSettings()
{
    if (m_paths.isPortable() || pathExists(m_paths.configXml(), PathKind::File) || qEnvironmentVariableIsSet("NPP_QT_USER_DIR"))
        return; // tests use their own user directory
#if defined(Q_OS_WIN)
    const char* appData = std::getenv("APPDATA");
    if (!appData)
        return;
    const std::string src = pathAppend(appData, "Notepad++");
    if (!pathExists(pathAppend(src, "config.xml"), PathKind::File))
        return;
    std::error_code ec;
    fs::create_directories(pathFromUtf8(m_paths.userDir()), ec);
    for (const char* f : {"config.xml", "langs.xml", "stylers.xml", "shortcuts.xml", "contextMenu.xml",
                          "userDefineLang.xml", "nativeLang.xml"})
        fs::copy_file(pathFromUtf8(pathAppend(src, f)), pathFromUtf8(pathAppend(m_paths.userDir(), f)),
                      fs::copy_options::skip_existing, ec);
    for (const char* d : {"userDefineLangs", "themes"})
        fs::copy(pathFromUtf8(pathAppend(src, d)), pathFromUtf8(pathAppend(m_paths.userDir(), d)),
                 fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
#endif
}

void MainWindow::createUi()
{
    m_actions = new ActionRegistry(this);
    m_dock = createDockManager(this);

    m_splitter = new QSplitter(m_config.gui.splitterVertical ? Qt::Horizontal : Qt::Vertical);
    for (int v = 0; v < 2; ++v) {
        auto* p = new EditorPane(v);
        m_panes[static_cast<size_t>(v)] = p;
        m_splitter->addWidget(p);
        applyEditorSettings(p);
        connect(p, &EditorPane::currentBufferChanged, this, [this, p](BufferId id) { onCurrentBufferChanged(p, id); });
        connect(p, &EditorPane::closeRequested, this, [this, p](BufferId id) { closeBuffer(id, p); });
        connect(p, &EditorPane::newDocumentRequested, this, [this, p] { showBuffer(m_files->newDocument(), p); });
        connect(p, &EditorPane::focused, this, [this, p](int) { setActivePane(p); });
        connect(p, &EditorPane::editorContextMenuRequested, this, [this, p](const QPoint& at) { showEditorContextMenu(p, at); });
        connect(p, &EditorPane::tabContextMenuRequested, this,
                [this, p](int i, const QPoint& at) { showTabContextMenu(p, i, at); });
        ScintillaEditBase* e = p->editor();
        connect(e, &ScintillaEditBase::savePointChanged, this, [this, p](bool dirty) {
            if (Buffer* b = buffer(p->current())) {
                b->dirty = dirty;
                refreshBuffer(b);
            }
        });
        connect(e, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update) { updateTitleAndStatus(); });
        connectHighlighters(p);
        connect(e, &ScintillaEditBase::modifyAttemptReadOnly, this, [this] {
            statusBar()->showMessage(tr("The document is read-only."), 2000);
        });
        connect(e, &ScintillaEditBase::uriDropped, this, [this](const QString& uri) { openFile(QUrl(uri).toLocalFile()); });
        connect(e, &ScintillaEditBase::marginClicked, this, [e](Scintilla::Position pos, Scintilla::KeyMod, int margin) {
            if (margin != kMarginBookmark)
                return;
            const auto line = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos));
            const auto markers = e->send(SCI_MARKERGET, static_cast<uptr_t>(line));
            if (markers & ((1 << kMarkHideBegin) | (1 << kMarkHideEnd))) { // show the hidden block
                const bool begin = markers & (1 << kMarkHideBegin);
                const auto other = e->send(begin ? SCI_MARKERNEXT : SCI_MARKERPREVIOUS, static_cast<uptr_t>(begin ? line + 1 : line - 1),
                                           1 << (begin ? kMarkHideEnd : kMarkHideBegin));
                if (other >= 0) {
                    const auto first = begin ? line : other, last = begin ? other : line;
                    e->send(SCI_SHOWLINES, static_cast<uptr_t>(first + 1), last - 1);
                    e->send(SCI_MARKERDELETE, static_cast<uptr_t>(first), kMarkHideBegin);
                    e->send(SCI_MARKERDELETE, static_cast<uptr_t>(last), kMarkHideEnd);
                }
                return;
            }
            if (e->send(SCI_MARKERGET, static_cast<uptr_t>(line)) & (1 << kMarkBookmark))
                e->send(SCI_MARKERDELETE, static_cast<uptr_t>(line), kMarkBookmark);
            else
                e->send(SCI_MARKERADD, static_cast<uptr_t>(line), kMarkBookmark);
        });
    }
    m_panes[1]->hide();
    m_dock->setCentralWidget(m_splitter);

    createPanels();
    menuBar()->setVisible(m_config.gui.menuBarShow);
    createStatusBar();
}

void MainWindow::createPanels()
{
    m_docList = new QListWidget;
    connect(m_docList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const BufferId id = item->data(Qt::UserRole).toULongLong();
        const int view = item->data(Qt::UserRole + 1).toInt();
        m_panes[static_cast<size_t>(view)]->activate(id);
        setActivePane(m_panes[static_cast<size_t>(view)]);
    });
    m_dock->addPanel({kPanelDocList, tr("Document List"), {}, DockArea::Left, true, false}, m_docList);

    auto* model = new QFileSystemModel(this);
    model->setRootPath(QDir::homePath());
    auto* tree = new QTreeView;
    tree->setModel(model);
    tree->setRootIndex(model->index(QDir::homePath()));
    for (int c = 1; c < model->columnCount(); ++c)
        tree->hideColumn(c);
    tree->header()->hide();
    m_setWorkspaceRoot = [this, model, tree](const QString& dir) {
        model->setRootPath(dir);
        tree->setRootIndex(model->index(dir));
        m_dock->showPanel(kPanelFolder);
    };
    connect(tree, &QTreeView::doubleClicked, this, [this, model](const QModelIndex& idx) {
        if (!model->isDir(idx))
            openFile(model->filePath(idx));
    });
    m_dock->addPanel({kPanelFolder, tr("Folder as Workspace"), {}, DockArea::Left, true, false}, tree);
    createExtraPanels();
}

void MainWindow::createStatusBar()
{
    std::array<StatusCell*, 6> cells{};
    for (size_t i = 0; i < cells.size(); ++i) {
        cells[i] = new StatusCell;
        cells[i]->setContentsMargins(6, 0, 6, 0);
        m_status[i] = cells[i];
    }
    auto popup = [this](int commandId) {
        return [this, commandId](QPoint at) {
            if (QMenu* m = menuOf(m_actions->action(commandId))) {
                m_actions->updateStates();
                m->popup(at);
            }
        };
    };
    cells[0]->onDoubleClick = popup(IDM_LANG_TEXT);
    cells[2]->onDoubleClick = [this](QPoint) { m_actions->trigger(IDM_SEARCH_GOTOLINE); };
    cells[3]->onDoubleClick = popup(IDM_FORMAT_TODOS);
    cells[4]->onDoubleClick = popup(IDM_FORMAT_ANSI);
    cells[5]->onDoubleClick = [this](QPoint) {
        ScintillaEditBase* e = currentPane()->editor();
        e->send(SCI_SETOVERTYPE, !e->send(SCI_GETOVERTYPE));
        updateTitleAndStatus();
    };
    statusBar()->addWidget(m_status[0], 1);
    for (size_t i = 1; i < m_status.size(); ++i)
        statusBar()->addPermanentWidget(m_status[i]);
    statusBar()->setVisible(m_config.gui.statusBarShow);
}

void MainWindow::applyScintillaKeys(ScintillaEditBase* e)
{
    auto def = [](const KeyCombo& k) {
        const int key = scintillaKey(k.key);
        int mods = (k.ctrl ? SCMOD_CTRL : 0) | (k.alt ? SCMOD_ALT : 0) | (k.shift ? SCMOD_SHIFT : 0);
        return key ? key + (mods << 16) : 0;
    };
    // Upstream defaults first, then shortcuts.xml <ScintillaKeys> overrides
    for (const SciKey& k : scintillaKeyDefaults())
        if (const int d = def({k.ctrl, k.alt, k.shift, k.vk}))
            e->send(SCI_ASSIGNCMDKEY, static_cast<uptr_t>(d), k.sci);
    for (const auto& sk : m_shortcuts.scintillaKeys) {
        if (sk.combos.empty() || !sk.combos.front().isEnabled()) // shortcut removed in the Shortcut Mapper
            for (const SciKey& k : scintillaKeyDefaults())
                if (k.sci == sk.scintId)
                    if (const int d = def({k.ctrl, k.alt, k.shift, k.vk}))
                        e->send(SCI_CLEARCMDKEY, static_cast<uptr_t>(d));
        for (const KeyCombo& k : sk.combos)
            if (const int d = def(k))
                e->send(SCI_ASSIGNCMDKEY, static_cast<uptr_t>(d), sk.scintId);
    }
}

void MainWindow::applyEditorSettings(EditorPane* pane)
{
    ScintillaEditBase* e = pane->editor();
    const GuiConfig& g = m_config.gui;
    e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    e->send(SCI_SETTABWIDTH, static_cast<uptr_t>(g.tabSize));
    e->send(SCI_SETUSETABS, !g.tabReplacedBySpace);
    e->send(SCI_SETBACKSPACEUNINDENTS, g.backspaceUnindent);
    e->send(SCI_SETCARETLINEVISIBLE, g.currentLineIndicator == 1);
    e->send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    e->send(SCI_SETCARETLINEFRAME, g.currentLineIndicator == 2 ? static_cast<uptr_t>(g.currentLineFrameWidth) : 0);
    e->send(SCI_SETMULTIPLESELECTION, g.multiSelection);
    e->send(SCI_SETADDITIONALSELECTIONTYPING, g.multiSelection);
    e->send(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    e->send(SCI_SETIMEINTERACTION, SC_IME_INLINE);
    e->send(SCI_SETWRAPMODE, g.wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
    e->send(SCI_SETWRAPINDENTMODE, g.lineWrapMethod == "indent" ? SC_WRAPINDENT_INDENT
                                   : g.lineWrapMethod == "aligned" ? SC_WRAPINDENT_SAME : SC_WRAPINDENT_FIXED);
    e->send(SCI_SETWRAPVISUALFLAGS, g.wrapSymbolShow ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
    e->send(SCI_SETVIEWWS, g.whiteSpaceShow ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    e->send(SCI_SETVIEWEOL, g.eolShow);
    e->send(SCI_SETINDENTATIONGUIDES, g.indentGuideLine ? SC_IV_LOOKBOTH : SC_IV_NONE);
    e->send(SCI_SETZOOM, static_cast<uptr_t>(pane->viewIndex() == 0 || g.zoomSync ? g.zoom : g.zoom2));
    e->send(SCI_SETCARETWIDTH, static_cast<uptr_t>(g.caretWidth));
    e->send(SCI_SETCARETPERIOD, static_cast<uptr_t>(g.caretBlinkRate));
    e->send(SCI_SETVIRTUALSPACEOPTIONS, g.virtualSpace ? SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE : SCVS_RECTANGULARSELECTION);
    e->send(SCI_SETENDATLASTLINE, !g.scrollBeyondLastLine);
    e->send(SCI_SETMARGINLEFT, 0, g.paddingLeft);
    e->send(SCI_SETMARGINRIGHT, 0, g.paddingRight);
    e->send(SCI_SETSCROLLWIDTHTRACKING, 1);
    const auto edges = tokenize(g.edgeMultiColumnPos, ' ');
    if (!edges.empty()) {
        e->send(SCI_SETEDGEMODE, edges.size() > 1 ? EDGE_MULTILINE : (g.edgeBgMode ? EDGE_BACKGROUND : EDGE_LINE));
        if (edges.size() == 1)
            e->send(SCI_SETEDGECOLUMN, static_cast<uptr_t>(std::atoi(edges[0].c_str())));
        else
            for (const auto& c : edges)
                e->send(SCI_MULTIEDGEADDLINE, static_cast<uptr_t>(std::atoi(c.c_str())), 0xC0C0C0);
    }

    e->send(SCI_SETMARGINTYPEN, kMarginLineNumber, SC_MARGIN_NUMBER);
    e->send(SCI_SETMARGINWIDTHN, kMarginLineNumber, g.lineNumberMargin ? 48 : 0);
    e->send(SCI_SETMARGINTYPEN, kMarginBookmark, SC_MARGIN_SYMBOL);
    e->send(SCI_SETMARGINMASKN, kMarginBookmark, (1 << kMarkBookmark) | (1 << kMarkHideBegin) | (1 << kMarkHideEnd));
    e->send(SCI_SETMARGINWIDTHN, kMarginBookmark, g.bookmarkMargin ? 16 : 0);
    e->send(SCI_SETMARGINSENSITIVEN, kMarginBookmark, 1);
    e->send(SCI_MARKERDEFINE, kMarkBookmark, SC_MARK_BOOKMARK);
    e->send(SCI_MARKERSETBACK, kMarkBookmark, 0xFF8000);
    e->send(SCI_SETMARGINTYPEN, kMarginFold, SC_MARGIN_SYMBOL);
    e->send(SCI_SETMARGINMASKN, kMarginFold, SC_MASK_FOLDERS);
    e->send(SCI_SETMARGINWIDTHN, kMarginFold, g.folderMarkStyle == "none" ? 0 : 14);
    e->send(SCI_SETMARGINSENSITIVEN, kMarginFold, 1);
    e->send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    const bool circle = g.folderMarkStyle == "circle", arrow = g.folderMarkStyle == "arrow",
               simple = g.folderMarkStyle == "simple", flat = arrow || simple;
    const std::pair<int, int> markers[] = {
        {SC_MARKNUM_FOLDEROPEN, circle ? SC_MARK_CIRCLEMINUS : arrow ? SC_MARK_ARROWDOWN : simple ? SC_MARK_MINUS : SC_MARK_BOXMINUS},
        {SC_MARKNUM_FOLDER, circle ? SC_MARK_CIRCLEPLUS : arrow ? SC_MARK_ARROW : simple ? SC_MARK_PLUS : SC_MARK_BOXPLUS},
        {SC_MARKNUM_FOLDERSUB, flat ? SC_MARK_EMPTY : SC_MARK_VLINE},
        {SC_MARKNUM_FOLDERTAIL, flat ? SC_MARK_EMPTY : circle ? SC_MARK_LCORNERCURVE : SC_MARK_LCORNER},
        {SC_MARKNUM_FOLDEREND, circle ? SC_MARK_CIRCLEPLUSCONNECTED : flat ? SC_MARK_EMPTY : SC_MARK_BOXPLUSCONNECTED},
        {SC_MARKNUM_FOLDEROPENMID, circle ? SC_MARK_CIRCLEMINUSCONNECTED : flat ? SC_MARK_EMPTY : SC_MARK_BOXMINUSCONNECTED},
        {SC_MARKNUM_FOLDERMIDTAIL, flat ? SC_MARK_EMPTY : circle ? SC_MARK_TCORNERCURVE : SC_MARK_TCORNER},
    };
    for (auto [num, sym] : markers) {
        e->send(SCI_MARKERDEFINE, static_cast<uptr_t>(num), sym);
        e->send(SCI_MARKERSETFORE, static_cast<uptr_t>(num), 0xFFFFFF);
        e->send(SCI_MARKERSETBACK, static_cast<uptr_t>(num), 0x808080);
    }
    applyIndicatorStyles(e);
    applyRepresentations(pane);
    applyScintillaKeys(e);
}

void MainWindow::applyRepresentations(EditorPane* pane)
{
    const GuiConfig& g = m_config.gui;
    RepresentationOptions o;
    o.eolMode = g.eolMode;
    o.npcShow = g.npcShow;
    o.npcMode = g.npcMode;
    o.npcCustomColor = g.npcCustomColor;
    o.npcIncludeCcUniEol = g.npcIncludeCcUniEol;
    o.ccUniEolShow = g.ccUniEolShow;
    o.colorsFrom(m_styles);
    core::applyRepresentations(pane->adapter(), o);
}

void MainWindow::restoreWindowState()
{
    const GuiConfig& g = m_config.gui;
    if (g.appWidth > 100 && g.appHeight > 100) {
        QRect r(g.appX, g.appY, g.appWidth, g.appHeight);
        if (!QGuiApplication::screenAt(r.center()))
            r.moveCenter(QGuiApplication::primaryScreen()->availableGeometry().center());
        setGeometry(r);
    }
    if (m_cmd.x != INT_MIN || m_cmd.y != INT_MIN) // -x / -y
        move(m_cmd.x != INT_MIN ? m_cmd.x : x(), m_cmd.y != INT_MIN ? m_cmd.y : y());
    else if (g.appMaximized)
        setWindowState(windowState() | Qt::WindowMaximized);
    // Panel layout is kept by the docking backend next to config.xml
    if (const auto st = readFileContent(pathAppend(m_paths.userDir(), "dockLayout-" + m_dock->backendName().toStdString() + ".bin")))
        m_dock->restoreNativeState(QByteArray::fromStdString(*st));
}

void MainWindow::saveSettings()
{
    GuiConfig& g = m_config.gui;
    const QRect r = normalGeometry();
    g.appX = r.x();
    g.appY = r.y();
    g.appWidth = r.width();
    g.appHeight = r.height();
    g.appMaximized = isMaximized();
    g.splitterVertical = m_splitter->orientation() == Qt::Horizontal;
    g.zoom = static_cast<int>(m_panes[0]->editor()->send(SCI_GETZOOM));
    // upstream Notepad_plus.cpp saveGUIParams: with zoom sync the second view stores the main view zoom
    g.zoom2 = g.zoomSync ? g.zoom : static_cast<int>(m_panes[1]->editor()->send(SCI_GETZOOM));
    g.statusBarShow = statusBar()->isVisible();
    g.menuBarShow = menuBar()->isVisible();
    if (m_findDialog)
        m_findDialog->saveHistory(m_config.findHistory);
    std::error_code ec;
    fs::create_directories(pathFromUtf8(m_paths.userDir()), ec);
    m_config.save(m_paths.configXml());
    writeFileContent(pathAppend(m_paths.userDir(), "dockLayout-" + m_dock->backendName().toStdString() + ".bin"),
                     m_dock->saveNativeState().toStdString());
}

// ------------------------------------------------------------------------------------------------
// Documents
// ------------------------------------------------------------------------------------------------

EditorPane* MainWindow::currentPane() const { return m_panes[static_cast<size_t>(m_activeView)]; }
EditorPane* MainWindow::otherPane() const { return m_panes[static_cast<size_t>(1 - m_activeView)]; }

Buffer* MainWindow::buffer(BufferId id) const { return m_files ? m_files->find(id) : nullptr; }
Buffer* MainWindow::currentBuffer() const { return buffer(currentPane()->current()); }

EditorPane* MainWindow::paneShowing(BufferId id) const
{
    if (currentPane()->indexOf(id) >= 0)
        return currentPane();
    if (otherPane()->indexOf(id) >= 0)
        return otherPane();
    return nullptr;
}

std::vector<std::pair<BufferId, EditorPane*>> MainWindow::allTabs() const
{
    std::vector<std::pair<BufferId, EditorPane*>> out;
    for (EditorPane* p : m_panes)
        for (BufferId id : p->buffers())
            out.emplace_back(id, p);
    return out;
}

void MainWindow::ensureDocument()
{
    if (m_panes[0]->count() == 0 && m_panes[1]->count() == 0) {
        Buffer* b = m_files->newDocument();
        if (m_config.gui.newDocLang > 0)
            b->lang = static_cast<LangType>(std::clamp(m_config.gui.newDocLang, 0, kLangCount - 1));
        showBuffer(b, m_panes[0]);
    }
}

void MainWindow::showBuffer(Buffer* b, EditorPane* pane, bool activateIt)
{
    if (!b)
        return;
    pane->addBuffer(*b, activateIt);
    if (pane->isHidden())
        pane->show();
    refreshBuffer(b);
    watchFile(b->path, true);
    if (activateIt)
        setActivePane(pane);
}

void MainWindow::setActivePane(EditorPane* pane)
{
    const bool changed = m_activeView != pane->viewIndex();
    m_activeView = pane->viewIndex();
    if (changed)
        panelsOnBufferChanged();
    if (!pane->editor()->hasFocus() && isVisible())
        pane->editor()->setFocus();
    updateTitleAndStatus();
}

void MainWindow::onCurrentBufferChanged(EditorPane* pane, BufferId id)
{
    Buffer* b = buffer(id);
    if (b) {
        applyLanguageToPane(pane, b);
        pane->editor()->send(SCI_SETEOLMODE, static_cast<uptr_t>(scintillaEolMode(b->eol)));
        pane->editor()->send(SCI_SETREADONLY, b->isReadOnly());
    }
    if (pane->count() == 0 && pane->viewIndex() == 1)
        pane->hide();
    updateTitleAndStatus();
    if (m_docList) { // Document List panel
        QSignalBlocker block(m_docList);
        m_docList->clear();
        for (const auto& [tid, p] : allTabs())
            if (Buffer* tb = buffer(tid)) {
                auto* item = new QListWidgetItem(tabText(*tb), m_docList);
                item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(tid));
                item->setData(Qt::UserRole + 1, p->viewIndex());
                if (p == currentPane() && tid == p->current())
                    m_docList->setCurrentItem(item);
            }
    }
    panelsOnBufferChanged();
}

void MainWindow::applyLanguageToPane(EditorPane* pane, Buffer* b)
{
    const UserLanguage* udl = b->lang == L_USER ? m_udls.find(b->userLangName) : nullptr;
    if (udl)
        applyUserLanguage(pane->adapter(), *udl, m_styles, m_applyOptions, b->id);
    else
        npp::core::applyLanguage(pane->adapter(), b->lang == L_USER ? L_TEXT : b->lang, m_langs, m_styles, m_applyOptions);
    ScintillaEditBase* e = pane->editor();
    if (const Style* s = m_styles.globalStyleByName("Current line background colour"); s && s->bg >= 0)
        e->send(SCI_SETCARETLINEBACK, static_cast<uptr_t>(Style::scintillaColour(s->bg)));
    if (const Style* s = m_styles.globalStyleByName("Selected text colour"); s && s->bg >= 0)
        e->send(SCI_SETSELBACK, 1, Style::scintillaColour(s->bg));
    if (const Style* s = m_styles.globalStyleByName("Caret colour"); s && s->fg >= 0)
        e->send(SCI_SETCARETFORE, static_cast<uptr_t>(Style::scintillaColour(s->fg)));
    if (const Style* s = m_styles.globalStyleByName("Fold margin"); s) {
        if (s->fg >= 0) e->send(SCI_SETFOLDMARGINHICOLOUR, 1, Style::scintillaColour(s->fg));
        if (s->bg >= 0) e->send(SCI_SETFOLDMARGINCOLOUR, 1, Style::scintillaColour(s->bg));
    }
    updateAutoCompletionLanguage(pane);
    const LanguageDef* def = m_langs.find(b->lang);
    if (def && def->tabSettings >= 0) { // per-language tab settings (langs.xml tabSettings)
        e->send(SCI_SETTABWIDTH, static_cast<uptr_t>(def->tabSettings & 0x7F));
        e->send(SCI_SETUSETABS, !(def->tabSettings & 0x80));
    } else {
        e->send(SCI_SETTABWIDTH, static_cast<uptr_t>(m_config.gui.tabSize));
        e->send(SCI_SETUSETABS, !m_config.gui.tabReplacedBySpace);
    }
}

void MainWindow::setLanguage(Buffer* b, LangType lang, const std::string& udl)
{
    if (!b)
        return;
    b->lang = lang;
    b->userLangName = lang == L_USER ? udl : std::string();
    for (EditorPane* p : m_panes)
        if (p->current() == b->id)
            applyLanguageToPane(p, b);
    updateTitleAndStatus();
    panelsOnBufferChanged();
}

QString MainWindow::miscString(const char* strId, const char* fallback) const
{
    return q(m_nativeLang.localizedString(strId, fallback));
}

QString MainWindow::tabText(const Buffer& b) const
{
    QString t = q(b.displayName());
    t.replace(QLatin1Char('&'), QLatin1String("&&"));
    if (b.dirty)
        t.prepend(QLatin1Char('*'));
    if (b.isReadOnly())
        t.append(QStringLiteral(" 🔒"));
    return t;
}

void MainWindow::refreshBuffer(Buffer* b)
{
    if (!b)
        return;
    for (EditorPane* p : m_panes)
        p->setTabText(b->id, tabText(*b), b->isUntitled() ? QString() : q(b->path));
    refreshTabDecorations(*b);
    updateTitleAndStatus();
}

void MainWindow::updateTitleAndStatus()
{
    Buffer* b = currentBuffer();
    if (!b || !m_status[0])
        return;
    const QString name = b->isUntitled() ? q(b->untitledName) : (m_config.gui.shortTitlebar ? q(b->displayName()) : q(b->path));
    setWindowTitle(QStringLiteral("%1%2 - Notepad++").arg(b->dirty ? QStringLiteral("*") : QString(), name));
    ScintillaEditBase* e = currentPane()->editor();
    m_status[0]->setText(b->lang == L_USER ? q(b->userLangName) : q(std::string(langInfo(b->lang).longName)));
    m_status[1]->setText(tr("length : %1    lines : %2")
                             .arg(QLocale().toString(static_cast<qlonglong>(e->send(SCI_GETLENGTH))))
                             .arg(QLocale().toString(static_cast<qlonglong>(e->send(SCI_GETLINECOUNT)))));
    const auto pos = e->send(SCI_GETCURRENTPOS);
    const auto selChars = e->send(SCI_COUNTCHARACTERS, static_cast<uptr_t>(e->send(SCI_GETSELECTIONSTART)), e->send(SCI_GETSELECTIONEND));
    m_status[2]->setText(tr("Ln : %1    Col : %2    Pos : %3    Sel : %4")
                             .arg(e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos)) + 1)
                             .arg(e->send(SCI_GETCOLUMN, static_cast<uptr_t>(pos)) + 1)
                             .arg(pos + 1)
                             .arg(selChars));
    m_status[3]->setText(eolLabel(b->eol));
    m_status[4]->setText(encodingLabel(*b));
    m_status[5]->setText(e->send(SCI_GETOVERTYPE) ? QStringLiteral("OVR") : QStringLiteral("INS"));
}

Buffer* MainWindow::openFile(const QString& rawPath, const LoadOptions& o, bool activateIt)
{
    if (rawPath.isEmpty())
        return nullptr;
    const QFileInfo fi(rawPath);
    if (fi.isDir()) { // upstream opens all files of a dropped folder (isFolderDroppedOpenFiles)
        const QDir d(rawPath);
        for (const QFileInfo& f : d.entryInfoList(QDir::Files))
            openFile(f.absoluteFilePath(), o, activateIt);
        return nullptr;
    }
    const std::string path = absolutePath(rawPath.toStdString());
    if (Buffer* existing = m_files->findByPath(path)) {
        EditorPane* p = paneShowing(existing->id);
        if (!p) {
            showBuffer(existing, currentPane(), activateIt);
        } else if (activateIt) {
            p->activate(existing->id);
            setActivePane(p);
        }
        return existing;
    }
    if (!fi.exists()) {
        const auto t = m_nativeLang.messageBox("CreateNewFileOrNot", "\"$STR_REPLACE$\" doesn't exist. Create it?",
                                               "Create new file", 0, path);
        if (QMessageBox::question(this, q(t.title), q(t.message)) != QMessageBox::Yes)
            return nullptr;
        if (!writeFileContent(path, {})) {
            QMessageBox::warning(this, QStringLiteral("Notepad++"), tr("Cannot create the file \"%1\".").arg(q(path)));
            return nullptr;
        }
    }
    const uint64_t size = static_cast<uint64_t>(fi.size());
    if (size >= m_files->settings().largeFileSizeBytes && size > 2ull * 1024 * 1024 * 1024 &&
        !m_config.gui.largeFileSuppress2GBWarning) {
        const auto t = m_nativeLang.messageBox("WantToOpenHugeFile", "Opening a huge file of 2GB+ could take several minutes.\nDo you want to open it?",
                                               "Opening huge file warning");
        if (QMessageBox::question(this, q(t.title), q(t.message)) != QMessageBox::Yes)
            return nullptr;
    }
    LoadError err = LoadError::None;
    Buffer* b = m_files->loadFile(path, o, &err);
    if (!b) {
        const auto t = m_nativeLang.messageBox("OpenFileError", "Can not open file \"$STR_REPLACE$\".", "ERROR", 0, path);
        QMessageBox::warning(this, q(t.title), q(t.message));
        return nullptr;
    }
    if (m_cmd.readOnly)
        b->userReadOnly = true;

    // Replace a pristine "new 1" document like upstream
    EditorPane* pane = currentPane();
    if (pane->count() == 1) {
        Buffer* first = buffer(pane->bufferAt(0));
        if (first && first != b && first->isUntitled() && !first->dirty && pane->editor()->send(SCI_GETLENGTH) == 0
            && otherPane()->indexOf(first->id) < 0) {
            showBuffer(b, pane, true);
            closeBuffer(first->id, pane, false);
            addToRecent(path);
            return b;
        }
    }
    showBuffer(b, pane, activateIt);
    addToRecent(path);
    return b;
}

void MainWindow::openFromCommandLine(const CommandLine& c)
{
    Buffer* last = nullptr;
    for (const QString& f : c.files) {
        LoadOptions o;
        if (!c.language.isEmpty())
            if (const auto t = langTypeFromName(c.language.toStdString()))
                o.langOverride = *t;
        if (Buffer* b = openFile(f, o))
            last = b;
    }
    if (last && (c.line > 0 || c.position > 0)) {
        ScintillaEditBase* e = currentPane()->editor();
        intptr_t pos = c.position > 0 ? c.position - 1
                                      : e->send(SCI_FINDCOLUMN, static_cast<uptr_t>(c.line - 1), std::max(0, c.column - 1));
        e->send(SCI_GOTOPOS, static_cast<uptr_t>(pos));
        e->send(SCI_SCROLLCARET);
    }
    if (c.alwaysOnTop) {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        show();
    }
    raise();
    activateWindow();
}

bool MainWindow::askToSave(Buffer* b)
{
    if (!b || !b->dirty)
        return true;
    for (EditorPane* p : m_panes)
        if (p->indexOf(b->id) >= 0) {
            p->activate(b->id);
            setActivePane(p);
            break;
        }
    const auto t = m_nativeLang.messageBox("DoSaveOrNot", "Save file \"$STR_REPLACE$\" ?", "Save", 0, b->displayName());
    const auto answer = QMessageBox::question(this, q(t.title), q(t.message),
                                              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Yes)
        return saveBuffer(b, false);
    return true;
}

bool MainWindow::closeBuffer(BufferId id, EditorPane* pane, bool ask)
{
    Buffer* b = buffer(id);
    if (!b || !pane)
        return false;
    const bool shownElsewhere = (pane == m_panes[0] ? m_panes[1] : m_panes[0])->indexOf(id) >= 0;
    if (ask && !shownElsewhere && !askToSave(b))
        return false;
    pane->removeBuffer(id);
    if (!shownElsewhere) {
        if (!b->isUntitled()) {
            m_lastClosed.push_front(b->path);
            if (m_lastClosed.size() > 30)
                m_lastClosed.pop_back();
        }
        watchFile(b->path, false);
        m_files->close(*b);
    }
    if (pane->count() == 0) {
        if (pane->viewIndex() == 1 || m_panes[1]->count() > 0) {
            if (pane->viewIndex() == 1)
                pane->hide();
            setActivePane(m_panes[pane->viewIndex() == 1 ? 0 : 1]);
        }
        if (!m_exiting && m_panes[0]->count() == 0 && m_panes[1]->count() == 0) {
            if (m_config.gui.tabQuitOnEmpty) {
                QTimer::singleShot(0, this, &QWidget::close);
            } else {
                ensureDocument();
            }
        }
    }
    return true;
}

bool MainWindow::closeBuffers(std::vector<std::pair<BufferId, EditorPane*>> list)
{
    for (const auto& [id, pane] : list)
        if (!closeBuffer(id, pane))
            return false;
    return true;
}

bool MainWindow::saveBuffer(Buffer* b, bool saveAs, bool copy)
{
    if (!b)
        return false;
    QString path = q(b->path);
    if (path.isEmpty() || saveAs || copy) {
        QString start = path;
        if (start.isEmpty()) {
            const std::string& dir = m_config.gui.openSaveDir == 2 ? m_config.gui.defaultDir : m_config.gui.lastUsedDir;
            start = QDir(q(dir)).filePath(q(b->displayName()));
        }
        QString filter;
        if (const LanguageDef* d = m_langs.find(b->lang); d && !d->extensions.empty()) {
            QStringList pats;
            for (const auto& e : d->extensions)
                pats << QStringLiteral("*.") + q(e);
            filter = q(std::string(langInfo(b->lang).longName)) + QStringLiteral(" (") + pats.join(' ') + QStringLiteral(");;");
        }
        filter += tr("All types (*.*)");
        path = QFileDialog::getSaveFileName(this, copy ? tr("Save a Copy As") : tr("Save As"), start, filter);
        if (path.isEmpty())
            return false;
        m_config.gui.lastUsedDir = QFileInfo(path).absolutePath().toStdString();
        if (!copy) {
            Buffer* other = m_files->findByPath(path.toStdString());
            if (other && other != b) {
                QMessageBox::warning(this, QStringLiteral("Notepad++"),
                                     tr("The file \"%1\" is already open in Notepad++.").arg(path));
                return false;
            }
        }
    }
    const bool wasUntitled = b->isUntitled();
    const std::string oldPath = b->path;
    watchFile(b->path, false);
    const SaveStatus st = m_files->save(*b, path.toStdString(), copy);
    watchFile(b->path, true);
    if (st != SaveStatus::Ok) {
        const auto t = m_nativeLang.messageBox("FileLockedWarning",
                                               "Please check whether if this file is opened in another program", "Save failed");
        QMessageBox::warning(this, q(t.title), q(t.message) + QStringLiteral("\n\n") + path);
        return false;
    }
    if (copy) {
        addToRecent(path.toStdString());
        return true;
    }
    // Save As to another extension: re-detect the language like upstream
    if (wasUntitled || oldPath != b->path) {
        if (b->lang == L_TEXT || wasUntitled) {
            std::string udl;
            const LangType lang = m_files->detectLanguage(b->path, {}, &udl);
            setLanguage(b, lang, udl);
        }
        addToRecent(b->path);
    }
    for (EditorPane* p : m_panes)
        if (p->current() == b->id)
            p->editor()->send(SCI_SETSAVEPOINT);
    refreshBuffer(b);
    return true;
}

void MainWindow::moveOrCloneToOtherView(bool clone)
{
    Buffer* b = currentBuffer();
    if (!b)
        return;
    EditorPane* from = currentPane();
    EditorPane* to = otherPane();
    const ViewState state = from->viewState(b->id);
    if (to->indexOf(b->id) < 0) {
        to->addBuffer(*b, true);
        to->setViewState(b->id, state);
    } else {
        to->activate(b->id);
    }
    to->show();
    refreshBuffer(b);
    if (!clone)
        closeBuffer(b->id, from, false);
    setActivePane(to);
}

void MainWindow::reloadBuffer(Buffer* b, int encoding, std::optional<UniMode> mode)
{
    if (!b || b->isUntitled())
        return;
    std::map<EditorPane*, ViewState> states;
    for (EditorPane* p : m_panes)
        if (p->indexOf(b->id) >= 0)
            states[p] = p->viewState(b->id);
    watchFile(b->path, false);
    m_files->reload(*b, encoding, mode);
    watchFile(b->path, true);
    for (auto& [p, st] : states) {
        p->setViewState(b->id, st);
        if (p->current() == b->id)
            onCurrentBufferChanged(p, b->id);
    }
    refreshBuffer(b);
}

void MainWindow::watchFile(const std::string& path, bool watch)
{
    if (!m_watcher || path.empty())
        return;
    if (watch)
        m_watcher->addPath(q(path));
    else
        m_watcher->removePath(q(path));
}

void MainWindow::checkFilesystemChanges()
{
    const int mode = m_config.gui.fileAutoDetection;
    if (m_checkingFiles || m_exiting || mode == cdDisabled)
        return;
    m_checkingFiles = true;
    std::vector<BufferId> ids;
    for (const auto& b : m_files->buffers())
        ids.push_back(b->id);
    for (BufferId id : ids) {
        Buffer* b = buffer(id);
        if (!b || (b->status != DocStatus::Regular && b->status != DocStatus::Deleted && b->status != DocStatus::Modified))
            continue;
        const FileChange ch = m_files->checkFileState(*b);
        if (ch == FileChange::Modified) {
            const bool silent = (mode & cdAutoUpdate) && !b->dirty;
            bool reload = silent;
            if (!silent) {
                const auto t = b->dirty ? m_nativeLang.messageBox("DoReloadOrNotAndLooseChange",
                                                                   "\"$STR_REPLACE$\"\n\nThis file has been modified by another program.\nDo you want to reload it and lose the changes made in Notepad++?", "Reload", 0, b->path)
                                        : m_nativeLang.messageBox("DoReloadOrNot",
                                                                   "\"$STR_REPLACE$\"\n\nThis file has been modified by another program.\nDo you want to reload it?", "Reload", 0, b->path);
                reload = QMessageBox::question(this, q(t.title), q(t.message)) == QMessageBox::Yes;
            }
            if (reload) {
                reloadBuffer(b);
                if (mode & cdGo2end)
                    for (EditorPane* p : m_panes)
                        if (p->current() == b->id)
                            p->editor()->send(SCI_DOCUMENTEND);
            } else {
                b->status = DocStatus::Regular;
            }
        } else if (ch == FileChange::Deleted) {
            const auto t = m_nativeLang.messageBox("DoCloseOrNot",
                                                   "The file \"$STR_REPLACE$\" doesn't exist anymore.\nKeep this file in editor?", "Keep non existing file", 0, b->path);
            if (QMessageBox::question(this, q(t.title), q(t.message)) == QMessageBox::No) {
                for (EditorPane* p : m_panes)
                    if (p->indexOf(id) >= 0)
                        closeBuffer(id, p, false);
            } else {
                b->dirty = true;
                refreshBuffer(b);
            }
        } else if (ch == FileChange::Restored || ch == FileChange::ReadOnlyChanged) {
            watchFile(b->path, true);
            refreshBuffer(b);
        }
    }
    m_checkingFiles = false;
}

void MainWindow::backupDirtyBuffers()
{
    if (!m_config.gui.snapshotMode)
        return;
    for (const auto& b : m_files->buffers())
        m_files->backup(*b, m_paths.backupDir());
}

void MainWindow::addToRecent(const std::string& path)
{
    if (path.empty())
        return;
    m_config.history.add(path);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const auto& files = m_config.history.files;
    for (size_t i = 0; i < files.size(); ++i) {
        if (m_files->findByPath(files[i]))
            continue; // upstream lists only closed files
        const std::string label = buildMenuFileName(m_config.history.customLength, static_cast<unsigned>(i), files[i]);
        QAction* a = m_recentMenu->addAction(q(label));
        const QString path = q(files[i]);
        connect(a, &QAction::triggered, this, [this, path] { openFile(path); });
    }
    if (!m_recentMenu->isEmpty())
        m_recentMenu->addSeparator();
    m_recentMenu->addAction(m_actions->action(IDM_OPEN_ALL_RECENT_FILE));
    m_recentMenu->addAction(m_actions->action(IDM_CLEAN_RECENT_FILE_LIST));
}

// ------------------------------------------------------------------------------------------------
// Session
// ------------------------------------------------------------------------------------------------

Session MainWindow::currentSession(bool withBackups) const
{
    Session s;
    s.activeView = m_activeView;
    for (int v = 0; v < 2; ++v) {
        const EditorPane* p = m_panes[static_cast<size_t>(v)];
        SessionView& sv = v == 0 ? s.mainView : s.subView;
        sv.activeIndex = std::max(0, p->currentIndex());
        for (BufferId id : p->buffers()) {
            const Buffer* b = buffer(id);
            if (!b || (b->isUntitled() && !withBackups))
                continue;
            if (b->isUntitled() && b->backupPath.empty() && !b->dirty)
                continue; // empty untitled tab
            SessionFile f;
            f.filename = b->isUntitled() ? b->untitledName : b->path;
            f.lang = b->lang == L_USER ? b->userLangName : std::string(langInfo(b->lang).shortName);
            f.encoding = b->encoding;
            f.userReadOnly = b->userReadOnly;
            f.tabColourId = b->docColorId;
            f.tabPinned = b->pinned;
            f.backupFilePath = withBackups ? b->backupPath : std::string();
            f.originalFileLastModifTimestamp = b->timestamp.low();
            f.originalFileLastModifTimestampHigh = b->timestamp.high();
            const ViewState st = p->viewState(id);
            f.startPos = st.anchor;
            f.endPos = st.caret;
            f.firstVisibleLine = st.firstVisibleLine;
            f.xOffset = st.xOffset;
            f.selMode = st.selMode;
            if (p->current() == id) {
                ScintillaEditBase* e = p->editor();
                for (intptr_t line = e->send(SCI_MARKERNEXT, 0, 1 << kMarkBookmark); line >= 0;
                     line = e->send(SCI_MARKERNEXT, static_cast<uptr_t>(line + 1), 1 << kMarkBookmark))
                    f.marks.push_back(static_cast<size_t>(line));
            }
            sv.files.push_back(std::move(f));
        }
    }
    return s;
}

void MainWindow::restoreSession(const Session& s)
{
    for (int v = 0; v < 2; ++v) {
        const SessionView& sv = v == 0 ? s.mainView : s.subView;
        EditorPane* p = m_panes[static_cast<size_t>(v)];
        BufferId activeId = kInvalidBuffer;
        for (size_t i = 0; i < sv.files.size(); ++i) {
            const SessionFile& f = sv.files[i];
            Buffer* b = nullptr;
            const bool isFile = pathExists(f.filename, PathKind::File);
            if (!f.backupFilePath.empty() && pathExists(f.backupFilePath, PathKind::File)) {
                b = m_files->loadBackup(f.backupFilePath, isFile ? f.filename : std::string(), isFile ? std::string() : f.filename,
                                        f.encoding);
            } else if (isFile) {
                if ((b = m_files->findByPath(f.filename)) == nullptr) {
                    LoadOptions o;
                    o.encodingOverride = f.encoding;
                    if (const auto t = langTypeFromShortName(f.lang))
                        o.langOverride = *t;
                    else if (m_udls.find(f.lang)) {
                        o.langOverride = L_USER;
                        o.userLangOverride = f.lang;
                    }
                    b = m_files->loadFile(f.filename, o);
                }
            }
            if (!b)
                continue;
            if (const auto t = langTypeFromShortName(f.lang))
                b->lang = *t;
            else if (m_udls.find(f.lang)) {
                b->lang = L_USER;
                b->userLangName = f.lang;
            }
            b->userReadOnly = f.userReadOnly;
            b->docColorId = std::clamp(f.tabColourId, -1, 4);
            b->pinned = f.tabPinned;
            p->addBuffer(*b, false);
            watchFile(b->path, true);
            p->setViewState(b->id, {static_cast<intptr_t>(f.startPos), static_cast<intptr_t>(f.endPos),
                                    static_cast<intptr_t>(f.firstVisibleLine), static_cast<intptr_t>(f.xOffset), f.selMode});
            if (static_cast<int>(i) == sv.activeIndex)
                activeId = b->id;
            for (size_t line : f.marks) { // bookmarks live in the document markers
                ScintillaEditBase* e = m_scratchWidget;
                const intptr_t prev = e->send(SCI_GETDOCPOINTER);
                e->send(SCI_ADDREFDOCUMENT, 0, prev);
                e->send(SCI_SETDOCPOINTER, 0, b->document);
                e->send(SCI_MARKERADD, line, kMarkBookmark);
                e->send(SCI_SETDOCPOINTER, 0, prev);
                e->send(SCI_RELEASEDOCUMENT, 0, prev);
            }
            refreshBuffer(b);
        }
        if (p->count() > 0) {
            p->show();
            p->activate(activeId != kInvalidBuffer ? activeId : p->bufferAt(0));
        }
    }
    setActivePane(m_panes[static_cast<size_t>(s.activeView == 1 && m_panes[1]->count() > 0 ? 1 : 0)]);
}

bool MainWindow::saveSessionTo(const QString& path)
{
    return saveSession(currentSession(false), path.toStdString());
}

bool MainWindow::loadSessionFrom(const QString& path)
{
    const auto s = loadSession(path.toStdString());
    if (!s) {
        const auto t = m_nativeLang.messageBox("SessionFileInvalidError", "Session file is either corrupted or not valid.",
                                               "Could not Load Session");
        QMessageBox::warning(this, q(t.title), q(t.message));
        return false;
    }
    restoreSession(*s);
    return true;
}

bool MainWindow::prepareExit()
{
    if (!panelsPrepareExit())
        return false;
    const bool snapshot = m_config.gui.snapshotMode && m_config.gui.rememberLastSession && !m_cmd.noSession;
    if (snapshot) {
        backupDirtyBuffers();
    } else {
        for (const auto& [id, p] : allTabs())
            if (!askToSave(buffer(id)))
                return false;
    }
    m_exiting = true;
    if (m_config.gui.rememberLastSession && !m_cmd.noSession) {
        std::error_code ec;
        fs::create_directories(pathFromUtf8(m_paths.sessionDir()), ec);
        saveSession(currentSession(snapshot), m_paths.sessionXml());
    }
    // Backups referenced by the session must survive: detach them before buffers are closed
    if (snapshot)
        for (const auto& b : m_files->buffers())
            b->backupPath.clear();
    saveSettings();
    return true;
}

void MainWindow::shutdownForTest()
{
    prepareExit();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_exiting && !prepareExit()) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            openFile(url.toLocalFile());
    event->acceptProposedAction();
}

} // namespace npp::ui
