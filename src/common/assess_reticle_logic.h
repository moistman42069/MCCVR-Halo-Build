#pragma once

// DEV ASSESSMENT MARKER (honest_reticle_assess; dev builds only consume it).
//
// The assessment marker shows where the game's shot really goes, so the game's lag behind the hand can be
// judged by eye. It is a SECOND marker: the player's own reticle stays exactly where it is, and the
// player aims with it; the marker only shows where that aim's shot lands. A shot does not leave the controller:
// the game fires it from its OWN shot origin E (on foot
// the engine eye, the camera before head look) along its aim a, and it lands on the first surface that ray
// meets. Drawing the game's aim from the controller at a fixed distance would put the mark parallel to the shot
// line, off by the eye-to-hand offset -- above the reticle in Halo 3 on foot, the sniper entirely above it
// when zoomed, below it in the Scorpion.
//
// So the mark is the WORLD POINT P = E + a * r, where r is the first hit along the game's aim (a trace of
// that ray) or a fallback, and it is drawn on the ray from the rendered centre eye H through P, at |P - H|:
// the same eye-anchored geometry the traced reticle uses, so it sits on the surface the shot would hit, at
// the right depth in both eyes. That geometry is shot_point_logic.h's (shared, as code only, with the
// player-facing seat limit reticle); this header adds what is the assessment's alone. Everything here is pure;
// the callers own the threads and the publications.

#include <cmath>
#include <cstdint>

#include "shot_point_logic.h"

namespace assess_reticle
{

// The shot-point geometry (shot_point_logic.h), under this namespace's names.
using shot_point::RangeSource;
using shot_point::RangeSourceName;
using shot_point::RangeChoice;
using shot_point::PositiveFinite;
using shot_point::ChooseRange;
using shot_point::PointAlongRay;
using shot_point::DistanceToRayPoint;
using shot_point::FromHead;
using shot_point::Placement;
using shot_point::Place;

// ---- which "game aim" the mark is built from -------------------------------------------------------------------
//
// The mark must show the direction the game's SHOT leaves along, never a camera. Most titles' aim feedback is
// that direction. Reach on foot is the exception: its feedback is the compact camera's forward, which carries
// the camera's kick and shake, while the projectile is built along the unit's aiming vector (unit +0x214, HREK
// weapons.cpp; R-V12/R-V18) -- which carries neither. Drawn from the camera, the mark rides the DMR's recoil
// while the shots do not (measured: 68 shots land 27 mm mean from the hand's traced point,
// while the mark sits where the reticle is at the height of recoil). So Reach on foot uses the unit's aiming
// vector, and with no fresh one draws NOTHING rather than fall back to the camera.
enum class GameAimSource : uint8_t
{
    AimFeedback,    // the poll's own aim feedback (every title but Reach on foot; Reach seated is already +0x214)
    ReachUnitAim,   // Reach on foot: the local unit's aiming vector, unit +0x214
    None,           // Reach on foot with no fresh unit aim: no mark this poll
};

inline const char* GameAimSourceName(GameAimSource source)
{
    switch (source)
    {
    case GameAimSource::AimFeedback: return "aim feedback";
    case GameAimSource::ReachUnitAim: return "unit +0x214";
    case GameAimSource::None: return "none";
    default: return "?";
    }
}

inline GameAimSource ChooseGameAimSource(bool reachOnFoot, bool unitAimUsable)
{
    if (!reachOnFoot)
        return GameAimSource::AimFeedback;
    return unitAimUsable ? GameAimSource::ReachUnitAim : GameAimSource::None;
}

// A published unit-aim sample is usable when it is from this level (generation), at most `maxAgeMs` old, not from
// the future, and a finite, non-degenerate direction.
inline bool UnitAimUsable(uint32_t sampleGeneration, uint32_t generation, uint64_t sampleMs, uint64_t nowMs,
                          uint64_t maxAgeMs, const float aim[3])
{
    if (!aim || !generation || sampleGeneration != generation || !sampleMs || nowMs < sampleMs ||
        nowMs - sampleMs > maxAgeMs)
        return false;
    const float length = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    return std::isfinite(length) && length > 1.0e-4f;
}

// ---- the marker's art ------------------------------------------------------------------------------------------
//
// The assessment is a SECOND marker beside the player's reticle, never a replacement: with the reticle replaced
// the player aims WITH the assessment, which is not what is being assessed. So it must read as a different thing
// at a glance: a small MAGENTA ring with a centre dot (the reticle is cyan arcs and ticks by default, red/green on
// targets). Its quad is kMarkerScaleOfReticle of the reticle's width at the same distance, so the ring sits inside
// the default reticle's inner ticks (radius 7/32 of its half-width) when the shot lands on the aim point.
inline constexpr uint32_t kMarkerTextureSize = 128;
inline constexpr float kMarkerScaleOfReticle = 0.25f;
inline constexpr float kMarkerRingRadius = 0.66f;     // of the quad's half-width
inline constexpr float kMarkerRingHalfWidth = 0.10f;
inline constexpr float kMarkerDotRadius = 0.18f;
inline constexpr float kMarkerOutline = 0.06f;        // dark rim around both, so it reads on any background
inline constexpr float kMarkerTint[3]{1.0f, 0.22f, 0.86f};   // magenta
inline constexpr float kMarkerOutlineShade = 0.25f;

inline float MarkerWidth(float reticleWidth) { return reticleWidth * kMarkerScaleOfReticle; }

// The marker's outermost extent, as a fraction of the quad's half-width.
inline constexpr float MarkerOuterRadius() { return kMarkerRingRadius + kMarkerRingHalfWidth + kMarkerOutline; }

// One texel, RGBA8 packed as the swapchain stores it: R in the low byte for an RGBA format, B there for BGRA.
// Straight (unpremultiplied) alpha, like the reticle's.
inline uint32_t MarkerPixel(uint32_t x, uint32_t y, uint32_t size, bool bgra)
{
    if (!size)
        return 0;
    const float half = static_cast<float>(size) * 0.5f;
    const float u = (static_cast<float>(x) + 0.5f - half) / half;
    const float v = (static_cast<float>(y) + 0.5f - half) / half;
    const float r = std::sqrt(u * u + v * v);
    const float aa = 1.5f / half;   // ~1.5 texels of edge
    const auto coverage = [aa](float distance, float halfWidth) {
        const float c = (halfWidth - distance) / aa + 0.5f;
        return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    };
    const float ringDistance = std::fabs(r - kMarkerRingRadius);
    const float fill = std::fmax(coverage(ringDistance, kMarkerRingHalfWidth), coverage(r, kMarkerDotRadius));
    const float outline = std::fmax(coverage(ringDistance, kMarkerRingHalfWidth + kMarkerOutline),
                                    coverage(r, kMarkerDotRadius + kMarkerOutline));
    const float alpha = std::fmax(fill, outline);
    const auto channel = [&](float tint) {
        const float shaded = tint * (kMarkerOutlineShade + (1.0f - kMarkerOutlineShade) * (alpha > 0.0f ? fill / alpha : 0.0f));
        const float value = shaded * 255.0f + 0.5f;
        return static_cast<uint32_t>(value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value));
    };
    const uint32_t red = channel(kMarkerTint[0]), green = channel(kMarkerTint[1]), blue = channel(kMarkerTint[2]);
    const uint32_t a8 = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
    const uint32_t low = bgra ? blue : red, high = bgra ? red : blue;
    return (a8 << 24) | (high << 16) | (green << 8) | low;
}

} // namespace assess_reticle
