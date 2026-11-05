# UI code as a static library: the executable and the UI tests link it.
qt_add_library(npp_ui STATIC
    MainWindow.h
    MainWindow.cpp
    MainCommands.cpp
    EditCommands.cpp
    SearchCommands.cpp
    FindCommands.cpp
    MacroCommands.cpp
    Highlighters.cpp
    Completion.cpp
    ToolDialogs.cpp
    SettingsCommands.cpp
    ContextMenus.cpp
    PanelCommands.cpp
    WindowCommands.cpp
    ToolCommands.cpp
    RunCommands.cpp
    PrintCommands.cpp
    TrayCommands.cpp
    panels/FunctionListPanel.h
    panels/FunctionListPanel.cpp
    panels/DocumentMapPanel.h
    panels/DocumentMapPanel.cpp
    panels/ProjectPanel.h
    panels/ProjectPanel.cpp
    panels/FolderAsWorkspacePanel.h
    panels/FolderAsWorkspacePanel.cpp
    panels/DocumentListPanel.h
    panels/DocumentListPanel.cpp
    panels/SmallPanels.h
    panels/SmallPanels.cpp
    panels/HtmlCharNames.inc
    panels/UdlPanel.h
    panels/UdlPanel.cpp
    PlatformShell.h
    ElevatedSave.h
    ElevatedSave.cpp
    CrashHandler.h
    CrashHandler.cpp
    StartupLog.h
    DialogTranslator.h
    DialogTranslator.cpp
    Toolbar.cpp
    menu/ToolbarButtons.inc
    Theme.cpp
    StyleConfigurator.h
    StyleConfigurator.cpp
    ShortcutMapper.h
    ShortcutMapper.cpp
    prefs/PreferencesDialog.h
    prefs/PreferencesDialog.cpp
    prefs/PreferenceBindings.cpp
    prefs/PreferencePages.inc
    menu/MacroableCommands.inc
    search/FindReplaceDialog.h
    search/FindReplaceDialog.cpp
    search/SearchResultsPanel.h
    search/SearchResultsPanel.cpp
    menu/ActionRegistry.h
    menu/ActionRegistry.cpp
    menu/CommandIds.h
    editor/EditorAdapter.h
    editor/EditorPane.h
    editor/EditorPane.cpp
    docking/DockManager.h
    docking/DockManager.cpp
    plugins/PluginHost.h
    plugins/PluginHost.cpp
    plugins/PluginMessages.cpp
)
# Exceptions thrown by plugin code through the C entry points are caught (/EHs: extern "C" may throw)
if(MSVC)
    set_source_files_properties(plugins/PluginHost.cpp plugins/PluginMessages.cpp PROPERTIES COMPILE_OPTIONS "/EHs")
endif()

# --- docking backend: selected here and in DockManager.cpp only ---------------
if(NPP_DOCKING_BACKEND STREQUAL "ADS")
    target_sources(npp_ui PRIVATE
        docking/ads/AdsDockManager.h
        docking/ads/AdsDockManager.cpp)
    target_compile_definitions(npp_ui PRIVATE NPP_DOCKING_BACKEND_ADS)
    target_link_libraries(npp_ui PRIVATE ads::qtadvanceddocking)
else()
    target_sources(npp_ui PRIVATE
        docking/qt/QtDockManager.h
        docking/qt/QtDockManager.cpp)
    target_compile_definitions(npp_ui PRIVATE NPP_DOCKING_BACKEND_QT)
endif()

target_include_directories(npp_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(npp_ui PUBLIC NPP_QT_VERSION="${PROJECT_VERSION}")
target_link_libraries(npp_ui PUBLIC
    npp_scintilla
    npp_core
    npp_plugin_api
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Network)

if(WIN32)
    target_link_libraries(npp_ui PRIVATE shell32 advapi32 dbghelp)
endif()

qt_add_executable(notepadpp-qt main.cpp)
target_link_libraries(notepadpp-qt PRIVATE npp_ui)

# --- application icon: window icon (all platforms) + Explorer icon / version info (Windows)
set(NPP_ICON_SIZES 16 32 48 64 128)
set(NPP_ICON_FILES)
foreach(size IN LISTS NPP_ICON_SIZES)
    list(APPEND NPP_ICON_FILES resources/icons/notepadpp-qt-${size}.png)
endforeach()
qt_add_resources(notepadpp-qt "appicon" PREFIX "/" FILES ${NPP_ICON_FILES})
if(WIN32)
    set(NPP_ICO "${NPP_ROOT_DIR}/PowerEditor/src/icons/npp.ico")
    configure_file(resources/notepadpp-qt.rc.in ${CMAKE_CURRENT_BINARY_DIR}/notepadpp-qt.rc @ONLY)
    target_sources(notepadpp-qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/notepadpp-qt.rc)
endif()

set_target_properties(notepadpp-qt PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.asprenas.notepadpp-qt"
    MACOSX_BUNDLE_BUNDLE_NAME "Notepad++ Qt"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# --- Windows: copy Qt runtime next to the executable (IDE run/debug without PATH setup)
option(NPP_DEPLOY_QT_RUNTIME "Run windeployqt after building (Windows)" OFF)
if(WIN32 AND NPP_DEPLOY_QT_RUNTIME)
    if(TARGET Qt6::windeployqt)
        set(NPP_WINDEPLOYQT $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(NPP_WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" REQUIRED)
    endif()
    add_custom_command(TARGET notepadpp-qt POST_BUILD
        COMMAND "${NPP_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                --no-compiler-runtime --verbose 0 "$<TARGET_FILE:notepadpp-qt>"
        COMMENT "windeployqt: copying Qt runtime next to notepadpp-qt"
        VERBATIM)
endif()

# --- runtime data (upstream model files) next to the executable ---------------
if(APPLE)
    set(NPP_DATA_OUT "$<TARGET_BUNDLE_CONTENT_DIR:notepadpp-qt>/Resources/data")
else()
    set(NPP_DATA_OUT "$<TARGET_FILE_DIR:notepadpp-qt>/data")
endif()
add_custom_command(TARGET notepadpp-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NPP_DATA_OUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NPP_LEGACY_SRC_DIR}/langs.model.xml
        ${NPP_LEGACY_SRC_DIR}/stylers.model.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/data/contextMenu.model.xml
        "${NPP_DATA_OUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_ROOT_DIR}/PowerEditor/bin/userDefineLangs "${NPP_DATA_OUT}/userDefineLangs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_ROOT_DIR}/PowerEditor/installer/APIs "${NPP_DATA_OUT}/autoCompletion"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_ROOT_DIR}/PowerEditor/installer/nativeLang "${NPP_DATA_OUT}/localization"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_ROOT_DIR}/PowerEditor/installer/themes "${NPP_DATA_OUT}/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_ROOT_DIR}/PowerEditor/installer/functionList "${NPP_DATA_OUT}/functionList"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_LEGACY_SRC_DIR}/icons "${NPP_DATA_OUT}/icons"
    VERBATIM)

# --- install layout (packaging: ci/package-windows.ps1, ci/package-linux.sh) ----------------
#  Windows: <prefix>/notepadpp-qt.exe + <prefix>/data/   (Qt runtime added by windeployqt)
#  Linux:   <prefix>/bin/notepadpp-qt + <prefix>/share/notepadpp-qt/data/ + desktop/icons
if(WIN32)
    install(TARGETS notepadpp-qt RUNTIME DESTINATION .)
    install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/ DESTINATION data)
elseif(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS notepadpp-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/notepadpp-qt/data)
    install(FILES ${NPP_ROOT_DIR}/packaging/linux/notepadpp-qt.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${NPP_ROOT_DIR}/packaging/linux/notepadpp-qt.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size IN LISTS NPP_ICON_SIZES)
        install(FILES resources/icons/notepadpp-qt-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME notepadpp-qt.png)
    endforeach()
endif()
