/*
 * npp_compat_win32.h: the few Win32 names that plugin sources use with Notepad++ only, so that
 * such code compiles unchanged on Windows (without <windows.h>) and Linux (phase 7).
 *
 *   HWND, WPARAM, LPARAM, LRESULT, BOOL, TRUE, FALSE, UINT, UCHAR, BYTE, WORD, DWORD, TCHAR, TEXT(), lstrcpy, lstrcat, lstrlen
 *   ::SendMessage(nppData handle, msg, w, l)   -> npp_send (application or editor)
 *   ::MessageBox(hwnd, text, title, type)      -> NppApi.messageBox
 *   MB_OK, MB_YESNO, ..., IDOK, IDYES, ...
 *
 * TCHAR is char (UTF-8), so TEXT("...") literals and lstrcpy keep working on plugin strings.
 * Do not include <windows.h> in the same translation unit: it defines SendMessage / MessageBox
 * macros. Win32 user interface code (dialogs, resources) must be rewritten (Qt or messageBox).
 */
#ifndef NPP_COMPAT_WIN32_H
#define NPP_COMPAT_WIN32_H

#include "npp_compat.h"

#include <cstring>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "npp_compat_win32.h replaces <windows.h>: do not include both in one source file"
#endif

typedef NppHandle HWND;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned char UCHAR;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef char TCHAR;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TEXT
#define TEXT(s) s
#endif

#define MB_OK              NPP_MB_OK
#define MB_OKCANCEL        NPP_MB_OKCANCEL
#define MB_YESNOCANCEL     NPP_MB_YESNOCANCEL
#define MB_YESNO           NPP_MB_YESNO
#define MB_ICONERROR       NPP_MB_ICONERROR
#define MB_ICONSTOP        NPP_MB_ICONERROR
#define MB_ICONQUESTION    NPP_MB_ICONQUESTION
#define MB_ICONWARNING     NPP_MB_ICONWARNING
#define MB_ICONEXCLAMATION NPP_MB_ICONWARNING
#define MB_ICONINFORMATION NPP_MB_ICONINFORMATION
#define IDOK     NPP_IDOK
#define IDCANCEL NPP_IDCANCEL
#define IDYES    NPP_IDYES
#define IDNO     NPP_IDNO

inline LRESULT SendMessage(HWND h, UINT msg, WPARAM w = 0, LPARAM l = 0) { return npp_send(h, msg, w, l); }

inline int MessageBox(HWND, const TCHAR* text, const TCHAR* title, UINT type)
{
    const NppApi* api = npp_compat_api();
    return api && NPP_API_HAS(api, messageBox) ? api->messageBox(text, title, type) : IDCANCEL;
}

inline TCHAR* lstrcpy(TCHAR* dst, const TCHAR* src) { return std::strcpy(dst, src); }
inline TCHAR* lstrcat(TCHAR* dst, const TCHAR* src) { return std::strcat(dst, src); }
inline int lstrlen(const TCHAR* s) { return s ? static_cast<int>(std::strlen(s)) : 0; }

#endif /* NPP_COMPAT_WIN32_H */
