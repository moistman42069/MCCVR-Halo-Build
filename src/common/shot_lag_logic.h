#pragma once

// SHOT LAG -- aim at a WORLD POINT, recomposed against each shot's own origin (the two-clocks rule).
//
// The converged aim is solved on the input thread from an engine eye E sampled at RENDER time, bent through the
// point P the intent ray hits, and published as ANGLES. The simulation writes those angles a frame or more later,
// and the shot leaves from the FIRE tick's eye. Angles are a direction, not a line: the shot line is the intended
// line moved by everything the eye did in between (v * dt, measured in Reach at a mean 93 mm, p90 187 mm, max 307 mm
// over 236 shots, dt 25-40 ms). No angle can fix a parallel offset; a point can. So the aim path publishes its
// intent as the point P itself, and every consumer that knows its own origin O aims along normalize(P - O):
//
//   * the shot: the title's own query or firing call, at the fire tick, from the shot's own origin -- exact;
//   * the record (HUD, autoaim, melee, network): the stick call, from the best estimate of the eye at the tick
//     that consumes it -- never from a P rebuilt at the current eye, which would recreate the very error.
//
// P is built from ONE frame-coherent pair: the hand direction and the camera of the same prepared OpenXR frame
// (a hand from frame k with a camera from frame k-1 puts P off by the eye's motion in one frame).
//
// Pure functions and plain structs only: unit-tested in tests/shot_lag_tests.cpp. World units are Blam's
// (1 wu = 3.048 m); times are microseconds of one monotonic clock (QPC in the module).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace shot_lag
{
inline constexpr float kMetresPerWu = 3.048f;
inline constexpr float kMmPerWu = 3048.0f;
inline constexpr float kWuPerMetre = 1.0f / 3.048f;
inline constexpr float kRadToDeg = 57.295779513082320f;

inline float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline float Length(const float a[3]) { return std::sqrt(Dot(a, a)); }
inline bool Finite3(const float a[3])
{
    return a && std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}

// ---- 1. re-aim through the point ---------------------------------------------------------------------------
// Why a re-aim was refused (the caller leaves its query or shot exactly as the game built it). Only geometric
// impossibilities: no line from this start through P points into the query's forward hemisphere, or none is defined.
enum class Reaim : uint8_t
{
    Applied = 0,
    NonFinite,      // any input not finite
    Degenerate,     // a zero-length query vector
    PointBehind,    // P is not ahead of the start along the query: no forward line through it
    PointTooClose,  // P is within minRangeWu of the start: it defines no direction
    AlreadyThrough, // the query already passes within minOffsetWu of P: left exactly as the game built it
    Count
};

inline const char* ReaimName(Reaim why)
{
    switch (why)
    {
    case Reaim::Applied: return "applied";
    case Reaim::NonFinite: return "non-finite";
    case Reaim::Degenerate: return "degenerate";
    case Reaim::PointBehind: return "point behind";
    case Reaim::PointTooClose: return "point too close";
    case Reaim::AlreadyThrough: return "already through";
    default: return "?";
    }
}

// NO MAGNITUDE LIMIT. A size limit cannot tell a correct big correction from a wrong one, so all it does is hide
// problems, and an absolute metres rail turns a constant ANGLE into a range-and-speed cliff. The Halo 3 Hornet's
// crosshair query leaves the record by an angle that grows with sideways speed (5.65 deg at a 15-18 m/s strafe; it
// fits atan(v_sideways / ~200 m/s), mechanism unproven), 3.5-4 m at 50 m. Under a rail of `offset <= 3 m and bend
// <= 30 deg`, from ~8 m/s sideways half or more of the queries are refused and the shots land 2-6 m behind along
// travel: accurate up to a certain strafe speed, then suddenly well off horizontally.
//
// Whether P is meaningful for THIS query is decided by identity and freshness, never by how far it is: the local
// player's fire (FireMark) and query (QueryIsLocal: his unit, or his vehicle's seat chain), P this generation and
// under 150 ms old with the converged aim live and no natural hold (ShouldUseIntent). What is left here is geometry:
// behind the start or within minRangeWu of it no forward line through P exists. Every refusal is counted by reason
// (Sight_ReaimThroughPoint, the SHOT REAIM line). A query that already passes within minOffsetWu of P is LEFT ALONE
// (AlreadyThrough): where the game's line is already coherent (Halo 3 campaign vehicles) the re-aim must not move
// it by the noise in P.
struct ReaimRails
{
    float minOffsetWu = 0.010f * kWuPerMetre;
    float minRangeWu = 0.3f * kWuPerMetre;
};

// out = normalize(P - start) * |vector|: the same query, from its own start, through the point. `out` is written
// only on Applied. `bendDeg`, when given, receives the angle between the old and the new line on Applied.
inline Reaim ReaimThroughPoint(const float start[3], const float vector[3], const float point[3],
                               const ReaimRails& rails, float out[3], float* bendDeg = nullptr)
{
    if (!Finite3(start) || !Finite3(vector) || !Finite3(point) || !out)
        return Reaim::NonFinite;
    const float length = Length(vector);
    if (!std::isfinite(length) || !(length > 1.0e-6f))
        return Reaim::Degenerate;
    const float toPoint[3]{point[0] - start[0], point[1] - start[1], point[2] - start[2]};
    const float distance = Length(toPoint);
    if (!std::isfinite(distance))
        return Reaim::NonFinite;
    const float d[3]{vector[0] / length, vector[1] / length, vector[2] / length};
    const float ahead = Dot(toPoint, d);
    if (!(ahead > 0.0f))
        return Reaim::PointBehind;
    if (!(distance > rails.minRangeWu))
        return Reaim::PointTooClose;
    // How far P sits off the old line, and the angle between the two lines -- from the perpendicular VECTOR, never
    // as distance^2 - ahead^2: that difference of two large squares cancels catastrophically in float (at 1 km its
    // rounding alone is ~0.5 m of "offset", so a query already through P read as off it), and acos near 1 is as bad.
    // toPoint - d * ahead keeps an absolute error of a few ulps of the range (well under a millimetre at 1 km).
    const float perpendicular[3]{toPoint[0] - d[0] * ahead, toPoint[1] - d[1] * ahead, toPoint[2] - d[2] * ahead};
    const float offset = Length(perpendicular);
    const float bend = std::atan2(offset, ahead) * kRadToDeg;
    if (offset < rails.minOffsetWu)
        return Reaim::AlreadyThrough;
    const float scale = length / distance;
    out[0] = toPoint[0] * scale;
    out[1] = toPoint[1] * scale;
    out[2] = toPoint[2] * scale;
    if (bendDeg)
        *bendDeg = bend;
    return Reaim::Applied;
}

// ---- 2. the published intent ----------------------------------------------------------------------------------
// What the aim path publishes: P and how it was built. Every field the shot site needs to decide to use it.
struct IntentPoint
{
    uint32_t generation = 0;
    uint64_t stampUs = 0;      // when the solve built P
    uint64_t serial = 0;       // the prepared OpenXR frame of the coherent pair (0 = none)
    bool coherent = false;     // hand and camera from the same prepared frame
    bool railHeld = false;     // the solve held the aim at the pitch rail: the record does not pass through P
    bool seated = false;
    float point[3]{};          // P = head + dir * rangeWu
    float dir[3]{};            // u, unit
    float head[3]{};           // H, the rendered eye of the pair
    float eye[3]{};            // E, the engine eye the solve used
    uint64_t eyeStampUs = 0;   // when E was sampled (the render that published it)
    float eyeVel[3]{};         // the engine eye's velocity, wu/s, over ~100 ms before eyeStampUs
    bool eyeVelValid = false;
    float rangeWu = 0.0f;
    float solvedYaw = 0.0f, solvedPitch = 0.0f;   // the angles the solve published from it (game basis)
    // THE CAMERA SAMPLE P WAS BUILT FROM (TransportPoint, 4a): the engine eye under the rendered eye `head` -- the
    // coherent pair's own frame eye, else the solve's eye -- and when that render ran. 0 / invalid: unknown.
    bool pointEyeValid = false;
    float pointEye[3]{};
    uint64_t pointEyeStampUs = 0;
    // When the solve's eye `eye` was rendered (the newest camera frame at the solve; 0 unknown): a seated shot's
    // point (SightIntent::solvePoint) was bent from that eye.
    uint64_t solveEyeStampUs = 0;
};

// The shot site's verdict on a published point. `maxAgeUs`: older is stale (the player no longer means it).
enum class IntentUse : uint8_t
{
    Use = 0,
    Missing,       // nothing published, or for another title generation
    Stale,         // older than maxAgeUs, or from the future
    RailHeld,      // the solve did not pass through it (held at the pitch rail)
    NaturalHeld,   // a natural cause holds the game's aim off it (a seat stop, a turret's slew)
    NotConverged,  // the converged aim is not in effect
    Bad,           // a non-finite or degenerate point
    OwnShotLine,   // the shot is another gun's own line (an off-hand gun, a barrel shot): never the aim hand's P
    OutsideFire,   // a crosshair query at the site outside any native fire (QueryGate::OutsideFire)
    OtherFire,     // inside a fire whose shooter is not the local player (QueryGate::OtherFire)
    NotOurQuery,   // inside our fire, but the query ignores neither our unit nor our seat chain (QueryGate)
    Count
};

inline const char* IntentUseName(IntentUse why)
{
    switch (why)
    {
    case IntentUse::Use: return "used";
    case IntentUse::Missing: return "missing";
    case IntentUse::Stale: return "stale";
    case IntentUse::RailHeld: return "rail-held";
    case IntentUse::NaturalHeld: return "natural-held";
    case IntentUse::NotConverged: return "not converged";
    case IntentUse::Bad: return "bad";
    case IntentUse::OwnShotLine: return "own shot line";
    case IntentUse::OutsideFire: return "outside a fire";
    case IntentUse::OtherFire: return "another shooter's fire";
    case IntentUse::NotOurQuery: return "not our query";
    default: return "?";
    }
}

// THE CROSSHAIR QUERY'S GATE: before any intent is consulted, may the re-aim touch this query at all? Only the local
// player's query at a resolved crosshair site, and only while no other shot line owns the fire. Independent dual aim
// (the off-hand gun) and barrel / muzzle shots run the fire with that GUN's own camera ray -- the firing assist's
// camera is swapped for it (Halo 3 0x13BC0D, ODST 0x161204, Reach 0x10F954) -- so its crosshair query traces that
// gun's line, and re-aiming it through the AIM HAND's point would steer the off-hand or barrel shot onto the other
// hand's target.
//
// THE LOCAL PLAYER'S FIRE. A query's ignores cannot tell two players in one vehicle apart (Reach and Halo 3 name only
// the vehicle's root or the seat's parent), and in networked co-op another player's
// assist runs on this machine too (the assists take a player index: Halo 3 0x13BAD0, Reach's aim_projectile). So the
// fire itself is tagged on the fire thread: a native fire's entry opens it as kFireOther, and its shot line -- the one
// call that names the shooting unit (Halo 3 0x3524B0 from 0x368B92: the OCCUPANT after the gunner chain; ODST
// 0x396B7C from 0x3AEB26; Reach's unit-adjust from 0x4C303A) -- marks it kFireLocal when that unit is the local
// player's. The fire's exit restores the enclosing value. Only a kFireLocal fire's queries are ever re-aimed.
inline constexpr int8_t kFireNone = -1;    // not inside a native fire on this thread
inline constexpr int8_t kFireOther = 0;    // inside a fire whose shooter is not (yet) the local player
inline constexpr int8_t kFireLocal = 1;    // inside the local player's fire
inline int8_t FireMark(int8_t current, bool shooterIsLocal)
{
    return current == kFireNone ? kFireNone : (shooterIsLocal ? kFireLocal : kFireOther);
}

// Every verdict but NotSite is counted by the caller (QueryGateSkip -> the SHOT REAIM line): an uncounted refusal
// can leave a whole stint of queries un-re-aimed with nothing in the log to show it.
enum class QueryGate : uint8_t
{
    Try = 0,       // consult the intent (ShouldUseIntent)
    NotSite,       // not a resolved crosshair site: not a query this path owns, not counted
    OutsideFire,   // at the site but outside any native fire
    OtherFire,     // inside another shooter's fire
    NotOurQuery,   // inside our fire, but not our unit's or seat chain's query
    OwnShotLine,   // the local fire's line belongs to another gun (IntentUse::OwnShotLine)
};
inline QueryGate GateCrosshairQuery(bool atSite, int8_t fire, bool queryLocal, bool ownShotLine)
{
    if (!atSite)
        return QueryGate::NotSite;
    if (fire == kFireNone)
        return QueryGate::OutsideFire;
    if (fire != kFireLocal)
        return QueryGate::OtherFire;
    if (!queryLocal)
        return QueryGate::NotOurQuery;
    if (ownShotLine)
        return QueryGate::OwnShotLine;
    return QueryGate::Try;
}
// The skip reason a counted gate verdict is logged under (Try and NotSite: none, IntentUse::Use).
inline IntentUse QueryGateSkip(QueryGate gate)
{
    switch (gate)
    {
    case QueryGate::OutsideFire: return IntentUse::OutsideFire;
    case QueryGate::OtherFire: return IntentUse::OtherFire;
    case QueryGate::NotOurQuery: return IntentUse::NotOurQuery;
    case QueryGate::OwnShotLine: return IntentUse::OwnShotLine;
    default: return IntentUse::Use;
    }
}

inline IntentUse ShouldUseIntent(bool published, const IntentPoint& p, uint32_t generation, uint64_t nowUs,
                                 uint64_t maxAgeUs, bool convergedLive, bool naturalHeld)
{
    if (!published || !generation || p.generation != generation || !p.stampUs)
        return IntentUse::Missing;
    if (nowUs < p.stampUs || nowUs - p.stampUs > maxAgeUs)
        return IntentUse::Stale;
    if (!convergedLive)
        return IntentUse::NotConverged;
    if (naturalHeld)
        return IntentUse::NaturalHeld;
    if (p.railHeld)
        return IntentUse::RailHeld;
    if (!Finite3(p.point) || !Finite3(p.dir) || !(std::isfinite(p.rangeWu) && p.rangeWu > 0.0f))
        return IntentUse::Bad;
    return IntentUse::Use;
}

// ---- 3. the eye's velocity ------------------------------------------------------------------------------------
// The engine eye's track on ONE thread (the publishing thread): samples with their own clock, the velocity over
// the newest ~windowUs. An eye only moves when the game renders a new frame, so a sample identical to the last is
// news only once it has stood still for kStillUs (a player who stops must read 0, not his last pace). A gap, a
// clock running backwards, or a jump faster than any vehicle (a teleport, a respawn, a seat change) starts afresh.
struct EyeTrack
{
    static constexpr int kSamples = 16;
    static constexpr uint64_t kGapUs = 250000;          // no sample for this long: start afresh
    static constexpr uint64_t kStillUs = 40000;         // an unchanged eye counts as a sample after this long
    static constexpr float kTeleportWuPerS = 150.0f * kWuPerMetre;
    uint64_t t[kSamples]{};
    float p[kSamples][3]{};
    int count = 0;
    int head = 0;   // next write

    void Reset() { count = 0; head = 0; }
    const float* Newest(uint64_t* at = nullptr) const
    {
        if (!count)
            return nullptr;
        const int i = (head + kSamples - 1) % kSamples;
        if (at)
            *at = t[i];
        return p[i];
    }
    void Push(uint64_t tUs, const float eye[3])
    {
        if (!Finite3(eye) || !tUs)
            return;
        uint64_t lastAt = 0;
        if (const float* last = Newest(&lastAt))
        {
            if (tUs <= lastAt)
            {
                if (tUs < lastAt)
                    Reset();   // the clock ran backwards: nothing before is comparable
                else
                    return;    // the same instant twice
            }
            else if (tUs - lastAt > kGapUs)
                Reset();
            else
            {
                const float d[3]{eye[0] - last[0], eye[1] - last[1], eye[2] - last[2]};
                const float moved = Length(d);
                const float seconds = float(tUs - lastAt) * 1.0e-6f;
                if (moved == 0.0f && tUs - lastAt < kStillUs)
                    return;    // a repeat of the last render's eye: no news yet
                if (!(moved <= kTeleportWuPerS * seconds))
                    Reset();
            }
        }
        t[head] = tUs;
        p[head][0] = eye[0];
        p[head][1] = eye[1];
        p[head][2] = eye[2];
        head = (head + 1) % kSamples;
        if (count < kSamples)
            ++count;
    }
    // Velocity (wu/s) from the newest sample back to the oldest one within windowUs of it, needing at least
    // minSpanUs between them. False: not enough track (the caller treats the speed as unknown, never as 0).
    bool Velocity(uint64_t windowUs, uint64_t minSpanUs, float out[3]) const
    {
        uint64_t newestAt = 0;
        const float* newest = Newest(&newestAt);
        if (!newest || count < 2)
            return false;
        int oldest = -1;
        for (int k = 1; k < count; ++k)
        {
            const int i = (head + kSamples - 1 - k) % kSamples;
            if (newestAt - t[i] > windowUs)
                break;
            oldest = i;
        }
        if (oldest < 0 || newestAt - t[oldest] < minSpanUs)
            return false;
        const float seconds = float(newestAt - t[oldest]) * 1.0e-6f;
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = (newest[axis] - p[oldest][axis]) / seconds;
        return Finite3(out);
    }
};

// ---- 3b. the game-eye proof -----------------------------------------------------------------------------------
// A ValueAgreement over a claim: "this evaluator's answer IS the engine eye". Compared against the engine camera's own
// position at rest (moving, a rendered camera may lead or trail the simulation). The comparison means something only
// where the two are the SAME point by construction -- on foot, in first person -- so a sample from a seat, a third-
// person or scripted camera, a carried turret or a death camera is no evidence either way (`eligible` false). One
// disagreeing sample proves nothing (a respawn, a teleport, a frame of transition), so disproof takes a STREAK of
// kDisagreeToDisprove consecutive disagreeing at-rest samples, as proof takes kAgreeToProve agreeing ones. A
// disproof holds for the unit's on-foot stint; a new unit (respawn) or a new stint re-opens the question. A new
// title generation starts over.
struct EyeProof
{
    enum class State : uint8_t { Unproven = 0, Proven, Disproven };
    static constexpr uint32_t kAgreeToProve = 30;
    static constexpr uint32_t kDisagreeToDisprove = 15;
    static constexpr float kAgreeMm = 50.0f;
    static constexpr float kDisagreeMm = 250.0f;

    State state = State::Unproven;
    uint32_t agree = 0, disagree = 0;
    uint32_t generation = 0;
    int32_t unit = -1;
    bool inStint = false;

    // One sample from the publisher. `offsetMm` is read only when eligible and at rest.
    void Note(uint32_t gen, int32_t unitNow, bool eligible, bool atRest, float offsetMm)
    {
        if (gen != generation)
        {
            *this = EyeProof{};
            generation = gen;
            unit = unitNow;
        }
        else if (unitNow != unit)
        {
            unit = unitNow;
            inStint = false;   // a new unit's stint starts below
        }
        if (!eligible)
        {
            inStint = false;
            return;
        }
        if (!inStint)
        {
            inStint = true;
            agree = disagree = 0;
            if (state == State::Disproven)
                state = State::Unproven;
        }
        if (!atRest || !std::isfinite(offsetMm))
            return;
        if (offsetMm < kAgreeMm)
        {
            disagree = 0;
            if (++agree >= kAgreeToProve && state != State::Disproven)
                state = State::Proven;
        }
        else if (offsetMm > kDisagreeMm)
        {
            agree = 0;
            if (++disagree >= kDisagreeToDisprove)
                state = State::Disproven;
        }
        else
            agree = disagree = 0;   // neither: no streak survives it
    }
};

// ---- 4. the eye now, and the angles toward the point --------------------------------------------------------
// The interim estimate of an eye at a later tick: E + v * dt, dt clamped to [0, maxDtS]. False (out untouched)
// without a finite eye and velocity.
inline bool ExtrapolateEye(const float eye[3], const float velocity[3], float dtS, float maxDtS, float out[3])
{
    if (!Finite3(eye) || !Finite3(velocity) || !std::isfinite(dtS) || !out)
        return false;
    const float dt = dtS < 0.0f ? 0.0f : (dtS > maxDtS ? maxDtS : dtS);
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = eye[axis] + velocity[axis] * dt;
    return true;
}

// ---- 4a. P transported with the eye (dev key shot_point_transport) ---------------------------------------------
// An A/B for shots that trail a strafe a little, compared with flat Halo. Flat Halo fires through its crosshair from
// the game's eye AT THE FIRE TICK: a crosshair held still relative to the body moves with the body. P is a WORLD point
// fixed at the camera sample it was built from, a tick or so older than the fire (measured in Halo 3: P's camera
// ~15.6 ms old at the fire, its hand ~10 ms), so with a body-fixed aim a strafing shot lands v * age (10-20 cm)
// behind where flat's would. Transported, P moves with the eye's own motion since that sample:
//
//      P' = P + (E_fire - E_P)
//
// E_P the engine eye of the camera sample P was built from (IntentPoint::pointEye), E_fire the eye the shot leaves
// from at the fire tick. From E_fire the line through P' is exactly parallel to the solved line from E_P, so it is
// what flat Halo would fire. E_fire MEASURED wherever the title has it (Halo 3 on foot, from the unit's camera
// evaluator that EyeProof proves to be the engine eye: the crosshair query's own start, the evaluator asked at a
// muzzle shot, else this tick's published eye -- FireEyeGate); otherwise EXTRAPOLATED from E_P's velocity over its age
// at the fire (E_P + v * age). Which was used is counted. No magnitude limit (see ReaimRails): a transport is
// refused only for identity or freshness -- an unknown camera sample, no eye and no velocity, an E_P from the
// future or older than maxAgeUs.
enum class Transport : uint8_t
{
    Measured = 0,   // E_fire is the game's own eye at the fire tick
    Extrapolated,   // no fire-tick eye: E_fire = E_P + v * (now - E_P's stamp)
    NoPointEye,     // P's camera sample unknown (no eye, no stamp, non-finite): P is used as published
    NoFireEye,      // no fire-tick eye and no velocity: P as published
    StaleEye,       // extrapolating, E_P stamped after now or older than maxAgeUs: P as published
    Bad,            // P (or the result) non-finite
    Count
};
inline const char* TransportName(Transport t)
{
    switch (t)
    {
    case Transport::Measured: return "measured";
    case Transport::Extrapolated: return "extrapolated";
    case Transport::NoPointEye: return "no camera sample";
    case Transport::NoFireEye: return "no fire eye or velocity";
    case Transport::StaleEye: return "camera sample stale";
    case Transport::Bad: return "bad";
    default: return "?";
    }
}
inline bool TransportApplied(Transport t) { return t == Transport::Measured || t == Transport::Extrapolated; }

struct TransportInput
{
    float point[3]{};          // P (on foot the frame-coherent point, seated the solve's point)
    bool pointEyeValid = false;
    float pointEye[3]{};       // E_P
    uint64_t pointEyeStampUs = 0;
    bool fireEyeValid = false; // E_fire measured (its identity and freshness are FireEyeGate's)
    float fireEye[3]{};
    bool velValid = false;     // the engine eye's velocity, wu/s (the extrapolation)
    float vel[3]{};
    uint64_t nowUs = 0;
    uint64_t maxAgeUs = 0;     // the oldest E_P an extrapolation carries
};
// `out` = P' and `shift` = P' - P, written only when applied (Measured / Extrapolated).
inline Transport TransportPoint(const TransportInput& in, float out[3], float shift[3])
{
    if (!out || !shift || !Finite3(in.point))
        return Transport::Bad;
    if (!in.pointEyeValid || !in.pointEyeStampUs || !Finite3(in.pointEye))
        return Transport::NoPointEye;
    float s[3]{};
    Transport how = Transport::Measured;
    if (in.fireEyeValid && Finite3(in.fireEye))
    {
        for (int axis = 0; axis < 3; ++axis)
            s[axis] = in.fireEye[axis] - in.pointEye[axis];
    }
    else if (in.velValid && Finite3(in.vel))
    {
        if (in.nowUs < in.pointEyeStampUs || in.nowUs - in.pointEyeStampUs > in.maxAgeUs)
            return Transport::StaleEye;
        const float dt = float(in.nowUs - in.pointEyeStampUs) * 1.0e-6f;
        for (int axis = 0; axis < 3; ++axis)
            s[axis] = in.vel[axis] * dt;
        how = Transport::Extrapolated;
    }
    else
        return Transport::NoFireEye;
    const float p[3]{in.point[0] + s[0], in.point[1] + s[1], in.point[2] + s[2]};
    if (!Finite3(s) || !Finite3(p))
        return Transport::Bad;
    for (int axis = 0; axis < 3; ++axis)
    {
        out[axis] = p[axis];
        shift[axis] = s[axis];
    }
    return how;
}

// THE FIRE-TICK EYE's gate (Halo 3 on foot, a site with no eye of its own and no evaluator asked -- the published
// tick eye; the other two sources are this fire's by construction): the eye the title published is usable as E_fire
// only when it is the
// local player's (his unit's), proven to be the engine eye (EyeProof), and from THIS tick -- younger than three
// quarters of the measured simulation tick (25 ms while the tick is unmeasured), so a tick that skipped its
// acquisition never lends the shot the previous tick's eye. A seat never has one: there the published eye and the
// render's engine eye are not the same point by design (EyeProof).
enum class FireEye : uint8_t
{
    Measured = 0,   // usable
    NoSource,       // the title publishes no fire-tick eye (ODST), or its hooks are absent
    Seated,         // a seated shot: never measured
    NotRead,        // none readable for this generation
    Unproven,       // not (yet) proven to be the engine eye
    OtherUnit,      // published for another unit
    NotThisTick,    // older than this tick (or from the future)
    ChasePath,      // asked the evaluator: it took the chase-camera path (not a first-person eye)
    Faulted,        // asked the evaluator: it faulted or answered a non-finite eye
    CarriedTick,    // usable: this tick's published eye carried one measured tick ahead (CarryTickEye), because the
                    // evaluator could not be asked at the fire -- the acquisition's eye is the eye BEFORE the tick
                    // moves the unit, one tick behind the eye the shot leaves from
    Count
};
// A fire-eye verdict whose eye is E_fire.
inline bool FireEyeUsable(FireEye f) { return f == FireEye::Measured || f == FireEye::CarriedTick; }

// THE FIRE EYE IS AFTER THE TICK'S MOVE (Halo 3; measured in the unmodified flat game and in VR). Within a
// simulation tick the control pass runs the aim acquisition FIRST -- its camera evaluator answers the eye the last
// render showed (E_P: the muzzle transport measured 0 mm between them on every moving shot) -- then the unit moves, then
// its weapon fires: the shot leaves from, and the firing assist traces from, the eye one tick of travel AHEAD (stock
// flat: the round lands on the crosshair of the frame after the fire, and +1 tick of strafe off the crosshair the
// player fired through). So E_fire is the evaluator asked AT the fire; failing that, the acquisition's eye carried one
// measured tick, as the record's recomposition carries it (4b).
inline bool CarryTickEye(const float eye[3], const float* velocity, float tickS, float out[3])
{
    if (!Finite3(eye) || !out)
        return false;
    if (!velocity || !Finite3(velocity) || !std::isfinite(tickS) || !(tickS > 0.0f))
    {
        std::memcpy(out, eye, sizeof(float) * 3);
        return false;   // the eye itself: nothing to carry it by
    }
    return ExtrapolateEye(eye, velocity, tickS, 0.05f, out);
}
inline const char* FireEyeName(FireEye f)
{
    switch (f)
    {
    case FireEye::Measured: return "measured";
    case FireEye::NoSource: return "no source";
    case FireEye::Seated: return "seated";
    case FireEye::NotRead: return "not read";
    case FireEye::Unproven: return "unproven";
    case FireEye::OtherUnit: return "another unit's";
    case FireEye::NotThisTick: return "not this tick";
    case FireEye::ChasePath: return "chase path";
    case FireEye::Faulted: return "faulted";
    case FireEye::CarriedTick: return "this tick's eye carried a tick";
    default: return "?";
    }
}
inline FireEye FireEyeGate(bool read, bool proven, int32_t eyeUnit, int32_t localUnit, uint64_t stampUs,
                           uint64_t nowUs, uint64_t tickUs)
{
    if (!read)
        return FireEye::NotRead;
    if (!proven)
        return FireEye::Unproven;
    if (eyeUnit == -1 || eyeUnit != localUnit)
        return FireEye::OtherUnit;
    const uint64_t window = tickUs ? tickUs * 3 / 4 : 25000;
    if (!stampUs || nowUs < stampUs || nowUs - stampUs >= window)
        return FireEye::NotThisTick;
    return FireEye::Measured;
}

// ---- 4e. a camera evaluator asked AT the fire, for a title that builds P at its shot site (Halo 2, Halo CE) -------
// Halo 2 and CE publish no intent: their muzzle shot builds P itself from the newest camera sample (Halo 2's observer
// publication, CE's gameplay context) -- E_P is that sample's engine eye. Their fire routines each ask a camera
// evaluator for the firing assist's eye -- (unit, position, direction) -> perspective: Halo 2 0x6D4730 (H2EK 489E10,
// called at 0x7597B8), CE's director ray (called at 0xB67BDF) -- the homolog of Halo 3's 0x212198. Asked inside the
// fire, after the tick's move, its position is E_fire, the eye the round leaves from in flat.
//
// Neither its return value's meaning nor its agreement with E_P is proven for those titles, so it is PROVEN here, as
// Halo 3's tick eye is (EyeProof): at rest -- where the two are the same point by construction -- it must agree with
// E_P within 5 cm for a streak, and the perspective it answered on those agreeing samples is LATCHED as its
// first-person path. Only then, and only on that path, is its answer E_fire (Measured). A sample on another path is
// no evidence about the latched one (a death or third-person camera). Identity and agreement only: no size limit on
// the eye's motion, which is the very thing measured.
struct EvaluatorEyeProof
{
    EyeProof proof{};
    int32_t path = 0;
    bool pathKnown = false;

    // One sample: `eligible` (our unit, on foot, in gameplay -- the caller's identity facts), `atRest` (E_P's
    // velocity known and under 0.25 m/s), the evaluator's answer's distance from E_P, and the path it answered on.
    void Note(uint32_t gen, int32_t unit, bool eligible, bool atRest, float offsetMm, int32_t evaluatorPath)
    {
        if (gen != proof.generation)
            pathKnown = false;
        if (pathKnown && evaluatorPath != path)
            eligible = false;   // another camera path ends the stint; it says nothing about the latched one
        proof.Note(gen, unit, eligible, atRest, offsetMm);
        if (!pathKnown && eligible && atRest && std::isfinite(offsetMm) && offsetMm < EyeProof::kAgreeMm)
        {
            path = evaluatorPath;
            pathKnown = true;
        }
    }
    // The verdict on one answer: Measured only when proven and on the latched path.
    FireEye Verdict(int32_t evaluatorPath, bool finite) const
    {
        if (!finite)
            return FireEye::Faulted;
        if (!pathKnown || evaluatorPath != path)
            return pathKnown ? FireEye::ChasePath : FireEye::Unproven;
        return proof.state == EyeProof::State::Proven ? FireEye::Measured : FireEye::Unproven;
    }
};

// How far to carry E_P when no fire eye is measured (seconds): the model measured in flat Halo 3 (above) -- the
// shot leaves from the eye ONE TICK of travel past the eye the render showed (the firing tick is the first to run
// after that render) -- or the sample's own age when that is longer (renders stalled; the eye moved on regardless).
// NOT the age alone: a sample from the newest render (Halo 2's observer publication, CE's gameplay context, Halo 4's
// controller root) is a few ms old at the fire, and carrying it by that would move the shot by almost nothing -- an
// A/B arm with no effect (shot-placement.md 2). -1: the sample is unusable -- no stamp, from the future, or older than
// `maxAgeUs` (freshness, not a size limit).
inline float SampleCarrySeconds(uint64_t stampUs, uint64_t nowUs, float tickS, uint64_t maxAgeUs)
{
    if (!stampUs || nowUs < stampUs || nowUs - stampUs > maxAgeUs || !std::isfinite(tickS) || !(tickS > 0.0f))
        return -1.0f;
    const float ageS = float(nowUs - stampUs) * 1.0e-6f;
    return ageS > tickS ? ageS : tickS;
}

// E_fire for such a title: the evaluator's answer when its verdict is Measured; otherwise E_P carried `carryS`
// (SampleCarrySeconds) by its velocity: CarriedTick. `asked` is the evaluator's verdict (Count: not asked). Without a
// usable E_P (or carryS < 0: stale) the evaluator's own verdict, else NotRead / NotThisTick.
inline FireEye ChooseSampleFireEye(FireEye asked, const float askedEye[3], bool sampleValid, const float sampleEye[3],
                                   const float* velocity, float carryS, float out[3])
{
    if (asked == FireEye::Measured && askedEye && Finite3(askedEye) && out)
    {
        std::memcpy(out, askedEye, sizeof(float) * 3);
        return FireEye::Measured;
    }
    if (!sampleValid || !sampleEye || !Finite3(sampleEye) || !out)
        return asked == FireEye::Count ? FireEye::NotRead : asked;
    if (!std::isfinite(carryS) || carryS < 0.0f)
        return FireEye::NotThisTick;
    if (!velocity || !Finite3(velocity))
        std::memcpy(out, sampleEye, sizeof(float) * 3);   // at rest, or no velocity yet: the eye itself
    else
        (void)ExtrapolateEye(sampleEye, velocity, carryS, carryS, out);
    return FireEye::CarriedTick;
}

// A title's simulation tick period, measured by a per-tick hook (one call per unit per tick; several units come
// through one tick): a gap of over 2 ms is a new tick, one of 250 ms or more a stall (forget it). Smoothed 1/8.
// Returns the new period (us), or `prior` unchanged.
inline uint32_t SmoothTickPeriod(uint32_t prior, uint64_t gapUs)
{
    if (gapUs <= 2000 || gapUs >= 250000)
        return prior;
    const int64_t period = static_cast<int64_t>(gapUs);
    return prior ? static_cast<uint32_t>(static_cast<int64_t>(prior) + (period - static_cast<int64_t>(prior)) / 8)
                 : static_cast<uint32_t>(period);
}

// ---- 4f. MEASURED OR NOTHING (Halo Reach on foot) ---------------------------------------------------------------
// Reach's on-foot shot sites -- the unit-adjust re-aim, the crosshair-query re-aim and the muzzle shot -- aim through
// the provider's WORLD point P, so by the rule above they trail a strafe by E_fire - E_P unless Reach fires from the
// eye the render showed. Whether it does is not settled (no lag is visible in play, yet the native shot line measures
// 93 mm mean off the solve's eye while moving), so E_fire is MEASURED here
// or the shot keeps P: never carried a tick, never extrapolated. Its source is the stock unit camera-position evaluator
// (HREK 0xD76E60, retail 0x48A1C0) -- the camera the unit-adjust itself builds the native origin on (R-V12: origin :=
// camera + forward * dot(origin - camera, forward)) -- asked once per native fire, inside it, for our unit, after the
// tick's move. Its answer is E_fire only once EvaluatorEyeProof has proven it equal to E_P at rest. If Reach does fire
// from the render's eye, the measured shift is ~0 and the transport a no-op, not an over-correction.
//
// The verdict for an evaluator answer asked at this fire: Measured (out = the eye) only when `asked` is Measured and the
// eye finite; anything else is returned as it is (Count: not asked -> NotRead) and `out` is untouched.
inline FireEye ChooseMeasuredFireEye(FireEye asked, const float* askedEye, float out[3])
{
    if (asked == FireEye::Measured)
    {
        if (!askedEye || !Finite3(askedEye) || !out)
            return FireEye::Faulted;
        std::memcpy(out, askedEye, sizeof(float) * 3);
        return FireEye::Measured;
    }
    return asked == FireEye::Count ? FireEye::NotRead : asked;
}

// One native fire's evaluator answer, asked once and reused by every consumer inside that fire (a fire's sites run in
// a fixed order on one thread: the muzzle shot, the unit-adjust, then the firing assist's crosshair query). Opened
// empty at the fire's entry; `For` hands the eye only to the unit it was asked for.
struct FireEyeScope
{
    bool asked = false;
    bool answered = false;   // the evaluator returned a finite eye (false: not callable, faulted or non-finite)
    int32_t unit = -1;
    float eye[3]{};

    void Store(int32_t forUnit, bool called, const float* answer)
    {
        asked = true;
        unit = forUnit;
        answered = called && answer && Finite3(answer);
        if (answered)
            std::memcpy(eye, answer, sizeof(eye));
    }
    // NotRead: nothing asked in this fire; OtherUnit: asked for another unit; Faulted: no finite answer; else
    // Measured with `out` the eye (the caller's proof turns that into its own verdict).
    FireEye For(int32_t forUnit, float out[3]) const
    {
        if (!asked)
            return FireEye::NotRead;
        if (forUnit == -1 || forUnit != unit)
            return FireEye::OtherUnit;
        if (!answered || !out)
            return FireEye::Faulted;
        std::memcpy(out, eye, sizeof(eye));
        return FireEye::Measured;
    }
};

// E_P "at rest" for a proof sample: its velocity known and under 0.25 m/s (the same bar the transport witness uses).
inline bool EyeAtRest(bool velValid, const float vel[3])
{
    return velValid && Finite3(vel) && Length(vel) * kMetresPerWu < 0.25f;
}

// How far a point lies from a line (origin, direction of any length), in wu; NaN when undefined. The dev witness
// measures the fire eye against the native shot line with it: the unit-adjust builds its origin on the evaluator's
// camera line, so an answer that is the fire's camera sits ~0 off that line.
inline float PointLineDistance(const float point[3], const float origin[3], const float dir[3])
{
    if (!Finite3(point) || !Finite3(origin) || !Finite3(dir))
        return std::nanf("");
    const float length = Length(dir);
    if (!std::isfinite(length) || !(length > 1.0e-6f))
        return std::nanf("");
    const float d[3]{dir[0] / length, dir[1] / length, dir[2] / length};
    const float v[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const float along = Dot(v, d);
    const float p[3]{v[0] - d[0] * along, v[1] - d[1] * along, v[2] - d[2] * along};
    return Length(p);
}

// A report window of transports (one per title and consumer; dev): what was used, by kind, and the shift P' - P --
// along the player's travel (the engine eye's velocity with its component along the view removed; + = ahead of where
// he is going), its magnitude, and its magnitude AT REST (eye under 0.25 m/s: it should be ~0; anything else is an
// offset between the two eyes' sources, not motion). Plain sums, owned by the caller's lock.
struct TransportWindow
{
    uint32_t n[static_cast<size_t>(Transport::Count)]{};
    uint32_t fireEye[static_cast<size_t>(FireEye::Count)]{};
    uint32_t travel = 0;
    float alongSum = 0.0f, alongMax = 0.0f;   // mm
    uint32_t applied = 0;
    float magnitudeSum = 0.0f;                // mm
    uint32_t rest = 0;
    float restSum = 0.0f;                     // mm
    uint32_t fireAges = 0;
    float fireAgeSum = 0.0f;                  // ms, measured transports
    uint32_t pointAges = 0;
    float pointAgeSum = 0.0f;                 // ms, the camera sample's age at the fire

    // `fireEyeAgeMs` < 0: none. `pointEyeAgeMs` < 0: unknown. `fire`: the fire-eye verdict, or Count when none was
    // asked. `dir` (any length) and `vel` (wu/s, may be null) only split the shift.
    void Add(Transport how, FireEye fire, const float shift[3], const float dir[3], const float* vel,
             float fireEyeAgeMs, float pointEyeAgeMs)
    {
        const size_t h = static_cast<size_t>(how);
        if (h < static_cast<size_t>(Transport::Count))
            ++n[h];
        const size_t f = static_cast<size_t>(fire);
        if (f < static_cast<size_t>(FireEye::Count))
            ++fireEye[f];
        if (!TransportApplied(how) || !Finite3(shift))
            return;
        ++applied;
        const float magnitude = Length(shift) * kMmPerWu;
        magnitudeSum += magnitude;
        if (how == Transport::Measured && fireEyeAgeMs >= 0.0f && std::isfinite(fireEyeAgeMs))
        {
            ++fireAges;
            fireAgeSum += fireEyeAgeMs;
        }
        if (pointEyeAgeMs >= 0.0f && std::isfinite(pointEyeAgeMs))
        {
            ++pointAges;
            pointAgeSum += pointEyeAgeMs;
        }
        if (!vel || !Finite3(vel))
            return;
        if (Length(vel) * kMetresPerWu < 0.25f)
        {
            ++rest;
            restSum += magnitude;
        }
        const float dl = dir && Finite3(dir) ? Length(dir) : 0.0f;
        if (!(dl > 1.0e-6f))
            return;
        const float d[3]{dir[0] / dl, dir[1] / dl, dir[2] / dl};
        const float along = Dot(vel, d);
        const float vp[3]{vel[0] - d[0] * along, vel[1] - d[1] * along, vel[2] - d[2] * along};
        const float vpl = Length(vp);
        if (!std::isfinite(vpl) || !(vpl * kMetresPerWu > 0.05f))
            return;
        const float a = Dot(shift, vp) / vpl * kMmPerWu;
        if (!std::isfinite(a))
            return;
        ++travel;
        alongSum += a;
        if (std::fabs(a) > std::fabs(alongMax))
            alongMax = a;
    }
    uint32_t Total() const
    {
        uint32_t t = 0;
        for (uint32_t c : n)
            t += c;
        return t;
    }
    // "<label>: transported N (measured a, extrapolated b), P as published M (reasons); ..." Returns the characters
    // written (0: nothing to report).
    size_t Format(const char* label, char* out, size_t size) const
    {
        if (!out || !size || !Total())
            return 0;
        size_t used = 0;
        const auto put = [&](const char* fmt, auto... args) {
            if (used >= size)
                return;
            const int w = std::snprintf(out + used, size - used, fmt, args...);
            if (w > 0)
                used += static_cast<size_t>(w) < size - used ? static_cast<size_t>(w) : size - used - 1;
        };
        const auto mean = [](float sum, uint32_t count) { return count ? double(sum / float(count)) : 0.0; };
        put("%s: transported %u (measured %u, extrapolated %u), P as published %u", label, applied,
            n[static_cast<size_t>(Transport::Measured)], n[static_cast<size_t>(Transport::Extrapolated)],
            Total() - applied);
        bool any = false;
        for (size_t i = 0; i < static_cast<size_t>(Transport::Count); ++i)
        {
            if (TransportApplied(static_cast<Transport>(i)) || !n[i])
                continue;
            put("%s%s %u", any ? ", " : " (", TransportName(static_cast<Transport>(i)), n[i]);
            any = true;
        }
        if (any)
            put(")");
        if (applied)
        {
            put("; shift mean |%.0f| mm", mean(magnitudeSum, applied));
            if (travel)
                put(", along travel %+.0f mm (max %+.0f, %u moving)", mean(alongSum, travel), double(alongMax), travel);
            else
                put(", along travel n/a");
            if (rest)
                put(", at rest |%.0f| mm (%u)", mean(restSum, rest), rest);
            if (pointAges)
                put("; camera sample %.1f ms old", mean(pointAgeSum, pointAges));
            if (fireAges)
                put("; fire eye %.1f ms old", mean(fireAgeSum, fireAges));
        }
        uint32_t asked = 0;
        for (uint32_t c : fireEye)
            asked += c;
        if (asked)
        {
            put("; fire eye: measured %u", fireEye[static_cast<size_t>(FireEye::Measured)]);
            for (size_t i = 1; i < static_cast<size_t>(FireEye::Count); ++i)
                if (fireEye[i])
                    put(", %s %u", FireEyeName(static_cast<FireEye>(i)), fireEye[i]);
        }
        return used;
    }
};

// The crosshair query's own evidence that it is the local player's (with GateCrosshairQuery's fire tag): BY IDENTITY.
// On foot its first ignore is the local unit itself. Seated it is the vehicle's root (the published seat chain's top:
// `seatC`, else `seatB`) or the seat's parent (`seatB`). Another occupant's query names those too; the fire tag is
// what tells his fire from ours (an AI gunner's or a co-op player's fire is never kFireLocal), as in Reach's gate.
// Do NOT add a distance check (e.g. "the query must start within 1.5 m of our eye, carried forward at most 100 ms"):
// that is a size limit posing as identity. A Halo 3 Banshee starts its queries 2.3-2.4 m from the solve's eye, ahead
// along its travel, while they run along our own record (within 0.05 deg); carried forward that sits on a 1.5 m
// edge, and such a check leaves a whole stint's queries neither re-aimed nor counted.
// Where a seat's camera sits relative to our eye is the seat's geometry and timing, not whose query it is.
inline bool QueryIsLocal(bool seated, int32_t ignoredA, int32_t localUnit, int32_t seatB, int32_t seatC)
{
    if (ignoredA == -1)
        return false;
    if (!seated)
        return ignoredA == localUnit;
    const int32_t root = seatC != -1 ? seatC : seatB;
    return ignoredA == root || ignoredA == seatB;
}

// The game-basis angles of normalize(P - origin) (yaw about +Z from +X, pitch up), pitch clamped to
// +-maxPitchRad. Yaw is atan2's (-pi, pi]: the caller wraps it as it wraps every published angle. False when P is
// within minRangeWu of the origin or anything is non-finite.
inline bool AnglesTowardPoint(const float origin[3], const float point[3], float minRangeWu, float maxPitchRad,
                              float& yaw, float& pitch)
{
    if (!Finite3(origin) || !Finite3(point))
        return false;
    const float d[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const float length = Length(d);
    if (!std::isfinite(length) || !(length > minRangeWu))
        return false;
    float s = d[2] / length;
    s = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
    float p = std::asin(s);
    p = p > maxPitchRad ? maxPitchRad : (p < -maxPitchRad ? -maxPitchRad : p);
    const float y = std::atan2(d[1], d[0]);
    if (!std::isfinite(y) || !std::isfinite(p))
        return false;
    yaw = y;
    pitch = p;
    return true;
}

// The identity the record recomposition checks: the setpoint it is about to write IS the solve that built P
// (the same angles, wrap-insensitive), so a setpoint from any other path (the fixed-distance steer, a seat author)
// is never replaced.
inline bool SameAngles(float yawA, float pitchA, float yawB, float pitchB, float toleranceRad = 1.0e-4f)
{
    if (!std::isfinite(yawA) || !std::isfinite(pitchA) || !std::isfinite(yawB) || !std::isfinite(pitchB))
        return false;
    float dy = std::fmod(yawA - yawB, 6.283185307179586f);
    if (dy > 3.141592653589793f) dy -= 6.283185307179586f;
    if (dy < -3.141592653589793f) dy += 6.283185307179586f;
    return std::fabs(dy) <= toleranceRad && std::fabs(pitchA - pitchB) <= toleranceRad;
}

// ---- 4b. the record recomposed through the point (Halo 3 on foot) ---------------------------------------------
// The stick call's setpoint -- the record the HUD, autoaim, melee and the network read -- recomposed as the angles of
// normalize(P - E'): P the published intent point, E' the game's own eye at the stick call carried one simulation
// tick ahead (the record written now is first consumed by the next tick). Never P rebuilt from the current eye. Kept
// as published (fail closed) for every reason below, in this order; `yaw`/`pitch`/`bendDeg` are written only on
// Recomposed. No magnitude limit (see ReaimRails): the bend is legitimately large at a seat entry -- the game's eye
// flies metres toward the seat while the record is still the on-foot one, so the bend grows as P comes close -- and
// at a mission's end. Measured in Halo 3 Forge under a fixed 30 deg cap from the setpoint: 25.7 deg applied and 43
// writes refused, every one at such a moment. Plain on-foot windows bend at most 6.8 deg.
enum class Recompose : uint8_t
{
    Recomposed = 0,
    NotOnFoot,      // not Halo 3's on-foot stick call, its publication invalid or seated, or the converged aim off
    NoIntent,       // nothing published
    OtherSolve,     // the setpoint is not the solve that built P (the fixed-distance steer, a seat author)
    PointUnusable,  // another generation, stale (or from the future), seated, held at the pitch rail, or non-finite
    NotFirstPerson, // the camera is not our unit's first-person eye (4d): Forge's Monitor or orbit camera, a chase
                    // or scripted camera, a seat, a cinematic -- an eye there is not the one P was solved from
    NoGameEye,      // the game's eye at the tick is unknown, or not from this tick
    EyeUnproven,    // that eye is not (yet) proven to be the engine eye (EyeProof)
    PointTooClose,  // P within 30 cm of the eye (or non-finite): it defines no direction
    PointBehind,    // P not ahead of the eye along the setpoint: no forward line through it
    Count
};
struct RecomposeInput
{
    bool onFoot = false;
    bool intentRead = false;
    IntentPoint intent{};
    float setYaw = 0.0f, setPitch = 0.0f;   // the setpoint about to be written
    uint32_t generation = 0;                // the title's current generation (0 = none)
    uint64_t nowUs = 0, maxAgeUs = 0;
    bool firstPersonEye = false;            // 4d: our unit on foot in control, the director's camera first person
    bool eyeRead = false;                   // the game's eye at this tick (published since the previous stick call)
    float eye[3]{}, eyeVel[3]{};
    bool eyeVelValid = false;
    bool eyeProven = false;
    float tickS = 0.0f;                     // the measured simulation tick; the carry is clamped to 50 ms
    // Dev key shot_point_transport (4a): recompose through P' = P + (E' - E_P) instead of P -- E' the game's eye
    // carried a tick ahead (below), measured. Where P's camera sample is unknown, through P as published.
    bool transport = false;
};
// `how` (optional): the transport's verdict, Transport::Count when none was asked; `shift` (optional): P' - P when
// applied. Both written only on Recomposed.
inline Recompose RecomposeRecord(const RecomposeInput& in, float& yaw, float& pitch, float& bendDeg,
                                 Transport* how = nullptr, float* shift = nullptr)
{
    if (!in.onFoot)
        return Recompose::NotOnFoot;
    if (!in.intentRead)
        return Recompose::NoIntent;
    const IntentPoint& p = in.intent;
    if (!SameAngles(in.setYaw, in.setPitch, p.solvedYaw, p.solvedPitch))
        return Recompose::OtherSolve;
    if (!in.generation || p.generation != in.generation || p.seated || p.railHeld || !p.stampUs ||
        in.nowUs < p.stampUs || in.nowUs - p.stampUs > in.maxAgeUs || !Finite3(p.point))
        return Recompose::PointUnusable;
    if (!in.firstPersonEye)
        return Recompose::NotFirstPerson;
    if (!in.eyeRead || !Finite3(in.eye))
        return Recompose::NoGameEye;
    if (!in.eyeProven)
        return Recompose::EyeUnproven;
    float fire[3]{in.eye[0], in.eye[1], in.eye[2]};
    if (in.eyeVelValid)
        (void)ExtrapolateEye(in.eye, in.eyeVel, in.tickS, 0.05f, fire);
    float target[3]{p.point[0], p.point[1], p.point[2]};
    float moved[3]{};
    Transport transported = Transport::Count;
    if (in.transport)
    {
        TransportInput t{};
        std::memcpy(t.point, p.point, sizeof(t.point));
        t.pointEyeValid = p.pointEyeValid;
        std::memcpy(t.pointEye, p.pointEye, sizeof(t.pointEye));
        t.pointEyeStampUs = p.pointEyeStampUs;
        t.fireEyeValid = true;
        std::memcpy(t.fireEye, fire, sizeof(t.fireEye));
        t.nowUs = in.nowUs;
        float out[3]{};
        transported = TransportPoint(t, out, moved);
        if (TransportApplied(transported))
            std::memcpy(target, out, sizeof(target));
    }
    float newYaw = 0.0f, newPitch = 0.0f;
    if (!AnglesTowardPoint(fire, target, 0.3f * kWuPerMetre, 1.45f, newYaw, newPitch))
        return Recompose::PointTooClose;
    const float a[3]{std::cos(in.setPitch) * std::cos(in.setYaw), std::cos(in.setPitch) * std::sin(in.setYaw),
                     std::sin(in.setPitch)};
    const float b[3]{std::cos(newPitch) * std::cos(newYaw), std::cos(newPitch) * std::sin(newYaw), std::sin(newPitch)};
    // Behind the eye along the setpoint (the eye has passed P): no forward line through it.
    const float toPoint[3]{target[0] - fire[0], target[1] - fire[1], target[2] - fire[2]};
    if (!(Dot(toPoint, a) > 0.0f))
        return Recompose::PointBehind;
    float c = Dot(a, b);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    const float bend = std::acos(c) * kRadToDeg;
    yaw = newYaw;
    pitch = newPitch;
    bendDeg = bend;
    if (how)
        *how = transported;
    if (shift && TransportApplied(transported))
        std::memcpy(shift, moved, sizeof(moved));
    return Recompose::Recomposed;
}

// ---- 4c. the game's eye when its acquisition did not publish one (Halo 3 on foot) --------------------------------
// The record's eye (4b) is published from the evaluator call inside Halo 3's aim acquisition (0x13B08C, called at
// 0xF4CC8 from the control update's targeting pass 0xF42D8). Two things stop it publishing, both the game's own:
//   * the game SKIPS the acquisition: `call 0xF4060; test al, al; je` at 0xF48C9 (0xF4060 says no when a local input
//     user's control flag, +0x18 bit 0 of its 0x30-byte record at TLS +0xC0, is set, or 0xF3710 holds);
//   * the acquisition RUNS WITHOUT ITS EVALUATOR: it asks the evaluator (0x13B1D7) only when 0x13C518 accepts the
//     aiming unit (0x355CD4: the unit itself on foot), and 0x13C518 answers false at once when the unit has no
//     current weapon (0x356388 on unit +0x262 at 0x13C58B; -1 jumps to its false return 0x13C8ED). An unarmed unit
//     -- Forge's Monitor -- never reaches it: the game itself does not aim it, so its eye is NOT asked for
//     (GameDeclined).
//     Measured as Forge's Monitor: ~18 of 122 stick calls per 2 s miss this way. Do not ask the evaluator directly
//     for such a unit: each answer would stand for up to 100 ms (the freshness window) and bend the record by up to
//     88 deg while the Monitor holds and spawns objects.
// Read from the pinned module B209D845...EFB63, offline. When the game skipped the acquisition and the eye is
// otherwise wanted, it is asked of the same evaluator directly at the stick call -- only for a first-person eye (4d).
// The evaluator returns its camera path: 0 = the first-person update 0x17DAEC, else the following (chase) camera
// 0x2144C0 (0x2121B8..0x2121D2, the path 0x1326E8 chose). Only a first-person-path eye is ever published, from the
// acquisition or from here. Each miss is counted by cause on the SHOT RECORD line, whichever way it ends.
enum class TickEyeMiss : uint8_t
{
    FromEvaluator = 0,  // asked the evaluator directly: the eye is published
    NoHook,             // the acquisition / evaluator hooks are not installed, disabled, faulted or another generation's
    NoUnit,             // no local unit
    NotFirstPersonEye,  // 4d said no: a seat, a cinematic, the Monitor's or an orbit / chase camera; not asked
    GameDeclined,       // the acquisition ran without its evaluator (no weapon to aim): not asked
    ThirdPersonPath,    // asked, and the evaluator took the chase-camera path: nothing published
    EvaluatorFault,     // the direct call faulted or answered a non-finite eye
    Count
};
inline const char* TickEyeMissName(TickEyeMiss why)
{
    switch (why)
    {
    case TickEyeMiss::FromEvaluator: return "asked the evaluator directly";
    case TickEyeMiss::NoHook: return "hooks absent";
    case TickEyeMiss::NoUnit: return "no unit";
    case TickEyeMiss::NotFirstPersonEye: return "not a first-person eye";
    case TickEyeMiss::GameDeclined: return "the game's acquisition declined the unit (nothing to aim)";
    case TickEyeMiss::ThirdPersonPath: return "the evaluator took the chase-camera path";
    case TickEyeMiss::EvaluatorFault: return "evaluator faulted";
    default: return "?";
    }
}
// Why the acquisition did not publish, for the same line: it did not run for input user 0 since the previous stick
// call (the game skipped it), or it ran and never reached the evaluator. `windowUs` is the time since that call.
enum class TickEyeAcquisition : uint8_t { Skipped = 0, RanWithoutEvaluator, Count };
inline TickEyeAcquisition ClassifyTickEyeAcquisition(uint64_t lastAcquisitionUs, uint64_t nowUs, uint64_t windowUs)
{
    return lastAcquisitionUs && nowUs >= lastAcquisitionUs && nowUs - lastAcquisitionUs <= windowUs
               ? TickEyeAcquisition::RanWithoutEvaluator
               : TickEyeAcquisition::Skipped;
}
// The record is recomposed from THIS tick's eye only: one published since the previous stick call. A held-over eye
// (a 100 ms freshness window alone would let one stand for six ticks) is off by everything the eye did since.
inline bool TickEyeFromThisTick(bool read, uint64_t stampUs, uint64_t previousStickUs)
{
    return read && stampUs && stampUs > previousStickUs;
}
struct TickEyeFallbackInput
{
    bool hooked = false;            // the evaluator's trampoline is live, the lane enabled and not faulted
    bool sameGeneration = false;    // the hooks were installed for this generation
    int32_t unit = -1;              // the local player's unit
    bool firstPersonEye = false;    // 4d: on foot, in control, the director's camera first person
    TickEyeAcquisition acquisition = TickEyeAcquisition::Skipped;
};
// FromEvaluator: ask. Anything else: the reason not to.
inline TickEyeMiss TickEyeFallbackGate(const TickEyeFallbackInput& in)
{
    if (!in.hooked || !in.sameGeneration)
        return TickEyeMiss::NoHook;
    if (in.unit == -1)
        return TickEyeMiss::NoUnit;
    if (!in.firstPersonEye)
        return TickEyeMiss::NotFirstPersonEye;
    if (in.acquisition == TickEyeAcquisition::RanWithoutEvaluator)
        return TickEyeMiss::GameDeclined;
    return TickEyeMiss::FromEvaluator;
}
// The evaluator's own answer for which camera it evaluated (0x212198 returns 0x1326E8's result): 0 = first person.
inline bool EvaluatorTookFirstPersonPath(int32_t evaluatorResult) { return evaluatorResult == 0; }

// ---- 4d. is the eye our unit's FIRST-PERSON eye? (Halo 3; the identity the record and the direct ask require) ----
// The recompose bends the record through P from the game's eye; that is only right when the eye is the camera P was
// solved from: our unit's first-person eye. Without this identity the record of Forge's Monitor is bent by up to
// 88 deg while it spawns and holds objects, and since the Monitor's aim carries what it holds, the held object and
// the player's position are thrown about. The identity is the game's own, read where the game keeps it:
//
//   THE DIRECTOR'S CAMERA for the output user. set_camera_mode (0x131B40) stores, every time a director changes
//   camera, the camera's PERSPECTIVE (c_director::get_perspective 0x131A00; 0 = first person, 1 third, 2 scripted,
//   3 neutral) at +0x604 and its MODE at +0x608 of the user's 0xC-byte record in the block at engine TLS +0x188
//   (0x131C44..0x131C8F). The mode is the camera's type: each case of set_camera_mode's switch (0x131B96) builds a
//   camera whose vtable's get_type returns that case's number (read offline: 0 -> 0x7FB748, 1 -> 0x7FB7B0, 3 ->
//   0x7961D0, 4..7 -> 0x7FB888/0x7FB8F0/0x7FB548/0x7FB958). Named after Halo 3's camera-mode enum: 0 following,
//   1 orbiting, 2 flying, 3 first person, 4 dead, 5 static, 6 scripted, 7 authored. Only type 3's get_perspective
//   is a constant 0 (first person); 0, 1 and 4 return 1 (third), 5 returns 3 (neutral), 6 and 7 decide at run
//   time. Directors do switch to the orbit: one (0x2121F0, 0x212491..0x21259B) turns its flying camera into mode 1
//   around the biped it looks at. Which mode Forge's Monitor and its spawn camera use is not read offline; the
//   SHOT RECORD first-person line counts the director's mode at every refusal, so a log names it. The
//   engine's own first-person gate is this same field: the first-person model cache (0x28ADDC) builds the view
//   model and names our unit the first-person camera object only when +0x604 is 0 (0x28AE31).
//   ADDR-HYGIENE: resolved. Nothing is assumed: the TLS index, the block offset, both field offsets and the user
//   stride are decoded from TWO independent sites of the pinned module, each matched uniquely, which must agree
//   (DecodeDirectorCameraSites below); the runtime read is then range-checked (DirectorCameraPlausible). A build
//   where they do not match or agree resolves nothing: no identity, the record keeps its solved angles, said once.
//
// Plus the unit itself: our unit, on foot (no seat word) and player-controlled (no cinematic).
inline constexpr int32_t kHalo3CameraModeFirstPerson = 3;
inline constexpr int32_t kHalo3CameraModeCount = 8;
inline constexpr int32_t kHalo3PerspectiveFirstPerson = 0;
inline constexpr int32_t kHalo3PerspectiveCount = 4;
inline bool DirectorCameraPlausible(int32_t mode, int32_t perspective)
{
    return mode >= 0 && mode < kHalo3CameraModeCount && perspective >= 0 && perspective < kHalo3PerspectiveCount;
}
inline const char* Halo3CameraModeName(int32_t mode)
{
    static const char* const names[kHalo3CameraModeCount]{"following", "orbiting",  "flying",   "first person",
                                                            "dead",      "static",    "scripted", "authored"};
    return mode >= 0 && mode < kHalo3CameraModeCount ? names[mode] : "?";
}
enum class FirstPersonEye : uint8_t
{
    Yes = 0,
    NotOnFootControlled,   // no unit, a seat word, or a cinematic / scripted control
    DirectorUnread,        // the director's record is not bound, not readable on this thread, or out of range
    NotFirstPersonCamera,  // the director's camera is not the first-person camera, or its perspective is not first
    Count
};
inline const char* FirstPersonEyeName(FirstPersonEye why)
{
    switch (why)
    {
    case FirstPersonEye::Yes: return "a first-person eye";
    case FirstPersonEye::NotOnFootControlled: return "not on foot in control";
    case FirstPersonEye::DirectorUnread: return "director camera unread";
    case FirstPersonEye::NotFirstPersonCamera: return "camera not first person";
    default: return "?";
    }
}
struct FirstPersonEyeInput
{
    bool onFootControlled = false;  // our unit, no seat word, player-controlled
    bool directorRead = false;      // the director's record read (and plausible)
    int32_t mode = -1, perspective = -1;
};
inline FirstPersonEye ClassifyFirstPersonEye(const FirstPersonEyeInput& in)
{
    if (!in.onFootControlled)
        return FirstPersonEye::NotOnFootControlled;
    if (!in.directorRead || !DirectorCameraPlausible(in.mode, in.perspective))
        return FirstPersonEye::DirectorUnread;
    if (in.mode != kHalo3CameraModeFirstPerson || in.perspective != kHalo3PerspectiveFirstPerson)
        return FirstPersonEye::NotFirstPersonCamera;
    return FirstPersonEye::Yes;
}

// The two sites the director's record is decoded from (halo3.dll, matched uniquely in the whole module):
//   CACHE  0x28AE09, the first-person model cache's gate:  mov ebp, BLOCK / ... / mov ecx, [rip + TLS index] /
//          gs:[0x58] / slots[index] / lea rcx, [user*3] / [tls + BLOCK] / cmp dword [rax + rcx*4 + PERSPECTIVE], 0
//   SETMODE 0x131C44, set_camera_mode's store:  gs:[0x58] / mov r8d, BLOCK / slots[r15] / [tls + BLOCK] /
//          movsxd rcx, [director + USER] / lea r8, [user*3] / cmp [rdx + r8*4 + PERSPECTIVE], eax / jne /
//          cmp [rdx + r8*4 + MODE], ebp / je / mov [rdx + r8*4 + PERSPECTIVE], eax
// Both index the per-user record as user*3*4 (0xC); the patterns fix that stride and every register.
inline constexpr const char* kHalo3DirectorCacheSitePattern =
    "BD ?? ?? ?? ?? 3B C6 75 ?? 3B DE 74 ?? 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 48 8B 14 C8 48 8D 0C 5B "
    "48 8B 04 2A 83 BC 88 ?? ?? ?? ?? 00 75";
inline constexpr const char* kHalo3DirectorSetModeSitePattern =
    "65 48 8B 0C 25 58 00 00 00 41 B8 ?? ?? ?? ?? 4A 8B 0C F9 49 8B 14 08 48 63 8E ?? ?? ?? ?? 4C 8D 04 49 "
    "42 39 84 82 ?? ?? ?? ?? 75 ?? 42 39 AC 82 ?? ?? ?? ?? 74 ?? 42 89 84 82 ?? ?? ?? ??";
inline constexpr size_t kDirectorCacheSiteBytes = 49, kDirectorSetModeSiteBytes = 62;
inline constexpr size_t kDirectorCacheBlock = 1, kDirectorCacheTlsDisp = 15, kDirectorCacheTlsNext = 19,
                        kDirectorCachePerspective = 43;
inline constexpr size_t kDirectorSetModeBlock = 11, kDirectorSetModeUser = 26, kDirectorSetModePerspective = 38,
                        kDirectorSetModeMode = 48, kDirectorSetModePerspectiveStore = 58;
inline constexpr uint32_t kDirectorUserStride = 0xC;
struct DirectorCameraSites
{
    uint32_t blockOffset = 0;       // the per-user camera block's pointer in engine TLS (0x188 on the pinned build)
    uint32_t perspectiveOffset = 0; // the perspective field in the block, user 0 (0x604)
    uint32_t modeOffset = 0;        // the mode field (0x608)
    int32_t tlsIndexDisp = 0;       // the CACHE site's rip-relative TLS index: target = site + 19 + disp
};
// True (and `out` filled) only when both sites agree: the same block, the same perspective field at all three uses,
// the mode field right after it inside the 0xC record, and every value a small aligned offset.
inline bool DecodeDirectorCameraSites(const uint8_t* cache, const uint8_t* setMode, DirectorCameraSites& out)
{
    if (!cache || !setMode)
        return false;
    const auto u32 = [](const uint8_t* at) {
        uint32_t v = 0;
        std::memcpy(&v, at, sizeof(v));
        return v;
    };
    const uint32_t block = u32(cache + kDirectorCacheBlock), persp = u32(cache + kDirectorCachePerspective);
    const uint32_t block2 = u32(setMode + kDirectorSetModeBlock), persp2 = u32(setMode + kDirectorSetModePerspective);
    const uint32_t mode = u32(setMode + kDirectorSetModeMode), perspStore = u32(setMode + kDirectorSetModePerspectiveStore);
    if (block != block2 || persp != persp2 || persp != perspStore || mode != persp + 4)
        return false;
    if (!block || block >= 0x1000 || (block & 7) || !persp || persp >= 0x10000 || (persp & 3) ||
        mode + 4 - persp > kDirectorUserStride)
        return false;
    out.blockOffset = block;
    out.perspectiveOffset = persp;
    out.modeOffset = mode;
    out.tlsIndexDisp = static_cast<int32_t>(u32(cache + kDirectorCacheTlsDisp));
    return true;
}

// ---- 5. the frame-coherent pair --------------------------------------------------------------------------------
// A render publishes the eyes it rendered from, keyed by the tracking-space head position its head look used; the
// aim path keeps the hand directions it computed, keyed by the head position of the sample the hand came from.
// The same head sample on both sides is the same prepared frame. The pair is the NEWEST hand whose key matches an
// eye exactly (the newest such eye when several share it: a frozen head).
struct EyeFrame
{
    bool valid = false;
    float key[3]{};
    float head[3]{};   // H: the rendered eye
    float eye[3]{};    // E: the engine eye under it
    uint64_t stampUs = 0;
    uint64_t serial = 0;
};
struct HandFrame
{
    bool valid = false;
    float key[3]{};
    float dir[3]{};    // u, world
    uint64_t stampUs = 0;
    uint64_t serial = 0;
};

inline bool SameKey(const float a[3], const float b[3])
{
    return Finite3(a) && Finite3(b) && a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

// Indices into the two arrays, or false. `maxSkewUs`: a hand newer than its eye by more than this is refused (a
// key that repeats after a long freeze is not the same frame). `maxHandAgeUs` (with `nowUs`; 0 = unbounded): a hand
// older than this at `nowUs`, or stamped after it, is refused -- during a render stall the newest hand whose frame
// rendered can be several polls old, and a pair built from it is the player's aim of that moment, not of now. The
// caller stamps the published point with the chosen HAND's stamp, so its consumers age it from when it was meant.
inline bool SelectCoherentPair(const EyeFrame* eyes, size_t eyeCount, const HandFrame* hands, size_t handCount,
                               uint64_t maxSkewUs, size_t& eyeIndex, size_t& handIndex, uint64_t nowUs = 0,
                               uint64_t maxHandAgeUs = 0)
{
    bool found = false;
    uint64_t bestHand = 0, bestEye = 0;
    for (size_t h = 0; h < handCount; ++h)
    {
        if (!hands[h].valid || !hands[h].stampUs || (found && hands[h].stampUs < bestHand))
            continue;
        if (maxHandAgeUs && (hands[h].stampUs > nowUs || nowUs - hands[h].stampUs > maxHandAgeUs))
            continue;
        for (size_t e = 0; e < eyeCount; ++e)
        {
            if (!eyes[e].valid || !SameKey(eyes[e].key, hands[h].key))
                continue;
            const uint64_t a = eyes[e].stampUs, b = hands[h].stampUs;
            const uint64_t skew = a > b ? a - b : b - a;
            if (skew > maxSkewUs)
                continue;
            if (!found || hands[h].stampUs > bestHand || (hands[h].stampUs == bestHand && a > bestEye))
            {
                found = true;
                bestHand = hands[h].stampUs;
                bestEye = a;
                eyeIndex = e;
                handIndex = h;
            }
        }
    }
    return found;
}

// ---- 6. the witness: where did the shot go, relative to P, and why ------------------------------------------
// One shot: its line (origin O, direction d) against P. The miss m = (closest point of the line to P) - P, split
// along the player's travel (v with its component along the shot removed: the axis a parallel shift moves the
// line along) and across it. The stale-eye term: the shot's origin minus the eye the solve used, perpendicular to
// the shot and signed along that same travel -- what a solve at the wrong eye costs -- and the dt it implies.
struct ShotLagSample
{
    bool valid = false;
    float missMm = 0.0f;
    float alongMm = 0.0f, acrossMm = 0.0f;   // along: signed (+ = ahead of where the player is going)
    float rangeM = 0.0f;                     // |P - O|
    float speedMps = 0.0f;
    bool travel = false;                     // the velocity had a usable component across the shot
    bool velocityKnown = false;
    float staleAlongMm = 0.0f;               // valid with `stale`
    float impliedDtMs = 0.0f;                // valid with `stale && travel`
    bool stale = false;
    int bucket = -1;                         // ShotLagBucket(speedMps), -1 without a velocity
};

inline constexpr int kShotLagBuckets = 4;
inline const char* ShotLagBucketName(int bucket)
{
    switch (bucket)
    {
    case 0: return "0-1 m/s";
    case 1: return "1-4";
    case 2: return "4-10";
    case 3: return "10+";
    default: return "?";
    }
}
inline int ShotLagBucket(float speedMps)
{
    if (!std::isfinite(speedMps) || speedMps < 0.0f)
        return -1;
    return speedMps < 1.0f ? 0 : speedMps < 4.0f ? 1 : speedMps < 10.0f ? 2 : 3;
}

// `velocity` (wu/s) may be null (unknown); `solveEye` may be null (no stale-eye term).
inline ShotLagSample MeasureShotLag(const float origin[3], const float direction[3], const float point[3],
                                    const float* solveEye, const float* velocity)
{
    ShotLagSample s{};
    if (!Finite3(origin) || !Finite3(direction) || !Finite3(point))
        return s;
    const float dl = Length(direction);
    if (!std::isfinite(dl) || !(dl > 1.0e-6f))
        return s;
    const float d[3]{direction[0] / dl, direction[1] / dl, direction[2] / dl};
    const float w[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const float t = Dot(w, d);
    const float m[3]{origin[0] + d[0] * t - point[0], origin[1] + d[1] * t - point[1],
                     origin[2] + d[2] * t - point[2]};
    s.missMm = Length(m) * kMmPerWu;
    s.rangeM = Length(w) * kMetresPerWu;
    s.acrossMm = s.missMm;
    float a[3]{};
    if (velocity && Finite3(velocity))
    {
        s.velocityKnown = true;
        s.speedMps = Length(velocity) * kMetresPerWu;
        s.bucket = ShotLagBucket(s.speedMps);
        const float along = Dot(velocity, d);
        const float vp[3]{velocity[0] - d[0] * along, velocity[1] - d[1] * along, velocity[2] - d[2] * along};
        const float vpl = Length(vp);
        // Below 5 cm/s across the shot there is no travel axis to sign against.
        if (std::isfinite(vpl) && vpl * kMetresPerWu > 0.05f)
        {
            s.travel = true;
            a[0] = vp[0] / vpl; a[1] = vp[1] / vpl; a[2] = vp[2] / vpl;
            const float ma = Dot(m, a);
            const float r[3]{m[0] - a[0] * ma, m[1] - a[1] * ma, m[2] - a[2] * ma};
            s.alongMm = ma * kMmPerWu;
            s.acrossMm = Length(r) * kMmPerWu;
            if (solveEye && Finite3(solveEye))
            {
                const float se[3]{origin[0] - solveEye[0], origin[1] - solveEye[1], origin[2] - solveEye[2]};
                s.stale = true;
                s.staleAlongMm = Dot(se, a) * kMmPerWu;   // the along-shot part is orthogonal to a already
                s.impliedDtMs = Dot(se, a) / vpl * 1000.0f;
            }
        }
    }
    s.valid = std::isfinite(s.missMm) && std::isfinite(s.rangeM) && std::isfinite(s.alongMm) &&
              std::isfinite(s.acrossMm);
    return s;
}

// A report window (one per title): plain sums, owned by whoever holds the title's witness lock.
struct ShotLagWindow
{
    uint32_t shots = 0, unmeasured = 0;
    uint32_t n[kShotLagBuckets]{};
    float missSum[kShotLagBuckets]{}, alongSum[kShotLagBuckets]{}, acrossSum[kShotLagBuckets]{};
    uint32_t noVelocity = 0;
    float noVelocityMissSum = 0.0f;
    uint32_t travelShots = 0;
    float alongTravelSum = 0.0f;
    uint32_t staleShots = 0;
    float staleAlongSum = 0.0f;
    uint32_t dtShots = 0;
    float impliedDtSum = 0.0f;
    float rangeSum = 0.0f;
    bool seated = false;
    int32_t seat = -1;

    void Add(const ShotLagSample& s, bool seatedNow, int32_t seatId)
    {
        if (!s.valid)
        {
            ++unmeasured;
            return;
        }
        ++shots;
        rangeSum += s.rangeM;
        seated = seatedNow;
        seat = seatId;
        if (s.bucket >= 0 && s.bucket < kShotLagBuckets)
        {
            ++n[s.bucket];
            missSum[s.bucket] += s.missMm;
            alongSum[s.bucket] += s.alongMm;
            acrossSum[s.bucket] += s.acrossMm;
        }
        else
        {
            ++noVelocity;
            noVelocityMissSum += s.missMm;
        }
        if (s.travel)
        {
            ++travelShots;
            alongTravelSum += s.alongMm;
        }
        if (s.stale)
        {
            ++staleShots;
            staleAlongSum += s.staleAlongMm;
            // A dt implied by under 1 m/s of travel is noise.
            if (s.travel && s.speedMps >= 1.0f && std::isfinite(s.impliedDtMs))
            {
                ++dtShots;
                impliedDtSum += s.impliedDtMs;
            }
        }
    }

    // "SHOT LAG <title> (dev): ..." -- the whole line. Returns the characters written (0 when there is nothing to
    // report).
    size_t Format(const char* title, char* out, size_t size) const
    {
        if (!out || !size || (!shots && !unmeasured))
            return 0;
        size_t used = 0;
        const auto put = [&](const char* fmt, auto... args) {
            if (used >= size)
                return;
            const int w = std::snprintf(out + used, size - used, fmt, args...);
            if (w > 0)
                used += static_cast<size_t>(w) < size - used ? static_cast<size_t>(w) : size - used - 1;
        };
        put("SHOT LAG %s (dev): %u shots", title, shots);
        if (unmeasured)
            put(" (+%u unmeasured: no fresh intent point)", unmeasured);
        for (int b = 0; b < kShotLagBuckets; ++b)
        {
            put(b == 0 ? "; speed %s: %u" : "; %s: %u", ShotLagBucketName(b), n[b]);
            if (n[b])
                put(", miss %.0f mm (along %+.0f, across %.0f)", double(missSum[b] / float(n[b])),
                    double(alongSum[b] / float(n[b])), double(acrossSum[b] / float(n[b])));
        }
        if (noVelocity)
            put("; speed unknown: %u, miss %.0f mm", noVelocity, double(noVelocityMissSum / float(noVelocity)));
        if (travelShots)
            put("; mean lag axis along travel %+.0f mm", double(alongTravelSum / float(travelShots)));
        else
            put("; mean lag axis along travel n/a");
        if (staleShots)
        {
            put("; stale eye along travel %+.0f mm", double(staleAlongSum / float(staleShots)));
            if (dtShots)
                put(" (implied dt %.0f ms)", double(impliedDtSum / float(dtShots)));
            else
                put(" (implied dt n/a)");
        }
        else
            put("; stale eye n/a");
        if (shots)
            put("; at range %.1f m", double(rangeSum / float(shots)));
        put("; seated=%d seat=%d", seated ? 1 : 0, int(seat));
        return used;
    }
};

} // namespace shot_lag
