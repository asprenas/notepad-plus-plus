#include "XmlHelpers.h"

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <cstring>

namespace npp::core::xml {

bool getBool(const pugi::xml_node& n, const char* attr, bool fallback)
{
    const pugi::xml_attribute a = n.attribute(attr);
    if (!a)
        return fallback;
    if (std::strcmp(a.value(), "yes") == 0)
        return true;
    if (std::strcmp(a.value(), "no") == 0)
        return false;
    return fallback;
}

int getInt(const pugi::xml_node& n, const char* attr, int fallback)
{
    const pugi::xml_attribute a = n.attribute(attr);
    if (!a || !*a.value())
        return fallback;
    const auto v = parseInt(a.value());
    return v ? static_cast<int>(*v) : fallback;
}

std::string getString(const pugi::xml_node& n, const char* attr, std::string_view fallback)
{
    const pugi::xml_attribute a = n.attribute(attr);
    return a ? std::string(a.value()) : std::string(fallback);
}

std::string text(const pugi::xml_node& n, std::string_view fallback)
{
    const pugi::xml_node t = n.first_child();
    if (t && (t.type() == pugi::node_pcdata || t.type() == pugi::node_cdata))
        return t.value();
    return std::string(fallback);
}

bool textBool(const pugi::xml_node& n, bool fallback)
{
    const std::string v = text(n);
    if (v == "yes")
        return true;
    if (v == "no")
        return false;
    return fallback;
}

int textInt(const pugi::xml_node& n, int fallback)
{
    const auto v = parseInt(text(n));
    return v ? static_cast<int>(*v) : fallback;
}

namespace {
pugi::xml_attribute ensureAttr(pugi::xml_node n, const char* attr)
{
    pugi::xml_attribute a = n.attribute(attr);
    return a ? a : n.append_attribute(attr);
}
} // namespace

void setBool(pugi::xml_node n, const char* attr, bool v) { ensureAttr(n, attr).set_value(v ? "yes" : "no"); }
void setInt(pugi::xml_node n, const char* attr, long long v) { ensureAttr(n, attr).set_value(v); }
void setString(pugi::xml_node n, const char* attr, std::string_view v)
{
    ensureAttr(n, attr).set_value(std::string(v).c_str());
}

void setText(pugi::xml_node n, std::string_view v)
{
    pugi::xml_node t = n.first_child();
    if (t && (t.type() == pugi::node_pcdata || t.type() == pugi::node_cdata)) {
        t.set_value(std::string(v).c_str());
        return;
    }
    if (v.empty())
        return;
    t = n.prepend_child(pugi::node_pcdata);
    t.set_value(std::string(v).c_str());
}

pugi::xml_node childWithAttr(pugi::xml_node parent, const char* tag, const char* attrName, std::string_view attrValue,
                             bool create)
{
    for (pugi::xml_node c = parent.child(tag); c; c = c.next_sibling(tag))
        if (attrValue == c.attribute(attrName).value())
            return c;
    if (!create)
        return {};
    pugi::xml_node c = parent.append_child(tag);
    c.append_attribute(attrName).set_value(std::string(attrValue).c_str());
    return c;
}

bool loadFile(pugi::xml_document& doc, std::string_view utf8Path)
{
    return static_cast<bool>(doc.load_file(pathFromUtf8(utf8Path).c_str(), kParseOptions));
}

bool saveFile(const pugi::xml_document& doc, std::string_view utf8Path)
{
    // Same formatting as upstream NppXml::saveFile (4-space indent).
    return doc.save_file(pathFromUtf8(utf8Path).c_str(), "    ", pugi::format_indent | pugi::format_save_file_text);
}

} // namespace npp::core::xml
