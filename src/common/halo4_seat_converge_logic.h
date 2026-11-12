#pragma once

// HALO 4 SEATED CONVERGENCE -- the engine-free half (every build). The engine half is
// src/dll/halo4_seat_converge.inl; the fire side's static RE (the assist, its cap, the shot line) is in
// halo4_seat_aim_logic.h.
//
// THE LINE A SEATED SHOT IS AIMED ALONG (static RE of the retail module; RVAs are where the code sits there):
//   * the fire routine's assist 0x1DCB8C -> 0x1DC2F4 -> 0x1DC86C asks the aim view 0x1DC9E4 for the unit's aim
//     line and traces 128 wu along it (0x1DC624: start + direction * 128);
//   * the view takes that line from the player's aim camera 0x2061A8, which asks the camera selector 0x10D268
//     and builds the first-person eye (0x124CD0: the unit's camera point 0x5F8A1C, or the turret marker for seat
//     flag bit 7) when it answers 0, else the FOLLOWING camera (0x293A84);
//   * the following camera is position = pivot [+ track offset] - distance * aim (0x293BAA..0x293BDF), the aim
//     being the unit's aim vector (+0x4E0, 0x293556), the pivot the seat's camera marker (0x5F8E88). It ORBITS
//     the pivot as the aim turns, and its line runs back through the pivot: exactly without a camera track,
//     closely with one;
//   * for a following camera (the selector answered 1) the view then moves the trace's START along the line to
//     the plane through the controlled unit's origin that faces the aim, plus 0.5 wu (0x1DCA3C..0x1DCB64). The
//     camera's own position is handed out beside it, untouched (0x1DCA2A).
//
// WHICH CAMERA THE SIMULATION USES IS NOT ASSUMED. The first-person seat override (native_vehicle_first_person)
// answers the selector for the local seat. While one-machine changes are not allowed (coop_safe_shots) it answers
// the frame only, so the fire -- which runs inside a simulation tick -- aims from the game's own camera: the
// following camera in a seat the game aims in third person. The aim view is therefore SAMPLED where the game
// calls it for the local unit in its control pass, with the override stood down for that call under the same
// rule, and the aim is solved along the line that sample gives. In a seat the game aims in first person, or with
// one-machine changes allowed, the same sample is the first-person eye: one solve covers both.
//
// THE SOLVE. Write the aim so the line from the sampled camera passes through P, the point the hand's intent ray
// hits: aim = normalize(P - S). S is a point of the sampled line: the camera, carried a little way toward the
// view's own trace start (SolveStart), then carried forward by its measured velocity for the sample's age and the
// ticks until the fire (CarrySeconds).
//
// THE FIXED POINT AND ITS RATE. The camera follows the aim it is solved from, so each solve is made against the
// camera the previous aim produced. With S a distance r behind the pivot along the line and P a distance L from
// the pivot, one step turns an angular error e into e * r / (L + r) (ContractionFactor): it shrinks from any
// start and for every range. From the camera itself r is the camera's distance D (a few metres): 0.13 at
// D = 4.5 m, L = 30 m; 0.6 at L = 3 m. The view's own trace start is 0.5 wu (1.5 m) PAST the controlled unit's
// origin plane, so it lies beyond the pivot: solved from there the factor is -d / (L - d), which lands on the
// other side each step and hunts at short range. So S is carried only a part of the way from the camera toward
// that start: half of it, never more than half of the way to P (seat_upstream::AskAgreement::LeadToward, Halo 3's
// own rule), and never more than kLeadCapWu -- the unit's origin can lie far ahead of a seat's pivot on a large
// vehicle, and a camera is taken to sit at least that far behind what it orbits (INFERRED, not measured: the
// lane's log reports the push and each sample's residual, which would show a start past the pivot as a residual
// that changes sides). With the cap r = D - 2 m: 0.08 at L = 30 m, 0.45 at L = 3 m.
// A step is one control pass -> tick -> control pass round trip. tests/halo4_seat_converge_tests.cpp holds the
// rate to this over a model of 0x293A84.
//
// Zero engine dependencies.

#include "converge_policy.h"
#include "seat_upstream_logic.h"
#include "shot_lag_logic.h"

#include <cmath>
#include <cstdint>

namespace halo4_seat_converge
{

// ---- 1. whose camera answers the sampled call -------------------------------------------------------------
// The control pass's aim-view call for the local seat is answered by the game's own camera selector exactly
// when a fire inside a simulation tick is (one-machine changes not allowed), and only while seats converge:
// with the key off nothing about that call changes.
inline constexpr bool SampleWithGameCamera(bool seatsConverge, bool fireTimeChangesAllowed)
{
    return seatsConverge && !fireTimeChangesAllowed;
}

// ---- 2. one sample of the aim view ------------------------------------------------------------------------
struct CameraSample
{
    bool valid = false;
    uint64_t stampUs = 0;        // when the view was called (the sight clock)
    float camera[3]{};           // the aim camera's position (0x1DC9E4's sixth argument)
    float dir[3]{};              // the line's direction (its fifth)
    float eye[3]{};              // where the game's own trace starts on that line (its fourth: the camera, or
                                 // pushed to the unit's plane for a following camera)
    float eyeVel[3]{};           // the eye's velocity, wu/s, over ~120 ms of samples
    bool eyeVelValid = false;
};

// A sample is used as it is this long (one comes every rendered frame); after that the start it gave stands in,
// held relative to the rendered head (StartHold), for as long as the game makes no aim-view call for the seat (a
// weapon switch takes about a second). Past the hold the sampler is taken to have stopped and the seat no longer
// solves. The fresh window is short on purpose: a sample is carried by its age and kCarryTicks ticks, the carry
// has a cap (kMaxCarryS), and an older sample would be carried short of where its camera has gone. The held
// start rides the vehicle with the head instead. (The static_assert below holds the two together at 60 ticks a
// second.)
inline constexpr uint64_t kSampleFreshUs = 60000;
inline constexpr uint64_t kSampleHoldUs = 3000000;

// How far ahead of the sample the fire's camera is: the sample's age, plus kCarryTicks simulation ticks.
// Outside a simulation tick the following camera's pivot is the frame-interpolated marker (0x5F8E88 asks the
// marker query for interpolated nodes; the interpolator refuses them only inside a tick, 0x133AE9), which shows
// the simulation one tick late; and the aim written from the sample is consumed by the tick after the write.
// Two ticks is that sum: INFERRED from those two facts, not measured. The per-fire witness reports what is left
// along the camera's travel so it can be set from a session.
inline constexpr float kCarryTicks = 2.0f;
inline constexpr float kMaxCarryS = 0.1f;
static_assert(float(kSampleFreshUs) * 1.0e-6f + kCarryTicks / 60.0f <= kMaxCarryS,
              "a sample still fresh must be carried its whole age and the ticks to the fire");

// Seconds to carry a sample taken at stampUs, read at nowUs. -1: no stamp, from the future, or past maxAgeUs.
inline float CarrySeconds(uint64_t stampUs, uint64_t nowUs, float tickS, uint64_t maxAgeUs = kSampleFreshUs)
{
    if (!stampUs || nowUs < stampUs || nowUs - stampUs > maxAgeUs || !std::isfinite(tickS) || !(tickS > 0.0f))
        return -1.0f;
    const float s = float(nowUs - stampUs) * 1.0e-6f + kCarryTicks * tickS;
    return s > kMaxCarryS ? kMaxCarryS : s;
}

// ---- 3. where the solve starts --------------------------------------------------------------------------
// The view's push is along the line by construction; an eye further than this off the camera's line is not that
// push and is ignored (the solve then starts at the camera). And no push is longer than this.
inline constexpr float kPushLineToleranceWu = 0.05f;
inline constexpr float kMaxPushWu = 40.0f * shot_lag::kWuPerMetre;
// The solve never starts further than this along the line from the camera (see the header: the start must stay
// behind the pivot the camera orbits).
inline constexpr float kLeadCapWu = 2.0f * shot_lag::kWuPerMetre;

struct Start
{
    bool valid = false;
    float pos[3]{};
    float pushWu = 0.0f;   // how far along the line the game's own trace starts ahead of the camera
    float leadWu = 0.0f;   // how far along it the solve starts (half of the push, half of the way to the
                           // point, kLeadCapWu: whichever is least)
    float carryS = 0.0f;   // how long the start was carried by the eye's velocity (0: not carried)
};

// The distance along the camera's line to the view's trace start (>= 0; 0 when the eye is the camera, behind it,
// off the line, or implausibly far).
inline float PushAlongLine(const CameraSample& s)
{
    float d[3]{s.dir[0], s.dir[1], s.dir[2]};
    if (!shot_lag::Finite3(s.camera) || !shot_lag::Finite3(s.eye) || !shot_lag::Finite3(d) ||
        !seat_upstream::Normalize(d))
        return 0.0f;
    const float w[3]{s.eye[0] - s.camera[0], s.eye[1] - s.camera[1], s.eye[2] - s.camera[2]};
    const float along = shot_lag::Dot(w, d);
    const float off[3]{w[0] - d[0] * along, w[1] - d[1] * along, w[2] - d[2] * along};
    if (!std::isfinite(along) || !(along > 0.0f) || along > kMaxPushWu ||
        !(shot_lag::Length(off) <= kPushLineToleranceWu))
        return 0.0f;
    return along;
}

// `point`: the point the aim will be solved through (null: not known yet -- the start is then the camera).
// `carryS`: CarrySeconds (<= 0, or no velocity: not carried).
inline Start SolveStart(const CameraSample& s, const float* point, float carryS)
{
    Start out{};
    float d[3]{s.dir[0], s.dir[1], s.dir[2]};
    if (!s.valid || !shot_lag::Finite3(s.camera) || !shot_lag::Finite3(d) || !seat_upstream::Normalize(d))
        return out;
    out.pushWu = PushAlongLine(s);
    if (point && shot_lag::Finite3(point))
    {
        out.leadWu = seat_upstream::AskAgreement::LeadToward(out.pushWu, s.camera, d, point);
        if (out.leadWu > kLeadCapWu)
            out.leadWu = kLeadCapWu;
    }
    for (int axis = 0; axis < 3; ++axis)
        out.pos[axis] = s.camera[axis] + d[axis] * out.leadWu;
    if (s.eyeVelValid && shot_lag::Finite3(s.eyeVel) && std::isfinite(carryS) && carryS > 0.0f)
    {
        float carried[3]{};
        if (shot_lag::ExtrapolateEye(out.pos, s.eyeVel, carryS, kMaxCarryS, carried))
        {
            out.pos[0] = carried[0];
            out.pos[1] = carried[1];
            out.pos[2] = carried[2];
            out.carryS = carryS > kMaxCarryS ? kMaxCarryS : carryS;
        }
    }
    out.valid = shot_lag::Finite3(out.pos);
    return out;
}

// One step's factor on the angular error: the start r behind the pivot (negative: past it), the point L from
// the pivot. |factor| < 1 is convergence; a negative factor alternates sides.
inline float ContractionFactor(float startBehindPivotWu, float pivotToPointWu)
{
    const float denominator = pivotToPointWu + startBehindPivotWu;
    if (!std::isfinite(denominator) || !(std::fabs(denominator) > 1.0e-6f))
        return 1.0f;
    return startBehindPivotWu / denominator;
}

// ---- 4. the start across a gap in the samples ----------------------------------------------------------------
// The game makes its aim-view call behind the unit's weapon test, so a weapon switch stops the samples for about
// a second. That is natural, and must not switch a seat's convergence off (converge_policy.h): the last start
// is kept as an offset from the rendered head, which rides the same vehicle, and stands in until a sample comes.
//
// A FOLLOWING camera's start does not stay put against the head: it orbits the seat's pivot as the aim turns,
// and nothing iterates the fixed point while no sample comes. A start held through a turn of t degrees leaves the
// aim off by about atan(r sin t / (L + r cos t)): 4 degrees for r = 2.5 m, L = 10 m, t = 20. So such a start is
// held only while the hand still points within kHoldTurnDeg of where it pointed when the start was taken (about
// 1 degree off at those numbers); past that the seat has no start until the next sample. A first-person eye does
// not move with the aim, and its start is held through any turn.
inline constexpr float kHoldTurnDeg = 5.0f;
struct StartHold
{
    bool have = false;
    bool orbits = false;     // the start was a following camera's (the view pushed its trace start)
    uint64_t atUs = 0;
    float fromHead[3]{};
    float pointing[3]{};     // where the hand pointed when the start was taken
    void Reset() { *this = StartHold{}; }
    void Note(uint64_t nowUs, const float start[3], const float head[3], const float handPointing[3],
              bool followingCamera)
    {
        if (!nowUs || !shot_lag::Finite3(start) || !shot_lag::Finite3(head) || !handPointing ||
            !shot_lag::Finite3(handPointing))
            return;
        have = true;
        orbits = followingCamera;
        atUs = nowUs;
        for (int axis = 0; axis < 3; ++axis)
        {
            fromHead[axis] = start[axis] - head[axis];
            pointing[axis] = handPointing[axis];
        }
    }
    bool Get(uint64_t nowUs, const float head[3], const float handPointing[3], float out[3]) const
    {
        if (!have || nowUs < atUs || nowUs - atUs > kSampleHoldUs || !shot_lag::Finite3(head))
            return false;
        if (orbits && (!handPointing || !shot_lag::Finite3(handPointing) ||
                       !(seat_upstream::AngleDeg(pointing, handPointing) <= kHoldTurnDeg)))
            return false;
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = head[axis] + fromHead[axis];
        return shot_lag::Finite3(out);
    }
};

// ---- 5. why a seated poll did not solve ----------------------------------------------------------------------
// Every reason is our code's, a switch, or a state the solve does not apply to. None is a natural limit: a
// seat's stop, a gun's rate cap and the assist's own cap leave the seat converged (the game holds the shot).
enum class Why : uint8_t
{
    Solved = 0,
    AimOff,            // converged_aim = 0, or seats are not aimed by the hand (reach_vehicle_aim = 0)
    ViewNotOurs,       // the first-person seat view is not up (vehicle_first_person off, or it is not placing)
    NoHead,            // the rendered eye is not published
    TraceOff,          // the title's aim trace is not armed (its collision query did not bind)
    NoAimCamera,       // the aim camera's anchors did not prove on this build: nothing can be sampled
    NoSample,          // the game has made no aim-view call for this seat (no weapon to aim), or stopped
    NoControlUpdate,   // the seat's control update does not reach the mod (it runs another controller, or the
                       // direct-drive hook is not installed): nothing traces the seat
    TraceFaulted,      // the native ray raised a fault and the trace latched off
    OtherStance,       // the newest trace is another stance's (just sat down, or changed seat)
    NoTrace,           // no fresh trace result
    NoRange,           // no range yet, or the geometry cannot resolve
    Count
};

inline const char* WhyName(Why why)
{
    switch (why)
    {
    case Why::Solved: return "solved";
    case Why::AimOff: return "aim switches off";
    case Why::ViewNotOurs: return "seat view not ours";
    case Why::NoHead: return "no rendered eye";
    case Why::TraceOff: return "trace not armed";
    case Why::NoAimCamera: return "aim camera not proven";
    case Why::NoSample: return "no aim-camera sample";
    case Why::NoControlUpdate: return "no control update";
    case Why::TraceFaulted: return "trace faulted";
    case Why::OtherStance: return "trace of another stance";
    case Why::NoTrace: return "no fresh trace";
    case Why::NoRange: return "no range";
    default: return "?";
    }
}

// What one seated poll knows before it tries the bend.
struct PollFacts
{
    bool aimOn = false;           // converged_aim and reach_vehicle_aim
    bool viewOurs = false;
    bool headKnown = false;
    bool traceArmed = false;
    bool anchors = false;         // the aim camera proved
    bool start = false;           // a start is in hand: a fresh sample's, or the held one
    bool controlUpdate = false;   // the seat's control update reached the mod lately
    bool traceFaulted = false;
    bool stanceOurs = false;      // the trace thread is tracing this seat's ray
};

// The first reason a poll may not try the bend, else Solved (the bend itself may still decline: FromBend).
inline Why DecidePoll(const PollFacts& f)
{
    if (!f.aimOn) return Why::AimOff;
    if (!f.viewOurs) return Why::ViewNotOurs;
    if (!f.headKnown) return Why::NoHead;
    if (!f.traceArmed) return Why::TraceOff;
    if (!f.anchors) return Why::NoAimCamera;
    if (!f.start) return Why::NoSample;
    if (!f.controlUpdate) return Why::NoControlUpdate;
    if (f.traceFaulted) return Why::TraceFaulted;
    if (!f.stanceOurs) return Why::OtherStance;
    return Why::Solved;
}

// The sight layer's reason for declining the bend, as this lane counts it.
inline Why FromBend(converge::ConvergeOff why)
{
    switch (why)
    {
    case converge::ConvergeOff::None: return Why::Solved;
    case converge::ConvergeOff::AimOff: return Why::AimOff;
    case converge::ConvergeOff::TraceFaulted: return Why::TraceFaulted;
    case converge::ConvergeOff::NoRange: return Why::NoRange;
    case converge::ConvergeOff::RayStale: return Why::NoHead;
    default: return Why::NoTrace;
    }
}

// ...and this lane's reason for the dev converge-off witness (Sight_NoteConvergeState).
inline converge::ConvergeOff OffReason(Why why)
{
    using converge::ConvergeOff;
    switch (why)
    {
    case Why::Solved: return ConvergeOff::None;
    case Why::AimOff: return ConvergeOff::AimOff;
    case Why::ViewNotOurs: return ConvergeOff::SeatViewNotOwned;
    case Why::NoHead: return ConvergeOff::RayStale;
    case Why::TraceOff: return ConvergeOff::NoTraceFlags;
    case Why::TraceFaulted: return ConvergeOff::TraceFaulted;
    case Why::NoRange: return ConvergeOff::NoRange;
    case Why::NoTrace:
    case Why::OtherStance: return ConvergeOff::TraceStale;
    default: return ConvergeOff::SeatProofPending;   // NoAimCamera, NoSample, NoControlUpdate
    }
}

// ---- 6. the witness: the game's own line against the point ---------------------------------------------------
// How far a line the game built (its start and direction) passes from the point the aim was solved through:
// the angle at the start, split into heading and elevation, and the miss at the point. `velocity` (wu/s, may be
// null): the miss along the start's travel. A start that had travelled further than the solve carried it leaves
// the point BEHIND the line, so alongMm is negative and its size over the speed is the time the carry was short
// by -- for travel across the aim; travel along the aim moves the start along its own line and shows nothing.
struct LineMiss
{
    bool valid = false;
    float deg = 0.0f;        // the angle between the line and start -> point
    float yawDeg = 0.0f;     // start -> point minus the line, heading (scaled to an angle at the point's pitch)
    float pitchDeg = 0.0f;   // ...and elevation
    float mm = 0.0f;         // the point's distance from the line
    float rangeM = 0.0f;
    bool travel = false;     // the start was moving (over 1 m/s): alongMm and impliedMs mean something
    float alongMm = 0.0f;    // the point's offset from the line, along the start's travel (negative: left behind)
    float impliedMs = 0.0f;  // alongMm over the speed (negative: the carry was that much short)
};

inline LineMiss MeasureLine(const float start[3], const float dir[3], const float point[3],
                            const float* velocity = nullptr)
{
    LineMiss m{};
    float d[3]{dir[0], dir[1], dir[2]};
    if (!shot_lag::Finite3(start) || !shot_lag::Finite3(point) || !shot_lag::Finite3(d) ||
        !seat_upstream::Normalize(d))
        return m;
    const float to[3]{point[0] - start[0], point[1] - start[1], point[2] - start[2]};
    const float range = shot_lag::Length(to);
    if (!std::isfinite(range) || !(range > 0.3f * shot_lag::kWuPerMetre))
        return m;
    float lineYaw = 0.0f, linePitch = 0.0f, toYaw = 0.0f, toPitch = 0.0f;
    if (!seat_upstream::AnglesFromDir(d, lineYaw, linePitch) || !seat_upstream::AnglesFromDir(to, toYaw, toPitch))
        return m;
    const float along = shot_lag::Dot(to, d);
    const float off[3]{to[0] - d[0] * along, to[1] - d[1] * along, to[2] - d[2] * along};
    m.valid = true;
    m.deg = seat_upstream::AngleDeg(d, to);
    m.yawDeg = std::remainder(toYaw - lineYaw, 6.2831853f) * std::cos(toPitch) * shot_lag::kRadToDeg;
    m.pitchDeg = (toPitch - linePitch) * shot_lag::kRadToDeg;
    m.mm = shot_lag::Length(off) * shot_lag::kMmPerWu;
    m.rangeM = range * shot_lag::kMetresPerWu;
    if (velocity && shot_lag::Finite3(velocity))
    {
        const float speed = shot_lag::Length(velocity);
        if (std::isfinite(speed) && speed > 1.0f * shot_lag::kWuPerMetre)
        {
            m.travel = true;
            m.alongMm = shot_lag::Dot(off, velocity) / speed * shot_lag::kMmPerWu;
            m.impliedMs = m.alongMm / (speed * shot_lag::kMmPerWu) * 1000.0f;
        }
    }
    return m;
}

// ---- 7. the seat, from its live flags word ---------------------------------------------------------------------
// Halo 4's seat flags as the retail module uses them (halo4_seat_aim_logic.h): bit 2 the driver (the control
// dispatcher's own test, 0xA88E8), bit 3 the gunner, bit 4 the following-camera seat, bit 7 a camera on the gun.
inline constexpr uint32_t kFlagDriver = 1u << 2;
inline constexpr uint32_t kFlagGunner = 1u << 3;
inline constexpr uint32_t kFlagFollowingCamera = 1u << 4;
inline constexpr uint32_t kFlagCameraOnGun = 1u << 7;

inline const char* RoleName(bool flagsKnown, uint32_t flags)
{
    if (!flagsKnown)
        return "unread";
    const bool driver = (flags & kFlagDriver) != 0, gunner = (flags & kFlagGunner) != 0;
    if (flags & kFlagCameraOnGun)
        return driver ? "driver, camera on the gun" : "gunner, camera on the gun";
    return driver && gunner ? "driver+gunner" : driver ? "driver" : gunner ? "gunner" : "passenger";
}

// A seat's "camera on its gun" flag, held for the seat stint. Set by a control update of this seat that read the
// seat's flags, and changed only by another such read: a poll whose read of the control update collided, or an
// update that came without the flags, leaves it as it was. Dropping it for one poll would flip the pitch servo out
// of its turret mode and back, which resets what the servo has measured. A new stint starts a new latch.
struct GunCameraLatch
{
    bool known = false, onGun = false;
    void Note(bool controlRead, bool flagsKnown, uint32_t flags)
    {
        if (!controlRead || !flagsKnown)
            return;
        known = true;
        onGun = (flags & kFlagCameraOnGun) != 0;
    }
    bool Turret() const { return known && onGun; }
};

} // namespace halo4_seat_converge
