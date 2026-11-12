#pragma once
// A LOOK-STEERED DRIVER'S HAND AIM, REFUSED BEHIND AND NEAR (vehicle_aim_reject_metres, vehicle_aim_reject_dot).
//
// While the hand steers a look-steered vehicle (Motion, vehicle_control_logic.h) the vehicle turns to face the
// heading the hand's aim writes. With a camera placed outside the vehicle the hand's ray easily lands on the ground
// just behind it, and the aim written from the seat through that point points backward: the vehicle swings round to
// face it, for as long as the hand strays there -- usually a moment, and never meant. So a point that is BOTH behind
// the vehicle AND near it steers nothing: the heading written is the vehicle's own forward, and it does not turn.
// Not the last accepted aim: that is a heading the vehicle may not be on any more, and would keep it turning.
//
// THE VEHICLE'S GROUND PLANE, not the world's: the plane through the vehicle's origin across its own up. On a slope
// "behind" and "near" are the vehicle's, as a player sitting in it sees them. The offset from the origin to the point
// is flattened onto that plane (its part along up dropped) and read in 2D, forward and left:
//   behind  the flattened offset's direction, dotted with the flattened forward, is BELOW `dot`
//           (-0.5: more than 120 deg off the nose; 0: more than 90; -1: never).
//   near    the flattened offset is no longer than the distance. FLATTENED, not 3D: the heading the vehicle turns to
//           comes from the flattened offset alone, and the shorter it is the further a small move of the hand swings
//           that heading -- a point's height above or below the plane does nothing to steady it. It also keeps the
//           origin's own height above the ground out of the distance.
//   a point at the origin (a flattened offset under kNoHeadingWu: straight above or below it) names no heading at
//           all; any heading it gave would be noise, so while the rule is on it is refused like a point behind.
//
// WITHOUT A POINT (no trace has given the hand's ray a range, so the aim was steered along the hand at the crosshair
// distance), the aim's own direction is judged: behind by its flattened direction's dot -- that direction IS the
// heading written then -- and near where its ray from the rendered eye meets the ground plane. A ray that never
// meets the plane (parallel to it, or leaving it) has no near point and steers as before.
//
// Pure: no engine, no clock, no allocation. World units (1 wu = 3.048 m), the vectors as the game gives them.

#include <cmath>
#include <cstdint>

namespace vehicle_aim_reject
{
inline constexpr float kWuPerMetre = 1.0f / 3.048f;
// Shorter than this, a flattened offset or direction names no heading (1 cm).
inline constexpr float kNoHeadingWu = 0.01f * kWuPerMetre;
// The config's defaults and bounds (vehicle_aim_reject_metres, vehicle_aim_reject_dot).
inline constexpr float kDefaultMetres = 20.0f;
inline constexpr float kMaxMetres = 20.0f;
inline constexpr float kDefaultDot = -0.5f;

struct Limits
{
    float distanceWu = 0.0f;   // 0: the rule is off
    float dot = kDefaultDot;
    constexpr bool on() const noexcept { return distanceWu > 0.0f; }
};

// The config's metres and dot as the rule's limits. Metres not above 0 (or not a number) switch the rule off; a dot
// that is not a number is the default, and one outside [-1, 1] is held to it.
inline Limits LimitsFromConfig(float metres, float dot) noexcept
{
    Limits limits{};
    limits.distanceWu = std::isfinite(metres) && metres > 0.0f
        ? (metres < kMaxMetres ? metres : kMaxMetres) * kWuPerMetre : 0.0f;
    limits.dot = !std::isfinite(dot) ? kDefaultDot : dot < -1.0f ? -1.0f : dot > 1.0f ? 1.0f : dot;
    return limits;
}

inline float Dot(const float a[3], const float b[3]) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The vehicle's ground frame: its origin, and unit forward, left and up at right angles (forward x left = up).
struct Ground
{
    float origin[3]{};
    float forward[3]{};
    float left[3]{};
    float up[3]{};
};

// From the vehicle's origin, forward and up as read: up normalised, forward made square to it (a hull's two vectors
// are only nearly so). False: not finite, or forward (nearly) along up -- no plane to judge in.
inline bool GroundFrom(const float origin[3], const float forward[3], const float up[3], Ground& out) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(forward[axis]) || !std::isfinite(up[axis]))
            return false;
    const float upLength = std::sqrt(Dot(up, up));
    if (!(upLength > 1.0e-6f))
        return false;
    float u[3]{up[0] / upLength, up[1] / upLength, up[2] / upLength};
    const float along = Dot(forward, u);
    float f[3]{forward[0] - along * u[0], forward[1] - along * u[1], forward[2] - along * u[2]};
    const float forwardLength = std::sqrt(Dot(f, f));
    const float forwardRead = std::sqrt(Dot(forward, forward));
    if (!(forwardLength > 1.0e-3f * forwardRead) || !(forwardLength > 1.0e-6f))
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        f[axis] /= forwardLength;
        out.origin[axis] = origin[axis];
        out.forward[axis] = f[axis];
        out.up[axis] = u[axis];
    }
    // left = up x forward
    out.left[0] = u[1] * f[2] - u[2] * f[1];
    out.left[1] = u[2] * f[0] - u[0] * f[2];
    out.left[2] = u[0] * f[1] - u[1] * f[0];
    return true;
}

// A vector in the ground plane's 2D coordinates: along forward, along left (its part along up dropped).
inline void Flatten(const Ground& ground, const float v[3], float& x, float& y) noexcept
{
    x = Dot(v, ground.forward);
    y = Dot(v, ground.left);
}

// THE RULE, on what was measured. `distanceWu`: the flattened distance from the origin; `dot`: the flattened
// direction's dot with forward, read only with `haveHeading`. Near is inclusive (at the distance is near); behind is
// strict (at the dot is not behind). Off, or a distance that is not a number, refuses nothing.
inline bool Refuses(const Limits& limits, float dot, float distanceWu, bool haveHeading) noexcept
{
    if (!limits.on() || !(distanceWu <= limits.distanceWu))
        return false;
    if (!haveHeading)
        return true;
    return dot < limits.dot;
}

enum class Verdict : uint8_t
{
    Off,        // the rule is off
    Accepted,   // the aim steers as it would without the rule
    Refused,    // behind and near: the vehicle keeps its forward
};

struct Judgement
{
    Verdict verdict = Verdict::Off;
    float dot = 0.0f;          // the flattened direction's dot with forward (0 with no heading)
    float distanceWu = 0.0f;   // the flattened distance from the origin to the point judged (-1: none)
    bool heading = false;      // the flattened direction names a heading
};

// A point the aim resolves to (world units).
inline Judgement JudgePoint(const Limits& limits, const Ground& ground, const float point[3]) noexcept
{
    Judgement judgement{};
    if (!limits.on())
        return judgement;
    const float offset[3]{point[0] - ground.origin[0], point[1] - ground.origin[1], point[2] - ground.origin[2]};
    float x = 0.0f, y = 0.0f;
    Flatten(ground, offset, x, y);
    const float distance = std::sqrt(x * x + y * y);
    judgement.distanceWu = std::isfinite(distance) ? distance : -1.0f;
    judgement.heading = distance > kNoHeadingWu;
    judgement.dot = judgement.heading ? x / distance : 0.0f;
    judgement.verdict = std::isfinite(distance) && Refuses(limits, judgement.dot, distance, judgement.heading)
        ? Verdict::Refused : Verdict::Accepted;
    return judgement;
}

// No point: the aim's direction `dir` (need not be unit length) from `eye`, where the aim starts. Behind by the
// flattened direction; near where the ray meets the ground plane, ahead of the eye.
inline Judgement JudgeRay(const Limits& limits, const Ground& ground, const float eye[3], const float dir[3]) noexcept
{
    Judgement judgement{};
    if (!limits.on())
        return judgement;
    judgement.verdict = Verdict::Accepted;
    judgement.distanceWu = -1.0f;
    float dx = 0.0f, dy = 0.0f;
    Flatten(ground, dir, dx, dy);
    const float dirLength = std::sqrt(Dot(dir, dir));
    const float flatLength = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(dirLength) || !(dirLength > 0.0f) || !std::isfinite(flatLength))
        return judgement;
    judgement.heading = flatLength > 1.0e-4f * dirLength;
    judgement.dot = judgement.heading ? dx / flatLength : 0.0f;
    // Where the ray meets the plane: eye + t dir with (eye + t dir - origin) . up = 0, for t > 0.
    const float height = (eye[0] - ground.origin[0]) * ground.up[0] + (eye[1] - ground.origin[1]) * ground.up[1] +
        (eye[2] - ground.origin[2]) * ground.up[2];
    const float rate = Dot(dir, ground.up);
    if (!std::isfinite(height) || !(std::fabs(rate) > 1.0e-6f * dirLength))
        return judgement;   // parallel to the plane: it never meets it
    const float t = -height / rate;
    if (!std::isfinite(t) || !(t > 0.0f))
        return judgement;   // leaving the plane: it met it behind the eye, if at all
    const float meet[3]{eye[0] + dir[0] * t, eye[1] + dir[1] * t, eye[2] + dir[2] * t};
    const float offset[3]{meet[0] - ground.origin[0], meet[1] - ground.origin[1], meet[2] - ground.origin[2]};
    float x = 0.0f, y = 0.0f;
    Flatten(ground, offset, x, y);
    const float distance = std::sqrt(x * x + y * y);
    if (!std::isfinite(distance))
        return judgement;
    judgement.distanceWu = distance;
    if (Refuses(limits, judgement.dot, distance, judgement.heading))
        judgement.verdict = Verdict::Refused;
    return judgement;
}

// The heading that keeps the vehicle on its forward: the world yaw of its forward as read (atan2(y, x), the yaw a
// look-steered driver's aim takes as the hull's heading). False: forward is (nearly) vertical, and has no heading.
inline bool ForwardYaw(const float forward[3], float& yaw) noexcept
{
    if (!std::isfinite(forward[0]) || !std::isfinite(forward[1]) || !std::isfinite(forward[2]))
        return false;
    const float horizontal = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    const float length = std::sqrt(Dot(forward, forward));
    if (!(horizontal > 1.0e-3f * length) || !(horizontal > 1.0e-6f))
        return false;
    yaw = std::atan2(forward[1], forward[0]);
    return std::isfinite(yaw);
}
} // namespace vehicle_aim_reject
