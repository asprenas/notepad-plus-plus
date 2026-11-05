// Platform file access checks (implemented in platform/win and platform/posix).
#pragma once

#include <string_view>

namespace npp::core {

// True when writing `utf8Path` fails because of permissions (protected file or folder) and
// a process with more rights could write it: the save goes through the elevated helper.
// False for read-only attribute files, sharing violations, missing folders, etc.
bool writeAccessDenied(std::string_view utf8Path);

} // namespace npp::core
