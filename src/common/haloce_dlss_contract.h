#pragma once
#include "haloce_contracts.generated.h"
// Independent optional constant-reader proof. See verify_ce_dlss_native_receipts.py.
namespace halo_ce::dlss_contract {
inline constexpr std::array<contract::Entry,2> entries={{
    {"anniversary_camera_constant_dispatch",0x2351e0,"48 89 5C 24 20 55 56 57 41 56 41 57 48 8D 6C 24 E0 48 81 EC 20 01 00 00",true},
    {"anniversary_camera_constant_writer",0x235860,"48 83 EC 18 48 8B 82 D8 00 00 00 F3 0F 10 5C 24 40 0F 28 CB 0F 28 D3 F3 0F 5C 4C 24 48 F3 0F 59 5C 24 48 C6 40 30 01",true},
}};
inline constexpr std::array<contract::Witness,3> witnesses={{
    {0x235883,"C6 40 30 01 48 8B 48 08 41 0F 10 01 F3 0F 5E D1 0F 11 81 70 02 00 00"},
    {0x2358f1,"48 8B 82 D8 00 00 00 C6 40 30 01 48 8B 48 08 41 0F 10 00 0F 11 81 40 02 00 00"},
    {0x23573c,"F3 0F 10 85 80 00 00 00 0F 11 4C 24 30 F3 0F 10 4D 78 0F C6 DC DD F3 0F 11 44 24 28 F3 0F 11 4C 24 20 0F 11 54 24 40 0F 11 5C 24 60 FF 90 10 01 00 00"},
}};
inline constexpr std::array<contract::Relative,0> relatives{};
inline constexpr std::array<contract::Pointer,2> pointers={{
    {0x18153c0,0x2351e0},
    {0x1815488,0x235860},
}};
}
