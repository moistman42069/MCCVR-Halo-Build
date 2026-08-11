#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Halo 4 has no CHUD curvature-info record. Official H4EK's sole
// ui\hud_globals tag instead authors this 3x3 screen-transform basis. The
// values are contiguous at tag-file offset 0xFD6 and are bracketed by the
// immutable damage-mesh and high-contrast fields below. Runtime code must
// locate this title-native payload; no address or layout is copied from H3.
struct Halo4HudBasisPoint
{
    float x;
    float y;
};

inline constexpr std::array<Halo4HudBasisPoint, 9>
    kHalo4HudAuthoredBasis{{
        {-1.0f, -1.0f}, {-0.98f, 0.0f}, {-1.0f, 1.0f},
        {0.0f, -0.92f}, {0.0f, 0.0f}, {0.0f, 0.92f},
        {1.0f, -1.0f}, {0.98f, 0.0f}, {1.0f, 1.0f},
    }};

inline constexpr std::array<Halo4HudBasisPoint, 9>
    kHalo4HudFlatBasis{{
        {-1.0f, -1.0f}, {-1.0f, 0.0f}, {-1.0f, 1.0f},
        {0.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f},
        {1.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
    }};

inline constexpr size_t kHalo4HudBasisBytes =
    sizeof(kHalo4HudAuthoredBasis);
inline constexpr ptrdiff_t kHalo4HudDamagePrefixFromBasis = -20;
inline constexpr size_t kHalo4HudSpreadFromBasis = 72;
inline constexpr size_t kHalo4HudFlagsFromBasis = 92;
inline constexpr size_t kHalo4HudContrastFromBasis = 96;
inline constexpr size_t kHalo4HudAnchorSpanFromBasis = 116;
inline constexpr float kHalo4HudVirtualHalfHeight = 360.0f;

inline constexpr std::array<float, 5> kHalo4HudDamagePrefix{
    35.0f, 0.6f, 3.0f, 0.04f, 1.0f};
inline constexpr std::array<float, 5> kHalo4HudContrastTail{
    0.05f, 0.41f, 0.5f, 0.75f, 1.25f};

inline bool Halo4HudAuthoredBasisMatches(const void* address)
{
    return address && std::memcmp(
        address, kHalo4HudAuthoredBasis.data(), kHalo4HudBasisBytes) == 0;
}
inline bool Halo4HudImmutableSurroundMatches(const uint8_t* basis)
{
    if (!basis)
        return false;
    uint32_t flags = 0;
    std::memcpy(
        &flags, basis + kHalo4HudFlagsFromBasis, sizeof(flags));
    return std::memcmp(
               basis + kHalo4HudDamagePrefixFromBasis,
               kHalo4HudDamagePrefix.data(), sizeof(kHalo4HudDamagePrefix)) == 0 &&
        std::memcmp(
               basis + kHalo4HudSpreadFromBasis,
               &kHalo4HudDamagePrefix.back(), sizeof(float)) == 0 &&
        flags == 0 &&
        std::memcmp(
               basis + kHalo4HudContrastFromBasis,
               kHalo4HudContrastTail.data(), sizeof(kHalo4HudContrastTail)) == 0;
}

inline bool Halo4HudBasisFinite(const Halo4HudBasisPoint* basis)
{
    if (!basis)
        return false;
    for (size_t i = 0; i < kHalo4HudAuthoredBasis.size(); ++i)
    {
        if (!std::isfinite(basis[i].x) || !std::isfinite(basis[i].y) ||
            std::fabs(basis[i].x) > 4.0f || std::fabs(basis[i].y) > 4.0f)
            return false;
    }
    return true;
}

// Player-facing semantics match the shared config:
// - horizontal/vertical are the already aspect-corrected hud_size pair;
// - curvature 0 is the identity grid, 0.5 retains H4's authored warp, and 1
//   doubles H4's authored bow rather than borrowing another engine's depth;
// - positive vertical pixels move the complete HUD upward.
inline bool Halo4ComputeHudBasis(
    float horizontal, float vertical, float curvature,
    float verticalOffsetPixels, Halo4HudBasisPoint* out)
{
    if (!out || !std::isfinite(horizontal) || !std::isfinite(vertical) ||
        !std::isfinite(curvature) || !std::isfinite(verticalOffsetPixels) ||
        horizontal < 0.15f || horizontal > 1.0f ||
        vertical < 0.15f || vertical > 1.0f ||
        curvature < 0.0f || curvature > 1.0f ||
        verticalOffsetPixels < -300.0f || verticalOffsetPixels > 300.0f)
        return false;

    const float authoredWeight = 2.0f * curvature;
    const float verticalTranslation =
        -verticalOffsetPixels / kHalo4HudVirtualHalfHeight;
    for (size_t i = 0; i < kHalo4HudAuthoredBasis.size(); ++i)
    {
        const float x = kHalo4HudFlatBasis[i].x +
            (kHalo4HudAuthoredBasis[i].x - kHalo4HudFlatBasis[i].x) *
                authoredWeight;
        const float y = kHalo4HudFlatBasis[i].y +
            (kHalo4HudAuthoredBasis[i].y - kHalo4HudFlatBasis[i].y) *
                authoredWeight;
        out[i].x = x * horizontal;
        out[i].y = y * vertical + verticalTranslation;
    }
    return Halo4HudBasisFinite(out);
}
