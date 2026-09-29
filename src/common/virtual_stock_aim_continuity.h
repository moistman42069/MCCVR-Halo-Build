#pragma once
// Grab/release aim continuity for Virtual Stock.
//
// Without this layer the emitted aim orientation hard-switches in one frame
// when the support-hand latch engages or releases: the live solver instantly
// replaces the one-hand calibrated orientation with the stocked/two-hand
// calibrated orientation (measured 5-60 deg). This header owns the pure,
// deterministic state machine for a temporary orientation correction layered
// AFTER the live fully calibrated solve, anchored so the latch itself adds
// ~zero orientation change and decays to the live solve over a fixed 200 ms.
//
// Composition contract (verified against src/dll/virtual_stock_aim.inl
// finishAimPose and src/common/virtual_stock_logic.h):
//
//   finishAimPose emits   pose = base (x) (yaw (x) pitch (x) roll)
//
// with the standard Hamilton product and the per-gun calibration
// post-multiplied in the pose's own local frame. The correction composes
// OUTSIDE the solver in exactly the same local frame:
//
//   presented = liveOrientation (x) correction
//   correction = inverse(liveOrientation) (x) anchor at seed time
//
// so presented == the anchor exactly (up to float rounding) on the seed frame,
// and the target is always the LIVE orientation of the frame being presented
// (never a frozen seed): presented = live(that frame) (x) correction. The
// correction is presentation-only: it is never fed back into solver inputs (no
// double calibration) and never moves the aim position.
//
// Continuity law (accepted headset result): the same fixed 200 ms smoothstep
// applies to BOTH acquisition and release:
//
//   t = clamp(elapsed / 0.200, 0, 1)
//   u = t * t * (3 - 2 * t)
//   correction = shortestArcSlerp(initialCorrection, identity, u)
//
// and the transition completes to exact identity at elapsed >= 0.200 s, so
// presented == the live orientation exactly from then on. There is no mode
// selector, no motion gate and no persistent-calibration hold; those
// experiment branches were tested and rejected.
//
// Purity/determinism: no globals, no config reads, no logging, no file I/O, no
// allocation. Every input (dt, serial, poses, validity) is injected. The state
// advances exactly once per prepared serial; repeating the same serial is a
// no-op. Apply() and the diagnostics reader are const.
//
// Timing: the consumer advances this module on the same prepared-frame/serial
// boundary that already gates the live solve, passing the prepared-frame dt.
#include "virtual_stock_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace virtual_stock
{
// Fixed 200 ms smoothstep continuity for acquisition and release. Below this
// remaining rotation the transition is complete and the correction snaps to
// exact identity, so an active transition can never linger forever.
inline constexpr float kAimContinuityEaseSeconds = 0.200f;
inline constexpr float kAimContinuityCompletionAngleRadians = 1.0e-5f;

enum class AimContinuityPhase
{
    Idle = 0,
    Acquire = 1,
    Release = 2,
};

enum class AimContinuityEdgeKind
{
    None = 0,
    Grab = 1,
    Release = 2,
};

enum class AimContinuityAnchorSource
{
    None = 0,
    SameFrameOneHand = 1,
    LastPresented = 2,
    Identity = 3,
};

// ---------------------------------------------------------------------------
// Quaternion helpers (Hamilton product, same operand order as finishAimPose).
// ---------------------------------------------------------------------------

inline Quat4 MultiplyQuat4(Quat4 a, Quat4 b) noexcept
{
    return Quat4{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat4 ConjugateQuat4(Quat4 value) noexcept
{
    return Quat4{-value.x, -value.y, -value.z, value.w};
}

inline float DotQuat4(Quat4 a, Quat4 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// Shortest-arc rotation angle between two unit quaternions, in degrees. Uses
// the vector part of the relative rotation (2*atan2(|v|,|w|)) instead of
// acos(|dot|), which loses all resolution below ~0.03 deg in float.
inline float Quat4AngleDegreesNormalized(Quat4 a, Quat4 b) noexcept
{
    Quat4 relative{};
    if (!TryNormalizeQuaternion(MultiplyQuat4(a, ConjugateQuat4(b)), relative))
        return std::numeric_limits<float>::quiet_NaN();
    const float vectorNorm = std::sqrt(
        relative.x * relative.x + relative.y * relative.y +
        relative.z * relative.z);
    if (!std::isfinite(vectorNorm))
        return std::numeric_limits<float>::quiet_NaN();
    const float angle = 2.0f * std::atan2(
        std::clamp(vectorNorm, 0.0f, 1.0f), std::fabs(relative.w));
    if (!std::isfinite(angle))
        return std::numeric_limits<float>::quiet_NaN();
    return angle * 57.2957795131f;
}

// Same angle for arbitrary input quaternions; returns a non-finite value when
// either quaternion cannot be normalized.
inline float Quat4AngleDegrees(Quat4 a, Quat4 b) noexcept
{
    Quat4 na{};
    Quat4 nb{};
    if (!TryNormalizeQuaternion(a, na) || !TryNormalizeQuaternion(b, nb))
        return std::numeric_limits<float>::quiet_NaN();
    return Quat4AngleDegreesNormalized(na, nb);
}

// Shortest-arc spherical interpolation. Returns `from` (normalized) at t <= 0
// and `to` (normalized) at t >= 1; never returns a non-finite quaternion.
inline Quat4 SlerpQuat4Shortest(Quat4 from, Quat4 to, float t) noexcept
{
    Quat4 a{};
    Quat4 b{};
    if (!TryNormalizeQuaternion(from, a))
        return Quat4{};
    if (!TryNormalizeQuaternion(to, b))
        return a;
    const float clamped = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    if (clamped <= 0.0f)
        return a;
    if (clamped >= 1.0f)
        return b;

    float dot = DotQuat4(a, b);
    if (dot < 0.0f)
    {
        b = Quat4{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0f, 1.0f);

    Quat4 candidate{};
    if (dot > 0.9995f)
    {
        candidate = Quat4{
            a.x + (b.x - a.x) * clamped,
            a.y + (b.y - a.y) * clamped,
            a.z + (b.z - a.z) * clamped,
            a.w + (b.w - a.w) * clamped};
    }
    else
    {
        const float theta = std::acos(dot);
        const float sinTheta = std::sin(theta);
        if (!std::isfinite(theta) || !std::isfinite(sinTheta) ||
            std::fabs(sinTheta) < 1.0e-8f)
            return a;
        const float weightFrom = std::sin((1.0f - clamped) * theta) / sinTheta;
        const float weightTo = std::sin(clamped * theta) / sinTheta;
        if (!std::isfinite(weightFrom) || !std::isfinite(weightTo))
            return a;
        candidate = Quat4{
            a.x * weightFrom + b.x * weightTo,
            a.y * weightFrom + b.y * weightTo,
            a.z * weightFrom + b.z * weightTo,
            a.w * weightFrom + b.w * weightTo};
    }
    Quat4 normalized{};
    return TryNormalizeQuaternion(candidate, normalized) ? normalized : a;
}

inline bool IsNearIdentityRotation(Quat4 value, float radiansEpsilon) noexcept
{
    Quat4 normalized{};
    if (!TryNormalizeQuaternion(value, normalized))
        return false;
    const float vectorNorm = std::sqrt(
        normalized.x * normalized.x + normalized.y * normalized.y +
        normalized.z * normalized.z);
    if (!std::isfinite(vectorNorm))
        return false;
    const float angle = 2.0f * std::atan2(
        std::clamp(vectorNorm, 0.0f, 1.0f), std::fabs(normalized.w));
    return std::isfinite(angle) && angle <= radiansEpsilon;
}

// ---------------------------------------------------------------------------
// Injected per-prepared-frame inputs.
// ---------------------------------------------------------------------------
struct AimContinuityInput
{
    uint64_t preparedSerial = 0;
    float dtSeconds = 0.0f;

    // Durable latch state. This is the only transition edge source for the
    // VS-OFF product bridge. VS Standard/Plus also preserve their historical
    // stock-solve ownership edge below.
    bool latched = false;

    // VS Standard/Plus historically start/finish continuity when their stock
    // solve begins/ends owning presentation. The VS-OFF product path leaves
    // this false, so B acceptance, PG retention and geometry changes cannot
    // restart its latch-edge bridge.
    bool stockSolveOwnsPresentation = false;

    // Fully calibrated orientation the live solver returned this serial.
    bool liveOrientationValid = false;
    Quat4 liveOrientation{};

    // Same-frame one-hand calibrated counterfactual (stock solve not applied).
    bool oneHandOrientationValid = false;
    Quat4 oneHandOrientation{};

    // Caller-provided orientation presented on the previous serial.
    bool lastPresentedOrientationValid = false;
    Quat4 lastPresentedOrientation{};
};

struct AimContinuityState
{
    bool initialized = false;
    uint64_t lastPreparedSerial = 0;
    bool previousLatched = false;
    bool previousStockOwnsPresentation = false;

    AimContinuityPhase phase = AimContinuityPhase::Idle;
    bool active = false;
    AimContinuityEdgeKind edgeKind = AimContinuityEdgeKind::None;
    AimContinuityAnchorSource anchorSource = AimContinuityAnchorSource::None;

    // Local-frame correction applied as live (x) correction.
    Quat4 correction{};
    Quat4 initialCorrection{};
    float elapsedSeconds = 0.0f;
};

// Read-only telemetry contract for the live integration. Field names are
// snake_case with unit suffixes, matching the config/telemetry convention
// (src/common/config.h), so the consumer can publish them directly.
struct AimContinuityDiagnostics
{
    AimContinuityPhase phase = AimContinuityPhase::Idle;
    bool active = false;
    AimContinuityEdgeKind edge_kind = AimContinuityEdgeKind::None;
    AimContinuityAnchorSource anchor_source = AimContinuityAnchorSource::None;
    float initial_correction_deg = 0.0f;
    float remaining_correction_deg = 0.0f;
    float transition_elapsed_ms = 0.0f;
    uint64_t last_prepared_serial = 0;
};

inline void ResetAimContinuity(AimContinuityState& state) noexcept
{
    state = AimContinuityState{};
}

inline Quat4 ApplyAimContinuity(
    const AimContinuityState& state, Quat4 liveOrientation) noexcept
{
    if (!state.active)
        return liveOrientation;
    Quat4 live{};
    Quat4 correction{};
    if (!TryNormalizeQuaternion(liveOrientation, live) ||
        !TryNormalizeQuaternion(state.correction, correction))
        return liveOrientation;
    Quat4 composed{};
    if (!TryNormalizeQuaternion(
            MultiplyQuat4(live, correction), composed))
        return liveOrientation;
    return composed;
}

inline AimContinuityDiagnostics ReadAimContinuityDiagnostics(
    const AimContinuityState& state) noexcept
{
    AimContinuityDiagnostics result{};
    result.phase = state.phase;
    result.active = state.active;
    result.edge_kind = state.edgeKind;
    result.anchor_source = state.anchorSource;
    const Quat4 identity{};
    const float initial = Quat4AngleDegrees(state.initialCorrection, identity);
    const float remaining = Quat4AngleDegrees(state.correction, identity);
    result.initial_correction_deg = std::isfinite(initial) ? initial : 0.0f;
    result.remaining_correction_deg = std::isfinite(remaining) ? remaining : 0.0f;
    result.transition_elapsed_ms = state.elapsedSeconds * 1000.0f;
    result.last_prepared_serial = state.lastPreparedSerial;
    return result;
}

// ---------------------------------------------------------------------------
// State machine internals (still pure; free functions keep the caller small).
// ---------------------------------------------------------------------------

inline void FailOpenAimContinuity(
    AimContinuityState& state, AimContinuityEdgeKind edgeKind) noexcept
{
    state.active = false;
    state.phase = AimContinuityPhase::Idle;
    state.edgeKind = edgeKind;
    state.anchorSource = AimContinuityAnchorSource::Identity;
    state.correction = Quat4{};
    state.initialCorrection = Quat4{};
    state.elapsedSeconds = 0.0f;
}

inline bool TryNormalizeAimContinuityInput(
    bool valid, Quat4 raw, Quat4& out) noexcept
{
    return valid && TryNormalizeQuaternion(raw, out);
}

inline bool CompleteSeededAimContinuity(
    AimContinuityState& state, AimContinuityEdgeKind edgeKind,
    AimContinuityAnchorSource anchor, Quat4 correction,
    AimContinuityPhase phase) noexcept
{
    Quat4 normalized{};
    if (!TryNormalizeQuaternion(correction, normalized))
        return false;
    state.correction = normalized;
    state.initialCorrection = normalized;
    state.anchorSource = anchor;
    state.edgeKind = edgeKind;
    state.phase = phase;
    state.active = true;
    state.elapsedSeconds = 0.0f;
    return true;
}

// Grab edge. Seeding makes presented == oneHandOrientation exactly; a
// re-entrant edge (a transition is still active) preserves the orientation the
// player is already seeing via lastPresentedOrientation.
inline void SeedAimContinuityGrab(
    AimContinuityState& state, const AimContinuityInput& input) noexcept
{
    Quat4 live{};
    const bool liveValid = TryNormalizeAimContinuityInput(
        input.liveOrientationValid, input.liveOrientation, live);
    if (!liveValid)
    {
        // No valid live solve: fail open. Stay at identity so the caller's own
        // presentation is untouched rather than fabricating a jump.
        FailOpenAimContinuity(state, AimContinuityEdgeKind::Grab);
        return;
    }

    const bool reentrant = state.active || !IsNearIdentityRotation(
        state.correction, kAimContinuityCompletionAngleRadians);

    Quat4 oneHand{};
    Quat4 lastPresented{};
    const bool oneHandValid = TryNormalizeAimContinuityInput(
        input.oneHandOrientationValid, input.oneHandOrientation, oneHand);
    const bool lastPresentedValid = TryNormalizeAimContinuityInput(
        input.lastPresentedOrientationValid, input.lastPresentedOrientation,
        lastPresented);

    if (reentrant && lastPresentedValid)
    {
        if (CompleteSeededAimContinuity(
                state, AimContinuityEdgeKind::Grab,
                AimContinuityAnchorSource::LastPresented,
                MultiplyQuat4(ConjugateQuat4(live), lastPresented),
                AimContinuityPhase::Acquire))
            return;
    }
    else if (reentrant)
    {
        // Re-entrant edge without a safe anchor: keep the module's own applied
        // correction (its last presented orientation) instead of jumping, and
        // restart the fixed ease from what the player is seeing right now.
        // Re-seeding the ease initial value from the running correction is what
        // makes that restart continuous: the seed serial presents exactly the
        // running correction and only then does the 200 ms smoothstep begin.
        state.edgeKind = AimContinuityEdgeKind::Grab;
        state.phase = AimContinuityPhase::Acquire;
        state.active = true;
        state.initialCorrection = state.correction;
        state.elapsedSeconds = 0.0f;
        return;
    }
    else if (oneHandValid)
    {
        if (CompleteSeededAimContinuity(
                state, AimContinuityEdgeKind::Grab,
                AimContinuityAnchorSource::SameFrameOneHand,
                MultiplyQuat4(ConjugateQuat4(live), oneHand),
                AimContinuityPhase::Acquire))
            return;
    }
    else if (lastPresentedValid)
    {
        if (CompleteSeededAimContinuity(
                state, AimContinuityEdgeKind::Grab,
                AimContinuityAnchorSource::LastPresented,
                MultiplyQuat4(ConjugateQuat4(live), lastPresented),
                AimContinuityPhase::Acquire))
            return;
    }

    // Neither anchor safely available (or the correction failed to normalize):
    // identity fail-open, never a fabricated jump.
    FailOpenAimContinuity(state, AimContinuityEdgeKind::Grab);
}

// Release edge. Seeding makes presented == lastPresentedOrientation anchored to
// the one-hand live solve, then the fixed ease decays it to the live solve.
inline void SeedAimContinuityRelease(
    AimContinuityState& state, const AimContinuityInput& input) noexcept
{
    Quat4 base{};
    bool baseValid = TryNormalizeAimContinuityInput(
        input.oneHandOrientationValid, input.oneHandOrientation, base);
    if (!baseValid)
        baseValid = TryNormalizeAimContinuityInput(
            input.liveOrientationValid, input.liveOrientation, base);

    Quat4 lastPresented{};
    const bool lastPresentedValid = TryNormalizeAimContinuityInput(
        input.lastPresentedOrientationValid, input.lastPresentedOrientation,
        lastPresented);

    if (baseValid && lastPresentedValid &&
        CompleteSeededAimContinuity(
            state, AimContinuityEdgeKind::Release,
            AimContinuityAnchorSource::LastPresented,
            MultiplyQuat4(ConjugateQuat4(base), lastPresented),
            AimContinuityPhase::Release))
        return;

    if (state.active)
    {
        // The module's own applied correction already preserves what the
        // player is seeing; keep it and start the release ease from here. The
        // ease initial value is re-seeded from the running correction so the
        // release serial presents it unchanged and the 200 ms smoothstep
        // starts only on the following advance.
        state.edgeKind = AimContinuityEdgeKind::Release;
        state.phase = AimContinuityPhase::Release;
        state.initialCorrection = state.correction;
        state.elapsedSeconds = 0.0f;
        return;
    }

    FailOpenAimContinuity(state, AimContinuityEdgeKind::Release);
}

inline void CompleteAimContinuity(AimContinuityState& state) noexcept
{
    // Keep edgeKind/anchorSource/initialCorrection/elapsed as the last-edge
    // diagnostics; the next edge or Reset replaces them.
    state.correction = Quat4{};
    state.active = false;
    state.phase = AimContinuityPhase::Idle;
}

// The accepted continuity law, identical for acquisition and release: a fixed
// 200 ms smoothstep from the seeded correction to identity, with an exact
// identity completion at and after 200 ms.
inline void StepAimContinuity(
    AimContinuityState& state, float dtSeconds) noexcept
{
    state.elapsedSeconds += dtSeconds;
    const float t = std::clamp(
        state.elapsedSeconds / kAimContinuityEaseSeconds, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    state.correction = SlerpQuat4Shortest(
        state.initialCorrection, Quat4{}, smooth);
    if (state.elapsedSeconds >= kAimContinuityEaseSeconds)
        CompleteAimContinuity(state);
}

// ---------------------------------------------------------------------------
// Public entry points.
// ---------------------------------------------------------------------------

inline void AdvanceAimContinuity(
    AimContinuityState& state, const AimContinuityInput& input) noexcept
{
    if (!input.preparedSerial || input.preparedSerial == state.lastPreparedSerial)
        return;
    // Degenerate timing: no advance, no state change (the serial stays
    // unconsumed so the next valid call still processes this frame).
    if (!std::isfinite(input.dtSeconds) || input.dtSeconds <= 0.0f)
        return;
    state.lastPreparedSerial = input.preparedSerial;

    if (!state.initialized)
    {
        // First observation (or first after Reset): an already-latched or
        // already-stocked state is never an edge. Establish the baseline and
        // stay inert so presented == live, exactly as if the module were off.
        state.initialized = true;
        state.previousLatched = input.latched;
        state.previousStockOwnsPresentation =
            input.stockSolveOwnsPresentation;
        return;
    }

    const bool grabEdge = (!state.previousLatched && input.latched) ||
        (!state.previousStockOwnsPresentation &&
            input.stockSolveOwnsPresentation);
    const bool releaseEdge = (state.previousLatched && !input.latched) ||
        (state.previousStockOwnsPresentation &&
            !input.stockSolveOwnsPresentation);
    state.previousLatched = input.latched;
    state.previousStockOwnsPresentation =
        input.stockSolveOwnsPresentation;

    // A released latch seeds the release transition; otherwise acquire only on
    // a false-to-true latch edge.
    if (releaseEdge)
        SeedAimContinuityRelease(state, input);
    else if (grabEdge)
        SeedAimContinuityGrab(state, input);
    else if (state.active)
        StepAimContinuity(state, input.dtSeconds);
}
} // namespace virtual_stock
