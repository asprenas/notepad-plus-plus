#include "GuiConfig.h"

#include "core/config/XmlHelpers.h"

#include <pugixml.hpp>

#include <string_view>

namespace npp::core {

namespace {

pugi::xml_node guiNode(const pugi::xml_node& root, const char* name)
{
    return xml::childWithAttr(root, "GUIConfig", "name", name, false);
}

pugi::xml_node guiNodeForWrite(pugi::xml_node root, const char* name)
{
    return xml::childWithAttr(root, "GUIConfig", "name", name, true);
}

bool showHide(const pugi::xml_node& n, const char* attr, bool fallback)
{
    const std::string v = xml::getString(n, attr);
    if (v == "show") return true;
    if (v == "hide") return false;
    return fallback;
}

} // namespace

int GuiConfig::fileAutoDetectionFromString(const std::string& v)
{
    if (v == "yesOld")            return cdEnabledOld;
    if (v == "autoOld")           return cdEnabledOld | cdAutoUpdate;
    if (v == "Update2EndOld")     return cdEnabledOld | cdGo2end;
    if (v == "autoUpdate2EndOld") return cdEnabledOld | cdAutoUpdate | cdGo2end;
    if (v == "yes")               return cdEnabledNew;
    if (v == "auto")              return cdEnabledNew | cdAutoUpdate;
    if (v == "Update2End")        return cdEnabledNew | cdGo2end;
    if (v == "autoUpdate2End")    return cdEnabledNew | cdAutoUpdate | cdGo2end;
    return cdDisabled;
}

std::string GuiConfig::fileAutoDetectionToString(int f)
{
    const bool au = f & cdAutoUpdate, end = f & cdGo2end;
    if (f & cdEnabledOld)
        return au && end ? "autoUpdate2EndOld" : au ? "autoOld" : end ? "Update2EndOld" : "yesOld";
    if (f & cdEnabledNew)
        return au && end ? "autoUpdate2End" : au ? "auto" : end ? "Update2End" : "yes";
    return "no";
}

void GuiConfig::load(const pugi::xml_node& root)
{
#define NPP_BOOL_ATTR(m, node, attr, def) m = xml::getBool(guiNode(root, node), attr, m);
#define NPP_SHOWHIDE_ATTR(m, node, attr, def) m = showHide(guiNode(root, node), attr, m);
#define NPP_INT_ATTR(m, node, attr, def) m = xml::getInt(guiNode(root, node), attr, m);
#define NPP_STR_ATTR(m, node, attr, def) m = xml::getString(guiNode(root, node), attr, m);
#define NPP_BOOL_TEXT(m, node, def) m = xml::textBool(guiNode(root, node), m);
#define NPP_INT_TEXT(m, node, def) m = xml::textInt(guiNode(root, node), m);
#define NPP_STR_TEXT(m, node, def) if (pugi::xml_node n_ = guiNode(root, node)) m = xml::text(n_, m);
#include "core/config/GuiConfigFields.inc"
#undef NPP_BOOL_ATTR
#undef NPP_SHOWHIDE_ATTR
#undef NPP_INT_ATTR
#undef NPP_STR_ATTR
#undef NPP_BOOL_TEXT
#undef NPP_INT_TEXT
#undef NPP_STR_TEXT

    if (const auto n = guiNode(root, "StatusBar"))
        statusBarShow = xml::text(n) != "hide";
    if (const auto n = guiNode(root, "MenuBar"))
        menuBarShow = xml::text(n) != "hide";
    if (const auto n = guiNode(root, "ScintillaViewsSplitter"))
        splitterVertical = xml::text(n) != "horizontal";
    if (const auto n = guiNode(root, "UserDefineDlg")) {
        userDefineDlgDocked = xml::getString(n, "position", "docked") == "docked";
        userDefineDlgShow = xml::text(n) == "show";
    }
    if (const auto n = guiNode(root, "Auto-detection"))
        fileAutoDetection = fileAutoDetectionFromString(xml::text(n));
    if (const auto n = guiNode(root, "ColorPickerCustomColors"))
        for (size_t i = 0; i < colorPickerCustomColors.size(); ++i)
            colorPickerCustomColors[i] = xml::getInt(n, ("color" + std::to_string(i)).c_str(), colorPickerCustomColors[i]);
    if (const auto n = guiNode(root, "auto-insert")) {
        autoInsertUserPairs.clear();
        for (pugi::xml_node p = n.child("UserDefinePair"); p; p = p.next_sibling("UserDefinePair")) {
            const int open = p.attribute("open").as_int(0), close = p.attribute("close").as_int(0);
            if (open > 0 && open < 0x80 && close > 0 && close < 0x80)
                autoInsertUserPairs.emplace_back(static_cast<char>(open), static_cast<char>(close));
        }
    }
}

void GuiConfig::save(pugi::xml_node root) const
{
#define NPP_BOOL_ATTR(m, node, attr, def) xml::setBool(guiNodeForWrite(root, node), attr, m);
#define NPP_SHOWHIDE_ATTR(m, node, attr, def) xml::setString(guiNodeForWrite(root, node), attr, m ? "show" : "hide");
#define NPP_INT_ATTR(m, node, attr, def) xml::setInt(guiNodeForWrite(root, node), attr, m);
#define NPP_STR_ATTR(m, node, attr, def) xml::setString(guiNodeForWrite(root, node), attr, m);
#define NPP_BOOL_TEXT(m, node, def) xml::setTextBool(guiNodeForWrite(root, node), m);
#define NPP_INT_TEXT(m, node, def) xml::setText(guiNodeForWrite(root, node), std::to_string(m));
#define NPP_STR_TEXT(m, node, def) xml::setText(guiNodeForWrite(root, node), m);
#include "core/config/GuiConfigFields.inc"
#undef NPP_BOOL_ATTR
#undef NPP_SHOWHIDE_ATTR
#undef NPP_INT_ATTR
#undef NPP_STR_ATTR
#undef NPP_BOOL_TEXT
#undef NPP_INT_TEXT
#undef NPP_STR_TEXT

    xml::setText(guiNodeForWrite(root, "StatusBar"), statusBarShow ? "show" : "hide");
    xml::setText(guiNodeForWrite(root, "MenuBar"), menuBarShow ? "show" : "hide");
    xml::setText(guiNodeForWrite(root, "ScintillaViewsSplitter"), splitterVertical ? "vertical" : "horizontal");
    {
        pugi::xml_node n = guiNodeForWrite(root, "UserDefineDlg");
        xml::setString(n, "position", userDefineDlgDocked ? "docked" : "undocked");
        xml::setText(n, userDefineDlgShow ? "show" : "hide");
    }
    xml::setText(guiNodeForWrite(root, "Auto-detection"), fileAutoDetectionToString(fileAutoDetection));
    {
        pugi::xml_node n = guiNodeForWrite(root, "ColorPickerCustomColors");
        for (size_t i = 0; i < colorPickerCustomColors.size(); ++i)
            xml::setInt(n, ("color" + std::to_string(i)).c_str(), colorPickerCustomColors[i]);
    }
    {
        pugi::xml_node n = guiNodeForWrite(root, "auto-insert");
        while (pugi::xml_node p = n.child("UserDefinePair"))
            n.remove_child(p);
        for (const auto& [open, close] : autoInsertUserPairs) {
            pugi::xml_node p = n.append_child("UserDefinePair");
            p.append_attribute("open").set_value(static_cast<int>(open));
            p.append_attribute("close").set_value(static_cast<int>(close));
        }
    }
}

} // namespace npp::core
