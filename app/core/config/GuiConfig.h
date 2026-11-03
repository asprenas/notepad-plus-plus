// Typed model of <GUIConfigs> in config.xml (upstream NppGUI + ScintillaViewParams).
//
// Unlike upstream (which deletes and rebuilds <GUIConfigs> on save), saving updates the
// existing elements in place, so entries/attributes unknown to this version are preserved
// (round-trip rule of docs/phase-1-core-extraction.md 4.3).
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace pugi { class xml_node; }

namespace npp::core {

#if defined(_WIN32)
#define NPP_DEFAULT_EOL_FORMAT 0 /* EolType::windows */
#else
#define NPP_DEFAULT_EOL_FORMAT 2 /* EolType::unix: new documents use LF outside Windows */
#endif

// File change detection flags (upstream ChangeDetect)
enum ChangeDetect : int {
    cdDisabled   = 0x00,
    cdEnabledOld = 0x01,
    cdEnabledNew = 0x02,
    cdAutoUpdate = 0x04,
    cdGo2end     = 0x08,
};

struct GuiConfig {
#define NPP_BOOL_ATTR(m, node, attr, def) bool m = def;
#define NPP_SHOWHIDE_ATTR(m, node, attr, def) bool m = def;
#define NPP_INT_ATTR(m, node, attr, def) int m = def;
#define NPP_STR_ATTR(m, node, attr, def) std::string m = def;
#define NPP_BOOL_TEXT(m, node, def) bool m = def;
#define NPP_INT_TEXT(m, node, def) int m = def;
#define NPP_STR_TEXT(m, node, def) std::string m = def;
#include "core/config/GuiConfigFields.inc"
#undef NPP_BOOL_ATTR
#undef NPP_SHOWHIDE_ATTR
#undef NPP_INT_ATTR
#undef NPP_STR_ATTR
#undef NPP_BOOL_TEXT
#undef NPP_INT_TEXT
#undef NPP_STR_TEXT

    // --- entries with special encodings -------------------------------------------------
    bool statusBarShow = true;          // <GUIConfig name="StatusBar">show|hide</GUIConfig>
    bool menuBarShow = true;            // <GUIConfig name="MenuBar">show|hide</GUIConfig>
    bool splitterVertical = true;       // ScintillaViewsSplitter: vertical|horizontal
    bool userDefineDlgDocked = true;    // UserDefineDlg position="docked|undocked"
    bool userDefineDlgShow = false;     // UserDefineDlg text show|hide
    int  fileAutoDetection = cdEnabledNew; // <GUIConfig name="Auto-detection">yes|auto|Update2End|...</GUIConfig>
    std::array<int, 16> colorPickerCustomColors{}; // COLORREF values (0x00BBGGRR)
    std::vector<std::pair<char, char>> autoInsertUserPairs; // <auto-insert><UserDefinePair open close/>

    // Loads from the <GUIConfigs> element; missing entries keep the upstream defaults above.
    void load(const pugi::xml_node& guiConfigs);
    // Writes into <GUIConfigs> in place (creates missing <GUIConfig> elements at the end).
    void save(pugi::xml_node guiConfigs) const;

    // Helpers matching upstream string encodings
    static int fileAutoDetectionFromString(const std::string& s);
    // edgeMultiColumnPos ("80 120") as column numbers
    static std::vector<int> edgeColumns(const std::string& s);
    static std::string fileAutoDetectionToString(int flags);
};

} // namespace npp::core
