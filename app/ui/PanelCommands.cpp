// Phase 5 docking panels: Function List, Document Map, Project Panels, Character Panel,
// Clipboard History (all docked through DockManager only).
#include "MainWindow.h"

#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "panels/DocumentMapPanel.h"
#include "panels/FunctionListPanel.h"

#include "core/funclist/FunctionParser.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QTimer>
#include <QTreeWidget>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

} // namespace

const char* const MainWindow::kPanelFunctionList = "npp.functionList";
const char* const MainWindow::kPanelDocumentMap = "npp.documentMap";

void MainWindow::createExtraPanels()
{
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
    if (m_docMap && m_dock->isPanelVisible(kPanelDocumentMap)) {
        Buffer* b = currentBuffer();
        m_docMap->setSource(currentPane()->editor(), b ? b->document : 0);
    }
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
    r->setHandler(IDM_VIEW_DOC_MAP, {toggle(kPanelDocumentMap), {}, visible(kPanelDocumentMap)});
    r->setHandler(IDM_VIEW_SWITCHTO_FUNC_LIST,
                  {focusPanel(kPanelFunctionList, [this]() -> QWidget* { return m_funcList->tree(); })});
}

} // namespace npp::ui
