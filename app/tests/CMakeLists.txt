find_package(Qt6 REQUIRED COMPONENTS Test)

# Make tests runnable from plain ctest / IDEs on Windows: Qt DLLs on PATH.
function(npp_test_env test)
    if(WIN32)
        set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

# --- UI smoke test (phase 0) ---------------------------------------------------------
add_executable(test_editor_smoke
    test_editor_smoke.cpp
    ${CMAKE_SOURCE_DIR}/app/ui/editor/LanguageConfig.cpp)
target_include_directories(test_editor_smoke PRIVATE ${CMAKE_SOURCE_DIR}/app/ui)
target_compile_definitions(test_editor_smoke PRIVATE NPP_TEST_DATA_DIR="${NPP_LEGACY_SRC_DIR}")
target_link_libraries(test_editor_smoke PRIVATE npp_scintilla npp_lexilla npp_pugixml Qt6::Widgets)
add_test(NAME editor_smoke COMMAND test_editor_smoke)
set_tests_properties(editor_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
npp_test_env(editor_smoke)

# --- npp-core unit tests (phase 1): one Qt Test executable per module -------------------
# npp_core_add_test(<name> SOURCES ... [LIBS ...]) -> target test_core_<name>, CTest core.<name>
function(npp_core_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    set(target test_core_${name})
    add_executable(${target} ${ARG_SOURCES})
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_link_libraries(${target} PRIVATE npp_core Qt6::Test ${ARG_LIBS})
    target_compile_definitions(${target} PRIVATE
        NPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        NPP_LEGACY_SRC_DIR="${NPP_LEGACY_SRC_DIR}"
        NPP_ROOT_DIR="${NPP_ROOT_DIR}")
    add_test(NAME core.${name} COMMAND ${target})
    set_tests_properties(core.${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    npp_test_env(core.${name})
endfunction()

npp_core_add_test(util SOURCES core/test_util.cpp)
npp_core_add_test(encoding SOURCES core/test_encoding.cpp)
npp_core_add_test(i18n SOURCES core/test_i18n.cpp)
npp_core_add_test(config SOURCES core/test_config.cpp)
npp_core_add_test(lang SOURCES core/test_lang.cpp LIBS npp_scintilla Qt6::Widgets)
npp_core_add_test(buffer SOURCES core/test_buffer.cpp LIBS npp_scintilla Qt6::Widgets)
npp_core_add_test(search SOURCES core/test_search.cpp LIBS npp_scintilla Qt6::Widgets)
