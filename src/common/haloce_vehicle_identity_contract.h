#pragma once
#include "haloce_contracts.generated.h"
// HCEEK render_objects.c / mod2 nodes, matched to the pinned retail consumers.
// See CE-SUBTITLE-VEHICLE-IDENTITY-RESEARCH-2026-09-30.md.
namespace halo_ce::vehicle_identity_contract {
inline constexpr std::array<contract::Entry,3> entries={{
    {"vehicle_cached_tag",0xA9B648,"48 8B 05 61 99 19 01 48 0F BF D1 48 C1 E2 05 48 63 4C 02 14 33 C0 85 C9 74 11 48 8B C1 48 2B 05 A4 7D 40 02 48 03 05 9D 17 30 02 C3",false},
    {"vehicle_definition_model",0xAF5EC8,"40 53 48 83 EC 20 E8 75 57 FA FF 4C 8B C0 8B 48 34 83 F9 FF 74 45 E8 65 57 FA FF 33 C9 48 8B D8",true},
    {"vehicle_model_node_names",0xB2A1AC,"48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 54 41 55 41 57 48 83 EC 20 4D 8B E0 44 8B CA E8 78 14 F7 FF 41 8B C9 48 8B F8 E8 6D 14 F7 FF",true},
}};
inline constexpr std::array<contract::Witness,2> witnesses={{
    {0xB2A1EB,"44 39 AF B8 00 00 00 0F 8E A1 00 00 00 48 63 8F BC 00 00 00 49 8B C5 85 C9 74 11 48 8B C1 48 2B 05 00 92 37 02 48 03 05 F9 2B 27 02 49 0F BF F2 41 0F B7 CD 48 69 DE 9C 00 00 00 48 03 D8"},
    {0xB2A255,"48 8B C3 48 2B D3 44 0F B6 00 44 0F B6 0C 10 45 2B C1 75 08 48 FF C0 45 85 C9 75 EA"},
}};
inline constexpr std::array<contract::Relative,2> relatives={{{0xB2A209,7,3,0x2EA3410},{0xB2A210,7,3,0x2D9CE10}}};
inline constexpr std::array<contract::Pointer,0> pointers{};
}
