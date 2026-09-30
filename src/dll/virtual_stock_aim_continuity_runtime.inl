// Live grab/release aim continuity seam for Virtual Stock.
//
// Included inside vr.cpp's anonymous namespace after ComputeAimPose(). The
// layer itself lives in src/common/virtual_stock_aim_continuity.h (pure state
// machine, own unit suite). This helper is the production wiring decision
// point and is deliberately pure so the fixture generator can compile the
// real body in an end-to-end test:
//
//   * the caller supplies the exact same-frame stock-aware AimPoseInputs the
//     presentation solves use (CurrentFrameStockAimPoseInputs);
//   * the live calibrated solve and the same-frame one-hand counterfactual
//     (identical inputs with twoHandLatched=false, so the one-hand branch runs
//     with identical calibration) are both computed here through the one
//     production ComputeAimPose path;
//   * the layer advances at most once per prepared serial and returns the
//     orientation to present, composed as live (x) correction.
//
// On a grab edge that composition makes presented == the same-frame one-hand
// counterfactual (the latch frame keeps the aim the player already had); on a
// release edge it keeps the aim the player was already seeing. Both directions
// then follow the same fixed 200 ms smoothstep to the live solve. No solver
// input, config value, global or publication is touched here, and the helper
// never allocates, logs, locks or reads files.

    struct AimContinuityFrameSolve
    {
        bool liveValid = false;
        virtual_stock::Quat4 live{};
        bool oneHandValid = false;
        virtual_stock::Quat4 oneHand{};
        // True when the layer returned a usable orientation this serial. It may
        // be exactly `live` when no correction is active.
        bool presentedValid = false;
        virtual_stock::Quat4 presented{};
        bool correctionActive = false;
        virtual_stock::Quat4 correction{};
        // True only when this call consumed a new prepared serial (the layer's
        // duplicate-serial and non-positive-dt guards leave it false).
        bool advanceConsumed = false;
    };

    AimContinuityFrameSolve AdvanceAimContinuityFrame(
        virtual_stock::AimContinuityState& state,
        const AimPoseInputs& stockInputs,
        uint64_t preparedSerial, float dtSeconds,
        bool lastPresentedValid, virtual_stock::Quat4 lastPresented) noexcept
    {
        AimContinuityFrameSolve solve{};

        const auto toQuat = [](const XrQuaternionf& value) {
            return virtual_stock::Quat4{
                value.x, value.y, value.z, value.w};
        };

        const AimPoseResult live = ComputeAimPose(stockInputs);
        solve.liveValid = live.updateTwoHandActivity && live.valid &&
            virtual_stock::TryNormalizeQuaternion(
                toQuat(live.pose.orientation), solve.live);

        // Same-frame one-hand counterfactual: only the latch differs, so the
        // per-gun calibration, stock settings, head sample and support
        // endpoint are byte-identical to the live solve.
        AimPoseInputs oneHandInputs = stockInputs;
        oneHandInputs.twoHandLatched = false;
        const AimPoseResult oneHand = ComputeAimPose(oneHandInputs);
        solve.oneHandValid = oneHand.updateTwoHandActivity && oneHand.valid &&
            virtual_stock::TryNormalizeQuaternion(
                toQuat(oneHand.pose.orientation), solve.oneHand);

        virtual_stock::AimContinuityInput input{};
        input.preparedSerial = preparedSerial;
        input.dtSeconds = dtSeconds;
        input.latched = stockInputs.twoHandLatched;
        // Preserve VS Standard/Plus ownership-edge behavior. The free-aim
        // product bridge is strictly latch-edge driven.
        input.stockSolveOwnsPresentation = stockInputs.virtualStockEnabled &&
            solve.liveValid && live.twoHandActive;
        input.liveOrientationValid = solve.liveValid;
        input.liveOrientation = solve.live;
        input.oneHandOrientationValid = solve.oneHandValid;
        input.oneHandOrientation = solve.oneHand;
        input.lastPresentedOrientationValid = lastPresentedValid;
        input.lastPresentedOrientation = lastPresented;

        const uint64_t previousSerial = state.lastPreparedSerial;
        virtual_stock::AdvanceAimContinuity(state, input);
        solve.advanceConsumed = state.lastPreparedSerial != previousSerial;

        solve.correctionActive = state.active;
        solve.correction = state.correction;
        if (solve.liveValid)
        {
            solve.presented =
                virtual_stock::ApplyAimContinuity(state, solve.live);
            solve.presentedValid = virtual_stock::Finite(solve.presented);
        }
        return solve;
    }
