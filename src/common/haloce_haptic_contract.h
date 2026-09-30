#pragma once
#include "haloce_contracts.generated.h"
// Own HCEEK player_vibrate.c and firing damage proof; see continuation evidence.
namespace halo_ce::haptic_contract {
inline constexpr uint32_t enqueue=0xB9B518;
inline constexpr uint32_t update=0xB9B400;
inline constexpr uint32_t curve=0xC72528;
inline constexpr uint32_t trigger=0xB78DE8;
inline constexpr uint32_t firingDamageReturn=0xB79876,enqueueReturn=0xBAB8A5;
inline constexpr std::array<contract::Entry,4> entries={{
    {"haptic_enqueue",enqueue,"48 0F BF C1 41 B9 01 00 00 00 4C 69 C0 08 02 00 00 4C 03 05 A8 53 20 02 49 8B C8 F3 41 0F 10 88",false},
    {"haptic_update",update,"48 89 5C 24 18 55 56 57 41 54 41 56 48 83 EC 20 83 64 24 50 00 33 F6 45 33 F6 BD B8 00 00 00 49",true},
    {"haptic_curve",curve,"40 53 48 83 EC 50 0F 29 74 24 40 0F 29 7C 24 30 F3 0F 10 3D B8 E9 B8 00 44 0F 29 44 24 20 45 0F",true},
    {"haptic_trigger",trigger,"48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 40 01 00 00 0F 29 70 B8 0F 29 78",true},
}};
inline constexpr std::array<contract::Witness,3> witnesses={{
    {0xB9B44B,"F3 0F 58 05 FD 3C DB 00"},
    {0xC72572,"66 83 F9 05"},
    {0x194F150,"89 88 08 3D"},
}};
inline constexpr std::array<contract::Relative,3> relatives={{{0xB79871,5,1,contract::contact::contact_damage},
    {0xBAB8A0,5,1,enqueue},{0xB9B726,5,1,curve}}};
inline constexpr std::array<contract::Pointer,0> pointers{};
}
