#include "AppPaths.h"

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <QtGlobal>

#include <cstdlib>

namespace npp::core {

namespace {

std::string env(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

void ensureDir(const std::string& dir, bool create)
{
    if (create && !dir.empty()) {
        std::error_code ec;
        fs::create_directories(pathFromUtf8(dir), ec);
    }
}

} // namespace

std::string AppPaths::platformUserDir()
{
#if defined(Q_OS_WIN)
    return pathAppend(env("APPDATA"), "Notepad++Qt");
#elif defined(Q_OS_MACOS)
    return pathAppend(env("HOME"), "Library/Application Support/Notepad++Qt");
#else
    std::string base = env("XDG_CONFIG_HOME");
    if (base.empty())
        base = pathAppend(env("HOME"), ".config");
    return pathAppend(base, "notepad++qt");
#endif
}

AppPaths AppPaths::resolve(const Options& o)
{
    AppPaths p;
    p.m_appDir = o.appDir;

    // Portable mode. Upstream refuses it when installed under "Program Files" (not writable);
    // we refuse it when the application directory is not writable.
    const std::string localConf = pathAppend(o.appDir, "doLocalConf.xml");
    p.m_portable = !o.appDir.empty() && pathExists(localConf, PathKind::File);
    if (p.m_portable) {
        std::error_code ec;
        const auto perms = fs::status(pathFromUtf8(o.appDir), ec).permissions();
        if (ec || (perms & fs::perms::owner_write) == fs::perms::none)
            p.m_portable = false;
    }

    if (p.m_portable) {
        p.m_userDir = o.appDir;
        p.m_pluginConfigDir = pathAppend(pathAppend(o.appDir, "plugins"), "Config");
    } else {
        p.m_userDir = o.userDirOverride.empty() ? platformUserDir() : o.userDirOverride;
        ensureDir(p.m_userDir, o.createDirectories);
        p.m_pluginConfigDir = pathAppend(pathAppend(p.m_userDir, "plugins"), "Config");
        ensureDir(p.m_pluginConfigDir, o.createDirectories);
    }
    p.m_sessionDir = p.m_userDir;

    // Cloud: <user dir>/cloud/choice contains the cloud settings directory (UTF-8).
    const std::string choice = pathAppend(pathAppend(p.m_userDir, "cloud"), "choice");
    if (pathExists(choice, PathKind::File)) {
        const std::string cloud = trimmed(readFileContent(choice).value_or(""));
        if (!cloud.empty() && pathExists(cloud, PathKind::Directory)) {
            p.m_cloudDir = cloud;
            p.m_userDir = cloud;
        }
    }

    // -settingsDir= wins over everything (and moves the session too), if valid.
    if (!o.settingsDirArg.empty() && pathExists(o.settingsDirArg, PathKind::Directory)) {
        p.m_userDir = o.settingsDirArg;
        p.m_sessionDir = o.settingsDirArg;
    }
    return p;
}

std::string AppPaths::configXml() const { return pathAppend(m_userDir, "config.xml"); }
std::string AppPaths::langsXml() const { return pathAppend(m_userDir, "langs.xml"); }
std::string AppPaths::stylersXml() const { return pathAppend(m_userDir, "stylers.xml"); }
std::string AppPaths::shortcutsXml() const { return pathAppend(m_userDir, "shortcuts.xml"); }
std::string AppPaths::contextMenuXml() const { return pathAppend(m_userDir, "contextMenu.xml"); }
std::string AppPaths::sessionXml() const { return pathAppend(m_sessionDir, "session.xml"); }
std::string AppPaths::userDefineLangXml() const { return pathAppend(m_userDir, "userDefineLang.xml"); }
std::string AppPaths::userDefineLangsDir() const { return pathAppend(m_userDir, "userDefineLangs"); }
std::string AppPaths::themesDir() const { return pathAppend(m_userDir, "themes"); }
std::string AppPaths::appThemesDir() const { return pathAppend(m_appDir, "themes"); }
std::string AppPaths::backupDir() const { return pathAppend(m_sessionDir, "backup"); }
std::string AppPaths::nativeLangXml() const { return pathAppend(m_userDir, "nativeLang.xml"); }
std::string AppPaths::localizationDir() const { return pathAppend(m_appDir, "localization"); }

} // namespace npp::core
