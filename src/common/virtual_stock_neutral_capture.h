#pragma once

#include "virtual_stock_logic.h"
#include <cstdint>

namespace virtual_stock
{
struct InverseNeckNeutralCaptureInput
{
    bool inverseNeckFamily = false;
    bool menuOpen = false;
    bool headSuitable = false;
    bool referenceTransitionPending = false;
    uint64_t preparedSerial = 0;
    uint64_t contactSpaceEpoch = 0;
    Quat4 headOrientation{};
};

struct InverseNeckNeutralCaptureState
{
    bool initialized = false;
    bool familyActive = false;
    bool captureArmed = false;
    bool neutralValid = false;
    uint64_t lastPreparedSerial = 0;
    uint64_t observedContactSpaceEpoch = 0;
    Quat4 neutralOrientation{};
    uint64_t captureSerial = 0;
    uint64_t captureContactSpaceEpoch = 0;
};

inline void AdvanceInverseNeckNeutralCapture(
    InverseNeckNeutralCaptureState& state,
    const InverseNeckNeutralCaptureInput& input) noexcept
{
    if (!input.preparedSerial || input.preparedSerial == state.lastPreparedSerial)
        return;
    state.lastPreparedSerial = input.preparedSerial;

    const bool familyChanged = !state.initialized ||
        input.inverseNeckFamily != state.familyActive;
    const bool epochChanged = state.initialized &&
        input.contactSpaceEpoch != state.observedContactSpaceEpoch;
    state.initialized = true;
    state.familyActive = input.inverseNeckFamily;
    state.observedContactSpaceEpoch = input.contactSpaceEpoch;

    if (!input.inverseNeckFamily)
    {
        state.captureArmed = false;
        state.neutralValid = false;
        state.neutralOrientation = {};
        state.captureSerial = 0;
        state.captureContactSpaceEpoch = 0;
        return;
    }

    if (familyChanged || epochChanged)
    {
        state.captureArmed = true;
        state.neutralValid = false;
        state.neutralOrientation = {};
        state.captureSerial = 0;
        state.captureContactSpaceEpoch = 0;
    }
    if (!state.captureArmed || input.menuOpen || !input.headSuitable ||
        input.referenceTransitionPending || epochChanged)
        return;

    Quat4 normalized{};
    if (!TryNormalizeQuaternion(input.headOrientation, normalized))
        return;
    state.neutralOrientation = normalized;
    state.captureSerial = input.preparedSerial;
    state.captureContactSpaceEpoch = input.contactSpaceEpoch;
    state.neutralValid = true;
    state.captureArmed = false;
}
} // namespace virtual_stock
