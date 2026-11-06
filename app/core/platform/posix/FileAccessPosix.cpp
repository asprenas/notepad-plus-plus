// POSIX implementation of core/platform/FileAccess.h
#include "core/platform/FileAccess.h"

#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace npp::core {

bool writeAccessDenied(std::string_view utf8Path)
{
    const std::string path(utf8Path);
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return false;
        return ::access(path.c_str(), W_OK) != 0 && (errno == EACCES || errno == EPERM);
    }
    const auto slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string(".") : (slash == 0 ? std::string("/") : path.substr(0, slash));
    if (::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    return ::access(dir.c_str(), W_OK | X_OK) != 0 && (errno == EACCES || errno == EPERM);
}

bool fileReadOnlyAttribute(std::string_view utf8Path)
{
    struct stat st {};
    const std::string path(utf8Path);
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0;
}

} // namespace npp::core
