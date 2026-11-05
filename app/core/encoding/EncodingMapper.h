// Code page table of the Encoding > Character sets menu (port of upstream EncodingMapper.*).
// The index order matches the IDM_FORMAT_ENCODE ... menu command order and must not change.
#pragma once

#include <string_view>
#include <vector>

namespace npp::core {

struct EncodingUnit {
    int              codePage;   // Windows code page number, -1 = unsupported slot
    std::string_view aliasList;  // space separated IANA / common names
};

class EncodingMapper {
public:
    static const std::vector<EncodingUnit>& table();

    static int encodingFromIndex(int index) noexcept;        // -1 if out of range
    static int indexFromEncoding(int codePage) noexcept;     // -1 if not in the table
    // "utf-8"/"utf8" -> 65001, otherwise first table entry whose alias list contains the name
    // (ASCII case-insensitive); -1 if unknown.
    static int encodingFromString(std::string_view alias) noexcept;
};

} // namespace npp::core
