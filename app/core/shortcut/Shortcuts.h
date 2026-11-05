// shortcuts.xml model (upstream feedShortcut / feedMacros / feedUserCmds / feedPluginCustomizedCmds /
// feedScintKeys and the insert* writers) plus Win32 virtual-key <-> Qt key conversion.
#pragma once

#include <QKeyCombination>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

// Upstream KeyCombo: modifiers + Win32 virtual-key code (VK_*), as stored in shortcuts.xml.
struct KeyCombo {
    bool ctrl = false, alt = false, shift = false;
    int  key = 0; // VK code, 0 = none

    bool isEnabled() const noexcept { return key != 0; }
    bool operator==(const KeyCombo&) const = default;
};

// VK code <-> Qt key. Numeric keypad VK codes map to the Qt key plus Qt::KeypadModifier.
std::optional<QKeyCombination> toQtKey(const KeyCombo& combo);
std::optional<KeyCombo> fromQtKey(QKeyCombination qt);
// Human readable, upstream style: "Ctrl+Shift+F" (non-localized key names).
std::string keyComboToString(const KeyCombo& combo);

struct CommandShortcut {       // <InternalCommands><Shortcut id Ctrl Alt Shift Key [nth]/>
    int id = 0;
    KeyCombo combo;
    int nth = 0;               // index for commands with several shortcuts
};

struct MacroStep {             // <Action type message wParam lParam sParam/>
    int type = 0;              // 0 use lParam, 1 use sParam, 2 menu command, 3 ...
    int message = 0;
    long long wParam = 0, lParam = 0;
    std::string sParam;
};

struct MacroShortcut {         // <Macros><Macro name Ctrl Alt Shift Key [FolderName]>
    std::string name, folder;
    KeyCombo combo;
    std::vector<MacroStep> steps;
};

struct UserCommand {           // <UserDefinedCommands><Command name ... [FolderName]>command</Command>
    std::string name, folder, command;
    KeyCombo combo;
};

struct PluginCommandShortcut { // <PluginCommands><PluginCommand moduleName internalID .../>
    std::string moduleName;
    int internalId = 0;
    KeyCombo combo;
};

struct ScintillaKeyShortcut {  // <ScintillaKeys><ScintKey ScintID menuCmdID ...><NextKey .../></ScintKey>
    int scintId = 0;
    int menuCmdId = 0;
    std::vector<KeyCombo> combos; // first = main key, rest = NextKey
};

struct ShortcutsFile {
    std::vector<CommandShortcut> internalCommands; // only the user-modified ones
    std::vector<MacroShortcut> macros;
    std::vector<UserCommand> userCommands;
    std::vector<PluginCommandShortcut> pluginCommands;
    std::vector<ScintillaKeyShortcut> scintillaKeys;

    bool load(std::string_view utf8Path);
    bool loadFromString(std::string_view xml);
    bool save(std::string_view utf8Path) const;
    std::string saveToString() const;
};

// Upstream getActions() normalisation of old macros: SCI_REPLACESEL steps that insert a lone
// EOL become SCI_NEWLINE (CR+LF / CR then LF collapse to one newline).
void normalizeMacroSteps(std::vector<MacroStep>& steps);

} // namespace npp::core
