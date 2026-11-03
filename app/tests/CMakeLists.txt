find_package(Qt6 REQUIRED COMPONENTS Test)

# Make tests runnable from plain ctest / IDEs on Windows: Qt DLLs on PATH.
function(npp_test_env test)
    if(WIN32)
        set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

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
npp_core_add_test(textops SOURCES core/test_textops.cpp LIBS npp_scintilla Qt6::Widgets)

# --- UI tests (phase 2): MainWindow against a temporary user directory --------------------
add_executable(test_ui_mainwindow ui/test_mainwindow.cpp)
set_target_properties(test_ui_mainwindow PROPERTIES AUTOMOC ON)
target_link_libraries(test_ui_mainwindow PRIVATE npp_ui Qt6::Test Qt6::Network)
add_dependencies(test_ui_mainwindow notepadpp-qt) # data/ is copied next to the executable
add_test(NAME ui.mainwindow COMMAND test_ui_mainwindow)
set_tests_properties(ui.mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
npp_test_env(ui.mainwindow)

add_executable(test_ui_find ui/test_find.cpp)
set_target_properties(test_ui_find PROPERTIES AUTOMOC ON)
target_link_libraries(test_ui_find PRIVATE npp_ui Qt6::Test)
add_dependencies(test_ui_find notepadpp-qt)
add_test(NAME ui.find COMMAND test_ui_find)
set_tests_properties(ui.find PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)
npp_test_env(ui.find)
