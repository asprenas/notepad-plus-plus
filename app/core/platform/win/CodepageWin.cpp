// Windows implementation of core/encoding/Codepage.h (Win32 code page API).
#include "core/encoding/Codepage.h"

#include "core/encoding/UniMode.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace npp::core {

bool isCodePageSupported(int codePage)
{
    return codePage > 0 && ::IsValidCodePage(static_cast<UINT>(codePage));
}

std::optional<std::string> codePageToUtf8(std::string_view bytes, int codePage)
{
    if (codePage == kCodePageUtf8)
        return std::string(bytes);
    if (!isCodePageSupported(codePage))
        return std::nullopt;
    if (bytes.empty())
        return std::string();

    const int srcLen = static_cast<int>(bytes.size());
    const int wlen = ::MultiByteToWideChar(static_cast<UINT>(codePage), 0, bytes.data(), srcLen, nullptr, 0);
    if (wlen <= 0)
        return std::nullopt;
    std::vector<wchar_t> wide(static_cast<size_t>(wlen));
    ::MultiByteToWideChar(static_cast<UINT>(codePage), 0, bytes.data(), srcLen, wide.data(), wlen);

    const int ulen = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(ulen), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out.data(), ulen, nullptr, nullptr);
    return out;
}

std::optional<std::string> utf8ToCodePage(std::string_view utf8, int codePage, bool* lossy)
{
    if (lossy)
        *lossy = false;
    if (codePage == kCodePageUtf8)
        return std::string(utf8);
    if (!isCodePageSupported(codePage))
        return std::nullopt;
    if (utf8.empty())
        return std::string();

    const int srcLen = static_cast<int>(utf8.size());
    const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, nullptr, 0);
    if (wlen <= 0)
        return std::nullopt;
    std::vector<wchar_t> wide(static_cast<size_t>(wlen));
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, wide.data(), wlen);

    BOOL usedDefault = FALSE;
    // lpUsedDefaultChar must be null for some code pages (e.g. 50220+); none of the menu ones.
    const int blen = ::WideCharToMultiByte(static_cast<UINT>(codePage), 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(blen), '\0');
    ::WideCharToMultiByte(static_cast<UINT>(codePage), 0, wide.data(), wlen, out.data(), blen, nullptr, &usedDefault);
    if (lossy)
        *lossy = usedDefault != FALSE;
    return out;
}

int systemAnsiCodePage()
{
    return static_cast<int>(::GetACP());
}

} // namespace npp::core
