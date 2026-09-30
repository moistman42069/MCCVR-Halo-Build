#include "two_hand_lab_logic.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace
{
using two_hand_lab::AdaptiveDampingStep;
using two_hand_lab::AnchorMode;
using two_hand_lab::ConstantDampingStep;
using two_hand_lab::DefaultSettings;
using two_hand_lab::Diagnostics;
using two_hand_lab::FallbackReason;
using two_hand_lab::Normalize;
using two_hand_lab::PivotInputs;
using two_hand_lab::QuickPreset;
using two_hand_lab::QuickPresetSettings;
using two_hand_lab::ResolvedAnchor;
using two_hand_lab::ResolvePivots;
using two_hand_lab::Settings;
using two_hand_lab::ShortestArcAngleDeg;
using two_hand_lab::SoftAuthorityConfidence;
using virtual_stock::Finite;
using virtual_stock::Point3;
using virtual_stock::Quat4;

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
constexpr float kPi = 3.14159265358979323846f;

Point3 Pt(float x, float y, float z)
{
    return Point3{x, y, z};
}

Quat4 YawDeg(float degrees)
{
    const float radians = degrees * kPi / 180.0f;
    return Quat4{0.0f, std::sin(radians * 0.5f), 0.0f,
        std::cos(radians * 0.5f)};
}

bool PtEqual(Point3 a, Point3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool QuatEqual(Quat4 a, Quat4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
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

    void CheckNear(float actual, float expected, float epsilon,
        const char* message)
    {
        ++checks;
        if (!std::isfinite(actual) || !std::isfinite(expected) ||
            std::fabs(actual - expected) > epsilon)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n  expected: %g\n  actual:   %g\n",
                message, expected, actual);
        }
    }

    int Finish() const
    {
        std::printf("%u checks, %u failures\n", checks, failures);
        return failures ? 1 : 0;
    }
};

PivotInputs MakeInputs()
{
    PivotInputs in{};
    in.primaryAim = Pt(0.30f, 1.40f, -0.20f);
    in.supportAim = Pt(-0.20f, 1.35f, -0.50f);
    in.primaryGripValid = true;
    in.primaryGrip = Pt(0.32f, 1.38f, -0.18f);
    in.supportGripValid = true;
    in.supportGrip = Pt(-0.18f, 1.33f, -0.48f);
    in.productionSupportUsedGrip = true;
    in.productionSupportPosition = Pt(0.10f, 1.20f, -0.60f);
    return in;
}

void TestDefaultSettings(TestContext& test)
{
    const Settings defaults = DefaultSettings();
    test.Check(!defaults.enabled, "lab defaults to disabled");
    test.Check(defaults.anchor == AnchorMode::Production,
        "lab defaults to the Production anchor");
    test.Check(defaults.offhandInfluence == 1.0f,
        "lab defaults to full off-hand influence");
    test.Check(defaults.agreement == two_hand_lab::AgreementMode::LegacyHard,
        "lab defaults to legacy hard agreement");
    test.Check(defaults.softFullAgreement >= 0.36f &&
            defaults.softFullAgreement <= 1.0f,
        "soft full-agreement seed stays inside its clamp band");
    test.Check(defaults.temporal == two_hand_lab::TemporalMode::None,
        "lab defaults to no temporal filtering");
    test.Check(defaults.dampingResponseMs == 150.0f,
        "constant-damping seed is 150 ms");
    test.Check(defaults.adaptiveSlowResponseMs == 400.0f,
        "adaptive slow seed is 400 ms");
    test.Check(defaults.adaptiveFastResponseMs == 50.0f,
        "adaptive fast seed is 50 ms");
    test.Check(defaults.adaptiveFullErrorDeg == 8.0f,
        "adaptive full-error seed is 8 deg");
}

void TestNormalize(TestContext& test)
{
    const Settings defaults = DefaultSettings();

    Settings low{};
    low.offhandInfluence = -1.0f;
    low.softFullAgreement = 0.0f;
    low.dampingResponseMs = 0.0f;
    low.adaptiveSlowResponseMs = 0.0f;
    low.adaptiveFastResponseMs = -5.0f;
    low.adaptiveFullErrorDeg = 0.0f;
    const Settings clampedLow = Normalize(low);
    test.Check(clampedLow.offhandInfluence == 0.0f,
        "influence clamps to zero from below");
    test.Check(clampedLow.softFullAgreement == 0.36f,
        "full agreement clamps to the 0.36 floor");
    test.Check(clampedLow.softFullAgreement - 0.35f >= 0.01f - 1.0e-6f,
        "full agreement keeps min separation above the 0.35 floor");
    test.Check(clampedLow.dampingResponseMs == 20.0f,
        "response clamps to 20 ms from below");
    test.Check(clampedLow.adaptiveFullErrorDeg == 1.0f,
        "full error clamps to 1 deg from below");

    Settings high{};
    high.offhandInfluence = 2.0f;
    high.softFullAgreement = 2.0f;
    high.dampingResponseMs = 5000.0f;
    high.adaptiveSlowResponseMs = 900.0f;
    high.adaptiveFastResponseMs = 800.0f;
    high.adaptiveFullErrorDeg = 100.0f;
    const Settings clampedHigh = Normalize(high);
    test.Check(clampedHigh.offhandInfluence == 1.0f,
        "influence clamps to one from above");
    test.Check(clampedHigh.softFullAgreement == 1.0f,
        "full agreement clamps to one from above");
    test.Check(clampedHigh.dampingResponseMs == 1000.0f,
        "response clamps to 1000 ms from above");
    test.Check(clampedHigh.adaptiveFullErrorDeg == 45.0f,
        "full error clamps to 45 deg from above");

    Settings inverted{};
    inverted.adaptiveSlowResponseMs = 50.0f;
    inverted.adaptiveFastResponseMs = 400.0f;
    const Settings ordered = Normalize(inverted);
    test.Check(ordered.adaptiveSlowResponseMs == 400.0f &&
            ordered.adaptiveFastResponseMs == 50.0f,
        "inverted slow/fast responses are swapped into slow>=fast order");
    test.Check(ordered.adaptiveSlowResponseMs >=
            ordered.adaptiveFastResponseMs,
        "normalized responses are well ordered");

    Settings nonFinite{};
    nonFinite.offhandInfluence = kNan;
    nonFinite.softFullAgreement = kInf;
    nonFinite.dampingResponseMs = kNan;
    nonFinite.adaptiveSlowResponseMs = -kInf;
    nonFinite.adaptiveFastResponseMs = kNan;
    nonFinite.adaptiveFullErrorDeg = kInf;
    const Settings repaired = Normalize(nonFinite);
    test.Check(repaired.offhandInfluence == defaults.offhandInfluence,
        "non-finite influence falls back to the default");
    test.Check(repaired.softFullAgreement == defaults.softFullAgreement,
        "non-finite full agreement falls back to the default");
    test.Check(repaired.dampingResponseMs == defaults.dampingResponseMs,
        "non-finite response falls back to the default");
    test.Check(repaired.adaptiveSlowResponseMs ==
            defaults.adaptiveSlowResponseMs,
        "non-finite slow response falls back to the default");
    test.Check(repaired.adaptiveFastResponseMs ==
            defaults.adaptiveFastResponseMs,
        "non-finite fast response falls back to the default");
    test.Check(repaired.adaptiveFullErrorDeg == defaults.adaptiveFullErrorDeg,
        "non-finite full error falls back to the default");
    test.Check(std::isfinite(repaired.offhandInfluence) &&
            std::isfinite(repaired.softFullAgreement) &&
            std::isfinite(repaired.dampingResponseMs) &&
            std::isfinite(repaired.adaptiveSlowResponseMs) &&
            std::isfinite(repaired.adaptiveFastResponseMs) &&
            std::isfinite(repaired.adaptiveFullErrorDeg),
        "every normalized output is finite");
    test.Check(repaired.adaptiveFullErrorDeg > 0.0f,
        "normalized full error is positive");
}

void TestResolverProduction(TestContext& test)
{
    const PivotInputs in = MakeInputs();
    for (const bool usedGrip : {true, false})
    {
        PivotInputs flagged = in;
        flagged.productionSupportUsedGrip = usedGrip;
        const auto resolved = ResolvePivots(flagged, AnchorMode::Production);
        test.Check(resolved.valid, "Production resolves valid");
        test.Check(resolved.requested == AnchorMode::Production,
            "Production echoes the request");
        test.Check(resolved.resolved == ResolvedAnchor::Production,
            "Production resolves to Production");
        test.Check(PtEqual(resolved.primary, in.primaryAim),
            "Production primary is the primary aim");
        test.Check(PtEqual(resolved.support, in.productionSupportPosition),
            "Production support is the production selection");
        test.Check(!resolved.fellBack &&
                resolved.fallbackReason == FallbackReason::None,
            "Production never falls back");
    }

    // Pass-through ignores every grip field, including non-finite grips.
    PivotInputs poisoned = in;
    poisoned.primaryGrip = Pt(kNan, 0.0f, 0.0f);
    poisoned.supportGrip = Pt(0.0f, kNan, 0.0f);
    poisoned.supportAim = Pt(0.0f, 0.0f, kInf);
    const auto ignored = ResolvePivots(poisoned, AnchorMode::Production);
    test.Check(ignored.valid &&
            PtEqual(ignored.support, in.productionSupportPosition),
        "Production ignores unused non-finite positions");

    PivotInputs badSupport = in;
    badSupport.productionSupportPosition = Pt(kNan, 0.0f, 0.0f);
    const auto invalid = ResolvePivots(badSupport, AnchorMode::Production);
    test.Check(!invalid.valid &&
            invalid.fallbackReason == FallbackReason::InvalidInput,
        "Production with a non-finite production position is InvalidInput");
}

void TestResolverAA(TestContext& test)
{
    const PivotInputs in = MakeInputs();
    const auto resolved = ResolvePivots(in, AnchorMode::AA);
    test.Check(resolved.valid, "AA resolves valid");
    test.Check(resolved.requested == AnchorMode::AA, "AA echoes the request");
    test.Check(resolved.resolved == ResolvedAnchor::AA, "AA resolves to AA");
    test.Check(PtEqual(resolved.primary, in.primaryAim) &&
            PtEqual(resolved.support, in.supportAim),
        "AA uses both aim pivots");
    test.Check(!resolved.fellBack &&
            resolved.fallbackReason == FallbackReason::None,
        "AA never falls back");

    PivotInputs badPrimary = in;
    badPrimary.primaryAim = Pt(kNan, 0.0f, 0.0f);
    const auto invalid = ResolvePivots(badPrimary, AnchorMode::AA);
    test.Check(!invalid.valid &&
            invalid.fallbackReason == FallbackReason::InvalidInput,
        "AA with a non-finite primary aim is InvalidInput");
}

void TestResolverAG(TestContext& test)
{
    const PivotInputs in = MakeInputs();
    const auto resolved = ResolvePivots(in, AnchorMode::AG);
    test.Check(resolved.valid, "AG resolves valid with a live support grip");
    test.Check(resolved.resolved == ResolvedAnchor::AG, "AG resolves to AG");
    test.Check(PtEqual(resolved.primary, in.primaryAim) &&
            PtEqual(resolved.support, in.supportGrip),
        "AG pairs the primary aim with the support grip");
    test.Check(!resolved.fellBack &&
            resolved.fallbackReason == FallbackReason::None,
        "AG with a live grip does not fall back");

    PivotInputs missing = in;
    missing.supportGripValid = false;
    const auto fallback = ResolvePivots(missing, AnchorMode::AG);
    test.Check(fallback.valid, "AG without a support grip stays valid");
    test.Check(fallback.resolved == ResolvedAnchor::AA,
        "AG without a support grip falls back to AA");
    test.Check(PtEqual(fallback.primary, in.primaryAim) &&
            PtEqual(fallback.support, in.supportAim),
        "AG fallback uses both aim pivots");
    test.Check(fallback.fellBack &&
            fallback.fallbackReason == FallbackReason::MissingSupportGrip,
        "AG fallback names the missing support grip");

    // A set validity flag commits to the grip path: a non-finite grip there
    // fails closed instead of silently downgrading.
    PivotInputs badGrip = in;
    badGrip.supportGrip = Pt(0.0f, kNan, 0.0f);
    const auto invalid = ResolvePivots(badGrip, AnchorMode::AG);
    test.Check(!invalid.valid &&
            invalid.fallbackReason == FallbackReason::InvalidInput,
        "AG with a flagged but non-finite support grip is InvalidInput");

    PivotInputs badAim = in;
    badAim.supportAim = Pt(0.0f, 0.0f, kNan);
    badAim.supportGripValid = false;
    const auto invalidFallback =
        ResolvePivots(badAim, AnchorMode::AG);
    test.Check(!invalidFallback.valid &&
            invalidFallback.fallbackReason == FallbackReason::InvalidInput,
        "AG fallback with a non-finite support aim is InvalidInput");
}

void TestResolverGA(TestContext& test)
{
    const PivotInputs in = MakeInputs();
    const auto resolved = ResolvePivots(in, AnchorMode::GA);
    test.Check(resolved.valid, "GA resolves valid with a live primary grip");
    test.Check(resolved.resolved == ResolvedAnchor::GA, "GA resolves to GA");
    test.Check(PtEqual(resolved.primary, in.primaryGrip) &&
            PtEqual(resolved.support, in.supportAim),
        "GA pairs the primary grip with the support aim");
    test.Check(!resolved.fellBack &&
            resolved.fallbackReason == FallbackReason::None,
        "GA with a live grip does not fall back");

    PivotInputs missing = in;
    missing.primaryGripValid = false;
    const auto fallback = ResolvePivots(missing, AnchorMode::GA);
    test.Check(fallback.valid, "GA without a primary grip stays valid");
    test.Check(fallback.resolved == ResolvedAnchor::AA,
        "GA without a primary grip falls back to AA");
    test.Check(PtEqual(fallback.primary, in.primaryAim) &&
            PtEqual(fallback.support, in.supportAim),
        "GA fallback uses both aim pivots");
    test.Check(fallback.fellBack &&
            fallback.fallbackReason == FallbackReason::MissingPrimaryGrip,
        "GA fallback names the missing primary grip");

    PivotInputs badGrip = in;
    badGrip.primaryGrip = Pt(kInf, 0.0f, 0.0f);
    const auto invalid = ResolvePivots(badGrip, AnchorMode::GA);
    test.Check(!invalid.valid &&
            invalid.fallbackReason == FallbackReason::InvalidInput,
        "GA with a flagged but non-finite primary grip is InvalidInput");
}

void TestResolverGG(TestContext& test)
{
    const PivotInputs in = MakeInputs();
    const auto resolved = ResolvePivots(in, AnchorMode::GG);
    test.Check(resolved.valid, "GG resolves valid with both grips live");
    test.Check(resolved.requested == AnchorMode::GG, "GG echoes the request");
    test.Check(resolved.resolved == ResolvedAnchor::GG, "GG resolves to GG");
    test.Check(PtEqual(resolved.primary, in.primaryGrip) &&
            PtEqual(resolved.support, in.supportGrip),
        "GG uses both grip pivots");
    test.Check(!resolved.fellBack &&
            resolved.fallbackReason == FallbackReason::None,
        "GG with live grips does not fall back");

    PivotInputs missingPrimary = in;
    missingPrimary.primaryGripValid = false;
    const auto primaryFallback = ResolvePivots(missingPrimary, AnchorMode::GG);
    test.Check(primaryFallback.valid, "GG without a primary grip stays valid");
    test.Check(primaryFallback.resolved == ResolvedAnchor::AA,
        "GG without a primary grip falls back to AA, never GA");
    test.Check(primaryFallback.resolved != ResolvedAnchor::GA &&
            primaryFallback.resolved != ResolvedAnchor::AG,
        "GG never silently produces a single-sided grip anchor");
    test.Check(primaryFallback.fellBack &&
            primaryFallback.fallbackReason ==
                FallbackReason::MissingPrimaryGrip,
        "GG fallback names the missing primary grip");

    PivotInputs missingSupport = in;
    missingSupport.supportGripValid = false;
    const auto supportFallback = ResolvePivots(missingSupport, AnchorMode::GG);
    test.Check(supportFallback.valid, "GG without a support grip stays valid");
    test.Check(supportFallback.resolved == ResolvedAnchor::AA,
        "GG without a support grip falls back to AA, never AG");
    test.Check(supportFallback.fellBack &&
            supportFallback.fallbackReason ==
                FallbackReason::MissingSupportGrip,
        "GG fallback names the missing support grip");

    PivotInputs missingBoth = in;
    missingBoth.primaryGripValid = false;
    missingBoth.supportGripValid = false;
    const auto bothFallback = ResolvePivots(missingBoth, AnchorMode::GG);
    test.Check(bothFallback.valid, "GG without either grip stays valid");
    test.Check(bothFallback.resolved == ResolvedAnchor::AA &&
            bothFallback.fellBack,
        "GG without either grip falls back to AA");
    test.Check(
        bothFallback.fallbackReason == FallbackReason::MissingPrimaryGrip,
        "doubly-missing GG reports the primary side first");

    PivotInputs badGrip = in;
    badGrip.primaryGrip = Pt(0.0f, 0.0f, kNan);
    const auto invalid = ResolvePivots(badGrip, AnchorMode::GG);
    test.Check(!invalid.valid &&
            invalid.fallbackReason == FallbackReason::InvalidInput,
        "GG with a flagged but non-finite grip is InvalidInput");

    // GG emits grips only, so unused non-finite aims do not poison it.
    PivotInputs badAims = in;
    badAims.primaryAim = Pt(kNan, 0.0f, 0.0f);
    badAims.supportAim = Pt(0.0f, kNan, 0.0f);
    const auto gripsOnly = ResolvePivots(badAims, AnchorMode::GG);
    test.Check(gripsOnly.valid &&
            gripsOnly.resolved == ResolvedAnchor::GG,
        "GG with live grips ignores unused non-finite aims");

    // A fallback with no finite aim line left is a hard failure.
    PivotInputs noAims = missingBoth;
    noAims.supportAim = Pt(0.0f, 0.0f, kInf);
    const auto invalidFallback = ResolvePivots(noAims, AnchorMode::GG);
    test.Check(!invalidFallback.valid &&
            invalidFallback.fallbackReason == FallbackReason::InvalidInput,
        "GG fallback with a non-finite aim line is InvalidInput");
}

void TestSoftAuthority(TestContext& test)
{
    constexpr float kFull = 0.9f;
    test.Check(SoftAuthorityConfidence(0.0f, kFull) == 0.0f,
        "zero agreement yields zero confidence");
    test.Check(SoftAuthorityConfidence(0.34f, kFull) == 0.0f,
        "below-floor agreement yields zero confidence");
    test.Check(SoftAuthorityConfidence(0.35f, kFull) == 0.0f,
        "at-floor agreement yields zero confidence");
    test.Check(SoftAuthorityConfidence(kFull, kFull) == 1.0f,
        "full agreement yields full confidence");
    test.Check(SoftAuthorityConfidence(1.0f, kFull) == 1.0f,
        "above-full agreement yields full confidence");
    test.Check(SoftAuthorityConfidence(1.5f, kFull) == 1.0f,
        "out-of-range high agreement clamps to full confidence");
    test.CheckNear(SoftAuthorityConfidence(0.625f, kFull), 0.5f, 1.0e-5f,
        "band midpoint yields half confidence");

    float previous = 0.0f;
    for (int sample = 0; sample <= 10; ++sample)
    {
        const float agreement =
            0.36f + (0.89f - 0.36f) * sample / 10.0f;
        const float confidence = SoftAuthorityConfidence(agreement, kFull);
        test.Check(confidence > 0.0f && confidence < 1.0f,
            "open-band agreement yields partial confidence");
        test.Check(confidence > previous,
            "confidence increases monotonically across the band");
        previous = confidence;
    }

    test.Check(SoftAuthorityConfidence(0.9f, 0.35f) == 0.0f,
        "full agreement at the floor guards to zero");
    test.Check(SoftAuthorityConfidence(0.9f, 0.2f) == 0.0f,
        "full agreement below the floor guards to zero");
    test.Check(SoftAuthorityConfidence(0.9f, -1.0f) == 0.0f,
        "negative full agreement guards to zero");
    test.Check(SoftAuthorityConfidence(kNan, kFull) == 0.0f,
        "non-finite agreement guards to zero");
    test.Check(SoftAuthorityConfidence(0.5f, kNan) == 0.0f,
        "non-finite full agreement guards to zero");
    test.Check(SoftAuthorityConfidence(kInf, kFull) == 0.0f,
        "infinite agreement guards to zero");
    test.CheckNear(SoftAuthorityConfidence(0.355f, 0.36f), 0.5f, 1.0e-5f,
        "minimum-separation band still blends through its midpoint");
}

void TestShortestArcAngle(TestContext& test)
{
    const Quat4 identity{};
    test.CheckNear(ShortestArcAngleDeg(identity, identity), 0.0f, 1.0e-6f,
        "equal orientations span zero angle");
    test.CheckNear(
        ShortestArcAngleDeg(identity, YawDeg(90.0f)), 90.0f, 1.0e-2f,
        "quarter turn spans 90 deg");
    test.CheckNear(
        ShortestArcAngleDeg(identity, YawDeg(180.0f)), 180.0f, 1.0e-2f,
        "half turn spans 180 deg");
    test.CheckNear(
        ShortestArcAngleDeg(YawDeg(45.0f), YawDeg(45.0f)), 0.0f, 1.0e-4f,
        "equal non-trivial orientations span zero angle");
    const float forward = ShortestArcAngleDeg(YawDeg(30.0f), YawDeg(80.0f));
    const float backward = ShortestArcAngleDeg(YawDeg(80.0f), YawDeg(30.0f));
    test.CheckNear(forward, 50.0f, 1.0e-2f, "offset yaw spans its difference");
    test.CheckNear(backward, forward, 1.0e-4f, "angle is symmetric");
    test.CheckNear(ShortestArcAngleDeg(YawDeg(60.0f),
                        Quat4{0.0f, -std::sin(60.0f * kPi / 360.0f), 0.0f,
                            -std::cos(60.0f * kPi / 360.0f)}),
        0.0f, 1.0e-4f, "double cover reads as zero angle");
    test.CheckNear(ShortestArcAngleDeg(identity,
                        Quat4{0.0f, 2.0f * std::sin(kPi / 4.0f), 0.0f,
                            2.0f * std::cos(kPi / 4.0f)}),
        90.0f, 1.0e-2f, "non-unit inputs normalize before measuring");
    const Quat4 degenerate{0.0f, 0.0f, 0.0f, 0.0f};
    test.Check(!std::isfinite(ShortestArcAngleDeg(degenerate, degenerate)),
        "non-normalizable inputs propagate non-finite (guarded by the steps)");
}

void TestConstantDamping(TestContext& test)
{
    const Quat4 previous{};
    const Quat4 target = YawDeg(90.0f);

    test.Check(QuatEqual(ConstantDampingStep(previous, target, 0.0f, 1.0f / 60.0f), target),
        "zero response answers the exact target");
    test.Check(QuatEqual(ConstantDampingStep(previous, target, -10.0f, 1.0f / 60.0f), target),
        "negative response answers the exact target");
    test.Check(QuatEqual(ConstantDampingStep(previous, target, 150.0f, 0.0f), previous),
        "zero dt holds the previous pose");
    test.Check(QuatEqual(ConstantDampingStep(previous, target, 150.0f, -0.01f), previous),
        "negative dt holds the previous pose");
    test.Check(QuatEqual(ConstantDampingStep(previous, target, 150.0f, kNan), previous),
        "non-finite dt holds the previous pose");
    test.Check(QuatEqual(ConstantDampingStep(previous, target, kNan, 1.0f / 60.0f), target),
        "non-finite response answers the target");
    test.Check(QuatEqual(ConstantDampingStep(previous, target, kInf, 1.0f / 60.0f), target),
        "infinite response answers the target");

    Quat4 state = previous;
    float previousAngle = ShortestArcAngleDeg(state, target);
    test.CheckNear(previousAngle, 90.0f, 0.05f, "sequence starts at 90 deg");
    for (int frame = 0; frame < 600; ++frame)
    {
        state = ConstantDampingStep(state, target, 150.0f, 1.0f / 60.0f);
        test.Check(Finite(state), "converging step stays finite");
        const float angle = ShortestArcAngleDeg(state, target);
        test.Check(angle <= previousAngle + 1.0e-3f,
            "convergence never increases the remaining angle");
        previousAngle = angle;
    }
    test.Check(previousAngle < 0.5f, "frame sequence converges to the target");

    Quat4 fine = previous;
    for (int frame = 0; frame < 240; ++frame)
        fine = ConstantDampingStep(fine, target, 150.0f, 1.0f / 240.0f);
    Quat4 coarse = previous;
    for (int frame = 0; frame < 60; ++frame)
        coarse = ConstantDampingStep(coarse, target, 150.0f, 1.0f / 60.0f);
    test.Check(Finite(fine) && Finite(coarse), "both rates stay finite");
    test.CheckNear(ShortestArcAngleDeg(fine, target),
        ShortestArcAngleDeg(coarse, target), 0.1f,
        "small and large steps agree over the same elapsed time");

    const Quat4 huge{1.0e20f, 0.0f, 0.0f, 0.0f};
    const Quat4 degenerate{0.0f, 0.0f, 0.0f, 0.0f};
    test.Check(Finite(ConstantDampingStep(huge, target, 150.0f, 1.0f / 60.0f)),
        "huge-but-valid previous stays finite");
    test.Check(Finite(ConstantDampingStep(previous, huge, 150.0f, 1.0f / 60.0f)),
        "huge-but-valid target stays finite");
    test.Check(Finite(ConstantDampingStep(degenerate, target, 150.0f, 1.0f / 60.0f)),
        "degenerate previous stays finite");
    test.Check(Finite(ConstantDampingStep(previous, degenerate, 150.0f, 1.0f / 60.0f)),
        "degenerate target stays finite");
    test.Check(Finite(ConstantDampingStep(previous, target, 1.0e30f, 1.0f / 60.0f)),
        "extreme response stays finite");
    test.Check(Finite(ConstantDampingStep(previous, target, 150.0f, 1.0e30f)),
        "extreme dt stays finite");
    test.Check(Finite(ConstantDampingStep(
                    Quat4{kNan, 0.0f, 0.0f, 1.0f}, target, 150.0f, 1.0f / 60.0f)),
        "non-finite previous fails closed to finite");
    test.Check(Finite(ConstantDampingStep(
                    previous, Quat4{0.0f, kNan, 0.0f, 1.0f}, 150.0f, 1.0f / 60.0f)),
        "non-finite target fails closed to finite");
    test.Check(Finite(ConstantDampingStep(
                    Quat4{kNan, 0.0f, 0.0f, kNan}, Quat4{0.0f, kNan, 0.0f, kNan},
                    kNan, kNan)),
        "fully non-finite inputs still answer finite");
}

void TestAdaptiveDamping(TestContext& test)
{
    const Quat4 previous{};
    constexpr float kSlowMs = 400.0f;
    constexpr float kFastMs = 50.0f;
    constexpr float kFullErrorDeg = 8.0f;
    constexpr float kDt = 1.0f / 60.0f;

    const Quat4 smallTarget = YawDeg(2.0f);
    const Quat4 adaptiveSmall =
        AdaptiveDampingStep(previous, smallTarget, kSlowMs, kFastMs,
            kFullErrorDeg, kDt);
    const Quat4 slowSmall =
        ConstantDampingStep(previous, smallTarget, kSlowMs, kDt);
    const Quat4 fastSmall =
        ConstantDampingStep(previous, smallTarget, kFastMs, kDt);
    const float movedAdaptive = ShortestArcAngleDeg(adaptiveSmall, previous);
    const float movedSlow = ShortestArcAngleDeg(slowSmall, previous);
    const float movedFast = ShortestArcAngleDeg(fastSmall, previous);
    test.Check(movedAdaptive > movedSlow,
        "small error answers faster than the pure slow constant");
    test.Check(movedAdaptive < movedFast,
        "small error answers slower than the pure fast constant");

    const Quat4 largeTarget = YawDeg(30.0f);
    const Quat4 adaptiveLarge =
        AdaptiveDampingStep(previous, largeTarget, kSlowMs, kFastMs,
            kFullErrorDeg, kDt);
    const Quat4 fastLarge =
        ConstantDampingStep(previous, largeTarget, kFastMs, kDt);
    test.CheckNear(ShortestArcAngleDeg(adaptiveLarge, largeTarget),
        ShortestArcAngleDeg(fastLarge, largeTarget), 1.0e-3f,
        "error past full error uses the fast response");

    const Quat4 fullTarget = YawDeg(kFullErrorDeg);
    const Quat4 adaptiveFull =
        AdaptiveDampingStep(previous, fullTarget, kSlowMs, kFastMs,
            kFullErrorDeg, kDt);
    const Quat4 fastFull =
        ConstantDampingStep(previous, fullTarget, kFastMs, kDt);
    test.CheckNear(ShortestArcAngleDeg(adaptiveFull, fullTarget),
        ShortestArcAngleDeg(fastFull, fullTarget), 1.0e-3f,
        "error at full error uses the fast response");

    float movedPrevious = -1.0f;
    for (const float error : {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 45.0f})
    {
        const Quat4 stepped = AdaptiveDampingStep(previous, YawDeg(error),
            kSlowMs, kFastMs, kFullErrorDeg, kDt);
        test.Check(Finite(stepped), "adaptive sweep stays finite");
        const float moved = ShortestArcAngleDeg(stepped, previous);
        test.Check(moved + 1.0e-4f >= movedPrevious,
            "response grows monotonically with error");
        movedPrevious = moved;
    }

    Settings inverted{};
    inverted.adaptiveSlowResponseMs = 50.0f;
    inverted.adaptiveFastResponseMs = 400.0f;
    const Settings ordered = Normalize(inverted);
    const Quat4 orderedStep = AdaptiveDampingStep(previous, YawDeg(20.0f),
        ordered.adaptiveSlowResponseMs, ordered.adaptiveFastResponseMs,
        ordered.adaptiveFullErrorDeg, kDt);
    test.Check(Finite(orderedStep), "normalized slow>=fast step stays finite");
    const Quat4 rawInvertedStep = AdaptiveDampingStep(previous, YawDeg(20.0f),
        50.0f, 400.0f, ordered.adaptiveFullErrorDeg, kDt);
    test.Check(Finite(rawInvertedStep), "raw inverted constants stay finite");

    test.Check(QuatEqual(
                    AdaptiveDampingStep(previous, smallTarget, kSlowMs,
                        kFastMs, kFullErrorDeg, 0.0f),
                    previous),
        "adaptive zero dt holds the previous pose");
    test.Check(QuatEqual(
                    AdaptiveDampingStep(previous, smallTarget, kSlowMs,
                        kFastMs, kFullErrorDeg, kNan),
                    previous),
        "adaptive non-finite dt holds the previous pose");
    test.Check(Finite(AdaptiveDampingStep(Quat4{}, Quat4{}, kSlowMs, kFastMs,
                        kFullErrorDeg, kDt)),
        "adaptive degenerate quats stay finite");
    test.Check(Finite(AdaptiveDampingStep(previous, smallTarget, kSlowMs,
                        kFastMs, kNan, kDt)),
        "adaptive non-finite full error stays finite");
}

void TestQuickPresets(TestContext& test)
{
    const Settings defaults = DefaultSettings();

    const struct Expectation
    {
        QuickPreset preset;
        AnchorMode anchor;
        float influence;
    } kExpectations[] = {
        {QuickPreset::Baseline, AnchorMode::Production, 1.0f},
        {QuickPreset::AA100, AnchorMode::AA, 1.0f},
        {QuickPreset::AG100, AnchorMode::AG, 1.0f},
        {QuickPreset::GG100, AnchorMode::GG, 1.0f},
        {QuickPreset::GG75, AnchorMode::GG, 0.75f},
        {QuickPreset::GG50, AnchorMode::GG, 0.50f},
    };
    for (const Expectation& expected : kExpectations)
    {
        const Settings built = QuickPresetSettings(expected.preset);
        test.Check(built.enabled, "preset is enabled");
        test.Check(built.anchor == expected.anchor,
            "preset selects its anchor");
        test.Check(built.offhandInfluence == expected.influence,
            "preset selects its influence");
        test.Check(built.agreement == two_hand_lab::AgreementMode::LegacyHard,
            "preset resets agreement to Legacy hard");
        test.Check(built.temporal == two_hand_lab::TemporalMode::None,
            "preset resets temporal to None");
        test.Check(built.softFullAgreement == defaults.softFullAgreement,
            "preset keeps the default soft full-agreement seed");
        test.Check(built.dampingResponseMs == defaults.dampingResponseMs,
            "preset keeps the default damping response seed");
        test.Check(built.adaptiveSlowResponseMs ==
                defaults.adaptiveSlowResponseMs,
            "preset keeps the default adaptive slow seed");
        test.Check(built.adaptiveFastResponseMs ==
                defaults.adaptiveFastResponseMs,
            "preset keeps the default adaptive fast seed");
        test.Check(built.adaptiveFullErrorDeg ==
                defaults.adaptiveFullErrorDeg,
            "preset keeps the default adaptive full-error seed");
    }

    // The builder takes no live state, so a contaminated agreement/temporal
    // selection cannot leak through a preset application: the local below
    // models the "previous settings" the old read-modify-write preset kept,
    // and the builder has no parameter that could receive it.
    Settings contaminated = DefaultSettings();
    contaminated.enabled = true;
    contaminated.anchor = AnchorMode::AA;
    contaminated.offhandInfluence = 0.25f;
    contaminated.agreement = two_hand_lab::AgreementMode::SoftAuthority;
    contaminated.softFullAgreement = 0.5f;
    contaminated.temporal = two_hand_lab::TemporalMode::AdaptiveDamping;
    contaminated.dampingResponseMs = 900.0f;
    contaminated.adaptiveSlowResponseMs = 800.0f;
    contaminated.adaptiveFastResponseMs = 700.0f;
    contaminated.adaptiveFullErrorDeg = 40.0f;
    (void)contaminated;

    const Settings recovered = QuickPresetSettings(QuickPreset::GG50);
    test.Check(recovered.enabled, "GG50 is enabled");
    test.Check(recovered.anchor == AnchorMode::GG, "GG50 selects GG");
    test.Check(recovered.offhandInfluence == 0.50f,
        "GG50 selects half influence");
    test.Check(recovered.agreement == two_hand_lab::AgreementMode::LegacyHard,
        "GG50 drops the contaminated agreement mode");
    test.Check(recovered.temporal == two_hand_lab::TemporalMode::None,
        "GG50 drops the contaminated temporal mode");
    test.Check(recovered.softFullAgreement == defaults.softFullAgreement,
        "GG50 drops the contaminated soft full-agreement seed");
    test.Check(recovered.dampingResponseMs == defaults.dampingResponseMs &&
            recovered.adaptiveSlowResponseMs ==
                defaults.adaptiveSlowResponseMs &&
            recovered.adaptiveFastResponseMs ==
                defaults.adaptiveFastResponseMs &&
            recovered.adaptiveFullErrorDeg ==
                defaults.adaptiveFullErrorDeg,
        "GG50 drops the contaminated damping seeds");

    const Settings baseline = QuickPresetSettings(QuickPreset::Baseline);
    test.Check(baseline.enabled &&
            baseline.anchor == AnchorMode::Production &&
            baseline.offhandInfluence == 1.0f &&
            baseline.agreement == two_hand_lab::AgreementMode::LegacyHard &&
            baseline.temporal == two_hand_lab::TemporalMode::None &&
            baseline.softFullAgreement == defaults.softFullAgreement &&
            baseline.dampingResponseMs == defaults.dampingResponseMs &&
            baseline.adaptiveSlowResponseMs ==
                defaults.adaptiveSlowResponseMs &&
            baseline.adaptiveFastResponseMs ==
                defaults.adaptiveFastResponseMs &&
            baseline.adaptiveFullErrorDeg ==
                defaults.adaptiveFullErrorDeg,
        "Baseline is the all-clean enabled Production/1.00 state");
}

void TestDiagnosticsPod(TestContext& test)
{
    static_assert(std::is_standard_layout<Diagnostics>::value,
        "diagnostics stay a plain standard-layout POD");
    static_assert(std::is_default_constructible<Diagnostics>::value,
        "diagnostics stay default constructible");
    const Diagnostics fresh{};
    test.Check(!fresh.labEnabledStored && !fresh.labActiveThisFrame,
        "diagnostics default to an inactive lab");
    test.Check(fresh.requestedAnchor == AnchorMode::Production &&
            fresh.resolvedAnchor == ResolvedAnchor::Production &&
            fresh.fallback == FallbackReason::None,
        "diagnostics default to the Production anchor contract");
    Diagnostics stored{};
    stored.labEnabledStored = true;
    stored.labActiveThisFrame = true;
    stored.requestedAnchor = AnchorMode::GG;
    stored.resolvedAnchor = ResolvedAnchor::AA;
    stored.fallback = FallbackReason::MissingPrimaryGrip;
    stored.confidence = 0.5f;
    stored.errorDeg = 3.0f;
    test.Check(stored.labEnabledStored && stored.labActiveThisFrame &&
            stored.requestedAnchor == AnchorMode::GG &&
            stored.resolvedAnchor == ResolvedAnchor::AA &&
            stored.fallback == FallbackReason::MissingPrimaryGrip &&
            stored.confidence == 0.5f && stored.errorDeg == 3.0f,
        "diagnostics carry the tranche-2 contract shape");
}

} // namespace

int main()
{
    TestContext test;
    TestDefaultSettings(test);
    TestNormalize(test);
    TestResolverProduction(test);
    TestResolverAA(test);
    TestResolverAG(test);
    TestResolverGA(test);
    TestResolverGG(test);
    TestSoftAuthority(test);
    TestShortestArcAngle(test);
    TestConstantDamping(test);
    TestAdaptiveDamping(test);
    TestQuickPresets(test);
    TestDiagnosticsPod(test);
    return test.Finish();
}
