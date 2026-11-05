// pugixml helpers following upstream Notepad++ conventions (booleans as "yes"/"no").
#pragma once

#include <pugixml.hpp>

#include <string>
#include <string_view>

namespace npp::core::xml {

// "yes" -> true, "no" -> false, anything else (or missing) -> fallback (upstream getBoolAttribute).
bool getBool(const pugi::xml_node& n, const char* attr, bool fallback);
int getInt(const pugi::xml_node& n, const char* attr, int fallback);
std::string getString(const pugi::xml_node& n, const char* attr, std::string_view fallback = {});

// Element text (first child PCDATA), upstream getBoolChildTextNode etc.
bool textBool(const pugi::xml_node& n, bool fallback);
int textInt(const pugi::xml_node& n, int fallback);
std::string text(const pugi::xml_node& n, std::string_view fallback = {});

void setBool(pugi::xml_node n, const char* attr, bool v);
void setInt(pugi::xml_node n, const char* attr, long long v);
void setString(pugi::xml_node n, const char* attr, std::string_view v);
// Replaces the element text, keeping attributes and child elements.
void setText(pugi::xml_node n, std::string_view v);
inline void setTextBool(pugi::xml_node n, bool v) { setText(n, v ? "yes" : "no"); }

// Child element <tag attrName="attrValue">, optionally created at the end.
pugi::xml_node childWithAttr(pugi::xml_node parent, const char* tag, const char* attrName, std::string_view attrValue,
                             bool create = false);

// Parse / save options matching upstream NppXml::loadFile / saveFile.
inline constexpr unsigned kParseOptions = pugi::parse_default | pugi::parse_comments | pugi::parse_declaration;
bool loadFile(pugi::xml_document& doc, std::string_view utf8Path);
bool saveFile(const pugi::xml_document& doc, std::string_view utf8Path);

} // namespace npp::core::xml
