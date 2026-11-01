#pragma once
// THE FREE CAMERA WITH NO PLAYER UNIT (death, a respawn wait, spectating): the pure, testable half.
//
// With no unit (dead in ODST, for one) an uncorrected aim servo keeps swirling the camera in a circle or a figure
// eight. Flying the free camera by pointing the hand where to go is wanted, so the hand KEEPS steering the camera
// while the player has no unit; what has to go is the swirl.
//
// What closes the loop. The hand's target is composed in the VR-owned frame (g_gameYawRef / g_headYawRef in
// Halo 3 / ODST, the published copies of the same pair in Reach and Halo 4): those move only with the turn stick
// and a recenter, never with the camera the servo turns, so the target itself is still. The loop that is left is
// the actuator's. The servo's stick goes into the camera's own look integrator, and ToRawStick floors every
// non-zero command at 27.5% deflection (aim_servo_logic.h): the camera hears only "stop" or "at least 27.5%",
// steps past the target, the error changes sign, and it steps back -- a limit cycle one minimum step wide. A
// turret pitch axis shows exactly that as a one-axis "wiggle"; a free camera has NO rate cap on EITHER axis, so
// both axes cycle at once, each at its own period, and two such cycles traced together are a circle or a figure
// eight. The cure is the turret's: rest hysteresis (AimServoCommand), on both axes.
//
// Two camera-derived terms could also feed the camera's motion back into the target, and are dropped while the
// player has no unit: trace-and-reaim (its eye is the engine camera's position, which a free or orbiting camera
// moves as it turns, and with no unit there is no shot to converge) and the seated re-origin (the rendered eye
// minus the engine camera). The vehicle's hull frame goes with them -- there is no seat.
//
// The signal. Each title's own proven sampler reads its local player's unit through the output-user getter on an
// engine thread and publishes Present / Absent with the time it read it. The input thread debounces it: a seat
// change can read no unit for a frame, so Absent must hold kEnterMs before the free-camera frame begins, and
// Present must hold kExitMs before it ends. Anything unknown -- no sample, a stale one, another title's -- fails
// OPEN: the aim behaves exactly as it does with a unit.
#include <cstdint>

#include "runtime_types.h"

namespace aim_no_unit
{
enum class Presence : uint8_t
{
    Unknown = 0,
    Present = 1,   // the local player's unit exists
    Absent = 2,    // the output-user getter returned no unit
};

// One sample in one 64-bit word, so a sampler thread publishes it with a single store: the sampler's clock in
// milliseconds above two presence bits.
constexpr uint64_t Pack(Presence presence, uint64_t sampleMs) noexcept
{
    return (sampleMs << 2) | static_cast<uint64_t>(presence);
}
constexpr Presence UnpackPresence(uint64_t word) noexcept
{
    const uint64_t bits = word & 3u;
    return bits == 1u ? Presence::Present : bits == 2u ? Presence::Absent : Presence::Unknown;
}
constexpr uint64_t UnpackMs(uint64_t word) noexcept
{
    return word >> 2;
}

// A seat exit, a boarding animation or a seat swap can read "no unit" for a frame or two; a death holds it for
// seconds. 200 ms is ~13 camera samples, longer than any measured transition blip and still well inside the
// death camera's first second.
constexpr uint64_t kEnterMs = 200;
// A respawn is real at once, but a lone Present inside a run of Absent is not a respawn.
constexpr uint64_t kExitMs = 60;
// A sample older than this answers nothing (the sampler stopped: a load, a pause, a teardown): fail open.
constexpr uint64_t kStaleMs = 250;

// Whether a host's screen may hold the controller because the player has no character. Read from the samples alone, on
// a thread of its own: the debounce below is stepped by the input poll that the holding of the controller cuts short,
// so it could never see the character come back. A blip of Absent must last kEnterMs; a Present ends it at once.
struct NoCharacterNow
{
    uint64_t absentSinceMs = 0;
    bool Step(Presence presence, uint64_t sampleMs, uint64_t nowMs) noexcept
    {
        const bool fresh = sampleMs != 0 && (sampleMs >= nowMs || nowMs - sampleMs <= kStaleMs);
        if (!fresh || presence != Presence::Absent)
        {
            absentSinceMs = 0;
            return false;
        }
        if (!absentSinceMs)
            absentSinceMs = sampleMs;
        return sampleMs >= absentSinceMs && sampleMs - absentSinceMs >= kEnterMs;
    }
};

enum class Edge : uint8_t
{
    None,
    Entered,           // the player has had no unit for kEnterMs: the free-camera frame begins
    Released,          // the unit is back (kExitMs): the ordinary frame resumes
    ReleasedUnknown,   // the sampler stopped answering while in the free-camera frame: fail open
};

// Input thread only (the servo's own thread). Fed the latest published sample once per aim poll.
struct Debounce
{
    bool freeCamera = false;     // the decision: the player has no unit
    bool absentRun = false;      // a run of Absent samples is open...
    uint64_t absentSinceMs = 0;  // ...since this sample
    bool presentRun = false;     // while in the free-camera frame: a run of Present samples is open...
    uint64_t presentSinceMs = 0; // ...since this sample

    void Reset() noexcept { *this = Debounce{}; }

    // `presence` and `sampleMs` from the title's latest sample, `nowMs` the consumer's clock (same clock). Runs are
    // timed on the SAMPLES' clock, so a sample read again proves nothing new: only a newer one lengthens a run.
    Edge Step(Presence presence, uint64_t sampleMs, uint64_t nowMs) noexcept
    {
        // A sample stamped a moment after `now` was read is fresh, not stale (two threads, one clock).
        const bool fresh = sampleMs != 0 && (sampleMs >= nowMs || nowMs - sampleMs <= kStaleMs);
        if (!fresh || presence == Presence::Unknown)
        {
            const bool was = freeCamera;
            Reset();
            return was ? Edge::ReleasedUnknown : Edge::None;
        }
        if (presence == Presence::Absent)
        {
            presentRun = false;
            if (!absentRun)
            {
                absentRun = true;
                absentSinceMs = sampleMs;
            }
            if (!freeCamera && sampleMs >= absentSinceMs && sampleMs - absentSinceMs >= kEnterMs)
            {
                freeCamera = true;
                return Edge::Entered;
            }
            return Edge::None;
        }
        // Present.
        absentRun = false;
        if (!freeCamera)
            return Edge::None;
        if (!presentRun)
        {
            presentRun = true;
            presentSinceMs = sampleMs;
        }
        if (sampleMs >= presentSinceMs && sampleMs - presentSinceMs >= kExitMs)
        {
            freeCamera = false;
            presentRun = false;
            return Edge::Released;
        }
        return Edge::None;
    }
};

// What the aim servo composes and actuates in each frame. The ordinary frame is exactly what shipped; the
// free-camera frame keeps the hand steering (the stick servo) and drops every term the camera's own motion can
// feed back into, plus every write that needs a unit.
struct Frame
{
    bool traceReaim = true;      // publish the intent ray and bend the aim through the traced point
    bool seatReOrigin = true;    // the seated passenger re-origin (rendered eye minus the engine camera)
    bool hullFrame = true;       // compose the aim in a followed vehicle's hull frame
    bool directDrive = true;     // publish a direct-drive setpoint (on foot or a seat): there is no record to write
    bool claimConverged = true;  // the converged mark (nothing is converged without a trace)
    bool parkingServo = false;   // the servo parks inside a rest band on both axes (rest hysteresis)
};

constexpr Frame FrameFor(bool freeCamera) noexcept
{
    Frame frame{};
    if (freeCamera)
    {
        frame.traceReaim = false;
        frame.seatReOrigin = false;
        frame.hullFrame = false;
        frame.directDrive = false;
        frame.claimConverged = false;
        frame.parkingServo = true;
    }
    return frame;
}

// Whether the servo uses the parking law (AimServoCommand) this poll: a turret seat always has, and so does the
// free camera; everywhere else the plain proportional command stands.
constexpr bool ServoParks(bool turretSeat, bool freeCamera) noexcept
{
    return turretSeat || FrameFor(freeCamera).parkingServo;
}

// The titles whose servo this reaches (Game_ComputeAimStick's own titles). Halo 2 aims by hand only in a seat
// (no seat without a unit) and Halo CE has no stick servo, so neither publishes.
constexpr bool TitlePublishes(GameTitle title) noexcept
{
    return title == GameTitle::Halo3 || title == GameTitle::Halo3ODST || title == GameTitle::HaloReach ||
           title == GameTitle::Halo4;
}
constexpr const char* TitleLabel(GameTitle title) noexcept
{
    return title == GameTitle::Halo3 ? "Halo 3"
         : title == GameTitle::Halo3ODST ? "ODST"
         : title == GameTitle::HaloReach ? "Reach"
         : title == GameTitle::Halo4 ? "Halo 4"
         : "title";
}
} // namespace aim_no_unit
