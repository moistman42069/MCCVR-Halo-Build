// ALWAYS THE CENTRED CROSSHAIR (MCC's crosshair location; game.cpp centred_crosshair.inl). MCC lets each player put
// the crosshair Centered or Lowered. Lowered pitches the first-person camera a steady angle above the aim and draws
// the crosshair below the screen's centre; in a headset, where the head owns the pitch, it only puts the shots off
// the crosshair and the zoomed crosshair off the aim. Under the mod every title uses Centered, whatever the profile
// says; the profile itself is left as the player set it.
//
// Halo 3 and ODST (H3EK halo3_tag_test.exe, matched in the retail modules 2026-11-10). The game keeps its own copy of
// each local player's profile; the setting is one int in it, read by the camera's pitch offset, the first-person
// weapon's offset and the crosshair's (H3EK: 0 Lowered, 1 Centered, and 1 with no profile). The game fills that copy
// from MCC's profile (byte +0x1C) whenever MCC changes it:
//     cmp  byte ptr [rX + 0x1C], 1      ; MCC's crosshair location
//     ...
//     je   +5                           ; <- the patch: always take the Centered arm
//     xor  r11d, r11d
//     jmp  +6
//     mov  r11d, 1
//     ...
//     mov  dword ptr [rbx + field], r11d   ; Halo 3 +0x68, ODST +0x5C
// so the copy is Centered from every later fill. A profile filled before the patch is set directly: the copies are
// four array elements (the per-user tick loops `cmp ebx, 4`), each holding its profile at +8 (`lea rcx, [r14+8]`
// before the fill), the array found from the fill's caller (`lea rax, [rip+array]; imul rdi, r9, stride`).
#pragma once

#include <cstddef>
#include <cstdint>

namespace centred_crosshair
{
    // From the conditional jump to the store of the setting into the game's profile copy. The last byte is the field;
    // the first is the jump, which the patch has made unconditional where the mod already ran (DecodeFill checks it).
    inline constexpr char kFillPattern[] =
        "?? 05 45 33 DB EB 06 41 BB 01 00 00 00 33 D2 48 8B CB E8 ?? ?? ?? ?? 80 BD ?? ?? 00 00 00 41 B7 01 44 89 5B ??";
    inline constexpr size_t kFillPatternBytes = 37;
    inline constexpr size_t kFillFieldOffset = kFillPatternBytes - 1;
    // How far before the jump the compare with MCC's byte may sit (Halo 3 13 bytes, ODST 4).
    inline constexpr size_t kFillCompareSearch = 16;
    // The fill's caller: the user's element of the array, and the array's stride.
    inline constexpr char kArrayPattern[] =
        "4C 63 CA 48 8D 05 ?? ?? ?? ?? 49 69 F9 ?? ?? 00 00 48 8B D9 48 03 F8 48 8B CF";
    inline constexpr size_t kArrayDisplacementOffset = 6;
    inline constexpr size_t kArrayNextInstructionOffset = 10;
    inline constexpr size_t kArrayStrideOffset = 13;
    inline constexpr uint32_t kUsers = 4;
    inline constexpr uint32_t kProfileInElement = 8;
    inline constexpr int32_t kCentred = 1;

    // `jump`: the bytes at the match; `before`: the kFillCompareSearch bytes before it. True when the jump reads MCC's
    // crosshair byte (`cmp byte ptr [reg + 0x1C], 1`, REX.B or not, disp8) and is still the stock `je +5` (or already
    // the patch's `jmp +5`); `field` is the profile copy's offset the store writes.
    inline bool DecodeFill(const uint8_t* jump, const uint8_t* before, bool& patched, uint8_t& field) noexcept
    {
        if (!jump || !before || jump[1] != 0x05 || (jump[0] != 0x74 && jump[0] != 0xEB))
            return false;
        bool compare = false;
        for (size_t i = 0; i + 4 <= kFillCompareSearch && !compare; ++i)
        {
            const uint8_t* b = before + i;
            const uint8_t* cmp = b[0] == 0x41 ? b + 1 : b;
            // 80 /7 ib with mod 01 (disp8): modrm 0x78..0x7F, not rsp (0x7C, which needs a SIB byte).
            compare = cmp + 4 <= before + kFillCompareSearch && cmp[0] == 0x80 && (cmp[1] & 0xF8) == 0x78 &&
                cmp[1] != 0x7C && cmp[2] == 0x1C && cmp[3] == 0x01;
        }
        if (!compare)
            return false;
        patched = jump[0] == 0xEB;
        field = jump[kFillFieldOffset];
        return true;
    }

    // `match`: the bytes at the array pattern's match, at `matchAddress`. The array's address and stride; the stride
    // must hold the profile's field.
    inline bool DecodeArray(const uint8_t* match, uintptr_t matchAddress, uint8_t field, uintptr_t& array,
                            uint32_t& stride) noexcept
    {
        if (!match)
            return false;
        int32_t displacement = 0;
        for (int i = 3; i >= 0; --i)
            displacement = static_cast<int32_t>(static_cast<uint32_t>(displacement) << 8 |
                                                match[kArrayDisplacementOffset + i]);
        stride = 0;
        for (int i = 3; i >= 0; --i)
            stride = stride << 8 | match[kArrayStrideOffset + i];
        array = matchAddress + kArrayNextInstructionOffset + static_cast<intptr_t>(displacement);
        return stride >= kProfileInElement + field + sizeof(int32_t) && stride < 0x10000;
    }

    // Where user `user`'s setting lives.
    inline uintptr_t FieldAddress(uintptr_t array, uint32_t stride, uint32_t user, uint8_t field) noexcept
    {
        return array + static_cast<uintptr_t>(user) * stride + kProfileInElement + field;
    }
}
