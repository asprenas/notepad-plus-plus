#include "CommentOps.h"

#include "IEditor.h"

#include <Scintilla.h>

#include <cctype>
#include <string>

namespace npp::core {

namespace {

// _strnicmp(a, b, n) == 0
bool startsWithNoCase(std::string_view a, std::string_view b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const char ca = i < a.size() ? a[i] : '\0', cb = i < b.size() ? b[i] : '\0';
        if (std::tolower(static_cast<unsigned char>(ca)) != std::tolower(static_cast<unsigned char>(cb)))
            return false;
        if (!ca)
            return true;
    }
    return true;
}

intptr_t searchInTarget(const IEditor& e, const std::string& text, intptr_t from, intptr_t to)
{
    e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(from), to);
    return e.send(SCI_SEARCHINTARGET, text.size(), reinterpret_cast<intptr_t>(text.data()));
}

void insertAt(const IEditor& e, intptr_t pos, const std::string& text)
{
    e.send(SCI_INSERTTEXT, static_cast<uintptr_t>(pos), reinterpret_cast<intptr_t>(text.c_str()));
}

void replaceSel(const IEditor& e, intptr_t start, intptr_t end)
{
    e.send(SCI_SETSEL, static_cast<uintptr_t>(start), end);
    e.send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>(""));
}

bool readOnly(const IEditor& e) { return e.send(SCI_GETREADONLY) != 0; }

} // namespace

std::string extractUdlCommentSymbol(char first, char second, std::string_view str)
{
    bool found = false;
    std::string extracted;
    for (size_t i = 0; i < str.size() && extracted.size() < 127; ++i) {
        if (found) {
            if (!str[i] || str[i] == ' ')
                return extracted;
            extracted += str[i];
        } else {
            if (!str[i])
                return {};
            if (str[i] == first && i + 1 < str.size() && str[i + 1] == second) {
                found = true;
                ++i;
            }
        }
    }
    return extracted;
}

CommentSymbols udlCommentSymbols(std::string_view kws)
{
    CommentSymbols s;
    s.lang = L_USER;
    s.line = extractUdlCommentSymbol('0', '0', kws);
    s.start = extractUdlCommentSymbol('0', '3', kws);
    s.end = extractUdlCommentSymbol('0', '4', kws);
    return s;
}

bool doBlockComment(const IEditor& e, const CommentSymbols& sym, CommentMode mode)
{
    if (readOnly(e))
        return false; // avoid side effects (caret moves) on read-only documents
    bool advanced = false; // single line comment with stream symbols per line
    if (sym.line.empty()) {
        if (sym.start.empty() || sym.end.empty())
            return false;
        if (mode == CommentMode::Uncomment)
            return undoStreamComment(e, sym, false);
        advanced = true;
    }
    const bool baanC = sym.lang == L_BAANC;
    std::string comment, advStart, advEnd;
    size_t commentLen = 0, advStartLen = 0, advEndLen = 0;
    if (!advanced) {
        comment = sym.line;
        if (!baanC) // BaanC standardization: no space
            comment += ' ';
        commentLen = comment.size();
    } else {
        advStart = sym.start + " ";
        advEnd = " " + sym.end;
        advStartLen = advStart.size();
        advEndLen = advEnd.size();
    }

    size_t selStart = static_cast<size_t>(e.send(SCI_GETSELECTIONSTART));
    size_t selEnd = static_cast<size_t>(e.send(SCI_GETSELECTIONEND));
    const size_t caret = static_cast<size_t>(e.send(SCI_GETCURRENTPOS));
    const bool moveCaret = caret < selEnd; // caret at the beginning of the selected block
    const intptr_t selStartLine = e.send(SCI_LINEFROMPOSITION, selStart);
    intptr_t selEndLine = e.send(SCI_LINEFROMPOSITION, selEnd);
    if (selEndLine - selStartLine > 0 && selEnd == static_cast<size_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(selEndLine))))
        --selEndLine; // "caret return" is part of the last selected line
    int nUncomments = 0;
    const bool avoidIndent = sym.lang == L_FORTRAN_77 || baanC;
    const bool commentEmptyLines = baanC;

    e.send(SCI_BEGINUNDOACTION);
    for (intptr_t i = selStartLine; i <= selEndLine; ++i) {
        const size_t lineStart = static_cast<size_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(i)));
        size_t lineIndent = static_cast<size_t>(e.send(SCI_GETLINEINDENTPOSITION, static_cast<uintptr_t>(i)));
        const size_t lineEnd = static_cast<size_t>(e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(i)));
        if (lineIndent == lineEnd && !commentEmptyLines)
            continue; // empty lines are not commented
        if (avoidIndent)
            lineIndent = lineStart;
        const std::string line = e.textRange(static_cast<intptr_t>(lineIndent), static_cast<intptr_t>(lineEnd));

        if (mode != CommentMode::Comment) { // uncomment / toggle
            if (!advanced) {
                // case insensitive: "REM" / "rem" in batch files
                if (startsWithNoCase(line, comment, baanC ? commentLen : commentLen - 1)) {
                    const size_t len = (commentLen - 1 < line.size() && line[commentLen - 1] == ' ') ? commentLen : (!baanC ? commentLen - 1 : commentLen);
                    replaceSel(e, static_cast<intptr_t>(lineIndent), static_cast<intptr_t>(lineIndent + len));
                    if (i == selStartLine) {
                        if (selStart > lineIndent + len)
                            selStart -= len;
                        else if (selStart > lineIndent)
                            selStart = lineIndent;
                    }
                    if (i == selEndLine) {
                        if (selEnd > lineIndent + len)
                            selEnd -= len;
                        else if (selEnd > lineIndent) {
                            selEnd = lineIndent;
                            if (lineIndent == lineStart && i != selStartLine)
                                ++selEnd; // avoid caret return in this case
                        }
                    } else {
                        selEnd -= len;
                    }
                    ++nUncomments;
                    continue;
                }
            } else if (line.size() + 1 >= advEndLen && startsWithNoCase(line, advStart, advStartLen - 1)
                       && startsWithNoCase(line.substr(line.size() - (advEndLen - 1)), advEnd.substr(1), advEndLen - 1)) {
                const size_t startLen = (advStartLen - 1 < line.size() && line[advStartLen - 1] == ' ') ? advStartLen : advStartLen - 1;
                const size_t endLen = (line.size() >= advEndLen && line[line.size() - advEndLen] == ' ') ? advEndLen : advEndLen - 1;
                replaceSel(e, static_cast<intptr_t>(lineIndent), static_cast<intptr_t>(lineIndent + startLen));
                replaceSel(e, static_cast<intptr_t>(lineEnd - startLen - endLen), static_cast<intptr_t>(lineEnd - startLen));
                if (i == selStartLine) {
                    if (selStart > lineEnd - endLen)
                        selStart = lineEnd - startLen - endLen;
                    else if (selStart > lineIndent + startLen)
                        selStart -= startLen;
                    else if (selStart > lineIndent)
                        selStart = lineIndent;
                }
                if (i == selEndLine) {
                    if (selEnd > lineEnd)
                        selEnd -= startLen + endLen;
                    else if (selEnd > lineEnd - endLen)
                        selEnd = lineEnd - startLen - endLen;
                    else if (selEnd > lineIndent + startLen)
                        selEnd -= startLen;
                    else if (selEnd > lineIndent) {
                        selEnd = lineIndent;
                        if (lineIndent == lineStart && i != selStartLine)
                            ++selEnd;
                    }
                } else {
                    selEnd -= startLen + endLen;
                }
                ++nUncomments;
                continue;
            }
        }

        if (mode != CommentMode::Uncomment) { // comment / toggle
            if (!advanced) {
                insertAt(e, static_cast<intptr_t>(lineIndent), comment);
                if (i == selStartLine && selStart >= lineIndent)
                    selStart += commentLen;
                if (i == selEndLine) {
                    if (selEnd >= lineIndent)
                        selEnd += commentLen;
                } else {
                    selEnd += commentLen;
                }
            } else {
                insertAt(e, static_cast<intptr_t>(lineIndent), advStart);
                insertAt(e, static_cast<intptr_t>(lineEnd + advStartLen), advEnd);
                if (i == selStartLine && selStart >= lineIndent)
                    selStart += advStartLen;
                if (i == selEndLine) {
                    if (selEnd > lineEnd)
                        selEnd += advStartLen + advEndLen;
                    else if (selEnd >= lineIndent)
                        selEnd += advStartLen;
                } else {
                    selEnd += advStartLen + advEndLen;
                }
            }
        }
    }
    if (moveCaret) {
        e.send(SCI_GOTOPOS, selEnd);
        e.send(SCI_SETCURRENTPOS, selStart);
    } else {
        e.send(SCI_SETSEL, selStart, static_cast<intptr_t>(selEnd));
    }
    e.send(SCI_ENDUNDOACTION);

    if (mode == CommentMode::Uncomment && nUncomments == 0)
        return undoStreamComment(e, sym, false);
    return true;
}

bool doStreamComment(const IEditor& e, const CommentSymbols& sym)
{
    if (readOnly(e))
        return false;
    if (sym.start.empty() || sym.end.empty())
        return sym.line.empty() ? false : doBlockComment(e, sym, CommentMode::Comment);

    const std::string startComment = sym.start + " ";
    const std::string endComment = " " + sym.end;
    size_t selStart = static_cast<size_t>(e.send(SCI_GETSELECTIONSTART));
    size_t selEnd = static_cast<size_t>(e.send(SCI_GETSELECTIONEND));
    const size_t caret = static_cast<size_t>(e.send(SCI_GETCURRENTPOS));
    const bool moveCaret = caret < selEnd;
    if (selEnd <= selStart) { // no selection: the current line
        const auto line = e.send(SCI_LINEFROMPOSITION, selStart);
        selStart = static_cast<size_t>(e.send(SCI_GETLINEINDENTPOSITION, static_cast<uintptr_t>(line)));
        selEnd = static_cast<size_t>(e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line)));
    }
    e.send(SCI_BEGINUNDOACTION);
    insertAt(e, static_cast<intptr_t>(selStart), startComment);
    selEnd += startComment.size();
    selStart += startComment.size();
    insertAt(e, static_cast<intptr_t>(selEnd), endComment);
    if (moveCaret) {
        e.send(SCI_GOTOPOS, selEnd);
        e.send(SCI_SETCURRENTPOS, selStart);
    } else {
        e.send(SCI_SETSEL, selStart, static_cast<intptr_t>(selEnd));
    }
    e.send(SCI_ENDUNDOACTION);
    return true;
}

bool undoStreamComment(const IEditor& e, const CommentSymbols& sym, bool tryBlockComment)
{
    if (readOnly(e))
        return false;
    if (sym.start.empty() || sym.end.empty())
        return (!sym.line.empty() && tryBlockComment) ? doBlockComment(e, sym, CommentMode::Uncomment) : false;

    const std::string startComment = sym.start, endComment = sym.end;
    bool retVal = false;
    e.send(SCI_BEGINUNDOACTION);
    for (;;) { // as long as stream comments are within the selection
        const intptr_t selStart = e.send(SCI_GETSELECTIONSTART);
        const intptr_t selEnd = e.send(SCI_GETSELECTIONEND);
        const intptr_t caret = e.send(SCI_GETCURRENTPOS);
        const intptr_t docLength = e.send(SCI_GETLENGTH);
        const bool moveCaret = caret < selEnd;

        e.send(SCI_SETSEARCHFLAGS, SCFIND_WORDSTART);
        auto find = [&e](const std::string& s, intptr_t from, intptr_t to) { return searchInTarget(e, s, from, to); };
        intptr_t posStartComment = 0, posEndComment = 0;
        // around the selection start
        const intptr_t sBefore0 = find(startComment, selStart, 0), eBefore0 = find(endComment, selStart, 0);
        const intptr_t sAfter0 = find(startComment, selStart, docLength), eAfter0 = find(endComment, selStart, docLength);
        if (sBefore0 != -1 && eAfter0 != -1 && (eBefore0 == -1 || sBefore0 >= eBefore0) && (sAfter0 == -1 || eAfter0 <= sAfter0)) {
            posStartComment = sBefore0;
            posEndComment = eAfter0;
        } else { // around the selection end
            const intptr_t sBefore1 = find(startComment, selEnd, 0), eBefore1 = find(endComment, selEnd, 0);
            const intptr_t sAfter1 = find(startComment, selEnd, docLength), eAfter1 = find(endComment, selEnd, docLength);
            if (sBefore1 != -1 && eAfter1 != -1 && (eBefore1 == -1 || sBefore1 >= eBefore1) && (sAfter1 == -1 || eAfter1 <= sAfter1)) {
                posStartComment = sBefore1;
                posEndComment = eAfter1;
            } else if (sAfter0 != -1 && sAfter0 < selEnd && eBefore1 != -1 && eBefore1 > selStart) { // inside the selection
                posStartComment = sAfter0;
                posEndComment = eAfter0;
            } else {
                e.send(SCI_ENDUNDOACTION);
                return retVal;
            }
        }
        retVal = true;
        intptr_t startLen = static_cast<intptr_t>(startComment.size());
        intptr_t endLen = static_cast<intptr_t>(endComment.size());
        // end comment first so that the start position does not change; a white space before it goes too
        if (posEndComment > 0 && e.textRange(posEndComment - 1, posEndComment) == " ") {
            endLen += 1;
            posEndComment -= 1;
        }
        e.send(SCI_BEGINUNDOACTION);
        replaceSel(e, posEndComment, posEndComment + endLen);
        if (e.textRange(posStartComment + startLen, posStartComment + startLen + 1) == " ")
            startLen += 1;
        replaceSel(e, posStartComment, posStartComment + startLen);
        e.send(SCI_ENDUNDOACTION);

        intptr_t startMove = 0, endMove = 0;
        if (selStart > posStartComment)
            startMove = selStart >= posStartComment + startLen ? -startLen : -(selStart - posStartComment);
        if (selEnd >= posEndComment + endLen)
            endMove = -(startLen + endLen);
        else if (selEnd <= posEndComment)
            endMove = -startLen;
        else
            endMove = -(startLen + (selEnd - posEndComment));
        if (moveCaret) {
            e.send(SCI_GOTOPOS, static_cast<uintptr_t>(selEnd + endMove));
            e.send(SCI_SETCURRENTPOS, static_cast<uintptr_t>(selStart + startMove));
        } else {
            e.send(SCI_SETSEL, static_cast<uintptr_t>(selStart + startMove), selEnd + endMove);
        }
    }
}

} // namespace npp::core
