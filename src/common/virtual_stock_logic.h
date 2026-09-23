// Adapted from Gab_dC's September 20 contribution; see docs/VIRTUAL-STOCK-INTEGRATION-2026-09-23.md.
#pragma once
// Pure geometry helpers for two-hand virtual-stock direction/orientation and
// support-grab acquisition. Grab-point adjustment is intentionally separate
// from aim geometry. When two-hand aim is engaged and the F1 "Virtual stock"
// option is on, the base two-hand orientation follows the configured blend
// between the primary controller and a coherent current-frame HMD/head target,
// toward the raw support controller. This is a pure,
// dependency-free geometry helper: no globals, no locks, no logging, no
// OpenXR types, so the standalone unit-test target exercises this exact code.
//
// The helper owns direction selection, two-hand orientation construction, and
// grab-point adjustment only. Base aim position (always the primary
// controller), per-gun yaw/pitch/roll calibration, grip acquisition, and
// latching all stay in the vr.cpp aim solver. Fail closed: every
// length/rotation/output is checked with explicit std::isfinite plus epsilon
// thresholds; valid finite behavior and thresholds match the released
// controller->controller solver.
#include <algorithm>
#include <cmath>

namespace virtual_stock
{
struct Point3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
struct Quat4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};
struct DirectionSelection
{
    bool valid = false;
    bool usedVirtualStock = false;
    bool rejectedExtreme = false;
    float rejectedAgreement = 0.0f;
    Point3 direction{0.0f, 0.0f, 0.0f};
};

struct SupportEndpointSelection
{
    bool usedGrip = false;
    Point3 position{0.0f, 0.0f, 0.0f};
};

struct AdaptiveStockTriangle
{
    Point3 a{};
    Point3 b{};
    Point3 c{};
};

struct AdaptiveStockPatch
{
    Point3 topLeft{};
    Point3 topRight{};
    Point3 bottomLeft{};
    Point3 bottomRight{};
    AdaptiveStockTriangle triangles[2]{};
};

inline float Dot(Point3 a, Point3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Point3 Cross(Point3 a, Point3 b) noexcept
{
    return Point3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

inline bool Finite(Point3 p) noexcept
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

inline bool Finite(Quat4 q) noexcept
{
    return std::isfinite(q.x) && std::isfinite(q.y) &&
        std::isfinite(q.z) && std::isfinite(q.w);
}

inline bool TryDistanceSquared(Point3 a, Point3 b, float& out) noexcept
{
    if (!Finite(a) || !Finite(b))
        return false;
    const Point3 delta{a.x - b.x, a.y - b.y, a.z - b.z};
    const float distanceSquared = Dot(delta, delta);
    if (!Finite(delta) || !std::isfinite(distanceSquared) ||
        distanceSquared < 0.0f)
        return false;
    out = distanceSquared;
    return true;
}

inline Point3 RotatePoint(Quat4 q, Point3 value) noexcept
{
    const Point3 u{q.x, q.y, q.z};
    const Point3 twiceCross{
        2.0f * (u.y * value.z - u.z * value.y),
        2.0f * (u.z * value.x - u.x * value.z),
        2.0f * (u.x * value.y - u.y * value.x)};
    return Point3{
        value.x + q.w * twiceCross.x + u.y * twiceCross.z - u.z * twiceCross.y,
        value.y + q.w * twiceCross.y + u.z * twiceCross.x - u.x * twiceCross.z,
        value.z + q.w * twiceCross.z + u.x * twiceCross.y - u.y * twiceCross.x};
}

inline bool TryBuildHmdHorizontalBasis(
    Quat4 hmdOrientation, Point3 trackingUp,
    Point3& horizontalForward, Point3& horizontalRight) noexcept
{
    if (!Finite(hmdOrientation) || !Finite(trackingUp))
        return false;
    const float quaternionLengthSquared =
        hmdOrientation.x * hmdOrientation.x +
        hmdOrientation.y * hmdOrientation.y +
        hmdOrientation.z * hmdOrientation.z +
        hmdOrientation.w * hmdOrientation.w;
    const float upLengthSquared = Dot(trackingUp, trackingUp);
    if (!std::isfinite(quaternionLengthSquared) ||
        quaternionLengthSquared < 1.0e-8f ||
        !std::isfinite(upLengthSquared) || upLengthSquared < 1.0e-8f)
        return false;

    const float quaternionLength = std::sqrt(quaternionLengthSquared);
    const float upLength = std::sqrt(upLengthSquared);
    if (!std::isfinite(quaternionLength) || !std::isfinite(upLength) ||
        quaternionLength <= 0.0f || upLength <= 0.0f)
        return false;
    hmdOrientation.x /= quaternionLength;
    hmdOrientation.y /= quaternionLength;
    hmdOrientation.z /= quaternionLength;
    hmdOrientation.w /= quaternionLength;
    trackingUp.x /= upLength;
    trackingUp.y /= upLength;
    trackingUp.z /= upLength;

    const Point3 headForward = RotatePoint(hmdOrientation, {0.0f, 0.0f, -1.0f});
    if (!Finite(headForward))
        return false;
    const float verticalComponent = Dot(headForward, trackingUp);
    if (!std::isfinite(verticalComponent))
        return false;
    Point3 projected{
        headForward.x - trackingUp.x * verticalComponent,
        headForward.y - trackingUp.y * verticalComponent,
        headForward.z - trackingUp.z * verticalComponent};
    const float projectedLengthSquared = Dot(projected, projected);
    if (!Finite(projected) || !std::isfinite(projectedLengthSquared) ||
        projectedLengthSquared < 1.0e-8f)
        return false;
    const float projectedLength = std::sqrt(projectedLengthSquared);
    if (!std::isfinite(projectedLength) || projectedLength <= 0.0f)
        return false;
    horizontalForward = Point3{
        projected.x / projectedLength,
        projected.y / projectedLength,
        projected.z / projectedLength};

    Point3 right = Cross(horizontalForward, trackingUp);
    const float rightLengthSquared = Dot(right, right);
    if (!Finite(right) || !std::isfinite(rightLengthSquared) ||
        rightLengthSquared < 1.0e-8f)
        return false;
    const float rightLength = std::sqrt(rightLengthSquared);
    if (!std::isfinite(rightLength) || rightLength <= 0.0f)
        return false;
    horizontalRight = Point3{
        right.x / rightLength,
        right.y / rightLength,
        right.z / rightLength};
    return Finite(horizontalForward) && Finite(horizontalRight);
}

inline SupportEndpointSelection SelectTwoHandSupportEndpoint(
    bool useGripPose, Point3 aimPosition, bool gripPositionValid,
    Point3 gripPosition) noexcept
{
    SupportEndpointSelection result{};
    result.position = aimPosition;
    if (useGripPose && gripPositionValid && Finite(gripPosition))
    {
        result.usedGrip = true;
        result.position = gripPosition;
    }
    return result;
}

// Rear reference -> raw support-controller direction, normalized.
inline bool BuildVirtualStockDirection(Point3 rear, Point3 support, Point3& out) noexcept
{
    if (!Finite(rear) || !Finite(support))
        return false;
    const float vx = support.x - rear.x;
    const float vy = support.y - rear.y;
    const float vz = support.z - rear.z;
    const float lengthSquared = vx * vx + vy * vy + vz * vz;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-8f)
        return false;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length <= 0.0f)
        return false;
    out = Point3{vx / length, vy / length, vz / length};
    return Finite(out);
}

// Build the full rear target in OpenXR LOCAL tracking space. +Y is
// gravity-aligned up, so rearHeightM never rotates with the HMD.
inline bool BuildVirtualStockRearTarget(
    Point3 head, float rearHeightM, Point3& out) noexcept
{
    if (!Finite(head) || !std::isfinite(rearHeightM))
        return false;
    const float clampedHeight = std::clamp(rearHeightM, -0.30f, 0.10f);
    out = Point3{head.x, head.y + clampedHeight, head.z};
    return Finite(out);
}

inline bool TryBuildHmdRelativeShoulderRearTarget(
    Point3 head, Quat4 hmdOrientation, float rearHeightM,
    float shoulderBackM, float shoulderSideM, bool leftHanded,
    Point3& out) noexcept
{
    Point3 headRear{};
    if (!BuildVirtualStockRearTarget(head, rearHeightM, headRear) ||
        !std::isfinite(shoulderBackM) || !std::isfinite(shoulderSideM))
        return false;
    const float shoulderBack = std::clamp(shoulderBackM, 0.0f, 0.25f);
    const float shoulderSide = std::clamp(shoulderSideM, 0.0f, 0.20f);
    if (shoulderBack == 0.0f && shoulderSide == 0.0f)
    {
        out = headRear;
        return true;
    }

    Point3 horizontalForward{}, horizontalRight{};
    if (!TryBuildHmdHorizontalBasis(
            hmdOrientation, Point3{0.0f, 1.0f, 0.0f},
            horizontalForward, horizontalRight))
        return false;
    const float firingSideSign = leftHanded ? -1.0f : 1.0f;
    out = Point3{
        headRear.x - horizontalForward.x * shoulderBack +
            horizontalRight.x * firingSideSign * shoulderSide,
        headRear.y - horizontalForward.y * shoulderBack +
            horizontalRight.y * firingSideSign * shoulderSide,
        headRear.z - horizontalForward.z * shoulderBack +
            horizontalRight.z * firingSideSign * shoulderSide};
    return Finite(out);
}

inline bool TryBuildHmdRelativeChestRearTarget(
    Point3 head, Quat4 hmdOrientation, float chestHeightM,
    float chestBackM, float chestSideM, bool leftHanded,
    Point3& out) noexcept
{
    if (!Finite(head) || !std::isfinite(chestHeightM) ||
        !std::isfinite(chestBackM) || !std::isfinite(chestSideM))
        return false;
    const float chestHeight = std::clamp(chestHeightM, -0.50f, -0.220f);
    const float chestBack = std::clamp(chestBackM, 0.0f, 0.25f);
    const float chestSide = std::clamp(chestSideM, 0.0f, 0.20f);
    const Point3 chestBase{head.x, head.y + chestHeight, head.z};
    if (chestBack == 0.0f && chestSide == 0.0f)
    {
        out = chestBase;
        return Finite(out);
    }

    Point3 horizontalForward{}, horizontalRight{};
    if (!TryBuildHmdHorizontalBasis(
            hmdOrientation, Point3{0.0f, 1.0f, 0.0f},
            horizontalForward, horizontalRight))
        return false;
    const float firingSideSign = leftHanded ? -1.0f : 1.0f;
    out = Point3{
        chestBase.x - horizontalForward.x * chestBack +
            horizontalRight.x * firingSideSign * chestSide,
        chestBase.y - horizontalForward.y * chestBack +
            horizontalRight.y * firingSideSign * chestSide,
        chestBase.z - horizontalForward.z * chestBack +
            horizontalRight.z * firingSideSign * chestSide};
    return Finite(out);
}

inline bool TryClosestPointOnSegment(
    Point3 point, Point3 a, Point3 b, Point3& out) noexcept
{
    if (!Finite(point) || !Finite(a) || !Finite(b))
        return false;
    const Point3 edge{b.x - a.x, b.y - a.y, b.z - a.z};
    const float edgeLengthSquared = Dot(edge, edge);
    if (!Finite(edge) || !std::isfinite(edgeLengthSquared) ||
        edgeLengthSquared < 0.0f)
        return false;
    if (edgeLengthSquared < 1.0e-8f)
    {
        float distanceA = 0.0f;
        float distanceB = 0.0f;
        if (!TryDistanceSquared(point, a, distanceA) ||
            !TryDistanceSquared(point, b, distanceB))
            return false;
        out = distanceB < distanceA ? b : a;
        return Finite(out);
    }

    const Point3 fromA{point.x - a.x, point.y - a.y, point.z - a.z};
    const float projection = Dot(fromA, edge);
    if (!Finite(fromA) || !std::isfinite(projection))
        return false;
    const float t = std::clamp(projection / edgeLengthSquared, 0.0f, 1.0f);
    if (!std::isfinite(t))
        return false;
    out = Point3{
        a.x + edge.x * t,
        a.y + edge.y * t,
        a.z + edge.z * t};
    return Finite(out);
}

inline bool TryClosestPointOnDegenerateTriangle(
    Point3 point, Point3 a, Point3 b, Point3 c, Point3& out) noexcept
{
    if (!Finite(point) || !Finite(a) || !Finite(b) || !Finite(c))
        return false;

    Point3 best{};
    float bestDistanceSquared = 0.0f;
    bool found = false;
    const auto consider = [&](Point3 candidate) {
        float distanceSquared = 0.0f;
        if (!Finite(candidate) ||
            !TryDistanceSquared(point, candidate, distanceSquared))
            return;
        if (!found || distanceSquared < bestDistanceSquared)
        {
            best = candidate;
            bestDistanceSquared = distanceSquared;
            found = true;
        }
    };
    const auto considerSegment = [&](Point3 first, Point3 second) {
        Point3 candidate{};
        if (TryClosestPointOnSegment(point, first, second, candidate))
            consider(candidate);
    };
    considerSegment(a, b);
    considerSegment(b, c);
    considerSegment(c, a);
    consider(a);
    consider(b);
    consider(c);
    if (!found)
        return false;
    out = best;
    return Finite(out);
}

inline bool TryClosestPointOnTriangle(
    Point3 point, Point3 a, Point3 b, Point3 c, Point3& out) noexcept
{
    if (!Finite(point) || !Finite(a) || !Finite(b) || !Finite(c))
        return false;

    const Point3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Point3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Point3 normal = Cross(ab, ac);
    const float areaSquared = Dot(normal, normal);
    if (!Finite(ab) || !Finite(ac) || !Finite(normal) ||
        !std::isfinite(areaSquared) || areaSquared < 1.0e-12f)
    {
        return TryClosestPointOnDegenerateTriangle(point, a, b, c, out);
    }

    const Point3 ap{point.x - a.x, point.y - a.y, point.z - a.z};
    const float d1 = Dot(ab, ap);
    const float d2 = Dot(ac, ap);
    if (Finite(ap) && std::isfinite(d1) && std::isfinite(d2) &&
        d1 <= 0.0f && d2 <= 0.0f)
    {
        out = a;
        return true;
    }

    const Point3 bp{point.x - b.x, point.y - b.y, point.z - b.z};
    const Point3 bc{c.x - b.x, c.y - b.y, c.z - b.z};
    const float d3 = Dot(ab, bp);
    const float d4 = Dot(ac, bp);
    if (Finite(bp) && std::isfinite(d3) && std::isfinite(d4) &&
        d3 >= 0.0f && d4 <= d3)
    {
        out = b;
        return true;
    }

    const float vc = d1 * d4 - d3 * d2;
    if (std::isfinite(vc) && vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float denominator = d1 - d3;
        if (std::isfinite(denominator) && denominator > 0.0f)
        {
            const float v = d1 / denominator;
            if (std::isfinite(v))
            {
                out = Point3{
                    a.x + ab.x * v,
                    a.y + ab.y * v,
                    a.z + ab.z * v};
                if (Finite(out))
                    return true;
            }
        }
    }

    const Point3 cp{point.x - c.x, point.y - c.y, point.z - c.z};
    const float d5 = Dot(ab, cp);
    const float d6 = Dot(ac, cp);
    if (Finite(cp) && std::isfinite(d5) && std::isfinite(d6) &&
        d6 >= 0.0f && d5 <= d6)
    {
        out = c;
        return true;
    }

    const float vb = d5 * d2 - d1 * d6;
    if (std::isfinite(vb) && vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float denominator = d2 - d6;
        if (std::isfinite(denominator) && denominator > 0.0f)
        {
            const float w = d2 / denominator;
            if (std::isfinite(w))
            {
                out = Point3{
                    a.x + ac.x * w,
                    a.y + ac.y * w,
                    a.z + ac.z * w};
                if (Finite(out))
                    return true;
            }
        }
    }

    const float va = d3 * d6 - d5 * d4;
    if (std::isfinite(va) && va <= 0.0f &&
        (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const float denominator = (d4 - d3) + (d5 - d6);
        if (std::isfinite(denominator) && denominator > 0.0f)
        {
            const float w = (d4 - d3) / denominator;
            if (std::isfinite(w))
            {
                out = Point3{
                    b.x + bc.x * w,
                    b.y + bc.y * w,
                    b.z + bc.z * w};
                if (Finite(out))
                    return true;
            }
        }
    }

    const float denominator = va + vb + vc;
    if (std::isfinite(denominator) && denominator > 0.0f)
    {
        const float inverse = 1.0f / denominator;
        const float v = vb * inverse;
        const float w = vc * inverse;
        if (std::isfinite(v) && std::isfinite(w))
        {
            out = Point3{
                a.x + ab.x * v + ac.x * w,
                a.y + ab.y * v + ac.y * w,
                a.z + ab.z * v + ac.z * w};
            if (Finite(out))
                return true;
        }
    }

    return TryClosestPointOnDegenerateTriangle(point, a, b, c, out);
}

inline bool TryBuildAdaptiveStockPatch(
    Point3 head, Quat4 hmdOrientation, float topHeightM,
    float bottomHeightM, float topHalfWidthM, float bottomHalfWidthM,
    AdaptiveStockPatch& out) noexcept
{
    out = AdaptiveStockPatch{};
    if (!Finite(head) || !std::isfinite(topHeightM) ||
        !std::isfinite(bottomHeightM) || !std::isfinite(topHalfWidthM) ||
        !std::isfinite(bottomHalfWidthM))
        return false;

    const float topHeight = std::clamp(topHeightM, -0.35f, 0.0f);
    const float bottomHeight = std::clamp(bottomHeightM, -0.65f, -0.20f);
    const float topHalfWidth = std::clamp(topHalfWidthM, 0.02f, 0.25f);
    const float bottomHalfWidth = std::clamp(bottomHalfWidthM, 0.02f, 0.30f);
    Point3 horizontalForward{};
    Point3 horizontalRight{};
    if (!TryBuildHmdHorizontalBasis(
            hmdOrientation, Point3{0.0f, 1.0f, 0.0f},
            horizontalForward, horizontalRight))
        return false;

    const Point3 topCentre{
        head.x, head.y + topHeight, head.z};
    const Point3 bottomCentre{
        head.x, head.y + bottomHeight, head.z};
    if (!Finite(topCentre) || !Finite(bottomCentre))
        return false;

    out.topLeft = Point3{
        topCentre.x - horizontalRight.x * topHalfWidth,
        topCentre.y - horizontalRight.y * topHalfWidth,
        topCentre.z - horizontalRight.z * topHalfWidth};
    out.topRight = Point3{
        topCentre.x + horizontalRight.x * topHalfWidth,
        topCentre.y + horizontalRight.y * topHalfWidth,
        topCentre.z + horizontalRight.z * topHalfWidth};
    out.bottomLeft = Point3{
        bottomCentre.x - horizontalRight.x * bottomHalfWidth,
        bottomCentre.y - horizontalRight.y * bottomHalfWidth,
        bottomCentre.z - horizontalRight.z * bottomHalfWidth};
    out.bottomRight = Point3{
        bottomCentre.x + horizontalRight.x * bottomHalfWidth,
        bottomCentre.y + horizontalRight.y * bottomHalfWidth,
        bottomCentre.z + horizontalRight.z * bottomHalfWidth};
    if (!Finite(out.topLeft) || !Finite(out.topRight) ||
        !Finite(out.bottomLeft) || !Finite(out.bottomRight))
    {
        out = AdaptiveStockPatch{};
        return false;
    }

    out.triangles[0] = AdaptiveStockTriangle{
        out.topLeft, out.bottomLeft, out.bottomRight};
    out.triangles[1] = AdaptiveStockTriangle{
        out.topLeft, out.bottomRight, out.topRight};
    for (const AdaptiveStockTriangle& triangle : out.triangles)
    {
        if (!Finite(triangle.a) || !Finite(triangle.b) ||
            !Finite(triangle.c))
        {
            out = AdaptiveStockPatch{};
            return false;
        }
    }
    return true;
}

inline bool TrySelectAdaptiveRearTarget(
    Point3 primary, const AdaptiveStockPatch& patch, Point3& out) noexcept
{
    out = Point3{};
    if (!Finite(primary))
        return false;

    Point3 best{};
    float bestDistanceSquared = 0.0f;
    bool found = false;
    for (const AdaptiveStockTriangle& triangle : patch.triangles)
    {
        Point3 candidate{};
        if (!TryClosestPointOnTriangle(
                primary, triangle.a, triangle.b, triangle.c, candidate))
            continue;
        float distanceSquared = 0.0f;
        if (!TryDistanceSquared(primary, candidate, distanceSquared))
            continue;
        if (!found || distanceSquared < bestDistanceSquared)
        {
            best = candidate;
            bestDistanceSquared = distanceSquared;
            found = true;
        }
    }
    if (!found)
        return false;
    out = best;
    return Finite(out);
}

inline bool ShouldFallbackToHeadRearTarget(
    bool rearTargetRequested, bool rearTargetValid) noexcept
{
    return !rearTargetRequested || !rearTargetValid;
}

// Build the continuous rear reference. Full strength uses the target directly;
// interpolation is used only for a strictly intermediate strength.
inline bool BuildVirtualStockRearReference(
    Point3 primary, Point3 head, float strength, float rearHeightM,
    Point3& out) noexcept
{
    if (!std::isfinite(strength))
        return false;
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    Point3 headTarget{};
    if (!BuildVirtualStockRearTarget(head, rearHeightM, headTarget))
        return false;
    if (clampedStrength >= 1.0f)
    {
        out = headTarget;
        return true;
    }
    if (!Finite(primary))
        return false;
    if (clampedStrength <= 0.0f)
    {
        out = primary;
        return true;
    }
    out = Point3{
        primary.x + clampedStrength * (headTarget.x - primary.x),
        primary.y + clampedStrength * (headTarget.y - primary.y),
        primary.z + clampedStrength * (headTarget.z - primary.z)};
    return Finite(out);
}

// Stateless release influence for the full rear target. Invalid geometry or
// thresholds fail closed to the legacy boundary (zero stock influence).
inline float ComputeProximityInfluence(
    float distance, float fullStockDistance, float releaseDistance) noexcept
{
    if (!std::isfinite(distance) || !std::isfinite(fullStockDistance) ||
        !std::isfinite(releaseDistance) || distance < 0.0f ||
        fullStockDistance < 0.0f || releaseDistance <= fullStockDistance)
        return 0.0f;
    if (distance <= fullStockDistance)
        return 1.0f;
    if (distance >= releaseDistance)
        return 0.0f;
    const float t = std::clamp(
        (distance - fullStockDistance) /
            (releaseDistance - fullStockDistance),
        0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return std::isfinite(smooth) ? 1.0f - smooth : 0.0f;
}

// Apply the optional release policy to the configured stock strength. The
// disabled path returns immediately, so malformed thresholds cannot affect the
// existing virtual-stock behavior when the feature is off.
inline float ApplyVirtualStockProximityRelease(
    bool enabled, float configuredStrength, Point3 primary, bool headValid,
    Point3 head, float rearHeightM, float fullStockDistance,
    float releaseDistance) noexcept
{
    if (!enabled)
        return configuredStrength;
    if (!headValid || !Finite(primary))
        return 0.0f;
    Point3 rearTarget{};
    if (!BuildVirtualStockRearTarget(head, rearHeightM, rearTarget))
        return 0.0f;
    const float dx = primary.x - rearTarget.x;
    const float dy = primary.y - rearTarget.y;
    const float dz = primary.z - rearTarget.z;
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distanceSquared) || distanceSquared < 0.0f)
        return 0.0f;
    const float distance = std::sqrt(distanceSquared);
    if (!std::isfinite(distance))
        return 0.0f;
    return configuredStrength * ComputeProximityInfluence(
        distance, fullStockDistance, releaseDistance);
}

inline float ApplyVirtualStockProximityReleaseForTarget(
    bool enabled, float configuredStrength, Point3 primary,
    bool rearTargetValid, Point3 rearTarget, float fullStockDistance,
    float releaseDistance) noexcept
{
    if (!enabled)
        return configuredStrength;
    if (!rearTargetValid || !Finite(primary) || !Finite(rearTarget))
        return 0.0f;
    const float dx = primary.x - rearTarget.x;
    const float dy = primary.y - rearTarget.y;
    const float dz = primary.z - rearTarget.z;
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distanceSquared) || distanceSquared < 0.0f)
        return 0.0f;
    const float distance = std::sqrt(distanceSquared);
    if (!std::isfinite(distance))
        return 0.0f;
    return configuredStrength * ComputeProximityInfluence(
        distance, fullStockDistance, releaseDistance);
}

// Direction-selection rule. A valid positive-strength stock ray wins outright:
// the legacy primary-forward agreement test must not reject it, since that
// test exists to guard the controller->controller line and would defeat the
// head-decoupling. Anything else (option off, no coherent head, degenerate
// ray) falls back to the legacy primary -> support line with its agreement
// rejection intact. Exact zero strength always enters that legacy branch.
inline DirectionSelection SelectTwoHandAimDirection(
    bool virtualStockEnabled, float stockStrength, float rearHeightM,
    bool headValid, Point3 head, Point3 stockSupport, Point3 primary,
    Point3 legacySupport, Point3 primaryForward) noexcept
{
    DirectionSelection result{};
    if (virtualStockEnabled && std::isfinite(stockStrength) &&
        std::isfinite(rearHeightM))
    {
        const float clampedStrength = std::clamp(stockStrength, 0.0f, 1.0f);
        const float clampedHeight = std::clamp(rearHeightM, -0.30f, 0.10f);
        if (clampedStrength > 0.0f && headValid)
        {
            Point3 stock{};
            bool stockValid = false;
            // Preserve the headset-tested endpoint's exact head->support path.
            if (clampedStrength >= 1.0f && clampedHeight == 0.0f)
                stockValid = BuildVirtualStockDirection(head, stockSupport, stock);
            else
            {
                Point3 rear{};
                stockValid = BuildVirtualStockRearReference(
                    primary, head, clampedStrength, clampedHeight, rear) &&
                    BuildVirtualStockDirection(rear, stockSupport, stock);
            }
            if (stockValid)
            {
                result.valid = true;
                result.usedVirtualStock = true;
                result.direction = stock;
                return result;
            }
        }
    }
    if (!Finite(primary) || !Finite(legacySupport) || !Finite(primaryForward))
        return result;
    const float vx = legacySupport.x - primary.x;
    const float vy = legacySupport.y - primary.y;
    const float vz = legacySupport.z - primary.z;
    const float lengthSquared = vx * vx + vy * vy + vz * vz;
    if (!std::isfinite(lengthSquared))
        return result;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length < 1e-4f)
        return result;
    const Point3 direction{vx / length, vy / length, vz / length};
    if (!Finite(direction))
        return result;
    const float agreement = Dot(direction, primaryForward);
    if (!std::isfinite(agreement) || agreement < 0.35f)
    {
        result.rejectedExtreme = true;
        result.rejectedAgreement = agreement;
        return result;
    }
    result.valid = true;
    result.direction = direction;
    return result;
}

inline DirectionSelection SelectTwoHandAimDirectionForTarget(
    bool virtualStockEnabled, float stockStrength, bool rearTargetValid,
    Point3 rearTarget, Point3 stockSupport, Point3 primary,
    Point3 legacySupport, Point3 primaryForward) noexcept
{
    DirectionSelection result{};
    if (virtualStockEnabled && std::isfinite(stockStrength))
    {
        const float clampedStrength = std::clamp(stockStrength, 0.0f, 1.0f);
        if (clampedStrength > 0.0f && rearTargetValid && Finite(rearTarget))
        {
            Point3 rear{};
            bool rearValid = false;
            if (clampedStrength >= 1.0f)
            {
                rear = rearTarget;
                rearValid = true;
            }
            else if (Finite(primary))
            {
                rear = Point3{
                    primary.x + clampedStrength * (rearTarget.x - primary.x),
                    primary.y + clampedStrength * (rearTarget.y - primary.y),
                    primary.z + clampedStrength * (rearTarget.z - primary.z)};
                rearValid = Finite(rear);
            }
            if (rearValid && BuildVirtualStockDirection(
                    rear, stockSupport, result.direction))
            {
                result.valid = true;
                result.usedVirtualStock = true;
                return result;
            }
        }
    }
    if (!Finite(primary) || !Finite(legacySupport) || !Finite(primaryForward))
        return result;
    const Point3 delta{
        legacySupport.x - primary.x,
        legacySupport.y - primary.y,
        legacySupport.z - primary.z};
    const float lengthSquared = Dot(delta, delta);
    if (!std::isfinite(lengthSquared))
        return result;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length < 1.0e-4f)
        return result;
    result.direction = Point3{
        delta.x / length, delta.y / length, delta.z / length};
    if (!Finite(result.direction))
        return result;
    const float agreement = Dot(result.direction, primaryForward);
    if (!std::isfinite(agreement) || agreement < 0.35f)
    {
        result.rejectedExtreme = true;
        result.rejectedAgreement = agreement;
        return result;
    }
    result.valid = true;
    return result;
}

// Two-hand orientation construction shared by both aim lines: local weapon -Z
// resolves onto the aim direction while the primary controller's up reference
// keeps roll. HMD rotation never controls roll. Matrix->quaternion branches
// and thresholds match the released solver; do not change valid behavior.
inline bool BuildTwoHandAimOrientation(Point3 aimDirection, Point3 primaryUp, Quat4& out) noexcept
{
    if (!Finite(aimDirection) || !Finite(primaryUp))
        return false;
    Point3 xa = Cross(aimDirection, primaryUp);
    if (!Finite(xa))
        return false;
    const float xlSquared = xa.x * xa.x + xa.y * xa.y + xa.z * xa.z;
    if (!std::isfinite(xlSquared))
        return false;
    const float xl = std::sqrt(xlSquared);
    if (!std::isfinite(xl) || xl < 1e-4f)
        return false;
    xa = Point3{xa.x / xl, xa.y / xl, xa.z / xl};
    const Point3 ya = Cross(xa, aimDirection);
    if (!Finite(ya))
        return false;
    const Point3 za{-aimDirection.x, -aimDirection.y, -aimDirection.z};
    if (!Finite(za))
        return false;
    const float m00 = xa.x, m10 = xa.y, m20 = xa.z;
    const float m01 = ya.x, m11 = ya.y, m21 = ya.z;
    const float m02 = za.x, m12 = za.y, m22 = za.z;
    if (!std::isfinite(m00) || !std::isfinite(m11) || !std::isfinite(m22))
        return false;
    const float tr = m00 + m11 + m22;
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
    if (tr > 0.0f)
    {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        if (!std::isfinite(s) || s <= 0.0f)
            return false;
        qw = 0.25f * s;
        qx = (m21 - m12) / s;
        qy = (m02 - m20) / s;
        qz = (m10 - m01) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        if (!std::isfinite(s) || s <= 0.0f)
            return false;
        qw = (m21 - m12) / s;
        qx = 0.25f * s;
        qy = (m01 + m10) / s;
        qz = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        if (!std::isfinite(s) || s <= 0.0f)
            return false;
        qw = (m02 - m20) / s;
        qx = (m01 + m10) / s;
        qy = 0.25f * s;
        qz = (m12 + m21) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        if (!std::isfinite(s) || s <= 0.0f)
            return false;
        qw = (m10 - m01) / s;
        qx = (m02 + m20) / s;
        qy = (m12 + m21) / s;
        qz = 0.25f * s;
    }
    if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw))
        return false;
    const float qlSquared = qx * qx + qy * qy + qz * qz + qw * qw;
    if (!std::isfinite(qlSquared))
        return false;
    const float ql = std::sqrt(qlSquared);
    if (!std::isfinite(ql) || ql < 1e-5f)
        return false;
    out = Quat4{qx / ql, qy / ql, qz / ql, qw / ql};
    return std::isfinite(out.x) && std::isfinite(out.y) &&
        std::isfinite(out.z) && std::isfinite(out.w);
}

// Phase-B grab-acquisition helper. This is NOT aim geometry: it moves only
// the two-hand grab sample from the tracked support controller toward the
// visible palm, along the support forward vector.
//
// Strict split, by design:
//   left_hand_forward_m  -> support-hand seating / IK (never enters here)
//   left_grip_forward_m  -> two-hand grab acquisition (this function only)
//   raw support position -> actual two-hand aim geometry (solver input)
//
// Only gripForwardM participates, clamped to its [-0.05, 0.25] F1 range.
// Any non-finite input returns the raw position (today's behavior), so a bad
// sample can neither teleport the grab zone nor poison acquisition.
inline Point3 SupportGrabPoint(
    Point3 rawPosition, Point3 supportForward, float gripForwardM) noexcept
{
    if (!Finite(rawPosition) || !Finite(supportForward) ||
        !std::isfinite(gripForwardM))
        return rawPosition;
    const float clamped = std::clamp(gripForwardM, -0.05f, 0.25f);
    if (!std::isfinite(clamped))
        return rawPosition;
    const Point3 shifted{
        rawPosition.x + supportForward.x * clamped,
        rawPosition.y + supportForward.y * clamped,
        rawPosition.z + supportForward.z * clamped};
    return Finite(shifted) ? shifted : rawPosition;
}
} // namespace virtual_stock
