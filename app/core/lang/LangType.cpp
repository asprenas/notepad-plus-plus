#include "LangType.h"

#include "core/util/StringUtil.h"

#include <array>
#include <string>

namespace npp::core {

namespace {

const std::array<LangInfo, kLangCount>& table()
{
    static const std::array<LangInfo, kLangCount> kTable = {{
#define NPP_LANG(e, name, shortName, longName, lexer) {e, name, shortName, longName, lexer},
#include "core/lang/LangTypes.inc"
#undef NPP_LANG
    }};
    return kTable;
}

} // namespace

const LangInfo& langInfo(LangType type)
{
    const int i = (type >= 0 && type < kLangCount) ? type : L_TEXT;
    return table()[static_cast<size_t>(i)];
}

std::optional<LangType> langTypeFromName(std::string_view name)
{
    for (const auto& li : table())
        if (iequalsAscii(li.name, name))
            return li.type;
    return std::nullopt;
}

std::optional<LangType> langTypeFromShortName(std::string_view shortName)
{
    if (iequalsAscii(shortName, "None (Normal Text)"))
        return L_TEXT;
    for (const auto& li : table())
        if (iequalsAscii(li.shortName, shortName))
            return li.type;
    return std::nullopt;
}

int keywordClassFromName(std::string_view n)
{
    static const char* const kNames[kKeywordClassCount] = {
        "instre1", "instre2", "type1", "type2", "type3", "type4", "type5", "type6", "type7",
        "substyle1", "substyle2", "substyle3", "substyle4", "substyle5", "substyle6", "substyle7", "substyle8"};
    for (int i = 0; i < kKeywordClassCount; ++i)
        if (n == kNames[i])
            return i;
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '8')
        return n[0] - '0';
    return -1;
}

LangType detectLanguageFromTextBeginning(std::string_view data)
{
    if (data.size() <= 3)
        return L_TEXT;
    size_t i = 0;
    const auto b = [&data](size_t k) { return static_cast<unsigned char>(data[k]); };
    if ((b(0) == 0xEF && b(1) == 0xBB && b(2) == 0xBF) || (b(0) == 0xFE && b(1) == 0xFF && b(2) == 0x00)
        || (b(0) == 0xFF && b(1) == 0xFE && b(2) == 0x00))
        i += 3;
    while (i < data.size() && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r'))
        ++i;
    if (i == data.size())
        return L_TEXT;

    std::string line(data.substr(i, std::min<size_t>(40, data.size() - i)));
    if (const size_t eol = line.find_first_of("\r\n"); eol != std::string::npos)
        line.resize(eol);

    if (line.rfind("#!", 0) == 0) {
        static const std::pair<const char*, LangType> kShebang[] = {
            {"sh", L_BASH}, {"python", L_PYTHON}, {"perl", L_PERL}, {"php", L_PHP}, {"ruby", L_RUBY}, {"node", L_JAVASCRIPT}};
        for (const auto& [pattern, lang] : kShebang)
            if (line.find(pattern) != std::string::npos)
                return lang;
        return L_TEXT;
    }
    static const std::pair<const char*, LangType> kFirstLine[] = {
        {"<?xml", L_XML}, {"<?php", L_PHP}, {"<html", L_HTML}, {"<!DOCTYPE html", L_HTML}, {"<?", L_PHP}};
    for (const auto& [pattern, lang] : kFirstLine)
        if (line.rfind(pattern, 0) == 0)
            return lang;
    return L_TEXT;
}

} // namespace npp::core
