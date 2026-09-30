// Deterministic unit tests for the pure Virtual Stock grab/release aim
// continuity state machine (src/common/virtual_stock_aim_continuity.h).
//
// The product contract under test: a single fixed 200 ms smoothstep
// (smoothstep(elapsed / 0.200)) applies to BOTH acquisition and release, the
// correction is composed as live (x) correction against the LIVE target of the
// frame being presented, and the transition reaches exact identity at and
// after 200 ms. There is no mode selector, no motion gate and no persistent
// hold; those experiment branches were rejected.
//
// The tests mirror the production composition convention from
// src/dll/virtual_stock_aim.inl finishAimPose: the per-gun calibration is
// post-multiplied onto the base pose in the pose's own local frame, so the
// correction this module layers on must compose the same way (including roll).
#include "virtual_stock_aim_continuity.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
using virtual_stock::Point3;
using virtual_stock::Quat4;

constexpr float kDegToRad = 0.01745329252f;
constexpr float kEaseSeconds = virtual_stock::kAimContinuityEaseSeconds;

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
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

    void CheckNear(float actual, float expected, const char* message,
        float epsilon)
    {
        ++checks;
        if (!std::isfinite(actual) || std::fabs(actual - expected) > epsilon)
        {
            ++failures;
            std::fprintf(stderr,
                "FAIL: %s\n  expected: %g\n  actual:   %g\n",
                message, expected, actual);
        }
    }

    void CheckAngle(Quat4 actual, Quat4 expected, const char* message,
        float epsilonDeg)
    {
        const float angle = virtual_stock::Quat4AngleDegrees(actual, expected);
        CheckNear(angle, 0.0f, message, epsilonDeg);
    }
};

// Angle between two (possibly non-unit) direction vectors, in degrees.
float VectorAngleDegrees(Point3 a, Point3 b)
{
    const float lengthA = std::sqrt(
        a.x * a.x + a.y * a.y + a.z * a.z);
    const float lengthB = std::sqrt(
        b.x * b.x + b.y * b.y + b.z * b.z);
    if (!(lengthA > 0.0f) || !(lengthB > 0.0f))
        return std::numeric_limits<float>::quiet_NaN();
    const float dot = (a.x * b.x + a.y * b.y + a.z * b.z) /
        (lengthA * lengthB);
    return std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.2957795131f;
}

// ---------------------------------------------------------------------------
// Convention helpers (copied verbatim from finishAimPose's operand order).
// ---------------------------------------------------------------------------
Quat4 Multiply(Quat4 a, Quat4 b)
{
    return Quat4{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat4 Conjugate(Quat4 value)
{
    return Quat4{-value.x, -value.y, -value.z, value.w};
}

Quat4 AxisAngle(float x, float y, float z, float degrees)
{
    const float lengthSquared = x * x + y * y + z * z;
    if (!(lengthSquared > 0.0f))
        return Quat4{};
    const float length = std::sqrt(lengthSquared);
    const float half = degrees * 0.5f * kDegToRad;
    const float s = std::sin(half);
    return Quat4{x / length * s, y / length * s, z / length * s,
        std::cos(half)};
}

// Exactly the finishAimPose calibration chain:
//   corrected = pose (x) ((qYaw (x) qPitch) (x) qRoll)
Quat4 Calibrated(Quat4 base, float yawDeg, float pitchDeg, float rollDeg)
{
    const float yaw = yawDeg * kDegToRad;
    const float pitch = pitchDeg * kDegToRad;
    const float roll = rollDeg * kDegToRad;
    const Quat4 qYaw{0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    const Quat4 qPitch{std::sin(pitch * 0.5f), 0.0f, 0.0f,
        std::cos(pitch * 0.5f)};
    const Quat4 qRoll{0.0f, 0.0f, std::sin(-roll * 0.5f),
        std::cos(roll * 0.5f)};
    return Multiply(base, Multiply(Multiply(qYaw, qPitch), qRoll));
}

const Quat4 kStockBase = AxisAngle(0.30f, 0.90f, 0.10f, 40.0f);
const Quat4 kOneHandBase = AxisAngle(-0.20f, 0.80f, 0.55f, 70.0f);
const float kGunYawDeg = -7.0f;
const float kGunPitchDeg = 11.0f;
const float kGunRollDeg = 23.0f;

Quat4 StockLive()
{
    return Calibrated(kStockBase, kGunYawDeg, kGunPitchDeg, kGunRollDeg);
}

Quat4 OneHandLive()
{
    return Calibrated(kOneHandBase, kGunYawDeg, kGunPitchDeg, kGunRollDeg);
}

bool SameQuat(Quat4 a, Quat4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool SameDiagnostics(
    const virtual_stock::AimContinuityDiagnostics& a,
    const virtual_stock::AimContinuityDiagnostics& b)
{
    return a.phase == b.phase && a.active == b.active &&
        a.edge_kind == b.edge_kind && a.anchor_source == b.anchor_source &&
        a.initial_correction_deg == b.initial_correction_deg &&
        a.remaining_correction_deg == b.remaining_correction_deg &&
        a.transition_elapsed_ms == b.transition_elapsed_ms &&
        a.last_prepared_serial == b.last_prepared_serial;
}

using virtual_stock::AimContinuityAnchorSource;
using virtual_stock::AimContinuityEdgeKind;
using virtual_stock::AimContinuityPhase;

// Emulates the live caller shape: one advance per prepared serial, with the
// previous frame's presented orientation fed back in.
struct Rig
{
    virtual_stock::AimContinuityState state{};
    uint64_t serial = 0;
    bool latched = false;
    bool owns = false;
    bool stockOwnershipEdgesEnabled = true;
    // The live caller derives the ownership edge as
    // virtualStockEnabled && the same-frame stock solve owning presentation
    // (src/dll/virtual_stock_aim_continuity_runtime.inl). Modelling the
    // Virtual Stock term explicitly lets the VS-OFF product path prove that
    // the solver's own steering verdicts cannot reach this edge source.
    bool virtualStockEnabled = true;
    bool liveValid = true;
    Quat4 live = StockLive();
    bool oneHandValid = true;
    Quat4 oneHand = OneHandLive();
    bool lastPresentedValid = true;
    Quat4 lastPresented{};
    float dt = 1.0f / 64.0f;

    virtual_stock::AimContinuityInput Input() const
    {
        virtual_stock::AimContinuityInput input{};
        input.preparedSerial = serial;
        input.dtSeconds = dt;
        input.latched = latched;
        input.stockSolveOwnsPresentation =
            stockOwnershipEdgesEnabled && virtualStockEnabled && owns;
        input.liveOrientationValid = liveValid;
        input.liveOrientation = live;
        input.oneHandOrientationValid = oneHandValid;
        input.oneHandOrientation = oneHand;
        input.lastPresentedOrientationValid = lastPresentedValid;
        input.lastPresentedOrientation = lastPresented;
        return input;
    }

    void Advance()
    {
        ++serial;
        virtual_stock::AdvanceAimContinuity(state, Input());
        lastPresented = virtual_stock::ApplyAimContinuity(state, live);
        lastPresentedValid = true;
    }

    // Advances without touching the caller-side presented record.
    void AdvanceRaw()
    {
        ++serial;
        virtual_stock::AdvanceAimContinuity(state, Input());
    }

    Quat4 Presented() const
    {
        return virtual_stock::ApplyAimContinuity(state, live);
    }

    virtual_stock::AimContinuityDiagnostics Diag() const
    {
        return virtual_stock::ReadAimContinuityDiagnostics(state);
    }
};

// Baseline frame (the module must not fire an edge for an already-latched or
// already-stocked first observation), then the grab edge frame.
void BeginHeld(Rig& rig)
{
    rig.Advance();               // baseline: nothing latched
    rig.latched = true;
    rig.owns = true;
    rig.Advance();               // grab edge / seed
}

// ---------------------------------------------------------------------------
// 1. Composition convention including roll.
// ---------------------------------------------------------------------------
void TestCompositionSeedEquality(TestContext& test)
{
    const Quat4 live = StockLive();
    const Quat4 oneHand = OneHandLive();
    const float hardSwitchDeg =
        virtual_stock::Quat4AngleDegrees(live, oneHand);
    test.Check(std::isfinite(hardSwitchDeg) && hardSwitchDeg > 15.0f,
        "fixture reproduces a real hard switch (roll included)");

    Rig rig;
    rig.live = live;
    rig.oneHand = oneHand;
    BeginHeld(rig);

    const auto diagnostics = rig.Diag();
    test.Check(diagnostics.active &&
            diagnostics.phase == AimContinuityPhase::Acquire,
        "grab edge enters the acquire phase");
    test.Check(diagnostics.edge_kind == AimContinuityEdgeKind::Grab,
        "grab edge reports edge_kind=grab");
    test.Check(diagnostics.anchor_source ==
            AimContinuityAnchorSource::SameFrameOneHand,
        "fresh grab anchors on the same-frame one-hand orientation");
    test.CheckNear(diagnostics.initial_correction_deg, hardSwitchDeg,
        "seeded correction equals the measured hard switch", 0.01f);
    test.CheckAngle(rig.Presented(), oneHand,
        "grab seed makes presented == oneHand",
        0.001f);

    // The exact inverse relation used for seeding, with roll present in the
    // calibration chain.
    const Quat4 correction = Multiply(Conjugate(live), oneHand);
    const Quat4 recomposed = Multiply(live, correction);
    test.CheckAngle(recomposed, oneHand,
        "live (x) inverse(live) (x) oneHand reproduces oneHand", 0.001f);

    // Order matters: the correction is a local-frame post-multiply, exactly
    // like finishAimPose's calibration chain. Roll is what makes the wrong
    // order observably different.
    const Quat4 wrongOrder = Multiply(correction, live);
    test.Check(
        virtual_stock::Quat4AngleDegrees(wrongOrder, oneHand) > 5.0f,
        "pre-multiplying the correction is provably the wrong convention");

    // Full-orientation equality (not just forward direction): all three basis
    // vectors must match, which is what roll makes meaningful.
    const Point3 forward =
        virtual_stock::RotatePoint(rig.Presented(), {0.0f, 0.0f, -1.0f});
    const Point3 expectedForward =
        virtual_stock::RotatePoint(oneHand, {0.0f, 0.0f, -1.0f});
    const Point3 up =
        virtual_stock::RotatePoint(rig.Presented(), {0.0f, 1.0f, 0.0f});
    const Point3 expectedUp =
        virtual_stock::RotatePoint(oneHand, {0.0f, 1.0f, 0.0f});
    test.CheckNear(
        VectorAngleDegrees(forward, expectedForward),
        0.0f, "presented forward matches the one-hand forward", 0.01f);
    test.CheckNear(
        VectorAngleDegrees(up, expectedUp),
        0.0f, "presented up/roll matches the one-hand roll", 0.01f);
}

// ---------------------------------------------------------------------------
// 2. The fixed 200 ms smoothstep acquisition.
// ---------------------------------------------------------------------------
void TestAcquireFixedEase(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    const auto seed = rig.Diag();
    test.Check(seed.active && seed.transition_elapsed_ms == 0.0f,
        "the seed frame does not step the ease clock");
    test.CheckAngle(rig.Presented(), rig.oneHand,
        "the acquisition seeds presented == oneHand", 0.001f);
    const float initial = seed.initial_correction_deg;
    test.Check(initial > 15.0f, "the seeded correction is non-trivial");

    // dt = 1/64 is exact in binary: 12 advances reach 187.5 ms, the 13th
    // reaches 203.125 ms (>= 200 ms) and must complete exactly.
    float previousRemaining = initial;
    for (int frame = 0; frame < 12; ++frame)
    {
        rig.Advance();
        const auto diagnostics = rig.Diag();
        test.Check(diagnostics.active,
            "the acquire ease stays active before 200 ms");
        test.Check(diagnostics.remaining_correction_deg <=
                previousRemaining + 1.0e-4f &&
                diagnostics.remaining_correction_deg >= 0.0f,
            "the acquire remaining rotation is monotonic and never overshoots");
        previousRemaining = diagnostics.remaining_correction_deg;
    }

    const auto mid = rig.Diag();
    test.CheckNear(mid.transition_elapsed_ms, 187.5f,
        "the ease clock accumulates per advance", 0.001f);
    const float t = 187.5f / (kEaseSeconds * 1000.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    test.CheckNear(mid.remaining_correction_deg, initial * (1.0f - smooth),
        "acquisition follows smoothstep(elapsed / 200 ms)", 0.05f);
    test.Check(mid.remaining_correction_deg > 0.0f,
        "acquisition is not finished at 187.5 ms");

    rig.Advance();
    const auto done = rig.Diag();
    test.Check(!done.active && done.phase == AimContinuityPhase::Idle,
        "acquisition completes at >= 200 ms");
    test.Check(done.remaining_correction_deg == 0.0f,
        "acquisition reaches exact identity at >= 200 ms");
    test.Check(done.transition_elapsed_ms >= 200.0f,
        "acquisition completion time is at least 200 ms");
    test.Check(SameQuat(rig.Presented(), rig.live),
        "a completed acquisition presents the live pose unchanged");
    test.Check(done.edge_kind == AimContinuityEdgeKind::Grab &&
            done.anchor_source == AimContinuityAnchorSource::SameFrameOneHand,
        "the last-edge diagnostics survive completion");
}

// ---------------------------------------------------------------------------
// 3. The presented target is the LIVE orientation, never a frozen seed.
// ---------------------------------------------------------------------------
void TestMovingLiveTargetDuringAcquire(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    const Quat4 seedLive = rig.live;

    // Sweep the live target while the ease decays; presented must always be
    // live(that frame) (x) correction and must never pass the live target.
    float previousAngleFromLive =
        virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live);
    bool sawFrozenSeedComposition = false;
    for (int frame = 0; frame < 40; ++frame)
    {
        rig.live = Calibrated(
            AxisAngle(0.30f, 0.90f, 0.10f, 40.0f + frame * 0.5f),
            kGunYawDeg, kGunPitchDeg, kGunRollDeg);
        rig.Advance();

        const Quat4 composed = Multiply(rig.live, rig.state.correction);
        test.CheckAngle(rig.Presented(), composed,
            "presented == live(that frame) (x) correction", 0.01f);
        const Quat4 frozen = Multiply(seedLive, rig.state.correction);
        if (virtual_stock::Quat4AngleDegrees(rig.Presented(), frozen) > 1.0f)
            sawFrozenSeedComposition = true;

        const float angleFromLive =
            virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live);
        test.Check(angleFromLive <= previousAngleFromLive + 1.0e-3f,
            "presented-to-live angle never increases during the ease");
        previousAngleFromLive = angleFromLive;
    }
    test.Check(sawFrozenSeedComposition,
        "the moving live target is tracked, not frozen at the seed");
    test.Check(!rig.Diag().active &&
            virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live) <
                1.0e-3f,
        "acquisition converges to the moving live target");
}

// ---------------------------------------------------------------------------
// 4. Release edge: same fixed ease law, seeded from the presented aim.
// ---------------------------------------------------------------------------
void TestReleaseFixedEase(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    for (int frame = 0; frame < 32; ++frame)
        rig.Advance();
    test.Check(!rig.Diag().active,
        "acquisition completed before the release fixture");
    const Quat4 presentedBeforeRelease = rig.Presented();
    test.CheckAngle(presentedBeforeRelease, rig.live,
        "a completed acquisition presents the stocked live pose", 0.001f);

    rig.latched = false;
    rig.owns = false;
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    test.Check(SameQuat(rig.lastPresented, presentedBeforeRelease),
        "caller records the previously presented orientation");
    rig.Advance();

    const auto released = rig.Diag();
    test.Check(released.active && released.phase == AimContinuityPhase::Release,
        "release edge enters the release phase");
    test.Check(released.edge_kind == AimContinuityEdgeKind::Release &&
            released.anchor_source == AimContinuityAnchorSource::LastPresented,
        "release edge anchors on last_presented");
    test.CheckAngle(rig.Presented(), presentedBeforeRelease,
        "release edge does not snap: presented == last presented", 0.001f);
    test.Check(
        virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live) > 5.0f,
        "the release correction is a real offset from the one-hand live pose");
    test.Check(released.transition_elapsed_ms == 0.0f,
        "the release seed frame does not step the ease clock");

    // The release follows the same smoothstep law with the same 200 ms
    // duration, so it is complete on the 13th advance after the seed.
    float previousRemaining = released.remaining_correction_deg;
    for (int frame = 0; frame < 12; ++frame)
    {
        rig.Advance();
        const auto diagnostics = rig.Diag();
        test.Check(diagnostics.active,
            "the release ease stays active before 200 ms");
        test.Check(diagnostics.remaining_correction_deg <=
                previousRemaining + 1.0e-4f &&
                diagnostics.remaining_correction_deg >= 0.0f,
            "the release remaining rotation is monotonic and never overshoots");
        previousRemaining = diagnostics.remaining_correction_deg;
    }
    const auto mid = rig.Diag();
    const float t = 187.5f / (kEaseSeconds * 1000.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    test.CheckNear(mid.remaining_correction_deg,
        released.initial_correction_deg * (1.0f - smooth),
        "release follows the same smoothstep law as acquisition", 0.05f);
    test.Check(mid.remaining_correction_deg > 0.0f,
        "release is not finished at 187.5 ms");

    rig.Advance();
    const auto done = rig.Diag();
    test.Check(!done.active && done.phase == AimContinuityPhase::Idle &&
            done.remaining_correction_deg == 0.0f,
        "release completes to exact identity at >= 200 ms");
    test.Check(SameQuat(rig.Presented(), rig.live),
        "after the release ease presented == the live one-hand pose exactly");
    test.Check(done.initial_correction_deg > 5.0f,
        "the release transition reports its initial correction");
}

void TestMovingLiveTargetDuringRelease(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    for (int frame = 0; frame < 32; ++frame)
        rig.Advance();
    rig.latched = false;
    rig.owns = false;
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    rig.Advance();

    // Move the one-hand live target while the release decays.
    float previousAngleFromLive =
        virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live);
    for (int frame = 0; frame < 40; ++frame)
    {
        rig.live = Calibrated(
            AxisAngle(-0.20f, 0.80f, 0.55f, 70.0f + frame * 0.8f),
            kGunYawDeg, kGunPitchDeg, kGunRollDeg);
        rig.oneHand = rig.live;
        rig.Advance();
        const Quat4 composed = Multiply(rig.live, rig.state.correction);
        test.CheckAngle(rig.Presented(), composed,
            "released presented == live(that frame) (x) correction", 0.01f);
        const float angleFromLive =
            virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live);
        test.Check(angleFromLive <= previousAngleFromLive + 1.0e-3f,
            "released presented-to-live angle never increases");
        previousAngleFromLive = angleFromLive;
    }
    test.Check(!rig.Diag().active &&
            virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live) <
                1.0e-3f,
        "release converges to the moving one-hand live target");
}

// ---------------------------------------------------------------------------
// 5. Edges without a latch change; release wins when both edges coincide.
// ---------------------------------------------------------------------------
void TestOwnershipEdgesWithoutLatchChange(TestContext& test)
{
    Rig rig;
    // The latch is already engaged before the module observes it: the first
    // observation is a baseline, never an edge.
    rig.latched = true;
    rig.owns = false;
    rig.Advance();
    test.Check(!rig.Diag().active,
        "an already-latched hand is not a grab edge at baseline");

    // Stock ownership becoming true is a grab edge.
    rig.owns = true;
    rig.live = StockLive();
    rig.oneHand = OneHandLive();
    rig.Advance();
    const auto grabbed = rig.Diag();
    test.Check(grabbed.active &&
            grabbed.edge_kind == AimContinuityEdgeKind::Grab &&
            grabbed.anchor_source == AimContinuityAnchorSource::SameFrameOneHand,
        "stock ownership becoming true is a grab edge");
    test.CheckAngle(rig.Presented(), rig.oneHand,
        "ownership grab edge seeds presented == oneHand", 0.001f);

    // Ownership leaving stock while the latch is still held is a release edge.
    // Let the acquisition decay for a few frames first so the release fixture
    // carries a real offset (a no-snap check that cannot pass trivially).
    for (int frame = 0; frame < 5; ++frame)
        rig.Advance();
    const Quat4 presentedBeforeRelease = rig.Presented();
    test.Check(
        virtual_stock::Quat4AngleDegrees(presentedBeforeRelease, rig.oneHand) >
            5.0f,
        "ownership release fixture carries a real stock offset");
    rig.owns = false;
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    test.Check(SameQuat(rig.lastPresented, presentedBeforeRelease),
        "caller presented record matches before the ownership release");
    rig.Advance();
    const auto released = rig.Diag();
    test.Check(released.active &&
            released.edge_kind == AimContinuityEdgeKind::Release &&
            released.phase == AimContinuityPhase::Release &&
            released.anchor_source == AimContinuityAnchorSource::LastPresented,
        "ownership leaving stock is a release edge");
    test.CheckAngle(rig.Presented(), presentedBeforeRelease,
        "ownership release does not snap", 0.001f);
}

void TestLatchOnlyFreeAimIgnoresOwnershipChanges(TestContext& test)
{
    Rig rig;
    rig.stockOwnershipEdgesEnabled = false;
    rig.Advance(); // unlatched baseline
    rig.latched = true;
    rig.live = StockLive();
    rig.oneHand = OneHandLive();
    rig.Advance();
    test.Check(rig.Diag().active &&
            rig.Diag().edge_kind == AimContinuityEdgeKind::Grab,
        "VS-OFF product transition starts only from the durable latch edge");

    rig.Advance();
    const float elapsedBeforeOwnershipChange = rig.state.elapsedSeconds;
    const Quat4 originalSeed = rig.state.initialCorrection;
    rig.owns = true;
    rig.Advance();
    test.Check(rig.Diag().active &&
            rig.Diag().edge_kind == AimContinuityEdgeKind::Grab &&
            rig.state.elapsedSeconds > elapsedBeforeOwnershipChange &&
            SameQuat(rig.state.initialCorrection, originalSeed),
        "VS-OFF stock/B ownership changes advance the existing ease without reseeding it");

    rig.latched = false;
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    rig.Advance();
    test.Check(rig.Diag().active &&
            rig.Diag().edge_kind == AimContinuityEdgeKind::Release &&
            rig.Diag().phase == AimContinuityPhase::Release,
        "VS-OFF release remains owned by the latch edge");
}

// ---------------------------------------------------------------------------
// 5b. VS-OFF: support-agreement floor crossings and persistent-grip retained
// steering cannot restart the running latch-edge ease.
// ---------------------------------------------------------------------------
void TestVsOffAgreementFloorCrossingsDoNotRestartContinuity(TestContext& test)
{
    // The live caller derives the ownership edge from
    // virtualStockEnabled && the same-frame stock solve owning presentation
    // (src/dll/virtual_stock_aim_continuity_runtime.inl). On the VS-OFF
    // product path that term is structurally false, so the support-agreement
    // verdict crossing the legacy 0.35 floor - accepted, rejected, accepted
    // while persistent-grip retention keeps steering - can flip the solver's
    // own steering without ever producing a continuity ownership edge.
    Rig rig;
    rig.stockOwnershipEdgesEnabled = true;
    rig.virtualStockEnabled = false;
    rig.Advance(); // baseline
    rig.latched = true;
    rig.owns = false;
    rig.Advance(); // the durable latch edge
    const auto seeded = rig.Diag();
    test.Check(seeded.active && seeded.edge_kind == AimContinuityEdgeKind::Grab,
        "the VS-OFF latch edge seeds the acquire ease");

    const Quat4 seedCorrection = rig.state.initialCorrection;
    float previousRemaining =
        virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live);
    test.Check(previousRemaining > 5.0f,
        "the VS-OFF ease carries a real offset before the floor crossings");
    for (int frame = 0; frame < 12; ++frame)
    {
        // Cross the floor both ways on consecutive frames: the caller-side
        // ownership term would flip if Virtual Stock were on.
        rig.owns = (frame % 2) == 0;
        rig.Advance();
        const auto diagnostics = rig.Diag();
        const float remaining =
            virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live);
        test.Check(diagnostics.active &&
                diagnostics.edge_kind == AimContinuityEdgeKind::Grab,
            "a .35 agreement-floor crossing advances the running VS-OFF ease");
        test.Check(SameQuat(rig.state.initialCorrection, seedCorrection),
            "a .35 agreement-floor crossing never reseeds the running ease");
        test.Check(rig.state.elapsedSeconds > 0.0f,
            ".35 crossings never reset the VS-OFF ease clock");
        test.Check(remaining <= previousRemaining + 1.0e-3f,
            "the VS-OFF presented-to-live angle never increases across .35 crossings");
        previousRemaining = remaining;
    }
    rig.owns = true; // one more retained-steering frame at the ease completion
    rig.Advance();
    test.Check(!rig.state.active &&
            virtual_stock::Quat4AngleDegrees(rig.Presented(), rig.live) < 1.0e-3f,
        "the VS-OFF latch-edge ease still reaches the live aim on schedule "
        "across .35 crossings");

    // Non-vacuity: the identical ownership flip is a real edge source when the
    // same caller runs with Virtual Stock on - it restarts the ease from the
    // currently presented aim (clock and seed reset), which is exactly what
    // must not happen on the VS-OFF product path.
    Rig stock;
    stock.stockOwnershipEdgesEnabled = true;
    stock.virtualStockEnabled = true;
    stock.Advance(); // baseline
    stock.latched = true;
    stock.owns = false;
    stock.Advance(); // latch edge
    for (int frame = 0; frame < 3; ++frame)
        stock.Advance();
    test.Check(stock.state.elapsedSeconds > 0.0f,
        "the VS-on fixture has advanced its ease before the ownership flip");
    stock.owns = true; // the term the VS-OFF caller can never set
    stock.Advance();
    test.Check(stock.state.elapsedSeconds == 0.0f &&
            stock.state.active &&
            stock.state.edgeKind == AimContinuityEdgeKind::Grab,
        "the same ownership flip reseeds the ease when Virtual Stock is on");
}

void TestReleaseWinsWhenBothEdgesCoincide(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    for (int frame = 0; frame < 3; ++frame)
        rig.Advance();
    const Quat4 presentedBefore = rig.Presented();

    // Latch released and ownership dropped on the same serial: the release
    // edge must win, keeping the presented aim continuous.
    rig.latched = false;
    rig.owns = false;
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    rig.Advance();
    const auto diagnostics = rig.Diag();
    test.Check(diagnostics.active &&
            diagnostics.edge_kind == AimContinuityEdgeKind::Release &&
            diagnostics.phase == AimContinuityPhase::Release,
        "a simultaneous release edge wins over a grab edge");
    test.CheckAngle(rig.Presented(), presentedBefore,
        "the winning release edge presents the previously presented aim",
        0.001f);
}

// ---------------------------------------------------------------------------
// 6. Rapid reversals: no edge discontinuity in either direction.
// ---------------------------------------------------------------------------
void TestRapidReleaseRegrab(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    for (int frame = 0; frame < 5; ++frame)
        rig.Advance();
    test.Check(rig.Diag().active &&
            rig.Diag().phase == AimContinuityPhase::Acquire,
        "acquisition is still active before the rapid release");

    // Release mid-acquire: the release seed keeps the presented aim.
    rig.latched = false;
    rig.owns = false;
    const Quat4 presentedBeforeRelease = rig.Presented();
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    rig.Advance();
    const auto released = rig.Diag();
    test.Check(released.active && released.phase == AimContinuityPhase::Release,
        "grabbing then releasing mid-acquire enters the release phase");
    test.CheckAngle(rig.Presented(), presentedBeforeRelease,
        "release mid-acquire introduces no discontinuity", 0.001f);

    // Re-grab mid-release.
    for (int frame = 0; frame < 3; ++frame)
        rig.Advance();
    test.Check(rig.Diag().active &&
            rig.Diag().phase == AimContinuityPhase::Release,
        "the release transition is still active at the re-grab");
    const Quat4 presentedBeforeRegrab = rig.Presented();
    test.Check(SameQuat(rig.lastPresented, presentedBeforeRegrab),
        "caller presented record matches the release presentation");

    rig.latched = true;
    rig.owns = true;
    rig.live = StockLive();
    rig.oneHand = OneHandLive();
    rig.Advance();
    const auto regrabbed = rig.Diag();
    test.Check(regrabbed.active &&
            regrabbed.phase == AimContinuityPhase::Acquire &&
            regrabbed.edge_kind == AimContinuityEdgeKind::Grab,
        "re-grab during a release re-enters the acquire phase");
    test.Check(regrabbed.anchor_source ==
            AimContinuityAnchorSource::LastPresented,
        "re-entrant grab anchors on last_presented, not the one-hand pose");
    test.CheckAngle(rig.Presented(), presentedBeforeRegrab,
        "re-grab introduces no discontinuity", 0.001f);

    // The re-seeded ease must decay from the running correction, not from the
    // original acquisition seed. Advance once and check the law.
    const float reSeeded = regrabbed.initial_correction_deg;
    test.CheckNear(reSeeded,
        virtual_stock::Quat4AngleDegrees(presentedBeforeRegrab, rig.live),
        "the re-entrant seed is the running correction", 0.02f);
    rig.Advance();
    const float t = (1.0f / 64.0f) / kEaseSeconds;
    const float smooth = t * t * (3.0f - 2.0f * t);
    test.CheckNear(rig.Diag().remaining_correction_deg,
        reSeeded * (1.0f - smooth),
        "the re-entrant ease follows the 200 ms law from the running "
        "correction", 0.05f);

    // It still completes on schedule.
    for (int frame = 0; frame < 40; ++frame)
        rig.Advance();
    test.Check(!rig.Diag().active &&
            SameQuat(rig.Presented(), rig.live),
        "the re-entrant acquisition completes to the live pose exactly");
}

void TestReentrantWithoutCallerAnchor(TestContext& test)
{
    // A re-entrant edge with no caller anchor keeps the module's own running
    // correction instead of jumping.
    Rig rig;
    BeginHeld(rig);
    for (int frame = 0; frame < 4; ++frame)
        rig.Advance();
    const Quat4 runningCorrection = rig.state.correction;

    rig.latched = false;
    rig.owns = false;
    rig.lastPresentedValid = false;
    rig.live = OneHandLive();
    rig.oneHand = rig.live;
    rig.Advance();
    const auto released = rig.Diag();
    test.Check(released.active && released.phase == AimContinuityPhase::Release,
        "anchorless release keeps decaying from the module correction");
    test.Check(SameQuat(rig.state.correction, runningCorrection),
        "anchorless release preserves the module's own correction");
    test.CheckAngle(rig.Presented(), Multiply(rig.live, runningCorrection),
        "anchorless release presents the running correction unchanged",
        0.001f);
    test.Check(SameQuat(rig.state.initialCorrection, runningCorrection),
        "anchorless release re-seeds the ease from the running correction");
}

// ---------------------------------------------------------------------------
// 7. Fail-open behaviour.
// ---------------------------------------------------------------------------
void TestFailOpen(TestContext& test)
{
    // Inactive release with no anchors: identity fail-open, Apply == live.
    Rig fresh;
    fresh.Advance();                 // baseline
    fresh.latched = true;
    fresh.owns = true;
    fresh.oneHandValid = false;
    fresh.lastPresentedValid = false; // the caller cannot provide an anchor
    fresh.Advance();                 // grab edge with no anchors
    test.Check(!fresh.Diag().active &&
            fresh.Diag().anchor_source == AimContinuityAnchorSource::Identity,
        "grab edge without usable anchors fails open to identity");
    test.Check(SameQuat(fresh.Presented(), fresh.live),
        "identity fail-open presents the live pose unchanged");
    fresh.latched = false;
    fresh.owns = false;
    fresh.oneHandValid = false;
    fresh.lastPresentedValid = false;
    fresh.Advance();                 // release edge with no anchors
    test.Check(!fresh.Diag().active &&
            fresh.Diag().anchor_source == AimContinuityAnchorSource::Identity,
        "release edge without usable anchors fails open to identity");
    test.Check(SameQuat(fresh.Presented(), fresh.live),
        "anchorless release fail-open presents the live pose unchanged");

    // Invalid live orientation at a grab edge fails open to identity.
    Rig invalidLive;
    invalidLive.Advance();
    invalidLive.latched = true;
    invalidLive.owns = true;
    invalidLive.liveValid = false;
    invalidLive.live = Quat4{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f,
        1.0f};
    invalidLive.Advance();
    const auto failOpen = invalidLive.Diag();
    test.Check(!failOpen.active &&
            failOpen.anchor_source == AimContinuityAnchorSource::Identity &&
            failOpen.remaining_correction_deg == 0.0f,
        "invalid live stock solve fails open to identity");
    test.Check(SameQuat(invalidLive.state.correction, Quat4{}),
        "fail-open correction is identity");

    // Invalid one-hand counterfactual with a valid last-presented orientation
    // falls back to last_presented instead of fabricating a jump.
    Rig invalidOneHand;
    invalidOneHand.oneHandValid = false;
    invalidOneHand.lastPresentedValid = true;
    invalidOneHand.lastPresented = Calibrated(kOneHandBase, kGunYawDeg,
        kGunPitchDeg, kGunRollDeg);
    invalidOneHand.Advance();
    invalidOneHand.latched = true;
    invalidOneHand.owns = true;
    invalidOneHand.Advance();
    const auto fallback = invalidOneHand.Diag();
    test.Check(fallback.active && fallback.anchor_source ==
            AimContinuityAnchorSource::LastPresented,
        "unavailable one-hand anchor falls back to last_presented");
    test.CheckAngle(invalidOneHand.Presented(),
        invalidOneHand.lastPresented,
        "last_presented fallback presents the previous orientation", 0.001f);
}

// ---------------------------------------------------------------------------
// 8. Once-per-serial lifetime (no hidden per-frame reset).
// ---------------------------------------------------------------------------
void TestOncePerSerialAndConsecutiveSerialLifetime(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    rig.Advance();
    const auto before = rig.Diag();
    const Quat4 beforeCorrection = rig.state.correction;
    const uint64_t beforeSerial = rig.state.lastPreparedSerial;
    const Quat4 beforePresented = rig.Presented();

    // Duplicate serial: a full no-op for state and reads.
    const auto repeat = rig.Input();
    virtual_stock::AdvanceAimContinuity(rig.state, repeat);
    virtual_stock::AdvanceAimContinuity(rig.state, repeat);
    test.Check(SameDiagnostics(before, rig.Diag()),
        "duplicate-serial advance is a no-op for diagnostics");
    test.Check(SameQuat(beforeCorrection, rig.state.correction),
        "duplicate-serial advance is a no-op for the correction");
    test.Check(beforeSerial == rig.state.lastPreparedSerial,
        "duplicate-serial advance does not consume the serial again");
    test.Check(SameQuat(beforePresented, rig.Presented()),
        "duplicate-serial advance is a no-op for Apply");

    // Reads (Apply/diagnostics) are const and never mutate the state.
    for (int read = 0; read < 5; ++read)
    {
        (void)rig.Presented();
        (void)rig.Diag();
    }
    test.Check(SameQuat(beforeCorrection, rig.state.correction) &&
            SameDiagnostics(before, rig.Diag()),
        "Apply and diagnostics reads never mutate state");

    // A long run of consecutive prepared serials must advance the same state
    // object to the end of the acquisition and through a whole release: the
    // layer must not require (or perform) a per-frame reset, which is the
    // defect this guards against in the live integration.
    Rig lifetime;
    BeginHeld(lifetime);
    for (int frame = 0; frame < 12; ++frame)
        lifetime.Advance();
    test.Check(lifetime.Diag().active,
        "the lifetime fixture is still transitioning after 12 serials");
    lifetime.Advance();
    test.Check(!lifetime.Diag().active,
        "the acquisition completes across consecutive serials without a "
        "reset");

    lifetime.latched = false;
    lifetime.owns = false;
    lifetime.live = OneHandLive();
    lifetime.oneHand = lifetime.live;
    lifetime.Advance();
    test.Check(lifetime.Diag().active &&
            lifetime.Diag().phase == AimContinuityPhase::Release,
        "the release edge is observed on the next consecutive serial");
    for (int frame = 0; frame < 13; ++frame)
        lifetime.Advance();
    test.Check(!lifetime.Diag().active &&
            SameQuat(lifetime.Presented(), lifetime.live),
        "the release completes across consecutive serials without a reset");
    test.Check(lifetime.state.lastPreparedSerial == lifetime.serial,
        "the module tracked every consecutive prepared serial");
}

// ---------------------------------------------------------------------------
// 9. Reset.
// ---------------------------------------------------------------------------
void TestReset(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    rig.Advance();
    test.Check(rig.Diag().active && !SameQuat(rig.state.correction, Quat4{}),
        "fixture has an active transition before Reset");

    virtual_stock::ResetAimContinuity(rig.state);
    const auto reset = rig.Diag();
    test.Check(!reset.active && reset.phase == AimContinuityPhase::Idle &&
            reset.edge_kind == AimContinuityEdgeKind::None &&
            reset.anchor_source == AimContinuityAnchorSource::None &&
            reset.remaining_correction_deg == 0.0f &&
            reset.initial_correction_deg == 0.0f &&
            reset.last_prepared_serial == 0,
        "Reset clears to identity and idle");
    test.Check(SameQuat(rig.Presented(), rig.live),
        "post-reset Apply presents the live pose exactly");
    test.Check(SameQuat(rig.state.correction, Quat4{}),
        "post-reset correction is identity");

    // The first observed frame after Reset never manufactures an edge, even
    // when the latch is already engaged.
    rig.latched = true;
    rig.owns = true;
    rig.Advance();
    test.Check(!rig.Diag().active && rig.Diag().phase == AimContinuityPhase::Idle,
        "post-reset first observation of a held latch is not an edge");
    test.Check(SameQuat(rig.Presented(), rig.live),
        "post-reset baseline presents the live pose exactly");

    // A genuine release edge after Reset is still processed.
    rig.latched = false;
    rig.owns = false;
    rig.Advance();
    test.Check(rig.Diag().active &&
            rig.Diag().phase == AimContinuityPhase::Release,
        "post-reset edges are processed normally again");
}

// ---------------------------------------------------------------------------
// 10. Degenerate inputs.
// ---------------------------------------------------------------------------
void TestDegenerateInputs(TestContext& test)
{
    Rig rig;
    BeginHeld(rig);
    rig.Advance();
    const auto before = rig.Diag();
    const Quat4 beforeCorrection = rig.state.correction;
    const uint64_t beforeSerial = rig.state.lastPreparedSerial;

    const float badTimes[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        0.0f,
        -0.01f};
    for (const float badTime : badTimes)
    {
        rig.dt = badTime;
        rig.AdvanceRaw();
        test.Check(SameDiagnostics(before, rig.Diag()),
            "degenerate dt leaves diagnostics unchanged");
        test.Check(SameQuat(beforeCorrection, rig.state.correction),
            "degenerate dt leaves the correction unchanged");
        test.Check(beforeSerial == rig.state.lastPreparedSerial,
            "degenerate dt does not consume the serial");
    }

    // The same serial is processed once the timing becomes valid again.
    rig.dt = 1.0f / 64.0f;
    rig.AdvanceRaw();
    test.Check(!SameDiagnostics(before, rig.Diag()) &&
            rig.state.lastPreparedSerial == rig.serial,
        "valid timing processes the next serial after a degenerate frame");
}

// ---------------------------------------------------------------------------
// 11. Determinism.
// ---------------------------------------------------------------------------
void TestDeterminism(TestContext& test)
{
    auto buildScript = []()
    {
        std::vector<virtual_stock::AimContinuityInput> script;
        Rig rig;
        const auto record = [&]()
        {
            rig.AdvanceRaw();
            rig.lastPresented =
                virtual_stock::ApplyAimContinuity(rig.state, rig.live);
            rig.lastPresentedValid = true;
            script.push_back(rig.Input());
        };
        for (int frame = 0; frame < 6; ++frame)
            record();
        rig.latched = true;
        rig.owns = true;
        record();
        for (int frame = 0; frame < 8; ++frame)
        {
            rig.live = Calibrated(
                AxisAngle(0.30f, 0.90f, 0.10f, 41.0f + frame),
                kGunYawDeg, kGunPitchDeg, kGunRollDeg);
            record();
        }
        rig.latched = false;
        rig.owns = false;
        rig.live = OneHandLive();
        rig.oneHand = rig.live;
        record();
        for (int frame = 0; frame < 5; ++frame)
            record();
        rig.latched = true;
        rig.owns = true;
        rig.live = StockLive();
        rig.oneHand = OneHandLive();
        record();
        rig.dt = std::numeric_limits<float>::quiet_NaN();
        record();
        rig.dt = 1.0f / 64.0f;
        record();
        return script;
    };

    const auto script = buildScript();
    std::vector<virtual_stock::AimContinuityState> states(2);
    std::vector<Quat4> presented[2];
    std::vector<virtual_stock::AimContinuityDiagnostics> recorded[2];
    for (int run = 0; run < 2; ++run)
    {
        for (const auto& input : script)
        {
            virtual_stock::AdvanceAimContinuity(states[run], input);
            presented[run].push_back(
                virtual_stock::ApplyAimContinuity(states[run],
                    input.liveOrientation));
            recorded[run].push_back(
                virtual_stock::ReadAimContinuityDiagnostics(states[run]));
        }
    }

    test.Check(script.size() > 20, "determinism script covers edges and eases");
    bool sameSequence = true;
    for (size_t index = 0; index < script.size(); ++index)
    {
        if (!SameQuat(presented[0][index], presented[1][index]) ||
            !SameDiagnostics(recorded[0][index], recorded[1][index]))
            sameSequence = false;
    }
    test.Check(sameSequence,
        "identical input sequences produce identical outputs");
    test.Check(SameDiagnostics(
            virtual_stock::ReadAimContinuityDiagnostics(states[0]),
            virtual_stock::ReadAimContinuityDiagnostics(states[1])),
        "identical input sequences produce identical final diagnostics");

    // The script must actually exercise a transition, not just idle frames.
    const auto diagnostics =
        virtual_stock::ReadAimContinuityDiagnostics(states[0]);
    test.Check(diagnostics.edge_kind != AimContinuityEdgeKind::None &&
            diagnostics.initial_correction_deg > 0.0f,
        "determinism script exercised a real transition");
}
} // namespace

int main()
{
    TestContext test;
    TestCompositionSeedEquality(test);
    TestAcquireFixedEase(test);
    TestMovingLiveTargetDuringAcquire(test);
    TestReleaseFixedEase(test);
    TestMovingLiveTargetDuringRelease(test);
    TestOwnershipEdgesWithoutLatchChange(test);
    TestLatchOnlyFreeAimIgnoresOwnershipChanges(test);
    TestVsOffAgreementFloorCrossingsDoNotRestartContinuity(test);
    TestReleaseWinsWhenBothEdgesCoincide(test);
    TestRapidReleaseRegrab(test);
    TestReentrantWithoutCallerAnchor(test);
    TestFailOpen(test);
    TestOncePerSerialAndConsecutiveSerialLifetime(test);
    TestReset(test);
    TestDegenerateInputs(test);
    TestDeterminism(test);
    std::printf("%u checks, %u failures\n", test.checks, test.failures);
    return test.failures ? 1 : 0;
}
