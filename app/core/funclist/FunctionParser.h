// Function List parsers (port of upstream WinControls/FunctionList/functionParser.*).
// The parser rules come from functionList/*.xml (regular expressions run by the Notepad++
// Boost.Regex Scintilla engine, SCFIND_REGEXP | SCFIND_POSIX | SCFIND_REGEXP_DOTMATCHESNL) and
// the language association from functionList/overrideMap.xml. Runs on any IEditor.
#pragma once

#include "core/editor/IEditor.h"
#include "core/lang/LangType.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace npp::core {

struct FoundFunction {
    std::string name;       // upstream foundInfo::_data
    std::string className;  // _data2 (empty: top level)
    intptr_t    pos = -1;
    intptr_t    classPos = -1;
};

using Zones = std::vector<std::pair<size_t, size_t>>;

class FunctionParser {
public:
    FunctionParser(std::string id, std::string displayName, std::string commentExpr, std::string functionExpr,
                   std::vector<std::string> functionNameExprs, std::vector<std::string> classNameExprs)
        : m_id(std::move(id)), m_displayName(std::move(displayName)), m_commentExpr(std::move(commentExpr)),
          m_functionExpr(std::move(functionExpr)), m_functionNameExprs(std::move(functionNameExprs)),
          m_classNameExprs(std::move(classNameExprs))
    {}
    virtual ~FunctionParser() = default;

    virtual void parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
                       const std::string& className = {}) = 0;
    void funcParse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
                   const std::string& className = {}, const Zones* commentZones = nullptr);

    const std::string& id() const noexcept { return m_id; }
    const std::string& displayName() const noexcept { return m_displayName; }

    static bool isInZones(size_t pos, const Zones& zones);

protected:
    void getCommentZones(Zones& zones, size_t begin, size_t end, const IEditor& e);
    static void getInvertZones(Zones& dest, const Zones& source, size_t begin, size_t end);
    static std::string parseSubLevel(size_t begin, size_t end, std::vector<std::string> exprs, intptr_t& foundPos,
                                     const IEditor& e);

    std::string m_id, m_displayName, m_commentExpr, m_functionExpr;
    std::vector<std::string> m_functionNameExprs, m_classNameExprs;
};

class FunctionUnitParser : public FunctionParser {
public:
    using FunctionParser::FunctionParser;
    void parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
               const std::string& className = {}) override;
};

class FunctionZoneParser : public FunctionParser {
public:
    FunctionZoneParser(std::string id, std::string displayName, std::string commentExpr, std::string rangeExpr,
                       std::string openSymbol, std::string closeSymbol, std::vector<std::string> classNameExprs,
                       std::string functionExpr, std::vector<std::string> functionNameExprs)
        : FunctionParser(std::move(id), std::move(displayName), std::move(commentExpr), std::move(functionExpr),
                         std::move(functionNameExprs), std::move(classNameExprs)),
          m_rangeExpr(std::move(rangeExpr)), m_openSymbol(std::move(openSymbol)), m_closeSymbol(std::move(closeSymbol))
    {}
    void parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
               const std::string& className = {}) override;

protected:
    void classParse(std::vector<FoundFunction>& out, Zones& scanned, const Zones& commentZones, size_t begin,
                    size_t end, const IEditor& e);

private:
    static size_t bodyClosePos(size_t begin, const std::string& open, const std::string& close,
                               const Zones& commentZones, const IEditor& e);
    std::string m_rangeExpr, m_openSymbol, m_closeSymbol;
};

class FunctionMixParser : public FunctionZoneParser {
public:
    FunctionMixParser(std::string id, std::string displayName, std::string commentExpr, std::string rangeExpr,
                      std::string openSymbol, std::string closeSymbol, std::vector<std::string> classNameExprs,
                      std::string functionExpr, std::vector<std::string> functionNameExprs,
                      std::unique_ptr<FunctionUnitParser> unitParser)
        : FunctionZoneParser(std::move(id), std::move(displayName), std::move(commentExpr), std::move(rangeExpr),
                             std::move(openSymbol), std::move(closeSymbol), std::move(classNameExprs),
                             std::move(functionExpr), std::move(functionNameExprs)),
          m_unitParser(std::move(unitParser))
    {}
    void parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
               const std::string& className = {}) override;

private:
    std::unique_ptr<FunctionUnitParser> m_unitParser;
};

// Loads parsers on demand from the user functionList/ folder, then the installed one
// (upstream FunctionParsersManager).
class FunctionParsersManager {
public:
    // Returns false when no overrideMap.xml could be read (built-in lexer names are still used).
    bool init(const std::string& userDir, const std::string& installedDir);

    // Parses the whole document; false when no parser exists for the language.
    bool parse(std::vector<FoundFunction>& out, const IEditor& e, LangType lang, const std::string& udlName = {});
    FunctionParser* parser(LangType lang, const std::string& udlName = {});

private:
    struct Entry {
        std::string fileName;  // rule file name from overrideMap.xml (empty: <lexer name>.xml)
        std::string udlName;
        std::unique_ptr<FunctionParser> parser;
        bool tried = false;
    };
    bool readOverrideMap(const std::string& dir);
    bool load(Entry& entry, const std::string& dir, LangType lang);
    Entry* entryFor(LangType lang, const std::string& udlName);

    std::string m_userDir, m_installedDir;
    std::vector<Entry> m_langs = std::vector<Entry>(kLangCount);
    std::vector<Entry> m_udls;
};

// Upstream FunctionListPanel::serialize JSON ({"leaves":[..],"nodes":[{"leaves":[..],"name":".."}],"root":".."}),
// byte-identical to nlohmann::json::dump() (keys sorted, no spaces).
std::string functionListJson(const std::vector<FoundFunction>& found, const std::string& rootName);

} // namespace npp::core
