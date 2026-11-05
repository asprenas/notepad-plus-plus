# MIME Tools: port to the Notepad++ Qt plugin API (phase 7, P7-07)

Source: https://github.com/npp-plugins/mimetools, version 3.2, commit `7a54297` (2026-10-31).
License: GNU GPL (see `license.txt`), same as Notepad++ and this port.

The whole menu is available on Windows and Linux (22 entries with separators): Base64 encode
(4 variants) / decode (3 variants), Quoted-printable encode / decode, URL encode (RFC1738, extended,
full, each also by line) / decode, SAML decode, About. The encoders and decoders (`b64.cpp`,
`qp.cpp`, `url.cpp`, `saml.cpp`, `tinflate.c`) are unchanged.

Changes (full diff against the upstream files: `port.patch`, 145 lines removed, 28 added, of ~1960):

| File | Change |
|---|---|
| `src/PluginInterface.h` | New: replaces upstream `PluginInterface.h` / `Notepad_plus_msgs.h` / `Scintilla.h` copies by `npp_compat_win32.h` + `Scintilla.h` of the application (NppData, FuncItem, `::SendMessage`, `::MessageBox`, `TCHAR`, `TEXT`, `lstrcpy`, NPPM numbers) |
| `src/b64.h`, `src/qp.h` | `#include <windows.h>` -> `#include "PluginInterface.h"` (`UCHAR`, `memset` came from it) |
| `src/mimeTools.cpp` | `DllMain(DLL_PROCESS_ATTACH)` body -> `commandMenuInit()`; `setInfo` / `getName` / `getFuncsArray` / `beNotified` / `isUnicode` / `messageProc` -> `npp_plugin_info` / `npp_plugin_init` / `npp_plugin_menu_items` (via `npp_compat_init` and `npp_compat_menu_items`); About dialog (Win32 resource `mimeTools.rc`) -> `::MessageBox`; dark mode handling of that dialog removed (`NPPN_DARKMODECHANGED`) |
| `mimeTools.rc`, `vs.proj/` | Not used (Win32 resources and Visual Studio project) |

Everything else, including every `::SendMessage(hCurrScintilla, SCI_..., ...)` call and
`getCurrentScintillaHandle()`, compiles as it was.

Build: `npp_add_plugin(mimeTools ...)` in `CMakeLists.txt` -> `<build>/plugins/mimeTools/mimeTools.dll`
(Linux `libmimeTools.so`). Install: copy that folder to `<settings>/plugins/`.

Tests: `ui.plugins` (`portedMimeToolsEncodesAndDecodes`): through the menu commands, Base64
(padding, by line, decode, decode strict), URL (RFC1738, full, decode), Quoted-printable and SAML
decode of UTF-8 text, compared with known results.
