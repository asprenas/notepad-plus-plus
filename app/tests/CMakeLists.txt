add_executable(test_editor_smoke
    test_editor_smoke.cpp
    ${CMAKE_SOURCE_DIR}/app/ui/editor/LanguageConfig.cpp)
target_include_directories(test_editor_smoke PRIVATE ${CMAKE_SOURCE_DIR}/app/ui)
target_compile_definitions(test_editor_smoke PRIVATE NPP_TEST_DATA_DIR="${NPP_LEGACY_SRC_DIR}")
target_link_libraries(test_editor_smoke PRIVATE npp_scintilla npp_lexilla npp_pugixml Qt6::Widgets)

add_test(NAME editor_smoke COMMAND test_editor_smoke)
set_tests_properties(editor_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
