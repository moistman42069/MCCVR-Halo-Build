#pragma once

// SEAT LIMIT RETICLE -- the reticle stops where the gun stops.
//
// A land vehicle's seat has natural ROTATIONAL limits: a passenger's yaw cone and pitch stops, a turret's pitch
// stops, a tank gun's limits. Every MCC title applies them inside the player-control accumulator, in the RECORD's
// frame (the angles the vehicle-tilt rebase turns into the world aim: world angles in case world / a, R^-1 of the
// world aim in case b, seat_frame_logic.h). Direct drive keeps writing the hand's aim (it never falls back to the
// stick for a limit); the game clamps it, and the gun aims along the clamped direction. So that the player SEES
// the stop, the reticle then sits where the gun really aims: on the limit-clamped direction, converged on the
// surface along that ray as traced from the seat's engine eye.
//
// What is NOT a limit, and never moves the reticle: a turret's traverse rate, a gun's catch-up, the game's aim still
// turning toward the hand. Those live downstream of the record (or are motion, not a stop), and no stop source here
// can see them: the seat-limit latch reads only the record the game clamped (aim_write::LimitLatch), and an interval
// is the record's own clamp. While the gun is still slewing the reticle stays on the hand's point. Air vehicles have
// no rotational limit: their seats publish no stops (the callers exclude them by identity), so nothing changes there.
//
// This header is the pure half: the stops in the record frame, the per-axis clamp of a world aim through that frame
// and back, and the hysteresis that keeps the reticle from flickering at the edge. The callers own the threads, the
// stop sources (the latch on the sim thread, a record's eased pitch limits), the traces and the drawing.
//
// Zero engine dependencies; unit-tested in tests/seat_limit_reticle_tests.cpp.

#include "aim_write_logic.h"
#include "seat_frame_logic.h"

#include <cmath>
#include <cstdint>

namespace seat_limit
{

inline constexpr float kRadToDeg = 57.2957795f;

// One axis's stops, record frame, radians. A one-sided stop (the latch: the game moved our write back from one side)
// has only `min` or only `max`; an interval (Halo 2's eased pitch limits) has both.
struct AxisStops
{
    bool hasMin = false, hasMax = false;
    float min = 0.0f, max = 0.0f;
    bool any() const { return hasMin || hasMax; }
};

// The stops of one seat, and the frame they live in.
struct Stops
{
    AxisStops axis[2];             // [0] yaw (wraps), [1] pitch
    bool rebased = false;          // case b: the record holds R^-1 of the world aim; `frame` is R
    seat_frame::Frame frame{};
    bool any() const { return axis[0].any() || axis[1].any(); }
};

// The seat-limit latch's stops: an axis it read this update (`axes`) and holds latched at a limit. side +1: the limit
// is a maximum (we were beyond it on the high side); -1: a minimum. A non-finite limit adds nothing.
inline void AddLatch(Stops& s, const aim_write::LimitLatch& latch, const bool axes[2])
{
    for (int a = 0; a < 2; ++a)
    {
        if (!axes[a] || !latch.latched[a] || !std::isfinite(latch.limit[a]))
            continue;
        if (latch.side[a] > 0.0f)
        {
            s.axis[a].hasMax = true;
            s.axis[a].max = latch.limit[a];
        }
        else if (latch.side[a] < 0.0f)
        {
            s.axis[a].hasMin = true;
            s.axis[a].min = latch.limit[a];
        }
    }
}

// An exact interval the record is clamped to (Halo 2's eased pitch limits). Ignored unless finite and ordered.
inline void AddInterval(Stops& s, int axis, float lo, float hi)
{
    if (axis < 0 || axis > 1 || !std::isfinite(lo) || !std::isfinite(hi) || lo > hi)
        return;
    s.axis[axis].hasMin = s.axis[axis].hasMax = true;
    s.axis[axis].min = lo;
    s.axis[axis].max = hi;
}

// THE RECORD'S OWN EASED PITCH LIMITS, Halo 3 and ODST. The accumulator (Halo 3 0xF7DF0, ODST 0x113AB8)
// eases two record fields toward the seat's pitch range every update and clamps the record's pitch to them -- in Halo
// 3 at 0xF86F1..0xF8747: `movss [rec+LO], xmm1` (the eased low), `movss xmm1, [rec+HI]` ... `movss [rec+HI], xmm1`,
// then `addss xmm2, [rec+PITCH]` (pitch + this update's delta), `maxss xmm2, [rec+LO]`, the min against HI, and
// `movss [rec+PITCH], xmm2`. A look-steered driver's pitch is not latched (the accumulator levels it as the vehicle
// moves, which the latch would read as a stop), so these two fields, read after the update, ARE that driver's pitch
// stops: exact, moving with the autolevel, never a false latch. The offsets are DECODED from the matched clamp (each
// read twice, as the code reads them) and must name the record's own pitch, two adjacent floats, inside the record.
struct PitchLimitSite
{
    int32_t loStore = 0;     // movss [rec+LO], xmm1
    int32_t hiLoad = 0;      // movss xmm1, [rec+HI]
    int32_t hiStore = 0;     // movss [rec+HI], xmm1
    int32_t pitchAdd = 0;    // addss xmm2, [rec+PITCH]
    int32_t loMax = 0;       // maxss xmm2, [rec+LO]
    int32_t pitchStore = 0;  // movss [rec+PITCH], xmm2
};

// The LO offset (HI is LO + 4), or 0 with `why` naming the first disagreement. `angles`: the record's desired-angle
// pair (yaw, then pitch at +4); `stride`: the per-user record's size. Offsets are from the drive's record pointer
// (globals + user * stride), which sits a header before the user's own block -- so they may exceed the stride (Halo 3
// +0x190 with a 0x110 stride) -- but from the angles to the high limit must fit inside one user's block.
inline uint32_t ResolvePitchLimits(const PitchLimitSite& s, int32_t angles, int32_t stride, const char*& why)
{
    why = nullptr;
    if (s.pitchAdd != angles + 4 || s.pitchStore != angles + 4)
        why = "the clamp does not read and write the record's own pitch";
    else if (s.loStore != s.loMax)
        why = "the clamp's low bound is not the eased low it stores";
    else if (s.hiLoad != s.hiStore)
        why = "the eased high is not read and written at one place";
    else if (s.hiLoad != s.loStore + 4)
        why = "the eased limits are not two adjacent floats";
    else if (s.loStore <= angles + 4 || s.hiLoad + 4 - angles > stride || (s.loStore & 3) != 0)
        why = "the eased limits are not inside the record";
    return why ? 0u : static_cast<uint32_t>(s.loStore);
}

// A pitch interval as read off a live record: finite, ordered, and within the pole (a seat's range never reaches past
// it). Anything else is no stop (the reticle is not clamped from it).
inline bool RecordPitchInterval(float lo, float hi)
{
    return std::isfinite(lo) && std::isfinite(hi) && lo <= hi && lo >= -1.6f && hi <= 1.6f;
}

// One axis: `t` held inside its stops. `edge` receives how far past the stop t was (radians, >= 0; 0 inside). Yaw
// differences wrap (remainder over 2pi); with both stops the nearer violated one wins.
inline float ClampAxis(bool wraps, const AxisStops& s, float t, float& edge)
{
    edge = 0.0f;
    const auto diff = [wraps](float a, float b) {
        return wraps ? std::remainder(a - b, aim_write::kTwoPi) : a - b;
    };
    const float overMax = s.hasMax ? diff(t, s.max) : 0.0f;   // > 0: past the maximum
    const float underMin = s.hasMin ? diff(s.min, t) : 0.0f;  // > 0: past the minimum
    const bool pastMax = s.hasMax && overMax > 0.0f;
    const bool pastMin = s.hasMin && underMin > 0.0f;
    if (pastMax && (!pastMin || overMax <= underMin))
    {
        edge = overMax;
        return s.max;
    }
    if (pastMin)
    {
        edge = underMin;
        return s.min;
    }
    return t;
}

struct Result
{
    bool clamped[2]{};          // the axis was past its stop
    float edgeDeg[2]{};         // how far past, degrees of the record angle (0 inside)
    float worldYaw = 0.0f;      // the aim the game will hold, world angles (the target itself when nothing clamps)
    float worldPitch = 0.0f;
    float dir[3]{};             // unit world direction of it
    float overshootDeg = 0.0f;  // the angle between the target and that aim (exactly 0 when nothing clamps)
    bool any() const { return clamped[0] || clamped[1]; }
};

// THE CLAMP. `worldYaw/worldPitch`: the aim the hand asks for (what the drive writes), world angles. In case b it goes
// into the record frame (R^-1), each axis is held inside its stops there, and the result comes back (R). `fallbackYaw`:
// the record's current yaw, kept when the aim runs along the record's up (seat_frame::RecordFromWorld). False when
// the angles or the frame do not resolve (the caller then draws no clamp).
inline bool Clamp(const Stops& s, float worldYaw, float worldPitch, float fallbackYaw, Result& out)
{
    if (!std::isfinite(worldYaw) || !std::isfinite(worldPitch))
        return false;
    Result r{};
    float recordYaw = worldYaw, recordPitch = worldPitch;
    if (s.rebased)
    {
        seat_frame::Angles a{};
        if (!seat_frame::RecordFromWorld(s.frame, worldYaw, worldPitch, fallbackYaw, a))
            return false;
        recordYaw = a.yaw;
        recordPitch = a.pitch;
    }
    float edge[2]{};
    const float heldYaw = ClampAxis(true, s.axis[0], recordYaw, edge[0]);
    const float heldPitch = ClampAxis(false, s.axis[1], recordPitch, edge[1]);
    for (int a = 0; a < 2; ++a)
    {
        r.clamped[a] = edge[a] > 0.0f;
        r.edgeDeg[a] = edge[a] * kRadToDeg;
    }
    if (!r.any())
    {
        // Nothing to clamp: the target exactly, never a round trip through R.
        r.worldYaw = worldYaw;
        r.worldPitch = worldPitch;
        seat_frame::Forward(worldYaw, worldPitch, r.dir);
        out = r;
        return true;
    }
    float worldOutYaw = heldYaw, worldOutPitch = heldPitch;
    if (s.rebased)
    {
        seat_frame::Angles w{};
        if (!seat_frame::WorldFromRecord(s.frame, heldYaw, heldPitch, w))
            return false;
        worldOutYaw = w.yaw;
        worldOutPitch = w.pitch;
    }
    float target[3]{};
    seat_frame::Forward(worldYaw, worldPitch, target);
    seat_frame::Forward(worldOutYaw, worldOutPitch, r.dir);
    const float separation = seat_frame::SeparationDeg(target, r.dir);
    if (!std::isfinite(separation) || !std::isfinite(worldOutYaw) || !std::isfinite(worldOutPitch))
        return false;
    r.worldYaw = worldOutYaw;
    r.worldPitch = worldOutPitch;
    r.overshootDeg = separation;
    out = r;
    return true;
}

// THE GATE: whether this poll's reticle is clamped.
//  * Stops are about ONE seat (`key`): another seat forgets everything.
//  * A fresh publication replaces the held stops (fresh with no stop: the latch let go, the hand is back inside).
//    With no fresh publication, the last stops are kept for `hold_ms` while the reticle is clamped -- a latch is
//    re-primed for an update or two after any update that did not end in a seat write, and the reticle must not
//    flash past the stop and back for that -- and dropped otherwise.
//  * HYSTERESIS: the reticle clamps once the aim is past the stop by more than `engage_deg`, and stays clamped until
//    it is back inside (overshoot 0), so a hand resting on the edge never flickers between the two.
struct Gate
{
    float engage_deg = 0.5f;
    uint64_t hold_ms = 150;

    bool engaged = false;
    bool have_key = false;
    uint32_t key = 0;
    bool have_held = false;
    Stops held{};
    uint64_t held_at_ms = 0;

    void reset()
    {
        engaged = false;
        have_key = false;
        key = 0;
        have_held = false;
        held = Stops{};
        held_at_ms = 0;
    }

    // This poll's stops for seat `k`: `fresh` says `s` is a publication from within the caller's freshness window.
    // Writes the stops to clamp against into `use` and returns true, or false when there are none.
    bool stops_for(bool fresh, const Stops& s, uint32_t k, uint64_t now_ms, Stops& use)
    {
        if (!have_key || k != key)
        {
            reset();
            have_key = true;
            key = k;
        }
        if (fresh)
        {
            held = s;
            held_at_ms = now_ms;
            have_held = true;
        }
        else if (!have_held || !engaged || now_ms < held_at_ms || now_ms - held_at_ms > hold_ms)
        {
            have_held = false;
        }
        if (!have_held || !held.any())
            return false;
        use = held;
        return true;
    }

    // `have`: this poll clamped against stops (Clamp succeeded); `overshoot_deg`: its result's overshoot.
    bool decide(bool have, float overshoot_deg)
    {
        if (!have || !std::isfinite(overshoot_deg))
        {
            engaged = false;
            return false;
        }
        engaged = engaged ? overshoot_deg > 0.0f : overshoot_deg > engage_deg;
        return engaged;
    }

    // Not seated, another title, the feature off: nothing carries into the next seat.
    void leave() { reset(); }
};

// The witness's per-poll account (dev line "SEAT LIMIT <title>: reticle clamped N polls (yaw edge Y, pitch edge P),
// max overshoot X deg"). Plain counters: the caller owns the thread.
struct Tally
{
    uint32_t polls = 0, yawEdge = 0, pitchEdge = 0;
    float maxOvershootDeg = 0.0f;

    void note(const Result& r)
    {
        ++polls;
        if (r.clamped[0])
            ++yawEdge;
        if (r.clamped[1])
            ++pitchEdge;
        if (std::isfinite(r.overshootDeg) && r.overshootDeg > maxOvershootDeg)
            maxOvershootDeg = r.overshootDeg;
    }
};

// Direction-only drawing (a title with no traced seat reticle): the reticle keeps its own origin and turns by what the
// clamp did to the target -- the world yaw / pitch DELTA mapped into VR yaw / pitch with the title's signs -- so it is
// continuous with the unclamped reticle at the edge. `vrYaw/vrPitch` in and out (the reticle's own convention).
inline bool TurnVrByClamp(float targetYaw, float targetPitch, const Result& r, float yawSign, float pitchSign,
                          float& vrYaw, float& vrPitch)
{
    if (!std::isfinite(vrYaw) || !std::isfinite(vrPitch) || !std::isfinite(yawSign) || !std::isfinite(pitchSign))
        return false;
    const float dYaw = std::remainder(r.worldYaw - targetYaw, aim_write::kTwoPi);
    const float dPitch = r.worldPitch - targetPitch;
    if (!std::isfinite(dYaw) || !std::isfinite(dPitch))
        return false;
    vrYaw += yawSign * dYaw;
    vrPitch += pitchSign * dPitch;
    const float limit = 1.5707963f;
    if (vrPitch > limit)
        vrPitch = limit;
    else if (vrPitch < -limit)
        vrPitch = -limit;
    return true;
}

} // namespace seat_limit
