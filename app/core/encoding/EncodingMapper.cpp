#include "EncodingMapper.h"

#include "core/encoding/UniMode.h"
#include "core/util/StringUtil.h"

namespace npp::core {

const std::vector<EncodingUnit>& EncodingMapper::table()
{
    // Same order and aliases as upstream PowerEditor/src/EncodingMapper.cpp (v8.9.8.1).
    static const std::vector<EncodingUnit> kTable = {
        {1250,  "windows-1250"},
        {1251,  "windows-1251"},
        {1252,  "windows-1252"},
        {1253,  "windows-1253"},
        {1254,  "windows-1254"},
        {1255,  "windows-1255"},
        {1256,  "windows-1256"},
        {1257,  "windows-1257"},
        {1258,  "windows-1258"},
        {28591, "latin1 ISO_8859-1 ISO-8859-1 CP819 IBM819 csISOLatin1 iso-ir-100 l1"},
        {28592, "latin2 ISO_8859-2 ISO-8859-2 csISOLatin2 iso-ir-101 l2"},
        {28593, "latin3 ISO_8859-3 ISO-8859-3 csISOLatin3 iso-ir-109 l3"},
        {28594, "latin4 ISO_8859-4 ISO-8859-4 csISOLatin4 iso-ir-110 l4"},
        {28595, "cyrillic ISO_8859-5 ISO-8859-5 csISOLatinCyrillic iso-ir-144"},
        {28596, "arabic ISO_8859-6 ISO-8859-6 csISOLatinArabic iso-ir-127 ASMO-708 ECMA-114"},
        {28597, "greek ISO_8859-7 ISO-8859-7 csISOLatinGreek greek8 iso-ir-126 ELOT_928 ECMA-118"},
        {28598, "hebrew ISO_8859-8 ISO-8859-8 csISOLatinHebrew iso-ir-138"},
        {28599, "latin5 ISO_8859-9 ISO-8859-9 csISOLatin5 iso-ir-148 l5"},
        {-1,    ""}, // ISO-8859-10 (not supported upstream)
        {-1,    ""}, // ISO-8859-11
        {28603, "ISO_8859-13 ISO-8859-13"},
        {28604, "iso-celtic latin8 ISO_8859-14 ISO-8859-14 18 iso-ir-199"},
        {28605, "Latin-9 ISO_8859-15 ISO-8859-15"},
        {-1,    ""}, // ISO-8859-16
        {437,   "IBM437 cp437 437 csPC8CodePage437"},
        {720,   "IBM720 cp720 oem720 720"},
        {737,   "IBM737 cp737 oem737 737"},
        {775,   "IBM775 cp775 oem775 775"},
        {850,   "IBM850 cp850 oem850 850"},
        {852,   "IBM852 cp852 oem852 852"},
        {855,   "IBM855 cp855 oem855 855 csIBM855"},
        {857,   "IBM857 cp857 oem857 857"},
        {858,   "IBM858 cp858 oem858 858"},
        {860,   "IBM860 cp860 oem860 860"},
        {861,   "IBM861 cp861 oem861 861"},
        {862,   "IBM862 cp862 oem862 862"},
        {863,   "IBM863 cp863 oem863 863"},
        {865,   "IBM865 cp865 oem865 865"},
        {866,   "IBM866 cp866 oem866 866"},
        {869,   "IBM869 cp869 oem869 869"},
        {950,   "big5 csBig5"},
        {936,   "gb2312 gbk csGB2312 gb18030"},
        {932,   "Shift_JIS MS_Kanji csShiftJIS csWindows31J"},
        {949,   "windows-949 korean"},
        {51949, "euc-kr csEUCKR"},
        {874,   "tis-620"},
        {10007, "x-mac-cyrillic xmaccyrillic"},
        {21866, "koi8_u"},
        {20866, "koi8_r csKOI8R"},
    };
    return kTable;
}

int EncodingMapper::encodingFromIndex(int index) noexcept
{
    const auto& t = table();
    if (index < 0 || static_cast<size_t>(index) >= t.size())
        return -1;
    return t[static_cast<size_t>(index)].codePage;
}

int EncodingMapper::indexFromEncoding(int codePage) noexcept
{
    if (codePage == -1)
        return -1;
    const auto& t = table();
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].codePage == codePage)
            return static_cast<int>(i);
    return -1;
}

int EncodingMapper::encodingFromString(std::string_view alias) noexcept
{
    if (isInList(alias, "utf-8 utf8"))
        return kCodePageUtf8;
    for (const auto& unit : table())
        if (unit.codePage != -1 && isInList(alias, unit.aliasList))
            return unit.codePage;
    return -1;
}

} // namespace npp::core
