#pragma once
// GUN-SLAVED SEATS (Halo 3 / ODST seat tag flag bit 7, "first person camera slaved to gun"): the pure half.
//
// What the game does. The first-person camera function (Halo 3 0x17DAEC, ODST 0x1AC670) takes the camera's
// forward from the unit's own aim vector (Halo 3 unit+0x1AC, ODST unit+0x1A4) -- unless the unit sits in a seat
// whose tag flags carry bit 7, where it takes the position and forward from a marker on the PARENT, the gun. The
// Warthog turret's gunner seat (flags 0x00101198) is one. So in that seat the camera forward this mod reads as
// "the game's aim" (g_aimFwd) is the GUN, and the gun slews toward the aim the record asks for at its own rate
// (~125-140 deg/s on the Warthog turret, measured). Comparing the record against it -- the seat's per-axis proof,
// the arm gate, the stick servo before the proof -- compares it against a quantity that lags by design: a turret
// stint then spends 1-2 s unwritten, a short one never writes, and with the pitch left on the stick servo the servo
// closes its loop on the lagging gun, runs the record past the target, and the gun nods.
//
// What the record really drives is the OCCUPANT's own aim vector, the same one the camera reads on foot. The
// control update proves that relation itself: it re-derives the record's desired angles from exactly that vector
// (Halo 3 0xF7082, ODST 0x112C5D: yaw = atan2(y, x), pitch from z, written to the record's +0xD4 / +0xD8). So in a
// gun-slaved seat the record is proven against the occupant's aim; the gun stays the outcome (witnesses only).
//
// Everything here is engine-free and unit-tested (tests/gun_slaved_seat_tests.cpp).

#include "seat_frame_logic.h"

#include <cmath>
#include <cstdint>

namespace gun_slaved
{
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kRadToDeg = 57.2957795f;

// ---- the seat ------------------------------------------------------------------------------------------------
// Bit 7 of a seat's tag flags (H3EK unit seat flags: "first person camera slaved to gun"). Halo 3 reads it from
// the occupied seat's ORIGINAL flags (the C20 patch keeps them; it only clears bit 4), ODST from the live seat word.
constexpr uint32_t kCameraSlavedToGunBit = 1u << 7;
constexpr bool CameraSlavedToGun(uint32_t seatFlags) noexcept
{
    return (seatFlags & kCameraSlavedToGunBit) != 0;
}

// ---- the binding: where the occupant's aim vector lives ------------------------------------------------------
// Two independent consumers, each found by a unique signature, must agree on the offset: the camera function's
// forward read (with the seat stride and the bit it tests, so the bit is proven to be the one this file names)
// and the control update's reset of the record from the same vector (whose two writes must land on the record's
// own yaw / pitch, the angles the direct drive writes). The z component is read 8 bytes on in both.
struct UnitAimSites
{
    int32_t cameraForward = 0;     // movsd xmm0, [unit + X]
    int32_t cameraForwardZ = 0;    // mov eax, [unit + X + 8]
    int32_t cameraSeatStride = 0;  // imul rdx, rax, stride (the seat block's element size)
    uint8_t cameraFlagShift = 0;   // shr eax, N; test r?b, al  (N = 7)
    int32_t controlForward = 0;    // movsd xmm0, [unit + X]
    int32_t controlForwardZ = 0;   // mov eax, [unit + X + 8]
    int32_t controlPitchWrite = 0; // movss [record + P], xmm0
    int32_t controlYawWrite = 0;   // movss [record + Y], xmm0
};

// The offset, or 0 with `why` naming the first disagreement. `seatStride` and `recordAngles` are the mod's own
// proven constants (the seat block stride, the record's desired-angle pair).
inline uint32_t ResolveUnitAimOffset(const UnitAimSites& s, int32_t seatStride, int32_t recordAngles,
                                     const char*& why) noexcept
{
    why = nullptr;
    if (s.cameraFlagShift != 7)
        why = "the camera function does not test seat flag bit 7";
    else if (s.cameraSeatStride != seatStride)
        why = "the camera function's seat stride is not the seat block's";
    else if (s.cameraForwardZ != s.cameraForward + 8 || s.controlForwardZ != s.controlForward + 8)
        why = "a consumer does not read the vector's z beside its x/y";
    else if (s.cameraForward != s.controlForward)
        why = "the camera and the control update read different fields";
    else if (s.controlYawWrite != recordAngles || s.controlPitchWrite != recordAngles + 4)
        why = "the control update's reset does not write the record's desired angles";
    else if (s.cameraForward < 0x100 || s.cameraForward > 0x800 || (s.cameraForward & 3) != 0)
        why = "the offset is outside a unit's aim block";
    return why ? 0u : static_cast<uint32_t>(s.cameraForward);
}

// The game keeps the vector normalised (it asserts so). Anything else is not this vector: no sample.
inline bool AnglesFromVector(const float v[3], float& yaw, float& pitch) noexcept
{
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
        return false;
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(length > 0.95f && length < 1.05f))
        return false;
    yaw = std::atan2(v[1], v[0]);
    const float z = v[2] / length;
    pitch = std::asin(z > 1.0f ? 1.0f : (z < -1.0f ? -1.0f : z));
    return std::isfinite(yaw) && std::isfinite(pitch);
}

// The angle between two directions given as (yaw, pitch), in degrees. NaN when either is not finite.
inline float AngleBetweenDeg(float yawA, float pitchA, float yawB, float pitchB) noexcept
{
    if (!std::isfinite(yawA) || !std::isfinite(pitchA) || !std::isfinite(yawB) || !std::isfinite(pitchB))
        return std::nanf("");
    const float ca = std::cos(pitchA), cb = std::cos(pitchB);
    float dot = ca * std::cos(yawA) * cb * std::cos(yawB) + ca * std::sin(yawA) * cb * std::sin(yawB) +
                std::sin(pitchA) * std::sin(pitchB);
    dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
    return std::acos(dot) * kRadToDeg;
}

// ---- the outcome guard: lag, or a real offset? ---------------------------------------------------------------
// The occupant's aim proves that the record holds the aim the record drives. It cannot prove that the GUN then
// points there: a seat whose gun marker sat at a constant angle from the occupant's aim would pass the occupant's
// check, be written, and leave the gun off the hand -- where the camera check (failing on that offset, by design)
// keeps the axis on the servo, closing on the gun. Lag and a real offset part company when the gun comes to REST:
// lag is gone then, an offset is not. So once an axis is written on the occupant's proof alone, this watch steps on
// every new sample of that axis: with the gun and the record both at rest (each moved under `rest` since the last
// sample) a gap over `tol` counts, anything under resets, and `needed` such samples in a row REVOKE the occupant's
// proof of that axis for the rest of the stint (the camera's check alone decides it again).
// A gun at a seat's stop is not watched: the caller holds the watch off while the seat-limit latch holds the axis.
// The accumulator clamps the record to the seat's range, but on a tilted vehicle the gun's own stop is not where
// that clamp leaves the record (more than 3 deg apart, at rest, on a Warthog climbing a slope), and a watch that
// counted it revoked the proof at every stop. The caller also runs the watch only where the simulation aims the
// seat from the gun camera: elsewhere the gun is a witness, not what the record is solved for.
struct OffsetWatch
{
    float tol = 0.0524f;     // 3 deg: farther than this, a gun at rest is not lagging
    float rest = 0.00175f;   // 0.1 deg per sample: at rest
    unsigned needed = 30;
    bool wrap = false;

    bool have = false;
    float lastGun = 0.0f, lastRecord = 0.0f;
    unsigned count = 0;
    bool revoked = false;

    void reset() noexcept
    {
        have = revoked = false;
        count = 0;
        lastGun = lastRecord = 0.0f;
    }

    bool step(float record, float gun) noexcept
    {
        if (!std::isfinite(record) || !std::isfinite(gun))
        {
            have = false;
            return revoked;
        }
        const auto diff = [this](float a, float b) {
            const float d = a - b;
            return wrap ? std::remainder(d, kTwoPi) : d;
        };
        if (have && std::fabs(diff(gun, lastGun)) < rest && std::fabs(diff(record, lastRecord)) < rest)
        {
            if (std::fabs(diff(record, gun)) > tol)
            {
                if (++count >= needed)
                    revoked = true;
            }
            else
            {
                count = 0;
            }
        }
        lastGun = gun;
        lastRecord = record;
        have = true;
        return revoked;
    }
};

// ---- the stick servo's feedback ------------------------------------------------------------------------------
// Where a gun-slaved seat's axis is still steered (before its proof, or after a failed one), the servo closes on
// the occupant's aim once that aim has agreed with the record on this axis for `trust` consecutive judged samples
// of this stint (the record is what the stick moves, so this feedback has no lag). Otherwise -- no binding, no
// fresh sample, a disagreement, any other seat -- it closes on the camera's forward.
enum class Feedback : uint8_t
{
    Gun = 0,       // the camera forward (g_aimFwd); in a gun-slaved seat, the gun
    Occupant = 1,  // the occupant's own aim, proven against the record this stint
};
constexpr unsigned kServoTrustSamples = 8;
constexpr Feedback ServoFeedback(bool gunSlaved, bool occupantFresh, unsigned agreeingSamples) noexcept
{
    return gunSlaved && occupantFresh && agreeingSamples >= kServoTrustSamples ? Feedback::Occupant : Feedback::Gun;
}

// THE PITCH CHECK'S QUIET AXIS. Until a stint's pitch check passes, the pitch is steered by the stick servo, and
// the check (seat_drive::StintCheck) needs a run of samples in which the record and the occupant's aim agree. On an
// axis with no rate cap -- a Warthog turret's pitch -- the servo's smallest step swings the record past its target
// and back, the two never agree for a whole run, the check uses up its budget and fails, and the pitch then stays
// on that same servo until the retry: the gun nods up and down for as long as it takes one attempt to pass by
// luck. So while an attempt is collecting evidence the stick's pitch is held at zero: the record stands still, the
// occupant's aim settles on it, and the check passes in its own run of samples (half a second), after which the
// pitch is written directly. The hold is bounded per attempt: a record that does not agree at rest is not the
// seat's aim, and that seat goes back to the servo for the rest of the attempt, as it always has.
// `attempt` names the attempt (the check's count of failed ones): a retry is a new attempt and is held again.
inline constexpr unsigned long long kCheckHoldMs = 1200;
struct CheckHold
{
    uint32_t key = 0, attempt = 0;
    bool have = false, started = false;
    unsigned long long sinceMs = 0;

    void reset() { *this = CheckHold{}; }

    // One input poll. `fresh`: the occupant's aim publication is this seat's and recent. `checking`: its pitch
    // check is collecting evidence (neither passed nor failed). True: command no pitch this poll.
    bool step(uint32_t seatKey, bool fresh, bool checking, uint32_t attemptIndex, unsigned long long nowMs) noexcept
    {
        if (!seatKey)
        {
            reset();
            return false;
        }
        if (!have || key != seatKey || attempt != attemptIndex)
        {
            have = true;
            key = seatKey;
            attempt = attemptIndex;
            started = false;
        }
        if (!fresh || !checking)
            return false;
        if (!started)
        {
            started = true;
            sinceMs = nowMs;
        }
        return nowMs >= sinceMs && nowMs - sinceMs <= kCheckHoldMs;
    }
};

// THE PACER: a servo in a gun-slaved seat (whichever feedback it closes on) commands in PULSES. The stick moves the
// record, and the feedback answers only after the input path, the control update, the tick and -- for the gun --
// its slew: polls later. A continuous command keeps pushing through that latency, the record runs past the target,
// the feedback arrives late, the error flips sign and the loop drives back: the nod. The rest hysteresis
// (AimServoCommand) cannot catch it, because the overshoot is the latency's, not the stick's quantum (its measured
// quantum reads 0.0000 in a turret stint: the gun never moves on the poll after a command).
// So the pacer lets one poll's command through, then holds zero until the feedback has moved and come to rest (or
// `timeoutPolls` pass: a stop that never answers is asked again), and holds while the feedback is still travelling
// toward the target. It also measures what one floor-level pulse moved the feedback by, from rest -- the quantum
// AimServoObserve cannot see through the latency -- and widens the rest band to it (capped), so the last pulse
// cannot hunt around the target either. Per axis; the yaw axis wraps. Engaged only in gun-slaved seats.
struct ServoPacer
{
    float moving = 0.0009f;        // ~0.05 deg per poll: less is at rest
    unsigned timeoutPolls = 24;    // ~200 ms at 120 polls/s
    float quantumCap = 0.035f;     // 2 deg: the widest the measured quantum may make the rest band
    bool wrap = false;

    bool have = false;
    float last = 0.0f;
    bool waiting = false, moved = false, floorPulse = false;
    unsigned waited = 0;
    float pulseFrom = 0.0f;
    float quantum = 0.0f;          // radians: one floor pulse's measured effect
    unsigned holds = 0;            // polls whose non-zero command was held (the caller's dev line takes it)

    void reset() noexcept
    {
        have = waiting = moved = floorPulse = false;
        waited = 0;
        last = pulseFrom = quantum = 0.0f;
        holds = 0;
    }

    float enter(float base) const noexcept { return quantum * 0.6f > base ? quantum * 0.6f : base; }
    float exit(float base) const noexcept { return quantum * 1.5f > base ? quantum * 1.5f : base; }

    // `command`: AimServoCommand's (with enter()/exit() as its rest bands); `floorMagnitude`: the deflection the
    // stick mapping floors smaller commands to. Returns the command to issue this poll.
    float pace(float feedback, float error, float command, float floorMagnitude) noexcept
    {
        if (!std::isfinite(feedback) || !std::isfinite(error))
        {
            have = waiting = false;
            return command;   // nothing to pace against: the servo as it was
        }
        float d = have ? feedback - last : 0.0f;
        if (wrap)
            d = std::remainder(d, kTwoPi);
        last = feedback;
        have = true;
        const bool movingNow = std::fabs(d) > moving;
        if (waiting)
        {
            ++waited;
            if (movingNow)
                moved = true;
            if ((moved && !movingNow) || waited >= timeoutPolls)
            {
                waiting = false;
                if (floorPulse && moved)
                {
                    float step = feedback - pulseFrom;
                    if (wrap)
                        step = std::remainder(step, kTwoPi);
                    step = std::fabs(step);
                    if (step > quantumCap)
                        step = quantumCap;
                    // Biased low: a pulse measured with an earlier command still in flight reads too far.
                    quantum = (quantum == 0.0f || step < quantum) ? step : quantum * 0.8f + step * 0.2f;
                }
            }
            else
            {
                if (command != 0.0f)
                    ++holds;
                return 0.0f;
            }
        }
        if (command == 0.0f)
            return 0.0f;
        if (movingNow && (d > 0.0f) == (error > 0.0f))
        {
            ++holds;   // still arriving
            return 0.0f;
        }
        waiting = true;
        moved = false;
        waited = 0;
        floorPulse = std::fabs(command) <= floorMagnitude && !movingNow;
        pulseFrom = feedback;
        return command;
    }
};

// ---- the outcome: has the gun landed where it was written? --------------------------------------------------
// The engine-crosshair witness scores a turret only once the gun has settled: within `tol` of the last write for
// `hold_ms`. Each unsettled episode's length (from leaving the band to re-entering it) is the gun's settle time.
struct GunSettle
{
    float tol = 0.01745f;                 // 1 deg
    unsigned long long hold_ms = 120;

    bool inBand = false;
    bool settled = false;
    unsigned long long inSinceMs = 0, outSinceMs = 0;
    unsigned long long lastSettleMs = 0;  // the last completed episode
    bool episodeDone = false;             // a new lastSettleMs since the caller last took it

    void reset() noexcept { *this = GunSettle{tol, hold_ms}; }

    // `missRad`: the gun's angle from the nearer of the last two writes. NaN (no write to compare): not settled,
    // and no episode is timed across it.
    bool step(float missRad, unsigned long long nowMs) noexcept
    {
        if (!nowMs)
            nowMs = 1;
        if (!std::isfinite(missRad))
        {
            inBand = settled = false;
            inSinceMs = outSinceMs = 0;
            return false;
        }
        if (missRad > tol)
        {
            if (inBand || !outSinceMs)
                outSinceMs = nowMs;
            inBand = settled = false;
            inSinceMs = 0;
            return false;
        }
        if (!inBand)
        {
            inBand = true;
            inSinceMs = nowMs;
            if (outSinceMs && nowMs >= outSinceMs)
            {
                lastSettleMs = nowMs - outSinceMs;
                episodeDone = true;
            }
            outSinceMs = 0;
        }
        settled = nowMs >= inSinceMs && nowMs - inSinceMs >= hold_ms;
        return settled;
    }
};

// The gun's angular speed between successive polls (deg/s), and the peak of it.
struct SlewRate
{
    bool have = false;
    float yaw = 0.0f, pitch = 0.0f;
    unsigned long long ms = 0;
    float peakDegPerS = 0.0f;

    void reset() noexcept { *this = SlewRate{}; }

    float step(float yawRad, float pitchRad, unsigned long long nowMs) noexcept
    {
        float rate = 0.0f;
        if (!std::isfinite(yawRad) || !std::isfinite(pitchRad))
        {
            have = false;
            return rate;
        }
        if (have && nowMs > ms && nowMs - ms <= 100)
        {
            const float deg = AngleBetweenDeg(yaw, pitch, yawRad, pitchRad);
            if (std::isfinite(deg))
            {
                rate = deg * 1000.0f / static_cast<float>(nowMs - ms);
                if (rate > peakDegPerS)
                    peakDegPerS = rate;
            }
        }
        if (!have || nowMs != ms)
        {
            yaw = yawRad;
            pitch = pitchRad;
            ms = nowMs;
        }
        have = true;
        return rate;
    }
};

// THE NOD WITNESS: direction reversals of one angle (the gun's pitch), with hysteresis, and the largest swing
// between two successive turning points. A hand that nods reverses too, so the caller runs one on the hand's
// target beside it: a gun that reverses where the hand does not is the loop's own nod.
struct NodWitness
{
    float deadband = 0.0035f;   // 0.2 deg: a smaller wobble is not a reversal
    bool have = false;
    int dir = 0;                // +1 rising, -1 falling, 0 not yet moving
    float peak = 0.0f;          // the running extreme in `dir`
    bool haveTurn = false;
    float lastTurn = 0.0f;
    unsigned reversals = 0;
    float maxSwing = 0.0f;      // radians, peak to peak

    void reset() noexcept { *this = NodWitness{deadband}; }

    void step(float v) noexcept
    {
        if (!std::isfinite(v))
        {
            have = false;
            dir = 0;
            haveTurn = false;
            return;
        }
        if (!have)
        {
            have = true;
            dir = 0;
            peak = v;
            return;
        }
        if (dir == 0)
        {
            if (v - peak > deadband) { dir = 1; peak = v; }
            else if (peak - v > deadband) { dir = -1; peak = v; }
            return;
        }
        if (dir > 0)
        {
            if (v > peak) peak = v;
            else if (peak - v > deadband) { turn(); dir = -1; peak = v; }
        }
        else
        {
            if (v < peak) peak = v;
            else if (v - peak > deadband) { turn(); dir = 1; peak = v; }
        }
    }

    // The counts since the last take (the trend state is kept, so a reversal straddling a report is not lost).
    void take(unsigned& outReversals, float& outMaxSwingRad) noexcept
    {
        outReversals = reversals;
        outMaxSwingRad = maxSwing;
        reversals = 0;
        maxSwing = 0.0f;
    }

private:
    void turn() noexcept
    {
        ++reversals;
        if (haveTurn)
        {
            const float swing = std::fabs(peak - lastTurn);
            if (swing > maxSwing)
                maxSwing = swing;
        }
        lastTurn = peak;
        haveTurn = true;
    }
};

// ---- the gun's own offset from the occupant's aim --------------------------------------------------------------
// The firing assist (Halo 3 0x13BAD0) aims the shot at the crosshair of the GUN's camera -- the marker the seat's
// first-person camera rides (0x212198 -> 0x17DAEC) -- not at the occupant's aim the record drives. On the Warthog's
// gauss turret (definition 0x069C) that camera's forward sits off the occupant's aim: still samples read
// ~0.6-0.9 deg, and at each fire the assist's query sits ~0.5 deg yaw / ~1.1 deg pitch off the record. (The gun
// against the record peaks at ~2.3-2.4 deg over a 2 s window, but that is the gun's own swing with the hand still,
// not a steady offset.) The solve aims the camera's ray through
// the point, so a record written as that ray leaves the gauss's slugs ~1.2 deg off it. The shot itself is carried
// onto the point at the fire by the crosshair-assist re-aim (converge::SeatAssistReaimRefuses), whatever the offset
// at that instant; this calibration only points the gun's camera at the point while it is at rest.
//
// So the offset is LEARNED, as yaw / pitch in the turret's own frame (its object frame: a fixed rotation of the gun
// on its mount reads the same there whatever the hull's tilt): delta = angles(gun camera) - angles(occupant aim),
// sampled only while the gun has come to rest on its aim and the hand is still (no lag in it), averaged per stint
// and remembered per definition. A sample is refused by identity only (OffsetSampleRefusal) -- the occupant's aim not
// proven against the record this stint, or the seat held at its stop (the gun rests where the hand's record does
// not) -- never by its size. Then it is fed
// FORWARD, once, as a static calibration: the record is written as the camera ray through the point MINUS delta, so
// the camera -- record plus delta -- lands on the ray. Nothing closes a loop on it. Everything that read the gun as
// a stand-in for the record (the camera check, the outcome guard, the servo's gun feedback) takes the gun minus
// delta: the occupant's aim the gun implies.

// The angles of a WORLD direction (yaw, pitch) in frame `f` (the angles of F^-1 e(yaw, pitch)).
inline bool AnglesInFrame(const seat_frame::Frame& f, float yaw, float pitch, float fallbackYaw,
                          seat_frame::Angles& out) noexcept
{
    return seat_frame::RecordFromWorld(f, yaw, pitch, fallbackYaw, out);
}

// One offset sample: the gun's camera minus the occupant's aim, both world angles, as (yaw, pitch) in frame `f`, and
// the plain angle between the two directions (radians). False when either does not resolve.
inline bool OffsetInFrame(const seat_frame::Frame& f, float gunYaw, float gunPitch, float occYaw, float occPitch,
                          float& dYaw, float& dPitch, float& separationRad) noexcept
{
    seat_frame::Angles gun{}, occ{};
    if (!AnglesInFrame(f, gunYaw, gunPitch, 0.0f, gun) || !AnglesInFrame(f, occYaw, occPitch, gun.yaw, occ))
        return false;
    dYaw = std::remainder(gun.yaw - occ.yaw, kTwoPi);
    dPitch = gun.pitch - occ.pitch;
    float g[3], o[3];
    seat_frame::Forward(gunYaw, gunPitch, g);
    seat_frame::Forward(occYaw, occPitch, o);
    separationRad = seat_frame::SeparationDeg(g, o) / kRadToDeg;   // atan2: exact at small angles, unlike acos
    return std::isfinite(dYaw) && std::isfinite(dPitch) && std::isfinite(separationRad);
}

// A world direction moved by (sYaw, sPitch) in frame `f`, back in world angles. The record to write is the ray
// through the point shifted by -delta; the occupant's aim a gun implies is the gun shifted by -delta.
inline bool ShiftInFrame(const seat_frame::Frame& f, float yaw, float pitch, float sYaw, float sPitch, float& outYaw,
                         float& outPitch) noexcept
{
    seat_frame::Angles a{}, w{};
    if (!std::isfinite(sYaw) || !std::isfinite(sPitch) || !AnglesInFrame(f, yaw, pitch, 0.0f, a))
        return false;
    float p = a.pitch + sPitch;
    p = p > 1.55f ? 1.55f : (p < -1.55f ? -1.55f : p);
    if (!seat_frame::WorldFromRecord(f, a.yaw + sYaw, p, w))
        return false;
    // The yaw on the branch the input was on (never a 2pi jump in what the caller compares).
    outYaw = yaw + std::remainder(w.yaw - yaw, kTwoPi);
    outPitch = w.pitch;
    return std::isfinite(outYaw) && std::isfinite(outPitch);
}

// "The gun has come to rest and the hand is still": the gun's camera and the occupant's aim each stayed within
// `gunTol` of where they were, and the hand's target within `handTol`, for `needMs`. Any of them moving re-anchors
// the timer there. World angles (gun yaw, gun pitch, occupant yaw, occupant pitch, hand yaw, hand pitch); yaw wraps.
struct Stillness
{
    float gunTol = 0.00175f;          // 0.1 deg
    float handTol = 0.0087f;          // 0.5 deg: a held hand jitters
    unsigned long long needMs = 100;

    bool have = false;
    float anchor[6]{};
    unsigned long long sinceMs = 0;

    void reset() noexcept
    {
        have = false;
        sinceMs = 0;
    }

    // True once everything has stayed put for needMs.
    bool step(const float v[6], unsigned long long nowMs) noexcept
    {
        for (int i = 0; i < 6; ++i)
            if (!std::isfinite(v[i]))
            {
                have = false;
                return false;
            }
        bool still = have && nowMs >= sinceMs;
        for (int i = 0; still && i < 6; ++i)
        {
            const float d = (i % 2 == 0) ? std::remainder(v[i] - anchor[i], kTwoPi) : v[i] - anchor[i];
            still = std::fabs(d) <= (i < 4 ? gunTol : handTol);
        }
        if (!still)
        {
            for (int i = 0; i < 6; ++i)
                anchor[i] = v[i];
            sinceMs = nowMs;
            have = true;
            return false;
        }
        return nowMs - sinceMs >= needMs;
    }
};

// Why a still sample is not averaged in: identity and freshness only, never the sample's size. The cases a size
// limit (such as "over 5 deg") would stand for are named instead:
//   * HeldAtStop -- the seat is held at its natural stop (the drive's seat-limit latch, or the reticle clamped there):
//     the gun rests at the stop, the hand's record need not;
//   * OccupantUnproven -- the occupant's aim has not agreed with the record for the servo's trust count this stint
//     (gun_slaved::kServoTrustSamples): the field is not yet proven to be the aim the record drives (a wrong field);
//   * NonFinite -- an angle that did not resolve.
// A gun still lagging is not a refusal: Stillness never admits one (the gun and the occupant must both be at rest).
enum class OffsetRefusal : uint8_t
{
    None = 0,
    NonFinite,
    OccupantUnproven,
    HeldAtStop,
    Count
};

inline OffsetRefusal OffsetSampleRefusal(bool occupantProven, bool heldAtStop) noexcept
{
    if (heldAtStop)
        return OffsetRefusal::HeldAtStop;
    if (!occupantProven)
        return OffsetRefusal::OccupantUnproven;
    return OffsetRefusal::None;
}

// THE LEARNER: an exponential average of the offset over the samples Stillness admits. A sample refused by the caller
// (OffsetSampleRefusal) or non-finite is counted by reason and never averaged. Usable after `ready` samples -- or at
// once when seeded from the definition's last stint.
struct OffsetLearner
{
    float alpha = 0.05f;
    unsigned ready = 12;              // ~0.1 s of still samples at 120 polls/s

    bool have = false;
    float yaw = 0.0f, pitch = 0.0f;
    unsigned samples = 0, refused = 0;
    unsigned refusedBy[static_cast<unsigned>(OffsetRefusal::Count)]{};

    void reset() noexcept
    {
        have = false;
        yaw = pitch = 0.0f;
        samples = refused = 0;
        for (unsigned& n : refusedBy)
            n = 0;
    }

    void seed(float y, float p, unsigned n) noexcept
    {
        if (!std::isfinite(y) || !std::isfinite(p) || !n)
            return;
        have = true;
        yaw = y;
        pitch = p;
        samples = n < ready ? n : ready;
    }

    // True when the sample was averaged in. `why`: the caller's identity verdict on it (OffsetSampleRefusal).
    bool step(float dYaw, float dPitch, OffsetRefusal why = OffsetRefusal::None) noexcept
    {
        if (why == OffsetRefusal::None && (!std::isfinite(dYaw) || !std::isfinite(dPitch)))
            why = OffsetRefusal::NonFinite;
        if (why != OffsetRefusal::None)
        {
            if (why < OffsetRefusal::Count)
                ++refusedBy[static_cast<unsigned>(why)];
            ++refused;
            return false;
        }
        if (!have)
        {
            yaw = dYaw;
            pitch = dPitch;
            have = true;
        }
        else
        {
            yaw += alpha * (dYaw - yaw);
            pitch += alpha * (dPitch - pitch);
        }
        ++samples;
        return true;
    }

    bool usable() const noexcept { return have && samples >= ready; }
};

// The offset per gun DEFINITION (a gauss turret is a gauss turret on every Warthog), so the next stint in one starts
// calibrated. A few entries, oldest replaced; keyed with the runtime generation (a new session is a new tag set).
struct OffsetBook
{
    struct Entry
    {
        uint32_t definition = 0xFFFFFFFFu, generation = 0;
        float yaw = 0.0f, pitch = 0.0f;
        unsigned samples = 0;
    };
    Entry e[8]{};
    unsigned next = 0;

    const Entry* find(uint32_t definition, uint32_t generation) const noexcept
    {
        for (const auto& x : e)
            if (x.samples && x.definition == definition && x.generation == generation)
                return &x;
        return nullptr;
    }

    void store(uint32_t definition, uint32_t generation, float yaw, float pitch, unsigned samples) noexcept
    {
        if (definition == 0xFFFFFFFFu || !samples || !std::isfinite(yaw) || !std::isfinite(pitch))
            return;
        for (auto& x : e)
            if (x.samples && x.definition == definition && x.generation == generation)
            {
                x.yaw = yaw;
                x.pitch = pitch;
                x.samples = samples;
                return;
            }
        Entry& slot = e[next % 8];
        next = (next + 1) % 8;
        slot = Entry{definition, generation, yaw, pitch, samples};
    }
};

// The dev line's account of one window: the sample offsets, and the gun's angles in the turret frame they were taken
// over (is the offset the same across the gun's range?).
struct OffsetRange
{
    unsigned samples = 0;
    float minYaw = 0.0f, maxYaw = 0.0f, minPitch = 0.0f, maxPitch = 0.0f;
    float gunMinYaw = 0.0f, gunMaxYaw = 0.0f, gunMinPitch = 0.0f, gunMaxPitch = 0.0f;

    void reset() noexcept { *this = OffsetRange{}; }

    void note(float dYaw, float dPitch, float gunYaw, float gunPitch) noexcept
    {
        if (!std::isfinite(dYaw) || !std::isfinite(dPitch) || !std::isfinite(gunYaw) || !std::isfinite(gunPitch))
            return;
        const float gy = std::remainder(gunYaw, kTwoPi);   // about the turret's nose, (-pi, pi]
        if (!samples)
        {
            minYaw = maxYaw = dYaw;
            minPitch = maxPitch = dPitch;
            gunMinYaw = gunMaxYaw = gy;
            gunMinPitch = gunMaxPitch = gunPitch;
        }
        else
        {
            minYaw = dYaw < minYaw ? dYaw : minYaw;
            maxYaw = dYaw > maxYaw ? dYaw : maxYaw;
            minPitch = dPitch < minPitch ? dPitch : minPitch;
            maxPitch = dPitch > maxPitch ? dPitch : maxPitch;
            gunMinYaw = gy < gunMinYaw ? gy : gunMinYaw;
            gunMaxYaw = gy > gunMaxYaw ? gy : gunMaxYaw;
            gunMinPitch = gunPitch < gunMinPitch ? gunPitch : gunMinPitch;
            gunMaxPitch = gunPitch > gunMaxPitch ? gunPitch : gunMaxPitch;
        }
        ++samples;
    }
};
} // namespace gun_slaved
