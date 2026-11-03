// User Defined Languages (UDL): userDefineLang.xml and userDefineLangs/*.xml
// (upstream UserLangContainer, feedUserLang / feedUserSettings / feedUserKeywordList /
// feedUserStyles, GlobalMappers, ScintillaEditView::setUserLexer).
#pragma once

#include "core/lang/StyleRegistry.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace npp::core {

inline constexpr int kUdlKeywordListCount = 28; // SCE_USER_KWLIST_TOTAL
inline constexpr int kUdlKeywordGroups = 8;     // SCE_USER_TOTAL_KEYWORD_GROUPS
inline constexpr int kUdlStyleCount = 24;       // SCE_USER_STYLE_TOTAL_STYLES

struct UserLanguage {
    std::string name, ext, udlVersion;
    std::string sourceFile;           // file it was loaded from
    bool darkModeTheme = false;
    bool caseIgnored = false;
    bool allowFoldOfComments = false;
    bool foldCompact = false;
    int  forcePureLC = 0;
    int  decimalSeparator = 0;
    std::array<bool, kUdlKeywordGroups> isPrefix{};
    std::array<std::string, kUdlKeywordListCount> keywordLists;
    std::vector<Style> styles;        // styleId = SCE_USER_STYLE_* (0..23)

    const Style* style(int id) const;
};

// Lexer configuration derived from a UDL, ready to be sent to the Scintilla "user" lexer
// (upstream setUserLexer): SCI_SETPROPERTY pairs then SCI_SETKEYWORDS lists in order.
struct UserLexerConfig {
    std::vector<std::pair<std::string, std::string>> properties;
    std::vector<std::string> keywordSets; // SCI_SETKEYWORDS 0..n-1
};
UserLexerConfig buildUserLexerConfig(const UserLanguage& udl);

// Serializes UDLs in the UDL 2.1 format (upstream UDL export / userDefineLang.xml).
std::string userLanguagesToXml(const std::vector<UserLanguage>& udls);
bool saveUserLanguages(const std::vector<UserLanguage>& udls, std::string_view utf8Path);

class UserLanguageRegistry {
public:
    // Adds the <UserLang> entries of one file. Returns the number added.
    int loadFile(std::string_view utf8Path);
    int loadFromString(std::string_view xml, std::string_view sourceFile = {});
    // userDefineLang.xml + every *.xml of the userDefineLangs directory (sorted by name).
    int loadAll(std::string_view userDefineLangXml, std::string_view userDefineLangsDir);

    const std::vector<UserLanguage>& languages() const noexcept { return m_langs; }
    const UserLanguage* find(std::string_view name) const;
    // UDL whose ext list contains the file extension (ASCII case-insensitive), or nullptr.
    const UserLanguage* findForFile(std::string_view utf8Path) const;

    // Keyword list name <-> SCE_USER_KWLIST_* (GlobalMappers::keywordIdMapper)
    static int keywordListId(std::string_view name);
    // Style name <-> SCE_USER_STYLE_* (GlobalMappers::styleIdMapper)
    static int styleId(std::string_view name);

private:
    std::vector<UserLanguage> m_langs;
};

} // namespace npp::core
