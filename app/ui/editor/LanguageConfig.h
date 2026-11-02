// Phase-0 language/style loader.
//
// Reads the upstream langs.model.xml / stylers.model.xml and applies lexer,
// keywords and styles to a Scintilla editor. This is a spike: it will be
// replaced by the npp-core configuration model (Parameters port) in phase 1.
// Strings are UTF-8 std::string per the project string policy.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

class ScintillaEditBase;

namespace npp::ui {

struct StyleDef {
    int                        styleId = 0;
    std::optional<int>         fore;      // Scintilla colour (0xBBGGRR)
    std::optional<int>         back;
    std::string                fontName;
    std::optional<int>         fontStyle; // bit 1 bold, 2 italic, 4 underline
    std::optional<int>         fontSize;
};

struct LanguageDef {
    std::string                name;      // Notepad++ language name, e.g. "cpp"
    std::vector<std::string>   extensions;
    std::map<int, std::string> keywords;  // Scintilla keyword set index -> words
    std::string                commentLine;
};

class LanguageConfig {
public:
    // dataDir must contain langs.model.xml and stylers.model.xml.
    bool load(const std::string& dataDir, std::string* error = nullptr);

    // Language name for a file path (by extension). "normal" when unknown.
    std::string languageForFile(const std::string& utf8Path) const;

    std::vector<std::string> languageNames() const;

    // Applies global styles, lexer, keywords and lexer styles.
    void apply(ScintillaEditBase* editor, const std::string& language) const;

    // Lexilla lexer name for a Notepad++ language name ("" = no lexer).
    static std::string lexerNameFor(const std::string& language);

private:
    void applyStyle(ScintillaEditBase* editor, const StyleDef& style) const;

    std::map<std::string, LanguageDef>           m_languages;
    std::map<std::string, std::string>           m_extToLanguage;
    std::vector<StyleDef>                        m_globalStyles;
    std::map<std::string, std::vector<StyleDef>> m_lexerStyles;
};

} // namespace npp::ui
