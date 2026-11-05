// Tools > MD5 / SHA-1 / SHA-256 / SHA-512 (upstream MISC/md5/md5Dlgs.cpp): lowercase hex digests.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace npp::core {

enum class HashType { Md5, Sha1, Sha256, Sha512 };

std::string hashHex(HashType type, std::string_view bytes);
// "Treat each line as a separate string": one digest per line ('\n' or "\r\n"), empty lines kept
// empty, lines joined with "\r\n" (upstream generateHashPerLine).
std::string hashPerLine(HashType type, std::string_view text);
// "<digest>  <file name>\r\n" per file (upstream HashFromFilesDlg); unreadable files are skipped.
std::string hashFiles(HashType type, const std::vector<std::string>& utf8Paths);
const char* hashName(HashType type); // "MD5", "SHA-1", ...

} // namespace npp::core
