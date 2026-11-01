#pragma once
// "INSTALLED" IS NOT "RUNNING", for Halo 3's own hooks as for the direct drives (drive_cascade.h: HookWatchBoard).
// The hooks' half: each watched detour marks its flag on entry, and each install records where it put its hook.
// The watches, what makes each call possible and their log lines are in halo3_hook_watch.inl.
//
// On the hot path a mark is one relaxed load, and one relaxed store on the first call after the cold worker last
// cleared the flag (it reads and clears every flag once a loop, about every 50 ms). Nothing else.
#include <atomic>
#include <cstddef>
#include <cstdint>

enum class Halo3Hook : uint8_t
{
    RenderView,
    ObserverEffect,
    ComposeBones,
    FpInterpolate,
    FpVisiblePalette,
    FpCameraRebuild,
    FpDriver,
    HudDrawWidget,
    HudAnchorBasis,
    HudPlace,
    HudCrosshairVisible,
    WorldCollision,
    ContactUpdate,
    SeatView,
    WeaponTriggerUpdate,
    HapticUpdate,
    SoundListener,
    PartDraw,
    Count
};
inline constexpr size_t kHalo3HookCount = static_cast<size_t>(Halo3Hook::Count);

// Set by the detours (any thread), read and cleared by the cold worker.
inline std::atomic<uint8_t> g_halo3HookCalled[kHalo3HookCount]{};

inline void Halo3HookCalled(Halo3Hook hook) noexcept
{
    auto& flag = g_halo3HookCalled[static_cast<size_t>(hook)];
    if (!flag.load(std::memory_order_relaxed))
        flag.store(1, std::memory_order_relaxed);
}

// Where the current install put each hook (null: it did not), the module it put them in, and which install of
// Halo 3's hooks this is. Install thread (the cold worker) only, written by the install and read by the watch.
inline const void* g_halo3HookTarget[kHalo3HookCount]{};
inline uintptr_t g_halo3HookWatchBase = 0;
inline uint32_t g_halo3HookWatchInstall = 0;

inline void Halo3HookWatchInstalled(Halo3Hook hook, uintptr_t target) noexcept
{
    g_halo3HookTarget[static_cast<size_t>(hook)] = reinterpret_cast<const void*>(target);
}
