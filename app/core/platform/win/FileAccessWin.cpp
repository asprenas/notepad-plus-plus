// Windows implementation of core/platform/FileAccess.h
#include "core/platform/FileAccess.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace npp::core {

namespace {

std::wstring widen(std::string_view s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    for (auto& c : w)
        if (c == L'/')
            c = L'\\';
    return w;
}

} // namespace

bool writeAccessDenied(std::string_view utf8Path)
{
    const std::wstring path = widen(utf8Path);
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))
            return false;
        const HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h);
            return false;
        }
        return ::GetLastError() == ERROR_ACCESS_DENIED;
    }
    // New file: can the folder take a file?
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return false;
    const std::wstring probe = path.substr(0, slash) + L"\\~npp-write-probe.tmp";
    const HANDLE h = ::CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h);
        return false;
    }
    return ::GetLastError() == ERROR_ACCESS_DENIED;
}

bool fileReadOnlyAttribute(std::string_view utf8Path)
{
    const DWORD attr = ::GetFileAttributesW(widen(utf8Path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0 && (attr & FILE_ATTRIBUTE_READONLY) != 0;
}

} // namespace npp::core
