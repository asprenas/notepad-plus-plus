/* Test plugin: exports of an upstream Win32 plugin only (getName / setInfo) -> refused with a hint. */
#include <npp_plugin_api.h>

NPP_EXPORT const char* getName(void) { return "Win32 Plugin"; }
NPP_EXPORT void setInfo(void* nppData) { (void)nppData; }
