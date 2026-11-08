// Unit tests for the engine-free aim-solve core + provider orchestrator (src/common/aim_solve_logic.h,
// src/common/aim_provider.h). No game, no engine -- a mock provider drives solve_and_actuate. Plain
// Check() harness; non-zero exit = failure, so CTest reports it.

#include "../src/common/aim_provider.h"
#include "../src/common/aim_write_logic.h"
#include "../src/common/reach_aim_basis.h"

#include <cmath>
#include <initializer_list>
#include <string>
#include <cstdio>

using namespace aim_solve;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

// A provider whose every answer is a settable field, so a test states the game's shape as data.
struct MockProvider : IAimProvider {
    AimCaps c{};
    Ray   intent{}; bool intent_ok = true;
    bool  trace_ok = false; float trace_cm = 0.0f;
    bool  delta_ok = false; Vec3 delta{};

    // observations
    bool actuated = false; Actuation last_how = Actuation::None; Angles last_angles{};
    bool engine_conv = false; Converge last_conv = Converge::DirectionOnly;
    int  trace_calls = 0;

    const char* title() const override { return "mock"; }
    AimCaps caps() const override { return c; }
    bool intent_ray(Ray* o) override { if (!intent_ok) return false; *o = intent; return true; }
    bool trace_impact_cm(const Ray&, float* o) override { ++trace_calls; if (!trace_ok) return false; *o = trace_cm; return true; }
    bool origin_delta(Vec3* o) override { if (!delta_ok) return false; *o = delta; return true; }
    ActuateResult actuate(Actuation how, const Angles& a) override {
        actuated = true; last_how = how; last_angles = a;
        return { true, how == Actuation::WriteState };
    }
    void apply_engine_convergence(Converge cv, const Ray&) override { engine_conv = true; last_conv = cv; }
};

static void test_selectors() {
    AimCaps c{};
    CHECK(select_actuation(c) == Actuation::None);           // nothing available -> fail closed
    c.has_stick_loop = true;
    CHECK(select_actuation(c) == Actuation::StickLoop);
    c.can_write_state = true;
    CHECK(select_actuation(c) == Actuation::WriteState);      // state write beats stick

    AimCaps v{};
    CHECK(select_converge(v) == Converge::DirectionOnly);     // floor
    v.has_fixed_range_reaim = true;
    CHECK(select_converge(v) == Converge::FixedRange);
    v.has_origin_relocation = true;
    CHECK(select_converge(v) == Converge::OriginRelocation);
    v.has_sightline_trace = true;
    CHECK(select_converge(v) == Converge::TraceReaim);        // trace beats all
}

static void test_angles_from_dir() {
    float y = 9, p = 9;
    CHECK(angles_from_dir(Vec3{1, 0, 0}, y, p) && near(y, 0) && near(p, 0));
    CHECK(angles_from_dir(Vec3{0, 1, 0}, y, p) && near(y, 90) && near(p, 0));
    CHECK(angles_from_dir(Vec3{0, 0, 1}, y, p) && near(p, 90));
    CHECK(!angles_from_dir(Vec3{0, 0, 0}, y, p));            // degenerate -> false
}

static void test_converge() {
    ConvergeRails r{};
    // Below min_divergence the unbent aim is ALREADY converged: true, and the angles are untouched.
    {
        float y = 3.0f, p = -2.0f;
        CHECK(converge_trace_reaim(y, p, Vec3{1, 0, 0}, 1000.0f, r) && near(y, 3.0f) && near(p, -2.0f));
    }
    // Only a degenerate range cannot resolve (the trace never reports under its 5 cm start); near
    // contact resolves: a 10 cm offset at 10 cm is a 45 deg bend onto the point the gun touches.
    {
        float y = 0, p = 0;
        CHECK(!converge_trace_reaim(y, p, Vec3{0, 10, 0}, 0.5f, r) && near(y, 0) && near(p, 0));
        CHECK(converge_trace_reaim(y, p, Vec3{0, 10, 0}, 10.0f, r) && near(y, 45.0f, 1e-3f));
    }
    // A known correction: aim +x, eye 10 cm off in +y, target 1000 cm out -> ~0.573 deg of +yaw.
    {
        float y = 0, p = 0;
        CHECK(converge_trace_reaim(y, p, Vec3{0, 10, 0}, 1000.0f, r));
        CHECK(near(y, std::atan2(10.0f, 1000.0f) * 57.29578f) && near(p, 0));
    }
    // NO correction-size rail: a big bend is geometry, not an error.
    // 4.2 m of offset at 7.7 m (29 deg) -- a case a 20 deg rail would hand to a fallback that leaves the
    // shot parallel and 4.2 m off -- converges, onto the target point.
    {
        float y = 0, p = 0;
        CHECK(converge_trace_reaim(y, p, Vec3{0, 420, 0}, 770.0f, r));
        CHECK(near(y, std::atan2(420.0f, 770.0f) * 57.29578f, 1e-3f) && near(p, 0));
    }
    // ...and so does a bend the character must fire backwards to make: 4 m behind and 1 m beside him,
    // aiming 3 m ahead, the target is behind him (135 deg).
    {
        float y = 0, p = 0;
        CHECK(converge_trace_reaim(y, p, Vec3{-400, 100, 0}, 300.0f, r) && near(y, 135.0f, 1e-3f));
    }
    // The one physical limit: an aim steeper than the control record accepts cannot resolve. A target
    // almost straight below the shot origin needs -86.6 deg; with Reach's 1.45 rad (83 deg) limit it
    // declines, and without one it converges.
    {
        const Vec3 delta{-290, 0, -170};   // the hand 2.9 m behind, 1.7 m below; aiming +x, 3 m out
        ConvergeRails reach = r;
        reach.max_pitch_deg = 1.45f * 57.29578f;
        float y = 0, p = 0;
        CHECK(!converge_trace_reaim(y, p, delta, 300.0f, reach) && near(y, 0) && near(p, 0));
        CHECK(converge_trace_reaim(y, p, delta, 300.0f, r) && near(p, -std::atan2(170.0f, 10.0f) * 57.29578f, 1e-3f));
    }
    // Every bend lands on the target: a 20 cm lateral offset at 1.5 m while aiming 70 deg down.
    {
        float y = 0, p = -70.0f;
        const Vec3 delta{0, 20, 0};
        const float range = 150.0f;
        CHECK(converge_trace_reaim(y, p, delta, range, r));
        // ...and the corrected direction passes through the target point delta + range * u.
        const float u[3]{std::cos(-70.0f / 57.29578f), 0.0f, std::sin(-70.0f / 57.29578f)};
        const float t[3]{delta.x + u[0] * range, delta.y + u[1] * range, delta.z + u[2] * range};
        const float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        const float cp = std::cos(p / 57.29578f);
        const float d[3]{cp * std::cos(y / 57.29578f), cp * std::sin(y / 57.29578f), std::sin(p / 57.29578f)};
        CHECK(near(d[0], t[0] / tl, 1e-4f) && near(d[1], t[1] / tl, 1e-4f) && near(d[2], t[2] / tl, 1e-4f));
    }
    // Inverse-range smoothing: from 30 m toward 2 m at alpha 0.5 lands near 3.75 m, not 16 m.
    {
        const float inv = ema_inverse_range(1.0f / 3000.0f, 200.0f, 0.5f);
        CHECK(near(1.0f / inv, 375.0f, 1.0f));
        CHECK(near(ema_inverse_range(0.0f, 200.0f, 0.5f), 1.0f / 200.0f, 1e-7f));   // first sample taken as-is
        CHECK(ema_inverse_range(0.01f, -5.0f, 0.5f) == 0.01f);                      // a bad sample changes nothing
    }
}

// Halo: Campaign Evolved's reticle placement, with its defaults (6 m cap, 50 cm standoff, 70 cm / 0.001).
static void test_traced_reticle() {
    const float cap = 6.0f, off = 0.5f, md = 0.7f, ms = 0.001f, deg = 10.0f;
    const float capWidth = 2.0f * cap * std::tan(deg * 0.5f / 57.29578f);
    // A wall at 4 m: drawn on it, pulled back 50 cm; world width ramps with distance.
    TracedReticle r = traced_reticle_placement(true, 4.0f, cap, off, md, ms, deg);
    CHECK(near(r.dist_m, 3.5f, 1e-4f));
    CHECK(near(r.width_m, (ms + (1.0f - ms) * (3.5f - md) / (cap - md)) * capWidth, 1e-4f));
    // Beyond the cap, and on a miss: parked at the cap, at the full (cap) size.
    r = traced_reticle_placement(true, 30.0f, cap, off, md, ms, deg);
    CHECK(near(r.dist_m, cap, 1e-4f) && near(r.width_m, capWidth, 1e-4f));
    r = traced_reticle_placement(false, 0.0f, cap, off, md, ms, deg);
    CHECK(near(r.dist_m, cap, 1e-4f) && near(r.width_m, capWidth, 1e-4f));
    // Muzzle against a surface: floored at 20 cm and shrunk to the minimum (it fades out).
    r = traced_reticle_placement(true, 0.3f, cap, off, md, ms, deg);
    CHECK(near(r.dist_m, 0.2f, 1e-4f) && near(r.width_m, ms * capWidth, 1e-5f));
    // Apparent size holds near the cap and falls away up close (CampE's "scales out when too close").
    const float at3 = traced_reticle_placement(true, 3.5f, cap, off, md, ms, deg).width_m / 3.0f;
    const float at1 = traced_reticle_placement(true, 1.5f, cap, off, md, ms, deg).width_m / 1.0f;
    CHECK(at3 > 0.8f * capWidth / cap && at1 < 0.5f * capWidth / cap);
}

static void test_orchestrator() {
    // No foundation -> fail closed, never actuates.
    {
        MockProvider m; m.intent = Ray{ Vec3{}, Vec3{1, 0, 0} };
        SolveState st{};
        ActuateResult res = solve_and_actuate(m, st, ConvergeRails{}, 0.011f, 120.0f);
        CHECK(!res.applied && !m.actuated);
    }
    // Stick loop + no convergence caps -> DirectionOnly engine conv, actuate raw intent on the stick.
    {
        MockProvider m; m.c.has_stick_loop = true; m.intent = Ray{ Vec3{}, Vec3{0, 1, 0} };
        SolveState st{};
        ActuateResult res = solve_and_actuate(m, st, ConvergeRails{}, 0.011f, 120.0f);
        CHECK(res.applied && m.actuated && m.last_how == Actuation::StickLoop);
        CHECK(m.engine_conv && m.last_conv == Converge::DirectionOnly);
        CHECK(m.trace_calls == 0);                            // no trace when not TraceReaim
        CHECK(near(m.last_angles.yaw_deg, 90) && near(m.last_angles.pitch_deg, 0));
    }
    // Origin relocation -> engine convergence, angles stay the intent, trace never consulted.
    {
        MockProvider m; m.c.can_write_state = true; m.c.has_origin_relocation = true;
        m.intent = Ray{ Vec3{}, Vec3{1, 0, 0} };
        SolveState st{};
        ActuateResult res = solve_and_actuate(m, st, ConvergeRails{}, 0.011f, 120.0f);
        CHECK(res.applied && res.wrote_replicated_state && m.last_how == Actuation::WriteState);
        CHECK(m.engine_conv && m.last_conv == Converge::OriginRelocation && m.trace_calls == 0);
        CHECK(near(m.last_angles.yaw_deg, 0));
    }
    // Trace-and-reaim -> core bends the setpoint from the traced range + delta.
    {
        MockProvider m; m.c.can_write_state = true; m.c.has_sightline_trace = true;
        m.intent = Ray{ Vec3{}, Vec3{1, 0, 0} };
        m.trace_ok = true; m.trace_cm = 1000.0f; m.delta_ok = true; m.delta = Vec3{0, 10, 0};
        SolveState st{};
        ActuateResult res = solve_and_actuate(m, st, ConvergeRails{}, 1.0f, 1.0f); // alpha~1: range=trace
        CHECK(res.applied && m.trace_calls == 1 && !m.engine_conv);
        CHECK(near(m.last_angles.yaw_deg, std::atan2(10.0f, 1000.0f) * 57.29578f));
        CHECK(st.converged);                                  // the core reports that it actually bent it
        // A sub-threshold offset: nothing to bend, and the aim is already converged (the firing site
        // must stand down, not relocate the shot).
        m.delta = Vec3{0, 1, 0};
        res = solve_and_actuate(m, st, ConvergeRails{}, 1.0f, 1.0f);
        CHECK(res.applied && st.converged && near(m.last_angles.yaw_deg, 0));
        // The core's generic contract: a poll whose provider reports no new range keeps the smoothed one.
        // (Reach never relies on it: it reports every fresh result, a miss as the trace's far end, and
        // with no fresh result it does not offer the trace at all.)
        m.delta = Vec3{0, 10, 0};
        m.trace_ok = false;
        res = solve_and_actuate(m, st, ConvergeRails{}, 1.0f, 1.0f);
        CHECK(res.applied && st.converged && near(st.range_cm, 1000.0f, 0.5f));
        CHECK(near(m.last_angles.yaw_deg, std::atan2(10.0f, 1000.0f) * 57.29578f));
    }
    // No intent this tick -> fail closed.
    {
        MockProvider m; m.c.has_stick_loop = true; m.intent_ok = false;
        SolveState st{};
        ActuateResult res = solve_and_actuate(m, st, ConvergeRails{}, 0.011f, 120.0f);
        CHECK(!res.applied && !m.actuated);
    }
}

// ---- aim_write_logic.h: the state-write rung's guards --------------------------------------------
static void test_unwrap_to_branch() {
    using aim_write::unwrap_to_branch;
    using aim_write::kTwoPi;
    CHECK(near(unwrap_to_branch(1.0f, 1.2f), 1.0f, 1e-5f));                   // same branch: unchanged
    // Game holds yaw on [0,2pi); the aim computes -0.1 for the same direction as 6.183. Keep 6.183.
    CHECK(near(unwrap_to_branch(-0.1f, 6.2f), kTwoPi - 0.1f, 1e-4f));
    CHECK(near(unwrap_to_branch(6.2f, -0.1f), 6.2f - kTwoPi, 1e-4f));         // and the other way
    // Accumulated game yaw far past 2*pi (never normalised): still the nearest branch.
    CHECK(near(unwrap_to_branch(0.5f, 5.0f * kTwoPi + 0.4f), 5.0f * kTwoPi + 0.5f, 1e-3f));
    CHECK(near(unwrap_to_branch(0.3f, NAN), 0.3f, 1e-6f));                    // no current value
    // Never more than half a turn from the current value.
    for (float t = -10.0f; t <= 10.0f; t += 0.37f)
        CHECK(std::fabs(unwrap_to_branch(t, 2.0f) - 2.0f) <= 3.1416f);
}

// Halo 3 / ODST quantise the control yaw as 13 bits over [0, 2pi] with no clamp; anything outside overflows
// into the action's button bits (a phantom exit-seat press). The branch-matched write at the seam lands just
// outside -- exactly the values below -- so the written yaw is wrapped into [0, 2pi).
static void test_wrap_0_2pi() {
    using aim_write::wrap_0_2pi;
    using aim_write::unwrap_to_branch;
    using aim_write::kTwoPi;
    const auto inRange = [&](float v) { return v >= 0.0f && v < kTwoPi; };
    CHECK(near(wrap_0_2pi(1.0f), 1.0f, 1e-6f));                                // in range: unchanged
    CHECK(near(wrap_0_2pi(0.0f), 0.0f, 1e-6f));
    CHECK(near(wrap_0_2pi(-0.01f), kTwoPi - 0.01f, 1e-4f));                    // just below the seam
    CHECK(near(wrap_0_2pi(kTwoPi + 0.01f), 0.01f, 1e-4f));                     // just above it
    CHECK(inRange(wrap_0_2pi(kTwoPi)) && inRange(wrap_0_2pi(-0.0f)));          // exactly the seam
    CHECK(inRange(wrap_0_2pi(std::nextafter(kTwoPi, 0.0f) - kTwoPi)));          // rounding lands on 2pi
    CHECK(std::isnan(wrap_0_2pi(NAN)));
    // The case that ejected the player: record just past the seam, target just before it (and the reverse).
    const float bad[2]{unwrap_to_branch(-0.01f, 0.01f), unwrap_to_branch(0.01f, 6.27f)};
    CHECK(!inRange(bad[0]) && !inRange(bad[1]));                               // branch matching leaves the range
    CHECK(inRange(wrap_0_2pi(bad[0])) && inRange(wrap_0_2pi(bad[1])));          // the wrap brings it back
    // A sweep through the seam from both sides: every written value stays in [0, 2pi).
    for (float native : {0.0005f, 0.02f, 3.0f, 6.26f, 6.2830f})
        for (float t = -0.5f; t <= 0.5f; t += 0.0007f)
            CHECK(inRange(wrap_0_2pi(unwrap_to_branch(t, native))));
}

static void test_clamp_pitch() {
    using aim_write::clamp_pitch;
    CHECK(near(clamp_pitch(0.2f, 1.45f), 0.2f, 1e-6f));
    CHECK(near(clamp_pitch(1.6f, 1.45f), 1.45f, 1e-6f));
    CHECK(near(clamp_pitch(-2.0f, 1.45f), -1.45f, 1e-6f));
}

static void test_arm_gate() {
    using aim_write::ArmGate;
    using Ev = ArmGate::Event;
    constexpr uint64_t A = 0x1000, B = 0x2000;   // two candidate record addresses
    {   // arms only after 3 CONSECUTIVE agreements, and reports what it proved
        ArmGate g;
        CHECK(g.step(true, A, 0, 0) == Ev::None && g.step(true, A, 0, 10) == Ev::None);
        CHECK(g.step(false, A, 0, 20) == Ev::None && !g.armed);               // streak broken
        CHECK(g.step(true, A, 0, 30) == Ev::None && g.step(true, A, 0, 40) == Ev::None);
        CHECK(g.step(true, A, 0, 50) == Ev::Armed && g.armed && g.record == A);
        CHECK(g.step(false, 0, 0, 60) == Ev::None && g.armed);                // agreement not consulted while armed
        CHECK(!g.wants_evidence(70));                                        // ...so the caller need not gather it
    }
    {   // a streak must be about ONE record: a changed candidate restarts the count on the new one
        ArmGate g;
        g.step(true, A, 0, 0); g.step(true, A, 0, 1);
        CHECK(g.step(true, B, 0, 2) == Ev::None && g.streak == 1 && g.record == B);
        CHECK(g.step(true, B, 0, 3) == Ev::None);
        CHECK(g.step(true, B, 0, 4) == Ev::Armed && g.record == B);
    }
    {   // "agreement" about no record at all never counts
        ArmGate g;
        for (int i = 0; i < 5; ++i) CHECK(g.step(true, 0, 0, i) == Ev::None);
        CHECK(!g.armed);
    }
    {   // a writer mismatch disarms at once, then backs off before re-validating
        ArmGate g;
        for (int i = 0; i < 3; ++i) g.step(true, A, 0, i);
        CHECK(g.armed);
        CHECK(g.step(true, 0, 1, 100) == Ev::Disarmed && !g.armed && g.disarms == 1);
        CHECK(!g.wants_evidence(200) && g.wants_evidence(5100));
        for (int i = 0; i < 5; ++i) CHECK(g.step(true, A, 1, 200 + i) == Ev::None);  // inside the backoff
        CHECK(!g.armed);
        g.step(true, A, 1, 5200); g.step(true, A, 1, 5210);
        CHECK(g.step(true, A, 1, 5220) == Ev::Armed);                         // re-proven after backoff
    }
    {   // mismatches counted while DISARMED are not the armed record's fault
        ArmGate g;
        g.step(true, A, 7, 0); g.step(true, A, 8, 1);
        CHECK(g.step(true, A, 9, 2) == Ev::Armed);
        CHECK(g.step(true, 0, 9, 3) == Ev::None && g.armed);
    }
    {   // three disarms -> give up for the session, and stay given up
        ArmGate g;
        uint64_t t = 0; unsigned mm = 0;
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 3; ++i) g.step(true, A, mm, t++);
            CHECK(g.armed);
            const Ev e = g.step(true, 0, ++mm, t);
            CHECK(e == (round < 2 ? Ev::Disarmed : Ev::GaveUp));
            t += 6000;
        }
        CHECK(g.given_up && !g.armed && !g.wants_evidence(t));
        for (int i = 0; i < 5; ++i) CHECK(g.step(true, A, mm, t + i) == Ev::None && !g.armed);
    }
    {   // release() drops the arm without counting a failure; re-arming re-proves from scratch
        ArmGate g;
        for (int i = 0; i < 3; ++i) g.step(true, A, 0, i);
        g.release();
        CHECK(!g.armed && g.disarms == 0 && g.record == 0);
        CHECK(g.step(true, A, 0, 10) == Ev::None && g.step(true, A, 0, 11) == Ev::None);
        CHECK(g.step(true, A, 0, 12) == Ev::Armed);
    }
    {   // release() alone never clears a give-up: that is the session's verdict
        ArmGate g;
        uint64_t t = 0; unsigned mm = 0;
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 3; ++i) g.step(true, A, mm, t++);
            g.step(true, 0, ++mm, t);
            t += 6000;
        }
        CHECK(g.given_up);
        g.release();
        CHECK(g.given_up && !g.wants_evidence(t));
    }
    {   // reset_session(): a new session (reinstall / teardown) clears the give-up, the disarm count and the
        // backoff, and the record must be proven from scratch (plan 2.E: no process-long give-up)
        ArmGate g;
        uint64_t t = 0; unsigned mm = 0;
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 3; ++i) g.step(true, A, mm, t++);
            g.step(true, 0, ++mm, t);
            t += 6000;
        }
        CHECK(g.given_up && g.disarms == 3);
        CHECK(g.reset_session());                                            // reports the cleared give-up
        CHECK(!g.given_up && !g.armed && g.disarms == 0 && g.record == 0 && g.retry_at_ms == 0);
        CHECK(g.wants_evidence(t));
        CHECK(g.step(true, B, mm, t) == Ev::None && g.step(true, B, mm, t + 1) == Ev::None);
        CHECK(g.step(true, B, mm, t + 2) == Ev::Armed && g.record == B);     // re-proven, on the new record
        CHECK(!g.reset_session() && !g.armed);                               // nothing to clear: reports false
    }
    {   // reset_session() mid-backoff: the new session re-validates at once, without the old backoff
        ArmGate g;
        for (int i = 0; i < 3; ++i) g.step(true, A, 0, i);
        CHECK(g.step(true, 0, 1, 100) == Ev::Disarmed && !g.wants_evidence(200));
        CHECK(!g.reset_session() && g.wants_evidence(200) && g.disarms == 0);
    }
}

// Reach's mapping into the core (src/common/reach_aim_basis.h): the frame the core cannot check itself.
static void test_reach_basis() {
    using namespace reach_aim_basis;
    CHECK(near(kCmPerWorldUnit, 304.8f, 1.0e-3f));   // a Reach world unit is 10 ft

    // A Reach yaw/pitch pair survives the trip into the core's basis and back, across the yaw wrap and
    // near vertical.
    for (float yaw : {-179.0f, -90.0f, 0.0f, 45.0f, 179.0f})
        for (float pitch : {-85.0f, -45.0f, 0.0f, 30.0f, 85.0f}) {
            float y = 0.0f, p = 0.0f;
            CHECK(angles_from_dir(DirFromGameAngles(yaw * kDegToRad, pitch * kDegToRad), y, p));
            CHECK(near(y, yaw, 1.0e-3f) && near(p, pitch, 1.0e-3f));
        }

    // End to end in Reach's own units: the engine eye E, the intent ray from the hand H along u, a hit
    // at d. The converged angles, read back as the radians desired_angles holds, must aim a ray from E
    // through the hit point H + u*d -- to well under a millimetre.
    for (float yaw : {-120.0f, 0.0f, 75.0f})
        for (float pitch : {-60.0f, 0.0f, 40.0f})
            for (float d : {0.5f, 3.0f, 20.0f}) {   // world units: 1.5 m, 9 m, 61 m
                const float E[3]{10.0f, -4.0f, 2.0f};
                const float H[3]{E[0] + 0.05f, E[1] - 0.06f, E[2] - 0.05f};   // a hand ~0.28 m off the eye
                const Vec3 u = DirFromGameAngles(yaw * kDegToRad, pitch * kDegToRad);
                float y = yaw, p = pitch;
                CHECK(converge_trace_reaim(y, p, DeltaCm(H, E), d * kCmPerWorldUnit, ConvergeRails{}));
                float yawRad = 0.0f, pitchRad = 0.0f;
                GameAnglesFromCore(Angles{y, p}, yawRad, pitchRad);
                const Vec3 a = DirFromGameAngles(yawRad, pitchRad);
                const float T[3]{H[0] + u.x * d, H[1] + u.y * d, H[2] + u.z * d};
                const float w[3]{T[0] - E[0], T[1] - E[1], T[2] - E[2]};
                const float c[3]{w[1] * a.z - w[2] * a.y, w[2] * a.x - w[0] * a.z, w[0] * a.y - w[1] * a.x};
                const float missCm = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) * kCmPerWorldUnit;
                CHECK(missCm < 0.1f);
                CHECK(w[0] * a.x + w[1] * a.y + w[2] * a.z > 0.0f);   // in front of the eye, not behind
            }
}

// Trace-and-reaim as a direction (aim_solve::converge_direction): the same policy as the angle form, in a
// title's own units, for titles whose aim record is a vector.
static void test_converge_direction() {
    const ConvergeRails r{};
    constexpr float kCm = 304.8f;   // a Blam world unit is 10 ft
    {   // agrees with the angle form on the same geometry (delta and range in world units)
        const Vec3 delta{0.02f, -0.05f, 0.03f};   // eye 20 cm-ish off the engine eye
        const float range = 2.5f;                  // 7.6 m
        for (float yaw : {-150.0f, 0.0f, 60.0f})
            for (float pitch : {-40.0f, 0.0f, 25.0f}) {
                const float cp = std::cos(pitch * 0.0174532925f), sp = std::sin(pitch * 0.0174532925f);
                const Vec3 u{cp * std::cos(yaw * 0.0174532925f), cp * std::sin(yaw * 0.0174532925f), sp};
                Vec3 a{};
                CHECK(converge_direction(delta, u, range, kCm, r, a));
                float y = yaw, p = pitch;
                CHECK(converge_trace_reaim(y, p, Vec3{delta.x * kCm, delta.y * kCm, delta.z * kCm}, range * kCm, r));
                float ay = 0, ap = 0;
                CHECK(angles_from_dir(a, ay, ap) && near(ay, y, 1e-3f) && near(ap, p, 1e-3f));
                // and the line from the engine eye along it passes through the traced point, to well under 1 mm
                const Vec3 T{delta.x + u.x * range, delta.y + u.y * range, delta.z + u.z * range};
                const Vec3 c{T.y * a.z - T.z * a.y, T.z * a.x - T.x * a.z, T.x * a.y - T.y * a.x};
                CHECK(std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z) * kCm < 0.1f);
            }
    }
    {   // an offset below the rail is already converged: the direction itself, normalized
        Vec3 a{};
        CHECK(converge_direction(Vec3{0.00005f, 0, 0}, Vec3{0, 2, 0}, 1.0f, kCm, r, a));
        CHECK(near(a.x, 0.0f, 1e-6f) && near(a.y, 1.0f, 1e-6f) && near(a.z, 0.0f, 1e-6f));
    }
    {   // cannot resolve: a degenerate range, a degenerate direction, a bad scale -- out untouched
        Vec3 a{9, 9, 9};
        CHECK(!converge_direction(Vec3{0.1f, 0, 0}, Vec3{1, 0, 0}, 0.00001f, kCm, r, a) && near(a.x, 9.0f, 1e-6f));
        CHECK(!converge_direction(Vec3{0.1f, 0, 0}, Vec3{0, 0, 0}, 1.0f, kCm, r, a) && near(a.x, 9.0f, 1e-6f));
        CHECK(!converge_direction(Vec3{0.1f, 0, 0}, Vec3{1, 0, 0}, 1.0f, 0.0f, r, a) && near(a.x, 9.0f, 1e-6f));
    }
    {   // a bend steeper than the record accepts is HELD AT THE RAIL and stays converged (a decline would
        // hand the write the unbent steer and switch convergence off)
        ConvergeRails steep{}; steep.max_pitch_deg = 1.45f * 57.29578f;
        Vec3 a{9, 9, 9};
        CHECK(converge_direction(Vec3{0.1f, 0, 0.9f}, Vec3{0, 0, 1}, 0.2f, kCm, steep, a));
        float ay = 0, ap = 0;
        CHECK(angles_from_dir(a, ay, ap) && near(ap, 83.08f, 0.01f) && near(ay, 0.0f, 1e-3f));
        CHECK(near(a.x * a.x + a.y * a.y + a.z * a.z, 1.0f, 1e-5f));
        // downward: held at -rail
        CHECK(converge_direction(Vec3{0.1f, 0, -0.9f}, Vec3{0, 0, -1}, 0.2f, kCm, steep, a));
        CHECK(angles_from_dir(a, ay, ap) && near(ap, -83.08f, 0.01f) && near(ay, 0.0f, 1e-3f));
        // both the bend and the hand exactly vertical: no heading at all, cannot resolve (out untouched)
        Vec3 b{9, 9, 9};
        CHECK(!converge_direction(Vec3{0, 0, 0.9f}, Vec3{0, 0, 1}, 0.2f, kCm, steep, b) && near(b.x, 9.0f, 1e-6f));
    }
    {   // continuous at the rail: a bend just under it and one just over it keep (nearly) the bent heading, even
        // with the hand's heading 30 deg away
        ConvergeRails steep{}; steep.max_pitch_deg = 1.45f * 57.29578f;
        const float D = 0.0174532925f;
        const Vec3 u{std::cos(80.0f * D), 0, std::sin(80.0f * D)};   // the hand: 80 deg up, heading 0
        // Pick delta so the bent aim is at 82.9 / 83.2 deg pitch, heading 30 deg: t = a_dir * L, delta = t - u.
        float yaws[2]{}, pitches[2]{};
        const float wantPitch[2]{82.9f, 83.2f};
        for (int i = 0; i < 2; ++i) {
            const float p = wantPitch[i] * D, h = 30.0f * D;
            const Vec3 t{std::cos(p) * std::cos(h), std::cos(p) * std::sin(h), std::sin(p)};
            const Vec3 delta{t.x - u.x, t.y - u.y, t.z - u.z};
            Vec3 a{};
            CHECK(converge_direction(delta, u, 1.0f, kCm, steep, a));
            CHECK(angles_from_dir(a, yaws[i], pitches[i]));
        }
        CHECK(near(pitches[0], 82.9f, 0.01f) && near(pitches[1], 83.08f, 0.01f));
        CHECK(near(yaws[0], 30.0f, 0.01f) && near(yaws[1], 30.0f, 1.5f));
    }
    {   // NEAR THE POLE the bent heading whips through every direction at the smallest change; the held aim
        // takes the hand's instead, so it stays put
        ConvergeRails steep{}; steep.max_pitch_deg = 1.45f * 57.29578f;
        const float D = 0.0174532925f;
        const Vec3 u{std::cos(80.0f * D), 0, std::sin(80.0f * D)};   // the hand: 80 deg up, heading 0
        float lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < 16; ++k) {
            // the bend's horizontal: 3 mm (in a 1 wu range) in sixteen directions -- within 0.2 deg of the pole
            const float h = k * 22.5f * D;
            const Vec3 delta{-u.x + 0.003f * std::cos(h), 0.003f * std::sin(h), 0.0f};
            Vec3 a{};
            CHECK(converge_direction(delta, u, 1.0f, kCm, steep, a));
            float ay = 0, ap = 0;
            CHECK(angles_from_dir(a, ay, ap) && near(ap, 83.08f, 0.01f));
            lo = ay < lo ? ay : lo;
            hi = ay > hi ? ay : hi;
        }
        CHECK(hi - lo < 0.5f);   // the bent aim's own heading swept all 360 deg
    }
    {   // THE BLEND'S SINGULARITY: a hand at 80 deg under a ceiling 0.5 m up, the engine eye
        // E ahead of the rendered head along the hand's heading (a roomscale lean). Sweeping E through ~13 cm sends the
        // bent aim over the pole and away from the hand; the stateless blend whips the output's heading through
        // 180 deg in 0.5 mm there (and through -52..-140 deg in 1 mm with a 2 mm lateral offset), a 14 deg swing of
        // the written aim. With a RailHold the sweep flips the heading AT MOST ONCE, and every other 0.1 mm step
        // moves the aim by a fraction of a degree.
        ConvergeRails steep{}; steep.max_pitch_deg = 1.45f * 57.29578f;
        const float D = 0.0174532925f, kM = 3.048f;   // metres per world unit
        const Vec3 u{std::cos(80.0f * D), 0, std::sin(80.0f * D)};
        const float range = (0.5f / std::sin(80.0f * D)) / kM;
        const auto stepDeg = [](const Vec3& a, const Vec3& b) {
            float d = a.x * b.x + a.y * b.y + a.z * b.z;
            d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
            return std::acos(d) * 57.29578f;
        };
        for (float lateral : {0.0f, 0.002f, -0.004f}) {
            if (lateral == 0.0f) {   // the stateless blend reproduces the whip (the reason for the hold)
                Vec3 prev{}; bool have = false; float worst = 0.0f;
                for (int i = 0; i <= 200; ++i) {
                    const float d = 0.120f + i * 0.0001f;
                    Vec3 a{};
                    CHECK(converge_direction(Vec3{-d / kM, -lateral / kM, 0}, u, range, kCm, steep, a));
                    if (have) { const float s = stepDeg(prev, a); worst = s > worst ? s : worst; }
                    prev = a; have = true;
                }
                CHECK(worst > 10.0f);
            }
            RailHold hold;
            Vec3 prev{}; bool have = false;
            int jumps = 0; float worstSmall = 0.0f;
            // d from 3.1 cm (just past min_divergence, whose own 3 deg step is not this clamp's) to 16 cm (under the
            // rail again), 0.1 mm steps
            for (int i = 0; i <= 1290; ++i) {
                const float d = 0.031f + i * 0.0001f;
                Vec3 a{};
                CHECK(converge_direction(Vec3{-d / kM, -lateral / kM, 0}, u, range, kCm, steep, a, &hold));
                if (have) {
                    const float s = stepDeg(prev, a);
                    if (s > 2.0f) ++jumps; else worstSmall = s > worstSmall ? s : worstSmall;
                }
                prev = a; have = true;
            }
            CHECK(jumps <= 1);
            CHECK(worstSmall < 0.25f);
            // JITTER AT THE SINGULARITY: the eye shaking +-1 mm about the old flip point moves nothing.
            hold.reset();
            for (int i = 0; i <= 995; ++i) {   // approach from the hand's side, as a lean would
                const float d = 0.031f + i * 0.0001f;
                Vec3 a{};
                converge_direction(Vec3{-d / kM, -lateral / kM, 0}, u, range, kCm, steep, a, &hold);
                prev = a;
            }
            float worstJitter = 0.0f;
            for (int i = 0; i < 400; ++i) {
                const float d = 0.1305f + 0.001f * std::sin(i * 0.7f) + 0.0003f * ((i * 7919) % 5 - 2);
                Vec3 a{};
                CHECK(converge_direction(Vec3{-d / kM, -lateral / kM, 0}, u, range, kCm, steep, a, &hold));
                const float s = stepDeg(prev, a);
                worstJitter = s > worstJitter ? s : worstJitter;
                prev = a;
            }
            CHECK(worstJitter < 0.25f);
        }
        {   // the hold is forgotten by any call that does not clamp, and by a decline
            RailHold hold;
            Vec3 a{};
            CHECK(converge_direction(Vec3{-0.13f / kM, 0, 0}, u, range, kCm, steep, a, &hold) && hold.active);
            CHECK(converge_direction(Vec3{0.1f, 0, 0}, Vec3{1, 0, 0}, 2.5f, kCm, steep, a, &hold) && !hold.active);
            CHECK(converge_direction(Vec3{-0.13f / kM, 0, 0}, u, range, kCm, steep, a, &hold) && hold.active);
            CHECK(!converge_direction(Vec3{0.1f, 0, 0}, Vec3{1, 0, 0}, 0.00001f, kCm, steep, a, &hold) &&
                  !hold.active && !hold.holding);
        }
    }
}

// A chase camera's crosshair line as the shot origin (aim_solve::line_point_nearest), and why the point
// nearest the pivot is the one to converge from: the camera orbits as the aim turns.
static void test_chase_camera_line() {
    {   // the point of the line nearest the pivot; a pivot behind the camera gives the camera
        Vec3 o{};
        CHECK(line_point_nearest(Vec3{-500, 0, 200}, Vec3{2, 0, 0}, Vec3{0, 30, 150}, o));
        CHECK(near(o.x, 0.0f, 1e-3f) && near(o.y, 0.0f, 1e-3f) && near(o.z, 200.0f, 1e-3f));
        CHECK(line_point_nearest(Vec3{-500, 0, 200}, Vec3{1, 0, 0}, Vec3{-900, 0, 150}, o));
        CHECK(near(o.x, -500.0f, 1e-3f) && near(o.z, 200.0f, 1e-3f));
        o = Vec3{7, 7, 7};
        CHECK(!line_point_nearest(Vec3{}, Vec3{0, 0, 0}, Vec3{1, 1, 1}, o) && near(o.x, 7.0f, 1e-6f));   // untouched
    }
    // An orbiting chase camera: 500 cm behind a pivot along the aim, 120 cm above it. The eye rendering
    // the intent ray sits in the seat, 60 cm left of and 40 cm above the pivot. Iterate the solve the way
    // the game does it (each frame re-solves from the camera the last aim produced) and check that the
    // camera's line ends up through the traced point -- including a target NEARER than the camera is
    // behind the pivot.
    const Vec3 pivot{1000, 2000, 300};
    const Vec3 eye{pivot.x, pivot.y + 60, pivot.z + 40};
    const auto cameraFor = [&](float yawDeg, float pitchDeg, Vec3& cam, Vec3& fwd) {
        const float cy = std::cos(yawDeg * 0.0174533f), sy = std::sin(yawDeg * 0.0174533f);
        const float cp = std::cos(pitchDeg * 0.0174533f), sp = std::sin(pitchDeg * 0.0174533f);
        fwd = Vec3{cy * cp, sy * cp, sp};
        cam = Vec3{pivot.x - fwd.x * 500, pivot.y - fwd.y * 500, pivot.z - fwd.z * 500 + 120};
    };
    const auto missCm = [&](float yawDeg, float pitchDeg, const Vec3& target) {
        Vec3 cam{}, f{};
        cameraFor(yawDeg, pitchDeg, cam, f);
        const Vec3 w{target.x - cam.x, target.y - cam.y, target.z - cam.z};
        const Vec3 c{w.y * f.z - w.z * f.y, w.z * f.x - w.x * f.z, w.x * f.y - w.y * f.x};
        return std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    };
    for (float range : {300.0f, 800.0f, 4000.0f}) {    // 3 m (nearer than the camera is back), 8 m, 40 m
        const Vec3 u{std::cos(0.3f), std::sin(0.3f), 0.0f};   // the hand points 17 deg left, level
        const Vec3 target{eye.x + u.x * range, eye.y + u.y * range, eye.z + u.z * range};
        float y = 0.0f, p = 0.0f;
        for (int frame = 0; frame < 40; ++frame) {
            Vec3 cam{}, f{}, anchor{};
            cameraFor(y, p, cam, f);
            CHECK(line_point_nearest(cam, f, pivot, anchor));
            float ny = std::atan2(u.y, u.x) * 57.29578f, np = std::asin(u.z) * 57.29578f;
            CHECK(converge_trace_reaim(ny, np, Vec3{eye.x - anchor.x, eye.y - anchor.y, eye.z - anchor.z},
                                       range, ConvergeRails{}));
            y = ny; p = np;
        }
        CHECK(missCm(y, p, target) < 1.0f);
    }
}

// Seat limits: the state write keeps inside the clamp the game applies (aim_write::LimitLatch).
static void test_limit_latch() {
    using aim_write::LimitLatch;
    constexpr float kD = 0.0174533f;
    // A fake game: desired_angles clamped to pitch >= -20 deg (the Warthog chaingun), stick neutral.
    const auto game = [&](const float wrote[2], float native[2]) {
        native[0] = wrote[0];
        native[1] = wrote[1] < -20.0f * kD ? -20.0f * kD : wrote[1];
    };
    {   // inside the limit: the target, untouched, and never limited
        LimitLatch l; float pair[2]{0.1f, -0.1f}, out[2]{};
        for (int t = 0; t < 10; ++t) {
            const float target[2]{0.1f + 0.01f * t, -0.1f};
            float native[2]; game(pair, native);
            CHECK(!l.apply(native, target, out) && near(out[0], target[0], 1e-6f) && near(out[1], target[1], 1e-6f));
            pair[0] = out[0]; pair[1] = out[1]; l.wrote(pair);
        }
    }
    {   // past the pitch stop: the first write overshoots, then the latch holds within one probe of the stop
        LimitLatch l; float pair[2]{0.0f, -0.1f}, out[2]{};
        const float target[2]{0.2f, -35.0f * kD};
        bool limited = false;
        for (int t = 0; t < 20; ++t) {
            float native[2]; game(pair, native);
            limited = l.apply(native, target, out);
            pair[0] = out[0]; pair[1] = out[1]; l.wrote(pair);
        }
        CHECK(limited && near(out[0], 0.2f, 1e-6f));                                // yaw is free
        CHECK(out[1] < -20.0f * kD && out[1] >= -20.0f * kD - l.probe - 1e-6f);    // one probe past the stop
        // the hand comes back above the stop: released at once, the target itself
        const float back[2]{0.2f, -10.0f * kD};
        float native[2]; game(pair, native);
        CHECK(!l.apply(native, back, out) && near(out[1], back[1], 1e-6f) && !l.latched[1]);
    }
    {   // a snap that is not a limit (the game re-set the record once): the probe doubles and catches up
        LimitLatch l; float out[2]{};
        const float wrote[2]{0.0f, 0.0f};
        l.wrote(wrote);
        const float snapped[2]{0.0f, 0.3f};                              // the game moved it by 17 deg once
        const float target[2]{0.0f, 0.0f};
        CHECK(l.apply(snapped, target, out));                             // looks like a minimum at 0.3...
        int ticks = 1;
        for (float pair[2]{out[0], out[1]}; ticks < 20 && l.latched[1]; ++ticks) {
            l.wrote(pair);
            l.apply(pair, target, out);                                   // ...but every probe holds
            pair[0] = out[0]; pair[1] = out[1];
        }
        CHECK(!l.latched[1] && near(out[1], 0.0f, 1e-6f) && ticks < 12);  // back on the target within a few ticks
    }
    {   // yaw wraps: a limit near +pi and a target just past it across the wrap
        LimitLatch l; float out[2]{};
        const float wrote[2]{-3.10f, 0.0f};                               // written just past +pi (wrapped)
        l.wrote(wrote);
        const float native[2]{3.10f, 0.0f};                               // the game clamped it back to 3.10
        const float target[2]{-3.12f, 0.0f};                              // still past it, on the far side
        CHECK(l.apply(native, target, out) && l.latched[0] && l.side[0] > 0.0f);
        CHECK(std::fabs(std::remainder(out[0] - 3.10f, aim_write::kTwoPi)) <= l.probe + 1e-5f);
    }
    {   // reset forgets: the next tick is judged afresh
        LimitLatch l; float out[2]{};
        const float wrote[2]{0.0f, 0.0f};
        l.wrote(wrote);
        l.reset();
        const float native[2]{0.0f, 0.5f}, target[2]{0.0f, 0.0f};
        CHECK(!l.apply(native, target, out) && near(out[1], 0.0f, 1e-6f));
    }
    {   // per axis: a disabled pitch gives back the game's value and never reads the game's own pitch
        // moves (a driver's autolevel) as a clamp; yaw keeps latching as before
        LimitLatch l; float out[2]{};
        const bool yawOnly[2]{true, false};
        const float wrote[2]{0.0f, 0.0f};
        l.wrote(wrote);
        for (int t = 0; t < 5; ++t) {
            const float native[2]{0.0f, 0.05f * (t + 1)};                 // the game levels pitch every tick
            const float target[2]{0.0f, -0.4f};
            CHECK(!l.apply(native, target, out, yawOnly));
            CHECK(near(out[1], native[1], 1e-6f) && !l.latched[1] && !l.have_last[1]);
            const float pair[2]{out[0], out[1]};
            l.wrote(pair);
            CHECK(l.have_last[0] && !l.have_last[1]);                     // wrote() skipped pitch
        }
        // pitch switched back on: judged afresh (no stale last), so the game's move is not a limit
        const bool both[2]{true, true};
        const float native[2]{0.0f, 0.9f}, target[2]{0.0f, -0.4f};
        CHECK(!l.apply(native, target, out, both) && near(out[1], -0.4f, 1e-6f) && !l.latched[1]);
    }
    {   // per axis, the shipping path: a mask with pitch enabled still reads and holds the pitch stop
        LimitLatch l; float pair[2]{0.0f, -0.1f}, out[2]{};
        const bool pitchOnly[2]{false, true};
        const bool recorded[2]{false, true};
        const float target[2]{0.2f, -35.0f * kD};
        bool limited = false;
        for (int t = 0; t < 20; ++t) {
            float native[2]; game(pair, native);
            limited = l.apply(native, target, out, pitchOnly);
            pair[0] = native[0]; pair[1] = out[1]; l.wrote(pair, recorded);
        }
        CHECK(limited && out[1] < -20.0f * kD && out[1] >= -20.0f * kD - l.probe - 1e-6f);
    }
    {   // wrote(axes): a write made through the prime (apply skipped the axis) is recorded, so the next
        // tick reads a limit off it
        LimitLatch l; float out[2]{};
        const bool off[2]{false, false}, pitchOn[2]{false, true}, recorded[2]{false, true};
        const float native0[2]{0.0f, 0.0f}, target[2]{0.0f, -0.5f};
        l.apply(native0, target, out, off);
        const float written[2]{0.0f, -0.5f};
        l.wrote(written, recorded);
        CHECK(l.have_last[1] && !l.have_last[0]);
        const float clamped[2]{0.0f, -0.3f};                              // the game clamped the primed write
        CHECK(l.apply(clamped, target, out, pitchOn) && l.latched[1]);
    }
    {   // per axis: a latched axis that is switched off forgets the latch
        LimitLatch l; float pair[2]{0.0f, -0.1f}, out[2]{};
        const float target[2]{0.2f, -35.0f * kD};
        for (int t = 0; t < 5; ++t) {
            float native[2]; game(pair, native);
            l.apply(native, target, out);
            pair[0] = out[0]; pair[1] = out[1]; l.wrote(pair);
        }
        CHECK(l.latched[1]);
        const bool yawOnly[2]{true, false};
        float native[2]; game(pair, native);
        CHECK(!l.apply(native, target, out, yawOnly) && !l.latched[1] && near(out[1], native[1], 1e-6f));
    }
}

// Shots from the muzzle (aim_solve::muzzle_shot_direction): normalize(P - M), up re-orthogonalized, and
// every refusal names its reason with the outputs untouched. No distance-from-the-eye limit.
static void test_muzzle_shot_direction() {
    const float kCm = 304.8f;                       // a Blam world unit is 10 ft, 304.8 cm
    const MuzzleShotRails r{};
    const Vec3 dir{1, 0, 0};
    const Vec3 muzzle{0.6f / 3.048f, -0.25f / 3.048f, -0.30f / 3.048f};   // 60 cm ahead, right and low
    const Vec3 point{10.0f, 0, 0};                                        // ~30 m down the intent ray
    {
        Vec3 out{9, 9, 9}, up{0, 0, 1};
        CHECK(muzzle_shot_direction(muzzle, muzzle, point, dir, kCm, r, out, up) == MuzzleShot::Aimed);
        const float lx = point.x - muzzle.x, ly = point.y - muzzle.y, lz = point.z - muzzle.z;
        const float l = std::sqrt(lx * lx + ly * ly + lz * lz);
        CHECK(near(out.x, lx / l, 1e-5f) && near(out.y, ly / l, 1e-5f) && near(out.z, lz / l, 1e-5f));
        // The shot from the muzzle along out passes through P.
        const float t = l;
        CHECK(near(muzzle.x + out.x * t, point.x, 1e-4f) && near(muzzle.y + out.y * t, point.y, 1e-4f) &&
              near(muzzle.z + out.z * t, point.z, 1e-4f));
        CHECK(near(up.x * out.x + up.y * out.y + up.z * out.z, 0.0f, 1e-5f));
        CHECK(near(std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z), 1.0f, 1e-5f) && up.z > 0.9f);
    }
    {   // A clamp of 3 cm passes; 6 cm (the gun through a wall the eye sees past) refuses, untouched.
        Vec3 out{9, 9, 9}, up{0, 0, 1};
        const Vec3 before3{muzzle.x + 0.03f / 3.048f, muzzle.y, muzzle.z};
        CHECK(muzzle_shot_direction(before3, muzzle, point, dir, kCm, r, out, up) == MuzzleShot::Aimed);
        out = Vec3{9, 9, 9}; up = Vec3{0, 0, 1};
        const Vec3 before6{muzzle.x + 0.06f / 3.048f, muzzle.y, muzzle.z};
        CHECK(muzzle_shot_direction(before6, muzzle, point, dir, kCm, r, out, up) == MuzzleShot::Obstructed);
        CHECK(near(out.x, 9.0f, 1e-6f) && near(up.z, 1.0f, 1e-6f));
    }
    {   // NO DISTANCE-FROM-THE-EYE LIMIT: a muzzle 2 m (or 10 m) from the eye is aimed through P. Whose gun it
        // is is proven by the caller's weapon_muzzle receipt, not by where it sits.
        for (const float metres : {2.0f, 10.0f}) {
            Vec3 out{9, 9, 9}, up{0, 0, 1};
            const Vec3 far{0.0f, metres / 3.048f, 0.0f};
            CHECK(muzzle_shot_direction(far, far, point, dir, kCm, r, out, up) == MuzzleShot::Aimed);
            const float lx = point.x - far.x, ly = point.y - far.y, lz = point.z - far.z;
            const float l = std::sqrt(lx * lx + ly * ly + lz * lz);
            CHECK(near(out.x, lx / l, 1e-5f) && near(out.y, ly / l, 1e-5f));
        }
    }
    {   // A converged point 5 cm ahead of the muzzle (a wall at the gun) refuses; 20 cm passes.
        Vec3 out{9, 9, 9}, up{0, 0, 1};
        const Vec3 close{muzzle.x + 0.05f / 3.048f, muzzle.y, muzzle.z};
        CHECK(muzzle_shot_direction(muzzle, muzzle, close, dir, kCm, r, out, up) == MuzzleShot::PointNotAhead);
        const Vec3 ok{muzzle.x + 0.20f / 3.048f, muzzle.y, muzzle.z};
        CHECK(muzzle_shot_direction(muzzle, muzzle, ok, dir, kCm, r, out, up) == MuzzleShot::Aimed &&
              near(out.x, 1.0f, 1e-5f));
    }
    {   // A point behind the gun refuses.
        Vec3 out{9, 9, 9}, up{0, 0, 1};
        const Vec3 behind{-1.0f, 0, 0};
        CHECK(muzzle_shot_direction(muzzle, muzzle, behind, dir, kCm, r, out, up) == MuzzleShot::PointNotAhead);
    }
    {   // An up along the shot falls back to the up nearest world +Z; non-finite input refuses.
        Vec3 out{}, up{1, 0, 0};
        CHECK(muzzle_shot_direction(Vec3{0, 0, 0}, Vec3{0, 0, 0}, point, dir, kCm, r, out, up) == MuzzleShot::Aimed);
        CHECK(near(out.x, 1.0f, 1e-6f) && near(up.z, 1.0f, 1e-5f));
        Vec3 out2{9, 9, 9}, up2{0, 0, 1};
        const Vec3 nan{std::nanf(""), 0, 0};
        CHECK(muzzle_shot_direction(muzzle, muzzle, nan, dir, kCm, r, out2, up2) == MuzzleShot::NonFinite &&
              near(out2.x, 9.0f, 1e-6f));
        CHECK(muzzle_shot_direction(muzzle, muzzle, point, dir, 0.0f, r, out2, up2) == MuzzleShot::NonFinite);
    }
    {   // Every reason has a log name.
        bool named = true;
        for (int w = 0; w < static_cast<int>(MuzzleShot::Count); ++w)
            named = named && std::string(MuzzleShotName(static_cast<MuzzleShot>(w))) != "?";
        CHECK(named);
    }
}

int main() {
    test_selectors();
    test_angles_from_dir();
    test_converge();
    test_traced_reticle();
    test_orchestrator();
    test_unwrap_to_branch();
    test_wrap_0_2pi();
    test_clamp_pitch();
    test_arm_gate();
    test_reach_basis();
    test_converge_direction();
    test_chase_camera_line();
    test_limit_latch();
    test_muzzle_shot_direction();
    if (g_fail == 0) std::printf("aim_solve_tests: OK\n");
    return g_fail == 0 ? 0 : 1;
}
