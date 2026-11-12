// A look-steered land driver's gun band (src/common/vehicle_gun_band_logic.h): the hull frame, the barrels learned
// from shots (animated barrels refused), the firing assist's cap from the weapon tag, the band, and the clamp of one
// shot's elevation relative to the hull's up (yaw never clamped).
//
// MUTATION-CHECKED (tools/mutate_vehicle_gun_aim.py): every mutation listed there for this header fails
// at least one check below.
#include "vehicle_gun_band_logic.h"

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

using namespace gun_band;
constexpr float kDeg = 0.017453292519943295f;

bool Near(float a, float b, float tolerance = 1e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

// A unit direction at (azimuth, elevation) in the hull frame.
void Dir(float azDeg, float elDeg, float out[3])
{
    seat_frame::Forward(azDeg * kDeg, elDeg * kDeg, out);
}

void TestHullFrame()
{
    seat_frame::Frame h{};
    const float fwd[3]{1.0f, 0.02f, 0.05f}, up[3]{-0.05f, 0.0f, 1.0f};
    Check(HullFrame(fwd, up, h), "a nearly orthogonal forward/up makes a frame");
    Check(Near(seat_frame::Dot(h.f, h.u), 0.0f, 1e-6f) && Near(seat_frame::Dot(h.f, h.l), 0.0f, 1e-6f) &&
              Near(seat_frame::Dot(h.l, h.u), 0.0f, 1e-6f),
          "orthogonal");
    Check(Near(seat_frame::Dot(h.f, h.f), 1.0f, 1e-5f) && Near(seat_frame::Dot(h.u, h.u), 1.0f, 1e-5f) &&
              Near(seat_frame::Dot(h.l, h.l), 1.0f, 1e-5f),
          "unit");
    Check(h.l[1] > 0.99f, "l = u x f (Halo's left: +Y for a hull facing +X)");
    const float badUp[3]{0.0f, 0.0f, 2.0f};
    Check(!HullFrame(fwd, badUp, h), "an up that is not unit is not a frame");
    const float skew[3]{0.5f, 0.0f, 0.866f};
    const float level[3]{1.0f, 0.0f, 0.0f};
    Check(!HullFrame(level, skew, h), "a forward 60 deg off the up's normal plane is not a frame");
    const float nan[3]{std::nanf(""), 0.0f, 1.0f};
    Check(!HullFrame(fwd, nan, h), "a non-finite up is not a frame");
}

void TestLocal()
{
    seat_frame::Frame h{};
    const float fwd[3]{0.0f, 1.0f, 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
    HullFrame(fwd, up, h);   // facing +Y
    const float w[3]{0.0f, 2.0f, -1.0f};
    float l[3]{}, back[3]{};
    ToLocal(h, w, l);
    Check(Near(l[0], 2.0f) && Near(l[1], 0.0f) && Near(l[2], -1.0f), "world +Y is the hull's forward");
    ToWorld(h, l, back);
    Check(Near(back[0], w[0]) && Near(back[1], w[1]) && Near(back[2], w[2]), "local and back");
}

void TestLearner()
{
    Learner l;
    l.reset(0x1234, 0x0657);
    const float o[3]{0.8f, 0.3f, -0.2f};
    float d[3]{};
    Check(!l.learned(), "nothing learned before a shot");
    Dir(0.0f, -2.0f, d);
    Check(l.step(0, d, o) == Shot::Learned && l.learned(), "the first shot is the barrel's line");
    Dir(0.5f, -1.6f, d);
    Check(l.step(0, d, o) == Shot::Learned, "0.4 deg of elevation holds to the line");
    float lo = 0.0f, hi = 0.0f;
    Check(l.elevationRange(lo, hi) && Near(lo, -2.0f * kDeg, 1e-4f) && Near(hi, -1.6f * kDeg, 1e-4f),
          "the range is the shots' elevations");
    Dir(3.0f, -2.0f, d);
    Check(l.step(0, d, o) == Shot::Learned, "3 deg of azimuth (a turning hull's frame lag) holds to the line");
    Dir(0.0f, 0.0f, d);
    Check(l.step(0, d, o) == Shot::Deviant, "2 deg of elevation is off the line");
    Check(l.elevationRange(lo, hi) && Near(hi, -1.6f * kDeg, 1e-4f), "a deviant shot widens nothing");
    Dir(0.0f, -2.0f, d);
    Check(l.step(0, d, o) == Shot::Learned, "a shot back on the line resets the run");
    Dir(0.0f, 0.0f, d);
    l.step(0, d, o);
    l.step(0, d, o);
    Check(!l.animated, "two deviant shots in a row are not yet animated");
    Check(l.step(0, d, o) == Shot::Animated && l.animated && Near(l.animatedDeg, 2.0f, 1e-2f),
          "three in a row: the barrels follow the aim");
    Check(!l.learned() && !l.elevationRange(lo, hi), "an animated vehicle has no band");
    float ol[3]{};
    Check(!l.originLocal(ol), "...and no origin");
    Dir(0.0f, -2.0f, d);
    Check(l.step(0, d, o) == Shot::Animated, "it stays animated");
    l.reset(0x1234, 0x0657);
    Check(!l.animated && !l.learned(), "a reset starts over");
    Dir(7.0f, -2.0f, d);
    l.step(0, d, o);
    Dir(13.0f, -2.0f, d);
    Check(l.step(0, d, o) == Shot::Deviant, "6 deg of azimuth is off the line");
    Learner back;
    back.reset(1, 2);
    Dir(179.0f, -2.0f, d);
    back.step(0, d, o);
    Dir(-179.0f, -2.0f, d);
    Check(back.step(0, d, o) == Shot::Learned, "azimuth wraps: 179 and -179 deg are 2 deg apart");
}

void TestLearnerBarrels()
{
    Learner l;
    l.reset(1, 2);
    float d[3]{};
    const float left[3]{0.8f, 0.4f, -0.2f}, right[3]{0.8f, -0.4f, -0.2f};
    Dir(0.0f, -1.0f, d);
    l.step(0, d, left);
    Dir(0.0f, -3.0f, d);
    l.step(1, d, right);
    float lo = 0.0f, hi = 0.0f;
    Check(l.elevationRange(lo, hi) && Near(lo, -3.0f * kDeg, 1e-4f) && Near(hi, -1.0f * kDeg, 1e-4f),
          "two barrels: the most permissive range across them");
    Learner swapped;
    swapped.reset(1, 2);
    Dir(0.0f, -3.0f, d);
    swapped.step(0, d, left);
    Dir(0.0f, -1.0f, d);
    swapped.step(1, d, right);
    Check(swapped.elevationRange(lo, hi) && Near(lo, -3.0f * kDeg, 1e-4f) && Near(hi, -1.0f * kDeg, 1e-4f),
          "...whichever barrel holds which end");
    float o[3]{};
    Check(l.originLocal(o) && Near(o[0], 0.8f) && Near(o[1], 0.0f) && Near(o[2], -0.2f),
          "the origin is the barrels' mean");
    const float later[3]{0.9f, 0.4f, -0.2f};
    Dir(0.0f, -1.0f, d);
    l.step(0, d, later);
    Check(l.originLocal(o) && Near(o[0], 0.825f), "a barrel's origin is the running mean of its shots");
    Check(l.step(4, d, left) == Shot::Refused && l.step(-1, d, left) == Shot::Refused, "an unknown barrel is refused");
    const float notUnit[3]{0.5f, 0.0f, 0.0f};
    Check(l.step(0, notUnit, left) == Shot::Refused, "a direction that is not unit is refused");
    const float nan[3]{std::nanf(""), 0.0f, 0.0f};
    Check(l.step(0, d, nan) == Shot::Refused, "a non-finite origin is refused");
}

void TestCap()
{
    const uint32_t bit25 = 1u << 25;
    Check(Near(TagAngle(bit25, 25, 0.10f, 0.30f), 0.10f), "flag bit 25 set: the primary angle alone");
    Check(Near(TagAngle(0, 25, 0.10f, 0.30f), 0.30f), "clear: the larger of the two");
    Check(Near(TagAngle(0, 25, 0.40f, 0.30f), 0.40f), "clear: the larger of the two (primary)");
    Check(std::isnan(TagAngle(0, 25, std::nanf(""), 0.3f)) && std::isnan(TagAngle(0, 25, -0.1f, 0.3f)) &&
              std::isnan(TagAngle(0, 25, 0.1f, 4.0f)) && std::isnan(TagAngle(0, 40, 0.1f, 0.3f)),
          "an implausible read is no angle");
    Check(Near(Cap(0.36651915f, 0.10f), 0.36651915f), "below the floor: the floor (21 deg)");
    Check(Near(Cap(0.36651915f, 0.50f), 0.50f), "above it: the weapon's own");
    Check(Near(Cap(0.36651915f, std::nanf("")), 0.36651915f), "no weapon angle: the floor");
    Check(std::isnan(Cap(std::nanf(""), 0.5f)) && std::isnan(Cap(0.0f, 0.5f)), "no floor: no cap");
}

void TestBand()
{
    Learner l;
    l.reset(1, 2);
    const float o[3]{1.0f, 0.0f, -0.3f};
    float d[3]{};
    Dir(0.0f, -2.0f, d);
    l.step(0, d, o);
    Dir(0.0f, -1.5f, d);
    l.step(0, d, o);
    float lo = 0.0f, hi = 0.0f;
    const float cap = 0.36651915f;
    Check(Band(l, cap, lo, hi) && Near(lo, -2.0f * kDeg - cap, 1e-4f) && Near(hi, -1.5f * kDeg + cap, 1e-4f),
          "the band is the barrels' range widened by the cap");
    Check(!Band(l, std::nanf(""), lo, hi) && !Band(l, 0.0f, lo, hi), "no cap: no band");
    Learner steep;
    steep.reset(1, 2);
    Dir(0.0f, 80.0f, d);
    steep.step(0, d, o);
    Check(Band(steep, 0.5f, lo, hi) && Near(hi, kPoleMarginRad), "held short of the pole");
    Learner none;
    Check(!Band(none, cap, lo, hi), "nothing learned: no band");
}

void TestClampShot()
{
    seat_frame::Frame level{};
    const float fwd[3]{1.0f, 0.0f, 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
    HullFrame(fwd, up, level);
    const float lo = -23.0f * kDeg, hi = 19.0f * kDeg;   // a barrel at -2 deg, a 21 deg cap
    const float origin[3]{0.0f, 0.0f, 0.0f};
    // The Chopper report: the point 26 deg down, 5.7 m (1.87 wu) ahead of the guns.
    const float r = 1.87f;
    const float low[3]{r * std::cos(26.0f * kDeg), 0.0f, -r * std::sin(26.0f * kDeg)};
    ShotClamp c{};
    Check(ClampShot(level, lo, hi, origin, low, c) && c.clamped && c.low, "26 deg down is past the band's low");
    Check(Near(std::asin(c.dir[2]), lo, 1e-4f) && Near(c.dir[1], 0.0f, 1e-5f) && c.dir[0] > 0.0f,
          "the shot is held at the band's edge, straight ahead");
    Check(Near(c.overshootDeg, 3.0f, 1e-2f), "3 deg past it");
    const float inside[3]{r * std::cos(10.0f * kDeg), 0.0f, -r * std::sin(10.0f * kDeg)};
    Check(ClampShot(level, lo, hi, origin, inside, c) && !c.clamped && Near(c.overshootDeg, 0.0f) &&
              Near(c.dir[2], -std::sin(10.0f * kDeg), 1e-5f),
          "inside the band: toward the point");
    const float high[3]{r * std::cos(30.0f * kDeg), 0.0f, r * std::sin(30.0f * kDeg)};
    Check(ClampShot(level, lo, hi, origin, high, c) && c.clamped && !c.low, "30 deg up is past the high");
    // Yaw is never clamped: a point far to the side, inside the band's elevation.
    const float side[3]{r * std::cos(10.0f * kDeg) * std::cos(70.0f * kDeg),
                        r * std::cos(10.0f * kDeg) * std::sin(70.0f * kDeg), -r * std::sin(10.0f * kDeg)};
    Check(ClampShot(level, lo, hi, origin, side, c) && !c.clamped, "70 deg to the side is catch-up, not a stop");
    // ...and past the band at the side, the azimuth is kept.
    const float sideLow[3]{r * std::cos(30.0f * kDeg) * std::cos(70.0f * kDeg),
                           r * std::cos(30.0f * kDeg) * std::sin(70.0f * kDeg), -r * std::sin(30.0f * kDeg)};
    Check(ClampShot(level, lo, hi, origin, sideLow, c) && c.clamped &&
              Near(std::atan2(c.dir[1], c.dir[0]), 70.0f * kDeg, 1e-4f),
          "a clamped shot keeps its azimuth");
    // The hull pitched 10 deg nose-down: 26 deg down in the world is 16 deg down in the hull's frame -- inside.
    seat_frame::Frame noseDown{};
    const float f2[3]{std::cos(10.0f * kDeg), 0.0f, -std::sin(10.0f * kDeg)},
                u2[3]{std::sin(10.0f * kDeg), 0.0f, std::cos(10.0f * kDeg)};
    HullFrame(f2, u2, noseDown);
    Check(ClampShot(noseDown, lo, hi, origin, low, c) && !c.clamped, "the band rides the hull's up, not the world's");
    Check(!ClampShot(level, lo, hi, origin, origin, c), "a point on the origin has no line");
    Check(!ClampShot(level, hi, lo, origin, low, c), "an unordered band clamps nothing");
}

// The shot's own line, not the eye's: guns below the eye see a near point steeper than the eye does.
void TestShotLineNotEye()
{
    seat_frame::Frame level{};
    const float fwd[3]{1.0f, 0.0f, 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
    HullFrame(fwd, up, level);
    const float lo = -23.0f * kDeg, hi = 19.0f * kDeg;
    const float eye[3]{0.0f, 0.0f, 0.0f};
    const float guns[3]{0.15f, 0.0f, -0.25f};   // ~0.45 m ahead of and ~0.76 m below the eye
    // A point the eye sees 20 deg down (inside), 1.2 wu (3.7 m) away.
    const float p[3]{1.2f * std::cos(20.0f * kDeg), 0.0f, -1.2f * std::sin(20.0f * kDeg)};
    ShotClamp fromEye{}, fromGuns{};
    Check(ClampShot(level, lo, hi, eye, p, fromEye) && !fromEye.clamped, "from the eye: 20 deg, inside");
    Check(ClampShot(level, lo, hi, guns, p, fromGuns) && !fromGuns.clamped,
          "from the guns the point is ~9 deg down: inside too");
    const float q[3]{0.6f * std::cos(35.0f * kDeg), 0.0f, -0.6f * std::sin(35.0f * kDeg) - 0.25f};
    Check(ClampShot(level, lo, hi, guns, q, fromGuns) && fromGuns.clamped,
          "a near point steep from the guns is past the band");
}
} // namespace

int main()
{
    TestHullFrame();
    TestLocal();
    TestLearner();
    TestLearnerBarrels();
    TestCap();
    TestBand();
    TestClampShot();
    TestShotLineNotEye();
    std::printf("vehicle_gun_band_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
