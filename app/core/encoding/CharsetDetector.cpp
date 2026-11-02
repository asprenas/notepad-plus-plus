#include "CharsetDetector.h"

#include "core/encoding/EncodingMapper.h"
#include "core/util/StringUtil.h"

#include <uchardet.h>

namespace npp::core {

int detectCodePage(std::string_view data)
{
    uchardet_t ud = uchardet_new();
    uchardet_handle_data(ud, data.data(), data.size());
    uchardet_data_end(ud);
    const char* cs = uchardet_get_charset(ud);
    int codePage = -1;
    if (cs && *cs && !iequalsAscii(cs, "TIS-620"))
        codePage = EncodingMapper::encodingFromString(cs);
    uchardet_delete(ud);
    return codePage;
}

} // namespace npp::core
