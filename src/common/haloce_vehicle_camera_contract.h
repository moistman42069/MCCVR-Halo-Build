#pragma once
#include "haloce_contracts.generated.h"
// THE VEHICLE CAMERAS' CE BINDINGS (haloce_vehicle_cameras.cpp, haloce_controls.cpp's seat facts). Each is a function
// or a field the Halo 1 engine names (director.c, following_camera.c, objects.c, unit_definitions.h, tag_files),
// matched in the pinned retail halo1.dll; the decomp's own offsets are never used. Verified at admission like every CE
// contract (unique entry signatures with unwind records, exact body witnesses, RIP-relative operands): one mismatch
// keeps the whole feature stock.
// ADDR-HYGIENE: guarded -- each RVA is a pinned-module contract, verified against the loaded image before use.
namespace halo_ce::vehicle_camera_contract {
inline constexpr std::array<contract::Entry,2> entries={{
    // following_camera_update(camera, controls, result): the director's third-person camera proc.
    {"vehicle_camera_following",0xC52CA4,"48 8B C4 48 89 58 10 48 89 70 18 55 57 41 56 48 8D 68 A1 48 81 EC A0 00 00 00 0F 29 70 D8 48 8B F2 0F 29 78 C8 48 8D 55 D7 44 0F 29 40 B8 48 8B F9 44 0F 29 48 A8 49 8B D8 0F B7 0E",true},
    // object_get_marker_by_name(object, name, markers, count), with MCC's render interpolation of node matrices.
    {"vehicle_camera_marker",0xB37178,"40 53 55 56 57 41 54 41 56 41 57 48 81 EC 60 0D 00 00 48 8B 05 7F DE 03",true},
}};
inline constexpr std::array<contract::Witness,10> witnesses={{
    // The command's focus position (+4, from the camera info), its focus distance (+0x1C) and offset (+0x10/+0x14/+0x18).
    {0xC52CEA,"F2 0F 10 45 E7 48 8D 4D 67 8B 45 EF F2 0F 11 43 04"},
    {0xC52D03,"89 43 0C"},
    {0xC52F64,"F3 0F 11 43 1C"},
    {0xC52F8D,"F3 0F 11 4B 10"},
    {0xC52FA0,"F3 44 0F 11 43 14"},
    {0xC52FAB,"F3 0F 11 43 18"},
    // A tag instance's path: entry +0x10, a cache pointer relocated as its data is.
    {0xA9AFE7,"48 C1 E0 05 48 03 05 BE 9F 19 01 74 79 39 70 10 74 74 48 63 48 10 48 2B 0D 0C 84 40 02 48 03 0D 05 1E 30 02"},
    // A unit definition's seat block (+0x2E8, stride 0x11C) and the seat's flags (+0), as the perspective chooser reads them.
    {0xB15025,"48 63 88 E8 02 00 00 85 C9 74 11 48 8B D1 48 2B 15 D6 E3 38 02 48 03 15 CF 7D 28 02 B9 D0 02 00 00 48 8D 86 04 03 00 00 49 03 C2 41 83 FA FF 48 0F 44 C1 48 0F BF 00 48 69 C8 1C 01 00 00 44 8B 04 11"},
    // The director re-chooses its camera only when the reported seat state differs from its own (+0x5A).
    {0xB151A1,"44 0F B7 4C 24 38 4C 8D 15 B2 67 28 02 33 C9 40 84 FF 75 08 66 46 39 4C 13 5A 74 7D"},
    // An empty marker name answers the root node, counted.
    {0xB372C0,"48 85 F6 74 0A 44 38 26 75 05 B9 01 00 00 00 0F B7 C1"},
}};
inline constexpr std::array<contract::Relative,7> relatives={{
    {0xA9AFEB,7,3,0x1C34FB0},
    {0xA9AFFD,7,3,0x2EA3410},
    {0xA9B004,7,3,0x2D9CE10},
    {0xB15033,7,3,0x2EA3410},
    {0xB1503A,7,3,0x2D9CE10},
    {0xB151A7,7,3,0x2D9B960},
    {0xB371ED,5,1,0xBA8B10},
}};
inline constexpr std::array<contract::Pointer,0> pointers{};
inline constexpr uint32_t following=0xC52CA4;
inline constexpr uint32_t marker=0xB37178;
}
