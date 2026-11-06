# Notepad++ Qt port — root build (Windows / Linux / macOS)
#
# The legacy Win32 build lives in PowerEditor/ (visual.net, gcc, src/CMakeLists.txt)
# and is not part of this build. See docs/qt-cross-platform-port-plan.md.

cmake_minimum_required(VERSION 3.24)

project(NotepadPlusPlusQt
    VERSION 0.2.0
    DESCRIPTION "Notepad++ Qt cross-platform port"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Deterministic output location: <build>/bin/notepadpp-qt[.exe] (+ bin/data/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
    # Release keeps a PDB next to the executable: crash dumps of packaged builds are readable
    add_compile_options($<$<CONFIG:Release>:/Zi> $<$<CONFIG:Release>:/FS>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

set(NPP_DOCKING_BACKEND "ADS" CACHE STRING "Docking backend implementation: ADS or QT")
set_property(CACHE NPP_DOCKING_BACKEND PROPERTY STRINGS ADS QT)
if(NOT NPP_DOCKING_BACKEND MATCHES "^(ADS|QT)$")
    message(FATAL_ERROR "NPP_DOCKING_BACKEND must be ADS or QT (got '${NPP_DOCKING_BACKEND}')")
endif()
message(STATUS "Docking backend: ${NPP_DOCKING_BACKEND}")

# Plain enable_testing() instead of include(CTest): the latter adds ~28 CDash
# dashboard targets (Nightly*, Continuous*, ...) that clutter IDE run configurations.
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Core5Compat)
qt_standard_project_setup()

# Paths to the existing (upstream) source tree reused by the port
set(NPP_ROOT_DIR       ${CMAKE_CURRENT_SOURCE_DIR})
set(NPP_SCINTILLA_DIR  ${NPP_ROOT_DIR}/scintilla)
set(NPP_LEXILLA_DIR    ${NPP_ROOT_DIR}/lexilla)
set(NPP_BOOSTREGEX_DIR ${NPP_ROOT_DIR}/boostregex)
set(NPP_LEGACY_SRC_DIR ${NPP_ROOT_DIR}/PowerEditor/src)

add_subdirectory(app)
