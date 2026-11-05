// Plugin host (phase 7): see PluginHost.h.
#include "plugins/PluginHost.h"

#include "MainWindow.h"
#include "StartupLog.h"
#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"

#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>
#include <ScintillaStructures.h>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QStatusBar>
#include <QToolBar>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <set>

namespace npp::ui {

using namespace npp::core;
namespace fs = std::filesystem;


namespace {

PluginHost* g_host = nullptr;

// "libmimeTools.so" / "mimeTools.dll" / "mimetools" -> "mimetools"
std::string normalizedModule(std::string_view name)
{
    std::string s(name);
    if (const auto dot = s.rfind('.'); dot != std::string::npos) {
        const std::string ext = s.substr(dot);
        if (ext == ".dll" || ext == ".so" || ext == ".dylib" || ext == ".DLL")
            s.erase(dot);
    }
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.size() > 3 && s.compare(0, 3, "lib") == 0)
        s.erase(0, 3);
    return s;
}

int copyOut(const std::string& s, char* buf, size_t size)
{
    if (buf && size > 0) {
        const size_t n = std::min(s.size(), size - 1);
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
    }
    return static_cast<int>(s.size());
}

PluginHost::Plugin* pluginOf(NppPluginHandle h) { return reinterpret_cast<PluginHost::Plugin*>(h); }

QString u(const char* s) { return s ? QString::fromUtf8(s) : QString(); }

// --- NppApi entry points ---------------------------------------------------------------------
MainWindow* win() { return g_host ? g_host->window() : nullptr; }

const char* apiAppVersion() { return NPP_QT_VERSION; }
void* apiMainWindow() { return static_cast<QWidget*>(win()); }

int apiPluginConfigDir(char* buf, size_t size)
{
    if (!win())
        return -1;
    const std::string dir = pathAppend(pathAppend(win()->paths().userDir(), "plugins"), "config");
    std::error_code ec;
    fs::create_directories(pathFromUtf8(dir), ec);
    return copyOut(dir, buf, size);
}

void apiSetStatusText(const char* text)
{
    if (win())
        win()->statusBar()->showMessage(u(text), 5000);
}

int apiIsDarkMode() { return win() && win()->isDarkMode() ? 1 : 0; }

int apiMessageBox(const char* text, const char* title, unsigned type)
{
    if (!win())
        return NPP_IDCANCEL;
    QMessageBox box(win());
    box.setWindowTitle(u(title));
    box.setText(u(text));
    switch (type & 0xF0) {
    case NPP_MB_ICONERROR: box.setIcon(QMessageBox::Critical); break;
    case NPP_MB_ICONQUESTION: box.setIcon(QMessageBox::Question); break;
    case NPP_MB_ICONWARNING: box.setIcon(QMessageBox::Warning); break;
    case NPP_MB_ICONINFORMATION: box.setIcon(QMessageBox::Information); break;
    default: break;
    }
    switch (type & 0x0F) {
    case NPP_MB_OKCANCEL: box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel); break;
    case NPP_MB_YESNOCANCEL: box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel); break;
    case NPP_MB_YESNO: box.setStandardButtons(QMessageBox::Yes | QMessageBox::No); break;
    default: box.setStandardButtons(QMessageBox::Ok); break;
    }
    switch (box.exec()) {
    case QMessageBox::Ok: return NPP_IDOK;
    case QMessageBox::Yes: return NPP_IDYES;
    case QMessageBox::No: return NPP_IDNO;
    default: return NPP_IDCANCEL;
    }
}

intptr_t apiCurrentView() { return win() && win()->currentPane() ? win()->currentPane()->viewIndex() : 0; }

intptr_t apiSendToEditor(intptr_t view, unsigned msg, uintptr_t w, intptr_t l)
{
    MainWindow* mw = win();
    if (!mw)
        return 0;
    EditorPane* p = view == NPP_VIEW_CURRENT ? mw->currentPane() : (view == 0 || view == 1) ? mw->pane(static_cast<int>(view)) : nullptr;
    return p ? p->editor()->send(msg, w, l) : 0;
}

NppBufferId apiCurrentBufferId()
{
    const Buffer* b = win() ? win()->currentBuffer() : nullptr;
    return b ? static_cast<NppBufferId>(b->id) : 0;
}

int apiBufferPath(NppBufferId id, char* buf, size_t size)
{
    const Buffer* b = win() ? win()->files().find(id) : nullptr;
    if (!b)
        return -1;
    return copyOut(b->isUntitled() ? b->untitledName : b->path, buf, size);
}

int apiCurrentFilePath(char* buf, size_t size) { return apiBufferPath(apiCurrentBufferId(), buf, size); }

int apiOpenFile(const char* path)
{
    return win() && path && win()->openFile(QString::fromUtf8(path)) ? 1 : 0;
}

int apiActivateBuffer(NppBufferId id)
{
    return g_host && g_host->activateBuffer(id) ? 1 : 0;
}

void apiRunMenuCommand(int id)
{
    if (win())
        win()->actions()->trigger(id);
}

void apiSetMenuItemCheck(int cmdId, int checked)
{
    if (g_host)
        g_host->setMenuItemCheck(cmdId, checked != 0);
}

int apiRegisterPanel(NppPluginHandle self, const char* panelId, const char* title, void* widget)
{
    return g_host ? g_host->registerPanel(pluginOf(self), panelId, title, widget) : 0;
}

void apiShowPanel(NppPluginHandle self, const char* panelId, int show)
{
    if (g_host)
        g_host->showPanel(pluginOf(self), panelId, show != 0);
}

int apiIsPanelVisible(NppPluginHandle self, const char* panelId)
{
    return g_host && g_host->isPanelVisible(pluginOf(self), panelId) ? 1 : 0;
}

intptr_t apiNppMessage(unsigned msg, uintptr_t w, intptr_t l)
{
    return g_host ? g_host->message(msg, w, l) : 0;
}

int apiPluginFilePath(NppPluginHandle self, char* buf, size_t size)
{
    const PluginHost::Plugin* p = pluginOf(self);
    return p ? copyOut(QDir::toNativeSeparators(p->file).toStdString(), buf, size) : -1;
}

int apiAddToolbarIcon(int cmdId, const char* icon, const char* darkIcon)
{
    return g_host && icon && g_host->addToolbarIcon(cmdId, u(icon), u(darkIcon)) ? 1 : 0;
}

const NppApi kApi = {
    sizeof(NppApi),
    NPP_PLUGIN_API_VERSION,
    apiAppVersion,
    apiMainWindow,
    apiPluginConfigDir,
    apiSetStatusText,
    apiIsDarkMode,
    apiMessageBox,
    apiCurrentView,
    apiSendToEditor,
    apiCurrentBufferId,
    apiBufferPath,
    apiCurrentFilePath,
    apiOpenFile,
    apiActivateBuffer,
    apiRunMenuCommand,
    apiSetMenuItemCheck,
    apiRegisterPanel,
    apiShowPanel,
    apiIsPanelVisible,
    apiNppMessage,
    apiPluginFilePath,
    apiAddToolbarIcon,
};

} // namespace

PluginHost::PluginHost(MainWindow* window) : QObject(window), m_window(window)
{
    g_host = this;
}

PluginHost::~PluginHost()
{
    shutdown();
    if (g_host == this)
        g_host = nullptr;
    // Libraries stay loaded (QLibrary does not unload on destruction): widgets, callbacks or
    // static destructors of a plugin may still run while the application ends.
}

PluginHost* PluginHost::instance() noexcept { return g_host; }

std::string PluginHost::dockId(const std::string& module, const std::string& panelId)
{
    return "plugin." + normalizedModule(module) + "." + panelId;
}

QStringList PluginHost::pluginDirs() const
{
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    dirs << appDir + QStringLiteral("/plugins");
#if defined(Q_OS_LINUX)
    dirs << appDir + QStringLiteral("/../lib/notepadpp-qt/plugins"); // FHS install, AppImage
#elif defined(Q_OS_MACOS)
    dirs << appDir + QStringLiteral("/../PlugIns");
#endif
    dirs << QString::fromStdString(m_window->pluginsDir());
    QStringList out;
    for (const QString& d : dirs) {
        const QString c = QDir::cleanPath(d);
        if (!out.contains(c) && QFileInfo(c).isDir())
            out << c;
    }
    return out;
}

void PluginHost::loadAll()
{
    for (const QString& dir : pluginDirs()) {
        // upstream layout: plugins/<name>/<name>.dll (Linux: lib<name>.so or <name>.so)
        for (const QFileInfo& sub : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString n = sub.fileName();
#if defined(Q_OS_WIN)
            const QStringList candidates{n + QStringLiteral(".dll")};
#elif defined(Q_OS_MACOS)
            const QStringList candidates{QStringLiteral("lib") + n + QStringLiteral(".dylib"), n + QStringLiteral(".dylib")};
#else
            const QStringList candidates{QStringLiteral("lib") + n + QStringLiteral(".so"), n + QStringLiteral(".so")};
#endif
            for (const QString& c : candidates) {
                const QString file = sub.absoluteFilePath() + QLatin1Char('/') + c;
                if (QFileInfo(file).isFile()) {
                    if (!pluginByModule(c.toStdString()))
                        load(file);
                    break;
                }
            }
        }
    }
    startupLog("  plugins loaded");
}

void PluginHost::addError(const QString& file, const QString& reason)
{
    m_errors.push_back({file, reason});
    std::fprintf(stderr, "plugin %s: %s\n", qPrintable(QDir::toNativeSeparators(file)), qPrintable(reason));
}

bool PluginHost::load(const QString& file)
{
    auto p = std::make_unique<Plugin>();
    p->file = file;
    p->module = QFileInfo(file).fileName().toStdString();
    p->lib = std::make_unique<QLibrary>(file);
    if (!p->lib->load()) {
        addError(file, tr("cannot be loaded: %1").arg(p->lib->errorString()));
        return false;
    }
    const auto info = reinterpret_cast<npp_plugin_info_fn>(p->lib->resolve("npp_plugin_info"));
    const auto init = reinterpret_cast<npp_plugin_init_fn>(p->lib->resolve("npp_plugin_init"));
    if (!info || !init) {
        // upstream Win32 plugins export getName / setInfo: not binary compatible
        const bool win32Plugin = p->lib->resolve("getName") || p->lib->resolve("setInfo");
        addError(file, win32Plugin ? tr("is a Notepad++ (Win32) plugin: rebuild it with the Qt plugin API")
                                   : tr("is not a Notepad++ Qt plugin (npp_plugin_info / npp_plugin_init missing)"));
        p->lib->unload();
        return false;
    }
    const NppPluginInfo* pi = nullptr;
    if (!guarded(*p, "npp_plugin_info", [&] { pi = info(); }) || !pi) {
        addError(file, tr("npp_plugin_info failed"));
        return false; // library stays loaded: an exception may have left state behind
    }
    if (pi->apiVersion != NPP_PLUGIN_API_VERSION) {
        addError(file, tr("was built for plugin API version %1, this version of Notepad++ Qt needs version %2")
                           .arg(pi->apiVersion)
                           .arg(NPP_PLUGIN_API_VERSION));
        p->lib->unload();
        return false;
    }
    p->name = pi->name && *pi->name ? QString::fromUtf8(pi->name) : QFileInfo(file).completeBaseName();
    p->version = u(pi->version);
    p->notify = reinterpret_cast<npp_plugin_notify_fn>(p->lib->resolve("npp_plugin_notify"));
    p->message = reinterpret_cast<npp_plugin_message_fn>(p->lib->resolve("npp_plugin_message"));
    p->shutdown = reinterpret_cast<npp_plugin_shutdown_fn>(p->lib->resolve("npp_plugin_shutdown"));
    const auto menuItems = reinterpret_cast<npp_plugin_menu_items_fn>(p->lib->resolve("npp_plugin_menu_items"));

    Plugin& plugin = *p;
    m_plugins.push_back(std::move(p));
    // upstream order: setInfo, getFuncsArray (menu), later NPPN_READY
    int rc = 0;
    if (!guarded(plugin, "npp_plugin_init", [&] { rc = init(&kApi, reinterpret_cast<NppPluginHandle>(&plugin)); }))
        return false;
    plugin.initialised = true;
    if (rc != 0) {
        disable(plugin, tr("npp_plugin_init returned %1").arg(rc));
        return false;
    }
    if (menuItems) {
        NppMenuItem* items = nullptr;
        int count = 0;
        if (!guarded(plugin, "npp_plugin_menu_items", [&] { count = menuItems(&items); }))
            return false;
        if (count > 0 && items) {
            plugin.items = items;
            plugin.itemCount = count;
            ActionRegistry* r = m_window->actions();
            for (int i = 0; i < count; ++i) {
                NppMenuItem& it = items[i];
                const bool separator = !it.name || !*it.name || !it.callback;
                if (separator || m_nextCommandId > kLastCommandId) {
                    it.cmdId = 0;
                    plugin.cmdIds.push_back(0);
                    continue;
                }
                const int id = m_nextCommandId++;
                it.cmdId = id;
                plugin.cmdIds.push_back(id);
                QAction* a = r->action(id);
                a->setText(QString::fromUtf8(it.name));
                Plugin* pp = &plugin;
                ActionRegistry::Handler h;
                h.run = [this, id] { runPluginCommand(id); };
                h.enabled = [pp] { return pp->enabled; };
                r->setHandler(id, h);
                if (it.checked)
                    setMenuItemCheck(id, true);
            }
        }
    }
    return true;
}

bool PluginHost::guarded(Plugin& p, const char* what, const std::function<void()>& call)
{
    try {
        call();
        return true;
    } catch (const std::exception& e) {
        disable(p, tr("%1 threw an exception: %2").arg(QString::fromLatin1(what), QString::fromLocal8Bit(e.what())));
    } catch (...) {
        disable(p, tr("%1 threw an exception").arg(QString::fromLatin1(what)));
    }
    return false;
}

void PluginHost::disable(Plugin& p, const QString& reason)
{
    if (!p.enabled)
        return;
    p.enabled = false;
    addError(p.file, reason + QLatin1Char(' ') + tr("(plugin disabled)"));
    for (int id : p.cmdIds)
        if (id)
            m_window->actions()->action(id)->setEnabled(false);
}

PluginHost::Plugin* PluginHost::pluginOfCommand(int cmdId) const
{
    for (const auto& p : m_plugins)
        if (std::find(p->cmdIds.begin(), p->cmdIds.end(), cmdId) != p->cmdIds.end())
            return p.get();
    return nullptr;
}

PluginHost::Plugin* PluginHost::pluginByModule(const std::string& module) const
{
    const std::string n = normalizedModule(module);
    for (const auto& p : m_plugins)
        if (normalizedModule(p->module) == n)
            return p.get();
    return nullptr;
}

bool PluginHost::runPluginCommand(int cmdId)
{
    Plugin* p = pluginOfCommand(cmdId);
    if (!p || !p->enabled)
        return false;
    const auto idx = static_cast<size_t>(std::find(p->cmdIds.begin(), p->cmdIds.end(), cmdId) - p->cmdIds.begin());
    void (*cb)(void) = p->items[idx].callback;
    return cb && guarded(*p, "menu command", [cb] { cb(); });
}

void PluginHost::setMenuItemCheck(int cmdId, bool checked)
{
    Plugin* p = pluginOfCommand(cmdId);
    if (!p)
        return;
    QAction* a = m_window->actions()->action(cmdId);
    if (!a->isCheckable()) {
        a->setCheckable(true);
        p->checkable.insert(cmdId);
    }
    const QSignalBlocker block(a);
    a->setChecked(checked);
}

void PluginHost::buildMenu(QMenu* pluginsMenu, QAction* before)
{
    if (!pluginsMenu)
        return;
    bool any = false;
    for (const auto& p : m_plugins) {
        if (p->itemCount == 0)
            continue;
        auto* sub = new QMenu(p->name, pluginsMenu);
        sub->setObjectName(QStringLiteral("pluginMenu_") + QString::fromStdString(normalizedModule(p->module)));
        for (int i = 0; i < p->itemCount; ++i) {
            const int id = p->cmdIds[static_cast<size_t>(i)];
            if (id)
                sub->addAction(m_window->actions()->action(id));
            else
                sub->addSeparator();
        }
        pluginsMenu->insertMenu(before, sub);
        any = true;
    }
    if (any)
        pluginsMenu->insertSeparator(before);
}

void PluginHost::applyShortcuts(const ShortcutsFile& shortcuts)
{
    for (const auto& p : m_plugins) {
        for (int i = 0; i < p->itemCount; ++i) {
            const int id = p->cmdIds[static_cast<size_t>(i)];
            if (!id)
                continue;
            const NppShortcut& s = p->items[i].shortcut;
            KeyCombo k{s.ctrl != 0, s.alt != 0, s.shift != 0, s.key};
            for (const PluginCommandShortcut& c : shortcuts.pluginCommands)
                if (c.internalId == i && normalizedModule(c.moduleName) == normalizedModule(p->module))
                    k = c.combo;
            QKeySequence seq;
            if (k.isEnabled())
                if (const auto qt = toQtKey(k))
                    seq = QKeySequence(*qt);
            m_window->actions()->action(id)->setShortcut(seq);
        }
    }
}

std::vector<PluginHost::Command> PluginHost::commands() const
{
    std::vector<Command> out;
    for (const auto& p : m_plugins)
        for (int i = 0; i < p->itemCount; ++i)
            if (const int id = p->cmdIds[static_cast<size_t>(i)]) {
                const NppShortcut& s = p->items[i].shortcut;
                out.push_back({p->module, i, id, p->name, QString::fromUtf8(p->items[i].name),
                               KeyCombo{s.ctrl != 0, s.alt != 0, s.shift != 0, s.key}});
            }
    return out;
}

void PluginHost::notify(uint32_t code, uintptr_t id)
{
    NppNotification n{sizeof(NppNotification), code, -1, id, nullptr};
    for (const auto& p : m_plugins)
        if (p->enabled && p->initialised && p->notify) {
            const npp_plugin_notify_fn f = p->notify;
            guarded(*p, "npp_plugin_notify", [f, &n] { f(&n); });
        }
}

void PluginHost::forwardScn(int view, const void* scn, uint32_t code)
{
    NppNotification n{sizeof(NppNotification), code, view, 0, scn};
    for (const auto& p : m_plugins)
        if (p->enabled && p->initialised && p->notify) {
            const npp_plugin_notify_fn f = p->notify;
            guarded(*p, "npp_plugin_notify", [f, &n] { f(&n); });
        }
}

void PluginHost::connectEditor(int view, ScintillaEditBase* editor)
{
    connect(editor, &ScintillaEditBase::notify, this, [this, view](Scintilla::NotificationData* scn) {
        if (!m_plugins.empty() && !m_shutDown)
            forwardScn(view, scn, static_cast<uint32_t>(scn->nmhdr.code));
    });
}

void PluginHost::shutdown()
{
    if (m_shutDown)
        return;
    notify(NPPN_SHUTDOWN);
    m_shutDown = true;
    for (const auto& p : m_plugins)
        if (p->initialised && p->shutdown) {
            const npp_plugin_shutdown_fn f = p->shutdown;
            guarded(*p, "npp_plugin_shutdown", [f] { f(); });
        }
}

QStringList PluginHost::loadedPlugins() const
{
    QStringList out;
    for (const auto& p : m_plugins)
        if (p->enabled)
            out << p->name;
    return out;
}

bool PluginHost::isEnabled(const QString& name) const
{
    for (const auto& p : m_plugins)
        if (p->name == name)
            return p->enabled;
    return false;
}

int PluginHost::registerPanel(Plugin* p, const char* panelId, const char* title, void* widget)
{
    if (!p || !panelId || !*panelId || !widget)
        return 0;
    const std::string id = dockId(p->module, panelId);
    DockManager* dm = m_window->dockManager();
    const auto ids = dm->panelIds();
    if (std::find(ids.begin(), ids.end(), id) != ids.end())
        return 0;
    PanelDesc d;
    d.id = id;
    d.title = title ? QString::fromUtf8(title) : QString::fromUtf8(panelId);
    d.defaultArea = DockArea::Right;
    d.visibleByDefault = false;
    auto* w = static_cast<QWidget*>(widget);
    w->setObjectName(QString::fromStdString(id));
    dm->addPanel(d, w);
    dm->hidePanel(id);
    p->panels.push_back(panelId);
    return 1;
}

void PluginHost::showPanel(Plugin* p, const char* panelId, bool show)
{
    if (!p || !panelId)
        return;
    const std::string id = dockId(p->module, panelId);
    if (show)
        m_window->dockManager()->showPanel(id);
    else
        m_window->dockManager()->hidePanel(id);
}

bool PluginHost::isPanelVisible(Plugin* p, const char* panelId) const
{
    return p && panelId && m_window->dockManager()->isPanelVisible(dockId(p->module, panelId));
}

bool PluginHost::addToolbarIcon(int cmdId, const QString& icon, const QString& darkIcon)
{
    if (!pluginOfCommand(cmdId) || QImage(icon).isNull())
        return false;
    for (ToolbarIcon& t : m_toolbarIcons)
        if (t.cmdId == cmdId) {
            t = {cmdId, icon, darkIcon};
            m_window->buildToolbar();
            return true;
        }
    m_toolbarIcons.push_back({cmdId, icon, darkIcon});
    m_window->buildToolbar();
    return true;
}

void PluginHost::addToolbarButtons(QToolBar* toolbar, bool dark, int size)
{
    if (m_toolbarIcons.empty())
        return;
    toolbar->addSeparator();
    for (const ToolbarIcon& t : m_toolbarIcons) {
        const QString file = dark && !t.dark.isEmpty() ? t.dark : t.light;
        QAction* a = m_window->actions()->action(t.cmdId);
        a->setIcon(QIcon(QPixmap::fromImage(QImage(file).scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
        a->setIconVisibleInMenu(false);
        toolbar->addAction(a);
    }
}

} // namespace npp::ui
