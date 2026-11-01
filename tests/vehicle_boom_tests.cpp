// The third-person boom (src/common/vehicle_boom_logic.h): the game's camera track evaluated as Halo 3 evaluates
// it, the flat game's look range, the boom's shape across that range, its angle from the stick, the inversion and
// where it starts.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include "../src/common/vehicle_boom_logic.h"

using namespace vehicle_boom;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool Near(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

constexpr float kDeg = kRadiansPerDegree;
constexpr float kHalfTurn = 3.14159265358979323846f;

// A track like a vehicle seat's: thirteen points from looking straight down to straight up, each looking at its
// own pitch, standing on an orbit of 1.5 world units about a point half a world unit above the pivot.
static float RaisedOrbit(float pitch, int axis)
{
    const float place[3]{-1.5f * std::cos(pitch), 0.0f, -1.5f * std::sin(pitch) + 0.5f};
    return place[axis];
}

static Track GameLikeTrack(int count = 13)
{
    Track track;
    track.count = count;
    for (int i = 0; i < count; ++i)
    {
        const float pitch = -kHalfTurn / 2.0f + kHalfTurn * static_cast<float>(i) / static_cast<float>(count - 1);
        TrackPoint& point = track.points[i];
        for (int k = 0; k < 3; ++k)
            point.position[k] = RaisedOrbit(pitch, k);
        // Turned about the right axis by the pitch: (1, 0, 0) is carried to (cos, 0, sin).
        point.orientation[0] = 0.0f;
        point.orientation[1] = -std::sin(pitch / 2.0f);
        point.orientation[2] = 0.0f;
        point.orientation[3] = std::cos(pitch / 2.0f);
    }
    return track;
}

static void TestCubic()
{
    // Any cubic is given back exactly by four of its own points.
    const auto cubic = [](float t) { return 1.0f + 2.0f * t - 3.0f * t * t + 0.5f * t * t * t; };
    const float t0 = 0.2f, h = 0.1f;
    for (const float t : {0.2f, 0.25f, 0.3f, 0.37f, 0.4f, 0.5f, 0.1f, 0.6f})
        Check(Near(UniformCubic(cubic(t0), cubic(t0 + h), cubic(t0 + 2 * h), cubic(t0 + 3 * h), t0, h, t), cubic(t), 2.0e-5f),
              "the cubic through four points is that cubic, between and beyond them");
}

static void TestRotate()
{
    const float ahead[3]{1.0f, 0.0f, 0.0f};
    float out[3];
    const float identity[4]{0.0f, 0.0f, 0.0f, 1.0f};
    Rotate(identity, ahead, out);
    Check(Near(out[0], 1.0f) && Near(out[1], 0.0f) && Near(out[2], 0.0f), "no turn keeps the vector");
    const float s = std::sin(kHalfTurn / 4.0f), c = std::cos(kHalfTurn / 4.0f);
    const float left[4]{0.0f, 0.0f, s, c}; // a quarter turn about up
    Rotate(left, ahead, out);
    Check(Near(out[0], 0.0f) && Near(out[1], 1.0f) && Near(out[2], 0.0f), "a quarter turn about up looks left");
    const float raise[4]{0.0f, -s, 0.0f, c}; // a quarter turn about right
    Rotate(raise, ahead, out);
    Check(Near(out[0], 0.0f) && Near(out[1], 0.0f) && Near(out[2], 1.0f), "a quarter turn about right looks up");
}

static void TestTrackWindow()
{
    // Positions that are no cubic, so the four points the game picks show in the answer.
    Track track = GameLikeTrack(8);
    for (int i = 0; i < track.count; ++i)
    {
        track.points[i].position[0] = static_cast<float>(i * i * i * i) * 0.01f;
        track.points[i].position[1] = 0.0f;
        track.points[i].position[2] = 0.0f;
    }
    const float h = 1.0f / 7.0f;
    float place[3];
    for (int i = 0; i < track.count; ++i)
        Check(TrackAt(track, static_cast<float>(i) * h, place, nullptr) &&
                  Near(place[0], track.points[i].position[0], 1.0e-3f),
              "the track passes through each of its points");
    // Halfway through frame 2 the window is points 1..4: one behind the frame, two ahead.
    const float t = 2.5f * h;
    const float* p = &track.points[1].position[0];
    const float want = UniformCubic(p[0], track.points[2].position[0], track.points[3].position[0],
                                    track.points[4].position[0], h, h, t);
    Check(TrackAt(track, t, place, nullptr) && Near(place[0], want), "a frame's window starts one point behind it");
    // In the last frame the window is the last four points.
    const float last = 6.5f * h;
    const float wantLast = UniformCubic(track.points[4].position[0], track.points[5].position[0],
                                        track.points[6].position[0], track.points[7].position[0], 4.0f * h, h, last);
    Check(TrackAt(track, last, place, nullptr) && Near(place[0], wantLast, 1.0e-3f), "and never runs past the end");

    Track short3 = GameLikeTrack(4);
    short3.count = 3;
    Check(!TrackAt(short3, 0.5f, place, nullptr), "fewer than four points: no track");
    Check(!Usable(short3), "and not usable");
    Track crooked = GameLikeTrack();
    crooked.points[5].orientation[3] = 3.0f;
    Check(!Usable(crooked), "an orientation that is not a turn: not usable");
    Track far = GameLikeTrack();
    far.points[2].position[2] = std::numeric_limits<float>::quiet_NaN();
    Check(!Usable(far), "a position that is not a number: not usable");
}

static void TestTrackPitch()
{
    const Track track = GameLikeTrack();
    for (const float degrees : {-60.0f, -30.0f, -5.0f, 0.0f, 12.0f, 45.0f, 80.0f})
    {
        const float pitch = degrees * kDeg;
        const float t = TrackParameterForPitch(track, pitch);
        float forward[3];
        Check(TrackAt(track, t, nullptr, forward), "the solved parameter is on the track");
        const float along = std::atan2(forward[2], std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]));
        Check(Near(along, pitch, 0.002f), "the track looks at the pitch it was solved for");
        float place[3];
        Check(TrackOffset(track, pitch, place), "the camera's place at that pitch");
        for (int k = 0; k < 3; ++k)
            Check(Near(place[k], RaisedOrbit(pitch, k), 0.01f), "is where the track's points put it");
    }
    float place[3];
    Check(TrackOffset(track, 0.0f, place) && place[0] < -1.4f && place[2] > 0.45f,
          "looking level, the game's camera stands behind and above the pivot");
    float down[3], up[3];
    TrackOffset(track, -40.0f * kDeg, down);
    TrackOffset(track, 40.0f * kDeg, up);
    Check(down[2] > place[2] && up[2] < place[2], "looking down raises it, looking up lowers it");
}

static void TestLookRange()
{
    Range range = LookRange(0.0f, 0.0f);
    Check(Near(range.min, -kFlatLookLimit) && Near(range.max, kFlatLookLimit), "a seat camera with no range: +-85.5 deg");
    range = LookRange(-0.5f, 1.0f);
    Check(Near(range.min, -0.5f) && Near(range.max, 1.0f), "the seat camera's own range");
    range = LookRange(0.0f, 0.7f);
    Check(Near(range.min, 0.0f) && Near(range.max, 0.7f), "either end set is a range");
    range = LookRange(-2.0f, 2.0f);
    Check(Near(range.min, -kFlatLookLimit) && Near(range.max, kFlatLookLimit), "each end held to +-85.5 deg");
    range = LookRange(-2.0f, 2.0f, -1.0f, 1.2f);
    Check(Near(range.min, -1.0f) && Near(range.max, 1.2f), "held to the flat limits the game holds it to");
    range = LookRange(0.9f, -0.3f);
    Check(Near(range.min, -0.3f) && Near(range.max, 0.9f), "a range written backwards is still that range");
    range = LookRange(std::numeric_limits<float>::quiet_NaN(), 0.5f, 1.0f, -1.0f);
    Check(Near(range.min, -kFlatLookLimit) && Near(range.max, kFlatLookLimit), "nothing usable: the flat game's own");
    Check(Near(kFlatLookLimit, 85.5f * kDeg), "the flat game's own limit is 85.5 deg");
}

static void TestShapes()
{
    const Track track = GameLikeTrack();
    const Range range = LookRange(-60.0f * kDeg, 70.0f * kDeg);
    Shape shape;
    Check(BuildTrackShape(track, range, shape) && shape.valid && shape.track, "a shape from the game's track");
    for (int i = 0; i <= 40; ++i)
    {
        const float pitch = range.min + (range.max - range.min) * static_cast<float>(i) / 40.0f;
        float table[3], exact[3];
        OffsetAt(shape, pitch, table);
        TrackOffset(track, pitch, exact);
        for (int k = 0; k < 3; ++k)
            Check(Near(table[k], exact[k], 0.002f), "a frame's lookup is the track's place within a millimetre or so");
        Check(Near(PitchForElevation(shape, ElevationAt(shape, pitch)), pitch, 0.003f),
              "and the pitch for that elevation is that pitch");
    }
    float held[3], end[3];
    OffsetAt(shape, 2.0f, held);
    OffsetAt(shape, range.max, end);
    Check(Near(held[0], end[0]) && Near(held[2], end[2]), "past the range is the end of the range");
    Check(Near(PitchForElevation(shape, 89.0f * kDeg), range.min, 1.0e-4f),
          "an elevation it cannot reach: the end that comes nearest");
    float distance[3];
    Offset(shape, 0.3f, 5.0f, distance);
    OffsetAt(shape, 0.3f, held);
    Check(Near(distance[0], held[0]) && Near(distance[2], held[2]), "a track's place has its own distance");

    Track broken = track;
    broken.count = 2;
    Check(!BuildTrackShape(broken, range, shape) && !shape.valid, "no track, no shape from it");

    Shape orbit;
    BuildOrbitShape(range, orbit);
    Check(orbit.valid && !orbit.track, "an orbit stands in");
    for (const float degrees : {-50.0f, -10.0f, 0.0f, 25.0f, 60.0f})
    {
        const float pitch = degrees * kDeg;
        Check(Near(ElevationAt(orbit, pitch), -pitch, 1.0e-3f), "an orbit's elevation is minus the pitch");
        Check(Near(PitchForElevation(orbit, -pitch), pitch, 1.0e-3f), "and back");
    }
    Offset(orbit, 0.0f, 2.0f, distance);
    Check(Near(distance[0], -2.0f, 1.0e-3f) && Near(distance[2], 0.0f, 1.0e-3f), "at the distance it is given");
    Offset(orbit, -30.0f * kDeg, 2.0f, distance);
    Check(Near(distance[2], 1.0f, 1.0e-3f) && Near(distance[0], -2.0f * std::cos(30.0f * kDeg), 1.0e-3f),
          "looking 30 deg down it stands half its distance up");
    Offset(orbit, 0.0f, std::numeric_limits<float>::quiet_NaN(), distance);
    Check(distance[0] == 0.0f && distance[2] == 0.0f, "no distance, no place");
}

static void TestStick()
{
    Check(StickAxis(0.1f) == 0.0f && StickAxis(-0.15f) == 0.0f, "inside the deadzone: nothing");
    Check(Near(StickAxis(1.0f), 1.0f) && Near(StickAxis(-1.0f), -1.0f), "all the way: all of it");
    Check(Near(StickAxis(0.575f), 0.5f) && Near(StickAxis(-0.575f), -0.5f), "past the deadzone it starts from nothing");
    Check(StickAxis(std::numeric_limits<float>::quiet_NaN()) == 0.0f, "not a number: nothing");

    const Range range = LookRange(-30.0f * kDeg, 45.0f * kDeg);
    Check(Near(Step(0.0f, 1.0f, 0.05f, 60.0f, false, range), 3.0f * kDeg), "a full push for a twentieth of a second at 60 deg/s");
    Check(Near(Step(0.0f, 1.0f, 0.05f, 60.0f, true, range), -3.0f * kDeg), "inverted, the other way");
    Check(Near(Step(0.0f, -1.0f, 0.05f, 60.0f, false, range), -3.0f * kDeg), "pulling back lowers the look pitch");
    Check(Near(Step(0.0f, 1.0f, 2.0f, 60.0f, false, range), 6.0f * kDeg), "a long frame moves it as a tenth of a second");
    Check(Near(Step(0.0f, 0.1f, 0.05f, 60.0f, false, range), 0.0f), "a rest on the stick moves nothing");
    Check(Near(Step(44.0f * kDeg, 1.0f, 0.1f, 60.0f, false, range), 45.0f * kDeg), "held at the top of the range");
    Check(Near(Step(-29.0f * kDeg, -1.0f, 0.1f, 60.0f, false, range), -30.0f * kDeg), "and at the bottom");
    Check(Near(Step(80.0f * kDeg, 0.0f, 0.05f, 60.0f, false, range), 45.0f * kDeg), "a pitch past the range is put back in it");
    Check(Near(Step(0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN(), 60.0f, false, range), 0.0f),
          "no time, no movement");
    Check(Near(Step(0.0f, 1.0f, 0.05f, 1.0e9f, false, range), kMaxRateDegPerSecond * 0.05f * kDeg),
          "the rate is held to its own limit");
    Check(Near(Step(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.05f, 60.0f, false, range), 0.0f),
          "a lost pitch is level again");
}

static void TestInversion()
{
    Check(!GameInverts(false, true, false) && GameInverts(true, false, false), "a seat takes the look inversion");
    Check(GameInverts(false, true, true) && !GameInverts(true, false, true), "an aircraft's pilot the flight inversion");
    Check(!Inverted(0, true, true) && Inverted(1, false, false), "0 is never, 1 is always");
    Check(Inverted(-1, true, true) && !Inverted(-1, true, false), "-1 follows the game");
    Check(!Inverted(-1, false, true), "and is not inverted when the game's setting could not be read");
}

static void TestDefault()
{
    Check(Near(kMeasuredLevelElevationDeg, std::asin(151.0f / 474.0f) / kDeg, 0.05f),
          "the measured level place: 151 cm up of 474 cm from the pivot");
    Shape orbit;
    BuildOrbitShape(Range{}, orbit);
    Check(Near(DefaultElevation(orbit, 25.0f), 25.0f * kDeg), "the camera file's boomPitch");
    Check(Near(DefaultElevation(orbit, kBoomPitchUnset), (kMeasuredLevelElevationDeg + kDefaultRaiseDeg) * kDeg),
          "left out, without the track: the measured level place, a little higher");
    Check(Near(DefaultElevation(orbit, 120.0f), (kMeasuredLevelElevationDeg + kDefaultRaiseDeg) * kDeg),
          "a boomPitch past straight up is left out");
    Shape shape;
    BuildTrackShape(GameLikeTrack(), LookRange(0.0f, 0.0f), shape);
    Check(Near(DefaultElevation(shape, kBoomPitchUnset), ElevationAt(shape, 0.0f) + kDefaultRaiseDeg * kDeg),
          "left out, with the track: where the game's camera stands looking level, a little higher");
    const float start = PitchForElevation(shape, DefaultElevation(shape, kBoomPitchUnset));
    Check(start < 0.0f && start > -15.0f * kDeg, "which is the game's camera looking a little down");
    Check(BoomPitchSet(-89.0f) && BoomPitchSet(0.0f) && !BoomPitchSet(kBoomPitchUnset) &&
              !BoomPitchSet(std::numeric_limits<float>::quiet_NaN()),
          "what counts as a boomPitch");
}

int main()
{
    TestCubic();
    TestRotate();
    TestTrackWindow();
    TestTrackPitch();
    TestLookRange();
    TestShapes();
    TestStick();
    TestInversion();
    TestDefault();
    std::printf("vehicle_boom_tests: %u checks passed\n", g_checks);
    return 0;
}
