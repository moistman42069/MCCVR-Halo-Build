// Seat limit reticle (src/common/seat_limit_reticle_logic.h): the stops a seat's latch or record publishes, the
// per-axis clamp of a world aim in the record's frame and back, the hysteresis and hold that keep the reticle from
// flickering at the edge, and the direction-only turn.
//
// MUTATION-CHECKED (tools/mutate_seat_limit_reticle.py, each mutation applied to a copy of the header
// and compiled against this file): AddLatch's side test inverted; the yaw wrap dropped (plain difference); the
// nearer-stop choice inverted; Clamp skipping the frame (R^-1 / R) in case b; the no-clamp path forced through the
// round trip; the gate's release test made the engage test (no hysteresis); the gate engaging at >= 0; the hold kept
// while not clamped; the hold kept past hold_ms; a new seat key not resetting; TurnVrByClamp dropping the yaw sign.
// Every one of them fails at least one check below.
#include "seat_limit_reticle_logic.h"

#include <cmath>
#include <cstdio>

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

using namespace seat_limit;
constexpr float kDeg = 0.017453292519943295f;
constexpr float kTwoPi = 6.28318530717958647692f;

bool Near(float a, float b, float tolerance = 1e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

bool NearAngle(float a, float b, float tolerance = 1e-4f)
{
    return std::fabs(std::remainder(a - b, kTwoPi)) <= tolerance;
}

void TestAddLatch()
{
    aim_write::LimitLatch latch{};
    latch.latched[0] = true;
    latch.limit[0] = 1.0f;
    latch.side[0] = 1.0f;     // a maximum
    latch.latched[1] = true;
    latch.limit[1] = -0.3f;
    latch.side[1] = -1.0f;    // a minimum
    const bool both[2]{true, true};
    Stops s{};
    AddLatch(s, latch, both);
    Check(s.axis[0].hasMax && !s.axis[0].hasMin && Near(s.axis[0].max, 1.0f), "latch side +1 is a yaw maximum");
    Check(s.axis[1].hasMin && !s.axis[1].hasMax && Near(s.axis[1].min, -0.3f), "latch side -1 is a pitch minimum");
    Check(s.any(), "latched stops are stops");

    Stops none{};
    const bool yawOnly[2]{true, false};
    AddLatch(none, latch, yawOnly);
    Check(none.axis[0].any() && !none.axis[1].any(), "an axis the latch did not read adds no stop");

    aim_write::LimitLatch idle{};
    Stops empty{};
    AddLatch(empty, idle, both);
    Check(!empty.any(), "an unlatched latch adds no stop");

    aim_write::LimitLatch bad{};
    bad.latched[0] = true;
    bad.limit[0] = std::nanf("");
    bad.side[0] = 1.0f;
    Stops nan{};
    AddLatch(nan, bad, both);
    Check(!nan.any(), "a non-finite latch limit adds no stop");
}

void TestAddInterval()
{
    Stops s{};
    AddInterval(s, 1, -0.4f, 0.6f);
    Check(s.axis[1].hasMin && s.axis[1].hasMax && Near(s.axis[1].min, -0.4f) && Near(s.axis[1].max, 0.6f),
          "an interval sets both stops");
    Stops bad{};
    AddInterval(bad, 1, 0.6f, -0.4f);
    AddInterval(bad, 1, std::nanf(""), 0.2f);
    AddInterval(bad, 2, -1.0f, 1.0f);
    Check(!bad.any(), "an unordered, non-finite or out-of-range interval is ignored");
}

void TestClampAxis()
{
    AxisStops pitchMax{};
    pitchMax.hasMax = true;
    pitchMax.max = 0.5f;
    float edge = -1.0f;
    Check(Near(ClampAxis(false, pitchMax, 0.8f, edge), 0.5f) && Near(edge, 0.3f), "past a maximum: held at it");
    Check(Near(ClampAxis(false, pitchMax, 0.2f, edge), 0.2f) && edge == 0.0f, "inside a maximum: the target");

    AxisStops pitchMin{};
    pitchMin.hasMin = true;
    pitchMin.min = -0.4f;
    Check(Near(ClampAxis(false, pitchMin, -0.9f, edge), -0.4f) && Near(edge, 0.5f), "past a minimum: held at it");

    AxisStops both{};
    both.hasMin = both.hasMax = true;
    both.min = -0.4f;
    both.max = 0.6f;
    Check(Near(ClampAxis(false, both, 0.7f, edge), 0.6f), "an interval: the maximum side");
    Check(Near(ClampAxis(false, both, -0.5f, edge), -0.4f), "an interval: the minimum side");

    // Yaw wraps: a maximum at 0.1 rad.
    AxisStops yawMax{};
    yawMax.hasMax = true;
    yawMax.max = 0.1f;
    Check(Near(ClampAxis(true, yawMax, 6.2f, edge), 6.2f) && edge == 0.0f,
          "yaw 6.2 is -0.08 rad: inside a maximum of 0.1 (wrapped)");
    Check(Near(ClampAxis(true, yawMax, 0.3f, edge), 0.1f) && Near(edge, 0.2f), "yaw past a maximum");
    // ...and a maximum just under the seam with the target just over it.
    AxisStops seam{};
    seam.hasMax = true;
    seam.max = 6.2f;
    Check(Near(ClampAxis(true, seam, 0.05f, edge), 6.2f) && Near(edge, 0.05f + kTwoPi - 6.2f),
          "yaw past a maximum across the 2pi seam");

    // A yaw cone given as two stops: outside it, the NEARER stop wins (a cone about yaw 0, +-90 deg, target 100 deg).
    AxisStops cone{};
    cone.hasMin = cone.hasMax = true;
    cone.min = -90.0f * kDeg;
    cone.max = 90.0f * kDeg;
    Check(NearAngle(ClampAxis(true, cone, 100.0f * kDeg, edge), 90.0f * kDeg) && Near(edge, 10.0f * kDeg),
          "outside a cone: the nearer stop (the maximum)");
    Check(NearAngle(ClampAxis(true, cone, -100.0f * kDeg, edge), -90.0f * kDeg) && Near(edge, 10.0f * kDeg),
          "outside a cone: the nearer stop (the minimum)");
}

void TestClampWorld()
{
    Stops s{};
    s.axis[0].hasMax = true;
    s.axis[0].max = 0.5f;
    Result r{};
    Check(Clamp(s, 0.8f, 0.2f, 0.0f, r), "world clamp resolves");
    Check(r.clamped[0] && !r.clamped[1] && Near(r.worldYaw, 0.5f) && Near(r.worldPitch, 0.2f),
          "world yaw held at its stop, pitch untouched");
    Check(Near(r.edgeDeg[0], 0.3f / kDeg, 1e-2f), "the yaw edge in degrees");
    // The overshoot is the angle between the two directions: 0.3 rad of yaw at pitch 0.2 is 0.3 * cos(0.2) nearly.
    Check(Near(r.overshootDeg, 0.3f * std::cos(0.2f) / kDeg, 0.05f), "the overshoot is the angle between the aims");
    float expect[3]{};
    seat_frame::Forward(0.5f, 0.2f, expect);
    Check(Near(r.dir[0], expect[0]) && Near(r.dir[1], expect[1]) && Near(r.dir[2], expect[2]),
          "the clamped direction is e(stop, pitch)");

    Result inside{};
    Check(Clamp(s, 0.3f, -0.1f, 0.0f, inside) && !inside.any() && inside.overshootDeg == 0.0f &&
              inside.worldYaw == 0.3f && inside.worldPitch == -0.1f,
          "inside every stop: the target exactly, overshoot exactly 0");

    Result nan{};
    Check(!Clamp(s, std::nanf(""), 0.0f, 0.0f, nan), "a non-finite target does not resolve");
}

void TestClampRebased()
{
    // Case b: a vehicle rolled 30 deg about world +X. A passenger's yaw stop at record yaw 0.6 rad.
    const float up[3]{0.0f, -std::sin(30.0f * kDeg), std::cos(30.0f * kDeg)};
    Stops s{};
    s.rebased = true;
    Check(seat_frame::BuildFrame(up, s.frame), "rolled frame builds");
    s.axis[0].hasMax = true;
    s.axis[0].max = 0.6f;
    // The world aim the record yaw 1.0, pitch 0.1 makes, through R: past the stop in the record frame.
    seat_frame::Angles world{};
    Check(seat_frame::WorldFromRecord(s.frame, 1.0f, 0.1f, world), "world of a record aim");
    Result r{};
    Check(Clamp(s, world.yaw, world.pitch, 1.0f, r) && r.clamped[0] && !r.clamped[1], "rebased: yaw clamps");
    // Back in the record frame the clamped aim sits exactly on the stop, with the record pitch kept.
    seat_frame::Angles record{};
    Check(seat_frame::RecordFromWorld(s.frame, r.worldYaw, r.worldPitch, 0.0f, record), "record of the clamp");
    Check(NearAngle(record.yaw, 0.6f, 2e-4f) && Near(record.pitch, 0.1f, 2e-4f),
          "rebased: the clamp is the stop in the RECORD frame, not in world angles");
    // ...which is NOT world yaw 0.6: the frame matters.
    Check(!NearAngle(r.worldYaw, 0.6f, 1e-2f) || !Near(r.worldPitch, 0.1f, 1e-2f),
          "rebased: the world aim differs from the record angles under a roll");
    // Inside: exactly the target.
    seat_frame::Angles insideWorld{};
    seat_frame::WorldFromRecord(s.frame, 0.2f, 0.1f, insideWorld);
    Result in{};
    Check(Clamp(s, insideWorld.yaw, insideWorld.pitch, 0.2f, in) && !in.any() && in.overshootDeg == 0.0f &&
              in.worldYaw == insideWorld.yaw,
          "rebased, inside: the target exactly");
}

void TestLatchEndToEnd()
{
    // The game clamps a turret's pitch at a 0.5 rad stop. Direct drive writes 0.8 every update; the latch reads the
    // clamp off the record and the reticle's stops come from it.
    aim_write::LimitLatch latch{};
    const bool axes[2]{false, true};
    float native[2]{0.0f, 0.0f}, target[2]{0.0f, 0.8f}, out[2]{};
    latch.apply(native, target, out, axes);
    latch.wrote(out, axes);
    native[1] = 0.5f;   // the game's update moved our write back to the stop
    latch.apply(native, target, out, axes);
    Stops s{};
    AddLatch(s, latch, axes);
    Check(s.axis[1].hasMax && Near(s.axis[1].max, 0.5f), "the latch's stop is where the game clamped");
    Result r{};
    Check(Clamp(s, 0.0f, 0.8f, 0.0f, r) && r.clamped[1] && Near(r.worldPitch, 0.5f),
          "the reticle sits on the stop the game holds the gun at");
    // The hand comes back inside: the latch lets go and publishes no stop.
    latch.wrote(out, axes);
    const float back[2]{0.0f, 0.3f};
    native[1] = out[1];
    latch.apply(native, back, out, axes);
    Stops released{};
    AddLatch(released, latch, axes);
    Check(!released.any(), "back inside: the latch publishes no stop");
}

void TestGate()
{
    Gate g{};
    Stops s{};
    s.axis[1].hasMax = true;
    s.axis[1].max = 0.5f;
    Stops use{};
    Check(g.stops_for(true, s, 7, 1000, use) && use.axis[1].hasMax, "fresh stops are used");
    Check(!g.decide(true, 0.4f), "under the engage threshold: not clamped");
    Check(g.decide(true, 0.6f), "past the engage threshold: clamped");
    Check(g.decide(true, 0.2f), "hysteresis: stays clamped while still past the stop");
    Check(!g.decide(true, 0.0f), "back inside: released");
    Check(!g.decide(true, 0.3f), "released: a small overshoot does not re-clamp");
    Check(g.decide(true, 0.51f), "re-clamps past the threshold");
    Check(!g.decide(false, 2.0f), "no stops this poll: not clamped");
    Check(!g.decide(true, std::nanf("")), "a non-finite overshoot: not clamped");

    // The hold: while clamped, a gap in the publications keeps the last stops for hold_ms.
    Gate h{};
    h.stops_for(true, s, 7, 1000, use);
    h.decide(true, 3.0f);
    Check(h.stops_for(false, Stops{}, 7, 1100, use) && use.axis[1].hasMax, "clamped: the last stops held 100 ms");
    Check(!h.stops_for(false, Stops{}, 7, 1200, use), "clamped: dropped past hold_ms");
    Gate n{};
    n.stops_for(true, s, 7, 1000, use);
    n.decide(true, 0.1f);   // not clamped
    Check(!n.stops_for(false, Stops{}, 7, 1010, use), "not clamped: no hold");
    // A fresh publication with no stop (the latch let go) is no stop at once, clamped or not.
    Gate f{};
    f.stops_for(true, s, 7, 1000, use);
    f.decide(true, 3.0f);
    Check(!f.stops_for(true, Stops{}, 7, 1010, use), "a fresh empty publication: no stop");
    // Another seat forgets the clamp and the held stops.
    Gate k{};
    k.stops_for(true, s, 7, 1000, use);
    k.decide(true, 3.0f);
    Check(!k.stops_for(false, Stops{}, 8, 1010, use) && !k.engaged, "another seat: nothing carries over");
    // A clock that ran backwards drops the hold.
    Gate c{};
    c.stops_for(true, s, 7, 1000, use);
    c.decide(true, 3.0f);
    Check(!c.stops_for(false, Stops{}, 7, 900, use), "a clock that went backwards drops the hold");
}

void TestTurnVr()
{
    Result r{};
    r.worldYaw = 1.0f;
    r.worldPitch = 0.3f;
    float vrYaw = 0.2f, vrPitch = 0.1f;
    // Target yaw 1.2, pitch 0.5: the clamp turned the aim by -0.2 yaw, -0.2 pitch; the VR yaw sign is -1.
    Check(TurnVrByClamp(1.2f, 0.5f, r, -1.0f, 1.0f, vrYaw, vrPitch), "turn resolves");
    Check(Near(vrYaw, 0.4f) && Near(vrPitch, -0.1f), "the VR reticle turns by the clamp's delta with the signs");
    // Across the seam the delta is the short way round.
    Result seam{};
    seam.worldYaw = 6.25f;
    seam.worldPitch = 0.0f;
    float y = 0.0f, p = 0.0f;
    TurnVrByClamp(0.05f, 0.0f, seam, 1.0f, 1.0f, y, p);
    Check(Near(y, 6.25f - kTwoPi - 0.05f), "the yaw delta wraps the short way");
    float bad = std::nanf(""), ok = 0.0f;
    Check(!TurnVrByClamp(0.0f, 0.0f, r, 1.0f, 1.0f, bad, ok), "a non-finite reticle does not turn");
}

void TestTally()
{
    Tally t{};
    Result a{};
    a.clamped[0] = true;
    a.overshootDeg = 3.0f;
    Result b{};
    b.clamped[1] = true;
    b.overshootDeg = 7.5f;
    t.note(a);
    t.note(b);
    Check(t.polls == 2 && t.yawEdge == 1 && t.pitchEdge == 1 && Near(t.maxOvershootDeg, 7.5f), "the tally");
}

// The record's eased pitch limits (Halo 3 / ODST): the offsets decoded from the accumulator's clamp.
void TestResolvePitchLimits()
{
    const char* why = nullptr;
    PitchLimitSite h3{0x190, 0x194, 0x194, 0xD8, 0x190, 0xD8};
    Check(ResolvePitchLimits(h3, 0xD4, 0x110, why) == 0x190 && !why, "Halo 3: LO +0x190, HI +0x194");
    PitchLimitSite odst{0x344, 0x348, 0x348, 0xD8, 0x344, 0xD8};
    Check(ResolvePitchLimits(odst, 0xD4, 0x2C0, why) == 0x344 && !why, "ODST: LO +0x344, HI +0x348");
    PitchLimitSite s = h3;
    s.pitchAdd = 0xD4;
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why) && why, "a clamp of the yaw is not the pitch's");
    s = h3;
    s.pitchStore = 0xDC;
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why), "a store elsewhere is not the record's pitch");
    s = h3;
    s.loMax = 0x198;
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why), "the clamp must bound by the low it eased");
    s = h3;
    s.hiStore = 0x198;
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why), "the high must be read and written at one place");
    s = h3;
    s.hiLoad = s.hiStore = 0x19C;
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why), "the limits must be adjacent");
    Check(ResolvePitchLimits(h3, 0xD4, 0xC4, why) == 0x190, "the angles-to-high span fits the stride exactly");
    Check(!ResolvePitchLimits(h3, 0xD4, 0xC0, why), "the limits must lie inside one user's record");
    s = h3;
    s.loStore = s.loMax = 0x191;
    s.hiLoad = s.hiStore = 0x195;
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why), "the limits must be float-aligned");
    s = PitchLimitSite{0xD0, 0xD4, 0xD4, 0xD8, 0xD0, 0xD8};
    Check(!ResolvePitchLimits(s, 0xD4, 0x110, why), "the limits must lie past the angles");
}

void TestRecordPitchInterval()
{
    Check(RecordPitchInterval(-0.33f, 0.61f), "a seat's range is an interval");
    Check(RecordPitchInterval(0.2f, 0.2f), "a closed range (both limits met) is one");
    Check(!RecordPitchInterval(0.3f, 0.2f), "an unordered pair is none");
    Check(!RecordPitchInterval(std::nanf(""), 0.2f) && !RecordPitchInterval(-0.2f, INFINITY), "non-finite is none");
    Check(!RecordPitchInterval(-1.7f, 0.2f) && !RecordPitchInterval(-0.2f, 1.7f), "past the pole is not a pitch");
    // End to end: a look-steered driver's hand 30 deg below the eased low (-25 deg) holds at the low.
    Stops s{};
    AddInterval(s, 1, -25.0f * kDeg, 35.0f * kDeg);
    Result r{};
    Check(Clamp(s, 1.0f, -55.0f * kDeg, 1.0f, r) && r.clamped[1] && !r.clamped[0] && Near(r.worldPitch, -25.0f * kDeg) &&
              NearAngle(r.worldYaw, 1.0f) && Near(r.overshootDeg, 30.0f, 1e-2f),
          "the eased low holds the reticle's pitch, the yaw untouched");
}
} // namespace

int main()
{
    TestAddLatch();
    TestAddInterval();
    TestClampAxis();
    TestClampWorld();
    TestClampRebased();
    TestLatchEndToEnd();
    TestGate();
    TestTurnVr();
    TestTally();
    TestResolvePitchLimits();
    TestRecordPitchInterval();
    std::printf("seat_limit_reticle_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
