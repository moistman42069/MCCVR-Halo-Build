// End-to-end test for the Two-Hand Lab temporal experiment modes.
//
// This compiles the real production temporal seam body
// (src/dll/two_hand_lab_temporal_runtime.inl) against the existing generated
// fixture of the production aim solver (tools/generate_aim_pose_fixture.py
// extracts AimPoseInputs, ComputeAimPoseImpl and ComputeAimPose from the
// current source). Nothing is modelled or reimplemented: the assertions are
// the shipping contract itself. Product latch-transition continuity is owned
// only by the product continuity seam; the Lab retains only Constant/Adaptive
// damping experiments.
//
// Proven here, for the Lab-active VS-off solve:
//   * disabled / VS-on / mode None presents the stateless orientation
//     bit-identically (identity fail-open);
//   * the historical Continuity200ms ordinal normalizes to None and cannot
//     stack another product 200 ms correction;
//   * VS on forces the Lab temporal inactive even with the Lab enabled;
//   * mode switches reset (no stale damping history reuse), tracking/epoch
//     resets (ResetTwoHandLabTemporal, as the lifecycle sites call it) clear;
//   * constant damping: first frame is the target, tau<=eps answers the
//     exact target, convergence is monotonic, duplicates/ resets behave;
//   * adaptive damping: fast side beats slow side for the same error,
//     monotonic convergence, degenerate guards hold;
//   * no NaN/Inf in any presented/stateless orientation or error metric.
#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/common/two_hand_lab_logic.h"
#include "../src/dll/aim_pose_trace.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
unsigned checks = 0;
unsigned failures = 0;

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
#include "../src/dll/two_hand_lab_temporal_runtime.inl"

void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
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

bool AllFiniteQuat(virtual_stock::Quat4 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
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

// The deterministic ease clock: 1/64 is exact in binary, so 12 advances
// after the seed reach 187.5 ms and the 13th reaches 203.125 ms (>= 200 ms).
constexpr float kDt = 1.0f / 64.0f;

// VS-off two-hand Lab baseline with an accepted B: D=(0,0,0) faces -Z,
// S=(0.3,0,-1) gives agreement ~0.958, comfortably above the 0.35 floor, so
// the latch is a real (~16.7 deg) orientation change.
AimPoseInputs LabInputs(bool latched, int temporalOrdinal)
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
    inputs.twoHandLatched = latched;
    inputs.virtualStockEnabled = false;
    inputs.twoHandLabEnabled = true;
    inputs.twoHandLabAnchor = 0;
    inputs.twoHandLabOffhandInfluence = 1.0f;
    inputs.twoHandLabAgreement = 0;
    inputs.twoHandLabSoftFullAgreement = 0.9f;
    inputs.twoHandLabTemporal = temporalOrdinal;
    inputs.twoHandLabGeneration = 1;
    return inputs;
}

two_hand_lab::Settings LabSettings(two_hand_lab::TemporalMode mode)
{
    two_hand_lab::Settings settings = two_hand_lab::DefaultSettings();
    settings.enabled = true;
    settings.temporal = mode;
    return settings;
}

// Drives the production temporal seam exactly the way the live caller does:
// one advance per prepared serial, feeding back the orientation it
// presented, with the trace so the stateless diagnostics ride along.
struct LabDriver
{
    TwoHandLabTemporalState state{};
    two_hand_lab::Settings settings =
        LabSettings(two_hand_lab::TemporalMode::None);
    uint64_t serial = 0;
    bool lastPresentedValid = false;
    virtual_stock::Quat4 lastPresented{};
    AimPoseTrace lastTrace{};

    TwoHandLabTemporalFrameSolve Step(const AimPoseInputs& inputs,
        float dt = kDt)
    {
        AimPoseTrace trace{};
        const TwoHandLabTemporalFrameSolve solve =
            AdvanceTwoHandLabTemporalFrame(state, inputs, settings,
                ++serial, dt, lastPresentedValid, lastPresented,
                TwoHandLabProductPresentation{}, &trace);
        lastTrace = trace;
        lastPresentedValid = solve.presentedValid;
        if (solve.presentedValid)
            lastPresented = solve.presented;
        return solve;
    }

    TwoHandLabTemporalFrameSolve StepSameSerial(const AimPoseInputs& inputs,
        float dt = kDt)
    {
        AimPoseTrace trace{};
        return AdvanceTwoHandLabTemporalFrame(state, inputs, settings,
            serial, dt, lastPresentedValid, lastPresented,
            TwoHandLabProductPresentation{}, &trace);
    }
};

void CheckNoNan(const TwoHandLabTemporalFrameSolve& solve,
    const char* context)
{
    char message[160]{};
    std::snprintf(message, sizeof(message),
        "stateless orientation is finite (%s)", context);
    Check(AllFiniteQuat(solve.stateless), message);
    std::snprintf(message, sizeof(message),
        "presented orientation is finite (%s)", context);
    Check(AllFiniteQuat(solve.presented), message);
    std::snprintf(message, sizeof(message),
        "error metric is finite and non-negative (%s)", context);
    Check(std::isfinite(solve.errorDeg) && solve.errorDeg >= 0.0f,
        message);
}

void TestRetiredContinuityModeDoesNotLayer();

void TestInactiveModesPresentStatelessExactly()
{
    // Lab stored-disabled.
    {
        LabDriver driver;
        AimPoseInputs inputs = LabInputs(false, 1);
        inputs.twoHandLabEnabled = false;
        driver.settings.enabled = false;
        const TwoHandLabTemporalFrameSolve solve = driver.Step(inputs);
        Check(solve.statelessValid, "disabled: stateless solve is valid");
        Check(!solve.temporalActive, "disabled: temporal stays inactive");
        Check(SameQuat(solve.presented, solve.stateless),
            "disabled: presented is the stateless orientation exactly");
        Check(solve.errorDeg == 0.0f, "disabled: error metric is zero");
        CheckNoNan(solve, "disabled");
    }
    // Virtual Stock on: the Lab never steers VS paths.
    {
        LabDriver driver;
        AimPoseInputs inputs = LabInputs(true, 1);
        inputs.virtualStockEnabled = true;
        inputs.headValid = true;
        inputs.headPosition = {0.0f, 1.60f, 0.0f};
        inputs.headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
        const TwoHandLabTemporalFrameSolve solve = driver.Step(inputs);
        Check(!solve.temporalActive, "VS on: Lab temporal stays inactive");
        Check(SameQuat(solve.presented, solve.stateless),
            "VS on: presented is the stateless orientation exactly");
        CheckNoNan(solve, "VS on");
    }
    // Lab enabled, temporal mode None.
    {
        LabDriver driver;
        driver.settings = LabSettings(two_hand_lab::TemporalMode::None);
        const TwoHandLabTemporalFrameSolve latched =
            driver.Step(LabInputs(true, 0));
        Check(latched.statelessValid, "mode None: stateless solve is valid");
        Check(!latched.temporalActive, "mode None: temporal stays inactive");
        Check(SameQuat(latched.presented, latched.stateless),
            "mode None: presented is the stateless orientation exactly");
        CheckNoNan(latched, "mode None");
    }
}

void TestRetiredContinuityModeDoesNotLayer()
{
    LabDriver driver;
    const auto oldMode = two_hand_lab::TemporalMode::Continuity200ms;
    Check(two_hand_lab::NormalizeTemporalMode(static_cast<int>(oldMode)) ==
            two_hand_lab::TemporalMode::None,
        "the historical Lab Continuity200ms ordinal normalizes to None");
    driver.settings = LabSettings(oldMode);
    AimPoseInputs inputs = LabInputs(true, static_cast<int>(oldMode));
    const AimPoseResult raw = ComputeAimPose(inputs);
    const virtual_stock::Quat4 live{raw.pose.orientation.x,
        raw.pose.orientation.y, raw.pose.orientation.z,
        raw.pose.orientation.w};
    const virtual_stock::Quat4 productPresented{
        0.0f, 0.17364818f, 0.0f, 0.98480775f};
    const TwoHandLabProductPresentation product{
        true, live, true, productPresented};
    const TwoHandLabTemporalFrameSolve solve =
        AdvanceTwoHandLabTemporalFrame(driver.state, inputs, driver.settings,
            ++driver.serial, kDt, false, {}, product, nullptr);
    Check(solve.mode == two_hand_lab::TemporalMode::None &&
            !solve.temporalActive && !driver.state.dampingSeeded,
        "retired Lab continuity does not own a second temporal state");
    Check(CloseRotation(solve.presentationBase, productPresented, 1.0e-4f) &&
            CloseRotation(solve.presented, productPresented, 1.0e-4f),
        "Lab receives the product-presented orientation without adding a second 200 ms correction");
}

void TestDuplicateSerialNeverAdvances()
{
    // Mid-damping duplicate (constant).
    {
        LabDriver driver;
        driver.settings =
            LabSettings(two_hand_lab::TemporalMode::ConstantDamping);
        AimPoseInputs inputs = LabInputs(true, 2);
        driver.Step(inputs);
        inputs.left.position = {0.6f, 0.0f, -1.0f};
        inputs.supportPosition = {0.6f, 0.0f, -1.0f};
        const TwoHandLabTemporalFrameSolve lagged = driver.Step(inputs);
        Check(lagged.errorDeg > 0.0f,
            "duplicate fixture: damping lags the moved target");
        const virtual_stock::Quat4 before = driver.lastPresented;
        const TwoHandLabTemporalFrameSolve repeat =
            driver.StepSameSerial(inputs);
        Check(!repeat.advanceConsumed,
            "a duplicate serial consumes no advance (damping)");
        Check(SameQuat(repeat.presented, before),
            "a duplicate serial re-presents the previous damping output");
        CheckNoNan(repeat, "damping duplicate");
    }
    // Degenerate dt never advances damping.
    {
        LabDriver driver;
        driver.settings =
            LabSettings(two_hand_lab::TemporalMode::ConstantDamping);
        const AimPoseInputs inputs = LabInputs(true, 2);
        driver.Step(inputs);
        const uint64_t previousSerial = driver.state.lastPreparedSerial;
        const TwoHandLabTemporalFrameSolve skipped =
            driver.Step(inputs, 0.0f);
        Check(!skipped.advanceConsumed,
            "a non-positive dt consumes no advance");
        Check(driver.state.lastPreparedSerial == previousSerial,
            "a non-positive dt does not consume the damping serial");
    }
}

void TestModeSwitchAndResetClearHistory()
{
    // Constant damping lag, then a switch to adaptive: the next frame seeds
    // the target exactly instead of reusing constant history.
    {
        LabDriver driver;
        driver.settings =
            LabSettings(two_hand_lab::TemporalMode::ConstantDamping);
        AimPoseInputs inputs = LabInputs(true, 2);
        driver.Step(inputs);
        inputs.left.position = {0.6f, 0.0f, -1.0f};
        inputs.supportPosition = {0.6f, 0.0f, -1.0f};
        const TwoHandLabTemporalFrameSolve lagged = driver.Step(inputs);
        Check(lagged.errorDeg > 0.5f,
            "mode-switch fixture: damping lags the moved target");
        driver.settings =
            LabSettings(two_hand_lab::TemporalMode::AdaptiveDamping);
        inputs.twoHandLabTemporal = 3;
        const TwoHandLabTemporalFrameSolve reseeded = driver.Step(inputs);
        Check(reseeded.advanceConsumed,
            "the mode-switch serial still consumes its advance");
        Check(SameQuat(reseeded.presented, reseeded.stateless),
            "a mode switch seeds the target exactly (no stale history)");
        Check(reseeded.errorDeg == 0.0f,
            "a mode switch reports zero error on its seed frame");
        Check(driver.state.dampingSeeded,
            "the new mode is seeded after the switch");
    }
    // Tracking/epoch lifecycle reset clears, as the mirrored invalidation
    // sites require: the next frame seeds the target exactly.
    {
        LabDriver driver;
        driver.settings =
            LabSettings(two_hand_lab::TemporalMode::ConstantDamping);
        AimPoseInputs inputs = LabInputs(true, 2);
        driver.Step(inputs);
        inputs.left.position = {0.6f, 0.0f, -1.0f};
        inputs.supportPosition = {0.6f, 0.0f, -1.0f};
        driver.Step(inputs);
        Check(driver.state.dampingSeeded,
            "reset fixture: damping history is established");
        ResetTwoHandLabTemporal(driver.state);
        const TwoHandLabTemporalFrameSolve reseeded = driver.Step(inputs);
        Check(SameQuat(reseeded.presented, reseeded.stateless),
            "a lifecycle reset seeds the target exactly");
        Check(reseeded.errorDeg == 0.0f,
            "a lifecycle reset reports zero error on its seed frame");
    }
    // Lab disable drops history too: re-enable seeds fresh.
    {
        LabDriver driver;
        driver.settings =
            LabSettings(two_hand_lab::TemporalMode::ConstantDamping);
        AimPoseInputs inputs = LabInputs(true, 2);
        driver.Step(inputs);
        inputs.left.position = {0.6f, 0.0f, -1.0f};
        inputs.supportPosition = {0.6f, 0.0f, -1.0f};
        driver.Step(inputs);
        AimPoseInputs off = inputs;
        off.twoHandLabEnabled = false;
        const TwoHandLabTemporalFrameSolve quiet = driver.Step(off);
        Check(!quiet.temporalActive,
            "disabling the Lab deactivates temporal");
        Check(!driver.state.dampingSeeded,
            "disabling the Lab clears the damping history");
        const TwoHandLabTemporalFrameSolve back = driver.Step(inputs);
        Check(SameQuat(back.presented, back.stateless),
            "re-enabling the Lab seeds the target exactly");
    }
}

void TestConstantDamping()
{
    LabDriver driver;
    driver.settings =
        LabSettings(two_hand_lab::TemporalMode::ConstantDamping);
    AimPoseInputs inputs = LabInputs(true, 2);

    // First valid frame: the target, exactly.
    const TwoHandLabTemporalFrameSolve seed = driver.Step(inputs);
    Check(seed.temporalActive, "constant damping is active on its seed");
    Check(SameQuat(seed.presented, seed.stateless),
        "constant damping presents the target on its first frame");
    Check(seed.errorDeg == 0.0f,
        "constant damping reports zero error on its seed frame");
    Check(driver.lastTrace.labValid,
        "constant damping still publishes the stateless trace");
    CheckNoNan(seed, "constant seed");

    // Move the support endpoint ~14 deg and watch monotonic convergence.
    inputs.left.position = {0.6f, 0.0f, -1.0f};
    inputs.supportPosition = {0.6f, 0.0f, -1.0f};
    float previousError = -1.0f;
    for (int frame = 0; frame < 40; ++frame)
    {
        const TwoHandLabTemporalFrameSolve step = driver.Step(inputs);
        Check(step.temporalActive,
            "constant damping stays active while converging");
        Check(step.errorDeg <= previousError + 1.0e-5f || previousError < 0.0f,
            "constant damping error never increases");
        previousError = step.errorDeg;
        CheckNoNan(step, "constant convergence");
    }
    Check(previousError < 1.0f,
        "constant damping converges toward the moved target");
    const TwoHandLabTemporalFrameSolve early = driver.Step(inputs);

    // tau <= eps answers the exact target: the pure step the seam shares.
    {
        const virtual_stock::Quat4 previous{0.0f, 0.0f, 0.0f, 1.0f};
        const virtual_stock::Quat4 target{
            0.0f, 0.0871557f, 0.0f, 0.9961947f};
        const virtual_stock::Quat4 instant =
            two_hand_lab::ConstantDampingStep(previous, target, 0.0f, kDt);
        Check(SameQuat(instant, target),
            "tau<=eps answers the exact target");
        const virtual_stock::Quat4 degenerate =
            two_hand_lab::ConstantDampingStep(
                virtual_stock::Quat4{
                    std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f,
                    1.0f},
                target, 150.0f, kDt);
        Check(SameQuat(degenerate, target),
            "a non-finite previous fails closed to the target");
        Check(AllFiniteQuat(early.presented),
            "the converged damping output stays finite");
    }
}

void TestAdaptiveDamping()
{
    // Fast side (large error) beats slow side (always slow) over the same
    // frames for the same moved target.
    LabDriver fast;
    fast.settings = LabSettings(two_hand_lab::TemporalMode::AdaptiveDamping);
    fast.settings.adaptiveSlowResponseMs = 1000.0f;
    fast.settings.adaptiveFastResponseMs = 20.0f;
    fast.settings.adaptiveFullErrorDeg = 8.0f;
    LabDriver slow;
    slow.settings = LabSettings(two_hand_lab::TemporalMode::AdaptiveDamping);
    slow.settings.adaptiveSlowResponseMs = 1000.0f;
    slow.settings.adaptiveFastResponseMs = 1000.0f;
    slow.settings.adaptiveFullErrorDeg = 8.0f;
    AimPoseInputs inputs = LabInputs(true, 3);
    fast.Step(inputs);
    slow.Step(inputs);
    inputs.left.position = {0.6f, 0.0f, -1.0f};
    inputs.supportPosition = {0.6f, 0.0f, -1.0f};
    TwoHandLabTemporalFrameSolve fastStep{};
    TwoHandLabTemporalFrameSolve slowStep{};
    for (int frame = 0; frame < 5; ++frame)
    {
        fastStep = fast.Step(inputs);
        slowStep = slow.Step(inputs);
        CheckNoNan(fastStep, "adaptive fast");
        CheckNoNan(slowStep, "adaptive slow");
    }
    Check(fastStep.errorDeg < slowStep.errorDeg,
        "large errors answer faster than the slow side");
    Check(slowStep.errorDeg > 0.0f,
        "the slow side still lags after five frames");
    // Monotonic convergence on the fast side (slower near the target by
    // design: small errors use the slow constant).
    float previousError = fastStep.errorDeg;
    for (int frame = 0; frame < 100; ++frame)
    {
        fastStep = fast.Step(inputs);
        Check(fastStep.errorDeg <= previousError + 1.0e-5f,
            "adaptive damping error never increases");
        previousError = fastStep.errorDeg;
    }
    Check(previousError < 1.0f, "adaptive damping converges");
    // Degenerate guards at the pure level the seam shares: a non-finite
    // full-error holds the slow constant, and inverted bounds never escape
    // Normalize.
    {
        const virtual_stock::Quat4 previous{0.0f, 0.0f, 0.0f, 1.0f};
        const virtual_stock::Quat4 target{
            0.0f, 0.0871557f, 0.0f, 0.9961947f};
        const virtual_stock::Quat4 held =
            two_hand_lab::AdaptiveDampingStep(previous, target, 400.0f,
                50.0f, std::numeric_limits<float>::quiet_NaN(), kDt);
        const virtual_stock::Quat4 slowConstant =
            two_hand_lab::ConstantDampingStep(
                previous, target, 400.0f, kDt);
        Check(SameQuat(held, slowConstant),
            "a non-finite full-error holds the slow constant");
        two_hand_lab::Settings inverted = two_hand_lab::DefaultSettings();
        inverted.adaptiveSlowResponseMs = 50.0f;
        inverted.adaptiveFastResponseMs = 400.0f;
        const two_hand_lab::Settings repaired =
            two_hand_lab::Normalize(inverted);
        Check(repaired.adaptiveSlowResponseMs >=
                repaired.adaptiveFastResponseMs,
            "slow>=fast is enforced before the adaptive step");
    }
}

} // namespace

int main()
{
    TestInactiveModesPresentStatelessExactly();
    TestRetiredContinuityModeDoesNotLayer();
    TestDuplicateSerialNeverAdvances();
    TestModeSwitchAndResetClearHistory();
    TestConstantDamping();
    TestAdaptiveDamping();
    std::printf("two_hand_lab_temporal: %u checks, %u failures\n", checks,
        failures);
    return failures == 0 ? 0 : 1;
}
