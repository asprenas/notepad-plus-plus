// Notepad++ language identifiers (upstream enum LangType; values are part of session.xml and the
// plugin API) and their names / Lexilla lexers (upstream _langNameInfoArray).
#pragma once

#include <optional>
#include <string_view>

namespace npp::core {

enum LangType : int {
#define NPP_LANG(e, name, shortName, longName, lexer) e,
#include "core/lang/LangTypes.inc"
#undef NPP_LANG
};

inline constexpr int kLangCount = L_EXTERNAL + 1;

struct LangInfo {
    LangType         type;
    std::string_view name;      // langs.xml / stylers.xml name, e.g. "cpp"
    std::string_view shortName; // menu name, e.g. "C++"
    std::string_view longName;  // status bar, e.g. "C++ source file"
    const char*      lexer;     // Lexilla lexer name ("null" = plain text)
};

const LangInfo& langInfo(LangType type);
// By langs.xml name (ASCII case-insensitive), e.g. "cpp" -> L_CPP.
std::optional<LangType> langTypeFromName(std::string_view name);
// By menu / session name, e.g. "C++" or "None (Normal Text)".
std::optional<LangType> langTypeFromShortName(std::string_view shortName);

// Keyword class names of langs.xml / stylers.xml (upstream LangIdxStyle):
// instre1=0 instre2=1 type1=2 .. type7=8 substyle1=9 .. substyle8=16, or a digit "0".."8".
inline constexpr int kKeywordClassCount = 17;
inline constexpr int kFirstSubstyleClass = 9;
int keywordClassFromName(std::string_view name); // -1 if unknown

// First-line based detection (shebang, <?xml, <html ...) of upstream
// FileManager::detectLanguageFromTextBeginning. L_TEXT when unknown.
LangType detectLanguageFromTextBeginning(std::string_view data);

} // namespace npp::core
