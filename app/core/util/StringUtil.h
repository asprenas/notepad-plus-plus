// UTF-8 string utilities (port of the portable parts of MISC/Common/Common.*).
// All strings are UTF-8 encoded std::string (project string policy, plan appendix A).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

// --- ASCII case helpers (locale independent) ---------------------------------
char asciiToLower(char c) noexcept;
char asciiToUpper(char c) noexcept;
std::string asciiLower(std::string_view s);
std::string asciiUpper(std::string_view s);
bool iequalsAscii(std::string_view a, std::string_view b) noexcept;
bool istartsWithAscii(std::string_view s, std::string_view prefix) noexcept;
bool iendsWithAscii(std::string_view s, std::string_view suffix) noexcept;

// --- Unicode aware case conversion (UTF-8 in, UTF-8 out) ---------------------
std::string utf8Lower(std::string_view s);
std::string utf8Upper(std::string_view s);
// Case-insensitive comparison with Unicode case folding.
bool iequalsUtf8(std::string_view a, std::string_view b);

// --- UTF-8 validation / measurement ------------------------------------------
bool isValidUtf8(std::string_view s) noexcept;
size_t utf8CodePointCount(std::string_view s) noexcept;
// Length in bytes of the UTF-8 sequence starting with lead byte c (1 for invalid lead bytes).
size_t utf8SequenceLength(unsigned char c) noexcept;
// Appends the UTF-8 encoding of code point cp (invalid values become U+FFFD).
void appendUtf8(std::string& out, char32_t cp);

// --- splitting / joining / trimming ------------------------------------------
// Splits on every occurrence of delimiter (empty fields kept), like upstream stringSplit.
std::vector<std::string> split(std::string_view input, std::string_view delimiter);
// Splits on runs of delim, empty tokens dropped (upstream tokenizeString).
std::vector<std::string> tokenize(std::string_view input, char delim);
std::string join(const std::vector<std::string>& parts, std::string_view separator);
void trimInPlace(std::string& s);            // spaces and tabs, like upstream trim()
std::string trimmed(std::string_view s);
std::string replaceAll(std::string subject, std::string_view search, std::string_view replace);
// Longest prefix of input made of admissible characters (upstream stringTakeWhileAdmissable).
std::string takeWhileAdmissible(std::string_view input, std::string_view admissible);

// --- lists / numbers ----------------------------------------------------------
// True if token equals (ASCII case-insensitive) one of the space separated words in list.
bool isInList(std::string_view token, std::string_view list);
// "12 3  45" -> {12,3,45}; false if any char other than digits/space (upstream str2numberVector).
bool parseNumberList(std::string_view s, std::vector<size_t>& out);
std::optional<long long> parseInt(std::string_view s) noexcept;
bool isNumber(std::string_view s) noexcept;  // optional leading '-', then digits only
int nbDigitsFromNbLines(size_t nbLines) noexcept;
// Digit grouping with ',' (upstream uses the system locale; the UI may re-format).
std::string commafy(size_t n);

// --- menu text helpers ---------------------------------------------------------
// Removes '&' accelerators (unless keepAmpersand) and everything after '\t'.
std::string purgeMenuItemString(std::string_view menuItem, bool keepAmpersand = false);
// Escapes '&' as "&&" so file names display literally in menus.
std::string escapeMenuAmpersands(std::string_view s);

template <typename T>
size_t removeDuplicatesKeepOrder(std::vector<T>& v)
{
    size_t removed = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        for (size_t j = i + 1; j < v.size();) {
            if (v[j] == v[i]) { v.erase(v.begin() + static_cast<std::ptrdiff_t>(j)); ++removed; }
            else ++j;
        }
    }
    return removed;
}

} // namespace npp::core
