// stylers.xml / themes/*.xml model (upstream Style, LexerStyler, StyleArray, feedStylerArray).
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

struct Style {
    int         styleId = -1;
    std::string name;            // e.g. "INSTRUCTION WORD"
    int         fg = -1;         // 0xRRGGBB, -1 = not set
    int         bg = -1;
    std::string fontName;        // empty = inherit
    int         fontStyle = -1;  // bit 1 bold, 2 italic, 4 underline; -1 = inherit
    int         fontSize = -1;   // -1 = inherit
    int         keywordClass = -1; // keyword class index (see LangType.h), -1 none
    std::string keywords;        // user added keywords (element text)
    int         colorStyle = -1; // upstream COLORSTYLE_* for some global styles
    int         nesting = 0;     // UDL only: nesting mask (SCE_USER_MASK_NESTING_*)

    static int scintillaColour(int rgb) noexcept // 0xRRGGBB -> 0xBBGGRR
    {
        return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    }
};

struct LexerStyler {
    std::string        name;  // language name, e.g. "cpp"
    std::string        desc;
    std::string        ext;   // user extensions ("ext" attribute)
    std::vector<Style> styles;

    const Style* findById(int id) const;
};

class StyleRegistry {
public:
    StyleRegistry();
    ~StyleRegistry();
    StyleRegistry(StyleRegistry&&) noexcept;
    StyleRegistry& operator=(StyleRegistry&&) noexcept;

    bool load(std::string_view utf8Path);
    bool loadFromString(std::string_view xml);
    // Writes the model back into the loaded document (unknown parts preserved) and saves.
    bool save(std::string_view utf8Path);
    std::string saveToString();

    const std::vector<LexerStyler>& lexers() const noexcept { return m_lexers; }
    std::vector<LexerStyler>& lexers() noexcept { return m_lexers; }
    const LexerStyler* lexer(std::string_view name) const;
    LexerStyler* lexer(std::string_view name);

    const std::vector<Style>& globalStyles() const noexcept { return m_global; }
    std::vector<Style>& globalStyles() noexcept { return m_global; }
    const Style* globalStyle(int styleId) const;
    const Style* globalStyleByName(std::string_view name) const;

    // Theme files available in a directory (name without extension -> path).
    static std::map<std::string, std::string> themesIn(std::string_view utf8Dir);

private:
    void parse();
    void writeBack();

    struct Impl;
    std::unique_ptr<Impl> d;
    std::vector<LexerStyler> m_lexers;
    std::vector<Style> m_global;
};

} // namespace npp::core
