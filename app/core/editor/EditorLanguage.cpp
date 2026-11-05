#include "EditorLanguage.h"

#include "core/lang/LanguageRegistry.h"
#include "core/lang/LexerSpecs.h"
#include "core/lang/StyleRegistry.h"
#include "core/lang/UserLanguage.h"

#include <ILexer.h>
#include <Lexilla.h>
#include <Scintilla.h>

#include <array>
#include <map>

namespace npp::core {

namespace {

constexpr int kStyleDefault = 32; // STYLE_DEFAULT

std::string fontFor(const std::string& name, const LanguageApplyOptions& o)
{
    if (o.fontSubstitute) {
        std::string sub = o.fontSubstitute(name);
        if (!sub.empty())
            return sub;
    }
    return name;
}

} // namespace

void applyStyle(const IEditor& e, const Style& s, const Style* def, const LanguageApplyOptions& o)
{
    if (s.styleId < 0)
        return;
    const uintptr_t id = static_cast<uintptr_t>(s.styleId);
    const GlobalOverride& go = o.override;
    const bool useDef = def && s.styleId != kStyleDefault;

    int fg = s.fg, bg = s.bg;
    if (useDef && go.fg && def->fg >= 0) fg = def->fg;
    if (useDef && go.bg && def->bg >= 0) bg = def->bg;
    if (fg >= 0) e.send(SCI_STYLESETFORE, id, Style::scintillaColour(fg));
    if (bg >= 0) e.send(SCI_STYLESETBACK, id, Style::scintillaColour(bg));

    std::string font = s.fontName;
    if (useDef && go.font && !def->fontName.empty()) font = def->fontName;
    if (!font.empty())
        e.sendStr(SCI_STYLESETFONT, id, fontFor(font, o));

    int size = s.fontSize;
    if (useDef && go.fontSize && def->fontSize > 0) size = def->fontSize;
    if (size > 0)
        e.send(SCI_STYLESETSIZE, id, size);

    int fs = s.fontStyle;
    if (useDef && def->fontStyle >= 0) {
        if (fs < 0) fs = 0;
        if (go.bold) fs = (fs & ~1) | (def->fontStyle & 1);
        if (go.italic) fs = (fs & ~2) | (def->fontStyle & 2);
        if (go.underline) fs = (fs & ~4) | (def->fontStyle & 4);
    }
    if (fs >= 0) {
        e.send(SCI_STYLESETBOLD, id, (fs & 1) != 0);
        e.send(SCI_STYLESETITALIC, id, (fs & 2) != 0);
        e.send(SCI_STYLESETUNDERLINE, id, (fs & 4) != 0);
    }
}

void applyGlobalStyles(const IEditor& e, const StyleRegistry& styles, const LanguageApplyOptions& o)
{
    e.send(SCI_STYLERESETDEFAULT);
    const Style* def = styles.globalStyle(kStyleDefault);
    if (def)
        applyStyle(e, *def, nullptr, o);
    e.send(SCI_STYLECLEARALL);
    for (const Style& s : styles.globalStyles())
        if (s.styleId > kStyleDefault && s.styleId <= STYLE_LASTPREDEFINED)
            applyStyle(e, s, def, o);
}

void applyLanguage(const IEditor& e, LangType lang, const LanguageRegistry& langs, const StyleRegistry& styles,
                   const LanguageApplyOptions& o)
{
    const LexerSpec& spec = lexerSpec(lang);
    applyGlobalStyles(e, styles, o);
    const Style* def = styles.globalStyle(kStyleDefault);

    e.send(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(CreateLexer(spec.lexer)));

    if (spec.asciiArtFont) {
        Style nfo;
        nfo.styleId = kStyleDefault;
        nfo.fontName = "Lucida Console";
        nfo.fontSize = 10;
        if (const LexerStyler* ls = styles.lexer("nfo")) {
            for (const Style& s : ls->styles) {
                if (s.name == "DEFAULT") {
                    nfo.fg = s.fg;
                    nfo.bg = s.bg;
                    if (!s.fontName.empty()) nfo.fontName = s.fontName;
                    if (s.fontSize > 0) nfo.fontSize = s.fontSize;
                }
            }
        }
        applyStyle(e, nfo, nullptr, o);
        e.send(SCI_STYLECLEARALL);
    }

    // makeStyle: lexer styles + user keywords (WordsStyle text) per (lexer, keyword class)
    std::map<std::pair<int, int>, std::string> userWords;
    for (LangType sl : spec.stylers) {
        const LexerStyler* ls = styles.lexer(langInfo(sl).name);
        if (!ls)
            continue;
        for (const Style& s : ls->styles) {
            applyStyle(e, s, def, o);
            if (s.keywordClass >= 0 && !s.keywords.empty())
                userWords[{sl, s.keywordClass}] = s.keywords;
        }
    }
    auto words = [&](LangType wordsLang, int cls, LangType userLang) {
        std::string w;
        if (auto it = userWords.find({userLang, cls}); it != userWords.end())
            w = it->second;
        w += " ";
        w += langs.wordList(wordsLang, cls);
        return w;
    };

    if (spec.clearKeywordSets0to3)
        for (int i = 0; i < 4; ++i)
            e.sendStr(SCI_SETKEYWORDS, static_cast<uintptr_t>(i), "");
    for (const KeywordAssign& k : spec.keywords) {
        const std::string w = k.rawWordsOnly ? langs.wordList(k.wordsLang, k.keywordClass)
                                             : words(k.wordsLang, k.keywordClass, k.userWordsLang);
        e.sendStr(SCI_SETKEYWORDS, static_cast<uintptr_t>(k.set), w);
    }
    for (const SubStyleAssign& ss : spec.subStyles) {
        const int first = static_cast<int>(e.send(SCI_ALLOCATESUBSTYLES, static_cast<uintptr_t>(ss.baseStyle), ss.count) & 0xFF);
        for (int i = 0; i < ss.count; ++i)
            e.sendStr(SCI_SETIDENTIFIERS, static_cast<uintptr_t>(first + i), words(ss.lang, ss.firstClass + i, ss.lang));
    }

    for (const auto& [k, v] : spec.properties)
        e.setProperty(k, v);
    if (spec.sqlBackslashOption)
        e.setProperty("sql.backslash.escapes", o.sqlBackslashEscape ? "1" : "0");
    if (!spec.wordChars.empty())
        e.sendStr(SCI_SETWORDCHARS, 0, spec.wordChars);
    else
        e.send(SCI_SETCHARSDEFAULT);
    for (int st : spec.eolFilledStyles)
        e.send(SCI_STYLESETEOLFILLED, static_cast<uintptr_t>(st), 1);
    for (int st : spec.escapeVisibilityStyles)
        e.send(SCI_STYLESETVISIBLE, static_cast<uintptr_t>(st), o.showCcUniEol ? 1 : 0);

    e.send(SCI_COLOURISE, 0, -1);
}

void applyUserLanguage(const IEditor& e, const UserLanguage& udl, const StyleRegistry& styles,
                       const LanguageApplyOptions& o, uintptr_t instanceKey)
{
    applyGlobalStyles(e, styles, o);
    const Style* def = styles.globalStyle(kStyleDefault);
    e.send(SCI_SETILEXER, 0, reinterpret_cast<intptr_t>(CreateLexer("user")));

    const UserLexerConfig cfg = buildUserLexerConfig(udl);
    for (const auto& [k, v] : cfg.properties)
        e.setProperty(k, v);
    for (size_t i = 0; i < cfg.keywordSets.size(); ++i)
        e.sendStr(SCI_SETKEYWORDS, i, cfg.keywordSets[i]);
    // LexUser caches per UDL / buffer using these numeric keys (upstream: pointer values)
    e.setProperty("userDefine.udlName", std::to_string(reinterpret_cast<uintptr_t>(&udl)));
    e.setProperty("userDefine.currentBufferID", std::to_string(instanceKey));

    for (const Style& s : udl.styles)
        applyStyle(e, s, def, o);
    e.send(SCI_COLOURISE, 0, -1);
}

} // namespace npp::core
