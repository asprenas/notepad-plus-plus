#include "UserLanguage.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace npp::core {

namespace {

enum KwList { // SCE_USER_KWLIST_*
    COMMENTS = 0, NUMBER_PREFIX1, NUMBER_PREFIX2, NUMBER_EXTRAS1, NUMBER_EXTRAS2, NUMBER_SUFFIX1, NUMBER_SUFFIX2,
    NUMBER_RANGE, OPERATORS1, OPERATORS2, FOLDERS_IN_CODE1_OPEN, FOLDERS_IN_CODE1_MIDDLE, FOLDERS_IN_CODE1_CLOSE,
    FOLDERS_IN_CODE2_OPEN, FOLDERS_IN_CODE2_MIDDLE, FOLDERS_IN_CODE2_CLOSE, FOLDERS_IN_COMMENT_OPEN,
    FOLDERS_IN_COMMENT_MIDDLE, FOLDERS_IN_COMMENT_CLOSE, KEYWORDS1, KEYWORDS2, KEYWORDS3, KEYWORDS4, KEYWORDS5,
    KEYWORDS6, KEYWORDS7, KEYWORDS8, DELIMITERS
};

const std::map<std::string, int, std::less<>>& keywordIdMap()
{
    static const std::map<std::string, int, std::less<>> m = {
        // UDL 1.0 names
        {"Operators", OPERATORS1}, {"Folder+", FOLDERS_IN_CODE1_OPEN}, {"Folder-", FOLDERS_IN_CODE1_CLOSE},
        {"Words1", KEYWORDS1}, {"Words2", KEYWORDS2}, {"Words3", KEYWORDS3}, {"Words4", KEYWORDS4},
        // UDL 2.x names
        {"Comments", COMMENTS}, {"Numbers, additional", NUMBER_RANGE}, {"Numbers, prefixes", NUMBER_PREFIX2},
        {"Numbers, extras with prefixes", NUMBER_EXTRAS2}, {"Numbers, suffixes", NUMBER_SUFFIX2},
        {"Operators1", OPERATORS1}, {"Operators2", OPERATORS2},
        {"Folders in code1, open", FOLDERS_IN_CODE1_OPEN}, {"Folders in code1, middle", FOLDERS_IN_CODE1_MIDDLE},
        {"Folders in code1, close", FOLDERS_IN_CODE1_CLOSE}, {"Folders in code2, open", FOLDERS_IN_CODE2_OPEN},
        {"Folders in code2, middle", FOLDERS_IN_CODE2_MIDDLE}, {"Folders in code2, close", FOLDERS_IN_CODE2_CLOSE},
        {"Folders in comment, open", FOLDERS_IN_COMMENT_OPEN}, {"Folders in comment, middle", FOLDERS_IN_COMMENT_MIDDLE},
        {"Folders in comment, close", FOLDERS_IN_COMMENT_CLOSE},
        {"Keywords1", KEYWORDS1}, {"Keywords2", KEYWORDS2}, {"Keywords3", KEYWORDS3}, {"Keywords4", KEYWORDS4},
        {"Keywords5", KEYWORDS5}, {"Keywords6", KEYWORDS6}, {"Keywords7", KEYWORDS7}, {"Keywords8", KEYWORDS8},
        {"Delimiters", DELIMITERS},
        {"Numbers, prefix1", NUMBER_PREFIX1}, {"Numbers, prefix2", NUMBER_PREFIX2},
        {"Numbers, extras1", NUMBER_EXTRAS1}, {"Numbers, extras2", NUMBER_EXTRAS2},
        {"Numbers, suffix1", NUMBER_SUFFIX1}, {"Numbers, suffix2", NUMBER_SUFFIX2}, {"Numbers, range", NUMBER_RANGE},
    };
    return m;
}

const std::map<std::string, int, std::less<>>& styleIdMap()
{
    static const std::map<std::string, int, std::less<>> m = {
        // UDL 1.0 names
        {"FOLDEROPEN", 13}, {"FOLDERCLOSE", 13}, {"KEYWORD1", 4}, {"KEYWORD2", 5}, {"KEYWORD3", 6}, {"KEYWORD4", 7},
        {"COMMENT", 1}, {"COMMENT LINE", 2}, {"NUMBER", 3}, {"OPERATOR", 12},
        {"DELIMINER1", 16}, {"DELIMINER2", 17}, {"DELIMINER3", 18},
        // UDL 2.x names
        {"DEFAULT", 0}, {"COMMENTS", 1}, {"LINE COMMENTS", 2}, {"NUMBERS", 3},
        {"KEYWORDS1", 4}, {"KEYWORDS2", 5}, {"KEYWORDS3", 6}, {"KEYWORDS4", 7},
        {"KEYWORDS5", 8}, {"KEYWORDS6", 9}, {"KEYWORDS7", 10}, {"KEYWORDS8", 11}, {"OPERATORS", 12},
        {"FOLDER IN CODE1", 13}, {"FOLDER IN CODE2", 14}, {"FOLDER IN COMMENT", 15},
        {"DELIMITERS1", 16}, {"DELIMITERS2", 17}, {"DELIMITERS3", 18}, {"DELIMITERS4", 19},
        {"DELIMITERS5", 20}, {"DELIMITERS6", 21}, {"DELIMITERS7", 22}, {"DELIMITERS8", 23},
    };
    return m;
}

// Lists sent as properties instead of keyword sets (GlobalMappers::setLexerMapper)
const std::map<int, const char*>& propertyLists()
{
    static const std::map<int, const char*> m = {
        {COMMENTS, "userDefine.comments"}, {DELIMITERS, "userDefine.delimiters"},
        {OPERATORS1, "userDefine.operators1"}, {NUMBER_PREFIX1, "userDefine.numberPrefix1"},
        {NUMBER_PREFIX2, "userDefine.numberPrefix2"}, {NUMBER_EXTRAS1, "userDefine.numberExtras1"},
        {NUMBER_EXTRAS2, "userDefine.numberExtras2"}, {NUMBER_SUFFIX1, "userDefine.numberSuffix1"},
        {NUMBER_SUFFIX2, "userDefine.numberSuffix2"}, {NUMBER_RANGE, "userDefine.numberRange"},
        {FOLDERS_IN_CODE1_OPEN, "userDefine.foldersInCode1Open"},
        {FOLDERS_IN_CODE1_MIDDLE, "userDefine.foldersInCode1Middle"},
        {FOLDERS_IN_CODE1_CLOSE, "userDefine.foldersInCode1Close"},
    };
    return m;
}

// Keyword list encoding of upstream setUserLexer: quotes group words, inner spaces become
// \v (double quotes) or \b (single quotes), backslash escapes quotes and backslash.
std::string encodeUdlKeywords(const std::string& kw)
{
    std::string out;
    bool inDouble = false, inSingle = false, nonWsFound = false;
    const size_t len = kw.size();
    for (size_t j = 0; j < len; ++j) {
        const char c = kw[j];
        if (!inSingle && c == '"') { inDouble = !inDouble; continue; }
        if (!inDouble && c == '\'') { inSingle = !inSingle; continue; }
        if (c == '\\' && j + 1 < len && (kw[j + 1] == '"' || kw[j + 1] == '\'' || kw[j + 1] == '\\')) {
            out.push_back(kw[++j]);
            continue;
        }
        if (inDouble || inSingle) {
            const char prev = j > 0 ? kw[j - 1] : '\0';
            const char next = j + 1 < len ? kw[j + 1] : '\0';
            if (static_cast<unsigned char>(c) > ' ') {
                out.push_back(c);
                nonWsFound = true;
            } else if (nonWsFound && prev != '"' && next != '"' && static_cast<unsigned char>(next) > ' ') {
                out.push_back(inDouble ? '\v' : '\b');
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void readUserLang(const pugi::xml_node& ul, UserLanguage& u)
{
    const pugi::xml_node settings = ul.child("Settings");
    if (const pugi::xml_node g = settings.child("Global")) {
        u.caseIgnored = xml::getBool(g, "caseIgnored", false);
        u.allowFoldOfComments = xml::getBool(g, "allowFoldOfComments", false);
        u.forcePureLC = g.attribute("forcePureLC").as_int(0);
        u.decimalSeparator = g.attribute("decimalSeparator").as_int(0);
        u.foldCompact = xml::getBool(g, "foldCompact", false);
    }
    if (const pugi::xml_node p = settings.child("Prefix")) {
        if (u.udlVersion == "2.1" || u.udlVersion == "2.0") {
            for (int i = 0; i < kUdlKeywordGroups; ++i)
                u.isPrefix[static_cast<size_t>(i)] = xml::getBool(p, ("Keywords" + std::to_string(i + 1)).c_str(), false);
        } else {
            for (int i = 0; i < 4; ++i)
                u.isPrefix[static_cast<size_t>(i)] = xml::getBool(p, ("words" + std::to_string(i + 1)).c_str(), false);
        }
    }

    for (pugi::xml_node kw = ul.child("KeywordLists").child("Keywords"); kw; kw = kw.next_sibling("Keywords")) {
        const std::string name = kw.attribute("name").value();
        const char* value = kw.text().get();
        if (!kw.first_child())
            continue;
        if (u.udlVersion.empty() && name == "Delimiters" && std::string_view(value).size() >= 6) {
            // pre 2.0 delimiters "ABCDEF" -> upstream conversion
            const std::string v = value;
            std::string t = "00";
            if (v[0] != '0') t += v[0];
            t += " 01 02";
            if (v[3] != '0') t += v[3];
            t += " 03";
            if (v[1] != '0') t += v[1];
            t += " 04 05";
            if (v[4] != '0') t += v[4];
            t += " 06";
            if (v[2] != '0') t += v[2];
            t += " 07 08";
            if (v[5] != '0') t += v[5];
            t += " 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23";
            u.keywordLists[DELIMITERS] = t;
        } else if (name == "Comment") {
            // pre 2.0 comment list
            std::string t = std::string(" ") + value;
            for (size_t pos = t.find(" 0"); pos != std::string::npos; pos = t.find(" 0", pos + 1))
                t.replace(pos, 2, " 00");
            for (size_t pos = t.find(" 1"); pos != std::string::npos; pos = t.find(" 1"))
                t.replace(pos, 2, " 03");
            for (size_t pos = t.find(" 2"); pos != std::string::npos; pos = t.find(" 2"))
                t.replace(pos, 2, " 04");
            t += " 01 02";
            if (!t.empty() && t[0] == ' ')
                t.erase(0, 1);
            u.keywordLists[COMMENTS] = t;
        } else if (const int id = UserLanguageRegistry::keywordListId(name); id >= 0) {
            u.keywordLists[static_cast<size_t>(id)] = std::strlen(value) < 30720
                ? std::string(value) : std::string("imported string too long, needs to be < max_char(30720)");
        }
    }

    for (pugi::xml_node ws = ul.child("Styles").child("WordsStyle"); ws; ws = ws.next_sibling("WordsStyle")) {
        const int id = UserLanguageRegistry::styleId(ws.attribute("name").value());
        if (id < 0)
            continue;
        Style s;
        s.styleId = id;
        s.name = ws.attribute("name").value();
        auto colour = [](const char* hex) -> int {
            if (!*hex) return -1;
            char* end = nullptr;
            const long v = std::strtol(hex, &end, 16);
            return *end ? -1 : static_cast<int>(v);
        };
        s.fg = colour(ws.attribute("fgColor").value());
        s.bg = colour(ws.attribute("bgColor").value());
        s.fontName = ws.attribute("fontName").value();
        s.fontStyle = *ws.attribute("fontStyle").value() ? ws.attribute("fontStyle").as_int(-1) : -1;
        s.fontSize = *ws.attribute("fontSize").value() ? ws.attribute("fontSize").as_int(-1) : -1;
        s.colorStyle = *ws.attribute("colorStyle").value() ? ws.attribute("colorStyle").as_int(-1) : -1;
        s.nesting = ws.attribute("nesting").as_int(0);
        u.styles.erase(std::remove_if(u.styles.begin(), u.styles.end(), [id](const Style& x) { return x.styleId == id; }),
                       u.styles.end());
        u.styles.push_back(std::move(s));
    }
    // upstream adds default styles for missing ids
    static const char* const kNames[kUdlStyleCount] = {
        "DEFAULT", "COMMENTS", "LINE COMMENTS", "NUMBERS", "KEYWORDS1", "KEYWORDS2", "KEYWORDS3", "KEYWORDS4",
        "KEYWORDS5", "KEYWORDS6", "KEYWORDS7", "KEYWORDS8", "OPERATORS", "FOLDER IN CODE1", "FOLDER IN CODE2",
        "FOLDER IN COMMENT", "DELIMITERS1", "DELIMITERS2", "DELIMITERS3", "DELIMITERS4", "DELIMITERS5",
        "DELIMITERS6", "DELIMITERS7", "DELIMITERS8"};
    for (int i = 0; i < kUdlStyleCount; ++i)
        if (!u.style(i)) {
            Style s;
            s.styleId = i;
            s.name = kNames[i];
            u.styles.push_back(s);
        }
    std::sort(u.styles.begin(), u.styles.end(), [](const Style& a, const Style& b) { return a.styleId < b.styleId; });
}

} // namespace

const Style* UserLanguage::style(int id) const
{
    for (const auto& s : styles)
        if (s.styleId == id)
            return &s;
    return nullptr;
}

int UserLanguageRegistry::keywordListId(std::string_view name)
{
    const auto& m = keywordIdMap();
    const auto it = m.find(name);
    return it == m.end() ? -1 : it->second;
}

int UserLanguageRegistry::styleId(std::string_view name)
{
    const auto& m = styleIdMap();
    const auto it = m.find(name);
    return it == m.end() ? -1 : it->second;
}

int UserLanguageRegistry::loadFromString(std::string_view text, std::string_view sourceFile)
{
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return 0;
    int added = 0;
    for (pugi::xml_node ul = doc.child("NotepadPlus").child("UserLang"); ul; ul = ul.next_sibling("UserLang")) {
        UserLanguage u;
        u.name = ul.attribute("name").value();
        if (u.name.empty())
            continue;
        // Settings, KeywordLists and Styles are mandatory upstream
        if (!ul.child("Settings") || !ul.child("KeywordLists") || !ul.child("Styles"))
            continue;
        u.ext = ul.attribute("ext").value();
        u.udlVersion = ul.attribute("udlVersion").value();
        u.darkModeTheme = xml::getBool(ul, "darkModeTheme", false);
        u.sourceFile = std::string(sourceFile);
        readUserLang(ul, u);
        m_langs.push_back(std::move(u));
        ++added;
    }
    return added;
}

int UserLanguageRegistry::loadFile(std::string_view utf8Path)
{
    const auto content = readFileContent(utf8Path);
    return content ? loadFromString(*content, utf8Path) : 0;
}

int UserLanguageRegistry::loadAll(std::string_view userDefineLangXml, std::string_view dir)
{
    int n = 0;
    if (!userDefineLangXml.empty() && pathExists(userDefineLangXml, PathKind::File))
        n += loadFile(userDefineLangXml);
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(pathFromUtf8(dir), ec)) {
        const std::string p = nativeUtf8Path(e.path());
        if (e.is_regular_file() && iequalsAscii(extensionOf(p), "xml"))
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files)
        n += loadFile(f);
    return n;
}

const UserLanguage* UserLanguageRegistry::find(std::string_view name) const
{
    for (const auto& u : m_langs)
        if (u.name == name)
            return &u;
    return nullptr;
}

const UserLanguage* UserLanguageRegistry::findForFile(std::string_view utf8Path) const
{
    const std::string ext = extensionOf(utf8Path);
    if (ext.empty())
        return nullptr;
    for (const auto& u : m_langs)
        for (const auto& e : tokenize(u.ext, ' '))
            if (iequalsAscii(e, ext))
                return &u;
    return nullptr;
}

UserLexerConfig buildUserLexerConfig(const UserLanguage& u)
{
    UserLexerConfig c;
    auto prop = [&c](std::string k, std::string v) { c.properties.emplace_back(std::move(k), std::move(v)); };
    prop("fold", "1");
    prop("userDefine.isCaseIgnored", u.caseIgnored ? "1" : "0");
    prop("userDefine.allowFoldOfComments", u.allowFoldOfComments ? "1" : "0");
    prop("userDefine.foldCompact", u.foldCompact ? "1" : "0");
    for (int i = 0; i < kUdlKeywordGroups; ++i)
        prop("userDefine.prefixKeywords" + std::to_string(i + 1), u.isPrefix[static_cast<size_t>(i)] ? "1" : "0");

    for (int i = 0; i < kUdlKeywordListCount; ++i) {
        const std::string& kw = u.keywordLists[static_cast<size_t>(i)];
        if (const auto it = propertyLists().find(i); it != propertyLists().end())
            prop(it->second, kw);
        else // OPERATORS2, FOLDERS_IN_CODE2*, FOLDERS_IN_COMMENT*, KEYWORDS1-8
            c.keywordSets.push_back(encodeUdlKeywords(kw));
    }
    prop("userDefine.forcePureLC", std::to_string(u.forcePureLC));
    prop("userDefine.decimalSeparator", std::to_string(u.decimalSeparator));
    for (const auto& s : u.styles) {
        char key[32];
        std::snprintf(key, sizeof(key), "userDefine.nesting.%02d", s.styleId);
        prop(key, std::to_string(s.nesting));
    }
    // upstream also sets userDefine.udlName / userDefine.currentBufferID (pointer values used by
    // LexUser as cache keys); the editor layer adds them per buffer.
    return c;
}

} // namespace npp::core
