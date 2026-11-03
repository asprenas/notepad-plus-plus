// Macros (phase 3, P3-09): upstream recordedMacroStep logic on the shortcuts.xml MacroStep model.
#pragma once

#include "core/shortcut/Shortcuts.h"

#include <cstdint>

namespace npp::core {

class IEditor;

enum MacroStepType { kMacroUseLParam = 0, kMacroUseSParam = 1, kMacroMenuCommand = 2, kMacroSavedSearch = 3 };

// recordedMacroStep(iMessage, wParam, lParam): string arguments are copied to sParam
MacroStep recordScintillaStep(int message, uintptr_t wParam, intptr_t lParam);
MacroStep menuCommandStep(int commandId);
// recordedMacroStep::isMacroable (Scintilla steps only)
bool isMacroable(const MacroStep& s);
// Plays a Scintilla step on the editor (non macroable steps are ignored like upstream)
void playScintillaStep(const IEditor& e, const MacroStep& s);

} // namespace npp::core
