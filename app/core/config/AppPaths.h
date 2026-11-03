// Settings / data directory resolution (upstream NppParameters::load path logic).
//
// Order: -settingsDir=<dir> command line > cloud/choice > portable (doLocalConf.xml next to the
// executable) > per-platform user directory:
//   Windows  %APPDATA%\Notepad++Qt
// (separate from the original Notepad++ folder so both can be installed side by side)
//   Linux    $XDG_CONFIG_HOME/notepad++qt  (default ~/.config/notepad++qt)
//   macOS    ~/Library/Application Support/Notepad++Qt
#pragma once

#include <string>
#include <string_view>

namespace npp::core {

class AppPaths {
public:
    struct Options {
        std::string appDir;          // directory of the executable (upstream _nppPath)
        std::string settingsDirArg;  // -settingsDir= value, may be empty
        std::string userDirOverride; // tests only: replaces the per-platform user directory
        bool createDirectories = true;
    };

    static AppPaths resolve(const Options& options);

    // Per-platform default user directory (without portable / cloud / -settingsDir handling).
    static std::string platformUserDir();

    const std::string& appDir() const noexcept { return m_appDir; }
    const std::string& userDir() const noexcept { return m_userDir; }       // settings files
    const std::string& sessionDir() const noexcept { return m_sessionDir; } // never on cloud
    const std::string& pluginConfigDir() const noexcept { return m_pluginConfigDir; }
    const std::string& cloudDir() const noexcept { return m_cloudDir; }     // empty if not cloud
    bool isPortable() const noexcept { return m_portable; }
    bool isCloud() const noexcept { return !m_cloudDir.empty(); }

    // Well known files
    std::string configXml() const;            // config.xml
    std::string langsXml() const;             // langs.xml
    std::string stylersXml() const;           // stylers.xml
    std::string shortcutsXml() const;         // shortcuts.xml
    std::string contextMenuXml() const;       // contextMenu.xml
    std::string sessionXml() const;           // session.xml (session dir)
    std::string userDefineLangXml() const;    // userDefineLang.xml
    std::string userDefineLangsDir() const;   // userDefineLangs/
    std::string themesDir() const;            // themes/ in the user dir
    std::string appThemesDir() const;         // themes/ next to the executable
    std::string backupDir() const;            // backup/ (session dir)
    std::string nativeLangXml() const;        // nativeLang.xml (selected translation copy)
    std::string localizationDir() const;      // localization/ next to the executable

private:
    std::string m_appDir, m_userDir, m_sessionDir, m_pluginConfigDir, m_cloudDir;
    bool m_portable = false;
};

} // namespace npp::core
