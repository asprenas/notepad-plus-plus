#include "RunCommand.h"

#include "core/util/PathUtil.h"

namespace npp::core {

namespace {

std::string valueOf(std::string_view name, const RunVariables& v)
{
    const std::string& p = v.fullCurrentPath;
    const size_t sep = p.find_last_of("\\/");
    const std::string dir = sep == std::string::npos ? std::string() : p.substr(0, sep);
    const std::string file = sep == std::string::npos ? p : p.substr(sep + 1);
    const size_t dot = file.find_last_of('.');
    if (name == "FULL_CURRENT_PATH") return p;
    if (name == "CURRENT_DIRECTORY") return dir;
    if (name == "FILE_NAME") return file;
    if (name == "NAME_PART") return dot == std::string::npos ? file : file.substr(0, dot);
    if (name == "EXT_PART") return dot == std::string::npos ? std::string() : file.substr(dot); // with the dot (PathFindExtension)
    if (name == "CURRENT_WORD") return v.currentWord;
    if (name == "NPP_FULL_FILE_PATH") return v.nppFullFilePath;
    if (name == "NPP_DIRECTORY") {
        const size_t s = v.nppFullFilePath.find_last_of("\\/");
        return s == std::string::npos ? std::string() : v.nppFullFilePath.substr(0, s);
    }
    if (name == "CURRENT_LINE") return std::to_string(v.currentLine);
    if (name == "CURRENT_COLUMN") return std::to_string(v.currentColumn);
    if (name == "CURRENT_LINESTR") return v.currentLineStr;
    return "\x01"; // not recognized
}

} // namespace

std::string expandRunVariables(std::string_view s, const RunVariables& v)
{
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '(') {
            const size_t close = s.find(')', i + 2);
            if (close != std::string_view::npos) {
                const std::string value = valueOf(s.substr(i + 2, close - i - 2), v);
                if (value != "\x01") {
                    out += value;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

SplitCommand splitRunCommand(std::string_view cmd)
{
    SplitCommand r;
    size_t i = 0;
    bool quoted = false;
    for (; i < cmd.size(); ++i) {
        if (cmd[i] == ' ' && !quoted)
            break;
        if (cmd[i] == '"')
            quoted = !quoted;
        r.program += cmd[i];
    }
    if (r.program.size() >= 2 && r.program.front() == '"' && r.program.back() == '"')
        r.program = r.program.substr(1, r.program.size() - 2);
    while (i < cmd.size() && cmd[i] == ' ')
        ++i;
    r.args = std::string(cmd.substr(i));
    while (!r.args.empty() && r.args.back() == ' ')
        r.args.pop_back();
    return r;
}

} // namespace npp::core
