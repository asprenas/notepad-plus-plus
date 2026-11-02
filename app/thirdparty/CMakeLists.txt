# Third-party / reused upstream components built as static libraries.

# ---------------------------------------------------------------------------
# Scintilla (Qt platform layer) + Notepad++ Boost.Regex search engine
# ---------------------------------------------------------------------------
file(GLOB NPP_SCINTILLA_CORE_SOURCES CONFIGURE_DEPENDS ${NPP_SCINTILLA_DIR}/src/*.cxx)

add_library(npp_scintilla STATIC
    ${NPP_SCINTILLA_CORE_SOURCES}
    ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase/PlatQt.cpp
    ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaQt.cpp
    ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaEditBase.cpp
    ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase/PlatQt.h
    ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaQt.h
    ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaEditBase.h
    ${NPP_BOOSTREGEX_DIR}/BoostRegExSearch.cxx
    ${NPP_BOOSTREGEX_DIR}/UTF8DocumentIterator.cxx
)
target_include_directories(npp_scintilla
    PUBLIC
        ${NPP_SCINTILLA_DIR}/include
        ${NPP_SCINTILLA_DIR}/qt/ScintillaEditBase
        ${NPP_SCINTILLA_DIR}/src            # ScintillaEditBase.h includes internal headers
    PRIVATE
        ${NPP_BOOSTREGEX_DIR}
)
target_compile_definitions(npp_scintilla
    PUBLIC
        SCINTILLA_QT=1
        EXPORT_IMPORT_API=          # static library: no dllimport/dllexport
    PRIVATE
        SCI_OWNREGEX                # use boostregex instead of built-in RESearch
        BOOST_REGEX_STANDALONE
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:NDEBUG=1>
)
set_target_properties(npp_scintilla PROPERTIES AUTOMOC ON)
target_link_libraries(npp_scintilla PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat)

# ---------------------------------------------------------------------------
# Lexilla (statically linked, CreateLexer() called directly)
# ---------------------------------------------------------------------------
file(GLOB NPP_LEXILLA_SOURCES CONFIGURE_DEPENDS
    ${NPP_LEXILLA_DIR}/lexlib/*.cxx
    ${NPP_LEXILLA_DIR}/lexers/*.cxx
)
add_library(npp_lexilla STATIC
    ${NPP_LEXILLA_DIR}/src/Lexilla.cxx
    ${NPP_LEXILLA_SOURCES}
)
target_include_directories(npp_lexilla
    PUBLIC
        ${NPP_LEXILLA_DIR}/include
    PRIVATE
        ${NPP_LEXILLA_DIR}/lexlib
        ${NPP_SCINTILLA_DIR}/include
)

# ---------------------------------------------------------------------------
# pugixml (UTF-8 / char mode, as in upstream)
# ---------------------------------------------------------------------------
add_library(npp_pugixml STATIC ${NPP_LEGACY_SRC_DIR}/pugixml/pugixml.cpp)
target_include_directories(npp_pugixml PUBLIC ${NPP_LEGACY_SRC_DIR}/pugixml)

# ---------------------------------------------------------------------------
# uchardet (charset detection, as bundled with upstream)
# ---------------------------------------------------------------------------
file(GLOB NPP_UCHARDET_SOURCES CONFIGURE_DEPENDS ${NPP_LEGACY_SRC_DIR}/uchardet/*.cpp)
add_library(npp_uchardet STATIC ${NPP_UCHARDET_SOURCES})
target_include_directories(npp_uchardet PUBLIC ${NPP_LEGACY_SRC_DIR}/uchardet)
if(NOT MSVC)
    target_compile_options(npp_uchardet PRIVATE -w) # third-party code, keep build output clean
endif()

# ---------------------------------------------------------------------------
# Qt Advanced Docking System (git submodule, pinned tag)
# ---------------------------------------------------------------------------
if(NPP_DOCKING_BACKEND STREQUAL "ADS")
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/qtads/CMakeLists.txt)
        message(FATAL_ERROR "ADS submodule missing. Run: git submodule update --init --recursive")
    endif()
    set(BUILD_STATIC   ON  CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(qtads EXCLUDE_FROM_ALL)
endif()
