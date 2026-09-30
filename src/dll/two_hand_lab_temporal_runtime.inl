// Two-Hand Lab temporal experiment modes (tranche 2B).
//
// Included inside vr.cpp's anonymous namespace after ComputeAimPose() and
// after virtual_stock_aim_continuity_runtime.inl. The layer itself is the pure
// per-mode machinery: tranche-1 steps in src/common/two_hand_lab_logic.h
// (ConstantDampingStep, AdaptiveDampingStep). Product latch continuity is
// supplied as the temporal target and is never duplicated in Lab state. This
// helper is the production wiring decision point and is
// deliberately store-free so the fixture generator can compile the real body
// in an end-to-end test (tests/two_hand_lab_temporal_tests.cpp):
//
//   * the caller supplies the exact same-frame stock-aware AimPoseInputs the
//     presentation solves use (CurrentFrameStockAimPoseInputs), which already
//     carry the Lab snapshot, the settings generation, and the grip
//     endpoints for this exact sample;
//   * the frame's stateless Lab orientation (the calibrated result.pose of
//     the Lab-active solve — the same live solve the product layer uses) is
//     solved here through the one production ComputeAimPose path, optionally
//     with a trace so one solve feeds both the stateless report and diagnostics;
//   * the layer advances at most once per prepared serial and returns the
//     orientation to present.
//
// Temporal modes are None, ConstantDamping, and AdaptiveDamping. Legacy
// ordinal 1 normalizes to None because product continuity owns the fixed
// 200 ms latch transition for both VS modes. Damping parameters come from the
// runtime snapshot. A mode change, Lab enable/disable edge, or applicability
// edge (resolved VS on/off, or the product continuity seam owning the frame)
// resets temporal history before the new serial is processed.
//
// Product continuity now owns the accepted 200 ms latch-transition law for
// both VS-on and VS-off solves. The retained legacy Lab ordinal is normalized
// to None, preventing two overlapping continuity layers. The caller's
// engagement predicate (TwoHandLabTemporalEngagedFor) additionally stands this
// fragment down entirely whenever the product seam owns the frame, so a
// matching product output is never supplied live: on an engaged frame the
// damping target is the raw stateless orientation. Position is never altered.
//
// ConstantDamping keeps `previous` and slerps shortest-arc toward the
// product-presented target (or raw live target when no matching product output
// exists) with alpha = 1-exp(-dt/tau); tau<=eps answers the
// exact target. AdaptiveDamping shares that chain with the tranche-1
// slow/fast/fullErrorDeg blend (Normalize keeps slow>=fast and finite).
// Both seed to the target on the first valid frame after any reset, so the
// latch/activation frame itself introduces no stale-history jump, and neither
// step can return NaN.
//
// On any fail-open path (Lab-inactive solve, mode None, invalid solve,
// duplicate serial, degenerate dt) the presented orientation is the matching
// product-presented target (or raw stateless target when unavailable): temporal never feeds back into
// tracking, solver inputs, or B geometry, and telemetry counterfactual solves
// (Lab-inert inputs) always take this path.
//
// Ambient requirements (same as virtual_stock_aim_continuity_runtime.inl):
// <cmath>, <cstdint>, openxr.h (XrQuaternionf), AimPoseInputs/AimPoseResult,
// ComputeAimPose, aim_pose_trace.h, virtual_stock_logic.h,
// virtual_stock_aim_continuity.h, two_hand_lab_logic.h. No globals, no config
// reads, no logging, no file I/O, no allocation, no locks.

    struct TwoHandLabTemporalFrameSolve
    {
        bool statelessValid = false;
        virtual_stock::Quat4 stateless{};
        bool presentationBaseValid = false;
        virtual_stock::Quat4 presentationBase{};
        bool presentedValid = false;
        virtual_stock::Quat4 presented{};
        bool temporalActive = false;
        float errorDeg = 0.0f;
        two_hand_lab::TemporalMode mode = two_hand_lab::TemporalMode::None;
        // True only when this call consumed a new prepared serial (the
        // duplicate-serial and non-positive-dt guards leave it false and
        // every state untouched).
        bool advanceConsumed = false;
    };

    struct TwoHandLabTemporalState
    {
        bool initialized = false;
        uint64_t lastPreparedSerial = 0;
        bool lastLabActive = false;
        two_hand_lab::TemporalMode lastMode =
            two_hand_lab::TemporalMode::None;
        // Damping history: the last presented orientation.
        bool dampingSeeded = false;
        virtual_stock::Quat4 dampingPrevious{};
    };

    struct TwoHandLabProductPresentation
    {
        bool liveValid = false;
        virtual_stock::Quat4 live{};
        bool presentedValid = false;
        virtual_stock::Quat4 presented{};
    };

    inline void ResetTwoHandLabTemporal(
        TwoHandLabTemporalState& state) noexcept
    {
        state = TwoHandLabTemporalState{};
    }

    inline two_hand_lab::TemporalMode TwoHandLabTemporalModeFromOrdinal(
        int value) noexcept
    {
        return two_hand_lab::NormalizeTemporalMode(value);
    }

    inline float TwoHandLabTemporalFiniteOrZero(float value) noexcept
    {
        return std::isfinite(value) ? value : 0.0f;
    }

    inline TwoHandLabTemporalFrameSolve AdvanceTwoHandLabTemporalFrame(
        TwoHandLabTemporalState& state, const AimPoseInputs& labInputs,
        const two_hand_lab::Settings& settings, uint64_t preparedSerial,
        float dtSeconds, bool lastPresentedValid,
        virtual_stock::Quat4 lastPresented,
        const TwoHandLabProductPresentation& product,
        AimPoseTrace* trace) noexcept
    {
        TwoHandLabTemporalFrameSolve solve{};

        const auto toQuat = [](const XrQuaternionf& value) {
            return virtual_stock::Quat4{
                value.x, value.y, value.z, value.w};
        };

        // The frame's stateless Lab orientation: the calibrated result of the
        // Lab-active solve. One solve feeds the stateless report, the temporal
        // target, and (when traced) the frame diagnostics, so the three can
        // never disagree.
        const AimPoseResult live = trace ? ComputeAimPose(labInputs, trace)
                                         : ComputeAimPose(labInputs);
        solve.statelessValid = live.updateTwoHandActivity && live.valid &&
            virtual_stock::TryNormalizeQuaternion(
                toQuat(live.pose.orientation), solve.stateless);

        // The product seam ran earlier for this same prepared serial. Its
        // output is a valid Lab damping target only when both seams solved the
        // identical calibrated live orientation; a torn/settings-changed
        // assembly falls open to the local stateless solve.
        if (solve.statelessValid)
        {
            solve.presentationBase = solve.stateless;
            solve.presentationBaseValid = true;
            virtual_stock::Quat4 productLive{};
            virtual_stock::Quat4 productPresented{};
            if (product.liveValid && product.presentedValid &&
                virtual_stock::TryNormalizeQuaternion(
                    product.live, productLive) &&
                virtual_stock::TryNormalizeQuaternion(
                    product.presented, productPresented) &&
                productLive.x == solve.stateless.x &&
                productLive.y == solve.stateless.y &&
                productLive.z == solve.stateless.z &&
                productLive.w == solve.stateless.w)
                solve.presentationBase = productPresented;
        }

        const bool labActive =
            labInputs.twoHandLabEnabled && !labInputs.virtualStockEnabled;
        const two_hand_lab::TemporalMode mode =
            TwoHandLabTemporalModeFromOrdinal(labInputs.twoHandLabTemporal);
        solve.mode = mode;
        // Damping parameters are filter tuning only (they never steer the
        // stateless solve): normalize defensively so a torn snapshot can only
        // retune the filter, never break it. slow>=fast ordering included.
        const two_hand_lab::Settings applied =
            two_hand_lab::Normalize(settings);

        // Duplicate/out-of-order serials and degenerate timing never advance
        // any state; the caller re-presents its previous output.
        if (!preparedSerial || preparedSerial <= state.lastPreparedSerial ||
            !std::isfinite(dtSeconds) || dtSeconds <= 0.0f)
        {
            solve.presentedValid = lastPresentedValid ||
                solve.presentationBaseValid;
            solve.presented = lastPresentedValid
                ? lastPresented : solve.presentationBase;
            if (mode == two_hand_lab::TemporalMode::ConstantDamping ||
                mode == two_hand_lab::TemporalMode::AdaptiveDamping)
            {
                solve.temporalActive = state.dampingSeeded &&
                    solve.presentedValid;
                solve.errorDeg = state.dampingSeeded
                    ? TwoHandLabTemporalFiniteOrZero(
                          two_hand_lab::ShortestArcAngleDeg(
                              state.dampingPrevious,
                              solve.presentationBase))
                    : 0.0f;
            }
            return solve;
        }

        // Settings/applicability edges reset the temporal history BEFORE this
        // serial is processed: first valid frame, mode change, Lab
        // enable/disable, VS on/off. Tracking/epoch lifecycle resets arrive
        // via ResetTwoHandLabTemporal at the mirrored invalidation sites.
        if (!state.initialized || state.lastMode != mode ||
            state.lastLabActive != labActive)
            ResetTwoHandLabTemporal(state);
        state.initialized = true;
        state.lastPreparedSerial = preparedSerial;
        state.lastLabActive = labActive;
        state.lastMode = mode;
        solve.advanceConsumed = true;

        // Fail open to the product target (or stateless target if the product
        // output could not be matched): Lab-inactive solve, mode None, or an
        // invalid target. The reset above clears stale damping history.
        if (!labActive || mode == two_hand_lab::TemporalMode::None ||
            !solve.presentationBaseValid)
        {
            solve.presented = solve.presentationBase;
            solve.presentedValid = solve.presentationBaseValid;
            solve.temporalActive = false;
            solve.errorDeg = 0.0f;
            return solve;
        }

        // Constant / adaptive damping share the tranche-1 pure steps exactly.
        // Product continuity remains the sole latch transition; its presented
        // output is the damping target. Seeding that target keeps Lab
        // activation itself jump-free.
        if (!state.dampingSeeded)
        {
            state.dampingPrevious = solve.presentationBase;
            state.dampingSeeded = true;
        }
        else if (mode == two_hand_lab::TemporalMode::ConstantDamping)
        {
            state.dampingPrevious = two_hand_lab::ConstantDampingStep(
                state.dampingPrevious, solve.presentationBase,
                applied.dampingResponseMs, dtSeconds);
        }
        else
        {
            state.dampingPrevious = two_hand_lab::AdaptiveDampingStep(
                state.dampingPrevious, solve.presentationBase,
                applied.adaptiveSlowResponseMs,
                applied.adaptiveFastResponseMs,
                applied.adaptiveFullErrorDeg, dtSeconds);
        }
        solve.presented = state.dampingPrevious;
        solve.presentedValid = virtual_stock::Finite(solve.presented);
        if (!solve.presentedValid)
        {
            solve.presented = solve.presentationBase;
            solve.presentedValid = solve.presentationBaseValid;
        }
        solve.temporalActive = solve.presentedValid;
        // Damping metric: error angle between previous and target.
        solve.errorDeg = TwoHandLabTemporalFiniteOrZero(
            two_hand_lab::ShortestArcAngleDeg(
                state.dampingPrevious, solve.presentationBase));
        return solve;
    }
