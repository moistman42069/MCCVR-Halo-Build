// Reach's aim provider (see reach_aim_provider.h). The engine-free core picks the actuation rung from
// caps(); this file supplies Reach's facts and actuates the rung it picks:
//   WriteState -- direct drive (reach_direct_drive=3): the converged setpoint is published to the
//                 sim-thread hook on haloreach+0x6375C, which writes player_control.desired_angles
//                 (upstream of the derived aim vector and of replication); the aim stick is held
//                 neutral so there is exactly one driver.
//   StickLoop  -- the shipped closed loop, Game_ComputeAimStick, unchanged.
// Convergence (reach_sight_trace.inl): TraceReaim is THE aim path -- the native shot stays at Reach's
// own origin and the setpoint is bent through the point the intent ray hits -- whenever the trace has
// a fresh result: a hit, or a miss (the far end of the 1 km trace). OriginRelocation (the firing site
// moves the shot onto the hand ray) is only the worst-case fallback for when it cannot resolve.
// Engine-bound (reads g_config and game.cpp's Reach state); the pure core it feeds is
// src/common/aim_solve_logic.h + aim_provider.h, and the write guards the hook applies are
// src/common/aim_write_logic.h.

#include "reach_aim_provider.h"

#include "../common/aim_provider.h"
#include "../common/reach_aim_basis.h"   // Reach <-> core units and angles (unit-tested)
#include "game.h"   // Game_ComputeAimStick (StickLoop) + the Game_ReachDirectDrive* WriteState seam
#include "sight_layer.h"   // the dev converge-off witness (Sight_NoteConvergeState)
#include "../common/config.h"
#include "../common/log.h"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace {
using namespace aim_solve;
using reach_aim_basis::kCmPerWorldUnit;   // 304.8: a Reach world unit is 10 ft
// The rule: the fallback (OriginRelocation) is only for when this path cannot resolve at all.
// Game_ReachSightSample hands over only a FRESH trace result (a hit, or a miss's far end); with none,
// the shot genuinely cannot be resolved.
// The direct-drive hook clamps pitch to 1.45 rad (aim_write::clamp_pitch in ReachPcApplyForUser), so a
// converged aim steeper than that could not be actuated exactly: the one physical limit on the solve.
constexpr float kWriteStatePitchLimitDeg = 1.45f * 57.29578f;

const char* rung_name(Actuation how) {
    switch (how) {
    case Actuation::WriteState: return "WriteState";
    case Actuation::StickLoop:  return "StickLoop";
    default:                    return "None";
    }
}

class ReachAimProvider final : public IAimProvider {
public:
    const char* title() const override { return "reach"; }

    // The core reads caps() more than once per poll; every read sees this poll's snapshot, so a
    // cross-thread flip mid-poll cannot pair a trace-and-reaim intent with a relocation actuation.
    AimCaps caps() const override { return snapshot; }

    AimCaps compute_caps() const {
        AimCaps c;
        // Rung 1 is player_control.desired_angles, the accumulator's own record: after stick
        // conditioning, before the aim vector is derived, and persistent (one write steers the sim).
        // Offered only with direct drive on (reach_direct_drive=3), with the hook proven by the arm-gate AND
        // demonstrably running (its heartbeat -- a hook that stopped would otherwise leave the stick
        // neutral with nothing writing), on foot or in a seat the hand aims (Game_ReachDirectDriveAimWhy:
        // not a seat whose stick is steering, as reach_vehicle_aim allows, and whose frame -- the
        // vehicle-tilt rebase -- reads: an unread frame writes nothing, so the seat goes to the stick loop
        // rather than freeze with the stick neutral). Cinematics keep the stick
        // loop. Co-op host-follow is UNVERIFIED, so actuate() never claims a replicated write.
        // Never in the free-camera frame (no player unit): there is no record to write, and the hand steers the
        // death / free camera through the stick loop (aim_no_unit_logic.h).
        c.can_write_state       = g_config.reach_direct_drive == 3 && Game_ReachDirectDriveReady() &&
                                  Game_ReachDirectDriveAimWritable() && !Game_AimFreeCamera();
        c.has_stick_loop        = true;   // Reach steers via Game_ComputeAimStick, like Halo 3
        // TRACE-AND-REAIM (reach_sight_trace.inl): whenever the trace has a fresh result (hit or miss)
        // and direct drive can actuate the bent aim (a stick loop cannot hold an aim that bends with
        // range).
        c.has_sightline_trace   = trace_ok && c.can_write_state;
        c.has_origin_relocation = true;   // the worst-case fallbacks: the hand shot ray on foot, and in a
                                          // seat R-V27 (passenger origin) and R-V22 (barrel direction)
        c.has_fixed_range_reaim = true;   // ComputeAimStick steers through the crosshair-distance point
        c.has_barrel_marker     = Sight_GunBarrelAim() != 0;
        c.camera_follows_aim    = true;   // Reach writes the aim, so the view follows the hand
        return c;
    }

    // With trace-and-reaim the intent is the RENDERED hand/reticle ray itself (origin and direction in
    // world units -> cm): the core bends it through the traced point. Otherwise it is the aim
    // Game_ComputeAimStick recorded during this poll (the mount-calibrated controller ray steered
    // through the crosshair-distance point, already in Reach's world basis -- which IS the core's
    // basis: Blam is Z-up, yaw about +Z from +X). Either way the servo must have recorded an aim this
    // poll -- that is the gate for every aim precondition (VR aim off, pause, tracking).
    bool intent_ray(Ray* out) override {
        if (!out) return false;
        float yaw = 0.0f, pitch = 0.0f;
        if (!Game_ReachTakeDesiredAim(yaw, pitch)) return false;
        if (snapshot.has_sightline_trace) {
            out->origin = reach_aim_basis::CmFromWorld(sight.origin);
            out->dir    = Vec3{sight.dir[0], sight.dir[1], sight.dir[2]};
            return true;
        }
        out->origin = Vec3{};
        out->dir    = reach_aim_basis::DirFromGameAngles(yaw, pitch);
        return true;
    }

    // The range to converge on, in cm: the hit, or on a miss the trace's far end (fresh results only).
    bool trace_impact_cm(const Ray&, float* out_cm) override {
        if (!out_cm || !snapshot.has_sightline_trace) return false;
        *out_cm = sight.rangeWu * kCmPerWorldUnit;
        return true;
    }
    // Ray origin minus the engine eye (where Reach aims from: on foot the character's eye, in a seat the
    // point of its own camera's line nearest the seat), world basis, cm.
    bool origin_delta(Vec3* out) override {
        if (!out || !snapshot.has_sightline_trace) return false;
        *out = reach_aim_basis::DeltaCm(sight.origin, sight.eye);
        last_delta_cm = std::sqrt(out->x * out->x + out->y * out->y + out->z * out->z);
        return true;
    }

    // WriteState publishes the setpoint for the hook's next sim tick, together with whether the core
    // actually bent it. The HOOK records that bit when it writes desired_angles, and the firing site
    // reads it from there, so the stand-down follows the angles really in effect, on the sim thread.
    // StickLoop was already computed by the servo this poll (ProduceStick hands its stick to the input
    // hook), so it reports whether the servo produced one.
    ActuateResult actuate(Actuation how, const Angles& converged) override {
        last_how = how;
        last_angles = converged;
        last_converged = false;
        if (how == Actuation::WriteState) {
            last_converged = state && state->converged && snapshot.has_sightline_trace;
            float yawRad = 0.0f, pitchRad = 0.0f;
            reach_aim_basis::GameAnglesFromCore(converged, yawRad, pitchRad);
            Game_ReachDirectDrivePublish(yawRad, pitchRad, last_converged, seated);
            if (last_converged)
                Game_ReachNoteConvergedSample(sight, state->range_cm / kCmPerWorldUnit);
            return { true, false };   // not a replicated write until co-op host-follow is verified
        }
        if (how == Actuation::StickLoop) return { servo_ok, false };
        return {};
    }

    void apply_engine_convergence(Converge cv, const Ray&) override { last_conv = cv; }

    AimCaps   snapshot{};
    bool      servo_ok    = false;               // set by ProduceStick before each solve
    bool      sight_ok    = false;               // ditto: a trace-and-reaim sample this poll
    bool      trace_ok    = false;               // ...carrying a fresh trace result (hit or miss)
    bool      seated      = false;               // this poll's aim is a seat's (the hook guards its limits)
    ReachSightSample sight{};
    const SolveState* state = nullptr;
    Actuation last_how    = Actuation::None;
    Converge  last_conv   = Converge::DirectionOnly;
    Angles    last_angles{};
    float     last_delta_cm = 0.0f;
    bool      last_converged = false;
};

// Status for the cold worker (never logged from the input hook: file I/O and the log lock stay out of
// the game's XInput path).
struct ProviderStatus {
    std::atomic<uint32_t> polls{0}, writes{0}, converged{0}, freshHits{0}, freshMisses{0};
    std::atomic<int> rung{0}, converge{0};
    std::atomic<float> yawDeg{0.0f}, pitchDeg{0.0f}, rangeM{0.0f}, deltaCm{0.0f};
} g_status;

} // namespace

bool ReachAimProvider_ProduceStick(float& outRx, float& outRy) {
    static ReachAimProvider provider;
    static SolveState state;
    static bool traceReaimLastPoll = false;
    static LARGE_INTEGER lastPoll{};

    // 1. The stick servo runs first, every poll: it records this poll's desired aim (the intent the core
    //    takes) and computes the StickLoop rung's stick, so falling back costs nothing and is immediate.
    float rx = 0.0f, ry = 0.0f;
    provider.servo_ok = Game_ComputeAimStick(rx, ry);
    provider.last_how = Actuation::None;
    provider.state = &state;

    // 2. This poll's trace-and-reaim sample and the caps snapshot the whole solve will see.
    provider.sight_ok = Game_ReachSightSample(provider.sight);   // true only with a fresh trace result
    provider.trace_ok = provider.sight_ok && provider.sight.traceSampleMs != 0;
    provider.seated = Game_ReachDirectDriveSeated();
    provider.snapshot = provider.compute_caps();
    // A range smoothed on foot describes nothing in a seat, and the other way round.
    static bool seatedLastPoll = false;
    if (provider.seated != seatedLastPoll)
    {
        state.range_cm = 0.0f;
        state.inv_range_per_cm = 0.0f;
        seatedLastPoll = provider.seated;
    }
    // A range smoothed before a gap describes some earlier view: start it afresh from the first hit.
    if (!provider.snapshot.has_sightline_trace || !traceReaimLastPoll)
    {
        state.range_cm = 0.0f;
        state.inv_range_per_cm = 0.0f;
    }
    traceReaimLastPoll = provider.snapshot.has_sightline_trace;

    // 3. The core picks the rung, converges, and actuates through the provider, with the measured poll
    //    interval (the input hook runs at the game's XInput rate, not a fixed one). Range tau matches
    //    CampE's convergence smoothing.
    LARGE_INTEGER qpc{}, freq{};
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    float dt = 1.0f / 90.0f;
    if (lastPoll.QuadPart && freq.QuadPart && qpc.QuadPart > lastPoll.QuadPart)
        dt = std::fmax(0.001f, std::fmin(0.1f, float(double(qpc.QuadPart - lastPoll.QuadPart) / double(freq.QuadPart))));
    lastPoll = qpc;
    ConvergeRails rails{};
    rails.max_pitch_deg = kWriteStatePitchLimitDeg;
    const ActuateResult res = solve_and_actuate(provider, state, rails, dt, 120.0f);
    const bool wroteState = res.applied && provider.last_how == Actuation::WriteState;
    if (!wroteState)
        Game_ReachDirectDriveRelease();   // no WriteState this poll: the hook must stop writing now

    g_status.polls.fetch_add(1, std::memory_order_relaxed);
    if (wroteState) g_status.writes.fetch_add(1, std::memory_order_relaxed);
    if (wroteState && provider.last_converged) g_status.converged.fetch_add(1, std::memory_order_relaxed);
    if (provider.trace_ok)
        (provider.sight.hit ? g_status.freshHits : g_status.freshMisses).fetch_add(1, std::memory_order_relaxed);
    g_status.rung.store(static_cast<int>(provider.last_how), std::memory_order_relaxed);
    g_status.converge.store(static_cast<int>(select_converge(provider.snapshot)), std::memory_order_relaxed);
    g_status.yawDeg.store(provider.last_angles.yaw_deg, std::memory_order_relaxed);
    g_status.pitchDeg.store(provider.last_angles.pitch_deg, std::memory_order_relaxed);
    g_status.rangeM.store(state.range_cm * 0.01f, std::memory_order_relaxed);
    g_status.deltaCm.store(provider.last_delta_cm, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // THE CONVERGE-OFF WITNESS (sight_layer.h): whether this poll actuated a converged setpoint and, if not, the
    // first of our code's reasons (converge::ReachConvergeOffReason). A seat limit is not among them: the hook's
    // latch never withdraws the claim (converge::ReachStampUpdate). Only polls the aim path ran are counted.
    if (provider.servo_ok)
    {
        converge::ReachConvergePoll p;
        p.live = wroteState && provider.last_converged;
        p.aimOn = g_config.reach_direct_drive == 3 && g_config.converged_aim;
        if (!p.live && p.aimOn)
        {
            p.driveReady = Game_ReachDirectDriveReady();
            p.aimWhy = Game_ReachDirectDriveAimWhy();
            Game_ReachSightHealth(p.flagsResolved, p.traceFaulted);
            p.sightSample = provider.trace_ok;
            p.rangeKnown = state.range_cm > 0.0f;
        }
        converge::ConvergeOff why = converge::ConvergeOff::None;
        if (converge::ReachConvergeOffReason(p, why))
            Sight_NoteConvergeState(GameTitle::HaloReach, p.live, why);
    }
#endif

    if (wroteState) {   // one driver: the state write owns aim, so the stick says nothing
        outRx = 0.0f;
        outRy = 0.0f;
        return true;
    }
    if (provider.servo_ok) {
        outRx = rx;
        outRy = ry;
    }
    return provider.servo_ok;
}

void ReachAimProvider_Report() {
    static uint64_t last_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now - last_ms < 2000) return;
    last_ms = now;
    const uint32_t polls = g_status.polls.exchange(0, std::memory_order_relaxed);
    if (!polls) return;   // the provider is not running (another title, a menu, or aim_provider and direct drive off)
    const int rung = g_status.rung.load(std::memory_order_relaxed);
    // ready = hook installed + arm-gate armed + hook heartbeat fresh (Game_ReachDirectDriveReady).
    // why = the failing aim-write conditions (Game_ReachDirectDriveAimWhy's bits; 0 = the write may drive:
    // on foot, or in a hand-aimed seat).
    // converge: 0 trace-and-reaim, 1 origin relocation, 2 fixed range, 3 direction only.
    LOG("AIMPROVIDER reach: rung=%s (WriteState %u/%u polls, converged %u; trace hits %u, misses %u "
        "[far end]) mode=%d ready=%d "
        "onfoot=%d seated=%d why=0x%03X converge=%d range=%.2f m eye->ray %.1f cm yaw=%.1f pitch=%.1f",
        rung_name(static_cast<Actuation>(rung)), g_status.writes.exchange(0, std::memory_order_relaxed), polls,
        g_status.converged.exchange(0, std::memory_order_relaxed),
        g_status.freshHits.exchange(0, std::memory_order_relaxed),
        g_status.freshMisses.exchange(0, std::memory_order_relaxed), g_config.reach_direct_drive,
        Game_ReachDirectDriveReady() ? 1 : 0, Game_ReachDirectDriveOnFoot() ? 1 : 0,
        Game_ReachDirectDriveSeated() ? 1 : 0, static_cast<unsigned>(Game_ReachDirectDriveAimWhy()),
        g_status.converge.load(std::memory_order_relaxed), g_status.rangeM.load(std::memory_order_relaxed),
        g_status.deltaCm.load(std::memory_order_relaxed), g_status.yawDeg.load(std::memory_order_relaxed),
        g_status.pitchDeg.load(std::memory_order_relaxed));
}
