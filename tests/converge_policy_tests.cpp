// Convergence policy (src/common/converge_policy.h): what switches the converged aim off (our code only), the
// dev assessment key's scope, and the natural-cause witnesses that are never an input to a decision.
#include "converge_policy.h"
#include "shot_lag_logic.h"
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace converge;

// THE REGRESSION GUARD: no natural in-game cause withholds the seated claim. It is decided on SeatedClaimFacts
// alone, whose fields are all our own code's -- solved, a finite point, our control mode owning the heading, a
// readable aim feedback -- so no natural cause can be
// handed to it. Adding a field (a latch, a stall, a turret, an outcome miss) fails these compile-time checks
// before it can fail a player; the call site in game.cpp is pinned by tools/check_seated_claim.py.
static_assert(std::is_same_v<decltype(&SeatedClaimDecide), SeatedClaim (*)(const SeatedClaimFacts&)>,
              "the seated converged claim is decided on SeatedClaimFacts alone");
static_assert(sizeof(SeatedClaimFacts) == 4 * sizeof(bool) && std::is_trivially_copyable_v<SeatedClaimFacts>,
              "SeatedClaimFacts holds exactly our code's four facts: no natural in-game cause");

// Shorthand: the claim for one combination of the facts.
bool Claims(bool solved, bool pointFinite, bool aimFeedbackFinite, bool handOwnsHeading = true)
{
    SeatedClaimFacts f;
    f.solved = solved;
    f.pointFinite = pointFinite;
    f.handOwnsHeading = handOwnsHeading;
    f.aimFeedbackFinite = aimFeedbackFinite;
    return SeatedClaimDecide(f).claim;
}

void TestClaimIgnoresNaturalCauses()
{
    // Every combination of the natural causes that must not switch a seat's convergence off, witnessed as they
    // are in the game; the claim is the same as with none of them.
    NaturalWitness witness;
    unsigned combinations = 0, changed = 0;
    for (unsigned bits = 0; bits < 64; ++bits)
    {
        NaturalEvents e;
        e.latchHeld = (bits & 1u) != 0;
        e.outcomeMiss = (bits & 2u) != 0;
        e.stall = (bits & 4u) != 0;
        e.turretSlew = (bits & 8u) != 0;
        e.reachLimited = (bits & 16u) != 0;
        e.yawShortH4 = (bits & 32u) != 0;
        for (unsigned facts = 0; facts < 16; ++facts)
        {
            const bool solved = (facts & 1u) != 0, point = (facts & 2u) != 0, finite = (facts & 4u) != 0;
            const bool owns = (facts & 8u) != 0;
            witness.note(e);
            ++combinations;
            if (Claims(solved, point, finite, owns) != (solved && point && finite && owns))
                ++changed;
        }
    }
    Check(combinations == 1024 && changed == 0, "no natural cause changes the seated converged claim");
    {
        unsigned held = 0;
        for (unsigned bits = 0; bits < 64; ++bits)
        {
            NaturalEvents e;
            e.latchHeld = (bits & 1u) != 0;
            e.outcomeMiss = (bits & 2u) != 0;
            e.stall = (bits & 4u) != 0;
            e.turretSlew = (bits & 8u) != 0;
            e.reachLimited = (bits & 16u) != 0;
            e.yawShortH4 = (bits & 32u) != 0;
            held += NaturalHoldsAimOff(e) ? 1u : 0u;
        }
        Check(held == 63, "any natural cause marks the poll held off the point (for the dev witnesses only)");
    }
    const NaturalCounts counts = witness.take();
    Check(counts.polls == 1024 && counts.latchHeld == 512 && counts.outcomeMiss == 512 && counts.stall == 512 &&
              counts.turretSlew == 512 && counts.reachLimited == 512 && counts.yawShortH4 == 512,
          "the witness counts every natural cause it is shown");
    const NaturalCounts again = witness.take();
    Check(again.polls == 0 && again.latchHeld == 0 && again.yawShortH4 == 0, "taking the counts resets them");

    // Our code's failures still decline, each with its reason; an unsolved poll keeps the solve's own.
    Check(Claims(true, true, true), "solved with a readable aim: claimed, as on foot");
    Check(!Claims(false, true, true), "not solved (no fresh trace, an unnamed seat): not claimed");
    Check(!Claims(true, false, true), "a point that is not a finite position: not claimed");
    Check(!Claims(true, true, false), "an unreadable aim feedback: not claimed");
    {
        SeatedClaimFacts f;
        f.solved = true;
        f.aimFeedbackFinite = true;
        Check(SeatedClaimDecide(f).claim && SeatedClaimDecide(f).why == ConvergeOff::None, "claimed: no reason");
        f.pointFinite = false;
        Check(SeatedClaimDecide(f).why == ConvergeOff::NoRange, "a non-finite point: NoRange");
        f.pointFinite = true;
        f.aimFeedbackFinite = false;
        Check(SeatedClaimDecide(f).why == ConvergeOff::AimFeedbackInvalid, "an unreadable aim: AimFeedbackInvalid");
        f.aimFeedbackFinite = true;
        f.handOwnsHeading = false;
        Check(!SeatedClaimDecide(f).claim && SeatedClaimDecide(f).why == ConvergeOff::SeatSteering,
              "a seat whose stick or wheel steers the heading: SeatSteering, as Reach's 0x100");
        f.solved = false;
        Check(!SeatedClaimDecide(f).claim && SeatedClaimDecide(f).why == ConvergeOff::None,
              "unsolved: the solve's own reason stands");
    }
}

void TestAimFeedback()
{
    Check(AimFeedbackFinite(1.0f, 0.0f, 0.0f), "a unit aim is finite");
    Check(AimFeedbackFinite(0.0f, 0.0f, 3.0f), "an unnormalised aim is still a direction");
    Check(!AimFeedbackFinite(0.0f, 0.0f, 0.0f), "a zero aim is not a direction");
    Check(!AimFeedbackFinite(std::nanf(""), 0.0f, 1.0f), "a NaN aim is not a direction");
    Check(!AimFeedbackFinite(INFINITY, 0.0f, 0.0f), "an infinite aim is not a direction");
}

void TestAssessment()
{
    Check(!AssessDrawsGameAim(0, false) && !AssessDrawsGameAim(0, true), "0: never");
    Check(AssessDrawsGameAim(1, false) && !AssessDrawsGameAim(1, true), "1: on foot only");
    Check(AssessDrawsGameAim(2, false) && AssessDrawsGameAim(2, true), "2: everywhere");
    Check(!AssessDrawsGameAim(-1, false), "a negative key is off");
    // The assessment key is not an input of the claim: the claim is pinned to two facts above, so this
    // documents the separation rather than computing it.
    Check(Claims(true, true, true), "the claim stands whatever the assessment draws");
}

void TestStallWitness()
{
    {
        StallWitness s;
        Check(!s.step(true, 0.2f, 1000), "a fresh stall is not yet a stall");
        Check(!s.step(true, 0.2f, 1200), "not before 250 ms");
        Check(s.step(true, 0.2f, 1250), "held 250 ms: stalled");
        Check(!s.step(false, 0.2f, 1300), "on foot never stalls");
    }
    {
        StallWitness s;
        s.step(true, 0.3f, 1000);
        Check(!s.step(true, 0.2f, 1300), "a shrinking error is converging, not stalled");
        Check(!s.step(true, 0.1f, 1400), "under 0.12 rad is ordinary lag");
        Check(!s.step(true, std::nanf(""), 1500), "a non-finite error is no evidence");
    }
}

void TestNames()
{
    bool all = true;
    for (unsigned i = 0; i < static_cast<unsigned>(ConvergeOff::Count); ++i)
        all = all && ConvergeOffName(static_cast<ConvergeOff>(i))[0] != '?';
    Check(all, "every ConvergeOff reason has a name");
}

void TestReachStamps()
{
    // The converged stamp follows the write and the converged setpoint; a seat limit never withdraws it.
    unsigned changed = 0;
    for (int wrote = 0; wrote < 2; ++wrote)
        for (int conv = 0; conv < 2; ++conv)
        {
            const ReachUpdateStamps free = ReachStampUpdate(wrote != 0, conv != 0, false);
            const ReachUpdateStamps held = ReachStampUpdate(wrote != 0, conv != 0, true);
            if (free.converged != held.converged)
                ++changed;
            Check(free.converged == (wrote && conv), "Reach: converged = wrote and a converged setpoint");
            Check(!free.heldAtLimit, "Reach: not limited, not held");
            Check(held.heldAtLimit == (wrote != 0), "Reach: held only when this update wrote");
        }
    Check(changed == 0, "Reach: a seat limit does not change the converged claim");
    Check(ReachStampUpdate(true, true, true).converged, "Reach: held at a seat's stop and still converged");
    Check(!ReachStampUpdate(false, true, true).converged, "Reach: nothing written, nothing claimed");

    Check(!StampLive(0, 1000, 100), "a stamp never set is not live");
    Check(StampLive(1000, 1100, 100), "inside the window");
    Check(!StampLive(1000, 1101, 100), "past the window");
    Check(!StampLive(1000, 999, 100), "a clock that went backwards is not live");
}

void TestSeatMuzzleRefusal()
{
    Check(SeatMuzzleRefuses(false, false) == SeatMuzzleRefusal::None, "free seat: the redirect may run");
    Check(SeatMuzzleRefuses(true, false) == SeatMuzzleRefusal::HeldAtLimit, "held at the stop: refused");
    Check(SeatMuzzleRefuses(false, true) == SeatMuzzleRefusal::Turret,
          "a turret refuses even when not latched (a rate cap is not a latch)");
    Check(SeatMuzzleRefuses(true, true) == SeatMuzzleRefusal::Turret, "a held turret reports the turret");
    // The refusal is a separate decision from the claim: the claim's signature takes neither input.
    Check(Claims(true, true, true), "the claim stands whatever the muzzle guard decides");
}

// ---- the crosshair-assist re-aim in a seat (converge::SeatAssistReaimRefuses) ----------------------------------
float Deg(float rad) { return rad * 57.2957795f; }
void Fwd(float yawDeg, float pitchDeg, float out[3])
{
    const float y = yawDeg / 57.2957795f, p = pitchDeg / 57.2957795f;
    out[0] = std::cos(p) * std::cos(y);
    out[1] = std::cos(p) * std::sin(y);
    out[2] = std::sin(p);
}
float AngleDeg(const float a[3], const float b[3])
{
    const float c[3]{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    return Deg(std::atan2(shot_lag::Length(c), shot_lag::Dot(a, b)));
}
// The firing assist as Halo 3 runs it for a seated shot (0x13BAD0; HALO3-VEHICLE-EVIDENCE.md "Vehicle guns"): the
// crosshair query from `start` along `query` hits the wall x = wallX; the shot leaving the barrel along `barrel` is
// turned toward hit - origin by at most `capDeg`. Returns the turn taken; `out` is the shot's direction.
float Assist(const float start[3], const float query[3], float wallX, const float origin[3], const float barrel[3],
             float capDeg, float out[3])
{
    const float t = (wallX - start[0]) / query[0];
    const float hit[3]{start[0] + query[0] * t, start[1] + query[1] * t, start[2] + query[2] * t};
    float to[3]{hit[0] - origin[0], hit[1] - origin[1], hit[2] - origin[2]};
    const float tl = shot_lag::Length(to);
    for (float& v : to)
        v /= tl;
    const float angle = AngleDeg(barrel, to);
    if (angle <= capDeg)
    {
        for (int i = 0; i < 3; ++i)
            out[i] = to[i];
        return angle;
    }
    // Rotate toward by the cap, in the plane of the two directions.
    float perp[3]{to[0] - barrel[0] * shot_lag::Dot(barrel, to), to[1] - barrel[1] * shot_lag::Dot(barrel, to),
                  to[2] - barrel[2] * shot_lag::Dot(barrel, to)};
    const float pl = shot_lag::Length(perp);
    const float c = capDeg / 57.2957795f;
    for (int i = 0; i < 3; ++i)
        out[i] = barrel[i] * std::cos(c) + perp[i] / pl * std::sin(c);
    return capDeg;
}
// How far the shot's line from `origin` along `dir` passes from `p`, in millimetres.
float MissMm(const float origin[3], const float dir[3], const float p[3])
{
    const float v[3]{p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
    const float c[3]{v[1] * dir[2] - v[2] * dir[1], v[2] * dir[0] - v[0] * dir[2], v[0] * dir[1] - v[1] * dir[0]};
    return shot_lag::Length(c) * shot_lag::kMmPerWu;
}

void TestSeatAssistReaim()
{
    Check(SeatAssistReaimRefuses(false, false) == SeatMuzzleRefusal::None, "a free seat is re-aimed");
    Check(SeatAssistReaimRefuses(false, true) == SeatMuzzleRefusal::None,
          "a turret is NOT refused for being one: the assist's own seat cap bounds a catch-up shot");
    Check(SeatAssistReaimRefuses(true, false) == SeatMuzzleRefusal::HeldAtLimit, "a seat held at its stop refuses");
    Check(SeatAssistReaimRefuses(true, true) == SeatMuzzleRefusal::HeldAtLimit, "a turret held at its stop refuses");
    // The muzzle redirect keeps refusing a turret: that is an identity guard (the muzzle is not the turret's gun).
    Check(SeatMuzzleRefuses(false, true) == SeatMuzzleRefusal::Turret, "the muzzle redirect still refuses a turret");

    // THE GAUSS TURRET (a measured shot): the record runs through the point P at 87.8 m from the
    // gun's camera (the solve's eye); at the fire the camera -- the line the assist traces -- sits +0.5 deg yaw and
    // +1.1 deg pitch off it, and the barrel, 208 mm behind, 64 mm across and 58 mm below the camera, is already on the
    // camera's crosshair (the assist turns it 0.01-0.06 deg). Unit vectors in world units, x along the record.
    const float cap = 21.0f;
    const float start[3]{0.0f, 0.0f, 0.0f};
    const float range = 87.8f * shot_lag::kWuPerMetre;
    const float p[3]{range, 0.0f, 0.0f};
    float camera[3]{};
    Fwd(0.5f, 1.1f, camera);
    float query[3]{camera[0] * 128.0f, camera[1] * 128.0f, camera[2] * 128.0f};
    const float origin[3]{-0.208f / 3.048f, 0.064f / 3.048f, -0.058f / 3.048f};
    float barrel[3]{};
    {
        const float t = range / camera[0];
        float to[3]{camera[0] * t - origin[0], camera[1] * t - origin[1], camera[2] * t - origin[2]};
        const float l = shot_lag::Length(to);
        for (int i = 0; i < 3; ++i)
            barrel[i] = to[i] / l;
    }
    float shot[3]{};
    // As the game builds it (the turret refused): the shot follows the camera's crosshair, ~1.8 m off P.
    const float turnedStock = Assist(start, query, range, origin, barrel, cap, shot);
    Check(turnedStock < 0.01f && MissMm(origin, shot, p) > 1500.0f,
          "unre-aimed, the gauss slug follows the gun camera's crosshair 1.5+ m beside the point at 88 m");
    // Re-aimed through P from the query's own start (no turret refusal): the assist carries the shot onto P.
    const bool held = SeatAssistReaimRefuses(false, true) != SeatMuzzleRefusal::None;
    float reaimed[3]{};
    Check(!held && shot_lag::ReaimThroughPoint(start, query, p, shot_lag::ReaimRails{}, reaimed) ==
                       shot_lag::Reaim::Applied,
          "the turret's crosshair query is re-aimed");
    const float turned = Assist(start, reaimed, range, origin, barrel, cap, shot);
    Check(MissMm(origin, shot, p) < 1.0f, "re-aimed, the gauss slug passes through the point (< 1 mm)");
    Check(turned > 1.0f && turned < 1.5f, "...by a turn of ~1.2 deg, far inside the game's 21 deg seat cap");

    // CATCH-UP: the gun still 40 deg off the point under its rate cap. The game bends the shot toward P by its own
    // cap and no further -- the re-aim adds no limit and removes none.
    float slewing[3]{};
    Fwd(40.0f, 0.0f, slewing);
    const float slewQuery[3]{slewing[0] * 128.0f, slewing[1] * 128.0f, slewing[2] * 128.0f};
    float slewReaimed[3]{};
    Check(shot_lag::ReaimThroughPoint(start, slewQuery, p, shot_lag::ReaimRails{}, slewReaimed) ==
              shot_lag::Reaim::Applied,
          "a catch-up query is re-aimed too");
    const float slewTurn = Assist(start, slewReaimed, range, origin, slewing, cap, shot);
    Check(std::fabs(slewTurn - cap) < 1e-3f && std::fabs(AngleDeg(slewing, shot) - cap) < 0.01f,
          "a catch-up shot turns by the game's seat cap, no further");
}

void TestConvergeOffWitness()
{
    ConvergeOffWitness w;
    Check(w.current() == kConvergeStateNone, "nothing noted yet");
    w.note(true, ConvergeOff::None);
    w.note(true, ConvergeOff::TraceStale);   // a live poll ignores a reason handed with it
    w.note(false, ConvergeOff::TraceStale);
    w.note(false, ConvergeOff::TraceStale);
    w.note(false, ConvergeOff::SeatUnnamed);   // off to off (another reason) is not an edge
    w.note(true, ConvergeOff::None);
    w.note(false, ConvergeOff::Count);         // out of range: unclassified
    Check(ConvergeStateIsOff(w.current()) && ConvergeStateReason(w.current()) == ConvergeOff::None,
          "the state byte holds the last reason (an out-of-range one is unclassified)");
    const ConvergeOffCounts c = w.take();
    Check(c.polls == 7 && c.live == 3 && c.off() == 4, "polls, live and off are counted");
    Check(c.why[static_cast<size_t>(ConvergeOff::TraceStale)] == 2 &&
              c.why[static_cast<size_t>(ConvergeOff::SeatUnnamed)] == 1 &&
              c.why[static_cast<size_t>(ConvergeOff::None)] == 1,
          "each off poll is counted under its first reason only");
    Check(c.offEdges == 2 && c.onEdges == 1, "edges: on->off twice, off->on once");
    const ConvergeOffCounts again = w.take();
    Check(again.polls == 0 && again.offEdges == 0 && again.why[static_cast<size_t>(ConvergeOff::TraceStale)] == 0,
          "taking resets the counts");
    Check(ConvergeStateIsOff(w.current()), "...but not the state the edge log reads");

    // THE REGRESSION GUARD at the witness: a seated poll beset by every natural cause, with our code healthy, is
    // claimed -- and so noted LIVE. Natural events go to their own witness and cannot become a ConvergeOff.
    ConvergeOffWitness seat;
    NaturalWitness natural;
    for (unsigned bits = 0; bits < 64; ++bits)
    {
        NaturalEvents e;
        e.latchHeld = (bits & 1u) != 0;
        e.outcomeMiss = (bits & 2u) != 0;
        e.stall = (bits & 4u) != 0;
        e.turretSlew = (bits & 8u) != 0;
        e.reachLimited = (bits & 16u) != 0;
        e.yawShortH4 = (bits & 32u) != 0;
        natural.note(e);
        seat.note(Claims(true, true, AimFeedbackFinite(1.0f, 0.0f, 0.0f)), ConvergeOff::None);
    }
    const ConvergeOffCounts seated = seat.take();
    Check(seated.polls == 64 && seated.live == 64 && seated.offEdges == 0,
          "natural causes leave every seated poll converged (no switch-off)");
    Check(natural.take().polls == 64, "...and are witnessed on their own line");
}

void TestConvergeEdgeLog()
{
    ConvergeEdgeLog log;
    uint8_t previous = 0;
    Check(!log.observe(kConvergeStateNone, 1000, previous), "nothing noted: no line");
    Check(!log.observe(kConvergeStateLive, 1000, previous), "first seen live: quiet");
    Check(!log.observe(kConvergeStateLive, 1050, previous), "unchanged: no line");
    const uint8_t stale = ConvergeOffState(ConvergeOff::TraceStale);
    Check(log.observe(stale, 1100, previous) && previous == kConvergeStateLive, "live -> off: a line");
    Check(log.observe(kConvergeStateLive, 1150, previous) && ConvergeStateReason(previous) == ConvergeOff::TraceStale,
          "off -> live: a line naming what it was off for");
    {
        ConvergeEdgeLog first;
        Check(first.observe(ConvergeOffState(ConvergeOff::SeatUnnamed), 10, previous) &&
                  previous == kConvergeStateNone,
              "first seen off: a line");
    }
    // A flicker: at most max_lines per window, the rest suppressed but still tracked.
    ConvergeEdgeLog flicker;
    flicker.observe(kConvergeStateLive, 0, previous);
    unsigned lines = 0;
    for (unsigned i = 0; i < 20; ++i)
        lines += flicker.observe(i % 2 ? kConvergeStateLive : stale, 5000 + i * 10, previous) ? 1u : 0u;
    Check(lines == flicker.max_lines, "a flicker logs at most max_lines in one window");
    Check(flicker.take_suppressed() == 20 - flicker.max_lines && flicker.take_suppressed() == 0,
          "the rest are counted as suppressed, once");
    Check(flicker.logged == kConvergeStateLive, "suppressed edges still move the logged state");
    Check(flicker.observe(stale, 5000 + 2000, previous), "a new window logs again");
}

void TestReachConvergeOffReason()
{
    ConvergeOff why = ConvergeOff::None;
    ReachConvergePoll p;
    p.live = true;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::None, "live: counted, no reason");
    p = {};
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::AimOff, "direct drive 3 or converged aim off");
    p.aimOn = true;
    p.aimWhy = 0x40;   // a cinematic
    Check(!ReachConvergeOffReason(p, why), "non-gameplay states are not counted");
    p.aimWhy = 0x04;   // our sampler's stamp went stale over a hitch
    Check(!ReachConvergeOffReason(p, why), "a stale sampler stamp is not counted either");
    p.aimWhy = 0x80;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::SeatViewNotOwned, "seat camera not up");
    p.aimWhy = 0x100;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::SeatSteering, "the stick steers this seat");
    p.aimWhy = 0x200;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::AimOff, "reach_vehicle_aim leaves it on the servo");
    p.aimWhy = 0x400;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::SeatProofPending,
          "the seat's frame did not read: the seat is on the servo, its proof pending");
    p.aimWhy = 0x200 | 0x400;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::AimOff, "...after reach_vehicle_aim, in order");
    p.aimWhy = 0;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::DriveNotArmed, "the hook is not armed");
    p.driveReady = true;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::NoTraceFlags, "the trace's flags did not resolve");
    p.flagsResolved = true;
    p.traceFaulted = true;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::TraceFaulted, "the trace faulted");
    p.traceFaulted = false;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::TraceStale, "no fresh ray or trace");
    p.sightSample = true;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::NoRange, "no range yet");
    p.rangeKnown = true;
    Check(ReachConvergeOffReason(p, why) && why == ConvergeOff::PitchRail, "a range and still declined: the rail");
    // Reach's seat limit is not among the inputs: a held update stays converged (ReachStampUpdate).
    Check(ReachStampUpdate(true, true, true).converged, "a seat limit is not a converge-off reason");
}
} // namespace

int main()
{
    TestClaimIgnoresNaturalCauses();
    TestAimFeedback();
    TestAssessment();
    TestStallWitness();
    TestReachStamps();
    TestSeatMuzzleRefusal();
    TestSeatAssistReaim();
    TestNames();
    TestConvergeOffWitness();
    TestConvergeEdgeLog();
    TestReachConvergeOffReason();
    std::printf("Converge policy: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
