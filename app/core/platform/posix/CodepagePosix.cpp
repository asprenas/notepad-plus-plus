// Linux / macOS implementation of core/encoding/Codepage.h (iconv).
#include "core/encoding/Codepage.h"

#include "core/encoding/UniMode.h"

#include <iconv.h>

#include <cerrno>
#include <map>
#include <mutex>
#include <vector>

namespace npp::core {

namespace {

// Candidate iconv names per Windows code page, most specific first (glibc and macOS libiconv
// do not agree on all names).
const std::vector<const char*>& candidates(int cp)
{
    static const std::map<int, std::vector<const char*>> kNames = {
        {1250, {"CP1250", "WINDOWS-1250"}}, {1251, {"CP1251", "WINDOWS-1251"}},
        {1252, {"CP1252", "WINDOWS-1252"}}, {1253, {"CP1253", "WINDOWS-1253"}},
        {1254, {"CP1254", "WINDOWS-1254"}}, {1255, {"CP1255", "WINDOWS-1255"}},
        {1256, {"CP1256", "WINDOWS-1256"}}, {1257, {"CP1257", "WINDOWS-1257"}},
        {1258, {"CP1258", "WINDOWS-1258"}},
        {28591, {"ISO-8859-1"}},  {28592, {"ISO-8859-2"}},  {28593, {"ISO-8859-3"}},
        {28594, {"ISO-8859-4"}},  {28595, {"ISO-8859-5"}},  {28596, {"ISO-8859-6"}},
        {28597, {"ISO-8859-7"}},  {28598, {"ISO-8859-8"}},  {28599, {"ISO-8859-9"}},
        {28603, {"ISO-8859-13"}}, {28604, {"ISO-8859-14"}}, {28605, {"ISO-8859-15"}},
        {437, {"CP437", "IBM437"}}, {720, {"CP720"}}, {737, {"CP737"}}, {775, {"CP775", "IBM775"}},
        {850, {"CP850", "IBM850"}}, {852, {"CP852", "IBM852"}}, {855, {"CP855", "IBM855"}},
        {857, {"CP857", "IBM857"}}, {858, {"CP858", "IBM00858", "CP850"}}, {860, {"CP860", "IBM860"}},
        {861, {"CP861", "IBM861"}}, {862, {"CP862", "IBM862"}}, {863, {"CP863", "IBM863"}},
        {865, {"CP865", "IBM865"}}, {866, {"CP866", "IBM866"}}, {869, {"CP869", "IBM869"}},
        {950, {"CP950", "BIG5"}}, {936, {"CP936", "GBK"}}, {932, {"CP932", "SHIFT_JIS"}},
        {949, {"CP949", "UHC"}}, {51949, {"EUC-KR"}}, {874, {"CP874", "TIS-620"}},
        {10007, {"MACCYRILLIC", "MAC-CYRILLIC", "x-mac-cyrillic"}},
        {21866, {"KOI8-U"}}, {20866, {"KOI8-R"}},
    };
    static const std::vector<const char*> kNone;
    auto it = kNames.find(cp);
    return it == kNames.end() ? kNone : it->second;
}

// Resolved iconv name per code page ("" = unsupported), cached.
const char* iconvName(int cp)
{
    static std::mutex m;
    static std::map<int, const char*> cache;
    std::lock_guard<std::mutex> lock(m);
    if (auto it = cache.find(cp); it != cache.end())
        return it->second;
    const char* found = "";
    for (const char* name : candidates(cp)) {
        iconv_t h = iconv_open("UTF-8", name);
        if (h != reinterpret_cast<iconv_t>(-1)) {
            iconv_close(h);
            found = name;
            break;
        }
    }
    cache[cp] = found;
    return found;
}

// Converts `in` from `from` to `to`. Invalid / unrepresentable input is replaced with
// `replacement` and reported through *lossy.
std::optional<std::string> convert(std::string_view in, const char* to, const char* from,
                                   std::string_view replacement, bool* lossy)
{
    iconv_t h = iconv_open(to, from);
    if (h == reinterpret_cast<iconv_t>(-1))
        return std::nullopt;

    std::string out;
    out.reserve(in.size() * 2);
    std::vector<char> buf(16 * 1024);
    char* src = const_cast<char*>(in.data());
    size_t srcLeft = in.size();
    while (srcLeft > 0) {
        char* dst = buf.data();
        size_t dstLeft = buf.size();
        const size_t r = iconv(h, &src, &srcLeft, &dst, &dstLeft);
        out.append(buf.data(), buf.size() - dstLeft);
        if (r == static_cast<size_t>(-1)) {
            if (errno == E2BIG)
                continue;
            // EILSEQ / EINVAL: skip one input unit (one UTF-8 sequence when the source is UTF-8).
            if (lossy)
                *lossy = true;
            out.append(replacement);
            size_t skip = 1;
            if (std::string_view(from) == "UTF-8") {
                const auto c = static_cast<unsigned char>(*src);
                skip = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
                if (skip > srcLeft) skip = srcLeft;
            }
            src += skip;
            srcLeft -= skip;
            iconv(h, nullptr, nullptr, nullptr, nullptr); // reset shift state
        }
    }
    // flush
    char* dst = buf.data();
    size_t dstLeft = buf.size();
    iconv(h, nullptr, nullptr, &dst, &dstLeft);
    out.append(buf.data(), buf.size() - dstLeft);
    iconv_close(h);
    return out;
}

} // namespace

bool isCodePageSupported(int codePage)
{
    return codePage == kCodePageUtf8 || *iconvName(codePage) != '\0';
}

std::optional<std::string> codePageToUtf8(std::string_view bytes, int codePage)
{
    if (codePage == kCodePageUtf8)
        return std::string(bytes);
    const char* name = iconvName(codePage);
    if (!*name)
        return std::nullopt;
    return convert(bytes, "UTF-8", name, "\xEF\xBF\xBD", nullptr);
}

std::optional<std::string> utf8ToCodePage(std::string_view utf8, int codePage, bool* lossy)
{
    if (lossy)
        *lossy = false;
    if (codePage == kCodePageUtf8)
        return std::string(utf8);
    const char* name = iconvName(codePage);
    if (!*name)
        return std::nullopt;
    return convert(utf8, name, "UTF-8", "?", lossy);
}

int systemAnsiCodePage()
{
    return 1252;
}

} // namespace npp::core
