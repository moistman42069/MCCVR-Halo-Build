#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "runtime_types.h"

constexpr size_t kTitleRuntimeSlotCount = 6;
constexpr size_t kInvalidTitleRuntimeSlot = kTitleRuntimeSlotCount;

constexpr size_t TitleRuntimeSlotIndex(GameTitle title)
{
    switch (title)
    {
    case GameTitle::Halo3: return 0;
    case GameTitle::Halo3ODST: return 1;
    case GameTitle::HaloReach: return 2;
    case GameTitle::Halo4: return 3;
    case GameTitle::HaloCE: return 4;
    case GameTitle::Halo2: return 5;
    default: return kInvalidTitleRuntimeSlot;
    }
}

constexpr GameTitle TitleRuntimeSlotTitle(size_t slot)
{
    constexpr GameTitle titles[kTitleRuntimeSlotCount] = {
        GameTitle::Halo3,
        GameTitle::Halo3ODST,
        GameTitle::HaloReach,
        GameTitle::Halo4,
        GameTitle::HaloCE,
        GameTitle::Halo2,
    };
    return slot < kTitleRuntimeSlotCount ? titles[slot] : GameTitle::None;
}

constexpr uint32_t TitleRuntimeAvailabilityBit(GameTitle title)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    return slot < kTitleRuntimeSlotCount ? uint32_t{1} << slot : 0;
}

constexpr uint32_t kTitleRuntimeAvailabilityMask =
    (uint32_t{1} << kTitleRuntimeSlotCount) - 1;

constexpr uint32_t kTitleRuntimeKnownCapabilities =
    TitleCapability_Stereo |
    TitleCapability_ControllerAim |
    TitleCapability_Hud |
    TitleCapability_ArmIk |
    TitleCapability_RuntimeModes |
    TitleCapability_RoomScale |
    TitleCapability_ControllerInput |
    TitleCapability_Haptics |
    TitleCapability_CutsceneTheater;

// Heartbeat freshness windows, promoted from src/dll/game.cpp so an offline
// test pins every value. ResolveTitleRuntime disqualifies a candidate whose
// window is zero, silently and unconditionally: a runtime-slotted title left
// out of this table can never become the resolved owner, every shared
// capability is denied, and the worker's fallback publication fights the
// present path (the Reach "Runtime mode: gameplay -> loading" flap). Every
// title that gains a runtime MUST gain a nonzero entry here in the same
// change.
constexpr uint64_t kTitleRuntimeHeartbeatFreshMs = 500;
// ODST's camera copy can legitimately go quiet for its hard timeout, so its
// window must outlast kOdstCameraHardTimeoutMs (5000); a static_assert in
// src/dll/game.cpp, where both headers are visible, pins the +1 relation.
constexpr uint64_t kOdstTitleRuntimeHeartbeatWindowMs = 5001;

constexpr uint64_t TitleRuntimeHeartbeatWindowMs(GameTitle title)
{
    switch (title)
    {
    case GameTitle::Halo3: return kTitleRuntimeHeartbeatFreshMs;
    case GameTitle::Halo3ODST: return kOdstTitleRuntimeHeartbeatWindowMs;
    // Reach and Halo 4 have no camera-copy hook; each heartbeats once per
    // armed Present, the fastest cadence of the titles, so Halo 3's window
    // fits both.
    case GameTitle::HaloReach: return kTitleRuntimeHeartbeatFreshMs;
    case GameTitle::Halo4: return kTitleRuntimeHeartbeatFreshMs;
    default: return 0;
    }
}

struct TitleRuntimeCandidate
{
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uint64_t generationStartMs = 0;
    bool installed = false;
    bool armed = false;
    bool teardownRequested = false;
    RuntimeMode mode = RuntimeMode::Shell;
    uint64_t heartbeatMs = 0;
    uint64_t heartbeatFreshForMs = 0;
    uint32_t enabledCapabilities = TitleCapability_None;
};

struct TitleRuntimeResolveInput
{
    uint32_t availabilityMask = 0;
    uint64_t availabilitySetEpochMs = 0;
    uint64_t nowMs = 0;
    std::array<TitleRuntimeCandidate, kTitleRuntimeSlotCount> titles{};
};

struct TitleRuntimeSnapshot
{
    GameTitle owner = GameTitle::None;
    uint32_t generation = 0;
    bool installed = false;
    bool armed = false;
    bool teardownRequested = false;
    RuntimeMode mode = RuntimeMode::Shell;
    uint64_t heartbeatMs = 0;
    uint32_t enabledCapabilities = TitleCapability_None;
    uint32_t qualifyingOwnerCount = 0;
};

// Pure ownership policy. A module can remain resident without retaining
// ownership: its generation-tagged heartbeat must be newer than both its own
// module generation and the most recent change to the complete module set.
TitleRuntimeSnapshot ResolveTitleRuntime(
    const TitleRuntimeResolveInput& input) noexcept;

// A newly changed module set gets a short opportunity to publish its first
// post-transition heartbeat. This is distinct from ownership: multiple fresh
// owners are never pending and must fail closed immediately.
bool TitleRuntimeOwnershipMayBePending(
    const TitleRuntimeResolveInput& input, GameTitle retainedOwner,
    uint64_t graceMs) noexcept;

// The owner may be selected before it is armed. Callers identify the
// capabilities that require an armed transaction; all other capabilities stay
// untouched.
uint32_t TitleRuntimeMaskUnarmedCapabilities(
    const TitleRuntimeSnapshot& snapshot,
    uint32_t capabilitiesRequiringArm) noexcept;

struct TitleRuntimeModuleSet
{
    uint32_t availabilityMask = 0;
    std::array<uintptr_t, kTitleRuntimeSlotCount> moduleBases{};
};

struct TitleRuntimeAvailabilitySnapshot
{
    bool stable = false;
    uint32_t availabilityMask = 0;
    uint64_t availabilitySetEpochMs = 0;
    uint32_t revision = 0;
    std::array<uintptr_t, kTitleRuntimeSlotCount> moduleBases{};
};

struct TitleRuntimeLifecycle
{
    bool installed = false;
    bool armed = false;
    bool teardownRequested = false;
    uint32_t enabledCapabilities = TitleCapability_None;
};

struct TitleRuntimeHeartbeatPolicy
{
    std::array<uint64_t, kTitleRuntimeSlotCount> freshForMs{};
};

constexpr TitleRuntimeHeartbeatPolicy MakeTitleRuntimeHeartbeatPolicy()
{
    TitleRuntimeHeartbeatPolicy policy{};
    for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
        policy.freshForMs[slot] =
            TitleRuntimeHeartbeatWindowMs(TitleRuntimeSlotTitle(slot));
    return policy;
}

// The pending-ownership retention title, from the per-slot camera-core
// install generations (nonzero from install to teardown - NOT the module
// residency generations, which are nonzero for every resident DLL). Exactly
// one nonzero generation retains its title through the 100 ms
// post-transition grace; zero or multiple fail closed to None, matching the
// established Halo 3 + ODST rule.
constexpr GameTitle RetainedRuntimeTitleFromGenerations(
    const std::array<uint32_t, kTitleRuntimeSlotCount>& generations) noexcept
{
    GameTitle retained = GameTitle::None;
    for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
    {
        if (generations[slot] == 0)
            continue;
        if (retained != GameTitle::None)
            return GameTitle::None;
        retained = TitleRuntimeSlotTitle(slot);
    }
    return retained;
}

// Fixed storage only. Publications are generation tagged, and snapshots use
// double-generation reads so data from a departed/reloaded title cannot become
// another title's state. PublishHeartbeat is suitable for a hot camera hook:
// it performs bounded atomic operations only (no allocation, lock, logging, or
// operating-system call).
class TitleRuntimeState
{
public:
    TitleRuntimeState() noexcept = default;
    TitleRuntimeState(const TitleRuntimeState&) = delete;
    TitleRuntimeState& operator=(const TitleRuntimeState&) = delete;

    bool PublishModuleSet(
        const TitleRuntimeModuleSet& modules, uint64_t observedAtMs) noexcept;

    uint32_t Generation(GameTitle title) const noexcept;
    TitleRuntimeAvailabilitySnapshot LoadAvailability() const noexcept;
    bool LoadCandidate(
        GameTitle title, uint64_t heartbeatFreshForMs,
        TitleRuntimeCandidate& candidate) const noexcept;

    bool PublishLifecycle(
        GameTitle title, uint32_t generation,
        const TitleRuntimeLifecycle& lifecycle) noexcept;
    bool PublishMode(
        GameTitle title, uint32_t generation, RuntimeMode mode) noexcept;
    bool PublishHeartbeat(
        GameTitle title, uint32_t generation, uint64_t heartbeatMs) noexcept;
    bool ClearHeartbeat(
        GameTitle title, uint32_t generation) noexcept;

    TitleRuntimeSnapshot Resolve(
        uint64_t nowMs, const TitleRuntimeHeartbeatPolicy& policy) const noexcept;

private:
    struct Slot
    {
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> generationStartMs{0};
        std::atomic<uintptr_t> moduleBase{0};

        std::atomic<uint32_t> lifecycleSequence{0};
        std::atomic<uint32_t> lifecycleGeneration{0};
        std::atomic<uint8_t> installed{0};
        std::atomic<uint8_t> armed{0};
        std::atomic<uint8_t> teardownRequested{0};
        std::atomic<uint32_t> enabledCapabilities{TitleCapability_None};

        std::atomic<uint64_t> stampedMode{0};

        std::atomic<uint32_t> heartbeatSequence{0};
        std::atomic<uint32_t> heartbeatGeneration{0};
        std::atomic<uint64_t> heartbeatMs{0};
    };

    std::array<Slot, kTitleRuntimeSlotCount> m_slots{};
    std::atomic<uint32_t> m_availabilitySequence{0};
    std::atomic<uint32_t> m_availabilityMask{0};
    std::atomic<uint64_t> m_availabilitySetEpochMs{0};
};
