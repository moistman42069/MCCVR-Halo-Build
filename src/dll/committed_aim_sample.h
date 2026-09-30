#pragma once

#include <atomic>
#include <cstdint>

#include <openxr/openxr.h>

#include "../common/runtime_types.h"

namespace committed_aim
{
// A committed input sample is immutable for the duration of its consumer call.
// Its serial remains the right identity even if a newer frame is published
// mid-call; live-sample consumers must still pass the final serial check. The
// reader is lazy so the frozen path bypasses only that final atomic reload.
template <typename ReadCurrentSerial>
inline bool PreparedSampleSerialStillMatches(uint64_t expectedSerial,
    bool sampleFrozen, ReadCurrentSerial&& readCurrentSerial)
    noexcept(noexcept(readCurrentSerial()))
{
    return sampleFrozen || readCurrentSerial() == expectedSerial;
}

// This matches VR_RoomscaleTrackingFresh's existing 100 ms tracking freshness
// window. It also ensures a temporarily unadvanced publication can never retain
// stale tracking indefinitely.
inline constexpr uint64_t kCommittedAimSampleMaxAgeMs = 100;

struct AimValues
{
    bool okR = false;
    bool okL = false;
    bool okH = false;
    bool supportGripValid = false;
    bool primaryGripValid = false;
    XrPosef rightAimPose{};
    XrPosef leftAimPose{};
    XrPosef headPose{};
    XrVector3f supportGripPosition{};
    XrVector3f primaryGripPosition{};
    bool capturedLeftHanded = false;
    bool inverseNeckNeutralValid = false;
    XrQuaternionf inverseNeckNeutralOrientation{};
    uint64_t inverseNeckNeutralCaptureSerial = 0;
    uint64_t inverseNeckNeutralCaptureContactSpaceEpoch = 0;
};

struct Sample
{
    AimValues values{};
    uint64_t preparedSerial = 0;
    uint64_t contactSpaceEpoch = 0;
    uint32_t sessionEpoch = 0;
    GameTitle title = GameTitle::None;
    uint32_t titleGeneration = 0;
    uint64_t commitTimeMs = 0;
};

struct Identity
{
    uint64_t preparedSerial = 0;
    uint64_t contactSpaceEpoch = 0;
    uint32_t sessionEpoch = 0;
    GameTitle title = GameTitle::None;
    uint32_t titleGeneration = 0;
    bool capturedLeftHanded = false;
};

static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uint8_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);

// Every payload member is atomic: the sequence word makes a multi-field read
// all-or-nothing without relying on racy ordinary-memory seqlock accesses.
struct AtomicVector3
{
    std::atomic<float> x{0.0f};
    std::atomic<float> y{0.0f};
    std::atomic<float> z{0.0f};

    void Store(const XrVector3f& value) noexcept
    {
        x.store(value.x, std::memory_order_relaxed);
        y.store(value.y, std::memory_order_relaxed);
        z.store(value.z, std::memory_order_relaxed);
    }

    XrVector3f Load() const noexcept
    {
        return {x.load(std::memory_order_relaxed),
            y.load(std::memory_order_relaxed),
            z.load(std::memory_order_relaxed)};
    }
};

struct AtomicPose
{
    std::atomic<float> qx{0.0f};
    std::atomic<float> qy{0.0f};
    std::atomic<float> qz{0.0f};
    std::atomic<float> qw{1.0f};
    AtomicVector3 position{};

    void Store(const XrPosef& value) noexcept
    {
        qx.store(value.orientation.x, std::memory_order_relaxed);
        qy.store(value.orientation.y, std::memory_order_relaxed);
        qz.store(value.orientation.z, std::memory_order_relaxed);
        qw.store(value.orientation.w, std::memory_order_relaxed);
        position.Store(value.position);
    }

    XrPosef Load() const noexcept
    {
        XrPosef value{};
        value.orientation = {qx.load(std::memory_order_relaxed),
            qy.load(std::memory_order_relaxed),
            qz.load(std::memory_order_relaxed),
            qw.load(std::memory_order_relaxed)};
        value.position = position.Load();
        return value;
    }
};

struct AtomicQuaternion
{
    std::atomic<float> x{0.0f};
    std::atomic<float> y{0.0f};
    std::atomic<float> z{0.0f};
    std::atomic<float> w{1.0f};

    void Store(const XrQuaternionf& value) noexcept
    {
        x.store(value.x, std::memory_order_relaxed);
        y.store(value.y, std::memory_order_relaxed);
        z.store(value.z, std::memory_order_relaxed);
        w.store(value.w, std::memory_order_relaxed);
    }

    XrQuaternionf Load() const noexcept
    {
        return {x.load(std::memory_order_relaxed),
            y.load(std::memory_order_relaxed),
            z.load(std::memory_order_relaxed),
            w.load(std::memory_order_relaxed)};
    }
};

struct Publication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint64_t> preparedSerial{0};
    std::atomic<uint64_t> contactSpaceEpoch{0};
    std::atomic<uint32_t> sessionEpoch{0};
    std::atomic<uint8_t> title{0};
    std::atomic<uint32_t> titleGeneration{0};
    std::atomic<uint64_t> commitTimeMs{0};
    std::atomic<uint8_t> okR{0}, okL{0}, okH{0};
    std::atomic<uint8_t> supportGripValid{0}, primaryGripValid{0};
    AtomicPose rightAimPose{};
    AtomicPose leftAimPose{};
    AtomicPose headPose{};
    AtomicVector3 supportGripPosition{};
    AtomicVector3 primaryGripPosition{};
    std::atomic<uint8_t> capturedLeftHanded{0};
    std::atomic<uint8_t> inverseNeckNeutralValid{0};
    AtomicQuaternion inverseNeckNeutralOrientation{};
    std::atomic<uint64_t> inverseNeckNeutralCaptureSerial{0};
    std::atomic<uint64_t> inverseNeckNeutralCaptureContactSpaceEpoch{0};

    void Publish(const Sample& sample) noexcept
    {
        sequence.fetch_add(1, std::memory_order_acq_rel);
        // Publish the odd marker before any relaxed payload stores. If a reader
        // observes one of those stores, its matching acquire fence below makes
        // this in-progress sequence update visible to the validation load.
        std::atomic_thread_fence(std::memory_order_release);
        preparedSerial.store(sample.preparedSerial, std::memory_order_relaxed);
        contactSpaceEpoch.store(sample.contactSpaceEpoch,
            std::memory_order_relaxed);
        sessionEpoch.store(sample.sessionEpoch, std::memory_order_relaxed);
        title.store(static_cast<uint8_t>(sample.title),
            std::memory_order_relaxed);
        titleGeneration.store(sample.titleGeneration,
            std::memory_order_relaxed);
        commitTimeMs.store(sample.commitTimeMs, std::memory_order_relaxed);
        okR.store(sample.values.okR ? 1u : 0u, std::memory_order_relaxed);
        okL.store(sample.values.okL ? 1u : 0u, std::memory_order_relaxed);
        okH.store(sample.values.okH ? 1u : 0u, std::memory_order_relaxed);
        supportGripValid.store(sample.values.supportGripValid ? 1u : 0u,
            std::memory_order_relaxed);
        primaryGripValid.store(sample.values.primaryGripValid ? 1u : 0u,
            std::memory_order_relaxed);
        rightAimPose.Store(sample.values.rightAimPose);
        leftAimPose.Store(sample.values.leftAimPose);
        headPose.Store(sample.values.headPose);
        supportGripPosition.Store(sample.values.supportGripPosition);
        primaryGripPosition.Store(sample.values.primaryGripPosition);
        capturedLeftHanded.store(
            sample.values.capturedLeftHanded ? 1u : 0u,
            std::memory_order_relaxed);
        inverseNeckNeutralValid.store(
            sample.values.inverseNeckNeutralValid ? 1u : 0u,
            std::memory_order_relaxed);
        inverseNeckNeutralOrientation.Store(
            sample.values.inverseNeckNeutralOrientation);
        inverseNeckNeutralCaptureSerial.store(
            sample.values.inverseNeckNeutralCaptureSerial,
            std::memory_order_relaxed);
        inverseNeckNeutralCaptureContactSpaceEpoch.store(
            sample.values.inverseNeckNeutralCaptureContactSpaceEpoch,
            std::memory_order_relaxed);
        sequence.fetch_add(1, std::memory_order_release);
    }

    bool TryRead(Sample& out) const noexcept
    {
        out = Sample{};
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;

            Sample candidate{};
            candidate.preparedSerial = preparedSerial.load(
                std::memory_order_relaxed);
            candidate.contactSpaceEpoch = contactSpaceEpoch.load(
                std::memory_order_relaxed);
            candidate.sessionEpoch = sessionEpoch.load(
                std::memory_order_relaxed);
            candidate.title = static_cast<GameTitle>(title.load(
                std::memory_order_relaxed));
            candidate.titleGeneration = titleGeneration.load(
                std::memory_order_relaxed);
            candidate.commitTimeMs = commitTimeMs.load(
                std::memory_order_relaxed);
            candidate.values.okR = okR.load(std::memory_order_relaxed) != 0;
            candidate.values.okL = okL.load(std::memory_order_relaxed) != 0;
            candidate.values.okH = okH.load(std::memory_order_relaxed) != 0;
            candidate.values.supportGripValid =
                supportGripValid.load(std::memory_order_relaxed) != 0;
            candidate.values.primaryGripValid =
                primaryGripValid.load(std::memory_order_relaxed) != 0;
            candidate.values.rightAimPose = rightAimPose.Load();
            candidate.values.leftAimPose = leftAimPose.Load();
            candidate.values.headPose = headPose.Load();
            candidate.values.supportGripPosition =
                supportGripPosition.Load();
            candidate.values.primaryGripPosition =
                primaryGripPosition.Load();
            candidate.values.capturedLeftHanded =
                capturedLeftHanded.load(std::memory_order_relaxed) != 0;
            candidate.values.inverseNeckNeutralValid =
                inverseNeckNeutralValid.load(std::memory_order_relaxed) != 0;
            candidate.values.inverseNeckNeutralOrientation =
                inverseNeckNeutralOrientation.Load();
            candidate.values.inverseNeckNeutralCaptureSerial =
                inverseNeckNeutralCaptureSerial.load(
                    std::memory_order_relaxed);
            candidate.values.inverseNeckNeutralCaptureContactSpaceEpoch =
                inverseNeckNeutralCaptureContactSpaceEpoch.load(
                    std::memory_order_relaxed);

            // Pair with Publish's release fence. This both orders the payload
            // reads before validation and prevents a read that observed any
            // in-progress payload store from validating against the old even
            // sequence value.
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence.load(std::memory_order_acquire) != before)
                continue;
            // Zero is the static-initialized, never-published state.
            if (!candidate.preparedSerial || !candidate.commitTimeMs)
                return false;
            out = candidate;
            return true;
        }
        return false;
    }
};

inline bool MatchesIdentity(const Sample& sample,
    const Identity& current) noexcept
{
    return sample.preparedSerial != 0 &&
        sample.preparedSerial == current.preparedSerial &&
        sample.contactSpaceEpoch == current.contactSpaceEpoch &&
        sample.sessionEpoch == current.sessionEpoch &&
        sample.title == current.title &&
        sample.titleGeneration == current.titleGeneration &&
        sample.values.capturedLeftHanded == current.capturedLeftHanded;
}

inline bool TrySelectCurrent(const Publication& publication,
    bool preferCommittedSample, const Identity& current, uint64_t nowMs,
    Sample& out) noexcept
{
    out = Sample{};
    // Keep the default/off path inert: do not inspect publication state.
    if (!preferCommittedSample)
        return false;

    Sample candidate{};
    if (!publication.TryRead(candidate) || !MatchesIdentity(candidate, current) ||
        nowMs < candidate.commitTimeMs ||
        nowMs - candidate.commitTimeMs > kCommittedAimSampleMaxAgeMs)
        return false;
    out = candidate;
    return true;
}

// Shared field application keeps the committed path and its headless solver
// fixture on the same raw-data mapping. Product/configuration inputs, latch,
// support qualification, smoothing, continuity and Lab state remain untouched.
template <typename SolverInputs>
inline void ApplyAimValuesToSolverInputs(
    SolverInputs& inputs, const AimValues& values) noexcept
{
    inputs.rightValid = values.okR;
    inputs.right = values.rightAimPose;
    inputs.leftValid = values.okL;
    inputs.left = values.leftAimPose;
    inputs.headValid = values.okH;
    inputs.headPosition = values.headPose.position;
    inputs.headOrientation = values.headPose.orientation;
    inputs.supportGripValid = values.supportGripValid;
    inputs.supportGripPosition = values.supportGripPosition;
    inputs.primaryGripValid = values.primaryGripValid;
    inputs.primaryGripPosition = values.primaryGripPosition;
    inputs.virtualStockLeftHanded = values.capturedLeftHanded;
}
} // namespace committed_aim
