#include "virtual_stock_logic.h"
#include "virtual_stock_neutral_capture.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
using virtual_stock::Point3;
using virtual_stock::Quat4;

Point3 Point(float x, float y, float z)
{
    return Point3{x, y, z};
}

Point3 Subtract(Point3 a, Point3 b)
{
    return Point3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Point3 Add(Point3 a, Point3 b)
{
    return Point3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Point3 Scale(Point3 value, float scale)
{
    return Point3{value.x * scale, value.y * scale, value.z * scale};
}

Point3 CrossReference(Point3 a, Point3 b)
{
    return Point3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

float DotReference(Point3 a, Point3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Point3 Midpoint(Point3 a, Point3 b)
{
    return Scale(Add(a, b), 0.5f);
}

Point3 Weighted3(Point3 a, float aWeight, Point3 b, float bWeight,
    Point3 c, float cWeight)
{
    return Add(Add(Scale(a, aWeight), Scale(b, bWeight)),
        Scale(c, cWeight));
}

float DistanceSquared(Point3 a, Point3 b)
{
    const Point3 delta = Subtract(a, b);
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}

bool IsFinitePoint(Point3 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) &&
        std::isfinite(point.z);
}

bool ApproximatelyEqual(Point3 a, Point3 b, float epsilon = 1e-5f)
{
    return std::fabs(a.x - b.x) <= epsilon &&
        std::fabs(a.y - b.y) <= epsilon &&
        std::fabs(a.z - b.z) <= epsilon;
}

// Expected geometry is calculated independently of the production helpers.
Point3 NormalizeReference(Point3 value)
{
    const double length = std::sqrt(
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z);
    return Point3{
        static_cast<float>(value.x / length),
        static_cast<float>(value.y / length),
        static_cast<float>(value.z / length)};
}

Point3 ProjectUpReference(Point3 up, Point3 aim)
{
    const double alongAim = static_cast<double>(up.x) * aim.x +
        static_cast<double>(up.y) * aim.y +
        static_cast<double>(up.z) * aim.z;
    return NormalizeReference(Point3{
        static_cast<float>(up.x - alongAim * aim.x),
        static_cast<float>(up.y - alongAim * aim.y),
        static_cast<float>(up.z - alongAim * aim.z)});
}

Point3 RotateByQuaternion(Quat4 quaternion, Point3 value)
{
    const float ux = quaternion.x;
    const float uy = quaternion.y;
    const float uz = quaternion.z;
    const float scalar = quaternion.w;
    const float dot = ux * value.x + uy * value.y + uz * value.z;
    const float crossX = uy * value.z - uz * value.y;
    const float crossY = uz * value.x - ux * value.z;
    const float crossZ = ux * value.y - uy * value.x;
    return Point3{
        value.x * (scalar * scalar - (ux * ux + uy * uy + uz * uz)) +
            2.0f * (ux * dot + scalar * crossX),
        value.y * (scalar * scalar - (ux * ux + uy * uy + uz * uz)) +
            2.0f * (uy * dot + scalar * crossY),
        value.z * (scalar * scalar - (ux * ux + uy * uy + uz * uz)) +
            2.0f * (uz * dot + scalar * crossZ)};
}

Quat4 MultiplyReference(Quat4 a, Quat4 b)
{
    return Quat4{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

bool IsFiniteUnitQuaternion(Quat4 quaternion, float epsilon = 1e-4f)
{
    if (!std::isfinite(quaternion.x) || !std::isfinite(quaternion.y) ||
        !std::isfinite(quaternion.z) || !std::isfinite(quaternion.w))
        return false;
    const double lengthSquared =
        static_cast<double>(quaternion.x) * quaternion.x +
        static_cast<double>(quaternion.y) * quaternion.y +
        static_cast<double>(quaternion.z) * quaternion.z +
        static_cast<double>(quaternion.w) * quaternion.w;
    return std::fabs(lengthSquared - 1.0) <= epsilon;
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

    void CheckNear(Point3 actual, Point3 expected, const char* message,
        float epsilon = 1e-5f)
    {
        ++checks;
        if (!ApproximatelyEqual(actual, expected, epsilon))
        {
            ++failures;
            std::fprintf(stderr,
                "FAIL: %s\n  expected: (%g, %g, %g)\n  actual:   (%g, %g, %g)\n",
                message, expected.x, expected.y, expected.z,
                actual.x, actual.y, actual.z);
        }
    }

    int Finish() const
    {
        std::printf("%u checks, %u failures\n", checks, failures);
        return failures ? 1 : 0;
    }
};

struct CommonGeometry
{
    Point3 head{Point(0.0f, 1.62f, 0.10f)};
    Point3 support{Point(0.22f, 1.38f, -0.55f)};
    Point3 primary{Point(0.34f, 1.42f, -0.18f)};
    Point3 stockDirection;
    Point3 legacyDirection;
    Point3 primaryForward;
    Point3 primaryUp{Point(0.0f, 1.0f, 0.0f)};

    CommonGeometry()
        : stockDirection(NormalizeReference(Subtract(support, head))),
          legacyDirection(NormalizeReference(Subtract(support, primary))),
          primaryForward(NormalizeReference(Point(-0.12f, -0.04f, -0.37f)))
    {
    }
};

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

void TestStockDirectionSelection(TestContext& test, const CommonGeometry& geometry)
{
    Point3 currentEndpoint{};
    test.Check(virtual_stock::BuildVirtualStockDirection(
            geometry.head, geometry.support, currentEndpoint),
        "current head-to-support endpoint builds");
    const auto stock = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    test.Check(stock.valid && stock.usedVirtualStock && !stock.rejectedExtreme,
        "stock ON + coherent HMD is valid and stock-owned");
    test.CheckNear(stock.direction, geometry.stockDirection,
        "stock direction uses headset-to-support ray");
    test.Check(stock.direction.x == currentEndpoint.x &&
            stock.direction.y == currentEndpoint.y &&
            stock.direction.z == currentEndpoint.z,
        "strength one and zero height use the exact current endpoint path");

    const auto legacy = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    test.Check(legacy.valid && !legacy.usedVirtualStock,
        "stock OFF uses legacy line");
    test.CheckNear(legacy.direction, geometry.legacyDirection,
        "stock OFF direction uses primary-to-support ray");

    const auto movedPrimary = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, Point(-0.5f, 0.9f, 0.7f),
        geometry.support, geometry.primaryForward);
    test.Check(stock.valid && movedPrimary.valid &&
            ApproximatelyEqual(stock.direction, movedPrimary.direction),
        "stock pitch/yaw independent of primary position");
}

void TestSupportEndpointSelection(TestContext& test,
    const CommonGeometry& geometry)
{
    const Point3 grip = Point(-0.10f, 1.30f, -0.62f);
    const auto off = virtual_stock::SelectTwoHandSupportEndpoint(
        false, geometry.support, true, grip);
    test.Check(!off.usedGrip && ApproximatelyEqual(off.position, geometry.support),
        "support grip endpoint OFF preserves the aim-pose support endpoint");
    const auto fresh = virtual_stock::SelectTwoHandSupportEndpoint(
        true, geometry.support, true, grip);
    test.Check(fresh.usedGrip && ApproximatelyEqual(fresh.position, grip),
        "fresh finite support grip endpoint is selected when enabled");
    const auto stale = virtual_stock::SelectTwoHandSupportEndpoint(
        true, geometry.support, false, grip);
    test.Check(!stale.usedGrip && ApproximatelyEqual(stale.position, geometry.support),
        "stale support grip endpoint falls back to aim-pose support endpoint");
    const auto unavailable = virtual_stock::SelectTwoHandSupportEndpoint(
        true, geometry.support, false, Point(0.0f, 0.0f, 0.0f));
    test.Check(!unavailable.usedGrip && ApproximatelyEqual(unavailable.position, geometry.support),
        "unavailable support grip endpoint falls back to aim-pose support endpoint");
    const auto nanGrip = virtual_stock::SelectTwoHandSupportEndpoint(
        true, geometry.support, true, Point(kNan, 0.0f, 0.0f));
    test.Check(!nanGrip.usedGrip && ApproximatelyEqual(nanGrip.position, geometry.support),
        "NaN support grip endpoint falls back to aim-pose support endpoint");
    const auto infGrip = virtual_stock::SelectTwoHandSupportEndpoint(
        true, geometry.support, true, Point(0.0f, kInf, 0.0f));
    test.Check(!infGrip.usedGrip && ApproximatelyEqual(infGrip.position, geometry.support),
        "Inf support grip endpoint falls back to aim-pose support endpoint");

    const Point3 selected = fresh.position;
    const Point3 legacyExpected = NormalizeReference(
        Subtract(selected, geometry.primary));
    const auto legacy = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, selected,
        geometry.primary, selected, legacyExpected);
    test.Check(legacy.valid && !legacy.usedVirtualStock,
        "legacy two-hand solver accepts the selected support endpoint");
    test.CheckNear(legacy.direction, legacyExpected,
        "legacy two-hand direction uses the selected support endpoint");
    const Point3 stockExpected = NormalizeReference(
        Subtract(selected, geometry.head));
    const auto stock = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, selected,
        geometry.primary, selected, Point(kNan, 0.0f, 0.0f));
    test.Check(stock.valid && stock.usedVirtualStock,
        "virtual-stock solver accepts the selected support endpoint");
    test.CheckNear(stock.direction, stockExpected,
        "virtual-stock direction uses the selected support endpoint");
}

// Free two-hand production geometry (W2): the fixed Grip -> Grip (GG) pair.
// The pair is a product rule, so it is never config-selected: nothing about the
// Virtual Stock support endpoint or "Reduce Support-Hand Rotation" enters it,
// both grips (or neither) must be usable, and the aim-position pair is the only
// fallback.
void TestFixedGripEndpointSelection(TestContext& test,
    const CommonGeometry& geometry)
{
    const Point3 primaryAim = Point(0.34f, 1.42f, -0.18f);
    const Point3 supportAim = Point(0.22f, 1.38f, -0.55f);
    const Point3 primaryGrip = Point(0.30f, 1.39f, -0.14f);
    const Point3 supportGrip = Point(0.26f, 1.40f, -0.61f);

    const auto both = virtual_stock::SelectTwoHandGripEndpoints(
        primaryAim, supportAim, true, primaryGrip, true, supportGrip);
    test.Check(both.valid && both.usedGrips,
        "GG is valid and grip-owned when both grips are committed");
    test.CheckNear(both.primary, primaryGrip,
        "GG primary pivot is the primary Grip position");
    test.CheckNear(both.support, supportGrip,
        "GG support pivot is the support Grip position");

    // The aim endpoints are not consulted at all while both grips are usable:
    // whatever the caller's support-endpoint selection produced, the pair is
    // the grips.
    const auto movedAims = virtual_stock::SelectTwoHandGripEndpoints(
        Point(9.0f, -9.0f, 9.0f), Point(-9.0f, 9.0f, -9.0f),
        true, primaryGrip, true, supportGrip);
    test.Check(movedAims.valid && movedAims.usedGrips &&
            ApproximatelyEqual(movedAims.primary, both.primary) &&
            ApproximatelyEqual(movedAims.support, both.support),
        "GG ignores both aim endpoints while both grips are usable");

    const auto missingSupport = virtual_stock::SelectTwoHandGripEndpoints(
        primaryAim, supportAim, true, primaryGrip, false, supportGrip);
    test.Check(missingSupport.valid && !missingSupport.usedGrips &&
            ApproximatelyEqual(missingSupport.primary, primaryAim) &&
            ApproximatelyEqual(missingSupport.support, supportAim),
        "a missing support grip falls back to the exact aim-position pair");

    const auto missingPrimary = virtual_stock::SelectTwoHandGripEndpoints(
        primaryAim, supportAim, false, primaryGrip, true, supportGrip);
    test.Check(missingPrimary.valid && !missingPrimary.usedGrips &&
            ApproximatelyEqual(missingPrimary.primary, primaryAim) &&
            ApproximatelyEqual(missingPrimary.support, supportAim),
        "a missing primary grip falls back to the exact aim-position pair");

    const auto noGrips = virtual_stock::SelectTwoHandGripEndpoints(
        primaryAim, supportAim, false, primaryGrip, false, supportGrip);
    test.Check(noGrips.valid && !noGrips.usedGrips,
        "no grip sample at all falls back to the aim-position pair");

    // A committed but non-finite grip is not usable. It must never be mixed
    // with the other hand's grip and never fabricated: the whole pair falls
    // back to the aim positions.
    const auto nanPrimary = virtual_stock::SelectTwoHandGripEndpoints(
        primaryAim, supportAim, true, Point(kNan, 1.39f, -0.14f),
        true, supportGrip);
    test.Check(nanPrimary.valid && !nanPrimary.usedGrips &&
            ApproximatelyEqual(nanPrimary.primary, primaryAim) &&
            ApproximatelyEqual(nanPrimary.support, supportAim),
        "a committed non-finite primary grip falls back to the aim-position pair");
    const auto infSupport = virtual_stock::SelectTwoHandGripEndpoints(
        primaryAim, supportAim, true, primaryGrip, true,
        Point(0.26f, kInf, -0.61f));
    test.Check(infSupport.valid && !infSupport.usedGrips &&
            ApproximatelyEqual(infSupport.primary, primaryAim) &&
            ApproximatelyEqual(infSupport.support, supportAim),
        "a committed non-finite support grip falls back to the aim-position pair");

    // Neither pair usable: invalid, so the caller keeps its own fail-closed
    // behaviour instead of consuming a manufactured pivot.
    const auto unusable = virtual_stock::SelectTwoHandGripEndpoints(
        Point(kNan, 0.0f, 1.42f), Point(0.0f, kInf, 0.0f), false, primaryGrip,
        false, supportGrip);
    test.Check(!unusable.valid && !unusable.usedGrips,
        "an unusable aim fallback leaves the GG selection invalid");

    // The VS-off selector consumes exactly the selected pair as the B line,
    // with the caller's primary forward as the agreement reference.
    const Point3 ggDirection =
        NormalizeReference(Subtract(supportGrip, primaryGrip));
    const auto legacy = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, both.support, both.primary,
        both.support, ggDirection);
    test.Check(legacy.valid && !legacy.usedVirtualStock,
        "the VS-off selector accepts the GG pair as its B line");
    test.CheckNear(legacy.direction, ggDirection,
        "the VS-off B direction is the Grip -> Grip line");

    // The selected pair is what makes the VS-off product geometry a real
    // differential against the pre-GG aim-line pair.
    const Point3 aimDirection =
        NormalizeReference(Subtract(supportAim, primaryAim));
    const auto aimLine = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, supportAim, primaryAim,
        supportAim, aimDirection);
    test.Check(aimLine.valid &&
            !ApproximatelyEqual(legacy.direction, aimLine.direction),
        "the GG line is a real differential against the aim-position line");
}

void TestGripHandednessRouting(TestContext& test)
{
    const Point3 physicalLeft = Point(-0.20f, 1.25f, -0.50f);
    const Point3 physicalRight = Point(0.25f, 1.35f, -0.40f);
    struct RoutedGripPositions
    {
        Point3 support{};
        bool supportValid = false;
        Point3 primary{};
        bool primaryValid = false;
    };
    auto route = [](bool leftHanded,
        Point3 leftGrip, bool leftGripValid, bool leftAimValid,
        Point3 rightGrip, bool rightGripValid, bool rightAimValid) {
        if (leftHanded)
        {
            std::swap(leftGrip, rightGrip);
            std::swap(leftGripValid, rightGripValid);
            std::swap(leftAimValid, rightAimValid);
        }
        return RoutedGripPositions{
            leftGrip, leftAimValid && leftGripValid,
            rightGrip, rightAimValid && rightGripValid};
    };
    const auto rightHanded = route(false,
        physicalLeft, true, true, physicalRight, true, true);
    test.Check(rightHanded.supportValid && rightHanded.primaryValid &&
            ApproximatelyEqual(rightHanded.support, physicalLeft) &&
            ApproximatelyEqual(rightHanded.primary, physicalRight),
        "right-handed routing maps physical left to semantic support and physical right to semantic primary");

    const auto leftHanded = route(true,
        physicalLeft, true, true, physicalRight, true, true);
    test.Check(leftHanded.supportValid && leftHanded.primaryValid &&
            ApproximatelyEqual(leftHanded.support, physicalRight) &&
            ApproximatelyEqual(leftHanded.primary, physicalLeft),
        "left-handed routing maps physical right to semantic support and physical left to semantic primary");

    const auto rightHandedMissingPrimary = route(false,
        physicalLeft, true, true, physicalRight, false, true);
    test.Check(rightHandedMissingPrimary.supportValid &&
            !rightHandedMissingPrimary.primaryValid &&
            ApproximatelyEqual(rightHandedMissingPrimary.support, physicalLeft),
        "missing right-handed primary grip does not substitute the support sample");
    const auto rightHandedMissingSupport = route(false,
        physicalLeft, false, true, physicalRight, true, true);
    test.Check(!rightHandedMissingSupport.supportValid &&
            rightHandedMissingSupport.primaryValid &&
            ApproximatelyEqual(rightHandedMissingSupport.primary, physicalRight),
        "missing right-handed support grip does not consume the primary sample");

    const auto leftHandedMissingPrimary = route(true,
        physicalLeft, false, true, physicalRight, true, true);
    test.Check(leftHandedMissingPrimary.supportValid &&
            !leftHandedMissingPrimary.primaryValid &&
            ApproximatelyEqual(leftHandedMissingPrimary.support, physicalRight),
        "missing left-handed primary grip does not substitute the support sample");
    const auto leftHandedMissingSupport = route(true,
        physicalLeft, true, true, physicalRight, false, true);
    test.Check(!leftHandedMissingSupport.supportValid &&
            leftHandedMissingSupport.primaryValid &&
            ApproximatelyEqual(leftHandedMissingSupport.primary, physicalLeft),
        "missing left-handed support grip does not consume the primary sample");

    const auto missingSupportAim = route(false,
        physicalLeft, true, false, physicalRight, true, true);
    test.Check(!missingSupportAim.supportValid &&
            missingSupportAim.primaryValid,
        "support aim invalidity does not invalidate or replace semantic primary grip");
    const auto missingPrimaryAim = route(false,
        physicalLeft, true, true, physicalRight, true, false);
    test.Check(missingPrimaryAim.supportValid &&
            !missingPrimaryAim.primaryValid,
        "primary aim invalidity does not invalidate or replace semantic support grip");
}

void TestContinuousRearReference(TestContext& test,
    const CommonGeometry& geometry)
{
    constexpr float strength = 0.5f;
    constexpr float height = -0.05f;
    const Point3 headTarget = Point(
        geometry.head.x, geometry.head.y + height, geometry.head.z);
    const Point3 expectedRear = Point(
        geometry.primary.x + strength * (headTarget.x - geometry.primary.x),
        geometry.primary.y + strength * (headTarget.y - geometry.primary.y),
        geometry.primary.z + strength * (headTarget.z - geometry.primary.z));
    Point3 rear{};
    test.Check(virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, strength, height, rear),
        "intermediate rear reference builds");
    test.CheckNear(rear, expectedRear,
        "intermediate rear reference is the expected position lerp");

    const Point3 expectedDirection = NormalizeReference(
        Subtract(geometry.support, expectedRear));
    const auto blended = virtual_stock::SelectTwoHandAimDirection(
        true, strength, height, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(blended.valid && blended.usedVirtualStock &&
            !blended.rejectedExtreme,
        "intermediate strength selects virtual-stock semantics");
    test.CheckNear(blended.direction, expectedDirection,
        "intermediate aim follows the independently calculated rear-to-support ray");

    const Point3 movedPrimary = Point(-0.25f, 1.10f, 0.30f);
    const Point3 movedExpectedRear = Point(
        movedPrimary.x + strength * (headTarget.x - movedPrimary.x),
        movedPrimary.y + strength * (headTarget.y - movedPrimary.y),
        movedPrimary.z + strength * (headTarget.z - movedPrimary.z));
    const Point3 movedExpectedDirection = NormalizeReference(
        Subtract(geometry.support, movedExpectedRear));
    const auto moved = virtual_stock::SelectTwoHandAimDirection(
        true, strength, height, true, geometry.head, geometry.support,
        movedPrimary, geometry.support, geometry.primaryForward);
    test.Check(moved.valid && moved.usedVirtualStock,
        "intermediate selection remains valid after moving primary");
    test.CheckNear(moved.direction, movedExpectedDirection,
        "intermediate selection changes with primary in the expected controlled way");
    test.Check(!ApproximatelyEqual(blended.direction, moved.direction),
        "intermediate selection is intentionally primary-position dependent");

    Point3 lowered{};
    test.Check(virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, 1.0f, -0.10f, lowered),
        "lowered full-strength rear reference builds");
    test.Check(lowered.x == geometry.head.x &&
            lowered.y == geometry.head.y - 0.10f &&
            lowered.z == geometry.head.z,
        "rear height changes only tracking-space vertical");

    // A pitched HMD would rotate local up away from tracking +Y. The helper
    // deliberately has no HMD-orientation input and uses gravity-aligned +Y.
    const float halfSqrt = std::sqrt(0.5f);
    const Point3 hmdLocalUp = RotateByQuaternion(
        Quat4{halfSqrt, 0.0f, 0.0f, halfSqrt}, Point(0.0f, 1.0f, 0.0f));
    const Point3 hypotheticalLocalOffset = Point(
        geometry.head.x + hmdLocalUp.x * -0.10f,
        geometry.head.y + hmdLocalUp.y * -0.10f,
        geometry.head.z + hmdLocalUp.z * -0.10f);
    test.Check(!ApproximatelyEqual(lowered, hypotheticalLocalOffset),
        "HMD orientation does not rotate the tracking-space height offset");

    Point3 clamped{};
    test.Check(virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, 1.0f, -0.30f, clamped) &&
            ApproximatelyEqual(clamped,
                Point(geometry.head.x, geometry.head.y - 0.30f, geometry.head.z)),
        "rear height accepts the exact -0.30 metre lower bound");
    test.Check(virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, 2.0f, -1.0f, clamped) &&
            ApproximatelyEqual(clamped,
                Point(geometry.head.x, geometry.head.y - 0.30f, geometry.head.z)),
        "strength and negative height clamp to their upper and lower endpoints");
    test.Check(virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, -2.0f, 1.0f, clamped) &&
            ApproximatelyEqual(clamped, geometry.primary),
        "negative strength clamps to the primary endpoint");
    test.Check(virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, 1.0f, 1.0f, clamped) &&
            ApproximatelyEqual(clamped,
                Point(geometry.head.x, geometry.head.y + 0.10f, geometry.head.z)),
        "positive rear height clamps to 0.10 metres");
}

void TestShoulderRearReference(TestContext& test,
    const CommonGeometry& geometry)
{
    const Quat4 identity{};
    Point3 forward{};
    Point3 right{};
    test.Check(virtual_stock::TryBuildHmdHorizontalBasis(
            identity, Point(0.0f, 1.0f, 0.0f), forward, right),
        "identity HMD builds the horizontal basis");
    test.CheckNear(forward, Point(0.0f, 0.0f, -1.0f),
        "horizontal basis forward follows HMD -Z");
    test.CheckNear(right, Point(1.0f, 0.0f, 0.0f),
        "horizontal basis right follows forward cross up");

    auto checkBasis = [&](Quat4 orientation, Point3 expectedForward,
                          Point3 expectedRight, const char* label) {
        Point3 actualForward{};
        Point3 actualRight{};
        test.Check(virtual_stock::TryBuildHmdHorizontalBasis(
                orientation, Point(0.0f, 1.0f, 0.0f),
                actualForward, actualRight), label);
        test.CheckNear(actualForward, expectedForward,
            "HMD-horizontal forward matches the requested rotation");
        test.CheckNear(actualRight, expectedRight,
            "HMD-horizontal right matches the requested rotation");
    };

    const float halfSqrt = std::sqrt(0.5f);
    const Quat4 yawPlus90{0.0f, halfSqrt, 0.0f, halfSqrt};
    const Quat4 yawMinus90{0.0f, -halfSqrt, 0.0f, halfSqrt};
    const Quat4 yaw180{0.0f, 1.0f, 0.0f, 0.0f};
    const Quat4 pitch{0.38268343f, 0.0f, 0.0f, 0.92387953f};
    const Quat4 roll{0.0f, 0.0f, 0.38268343f, 0.92387953f};
    checkBasis(yawPlus90, Point(-1.0f, 0.0f, 0.0f),
        Point(0.0f, 0.0f, -1.0f), "+90 degree HMD yaw builds the basis");
    checkBasis(yawMinus90, Point(1.0f, 0.0f, 0.0f),
        Point(0.0f, 0.0f, 1.0f), "-90 degree HMD yaw builds the basis");
    checkBasis(yaw180, Point(0.0f, 0.0f, 1.0f),
        Point(-1.0f, 0.0f, 0.0f), "180 degree HMD yaw builds the basis");
    checkBasis(pitch, Point(0.0f, 0.0f, -1.0f),
        Point(1.0f, 0.0f, 0.0f), "pure HMD pitch preserves horizontal heading");
    checkBasis(roll, Point(0.0f, 0.0f, -1.0f),
        Point(1.0f, 0.0f, 0.0f), "pure HMD roll preserves horizontal heading");
    checkBasis(MultiplyReference(yawPlus90, pitch),
        Point(-1.0f, 0.0f, 0.0f), Point(0.0f, 0.0f, -1.0f),
        "mixed HMD yaw and pitch preserves horizontal yaw");
    checkBasis(MultiplyReference(yawPlus90, roll),
        Point(-1.0f, 0.0f, 0.0f), Point(0.0f, 0.0f, -1.0f),
        "mixed HMD yaw and roll preserves horizontal yaw");

    Point3 shoulderRight{};
    test.Check(virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, identity, -0.05f, 0.08f, 0.10f, false,
            shoulderRight),
        "right-handed Shoulder target builds");
    test.CheckNear(shoulderRight,
        Point(geometry.head.x + 0.10f, geometry.head.y - 0.05f,
            geometry.head.z + 0.08f),
        "right-handed Shoulder target uses back and firing-side offsets");

    Point3 shoulderLeft{};
    test.Check(virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, identity, -0.05f, 0.08f, 0.10f, true,
            shoulderLeft),
        "left-handed Shoulder target builds");
    test.CheckNear(shoulderLeft,
        Point(geometry.head.x - 0.10f, geometry.head.y - 0.05f,
            geometry.head.z + 0.08f),
        "left-handed Shoulder target mirrors the semantic firing side");

    Point3 yawedShoulder{};
    test.Check(virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, yawPlus90, -0.05f,
            0.08f, 0.10f, false, yawedShoulder),
        "yawed HMD builds a Shoulder target");
    test.CheckNear(yawedShoulder,
        Point(geometry.head.x + 0.08f, geometry.head.y - 0.05f,
            geometry.head.z - 0.10f),
        "Shoulder offsets rotate with horizontal HMD yaw");

    Point3 pitchedShoulder{};
    test.Check(virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, pitch, -0.05f,
            0.08f, 0.10f, false, pitchedShoulder),
        "pitched HMD still builds a gravity-aligned Shoulder target");
    test.CheckNear(pitchedShoulder, shoulderRight,
        "HMD pitch and roll do not rotate the horizontal Shoulder offsets");

    Point3 zeroOffsets{};
    test.Check(virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, Quat4{kNan, 0.0f, 0.0f, 1.0f}, -0.05f,
            0.0f, 0.0f, false, zeroOffsets),
        "zero Shoulder offsets do not require a valid HMD orientation");
    test.CheckNear(zeroOffsets,
        Point(geometry.head.x, geometry.head.y - 0.05f, geometry.head.z),
        "zero Shoulder offsets exactly select the Head target");

    Point3 invalid{};
    test.Check(!virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, Quat4{kNan, 0.0f, 0.0f, 1.0f}, -0.05f,
            0.08f, 0.10f, false, invalid),
        "invalid HMD basis rejects non-zero Shoulder offsets");

    const Point3 support = Point(0.22f, 1.38f, -0.55f);
    const Point3 primary = Point(0.20f, 1.42f, 0.02f);
    const Point3 primaryForward = NormalizeReference(Point(-0.12f, -0.04f, -0.37f));
    const auto selected = virtual_stock::SelectTwoHandAimDirectionForTarget(
        true, 1.0f, true, shoulderRight, support, primary, support,
        primaryForward);
    test.Check(selected.valid && selected.usedVirtualStock,
        "target-aware selection accepts a valid Shoulder target");
    test.CheckNear(selected.direction,
        NormalizeReference(Subtract(support, shoulderRight)),
        "target-aware selection uses the same complete Shoulder target");

    const float proximity =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.75f, shoulderRight, true, shoulderRight,
            0.25f, 0.45f);
    test.Check(proximity == 0.75f,
        "target-aware proximity uses the complete Shoulder target");
    const auto invalidTarget = virtual_stock::SelectTwoHandAimDirectionForTarget(
        true, 1.0f, false, shoulderRight, support, primary, support,
        primaryForward);
    test.Check(invalidTarget.valid && !invalidTarget.usedVirtualStock,
        "invalid Shoulder target falls back to legacy selection");

    Point3 releasedShoulderTarget{};
    const bool releasedTargetValid =
        virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
            geometry.head, identity, 0.0f, 0.25f, 0.0f, false,
            releasedShoulderTarget);
    const Point3 releasedPrimary = Point(
        geometry.head.x, geometry.head.y, geometry.head.z - 0.20f);
    const Point3 rejectedSupport = Point(
        releasedPrimary.x, releasedPrimary.y, releasedPrimary.z - 1.0f);
    const Point3 opposedPrimaryForward = Point(0.0f, 0.0f, 1.0f);
    const float releasedShoulderStrength =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.95f, releasedPrimary, releasedTargetValid,
            releasedShoulderTarget, 0.25f, 0.45f);
    const auto releasedShoulderSelection =
        virtual_stock::SelectTwoHandAimDirectionForTarget(
            true, releasedShoulderStrength, releasedTargetValid,
            releasedShoulderTarget, rejectedSupport, releasedPrimary,
            rejectedSupport, opposedPrimaryForward);
    const float correspondingHeadStrength =
        virtual_stock::ApplyVirtualStockProximityRelease(
            true, 0.95f, releasedPrimary, true, geometry.head, 0.0f,
            0.25f, 0.45f);
    const auto correspondingHeadSelection =
        virtual_stock::SelectTwoHandAimDirection(
            true, correspondingHeadStrength, 0.0f, true, geometry.head,
            rejectedSupport, releasedPrimary, rejectedSupport,
            opposedPrimaryForward);
    test.Check(releasedTargetValid && releasedShoulderStrength == 0.0f,
        "valid Shoulder target reaches exact zero at the release boundary");
    test.Check(!virtual_stock::ShouldFallbackToHeadRearTarget(
            true, releasedTargetValid) &&
            !releasedShoulderSelection.valid &&
            releasedShoulderSelection.rejectedExtreme,
        "released Shoulder preserves the legacy 0.35 rejection without Head fallback");
    test.Check(correspondingHeadStrength > 0.0f &&
            correspondingHeadSelection.valid &&
            correspondingHeadSelection.usedVirtualStock,
        "the corresponding Head target would still re-engage stock");
}

void TestChestRearReference(TestContext& test,
    const CommonGeometry& geometry)
{
    const Quat4 identity{};
    const float chestHeight = -0.320f;
    Point3 centered{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, Quat4{kNan, 0.0f, 0.0f, 1.0f},
            chestHeight, 0.0f, 0.0f, false, centered),
        "centered Chest target uses the zero-horizontal fast path");
    test.CheckNear(centered,
        Point(geometry.head.x, geometry.head.y + chestHeight, geometry.head.z),
        "centered Chest target is head position plus chest height");

    Point3 lower{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, -0.450f, 0.0f, 0.0f, false, lower),
        "Chest height independently builds a lower target");
    test.CheckNear(lower,
        Point(geometry.head.x, geometry.head.y - 0.450f, geometry.head.z),
        "Chest height moves only the vertical target coordinate");

    Point3 back{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, chestHeight, 0.080f, 0.0f, false, back),
        "positive Chest back builds");
    test.CheckNear(back,
        Point(geometry.head.x, geometry.head.y + chestHeight,
            geometry.head.z + 0.080f),
        "Chest back follows the existing horizontal forward basis");

    Point3 rightSide{};
    Point3 leftSide{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, chestHeight, 0.0f, 0.015f, false,
            rightSide),
        "right-handed Chest side builds");
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, chestHeight, 0.0f, 0.015f, true,
            leftSide),
        "left-handed Chest side builds");
    test.CheckNear(rightSide,
        Point(geometry.head.x + 0.015f, geometry.head.y + chestHeight,
            geometry.head.z),
        "right-handed Chest side uses positive horizontal right");
    test.CheckNear(leftSide,
        Point(geometry.head.x - 0.015f, geometry.head.y + chestHeight,
            geometry.head.z),
        "left-handed Chest side mirrors semantic firing side");

    const float halfSqrt = std::sqrt(0.5f);
    Point3 yawed{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, Quat4{0.0f, halfSqrt, 0.0f, halfSqrt},
            chestHeight, 0.080f, 0.015f, false, yawed),
        "HMD yaw builds a Chest target");
    test.CheckNear(yawed,
        Point(geometry.head.x + 0.080f, geometry.head.y + chestHeight,
            geometry.head.z - 0.015f),
        "HMD yaw rotates Chest horizontal offsets");

    Point3 pitched{};
    Point3 rolled{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, Quat4{0.38268343f, 0.0f, 0.0f, 0.92387953f},
            chestHeight, 0.080f, 0.015f, false, pitched),
        "HMD pitch builds a Chest target");
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, Quat4{0.0f, 0.0f, 0.38268343f, 0.92387953f},
            chestHeight, 0.080f, 0.015f, false, rolled),
        "HMD roll builds a Chest target");
    const Point3 horizontalExpected{
        geometry.head.x + 0.015f, geometry.head.y + chestHeight,
        geometry.head.z + 0.080f};
    test.CheckNear(pitched, horizontalExpected,
        "HMD pitch does not tilt Chest horizontal offsets");
    test.CheckNear(rolled, horizontalExpected,
        "HMD roll does not tilt Chest horizontal offsets");

    Point3 invalidBasis{};
    test.Check(!virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, Quat4{kNan, 0.0f, 0.0f, 1.0f},
            chestHeight, 0.080f, 0.015f, false, invalidBasis),
        "invalid Chest horizontal basis fails construction");
    test.Check(!virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, kNan, 0.0f, 0.0f, false, invalidBasis),
        "non-finite Chest height fails safely");
    test.Check(!virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, chestHeight, kNan, 0.0f, false,
            invalidBasis),
        "non-finite Chest back fails safely");
    test.Check(!virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, chestHeight, 0.0f, kInf, false,
            invalidBasis),
        "non-finite Chest side fails safely");

    const Point3 aimSupport = Point(0.20f, 1.38f, -0.55f);
    const Point3 gripSupport = Point(-0.12f, 1.31f, -0.62f);
    const auto aimEndpoint = virtual_stock::SelectTwoHandSupportEndpoint(
        false, aimSupport, true, gripSupport);
    const auto gripEndpoint = virtual_stock::SelectTwoHandSupportEndpoint(
        true, aimSupport, true, gripSupport);
    Point3 compositionTarget{};
    test.Check(virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, chestHeight, 0.080f, 0.015f, false,
            compositionTarget),
        "Chest composition target builds");
    const Point3 primary = Point(
        compositionTarget.x, compositionTarget.y, compositionTarget.z + 0.35f);
    const float effectiveStrength =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.80f, primary, true, compositionTarget, 0.25f, 0.45f);
    const auto aimSelection = virtual_stock::SelectTwoHandAimDirectionForTarget(
        true, 1.0f, true, compositionTarget, aimEndpoint.position, primary,
        aimEndpoint.position, Point(0.0f, 0.0f, -1.0f));
    const auto gripSelection = virtual_stock::SelectTwoHandAimDirectionForTarget(
        true, effectiveStrength, true, compositionTarget, gripEndpoint.position,
        primary, gripEndpoint.position, Point(0.0f, 0.0f, -1.0f));
    test.Check(aimEndpoint.position.x == aimSupport.x &&
            gripEndpoint.position.x == gripSupport.x &&
            aimSelection.valid && aimSelection.usedVirtualStock &&
            gripSelection.valid && gripSelection.usedVirtualStock,
        "Chest composes with aim-pose and grip-pose support endpoints");
    test.Check(effectiveStrength > 0.0f && effectiveStrength < 0.80f,
        "Chest proximity composes with the configured strength");
    test.CheckNear(aimSelection.direction,
        NormalizeReference(Subtract(aimSupport, compositionTarget)),
        "Chest aim-pose support uses the complete Chest target");
    const Point3 blendedRear{
        primary.x + effectiveStrength * (compositionTarget.x - primary.x),
        primary.y + effectiveStrength * (compositionTarget.y - primary.y),
        primary.z + effectiveStrength * (compositionTarget.z - primary.z)};
    test.CheckNear(gripSelection.direction,
        NormalizeReference(Subtract(gripSupport, blendedRear)),
        "Chest grip-point proximity uses one target for distance and aim");

    Point3 releasedTarget{};
    const bool releasedTargetValid =
        virtual_stock::TryBuildHmdRelativeChestRearTarget(
            geometry.head, identity, -0.220f, 0.25f, 0.20f, false,
            releasedTarget);
    const Point3 headRear{
        geometry.head.x, geometry.head.y - 0.220f, geometry.head.z};
    const Point3 chestOffset = Subtract(releasedTarget, headRear);
    const Point3 chestDirection = NormalizeReference(chestOffset);
    const Point3 releasedPrimary{
        releasedTarget.x - chestDirection.x * 0.45f,
        releasedTarget.y - chestDirection.y * 0.45f,
        releasedTarget.z - chestDirection.z * 0.45f};
    const Point3 rejectedSupport{
        releasedPrimary.x, releasedPrimary.y, releasedPrimary.z - 1.0f};
    const Point3 opposedPrimaryForward{0.0f, 0.0f, 1.0f};
    const float releasedChestStrength =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.95f, releasedPrimary, releasedTargetValid,
            releasedTarget, 0.25f, 0.45f);
    const auto releasedChestSelection =
        virtual_stock::SelectTwoHandAimDirectionForTarget(
            true, releasedChestStrength, releasedTargetValid, releasedTarget,
            rejectedSupport, releasedPrimary, rejectedSupport,
            opposedPrimaryForward);
    const float correspondingHeadStrength =
        virtual_stock::ApplyVirtualStockProximityRelease(
            true, 0.95f, releasedPrimary, true, geometry.head, -0.220f,
            0.25f, 0.45f);
    const auto correspondingHeadSelection =
        virtual_stock::SelectTwoHandAimDirection(
            true, correspondingHeadStrength, -0.220f, true, geometry.head,
            rejectedSupport, releasedPrimary, rejectedSupport,
            opposedPrimaryForward);
    test.Check(releasedTargetValid && releasedChestStrength == 0.0f,
        "valid Chest target reaches exact zero at the release boundary");
    test.Check(!virtual_stock::ShouldFallbackToHeadRearTarget(
            true, releasedTargetValid) &&
            !releasedChestSelection.valid &&
            releasedChestSelection.rejectedExtreme,
        "released Chest preserves legacy rejection without Head fallback");
    test.Check(correspondingHeadStrength > 0.0f &&
            correspondingHeadSelection.valid &&
            correspondingHeadSelection.usedVirtualStock,
        "the corresponding Head target would still re-engage stock");
}

void TestAdaptivePatchGeometry(TestContext& test)
{
    const Point3 head = Point(0.0f, 1.62f, 0.10f);
    const Quat4 identity{};
    constexpr float topHeight = -0.180f;
    constexpr float bottomHeight = -0.450f;
    constexpr float topHalfWidth = 0.080f;
    constexpr float bottomHalfWidth = 0.140f;
    virtual_stock::AdaptiveStockPatch patch{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            head, identity, topHeight, bottomHeight,
            topHalfWidth, bottomHalfWidth, patch),
        "coplanar Adaptive trapezoid builds");
    test.CheckNear(patch.topLeft, Point(-0.080f, 1.44f, 0.10f),
        "Adaptive TL uses the independent top height and width");
    test.CheckNear(patch.topRight, Point(0.080f, 1.44f, 0.10f),
        "Adaptive TR mirrors TL");
    test.CheckNear(patch.bottomLeft, Point(-0.140f, 1.17f, 0.10f),
        "Adaptive BL uses the independent bottom height and width");
    test.CheckNear(patch.bottomRight, Point(0.140f, 1.17f, 0.10f),
        "Adaptive BR mirrors BL");
    test.Check(IsFinitePoint(patch.topLeft) && IsFinitePoint(patch.topRight) &&
            IsFinitePoint(patch.bottomLeft) && IsFinitePoint(patch.bottomRight),
        "Adaptive trapezoid vertices are finite");

    const Point3 normal = CrossReference(
        Subtract(patch.topRight, patch.topLeft),
        Subtract(patch.bottomLeft, patch.topLeft));
    test.Check(std::fabs(DotReference(normal,
                Subtract(patch.bottomRight, patch.topLeft))) < 1.0e-6f,
        "all Adaptive trapezoid vertices are coplanar");
    test.CheckNear(Scale(Subtract(patch.topRight, patch.topLeft), 1.0f),
        Point(0.16f, 0.0f, 0.0f),
        "Adaptive top width is exactly twice the top half-width");
    test.CheckNear(Scale(Subtract(patch.bottomRight, patch.bottomLeft), 1.0f),
        Point(0.28f, 0.0f, 0.0f),
        "Adaptive bottom width is exactly twice the bottom half-width");
    test.CheckNear(Subtract(patch.topLeft, patch.bottomLeft),
        Point(0.06f, 0.27f, 0.0f),
        "Adaptive vertical span and widening are independent");
    test.CheckNear(patch.triangles[0].a, patch.topLeft,
        "Adaptive T0 starts at TL");
    test.CheckNear(patch.triangles[0].b, patch.bottomLeft,
        "Adaptive T0 uses BL");
    test.CheckNear(patch.triangles[0].c, patch.bottomRight,
        "Adaptive T0 uses BR");
    test.CheckNear(patch.triangles[1].a, patch.topLeft,
        "Adaptive T1 starts at TL");
    test.CheckNear(patch.triangles[1].b, patch.bottomRight,
        "Adaptive T1 uses BR");
    test.CheckNear(patch.triangles[1].c, patch.topRight,
        "Adaptive T1 uses TR");

    virtual_stock::AdaptiveStockPatch sameGeometry{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            head, identity, topHeight, bottomHeight,
            topHalfWidth, bottomHalfWidth, sameGeometry),
        "Adaptive construction has no handedness input");
    test.Check(ApproximatelyEqual(patch.topLeft, sameGeometry.topLeft) &&
            ApproximatelyEqual(patch.topRight, sameGeometry.topRight) &&
            ApproximatelyEqual(patch.bottomLeft, sameGeometry.bottomLeft) &&
            ApproximatelyEqual(patch.bottomRight, sameGeometry.bottomRight),
        "Adaptive geometry is identical for both handedness modes");

    virtual_stock::AdaptiveStockPatch rectangle{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            head, identity, -0.200f, -0.400f,
            0.060f, 0.060f, rectangle),
        "equal widths produce a valid Adaptive rectangle");
    test.CheckNear(Subtract(rectangle.topRight, rectangle.topLeft),
        Point(0.12f, 0.0f, 0.0f),
        "equal widths produce equal top and bottom spans");

    virtual_stock::AdaptiveStockPatch coincidentRows{};
    Point3 selected{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            head, identity, -0.300f, -0.300f,
            0.020f, 0.020f, coincidentRows) &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                Point(0.0f, 1.30f, -0.10f), coincidentRows, selected) &&
            IsFinitePoint(selected),
        "minimum-width coincident Adaptive rows remain usable");
    virtual_stock::AdaptiveStockPatch invalidBasis{};
    test.Check(!virtual_stock::TryBuildAdaptiveStockPatch(
            head, Quat4{kNan, 0.0f, 0.0f, 1.0f},
            topHeight, bottomHeight, topHalfWidth, bottomHalfWidth,
            invalidBasis),
        "Adaptive rejects an invalid HMD basis for non-zero widths");
}

void TestAdaptiveProjection(TestContext& test)
{
    virtual_stock::AdaptiveStockPatch patch{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            Point(0.0f, 1.62f, 0.10f), Quat4{},
            -0.180f, -0.450f, 0.080f, 0.140f, patch),
        "Adaptive projection patch builds");
    const auto checkNearest = [&](Point3 primary, Point3 expected,
                                  const char* message) {
        Point3 selected{};
        test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
                primary, patch, selected), message);
        test.CheckNear(selected, expected,
            "Adaptive nearest point matches the expected feature", 1.0e-4f);
    };
    checkNearest(patch.topLeft, patch.topLeft, "Adaptive selects TL");
    checkNearest(patch.topRight, patch.topRight, "Adaptive selects TR");
    checkNearest(patch.bottomLeft, patch.bottomLeft, "Adaptive selects BL");
    checkNearest(patch.bottomRight, patch.bottomRight, "Adaptive selects BR");
    checkNearest(Midpoint(patch.topLeft, patch.topRight),
        Midpoint(patch.topLeft, patch.topRight),
        "Adaptive selects the top edge");
    checkNearest(Midpoint(patch.bottomLeft, patch.bottomRight),
        Midpoint(patch.bottomLeft, patch.bottomRight),
        "Adaptive selects the bottom edge");
    checkNearest(Midpoint(patch.topLeft, patch.bottomLeft),
        Midpoint(patch.topLeft, patch.bottomLeft),
        "Adaptive selects the left edge");
    checkNearest(Midpoint(patch.topRight, patch.bottomRight),
        Midpoint(patch.topRight, patch.bottomRight),
        "Adaptive selects the right edge");
    checkNearest(Midpoint(patch.topLeft, patch.bottomRight),
        Midpoint(patch.topLeft, patch.bottomRight),
        "Adaptive selects the internal diagonal");

    for (const auto& triangle : patch.triangles)
    {
        const Point3 interior = Weighted3(
            triangle.a, 0.20f, triangle.b, 0.30f, triangle.c, 0.50f);
        Point3 selected{};
        test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
                interior, patch, selected) && IsFinitePoint(selected),
            "Adaptive selects a finite triangle-interior point");
        test.CheckNear(selected, interior,
            "Adaptive interior projection is identity", 1.0e-4f);
    }

    Point3 above{};
    Point3 below{};
    Point3 left{};
    Point3 right{};
    Point3 forward{};
    test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
            Point(0.0f, 1.60f, 0.10f), patch, above) &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                Point(0.0f, 1.00f, 0.10f), patch, below) &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                Point(-0.30f, 1.30f, 0.10f), patch, left) &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                Point(0.30f, 1.30f, 0.10f), patch, right) &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                Point(0.0f, 1.30f, -0.20f), patch, forward) &&
            IsFinitePoint(above) && IsFinitePoint(below) &&
            IsFinitePoint(left) && IsFinitePoint(right) &&
            IsFinitePoint(forward),
        "Adaptive finite boundary projections remain valid");
    test.CheckNear(above, Midpoint(patch.topLeft, patch.topRight),
        "above Adaptive projects to the finite top boundary");
    test.CheckNear(below, Midpoint(patch.bottomLeft, patch.bottomRight),
        "below Adaptive projects to the finite bottom boundary");
    test.Check(left.x < -0.079f && right.x > 0.079f &&
            std::fabs(forward.z - 0.10f) < 1.0e-4f,
        "side and forward points project onto the finite body sheet");
}

void TestAdaptiveDiagonalContinuity(TestContext& test)
{
    virtual_stock::AdaptiveStockPatch patch{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            Point(0.0f, 1.62f, 0.10f), Quat4{},
            -0.180f, -0.450f, 0.080f, 0.140f, patch),
        "Adaptive diagonal-continuity patch builds");
    const Point3 diagonal = Subtract(patch.bottomRight, patch.topLeft);
    const Point3 perpendicular = NormalizeReference(
        Point(-diagonal.y, diagonal.x, 0.0f));
    float maximumGain = 0.0f;
    for (const float t : {0.20f, 0.50f, 0.80f})
    {
        const Point3 diagonalPoint = Add(
            patch.topLeft, Scale(diagonal, t));
        Point3 onDiagonal{};
        test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
                Add(diagonalPoint, Point(0.0f, 0.0f, -0.05f)),
                patch, onDiagonal) &&
                ApproximatelyEqual(onDiagonal, diagonalPoint, 1.0e-4f),
            "Adaptive internal diagonal has one continuous Q");

        Point3 previousPrimary{};
        Point3 previousTarget{};
        bool havePrevious = false;
        for (int sample = 0; sample <= 80; ++sample)
        {
            const float offset = -0.020f +
                static_cast<float>(sample) * 0.0005f;
            const Point3 primary = Add(
                Add(diagonalPoint, Scale(perpendicular, offset)),
                Point(0.0f, 0.0f, -0.05f));
            Point3 target{};
            test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
                    primary, patch, target),
                "Adaptive diagonal sweep remains selectable");
            if (havePrevious)
            {
                const float primaryStep = std::sqrt(DistanceSquared(
                    primary, previousPrimary));
                maximumGain = std::max(maximumGain,
                    std::sqrt(DistanceSquared(target, previousTarget)) /
                        primaryStep);
            }
            previousPrimary = primary;
            previousTarget = target;
            havePrevious = true;
        }
    }
    test.Check(maximumGain <= 1.05f,
        "Adaptive internal diagonal has no local gain spike");
}

void TestAdaptiveSemanticPrimaryRouting(TestContext& test)
{
    virtual_stock::AdaptiveStockPatch patch{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            Point(0.0f, 1.62f, 0.10f), Quat4{}, -0.180f,
            -0.450f, 0.080f, 0.140f, patch),
        "Adaptive semantic-primary routing patch builds");

    const Point3 physicalLeft{-0.20f, 1.35f, -0.10f};
    const Point3 physicalRight{0.20f, 1.35f, -0.10f};
    const auto semanticPrimary = [&](bool leftHanded) {
        return leftHanded ? physicalLeft : physicalRight;
    };
    Point3 rightHandedTarget{};
    Point3 leftHandedTarget{};
    test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
            semanticPrimary(false), patch, rightHandedTarget) &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                semanticPrimary(true), patch, leftHandedTarget),
        "Adaptive selects a target from either semantic primary controller");
    test.Check(rightHandedTarget.x >= -1.0e-4f &&
            leftHandedTarget.x <= 1.0e-4f &&
            std::fabs(rightHandedTarget.x + leftHandedTarget.x) < 1.0e-4f,
        "semantic primary routing selects mirrored bilateral targets without changing the patch");
}

void TestAdaptiveBilateralSweeps(TestContext& test)
{
    virtual_stock::AdaptiveStockPatch patch{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            Point(0.0f, 1.62f, 0.10f), Quat4{}, -0.180f,
            -0.450f, 0.080f, 0.140f, patch),
        "Adaptive gain-test patch builds");

    const auto checkPath = [&](const char* label, Point3 first,
                               Point3 last) {
        constexpr int kSamples = 241;
        Point3 previousPrimary{};
        Point3 previousTarget{};
        Point3 previousBlended{};
        bool havePrevious = false;
        float maximumQGain = 0.0f;
        float maximumBlendedGain = 0.0f;
        for (int sample = 0; sample < kSamples; ++sample)
        {
            const float t = static_cast<float>(sample) /
                static_cast<float>(kSamples - 1);
            const Point3 primary = Add(first, Scale(Subtract(last, first), t));
            Point3 target{};
            test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
                    primary, patch, target) && IsFinitePoint(target), label);
            const float strength =
                virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
                    true, 0.95f, primary, true, target, 0.270f, 0.425f);
            const Point3 blended = Add(primary,
                Scale(Subtract(target, primary), strength));
            test.Check(std::isfinite(strength) && IsFinitePoint(blended),
                "Adaptive full-strength trajectory remains finite");
            if (havePrevious)
            {
                const float primaryStep = std::sqrt(DistanceSquared(
                    primary, previousPrimary));
                const float qGain = std::sqrt(DistanceSquared(
                    target, previousTarget)) / primaryStep;
                const float blendedGain = std::sqrt(DistanceSquared(
                    blended, previousBlended)) / primaryStep;
                maximumQGain = std::max(maximumQGain, qGain);
                maximumBlendedGain = std::max(maximumBlendedGain, blendedGain);
            }
            previousPrimary = primary;
            previousTarget = target;
            previousBlended = blended;
            havePrevious = true;
        }
        test.Check(maximumQGain <= 1.05f,
            "Adaptive coplanar projection is locally non-expansive");
        test.Check(maximumBlendedGain <= 1.10f,
            "Adaptive blended rear remains locally coherent");
    };

    checkPath("Adaptive centred high-to-low path remains finite",
        Point(0.0f, 1.50f, -0.02f), Point(0.0f, 1.05f, -0.02f));
    for (const float lateral : {-0.120f, -0.080f, -0.050f, -0.020f,
                                0.020f, 0.050f, 0.080f, 0.120f})
    {
        checkPath("Adaptive off-centre high-to-low path remains finite",
            Point(lateral, 1.50f, -0.02f),
            Point(lateral, 1.05f, -0.02f));
    }
    checkPath("Adaptive left-to-right torso path remains finite",
        Point(-0.18f, 1.30f, -0.02f), Point(0.18f, 1.30f, -0.02f));
    checkPath("Adaptive right diagonal path remains finite",
        Point(0.0f, 1.50f, -0.02f), Point(0.12f, 1.30f, -0.02f));
    checkPath("Adaptive left diagonal path remains finite",
        Point(-0.12f, 1.30f, -0.02f), Point(0.0f, 1.50f, -0.02f));
}

void TestAdaptiveDegenerateGeometry(TestContext& test)
{
    const Point3 head{0.0f, 1.62f, 0.10f};
    const Point3 primary{0.0f, 1.30f, -0.10f};
    const auto checkPatch = [&](float topHeight, float bottomHeight,
                                float topHalfWidth, float bottomHalfWidth,
                                const char* message) {
        virtual_stock::AdaptiveStockPatch patch{};
        Point3 selected{};
        const bool built = virtual_stock::TryBuildAdaptiveStockPatch(
            head, Quat4{}, topHeight, bottomHeight,
            topHalfWidth, bottomHalfWidth, patch);
        const bool selectedValid = built &&
            virtual_stock::TrySelectAdaptiveRearTarget(
                primary, patch, selected);
        test.Check(selectedValid && IsFinitePoint(selected), message);
    };

    checkPatch(-0.300f, -0.300f, 0.020f, 0.020f,
        "Adaptive survives coincident minimum-width rows");
    checkPatch(-0.300f, -0.300f, 0.020f, 0.140f,
        "Adaptive survives coincident rows with different widths");
    checkPatch(-0.180f, -0.450f, 0.020f, 0.020f,
        "Adaptive survives minimum-width coverage");
    checkPatch(-0.350f, -0.200f, 0.250f, 0.300f,
        "Adaptive survives the opposite valid height ordering");

    Point3 selected{};
    test.Check(virtual_stock::TryClosestPointOnTriangle(
            Point(0.5f, 0.2f, 0.0f),
            Point(0.0f, 0.0f, 0.0f), Point(1.0f, 0.0f, 0.0f),
            Point(2.0f, 0.0f, 0.0f), selected),
        "degenerate Adaptive triangle reduces to a valid segment");
    test.CheckNear(selected, Point(0.5f, 0.0f, 0.0f),
        "degenerate Adaptive triangle selects its closest segment point");
    test.Check(virtual_stock::TryClosestPointOnTriangle(
            Point(0.3f, 0.2f, 0.0f),
            Point(0.0f, 0.0f, 0.0f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, 0.0f), selected),
        "fully collapsed Adaptive triangle reduces to an anchor");
    test.CheckNear(selected, Point(0.0f, 0.0f, 0.0f),
        "fully collapsed Adaptive triangle returns its finite anchor");

    virtual_stock::AdaptiveStockPatch invalidBasis{};
    test.Check(!virtual_stock::TryBuildAdaptiveStockPatch(
            head, Quat4{kNan, 0.0f, 0.0f, 1.0f}, -0.180f,
            -0.450f, 0.080f, 0.140f, invalidBasis),
        "Adaptive rejects an invalid HMD basis for coverage construction");
}

void TestAdaptiveProximityComposition(TestContext& test)
{
    virtual_stock::AdaptiveStockPatch patch{};
    test.Check(virtual_stock::TryBuildAdaptiveStockPatch(
            Point(0.0f, 1.62f, 0.10f), Quat4{}, -0.180f,
            -0.450f, 0.080f, 0.140f, patch),
        "Adaptive composition patch builds");

    const Point3 primary{0.060f, 1.30f, -0.020f};
    Point3 target{};
    test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
            primary, patch, target),
        "Adaptive composition selects one Q");
    const float effectiveStrength =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.95f, primary, true, target, 0.270f, 0.425f);
    const Point3 support{0.20f, 1.38f, -0.55f};
    const auto selection = virtual_stock::SelectTwoHandAimDirectionForTarget(
        true, effectiveStrength, true, target, support, primary,
        support, Point(0.0f, 0.0f, -1.0f));
    const Point3 blendedRear{
        primary.x + effectiveStrength * (target.x - primary.x),
        primary.y + effectiveStrength * (target.y - primary.y),
        primary.z + effectiveStrength * (target.z - primary.z)};
    test.Check(effectiveStrength > 0.0f && selection.valid &&
            selection.usedVirtualStock,
        "Adaptive uses positive stock strength and target-aware aiming");
    test.CheckNear(selection.direction,
        NormalizeReference(Subtract(support, blendedRear)),
        "Adaptive proximity and aiming use the same Q");

    const Point3 onPatch = Weighted3(
        patch.topLeft, 0.20f, patch.bottomLeft, 0.30f,
        patch.bottomRight, 0.50f);
    Point3 selectedOnPatch{};
    const float onPatchStrength =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.95f, onPatch, true,
            onPatch, 0.270f, 0.425f);
    test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
            onPatch, patch, selectedOnPatch) &&
            ApproximatelyEqual(selectedOnPatch, onPatch) &&
            onPatchStrength == 0.95f,
        "movement on the Adaptive sheet remains fully stocked");

    float previousStrength = 0.95f;
    for (int sample = 1; sample <= 120; ++sample)
    {
        const float depth = -0.020f - static_cast<float>(sample) * 0.005f;
        const Point3 forwardPrimary{0.060f, 1.30f, depth};
        Point3 forwardTarget{};
        test.Check(virtual_stock::TrySelectAdaptiveRearTarget(
                forwardPrimary, patch, forwardTarget),
            "forward release keeps selecting a finite Q");
        const float strength =
            virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
                true, 0.95f, forwardPrimary, true,
                forwardTarget, 0.270f, 0.425f);
        test.Check(strength <= previousStrength + 1.0e-5f,
            "forward release strength is monotonic");
        previousStrength = strength;
    }

    const Point3 forwardPrimary = Add(target, Point(0.0f, 0.0f, -0.425f));
    const float atRelease =
        virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.95f, forwardPrimary, true, target, 0.270f, 0.425f);
    const Point3 rejectedSupport{
        forwardPrimary.x, forwardPrimary.y, forwardPrimary.z - 1.0f};
    const auto rejected = virtual_stock::SelectTwoHandAimDirectionForTarget(
        true, atRelease, true, target, rejectedSupport, forwardPrimary,
        rejectedSupport, Point(0.0f, 0.0f, 1.0f));
    test.Check(atRelease == 0.0f && !virtual_stock::ShouldFallbackToHeadRearTarget(
            true, true) && !rejected.valid && rejected.rejectedExtreme,
        "Adaptive exact release preserves the legacy rejection without Head fallback");

    const Point3 furtherForward = Add(target, Point(0.0f, 0.0f, -0.60f));
    test.Check(virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
            true, 0.95f, furtherForward, true, target,
            0.270f, 0.425f) == 0.0f,
        "Adaptive forward movement remains fully released beyond the threshold");
}

void TestProximityRelease(TestContext& test,
    const CommonGeometry& geometry)
{
    Point3 fullTarget{};
    test.Check(virtual_stock::BuildVirtualStockRearTarget(
            geometry.head, -0.05f, fullTarget) &&
            ApproximatelyEqual(fullTarget,
                Point(geometry.head.x, geometry.head.y - 0.05f, geometry.head.z)),
        "proximity uses the full gravity-aligned rear target");

    test.Check(virtual_stock::ComputeProximityInfluence(
            0.25f, 0.25f, 0.45f) == 1.0f,
        "proximity is fully engaged at the full-stock distance");
    test.Check(virtual_stock::ComputeProximityInfluence(
            0.45f, 0.25f, 0.45f) == 0.0f,
        "proximity reaches exact zero at the release distance");
    test.CheckNear(Point(virtual_stock::ComputeProximityInfluence(
                0.35f, 0.25f, 0.45f), 0.0f, 0.0f),
        Point(0.5f, 0.0f, 0.0f),
        "proximity midpoint uses the smoothstep influence");
    test.Check(virtual_stock::ComputeProximityInfluence(
            0.30f, 0.25f, 0.45f) > 0.0f &&
            virtual_stock::ComputeProximityInfluence(
                0.30f, 0.25f, 0.45f) < 1.0f,
        "proximity intermediate distance remains strictly partial");
    test.Check(virtual_stock::ComputeProximityInfluence(
            0.35f, 0.45f, 0.25f) == 0.0f &&
            virtual_stock::ComputeProximityInfluence(
                0.35f, kNan, 0.45f) == 0.0f &&
            virtual_stock::ComputeProximityInfluence(
                0.35f, 0.25f, kInf) == 0.0f,
        "invalid proximity thresholds fail closed");

    const float unchanged = virtual_stock::ApplyVirtualStockProximityRelease(
        false, 0.75f, geometry.primary, false,
        Point(kNan, 0.0f, 0.0f), kNan, kNan, kInf);
    test.Check(unchanged == 0.75f,
        "proximity OFF bypasses invalid thresholds and head data entirely");
    const auto bypassAim = virtual_stock::SelectTwoHandAimDirection(
        true, unchanged, -0.05f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    const auto baselineAim = virtual_stock::SelectTwoHandAimDirection(
        true, 0.75f, -0.05f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(bypassAim.valid == baselineAim.valid &&
            bypassAim.usedVirtualStock == baselineAim.usedVirtualStock &&
            ApproximatelyEqual(bypassAim.direction, baselineAim.direction),
        "proximity OFF leaves aiming identical despite invalid thresholds");

    const float inside = virtual_stock::ApplyVirtualStockProximityRelease(
        true, 0.75f, Point(0.0f, 0.0f, 0.20f), true,
        Point(0.0f, 0.0f, 0.0f), 0.0f, 0.25f, 0.45f);
    test.Check(inside == 0.75f,
        "full-stock proximity preserves configured strength inside the inner radius");
    const float partial = virtual_stock::ApplyVirtualStockProximityRelease(
        true, 0.75f, Point(0.0f, 0.0f, 0.35f), true,
        Point(0.0f, 0.0f, 0.0f), 0.0f, 0.25f, 0.45f);
    test.CheckNear(Point(partial, 0.0f, 0.0f), Point(0.375f, 0.0f, 0.0f),
        "proximity multiplies configured strength by the smoothstep influence");

    // The release boundary deliberately crosses into the actual legacy
    // controller-to-controller solver. The legacy forward is opposed, so its
    // 0.35 rejection is observable at exact zero.
    const Point3 head = Point(0.0f, 0.0f, 0.0f);
    const Point3 support = Point(0.0f, 0.0f, -1.0f);
    const Point3 justInsidePrimary = Point(0.0f, 0.0f, 0.449f);
    const Point3 atReleasePrimary = Point(0.0f, 0.0f, 0.45f);
    const Point3 opposedForward = Point(0.0f, 0.0f, 1.0f);
    const float justInsideStrength = virtual_stock::ApplyVirtualStockProximityRelease(
        true, 1.0f, justInsidePrimary, true, head, 0.0f, 0.25f, 0.45f);
    const auto justInside = virtual_stock::SelectTwoHandAimDirection(
        true, justInsideStrength, 0.0f, true, head, support,
        justInsidePrimary, support, opposedForward);
    test.Check(justInsideStrength > 0.0f && justInside.valid &&
            justInside.usedVirtualStock && !justInside.rejectedExtreme,
        "just below release keeps positive stock semantics and bypasses legacy rejection");

    const float atReleaseStrength = virtual_stock::ApplyVirtualStockProximityRelease(
        true, 1.0f, atReleasePrimary, true, head, 0.0f, 0.25f, 0.45f);
    const auto atRelease = virtual_stock::SelectTwoHandAimDirection(
        true, atReleaseStrength, 0.0f, true, head, support,
        atReleasePrimary, support, opposedForward);
    test.Check(atReleaseStrength == 0.0f && !atRelease.valid &&
            !atRelease.usedVirtualStock && atRelease.rejectedExtreme,
        "exact release enters the legacy solver and applies its 0.35 rejection");
}

void TestHybridHorizontalRelease(TestContext& test)
{
    constexpr float full = 0.270f;
    constexpr float release = 0.425f;
    const Point3 head = Point(1.0f, 1.7f, -2.0f);
    const auto evaluate = [&](Point3 primary,
                              virtual_stock::HybridHorizontalReleaseEvaluation& details) {
        return virtual_stock::ComputeHybridHorizontalRearReleaseInfluence(
            primary, true, head, full, release, &details);
    };

    virtual_stock::HybridHorizontalReleaseEvaluation inside{};
    const float insideInfluence = evaluate(
        Point(head.x + 0.20f, -4.0f, head.z), inside);
    test.Check(inside.valid && insideInfluence == 1.0f &&
            inside.influence == 1.0f &&
            std::fabs(inside.horizontalReach - 0.20f) <= 1.0e-6f,
        "horizontal release is exact full authority inside the full radius");

    virtual_stock::HybridHorizontalReleaseEvaluation outside{};
    const float outsideInfluence = evaluate(
        Point(head.x + release, 9.0f, head.z), outside);
    test.Check(outside.valid && outsideInfluence == 0.0f &&
            outside.influence == 0.0f,
        "horizontal release is exact zero authority at the release radius");

    const float midpointReach = (full + release) * 0.5f;
    virtual_stock::HybridHorizontalReleaseEvaluation midpoint{};
    const float midpointInfluence = evaluate(
        Point(head.x, head.y, head.z - midpointReach), midpoint);
    test.Check(midpoint.valid && midpointInfluence > 0.0f &&
            midpointInfluence < 1.0f &&
            std::fabs(midpointInfluence - 0.5f) <= 1.0e-5f,
        "horizontal release reuses the production cubic proximity curve");

    float previous = 1.0f;
    bool monotonic = true;
    for (int step = 0; step <= 100; ++step)
    {
        const float reach = static_cast<float>(step) * 0.006f;
        virtual_stock::HybridHorizontalReleaseEvaluation sample{};
        const float influence = evaluate(
            Point(head.x + reach, head.y, head.z), sample);
        monotonic = monotonic && sample.valid &&
            influence <= previous + 1.0e-6f;
        previous = influence;
    }
    test.Check(monotonic,
        "horizontal release authority is monotonic with increasing X/Z reach");

    virtual_stock::HybridHorizontalReleaseEvaluation high{};
    virtual_stock::HybridHorizontalReleaseEvaluation low{};
    const float highInfluence = evaluate(
        Point(head.x + 0.33f, head.y + 5.0f, head.z), high);
    const float lowInfluence = evaluate(
        Point(head.x + 0.33f, head.y - 5.0f, head.z), low);
    test.Check(high.valid && low.valid && highInfluence == lowInfluence &&
            high.horizontalReach == low.horizontalReach,
        "horizontal release ignores vertical rear-hand displacement");

    virtual_stock::HybridHorizontalReleaseEvaluation first{};
    virtual_stock::HybridHorizontalReleaseEvaluation second{};
    const Point3 repeatedPrimary = Point(head.x - 0.31f, 0.0f, head.z);
    const float firstInfluence = evaluate(repeatedPrimary, first);
    const float secondInfluence = evaluate(repeatedPrimary, second);
    test.Check(first.valid && second.valid &&
            firstInfluence == secondInfluence &&
            first.horizontalReach == second.horizontalReach,
        "horizontal release is stateless and repeatable");

    virtual_stock::HybridHorizontalReleaseEvaluation invalid{};
    test.Check(virtual_stock::ComputeHybridHorizontalRearReleaseInfluence(
                Point(kNan, 0.0f, 0.0f), true, head,
                full, release, &invalid) == 0.0f && !invalid.valid &&
            virtual_stock::ComputeHybridHorizontalRearReleaseInfluence(
                Point(0.0f, 0.0f, 0.0f), false, head,
                full, release, &invalid) == 0.0f && !invalid.valid &&
            virtual_stock::ComputeHybridHorizontalRearReleaseInfluence(
                Point(0.0f, 0.0f, 0.0f), true, Point(kInf, 0.0f, 0.0f),
                full, release, &invalid) == 0.0f && !invalid.valid &&
            virtual_stock::ComputeHybridHorizontalRearReleaseInfluence(
                Point(0.0f, 0.0f, 0.0f), true, head,
                release, full, &invalid) == 0.0f && !invalid.valid,
        "non-finite or malformed horizontal release geometry fails to zero authority");
}

void TestStrengthBoundarySemantics(TestContext& test,
    const CommonGeometry& geometry)
{
    const Point3 opposingLegacy = Point(-geometry.legacyDirection.x,
        -geometry.legacyDirection.y, -geometry.legacyDirection.z);
    const auto zero = virtual_stock::SelectTwoHandAimDirection(
        true, 0.0f, -0.10f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, opposingLegacy);
    test.Check(!zero.valid && !zero.usedVirtualStock && zero.rejectedExtreme,
        "exact zero strength invokes legacy extreme-angle rejection");

    const auto barelyPositive = virtual_stock::SelectTwoHandAimDirection(
        true, std::numeric_limits<float>::epsilon(), 0.0f, true,
        geometry.head, geometry.support, geometry.primary, geometry.support,
        opposingLegacy);
    test.Check(barelyPositive.valid && barelyPositive.usedVirtualStock &&
            !barelyPositive.rejectedExtreme,
        "any positive strength uses stock semantics without legacy rejection");

    const auto zeroAccepted = virtual_stock::SelectTwoHandAimDirection(
        true, 0.0f, 0.10f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(zeroAccepted.valid && !zeroAccepted.usedVirtualStock,
        "zero strength selects the actual legacy solver");
    test.CheckNear(zeroAccepted.direction, geometry.legacyDirection,
        "zero strength preserves the exact legacy direction");

    const auto belowZero = virtual_stock::SelectTwoHandAimDirection(
        true, -1.0f, 0.0f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(belowZero.valid && !belowZero.usedVirtualStock &&
            ApproximatelyEqual(belowZero.direction, geometry.legacyDirection),
        "strength below range clamps to exact legacy semantics");
    const auto aboveOne = virtual_stock::SelectTwoHandAimDirection(
        true, 2.0f, 0.0f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, Point(kNan, 0.0f, 0.0f));
    test.Check(aboveOne.valid && aboveOne.usedVirtualStock &&
            ApproximatelyEqual(aboveOne.direction, geometry.stockDirection),
        "strength above range clamps to the full stock endpoint");
}

void TestLegacyFallbackAndRejection(TestContext& test,
    const CommonGeometry& geometry)
{
    const auto incoherent = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, false, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    test.Check(incoherent.valid && !incoherent.usedVirtualStock,
        "incoherent HMD falls back");
    test.CheckNear(incoherent.direction, geometry.legacyDirection,
        "incoherent HMD fallback direction uses legacy ray");

    const auto degenerateStock = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.support, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    test.Check(degenerateStock.valid && !degenerateStock.usedVirtualStock,
        "coincident HMD/support falls back to legacy, stays latched-safe");
    test.CheckNear(degenerateStock.direction, geometry.legacyDirection,
        "degenerate stock fallback direction uses legacy ray");

    const auto fullyDegenerate = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.primary, geometry.primary, geometry.primary,
        geometry.primary, geometry.primaryForward);
    test.Check(!fullyDegenerate.valid && !fullyDegenerate.rejectedExtreme,
        "zero-length rays fail closed");

    // Extreme-angle rejection belongs only to the legacy controller-to-controller path.
    const Point3 opposingLegacy = Point(-geometry.legacyDirection.x,
        -geometry.legacyDirection.y, -geometry.legacyDirection.z);
    const auto rejectedLegacy = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, opposingLegacy);
    test.Check(!rejectedLegacy.valid && rejectedLegacy.rejectedExtreme,
        "legacy opposing-forward rejects extreme");
    test.Check(rejectedLegacy.rejectedAgreement < 0.35f,
        "rejection records agreement");
    test.Check(ApproximatelyEqual(rejectedLegacy.direction, Point(0.0f, 0.0f, 0.0f)),
        "legacy Head selector keeps its zero direction on extreme rejection");

    const auto rejectedTarget = virtual_stock::SelectTwoHandAimDirectionForTarget(
        false, 1.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, opposingLegacy);
    test.Check(!rejectedTarget.valid && rejectedTarget.rejectedExtreme &&
            ApproximatelyEqual(rejectedTarget.direction,
                NormalizeReference(Subtract(geometry.support, geometry.primary))),
        "target-aware selector retains its historically normalized rejected direction");

    // A valid stock ray does not consult legacy-only primary-forward rejection geometry.
    const Point3 opposingStock = Point(-geometry.stockDirection.x,
        -geometry.stockDirection.y, -geometry.stockDirection.z);
    const auto opposingStockSelection = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, opposingStock);
    test.Check(opposingStockSelection.valid &&
            opposingStockSelection.usedVirtualStock &&
            !opposingStockSelection.rejectedExtreme,
        "stock ray survives disagreeing primary forward");
    test.CheckNear(opposingStockSelection.direction, geometry.stockDirection,
        "surviving stock ray uses headset-to-support direction");

    const auto nanForwardStock = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, Point(kNan, 0.0f, 0.0f));
    test.Check(nanForwardStock.valid && nanForwardStock.usedVirtualStock &&
            !nanForwardStock.rejectedExtreme &&
            ApproximatelyEqual(nanForwardStock.direction, geometry.stockDirection),
        "valid stock ray ignores non-finite legacy primary forward");

    const auto rejectedFallback = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.support, geometry.support, geometry.primary,
        geometry.support, opposingLegacy);
    test.Check(!rejectedFallback.valid && !rejectedFallback.usedVirtualStock &&
            rejectedFallback.rejectedExtreme &&
            rejectedFallback.rejectedAgreement < 0.35f,
        "degenerate stock ray preserves legacy extreme-angle rejection");
}

// T13 corrective: persistent-grip retained support steering. A qualified
// (engaged + trusted) invocation keeps its support-derived direction when the
// primary -> support agreement crosses below the legacy 0.35 floor; every
// other guard and every unretained path stays exactly as it was.
void TestRetainedSupportSteering(TestContext& test)
{
    const Point3 primary{0.0f, 0.0f, 0.0f};
    const Point3 forward{0.0f, 0.0f, -1.0f};
    const auto supportAtAgreement = [](float agreement) {
        const float lateral = std::sqrt(1.0f - agreement * agreement);
        return Point3{lateral, 0.0f, -agreement};
    };
    const Point3 above = supportAtAgreement(0.3501f);
    const Point3 below = supportAtAgreement(0.3499f);

    // Unretained behaviour is byte-identical at the boundary: 0.3501 passes,
    // 0.3499 rejects with the floor recorded.
    Point3 unretainedDirection{};
    bool unretainedRejected = false;
    float unretainedAgreement = 0.0f;
    test.Check(virtual_stock::TryBuildAcceptedSupportDirection(
            primary, above, forward, unretainedDirection,
            unretainedRejected, unretainedAgreement) &&
            !unretainedRejected && unretainedAgreement == 0.0f,
        "the legacy rule still accepts just above the 0.35 agreement floor");
    test.Check(virtual_stock::Dot(
            unretainedDirection, forward) > 0.35f &&
            ApproximatelyEqual(unretainedDirection,
                NormalizeReference(Subtract(above, primary))),
        "the accepted legacy direction is the exact normalized segment");
    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, below, forward, unretainedDirection,
            unretainedRejected, unretainedAgreement) &&
            unretainedRejected &&
            std::fabs(unretainedAgreement - 0.3499f) <= 1e-5f,
        "the legacy rule still rejects just below the 0.35 agreement floor");

    // Retained: the floor no longer rejects, the direction is the same exact
    // normalized segment, and the floor verdict is still reported.
    Point3 retainedDirection{};
    bool retainedRejected = false;
    float retainedAgreement = 0.0f;
    test.Check(virtual_stock::TryBuildAcceptedSupportDirection(
            primary, below, forward, retainedDirection,
            retainedRejected, retainedAgreement, true) &&
            retainedRejected &&
            std::fabs(retainedAgreement - 0.3499f) <= 1e-5f,
        "retention accepts the sub-floor direction and still records the "
        "floor verdict");
    test.Check(ApproximatelyEqual(retainedDirection,
            NormalizeReference(Subtract(below, primary))) &&
            std::fabs(std::sqrt(virtual_stock::Dot(
                retainedDirection, retainedDirection)) - 1.0f) <= 1e-5f,
        "the retained direction is the exact normalized support segment");

    // Retention skips ONLY the floor: finiteness and the minimum segment
    // length still reject.
    bool retainedRejectedAfter = true;
    float retainedAgreementAfter = 1.0f;
    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, below, Point(kNan, 0.0f, 0.0f), retainedDirection,
            retainedRejectedAfter, retainedAgreementAfter, true) &&
            !retainedRejectedAfter && retainedAgreementAfter == 0.0f,
        "retention never accepts a non-finite primary forward");
    // A finite-but-huge forward overflows the dot product: the non-finite
    // agreement guard must still reject (and record) even when retained.
    retainedRejectedAfter = false;
    retainedAgreementAfter = 0.0f;
    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, Point(0.577f, 0.577f, 0.577f),
            Point(3.0e38f, 3.0e38f, 3.0e38f), retainedDirection,
            retainedRejectedAfter, retainedAgreementAfter, true) &&
            retainedRejectedAfter && !std::isfinite(retainedAgreementAfter),
        "retention never accepts a non-finite agreement");
    retainedRejectedAfter = true;
    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, Point(kInf, 0.0f, 0.0f), forward, retainedDirection,
            retainedRejectedAfter, retainedAgreementAfter, true) &&
            !retainedRejectedAfter,
        "retention never accepts non-finite support geometry");
    retainedRejectedAfter = true;
    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, Point(primary.x + 5.0e-5f, primary.y, primary.z), forward,
            retainedDirection, retainedRejectedAfter, retainedAgreementAfter,
            true) && !retainedRejectedAfter &&
            ApproximatelyEqual(retainedDirection, Point(0.0f, 0.0f, 0.0f)),
        "retention keeps the minimum primary-to-support segment guard");

    // Selector plumbing: VS off with retention keeps the exact legacy ray and
    // flags the crossed floor; the unretained selector still rejects.
    const auto retainedSelection =
        virtual_stock::SelectTwoHandAimDirection(
            false, 1.0f, 0.0f, true, Point(0.0f, 0.0f, 0.0f), below,
            primary, below, forward, true);
    test.Check(retainedSelection.valid && !retainedSelection.usedVirtualStock &&
            retainedSelection.retainedBeyondAgreementFloor &&
            !retainedSelection.rejectedExtreme &&
            std::fabs(retainedSelection.rejectedAgreement - 0.3499f) <= 1e-5f,
        "the retained legacy selection is valid, attributed and keeps the "
        "exact crossed floor agreement for diagnostics");
    // T14-VER-03 F1: the agreement store is retention-only. An ordinary
    // in-cone acceptance must keep it at exactly zero, so the diagnostic can
    // never present a normal pass as a crossed floor.
    const auto inConeSelection =
        virtual_stock::SelectTwoHandAimDirection(
            false, 1.0f, 0.0f, true, Point(0.0f, 0.0f, 0.0f), above,
            primary, above, forward, true);
    test.Check(inConeSelection.valid && !inConeSelection.usedVirtualStock &&
            !inConeSelection.retainedBeyondAgreementFloor &&
            !inConeSelection.rejectedExtreme &&
            inConeSelection.rejectedAgreement == 0.0f,
        "an ordinary in-cone selection keeps the retention store at zero");
    test.CheckNear(retainedSelection.direction,
        NormalizeReference(Subtract(below, primary)),
        "the retained legacy selection preserves the exact support direction");
    const auto unretainedSelection =
        virtual_stock::SelectTwoHandAimDirection(
            false, 1.0f, 0.0f, true, Point(0.0f, 0.0f, 0.0f), below,
            primary, below, forward);
    test.Check(!unretainedSelection.valid &&
            unretainedSelection.rejectedExtreme &&
            !unretainedSelection.retainedBeyondAgreementFloor &&
            unretainedSelection.rejectedAgreement < 0.35f,
        "the unretained legacy selection still rejects the sub-floor ray");

    // VS-on stock paths ignore the retention request entirely: a valid stock
    // ray wins outright and never reports a retained selection.
    const Point3 stockSupport{0.55f, 0.30f, -0.80f};
    const auto stockWithRetention =
        virtual_stock::SelectTwoHandAimDirection(
            true, 1.0f, 0.0f, true, Point(0.0f, 1.62f, 0.0f), stockSupport,
            primary, below, Point(-1.0f, 0.0f, 0.0f), true);
    test.Check(stockWithRetention.valid && stockWithRetention.usedVirtualStock &&
            !stockWithRetention.retainedBeyondAgreementFloor &&
            !stockWithRetention.rejectedExtreme,
        "a valid stock ray is unaffected by the retention request");
    test.CheckNear(stockWithRetention.direction,
        NormalizeReference(Subtract(stockSupport, Point(0.0f, 1.62f, 0.0f))),
        "the stock ray keeps its head-to-support direction");
}

void TestHybridPureHelpers(TestContext& test)
{
    const Point3 primary{0.0f, 0.0f, 0.0f};
    const Point3 primaryForward{0.0f, 0.0f, -1.0f};
    const Point3 support{0.6f, 0.0f, -1.0f};
    Point3 offhand{};
    bool rejectedExtreme = false;
    float rejectedAgreement = 0.0f;
    test.Check(virtual_stock::TryBuildAcceptedSupportDirection(
            primary, support, primaryForward, offhand,
            rejectedExtreme, rejectedAgreement) &&
            !rejectedExtreme && ApproximatelyEqual(
                offhand, NormalizeReference(Subtract(support, primary))),
        "Hybrid B accepts an ordinary support translation through the legacy rule");

    const Point3 intermediateExpected = NormalizeReference(Point(
        primaryForward.x * 0.5f + offhand.x * 0.5f,
        primaryForward.y * 0.5f + offhand.y * 0.5f,
        primaryForward.z * 0.5f + offhand.z * 0.5f));
    Point3 blended{};
    test.Check(virtual_stock::TryBlendDirectionAuthority(
            primaryForward, offhand, 0.0f, blended) &&
            blended.x == primaryForward.x && blended.y == primaryForward.y &&
            blended.z == primaryForward.z,
        "Hybrid influence zero returns the exact primary direction");
    test.Check(virtual_stock::TryBlendDirectionAuthority(
            primaryForward, offhand, 0.5f, blended) &&
            ApproximatelyEqual(blended, intermediateExpected),
        "Hybrid intermediate influence uses normalized direction-domain blending");
    test.Check(virtual_stock::TryBlendDirectionAuthority(
            primaryForward, offhand, 1.0f, blended) &&
            blended.x == offhand.x && blended.y == offhand.y &&
            blended.z == offhand.z,
        "Hybrid influence one returns the exact accepted support direction");

    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, Point(1.0f, 0.0f, 0.0f), primaryForward, blended,
            rejectedExtreme, rejectedAgreement) && rejectedExtreme &&
            rejectedAgreement < 0.35f,
        "Hybrid B preserves the existing 0.35 agreement rejection");
    test.Check(!virtual_stock::TryBuildAcceptedSupportDirection(
            primary, primary, primaryForward, blended,
            rejectedExtreme, rejectedAgreement) && !rejectedExtreme,
        "Hybrid B rejects a degenerate primary-to-support segment");

    const Point3 target{0.0f, 0.0f, 0.0f};
    const Point3 stockSupport{0.0f, 0.0f, -1.0f};
    Point3 stock{};
    test.Check(!virtual_stock::TryBuildHybridStockDirection(
            primary, target, stockSupport, 0.0f, stock),
        "Hybrid strength zero makes C unavailable");
    test.Check(virtual_stock::TryBuildHybridStockDirection(
            primary, target, stockSupport, 1.0f, stock) &&
            ApproximatelyEqual(stock, Point(0.0f, 0.0f, -1.0f)),
        "Hybrid strength one builds C directly from T to S");
    const Point3 intermediateTarget{0.0f, 0.0f, 1.0f};
    const Point3 intermediatePrimary{0.0f, 0.0f, 0.0f};
    test.Check(virtual_stock::TryBuildHybridStockDirection(
            intermediatePrimary, intermediateTarget, stockSupport, 0.5f, stock) &&
            ApproximatelyEqual(stock, Point(0.0f, 0.0f, -1.0f)),
        "Hybrid intermediate strength uses R between P and T");
    test.Check(!virtual_stock::TryBuildHybridStockDirection(
            primary, target, stockSupport, kNan, stock),
        "Hybrid C rejects non-finite strength");

    const float full = 0.050f;
    const float release = 0.150f;
    test.Check(virtual_stock::ComputeHybridSeatInfluence(
            Point(0.0f, 0.0f, -0.50f), target, stockSupport,
            full, release) == 1.0f,
        "Hybrid W is one for a primary hand seated on T-to-S");
    test.CheckNear(Point(virtual_stock::ComputeHybridSeatInfluence(
                Point(0.10f, 0.0f, 0.0f), target, stockSupport,
                full, release), 0.0f, 0.0f),
        Point(0.5f, 0.0f, 0.0f),
        "Hybrid W uses the smooth full-to-release fade");
    test.Check(virtual_stock::ComputeHybridSeatInfluence(
            Point(0.0f, 0.0f, 0.05f), target, stockSupport,
            full, release) == 1.0f &&
            virtual_stock::ComputeHybridSeatInfluence(
                Point(0.0f, 0.0f, -1.15f), target, stockSupport,
                full, release) == 0.0f,
        "Hybrid W clamps projections before T and beyond S to the segment endpoints");
    test.Check(virtual_stock::ComputeHybridSeatInfluence(
            Point(0.0f, 0.0f, -0.50f), target, target,
            full, release) == 0.0f,
        "Hybrid W fails closed for a degenerate T-to-S segment");
    test.Check(virtual_stock::ComputeHybridSeatInfluence(
            Point(kNan, 0.0f, 0.0f), target, stockSupport,
            full, release) == 0.0f &&
            virtual_stock::ComputeHybridSeatInfluence(
                primary, Point(kInf, 0.0f, 0.0f), stockSupport,
                full, release) == 0.0f,
        "Hybrid W fails closed for non-finite seating geometry");

    const float lowReady = virtual_stock::ComputeHybridSeatInfluence(
        Point(0.0f, 0.0f, -0.15f), target,
        Point(0.0f, 0.0f, -0.30f), full, release);
    test.Check(lowReady == 1.0f,
        "Collinear low-ready geometry can produce high W; no posture gate is invented");

    Point3 convergedB{};
    Point3 convergedC{};
    test.Check(virtual_stock::TryBuildAcceptedSupportDirection(
            primary, stockSupport, primaryForward, convergedB,
            rejectedExtreme, rejectedAgreement) &&
            virtual_stock::TryBuildHybridStockDirection(
                primary, primary, stockSupport, 1.0f, convergedC) &&
            ApproximatelyEqual(convergedB, convergedC),
        "Hybrid B and full-strength C converge for the same geometric ray");

    test.Check(!virtual_stock::TryBlendDirectionAuthority(
            primaryForward, Point(0.0f, 0.0f, 1.0f), 0.5f, blended),
        "Invalid opposing direction blend fails closed for caller fallback to A or HipAim");
}

void TestHybridRichEvaluatorEquivalence(TestContext& test)
{
    struct StockCase
    {
        Point3 primary;
        Point3 target;
        Point3 support;
        float strength;
    };
    const StockCase stockCases[] = {
        {Point(0.1f, 0.0f, 0.0f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 1.0f},
        {Point(0.1f, 0.0f, 0.0f), Point(0.0f, 0.0f, 1.0f),
            Point(0.0f, 0.0f, -1.0f), 0.5f},
        {Point(0.0f, 0.0f, 0.0f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, 0.0f), 1.0f},
        {Point(kNan, 0.0f, 0.0f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 1.0f},
        {Point(0.0f, 0.0f, 0.0f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 0.0f},
    };
    for (const StockCase& item : stockCases)
    {
        const virtual_stock::HybridStockEvaluation rich =
            virtual_stock::EvaluateHybridStock(
                item.primary, item.target, item.support, item.strength);
        Point3 compatibilityOutput{};
        const bool compatibilityValid =
            virtual_stock::TryBuildHybridStockDirection(
                item.primary, item.target, item.support, item.strength,
                compatibilityOutput);
        test.Check(rich.valid == compatibilityValid,
            "Rich Hybrid stock evaluator preserves compatibility validity");
        if (compatibilityValid)
        {
            test.Check(rich.direction.x == compatibilityOutput.x &&
                    rich.direction.y == compatibilityOutput.y &&
                    rich.direction.z == compatibilityOutput.z,
                "Rich Hybrid stock evaluator preserves exact compatibility output");
        }
    }

    struct SeatCase
    {
        Point3 primary;
        Point3 target;
        Point3 support;
        float full;
        float release;
    };
    const SeatCase seatCases[] = {
        {Point(0.0f, 0.0f, -0.5f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 0.05f, 0.15f},
        {Point(0.1f, 0.0f, -0.5f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 0.05f, 0.15f},
        {Point(0.0f, 0.0f, 0.1f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 0.05f, 0.15f},
        {Point(0.0f, 0.0f, -0.5f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, 0.0f), 0.05f, 0.15f},
        {Point(kInf, 0.0f, 0.0f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 0.05f, 0.15f},
    };
    for (const SeatCase& item : seatCases)
    {
        const virtual_stock::HybridSeatEvaluation rich =
            virtual_stock::EvaluateHybridSeat(
                item.primary, item.target, item.support,
                item.full, item.release);
        const float compatibility = virtual_stock::ComputeHybridSeatInfluence(
            item.primary, item.target, item.support,
            item.full, item.release);
        test.Check(rich.influence == compatibility,
            "Rich Hybrid seat evaluator preserves exact compatibility influence");
    }

    const virtual_stock::HybridSeatEvaluation known =
        virtual_stock::EvaluateHybridSeat(
            Point(0.1f, 0.0f, -0.5f), Point(0.0f, 0.0f, 0.0f),
            Point(0.0f, 0.0f, -1.0f), 0.05f, 0.15f);
    test.Check(known.valid && known.segmentLength == 1.0f &&
            known.rawProjection == 0.5f &&
            known.clampedProjection == 0.5f &&
            ApproximatelyEqual(known.closest, Point(0.0f, 0.0f, -0.5f)) &&
            std::fabs(known.seatError - 0.1f) <= 1.0e-6f &&
            std::fabs(known.influence - 0.5f) <= 1.0e-6f,
        "Rich Hybrid seat evaluator exposes the expected geometric intermediates");
}

struct HybridSyntheticResult
{
    bool bAccepted = false;
    bool cAvailable = false;
    bool active = false;
    bool finalValid = false;
    float w = 0.0f;
    Point3 hip{};
    Point3 stock{};
    Point3 final{};
};

HybridSyntheticResult EvaluateHybridSynthetic(
    Point3 primary, Point3 target, Point3 support,
    float strength = 1.0f, float offhandInfluence = 0.50f,
    float fullSeatDistance = 0.050f,
    float releaseSeatDistance = 0.150f)
{
    HybridSyntheticResult result{};
    const Point3 primaryForward = Point(0.0f, 0.0f, -1.0f);
    bool rejectedExtreme = false;
    float rejectedAgreement = 0.0f;
    Point3 offhand{};
    result.bAccepted = virtual_stock::TryBuildAcceptedSupportDirection(
        primary, support, primaryForward, offhand,
        rejectedExtreme, rejectedAgreement);
    result.hip = primaryForward;
    if (result.bAccepted && !virtual_stock::TryBlendDirectionAuthority(
            primaryForward, offhand, offhandInfluence, result.hip))
    {
        result.hip = primaryForward;
    }
    result.cAvailable = virtual_stock::TryBuildHybridStockDirection(
        primary, target, support, strength, result.stock);
    if (result.cAvailable)
    {
        result.w = virtual_stock::ComputeHybridSeatInfluence(
            primary, target, support, fullSeatDistance, releaseSeatDistance);
    }
    result.final = result.hip;
    bool stockContributed = false;
    if (result.cAvailable && result.w > 0.0f &&
        virtual_stock::TryBlendDirectionAuthority(
            result.hip, result.stock, result.w, result.final))
    {
        stockContributed = true;
    }
    result.finalValid = virtual_stock::Finite(result.final);
    result.active = result.bAccepted || stockContributed;
    return result;
}

bool IsFiniteUnitDirection(Point3 direction, float epsilon = 1.0e-4f)
{
    if (!virtual_stock::Finite(direction))
        return false;
    const float lengthSquared = virtual_stock::Dot(direction, direction);
    return std::isfinite(lengthSquared) &&
        std::fabs(lengthSquared - 1.0f) <= epsilon;
}

void TestHybridGeometricRegressions(TestContext& test)
{
    const Point3 target = Point(0.0f, 0.0f, -0.50f);
    const Point3 support = Point(0.0f, 0.0f, -1.00f);
    const float rearHandPerturbations[] = {
        0.001f, 0.005f, 0.010f, 0.020f};
    constexpr size_t kRearHandPerturbationCount =
        sizeof(rearHandPerturbations) / sizeof(rearHandPerturbations[0]);
    HybridSyntheticResult perturbations[kRearHandPerturbationCount]{};
    for (size_t i = 0; i < kRearHandPerturbationCount; ++i)
    {
        const float lateralOffset = 0.050f + rearHandPerturbations[i];
        perturbations[i] = EvaluateHybridSynthetic(
            Point(lateralOffset, 0.0f, -0.75f), target, support);
        test.Check(perturbations[i].finalValid &&
                IsFiniteUnitDirection(perturbations[i].final) &&
                std::isfinite(perturbations[i].w),
            "Hybrid rear-hand millimetre perturbation remains finite and normalized");
        if (i > 0)
        {
            test.Check(perturbations[i].w <= perturbations[i - 1].w,
                "Hybrid rear-hand perturbation changes W monotonically away from the seat");
            test.Check(virtual_stock::Dot(
                    perturbations[i - 1].final, perturbations[i].final) > 0.0f,
                "Hybrid rear-hand perturbation does not cross a discontinuous direction branch");
        }
    }

    const float sweepHeights[] = {
        -0.20f, -0.12f, -0.10f, -0.05f, 0.0f,
        -0.05f, -0.10f, -0.12f, -0.20f};
    constexpr size_t kSweepHeightCount =
        sizeof(sweepHeights) / sizeof(sweepHeights[0]);
    HybridSyntheticResult sweep[kSweepHeightCount]{};
    for (size_t i = 0; i < kSweepHeightCount; ++i)
    {
        sweep[i] = EvaluateHybridSynthetic(
            Point(0.0f, sweepHeights[i], -0.75f), target, support);
        test.Check(sweep[i].finalValid && IsFiniteUnitDirection(sweep[i].final) &&
                sweep[i].w >= 0.0f && sweep[i].w <= 1.0f,
            "Hybrid hip/raise/seat/lower sweep remains finite and bounded");
    }
    for (size_t i = 1; i <= 4; ++i)
    {
        test.Check(sweep[i].w >= sweep[i - 1].w,
            "Hybrid seating W rises continuously toward the seat");
    }
    for (size_t i = 5; i < kSweepHeightCount; ++i)
    {
        test.Check(sweep[i].w <= sweep[i - 1].w,
            "Hybrid seating W falls continuously after the seat");
    }
    for (size_t i = 0; i < kSweepHeightCount; ++i)
    {
        test.Check(sweep[i].w == sweep[kSweepHeightCount - 1 - i].w &&
                ApproximatelyEqual(
                    sweep[i].final, sweep[kSweepHeightCount - 1 - i].final),
            "Hybrid seating sweep is stateless and reverses through the same curve");
    }

    const Point3 compactPrimary = Point(0.0f, 0.0f, 0.0f);
    const Point3 compactDegenerate = Point(0.00005f, 0.0f, -0.00005f);
    const HybridSyntheticResult degenerate = EvaluateHybridSynthetic(
        compactPrimary, compactDegenerate, compactDegenerate);
    test.Check(!degenerate.bAccepted && !degenerate.cAvailable &&
            !degenerate.active && degenerate.finalValid &&
            IsFiniteUnitDirection(degenerate.final) &&
            ApproximatelyEqual(degenerate.final, Point(0.0f, 0.0f, -1.0f)),
        "Compact degenerate geometry fails closed without leaking B or C authority");

    const Point3 compactSupport = Point(0.00020f, 0.0f, -0.00020f);
    const HybridSyntheticResult compact = EvaluateHybridSynthetic(
        compactPrimary, Point(0.0f, 0.0f, -0.00050f), compactSupport);
    test.Check(compact.bAccepted && compact.cAvailable && compact.active &&
            compact.finalValid && IsFiniteUnitDirection(compact.final),
        "Compact but valid geometry stays finite without a special pistol branch");
}

void TestOrientationConstruction(TestContext& test,
    const CommonGeometry& geometry)
{
    Quat4 orientation{};
    test.Check(virtual_stock::BuildTwoHandAimOrientation(
            geometry.stockDirection, geometry.primaryUp, orientation),
        "orientation builds for stock direction");
    test.CheckNear(RotateByQuaternion(orientation, Point(0.0f, 0.0f, -1.0f)),
        geometry.stockDirection, "local -Z resolves to selected direction", 1e-4f);
    test.Check(IsFiniteUnitQuaternion(orientation),
        "successful orientation is finite and unit length");

    const Point3 tiltedUp = NormalizeReference(Point(0.3f, 1.0f, 0.1f));
    Quat4 primaryUpOrientation{};
    Quat4 tiltedUpOrientation{};
    test.Check(virtual_stock::BuildTwoHandAimOrientation(
            geometry.stockDirection, geometry.primaryUp, primaryUpOrientation),
        "orientation builds (up A)");
    test.Check(virtual_stock::BuildTwoHandAimOrientation(
            geometry.stockDirection, tiltedUp, tiltedUpOrientation),
        "orientation builds (up B)");
    test.CheckNear(
        RotateByQuaternion(primaryUpOrientation, Point(0.0f, 0.0f, -1.0f)),
        geometry.stockDirection, "up A keeps local -Z on selected direction", 1e-4f);
    test.CheckNear(
        RotateByQuaternion(tiltedUpOrientation, Point(0.0f, 0.0f, -1.0f)),
        geometry.stockDirection, "up B keeps local -Z on selected direction", 1e-4f);

    const Point3 rollAxisA = RotateByQuaternion(
        primaryUpOrientation, Point(0.0f, 1.0f, 0.0f));
    const Point3 rollAxisB = RotateByQuaternion(
        tiltedUpOrientation, Point(0.0f, 1.0f, 0.0f));
    test.CheckNear(rollAxisA,
        ProjectUpReference(geometry.primaryUp, geometry.stockDirection),
        "local +Y follows primary-controller up A", 1e-4f);
    test.CheckNear(rollAxisB,
        ProjectUpReference(tiltedUp, geometry.stockDirection),
        "local +Y follows primary-controller up B", 1e-4f);
    test.Check(!ApproximatelyEqual(rollAxisA, rollAxisB, 1e-4f),
        "different up references produce different roll around the common aim axis");

    test.Check(!virtual_stock::BuildTwoHandAimOrientation(
            Point(0.0f, 1.0f, 0.0f), Point(0.0f, 1.0f, 0.0f), orientation),
        "parallel aim and up vectors fail closed");

    const auto blended = virtual_stock::SelectTwoHandAimDirection(
        true, 0.5f, -0.05f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    Quat4 blendedOrientation{};
    test.Check(blended.valid && virtual_stock::BuildTwoHandAimOrientation(
            blended.direction, tiltedUp, blendedOrientation),
        "intermediate direction uses the unchanged orientation builder");
    test.CheckNear(RotateByQuaternion(
            blendedOrientation, Point(0.0f, 0.0f, -1.0f)),
        blended.direction,
        "intermediate orientation keeps local -Z on its selected direction", 1e-4f);
    test.CheckNear(RotateByQuaternion(
            blendedOrientation, Point(0.0f, 1.0f, 0.0f)),
        ProjectUpReference(tiltedUp, blended.direction),
        "intermediate orientation keeps roll owned by primary up", 1e-4f);
}

void TestToggleStatelessness(TestContext& test, const CommonGeometry& geometry)
{
    const auto firstOn = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    const auto off = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    const auto secondOn = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    test.Check(firstOn.valid && !off.usedVirtualStock && secondOn.valid &&
            ApproximatelyEqual(firstOn.direction, secondOn.direction) &&
            ApproximatelyEqual(off.direction, geometry.legacyDirection),
        "toggle is stateless, no re-grab state");
}

void TestInvalidInputs(TestContext& test, const CommonGeometry& geometry)
{
    Point3 direction{};
    Point3 rear{};
    test.Check(!virtual_stock::BuildVirtualStockDirection(
            Point(kNan, 0.0f, 0.0f), geometry.support, direction),
        "NaN head fails closed");
    test.Check(!virtual_stock::BuildVirtualStockDirection(
            geometry.head, Point(kInf, 0.0f, 0.0f), direction),
        "Inf support fails closed");
    test.Check(!virtual_stock::BuildVirtualStockDirection(
            geometry.head, geometry.head, direction),
        "zero-length stock ray fails closed");
    test.Check(!virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, kNan, 0.0f, rear),
        "NaN runtime strength fails rear-reference construction");
    test.Check(!virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, geometry.head, 0.5f, kInf, rear),
        "infinite runtime height fails rear-reference construction");
    test.Check(!virtual_stock::BuildVirtualStockRearReference(
            geometry.primary, Point(kInf, 0.0f, 0.0f), 0.5f, 0.0f, rear),
        "non-finite runtime head fails rear-reference construction");

    const auto stockFallback = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, Point(kNan, 0.0f, 0.0f), geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(stockFallback.valid && !stockFallback.usedVirtualStock,
        "NaN head falls back to finite legacy");

    const auto nanStrengthFallback = virtual_stock::SelectTwoHandAimDirection(
        true, kNan, 0.0f, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(nanStrengthFallback.valid &&
            !nanStrengthFallback.usedVirtualStock &&
            ApproximatelyEqual(nanStrengthFallback.direction,
                geometry.legacyDirection),
        "NaN runtime strength falls back to exact legacy selection");
    const auto infHeightFallback = virtual_stock::SelectTwoHandAimDirection(
        true, 0.5f, kInf, true, geometry.head, geometry.support,
        geometry.primary, geometry.support, geometry.primaryForward);
    test.Check(infHeightFallback.valid && !infHeightFallback.usedVirtualStock &&
            ApproximatelyEqual(infHeightFallback.direction,
                geometry.legacyDirection),
        "infinite runtime height falls back to exact legacy selection");

    const auto invalidLegacy = virtual_stock::SelectTwoHandAimDirection(
        false, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, Point(kNan, 0.0f, 0.0f));
    test.Check(!invalidLegacy.valid, "NaN forward fails legacy closed");

    Quat4 orientation{};
    test.Check(!virtual_stock::BuildTwoHandAimOrientation(
            Point(0.0f, 0.0f, 0.0f), geometry.primaryUp, orientation),
        "zero aim direction fails closed");
    test.Check(!virtual_stock::BuildTwoHandAimOrientation(
            geometry.stockDirection, Point(0.0f, 0.0f, 0.0f), orientation),
        "zero up fails closed");
    test.Check(!virtual_stock::BuildTwoHandAimOrientation(
            Point(kInf, 0.0f, 0.0f), geometry.primaryUp, orientation),
        "Inf aim direction fails closed");
}

Quat4 AxisAngle(Point3 axis, float radians)
{
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return Quat4{axis.x * sine, axis.y * sine, axis.z * sine,
        std::cos(half)};
}

void TestHybridInverseNeck(TestContext& test)
{
    constexpr float pi = 3.14159265358979323846f;
    const Point3 d{0.0f, 0.040f, -0.100f};
    const Point3 neck{0.3f, 1.4f, -0.2f};
    const Quat4 neutral = AxisAngle(Point(0.0f, 1.0f, 0.0f), -0.2f);
    const Quat4 yaw = AxisAngle(Point(0.0f, 1.0f, 0.0f), 0.6f);
    const Quat4 pitch = AxisAngle(Point(1.0f, 0.0f, 0.0f), -0.4f);
    const Quat4 orientations[]{neutral, yaw, pitch};
    for (const Quat4 orientation : orientations)
    {
        const Point3 head = Add(neck, RotateByQuaternion(orientation, d));
        const auto corrected = virtual_stock::EvaluateHybridInverseNeck(
            head, orientation, neutral, 1.0f, 0.100f, 0.040f, 0.0f);
        test.Check(corrected.valid,
            "rigid-orbit inverse-neck geometry is valid");
        test.CheckNear(corrected.correctedHead,
            Add(neck, RotateByQuaternion(neutral, d)),
            "k=1 removes rotational orbit across yaw and pitch", 2.0e-5f);
    }

    const Point3 head = Add(neck, RotateByQuaternion(yaw, d));
    const auto zero = virtual_stock::EvaluateHybridInverseNeck(
        head, yaw, neutral, 0.0f, 0.100f, 0.040f, 0.0f);
    test.Check(zero.valid && zero.correctedHead.x == head.x &&
            zero.correctedHead.y == head.y && zero.correctedHead.z == head.z,
        "k=0 preserves the raw head position exactly");
    const auto sameHalf = virtual_stock::EvaluateHybridInverseNeck(
        head, yaw, yaw, 0.5f, 0.100f, 0.040f, 0.0f);
    const auto sameFull = virtual_stock::EvaluateHybridInverseNeck(
        head, yaw, yaw, 1.0f, 0.100f, 0.040f, 0.0f);
    test.Check(sameHalf.valid && sameFull.valid &&
            ApproximatelyEqual(sameHalf.appliedCorrection, {}) &&
            ApproximatelyEqual(sameFull.appliedCorrection, {}) &&
            ApproximatelyEqual(sameFull.correctedHead, head),
        "Q equal to Q0 yields zero correction at half and full strength");

    const auto half = virtual_stock::EvaluateHybridInverseNeck(
        head, yaw, neutral, 0.5f, 0.100f, 0.040f, 0.0f);
    test.CheckNear(half.appliedCorrection,
        Scale(half.predictedOrbit, 0.5f),
        "k=0.5 applies exactly half the unclamped orbit");

    const Point3 translation{0.7f, -0.2f, 0.4f};
    const Point3 translatedHead = Add(head, translation);
    const auto translated = virtual_stock::EvaluateHybridInverseNeck(
        translatedHead, yaw, neutral, 1.0f, 0.100f, 0.040f, 0.0f);
    const auto untranslated = virtual_stock::EvaluateHybridInverseNeck(
        head, yaw, neutral, 1.0f, 0.100f, 0.040f, 0.0f);
    test.CheckNear(Subtract(translated.correctedHead,
            untranslated.correctedHead), translation,
        "pure body translation with constant Q is preserved exactly");
    test.CheckNear(translated.correctedHead,
        Add(Add(neck, translation), RotateByQuaternion(neutral, d)),
        "combined body translation and head rotation preserves translation");

    const auto signEquivalent = virtual_stock::EvaluateHybridInverseNeck(
        head, Quat4{-yaw.x, -yaw.y, -yaw.z, -yaw.w}, neutral,
        1.0f, 0.100f, 0.040f, 0.0f);
    const auto signReference = virtual_stock::EvaluateHybridInverseNeck(
        head, yaw, neutral, 1.0f, 0.100f, 0.040f, 0.0f);
    test.CheckNear(signEquivalent.correctedHead, signReference.correctedHead,
        "quaternion sign-equivalent current orientations produce equal correction");

    const auto convention = virtual_stock::EvaluateHybridInverseNeck(
        Point(0.0f, 0.0f, 0.0f), Quat4{}, Quat4{},
        1.0f, 0.100f, 0.040f, 0.0f);
    test.Check(convention.valid &&
            ApproximatelyEqual(convention.currentOffset,
                Point(0.0f, 0.040f, -0.100f)),
        "semantic positive forward uses OpenXR local -Z");

    const auto invalid = virtual_stock::EvaluateHybridInverseNeck(
        head, Quat4{kNan, 0.0f, 0.0f, 1.0f}, neutral,
        1.0f, 0.100f, 0.040f, 0.0f);
    test.Check(!invalid.valid && ApproximatelyEqual(invalid.correctedHead, head) &&
            IsFinitePoint(invalid.correctedHead),
        "non-finite correction input falls back to finite raw HMD position");

    const auto clamped = virtual_stock::EvaluateHybridInverseNeck(
        Point(0.0f, 0.0f, 0.0f),
        AxisAngle(Point(0.0f, 1.0f, 0.0f), pi), Quat4{},
        1.0f, 0.100f, 0.040f, 0.0f);
    const float appliedLength = std::sqrt(
        DistanceSquared(clamped.appliedCorrection, {}));
    test.Check(clamped.valid && clamped.correctionClamped &&
            std::fabs(appliedLength -
                kVirtualStockHybridInverseNeckCorrectionCapM) <= 1.0e-5f,
        "inverse-neck safety clamp bounds correction at 0.15 metres");
}

void TestInverseNeckNeutralCapture(TestContext& test)
{
    using virtual_stock::AdvanceInverseNeckNeutralCapture;
    using virtual_stock::InverseNeckNeutralCaptureInput;
    using virtual_stock::InverseNeckNeutralCaptureState;
    InverseNeckNeutralCaptureState state{};
    const Quat4 q0 = AxisAngle(Point(0.0f, 1.0f, 0.0f), 0.25f);
    auto sample = [&](uint64_t serial, bool family, bool menu, bool suitable,
                      uint64_t epoch, Quat4 orientation,
                      bool pending = false) {
        AdvanceInverseNeckNeutralCapture(state,
            InverseNeckNeutralCaptureInput{family, menu, suitable, pending,
                serial, epoch, orientation});
    };
    sample(1, true, true, true, 4, q0);
    test.Check(state.captureArmed && !state.neutralValid,
        "entering inverse-neck family in F1 arms without capturing");
    sample(2, true, false, false, 4, q0);
    test.Check(state.captureArmed && !state.neutralValid,
        "unfocused or invalid post-menu sample cannot capture neutral");
    sample(3, true, false, true, 4, q0);
    test.Check(state.neutralValid && !state.captureArmed &&
            state.captureSerial == 3 && state.captureContactSpaceEpoch == 4,
        "first suitable post-menu frame captures Q0 and stamps serial and epoch");
    const auto retained = state;
    sample(4, true, false, true, 4,
        AxisAngle(Point(1.0f, 0.0f, 0.0f), 0.8f));
    sample(5, true, false, true, 4, Quat4{});
    test.Check(std::memcmp(&state.neutralOrientation,
                   &retained.neutralOrientation, sizeof(Quat4)) == 0 &&
            state.captureSerial == retained.captureSerial &&
            state.captureContactSpaceEpoch ==
                retained.captureContactSpaceEpoch,
        "switches within inverse-neck family retain byte-identical neutral state");
    sample(6, true, false, true, 5, q0);
    test.Check(!state.neutralValid && state.captureArmed,
        "reference-space epoch change invalidates and defers neutral recapture");
    sample(7, true, false, true, 5, q0, true);
    test.Check(!state.neutralValid,
        "pending reference-space transition blocks neutral capture");
    sample(8, true, false, true, 5, q0);
    test.Check(state.neutralValid && state.captureSerial == 8 &&
            state.captureContactSpaceEpoch == 5,
        "next suitable stable reference-space frame deliberately recaptures");
    const auto beforePresentationRecenter = state;
    sample(9, true, true, true, 5, q0);
    test.Check(state.neutralValid && !state.captureArmed &&
            state.captureSerial == beforePresentationRecenter.captureSerial &&
            std::memcmp(&state.neutralOrientation,
                &beforePresentationRecenter.neutralOrientation,
                sizeof(Quat4)) == 0,
        "ordinary presentation recenter/menu activity does not invalidate Q0");
    sample(10, false, false, true, 5, q0);
    test.Check(!state.familyActive && !state.captureArmed &&
            !state.neutralValid && state.captureSerial == 0,
        "leaving inverse-neck family clears neutral state deliberately");
}

void TestSupportGrabPoint(TestContext& test)
{
    const Point3 raw = Point(1.0f, 2.0f, 3.0f);
    const Point3 forward = Point(0.0f, 0.0f, -1.0f);

    const Point3 atDefault = virtual_stock::SupportGrabPoint(raw, forward, 0.097f);
    test.CheckNear(Subtract(atDefault, raw), Point(0.0f, 0.0f, -0.097f),
        "grab sample advances along support forward by palm depth");

    const Point3 below = virtual_stock::SupportGrabPoint(raw, forward, -1.0f);
    test.CheckNear(Subtract(below, raw), Point(0.0f, 0.0f, 0.05f),
        "palm depth below range clamps to -0.05");

    const Point3 above = virtual_stock::SupportGrabPoint(raw, forward, 1.0f);
    test.CheckNear(Subtract(above, raw), Point(0.0f, 0.0f, -0.25f),
        "palm depth above range clamps to 0.25");

    test.CheckNear(virtual_stock::SupportGrabPoint(raw, forward, 0.0f), raw,
        "zero palm depth keeps the raw point");
    test.CheckNear(virtual_stock::SupportGrabPoint(raw, forward, kNan), raw,
        "NaN palm depth keeps the raw point");
    test.CheckNear(virtual_stock::SupportGrabPoint(raw, forward, kInf), raw,
        "Inf palm depth keeps the raw point");

    const Point3 nanPosition = virtual_stock::SupportGrabPoint(
        Point(kNan, 0.0f, 0.0f), forward, 0.1f);
    test.Check(std::isnan(nanPosition.x) && nanPosition.y == 0.0f &&
            nanPosition.z == 0.0f,
        "non-finite raw position is returned unchanged");

    const Point3 infPosition = virtual_stock::SupportGrabPoint(
        Point(kInf, 0.0f, 0.0f), forward, 0.1f);
    test.Check(std::isinf(infPosition.x) && infPosition.x > 0.0f &&
            infPosition.y == 0.0f && infPosition.z == 0.0f,
        "infinite raw position is returned unchanged");

    test.CheckNear(virtual_stock::SupportGrabPoint(
            raw, Point(kInf, 0.0f, 0.0f), 0.1f), raw,
        "infinite support forward keeps the raw point");
    test.CheckNear(virtual_stock::SupportGrabPoint(
            raw, Point(kNan, 0.0f, 0.0f), 0.1f), raw,
        "NaN support forward keeps the raw point");
}

void TestPalmDepthDoesNotAffectAim(TestContext& test,
    const CommonGeometry& geometry)
{
    // Palm-depth adjustment is acquisition-only; aim always uses raw support position.
    const Point3 movedGrab = virtual_stock::SupportGrabPoint(
        geometry.support, Point(0.0f, 0.0f, -1.0f), 0.25f);
    test.Check(!ApproximatelyEqual(movedGrab, geometry.support),
        "palm depth moves the acquisition point");

    const auto rawSelection = virtual_stock::SelectTwoHandAimDirection(
        true, 1.0f, 0.0f, true, geometry.head, geometry.support, geometry.primary,
        geometry.support, geometry.primaryForward);
    test.Check(rawSelection.valid &&
            ApproximatelyEqual(rawSelection.direction, geometry.stockDirection),
        "aim selection from the raw point is unchanged by palm depth");
}
}

int main()
{
    TestContext test;
    const CommonGeometry geometry;
    TestStockDirectionSelection(test, geometry);
    TestSupportEndpointSelection(test, geometry);
    TestFixedGripEndpointSelection(test, geometry);
    TestGripHandednessRouting(test);
    TestContinuousRearReference(test, geometry);
    TestShoulderRearReference(test, geometry);
    TestChestRearReference(test, geometry);
    TestAdaptivePatchGeometry(test);
    TestAdaptiveProjection(test);
    TestAdaptiveDiagonalContinuity(test);
    TestAdaptiveSemanticPrimaryRouting(test);
    TestAdaptiveBilateralSweeps(test);
    TestAdaptiveDegenerateGeometry(test);
    TestAdaptiveProximityComposition(test);
    TestProximityRelease(test, geometry);
    TestHybridHorizontalRelease(test);
    TestStrengthBoundarySemantics(test, geometry);
    TestLegacyFallbackAndRejection(test, geometry);
    TestRetainedSupportSteering(test);
    TestHybridPureHelpers(test);
    TestHybridRichEvaluatorEquivalence(test);
    TestHybridGeometricRegressions(test);
    TestOrientationConstruction(test, geometry);
    TestToggleStatelessness(test, geometry);
    TestHybridInverseNeck(test);
    TestInverseNeckNeutralCapture(test);
    TestInvalidInputs(test, geometry);
    TestSupportGrabPoint(test);
    TestPalmDepthDoesNotAffectAim(test, geometry);
    return test.Finish();
}
