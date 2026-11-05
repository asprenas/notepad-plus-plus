// config.xml: GUI settings, recent files, find history (upstream NppParameters parts).
// The pugixml document is kept for the whole life of the object so that everything this
// version does not model is written back untouched.
#pragma once

#include "core/config/GuiConfig.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

// <History nbMaxFile="10" inSubMenu="no" customLength="-1"><File filename="..."/></History>
struct RecentFiles {
    static constexpr int kMaxFiles = 30;           // NB_MAX_LRF_FILE
    static constexpr int kShowFullPath = -1;       // RECENTFILES_SHOWFULLPATH
    int  maxFiles = 10;
    int  customLength = kShowFullPath;             // -1 full path, 0 file name only, >0 compact length
    bool inSubMenu = false;
    std::vector<std::string> files;                // most recent first

    void add(const std::string& path);             // moves to front, trims to maxFiles
    void remove(const std::string& path);
};

// <FindHistory ...><Path/Filter/Find/Replace name=".."/></FindHistory>
struct FindHistory {
    enum SearchMode { Normal = 0, Extended = 1, Regex = 2 };
    enum Transparency { None = 0, OnLosingFocus = 1, Persistent = 2 };

    int  maxPath = 10, maxFilter = 10, maxFind = 10, maxReplace = 10;
    bool matchWord = false, matchCase = false, wrap = true, directionDown = true;
    bool fifRecursive = true, fifInHiddenFolder = false;
    bool fifProjectPanel1 = false, fifProjectPanel2 = false, fifProjectPanel3 = false;
    bool fifFilterFollowsDoc = false;
    int  searchMode = Normal;
    int  transparencyMode = OnLosingFocus;
    int  transparency = 150;
    bool dotMatchesNewline = false, search2ButtonsMode = false, regexBackwardForPowerUser = false;
    bool bookmarkLine = false, purge = false;
    std::vector<std::string> paths, filters, finds, replaces; // most recent first

    // Adds to the front of a history list, removing duplicates, trimmed to max.
    static void push(std::vector<std::string>& list, const std::string& value, int max);
};

// <FileBrowser latestSelectedItem=".."><root foldername=".."><expanded path=".."/></root></FileBrowser>
// (Folder as Workspace, upstream writeFileBrowserSettings)
struct FileBrowserSettings {
    struct Root {
        std::string folder;
        std::vector<std::string> expanded;
        bool operator==(const Root&) const = default;
    };
    std::string latestSelectedItem;
    std::vector<Root> roots;
    bool operator==(const FileBrowserSettings&) const = default;
};

class ConfigFile {
public:
    ConfigFile();
    ~ConfigFile();
    ConfigFile(ConfigFile&&) noexcept;
    ConfigFile& operator=(ConfigFile&&) noexcept;

    // Missing file -> defaults (returns true, isNew() == true). Corrupt file -> false.
    bool load(std::string_view utf8Path);
    bool loadFromString(std::string_view xml);
    bool save(std::string_view utf8Path);
    std::string saveToString();
    bool isNew() const noexcept { return m_isNew; }

    GuiConfig   gui;
    RecentFiles history;
    FindHistory findHistory;
    // <ProjectPanels><ProjectPanel id="0..2" workSpaceFile=".."/></ProjectPanels>
    std::array<std::string, 3> projectPanelFiles;
    FileBrowserSettings fileBrowser;

private:
    void parse();
    void writeBack();
    std::array<std::string, 3> m_loadedProjectPanelFiles; // written back only when changed
    FileBrowserSettings m_loadedFileBrowser;

    struct Impl;
    std::unique_ptr<Impl> d;
    bool m_isNew = false;
};

} // namespace npp::core
