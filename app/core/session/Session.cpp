#include "Session.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <set>
#include <sstream>

namespace npp::core {

namespace {

const std::set<std::string>& knownFileAttributes()
{
    static const std::set<std::string> k = {
        "firstVisibleLine", "xOffset", "scrollWidth", "startPos", "endPos", "selMode", "offset", "wrapCount",
        "lang", "encoding", "userReadOnly", "filename", "backupFilePath", "originalFileLastModifTimestamp",
        "originalFileLastModifTimestampHigh", "tabColourId", "RTL", "tabPinned", "untitleTabRenamed",
        "mapFirstVisibleDisplayLine", "mapFirstVisibleDocLine", "mapLastVisibleDocLine", "mapNbLine",
        "mapHigherPos", "mapWidth", "mapHeight", "mapKByteInDoc", "mapWrapIndentMode", "mapIsWrap"};
    return k;
}

long long ll(const pugi::xml_node& n, const char* a, long long def)
{
    const pugi::xml_attribute at = n.attribute(a);
    return at ? at.as_llong(def) : def;
}

void readView(const pugi::xml_node& viewNode, SessionView& view)
{
    view.activeIndex = viewNode.attribute("activeIndex").as_int(0);
    for (pugi::xml_node fn = viewNode.child("File"); fn; fn = fn.next_sibling("File")) {
        SessionFile f;
        f.filename = fn.attribute("filename").value();
        if (f.filename.empty())
            continue;
        f.firstVisibleLine = ll(fn, "firstVisibleLine", 0);
        f.xOffset = ll(fn, "xOffset", 0);
        f.scrollWidth = ll(fn, "scrollWidth", 1);
        f.startPos = ll(fn, "startPos", 0);
        f.endPos = ll(fn, "endPos", 0);
        f.selMode = fn.attribute("selMode").as_int(0);
        f.offset = ll(fn, "offset", 0);
        f.wrapCount = ll(fn, "wrapCount", 1);
        f.lang = fn.attribute("lang").value();
        f.encoding = fn.attribute("encoding").as_int(-1);
        f.userReadOnly = xml::getBool(fn, "userReadOnly", false);
        f.backupFilePath = fn.attribute("backupFilePath").value();
        f.originalFileLastModifTimestamp = fn.attribute("originalFileLastModifTimestamp").as_uint(0);
        f.originalFileLastModifTimestampHigh = fn.attribute("originalFileLastModifTimestampHigh").as_uint(0);
        f.tabColourId = fn.attribute("tabColourId").as_int(-1);
        f.rtl = xml::getBool(fn, "RTL", false);
        f.tabPinned = xml::getBool(fn, "tabPinned", false);
        f.untitledTabRenamed = xml::getBool(fn, "untitleTabRenamed", false);
        f.map.firstVisibleDisplayLine = ll(fn, "mapFirstVisibleDisplayLine", -1);
        f.map.firstVisibleDocLine = ll(fn, "mapFirstVisibleDocLine", -1);
        f.map.lastVisibleDocLine = ll(fn, "mapLastVisibleDocLine", -1);
        f.map.nbLine = ll(fn, "mapNbLine", -1);
        f.map.higherPos = ll(fn, "mapHigherPos", -1);
        f.map.width = ll(fn, "mapWidth", -1);
        f.map.height = ll(fn, "mapHeight", -1);
        f.map.kByteInDoc = ll(fn, "mapKByteInDoc", -1);
        f.map.wrapIndentMode = ll(fn, "mapWrapIndentMode", -1);
        f.map.isWrap = xml::getBool(fn, "mapIsWrap", false);
        for (pugi::xml_node m = fn.child("Mark"); m; m = m.next_sibling("Mark"))
            f.marks.push_back(static_cast<size_t>(m.attribute("line").as_ullong(0)));
        for (pugi::xml_node m = fn.child("Fold"); m; m = m.next_sibling("Fold"))
            f.folds.push_back(static_cast<size_t>(m.attribute("line").as_ullong(0)));
        for (pugi::xml_attribute a = fn.first_attribute(); a; a = a.next_attribute())
            if (!knownFileAttributes().count(a.name()))
                f.extraAttributes[a.name()] = a.value();
        view.files.push_back(std::move(f));
    }
}

std::optional<Session> parseDoc(const pugi::xml_document& doc)
{
    const pugi::xml_node sessionNode = doc.child("NotepadPlus").child("Session");
    if (!sessionNode)
        return std::nullopt;
    Session s;
    s.activeView = sessionNode.attribute("activeView").as_int(0);
    readView(sessionNode.child("mainView"), s.mainView);
    readView(sessionNode.child("subView"), s.subView);
    if (const pugi::xml_node fb = sessionNode.child("FileBrowser")) {
        s.fileBrowserSelectedItem = fb.attribute("latestSelectedItem").value();
        for (pugi::xml_node r = fb.child("root"); r; r = r.next_sibling("root")) {
            SessionFileBrowserRoot root{r.attribute("foldername").value(), {}};
            for (pugi::xml_node e = r.child("expanded"); e; e = e.next_sibling("expanded"))
                root.expanded.emplace_back(e.attribute("path").value());
            s.fileBrowserRoots.push_back(std::move(root));
        }
    }
    return s;
}

void writeDoc(const Session& s, pugi::xml_document& doc)
{
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version").set_value("1.0");
    decl.append_attribute("encoding").set_value("UTF-8");
    pugi::xml_node sessionNode = doc.append_child("NotepadPlus").append_child("Session");
    sessionNode.append_attribute("activeView").set_value(s.activeView);

    auto writeView = [&sessionNode](const char* tag, const SessionView& v) {
        pugi::xml_node vn = sessionNode.append_child(tag);
        vn.append_attribute("activeIndex").set_value(v.activeIndex);
        for (const auto& f : v.files) {
            pugi::xml_node fn = vn.append_child("File");
            xml::setInt(fn, "firstVisibleLine", f.firstVisibleLine);
            xml::setInt(fn, "xOffset", f.xOffset);
            xml::setInt(fn, "scrollWidth", f.scrollWidth);
            xml::setInt(fn, "startPos", f.startPos);
            xml::setInt(fn, "endPos", f.endPos);
            xml::setInt(fn, "selMode", f.selMode);
            xml::setInt(fn, "offset", f.offset);
            xml::setInt(fn, "wrapCount", f.wrapCount);
            xml::setString(fn, "lang", f.lang);
            xml::setInt(fn, "encoding", f.encoding);
            xml::setBool(fn, "userReadOnly", f.userReadOnly);
            xml::setString(fn, "filename", f.filename);
            xml::setString(fn, "backupFilePath", f.backupFilePath);
            fn.append_attribute("originalFileLastModifTimestamp").set_value(f.originalFileLastModifTimestamp);
            fn.append_attribute("originalFileLastModifTimestampHigh").set_value(f.originalFileLastModifTimestampHigh);
            xml::setInt(fn, "tabColourId", f.tabColourId);
            xml::setBool(fn, "RTL", f.rtl);
            xml::setBool(fn, "tabPinned", f.tabPinned);
            if (f.untitledTabRenamed)
                xml::setString(fn, "untitleTabRenamed", "yes");
            xml::setInt(fn, "mapFirstVisibleDisplayLine", f.map.firstVisibleDisplayLine);
            xml::setInt(fn, "mapFirstVisibleDocLine", f.map.firstVisibleDocLine);
            xml::setInt(fn, "mapLastVisibleDocLine", f.map.lastVisibleDocLine);
            xml::setInt(fn, "mapNbLine", f.map.nbLine);
            xml::setInt(fn, "mapHigherPos", f.map.higherPos);
            xml::setInt(fn, "mapWidth", f.map.width);
            xml::setInt(fn, "mapHeight", f.map.height);
            xml::setInt(fn, "mapKByteInDoc", f.map.kByteInDoc);
            xml::setInt(fn, "mapWrapIndentMode", f.map.wrapIndentMode);
            xml::setBool(fn, "mapIsWrap", f.map.isWrap);
            for (const auto& [k, v2] : f.extraAttributes)
                xml::setString(fn, k.c_str(), v2);
            for (size_t m : f.marks)
                fn.append_child("Mark").append_attribute("line").set_value(static_cast<unsigned long long>(m));
            for (size_t m : f.folds)
                fn.append_child("Fold").append_attribute("line").set_value(static_cast<unsigned long long>(m));
        }
    };
    writeView("mainView", s.mainView);
    writeView("subView", s.subView);

    if (!s.fileBrowserRoots.empty()) {
        pugi::xml_node fb = sessionNode.append_child("FileBrowser");
        fb.append_attribute("latestSelectedItem").set_value(s.fileBrowserSelectedItem.c_str());
        for (const auto& r : s.fileBrowserRoots) {
            pugi::xml_node rn = fb.append_child("root");
            rn.append_attribute("foldername").set_value(r.folder.c_str());
            for (const auto& e : r.expanded)
                rn.append_child("expanded").append_attribute("path").set_value(e.c_str());
        }
    }
}

} // namespace

std::optional<Session> parseSession(std::string_view text)
{
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return std::nullopt;
    return parseDoc(doc);
}

std::optional<Session> loadSession(std::string_view utf8Path)
{
    pugi::xml_document doc;
    if (!xml::loadFile(doc, utf8Path))
        return std::nullopt;
    return parseDoc(doc);
}

bool saveSession(const Session& session, std::string_view utf8Path)
{
    pugi::xml_document doc;
    writeDoc(session, doc);
    return xml::saveFile(doc, utf8Path);
}

std::string serializeSession(const Session& session)
{
    pugi::xml_document doc;
    writeDoc(session, doc);
    std::ostringstream os;
    doc.save(os, "    ", pugi::format_indent);
    return os.str();
}

} // namespace npp::core
