/*
 * Sample plugin without user interface, written in C (phase 7, P7-06).
 *
 * Shows: the C ABI only (no Qt, no C++), menu items with a default shortcut and a check mark,
 * editing the document with SCI_* messages, NPPN_* / SCN_* notifications, messages from other
 * plugins (NPPM_MSGTOPLUGIN) and a message box of the application.
 *
 * Plugins > Text Case:
 *   UPPERCASE                     Ctrl+Alt+Shift+U   selection to upper case (Scintilla)
 *   lowercase                     Ctrl+Alt+Shift+L
 *   Swap case                                        ASCII letters of the selection
 *   ---
 *   Trim trailing spaces on save  (check)            on NPPN_FILEBEFORESAVE
 *   Statistics...                                    notifications received so far
 */
#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_plugin_api.h>
#include <Scintilla.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NPPM_MSGTOPLUGIN protocol of this plugin (used by the tests and by other plugins):
   internalMsg TEXTCASE_MSG_STATS: info points to a TextCaseStats that is filled. */
#define TEXTCASE_MSG_STATS 1
typedef struct TextCaseStats {
    int ready, opened, saved, closed, activated, langChanged, shutdown, modified;
} TextCaseStats;

static const NppApi* g_api;
static TextCaseStats g_stats;
static int g_trimOnSave;

static void toUpper(void) { g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_UPPERCASE, 0, 0); }
static void toLower(void) { g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_LOWERCASE, 0, 0); }

static void swapCase(void)
{
    const intptr_t len = g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_GETSELTEXT, 0, 0); /* without the NUL */
    char* text;
    intptr_t i;
    if (len <= 0)
        return;
    text = (char*)malloc((size_t)len + 1);
    if (!text)
        return;
    g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_GETSELTEXT, 0, (intptr_t)text);
    for (i = 0; i < len; ++i) {
        const char c = text[i];
        if (c >= 'a' && c <= 'z')
            text[i] = (char)(c - 'a' + 'A');
        else if (c >= 'A' && c <= 'Z')
            text[i] = (char)(c - 'A' + 'a');
    }
    text[len] = '\0';
    {
        const intptr_t start = g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_GETSELECTIONSTART, 0, 0);
        g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_REPLACESEL, 0, (intptr_t)text);
        g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_SETSEL, (uintptr_t)start, start + len);
    }
    free(text);
}

static void trimTrailingSpaces(intptr_t view)
{
    const intptr_t lines = g_api->sendToEditor(view, SCI_GETLINECOUNT, 0, 0);
    intptr_t line;
    g_api->sendToEditor(view, SCI_BEGINUNDOACTION, 0, 0);
    for (line = 0; line < lines; ++line) {
        const intptr_t start = g_api->sendToEditor(view, SCI_POSITIONFROMLINE, (uintptr_t)line, 0);
        const intptr_t end = g_api->sendToEditor(view, SCI_GETLINEENDPOSITION, (uintptr_t)line, 0);
        intptr_t p = end;
        while (p > start) {
            const intptr_t c = g_api->sendToEditor(view, SCI_GETCHARAT, (uintptr_t)(p - 1), 0);
            if (c != ' ' && c != '\t')
                break;
            --p;
        }
        if (p < end) {
            g_api->sendToEditor(view, SCI_SETTARGETRANGE, (uintptr_t)p, end);
            g_api->sendToEditor(view, SCI_REPLACETARGET, 0, (intptr_t)"");
        }
    }
    g_api->sendToEditor(view, SCI_ENDUNDOACTION, 0, 0);
}

static void toggleTrim(void);
static void showStatistics(void);

static NppMenuItem g_items[] = {
    {"UPPERCASE", toUpper, 0, {1, 1, 1, 'U'}, 0},
    {"lowercase", toLower, 0, {1, 1, 1, 'L'}, 0},
    {"Swap case", swapCase, 0, {0, 0, 0, 0}, 0},
    {NULL, NULL, 0, {0, 0, 0, 0}, 0},
    {"Trim trailing spaces on save", toggleTrim, 0, {0, 0, 0, 0}, 0},
    {"Statistics...", showStatistics, 0, {0, 0, 0, 0}, 0},
};
enum { kTrimItem = 4 };

static void toggleTrim(void)
{
    g_trimOnSave = !g_trimOnSave;
    g_api->setMenuItemCheck(g_items[kTrimItem].cmdId, g_trimOnSave);
}

static void showStatistics(void)
{
    char text[512];
    snprintf(text, sizeof text,
             "Notifications received:\n\nready: %d\nfiles opened: %d\nfiles saved: %d\nfiles closed: %d\n"
             "buffers activated: %d\nlanguage changes: %d\ntext modifications (SCN_MODIFIED): %d",
             g_stats.ready, g_stats.opened, g_stats.saved, g_stats.closed, g_stats.activated, g_stats.langChanged,
             g_stats.modified);
    g_api->messageBox(text, "Text Case", NPP_MB_OK | NPP_MB_ICONINFORMATION);
}

NPP_EXPORT const NppPluginInfo* npp_plugin_info(void)
{
    static const NppPluginInfo info = {NPP_PLUGIN_API_VERSION, "Text Case", "1.0"};
    return &info;
}

NPP_EXPORT int npp_plugin_init(const NppApi* api, NppPluginHandle self)
{
    (void)self;
    if (!NPP_API_HAS(api, sendToEditor) || !NPP_API_HAS(api, messageBox))
        return 1;
    g_api = api;
    memset(&g_stats, 0, sizeof g_stats);
    g_trimOnSave = 0;
    return 0;
}

NPP_EXPORT int npp_plugin_menu_items(NppMenuItem** items)
{
    *items = g_items;
    return (int)(sizeof g_items / sizeof g_items[0]);
}

NPP_EXPORT void npp_plugin_notify(const NppNotification* n)
{
    switch (n->code) {
    case NPPN_READY: ++g_stats.ready; break;
    case NPPN_FILEOPENED: ++g_stats.opened; break;
    case NPPN_FILEBEFORESAVE:
        if (g_trimOnSave) {
            /* the document being saved is the current one when saved from the menu */
            if (g_api->currentBufferId() == (NppBufferId)n->id)
                trimTrailingSpaces(NPP_VIEW_CURRENT);
        }
        break;
    case NPPN_FILESAVED: ++g_stats.saved; break;
    case NPPN_FILECLOSED: ++g_stats.closed; break;
    case NPPN_BUFFERACTIVATED: ++g_stats.activated; break;
    case NPPN_LANGCHANGED: ++g_stats.langChanged; break;
    case NPPN_SHUTDOWN: ++g_stats.shutdown; break;
    case SCN_MODIFIED: {
        const SCNotification* scn = (const SCNotification*)n->scn;
        if (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
            ++g_stats.modified;
        break;
    }
    default: break;
    }
}

NPP_EXPORT intptr_t npp_plugin_message(unsigned msg, uintptr_t w, intptr_t l)
{
    (void)msg;
    (void)w;
    {
        const NppCommunicationInfo* ci = (const NppCommunicationInfo*)l;
        if (ci && ci->internalMsg == TEXTCASE_MSG_STATS && ci->info) {
            memcpy(ci->info, &g_stats, sizeof g_stats);
            return 1;
        }
    }
    return 0;
}

NPP_EXPORT void npp_plugin_shutdown(void)
{
    g_api = NULL;
}
