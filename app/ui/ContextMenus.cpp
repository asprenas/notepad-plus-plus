// Edit zone context menu (contextMenu.xml), tab context menu (tabContextMenu.xml or upstream's
// built-in default), tab colours 1-5 and pinned tabs (ports of Notepad_plus TCN_TABPINNED,
// fileCloseAllButPinned, DocTabView::setIndividualTabColour, NppParameters::getContextMenuFromXmlTree).
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/config/ContextMenuConfig.h"
#include "core/util/PathUtil.h"

#include <QAction>
#include <QMenu>

#include <algorithm>
#include <filesystem>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

struct DefaultTabItem {
    int id;
    const char* name;
    int folder; // -1 none, else nativeLang <TabBar> CMDID of the sub menu
};

// Upstream default tab context menu (NppNotification.cpp, NM_RCLICK)
constexpr DefaultTabItem kDefaultTabMenu[] = {
    {IDM_FILE_CLOSE, "Close", -1},
    {IDM_FILE_CLOSEALL_BUT_CURRENT, "Close All BUT This", 0},
    {IDM_FILE_CLOSEALL_BUT_PINNED, "Close All BUT Pinned", 0},
    {IDM_FILE_CLOSEALL_TOLEFT, "Close All to the Left", 0},
    {IDM_FILE_CLOSEALL_TORIGHT, "Close All to the Right", 0},
    {IDM_FILE_CLOSEALL_UNCHANGED, "Close All Unchanged", 0},
    {IDM_PINTAB, "Pin Tab", -1},
    {IDM_FILE_SAVE, "Save", -1},
    {IDM_FILE_SAVEAS, "Save As...", -1},
    {IDM_FILE_OPEN_FOLDER, "Open Containing Folder in Explorer", 1},
    {IDM_FILE_OPEN_CMD, "Open Containing Folder in cmd", 1},
    {IDM_FILE_OPEN_POWERSHELL, "Open Containing Folder in PowerShell", 1},
    {IDM_FILE_CONTAININGFOLDERASWORKSPACE, "Open Containing Folder as Workspace", 1},
    {0, nullptr, 1},
    {IDM_FILE_OPEN_DEFAULT_VIEWER, "Open in Default Viewer", 1},
    {IDM_FILE_RENAME, "Rename", -1},
    {IDM_FILE_DELETE, "Move to Recycle Bin", -1},
    {IDM_FILE_RELOAD, "Reload", -1},
    {IDM_FILE_PRINT, "Print", -1},
    {0, nullptr, -1},
    {IDM_EDIT_TOGGLEREADONLY, "Read-Only in Notepad++", -1},
    {IDM_EDIT_TOGGLESYSTEMREADONLY, "Read-Only Attribute in Windows", -1},
    {0, nullptr, -1},
    {IDM_EDIT_FULLPATHTOCLIP, "Copy Full File Path", 2},
    {IDM_EDIT_FILENAMETOCLIP, "Copy Filename", 2},
    {IDM_EDIT_CURRENTDIRTOCLIP, "Copy Current Dir. Path", 2},
    {IDM_VIEW_GOTO_START, "Move to Start", 3},
    {IDM_VIEW_GOTO_END, "Move to End", 3},
    {0, nullptr, 3},
    {IDM_VIEW_GOTO_ANOTHER_VIEW, "Move to Other View", 3},
    {IDM_VIEW_CLONE_TO_ANOTHER_VIEW, "Clone to Other View", 3},
    {IDM_VIEW_GOTO_NEW_INSTANCE, "Move to New Instance", 3},
    {IDM_VIEW_LOAD_IN_NEW_INSTANCE, "Open in New Instance", 3},
    {IDM_VIEW_TAB_COLOUR_1, "Apply Color 1", 4},
    {IDM_VIEW_TAB_COLOUR_2, "Apply Color 2", 4},
    {IDM_VIEW_TAB_COLOUR_3, "Apply Color 3", 4},
    {IDM_VIEW_TAB_COLOUR_4, "Apply Color 4", 4},
    {IDM_VIEW_TAB_COLOUR_5, "Apply Color 5", 4},
    {IDM_VIEW_TAB_COLOUR_NONE, "Remove Color", 4},
};
constexpr const char* kDefaultTabFolders[] = {"Close Multiple Tabs", "Open into", "Copy to Clipboard", "Move Document",
                                              "Apply Color to Tab"};

} // namespace

QColor MainWindow::tabColour(int colourId) const
{
    if (colourId < 0 || colourId > 4)
        return {};
    const std::string name = (isDarkMode() ? "Tab color dark mode " : "Tab color ") + std::to_string(colourId + 1);
    if (const Style* s = m_styles.globalStyleByName(name); s && s->bg >= 0)
        return QColor::fromRgb(static_cast<QRgb>(s->bg));
    static constexpr QRgb light[] = {0xF3F0CB, 0xDBF3CB, 0xCBDBF3, 0xF3DBCB, 0xF3CBEE};
    static constexpr QRgb dark[] = {0x807848, 0x568048, 0x507094, 0x804849, 0x754880};
    return QColor::fromRgb((isDarkMode() ? dark : light)[colourId]);
}

void MainWindow::refreshTabDecorations(const Buffer& b)
{
    for (EditorPane* p : m_panes)
        p->setTabDecoration(b.id, tabColour(b.docColorId), b.pinned);
}

void MainWindow::registerTabCommands()
{
    ActionRegistry* r = m_actions;
    for (int id = IDM_VIEW_TAB_COLOUR_NONE; id <= IDM_VIEW_TAB_COLOUR_5; ++id)
        r->setHandler(id, {[this, id] {
            if (Buffer* b = currentBuffer()) {
                b->docColorId = id - IDM_VIEW_TAB_COLOUR_NONE - 1;
                refreshTabDecorations(*b);
            }
        }});
    r->setHandler(IDM_PINTAB, {[this] { togglePin(currentPane(), currentPane()->currentIndex()); },
                               [this] { return currentBuffer() != nullptr; }});
    r->setHandler(IDM_FILE_CLOSEALL_BUT_PINNED, {[this] {
        std::vector<std::pair<BufferId, EditorPane*>> list;
        for (EditorPane* p : m_panes) {
            const auto ids = p->buffers();
            for (auto it = ids.rbegin(); it != ids.rend(); ++it)
                if (const Buffer* b = buffer(*it); b && !b->pinned)
                    list.emplace_back(*it, p);
        }
        closeBuffers(list);
    }});
}

void MainWindow::togglePin(EditorPane* p, int index)
{
    Buffer* b = buffer(p->bufferAt(index));
    if (!b)
        return;
    // upstream tabToStart / tabToEnd: pinned tabs stay in front of the others
    int pinnedCount = 0;
    for (BufferId id : p->buffers())
        if (const Buffer* x = buffer(id); x && x->pinned)
            ++pinnedCount;
    if (!b->pinned)
        p->moveTab(index, std::min(pinnedCount, p->count() - 1));
    else
        p->moveTab(index, std::max(pinnedCount - 1, 0));
    b->pinned = !b->pinned;
    refreshTabDecorations(*b);
}

QMenu* MainWindow::buildContextMenu(const std::vector<ContextMenuEntry>& entries, QWidget* parent)
{
    auto* menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    std::vector<std::pair<QString, QMenu*>> folders;
    m_actions->updateStates();
    for (const ContextMenuEntry& e : entries) {
        QMenu* target = menu;
        if (!e.folder.isEmpty()) {
            auto it = std::find_if(folders.begin(), folders.end(), [&](const auto& f) { return f.first == e.folder; });
            if (it == folders.end()) {
                folders.emplace_back(e.folder, menu->addMenu(e.folder));
                it = std::prev(folders.end());
            }
            target = it->second;
        }
        if (e.id == 0) {
            target->addSeparator();
            continue;
        }
        QAction* shared = m_actions->action(e.id);
        if (!shared)
            continue;
        if (e.text.isEmpty()) {
            target->addAction(shared);
            continue;
        }
        QAction* a = target->addAction(e.text);
        a->setObjectName(QString::number(e.id));
        a->setEnabled(shared->isEnabled());
        a->setCheckable(shared->isCheckable());
        a->setChecked(shared->isChecked());
        a->setIcon(shared->icon());
        a->setShortcut(shared->shortcut());
        const int id = e.id;
        connect(a, &QAction::triggered, this, [this, id] { m_actions->trigger(id); });
    }
    return menu;
}

std::vector<MainWindow::ContextMenuEntry> MainWindow::contextMenuEntries(bool editZone) const
{
    std::vector<ContextMenuEntry> out;
    ContextMenuConfig cfg;
    const std::string userFile = editZone ? m_paths.contextMenuXml() : pathAppend(m_paths.userDir(), "tabContextMenu.xml");
    const bool loaded = cfg.load(userFile, editZone ? "ScintillaContextMenu" : "TabContextMenu")
                        || (editZone && cfg.load(pathAppend(m_dataDir, "contextMenu.model.xml"), "ScintillaContextMenu"));
    if (loaded) {
        for (const ContextMenuItem& it : cfg.items) {
            ContextMenuEntry e;
            e.folder = qs(it.folderName);
            if (const auto t = it.extraAttributes.find("TranslateID"); t != it.extraAttributes.end() && !e.folder.isEmpty())
                e.folder = qs(m_nativeLang.localizedString(t->second, it.folderName));
            e.text = qs(it.itemNameAs);
            if (it.id >= 0)
                e.id = it.id;
            else if (!it.menuEntryName.empty() && !it.menuItemName.empty())
                e.id = ActionRegistry::commandIdFromMenuNames(it.menuEntryName, it.menuItemName);
            else
                e.id = -1; // plugin commands: phase 7
            if (e.id >= 0)
                out.push_back(e);
        }
        return out;
    }
    if (editZone)
        return out;
    const auto names = m_nativeLang.itemNames({"Menu", "TabBar"}, "CMDID");
    auto tr = [&names](int id, const char* fallback) {
        const auto it = names.find(id);
        return it != names.end() ? qs(it->second) : QString::fromUtf8(fallback);
    };
    for (const DefaultTabItem& d : kDefaultTabMenu) {
        ContextMenuEntry e;
        e.id = d.id;
        if (d.folder >= 0)
            e.folder = tr(d.folder, kDefaultTabFolders[d.folder]);
        if (d.id != 0)
            e.text = tr(d.id, d.name);
        out.push_back(e);
    }
    return out;
}

void MainWindow::showEditorContextMenu(EditorPane* p, const QPoint& globalPos)
{
    setActivePane(p);
    buildContextMenu(contextMenuEntries(true), this)->popup(globalPos);
}

QMenu* MainWindow::tabContextMenu()
{
    QMenu* m = buildContextMenu(contextMenuEntries(false), this);
    const Buffer* b = currentBuffer();
    for (QAction* a : m->findChildren<QAction*>()) {
        const int id = a->objectName().toInt();
        if (id >= IDM_VIEW_TAB_COLOUR_1 && id <= IDM_VIEW_TAB_COLOUR_5) {
            QPixmap pm(16, 16);
            pm.fill(tabColour(id - IDM_VIEW_TAB_COLOUR_1));
            a->setIcon(QIcon(pm));
        } else if (id == IDM_PINTAB && b && b->pinned) {
            const auto alt = m_nativeLang.itemAttribute({"Menu", "TabBar"}, IDM_PINTAB, "alternativeName", "CMDID");
            a->setText(alt.empty() ? QStringLiteral("Unpin Tab") : qs(alt));
        }
    }
    return m;
}

void MainWindow::showTabContextMenu(EditorPane* p, int index, const QPoint& globalPos)
{
    Q_UNUSED(index);
    setActivePane(p);
    tabContextMenu()->popup(globalPos);
}

void MainWindow::ensureContextMenuFile()
{
    // upstream: contextMenu.xml is copied from the model on first start
    std::error_code ec;
    const auto dst = pathFromUtf8(m_paths.contextMenuXml());
    if (!std::filesystem::exists(dst, ec))
        std::filesystem::copy_file(pathFromUtf8(pathAppend(m_dataDir, "contextMenu.model.xml")), dst, ec);
}

} // namespace npp::ui
