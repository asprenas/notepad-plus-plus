// Unicode mode of a document (upstream NppConstants.h enum UniMode).
// Numeric values are persisted (session.xml, config.xml) and must not change.
#pragma once

#include <cstdint>

namespace npp::core {

enum class UniMode : int {
    Ansi       = 0, // uni8Bit:       ANSI / code page
    Utf8Bom    = 1, // uniUTF8:       UTF-8 with BOM
    Utf16BE    = 2, // uni16BE:       UTF-16 big endian with BOM
    Utf16LE    = 3, // uni16LE:       UTF-16 little endian with BOM
    Utf8       = 4, // uniUTF8_NoBOM: UTF-8 without BOM
    Ascii7     = 5, // uni7Bit:       0 - 127 ASCII
    Utf16BENoBom = 6,
    Utf16LENoBom = 7,
};

inline bool isUtf16(UniMode m) noexcept
{
    return m == UniMode::Utf16BE || m == UniMode::Utf16LE || m == UniMode::Utf16BENoBom || m == UniMode::Utf16LENoBom;
}

inline bool isBigEndian(UniMode m) noexcept { return m == UniMode::Utf16BE || m == UniMode::Utf16BENoBom; }

inline bool hasBom(UniMode m) noexcept
{
    return m == UniMode::Utf8Bom || m == UniMode::Utf16BE || m == UniMode::Utf16LE;
}

// Scintilla code page constant for UTF-8 (SC_CP_UTF8), also used by upstream as "encoding".
inline constexpr int kCodePageUtf8 = 65001;

} // namespace npp::core
