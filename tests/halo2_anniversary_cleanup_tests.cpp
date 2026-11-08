#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include "../src/common/halo2_render_logic.h"
#include "../src/common/manual_vr_recovery_logic.h"

namespace
{
enum class CoreState : uint8_t { StockFallback, CleanupRequired, Installed };
CoreState g_coreState = CoreState::Installed;
std::atomic<bool> g_armed{}, g_teardown{}, g_installed{}, g_referenceValid{},
    g_recenterRequested{}, g_levelLive{}, g_remasteredLive{};
std::atomic<uint32_t> g_activeCallbacks{}, g_generation{}, g_vrFailureGeneration{};
std::atomic<uint32_t> g_zoomFaultGeneration{},g_zoomFaults{};
std::atomic<uintptr_t> g_originalScene{}, g_originalRebuild{}, g_originalHostUi{},
    g_fpPatchRecord{}, g_rebuildMatrices{}, g_cameraCommit{}, g_cameraRefreshRect{},
    g_moduleBase{}, g_observerResult{};
std::atomic<uint64_t> g_lastCompletedSerial{};
HMODULE g_moduleReference{};
void* g_sceneTarget{};
void* g_rebuildTarget{};
void* g_hostUiTarget{};
uint32_t g_rejectedGeneration{}, g_manualRecoveryGeneration{}, g_armedLoggedGeneration{};
GameTitle activeTitle = GameTitle::Halo2;
struct HookState { bool exists = true; bool enabled = true; };
std::array<HookState, 3> hooks{};
unsigned checks{}, failures{}, sleepCalls{}, disableCalls{}, removeCalls{},
    quiescenceCalls{}, installCalls{}, installedOverPending{}, removedBeforeQuiescence{};
int failedDisable = -1, failedRemove = -1;
bool quiescent = true, actualQuiescenceObserved = false;
void* Target(size_t index) { return reinterpret_cast<void*>(0x1000 + index * 0x100); }
uintptr_t Original(size_t index) { return 0x2000 + index * 0x100; }
size_t Index(void* target)
{
    for (size_t i = 0; i < hooks.size(); ++i) if (target == Target(i)) return i;
    return hooks.size();
}
void Check(bool pass, const char* message)
{
    ++checks;
    if (!pass) { ++failures; std::printf("FAIL: %s\n", message); }
}
void FixtureSleep(DWORD) { ++sleepCalls; }
void SaberSceneDetour() {}
void RebuildDetour() {}
void HostUiDetour() {}
MH_STATUS MCCVR_DisableHookForRetirement(void* target)
{
    ++disableCalls;
    const size_t index = Index(target);
    if (index == hooks.size() || !hooks[index].exists) return MH_ERROR_NOT_CREATED;
    if (static_cast<int>(index) == failedDisable) return MH_ERROR_MEMORY_PROTECT;
    if (!hooks[index].enabled) return MH_ERROR_DISABLED;
    hooks[index].enabled = false;
    return MH_OK;
}
// The original implementation calls upstream DisableHook directly. Keep the
// same already-disabled outcome available for its preserved regression run.
MH_STATUS FixtureDisableHook(void* target)
{ return MCCVR_DisableHookForRetirement(target); }
MH_STATUS FixtureRemoveHook(void* target)
{
    ++removeCalls;
    const size_t index = Index(target);
    if (index == hooks.size() || !hooks[index].exists) return MH_ERROR_NOT_CREATED;
    if (g_activeCallbacks.load() || !quiescent) ++removedBeforeQuiescence;
    if (static_cast<int>(index) == failedRemove) return MH_ERROR_MEMORY_PROTECT;
    hooks[index] = {false, false};
    return MH_OK;
}
bool WaitForNativeDetourQuiescence(const void* const* functions,
    const void* const* originals, size_t count, const std::atomic<uint32_t>& callbacks)
{
    ++quiescenceCalls;
    Check(count == 3 && functions[0] == reinterpret_cast<const void*>(&SaberSceneDetour) &&
        functions[1] == reinterpret_cast<const void*>(&RebuildDetour) &&
        functions[2] == reinterpret_cast<const void*>(&HostUiDetour),
        "production quiescence checks all three detour entries");
    const void* targets[] = {g_sceneTarget, g_rebuildTarget, g_hostUiTarget};
    for (size_t i = 0; i < 3; ++i)
    {
        Check(!targets[i] || !hooks[i].exists || !hooks[i].enabled,
              "entries are disabled before quiescence");
        Check(originals[i] == (targets[i] ? reinterpret_cast<void*>(Original(i)) : nullptr),
              "quiescence receives each remaining original trampoline");
    }
    actualQuiescenceObserved = quiescent && !callbacks.load();
    return actualQuiescenceObserved;
}
GameTitle TitleAdapter_GetActiveTitle() { return activeTitle; }
void Report() {}
void ReportPropagatedGameFaults(uintptr_t, size_t) {}

#define Sleep FixtureSleep
#define MH_DisableHook FixtureDisableHook
#define MH_RemoveHook FixtureRemoveHook
#define LOG(...) ((void)0)
#ifndef H2A_CLEANUP_INL
#define H2A_CLEANUP_INL "../src/dll/halo2_anniversary_cleanup.inl"
#endif
#include H2A_CLEANUP_INL

bool InstallCore(uintptr_t base, uint32_t generation, uintptr_t observer)
{
    ++installCalls;
    for (const auto& hook : hooks)
        if (hook.exists) { ++installedOverPending; return false; }
    for (auto& hook : hooks) hook = {true, true};
    g_sceneTarget = Target(0); g_rebuildTarget = Target(1); g_hostUiTarget = Target(2);
    g_originalScene = Original(0); g_originalRebuild = Original(1); g_originalHostUi = Original(2);
    g_coreState = CoreState::Installed; g_installed = true; g_teardown = false;
    g_moduleBase = base; g_generation = generation; g_observerResult = observer;
    return true;
}
#ifndef H2A_POLL_INL
#define H2A_POLL_INL "../src/dll/halo2_anniversary_poll.inl"
#endif
#include H2A_POLL_INL
#undef LOG
#undef MH_RemoveHook
#undef MH_DisableHook
#undef Sleep

void Reset()
{
    for (auto& hook : hooks) hook = {true, true};
    g_sceneTarget = Target(0); g_rebuildTarget = Target(1); g_hostUiTarget = Target(2);
    g_originalScene = Original(0); g_originalRebuild = Original(1); g_originalHostUi = Original(2);
    g_moduleBase = 0x10000000; g_generation = 7; g_observerResult = 0x3000;
    g_moduleReference = reinterpret_cast<HMODULE>(0x10000000);
    g_coreState = CoreState::Installed; g_installed = true; g_armed = true; g_teardown = false;
    g_activeCallbacks = 0; g_vrFailureGeneration = 0; g_manualRecoveryGeneration = 0;
    g_rejectedGeneration = 0; g_armedLoggedGeneration = 0;
    failedDisable = failedRemove = -1;
    sleepCalls = disableCalls = removeCalls = quiescenceCalls = installCalls = 0;
    installedOverPending = removedBeforeQuiescence = 0;
    quiescent = true; actualQuiescenceObserved = false;
    activeTitle = GameTitle::Halo2;
}
bool Poll(bool activeAndRange = true, bool running = true)
{
    return Halo2AnniversaryStereo_Poll(0x10000000, kHalo2RetailImageSize, 7,
        activeAndRange, running, true, true, 0x3000);
}
void CheckRetained(const char* description)
{
    Check(!g_armed.load() && g_teardown.load() && g_installed.load() &&
        g_generation.load() == 7 && g_moduleBase.load() == 0x10000000 &&
        g_originalScene.load() == Original(0) && g_originalRebuild.load() == Original(1) &&
        g_originalHostUi.load() == Original(2), description);
}
}

int main()
{
    Reset();
    g_activeCallbacks = 1;
    Check(!RemoveCore("callback timeout"), "busy callback blocks initial removal");
    Check(sleepCalls == 200 && removeCalls == 0, "timeout never removes live callback trampolines");
    CheckRetained("timeout retains exact hook originals/module/generation and closes arming");
    Check(g_coreState == CoreState::CleanupRequired, "timeout explicitly remains cleanup required");
    Check(!hooks[0].enabled && !hooks[1].enabled && !hooks[2].enabled,
          "initial timeout leaves all entries safely disabled");
    g_activeCallbacks = 0;
    Check(RemoveCore("callback timeout retry"),
          "second removal succeeds when upstream reports already disabled");
    Check(removeCalls == 3 && !g_installed.load() && !g_generation.load() &&
          !g_originalScene.load() && !g_sceneTarget && !g_rebuildTarget && !g_hostUiTarget,
          "quiescent retry removes records and clears originals exactly once");

    Reset(); quiescent = false;
    Check(!RemoveCore("detour prologue busy"), "zero callback counter alone cannot prove quiescence");
    Check(removeCalls == 0, "suspended detour/trampoline blocks every removal");
    CheckRetained("suspended detour retains original trampolines and module identity");
    quiescent = true;
    Check(RemoveCore("detour prologue retry"), "detour quiescence retry completes retirement");
    Check(!removedBeforeQuiescence, "no removal precedes callback and detour quiescence");

    for (int index = 0; index < 3; ++index)
    {
        Reset(); failedDisable = index;
        Check(!RemoveCore("disable failure"), "unsafe disable status blocks cleanup");
        Check(removeCalls == 0, "disable failure retains all records");
        CheckRetained("partial disable retains originals until complete quiescence");
        failedDisable = -1;
        Check(RemoveCore("disable retry"), "partial disable retries already disabled earlier entries");
    }

    for (int index = 0; index < 3; ++index)
    {
        Reset(); failedRemove = index;
        Check(!RemoveCore("remove failure"), "failed hook removal remains pending");
        Check(g_coreState == CoreState::CleanupRequired && g_installed.load(),
              "remove failure cannot publish stock-clean completion");
        const void* targets[] = {g_sceneTarget, g_rebuildTarget, g_hostUiTarget};
        for (int i = 0; i < 3; ++i)
            Check((targets[i] != nullptr) == (i >= index),
                  "partial removal preserves precisely the remaining target records");
        Check(!Poll() && installCalls == 0 && !g_armed.load(),
              "eligible title cannot reinstall or rearm over pending cleanup");
        failedRemove = -1;
        Check(RemoveCore("remove retry"), "remaining rebuild/UI-only records are retired on retry");
        Check(!installedOverPending && !removedBeforeQuiescence,
              "partial-removal retries preserve install and lifetime boundaries");
    }

    Reset(); g_activeCallbacks = 1;
    Check(!Poll(false), "title exit with live callback retains cleanup");
    Check(!Poll() && installCalls == 0 && !g_armed.load(),
          "immediate same-module reentry cannot bypass unfinished exit cleanup");
    g_activeCallbacks = 0;
    Check(Poll() && installCalls == 1 && !installedOverPending,
          "automatic reentry resumes installation only after retirement completes");

    Reset(); g_manualRecoveryGeneration = 7; g_activeCallbacks = 1;
    Check(!Poll() && g_manualRecoveryGeneration == 7 && !installCalls,
          "manual recovery remains pending through callback timeout");
    Check(!g_armed.load(), "pending manual cleanup cannot arm old hooks");
    g_activeCallbacks = 0;
    Check(Poll() && !g_manualRecoveryGeneration && installCalls == 1,
          "manual retry retires disabled hooks before a fresh install");

    Reset(); g_manualRecoveryGeneration = 7;
    Check(!Poll(false) && g_manualRecoveryGeneration == 7 && !installCalls && !disableCalls,
          "missing mapping proof keeps manual request pending without touching hooks");
    // The worker checks the pending request before declaring recovery settled,
    // even if existing camera arming has not yet been retired.
    Check(g_armed.load() && g_manualRecoveryGeneration != 0,
          "armed old camera can coexist with a pending, uncompleted request");

    Reset(); g_rejectedGeneration = 7;
    Check(!Poll(false) && g_rejectedGeneration == 7,
          "transient active-title proof loss does not erase failed-install latch");
    activeTitle = GameTitle::Halo3;
    Check(!Poll(false) && !g_rejectedGeneration,
          "actual title exit permits next-entry retry of failed generation");

    Reset(); hooks[1] = {false, false};
    Check(RemoveCore("already absent record"), "already absent hook record is safe to retire");
    std::printf("%s: %u checks, %u failures in production H2A cleanup/poll\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
