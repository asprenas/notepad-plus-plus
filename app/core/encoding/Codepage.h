// Code page <-> UTF-8 conversion.
// Windows: Win32 MultiByteToWideChar / WideCharToMultiByte (platform/win).
// Linux, macOS: iconv (platform/posix). The Qt Windows builds ship without ICU, so
// QStringConverter cannot be used for legacy code pages (measured in phase 1).
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace npp::core {

// True if this platform can convert the given Windows code page number.
bool isCodePageSupported(int codePage);

// Bytes in `codePage` -> UTF-8. nullopt if the code page is unsupported.
// Invalid sequences become U+FFFD.
std::optional<std::string> codePageToUtf8(std::string_view bytes, int codePage);

// UTF-8 -> bytes in `codePage`. Characters that cannot be represented become '?'
// and *lossy is set. nullopt if the code page is unsupported.
std::optional<std::string> utf8ToCodePage(std::string_view utf8, int codePage, bool* lossy = nullptr);

// Code page used for "ANSI" documents (encoding -1): the system ANSI code page on Windows,
// 1252 elsewhere (UTF-8 systems have no ANSI code page; 1252 matches upstream's common default).
int systemAnsiCodePage();

} // namespace npp::core
