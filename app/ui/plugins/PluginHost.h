// Plugin host (phase 7): loads plugins of the Qt plugin API (app/plugin-api/include/npp_plugin_api.h),
// builds their Plugins sub menus, forwards NPPN_* / SCN_* notifications, hosts their dock panels
// and answers the NPPM_* compatibility messages (PluginMessages.cpp).
//
// Faults are isolated per plugin: a plugin that cannot be loaded, has another API version, fails
// to initialise or throws from a call is disabled (its commands are greyed out) and reported; the
// application keeps running. Crashes inside plugin code (access violations) are not caught.
#pragma once

#include "core/shortcut/Shortcuts.h"

#include <npp_plugin_api.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

class QLibrary;
class QMenu;
class QAction;
class QToolBar;
class ScintillaEditBase;

namespace npp::ui {

class MainWindow;

class PluginHost final : public QObject {
    Q_OBJECT
public:
    // upstream resource.h ranges
    static constexpr int kFirstCommandId = 22000, kLastCommandId = 22999;           // ID_PLUGINS_CMD
    static constexpr int kFirstDynamicId = 23000, kLastDynamicId = 24999;           // NPPM_ALLOCATECMDID
    static constexpr int kFirstMarker = 1, kLastMarker = 15;                         // NPPM_ALLOCATEMARKER
    static constexpr int kFirstIndicator = 9, kLastIndicator = 20;                   // NPPM_ALLOCATEINDICATOR

    struct Error {
        QString file;
        QString reason;
    };
    struct Command {               // one menu item of a plugin (Shortcut Mapper, tests)
        std::string module;        // library file name, shortcuts.xml moduleName
        int index = 0;             // shortcuts.xml internalID
        int cmdId = 0;
        QString plugin, name;
        core::KeyCombo defaultKey;
    };

    explicit PluginHost(MainWindow* window);
    ~PluginHost() override;

    // Plugin folders, in load order: <app>/plugins (Linux install: <prefix>/lib/notepadpp-qt/plugins)
    // then <settings>/plugins. A module name loads once (the first one found).
    QStringList pluginDirs() const;
    void loadAll();
    // Loads one library file; false (and an entry in errors()) if refused.
    bool load(const QString& file);

    // Adds the plugin sub menus to the Plugins menu (before `before`, upstream order).
    void buildMenu(QMenu* pluginsMenu, QAction* before);
    // Default shortcuts of plugin commands + shortcuts.xml <PluginCommands>.
    void applyShortcuts(const core::ShortcutsFile& shortcuts);
    std::vector<Command> commands() const;

    // NPPN_* notification (upstream idFrom in `id`).
    void notify(uint32_t code, uintptr_t id = 0);
    // SCN_* notifications of an editor view are forwarded to the plugins.
    void connectEditor(int view, ScintillaEditBase* editor);
    // NPPN_SHUTDOWN then npp_plugin_shutdown of every plugin (once).
    void shutdown();

    const std::vector<Error>& errors() const noexcept { return m_errors; }
    QStringList loadedPlugins() const;     // display names of the enabled plugins
    bool isEnabled(const QString& name) const;
    int pluginCount() const noexcept { return static_cast<int>(m_plugins.size()); }

    // NPPM_* message (PluginMessages.cpp)
    intptr_t message(unsigned msg, uintptr_t w, intptr_t l);
    // Panel id in the DockManager for a plugin panel
    static std::string dockId(const std::string& module, const std::string& panelId);

    struct Plugin {
        std::unique_ptr<QLibrary> lib;
        QString file;
        std::string module;            // library file name ("mimeTools.dll")
        QString name, version;
        npp_plugin_notify_fn notify = nullptr;
        npp_plugin_message_fn message = nullptr;
        npp_plugin_shutdown_fn shutdown = nullptr;
        NppMenuItem* items = nullptr;
        int itemCount = 0;
        std::vector<int> cmdIds;       // per item, 0 = separator
        std::vector<std::string> panels;
        std::set<int> checkable;
        bool enabled = true;
        bool initialised = false;
    };
    // for the C entry points of NppApi (one host at a time)
    static PluginHost* instance() noexcept;
    MainWindow* window() const noexcept { return m_window; }
    int registerPanel(Plugin* p, const char* panelId, const char* title, void* widget);
    void showPanel(Plugin* p, const char* panelId, bool show);
    bool isPanelVisible(Plugin* p, const char* panelId) const;
    void setMenuItemCheck(int cmdId, bool checked);
    bool runPluginCommand(int cmdId);
    bool activateBuffer(uint64_t bufferId);
    bool addToolbarIcon(int cmdId, const QString& icon, const QString& darkIcon);
    // Plugin buttons at the end of the toolbar (called by MainWindow::buildToolbar)
    void addToolbarButtons(QToolBar* toolbar, bool dark, int size);

private:
    bool guarded(Plugin& p, const char* what, const std::function<void()>& call);
    void disable(Plugin& p, const QString& reason);
    void addError(const QString& file, const QString& reason);
    Plugin* pluginOfCommand(int cmdId) const;
    Plugin* pluginByModule(const std::string& module) const;
    void forwardScn(int view, const void* scn, uint32_t code);

    MainWindow* m_window;
    std::vector<std::unique_ptr<Plugin>> m_plugins;
    std::vector<Error> m_errors;
    struct ToolbarIcon {
        int cmdId;
        QString light, dark;
    };
    std::vector<ToolbarIcon> m_toolbarIcons;
    int m_nextCommandId = kFirstCommandId;
    int m_nextDynamicId = kFirstDynamicId;
    int m_nextMarker = kFirstMarker;
    int m_nextIndicator = kFirstIndicator;
    bool m_shutDown = false;
};

} // namespace npp::ui
