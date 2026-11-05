// Edit menu text operations (phase 3, P3-06): ports of upstream Notepad_plus / ScintillaEditView
// functions running on an IEditor. Character classification is done on UTF-16 (Qt QChar) like
// the upstream wide-char Win32 calls (IsCharAlphaW, CharUpperW, ...).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace npp::core {

class IEditor;
class ISorter;

std::u16string toUtf16(std::string_view utf8);
std::string toUtf8(const std::u16string& s);

// Upstream TextCase / ScintillaEditView::changeCase
enum class TextCase { Upper, Lower, ProperForce, ProperBlend, SentenceForce, SentenceBlend, Invert, Random };
void changeCase(std::u16string& s, TextCase c);
std::string changeCaseUtf8(std::string_view s, TextCase c);

// ScintillaEditView helpers
std::string eolString(const IEditor& e);
std::pair<intptr_t, intptr_t> selectionLinesRange(const IEditor& e); // getSelectionLinesRange()
std::string lineText(const IEditor& e, intptr_t line);               // without EOL

// Edit > Convert Case to (convertSelectedTextTo; every selection of a multi-selection)
void convertSelectedTextTo(const IEditor& e, TextCase c);

// Edit > Line Operations
enum class SortKind { Lexicographic, LexicographicIgnoreCase, Integer, DecimalComma, DecimalDot, Length, Reverse, Random };
// Sorts the selected lines (or the column range of a rectangular selection, or the whole
// document). Returns -1 on success / nothing to do, else the 1-based line that is not a number.
intptr_t sortSelectedLines(const IEditor& e, SortKind kind, bool descending);
void sortLines(const IEditor& e, intptr_t fromLine, intptr_t toLine, ISorter& sorter);
void removeAnyDuplicateLines(const IEditor& e);         // ScintillaEditView::removeAnyDuplicateLines
void removeConsecutiveDuplicateLines(const IEditor& e); // Notepad_plus::removeDuplicateLines
void removeEmptyLines(const IEditor& e, bool blankContained);
void insertNewLineAboveCurrentLine(const IEditor& e);
void insertNewLineBelowCurrentLine(const IEditor& e);

// Edit > Blank Operations
enum class TrimPart { Head, Tail, Both };
void doTrim(const IEditor& e, TrimPart part);
void eol2ws(const IEditor& e);
void trimAll(const IEditor& e); // IDM_EDIT_TRIMALL
enum class SpaceTab { Tab2Space, Space2TabLeading, Space2TabAll };
void wsTabConvert(const IEditor& e, SpaceTab way);

} // namespace npp::core
