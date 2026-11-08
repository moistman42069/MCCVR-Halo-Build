#pragma once
// SEAT FRAME CONVERSION -- the pure math of every Blam title's vehicle-up rebase of a seated player's aim, and its
// inverse (docs/ALL-TITLES-AIM-2026-09-29.md, "Seats aim in world angles: the C20 flag and the vehicle-tilt
// rebase", the per-title table). Unit-tested in tests/seat_frame_logic_tests.cpp; the convention below is
// re-proven against every retail module by tools/verify_seat_frame_conversion.py, which EXECUTES each title's own
// rebase code (a small x86 interpreter over the retail bytes) and compares it with a Python twin of this file.
//
// WHAT THE GAME DOES (static RE of the retail Steam modules; the Store modules are byte-identical). Each title
// builds a player unit's aim vector d = e(yaw, pitch) from the player-control angles and hands it to one rebase
// function (Halo 3 0x355E20, ODST 0x39A600, Reach 0x48ADB8, Halo 2 0x8F0D20, Halo 4 0x5F97DC). Unless the
// occupied seat's tag flags carry bit 4 (`third person camera`), it reads the parent vehicle's UP vector u (the
// object's own up, or composed through its parent's node matrix when the vehicle is attached) and replaces d by
// R(u) d with
//
//      f = normalize(Y x u)      -- the game's normalize: below 1e-4 it leaves the vector and returns 0, and then
//      f = normalize(Z x u)         the game falls back to Z x u (u parallel to world Y, a vehicle on its side)
//      l = u x f
//      R = [f l u]               -- columns: d' = d.x f + d.y l + d.z u (the matrix transform every title shares)
//
// u is used AS READ (never normalised); f is unit length unless both branches degenerate. So R is the identity
// only while u is +Z (a level vehicle), and the control angles stop being world angles the moment the hull tilts.
//
// THE INVERSE. To make the game aim along a world direction w through a rebased seat, the record must hold the
// angles of R^-1 w. R's columns are mutually orthogonal by construction (f is perpendicular to u; l = u x f to
// both), so R^-1 w = (f.w / |f|^2, l.w / |l|^2, u.w / |u|^2) exactly, whatever |u| is; only its DIRECTION
// matters (the angles). The yaw of a record direction that points straight along the record's own up has no
// meaning; the caller's fallback (the record's current yaw) is kept there instead of a jump.
//
// Yaw is always returned in [0, 2pi): every title quantizes the record's yaw into its player action as 13 bits
// over [0, 2pi], and in Halo 3 / ODST an out-of-range yaw spills into the action's button bits (a fake press).
// Zero engine dependencies.

#include <cmath>
#include <cstdint>
#include <limits>

#include "aim_write_logic.h"

namespace seat_frame
{
// Every title's normalize (Reach 0x1FCFC, Halo 3 0x212AC, ODST 0x232E4, Halo 4 0x48314, Halo 2 0x2668D0):
// |length - 0| < 1e-4 (in double, after a float sqrt) -> the vector is left alone and 0 is returned.
inline constexpr float kNormalizeEpsilon = 1.0e-4f;

// R = [f l u], columns, exactly as the game's matrix (scale 1, forward +0x04, left +0x10, up +0x1C).
struct Frame
{
    float f[3]{1.0f, 0.0f, 0.0f};
    float l[3]{0.0f, 1.0f, 0.0f};
    float u[3]{0.0f, 0.0f, 1.0f};
};

inline bool Finite3(const float v[3])
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The game's normalize: the length; below the epsilon the vector is untouched and 0 is returned.
inline float GameNormalize(float v[3])
{
    const float length = std::sqrt(Dot(v, v));
    if (!(std::fabs(static_cast<double>(length)) >= static_cast<double>(kNormalizeEpsilon)))
        return 0.0f;
    const float scale = 1.0f / length;
    v[0] *= scale;
    v[1] *= scale;
    v[2] *= scale;
    return length;
}

// The rebase's frame for an up vector, in the game's own operation order. False when u is not finite, or both
// forward candidates degenerate (u is the zero vector): no frame, nothing may be written through it.
inline bool BuildFrame(const float up[3], Frame& out)
{
    if (!Finite3(up))
        return false;
    const float ux = up[0], uy = up[1], uz = up[2];
    // f = Y x u = (uz, 0, -ux). The game multiplies the zero terms out (0 * uy etc.); with finite inputs that is
    // exactly 0, so the plain form is identical.
    Frame r{};
    r.f[0] = uz;
    r.f[1] = 0.0f;
    r.f[2] = -ux;
    if (GameNormalize(r.f) == 0.0f)
    {
        // f = Z x u = (-uy, ux, 0)
        r.f[0] = -uy;
        r.f[1] = ux;
        r.f[2] = 0.0f;
        if (GameNormalize(r.f) == 0.0f)
            return false;
    }
    r.u[0] = ux;
    r.u[1] = uy;
    r.u[2] = uz;
    // l = u x f
    r.l[0] = r.u[1] * r.f[2] - r.u[2] * r.f[1];
    r.l[1] = r.u[2] * r.f[0] - r.u[0] * r.f[2];
    r.l[2] = r.u[0] * r.f[1] - r.u[1] * r.f[0];
    out = r;
    return true;
}

// Halo's forward for control angles: x = cos(pitch)cos(yaw), y = cos(pitch)sin(yaw), z = sin(pitch).
inline void Forward(float yaw, float pitch, float out[3])
{
    const float cp = std::cos(pitch);
    out[0] = cp * std::cos(yaw);
    out[1] = cp * std::sin(yaw);
    out[2] = std::sin(pitch);
}

// d' = R d: what the game makes of a record direction d in this seat.
inline void Apply(const Frame& r, const float d[3], float out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = d[0] * r.f[i] + d[1] * r.l[i] + d[2] * r.u[i];
}

// R^-1 w (the columns are orthogonal; see the header). False when a column is degenerate or the result is not
// finite.
inline bool Invert(const Frame& r, const float w[3], float out[3])
{
    const float ff = Dot(r.f, r.f), ll = Dot(r.l, r.l), uu = Dot(r.u, r.u);
    if (!(ff > 1.0e-12f) || !(ll > 1.0e-12f) || !(uu > 1.0e-12f))
        return false;
    out[0] = Dot(r.f, w) / ff;
    out[1] = Dot(r.l, w) / ll;
    out[2] = Dot(r.u, w) / uu;
    return Finite3(out);
}

struct Angles
{
    float yaw = 0.0f;     // [0, 2pi)
    float pitch = 0.0f;   // [-pi/2, pi/2]
};

// The yaw / pitch of a direction; yaw in [0, 2pi). A direction along +-Z has no yaw: `fallbackYaw` is kept.
inline bool AnglesOf(const float d[3], float fallbackYaw, Angles& out)
{
    if (!Finite3(d))
        return false;
    const float horizontal = std::sqrt(d[0] * d[0] + d[1] * d[1]);
    const float length = std::sqrt(horizontal * horizontal + d[2] * d[2]);
    if (!(length > 1.0e-6f))
        return false;
    const float yaw = horizontal > 1.0e-6f * length ? std::atan2(d[1], d[0]) : fallbackYaw;
    const float pitch = std::atan2(d[2], horizontal);
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    out.yaw = aim_write::wrap_0_2pi(yaw);
    out.pitch = pitch;
    return std::isfinite(out.yaw);
}

// The record angles that make the game aim along the WORLD angles (yaw, pitch) through this seat's rebase:
// the angles of R^-1 e(yaw, pitch). `fallbackYaw` (the record's current yaw) is kept when that direction runs
// along the record's own up.
inline bool RecordFromWorld(const Frame& r, float yaw, float pitch, float fallbackYaw, Angles& out)
{
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    float w[3], d[3];
    Forward(yaw, pitch, w);
    return Invert(r, w, d) && AnglesOf(d, fallbackYaw, out);
}

// The world angles the game aims along for a record (yaw, pitch) through this seat's rebase: the angles of
// R e(yaw, pitch). Used to put the game's own aim and a record into one frame for comparison.
inline bool WorldFromRecord(const Frame& r, float yaw, float pitch, Angles& out)
{
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    float d[3], w[3];
    Forward(yaw, pitch, d);
    Apply(r, d, w);
    return AnglesOf(w, yaw, out);
}

// The angle between two directions, degrees (atan2(|a x b|, a.b): accurate at small angles); NaN when either is
// not finite or zero.
inline float SeparationDeg(const float a[3], const float b[3])
{
    if (!Finite3(a) || !Finite3(b))
        return std::numeric_limits<float>::quiet_NaN();
    const float cx = a[1] * b[2] - a[2] * b[1], cy = a[2] * b[0] - a[0] * b[2], cz = a[0] * b[1] - a[1] * b[0];
    const float cross = std::sqrt(cx * cx + cy * cy + cz * cz);
    const float dot = Dot(a, b);
    if (!(cross > 0.0f) && !(std::fabs(dot) > 0.0f))
        return std::numeric_limits<float>::quiet_NaN();
    return std::atan2(cross, dot) * 57.2957795f;
}

// THE FRAME CHECK (the dev witness): how far the game's aim e(aimYaw, aimPitch) sits from R e(record), degrees.
// With R the identity this is the plain "record vs game aim" separation.
inline float FrameCheckDeg(const Frame& r, float recordYaw, float recordPitch, float aimYaw, float aimPitch)
{
    if (!std::isfinite(recordYaw) || !std::isfinite(recordPitch) || !std::isfinite(aimYaw) ||
        !std::isfinite(aimPitch))
        return std::numeric_limits<float>::quiet_NaN();
    float d[3], w[3], a[3];
    Forward(recordYaw, recordPitch, d);
    Apply(r, d, w);
    Forward(aimYaw, aimPitch, a);
    return SeparationDeg(w, a);
}

// How far an up vector leans from world up (+Z), degrees; NaN when it is not a usable vector.
inline float TiltDeg(const float up[3])
{
    if (!Finite3(up))
        return std::numeric_limits<float>::quiet_NaN();
    const float horizontal = std::sqrt(up[0] * up[0] + up[1] * up[1]);
    if (horizontal + std::fabs(up[2]) < 1e-4f)
        return std::numeric_limits<float>::quiet_NaN();
    return std::atan2(horizontal, up[2]) * 57.2957795f;
}

// VALUE AGREEMENT for a read object frame: the forward and up the game's getter produced must be finite, unit
// length within 2 % and perpendicular within 2 deg. A read that is not is no frame (a wrong field, a torn object,
// a scaled node): nothing is written through it.
inline constexpr float kUnitLengthTolerance = 0.02f;
inline constexpr float kPerpendicularCos = 0.0349f;   // sin(2 deg)

inline bool FrameReadAgrees(const float forward[3], const float up[3])
{
    if (!Finite3(forward) || !Finite3(up))
        return false;
    const float ff = Dot(forward, forward), uu = Dot(up, up);
    const float lo = (1.0f - kUnitLengthTolerance) * (1.0f - kUnitLengthTolerance);
    const float hi = (1.0f + kUnitLengthTolerance) * (1.0f + kUnitLengthTolerance);
    if (!(ff >= lo && ff <= hi && uu >= lo && uu <= hi))
        return false;
    return std::fabs(Dot(forward, up)) < kPerpendicularCos * std::sqrt(ff * uu);
}

// WHAT FRAME A SEATED RECORD IS IN, this update, as the game will treat it.
enum class Case : uint8_t
{
    Unknown,     // a link did not read, or the frame failed its value check: make NO seated write
    World,       // the rebase will not rotate (the seat's live flags carry bit 4, or its own gate exits first)
    SkipWorld,   // case (a): the live flags lack bit 4 only because OUR seat patch cleared it, and our jump skips
                 // the rebase, as an unmodded peer (whose seat keeps bit 4) never runs it: world angles
    Rebased,     // case (b): the seat lacks bit 4 and the rebase WILL run (its own data, or our skip stood down):
                 // every peer rotates the action angles by R(u), so the record must hold R^-1 of the world aim
};

inline const char* CaseName(Case c)
{
    switch (c)
    {
    case Case::World: return "world";
    case Case::SkipWorld: return "a";
    case Case::Rebased: return "b";
    default: return "unknown";
    }
}

// The decision, from what the rebase itself reads. `gateSeated`: the rebase's own early exits pass (the unit
// has a parent and a seat); `flagsKnown` / `flagsBit4`: the occupied seat's LIVE flags word; `skipPatched`: our
// jump is in the rebase's bit-4 test right now (Halo 3, ODST, Reach); `frameUsable`: u read, agreed and built.
inline Case Classify(bool readable, bool gateSeated, bool flagsKnown, bool flagsBit4, bool skipPatched,
                     bool frameUsable)
{
    if (!readable)
        return Case::Unknown;
    if (!gateSeated)
        return Case::World;
    if (!flagsKnown)
        return Case::Unknown;
    if (flagsBit4)
        return Case::World;
    if (skipPatched)
        return Case::SkipWorld;
    return frameUsable ? Case::Rebased : Case::Unknown;
}

// THE YAW-ONLY WRITE. A seat that writes only the record's YAW (Halo 4 always; Halo 3 /
// ODST / Halo 2 while the pitch stays on the servo) keeps the game's own record pitch. RecordFromWorld(hand yaw,
// hand pitch) would aim R^-1 of the hand's whole direction and keep only its yaw, which is right only once the
// record's pitch has converged on the hand's: under a tilted hull the yaw of R^-1 d depends on d's pitch (30 deg of
// roll, hand 20 deg up, record level: about 9 deg of world yaw error until the pitch servo catches up). Instead the
// world target is e(hand yaw, the game's CURRENT world pitch) -- the pitch R e(record yaw, record pitch) puts into
// effect -- so the written yaw is the one that turns the aim to the hand's heading at the pitch the record keeps.
// Exact when level; under tilt the residual shrinks every update as the record yaw follows the write (the fixed
// point of this map is the hand's heading). False when either conversion does not resolve.
inline bool RecordYawForWorldYaw(const Frame& r, float worldYaw, float recordYaw, float recordPitch, float& outYaw)
{
    Angles now{}, record{};
    if (!WorldFromRecord(r, recordYaw, recordPitch, now) ||
        !RecordFromWorld(r, worldYaw, now.pitch, recordYaw, record))
        return false;
    outYaw = record.yaw;
    return true;
}

// THE HAND-BACK (Reach, Game_ReachDirectDriveAimWhy's 0x400). A seated update whose frame reads Unknown writes
// nothing -- and Reach's aim provider holds the aim stick NEUTRAL while it selects the state write, so without this
// the seat's aim would simply FREEZE. So the provider leaves the state write for the stick servo whenever the seat's
// frame cannot be trusted: the reader is not bound (it never binds again within a module generation), or a seated
// read came back Unknown within the last `holdMs`. The sim thread probes the frame on every seated stick update,
// written or not, so a seat that stays unreadable keeps its stamp fresh and stays on the servo; one that reads again
// returns to the state write `holdMs` after its last Unknown (no flapping on a single bad read). `nowMs` earlier
// than the stamp (the reading thread's clock sampled just before the sim thread's) counts as fresh.
inline constexpr uint64_t kUnreadHandBackMs = 500;

inline bool SeatFrameHandsBack(bool readerBound, uint64_t lastUnknownMs, uint64_t nowMs,
                               uint64_t holdMs = kUnreadHandBackMs)
{
    if (!readerBound)
        return true;
    return lastUnknownMs != 0 && (nowMs < lastUnknownMs || nowMs - lastUnknownMs <= holdMs);
}
} // namespace seat_frame
