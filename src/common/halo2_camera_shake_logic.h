#pragma once
#include <cstddef>
#include <cstdint>

// Halo 2's camera shake. The observer's last transform (kHalo2ObserverFinalTransformRva, 0x6F0250) starts from an
// identity matrix, lets two engine routines multiply their shake into it, then applies it to the observer's
// position, forward and up before the collision push and the write-back -- the shape of Halo 3's
// observer_apply_camera_effect, which the mod bypasses whole. Here only the two contributions are undone:
//   0x818010 (user, matrix): in first person, the first-person weapon animation's camera node (the kick a weapon's
//            fire animation gives the view);
//   0x6D64C0 (user, matrix): the screen shake from damage, explosions and weapon effects, outside a pause.
// Each has that one caller (0x6F02B1, 0x6F02DD). The routines still run, so whatever they read or draw (random
// seeds included) is unchanged; the matrix they were handed is put back as it was, and the view keeps still.
// Retail halo2.dll (Steam and Microsoft Store, the same bytes), each pattern unique in the image.
namespace halo2_camera_shake
{
inline constexpr uint32_t kAnimationCameraRva = 0x00818010;
inline constexpr char kAnimationCameraPattern[] =
    "48 89 5C 24 08 57 48 83 EC 60 48 63 C1 48 8B FA 48 69 D8 FC 20 00 00 48 03 1D ?? ?? ?? ?? 83 BB C4 20 00 00 FF";
inline constexpr uint32_t kScreenShakeRva = 0x006D64C0;
inline constexpr char kScreenShakePattern[] =
    "48 8B C4 55 41 56 41 57 48 8D 68 A1 48 81 EC D0 00 00 00 4C 63 F9 4C 8B F2 41 83 FF FF 0F 84 ?? ?? ?? ?? "
    "48 89 58 08 48 8B 1D ?? ?? ?? ?? 0F 29 70 D8";
// The observer transform's two calls, with the camera-mode and pause checks between them.
inline constexpr uint32_t kCallSitesRva = 0x006F02AA;
inline constexpr char kCallSitesPattern[] =
    "48 8D 54 24 60 8B CB E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 75 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 8B CB E8 ?? ?? ?? ?? "
    "A9 FD FF FF FF 75 ?? 48 8D 54 24 60 8B CB E8";
inline constexpr uint32_t kAnimationCameraCallOffset = 7;   // 0x6F02B1
inline constexpr uint32_t kScreenShakeCallOffset = 0x33;    // 0x6F02DD
// The matrix both multiply into: real_matrix4x3 (scale, forward, left, up, position), 13 floats.
inline constexpr size_t kMatrixBytes = 0x34;

// The byte a pattern names at `offset` (its index in whole bytes), or -1 for a wildcard or a pattern too short.
constexpr int PatternByte(const char* pattern, uint32_t offset) noexcept
{
    uint32_t index = 0;
    for (const char* c = pattern; *c;)
    {
        while (*c == ' ') ++c;
        if (!*c) break;
        if (index == offset)
        {
            if (c[0] == '?') return -1;
            auto nibble = [](char h) { return h >= 'A' ? h - 'A' + 10 : h - '0'; };
            return nibble(c[0]) * 16 + nibble(c[1]);
        }
        ++index;
        c += 2;
    }
    return -1;
}
}
