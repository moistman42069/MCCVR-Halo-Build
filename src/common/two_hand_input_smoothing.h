#pragma once
// Pavlov-inspired controller-input smoothing for two-hand aim (Virtual Stock
// on or off).
// The temporal filter is FIXED: one speed-25 delta-time response, quaternion-
// native (shortest-arc SLERP). The user-facing strength (0..25) is a wet/dry
// mix over that unchanged full-filter output, never an interpolation speed:
// 0 emits raw input untouched, 25 emits the full-filtered result untouched,
// and intermediate values stay between them. Both endpoints are exact.
// This filters copies of the directional solve geometry only: no OpenXR pose,
// latch/acquisition sample, one-hand aim, weapon base position, or raw Virtual
// Stock input is mutated. The VS-OFF-only product geometry and Lab keep their
// own gates; a VS-ON solve consumes the same directional copies.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "virtual_stock_logic.h"
#include "virtual_stock_aim_continuity.h"

namespace two_hand_input_smoothing
{
inline constexpr float kResponseSpeed = 25.0f;
inline constexpr float kPi = 3.14159265358979323846f;
// User-facing strength domain. 25 is the full fixed speed-25 filter above.
inline constexpr float kStrengthMinimum = kTwoHandSmoothingStrengthMinimum;
inline constexpr float kStrengthMaximum = kTwoHandSmoothingStrengthMaximum;

struct State
{
    bool initialized = false;
    uint64_t lastPreparedSerial = 0;
    virtual_stock::Quat4 primaryOrientation{};
    virtual_stock::Point3 primaryAimPosition{};
    virtual_stock::Point3 supportAimPosition{};
    bool primaryGripValid = false;
    virtual_stock::Point3 primaryGripPosition{};
    bool supportGripValid = false;
    virtual_stock::Point3 supportGripPosition{};
};

struct Sample
{
    virtual_stock::Quat4 primaryOrientation{};
    virtual_stock::Point3 primaryAimPosition{};
    virtual_stock::Point3 supportAimPosition{};
    bool primaryGripValid = false;
    virtual_stock::Point3 primaryGripPosition{};
    bool supportGripValid = false;
    virtual_stock::Point3 supportGripPosition{};
};

struct Result
{
    // valid: the prepared serial carried a usable raw sample and the layer was
    // eligible. false = fail open to raw for this frame (no filtered packet).
    bool valid = false;
    // advanced: this serial moved the fixed filter's own history. Duplicate
    // serials, non-positive dt and ineligible frames never advance it.
    bool advanced = false;
    // INTERNAL speed-25 temporal coefficient: clamp(25*dt, 0, 1). This is the
    // filter's own response, never the user-facing strength amount.
    float alpha = 0.0f;
    // User-facing strength in 0..25 and its normalized wet/dry mix strength/25.
    float strength = 0.0f;
    float mix = 0.0f;
    Sample raw{};
    // Full fixed speed-25 filter output (independent of the user strength).
    Sample filtered{};
    // What consumers actually receive: raw<->filtered blended by mix. Exact
    // raw at mix<=0 and the exact filtered sample at mix>=1.
    Sample mixed{};
    // Raw-to-FULL-filtered input error metrics (unchanged meanings). The
    // applied mix's own deviation is mix times these by construction, so it is
    // deliberately not stored a second time.
    float primaryOrientationErrorDeg = 0.0f;
    float primaryPositionErrorM = 0.0f;
    float supportPositionErrorM = 0.0f;
};

// Identity frozen with a prepared-frame publication. Cross-thread consumers
// accept a filtered copy only for the exact captured serial and coordinate/
// ownership context that produced it.
struct PublicationIdentity
{
    uint64_t preparedSerial = 0;
    uint64_t contactSpaceEpoch = 0;
    uint8_t title = 0;
    uint32_t titleGeneration = 0;
    bool leftHanded = false;
    bool supportEndpointUsedGrip = false;
};

inline bool SamePublicationIdentity(
    const PublicationIdentity& expected,
    const PublicationIdentity& actual) noexcept
{
    return expected.preparedSerial != 0 &&
        expected.preparedSerial == actual.preparedSerial &&
        expected.contactSpaceEpoch == actual.contactSpaceEpoch &&
        expected.title == actual.title &&
        expected.titleGeneration == actual.titleGeneration &&
        expected.leftHanded == actual.leftHanded &&
        expected.supportEndpointUsedGrip ==
            actual.supportEndpointUsedGrip;
}

inline float ClampAlpha(float dtSeconds) noexcept
{
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0f)
        return 0.0f;
    return std::clamp(25.0f * dtSeconds, 0.0f, 1.0f);
}

// User strength clamped into the 0..25 domain. Non-finite and non-positive
// values read as 0 (off), values above 25 saturate to the full filter.
inline float ClampStrength(float strength) noexcept
{
    if (!std::isfinite(strength) || strength <= 0.0f)
        return kStrengthMinimum;
    return std::min(strength, kStrengthMaximum);
}

// Normalized wet/dry amount in 0..1: strength 25 -> 1 (full filter output).
inline float StrengthMix(float strength) noexcept
{
    return ClampStrength(strength) / kStrengthMaximum;
}

inline bool FiniteSample(const Sample& sample) noexcept
{
    virtual_stock::Quat4 orientation{};
    return virtual_stock::TryNormalizeQuaternion(
               sample.primaryOrientation, orientation) &&
        virtual_stock::Finite(sample.primaryAimPosition) &&
        virtual_stock::Finite(sample.supportAimPosition) &&
        (!sample.primaryGripValid ||
            virtual_stock::Finite(sample.primaryGripPosition)) &&
        (!sample.supportGripValid ||
            virtual_stock::Finite(sample.supportGripPosition));
}

inline virtual_stock::Point3 Lerp(
    virtual_stock::Point3 from, virtual_stock::Point3 to, float alpha) noexcept
{
    return {from.x + (to.x - from.x) * alpha,
            from.y + (to.y - from.y) * alpha,
            from.z + (to.z - from.z) * alpha};
}

inline Sample SampleFromState(const State& state) noexcept
{
    return Sample{state.primaryOrientation, state.primaryAimPosition,
        state.supportAimPosition, state.primaryGripValid,
        state.primaryGripPosition, state.supportGripValid,
        state.supportGripPosition};
}

inline float DistanceOrZero(
    virtual_stock::Point3 a, virtual_stock::Point3 b) noexcept
{
    const virtual_stock::Point3 delta{a.x - b.x, a.y - b.y, a.z - b.z};
    const float squared = virtual_stock::Dot(delta, delta);
    return std::isfinite(squared) && squared >= 0.0f
        ? std::sqrt(squared) : 0.0f;
}

inline void FillErrors(Result& result) noexcept
{
    const float angle = virtual_stock::Quat4AngleDegrees(
        result.raw.primaryOrientation, result.filtered.primaryOrientation);
    result.primaryOrientationErrorDeg = std::isfinite(angle) ? angle : 0.0f;
    result.primaryPositionErrorM = DistanceOrZero(
        result.raw.primaryAimPosition, result.filtered.primaryAimPosition);
    result.supportPositionErrorM = DistanceOrZero(
        result.raw.supportAimPosition, result.filtered.supportAimPosition);
}

// Wet/dry blend of one raw sample with its full fixed-filter output. Both
// endpoints are exact assignments: mix <= 0 is raw untouched, mix >= 1 is the
// full-filtered sample untouched (never a lerp that could round). Intermediate
// mixes stay strictly between the two, so no partial strength can lag more
// than the full-strength result it blends toward. Grip copies are only blended
// where both endpoints are valid; validity itself is the raw sample's.
inline Sample BlendByMix(const Sample& raw, const Sample& fullFiltered,
    float mix) noexcept
{
    if (!(mix > 0.0f))
        return raw;
    if (mix >= 1.0f)
        return fullFiltered;
    Sample blended = raw;
    blended.primaryOrientation = virtual_stock::SlerpQuat4Shortest(
        raw.primaryOrientation, fullFiltered.primaryOrientation, mix);
    blended.primaryAimPosition = Lerp(
        raw.primaryAimPosition, fullFiltered.primaryAimPosition, mix);
    blended.supportAimPosition = Lerp(
        raw.supportAimPosition, fullFiltered.supportAimPosition, mix);
    if (raw.primaryGripValid && fullFiltered.primaryGripValid)
        blended.primaryGripPosition = Lerp(
            raw.primaryGripPosition, fullFiltered.primaryGripPosition, mix);
    if (raw.supportGripValid && fullFiltered.supportGripValid)
        blended.supportGripPosition = Lerp(
            raw.supportGripPosition, fullFiltered.supportGripPosition, mix);
    return blended;
}

inline void Reset(State& state) noexcept
{
    state = State{};
}

// One fixed alpha=clamp(25*dt,0,1), advanced at most once for each prepared
// serial, whenever the user strength is above zero. First valid observation
// seeds exactly raw, so the mixed output is also exactly raw on that frame at
// every strength. Invalid input fails open without emitting stale filtered
// data. The mixed output is never written back into filter history.
inline Result Advance(State& state, uint64_t preparedSerial,
    float dtSeconds, const Sample& raw,
    float strength = kStrengthMaximum) noexcept
{
    Result result{};
    result.raw = raw;
    result.strength = ClampStrength(strength);
    result.mix = result.strength / kStrengthMaximum;
    if (result.strength <= kStrengthMinimum)
    {
        // Strength off: the fixed filter is neither consumed nor advanced and
        // no filtered packet exists. Consumers take their exact raw path.
        result.filtered = raw;
        result.mixed = raw;
        return result;
    }
    if (!preparedSerial || !std::isfinite(dtSeconds) || dtSeconds <= 0.0f ||
        !FiniteSample(raw))
        return result;

    result.valid = true;
    result.alpha = ClampAlpha(dtSeconds);
    if (state.initialized && preparedSerial <= state.lastPreparedSerial)
    {
        result.filtered = SampleFromState(state);
        result.mixed = BlendByMix(raw, result.filtered, result.mix);
        if (!FiniteSample(result.filtered) || !FiniteSample(result.mixed))
        {
            Reset(state);
            result.filtered = raw;
            result.mixed = raw;
            result.alpha = 0.0f;
            result.valid = false;
            return result;
        }
        result.alpha = 0.0f;
        FillErrors(result);
        return result;
    }

    virtual_stock::Quat4 previousOrientation{};
    const Sample previous = SampleFromState(state);
    const bool havePrevious = state.initialized &&
        virtual_stock::TryNormalizeQuaternion(
            state.primaryOrientation, previousOrientation) &&
        FiniteSample(previous);
    Sample filtered = raw;
    if (!havePrevious)
    {
        // The first eligible frame is exactly the raw input, with no catch-up
        // from one-handed, invalid, or otherwise inactive history.
    }
    else
    {
        const float alpha = result.alpha;
        filtered.primaryOrientation = virtual_stock::SlerpQuat4Shortest(
            previousOrientation, raw.primaryOrientation, alpha);
        filtered.primaryAimPosition = Lerp(
            previous.primaryAimPosition, raw.primaryAimPosition, alpha);
        filtered.supportAimPosition = Lerp(
            previous.supportAimPosition, raw.supportAimPosition, alpha);
        if (raw.primaryGripValid && previous.primaryGripValid)
            filtered.primaryGripPosition = Lerp(
                previous.primaryGripPosition, raw.primaryGripPosition, alpha);
        if (raw.supportGripValid && previous.supportGripValid)
            filtered.supportGripPosition = Lerp(
                previous.supportGripPosition, raw.supportGripPosition, alpha);
    }

    // Protect both the returned packet and the retained history from overflow
    // or malformed quaternion interpolation. This feature fails open to raw.
    if (!FiniteSample(filtered))
    {
        Reset(state);
        result.filtered = raw;
        result.mixed = raw;
        result.alpha = 0.0f;
        result.valid = true;
        result.advanced = false;
        return result;
    }
    result.mixed = BlendByMix(raw, filtered, result.mix);
    if (!FiniteSample(result.mixed))
    {
        Reset(state);
        result.filtered = raw;
        result.mixed = raw;
        result.alpha = 0.0f;
        result.valid = true;
        result.advanced = false;
        return result;
    }

    state.primaryOrientation = filtered.primaryOrientation;
    state.primaryAimPosition = filtered.primaryAimPosition;
    state.supportAimPosition = filtered.supportAimPosition;
    state.primaryGripValid = filtered.primaryGripValid;
    state.primaryGripPosition = filtered.primaryGripPosition;
    state.supportGripValid = filtered.supportGripValid;
    state.supportGripPosition = filtered.supportGripPosition;
    state.initialized = true;
    state.lastPreparedSerial = preparedSerial;
    result.advanced = true;
    result.filtered = filtered;
    FillErrors(result);
    return result;
}
} // namespace two_hand_input_smoothing
