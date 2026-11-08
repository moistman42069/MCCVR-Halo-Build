#pragma once

// AIM WRITE -- engine-free helpers for the STATE-WRITE actuation rung (Actuation::WriteState).
//
// A state write replaces the game's own control angles every tick, so it has failure modes a stick
// loop does not: a yaw written on a different 2*pi branch than the game's own value, a pitch past the
// game's clamp, and -- the dangerous one -- writing through an address that was never proven, or has
// stopped being right (level change, reload). These helpers hold the game-agnostic half of those
// guards, so each provider only supplies facts (angles, "does the record agree with the aim?", "did
// the writer see a different record?") and the policy is one tested body of code.
//
// Zero engine/game dependencies; unit-tested in tests/aim_solve_tests.cpp alongside aim_solve_logic.h.

#include <cmath>
#include <cstdint>

namespace aim_write {

constexpr float kTwoPi = 6.28318530717958647692f;

// Put `target` (radians) on the same 2*pi branch as `current`, the value the game holds now. A write
// that jumps by 2*pi between ticks is the same direction to the sim, but anything interpolating
// between snapshots renders it as a full spin. Non-finite `current` -> `target` unchanged.
inline float unwrap_to_branch(float target, float current) {
    if (!std::isfinite(current)) return target;
    return current + std::remainder(target - current, kTwoPi);
}

// Put a yaw into [0, 2*pi), inside the range every MCC title keeps its control yaw in (Halo 3, ODST, Halo 4
// [0, 2*pi); Reach and Halo 2 [0, 2*pi]). A branch-matched
// write (unwrap_to_branch) can fall up to half a turn outside it when the heading crosses the seam, and
// those titles pack the yaw into their per-tick player action with a 13-bit quantizer over [0, 2*pi]
// that neither clamps nor masks: a yaw of 2*pi + 0.0004 or -0.0004 overflows into the action's button
// field and reads as an action-button press -- in a vehicle, EXIT SEAT (halo3 0x4EAD4 / 0xA3554 /
// 0x110050 -> 0xE7C68 -> 0xE82CB -> unit action 0xD). Halo 4 and Reach clamp instead (a one-update nudge to
// 0 or 2*pi); Halo 2 masks. The game's own record jumps by 2*pi at the same
// seam, so this adds no jump the game does not already make. Non-finite -> unchanged.
inline float wrap_0_2pi(float a) {
    if (!std::isfinite(a)) return a;
    a = std::fmod(a, kTwoPi);
    if (a < 0.0f) a += kTwoPi;
    if (!(a < kTwoPi) || !(a >= 0.0f)) a = 0.0f;   // fmod/add rounding can land exactly on 2*pi
    return a;
}

// Keep a written pitch inside the game's own clamp (radians).
inline float clamp_pitch(float pitch, float limit) {
    return pitch < -limit ? -limit : (pitch > limit ? limit : pitch);
}

// ARM GATE -- never write through an address that has not been proven, and stop the moment it may
// be wrong. The provider feeds one step per tick:
//   agree     -- this tick's ValueAgreement: the candidate record holds the SAME angles the game's
//                derived aim shows (only consulted while disarmed; the aim is derived from the record
//                every tick, so a wrong address cannot keep agreeing);
//   record    -- WHICH record agreed (its address; 0 = none). A streak must be about ONE record: if the
//                candidate changes mid-streak, the count restarts on the new one;
//   mismatch  -- the WRITER's cumulative count of ticks where it resolved a different record than the
//                one armed (a level change, a checkpoint reload, a second instance);
//   now_ms    -- a monotonic clock.
// Policy: arm after `agree_needed` consecutive agreements about the same record (then `record` holds
// what was proven); while armed, ANY new mismatch disarms at once and backs off `backoff_ms` before
// re-validating; after `max_disarms` disarms, give up for the session (the two ways of locating the
// record disagree by design on this build -- fail closed and say so). A session ends when the hook is
// reinstalled or torn down (reset_session), never at the end of the process. This is OUR code's fallback:
// nothing natural in the game disarms it.
struct ArmGate {
    int      agree_needed = 3;
    uint64_t backoff_ms   = 5000;
    int      max_disarms  = 3;

    bool     armed      = false;
    bool     given_up   = false;
    int      streak     = 0;
    int      disarms    = 0;
    uint64_t record     = 0;    // the record the current streak -- or the arm -- is about
    unsigned seen_mismatch = 0;
    uint64_t retry_at_ms   = 0;

    enum class Event { None, Armed, Disarmed, GaveUp };

    // True when this tick's evidence can change anything (disarmed, not given up, out of backoff), so
    // the caller can skip gathering it -- evidence is usually the expensive half.
    bool wants_evidence(uint64_t now_ms) const { return !armed && !given_up && now_ms >= retry_at_ms; }

    Event step(bool agree, uint64_t candidate, unsigned mismatch, uint64_t now_ms) {
        if (given_up) return Event::None;
        if (armed) {
            if (mismatch != seen_mismatch) {
                seen_mismatch = mismatch;
                armed = false;
                streak = 0;
                ++disarms;
                retry_at_ms = now_ms + backoff_ms;
                if (disarms >= max_disarms) { given_up = true; return Event::GaveUp; }
                return Event::Disarmed;
            }
            return Event::None;
        }
        seen_mismatch = mismatch;   // mismatches seen while disarmed are not the armed record's fault
        if (now_ms < retry_at_ms) return Event::None;
        if (!agree || candidate == 0) { streak = 0; return Event::None; }
        if (candidate != record) { record = candidate; streak = 0; }
        if (++streak < agree_needed) return Event::None;
        armed = true;
        streak = 0;
        return Event::Armed;
    }

    // Leave the armed state without counting a failure (the mode was switched off, the provider stopped
    // wanting a state write). A later re-arm must re-prove the address from scratch.
    void release() { armed = false; streak = 0; record = 0; }

    // A NEW SESSION: the hook was reinstalled or torn down (a new title module, a reload), so the record must
    // be proven from scratch AND the failures counted against the old one no longer apply. "Give up for the
    // session" means the session the disarms happened in, never the rest of the process: without this, three
    // disarms in one level left direct drive off until MCC itself restarted. Returns whether a give-up was
    // cleared, so the caller can say so.
    bool reset_session() {
        const bool cleared = given_up;
        release();
        given_up = false;
        disarms = 0;
        retry_at_ms = 0;
        return cleared;
    }
};

// SEAT LIMITS -- keep a state write inside the limits the game clamps the record to.
//
// The write lands AFTER the game's own update of the record, and that update is where a seat's limits
// are applied (a turret's pitch stop, a passenger's cone). Written blindly, the aim goes straight past
// them: a gun that cannot point below -20 deg is told to aim at -35. The game shows where the limit is
// without being asked: while the write owns the aim the stick is neutral, so the game's update leaves
// last tick's written value alone UNLESS a limit moved it. Per axis (yaw, pitch; radians):
//   * last tick's write came back moved by more than `eps`: latch that value as the limit, on the side
//     we were beyond, and restart the probe step;
//   * latched, and the target is still past the limit: write the limit plus a probe step toward the
//     target. A static limit clamps the probe straight back, so the aim sits one small step past it
//     (never a flicker between the limit and the target). A probe that comes back unmoved means the
//     limit has moved out (a cone turning with the vehicle), or the latch came from a one-off snap: the
//     step doubles, so the aim catches up with the target in a few ticks;
//   * the target comes back inside the limit: release.
// apply() returns true while an axis is held short of its target by more than the probe step -- the aim
// is limited. That shapes the WRITE only: callers record it for their logs and the defensive muzzle-redirect
// refusal, and it never withdraws a converged claim (converge_policy.h: a seat's stop is natural, the game
// shows it as flat Halo does). Yaw differences wrap.
// `enabled` (optional, per axis): an axis the caller does not latch this tick -- it is not written, or it
// is written as is -- gives back `native` in `out`, forgets its latch, and reads no limit on the next tick
// (what the game did to it meanwhile was not a clamp of ours). Both axes on is exactly the old behaviour.
struct LimitLatch {
    float eps      = 0.0044f;   // 0.25 deg: the game moving our value by less is not a clamp
    float probe    = 0.0087f;   // 0.5 deg: the first step past a latched limit
    float max_step = 1.0f;      // cap on the doubling probe

    bool  have_last[2]{};       // per axis: last tick wrote this axis through the latch
    float last[2]{};
    bool  latched[2]{};
    float limit[2]{};
    float side[2]{};            // +1: the limit is a maximum; -1: a minimum
    float step[2]{};
    bool  skipped[2]{};         // this tick's apply() left the axis alone: wrote() does not record it

    // Nothing written last tick (the write stopped, or it is not a seat): forget everything.
    void reset() {
        have_last[0] = have_last[1] = false;
        latched[0] = latched[1] = false;
    }

    // `native`: the record after the game's own update this tick. `target`: the aim to write. Writes the
    // values to write into `out` (the caller still puts yaw on the game's branch and clamps pitch).
    bool apply(const float native[2], const float target[2], float out[2], const bool* enabled = nullptr) {
        bool limited = false;
        for (int a = 0; a < 2; ++a) {
            skipped[a] = enabled && !enabled[a];
            if (skipped[a]) {
                out[a] = native[a];
                latched[a] = false;
                have_last[a] = false;
                continue;
            }
            const auto wrap = [a](float v) { return a == 0 ? std::remainder(v, kTwoPi) : v; };
            if (have_last[a] && std::isfinite(native[a])) {
                const float moved = wrap(native[a] - last[a]);
                if (std::fabs(moved) > eps) {          // the game clamped last tick's write: that is the limit
                    latched[a] = true;
                    limit[a] = native[a];
                    side[a] = moved < 0.0f ? 1.0f : -1.0f;
                    step[a] = probe;
                } else if (latched[a]) {               // the probe held: the limit is at least that far out
                    limit[a] = last[a];
                    step[a] = std::fmin(step[a] * 2.0f, max_step);
                }
            }
            float t = target[a];
            if (latched[a]) {
                const float beyond = wrap(t - limit[a]) * side[a];
                if (!(beyond > 0.0f)) {
                    latched[a] = false;                // back inside: the target itself
                } else {
                    t = limit[a] + side[a] * std::fmin(beyond, step[a]);
                    if (beyond > step[a]) limited = true;
                }
            }
            out[a] = t;
        }
        return limited;
    }

    // What was actually written this tick (after the caller's branch and pitch handling). An axis this
    // tick's apply() skipped is not recorded -- unless `axes` (optional, per axis) names exactly the axes to
    // record: an axis written through the latch's prime, unlatched this tick, is still a value the next tick
    // can read a limit off.
    void wrote(const float written[2], const bool* axes = nullptr) {
        for (int a = 0; a < 2; ++a) {
            if (axes ? !axes[a] : skipped[a]) continue;
            last[a] = written[a];
            have_last[a] = true;
        }
    }
};

} // namespace aim_write
