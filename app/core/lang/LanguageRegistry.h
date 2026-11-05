// langs.xml model (upstream NppParameters::feedKeyWordsParameters, Lang).
#pragma once

#include "core/lang/LangType.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

struct LanguageDef {
    LangType                 type = L_TEXT;
    std::string              name;            // langs.xml name, e.g. "cpp"
    std::vector<std::string> extensions;      // lower case, without dot (incl. user extensions)
    std::string              extAttribute;    // "ext" as written in langs.xml
    std::string              commentLine, commentStart, commentEnd;
    int                      tabSettings = -1; // upstream encoding: -1 none, bit7 replace by space, low 7 bits size
    std::array<std::string, kKeywordClassCount> keywords; // by keyword class
};

class LanguageRegistry {
public:
    bool load(std::string_view utf8Path);
    bool loadFromString(std::string_view xml);
    // Writes the model back into the loaded document (unknown parts preserved).
    std::string saveToString() const;
    bool save(std::string_view utf8Path) const;
    LanguageDef* findMutable(LangType type);

    const LanguageDef* find(LangType type) const;
    const LanguageDef* findByName(std::string_view name) const;
    const std::vector<LanguageDef>& languages() const noexcept { return m_langs; }

    // Keyword list of a class ("" if none) - upstream NppParameters::getWordList.
    const std::string& wordList(LangType type, int keywordClass) const;

    // Language for a file name: extension match first (upstream also matches full names such as
    // "makefile" / "CMakeLists.txt"); L_TEXT if none. Extra user extensions (stylers.xml ext
    // attribute) can be given per language name.
    LangType languageForFile(std::string_view utf8Path) const;
    void addUserExtensions(LangType type, std::string_view spaceSeparatedExts);

private:
    std::vector<LanguageDef> m_langs;
    std::string m_source; // original document
};

} // namespace npp::core
