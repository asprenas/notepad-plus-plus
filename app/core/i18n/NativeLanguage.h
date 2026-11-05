// Localization strings from nativeLang/*.xml (port of the query side of upstream
// localization.* NativeLangSpeaker). The UI applies the returned strings to its widgets
// using the same numeric control / command IDs as upstream.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

struct DialogStrings {
    std::string                             title;
    std::map<int, std::string>              items;      // control id -> text
    std::map<int, std::vector<std::string>> comboBoxes; // combo id -> entries
};

struct LanguageFileInfo {
    std::string name;     // Native-Langue@name, e.g. "Korean"
    std::string fileName; // e.g. "korean.xml"
    std::string path;     // full UTF-8 path
};

class NativeLanguage {
public:
    NativeLanguage();
    ~NativeLanguage();
    NativeLanguage(NativeLanguage&&) noexcept;
    NativeLanguage& operator=(NativeLanguage&&) noexcept;

    // Upstream semantics: english.xml is not "loaded" unless loadIfEnglish (the built-in
    // English strings are used instead).
    bool loadFile(std::string_view utf8Path, bool loadIfEnglish = false);
    bool loadFromString(std::string_view xml, bool loadIfEnglish = false);
    void clear();

    bool isLoaded() const noexcept;
    bool isRTL() const noexcept { return m_rtl; }
    bool isEditZoneRTL() const noexcept { return m_editZoneRtl; }
    const std::string& fileName() const noexcept { return m_fileName; }
    const std::string& languageName() const noexcept { return m_name; }

    // Menu
    std::string mainMenuEntry(std::string_view menuId, std::string_view fallback = {}) const;  // Menu/Main/Entries
    std::string subMenuEntry(std::string_view subMenuId, std::string_view fallback = {}) const; // Menu/Main/SubEntries
    std::string menuCommand(int commandId, std::string_view fallback = {}, bool removeMarkAnd = false) const;
    std::string shortcutName(int commandId) const; // Dialog/ShortcutMapper/MainCommandNames

    // Dialogs: <Dialog> subtree searched recursively for tag (upstream searchDlgNode).
    std::optional<DialogStrings> dialog(std::string_view tag, std::string_view titleAttribute = "title") const;
    std::string dialogAttribute(std::string_view tag, std::string_view attribute, std::string_view fallback = {}) const;

    // Message boxes: <MessageBox><tag title=".." message=".."/>; $INT_REPLACE$ / $STR_REPLACE$
    // substituted. Falls back to the given defaults; empty or "0" title -> "common-error" string.
    struct MessageText { std::string title; std::string message; };
    MessageText messageBox(std::string_view tag, std::string_view defaultMessage, std::string_view defaultTitle,
                           int intInfo = 0, std::string_view strInfo = {}) const;

    // <MiscStrings><strId value=".."/>
    std::string localizedString(std::string_view strId, std::string_view fallback) const;

    // Generic: Item id -> name under a node path from <Native-Langue>, e.g.
    // {"Menu","TabBar"} (idAttribute "CMDID") or {"ProjectManager","Menus","WorkspaceMenu"}.
    std::map<int, std::string> itemNames(const std::vector<std::string>& path, std::string_view idAttribute = "id") const;
    std::string itemName(const std::vector<std::string>& path, int id, std::string_view fallback = {},
                         std::string_view idAttribute = "id") const;
    // Other attribute of an Item (e.g. TabBar alternativeName); empty if none.
    std::string itemAttribute(const std::vector<std::string>& path, int id, std::string_view attribute,
                              std::string_view idAttribute = "id") const;
    // Attribute of the node at path (upstream getAttrNameStr).
    std::string attribute(const std::vector<std::string>& path, std::string_view attribute,
                          std::string_view fallback = {}) const;

    // Removes accelerator marks: "(&X)" CJK suffix, single '&', "&&" -> '&', trailing "..."
    // (upstream purifyMenuString).
    static std::string purifyMenuString(std::string s);

    // Scans a nativeLang directory (upstream LocalizationSwitcher).
    static std::vector<LanguageFileInfo> availableLanguages(std::string_view utf8Dir);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    bool        m_rtl = false;
    bool        m_editZoneRtl = false;
    std::string m_fileName;
    std::string m_name;
};

} // namespace npp::core
