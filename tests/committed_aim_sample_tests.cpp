#include <Windows.h>
#include <openxr/openxr.h>

#include "../src/common/virtual_stock_logic.h"
#include "../src/common/two_hand_input_smoothing.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/dll/aim_pose_trace.h"

XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const XrVector3f u{q.x, q.y, q.z};
    const auto cross = [](const XrVector3f& a, const XrVector3f& b) {
        return XrVector3f{a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    XrVector3f c1 = cross(u, v);
    c1.x += q.w * v.x;
    c1.y += q.w * v.y;
    c1.z += q.w * v.z;
    const XrVector3f c2 = cross(u, c1);
    return {v.x + 2.0f * c2.x, v.y + 2.0f * c2.y,
        v.z + 2.0f * c2.z};
}

#include "aim_pose_functions.inl"
#include "../src/dll/committed_aim_sample.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <thread>

namespace
{
unsigned checks = 0;
unsigned failures = 0;

void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

bool Near(float a, float b, float epsilon = 1.0e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

bool SamePose(const XrPosef& a, const XrPosef& b)
{
    return Near(a.position.x, b.position.x) &&
        Near(a.position.y, b.position.y) &&
        Near(a.position.z, b.position.z) &&
        Near(a.orientation.x, b.orientation.x) &&
        Near(a.orientation.y, b.orientation.y) &&
        Near(a.orientation.z, b.orientation.z) &&
        Near(a.orientation.w, b.orientation.w);
}

bool SameAim(const AimPoseResult& a, const AimPoseResult& b)
{
    return a.valid == b.valid &&
        a.updateTwoHandActivity == b.updateTwoHandActivity &&
        a.twoHandActive == b.twoHandActive &&
        a.rejectedExtreme == b.rejectedExtreme &&
        Near(a.rejectedAgreement, b.rejectedAgreement) &&
        SamePose(a.pose, b.pose);
}

float QuaternionDifference(const XrQuaternionf& a, const XrQuaternionf& b)
{
    const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::clamp(dot, 0.0f, 1.0f));
}

committed_aim::Sample CoherentSample()
{
    committed_aim::Sample sample{};
    sample.preparedSerial = 41;
    sample.contactSpaceEpoch = 9;
    sample.sessionEpoch = 3;
    sample.title = GameTitle::Halo3;
    sample.titleGeneration = 12;
    sample.commitTimeMs = 1000;
    auto& values = sample.values;
    values.okR = values.okL = values.okH = true;
    values.supportGripValid = values.primaryGripValid = true;
    values.rightAimPose.orientation = {0, 0, 0, 1};
    values.rightAimPose.position = {0.05f, 1.2f, -0.1f};
    values.leftAimPose.orientation = {0, 0, 0, 1};
    values.leftAimPose.position = {0.02f, 1.2f, -1.1f};
    values.headPose.orientation = {0, 0, 0, 1};
    values.headPose.position = {0, 1.6f, 0};
    values.primaryGripPosition = {0.05f, 1.2f, 0.2f};
    values.supportGripPosition = {0.55f, 1.2f, -0.35f};
    values.inverseNeckNeutralValid = true;
    values.inverseNeckNeutralOrientation = {0.1f, -0.2f, 0.3f, 0.9f};
    values.inverseNeckNeutralCaptureSerial = sample.preparedSerial;
    values.inverseNeckNeutralCaptureContactSpaceEpoch =
        sample.contactSpaceEpoch;
    return sample;
}

committed_aim::Identity IdentityOf(const committed_aim::Sample& sample)
{
    return {sample.preparedSerial, sample.contactSpaceEpoch,
        sample.sessionEpoch, sample.title, sample.titleGeneration,
        sample.values.capturedLeftHanded};
}

AimPoseInputs ProductInputs(bool virtualStock = false)
{
    AimPoseInputs inputs{};
    inputs.twoHandEnabled = true;
    inputs.twoHandLatched = true;
    inputs.twoHandToggle = true;
    inputs.virtualStockEnabled = virtualStock;
    inputs.virtualStockStrength = 0.95f;
    inputs.virtualStockRearHeightM = -0.22f;
    inputs.virtualStockRearReference = 0;
    inputs.virtualStockHybridOffhandInfluence = 0.0f;
    inputs.virtualStockHybridAdsReference = 0;
    inputs.virtualStockHybridSeatFullM = 10.0f;
    inputs.virtualStockHybridSeatReleaseM = 11.0f;
    inputs.hybridHorizontalRearReleaseEnabled = false;
    inputs.hybridInverseNeckEnabled = false;
    inputs.virtualStockProximityRelease = false;
    inputs.gunYawDeg = inputs.gunPitchDeg = inputs.gunRollDeg = 0.0f;
    return inputs;
}

AimPoseInputs InputsFrom(const committed_aim::AimValues& values,
    bool virtualStock = false)
{
    AimPoseInputs inputs = ProductInputs(virtualStock);
    committed_aim::ApplyAimValuesToSolverInputs(inputs, values);
    // The real stock-aware constructor derives this from the selected raw left
    // aim and its product support-endpoint setting. The VS-off production
    // fallback exercised here uses the same left aim endpoint.
    inputs.supportPosition = values.leftAimPose.position;
    return inputs;
}

AimPoseResult Solve(const committed_aim::AimValues& values,
    bool virtualStock = false)
{
    return ComputeAimPose(InputsFrom(values, virtualStock));
}

bool ConsistentSerial(const committed_aim::Sample& value)
{
    const float n = static_cast<float>(value.preparedSerial);
    return value.preparedSerial != 0 &&
        value.contactSpaceEpoch == value.preparedSerial + 100 &&
        value.sessionEpoch == static_cast<uint32_t>(value.preparedSerial) &&
        value.title == ((value.preparedSerial & 1u)
            ? GameTitle::Halo3 : GameTitle::HaloReach) &&
        value.titleGeneration == static_cast<uint32_t>(value.preparedSerial + 7) &&
        value.commitTimeMs == value.preparedSerial + 2000 &&
        value.values.okR == ((value.preparedSerial & 1u) != 0) &&
        value.values.okL == ((value.preparedSerial & 1u) == 0) &&
        value.values.okH &&
        value.values.supportGripValid == ((value.preparedSerial & 1u) != 0) &&
        value.values.primaryGripValid == ((value.preparedSerial & 1u) == 0) &&
        value.values.capturedLeftHanded == ((value.preparedSerial & 1u) != 0) &&
        value.values.rightAimPose.position.x == n &&
        value.values.rightAimPose.orientation.x == n + 1.0f &&
        value.values.leftAimPose.position.y == n + 2.0f &&
        value.values.headPose.position.z == n + 3.0f &&
        value.values.headPose.orientation.w == n + 4.0f &&
        value.values.supportGripPosition.x == n + 5.0f &&
        value.values.primaryGripPosition.z == n + 6.0f &&
        value.values.inverseNeckNeutralValid ==
            ((value.preparedSerial & 1u) != 0) &&
        value.values.inverseNeckNeutralOrientation.x == n + 7.0f &&
        value.values.inverseNeckNeutralOrientation.y == n + 8.0f &&
        value.values.inverseNeckNeutralOrientation.z == n + 9.0f &&
        value.values.inverseNeckNeutralOrientation.w == n + 10.0f &&
        value.values.inverseNeckNeutralCaptureSerial ==
            value.preparedSerial + 11 &&
        value.values.inverseNeckNeutralCaptureContactSpaceEpoch ==
            value.preparedSerial + 12;
}

committed_aim::Sample SerialSample(uint64_t serial)
{
    committed_aim::Sample sample{};
    sample.preparedSerial = serial;
    sample.contactSpaceEpoch = serial + 100;
    sample.sessionEpoch = static_cast<uint32_t>(serial);
    sample.title = (serial & 1u) ? GameTitle::Halo3 : GameTitle::HaloReach;
    sample.titleGeneration = static_cast<uint32_t>(serial + 7);
    sample.commitTimeMs = serial + 2000;
    sample.values.okR = (serial & 1u) != 0;
    sample.values.okL = (serial & 1u) == 0;
    sample.values.okH = true;
    sample.values.supportGripValid = (serial & 1u) != 0;
    sample.values.primaryGripValid = (serial & 1u) == 0;
    sample.values.capturedLeftHanded = (serial & 1u) != 0;
    sample.values.rightAimPose.position.x = static_cast<float>(serial);
    sample.values.rightAimPose.orientation.x = static_cast<float>(serial + 1);
    sample.values.leftAimPose.position.y = static_cast<float>(serial + 2);
    sample.values.headPose.position.z = static_cast<float>(serial + 3);
    sample.values.headPose.orientation.w = static_cast<float>(serial + 4);
    sample.values.supportGripPosition.x = static_cast<float>(serial + 5);
    sample.values.primaryGripPosition.z = static_cast<float>(serial + 6);
    sample.values.inverseNeckNeutralValid = (serial & 1u) != 0;
    sample.values.inverseNeckNeutralOrientation = {
        static_cast<float>(serial + 7), static_cast<float>(serial + 8),
        static_cast<float>(serial + 9), static_cast<float>(serial + 10)};
    sample.values.inverseNeckNeutralCaptureSerial = serial + 11;
    sample.values.inverseNeckNeutralCaptureContactSpaceEpoch = serial + 12;
    return sample;
}

void TestCommittedGeometryBeatsLiveCaptureInvalidation()
{
    committed_aim::Publication publication{};
    const auto sample = CoherentSample();
    publication.Publish(sample);
    committed_aim::Sample selected{};
    Check(committed_aim::TrySelectCurrent(publication, true,
            IdentityOf(sample), sample.commitTimeMs + 1, selected),
        "valid committed sample is selected for its exact published identity");

    AimPoseInputs serialInputs = InputsFrom(selected.values);
    serialInputs.supportSolveSerial = selected.preparedSerial;
    const AimPoseResult serialAim = ComputeAimPose(serialInputs);
    Check(serialAim.valid &&
            serialAim.supportSolveSerial == selected.preparedSerial,
        "solver result preserves the serial assigned by its committed assembly");

    auto live = sample.values;
    // This represents the current capture invalidating stock/grip freshness
    // while its next Aim/Grip/head sample is still being assembled.
    live.okH = false;
    live.supportGripValid = false;
    live.primaryGripValid = false;
    const AimPoseResult committedAim = Solve(selected.values);
    const AimPoseResult liveAim = Solve(live);
    Check(committedAim.valid && committedAim.twoHandActive,
        "committed complete grip pair produces the production two-hand solve");
    Check(liveAim.valid && liveAim.twoHandActive,
        "cleared grip gates retain the production Aim-to-Aim fallback");
    Check(QuaternionDifference(committedAim.pose.orientation,
            liveAim.pose.orientation) > 0.10f,
        "selected committed GG geometry wins instead of the live AA fallback");
}

void TestPublicationIsAllOrNothing()
{
    committed_aim::Publication publication{};
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<uint32_t> coherentReads{0};
    std::atomic<uint32_t> hybridReads{0};
    std::thread writer([&] {
        started.store(true, std::memory_order_release);
        for (uint64_t serial = 1; serial <= 30000; ++serial)
            publication.Publish(SerialSample(serial));
        done.store(true, std::memory_order_release);
    });
    std::thread reader([&] {
        while (!started.load(std::memory_order_acquire))
            std::this_thread::yield();
        do
        {
            committed_aim::Sample observed{};
            if (publication.TryRead(observed))
            {
                coherentReads.fetch_add(1, std::memory_order_relaxed);
                if (!ConsistentSerial(observed))
                    hybridReads.fetch_add(1, std::memory_order_relaxed);
            }
        } while (!done.load(std::memory_order_acquire));
    });
    writer.join();
    reader.join();
    committed_aim::Sample finalSample{};
    Check(publication.TryRead(finalSample) && ConsistentSerial(finalSample),
        "the final N+1 sample contains only its own fields");
    Check(coherentReads.load(std::memory_order_relaxed) != 0,
        "concurrent reader observed at least one committed publication");
    Check(hybridReads.load(std::memory_order_relaxed) == 0,
        "no read combines fields from N and N+1");
}

void TestCommittedGripInvalidityUsesExistingAimFallback()
{
    for (const bool missingSupport : {true, false})
    {
        committed_aim::Publication publication{};
        auto sample = CoherentSample();
        if (missingSupport)
        {
            sample.values.supportGripValid = false;
            // Deliberately nonzero old storage: validity, not last-known-good
            // coordinates, controls the production endpoint selection.
            sample.values.supportGripPosition = {4.0f, -3.0f, 2.0f};
        }
        else
        {
            sample.values.primaryGripValid = false;
            sample.values.primaryGripPosition = {-4.0f, 3.0f, -2.0f};
        }
        publication.Publish(sample);
        committed_aim::Sample selected{};
        Check(committed_aim::TrySelectCurrent(publication, true,
                IdentityOf(sample), sample.commitTimeMs, selected),
            missingSupport ? "sample with missing support grip remains selectable"
                           : "sample with missing primary grip remains selectable");
        const AimPoseResult selectedAim = Solve(selected.values);

        auto expectedValues = sample.values;
        expectedValues.primaryGripValid = false;
        expectedValues.supportGripValid = false;
        const AimPoseResult expectedAim = Solve(expectedValues);
        const AimPoseResult forgedLastKnownGood = Solve(CoherentSample().values);
        Check(SameAim(selectedAim, expectedAim),
            missingSupport
                ? "missing committed support grip passes invalidity into AA fallback"
                : "missing committed primary grip passes invalidity into AA fallback");
        Check(QuaternionDifference(selectedAim.pose.orientation,
                forgedLastKnownGood.pose.orientation) > 0.10f,
            "invalid committed grip data is not replaced by a last-known-good pair");
    }
}

void TestStartupAndIdentityRejectionFailOpen()
{
    committed_aim::Publication empty{};
    const auto sample = CoherentSample();
    committed_aim::Sample selected{};
    Check(!empty.TryRead(selected) &&
            !committed_aim::TrySelectCurrent(empty, true, IdentityOf(sample),
                sample.commitTimeMs, selected),
        "startup with no publication does not expose static-initialized sample data");

    committed_aim::Publication publication{};
    publication.Publish(sample);
    auto mismatch = IdentityOf(sample);
    ++mismatch.contactSpaceEpoch;
    Check(!committed_aim::TrySelectCurrent(publication, true, mismatch,
            sample.commitTimeMs, selected),
        "contact-space epoch change rejects committed sample");
    mismatch = IdentityOf(sample);
    mismatch.capturedLeftHanded = !mismatch.capturedLeftHanded;
    Check(!committed_aim::TrySelectCurrent(publication, true, mismatch,
            sample.commitTimeMs, selected),
        "handedness change rejects committed sample");

    mismatch = IdentityOf(sample);
    ++mismatch.titleGeneration;
    Check(!committed_aim::TrySelectCurrent(publication, true, mismatch,
            sample.commitTimeMs, selected),
        "title generation change rejects committed sample");
    mismatch = IdentityOf(sample);
    mismatch.title = GameTitle::HaloReach;
    Check(!committed_aim::TrySelectCurrent(publication, true, mismatch,
            sample.commitTimeMs, selected),
        "active title change rejects committed sample");
    mismatch = IdentityOf(sample);
    ++mismatch.sessionEpoch;
    Check(!committed_aim::TrySelectCurrent(publication, true, mismatch,
            sample.commitTimeMs, selected),
        "session epoch change rejects committed sample");
    mismatch = IdentityOf(sample);
    ++mismatch.preparedSerial;
    Check(!committed_aim::TrySelectCurrent(publication, true, mismatch,
            sample.commitTimeMs, selected),
        "prepared serial change rejects committed sample");
}

void TestOffAndStaleSamplesFallBack()
{
    committed_aim::Publication publication{};
    const auto sample = CoherentSample();
    publication.Publish(sample);
    committed_aim::Sample selected{};
    Check(!committed_aim::TrySelectCurrent(publication, false,
            IdentityOf(sample), sample.commitTimeMs, selected),
        "OFF skips committed-sample selection even when a matching sample exists");
    Check(selected.preparedSerial == 0,
        "OFF leaves the caller without a committed sample to apply");

    Check(committed_aim::TrySelectCurrent(publication, true,
            IdentityOf(sample), sample.commitTimeMs +
                committed_aim::kCommittedAimSampleMaxAgeMs, selected),
        "sample at the exact freshness boundary is accepted");
    Check(!committed_aim::TrySelectCurrent(publication, true,
            IdentityOf(sample), sample.commitTimeMs +
                committed_aim::kCommittedAimSampleMaxAgeMs + 1, selected),
        "sample older than the freshness bound is rejected");
    Check(!committed_aim::TrySelectCurrent(publication, true,
            IdentityOf(sample), sample.commitTimeMs - 1, selected),
        "future-dated sample is rejected rather than underflowing its age");
}

void TestOneHandAndVirtualStockAreUnaffected()
{
    committed_aim::Publication publication{};
    const auto sample = CoherentSample();
    publication.Publish(sample);
    committed_aim::Sample selected{};
    Check(committed_aim::TrySelectCurrent(publication, true,
            IdentityOf(sample), sample.commitTimeMs, selected),
        "raw sample selection does not depend on aim mode");

    auto oneHand = selected.values;
    const AimPoseResult enabledOneHand = [&] {
        AimPoseInputs inputs = ProductInputs();
        inputs.twoHandLatched = false;
        committed_aim::ApplyAimValuesToSolverInputs(inputs, oneHand);
        return ComputeAimPose(inputs);
    }();
    Check(enabledOneHand.valid && !enabledOneHand.twoHandActive &&
            SamePose(enabledOneHand.pose, selected.values.rightAimPose),
        "enabled committed mechanism preserves the production one-handed solve");

    for (const int rearReference : {0, 3})
    {
        AimPoseInputs inputs = InputsFrom(selected.values, true);
        inputs.virtualStockRearReference = rearReference;
        inputs.virtualStockStrength = rearReference == 3 ? 0.80f : 0.95f;
        const AimPoseResult actual = ComputeAimPose(inputs);
        AimPoseInputs direct = ProductInputs(true);
        direct.virtualStockRearReference = rearReference;
        direct.virtualStockStrength = rearReference == 3 ? 0.80f : 0.95f;
        direct.rightValid = sample.values.okR;
        direct.right = sample.values.rightAimPose;
        direct.leftValid = sample.values.okL;
        direct.left = sample.values.leftAimPose;
        direct.headValid = sample.values.okH;
        direct.headPosition = sample.values.headPose.position;
        direct.headOrientation = sample.values.headPose.orientation;
        direct.supportGripValid = sample.values.supportGripValid;
        direct.supportGripPosition = sample.values.supportGripPosition;
        direct.primaryGripValid = sample.values.primaryGripValid;
        direct.primaryGripPosition = sample.values.primaryGripPosition;
        direct.virtualStockLeftHanded = sample.values.capturedLeftHanded;
        direct.supportPosition = sample.values.leftAimPose.position;
        const AimPoseResult expected = ComputeAimPose(direct);
        Check(SameAim(actual, expected) && actual.valid,
            rearReference == 3
                ? "committed raw geometry leaves Virtual Stock Plus solver semantics unchanged"
                : "committed raw geometry leaves Virtual Stock Standard solver semantics unchanged");
        Check(selected.values.rightAimPose.position.x == sample.values.rightAimPose.position.x &&
                selected.values.supportGripPosition.x == sample.values.supportGripPosition.x,
            "Virtual Stock mode does not transform raw committed aim/grip geometry");
    }
}

void TestFrozenPreparedSerialCheck()
{
    uint32_t liveSerialReads = 0;
    const auto readLaterSerial = [&]() noexcept {
        ++liveSerialReads;
        return uint64_t{42};
    };

    Check(committed_aim::PreparedSampleSerialStillMatches(
            41, true, readLaterSerial) && liveSerialReads == 0,
        "frozen sample keeps its own identity and bypasses only the final live-serial read");
    Check(!committed_aim::PreparedSampleSerialStillMatches(
            41, false, readLaterSerial) && liveSerialReads == 1,
        "live sample is vetoed when the prepared serial advanced mid-call");
    const auto readExpectedSerial = []() noexcept { return uint64_t{41}; };
    Check(committed_aim::PreparedSampleSerialStillMatches(
            41, false, readExpectedSerial),
        "live sample is accepted while its prepared serial remains current");
}
} // namespace

int main()
{
    TestCommittedGeometryBeatsLiveCaptureInvalidation();
    TestPublicationIsAllOrNothing();
    TestCommittedGripInvalidityUsesExistingAimFallback();
    TestStartupAndIdentityRejectionFailOpen();
    TestOffAndStaleSamplesFallBack();
    TestOneHandAndVirtualStockAreUnaffected();
    TestFrozenPreparedSerialCheck();
    std::printf("Committed aim sample: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
