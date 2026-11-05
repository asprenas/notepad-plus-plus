#include "NativeLanguage.h"

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cstring>

namespace npp::core {

struct NativeLanguage::Impl {
    pugi::xml_document doc;
    pugi::xml_node     root; // <Native-Langue>, empty when not loaded
};

NativeLanguage::NativeLanguage() : d(std::make_unique<Impl>()) {}
NativeLanguage::~NativeLanguage() = default;
NativeLanguage::NativeLanguage(NativeLanguage&&) noexcept = default;
NativeLanguage& NativeLanguage::operator=(NativeLanguage&&) noexcept = default;

namespace {

constexpr unsigned kParseOptions = pugi::parse_cdata | pugi::parse_escapes | pugi::parse_eol | pugi::parse_comments;

pugi::xml_node findRecursive(pugi::xml_node node, const char* tag)
{
    if (pugi::xml_node direct = node.child(tag))
        return direct;
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element)
            continue;
        if (pugi::xml_node found = findRecursive(c, tag))
            return found;
    }
    return {};
}

pugi::xml_node descend(pugi::xml_node node, const std::vector<std::string>& path)
{
    for (const auto& p : path) {
        if (!node)
            return {};
        node = node.child(p.c_str());
    }
    return node;
}

std::string orFallback(const char* v, std::string_view fallback)
{
    return (v && *v) ? std::string(v) : std::string(fallback);
}

} // namespace

void NativeLanguage::clear()
{
    d->doc.reset();
    d->root = {};
    m_rtl = m_editZoneRtl = false;
    m_fileName.clear();
    m_name.clear();
}

bool NativeLanguage::loadFromString(std::string_view xml, bool loadIfEnglish)
{
    clear();
    if (!d->doc.load_buffer(xml.data(), xml.size(), kParseOptions))
        return false;
    pugi::xml_node lang = d->doc.child("NotepadPlus").child("Native-Langue");
    if (!lang)
        return false;

    if (const pugi::xml_attribute rtl = lang.attribute("RTL")) {
        m_rtl = std::strcmp(rtl.value(), "yes") == 0;
        if (m_rtl) {
            const pugi::xml_attribute ez = lang.attribute("editZoneRTL");
            m_editZoneRtl = ez ? std::strcmp(ez.value(), "no") != 0 : true;
        }
    }
    m_fileName = lang.attribute("filename").value();
    m_name = lang.attribute("name").value();
    if (!loadIfEnglish && iequalsAscii(m_fileName, "english.xml"))
        return false; // upstream keeps the built-in English strings
    d->root = lang;
    return true;
}

bool NativeLanguage::loadFile(std::string_view utf8Path, bool loadIfEnglish)
{
    const auto content = readFileContent(utf8Path);
    if (!content) {
        clear();
        return false;
    }
    return loadFromString(*content, loadIfEnglish);
}

bool NativeLanguage::isLoaded() const noexcept
{
    return static_cast<bool>(d->root);
}

std::string NativeLanguage::mainMenuEntry(std::string_view menuId, std::string_view fallback) const
{
    const pugi::xml_node entries = d->root.child("Menu").child("Main").child("Entries");
    for (pugi::xml_node it = entries.child("Item"); it; it = it.next_sibling("Item"))
        if (menuId == it.attribute("menuId").value())
            return orFallback(it.attribute("name").value(), fallback);
    return std::string(fallback);
}

std::string NativeLanguage::subMenuEntry(std::string_view subMenuId, std::string_view fallback) const
{
    const pugi::xml_node entries = d->root.child("Menu").child("Main").child("SubEntries");
    for (pugi::xml_node it = entries.child("Item"); it; it = it.next_sibling("Item"))
        if (subMenuId == it.attribute("subMenuId").value())
            return orFallback(it.attribute("name").value(), fallback);
    return std::string(fallback);
}

std::string NativeLanguage::menuCommand(int commandId, std::string_view fallback, bool removeMarkAnd) const
{
    const pugi::xml_node commands = d->root.child("Menu").child("Main").child("Commands");
    for (pugi::xml_node it = commands.child("Item"); it; it = it.next_sibling("Item")) {
        if (it.attribute("id").as_int(0) == commandId) {
            if (const char* name = it.attribute("name").value(); *name)
                return removeMarkAnd ? purifyMenuString(name) : std::string(name);
        }
    }
    return std::string(fallback);
}

std::string NativeLanguage::shortcutName(int commandId) const
{
    return itemName({"Dialog", "ShortcutMapper", "MainCommandNames"}, commandId);
}

std::optional<DialogStrings> NativeLanguage::dialog(std::string_view tag, std::string_view titleAttribute) const
{
    const pugi::xml_node dlgRoot = d->root.child("Dialog");
    if (!dlgRoot)
        return std::nullopt;
    const pugi::xml_node dlg = findRecursive(dlgRoot, std::string(tag).c_str());
    if (!dlg)
        return std::nullopt;

    DialogStrings out;
    out.title = dlg.attribute(std::string(titleAttribute.empty() ? "title" : titleAttribute).c_str()).value();
    for (pugi::xml_node it = dlg.child("Item"); it; it = it.next_sibling("Item")) {
        const int id = it.attribute("id").as_int(-1);
        const char* name = it.attribute("name").value();
        if (id >= 0 && *name)
            out.items[id] = name;
    }
    for (pugi::xml_node combo = dlg.child("ComboBox"); combo; combo = combo.next_sibling("ComboBox")) {
        auto& entries = out.comboBoxes[combo.attribute("id").as_int(-1)];
        for (pugi::xml_node e = combo.child("Element"); e; e = e.next_sibling("Element"))
            entries.emplace_back(e.attribute("name").value());
    }
    return out;
}

std::string NativeLanguage::dialogAttribute(std::string_view tag, std::string_view attribute, std::string_view fallback) const
{
    const pugi::xml_node dlgRoot = d->root.child("Dialog");
    if (!dlgRoot)
        return std::string(fallback);
    const pugi::xml_node dlg = findRecursive(dlgRoot, std::string(tag).c_str());
    return orFallback(dlg.attribute(std::string(attribute).c_str()).value(), fallback);
}

NativeLanguage::MessageText NativeLanguage::messageBox(std::string_view tag, std::string_view defaultMessage,
                                                       std::string_view defaultTitle, int intInfo,
                                                       std::string_view strInfo) const
{
    MessageText t{std::string(defaultTitle), std::string(defaultMessage)};
    if (const pugi::xml_node boxRoot = d->root.child("MessageBox")) {
        const pugi::xml_node box = findRecursive(boxRoot, std::string(tag).c_str());
        const char* title = box.attribute("title").value();
        const char* msg = box.attribute("message").value();
        if (*title && *msg) {
            t.title = title;
            t.message = msg;
        }
    }
    const std::string n = std::to_string(intInfo);
    t.title = replaceAll(t.title, "$INT_REPLACE$", n);
    t.message = replaceAll(t.message, "$INT_REPLACE$", n);
    if (!strInfo.empty()) {
        t.title = replaceAll(t.title, "$STR_REPLACE$", strInfo);
        t.message = replaceAll(t.message, "$STR_REPLACE$", strInfo);
    }
    if (t.title.empty() || t.title == "0")
        t.title = localizedString("common-error", "Error");
    return t;
}

std::string NativeLanguage::localizedString(std::string_view strId, std::string_view fallback) const
{
    if (strId.empty())
        return std::string(fallback);
    const pugi::xml_node node = d->root.child("MiscStrings").child(std::string(strId).c_str());
    return orFallback(node.attribute("value").value(), fallback);
}

std::map<int, std::string> NativeLanguage::itemNames(const std::vector<std::string>& path,
                                                     std::string_view idAttribute) const
{
    std::map<int, std::string> out;
    const pugi::xml_node node = descend(d->root, path);
    const std::string idAttr(idAttribute);
    for (pugi::xml_node it = node.child("Item"); it; it = it.next_sibling("Item")) {
        const int id = it.attribute(idAttr.c_str()).as_int(-1);
        const char* name = it.attribute("name").value();
        if (id >= 0 && *name)
            out.emplace(id, name);
    }
    return out;
}

std::string NativeLanguage::itemName(const std::vector<std::string>& path, int id, std::string_view fallback,
                                     std::string_view idAttribute) const
{
    const auto names = itemNames(path, idAttribute);
    const auto it = names.find(id);
    return it != names.end() ? it->second : std::string(fallback);
}

std::string NativeLanguage::itemAttribute(const std::vector<std::string>& path, int id, std::string_view attribute,
                                          std::string_view idAttribute) const
{
    const pugi::xml_node node = descend(d->root, path);
    const std::string idAttr(idAttribute), attr(attribute);
    for (pugi::xml_node it = node.child("Item"); it; it = it.next_sibling("Item"))
        if (it.attribute(idAttr.c_str()).as_int(-1) == id)
            return it.attribute(attr.c_str()).value();
    return {};
}

std::string NativeLanguage::attribute(const std::vector<std::string>& path, std::string_view attribute,
                                      std::string_view fallback) const
{
    const pugi::xml_node node = descend(d->root, path);
    return orFallback(node.attribute(std::string(attribute.empty() ? "name" : attribute).c_str()).value(), fallback);
}

std::string NativeLanguage::purifyMenuString(std::string s)
{
    // "(&X)" accelerator suffix used by CJK translations
    if (const size_t pos = s.find("(&"); pos != std::string::npos && pos + 3 < s.size() && s[pos + 3] == ')')
        s.erase(pos, 4);
    // '&' removed, "&&" -> '&'
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
        if (s[static_cast<size_t>(i)] == '&') {
            if (i >= 1 && s[static_cast<size_t>(i) - 1] == '&') {
                s.erase(static_cast<size_t>(i), 1);
                --i;
            } else {
                s.erase(static_cast<size_t>(i), 1);
            }
        }
    }
    if (s.size() > 3 && s.compare(s.size() - 3, 3, "...") == 0)
        s.erase(s.size() - 3);
    return s;
}

std::vector<LanguageFileInfo> NativeLanguage::availableLanguages(std::string_view utf8Dir)
{
    std::vector<LanguageFileInfo> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(pathFromUtf8(utf8Dir), ec)) {
        if (!entry.is_regular_file() || !iequalsAscii(extensionOf(utf8FromPath(entry.path())), "xml"))
            continue;
        pugi::xml_document doc;
        if (!doc.load_file(entry.path().c_str()))
            continue;
        const pugi::xml_node lang = doc.child("NotepadPlus").child("Native-Langue");
        if (!lang)
            continue;
        out.push_back({lang.attribute("name").value(), fileNameOf(utf8FromPath(entry.path())),
                       nativeUtf8Path(entry.path())});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

} // namespace npp::core
