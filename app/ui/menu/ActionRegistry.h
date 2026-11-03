// Command registry: one QAction per upstream IDM_* command id (phase 2, P2-01).
// Menus are built from the generated MainMenu.inc tree; texts come from the nativeLang
// translation when one is loaded; shortcuts from the upstream defaults + shortcuts.xml.
#pragma once

#include <QObject>

#include <functional>
#include <map>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

namespace npp::core {
class NativeLanguage;
struct ShortcutsFile;
struct KeyCombo;
}

namespace npp::ui {

class ActionRegistry final : public QObject {
    Q_OBJECT
public:
    struct Handler {
        std::function<void()> run;
        std::function<bool()> enabled;   // empty = always enabled
        std::function<bool()> checked;   // empty = not checkable
    };

    explicit ActionRegistry(QWidget* owner);

    // Action of a command (created on first use).
    QAction* action(int id);
    bool hasHandler(int id) const { return m_handlers.count(id) != 0; }
    void setHandler(int id, Handler h);
    // Runs a command as if chosen from the menu; false if no handler.
    bool trigger(int id);

    // Builds the main menu bar. `compactLanguageMenu` selects upstream's compact / full Language menu.
    // Returns the top-level menus by their nativeLang menuId ("file", "edit", ...).
    std::map<std::string, QMenu*> buildMenuBar(QMenuBar* bar, const core::NativeLanguage* lang,
                                               bool compactLanguageMenu);

    // Upstream default shortcuts, then the user's shortcuts.xml overrides.
    void applyShortcuts(const core::ShortcutsFile* userShortcuts);
    std::optional<core::KeyCombo> defaultShortcut(int id) const;

    // Refreshes enabled / checked state from the handlers (call before menus are shown).
    void updateStates();
    // Commands that appear in the menu but have no handler yet (later phases).
    std::vector<int> unimplementedMenuCommands() const;
    const std::vector<int>& menuCommands() const noexcept { return m_menuCommands; }

signals:
    void commandTriggered(int id);

private:
    QWidget* m_owner;
    std::map<int, QAction*> m_actions;
    std::map<int, Handler> m_handlers;
    std::vector<int> m_menuCommands;
};

} // namespace npp::ui
