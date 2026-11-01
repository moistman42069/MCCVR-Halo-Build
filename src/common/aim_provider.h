#pragma once

// AIM PROVIDER -- the per-game half of the aim solution, plus the orchestrator that composes it.
//
// The seam: the CORE (aim_solve_logic.h) is engine-free and picks the strategy from caps(); the
// PROVIDER owns every engine-specific fact behind these methods. The core hands down a CONVERGED
// setpoint; the provider actuates it. Direct drive and convergence compose -- actuate the converged
// aim, never the raw hand aim.
//
// Why this exists: MCC spans six Blam generations (CE 2001 .. H4 2012, H4 a 343 fork) plus Saber
// Anniversary renderers on CE/H2, so each title exposes its aim state, shot origin, and collision
// differently. A single hardcoded solution is impossible; this interface absorbs the divergence so
// the core stays one tested body of code. See the Engineering Vault concept
// "multi-engine-aim-provider-interface".
//
// This header is engine-free: it declares an abstract contract over the pure types in
// aim_solve_logic.h, plus a pure orchestrator that drives a provider. The concrete providers (e.g.
// src/dll/reach_aim_provider.*) are engine-bound.

#include "aim_solve_logic.h"

namespace aim_solve {

struct Ray    { Vec3 origin; Vec3 dir; };            // world/game space, `dir` need not be normalized
struct Angles { float yaw_deg = 0.0f; float pitch_deg = 0.0f; };

// Result of an actuation attempt. Reported so a caller can prove the REAL path ran, not merely that a
// hook installed ("installed" is not "running"), and whether the write is the replicated one.
struct ActuateResult {
    bool applied = false;                 // the provider acted this tick
    bool wrote_replicated_state = false;  // true only for a verified rung-1 write on the wire
};

// One implementation per title. Called on the game/aim path: keep it allocation-free, deterministic,
// and fail-closed (return false / applied=false and say so in the log, never write a guessed field).
class IAimProvider {
public:
    virtual ~IAimProvider() = default;

    // Which title this is, for logging/selection (e.g. "reach"). Static string, never null.
    virtual const char* title() const = 0;

    // What this title supports -- drives the core's strategy selection. May change at runtime (a
    // record located mid-session flips can_write_state), so it is queried, not cached by the core.
    virtual AimCaps caps() const = 0;

    // (B) INTENT RAY: the authored muzzle bore rotated by the live pose (has_barrel_marker), else the
    //     controller ray. Origin + direction in game space (the core's basis). False when unavailable.
    virtual bool intent_ray(Ray* out) = 0;

    // (C) TRACE the sightline into THIS engine's own collision. Range in cm on a blocking hit; false
    //     on a miss or when the title has no trace -- the caller then HOLDS the last range. Only
    //     meaningful when caps().has_sightline_trace.
    virtual bool trace_impact_cm(const Ray& sightline, float* out_cm) = 0;

    // (C) Measured eye-minus-shot-origin offset (cm, in the core's basis); false until known. This is
    //     the `delta` converge_trace_reaim() consumes. (MCC: engine eye - base camera; CampE: the VR
    //     stereo callbacks.) The provider averages to the cyclopean eye so half an IPD is not baked in
    //     as a standing bias, AND maps into the core's basis (see aim_solve_logic.h's PRECONDITION).
    virtual bool origin_delta(Vec3* out) = 0;

    // (A) ACTUATE the CONVERGED setpoint. WriteState writes the replicated control record; StickLoop
    //     drives the game's own stick input toward `converged` (reading the game's current aim
    //     internally). The core selects exactly one and never both. Reports what it actually did.
    virtual ActuateResult actuate(Actuation how, const Angles& converged) = 0;

    // (C, provider-owned strategies) When the core selects OriginRelocation / FixedRange /
    // DirectionOnly, the reconciliation lives in the engine (a firing-site hook, or the
    // reticle-distance re-aim already in the game code, or nothing beyond native aim-assist). This
    // hook lets the provider run/confirm it per tick; TraceReaim instead runs in the core via
    // converge_trace_reaim(). No-op by default.
    virtual void apply_engine_convergence(Converge /*selected*/, const Ray& /*intent*/) {}
};

// ---- the orchestrator (THE COMPOSITION CONTRACT, encoded) ----------------------------------------
// Per tick the pieces compose in exactly this order; nothing else is a valid composition:
//   1. caps -> select actuation; if None, fail closed (do not touch the game's aim).
//   2. intent_ray -> the desired aim; derive its (yaw,pitch).
//   3. select convergence:
//        TraceReaim -> trace the sightline for a range (EMA-smoothed, held on a miss), read the
//                      eye/origin delta, and bend the setpoint here in the core (no-op if not live).
//        else       -> the provider applies the engine-owned reconciliation; the setpoint stays the
//                      intent (relocation/fixed-range/direction-only move the shot, not the angles).
//   4. actuate the CONVERGED setpoint (== intent when convergence is engine-owned).
// Pure and engine-free: it drives the game only through the provider, so it is unit-testable with a
// mock provider. `st` holds the smoothed range across ticks.
// The smoothed range is kept as its INVERSE (see ema_inverse_range); range_cm is the derived value.
struct SolveState { float range_cm = 0.0f; float inv_range_per_cm = 0.0f; bool converged = false; };

inline ActuateResult solve_and_actuate(IAimProvider& p, SolveState& st,
                                       const ConvergeRails& rails, float dt_s, float range_tau_ms) {
    st.converged = false;
    const AimCaps caps = p.caps();
    const Actuation act = select_actuation(caps);
    if (act == Actuation::None) return {};        // fail closed: no way to drive this title's aim

    Ray intent;
    if (!p.intent_ray(&intent)) return {};         // no aim source this tick

    Angles a;
    if (!angles_from_dir(intent.dir, a.yaw_deg, a.pitch_deg)) return {};

    const Converge cv = select_converge(caps);
    if (cv == Converge::TraceReaim) {
        float hit_cm = 0.0f;
        if (p.trace_impact_cm(intent, &hit_cm)) {  // miss -> hold the last range (sky has no range)
            st.inv_range_per_cm = ema_inverse_range(st.inv_range_per_cm, hit_cm, ema_alpha(range_tau_ms, dt_s));
            st.range_cm = st.inv_range_per_cm > 0.0f ? 1.0f / st.inv_range_per_cm : 0.0f;
        }
        Vec3 delta;
        if (st.range_cm > 0.0f && p.origin_delta(&delta))
            st.converged = converge_trace_reaim(a.yaw_deg, a.pitch_deg, delta, st.range_cm, rails); // no-op if not live
    } else {
        p.apply_engine_convergence(cv, intent);    // origin stays engine-owned; angles = intent
    }

    return p.actuate(act, a);                      // actuate the CONVERGED setpoint
}

} // namespace aim_solve
