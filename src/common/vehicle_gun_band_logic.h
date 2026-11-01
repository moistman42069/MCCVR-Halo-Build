#pragma once

// THE GUN BAND of a look-steered land driver (Halo 3: the Chopper, the Ghost). The rule it serves: respect
// the natural rotational limits of land vehicles; air vehicles never clamp.
//
// What the game does (static RE of retail halo3.dll, docs/HALO3-VEHICLE-EVIDENCE.md "Vehicle guns"). A vehicle gun
// with a gunner fires along its BARREL marker's forward (the fire routine 0x3683A0 -> the shot line 0x3524B0 at
// 0x368B92, unit-aim flag clear), and the firing assist 0x13BAD0 (called at 0x368DFE) then turns that shot toward
// the point the seat camera's crosshair trace hits -- but, in a seat, by at most max(21 deg, Q) (the rotate-toward at
// 0x13C2EB..0x13C361; Q from the weapon tag, 0x13C518 +0x1C). A Chopper's guns are fixed to the hull, so a hand aimed
// 26 deg below them at 5.7 m turns the bolts only 21 deg down: they land short of the reticle. The turn the assist
// cannot make in ELEVATION is a natural rotational limit of the gun, so the reticle stops there. Its YAW is not: the
// hull turns toward the hand (a look-steered driver steers by the aim), so a yaw past the cap is catch-up, never a
// stop -- the record's own yaw cone (the latch) still clamps yaw where a seat has one.
//
// The band is learned, never assumed. Each shot's barrel direction (before the assist turns it) is taken into the
// hull's object frame; a barrel that holds still in that frame is hull-fixed, and its elevation +/- the cap is the
// band. A barrel that moves more than 1 deg in elevation between shots (3 shots in a row) is ANIMATED -- it follows
// the aim -- and that vehicle gets no band. Azimuth is held to 5 deg: the barrel marker and the object frame may be a
// tick apart while the hull turns, which reads as azimuth, not elevation. The shot's origin is learned the same way,
// so the clamp is applied to the shot's own line (from the barrel toward the crosshair point), not to the eye's.
//
// Pure half: the learner, the cap, the band and the clamp of one shot. The callers own the threads (the shot hook and
// the drive detour on the simulation thread, the aim poll on the input thread), the reads and the drawing.
// Zero engine dependencies; unit-tested in tests/vehicle_gun_band_tests.cpp, mutation-checked by
// tools/mutate_vehicle_gun_aim.py.

#include "seat_frame_logic.h"
#include "seat_limit_reticle_logic.h"

#include <cmath>
#include <cstdint>

namespace gun_band
{
inline constexpr float kRadToDeg = 57.2957795f;
inline constexpr int kBarrels = 4;
inline constexpr float kSteadyElevationRad = 0.01745329f;   // 1 deg
inline constexpr float kSteadyAzimuthRad = 0.0872665f;      // 5 deg
inline constexpr unsigned kAnimatedRun = 3;                 // off-reference shots in a row that mean "animated"
inline constexpr float kPoleMarginRad = 1.55f;              // a band edge is held short of the pole

// The vehicle's object frame (forward, up as the object stores them) as an orthonormal R = [f l u], l = u x f.
inline bool HullFrame(const float fwd[3], const float up[3], seat_frame::Frame& out)
{
    float u[3]{up[0], up[1], up[2]}, f[3]{fwd[0], fwd[1], fwd[2]};
    if (!seat_frame::Finite3(u) || !seat_frame::Finite3(f))
        return false;
    const float ul = std::sqrt(seat_frame::Dot(u, u)), fl = std::sqrt(seat_frame::Dot(f, f));
    if (!(ul > 0.9f && ul < 1.1f) || !(fl > 0.9f && fl < 1.1f))
        return false;
    for (int i = 0; i < 3; ++i)
    {
        u[i] /= ul;
        f[i] /= fl;
    }
    const float fu = seat_frame::Dot(f, u);
    if (std::fabs(fu) > 0.15f)
        return false;
    for (int i = 0; i < 3; ++i)
        f[i] -= fu * u[i];
    const float fl2 = std::sqrt(seat_frame::Dot(f, f));
    if (!(fl2 > 0.5f))
        return false;
    seat_frame::Frame r{};
    for (int i = 0; i < 3; ++i)
    {
        r.f[i] = f[i] / fl2;
        r.u[i] = u[i];
    }
    r.l[0] = r.u[1] * r.f[2] - r.u[2] * r.f[1];
    r.l[1] = r.u[2] * r.f[0] - r.u[0] * r.f[2];
    r.l[2] = r.u[0] * r.f[1] - r.u[1] * r.f[0];
    out = r;
    return true;
}

// A world vector in the hull frame (forward, left, up components), and back. `h` must be orthonormal (HullFrame).
inline void ToLocal(const seat_frame::Frame& h, const float w[3], float out[3])
{
    out[0] = seat_frame::Dot(h.f, w);
    out[1] = seat_frame::Dot(h.l, w);
    out[2] = seat_frame::Dot(h.u, w);
}

inline void ToWorld(const seat_frame::Frame& h, const float local[3], float out[3])
{
    seat_frame::Apply(h, local, out);
}

// One barrel's learned line, hull frame: its first shot's direction (the reference), the elevation range of the shots
// that held to it, and the running mean of their origins.
struct Barrel
{
    bool have = false;
    float ref[3]{};
    float elevMin = 0.0f, elevMax = 0.0f;
    float origin[3]{};
    unsigned shots = 0, run = 0;
};

enum class Shot : uint8_t
{
    Learned = 0,   // held to the barrel's line (or the first shot of it)
    Deviant,       // off the line, not yet animated (not used for the band)
    Animated,      // the barrels follow the aim: this vehicle has no band
    Refused,       // not a shot this learner can use (barrel index, a non-unit direction, non-finite)
};

// The barrels of ONE vehicle (handle and definition: anything else starts over).
struct Learner
{
    uint32_t vehicle = 0xFFFFFFFFu, definition = 0xFFFFFFFFu;
    bool animated = false;
    float animatedDeg = 0.0f;    // the move between shots that made it so
    Barrel barrel[kBarrels]{};
    unsigned shots = 0, deviant = 0;

    void reset(uint32_t v, uint32_t d)
    {
        *this = Learner{};
        vehicle = v;
        definition = d;
    }

    // One shot of barrel `index`: its unit direction and its origin, both in the hull frame.
    Shot step(int index, const float dir[3], const float origin[3])
    {
        if (index < 0 || index >= kBarrels || !seat_frame::Finite3(dir) || !seat_frame::Finite3(origin))
            return Shot::Refused;
        const float length = std::sqrt(seat_frame::Dot(dir, dir));
        if (!(length > 0.99f && length < 1.01f))
            return Shot::Refused;
        if (animated)
            return Shot::Animated;
        ++shots;
        Barrel& b = barrel[index];
        const float elev = std::asin(dir[2] > 1.0f ? 1.0f : (dir[2] < -1.0f ? -1.0f : dir[2]));
        if (!b.have)
        {
            b = Barrel{};
            b.have = true;
            for (int i = 0; i < 3; ++i)
            {
                b.ref[i] = dir[i];
                b.origin[i] = origin[i];
            }
            b.elevMin = b.elevMax = elev;
            b.shots = 1;
            return Shot::Learned;
        }
        const float refElev = std::asin(b.ref[2] > 1.0f ? 1.0f : (b.ref[2] < -1.0f ? -1.0f : b.ref[2]));
        const float dElev = std::fabs(elev - refElev);
        const float dAz = std::fabs(std::remainder(std::atan2(dir[1], dir[0]) - std::atan2(b.ref[1], b.ref[0]),
                                                   6.28318531f));
        if (dElev > kSteadyElevationRad || dAz > kSteadyAzimuthRad)
        {
            ++deviant;
            if (++b.run >= kAnimatedRun)
            {
                animated = true;
                animatedDeg = (dElev > dAz ? dElev : dAz) * kRadToDeg;
                return Shot::Animated;
            }
            return Shot::Deviant;
        }
        b.run = 0;
        b.elevMin = elev < b.elevMin ? elev : b.elevMin;
        b.elevMax = elev > b.elevMax ? elev : b.elevMax;
        ++b.shots;
        for (int i = 0; i < 3; ++i)
            b.origin[i] += (origin[i] - b.origin[i]) / static_cast<float>(b.shots);
        return Shot::Learned;
    }

    // The barrels' elevation range, the MOST PERMISSIVE across them (the lowest low, the highest high).
    bool elevationRange(float& lo, float& hi) const
    {
        bool any = false;
        for (const Barrel& b : barrel)
        {
            if (!b.have)
                continue;
            lo = any && lo < b.elevMin ? lo : b.elevMin;
            hi = any && hi > b.elevMax ? hi : b.elevMax;
            any = true;
        }
        return any && !animated;
    }

    // Where the shots leave, hull frame: the mean of the barrels' origins (the guns sit side by side).
    bool originLocal(float out[3]) const
    {
        unsigned n = 0;
        float sum[3]{};
        for (const Barrel& b : barrel)
        {
            if (!b.have)
                continue;
            for (int i = 0; i < 3; ++i)
                sum[i] += b.origin[i];
            ++n;
        }
        if (!n || animated)
            return false;
        for (int i = 0; i < 3; ++i)
            out[i] = sum[i] / static_cast<float>(n);
        return true;
    }

    bool learned() const
    {
        float lo = 0.0f, hi = 0.0f;
        return elevationRange(lo, hi);
    }
};

// THE WEAPON'S OWN ANGLE Q, as the assist's query (0x13C518) computes it for an unzoomed weapon: the tag's primary
// angle, maxed with its secondary unless the flag bit is set (+0x340, +0x328, flags +0x18C bit 25 in Halo 3). NaN when
// either read is not a plausible angle.
inline float TagAngle(uint32_t flags, unsigned shift, float primary, float secondary)
{
    if (shift > 31 || !std::isfinite(primary) || !std::isfinite(secondary) || primary < 0.0f || secondary < 0.0f ||
        primary > 3.1416f || secondary > 3.1416f)
        return std::nanf("");
    return ((flags >> shift) & 1u) ? primary : (primary > secondary ? primary : secondary);
}

// THE CAP: max(the binary's floor, Q). NaN unless the floor is a positive angle; a NaN Q leaves the floor.
inline float Cap(float floor, float q)
{
    if (!std::isfinite(floor) || !(floor > 0.0f) || floor > 3.1416f)
        return std::nanf("");
    return std::isfinite(q) && q > floor ? q : floor;
}

// THE BAND, elevation relative to the hull's up: the barrels' range widened by the cap, held short of the pole.
inline bool Band(const Learner& l, float cap, float& lo, float& hi)
{
    float elevLo = 0.0f, elevHi = 0.0f;
    if (!std::isfinite(cap) || !(cap > 0.0f) || !l.elevationRange(elevLo, elevHi))
        return false;
    lo = elevLo - cap;
    hi = elevHi + cap;
    lo = lo < -kPoleMarginRad ? -kPoleMarginRad : lo;
    hi = hi > kPoleMarginRad ? kPoleMarginRad : hi;
    return lo <= hi;
}

struct ShotClamp
{
    bool clamped = false;        // the shot's elevation is past the band
    bool low = false;            // ...below it (else above)
    float dir[3]{};              // the shot's direction: toward the point, or held at the band edge
    float worldYaw = 0.0f, worldPitch = 0.0f;
    float overshootDeg = 0.0f;   // the angle between the line to the point and the held shot (0 inside)
};

// ONE SHOT: from `origin` toward `point` (world), its elevation relative to `hull`'s up held to [lo, hi]; the azimuth
// in the hull frame is kept (yaw past the cap is catch-up, not a stop). False when the line or the frame does not
// resolve (the caller then clamps nothing).
inline bool ClampShot(const seat_frame::Frame& hull, float lo, float hi, const float origin[3], const float point[3],
                      ShotClamp& out)
{
    if (!seat_frame::Finite3(origin) || !seat_frame::Finite3(point) || !std::isfinite(lo) || !std::isfinite(hi) ||
        lo > hi)
        return false;
    const float v[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    seat_frame::Angles line{};
    if (!(seat_frame::Dot(v, v) > 1.0e-6f) || !seat_frame::AnglesOf(v, 0.0f, line))
        return false;
    seat_limit::Stops band{};
    band.rebased = true;   // the hull frame: pitch there IS the elevation relative to the hull's up
    band.frame = hull;
    seat_limit::AddInterval(band, 1, lo, hi);
    seat_limit::Result r{};
    if (!seat_limit::Clamp(band, line.yaw, line.pitch, 0.0f, r))
        return false;
    ShotClamp c{};
    c.clamped = r.clamped[1];
    if (c.clamped)
    {
        seat_frame::Angles local{};
        c.low = seat_frame::RecordFromWorld(hull, line.yaw, line.pitch, 0.0f, local) && local.pitch < lo;
    }
    for (int i = 0; i < 3; ++i)
        c.dir[i] = r.dir[i];
    c.worldYaw = r.worldYaw;
    c.worldPitch = r.worldPitch;
    c.overshootDeg = r.overshootDeg;
    out = c;
    return true;
}
} // namespace gun_band
