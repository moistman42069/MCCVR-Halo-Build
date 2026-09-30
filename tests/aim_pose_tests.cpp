#include <Windows.h>
#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/two_hand_input_smoothing.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/dll/aim_pose_trace.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>

namespace
{
unsigned checks{}, failures{};

void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const XrVector3f u{q.x, q.y, q.z};
    const auto cross = [](const XrVector3f& a, const XrVector3f& b) {
        return XrVector3f{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    };
    XrVector3f c1 = cross(u, v);
    c1.x += q.w * v.x;
    c1.y += q.w * v.y;
    c1.z += q.w * v.z;
    const XrVector3f c2 = cross(u, c1);
    return {v.x + 2.0f * c2.x, v.y + 2.0f * c2.y, v.z + 2.0f * c2.z};
}

#include "aim_pose_functions.inl"

bool Near(float actual, float expected, float epsilon = 1.0e-5f)
{
    return std::fabs(actual - expected) <= epsilon;
}

bool SamePosition(const XrVector3f& actual, const XrVector3f& expected)
{
    return Near(actual.x, expected.x) && Near(actual.y, expected.y) &&
        Near(actual.z, expected.z);
}

bool SameQuaternion(const XrQuaternionf& actual, const XrQuaternionf& expected,
    float epsilon = 1.0e-5f)
{
    return Near(actual.x, expected.x, epsilon) &&
        Near(actual.y, expected.y, epsilon) &&
        Near(actual.z, expected.z, epsilon) &&
        Near(actual.w, expected.w, epsilon);
}

XrVector3f AimForward(const AimPoseResult& result)
{
    return Rotate(result.pose.orientation, {0.0f, 0.0f, -1.0f});
}

bool SameAimResult(const AimPoseResult& actual, const AimPoseResult& expected,
    float epsilon = 1.0e-5f)
{
    return actual.valid == expected.valid &&
        actual.updateTwoHandActivity == expected.updateTwoHandActivity &&
        actual.twoHandActive == expected.twoHandActive &&
        actual.rejectedExtreme == expected.rejectedExtreme &&
        Near(actual.rejectedAgreement, expected.rejectedAgreement, epsilon) &&
        SamePosition(actual.pose.position, expected.pose.position) &&
        SameQuaternion(actual.pose.orientation, expected.pose.orientation, epsilon) &&
        SamePosition(AimForward(actual), AimForward(expected));
}

bool ExactAimResult(const AimPoseResult& actual, const AimPoseResult& expected)
{
    return actual.valid == expected.valid &&
        actual.updateTwoHandActivity == expected.updateTwoHandActivity &&
        actual.twoHandActive == expected.twoHandActive &&
        actual.rejectedExtreme == expected.rejectedExtreme &&
        actual.rejectedAgreement == expected.rejectedAgreement &&
        actual.pose.position.x == expected.pose.position.x &&
        actual.pose.position.y == expected.pose.position.y &&
        actual.pose.position.z == expected.pose.position.z &&
        actual.pose.orientation.x == expected.pose.orientation.x &&
        actual.pose.orientation.y == expected.pose.orientation.y &&
        actual.pose.orientation.z == expected.pose.orientation.z &&
        actual.pose.orientation.w == expected.pose.orientation.w;
}

XrQuaternionf Multiply(const XrQuaternionf& a, const XrQuaternionf& b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

XrQuaternionf ApplyCalibration(XrQuaternionf pose, float yawDeg,
    float pitchDeg, float rollDeg)
{
    constexpr float kDegToRad = 0.01745329252f;
    const float yaw = yawDeg * kDegToRad;
    const float pitch = pitchDeg * kDegToRad;
    const float roll = rollDeg * kDegToRad;
    const XrQuaternionf qYaw{
        0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    const XrQuaternionf qPitch{
        std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f)};
    const XrQuaternionf qRoll{
        0.0f, 0.0f, std::sin(-roll * 0.5f), std::cos(roll * 0.5f)};
    const XrQuaternionf corrected = Multiply(
        pose, Multiply(Multiply(qYaw, qPitch), qRoll));
    const float length = std::sqrt(
        corrected.x * corrected.x + corrected.y * corrected.y +
        corrected.z * corrected.z + corrected.w * corrected.w);
    return {
        corrected.x / length, corrected.y / length,
        corrected.z / length, corrected.w / length};
}

AimPoseInputs HybridInputs()
{
    AimPoseInputs inputs{};
    inputs.rightValid = true;
    inputs.right.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.right.position = {0.0f, 0.0f, 0.0f};
    inputs.leftValid = true;
    inputs.left.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.left.position = {0.0f, 0.0f, 0.0f};
    inputs.supportPosition = {0.6f, 0.0f, -1.0f};
    inputs.twoHandEnabled = true;
    inputs.twoHandLatched = true;
    inputs.virtualStockEnabled = true;
    inputs.virtualStockStrength = 1.0f;
    inputs.virtualStockRearHeightM = 0.0f;
    inputs.virtualStockRearReference = 3;
    inputs.virtualStockShoulderBackM = 0.20f;
    inputs.virtualStockShoulderSideM = 0.10f;
    inputs.virtualStockHybridOffhandInfluence = 0.50f;
    inputs.virtualStockHybridAdsReference = 0;
    inputs.virtualStockHybridSeatFullM = 0.050f;
    inputs.virtualStockHybridSeatReleaseM = 0.150f;
    inputs.headValid = true;
    inputs.headPosition = {0.0f, 0.5f, 0.0f};
    inputs.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    return inputs;
}

AimPoseInputs EquivalenceInputs()
{
    AimPoseInputs inputs = HybridInputs();
    inputs.right.position = {0.10f, 0.08f, 0.02f};
    inputs.supportPosition = {0.42f, 0.08f, -0.78f};
    inputs.headPosition = {-0.18f, 0.62f, 0.11f};
    inputs.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.virtualStockStrength = 0.73f;
    inputs.virtualStockRearHeightM = -0.12f;
    inputs.virtualStockShoulderBackM = 0.11f;
    inputs.virtualStockShoulderSideM = 0.07f;
    inputs.virtualStockProximityRelease = false;
    inputs.virtualStockProximityFullM = 0.270f;
    inputs.virtualStockProximityReleaseM = 0.425f;
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::Normal;
    return inputs;
}

void TestHybridMode3()
{
    AimPoseInputs inputs = HybridInputs();
    inputs.headValid = false;
    inputs.virtualStockHybridOffhandInfluence = 0.50f;
    inputs.virtualStockAdaptiveTopHeightM = NAN;
    inputs.virtualStockAdaptiveBottomHeightM = NAN;
    inputs.virtualStockAdaptiveTopHalfWidthM = NAN;
    inputs.virtualStockAdaptiveBottomHalfWidthM = NAN;
    const AimPoseResult result = ComputeAimPose(inputs);
    AimPoseTrace trace{};
    const AimPoseResult traced = ComputeAimPose(inputs, &trace);

    const virtual_stock::Point3 a{0.0f, 0.0f, -1.0f};
    const virtual_stock::Point3 b = virtual_stock::Point3{
        0.6f, 0.0f, -1.0f};
    const float bLength = std::sqrt(1.36f);
    const virtual_stock::Point3 expected = {
        (a.x * 0.5f + b.x / bLength * 0.5f),
        0.0f,
        (a.z * 0.5f + b.z / bLength * 0.5f)};
    const float expectedLength = std::sqrt(
        expected.x * expected.x + expected.y * expected.y +
        expected.z * expected.z);
    const XrVector3f expectedDirection{
        expected.x / expectedLength, expected.y / expectedLength,
        expected.z / expectedLength};
    Check(result.valid && result.twoHandActive,
        "Production mode 3 selects Hybrid and keeps accepted B presentation active");
    Check(ExactAimResult(traced, result) &&
            trace.path == AimSolverPath::Hybrid &&
            !trace.fixedTargetValid && !trace.fixedRearDistanceValid &&
            !trace.fixedProximityCalculated &&
            trace.requestedTarget == AimStockTarget::Head,
        "Traced mode 3 stays on Hybrid and never instruments the dormant Adaptive tail");
    Check(SamePosition(result.pose.position, inputs.right.position),
        "Hybrid production output position remains the exact primary P");
    Check(SamePosition(Rotate(result.pose.orientation, {0.0f, 0.0f, -1.0f}),
            expectedDirection),
        "Production mode 3 uses A/B direction authority rather than Adaptive geometry");
}

void TestHybridEndpointsAndFallbacks()
{
    AimPoseInputs inputs = HybridInputs();
    inputs.headValid = false;
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    const XrQuaternionf exactA{
        0.0f, std::sin(0.2f), 0.0f, std::cos(0.2f)};
    inputs.right.orientation = exactA;
    AimPoseResult result = ComputeAimPose(inputs);
    Check(result.valid && result.twoHandActive &&
            SameQuaternion(result.pose.orientation, exactA),
        "Hybrid influence zero with no C preserves the exact A quaternion and active presentation");
    Check(SamePosition(result.pose.position, inputs.right.position),
        "Pure-A Hybrid endpoint preserves P");

    inputs = HybridInputs();
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    result = ComputeAimPose(inputs);
    Check(result.valid && result.twoHandActive &&
            SameQuaternion(result.pose.orientation, inputs.right.orientation),
        "Accepted B plus influence zero plus W zero preserves exact A and remains active");

    inputs = HybridInputs();
    inputs.headValid = false;
    inputs.left.orientation = {0.4f, 0.0f, 0.0f, 0.916515f};
    result = ComputeAimPose(inputs);
    AimPoseInputs unchangedSupport = inputs;
    unchangedSupport.left.orientation = {0.0f, 0.7f, 0.0f, 0.714143f};
    const AimPoseResult rotatedSupport = ComputeAimPose(unchangedSupport);
    Check(result.valid && result.twoHandActive &&
            SameQuaternion(result.pose.orientation, rotatedSupport.pose.orientation),
        "Support-controller rotation has no direct Hybrid forward or roll authority");

    inputs = HybridInputs();
    inputs.headValid = false;
    inputs.supportPosition = {1.0f, 0.0f, 0.0f};
    result = ComputeAimPose(inputs);
    Check(result.valid && !result.twoHandActive &&
            SameQuaternion(result.pose.orientation, inputs.right.orientation),
        "Rejected B with no valid positive C falls back to exact A and inactive presentation");

    inputs = HybridInputs();
    inputs.supportPosition = {0.2f, 0.0f, 0.0f};
    inputs.right.position = {0.2f, 0.0f, 0.0f};
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    result = ComputeAimPose(inputs);
    const XrVector3f cOnlyForward = Rotate(
        result.pose.orientation, {0.0f, 0.0f, -1.0f});
    Check(result.valid && result.twoHandActive &&
            Near(cOnlyForward.x, 1.0f, 1.0e-4f) &&
            Near(cOnlyForward.y, 0.0f, 1.0e-4f) &&
            Near(cOnlyForward.z, 0.0f, 1.0e-4f),
        "Rejected B can be rescued by valid C and W greater than zero");

    inputs = HybridInputs();
    inputs.supportPosition = {1.0f, 0.0f, 0.0f};
    inputs.virtualStockStrength = 0.0f;
    inputs.headValid = true;
    result = ComputeAimPose(inputs);
    Check(result.valid && !result.twoHandActive &&
            SameQuaternion(result.pose.orientation, inputs.right.orientation),
        "Rejected B cannot be resurrected through zero-strength C");
}

void TestHybridTargetsAndCalibration()
{
    AimPoseInputs inputs = HybridInputs();
    inputs.right.position = {0.0f, 0.0f, 0.0f};
    inputs.supportPosition = {0.3f, 0.0f, -1.0f};
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    inputs.virtualStockHybridSeatFullM = 0.50f;
    inputs.virtualStockHybridSeatReleaseM = 0.60f;
    inputs.virtualStockHybridAdsReference = 0;
    AimPoseResult head = ComputeAimPose(inputs);
    const XrVector3f headForward = Rotate(
        head.pose.orientation, {0.0f, 0.0f, -1.0f});

    inputs.virtualStockHybridAdsReference = 1;
    const AimPoseResult shoulder = ComputeAimPose(inputs);
    const XrVector3f shoulderForward = Rotate(
        shoulder.pose.orientation, {0.0f, 0.0f, -1.0f});
    Check(head.valid && shoulder.valid && head.twoHandActive &&
            shoulder.twoHandActive &&
            !SamePosition(headForward, shoulderForward),
        "Hybrid Head and Shoulder ADS references produce independently observable C targets");

    inputs.headOrientation.x = NAN;
    const AimPoseResult shoulderFallback = ComputeAimPose(inputs);
    Check(SamePosition(Rotate(shoulderFallback.pose.orientation,
                {0.0f, 0.0f, -1.0f}), headForward),
        "Invalid Hybrid Shoulder target falls back to the valid Head target");

    inputs = HybridInputs();
    inputs.headValid = false;
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    inputs.gunYawDeg = 7.0f;
    inputs.gunPitchDeg = -3.0f;
    inputs.gunRollDeg = 2.0f;
    const AimPoseResult calibrated = ComputeAimPose(inputs);
    const XrQuaternionf expected = ApplyCalibration(
        inputs.right.orientation, inputs.gunYawDeg,
        inputs.gunPitchDeg, inputs.gunRollDeg);
    Check(calibrated.valid && SameQuaternion(
            calibrated.pose.orientation, expected, 2.0e-5f),
        "Shared finishAimPose applies gun calibration exactly once after Hybrid exact-A selection");
}

void TestHybridIgnoresLegacyProximity()
{
    AimPoseInputs baseline = HybridInputs();
    baseline.headPosition = {0.0f, 0.0f, 0.0f};
    baseline.virtualStockProximityRelease = false;
    baseline.virtualStockProximityFullM = 0.250f;
    baseline.virtualStockProximityReleaseM = 0.450f;
    const AimPoseResult withoutLegacyProximity = ComputeAimPose(baseline);

    AimPoseInputs changed = baseline;
    changed.virtualStockProximityRelease = true;
    changed.virtualStockProximityFullM = 0.010f;
    changed.virtualStockProximityReleaseM = 0.020f;
    const AimPoseResult withLegacyProximity = ComputeAimPose(changed);

    Check(withoutLegacyProximity.valid == withLegacyProximity.valid &&
            withoutLegacyProximity.twoHandActive == withLegacyProximity.twoHandActive &&
            withoutLegacyProximity.rejectedExtreme == withLegacyProximity.rejectedExtreme &&
            Near(withoutLegacyProximity.rejectedAgreement,
                withLegacyProximity.rejectedAgreement) &&
            SamePosition(withoutLegacyProximity.pose.position,
                withLegacyProximity.pose.position) &&
            SameQuaternion(withoutLegacyProximity.pose.orientation,
                withLegacyProximity.pose.orientation),
        "Hybrid output is independent of legacy proximity release settings");
}

void TestHybridHorizontalReleaseComposition()
{
    const auto makeBase = [](float horizontalReach) {
        AimPoseInputs inputs = HybridInputs();
        inputs.right.position = {horizontalReach, 0.0f, 0.0f};
        inputs.supportPosition = {
            horizontalReach + 0.15f, 0.0f, -1.0f};
        // The product VS-off pair is the fixed Grip -> Grip line when both grip
        // samples are usable and the aim-position pair otherwise (W2). This
        // fixture carries no grip sample, so keep the support AIM point on the
        // production support endpoint - exactly what the assembly produces when
        // the selected endpoint is not a grip sample - so the free solve below
        // is the same aim-line pair the reference compares against.
        inputs.left.position = inputs.supportPosition;
        inputs.headPosition = {0.0f, 0.0f, 0.0f};
        inputs.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
        return inputs;
    };
    const auto forProfile = [](const AimPoseInputs& base,
                               VirtualStockTestProfile profile) {
        return AimPoseInputsForProfile(base, profile);
    };
    constexpr VirtualStockTestProfile kHorizontalExperimentProfile =
        static_cast<VirtualStockTestProfile>(
            kVirtualStockExperimentProfileFirstId);
    constexpr VirtualStockAimSettings kHorizontalSettings =
        MakeHybridHorizontalReleaseProfileSettings(0.270f, 0.425f);
    const auto forHorizontal = [&](const AimPoseInputs& base) {
        AimPoseInputs resolved = base;
        ApplyVirtualStockAimSettings(
            resolved, kHorizontalSettings, kHorizontalExperimentProfile);
        return resolved;
    };

    const AimPoseInputs closeBase = makeBase(0.20f);
    const AimPoseInputs closeHorizontal = forHorizontal(closeBase);
    const AimPoseInputs closeForceStock = forProfile(
        closeBase, VirtualStockTestProfile::F_HybridForceStockHead);
    AimPoseTrace closeTrace{};
    const AimPoseResult closeResult = ComputeAimPose(
        closeHorizontal, &closeTrace);
    const AimPoseResult forceStockResult = ComputeAimPose(closeForceStock);
    Check(SameAimResult(closeResult, forceStockResult, 2.0e-5f) &&
            closeTrace.horizontalReleaseAttempted &&
            closeTrace.horizontalReleaseValid &&
            closeTrace.horizontalReleaseInfluence == 1.0f &&
            closeTrace.wAfterDiagnosticValid &&
            closeTrace.wAfterDiagnostic == 1.0f &&
            closeTrace.wEffectiveValid &&
            closeTrace.wEffective == 1.0f &&
            SamePosition(closeResult.pose.position,
                closeHorizontal.right.position),
        "Horizontal-release tuning inside full radius exactly matches Force Stock C and preserves P");

    const AimPoseInputs releasedBase = makeBase(0.60f);
    const AimPoseInputs releasedHorizontal = forHorizontal(releasedBase);
    const AimPoseInputs releasedFixed = forProfile(
        releasedBase, VirtualStockTestProfile::B_FixedHeadControl);
    AimPoseTrace releasedTrace{};
    const AimPoseResult releasedResult = ComputeAimPose(
        releasedHorizontal, &releasedTrace);
    const AimPoseResult fixedReleasedResult = ComputeAimPose(releasedFixed);
    Check(SameAimResult(releasedResult, fixedReleasedResult, 2.0e-5f) &&
            releasedTrace.bAccepted &&
            releasedTrace.horizontalReleaseValid &&
            releasedTrace.horizontalReleaseInfluence == 0.0f &&
            releasedTrace.wAfterDiagnostic == 1.0f &&
            releasedTrace.wEffective == 0.0f &&
            !releasedTrace.stockContributed &&
            SamePosition(releasedResult.pose.position,
                releasedHorizontal.right.position),
        "Horizontal-release tuning outside release exactly matches the fixed-stock guarded B endpoint");

    const AimPoseInputs transitionBase = makeBase(0.3475f);
    const AimPoseInputs transition = forHorizontal(transitionBase);
    AimPoseTrace transitionTrace{};
    const AimPoseResult transitionResult = ComputeAimPose(
        transition, &transitionTrace);
    virtual_stock::Point3 expectedTransition{};
    const bool expectedTransitionValid =
        virtual_stock::TryBlendDirectionAuthority(
            {transitionTrace.bDirection.x, transitionTrace.bDirection.y,
                transitionTrace.bDirection.z},
            {transitionTrace.cDirection.x, transitionTrace.cDirection.y,
                transitionTrace.cDirection.z},
            transitionTrace.horizontalReleaseInfluence,
            expectedTransition);
    Check(transitionResult.valid && transitionResult.twoHandActive &&
            transitionTrace.bAccepted && transitionTrace.cValid &&
            transitionTrace.horizontalReleaseValid &&
            transitionTrace.horizontalReleaseInfluence > 0.0f &&
            transitionTrace.horizontalReleaseInfluence < 1.0f &&
            Near(transitionTrace.wEffective,
                transitionTrace.horizontalReleaseInfluence) &&
            transitionTrace.stockBlendSucceeded &&
            expectedTransitionValid &&
            Near(transitionTrace.finalDirection.x, expectedTransition.x) &&
            Near(transitionTrace.finalDirection.y, expectedTransition.y) &&
            Near(transitionTrace.finalDirection.z, expectedTransition.z) &&
            SamePosition(AimForward(transitionResult),
                {expectedTransition.x, expectedTransition.y,
                    expectedTransition.z}) &&
            SamePosition(transitionResult.pose.position,
                transition.right.position),
        "Horizontal-release tuning blends guarded B directly toward C with finite partial authority");

    XrVector3f previousForward{};
    bool havePrevious = false;
    bool continuous = true;
    for (int step = 0; step <= 40; ++step)
    {
        const float reach = 0.25f + static_cast<float>(step) * 0.005f;
        const AimPoseInputs sample = forHorizontal(makeBase(reach));
        const AimPoseResult result = ComputeAimPose(sample);
        const XrVector3f forward = AimForward(result);
        continuous = continuous && result.valid &&
            std::isfinite(forward.x) && std::isfinite(forward.y) &&
            std::isfinite(forward.z) &&
            SamePosition(result.pose.position, sample.right.position);
        if (havePrevious)
        {
            const float dx = forward.x - previousForward.x;
            const float dy = forward.y - previousForward.y;
            const float dz = forward.z - previousForward.z;
            continuous = continuous &&
                std::sqrt(dx * dx + dy * dy + dz * dz) < 0.05f;
        }
        previousForward = forward;
        havePrevious = true;
    }
    Check(continuous,
        "Horizontal-release tuning stays finite and continuous across both exact endpoints");

    AimPoseInputs yawChanged = transition;
    yawChanged.headOrientation = {
        0.0f, std::sin(0.7f), 0.0f, std::cos(0.7f)};
    const AimPoseResult yawChangedResult = ComputeAimPose(yawChanged);
    Check(SameAimResult(yawChangedResult, transitionResult, 2.0e-5f),
        "Horizontal release ignores HMD yaw when head and hand positions are unchanged");

    AimPoseInputs supportRotated = transition;
    supportRotated.left.orientation = {
        0.4f, 0.3f, 0.1f, 0.8602325f};
    Check(SameAimResult(
            ComputeAimPose(supportRotated), transitionResult, 2.0e-5f),
        "Horizontal release grants no authority to support-controller orientation");

    AimPoseInputs verticallyMoved = transition;
    verticallyMoved.right.position.y += 2.0f;
    AimPoseTrace verticalTrace{};
    ComputeAimPose(verticallyMoved, &verticalTrace);
    Check(verticalTrace.horizontalReleaseValid &&
            verticalTrace.rearHorizontalReachM ==
                transitionTrace.rearHorizontalReachM &&
            verticalTrace.horizontalReleaseInfluence ==
                transitionTrace.horizontalReleaseInfluence,
        "Horizontal release influence is invariant to vertical rear-hand movement");

    AimPoseInputs zeroStrength = closeHorizontal;
    zeroStrength.virtualStockStrength = 0.0f;
    AimPoseInputs zeroStrengthFree = zeroStrength;
    zeroStrengthFree.virtualStockEnabled = false;
    const AimPoseResult zeroStrengthResult = ComputeAimPose(zeroStrength);
    Check(SameAimResult(
            zeroStrengthResult, ComputeAimPose(zeroStrengthFree), 2.0e-5f),
        "Horizontal release cannot fabricate C when stock strength is zero");

    AimPoseInputs noHead = closeHorizontal;
    noHead.headValid = false;
    AimPoseInputs noHeadFree = noHead;
    noHeadFree.virtualStockEnabled = false;
    AimPoseTrace noHeadTrace{};
    const AimPoseResult noHeadResult = ComputeAimPose(noHead, &noHeadTrace);
    Check(SameAimResult(
            noHeadResult, ComputeAimPose(noHeadFree), 2.0e-5f) &&
            noHeadTrace.horizontalReleaseAttempted &&
            !noHeadTrace.horizontalReleaseValid &&
            !noHeadTrace.cValid && !noHeadTrace.wEffectiveValid,
        "Invalid HMD geometry fails safely to guarded free aim without C authority");

    AimPoseInputs malformed = transition;
    malformed.hybridHorizontalRearReleaseFullM = NAN;
    AimPoseInputs malformedFree = malformed;
    malformedFree.virtualStockEnabled = false;
    AimPoseTrace malformedTrace{};
    const AimPoseResult malformedResult = ComputeAimPose(
        malformed, &malformedTrace);
    Check(SameAimResult(
            malformedResult, ComputeAimPose(malformedFree), 2.0e-5f) &&
            malformedTrace.horizontalReleaseAttempted &&
            !malformedTrace.horizontalReleaseValid &&
            malformedTrace.wEffective == 0.0f,
        "Malformed enabled release thresholds fail to zero C authority");

    AimPoseInputs rejectedBase = makeBase(0.20f);
    rejectedBase.supportPosition = rejectedBase.right.position;
    const AimPoseInputs rejectedClose = forHorizontal(rejectedBase);
    const AimPoseInputs rejectedForceStock = forProfile(
        rejectedBase, VirtualStockTestProfile::F_HybridForceStockHead);
    Check(SameAimResult(
            ComputeAimPose(rejectedClose),
            ComputeAimPose(rejectedForceStock), 2.0e-5f),
        "Valid close C retains the existing ability to rescue rejected B");

    const AimPoseInputs isolationBase = EquivalenceInputs();
    bool existingProfilesUnchanged = true;
    for (size_t index = 0; index < 15; ++index)
    {
        const VirtualStockTestProfile profile =
            VirtualStockTestProfileDefinitionAt(index).id;
        AimPoseInputs baseline = forProfile(isolationBase, profile);
        const AimPoseResult expected = ComputeAimPose(baseline);
        existingProfilesUnchanged = existingProfilesUnchanged &&
            !baseline.hybridHorizontalRearReleaseEnabled;
        baseline.hybridHorizontalRearReleaseFullM = NAN;
        baseline.hybridHorizontalRearReleaseReleaseM = INFINITY;
        existingProfilesUnchanged = existingProfilesUnchanged &&
            ExactAimResult(ComputeAimPose(baseline), expected);
    }
    Check(existingProfilesUnchanged,
        "Custom and A-N ignore dormant horizontal release fields exactly");
}

void TestHybridDiagnosticState()
{
    HybridDiagnosticOverrideState state;
    Check(state.Load() == HybridDiagnosticOverride::Normal,
        "Hybrid diagnostic state starts in Normal");
    state.Store(HybridDiagnosticOverride::ForceHip);
    Check(state.Load() == HybridDiagnosticOverride::ForceHip,
        "Hybrid diagnostic state stores Force Hip at runtime");
    state.Store(HybridDiagnosticOverride::ForceStock);
    Check(state.Load() == HybridDiagnosticOverride::ForceStock,
        "Hybrid diagnostic state stores Force Stock at runtime");
    state.Store(static_cast<HybridDiagnosticOverride>(99));
    Check(state.Load() == HybridDiagnosticOverride::Normal,
        "Invalid Hybrid diagnostic state input normalizes to Normal");
    HybridDiagnosticOverrideState freshState;
    Check(freshState.Load() == HybridDiagnosticOverride::Normal,
        "A fresh Hybrid diagnostic state resets to Normal");
}

void TestTwoHandInputSmoothing()
{
    using two_hand_input_smoothing::Advance;
    using two_hand_input_smoothing::Reset;
    using two_hand_input_smoothing::Sample;
    using virtual_stock::Point3;
    using virtual_stock::Quat4;
    two_hand_input_smoothing::State state{};
    Sample raw{};
    raw.primaryOrientation = Quat4{0.0f, 0.0f, 0.0f, 1.0f};
    raw.primaryGripValid = true;
    raw.supportGripValid = true;

    const auto seed = Advance(state, 1, 0.01f, raw);
    Check(seed.valid && seed.advanced && Near(seed.alpha, 0.25f) &&
            seed.filtered.primaryOrientation.w == raw.primaryOrientation.w &&
            seed.filtered.primaryAimPosition.x == raw.primaryAimPosition.x &&
            seed.filtered.supportAimPosition.x == raw.supportAimPosition.x,
        "first active prepared frame seeds every filtered input exactly raw");

    Sample moved = raw;
    moved.primaryOrientation = Quat4{0.0f, 1.0f, 0.0f, 0.0f};
    moved.primaryAimPosition = Point3{4.0f, 0.0f, 0.0f};
    moved.supportAimPosition = Point3{2.0f, 0.0f, -2.0f};
    moved.primaryGripPosition = Point3{0.4f, 0.0f, 0.0f};
    moved.supportGripPosition = Point3{0.0f, 0.0f, -0.4f};
    const auto filtered = Advance(state, 2, 0.01f, moved);
    const Quat4 expectedRotation = virtual_stock::SlerpQuat4Shortest(
        raw.primaryOrientation, moved.primaryOrientation, 0.25f);
    Check(filtered.valid && filtered.advanced && Near(filtered.alpha, 0.25f) &&
            Near(filtered.filtered.primaryAimPosition.x, 1.0f) &&
            Near(filtered.filtered.supportAimPosition.x, 0.5f) &&
            Near(filtered.filtered.primaryGripPosition.x, 0.1f) &&
            Near(filtered.filtered.supportGripPosition.z, -0.1f) &&
            Near(virtual_stock::Quat4AngleDegrees(
                filtered.filtered.primaryOrientation, expectedRotation), 0.0f,
                0.01f),
        "speed-25 applies alpha=clamp(25*dt,0,1) to positions and quaternion SLERP");
    const Sample beforeDuplicate = filtered.filtered;
    Sample duplicateTarget = moved;
    duplicateTarget.primaryAimPosition = Point3{-20.0f, 0.0f, 0.0f};
    const auto duplicate = Advance(state, 2, 0.04f, duplicateTarget);
    Check(duplicate.valid && !duplicate.advanced &&
            Near(state.primaryAimPosition.x,
                beforeDuplicate.primaryAimPosition.x) &&
            Near(state.primaryAimPosition.z,
                beforeDuplicate.primaryAimPosition.z),
        "a duplicate prepared serial cannot advance filter history twice");
    const auto invalidDt = Advance(state, 3, 0.0f, moved);
    Check(!invalidDt.valid && !invalidDt.advanced &&
            state.lastPreparedSerial == 2,
        "non-positive dt fails open without consuming the prepared serial");

    Reset(state);
    const auto yaw = [](float degrees) {
        const float radians = degrees * 0.008726646259971648f;
        return Quat4{0.0f, std::sin(radians), 0.0f, std::cos(radians)};
    };
    Sample arcStart = raw;
    arcStart.primaryOrientation = yaw(170.0f);
    Check(Advance(state, 1, 0.01f, arcStart).advanced,
        "shortest-arc fixture seeds its initial quaternion");
    Sample arcEnd = arcStart;
    arcEnd.primaryOrientation = yaw(-170.0f);
    const auto arcMid = Advance(state, 2, 0.02f, arcEnd);
    Check(arcMid.valid && arcMid.advanced &&
            Near(virtual_stock::Quat4AngleDegrees(
                arcMid.filtered.primaryOrientation,
                Quat4{0.0f, 1.0f, 0.0f, 0.0f}), 0.0f, 0.1f),
        "quaternion smoothing takes the 20-degree shortest arc across the sign boundary");
    Reset(state);
    const auto fullResponse = Advance(state, 1, 1.0f, moved);
    Check(fullResponse.valid && fullResponse.advanced &&
            fullResponse.alpha == 1.0f &&
            Near(fullResponse.filtered.primaryAimPosition.x,
                moved.primaryAimPosition.x),
        "alpha clamps to one when dt is at least 40 ms");

    const two_hand_input_smoothing::PublicationIdentity identity{
        17, 23, 4, 9, true, false};
    Check(two_hand_input_smoothing::SamePublicationIdentity(identity, identity),
        "a filtered publication is accepted for its exact frozen identity");
    auto changedIdentity = identity;
    changedIdentity.preparedSerial++;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            identity, changedIdentity),
        "a filtered publication fails open for another prepared serial");
    changedIdentity = identity;
    changedIdentity.contactSpaceEpoch++;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            identity, changedIdentity),
        "a filtered publication fails open after a contact-space epoch change");
    changedIdentity = identity;
    changedIdentity.title++;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            identity, changedIdentity),
        "a filtered publication fails open for another active title");
    changedIdentity = identity;
    changedIdentity.titleGeneration++;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            identity, changedIdentity),
        "a filtered publication fails open after title-generation replacement");
    changedIdentity = identity;
    changedIdentity.leftHanded = false;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            identity, changedIdentity),
        "a filtered publication fails open after handedness changes");
    changedIdentity = identity;
    changedIdentity.supportEndpointUsedGrip = true;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            identity, changedIdentity),
        "a filtered publication fails open when the support endpoint source changes");
    changedIdentity = identity;
    changedIdentity.preparedSerial = 0;
    Check(!two_hand_input_smoothing::SamePublicationIdentity(
            changedIdentity, changedIdentity),
        "serial zero never qualifies as a filtered publication identity");

    Reset(state);
    Sample bad = raw;
    bad.supportAimPosition.x = std::numeric_limits<float>::quiet_NaN();
    Check(!Advance(state, 1, 0.01f, bad).valid && !state.initialized,
        "non-finite input fails open and is never retained");
    const float largest = std::numeric_limits<float>::max();
    Sample extreme = raw;
    extreme.primaryAimPosition.x = -largest;
    Check(Advance(state, 1, 0.01f, extreme).advanced,
        "finite extreme input can seed without arithmetic");
    extreme.primaryAimPosition.x = largest;
    const auto overflow = Advance(state, 2, 0.02f, extreme);
    Check(overflow.valid && !overflow.advanced && !state.initialized &&
            overflow.filtered.primaryAimPosition.x == largest,
        "non-finite interpolation falls back to raw and clears bad history");

    AimPoseInputs rawInputs = EquivalenceInputs();
    rawInputs.virtualStockEnabled = false;
    rawInputs.twoHandLabEnabled = false;
    rawInputs.primaryGripValid = true;
    rawInputs.primaryGripPosition = {0.12f, 0.08f, 0.02f};
    rawInputs.supportGripValid = true;
    rawInputs.supportGripPosition = {0.40f, 0.08f, -0.76f};
    const AimPoseResult rawTwoHand = ComputeAimPose(rawInputs);
    AimPoseInputs filteredInputs = rawInputs;
    filteredInputs.twoHandSmoothingGeometryValid = true;
    filteredInputs.twoHandSmoothedPrimaryOrientation =
        {0.0f, 0.04f, 0.0f, 0.9991997f};
    filteredInputs.twoHandSmoothedPrimaryAimPosition = {0.25f, 0.08f, 0.02f};
    filteredInputs.twoHandSmoothedSupportAimPosition = {0.36f, 0.08f, -0.78f};
    filteredInputs.twoHandSmoothedPrimaryGripValid = true;
    filteredInputs.twoHandSmoothedPrimaryGripPosition = {0.27f, 0.08f, 0.02f};
    filteredInputs.twoHandSmoothedSupportGripValid = true;
    filteredInputs.twoHandSmoothedSupportGripPosition = {0.38f, 0.08f, -0.80f};
    const AimPoseResult filteredTwoHand = ComputeAimPose(filteredInputs);
    Check(rawTwoHand.valid && rawTwoHand.twoHandActive &&
            filteredTwoHand.valid && filteredTwoHand.twoHandActive &&
            SamePosition(filteredTwoHand.pose.position,
                rawInputs.right.position) &&
            !Near(AimForward(filteredTwoHand).x, AimForward(rawTwoHand).x,
                1.0e-4f),
        "VS-OFF two-hand solve consumes filtered geometry but keeps raw weapon/base position");

    AimPoseInputs oneHand = filteredInputs;
    oneHand.twoHandLatched = false;
    AimPoseInputs rawOneHand = rawInputs;
    rawOneHand.twoHandLatched = false;
    Check(ExactAimResult(ComputeAimPose(oneHand), ComputeAimPose(rawOneHand)),
        "one-handed aiming ignores prepared smoother copies exactly");

    // (a2) Virtual Stock consumption contract (2026-09-29 Two-Hand Smoothing
    // scope): the prepared smoothed copies now feed the VS-ON solve too. With
    // the packet absent (`twoHandSmoothingGeometryValid` false, which is what
    // strength 0 publishes) the VS solve must stay bit-identical to raw even
    // with hostile copies present; with the packet present the same fixture
    // must be a real presentation differential, with the base position (and
    // the raw-derived support-endpoint correspondence) still raw.
    {
        AimPoseInputs stockRaw = rawInputs;
        stockRaw.virtualStockEnabled = true;
        const AimPoseResult stockRawResult = ComputeAimPose(stockRaw);
        Check(stockRawResult.valid && stockRawResult.twoHandActive,
            "the Virtual Stock smoothing fixture is a valid stock solve");
        AimPoseInputs stockAbsent = stockRaw;
        stockAbsent.twoHandSmoothingGeometryValid = false;
        stockAbsent.twoHandSmoothedPrimaryOrientation = {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN()};
        stockAbsent.twoHandSmoothedPrimaryAimPosition = {
            std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
        stockAbsent.twoHandSmoothedSupportAimPosition = {
            0.0f, -std::numeric_limits<float>::infinity(), 0.0f};
        stockAbsent.twoHandSmoothedPrimaryGripValid = true;
        stockAbsent.twoHandSmoothedPrimaryGripPosition = {
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
        stockAbsent.twoHandSmoothedSupportGripValid = true;
        stockAbsent.twoHandSmoothedSupportGripPosition = {
            0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
        Check(ExactAimResult(ComputeAimPose(stockAbsent), stockRawResult),
            "Virtual Stock at smoothing strength 0 (packet absent) stays byte-identical to raw and ignores hostile copies");
        AimPoseInputs stockOn = filteredInputs;
        stockOn.virtualStockEnabled = true;
        const AimPoseResult stockOnResult = ComputeAimPose(stockOn);
        Check(stockOnResult.valid && stockOnResult.twoHandActive &&
                !ExactAimResult(stockOnResult, stockRawResult) &&
                SamePosition(stockOnResult.pose.position,
                    stockRaw.right.position),
            "Virtual Stock at smoothing strength > 0 consumes the smoothed copies as a real differential while the base position stays raw");
    }

    AimPoseInputs labGG = filteredInputs;
    labGG.twoHandLabEnabled = true;
    labGG.twoHandLabAnchor = 4;
    labGG.twoHandLabOffhandInfluence = 1.0f;
    AimPoseTrace labTrace{};
    const AimPoseResult labResult = ComputeAimPose(labGG, &labTrace);
    Check(labResult.valid && labTrace.labValid && labTrace.lab.pivotsValid &&
            Near(labTrace.lab.primaryPivot.x,
                labGG.twoHandSmoothedPrimaryGripPosition.x) &&
            Near(labTrace.lab.supportPivot.z,
                labGG.twoHandSmoothedSupportGripPosition.z),
        "Lab GG pivots consume the same filtered Grip copies as the production solve");

    // (a) Acquisition/admission is raw: the prepared smoother copies exist only
    // for an already-acquired two-hand hold. vr.cpp applies them only when
    // inputs.twoHandLatched is set (ApplyPreparedTwoHandInputSmoothing), and
    // the grip-zone admission itself consumes the raw captured grips, so a
    // frame that has not acquired the hold must solve bit-identically with
    // hostile smoother copies present.
    const auto withHostileCopies = [](AimPoseInputs inputs) {
        inputs.twoHandSmoothingGeometryValid = true;
        inputs.twoHandSmoothedPrimaryOrientation =
            {0.0f, 0.0998334f, 0.0f, 0.9950042f};
        inputs.twoHandSmoothedPrimaryAimPosition = {9.0f, 9.0f, 9.0f};
        inputs.twoHandSmoothedSupportAimPosition = {-9.0f, -9.0f, -9.0f};
        inputs.twoHandSmoothedPrimaryGripValid = true;
        inputs.twoHandSmoothedPrimaryGripPosition = {9.0f, 9.0f, 9.0f};
        inputs.twoHandSmoothedSupportGripValid = true;
        inputs.twoHandSmoothedSupportGripPosition = {-9.0f, -9.0f, -9.0f};
        return inputs;
    };
    {
        AimPoseInputs unacquired = rawInputs;
        unacquired.twoHandLatched = false;
        Check(ExactAimResult(ComputeAimPose(withHostileCopies(unacquired)),
                ComputeAimPose(unacquired)),
            "an unacquired latch solves exactly as raw, so smoother state cannot admit or steer acquisition");

        AimPoseInputs supportMissing = rawInputs;
        supportMissing.leftValid = false;
        Check(ExactAimResult(ComputeAimPose(withHostileCopies(supportMissing)),
                ComputeAimPose(supportMissing)),
            "a frame without the support acquisition stays raw despite smoother copies");

        AimPoseInputs twoHandOff = rawInputs;
        twoHandOff.twoHandEnabled = false;
        Check(ExactAimResult(ComputeAimPose(withHostileCopies(twoHandOff)),
                ComputeAimPose(twoHandOff)),
            "a disabled two-hand path ignores smoother copies entirely");
    }

    // (d) Smoothing OFF preserves the current output: with the consumption flag
    // false the consumer must ignore whatever the smoothed copies hold, so the
    // OFF frame is the pre-feature raw solve bit-for-bit, while the ON frame is
    // a real differential against it.
    {
        AimPoseInputs smoothingOff = rawInputs;
        smoothingOff.twoHandSmoothingGeometryValid = false;
        smoothingOff.twoHandSmoothedPrimaryOrientation =
            {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::quiet_NaN()};
        smoothingOff.twoHandSmoothedPrimaryAimPosition = {
            std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
        smoothingOff.twoHandSmoothedSupportAimPosition = {
            0.0f, -std::numeric_limits<float>::infinity(), 0.0f};
        smoothingOff.twoHandSmoothedPrimaryGripValid = true;
        smoothingOff.twoHandSmoothedPrimaryGripPosition = {
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
        smoothingOff.twoHandSmoothedSupportGripValid = true;
        smoothingOff.twoHandSmoothedSupportGripPosition = {
            0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
        const AimPoseResult offResult = ComputeAimPose(smoothingOff);
        Check(ExactAimResult(offResult, rawTwoHand),
            "smoothing off consumes no smoothed copy and preserves the raw output exactly");
        Check(!ExactAimResult(ComputeAimPose(filteredInputs), offResult),
            "smoothing on is a real presentation differential against smoothing off");
    }

    // (f) Layer lifecycle reset fails open: the vr.cpp wiring resets
    // layer.filter via two_hand_input_smoothing::Reset on a title/generation/
    // contact-space-epoch change and on a handedness swap, so no filtered
    // geometry may blend across the identity boundary and the packet published
    // for the previous identity stays rejected.
    {
        two_hand_input_smoothing::State layer{};
        Sample first = raw;
        first.primaryOrientation = Quat4{0.0f, 0.0f, 0.0f, 1.0f};
        first.primaryAimPosition = {0.0f, 0.0f, 0.0f};
        first.supportAimPosition = {0.0f, 0.0f, 0.0f};
        Check(Advance(layer, 1, 0.01f, first).advanced,
            "the lifecycle fixture seeds its filter history");
        Sample movedSample = first;
        movedSample.primaryOrientation = Quat4{0.0f, 0.1305262f, 0.0f, 0.9914449f};
        movedSample.primaryAimPosition = {1.0f, 0.0f, 0.0f};
        movedSample.supportAimPosition = {0.5f, 0.0f, 0.0f};
        const auto blended = Advance(layer, 2, 0.01f, movedSample);
        Check(blended.advanced &&
                blended.filtered.primaryAimPosition.x !=
                    movedSample.primaryAimPosition.x,
            "the lifecycle fixture has retained filtered history before the change");

        Reset(layer); // the epoch/title/handedness reset the wiring performs
        Sample afterChange = movedSample;
        afterChange.primaryOrientation = Quat4{0.0f, 0.2588190f, 0.0f, 0.9659258f};
        afterChange.primaryAimPosition = {5.0f, 0.0f, 0.0f};
        afterChange.supportAimPosition = {4.0f, 0.0f, 0.0f};
        const auto reseeded = Advance(layer, 3, 0.01f, afterChange);
        Check(reseeded.valid && reseeded.advanced &&
                Near(reseeded.alpha, 0.25f) &&
                reseeded.filtered.primaryAimPosition.x ==
                    afterChange.primaryAimPosition.x &&
                reseeded.filtered.supportAimPosition.x ==
                    afterChange.supportAimPosition.x &&
                reseeded.filtered.primaryOrientation.w ==
                    afterChange.primaryOrientation.w,
            "the first advance after a lifecycle reset is exactly the new raw sample");
        const auto afterResetPacket = Advance(layer, 4, 0.01f, afterChange);
        Check(afterResetPacket.valid &&
                afterResetPacket.filtered.primaryAimPosition.x ==
                    afterChange.primaryAimPosition.x,
            "history after the lifecycle reset belongs only to the new identity");

        const two_hand_input_smoothing::PublicationIdentity oldIdentity{
            2, 9, 3, 7, true, false};
        auto changedEpoch = oldIdentity;
        changedEpoch.contactSpaceEpoch = 10;
        Check(!two_hand_input_smoothing::SamePublicationIdentity(
                oldIdentity, changedEpoch),
            "a pre-change filtered packet is rejected after the contact-space epoch moves");
        auto changedHandedness = oldIdentity;
        changedHandedness.leftHanded = false;
        Check(!two_hand_input_smoothing::SamePublicationIdentity(
                oldIdentity, changedHandedness),
            "a pre-change filtered packet is rejected after the handedness swap");
    }
}

// The 0..25 Two-Hand Smoothing strength is a wet/dry mix over the UNCHANGED
// fixed speed-25 filter: 0 is raw exactly (and never advances the filter), 25
// is the previous candidate's full filter output bit-for-bit, and intermediate
// values stay between the two with no extra lag. The filter's own history
// never depends on the strength and never receives the mixed output back.
void TestTwoHandInputSmoothingStrength()
{
    using two_hand_input_smoothing::Advance;
    using two_hand_input_smoothing::Reset;
    using two_hand_input_smoothing::Sample;
    using virtual_stock::Point3;
    using virtual_stock::Quat4;

    const auto yaw = [](float degrees) {
        const float radians = degrees * 0.008726646259971648f;
        return Quat4{0.0f, std::sin(radians), 0.0f, std::cos(radians)};
    };
    const auto SameSample = [](const Sample& a, const Sample& b) {
        return a.primaryOrientation.x == b.primaryOrientation.x &&
            a.primaryOrientation.y == b.primaryOrientation.y &&
            a.primaryOrientation.z == b.primaryOrientation.z &&
            a.primaryOrientation.w == b.primaryOrientation.w &&
            a.primaryAimPosition.x == b.primaryAimPosition.x &&
            a.primaryAimPosition.y == b.primaryAimPosition.y &&
            a.primaryAimPosition.z == b.primaryAimPosition.z &&
            a.supportAimPosition.x == b.supportAimPosition.x &&
            a.supportAimPosition.y == b.supportAimPosition.y &&
            a.supportAimPosition.z == b.supportAimPosition.z &&
            a.primaryGripValid == b.primaryGripValid &&
            a.primaryGripPosition.x == b.primaryGripPosition.x &&
            a.primaryGripPosition.y == b.primaryGripPosition.y &&
            a.primaryGripPosition.z == b.primaryGripPosition.z &&
            a.supportGripValid == b.supportGripValid &&
            a.supportGripPosition.x == b.supportGripPosition.x &&
            a.supportGripPosition.y == b.supportGripPosition.y &&
            a.supportGripPosition.z == b.supportGripPosition.z;
    };
    const auto SameState = [](const two_hand_input_smoothing::State& a,
                              const two_hand_input_smoothing::State& b) {
        return a.initialized == b.initialized &&
            a.lastPreparedSerial == b.lastPreparedSerial &&
            a.primaryGripValid == b.primaryGripValid &&
            a.supportGripValid == b.supportGripValid &&
            a.primaryOrientation.x == b.primaryOrientation.x &&
            a.primaryOrientation.y == b.primaryOrientation.y &&
            a.primaryOrientation.z == b.primaryOrientation.z &&
            a.primaryOrientation.w == b.primaryOrientation.w &&
            a.primaryAimPosition.x == b.primaryAimPosition.x &&
            a.primaryAimPosition.y == b.primaryAimPosition.y &&
            a.primaryAimPosition.z == b.primaryAimPosition.z &&
            a.supportAimPosition.x == b.supportAimPosition.x &&
            a.supportAimPosition.y == b.supportAimPosition.y &&
            a.supportAimPosition.z == b.supportAimPosition.z &&
            a.primaryGripPosition.x == b.primaryGripPosition.x &&
            a.primaryGripPosition.y == b.primaryGripPosition.y &&
            a.primaryGripPosition.z == b.primaryGripPosition.z &&
            a.supportGripPosition.x == b.supportGripPosition.x &&
            a.supportGripPosition.y == b.supportGripPosition.y &&
            a.supportGripPosition.z == b.supportGripPosition.z;
    };
    const auto distance = [](Point3 a, Point3 b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const auto angle = [](Quat4 a, Quat4 b) {
        return virtual_stock::Quat4AngleDegrees(a, b);
    };

    Sample raw{};
    raw.primaryOrientation = Quat4{0.0f, 0.0f, 0.0f, 1.0f};
    raw.primaryGripValid = true;
    raw.supportGripValid = true;

    // --- Endpoint 0: the exact previous "smoothing off" path. The fixed
    // filter is not consumed, not advanced, and the packet reports 0 strength.
    {
        two_hand_input_smoothing::State state{};
        Sample moved = raw;
        moved.primaryOrientation = yaw(12.0f);
        moved.primaryAimPosition = Point3{4.0f, 0.0f, -1.0f};
        const auto off = Advance(state, 1, 0.01f, moved, 0.0f);
        Check(!off.valid && !off.advanced && off.alpha == 0.0f &&
                off.strength == 0.0f && off.mix == 0.0f &&
                !state.initialized && state.lastPreparedSerial == 0,
            "strength 0 is inactive: no filtered packet and no filter advance");
        Check(off.mixed.primaryAimPosition.x == moved.primaryAimPosition.x &&
                off.mixed.primaryAimPosition.z == moved.primaryAimPosition.z &&
                off.mixed.primaryOrientation.w == moved.primaryOrientation.w,
            "strength 0 emits the raw sample untouched");
        Check(!Advance(state, 2, 0.01f, moved, -3.0f).valid &&
                !Advance(state, 2, 0.01f, moved,
                    std::numeric_limits<float>::quiet_NaN()).valid &&
                !Advance(state, 2, 0.01f, moved,
                    std::numeric_limits<float>::infinity()).valid &&
                !state.initialized,
            "negative and non-finite strengths read as off and never advance");
        const auto clampedHigh = Advance(state, 2, 0.01f, moved, 100.0f);
        Check(clampedHigh.valid && clampedHigh.advanced &&
                clampedHigh.strength ==
                    two_hand_input_smoothing::kStrengthMaximum &&
                clampedHigh.mix == 1.0f,
            "strength above 25 clamps to the full filter, not beyond it");
    }

    // --- Endpoint 25 is the previous candidate's full filter output exactly,
    // including the retained history that produced it.
    {
        Sample first = raw;
        first.primaryOrientation = yaw(3.0f);
        first.primaryAimPosition = Point3{0.15f, 1.30f, -0.25f};
        first.supportAimPosition = Point3{0.10f, 1.29f, -0.60f};
        first.primaryGripPosition = Point3{0.16f, 1.30f, -0.25f};
        first.supportGripPosition = Point3{0.11f, 1.29f, -0.61f};
        Sample second = first;
        second.primaryOrientation = yaw(9.0f);
        second.primaryAimPosition = Point3{0.30f, 1.31f, -0.28f};
        second.supportAimPosition = Point3{0.14f, 1.28f, -0.66f};
        second.primaryGripPosition = Point3{0.31f, 1.31f, -0.28f};
        second.supportGripPosition = Point3{0.15f, 1.28f, -0.67f};

        two_hand_input_smoothing::State defaultStrength{};
        two_hand_input_smoothing::State explicitFull{};
        const auto seedDefault = Advance(defaultStrength, 1, 0.01f, first);
        const auto seedExplicit = Advance(explicitFull, 1, 0.01f, first, 25.0f);
        Check(seedDefault.advanced && seedExplicit.advanced &&
                SameSample(seedDefault.filtered, seedExplicit.filtered) &&
                SameSample(seedDefault.mixed, seedExplicit.mixed) &&
                SameSample(seedDefault.mixed, seedDefault.filtered),
            "the default strength is the full 25 filter and its 1.0 mix returns the filtered sample itself");
        const auto movedDefault = Advance(defaultStrength, 2, 0.01f, second);
        const auto movedExplicit = Advance(explicitFull, 2, 0.01f, second, 25.0f);
        Check(movedDefault.advanced && movedExplicit.advanced &&
                SameSample(movedDefault.raw, movedExplicit.raw) &&
                SameSample(movedDefault.filtered, movedExplicit.filtered) &&
                SameSample(movedDefault.mixed, movedExplicit.filtered) &&
                movedDefault.alpha == movedExplicit.alpha &&
                SameState(defaultStrength, explicitFull),
            "strength 25 reproduces the pre-slider full smoothing result and history bit-for-bit");
        Check(movedDefault.filtered.primaryAimPosition.x !=
                second.primaryAimPosition.x &&
                movedDefault.filtered.primaryOrientation.w !=
                second.primaryOrientation.w,
            "the endpoint fixture really lags raw motion, so the equality above is not vacuous");
    }

    // --- Identical-history endpoints/intermediates: one history (advanced at
    // full strength) is copied per strength so the only difference is the mix.
    two_hand_input_smoothing::State history{};
    Sample last = raw;
    for (int index = 0; index < 6; ++index)
    {
        Sample step = raw;
        step.primaryOrientation = yaw(3.0f * static_cast<float>(index));
        step.primaryAimPosition = Point3{0.05f * index, 1.30f,
            -0.20f - 0.01f * index};
        step.supportAimPosition = Point3{0.02f * index, 1.29f,
            -0.55f - 0.01f * index};
        step.primaryGripPosition = Point3{0.06f * index, 1.30f,
            -0.21f - 0.01f * index};
        step.supportGripPosition = Point3{0.03f * index, 1.29f,
            -0.56f - 0.01f * index};
        last = step;
        const auto advanced = Advance(history,
            static_cast<uint64_t>(index) + 7, 0.01f, step);
        if (index < 5)
            Check(advanced.advanced && advanced.mixed.primaryAimPosition.x ==
                    advanced.filtered.primaryAimPosition.x,
                "the endpoint history advances fully under the default strength");
    }
    const auto mixAt = [&](float strength) {
        two_hand_input_smoothing::State copy = history;
        return Advance(copy, 13, 0.01f, last, strength);
    };
    const auto at5 = mixAt(5.0f);
    const auto at125 = mixAt(12.5f);
    const auto at20 = mixAt(20.0f);
    const auto atFull = mixAt(25.0f);
    Check(at5.valid && at5.advanced && at125.advanced && at20.advanced &&
            atFull.advanced && Near(at5.mix, 0.2f) &&
            Near(at125.mix, 0.5f) && Near(at20.mix, 0.8f) &&
            atFull.mix == 1.0f,
        "intermediate strengths report mix = strength/25");
    Check(atFull.mixed.primaryAimPosition.x ==
            atFull.filtered.primaryAimPosition.x &&
            atFull.mixed.primaryAimPosition.x !=
                atFull.raw.primaryAimPosition.x,
        "only strength 25 reproduces the full filtered position exactly");

    const auto rawToMixed = [&](const two_hand_input_smoothing::Result& r) {
        return distance(r.raw.primaryAimPosition, r.mixed.primaryAimPosition);
    };
    const auto filteredToMixed = [&](const two_hand_input_smoothing::Result& r) {
        return distance(r.filtered.primaryAimPosition,
            r.mixed.primaryAimPosition);
    };
    const float fullDeviation = distance(atFull.raw.primaryAimPosition,
        atFull.filtered.primaryAimPosition);
    Check(fullDeviation > 0.0f &&
            rawToMixed(at5) > 0.0f && rawToMixed(at125) > 0.0f &&
            rawToMixed(at20) > 0.0f,
        "the shared-strength fixture has real filter deviation to blend");
    Check(rawToMixed(at5) < rawToMixed(at125) &&
            rawToMixed(at125) < rawToMixed(at20) &&
            rawToMixed(at20) < fullDeviation &&
            rawToMixed(at5) <= fullDeviation + 1.0e-6f &&
            rawToMixed(at20) <= fullDeviation + 1.0e-6f,
        "increasing strength monotonically moves the applied position toward the full filter and never past it");
    Check(filteredToMixed(at5) > filteredToMixed(at125) &&
            filteredToMixed(at125) > filteredToMixed(at20) &&
            filteredToMixed(at20) >= 0.0f,
        "increasing strength monotonically closes the distance to the full filtered position");
    Check(Near(rawToMixed(at5), 0.2f * fullDeviation, 1.0e-5f) &&
            Near(rawToMixed(at125), 0.5f * fullDeviation, 1.0e-5f) &&
            Near(rawToMixed(at20), 0.8f * fullDeviation, 1.0e-5f),
        "the applied position offset is exactly mix x the full-filter offset, so the slider is a wet/dry amount and never a slower filter");
    for (const two_hand_input_smoothing::Result* partial :
        {&at5, &at125, &at20})
    {
        Check(angle(partial->raw.primaryOrientation,
                    partial->mixed.primaryOrientation) <=
                angle(partial->raw.primaryOrientation,
                    partial->filtered.primaryOrientation) + 1.0e-3f,
            "a partial strength never adds more rotational lag than the full filter");
        Check(distance(partial->raw.supportAimPosition,
                    partial->mixed.supportAimPosition) <=
                distance(partial->raw.supportAimPosition,
                    partial->filtered.supportAimPosition) + 1.0e-6f,
            "a partial strength never adds more support-position lag than the full filter");
    }
    Check(distance(at125.raw.primaryGripPosition,
                at125.mixed.primaryGripPosition) > 0.0f &&
            distance(at125.raw.primaryGripPosition,
                at125.mixed.primaryGripPosition) <
                distance(at125.raw.primaryGripPosition,
                    at125.filtered.primaryGripPosition),
        "grip copies blend by the same mix and stay short of the full filter");

    // --- Filter independence: the retained history is identical no matter what
    // strength each frame used, and the mixed output is never fed back.
    {
        two_hand_input_smoothing::State partialHistory{};
        two_hand_input_smoothing::State fullHistory{};
        int differingFrames = 0;
        for (int index = 0; index < 6; ++index)
        {
            Sample step = raw;
            step.primaryOrientation = yaw(2.0f * static_cast<float>(index));
            step.primaryAimPosition = Point3{0.04f * index, 1.30f,
                -0.15f - 0.02f * index};
            step.supportAimPosition = Point3{0.01f * index, 1.29f,
                -0.50f - 0.02f * index};
            const uint64_t serial = static_cast<uint64_t>(index) + 40;
            const auto partial = Advance(partialHistory, serial, 0.01f, step,
                6.0f);
            const auto full = Advance(fullHistory, serial, 0.01f, step, 25.0f);
            Check(partial.advanced && full.advanced,
                "both strengths advance the shared filter history");
            // The first frame reseeds both filters exactly raw, so the two
            // strengths agree there; every frame with real filtered deviation
            // must differ.
            if (index > 0)
            {
                Check(!SameSample(partial.mixed, full.mixed),
                    "the strength genuinely changes what consumers receive");
                if (!SameSample(partial.mixed, full.mixed))
                    ++differingFrames;
            }
        }
        Check(differingFrames > 0,
            "the strength genuinely changes what consumers receive");
        Check(SameState(partialHistory, fullHistory),
            "the fixed filter history advances identically for every strength: the mixed output is never fed back");
    }

    // --- A strength change on an already-advanced serial cannot advance the
    // filter a second time, and cannot rewrite the frozen result.
    {
        two_hand_input_smoothing::State state{};
        Sample step = raw;
        step.primaryAimPosition = Point3{1.0f, 0.0f, 0.0f};
        const auto first = Advance(state, 60, 0.01f, step, 5.0f);
        const two_hand_input_smoothing::State snapshot = state;
        Sample other = step;
        other.primaryAimPosition = Point3{-9.0f, 0.0f, 0.0f};
        const auto repeat = Advance(state, 60, 0.04f, other, 25.0f);
        Check(first.advanced && !repeat.advanced && SameState(state, snapshot) &&
                repeat.mix == 1.0f &&
                repeat.filtered.primaryAimPosition.x ==
                    first.filtered.primaryAimPosition.x,
            "changing the strength for the same prepared serial never re-advances or rewrites filter history");
    }

    // --- Stateful lifecycle at partial strength: the first active frame still
    // reseeds exactly raw (so the mix is raw too), and invalid input fails open
    // without touching history.
    {
        two_hand_input_smoothing::State state{};
        Sample moved = raw;
        moved.primaryOrientation = yaw(11.0f);
        moved.primaryAimPosition = Point3{3.0f, 0.0f, 0.0f};
        const auto reseed = Advance(state, 1, 0.01f, moved, 12.5f);
        Check(reseed.valid && reseed.advanced && reseed.mix == 0.5f &&
                SameSample(reseed.filtered, moved) &&
                SameSample(reseed.mixed, moved),
            "the first active frame at a partial strength still reseeds and emits exactly raw");
        Sample bad = moved;
        bad.supportAimPosition.x = std::numeric_limits<float>::quiet_NaN();
        const auto failed = Advance(state, 2, 0.01f, bad, 12.5f);
        Check(!failed.valid && !failed.advanced && state.lastPreparedSerial == 1,
            "a non-finite frame at a partial strength fails open with no advance");
        const auto overflowSeed = Advance(state, 3, 0.01f, moved, 12.5f);
        Check(overflowSeed.valid && overflowSeed.advanced,
            "the filter resumes from its last good history after a failed frame");
    }

    // --- Orientation: shortest-arc blending across the hemisphere/sign
    // boundary must never take the long way around.
    {
        two_hand_input_smoothing::State state{};
        const auto arcSeed = Advance(state, 1, 0.01f, [&] {
            Sample s = raw;
            s.primaryOrientation = yaw(170.0f);
            return s;
        }(), 12.5f);
        Check(arcSeed.advanced, "the partial-strength arc fixture seeds 170 degrees");
        Sample arcEnd = raw;
        arcEnd.primaryOrientation = yaw(-170.0f);
        const auto arc = Advance(state, 2, 0.02f, arcEnd, 12.5f);
        // The filter's alpha=0.5 step from 170 degrees toward -170 degrees takes
        // the 20-degree short arc to 180 degrees; the raw<->filtered blend then
        // sits halfway between -170 and 180, i.e. at 185 degrees (-175). A
        // long-way interpolation would land near -20 (filter) or -95 (blend).
        Check(arc.valid && arc.advanced &&
                Near(angle(arc.filtered.primaryOrientation, yaw(180.0f)), 0.0f,
                    0.1f) &&
                Near(angle(arc.mixed.primaryOrientation, yaw(-175.0f)), 0.0f,
                    0.1f) &&
                Near(angle(arc.raw.primaryOrientation,
                        arc.mixed.primaryOrientation), 5.0f, 0.1f) &&
                Near(angle(arc.raw.primaryOrientation,
                        arc.filtered.primaryOrientation), 10.0f, 0.1f),
            "partial strength blends along the 20-degree shortest arc instead of the long way");
        Check(angle(arc.raw.primaryOrientation, arc.mixed.primaryOrientation) <=
                angle(arc.raw.primaryOrientation,
                    arc.filtered.primaryOrientation) + 1.0e-3f,
            "the partial-strength arc deviation stays inside the full-filter arc");

        Sample a = raw;
        a.primaryOrientation = yaw(37.0f);
        Sample flipped = a;
        flipped.primaryOrientation = Quat4{-yaw(37.0f).x, -yaw(37.0f).y,
            -yaw(37.0f).z, -yaw(37.0f).w};
        const Sample hemisphere =
            two_hand_input_smoothing::BlendByMix(a, flipped, 0.5f);
        Check(virtual_stock::Quat4AngleDegrees(a.primaryOrientation,
                hemisphere.primaryOrientation) <= 0.01f,
            "a sign-flipped (same-rotation) filtered quaternion blends without a 360-degree excursion");
    }

    // --- Consumer scope (2026-09-29): the strength changes only the
    // already-existing filtered directional input copies reaching the two-hand
    // solve, with Virtual Stock on or off. Virtual Stock Standard (rear
    // reference 0) and Plus (rear reference 3) stay byte-identical at strength
    // 0 / packet absent and are a real differential at strength 25, and the
    // weapon/base position stays raw on both.
    {
        AimPoseInputs rawInputs = EquivalenceInputs();
        rawInputs.virtualStockEnabled = false;
        rawInputs.twoHandLabEnabled = false;
        rawInputs.primaryGripValid = true;
        rawInputs.primaryGripPosition = {0.12f, 0.08f, 0.02f};
        rawInputs.supportGripValid = true;
        rawInputs.supportGripPosition = {0.40f, 0.08f, -0.76f};
        const AimPoseResult zeroStrength = ComputeAimPose(rawInputs);
        AimPoseInputs fullStrength = rawInputs;
        fullStrength.twoHandSmoothingGeometryValid = true;
        fullStrength.twoHandSmoothedPrimaryOrientation =
            {0.0f, 0.04f, 0.0f, 0.9991997f};
        fullStrength.twoHandSmoothedPrimaryAimPosition = {0.25f, 0.08f, 0.02f};
        fullStrength.twoHandSmoothedSupportAimPosition = {0.36f, 0.08f, -0.78f};
        fullStrength.twoHandSmoothedPrimaryGripValid = true;
        fullStrength.twoHandSmoothedPrimaryGripPosition = {0.27f, 0.08f, 0.02f};
        fullStrength.twoHandSmoothedSupportGripValid = true;
        fullStrength.twoHandSmoothedSupportGripPosition = {0.38f, 0.08f, -0.80f};
        const AimPoseResult fullSmoothed = ComputeAimPose(fullStrength);
        Check(zeroStrength.valid && fullSmoothed.valid &&
                !Near(AimForward(fullSmoothed).x, AimForward(zeroStrength).x,
                    1.0e-4f) &&
                SamePosition(fullSmoothed.pose.position,
                    rawInputs.right.position) &&
                SamePosition(zeroStrength.pose.position,
                    rawInputs.right.position),
            "the slider endpoints are a real VS-OFF two-hand differential while the base position stays raw");

        for (int rearReference : {0, 3})
        {
            AimPoseInputs stockRaw = rawInputs;
            stockRaw.virtualStockEnabled = true;
            stockRaw.virtualStockRearReference = rearReference;
            const AimPoseResult stockRawResult = ComputeAimPose(stockRaw);
            Check(stockRawResult.valid && stockRawResult.twoHandActive,
                "the Virtual Stock endpoint fixture is a valid stock solve");
            // Strength 0 / packet absent: the flag is false, so even hostile
            // copies must be ignored exactly.
            AimPoseInputs stockAbsent = stockRaw;
            stockAbsent.twoHandSmoothingGeometryValid = false;
            stockAbsent.twoHandSmoothedPrimaryOrientation =
                {0.0f, 0.5f, 0.0f, 0.8660254f};
            stockAbsent.twoHandSmoothedPrimaryAimPosition = {9.0f, 9.0f, 9.0f};
            stockAbsent.twoHandSmoothedSupportAimPosition = {-9.0f, -9.0f, -9.0f};
            stockAbsent.twoHandSmoothedPrimaryGripValid = true;
            stockAbsent.twoHandSmoothedPrimaryGripPosition = {9.0f, 9.0f, 9.0f};
            stockAbsent.twoHandSmoothedSupportGripValid = true;
            stockAbsent.twoHandSmoothedSupportGripPosition = {-9.0f, -9.0f, -9.0f};
            Check(ExactAimResult(ComputeAimPose(stockAbsent), stockRawResult),
                "Virtual Stock Standard/Plus output is bit-identical at smoothing strength 0 (packet absent)");
            // Strength 25 / packet present: the same copies must now move the
            // VS presentation, with the base position still raw.
            AimPoseInputs stockFull = fullStrength;
            stockFull.virtualStockEnabled = true;
            stockFull.virtualStockRearReference = rearReference;
            const AimPoseResult stockFullResult = ComputeAimPose(stockFull);
            Check(stockFullResult.valid && stockFullResult.twoHandActive &&
                    !ExactAimResult(stockFullResult, stockRawResult) &&
                    SamePosition(stockFullResult.pose.position,
                        stockRaw.right.position),
                "Virtual Stock Standard/Plus consumes the smoothed copies at strength 25 as a real differential while the base position stays raw");
        }

        AimPoseInputs oneHandZero = rawInputs;
        oneHandZero.twoHandLatched = false;
        AimPoseInputs oneHandFull = fullStrength;
        oneHandFull.twoHandLatched = false;
        Check(ExactAimResult(ComputeAimPose(oneHandZero),
                ComputeAimPose(oneHandFull)),
            "one-handed aiming ignores the strength-mixed copies exactly");
    }
}

// 2026-09-29 Two-Hand Smoothing scope (Virtual Stock ON). The prepared smoothed
// copies now feed the VS-ON solve; what stays VS-OFF only is the product
// Grip -> Grip geometry and the Two-Handed Lab, and the support endpoint is
// still selected by the RAW-derived grip-vs-aim correspondence (the flag
// travels with the packet identity; it is never re-derived from the smoothed
// copies). These are the solver-level terms the vr.cpp apply gates feed. The
// vr.cpp eligibility gates themselves are not offline-compilable; this suite
// covers the consumption contract they gate, not the gate text.
void TestTwoHandSmoothingVirtualStockConsumption()
{
    AimPoseInputs base = EquivalenceInputs();
    base.virtualStockEnabled = true;
    // Standard head path with the preserved exact head -> support endpoint ray
    // (strength 1.0, rear height 0), so the presented forward is the direction
    // to whichever support endpoint this solve consumed.
    base.virtualStockRearReference = 0;
    base.virtualStockStrength = 1.0f;
    base.virtualStockRearHeightM = 0.0f;
    base.virtualStockProximityRelease = false;
    base.headValid = true;
    base.headPosition = {-0.18f, 0.62f, 0.11f};
    base.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    base.leftValid = true;
    base.left.position = {0.42f, 0.08f, -0.78f};
    base.supportPosition = base.left.position;
    base.primaryGripValid = true;
    base.primaryGripPosition = {0.12f, 0.08f, 0.02f};
    base.supportGripValid = true;
    base.supportGripPosition = {0.40f, 0.08f, -0.76f};
    base.twoHandSmoothingGeometryValid = true;
    base.twoHandSmoothedPrimaryOrientation = {0.0f, 0.04f, 0.0f, 0.9991997f};
    base.twoHandSmoothedPrimaryAimPosition = {0.25f, 0.08f, 0.02f};
    base.twoHandSmoothedSupportAimPosition = {0.36f, 0.08f, -0.78f};
    base.twoHandSmoothedPrimaryGripValid = true;
    base.twoHandSmoothedPrimaryGripPosition = {0.27f, 0.08f, 0.02f};
    base.twoHandSmoothedSupportGripValid = true;
    base.twoHandSmoothedSupportGripPosition = {0.38f, 0.08f, -0.80f};

    const auto directionTo = [](const XrVector3f& from, const XrVector3f& to) {
        const float x = to.x - from.x;
        const float y = to.y - from.y;
        const float z = to.z - from.z;
        const float length = std::sqrt(x * x + y * y + z * z);
        return XrVector3f{x / length, y / length, z / length};
    };
    const auto sameDirection = [&](const XrVector3f& actual,
                                   const XrVector3f& expected) {
        return Near(actual.x, expected.x, 1.0e-4f) &&
            Near(actual.y, expected.y, 1.0e-4f) &&
            Near(actual.z, expected.z, 1.0e-4f);
    };

    AimPoseInputs aimEndpoint = base;
    aimEndpoint.supportEndpointUsedGrip = false;
    const AimPoseResult aimEndpointResult = ComputeAimPose(aimEndpoint);
    AimPoseInputs gripEndpoint = base;
    gripEndpoint.supportEndpointUsedGrip = true;
    const AimPoseResult gripEndpointResult = ComputeAimPose(gripEndpoint);

    Check(aimEndpointResult.valid && gripEndpointResult.valid &&
            aimEndpointResult.twoHandActive &&
            gripEndpointResult.twoHandActive,
        "the VS-ON smoothed-endpoint fixture is a valid stock solve");
    Check(sameDirection(AimForward(aimEndpointResult),
            directionTo(base.headPosition,
                base.twoHandSmoothedSupportAimPosition)) &&
            !sameDirection(AimForward(aimEndpointResult),
                directionTo(base.headPosition, base.left.position)),
        "the VS-ON stock ray consumes the SMOOTHED support aim copy, never the raw support position");
    Check(sameDirection(AimForward(gripEndpointResult),
            directionTo(base.headPosition,
                base.twoHandSmoothedSupportGripPosition)) &&
            !sameDirection(AimForward(gripEndpointResult),
                directionTo(base.headPosition, base.supportGripPosition)),
        "the raw-derived grip correspondence selects the SMOOTHED support grip copy, never the raw grip");
    Check(!ExactAimResult(aimEndpointResult, gripEndpointResult),
        "the raw-derived endpoint correspondence is a real VS-ON differential");

    // A smoothed grip the solve cannot use falls back to the SMOOTHED aim copy
    // (never the raw grip), exactly as the VS-OFF product pair does.
    AimPoseInputs gripUnusable = base;
    gripUnusable.supportEndpointUsedGrip = true;
    gripUnusable.twoHandSmoothedSupportGripValid = false;
    Check(ExactAimResult(ComputeAimPose(gripUnusable), aimEndpointResult),
        "an unusable smoothed support grip falls back to the smoothed aim copy, never the raw grip");

    // The smoothed primary copy steers the VS-ON solve too: with the rear
    // reference built from the primary position (strength < 1), moving only the
    // smoothed primary aim copy moves the presented direction.
    AimPoseInputs rearFromPrimary = base;
    rearFromPrimary.supportEndpointUsedGrip = false;
    rearFromPrimary.virtualStockStrength = 0.73f;
    rearFromPrimary.virtualStockRearHeightM = -0.12f;
    AimPoseInputs rearMoved = rearFromPrimary;
    rearMoved.twoHandSmoothedPrimaryAimPosition = {0.40f, 0.08f, 0.02f};
    const AimPoseResult rearBaseResult = ComputeAimPose(rearFromPrimary);
    const AimPoseResult rearMovedResult = ComputeAimPose(rearMoved);
    Check(rearBaseResult.valid && rearMovedResult.valid &&
            rearBaseResult.twoHandActive && rearMovedResult.twoHandActive &&
            !ExactAimResult(rearBaseResult, rearMovedResult) &&
            SamePosition(rearMovedResult.pose.position, base.right.position),
        "the smoothed primary copy steers the VS-ON stock ray while the base position stays raw");

    // The smoothed primary ORIENTATION stays the VS-ON roll baseline.
    AimPoseInputs rollMoved = base;
    rollMoved.supportEndpointUsedGrip = false;
    rollMoved.twoHandSmoothedPrimaryOrientation = {0.05f, 0.04f, 0.02f, 0.9977f};
    const AimPoseResult rollMovedResult = ComputeAimPose(rollMoved);
    Check(rollMovedResult.valid && rollMovedResult.twoHandActive &&
            !ExactAimResult(rollMovedResult, aimEndpointResult),
        "the smoothed primary orientation stays a VS-ON orientation input");

    // The Two-Handed Lab stays VS-OFF only: with the Lab enabled and the
    // smoothing packet present, a VS-ON solve publishes no Lab diagnostics.
    AimPoseInputs labOnStock = base;
    labOnStock.twoHandLabEnabled = true;
    labOnStock.twoHandLabAnchor = 4;
    labOnStock.twoHandLabOffhandInfluence = 1.0f;
    AimPoseTrace labStockTrace{};
    const AimPoseResult labStockResult =
        ComputeAimPose(labOnStock, &labStockTrace);
    Check(labStockResult.valid && labStockResult.twoHandActive &&
            !labStockTrace.labValid,
        "the Two-Handed Lab never steers or reports a VS-ON solve even with smoothed copies present");
}

void CheckFixedStockEquivalence(
    int fixedReference, int hybridAdsReference, bool leftHanded,
    const char* message)
{
    AimPoseInputs fixed = EquivalenceInputs();
    fixed.virtualStockRearReference = fixedReference;
    fixed.virtualStockLeftHanded = leftHanded;
    const AimPoseResult fixedResult = ComputeAimPose(fixed);

    AimPoseInputs hybrid = fixed;
    hybrid.virtualStockRearReference = 3;
    hybrid.virtualStockHybridAdsReference = hybridAdsReference;
    hybrid.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    const AimPoseResult hybridResult = ComputeAimPose(hybrid);

    Check(SameAimResult(hybridResult, fixedResult, 2.0e-5f), message);
}

void TestHybridStockEquivalence()
{
    CheckFixedStockEquivalence(
        0, 0, false,
        "Hybrid Force Stock Head matches fixed Head with proximity disabled");
    CheckFixedStockEquivalence(
        1, 1, false,
        "Hybrid Force Stock Shoulder matches fixed Shoulder with proximity disabled");
    CheckFixedStockEquivalence(
        1, 1, true,
        "Hybrid Force Stock Shoulder preserves left-handed mirroring");
}

void TestHybridDiagnosticOverrides()
{
    AimPoseInputs inputs = HybridInputs();
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceHip;
    const AimPoseResult forceHip = ComputeAimPose(inputs);
    virtual_stock::Point3 support{0.6f, 0.0f, -1.0f};
    virtual_stock::Point3 offhand{};
    bool rejectedExtreme = false;
    float rejectedAgreement = 0.0f;
    Check(virtual_stock::TryBuildAcceptedSupportDirection(
            {0.0f, 0.0f, 0.0f}, support, {0.0f, 0.0f, -1.0f},
            offhand, rejectedExtreme, rejectedAgreement),
        "Force Hip fixture accepts B");
    virtual_stock::Point3 expectedHip{};
    Check(virtual_stock::TryBlendDirectionAuthority(
            {0.0f, 0.0f, -1.0f}, offhand, 0.50f, expectedHip),
        "Force Hip fixture builds normal A/B HipAim");
    Check(forceHip.valid && forceHip.twoHandActive &&
            SamePosition(AimForward(forceHip),
                {expectedHip.x, expectedHip.y, expectedHip.z}),
        "Force Hip ignores natural W and leaves normal HipAim active");

    inputs = HybridInputs();
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    inputs.right.orientation = {0.0f, std::sin(0.2f), 0.0f, std::cos(0.2f)};
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceHip;
    const AimPoseResult pureHip = ComputeAimPose(inputs);
    Check(pureHip.valid && pureHip.twoHandActive &&
            SameQuaternion(pureHip.pose.orientation, inputs.right.orientation),
        "Force Hip with zero influence preserves exact A and active presentation");

    inputs = HybridInputs();
    inputs.supportPosition = {1.0f, 0.0f, 0.0f};
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceHip;
    const AimPoseResult rejectedHip = ComputeAimPose(inputs);
    Check(rejectedHip.valid && !rejectedHip.twoHandActive &&
            SameQuaternion(rejectedHip.pose.orientation, inputs.right.orientation),
        "Force Hip with rejected B falls back to exact A and inactive presentation");

    inputs = HybridInputs();
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    const AimPoseResult forceStock = ComputeAimPose(inputs);
    const float supportLength = std::sqrt(1.36f);
    Check(forceStock.valid && forceStock.twoHandActive &&
            SamePosition(AimForward(forceStock),
                {0.6f / supportLength, 0.0f, -1.0f / supportLength}),
        "Force Stock gives valid C full final authority");

    inputs.virtualStockStrength = 0.0f;
    const AimPoseResult zeroStrengthStock = ComputeAimPose(inputs);
    Check(zeroStrengthStock.valid && zeroStrengthStock.twoHandActive &&
            SamePosition(AimForward(zeroStrengthStock),
                {expectedHip.x, expectedHip.y, expectedHip.z}),
        "Force Stock cannot manufacture C when Hybrid strength is zero");

    inputs = HybridInputs();
    inputs.headValid = false;
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    const AimPoseResult noHeadStock = ComputeAimPose(inputs);
    Check(noHeadStock.valid && noHeadStock.twoHandActive &&
            SamePosition(AimForward(noHeadStock),
                {expectedHip.x, expectedHip.y, expectedHip.z}),
        "Force Stock falls back to HipAim when HMD is unavailable");

    inputs = HybridInputs();
    inputs.headPosition.x = NAN;
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    const AimPoseResult invalidTargetStock = ComputeAimPose(inputs);
    Check(invalidTargetStock.valid && invalidTargetStock.twoHandActive &&
            SamePosition(AimForward(invalidTargetStock),
                {expectedHip.x, expectedHip.y, expectedHip.z}),
        "Force Stock falls back to HipAim for a non-finite target");

    inputs = HybridInputs();
    inputs.right.position = {0.2f, 0.0f, 0.0f};
    inputs.supportPosition = {0.2f, 0.0f, 0.0f};
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    const AimPoseResult rejectedBStock = ComputeAimPose(inputs);
    Check(rejectedBStock.valid && rejectedBStock.twoHandActive &&
            SamePosition(AimForward(rejectedBStock), {1.0f, 0.0f, 0.0f}),
        "Force Stock lets valid C rescue a rejected B");

    inputs = HybridInputs();
    inputs.supportPosition = {1.0f, 0.0f, 0.0f};
    inputs.headPosition = {1.0f, 0.0f, 0.0f};
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    const AimPoseResult invalidCStock = ComputeAimPose(inputs);
    Check(invalidCStock.valid && !invalidCStock.twoHandActive &&
            SameQuaternion(invalidCStock.pose.orientation, inputs.right.orientation),
        "Force Stock keeps rejected-B/invalid-C inactive at exact A");
}

void TestHybridDiagnosticModeIsolation()
{
    const HybridDiagnosticOverride overrides[] = {
        HybridDiagnosticOverride::ForceHip,
        HybridDiagnosticOverride::ForceStock};
    const int fixedModes[] = {0, 1, 2};
    for (const int mode : fixedModes)
    {
        AimPoseInputs baseline = EquivalenceInputs();
        baseline.virtualStockRearReference = mode;
        baseline.hybridDiagnosticOverride = HybridDiagnosticOverride::Normal;
        const AimPoseResult expected = ComputeAimPose(baseline);
        for (const HybridDiagnosticOverride diagnostic : overrides)
        {
            AimPoseInputs isolated = baseline;
            isolated.hybridDiagnosticOverride = diagnostic;
            const AimPoseResult actual = ComputeAimPose(isolated);
            Check(SameAimResult(actual, expected, 2.0e-5f),
                "Hybrid diagnostic override is ignored by fixed modes 0-2");
        }
    }

    AimPoseInputs stockOff = EquivalenceInputs();
    stockOff.virtualStockEnabled = false;
    stockOff.virtualStockRearReference = 0;
    const AimPoseResult expectedOff = ComputeAimPose(stockOff);
    for (const HybridDiagnosticOverride diagnostic : overrides)
    {
        stockOff.hybridDiagnosticOverride = diagnostic;
        Check(SameAimResult(ComputeAimPose(stockOff), expectedOff, 2.0e-5f),
            "Hybrid diagnostic override is ignored when Virtual Stock is off");
    }
}

bool SameVirtualStockSettings(
    const VirtualStockAimSettings& actual,
    const VirtualStockAimSettings& expected,
    float epsilon);

void CheckTraceEquivalence(const AimPoseInputs& inputs, const char* message)
{
    const AimPoseResult withoutTrace = ComputeAimPose(inputs, nullptr);
    AimPoseTrace trace{};
    const AimPoseResult withTrace = ComputeAimPose(inputs, &trace);
    Check(ExactAimResult(withTrace, withoutTrace), message);
}

void TestAimTraceObservationalEquivalence()
{
    AimPoseInputs inputs = EquivalenceInputs();
    inputs.rightValid = false;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for invalid primary input");

    inputs = EquivalenceInputs();
    inputs.twoHandEnabled = false;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for one-hand aim");

    inputs = EquivalenceInputs();
    inputs.virtualStockEnabled = false;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for legacy two-hand aim");

    inputs = EquivalenceInputs();
    inputs.virtualStockRearReference = 0;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for fixed Head aim");

    inputs.virtualStockRearReference = 1;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for fixed Shoulder aim");

    inputs.virtualStockRearReference = 2;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for fixed Chest aim");

    inputs = EquivalenceInputs();
    inputs.virtualStockRearReference = 3;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for normal Hybrid aim");

    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceHip;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for Force Hip aim");
    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for Force Stock aim");

    inputs = EquivalenceInputs();
    ApplyVirtualStockAimSettings(
        inputs,
        MakeHybridHorizontalReleaseProfileSettings(0.270f, 0.425f),
        static_cast<VirtualStockTestProfile>(
            kVirtualStockExperimentProfileFirstId));
    CheckTraceEquivalence(inputs,
        "Null and non-null traces are exactly equivalent for horizontal-release aim");
}

// T13 corrective: retained support steering for a qualified persistent-grip
// invocation on the VS-off legacy path. The fixture geometry puts the
// primary -> support agreement at ~0.310, i.e. below the legacy 0.35 floor.
AimPoseInputs RetainedLegacyInputs()
{
    AimPoseInputs inputs{};
    inputs.rightValid = true;
    inputs.right.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.right.position = {0.0f, 0.0f, 0.0f};
    inputs.leftValid = true;
    inputs.left.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.left.position = {0.95f, 0.0f, -0.31f};
    inputs.supportPosition = {0.95f, 0.0f, -0.31f};
    inputs.twoHandEnabled = true;
    inputs.twoHandLatched = true;
    inputs.virtualStockEnabled = false;
    inputs.twoHandToggle = false;
    inputs.gunYawDeg = 0.0f;
    inputs.gunPitchDeg = 0.0f;
    inputs.gunRollDeg = 0.0f;
    return inputs;
}

void TestPersistentGripRetainedSteering()
{
    AimPoseInputs inputs = RetainedLegacyInputs();
    const float supportLength = std::sqrt(
        0.95f * 0.95f + 0.31f * 0.31f);
    const virtual_stock::Point3 expectedSupportDirection{
        0.95f / supportLength, 0.0f, -0.31f / supportLength};
    const float expectedAgreement = -expectedSupportDirection.z;
    Check(expectedAgreement < 0.35f && expectedAgreement > 0.30f,
        "the retained-steering fixture crosses the legacy floor");

    // Unretained (PG off / untrusted / disengaged): the pre-fix behaviour is
    // byte-identical - the floor rejects and the solve stays one-hand.
    AimPoseTrace unretainedTrace{};
    const AimPoseResult unretained = ComputeAimPose(inputs, &unretainedTrace);
    Check(unretained.valid && !unretained.twoHandActive &&
            unretained.rejectedExtreme &&
            Near(unretained.rejectedAgreement, expectedAgreement, 1.0e-4f) &&
            SameQuaternion(unretained.pose.orientation,
                inputs.right.orientation),
        "an unretained sub-floor invocation keeps the exact one-hand pose");
    Check(unretainedTrace.path == AimSolverPath::LegacyTwoHand &&
            unretainedTrace.bAttempted && !unretainedTrace.bAccepted &&
            unretainedTrace.bExtremeRejected &&
            !unretainedTrace.bSteeringRetained &&
            Near(unretainedTrace.bRejectedAgreement, expectedAgreement,
                1.0e-4f) &&
            unretainedTrace.exactAEndpointSelected &&
            Near(unretainedTrace.bAgreement, expectedAgreement, 1.0e-4f),
        "the unretained trace reports the rejection with its floor agreement");

    // Retained (engaged + trusted persistent grip, Virtual Stock off): the
    // solve keeps the support-derived orientation instead of the one-hand
    // fallback, and the receipt says support geometry steered the pose.
    inputs.supportSteeringRetained = true;
    inputs.supportMayConsume = true;
    AimPoseTrace retainedTrace{};
    const AimPoseResult retained = ComputeAimPose(inputs, &retainedTrace);
    Check(retained.valid && retained.twoHandActive &&
            !retained.rejectedExtreme && retained.supportTrusted,
        "a retained invocation keeps two-hand support authority");
    Check(SamePosition(retained.pose.position, inputs.right.position),
        "retained steering never moves the primary base position");
    Check(Near(AimForward(retained).x, expectedSupportDirection.x, 1.0e-4f) &&
            Near(AimForward(retained).y, expectedSupportDirection.y, 1.0e-4f) &&
            Near(AimForward(retained).z, expectedSupportDirection.z, 1.0e-4f),
        "retained steering resolves onto the exact legacy support direction");
    Check(retainedTrace.path == AimSolverPath::LegacyTwoHand &&
            retainedTrace.bAttempted && retainedTrace.bAccepted &&
            retainedTrace.bSteeringRetained &&
            !retainedTrace.bExtremeRejected &&
            retainedTrace.bRejectedAgreement == 0.0f &&
            !retainedTrace.exactAEndpointSelected &&
            retainedTrace.orientationRebuildAttempted &&
            retainedTrace.orientationRebuildSucceeded &&
            Near(retainedTrace.bAgreement, expectedAgreement, 1.0e-4f) &&
            Near(retainedTrace.bAgreement, unretainedTrace.bAgreement, 1.0e-6f),
        "the retained trace reports acceptance, the crossed floor agreement "
        "and a rebuilt orientation");

    // The retention input cannot leak into the Virtual Stock path: with VS on
    // and no usable stock ray the legacy fallback keeps its floor.
    AimPoseInputs stockFallback = RetainedLegacyInputs();
    stockFallback.virtualStockEnabled = true;
    stockFallback.virtualStockStrength = 1.0f;
    stockFallback.virtualStockRearHeightM = 0.0f;
    stockFallback.virtualStockRearReference = 0;
    stockFallback.supportSteeringRetained = true;
    stockFallback.supportMayConsume = true;
    AimPoseTrace stockFallbackTrace{};
    const AimPoseResult stockFallbackResult =
        ComputeAimPose(stockFallback, &stockFallbackTrace);
    Check(stockFallbackResult.valid && !stockFallbackResult.twoHandActive &&
            stockFallbackResult.rejectedExtreme &&
            !stockFallbackTrace.bAccepted &&
            !stockFallbackTrace.bSteeringRetained &&
            stockFallbackTrace.bExtremeRejected,
        "the retention input never changes a Virtual Stock invocation");
}

// Free two-hand production geometry (W2). With Virtual Stock OFF and the hold
// latched, the positional B line is primary Grip position -> support Grip
// position ("GG"). The pair is a fixed product rule: no Virtual Stock knob and
// no "Reduce Support-Hand Rotation" selection may change it, the orientation
// and roll baseline stays the primary Aim controller, the weapon base position
// stays the raw primary position, and the aim-position pair is the only
// fallback when a required grip sample is missing or unusable.
void TestFixedGripProductionGeometry()
{
    const auto base = []() {
        AimPoseInputs inputs{};
        inputs.rightValid = true;
        // 12-degree pitch so the primary quaternion cannot be mistaken for the
        // identity.
        inputs.right.orientation = {0.1045285f, 0.0f, 0.0f, 0.9945219f};
        inputs.right.position = {0.18f, 1.31f, -0.24f};
        inputs.leftValid = true;
        inputs.left.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        inputs.left.position = {0.02f, 1.29f, -0.58f};
        inputs.supportPosition = inputs.left.position;
        inputs.twoHandEnabled = true;
        inputs.twoHandLatched = true;
        inputs.twoHandLabEnabled = false;
        inputs.virtualStockEnabled = false;
        inputs.twoHandToggle = false;
        inputs.primaryGripValid = true;
        inputs.primaryGripPosition = {0.13f, 1.30f, -0.20f};
        inputs.supportGripValid = true;
        inputs.supportGripPosition = {0.05f, 1.30f, -0.62f};
        return inputs;
    };
    const auto normalize = [](const XrVector3f& value) {
        const float length = std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z);
        return XrVector3f{
            value.x / length, value.y / length, value.z / length};
    };
    const auto directionBetween = [&normalize](
        const XrVector3f& from, const XrVector3f& to) {
        return normalize(
            XrVector3f{to.x - from.x, to.y - from.y, to.z - from.z});
    };
    // Independent roll reference: the primary controller's own up vector,
    // orthogonalized against the aim direction (never the support controller
    // and never the headset).
    const auto rollReference = [&normalize](
        const XrQuaternionf& primaryOrientation, const XrVector3f& forward) {
        const XrVector3f up = Rotate(primaryOrientation, {0.0f, 1.0f, 0.0f});
        const float along = up.x * forward.x + up.y * forward.y +
            up.z * forward.z;
        return normalize(XrVector3f{
            up.x - along * forward.x, up.y - along * forward.y,
            up.z - along * forward.z});
    };

    const AimPoseInputs gg = base();
    const XrVector3f ggForward = directionBetween(
        gg.primaryGripPosition, gg.supportGripPosition);
    AimPoseTrace ggTrace{};
    const AimPoseResult result = ComputeAimPose(gg, &ggTrace);
    Check(result.valid && result.twoHandActive &&
            ggTrace.path == AimSolverPath::LegacyTwoHand,
        "the VS-OFF two-hand solve steers with the fixed Grip pair");
    Check(Near(AimForward(result).x, ggForward.x, 1.0e-4f) &&
            Near(AimForward(result).y, ggForward.y, 1.0e-4f) &&
            Near(AimForward(result).z, ggForward.z, 1.0e-4f),
        "the VS-OFF product direction is primary Grip -> support Grip");
    Check(SamePosition(Rotate(result.pose.orientation, {0.0f, 1.0f, 0.0f}),
            rollReference(gg.right.orientation, ggForward)),
        "the orientation/roll baseline stays the primary Aim controller");
    Check(SamePosition(result.pose.position, gg.right.position),
        "the weapon/base position stays the raw primary position");

    // Positional pivots only: neither the primary AIM position nor the support
    // AIM position may enter the direction.
    {
        AimPoseInputs movedPrimaryAim = gg;
        movedPrimaryAim.right.position = {-0.42f, 1.05f, 0.33f};
        const AimPoseResult moved = ComputeAimPose(movedPrimaryAim);
        Check(moved.valid && moved.twoHandActive &&
                SamePosition(moved.pose.position,
                    movedPrimaryAim.right.position) &&
                SamePosition(AimForward(moved), AimForward(result)),
            "the primary Aim position moves the base position only, never the GG direction");
        AimPoseInputs movedSupportAim = gg;
        movedSupportAim.left.position = {0.55f, 1.02f, -1.10f};
        movedSupportAim.supportPosition = {0.55f, 1.02f, -1.10f};
        const AimPoseResult movedSupport = ComputeAimPose(movedSupportAim);
        Check(movedSupport.valid && movedSupport.twoHandActive &&
                SamePosition(AimForward(movedSupport), AimForward(result)),
            "the support Aim position no longer steers the VS-OFF product line");
        AimPoseInputs rotatedSupport = gg;
        rotatedSupport.left.orientation = {0.5f, 0.5f, 0.5f, 0.5f};
        const AimPoseResult rotated = ComputeAimPose(rotatedSupport);
        Check(ExactAimResult(rotated, result),
            "support-controller rotation keeps no GG authority");
    }

    // Reduce Support-Hand Rotation is not an input of the fixed pair: both
    // states of its support-endpoint selection solve exactly the same.
    {
        AimPoseInputs rsrOn = gg;
        rsrOn.supportGripPoseEnabled = true;
        rsrOn.supportEndpointUsedGrip = true;
        rsrOn.supportPosition = gg.supportGripPosition;
        AimPoseInputs rsrOff = gg;
        rsrOff.supportGripPoseEnabled = false;
        rsrOff.supportEndpointUsedGrip = false;
        rsrOff.supportPosition = gg.left.position;
        Check(ExactAimResult(ComputeAimPose(rsrOn), ComputeAimPose(rsrOff)) &&
                ExactAimResult(ComputeAimPose(rsrOn), result),
            "the RSR endpoint selection cannot change the fixed GG solve");
        // The differential is real: the aim-line pair is a different geometry.
        AimPoseInputs aimLine = gg;
        aimLine.primaryGripValid = false;
        aimLine.supportGripValid = false;
        Check(!ExactAimResult(ComputeAimPose(aimLine), result),
            "the aim-position line is a real differential against GG");
    }

    // Grip fallbacks: a missing or non-finite required grip resolves to the
    // exact aim-position geometry (the pre-GG product pair), never a mixed
    // pair and never a fabricated position.
    {
        AimPoseInputs aimOnly = gg;
        aimOnly.primaryGripValid = false;
        aimOnly.supportGripValid = false;
        const AimPoseResult expected = ComputeAimPose(aimOnly);
        Check(expected.valid && expected.twoHandActive,
            "the aim-position fallback still steers");
        AimPoseInputs missingSupportGrip = gg;
        missingSupportGrip.supportGripValid = false;
        AimPoseInputs missingPrimaryGrip = gg;
        missingPrimaryGrip.primaryGripValid = false;
        AimPoseInputs nanSupportGrip = gg;
        nanSupportGrip.supportGripPosition.x =
            std::numeric_limits<float>::quiet_NaN();
        AimPoseInputs infPrimaryGrip = gg;
        infPrimaryGrip.primaryGripPosition.y =
            std::numeric_limits<float>::infinity();
        const AimPoseResult fallbacks[] = {
            ComputeAimPose(missingSupportGrip),
            ComputeAimPose(missingPrimaryGrip),
            ComputeAimPose(nanSupportGrip),
            ComputeAimPose(infPrimaryGrip)};
        for (const AimPoseResult& fallback : fallbacks)
        {
            Check(ExactAimResult(fallback, expected) && fallback.valid &&
                    std::isfinite(fallback.pose.position.x) &&
                    std::isfinite(fallback.pose.position.y) &&
                    std::isfinite(fallback.pose.position.z) &&
                    std::isfinite(fallback.pose.orientation.x) &&
                    std::isfinite(fallback.pose.orientation.y) &&
                    std::isfinite(fallback.pose.orientation.z) &&
                    std::isfinite(fallback.pose.orientation.w),
                "an unusable required grip falls back to the exact aim-position pair");
        }
    }

    // Prepared-serial smoothed copies: when this solve carries them, the pair
    // comes from the smoothed grips (never a mix of smoothed and raw), and the
    // roll baseline is the smoothed primary orientation.
    {
        AimPoseInputs smoothed = gg;
        smoothed.twoHandSmoothingGeometryValid = true;
        smoothed.twoHandSmoothedPrimaryOrientation = gg.right.orientation;
        smoothed.twoHandSmoothedPrimaryAimPosition = {-0.31f, 1.28f, -0.19f};
        smoothed.twoHandSmoothedSupportAimPosition = {0.11f, 1.30f, -0.55f};
        smoothed.twoHandSmoothedPrimaryGripValid = true;
        smoothed.twoHandSmoothedPrimaryGripPosition = {0.10f, 1.30f, -0.18f};
        smoothed.twoHandSmoothedSupportGripValid = true;
        smoothed.twoHandSmoothedSupportGripPosition = {0.07f, 1.30f, -0.66f};
        const AimPoseResult smoothedResult = ComputeAimPose(smoothed);
        const XrVector3f smoothedForward = directionBetween(
            smoothed.twoHandSmoothedPrimaryGripPosition,
            smoothed.twoHandSmoothedSupportGripPosition);
        Check(smoothedResult.valid && smoothedResult.twoHandActive &&
                !ExactAimResult(smoothedResult, result) &&
                Near(AimForward(smoothedResult).x, smoothedForward.x, 1.0e-4f) &&
                Near(AimForward(smoothedResult).y, smoothedForward.y, 1.0e-4f) &&
                Near(AimForward(smoothedResult).z, smoothedForward.z, 1.0e-4f) &&
                SamePosition(smoothedResult.pose.position, gg.right.position),
            "the smoothed grip copies, not the raw pair, define the smoothed GG solve");
        Check(SamePosition(
                Rotate(smoothedResult.pose.orientation, {0.0f, 1.0f, 0.0f}),
                rollReference(smoothed.twoHandSmoothedPrimaryOrientation,
                    smoothedForward)),
            "the smoothed primary orientation stays the roll baseline");

        AimPoseInputs smoothedSupportMissing = smoothed;
        smoothedSupportMissing.twoHandSmoothedSupportGripValid = false;
        AimPoseInputs smoothedAimOnly = smoothed;
        smoothedAimOnly.twoHandSmoothedPrimaryGripValid = false;
        smoothedAimOnly.twoHandSmoothedSupportGripValid = false;
        Check(ExactAimResult(ComputeAimPose(smoothedSupportMissing),
                ComputeAimPose(smoothedAimOnly)),
            "an unusable smoothed grip falls back to the smoothed aim pair, never the raw grip");
    }

    // Virtual Stock keeps the Reduce Support-Hand Rotation endpoint: the stock
    // ray still resolves head -> selected support endpoint on both settings.
    {
        AimPoseInputs stock = gg;
        stock.virtualStockEnabled = true;
        stock.virtualStockStrength = 1.0f;
        stock.virtualStockRearHeightM = 0.0f;
        stock.virtualStockRearReference = 0;
        stock.virtualStockProximityRelease = false;
        stock.headValid = true;
        stock.headPosition = {0.0f, 1.60f, 0.0f};
        stock.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
        AimPoseInputs aimEndpoint = stock;
        aimEndpoint.supportGripPoseEnabled = false;
        aimEndpoint.supportEndpointUsedGrip = false;
        aimEndpoint.supportPosition = stock.left.position;
        AimPoseInputs gripEndpoint = stock;
        gripEndpoint.supportGripPoseEnabled = true;
        gripEndpoint.supportEndpointUsedGrip = true;
        gripEndpoint.supportPosition = stock.supportGripPosition;
        const AimPoseResult aimEndpointResult = ComputeAimPose(aimEndpoint);
        const AimPoseResult gripEndpointResult = ComputeAimPose(gripEndpoint);
        const XrVector3f aimStock = directionBetween(
            aimEndpoint.headPosition, aimEndpoint.left.position);
        const XrVector3f gripStock = directionBetween(
            gripEndpoint.headPosition, gripEndpoint.supportGripPosition);
        Check(aimEndpointResult.valid && gripEndpointResult.valid &&
                aimEndpointResult.twoHandActive &&
                gripEndpointResult.twoHandActive &&
                !ExactAimResult(aimEndpointResult, gripEndpointResult),
            "Virtual Stock still consumes the RSR support endpoint");
        Check(Near(AimForward(aimEndpointResult).x, aimStock.x, 1.0e-4f) &&
                Near(AimForward(aimEndpointResult).y, aimStock.y, 1.0e-4f) &&
                Near(AimForward(aimEndpointResult).z, aimStock.z, 1.0e-4f) &&
                Near(AimForward(gripEndpointResult).x, gripStock.x, 1.0e-4f) &&
                Near(AimForward(gripEndpointResult).y, gripStock.y, 1.0e-4f) &&
                Near(AimForward(gripEndpointResult).z, gripStock.z, 1.0e-4f),
            "the Virtual Stock ray still resolves head -> the selected support endpoint");
    }
}

// Free two-hand offhand influence (W3). With Virtual Stock OFF and the hold
// latched, twoHandOffhandInfluence selects the DIRECTIONAL AUTHORITY of the
// accepted support direction: 0 keeps the exact primary orientation with no
// positional rebuild, 1 keeps the pre-W3 full accepted-B presentation, and an
// intermediate value blends primary direction -> accepted B through the same
// tested TryBlendDirectionAuthority the Lab consumes. Acceptance, the active
// two-hand relationship and the base pose never depend on it.
void TestFreeTwoHandOffhandInfluence()
{
    const auto base = []() {
        AimPoseInputs inputs{};
        inputs.rightValid = true;
        // 12-degree pitch so the primary quaternion cannot be mistaken for the
        // identity.
        inputs.right.orientation = {0.1045285f, 0.0f, 0.0f, 0.9945219f};
        inputs.right.position = {0.18f, 1.31f, -0.24f};
        inputs.leftValid = true;
        inputs.left.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        inputs.left.position = {0.02f, 1.29f, -0.58f};
        inputs.supportPosition = inputs.left.position;
        inputs.twoHandEnabled = true;
        inputs.twoHandLatched = true;
        inputs.virtualStockEnabled = false;
        inputs.primaryGripValid = true;
        inputs.primaryGripPosition = {0.13f, 1.30f, -0.20f};
        inputs.supportGripValid = true;
        inputs.supportGripPosition = {0.05f, 1.30f, -0.62f};
        return inputs;
    };
    const auto normalize = [](const XrVector3f& value) {
        const float length = std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z);
        return XrVector3f{
            value.x / length, value.y / length, value.z / length};
    };
    const auto dot = [](const XrVector3f& a, const XrVector3f& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    const auto directionBetween = [&normalize](
        const XrVector3f& from, const XrVector3f& to) {
        return normalize(
            XrVector3f{to.x - from.x, to.y - from.y, to.z - from.z});
    };

    const AimPoseInputs fullInputs = base();
    AimPoseTrace fullTrace{};
    const AimPoseResult full = ComputeAimPose(fullInputs, &fullTrace);
    const XrVector3f primaryForward = normalize(
        Rotate(fullInputs.right.orientation, {0.0f, 0.0f, -1.0f}));
    const XrVector3f acceptedB = directionBetween(
        fullInputs.primaryGripPosition, fullInputs.supportGripPosition);
    Check(full.valid && full.twoHandActive &&
            fullTrace.path == AimSolverPath::LegacyTwoHand &&
            fullTrace.bAccepted && fullTrace.orientationRebuildSucceeded &&
            !fullTrace.exactAEndpointSelected &&
            Near(AimForward(full).x, acceptedB.x, 1.0e-4f) &&
            Near(AimForward(full).y, acceptedB.y, 1.0e-4f) &&
            Near(AimForward(full).z, acceptedB.z, 1.0e-4f),
        "100% authority rebuilds onto the exact accepted support direction");
    Check(!SamePosition(primaryForward, acceptedB),
        "the primary direction and accepted B are a real differential");
    Check(fullInputs.twoHandOffhandInfluence == 1.0f &&
            ExactAimResult(ComputeAimPose(base()), full),
        "an unstamped influence keeps the pre-W3 full accepted-B behaviour exactly");

    // 0%: primary Aim-orientation authority only. The accepted B relationship
    // stays active (authority is not the logical hold) and no positional
    // rebuild happens, so the presented orientation is the exact primary
    // quaternion.
    AimPoseInputs zeroInputs = base();
    zeroInputs.twoHandOffhandInfluence = 0.0f;
    AimPoseTrace zeroTrace{};
    const AimPoseResult zero = ComputeAimPose(zeroInputs, &zeroTrace);
    Check(zero.valid && zero.twoHandActive && zero.updateTwoHandActivity &&
            SameQuaternion(zero.pose.orientation, zeroInputs.right.orientation),
        "0% presents the exact primary orientation");
    Check(!zeroTrace.orientationRebuildAttempted &&
            zeroTrace.exactAEndpointSelected && zeroTrace.bAttempted &&
            zeroTrace.bAccepted &&
            SamePosition(AimForward(zero), primaryForward),
        "0% keeps accepted B active with no positional rebuild");
    Check(zero.valid == full.valid &&
            zero.updateTwoHandActivity == full.updateTwoHandActivity &&
            zero.twoHandActive == full.twoHandActive &&
            zero.rejectedExtreme == full.rejectedExtreme &&
            zero.rejectedAgreement == full.rejectedAgreement &&
            SamePosition(zero.pose.position, full.pose.position) &&
            zeroTrace.path == fullTrace.path &&
            zeroTrace.bAccepted == fullTrace.bAccepted,
        "zero authority changes the presented orientation only: relationship, "
        "acceptance, rejection and base pose are identical");
    Check(!SameQuaternion(zero.pose.orientation, full.pose.orientation, 1.0e-4f),
        "0% and 100% present genuinely different orientations");

    // The product default (0.50) is the tuned A<->B blend and must sit strictly
    // between the two endpoints.
    Check(kTwoHandOffhandInfluenceDefault == 0.5f,
        "the free two-hand offhand influence default is exactly half authority");
    AimPoseInputs halfInputs = base();
    halfInputs.twoHandOffhandInfluence = kTwoHandOffhandInfluenceDefault;
    AimPoseTrace halfTrace{};
    const AimPoseResult half = ComputeAimPose(halfInputs, &halfTrace);
    const XrVector3f expectedHalf = normalize(XrVector3f{
        primaryForward.x * 0.5f + acceptedB.x * 0.5f,
        primaryForward.y * 0.5f + acceptedB.y * 0.5f,
        primaryForward.z * 0.5f + acceptedB.z * 0.5f});
    Check(Near(AimForward(half).x, expectedHalf.x, 1.0e-4f) &&
            Near(AimForward(half).y, expectedHalf.y, 1.0e-4f) &&
            Near(AimForward(half).z, expectedHalf.z, 1.0e-4f),
        "50% presents the tested half-and-half primary -> accepted-B blend");
    Check(dot(AimForward(half), acceptedB) >
                  dot(primaryForward, acceptedB) + 1.0e-4f &&
            dot(AimForward(half), acceptedB) < 1.0f - 1.0e-4f &&
            !SamePosition(AimForward(half), primaryForward) &&
            !SamePosition(AimForward(half), acceptedB),
        "50% is strictly between the primary direction and accepted B");
    Check(half.valid && half.twoHandActive &&
            halfTrace.orientationRebuildAttempted &&
            halfTrace.orientationRebuildSucceeded &&
            !halfTrace.exactAEndpointSelected,
        "an intermediate influence still rebuilds from accepted B and stays active");

    // Monotonic: more requested authority always moves the presented direction
    // strictly closer to accepted B.
    {
        const float influences[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        float previousAlignment = -2.0f;
        float previousDistanceFromPrimary = -1.0f;
        bool monotonic = true;
        for (const float influence : influences)
        {
            AimPoseInputs inputs = base();
            inputs.twoHandOffhandInfluence = influence;
            const XrVector3f forward = AimForward(ComputeAimPose(inputs));
            const float alignment = dot(forward, acceptedB);
            const XrVector3f fromPrimary{
                forward.x - primaryForward.x,
                forward.y - primaryForward.y,
                forward.z - primaryForward.z};
            const float distance = std::sqrt(dot(fromPrimary, fromPrimary));
            monotonic = monotonic && alignment > previousAlignment &&
                distance > previousDistanceFromPrimary &&
                alignment <= 1.0f + 1.0e-5f;
            previousAlignment = alignment;
            previousDistanceFromPrimary = distance;
        }
        Check(monotonic,
            "offhand influence is monotonic: more authority always moves the "
            "presented direction strictly closer to accepted B");
    }

    // Out-of-range and non-finite authority: the endpoints own the extremes and
    // an unusable value fails safe to the exact primary endpoint.
    {
        AimPoseInputs over = base();
        over.twoHandOffhandInfluence = 1.5f;
        AimPoseInputs under = base();
        under.twoHandOffhandInfluence = -0.5f;
        AimPoseInputs nan = base();
        nan.twoHandOffhandInfluence =
            std::numeric_limits<float>::quiet_NaN();
        AimPoseInputs infinite = base();
        infinite.twoHandOffhandInfluence =
            std::numeric_limits<float>::infinity();
        Check(ExactAimResult(ComputeAimPose(over), full),
            "an influence above 1 clamps to full accepted-B authority");
        Check(ExactAimResult(ComputeAimPose(under), zero) &&
                ExactAimResult(ComputeAimPose(nan), zero) &&
                ExactAimResult(ComputeAimPose(infinite), zero),
            "an out-of-range or non-finite influence fails safe to the exact primary endpoint");
    }

    // Virtual Stock ignores the setting entirely: its own gates and stock ray
    // are untouched by the free two-hand authority input.
    {
        AimPoseInputs stock = base();
        stock.virtualStockEnabled = true;
        stock.virtualStockStrength = 1.0f;
        stock.virtualStockRearHeightM = 0.0f;
        stock.virtualStockRearReference = 0;
        stock.virtualStockProximityRelease = false;
        stock.headValid = true;
        stock.headPosition = {0.0f, 1.60f, 0.0f};
        stock.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
        AimPoseInputs stockZero = stock;
        stockZero.twoHandOffhandInfluence = 0.0f;
        AimPoseInputs stockHalf = stock;
        stockHalf.twoHandOffhandInfluence = kTwoHandOffhandInfluenceDefault;
        AimPoseTrace stockTrace{};
        const AimPoseResult stockResult = ComputeAimPose(stock, &stockTrace);
        Check(stockResult.valid && stockResult.twoHandActive &&
                stockTrace.path == AimSolverPath::FixedStock &&
                !ExactAimResult(stockResult, full),
            "the Virtual Stock baseline is a real, distinct stock solve");
        Check(ExactAimResult(ComputeAimPose(stockZero), stockResult) &&
                ExactAimResult(ComputeAimPose(stockHalf), stockResult),
            "Virtual Stock ignores the free two-hand offhand influence entirely");
    }

    // The Lab-disabled production path consumes the product setting: stored Lab
    // values are never read while the Lab is off.
    {
        AimPoseInputs labStoredZero = base();
        labStoredZero.twoHandLabEnabled = false;
        labStoredZero.twoHandLabOffhandInfluence = 0.0f;
        labStoredZero.twoHandOffhandInfluence = 1.0f;
        AimPoseInputs labStoredFull = labStoredZero;
        labStoredFull.twoHandLabOffhandInfluence = 1.0f;
        AimPoseTrace labStoredTrace{};
        const AimPoseResult labStoredResult =
            ComputeAimPose(labStoredFull, &labStoredTrace);
        Check(ExactAimResult(ComputeAimPose(labStoredZero), labStoredResult) &&
                !labStoredTrace.labValid,
            "a Lab-disabled solve never reads the stored Lab influence");
        AimPoseInputs productZero = labStoredFull;
        productZero.twoHandOffhandInfluence = 0.0f;
        Check(ExactAimResult(ComputeAimPose(productZero), zero),
            "the Lab-disabled production path consumes the product offhand influence");
    }

    // Smoothed geometry: at 0% the exact presented primary orientation is the
    // smoothed primary copy this solve actually consumed, never the raw one.
    {
        AimPoseInputs smoothed = base();
        smoothed.twoHandSmoothingGeometryValid = true;
        smoothed.twoHandSmoothedPrimaryOrientation =
            {0.1305262f, 0.0f, 0.0f, 0.9914449f};
        smoothed.twoHandSmoothedPrimaryAimPosition = {-0.31f, 1.28f, -0.19f};
        smoothed.twoHandSmoothedSupportAimPosition = {0.11f, 1.30f, -0.55f};
        smoothed.twoHandSmoothedPrimaryGripValid = true;
        smoothed.twoHandSmoothedPrimaryGripPosition = {0.10f, 1.30f, -0.18f};
        smoothed.twoHandSmoothedSupportGripValid = true;
        smoothed.twoHandSmoothedSupportGripPosition = {0.07f, 1.30f, -0.66f};
        smoothed.twoHandOffhandInfluence = 0.0f;
        const AimPoseResult smoothedZero = ComputeAimPose(smoothed);
        Check(smoothedZero.valid && smoothedZero.twoHandActive &&
                SameQuaternion(smoothedZero.pose.orientation,
                    smoothed.twoHandSmoothedPrimaryOrientation),
            "at 0% the smoothed two-hand solve keeps the exact smoothed primary orientation");
    }
}

void TestAimTraceSemantics()
{
    AimPoseInputs inputs = HybridInputs();
    inputs.right.position = {0.10f, 0.0f, -0.50f};
    inputs.supportPosition = {0.0f, 0.0f, -1.0f};
    inputs.headPosition = {0.0f, 0.0f, 0.0f};
    AimPoseTrace normal{};
    const AimPoseResult normalResult = ComputeAimPose(inputs, &normal);
    Check(normalResult.valid && normal.path == AimSolverPath::Hybrid &&
            normal.aValid && normal.bAttempted && normal.bAccepted &&
            normal.hipAimValid && normal.targetValid && normal.cAttempted &&
            normal.cValid && normal.virtualRearValid && normal.seatAttempted &&
            normal.seatValid && normal.seatClosestValid &&
            normal.wNaturalValid && normal.wAfterDiagnosticValid &&
            normal.wEffectiveValid &&
            normal.releaseGeometryValid && normal.finalDirectionValid &&
            normal.finalCalibrationValid,
        "Hybrid rich trace marks all valid geometry stages explicitly");
    Check(Near(normal.wNatural, 0.5f) &&
            Near(normal.wAfterDiagnostic, normal.wNatural) &&
            Near(normal.wEffective, normal.wNatural) &&
            !normal.overrideApplied &&
            !normal.horizontalReleaseAttempted &&
            !normal.horizontalReleaseValid,
        "Normal Hybrid trace keeps natural and effective seating authority equal");
    Check(Near(normal.seatSegmentLength, 1.0f) &&
            Near(normal.seatRawProjection, 0.5f) &&
            Near(normal.seatClampedProjection, 0.5f) &&
            Near(normal.seatErrorM, 0.1f) &&
            Near(normal.rearToStockTargetDistanceM,
                std::sqrt(0.26f)),
        "Hybrid rich trace reports independently checkable seat and release geometry");

    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceHip;
    AimPoseTrace forceHip{};
    ComputeAimPose(inputs, &forceHip);
    Check(Near(forceHip.wNatural, 0.5f) &&
            forceHip.wAfterDiagnostic == 0.0f &&
            forceHip.wEffective == 0.0f &&
            forceHip.wNaturalValid && forceHip.wAfterDiagnosticValid &&
            forceHip.wEffectiveValid &&
            forceHip.overrideApplied && forceHip.forceStockEligible &&
            !forceHip.stockContributed,
        "Force Hip preserves natural W while exposing overridden effective W");

    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    AimPoseTrace forceStock{};
    ComputeAimPose(inputs, &forceStock);
    Check(Near(forceStock.wNatural, 0.5f) &&
            forceStock.wAfterDiagnostic == 1.0f &&
            forceStock.wEffective == 1.0f &&
            forceStock.wNaturalValid && forceStock.wAfterDiagnosticValid &&
            forceStock.wEffectiveValid &&
            forceStock.overrideApplied && forceStock.forceStockEligible &&
            forceStock.stockContributed,
        "Force Stock preserves natural W while exposing full effective W");

    inputs.headValid = false;
    AimPoseTrace unavailableStock{};
    ComputeAimPose(inputs, &unavailableStock);
    Check(!unavailableStock.targetValid && !unavailableStock.cAttempted &&
            !unavailableStock.cValid && !unavailableStock.seatAttempted &&
            !unavailableStock.wNaturalValid &&
            unavailableStock.wNatural == 0.0f &&
            !unavailableStock.wAfterDiagnosticValid &&
            unavailableStock.wAfterDiagnostic == 0.0f &&
            !unavailableStock.wEffectiveValid &&
            unavailableStock.wEffective == 0.0f &&
            !unavailableStock.overrideApplied &&
            !unavailableStock.forceStockEligible,
        "Invalid Hybrid stock geometry remains explicit zero/default trace data");

    inputs.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceHip;
    AimPoseTrace unavailableForceHip{};
    ComputeAimPose(inputs, &unavailableForceHip);
    Check(!unavailableForceHip.wNaturalValid &&
            !unavailableForceHip.wAfterDiagnosticValid &&
            !unavailableForceHip.wEffectiveValid &&
            unavailableForceHip.wNatural == 0.0f &&
            unavailableForceHip.wAfterDiagnostic == 0.0f &&
            unavailableForceHip.wEffective == 0.0f &&
            !unavailableForceHip.overrideApplied,
        "Force Hip cannot claim an applied valid-zero weight when C is unavailable");

    inputs = HybridInputs();
    inputs.headValid = false;
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    AimPoseTrace exactA{};
    const AimPoseResult exactAResult = ComputeAimPose(inputs, &exactA);
    Check(exactAResult.valid && exactAResult.twoHandActive &&
            exactA.exactAEndpointSelected && !exactA.orientationRebuildAttempted &&
            SameQuaternion(exactAResult.pose.orientation, inputs.right.orientation),
        "Trace distinguishes the exact-A endpoint from an orientation rebuild");

    inputs = EquivalenceInputs();
    inputs.virtualStockRearReference = 1;
    inputs.virtualStockProximityRelease = true;
    inputs.virtualStockProximityFullM = 0.60f;
    inputs.virtualStockProximityReleaseM = 0.80f;
    AimPoseTrace fixed{};
    ComputeAimPose(inputs, &fixed);
    Check(fixed.path == AimSolverPath::FixedStock &&
            fixed.requestedTarget == AimStockTarget::Shoulder &&
            fixed.actualTarget == AimStockTarget::Shoulder &&
            fixed.fixedTargetValid && fixed.fixedRearDistanceValid &&
            fixed.fixedProximityEnabled && fixed.fixedProximityCalculated &&
            Near(fixed.fixedConfiguredStrength, inputs.virtualStockStrength) &&
            Near(fixed.fixedProximityInfluence, 1.0f) &&
            Near(fixed.fixedEffectiveStrength, inputs.virtualStockStrength) &&
            fixed.fixedDirectionValid &&
            fixed.orientationRebuildAttempted &&
            fixed.orientationRebuildSucceeded &&
            !fixed.exactAEndpointSelected,
        "Fixed Shoulder trace reports target, distance, proximity and direction stages");
}

void TestCounterfactualProfileIdentity()
{
    AimPoseInputs base = EquivalenceInputs();
    base.testProfileUsed = VirtualStockTestProfile::E_HybridMaxSeat;
    base.supportEndpointUsedGrip = true;
    base.supportGripPoseEnabled = true;
    base.twoHandToggle = false;
    const VirtualStockAimSettings originalSettings =
        VirtualStockAimSettingsFromAimPoseInputs(base);
    const VirtualStockTestProfile controls[] = {
        kVsOffControlProfile,
        kFixedHeadControlProfile,
        kFixedShoulderControlProfile,
    };
    for (const VirtualStockTestProfile profile : controls)
    {
        const AimPoseInputs selectedActual =
            AimPoseInputsForSelectedProfile(
                base, originalSettings, profile);
        const AimPoseResult actual = ComputeAimPose(selectedActual);

        const AimPoseInputs loggedCounterfactual =
            AimPoseInputsForProfile(base, profile);
        AimPoseTrace counterfactualTrace{};
        const AimPoseResult counterfactual =
            ComputeAimPose(loggedCounterfactual, &counterfactualTrace);
        Check(ExactAimResult(actual, counterfactual),
            "Selected A/B/C profile output exactly equals its same-frame counterfactual");
        Check(SameVirtualStockSettings(
                    VirtualStockAimSettingsFromAimPoseInputs(selectedActual),
                    VirtualStockAimSettingsFromAimPoseInputs(loggedCounterfactual),
                    0.0f) &&
                selectedActual.testProfileUsed == profile &&
                loggedCounterfactual.testProfileUsed == profile &&
                loggedCounterfactual.supportEndpointUsedGrip &&
                loggedCounterfactual.supportGripPoseEnabled &&
                !selectedActual.twoHandToggle &&
                !loggedCounterfactual.twoHandToggle,
            "Counterfactual input records explicit profile, acquisition mode and support-endpoint provenance");
    }

    Check(base.testProfileUsed == VirtualStockTestProfile::E_HybridMaxSeat &&
            base.supportEndpointUsedGrip && base.supportGripPoseEnabled &&
            SameVirtualStockSettings(
                VirtualStockAimSettingsFromAimPoseInputs(base), originalSettings,
                1.0e-6f),
        "Counterfactual solves leave the immutable canonical input packet unchanged");

    const AimPoseInputs a = AimPoseInputsForProfile(
        base, kVsOffControlProfile);
    const AimPoseInputs b = AimPoseInputsForProfile(
        base, kFixedHeadControlProfile);
    const AimPoseInputs c = AimPoseInputsForProfile(
        base, kFixedShoulderControlProfile);
    Check(!a.virtualStockEnabled && a.virtualStockRearReference == 0 &&
            b.virtualStockEnabled && b.virtualStockRearReference == 0 &&
            c.virtualStockEnabled && c.virtualStockRearReference == 1,
        "A/B/C controls resolve to deterministic VS OFF, fixed Head and fixed Shoulder settings");
}

void TestHybridInverseNeckIntegration()
{
    AimPoseInputs base = HybridInputs();
    base.right.position = {0.10f, 1.35f, 0.02f};
    base.supportPosition = {0.25f, 1.36f, -0.42f};
    base.headPosition = {0.0f, 1.60f, 0.0f};
    base.headOrientation = {0.0f, std::sin(0.30f), 0.0f, std::cos(0.30f)};
    base.inverseNeckNeutralValid = true;
    base.inverseNeckNeutralOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    base.inverseNeckNeutralCaptureSerial = 77;
    base.inverseNeckNeutralCaptureContactSpaceEpoch = 9;

    VirtualStockAimSettings acceptedY3 =
        MakeHybridHorizontalReleaseProfileSettings(0.330f, 0.475f);
    acceptedY3.virtualStockStrength = 0.80f;
    AimPoseInputs accepted = base;
    ApplyVirtualStockAimSettings(
        accepted, acceptedY3, VirtualStockTestProfile::Custom);
    AimPoseTrace acceptedTrace{};
    const AimPoseResult acceptedResult = ComputeAimPose(accepted, &acceptedTrace);

    AimPoseInputs nk0 = base;
    ApplyVirtualStockAimSettings(nk0,
        MakeHybridInverseNeckY3ProfileSettings(
            0.0f, 0.100f, 0.040f, 0.000f),
        static_cast<VirtualStockTestProfile>(
            kVirtualStockExperimentProfileFirstId));
    AimPoseTrace nk0Trace{};
    const AimPoseResult nk0Result = ComputeAimPose(nk0, &nk0Trace);
    Check(ExactAimResult(nk0Result, acceptedResult) &&
            nk0Trace.inverseNeckAttempted && nk0Trace.inverseNeckValid &&
            nk0Trace.rawHeadTargetValid &&
            nk0Trace.correctedHeadTargetValid &&
            nk0Trace.rawHeadTarget.x == nk0Trace.correctedHeadTarget.x &&
            nk0Trace.rawHeadTarget.y == nk0Trace.correctedHeadTarget.y &&
            nk0Trace.rawHeadTarget.z == nk0Trace.correctedHeadTarget.z,
        "NK0 is exact accepted Y3 output parity while auditing a zero correction");
    Check(nk0Trace.horizontalReleaseValid && acceptedTrace.horizontalReleaseValid &&
            nk0Trace.rearHorizontalReachM ==
                acceptedTrace.rearHorizontalReachM &&
            nk0Trace.horizontalReleaseInfluence ==
                acceptedTrace.horizontalReleaseInfluence,
        "inverse-neck leaves horizontal release on raw head-position XZ semantics");

    AimPoseInputs nk100 = base;
    ApplyVirtualStockAimSettings(nk100,
        MakeHybridInverseNeckY3ProfileSettings(
            1.0f, 0.100f, 0.040f, 0.000f),
        static_cast<VirtualStockTestProfile>(
            kVirtualStockExperimentProfileFirstId));
    AimPoseTrace nk100Trace{};
    const AimPoseResult nk100Result = ComputeAimPose(nk100, &nk100Trace);
    Check(nk100Result.valid && nk100Trace.inverseNeckValid &&
            nk100Trace.inverseNeckNeutralValid &&
            nk100Trace.inverseNeckNeutralCaptureSerial == 77 &&
            nk100Trace.inverseNeckNeutralCaptureContactSpaceEpoch == 9 &&
            !SamePosition(
                {nk100Trace.rawHeadTarget.x, nk100Trace.rawHeadTarget.y,
                 nk100Trace.rawHeadTarget.z},
                {nk100Trace.correctedHeadTarget.x,
                 nk100Trace.correctedHeadTarget.y,
                 nk100Trace.correctedHeadTarget.z}),
        "Hybrid C consumes the corrected Head target only with enabled valid neutral state");
    Check(nk100Trace.rearHorizontalReachM ==
            acceptedTrace.rearHorizontalReachM,
        "NK100 correction does not feed horizontal release geometry");

    AimPoseInputs invalidNeutral = nk100;
    invalidNeutral.inverseNeckNeutralValid = false;
    AimPoseTrace invalidTrace{};
    const AimPoseResult invalidResult = ComputeAimPose(
        invalidNeutral, &invalidTrace);
    Check(ExactAimResult(invalidResult, acceptedResult) &&
            invalidTrace.inverseNeckAttempted && !invalidTrace.inverseNeckValid &&
            !invalidTrace.inverseNeckNeutralValid,
        "missing neutral state fails cleanly to raw-HMD accepted Y3 behavior");

    AimPoseInputs disabled = accepted;
    disabled.inverseNeckNeutralValid = true;
    disabled.inverseNeckNeutralOrientation =
        base.inverseNeckNeutralOrientation;
    disabled.inverseNeckNeutralCaptureSerial = 77;
    Check(ExactAimResult(ComputeAimPose(disabled), acceptedResult),
        "current Head Y3 with inverse-neck disabled remains behavior-identical");

    AimPoseInputs shoulder = nk100;
    shoulder.virtualStockHybridAdsReference = 1;
    AimPoseInputs shoulderDisabled = shoulder;
    shoulderDisabled.hybridInverseNeckEnabled = false;
    AimPoseTrace shoulderTrace{};
    const AimPoseResult shoulderResult =
        ComputeAimPose(shoulder, &shoulderTrace);
    AimPoseTrace shoulderDisabledTrace{};
    const AimPoseResult shoulderDisabledResult =
        ComputeAimPose(shoulderDisabled, &shoulderDisabledTrace);
    Check(!ExactAimResult(shoulderResult, shoulderDisabledResult) &&
            shoulderTrace.inverseNeckAttempted &&
            shoulderTrace.inverseNeckValid,
        "Plus Shoulder consumes the corrected positional base when sway is ON");
    Check(shoulderTrace.horizontalReleaseValid &&
            shoulderDisabledTrace.horizontalReleaseValid &&
            shoulderTrace.rearHorizontalReachM ==
                shoulderDisabledTrace.rearHorizontalReachM &&
            shoulderTrace.horizontalReleaseInfluence ==
                shoulderDisabledTrace.horizontalReleaseInfluence,
        "Plus Shoulder correction preserves raw-HMD horizontal release timing");
    AimPoseInputs shoulderNoQ0 = shoulder;
    shoulderNoQ0.inverseNeckNeutralValid = false;
    Check(ExactAimResult(
            ComputeAimPose(shoulderNoQ0), shoulderDisabledResult),
        "Plus Shoulder without valid Q0 falls back to raw-HMD behaviour");

    AimPoseInputs oneHand = nk100;
    oneHand.twoHandEnabled = false;
    AimPoseInputs oneHandDisabled = oneHand;
    oneHandDisabled.hybridInverseNeckEnabled = false;
    AimPoseTrace oneHandTrace{};
    const AimPoseResult oneHandResult = ComputeAimPose(oneHand, &oneHandTrace);
    Check(ExactAimResult(
            oneHandResult, ComputeAimPose(oneHandDisabled)) &&
            !oneHandTrace.inverseNeckAttempted &&
            oneHandTrace.inverseNeckNeutralValid &&
            oneHandTrace.inverseNeckNeutralCaptureSerial == 77,
        "one-hand output remains unchanged while telemetry retains valid runtime Q0 provenance");
}

void TestPlantedStandardSwayMatrix()
{
    AimPoseInputs base = EquivalenceInputs();
    base.right.position = {0.10f, 1.35f, 0.02f};
    base.supportPosition = {0.25f, 1.36f, -0.42f};
    base.headPosition = {0.0f, 1.60f, 0.0f};
    base.headOrientation = {0.0f, std::sin(0.30f), 0.0f, std::cos(0.30f)};
    base.headValid = true;
    base.virtualStockEnabled = true;
    base.virtualStockStrength = 0.95f;
    base.virtualStockRearHeightM = -0.220f;
    base.virtualStockShoulderBackM = 0.005f;
    base.virtualStockShoulderSideM = 0.015f;
    base.virtualStockProximityRelease = false;
    base.virtualStockProximityFullM = 0.270f;
    base.virtualStockProximityReleaseM = 0.425f;
    base.hybridDiagnosticOverride = HybridDiagnosticOverride::Normal;
    base.inverseNeckNeutralValid = true;
    base.inverseNeckNeutralOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    base.inverseNeckNeutralCaptureSerial = 77;
    base.inverseNeckNeutralCaptureContactSpaceEpoch = 9;

    // Standard Centre OFF reproduces raw fixed behaviour.
    AimPoseInputs centreOff = base;
    centreOff.virtualStockRearReference = 0;
    centreOff.hybridInverseNeckEnabled = false;
    const AimPoseResult centreOffResult = ComputeAimPose(centreOff);
    // Standard Centre ON applies correction to the rear positional base.
    AimPoseInputs centreOn = centreOff;
    centreOn.hybridInverseNeckEnabled = true;
    centreOn.hybridInverseNeckStrength = 1.00f;
    centreOn.hybridInverseNeckForwardM = 0.100f;
    centreOn.hybridInverseNeckUpM = 0.040f;
    centreOn.hybridInverseNeckLateralM = 0.000f;
    AimPoseTrace centreOnTrace{};
    const AimPoseResult centreOnResult =
        ComputeAimPose(centreOn, &centreOnTrace);
    Check(centreOffResult.valid && centreOnResult.valid &&
            !ExactAimResult(centreOnResult, centreOffResult) &&
            centreOnTrace.inverseNeckAttempted &&
            centreOnTrace.inverseNeckValid,
        "Standard Centre sway ON corrects the rear base while staying valid");
    // Dormant thresholds do not alter planted Standard when disabled.
    AimPoseInputs centreVaried = centreOn;
    centreVaried.virtualStockProximityFullM = 0.31f;
    centreVaried.virtualStockProximityReleaseM = 0.57f;
    Check(ExactAimResult(ComputeAimPose(centreVaried), centreOnResult),
        "planted Standard Centre ignores dormant proximity thresholds");
    // Standard Shoulder OFF/ON with HMD yaw basis preserved.
    AimPoseInputs shoulderOff = centreOff;
    shoulderOff.virtualStockRearReference = 1;
    const AimPoseResult shoulderOffResult = ComputeAimPose(shoulderOff);
    AimPoseInputs shoulderOn = centreOn;
    shoulderOn.virtualStockRearReference = 1;
    AimPoseTrace shoulderOnTrace{};
    const AimPoseResult shoulderOnResult =
        ComputeAimPose(shoulderOn, &shoulderOnTrace);
    Check(shoulderOffResult.valid && shoulderOnResult.valid &&
            !ExactAimResult(shoulderOnResult, shoulderOffResult) &&
            shoulderOnTrace.inverseNeckAttempted &&
            shoulderOnTrace.inverseNeckValid,
        "Standard Shoulder sway ON corrects the positional base while staying valid");
    AimPoseInputs shoulderVaried = shoulderOn;
    shoulderVaried.virtualStockProximityFullM = 0.31f;
    shoulderVaried.virtualStockProximityReleaseM = 0.57f;
    Check(ExactAimResult(ComputeAimPose(shoulderVaried), shoulderOnResult),
        "planted Standard Shoulder ignores dormant proximity thresholds");
    // Invalid Q0 falls back to raw without dropout.
    AimPoseInputs centreNoQ0 = centreOn;
    centreNoQ0.inverseNeckNeutralValid = false;
    Check(ExactAimResult(ComputeAimPose(centreNoQ0), centreOffResult),
        "Standard Centre without valid Q0 falls back to raw-HMD behaviour");
    AimPoseInputs shoulderNoQ0 = shoulderOn;
    shoulderNoQ0.inverseNeckNeutralValid = false;
    Check(ExactAimResult(ComputeAimPose(shoulderNoQ0), shoulderOffResult),
        "Standard Shoulder without valid Q0 falls back to raw-HMD behaviour");
    // Planted Standard matches the Force-Stock oracle at .95 with sway OFF.
    CheckFixedStockEquivalence(0, 0, false,
        "planted Standard Centre matches the Force-Stock oracle");
    CheckFixedStockEquivalence(1, 1, false,
        "planted Standard Shoulder matches the Force-Stock oracle");
    CheckFixedStockEquivalence(1, 1, true,
        "planted Standard Shoulder preserves left-handed mirroring");
    // Exact zero preserves the legacy/no-stock path.
    AimPoseInputs zeroCentre = centreOff;
    zeroCentre.virtualStockStrength = 0.0f;
    AimPoseTrace zeroTrace{};
    const AimPoseResult zeroResult = ComputeAimPose(zeroCentre, &zeroTrace);
    Check(zeroResult.valid &&
            zeroTrace.path == AimSolverPath::LegacyTwoHand &&
            !zeroTrace.fixedDirectionValid,
        "Standard strength zero preserves the legacy no-stock path");
}

void TestDormantChestUnaffectedBySway()
{
    AimPoseInputs base = EquivalenceInputs();
    base.right.position = {0.10f, 1.35f, 0.02f};
    base.supportPosition = {0.25f, 1.36f, -0.42f};
    base.headPosition = {0.0f, 1.60f, 0.0f};
    base.headOrientation = {0.0f, std::sin(0.30f), 0.0f, std::cos(0.30f)};
    base.headValid = true;
    base.virtualStockEnabled = true;
    base.virtualStockStrength = 0.95f;
    base.virtualStockRearHeightM = -0.220f;
    base.virtualStockRearReference = 2;
    base.virtualStockChestHeightM = -0.320f;
    base.virtualStockChestBackM = 0.000f;
    base.virtualStockChestSideM = 0.015f;
    base.virtualStockProximityRelease = false;
    base.virtualStockProximityFullM = 0.270f;
    base.virtualStockProximityReleaseM = 0.425f;
    base.hybridDiagnosticOverride = HybridDiagnosticOverride::Normal;
    base.inverseNeckNeutralValid = true;
    base.inverseNeckNeutralOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    base.inverseNeckNeutralCaptureSerial = 77;
    base.inverseNeckNeutralCaptureContactSpaceEpoch = 9;

    // Dormant Chest with a valid Chest target: sway must not alter output.
    AimPoseInputs chestOff = base;
    chestOff.hybridInverseNeckEnabled = false;
    AimPoseTrace chestOffTrace{};
    const AimPoseResult chestOffResult =
        ComputeAimPose(chestOff, &chestOffTrace);
    AimPoseInputs chestOn = base;
    chestOn.hybridInverseNeckEnabled = true;
    chestOn.hybridInverseNeckStrength = 1.00f;
    chestOn.hybridInverseNeckForwardM = 0.100f;
    chestOn.hybridInverseNeckUpM = 0.040f;
    chestOn.hybridInverseNeckLateralM = 0.000f;
    AimPoseTrace chestOnTrace{};
    const AimPoseResult chestOnResult =
        ComputeAimPose(chestOn, &chestOnTrace);
    Check(chestOffResult.valid && chestOnResult.valid &&
            ExactAimResult(chestOnResult, chestOffResult) &&
            !chestOnTrace.inverseNeckAttempted,
        "dormant Chest output is identical with sway OFF vs ON");
    // Fallback edge: invalid Chest construction falls back to Head/Centre.
    // The fallback must remain raw-HMD with sway OFF vs ON.
    AimPoseInputs fallbackOff = chestOff;
    fallbackOff.virtualStockChestBackM =
        std::numeric_limits<float>::quiet_NaN();
    AimPoseTrace fallbackOffTrace{};
    const AimPoseResult fallbackOffResult =
        ComputeAimPose(fallbackOff, &fallbackOffTrace);
    AimPoseInputs fallbackOn = chestOn;
    fallbackOn.virtualStockChestBackM =
        std::numeric_limits<float>::quiet_NaN();
    AimPoseTrace fallbackOnTrace{};
    const AimPoseResult fallbackOnResult =
        ComputeAimPose(fallbackOn, &fallbackOnTrace);
    Check(fallbackOffResult.valid && fallbackOnResult.valid &&
            fallbackOffTrace.shoulderToHeadFallback ==
                fallbackOnTrace.shoulderToHeadFallback &&
            fallbackOnTrace.actualTarget == AimStockTarget::Head &&
            fallbackOffTrace.actualTarget == AimStockTarget::Head &&
            ExactAimResult(fallbackOnResult, fallbackOffResult) &&
            !fallbackOnTrace.inverseNeckAttempted,
        "invalid-Chest fallback remains raw-HMD with sway OFF vs ON");
}

bool SameVirtualStockSettings(
    const VirtualStockAimSettings& actual,
    const VirtualStockAimSettings& expected,
    float epsilon = 1.0e-6f)
{
    return actual.virtualStockEnabled == expected.virtualStockEnabled &&
        Near(actual.virtualStockStrength, expected.virtualStockStrength, epsilon) &&
        Near(actual.virtualStockRearHeightM, expected.virtualStockRearHeightM, epsilon) &&
        actual.virtualStockRearReference == expected.virtualStockRearReference &&
        Near(actual.virtualStockShoulderBackM, expected.virtualStockShoulderBackM, epsilon) &&
        Near(actual.virtualStockShoulderSideM, expected.virtualStockShoulderSideM, epsilon) &&
        Near(actual.virtualStockChestHeightM, expected.virtualStockChestHeightM, epsilon) &&
        Near(actual.virtualStockChestBackM, expected.virtualStockChestBackM, epsilon) &&
        Near(actual.virtualStockChestSideM, expected.virtualStockChestSideM, epsilon) &&
        Near(actual.virtualStockAdaptiveTopHeightM,
            expected.virtualStockAdaptiveTopHeightM, epsilon) &&
        Near(actual.virtualStockAdaptiveBottomHeightM,
            expected.virtualStockAdaptiveBottomHeightM, epsilon) &&
        Near(actual.virtualStockAdaptiveTopHalfWidthM,
            expected.virtualStockAdaptiveTopHalfWidthM, epsilon) &&
        Near(actual.virtualStockAdaptiveBottomHalfWidthM,
            expected.virtualStockAdaptiveBottomHalfWidthM, epsilon) &&
        Near(actual.virtualStockHybridOffhandInfluence,
            expected.virtualStockHybridOffhandInfluence, epsilon) &&
        actual.virtualStockHybridAdsReference ==
            expected.virtualStockHybridAdsReference &&
        Near(actual.virtualStockHybridSeatFullM,
            expected.virtualStockHybridSeatFullM, epsilon) &&
        Near(actual.virtualStockHybridSeatReleaseM,
            expected.virtualStockHybridSeatReleaseM, epsilon) &&
        actual.hybridHorizontalRearReleaseEnabled ==
            expected.hybridHorizontalRearReleaseEnabled &&
        Near(actual.hybridHorizontalRearReleaseFullM,
            expected.hybridHorizontalRearReleaseFullM, epsilon) &&
        Near(actual.hybridHorizontalRearReleaseReleaseM,
            expected.hybridHorizontalRearReleaseReleaseM, epsilon) &&
        actual.hybridInverseNeckEnabled ==
            expected.hybridInverseNeckEnabled &&
        Near(actual.hybridInverseNeckStrength,
            expected.hybridInverseNeckStrength, epsilon) &&
        Near(actual.hybridInverseNeckForwardM,
            expected.hybridInverseNeckForwardM, epsilon) &&
        Near(actual.hybridInverseNeckUpM,
            expected.hybridInverseNeckUpM, epsilon) &&
        Near(actual.hybridInverseNeckLateralM,
            expected.hybridInverseNeckLateralM, epsilon) &&
        actual.hybridDiagnosticOverride == expected.hybridDiagnosticOverride &&
        actual.virtualStockProximityRelease == expected.virtualStockProximityRelease &&
        Near(actual.virtualStockProximityFullM,
            expected.virtualStockProximityFullM, epsilon) &&
        Near(actual.virtualStockProximityReleaseM,
            expected.virtualStockProximityReleaseM, epsilon);
}

void CheckProfileSettings(VirtualStockTestProfile profile, bool enabled,
    int rearReference, HybridDiagnosticOverride diagnostic, int adsReference,
    float offhandInfluence, float seatFull, float seatRelease,
    bool proximityRelease, const char* message)
{
    const VirtualStockAimSettings actual = ResolveVirtualStockTestProfile(
        profile, CanonicalVirtualStockAimSettings());
    VirtualStockAimSettings expected = CanonicalVirtualStockAimSettings();
    expected.virtualStockEnabled = enabled;
    expected.virtualStockRearReference = rearReference;
    expected.hybridDiagnosticOverride = diagnostic;
    expected.virtualStockHybridAdsReference = adsReference;
    expected.virtualStockHybridOffhandInfluence = offhandInfluence;
    expected.virtualStockHybridSeatFullM = seatFull;
    expected.virtualStockHybridSeatReleaseM = seatRelease;
    expected.virtualStockProximityRelease = proximityRelease;
    Check(SameVirtualStockSettings(actual, expected),
        message);
}

void TestVirtualStockTestProfiles()
{
    struct HistoricalProfile
    {
        VirtualStockTestProfile profile;
        uint8_t id;
        const char* stableName;
        const char* uiLabel;
        VirtualStockTestProfileRole role;
    };
    constexpr HistoricalProfile historical[] = {
        {VirtualStockTestProfile::Custom, 0, "Custom", "Custom",
            VirtualStockTestProfileRole::None},
        {VirtualStockTestProfile::A_VsOffControl, 1, "A_VsOffControl",
            "A - VS OFF Control", VirtualStockTestProfileRole::VsOffControl},
        {VirtualStockTestProfile::B_FixedHeadControl, 2,
            "B_FixedHeadControl", "B - Fixed Head Control",
            VirtualStockTestProfileRole::FixedHeadControl},
        {VirtualStockTestProfile::C_FixedShoulderControl, 3,
            "C_FixedShoulderControl", "C - Fixed Shoulder Control",
            VirtualStockTestProfileRole::FixedShoulderControl},
        {VirtualStockTestProfile::D_HybridBaseline, 4, "D_HybridBaseline",
            "D - Hybrid Baseline", VirtualStockTestProfileRole::Tuning},
        {VirtualStockTestProfile::E_HybridMaxSeat, 5, "E_HybridMaxSeat",
            "E - Hybrid Max Seat", VirtualStockTestProfileRole::Tuning},
        {VirtualStockTestProfile::F_HybridForceStockHead, 6,
            "F_HybridForceStockHead", "F - Hybrid Force Stock Head",
            VirtualStockTestProfileRole::Diagnostic},
        {VirtualStockTestProfile::G_HybridForceStockShoulder, 7,
            "G_HybridForceStockShoulder", "G - Hybrid Force Stock Shoulder",
            VirtualStockTestProfileRole::Diagnostic},
        {VirtualStockTestProfile::H_HybridForceHip50, 8,
            "H_HybridForceHip50", "H - Hybrid Force Hip 50%",
            VirtualStockTestProfileRole::Diagnostic},
        {VirtualStockTestProfile::I_HybridForceHip100, 9,
            "I_HybridForceHip100", "I - Hybrid Force Hip 100%",
            VirtualStockTestProfileRole::Diagnostic},
    };
    constexpr std::array<VirtualStockExperimentProfileSpec, 0>
        emptyExperiments{};
    constexpr auto emptyRegistry =
        MergeVirtualStockTestProfiles(emptyExperiments);
    bool emptyRegistryMatchesCore =
        emptyRegistry.size() == kVirtualStockCoreTestProfileCount;
    for (size_t index = 0; index < emptyRegistry.size(); ++index)
    {
        emptyRegistryMatchesCore = emptyRegistryMatchesCore &&
            emptyRegistry[index].id ==
                kVirtualStockCoreTestProfileDefinitions[index].id &&
            VirtualStockTestProfileStringEqual(
                emptyRegistry[index].stableName,
                kVirtualStockCoreTestProfileDefinitions[index].stableName) &&
            VirtualStockTestProfileStringEqual(
                emptyRegistry[index].uiLabel,
                kVirtualStockCoreTestProfileDefinitions[index].uiLabel) &&
            emptyRegistry[index].role ==
                kVirtualStockCoreTestProfileDefinitions[index].role &&
            emptyRegistry[index].useCustomSettings ==
                kVirtualStockCoreTestProfileDefinitions[index].useCustomSettings &&
            SameVirtualStockSettings(
                emptyRegistry[index].settings,
                kVirtualStockCoreTestProfileDefinitions[index].settings);
    }
    Check(kVirtualStockCoreTestProfileCount == std::size(historical) + 5 &&
            kVirtualStockTestProfileCount ==
                kVirtualStockCoreTestProfileCount +
                    kVirtualStockExperimentProfileCount &&
            emptyRegistryMatchesCore,
        "Empty experiment merge is exactly the immutable Custom and A-N core registry");
    Check(kVirtualStockExperimentProfileCount == 2,
        "Final-composition experiment pack contains exactly AB0 and AB1");
    const auto ab0Profile = static_cast<VirtualStockTestProfile>(
        kVirtualStockExperimentProfileFirstId);
    const auto ab1Profile = static_cast<VirtualStockTestProfile>(
        kVirtualStockExperimentProfileFirstId + 1);
    const auto* ab0Definition =
        FindVirtualStockTestProfileDefinition(ab0Profile);
    const auto* ab1Definition =
        FindVirtualStockTestProfileDefinition(ab1Profile);
    const VirtualStockAimSettings ab0 = ResolveVirtualStockTestProfile(
        ab0Profile, CanonicalVirtualStockAimSettings());
    const VirtualStockAimSettings ab1 = ResolveVirtualStockTestProfile(
        ab1Profile, CanonicalVirtualStockAimSettings());
    Check(ab0Definition != nullptr && ab1Definition != nullptr &&
            std::strcmp(ab0Definition->stableName,
                "AB0_NK100_ForceStock_Control") == 0 &&
            std::strcmp(ab1Definition->stableName,
                "AB1_NK100_Production_Seat464_474") == 0 &&
            SameVirtualStockSettings(ab0,
                MakeHybridInverseNeckY3ProfileSettings(
                    1.00f, 0.100f, 0.040f, 0.000f)) &&
            SameVirtualStockSettings(ab1,
                MakeHybridInverseNeckY3ProductionCandidateSettings(
                    1.00f, 0.100f, 0.040f, 0.000f)),
        "AB0 remains exact NK100 ForceStock and AB1 resolves through the narrow production builder");
    VirtualStockAimSettings ab1AsControl = ab1;
    ab1AsControl.hybridDiagnosticOverride =
        HybridDiagnosticOverride::ForceStock;
    ab1AsControl.virtualStockHybridSeatFullM =
        ab0.virtualStockHybridSeatFullM;
    ab1AsControl.virtualStockHybridSeatReleaseM =
        ab0.virtualStockHybridSeatReleaseM;
    Check(ab0.hybridInverseNeckEnabled && ab1.hybridInverseNeckEnabled &&
            ab0.hybridDiagnosticOverride ==
                HybridDiagnosticOverride::ForceStock &&
            ab1.hybridDiagnosticOverride == HybridDiagnosticOverride::Normal &&
            Near(ab1.virtualStockHybridSeatFullM, 0.464f) &&
            Near(ab1.virtualStockHybridSeatReleaseM, 0.474f) &&
            SameVirtualStockSettings(ab0, ab1AsControl),
        "AB1 differs from AB0 only by diagnostic override and the 0.464/0.474 seat envelope");
    for (size_t index = 0; index < std::size(historical); ++index)
    {
        const HistoricalProfile& expected = historical[index];
        const auto* definition =
            FindVirtualStockTestProfileDefinition(expected.profile);
        Check(definition != nullptr &&
                static_cast<uint8_t>(definition->id) == expected.id &&
                std::strcmp(definition->stableName, expected.stableName) == 0 &&
                std::strcmp(definition->uiLabel, expected.uiLabel) == 0 &&
                definition->role == expected.role &&
                VirtualStockTestProfileDefinitionAt(index).id ==
                    expected.profile &&
                VirtualStockTestProfileIndex(expected.profile) == index,
            "Historical A-I profile ID, name, label, role and order are immutable");
    }
    Check(kVsOffControlProfile == VirtualStockTestProfile::A_VsOffControl &&
            kFixedHeadControlProfile ==
                VirtualStockTestProfile::B_FixedHeadControl &&
            kFixedShoulderControlProfile ==
                VirtualStockTestProfile::C_FixedShoulderControl &&
            VirtualStockTestProfileRoleCount(
                VirtualStockTestProfileRole::VsOffControl) == 1 &&
            VirtualStockTestProfileRoleCount(
                VirtualStockTestProfileRole::FixedHeadControl) == 1 &&
            VirtualStockTestProfileRoleCount(
                VirtualStockTestProfileRole::FixedShoulderControl) == 1,
        "Control roles resolve once to the immutable A/B/C profiles");
    Check(std::strcmp(VirtualStockTestProfileRoleName(
                VirtualStockTestProfileRole::VsOffControl),
                "vs_off_control") == 0 &&
            std::strcmp(VirtualStockTestProfileRoleName(
                VirtualStockTestProfileRole::FixedHeadControl),
                "fixed_head_control") == 0 &&
            std::strcmp(VirtualStockTestProfileRoleName(
                VirtualStockTestProfileRole::FixedShoulderControl),
                "fixed_shoulder_control") == 0,
        "Control roles expose stable telemetry names");
    Check(std::strcmp(
                VirtualStockTestProfileName(
                    VirtualStockTestProfile::E_HybridMaxSeat),
                "E_HybridMaxSeat") == 0 &&
            std::strcmp(
                VirtualStockTestProfileLabel(
                    VirtualStockTestProfile::E_HybridMaxSeat),
                "E - Hybrid Max Seat") == 0,
        "Profile E has a stable max-seat name rather than a historical reproduction name");
    CheckProfileSettings(VirtualStockTestProfile::A_VsOffControl,
        false, 0, HybridDiagnosticOverride::Normal, 0, 0.50f,
        0.050f, 0.150f, false,
        "Profile A resolves to Virtual Stock OFF");
    CheckProfileSettings(VirtualStockTestProfile::B_FixedHeadControl,
        true, 0, HybridDiagnosticOverride::Normal, 0, 0.50f,
        0.050f, 0.150f, true,
        "Profile B resolves to fixed Head with legacy proximity enabled");
    CheckProfileSettings(VirtualStockTestProfile::C_FixedShoulderControl,
        true, 1, HybridDiagnosticOverride::Normal, 0, 0.50f,
        0.050f, 0.150f, true,
        "Profile C resolves to fixed Shoulder with legacy proximity enabled");
    CheckProfileSettings(VirtualStockTestProfile::D_HybridBaseline,
        true, 3, HybridDiagnosticOverride::Normal, 0, 0.50f,
        0.050f, 0.150f, false,
        "Profile D resolves to the canonical Hybrid baseline");
    CheckProfileSettings(VirtualStockTestProfile::E_HybridMaxSeat,
        true, 3, HybridDiagnosticOverride::Normal, 0, 0.50f,
        0.600f, 0.800f, false,
        "Profile E resolves to the maximum-valid 0.600/0.800 metre max-seat condition");
    CheckProfileSettings(VirtualStockTestProfile::F_HybridForceStockHead,
        true, 3, HybridDiagnosticOverride::ForceStock, 0, 0.50f,
        0.050f, 0.150f, false,
        "Profile F resolves to Hybrid Force Stock Head");
    CheckProfileSettings(VirtualStockTestProfile::G_HybridForceStockShoulder,
        true, 3, HybridDiagnosticOverride::ForceStock, 1, 0.50f,
        0.050f, 0.150f, false,
        "Profile G resolves to Hybrid Force Stock Shoulder");
    CheckProfileSettings(VirtualStockTestProfile::H_HybridForceHip50,
        true, 3, HybridDiagnosticOverride::ForceHip, 0, 0.50f,
        0.050f, 0.150f, false,
        "Profile H resolves to Hybrid Force Hip at 50 percent influence");
    CheckProfileSettings(VirtualStockTestProfile::I_HybridForceHip100,
        true, 3, HybridDiagnosticOverride::ForceHip, 0, 1.00f,
        0.050f, 0.150f, false,
        "Profile I resolves to Hybrid Force Hip at 100 percent influence");

    struct TuningProfile
    {
        VirtualStockTestProfile profile;
        uint8_t id;
        const char* stableName;
        const char* uiLabel;
        float seatFullM;
    };
    constexpr TuningProfile tuning[] = {
        {VirtualStockTestProfile::J_HybridSeat_464_474, 10,
            "J_HybridSeat_464_474", "J - Hybrid Seat 0.464 / 0.474", 0.464f},
        {VirtualStockTestProfile::K_HybridSeat_440_474, 11,
            "K_HybridSeat_440_474", "K - Hybrid Seat 0.440 / 0.474", 0.440f},
        {VirtualStockTestProfile::L_HybridSeat_420_474, 12,
            "L_HybridSeat_420_474", "L - Hybrid Seat 0.420 / 0.474", 0.420f},
        {VirtualStockTestProfile::M_HybridSeat_400_474, 13,
            "M_HybridSeat_400_474", "M - Hybrid Seat 0.400 / 0.474", 0.400f},
        {VirtualStockTestProfile::N_HybridSeat_380_474, 14,
            "N_HybridSeat_380_474", "N - Hybrid Seat 0.380 / 0.474", 0.380f},
    };
    const VirtualStockAimSettings tuningReference =
        ResolveVirtualStockTestProfile(
            VirtualStockTestProfile::J_HybridSeat_464_474,
            CanonicalVirtualStockAimSettings());
    for (size_t index = 0; index < std::size(tuning); ++index)
    {
        const TuningProfile& expectedProfile = tuning[index];
        const auto* definition = FindVirtualStockTestProfileDefinition(
            expectedProfile.profile);
        Check(definition != nullptr &&
                static_cast<uint8_t>(definition->id) == expectedProfile.id &&
                std::strcmp(definition->stableName,
                    expectedProfile.stableName) == 0 &&
                std::strcmp(definition->uiLabel,
                    expectedProfile.uiLabel) == 0 &&
                definition->role == VirtualStockTestProfileRole::Tuning &&
                !definition->useCustomSettings &&
                VirtualStockTestProfileIndex(expectedProfile.profile) ==
                    std::size(historical) + index,
            "J-N have append-only IDs, stable metadata and tuning roles");
        CheckProfileSettings(expectedProfile.profile,
            true, 3, HybridDiagnosticOverride::Normal, 0, 0.50f,
            expectedProfile.seatFullM, 0.474f, false,
            "J-N inherit canonical Hybrid settings and declare only seat thresholds");

        VirtualStockAimSettings expectedSettings = tuningReference;
        expectedSettings.virtualStockHybridSeatFullM =
            expectedProfile.seatFullM;
        const VirtualStockAimSettings actualSettings =
            ResolveVirtualStockTestProfile(
                expectedProfile.profile, CanonicalVirtualStockAimSettings());
        Check(SameVirtualStockSettings(actualSettings, expectedSettings) &&
                actualSettings.virtualStockHybridSeatFullM >=
                    kVirtualStockHybridSeatFullMinimumM &&
                actualSettings.virtualStockHybridSeatFullM <=
                    kVirtualStockHybridSeatFullMaximumM &&
                actualSettings.virtualStockHybridSeatReleaseM >=
                    kVirtualStockHybridSeatReleaseMinimumM &&
                actualSettings.virtualStockHybridSeatReleaseM <=
                    kVirtualStockHybridSeatReleaseMaximumM &&
                actualSettings.virtualStockHybridSeatReleaseM -
                        actualSettings.virtualStockHybridSeatFullM >=
                    kVirtualStockHybridSeatMinimumSeparationM,
            "J-N vary only seat-full while retaining one valid 0.474 release threshold");
    }

    constexpr std::array syntheticExperiments = {
        VirtualStockExperimentProfileSpec{
            "Temporary_Seat_360_474",
            "Temporary - Seat 0.360 / 0.474",
            MakeHybridTuningProfileSettings(0.360f, 0.474f)},
        VirtualStockExperimentProfileSpec{
            "Temporary_Horizontal_300_450",
            "Temporary - Horizontal 0.300 / 0.450",
            MakeHybridHorizontalReleaseProfileSettings(0.300f, 0.450f)},
    };
    constexpr auto syntheticRegistry =
        MergeVirtualStockTestProfiles(syntheticExperiments);
    constexpr auto firstTemporary = static_cast<VirtualStockTestProfile>(
        kVirtualStockExperimentProfileFirstId);
    constexpr auto secondTemporary = static_cast<VirtualStockTestProfile>(
        kVirtualStockExperimentProfileFirstId + 1);
    const auto* firstTemporaryDefinition =
        FindVirtualStockTestProfileDefinitionInRegistry(
            syntheticRegistry, firstTemporary);
    const auto* secondTemporaryDefinition =
        FindVirtualStockTestProfileDefinitionInRegistry(
            syntheticRegistry, secondTemporary);
    Check(VirtualStockTestProfileRegistryIsValid(
              syntheticRegistry, kVirtualStockCoreTestProfileCount) &&
            syntheticRegistry.size() == kVirtualStockCoreTestProfileCount + 2 &&
            firstTemporaryDefinition != nullptr &&
            secondTemporaryDefinition != nullptr &&
            std::strcmp(firstTemporaryDefinition->stableName,
                "Temporary_Seat_360_474") == 0 &&
            std::strcmp(secondTemporaryDefinition->stableName,
                "Temporary_Horizontal_300_450") == 0 &&
            firstTemporaryDefinition->role ==
                VirtualStockTestProfileRole::Tuning &&
            secondTemporaryDefinition->role ==
                VirtualStockTestProfileRole::Tuning &&
            !firstTemporaryDefinition->useCustomSettings &&
            !secondTemporaryDefinition->useCustomSettings,
        "Synthetic experiment rows receive contiguous IDs 128-129 and forced tuning metadata");
    Check(SameVirtualStockSettings(
              firstTemporaryDefinition->settings,
              MakeHybridTuningProfileSettings(0.360f, 0.474f)) &&
            SameVirtualStockSettings(
                secondTemporaryDefinition->settings,
                MakeHybridHorizontalReleaseProfileSettings(0.300f, 0.450f)),
        "Synthetic experiment merge preserves production-built settings exactly");

    constexpr std::array duplicateNames = {
        VirtualStockExperimentProfileSpec{
            "Duplicate", "First", MakeHybridProfileSettings()},
        VirtualStockExperimentProfileSpec{
            "Duplicate", "Second", MakeHybridProfileSettings()},
    };
    constexpr std::array emptyName = {
        VirtualStockExperimentProfileSpec{
            "", "Empty name", MakeHybridProfileSettings()},
    };
    constexpr std::array emptyLabel = {
        VirtualStockExperimentProfileSpec{
            "Empty_Label", "", MakeHybridProfileSettings()},
    };
    constexpr std::array duplicateLabels = {
        VirtualStockExperimentProfileSpec{
            "First_Label", "Duplicate label", MakeHybridProfileSettings()},
        VirtualStockExperimentProfileSpec{
            "Second_Label", "Duplicate label", MakeHybridProfileSettings()},
    };
    constexpr std::array coreCollision = {
        VirtualStockExperimentProfileSpec{
            "D_HybridBaseline", "Core collision", MakeHybridProfileSettings()},
    };
    constexpr std::array coreLabelCollision = {
        VirtualStockExperimentProfileSpec{
            "Core_Label_Collision", "D - Hybrid Baseline",
            MakeHybridProfileSettings()},
    };
    constexpr std::array badScalar = {
        VirtualStockExperimentProfileSpec{
            "Bad_Scalar", "Bad scalar", [] {
                auto settings = MakeHybridProfileSettings();
                settings.virtualStockStrength = 2.0f;
                return settings;
            }()},
    };
    constexpr std::array badSeat = {
        VirtualStockExperimentProfileSpec{
            "Bad_Seat", "Bad seat", MakeHybridTuningProfileSettings(0.40f, 0.40f)},
    };
    constexpr std::array badHorizontal = {
        VirtualStockExperimentProfileSpec{
            "Bad_Horizontal", "Bad horizontal",
            MakeHybridHorizontalReleaseProfileSettings(0.45f, 0.30f)},
    };
    constexpr std::array badProximity = {
        VirtualStockExperimentProfileSpec{
            "Bad_Proximity", "Bad proximity", [] {
                auto settings = MakeHybridProfileSettings();
                settings.virtualStockProximityFullM = 0.50f;
                settings.virtualStockProximityReleaseM = 0.40f;
                return settings;
            }()},
    };
    Check(!VirtualStockExperimentProfileNamesAreUnique(duplicateNames) &&
            !VirtualStockExperimentProfileNamesAreNonEmpty(emptyName) &&
            !VirtualStockExperimentProfileLabelsAreNonEmpty(emptyLabel) &&
            !VirtualStockExperimentProfileLabelsAreUnique(duplicateLabels) &&
            !VirtualStockExperimentProfileNamesDoNotCollideWithCore(
                coreCollision) &&
            !VirtualStockExperimentProfileLabelsDoNotCollideWithCore(
                coreLabelCollision) &&
            !VirtualStockExperimentScalarSettingsAreValid(badScalar) &&
            !VirtualStockExperimentSettingIsInRange(
                badScalar, &VirtualStockAimSettings::virtualStockStrength,
                kVirtualStockStrengthMinimum, kVirtualStockStrengthMaximum) &&
            !VirtualStockExperimentSeatSettingsAreValid(badSeat) &&
            !VirtualStockExperimentSettingSeparationIsValid(
                badSeat,
                &VirtualStockAimSettings::virtualStockHybridSeatFullM,
                &VirtualStockAimSettings::virtualStockHybridSeatReleaseM,
                kVirtualStockHybridSeatMinimumSeparationM) &&
            !VirtualStockExperimentHorizontalReleaseSettingsAreValid(
                badHorizontal) &&
            !VirtualStockExperimentSettingSeparationIsValid(
                badHorizontal,
                &VirtualStockAimSettings::hybridHorizontalRearReleaseFullM,
                &VirtualStockAimSettings::hybridHorizontalRearReleaseReleaseM,
                kVirtualStockProximityMinimumSeparationM) &&
            !VirtualStockExperimentProximitySettingsAreValid(badProximity) &&
            !VirtualStockExperimentSettingSeparationIsValid(
                badProximity,
                &VirtualStockAimSettings::virtualStockProximityFullM,
                &VirtualStockAimSettings::virtualStockProximityReleaseM,
                kVirtualStockProximityMinimumSeparationM) &&
            !VirtualStockExperimentProfileCountIsValid(
                kVirtualStockExperimentProfileCapacity + 1),
        "Hostile experiment packs are rejected by focused compile-time predicates");
    Check(NormalizeVirtualStockTestProfile(15) ==
              VirtualStockTestProfile::Custom &&
            NormalizeVirtualStockTestProfile(127) ==
              VirtualStockTestProfile::Custom &&
            NormalizeVirtualStockTestProfile(255) ==
              VirtualStockTestProfile::Custom,
        "Reserved and invalid profile IDs normalize to Custom");
    const auto firstConfiguredTemporary = static_cast<VirtualStockTestProfile>(
        kVirtualStockExperimentProfileFirstId);
    Check(kVirtualStockExperimentProfileCount != 0
            ? FindVirtualStockTestProfileDefinition(firstConfiguredTemporary) !=
                  nullptr &&
                NormalizeVirtualStockTestProfile(
                    kVirtualStockExperimentProfileFirstId) ==
                    firstConfiguredTemporary
            : NormalizeVirtualStockTestProfile(
                  kVirtualStockExperimentProfileFirstId) ==
                  VirtualStockTestProfile::Custom,
        "Configured temporary ID 128 resolves, while an empty pack leaves it invalid");

    const VirtualStockAimSettings maxSeat = ResolveVirtualStockTestProfile(
        VirtualStockTestProfile::E_HybridMaxSeat,
        CanonicalVirtualStockAimSettings());
    Check(maxSeat.virtualStockHybridSeatFullM ==
                kVirtualStockHybridSeatFullMaximumM &&
            maxSeat.virtualStockHybridSeatReleaseM ==
                kVirtualStockHybridSeatReleaseMaximumM &&
            maxSeat.virtualStockHybridSeatReleaseM -
                    maxSeat.virtualStockHybridSeatFullM >=
                kVirtualStockHybridSeatMinimumSeparationM,
        "Profile E is exactly the maximum-seat 0.600/0.800 stress condition");

    VirtualStockAimSettings custom = CanonicalVirtualStockAimSettings();
    custom.virtualStockEnabled = true;
    custom.virtualStockStrength = 0.37f;
    custom.virtualStockRearHeightM = -0.11f;
    custom.virtualStockRearReference = 2;
    custom.virtualStockShoulderBackM = 0.17f;
    custom.virtualStockShoulderSideM = 0.18f;
    custom.virtualStockChestHeightM = -0.41f;
    custom.virtualStockChestBackM = 0.13f;
    custom.virtualStockChestSideM = 0.19f;
    custom.virtualStockAdaptiveTopHeightM = -0.21f;
    custom.virtualStockAdaptiveBottomHeightM = -0.51f;
    custom.virtualStockAdaptiveTopHalfWidthM = 0.09f;
    custom.virtualStockAdaptiveBottomHalfWidthM = 0.18f;
    custom.virtualStockHybridOffhandInfluence = 0.73f;
    custom.virtualStockHybridAdsReference = 1;
    custom.virtualStockHybridSeatFullM = 0.08f;
    custom.virtualStockHybridSeatReleaseM = 0.22f;
    custom.hybridHorizontalRearReleaseFullM = 0.29f;
    custom.hybridHorizontalRearReleaseReleaseM = 0.51f;
    custom.hybridInverseNeckEnabled = true;
    custom.hybridInverseNeckStrength = 0.62f;
    custom.hybridInverseNeckForwardM = 0.12f;
    custom.hybridInverseNeckUpM = 0.03f;
    custom.hybridInverseNeckLateralM = -0.02f;
    custom.hybridDiagnosticOverride = HybridDiagnosticOverride::ForceStock;
    custom.virtualStockProximityRelease = true;
    custom.virtualStockProximityFullM = 0.31f;
    custom.virtualStockProximityReleaseM = 0.57f;
    Check(SameVirtualStockSettings(
            ResolveVirtualStockTestProfile(
                VirtualStockTestProfile::Custom, custom), custom),
        "Custom resolves immediately back to every ordinary user setting");
    Check(SameVirtualStockSettings(
            ResolveVirtualStockTestProfile(
                static_cast<VirtualStockTestProfile>(99), custom), custom),
        "Invalid profile values normalize to Custom settings");

    VirtualStockTestProfileState state;
    Check(state.Load() == VirtualStockTestProfile::Custom,
        "A fresh test-profile state starts in Custom");
    state.Store(VirtualStockTestProfile::A_VsOffControl);
    Check(state.Load() == VirtualStockTestProfile::A_VsOffControl,
        "Test-profile state stores a runtime profile");
    state.Store(static_cast<VirtualStockTestProfile>(99));
    Check(state.Load() == VirtualStockTestProfile::Custom,
        "Invalid test-profile state normalizes to Custom");
    VirtualStockTestProfileState freshState;
    Check(freshState.Load() == VirtualStockTestProfile::Custom,
        "A fresh process-equivalent test-profile state resets to Custom");

    state.Store(VirtualStockTestProfile::E_HybridMaxSeat);
    const VirtualStockAimSettings activeMaxSeat =
        ResolveVirtualStockTestProfile(state.Load(), custom);
    Check(activeMaxSeat.virtualStockRearReference == 3 &&
            Near(activeMaxSeat.virtualStockHybridSeatFullM, 0.600f) &&
            Near(activeMaxSeat.virtualStockHybridSeatReleaseM, 0.800f),
        "Selecting a profile resolves effective settings without changing Custom values");
    state.Store(VirtualStockTestProfile::Custom);
    Check(SameVirtualStockSettings(
            ResolveVirtualStockTestProfile(
                state.Load(), custom), custom),
        "Switching the runtime profile back to Custom restores user settings");
}
}

int main()
{
    TestHybridMode3();
    TestHybridEndpointsAndFallbacks();
    TestHybridTargetsAndCalibration();
    TestHybridIgnoresLegacyProximity();
    TestHybridHorizontalReleaseComposition();
    TestHybridDiagnosticState();
    TestTwoHandInputSmoothing();
    TestTwoHandInputSmoothingStrength();
    TestTwoHandSmoothingVirtualStockConsumption();
    TestHybridStockEquivalence();
    TestHybridDiagnosticOverrides();
    TestHybridDiagnosticModeIsolation();
    TestAimTraceObservationalEquivalence();
    TestPersistentGripRetainedSteering();
    TestFixedGripProductionGeometry();
    TestFreeTwoHandOffhandInfluence();
    TestAimTraceSemantics();
    TestCounterfactualProfileIdentity();
    TestHybridInverseNeckIntegration();
    TestPlantedStandardSwayMatrix();
    TestDormantChestUnaffectedBySway();
    TestVirtualStockTestProfiles();
    std::printf("Production Hybrid aim-pose fixture: %u checks, %u failures\n",
        checks, failures);
    return failures ? 1 : 0;
}
