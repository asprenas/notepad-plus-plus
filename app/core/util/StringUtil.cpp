#include "StringUtil.h"

#include <QString>

#include <algorithm>
#include <charconv>

namespace npp::core {

char asciiToLower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
char asciiToUpper(char c) noexcept { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

std::string asciiLower(std::string_view s)
{
    std::string r(s);
    for (char& c : r) c = asciiToLower(c);
    return r;
}

std::string asciiUpper(std::string_view s)
{
    std::string r(s);
    for (char& c : r) c = asciiToUpper(c);
    return r;
}

bool iequalsAscii(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (asciiToLower(a[i]) != asciiToLower(b[i]))
            return false;
    return true;
}

bool istartsWithAscii(std::string_view s, std::string_view prefix) noexcept
{
    return s.size() >= prefix.size() && iequalsAscii(s.substr(0, prefix.size()), prefix);
}

bool iendsWithAscii(std::string_view s, std::string_view suffix) noexcept
{
    return s.size() >= suffix.size() && iequalsAscii(s.substr(s.size() - suffix.size()), suffix);
}

// Unicode case mapping is delegated to QString (QtCore), which implements full
// Unicode simple case mapping on every platform.
std::string utf8Lower(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())).toLower().toStdString();
}

std::string utf8Upper(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())).toUpper().toStdString();
}

bool iequalsUtf8(std::string_view a, std::string_view b)
{
    return QString::fromUtf8(a.data(), static_cast<qsizetype>(a.size()))
               .compare(QString::fromUtf8(b.data(), static_cast<qsizetype>(b.size())), Qt::CaseInsensitive) == 0;
}

size_t utf8SequenceLength(unsigned char c) noexcept
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

bool isValidUtf8(std::string_view s) noexcept
{
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        size_t len = 0;
        char32_t cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // reject overlong forms, surrogates and out of range values
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)
            || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

size_t utf8CodePointCount(std::string_view s) noexcept
{
    size_t count = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++count;
    return count;
}

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::vector<std::string> split(std::string_view input, std::string_view delimiter)
{
    std::vector<std::string> out;
    if (delimiter.empty()) {
        out.emplace_back(input);
        return out;
    }
    size_t start = 0;
    for (size_t pos; (pos = input.find(delimiter, start)) != std::string_view::npos; start = pos + delimiter.size())
        out.emplace_back(input.substr(start, pos - start));
    out.emplace_back(input.substr(start));
    return out;
}

std::vector<std::string> tokenize(std::string_view input, char delim)
{
    std::vector<std::string> tokens;
    size_t last = input.find_first_not_of(delim, 0);
    size_t pos = input.find_first_of(delim, last);
    while (pos != std::string_view::npos || last != std::string_view::npos) {
        tokens.emplace_back(input.substr(last, pos - last));
        last = input.find_first_not_of(delim, pos);
        pos = input.find_first_of(delim, last);
    }
    return tokens;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator)
{
    std::string r;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) r.append(separator);
        r.append(parts[i]);
    }
    return r;
}

void trimInPlace(std::string& s)
{
    const auto notSpace = [](char c) { return c != ' ' && c != '\t'; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

std::string trimmed(std::string_view s)
{
    std::string r(s);
    trimInPlace(r);
    return r;
}

std::string replaceAll(std::string subject, std::string_view search, std::string_view replace)
{
    if (search.empty())
        return subject;
    for (size_t pos = 0; (pos = subject.find(search, pos)) != std::string::npos; pos += replace.size())
        subject.replace(pos, search.size(), replace);
    return subject;
}

std::string takeWhileAdmissible(std::string_view input, std::string_view admissible)
{
    const size_t idx = input.find_first_not_of(admissible);
    return std::string(idx == std::string_view::npos ? input : input.substr(0, idx));
}

bool isInList(std::string_view token, std::string_view list)
{
    if (token.empty())
        return false;
    for (const auto& word : tokenize(list, ' '))
        if (iequalsAscii(token, word))
            return true;
    return false;
}

bool parseNumberList(std::string_view s, std::vector<size_t>& out)
{
    out.clear();
    for (char c : s)
        if (c != ' ' && (c < '0' || c > '9'))
            return false;
    for (const auto& tok : split(s, " "))
        if (!tok.empty() && tok.size() < 5)
            out.push_back(static_cast<size_t>(std::stoi(tok)));
    return true;
}

std::optional<long long> parseInt(std::string_view s) noexcept
{
    const std::string t = trimmed(s);
    long long v = 0;
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc() || p != t.data() + t.size() || t.empty())
        return std::nullopt;
    return v;
}

bool isNumber(std::string_view s) noexcept
{
    if (!s.empty() && s[0] == '-')
        s.remove_prefix(1);
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

int nbDigitsFromNbLines(size_t nbLines) noexcept
{
    int nbDigits = 1;
    while (nbLines >= 10) {
        nbLines /= 10;
        ++nbDigits;
    }
    return nbDigits;
}

std::string commafy(size_t n)
{
    std::string digits = std::to_string(n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

std::string purgeMenuItemString(std::string_view menuItem, bool keepAmpersand)
{
    std::string r;
    for (char c : menuItem) {
        if (c == '\t')
            break;
        if (c == '&' && !keepAmpersand)
            continue;
        r.push_back(c);
    }
    return r;
}

std::string escapeMenuAmpersands(std::string_view s)
{
    return replaceAll(std::string(s), "&", "&&");
}

} // namespace npp::core
