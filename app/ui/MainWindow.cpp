#include "MainWindow.h"

#include "docking/DockManager.h"
#include "editor/EditorAdapter.h"

#include "core/encoding/EncodingMapper.h"
#include "core/session/Session.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeView>

namespace npp::ui {

using namespace npp::core;

namespace {

constexpr char kPropBuffer[] = "nppBufferId";
constexpr char kPanelDocList[] = "npp.documentList";
constexpr char kPanelFolder[] = "npp.folderAsWorkspace";
constexpr int kMarginLineNumber = 0;
constexpr int kMarginBookmark = 1;
constexpr int kMarginFold = 2;

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
        const auto& t = EncodingMapper::table();
        if (idx >= 0) {
            const std::string_view aliases = t[static_cast<size_t>(idx)].aliasList;
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

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Notepad++ Qt"));
    resize(1200, 800);

    loadSettings();
    m_dock = createDockManager(this);
    createEditorArea();
    createPanels();
    createMenus();
    createStatusBar();
    restoreWindowState();
}

MainWindow::~MainWindow()
{
    // Views must drop their documents before the FileManager releases them.
    while (m_tabs && m_tabs->count() > 0) {
        QWidget* w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
    m_files.reset();
    m_scratch.reset();
    delete m_scratchWidget;
}

void MainWindow::loadSettings()
{
    // Data files shipped with the application (model files, UDLs)
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

    // Settings folder of Notepad++ (portable / cloud / per platform). Never written by this sample:
    // it is shared with the original Notepad++ (docs/qt-cross-platform-port-plan.md 6.2).
    AppPaths::Options po;
    po.appDir = QCoreApplication::applicationDirPath().toStdString();
    po.createDirectories = false;
    m_paths = AppPaths::resolve(po);
    m_config.load(m_paths.configXml());

    auto pick = [this](const std::string& user, const char* model) {
        return pathExists(user, PathKind::File) ? user : pathAppend(m_dataDir, model);
    };
    if (!m_langs.load(pick(m_paths.langsXml(), "langs.model.xml")) ||
        !m_styles.load(pick(m_paths.stylersXml(), "stylers.model.xml")))
        QMessageBox::warning(this, tr("Configuration"), tr("langs.xml / stylers.xml could not be loaded."));
    for (const auto& ls : m_styles.lexers())
        if (!ls.ext.empty())
            if (const auto t = langTypeFromName(ls.name))
                m_langs.addUserExtensions(*t, ls.ext);
    m_udls.loadAll(m_paths.userDefineLangXml(), m_paths.userDefineLangsDir());
    m_udls.loadAll({}, pathAppend(m_dataDir, "userDefineLangs"));

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
    fs.largeFileSizeBytes = static_cast<uint64_t>(g.largeFileSizeMB) * 1024 * 1024;
    m_files = std::make_unique<FileManager>(*m_scratch, fs, &m_langs, &m_udls);
}

void MainWindow::createEditorArea()
{
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeEditor(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        updateStatus();
        refreshDocumentList();
    });
    m_dock->setCentralWidget(m_tabs);
}

void MainWindow::createPanels()
{
    m_docList = new QListWidget;
    connect(m_docList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_tabs->count())
            m_tabs->setCurrentIndex(row);
    });
    m_dock->addPanel({kPanelDocList, tr("Document List"), {}, DockArea::Left}, m_docList);

    auto* model = new QFileSystemModel(this);
    model->setRootPath(QDir::homePath());
    auto* tree = new QTreeView;
    tree->setModel(model);
    tree->setRootIndex(model->index(QDir::currentPath()));
    for (int c = 1; c < model->columnCount(); ++c)
        tree->hideColumn(c);
    tree->header()->hide();
    connect(tree, &QTreeView::doubleClicked, this, [this, model](const QModelIndex& idx) {
        if (!model->isDir(idx))
            openFile(model->filePath(idx));
    });
    m_dock->addPanel({kPanelFolder, tr("Folder as Workspace"), {}, DockArea::Left}, tree);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, [this] { addTab(m_files->newDocument()); });
    file->addAction(tr("&Open..."), QKeySequence::Open, this, [this] {
        for (const QString& f : QFileDialog::getOpenFileNames(this, tr("Open")))
            openFile(f);
    });
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] {
        if (auto* e = currentEditor()) saveEditor(e, false);
    });
    file->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, [this] {
        if (auto* e = currentEditor()) saveEditor(e, true);
    });
    file->addAction(tr("&Close"), QKeySequence::Close, this, [this] { closeEditor(m_tabs->currentIndex()); });
    file->addSeparator();
    file->addAction(tr("Load Notepad++ session"), this, &MainWindow::restoreNppSession);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close)->setMenuRole(QAction::QuitRole);

    QMenu* search = menuBar()->addMenu(tr("&Search"));
    search->addAction(tr("Find (regular expression)..."), QKeySequence::Find, this, &MainWindow::findRegex);

    QMenu* lang = menuBar()->addMenu(tr("&Language"));
    m_languageGroup = new QActionGroup(this);
    for (int i = 0; i < kLangCount; ++i) {
        const auto t = static_cast<LangType>(i);
        if (t == L_USER || t == L_EXTERNAL || t == L_JS_EMBEDDED || t == L_SEARCHRESULT)
            continue;
        QAction* a = lang->addAction(q(std::string(langInfo(t).shortName)));
        a->setCheckable(true);
        a->setData(i);
        m_languageGroup->addAction(a);
    }
    if (!m_udls.languages().empty()) {
        lang->addSeparator();
        for (const auto& u : m_udls.languages()) {
            QAction* a = lang->addAction(q(u.name));
            a->setCheckable(true);
            a->setData(q(u.name));
            m_languageGroup->addAction(a);
        }
    }
    connect(m_languageGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        if (auto* e = currentEditor()) {
            if (a->data().typeId() == QMetaType::QString)
                setLanguage(e, L_USER, a->data().toString().toStdString());
            else
                setLanguage(e, static_cast<LangType>(a->data().toInt()));
        }
    });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    const std::pair<const char*, QString> panels[] = {{kPanelDocList, tr("Document List")},
                                                      {kPanelFolder, tr("Folder as Workspace")}};
    for (const auto& [id, title] : panels) {
        QAction* a = view->addAction(title);
        a->setCheckable(true);
        a->setChecked(m_dock->isPanelVisible(id));
        const std::string panelId = id;
        connect(a, &QAction::toggled, this, [this, panelId](bool on) {
            on ? m_dock->showPanel(panelId) : m_dock->hidePanel(panelId);
        });
        connect(m_dock.get(), &DockManager::panelVisibilityChanged, a,
                [a, panelId](const std::string& changed, bool visible) {
                    if (changed == panelId && a->isChecked() != visible) {
                        QSignalBlocker block(a);
                        a->setChecked(visible);
                    }
                });
    }

    QMenu* help = menuBar()->addMenu(tr("&?"));
    help->addAction(tr("About..."), this, [this] {
        QMessageBox::about(this, tr("About"),
                           tr("Notepad++ Qt port (npp-core)\nQt %1, docking: %2\nSettings: %3")
                               .arg(QString::fromLatin1(qVersion()), m_dock->backendName(), q(m_paths.userDir())));
    })->setMenuRole(QAction::AboutRole);
}

void MainWindow::createStatusBar()
{
    m_statusLang = new QLabel;
    m_statusPos = new QLabel;
    m_statusEnc = new QLabel;
    statusBar()->addWidget(m_statusLang, 1);
    statusBar()->addPermanentWidget(m_statusPos);
    statusBar()->addPermanentWidget(m_statusEnc);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("dock: %1").arg(m_dock->backendName())));
}

void MainWindow::applyEditorSettings(ScintillaEditBase* e)
{
    const GuiConfig& g = m_config.gui;
    e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    e->send(SCI_SETTABWIDTH, g.tabSize);
    e->send(SCI_SETUSETABS, !g.tabReplacedBySpace);
    e->send(SCI_SETCARETLINEVISIBLE, g.currentLineIndicator == 1);
    e->send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    e->send(SCI_SETMULTIPLESELECTION, g.multiSelection);
    e->send(SCI_SETADDITIONALSELECTIONTYPING, g.multiSelection);
    e->send(SCI_SETIMEINTERACTION, SC_IME_INLINE);
    e->send(SCI_SETWRAPMODE, g.wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
    e->send(SCI_SETVIEWWS, g.whiteSpaceShow ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    e->send(SCI_SETVIEWEOL, g.eolShow);
    e->send(SCI_SETINDENTATIONGUIDES, g.indentGuideLine ? SC_IV_LOOKBOTH : SC_IV_NONE);
    e->send(SCI_SETZOOM, static_cast<uintptr_t>(g.zoom));
    e->send(SCI_SETCARETWIDTH, static_cast<uintptr_t>(g.caretWidth));

    e->send(SCI_SETMARGINTYPEN, kMarginLineNumber, SC_MARGIN_NUMBER);
    e->send(SCI_SETMARGINWIDTHN, kMarginLineNumber, g.lineNumberMargin ? 48 : 0);
    e->send(SCI_SETMARGINWIDTHN, kMarginBookmark, g.bookmarkMargin ? 16 : 0);
    e->send(SCI_SETMARGINTYPEN, kMarginFold, SC_MARGIN_SYMBOL);
    e->send(SCI_SETMARGINMASKN, kMarginFold, SC_MASK_FOLDERS);
    e->send(SCI_SETMARGINWIDTHN, kMarginFold, 14);
    e->send(SCI_SETMARGINSENSITIVEN, kMarginFold, 1);
    e->send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    const bool circle = g.folderMarkStyle == "circle", arrow = g.folderMarkStyle == "arrow",
               simple = g.folderMarkStyle == "simple";
    const std::pair<int, int> markers[] = {
        {SC_MARKNUM_FOLDEROPEN, circle ? SC_MARK_CIRCLEMINUS : arrow ? SC_MARK_ARROWDOWN : simple ? SC_MARK_MINUS : SC_MARK_BOXMINUS},
        {SC_MARKNUM_FOLDER, circle ? SC_MARK_CIRCLEPLUS : arrow ? SC_MARK_ARROW : simple ? SC_MARK_PLUS : SC_MARK_BOXPLUS},
        {SC_MARKNUM_FOLDERSUB, (arrow || simple) ? SC_MARK_EMPTY : SC_MARK_VLINE},
        {SC_MARKNUM_FOLDERTAIL, (arrow || simple) ? SC_MARK_EMPTY : circle ? SC_MARK_CIRCLEPLUS + 2 /*LCORNERCURVE*/ : SC_MARK_LCORNER},
        {SC_MARKNUM_FOLDEREND, circle ? SC_MARK_CIRCLEPLUSCONNECTED : (arrow || simple) ? SC_MARK_EMPTY : SC_MARK_BOXPLUSCONNECTED},
        {SC_MARKNUM_FOLDEROPENMID, circle ? SC_MARK_CIRCLEMINUSCONNECTED : (arrow || simple) ? SC_MARK_EMPTY : SC_MARK_BOXMINUSCONNECTED},
        {SC_MARKNUM_FOLDERMIDTAIL, (arrow || simple) ? SC_MARK_EMPTY : SC_MARK_TCORNER},
    };
    for (auto [num, sym] : markers) {
        e->send(SCI_MARKERDEFINE, num, sym);
        e->send(SCI_MARKERSETFORE, num, 0xFFFFFF);
        e->send(SCI_MARKERSETBACK, num, 0x808080);
    }
}

ScintillaEditBase* MainWindow::addTab(Buffer* b)
{
    auto* e = new ScintillaEditBase;
    e->send(SCI_SETDOCPOINTER, 0, b->document);
    e->setProperty(kPropBuffer, QVariant::fromValue<qulonglong>(b->id));
    applyEditorSettings(e);
    e->send(SCI_SETEOLMODE, static_cast<uintptr_t>(scintillaEolMode(b->eol)));
    e->send(SCI_SETREADONLY, b->isReadOnly());

    connect(e, &ScintillaEditBase::savePointChanged, this, [this, e](bool dirty) {
        if (Buffer* buf = bufferOf(e))
            buf->dirty = dirty;
        updateTabTitle(e);
    });
    connect(e, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update) { updateStatus(); });

    const int index = m_tabs->addTab(e, q(b->displayName()));
    m_tabs->setTabToolTip(index, q(b->path));
    m_tabs->setCurrentIndex(index);
    setLanguage(e, b->lang, b->userLangName);
    updateTabTitle(e);
    e->setFocus();
    refreshDocumentList();
    return e;
}

void MainWindow::openFile(const QString& path)
{
    LoadError err = LoadError::None;
    Buffer* b = m_files->loadFile(path.toStdString(), {}, &err);
    if (!b) {
        QMessageBox::warning(this, tr("Open"), tr("Cannot open %1").arg(path));
        return;
    }
    if (const int existing = indexOfBuffer(b); existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }
    // Replace a pristine "new 1" tab like Notepad++ does
    if (m_tabs->count() == 1) {
        ScintillaEditBase* e = editorAt(0);
        Buffer* first = bufferOf(e);
        if (first && first->isUntitled() && !first->dirty && e->send(SCI_GETLENGTH) == 0) {
            m_tabs->removeTab(0);
            delete e;
            m_files->close(*first);
        }
    }
    addTab(b);
}

void MainWindow::ensureDocument()
{
    if (m_tabs->count() == 0)
        addTab(m_files->newDocument());
}

void MainWindow::restoreNppSession()
{
    const auto s = loadSession(m_paths.sessionXml());
    if (!s) {
        statusBar()->showMessage(tr("No Notepad++ session found in %1").arg(q(m_paths.sessionDir())), 4000);
        return;
    }
    for (const SessionView* v : {&s->mainView, &s->subView})
        for (const SessionFile& f : v->files) {
            Buffer* b = nullptr;
            if (!f.backupFilePath.empty() && pathExists(f.backupFilePath, PathKind::File))
                b = m_files->loadBackup(f.backupFilePath, f.untitledTabRenamed || !pathExists(f.filename) ? std::string() : f.filename,
                                        f.filename, f.encoding);
            else if (pathExists(f.filename, PathKind::File)) {
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
            if (!b || indexOfBuffer(b) >= 0)
                continue;
            // The backup belongs to the original Notepad++ (shared settings folder): never let
            // FileManager::close() / backup() delete or overwrite it.
            b->backupPath.clear();
            ScintillaEditBase* e = addTab(b);
            e->send(SCI_SETSEL, static_cast<uintptr_t>(f.startPos), f.endPos);
            e->send(SCI_SETFIRSTVISIBLELINE, static_cast<uintptr_t>(f.firstVisibleLine));
            for (size_t line : f.marks)
                e->send(SCI_MARKERADD, line, 24); // MARK_BOOKMARK
        }
}

bool MainWindow::saveEditor(ScintillaEditBase* e, bool saveAs)
{
    Buffer* b = bufferOf(e);
    if (!b)
        return false;
    QString path = q(b->path);
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, tr("Save As"), path.isEmpty() ? q(b->displayName()) : path);
        if (path.isEmpty())
            return false;
    }
    const bool wasUntitled = b->isUntitled();
    if (m_files->save(*b, path.toStdString()) != SaveStatus::Ok) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot save %1").arg(path));
        return false;
    }
    const int index = m_tabs->indexOf(e);
    m_tabs->setTabToolTip(index, q(b->path));
    if (wasUntitled || saveAs) {
        std::string udl;
        b->lang = m_files->detectLanguage(b->path, {}, &udl);
        b->userLangName = udl;
        setLanguage(e, b->lang, udl);
    }
    updateTabTitle(e);
    refreshDocumentList();
    return true;
}

bool MainWindow::closeEditor(int index)
{
    ScintillaEditBase* e = editorAt(index);
    Buffer* b = bufferOf(e);
    if (!e || !b)
        return false;
    if (b->dirty) {
        m_tabs->setCurrentIndex(index);
        const auto answer = QMessageBox::question(this, tr("Save"), tr("Save file \"%1\"?").arg(q(b->displayName())),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Yes && !saveEditor(e, false)))
            return false;
    }
    m_tabs->removeTab(index);
    delete e;
    m_files->close(*b);
    if (m_tabs->count() == 0)
        addTab(m_files->newDocument());
    refreshDocumentList();
    return true;
}

void MainWindow::setLanguage(ScintillaEditBase* e, LangType lang, const std::string& udlName)
{
    Buffer* b = bufferOf(e);
    EditorAdapter ed(e);
    const UserLanguage* udl = lang == L_USER ? m_udls.find(udlName) : nullptr;
    if (udl)
        applyUserLanguage(ed, *udl, m_styles, m_applyOptions, b ? b->id : 0);
    else
        applyLanguage(ed, lang == L_USER ? L_TEXT : lang, m_langs, m_styles, m_applyOptions);
    if (b) {
        b->lang = udl ? L_USER : (lang == L_USER ? L_TEXT : lang);
        b->userLangName = udl ? udlName : std::string();
    }
    if (const Style* cl = m_styles.globalStyleByName("Current line background colour"); cl && cl->bg >= 0)
        e->send(SCI_SETCARETLINEBACK, static_cast<uintptr_t>(Style::scintillaColour(cl->bg)));
    e->send(SCI_SETFOLDMARGINCOLOUR, 1, 0xF0F0F0);
    e->send(SCI_SETFOLDMARGINHICOLOUR, 1, 0xF0F0F0);
    updateStatus();
}

void MainWindow::updateTabTitle(ScintillaEditBase* e)
{
    const int index = m_tabs->indexOf(e);
    Buffer* b = bufferOf(e);
    if (index >= 0 && b)
        m_tabs->setTabText(index, (b->dirty ? QStringLiteral("*") : QString()) + q(b->displayName()));
}

void MainWindow::updateStatus()
{
    ScintillaEditBase* e = currentEditor();
    Buffer* b = bufferOf(e);
    if (!e || !b || !m_statusLang)
        return;
    m_statusLang->setText(b->lang == L_USER ? q(b->userLangName) : q(std::string(langInfo(b->lang).longName)));
    const auto pos = e->send(SCI_GETCURRENTPOS);
    m_statusPos->setText(tr("Ln : %1    Col : %2")
                             .arg(e->send(SCI_LINEFROMPOSITION, pos) + 1)
                             .arg(e->send(SCI_GETCOLUMN, pos) + 1));
    m_statusEnc->setText(eolLabel(b->eol) + QStringLiteral("   ") + encodingLabel(*b));
    if (m_languageGroup)
        for (QAction* a : m_languageGroup->actions()) {
            const bool match = b->lang == L_USER ? a->data().toString() == q(b->userLangName)
                                                 : (a->data().typeId() != QMetaType::QString && a->data().toInt() == b->lang);
            if (match) {
                QSignalBlocker block(m_languageGroup);
                a->setChecked(true);
            }
        }
    setWindowTitle(QStringLiteral("%1 - Notepad++ Qt").arg(b->isUntitled() ? q(b->untitledName) : q(b->path)));
}

void MainWindow::refreshDocumentList()
{
    if (!m_docList)
        return;
    QSignalBlocker block(m_docList);
    m_docList->clear();
    for (int i = 0; i < m_tabs->count(); ++i)
        m_docList->addItem(m_tabs->tabText(i));
    m_docList->setCurrentRow(m_tabs->currentIndex());
}

void MainWindow::findRegex()
{
    ScintillaEditBase* e = currentEditor();
    if (!e)
        return;
    bool ok = false;
    const QString pattern = QInputDialog::getText(this, tr("Find (regex)"), tr("Regular expression:"),
                                                  QLineEdit::Normal, {}, &ok);
    if (!ok || pattern.isEmpty())
        return;
    SearchOptions o;
    o.type = SearchType::Regex;
    o.findWhat = pattern.toStdString();
    EditorAdapter ed(e);
    const auto m = findNext(ed, o, e->send(SCI_GETSELECTIONEND));
    if (!m) {
        statusBar()->showMessage(tr("Not found: %1").arg(pattern), 3000);
        return;
    }
    e->send(SCI_SETSEL, static_cast<uintptr_t>(m->start), m->end);
    e->send(SCI_SCROLLCARET);
    if (m->wrapped)
        statusBar()->showMessage(tr("Search wrapped"), 2000);
}

ScintillaEditBase* MainWindow::currentEditor() const
{
    return qobject_cast<ScintillaEditBase*>(m_tabs->currentWidget());
}

ScintillaEditBase* MainWindow::editorAt(int index) const
{
    return qobject_cast<ScintillaEditBase*>(m_tabs->widget(index));
}

Buffer* MainWindow::bufferOf(const ScintillaEditBase* e) const
{
    return e ? m_files->find(e->property(kPropBuffer).toULongLong()) : nullptr;
}

int MainWindow::indexOfBuffer(const Buffer* b) const
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (bufferOf(editorAt(i)) == b)
            return i;
    return -1;
}

void MainWindow::restoreWindowState()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    const QString backendKey = QStringLiteral("dock/%1/native").arg(m_dock->backendName());
    if (!m_dock->restoreNativeState(s.value(backendKey).toByteArray())) {
        DockLayout layout;
        const int n = s.beginReadArray(QStringLiteral("dock/panels"));
        for (int i = 0; i < n; ++i) {
            s.setArrayIndex(i);
            layout.panels.push_back({s.value(QStringLiteral("id")).toString().toStdString(),
                                     s.value(QStringLiteral("visible"), true).toBool(),
                                     dockAreaFromString(s.value(QStringLiteral("area")).toString())});
        }
        s.endArray();
        m_dock->applyLayout(layout);
    }
}

void MainWindow::saveWindowState() const
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("dock/%1/native").arg(m_dock->backendName()), m_dock->saveNativeState());
    const DockLayout layout = m_dock->layout();
    s.beginWriteArray(QStringLiteral("dock/panels"), static_cast<int>(layout.panels.size()));
    for (int i = 0; i < static_cast<int>(layout.panels.size()); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), QString::fromStdString(layout.panels[i].id));
        s.setValue(QStringLiteral("visible"), layout.panels[i].visible);
        s.setValue(QStringLiteral("area"), dockAreaToString(layout.panels[i].area));
    }
    s.endArray();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        Buffer* b = bufferOf(editorAt(i));
        if (b && b->dirty && !closeEditor(i)) {
            event->ignore();
            return;
        }
    }
    saveWindowState();
    event->accept();
}

} // namespace npp::ui
