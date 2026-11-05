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
npp_core_add_test(regexcompat SOURCES core/test_regexcompat.cpp LIBS npp_scintilla Qt6::Widgets)
npp_core_add_test(urldetect SOURCES core/test_urldetect.cpp)
npp_core_add_test(funclist SOURCES core/test_funclist.cpp LIBS npp_scintilla Qt6::Widgets)
npp_core_add_test(autocompletion SOURCES core/test_autocompletion.cpp LIBS npp_scintilla Qt6::Widgets)

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
target_compile_definitions(test_ui_find PRIVATE NPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME ui.find COMMAND test_ui_find)
set_tests_properties(ui.find PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)
if(WIN32)
    # Find in Files responsiveness on the real platform: with the headless platforms a Qt animation
    # timer event holds the nested event loop for seconds on Windows (not with the real platform).
    # Only this test function: the others use the clipboard, which must not be the user's one.
    add_test(NAME ui.find.responsiveness COMMAND test_ui_find findInFilesLargeFolderIsResponsiveAndCancellable)
    set_tests_properties(ui.find.responsiveness PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 600)
    npp_test_env(ui.find.responsiveness)
endif()
npp_test_env(ui.find)

add_executable(test_ui_settings ui/test_settings.cpp)
set_target_properties(test_ui_settings PROPERTIES AUTOMOC ON)
target_link_libraries(test_ui_settings PRIVATE npp_ui Qt6::Test)
add_dependencies(test_ui_settings notepadpp-qt)
add_test(NAME ui.settings COMMAND test_ui_settings)
set_tests_properties(ui.settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
npp_test_env(ui.settings)
# "Follow system" dark mode needs a platform plugin that can change the colour scheme
# (offscreen cannot): the real platform on Windows, xcb + GTK platform theme under Xvfb on Linux.
if(WIN32)
    add_test(NAME ui.settings.systemtheme COMMAND test_ui_settings systemModeFollowsColorScheme)
    set_tests_properties(ui.settings.systemtheme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    npp_test_env(ui.settings.systemtheme)
elseif(UNIX AND NOT APPLE)
    find_program(NPP_XVFB_RUN xvfb-run)
    if(NPP_XVFB_RUN)
        add_test(NAME ui.settings.systemtheme COMMAND ${NPP_XVFB_RUN} -a $<TARGET_FILE:test_ui_settings> systemModeFollowsColorScheme)
        set_tests_properties(ui.settings.systemtheme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb;QT_QPA_PLATFORMTHEME=gtk3" SKIP_REGULAR_EXPRESSION "SKIP   :")
    endif()
endif()

# --- settings compatibility helper (tools/settings-compat/run-with-npp.ps1, phase 4) --------
add_executable(settings_compat tools/settings_compat.cpp)
target_link_libraries(settings_compat PRIVATE npp_core)

# --- phase 5: panels and remaining dialogs ------------------------------------------------
add_executable(test_ui_panels ui/test_panels.cpp)
set_target_properties(test_ui_panels PROPERTIES AUTOMOC ON)
target_link_libraries(test_ui_panels PRIVATE npp_ui Qt6::Test Qt6::PrintSupport)
add_dependencies(test_ui_panels notepadpp-qt)
target_compile_definitions(test_ui_panels PRIVATE NPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" NPP_ROOT_DIR="${NPP_ROOT_DIR}")
add_test(NAME ui.panels COMMAND test_ui_panels)
set_tests_properties(ui.panels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
npp_test_env(ui.panels)
if(WIN32)
    # UDL highlighting screenshots with real fonts (the offscreen platform has none)
    add_test(NAME ui.panels.udlscreenshots COMMAND test_ui_panels udlFilesImportAndHighlight)
    set_tests_properties(ui.panels.udlscreenshots PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    npp_test_env(ui.panels.udlscreenshots)
endif()

# --- phase 6: crash report and recovery after an abnormal end ---------------------------
add_test(NAME app.crashrecovery
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:notepadpp-qt> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/crashrecovery
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/crash_recovery.cmake)
set_tests_properties(app.crashrecovery PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 150)
npp_test_env(app.crashrecovery)
add_test(NAME app.elevatedcopy
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:notepadpp-qt> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/elevatedcopy
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/elevated_copy.cmake)
npp_test_env(app.elevatedcopy)
