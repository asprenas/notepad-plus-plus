#include "ActionRegistry.h"

#include "menu/CommandIds.h"

#include "core/i18n/NativeLanguage.h"
#include "core/shortcut/Shortcuts.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>

#include <vector>

namespace npp::ui {

using namespace cmd;

namespace {

enum class Node { Begin, End, Item, Separator };
struct MenuNode {
    Node kind;
    const char* text;
    int id;
};

const std::vector<MenuNode>& menuTree()
{
    static const std::vector<MenuNode> tree = {
#define NPP_POPUP_BEGIN(t) {Node::Begin, t, 0},
#define NPP_POPUP_END() {Node::End, nullptr, 0},
#define NPP_ITEM(t, id) {Node::Item, t, id},
#define NPP_SEPARATOR() {Node::Separator, nullptr, 0},
#include "menu/MainMenu.inc"
#undef NPP_POPUP_BEGIN
#undef NPP_POPUP_END
#undef NPP_ITEM
#undef NPP_SEPARATOR
    };
    return tree;
}

struct DefKey {
    int vk, id;
    bool ctrl, alt, shift;
};

const std::vector<DefKey>& defaultKeys()
{
    static const std::vector<DefKey> keys = {
#define NPP_DEFKEY(vk, id, c, a, s, name) {vk, id, c, a, s},
#include "menu/DefaultShortcuts.inc"
#undef NPP_DEFKEY
    };
    return keys;
}

// Top-level popups of IDR_M30_MENU, in order, with their nativeLang menuId.
// Index 5 / 6 are the full / compact Language menus (upstream removes one at start-up).
const char* const kTopLevelIds[] = {"file", "edit", "search", "view", "encoding", "language", "language",
                                    "settings", "tools", "macro", "run", "Plugins", "Window", "?", ""};

// Sub menus translated from Menu/Main/SubEntries by position (upstream localization.cpp
// g_menuFolderPositions: top-level index after the unused Language menu is removed, position in
// the popup counting separators, position in the sub popup).
struct SubMenuPosition {
    int x, y, z;
    const char* id;
};
constexpr SubMenuPosition kSubMenuPositions[] = {
    {0, 2, -1, "file-openFolder"}, {0, 13, -1, "file-closeMore"}, {0, 22, -1, "file-recentFiles"},
    {1, 11, -1, "edit-insert"}, {1, 12, -1, "edit-copyToClipboard"}, {1, 13, -1, "edit-indent"},
    {1, 14, -1, "edit-convertCaseTo"}, {1, 15, -1, "edit-lineOperations"}, {1, 16, -1, "edit-comment"},
    {1, 17, -1, "edit-autoCompletion"}, {1, 18, -1, "edit-eolConversion"}, {1, 19, -1, "edit-blankOperations"},
    {1, 20, -1, "edit-pasteSpecial"}, {1, 21, -1, "edit-onSelection"}, {1, 23, -1, "edit-multiSelectALL"},
    {1, 24, -1, "edit-multiSelectNext"}, {1, 33, -1, "edit-readonlyInNotepad++"},
    {2, 18, -1, "search-changeHistory"}, {2, 20, -1, "search-markAll"}, {2, 21, -1, "search-markOne"},
    {2, 22, -1, "search-unmarkAll"}, {2, 23, -1, "search-jumpUp"}, {2, 24, -1, "search-jumpDown"},
    {2, 25, -1, "search-copyStyledText"}, {2, 27, -1, "search-bookmark"},
    {3, 5, -1, "view-currentFileIn"}, {3, 7, -1, "view-showSymbol"}, {3, 8, -1, "view-zoom"},
    {3, 9, -1, "view-moveCloneDocument"}, {3, 10, -1, "view-tab"}, {3, 19, -1, "view-collapseLevel"},
    {3, 20, -1, "view-uncollapseLevel"}, {3, 24, -1, "view-project"},
    {4, 5, -1, "encoding-characterSets"}, {4, 5, 0, "encoding-arabic"}, {4, 5, 1, "encoding-baltic"},
    {4, 5, 2, "encoding-celtic"}, {4, 5, 3, "encoding-cyrillic"}, {4, 5, 4, "encoding-centralEuropean"},
    {4, 5, 5, "encoding-chinese"}, {4, 5, 6, "encoding-easternEuropean"}, {4, 5, 7, "encoding-greek"},
    {4, 5, 8, "encoding-hebrew"}, {4, 5, 9, "encoding-japanese"}, {4, 5, 10, "encoding-korean"},
    {4, 5, 11, "encoding-northEuropean"}, {4, 5, 12, "encoding-thai"}, {4, 5, 13, "encoding-turkish"},
    {4, 5, 14, "encoding-westernEuropean"}, {4, 5, 15, "encoding-vietnamese"},
    {5, 25, -1, "language-userDefinedLanguage"},
    {6, 4, -1, "settings-import"},
    {7, 0, -1, "tools-md5"}, {7, 1, -1, "tools-sha1"}, {7, 2, -1, "tools-sha256"}, {7, 3, -1, "tools-sha512"},
    {11, 0, -1, "window-sortby"},
};

const char* subMenuId(int x, int y, int z)
{
    for (const SubMenuPosition& p : kSubMenuPositions)
        if (p.x == x && p.y == y && p.z == z)
            return p.id;
    return nullptr;
}

QKeySequence toSequence(const core::KeyCombo& k)
{
    const auto q = core::toQtKey(k);
    return q ? QKeySequence(*q) : QKeySequence();
}

} // namespace

ActionRegistry::ActionRegistry(QWidget* owner) : QObject(owner), m_owner(owner) {}

QAction* ActionRegistry::action(int id)
{
    if (auto it = m_actions.find(id); it != m_actions.end())
        return it->second;
    auto* a = new QAction(m_owner);
    a->setData(id);
    a->setShortcutContext(Qt::WindowShortcut);
    m_owner->addAction(a); // shortcuts work even when the menu bar is hidden
    connect(a, &QAction::triggered, this, [this, id] { trigger(id); });
    m_actions[id] = a;
    return a;
}

void ActionRegistry::setHandler(int id, Handler h)
{
    QAction* a = action(id);
    a->setCheckable(static_cast<bool>(h.checked));
    m_handlers[id] = std::move(h);
}

bool ActionRegistry::trigger(int id)
{
    const auto it = m_handlers.find(id);
    if (it == m_handlers.end() || !it->second.run)
        return false;
    if (it->second.enabled && !it->second.enabled())
        return false;
    it->second.run();
    emit commandTriggered(id);
    updateStates();
    return true;
}

std::map<std::string, QMenu*> ActionRegistry::buildMenuBar(QMenuBar* bar, const core::NativeLanguage* lang,
                                                           bool compactLanguageMenu)
{
    std::map<std::string, QMenu*> top;
    std::vector<QMenu*> stack;
    std::vector<int> path;     // position of each open popup (top-level index after the Language variant removal)
    std::vector<int> children; // children added so far to each open popup (separators included, like Win32)
    int topIndex = -1;
    bool skip = false; // inside the Language menu variant not used
    int skipDepth = 0;
    m_menuCommands.clear();

    for (const MenuNode& n : menuTree()) {
        if (skip) {
            if (n.kind == Node::Begin) ++skipDepth;
            if (n.kind == Node::End && --skipDepth == 0) skip = false;
            continue;
        }
        switch (n.kind) {
        case Node::Begin: {
            QString text = QString::fromUtf8(n.text);
            if (stack.empty()) {
                ++topIndex;
                const bool unused = (topIndex == 5 && compactLanguageMenu) || (topIndex == 6 && !compactLanguageMenu)
                    || topIndex >= 14; // trailing "▼" tab list popup (not a menu in the Qt UI)
                if (unused) {
                    skip = true;
                    skipDepth = 1;
                    continue;
                }
                const char* menuId = kTopLevelIds[std::min(topIndex, 14)];
                if (lang && lang->isLoaded())
                    text = QString::fromStdString(lang->mainMenuEntry(menuId, n.text));
                QMenu* m = bar->addMenu(text);
                m->setObjectName(QString::fromLatin1(menuId));
                top[menuId] = m;
                stack.push_back(m);
                path.push_back(topIndex > 6 ? topIndex - 1 : std::min(topIndex, 5));
            } else {
                path.push_back(children.back()++);
                if (lang && lang->isLoaded() && path.size() <= 3) {
                    if (const char* id = subMenuId(path[0], path[1], path.size() == 3 ? path[2] : -1))
                        text = QString::fromStdString(lang->subMenuEntry(id, n.text));
                }
                QMenu* m = stack.back()->addMenu(text);
                stack.push_back(m);
            }
            children.push_back(0);
            connect(stack.back(), &QMenu::aboutToShow, this, &ActionRegistry::updateStates);
            break;
        }
        case Node::End:
            if (!stack.empty()) {
                stack.pop_back();
                path.pop_back();
                children.pop_back();
            }
            break;
        case Node::Separator:
            if (!stack.empty()) {
                stack.back()->addSeparator();
                ++children.back();
            }
            break;
        case Node::Item: {
            QAction* a = action(n.id);
            QString text = QString::fromUtf8(n.text);
            if (lang && lang->isLoaded())
                text = QString::fromStdString(lang->menuCommand(n.id, n.text));
            a->setText(text);
            if (n.id == IDM_FILE_EXIT)
                a->setMenuRole(QAction::QuitRole);
            else if (n.id == IDM_ABOUT)
                a->setMenuRole(QAction::AboutRole);
            else if (n.id == IDM_SETTING_PREFERENCE)
                a->setMenuRole(QAction::PreferencesRole);
            else
                a->setMenuRole(QAction::NoRole);
            if (!stack.empty()) {
                stack.back()->addAction(a);
                ++children.back();
            }
            m_menuCommands.push_back(n.id);
            break;
        }
        }
    }
    updateStates();
    return top;
}

std::optional<core::KeyCombo> ActionRegistry::defaultShortcut(int id) const
{
    for (const DefKey& k : defaultKeys())
        if (k.id == id && k.vk != 0)
            return core::KeyCombo{k.ctrl, k.alt, k.shift, k.vk};
    return std::nullopt;
}

void ActionRegistry::applyShortcuts(const core::ShortcutsFile* user)
{
    std::map<int, QList<QKeySequence>> seqs;
    for (const DefKey& k : defaultKeys())
        if (k.vk != 0)
            seqs[k.id].append(toSequence({k.ctrl, k.alt, k.shift, k.vk}));
    if (user)
        for (const auto& c : user->internalCommands) {
            auto& list = seqs[c.id];
            if (c.nth == 0)
                list.clear();
            if (c.combo.isEnabled())
                list.append(toSequence(c.combo));
        }
    for (auto& [id, list] : seqs) {
        list.removeAll(QKeySequence());
        action(id)->setShortcuts(list);
    }
}

void ActionRegistry::updateStates()
{
    for (auto& [id, a] : m_actions) {
        const auto it = m_handlers.find(id);
        if (it == m_handlers.end()) {
            a->setEnabled(false);
            continue;
        }
        a->setEnabled(!it->second.enabled || it->second.enabled());
        if (it->second.checked) {
            const QSignalBlocker block(a);
            a->setChecked(it->second.checked());
        }
    }
}

std::vector<int> ActionRegistry::unimplementedMenuCommands() const
{
    std::vector<int> out;
    for (int id : m_menuCommands)
        if (!m_handlers.count(id))
            out.push_back(id);
    return out;
}

int ActionRegistry::commandIdFromMenuNames(std::string_view entryName, std::string_view itemName)
{
    auto norm = [](std::string_view t) {
        return QString::fromStdString(core::NativeLanguage::purifyMenuString(std::string(t))).section(QChar(9), 0, 0).trimmed().toCaseFolded();
    };
    const QString entry = norm(entryName), item = norm(itemName);
    int depth = 0;
    bool inEntry = false;
    for (const MenuNode& n : menuTree()) {
        if (n.kind == Node::Begin) {
            if (depth == 0)
                inEntry = norm(n.text) == entry;
            ++depth;
        } else if (n.kind == Node::End) {
            --depth;
        } else if (n.kind == Node::Item && inEntry && norm(n.text) == item) {
            return n.id;
        }
    }
    return -1;
}

} // namespace npp::ui
