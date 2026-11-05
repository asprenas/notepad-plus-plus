add_subdirectory(thirdparty)
add_subdirectory(core)
add_subdirectory(plugin-api)
add_subdirectory(ui)

option(NPP_BUILD_PLUGINS "Build the sample plugins and the ported plugin (app/plugins)" ON)
if(NPP_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
