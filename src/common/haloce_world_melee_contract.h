#pragma once
#include "haloce_contracts.generated.h"
// Official HCEEK 8D4A90 / 811D90, matched to pinned retail; see
// PHYSICAL-MELEE-WORLD-2026-09-30.md. Cold verification only.
namespace halo_ce::world_melee_contract {
inline constexpr uint32_t fan=0xB0BFAC,damage=0xA9C174,bsp=0x1B860A4;
inline constexpr std::array<contract::Entry,2> entries={{
    {"world_melee_fan",fan,"48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 B8 48 81 EC 48 01 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 50 88 44 0F 29 98 78 FF FF FF 44 0F 29 A0 68 FF FF FF 48 8B 05 25 90 06 01",true},
    {"world_melee_breakable",damage,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 83 C8 FF 45 8B F0",true},
}};
inline constexpr std::array<contract::Witness,2> witnesses={{
    {0xB0C2C2,"41 83 3E FF 75 1C F6 45 BC 08 0F B7 45 A4 66 41 89 07 74 0E 0F B6 45 BD 66 41 89 45 00 8B 45 B4 89 07"},
    {0xA9C1AB,"0F B7 4A 50 66 3B C8 0F 84 1B 01 00 00 48 0F BF 2D E4 9E 0E 01"},
}};
inline constexpr std::array<contract::Relative,3> relatives={{{0xB0C462,5,1,fan},
    {0xB0C669,5,1,damage},{0xA9C1B8,8,4,bsp}}};
inline constexpr std::array<contract::Pointer,0> pointers{};
}
