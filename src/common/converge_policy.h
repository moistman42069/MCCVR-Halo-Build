#pragma once

// CONVERGENCE POLICY -- when the converged aim (the traced reticle, the converged mark, the scope's
// convergence and the muzzle redirect that rides on it) is switched OFF, and what is only WITNESSED.
//
// THE RULE: convergence and direct drive are never switched off for a NATURAL,
// in-game cause. A seat stop, a turret's rate cap, an aim the game is still integrating toward the hand --
// in flat Halo those already send a shot somewhere the reticle is not (a stuck vehicle's shots do), and the
// game shows it the way it always has: tracers and impacts. That is close enough to the game's own behaviour
// that a second, VR-only signal (an "honest reticle" jumping onto the gun) is not wanted.
//
// What still switches convergence off is a failure of OUR code or a state the converged aim does not apply
// to: an address that did not resolve, a trace that faulted or went stale, a seat we have not implemented, an
// aim feedback we cannot read. Those are the ConvergeOff reasons below.
//
// The natural causes are still COUNTED (NaturalWitness, dev lines only) as the evidence that they
// leave convergence on. They are never an input to a decision: SeatedClaimDecide's facts hold no natural
// cause by construction (tests/converge_policy_tests.cpp pins their layout, tools/check_seated_claim.py the
// call site in game.cpp).
//
// Zero engine dependencies; unit-tested in tests/converge_policy_tests.cpp.

#include <atomic>
#include <cmath>
#include <cstdint>

namespace converge {

// Why the converged aim is not live this poll. Every one is our code's or a non-gameplay state's -- never a
// natural in-game limit (those are NaturalEvents, witnessed only).
enum class ConvergeOff : uint8_t {
    None,
    AimOff,              // VR aim, converged_aim or the title's aim feature is off
    NoTraceFlags,        // the crosshair trace's flags did not resolve
    TraceFaulted,        // the trace raised a native fault and latched off
    TraceStale,          // no fresh trace (a hitch, a load, a pause)
    RayStale,            // no fresh intent ray
    NoRange,             // no usable range: no hit yet since the traces restarted, or a point inside the range floor
    PitchRail,           // Reach only: the bent pitch would pass its rail (Halo 3/ODST/Halo 4 clamp at the
                         // rail and stay converged, aim_solve::converge_direction)
    SeatUnnamed,         // a seat whose vehicle the sampler could not name (the trace cannot look past it)
    SeatViewNotOwned,    // a seat whose first-person view is not ours (a chase camera)
    SeatNotImplemented,  // a seat this title does not converge yet
    DriveNotArmed,       // the direct drive has not proven its record yet
    CallerMapUnproven,   // the aim caller map is unproven (every call counts)
    AimFeedbackInvalid,  // the game's aim could not be read as a finite direction
    MarkStale,           // the converged mark is older than its freshness window
    SeatProofPending,    // a seat whose own proofs (Halo 4's eye agreement, its record facts) do not hold yet
    SeatSteering,        // a seat whose stick or wheel steers right now: our control mode leaves the heading off
                         // the hand, so the hand's point is not where the gun turns (Reach's 0x100; Halo 3 /
                         // ODST's C9 steering author, Halo3SeatAuthorsSteeringNow)
    Count
};

inline const char* ConvergeOffName(ConvergeOff why)
{
    switch (why) {
    case ConvergeOff::None: return "None";
    case ConvergeOff::AimOff: return "AimOff";
    case ConvergeOff::NoTraceFlags: return "NoTraceFlags";
    case ConvergeOff::TraceFaulted: return "TraceFaulted";
    case ConvergeOff::TraceStale: return "TraceStale";
    case ConvergeOff::RayStale: return "RayStale";
    case ConvergeOff::NoRange: return "NoRange";
    case ConvergeOff::PitchRail: return "PitchRail";
    case ConvergeOff::SeatUnnamed: return "SeatUnnamed";
    case ConvergeOff::SeatViewNotOwned: return "SeatViewNotOwned";
    case ConvergeOff::SeatNotImplemented: return "SeatNotImplemented";
    case ConvergeOff::DriveNotArmed: return "DriveNotArmed";
    case ConvergeOff::CallerMapUnproven: return "CallerMapUnproven";
    case ConvergeOff::AimFeedbackInvalid: return "AimFeedbackInvalid";
    case ConvergeOff::MarkStale: return "MarkStale";
    case ConvergeOff::SeatProofPending: return "SeatProofPending";
    case ConvergeOff::SeatSteering: return "SeatSteering";
    case ConvergeOff::Count: break;
    }
    return "?";
}

// THE SEATED CONVERGED CLAIM (Halo 3, ODST, Halo 4): the mark, so the traced reticle, the scope's convergence and
// the muzzle redirect that rides on it. Claimed whenever the aim was solved through the traced point -- exactly as
// on foot. Every fact it is decided on is our code's or our control mode's: nothing about the seat's limits, a turret's rate cap or how
// far the game's aim trails the target takes part (those are natural, and the game shows them itself).
//
// This is the WHOLE decision: Game_ComputeAimStick assigns its claim from SeatedClaimDecide and nothing else
// (tools/check_seated_claim.py checks that call site), and the facts struct holds no natural cause by
// construction (tests/converge_policy_tests.cpp pins its layout).
//
// handOwnsHeading is a CONTROL MODE of ours, not a natural limit: with the view following the hull, a seat whose
// stick or wheel steers (C9, vehicle_view_follow) never actuates the heading the aim was solved for -- the stick
// or wheel does -- so the hand's point is not where the gun is turned to. Reach withholds its claim in the same
// state (ConvergeOff::SeatSteering, aim-why 0x100).
struct SeatedClaimFacts {
    bool solved = false;              // the aim was bent through the traced point this poll
    bool pointFinite = true;          // the point it was bent through is a finite position (Halo 4 computes it)
    bool handOwnsHeading = true;      // our code actuates the solved heading (no stick or wheel steering author)
    bool aimFeedbackFinite = false;   // the game's aim feedback reads as a direction
};
// `why` names the reason only when the aim WAS solved and the claim is still withheld; an unsolved poll keeps the
// solve's own reason (None here).
struct SeatedClaim {
    bool claim = false;
    ConvergeOff why = ConvergeOff::None;
};
inline SeatedClaim SeatedClaimDecide(const SeatedClaimFacts& f)
{
    SeatedClaim c;
    if (!f.solved)
        return c;
    if (!f.pointFinite) { c.why = ConvergeOff::NoRange; return c; }
    if (!f.handOwnsHeading) { c.why = ConvergeOff::SeatSteering; return c; }
    if (!f.aimFeedbackFinite) { c.why = ConvergeOff::AimFeedbackInvalid; return c; }
    c.claim = true;
    return c;
}

// A timestamp (GetTickCount64) still inside its freshness window. 0 = never stamped.
inline bool StampLive(uint64_t at_ms, uint64_t now_ms, uint64_t window_ms)
{
    return at_ms && now_ms >= at_ms && now_ms - at_ms <= window_ms;
}

// REACH, on the sim thread, after each stick update of desired_angles: what the update now in effect claims.
// `converged` (the traced reticle, the scope's convergence, and Reach's shot fallbacks standing down) follows
// the write and the converged setpoint ONLY -- a seat limit the game clamped the write to does not withdraw it,
// as in every other title (in flat Reach a stopped gun's shots already go where the crosshair is not).
// `heldAtLimit` is recorded separately and read only DEFENSIVELY: the muzzle redirect refuses while it is live,
// and the dev engine-trace witness skips those updates. Neither ever changes the claim or the write.
struct ReachUpdateStamps {
    bool converged = false;
    bool heldAtLimit = false;
};
inline ReachUpdateStamps ReachStampUpdate(bool wrote, bool drivenConverged, bool limited)
{
    ReachUpdateStamps s;
    s.converged = wrote && drivenConverged;
    s.heldAtLimit = wrote && limited;
    return s;
}

// SHOTS FROM THE MUZZLE in a seat (defensive: no title is known to publish a seated muzzle palette, but Halo 3
// and ODST only refuse a unit in a VEHICLE, and unit_in_vehicle answers false in a stationary turret). The
// redirect bends a shot through the converged point from the muzzle, which is right only while the gun points
// at that point. A seat held at its stop, or a rate-capped turret still slewing toward the hand, points
// elsewhere -- naturally, as in flat Halo -- so the redirect stands down and the shot leaves as the game fires
// it. A turret refuses even when it is not slewing: a rate cap is not latched, so "held" cannot see it.
// Only the REDIRECT is refused; the convergence claim, the reticle and the write are untouched.
enum class SeatMuzzleRefusal : uint8_t { None, HeldAtLimit, Turret };
inline SeatMuzzleRefusal SeatMuzzleRefuses(bool heldAtLimit, bool turretSeat)
{
    if (turretSeat) return SeatMuzzleRefusal::Turret;
    if (heldAtLimit) return SeatMuzzleRefusal::HeldAtLimit;
    return SeatMuzzleRefusal::None;
}

// THE CROSSHAIR-ASSIST RE-AIM in a seat (Halo 3 / ODST) -- a different decision from the muzzle redirect
// above. The re-aim moves only the firing assist's own crosshair query (Halo 3 0x13BAD0 from the fire routine,
// ODST 0x1610D4) through the point; the GAME then turns the shot from its barrel toward that query's hit, in a seat by
// at most max(21 deg, the weapon's own angle) -- its own cap, on every seated shot. So a turret needs no refusal here:
//   * a gun-slaved turret's camera -- the line the assist traces -- sits off the record the drive writes, and moves
//     again at the fire (measured on the Warthog gauss turret 0x069C: its query ~0.5 deg yaw and ~1.1 deg pitch off
//     the record at every fire, against ~0.8 deg at rest; without the re-aim its slugs land 1.7-2.1 m beside the
//     reticle at 87-89 m while the chaingun's stay within 0.2 m). Re-aimed, the assist carries the shot onto the
//     point whatever the camera's offset at the fire;
//   * while the gun catches up under its rate cap the game bends the shot toward the point by no more than it bends
//     any seated shot -- turn-rate catch-up is not a limit, as for a look-steered driver's
//     hull-fixed guns, whose catch-up shots this re-aim already turns.
// Only a seat HELD at its natural stop refuses: the reticle is clamped there and the shot leaves along the clamped
// aim, through the clamped mark. Reach's crosshair re-aim never refuses a turret either (reach_sight_trace.inl).
// `turretSeat` is taken so the decision is explicit and tested: it never refuses by itself.
inline SeatMuzzleRefusal SeatAssistReaimRefuses(bool heldAtLimit, bool turretSeat)
{
    (void)turretSeat;
    return heldAtLimit ? SeatMuzzleRefusal::HeldAtLimit : SeatMuzzleRefusal::None;
}

// The game's aim feedback as a direction: finite and not degenerate.
inline bool AimFeedbackFinite(float x, float y, float z)
{
    const float length = std::sqrt(x * x + y * y + z * z);
    return std::isfinite(length) && length > 1.0e-4f;
}

// DEV ASSESSMENT ONLY (honest_reticle_assess, parsed in dev builds only): draw a second marker where the game's
// shot lands, beside the unchanged reticle, to judge by eye how far the game trails the hand. 0 = off, 1 = on foot
// only, 2 = everywhere. It draws that marker and nothing else: the reticle, the mark, the shots, the write and the
// drive mode never read it (tools/check_assess_isolation.py).
inline bool AssessDrawsGameAim(int assess, bool seated) { return assess >= 2 || (assess == 1 && !seated); }

// ---- the converge-off witness (dev lines only) ------------------------------------------------------------

// One title's converged state, poll by poll: how many polls it was live, why it was not on the others (the
// FIRST failing reason, which is always our code's or a non-gameplay state's), and how often it went off and
// came back. Written by the title's aim poll (one thread per title), taken by the cold worker. Lock-free. Only
// polls in gameplay with the aim running are noted (the aim poll returns before the witness otherwise).
struct ConvergeOffCounts {
    uint32_t polls = 0, live = 0, offEdges = 0, onEdges = 0;
    uint32_t why[static_cast<size_t>(ConvergeOff::Count)]{};
    uint32_t off() const { return polls >= live ? polls - live : 0; }
};

// The state byte the poll leaves behind: 0 = never noted, 1 = live, 2 + reason = off for that reason.
constexpr uint8_t kConvergeStateNone = 0;
constexpr uint8_t kConvergeStateLive = 1;
inline uint8_t ConvergeOffState(ConvergeOff why) { return static_cast<uint8_t>(2u + static_cast<unsigned>(why)); }
inline bool ConvergeStateIsOff(uint8_t state) { return state >= 2; }
inline ConvergeOff ConvergeStateReason(uint8_t state)
{
    const unsigned r = state >= 2 ? state - 2u : 0u;
    return r < static_cast<unsigned>(ConvergeOff::Count) ? static_cast<ConvergeOff>(r) : ConvergeOff::None;
}

struct ConvergeOffWitness {
    std::atomic<uint32_t> polls{0}, live{0}, offEdges{0}, onEdges{0};
    std::atomic<uint32_t> why[static_cast<size_t>(ConvergeOff::Count)]{};
    std::atomic<uint8_t> state{kConvergeStateNone};

    // `isLive`: the converged aim is in effect this poll. Otherwise `reason` is the first thing that failed
    // (None when the poll could not name it; logged as "Unclassified", which is itself a finding).
    void note(bool isLive, ConvergeOff reason)
    {
        polls.fetch_add(1, std::memory_order_relaxed);
        uint8_t now = kConvergeStateLive;
        if (isLive)
            live.fetch_add(1, std::memory_order_relaxed);
        else
        {
            if (static_cast<unsigned>(reason) >= static_cast<unsigned>(ConvergeOff::Count))
                reason = ConvergeOff::None;
            why[static_cast<size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
            now = ConvergeOffState(reason);
        }
        const uint8_t before = state.exchange(now, std::memory_order_relaxed);
        if (before == kConvergeStateLive && now != kConvergeStateLive)
            offEdges.fetch_add(1, std::memory_order_relaxed);
        else if (ConvergeStateIsOff(before) && now == kConvergeStateLive)
            onEdges.fetch_add(1, std::memory_order_relaxed);
    }

    uint8_t current() const { return state.load(std::memory_order_relaxed); }

    ConvergeOffCounts take()
    {
        ConvergeOffCounts c;
        c.polls = polls.exchange(0, std::memory_order_relaxed);
        c.live = live.exchange(0, std::memory_order_relaxed);
        c.offEdges = offEdges.exchange(0, std::memory_order_relaxed);
        c.onEdges = onEdges.exchange(0, std::memory_order_relaxed);
        for (size_t i = 0; i < static_cast<size_t>(ConvergeOff::Count); ++i)
            c.why[i] = why[i].exchange(0, std::memory_order_relaxed);
        return c;
    }
};

// The cold worker's EDGE log for one title: one line when the state it sees differs from the one it last
// logged (the converged aim went off, or came back), at most `max_lines` per `window_ms` so a flicker cannot
// flood the log -- the 2 s summary still counts every edge the poll saw, including any between two looks.
// A title first seen live logs nothing; one first seen off logs that.
struct ConvergeEdgeLog {
    uint8_t logged = kConvergeStateNone;
    uint64_t window_ms = 2000;
    uint32_t max_lines = 6;
    uint64_t window_start_ms = 0;
    uint32_t lines = 0, suppressed = 0;

    // True: write an edge line for `current`; `previous` receives the state last logged. A suppressed edge still
    // moves what is logged, so the next line describes a real change.
    bool observe(uint8_t current, uint64_t now_ms, uint8_t& previous)
    {
        previous = logged;
        if (current == kConvergeStateNone || current == logged)
            return false;
        logged = current;
        if (previous == kConvergeStateNone && current == kConvergeStateLive)
            return false;
        if (!window_start_ms || now_ms < window_start_ms || now_ms - window_start_ms >= window_ms)
        {
            window_start_ms = now_ms ? now_ms : 1;
            lines = 0;
        }
        if (lines >= max_lines)
        {
            ++suppressed;
            return false;
        }
        ++lines;
        return true;
    }

    uint32_t take_suppressed()
    {
        const uint32_t s = suppressed;
        suppressed = 0;
        return s;
    }
};

// REACH, per aim poll of its provider: whether this poll is counted at all, and if so why the converged aim is
// not live. False = not counted: the aim-write conditions 0x01-0x40 are non-gameplay states (another title, a
// menu, a cinematic) or our sampler's stale stamps around a hitch. The order is the provider's own.
struct ReachConvergePoll {
    bool live = false;           // a WriteState actuation with a converged setpoint this poll
    bool aimOn = false;          // reach_direct_drive == 3 and converged_aim
    bool driveReady = false;     // the hook is armed and its heartbeat fresh
    uint32_t aimWhy = 0;         // Game_ReachDirectDriveAimWhy()
    bool flagsResolved = false;  // the crosshair trace's flags resolved
    bool traceFaulted = false;   // the trace latched off on a native fault
    bool sightSample = false;    // a fresh ray AND a fresh trace result this poll
    bool rangeKnown = false;     // the solve had a range to converge on (else nothing to bend through yet)
};
inline bool ReachConvergeOffReason(const ReachConvergePoll& p, ConvergeOff& why)
{
    why = ConvergeOff::None;
    if (p.live)
        return true;
    if (!p.aimOn) { why = ConvergeOff::AimOff; return true; }
    if (p.aimWhy & 0x7Fu)
        return false;
    if (p.aimWhy & 0x80u) { why = ConvergeOff::SeatViewNotOwned; return true; }
    if (p.aimWhy & 0x100u) { why = ConvergeOff::SeatSteering; return true; }
    if (p.aimWhy & 0x200u) { why = ConvergeOff::AimOff; return true; }   // reach_vehicle_aim / seat_direct_drive
    if (p.aimWhy & 0x400u) { why = ConvergeOff::SeatProofPending; return true; }   // the seat's frame: unread
    if (!p.driveReady) { why = ConvergeOff::DriveNotArmed; return true; }
    if (!p.flagsResolved) { why = ConvergeOff::NoTraceFlags; return true; }
    if (p.traceFaulted) { why = ConvergeOff::TraceFaulted; return true; }
    if (!p.sightSample) { why = ConvergeOff::TraceStale; return true; }
    // A fresh trace and still no converged setpoint: no range yet, or the bend passed Reach's rail.
    why = p.rangeKnown ? ConvergeOff::PitchRail : ConvergeOff::NoRange;
    return true;
}

// ---- natural causes: witnessed, never decided on -------------------------------------------------------

// The natural causes seen this poll: each can hold the game's aim off the converged point, and none of them may
// switch convergence off (or move the reticle onto the gun).
struct NaturalEvents {
    bool latchHeld = false;     // the seat-limit latch held a written axis at the game's clamp
    bool outcomeMiss = false;   // the game's aim stayed off the written target (seat_drive::OutcomeGate's trigger)
    bool stall = false;         // the servo's error stalled past the 250 ms stall check (StallWitness)
    bool turretSlew = false;    // a rate-capped turret seat
    bool reachLimited = false;  // Reach's seat latch held the write at a limit
    bool yawShortH4 = false;    // a yaw-only seat whose game yaw stayed off the writes (Halo 4's slope frame)
};

// Whether any natural cause held the game's aim off the converged point this poll. Read only by the dev
// witnesses that compare the game's line with the point (they skip such polls), never by a decision.
inline bool NaturalHoldsAimOff(const NaturalEvents& e)
{
    return e.latchHeld || e.outcomeMiss || e.stall || e.turretSlew || e.reachLimited || e.yawShortH4;
}

struct NaturalCounts {
    uint32_t polls = 0;
    uint32_t latchHeld = 0, outcomeMiss = 0, stall = 0, turretSlew = 0, reachLimited = 0, yawShortH4 = 0;
};

// Lock-free counters, one per natural cause; any thread may note, the cold worker takes them every report.
struct NaturalWitness {
    std::atomic<uint32_t> polls{0};
    std::atomic<uint32_t> latchHeld{0}, outcomeMiss{0}, stall{0}, turretSlew{0}, reachLimited{0}, yawShortH4{0};

    void note(const NaturalEvents& e)
    {
        polls.fetch_add(1, std::memory_order_relaxed);
        if (e.latchHeld) latchHeld.fetch_add(1, std::memory_order_relaxed);
        if (e.outcomeMiss) outcomeMiss.fetch_add(1, std::memory_order_relaxed);
        if (e.stall) stall.fetch_add(1, std::memory_order_relaxed);
        if (e.turretSlew) turretSlew.fetch_add(1, std::memory_order_relaxed);
        if (e.reachLimited) reachLimited.fetch_add(1, std::memory_order_relaxed);
        if (e.yawShortH4) yawShortH4.fetch_add(1, std::memory_order_relaxed);
    }

    NaturalCounts take()
    {
        NaturalCounts c;
        c.polls = polls.exchange(0, std::memory_order_relaxed);
        c.latchHeld = latchHeld.exchange(0, std::memory_order_relaxed);
        c.outcomeMiss = outcomeMiss.exchange(0, std::memory_order_relaxed);
        c.stall = stall.exchange(0, std::memory_order_relaxed);
        c.turretSlew = turretSlew.exchange(0, std::memory_order_relaxed);
        c.reachLimited = reachLimited.exchange(0, std::memory_order_relaxed);
        c.yawShortH4 = yawShortH4.exchange(0, std::memory_order_relaxed);
        return c;
    }
};

// The servo stall check, a witness only (nothing decides on it): the error is "stalled" when it is at least
// `stall_rad`, not shrinking by more than `converging_rad` a poll, and has stayed so for `hold_ms` while seated.
struct StallWitness {
    float stall_rad = 0.12f;          // ~7 deg, far beyond the loop's ordinary lag
    float converging_rad = 0.005f;
    unsigned long long hold_ms = 250;

    unsigned long long since_ms = 0;
    float previous = 0.0f;

    void reset() { since_ms = 0; previous = 0.0f; }

    bool step(bool seated, float error_rad, unsigned long long now_ms)
    {
        const bool finite = std::isfinite(error_rad);
        if (!seated || !finite || error_rad < stall_rad || error_rad < previous - converging_rad)
            since_ms = 0;
        else if (!since_ms)
            since_ms = now_ms ? now_ms : 1;
        previous = finite ? error_rad : 0.0f;
        return since_ms && now_ms >= since_ms && now_ms - since_ms >= hold_ms;
    }
};

} // namespace converge
