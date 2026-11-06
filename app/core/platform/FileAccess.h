// Platform file access checks (implemented in platform/win and platform/posix).
#pragma once

#include <string_view>

namespace npp::core {

// True when writing `utf8Path` fails because of permissions (protected file or folder) and
// a process with more rights could write it: the save goes through the elevated helper.
// False for read-only attribute files, sharing violations, missing folders, etc.
bool writeAccessDenied(std::string_view utf8Path);

// Read-only file in the upstream sense (Windows read-only attribute; POSIX: no write
// permission bit at all, chmod a-w). A file only protected by its owner / ACL (e.g.
// /etc/hosts, C:\Windows\...\hosts) is not read-only: it is edited and saved through the
// elevated helper.
bool fileReadOnlyAttribute(std::string_view utf8Path);

} // namespace npp::core
