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
    PRIVATE npp_uchardet npp_pugixml)
set_target_properties(npp_core PROPERTIES AUTOMOC OFF)
