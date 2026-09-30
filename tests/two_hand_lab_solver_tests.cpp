// Two-Hand Lab tranche 2A solver integration tests.
//
// Fixture-driven against the shipping solver (tools/generate_aim_pose_fixture.py
// extracts AimPoseInputs/AimPoseResult/ComputeAimPoseImpl/ComputeAimPose from
// the current source): every assertion below exercises the production code,
// not a model of it. Pure Lab maths (ResolvePivots, SoftAuthorityConfidence)
// are reused here as the independent oracle for the trace/live coherence
// checks, exactly as the mission requires (assert against the same resolved
// pair the live solve used).
#include <Windows.h>
#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/common/two_hand_lab_logic.h"
#include "../src/dll/aim_pose_trace.h"
#include <cmath>
#include <cstdio>

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

XrVector3f AimForward(const AimPoseResult& result)
{
    return Rotate(result.pose.orientation, {0.0f, 0.0f, -1.0f});
}

XrVector3f V3(float x, float y, float z)
{
    return {x, y, z};
}

// VS-off two-hand baseline with an accepted B: D=(0,0,0) faces -Z,
// S=(0.3,0,-1) gives agreement ~0.958, comfortably above the 0.35 floor.
AimPoseInputs LabBaselineInputs()
{
    AimPoseInputs inputs{};
    inputs.rightValid = true;
    inputs.right.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.right.position = {0.0f, 0.0f, 0.0f};
    inputs.leftValid = true;
    inputs.left.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    inputs.left.position = {0.3f, 0.0f, -1.0f};
    inputs.supportPosition = {0.3f, 0.0f, -1.0f};
    inputs.twoHandEnabled = true;
    inputs.twoHandLatched = true;
    inputs.virtualStockEnabled = false;
    return inputs;
}

void SetLab(AimPoseInputs& inputs, int anchor, float influence, int agreement,
    float fullAgreement, bool primaryGripValid, XrVector3f primaryGrip,
    bool supportGripValid, XrVector3f supportGrip)
{
    inputs.twoHandLabEnabled = true;
    inputs.twoHandLabAnchor = anchor;
    inputs.twoHandLabOffhandInfluence = influence;
    inputs.twoHandLabAgreement = agreement;
    inputs.twoHandLabSoftFullAgreement = fullAgreement;
    inputs.twoHandLabTemporal = 0;
    inputs.primaryGripValid = primaryGripValid;
    inputs.primaryGripPosition = primaryGrip;
    inputs.supportGripValid = supportGripValid;
    inputs.supportGripPosition = supportGrip;
}

two_hand_lab::PivotInputs LabPivotInputs(const AimPoseInputs& inputs)
{
    two_hand_lab::PivotInputs pivots{};
    pivots.primaryAim = {inputs.right.position.x, inputs.right.position.y,
        inputs.right.position.z};
    pivots.supportAim = {inputs.left.position.x, inputs.left.position.y,
        inputs.left.position.z};
    pivots.primaryGripValid = inputs.primaryGripValid;
    pivots.primaryGrip = {inputs.primaryGripPosition.x,
        inputs.primaryGripPosition.y, inputs.primaryGripPosition.z};
    pivots.supportGripValid = inputs.supportGripValid;
    pivots.supportGrip = {inputs.supportGripPosition.x,
        inputs.supportGripPosition.y, inputs.supportGripPosition.z};
    pivots.productionSupportUsedGrip = inputs.supportEndpointUsedGrip;
    pivots.productionSupportPosition = {inputs.supportPosition.x,
        inputs.supportPosition.y, inputs.supportPosition.z};
    return pivots;
}

void TestLabDisabledParity()
{
    // Product GG (W2): with Virtual Stock off and the hold latched, the
    // production positional pair is primary Grip -> support Grip. The parity
    // fixture therefore carries ONE fixed valid grip pair (the production
    // geometry) and varies only the stored Lab state: the Lab-disabled solve
    // must stay exactly the product solve.
    AimPoseInputs baseline = LabBaselineInputs();
    baseline.primaryGripValid = true;
    baseline.primaryGripPosition = V3(0.1f, 0.0f, -0.2f);
    baseline.supportGripValid = true;
    baseline.supportGripPosition = V3(0.4f, 0.1f, -0.9f);
    const AimPoseResult expected = ComputeAimPose(baseline);
    Check(expected.valid && expected.twoHandActive,
        "Lab parity fixture accepts B on the VS-off GG production baseline");
    // With the same grips removed the solve resolves to the aim-line pair and
    // is a real differential, so the parity assertions below are not vacuous.
    AimPoseInputs aimLine = baseline;
    aimLine.primaryGripValid = false;
    aimLine.supportGripValid = false;
    Check(!ExactAimResult(ComputeAimPose(aimLine), expected),
        "the parity baseline is the production Grip pair, not the aim-line pair");
    // Disabled with arbitrary stored selections, influences and agreement
    // modes (the production grip pair unchanged): byte-identical output, no
    // Lab trace.
    const int anchors[] = {0, 1, 2, 3, 4};
    const float influences[] = {0.0f, 0.5f, 1.0f};
    for (const int anchor : anchors)
    {
        for (const float influence : influences)
        {
            AimPoseInputs inputs = baseline;
            inputs.twoHandLabEnabled = false;
            inputs.twoHandLabAnchor = anchor;
            inputs.twoHandLabOffhandInfluence = influence;
            inputs.twoHandLabAgreement = 1;
            inputs.twoHandLabSoftFullAgreement = 0.36f;
            inputs.twoHandLabTemporal = 2;
            inputs.twoHandLabGeneration = 7;
            AimPoseTrace trace{};
            const AimPoseResult actual = ComputeAimPose(inputs, &trace);
            Check(ExactAimResult(actual, expected) && !trace.labValid,
                "Lab disabled ignores every stored selection, influence, "
                "agreement and temporal mode exactly");
        }
    }
    // W5 (the Lab is hidden from navigation): the identity the hide rests on.
    // An empty Lab state (the baseline above, every Lab input at its default)
    // and a maximally populated-but-disabled Lab state must produce
    // byte-identical output and no Lab trace: production never reads stored
    // Lab overrides. The stored state is a real differential once the Lab is
    // enabled (asserted immediately below), so the identity is not vacuous.
    AimPoseInputs populatedDisabled = baseline;
    populatedDisabled.twoHandLabEnabled = false;
    populatedDisabled.twoHandLabAnchor = 0;
    populatedDisabled.twoHandLabOffhandInfluence = 0.5f;
    populatedDisabled.twoHandLabAgreement = 1;
    populatedDisabled.twoHandLabSoftFullAgreement = 0.36f;
    populatedDisabled.twoHandLabTemporal = 2;
    populatedDisabled.twoHandLabGeneration = 7;
    AimPoseTrace populatedDisabledTrace{};
    const AimPoseResult populatedDisabledResult =
        ComputeAimPose(populatedDisabled, &populatedDisabledTrace);
    Check(ExactAimResult(populatedDisabledResult, expected) &&
            !populatedDisabledTrace.labValid,
        "an empty Lab state and a populated-but-disabled Lab state solve "
        "identically: production never reads stored Lab overrides");
    AimPoseInputs populatedEnabled = populatedDisabled;
    populatedEnabled.twoHandLabEnabled = true;
    AimPoseTrace populatedEnabledTrace{};
    const AimPoseResult populatedEnabledResult =
        ComputeAimPose(populatedEnabled, &populatedEnabledTrace);
    Check(populatedEnabledResult.valid && populatedEnabledTrace.labValid &&
            !ExactAimResult(populatedEnabledResult, expected),
        "the populated stored Lab state is a real differential when enabled");
    // Enabled Baseline preset (Production/1.0/Legacy/None): the Production
    // anchor is the rig's own pass-through of the caller's production pair -
    // primary AIM plus the selected production support endpoint. That is the
    // pre-GG production shape, so it is a genuine third geometry, distinct
    // from both the product GG pair and the aim-line fallback.
    AimPoseInputs preset = baseline;
    SetLab(preset, 0, 1.0f, 0, 0.9f, true, baseline.primaryGripPosition, true,
        baseline.supportGripPosition);
    AimPoseTrace presetTrace{};
    const AimPoseResult presetResult = ComputeAimPose(preset, &presetTrace);
    Check(presetResult.valid && presetResult.twoHandActive &&
            presetTrace.labValid && presetTrace.lab.labActiveThisFrame &&
            presetTrace.lab.requestedAnchor ==
                two_hand_lab::AnchorMode::Production &&
            presetTrace.lab.resolvedAnchor ==
                two_hand_lab::ResolvedAnchor::Production &&
            presetTrace.lab.fallback == two_hand_lab::FallbackReason::None &&
            presetTrace.lab.bValid &&
            Near(presetTrace.lab.effectiveInfluence, 1.0f),
        "Lab Baseline preset reports an active Production solve");
    Check(Near(presetTrace.lab.primaryPivot.x, preset.right.position.x) &&
            Near(presetTrace.lab.primaryPivot.y, preset.right.position.y) &&
            Near(presetTrace.lab.primaryPivot.z, preset.right.position.z) &&
            Near(presetTrace.lab.supportPivot.x, preset.supportPosition.x) &&
            Near(presetTrace.lab.supportPivot.y, preset.supportPosition.y) &&
            Near(presetTrace.lab.supportPivot.z, preset.supportPosition.z),
        "Lab Production anchor passes the caller's production pair through "
        "exactly");
    // The product's own pair is what the GG anchor resolves for the same
    // sample, and it is a real differential against the rig's Production
    // pass-through while the grips differ from the production endpoint.
    AimPoseInputs gg = baseline;
    SetLab(gg, 4, 1.0f, 0, 0.9f, true, baseline.primaryGripPosition, true,
        baseline.supportGripPosition);
    AimPoseTrace ggTrace{};
    const AimPoseResult ggResult = ComputeAimPose(gg, &ggTrace);
    Check(ExactAimResult(ggResult, expected) && ggTrace.labValid &&
            ggTrace.lab.pivotsValid &&
            ggTrace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::GG &&
            Near(ggTrace.lab.primaryPivot.x, baseline.primaryGripPosition.x) &&
            Near(ggTrace.lab.primaryPivot.y, baseline.primaryGripPosition.y) &&
            Near(ggTrace.lab.primaryPivot.z, baseline.primaryGripPosition.z) &&
            Near(ggTrace.lab.supportPivot.x, baseline.supportGripPosition.x) &&
            Near(ggTrace.lab.supportPivot.y, baseline.supportGripPosition.y) &&
            Near(ggTrace.lab.supportPivot.z, baseline.supportGripPosition.z),
        "Lab GG anchor reproduces the product Grip pair exactly");
    Check(!ExactAimResult(presetResult, ggResult),
        "the Lab Production pass-through and the product GG pair are genuinely "
        "different anchors");
}

// W6 scope audit: the Reduce Support-Hand Rotation selection is an endpoint
// choice for Virtual Stock (and for the rig's Production pass-through), never a
// grip-sampling or grip-pivot switch. Both states of that selection leave the
// Lab's grip anchors (GG/AG/GA) and the product Grip pair untouched and valid.
void TestLabGripPivotsIndependentOfSupportRotation()
{
    const XrVector3f primaryGrip = V3(0.05f, -0.02f, -0.15f);
    const XrVector3f supportGrip = V3(0.45f, 0.05f, -0.85f);
    const auto withRotation = [&](bool enabled) {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 4, 1.0f, 0, 0.9f, true, primaryGrip, true, supportGrip);
        inputs.supportGripPoseEnabled = enabled;
        inputs.supportEndpointUsedGrip = enabled;
        inputs.supportPosition = enabled ? supportGrip : inputs.left.position;
        return inputs;
    };
    AimPoseTrace enabledTrace{};
    AimPoseTrace disabledTrace{};
    const AimPoseResult enabledResult =
        ComputeAimPose(withRotation(true), &enabledTrace);
    const AimPoseResult disabledResult =
        ComputeAimPose(withRotation(false), &disabledTrace);
    Check(enabledTrace.labValid && disabledTrace.labValid &&
            enabledTrace.lab.pivotsValid && disabledTrace.lab.pivotsValid &&
            enabledTrace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::GG &&
            disabledTrace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::GG &&
            enabledTrace.lab.fallback == two_hand_lab::FallbackReason::None &&
            disabledTrace.lab.fallback == two_hand_lab::FallbackReason::None &&
            enabledTrace.lab.primaryGripValid && disabledTrace.lab.primaryGripValid &&
            enabledTrace.lab.supportGripValid && disabledTrace.lab.supportGripValid,
        "toggling the support-rotation selection never changes GG pivot availability");
    Check(ExactAimResult(enabledResult, disabledResult) &&
            Near(enabledTrace.lab.primaryPivot.x, primaryGrip.x) &&
            Near(enabledTrace.lab.primaryPivot.z, primaryGrip.z) &&
            Near(enabledTrace.lab.supportPivot.x, supportGrip.x) &&
            Near(enabledTrace.lab.supportPivot.z, supportGrip.z) &&
            enabledTrace.lab.primaryPivot.x == disabledTrace.lab.primaryPivot.x &&
            enabledTrace.lab.supportPivot.z == disabledTrace.lab.supportPivot.z,
        "both support-rotation states resolve the identical GG pivots");
    // AG/GA stay grip-backed for the same reason: their availability comes
    // from the grip sample, not from the rotation setting.
    for (const int anchor : {2, 3})
    {
        AimPoseInputs on = withRotation(true);
        on.twoHandLabAnchor = anchor;
        AimPoseInputs off = withRotation(false);
        off.twoHandLabAnchor = anchor;
        AimPoseTrace onTrace{};
        AimPoseTrace offTrace{};
        const AimPoseResult onResult = ComputeAimPose(on, &onTrace);
        const AimPoseResult offResult = ComputeAimPose(off, &offTrace);
        Check(ExactAimResult(onResult, offResult) &&
                onTrace.lab.pivotsValid && offTrace.lab.pivotsValid &&
                onTrace.lab.resolvedAnchor == offTrace.lab.resolvedAnchor &&
                onTrace.lab.fallback == offTrace.lab.fallback &&
                onTrace.lab.fallback == two_hand_lab::FallbackReason::None,
            "grip-pivot anchors resolve identically on both rotation settings");
    }
}

void TestLabAnchors()
{
    const XrVector3f primaryGrip = V3(0.05f, -0.02f, -0.15f);
    const XrVector3f supportGrip = V3(0.45f, 0.05f, -0.85f);
    // AA always resolves to the two AIM pivots.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 1, 1.0f, 0, 0.9f, true, primaryGrip, true,
            supportGrip);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        Check(result.valid && result.twoHandActive && trace.labValid &&
                trace.lab.requestedAnchor == two_hand_lab::AnchorMode::AA &&
                trace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::AA &&
                trace.lab.fallback == two_hand_lab::FallbackReason::None &&
                trace.lab.primaryGripValid && trace.lab.supportGripValid,
            "Lab AA resolves AIM/AIM with no fallback");
    }
    // AG with a valid support grip uses it; without, visible AA fallback.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 2, 1.0f, 0, 0.9f, false, primaryGrip, true,
            supportGrip);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        Check(result.valid && result.twoHandActive && trace.labValid &&
                trace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::AG &&
                trace.lab.fallback == two_hand_lab::FallbackReason::None,
            "Lab AG consumes a valid support grip");
        AimPoseInputs fallback = inputs;
        fallback.supportGripValid = false;
        AimPoseTrace fallbackTrace{};
        const AimPoseResult fallbackResult =
            ComputeAimPose(fallback, &fallbackTrace);
        AimPoseInputs aa = LabBaselineInputs();
        SetLab(aa, 1, 1.0f, 0, 0.9f, false, primaryGrip, false,
            supportGrip);
        Check(fallbackTrace.labValid &&
                fallbackTrace.lab.resolvedAnchor ==
                    two_hand_lab::ResolvedAnchor::AA &&
                fallbackTrace.lab.fallback ==
                    two_hand_lab::FallbackReason::MissingSupportGrip &&
                !fallbackTrace.lab.supportGripValid &&
                ExactAimResult(fallbackResult, ComputeAimPose(aa)),
            "Lab AG without a support grip visibly falls back to AA");
    }
    // GA with a valid primary grip uses it; without, visible AA fallback.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 3, 1.0f, 0, 0.9f, true, primaryGrip, false,
            supportGrip);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        Check(result.valid && result.twoHandActive && trace.labValid &&
                trace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::GA &&
                trace.lab.fallback == two_hand_lab::FallbackReason::None,
            "Lab GA consumes a valid primary grip");
        AimPoseInputs fallback = inputs;
        fallback.primaryGripValid = false;
        AimPoseTrace fallbackTrace{};
        ComputeAimPose(fallback, &fallbackTrace);
        Check(fallbackTrace.labValid &&
                fallbackTrace.lab.resolvedAnchor ==
                    two_hand_lab::ResolvedAnchor::AA &&
                fallbackTrace.lab.fallback ==
                    two_hand_lab::FallbackReason::MissingPrimaryGrip,
            "Lab GA without a primary grip visibly falls back to AA");
    }
    // GG needs both grips; either missing is a visible AA fallback, never a
    // silent AG/GA.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 4, 1.0f, 0, 0.9f, true, primaryGrip, true,
            supportGrip);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        Check(result.valid && result.twoHandActive && trace.labValid &&
                trace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::GG &&
                trace.lab.fallback == two_hand_lab::FallbackReason::None,
            "Lab GG resolves GRIP/GRIP when both grips are valid");
        AimPoseInputs missingPrimary = inputs;
        missingPrimary.primaryGripValid = false;
        AimPoseTrace missingPrimaryTrace{};
        ComputeAimPose(missingPrimary, &missingPrimaryTrace);
        Check(missingPrimaryTrace.labValid &&
                missingPrimaryTrace.lab.resolvedAnchor ==
                    two_hand_lab::ResolvedAnchor::AA &&
                missingPrimaryTrace.lab.fallback ==
                    two_hand_lab::FallbackReason::MissingPrimaryGrip,
            "Lab GG without a primary grip falls back to AA, never GA");
        AimPoseInputs missingSupport = inputs;
        missingSupport.supportGripValid = false;
        AimPoseTrace missingSupportTrace{};
        ComputeAimPose(missingSupport, &missingSupportTrace);
        Check(missingSupportTrace.labValid &&
                missingSupportTrace.lab.resolvedAnchor ==
                    two_hand_lab::ResolvedAnchor::AA &&
                missingSupportTrace.lab.fallback ==
                    two_hand_lab::FallbackReason::MissingSupportGrip,
            "Lab GG without a support grip falls back to AA, never AG");
    }
    // Production passes the existing production-selected endpoint through,
    // including a grip endpoint, exactly. The fixture keeps one consistent
    // production selection: the support endpoint is the support grip sample,
    // and the support AIM point is that same point - the state the assembly
    // produces for an RSR grip endpoint. The primary grip stays unavailable, so
    // the product's fixed Grip -> Grip pair is not usable and its aim-line
    // fallback resolves the very pair the rig's Production anchor passes
    // through.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        inputs.supportGripValid = true;
        inputs.supportGripPosition = V3(0.42f, 0.08f, -0.78f);
        inputs.supportPosition = inputs.supportGripPosition;
        inputs.left.position = inputs.supportGripPosition;
        inputs.supportEndpointUsedGrip = true;
        const AimPoseResult expected = ComputeAimPose(inputs);
        SetLab(inputs, 0, 1.0f, 0, 0.9f, true, primaryGrip, true,
            supportGrip);
        AimPoseTrace trace{};
        const AimPoseResult actual = ComputeAimPose(inputs, &trace);
        Check(ExactAimResult(actual, expected) && trace.labValid &&
                trace.lab.resolvedAnchor ==
                    two_hand_lab::ResolvedAnchor::Production,
            "Lab Production passes the existing support endpoint through "
            "exactly");
    }
}

void TestLabInfluence()
{
    // Full influence follows the accepted-B path exactly.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        const AimPoseResult expected = ComputeAimPose(inputs);
        SetLab(inputs, 1, 1.0f, 0, 0.9f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        Check(ExactAimResult(ComputeAimPose(inputs), expected),
            "Lab influence 1 follows the accepted-B path exactly");
    }
    // Zero influence keeps the exact primary quaternion (same finishAimPose
    // from inputs.right as the one-hand path) while staying active.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 1, 0.0f, 0, 0.9f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        AimPoseTrace trace{};
        const AimPoseResult actual = ComputeAimPose(inputs, &trace);
        AimPoseInputs oneHand = inputs;
        oneHand.twoHandEnabled = false;
        oneHand.twoHandLabEnabled = false;
        const AimPoseResult oneHandResult = ComputeAimPose(oneHand);
        Check(actual.valid && actual.twoHandActive &&
                actual.pose.orientation.x ==
                    oneHandResult.pose.orientation.x &&
                actual.pose.orientation.y ==
                    oneHandResult.pose.orientation.y &&
                actual.pose.orientation.z ==
                    oneHandResult.pose.orientation.z &&
                actual.pose.orientation.w ==
                    oneHandResult.pose.orientation.w &&
                SamePosition(
                    actual.pose.position, inputs.right.position) &&
                trace.labValid && trace.exactAEndpointSelected &&
                !trace.orientationRebuildAttempted &&
                Near(trace.lab.effectiveInfluence, 0.0f),
            "Lab influence 0 emits the exact primary quaternion with "
            "accepted-B activity semantics");
    }
    // Intermediate influence is a finite strict intermediate.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 1, 0.5f, 0, 0.9f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        AimPoseTrace trace{};
        const AimPoseResult actual = ComputeAimPose(inputs, &trace);
        const XrVector3f forward = AimForward(actual);
        const XrVector3f aForward = V3(0.0f, 0.0f, -1.0f);
        const float bLength = std::sqrt(1.09f);
        const XrVector3f bDirection =
            V3(0.3f / bLength, 0.0f, -1.0f / bLength);
        const float dotA = forward.x * aForward.x + forward.y * aForward.y +
            forward.z * aForward.z;
        const float dotB = forward.x * bDirection.x +
            forward.y * bDirection.y + forward.z * bDirection.z;
        const float dotAB = aForward.x * bDirection.x +
            aForward.y * bDirection.y + aForward.z * bDirection.z;
        Check(actual.valid && actual.twoHandActive &&
                std::isfinite(forward.x) && std::isfinite(forward.y) &&
                std::isfinite(forward.z) && dotA > dotAB && dotB > dotAB &&
                !SamePosition(forward, aForward) &&
                !SamePosition(forward, bDirection) &&
                trace.labValid &&
                Near(trace.lab.effectiveInfluence, 0.5f) &&
                Near(trace.finalDirection.x, forward.x) &&
                Near(trace.finalDirection.y, forward.y) &&
                Near(trace.finalDirection.z, forward.z),
            "Lab influence 0.5 blends to a finite strict intermediate");
    }
    // Rejected B takes the exact legacy fallback, with Lab diagnostics.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        inputs.supportPosition = V3(0.0f, 0.0f, 0.5f);
        inputs.left.position = V3(0.0f, 0.0f, 0.5f);
        const AimPoseResult expected = ComputeAimPose(inputs);
        Check(expected.valid && !expected.twoHandActive &&
                expected.rejectedExtreme,
            "Lab rejection fixture rejects B below the 0.35 floor");
        SetLab(inputs, 4, 0.5f, 1, 0.9f, true, V3(0.0f, 0.0f, 0.4f), true,
            V3(0.0f, 0.0f, 0.45f));
        AimPoseTrace trace{};
        const AimPoseResult actual = ComputeAimPose(inputs, &trace);
        Check(ExactAimResult(actual, expected) && trace.labValid &&
                !trace.lab.bValid &&
                Near(trace.lab.effectiveInfluence, 0.0f) &&
                trace.exactAEndpointSelected,
            "Lab with rejected B takes the exact legacy fallback with "
            "zero authority");
    }
}

void TestLabAgreement()
{
    // LegacyHard matches current behaviour at partial influence.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        const AimPoseResult expected = ComputeAimPose(inputs);
        AimPoseInputs legacy = inputs;
        SetLab(legacy, 0, 1.0f, 0, 0.9f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        Check(ExactAimResult(ComputeAimPose(legacy), expected),
            "Lab LegacyHard at full influence matches current behaviour");
    }
    // The 0.35 boundary carries no authority by construction.
    Check(two_hand_lab::SoftAuthorityConfidence(0.35f, 0.9f) == 0.0f &&
            two_hand_lab::SoftAuthorityConfidence(0.349f, 0.9f) == 0.0f,
        "Lab Soft confidence is exactly zero at and below the 0.35 floor");
    // A degenerate fullAgreement band grants no Soft authority even for an
    // accepted B: exact primary, still active.
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, 1, 0.7f, 1, 0.35f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        AimPoseTrace trace{};
        const AimPoseResult actual = ComputeAimPose(inputs, &trace);
        AimPoseInputs oneHand = inputs;
        oneHand.twoHandEnabled = false;
        oneHand.twoHandLabEnabled = false;
        const AimPoseResult oneHandResult = ComputeAimPose(oneHand);
        Check(actual.valid && actual.twoHandActive &&
                actual.pose.orientation.x ==
                    oneHandResult.pose.orientation.x &&
                actual.pose.orientation.y ==
                    oneHandResult.pose.orientation.y &&
                actual.pose.orientation.z ==
                    oneHandResult.pose.orientation.z &&
                actual.pose.orientation.w ==
                    oneHandResult.pose.orientation.w &&
                trace.labValid && trace.exactAEndpointSelected &&
                Near(trace.lab.confidence, 0.0f) &&
                Near(trace.lab.effectiveInfluence, 0.0f),
            "Lab Soft with a degenerate band keeps exact A active at zero "
            "authority");
    }
    // At fullAgreement the Soft weight is full: identical to Legacy.
    {
        AimPoseInputs legacy = LabBaselineInputs();
        SetLab(legacy, 1, 0.7f, 0, 0.9f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        AimPoseInputs soft = legacy;
        soft.twoHandLabAgreement = 1;
        AimPoseTrace softTrace{};
        const AimPoseResult softResult = ComputeAimPose(soft, &softTrace);
        Check(softTrace.labValid &&
                Near(softTrace.lab.agreement, 0.95783f, 1.0e-4f) &&
                Near(softTrace.lab.confidence, 1.0f) &&
                ExactAimResult(softResult, ComputeAimPose(legacy)),
            "Lab Soft at full agreement carries full authority like Legacy");
    }
    // Between the floor and fullAgreement: effective = requested*confidence,
    // monotonic in agreement, output near-A and active for small weights.
    {
        bool monotonic = true;
        float previousEffective = -1.0f;
        for (int step = 0; step < 8; ++step)
        {
            // Wide to narrow: agreement rises, so effective must not fall.
            const float theta = (65.0f - step * (55.0f / 7.0f)) *
                3.14159265358979323846f / 180.0f;
            AimPoseInputs inputs = LabBaselineInputs();
            inputs.left.position = V3(
                std::sin(theta), 0.0f, -std::cos(theta));
            inputs.supportPosition = inputs.left.position;
            SetLab(inputs, 1, 0.7f, 1, 0.9f, false, V3(0, 0, 0), false,
                V3(0, 0, 0));
            AimPoseTrace trace{};
            const AimPoseResult result = ComputeAimPose(inputs, &trace);
            const float expectedConfidence =
                two_hand_lab::SoftAuthorityConfidence(
                    trace.lab.agreement, 0.9f);
            monotonic = monotonic && result.valid && result.twoHandActive &&
                trace.labValid && trace.lab.bValid &&
                Near(trace.lab.requestedInfluence, 0.7f) &&
                Near(trace.lab.effectiveInfluence,
                    0.7f * expectedConfidence, 1.0e-6f) &&
                trace.lab.effectiveInfluence + 1.0e-6f >=
                    previousEffective;
            previousEffective = trace.lab.effectiveInfluence;
        }
        Check(monotonic,
            "Lab Soft effective influence equals requested*confidence and "
            "grows monotonically with agreement");
        // Small-agreement Soft stays near A while active.
        AimPoseInputs inputs = LabBaselineInputs();
        inputs.left.position = V3(0.91652f, 0.0f, -0.4f);
        inputs.supportPosition = inputs.left.position;
        SetLab(inputs, 1, 0.5f, 1, 0.9f, false, V3(0, 0, 0), false,
            V3(0, 0, 0));
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        const XrVector3f forward = AimForward(result);
        Check(result.valid && result.twoHandActive && trace.labValid &&
                trace.lab.bValid &&
                trace.lab.effectiveInfluence > 0.0f &&
                trace.lab.effectiveInfluence < 0.05f &&
                Near(forward.z, -1.0f, 1.0e-3f) &&
                Near(forward.x, 0.0f, 2.0e-2f) &&
                Near(forward.y, 0.0f, 1.0e-5f),
            "Lab Soft just above the floor keeps near-A aim active at a "
            "small but nonzero weight");
    }
}

void TestLabVsIsolation()
{
    AimPoseInputs stock = LabBaselineInputs();
    stock.virtualStockEnabled = true;
    stock.virtualStockRearReference = 0;
    stock.virtualStockStrength = 0.95f;
    stock.headValid = true;
    stock.headPosition = V3(0.0f, 0.5f, 0.0f);
    stock.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    const AimPoseResult expectedFixed = ComputeAimPose(stock);
    AimPoseInputs labStock = stock;
    SetLab(labStock, 4, 0.0f, 1, 0.36f, true, V3(1.0f, 1.0f, 1.0f), true,
        V3(-1.0f, -1.0f, -1.0f));
    AimPoseTrace stockTrace{};
    const AimPoseResult actualFixed = ComputeAimPose(labStock, &stockTrace);
    Check(ExactAimResult(actualFixed, expectedFixed) && !stockTrace.labValid,
        "Lab GG/zero/Soft is fully ignored while Virtual Stock is on "
        "(fixed Head)");
    AimPoseInputs hybrid = stock;
    hybrid.virtualStockRearReference = 3;
    const AimPoseResult expectedHybrid = ComputeAimPose(hybrid);
    AimPoseInputs labHybrid = hybrid;
    SetLab(labHybrid, 4, 0.0f, 1, 0.36f, true, V3(1.0f, 1.0f, 1.0f), true,
        V3(-1.0f, -1.0f, -1.0f));
    AimPoseTrace hybridTrace{};
    const AimPoseResult actualHybrid = ComputeAimPose(labHybrid, &hybridTrace);
    Check(ExactAimResult(actualHybrid, expectedHybrid) &&
            !hybridTrace.labValid,
        "Lab GG/zero/Soft is fully ignored while Virtual Stock is on "
        "(Hybrid)");
}

void TestProductInfluenceIsolation()
{
    // W3: the free two-hand product authority is never read while the Lab owns
    // the solve. A Lab-active VS-off solve depends only on its own stored
    // influence and agreement mode, so productising the product setting cannot
    // change Lab behaviour.
    AimPoseInputs lab = LabBaselineInputs();
    SetLab(lab, 4, 0.5f, 0, 0.9f, true, V3(0.1f, 0.0f, -0.2f), true,
        V3(0.4f, 0.1f, -0.9f));
    AimPoseInputs productZero = lab;
    productZero.twoHandOffhandInfluence = 0.0f;
    AimPoseInputs productFull = lab;
    productFull.twoHandOffhandInfluence = 1.0f;
    AimPoseTrace productFullTrace{};
    const AimPoseResult productFullResult =
        ComputeAimPose(productFull, &productFullTrace);
    Check(productFullTrace.labValid &&
            productFullTrace.lab.labActiveThisFrame &&
            Near(productFullTrace.lab.requestedInfluence, 0.5f) &&
            Near(productFullTrace.lab.effectiveInfluence, 0.5f) &&
            productFullResult.valid && productFullResult.twoHandActive,
        "a Lab-active solve reports the Lab's own stored influence");
    Check(ExactAimResult(ComputeAimPose(productZero), productFullResult) &&
            ExactAimResult(ComputeAimPose(lab), productFullResult),
        "a Lab-active solve ignores the free two-hand product influence entirely");
    AimPoseInputs labZero = lab;
    labZero.twoHandLabOffhandInfluence = 0.0f;
    Check(!ExactAimResult(ComputeAimPose(labZero), productFullResult),
        "the Lab's own stored influence still controls a Lab-active solve");
}

void TestLabHandedness()
{
    // The solver keys off semantic primary/support (inputs.right/left): the
    // VS-off Lab path ignores the physical handedness flag, and GG/GA route
    // the semantic grips.
    AimPoseInputs inputs = LabBaselineInputs();
    inputs.virtualStockLeftHanded = true;
    SetLab(inputs, 4, 1.0f, 0, 0.9f, true, V3(0.05f, -0.02f, -0.15f), true,
        V3(0.45f, 0.05f, -0.85f));
    AimPoseTrace trace{};
    const AimPoseResult leftHanded = ComputeAimPose(inputs, &trace);
    AimPoseInputs rightHanded = inputs;
    rightHanded.virtualStockLeftHanded = false;
    Check(trace.labValid &&
            trace.lab.resolvedAnchor == two_hand_lab::ResolvedAnchor::GG &&
            ExactAimResult(leftHanded, ComputeAimPose(rightHanded)),
        "Lab GG uses semantic primary/support grips independent of the "
        "handedness flag");
    AimPoseInputs ga = inputs;
    ga.twoHandLabAnchor = 3;
    AimPoseTrace gaTrace{};
    const AimPoseResult gaResult = ComputeAimPose(ga, &gaTrace);
    Check(gaResult.valid && gaResult.twoHandActive && gaTrace.labValid &&
            gaTrace.lab.resolvedAnchor ==
                two_hand_lab::ResolvedAnchor::GA,
        "Lab GA routes the semantic primary grip while left-handed");
}

void TestLabTraceLiveCoherence()
{
    // For every anchor mode plus each fallback, the trace's B/D/S
    // description must match the live selection inputs: recompute the same
    // resolved pair in-test and compare.
    struct Case
    {
        int anchor;
        bool primaryGripValid;
        bool supportGripValid;
        two_hand_lab::ResolvedAnchor resolved;
        two_hand_lab::FallbackReason fallback;
    };
    const XrVector3f primaryGrip = V3(0.05f, -0.02f, -0.15f);
    const XrVector3f supportGrip = V3(0.45f, 0.05f, -0.85f);
    const Case cases[] = {
        {0, true, true, two_hand_lab::ResolvedAnchor::Production,
            two_hand_lab::FallbackReason::None},
        {1, false, false, two_hand_lab::ResolvedAnchor::AA,
            two_hand_lab::FallbackReason::None},
        {2, false, true, two_hand_lab::ResolvedAnchor::AG,
            two_hand_lab::FallbackReason::None},
        {2, false, false, two_hand_lab::ResolvedAnchor::AA,
            two_hand_lab::FallbackReason::MissingSupportGrip},
        {3, true, false, two_hand_lab::ResolvedAnchor::GA,
            two_hand_lab::FallbackReason::None},
        {3, false, false, two_hand_lab::ResolvedAnchor::AA,
            two_hand_lab::FallbackReason::MissingPrimaryGrip},
        {4, true, true, two_hand_lab::ResolvedAnchor::GG,
            two_hand_lab::FallbackReason::None},
        {4, false, true, two_hand_lab::ResolvedAnchor::AA,
            two_hand_lab::FallbackReason::MissingPrimaryGrip},
        {4, true, false, two_hand_lab::ResolvedAnchor::AA,
            two_hand_lab::FallbackReason::MissingSupportGrip},
    };
    for (const Case& testCase : cases)
    {
        AimPoseInputs inputs = LabBaselineInputs();
        SetLab(inputs, testCase.anchor, 1.0f, 0, 0.9f,
            testCase.primaryGripValid, primaryGrip,
            testCase.supportGripValid, supportGrip);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        const two_hand_lab::ResolvedPivots expected =
            two_hand_lab::ResolvePivots(
                LabPivotInputs(inputs),
                static_cast<two_hand_lab::AnchorMode>(testCase.anchor));
        const virtual_stock::Point3 delta{
            expected.support.x - expected.primary.x,
            expected.support.y - expected.primary.y,
            expected.support.z - expected.primary.z};
        const float length = std::sqrt(
            delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const virtual_stock::Point3 expectedB{
            delta.x / length, delta.y / length, delta.z / length};
        const float expectedAgreement = expectedB.x * 0.0f +
            expectedB.y * 0.0f + expectedB.z * -1.0f;
        char message[160];
        std::snprintf(message, sizeof(message),
            "Lab trace/live coherence holds for anchor %d (resolved %d)",
            testCase.anchor, static_cast<int>(expected.resolved));
        Check(result.valid && result.twoHandActive && trace.labValid &&
                expected.valid && expected.resolved == testCase.resolved &&
                expected.fallbackReason == testCase.fallback &&
                trace.lab.requestedAnchor ==
                    static_cast<two_hand_lab::AnchorMode>(
                        testCase.anchor) &&
                trace.lab.resolvedAnchor == expected.resolved &&
                trace.lab.fallback == expected.fallbackReason &&
                trace.lab.primaryGripValid ==
                    testCase.primaryGripValid &&
                trace.lab.supportGripValid ==
                    testCase.supportGripValid &&
                trace.bAttempted && trace.bAccepted && trace.lab.bValid &&
                Near(trace.bDirection.x, expectedB.x) &&
                Near(trace.bDirection.y, expectedB.y) &&
                Near(trace.bDirection.z, expectedB.z) &&
                Near(trace.bAgreement, expectedAgreement) &&
                Near(trace.finalDirection.x, expectedB.x) &&
                Near(trace.finalDirection.y, expectedB.y) &&
                Near(trace.finalDirection.z, expectedB.z),
            message);
    }
}

void TestLabCounterfactualInert()
{
    AimPoseInputs inputs = LabBaselineInputs();
    SetLab(inputs, 4, 0.5f, 1, 0.9f, true, V3(0.05f, 0.0f, -0.15f), true,
        V3(0.45f, 0.05f, -0.85f));
    const AimPoseInputs control =
        AimPoseInputsForProfile(inputs, kVsOffControlProfile);
    Check(!control.twoHandLabEnabled,
        "AimPoseInputsForProfile clears the Lab enable for "
        "control/counterfactual solves");
    AimPoseInputs manual = inputs;
    manual.twoHandLabEnabled = false;
    const AimPoseInputs manualControl =
        AimPoseInputsForProfile(manual, kVsOffControlProfile);
    Check(ExactAimResult(ComputeAimPose(control),
            ComputeAimPose(manualControl)),
        "Lab-inert counterfactual equals the manually disabled solve");
}
}

int main()
{
    TestLabDisabledParity();
    TestLabGripPivotsIndependentOfSupportRotation();
    TestLabAnchors();
    TestLabInfluence();
    TestLabAgreement();
    TestLabVsIsolation();
    TestProductInfluenceIsolation();
    TestLabHandedness();
    TestLabTraceLiveCoherence();
    TestLabCounterfactualInert();
    std::printf("Two-Hand Lab tranche 2A solver integration: %u checks, %u failures\n",
        checks, failures);
    return failures ? 1 : 0;
}
