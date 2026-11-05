#include "PathUtil.h"

#include "StringUtil.h"

#include <QtGlobal>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <thread>

namespace npp::core {

fs::path pathFromUtf8(std::string_view utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8FromPath(const fs::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::string nativeUtf8Path(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

namespace {

bool isSep(char c) noexcept
{
#if defined(Q_OS_WIN)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

constexpr char kNativeSep =
#if defined(Q_OS_WIN)
    '\\';
#else
    '/';
#endif

} // namespace

std::string pathAppend(std::string_view base, std::string_view append)
{
    if (base.empty() && append.empty())
        return std::string(1, kNativeSep);
    if (base.empty())
        return std::string(append);
    if (append.empty())
        return std::string(base);
    const bool baseSep = isSep(base.back());
    const bool appSep = isSep(append.front());
    std::string r(base);
    if (baseSep && appSep)
        r.pop_back();
    else if (!baseSep && !appSep)
        r.push_back(kNativeSep);
    r.append(append);
    return r;
}

std::string parentPath(std::string_view path)
{
    const fs::path p = pathFromUtf8(path);
    if (!p.has_filename())
        return std::string(path);
    fs::path parent = p.parent_path();
    const std::u8string u = parent.u8string();
    return std::string(u.begin(), u.end());
}

std::string fileNameOf(std::string_view path)
{
    const std::u8string u = pathFromUtf8(path).filename().u8string();
    return std::string(u.begin(), u.end());
}

std::string extensionOf(std::string_view path)
{
    const std::u8string u = pathFromUtf8(path).extension().u8string();
    return u.empty() ? std::string() : std::string(u.begin() + 1, u.end());
}

std::string absolutePath(std::string_view path)
{
    std::error_code ec;
    fs::path abs = fs::absolute(pathFromUtf8(path), ec);
    if (ec)
        abs = pathFromUtf8(path);
    return nativeUtf8Path(abs.lexically_normal());
}

bool isUncPath(std::string_view path) noexcept
{
    return path.size() > 2 && (path[0] == '\\' || path[0] == '/') && path[1] == path[0];
}

bool fileNamesAreCaseInsensitive() noexcept
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool samePath(std::string_view a, std::string_view b)
{
    const std::string na = absolutePath(a), nb = absolutePath(b);
    return fileNamesAreCaseInsensitive() ? iequalsUtf8(na, nb) : na == nb;
}

namespace {

// Iterative '*'/'?' matcher with backtracking on the last '*'.
bool matchOne(std::string_view s, std::string_view p)
{
    size_t si = 0, pi = 0, star = std::string_view::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && (p[pi] == '?' || asciiToLower(p[pi]) == asciiToLower(s[si]))) {
            ++si;
            ++pi;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = si;
        } else if (star != std::string_view::npos) {
            pi = star + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*')
        ++pi;
    return pi == p.size();
}

} // namespace

bool wildcardMatch(std::string_view name, std::string_view spec)
{
    for (const auto& pattern : split(spec, ";")) {
        const std::string pat = trimmed(pattern);
        if (!pat.empty() && matchOne(name, pat))
            return true;
    }
    return false;
}

bool matchInList(std::string_view fileName, const std::vector<std::string>& patterns)
{
    bool matched = false;
    for (const auto& p : patterns) {
        if (p.size() > 1 && p[0] == '!') {
            if (wildcardMatch(fileName, std::string_view(p).substr(1)))
                return false;
            continue;
        }
        if (wildcardMatch(fileName, p))
            matched = true;
    }
    return matched;
}

bool matchInExcludeDirList(std::string_view dirName, const std::vector<std::string>& patterns, size_t level)
{
    const auto sep = [](char c) { return c == '\\' || c == '/'; };
    for (const auto& p : patterns) {
        if (p.size() > 3 && p[0] == '!' && p[1] == '+' && sep(p[2])) {
            if (wildcardMatch(dirName, std::string_view(p).substr(3)))
                return true;
        } else if (p.size() > 2 && p[0] == '!' && sep(p[1])) {
            if (level == 1 && wildcardMatch(dirName, std::string_view(p).substr(2)))
                return true;
        }
    }
    return false;
}

bool allPatternsAreExclusion(const std::vector<std::string>& patterns)
{
    for (const auto& p : patterns)
        if (p.empty() || p[0] != '!')
            return false;
    return true;
}

std::optional<std::string> readFileContent(std::string_view path)
{
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool writeFileContent(std::string_view path, std::string_view content)
{
    std::ofstream out(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

bool pathExists(std::string_view path, PathKind kind, std::chrono::milliseconds timeout, bool* timedOut)
{
    if (timedOut)
        *timedOut = false;
    const fs::path p = pathFromUtf8(path);
    auto check = [p, kind]() {
        std::error_code ec;
        const fs::file_status st = fs::status(p, ec);
        if (ec || !fs::exists(st))
            return false;
        switch (kind) {
        case PathKind::File:      return fs::is_regular_file(st);
        case PathKind::Directory: return fs::is_directory(st);
        case PathKind::Any:       return true;
        }
        return true;
    };
    if (timeout.count() <= 0)
        return check();

    // The worker may outlive us on timeout (hung network share): it owns everything it uses.
    auto task = std::make_shared<std::packaged_task<bool()>>(check);
    std::future<bool> result = task->get_future();
    std::thread([task] { (*task)(); }).detach();
    if (result.wait_for(timeout) != std::future_status::ready) {
        if (timedOut)
            *timedOut = true;
        return false;
    }
    return result.get();
}

std::string expandEnvironment(std::string_view in)
{
    std::string out;
    size_t i = 0;
#if !defined(Q_OS_WIN)
    if (!in.empty() && in[0] == '~' && (in.size() == 1 || in[1] == '/')) {
        if (const char* home = std::getenv("HOME")) {
            out = home;
            i = 1;
        }
    }
#endif
    while (i < in.size()) {
        const char c = in[i];
        if (c == '%') {
            const size_t end = in.find('%', i + 1);
            if (end != std::string_view::npos && end > i + 1) {
                const std::string name(in.substr(i + 1, end - i - 1));
                if (const char* v = std::getenv(name.c_str())) {
                    out += v;
                    i = end + 1;
                    continue;
                }
            }
        }
#if !defined(Q_OS_WIN)
        if (c == '$' && i + 1 < in.size()) {
            size_t start = i + 1, end;
            bool braces = in[start] == '{';
            if (braces) {
                end = in.find('}', start + 1);
                if (end != std::string_view::npos) {
                    const std::string name(in.substr(start + 1, end - start - 1));
                    if (const char* v = std::getenv(name.c_str())) {
                        out += v;
                        i = end + 1;
                        continue;
                    }
                }
            } else {
                end = start;
                while (end < in.size() && (std::isalnum(static_cast<unsigned char>(in[end])) || in[end] == '_'))
                    ++end;
                if (end > start) {
                    const std::string name(in.substr(start, end - start));
                    if (const char* v = std::getenv(name.c_str())) {
                        out += v;
                        i = end;
                        continue;
                    }
                }
            }
        }
#endif
        out.push_back(c);
        ++i;
    }
    return out;
}

std::string compactPath(std::string_view path, size_t maxChars)
{
    if (utf8CodePointCount(path) <= maxChars || maxChars < 5)
        return std::string(path);
    // Keep the file name; elide the middle of the directory part.
    const std::string name = fileNameOf(path);
    const size_t nameLen = utf8CodePointCount(name);
    if (nameLen + 4 >= maxChars) {
        // Even the name is too long: keep its tail.
        std::string r = "...";
        const size_t keep = maxChars - 3;
        size_t cps = nameLen, pos = 0;
        while (cps > keep && pos < name.size()) {
            pos += utf8SequenceLength(static_cast<unsigned char>(name[pos]));
            --cps;
        }
        return r + name.substr(pos);
    }
    const size_t headCps = maxChars - nameLen - 4; // "...", separator
    std::string head;
    size_t pos = 0, cps = 0;
    while (cps < headCps && pos < path.size()) {
        const size_t len = utf8SequenceLength(static_cast<unsigned char>(path[pos]));
        head.append(path.substr(pos, len));
        pos += len;
        ++cps;
    }
    return head + "..." + std::string(1, kNativeSep) + name;
}

std::string buildMenuFileName(int filenameLen, unsigned int pos, std::string_view filename, bool ordinalNumber)
{
    std::string r;
    if (ordinalNumber) {
        if (pos < 9) {
            r.push_back('&');
            r.push_back(static_cast<char>('1' + pos));
        } else if (pos == 9) {
            r.append("1&0");
        } else {
            r.append(std::to_string((pos + 1) / 10));
            r.push_back('&');
            r.append(std::to_string((pos + 1) % 10));
        }
        r.append(": ");
    } else {
        r.push_back('&');
    }

    std::string label;
    if (filenameLen > 0)
        label = compactPath(filename, static_cast<size_t>(filenameLen));
    else if (filenameLen == 0)
        label = fileNameOf(filename);
    else
        label = std::string(filename);
    r.append(escapeMenuAmpersands(label));
    return r;
}

} // namespace npp::core
