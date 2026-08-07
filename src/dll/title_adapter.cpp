#include "title_adapter.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>

#include "../common/coop_probe_logic.h"
#include "../common/log.h"
#include "d3d11_hook.h"
#include "halo4_adapter.h"
#include "reach_adapter.h"

namespace
{
    constexpr uint32_t kAdapterSnapshotReadAttempts = 8;
    constexpr uint64_t kAmbiguousOwnershipPendingMs = 100;

    TitleRuntimeState g_titleRuntime;
    std::atomic<GameTitle> g_activeTitle{ GameTitle::None };
    std::atomic<uint64_t> g_activeTitleEpochMs{ 0 };
    std::atomic<RuntimeMode> g_runtimeMode{ RuntimeMode::Shell };

    struct CinematicControlSlot
    {
        // High dword = module generation, low byte = CinematicControlState.
        // A tiny sequence lock keeps state and heartbeat from different camera
        // samples from ever being combined by the compositor.
        std::atomic<uint64_t> sequence{0};
        std::atomic<uint64_t> stamp{0};
        std::atomic<uint64_t> heartbeatMs{0};
    };
    std::array<CinematicControlSlot, kTitleRuntimeSlotCount>
        g_cinematicControlSlots{};

    struct CutsceneTheaterProjectionSlot
    {
        std::atomic<uint64_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint32_t> authoredAspectBits{0};
        std::atomic<uint64_t> heartbeatMs{0};
    };
    std::array<CutsceneTheaterProjectionSlot, kTitleRuntimeSlotCount>
        g_cutsceneTheaterProjectionSlots{};

    GameTitle UniqueAvailableTitle(uint32_t availabilityMask)
    {
        const uint32_t known =
            availabilityMask & kTitleRuntimeAvailabilityMask;
        if (!known || (known & (known - 1u)) != 0)
            return known ? GameTitle::Unknown : GameTitle::None;
        for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
        {
            if (known & (uint32_t{1} << slot))
                return TitleRuntimeSlotTitle(slot);
        }
        return GameTitle::None;
    }

    RuntimeMode FallbackRuntimeMode(uint32_t availabilityMask)
    {
        const GameTitle available = UniqueAvailableTitle(availabilityMask);
        if (available == GameTitle::None)
            return RuntimeMode::Shell;
        const TitleDescriptor* descriptor = TitleRegistry_Find(available);
        return descriptor &&
                (descriptor->runtimeSupported ||
                 TitleRegistry_HookPlan(available) != TitleHookPlan::None)
            ? RuntimeMode::Loading
            : RuntimeMode::Unsupported;
    }

    bool ModuleSetMatches(
        const TitleRuntimeAvailabilitySnapshot& previous,
        const TitleRuntimeModuleSet& next)
    {
        if (!previous.stable ||
            previous.availabilityMask != next.availabilityMask)
        {
            return false;
        }
        for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
        {
            if (previous.moduleBases[slot] != next.moduleBases[slot])
                return false;
        }
        return true;
    }

    bool LoadResolveInput(
        uint64_t nowMs, const TitleRuntimeHeartbeatPolicy& policy,
        TitleRuntimeResolveInput& input)
    {
        for (uint32_t attempt = 0;
             attempt < kAdapterSnapshotReadAttempts; ++attempt)
        {
            const TitleRuntimeAvailabilitySnapshot availability =
                g_titleRuntime.LoadAvailability();
            if (!availability.stable)
                continue;

            TitleRuntimeResolveInput next{};
            next.availabilityMask = availability.availabilityMask;
            next.availabilitySetEpochMs =
                availability.availabilitySetEpochMs;
            next.nowMs = nowMs;

            bool candidatesStable = true;
            for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
            {
                const GameTitle title = TitleRuntimeSlotTitle(slot);
                if (!g_titleRuntime.LoadCandidate(
                        title, policy.freshForMs[slot], next.titles[slot]))
                {
                    if (g_titleRuntime.Generation(title) != 0)
                    {
                        candidatesStable = false;
                        break;
                    }
                    next.titles[slot].title = title;
                    next.titles[slot].heartbeatFreshForMs =
                        policy.freshForMs[slot];
                }
            }
            if (!candidatesStable)
                continue;

            const TitleRuntimeAvailabilitySnapshot after =
                g_titleRuntime.LoadAvailability();
            if (!after.stable ||
                after.revision != availability.revision ||
                after.availabilityMask != availability.availabilityMask ||
                after.availabilitySetEpochMs !=
                    availability.availabilitySetEpochMs ||
                after.moduleBases != availability.moduleBases)
            {
                continue;
            }

            input = next;
            return true;
        }
        return false;
    }

    TitleRuntimeSnapshot PendingSnapshotFromCandidate(
        const TitleRuntimeCandidate& candidate)
    {
        TitleRuntimeSnapshot snapshot{};
        snapshot.owner = candidate.title;
        snapshot.generation = candidate.generation;
        snapshot.installed = candidate.installed;
        // Pending is teardown retention only. It must never manufacture an
        // armed owner, a gameplay mode, a heartbeat, or any capability before
        // the first post-transition camera publication.
        snapshot.armed = false;
        snapshot.teardownRequested = candidate.teardownRequested;
        snapshot.mode = RuntimeMode::Loading;
        snapshot.heartbeatMs = 0;
        snapshot.enabledCapabilities = TitleCapability_None;
        return snapshot;
    }
}

const TitleDescriptor* TitleAdapter_GetActive()
{
    return TitleRegistry_Find(g_activeTitle.load(std::memory_order_acquire));
}

GameTitle TitleAdapter_GetActiveTitle()
{
    return g_activeTitle.load(std::memory_order_acquire);
}

uint64_t TitleAdapter_GetActiveTitleEpochMs()
{
    return g_activeTitleEpochMs.load(std::memory_order_acquire);
}

RuntimeMode TitleAdapter_GetRuntimeMode()
{
    return g_runtimeMode.load(std::memory_order_acquire);
}

void TitleAdapter_SetRuntimeMode(RuntimeMode mode)
{
    const RuntimeMode previous =
        g_runtimeMode.exchange(mode, std::memory_order_acq_rel);
    if (previous == mode)
        return;
    LOG("Runtime mode: %s -> %s",
        RuntimeModeName(previous), RuntimeModeName(mode));
    // Preserve the exact teardown edge for targeted co-op diagnostic rebuilds.
    // Production builds compile the sampler and dump out, so this call is a
    // no-op. Ordinary menu traffic is still excluded from the dormant path.
    if (mode == RuntimeMode::Loading && CoopProbeIsInLevelMode(previous))
        CoopProbe_DumpRunUp(RuntimeModeName(previous));
}

uint32_t TitleAdapter_GetGeneration(GameTitle title)
{
    return g_titleRuntime.Generation(title);
}

TitleRuntimeAvailabilitySnapshot TitleAdapter_GetAvailability()
{
    return g_titleRuntime.LoadAvailability();
}

bool TitleAdapter_GetCandidate(
    GameTitle title, uint64_t heartbeatFreshForMs,
    TitleRuntimeCandidate& candidate)
{
    return g_titleRuntime.LoadCandidate(
        title, heartbeatFreshForMs, candidate);
}

bool TitleAdapter_PublishLifecycle(
    GameTitle title, uint32_t generation,
    const TitleRuntimeLifecycle& lifecycle)
{
    return g_titleRuntime.PublishLifecycle(title, generation, lifecycle);
}

bool TitleAdapter_PublishMode(
    GameTitle title, uint32_t generation, RuntimeMode mode)
{
    if (!g_titleRuntime.PublishMode(title, generation, mode))
        return false;
    TitleAdapter_SetRuntimeMode(mode);
    return true;
}

bool TitleAdapter_PublishHeartbeat(
    GameTitle title, uint32_t generation, uint64_t heartbeatMs)
{
    return g_titleRuntime.PublishHeartbeat(title, generation, heartbeatMs);
}

bool TitleAdapter_ClearHeartbeat(GameTitle title, uint32_t generation)
{
    return g_titleRuntime.ClearHeartbeat(title, generation);
}

bool TitleAdapter_PublishCinematicControl(
    GameTitle title, uint32_t generation,
    CinematicControlState state, uint64_t heartbeatMs)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot >= kTitleRuntimeSlotCount || !generation || !heartbeatMs ||
        state > CinematicControlState::AuthoredLocked ||
        g_titleRuntime.Generation(title) != generation)
    {
        return false;
    }
    CinematicControlSlot& publication = g_cinematicControlSlots[slot];
    uint64_t sequence = publication.sequence.load(std::memory_order_acquire);
    if ((sequence & 1u) || !publication.sequence.compare_exchange_strong(
            sequence, sequence + 1, std::memory_order_acq_rel))
    {
        return false;
    }
    publication.heartbeatMs.store(heartbeatMs, std::memory_order_relaxed);
    const uint64_t stamp = (static_cast<uint64_t>(generation) << 32) |
        static_cast<uint8_t>(state);
    publication.stamp.store(stamp, std::memory_order_relaxed);
    publication.sequence.store(sequence + 2, std::memory_order_release);
    return true;
}

void TitleAdapter_ClearCinematicControl(
    GameTitle title, uint32_t generation)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot >= kTitleRuntimeSlotCount)
        return;
    CinematicControlSlot& publication = g_cinematicControlSlots[slot];
    const uint64_t stamp = publication.stamp.load(std::memory_order_acquire);
    if (!generation || static_cast<uint32_t>(stamp >> 32) == generation)
    {
        uint64_t sequence = publication.sequence.load(std::memory_order_acquire);
        if (!(sequence & 1u) && publication.sequence.compare_exchange_strong(
                sequence, sequence + 1, std::memory_order_acq_rel))
        {
            publication.stamp.store(0, std::memory_order_relaxed);
            publication.heartbeatMs.store(0, std::memory_order_relaxed);
            publication.sequence.store(sequence + 2, std::memory_order_release);
        }
    }

    TitleAdapter_ClearCutsceneTheaterProjection(title, generation);
}

CinematicControlPublication TitleAdapter_GetCinematicControlPublication(
    GameTitle title)
{
    CinematicControlPublication result{};
    result.title = title;
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot >= kTitleRuntimeSlotCount)
        return result;
    const CinematicControlSlot& publication = g_cinematicControlSlots[slot];
    for (uint32_t attempt = 0; attempt < kAdapterSnapshotReadAttempts; ++attempt)
    {
        const uint64_t before = publication.sequence.load(
            std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint64_t stamp = publication.stamp.load(
            std::memory_order_relaxed);
        const uint64_t heartbeat = publication.heartbeatMs.load(
            std::memory_order_relaxed);
        const uint64_t after = publication.sequence.load(
            std::memory_order_acquire);
        if (before != after)
            continue;
        const uint8_t rawState = static_cast<uint8_t>(stamp & 0xFFu);
        if (rawState > static_cast<uint8_t>(
                CinematicControlState::AuthoredLocked))
        {
            return result;
        }
        result.generation = static_cast<uint32_t>(stamp >> 32);
        result.state = static_cast<CinematicControlState>(rawState);
        result.heartbeatMs = heartbeat;
        return result;
    }
    return result;
}

bool TitleAdapter_PublishCutsceneTheaterProjection(
    GameTitle title, uint32_t generation,
    float authoredAspect, uint64_t heartbeatMs)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot >= kTitleRuntimeSlotCount || !generation || !heartbeatMs ||
        !std::isfinite(authoredAspect) || authoredAspect < 0.25f ||
        authoredAspect > 4.0f ||
        g_titleRuntime.Generation(title) != generation)
    {
        return false;
    }
    CutsceneTheaterProjectionSlot& publication =
        g_cutsceneTheaterProjectionSlots[slot];
    uint64_t sequence = publication.sequence.load(std::memory_order_acquire);
    if ((sequence & 1u) || !publication.sequence.compare_exchange_strong(
            sequence, sequence + 1, std::memory_order_acq_rel))
    {
        return false;
    }
    uint32_t aspectBits = 0;
    static_assert(sizeof(aspectBits) == sizeof(authoredAspect));
    memcpy(&aspectBits, &authoredAspect, sizeof(aspectBits));
    publication.generation.store(generation, std::memory_order_relaxed);
    publication.authoredAspectBits.store(aspectBits, std::memory_order_relaxed);
    publication.heartbeatMs.store(heartbeatMs, std::memory_order_relaxed);
    publication.sequence.store(sequence + 2, std::memory_order_release);
    return true;
}

void TitleAdapter_ClearCutsceneTheaterProjection(
    GameTitle title, uint32_t generation)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot >= kTitleRuntimeSlotCount)
        return;
    CutsceneTheaterProjectionSlot& publication =
        g_cutsceneTheaterProjectionSlots[slot];
    uint64_t sequence = publication.sequence.load(std::memory_order_acquire);
    if ((sequence & 1u) || !publication.sequence.compare_exchange_strong(
            sequence, sequence + 1, std::memory_order_acq_rel))
    {
        return;
    }
    const uint32_t publishedGeneration = publication.generation.load(
        std::memory_order_relaxed);
    if (!generation || publishedGeneration == generation)
    {
        publication.generation.store(0, std::memory_order_relaxed);
        publication.authoredAspectBits.store(0, std::memory_order_relaxed);
        publication.heartbeatMs.store(0, std::memory_order_relaxed);
    }
    publication.sequence.store(sequence + 2, std::memory_order_release);
}

CutsceneTheaterProjectionPublication
TitleAdapter_GetCutsceneTheaterProjectionPublication(GameTitle title)
{
    CutsceneTheaterProjectionPublication result{};
    result.title = title;
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot >= kTitleRuntimeSlotCount)
        return result;
    const CutsceneTheaterProjectionSlot& publication =
        g_cutsceneTheaterProjectionSlots[slot];
    for (uint32_t attempt = 0; attempt < kAdapterSnapshotReadAttempts; ++attempt)
    {
        const uint64_t before = publication.sequence.load(
            std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint32_t generation = publication.generation.load(
            std::memory_order_relaxed);
        const uint32_t aspectBits = publication.authoredAspectBits.load(
            std::memory_order_relaxed);
        const uint64_t heartbeat = publication.heartbeatMs.load(
            std::memory_order_relaxed);
        const uint64_t after = publication.sequence.load(
            std::memory_order_acquire);
        if (before != after)
            continue;
        float authoredAspect = 0.0f;
        memcpy(&authoredAspect, &aspectBits, sizeof(authoredAspect));
        result.generation = generation;
        result.authoredAspect = authoredAspect;
        result.heartbeatMs = heartbeat;
        return result;
    }
    return result;
}

TitleAdapterRuntimeSnapshot TitleAdapter_GetRuntimeSnapshot(
    uint64_t nowMs, const TitleRuntimeHeartbeatPolicy& policy,
    GameTitle retainedOwner)
{
    TitleAdapterRuntimeSnapshot result{};
    TitleRuntimeResolveInput input{};
    if (!LoadResolveInput(nowMs, policy, input))
    {
        result.runtime.mode = RuntimeMode::Unsupported;
        return result;
    }

    const uint32_t availableTitles =
        input.availabilityMask & kTitleRuntimeAvailabilityMask;
    if (availableTitles &&
        (availableTitles & (availableTitles - 1u)) != 0)
    {
        // Resident-module ambiguity has a deliberately tighter ownership
        // window than each title's normal loading/teardown heartbeat policy.
        // A zero policy remains disabled; all enabled title candidates must
        // keep publishing inside the accepted 100 ms ambiguity limit.
        for (TitleRuntimeCandidate& candidate : input.titles)
        {
            if (candidate.heartbeatFreshForMs >
                kAmbiguousOwnershipPendingMs)
            {
                candidate.heartbeatFreshForMs =
                    kAmbiguousOwnershipPendingMs;
            }
        }
    }

    result.runtime = ResolveTitleRuntime(input);
    if (result.runtime.owner == GameTitle::None &&
        TitleRuntimeOwnershipMayBePending(
            input, retainedOwner, kAmbiguousOwnershipPendingMs))
    {
        const size_t slot = TitleRuntimeSlotIndex(retainedOwner);
        if (slot < kTitleRuntimeSlotCount)
        {
            result.runtime = PendingSnapshotFromCandidate(input.titles[slot]);
            result.ownershipPending = true;
        }
    }

    if (result.runtime.owner == GameTitle::None)
        result.runtime.mode = FallbackRuntimeMode(input.availabilityMask);
    return result;
}

const TitleDescriptor* TitleAdapter_PollLoaded(uint64_t observedAtMs)
{
    size_t count = 0;
    const TitleDescriptor* titles = TitleRegistry_All(count);
    TitleRuntimeModuleSet modules{};
    const TitleDescriptor* detected = nullptr;
    size_t detectedCount = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const HMODULE module = GetModuleHandleW(titles[i].moduleName);
        if (!module)
            continue;
        const size_t slot = TitleRuntimeSlotIndex(titles[i].title);
        if (slot >= kTitleRuntimeSlotCount)
            continue;
        modules.availabilityMask |= uint32_t{1} << slot;
        modules.moduleBases[slot] = reinterpret_cast<uintptr_t>(module);
        if (!detected)
            detected = &titles[i];
        ++detectedCount;
    }

    const TitleRuntimeAvailabilitySnapshot previousAvailability =
        g_titleRuntime.LoadAvailability();
    const bool moduleSetChanged =
        !ModuleSetMatches(previousAvailability, modules);
    if (!g_titleRuntime.PublishModuleSet(modules, observedAtMs))
    {
        LOG("Title adapter: module availability publication failed; "
            "leaving runtime ownership unchanged");
        return nullptr;
    }

    const bool ambiguous = detectedCount > 1;
    const GameTitle next = ambiguous ? GameTitle::Unknown :
        (detected ? detected->title : GameTitle::None);
    const GameTitle previous =
        g_activeTitle.load(std::memory_order_acquire);
    if (!moduleSetChanged && previous == next)
        return ambiguous ? nullptr : detected;

    // Publish the exact module-set observation before the raw availability
    // title. Ownership resolution separately requires a title heartbeat newer
    // than this transition.
    g_activeTitleEpochMs.store(observedAtMs, std::memory_order_release);
    g_activeTitle.store(next, std::memory_order_release);

    if (ambiguous)
    {
        char names[256];
        names[0] = '\0';
        for (size_t i = 0; i < count; ++i)
        {
            const size_t slot = TitleRuntimeSlotIndex(titles[i].title);
            if (slot >= kTitleRuntimeSlotCount ||
                (modules.availabilityMask & (uint32_t{1} << slot)) == 0)
            {
                continue;
            }
            const size_t used = strlen(names);
            _snprintf_s(names + used, sizeof(names) - used, _TRUNCATE,
                        "%s%ls", used ? "," : "", titles[i].moduleName);
        }
        LOG("Title adapter: ambiguous MCC state (%zu game modules loaded: %s); "
            "runtime ownership is awaiting one unique post-transition camera "
            "heartbeat",
            detectedCount, names);
        return nullptr;
    }
    if (!detected)
    {
        LOG("Title adapter: no MCC game module is loaded");
        TitleAdapter_SetRuntimeMode(RuntimeMode::Shell);
    }
    else if (!detected->runtimeSupported)
    {
        if (detected->title == GameTitle::HaloReach &&
            ReachAdapter_GetStage() ==
                ReachAdapterStage::ControllerInputOnly)
        {
            LOG("Title adapter: detected %s (%ls); shared virtual-controller "
                "transport is enabled; Reach camera, render, aim/movement "
                "transforms, HUD, haptics, lifecycle, and runtime hooks remain "
                "disabled",
                detected->displayName, detected->moduleName);
        }
        else if (detected->title == GameTitle::Halo4 &&
            Halo4Adapter_GetStage() ==
                Halo4AdapterStage::ControllerInputAndColdObservation)
        {
            // Exact-stage check on purpose: this branch's log text promises
            // that no runtime hook exists, which is only true of this stage.
            // A future hook-carrying stage must NOT inherit the banner - it
            // falls through to the generic message until given its own
            // branch, exactly like Reach's stage handling above.
            // The pinned identity is reported from compile-time constants
            // here. Nothing in the loaded Halo 4 image is read at detection
            // time: a title whose level is still loading must not be touched
            // at all, which is what caused the load bounce. C-H4-2's cold
            // observation verifies the loaded image on the title worker,
            // behind the level-load gate.
            const Halo4EvidenceIdentity& identity =
                Halo4Adapter_GetEvidenceIdentity();
            LOG("Title adapter: detected %s (%ls); shared virtual-controller "
                "transport is enabled; cold observation (level-load gate + "
                "one-shot loaded-image preflight) is armed; Halo 4 camera, "
                "render, aim/movement transforms, HUD, haptics, lifecycle, "
                "and runtime hooks remain disabled",
                detected->displayName, detected->moduleName);
            LOG("Halo 4 pinned identity (expected; the cold observation "
                "verifies it against the loaded image once the level runs): "
                "PE timestamp 0x%08X, SizeOfImage 0x%08X, "
                "H4EK build %s, SHA-256 Steam %s, Store %s",
                identity.peTimestamp, identity.sizeOfImage,
                identity.h4ekBuild, identity.moduleSha256Steam,
                identity.moduleSha256Store);
        }
        else if (TitleRegistry_HookPlan(detected->title) ==
            TitleHookPlan::OdstExperimentalCameraCore)
        {
            LOG("Title adapter: detected %s (%ls); public adapter remains "
                "unsupported, private camera-only bring-up is build-enabled",
                detected->displayName, detected->moduleName);
        }
        else
        {
            LOG("Title adapter: detected %s (%ls); adapter not implemented, "
                "leaving stock game untouched",
                detected->displayName, detected->moduleName);
        }
        TitleAdapter_SetRuntimeMode(
            TitleRegistry_HookPlan(detected->title) != TitleHookPlan::None
                ? RuntimeMode::Loading
                : RuntimeMode::Unsupported);
    }
    else
    {
        LOG("Title adapter: detected supported title %s (%ls)",
            detected->displayName, detected->moduleName);
        TitleAdapter_SetRuntimeMode(RuntimeMode::Loading);
    }
    return detected;
}
