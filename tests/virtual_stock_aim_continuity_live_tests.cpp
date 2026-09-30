// End-to-end test for the live grab/release aim continuity seam.
//
// This compiles the real production seam body
// (src/dll/virtual_stock_aim_continuity_runtime.inl) against the existing
// generated fixture of the production aim solver
// (tools/generate_virtual_stock_fixture.py extracts AimPoseInputs,
// ComputeAimPoseImpl, ComputeStockAimPose and ComputeAimPose from the current
// source). Nothing is modelled or reimplemented: the assertions are the
// shipping contract itself.
//
// The product contract, exercised for Standard Virtual Stock (rear reference
// 0/1, strength 0.95) and Plus Virtual Stock (rear reference 3):
//   * acquisition: the latch serial presents the same-frame one-hand
//     counterfactual, then a fixed 200 ms smoothstep reaches identity;
//   * release: the un-latch serial keeps the previously presented aim, then
//     the same fixed 200 ms smoothstep reaches the live one-hand aim;
//   * the presented orientation always follows the LIVE target of the frame,
//     never a frozen seed;
//   * rapid reversals seed from what is currently presented (no snap);
//   * one advance per prepared serial, no hidden per-frame reset;
//   * the layer never alters a solver's live target and never mutates caller
//     inputs, and whenever it is inactive presented is bit-identical to live.
#include <openxr/openxr.h>
#include "../src/common/two_hand_input_smoothing.h"
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/virtual_stock_aim_continuity.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/dll/aim_pose_trace.h"
#include "virtual_stock_fixture.inl"
#include "../src/dll/virtual_stock_aim_continuity_runtime.inl"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
void Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

bool Close(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

bool SameQuat(virtual_stock::Quat4 a, virtual_stock::Quat4 b)
{
    return Close(a.x, b.x, 1.0e-6f) && Close(a.y, b.y, 1.0e-6f) &&
        Close(a.z, b.z, 1.0e-6f) && Close(a.w, b.w, 1.0e-6f);
}

bool ExactQuat(virtual_stock::Quat4 a, virtual_stock::Quat4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool CloseRotation(virtual_stock::Quat4 a, virtual_stock::Quat4 b,
    float degrees)
{
    const float angle = virtual_stock::Quat4AngleDegrees(a, b);
    return std::isfinite(angle) && angle <= degrees;
}

float RotationDegrees(virtual_stock::Quat4 a, virtual_stock::Quat4 b)
{
    return virtual_stock::Quat4AngleDegrees(a, b);
}

// The deterministic ease clock: 1/64 is exact in binary, so 12 advances after
// the seed reach 187.5 ms and the 13th reaches 203.125 ms (>= 200 ms).
constexpr float kDt = 1.0f / 64.0f;

// One reach/stock geometry, held identical across modes so the only variable
// is the Virtual Stock mode.
AimPoseInputs StockInputs(int rearReference, float strength, bool latched)
{
    AimPoseInputs input{};
    input.rightValid = true;
    input.leftValid = true;
    input.twoHandEnabled = true;
    input.twoHandLatched = latched;
    input.right.position = {0.15f, 1.30f, -0.25f};
    input.right.orientation = {0.0f, 0.0871557f, 0.0f, 0.9961947f};
    input.left.position = {0.10f, 1.29f, -0.60f};
    input.supportPosition = input.left.position;
    input.headValid = true;
    input.headPosition = {0.0f, 1.60f, 0.0f};
    input.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    input.virtualStockEnabled = true;
    input.virtualStockStrength = strength;
    input.virtualStockRearReference = rearReference;
    // Neutral hybrid/seat fields: the seat is always fully engaged (10 m) and
    // the legacy proximity/horizontal/inverse-neck helpers stay off, so the
    // solve is the mode's own stock geometry and nothing else.
    input.virtualStockHybridOffhandInfluence = 0.0f;
    input.virtualStockHybridAdsReference = 0;
    input.virtualStockHybridSeatFullM = 10.0f;
    input.virtualStockHybridSeatReleaseM = 11.0f;
    input.hybridHorizontalRearReleaseEnabled = false;
    input.hybridInverseNeckEnabled = false;
    input.virtualStockProximityRelease = false;
    input.gunYawDeg = 0.0f;
    input.gunPitchDeg = 0.0f;
    input.gunRollDeg = 0.0f;
    return input;
}

struct StockMode
{
    const char* name;
    int rearReference;
    float strength;
};

// Standard product strength is 0.95; Plus keeps the validated fixture's full
// strength so the latch is a large, non-vacuous orientation change.
const StockMode kStockModes[] = {
    {"Standard-Head", 0, 0.95f},
    {"Standard-Shoulder", 1, 0.95f},
    {"Plus", 3, 1.0f},
};

// Drives the production seam exactly the way the live caller does: one
// advance per prepared serial, feeding back the orientation it presented.
struct Driver
{
    virtual_stock::AimContinuityState state{};
    uint64_t serial = 0;
    bool lastPresentedValid = false;
    virtual_stock::Quat4 lastPresented{};

    AimContinuityFrameSolve Step(const AimPoseInputs& inputs, float dt = kDt)
    {
        const AimContinuityFrameSolve solve = AdvanceAimContinuityFrame(
            state, inputs, ++serial, dt, lastPresentedValid, lastPresented);
        lastPresentedValid = solve.presentedValid;
        if (solve.presentedValid)
            lastPresented = solve.presented;
        return solve;
    }

    AimContinuityFrameSolve StepSameSerial(
        const AimPoseInputs& inputs, float dt = kDt)
    {
        return AdvanceAimContinuityFrame(
            state, inputs, serial, dt, lastPresentedValid, lastPresented);
    }
};

// Production reference solves for the same inputs, used to prove the seam
// computed exactly the production live/counterfactual aims.
virtual_stock::Quat4 Orientation(const AimPoseResult& result)
{
    return {result.pose.orientation.x, result.pose.orientation.y,
        result.pose.orientation.z, result.pose.orientation.w};
}

void CheckProductionAims(
    const AimPoseInputs& latched, const AimContinuityFrameSolve& solve,
    const char* modeName)
{
    AimPoseInputs counterfactual = latched;
    counterfactual.twoHandLatched = false;
    const AimPoseResult liveReference = ComputeAimPose(latched);
    const AimPoseResult oneHandReference = ComputeAimPose(counterfactual);
    Check(liveReference.valid && liveReference.twoHandActive &&
            oneHandReference.valid && oneHandReference.updateTwoHandActivity,
        "the fixture produces a valid stocked and one-hand solve");
    Check(solve.liveValid && solve.oneHandValid,
        "the seam computed both the live and the one-hand counterfactual");
    Check(CloseRotation(solve.live, Orientation(liveReference), 1.0e-4f),
        "the seam's live solve is the production stocked solve");
    Check(CloseRotation(solve.oneHand, Orientation(oneHandReference), 1.0e-4f),
        "the seam's counterfactual is the production one-hand solve");
    const float hardSwitch = RotationDegrees(
        solve.live, solve.oneHand);
    Check(std::isfinite(hardSwitch) && hardSwitch > 3.0f,
        "the latch is a real orientation change in this mode");
    std::cout << "  " << modeName << ": latch swing "
              << hardSwitch << " deg\n";
}

void TestAcquisitionForEveryStockMode()
{
    for (const StockMode& mode : kStockModes)
    {
        const AimPoseInputs idle = StockInputs(
            mode.rearReference, mode.strength, false);
        const AimPoseInputs latched = StockInputs(
            mode.rearReference, mode.strength, true);
        Driver driver;

        // Baseline: an unlatched serial is never an edge, and an inactive
        // layer presents the live solve bit-identically.
        const AimContinuityFrameSolve baseline = driver.Step(idle);
        Check(!baseline.correctionActive && !driver.state.active,
            "an unlatched baseline serial is not a continuity edge");
        Check(ExactQuat(baseline.presented, baseline.live),
            "an inactive layer presents the live aim bit-identically");

        const AimContinuityFrameSolve grab = driver.Step(latched);
        CheckProductionAims(latched, grab, mode.name);
        Check(grab.correctionActive && driver.state.active,
            "the latch serial seeds an active transition");
        Check(driver.state.phase == virtual_stock::AimContinuityPhase::Acquire,
            "the latch serial enters the acquire phase");
        Check(driver.state.edgeKind == virtual_stock::AimContinuityEdgeKind::Grab,
            "the latch serial reports a grab edge");
        Check(driver.state.anchorSource ==
                virtual_stock::AimContinuityAnchorSource::SameFrameOneHand,
            "the latch serial anchors on the same-frame one-hand aim");
        Check(CloseRotation(grab.presented, grab.oneHand, 1.0e-4f),
            "presented == the same-frame one-hand counterfactual on the latch "
            "serial");
        Check(!CloseRotation(grab.presented, grab.live, 3.0f),
            "the latch serial does not hard-switch to the stocked aim");

        // The fixed 200 ms smoothstep: still active and monotonic at 187.5 ms,
        // exact identity at 203.125 ms.
        float previousRemaining = RotationDegrees(grab.presented, grab.live);
        Check(previousRemaining > 3.0f, "the seed correction is non-trivial");
        for (int frame = 0; frame < 12; ++frame)
        {
            const AimContinuityFrameSolve step = driver.Step(latched);
            Check(driver.state.active,
                "the acquire ease is still active before 200 ms");
            const float remaining = RotationDegrees(step.presented, step.live);
            Check(remaining <= previousRemaining + 1.0e-3f,
                "the acquire presented-to-live angle never increases");
            previousRemaining = remaining;
        }
        const float t = 187.5f / 200.0f;
        const float smooth = t * t * (3.0f - 2.0f * t);
        const float initial = RotationDegrees(grab.presented, grab.live);
        Check(Close(previousRemaining, initial * (1.0f - smooth), 0.05f),
            "acquisition follows smoothstep(elapsed / 200 ms)");
        Check(previousRemaining > 0.0f, "acquisition is not finished at 187.5 ms");

        const AimContinuityFrameSolve settled = driver.Step(latched);
        Check(!driver.state.active &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Idle,
            "acquisition completes at >= 200 ms");
        Check(driver.state.correction.x == 0.0f &&
                driver.state.correction.y == 0.0f &&
                driver.state.correction.z == 0.0f &&
                driver.state.correction.w == 1.0f,
            "acquisition reaches exact identity at >= 200 ms");
        Check(ExactQuat(settled.presented, settled.live),
            "a completed acquisition presents the live stocked aim exactly");
    }
}

void TestReleaseForEveryStockMode()
{
    for (const StockMode& mode : kStockModes)
    {
        const AimPoseInputs idle = StockInputs(
            mode.rearReference, mode.strength, false);
        const AimPoseInputs latched = StockInputs(
            mode.rearReference, mode.strength, true);
        Driver driver;

        driver.Step(idle);
        const AimContinuityFrameSolve grab = driver.Step(latched);
        Check(grab.correctionActive, "the latch serial seeds a transition");
        for (int frame = 0; frame < 13; ++frame)
            driver.Step(latched);
        Check(!driver.state.active,
            "the acquisition completed before the release fixture");
        const virtual_stock::Quat4 presentedBeforeRelease = driver.lastPresented;

        const AimContinuityFrameSolve release = driver.Step(idle);
        Check(release.correctionActive && driver.state.active,
            "the release serial seeds an active transition");
        Check(driver.state.phase == virtual_stock::AimContinuityPhase::Release,
            "the release serial enters the release phase");
        Check(driver.state.edgeKind ==
                virtual_stock::AimContinuityEdgeKind::Release,
            "the release serial reports a release edge");
        Check(driver.state.anchorSource ==
                virtual_stock::AimContinuityAnchorSource::LastPresented,
            "the release serial anchors on last presented");
        Check(CloseRotation(release.presented, presentedBeforeRelease, 0.05f),
            "the release serial keeps the aim the player was already seeing");
        const float releaseOffset = RotationDegrees(
            release.presented, release.live);
        Check(releaseOffset > 3.0f,
            "the release correction is a real offset from the live one-hand "
            "aim");

        // The same fixed 200 ms smoothstep, completed on the 13th advance.
        float previousRemaining = releaseOffset;
        for (int frame = 0; frame < 12; ++frame)
        {
            const AimContinuityFrameSolve step = driver.Step(idle);
            Check(driver.state.active,
                "the release ease is still active before 200 ms");
            const float remaining = RotationDegrees(step.presented, step.live);
            Check(remaining <= previousRemaining + 1.0e-3f,
                "the release presented-to-live angle never increases");
            previousRemaining = remaining;
        }
        const float t = 187.5f / 200.0f;
        const float smooth = t * t * (3.0f - 2.0f * t);
        Check(Close(previousRemaining, releaseOffset * (1.0f - smooth), 0.05f),
            "release follows the same smoothstep law as acquisition");
        Check(previousRemaining > 0.0f, "release is not finished at 187.5 ms");

        const AimContinuityFrameSolve settled = driver.Step(idle);
        Check(!driver.state.active &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Idle,
            "release completes at >= 200 ms");
        Check(ExactQuat(settled.presented, settled.live),
            "after the release ease presented == the live one-hand aim "
            "exactly");
        Check(!CloseRotation(settled.presented, presentedBeforeRelease, 3.0f),
            "the completed release really returned to the one-hand aim");
    }
}

void TestMovingLiveTargetForEveryStockMode()
{
    for (const StockMode& mode : kStockModes)
    {
        const AimPoseInputs latched = StockInputs(
            mode.rearReference, mode.strength, true);
        Driver driver;
        driver.Step(StockInputs(mode.rearReference, mode.strength, false));
        const AimContinuityFrameSolve grab = driver.Step(latched);
        const virtual_stock::Quat4 seedLive = grab.live;

        // Move the stock geometry while the acquire ease decays (the primary
        // hand alone does not move a stocked solve in every mode): presented
        // must be live(that frame) (x) correction, and must track the new live
        // aim rather than the frozen seed aim.
        bool liveTargetMoved = false;
        bool trackedMovingTarget = false;
        float previousAngleFromLive = RotationDegrees(grab.presented, grab.live);
        for (int frame = 0; frame < 12; ++frame)
        {
            AimPoseInputs moved = latched;
            moved.supportPosition = {
                0.10f + 0.006f * (frame + 1), 1.29f, -0.60f};
            moved.left.position = moved.supportPosition;
            const AimContinuityFrameSolve step = driver.Step(moved);
            if (!step.liveValid)
                throw std::runtime_error("a moved live target must stay valid");
            const virtual_stock::Quat4 composed = virtual_stock::MultiplyQuat4(
                step.live, driver.state.correction);
            Check(CloseRotation(step.presented, composed, 0.01f),
                "presented == live(that frame) (x) correction");
            const float angleFromLive = RotationDegrees(step.presented, step.live);
            Check(angleFromLive <= previousAngleFromLive + 1.0e-3f,
                "the acquire presented-to-live angle never increases while the "
                "target moves");
            previousAngleFromLive = angleFromLive;
            const float movedLive = RotationDegrees(step.live, seedLive);
            if (movedLive > 1.0f)
                liveTargetMoved = true;
            const float frozenAngle = RotationDegrees(step.presented,
                virtual_stock::MultiplyQuat4(seedLive, driver.state.correction));
            if (frozenAngle > 1.0f)
                trackedMovingTarget = true;
        }
        Check(liveTargetMoved,
            "the stocked live target really moved during the ease");
        Check(trackedMovingTarget,
            "the ease tracks the moving live target, not the frozen seed");

        // Release while the target keeps moving: same property.
        const AimPoseInputs idle = StockInputs(
            mode.rearReference, mode.strength, false);
        driver.Step(latched);
        for (int frame = 0; frame < 2; ++frame)
            driver.Step(latched);
        const AimContinuityFrameSolve release = driver.Step(idle);
        Check(release.correctionActive &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Release,
            "mid-target-motion release seeds the release phase");
        float previousReleaseFromLive =
            RotationDegrees(release.presented, release.live);
        for (int frame = 0; frame < 12; ++frame)
        {
            AimPoseInputs moved = idle;
            moved.right.orientation = {
                0.0f, std::sin((4.0f - frame * 0.5f) * 0.5f * 0.01745329252f),
                0.0f,
                std::cos((4.0f - frame * 0.5f) * 0.5f * 0.01745329252f)};
            const AimContinuityFrameSolve step = driver.Step(moved);
            const virtual_stock::Quat4 composed = virtual_stock::MultiplyQuat4(
                step.live, driver.state.correction);
            Check(CloseRotation(step.presented, composed, 0.01f),
                "released presented == live(that frame) (x) correction");
            const float angleFromLive = RotationDegrees(step.presented, step.live);
            Check(angleFromLive <= previousReleaseFromLive + 1.0e-3f,
                "the release presented-to-live angle never increases while the "
                "target moves");
            previousReleaseFromLive = angleFromLive;
        }
    }
}

void TestRapidReversalsForEveryStockMode()
{
    for (const StockMode& mode : kStockModes)
    {
        const AimPoseInputs idle = StockInputs(
            mode.rearReference, mode.strength, false);
        const AimPoseInputs latched = StockInputs(
            mode.rearReference, mode.strength, true);
        Driver driver;
        driver.Step(idle);
        driver.Step(latched);
        for (int frame = 0; frame < 4; ++frame)
            driver.Step(latched);
        Check(driver.state.active &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Acquire,
            "the acquisition is mid-ease before the rapid release");

        // Release mid-acquire: no discontinuity.
        const virtual_stock::Quat4 beforeRelease = driver.lastPresented;
        const AimContinuityFrameSolve release = driver.Step(idle);
        Check(release.correctionActive &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Release,
            "grabbing then releasing mid-acquire enters the release phase");
        Check(CloseRotation(release.presented, beforeRelease, 0.05f),
            "the release mid-acquire introduces no discontinuity");

        // Re-grab mid-release: no discontinuity.
        for (int frame = 0; frame < 3; ++frame)
            driver.Step(idle);
        Check(driver.state.active &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Release,
            "the release is still active at the re-grab");
        const virtual_stock::Quat4 beforeRegrab = driver.lastPresented;
        const AimContinuityFrameSolve regrab = driver.Step(latched);
        Check(regrab.correctionActive &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Acquire,
            "the re-grab mid-release re-enters the acquire phase");
        Check(driver.state.edgeKind ==
                virtual_stock::AimContinuityEdgeKind::Grab,
            "the re-grab reports a grab edge");
        Check(CloseRotation(regrab.presented, beforeRegrab, 0.05f),
            "the re-grab mid-release introduces no discontinuity");

        // The re-entrant acquisition still completes to the live aim.
        AimContinuityFrameSolve lastSolve = regrab;
        for (int frame = 0; frame < 13; ++frame)
            lastSolve = driver.Step(latched);
        Check(!driver.state.active,
            "the re-entrant acquisition completes on schedule");
        Check(ExactQuat(lastSolve.presented, lastSolve.live),
            "the completed re-entrant acquisition presents the live aim");
    }
}

void TestOncePerSerialAndConsecutiveSerials()
{
    const AimPoseInputs idle = StockInputs(3, 1.0f, false);
    const AimPoseInputs latched = StockInputs(3, 1.0f, true);
    Driver driver;
    driver.Step(idle);
    driver.Step(latched);
    for (int frame = 0; frame < 4; ++frame)
        driver.Step(latched);

    // Repeating the same prepared serial must not advance the layer.
    const uint64_t serialBefore = driver.state.lastPreparedSerial;
    const virtual_stock::Quat4 correctionBefore = driver.state.correction;
    const float elapsedBefore = driver.state.elapsedSeconds;
    const AimContinuityFrameSolve duplicate = driver.StepSameSerial(latched);
    const AimContinuityFrameSolve duplicateAgain = driver.StepSameSerial(latched);
    Check(!duplicate.advanceConsumed && !duplicateAgain.advanceConsumed,
        "a duplicate prepared serial never advances the layer");
    Check(driver.state.lastPreparedSerial == serialBefore &&
            ExactQuat(driver.state.correction, correctionBefore) &&
            driver.state.elapsedSeconds == elapsedBefore,
        "a duplicate prepared serial leaves the state untouched");
    Check(duplicate.presentedValid,
        "a duplicate serial still reports the current presented orientation");

    // A non-positive delta is a no-op and leaves the serial unconsumed.
    const uint64_t nextSerial = serialBefore + 1;
    const AimContinuityFrameSolve skipped = AdvanceAimContinuityFrame(
        driver.state, latched, nextSerial, 0.0f, true, driver.lastPresented);
    Check(!skipped.advanceConsumed &&
            driver.state.lastPreparedSerial == serialBefore,
        "a non-positive dt does not consume the prepared serial");
    const AimContinuityFrameSolve consumed = AdvanceAimContinuityFrame(
        driver.state, latched, nextSerial, kDt, true, driver.lastPresented);
    Check(consumed.advanceConsumed &&
            driver.state.lastPreparedSerial == nextSerial,
        "the next valid call consumes the same serial");

    // A consecutive-serial lifetime: the same state object must carry the
    // acquisition to completion and then a whole release. The live
    // integration's routine prepared-frame retire (SubmitPreparedFrame ->
    // ResetPreparedFrame) deliberately does not reset this state; only the
    // explicit exceptional invalidation call sites do. A hidden per-frame
    // reset would make the next serial a first observation and no edge could
    // ever seed.
    Driver lifetime;
    lifetime.Step(idle);
    const AimContinuityFrameSolve firstGrab = lifetime.Step(latched);
    Check(firstGrab.correctionActive,
        "the lifetime fixture seeds its acquisition on the second serial");
    for (int frame = 0; frame < 12; ++frame)
        lifetime.Step(latched);
    Check(lifetime.state.active,
        "the acquisition is still easing after 12 consecutive serials");
    AimContinuityFrameSolve lastSolve = lifetime.Step(latched);
    Check(!lifetime.state.active,
        "the acquisition completes across consecutive serials without a reset");
    Check(ExactQuat(lastSolve.presented, lastSolve.live),
        "the completed acquisition presents the live aim exactly");
    lifetime.Step(idle);
    Check(lifetime.state.active &&
            lifetime.state.phase == virtual_stock::AimContinuityPhase::Release,
        "the release edge is observed on the next consecutive serial");
    for (int frame = 0; frame < 12; ++frame)
        lastSolve = lifetime.Step(idle);
    Check(lifetime.state.active,
        "the release is still easing after 12 consecutive serials");
    lastSolve = lifetime.Step(idle);
    Check(!lifetime.state.active,
        "the release completes across consecutive serials without a reset");
    Check(ExactQuat(lastSolve.presented, lastSolve.live),
        "the completed release presents the live aim exactly");
    Check(lifetime.state.lastPreparedSerial == lifetime.serial,
        "the module tracked every consecutive prepared serial");
}

void TestInvalidationAndFailOpen()
{
    const AimPoseInputs latched = StockInputs(3, 1.0f, true);
    const AimPoseInputs idle = StockInputs(3, 1.0f, false);

    // Reset clears to identity and idle, and the first observation after a
    // reset is never an edge.
    Driver driver;
    driver.Step(idle);
    driver.Step(latched);
    driver.Step(latched);
    Check(driver.state.active && !ExactQuat(driver.state.correction,
            virtual_stock::Quat4{0.0f, 0.0f, 0.0f, 1.0f}),
        "the fixture has an active transition before Reset");
    virtual_stock::ResetAimContinuity(driver.state);
    const auto diagnostics = virtual_stock::ReadAimContinuityDiagnostics(
        driver.state);
    Check(!diagnostics.active &&
            diagnostics.phase == virtual_stock::AimContinuityPhase::Idle &&
            diagnostics.edge_kind == virtual_stock::AimContinuityEdgeKind::None &&
            diagnostics.anchor_source ==
                virtual_stock::AimContinuityAnchorSource::None &&
            diagnostics.initial_correction_deg == 0.0f &&
            diagnostics.remaining_correction_deg == 0.0f &&
            diagnostics.last_prepared_serial == 0,
        "Reset clears to identity and idle");
    const AimContinuityFrameSolve postReset = driver.Step(latched);
    Check(ExactQuat(postReset.presented, postReset.live),
        "the first observation after Reset presents the live aim exactly");
    Check(!postReset.correctionActive,
        "an already-latched first observation after Reset is not an edge");
    const AimContinuityFrameSolve postResetRelease = driver.Step(idle);
    Check(postResetRelease.correctionActive &&
            driver.state.phase == virtual_stock::AimContinuityPhase::Release,
        "genuine edges are processed normally after Reset");

    // An invalid primary pose fails open: no transition, no fabricated
    // presented orientation, and the caller's inputs are untouched.
    Driver invalid;
    invalid.Step(idle);
    AimPoseInputs noPrimary = latched;
    noPrimary.rightValid = false;
    const AimPoseInputs untouched = noPrimary;
    const AimContinuityFrameSolve invalidSolve = invalid.Step(noPrimary);
    Check(!invalidSolve.liveValid && !invalidSolve.presentedValid,
        "an invalid primary pose never fabricates a presented orientation");
    Check(!invalid.state.active &&
            invalid.state.anchorSource ==
                virtual_stock::AimContinuityAnchorSource::Identity,
        "an invalid primary pose at a grab edge fails open to identity");
    Check(std::memcmp(&untouched, &noPrimary, sizeof(untouched)) == 0,
        "the seam never mutates the caller's solver inputs");
}

void TestSolverIsolationAndImmutability()
{
    for (const StockMode& mode : kStockModes)
    {
        const AimPoseInputs idle = StockInputs(
            mode.rearReference, mode.strength, false);
        const AimPoseInputs latched = StockInputs(
            mode.rearReference, mode.strength, true);

        // A layer running through whole acquire/release cycles must not change
        // what the solver returns for the same inputs (bit-identical), and
        // must not mutate the caller's inputs.
        const AimPoseResult referenceLatched = ComputeAimPose(latched);
        const AimPoseResult referenceIdle = ComputeAimPose(idle);
        AimPoseInputs callerLatched = latched;
        const AimPoseInputs callerSnapshot = callerLatched;

        Driver driver;
        driver.Step(idle);
        driver.Step(callerLatched);
        for (int frame = 0; frame < 13; ++frame)
            driver.Step(callerLatched);
        driver.Step(idle);
        for (int frame = 0; frame < 13; ++frame)
            driver.Step(idle);
        driver.Step(callerLatched);

        const AimPoseResult afterLatched = ComputeAimPose(callerLatched);
        const AimPoseResult afterIdle = ComputeAimPose(idle);
        Check(ExactQuat(Orientation(afterLatched), Orientation(referenceLatched)) &&
                afterLatched.pose.position.x ==
                    referenceLatched.pose.position.x &&
                afterLatched.pose.position.y ==
                    referenceLatched.pose.position.y &&
                afterLatched.pose.position.z ==
                    referenceLatched.pose.position.z &&
                afterLatched.valid == referenceLatched.valid &&
                afterLatched.twoHandActive == referenceLatched.twoHandActive,
            "the continuity layer never alters the solver's live target");
        Check(ExactQuat(Orientation(afterIdle), Orientation(referenceIdle)) &&
                afterIdle.pose.position.x == referenceIdle.pose.position.x &&
                afterIdle.pose.position.y == referenceIdle.pose.position.y &&
                afterIdle.pose.position.z == referenceIdle.pose.position.z &&
                afterIdle.twoHandActive == referenceIdle.twoHandActive,
            "the continuity layer never alters the one-hand target");
        Check(std::memcmp(&callerSnapshot, &callerLatched,
                sizeof(callerLatched)) == 0,
            "the continuity layer never mutates the caller's solver inputs");

        // Whenever the layer is inactive, presented is bit-identical to live.
        const AimContinuityFrameSolve grabbed = driver.Step(callerLatched);
        Check(grabbed.correctionActive,
            "the isolation fixture seeds an active transition");
        for (int frame = 0; frame < 3; ++frame)
            driver.Step(callerLatched);
        const virtual_stock::Quat4 beforeRelease = driver.lastPresented;
        const AimContinuityFrameSolve released = driver.Step(idle);
        Check(released.correctionActive &&
                driver.state.phase == virtual_stock::AimContinuityPhase::Release,
            "the isolation release seeds an active transition");
        Check(CloseRotation(released.presented, beforeRelease, 0.05f),
            "the isolation release keeps the presented aim");
        for (int frame = 0; frame < 13; ++frame)
            driver.Step(idle);
        const AimContinuityFrameSolve settled = driver.Step(idle);
        Check(!driver.state.active,
            "the isolation release completes");
        Check(ExactQuat(settled.presented, settled.live),
            "an inactive layer presents the live aim bit-identically");
    }
}
// ---------------------------------------------------------------------------
// Composition order: Pavlov-inspired input smoothing runs FIRST, and the
// continuity layer composes its live (x) correction onto the SMOOTHED solve.
// The raw capture, the latch/acquisition sample and the weapon base position
// stay untouched by both stages.
// ---------------------------------------------------------------------------
two_hand_input_smoothing::Sample SmoothingSampleOf(const AimPoseInputs& inputs)
{
    two_hand_input_smoothing::Sample sample{};
    sample.primaryOrientation = {inputs.right.orientation.x,
        inputs.right.orientation.y, inputs.right.orientation.z,
        inputs.right.orientation.w};
    sample.primaryAimPosition = {inputs.right.position.x,
        inputs.right.position.y, inputs.right.position.z};
    sample.supportAimPosition = {inputs.left.position.x,
        inputs.left.position.y, inputs.left.position.z};
    sample.primaryGripValid = inputs.primaryGripValid;
    sample.primaryGripPosition = {inputs.primaryGripPosition.x,
        inputs.primaryGripPosition.y, inputs.primaryGripPosition.z};
    sample.supportGripValid = inputs.supportGripValid;
    sample.supportGripPosition = {inputs.supportGripPosition.x,
        inputs.supportGripPosition.y, inputs.supportGripPosition.z};
    return sample;
}

// Mirrors the production consumer mapping: the filtered copies replace only
// the directional solve geometry; the raw pose fields, the latch and the base
// position are untouched.
AimPoseInputs WithFilteredGeometry(
    const AimPoseInputs& inputs,
    const two_hand_input_smoothing::Result& filtered)
{
    AimPoseInputs out = inputs;
    out.twoHandSmoothingGeometryValid = true;
    out.twoHandSmoothedPrimaryOrientation = {
        filtered.filtered.primaryOrientation.x,
        filtered.filtered.primaryOrientation.y,
        filtered.filtered.primaryOrientation.z,
        filtered.filtered.primaryOrientation.w};
    out.twoHandSmoothedPrimaryAimPosition = {
        filtered.filtered.primaryAimPosition.x,
        filtered.filtered.primaryAimPosition.y,
        filtered.filtered.primaryAimPosition.z};
    out.twoHandSmoothedSupportAimPosition = {
        filtered.filtered.supportAimPosition.x,
        filtered.filtered.supportAimPosition.y,
        filtered.filtered.supportAimPosition.z};
    out.twoHandSmoothedPrimaryGripValid = filtered.filtered.primaryGripValid;
    out.twoHandSmoothedPrimaryGripPosition = {
        filtered.filtered.primaryGripPosition.x,
        filtered.filtered.primaryGripPosition.y,
        filtered.filtered.primaryGripPosition.z};
    out.twoHandSmoothedSupportGripValid = filtered.filtered.supportGripValid;
    out.twoHandSmoothedSupportGripPosition = {
        filtered.filtered.supportGripPosition.x,
        filtered.filtered.supportGripPosition.y,
        filtered.filtered.supportGripPosition.z};
    return out;
}

void TestSmoothingComposesBeforeContinuity()
{
    using two_hand_input_smoothing::Advance;

    // One VS-OFF fixture: no Virtual Stock, both controllers valid, primary
    // grip present. The yaw/position parameters let one serial of raw motion
    // be replayed both through the smoothing filter and as the raw solve.
    const auto frameAt = [](float yawDeg, float primaryX, float supportZ) {
        AimPoseInputs frame = StockInputs(3, 1.0f, true);
        frame.virtualStockEnabled = false;
        frame.primaryGripValid = true;
        frame.supportGripValid = true;
        frame.supportEndpointUsedGrip = false;
        const float half = yawDeg * 0.5f * 0.01745329252f;
        frame.right.orientation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
        frame.right.position = {primaryX, 1.30f, -0.25f};
        frame.left.position = {0.10f, 1.29f, supportZ};
        frame.supportPosition = frame.left.position;
        frame.primaryGripPosition = frame.right.position;
        frame.supportGripPosition = frame.left.position;
        return frame;
    };
    const auto frameYaw = [&](int index) {
        // A fast lateral swing (3 deg per 64 Hz prepared frame) that the
        // smoothing layer genuinely lags; the latch swing stays well inside
        // the support-agreement acceptance.
        return frameAt(3.0f * static_cast<float>(index), 0.15f,
            -0.60f - 0.005f * static_cast<float>(index));
    };

    AimPoseInputs idle = frameYaw(0);
    idle.twoHandLatched = false;
    const AimPoseResult latchedReference = ComputeAimPose(frameYaw(0));
    Check(latchedReference.valid && latchedReference.twoHandActive,
        "the VS-OFF smoothing/continuity fixture engages the two-hand solve");

    // The smoothing advance clock is its own fixed 10 ms step (exact alpha
    // 0.25); the continuity driver keeps the 1/64 prepared-frame step.
    constexpr float kSmoothingDt = 0.01f;

    Driver driver;
    two_hand_input_smoothing::State filter{};
    // Serial 1: unlatched baseline. Production does not even run the filter
    // without the durable latch, so no filtered copies exist and the filter
    // history is still empty.
    driver.Step(idle);

    // Serial 2: the latch edge. The filter seeds its first eligible serial
    // exactly raw, so the copies are present and equal to the raw geometry.
    const auto seeded = Advance(
        filter, 2, kSmoothingDt, SmoothingSampleOf(frameYaw(0)));
    Check(seeded.valid && seeded.advanced,
        "the smoothing layer seeds its first latched prepared serial");
    const AimContinuityFrameSolve grab =
        driver.Step(WithFilteredGeometry(frameYaw(0), seeded));
    Check(grab.correctionActive &&
            driver.state.edgeKind == virtual_stock::AimContinuityEdgeKind::Grab &&
            CloseRotation(grab.presented, grab.oneHand, 1.0e-4f),
        "the latch edge still anchors on the same-frame one-hand aim with smoothing upstream");

    // Serial 3: the raw primary arm has already moved, so the filtered copy is
    // a genuine blend that differs from the raw solve. Continuity's live
    // target must be the SMOOTHED solve.
    AimPoseInputs raw3 = frameYaw(1);
    const auto blended = Advance(filter, 3, kSmoothingDt, SmoothingSampleOf(raw3));
    Check(blended.valid && blended.advanced && Close(blended.alpha, 0.25f, 1.0e-6f),
        "the smoothing layer applies its fixed speed-25 alpha while moving");
    const AimPoseInputs smoothed3 = WithFilteredGeometry(raw3, blended);
    const AimPoseResult smoothedReference = ComputeAimPose(smoothed3);
    const AimPoseResult rawReference = ComputeAimPose(raw3);
    Check(smoothedReference.valid && smoothedReference.twoHandActive &&
            rawReference.valid && rawReference.twoHandActive,
        "both the filtered and the raw solve are valid two-hand solves");
    Check(RotationDegrees(Orientation(smoothedReference), Orientation(rawReference)) >
            0.05f,
        "the filtered geometry really differs from the raw solve in this fixture");
    const AimContinuityFrameSolve step3 = driver.Step(smoothed3);
    Check(step3.liveValid &&
            CloseRotation(step3.live, Orientation(smoothedReference), 1.0e-4f),
        "continuity's live target is the smoothed solve, so smoothing runs first");
    const virtual_stock::Quat4 composed = virtual_stock::MultiplyQuat4(
        step3.live, driver.state.correction);
    Check(CloseRotation(step3.presented, composed, 0.01f),
        "continuity composes its correction onto the smoothed live aim");

    // Keep a constant raw motion flowing until the acquire ease completes. The
    // smoothing lag stays bounded, so at completion the presented aim is the
    // smoothed live solve, measurably away from the raw one.
    AimPoseInputs rawFinal = raw3;
    AimPoseResult finalReference{};
    AimContinuityFrameSolve settled = step3;
    for (int frame = 0; frame < 14; ++frame)
    {
        rawFinal = frameYaw(2 + frame);
        const auto filtered = Advance(filter, 4 + static_cast<uint64_t>(frame),
            kSmoothingDt, SmoothingSampleOf(rawFinal));
        const AimPoseInputs inputs = WithFilteredGeometry(rawFinal, filtered);
        settled = driver.Step(inputs);
        finalReference = ComputeAimPose(inputs);
        Check(CloseRotation(settled.live, Orientation(finalReference), 1.0e-4f),
            "the continuity target keeps tracking the smoothed solve every serial");
    }
    Check(!driver.state.active,
        "the acquisition completes on schedule with the smoothing layer upstream");
    Check(CloseRotation(settled.presented, settled.live, 1.0e-4f),
        "the completed acquisition presents the smoothed live aim exactly");
    Check(RotationDegrees(settled.presented, Orientation(ComputeAimPose(rawFinal))) >
            0.05f,
        "the completed presentation is the lagging smoothed aim, not the raw aim");
    std::cout << "  smoothing lag at completion: "
              << RotationDegrees(
                     settled.presented, Orientation(ComputeAimPose(rawFinal)))
              << " deg\n";
    Check(finalReference.valid && finalReference.twoHandActive &&
            finalReference.pose.position.x == rawFinal.right.position.x &&
            finalReference.pose.position.y == rawFinal.right.position.y &&
            finalReference.pose.position.z == rawFinal.right.position.z,
        "the presented base position stays the raw primary position");
}
} // namespace

int main()
{
    try
    {
        TestAcquisitionForEveryStockMode();
        TestReleaseForEveryStockMode();
        TestMovingLiveTargetForEveryStockMode();
        TestRapidReversalsForEveryStockMode();
        TestOncePerSerialAndConsecutiveSerials();
        TestInvalidationAndFailOpen();
        TestSolverIsolationAndImmutability();
        TestSmoothingComposesBeforeContinuity();
        std::cout << "aim continuity live seam: Standard (rear 0/1) and Plus "
                     "(rear 3) acquisition/release continuity, moving live "
                     "target, rapid reversals, once-per-serial lifetime, "
                     "invalidation, fail-open, solver isolation and "
                     "smoothing-then-continuity composition passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
