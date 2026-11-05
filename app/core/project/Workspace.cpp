#include "Workspace.h"

#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <sstream>

namespace npp::core {

namespace {

bool isRelative(const std::string& p)
{
    return pathFromUtf8(p).is_relative() && !isUncPath(p);
}

std::string nativeSeparators(std::string p)
{
#if defined(_WIN32)
    for (char& c : p)
        if (c == '/')
            c = '\\';
#else
    for (char& c : p) // workspaces written on Windows
        if (c == '\\')
            c = '/';
#endif
    return p;
}

void readChildren(const pugi::xml_node& node, std::vector<ProjectItem>& out, const std::string& wsPath)
{
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
        if (std::string_view(c.name()) == "Folder") {
            ProjectItem f = ProjectItem::folder(c.attribute("name").value());
            readChildren(c, f.children, wsPath);
            out.push_back(std::move(f));
        } else if (std::string_view(c.name()) == "File") {
            const std::string stored = c.attribute("name").value();
            ProjectItem f;
            f.kind = ProjectItem::Kind::File;
            f.path = workspaceAbsolutePath(stored, wsPath);
            f.name = fileNameOf(nativeSeparators(stored));
            out.push_back(std::move(f));
        }
    }
}

void writeChildren(pugi::xml_node node, const std::vector<ProjectItem>& items, const std::string& wsPath)
{
    for (const ProjectItem& it : items) {
        if (it.kind == ProjectItem::Kind::File) {
            node.append_child("File").append_attribute("name").set_value(workspaceRelativePath(it.path, wsPath).c_str());
        } else {
            pugi::xml_node f = node.append_child("Folder");
            f.append_attribute("name").set_value(it.name.c_str());
            writeChildren(f, it.children, wsPath);
        }
    }
}

pugi::xml_document toXml(const Workspace& ws, const std::string& wsPath)
{
    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("NotepadPlus");
    for (const ProjectItem& p : ws.projects) {
        pugi::xml_node pn = root.append_child("Project");
        pn.append_attribute("name").set_value(p.name.c_str());
        writeChildren(pn, p.children, wsPath);
    }
    return doc;
}

} // namespace

ProjectItem ProjectItem::file(std::string absPath)
{
    ProjectItem f;
    f.kind = Kind::File;
    f.name = fileNameOf(absPath);
    f.path = std::move(absPath);
    return f;
}

std::string workspaceRelativePath(const std::string& filePath, const std::string& workspacePath)
{
    const std::string dir = nativeSeparators(parentPath(workspacePath));
    const std::string file = nativeSeparators(filePath);
    if (dir.empty() || file.size() <= dir.size())
        return file;
    const std::string head = file.substr(0, dir.size());
    if (!samePath(head, dir))
        return file;
    std::string rest = file.substr(dir.size());
    if (!rest.empty() && (rest[0] == '\\' || rest[0] == '/'))
        rest.erase(0, 1);
    else if (dir.back() != '\\' && dir.back() != '/')
        return file; // "C:\dir2\x" is not inside "C:\dir"
    return rest;
}

std::string workspaceAbsolutePath(const std::string& storedName, const std::string& workspacePath)
{
    const std::string n = nativeSeparators(storedName);
    if (!isRelative(n))
        return n;
    return nativeSeparators(pathAppend(parentPath(workspacePath), n));
}

std::optional<Workspace> loadWorkspace(const std::string& utf8Path)
{
    pugi::xml_document doc;
    if (!doc.load_file(pathFromUtf8(utf8Path).c_str(),
                       pugi::parse_default | pugi::parse_comments | pugi::parse_declaration))
        return std::nullopt;
    const pugi::xml_node root = doc.child("NotepadPlus");
    if (!root.child("Project"))
        return std::nullopt;
    Workspace ws;
    for (pugi::xml_node p = root.child("Project"); p; p = p.next_sibling("Project")) {
        ProjectItem pi = ProjectItem::project(p.attribute("name").value());
        readChildren(p, pi.children, utf8Path);
        ws.projects.push_back(std::move(pi));
    }
    return ws;
}

std::string workspaceXml(const Workspace& ws, const std::string& workspacePath)
{
    std::ostringstream os;
    toXml(ws, workspacePath).save(os, "    ", pugi::format_indent | pugi::format_no_declaration);
    return os.str();
}

bool saveWorkspace(const Workspace& ws, const std::string& utf8Path)
{
    // upstream NppXml::saveFileProject
    return toXml(ws, utf8Path).save_file(pathFromUtf8(utf8Path).c_str(), "    ",
                                         pugi::format_indent | pugi::format_no_declaration | pugi::format_save_file_text);
}

} // namespace npp::core
