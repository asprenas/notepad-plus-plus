/* Test plugin: built for another plugin API version -> refused, the application keeps running. */
#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_plugin_api.h>

NPP_EXPORT const NppPluginInfo* npp_plugin_info(void)
{
    static const NppPluginInfo info = {NPP_PLUGIN_API_VERSION + 98, "Bad Version", "1.0"};
    return &info;
}

NPP_EXPORT int npp_plugin_init(const NppApi* api, NppPluginHandle self)
{
    (void)api;
    (void)self;
    return 0;
}
