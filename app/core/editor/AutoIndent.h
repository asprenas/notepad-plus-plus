// Auto-indentation on typing (upstream Notepad_plus::maintainIndentation).
#pragma once

#include "core/lang/LangType.h"

namespace npp::core {

class IEditor;

// GuiConfig maintainIndent values (upstream autoIndent_*)
inline constexpr int kAutoIndentNone = 0, kAutoIndentBasic = 1, kAutoIndentAdvanced = 2;
// Called on SCN_CHARADDED (not while recording / playing a macro)
void maintainIndentation(const IEditor& e, LangType type, int maintainIndent, int ch);

} // namespace npp::core
