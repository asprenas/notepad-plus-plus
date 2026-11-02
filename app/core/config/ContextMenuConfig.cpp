#include "ContextMenuConfig.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <set>
#include <sstream>

namespace npp::core {

namespace {
const std::set<std::string> kKnown = {"id", "MenuEntryName", "MenuItemName", "PluginEntryName",
                                      "PluginCommandItemName", "FolderName", "ItemNameAs"};
}

bool ContextMenuConfig::loadFromString(std::string_view text, std::string_view tag)
{
    *this = ContextMenuConfig();
    rootTag = std::string(tag);
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return false;
    const pugi::xml_node root = doc.child("NotepadPlus").child(rootTag.c_str());
    if (!root)
        return false;
    for (pugi::xml_node n = root.child("Item"); n; n = n.next_sibling("Item")) {
        ContextMenuItem it;
        it.id = n.attribute("id").as_int(-1);
        it.menuEntryName = n.attribute("MenuEntryName").value();
        it.menuItemName = n.attribute("MenuItemName").value();
        it.pluginEntryName = n.attribute("PluginEntryName").value();
        it.pluginCommandItemName = n.attribute("PluginCommandItemName").value();
        it.folderName = n.attribute("FolderName").value();
        it.itemNameAs = n.attribute("ItemNameAs").value();
        for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute())
            if (!kKnown.count(a.name()))
                it.extraAttributes[a.name()] = a.value();
        items.push_back(std::move(it));
    }
    return true;
}

bool ContextMenuConfig::load(std::string_view utf8Path, std::string_view tag)
{
    const auto content = readFileContent(utf8Path);
    return content && loadFromString(*content, tag);
}

std::string ContextMenuConfig::saveToString() const
{
    pugi::xml_document doc;
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version").set_value("1.0");
    decl.append_attribute("encoding").set_value("UTF-8");
    pugi::xml_node root = doc.append_child("NotepadPlus").append_child(rootTag.c_str());
    for (const auto& it : items) {
        pugi::xml_node n = root.append_child("Item");
        auto put = [&n](const char* name, const std::string& v) {
            if (!v.empty())
                n.append_attribute(name).set_value(v.c_str());
        };
        put("FolderName", it.folderName);
        if (it.id >= 0)
            n.append_attribute("id").set_value(it.id);
        put("MenuEntryName", it.menuEntryName);
        put("MenuItemName", it.menuItemName);
        put("PluginEntryName", it.pluginEntryName);
        put("PluginCommandItemName", it.pluginCommandItemName);
        put("ItemNameAs", it.itemNameAs);
        for (const auto& [k, v] : it.extraAttributes)
            n.append_attribute(k.c_str()).set_value(v.c_str());
    }
    std::ostringstream os;
    doc.save(os, "    ", pugi::format_indent);
    return os.str();
}

bool ContextMenuConfig::save(std::string_view utf8Path) const
{
    return writeFileContent(utf8Path, saveToString());
}

} // namespace npp::core
