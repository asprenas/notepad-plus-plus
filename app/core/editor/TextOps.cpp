#include "TextOps.h"

#include "IEditor.h"
#include "Sorters.h"
#include "core/search/Search.h"

#include <Scintilla.h>

#include <QChar>
#include <QString>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace npp::core {

namespace {

using u16 = std::u16string;

bool isAlpha(char16_t c) { return QChar(c).isLetter(); }
bool isAlnum(char16_t c) { return QChar(c).isLetterOrNumber(); }
bool isLower(char16_t c) { return QChar(c).isLower(); }
char16_t upper(char16_t c) { return QChar(c).toUpper().unicode(); }
char16_t lower(char16_t c) { return QChar(c).toLower().unicode(); }
bool isSpace(char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n' || c == u'\v' || c == u'\f' || QChar(c).isSpace(); }
bool isSingleQuote(char16_t c) { return c == u'\'' || c == u'’' || c == u'‘'; } // upstream isCharSingleQuote

std::string range(const IEditor& e, intptr_t start, intptr_t end) { return e.textRange(start, end); }

void replaceTarget(const IEditor& e, const std::string& text, intptr_t start, intptr_t end)
{
    e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(start), end);
    e.send(SCI_REPLACETARGET, text.size(), reinterpret_cast<intptr_t>(text.data()));
}

std::vector<u16> split(const u16& input, const u16& delimiter)
{
    std::vector<u16> out;
    size_t start = 0, end = input.find(delimiter);
    while (end != u16::npos) {
        out.push_back(input.substr(start, end - start));
        start = end + delimiter.size();
        end = input.find(delimiter, start);
    }
    out.push_back(input.substr(start));
    return out;
}

u16 join(const std::vector<u16>& lines, const u16& delimiter)
{
    u16 out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i)
            out += delimiter;
        out += lines[i];
    }
    return out;
}

intptr_t caseConvertRange(const IEditor& e, intptr_t start, intptr_t end, TextCase c)
{
    if (end <= start)
        return 0;
    const std::string converted = changeCaseUtf8(range(e, start, end), c);
    replaceTarget(e, converted, start, end);
    return static_cast<intptr_t>(converted.size()) - (end - start);
}

// Range of lines [from, to] processed by the line commands: the selected lines or the whole document
bool selectedOrAllLines(const IEditor& e, intptr_t& from, intptr_t& to)
{
    if (e.send(SCI_GETSELECTIONSTART) != e.send(SCI_GETSELECTIONEND)) {
        const auto r = selectionLinesRange(e);
        if (r.first == r.second)
            return false; // one single line selection is not allowed
        from = r.first;
        to = r.second;
    } else {
        from = 0;
        to = e.send(SCI_GETLINECOUNT) - 1;
    }
    return from < to;
}

// Upstream "process lines [from,to] as a list": helper shared by sortLines and removeAnyDuplicateLines
template <class F>
void processLineList(const IEditor& e, intptr_t fromLine, intptr_t toLine, F&& f)
{
    if (fromLine >= toLine)
        return;
    const auto startPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(fromLine));
    const auto endPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(toLine)) + e.send(SCI_LINELENGTH, static_cast<uintptr_t>(toLine));
    const u16 eol = toUtf16(eolString(e));
    const u16 text = toUtf16(range(e, startPos, endPos));
    std::vector<u16> lines = split(text, eol);
    const bool entireDocument = toLine == e.send(SCI_GETLINECOUNT) - 1;
    if (!entireDocument && lines.back().empty())
        lines.pop_back();
    f(lines);
    u16 joined = join(lines, eol);
    if (!entireDocument)
        joined += eol;
    if (joined != text)
        replaceTarget(e, toUtf8(joined), startPos, endPos);
}

} // namespace

// ------------------------------------------------------------------------------------------------

std::u16string toUtf16(std::string_view utf8)
{
    const QString q = QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size()));
    return q.toStdU16String();
}

std::string toUtf8(const std::u16string& s)
{
    return QString::fromStdU16String(s).toUtf8().toStdString();
}

int compareIgnoreCase(const std::u16string& a, const std::u16string& b)
{
    // _wcsicmp compares the lower case forms
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int ca = lower(a[i]), cb = lower(b[i]);
        if (ca != cb)
            return ca - cb;
    }
    return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

std::u16string stringTakeWhileAdmissable(const std::u16string& input, const std::u16string& admissable)
{
    const size_t idx = input.find_first_not_of(admissable);
    return idx == std::u16string::npos ? input : input.substr(0, idx);
}

std::u16string stringReplace(std::u16string subject, const std::u16string& search, const std::u16string& replace)
{
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::u16string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

double parseDouble(const std::u16string& input)
{
    // wcstod: skips leading white space, parses the longest valid prefix
    std::string s;
    for (char16_t c : input)
        s += c < 0x80 ? static_cast<char>(c) : '?';
    size_t i = s.find_first_not_of(" \t\r\n\v\f");
    if (i == std::string::npos)
        throw std::invalid_argument("invalid stod argument");
    double v = 0;
    const auto r = std::from_chars(s.data() + i, s.data() + s.size(), v);
    if (r.ec == std::errc::invalid_argument)
        throw std::invalid_argument("invalid stod argument");
    if (r.ec == std::errc::result_out_of_range)
        throw std::out_of_range("stod argument out of range");
    return v;
}

void changeCase(std::u16string& s, TextCase c)
{
    const int n = static_cast<int>(s.size());
    switch (c) {
    case TextCase::Upper:
        for (auto& ch : s)
            ch = upper(ch);
        break;
    case TextCase::Lower:
        for (auto& ch : s)
            ch = lower(ch);
        break;
    case TextCase::ProperForce:
    case TextCase::ProperBlend:
        for (int i = 0; i < n; ++i) {
            if (!isAlpha(s[i]))
                continue;
            if (i >= 2 && isSingleQuote(s[i - 1]) && isAlnum(s[i - 2])) { // exception for single quotes
                if (c == TextCase::ProperForce)
                    s[i] = lower(s[i]);
            } else if (i < 1 || !isAlnum(s[i - 1])) {
                s[i] = upper(s[i]);
            } else if (c == TextCase::ProperForce) {
                s[i] = lower(s[i]);
            }
        }
        break;
    case TextCase::SentenceForce:
    case TextCase::SentenceBlend: {
        bool isNewSentence = true, wasEolR = false, wasEolN = false;
        for (int i = 0; i < n; ++i) {
            if (isAlpha(s[i])) {
                if (isNewSentence) {
                    s[i] = upper(s[i]);
                    isNewSentence = false;
                } else if (c == TextCase::SentenceForce) {
                    s[i] = lower(s[i]);
                }
                wasEolR = wasEolN = false;
                // An exception: the pronoun "i"
                if (s[i] == u'i' && (i >= 1 && (isSpace(s[i - 1]) || s[i - 1] == u'(' || s[i - 1] == u'"'))
                    && (i + 1 < n && (isSpace(s[i + 1]) || s[i + 1] == u'\'')))
                    s[i] = u'I';
            } else if (s[i] == u'.' || s[i] == u'!' || s[i] == u'?') {
                isNewSentence = !(i + 1 == n || isAlnum(s[i + 1]));
            } else if (s[i] == u'\r') {
                if (wasEolR)
                    isNewSentence = true;
                else
                    wasEolR = true;
            } else if (s[i] == u'\n') {
                if (wasEolN)
                    isNewSentence = true;
                else
                    wasEolN = true;
            }
        }
        break;
    }
    case TextCase::Invert:
        for (auto& ch : s)
            ch = isLower(ch) ? upper(ch) : lower(ch);
        break;
    case TextCase::Random:
        for (auto& ch : s)
            if (isAlpha(ch))
                ch = (std::rand() & 1) ? upper(ch) : lower(ch);
        break;
    }
}

std::string changeCaseUtf8(std::string_view s, TextCase c)
{
    std::u16string w = toUtf16(s);
    changeCase(w, c);
    return toUtf8(w);
}

std::string eolString(const IEditor& e)
{
    switch (e.send(SCI_GETEOLMODE)) {
    case SC_EOL_CRLF: return "\r\n";
    case SC_EOL_CR:   return "\r";
    default:          return "\n";
    }
}

std::pair<intptr_t, intptr_t> selectionLinesRange(const IEditor& e)
{
    const auto startPos = e.send(SCI_GETSELECTIONSTART), endPos = e.send(SCI_GETSELECTIONEND);
    const auto line1 = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(startPos));
    auto line2 = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(endPos));
    if (line1 != line2 && e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line2)) == endPos)
        --line2; // the selection ends with the line ending: the next line is not included
    return {line1, line2};
}

std::string lineText(const IEditor& e, intptr_t line)
{
    return range(e, e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line)),
                 e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line)));
}

void convertSelectedTextTo(const IEditor& e, TextCase c)
{
    const intptr_t nbSel = e.send(SCI_GETSELECTIONS);
    if (nbSel > 1) { // multi-selection / column mode
        struct Sel { intptr_t anchor, caret; };
        std::vector<Sel> sels;
        for (intptr_t i = 0; i < nbSel; ++i)
            sels.push_back({e.send(SCI_GETSELECTIONNANCHOR, static_cast<uintptr_t>(i)), e.send(SCI_GETSELECTIONNCARET, static_cast<uintptr_t>(i))});
        std::vector<size_t> order(sels.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return std::min(sels[a].anchor, sels[a].caret) < std::min(sels[b].anchor, sels[b].caret);
        });
        e.send(SCI_BEGINUNDOACTION);
        intptr_t delta = 0;
        for (size_t idx : order) {
            Sel& s = sels[idx];
            const bool forward = s.anchor <= s.caret;
            intptr_t l = std::min(s.anchor, s.caret) + delta, r = std::max(s.anchor, s.caret) + delta;
            const intptr_t d = caseConvertRange(e, l, r, c);
            delta += d;
            r += d;
            s = forward ? Sel{l, r} : Sel{r, l};
        }
        e.send(SCI_SETSELECTION, static_cast<uintptr_t>(sels[0].caret), sels[0].anchor);
        for (size_t i = 1; i < sels.size(); ++i)
            e.send(SCI_ADDSELECTION, static_cast<uintptr_t>(sels[i].caret), sels[i].anchor);
        e.send(SCI_ENDUNDOACTION);
        return;
    }
    const auto start = e.send(SCI_GETSELECTIONSTART);
    auto end = e.send(SCI_GETSELECTIONEND);
    if (start < end) {
        end += caseConvertRange(e, start, end, c);
        e.send(SCI_SETSEL, static_cast<uintptr_t>(start), end);
    }
}

// ------------------------------------------------------------------------------------------------
// Line operations
// ------------------------------------------------------------------------------------------------

void sortLines(const IEditor& e, intptr_t fromLine, intptr_t toLine, ISorter& sorter)
{
    processLineList(e, fromLine, toLine, [&sorter](std::vector<u16>& lines) { sorter.sort(lines); });
}

intptr_t sortSelectedLines(const IEditor& e, SortKind kind, bool descending)
{
    intptr_t fromLine = 0, toLine = 0;
    size_t fromColumn = 0, toColumn = 0;
    bool hasLineSelection = false;
    if (e.send(SCI_GETSELECTIONS) > 1) {
        if (!(e.send(SCI_SELECTIONISRECTANGLE) || e.send(SCI_GETSELECTIONMODE) == SC_SEL_THIN))
            return -1;
        const auto anchor = e.send(SCI_GETRECTANGULARSELECTIONANCHOR), caret = e.send(SCI_GETRECTANGULARSELECTIONCARET);
        const auto anchorLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(anchor));
        const auto caretLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(caret));
        fromLine = std::min(anchorLine, caretLine);
        toLine = std::max(anchorLine, caretLine);
        const auto anchorOffset = static_cast<size_t>(anchor - e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(anchorLine)) + e.send(SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE));
        const auto caretOffset = static_cast<size_t>(caret - e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(caretLine)) + e.send(SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE));
        fromColumn = std::min(anchorOffset, caretOffset);
        toColumn = std::max(anchorOffset, caretOffset);
    } else {
        hasLineSelection = e.send(SCI_GETSELECTIONSTART) != e.send(SCI_GETSELECTIONEND);
        if (hasLineSelection) {
            const auto r = selectionLinesRange(e);
            if (r.first == r.second)
                return -1;
            fromLine = r.first;
            toLine = r.second;
        } else {
            fromLine = 0;
            toLine = e.send(SCI_GETLINECOUNT) - 1;
        }
    }

    std::unique_ptr<ISorter> sorter;
    switch (kind) {
    case SortKind::Lexicographic:           sorter = std::make_unique<LexicographicSorter>(descending, fromColumn, toColumn); break;
    case SortKind::LexicographicIgnoreCase: sorter = std::make_unique<LexicographicCaseInsensitiveSorter>(descending, fromColumn, toColumn); break;
    case SortKind::Integer:                 sorter = std::make_unique<IntegerSorter>(descending, fromColumn, toColumn); break;
    case SortKind::DecimalComma:            sorter = std::make_unique<DecimalCommaSorter>(descending, fromColumn, toColumn); break;
    case SortKind::DecimalDot:              sorter = std::make_unique<DecimalDotSorter>(descending, fromColumn, toColumn); break;
    case SortKind::Length:                  sorter = std::make_unique<LineLengthSorter>(descending, fromColumn, toColumn); break;
    case SortKind::Reverse:                 sorter = std::make_unique<ReverseSorter>(descending, fromColumn, toColumn); break;
    case SortKind::Random:                  sorter = std::make_unique<RandomSorter>(descending, fromColumn, toColumn); break;
    }
    intptr_t failedLine = -1;
    e.send(SCI_BEGINUNDOACTION);
    try {
        sortLines(e, fromLine, toLine, *sorter);
    } catch (size_t& failedIndex) {
        failedLine = 1 + fromLine + static_cast<intptr_t>(failedIndex);
    }
    e.send(SCI_ENDUNDOACTION);
    if (hasLineSelection) { // restore the line selection
        e.send(SCI_SETSELECTIONSTART, static_cast<uintptr_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(fromLine))));
        e.send(SCI_SETSELECTIONEND, static_cast<uintptr_t>(e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(toLine))));
    }
    return failedLine;
}

void removeAnyDuplicateLines(const IEditor& e)
{
    intptr_t from = 0, to = 0;
    if (!selectedOrAllLines(e, from, to))
        return;
    e.send(SCI_BEGINUNDOACTION);
    processLineList(e, from, to, [](std::vector<u16>& lines) { // vecRemoveDuplicates: keep first occurrences
        std::unordered_set<u16> seen;
        std::vector<u16> out;
        for (auto& l : lines)
            if (seen.insert(l).second)
                out.push_back(std::move(l));
        lines = std::move(out);
    });
    e.send(SCI_ENDUNDOACTION);
}

void removeConsecutiveDuplicateLines(const IEditor& e)
{
    const bool isEntireDoc = e.send(SCI_GETANCHOR) == e.send(SCI_GETCURRENTPOS);
    const intptr_t lastLine = e.send(SCI_GETLINECOUNT) - 1;
    intptr_t startLine = 0, endLine = lastLine;
    if (!isEntireDoc) {
        const auto startPos = e.send(SCI_GETSELECTIONSTART), endPos = e.send(SCI_GETSELECTIONEND);
        startLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(startPos));
        endLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(endPos));
        if (endPos == e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(endLine)))
            endLine -= 1;
    }
    if (startLine == endLine)
        return;
    e.send(SCI_BEGINUNDOACTION);
    // upstream getLine(): the line with its EOL
    auto lineWithEol = [&e](intptr_t line) {
        const auto start = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line));
        return range(e, start, start + e.send(SCI_LINELENGTH, static_cast<uintptr_t>(line)));
    };
    intptr_t firstNr = 0, lastNr = 0;
    std::string firstStr, lastStr;
    for (intptr_t i = startLine; i <= endLine; ++i) {
        if (firstStr.empty()) {
            firstNr = lastNr = i;
            firstStr = lineWithEol(i);
            continue;
        }
        lastStr = lineWithEol(i);
        if (firstStr == lastStr) {
            lastNr = i;
            if (i != endLine)
                continue;
        }
        if (firstNr != lastNr) {
            const auto startPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(firstNr + 1));
            const auto endPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(lastNr)) + e.send(SCI_LINELENGTH, static_cast<uintptr_t>(lastNr));
            e.send(SCI_DELETERANGE, static_cast<uintptr_t>(startPos), endPos - startPos);
            const intptr_t removed = lastNr - firstNr;
            i -= removed;
            endLine -= removed;
        }
        firstStr = lastStr;
        firstNr = lastNr = i;
    }
    // correct the last line (without EOL) if it's a duplicate line
    const auto endLineStart = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(endLine));
    const auto endLineEnd = e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(endLine));
    const auto endLineLength = e.send(SCI_LINELENGTH, static_cast<uintptr_t>(endLine));
    if (endLine == e.send(SCI_GETLINECOUNT) - 1 && endLineLength && endLineEnd - endLineStart == endLineLength && endLine > 0) {
        const intptr_t prev = endLine - 1;
        const auto prevStart = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(prev));
        if (lineWithEol(endLine) == lineText(e, prev))
            e.send(SCI_DELETERANGE, static_cast<uintptr_t>(prevStart), e.send(SCI_LINELENGTH, static_cast<uintptr_t>(prev)));
    }
    e.send(SCI_ENDUNDOACTION);
}

void removeEmptyLines(const IEditor& e, bool blankContained)
{
    SearchOptions o;
    o.type = SearchType::Regex;
    o.findWhat = blankContained ? "^(?>[\\t ]*[\\r\\n]+)+" : "^[\\r\\n]+";
    const auto selStart = e.send(SCI_GETSELECTIONSTART), selEnd = e.send(SCI_GETSELECTIONEND);
    const bool isEntireDoc = selStart == selEnd;
    e.send(SCI_BEGINUNDOACTION);
    if (isEntireDoc)
        replaceAll(e, o);
    else
        replaceAll(e, o, selStart, selEnd);

    // remove the last line if it's an empty line
    const auto lastLine = e.send(SCI_GETLINECOUNT) - 1;
    intptr_t startPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(std::max<intptr_t>(0, lastLine - 1)));
    intptr_t endPos = e.send(SCI_GETLENGTH);
    bool doLast = true;
    if (!isEntireDoc) {
        startPos = e.send(SCI_GETSELECTIONSTART);
        endPos = e.send(SCI_GETSELECTIONEND);
        const auto endLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(endPos));
        doLast = endPos == e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(endLine)) + e.send(SCI_LINELENGTH, static_cast<uintptr_t>(endLine));
    }
    if (doLast) {
        const std::string last = blankContained ? "[\\r\\n]+^[\\t ]*$|^[\\t ]+$" : "[\\r\\n]+^$";
        e.send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);
        e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(startPos), endPos);
        const auto found = e.send(SCI_SEARCHINTARGET, last.size(), reinterpret_cast<intptr_t>(last.c_str()));
        if (found >= 0)
            replaceTarget(e, {}, found, endPos);
    }
    e.send(SCI_ENDUNDOACTION);
}

void insertNewLineAboveCurrentLine(const IEditor& e)
{
    const std::string nl = eolString(e);
    const auto line = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(e.send(SCI_GETCURRENTPOS)));
    const auto pos = line == 0 ? 0 : e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line)) - static_cast<intptr_t>(nl.size());
    e.send(SCI_INSERTTEXT, static_cast<uintptr_t>(pos), reinterpret_cast<intptr_t>(nl.c_str()));
    e.send(SCI_SETEMPTYSELECTION, static_cast<uintptr_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line))));
}

void insertNewLineBelowCurrentLine(const IEditor& e)
{
    const std::string nl = eolString(e);
    const auto line = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(e.send(SCI_GETCURRENTPOS)));
    if (line == e.send(SCI_GETLINECOUNT) - 1)
        e.send(SCI_APPENDTEXT, nl.size(), reinterpret_cast<intptr_t>(nl.c_str()));
    else
        e.send(SCI_INSERTTEXT, static_cast<uintptr_t>(e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line)) + static_cast<intptr_t>(nl.size())),
               reinterpret_cast<intptr_t>(nl.c_str()));
    e.send(SCI_SETEMPTYSELECTION, static_cast<uintptr_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line + 1))));
}

// ------------------------------------------------------------------------------------------------
// Blank operations
// ------------------------------------------------------------------------------------------------

void doTrim(const IEditor& e, TrimPart part)
{
    SearchOptions o;
    o.type = SearchType::Regex;
    o.findWhat = part == TrimPart::Head ? "^[\\t ]+" : part == TrimPart::Tail ? "[\\t ]+$" : "^[\\t ]+|[\\t ]+$";
    const auto anchor = e.send(SCI_GETANCHOR), caret = e.send(SCI_GETCURRENTPOS);
    const bool isEntireDoc = anchor == caret;
    const auto docLength = e.send(SCI_GETLENGTH);
    const auto mode = e.send(SCI_GETSELECTIONMODE);
    if (mode == SC_SEL_RECTANGLE || mode == SC_SEL_THIN)
        return; // block selection is not supported
    e.send(SCI_BEGINUNDOACTION);
    if (isEntireDoc) {
        replaceAll(e, o);
    } else { // auto-expand partially selected lines
        auto startPos = e.send(SCI_GETSELECTIONSTART);
        auto endPos = e.send(SCI_GETSELECTIONEND);
        const auto startLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(startPos));
        const auto endLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(endPos));
        startPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(startLine));
        if (endPos != e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(endLine)) && endPos < e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(endLine)))
            endPos = e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(endLine));
        const auto before = e.send(SCI_GETLENGTH);
        replaceAll(e, o, startPos, endPos);
        e.send(SCI_SETSEL, static_cast<uintptr_t>(startPos), endPos + (e.send(SCI_GETLENGTH) - before));
    }
    e.send(SCI_ENDUNDOACTION);
    if (!isEntireDoc && docLength == e.send(SCI_GETLENGTH)) { // restore the selection if nothing changed
        e.send(SCI_SETANCHOR, static_cast<uintptr_t>(anchor));
        e.send(SCI_SETCURRENTPOS, static_cast<uintptr_t>(caret));
    }
}

void eol2ws(const IEditor& e)
{
    const bool isEntireDoc = e.send(SCI_GETANCHOR) == e.send(SCI_GETCURRENTPOS);
    const auto mode = e.send(SCI_GETSELECTIONMODE);
    if (mode == SC_SEL_RECTANGLE || mode == SC_SEL_THIN)
        return;
    e.send(isEntireDoc ? SCI_TARGETWHOLEDOCUMENT : SCI_TARGETFROMSELECTION);
    e.send(SCI_LINESJOIN);
}

void trimAll(const IEditor& e)
{
    e.send(SCI_BEGINUNDOACTION);
    const bool isEntireDoc = e.send(SCI_GETANCHOR) == e.send(SCI_GETCURRENTPOS);
    doTrim(e, TrimPart::Both);
    if (isEntireDoc || e.send(SCI_GETANCHOR) != e.send(SCI_GETCURRENTPOS))
        eol2ws(e);
    e.send(SCI_ENDUNDOACTION);
}

void wsTabConvert(const IEditor& e, SpaceTab way)
{
    const auto mode = e.send(SCI_GETSELECTIONMODE);
    if (mode == SC_SEL_RECTANGLE || mode == SC_SEL_THIN)
        return;
    const intptr_t tabWidth = e.send(SCI_GETTABWIDTH);
    const intptr_t currentPos = e.send(SCI_GETCURRENTPOS);
    const intptr_t currentLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(currentPos));
    const intptr_t currentPosInLine = currentPos - e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(currentLine));
    intptr_t startLine = 0, endLine = e.send(SCI_GETLINECOUNT) - 1, endLineCorrect = endLine;
    intptr_t dataLength = e.send(SCI_GETLENGTH) + 1;
    const intptr_t anchor = e.send(SCI_GETANCHOR);
    const bool isEntireDoc = anchor == currentPos;
    auto restoreSelection = [&] {
        if (!isEntireDoc) {
            e.send(SCI_SETANCHOR, static_cast<uintptr_t>(anchor));
            e.send(SCI_SETCURRENTPOS, static_cast<uintptr_t>(currentPos));
        }
    };
    if (!isEntireDoc) {
        intptr_t startPos = e.send(SCI_GETSELECTIONSTART);
        startLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(startPos));
        intptr_t endPos = e.send(SCI_GETSELECTIONEND);
        endLine = endLineCorrect = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(endPos));
        startPos = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(startLine));
        if (endPos == e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(endLine)))
            endLineCorrect = endLine - 1;
        else if (endPos < e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(endLine)))
            endPos = e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(endLine));
        dataLength = endPos - startPos + 1;
        e.send(SCI_SETSEL, static_cast<uintptr_t>(startPos), endPos);
    }
    if (dataLength < 2) {
        restoreSelection();
        return;
    }

    intptr_t changeDataCount = 0, newCurrentPos = 0;
    std::vector<intptr_t> folding;
    e.send(SCI_BEGINUNDOACTION);
    for (intptr_t idx = startLine; idx < endLineCorrect + 1; ++idx) {
        const intptr_t lineStart = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(idx));
        const intptr_t lineEnd = e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(idx));
        const std::string source = range(e, lineStart, lineEnd) + std::string(1, '\0') + std::string(static_cast<size_t>(tabWidth) + 2, '\0');
        const char* src = source.c_str();
        std::string dest;
        intptr_t column = 0, tabStop = tabWidth - 1, changeDataLineCount = 0;
        bool onlyLeading = false;

        if (way == SpaceTab::Tab2Space) {
            if (std::string_view(src).find('\t') == std::string_view::npos)
                continue;
            for (int i = 0; src[i] != '\0'; ++i) {
                if (src[i] == '\t') {
                    const intptr_t insertTabs = tabWidth - (column % tabWidth);
                    for (intptr_t j = 0; j < insertTabs; ++j) {
                        dest += ' ';
                        ++changeDataCount;
                        ++changeDataLineCount;
                        if (idx == currentLine && i < currentPosInLine)
                            ++newCurrentPos;
                    }
                    column += insertTabs;
                } else {
                    dest += src[i];
                    if (idx == currentLine && i < currentPosInLine)
                        ++newCurrentPos;
                    if (src[i] == '\n' || src[i] == '\r')
                        column = 0;
                    else if ((src[i] & 0xC0) != 0x80)
                        ++column;
                }
            }
        } else {
            onlyLeading = way == SpaceTab::Space2TabLeading;
            bool nextChar = false, nonSpaceFound = false;
            int counter = 0;
            for (int i = 0; src[i] != '\0'; ++i) {
                if (!nonSpaceFound) {
                    while (src[i + counter] == ' ') {
                        if (column + counter == tabStop) {
                            tabStop += tabWidth;
                            if (counter >= 1) {
                                dest += '\t';
                                ++changeDataCount;
                                ++changeDataLineCount;
                                i += counter;
                                column += counter + 1;
                                counter = 0;
                                nextChar = true;
                                if (idx == currentLine && i <= currentPosInLine)
                                    ++newCurrentPos;
                                break;
                            } else if (src[i + 1] == ' ' || src[i + 1] == '\t') {
                                dest += '\t';
                                ++changeDataCount;
                                ++changeDataLineCount;
                                ++i;
                                column += 1;
                                counter = 0;
                                if (idx == currentLine && i <= currentPosInLine)
                                    ++newCurrentPos;
                            } else {
                                dest += src[i];
                                column += 1;
                                counter = 0;
                                nextChar = true;
                                if (idx == currentLine && i <= currentPosInLine)
                                    ++newCurrentPos;
                                break;
                            }
                        } else {
                            ++counter;
                        }
                    }
                    if (nextChar) {
                        nextChar = false;
                        continue;
                    }
                    if (src[i] == ' ' && src[i + counter] == '\t') { // spaces absorbed by a TAB on the right
                        dest += '\t';
                        ++changeDataCount;
                        ++changeDataLineCount;
                        i += counter;
                        column = tabStop + 1;
                        tabStop += tabWidth;
                        counter = 0;
                        if (idx == currentLine && i <= currentPosInLine)
                            ++newCurrentPos;
                        continue;
                    }
                }
                if (onlyLeading && !nonSpaceFound)
                    nonSpaceFound = true;
                if (src[i] == '\n' || src[i] == '\r') {
                    dest += src[i];
                    column = 0;
                    tabStop = tabWidth - 1;
                    nonSpaceFound = false;
                } else if (src[i] == '\t') {
                    dest += src[i];
                    column = tabStop + 1;
                    tabStop += tabWidth;
                    counter = 0;
                } else {
                    dest += src[i];
                    counter = 0;
                    if ((src[i] & 0xC0) != 0x80) {
                        ++column;
                        if (column > 0 && column % tabWidth == 0)
                            tabStop += tabWidth;
                    }
                }
                if (idx == currentLine && i < currentPosInLine)
                    ++newCurrentPos;
            }
        }
        if ((e.send(SCI_GETFOLDLEVEL, static_cast<uintptr_t>(idx)) & SC_FOLDLEVELHEADERFLAG) && e.send(SCI_GETFOLDEXPANDED, static_cast<uintptr_t>(idx)) == 0)
            folding.push_back(idx);
        if (changeDataLineCount)
            replaceTarget(e, dest, lineStart, lineEnd);
    }
    e.send(SCI_ENDUNDOACTION);

    if (changeDataCount) {
        if (!isEntireDoc)
            e.send(SCI_SETSEL, static_cast<uintptr_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(startLine))),
                   endLineCorrect != endLine ? e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(endLine))
                                             : e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(endLine)));
        else
            e.send(SCI_GOTOPOS, static_cast<uintptr_t>(e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(currentLine)) + newCurrentPos));
        for (intptr_t line : folding)
            e.send(SCI_FOLDLINE, static_cast<uintptr_t>(line), SC_FOLDACTION_CONTRACT);
    } else {
        restoreSelection();
    }
}

} // namespace npp::core
