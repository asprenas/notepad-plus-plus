// Find / replace engine (port of the non-UI parts of upstream FindReplaceDlg.cpp: Searching,
// processFindNext, processReplace, processAll, findInFiles). Runs on any IEditor, so the
// regular expression behaviour is the one of the Notepad++ Boost.Regex Scintilla engine.
#pragma once

#include "core/editor/IEditor.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

class FileManager;

enum class SearchType { Normal = 0, Extended = 1, Regex = 2 }; // upstream FindHistory::searchMode

struct SearchOptions {
    std::string findWhat;
    std::string replaceWith;
    SearchType  type = SearchType::Normal;
    bool matchCase = false;
    bool wholeWord = false;
    bool dotMatchesNewline = false;
    bool wrap = true;
    bool directionDown = true;
};

// Upstream FINDNEXTTYPE_*: decides how empty regex matches are handled
enum class FindNextType { FindNext, ReplaceNext, FindNextForReplace };

struct SearchMatch {
    intptr_t start = 0;
    intptr_t end = 0;
    bool wrapped = false; // found after wrapping around
};

// "\n \r \t \0 \\ \xHH \uHHHH \dDDD \oOOO \bBBBBBBBB" -> characters (upstream convertExtendedToString)
std::string convertExtended(std::string_view s);
// SCI_SETSEARCHFLAGS value (upstream buildSearchFlags + findNextType flags)
int searchFlags(const SearchOptions& o, FindNextType type = FindNextType::FindNext);
// Pattern actually given to Scintilla (extended sequences converted)
std::string searchPattern(const SearchOptions& o);

// Searches from `from` in the option direction, wrapping if enabled. Selection not changed.
std::optional<SearchMatch> findNext(const IEditor& e, const SearchOptions& o, intptr_t from,
                                    FindNextType type = FindNextType::FindNext);
// All matches in [start, end) (end < 0: document end).
std::vector<SearchMatch> findAll(const IEditor& e, const SearchOptions& o, intptr_t start = 0, intptr_t end = -1);
int countMatches(const IEditor& e, const SearchOptions& o);
// Replaces `match` (regex: with back references); returns the end of the replacement.
intptr_t replaceMatch(const IEditor& e, const SearchOptions& o, const SearchMatch& match);
// Replace All in [start, end) as one undo action; returns the number of replacements.
int replaceAll(const IEditor& e, const SearchOptions& o, intptr_t start = 0, intptr_t end = -1);

// --- Find in Files -------------------------------------------------------------------------------
struct FindInFilesOptions {
    std::string directory;
    std::string filters = "*.*";  // space / ';' separated, "!pattern" excludes, "!\dir" / "!+\dir" exclude dirs
    bool recursive = true;
    bool includeHidden = false;
};

struct LineHit {
    std::string file;              // UTF-8 full path
    size_t      line = 0;          // 1-based
    std::string text;              // line text (UTF-8, without EOL)
    std::vector<std::pair<size_t, size_t>> ranges; // byte ranges of the matches inside `text`
};

// Files to search (upstream getMatchedFilenames), sorted.
std::vector<std::string> listFilesToSearch(const FindInFilesOptions& o);

// progress(index, total, file) returns false to cancel.
using SearchProgress = std::function<bool(size_t, size_t, const std::string&)>;

std::vector<LineHit> findInFiles(FileManager& fm, const std::vector<std::string>& files, const SearchOptions& o,
                                 const SearchProgress& progress = {});
// Replaces in every file and saves it in its own encoding. Returns total replacements.
int replaceInFiles(FileManager& fm, const std::vector<std::string>& files, const SearchOptions& o,
                   const SearchProgress& progress = {});

} // namespace npp::core
