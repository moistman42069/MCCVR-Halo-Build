// Support-grab acquisition predicate tests (A003 Fix Two).
//
// The defect under test was rotation coupling: the effective grab sample was
// shifted along the SUPPORT controller's forward vector, so a pure wrist
// rotation orbited the sample by up to the configured palm-depth offset
// (+/- 9.7 cm at the 0.097 m default) around a 9 cm lateral acceptance radius.
// The old pattern is reproduced here by its own reference arithmetic (the
// pre-fix vr.cpp UpdateTwoHandLatch lambda plus the existing
// virtual_stock::SupportGrabPoint) so the test can prove the defect and the
// fix on the same frozen geometry.
//
// Runtime wiring (T7 corrections): the predicate below is the PERSISTENT
// support grip acquisition rule. The durable writer runs it only while the
// feature is on and the title is wired; PG off (and unwired titles) keep the
// pre-fix geometry verbatim in vr.cpp's `TwoHandGrabZoneHit`. The shipping
// helpers themselves are asserted by tests/grip_pose_capture_tests.cpp.
#include "support_grab_logic.h"
#include "virtual_stock_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
using virtual_stock::Point3;
using virtual_stock::Quat4;

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

Point3 Point(float x, float y, float z)
{
    return Point3{x, y, z};
}

Point3 Add(Point3 a, Point3 b)
{
    return Point3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Point3 Subtract(Point3 a, Point3 b)
{
    return Point3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Quat4 AxisAngle(Point3 axis, float degrees)
{
    const double radians =
        static_cast<double>(degrees) * 3.14159265358979323846 / 180.0;
    const float halfSine = static_cast<float>(std::sin(radians * 0.5));
    return Quat4{
        axis.x * halfSine, axis.y * halfSine, axis.z * halfSine,
        static_cast<float>(std::cos(radians * 0.5))};
}

Quat4 MultiplyQuaternion(Quat4 a, Quat4 b)
{
    return Quat4{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

bool SameZoneBits(
    const support_grab::SupportGrabZone& a,
    const support_grab::SupportGrabZone& b)
{
    return std::memcmp(&a.along, &b.along, sizeof(float)) == 0 &&
        std::memcmp(&a.lateral, &b.lateral, sizeof(float)) == 0 &&
        a.inZone == b.inZone;
}

bool FailsClosed(const support_grab::SupportGrabZone& zone)
{
    return !zone.inZone && zone.along == 0.0f && zone.lateral == 0.0f;
}

struct TestContext
{
    unsigned checks{};
    unsigned failures{};

    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }

    void CheckNear(float actual, float expected, const char* message,
        float epsilon = 1e-4f)
    {
        ++checks;
        if (!(std::fabs(actual - expected) <= epsilon))
        {
            ++failures;
            std::fprintf(stderr,
                "FAIL: %s\n  expected: %g\n  actual:   %g\n",
                message, expected, actual);
        }
    }
};

// Primary frame used by most cases: forward -Z, right +X, identity rotation.
const Point3 kPrimary = Point(0.30f, 1.40f, -0.25f);
const Point3 kForward = Point(0.0f, 0.0f, -1.0f);
const Point3 kRight = Point(1.0f, 0.0f, 0.0f);
const float kGripForward = 0.097f;

// Place the raw support point so the acquisition-axis sample (palm depth
// already clamped exactly as virtual_stock::SupportGrabPoint clamps it) sits
// at the requested along/lateral in the primary frame above.
Point3 RawSupportFor(float along, float lateral, float gripForwardM)
{
    const float effectiveGrip = std::clamp(gripForwardM, -0.05f, 0.25f);
    return Point(kPrimary.x + lateral, kPrimary.y,
        kPrimary.z - along + effectiveGrip);
}

// Exact pre-fix arithmetic: origin = primary + right*clamp(zoneRight, +/-0.10),
// sample = raw support shifted along the SUPPORT forward, along/lateral in the
// primary frame, strict thresholds. Mirrors the replaced vr.cpp lambda text.
struct LegacyZone
{
    float along = 0.0f;
    float lateral = 0.0f;
    bool inZone = false;
};

LegacyZone EvaluateLegacyZone(
    Point3 primaryPosition, Point3 primaryForward, Point3 primaryRight,
    float zoneRightM, Point3 rawSupport, Point3 supportForward,
    float gripForwardM)
{
    const float zoneRight = std::clamp(zoneRightM, -0.10f, 0.10f);
    const Point3 origin{
        primaryPosition.x + primaryRight.x * zoneRight,
        primaryPosition.y + primaryRight.y * zoneRight,
        primaryPosition.z + primaryRight.z * zoneRight};
    const Point3 sample = virtual_stock::SupportGrabPoint(
        rawSupport, supportForward, gripForwardM);
    const Point3 v{
        sample.x - origin.x, sample.y - origin.y, sample.z - origin.z};
    const float along = virtual_stock::Dot(v, primaryForward);
    const Point3 perpendicular{
        v.x - along * primaryForward.x,
        v.y - along * primaryForward.y,
        v.z - along * primaryForward.z};
    const float lateral =
        std::sqrt(virtual_stock::Dot(perpendicular, perpendicular));
    LegacyZone zone{};
    zone.along = along;
    zone.lateral = lateral;
    zone.inZone = along > 0.08f && along < 0.80f && lateral < 0.09f;
    return zone;
}

struct SupportOrientation
{
    const char* name;
    Quat4 rotation;
};

// Modelled support-controller wrist orientations: neutral, both roll signs,
// both pitch signs, both yaw signs, and a combined cant. The new predicate has
// no support-orientation input at all; these exist only to drive the old
// pattern and to prove the new one is invariant across them.
const SupportOrientation kOrientations[] = {
    {"neutral", Quat4{0.0f, 0.0f, 0.0f, 1.0f}},
    {"roll +60", AxisAngle(Point(0.0f, 0.0f, 1.0f), 60.0f)},
    {"roll -60", AxisAngle(Point(0.0f, 0.0f, 1.0f), -60.0f)},
    {"pitch +45", AxisAngle(Point(1.0f, 0.0f, 0.0f), 45.0f)},
    {"pitch -45", AxisAngle(Point(1.0f, 0.0f, 0.0f), -45.0f)},
    {"yaw +45", AxisAngle(Point(0.0f, 1.0f, 0.0f), 45.0f)},
    {"yaw -45", AxisAngle(Point(0.0f, 1.0f, 0.0f), -45.0f)},
    {"combined cant",
        MultiplyQuaternion(AxisAngle(Point(0.0f, 1.0f, 0.0f), -40.0f),
            AxisAngle(Point(1.0f, 0.0f, 0.0f), 30.0f))},
};

struct OrientationSweep
{
    unsigned samplesMoved = 0;
    unsigned legacyOutNewIn = 0;
    unsigned legacyInNewOut = 0;
    bool newStable = true;
    bool newMatchesExpected = true;
};

// Re-evaluate the shipped predicate for every modelled wrist orientation and
// compare it against the old-pattern reference on the same raw support point.
// The shipped predicate call deliberately contains no orientation argument;
// only the reference call consumes the rotated support forward.
OrientationSweep SweepOrientations(
    Point3 primaryPosition, Point3 primaryForward, Point3 primaryRight,
    float zoneRightM, Point3 rawSupport, float gripForwardM,
    bool expectedInZone)
{
    OrientationSweep sweep{};
    const support_grab::SupportGrabZone reference =
        support_grab::EvaluateSupportGrabZone(primaryPosition, primaryForward,
            primaryRight, zoneRightM, rawSupport, gripForwardM);
    const Point3 acquisitionSample = support_grab::SupportGrabSample(
        rawSupport, primaryForward, gripForwardM);
    for (const SupportOrientation& orientation : kOrientations)
    {
        const Point3 supportForward = virtual_stock::RotatePoint(
            orientation.rotation, Point(0.0f, 0.0f, -1.0f));
        const Point3 legacySample = virtual_stock::SupportGrabPoint(
            rawSupport, supportForward, gripForwardM);
        const LegacyZone legacy = EvaluateLegacyZone(primaryPosition,
            primaryForward, primaryRight, zoneRightM, rawSupport,
            supportForward, gripForwardM);
        const support_grab::SupportGrabZone again =
            support_grab::EvaluateSupportGrabZone(primaryPosition,
                primaryForward, primaryRight, zoneRightM, rawSupport,
                gripForwardM);
        if (!SameZoneBits(reference, again))
            sweep.newStable = false;
        if (again.inZone != expectedInZone)
            sweep.newMatchesExpected = false;
        if (!(std::fabs(legacySample.x - acquisitionSample.x) <= 1e-6f &&
                std::fabs(legacySample.y - acquisitionSample.y) <= 1e-6f &&
                std::fabs(legacySample.z - acquisitionSample.z) <= 1e-6f))
            ++sweep.samplesMoved;
        if (!legacy.inZone && again.inZone)
            ++sweep.legacyOutNewIn;
        if (legacy.inZone && !again.inZone)
            ++sweep.legacyInNewOut;
    }
    return sweep;
}

void TestConstantsAndSample(TestContext& test)
{
    test.Check(support_grab::kAlongMinimumM == 0.08f &&
            support_grab::kAlongMaximumM == 0.80f &&
            support_grab::kLateralMaximumM == 0.09f &&
            support_grab::kZoneRightClampM == 0.10f,
        "acquisition thresholds and zone-right clamp keep the pre-fix values");

    const Point3 raw = Point(0.20f, 1.30f, -0.40f);
    const Point3 sample = support_grab::SupportGrabSample(
        raw, kForward, kGripForward);
    test.CheckNear(sample.x, raw.x, "sample keeps x off the acquisition axis");
    test.CheckNear(sample.y, raw.y, "sample keeps y off the acquisition axis");
    test.CheckNear(sample.z, raw.z - kGripForward,
        "sample advances along the acquisition axis by the palm depth");

    const Point3 clampedLow = support_grab::SupportGrabSample(
        raw, kForward, -1.0f);
    test.CheckNear(clampedLow.z, raw.z + 0.05f,
        "palm depth below range clamps to -0.05");
    const Point3 clampedHigh = support_grab::SupportGrabSample(
        raw, kForward, 1.0f);
    test.CheckNear(clampedHigh.z, raw.z - 0.25f,
        "palm depth above range clamps to 0.25");
    const Point3 zero = support_grab::SupportGrabSample(raw, kForward, 0.0f);
    test.CheckNear(zero.z, raw.z, "zero palm depth keeps the raw point");
}

void TestRotationStability(TestContext& test)
{
    const Point3 rawSupport = RawSupportFor(0.40f, 0.089f, kGripForward);
    const support_grab::SupportGrabZone reference =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.0f, rawSupport, kGripForward);
    test.Check(reference.inZone, "reference support point is inside the zone");
    test.CheckNear(reference.along, 0.40f,
        "reference along is the hand depth in the primary frame");
    test.CheckNear(reference.lateral, 0.089f,
        "reference lateral sits just under the 9 cm limit");

    const OrientationSweep sweep = SweepOrientations(kPrimary, kForward,
        kRight, 0.0f, rawSupport, kGripForward, true);
    test.Check(sweep.newStable,
        "predicate result is bit-identical across every support wrist "
        "rotation");
    test.Check(sweep.newMatchesExpected,
        "every support wrist rotation keeps the fixed hand position in zone");
    test.Check(sweep.samplesMoved >= 4,
        "old pattern moved the effective sample for pitch, yaw and cant");
    test.Check(sweep.legacyOutNewIn >= 2,
        "old pattern dropped the grab for wrist rotations the fix now ignores");

    // Named demonstration: mirrored yaw on one fixed hand position. The old
    // shift follows the support forward, so one sign pulls the sample across
    // the 9 cm lateral boundary and the other does not. The shipped predicate
    // cannot see the difference.
    const Point3 yawPositive = virtual_stock::RotatePoint(
        kOrientations[5].rotation, Point(0.0f, 0.0f, -1.0f));
    const Point3 yawNegative = virtual_stock::RotatePoint(
        kOrientations[6].rotation, Point(0.0f, 0.0f, -1.0f));
    const LegacyZone legacyYawPositive = EvaluateLegacyZone(kPrimary, kForward,
        kRight, 0.0f, rawSupport, yawPositive, kGripForward);
    const LegacyZone legacyYawNegative = EvaluateLegacyZone(kPrimary, kForward,
        kRight, 0.0f, rawSupport, yawNegative, kGripForward);
    test.Check(legacyYawPositive.inZone,
        "old pattern: yaw one way stays inside the lateral boundary");
    test.Check(!legacyYawNegative.inZone &&
            legacyYawNegative.lateral > support_grab::kLateralMaximumM + 0.05f,
        "old pattern: mirrored yaw swings the sample outside the boundary");
    test.Check(reference.inZone,
        "fixed predicate: mirrored yaw cannot change eligibility");
}

void TestBoundaryResponse(TestContext& test)
{
    // Just inside / near / just outside the lateral limit. Rotation must not
    // swing any of them; hand movement must.
    struct LateralCase
    {
        float lateral;
        bool expectedInZone;
    };
    const LateralCase lateralCases[] = {
        {0.0890f, true}, {0.0895f, true}, {0.0905f, false}};
    for (const LateralCase& boundary : lateralCases)
    {
        const Point3 rawSupport = RawSupportFor(
            0.40f, boundary.lateral, kGripForward);
        const support_grab::SupportGrabZone zone =
            support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
                0.0f, rawSupport, kGripForward);
        test.CheckNear(zone.lateral, boundary.lateral,
            "constructed lateral is the requested lateral");
        test.Check(zone.inZone == boundary.expectedInZone,
            "lateral threshold stays strict on the constructed boundary case");
        const OrientationSweep sweep = SweepOrientations(kPrimary, kForward,
            kRight, 0.0f, rawSupport, kGripForward, boundary.expectedInZone);
        test.Check(sweep.newStable && sweep.newMatchesExpected,
            "rotation cannot swing eligibility at the lateral boundary");
        if (boundary.expectedInZone)
        {
            test.Check(sweep.legacyOutNewIn >= 1,
                "old pattern: rotation disabled a grab just inside the limit");
        }
        else
        {
            test.Check(sweep.legacyInNewOut >= 1,
                "old pattern: rotation enabled a grab just outside the limit");
        }
    }

    // Hand movement still steers the zone naturally.
    struct HandCase
    {
        float along;
        float lateral;
        bool expectedInZone;
    };
    const HandCase handCases[] = {
        {0.20f, 0.02f, true},
        {0.40f, 0.02f, true},
        {0.05f, 0.02f, false},
        {0.85f, 0.02f, false},
        {0.40f, 0.12f, false},
        {-0.10f, 0.02f, false}};
    for (const HandCase& hand : handCases)
    {
        const Point3 rawSupport = RawSupportFor(
            hand.along, hand.lateral, kGripForward);
        const support_grab::SupportGrabZone zone =
            support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
                0.0f, rawSupport, kGripForward);
        test.CheckNear(zone.along, hand.along,
            "moving the hand changes along");
        test.CheckNear(zone.lateral, hand.lateral,
            "moving the hand changes lateral");
        test.Check(zone.inZone == hand.expectedInZone,
            "hand movement alone decides eligibility");
    }

    // Strict along bounds with an exact construction (zero palm depth so the
    // raw point is the sample bit-for-bit).
    const support_grab::SupportGrabZone atMinimum =
        support_grab::EvaluateSupportGrabZone(Point(0, 0, 0), kForward, kRight,
            0.0f, Point(0.02f, 0.0f, -0.08f), 0.0f);
    test.Check(atMinimum.along == 0.08f && !atMinimum.inZone,
        "along minimum is strict at exactly 0.08 m");
    const support_grab::SupportGrabZone atMaximum =
        support_grab::EvaluateSupportGrabZone(Point(0, 0, 0), kForward, kRight,
            0.0f, Point(0.02f, 0.0f, -0.80f), 0.0f);
    test.Check(atMaximum.along == 0.80f && !atMaximum.inZone,
        "along maximum is strict at exactly 0.80 m");
}

void TestZoneRightClamp(TestContext& test)
{
    const Point3 rawSupport = RawSupportFor(0.40f, 0.05f, kGripForward);
    const support_grab::SupportGrabZone atLimit =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            support_grab::kZoneRightClampM, rawSupport, kGripForward);
    const support_grab::SupportGrabZone overLimit =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            1.0f, rawSupport, kGripForward);
    const support_grab::SupportGrabZone belowLimit =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            -1.0f, rawSupport, kGripForward);
    const support_grab::SupportGrabZone negativeLimit =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            -support_grab::kZoneRightClampM, rawSupport, kGripForward);
    test.Check(SameZoneBits(overLimit, atLimit),
        "zone-right nudge clamps above the limit to exactly +0.10");
    test.Check(SameZoneBits(belowLimit, negativeLimit),
        "zone-right nudge clamps below the limit to exactly -0.10");

    // The nudge still moves the zone: a point outside at zero nudge is inside
    // once the origin is shifted by the configured amount.
    const Point3 edge = RawSupportFor(0.40f, 0.155f, kGripForward);
    const support_grab::SupportGrabZone noNudge =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.0f, edge, kGripForward);
    const support_grab::SupportGrabZone nudged =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.075f, edge, kGripForward);
    test.Check(!noNudge.inZone && nudged.inZone,
        "zone-right nudge still moves the acceptance boundary");
}

void TestDeterminism(TestContext& test)
{
    const Point3 insideRaw = RawSupportFor(0.40f, 0.05f, kGripForward);
    const Point3 outsideRaw = RawSupportFor(0.40f, 0.12f, kGripForward);
    const support_grab::SupportGrabZone insideA =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.03f, insideRaw, kGripForward);
    const support_grab::SupportGrabZone insideB =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.03f, insideRaw, kGripForward);
    const support_grab::SupportGrabZone insideC =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.03f, insideRaw, kGripForward);
    const support_grab::SupportGrabZone outsideA =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.03f, outsideRaw, kGripForward);
    const support_grab::SupportGrabZone outsideB =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            0.03f, outsideRaw, kGripForward);
    test.Check(SameZoneBits(insideA, insideB) && SameZoneBits(insideB, insideC),
        "repeated evaluation of frozen in-zone inputs is bit-identical");
    test.Check(SameZoneBits(outsideA, outsideB),
        "repeated evaluation of frozen out-of-zone inputs is bit-identical");
    test.Check(insideA.inZone && !outsideA.inZone,
        "determinism case spans both eligibility outcomes");
}

void TestRecenterInvariance(TestContext& test)
{
    const float zoneRight = 0.03f;
    const Point3 rawSupport = RawSupportFor(0.40f, 0.05f, kGripForward);
    const support_grab::SupportGrabZone base =
        support_grab::EvaluateSupportGrabZone(kPrimary, kForward, kRight,
            zoneRight, rawSupport, kGripForward);
    test.Check(base.inZone, "recenter reference is inside the zone");

    // One consistent rigid transform (rotation + translation) applied to every
    // positional input: F1 recenter must not change the zone result.
    const Quat4 rigid = MultiplyQuaternion(
        AxisAngle(Point(0.0f, 1.0f, 0.0f), 25.0f),
        AxisAngle(Point(1.0f, 0.0f, 0.0f), 12.0f));
    const Point3 translation = Point(0.20f, -0.05f, 0.30f);
    const Point3 movedPrimary = Add(
        virtual_stock::RotatePoint(rigid, kPrimary), translation);
    const Point3 movedForward = virtual_stock::RotatePoint(rigid, kForward);
    const Point3 movedRight = virtual_stock::RotatePoint(rigid, kRight);
    const Point3 movedSupport = Add(
        virtual_stock::RotatePoint(rigid, rawSupport), translation);
    const support_grab::SupportGrabZone recentered =
        support_grab::EvaluateSupportGrabZone(movedPrimary, movedForward,
            movedRight, zoneRight, movedSupport, kGripForward);

    test.CheckNear(recentered.along, base.along,
        "along is unchanged by a rigid recenter of every input");
    test.CheckNear(recentered.lateral, base.lateral,
        "lateral is unchanged by a rigid recenter of every input");
    test.Check(recentered.inZone == base.inZone,
        "eligibility is unchanged by a rigid recenter of every input");
    test.Check(recentered.inZone, "recentered sample stays inside the zone");
}

void TestFailClosedInputs(TestContext& test)
{
    const Point3 rawSupport = RawSupportFor(0.40f, 0.05f, kGripForward);
    const Point3 badPoint = Point(kNan, 0.0f, 0.0f);
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            badPoint, kForward, kRight, 0.0f, rawSupport, kGripForward)),
        "non-finite primary position fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, badPoint, kRight, 0.0f, rawSupport, kGripForward)),
        "non-finite acquisition forward fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, kForward, badPoint, 0.0f, rawSupport, kGripForward)),
        "non-finite primary right fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, kForward, kRight, kNan, rawSupport, kGripForward)),
        "NaN zone-right nudge fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, kForward, kRight, kInf, rawSupport, kGripForward)),
        "infinite zone-right nudge fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, kForward, kRight, 0.0f, badPoint, kGripForward)),
        "non-finite raw support position fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, kForward, kRight, 0.0f, rawSupport, kNan)),
        "NaN palm depth fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            kPrimary, kForward, kRight, 0.0f, rawSupport, kInf)),
        "infinite palm depth fails closed");
    test.Check(FailsClosed(support_grab::EvaluateSupportGrabZone(
            Point(kInf, 0.0f, 0.0f), kForward, kRight, 0.0f, rawSupport,
            kGripForward)),
        "infinite primary position fails closed");
}

} // namespace

int main()
{
    TestContext test;
    TestConstantsAndSample(test);
    TestRotationStability(test);
    TestBoundaryResponse(test);
    TestZoneRightClamp(test);
    TestDeterminism(test);
    TestRecenterInvariance(test);
    TestFailClosedInputs(test);
    std::printf("%u checks, %u failures\n", test.checks, test.failures);
    return test.failures ? 1 : 0;
}
