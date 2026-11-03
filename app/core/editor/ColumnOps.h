// Column editor and "Find characters in range" (phase 3, P3-05/P3-09): ports of upstream
// ColumnEditorDlg (IDOK), ScintillaEditView::getColumnModeSelectInfo / columnReplace /
// setMultiSelections / variedFormatNumber2String and FindCharsInRangeDlg::findCharInRange.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace npp::core {

class IEditor;

enum class NumBase { Dec, Hex, HexUpper, Oct, Bin };          // upstream NumBase
enum class Leading { None, Zeros, Spaces };                     // upstream ColumnEditorParam::leadingChoice

size_t numberOfDigits(size_t number, size_t base);              // getNbDigits
// variedFormatNumber2String: `nbDigits` wide, padded according to `lead` (None pads on the right)
std::string formatColumnNumber(size_t number, NumBase base, size_t nbDigits, Leading lead);

// Insert text in a column: the column / multi selection, else at the caret column of every line
// from the caret line to the end of the document. One undo action.
void columnInsertText(const IEditor& e, const std::string& text);
void columnInsertNumbers(const IEditor& e, size_t initial, size_t increase, size_t repeat, NumBase base, Leading lead);

// Byte values in [begin, end] (e.g. 128..255 = non ASCII); selects the found byte. startPos -1:
// document start (down) / end (up).
bool findCharInRange(const IEditor& e, unsigned char begin, unsigned char end, intptr_t startPos, bool down, bool wrap);

} // namespace npp::core
