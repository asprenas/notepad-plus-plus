// Run > Run... and user commands (upstream RunDlg.cpp): $(VARIABLE) expansion and splitting of the
// command into program and arguments.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace npp::core {

struct RunVariables {
    std::string fullCurrentPath;   // FULL_CURRENT_PATH (CURRENT_DIRECTORY, FILE_NAME, NAME_PART, EXT_PART derive from it)
    std::string currentWord;       // CURRENT_WORD: selection, or the word at the caret
    std::string currentLineStr;    // CURRENT_LINESTR: text of the caret line (with its EOL, as upstream)
    std::string nppFullFilePath;   // NPP_FULL_FILE_PATH (NPP_DIRECTORY derives from it)
    intptr_t currentLine = 0;      // CURRENT_LINE (0-based, upstream NPPM_GETCURRENTLINE)
    intptr_t currentColumn = 0;    // CURRENT_COLUMN (0-based)
};

// $(FULL_CURRENT_PATH) ... replaced; unknown or unterminated $( kept as is (upstream expandNppEnvironmentStrs).
std::string expandRunVariables(std::string_view s, const RunVariables& v);

struct SplitCommand {
    std::string program; // without surrounding quotes
    std::string args;    // the rest, leading / trailing spaces removed
};
// upstream Command::extractArgs: the program ends at the first space outside quotes.
SplitCommand splitRunCommand(std::string_view cmd);

} // namespace npp::core
