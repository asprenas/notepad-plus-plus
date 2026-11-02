qt_add_executable(notepadpp-qt
    main.cpp
    MainWindow.h
    MainWindow.cpp
    editor/LanguageConfig.h
    editor/LanguageConfig.cpp
    docking/DockManager.h
    docking/DockManager.cpp
)

# --- docking backend: selected here and in DockManager.cpp only ---------------
if(NPP_DOCKING_BACKEND STREQUAL "ADS")
    target_sources(notepadpp-qt PRIVATE
        docking/ads/AdsDockManager.h
        docking/ads/AdsDockManager.cpp)
    target_compile_definitions(notepadpp-qt PRIVATE NPP_DOCKING_BACKEND_ADS)
    target_link_libraries(notepadpp-qt PRIVATE ads::qtadvanceddocking)
else()
    target_sources(notepadpp-qt PRIVATE
        docking/qt/QtDockManager.h
        docking/qt/QtDockManager.cpp)
    target_compile_definitions(notepadpp-qt PRIVATE NPP_DOCKING_BACKEND_QT)
endif()

target_include_directories(notepadpp-qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(notepadpp-qt PRIVATE NPP_QT_VERSION="${PROJECT_VERSION}")
target_link_libraries(notepadpp-qt PRIVATE
    npp_scintilla
    npp_lexilla
    npp_pugixml
    Qt6::Widgets)

set_target_properties(notepadpp-qt PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.asprenas.notepadpp-qt"
    MACOSX_BUNDLE_BUNDLE_NAME "Notepad++ Qt"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

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
    VERBATIM)
