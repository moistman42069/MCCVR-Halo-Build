// ALWAYS THE CENTRED CROSSHAIR (MCC's crosshair location; game.cpp centred_crosshair.inl). MCC lets each player put
// the crosshair Centered or Lowered. Lowered pitches the first-person camera a steady angle above the aim and draws
// the crosshair below the screen's centre; in a headset, where the head owns the pitch, it only puts the shots off
// the crosshair and the zoomed crosshair off the aim. Under the mod every title uses Centered, whatever the profile
// says; the profile itself is left as the player set it.
//
// Halo 3, ODST, Reach and Halo 4 (H3EK halo3_tag_test.exe, HREK reach_tag_test.exe, H4EK halo4_tag_test.exe; matched
// in the retail modules, Steam and Store alike, 2026-11-10/11). Each game keeps its own copy of each local player's
// profile; the setting is one int in it (the kits' "camera_crosshair_location": 0 Lowered, 1 Centered, and 1 with no
// profile), read by the camera's pitch offset, the first-person weapon's offset and the crosshair's. The game fills
// that copy from MCC's profile (byte +0x1C) whenever MCC changes it, in one of two shapes:
//     cmp  byte ptr [rsi + 0x1C], 1    ; MCC's crosshair location
//     je   <store 1>                   ; <- Jump: always take the Centered arm
//     <store 0> ... <store 1>
// or
//     movzx ebx, byte ptr [r14 + 0x1C] ; <- Load: mov ebx, 1
//     ... mov dword ptr [rdi + field], ebx   (or a setter that stores it)
// Halo 3 and ODST have one fill each, Reach and Halo 4 two. With every fill patched, every later fill is Centered. A
// profile filled before the patch is set directly: the copies are four array elements (Halo 3's per-user tick loops
// `cmp ebx, 4`), each holding its profile at +8 (`lea rcx, [element + 8]` before the fill), the array found from the
// fill's caller (`lea rax, [rip + array]; imul reg, user, stride`).
//
// Halo CE and Halo 2 (no kit names the setting: MCC added it; retail only, 2026-11-11) copy MCC's whole profile
// (0xACC bytes) per user and read byte +0x1C in one place each, a check of whether the crosshair is centred right
// now. The check is made to read Centered:
//     CE:      cmp byte ptr [rax + rcx], 1   ; copy + 0x1C          Halo 2: cmp byte ptr [rdx + rax], 1
//              je  <answer true>             ; <- NearJump: jmp           jne <answer false>  ; <- Fallthrough
// CE's check answers true for Centered (and for Lowered while the unit's +0x2D0 is set); its callers then skip the
// Lowered offsets. Halo 2's answers true for Centered while the unit's +0x210 is clear; its callers then take the
// Lowered offsets out. The copies themselves are left alone.
//
// Where each fill is, in each title's module: centred_crosshair_sites.h. The patterns there wildcard the patched
// instruction, so a module the mod already patched still matches; the instruction itself is checked here.
#pragma once

#include <cstddef>
#include <cstdint>

namespace centred_crosshair
{
    enum class Patch : uint8_t
    {
        Jump,          // je rel8 -> jmp rel8: the opcode alone
        Load,          // movzx r32, byte ptr [r64 + 0x1C] (REX.B) -> mov r32, 1: five bytes
        NearJump,      // je rel32 (0F 84) -> nop; jmp rel32 (90 E9): the same target
        Fallthrough,   // jne rel8 -> two nops
    };

    inline constexpr uint32_t kUsers = 4;
    inline constexpr uint32_t kProfileInElement = 8;
    inline constexpr int32_t kCentred = 1;
    inline constexpr size_t kMostPatchBytes = 5;

    // How many bytes a pattern spans.
    inline constexpr size_t PatternBytes(const char* pattern) noexcept
    {
        size_t bytes = 0;
        for (bool inToken = false; *pattern; ++pattern)
        {
            const bool space = *pattern == ' ';
            if (!space && !inToken)
                ++bytes;
            inToken = !space;
        }
        return bytes;
    }

    // `bytes` (at least PatternBytes(pattern) of them) against a pattern of hex bytes and "??".
    inline bool Matches(const uint8_t* bytes, const char* pattern) noexcept
    {
        if (!bytes || !pattern)
            return false;
        auto nibble = [](char c) -> int {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        for (const char* p = pattern; *p;)
        {
            if (*p == ' ')
            {
                ++p;
                continue;
            }
            if (!p[1])
                return false;
            if (p[0] != '?')
            {
                const int high = nibble(p[0]), low = nibble(p[1]);
                if (high < 0 || low < 0 || *bytes != static_cast<uint8_t>(high << 4 | low))
                    return false;
            }
            ++bytes;
            p += 2;
        }
        return true;
    }

    // The instruction at `at`: the stock one (`patched` false; `centred` is what replaces it) or the patch already in
    // place (`patched` true). `length` is how many bytes the patch writes. False for anything else.
    inline bool CentredBytes(Patch patch, const uint8_t* at, uint8_t (&centred)[kMostPatchBytes], size_t& length,
                             bool& patched) noexcept
    {
        if (!at)
            return false;
        if (patch == Patch::NearJump)
        {
            if (at[0] == 0x90 && at[1] == 0xE9)
                patched = true;
            else if (at[0] == 0x0F && at[1] == 0x84)
                patched = false;
            else
                return false;
            centred[0] = 0x90;
            centred[1] = 0xE9;
            length = 2;
            return true;
        }
        if (patch == Patch::Fallthrough)
        {
            if (at[0] == 0x90 && at[1] == 0x90)
                patched = true;
            else if (at[0] == 0x75)
                patched = false;
            else
                return false;
            centred[0] = centred[1] = 0x90;
            length = 2;
            return true;
        }
        if (patch == Patch::Jump)
        {
            if (at[0] != 0x74 && at[0] != 0xEB)
                return false;
            patched = at[0] == 0xEB;
            centred[0] = 0xEB;
            length = 1;
            return true;
        }
        // mov r32, 1 (B8+r imm32): the patch, whatever the register.
        if (at[0] >= 0xB8 && at[0] <= 0xBF && at[1] == 0x01 && !at[2] && !at[3] && !at[4])
        {
            patched = true;
            for (size_t i = 0; i < kMostPatchBytes; ++i)
                centred[i] = at[i];
            length = kMostPatchBytes;
            return true;
        }
        // 41 0F B6 /r with mod 01 (disp8) and the disp 0x1C; not rsp/r12 as the base (a SIB byte).
        if (at[0] != 0x41 || at[1] != 0x0F || at[2] != 0xB6 || (at[3] & 0xC0) != 0x40 || (at[3] & 7) == 4 ||
            at[4] != 0x1C)
            return false;
        patched = false;
        centred[0] = static_cast<uint8_t>(0xB8 + (at[3] >> 3 & 7));
        centred[1] = 0x01;
        centred[2] = centred[3] = centred[4] = 0x00;
        length = kMostPatchBytes;
        return true;
    }

    // Where user `user`'s setting lives.
    inline uintptr_t FieldAddress(uintptr_t array, uint32_t stride, uint32_t user, uint32_t field) noexcept
    {
        return array + static_cast<uintptr_t>(user) * stride + kProfileInElement + field;
    }
}
