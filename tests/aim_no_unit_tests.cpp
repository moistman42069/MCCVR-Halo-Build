// The free camera with no player unit (src/common/aim_no_unit_logic.h): the debounce that decides it, its
// fail-open rules, the release on respawn, and the frame the aim servo composes and actuates while it holds.
#include "aim_no_unit_logic.h"
#include <cstdio>
#include <initializer_list>

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

using aim_no_unit::Debounce;
using aim_no_unit::Edge;
using aim_no_unit::Presence;

// Feeds `presence` as a fresh sample every `stepMs` from `fromMs` through `toMs` inclusive (the consumer reads it
// the moment it lands), returning the first non-None edge seen and where.
Edge Feed(Debounce& d, Presence presence, uint64_t fromMs, uint64_t toMs, uint64_t stepMs, uint64_t* atMs = nullptr)
{
    Edge first = Edge::None;
    for (uint64_t t = fromMs; t <= toMs; t += stepMs)
    {
        const Edge e = d.Step(presence, t, t);
        if (e != Edge::None && first == Edge::None)
        {
            first = e;
            if (atMs)
                *atMs = t;
        }
    }
    return first;
}

void TestPacking()
{
    for (Presence p : {Presence::Unknown, Presence::Present, Presence::Absent})
    {
        const uint64_t word = aim_no_unit::Pack(p, 123456789ull);
        Check(aim_no_unit::UnpackPresence(word) == p, "a packed presence round-trips");
        Check(aim_no_unit::UnpackMs(word) == 123456789ull, "a packed sample time round-trips");
    }
    Check(aim_no_unit::UnpackPresence(0) == Presence::Unknown, "an unwritten word is Unknown");
    Check(aim_no_unit::UnpackPresence(3) == Presence::Unknown, "the spare bit pattern is Unknown, not a verdict");
}

void TestEnterDebounce()
{
    // A death: no unit from t=1000, sampled at ~66 Hz.
    Debounce d;
    Check(Feed(d, Presence::Present, 0, 990, 15) == Edge::None, "alive: nothing happens");
    uint64_t at = 0;
    Check(Feed(d, Presence::Absent, 1000, 1500, 15, &at) == Edge::Entered, "a held absence enters the free camera");
    Check(at >= 1000 + aim_no_unit::kEnterMs && at < 1000 + aim_no_unit::kEnterMs + 15,
          "...as soon as it has held kEnterMs, not before or long after");
    Check(d.freeCamera, "the decision holds after entering");

    // A seat change reads no unit for a frame or two: never a free camera.
    Debounce blip;
    Feed(blip, Presence::Present, 0, 990, 15);
    Check(Feed(blip, Presence::Absent, 1000, 1030, 15) == Edge::None, "a two-frame blip of no unit does not enter");
    Check(Feed(blip, Presence::Present, 1045, 3000, 15) == Edge::None, "and the unit coming back is no edge");
    Check(!blip.freeCamera, "a blip never decides free camera");

    // Absences broken by a unit are separate runs: each must hold kEnterMs on its own.
    Debounce runs;
    Feed(runs, Presence::Absent, 0, 150, 15);
    Feed(runs, Presence::Present, 165, 165, 15);
    Check(Feed(runs, Presence::Absent, 180, 330, 15) == Edge::None,
          "an absence restarted by a unit sample is timed afresh (150 + 150 ms is not 200)");
    Check(!runs.freeCamera, "two short runs do not add up");
}

void TestRepeatIsNoEvidence()
{
    // The sampler stopped advancing but its last word is still fresh: a repeated sample proves nothing new.
    Debounce d;
    Check(d.Step(Presence::Absent, 1000, 1000) == Edge::None, "first absent sample");
    Check(d.Step(Presence::Absent, 1000, 1240) == Edge::None, "the same sample read again 240 ms later does not enter");
    Check(!d.freeCamera, "only newer samples time an absence");
    Check(d.Step(Presence::Absent, 1200, 1240) == Edge::Entered, "a newer sample 200 ms on does");
}

void TestRelease()
{
    Debounce d;
    Feed(d, Presence::Absent, 0, 300, 15);
    Check(d.freeCamera, "in the free camera");

    // A lone unit sample inside the death is not a respawn.
    Check(d.Step(Presence::Present, 315, 315) == Edge::None, "one unit sample does not release");
    Check(Feed(d, Presence::Absent, 330, 600, 15) == Edge::None, "the absence continues: still no edge");
    Check(d.freeCamera, "a lone unit sample leaves the free camera in place");

    // The respawn.
    uint64_t at = 0;
    Check(Feed(d, Presence::Present, 1000, 1200, 15, &at) == Edge::Released, "the unit back releases the free camera");
    Check(at >= 1000 + aim_no_unit::kExitMs && at < 1000 + aim_no_unit::kExitMs + 15,
          "...after kExitMs of it, not before or long after");
    Check(!d.freeCamera, "the ordinary frame resumes");

    // A later death is timed from scratch.
    Check(Feed(d, Presence::Absent, 2000, 2150, 15) == Edge::None, "a new death waits its own kEnterMs");
    Check(Feed(d, Presence::Absent, 2165, 2300, 15) == Edge::Entered, "and then enters");
}

void TestFailOpen()
{
    // Nothing published: never free camera.
    Debounce none;
    for (uint64_t t = 0; t < 2000; t += 15)
        Check(none.Step(Presence::Unknown, 0, t) == Edge::None, "no sample: nothing happens");
    Check(!none.freeCamera, "no sample is not a death");

    // Stale absence: the sampler stopped (a load, a teardown) on a no-unit word.
    Debounce stale;
    stale.Step(Presence::Absent, 1000, 1000);
    Check(stale.Step(Presence::Absent, 1000, 1000 + aim_no_unit::kStaleMs + 1) == Edge::None,
          "a stale absence is no answer");
    Check(!stale.freeCamera && !stale.absentRun, "...and forgets the run it was building");
    Check(stale.Step(Presence::Absent, 1300, 1300) == Edge::None,
          "a fresh absence after a stale gap starts a new run (it does not inherit the old start)");

    // In the free camera, the sampler goes quiet: fail open, loudly (its own edge).
    Debounce quiet;
    Feed(quiet, Presence::Absent, 0, 300, 15);
    Check(quiet.freeCamera, "free camera before the sampler stops");
    Check(quiet.Step(Presence::Absent, 300, 300 + aim_no_unit::kStaleMs) == Edge::None,
          "exactly kStaleMs old is still an answer");
    Check(quiet.Step(Presence::Absent, 300, 300 + aim_no_unit::kStaleMs + 1) == Edge::ReleasedUnknown,
          "older releases as unknown");
    Check(!quiet.freeCamera, "unknown is not a death");

    // An Unknown word (a title publishing nothing, or a cleared slot) releases too.
    Debounce cleared;
    Feed(cleared, Presence::Absent, 0, 300, 15);
    Check(cleared.Step(Presence::Unknown, 315, 315) == Edge::ReleasedUnknown, "an Unknown sample releases");

    // A sample stamped after the consumer read its clock is fresh.
    Debounce race;
    Check(race.Step(Presence::Absent, 1005, 1000) == Edge::None && race.absentRun,
          "a sample a moment ahead of the consumer's clock counts");
}

void TestFrame()
{
    const aim_no_unit::Frame ordinary = aim_no_unit::FrameFor(false);
    Check(ordinary.traceReaim && ordinary.seatReOrigin && ordinary.hullFrame && ordinary.directDrive &&
              ordinary.claimConverged && !ordinary.parkingServo,
          "the ordinary frame is exactly what shipped");
    const aim_no_unit::Frame free = aim_no_unit::FrameFor(true);
    Check(!free.traceReaim, "free camera: no trace bend (its eye is the camera the servo turns)");
    Check(!free.seatReOrigin, "free camera: no seated re-origin (rendered eye minus the engine camera)");
    Check(!free.hullFrame, "free camera: no hull frame (there is no seat)");
    Check(!free.directDrive, "free camera: no direct-drive setpoint (there is no unit to write)");
    Check(!free.claimConverged, "free camera: nothing is claimed converged");
    Check(free.parkingServo, "free camera: the servo parks (rest hysteresis), so it cannot hunt");

    Check(!aim_no_unit::ServoParks(false, false), "on foot, alive: the plain proportional servo");
    Check(aim_no_unit::ServoParks(true, false), "a turret seat parks, as it always has");
    Check(aim_no_unit::ServoParks(false, true), "the free camera parks");
    Check(aim_no_unit::ServoParks(true, true), "both: parks");

    Check(aim_no_unit::TitlePublishes(GameTitle::Halo3) && aim_no_unit::TitlePublishes(GameTitle::Halo3ODST) &&
              aim_no_unit::TitlePublishes(GameTitle::HaloReach) && aim_no_unit::TitlePublishes(GameTitle::Halo4),
          "the four servo titles publish");
    Check(!aim_no_unit::TitlePublishes(GameTitle::Halo2) && !aim_no_unit::TitlePublishes(GameTitle::HaloCE) &&
              !aim_no_unit::TitlePublishes(GameTitle::None),
          "Halo 2, CE and no title do not");
}
} // namespace

int main()
{
    TestPacking();
    TestEnterDebounce();
    TestRepeatIsNoEvidence();
    TestRelease();
    TestFailOpen();
    TestFrame();
    std::printf("aim_no_unit_tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
