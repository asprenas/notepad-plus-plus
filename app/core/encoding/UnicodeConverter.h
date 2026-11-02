// Unicode BOM detection and UTF-16 <-> UTF-8 conversion (port of upstream Utf8_16.*).
//
// Differences from upstream (documented in docs/phase-1-core-extraction.md):
//  - UTF-16LE-without-BOM detection uses a statistical heuristic instead of Win32 IsTextUnicode().
//  - Unpaired UTF-16 surrogates and invalid UTF-8 become U+FFFD instead of being dropped.
//  - The "skip BOM" state is per instance (upstream used a function-static variable).
#pragma once

#include "core/encoding/UniMode.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace npp::core {

// BOM only: Utf16BE / Utf16LE / Utf8Bom, or Ansi when there is none (upstream determineEncodingFromBOM).
UniMode detectBom(std::string_view data) noexcept;
size_t bomSize(UniMode mode) noexcept;
std::string_view bomBytes(UniMode mode) noexcept;

// Classification of a buffer without BOM (upstream utf8_7bits_8bits).
enum class Utf8Class { Utf8NoBom, Ascii7, Ascii8 };
Utf8Class classifyUtf8(std::string_view data) noexcept;

// Full detection on the first block of a file (upstream Utf8_16_Read::determineEncoding).
UniMode detectUnicodeMode(std::string_view firstBlock) noexcept;

// Heuristic replacement for IsTextUnicode(IS_TEXT_UNICODE_STATISTICS) for little-endian UTF-16.
bool looksLikeUtf16LE(std::string_view data) noexcept;

// Streaming reader: feed file blocks in order, get UTF-8 (upstream Utf8_16_Read::convert).
// For non UTF-16 modes the bytes pass through (BOM removed for UTF-8 with BOM).
class UnicodeReader {
public:
    // Appends the UTF-8 conversion of `block` to `out`. The mode is detected on the first call
    // unless forced with setMode() beforehand.
    void convert(std::string_view block, std::string& out);
    // Flushes a pending odd byte / lone high surrogate at end of input.
    void finish(std::string& out);

    UniMode mode() const noexcept { return m_mode; }
    void setMode(UniMode mode) noexcept { m_mode = mode; m_modeKnown = true; }
    void reset() noexcept { *this = UnicodeReader(); }

private:
    void emitUnit(uint16_t unit, std::string& out);
    void process(std::string_view bytes, std::string& out);

    UniMode  m_mode = UniMode::Ansi;
    bool     m_modeKnown = false;
    bool     m_firstBlock = true;
    bool     m_checkingBom = false;   // BOM may span several blocks
    size_t   m_bomMatched = 0;
    bool     m_hasPendingByte = false;
    uint8_t  m_pendingByte = 0;
    uint16_t m_highSurrogate = 0;
};

// Encodes UTF-8 text for writing in `mode` (upstream Utf8_16_Write::convert): adds the BOM for
// BOM modes, converts to UTF-16 for UTF-16 modes, passes through otherwise.
std::string encodeFromUtf8(std::string_view utf8, UniMode mode, bool includeBom = true);

// Decodes a complete buffer in `mode` to UTF-8 (BOM, if any, removed).
std::string decodeToUtf8(std::string_view data, UniMode mode);

} // namespace npp::core
