#pragma once
#include "haloce_contracts.generated.h"
// THE VEHICLE CAMERAS' CE BINDINGS (haloce_vehicle_cameras.cpp, haloce_controls.cpp's seat facts). Each is a function
// or a field the Halo 1 engine names (director.c, following_camera.c, objects.c, unit_definitions.h, tag_files),
// matched in the pinned retail halo1.dll; the decomp's own offsets are never used. Verified at admission like every CE
// contract (unique entry signatures with unwind records, exact body witnesses, RIP-relative operands): one mismatch
// keeps the whole feature stock.
// ADDR-HYGIENE: guarded -- each RVA is a pinned-module contract, verified against the loaded image before use.
namespace halo_ce::vehicle_camera_contract {
inline constexpr std::array<contract::Entry,3> entries={{
    // following_camera_update(camera, controls, result): the director's third-person camera proc.
    {"vehicle_camera_following",0xC52CA4,"48 8B C4 48 89 58 10 48 89 70 18 55 57 41 56 48 8D 68 A1 48 81 EC A0 00 00 00 0F 29 70 D8 48 8B F2 0F 29 78 C8 48 8D 55 D7 44 0F 29 40 B8 48 8B F9 44 0F 29 48 A8 49 8B D8 0F B7 0E",true},
    // object_get_marker_by_name(object, name, markers, count), with MCC's render interpolation of node matrices.
    {"vehicle_camera_marker",0xB37178,"40 53 55 56 57 41 54 41 56 41 57 48 81 EC 60 0D 00 00 48 8B 05 7F DE 03",true},
    // The cached tag getter (a datum's definition data) the seat facts and the seat's camera track are read through.
    {"vehicle_camera_tag",0xA9B648,"48 8B 05 61 99 19 01 48 0F BF D1 48 C1 E2 05 48 63 4C 02 14 33 C0 85 C9 74 11 48 8B C1 48 2B 05 A4 7D 40 02 48 03 05 9D 17 30 02 C3",false},
}};
inline constexpr std::array<contract::Witness,20> witnesses={{
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
    // unit_get_camera_info: a seated unit's camera is its vehicle definition's seat (stride 0x11C) at +0x84, stored at
    // +8 of the info it answers.
    {0xB04F8B,"48 69 C0 1C 01 00 00 89 33 48 05 84 00 00 00 48 03 C1 48 8B 0D ?? ?? ?? ?? 48 89 43 08"},
    // camera_track_splut: the unit camera's tracks block (+0x4C count, +0x50 address, 0x1C an element), its first...
    {0xC529DD,"8B 41 4C 45 33 C9"},
    {0xC529FB,"48 63 51 50 83 E8 01 41 8B C9 41 0F 49 C1 85 D2 74 09 48 8B CA 49 2B CA 49 03 CB 48 98 48 6B D0 1C 48 03 D1"},
    // ...whose track reference's datum (+0xC) of none falls back to the game globals' default (+0x108)...
    {0xC52A24,"83 CE FF 48 85 D2 74 07 8B 4A 0C 3B CE 75 21 48 8B 05"},
    {0xC52A3A,"48 63 88 08 01 00 00 49 8B C1 85 C9 74 09 48 8B C1 49 2B C2 49 03 C3 8B 48 0C E8"},
    // ...evaluated at (pitch + half pi) / pi over the track's control points (+4 count, +8 address, 0x3C a point).
    {0xC52A6C,"44 8B 40 04 F3 0F 5E 15"},
    {0xC52AB6,"48 63 57 08 4D 8B C1 85 D2 74 09 4C 8B C2 4D 2B C2 4D 03 C3 0F BF FB 8D 47 03 48 63 C8 48 6B E9 3C"},
    // The player's look pitch is held to the seat camera's limits (+0x44 lowest, +0x48 highest) where either is set,
    // each inside the flat game's own +-85.5 deg; the camera is unit_get_camera_info's answer.
    {0xA99D00,"48 8D 4C 24 20 E8"},
    {0xA99D23,"4C 8B 74 24 28"},
    {0xA99F24,"F3 45 0F 10 5E 48 4C 8B 2D C7 2E 30 02 F3 41 0F 10 7E 40 48 8D 14 40 48 63 41 34 48 03 C8 45 0F 2E D9 48 63 74 91 08 7A 14 75 12 F3 41 0F 10 46 44 41 0F 2E C1 7A 06 0F 84 D6 00 00 00 F3 45 0F 10 46 44"},
}};
inline constexpr std::array<contract::Relative,17> relatives={{
    {0xA9AFEB,7,3,0x1C34FB0},
    {0xA9AFFD,7,3,0x2EA3410},
    {0xA9B004,7,3,0x2D9CE10},
    {0xB15033,7,3,0x2EA3410},
    {0xB1503A,7,3,0x2D9CE10},
    {0xB151A7,7,3,0x2D9B960},
    {0xB371ED,5,1,0xBA8B10},
    {0xC529E3,7,3,0x2EA3410},
    {0xC529ED,7,3,0x2D9CE10},
    {0xC52A33,7,3,0x1C55C60},
    {0xC52A54,5,1,0xA9B648},
    {0xC52A59,8,4,0x194F5D8},
    {0xC52A70,8,4,0x194F81C},
    {0xC52B57,5,1,0xC49B98},
    {0xA99D05,5,1,0xB04EE4},
    {0xA99CCE,9,5,0x194FE68},
    {0xA99CDA,9,5,0x194F5CC},
}};
inline constexpr std::array<contract::Pointer,0> pointers{};
inline constexpr uint32_t following=0xC52CA4;
inline constexpr uint32_t marker=0xB37178;
inline constexpr uint32_t tag=0xA9B648;
// The tag cache's address translation (a 32-bit cache pointer, less the first, plus the second) and the tag instance
// array (0x20 an instance: +0x10 its path, +0x14 its data), as the relatives above prove the engine reads them.
inline constexpr uint32_t cacheVirtualBase=0x2EA3410;
inline constexpr uint32_t cacheMappedBase=0x2D9CE10;
inline constexpr uint32_t tagInstances=0x1C34FB0;
// The game globals' data, whose +0x108 holds the default camera track's reference.
inline constexpr uint32_t globals=0x1C55C60;
}
