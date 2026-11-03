#include "Macro.h"

#include "core/editor/IEditor.h"

#include <Scintilla.h>

namespace npp::core {

namespace {

bool takesStringArgument(int message)
{
    switch (message) {
#define NPP_MACRO_STRINGARG(m) case m:
#define NPP_MACRO_SPARAM(m)
#define NPP_MACRO_LPARAM(m)
#include "MacroMessages.inc"
#undef NPP_MACRO_STRINGARG
#undef NPP_MACRO_SPARAM
#undef NPP_MACRO_LPARAM
        return true;
    default:
        return false;
    }
}

} // namespace

MacroStep recordScintillaStep(int message, uintptr_t wParam, intptr_t lParam)
{
    MacroStep s;
    s.type = kMacroUseLParam;
    s.message = message;
    s.wParam = static_cast<long long>(wParam);
    s.lParam = lParam;
    if (lParam && takesStringArgument(message)) {
        s.sParam = reinterpret_cast<const char*>(lParam);
        s.type = kMacroUseSParam;
        s.lParam = 0;
    }
    return s;
}

MacroStep menuCommandStep(int commandId)
{
    MacroStep s;
    s.type = kMacroMenuCommand;
    s.wParam = commandId;
    return s;
}

bool isMacroable(const MacroStep& s)
{
    switch (s.message) {
#define NPP_MACRO_SPARAM(m) case m: return s.type == kMacroUseSParam;
#define NPP_MACRO_LPARAM(m) case m: return s.type == kMacroUseLParam;
#define NPP_MACRO_STRINGARG(m)
#include "MacroMessages.inc"
#undef NPP_MACRO_STRINGARG
#undef NPP_MACRO_SPARAM
#undef NPP_MACRO_LPARAM
    default:
        return false; // display changes and others are filtered out
    }
}

void playScintillaStep(const IEditor& e, const MacroStep& s)
{
    if (!isMacroable(s))
        return;
    if (s.type == kMacroUseSParam)
        e.send(static_cast<unsigned>(s.message), static_cast<uintptr_t>(s.wParam), reinterpret_cast<intptr_t>(s.sParam.c_str()));
    else
        e.send(static_cast<unsigned>(s.message), static_cast<uintptr_t>(s.wParam), static_cast<intptr_t>(s.lParam));
}

} // namespace npp::core
