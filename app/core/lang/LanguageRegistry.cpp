#include "LanguageRegistry.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <pugixml.hpp>

#include <sstream>

namespace npp::core {

LanguageDef* LanguageRegistry::findMutable(LangType type)
{
    for (auto& l : m_langs)
        if (l.type == type)
            return &l;
    return nullptr;
}

std::string LanguageRegistry::saveToString() const
{
    static const char* const kNames[kKeywordClassCount] = {
        "instre1", "instre2", "type1", "type2", "type3", "type4", "type5", "type6", "type7",
        "substyle1", "substyle2", "substyle3", "substyle4", "substyle5", "substyle6", "substyle7", "substyle8"};
    pugi::xml_document doc;
    doc.load_buffer(m_source.data(), m_source.size(), xml::kParseOptions);
    pugi::xml_node root = doc.child("NotepadPlus");
    if (!root)
        root = doc.append_child("NotepadPlus");
    pugi::xml_node langs = root.child("Languages");
    if (!langs)
        langs = root.append_child("Languages");
    for (const auto& l : m_langs) {
        pugi::xml_node ln = xml::childWithAttr(langs, "Language", "name", l.name, true);
        auto attr = [&ln](const char* a, const std::string& v) {
            if (v != ln.attribute(a).value() && (!v.empty() || ln.attribute(a)))
                xml::setString(ln, a, v);
        };
        attr("ext", l.extAttribute);
        attr("commentLine", l.commentLine);
        attr("commentStart", l.commentStart);
        attr("commentEnd", l.commentEnd);
        if (l.tabSettings != ln.attribute("tabSettings").as_int(-1))
            xml::setInt(ln, "tabSettings", l.tabSettings);
        for (int c = 0; c < kKeywordClassCount; ++c) {
            const std::string& words = l.keywords[static_cast<size_t>(c)];
            pugi::xml_node kw = xml::childWithAttr(ln, "Keywords", "name", kNames[c], false);
            if (!kw && words.empty())
                continue;
            if (!kw)
                kw = xml::childWithAttr(ln, "Keywords", "name", kNames[c], true);
            if (words != kw.text().get())
                xml::setText(kw, words);
        }
    }
    std::ostringstream os;
    doc.save(os, "    ", pugi::format_indent);
    return os.str();
}

bool LanguageRegistry::save(std::string_view utf8Path) const
{
    return writeFileContent(utf8Path, saveToString());
}

bool LanguageRegistry::loadFromString(std::string_view text)
{
    m_langs.clear();
    m_source = std::string(text);
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return false;
    const pugi::xml_node langs = doc.child("NotepadPlus").child("Languages");
    if (!langs)
        return false;
    for (pugi::xml_node ln = langs.child("Language"); ln; ln = ln.next_sibling("Language")) {
        LanguageDef def;
        def.name = ln.attribute("name").value();
        const auto type = langTypeFromName(def.name);
        if (def.name.empty() || !type)
            continue; // unknown language names are ignored, like upstream
        def.type = *type;
        def.extAttribute = ln.attribute("ext").value();
        for (const auto& e : tokenize(ln.attribute("ext").value(), ' '))
            def.extensions.push_back(asciiLower(e));
        def.commentLine = ln.attribute("commentLine").value();
        def.commentStart = ln.attribute("commentStart").value();
        def.commentEnd = ln.attribute("commentEnd").value();
        def.tabSettings = ln.attribute("tabSettings").as_int(-1);
        for (pugi::xml_node kw = ln.child("Keywords"); kw; kw = kw.next_sibling("Keywords")) {
            const int cls = keywordClassFromName(kw.attribute("name").value());
            if (cls >= 0)
                def.keywords[static_cast<size_t>(cls)] = kw.text().get();
        }
        m_langs.push_back(std::move(def));
    }
    return true;
}

bool LanguageRegistry::load(std::string_view utf8Path)
{
    const auto content = readFileContent(utf8Path);
    return content && loadFromString(*content);
}

const LanguageDef* LanguageRegistry::find(LangType type) const
{
    for (const auto& l : m_langs)
        if (l.type == type)
            return &l;
    return nullptr;
}

const LanguageDef* LanguageRegistry::findByName(std::string_view name) const
{
    for (const auto& l : m_langs)
        if (iequalsAscii(l.name, name))
            return &l;
    return nullptr;
}

const std::string& LanguageRegistry::wordList(LangType type, int cls) const
{
    static const std::string kEmpty;
    const LanguageDef* l = find(type);
    if (!l || cls < 0 || cls >= kKeywordClassCount)
        return kEmpty;
    return l->keywords[static_cast<size_t>(cls)];
}

LangType LanguageRegistry::languageForFile(std::string_view utf8Path) const
{
    const std::string name = asciiLower(fileNameOf(utf8Path));
    // upstream Buffer::setFileName special file names
    if (name == "makefile" || name == "gnumakefile")
        return L_MAKEFILE;
    if (name == "cmakelists.txt")
        return L_CMAKE;

    const std::string ext = asciiLower(extensionOf(utf8Path));
    // upstream getLangFromExt scans the languages from the last one to the first one
    if (!ext.empty())
        for (auto l = m_langs.rbegin(); l != m_langs.rend(); ++l)
            for (const auto& e : l->extensions)
                if (e == ext)
                    return l->type;
    return L_TEXT;
}

void LanguageRegistry::addUserExtensions(LangType type, std::string_view exts)
{
    for (auto& l : m_langs)
        if (l.type == type)
            for (const auto& e : tokenize(exts, ' '))
                l.extensions.insert(l.extensions.begin(), asciiLower(e)); // user extensions win
}

} // namespace npp::core
