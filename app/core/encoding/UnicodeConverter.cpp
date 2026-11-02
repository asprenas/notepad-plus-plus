#include "UnicodeConverter.h"

#include "core/util/StringUtil.h"

namespace npp::core {

namespace {

constexpr char kBomUtf8[] = "\xEF\xBB\xBF";
constexpr char kBomBE[] = "\xFE\xFF";
constexpr char kBomLE[] = "\xFF\xFE";

inline uint8_t byteAt(std::string_view s, size_t i) noexcept { return static_cast<uint8_t>(s[i]); }

} // namespace

UniMode detectBom(std::string_view d) noexcept
{
    if (d.size() > 1 && byteAt(d, 0) == 0xFE && byteAt(d, 1) == 0xFF)
        return UniMode::Utf16BE;
    if (d.size() > 1 && byteAt(d, 0) == 0xFF && byteAt(d, 1) == 0xFE)
        return UniMode::Utf16LE;
    if (d.size() > 2 && byteAt(d, 0) == 0xEF && byteAt(d, 1) == 0xBB && byteAt(d, 2) == 0xBF)
        return UniMode::Utf8Bom;
    return UniMode::Ansi;
}

size_t bomSize(UniMode mode) noexcept
{
    switch (mode) {
    case UniMode::Utf8Bom: return 3;
    case UniMode::Utf16BE:
    case UniMode::Utf16LE: return 2;
    default:               return 0;
    }
}

std::string_view bomBytes(UniMode mode) noexcept
{
    switch (mode) {
    case UniMode::Utf8Bom: return {kBomUtf8, 3};
    case UniMode::Utf16BE: return {kBomBE, 2};
    case UniMode::Utf16LE: return {kBomLE, 2};
    default:               return {};
    }
}

Utf8Class classifyUtf8(std::string_view d) noexcept
{
    // Same rules as upstream utf8_7bits_8bits(): NUL means "not UTF-8"; no overlong check.
    bool ascii7only = true;
    size_t i = 0;
    const size_t n = d.size();
    while (i < n) {
        const uint8_t c = byteAt(d, i);
        if (c == 0)
            return Utf8Class::Ascii8;
        if ((c & 0x80) == 0) {
            ++i;
            continue;
        }
        ascii7only = false;
        size_t len;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return Utf8Class::Ascii8;
        if (n - i < len)
            return Utf8Class::Ascii8;
        for (size_t k = 1; k < len; ++k)
            if ((byteAt(d, i + k) & 0xC0) != 0x80)
                return Utf8Class::Ascii8;
        i += len;
    }
    return ascii7only ? Utf8Class::Ascii7 : Utf8Class::Utf8NoBom;
}

bool looksLikeUtf16LE(std::string_view d) noexcept
{
    // Text in UTF-16LE (mostly Latin script) has many 0x00 bytes at odd offsets and almost none
    // at even offsets. Require: even size, first unit is a non-NUL low byte, >= 50% zero high
    // bytes, <= 5% zero low bytes, and no "\0\0" units.
    const size_t n = d.size();
    if (n < 2 || n % 2 != 0 || byteAt(d, 0) == 0 || byteAt(d, 1) != 0)
        return false;
    size_t units = n / 2, zeroHigh = 0, zeroLow = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        const bool lowZero = byteAt(d, i) == 0;
        const bool highZero = byteAt(d, i + 1) == 0;
        if (lowZero && highZero)
            return false;
        zeroLow += lowZero;
        zeroHigh += highZero;
    }
    return zeroHigh * 2 >= units && zeroLow * 20 <= units;
}

UniMode detectUnicodeMode(std::string_view d) noexcept
{
    const UniMode bom = detectBom(d);
    if (bom != UniMode::Ansi)
        return bom;
    if (looksLikeUtf16LE(d))
        return UniMode::Utf16LENoBom;
    switch (classifyUtf8(d)) {
    case Utf8Class::Utf8NoBom: return UniMode::Utf8;
    case Utf8Class::Ascii7:    return UniMode::Ascii7;
    case Utf8Class::Ascii8:    return UniMode::Ansi;
    }
    return UniMode::Ansi;
}

void UnicodeReader::emitUnit(uint16_t unit, std::string& out)
{
    if (m_highSurrogate) {
        if (unit >= 0xDC00 && unit < 0xE000) {
            const char32_t cp = 0x10000 + ((static_cast<char32_t>(m_highSurrogate) & 0x3FF) << 10) + (unit & 0x3FF);
            m_highSurrogate = 0;
            appendUtf8(out, cp);
            return;
        }
        appendUtf8(out, 0xFFFD); // lone high surrogate
        m_highSurrogate = 0;
    }
    if (unit >= 0xD800 && unit < 0xDC00) {
        m_highSurrogate = unit;
        return;
    }
    appendUtf8(out, unit >= 0xDC00 && unit < 0xE000 ? char32_t(0xFFFD) : char32_t(unit));
}

void UnicodeReader::convert(std::string_view block, std::string& out)
{
    if (m_firstBlock) {
        if (!m_modeKnown) {
            m_mode = detectUnicodeMode(block);
            m_modeKnown = true;
        }
        m_checkingBom = !bomBytes(m_mode).empty();
        m_firstBlock = false;
    }

    // Skip the BOM of the mode, even when it is split across blocks.
    if (m_checkingBom) {
        const std::string_view bom = bomBytes(m_mode);
        while (m_bomMatched < bom.size() && !block.empty()) {
            if (block[0] != bom[m_bomMatched]) {
                // Not a BOM after all: process the bytes matched so far as data.
                m_checkingBom = false;
                process(bom.substr(0, m_bomMatched), out);
                break;
            }
            ++m_bomMatched;
            block.remove_prefix(1);
        }
        if (m_bomMatched == bom.size())
            m_checkingBom = false;
        if (m_checkingBom)
            return; // block exhausted inside the BOM
    }
    process(block, out);
}

void UnicodeReader::process(std::string_view block, std::string& out)
{
    if (!isUtf16(m_mode)) {
        out.append(block);
        return;
    }

    const bool be = isBigEndian(m_mode);
    size_t i = 0;
    if (m_hasPendingByte && !block.empty()) {
        const uint8_t b0 = m_pendingByte, b1 = byteAt(block, 0);
        emitUnit(be ? static_cast<uint16_t>((b0 << 8) | b1) : static_cast<uint16_t>((b1 << 8) | b0), out);
        m_hasPendingByte = false;
        i = 1;
    }
    out.reserve(out.size() + (block.size() - i) * 3 / 2);
    for (; i + 1 < block.size(); i += 2) {
        const uint8_t b0 = byteAt(block, i), b1 = byteAt(block, i + 1);
        emitUnit(be ? static_cast<uint16_t>((b0 << 8) | b1) : static_cast<uint16_t>((b1 << 8) | b0), out);
    }
    if (i < block.size()) {
        m_pendingByte = byteAt(block, i);
        m_hasPendingByte = true;
    }
}

void UnicodeReader::finish(std::string& out)
{
    if (m_highSurrogate) {
        appendUtf8(out, 0xFFFD);
        m_highSurrogate = 0;
    }
    if (m_hasPendingByte) {
        appendUtf8(out, 0xFFFD);
        m_hasPendingByte = false;
    }
}

std::string encodeFromUtf8(std::string_view utf8, UniMode mode, bool includeBom)
{
    std::string out;
    if (includeBom)
        out.append(bomBytes(mode));
    if (!isUtf16(mode)) {
        out.append(utf8);
        return out;
    }

    const bool be = isBigEndian(mode);
    auto put = [&out, be](uint16_t u) {
        if (be) {
            out.push_back(static_cast<char>(u >> 8));
            out.push_back(static_cast<char>(u & 0xFF));
        } else {
            out.push_back(static_cast<char>(u & 0xFF));
            out.push_back(static_cast<char>(u >> 8));
        }
    };

    out.reserve(out.size() + utf8.size() * 2);
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        const uint8_t c = byteAt(utf8, i);
        char32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { put(0xFFFD); ++i; continue; }

        bool ok = i + len <= n;
        for (size_t k = 1; ok && k < len; ++k) {
            const uint8_t cc = byteAt(utf8, i + k);
            if ((cc & 0xC0) != 0x80)
                ok = false;
            else
                cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            put(0xFFFD);
            ++i;
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put(static_cast<uint16_t>(0xD800 | (cp >> 10)));
            put(static_cast<uint16_t>(0xDC00 | (cp & 0x3FF)));
        } else {
            put(static_cast<uint16_t>(cp));
        }
        i += len;
    }
    return out;
}

std::string decodeToUtf8(std::string_view data, UniMode mode)
{
    UnicodeReader reader;
    reader.setMode(mode);
    std::string out;
    reader.convert(data, out);
    reader.finish(out);
    return out;
}

} // namespace npp::core
