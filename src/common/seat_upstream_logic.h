#pragma once

// SEATED AIM UPSTREAM OF REPLICATION (Halo 3). In MCC online play a client sends its control record --
// the per-user desired yaw/pitch the direct drive writes, plus buttons -- to the host, which may run NO mod and
// fires the authoritative shot with the stock game from that record. Every correction at the FIRE SITE (re-aiming the
// firing assist's crosshair query through P, Sight_ReaimThroughPoint) changes only this machine's prediction. So the
// record itself is chosen here so that the STOCK game's own crosshair query at the fire tick already passes through
// P. A seat whose tag this machine writes (the dev A/B) keeps the fire-site re-aim as its local backstop. A seat
// whose tag is as loaded is never re-aimed at the fire site: there the record is all there is, so its line's start
// has to be in hand at every control pass (section 4b). The dev SHOT UPSTREAM line measures what is left, in mm and
// degrees.
//
// THE STOCK QUERY, read from the pinned halo3.dll (B209D845...EFB63, offline):
//   * the firing assist 0x13BAD0 takes its line from the camera evaluator 0x212198 (called at 0x13BC08): the line
//     starts at the camera and runs along the camera's direction (0x13BE78..0x13BF8C: the query starts on that line
//     at the shot origin's projection, and runs 128 wu along it -- only the LINE matters for where it points);
//   * the evaluator's first-person path 0x17DAEC answers position = the unit's camera position (0x3555EC) and
//     direction = the OCCUPANT's aiming vector, unit +0x1AC (0x17DB44..0x17DB56) -- except a seat whose flags carry
//     bit 7 (gun-slaved, the Warthog turret, the Gauss), whose camera is the parent's marker (0x343D74, both
//     position and direction: 0x17DBE3..0x17DC29). That line is the GUN's, and it is not modelled here;
//   * the control record reaches the occupant's desired aiming (+0x1A0) through 0xE8DD8 -> 0xE3A48 (angles to a
//     vector, then the seat rebase 0x355E20, a pure rotation C20 skips) -> unit_control 0x350ED4 (a plain copy):
//     no velocity term anywhere on that path. The aiming vector then follows the desired one (0x34FA88).
//   * the THIS TICK camera is what the native acquisition 0x13B08C asks the same evaluator for (its call at
//     0x13B1D7, return 0x13B1DC) in the control pass that runs just before the stick call -- for a seated unit too:
//     the aiming unit 0x355CD4 is then the vehicle (seat flags bit 0 or 3), which carries the gun 0x13C518 checks.
//
// THE VELOCITY TERM (a fit, the place it is applied not found in the module). 333 captured Hornet queries
// put the stock query along normalize(R - g * v), R the record's unit
// vector, v the camera's velocity: the deviation from R points against v's part across R (cosine median 0.999, p10
// 0.997 over 102 moving queries in one Hornet stint), with a gain g that is steady within a stint over 4..20 m/s
// (1/g ~ 190 m/s there; ~440 m/s and ~150 m/s in two other stints; ~880 m/s in a Banshee-like one). So g is LEARNED
// per seat from the game's own answers (GainFit), every control pass, from the acquisition's camera direction and
// the record that drove it, never assumed. An unlearned seat uses g = 0: the record is then still solved from the
// tick's own camera, which already removes the eye-travel and camera-offset parallax.
//
// Pure functions and plain structs only: unit-tested in tests/seat_upstream_tests.cpp. World units are Blam's (1 wu
// = 3.048 m); velocities wu/s; times microseconds.

#include "shot_lag_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace seat_upstream
{
inline void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

inline bool Normalize(float v[3])
{
    const float l = shot_lag::Length(v);
    if (!std::isfinite(l) || !(l > 1.0e-6f))
        return false;
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
    return true;
}

// The game basis's unit vector of (yaw, pitch), and back (yaw atan2's (-pi, pi]).
inline void DirFromAngles(float yaw, float pitch, float out[3])
{
    out[0] = std::cos(pitch) * std::cos(yaw);
    out[1] = std::cos(pitch) * std::sin(yaw);
    out[2] = std::sin(pitch);
}
inline bool AnglesFromDir(const float d[3], float& yaw, float& pitch)
{
    float n[3]{d[0], d[1], d[2]};
    if (!shot_lag::Finite3(n) || !Normalize(n))
        return false;
    const float s = n[2] > 1.0f ? 1.0f : (n[2] < -1.0f ? -1.0f : n[2]);
    const float y = std::atan2(n[1], n[0]), p = std::asin(s);
    if (!std::isfinite(y) || !std::isfinite(p))
        return false;
    yaw = y;
    pitch = p;
    return true;
}
inline float AngleDeg(const float a[3], const float b[3])
{
    float c[3]{};
    Cross(a, b, c);
    const float s = shot_lag::Length(c), d = shot_lag::Dot(a, b);
    const float r = std::atan2(s, d) * shot_lag::kRadToDeg;
    return std::isfinite(r) ? r : 0.0f;
}

// ---- 1. the stock query from a record, and its inverse --------------------------------------------------------
// The model: query = normalize(record - gain * velocity). gain 0 = the query IS the record.
inline bool StockQuery(const float record[3], const float velocity[3], float gain, float out[3])
{
    if (!shot_lag::Finite3(record) || !shot_lag::Finite3(velocity) || !std::isfinite(gain) || !out)
        return false;
    float q[3]{record[0] - gain * velocity[0], record[1] - gain * velocity[1], record[2] - gain * velocity[2]};
    if (!Normalize(q))
        return false;
    out[0] = q[0];
    out[1] = q[1];
    out[2] = q[2];
    return true;
}

enum class Invert : uint8_t
{
    Solved = 0,
    NonFinite,    // any input not finite
    Degenerate,   // a zero-length wanted direction
    NoSolution,   // no unit record maps onto that query (|gain * velocity across it| >= 1, or only backwards)
    Count
};

// The record R (unit) whose stock query is `want`: R = mu * D + gain * v with |R| = 1 and mu > 0, D = want normalized.
// mu = -g(D.v) + sqrt(g^2 (D.v)^2 - g^2 |v|^2 + 1): exact, no iteration. `out` is written only on Solved.
inline Invert RecordForQuery(const float want[3], const float velocity[3], float gain, float out[3])
{
    if (!shot_lag::Finite3(want) || !shot_lag::Finite3(velocity) || !std::isfinite(gain) || !out)
        return Invert::NonFinite;
    float d[3]{want[0], want[1], want[2]};
    if (!Normalize(d))
        return Invert::Degenerate;
    const float gv[3]{gain * velocity[0], gain * velocity[1], gain * velocity[2]};
    const float b = shot_lag::Dot(d, gv);
    const float disc = b * b - shot_lag::Dot(gv, gv) + 1.0f;
    if (!std::isfinite(disc) || disc < 0.0f)
        return Invert::NoSolution;
    const float mu = -b + std::sqrt(disc);
    if (!(mu > 0.0f))
        return Invert::NoSolution;
    float r[3]{mu * d[0] + gv[0], mu * d[1] + gv[1], mu * d[2] + gv[2]};
    if (!Normalize(r))
        return Invert::NoSolution;
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    return Invert::Solved;
}

// ---- 2. the gain, learned from the game's own answers ------------------------------------------------------------
// One sample: the record R (unit) that drove the aim, the camera direction Q the game answered with it, the camera's
// velocity v. Q is parallel to R - g v, so Q x R = g (Q x v); the least-squares g over the samples is
// sum((Q x R).(Q x v)) / sum(|Q x v|^2), each sum decaying by `decay` per sample (the seat's recent answers weigh
// most: a weapon or seat change re-learns in about 1 / (1 - decay) samples). Only a sample with |Q x v| (the
// velocity across the line) of at least minCrossWuPerS carries information about g. `Learned` is evidence, not a
// size: enough crossing velocity seen (weight) since the seat started. No bound on g itself.
struct GainFit
{
    double num = 0.0, den = 0.0;
    uint32_t used = 0, ignored = 0;
    void Reset() { *this = GainFit{}; }
    bool Add(const float record[3], const float query[3], const float velocity[3], float minCrossWuPerS, float decay)
    {
        if (!shot_lag::Finite3(record) || !shot_lag::Finite3(query) || !shot_lag::Finite3(velocity) ||
            !std::isfinite(decay))
        {
            ++ignored;
            return false;
        }
        float qr[3]{}, qv[3]{};
        Cross(query, record, qr);
        Cross(query, velocity, qv);
        const float cross2 = shot_lag::Dot(qv, qv);
        if (!std::isfinite(cross2) || !(cross2 >= minCrossWuPerS * minCrossWuPerS) || !(cross2 > 0.0f))
        {
            ++ignored;
            return false;
        }
        num = num * decay + double(shot_lag::Dot(qr, qv));
        den = den * decay + double(cross2);
        ++used;
        return true;
    }
    // At least `minWeight` (wu/s)^2 of decayed crossing velocity behind the fit.
    bool Learned(double minWeight) const { return used > 0 && den >= minWeight && std::isfinite(num / den); }
    float Gain() const { return den > 0.0 ? float(num / den) : 0.0f; }
};

// The learner's defaults: velocity across the line of at least 2 m/s carries information; a sample's weight decays
// over ~30 control passes (~0.3 s at 100 fps); learned once 10 samples at 2 m/s worth of crossing velocity is behind
// it (one second of a 4 m/s strafe gives far more).
inline constexpr float kGainMinCrossWuPerS = 2.0f * shot_lag::kWuPerMetre;
inline constexpr float kGainDecay = 0.97f;
inline constexpr double kGainMinWeight = 10.0 * double(kGainMinCrossWuPerS) * double(kGainMinCrossWuPerS);

// ---- 3. which record drove the camera the acquisition answered with ---------------------------------------------
// The control pass runs ahead of its frame's 0..N simulation ticks, so the camera the acquisition reads in a pass
// was aimed by the record left by the newest stick call BEFORE the most recent tick: a pass with no tick leaves the
// aim of an older call in place. Every stick call is noted with the simulation tick count it ran at and the record
// it left (written, or unknown when it wrote nothing: the servo or the game moved it); the record in effect at tick
// count T is the one the newest call below T left -- and nothing when that call left it unknown or in another seat.
struct WriteRing
{
    static constexpr int kSlots = 8;
    struct Write
    {
        bool used = false;     // the slot holds a stick call
        bool known = false;    // ...which left a record we wrote (else unknown)
        uint32_t ticks = 0;    // the simulation tick counter at the call (before the ticks that consume it)
        uint32_t seatKey = 0;  // 0 = on foot
        float dir[3]{};        // the record left, world unit vector (known only)
    };
    Write writes[kSlots]{};
    int next = 0;
    void Reset() { *this = WriteRing{}; }
    // `dir` null (or non-finite): the call left a record we did not write.
    void Note(uint32_t ticks, uint32_t seatKey, const float* dir)
    {
        Write& w = writes[next];
        next = (next + 1) % kSlots;
        w.used = true;
        w.known = dir && shot_lag::Finite3(dir);
        w.ticks = ticks;
        w.seatKey = seatKey;
        if (w.known)
        {
            w.dir[0] = dir[0];
            w.dir[1] = dir[1];
            w.dir[2] = dir[2];
        }
    }
    // The record of `seatKey` in effect at tick count `ticksNow` (wrap-safe), or null.
    const Write* InEffect(uint32_t ticksNow, uint32_t seatKey) const
    {
        for (int k = 1; k <= kSlots; ++k)
        {
            const Write& w = writes[(next + kSlots - k) % kSlots];
            if (!w.used)
                return nullptr;
            if (static_cast<int32_t>(ticksNow - w.ticks) <= 0)
                continue;   // made at this count: no tick has consumed it yet
            // The newest consumed call decides: a record it did not write, or another seat's, is none.
            return w.known && w.seatKey == seatKey ? &w : nullptr;
        }
        return nullptr;
    }
};

// ---- 3b. the camera an UNMODDED machine queries our seat from ------------------------------------------------------
// The evaluator's path is the seat's decision (0x1326E8): a seat whose LOADED flags carry bit 4 (third person) takes
// the chase path 0x2144C0 -- whose answer is the unit's camera position plus a camera-track offset (the seat's track,
// evaluated at the aim's pitch by the spline helpers 0x214A54 / 0x2146C0, turned to the aim's heading) and the SAME
// direction, the occupant's aiming vector +0x1AC (0x2145B2). C20 clears bit 4 in this machine's loaded tag while it
// owns the seat's view; a host that runs no mod keeps it, and so does a modded host whose own player is not in that
// seat. So for a stock third-person seat the host's crosshair line is ours shifted by the chase offset. The chase path
// picks its camera track by the LIVE flags (`test byte [seat], 0x15`: bits 0, 2, 4), so asking it here reproduces the
// stock answer only when bit 0 or 2 still selects the seat's track with bit 4 cleared.
enum class StockCamera : uint8_t
{
    SameAsHere = 0,     // stock bit 4 clear: an unmodded host queries from the same first-person camera
    Chase,              // stock third person, and the chase path reproduces it here
    NotReproducible,    // stock third person, but only bit 4 selected the seat's track: asking here would answer the
                        // unit's own track instead
    Count
};
inline constexpr uint32_t kSeatThirdPersonBit = 1u << 4;
inline constexpr uint32_t kSeatTrackBitsBesidesThirdPerson = (1u << 0) | (1u << 2);
inline StockCamera StockCameraFor(uint32_t originalSeatFlags)
{
    if (!(originalSeatFlags & kSeatThirdPersonBit))
        return StockCamera::SameAsHere;
    return (originalSeatFlags & kSeatTrackBitsBesidesThirdPerson) ? StockCamera::Chase : StockCamera::NotReproducible;
}

// ---- 4. the seated record, recomposed so the stock query passes through P --------------------------------------
enum class SeatRecompose : uint8_t
{
    Recomposed = 0,
    Off,            // the key seat_upstream_aim is 0 (dev A/B), or the converged aim is off
    NotSeated,      // on foot, or not a seat this direct drive writes
    GunSlaved,      // seat flag bit 7: the query is the gun's marker line, which this model does not cover
    NoIntent,       // nothing published
    OtherSolve,     // the setpoint is not the solve that built P (the gun-offset shift, a steering author)
    PointUnusable,  // another generation, stale (or from the future), on-foot, held at the pitch rail, or non-finite
    NoCamera,       // no camera from this pass's acquisition for our unit (the seated evaluator did not answer)
    PointTooClose,  // P within 30 cm of the camera: no direction
    PointBehind,    // P behind the camera along the setpoint
    NoSolution,     // no record maps onto the line through P (RecordForQuery)
    Count
};
inline const char* SeatRecomposeName(SeatRecompose why)
{
    switch (why)
    {
    case SeatRecompose::Recomposed: return "recomposed";
    case SeatRecompose::Off: return "off";
    case SeatRecompose::NotSeated: return "not seated";
    case SeatRecompose::GunSlaved: return "gun-slaved";
    case SeatRecompose::NoIntent: return "no intent";
    case SeatRecompose::OtherSolve: return "another solve";
    case SeatRecompose::PointUnusable: return "point stale/rail-held/unseated";
    case SeatRecompose::NoCamera: return "no camera this pass";
    case SeatRecompose::PointTooClose: return "point within 30 cm";
    case SeatRecompose::PointBehind: return "point behind";
    case SeatRecompose::NoSolution: return "no solution";
    default: return "?";
    }
}

struct SeatRecomposeInput
{
    bool enabled = false;          // seat_upstream_aim and the converged aim
    bool seated = false;           // the publication is a seat this update writes
    bool gunSlaved = false;
    bool intentRead = false;
    shot_lag::IntentPoint intent{};
    float point[3]{};              // the seat's P: the point the solve bent the record through (solvePoint)
    float setYaw = 0.0f, setPitch = 0.0f;   // the published setpoint (world angles)
    uint32_t generation = 0;
    uint64_t nowUs = 0, maxAgeUs = 0;
    bool cameraRead = false;       // this pass's acquisition camera, our unit, finite
    float camera[3]{};
    float cameraVel[3]{};          // wu/s
    bool cameraVelValid = false;
    float tickS = 0.0f;            // the simulation tick; the camera is carried this far ahead (clamped to 50 ms)
    float gain = 0.0f;             // the learned gain (0 when not learned)
};

// yaw / pitch / bendDeg are written only on Recomposed. bendDeg: from the published setpoint to the new record.
inline SeatRecompose RecomposeSeat(const SeatRecomposeInput& in, float& yaw, float& pitch, float& bendDeg)
{
    if (!in.enabled)
        return SeatRecompose::Off;
    if (!in.seated)
        return SeatRecompose::NotSeated;
    if (in.gunSlaved)
        return SeatRecompose::GunSlaved;
    if (!in.intentRead)
        return SeatRecompose::NoIntent;
    const shot_lag::IntentPoint& p = in.intent;
    if (!shot_lag::SameAngles(in.setYaw, in.setPitch, p.solvedYaw, p.solvedPitch))
        return SeatRecompose::OtherSolve;
    if (!in.generation || p.generation != in.generation || !p.seated || p.railHeld || !p.stampUs ||
        in.nowUs < p.stampUs || in.nowUs - p.stampUs > in.maxAgeUs || !shot_lag::Finite3(in.point))
        return SeatRecompose::PointUnusable;
    if (!in.cameraRead || !shot_lag::Finite3(in.camera))
        return SeatRecompose::NoCamera;
    const float zero[3]{};
    const float* vel = in.cameraVelValid && shot_lag::Finite3(in.cameraVel) ? in.cameraVel : zero;
    float camera[3]{in.camera[0], in.camera[1], in.camera[2]};
    (void)shot_lag::ExtrapolateEye(in.camera, vel, in.tickS, 0.05f, camera);
    const float toPoint[3]{in.point[0] - camera[0], in.point[1] - camera[1], in.point[2] - camera[2]};
    const float range = shot_lag::Length(toPoint);
    if (!std::isfinite(range) || !(range > 0.3f * shot_lag::kWuPerMetre))
        return SeatRecompose::PointTooClose;
    float set[3]{};
    DirFromAngles(in.setYaw, in.setPitch, set);
    if (!(shot_lag::Dot(toPoint, set) > 0.0f))
        return SeatRecompose::PointBehind;
    const float gain = std::isfinite(in.gain) ? in.gain : 0.0f;
    float record[3]{};
    if (RecordForQuery(toPoint, vel, gain, record) != Invert::Solved)
        return SeatRecompose::NoSolution;
    float y = 0.0f, pt = 0.0f;
    if (!AnglesFromDir(record, y, pt))
        return SeatRecompose::NoSolution;
    yaw = y;
    pitch = pt;
    bendDeg = AngleDeg(set, record);
    return SeatRecompose::Recomposed;
}

// ---- 4b. where a stock seat's crosshair line starts -------------------------------------------------------------
// A seat whose loaded tag keeps `third person camera` is queried from the game's chase camera, on every machine.
// The record is solved from that camera (RecomposeSeat), so the camera's position is needed at every control pass,
// firing or not. Three sources, best first:
//   Evaluator  this pass's own acquisition camera: the evaluator's chase-path answer for our unit, where the
//              evaluator is hooked. Fresh, with a measured velocity.
//   Asked      the evaluator's chase path asked directly at the stick call, once a simulation tick. As fresh; its
//              velocity comes from its own track. Used unless it is known to be off the game's line (AskAgreement),
//              and carried along its own direction by the lead the game's query shows.
//   Query      the start of the game's last crosshair query for the seat: a point on the camera's line, not the
//              camera (the shot origin's projection on it). The game runs that query when the gun fires, so
//              between bursts there is none and inside one it can be old: it is carried forward by the seat's own
//              velocity for its age, and refused past kStockQueryMaxAgeUs.
// The chase camera follows the record, so each record is solved against the camera the last one produced. With the
// camera R behind its pivot and the point L from the pivot the angle error shrinks by R / (L + R) a step, from any
// start (unit-tested). A step is one simulation tick for the first two sources and the time between queries for
// the third, which is why they come first.
enum class StockStart : uint8_t { None = 0, Evaluator, Asked, Query, Count };
inline const char* StockStartName(StockStart source)
{
    switch (source)
    {
    case StockStart::Evaluator: return "this pass's own camera";
    case StockStart::Asked: return "the chase camera asked at the stick call";
    case StockStart::Query: return "the last crosshair query's start";
    default: return "none";
    }
}
inline constexpr uint64_t kStockQueryMaxAgeUs = 250000;
// The chase camera changes once a simulation tick; without a tick count it is asked again after this long.
inline constexpr uint64_t kStockAskRefreshUs = 50000;

// IS THE ASKED CAMERA ON THE GAME'S OWN QUERY LINE? The chase path is asked outside the evaluator, so a host that
// replaces the evaluator's answer (a camera mode of its own) would leave the ask describing a camera the game no
// longer uses. Whenever a fresh query is in hand the two are compared -- as a point against a LINE. The query does
// not start at the camera: it starts further along the camera's line, at the shot origin's projection on it (some
// metres ahead in a chase view), so the distance between the two points says nothing. What must hold is that the
// asked camera lies ON the query's line: its distance aside from it under kAgreeWu. A comparison made while the
// aim turned between the two (their directions more than kSameAimDeg apart) is no evidence. That bound is tight
// on purpose: the query and the ask are the same camera a pass apart, while another occupant of the same vehicle
// (whose query names the same objects, and whose fire is not always tagged) looks along his own aim, which
// differs by the seats' parallax even when both shoot at one target. Not yet compared is not disagreement: the
// ask is used until kDisagreeToRefuse comparisons in a row put it off the line, and again from the first
// that puts it on.
// THE LEAD. How far along the line the query starts ahead of the camera is kept, smoothed. From the camera itself
// each step of the solve shrinks the error by R / (L + R) (the camera R behind the pivot it turns about, the point
// L from the pivot), always from the same side. A start carried ahead along the camera's direction is nearer the
// pivot, and the solve less sensitive to how far the camera swings between this pass and the fire. A start
// carried PAST the pivot by d turns each step's error into -d / (L - d) of itself: the record swings past the
// answer and back, and a gun that follows the record is seen to hunt. The query's start is past the pivot by the
// gun's own reach ahead of it, so (LeadToward):
//   * only kLeadShare of the measured lead is used: the start stays behind the pivot wherever the gun's reach
//     ahead of the pivot is less than the camera's distance behind it, which is every chase camera;
//   * never more than half the way from the camera to the point: the factor's size is then under
//     (L - R) / (L + R), below one for every arm and every point;
//   * never negative: behind the camera is further from the pivot than the camera is.
struct AskAgreement
{
    static constexpr float kAgreeWu = 1.0f * shot_lag::kWuPerMetre;
    static constexpr float kSameAimDeg = 0.5f;
    static constexpr float kMaxLeadWu = 40.0f * shot_lag::kWuPerMetre;
    static constexpr float kLeadShare = 0.5f;
    static constexpr uint64_t kCompareMaxAgeUs = 60000;   // a query older than this is not compared
    static constexpr uint32_t kDisagreeToRefuse = 3;
    bool refused = false;
    uint32_t streak = 0;
    bool leadKnown = false;
    float leadWu = 0.0f;

    struct Reading
    {
        bool compared = false;
        float asideWu = 0.0f;   // the asked camera's distance from the query's line
        float aheadWu = 0.0f;   // how far along the line the query starts ahead of the camera
    };
    void Reset() { *this = AskAgreement{}; }
    static bool Agrees(const Reading& reading) { return reading.compared && reading.asideWu <= kAgreeWu; }
    // One comparison: the asked camera (its position and direction) against the query's line, whose start is
    // already carried to the instant of the ask. A reading that is not `compared` changes nothing.
    Reading Note(const float asked[3], const float askedDir[3], const float lineStart[3], const float lineDir[3])
    {
        Reading reading;
        float a[3]{askedDir[0], askedDir[1], askedDir[2]}, d[3]{lineDir[0], lineDir[1], lineDir[2]};
        if (!shot_lag::Finite3(asked) || !shot_lag::Finite3(lineStart) || !shot_lag::Finite3(a) ||
            !shot_lag::Finite3(d) || !Normalize(a) || !Normalize(d))
            return reading;
        if (AngleDeg(a, d) > kSameAimDeg)
            return reading;
        const float w[3]{lineStart[0] - asked[0], lineStart[1] - asked[1], lineStart[2] - asked[2]};
        const float ahead = shot_lag::Dot(w, d);
        const float off[3]{w[0] - d[0] * ahead, w[1] - d[1] * ahead, w[2] - d[2] * ahead};
        const float aside = shot_lag::Length(off);
        if (!std::isfinite(aside) || !std::isfinite(ahead))
            return reading;
        reading.compared = true;
        reading.asideWu = aside;
        reading.aheadWu = ahead;
        if (aside <= kAgreeWu)
        {
            streak = 0;
            refused = false;
            if (std::fabs(ahead) <= kMaxLeadWu)
            {
                leadWu = leadKnown ? leadWu + (ahead - leadWu) * 0.25f : ahead;
                leadKnown = true;
            }
        }
        else if (++streak >= kDisagreeToRefuse)
            refused = true;
        return reading;
    }
    bool Usable() const { return !refused; }
    float Lead() const { return leadKnown ? leadWu : 0.0f; }
    // The lead to use toward `point` from a camera at `asked` looking along `askedDir` (unit).
    static float LeadToward(float leadWu, const float asked[3], const float askedDir[3], const float point[3])
    {
        if (!(leadWu > 0.0f) || !std::isfinite(leadWu))
            return 0.0f;
        const float toPoint[3]{point[0] - asked[0], point[1] - asked[1], point[2] - asked[2]};
        const float reach = shot_lag::Dot(toPoint, askedDir);
        if (!std::isfinite(reach) || reach <= 0.0f)
            return 0.0f;
        const float share = leadWu * kLeadShare, half = reach * 0.5f;
        return share < half ? share : half;
    }
};

struct StockStartInput
{
    bool evaluator = false;
    float evaluatorPos[3]{}, evaluatorVel[3]{};
    bool evaluatorVelValid = false;
    bool asked = false;            // asked this tick, and not refused by AskAgreement
    float askedPos[3]{}, askedVel[3]{};
    bool askedVelValid = false;
    float askedDir[3]{};           // the asked camera's own direction
    float askedLeadWu = 0.0f;      // AskAgreement::Lead(): how far along it the solve starts (0 = at the camera)
    bool pointValid = false;       // the point the record is solved through; without one the lead is not applied
    float point[3]{};
    bool query = false;
    float queryPos[3]{};
    uint64_t queryAgeUs = 0;
    float seatVel[3]{};            // the seat's own velocity, wu/s: what carries an old query start forward
    bool seatVelValid = false;
};
struct StockStartChoice
{
    StockStart source = StockStart::None;
    float pos[3]{}, vel[3]{};
    bool velValid = false;
};
// A query start carried to now by the seat's velocity. False (out untouched): too old, or not finite.
inline bool CarryQueryStart(const float queryPos[3], uint64_t ageUs, const float seatVel[3], bool seatVelValid,
                            float out[3])
{
    if (!shot_lag::Finite3(queryPos) || ageUs > kStockQueryMaxAgeUs)
        return false;
    const float zero[3]{};
    const float* vel = seatVelValid && shot_lag::Finite3(seatVel) ? seatVel : zero;
    return shot_lag::ExtrapolateEye(queryPos, vel, float(ageUs) * 1.0e-6f, float(kStockQueryMaxAgeUs) * 1.0e-6f, out);
}
inline StockStartChoice ChooseStockStart(const StockStartInput& in)
{
    StockStartChoice c{};
    const auto take = [&c](StockStart source, const float pos[3], const float vel[3], bool velValid) {
        const bool usable = velValid && shot_lag::Finite3(vel);
        c.source = source;
        for (int axis = 0; axis < 3; ++axis)
        {
            c.pos[axis] = pos[axis];
            c.vel[axis] = usable ? vel[axis] : 0.0f;
        }
        c.velValid = usable;
    };
    float carried[3]{};
    if (in.evaluator && shot_lag::Finite3(in.evaluatorPos))
        take(StockStart::Evaluator, in.evaluatorPos, in.evaluatorVel, in.evaluatorVelValid);
    else if (in.asked && shot_lag::Finite3(in.askedPos))
    {
        float origin[3]{in.askedPos[0], in.askedPos[1], in.askedPos[2]};
        float along[3]{in.askedDir[0], in.askedDir[1], in.askedDir[2]};
        if (in.pointValid && shot_lag::Finite3(in.point) && shot_lag::Finite3(along) && Normalize(along))
        {
            const float lead = AskAgreement::LeadToward(in.askedLeadWu, in.askedPos, along, in.point);
            for (int axis = 0; axis < 3; ++axis)
                origin[axis] += along[axis] * lead;
        }
        take(StockStart::Asked, origin, in.askedVel, in.askedVelValid);
    }
    else if (in.query && CarryQueryStart(in.queryPos, in.queryAgeUs, in.seatVel, in.seatVelValid, carried))
        take(StockStart::Query, carried, in.seatVel, in.seatVelValid);
    return c;
}

// ---- 5. the multiplayer residual: the stock query against P, before any re-aim -----------------------------------
// What an unmodded host would compute: the game's own crosshair line at the fire tick, as the game built it, against
// the point the player meant. Per seat window (dev line SHOT UPSTREAM): the miss of P off the line (mm), the angle
// at the line's start between the line and P (deg), the miss split along / across the camera's travel.
struct StockLineSample
{
    bool valid = false;
    float missMm = 0.0f, angleDeg = 0.0f, rangeM = 0.0f;
    float alongMm = 0.0f, acrossMm = 0.0f;
    bool travel = false;
};
inline StockLineSample MeasureStockLine(const float start[3], const float vector[3], const float point[3],
                                        const float* velocity)
{
    StockLineSample s{};
    const shot_lag::ShotLagSample m = shot_lag::MeasureShotLag(start, vector, point, nullptr, velocity);
    if (!m.valid)
        return s;
    const float toPoint[3]{point[0] - start[0], point[1] - start[1], point[2] - start[2]};
    float d[3]{vector[0], vector[1], vector[2]};
    if (!Normalize(d))
        return s;
    s.valid = true;
    s.missMm = m.missMm;
    s.rangeM = m.rangeM;
    s.angleDeg = AngleDeg(d, toPoint);
    s.travel = m.travel;
    s.alongMm = m.alongMm;
    s.acrossMm = m.acrossMm;
    return s;
}

struct StockLineWindow
{
    uint32_t tag = 0;   // the seat (caller's packing); 0 = unused
    uint32_t n = 0, travel = 0;
    float missSum = 0.0f, missMax = 0.0f, angleSum = 0.0f, angleMax = 0.0f, rangeSum = 0.0f;
    float alongSum = 0.0f, acrossSum = 0.0f;
    uint32_t reaimed = 0, through = 0;   // what the fire-site re-aim then did with these queries
    // A stock third-person seat: the chase line an unmodded host would trace instead (StockCamera::Chase), and the
    // queries whose chase camera could not be reproduced here.
    uint32_t hostN = 0, hostUnreproducible = 0;
    float hostMissSum = 0.0f, hostMissMax = 0.0f, hostAngleSum = 0.0f, hostAngleMax = 0.0f;
    void AddHost(const StockLineSample& s)
    {
        if (!s.valid)
            return;
        ++hostN;
        hostMissSum += s.missMm;
        hostMissMax = s.missMm > hostMissMax ? s.missMm : hostMissMax;
        hostAngleSum += s.angleDeg;
        hostAngleMax = s.angleDeg > hostAngleMax ? s.angleDeg : hostAngleMax;
    }
    void Add(const StockLineSample& s)
    {
        if (!s.valid)
            return;
        ++n;
        missSum += s.missMm;
        missMax = s.missMm > missMax ? s.missMm : missMax;
        angleSum += s.angleDeg;
        angleMax = s.angleDeg > angleMax ? s.angleDeg : angleMax;
        rangeSum += s.rangeM;
        if (s.travel)
        {
            ++travel;
            alongSum += s.alongMm;
            acrossSum += s.acrossMm;
        }
    }
    size_t Format(const char* title, const char* seatName, char* out, size_t size) const
    {
        if (!out || !size || !n)
            return 0;
        int w = std::snprintf(
            out, size,
            "SHOT UPSTREAM %s seat %s (dev): %u stock crosshair queries before any fire-site re-aim (what a host "
            "firing from our record traces from this camera): P off the stock line mean %.0f mm (max %.0f), mean "
            "%.3f deg (max %.3f) at %.1f m; along the camera's travel %+.0f mm, across %.0f mm (%u moving); the "
            "fire-site re-aim then acted %u, already through %u",
            title, seatName, n, double(missSum / float(n)), double(missMax), double(angleSum / float(n)),
            double(angleMax), double(rangeSum / float(n)), travel ? double(alongSum / float(travel)) : 0.0,
            travel ? double(acrossSum / float(travel)) : 0.0, travel, reaimed, through);
        if (w <= 0)
            return 0;
        size_t used = static_cast<size_t>(w) < size ? static_cast<size_t>(w) : size - 1;
        if (hostN && used + 1 < size)
        {
            w = std::snprintf(out + used, size - used,
                              "; an UNMODDED host's chase line (stock third-person seat): P off it mean %.0f mm (max "
                              "%.0f), mean %.3f deg (max %.3f) over %u",
                              double(hostMissSum / float(hostN)), double(hostMissMax),
                              double(hostAngleSum / float(hostN)), double(hostAngleMax), hostN);
            if (w > 0)
                used += static_cast<size_t>(w) < size - used ? static_cast<size_t>(w) : size - used - 1;
        }
        if (hostUnreproducible && used + 1 < size)
        {
            w = std::snprintf(out + used, size - used,
                              "; %u queries of a stock third-person seat whose chase camera cannot be reproduced here",
                              hostUnreproducible);
            if (w > 0)
                used += static_cast<size_t>(w) < size - used ? static_cast<size_t>(w) : size - used - 1;
        }
        return used;
    }
};

} // namespace seat_upstream
