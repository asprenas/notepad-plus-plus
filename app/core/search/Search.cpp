#include "Search.h"

#include "core/buffer/FileManager.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <BoostRegexSearch.h>
#include <Scintilla.h>

#include <QFileInfo>

#include <algorithm>

namespace npp::core {

namespace {

bool readBase(std::string_view s, size_t pos, int size, int base, int& value)
{
    if (pos + static_cast<size_t>(size) > s.size())
        return false;
    int v = 0;
    for (int i = 0; i < size; ++i) {
        const char c = s[pos + static_cast<size_t>(i)];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (d >= base)
            return false;
        v = v * base + d;
    }
    value = v;
    return true;
}

class DocSwitch {
public:
    DocSwitch(const IEditor& e, intptr_t doc) : m_e(e)
    {
        m_prev = e.send(SCI_GETDOCPOINTER);
        e.send(SCI_ADDREFDOCUMENT, 0, m_prev);
        e.send(SCI_SETDOCPOINTER, 0, doc);
    }
    ~DocSwitch()
    {
        m_e.send(SCI_SETDOCPOINTER, 0, m_prev);
        m_e.send(SCI_RELEASEDOCUMENT, 0, m_prev);
    }
private:
    const IEditor& m_e;
    intptr_t m_prev = 0;
};

intptr_t searchInTarget(const IEditor& e, const std::string& pattern, intptr_t start, intptr_t end)
{
    e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(start), end);
    return e.send(SCI_SEARCHINTARGET, pattern.size(), reinterpret_cast<intptr_t>(pattern.data()));
}

} // namespace

std::string convertExtended(std::string_view q)
{
    std::string out;
    for (size_t i = 0; i < q.size(); ++i) {
        const char c = q[i];
        if (c != '\\' || i + 1 >= q.size()) {
            out.push_back(c);
            continue;
        }
        const char n = q[++i];
        switch (n) {
        case 'r': out.push_back('\r'); break;
        case 'n': out.push_back('\n'); break;
        case '0': out.push_back('\0'); break;
        case 't': out.push_back('\t'); break;
        case '\\': out.push_back('\\'); break;
        case 'b': case 'd': case 'o': case 'x': case 'u': {
            const int size = n == 'b' ? 8 : n == 'o' ? 3 : n == 'd' ? 3 : n == 'x' ? 2 : 4;
            const int base = n == 'b' ? 2 : n == 'o' ? 8 : n == 'd' ? 10 : 16;
            int v = 0;
            if (readBase(q, i + 1, size, base, v)) {
                appendUtf8(out, static_cast<char32_t>(v)); // upstream builds a wchar_t, i.e. a code point
                i += static_cast<size_t>(size);
                break;
            }
            [[fallthrough]];
        }
        default: // unknown sequence: kept as typed
            out.push_back('\\');
            out.push_back(n);
            break;
        }
    }
    return out;
}

int searchFlags(const SearchOptions& o, FindNextType type)
{
    int f = (o.wholeWord ? SCFIND_WHOLEWORD : 0) | (o.matchCase ? SCFIND_MATCHCASE : 0);
    if (o.type == SearchType::Regex) {
        f |= SCFIND_REGEXP | SCFIND_POSIX;
        if (o.dotMatchesNewline)
            f |= SCFIND_REGEXP_DOTMATCHESNL;
    }
    switch (type) {
    case FindNextType::FindNext:
        f |= SCFIND_REGEXP_EMPTYMATCH_ALL | SCFIND_REGEXP_SKIPCRLFASONE;
        break;
    case FindNextType::ReplaceNext:
        f |= SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH | SCFIND_REGEXP_SKIPCRLFASONE;
        break;
    case FindNextType::FindNextForReplace:
        f |= SCFIND_REGEXP_EMPTYMATCH_ALL | SCFIND_REGEXP_EMPTYMATCH_ALLOWATSTART | SCFIND_REGEXP_SKIPCRLFASONE;
        break;
    }
    return f;
}

std::string searchPattern(const SearchOptions& o)
{
    return o.type == SearchType::Extended ? convertExtended(o.findWhat) : o.findWhat;
}

std::optional<SearchMatch> findNext(const IEditor& e, const SearchOptions& o, intptr_t from, FindNextType type)
{
    const std::string pattern = searchPattern(o);
    if (pattern.empty())
        return std::nullopt;
    const intptr_t len = e.send(SCI_GETLENGTH);
    int flags = searchFlags(o, type);
    // Never allow a zero length match in the middle of a CRLF (upstream)
    if (from > 0 && e.send(SCI_GETCHARAT, static_cast<uintptr_t>(from - 1)) == '\r'
        && e.send(SCI_GETCHARAT, static_cast<uintptr_t>(from)) == '\n')
        flags = (flags & ~SCFIND_REGEXP_EMPTYMATCH_MASK) | SCFIND_REGEXP_EMPTYMATCH_NONE;
    e.send(SCI_SETSEARCHFLAGS, static_cast<uintptr_t>(flags));

    intptr_t pos = o.directionDown ? searchInTarget(e, pattern, from, len) : searchInTarget(e, pattern, from, 0);
    bool wrapped = false;
    if (pos < 0 && o.wrap) {
        wrapped = true;
        pos = o.directionDown ? searchInTarget(e, pattern, 0, len) : searchInTarget(e, pattern, len, 0);
    }
    if (pos < 0)
        return std::nullopt;
    return SearchMatch{e.send(SCI_GETTARGETSTART), e.send(SCI_GETTARGETEND), wrapped};
}

std::vector<SearchMatch> findAll(const IEditor& e, const SearchOptions& o, intptr_t start, intptr_t end)
{
    std::vector<SearchMatch> out;
    const std::string pattern = searchPattern(o);
    if (pattern.empty())
        return out;
    if (end < 0)
        end = e.send(SCI_GETLENGTH);
    e.send(SCI_SETSEARCHFLAGS, static_cast<uintptr_t>(searchFlags(o, FindNextType::FindNext)));
    intptr_t pos = start;
    while (pos <= end) {
        if (searchInTarget(e, pattern, pos, end) < 0)
            break;
        const intptr_t s = e.send(SCI_GETTARGETSTART), t = e.send(SCI_GETTARGETEND);
        out.push_back({s, t, false});
        if (t > s)
            pos = t;
        else // empty match: step one character
            pos = e.send(SCI_POSITIONAFTER, static_cast<uintptr_t>(t)) > t ? e.send(SCI_POSITIONAFTER, static_cast<uintptr_t>(t)) : t + 1;
        if (pos > end || (t == end && t == s))
            break;
    }
    return out;
}

int countMatches(const IEditor& e, const SearchOptions& o)
{
    return static_cast<int>(findAll(e, o).size());
}

intptr_t replaceMatch(const IEditor& e, const SearchOptions& o, const SearchMatch& m)
{
    e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(m.start), m.end);
    if (o.type == SearchType::Regex) {
        e.send(SCI_REPLACETARGETRE, o.replaceWith.size(), reinterpret_cast<intptr_t>(o.replaceWith.data()));
    } else {
        const std::string r = o.type == SearchType::Extended ? convertExtended(o.replaceWith) : o.replaceWith;
        e.send(SCI_REPLACETARGET, r.size(), reinterpret_cast<intptr_t>(r.data()));
    }
    return e.send(SCI_GETTARGETEND);
}

int replaceAll(const IEditor& e, const SearchOptions& o, intptr_t start, intptr_t end)
{
    const std::string pattern = searchPattern(o);
    if (pattern.empty())
        return 0;
    if (end < 0)
        end = e.send(SCI_GETLENGTH);
    int count = 0;
    e.send(SCI_BEGINUNDOACTION);
    e.send(SCI_SETSEARCHFLAGS, static_cast<uintptr_t>(searchFlags(o, FindNextType::ReplaceNext)));
    intptr_t pos = start;
    while (pos <= end) {
        if (searchInTarget(e, pattern, pos, end) < 0)
            break;
        const intptr_t s = e.send(SCI_GETTARGETSTART), t = e.send(SCI_GETTARGETEND);
        const intptr_t newEnd = replaceMatch(e, o, {s, t, false});
        ++count;
        end += (newEnd - s) - (t - s); // document length change
        pos = newEnd;
        if (t == s) { // empty match: move past one character
            if (pos >= end)
                break;
            pos = e.send(SCI_POSITIONAFTER, static_cast<uintptr_t>(pos));
        }
    }
    e.send(SCI_ENDUNDOACTION);
    return count;
}

// --- Find in Files -----------------------------------------------------------------------------

std::vector<std::string> listFilesToSearch(const FindInFilesOptions& o)
{
    std::vector<std::string> patterns;
    for (std::string tok : tokenize(replaceAll(o.filters, ";", " "), ' '))
        patterns.push_back(tok);
    if (patterns.empty() || allPatternsAreExclusion(patterns))
        patterns.insert(patterns.begin(), "*.*");

    std::vector<std::string> out;
    std::vector<std::pair<fs::path, size_t>> dirs{{pathFromUtf8(o.directory), 1}};
    while (!dirs.empty()) {
        auto [dir, level] = dirs.back();
        dirs.pop_back();
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            const std::string p = nativeUtf8Path(entry.path());
            const std::string name = fileNameOf(p);
            if (!o.includeHidden && QFileInfo(QString::fromStdString(p)).isHidden())
                continue;
            std::error_code ec2;
            if (entry.is_directory(ec2)) {
                if (o.recursive && !matchInExcludeDirList(name, patterns, level))
                    dirs.emplace_back(entry.path(), level + 1);
            } else if (entry.is_regular_file(ec2)) {
                // "*.*" must also match names without a dot (PathMatchSpec semantics)
                std::vector<std::string> pats = patterns;
                for (auto& pt : pats)
                    if (pt == "*.*")
                        pt = "*";
                if (matchInList(name, pats))
                    out.push_back(p);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<LineHit> findInFiles(FileManager& fm, const std::vector<std::string>& files, const SearchOptions& o,
                                 const SearchProgress& progress)
{
    std::vector<LineHit> hits;
    // FileManager's scratch editor does the searching (same engine as the editor)
    const IEditor& e = fm.scratchEditor();
    for (size_t i = 0; i < files.size(); ++i) {
        if (progress && !progress(i, files.size(), files[i]))
            break;
        const bool wasOpen = fm.findByPath(files[i]) != nullptr;
        Buffer* b = fm.loadFile(files[i]);
        if (!b)
            continue;
        {
            DocSwitch sw(e, b->document);
            const auto matches = findAll(e, o);
            for (const auto& m : matches) {
                const intptr_t line = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(m.start));
                const intptr_t ls = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line));
                const intptr_t le = e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line));
                if (hits.empty() || hits.back().file != b->path || hits.back().line != static_cast<size_t>(line + 1))
                    hits.push_back({b->path, static_cast<size_t>(line + 1), e.textRange(ls, le), {}});
                const intptr_t me = std::min(m.end, le);
                hits.back().ranges.emplace_back(static_cast<size_t>(m.start - ls), static_cast<size_t>(me - ls));
            }
        }
        if (!wasOpen)
            fm.close(*b);
    }
    return hits;
}

int replaceInFiles(FileManager& fm, const std::vector<std::string>& files, const SearchOptions& o,
                   const SearchProgress& progress)
{
    int total = 0;
    const IEditor& e = fm.scratchEditor();
    for (size_t i = 0; i < files.size(); ++i) {
        if (progress && !progress(i, files.size(), files[i]))
            break;
        const bool wasOpen = fm.findByPath(files[i]) != nullptr;
        Buffer* b = fm.loadFile(files[i]);
        if (!b || b->isReadOnly())
            continue;
        int n = 0;
        {
            DocSwitch sw(e, b->document);
            n = replaceAll(e, o);
        }
        if (n > 0) {
            total += n;
            if (wasOpen)
                b->dirty = true; // open documents are changed, not saved (upstream behaviour)
            else
                fm.save(*b, b->path);
        }
        if (!wasOpen)
            fm.close(*b);
    }
    return total;
}

} // namespace npp::core
