// contextMenu.xml / tabContextMenu.xml (upstream getContextMenuFromXmlTree).
// Items reference commands by id, by menu entry/item name, by plugin command name, or are
// separators (id="0"); FolderName groups items into sub menus, ItemNameAs renames.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

struct ContextMenuItem {
    int         id = -1;                 // command id, 0 = separator, -1 = not given
    std::string menuEntryName;           // MenuEntryName  (main menu entry, e.g. "Edit")
    std::string menuItemName;            // MenuItemName   (item text in that menu)
    std::string pluginEntryName;         // PluginEntryName
    std::string pluginCommandItemName;   // PluginCommandItemName
    std::string folderName;              // FolderName (sub menu)
    std::string itemNameAs;              // ItemNameAs (display name override)
    std::map<std::string, std::string> extraAttributes;

    bool isSeparator() const noexcept { return id == 0; }
};

struct ContextMenuConfig {
    std::string rootTag = "ScintillaContextMenu"; // or "TabContextMenu"
    std::vector<ContextMenuItem> items;

    bool load(std::string_view utf8Path, std::string_view rootTag = "ScintillaContextMenu");
    bool loadFromString(std::string_view xml, std::string_view rootTag = "ScintillaContextMenu");
    bool save(std::string_view utf8Path) const;
    std::string saveToString() const;
};

} // namespace npp::core
