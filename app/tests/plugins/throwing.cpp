// Test plugin: a menu command and a notification throw C++ exceptions -> the plugin is disabled,
// the application keeps running.
#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_plugin_api.h>

#include <stdexcept>

namespace {
bool g_throwOnNotify = false;
void boom() { throw std::runtime_error("boom from a menu command"); }
void armNotify() { g_throwOnNotify = true; }
NppMenuItem g_items[] = {
    {"Throw", boom, 0, {}, 0},
    {"Throw on next notification", armNotify, 0, {}, 0},
};
} // namespace

extern "C" {

NPP_EXPORT const NppPluginInfo* npp_plugin_info(void)
{
    static const NppPluginInfo info{NPP_PLUGIN_API_VERSION, "Throwing", "1.0"};
    return &info;
}

NPP_EXPORT int npp_plugin_init(const NppApi*, NppPluginHandle)
{
    g_throwOnNotify = false;
    return 0;
}

NPP_EXPORT int npp_plugin_menu_items(NppMenuItem** items)
{
    *items = g_items;
    return 2;
}

NPP_EXPORT void npp_plugin_notify(const NppNotification* n)
{
    if (g_throwOnNotify && n->code == NPPN_BUFFERACTIVATED)
        throw std::logic_error("boom from a notification");
}

} // extern "C"
