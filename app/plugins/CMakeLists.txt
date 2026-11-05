# Plugins built with the application (phase 7): two samples and one ported public plugin.
# Output: <build>/plugins/<name>/<library> (see npp_add_plugin in app/plugin-api/CMakeLists.txt).
npp_add_plugin(textcase SOURCES samples/textcase/textcase.c)
npp_add_plugin(docinfo QT SOURCES samples/docinfo/docinfo.cpp)
# toolbar icons of the plugin, next to its library (upstream Document List toolbar icons)
add_custom_command(TARGET docinfo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NPP_LEGACY_SRC_DIR}/icons/light/toolbar/regular/docList_off.ico ${NPP_PLUGIN_OUTPUT_DIR}/docinfo/docinfo.ico
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NPP_LEGACY_SRC_DIR}/icons/dark/toolbar/regular/docList_off.ico ${NPP_PLUGIN_OUTPUT_DIR}/docinfo/docinfo_dark.ico
    VERBATIM)
add_subdirectory(ports/mimetools)
