#pragma once
#include <cstdint>
// H2EK 89E5A0 -> 884420 -> 499490 -> 4D3150, verified in pinned retail.
namespace halo2_haptic_contract {
inline constexpr uint32_t trigger=0x8E6180,effect=0x8F5A20,enqueue=0x73B890;
inline constexpr uint32_t update=0x73B9F0,evaluate=0x73B300,curve=0x7774C0;
inline constexpr uint32_t effectReturn=0x8E6B3E,enqueueReturn=0x6D71EF,evaluateReturn=0x73BAB7;
inline constexpr uint32_t instances=0x15E4B30,tagData=0x15E4B38;
inline constexpr uint32_t blockBase=0xE80AB0,alternateBlockBase=0xE80AC0;
inline constexpr uint32_t vibration=0x16503F0,accumulator=0x16503F8;
inline constexpr uint32_t interval=0xDFCEA0,durationScale=0xDFCE9C,amplitudeScale=0xDFCE98;
struct Binding {uint32_t rva;const char* pattern;};
inline constexpr Binding entries[]{
 {trigger,"48 89 5C 24 18 66 89 54 24 10 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F0 E3 FF"},
 {effect,"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC F0 00 00 00 48 8B 05 5A 19 FC 00 33 DB"},
 {enqueue,"48 89 5C 24 08 0F 28 E3 45 8B D8 8B DA 83 FA FF 0F 84 14 01 00 00 41 83 F8 FF 0F 84 0A 01 00 00"},
 {update,"48 83 EC 78 F3 0F 10 15 A4 14 6C 00 0F 29 74 24 60 0F 28 F0 0F 29 7C 24 50 0F 57 FF 0F 2F D7 76"},
 {evaluate,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 56 41 57 48 83 EC 70 F3 0F 10 82 80 00 00 00"},
 {curve,"48 8B C4 53 55 57 41 56 48 81 EC C8 00 00 00 0F 29 70 C8 4C 8B F1 0F 29 78 B8 0F 57 FF 44 0F 29"}
};
inline constexpr Binding witnesses[]{
 {0x73B398,"48 03 C9 48 63 54 C8 08 48 03 15 91 97 EA 00 45 85 C0 0F 88 9B 00 00 00 44 3B 42 6C 0F 8D 91 00 00 00 48 63 42 70 33 D2"},
 {0x73B3B0,"44 3B 42 6C 0F 8D 91 00 00 00 48 63 42 70 33 D2 83 F8 FF 74 1D 85 C0 79 0F 8B D0 0F BA F2 1F 48 03 15 EA 56 74 00 EB 0A"},
 {0x73B3E2,"49 6B F8 4C 33 DB 48 83 C7 24 48 03 FA 90 F3 0F 10 05 A4 1A 6C 00 F3 0F 59 07 0F 2F C7 76 3D F3 44 0F 10 46 04 0F 28 D7"},
 {0x73B441,"48 83 C7 0C 48 83 FB 02 7C A5 48 83 C5 04 48 83 C6 0C 49 83 EE 01 0F 85 13 FF FF FF 48 8B 05 8C 4F F1 00 F3 0F 10 88 28"},
 {0x6D71E2,"0F 28 DF 44 8B C3 8B CF E8 A1 46 06 00 8B 4D 48 8B D3 E8 B7 73 08 00 4C 8B 7D 50 4C 8B A4 24 08 01 00 00 4D 8D 46 74 F3"},
 {0x73B420,"48 8D 4F 04 41 0F 28 D1 E8 93 C0 03 00 F3 41 0F 59 C0 F3 0F 58 44 9C 20 F3 0F 11 44 9C 20 48 FF C3 48 83 C7 0C 48 83 FB"},
 {0x73BA19,"F3 0F 58 C8 0F 2F D1 F3 0F 11 0D D0 49 F1 00 0F 83 87 01 00 00 0F 28 C1 F3 0F 5E C2 F3 0F 2C C0 66 0F 6E F0 0F 5B F6 F3"},
 {0x73BA2E,"0F 28 C1 F3 0F 5E C2 F3 0F 2C C0 66 0F 6E F0 0F 5B F6 F3 0F 59 F2 F3 0F 5C CE F3 0F 11 0D A8 49 F1 00 48 89 9C 24 80 00"}
};
struct Edge {uint32_t call,target;};
inline constexpr Edge edges[]{
 {0x8E6B39,effect},{0x8F5B28,0x6D6F40},{0x6D71EA,enqueue},
 {0x73BAB2,evaluate},{0x73B428,curve}
};
struct Reference {uint32_t instruction,length,displacement,target;};
inline constexpr Reference references[]{
 {0x73B391,7,3,instances},{0x73B3A0,7,3,tagData},
 {0x73B3CF,7,3,alternateBlockBase},{0x73B3DB,7,3,blockBase},
 {0x73B3F0,8,4,durationScale},{0x73B4A8,8,4,amplitudeScale},
 {0x73B9F4,8,4,interval},{0x73BA11,8,4,accumulator},
 {0x73BAA0,7,3,vibration},{0x73B8BA,7,3,vibration}
};
}
