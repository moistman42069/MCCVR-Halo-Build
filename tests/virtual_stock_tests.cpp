#include "virtual_stock_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

void TestSupportGripHandednessRouting(TestContext& test)
{
    const Point3 physicalLeft = Point(-0.20f, 1.25f, -0.50f);
    const Point3 physicalRight = Point(0.25f, 1.35f, -0.40f);
    auto routeSupport = [](bool leftHanded,
        Point3 left, bool leftValid, Point3 right, bool rightValid) {
        if (leftHanded)
        {
            std::swap(left, right);
            std::swap(leftValid, rightValid);
        }
        return virtual_stock::SelectTwoHandSupportEndpoint(
            true, Point(0.0f, 0.0f, 0.0f), leftValid, left);
    };
    const auto rightHanded = routeSupport(
        false, physicalLeft, true, physicalRight, false);
    test.Check(rightHanded.usedGrip &&
            ApproximatelyEqual(rightHanded.position, physicalLeft),
        "right-handed routing uses the physical-left grip sample as semantic support");
    const auto rightHandedMissingSupport = routeSupport(
        false, physicalLeft, false, physicalRight, true);
    test.Check(!rightHandedMissingSupport.usedGrip,
        "right-handed routing does not mistake a primary-hand grip sample for support");

    const auto leftHanded = routeSupport(
        true, physicalLeft, false, physicalRight, true);
    test.Check(leftHanded.usedGrip &&
            ApproximatelyEqual(leftHanded.position, physicalRight),
        "left-handed routing uses the physical-right grip sample as semantic support");
    const auto leftHandedMissingSupport = routeSupport(
        true, physicalLeft, true, physicalRight, false);
    test.Check(!leftHandedMissingSupport.usedGrip,
        "left-handed routing does not mistake a primary-hand grip sample for support");
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
    TestSupportGripHandednessRouting(test);
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
    TestStrengthBoundarySemantics(test, geometry);
    TestLegacyFallbackAndRejection(test, geometry);
    TestOrientationConstruction(test, geometry);
    TestToggleStatelessness(test, geometry);
    TestInvalidInputs(test, geometry);
    TestSupportGrabPoint(test);
    TestPalmDepthDoesNotAffectAim(test, geometry);
    return test.Finish();
}
