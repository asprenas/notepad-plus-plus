// Path and file helpers (port of the portable parts of MISC/Common/Common.*).
// Paths cross the API as UTF-8 std::string or std::filesystem::path.
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

namespace fs = std::filesystem;

// --- UTF-8 <-> std::filesystem::path (never use the narrow path ctor: it is ANSI on Windows)
fs::path pathFromUtf8(std::string_view utf8);
std::string utf8FromPath(const fs::path& p);
// Native separators for display ("C:\dir\file" on Windows, "/dir/file" elsewhere).
std::string nativeUtf8Path(const fs::path& p);

// Joins like upstream pathAppend(): exactly one separator between parts.
std::string pathAppend(std::string_view base, std::string_view append);
// Directory part (upstream pathRemoveFileSpec), "" if none.
std::string parentPath(std::string_view path);
std::string fileNameOf(std::string_view path);
// Extension without the dot, "" if none.
std::string extensionOf(std::string_view path);
// Absolute, lexically normalised path (upstream relativeFilePathToFullFilePath).
std::string absolutePath(std::string_view path);
bool isUncPath(std::string_view path) noexcept;

// File names are case-insensitive on Windows and (by default) macOS, case-sensitive on Linux.
bool fileNamesAreCaseInsensitive() noexcept;
bool samePath(std::string_view a, std::string_view b);

// --- wildcard matching (replacement for Win32 PathMatchSpec) ------------------
// '*' any sequence, '?' one char; spec may contain several patterns separated by ';'.
// Matching is ASCII case-insensitive, like PathMatchSpec (Find in Files filters).
bool wildcardMatch(std::string_view name, std::string_view spec);
// Upstream matchInList(): '!pattern' excludes; true if an inclusion pattern matches.
bool matchInList(std::string_view fileName, const std::vector<std::string>& patterns);
// Upstream matchInExcludeDirList(): "!+\dir" (all levels), "!\dir" (first level). '/' accepted too.
bool matchInExcludeDirList(std::string_view dirName, const std::vector<std::string>& patterns, size_t level);
bool allPatternsAreExclusion(const std::vector<std::string>& patterns);

// --- file content -----------------------------------------------------------------
std::optional<std::string> readFileContent(std::string_view path);
bool writeFileContent(std::string_view path, std::string_view content);

// --- existence checks with timeout (slow network shares) --------------------------
enum class PathKind { Any, File, Directory };
// Returns false on timeout; *timedOut set accordingly.
bool pathExists(std::string_view path, PathKind kind = PathKind::Any,
                std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(), bool* timedOut = nullptr);

// Expands environment variables: %VAR% (all platforms, upstream syntax), $VAR / ${VAR} and a
// leading ~ (non-Windows).
std::string expandEnvironment(std::string_view path);

// Shortens a path for display to at most maxChars characters with "..." in the middle
// (replacement for Win32 PathCompactPathEx).
std::string compactPath(std::string_view path, size_t maxChars);
// Recent-files menu label (upstream BuildMenuFileName). filenameLen > 0: compact full path to
// that length; 0: file name only; < 0: full path.
std::string buildMenuFileName(int filenameLen, unsigned int pos, std::string_view filename, bool ordinalNumber = true);

} // namespace npp::core
