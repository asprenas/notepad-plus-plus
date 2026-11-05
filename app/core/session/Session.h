// session.xml model (upstream Session / sessionFileInfo, loadSession / writeSession).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

struct SessionMapPosition {
    long long firstVisibleDisplayLine = -1, firstVisibleDocLine = -1, lastVisibleDocLine = -1;
    long long nbLine = -1, higherPos = -1, width = -1, height = -1, wrapIndentMode = -1, kByteInDoc = -1;
    bool isWrap = false;
};

struct SessionFile {
    std::string filename;            // UTF-8 full path, or "new 1" style name for untitled
    std::string lang;                // language name as shown in the Language menu
    int  encoding = -1;              // code page, -1 = none
    long long firstVisibleLine = 0, xOffset = 0, scrollWidth = 1, startPos = 0, endPos = 0;
    int  selMode = 0;
    long long offset = 0, wrapCount = 1;
    bool userReadOnly = false;
    std::string backupFilePath;      // snapshot backup of a dirty / untitled document
    uint32_t originalFileLastModifTimestamp = 0;     // Win32 FILETIME low part
    uint32_t originalFileLastModifTimestampHigh = 0; // Win32 FILETIME high part
    int  tabColourId = -1;
    bool rtl = false;
    bool tabPinned = false;
    bool untitledTabRenamed = false;
    SessionMapPosition map;
    std::vector<size_t> marks;       // bookmarked lines
    std::vector<size_t> folds;       // folded header lines
    std::map<std::string, std::string> extraAttributes; // unknown attributes, preserved
};

struct SessionView {
    int activeIndex = 0;
    std::vector<SessionFile> files;
};

struct SessionFileBrowserRoot {
    std::string folder;
    std::vector<std::string> expanded;
};

struct Session {
    int activeView = 0; // 0 main, 1 sub
    SessionView mainView;
    SessionView subView;
    std::string fileBrowserSelectedItem;
    std::vector<SessionFileBrowserRoot> fileBrowserRoots;

    bool empty() const noexcept { return mainView.files.empty() && subView.files.empty(); }
};

// Returns nullopt if the file cannot be read or is not a session file.
std::optional<Session> loadSession(std::string_view utf8Path);
std::optional<Session> parseSession(std::string_view xml);
bool saveSession(const Session& session, std::string_view utf8Path);
std::string serializeSession(const Session& session);

} // namespace npp::core
