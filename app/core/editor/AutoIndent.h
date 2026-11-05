// Auto-indentation on typing (upstream Notepad_plus::maintainIndentation).
#pragma once

#include "core/lang/LangType.h"

namespace npp::core {

class IEditor;

// GuiConfig maintainIndent values = upstream AutoIndentMode (NppConstants.h) as stored in config.xml:
// 0 none, 1 advanced, 2 basic
inline constexpr int kAutoIndentNone = 0, kAutoIndentAdvanced = 1, kAutoIndentBasic = 2;
// Called on SCN_CHARADDED (not while recording / playing a macro)
void maintainIndentation(const IEditor& e, LangType type, int maintainIndent, int ch);

} // namespace npp::core
