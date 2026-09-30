# Use the launcher's unchanged OFL font in the VR menu without runtime disk I/O.
set(mccvr_menu_font "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/assets/fonts/Oxanium.ttf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mccvr_menu_font}")
file(READ "${mccvr_menu_font}" mccvr_menu_font_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," mccvr_menu_font_bytes "${mccvr_menu_font_hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" mccvr_menu_font_bytes "${mccvr_menu_font_bytes}")
file(CONFIGURE OUTPUT "${HALOMCCVR_GENERATED_DIR}/menu_font.generated.h" CONTENT
"// Unmodified Oxanium. Copyright 2019 The Oxanium Project Authors. SIL OFL 1.1.
// See assets/fonts/OFL-Oxanium.txt in the distribution.
#pragma once
namespace menu_assets {
inline const unsigned char oxanium[] = {
@mccvr_menu_font_bytes@
};
}
" @ONLY)
unset(mccvr_menu_font_hex)
unset(mccvr_menu_font_bytes)
