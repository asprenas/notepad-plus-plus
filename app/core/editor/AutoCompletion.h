// Auto-completion, function call tips and auto-insertion of matched characters (phase 3, P3-07):
// port of upstream ScintillaComponent/AutoCompletion.cpp and FunctionCallTip.cpp (v8.9.8.1) on
// an IEditor. API files are the upstream autoCompletion/<language>.xml (installer/APIs).
#pragma once

#include "core/lang/LangType.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace npp::core {

class IEditor;

// API file model (<NotepadPlus><AutoComplete><Environment/><KeyWord name func><Overload retVal descr><Param name/>)
struct ApiOverload {
    std::string retVal, descr;
    std::vector<std::string> params;
};
struct ApiKeyword {
    std::string name;
    bool func = false;
    std::vector<ApiOverload> overloads;
};
struct ApiFile {
    bool ignoreCase = true;
    char start = '(', stop = ')', param = ',', terminal = ';';
    std::string additionalWordChar;
    std::vector<ApiKeyword> keywords; // file order
    bool load(const std::string& utf8Path);
    bool loadFromString(const std::string& xml);
};

struct AutoCompletionSettings { // GuiConfig auto-completion / auto-insert
    int  action = 3;            // 0 none, 1 function, 2 word, 3 both (upstream autoc_*)
    int  fromLen = 1;
    bool ignoreNumbers = true;
    bool brief = false;
    bool funcParams = true;
};

struct MatchedPairConf { // upstream MatchedPairConf (Preferences > Auto-Completion)
    std::vector<std::pair<char, char>> userPairs;
    bool parentheses = false, brackets = false, curlyBrackets = false, quotes = false, doubleQuotes = false, htmlXmlTag = false;
};

class FunctionCallTip {
public:
    explicit FunctionCallTip(const IEditor& e) : m_e(e) {}
    void setApi(const ApiFile* api);
    bool updateCalltip(int ch, bool needShown = false);
    void showNextOverload();
    void showPrevOverload();
    bool isVisible() const;
    void close();

private:
    friend class AutoCompletion;
    bool getCursorFunction();
    bool loadFunction();
    void showCalltip();
    void reset();
    bool isAdditionalWordChar(char ch) const;

    const IEditor& m_e;
    const ApiFile* m_api = nullptr;
    intptr_t m_curPos = 0, m_startPos = 0;
    std::string m_funcName;
    bool m_haveFuncName = false;
    std::vector<std::string> m_retVals, m_descriptions;
    std::vector<std::vector<std::string>> m_overloads;
    size_t m_currentNbOverloads = 0, m_currentOverload = 0, m_currentParam = 0;
    bool m_selfActivated = false;
};

class AutoCompletion {
public:
    explicit AutoCompletion(const IEditor& e) : m_e(e), m_funcCalltip(e) {}

    // Loads <apiDir>/<apiName>.xml (upstream getApiFileName: language name, UDL name)
    bool setLanguage(LangType lang, const std::string& apiDir, const std::string& apiName);
    void setSettings(const AutoCompletionSettings& s) { m_settings = s; }
    bool isFuncCompletionActive() const noexcept { return m_funcCompletionActive; }

    bool showApiComplete();
    bool showApiAndWordComplete();
    bool showWordComplete(bool autoInsert);
    bool showFunctionComplete();
    void showPathCompletion();
    void update(int character);              // on SCN_CHARADDED
    void callTipClick(size_t direction);      // 1 previous, 2 next overload
    void insertMatchedChars(int character, const MatchedPairConf& conf);
    FunctionCallTip& callTip() noexcept { return m_funcCalltip; }

    enum AutocType { Func, FuncAndWord, FuncBrief, Word }; // upstream order (showAutoComplete tests "type & FuncAndWord")
    bool showAutoComplete(AutocType type, bool autoInsert);

private:
    struct MatchedChar {
        char c;
        intptr_t pos;
    };
    void getWordArray(std::vector<std::string>& wordArray, const std::string& beginChars, const std::string& excludeChars) const;
    std::string getCloseTag(intptr_t caretPos, bool isHTML) const;
    void removeInvalidMatched(MatchedChar mci);
    void addMatched(MatchedChar mci);
    intptr_t searchMatched(char startChar, char endChar, intptr_t posToDetect);

    const IEditor& m_e;
    LangType m_curLang = L_TEXT;
    std::string m_apiPath;
    ApiFile m_api;
    bool m_funcCompletionActive = false;
    bool m_ignoreCase = true;
    std::vector<std::string> m_keyWordArray;
    std::string m_keyWords;
    size_t m_keyWordMaxLen = 0;
    bool m_imagesRegistered = false;
    FunctionCallTip m_funcCalltip;
    AutoCompletionSettings m_settings;
    std::vector<MatchedChar> m_insertedMatchedChars;
};

} // namespace npp::core
