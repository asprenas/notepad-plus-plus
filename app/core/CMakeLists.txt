# npp-core: platform independent core (C++20 + QtCore only).
# Rules (docs/phase-1-core-extraction.md, checked by ci/check-core-rules.sh):
#  - no QtGui/QtWidgets
#  - no <windows.h>, wchar_t, std::wstring outside platform/win/

add_library(npp_core STATIC
    util/StringUtil.h
    util/StringUtil.cpp
    util/PathUtil.h
    util/PathUtil.cpp

    encoding/UniMode.h
    encoding/UnicodeConverter.h
    encoding/UnicodeConverter.cpp
    encoding/EncodingMapper.h
    encoding/EncodingMapper.cpp
    encoding/Codepage.h
    encoding/CharsetDetector.h
    encoding/CharsetDetector.cpp

    i18n/NativeLanguage.h
    i18n/NativeLanguage.cpp

    config/XmlHelpers.h
    config/XmlHelpers.cpp
    config/GuiConfigFields.inc
    config/GuiConfig.h
    config/GuiConfig.cpp
    config/ConfigFile.h
    config/ConfigFile.cpp
    config/AppPaths.h
    config/AppPaths.cpp
    config/ContextMenuConfig.h
    config/ContextMenuConfig.cpp

    session/Session.h
    session/Session.cpp

    shortcut/Shortcuts.h
    shortcut/Shortcuts.cpp

    lang/LangTypes.inc
    lang/LangType.h
    lang/LangType.cpp
    lang/LanguageRegistry.h
    lang/LanguageRegistry.cpp
    lang/StyleRegistry.h
    lang/StyleRegistry.cpp
    lang/UserLanguage.h
    lang/UserLanguage.cpp
    lang/LexerSpecs.inc
    lang/LexerSpecs.h
    lang/LexerSpecs.cpp

    editor/IEditor.h
    editor/IEditor.cpp
    editor/EditorLanguage.h
    editor/EditorLanguage.cpp
    editor/Sorters.h
    editor/TextOps.h
    editor/TextOps.cpp
    editor/CommentOps.h
    editor/CommentOps.cpp

    buffer/Buffer.h
    buffer/FileManager.h
    buffer/FileManager.cpp

    search/Search.h
    search/Search.cpp
)

if(WIN32)
    target_sources(npp_core PRIVATE platform/win/CodepageWin.cpp)
else()
    target_sources(npp_core PRIVATE platform/posix/CodepagePosix.cpp)
    find_package(Iconv REQUIRED)
    target_link_libraries(npp_core PRIVATE Iconv::Iconv)
endif()

target_include_directories(npp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)   # #include "core/..."
target_link_libraries(npp_core
    PUBLIC  Qt6::Core
    PUBLIC  npp_pugixml        # pugixml types appear in a few core headers (XmlHelpers)
    PRIVATE npp_uchardet
            npp_lexilla)       # CreateLexer() (plain C++ library, no Qt)
# Scintilla / Lexilla public headers only (message and style constants)
target_include_directories(npp_core PUBLIC ${NPP_SCINTILLA_DIR}/include ${NPP_LEXILLA_DIR}/include)
set_target_properties(npp_core PROPERTIES AUTOMOC OFF)
