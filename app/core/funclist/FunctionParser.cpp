#include "FunctionParser.h"

#include "core/util/PathUtil.h"

#include <BoostRegexSearch.h>
#include <Scintilla.h>
#include <pugixml.hpp>

#include <cstdio>

namespace npp::core {

namespace {

constexpr int kFlags = SCFIND_REGEXP | SCFIND_POSIX | SCFIND_REGEXP_DOTMATCHESNL;

intptr_t searchInTarget(const IEditor& e, const std::string& text, size_t from, size_t to)
{
    e.send(SCI_SETTARGETRANGE, from, static_cast<intptr_t>(to));
    return e.send(SCI_SEARCHINTARGET, text.size(), reinterpret_cast<intptr_t>(text.data()));
}

// Upstream getGenericText into a 1024 char buffer (strncpy_s _TRUNCATE): stops at a NUL, max 1023 bytes.
std::string genericText(const IEditor& e, intptr_t start, intptr_t end)
{
    std::string s = e.textRange(start, end);
    if (const size_t nul = s.find('\0'); nul != std::string::npos)
        s.resize(nul);
    if (s.size() > 1023)
        s.resize(1023);
    return s;
}

std::string attr(const pugi::xml_node& n, const char* name)
{
    return n.attribute(name).as_string();
}

std::vector<std::string> exprList(const pugi::xml_node& parent, const char* tag)
{
    std::vector<std::string> v;
    for (pugi::xml_node c = parent.child(tag); c; c = c.next_sibling(tag))
        if (std::string ex = attr(c, "expr"); !ex.empty())
            v.push_back(std::move(ex));
    return v;
}

void appendJsonString(std::string& out, const std::string& s)
{
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '\b': out += "\\b"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\f': out += "\\f"; break;
        case '\r': out += "\\r"; break;
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        default:
            if (c <= 0x1F) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}

void appendJsonArray(std::string& out, const std::vector<std::string>& v)
{
    out += '[';
    for (size_t i = 0; i < v.size(); ++i) {
        if (i)
            out += ',';
        appendJsonString(out, v[i]);
    }
    out += ']';
}

} // namespace

// --- FunctionParser ------------------------------------------------------------------------------

void FunctionParser::funcParse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
                               const std::string& className, const Zones* commentZones)
{
    if (begin >= end || m_functionExpr.empty())
        return;

    e.send(SCI_SETSEARCHFLAGS, kFlags);
    intptr_t targetStart = searchInTarget(e, m_functionExpr, begin, end);
    while (targetStart >= 0) {
        targetStart = e.send(SCI_GETTARGETSTART);
        const intptr_t targetEnd = e.send(SCI_GETTARGETEND);
        if (targetEnd > static_cast<intptr_t>(end))
            break;
        const size_t foundLen = static_cast<size_t>(targetEnd - targetStart);
        if (static_cast<size_t>(targetStart) + foundLen == end)
            break;

        FoundFunction fi;
        if (m_functionNameExprs.empty() && m_classNameExprs.empty()) {
            fi.name = genericText(e, targetStart, targetEnd);
            fi.pos = targetStart;
        } else {
            intptr_t foundPos = -1;
            if (!m_functionNameExprs.empty()) {
                fi.name = parseSubLevel(targetStart, targetEnd, m_functionNameExprs, foundPos, e);
                fi.pos = foundPos;
            }
            if (!className.empty()) {
                fi.className = className;
                fi.classPos = -1;
            } else if (!m_classNameExprs.empty()) {
                fi.className = parseSubLevel(targetStart, targetEnd, m_classNameExprs, foundPos, e);
                fi.classPos = foundPos;
            }
        }

        if (fi.pos != -1 || fi.classPos != -1) {
            if (!commentZones || (!isInZones(static_cast<size_t>(fi.pos), *commentZones)
                                  && !isInZones(static_cast<size_t>(fi.classPos), *commentZones)))
                out.push_back(std::move(fi));
        }

        begin = static_cast<size_t>(targetStart) + foundLen;
        targetStart = searchInTarget(e, m_functionExpr, begin, end);
    }
}

std::string FunctionParser::parseSubLevel(size_t begin, size_t end, std::vector<std::string> exprs,
                                          intptr_t& foundPos, const IEditor& e)
{
    if (begin >= end) {
        foundPos = -1;
        return {};
    }
    if (exprs.empty())
        return {};

    e.send(SCI_SETSEARCHFLAGS, kFlags);
    const intptr_t targetStart = searchInTarget(e, exprs[0], begin, end);
    if (targetStart < 0) {
        foundPos = -1;
        return {};
    }
    const intptr_t targetEnd = e.send(SCI_GETTARGETEND);
    if (exprs.size() >= 2) {
        exprs.erase(exprs.begin());
        return parseSubLevel(static_cast<size_t>(targetStart), static_cast<size_t>(targetEnd), std::move(exprs),
                             foundPos, e);
    }
    foundPos = targetStart;
    return genericText(e, targetStart, targetEnd);
}

void FunctionParser::getCommentZones(Zones& zones, size_t begin, size_t end, const IEditor& e)
{
    if (begin >= end || m_commentExpr.empty())
        return;

    e.send(SCI_SETSEARCHFLAGS, kFlags);
    intptr_t targetStart = searchInTarget(e, m_commentExpr, begin, end);
    while (targetStart >= 0) {
        targetStart = e.send(SCI_GETTARGETSTART);
        const intptr_t targetEnd = e.send(SCI_GETTARGETEND);
        if (targetEnd > static_cast<intptr_t>(end))
            break;
        zones.emplace_back(static_cast<size_t>(targetStart), static_cast<size_t>(targetEnd));
        if (targetEnd == static_cast<intptr_t>(end))
            break;
        begin = static_cast<size_t>(targetEnd);
        targetStart = searchInTarget(e, m_commentExpr, begin, end);
    }
}

bool FunctionParser::isInZones(size_t pos, const Zones& zones)
{
    for (const auto& z : zones)
        if (pos >= z.first && pos < z.second)
            return true;
    return false;
}

void FunctionParser::getInvertZones(Zones& dest, const Zones& source, size_t begin, size_t end)
{
    if (source.empty()) {
        dest.emplace_back(begin, end);
        return;
    }
    if (begin < source[0].first)
        dest.emplace_back(begin, source[0].first - 1);

    size_t i = 0;
    for (size_t len = source.size() - 1; i < len; ++i) {
        const size_t newBegin = source[i].second + 1;
        const size_t newEnd = source[i + 1].first - 1;
        if (newBegin < newEnd)
            dest.emplace_back(newBegin, newEnd);
    }
    const size_t lastBegin = source[i].second + 1;
    if (lastBegin < end)
        dest.emplace_back(lastBegin, end);
}

void FunctionUnitParser::parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
                               const std::string& className)
{
    Zones commentZones, nonComment;
    getCommentZones(commentZones, begin, end, e);
    getInvertZones(nonComment, commentZones, begin, end);
    for (const auto& z : nonComment)
        funcParse(out, z.first, z.second, e, className);
}

// --- FunctionZoneParser ----------------------------------------------------------------------------

size_t FunctionZoneParser::bodyClosePos(size_t begin, const std::string& open, const std::string& close,
                                        const Zones& commentZones, const IEditor& e)
{
    size_t cntOpen = 1;
    const auto docLen = static_cast<size_t>(e.send(SCI_GETLENGTH));
    if (begin >= docLen)
        return docLen;

    const std::string expr = "(" + open + "|" + close + ")";
    e.send(SCI_SETSEARCHFLAGS, kFlags);
    intptr_t targetStart = searchInTarget(e, expr, begin, docLen);
    intptr_t targetEnd = 0;
    do {
        if (targetStart >= 0) {
            targetEnd = e.send(SCI_GETTARGETEND);
            if (!isInZones(static_cast<size_t>(targetStart), commentZones)) {
                if (searchInTarget(e, open, static_cast<size_t>(targetStart), static_cast<size_t>(targetEnd)) >= 0)
                    ++cntOpen;
                else
                    --cntOpen;
            }
        } else {
            cntOpen = 0;
            targetEnd = static_cast<intptr_t>(begin);
        }
        targetStart = searchInTarget(e, expr, static_cast<size_t>(targetEnd), docLen);
    } while (cntOpen);
    return static_cast<size_t>(targetEnd);
}

void FunctionZoneParser::classParse(std::vector<FoundFunction>& out, Zones& scanned, const Zones& commentZones,
                                    size_t begin, size_t end, const IEditor& e)
{
    if (begin >= end)
        return;

    e.send(SCI_SETSEARCHFLAGS, kFlags);
    intptr_t targetStart = searchInTarget(e, m_rangeExpr, begin, end);
    while (targetStart >= 0) {
        intptr_t targetEnd = e.send(SCI_GETTARGETEND);
        intptr_t foundPos = 0;
        const std::string subClassName = parseSubLevel(static_cast<size_t>(targetStart),
                                                       static_cast<size_t>(targetEnd), m_classNameExprs, foundPos, e);
        if (!m_openSymbol.empty() && !m_closeSymbol.empty())
            targetEnd = static_cast<intptr_t>(
                bodyClosePos(static_cast<size_t>(targetEnd), m_openSymbol, m_closeSymbol, commentZones, e));
        if (targetEnd > static_cast<intptr_t>(end))
            break;

        scanned.emplace_back(static_cast<size_t>(targetStart), static_cast<size_t>(targetEnd));
        if (targetEnd == static_cast<intptr_t>(end))
            break;

        if (!isInZones(static_cast<size_t>(targetStart), commentZones))
            funcParse(out, static_cast<size_t>(targetStart), static_cast<size_t>(targetEnd), e, subClassName,
                      &commentZones);
        begin = static_cast<size_t>(targetEnd);
        e.send(SCI_SETSEARCHFLAGS, kFlags);
        targetStart = searchInTarget(e, m_rangeExpr, begin, end);
    }
}

void FunctionZoneParser::parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
                               const std::string&)
{
    Zones classZones, commentZones, nonComment;
    getCommentZones(commentZones, begin, end, e);
    getInvertZones(nonComment, commentZones, begin, end);
    for (const auto& z : nonComment)
        classParse(out, classZones, commentZones, z.first, z.second, e);
}

void FunctionMixParser::parse(std::vector<FoundFunction>& out, size_t begin, size_t end, const IEditor& e,
                              const std::string& className)
{
    Zones commentZones, scanned, nonScanned;
    getCommentZones(commentZones, begin, end, e);
    classParse(out, scanned, commentZones, begin, end, e);
    for (const auto& z : scanned) {
        Zones temp;
        classParse(out, temp, commentZones, z.first, z.second, e);
    }
    getInvertZones(nonScanned, scanned, begin, end);
    if (m_unitParser)
        for (const auto& z : nonScanned)
            m_unitParser->funcParse(out, z.first, z.second, e, className, &commentZones);
}

// --- FunctionParsersManager --------------------------------------------------------------------------

bool FunctionParsersManager::init(const std::string& userDir, const std::string& installedDir)
{
    m_userDir = userDir;
    m_installedDir = installedDir;
    m_langs = std::vector<Entry>(kLangCount);
    m_udls.clear();
    if (readOverrideMap(m_userDir))
        return true;
    if (m_installedDir != m_userDir && !m_installedDir.empty())
        return readOverrideMap(m_installedDir);
    return false;
}

bool FunctionParsersManager::readOverrideMap(const std::string& dir)
{
    if (dir.empty())
        return false;
    pugi::xml_document doc;
    if (!doc.load_file(pathFromUtf8(dir + "/overrideMap.xml").c_str()))
        return false;
    const pugi::xml_node root = doc.child("NotepadPlus").child("functionList");
    if (!root)
        return false;
    for (pugi::xml_node a = root.child("associationMap").child("association"); a; a = a.next_sibling("association")) {
        const std::string id = attr(a, "id");
        if (id.empty())
            continue;
        const int langId = a.attribute("langID").as_int(-1);
        const std::string udl = attr(a, "userDefinedLangName");
        if (langId >= 0 && langId < kLangCount)
            m_langs[static_cast<size_t>(langId)] = Entry{id, {}, nullptr, false};
        else if (!udl.empty())
            m_udls.push_back(Entry{id, udl, nullptr, false});
    }
    return true;
}

bool FunctionParsersManager::load(Entry& entry, const std::string& dir, LangType lang)
{
    if (dir.empty())
        return false;
    std::string file = entry.fileName;
    if (file.empty()) {
        if (lang == L_USER)
            return false;
        file = std::string(langInfo(lang).name) + ".xml";
    }
    pugi::xml_document doc;
    if (!doc.load_file(pathFromUtf8(dir + "/" + file).c_str(),
                       pugi::parse_cdata | pugi::parse_escapes | pugi::parse_eol))
        return false;
    const pugi::xml_node parserRoot = doc.child("NotepadPlus").child("functionList").child("parser");
    if (!parserRoot)
        return false;
    const std::string id = attr(parserRoot, "id");
    if (id.empty())
        return false;
    const std::string commentExpr = attr(parserRoot, "commentExpr");
    std::string displayName = attr(parserRoot, "displayName");
    if (displayName.empty())
        displayName = id;

    const pugi::xml_node classRange = parserRoot.child("classRange");
    const pugi::xml_node function = parserRoot.child("function");

    struct Zone {
        std::string mainExpr, open, close, functionExpr;
        std::vector<std::string> classNames, functionNames;
    };
    auto zoneParams = [](const pugi::xml_node& n) {
        Zone z;
        z.mainExpr = attr(n, "mainExpr");
        z.open = attr(n, "openSymbole");
        z.close = attr(n, "closeSymbole");
        z.classNames = exprList(n.child("className"), "nameExpr");
        const pugi::xml_node f = n.child("function");
        z.functionExpr = attr(f, "mainExpr");
        z.functionNames = exprList(f.child("functionName"), "funcNameExpr");
        return z;
    };
    auto unitParser = [&](const pugi::xml_node& n) {
        return std::make_unique<FunctionUnitParser>(id, displayName, commentExpr, attr(n, "mainExpr"),
                                                    exprList(n.child("functionName"), "nameExpr"),
                                                    exprList(n.child("className"), "nameExpr"));
    };

    if (classRange && function) {
        Zone z = zoneParams(classRange);
        entry.parser = std::make_unique<FunctionMixParser>(id, displayName, commentExpr, z.mainExpr, z.open, z.close,
                                                           z.classNames, z.functionExpr, z.functionNames,
                                                           unitParser(function));
    } else if (classRange) {
        Zone z = zoneParams(classRange);
        entry.parser = std::make_unique<FunctionZoneParser>(id, displayName, commentExpr, z.mainExpr, z.open,
                                                            z.close, z.classNames, z.functionExpr, z.functionNames);
    } else if (function) {
        entry.parser = unitParser(function);
    }
    return true;
}

FunctionParsersManager::Entry* FunctionParsersManager::entryFor(LangType lang, const std::string& udlName)
{
    if (lang == L_USER) {
        if (udlName.empty())
            return nullptr;
        for (Entry& u : m_udls)
            if (u.udlName == udlName)
                return &u;
        return nullptr;
    }
    if (lang < 0 || lang >= kLangCount)
        return nullptr;
    return &m_langs[static_cast<size_t>(lang)];
}

FunctionParser* FunctionParsersManager::parser(LangType lang, const std::string& udlName)
{
    if (lang == L_JS_EMBEDDED)
        lang = L_JAVASCRIPT;
    Entry* entry = entryFor(lang, udlName);
    if (!entry)
        return nullptr;
    if (entry->parser)
        return entry->parser.get();
    if (load(*entry, m_userDir, lang))
        return entry->parser.get();
    if (m_installedDir != m_userDir && load(*entry, m_installedDir, lang))
        return entry->parser.get();
    return nullptr;
}

bool FunctionParsersManager::parse(std::vector<FoundFunction>& out, const IEditor& e, LangType lang,
                                   const std::string& udlName)
{
    FunctionParser* p = parser(lang, udlName);
    if (!p)
        return false;
    p->parse(out, 0, static_cast<size_t>(e.send(SCI_GETLENGTH)), e);
    return true;
}

// --- JSON export -------------------------------------------------------------------------------------

std::string functionListJson(const std::vector<FoundFunction>& found, const std::string& rootName)
{
    std::vector<std::string> leaves;
    std::vector<std::pair<std::string, std::vector<std::string>>> nodes;
    for (const FoundFunction& f : found) {
        if (f.className.empty()) {
            leaves.push_back(f.name);
            continue;
        }
        auto it = nodes.begin();
        for (; it != nodes.end(); ++it)
            if (it->first == f.className)
                break;
        if (it == nodes.end())
            nodes.emplace_back(f.className, std::vector<std::string>{f.name});
        else
            it->second.push_back(f.name);
    }

    std::string out = "{";
    bool first = true;
    if (!leaves.empty()) {
        out += "\"leaves\":";
        appendJsonArray(out, leaves);
        first = false;
    }
    if (!nodes.empty()) {
        if (!first)
            out += ',';
        out += "\"nodes\":[";
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (i)
                out += ',';
            out += "{\"leaves\":";
            appendJsonArray(out, nodes[i].second);
            out += ",\"name\":";
            appendJsonString(out, nodes[i].first);
            out += '}';
        }
        out += ']';
        first = false;
    }
    if (!first)
        out += ',';
    out += "\"root\":";
    appendJsonString(out, rootName);
    out += '}';
    return out;
}

} // namespace npp::core
