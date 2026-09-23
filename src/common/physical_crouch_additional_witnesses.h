#pragma once
#include "runtime_types.h"
// Own-kit camera math matched to pinned retail; offline verifier reproduces these witnesses.
struct CrouchNativeWitness {GameTitle title;uint32_t rva;const char* pattern;};
inline constexpr CrouchNativeWitness kCrouchAdditionalWitnesses[]{
    {GameTitle::Halo3ODST,0x399E43,"41 83 CB FF 44 39 5E 10 0F 85 B0 00 00 00 8B 86 04 01 00 00 41 8D 5B 02 C1 E8 02 84 C3 75 37 44 38 B6 96 00 00 00 75 2E"},
    {GameTitle::Halo3ODST,0x38A53D,"41 8B CE E8 9F 22 00 00 84 C0 75 09 F3 44 0F 10 87 98 03 00 00"},
    {GameTitle::Halo3ODST,0x38A698,"F3 41 0F 5C F8 F3 45 0F 59 84 24 18 03 00 00 F3 41 0F 59 BC 24 10 03 00 00 F3 41 0F 58 F8 F3 0F 59 BF 8C 00 00 00"},
    {GameTitle::Halo3ODST,0x38C813,"80 B9 16 05 00 00 06 75 16 8B 89 1C 05 00 00 BA 01 00 00 00 C1 E9 04 84 CA 0F B6 C0 0F 45 C2"},
    {GameTitle::Halo3ODST,0x38A4CF,"48 0F BF 87 5A 01 00 00 8B 0C 38 83 C1 F9 83 F9 01 77 04 48 8D 77 68"},
    {GameTitle::Halo2,0x8F91FB,"41 83 F8 FF 75 35 0F B6 88 0A 01 00 00 C0 E9 02 F6 C1 01 75 26 38 98 AA 00 00 00 75 1E"},
    {GameTitle::Halo2,0x92724C,"8B CF E8 8D 17 00 00 84 C0 0F 85 B5 00 00 00 F3 41 0F 10 87 0C 03 00 00"},
    {GameTitle::Halo2,0x927313,"F3 0F 5C F0 BE FF FF FF FF F3 41 0F 59 84 24 1C 02 00 00 48 8D 55 80 48 8D 4C 24 70 66 89 75 B4 F3 41 0F 59 B4 24 18 02 00 00 F3 0F 58 F0 F3 41 0F 59 B7 A0 00 00 00"},
    {GameTitle::Halo2,0x928A47,"0F B7 83 90 03 00 00 B9 01 00 00 00 66 C1 E8 0D A8 01 40 0F B6 C6 0F 45 C1 48 8B"},
    {GameTitle::Halo2,0x927210,"48 8B 0D 19 D9 CB 00 4C 8B F8 F3 0F 10 35 02 B6 30 00 0F 57 FF 0F B7 10 48 03 D2 4C 63 64 D1 08 4C 03 25 01 D9 CB 00"},
};
