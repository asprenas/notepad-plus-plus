#include "MainWindow.h"

#include "docking/DockManager.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeView>

namespace npp::ui {

namespace {

constexpr char kPropPath[]     = "nppPath";
constexpr char kPropLanguage[] = "nppLanguage";
constexpr char kPropTitle[]    = "nppTitle";

constexpr char kPanelDocList[] = "npp.documentList";
constexpr char kPanelFolder[]  = "npp.folderAsWorkspace";

constexpr int kMarginLineNumber = 0;
constexpr int kMarginFold       = 2;

QString pathOf(const ScintillaEditBase* e)  { return e->property(kPropPath).toString(); }
QString titleOf(const ScintillaEditBase* e) { return e->property(kPropTitle).toString(); }
bool isDirty(const ScintillaEditBase* e)    { return e->send(SCI_GETMODIFY) != 0; }

void setupEditorDefaults(ScintillaEditBase* e)
{
    e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    e->send(SCI_SETEOLMODE, QSysInfo::productType() == QLatin1String("windows") ? SC_EOL_CRLF : SC_EOL_LF);
    e->send(SCI_SETTABWIDTH, 4);
    e->send(SCI_SETCARETLINEVISIBLE, 1);
    e->send(SCI_SETCARETLINEBACK, 0xFFE8E8);
    e->send(SCI_SETMULTIPLESELECTION, 1);
    e->send(SCI_SETADDITIONALSELECTIONTYPING, 1);
    e->send(SCI_SETIMEINTERACTION, SC_IME_INLINE); // inline IME composition (Korean etc.)

    // Line numbers
    e->send(SCI_SETMARGINTYPEN, kMarginLineNumber, SC_MARGIN_NUMBER);
    e->send(SCI_SETMARGINWIDTHN, kMarginLineNumber, 48);

    // Folding margin (box tree, like Notepad++ default)
    e->send(SCI_SETMARGINTYPEN, kMarginFold, SC_MARGIN_SYMBOL);
    e->send(SCI_SETMARGINMASKN, kMarginFold, SC_MASK_FOLDERS);
    e->send(SCI_SETMARGINWIDTHN, kMarginFold, 14);
    e->send(SCI_SETMARGINSENSITIVEN, kMarginFold, 1);
    e->send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    const std::pair<int, int> markers[] = {
        {SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS},     {SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS},
        {SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE},         {SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER},
        {SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED},
        {SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED},
        {SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER},
    };
    for (auto [num, sym] : markers) {
        e->send(SCI_MARKERDEFINE, num, sym);
        e->send(SCI_MARKERSETFORE, num, 0xFFFFFF);
        e->send(SCI_MARKERSETBACK, num, 0x808080);
    }
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Notepad++ Qt (phase 0 spike)"));
    resize(1200, 800);

    loadLanguageConfig();
    m_dock = createDockManager(this);
    createEditorArea();
    createPanels();
    createMenus();
    createStatusBar();
    restoreSession();

    if (m_tabs->count() == 0)
        newEditor();
    updateStatus();
}

MainWindow::~MainWindow() = default;

void MainWindow::loadLanguageConfig()
{
    // Search order: $NPP_DATA_DIR, <exe>/data (copied at build time).
    QStringList candidates;
    if (const QString env = qEnvironmentVariable("NPP_DATA_DIR"); !env.isEmpty())
        candidates << env;
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/data");
#if defined(Q_OS_MACOS)
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/data");
#endif

    for (const QString& dir : candidates) {
        if (!QFileInfo::exists(dir + QStringLiteral("/langs.model.xml")))
            continue;
        std::string error;
        if (m_langConfig.load(dir.toStdString(), &error)) {
            m_langConfigLoaded = true;
            return;
        }
        QMessageBox::warning(this, tr("Configuration"), QString::fromStdString(error));
    }
    QMessageBox::warning(this, tr("Configuration"),
                         tr("langs.model.xml / stylers.model.xml not found. Syntax highlighting disabled."));
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
    // Document List
    m_docList = new QListWidget;
    connect(m_docList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_tabs->count())
            m_tabs->setCurrentIndex(row);
    });
    m_dock->addPanel({kPanelDocList, tr("Document List"), {}, DockArea::Left}, m_docList);

    // Folder as Workspace
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
    // File
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, [this] { newEditor(); });
    file->addAction(tr("&Open..."), QKeySequence::Open, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open"));
        for (const QString& f : files)
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
    QAction* quit = file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);
    quit->setMenuRole(QAction::QuitRole);

    // Search
    QMenu* search = menuBar()->addMenu(tr("&Search"));
    search->addAction(tr("Find (regular expression, Boost)..."), QKeySequence::Find, this, &MainWindow::findRegex);

    // Language
    m_languageMenu = menuBar()->addMenu(tr("&Language"));
    m_languageGroup = new QActionGroup(this);
    for (const std::string& name : m_langConfig.languageNames()) {
        QAction* a = m_languageMenu->addAction(QString::fromStdString(name));
        a->setCheckable(true);
        a->setData(QString::fromStdString(name));
        m_languageGroup->addAction(a);
    }
    connect(m_languageGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        if (auto* e = currentEditor())
            setLanguage(e, a->data().toString().toStdString());
    });

    // View: panel toggles (through the DockManager only)
    QMenu* view = menuBar()->addMenu(tr("&View"));
    const std::pair<const char*, QString> panels[] = {
        {kPanelDocList, tr("Document List")},
        {kPanelFolder, tr("Folder as Workspace")},
    };
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

    // Help
    QMenu* help = menuBar()->addMenu(tr("&?"));
    QAction* about = help->addAction(tr("About..."), this, [this] {
        QMessageBox::about(this, tr("About"),
                           tr("Notepad++ Qt port — phase 0 spike\nQt %1, docking backend: %2")
                               .arg(QString::fromLatin1(qVersion()), m_dock->backendName()));
    });
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createStatusBar()
{
    m_statusLang = new QLabel;
    m_statusPos  = new QLabel;
    m_statusEnc  = new QLabel(QStringLiteral("UTF-8"));
    statusBar()->addWidget(m_statusLang, 1);
    statusBar()->addPermanentWidget(m_statusPos);
    statusBar()->addPermanentWidget(m_statusEnc);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("dock: %1").arg(m_dock->backendName())));
}

ScintillaEditBase* MainWindow::newEditor(const QString& path)
{
    auto* e = new ScintillaEditBase;
    setupEditorDefaults(e);
    e->setProperty(kPropPath, path);
    e->setProperty(kPropTitle, path.isEmpty() ? tr("new %1").arg(++m_untitledCounter) : QFileInfo(path).fileName());

    connect(e, &ScintillaEditBase::savePointChanged, this, [this, e](bool) { updateTabTitle(e); });
    connect(e, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update) { updateStatus(); });

    const int index = m_tabs->addTab(e, titleOf(e));
    m_tabs->setTabToolTip(index, path);
    m_tabs->setCurrentIndex(index);
    setLanguage(e, path.isEmpty() ? std::string("normal") : m_langConfig.languageForFile(path.toStdString()));
    e->setFocus();
    refreshDocumentList();
    return e;
}

void MainWindow::openFile(const QString& rawPath)
{
    const QString path = QFileInfo(rawPath).absoluteFilePath();
    if (const int existing = indexOfPath(path); existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, f.errorString()));
        return;
    }
    const QByteArray data = f.readAll(); // phase 0: assume UTF-8 (encoding detection comes with npp-core)

    // Reuse a pristine "new 1" tab like Notepad++ does.
    ScintillaEditBase* e = currentEditor();
    if (e && pathOf(e).isEmpty() && !isDirty(e) && e->send(SCI_GETLENGTH) == 0 && m_tabs->count() == 1) {
        m_tabs->removeTab(m_tabs->currentIndex());
        e->deleteLater();
    }
    e = newEditor(path);

    e->send(SCI_CLEARALL);
    e->send(SCI_APPENDTEXT, data.size(), reinterpret_cast<sptr_t>(data.constData()));
    // Keep the file's EOL convention.
    if (data.contains("\r\n"))
        e->send(SCI_SETEOLMODE, SC_EOL_CRLF);
    else if (data.contains('\n'))
        e->send(SCI_SETEOLMODE, SC_EOL_LF);
    e->send(SCI_EMPTYUNDOBUFFER);
    e->send(SCI_SETSAVEPOINT);
    e->send(SCI_GOTOPOS, 0);
}

bool MainWindow::saveEditor(ScintillaEditBase* e, bool saveAs)
{
    QString path = pathOf(e);
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, tr("Save As"), path.isEmpty() ? titleOf(e) : path);
        if (path.isEmpty())
            return false;
    }

    const auto length = e->send(SCI_GETLENGTH);
    const auto* text  = reinterpret_cast<const char*>(e->send(SCI_GETCHARACTERPOINTER));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(text, length) != length || !f.commit()) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot save %1:\n%2").arg(path, f.errorString()));
        return false;
    }

    if (path != pathOf(e)) {
        e->setProperty(kPropPath, path);
        e->setProperty(kPropTitle, QFileInfo(path).fileName());
        m_tabs->setTabToolTip(m_tabs->indexOf(e), path);
        setLanguage(e, m_langConfig.languageForFile(path.toStdString()));
    }
    e->send(SCI_SETSAVEPOINT);
    updateTabTitle(e);
    refreshDocumentList();
    return true;
}

bool MainWindow::closeEditor(int index)
{
    ScintillaEditBase* e = editorAt(index);
    if (!e)
        return false;
    if (isDirty(e)) {
        m_tabs->setCurrentIndex(index);
        const auto answer = QMessageBox::question(this, tr("Save"), tr("Save file \"%1\"?").arg(titleOf(e)),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Yes && !saveEditor(e, false)))
            return false;
    }
    m_tabs->removeTab(index);
    e->deleteLater();
    if (m_tabs->count() == 0)
        newEditor();
    refreshDocumentList();
    return true;
}

void MainWindow::setLanguage(ScintillaEditBase* e, const std::string& language)
{
    e->setProperty(kPropLanguage, QString::fromStdString(language));
    if (m_langConfigLoaded)
        m_langConfig.apply(e, language);
    // Re-apply margin colours lost by SCI_STYLECLEARALL.
    e->send(SCI_SETFOLDMARGINCOLOUR, 1, 0xF0F0F0);
    e->send(SCI_SETFOLDMARGINHICOLOUR, 1, 0xF0F0F0);
    updateStatus();
}

void MainWindow::updateTabTitle(ScintillaEditBase* e)
{
    const int index = m_tabs->indexOf(e);
    if (index >= 0)
        m_tabs->setTabText(index, (isDirty(e) ? QStringLiteral("*") : QString()) + titleOf(e));
}

void MainWindow::updateStatus()
{
    ScintillaEditBase* e = currentEditor();
    if (!e || !m_statusLang)
        return;
    const QString lang = e->property(kPropLanguage).toString();
    m_statusLang->setText(lang);
    const auto pos  = e->send(SCI_GETCURRENTPOS);
    const auto line = e->send(SCI_LINEFROMPOSITION, pos);
    const auto col  = e->send(SCI_GETCOLUMN, pos);
    m_statusPos->setText(tr("Ln : %1    Col : %2").arg(line + 1).arg(col + 1));
    m_statusEnc->setText(e->send(SCI_GETEOLMODE) == SC_EOL_CRLF ? QStringLiteral("Windows (CR LF)   UTF-8")
                                                                 : QStringLiteral("Unix (LF)   UTF-8"));

    if (m_languageGroup) {
        for (QAction* a : m_languageGroup->actions()) {
            if (a->data().toString() == lang) {
                QSignalBlocker block(m_languageGroup);
                a->setChecked(true);
            }
        }
    }
    setWindowTitle(QStringLiteral("%1 - Notepad++ Qt (phase 0)").arg(pathOf(e).isEmpty() ? titleOf(e) : pathOf(e)));
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
    const QString pattern = QInputDialog::getText(this, tr("Find (regex)"), tr("Boost regular expression:"),
                                                  QLineEdit::Normal, {}, &ok);
    if (!ok || pattern.isEmpty())
        return;

    const QByteArray utf8 = pattern.toUtf8();
    e->send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP);
    e->send(SCI_SETTARGETRANGE, e->send(SCI_GETCURRENTPOS), e->send(SCI_GETLENGTH));
    auto found = e->send(SCI_SEARCHINTARGET, utf8.size(), reinterpret_cast<sptr_t>(utf8.constData()));
    if (found < 0) { // wrap around
        e->send(SCI_SETTARGETRANGE, 0, e->send(SCI_GETLENGTH));
        found = e->send(SCI_SEARCHINTARGET, utf8.size(), reinterpret_cast<sptr_t>(utf8.constData()));
    }
    if (found < 0) {
        statusBar()->showMessage(tr("Not found: %1").arg(pattern), 3000);
        return;
    }
    e->send(SCI_SETSEL, e->send(SCI_GETTARGETSTART), e->send(SCI_GETTARGETEND));
    e->send(SCI_SCROLLCARET);
}

ScintillaEditBase* MainWindow::currentEditor() const
{
    return qobject_cast<ScintillaEditBase*>(m_tabs->currentWidget());
}

ScintillaEditBase* MainWindow::editorAt(int index) const
{
    return qobject_cast<ScintillaEditBase*>(m_tabs->widget(index));
}

int MainWindow::indexOfPath(const QString& path) const
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (QFileInfo(pathOf(editorAt(i))) == QFileInfo(path))
            return i;
    return -1;
}

// Phase 0 keeps window/dock state in QSettings; config.xml/session.xml come with npp-core.
void MainWindow::restoreSession()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());

    const QString backendKey = QStringLiteral("dock/%1/native").arg(m_dock->backendName());
    if (!m_dock->restoreNativeState(s.value(backendKey).toByteArray())) {
        // Different backend (or first run): fall back to the backend-independent layout.
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

void MainWindow::saveSession() const
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
    while (m_tabs->count() > 0) {
        ScintillaEditBase* e = editorAt(0);
        if (isDirty(e) && !closeEditor(0)) {
            event->ignore();
            return;
        }
        m_tabs->removeTab(0);
        e->deleteLater();
    }
    saveSession();
    event->accept();
}

} // namespace npp::ui
