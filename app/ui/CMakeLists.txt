# UI code as a static library: the executable and the UI tests link it.
qt_add_library(npp_ui STATIC
    MainWindow.h
    MainWindow.cpp
    MainCommands.cpp
    EditCommands.cpp
    SearchCommands.cpp
    FindCommands.cpp
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
)

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
    Qt6::Widgets
    Qt6::Network)

qt_add_executable(notepadpp-qt main.cpp)
target_link_libraries(notepadpp-qt PRIVATE npp_ui)

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
        "${NPP_DATA_OUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NPP_ROOT_DIR}/PowerEditor/bin/userDefineLangs "${NPP_DATA_OUT}/userDefineLangs"
    VERBATIM)
