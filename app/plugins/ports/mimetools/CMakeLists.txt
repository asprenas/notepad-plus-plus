# MIME Tools (github.com/npp-plugins/mimetools, 3.2, commit 7a54297) ported to the Qt plugin API
# (phase 7, P7-07). See PORTING.md for the changes; the encoders / decoders are unchanged.
npp_add_plugin(mimeTools SOURCES
    src/PluginInterface.h
    src/mimeTools.h
    src/mimeTools.cpp
    src/b64.h
    src/b64.cpp
    src/qp.h
    src/qp.cpp
    src/url.h
    src/url.cpp
    src/saml.h
    src/saml.cpp
    src/tinf.h
    src/tinflate.c)
if(MSVC)
    target_compile_definitions(mimeTools PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
