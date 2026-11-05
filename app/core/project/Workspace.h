// Project Panel workspace files (upstream WinControls/ProjectPanel):
//   <NotepadPlus><Project name=".."><Folder name=".."><File name="relative\or\absolute"/></Folder></Project></NotepadPlus>
// File names are stored relative to the workspace file folder when they are inside it.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace npp::core {

struct ProjectItem {
    enum class Kind { Project, Folder, File };
    Kind kind = Kind::File;
    std::string name;               // project / folder name, file name (display)
    std::string path;               // File: absolute UTF-8 path
    std::vector<ProjectItem> children;

    static ProjectItem project(std::string n) { return {Kind::Project, std::move(n), {}, {}}; }
    static ProjectItem folder(std::string n) { return {Kind::Folder, std::move(n), {}, {}}; }
    static ProjectItem file(std::string absPath);
};

struct Workspace {
    std::vector<ProjectItem> projects;
};

// nullopt: missing file, XML error or no <Project>.
std::optional<Workspace> loadWorkspace(const std::string& utf8Path);
bool saveWorkspace(const Workspace& ws, const std::string& utf8Path);
std::string workspaceXml(const Workspace& ws, const std::string& workspacePath);

// upstream ProjectPanel::getRelativePath / getAbsoluteFilePath
std::string workspaceRelativePath(const std::string& filePath, const std::string& workspacePath);
std::string workspaceAbsolutePath(const std::string& storedName, const std::string& workspacePath);

} // namespace npp::core
