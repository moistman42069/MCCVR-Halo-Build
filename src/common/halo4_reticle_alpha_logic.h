#pragma once

// HALO 4'S RETICLE CAPTURE AND ITS ALPHA PLANE.
//
// The crosshair shown in the headset is the game's own art, drawn into a capture cleared to transparent and
// uploaded with its alpha rebuilt as the larger of the captured alpha and the brightest colour channel
// (vr_blit_shader.h). That reads the capture's alpha as coverage: how much of the scene the art hides.
//
// Halo 4 does not draw its HUD that way. Measured on the draws of a capture, on foot and in vehicle seats, the
// colour blend is one of two pairs (source factor, destination factor):
//
//   * (one, one): the art is added to the scene and hides nothing;
//   * (one, source alpha): the shader's alpha is how much of the scene shows THROUGH, so a sprite's empty texels
//     carry alpha 1.
//
// and the alpha blend beside them is (one, one), (one, source alpha) or (dest alpha, zero). None of these puts
// coverage in the target's alpha. On a flat screen that plane belongs to a scene target nobody reads. In the
// capture it is read as coverage, and a show-through sprite's empty texels come out opaque black: the square
// behind the centre ring of the Mantis's crosshair (captured alpha 255 where the brightest colour was 41).
//
// So a draw into the capture writes the capture's alpha only if its blend is the classic pair whose alpha IS
// coverage (source alpha, one minus source alpha). Every other draw is made without its alpha write, and the
// upload's rule then gives its art the coverage of its own brightness, which is what an on-foot crosshair has
// always had (its capture's alpha is zero).
//
// Pure logic, no D3D types: the caller reads the bound blend state. Unit-tested in tests/core_tests.cpp.

#include <cstdint>

namespace halo4_reticle_alpha
{
// The values of D3D11_BLEND, D3D11_BLEND_OP and D3D11_COLOR_WRITE_ENABLE this reads.
inline constexpr int kBlendInvSrcAlpha = 6;
inline constexpr int kBlendOpAdd = 1;
inline constexpr uint8_t kWriteAlpha = 8;

// Render target 0 of the blend state bound at the draw.
struct Blend
{
    bool enabled = false;
    int destBlend = 0;
    int blendOp = 0;
    uint8_t writeMask = 0;
};

// The draw lays its colour over the scene and keeps (one minus its alpha) of what was there: the alpha it
// writes is its coverage.
inline bool WritesCoverage(const Blend& b)
{
    return b.enabled && b.blendOp == kBlendOpAdd && b.destBlend == kBlendInvSrcAlpha;
}

// Whether this draw into the capture is made without its alpha write.
inline bool DropsAlphaWrite(const Blend& b)
{
    return (b.writeMask & kWriteAlpha) != 0 && !WritesCoverage(b);
}

inline uint8_t WriteMaskWithoutAlpha(uint8_t mask)
{
    return static_cast<uint8_t>(mask & static_cast<uint8_t>(~kWriteAlpha));
}
}   // namespace halo4_reticle_alpha
