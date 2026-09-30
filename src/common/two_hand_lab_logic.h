#pragma once
// Two-Handed Lab ("Two-Hand Lab"): dependency-light PURE logic module.
//
// Header-only experimental rig for two-hand anchor selection (the aim/grip
// pivot matrix), soft off-hand authority, and temporal damping of the emitted
// aim orientation. Landed architecture: this pure module is shared by the
// runtime solver integration (the two_hand_lab_runtime store in
// src/dll/vr.cpp and the Lab branch of src/dll/virtual_stock_aim.inl), the
// temporal fragment (src/dll/two_hand_lab_temporal_runtime.inl), the Lab menu
// page (src/dll/two_hand_lab_menu.inl, settings only), and the unit tests
// (tests/two_hand_lab_logic_tests.cpp plus the solver/temporal suites).
//
// Purity contract: namespace two_hand_lab only; no OpenXR/ImGui types, no
// globals, no logging, no file I/O. Reuses the existing pure maths from
// src/common/virtual_stock_logic.h (Point3, Quat4, Finite, Dot,
// TryNormalizeQuaternion) and src/common/virtual_stock_aim_continuity.h
// (shortest-arc SlerpQuat4Shortest, Quat4AngleDegrees) rather than cloning
// maths.
//
// Anchor vocabulary: A = aim pose, G = grip pose. AnchorMode::Production is a
// pass-through of the existing production support selection, never an
// emulation of it.
#include <algorithm>
#include <cmath>
#include <limits>

#include "virtual_stock_logic.h"
#include "virtual_stock_aim_continuity.h"

namespace two_hand_lab
{

// Agreement floor shared with the legacy controller->controller acceptance
// rule (virtual_stock::TryBuildAcceptedSupportDirection rejects below 0.35).
inline constexpr float kAgreementFloor = 0.35f;

// Soft-authority full-agreement bounds. The minimum keeps a guaranteed 0.01
// separation above the 0.35 floor so the confidence band never degenerates.
inline constexpr float kSoftFullAgreementMinimum = 0.36f;
inline constexpr float kSoftFullAgreementMaximum = 1.0f;

inline constexpr float kOffhandInfluenceMinimum = 0.0f;
inline constexpr float kOffhandInfluenceMaximum = 1.0f;

inline constexpr float kDampingResponseMinimumMs = 20.0f;
inline constexpr float kDampingResponseMaximumMs = 1000.0f;

inline constexpr float kAdaptiveFullErrorMinimumDeg = 1.0f;
inline constexpr float kAdaptiveFullErrorMaximumDeg = 45.0f;

// Below this time constant the damper is treated as an instant response.
inline constexpr float kDampingTauEpsilonSeconds = 1.0e-6f;

enum class AnchorMode
{
    Production,
    AA,
    AG,
    GA,
    GG,
};

enum class ResolvedAnchor
{
    Production,
    AA,
    AG,
    GA,
    GG,
};

enum class FallbackReason
{
    None,
    MissingPrimaryGrip,
    MissingSupportGrip,
    InvalidInput,
};

enum class AgreementMode
{
    LegacyHard,
    SoftAuthority,
};

enum class TemporalMode
{
    None,
    Continuity200ms,
    ConstantDamping,
    AdaptiveDamping,
};

// Ordinal 1 belonged to the Lab's former duplicate continuity experiment. Keep
// the value readable for runtime-only snapshots/config compatibility, but the
// product latch-transition layer is now its sole owner.
inline TemporalMode NormalizeTemporalMode(int ordinal) noexcept
{
    switch (ordinal)
    {
    case static_cast<int>(TemporalMode::ConstantDamping):
        return TemporalMode::ConstantDamping;
    case static_cast<int>(TemporalMode::AdaptiveDamping):
        return TemporalMode::AdaptiveDamping;
    case static_cast<int>(TemporalMode::None):
    case static_cast<int>(TemporalMode::Continuity200ms):
    default:
        return TemporalMode::None;
    }
}

struct Settings
{
    bool enabled = false;
    AnchorMode anchor = AnchorMode::Production;
    float offhandInfluence = 1.0f;
    AgreementMode agreement = AgreementMode::LegacyHard;
    float softFullAgreement = 0.9f;
    TemporalMode temporal = TemporalMode::None;
    // Temporal damping seeds. PROVISIONAL EXPERIMENTAL VALUES for lab
    // exploration only, not product recommendations. The Lab reads these
    // only while explicitly enabled; Normalize() keeps every seed
    // finite and ordered (slow >= fast) regardless of operator input.
    float dampingResponseMs = 150.0f;
    float adaptiveSlowResponseMs = 400.0f;
    float adaptiveFastResponseMs = 50.0f;
    float adaptiveFullErrorDeg = 8.0f;
};

inline Settings DefaultSettings() noexcept
{
    Settings out;
    out.enabled = false;
    out.anchor = AnchorMode::Production;
    out.offhandInfluence = 1.0f;
    out.agreement = AgreementMode::LegacyHard;
    out.softFullAgreement = 0.9f;
    out.temporal = TemporalMode::None;
    out.dampingResponseMs = 150.0f;
    out.adaptiveSlowResponseMs = 400.0f;
    out.adaptiveFastResponseMs = 50.0f;
    out.adaptiveFullErrorDeg = 8.0f;
    return out;
}

inline float NormalizeOrDefault(
    float value, float minimum, float maximum, float fallback) noexcept
{
    if (!std::isfinite(value))
        return fallback;
    return std::clamp(value, minimum, maximum);
}

inline Settings Normalize(const Settings& in) noexcept
{
    const Settings defaults = DefaultSettings();
    Settings out = in;
    out.offhandInfluence = NormalizeOrDefault(in.offhandInfluence,
        kOffhandInfluenceMinimum, kOffhandInfluenceMaximum,
        defaults.offhandInfluence);
    out.softFullAgreement = NormalizeOrDefault(in.softFullAgreement,
        kSoftFullAgreementMinimum, kSoftFullAgreementMaximum,
        defaults.softFullAgreement);
    out.dampingResponseMs = NormalizeOrDefault(in.dampingResponseMs,
        kDampingResponseMinimumMs, kDampingResponseMaximumMs,
        defaults.dampingResponseMs);
    out.adaptiveSlowResponseMs = NormalizeOrDefault(
        in.adaptiveSlowResponseMs,
        kDampingResponseMinimumMs, kDampingResponseMaximumMs,
        defaults.adaptiveSlowResponseMs);
    out.adaptiveFastResponseMs = NormalizeOrDefault(
        in.adaptiveFastResponseMs,
        kDampingResponseMinimumMs, kDampingResponseMaximumMs,
        defaults.adaptiveFastResponseMs);
    out.adaptiveFullErrorDeg = NormalizeOrDefault(in.adaptiveFullErrorDeg,
        kAdaptiveFullErrorMinimumDeg, kAdaptiveFullErrorMaximumDeg,
        defaults.adaptiveFullErrorDeg);
    // The fixed 200 ms product latch transition is now authoritative for both
    // Virtual Stock modes. Preserve the enum ordinal for compatibility with
    // old runtime snapshots, but do not let Lab settings duplicate it.
    out.temporal = NormalizeTemporalMode(static_cast<int>(in.temporal));
    // A slower "slow" than "fast" is inverted: swap so the slow constant is
    // always the weaker response. Clamped finite inputs stay finite.
    if (out.adaptiveSlowResponseMs < out.adaptiveFastResponseMs)
        std::swap(out.adaptiveSlowResponseMs, out.adaptiveFastResponseMs);
    return out;
}

// ---------------------------------------------------------------------------
// Quick presets: complete deterministic states for clean A/B testing.
// ---------------------------------------------------------------------------

// Preset selector for QuickPresetSettings. Baseline reproduces the enabled
// Production measurement point; the rest pin an anchor plus an off-hand
// influence (100/75/50 percent).
enum class QuickPreset
{
    Baseline,
    AA100,
    AG100,
    GG100,
    GG75,
    GG50,
};

// Build a preset from DefaultSettings(), never from live state: every field
// not named by the preset keeps its canonical default (Legacy hard
// agreement, no temporal filtering, dormant soft/damping seeds), so applying
// a preset can never inherit a previous agreement/temporal selection.
// Normalize() runs before returning as defence-in-depth; the pinned values
// below are already in range, so it is a no-op for these inputs.
inline Settings QuickPresetSettings(QuickPreset preset) noexcept
{
    Settings out = DefaultSettings();
    out.enabled = true;
    switch (preset)
    {
    case QuickPreset::Baseline:
        out.anchor = AnchorMode::Production;
        out.offhandInfluence = 1.0f;
        break;
    case QuickPreset::AA100:
        out.anchor = AnchorMode::AA;
        out.offhandInfluence = 1.0f;
        break;
    case QuickPreset::AG100:
        out.anchor = AnchorMode::AG;
        out.offhandInfluence = 1.0f;
        break;
    case QuickPreset::GG100:
        out.anchor = AnchorMode::GG;
        out.offhandInfluence = 1.0f;
        break;
    case QuickPreset::GG75:
        out.anchor = AnchorMode::GG;
        out.offhandInfluence = 0.75f;
        break;
    case QuickPreset::GG50:
        out.anchor = AnchorMode::GG;
        out.offhandInfluence = 0.50f;
        break;
    }
    return Normalize(out);
}

// ---------------------------------------------------------------------------
// Anchor resolver.
// ---------------------------------------------------------------------------

struct PivotInputs
{
    virtual_stock::Point3 primaryAim{};
    virtual_stock::Point3 supportAim{};
    bool primaryGripValid = false;
    virtual_stock::Point3 primaryGrip{};
    bool supportGripValid = false;
    virtual_stock::Point3 supportGrip{};
    bool productionSupportUsedGrip = false;
    virtual_stock::Point3 productionSupportPosition{};
};

struct ResolvedPivots
{
    bool valid = false;
    virtual_stock::Point3 primary{};
    virtual_stock::Point3 support{};
    AnchorMode requested = AnchorMode::Production;
    ResolvedAnchor resolved = ResolvedAnchor::Production;
    bool fellBack = false;
    FallbackReason fallbackReason = FallbackReason::None;
};

inline ResolvedAnchor ToResolvedAnchor(AnchorMode mode) noexcept
{
    switch (mode)
    {
    case AnchorMode::Production:
        return ResolvedAnchor::Production;
    case AnchorMode::AA:
        return ResolvedAnchor::AA;
    case AnchorMode::AG:
        return ResolvedAnchor::AG;
    case AnchorMode::GA:
        return ResolvedAnchor::GA;
    case AnchorMode::GG:
        return ResolvedAnchor::GG;
    }
    return ResolvedAnchor::Production;
}

// Resolve the operator-requested anchor to concrete pivots. Grip validity
// flags are the production validity contract: a set flag commits to the grip
// path, so a non-finite grip on a committed path is a required-position
// failure (InvalidInput, fail closed), not a silent downgrade. A clear flag
// selects the aim-line fallback explicitly (fellBack + reason). GG with both
// grips missing reports MissingPrimaryGrip (the primary side is evaluated
// first); the contract accepts either reason there. Production ignores every
// grip field, including productionSupportUsedGrip: it passes the existing
// production selection through unchanged.
inline ResolvedPivots ResolvePivots(
    const PivotInputs& in, AnchorMode requested) noexcept
{
    using virtual_stock::Finite;
    using virtual_stock::Point3;
    ResolvedPivots out{};
    out.requested = requested;
    out.resolved = ToResolvedAnchor(requested);

    const auto invalid = [&](ResolvedAnchor resolved) {
        out.valid = false;
        out.primary = Point3{};
        out.support = Point3{};
        out.resolved = resolved;
        out.fellBack = false;
        out.fallbackReason = FallbackReason::InvalidInput;
        return out;
    };
    const auto emit = [&](Point3 primary, Point3 support,
                          ResolvedAnchor resolved, bool fellBack,
                          FallbackReason reason) {
        out.valid = true;
        out.primary = primary;
        out.support = support;
        out.resolved = resolved;
        out.fellBack = fellBack;
        out.fallbackReason = reason;
        return out;
    };

    switch (requested)
    {
    case AnchorMode::Production:
        if (!Finite(in.primaryAim) || !Finite(in.productionSupportPosition))
            return invalid(ResolvedAnchor::Production);
        return emit(in.primaryAim, in.productionSupportPosition,
            ResolvedAnchor::Production, false, FallbackReason::None);

    case AnchorMode::AA:
        if (!Finite(in.primaryAim) || !Finite(in.supportAim))
            return invalid(ResolvedAnchor::AA);
        return emit(in.primaryAim, in.supportAim,
            ResolvedAnchor::AA, false, FallbackReason::None);

    case AnchorMode::AG:
        if (in.supportGripValid)
        {
            if (!Finite(in.primaryAim) || !Finite(in.supportGrip))
                return invalid(ResolvedAnchor::AG);
            return emit(in.primaryAim, in.supportGrip,
                ResolvedAnchor::AG, false, FallbackReason::None);
        }
        if (!Finite(in.primaryAim) || !Finite(in.supportAim))
            return invalid(ResolvedAnchor::AG);
        return emit(in.primaryAim, in.supportAim,
            ResolvedAnchor::AA, true, FallbackReason::MissingSupportGrip);

    case AnchorMode::GA:
        if (in.primaryGripValid)
        {
            if (!Finite(in.primaryGrip) || !Finite(in.supportAim))
                return invalid(ResolvedAnchor::GA);
            return emit(in.primaryGrip, in.supportAim,
                ResolvedAnchor::GA, false, FallbackReason::None);
        }
        if (!Finite(in.primaryAim) || !Finite(in.supportAim))
            return invalid(ResolvedAnchor::GA);
        return emit(in.primaryAim, in.supportAim,
            ResolvedAnchor::AA, true, FallbackReason::MissingPrimaryGrip);

    case AnchorMode::GG:
        if (in.primaryGripValid && in.supportGripValid)
        {
            if (!Finite(in.primaryGrip) || !Finite(in.supportGrip))
                return invalid(ResolvedAnchor::GG);
            return emit(in.primaryGrip, in.supportGrip,
                ResolvedAnchor::GG, false, FallbackReason::None);
        }
        if (!Finite(in.primaryAim) || !Finite(in.supportAim))
            return invalid(ResolvedAnchor::GG);
        return emit(in.primaryAim, in.supportAim,
            ResolvedAnchor::AA, true,
            !in.primaryGripValid ? FallbackReason::MissingPrimaryGrip
                                 : FallbackReason::MissingSupportGrip);
    }
    return invalid(ResolvedAnchor::Production);
}

// ---------------------------------------------------------------------------
// Soft off-hand authority.
// ---------------------------------------------------------------------------

// Confidence ramp over the agreement band [0.35, fullAgreement]: at or below
// the legacy 0.35 floor there is no authority, at or above fullAgreement
// there is full authority, and between the two a smoothstep blends. A
// fullAgreement at or below the floor (or any non-finite input) yields 0.
inline float SoftAuthorityConfidence(
    float agreement, float fullAgreement) noexcept
{
    if (!std::isfinite(agreement) || !std::isfinite(fullAgreement))
        return 0.0f;
    if (fullAgreement <= kAgreementFloor)
        return 0.0f;
    if (agreement <= kAgreementFloor)
        return 0.0f;
    if (agreement >= fullAgreement)
        return 1.0f;
    const float t = std::clamp(
        (agreement - kAgreementFloor) / (fullAgreement - kAgreementFloor),
        0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return std::isfinite(smooth) ? smooth : 0.0f;
}

// ---------------------------------------------------------------------------
// Temporal damping (orientation domain).
// ---------------------------------------------------------------------------

// Shortest-arc angle between two orientations in degrees. Passes through the
// reused Quat4AngleDegrees machinery, including its contract: inputs that
// cannot be normalized yield a non-finite result. Callers that need a finite
// value (AdaptiveDampingStep) map that case to the zero-error response.
inline float ShortestArcAngleDeg(
    const virtual_stock::Quat4& a, const virtual_stock::Quat4& b) noexcept
{
    return virtual_stock::Quat4AngleDegrees(a, b);
}

// First-order exponential approach: alpha = 1 - exp(-dt/tau) applied as a
// shortest-arc slerp from previous toward target. Guard order is deliberate:
// non-finite quaternions/responses fail closed to the target first so the
// "always finite" guarantee holds even for doubly-degenerate inputs (a naive
// dt-first order would return a non-finite previous verbatim); a bad or
// non-positive dt then holds the previous pose exactly; a sub-epsilon time
// constant answers the exact target.
inline virtual_stock::Quat4 ConstantDampingStep(
    const virtual_stock::Quat4& previous, const virtual_stock::Quat4& target,
    float responseMs, float dtSeconds) noexcept
{
    using virtual_stock::Finite;
    using virtual_stock::Quat4;
    const bool previousFinite = Finite(previous);
    const bool targetFinite = Finite(target);
    if (!previousFinite || !targetFinite || !std::isfinite(responseMs))
        return targetFinite ? target
                            : (previousFinite ? previous : Quat4{});
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0f)
        return previous;
    const float tauSeconds = responseMs / 1000.0f;
    if (!std::isfinite(tauSeconds) || tauSeconds <= kDampingTauEpsilonSeconds)
        return target;
    const float alpha = 1.0f - std::exp(-dtSeconds / tauSeconds);
    if (!std::isfinite(alpha))
        return target;
    // SlerpQuat4Shortest clamps t to [0,1] and never returns non-finite.
    return virtual_stock::SlerpQuat4Shortest(previous, target, alpha);
}

// Adaptive response: the time constant lerps from slowMs at zero error to
// fastMs at fullErrorDeg via a smoothstepped error fraction, then shares the
// ConstantDampingStep guard/alpha/slerp chain exactly. Requires normalized
// settings (slow >= fast) for the "large error answers faster" ordering;
// degenerate quats read as zero error (slow side) and a non-finite or
// non-positive fullErrorDeg holds the slow constant.
inline virtual_stock::Quat4 AdaptiveDampingStep(
    const virtual_stock::Quat4& previous, const virtual_stock::Quat4& target,
    float slowMs, float fastMs, float fullErrorDeg,
    float dtSeconds) noexcept
{
    float errorDeg = ShortestArcAngleDeg(previous, target);
    if (!std::isfinite(errorDeg))
        errorDeg = 0.0f;
    float blend = 0.0f;
    if (std::isfinite(fullErrorDeg) && fullErrorDeg > 0.0f)
    {
        const float t = std::clamp(errorDeg / fullErrorDeg, 0.0f, 1.0f);
        blend = t * t * (3.0f - 2.0f * t);
    }
    const float responseMs = slowMs + (fastMs - slowMs) * blend;
    return ConstantDampingStep(previous, target, responseMs, dtSeconds);
}

// ---------------------------------------------------------------------------
// Shared diagnostics contract (plain POD, no logic) for tranche-2 runtime
// and UI reuse.
// ---------------------------------------------------------------------------

struct Diagnostics
{
    bool labEnabledStored = false;
    bool labActiveThisFrame = false;
    AnchorMode requestedAnchor = AnchorMode::Production;
    ResolvedAnchor resolvedAnchor = ResolvedAnchor::Production;
    FallbackReason fallback = FallbackReason::None;
    bool primaryGripValid = false;
    bool supportGripValid = false;
    bool bValid = false;
    float agreement = 0.0f;
    AgreementMode agreementMode = AgreementMode::LegacyHard;
    float confidence = 0.0f;
    float requestedInfluence = 0.0f;
    float effectiveInfluence = 0.0f;
    TemporalMode temporalMode = TemporalMode::None;
    bool temporalActive = false;
    float errorDeg = 0.0f;
    // Selected pivot positions (tranche 2B, additive). The pair the live
    // selection actually consumed: pivotsValid mirrors the Lab resolve, and
    // the positions are the same (D, S) values the legacy/VS-OFF selection
    // and its trace re-derivation used. Invalid (e.g. one-hand solves, which
    // consume no pivots) reads false with zeroed positions, never stale data.
    bool pivotsValid = false;
    virtual_stock::Point3 primaryPivot{};
    virtual_stock::Point3 supportPivot{};
};

} // namespace two_hand_lab
