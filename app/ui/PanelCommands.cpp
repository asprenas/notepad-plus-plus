// Phase 5 docking panels: Function List, Document Map, Project Panels, Character Panel,
// Clipboard History (all docked through DockManager only).
#include "MainWindow.h"

#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "panels/DocumentListPanel.h"
#include "panels/DocumentMapPanel.h"
#include "panels/FolderAsWorkspacePanel.h"
#include "search/FindReplaceDialog.h"
#include "panels/FunctionListPanel.h"
#include "panels/ProjectPanel.h"
#include "panels/SmallPanels.h"

#include "core/funclist/FunctionParser.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QDir>
#include <QTimer>

#include <array>
#include <QTreeWidget>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

} // namespace

const char* const MainWindow::kPanelFunctionList = "npp.functionList";
const char* const MainWindow::kPanelDocumentMap = "npp.documentMap";
const char* const MainWindow::kPanelFolderAsWorkspace = "npp.folderAsWorkspace";
const char* const MainWindow::kPanelDocumentList = "npp.documentList";
const char* const MainWindow::kPanelCharacter = "npp.charPanel";
const char* const MainWindow::kPanelClipboardHistory = "npp.clipboardHistory";

void MainWindow::createExtraPanels()
{
    // --- Document List
    m_docList = new DocumentListPanel;
    {
        const GuiConfig& g = m_config.gui;
        m_docList->setColumns({!g.fileSwitcherWithoutExtColumn, !g.fileSwitcherWithoutPathColumn, !g.fileSwitcherNoGroups,
                               g.fileSwitcherExtWidth, g.fileSwitcherPathWidth});
    }
    connect(m_docList, &DocumentListPanel::activateRequested, this, [this](BufferId id, int view) {
        EditorPane* p = m_panes[static_cast<size_t>(view)];
        p->activate(id);
        setActivePane(p);
    });
    auto toPanes = [this](const std::vector<std::pair<BufferId, int>>& docs) {
        std::vector<std::pair<BufferId, EditorPane*>> out;
        for (const auto& [id, v] : docs)
            out.emplace_back(id, m_panes[static_cast<size_t>(v)]);
        return out;
    };
    connect(m_docList, &DocumentListPanel::closeRequested, this,
            [this, toPanes](std::vector<std::pair<BufferId, int>> docs) { closeBuffers(toPanes(docs)); });
    connect(m_docList, &DocumentListPanel::saveRequested, this, [this](std::vector<std::pair<BufferId, int>> docs) {
        for (const auto& d : docs)
            if (Buffer* b = buffer(d.first))
                saveBuffer(b, false);
    });
    connect(m_docList, &DocumentListPanel::columnsChanged, this, &MainWindow::updateDocumentList);
    m_dock->addPanel({kPanelDocumentList, miscString("panel-documentList", "Document List"), {}, DockArea::Left, true, false},
                     m_docList);

    // --- Folder as Workspace
    m_folderPanel = new FolderAsWorkspacePanel;
    connect(m_folderPanel, &FolderAsWorkspacePanel::openFileRequested, this, [this](const QString& f) { openFile(f); });
    connect(m_folderPanel, &FolderAsWorkspacePanel::findInFilesRequested, this, [this](const QString& dir) {
        findDialog()->showTab(FindReplaceDialog::FindInFilesTab);
        findDialog()->setDirectory(QDir::toNativeSeparators(dir));
    });
    m_dock->addPanel({kPanelFolderAsWorkspace, miscString("panel-folderAsWorkspace", "Folder as Workspace"), {}, DockArea::Left, true, false},
                     m_folderPanel);
    m_setWorkspaceRoot = [this](const QString& dir) {
        m_folderPanel->addRoot(dir);
        m_dock->showPanel(kPanelFolderAsWorkspace);
    };
    m_folderPanel->restore(m_config.fileBrowser);

    // --- Function List
    m_funcParsers = std::make_unique<FunctionParsersManager>();
    m_funcParsers->init(pathAppend(m_paths.userDir(), "functionList"), pathAppend(m_dataDir, "functionList"));
    m_funcList = new FunctionListPanel;
    m_funcList->setSortByDefault(m_config.gui.sortFunctionList);
    connect(m_funcList, &FunctionListPanel::sortByDefaultChanged, this,
            [this](bool on) { m_config.gui.sortFunctionList = on; });
    connect(m_funcList, &FunctionListPanel::reloadRequested, this, &MainWindow::reloadFunctionList);
    connect(m_funcList, &FunctionListPanel::gotoPosition, this, [this](intptr_t pos) {
        ScintillaEditBase* e = currentPane()->editor();
        const auto line = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos));
        e->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(line));
        e->send(SCI_GOTOPOS, static_cast<uptr_t>(pos));
        e->send(SCI_SCROLLCARET);
        e->setFocus();
    });
    m_dock->addPanel({kPanelFunctionList, miscString("function-list-title", "Function List"), {}, DockArea::Right, true, false},
                     m_funcList);

    // --- Document Map
    m_docMap = new DocumentMapPanel;
    m_dock->addPanel({kPanelDocumentMap, miscString("document-map-title", "Document Map"), {}, DockArea::Right, true, false},
                     m_docMap);

    // --- Project Panels 1-3
    for (int i = 0; i < 3; ++i) {
        auto* pp = new ProjectPanel(i);
        m_projectPanels[static_cast<size_t>(i)] = pp;
        if (const std::string& ws = m_config.projectPanelFiles[static_cast<size_t>(i)]; !ws.empty())
            pp->openWorkspace(q(ws));
        connect(pp, &ProjectPanel::openFileRequested, this, [this](const QString& f) { openFile(f); });
        connect(pp, &ProjectPanel::workspaceChanged, this,
                [this, i](const QString& f) { m_config.projectPanelFiles[static_cast<size_t>(i)] = f.toStdString(); });
        m_dock->addPanel({projectPanelId(i), tr("Project Panel %1").arg(i + 1), {}, DockArea::Left, true, false}, pp);
    }

    // --- Character Panel, Clipboard History
    auto insertText = [this](const QByteArray& utf8) {
        ScintillaEditBase* e = currentPane()->editor();
        e->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>(utf8.constData()));
        e->setFocus();
    };
    m_charPanel = new CharacterPanel;
    connect(m_charPanel, &CharacterPanel::insertRequested, this, insertText);
    m_dock->addPanel({kPanelCharacter, miscString("panel-asciiInsertion", "ASCII Codes Insertion Panel"), {}, DockArea::Right, true, false},
                     m_charPanel);
    m_clipboardPanel = new ClipboardHistoryPanel;
    connect(m_clipboardPanel, &ClipboardHistoryPanel::insertRequested, this,
            [insertText](const QString& t) { insertText(t.toUtf8()); });
    m_dock->addPanel({kPanelClipboardHistory, miscString("panel-clipboardHistory", "Clipboard History"), {}, DockArea::Right, true, false},
                     m_clipboardPanel);

    m_panelTimer = new QTimer(this);
    m_panelTimer->setSingleShot(true);
    m_panelTimer->setInterval(1000); // upstream reloads the list at most once per second after edits
    connect(m_panelTimer, &QTimer::timeout, this, &MainWindow::reloadFunctionList);
    for (EditorPane* p : m_panes) {
        ScintillaEditBase* e = p->editor();
        connect(e, &ScintillaEditBase::modified, this, [this](Scintilla::ModificationFlags type, Scintilla::Position,
                                                              Scintilla::Position, Scintilla::Position,
                                                              const QByteArray&, Scintilla::Position,
                                                              Scintilla::FoldLevel, Scintilla::FoldLevel) {
            using Scintilla::ModificationFlags;
            if (Scintilla::FlagSet(type, ModificationFlags::InsertText) || Scintilla::FlagSet(type, ModificationFlags::DeleteText))
                if (m_dock->isPanelVisible(kPanelFunctionList))
                    m_panelTimer->start();
        });
        connect(e, &ScintillaEditBase::updateUi, this, [this, p](Scintilla::Update u) {
            if (p != currentPane())
                return;
            if (Scintilla::FlagSet(u, Scintilla::Update::Selection) && m_dock->isPanelVisible(kPanelFunctionList))
                markFunctionListEntry();
            if (m_dock->isPanelVisible(kPanelDocumentMap))
                m_docMap->sync();
        });
        connect(e, &ScintillaEditBase::zoom, this, [this, p](int) {
            if (p == currentPane() && m_dock->isPanelVisible(kPanelDocumentMap))
                m_docMap->sync();
        });
    }
    connect(m_dock.get(), &DockManager::panelVisibilityChanged, this, [this](const std::string& id, bool visible) {
        if (visible && id == kPanelFunctionList)
            reloadFunctionList();
        if (visible && id == kPanelDocumentMap)
            panelsOnBufferChanged();
    });
}

void MainWindow::panelsOnBufferChanged()
{
    if (m_funcList && m_dock->isPanelVisible(kPanelFunctionList))
        reloadFunctionList();
    if (m_charPanel)
        if (Buffer* b = currentBuffer())
            m_charPanel->setCodePage(b->encoding);
    if (m_docMap && m_dock->isPanelVisible(kPanelDocumentMap)) {
        Buffer* b = currentBuffer();
        m_docMap->setSource(currentPane()->editor(), b ? b->document : 0);
    }
}

void MainWindow::panelsAfterRestore()
{
    // upstream launches the Folder as Workspace panel when roots were saved
    if (m_folderPanel && !m_folderPanel->roots().isEmpty())
        m_dock->showPanel(kPanelFolderAsWorkspace, false);
}

void MainWindow::updateDocumentList()
{
    if (!m_docList)
        return;
    std::vector<DocumentListPanel::Entry> entries;
    for (const auto& [id, p] : allTabs())
        if (Buffer* b = buffer(id))
            entries.push_back({id, p->viewIndex(), q(b->displayName()), b->isUntitled() ? QString() : QDir::toNativeSeparators(q(b->path)),
                               b->dirty, b->isReadOnly(), p == currentPane() && id == p->current()});
    m_docList->setEntries(entries);
    if (m_charPanel) // encoding changes refresh the buffer too
        if (Buffer* b = currentBuffer())
            m_charPanel->setCodePage(b->encoding);
}

void MainWindow::panelsSaveSettings()
{
    if (m_docList) {
        const DocumentListPanel::Columns c = m_docList->columns();
        GuiConfig& g = m_config.gui;
        g.fileSwitcherWithoutExtColumn = !c.ext;
        g.fileSwitcherWithoutPathColumn = !c.path;
        g.fileSwitcherNoGroups = !c.groups;
        g.fileSwitcherExtWidth = c.extWidth;
        g.fileSwitcherPathWidth = c.pathWidth;
    }
    if (m_folderPanel)
        m_config.fileBrowser = m_folderPanel->settings();
}

bool MainWindow::panelsPrepareExit()
{
    for (ProjectPanel* pp : m_projectPanels)
        if (pp && !pp->askToSave())
            return false;
    return true;
}

void MainWindow::panelsOnStylesChanged()
{
    if (m_docMap && m_dock->isPanelVisible(kPanelDocumentMap))
        m_docMap->refreshStyles();
}

std::vector<FoundFunction> MainWindow::parseFunctions(Buffer* b, bool* parsed)
{
    std::vector<FoundFunction> found;
    bool ok = false;
    if (b && m_funcParsers) {
        m_scratch->send(SCI_SETDOCPOINTER, 0, b->document);
        ok = m_funcParsers->parse(found, *m_scratch, b->lang, b->userLangName);
        m_scratch->send(SCI_SETDOCPOINTER, 0, 0);
    }
    if (parsed)
        *parsed = ok;
    return found;
}

void MainWindow::reloadFunctionList()
{
    if (!m_funcList)
        return;
    m_panelTimer->stop();
    Buffer* b = currentBuffer();
    if (!b) {
        m_funcList->setResult({}, {}, {}, false);
        return;
    }
    bool parsed = false;
    const std::vector<FoundFunction> found = parseFunctions(b, &parsed);
    const QString key = b->path.empty() ? q(b->displayName()) : q(b->path);
    m_funcList->setResult(key, q(b->displayName()), found, parsed);
    markFunctionListEntry();
}

void MainWindow::markFunctionListEntry()
{
    ScintillaEditBase* e = currentPane()->editor();
    const auto line = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(e->send(SCI_GETCURRENTPOS)));
    m_funcList->markLine(line, [e](intptr_t pos) { return e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos)); });
}

void MainWindow::registerPanelCommands()
{
    ActionRegistry* r = m_actions;
    auto toggle = [this](const char* id) {
        return [this, id] { m_dock->isPanelVisible(id) ? m_dock->hidePanel(id) : m_dock->showPanel(id); };
    };
    auto visible = [this](const char* id) { return [this, id] { return m_dock->isPanelVisible(id); }; };
    auto focusPanel = [this](const char* id, std::function<QWidget*()> widget) {
        return [this, id, widget] {
            m_dock->showPanel(id);
            if (QWidget* w = widget())
                w->setFocus();
        };
    };
    r->setHandler(IDM_VIEW_FUNC_LIST, {toggle(kPanelFunctionList), {}, visible(kPanelFunctionList)});
    for (int i = 0; i < 3; ++i) {
        static const std::array<std::string, 3> ids{projectPanelId(0), projectPanelId(1), projectPanelId(2)};
        const char* id = ids[static_cast<size_t>(i)].c_str();
        r->setHandler(IDM_VIEW_PROJECT_PANEL_1 + i, {toggle(id), {}, visible(id)});
        r->setHandler(IDM_VIEW_SWITCHTO_PROJECT_PANEL_1 + i,
                      {focusPanel(id, [this, i]() -> QWidget* { return m_projectPanels[static_cast<size_t>(i)]->tree(); })});
    }
    r->setHandler(IDM_EDIT_CHAR_PANEL, {toggle(kPanelCharacter), {}, visible(kPanelCharacter)});
    r->setHandler(IDM_EDIT_CLIPBOARDHISTORY_PANEL, {toggle(kPanelClipboardHistory), {}, visible(kPanelClipboardHistory)});
    r->setHandler(IDM_VIEW_DOC_MAP, {toggle(kPanelDocumentMap), {}, visible(kPanelDocumentMap)});
    r->setHandler(IDM_VIEW_SWITCHTO_FUNC_LIST,
                  {focusPanel(kPanelFunctionList, [this]() -> QWidget* { return m_funcList->tree(); })});
}

} // namespace npp::ui
