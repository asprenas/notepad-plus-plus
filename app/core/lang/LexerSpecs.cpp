#include "LexerSpecs.h"

#include <SciLexer.h>

#include <array>
#include <memory>
#include <sstream>

namespace npp::core {

namespace {

using Props = std::vector<std::pair<std::string, std::string>>;

// upstream setLexer() always ends with these
const Props kGenericFold = {{"fold", "1"}, {"fold.compact", "0"}, {"fold.comment", "1"}};
// upstream C-family lexers
const Props kCppFold = {{"fold", "1"},
                        {"fold.compact", "0"},
                        {"fold.comment", "1"},
                        {"fold.cpp.comment.explicit", "0"},
                        {"fold.preprocessor", "1"},
                        {"lexer.cpp.track.preprocessor", "0"}};

constexpr int INSTR = 0, INSTR2 = 1, TYPE = 2, TYPE2 = 3, TYPE3 = 4, TYPE4 = 5, TYPE5 = 6, TYPE6 = 7, TYPE7 = 8;
constexpr int SUBSTYLE1 = 9, SUBSTYLE5 = 13;

Props parseProps(const char* s)
{
    Props out;
    std::istringstream in(s);
    for (std::string kv; std::getline(in, kv, ';');)
        if (const size_t eq = kv.find('='); eq != std::string::npos)
            out.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    return out;
}

void append(Props& dst, const Props& src) { dst.insert(dst.end(), src.begin(), src.end()); }

KeywordAssign kw(int set, LangType lang, int cls) { return {set, lang, cls, lang, false}; }
KeywordAssign doxygen() { return {2, L_CPP, TYPE2, L_CPP, true}; }

void cppFamily(LexerSpec& s, LangType lang)
{
    s.lexer = langInfo(L_CPP).lexer;
    s.stylers = {lang};
    s.keywords.clear();
    if (lang != L_RC)
        s.keywords.push_back(doxygen());
    s.keywords.push_back(kw(0, lang, INSTR));
    s.keywords.push_back(kw(1, lang, TYPE));
    s.keywords.push_back(kw(3, lang, INSTR2));
    s.subStyles = {{SCE_C_IDENTIFIER, 8, SUBSTYLE1, lang}};
    s.properties.clear();
    if (lang == L_GOLANG)
        s.properties.emplace_back("lexer.cpp.backquoted.strings", "1");
    append(s.properties, kCppFold);
}

std::unique_ptr<std::array<LexerSpec, kLangCount>> buildTable()
{
    auto t = std::make_unique<std::array<LexerSpec, kLangCount>>();
    for (int i = 0; i < kLangCount; ++i) {
        LexerSpec& s = (*t)[static_cast<size_t>(i)];
        s.lang = static_cast<LangType>(i);
        s.lexer = langInfo(s.lang).lexer;
        s.stylers = {s.lang};
    }
    auto at = [&t](LangType l) -> LexerSpec& { return (*t)[static_cast<size_t>(l)]; };

    // --- generated: upstream inline setXxxLexer() { setLexer(L_X, LIST_..., [subStyleBase, n]); props }
    auto generic = [&at](LangType l, int mask, int subBase, int nSub, const char* props) {
        LexerSpec& s = at(l);
        for (int c = 0; c <= TYPE7; ++c)
            if (mask & (1 << c))
                s.keywords.push_back(kw(c, l, c));
        if (subBase >= 0)
            s.subStyles.push_back({subBase, nSub, SUBSTYLE1, l});
        s.properties = kGenericFold;
        append(s.properties, parseProps(props));
    };
#define NPP_LEXER_GENERIC(l, mask, subBase, nSub, props) generic(l, mask, subBase, nSub, props);
#define NPP_LEXER_SPECIAL(l, func)
#include "core/lang/LexerSpecs.inc"
#undef NPP_LEXER_GENERIC
#undef NPP_LEXER_SPECIAL

    // --- hand-written ports of the special upstream functions ---------------------------
    for (LangType l : {L_C, L_CPP, L_JAVA, L_RC, L_CS, L_FLASH, L_SWIFT, L_GOLANG})
        cppFamily(at(l), l);

    { // setObjCLexer
        LexerSpec& s = at(L_OBJC);
        s.keywords = {kw(0, L_OBJC, INSTR), kw(1, L_OBJC, TYPE), doxygen(), kw(3, L_OBJC, INSTR2), kw(4, L_OBJC, TYPE2)};
        s.properties = {{"fold", "1"}, {"fold.compact", "0"}, {"fold.comment", "1"},
                        {"fold.cpp.comment.explicit", "0"}, {"fold.preprocessor", "1"}};
    }
    for (LangType l : {L_JAVASCRIPT, L_JS_EMBEDDED}) { // setJsLexer (stylers.xml has "javascript.js")
        LexerSpec& s = at(l);
        s.lexer = langInfo(L_JAVASCRIPT).lexer;
        s.stylers = {L_JAVASCRIPT};
        s.keywords = {doxygen(), kw(0, L_JAVASCRIPT, INSTR), kw(1, L_JAVASCRIPT, TYPE), kw(3, L_JAVASCRIPT, INSTR2)};
        s.subStyles = {{SCE_C_IDENTIFIER, 8, SUBSTYLE1, L_JAVASCRIPT}};
        s.properties = kCppFold;
        s.properties.emplace_back("lexer.cpp.backquoted.strings", "2"); // template literals
    }
    { // setTypeScriptLexer
        LexerSpec& s = at(L_TYPESCRIPT);
        s.keywords = {doxygen(), kw(0, L_TYPESCRIPT, INSTR), kw(1, L_TYPESCRIPT, TYPE)};
        s.subStyles = {{SCE_C_IDENTIFIER, 8, SUBSTYLE1, L_TYPESCRIPT}};
        s.properties = kCppFold;
        s.properties.emplace_back("lexer.cpp.backquoted.strings", "1");
    }
    { // setTclLexer
        LexerSpec& s = at(L_TCL);
        s.keywords = {kw(0, L_TCL, INSTR), kw(1, L_TCL, TYPE), kw(2, L_TCL, INSTR2), kw(3, L_TCL, TYPE2),
                      kw(4, L_TCL, TYPE3), kw(5, L_TCL, TYPE4), kw(6, L_TCL, TYPE5), kw(7, L_TCL, TYPE6),
                      kw(8, L_TCL, TYPE7)};
        s.properties.clear();
    }
    { // setXmlLexer(L_XML)
        LexerSpec& s = at(L_XML);
        s.keywords = {kw(5, L_XML, INSTR)};
        s.subStyles = {{SCE_H_ATTRIBUTE, 8, SUBSTYLE1, L_XML}};
        s.properties = {{"lexer.xml.allow.scripts", "0"}, {"fold", "1"}, {"fold.compact", "0"},
                        {"fold.html", "1"}, {"fold.hypertext.comment", "1"}};
    }
    for (LangType l : {L_HTML, L_PHP, L_ASP, L_JSP}) { // setXmlLexer(html family): HTML + embedded JS/PHP/ASP
        LexerSpec& s = at(l);
        s.lexer = langInfo(L_HTML).lexer;
        s.stylers = {L_HTML, L_JS_EMBEDDED, L_PHP, L_ASP};
        s.keywords = {kw(0, L_HTML, INSTR), kw(5, L_HTML, INSTR2),           // setHTMLLexer
                      kw(1, L_JS_EMBEDDED, INSTR),                           // setEmbeddedJSLexer
                      kw(4, L_PHP, INSTR),                                   // setEmbeddedPhpLexer
                      {2, L_VB, INSTR, L_ASP, false}};                       // setEmbeddedAspLexer (VB words)
        s.subStyles = {{SCE_H_TAG, 4, SUBSTYLE1, L_HTML},
                       {SCE_H_ATTRIBUTE, 4, SUBSTYLE5, L_HTML},
                       {SCE_HJ_WORD, 8, SUBSTYLE1, L_JS_EMBEDDED},
                       {SCE_HPHP_WORD, 8, SUBSTYLE1, L_PHP},
                       {SCE_HB_WORD, 8, SUBSTYLE1, L_ASP}};
        s.properties = {{"asp.default.language", "2"}, {"fold", "1"}, {"fold.compact", "0"},
                        {"fold.html", "1"}, {"fold.hypertext.comment", "1"}};
    }
    for (LangType l : {L_JSON, L_JSON5}) { // setJsonLexer
        LexerSpec& s = at(l);
        s.stylers = {L_JSON};
        s.keywords = {kw(0, L_JSON, INSTR), kw(1, L_JSON, INSTR2)};
        s.properties = {{"fold", "1"}, {"fold.compact", "0"}, {"lexer.json.escape.sequence", "1"}};
        if (l == L_JSON5)
            s.properties.emplace_back("lexer.json.allow.comments", "1");
    }
    for (LangType l : {L_PROPS, L_INI}) { // setPropsLexer
        LexerSpec& s = at(l);
        s.properties = kGenericFold;
        s.eolFilledStyles = {SCE_PROPS_SECTION};
    }
    { // setBashLexer
        LexerSpec& s = at(L_BASH);
        s.keywords = {kw(0, L_BASH, INSTR)};
        s.subStyles = {{SCE_SH_IDENTIFIER, 4, SUBSTYLE1, L_BASH}, {SCE_SH_SCALAR, 4, SUBSTYLE5, L_BASH}};
        s.properties = kGenericFold;
    }
    // generic + one extra statement
    auto genericKw = [&at](LangType l, int mask) {
        LexerSpec& s = at(l);
        for (int c = 0; c <= TYPE7; ++c)
            if (mask & (1 << c))
                s.keywords.push_back(kw(c, l, c));
        s.properties = kGenericFold;
        return std::ref(s);
    };
    const std::string wcAlnum = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    genericKw(L_AVS, 0x3F).get().wordChars = wcAlnum + "_#";
    genericKw(L_FORTH, 0x3F).get().wordChars = wcAlnum + "%-";
    genericKw(L_NNCRONTAB, 0x07).get().wordChars = wcAlnum + "%-";
    genericKw(L_OSCRIPT, 0x3F).get().wordChars = wcAlnum + "_$";
    genericKw(L_REBOL, 0x7F).get().wordChars = wcAlnum + "?!.'+-*&|=_~";
    genericKw(L_RUST, 0x7F).get().wordChars = wcAlnum + "_#";
    {
        LexerSpec& s = genericKw(L_BAANC, 0x1FF);
        append(s.properties, {{"lexer.baan.styling.within.preprocessor", "1"}, {"fold.preprocessor", "1"},
                              {"fold.baan.syntax.based", "1"}, {"fold.baan.keywords.based", "1"},
                              {"fold.baan.sections", "1"}, {"fold.baan.inner.level", "1"}});
        s.wordChars = wcAlnum + "_$:";
        s.eolFilledStyles = {SCE_BAAN_STRINGEOL};
    }
    genericKw(L_CSOUND, 0x07).get().eolFilledStyles = {SCE_CSOUND_STRINGEOL};
    genericKw(L_RUBY, 0x01).get().eolFilledStyles = {SCE_RB_POD};
    genericKw(L_SQL, 0x13).get().sqlBackslashOption = true;
    genericKw(L_TEX, 0x00).get().clearKeywordSets0to3 = true;
    {
        LexerSpec& s = genericKw(L_ERRORLIST, 0);
        s.escapeVisibilityStyles = {SCE_ERR_ESCSEQ, SCE_ERR_ESCSEQ_UNKNOWN};
        append(s.properties, {{"lexer.errorlist.value.separate", "0"}, {"lexer.errorlist.escape.sequences", "1"}});
    }
    {
        LexerSpec& s = genericKw(L_ESCSEQ, 0);
        s.escapeVisibilityStyles = {SCE_ESCSEQ_IDENTIFIER, SCE_ESCSEQ_UNKNOWN};
        append(s.properties, {{"lexer.escseq.colour.text", "1"}});
    }
    at(L_ASCII).asciiArtFont = true;
    at(L_USER).isUserDefined = true;
    at(L_TEXT).stylers.clear();
    return t;
}

} // namespace

const LexerSpec& lexerSpec(LangType lang)
{
    static const auto table = buildTable();
    const int i = (lang >= 0 && lang < kLangCount) ? lang : L_TEXT;
    return (*table)[static_cast<size_t>(i)];
}

} // namespace npp::core
