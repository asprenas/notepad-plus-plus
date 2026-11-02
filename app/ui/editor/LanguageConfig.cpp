#include "LanguageConfig.h"

#include <ILexer.h>   // must precede Lexilla.h
#include <Lexilla.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QFontDatabase>

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace npp::ui {

namespace {

std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string utf8FromPath(const std::filesystem::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Same mapping as upstream Parameters.cpp getKwClassFromName (LANG_INDEX_*).
int keywordIndexFromClass(const char* name)
{
    static const char* const kNames[] = {"instre1", "instre2", "type1", "type2", "type3",
                                         "type4",   "type5",   "type6", "type7"};
    for (int i = 0; i < 9; ++i)
        if (std::strcmp(name, kNames[i]) == 0)
            return i;
    if (name[0] >= '0' && name[0] <= '8' && name[1] == '\0')
        return name[0] - '0';
    return -1; // substyleN etc. not handled in phase 0
}

// "RRGGBB" -> Scintilla 0xBBGGRR
std::optional<int> parseColour(const char* hex)
{
    if (!hex || std::strlen(hex) != 6)
        return std::nullopt;
    char* end = nullptr;
    const long rgb = std::strtol(hex, &end, 16);
    if (*end != '\0')
        return std::nullopt;
    const int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return r | (g << 8) | (b << 16);
}

std::optional<int> parseInt(const pugi::xml_attribute& a)
{
    if (!a || !*a.value())
        return std::nullopt;
    return a.as_int();
}

StyleDef parseStyle(const pugi::xml_node& n)
{
    StyleDef s;
    s.styleId   = n.attribute("styleID").as_int();
    s.fore      = parseColour(n.attribute("fgColor").value());
    s.back      = parseColour(n.attribute("bgColor").value());
    s.fontName  = n.attribute("fontName").value();
    s.fontStyle = parseInt(n.attribute("fontStyle"));
    s.fontSize  = parseInt(n.attribute("fontSize"));
    return s;
}

// Windows default fonts in the model files are not available everywhere.
std::string platformFontFor(const std::string& fontName)
{
#if defined(Q_OS_WIN)
    return fontName;
#else
    if (fontName == "Courier New" || fontName == "Consolas" || fontName == "Lucida Console")
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family().toStdString();
    return fontName;
#endif
}

} // namespace

std::string LanguageConfig::lexerNameFor(const std::string& language)
{
    // Languages whose Lexilla lexer name differs from the Notepad++ language name.
    // Extracted from upstream ScintillaEditView::_langNameInfoArray.
    static const std::map<std::string, std::string> kMap = {
        {"normal", ""},          {"nfo", ""},             {"udf", "user"},
        {"php", "phpscript"},    {"c", "cpp"},            {"cs", "cpp"},
        {"java", "cpp"},         {"rc", "cpp"},           {"javascript", "cpp"},
        {"javascript.js", "cpp"},{"actionscript", "cpp"}, {"swift", "cpp"},
        {"typescript", "cpp"},   {"go", "cpp"},           {"html", "hypertext"},
        {"asp", "hypertext"},    {"jsp", "hypertext"},    {"ini", "props"},
        {"scheme", "lisp"},      {"postscript", "ps"},    {"autoit", "au3"},
        {"cobol", "COBOL"},      {"fortran77", "f77"},    {"baanc", "baan"},
        {"nim", "nimrod"},       {"json5", "json"},
    };
    auto it = kMap.find(language);
    return it != kMap.end() ? it->second : language;
}

bool LanguageConfig::load(const std::string& dataDir, std::string* error)
{
    const std::filesystem::path dir = pathFromUtf8(dataDir);

    pugi::xml_document langs;
    if (auto r = langs.load_file((dir / "langs.model.xml").c_str()); !r) {
        if (error) *error = "langs.model.xml: " + std::string(r.description());
        return false;
    }
    for (auto lang : langs.child("NotepadPlus").child("Languages").children("Language")) {
        LanguageDef def;
        def.name        = lang.attribute("name").value();
        def.commentLine = lang.attribute("commentLine").value();
        std::istringstream exts(lang.attribute("ext").value());
        for (std::string ext; exts >> ext;) {
            ext = toLower(ext);
            def.extensions.push_back(ext);
            m_extToLanguage.emplace(ext, def.name); // first definition wins
        }
        for (auto kw : lang.children("Keywords")) {
            const int idx = keywordIndexFromClass(kw.attribute("name").value());
            if (idx >= 0)
                def.keywords[idx] = kw.text().get();
        }
        m_languages[def.name] = std::move(def);
    }

    pugi::xml_document stylers;
    if (auto r = stylers.load_file((dir / "stylers.model.xml").c_str()); !r) {
        if (error) *error = "stylers.model.xml: " + std::string(r.description());
        return false;
    }
    auto root = stylers.child("NotepadPlus");
    for (auto lexer : root.child("LexerStyles").children("LexerType")) {
        auto& styles = m_lexerStyles[lexer.attribute("name").value()];
        for (auto ws : lexer.children("WordsStyle"))
            styles.push_back(parseStyle(ws));
    }
    for (auto ws : root.child("GlobalStyles").children("WidgetStyle")) {
        const int id = ws.attribute("styleID").as_int();
        if (id >= STYLE_DEFAULT && id <= STYLE_LASTPREDEFINED) // Scintilla predefined styles only
            m_globalStyles.push_back(parseStyle(ws));
    }
    return true;
}

std::string LanguageConfig::languageForFile(const std::string& utf8Path) const
{
    const std::filesystem::path p = pathFromUtf8(utf8Path);
    std::string ext = utf8FromPath(p.extension());
    if (!ext.empty())
        ext = toLower(ext.substr(1)); // drop leading '.'
    auto it = m_extToLanguage.find(ext);
    if (it != m_extToLanguage.end())
        return it->second;
    // Files like "Makefile" / "CMakeLists.txt" are matched by full name in upstream too.
    it = m_extToLanguage.find(toLower(utf8FromPath(p.filename())));
    return it != m_extToLanguage.end() ? it->second : "normal";
}

std::vector<std::string> LanguageConfig::languageNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, def] : m_languages)
        names.push_back(name);
    return names;
}

void LanguageConfig::applyStyle(ScintillaEditBase* e, const StyleDef& s) const
{
    if (s.fore) e->send(SCI_STYLESETFORE, s.styleId, *s.fore);
    if (s.back) e->send(SCI_STYLESETBACK, s.styleId, *s.back);
    if (!s.fontName.empty()) {
        const std::string font = platformFontFor(s.fontName);
        e->send(SCI_STYLESETFONT, s.styleId, reinterpret_cast<sptr_t>(font.c_str()));
    }
    if (s.fontSize && *s.fontSize > 0)
        e->send(SCI_STYLESETSIZE, s.styleId, *s.fontSize);
    if (s.fontStyle) {
        e->send(SCI_STYLESETBOLD,      s.styleId, (*s.fontStyle & 1) != 0);
        e->send(SCI_STYLESETITALIC,    s.styleId, (*s.fontStyle & 2) != 0);
        e->send(SCI_STYLESETUNDERLINE, s.styleId, (*s.fontStyle & 4) != 0);
    }
}

void LanguageConfig::apply(ScintillaEditBase* e, const std::string& language) const
{
    // 1. Global default style, then propagate to all styles.
    e->send(SCI_STYLERESETDEFAULT);
    for (const auto& s : m_globalStyles)
        if (s.styleId == STYLE_DEFAULT)
            applyStyle(e, s);
    e->send(SCI_STYLECLEARALL);
    for (const auto& s : m_globalStyles)
        if (s.styleId != STYLE_DEFAULT)
            applyStyle(e, s);

    // 2. Lexer.
    const std::string lexerName = lexerNameFor(language);
    Scintilla::ILexer5* lexer = lexerName.empty() ? nullptr : CreateLexer(lexerName.c_str());
    e->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
    if (!lexer)
        return;

    // 3. Keywords. Keyword class -> Scintilla keyword set is lexer specific upstream
    //    (ScintillaEditView::setXxxLexer). Phase 0 handles the C-family; the rest uses
    //    the generic identity mapping of ScintillaEditView::setLexer().
    // TODO(phase 1): port the per-language setXxxLexer() mappings into npp-core.
    auto keywordSet = [&lexerName](int cls) -> int {
        if (lexerName == "cpp") {
            switch (cls) {
            case 0: return 0;  // instre1 -> primary keywords
            case 2: return 1;  // type1   -> secondary keywords (types)
            case 3: return 2;  // type2   -> doc comment keywords (doxygen)
            case 1: return 3;  // instre2 -> global classes
            default: return -1;
            }
        }
        return cls;
    };
    if (auto it = m_languages.find(language); it != m_languages.end())
        for (const auto& [cls, words] : it->second.keywords)
            if (const int set = keywordSet(cls); set >= 0)
                e->send(SCI_SETKEYWORDS, set, reinterpret_cast<sptr_t>(words.c_str()));

    // 4. Lexer styles.
    if (auto it = m_lexerStyles.find(language); it != m_lexerStyles.end())
        for (const auto& s : it->second)
            applyStyle(e, s);

    // 5. Folding.
    e->send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
    e->send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.compact"), reinterpret_cast<sptr_t>("0"));
    e->send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.html"), reinterpret_cast<sptr_t>("1"));
    e->send(SCI_COLOURISE, 0, -1);
}

} // namespace npp::ui
