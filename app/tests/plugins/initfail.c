/* Test plugin: npp_plugin_init reports a failure -> disabled, no menu. */
#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_plugin_api.h>

static void never(void) {}
static NppMenuItem g_items[] = {{"Never", never, 0, {0, 0, 0, 0}, 0}};

NPP_EXPORT const NppPluginInfo* npp_plugin_info(void)
{
    static const NppPluginInfo info = {NPP_PLUGIN_API_VERSION, "Init Fail", "1.0"};
    return &info;
}

NPP_EXPORT int npp_plugin_init(const NppApi* api, NppPluginHandle self)
{
    (void)api;
    (void)self;
    return 5;
}

NPP_EXPORT int npp_plugin_menu_items(NppMenuItem** items)
{
    *items = g_items;
    return 1;
}
