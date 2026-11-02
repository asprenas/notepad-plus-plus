#include "StyleRegistry.h"

#include "core/config/XmlHelpers.h"
#include "core/lang/LangType.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <pugixml.hpp>

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace npp::core {

namespace {

int parseColour(const char* hex)
{
    if (!hex || !*hex)
        return -1;
    char* end = nullptr;
    const long v = std::strtol(hex, &end, 16);
    return (*end == '\0' && v >= 0 && v <= 0xFFFFFF) ? static_cast<int>(v) : -1;
}

std::string colourToString(int rgb)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06X", rgb & 0xFFFFFF);
    return buf;
}

int optInt(const pugi::xml_node& n, const char* attr)
{
    const char* v = n.attribute(attr).value();
    if (!*v)
        return -1;
    const auto p = parseInt(v);
    return p ? static_cast<int>(*p) : -1;
}

Style readStyle(const pugi::xml_node& n)
{
    Style s;
    s.styleId = n.attribute("styleID").as_int(-1);
    s.name = n.attribute("name").value();
    s.fg = parseColour(n.attribute("fgColor").value());
    s.bg = parseColour(n.attribute("bgColor").value());
    s.fontName = n.attribute("fontName").value();
    s.fontStyle = optInt(n, "fontStyle");
    s.fontSize = optInt(n, "fontSize");
    s.keywordClass = keywordClassFromName(n.attribute("keywordClass").value());
    s.keywords = n.text().get();
    s.colorStyle = optInt(n, "colorStyle");
    return s;
}

void writeStyle(pugi::xml_node n, const Style& s)
{
    // An attribute is rewritten only when its parsed value differs from the model, so
    // unchanged values keep their original spelling ("090", lower case hex, ...).
    auto setIntAttr = [&n](const char* a, int v, bool emptyIfNegative) {
        const pugi::xml_attribute cur = n.attribute(a);
        if (cur && optInt(n, a) == (v < 0 ? -1 : v))
            return;
        if (v < 0 && !cur)
            return;
        xml::setString(n, a, v < 0 && emptyIfNegative ? std::string() : std::to_string(v));
    };
    auto setColourAttr = [&n](const char* a, int rgb) {
        const pugi::xml_attribute cur = n.attribute(a);
        if (cur && parseColour(cur.value()) == rgb)
            return;
        if (rgb < 0 && !cur)
            return;
        xml::setString(n, a, rgb >= 0 ? colourToString(rgb) : std::string());
    };
    if (s.name != n.attribute("name").value())
        xml::setString(n, "name", s.name);
    setIntAttr("styleID", s.styleId, false);
    setColourAttr("fgColor", s.fg);
    setColourAttr("bgColor", s.bg);
    if (s.fontName != n.attribute("fontName").value() && (!s.fontName.empty() || n.attribute("fontName")))
        xml::setString(n, "fontName", s.fontName);
    setIntAttr("fontStyle", s.fontStyle, true);
    setIntAttr("fontSize", s.fontSize, true);
    if (s.colorStyle >= 0)
        setIntAttr("colorStyle", s.colorStyle, false);
    if (s.keywords != n.text().get() && (!s.keywords.empty() || n.text()))
        xml::setText(n, s.keywords);
}

} // namespace

const Style* LexerStyler::findById(int id) const
{
    for (const auto& s : styles)
        if (s.styleId == id)
            return &s;
    return nullptr;
}

struct StyleRegistry::Impl {
    pugi::xml_document doc;
};

StyleRegistry::StyleRegistry() : d(std::make_unique<Impl>()) {}
StyleRegistry::~StyleRegistry() = default;
StyleRegistry::StyleRegistry(StyleRegistry&&) noexcept = default;
StyleRegistry& StyleRegistry::operator=(StyleRegistry&&) noexcept = default;

bool StyleRegistry::loadFromString(std::string_view text)
{
    *this = StyleRegistry();
    if (!d->doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return false;
    if (!d->doc.child("NotepadPlus"))
        return false;
    parse();
    return true;
}

bool StyleRegistry::load(std::string_view utf8Path)
{
    const auto content = readFileContent(utf8Path);
    return content && loadFromString(*content);
}

void StyleRegistry::parse()
{
    const pugi::xml_node root = d->doc.child("NotepadPlus");
    for (pugi::xml_node lt = root.child("LexerStyles").child("LexerType"); lt; lt = lt.next_sibling("LexerType")) {
        LexerStyler ls;
        ls.name = lt.attribute("name").value();
        ls.desc = lt.attribute("desc").value();
        ls.ext = lt.attribute("ext").value();
        for (pugi::xml_node ws = lt.child("WordsStyle"); ws; ws = ws.next_sibling("WordsStyle"))
            ls.styles.push_back(readStyle(ws));
        m_lexers.push_back(std::move(ls));
    }
    for (pugi::xml_node ws = root.child("GlobalStyles").child("WidgetStyle"); ws; ws = ws.next_sibling("WidgetStyle"))
        m_global.push_back(readStyle(ws));
}

void StyleRegistry::writeBack()
{
    pugi::xml_node root = d->doc.child("NotepadPlus");
    if (!root)
        root = d->doc.append_child("NotepadPlus");
    pugi::xml_node lexRoot = root.child("LexerStyles");
    if (!lexRoot)
        lexRoot = root.append_child("LexerStyles");
    for (const auto& ls : m_lexers) {
        pugi::xml_node lt = xml::childWithAttr(lexRoot, "LexerType", "name", ls.name, true);
        xml::setString(lt, "desc", ls.desc);
        xml::setString(lt, "ext", ls.ext);
        for (const auto& s : ls.styles) {
            pugi::xml_node ws;
            for (pugi::xml_node c = lt.child("WordsStyle"); c; c = c.next_sibling("WordsStyle"))
                if (c.attribute("styleID").as_int(-2) == s.styleId && s.name == c.attribute("name").value()) { ws = c; break; }
            if (!ws)
                ws = lt.append_child("WordsStyle");
            writeStyle(ws, s);
        }
    }
    pugi::xml_node globalRoot = root.child("GlobalStyles");
    if (!globalRoot)
        globalRoot = root.append_child("GlobalStyles");
    for (const auto& s : m_global) {
        pugi::xml_node ws = xml::childWithAttr(globalRoot, "WidgetStyle", "name", s.name, true);
        writeStyle(ws, s);
    }
}

bool StyleRegistry::save(std::string_view utf8Path)
{
    writeBack();
    return xml::saveFile(d->doc, utf8Path);
}

std::string StyleRegistry::saveToString()
{
    writeBack();
    std::ostringstream os;
    d->doc.save(os, "    ", pugi::format_indent);
    return os.str();
}

const LexerStyler* StyleRegistry::lexer(std::string_view name) const
{
    for (const auto& l : m_lexers)
        if (iequalsAscii(l.name, name))
            return &l;
    return nullptr;
}

LexerStyler* StyleRegistry::lexer(std::string_view name)
{
    for (auto& l : m_lexers)
        if (iequalsAscii(l.name, name))
            return &l;
    return nullptr;
}

const Style* StyleRegistry::globalStyle(int styleId) const
{
    for (const auto& s : m_global)
        if (s.styleId == styleId)
            return &s;
    return nullptr;
}

const Style* StyleRegistry::globalStyleByName(std::string_view name) const
{
    for (const auto& s : m_global)
        if (s.name == name)
            return &s;
    return nullptr;
}

std::map<std::string, std::string> StyleRegistry::themesIn(std::string_view utf8Dir)
{
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(pathFromUtf8(utf8Dir), ec)) {
        const std::string p = utf8FromPath(e.path());
        if (e.is_regular_file() && iequalsAscii(extensionOf(p), "xml")) {
            std::string name = fileNameOf(p);
            name.resize(name.size() - 4);
            out[name] = nativeUtf8Path(e.path());
        }
    }
    return out;
}

} // namespace npp::core
