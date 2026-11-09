// Document state (port of upstream Buffer, ScintillaComponent/Buffer.h).
//
// Difference from upstream (docs/plan/phase-1-core-extraction.md): Scintilla always holds UTF-8.
// Documents in a code page (including "ANSI") are converted on load and converted back on save;
// upstream kept ANSI documents in the system code page inside Scintilla.
#pragma once

#include "core/encoding/UniMode.h"
#include "core/lang/LangType.h"

#include <cstdint>
#include <string>

namespace npp::core {

enum class EolType : uint8_t { Windows = 0, Mac = 1, Unix = 2, Unknown = 3 };

// First EOL in data (upstream getEOLFormatForm).
EolType detectEol(const char* data, size_t length, EolType fallback = EolType::Unknown);
const char* eolString(EolType eol) noexcept;  // "\r\n", "\r", "\n"
int scintillaEolMode(EolType eol) noexcept;   // SC_EOL_CRLF / SC_EOL_CR / SC_EOL_LF

// Upstream DocFileStatus
enum class DocStatus { Regular, Unnamed, Deleted, Modified, Inaccessible };

// Last write time in Win32 FILETIME units (100 ns since 1601-01-01 UTC), the unit stored in
// session.xml (originalFileLastModifTimestamp / ...High).
struct FileTime {
    int64_t value = 0;
    uint32_t low() const noexcept { return static_cast<uint32_t>(value & 0xFFFFFFFF); }
    uint32_t high() const noexcept { return static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32); }
    static FileTime fromParts(uint32_t low, uint32_t high) noexcept
    {
        return {static_cast<int64_t>((static_cast<uint64_t>(high) << 32) | low)};
    }
    static FileTime fromUnixMs(int64_t ms) noexcept { return {ms * 10000 + 116444736000000000LL}; }
    int64_t toUnixMs() const noexcept { return (value - 116444736000000000LL) / 10000; }
    bool operator==(const FileTime&) const = default;
};
// FileTime of a file, nullopt-like {0} if missing.
FileTime fileLastWriteTime(const std::string& utf8Path);

using BufferId = uint64_t;
inline constexpr BufferId kInvalidBuffer = 0;

class Buffer {
public:
    BufferId    id = kInvalidBuffer;
    intptr_t    document = 0;       // Scintilla document (SCI_CREATEDOCUMENT), ref counted by FileManager

    std::string path;               // UTF-8 full path, empty for untitled documents
    std::string untitledName;       // "new 1" ...
    LangType    lang = L_TEXT;
    std::string userLangName;       // UDL name when lang == L_USER

    int         encoding = -1;      // code page, -1 = none (Unicode modes / ANSI)
    UniMode     uniMode = UniMode::Utf8;
    EolType     eol = EolType::Windows;

    DocStatus   status = DocStatus::Regular;
    bool        userReadOnly = false;
    bool        fileReadOnly = false;
    bool        dirty = false;      // kept in sync by the UI from Scintilla save point notifications
    bool        largeFile = false;
    uint64_t    fileSize = 0;
    FileTime    timestamp;          // last known write time of `path`
    std::string backupPath;         // snapshot backup file, empty if none
    int         docColorId = -1;    // tab colour 0..4 (View > Tab > Apply Color), -1 none (session tabColourId)
    bool        pinned = false;     // pinned tab (session tabPinned)
    bool        monitoring = false; // View > Monitoring (tail -f)

    bool isUntitled() const noexcept { return path.empty(); }
    bool isReadOnly() const noexcept { return userReadOnly || fileReadOnly; }
    // Tab / title name: file name or untitled name
    std::string displayName() const;
};

} // namespace npp::core
