// A look-steered driver's hand aim refused behind and near (src/common/vehicle_aim_reject_logic.h): the 2D dot and
// the flattened distance in the vehicle's own ground plane, the ray judged when there is no point, and the heading
// that keeps the vehicle on its forward.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../src/common/vehicle_aim_reject_logic.h"

using namespace vehicle_aim_reject;

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

static bool Near(float a, float b, float tolerance = 1.0e-5f)
{
    return std::fabs(a - b) <= tolerance;
}

// A level vehicle at `origin` facing +X (Z up).
static Ground Level(float ox = 0.0f, float oy = 0.0f, float oz = 0.0f)
{
    const float origin[3]{ox, oy, oz};
    const float forward[3]{1.0f, 0.0f, 0.0f};
    const float up[3]{0.0f, 0.0f, 1.0f};
    Ground ground{};
    Check(GroundFrom(origin, forward, up, ground), "a level vehicle has a ground plane");
    return ground;
}

static Verdict Point(const Limits& limits, const Ground& ground, float x, float y, float z)
{
    const float point[3]{x, y, z};
    return JudgePoint(limits, ground, point).verdict;
}

static void TestLimits()
{
    const Limits defaults = LimitsFromConfig(kDefaultMetres, kDefaultDot);
    Check(defaults.on() && Near(defaults.distanceWu, 20.0f / 3.048f) && Near(defaults.distanceWu, 6.5617f, 1.0e-4f),
          "20 m is 2000 cm, about 6.56 world units");
    Check(defaults.dot == -0.5f, "the default dot is -0.5");
    Check(!LimitsFromConfig(0.0f, -0.5f).on() && !LimitsFromConfig(-3.0f, -0.5f).on() &&
              !LimitsFromConfig(NAN, -0.5f).on(),
          "0 m, a negative distance or not a number switch the rule off");
    Check(Near(LimitsFromConfig(1000.0f, -0.5f).distanceWu, kMaxMetres * kWuPerMetre),
          "a distance past the bound is held to it");
    Check(LimitsFromConfig(5.0f, -3.0f).dot == -1.0f && LimitsFromConfig(5.0f, 2.0f).dot == 1.0f &&
              LimitsFromConfig(5.0f, NAN).dot == kDefaultDot,
          "the dot is held to [-1, 1]; not a number is the default");
}

static void TestLevelVehicle()
{
    const Ground ground = Level(10.0f, -4.0f, 2.0f);
    const Limits limits = LimitsFromConfig(5.0f, -0.5f);
    const float r = limits.distanceWu;
    // Offsets from the origin (10, -4, 2), the vehicle facing +X.
    Check(Point(limits, ground, 10.0f - 0.5f * r, -4.0f, 2.0f) == Verdict::Refused,
          "straight behind and near: refused");
    Check(Point(limits, ground, 10.0f - 0.5f * r, -4.0f + 0.2f * r, 1.5f) == Verdict::Refused,
          "behind, a little aside and on the ground below the origin: refused");
    Check(Point(limits, ground, 10.0f - 2.0f * r, -4.0f, 2.0f) == Verdict::Accepted,
          "straight behind and far: accepted -- the player means to turn round");
    Check(Point(limits, ground, 10.0f, -4.0f + 0.5f * r, 2.0f) == Verdict::Accepted,
          "beside (dot 0) and near: accepted");
    Check(Point(limits, ground, 10.0f + 0.01f * r, -4.0f - 0.9f * r, 2.0f) == Verdict::Accepted,
          "beside on the other side, a hair ahead: accepted");
    Check(Point(limits, ground, 10.0f + 0.5f * r, -4.0f, 2.0f) == Verdict::Accepted, "ahead and near: accepted");
    Check(Point(limits, ground, 10.0f + 3.0f * r, -4.0f + r, 2.0f) == Verdict::Accepted, "ahead and far: accepted");
    // Behind and inside 120 deg either side of the nose: dot -0.4 is not behind enough.
    Check(Point(limits, ground, 10.0f - 0.4f * 0.5f * r, -4.0f + std::sqrt(1.0f - 0.16f) * 0.5f * r, 2.0f) ==
              Verdict::Accepted,
          "113 deg off the nose (dot -0.4) and near: accepted at -0.5");
    Check(Point(limits, ground, 10.0f - 0.6f * 0.5f * r, -4.0f - 0.8f * 0.5f * r, 2.0f) == Verdict::Refused,
          "127 deg off the nose (dot -0.6) and near: refused at -0.5");
    // FLATTENED distance: a point well above or below the plane, behind and near in the plane, is still near.
    Check(Point(limits, ground, 10.0f - 0.5f * r, -4.0f, 2.0f - 10.0f * r) == Verdict::Refused,
          "far below the plane but near in it: refused (the distance is the flattened one)");
    Check(Point(limits, ground, 10.0f - 0.5f * r, -4.0f, 2.0f + 10.0f * r) == Verdict::Refused,
          "far above it: refused too");

    const float behind[3]{10.0f - 0.5f * r, -4.0f, 2.0f};
    const Judgement j = JudgePoint(limits, ground, behind);
    Check(j.heading && Near(j.dot, -1.0f) && Near(j.distanceWu, 0.5f * r), "the judgement reports its dot and distance");
}

static void TestThresholds()
{
    // At the thresholds exactly: near is inclusive, behind is strict. 3-4-5 offsets of length 2.5 are exact in float,
    // and so are their dots, -0.6 and -0.8.
    const Ground ground = Level();
    const Limits roomy{3.0f, -0.6f};
    Check(Point(roomy, ground, -1.5f, 2.0f, 0.0f) == Verdict::Accepted,
          "a dot exactly at the threshold (-0.6) is not behind");
    Check(Point(roomy, ground, -2.0f, -1.5f, 0.0f) == Verdict::Refused, "a dot past it (-0.8) is");
    const Limits tight{2.5f, -0.6f};
    Check(Point(tight, ground, -2.0f, -1.5f, 0.0f) == Verdict::Refused,
          "behind, at the distance exactly: refused -- at the distance is near");
    Check(Point(tight, ground, -2.5f, 0.0f, 0.0f) == Verdict::Refused, "straight behind at the distance exactly: refused");
    Check(Point(tight, ground, -2.75f, 0.0f, 0.0f) == Verdict::Accepted, "straight behind just past it: accepted");

    // The defaults, on the rule itself.
    const Limits defaults = LimitsFromConfig(kDefaultMetres, kDefaultDot);
    Check(!Refuses(defaults, -0.5f, defaults.distanceWu, true),
          "the default dot exactly (-0.5) at the default distance exactly: not behind, accepted");
    Check(Refuses(defaults, -0.5001f, defaults.distanceWu, true),
          "a dot just below -0.5 at the default distance exactly: refused");
    Check(!Refuses(defaults, -1.0f, defaults.distanceWu * 1.0001f, true),
          "straight behind just past the default distance: accepted");
    Check(!Refuses(defaults, -1.0f, NAN, true), "a distance that is not a number refuses nothing");
    const Limits all{2.0f, 1.0f};
    Check(Point(all, ground, 0.5f, 0.5f, 0.0f) == Verdict::Refused && Point(all, ground, 1.0f, 0.0f, 0.0f) ==
              Verdict::Accepted,
          "dot 1 refuses every near point but the one dead ahead");
    const Limits none{2.0f, -1.0f};
    Check(Point(none, ground, -1.0f, 0.0f, 0.0f) == Verdict::Accepted, "dot -1 refuses nothing that has a heading");
}

static void TestOff()
{
    const Ground ground = Level();
    const Limits off = LimitsFromConfig(0.0f, -0.5f);
    Check(Point(off, ground, -0.1f, 0.0f, 0.0f) == Verdict::Off, "distance 0: off, even straight behind and near");
    Check(Point(off, ground, 0.0f, 0.0f, 0.0f) == Verdict::Off, "and at the origin");
    const float eye[3]{0.0f, 0.0f, 1.0f};
    const float back[3]{-1.0f, 0.0f, -1.0f};
    Check(JudgeRay(off, ground, eye, back).verdict == Verdict::Off, "the ray too");
    Check(!Refuses(off, -1.0f, 0.0f, true) && !Refuses(off, 0.0f, 0.0f, false), "and the rule itself");
}

static void TestOrigin()
{
    // A point straight above or below the origin names no heading: whatever heading it gave would be noise, so it is
    // refused while the rule is on (the vehicle keeps its forward), whatever the dot threshold.
    const Ground ground = Level();
    const Limits limits = LimitsFromConfig(5.0f, -0.5f);
    Check(Point(limits, ground, 0.0f, 0.0f, 0.0f) == Verdict::Refused, "the origin itself: refused");
    Check(Point(limits, ground, 0.0f, 0.0f, -0.3f) == Verdict::Refused, "straight below it: refused");
    Check(Point(limits, ground, 0.001f, 0.0f, -0.3f) == Verdict::Refused,
          "a millimetre-scale offset is still no heading: refused");
    const Limits strict{limits.distanceWu, -1.0f};
    Check(Point(strict, ground, 0.0f, 0.0f, 0.0f) == Verdict::Refused, "even where no direction would be behind");
    const float origin[3]{0.0f, 0.0f, 0.0f};
    const Judgement j = JudgePoint(limits, ground, origin);
    Check(!j.heading && j.distanceWu == 0.0f, "reported as no heading at distance 0");
}

static void TestSlope()
{
    // A vehicle climbing a 30 deg slope toward +X, the slope also banked 20 deg: its ground plane is not the world's.
    const float pitch = 30.0f * 3.14159265f / 180.0f, roll = 20.0f * 3.14159265f / 180.0f;
    // forward pitched up about the world Y axis; up tilted back by the pitch, then rolled about forward.
    const float forward[3]{std::cos(pitch), 0.0f, std::sin(pitch)};
    const float up0[3]{-std::sin(pitch), 0.0f, std::cos(pitch)};
    const float left0[3]{0.0f, 1.0f, 0.0f};
    const float up[3]{up0[0] * std::cos(roll) - left0[0] * std::sin(roll), up0[1] * std::cos(roll) - left0[1] * std::sin(roll),
                      up0[2] * std::cos(roll) - left0[2] * std::sin(roll)};
    const float left[3]{left0[0] * std::cos(roll) + up0[0] * std::sin(roll),
                        left0[1] * std::cos(roll) + up0[1] * std::sin(roll),
                        left0[2] * std::cos(roll) + up0[2] * std::sin(roll)};
    const float origin[3]{3.0f, 2.0f, 1.0f};
    Ground ground{};
    Check(GroundFrom(origin, forward, up, ground), "a vehicle on a slope has a ground plane");
    Check(Near(ground.left[0], left[0]) && Near(ground.left[1], left[1]) && Near(ground.left[2], left[2]),
          "its left is up x forward");
    const Limits limits = LimitsFromConfig(5.0f, -0.5f);
    const float r = limits.distanceWu;
    const auto at = [&](float back, float aside, float lift) {
        const float p[3]{origin[0] + forward[0] * back + left[0] * aside + up[0] * lift,
                         origin[1] + forward[1] * back + left[1] * aside + up[1] * lift,
                         origin[2] + forward[2] * back + left[2] * aside + up[2] * lift};
        return JudgePoint(limits, ground, p).verdict;
    };
    Check(at(-0.6f * r, 0.0f, -0.3f) == Verdict::Refused, "down the slope behind it, near: refused");
    Check(at(0.6f * r, 0.0f, -0.3f) == Verdict::Accepted, "up the slope ahead of it, near: accepted");
    Check(at(-0.6f * r, 0.0f, 50.0f * r) == Verdict::Refused,
          "far off its plane along its own up but behind and near in it: refused");
    // THE PLANE IS THE VEHICLE'S. The same vehicle judged in the world's level plane (its world heading, +X, and the
    // world's up) disagrees with it on points off its deck, and the vehicle's plane is the one that counts.
    const Ground world = Level(origin[0], origin[1], origin[2]);
    const auto both = [&](float back, float lift, Verdict& vehicle, Verdict& level) {
        const float p[3]{origin[0] + forward[0] * back + up[0] * lift, origin[1] + forward[1] * back + up[1] * lift,
                         origin[2] + forward[2] * back + up[2] * lift};
        vehicle = JudgePoint(limits, ground, p).verdict;
        level = JudgePoint(limits, world, p).verdict;
    };
    Verdict vehicle = Verdict::Off, level = Verdict::Off;
    // A little ahead along the deck and well up along the vehicle's own up (a face above it on the climb): ahead for
    // the vehicle, behind and near for a level plane.
    both(0.3f * r, 1.6f * r, vehicle, level);
    Check(vehicle == Verdict::Accepted && level == Verdict::Refused,
          "a point above the deck, ahead of the nose: accepted in the vehicle's plane (a level plane would refuse it)");
    // A little behind along the deck and well down along its up (the ground below it on the climb): behind and near
    // for the vehicle, ahead for a level plane.
    both(-0.3f * r, -1.6f * r, vehicle, level);
    Check(vehicle == Verdict::Refused && level == Verdict::Accepted,
          "a point below the deck, behind the tail: refused in the vehicle's plane (a level plane would accept it)");
    // A forward not square to up (as read) is made square in the plane, not refused.
    const float skewed[3]{forward[0] + 0.05f * up[0], forward[1] + 0.05f * up[1], forward[2] + 0.05f * up[2]};
    Ground squared{};
    Check(GroundFrom(origin, skewed, up, squared) && Near(Dot(squared.forward, squared.up), 0.0f) &&
              Near(squared.forward[0], forward[0], 1.0e-4f) && Near(squared.forward[2], forward[2], 1.0e-4f),
          "a forward slightly off the plane is squared to it");
}

static void TestBadFrames()
{
    const float origin[3]{0.0f, 0.0f, 0.0f};
    const float forward[3]{1.0f, 0.0f, 0.0f};
    const float up[3]{0.0f, 0.0f, 1.0f};
    const float zero[3]{0.0f, 0.0f, 0.0f};
    const float bad[3]{NAN, 0.0f, 0.0f};
    Ground ground{};
    Check(!GroundFrom(origin, forward, zero, ground), "no up: no plane");
    Check(!GroundFrom(origin, up, up, ground), "forward along up: no plane");
    Check(!GroundFrom(bad, forward, up, ground) && !GroundFrom(origin, bad, up, ground),
          "a value that is not a number: no plane");
}

static void TestRay()
{
    // No point: the aim's own direction from the eye, judged by its flattened dot and where it meets the plane.
    const Ground ground = Level();
    const Limits limits = LimitsFromConfig(5.0f, -0.5f);
    const float r = limits.distanceWu;
    // A chase camera 2 r behind and r above the vehicle.
    const float chase[3]{-2.0f * r, 0.0f, r};
    const float backDown[3]{-1.0f, 0.0f, -1.0f};
    Check(JudgeRay(limits, ground, chase, backDown).verdict == Verdict::Accepted,
          "pointing back and down from a camera far behind: the ground it meets is far, accepted");
    // The seat's own eye, just above the origin.
    const float seat[3]{0.0f, 0.0f, 0.4f * r};
    Check(JudgeRay(limits, ground, seat, backDown).verdict == Verdict::Refused,
          "pointing back and down from the seat at the ground just behind: refused");
    const float backFlat[3]{-1.0f, 0.0f, 0.0f};
    Check(JudgeRay(limits, ground, seat, backFlat).verdict == Verdict::Accepted,
          "pointing straight back, parallel to the ground: it never meets the plane, accepted");
    const float backUp[3]{-1.0f, 0.0f, 0.2f};
    Check(JudgeRay(limits, ground, seat, backUp).verdict == Verdict::Accepted,
          "pointing back and up: it leaves the plane, accepted");
    const float aheadDown[3]{1.0f, 0.0f, -1.0f};
    Check(JudgeRay(limits, ground, seat, aheadDown).verdict == Verdict::Accepted,
          "pointing ahead and down: not behind, accepted");
    const float sideDown[3]{0.0f, 1.0f, -1.0f};
    Check(JudgeRay(limits, ground, seat, sideDown).verdict == Verdict::Accepted,
          "pointing aside and down near the vehicle (dot 0): accepted");
    // From the chase camera, forward and down onto the ground just behind the vehicle: the direction is forward, the
    // heading it writes is forward, so there is nothing to refuse.
    const float forwardDown[3]{1.5f, 0.0f, -1.0f};
    Check(JudgeRay(limits, ground, chase, forwardDown).verdict == Verdict::Accepted,
          "forward and down from a chase camera onto the ground behind the vehicle: its heading is forward, accepted");
    const float down[3]{0.0f, 0.0f, -1.0f};
    const Judgement straightDown = JudgeRay(limits, ground, seat, down);
    Check(!straightDown.heading && straightDown.verdict == Verdict::Refused,
          "straight down from the seat names no heading, near: refused");
    Check(JudgeRay(limits, ground, chase, down).verdict == Verdict::Accepted,
          "straight down from a camera far behind: far, accepted");
    const float zero[3]{0.0f, 0.0f, 0.0f};
    Check(JudgeRay(limits, ground, seat, zero).verdict == Verdict::Accepted, "no direction at all: accepted");
}

static void TestForwardYaw()
{
    float yaw = 0.0f;
    const float east[3]{1.0f, 0.0f, 0.0f};
    Check(ForwardYaw(east, yaw) && Near(yaw, 0.0f), "facing +X: yaw 0");
    const float north[3]{0.0f, 1.0f, 0.0f};
    Check(ForwardYaw(north, yaw) && Near(yaw, 1.5707964f), "facing +Y: yaw 90 deg (toward the left)");
    const float climbing[3]{-0.5f, 0.0f, 0.8660254f};
    Check(ForwardYaw(climbing, yaw) && Near(std::fabs(yaw), 3.1415927f), "a pitched forward keeps its heading");
    const float vertical[3]{0.0f, 0.0f, 1.0f};
    Check(!ForwardYaw(vertical, yaw), "straight up has no heading");
    const float bad[3]{NAN, 1.0f, 0.0f};
    Check(!ForwardYaw(bad, yaw), "not a number has no heading");
}

int main()
{
    TestLimits();
    TestLevelVehicle();
    TestThresholds();
    TestOff();
    TestOrigin();
    TestSlope();
    TestBadFrames();
    TestRay();
    TestForwardYaw();
    std::printf("vehicle_aim_reject_tests: %u checks passed\n", g_checks);
    return 0;
}
