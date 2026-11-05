#include "Hash.h"

#include "core/util/PathUtil.h"

#include <QCryptographicHash>

namespace npp::core {

namespace {

QCryptographicHash::Algorithm algorithm(HashType t)
{
    switch (t) {
    case HashType::Md5: return QCryptographicHash::Md5;
    case HashType::Sha1: return QCryptographicHash::Sha1;
    case HashType::Sha256: return QCryptographicHash::Sha256;
    case HashType::Sha512: return QCryptographicHash::Sha512;
    }
    return QCryptographicHash::Md5;
}

} // namespace

const char* hashName(HashType type)
{
    switch (type) {
    case HashType::Md5: return "MD5";
    case HashType::Sha1: return "SHA-1";
    case HashType::Sha256: return "SHA-256";
    case HashType::Sha512: return "SHA-512";
    }
    return "";
}

std::string hashHex(HashType type, std::string_view bytes)
{
    return QCryptographicHash::hash(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())), algorithm(type))
        .toHex()
        .toStdString();
}

std::string hashPerLine(HashType type, std::string_view text)
{
    std::string result;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (last)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!line.empty())
            result += hashHex(type, line);
        result += "\r\n";
        start = end + 1;
    }
    return result;
}

std::string hashFiles(HashType type, const std::vector<std::string>& utf8Paths)
{
    std::string result;
    for (const std::string& p : utf8Paths) {
        const auto content = readFileContent(p);
        if (!content)
            continue;
        result += hashHex(type, *content);
        result += "  ";
        result += fileNameOf(p);
        result += "\r\n";
    }
    return result;
}

} // namespace npp::core
