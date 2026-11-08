#pragma once

// SEAT DRIVE POLICY -- which axes of a vehicle seat's aim the direct-drive hook may WRITE, rather than
// leave to the stick servo (or to the steering author, for a driver whose stick or wheel steers).
//
// Every MCC title keeps a seat's aim in the same player_control record, updated by the same accumulator
// as on foot; the seat's limits are applied inside that accumulator (mapped for Halo 3, ODST,
// Halo 4, Halo 2 and Reach). So a seat can be written exactly like on foot, axis by axis, once the policy
// below says the hand owns that axis. Reach's per-seat rules (Game_ReachDirectDriveAimWhy) are the model:
//   * a passenger, a gunner, a turret, or a driver whose vehicle does NOT steer by the look (a tank: the
//     look aims the turret) -- the hand owns both axes;
//   * a driver whose vehicle steers toward the look (Warthog, Ghost, Banshee...) -- the hand owns both axes
//     only at level 2 ("drivers aim with the hand"); level 1 leaves that seat on the servo;
//   * the same driver while the stick or wheel steers (vehicle_view_follow): the heading is the steering
//     author's, never written; the climb/dive (pitch) still follows the hand, unless both hands are on the
//     wheel, which aims nothing;
//   * an unidentified seat, or a seat whose first-person view is not ours: nothing is written.
// What the policy allows is not yet what the hook writes: each seat stint still proves, axis by axis, that
// its record holds the game's aim before the first write (legacy_direct_drive.inl).
//
// Zero engine dependencies; unit-tested in tests/seat_drive_policy_tests.cpp.

#include <cmath>

namespace seat_drive {

struct Seat {
    bool identified = false;        // the vehicle and seat role are known
    bool view_owned = false;        // our first-person seat view is up (the intent ray is the hand's)
    bool driver = false;            // the seat that drives the vehicle
    bool look_steered = false;      // the vehicle turns toward the driver's aim
    bool authors_steering = false;  // the stick or wheel steers this driver now (vehicle_view_follow)
    bool wheel = false;             // both hands are on the wheel: the hands aim nothing
};

struct Axes {
    bool yaw = false;
    bool pitch = false;
    bool any() const { return yaw || pitch; }
};

// level: reach_vehicle_aim (0 = every seat on the servo, 1 = all but look-steered drivers, 2 = all).
// seat_drive_on: seat_direct_drive.
inline Axes Decide(const Seat& s, int level, bool seat_drive_on)
{
    Axes a;
    if (!seat_drive_on || level <= 0 || !s.identified || !s.view_owned)
        return a;
    if (!s.driver || !s.look_steered) {
        a.yaw = a.pitch = true;
        return a;
    }
    if (level < 2)
        return a;
    if (s.authors_steering) {
        a.pitch = !s.wheel;
        return a;
    }
    a.yaw = a.pitch = true;
    return a;
}

// Whether the seat-limit latch may read the game's pitch moves as a limit. A look-steered driver's pitch is
// levelled by the accumulator itself as the vehicle moves (autolevel, speed-scaled), which the latch would
// take for a limit and hold the aim short. A tank driver's look aims a turret with a real pitch stop, like a
// gunner's (Reach latches those too): it latches, as every other seat does.
inline bool PitchLatches(const Seat& s) { return !s.driver || !s.look_steered; }

// One seat stint, as the hook sees it: a key that changes when the occupant changes seat or vehicle, so a
// check passed in one seat is never carried into the next.
inline unsigned SeatKey(int parent_handle, int seat_index, bool mounted)
{
    return static_cast<unsigned>(parent_handle) * 2654435761u ^ (static_cast<unsigned>(seat_index + 1) << 1) ^
           (mounted ? 1u : 0u);
}

// PER-AXIS SEAT CHECK -- each seat stint proves, axis by axis, that its record holds the aim the game
// shows before that axis is written. The record and the published aim are read on different clocks, so
// while either is moving -- a vehicle turning, flight, a rate-capped barrel slewing, an impulse under fire --
// the gap between them includes the motion since the last sample. So the tolerance GROWS WITH THAT MOTION
// (`motion` = the larger of how far the aim and the record moved on that axis since the last judged sample):
//   * AGREE when the gap is under tol + motion: counts, moving or still.
//   * DISAGREE when the gap is over tol + 3 * motion: resets that axis's count.
//   * Between the two, or when either moved more than `max_motion` in one sample (a teleport, a seat snap, a
//     big hit), the sample is no evidence: it neither counts nor resets.
// A record that really holds another aim -- a Halo 3 driver+gunner's pitch ~13 deg off the gun -- disagrees
// at rest and in motion alike (13 deg is far past tol + 3 * any per-sample motion under max_motion), so it
// never passes. Do NOT restrict evidence to SETTLED samples (under 0.1 deg of motion): that keeps natural
// motion from resetting the check, but a flying Banshee is never settled, so the check would barely ever
// pass and the seat would stay on the stick servo -- natural motion switching direct drive off by another route.
//   * Yaw passes on `needed` agreeing samples with no disagreement between them.
//   * Pitch passes the same way, never before yaw. Its BUDGET counts evidence time after yaw passed
//     (`budget_ms`); when it runs out the pitch stays on the servo, and the check RETRIES `retry_ms` of
//     updates later. A pitch that agreed all along passes the moment yaw does.
// Time counts only while the game is updating (a gap -- a pause menu -- adds at most `gap_ms`). A REPEATED
// aim sample (the sim ticking faster than the aim is published), a stale one or a non-finite one is no
// evidence: it neither counts nor resets.
struct StintCheck {
    float tol = 0.03f;          // 1.7 deg
    float max_motion = 0.1f;    // 5.7 deg in one sample: more is a jump, not evidence
    unsigned needed = 30;
    unsigned long long budget_ms = 3000;   // pitch evidence time after yaw passed
    unsigned long long retry_ms = 5000;    // after a failed pitch check, of updating time
    unsigned long long gap_ms = 100;

    bool yaw_ok = false, pitch_ok = false, pitch_failed = false;
    unsigned yaw_count = 0, pitch_count = 0, pitch_fails = 0;
    unsigned long long elapsed_ms = 0, last_ms = 0, yaw_ok_at_ms = 0, pitch_settled_ms = 0, pitch_failed_at_ms = 0;
    bool have_last = false;
    float last_aim_yaw = 0.0f, last_aim_pitch = 0.0f, last_native_yaw = 0.0f, last_native_pitch = 0.0f;

    void reset()
    {
        yaw_ok = pitch_ok = pitch_failed = false;
        yaw_count = pitch_count = pitch_fails = 0;
        elapsed_ms = last_ms = yaw_ok_at_ms = pitch_settled_ms = pitch_failed_at_ms = 0;
        have_last = false;
        last_aim_yaw = last_aim_pitch = last_native_yaw = last_native_pitch = 0.0f;
    }

    // How many consecutive judged samples on this axis (0 yaw, 1 pitch) have agreed: `needed` once proven, 0
    // while a failed pitch waits for its retry. A disagreement resets it.
    unsigned agreeing(int axis) const
    {
        if (axis == 0)
            return yaw_ok ? needed : yaw_count;
        return pitch_ok ? needed : pitch_failed ? 0u : pitch_count;
    }

    static float wrap(float a) { return std::remainder(a, 6.28318531f); }

    // One update of the stint. `fresh`: the aim was published recently. `new_sample`: the aim is not the one
    // the previous update already judged. Yaw differences wrap.
    void step(bool fresh, float native_yaw, float native_pitch, float aim_yaw, float aim_pitch,
              unsigned long long now_ms, bool new_sample = true)
    {
        unsigned long long dt = 0;
        if (last_ms && now_ms >= last_ms)
            dt = (now_ms - last_ms) < gap_ms ? (now_ms - last_ms) : gap_ms;
        elapsed_ms += dt;
        last_ms = now_ms ? now_ms : 1;
        const bool finite = fresh && std::isfinite(native_yaw) && std::isfinite(native_pitch) &&
                            std::isfinite(aim_yaw) && std::isfinite(aim_pitch);
        const bool judged = finite && new_sample && have_last;
        // Evidence on one axis: +1 agree, -1 disagree, 0 none (see the comment above the struct).
        const auto evidence = [&](float gap, float aim_moved, float record_moved) {
            const float motion = aim_moved > record_moved ? aim_moved : record_moved;
            if (!judged || !(motion <= max_motion)) return 0;
            if (gap < tol + motion) return 1;
            if (gap > tol + 3.0f * motion) return -1;
            return 0;
        };
        const int yaw_ev = evidence(std::fabs(wrap(native_yaw - aim_yaw)), std::fabs(wrap(aim_yaw - last_aim_yaw)),
                                    std::fabs(wrap(native_yaw - last_native_yaw)));
        const int pitch_ev = evidence(std::fabs(native_pitch - aim_pitch), std::fabs(aim_pitch - last_aim_pitch),
                                      std::fabs(native_pitch - last_native_pitch));
        if (!yaw_ok && yaw_ev) {
            yaw_count = yaw_ev > 0 ? yaw_count + 1 : 0;
            if (yaw_count >= needed) {
                yaw_ok = true;
                yaw_ok_at_ms = elapsed_ms;
            }
        }
        if (pitch_failed && elapsed_ms - pitch_failed_at_ms >= retry_ms) {
            pitch_failed = false;   // a fresh attempt: the count and the budget start again
            pitch_count = 0;
            pitch_settled_ms = 0;
        }
        if (!pitch_ok && !pitch_failed) {
            if (pitch_ev) {
                pitch_count = pitch_ev > 0 ? pitch_count + 1 : 0;
                if (yaw_ok) pitch_settled_ms += dt;   // evidence time (the name predates the motion rule)
            }
            if (yaw_ok && pitch_count >= needed) pitch_ok = true;
            else if (yaw_ok && pitch_settled_ms > budget_ms) {
                pitch_failed = true;
                pitch_failed_at_ms = elapsed_ms;
                ++pitch_fails;
            }
        }
        if (finite && new_sample) {
            have_last = true;
            last_aim_yaw = aim_yaw;
            last_aim_pitch = aim_pitch;
            last_native_yaw = native_yaw;
            last_native_pitch = native_pitch;
        }
    }
};

// TWO REFERENCES, ONE STINT (gun-slaved seats, gun_slaved_seat_logic.h). In a seat whose first-person camera is
// the gun (seat flag bit 7), the camera's forward -- `camera`, the reference every seat is checked against --
// is a gun that slews toward the record at its own rate, so it agrees with the record only once the gun has caught
// up. `occupant` runs the same check against the occupant's own aim vector, which the record drives directly; it is
// active only in such a seat with that vector bound. An axis passes when EITHER check passes it, so a seat is never
// slower to prove than by the camera check alone (fail closed to it: it runs unchanged), and a
// gun-slaved seat proves as soon as the record agrees with what it drives. Each check keeps its own counts and
// budget; the pitch is on the servo until its retry only when every active check has failed it. An axis whose
// occupant proof the outcome guard REVOKED (gun_slaved::OffsetWatch: the gun came to rest off the record) is the
// camera check's alone again for the rest of the stint.
struct StintVerdict
{
    bool yaw_ok = false, pitch_ok = false, pitch_failed = false;
    bool yaw_by_occupant = false, pitch_by_occupant = false;   // proven by the occupant's check alone
};

inline StintVerdict CombineStints(const StintCheck& camera, const StintCheck& occupant, bool occupantActive,
                                  bool yawRevoked = false, bool pitchRevoked = false)
{
    StintVerdict v;
    const bool occYaw = occupantActive && !yawRevoked && occupant.yaw_ok;
    const bool occPitch = occupantActive && !pitchRevoked && occupant.pitch_ok;
    v.yaw_ok = camera.yaw_ok || occYaw;
    v.pitch_ok = v.yaw_ok && (camera.pitch_ok || occPitch);
    v.pitch_failed =
        !v.pitch_ok && camera.pitch_failed && (!occupantActive || pitchRevoked || occupant.pitch_failed);
    v.yaw_by_occupant = occYaw && !camera.yaw_ok;
    v.pitch_by_occupant = v.pitch_ok && occPitch && !camera.pitch_ok;
    return v;
}

// OUTCOME WITNESS (dev lines only). A written seat's aim can still fall short downstream of
// the record (a gun's rate cap, an aiming clip), where the seat-limit latch cannot see it. While the game's
// aim misses the written target by more than `enter` for `hold_ms` the gate reads `honest` (a latch hold at
// once), and it clears once the miss is under `exit`. It must not move the reticle onto the game's aim or
// withhold the converged claim: those misses are natural limits, and natural limits never
// switch convergence off (converge_policy.h). Game_ComputeAimStick feeds it only to COUNT the miss for the
// dev witness (outcomeMiss, yawShortH4); no decision reads it.
struct OutcomeGate {
    float enter = 0.035f;       // 2 deg
    float exit = 0.0175f;       // 1 deg
    unsigned long long hold_ms = 120;

    bool honest = false;
    unsigned long long since_ms = 0;

    void reset() { honest = false; since_ms = 0; }

    bool step(float miss_rad, bool latch_limited, unsigned long long now_ms)
    {
        if (latch_limited || !std::isfinite(miss_rad)) {
            honest = true;
            since_ms = 0;
            return honest;
        }
        if (honest) {
            if (miss_rad < exit) {
                honest = false;
                since_ms = 0;
            }
            return honest;
        }
        if (miss_rad <= enter) {
            since_ms = 0;
            return honest;
        }
        if (!since_ms) since_ms = now_ms ? now_ms : 1;
        if (now_ms >= since_ms && now_ms - since_ms >= hold_ms) honest = true;
        return honest;
    }
};

} // namespace seat_drive
