// Per-language lexer setup as data (port of the 85 upstream ScintillaEditView::setXxxLexer()
// functions; docs/plan/phase-1-core-extraction.md 4.4).
#pragma once

#include "core/lang/LangType.h"

#include <string>
#include <utility>
#include <vector>

namespace npp::core {

// SCI_SETKEYWORDS(set, <styler user words of class> + " " + <langs.xml words of class>)
struct KeywordAssign {
    int      set;
    LangType wordsLang;     // langs.xml language providing the words
    int      keywordClass;  // keyword class (LangType.h) in langs.xml
    LangType userWordsLang; // stylers.xml lexer providing the user words of that class
    bool     rawWordsOnly = false; // doxygen lists: langs.xml words only, no user words
};

// SCI_ALLOCATESUBSTYLES(baseStyle, count); identifiers of substyle i = class firstClass + i
struct SubStyleAssign {
    int      baseStyle;
    int      count;
    int      firstClass;
    LangType lang;          // lang used for both langs.xml and user words
};

struct LexerSpec {
    LangType                lang = L_TEXT;
    const char*             lexer = "null";      // Lexilla lexer name
    std::vector<LangType>   stylers;             // stylers.xml lexers applied (upstream makeStyle) in order
    std::vector<KeywordAssign> keywords;
    std::vector<SubStyleAssign> subStyles;
    std::vector<std::pair<std::string, std::string>> properties;
    std::string             wordChars;            // SCI_SETWORDCHARS if not empty
    std::vector<int>        eolFilledStyles;      // SCI_STYLESETEOLFILLED(style, true)
    std::vector<int>        escapeVisibilityStyles; // SCI_STYLESETVISIBLE(style, showCcUniEol)
    bool                    sqlBackslashOption = false; // "sql.backslash.escapes" from GuiConfig
    bool                    clearKeywordSets0to3 = false; // TeX
    bool                    asciiArtFont = false;  // L_ASCII: "Lucida Console" 10 default font
    bool                    isUserDefined = false; // L_USER: UDL, see UserLanguage
};

// Specification for every LangType (generic ones generated from upstream, special ones hand-written).
const LexerSpec& lexerSpec(LangType lang);

} // namespace npp::core
