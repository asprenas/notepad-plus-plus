#include "ConfigFile.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <algorithm>
#include <sstream>

namespace npp::core {

void RecentFiles::add(const std::string& path)
{
    remove(path);
    files.insert(files.begin(), path);
    if (static_cast<int>(files.size()) > maxFiles)
        files.resize(static_cast<size_t>(std::max(0, maxFiles)));
}

void RecentFiles::remove(const std::string& path)
{
    files.erase(std::remove_if(files.begin(), files.end(), [&](const std::string& f) { return samePath(f, path); }),
                files.end());
}

void FindHistory::push(std::vector<std::string>& list, const std::string& value, int max)
{
    if (value.empty())
        return;
    list.erase(std::remove(list.begin(), list.end(), value), list.end());
    list.insert(list.begin(), value);
    if (static_cast<int>(list.size()) > max)
        list.resize(static_cast<size_t>(std::max(0, max)));
}

struct ConfigFile::Impl {
    pugi::xml_document doc;
};

ConfigFile::ConfigFile() : d(std::make_unique<Impl>()) {}
ConfigFile::~ConfigFile() = default;
ConfigFile::ConfigFile(ConfigFile&&) noexcept = default;
ConfigFile& ConfigFile::operator=(ConfigFile&&) noexcept = default;

bool ConfigFile::load(std::string_view utf8Path)
{
    *this = ConfigFile();
    if (!pathExists(utf8Path, PathKind::File)) {
        m_isNew = true;
        d->doc.append_child(pugi::node_declaration).append_attribute("version").set_value("1.0");
        d->doc.first_child().append_attribute("encoding").set_value("UTF-8");
        d->doc.append_child("NotepadPlus");
        return true;
    }
    if (!xml::loadFile(d->doc, utf8Path))
        return false;
    parse();
    return true;
}

bool ConfigFile::loadFromString(std::string_view text)
{
    *this = ConfigFile();
    if (!d->doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return false;
    parse();
    return true;
}

void ConfigFile::parse()
{
    const pugi::xml_node root = d->doc.child("NotepadPlus");
    gui.load(root.child("GUIConfigs"));

    if (const pugi::xml_node h = root.child("History")) {
        history.maxFiles = std::clamp(xml::getInt(h, "nbMaxFile", history.maxFiles), 0, RecentFiles::kMaxFiles);
        history.customLength = std::min(xml::getInt(h, "customLength", RecentFiles::kShowFullPath), 259);
        history.inSubMenu = xml::getBool(h, "inSubMenu", false);
        for (pugi::xml_node f = h.child("File"); f && static_cast<int>(history.files.size()) < RecentFiles::kMaxFiles;
             f = f.next_sibling("File"))
            if (const char* name = f.attribute("filename").value(); *name)
                history.files.emplace_back(name);
    }

    if (const pugi::xml_node fh = root.child("FindHistory")) {
        auto& h = findHistory;
        h.maxPath = std::clamp(xml::getInt(fh, "nbMaxFindHistoryPath", h.maxPath), 0, 30);
        h.maxFilter = std::clamp(xml::getInt(fh, "nbMaxFindHistoryFilter", h.maxFilter), 0, 30);
        h.maxFind = std::clamp(xml::getInt(fh, "nbMaxFindHistoryFind", h.maxFind), 0, 30);
        h.maxReplace = std::clamp(xml::getInt(fh, "nbMaxFindHistoryReplace", h.maxReplace), 0, 30);
        h.matchWord = xml::getBool(fh, "matchWord", h.matchWord);
        h.matchCase = xml::getBool(fh, "matchCase", h.matchCase);
        h.wrap = xml::getBool(fh, "wrap", h.wrap);
        h.directionDown = xml::getBool(fh, "directionDown", h.directionDown);
        h.fifRecursive = xml::getBool(fh, "fifRecuisive", h.fifRecursive); // sic (upstream spelling)
        h.fifInHiddenFolder = xml::getBool(fh, "fifInHiddenFolder", h.fifInHiddenFolder);
        h.fifProjectPanel1 = xml::getBool(fh, "fifProjectPanel1", h.fifProjectPanel1);
        h.fifProjectPanel2 = xml::getBool(fh, "fifProjectPanel2", h.fifProjectPanel2);
        h.fifProjectPanel3 = xml::getBool(fh, "fifProjectPanel3", h.fifProjectPanel3);
        h.fifFilterFollowsDoc = xml::getBool(fh, "fifFilterFollowsDoc", h.fifFilterFollowsDoc);
        h.searchMode = std::clamp(xml::getInt(fh, "searchMode", h.searchMode), 0, 2);
        h.transparencyMode = std::clamp(xml::getInt(fh, "transparencyMode", h.transparencyMode), 0, 2);
        h.transparency = std::clamp(xml::getInt(fh, "transparency", h.transparency), 0, 255);
        h.dotMatchesNewline = xml::getBool(fh, "dotMatchesNewline", h.dotMatchesNewline);
        h.search2ButtonsMode = xml::getBool(fh, "isSearch2ButtonsMode", h.search2ButtonsMode);
        h.regexBackwardForPowerUser = xml::getBool(fh, "regexBackward4PowerUser", h.regexBackwardForPowerUser);
        h.bookmarkLine = xml::getBool(fh, "bookmarkLine", h.bookmarkLine);
        h.purge = xml::getBool(fh, "purge", h.purge);
        auto readList = [&fh](const char* tag, std::vector<std::string>& out, int max) {
            for (pugi::xml_node n = fh.child(tag); n && static_cast<int>(out.size()) < max; n = n.next_sibling(tag))
                if (const char* v = n.attribute("name").value(); *v)
                    out.emplace_back(v);
        };
        readList("Path", h.paths, h.maxPath);
        readList("Filter", h.filters, h.maxFilter);
        readList("Find", h.finds, h.maxFind);
        readList("Replace", h.replaces, h.maxReplace);
    }

    for (pugi::xml_node n = root.child("ProjectPanels").child("ProjectPanel"); n; n = n.next_sibling("ProjectPanel")) {
        const int id = xml::getInt(n, "id", 0);
        if (id >= 0 && id <= 2)
            if (const char* f = n.attribute("workSpaceFile").value(); *f)
                projectPanelFiles[static_cast<size_t>(id)] = f;
    }

    if (const pugi::xml_node fb = root.child("FileBrowser")) {
        fileBrowser.latestSelectedItem = fb.attribute("latestSelectedItem").value();
        for (pugi::xml_node r = fb.child("root"); r; r = r.next_sibling("root")) {
            FileBrowserSettings::Root fr{r.attribute("foldername").value(), {}};
            if (fr.folder.empty())
                continue;
            for (pugi::xml_node e = r.child("expanded"); e; e = e.next_sibling("expanded"))
                if (const char* p = e.attribute("path").value(); *p)
                    fr.expanded.emplace_back(p);
            fileBrowser.roots.push_back(std::move(fr));
        }
    }
    m_loadedProjectPanelFiles = projectPanelFiles;
    m_loadedFileBrowser = fileBrowser;
}

void ConfigFile::writeBack()
{
    pugi::xml_node root = d->doc.child("NotepadPlus");
    if (!root)
        root = d->doc.append_child("NotepadPlus");

    pugi::xml_node guiRoot = root.child("GUIConfigs");
    if (!guiRoot)
        guiRoot = root.append_child("GUIConfigs");
    gui.save(guiRoot);

    pugi::xml_node h = root.child("History");
    if (!h)
        h = root.prepend_child("History"); // upstream writes History first
    xml::setInt(h, "nbMaxFile", history.maxFiles);
    xml::setBool(h, "inSubMenu", history.inSubMenu);
    xml::setInt(h, "customLength", history.customLength);
    while (pugi::xml_node f = h.child("File"))
        h.remove_child(f);
    for (const auto& f : history.files)
        h.append_child("File").append_attribute("filename").set_value(f.c_str());

    pugi::xml_node fh = root.child("FindHistory");
    if (!fh)
        fh = root.insert_child_after("FindHistory", h);
    const auto& fhd = findHistory;
    xml::setInt(fh, "nbMaxFindHistoryPath", fhd.maxPath);
    xml::setInt(fh, "nbMaxFindHistoryFilter", fhd.maxFilter);
    xml::setInt(fh, "nbMaxFindHistoryFind", fhd.maxFind);
    xml::setInt(fh, "nbMaxFindHistoryReplace", fhd.maxReplace);
    xml::setBool(fh, "matchWord", fhd.matchWord);
    xml::setBool(fh, "matchCase", fhd.matchCase);
    xml::setBool(fh, "wrap", fhd.wrap);
    xml::setBool(fh, "directionDown", fhd.directionDown);
    xml::setBool(fh, "fifRecuisive", fhd.fifRecursive);
    xml::setBool(fh, "fifInHiddenFolder", fhd.fifInHiddenFolder);
    xml::setBool(fh, "fifProjectPanel1", fhd.fifProjectPanel1);
    xml::setBool(fh, "fifProjectPanel2", fhd.fifProjectPanel2);
    xml::setBool(fh, "fifProjectPanel3", fhd.fifProjectPanel3);
    xml::setBool(fh, "fifFilterFollowsDoc", fhd.fifFilterFollowsDoc);
    xml::setInt(fh, "searchMode", fhd.searchMode);
    xml::setInt(fh, "transparencyMode", fhd.transparencyMode);
    xml::setInt(fh, "transparency", fhd.transparency);
    xml::setBool(fh, "dotMatchesNewline", fhd.dotMatchesNewline);
    xml::setBool(fh, "isSearch2ButtonsMode", fhd.search2ButtonsMode);
    xml::setBool(fh, "regexBackward4PowerUser", fhd.regexBackwardForPowerUser);
    xml::setBool(fh, "bookmarkLine", fhd.bookmarkLine);
    xml::setBool(fh, "purge", fhd.purge);
    for (const char* tag : {"Path", "Filter", "Find", "Replace"})
        while (pugi::xml_node n = fh.child(tag))
            fh.remove_child(n);
    auto writeList = [&fh](const char* tag, const std::vector<std::string>& list) {
        for (const auto& v : list)
            fh.append_child(tag).append_attribute("name").set_value(v.c_str());
    };
    writeList("Path", fhd.paths);
    writeList("Filter", fhd.filters);
    writeList("Find", fhd.finds);
    writeList("Replace", fhd.replaces);

    // Upstream recreates both at the end of <NotepadPlus> on every save; rewritten here only when
    // changed (unchanged files keep their content, a new config.xml stays minimal).
    if (projectPanelFiles != m_loadedProjectPanelFiles) {
        pugi::xml_node old = root.child("ProjectPanels");
        if (old)
            root.remove_child(old);
        pugi::xml_node pp = root.append_child("ProjectPanels");
        for (int i = 0; i < 3; ++i) {
            pugi::xml_node n = pp.append_child("ProjectPanel");
            xml::setInt(n, "id", i);
            xml::setString(n, "workSpaceFile", projectPanelFiles[static_cast<size_t>(i)]);
        }
    }
    if (!(fileBrowser == m_loadedFileBrowser)) {
        pugi::xml_node old = root.child("FileBrowser");
        if (old)
            root.remove_child(old);
        pugi::xml_node fb = root.append_child("FileBrowser");
        if (!fileBrowser.roots.empty()) {
            xml::setString(fb, "latestSelectedItem", fileBrowser.latestSelectedItem);
            for (const auto& r : fileBrowser.roots) {
                pugi::xml_node rn = fb.append_child("root");
                xml::setString(rn, "foldername", r.folder);
                std::vector<std::string> expanded = r.expanded;
                std::sort(expanded.begin(), expanded.end());
                for (const auto& e : expanded)
                    xml::setString(rn.append_child("expanded"), "path", e);
            }
        }
    }
    m_loadedProjectPanelFiles = projectPanelFiles;
    m_loadedFileBrowser = fileBrowser;
}

bool ConfigFile::save(std::string_view utf8Path)
{
    writeBack();
    return xml::saveFile(d->doc, utf8Path);
}

std::string ConfigFile::saveToString()
{
    writeBack();
    std::ostringstream os;
    d->doc.save(os, "    ", pugi::format_indent);
    return os.str();
}

} // namespace npp::core
