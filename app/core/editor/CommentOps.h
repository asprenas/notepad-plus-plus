// Edit > Comment/Uncomment (phase 3, P3-06): ports of upstream Notepad_plus::doBlockComment,
// doStreamComment and undoStreamComment running on an IEditor.
#pragma once

#include "core/lang/LangType.h"

#include <string>
#include <string_view>

namespace npp::core {

class IEditor;

struct CommentSymbols {
    std::string line;   // commentLine of langs.xml (e.g. "//")
    std::string start;  // commentStart (e.g. "/*")
    std::string end;    // commentEnd   (e.g. "*/")
    LangType    lang = L_TEXT;
};

// Upstream extractSymbol: "00//  03/*  04*/" (UDL comments keyword list) -> symbol after the prefix
std::string extractUdlCommentSymbol(char first, char second, std::string_view keywords);
// Comment symbols of a user defined language (keyword list SCE_USER_KWLIST_COMMENTS)
CommentSymbols udlCommentSymbols(std::string_view commentsKeywordList);

enum class CommentMode { Comment, Uncomment, Toggle };
bool doBlockComment(const IEditor& e, const CommentSymbols& s, CommentMode mode);
bool doStreamComment(const IEditor& e, const CommentSymbols& s);
bool undoStreamComment(const IEditor& e, const CommentSymbols& s, bool tryBlockComment = true);

} // namespace npp::core
