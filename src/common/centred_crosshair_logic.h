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
#pragma once

#include <cstddef>
#include <cstdint>

namespace centred_crosshair
{
    enum class Patch : uint8_t
    {
        Jump,   // je rel8 -> jmp rel8: the opcode alone
        Load,   // movzx r32, byte ptr [r64 + 0x1C] (REX.B) -> mov r32, 1: five bytes
    };

    // One fill. The pattern is the stock bytes with the patched instruction wildcarded, so a module the mod already
    // patched still matches; the stock or patched instruction itself is checked at `patchOffset`. A fill that hands
    // the value to a setter names the call (`callOffset`, an E8) and what the setter must be.
    struct Site
    {
        const char* pattern;
        size_t patchOffset;
        Patch patch;
        size_t callOffset = 0;
        const char* callee = nullptr;
    };

    // The fill's caller: `lea rax, [rip + array]` and `imul reg, user, stride`.
    struct ProfileArray
    {
        const char* pattern;
        size_t displacementOffset;
        size_t nextInstructionOffset;
        size_t strideOffset;
    };

    struct Title
    {
        const char* name;
        const Site* sites;
        size_t siteCount;
        ProfileArray array;
        uint32_t field;   // the setting's offset in the profile
    };

    inline constexpr uint32_t kUsers = 4;
    inline constexpr uint32_t kProfileInElement = 8;
    inline constexpr int32_t kCentred = 1;
    inline constexpr size_t kMostPatchBytes = 5;
    inline constexpr size_t kCalleeBytes = 64;

    // halo3.dll +0x2DA29F: the compare, a store between, then the jump.
    inline constexpr Site kHalo3Sites[]{
        {"41 80 7E 1C 01 F3 0F 11 8D ?? ?? 00 00 ?? 05 45 33 DB EB 06 41 BB 01 00 00 00 33 D2 48 8B CB E8 ?? ?? ?? ?? "
         "80 BD ?? ?? 00 00 00 41 B7 01 44 89 5B 68",
         13, Patch::Jump},
    };
    // halo3odst.dll +0x305FF9.
    inline constexpr Site kOdstSites[]{
        {"80 7E 1C 01 ?? 05 45 33 DB EB 06 41 BB 01 00 00 00 33 D2 48 8B CB E8 ?? ?? ?? ?? 80 BD ?? ?? 00 00 00 41 B7 01 "
         "44 89 5B 5C",
         4, Patch::Jump},
    };
    // haloreach.dll +0x2C9664 (the full fill) and +0x2EE03F (the fill of a changed setting).
    inline constexpr Site kReachSites[]{
        {"80 7E 1C 01 ?? 0E E8 ?? ?? ?? ?? 83 A7 80 00 00 00 00 EB 0F E8 ?? ?? ?? ?? C7 87 80 00 00 00 01 00 00 00", 4,
         Patch::Jump},
        {"44 89 67 48 ?? ?? ?? ?? ?? 83 FB 02 73 17 3B 9F 80 00 00 00 48 8B CF 0F 95 C2 E8 ?? ?? ?? ?? 89 9F 80 00 00 00",
         4, Patch::Load},
    };
    // halo4.dll +0x3CC3EF (the full fill) and +0x3FB146 (the fill of a changed setting, through the setter at
    // +0x3C9C30).
    inline constexpr Site kHalo4Sites[]{
        {"80 7E 1C 01 ?? 04 33 DB EB 05 BB 01 00 00 00 48 8B 7C 24 30 33 D2 48 8B CF E8 ?? ?? ?? ?? 80 7C 24 21 00 89 9F "
         "90 00 00 00",
         4, Patch::Jump},
        {"45 8A C5 ?? ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 8A C5 48 8B CF 84 C0 0F 84", 3, Patch::Load,
         11,
         "83 FA 02 7D ?? 48 89 5C 24 08 57 48 83 EC 20 8B FA 48 8B D9 33 D2 45 84 C0 74 ?? 3B B9 90 00 00 00 74 02 B2 01 "
         "E8 ?? ?? ?? ?? 89 BB 90 00 00 00"},
    };

    inline constexpr Title kHalo3{"Halo 3", kHalo3Sites, 1,
                                  {"4C 63 CA 48 8D 05 ?? ?? ?? ?? 49 69 F9 ?? ?? 00 00 48 8B D9 48 03 F8 48 8B CF", 6, 10, 13},
                                  0x68};
    inline constexpr Title kOdst{"ODST", kOdstSites, 1,
                                 {"4C 63 CA 48 8D 05 ?? ?? ?? ?? 49 69 F9 ?? ?? 00 00 48 8B D9 48 03 F8 48 8B CF", 6, 10, 13},
                                 0x5C};
    inline constexpr Title kReach{"Reach", kReachSites, 2,
                                  {"4C 63 CA 48 8D 05 ?? ?? ?? ?? 48 8B D9 49 69 F1 ?? ?? 00 00 48 03 F0 48 8B CE", 6, 10, 16},
                                  0x80};
    inline constexpr Title kHalo4{"Halo 4", kHalo4Sites, 2,
                                  {"4C 63 E2 48 8D 05 ?? ?? ?? ?? 48 8B F9 49 69 F4 ?? ?? 00 00 48 03 F0 48 8B CE", 6, 10, 16},
                                  0x90};

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

    // The target of the E8 at `call`, which sits at `callAddress`.
    inline uintptr_t CallTarget(const uint8_t* call, uintptr_t callAddress) noexcept
    {
        if (!call || call[0] != 0xE8)
            return 0;
        uint32_t rel = 0;
        for (int i = 4; i >= 1; --i)
            rel = rel << 8 | call[i];
        return callAddress + 5 + static_cast<intptr_t>(static_cast<int32_t>(rel));
    }

    // `match`: the bytes at the array pattern's match, at `matchAddress`. The array's address and stride; the stride
    // must hold the profile's field.
    inline bool DecodeArray(const ProfileArray& layout, const uint8_t* match, uintptr_t matchAddress, uint32_t field,
                            uintptr_t& array, uint32_t& stride) noexcept
    {
        if (!match)
            return false;
        uint32_t displacement = 0;
        for (int i = 3; i >= 0; --i)
            displacement = displacement << 8 | match[layout.displacementOffset + i];
        stride = 0;
        for (int i = 3; i >= 0; --i)
            stride = stride << 8 | match[layout.strideOffset + i];
        array = matchAddress + layout.nextInstructionOffset + static_cast<intptr_t>(static_cast<int32_t>(displacement));
        return stride >= kProfileInElement + field + sizeof(int32_t) && stride < 0x10000;
    }

    // Where user `user`'s setting lives.
    inline uintptr_t FieldAddress(uintptr_t array, uint32_t stride, uint32_t user, uint32_t field) noexcept
    {
        return array + static_cast<uintptr_t>(user) * stride + kProfileInElement + field;
    }
}
