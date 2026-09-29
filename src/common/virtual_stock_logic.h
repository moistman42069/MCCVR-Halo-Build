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
#include "virtual_stock_settings.h"

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
    // Persistent support grip corrective (T13): true only when `valid` was
    // produced by a retained support-steering invocation whose primary ->
    // support agreement crossed below the legacy 0.35 floor. The floor verdict
    // itself stays recorded in rejectedAgreement, so the acceptance is never
    // presented as an ordinary in-cone pass.
    bool retainedBeyondAgreementFloor = false;
    Point3 direction{0.0f, 0.0f, 0.0f};
};

struct SupportEndpointSelection
{
    bool usedGrip = false;
    Point3 position{0.0f, 0.0f, 0.0f};
};

struct HybridStockEvaluation
{
    bool valid = false;
    bool virtualRearValid = false;
    Point3 virtualRear{};
    Point3 direction{};
};

struct HybridSeatEvaluation
{
    bool valid = false;
    float segmentLength = 0.0f;
    float rawProjection = 0.0f;
    float clampedProjection = 0.0f;
    Point3 closest{};
    float seatError = 0.0f;
    float influence = 0.0f;
};

struct HybridHorizontalReleaseEvaluation
{
    bool valid = false;
    float horizontalReach = 0.0f;
    float influence = 0.0f;
};

struct HybridInverseNeckEvaluation
{
    bool valid = false;
    bool correctionClamped = false;
    Point3 currentOffset{};
    Point3 neutralOffset{};
    Point3 predictedOrbit{};
    Point3 clampedOrbit{};
    Point3 appliedCorrection{};
    Point3 correctedHead{};
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

inline bool TryNormalizeQuaternion(Quat4 value, Quat4& out) noexcept
{
    if (!Finite(value))
        return false;
    const float lengthSquared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f)
        return false;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length <= 0.0f)
        return false;
    out = Quat4{value.x / length, value.y / length,
        value.z / length, value.w / length};
    return Finite(out);
}

// Neutral-preserving inverse-neck diagnostic. Semantic local forward is -Z,
// matching the existing OpenXR aiming convention.
inline HybridInverseNeckEvaluation EvaluateHybridInverseNeck(
    Point3 head, Quat4 currentOrientation, Quat4 neutralOrientation,
    float strength, float forwardM, float upM, float lateralM) noexcept
{
    HybridInverseNeckEvaluation result{};
    result.correctedHead = head;
    if (!Finite(head) || !std::isfinite(strength) ||
        !std::isfinite(forwardM) || !std::isfinite(upM) ||
        !std::isfinite(lateralM))
        return result;

    Quat4 current{};
    Quat4 neutral{};
    if (!TryNormalizeQuaternion(currentOrientation, current) ||
        !TryNormalizeQuaternion(neutralOrientation, neutral))
        return result;

    const float k = std::clamp(strength,
        kVirtualStockHybridInverseNeckStrengthMinimum,
        kVirtualStockHybridInverseNeckStrengthMaximum);
    const Point3 localOffset{
        std::clamp(lateralM,
            kVirtualStockHybridInverseNeckLateralMinimumM,
            kVirtualStockHybridInverseNeckLateralMaximumM),
        std::clamp(upM, kVirtualStockHybridInverseNeckUpMinimumM,
            kVirtualStockHybridInverseNeckUpMaximumM),
        -std::clamp(forwardM,
            kVirtualStockHybridInverseNeckForwardMinimumM,
            kVirtualStockHybridInverseNeckForwardMaximumM)};
    result.currentOffset = RotatePoint(current, localOffset);
    result.neutralOffset = RotatePoint(neutral, localOffset);
    result.predictedOrbit = Point3{
        result.currentOffset.x - result.neutralOffset.x,
        result.currentOffset.y - result.neutralOffset.y,
        result.currentOffset.z - result.neutralOffset.z};
    const float orbitLengthSquared = Dot(result.predictedOrbit,
        result.predictedOrbit);
    if (!Finite(result.currentOffset) || !Finite(result.neutralOffset) ||
        !Finite(result.predictedOrbit) || !std::isfinite(orbitLengthSquared) ||
        orbitLengthSquared < 0.0f)
        return result;
    const float orbitLength = std::sqrt(orbitLengthSquared);
    if (!std::isfinite(orbitLength))
        return result;
    result.clampedOrbit = result.predictedOrbit;
    if (orbitLength > kVirtualStockHybridInverseNeckCorrectionCapM)
    {
        const float scale = kVirtualStockHybridInverseNeckCorrectionCapM /
            orbitLength;
        result.clampedOrbit = Point3{
            result.predictedOrbit.x * scale,
            result.predictedOrbit.y * scale,
            result.predictedOrbit.z * scale};
        result.correctionClamped = true;
    }
    result.appliedCorrection = Point3{
        result.clampedOrbit.x * k,
        result.clampedOrbit.y * k,
        result.clampedOrbit.z * k};
    result.correctedHead = k <= 0.0f ? head : Point3{
        head.x - result.appliedCorrection.x,
        head.y - result.appliedCorrection.y,
        head.z - result.appliedCorrection.z};
    result.valid = Finite(result.clampedOrbit) &&
        Finite(result.appliedCorrection) && Finite(result.correctedHead);
    if (!result.valid)
        result = HybridInverseNeckEvaluation{false, false, {}, {}, {}, {}, {}, head};
    return result;
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

// Free two-hand production geometry: Grip -> Grip (GG). With Virtual Stock OFF
// and the two-hand hold latched, the positional B line IS the primary Grip
// position -> support Grip position pair. It is a fixed implementation detail
// of the product, not a user-selectable anchor and not a config/menu switch,
// and it deliberately never consults "Reduce Support-Hand Rotation": that
// setting only chooses the Virtual Stock support endpoint.
//
// Both grips must be committed (`*GripValid`) and finite. Anything else -
// either grip missing, non-finite, or the solve carrying no grip sample at all
// - returns the caller's aim-position pair, which is the exact pre-GG geometry,
// so absent or untracked grip data degrades to the previous behaviour instead
// of a mixed grip/aim pair or a fabricated position. A committed-but-
// non-finite grip is treated the same way here rather than failing the whole
// selection: the product solve must keep steering (fail-safe, never NaN).
//
// This mirrors the Two-Hand Lab's GG anchor (same both-grips-or-aim-line rule);
// the product path computes it here so GG never depends on the Lab being
// enabled.
struct TwoHandGripEndpoints
{
    // True only when the returned pair is the Grip -> Grip pair.
    bool usedGrips = false;
    // False only when neither pair is usable (non-finite aim endpoints); the
    // caller then keeps today's fail-closed behaviour.
    bool valid = false;
    Point3 primary{};
    Point3 support{};
};

inline TwoHandGripEndpoints SelectTwoHandGripEndpoints(
    Point3 primaryAim, Point3 supportAim, bool primaryGripValid,
    Point3 primaryGrip, bool supportGripValid, Point3 supportGrip) noexcept
{
    TwoHandGripEndpoints result{};
    if (primaryGripValid && supportGripValid && Finite(primaryGrip) &&
        Finite(supportGrip))
    {
        result.usedGrips = true;
        result.primary = primaryGrip;
        result.support = supportGrip;
        result.valid = true;
        return result;
    }
    if (Finite(primaryAim) && Finite(supportAim))
    {
        result.primary = primaryAim;
        result.support = supportAim;
        result.valid = true;
    }
    return result;
}

// The legacy controller-to-controller acceptance rule is also the Hybrid
// offhand candidate. Keep the rejection threshold and diagnostics in one pure
// helper so Hybrid cannot accidentally bypass the stock-safe agreement guard.
//
// `retainBeyondAgreementFloor` is the persistent-support-grip corrective: the
// caller may set it only for an invocation that is qualified to consume
// support geometry (wired title, durable relationship engaged and readable,
// current invocation proving the same owner). When set, the 0.35 agreement
// floor no longer rejects the direction; every other guard is unchanged
// (finite inputs, the minimum segment length, the normalization and the
// finite output check all still run first), and the floor verdict is still
// written to `rejectedExtreme` / `rejectedAgreement` so a caller can record
// that the floor would have rejected. The stock ray path and the Hybrid
// offhand path never set it.
inline bool TryBuildAcceptedSupportDirection(
    Point3 primary, Point3 support, Point3 primaryForward,
    Point3& out, bool& rejectedExtreme, float& rejectedAgreement,
    bool retainBeyondAgreementFloor = false) noexcept
{
    out = Point3{};
    rejectedExtreme = false;
    rejectedAgreement = 0.0f;
    if (!Finite(primary) || !Finite(support) || !Finite(primaryForward))
        return false;

    const Point3 delta{
        support.x - primary.x,
        support.y - primary.y,
        support.z - primary.z};
    const float lengthSquared = Dot(delta, delta);
    if (!Finite(delta) || !std::isfinite(lengthSquared))
        return false;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length < 1.0e-4f)
        return false;
    out = Point3{
        delta.x / length,
        delta.y / length,
        delta.z / length};
    if (!Finite(out))
        return false;
    const float agreement = Dot(out, primaryForward);
    if (!std::isfinite(agreement))
    {
        // A non-finite agreement is never retention-eligible: the primary
        // forward itself is unusable and the caller must fall back.
        rejectedExtreme = true;
        rejectedAgreement = agreement;
        return false;
    }
    if (agreement < 0.35f)
    {
        rejectedExtreme = true;
        rejectedAgreement = agreement;
        return retainBeyondAgreementFloor;
    }
    return true;
}

// Blend direction-domain authority. The endpoint paths intentionally return
// their input direction without reconstructing or renormalizing it.
inline bool TryBlendDirectionAuthority(
    Point3 primary, Point3 support, float influence, Point3& out) noexcept
{
    out = Point3{};
    if (!Finite(primary) || !Finite(support) || !std::isfinite(influence))
        return false;
    if (influence <= 0.0f)
    {
        out = primary;
        return true;
    }
    if (influence >= 1.0f)
    {
        out = support;
        return true;
    }
    const float primaryWeight = 1.0f - influence;
    const Point3 candidate{
        primary.x * primaryWeight + support.x * influence,
        primary.y * primaryWeight + support.y * influence,
        primary.z * primaryWeight + support.z * influence};
    const float lengthSquared = Dot(candidate, candidate);
    if (!Finite(candidate) || !std::isfinite(lengthSquared) ||
        lengthSquared < 1.0e-8f)
        return false;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length <= 0.0f)
        return false;
    out = Point3{
        candidate.x / length,
        candidate.y / length,
        candidate.z / length};
    return Finite(out);
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
    const float clampedHeight = std::clamp(
        rearHeightM, kVirtualStockRearHeightMinimumM,
        kVirtualStockRearHeightMaximumM);
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
    const float shoulderBack = std::clamp(
        shoulderBackM, kVirtualStockShoulderBackMinimumM,
        kVirtualStockShoulderBackMaximumM);
    const float shoulderSide = std::clamp(
        shoulderSideM, kVirtualStockShoulderSideMinimumM,
        kVirtualStockShoulderSideMaximumM);
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
    const float chestHeight = std::clamp(
        chestHeightM, kVirtualStockChestHeightMinimumM,
        kVirtualStockChestHeightMaximumM);
    const float chestBack = std::clamp(
        chestBackM, kVirtualStockChestBackMinimumM,
        kVirtualStockChestBackMaximumM);
    const float chestSide = std::clamp(
        chestSideM, kVirtualStockChestSideMinimumM,
        kVirtualStockChestSideMaximumM);
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

    const float topHeight = std::clamp(
        topHeightM, kVirtualStockAdaptiveTopHeightMinimumM,
        kVirtualStockAdaptiveTopHeightMaximumM);
    const float bottomHeight = std::clamp(
        bottomHeightM, kVirtualStockAdaptiveBottomHeightMinimumM,
        kVirtualStockAdaptiveBottomHeightMaximumM);
    const float topHalfWidth = std::clamp(
        topHalfWidthM, kVirtualStockAdaptiveTopHalfWidthMinimumM,
        kVirtualStockAdaptiveTopHalfWidthMaximumM);
    const float bottomHalfWidth = std::clamp(
        bottomHalfWidthM, kVirtualStockAdaptiveBottomHalfWidthMinimumM,
        kVirtualStockAdaptiveBottomHalfWidthMaximumM);
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
    const float clampedStrength = std::clamp(
        strength, kVirtualStockStrengthMinimum,
        kVirtualStockStrengthMaximum);
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

// Hybrid fixed-stock candidate. Unlike the legacy selectors, zero strength is
// unavailable rather than a request to reuse the unguarded primary->support
// ray, so a rejected B cannot be resurrected through C. Optional details are
// populated from this production calculation rather than a parallel formula.
inline bool TryBuildHybridStockDirection(
    Point3 primary, Point3 target, Point3 support, float strength,
    Point3& out, HybridStockEvaluation* details = nullptr) noexcept
{
    out = Point3{};
    if (details)
        *details = HybridStockEvaluation{};
    if (!Finite(primary) || !Finite(target) || !Finite(support) ||
        !std::isfinite(strength) || strength <= 0.0f)
        return false;
    const float clampedStrength = std::clamp(
        strength, kVirtualStockStrengthMinimum,
        kVirtualStockStrengthMaximum);
    Point3 rear = target;
    if (clampedStrength < 1.0f)
    {
        rear = Point3{
            primary.x + clampedStrength * (target.x - primary.x),
            primary.y + clampedStrength * (target.y - primary.y),
            primary.z + clampedStrength * (target.z - primary.z)};
    }
    if (!Finite(rear))
        return false;
    if (details)
    {
        details->virtualRear = rear;
        details->virtualRearValid = true;
    }
    const bool valid = BuildVirtualStockDirection(rear, support, out);
    if (details)
    {
        details->valid = valid;
        details->direction = out;
    }
    return valid;
}

inline HybridStockEvaluation EvaluateHybridStock(
    Point3 primary, Point3 target, Point3 support, float strength) noexcept
{
    HybridStockEvaluation result{};
    Point3 direction{};
    TryBuildHybridStockDirection(
        primary, target, support, strength, direction, &result);
    return result;
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

// Profile-only Hybrid release geometry. X/Z tracking-space reach deliberately
// ignores hand height and HMD orientation; invalid enabled geometry fails to
// zero stock authority rather than granting the C endpoint.
inline float ComputeHybridHorizontalRearReleaseInfluence(
    Point3 primary, bool headValid, Point3 head,
    float fullStockDistance, float releaseDistance,
    HybridHorizontalReleaseEvaluation* details = nullptr) noexcept
{
    if (details)
        *details = HybridHorizontalReleaseEvaluation{};
    if (!headValid || !Finite(primary) || !Finite(head) ||
        !std::isfinite(fullStockDistance) ||
        !std::isfinite(releaseDistance) || fullStockDistance < 0.0f ||
        releaseDistance <= fullStockDistance)
    {
        return 0.0f;
    }
    const float dx = primary.x - head.x;
    const float dz = primary.z - head.z;
    const float distanceSquared = dx * dx + dz * dz;
    if (!std::isfinite(dx) || !std::isfinite(dz) ||
        !std::isfinite(distanceSquared) || distanceSquared < 0.0f)
    {
        return 0.0f;
    }
    const float horizontalReach = std::sqrt(distanceSquared);
    if (!std::isfinite(horizontalReach))
        return 0.0f;
    const float influence = ComputeProximityInfluence(
        horizontalReach, fullStockDistance, releaseDistance);
    if (details)
    {
        details->valid = true;
        details->horizontalReach = horizontalReach;
        details->influence = influence;
    }
    return influence;
}

// Hybrid seating authority projects the primary hand onto the finite T->S
// segment. A nearly collapsed segment is not a valid seat, even though the
// generic closest-point helper intentionally treats one as an endpoint.
// Optional details are populated after the production influence arithmetic.
inline float ComputeHybridSeatInfluence(
    Point3 primary, Point3 target, Point3 support,
    float fullSeatDistance, float releaseSeatDistance,
    HybridSeatEvaluation* details = nullptr) noexcept
{
    if (details)
        *details = HybridSeatEvaluation{};
    if (!Finite(primary) || !Finite(target) || !Finite(support) ||
        !std::isfinite(fullSeatDistance) ||
        !std::isfinite(releaseSeatDistance))
        return 0.0f;
    const Point3 segment{
        support.x - target.x,
        support.y - target.y,
        support.z - target.z};
    const float denominator = Dot(segment, segment);
    if (!Finite(segment) || !std::isfinite(denominator) ||
        denominator < 1.0e-8f)
        return 0.0f;
    const Point3 fromTarget{
        primary.x - target.x,
        primary.y - target.y,
        primary.z - target.z};
    const float numerator = Dot(fromTarget, segment);
    if (!Finite(fromTarget) || !std::isfinite(numerator))
        return 0.0f;
    const float rawProjection = numerator / denominator;
    if (!std::isfinite(rawProjection))
        return 0.0f;
    const float projection = std::clamp(rawProjection, 0.0f, 1.0f);
    const Point3 closest{
        target.x + segment.x * projection,
        target.y + segment.y * projection,
        target.z + segment.z * projection};
    float errorSquared = 0.0f;
    if (!Finite(closest) || !TryDistanceSquared(primary, closest, errorSquared))
        return 0.0f;
    const float seatError = std::sqrt(errorSquared);
    if (!std::isfinite(seatError))
        return 0.0f;
    const float influence = ComputeProximityInfluence(
        seatError, fullSeatDistance, releaseSeatDistance);
    if (details)
    {
        const float segmentLength = std::sqrt(denominator);
        if (std::isfinite(segmentLength))
        {
            details->valid = true;
            details->segmentLength = segmentLength;
            details->rawProjection = rawProjection;
            details->clampedProjection = projection;
            details->closest = closest;
            details->seatError = seatError;
            details->influence = influence;
        }
    }
    return influence;
}

inline HybridSeatEvaluation EvaluateHybridSeat(
    Point3 primary, Point3 target, Point3 support,
    float fullSeatDistance, float releaseSeatDistance) noexcept
{
    HybridSeatEvaluation result{};
    ComputeHybridSeatInfluence(
        primary, target, support, fullSeatDistance, releaseSeatDistance,
        &result);
    return result;
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
//
// `retainBeyondAgreementFloor` is the persistent-support-grip corrective and
// applies to the legacy fallback only. The caller sets it only for a
// qualified invocation (engaged + trusted relationship) AND only while
// Virtual Stock is off; the stock branch above and the non-retained fallback
// are byte-identical to the pre-fix rule. When it is set and the floor
// rejects, the selection stays valid and records the crossed floor in
// `retainedBeyondAgreementFloor` (with the floor agreement in
// `rejectedAgreement`), so the caller can distinguish the retained
// acceptance from an ordinary in-cone pass.
inline DirectionSelection SelectTwoHandAimDirection(
    bool virtualStockEnabled, float stockStrength, float rearHeightM,
    bool headValid, Point3 head, Point3 stockSupport, Point3 primary,
    Point3 legacySupport, Point3 primaryForward,
    bool retainBeyondAgreementFloor = false) noexcept
{
    DirectionSelection result{};
    if (virtualStockEnabled && std::isfinite(stockStrength) &&
        std::isfinite(rearHeightM))
    {
        const float clampedStrength = std::clamp(
            stockStrength, kVirtualStockStrengthMinimum,
            kVirtualStockStrengthMaximum);
        const float clampedHeight = std::clamp(
            rearHeightM, kVirtualStockRearHeightMinimumM,
            kVirtualStockRearHeightMaximumM);
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
    Point3 candidate{};
    bool floorRejected = false;
    float floorAgreement = 0.0f;
    const bool accepted = TryBuildAcceptedSupportDirection(
        primary, legacySupport, primaryForward, candidate,
        floorRejected, floorAgreement, retainBeyondAgreementFloor);
    result.valid = accepted;
    if (accepted)
    {
        result.direction = candidate;
        result.retainedBeyondAgreementFloor = floorRejected;
        // Only a retained acceptance bypassed the floor: it keeps the crossed
        // agreement the floor rejected (the `DirectionSelection` contract),
        // while an ordinary in-cone pass keeps exactly zero. No consumer reads
        // this field for an accepted selection, so the store is diagnostic.
        result.rejectedAgreement = floorRejected ? floorAgreement : 0.0f;
    }
    else
    {
        result.rejectedExtreme = floorRejected;
        result.rejectedAgreement = floorAgreement;
    }
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
        const float clampedStrength = std::clamp(
            stockStrength, kVirtualStockStrengthMinimum,
            kVirtualStockStrengthMaximum);
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
    Point3 candidate{};
    result.valid = TryBuildAcceptedSupportDirection(
        primary, legacySupport, primaryForward, candidate,
        result.rejectedExtreme, result.rejectedAgreement);
    if (result.valid || result.rejectedExtreme)
        result.direction = candidate;
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
