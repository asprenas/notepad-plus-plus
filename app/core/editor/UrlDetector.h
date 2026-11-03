// URL detection of upstream Notepad++ (clickable links), on UTF-16 text.
#pragma once

#include <string>

namespace npp::core {

// GuiConfig uriSchemes (space separated, e.g. "svn:// cvs://"), in addition to ftp/http/https/mailto/file
void setExtraUrlSchemes(const std::u16string& schemes);
// Upstream isUrl: true if an URL starts at text[start] (its length in segmentLen); else
// segmentLen = characters to skip before the next candidate.
bool isUrl(const char16_t* text, int textLen, int start, int* segmentLen);

} // namespace npp::core
