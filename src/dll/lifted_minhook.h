#pragma once

// Included after <MinHook.h> by the files that hook Halo 3: their MinHook calls go through lifted_engine.h, which is
// MinHook itself everywhere but in Project Reclaimer 0.10, where a target in halo3.dll's image (which never runs
// there) is hooked on that function's translated body instead. Other titles' targets are never in that image, so
// their calls are MinHook's unchanged. Their VirtualProtect is refused for that image's code alone, so a byte patch
// there takes its own failure path instead of reporting a change that never runs.

#include <MinHook.h>
#include "lifted_engine.h"
#include "../common/minhook_lifecycle.h"

#define MH_CreateHook(target, detour, original) lifted::CreateHook((target), (detour), (original))
#define MH_EnableHook(target) lifted::EnableHook(target)
#define MH_DisableHook(target) lifted::DisableHook(target)
#define MH_RemoveHook(target) lifted::RemoveHook(target)
#define MH_QueueEnableHook(target) lifted::QueueEnableHook(target)
#define MH_QueueDisableHook(target) lifted::QueueDisableHook(target)
#define MCCVR_DisableHookForRetirement(target) MCCVR_DisableHookForRetirement(lifted::PatchedAddress(target))
#define VirtualProtect(address, size, protect, previous) \
    lifted::GuardedVirtualProtect((address), (size), (protect), (previous))
