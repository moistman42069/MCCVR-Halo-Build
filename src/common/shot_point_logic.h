#pragma once

// SHOT-POINT GEOMETRY (all builds; pure). Where a shot lands and how to draw a mark there from the eye.
//
// A shot does not leave the controller: the game fires it from its OWN shot origin E (on foot the engine eye, in
// a seat the seat's eye) along a direction a, and it lands on the first surface that ray meets. The mark is the
// WORLD POINT P = E + a * r, where r is the first hit along that ray (a trace of it) or a fallback, drawn on the ray
// from the rendered centre eye H through P, at |P - H|: the eye-anchored geometry the traced reticle uses, so it
// sits on the surface the shot would hit, at the right depth in both eyes.
//
// Users, which share this CODE and nothing else: the player-facing seat limit reticle (a seat clamped at its stop:
// a = the clamped aim) and the dev assessment marker (assess_reticle_logic.h: a = the game's own aim). The
// callers own the threads and the publications.

#include <cmath>
#include <cstdint>

namespace shot_point
{

// Where r came from, best first.
enum class RangeSource : uint8_t
{
    GameAimTrace,       // a trace along the shot's own direction from its shot origin (exact)
    IntentTrace,        // the intent ray's traced point, taken at its distance from E (exact while the aim is
                        // converged on that point; during lag the depth is that point's, the direction exact)
    CrosshairDistance,  // nothing traced: crosshair_distance_m along the game's aim from E
    Count
};

inline const char* RangeSourceName(RangeSource source)
{
    switch (source)
    {
    case RangeSource::GameAimTrace: return "game-aim trace";
    case RangeSource::IntentTrace: return "intent-ray trace";
    case RangeSource::CrosshairDistance: return "crosshair distance";
    default: return "?";
    }
}

struct RangeChoice
{
    float rangeWu = 0.0f;   // along the game's aim from E, in the title's world units
    bool surface = false;   // r is a surface the shot meets (a trace HIT); false = parked (a miss or a guess)
    RangeSource source = RangeSource::CrosshairDistance;
};

inline bool PositiveFinite(float v) { return std::isfinite(v) && v > 0.0f; }

// Picks r. `gameAim*`: a fresh trace along the game's aim (a miss counts as the trace's far end, as the intent
// ray's does). `intent*`: the intent ray's fresh traced point, as its distance from E. `fallbackWu`: the
// crosshair distance in world units. Never returns a non-positive range while fallbackWu is positive.
inline RangeChoice ChooseRange(bool gameAimFresh, bool gameAimHit, float gameAimRangeWu, bool intentFresh,
                               bool intentHit, float intentDistanceWu, float fallbackWu, float traceLengthWu)
{
    RangeChoice out{};
    if (gameAimFresh)
    {
        if (gameAimHit && PositiveFinite(gameAimRangeWu))
            return {gameAimRangeWu, true, RangeSource::GameAimTrace};
        if (PositiveFinite(traceLengthWu))
            return {traceLengthWu, false, RangeSource::GameAimTrace};
    }
    if (intentFresh && PositiveFinite(intentDistanceWu))
        return {intentDistanceWu, intentHit, RangeSource::IntentTrace};
    out.rangeWu = PositiveFinite(fallbackWu) ? fallbackWu : 0.0f;
    return out;
}

// out = origin + normalize(dir) * range. False (out untouched) on a degenerate direction, a non-positive or
// non-finite range, or any non-finite input.
inline bool PointAlongRay(const float origin[3], const float dir[3], float range, float out[3])
{
    if (!origin || !dir || !out || !PositiveFinite(range))
        return false;
    const float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!std::isfinite(length) || length < 1.0e-6f)
        return false;
    float p[3];
    for (int i = 0; i < 3; ++i)
    {
        p[i] = origin[i] + dir[i] / length * range;
        if (!std::isfinite(p[i]))
            return false;
    }
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    return true;
}

// The distance from `eye` to the point `range` along the (unit or not) intent ray from `head`: the range the
// game's aim reaches the intent ray's traced point at, when it is converged on it. Negative on bad input.
inline float DistanceToRayPoint(const float eye[3], const float head[3], const float dir[3], float range)
{
    float p[3];
    if (!eye || !PointAlongRay(head, dir, range, p))
        return -1.0f;
    const float d[3]{p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]};
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    return std::isfinite(distance) ? distance : -1.0f;
}

// The eye-anchored ray to draw the mark on: the unit direction from the rendered eye `head` to `point`, and
// its length. False when the point is (nearly) at the eye or anything is non-finite.
inline bool FromHead(const float head[3], const float point[3], float outDir[3], float& outRange)
{
    if (!head || !point || !outDir)
        return false;
    const float d[3]{point[0] - head[0], point[1] - head[1], point[2] - head[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!std::isfinite(length) || length < 1.0e-4f)
        return false;
    for (int i = 0; i < 3; ++i)
        outDir[i] = d[i] / length;
    outRange = length;
    return true;
}

// The whole placement: P = E + a * r from the chosen range, then the ray from H to P. `unitsPerVrMetre`
// converts the length into VR metres for the compositor. False leaves the caller on its fallback.
struct Placement
{
    float point[3]{};    // P, world units
    float dir[3]{};      // unit, world, from H toward P
    float rangeVrM = 0.0f;
    bool surface = false;
    RangeSource source = RangeSource::CrosshairDistance;
};

inline bool Place(const float head[3], const float shotOrigin[3], const float gameAim[3], const RangeChoice& range,
                  float unitsPerVrMetre, Placement& out)
{
    if (!PositiveFinite(unitsPerVrMetre))
        return false;
    Placement p{};
    float lengthWu = 0.0f;
    if (!PointAlongRay(shotOrigin, gameAim, range.rangeWu, p.point) || !FromHead(head, p.point, p.dir, lengthWu))
        return false;
    p.rangeVrM = lengthWu / unitsPerVrMetre;
    if (!PositiveFinite(p.rangeVrM))
        return false;
    p.surface = range.surface;
    p.source = range.source;
    out = p;
    return true;
}

} // namespace shot_point
