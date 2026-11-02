// Charset detection for files without BOM (port of upstream FileManager::detectCodepage, uchardet).
#pragma once

#include <string_view>

namespace npp::core {

// Returns the detected Windows code page (65001 for UTF-8) or -1 if unknown.
// TIS-620 results are ignored, like upstream (uchardet often mistakes UTF-8 for TIS-620).
int detectCodePage(std::string_view data);

} // namespace npp::core
