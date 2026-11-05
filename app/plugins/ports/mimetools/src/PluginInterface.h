// Qt port of MIME Tools: replaces upstream PluginInterface.h / Notepad_plus_msgs.h / <windows.h>.
// NppData, FuncItem, ShortcutKey, NPPM_* / NPPN_* and the Win32 names the plugin uses
// (::SendMessage on the NppData handles, ::MessageBox, TCHAR, TEXT, lstrcpy) come from the
// compatibility headers of the Notepad++ Qt plugin API.
#pragma once

#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_compat_win32.h>
#include <Scintilla.h>
