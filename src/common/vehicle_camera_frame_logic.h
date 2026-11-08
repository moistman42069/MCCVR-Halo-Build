#pragma once
// WHERE A VEHICLE CAMERA SITS: the geometry behind the camera file's "offset", "locationTracking",
// "rotationTracking" and leash (vehicle_camera_file.h), in the games' own space.
//
// SPACE. World units (1 wu = 3.048 m = 304.8 cm), Z up, right-handed: a frame is forward, LEFT and up, with
// up = forward x left -- the order of the engines' node matrices. A yaw is an angle about +Z, positive toward
// the left. The camera file speaks centimetres and [forward, RIGHT, up]; OffsetToWorld and LeashToBasis are the
// only places that cross over.
//
// TRACKING. A camera follows only the rotations of its vehicle that its mode lists:
//   the TILT: the frame's up leans with the vehicle by the tracked parts of its pitch and roll, taken about the
//     vehicle's OWN axes (its nose up or down, its bank), so "pitch" means the vehicle's pitch whichever way the
//     frame itself faces;
//   the HEADING: the vehicle's when yaw is tracked; otherwise a free heading (a world yaw the caller keeps),
//     laid onto that tilted deck -- the frame stands on the vehicle's floor and faces its own way.
// With all three tracked the frame IS the vehicle's; with none it is level at the free heading.
//
// Pure: no engine, no clock, no allocation.

#include <cmath>

namespace vehicle_camera_frame
{
inline constexpr float kWorldUnitsPerCm = 1.0f / 304.8f;

struct Basis
{
    float forward[3];
    float left[3];
    float up[3];
};

inline float Dot(const float a[3], const float b[3]) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline void Cross(const float a[3], const float b[3], float out[3]) noexcept
{
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

inline bool Normalize(float v[3]) noexcept
{
    const float length2 = Dot(v, v);
    if (!std::isfinite(length2) || length2 < 1.0e-12f)
        return false;
    const float scale = 1.0f / std::sqrt(length2);
    v[0] *= scale;
    v[1] *= scale;
    v[2] *= scale;
    return true;
}

// Three finite unit vectors at right angles, right-handed.
inline bool Usable(const Basis& basis) noexcept
{
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(basis.forward[k]) || !std::isfinite(basis.left[k]) || !std::isfinite(basis.up[k]))
            return false;
    float cross[3];
    Cross(basis.forward, basis.left, cross);
    return std::fabs(Dot(basis.forward, basis.forward) - 1.0f) <= 0.02f &&
           std::fabs(Dot(basis.left, basis.left) - 1.0f) <= 0.02f &&
           std::fabs(Dot(basis.up, basis.up) - 1.0f) <= 0.02f && std::fabs(Dot(basis.forward, basis.left)) <= 0.02f &&
           std::fabs(Dot(basis.forward, basis.up)) <= 0.02f && std::fabs(Dot(basis.left, basis.up)) <= 0.02f &&
           Dot(cross, basis.up) > 0.9f;
}

// The frame with this forward whose up is as near `up` as a right angle allows. False (and `out` untouched)
// when either is not a direction or the two are parallel.
inline bool FromForwardUp(const float forward[3], const float up[3], Basis& out) noexcept
{
    Basis basis{};
    for (int k = 0; k < 3; ++k)
        basis.forward[k] = forward[k];
    if (!Normalize(basis.forward))
        return false;
    Cross(up, basis.forward, basis.left);
    if (!Normalize(basis.left))
        return false;
    Cross(basis.forward, basis.left, basis.up);
    if (!Normalize(basis.up))
        return false;
    out = basis;
    return true;
}

// The frame turned `yaw` about +Z, then nosed up by `pitch`, then banked by `roll` about its own forward
// (positive lifts the left side). Radians.
inline Basis FromAngles(float yaw, float pitch, float roll) noexcept
{
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const float levelLeft[3]{-sy, cy, 0.0f};
    const float levelUp[3]{-sp * cy, -sp * sy, cp};
    Basis basis{};
    basis.forward[0] = cp * cy;
    basis.forward[1] = cp * sy;
    basis.forward[2] = sp;
    for (int k = 0; k < 3; ++k)
    {
        basis.left[k] = cr * levelLeft[k] + sr * levelUp[k];
        basis.up[k] = cr * levelUp[k] - sr * levelLeft[k];
    }
    return basis;
}

// The angles FromAngles would take to build `basis`. Pointing straight up or down the heading is lost; the
// yaw is then read as zero and the turn appears in the roll.
inline void ToAngles(const Basis& basis, float& yaw, float& pitch, float& roll) noexcept
{
    const float level = std::sqrt(basis.forward[0] * basis.forward[0] + basis.forward[1] * basis.forward[1]);
    pitch = std::atan2(basis.forward[2], level);
    yaw = level > 1.0e-6f ? std::atan2(basis.forward[1], basis.forward[0]) : 0.0f;
    const Basis unrolled = FromAngles(yaw, pitch, 0.0f);
    roll = std::atan2(Dot(basis.left, unrolled.up), Dot(basis.left, unrolled.left));
}

inline float Heading(const Basis& basis) noexcept
{
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    ToAngles(basis, yaw, pitch, roll);
    return yaw;
}

// The frame that follows only the listed rotations of `vehicle` (see TRACKING above). `freeYaw` is the heading
// used when yaw is not tracked. When the deck stands nearly on edge a free heading cannot be laid onto it, and
// the tilted frame keeps the vehicle's own heading instead of inventing one.
inline Basis Tracked(const Basis& vehicle, bool trackYaw, bool trackPitch, bool trackRoll, float freeYaw) noexcept
{
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    ToAngles(vehicle, yaw, pitch, roll);
    Basis tilted = FromAngles(yaw, trackPitch ? pitch : 0.0f, trackRoll ? roll : 0.0f);
    if (trackYaw)
        return tilted;
    // Laid onto the deck VERTICALLY: the level heading is raised or lowered until it lies in the deck, so its
    // compass bearing stays exactly the free heading however the deck tilts. The view never yaws, it only tilts.
    if (std::fabs(tilted.up[2]) > 0.1f)
    {
        const float heading[2]{std::cos(freeYaw), std::sin(freeYaw)};
        const float rise = -(heading[0] * tilted.up[0] + heading[1] * tilted.up[1]) / tilted.up[2];
        float forward[3]{heading[0], heading[1], rise};
        if (Normalize(forward))
            for (int k = 0; k < 3; ++k)
                tilted.forward[k] = forward[k];
    }
    Cross(tilted.up, tilted.forward, tilted.left);
    Normalize(tilted.left);
    return tilted;
}

// A file offset, [forward, right, up] in cm, as a world vector along `basis`. `reach` scales it: 1 = the whole
// offset, less when something blocks the way.
inline void OffsetToWorld(const Basis& basis, const float offsetCm[3], float reach, float out[3]) noexcept
{
    const float scale = kWorldUnitsPerCm * reach;
    for (int k = 0; k < 3; ++k)
        out[k] = (basis.forward[k] * offsetCm[0] - basis.left[k] * offsetCm[1] + basis.up[k] * offsetCm[2]) * scale;
}

// THE LEASH, as the box the head may move in around the camera's point. The file's limits are places measured
// from the ORIGIN like the offset; the head moves relative to the camera's point, so the box is shifted by
// where that point is (`cameraCm`: the offset, times any pull-in) and turned into world units along forward /
// LEFT / up. The camera's own point always stays inside: a limit set on the far side of it becomes the point
// itself, so a leash never moves the view, it only stops it. An unlimited side stays far out of reach.
struct LeashBox
{
    float low[3];  // forward, left, up
    float high[3];
};
inline LeashBox LeashToBasis(const float leashMinCm[3], const float leashMaxCm[3], const float cameraCm[3]) noexcept
{
    float low[3], high[3]; // forward, right, up, relative to the camera's point, in cm
    for (int axis = 0; axis < 3; ++axis)
    {
        const float l = leashMinCm[axis] - cameraCm[axis];
        const float h = leashMaxCm[axis] - cameraCm[axis];
        low[axis] = l < 0.0f ? l : 0.0f;
        high[axis] = h > 0.0f ? h : 0.0f;
    }
    LeashBox box{};
    box.low[0] = low[0] * kWorldUnitsPerCm;
    box.high[0] = high[0] * kWorldUnitsPerCm;
    box.low[1] = -high[1] * kWorldUnitsPerCm; // furthest right is the lowest left
    box.high[1] = -low[1] * kWorldUnitsPerCm;
    box.low[2] = low[2] * kWorldUnitsPerCm;
    box.high[2] = high[2] * kWorldUnitsPerCm;
    return box;
}

// How far a head displaced `head` (world) from the camera's point overshoots the box along `basis`, written to
// `out` as the world vector to SUBTRACT from the view's position: zero inside the box, and on each axis exactly
// the overshoot, so past a limit the view stops following the head that way and the other axes are untouched.
inline void LeashShift(const float head[3], const Basis& basis, const LeashBox& box, float out[3]) noexcept
{
    const float* const axes[3]{basis.forward, basis.left, basis.up};
    out[0] = out[1] = out[2] = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float along = Dot(head, axes[axis]);
        const float kept = along < box.low[axis] ? box.low[axis] : (along > box.high[axis] ? box.high[axis] : along);
        const float over = along - kept;
        for (int k = 0; k < 3; ++k)
            out[k] += over * axes[axis][k];
    }
}

// `v`, given in `basis`'s forward / left / up coordinates, as a world vector; and a world vector in them.
inline void FromBasis(const Basis& basis, const float v[3], float out[3]) noexcept
{
    for (int k = 0; k < 3; ++k)
        out[k] = basis.forward[k] * v[0] + basis.left[k] * v[1] + basis.up[k] * v[2];
}
inline void ToBasis(const Basis& basis, const float v[3], float out[3]) noexcept
{
    const float x = Dot(basis.forward, v), y = Dot(basis.left, v), z = Dot(basis.up, v);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// `child`'s axes as seen from `parent`, in its forward / left / up coordinates.
inline Basis InFrame(const Basis& parent, const Basis& child) noexcept
{
    Basis out{};
    ToBasis(parent, child.forward, out.forward);
    ToBasis(parent, child.left, out.left);
    ToBasis(parent, child.up, out.up);
    return out;
}

// `parent` turned as far as a child seen from it (InFrame) has turned since `rest` (the same, taken earlier),
// about the parent's own axes. At rest the result is `parent` itself whichever way the child's own axes point --
// a head marker's need not be its face's.
inline Basis TurnedSince(const Basis& parent, const Basis& child, const Basis& rest) noexcept
{
    static constexpr float kAxes[3][3]{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    Basis out{};
    float* const outAxes[3]{out.forward, out.left, out.up};
    for (int a = 0; a < 3; ++a)
    {
        float inRest[3], turned[3];
        ToBasis(rest, kAxes[a], inRest);    // the rest's inverse, applied to the parent's axis
        FromBasis(child, inRest, turned);   // ... then the child's turn
        FromBasis(parent, turned, outAxes[a]);
    }
    return out;
}

// The difference between two yaws, wrapped into (-pi, pi].
inline float YawDelta(float to, float from) noexcept
{
    constexpr float kPi = 3.14159265358979323846f;
    float delta = std::remainder(to - from, 2.0f * kPi);
    if (delta <= -kPi)
        delta += 2.0f * kPi;
    return delta;
}
} // namespace vehicle_camera_frame
