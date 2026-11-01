#include "title_adapter.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>

#include "../common/coop_probe_logic.h"
#include "../common/host_unload_gate_logic.h"
#include "../common/title_build_identity.h"
#include "../common/log.h"
#include "d3d11_hook.h"
#include "halo2_adapter.h"
#include "halo4_adapter.h"
#include "reach_adapter.h"
#include "title_reentry_probe.h"
#include "seat_exit_witness.h"

namespace
{
    constexpr uint32_t kAdapterSnapshotReadAttempts = 8;
    constexpr uint64_t kAmbiguousOwnershipPendingMs = 100;

    TitleRuntimeState g_titleRuntime;
    std::atomic<GameTitle> g_activeTitle{ GameTitle::None };
    std::atomic<uint64_t> g_activeTitleEpochMs{ 0 };
    std::atomic<RuntimeMode> g_runtimeMode{ RuntimeMode::Shell };

    // UNLOADS THE POLL DID NOT SEE (TitleAdapter_NoteImageUnloading). The bases the last poll found, and how
    // often the loader has unmapped each since the process began.
    std::array<std::atomic<uintptr_t>, kTitleRuntimeSlotCount> g_polledBases{};
    std::array<std::atomic<uint32_t>, kTitleRuntimeSlotCount> g_imageUnloads{};
    std::array<uint32_t, kTitleRuntimeSlotCount> g_imageUnloadsPolled{};   // the polling thread's own

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
#if HALOMCCVR_DEV_DIAGNOSTICS
    // SEATEXIT witness: a vehicle/turret -> gameplay edge gets its own block unless a raw seat edge explained it.
    SeatWitness_NoteRuntimeMode(TitleAdapter_GetActiveTitle(), static_cast<uint8_t>(previous),
        static_cast<uint8_t>(mode));
#endif
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

void TitleAdapter_NoteImageUnloading(uintptr_t base) noexcept
{
    if (!base)
        return;
    for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
        if (g_polledBases[slot].load(std::memory_order_acquire) == base)
            g_imageUnloads[slot].fetch_add(1, std::memory_order_acq_rel);
}

// A loaded image's PE timestamp and size. Guarded: the DLL can be unloaded under the read.
static bool ReadTitleImageIdentity(HMODULE module, uint32_t& stamp, uint32_t& size) noexcept
{
    __try
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;
        stamp = nt->FileHeader.TimeDateStamp;
        size = nt->OptionalHeader.SizeOfImage;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

const TitleDescriptor* TitleAdapter_PollLoaded(uint64_t observedAtMs)
{
    size_t count = 0;
    const TitleDescriptor* titles = TitleRegistry_All(count);
    TitleRuntimeModuleSet modules{};
    const TitleDescriptor* detected = nullptr;
    // BUILD CHECK (title_build_identity.h): once per title and per build seen this session, whether its DLL is
    // the build this mod was made for. Worker thread; the header read is guarded (the DLL can unload under it).
    static uint32_t reportedStamp[kTitleRuntimeSlotCount]{}, reportedSize[kTitleRuntimeSlotCount]{};
    const auto reportBuild = [](const TitleDescriptor& title, HMODULE module, size_t slot) noexcept
    {
        uint32_t stamp = 0, size = 0;
        if (!ReadTitleImageIdentity(module, stamp, size))
            return;
        if (!stamp || (reportedStamp[slot] == stamp && reportedSize[slot] == size))
            return;
        reportedStamp[slot] = stamp;
        reportedSize[slot] = size;
        const title_build::Identity* const known = title_build::Known(title.title);
        switch (title_build::Compare(title.title, stamp, size))
        {
        case title_build::Match::Known:
            LOG("BUILD CHECK %s: %ls is the build this mod was made for (PE timestamp 0x%08X, image size 0x%X; MCC "
                "%s; the same on Steam and the Microsoft Store)",
                title.displayName, title.moduleName, stamp, size, title_build::kKnownMccBuild);
            break;
        case title_build::Match::Different:
            LOG("BUILD CHECK %s: %ls is NOT the build this mod was made for: PE timestamp 0x%08X, image size 0x%X "
                "(made for 0x%08X, 0x%X; MCC %s). MCC has been updated, or this is a build nobody has looked at. %s "
                "THE MOD NEEDS AN UPDATE FOR THIS BUILD; send this line with any report",
                title.displayName, title.moduleName, stamp, size, known ? known->peTimestamp : 0,
                known ? known->sizeOfImage : 0, title_build::kKnownMccBuild,
                title_build::CoreIsBuildGated(title.title)
                    ? "This game's VR core installs only on that build, so it will stay off here and the game "
                      "plays flat (look for StockFallback, FAIL or WITHHELD lines)."
                    : "This game's VR core looks its functions up by signature and may still install; features "
                      "that do not find theirs say StockFallback.");
            break;
        default:
            break;
        }
    };
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
        reportBuild(titles[i], module, slot);
        if (!detected)
            detected = &titles[i];
        ++detectedCount;
    }

    // A title DLL that the loader unmapped since the last poll and that is here again at the same base was
    // unloaded and loaded again in between: publish the set without it first, so it starts a new generation
    // exactly as it does when a poll catches it gone. (An unload that lands between this poll's module
    // lookups and the read below publishes the same gap; the next poll then sees what is really there.)
    TitleRuntimeModuleSet gap = modules;
    uint32_t reloadedMask = 0;
    for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
    {
        const uint32_t unloads = g_imageUnloads[slot].load(std::memory_order_acquire);
        const bool unloaded = unloads != g_imageUnloadsPolled[slot];
        g_imageUnloadsPolled[slot] = unloads;
        if (host_unload_gate::ReloadedUnseen(unloaded, modules.moduleBases[slot],
                                             g_polledBases[slot].load(std::memory_order_relaxed)))
        {
            reloadedMask |= uint32_t{1} << slot;
            gap.availabilityMask &= ~(uint32_t{1} << slot);
            gap.moduleBases[slot] = 0;
        }
        g_polledBases[slot].store(modules.moduleBases[slot], std::memory_order_release);
    }
    if (reloadedMask && g_titleRuntime.PublishModuleSet(gap, observedAtMs))
        LOG("Title adapter: game DLLs were unloaded and loaded again at the same base between two module polls "
            "(slot mask 0x%X); published the unload the poll did not see, so each starts a new generation in "
            "its new image", reloadedMask);

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

    const GameTitle previous =
        g_activeTitle.load(std::memory_order_acquire);
    bool ambiguous = detectedCount > 1;
    if (ambiguous)
    {
        const GameTitle reentry =
            TitleReentryProbe_Resolve(modules, observedAtMs, previous);
        if (reentry != GameTitle::None && reentry != GameTitle::Unknown)
        {
            detected = TitleRegistry_Find(reentry);
            ambiguous = detected == nullptr;
            if (!ambiguous)
                detectedCount = 1;
        }
    }
    const GameTitle next = ambiguous ? GameTitle::Unknown :
        (detected ? detected->title : GameTitle::None);
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
        else if (detected->title == GameTitle::Halo2 &&
            Halo2Adapter_GetStage() == Halo2AdapterStage::SameFrameStereoSixDof)
        {
            LOG("Title adapter: detected %s (%ls); C-H2-6 same-frame stereo "
                "+ 6DOF is build-enabled behind C-H2-1 identity/liveness "
                "proof. Both eyes must render from the current OpenXR serial "
                "inside one game frame; no temporal eye reuse is permitted",
                detected->displayName, detected->moduleName);
        }
        else if (detected->title == GameTitle::Halo2 &&
            Halo2Adapter_GetStage() ==
                Halo2AdapterStage::TemporalStereoPositionOnly)
        {
            LOG("Title adapter: detected %s (%ls); C-H2-2 temporal stereo is "
                "build-enabled behind C-H2-1 identity/liveness proof. It "
                "alternates position-only eyes across adjacent frames and "
                "keeps Halo 2 head pose/6DOF, controller input/aim, HUD, "
                "haptics, and generic engine writes stock",
                detected->displayName, detected->moduleName);
        }
        else if (detected->title == GameTitle::Halo2 &&
            Halo2Adapter_GetStage() ==
                Halo2AdapterStage::ColdObservationOnly)
        {
            // Detection itself reads no game bytes. The worker first validates
            // two bounded liveness anchors at their pinned RVAs, then waits for
            // a coherent active-map tick before taking a short module pin and
            // running the one-shot full-image observation.
            const Halo2EvidenceIdentity& identity =
                Halo2Adapter_GetEvidenceIdentity();
            LOG("Title adapter: detected %s (%ls); C-H2-1 read-only cold "
                "observation is armed. Shared controller input, stereo, 6DOF, "
                "camera/render/aim/HUD/haptics, Halo 2 engine hooks, and all "
                "Halo 2 engine writes remain disabled",
                detected->displayName, detected->moduleName);
            LOG("Halo 2 offline evidence identity (loaded PE fields and anchors "
                "will be verified after an active level tick; file hashes are "
                "pinned provenance, not runtime-hashed): PE timestamp 0x%08X, "
                "SizeOfImage 0x%08X, H2EK build %s, H2EK tag-test SHA-256 %s, "
                "retail SHA-256 Steam %s, Store %s",
                identity.peTimestamp, identity.sizeOfImage,
                identity.h2ekBuild, identity.h2ekTagTestSha256,
                identity.moduleSha256Steam, identity.moduleSha256Store);
        }
        else if (detected->title == GameTitle::Halo4 &&
            Halo4Adapter_GetStage() ==
                Halo4AdapterStage::ControllerInputAndStereoCamera)
        {
            LOG("Title adapter: detected %s (%ls); the generation-owned Halo 4 "
                "camera core is build-enabled behind its level-load and "
                "loaded-image proofs",
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
        else if (detected->title == GameTitle::HaloCE)
        {
            LOG("Title adapter: detected CE; Original and Anniversary camera paths "
                "will verify their native bindings before arming");
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
