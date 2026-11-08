#pragma once

// AIM SOLVE -- the engine-free heart of the "overall aim solution", fed by a per-game provider.
//
// The solution has three parts (direct drive + barrel/muzzle origin + convergence). Each has a
// game-AGNOSTIC half, which lives here as pure math + policy, and a game-SPECIFIC half, which lives
// behind IAimProvider (aim_provider.h). This file has ZERO engine/game dependencies so it can be
// unit-tested off the game, the way aim_servo_logic.h is.
//
// THE LADDER (the rule the caps below encode). Find the single place the game keeps its own aim --
// AFTER input conditioning (acceleration/deadzone/magnetism), BEFORE the aim vector is derived --
// and write THAT. Rank a candidate by four tests: (1) after conditioning, so a write is 1:1 with the
// hand; (2) before the direction is derived, so every mirror recomputes from it; (3) persistent --
// write once, stop, does it hold; (4) on the wire -- does a co-op HOST follow it. The best rung is a
// state write; a closed loop around the game's own stick is the replication-safe fallback; a derived
// mirror is a measurement source, never a write target.
//
// CONVERGENCE. A direction alone cannot express "hit that": a shot from origin C parallel to a
// sightline from eye E is displaced by (E-C) at every range. Reconcile it, best-first: trace the
// sightline to its impact and re-aim through it (origin cancels; below); relocate the shot origin
// onto the sight ray at the firing site (engine hook, provider-owned); re-aim through the reticle at
// a fixed range (exact at one range only); or write the direction and let native aim-assist converge.
// The core owns the trace-and-reaim math and the strategy choice; the provider owns the trace and the
// origin hook.
//
// Reference: CampE's src/AimConverge.cpp, from which the trace-and-reaim math below is ported.

#include <cmath>
#include <cstdint>

namespace aim_solve {

// ---- capabilities a per-game provider reports (the ladder, made data) ----------------------------
struct AimCaps {
    // Actuation -- which writable aim state this title exposes (best-first).
    bool can_write_state       = false; // rung 1: a control record UPSTREAM OF REPLICATION
    bool has_stick_loop        = false; // rung 2: a closed loop around the game's own stick input
    // A derived aim vector is NOT a foundation (a mirror the owner re-stamps); it is never a cap.

    // Convergence -- how the shot origin is reconciled with the sightline (best-first).
    bool has_sightline_trace   = false; // trace-and-reaim: range measured in the engine's own collision
    bool has_origin_relocation = false; // firing-site substitution: shot leaves the sight ray
    bool has_fixed_range_reaim = false; // re-aim through the reticle at a fixed distance (one-range-exact)
    // else the floor is DirectionOnly: write the direction, let the game's native aim-assist converge

    // Intent ray -- source of the aim direction.
    bool has_barrel_marker     = false; // aim from the authored muzzle bore, not the raw hand ray

    // CONSEQUENCE, not a strategy input: writing the aim moves the game camera, so view-keyed systems
    // (audio, aim-culling, HUD projection, arm IK) follow the HAND. The CORE DOES NOT CONSUME THIS --
    // it is a note the provider/integration must act on; it is surfaced here so the fact travels with
    // the caps rather than being rediscovered per title.
    bool camera_follows_aim    = false;
};

// Actuation FOUNDATIONS. None = no writable state and no stick loop -> the caller must leave the
// game's own aim alone (rungs 3/4, synth input / mesh cosmetic, are not foundations and not here).
enum class Actuation { WriteState, StickLoop, None };
// Convergence strategies, best-first. DirectionOnly is the universal floor (needs nothing but the
// direction write plus the game's native aim-assist).
enum class Converge  { TraceReaim, OriginRelocation, FixedRange, DirectionOnly };

// Best-available with a guaranteed floor. The core branches on caps, never on the game.
inline Actuation select_actuation(const AimCaps& c) {
    if (c.can_write_state) return Actuation::WriteState;
    if (c.has_stick_loop)  return Actuation::StickLoop;
    return Actuation::None;
}
inline Converge select_converge(const AimCaps& c) {
    if (c.has_sightline_trace)   return Converge::TraceReaim;
    if (c.has_origin_relocation) return Converge::OriginRelocation;
    if (c.has_fixed_range_reaim) return Converge::FixedRange;
    return Converge::DirectionOnly;
}

// ---- pure geometry -------------------------------------------------------------------------------
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

// ANGLE/VECTOR CONVENTION -- a PRECONDITION, not a suggestion. The core works in the single basis:
//   unit(yaw,pitch) = { cos(yaw)cos(pitch), sin(yaw)cos(pitch), sin(pitch) }   [yaw about +Z, x fwd]
// `delta` handed to converge_trace_reaim MUST live in this same basis, and the yaw/pitch read back
// are in it. Mapping the game's own frame into and out of this basis is the PROVIDER'S job. A delta
// supplied in a different frame produces a plausible-but-wrong correction that nothing here can catch
// -- the classic frame error whose tell is a value near a right angle, not a bad hand pose. Nothing
// pure can validate the caller's basis, so this contract is enforced by the provider (and its unit
// test, test_reach_basis), not here.

// TRACE-AND-REAIM IS THE AIM PATH, NOT ONE OPTION AMONG SEVERAL. It declines ONLY when it cannot
// resolve the shot: the geometry is degenerate, or the title cannot actuate the aim it needs. The other
// reconciliations (origin relocation, fixed-range re-aim) are the worst-case fallback for exactly
// those cases, never an alternative.
//
// There is deliberately NO correction-size rail. Declining a large bend (say, over 20 deg) as "a wrong
// range" is right only while the ray starts within arm's reach of the shot origin. With the
// player standing away from the character (roomscale off, or the body still catching up), a big bend
// is the geometry itself -- 2.4 m of offset at 7 m is 19 deg -- and every decline hands the shot to a
// fallback that cannot reach (relocation refuses beyond 1.5 m), so it leaves stock, parallel to the
// ray and off by the whole offset (measured). The range comes from the engine's own
// crosshair-trace query and the offset from the frame both sides render, so a big bend is not evidence
// that either is wrong.
struct ConvergeRails {
    float min_divergence_cm = 3.0f;   // below this eye-vs-origin offset the unbent aim is ALREADY
                                      // converged: nothing to bend, and it counts as converged
    float range_floor_cm    = 1.0f;   // degenerate guard only: a hit this close defines no direction
    float max_pitch_deg     = 90.0f;  // the steepest aim the title's control record accepts; a bend
                                      // past it cannot be actuated exactly: converge_trace_reaim (Reach)
                                      // declines, converge_direction clamps and stays converged
};

// Range smoothing. The correction goes as 1/range, so sweeping off a near wall onto a far one would
// snap the aim; EMA it. A MISS must hold the last range (sky has none) -- the caller skips feeding on
// a miss rather than passing a fabricated range here.
inline float ema_range(float cur_cm, float sample_cm, float alpha) {
    if (!(cur_cm > 0.0f)) return sample_cm;
    return cur_cm + (sample_cm - cur_cm) * alpha;
}
// Smooth the INVERSE range (what the correction is proportional to), not the range. Smoothing range
// makes far->near slow: moving from a 30 m wall to an enemy at 2 m, a range EMA is still ~14 m after
// one time constant and the shot misses by most of the eye offset; the inverse is half-way there.
// cur_inv 0 = no range yet (the first sample is taken as-is).
inline float ema_inverse_range(float cur_inv_per_cm, float sample_cm, float alpha) {
    if (!(sample_cm > 0.0f) || !std::isfinite(sample_cm)) return cur_inv_per_cm;
    const float sample_inv = 1.0f / sample_cm;
    if (!(cur_inv_per_cm > 0.0f)) return sample_inv;
    return cur_inv_per_cm + (sample_inv - cur_inv_per_cm) * alpha;
}
// Standard time-constant EMA weight for a per-tick feed (tau in ms, dt in seconds).
inline float ema_alpha(float tau_ms, float dt_s) {
    if (!(tau_ms > 0.0f) || !(dt_s > 0.0f)) return 1.0f;
    const float a = 1.0f - std::exp(-(dt_s * 1000.0f) / tau_ms);
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// Recover (yaw,pitch) [degrees] from a direction in the core's basis. False on a degenerate vector.
inline bool angles_from_dir(const Vec3& d, float& yaw_deg, float& pitch_deg) {
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(len) || !(len > 1.0e-6f)) return false;
    constexpr float R2D = 57.295779513082320f;
    yaw_deg = std::atan2(d.y, d.x) * R2D;
    float c = d.z / len;
    if (c < -1.0f) c = -1.0f; else if (c > 1.0f) c = 1.0f;
    pitch_deg = std::asin(c) * R2D;
    return true;
}

// TRACE-AND-REAIM. Bend (yaw,pitch) [degrees] so a shot from the shot origin passes through the point
// the sightline hits: aim = normalize(delta + range * sightline), delta = eye - shot_origin (cm, in
// the basis above). The shot origin cancels, so nothing depends on two coordinate spaces agreeing.
// Returns true when the aim now converges (bent, or already converged because the offset is below
// min_divergence_cm). Returns false and LEAVES THE ANGLES UNTOUCHED only when it cannot resolve (see
// ConvergeRails), so the caller falls back to its worst-case path -- safe to apply unconditionally.
inline bool converge_trace_reaim(float& yaw_deg, float& pitch_deg,
                                 const Vec3& delta, float range_cm, const ConvergeRails& r) {
    const float m2 = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (!std::isfinite(m2)) return false;
    if (m2 <= r.min_divergence_cm * r.min_divergence_cm) return true;    // already converged: no bend
    if (!std::isfinite(range_cm) || !(range_cm > r.range_floor_cm))      return false;

    constexpr float D2R = 0.017453292519943295f;
    constexpr float R2D = 57.295779513082320f;
    const float cp = std::cos(pitch_deg * D2R);
    const float ux = cp * std::cos(yaw_deg * D2R);
    const float uy = cp * std::sin(yaw_deg * D2R);
    const float uz = std::sin(pitch_deg * D2R);

    const float tx = delta.x + ux * range_cm;
    const float ty = delta.y + uy * range_cm;
    const float tz = delta.z + uz * range_cm;
    const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (!std::isfinite(len) || !(len > 1.0f)) return false;

    const float ny = std::atan2(ty, tx) * R2D;
    float c = tz / len;
    if (c < -1.0f) c = -1.0f; else if (c > 1.0f) c = 1.0f;
    const float np = std::asin(c) * R2D;
    if (!std::isfinite(ny) || !std::isfinite(np)) return false;
    // The one physical limit: an aim steeper than the control record accepts would be clamped on the
    // way in and miss, so it cannot resolve here. Reach (the only caller of this angle form) keeps this
    // decline on purpose (R13r): its fallback relocates the hand shot exactly, which lands a close
    // floor or ceiling shot better than a clamped converged aim. converge_direction clamps instead.
    if (std::fabs(np) > r.max_pitch_deg) return false;

    yaw_deg = ny;
    pitch_deg = np;
    return true;
}

// TRACE-AND-REAIM as a DIRECTION, for a title whose aim record is a vector, or whose closed loop steers
// toward one: the unit direction from the engine eye E that passes through the point the intent ray hits,
// out = normalize((H - E) + dir * range), with delta = H - E. `delta` and `range` are in the title's own
// units and `cm_per_unit` converts them for the rails (which are in cm), so every title shares one policy.
// `dir` need not be exactly unit length; it is normalized first. Same contract as converge_trace_reaim:
// true when the aim converges (bent, or the offset is below min_divergence_cm, which returns `dir`
// itself); false, `out` untouched, only when it cannot resolve.
//
// THE PITCH RAIL CLAMPS here (Halo 3, ODST and Halo 4 -- the H2/CE callers pass 90, where it is
// inert). A bend steeper than max_pitch_deg -- a close ceiling or floor with the head well off the engine eye --
// cannot be actuated exactly, but it is natural geometry, not our code failing, so it does not decline (a decline
// would hand the write the unbent steer, a jump by the whole bend, and switch convergence off). The aim is held
// AT the rail and stays converged. Its heading: the bent aim's own near the rail (continuous with the unclamped
// bend), handing over to the intent ray's as the bent aim nears the pole, where atan2 of a vanishing horizontal
// whips through any heading at the smallest change. Weight w^2, w = |bent horizontal| / cos(rail).
//
// THAT BLEND HAS A SINGULARITY, and no blend can avoid one: mapping every bent aim past the rail onto a heading
// that agrees with the bent aim's own at the rail is a retraction of a disk onto its rim. It sits where the bent
// heading points AWAY from the hand's and w^2 = 1/2 (a bent pitch near 85 deg at an 83 deg rail): a hand at 80 deg
// under a ceiling 0.5 m up, with the engine eye 13 cm ahead of the rendered head, sweeps the output heading through
// 180 deg in half a millimetre of eye motion -- a 14 deg whip of the written aim near vertical. So the clamp keeps
// a RailHold (caller-owned, one per actuation loop): inside the band where the blend nearly cancels
// (|mix| < kRailHoldEnter) it holds the heading it wrote last poll, and lets go only once the blend agrees with
// that heading again (within ~10 deg, out of the band) or is decisively on the far side (|mix| > kRailHoldLeave).
// A hand that jitters at the singularity then moves nothing; one that really crosses it flips the heading once.
// Without a hold (nullptr) the clamp is the stateless blend. Reach's angle form keeps its decline (its fallback relocates
// the shot exactly; see converge_trace_reaim).
struct RailHold {
    bool active = false;    // the previous call clamped at the rail and wrote heading (hx, hy)
    bool holding = false;   // that heading is being held through the blend's cancellation band
    float hx = 0.0f, hy = 0.0f;
    void reset() { active = holding = false; hx = hy = 0.0f; }
};
constexpr float kRailHoldEnter = 0.25f;   // |mix| below this: hold the heading written last
constexpr float kRailHoldLeave = 0.5f;    // |mix| above this: let go, whatever its heading
constexpr float kRailHoldAgree = 0.985f;  // or once the blend's heading is within ~10 deg of the held one

inline bool clamp_to_pitch_rail(const Vec3& bent, const Vec3& intent, float max_pitch_deg, Vec3& out,
                                RailHold* hold = nullptr) {
    constexpr float D2R = 0.017453292519943295f;
    const float cr = std::cos(max_pitch_deg * D2R), sr = std::sin(max_pitch_deg * D2R);
    if (!(cr > 1.0e-6f)) { if (hold) hold->reset(); return false; }
    const float bh = std::sqrt(bent.x * bent.x + bent.y * bent.y);
    const float uh = std::sqrt(intent.x * intent.x + intent.y * intent.y);
    float w = bh / cr;
    w = w > 1.0f ? 1.0f : w;
    w *= w;
    float mx = 0.0f, my = 0.0f;
    if (bh > 1.0e-6f) { mx += w * bent.x / bh; my += w * bent.y / bh; }
    if (uh > 1.0e-6f) { mx += (1.0f - w) * intent.x / uh; my += (1.0f - w) * intent.y / uh; }
    const float ml = std::sqrt(mx * mx + my * my);
    if (!std::isfinite(ml)) { if (hold) hold->reset(); return false; }
    float hx = 0.0f, hy = 0.0f;
    bool have = ml > 1.0e-4f;
    if (have) { hx = mx / ml; hy = my / ml; }
    if (hold) {
        if (hold->active && hold->holding) {
            const bool agree = have && hx * hold->hx + hy * hold->hy > kRailHoldAgree;
            if (have && (ml > kRailHoldLeave || (ml >= kRailHoldEnter && agree)))
                hold->holding = false;
            else { hx = hold->hx; hy = hold->hy; have = true; }
        } else if (hold->active && ml < kRailHoldEnter) {
            hold->holding = true;
            hx = hold->hx; hy = hold->hy; have = true;
        } else if (!hold->active && ml < kRailHoldEnter) {
            // The first clamped call is already in the band: start from the intent ray's heading, and hold it.
            hold->holding = true;
            if (uh > 1.0e-6f) { hx = intent.x / uh; hy = intent.y / uh; have = true; }
        }
    }
    if (!have) {
        // The two headings cancel exactly (the bend went over the pole): the intent ray's, else the bent aim's.
        if (uh > 1.0e-6f) { hx = intent.x / uh; hy = intent.y / uh; }
        else if (bh > 1.0e-6f) { hx = bent.x / bh; hy = bent.y / bh; }
        else { if (hold) hold->reset(); return false; }   // both vertical: no heading at all
    }
    out = Vec3{cr * hx, cr * hy, bent.z < 0.0f ? -sr : sr};
    if (hold) { hold->active = true; hold->hx = hx; hold->hy = hy; }
    return true;
}

// `hold` (optional): the caller's RailHold for clamp_to_pitch_rail. Any call that does not clamp -- the aim is
// under the rail, already converged, or does not resolve -- forgets it.
inline bool converge_direction(const Vec3& delta, const Vec3& dir, float range, float cm_per_unit,
                               const ConvergeRails& r, Vec3& out, RailHold* hold = nullptr) {
    const auto unclamped = [hold](bool result) { if (hold) hold->reset(); return result; };
    if (!std::isfinite(cm_per_unit) || !(cm_per_unit > 0.0f)) return unclamped(false);
    const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (!std::isfinite(dl) || !(dl > 1.0e-6f)) return unclamped(false);
    const Vec3 u{dir.x / dl, dir.y / dl, dir.z / dl};
    const float m2 = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (!std::isfinite(m2)) return unclamped(false);
    const float minDiv = r.min_divergence_cm / cm_per_unit;
    if (m2 <= minDiv * minDiv) { out = u; return unclamped(true); }       // already converged: no bend
    if (!std::isfinite(range) || !(range * cm_per_unit > r.range_floor_cm)) return unclamped(false);
    const Vec3 t{delta.x + u.x * range, delta.y + u.y * range, delta.z + u.z * range};
    const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
    if (!std::isfinite(len) || !(len * cm_per_unit > 1.0f)) return unclamped(false);
    const Vec3 a{t.x / len, t.y / len, t.z / len};
    float c = a.z; c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    if (std::fabs(std::asin(c) * 57.295779513082320f) > r.max_pitch_deg)
        return clamp_to_pitch_rail(a, u, r.max_pitch_deg, out, hold);   // held at the rail, still converged
    out = a;
    return unclamped(true);
}

// A CHASE CAMERA'S CROSSHAIR LINE, as a shot origin. A title that aims vehicle weapons at the point under
// a third-person camera's crosshair traces from that camera along its forward (Reach: HREK aim_assist ->
// following-camera position and forward). That camera ORBITS a pivot as the aim turns, so its position
// is not a fixed origin: bending the aim from it moves it, and from a camera several metres back a near
// target never settles. The orbit keeps the camera's line passing close to the pivot, so the point to
// converge from is the point of that line nearest the pivot, which barely moves as the aim turns. On foot
// or in a first-person seat the pivot is the camera, and this returns the camera itself. A pivot behind
// the camera also returns the camera. False (out untouched) on a degenerate forward.
inline bool line_point_nearest(const Vec3& origin, const Vec3& forward, const Vec3& pivot, Vec3& out) {
    const float len = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (!std::isfinite(len) || !(len > 1.0e-6f)) return false;
    const Vec3 f{forward.x / len, forward.y / len, forward.z / len};
    float t = (pivot.x - origin.x) * f.x + (pivot.y - origin.y) * f.y + (pivot.z - origin.z) * f.z;
    if (!std::isfinite(t)) return false;
    if (t < 0.0f) t = 0.0f;
    out = Vec3{origin.x + f.x * t, origin.y + f.y * t, origin.z + f.z * t};
    return true;
}

// HALO: CAMPAIGN EVOLVED'S RETICLE PLACEMENT (Plugin.cpp onfoot_reticule_tick), for a marker drawn on
// an EYE-anchored intent ray -- the ray converge_trace_reaim bends the shot onto. On a hit the marker
// sits on the surface, pulled back `surface_off_m` (never closer than 20 cm); past `cap_m`, and on a
// miss, it parks at the cap. On an eye-anchored ray every depth projects onto the same background
// point, so parking it short costs nothing.
// Its WORLD width is `scale` x the width that subtends `size_deg` at the cap, where scale ramps
// linearly from `min_scale` at `min_scale_dist_m` up to 1 at the cap: near-constant apparent size out
// to a few metres, shrinking away as a surface closes in on the muzzle.
struct TracedReticle { float dist_m = 0.0f; float width_m = 0.0f; };
inline TracedReticle traced_reticle_placement(bool hit, float range_m, float cap_m, float surface_off_m,
                                              float min_scale_dist_m, float min_scale, float size_deg) {
    TracedReticle out;
    float d = cap_m;
    if (hit && std::isfinite(range_m) && range_m <= cap_m) {
        d = range_m - surface_off_m;
        if (d < 0.2f) d = 0.2f;
    }
    float s = 1.0f;
    if (cap_m > min_scale_dist_m) {
        float t = (d - min_scale_dist_m) / (cap_m - min_scale_dist_m);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        s = min_scale + (1.0f - min_scale) * t;
    }
    out.dist_m = d;
    out.width_m = s * 2.0f * cap_m * std::tan(size_deg * 0.5f * 0.017453292519943295f);
    return out;
}

// SHOTS FROM THE MUZZLE (the player key shots_from_muzzle). A shot that leaves the visible muzzle M and
// passes through the converged point P -- the point the converged aim's own line from the engine eye
// passes through -- lands where that aim lands, while its tracer and projectile come out of the gun:
// out = normalize(P - M), and `up` is re-orthogonalized against it. Refused (out and up untouched, so the
// shot leaves the eye exactly as before), each for its own counted reason, when:
//   - Obstructed: the title's own obstruction check pulled the muzzle back more than max_clamp_cm (the gun
//     is through a surface the eye sees past: from the muzzle the shot would hit that surface);
//   - PointNotAhead: P less than min_ahead_cm ahead of the muzzle along the intent ray (a point behind or
//     beside the gun, or a wall at its muzzle);
//   - NonFinite: any non-finite or degenerate input.
// NO DISTANCE-FROM-THE-EYE LIMIT (a size limit cannot tell a right answer from a wrong one). A rail such as
// "a muzzle over 150 cm from the engine eye is stale or foreign" is not needed:
// every caller already proves that by identity before it gets here: each title reads the muzzle from a
// weapon_muzzle receipt that must match this title, generation, the firing unit, the weapon being fired, its
// slot, barrel and handedness, and be under 100 ms old (weapon_muzzle::Fresh). Where the gun sits relative to
// the eye is the player's arm, not whose gun it is.
// Distances in the title's own units, converted by cm_per_unit.
enum class MuzzleShot : uint8_t { Aimed = 0, NonFinite, Obstructed, PointNotAhead, Count };
inline const char* MuzzleShotName(MuzzleShot why) {
    switch (why) {
    case MuzzleShot::Aimed: return "aimed";
    case MuzzleShot::NonFinite: return "non-finite";
    case MuzzleShot::Obstructed: return "gun through a surface";
    case MuzzleShot::PointNotAhead: return "point not ahead of the muzzle";
    default: return "?";
    }
}
struct MuzzleShotRails {
    float max_clamp_cm    = 5.0f;
    float min_ahead_cm    = 10.0f;
};
inline MuzzleShot muzzle_shot_direction(const Vec3& muzzle_before_clamp, const Vec3& muzzle, const Vec3& point,
                                        const Vec3& ray_dir, float cm_per_unit, const MuzzleShotRails& r,
                                        Vec3& out, Vec3& up) {
    if (!std::isfinite(cm_per_unit) || !(cm_per_unit > 0.0f)) return MuzzleShot::NonFinite;
    const auto len = [](const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    const Vec3 clamp{muzzle.x - muzzle_before_clamp.x, muzzle.y - muzzle_before_clamp.y,
                     muzzle.z - muzzle_before_clamp.z};
    const Vec3 toPoint{point.x - muzzle.x, point.y - muzzle.y, point.z - muzzle.z};
    const float clampLen = len(clamp), pointLen = len(toPoint), dirLen = len(ray_dir);
    if (!std::isfinite(clampLen) || !std::isfinite(pointLen) || !std::isfinite(dirLen) ||
        !(dirLen > 1.0e-6f) || !(pointLen > 1.0e-6f))
        return MuzzleShot::NonFinite;
    if (clampLen * cm_per_unit > r.max_clamp_cm) return MuzzleShot::Obstructed;
    const float ahead = (toPoint.x * ray_dir.x + toPoint.y * ray_dir.y + toPoint.z * ray_dir.z) / dirLen;
    if (!std::isfinite(ahead)) return MuzzleShot::NonFinite;
    if (ahead * cm_per_unit < r.min_ahead_cm) return MuzzleShot::PointNotAhead;
    const Vec3 f{toPoint.x / pointLen, toPoint.y / pointLen, toPoint.z / pointLen};
    // Up keeps its own lean, minus whatever now lies along the new forward; a degenerate one (up along the
    // shot) falls back to the up nearest world +Z.
    const float d = up.x * f.x + up.y * f.y + up.z * f.z;
    Vec3 u{up.x - f.x * d, up.y - f.y * d, up.z - f.z * d};
    float ul = len(u);
    if (!std::isfinite(ul) || !(ul > 1.0e-4f)) {
        u = Vec3{-f.x * f.z, -f.y * f.z, 1.0f - f.z * f.z};
        ul = len(u);
        if (!std::isfinite(ul) || !(ul > 1.0e-4f)) {
            u = Vec3{1.0f, 0.0f, 0.0f};
            ul = 1.0f;
        }
    }
    out = f;
    up = Vec3{u.x / ul, u.y / ul, u.z / ul};
    return MuzzleShot::Aimed;
}

} // namespace aim_solve
