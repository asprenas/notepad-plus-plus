/*
 * npp_compat.h: helps porting existing Notepad++ plugin sources to the Qt plugin API (phase 7).
 *
 * Header only, C++17. What it gives:
 *  - NppData with opaque handles, ShortcutKey, FuncItem (upstream PluginInterface.h shapes)
 *  - npp_send(handle, msg, w, l): replaces ::SendMessage(nppData._nppHandle / _scintillaXxxHandle, ...)
 *  - npp_compat_send(msg, w, l): NPPM_* message to the application (UTF-8 strings)
 *  - NPPM_* numbers (npp_msgs.h) and NPPN_* codes (npp_plugin_api.h)
 *  - glue for the entry points: npp_compat_init / npp_compat_menu_items / npp_compat_notify
 *
 * Differences from upstream (see docs/plugins/plugin-development.md, "Porting"):
 *  - strings are UTF-8 char*, not wchar_t* (buffer sizes are in bytes)
 *  - handles are not windows: no Win32 calls on them (GetWindowRect, SetParent, ...)
 *  - messages that need Win32 objects (HMENU, HWND of dialogs, HBITMAP) are not supported:
 *    docs/plugins/plugin-api-nppm.md lists them and the replacement (registerPanel, ...)
 *
 * Minimal port of a plugin:
 *
 *   #define NPP_PLUGIN_IMPLEMENTATION
 *   #include <npp_compat.h>
 *   static FuncItem funcItem[2];
 *   static NppData nppData;
 *   extern "C" {
 *   NPP_EXPORT const NppPluginInfo* npp_plugin_info(void) { static NppPluginInfo i{NPP_PLUGIN_API_VERSION, "My plugin", "1.0"}; return &i; }
 *   NPP_EXPORT int npp_plugin_init(const NppApi* api, NppPluginHandle self) { nppData = npp_compat_init(api, self); return 0; }
 *   NPP_EXPORT int npp_plugin_menu_items(NppMenuItem** items) { commandMenuInit(); return npp_compat_menu_items(funcItem, 2, items); }
 *   NPP_EXPORT void npp_plugin_notify(const NppNotification* n) { npp_compat_notify(n, beNotified); }
 *   }
 */
#ifndef NPP_COMPAT_H
#define NPP_COMPAT_H

#ifndef __cplusplus
#error "npp_compat.h needs C++ (use npp_plugin_api.h from C)"
#endif

#include "npp_plugin_api.h"
#include "npp_msgs.h"

#include <cstring>
#include <vector>

#if __has_include(<Scintilla.h>)
#  include <Scintilla.h>
#  define NPP_COMPAT_HAS_SCINTILLA 1
#endif

typedef void* NppHandle;

/* upstream NppData (HWND replaced by opaque handles usable with npp_send) */
struct NppData {
    NppHandle _nppHandle = nullptr;
    NppHandle _scintillaMainHandle = nullptr;
    NppHandle _scintillaSecondHandle = nullptr;
};

/* upstream ShortcutKey (same layout as NppShortcut) */
struct ShortcutKey {
    bool _isCtrl = false;
    bool _isAlt = false;
    bool _isShift = false;
    unsigned char _key = 0;
};

typedef void (*PFUNCPLUGINCMD)();

#ifndef NPP_COMPAT_MENU_ITEM_SIZE
#define NPP_COMPAT_MENU_ITEM_SIZE 64
#endif

/* upstream FuncItem (_itemName is UTF-8) */
struct FuncItem {
    char _itemName[NPP_COMPAT_MENU_ITEM_SIZE] = {};
    PFUNCPLUGINCMD _pFunc = nullptr;
    int _cmdID = 0;
    bool _init2Check = false;
    ShortcutKey* _pShKey = nullptr;
};

/* upstream CommunicationInfo */
typedef NppCommunicationInfo CommunicationInfo;

namespace npp_compat {

inline NppHandle nppHandle()       { return reinterpret_cast<NppHandle>(static_cast<uintptr_t>(0x4E505000)); }
inline NppHandle mainHandle()      { return reinterpret_cast<NppHandle>(static_cast<uintptr_t>(0x4E505001)); }
inline NppHandle secondHandle()    { return reinterpret_cast<NppHandle>(static_cast<uintptr_t>(0x4E505002)); }

struct State {
    const NppApi* api = nullptr;
    NppPluginHandle self = nullptr;
    FuncItem* funcItems = nullptr;
    std::vector<NppMenuItem> menuItems;
};
inline State& state()
{
    static State s;
    return s;
}

/* cmdId given by the application -> FuncItem._cmdID */
inline void syncCommandIds()
{
    State& s = state();
    if (!s.funcItems)
        return;
    for (size_t i = 0; i < s.menuItems.size(); ++i)
        s.funcItems[i]._cmdID = s.menuItems[i].cmdId;
}

} // namespace npp_compat

/* Stores the API; returns the handles of NppData. Call from npp_plugin_init. */
inline NppData npp_compat_init(const NppApi* api, NppPluginHandle self)
{
    npp_compat::state().api = api;
    npp_compat::state().self = self;
    NppData d;
    d._nppHandle = npp_compat::nppHandle();
    d._scintillaMainHandle = npp_compat::mainHandle();
    d._scintillaSecondHandle = npp_compat::secondHandle();
    return d;
}

inline const NppApi* npp_compat_api() { return npp_compat::state().api; }
inline NppPluginHandle npp_compat_self() { return npp_compat::state().self; }

/* NPPM_* message to the application */
inline intptr_t npp_compat_send(unsigned msg, uintptr_t w = 0, intptr_t l = 0)
{
    const NppApi* api = npp_compat::state().api;
    if (!api)
        return 0;
    npp_compat::syncCommandIds();
    return api->nppMessage(msg, w, l);
}

/* ::SendMessage replacement for the NppData handles */
inline intptr_t npp_send(NppHandle h, unsigned msg, uintptr_t w = 0, intptr_t l = 0)
{
    const NppApi* api = npp_compat::state().api;
    if (!api)
        return 0;
    if (h == npp_compat::mainHandle())
        return api->sendToEditor(NPP_VIEW_MAIN, msg, w, l);
    if (h == npp_compat::secondHandle())
        return api->sendToEditor(NPP_VIEW_SECOND, msg, w, l);
    return npp_compat_send(msg, w, l);
}

/* Handle of the current editor (upstream: NPPM_GETCURRENTSCINTILLA + choose the handle) */
inline NppHandle npp_compat_current_scintilla()
{
    const NppApi* api = npp_compat::state().api;
    return api && api->currentView() == NPP_VIEW_SECOND ? npp_compat::secondHandle() : npp_compat::mainHandle();
}

/* FuncItem array -> NppMenuItem array (call from npp_plugin_menu_items). The FuncItem array
   must stay alive (it usually is a global); _cmdID is filled once the application assigned ids. */
inline int npp_compat_menu_items(FuncItem* funcItems, int count, NppMenuItem** items)
{
    npp_compat::State& s = npp_compat::state();
    s.funcItems = funcItems;
    s.menuItems.assign(static_cast<size_t>(count), NppMenuItem{});
    for (int i = 0; i < count; ++i) {
        NppMenuItem& m = s.menuItems[static_cast<size_t>(i)];
        const FuncItem& f = funcItems[i];
        m.name = f._pFunc ? f._itemName : nullptr; /* upstream: no function = separator */
        m.callback = f._pFunc;
        m.checked = f._init2Check ? 1 : 0;
        if (f._pShKey)
            m.shortcut = NppShortcut{f._pShKey->_isCtrl, f._pShKey->_isAlt, f._pShKey->_isShift, f._pShKey->_key};
    }
    *items = s.menuItems.data();
    return count;
}

#ifdef NPP_COMPAT_HAS_SCINTILLA
/* Calls an upstream style beNotified(SCNotification*) for a notification. */
inline void npp_compat_notify(const NppNotification* n, void (*beNotified)(SCNotification*))
{
    npp_compat::syncCommandIds();
    SCNotification scn;
    std::memset(&scn, 0, sizeof scn);
    if (n->scn) {
        std::memcpy(&scn, n->scn, sizeof scn);
        scn.nmhdr.hwndFrom = n->view == NPP_VIEW_SECOND ? npp_compat::secondHandle() : npp_compat::mainHandle();
    } else {
        scn.nmhdr.hwndFrom = npp_compat::nppHandle();
        scn.nmhdr.idFrom = n->id;
    }
    scn.nmhdr.code = n->code;
    beNotified(&scn);
}
#endif

#endif /* NPP_COMPAT_H */
