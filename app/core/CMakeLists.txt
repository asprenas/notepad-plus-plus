# npp-core: platform independent core (C++20 + QtCore only).
# Rule (docs/phase-1-core-extraction.md): no QtGui/QtWidgets, no <windows.h> outside platform/win.

add_library(npp_core STATIC
    util/StringUtil.h
    util/StringUtil.cpp
    util/PathUtil.h
    util/PathUtil.cpp
)

target_include_directories(npp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)   # #include "core/..."
target_link_libraries(npp_core PUBLIC Qt6::Core)
set_target_properties(npp_core PROPERTIES AUTOMOC OFF)
