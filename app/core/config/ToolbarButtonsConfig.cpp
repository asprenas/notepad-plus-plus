#include "ToolbarButtonsConfig.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <cstring>

namespace npp::core {

bool ToolbarButtonsConfig::loadFromString(std::string_view text)
{
    *this = ToolbarButtonsConfig();
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return false;
    const pugi::xml_node root = doc.child("NotepadPlus");
    if (!root)
        return false;
    const pugi::xml_node buttons = root.child("ToolbarButtons");
    if (const pugi::xml_node stdNode = buttons.child("Standard")) {
        standardHideAll = std::strcmp(stdNode.attribute("hideAll").value(), "yes") == 0;
        for (pugi::xml_node b = stdNode.child("Button"); b; b = b.next_sibling("Button")) {
            const int id = b.attribute("id").as_int(-1), index = b.attribute("index").as_int(-1);
            if (id > -1 && index > -1 && std::strcmp(b.attribute("hide").value(), "yes") == 0)
                hiddenStandard.insert({index, id});
        }
    }
    if (const pugi::xml_node plugin = buttons.child("Plugin")) {
        pluginHideAll = std::strcmp(plugin.attribute("hideAll").value(), "yes") == 0;
        for (pugi::xml_node b = plugin.child("Button"); b; b = b.next_sibling("Button"))
            pluginHidden.push_back(std::strcmp(b.attribute("hide").value(), "yes") == 0);
    }
    if (const pugi::xml_node icons = root.child("ToolBarIcons")) {
        hasIconFolder = true;
        iconFolderName = icons.attribute("icoFolderName").value();
    }
    return true;
}

bool ToolbarButtonsConfig::load(std::string_view utf8Path)
{
    const auto content = readFileContent(utf8Path);
    return content && loadFromString(*content);
}

} // namespace npp::core
