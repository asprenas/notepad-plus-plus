/*
 * Notepad++ Qt plugin API, version 1 (phase 7).
 *
 * C ABI only: plain C functions and POD structures cross the plugin boundary, so a plugin
 * may be built with another compiler than the application (MSVC plugin in a MinGW build,
 * for example). The single exception is the panel widget of registerPanel (a QWidget*):
 * plugins with a Qt user interface must be linked against the same Qt major/minor version
 * as the application and built with an ABI compatible compiler.
 *
 * Strings are UTF-8 everywhere. Buffers filled by the application ("buf, bufSize") always
 * receive a NUL terminated string when bufSize > 0; the return value is the full length in
 * bytes without the NUL (call with bufSize 0 to ask for the length), or -1 on error.
 *
 * Compatibility rules for later versions:
 *  - NPP_PLUGIN_API_VERSION changes only when existing members change meaning (a version 1
 *    plugin is refused by a version 2 application and the other way round).
 *  - New NppApi members are appended; NppApi.size tells which members exist. A plugin that
 *    uses a member added later checks NPP_API_HAS(api, member) first.
 *
 * Exported by a plugin (see docs/plugins/plugin-development.md):
 *   npp_plugin_info        required  identification, API version
 *   npp_plugin_init        required  receives the NppApi; return 0 = success
 *   npp_plugin_menu_items  optional  commands of the plugin's sub menu of Plugins
 *   npp_plugin_notify      optional  NPPN_* and SCN_* notifications
 *   npp_plugin_message     optional  NPPM_MSGTOPLUGIN messages from other plugins
 *   npp_plugin_shutdown    optional  before the library is unloaded
 */
#ifndef NPP_PLUGIN_API_H
#define NPP_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NPP_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#  define NPP_EXPORT __declspec(dllexport)
#else
#  define NPP_EXPORT __attribute__((visibility("default")))
#endif

/* Editor views (sendToEditor, currentView, notifications) */
#define NPP_VIEW_CURRENT (-1)
#define NPP_VIEW_MAIN    0
#define NPP_VIEW_SECOND  1

/* messageBox: Win32 MessageBox values (MB_*, ID*) */
#define NPP_MB_OK              0x0
#define NPP_MB_OKCANCEL        0x1
#define NPP_MB_YESNOCANCEL     0x3
#define NPP_MB_YESNO           0x4
#define NPP_MB_ICONERROR       0x10
#define NPP_MB_ICONQUESTION    0x20
#define NPP_MB_ICONWARNING     0x30
#define NPP_MB_ICONINFORMATION 0x40
#define NPP_IDOK     1
#define NPP_IDCANCEL 2
#define NPP_IDYES    6
#define NPP_IDNO     7

/* Opaque identity of a loaded plugin, given to npp_plugin_init. */
typedef struct NppPluginHandle_* NppPluginHandle;

/* Buffer (document) id; the same value as upstream's BufferID for NPPM_* messages. */
typedef uintptr_t NppBufferId;

/* Shortcut of a menu item: Win32 virtual-key code (VK_*) as in shortcuts.xml, on every platform. */
typedef struct NppShortcut {
    uint8_t ctrl, alt, shift;
    uint8_t key;               /* 0 = no shortcut */
} NppShortcut;

/* One command of the plugin's sub menu (upstream FuncItem). */
typedef struct NppMenuItem {
    const char*  name;         /* UTF-8; NULL or "" = separator */
    void       (*callback)(void);
    int          checked;      /* initial check mark */
    NppShortcut  shortcut;     /* default shortcut (shortcuts.xml may override it) */
    int          cmdId;        /* out: command id given by the application (setMenuItemCheck, NPPM_*) */
} NppMenuItem;

/* Notification (upstream SCNotification passed to beNotified). */
typedef struct NppNotification {
    uint32_t    size;          /* sizeof(NppNotification) of the application */
    uint32_t    code;          /* NPPN_* (1001..) or SCN_* (2000..) */
    intptr_t    view;          /* SCN_*: editor view (0 / 1); NPPN_*: -1 */
    uintptr_t   id;            /* NPPN_*: upstream idFrom (buffer id, ...); SCN_*: 0 */
    const void* scn;           /* SCN_*: Scintilla's SCNotification (Scintilla.h); NPPN_*: NULL */
} NppNotification;

/* NPPM_MSGTOPLUGIN(const char* destModuleName, NppCommunicationInfo* info): the destination
   plugin receives npp_plugin_message(NPPM_MSGTOPLUGIN, 0, (intptr_t)info). Module names are
   library file names ("mimeTools.dll", "libmimeTools.so"); the comparison ignores case, the
   "lib" prefix and the extension, so "mimeTools" works everywhere. (upstream CommunicationInfo) */
typedef struct NppCommunicationInfo {
    long        internalMsg;
    const char* srcModuleName;
    void*       info;
} NppCommunicationInfo;

typedef struct NppPluginInfo {
    uint32_t    apiVersion;    /* NPP_PLUGIN_API_VERSION the plugin was built with */
    const char* name;          /* menu / display name (UTF-8) */
    const char* version;       /* plugin version text */
} NppPluginInfo;

typedef struct NppApi {
    uint32_t    size;          /* sizeof(NppApi) of the application */
    uint32_t    version;       /* NPP_PLUGIN_API_VERSION of the application */

    /* --- application ------------------------------------------------------------------- */
    const char* (*appVersion)(void);                     /* "0.2.0" */
    void*       (*mainWindow)(void);                     /* QWidget* parent for plugin dialogs */
    int         (*pluginConfigDir)(char* buf, size_t bufSize);  /* <settings>/plugins/config */
    void        (*setStatusText)(const char* text);      /* temporary status bar message */
    int         (*isDarkMode)(void);
    /* Modal message box for plugins without Qt. type: NPP_MB_* buttons | NPP_MB_ICON*;
       returns NPP_ID* of the pressed button. */
    int         (*messageBox)(const char* text, const char* title, unsigned type);

    /* --- editors and documents --------------------------------------------------------- */
    intptr_t    (*currentView)(void);                    /* NPP_VIEW_MAIN / NPP_VIEW_SECOND */
    /* Scintilla message (SCI_*) to an editor; view NPP_VIEW_CURRENT = the active one. */
    intptr_t    (*sendToEditor)(intptr_t view, unsigned msg, uintptr_t w, intptr_t l);
    NppBufferId (*currentBufferId)(void);
    int         (*bufferPath)(NppBufferId id, char* buf, size_t bufSize); /* full path or tab name */
    int         (*currentFilePath)(char* buf, size_t bufSize);
    int         (*openFile)(const char* path);           /* 1 = opened (or already open) */
    int         (*activateBuffer)(NppBufferId id);       /* 1 = found */

    /* --- commands and menus ------------------------------------------------------------ */
    void        (*runMenuCommand)(int commandId);        /* IDM_* of the main menu, or a plugin cmdId */
    void        (*setMenuItemCheck)(int cmdId, int checked);

    /* --- dockable panels ---------------------------------------------------------------- */
    /* qWidget: the panel content (QWidget*); the application takes ownership. panelId is
       unique per plugin. Returns 1 on success. Panels start hidden: call showPanel. */
    int         (*registerPanel)(NppPluginHandle self, const char* panelId, const char* title, void* qWidget);
    void        (*showPanel)(NppPluginHandle self, const char* panelId, int show);
    int         (*isPanelVisible)(NppPluginHandle self, const char* panelId);

    /* --- upstream messages ---------------------------------------------------------------
       NPPM_* message with upstream numbers and meaning, strings as UTF-8 char* instead of
       wchar_t*. Unsupported messages return 0. See npp_compat.h and docs/plugins/plugin-api-nppm.md. */
    intptr_t    (*nppMessage)(unsigned msg, uintptr_t w, intptr_t l);

    /* --- files of the plugin, toolbar ----------------------------------------------------- */
    /* Full path of the plugin's library file (its folder holds the plugin's own files). */
    int         (*pluginFilePath)(NppPluginHandle self, char* buf, size_t bufSize);
    /* Toolbar button for a menu command of the plugin (upstream NPPM_ADDTOOLBARICON_FORDARKMODE).
       Icons are image files (PNG, ICO, BMP); darkModeIconFile may be NULL. Call it when
       NPPN_TBMODIFICATION arrives. Returns 1 on success. */
    int         (*addToolbarIcon)(int cmdId, const char* iconFile, const char* darkModeIconFile);
} NppApi;

#define NPP_API_HAS(api, member) \
    ((api)->size >= offsetof(NppApi, member) + sizeof((api)->member) && (api)->member != NULL)

/* Plugin entry points (resolved by name). */
typedef const NppPluginInfo* (*npp_plugin_info_fn)(void);
typedef int  (*npp_plugin_init_fn)(const NppApi* api, NppPluginHandle self);
typedef int  (*npp_plugin_menu_items_fn)(NppMenuItem** items);   /* returns the item count */
typedef void (*npp_plugin_notify_fn)(const NppNotification* n);
typedef intptr_t (*npp_plugin_message_fn)(unsigned msg, uintptr_t w, intptr_t l);
typedef void (*npp_plugin_shutdown_fn)(void);

#ifdef NPP_PLUGIN_IMPLEMENTATION
NPP_EXPORT const NppPluginInfo* npp_plugin_info(void);
NPP_EXPORT int  npp_plugin_init(const NppApi* api, NppPluginHandle self);
NPP_EXPORT int  npp_plugin_menu_items(NppMenuItem** items);
NPP_EXPORT void npp_plugin_notify(const NppNotification* n);
NPP_EXPORT intptr_t npp_plugin_message(unsigned msg, uintptr_t w, intptr_t l);
NPP_EXPORT void npp_plugin_shutdown(void);
#endif

/* --- NPPN_* notification codes (upstream values) ---------------------------------------- */
#define NPPN_FIRST                   1000
#define NPPN_READY                   (NPPN_FIRST + 1)
#define NPPN_TBMODIFICATION          (NPPN_FIRST + 2)
#define NPPN_FILEBEFORECLOSE         (NPPN_FIRST + 3)
#define NPPN_FILEOPENED              (NPPN_FIRST + 4)
#define NPPN_FILECLOSED              (NPPN_FIRST + 5)
#define NPPN_FILEBEFOREOPEN          (NPPN_FIRST + 6)
#define NPPN_FILEBEFORESAVE          (NPPN_FIRST + 7)
#define NPPN_FILESAVED               (NPPN_FIRST + 8)
#define NPPN_SHUTDOWN                (NPPN_FIRST + 9)
#define NPPN_BUFFERACTIVATED         (NPPN_FIRST + 10)
#define NPPN_LANGCHANGED             (NPPN_FIRST + 11)
#define NPPN_WORDSTYLESUPDATED       (NPPN_FIRST + 12)
#define NPPN_SHORTCUTREMAPPED        (NPPN_FIRST + 13)
#define NPPN_FILEBEFORELOAD          (NPPN_FIRST + 14)
#define NPPN_FILELOADFAILED          (NPPN_FIRST + 15)
#define NPPN_READONLYCHANGED         (NPPN_FIRST + 16)
#define NPPN_DOCORDERCHANGED         (NPPN_FIRST + 17)
#define NPPN_SNAPSHOTDIRTYFILELOADED (NPPN_FIRST + 18)
#define NPPN_BEFORESHUTDOWN          (NPPN_FIRST + 19)
#define NPPN_CANCELSHUTDOWN          (NPPN_FIRST + 20)
#define NPPN_FILEBEFORERENAME        (NPPN_FIRST + 21)
#define NPPN_FILERENAMECANCEL        (NPPN_FIRST + 22)
#define NPPN_FILERENAMED             (NPPN_FIRST + 23)
#define NPPN_FILEBEFOREDELETE        (NPPN_FIRST + 24)
#define NPPN_FILEDELETEFAILED        (NPPN_FIRST + 25)
#define NPPN_FILEDELETED             (NPPN_FIRST + 26)
#define NPPN_DARKMODECHANGED         (NPPN_FIRST + 27)
#define NPPN_CMDLINEPLUGINMSG        (NPPN_FIRST + 28)
#define NPPN_EXTERNALLEXERBUFFER     (NPPN_FIRST + 29)
#define NPPN_GLOBALMODIFIED          (NPPN_FIRST + 30)
#define NPPN_NATIVELANGCHANGED       (NPPN_FIRST + 31)
#define NPPN_TOOLBARICONSETCHANGED   (NPPN_FIRST + 32)

#ifdef __cplusplus
}
#endif

#endif /* NPP_PLUGIN_API_H */
