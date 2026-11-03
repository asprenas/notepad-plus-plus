#include "AutoCompletion.h"

#include "IEditor.h"
#include "core/util/PathUtil.h"

#include <SciLexer.h>
#include <Scintilla.h>

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace npp::core {

namespace {

constexpr int kFuncImgId = 1000, kBoxImgId = 1001;
constexpr size_t kTagMaxLen = 256;

// upstream xpm images of the auto-completion list (function / keyword)
const char* const kXpmFn[] = {
    "16 16 36 1 ", "u c None", "  c #131313", ". c #252525", "X c #161616", "o c #202020", "O c #393939", "+ c #242424",
    "@ c #282828", "# c #4E4E4E", "$ c #343434", "% c #5B5B5B", "& c #5F5F5F", "* c #626262", "= c #404040", "- c #686868",
    "; c #434343", ": c #464646", "> c #484848", ", c #494949", "< c #515151", "1 c #929292", "2 c #9B9B9B", "3 c #636363",
    "4 c #656565", "5 c #AFAFAF", "6 c #B7B7B7", "7 c #757575", "8 c #CDCDCD", "9 c #858585", "0 c #868686", "q c #DDDDDD",
    "w c #E1E1E1", "e c #E9E9E9", "r c #EEEEEE", "t c #959595", "y c #F6F6F6",
    "uuuuuuuuuuuuuuuu", "uuuuu5o.:uuuuuuu", "uuuu8 $:.0uuuuuu", "uuuu2 uuuuuuuuuu", "uuu6$ 46uuuuuuuu", "uuuO   Ouuuuuuuu",
    "uuuu;#uuuuuuuuuu", "uuuu##u& 3uu<+uu", "uuuu#;0.@X0, >uu", "uuuu+>uuuoo >uuu", "uuuu >uuu* =uuuu", "uuuu 2uu, Xotuuu",
    "uuu# 4u< >9 %ouu", "u:,#X0uO>uu1 $ u", "u- +7uuuuuuuuuuu", "uuuuuuuuuuuuuuuu"};
const char* const kXpmBox[] = {
    "16 16 33 1 ", "r c None", "  c #000000", ". c #030303", "X c #101010", "o c #181818", "O c #202020", "+ c #282828",
    "@ c #191919", "# c #222222", "$ c #252525", "% c #484848", "& c #505050", "* c #606060", "= c #444444", "- c #474747",
    "; c #505050", ": c #535353", "> c #565656", ", c #979797", "< c #9A9A9A", "1 c #9F9F9F", "2 c #A7A7A7", "3 c #AFAFAF",
    "4 c #B7B7B7", "5 c #757575", "6 c #767676", "7 c #787878", "8 c #818181", "9 c #D7D7D7", "0 c #DFDFDF", "q c #E7E7E7",
    "w c #EFEFEF", "e c #979797",
    "rrrrrrrrrrrrrrrr", "rrrrrrrrrrrrrrrr", "rrre4;$  *rrrrrr", "r4@ $-rr6;X rrrr", "r1oX>rrrrr5+  rr", "r1*r%OrrrXO*&;rr",
    "r1*rrr==%rrr1;rr", "r1*rrrr$rrrr1;rr", "r1*rrrr$rrrr1;rr", "r1*rrrr$rrrr1;rr", "r1*rrrr$rrrr1;rr", "r3o<rrr$rrr8$;rr",
    "rr7#%rr$rr #;rrr", "rrrr,o.$.%:rrrrr", "rrrrrr7*7rrrrrrr", "rrrrrrrrrrrrrrrr"};

bool isInList(const std::string& w, const std::vector<std::string>& a) { return std::find(a.begin(), a.end(), w) != a.end(); }

bool isAllDigits(const std::string& s)
{
    return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

int lowerChar(unsigned char c) { return std::tolower(c); }

void sortInsensitive(std::vector<std::string>& v)
{
    std::sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::toupper(static_cast<unsigned char>(x)) < std::toupper(static_cast<unsigned char>(y));
        });
    });
}

// _stricmp(a, b.substr(0, n))
int compareNoCase(const std::string& a, const std::string& b)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int d = lowerChar(static_cast<unsigned char>(a[i])) - lowerChar(static_cast<unsigned char>(b[i]));
        if (d)
            return d;
    }
    return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

int testNameNoCase(const std::string& name1, const std::string& name2, size_t len = 1024)
{
    for (size_t i = 0; i < len; ++i) {
        const char c1 = i < name1.size() ? name1[i] : '\0', c2 = i < name2.size() ? name2[i] : '\0';
        if (c1 == '\0' || c2 == '\0')
            return c1 - c2;
        if (c1 != c2 && lowerChar(static_cast<unsigned char>(c1)) != lowerChar(static_cast<unsigned char>(c2)))
            return c1 - c2;
    }
    return 0;
}

bool isBasicWordChar(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }

intptr_t searchInTarget(const IEditor& e, std::string_view text, intptr_t from, intptr_t to)
{
    e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(from), to);
    return e.send(SCI_SEARCHINTARGET, text.size(), reinterpret_cast<intptr_t>(text.data()));
}

} // namespace

// ------------------------------------------------------------------------------------------------
// API file
// ------------------------------------------------------------------------------------------------

static bool parseApi(pugi::xml_document& doc, ApiFile& api)
{
    api = {};
    pugi::xml_node autoNode = doc.child("NotepadPlus").child("AutoComplete");
    if (!autoNode || !autoNode.child("KeyWord"))
        return false;
    if (pugi::xml_node env = autoNode.child("Environment")) {
        if (std::strcmp(env.attribute("ignoreCase").as_string("yes"), "no") == 0)
            api.ignoreCase = false;
        auto ch = [&env](const char* a, char& out) {
            const char* v = env.attribute(a).as_string();
            if (v[0])
                out = v[0];
        };
        ch("startFunc", api.start);
        ch("stopFunc", api.stop);
        ch("paramSeparator", api.param);
        ch("terminal", api.terminal);
        api.additionalWordChar = env.attribute("additionalWordChar").as_string();
    }
    for (pugi::xml_node k = autoNode.child("KeyWord"); k; k = k.next_sibling("KeyWord")) {
        ApiKeyword kw;
        kw.name = k.attribute("name").as_string();
        kw.func = std::strcmp(k.attribute("func").as_string(), "yes") == 0;
        for (pugi::xml_node o = k.child("Overload"); o; o = o.next_sibling("Overload")) {
            if (!o.attribute("retVal"))
                continue; // malformed node
            ApiOverload ov;
            ov.retVal = o.attribute("retVal").as_string();
            ov.descr = o.attribute("descr").as_string();
            for (pugi::xml_node p = o.child("Param"); p; p = p.next_sibling("Param"))
                if (p.attribute("name"))
                    ov.params.push_back(p.attribute("name").as_string());
            kw.overloads.push_back(std::move(ov));
        }
        api.keywords.push_back(std::move(kw));
    }
    return true;
}

bool ApiFile::load(const std::string& utf8Path)
{
    pugi::xml_document doc;
    if (!doc.load_file(pathFromUtf8(utf8Path).c_str()))
        return false;
    return parseApi(doc, *this);
}

bool ApiFile::loadFromString(const std::string& xml)
{
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size()))
        return false;
    return parseApi(doc, *this);
}

// ------------------------------------------------------------------------------------------------
// FunctionCallTip
// ------------------------------------------------------------------------------------------------

namespace {
struct Token {
    size_t begin;
    int length;
    bool isIdentifier;
};
struct FunctionValues {
    int lastIdentifier = -1, lastFunctionIdentifier = -1, param = 0, scopeLevel = -1;
};
} // namespace

bool FunctionCallTip::isVisible() const { return m_e.send(SCI_CALLTIPACTIVE) != 0; }
bool FunctionCallTip::isAdditionalWordChar(char ch) const { return m_api && m_api->additionalWordChar.find(ch) != std::string::npos; }

void FunctionCallTip::setApi(const ApiFile* api)
{
    if (isVisible())
        close();
    m_api = api;
    reset();
    m_haveFuncName = false;
}

bool FunctionCallTip::updateCalltip(int ch, bool needShown)
{
    if (!m_api)
        return false;
    if (!needShown && ch != m_api->start && ch != m_api->param && !isVisible())
        return false;
    m_curPos = m_e.send(SCI_GETCURRENTPOS);
    if (!getCursorFunction()) {
        close();
        return false;
    }
    showCalltip();
    return true;
}

void FunctionCallTip::showNextOverload()
{
    if (!isVisible())
        return;
    if (m_currentNbOverloads > 0)
        m_currentOverload = (m_currentOverload + 1) % m_currentNbOverloads;
    showCalltip();
}

void FunctionCallTip::showPrevOverload()
{
    if (!isVisible())
        return;
    m_currentOverload = m_currentOverload > 0 ? m_currentOverload - 1 : m_currentNbOverloads - 1;
    showCalltip();
}

void FunctionCallTip::close()
{
    if (!isVisible() || !m_selfActivated)
        return;
    m_e.send(SCI_CALLTIPCANCEL);
    m_selfActivated = false;
    m_currentOverload = 0;
}

bool FunctionCallTip::getCursorFunction()
{
    const auto line = m_e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(m_curPos));
    const intptr_t startpos = m_e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line));
    const intptr_t endpos = m_e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line));
    const intptr_t len = endpos - startpos + 3;
    const intptr_t offset = m_curPos - startpos;
    constexpr intptr_t maxLen = 256;
    if (offset < 2 || len >= maxLen) {
        reset();
        return false; // cannot be a function: needs a name and a separator
    }
    std::string lineData = m_e.textRange(startpos, startpos + m_e.send(SCI_LINELENGTH, static_cast<uintptr_t>(line)));
    lineData.resize(static_cast<size_t>(maxLen), '\0');

    std::vector<Token> tokens;
    for (int i = 0; i < offset; ++i) {
        char ch = lineData[static_cast<size_t>(i)];
        if (isBasicWordChar(ch) || isAdditionalWordChar(ch)) {
            int tokenLen = 0;
            const size_t begin = static_cast<size_t>(i);
            while ((isBasicWordChar(ch) || isAdditionalWordChar(ch)) && i < offset) {
                ++tokenLen;
                ++i;
                ch = lineData[static_cast<size_t>(i)];
            }
            tokens.push_back({begin, tokenLen, true});
            --i;
        } else if (!(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')) {
            tokens.push_back({static_cast<size_t>(i), 1, false});
        }
    }

    std::vector<FunctionValues> valueVec;
    FunctionValues curValue;
    int scopeLevel = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        const char c = lineData[t.begin];
        if (t.isIdentifier) {
            curValue.lastIdentifier = static_cast<int>(i);
        } else if (c == m_api->start) {
            ++scopeLevel;
            valueVec.push_back(curValue);
            curValue.scopeLevel = scopeLevel;
            if (i > 0 && curValue.lastIdentifier == static_cast<int>(i) - 1) {
                curValue.lastFunctionIdentifier = curValue.lastIdentifier;
                curValue.param = 0;
            } else {
                curValue.lastFunctionIdentifier = -1;
            }
        } else if (c == m_api->param && curValue.lastFunctionIdentifier > -1) {
            ++curValue.param;
        } else if (c == m_api->stop) {
            if (scopeLevel)
                --scopeLevel;
            if (!valueVec.empty()) {
                curValue = valueVec.back();
                valueVec.pop_back();
            } else {
                curValue = FunctionValues();
            }
        } else if (c == m_api->terminal) {
            valueVec.clear();
            curValue = FunctionValues();
        }
    }
    while (curValue.lastFunctionIdentifier == -1 && !valueVec.empty()) {
        curValue = valueVec.back();
        valueVec.pop_back();
    }
    if (curValue.lastFunctionIdentifier <= -1)
        return false;
    const Token& ft = tokens[static_cast<size_t>(curValue.lastFunctionIdentifier)];
    const std::string funcName = lineData.substr(ft.begin, static_cast<size_t>(ft.length));
    m_currentParam = static_cast<size_t>(curValue.param);
    bool same = false;
    if (m_haveFuncName)
        same = m_api->ignoreCase ? testNameNoCase(m_funcName, funcName, m_funcName.size()) == 0
                                 : funcName.compare(0, m_funcName.size(), m_funcName) == 0;
    if (same)
        return true;
    m_funcName = funcName;
    m_haveFuncName = true;
    return loadFunction();
}

bool FunctionCallTip::loadFunction()
{
    reset();
    const ApiKeyword* fn = nullptr;
    for (const ApiKeyword& k : m_api->keywords) {
        const int cmp = m_api->ignoreCase ? testNameNoCase(k.name, m_funcName) : k.name.compare(m_funcName);
        if (cmp == 0) {
            if (!k.func)
                return false; // name matches but it is not a function
            fn = &k;
            break;
        }
    }
    if (!fn)
        return false;
    for (const ApiOverload& o : fn->overloads) {
        m_retVals.push_back(o.retVal);
        m_descriptions.push_back(o.descr);
        m_overloads.push_back(o.params);
    }
    m_currentNbOverloads = m_overloads.size();
    return m_currentNbOverloads != 0;
}

void FunctionCallTip::showCalltip()
{
    if (m_currentNbOverloads == 0)
        return;
    size_t psize = m_overloads.at(m_currentOverload).size() + 1;
    if (m_currentParam >= psize) // the current overload has not enough parameters: take one that has
        for (size_t i = 0; i < m_overloads.size(); ++i)
            if (m_currentParam < m_overloads[i].size() + 1) {
                m_currentOverload = i;
                break;
            }
    const auto& params = m_overloads.at(m_currentOverload);
    std::stringstream text;
    if (m_currentNbOverloads > 1)
        text << "\001" << m_currentOverload + 1 << " of " << m_currentNbOverloads << "\002";
    text << m_retVals.at(m_currentOverload) << ' ' << m_funcName << ' ' << m_api->start;
    int hlStart = 0, hlEnd = 0;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i == m_currentParam) {
            hlStart = static_cast<int>(text.str().length());
            hlEnd = hlStart + static_cast<int>(params[i].size());
        }
        text << params[i];
        if (i < params.size() - 1)
            text << m_api->param << ' ';
    }
    text << m_api->stop;
    if (!m_descriptions.at(m_currentOverload).empty())
        text << "\n" << m_descriptions.at(m_currentOverload);
    if (isVisible())
        m_e.send(SCI_CALLTIPCANCEL);
    else
        m_startPos = m_curPos;
    const std::string s = text.str();
    m_e.send(SCI_CALLTIPSHOW, static_cast<uintptr_t>(m_startPos), reinterpret_cast<intptr_t>(s.c_str()));
    m_selfActivated = true;
    if (hlStart != hlEnd)
        m_e.send(SCI_CALLTIPSETHLT, static_cast<uintptr_t>(hlStart), hlEnd);
}

void FunctionCallTip::reset()
{
    m_currentOverload = 0;
    m_currentParam = 0;
    m_startPos = 0;
    m_overloads.clear();
    m_currentNbOverloads = 0;
    m_retVals.clear();
    m_descriptions.clear();
}

// ------------------------------------------------------------------------------------------------
// AutoCompletion
// ------------------------------------------------------------------------------------------------

bool AutoCompletion::setLanguage(LangType lang, const std::string& apiDir, const std::string& apiName)
{
    const std::string path = pathAppend(apiDir, apiName + ".xml");
    if (m_curLang == lang && m_apiPath == path)
        return m_funcCompletionActive;
    m_curLang = lang;
    m_apiPath = path;
    m_funcCompletionActive = m_api.load(path);
    m_ignoreCase = !m_funcCompletionActive || m_api.ignoreCase;
    m_funcCalltip.setApi(m_funcCompletionActive ? &m_api : nullptr);
    m_keyWords.clear();
    m_keyWordArray.clear();
    m_keyWordMaxLen = 0;
    if (!m_funcCompletionActive)
        return false;
    for (const ApiKeyword& k : m_api.keywords) {
        if (k.name.empty())
            continue;
        m_keyWordArray.push_back(k.name + "\x1E" + std::to_string(k.func ? kFuncImgId : kBoxImgId));
        m_keyWordMaxLen = std::max(m_keyWordMaxLen, k.name.size());
    }
    if (m_ignoreCase)
        sortInsensitive(m_keyWordArray);
    else
        std::sort(m_keyWordArray.begin(), m_keyWordArray.end());
    for (const auto& w : m_keyWordArray)
        m_keyWords += w + " ";
    return true;
}

void AutoCompletion::getWordArray(std::vector<std::string>& wordArray, const std::string& beginChars, const std::string& excludeChars) const
{
    if (m_settings.ignoreNumbers && isAllDigits(beginChars))
        return;
    const std::string expr = "\\<" + beginChars + "[^ \\t\\n\\r.,;:\"(){}=<>'+!?\\[\\]]+";
    const intptr_t docLength = m_e.send(SCI_GETLENGTH);
    int flags = SCFIND_WORDSTART | SCFIND_REGEXP | SCFIND_POSIX;
    if (!m_ignoreCase || (!m_funcCompletionActive && m_curLang == L_TEXT))
        flags |= SCFIND_MATCHCASE;
    m_e.send(SCI_SETSEARCHFLAGS, static_cast<uintptr_t>(flags));
    intptr_t posFind = searchInTarget(m_e, expr, 0, docLength);
    const std::string boxId = "\x1E" + std::to_string(kBoxImgId), funcId = "\x1E" + std::to_string(kFuncImgId);
    while (posFind >= 0) {
        const intptr_t wordStart = m_e.send(SCI_GETTARGETSTART), wordEnd = m_e.send(SCI_GETTARGETEND);
        if (wordEnd - wordStart < 256) {
            const std::string w = m_e.textRange(wordStart, wordEnd);
            if (w != excludeChars) {
                if (m_funcCompletionActive && isInList(w + boxId, m_keyWordArray)) {
                    if (!isInList(w + boxId, wordArray))
                        wordArray.push_back(w + boxId);
                } else if (m_funcCompletionActive && isInList(w + funcId, m_keyWordArray)) {
                    if (!isInList(w + funcId, wordArray))
                        wordArray.push_back(w + funcId);
                } else if (!isInList(w, wordArray)) {
                    wordArray.push_back(w);
                }
            }
        }
        posFind = searchInTarget(m_e, expr, wordEnd, docLength);
    }
}

bool AutoCompletion::showAutoComplete(AutocType type, bool autoInsert)
{
    if (type == Func && !m_funcCompletionActive)
        return false;
    const intptr_t curPos = m_e.send(SCI_GETCURRENTPOS);
    const intptr_t startPos = m_e.send(SCI_WORDSTARTPOSITION, static_cast<uintptr_t>(curPos), 1);
    if (curPos == startPos)
        return false;
    const auto len = static_cast<size_t>(curPos - startPos);
    std::string words;
    if (type == Func) {
        if (len >= m_keyWordMaxLen)
            return false;
    } else {
        constexpr size_t bufSize = 256;
        if (len >= bufSize)
            return false;
        const intptr_t endPos = m_e.send(SCI_WORDENDPOSITION, static_cast<uintptr_t>(curPos), 1);
        if (static_cast<size_t>(endPos - startPos) >= bufSize)
            return false;
        const std::string beginChars = m_e.textRange(startPos, curPos);
        std::vector<std::string> wordArray;
        if (type == Word || type == FuncAndWord)
            getWordArray(wordArray, beginChars, m_e.textRange(startPos, endPos));
        if (type == FuncBrief || type == FuncAndWord)
            for (const std::string& kw : m_keyWordArray) {
                const int cmp = m_ignoreCase ? compareNoCase(beginChars, kw.substr(0, len)) : kw.compare(0, len, beginChars);
                if (cmp == 0 && !isInList(kw, wordArray))
                    wordArray.push_back(kw);
            }
        if (wordArray.empty())
            return false;
        if (type == Word && wordArray.size() == 1 && autoInsert) {
            const size_t sep = wordArray[0].find('\x1E');
            const std::string w = sep == std::string::npos ? wordArray[0] : wordArray[0].substr(0, sep);
            m_e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(startPos), curPos);
            m_e.send(SCI_REPLACETARGET, w.size(), reinterpret_cast<intptr_t>(w.data()));
            m_e.send(SCI_GOTOPOS, static_cast<uintptr_t>(startPos + static_cast<intptr_t>(w.size())));
            return true;
        }
        if (type == Word || (type & FuncAndWord)) {
            if (m_ignoreCase)
                sortInsensitive(wordArray);
            else
                std::sort(wordArray.begin(), wordArray.end());
        }
        for (size_t i = 0; i < wordArray.size(); ++i)
            words += (i ? " " : "") + wordArray[i];
    }
    if (!m_imagesRegistered) {
        m_e.send(SCI_CLEARREGISTEREDIMAGES);
        m_e.send(SCI_REGISTERIMAGE, kFuncImgId, reinterpret_cast<intptr_t>(kXpmFn));
        m_e.send(SCI_REGISTERIMAGE, kBoxImgId, reinterpret_cast<intptr_t>(kXpmBox));
        m_imagesRegistered = true;
    }
    m_e.send(SCI_AUTOCSETTYPESEPARATOR, '\x1E');
    m_e.send(SCI_AUTOCSETSEPARATOR, ' ');
    m_e.send(SCI_AUTOCSETIGNORECASE, m_ignoreCase);
    m_e.send(SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, m_ignoreCase);
    const std::string& list = type == Func ? m_keyWords : words;
    m_e.send(SCI_AUTOCSHOW, static_cast<uintptr_t>(curPos - startPos), reinterpret_cast<intptr_t>(list.c_str()));
    return true;
}

bool AutoCompletion::showApiComplete() { return showAutoComplete(Func, false); }
bool AutoCompletion::showApiAndWordComplete() { return showAutoComplete(FuncAndWord, false); }
bool AutoCompletion::showWordComplete(bool autoInsert) { return showAutoComplete(Word, autoInsert); }

bool AutoCompletion::showFunctionComplete()
{
    return m_funcCompletionActive && m_funcCalltip.updateCalltip(0, true);
}

// Path completion: upstream getRawPath / getPathsForPathCompletion; besides "X:\..." the port
// also completes absolute POSIX paths ("/...") since there are no drive letters on Linux.
void AutoCompletion::showPathCompletion()
{
    constexpr intptr_t bufSize = 260; // MAX_PATH
    const intptr_t currentPos = m_e.send(SCI_GETCURRENTPOS);
    const QString currentLine = QString::fromStdString(m_e.textRange(std::max<intptr_t>(0, currentPos - bufSize), currentPos));
    auto allowedBefore = [](QChar c) { return c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('(') || c.isSpace(); };
    QString rawPath;
    const int colon = static_cast<int>(currentLine.lastIndexOf(QLatin1Char(':')));
    if (colon > 0 && currentLine[colon - 1].isLetter() && (colon < 2 || allowedBefore(currentLine[colon - 2]))) {
        rawPath = currentLine.mid(colon - 1);
    } else {
#if !defined(_WIN32)
        int slash = static_cast<int>(currentLine.size()) - 1;
        while (slash >= 0 && !currentLine[slash].isSpace() && currentLine[slash] != QLatin1Char('"') && currentLine[slash] != QLatin1Char('\''))
            --slash;
        rawPath = currentLine.mid(slash + 1);
        if (!rawPath.startsWith(QLatin1Char('/')))
            return;
#else
        return;
#endif
    }
    const QChar sep = QDir::separator();
    auto trimSlash = [](QString p) {
        while (p.size() > 1 && (p.endsWith(QLatin1Char('\\')) || p.endsWith(QLatin1Char('/'))))
            p.chop(1);
        return p;
    };
    QString pathToMatch;
    if (QFileInfo(trimSlash(rawPath)).isFile())
        return;
    if (QFileInfo(rawPath).isDir() && (rawPath.endsWith(QLatin1Char('\\')) || rawPath.endsWith(QLatin1Char('/')) || rawPath.size() <= 3)) {
        pathToMatch = rawPath;
    } else {
        const int last = static_cast<int>(std::max(rawPath.lastIndexOf(QLatin1Char('\\')), rawPath.lastIndexOf(QLatin1Char('/'))));
        if (last < 0)
            return;
        pathToMatch = rawPath.left(last);
    }
    QString base = pathToMatch;
    if (!base.endsWith(QLatin1Char('\\')) && !base.endsWith(QLatin1Char('/')))
        base += sep;
    const QDir dir(base);
    if (!dir.exists())
        return;
    QStringList entries;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (entries.size() >= 2000)
            break;
        entries << base + fi.fileName() + (fi.isDir() ? QString(sep) : QString());
    }
    if (entries.isEmpty())
        return;
    const QByteArray list = entries.join(QLatin1Char('\n')).toUtf8();
    m_e.send(SCI_AUTOCSETSEPARATOR, '\n');
    m_e.send(SCI_AUTOCSETIGNORECASE, 1);
    m_e.send(SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, 1);
    m_e.send(SCI_AUTOCSHOW, static_cast<uintptr_t>(rawPath.toUtf8().size()), reinterpret_cast<intptr_t>(list.constData()));
}

void AutoCompletion::update(int character)
{
    if (!character)
        return;
    if (!m_funcCompletionActive && m_settings.action == 1)
        return;
    if (m_settings.funcParams || m_funcCalltip.isVisible())
        if (m_funcCalltip.updateCalltip(character))
            return;
    if (!m_settings.brief && m_e.send(SCI_AUTOCACTIVE) != 0)
        return; // already active: Scintilla handles it
    const intptr_t caret = m_e.send(SCI_GETCURRENTPOS);
    const intptr_t start = m_e.send(SCI_WORDSTARTPOSITION, static_cast<uintptr_t>(caret), 1);
    const std::string s = caret - start < 64 ? m_e.textRange(start, caret) : std::string();
    if (s.size() >= static_cast<size_t>(std::max(0, m_settings.fromLen))) {
        if (m_settings.action == 2)
            showWordComplete(false);
        else if (m_settings.action == 1)
            m_settings.brief ? showAutoComplete(FuncBrief, false) : showApiComplete();
        else if (m_settings.action == 3)
            showApiAndWordComplete();
    }
}

void AutoCompletion::callTipClick(size_t direction)
{
    if (!m_funcCompletionActive)
        return;
    if (direction == 1)
        m_funcCalltip.showPrevOverload();
    else if (direction == 2)
        m_funcCalltip.showNextOverload();
}

std::string AutoCompletion::getCloseTag(intptr_t caretPos, bool isHTML) const
{
    if (isHTML && m_e.send(SCI_GETSTYLEAT, static_cast<uintptr_t>(caretPos)) >= SCE_HJ_START)
        return {}; // inside a scripting language
    const char prev = static_cast<char>(m_e.send(SCI_GETCHARAT, static_cast<uintptr_t>(caretPos - 2)));
    const char prevprev = static_cast<char>(m_e.send(SCI_GETCHARAT, static_cast<uintptr_t>(caretPos - 3)));
    if ((prevprev == '-' && prev == '-') || prev == '/')
        return {}; // "-->", "<toto/>"
    m_e.send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);
    const intptr_t targetStart = searchInTarget(m_e, "<[^\\s>]*", caretPos, 0);
    if (targetStart < 0)
        return {};
    const intptr_t targetEnd = m_e.send(SCI_GETTARGETEND);
    const intptr_t foundLen = targetEnd - targetStart;
    if (foundLen < 2 || foundLen > static_cast<intptr_t>(kTagMaxLen) - 2)
        return {};
    const std::string tagHead = m_e.textRange(targetStart, targetEnd);
    if (tagHead[1] == '/' || tagHead[1] == '?' || tagHead.compare(0, 4, "<!--") == 0)
        return {};
    if (isHTML) {
        static const char* const voidTags[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "keygen",
                                               "link", "meta", "param", "source", "track", "wbr", "!doctype"};
        for (const char* t : voidTags) {
            const size_t n = std::strlen(t);
            if (compareNoCase(tagHead.substr(1, n), t) == 0)
                return {};
        }
    }
    return "</" + tagHead.substr(1) + ">";
}

void AutoCompletion::removeInvalidMatched(MatchedChar mci)
{
    if (mci.c == '\n' || mci.c == '\r') {
        m_insertedMatchedChars.clear();
        return;
    }
    for (int i = static_cast<int>(m_insertedMatchedChars.size()) - 1; i >= 0; --i) {
        const MatchedChar& m = m_insertedMatchedChars[static_cast<size_t>(i)];
        if (m.pos < mci.pos) {
            if (m_e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(mci.pos)) != m_e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(m.pos)))
                m_insertedMatchedChars.erase(m_insertedMatchedChars.begin() + i);
        } else {
            m_insertedMatchedChars.erase(m_insertedMatchedChars.begin() + i);
        }
    }
}

void AutoCompletion::addMatched(MatchedChar mci)
{
    removeInvalidMatched(mci);
    m_insertedMatchedChars.push_back(mci);
}

intptr_t AutoCompletion::searchMatched(char startChar, char endChar, intptr_t posToDetect)
{
    if (m_insertedMatchedChars.empty())
        return -1;
    const auto detectLine = m_e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(posToDetect));
    for (int i = static_cast<int>(m_insertedMatchedChars.size()) - 1; i >= 0; --i) {
        const MatchedChar m = m_insertedMatchedChars[static_cast<size_t>(i)];
        if (m.c != startChar)
            continue;
        if (m.pos < posToDetect) {
            const auto startLine = m_e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(m.pos));
            if (detectLine == startLine) {
                const intptr_t endPos = m_e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(startLine));
                for (intptr_t j = posToDetect; j <= endPos; ++j) {
                    const char a = static_cast<char>(m_e.send(SCI_GETCHARAT, static_cast<uintptr_t>(j)));
                    if (a != ' ') {
                        m_insertedMatchedChars.erase(m_insertedMatchedChars.begin() + i);
                        return a == endChar ? j : -1;
                    }
                }
            } else {
                m_insertedMatchedChars.erase(m_insertedMatchedChars.begin() + i);
            }
        } else {
            m_insertedMatchedChars.erase(m_insertedMatchedChars.begin() + i);
        }
    }
    return -1;
}

void AutoCompletion::insertMatchedChars(int character, const MatchedPairConf& conf)
{
    const intptr_t caretPos = m_e.send(SCI_GETCURRENTPOS);
    if (character > 127) {
        if (!m_insertedMatchedChars.empty())
            removeInvalidMatched({'\0', caretPos - 1});
        return;
    }
    const char ch = static_cast<char>(character);
    const char charPrev = static_cast<char>(m_e.send(SCI_GETCHARAT, static_cast<uintptr_t>(caretPos - 2)));
    const char charNext = static_cast<char>(m_e.send(SCI_GETCHARAT, static_cast<uintptr_t>(caretPos)));
    const bool prevBlank = charPrev == ' ' || charPrev == '\t' || charPrev == '\n' || charPrev == '\r' || charPrev == '\0';
    const bool nextBlank = charNext == ' ' || charNext == '\t' || charNext == '\n' || charNext == '\r' || caretPos == m_e.send(SCI_GETLENGTH);
    const bool nextClose = charNext == ')' || charNext == ']' || charNext == '}';
    const bool sandwich = (charPrev == '(' && charNext == ')') || (charPrev == '[' && charNext == ']') || (charPrev == '{' && charNext == '}');

    for (const auto& [open, close] : conf.userPairs) // user defined pairs first
        if (open == ch && nextBlank) {
            const char s[2] = {close, '\0'};
            m_e.send(SCI_INSERTTEXT, static_cast<uintptr_t>(caretPos), reinterpret_cast<intptr_t>(s));
            return;
        }

    std::string matched;
    auto quoteContext = [&] {
        return (prevBlank && nextBlank) || sandwich || (charPrev == '(' && nextBlank) || (prevBlank && charNext == ')')
               || (charPrev == '[' && nextBlank) || (prevBlank && charNext == ']') || (charPrev == '{' && nextBlank)
               || (prevBlank && charNext == '}');
    };
    switch (ch) {
    case '(':
    case '[':
    case '{': {
        const bool on = ch == '(' ? conf.parentheses : ch == '[' ? conf.brackets : conf.curlyBrackets;
        if (on && (nextBlank || nextClose)) {
            matched = ch == '(' ? ")" : ch == '[' ? "]" : "}";
            addMatched({ch, caretPos - 1});
        }
        break;
    }
    case '"':
    case '\'': {
        if (ch == '"' ? conf.doubleQuotes : conf.quotes) {
            if (!m_insertedMatchedChars.empty()) {
                const intptr_t pos = searchMatched(ch, ch, caretPos);
                if (pos != -1) { // typing the closing quote over the inserted one
                    m_e.send(SCI_DELETERANGE, static_cast<uintptr_t>(pos), 1);
                    m_e.send(SCI_GOTOPOS, static_cast<uintptr_t>(pos));
                    return;
                }
            }
            if (quoteContext()) {
                matched = std::string(1, ch);
                addMatched({ch, caretPos - 1});
            }
        }
        break;
    }
    case '>':
        if (conf.htmlXmlTag && (m_curLang == L_HTML || m_curLang == L_XML))
            matched = getCloseTag(caretPos, m_curLang == L_HTML);
        break;
    case ')':
    case ']':
    case '}': {
        if (m_insertedMatchedChars.empty())
            break;
        const bool on = ch == ')' ? conf.parentheses : ch == ']' ? conf.brackets : conf.curlyBrackets;
        if (!on)
            return;
        const char startChar = ch == ')' ? '(' : ch == ']' ? '[' : '{';
        const intptr_t pos = searchMatched(startChar, ch, caretPos);
        if (pos != -1) {
            m_e.send(SCI_DELETERANGE, static_cast<uintptr_t>(pos), 1);
            m_e.send(SCI_GOTOPOS, static_cast<uintptr_t>(pos));
        }
        return;
    }
    default:
        if (!m_insertedMatchedChars.empty())
            removeInvalidMatched({ch, caretPos - 1});
    }
    if (!matched.empty())
        m_e.send(SCI_INSERTTEXT, static_cast<uintptr_t>(caretPos), reinterpret_cast<intptr_t>(matched.c_str()));
}

} // namespace npp::core
