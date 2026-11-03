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
            } else {
                QMenu* m = stack.back()->addMenu(text);
                stack.push_back(m);
            }
            connect(stack.back(), &QMenu::aboutToShow, this, &ActionRegistry::updateStates);
            break;
        }
        case Node::End:
            if (!stack.empty())
                stack.pop_back();
            break;
        case Node::Separator:
            if (!stack.empty())
                stack.back()->addSeparator();
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
            if (!stack.empty())
                stack.back()->addAction(a);
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

} // namespace npp::ui
