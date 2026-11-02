#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/common/minhook_lifecycle.h"
#include "../src/common/manual_vr_recovery.h"

static int failures = 0;
static void Check(bool condition, const char* description)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}
__declspec(noinline) static int Detour() { return 42; }
__declspec(noinline) static int OtherDetour() { return 43; }

// MCCVR_EnableHook inside and outside a deferral (minhook_lifecycle.h), on two synthetic functions in `page`.
static void DeferralTests(unsigned char* page)
{
    using Function = int(*)();
    const unsigned char seven[]{0xB8, 7, 0, 0, 0, 0xC3};  // mov eax,7; ret
    const unsigned char nine[]{0xB8, 9, 0, 0, 0, 0xC3};   // mov eax,9; ret
    unsigned char* const a = page;
    unsigned char* const b = page + 0x200;
    const auto fa = reinterpret_cast<Function>(a);
    const auto fb = reinterpret_cast<Function>(b);
    const auto reset = [&] {
        std::memset(page, 0x90, 4096);
        std::memcpy(a, seven, sizeof(seven));
        std::memcpy(b, nine, sizeof(nine));
    };
    const auto create = [&] {
        Function oa = nullptr, ob = nullptr;
        return MH_CreateHook(a, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&oa)) == MH_OK &&
               MH_CreateHook(b, reinterpret_cast<void*>(&OtherDetour), reinterpret_cast<void**>(&ob)) == MH_OK;
    };
    const auto remove = [&] {
        (void)MCCVR_DisableHookForRetirement(a);
        (void)MCCVR_DisableHookForRetirement(b);
        return MH_RemoveHook(a) == MH_OK && MH_RemoveHook(b) == MH_OK;
    };
    UINT immediate = 99;

    // Outside a deferral it is MH_EnableHook.
    reset();
    Check(create(), "deferral: create two hooks");
    Check(MCCVR_EnableHook(a) == MH_OK && fa() == 42, "no deferral: enabled at once");
    Check(MCCVR_EnableHook(a) == MH_ERROR_ENABLED, "no deferral: an enabled hook is refused as MinHook refuses it");
    Check(remove(), "deferral: remove");

    // Deferred, then the caller's MH_ApplyQueued takes them in one pass.
    reset();
    Check(create(), "deferral: create two hooks again");
    Check(MCCVR_BeginHookEnableDeferral() == TRUE, "deferral opens");
    Check(MCCVR_BeginHookEnableDeferral() == FALSE, "a second open on the same thread changes nothing");
    Check(MCCVR_EnableHook(a) == MH_OK && MCCVR_EnableHook(b) == MH_OK, "deferred enables report MH_OK");
    Check(fa() == 7 && fb() == 9 && a[0] == 0xB8 && b[0] == 0xB8, "deferred: the bytes are still the game's");
    Check(MCCVR_EnableHook(page + 0x400) == MH_ERROR_NOT_CREATED, "deferred: no hook there is refused");
    Check(MCCVR_EndHookEnableDeferral(TRUE, &immediate) == 2 && immediate == 0, "two deferred, none at once");
    Check(fa() == 7 && fb() == 9, "still not live before the apply");
    Check(MH_ApplyQueued() == MH_OK && fa() == 42 && fb() == 43, "one MH_ApplyQueued enables both");
    Check(MCCVR_EnableHook(a) == MH_ERROR_ENABLED, "closed: the ordinary path again");
    Check(remove() && fa() == 7 && fb() == 9, "retired: the game's own bytes");

    // Closed with nothing to apply: each is enabled on its own, and nothing stays queued.
    reset();
    Check(create(), "deferral: create for the no-apply close");
    Check(MCCVR_BeginHookEnableDeferral() == TRUE, "deferral opens again");
    Check(MCCVR_EnableHook(a) == MH_OK && MCCVR_EnableHook(b) == MH_OK, "deferred again");
    Check(MCCVR_EndHookEnableDeferral(FALSE, &immediate) == 2 && fa() == 42 && fb() == 43,
          "a close with no apply to follow enables each now");
    Check(MH_ApplyQueued() == MH_OK && fa() == 42 && fb() == 43, "and leaves nothing queued to flip");
    Check(remove(), "deferral: remove after the no-apply close");

    // A deferred hook a feature retires before the apply stays the game's.
    reset();
    Check(create(), "deferral: create for the retire-before-apply case");
    Check(MCCVR_BeginHookEnableDeferral() == TRUE, "deferral opens a third time");
    Check(MCCVR_EnableHook(a) == MH_OK && MCCVR_EnableHook(b) == MH_OK, "deferred a third time");
    Check(MCCVR_DisableHookForRetirement(a) == MH_ERROR_DISABLED, "a retired deferred hook was never live");
    Check(MCCVR_EndHookEnableDeferral(TRUE, &immediate) == 2, "the deferral counted both");
    Check(MH_ApplyQueued() == MH_OK && fa() == 7 && fb() == 43, "the apply skips the retired one");
    Check(remove(), "deferral: remove after the retire case");

    // Another thread's enable is never deferred by this thread's deferral.
    reset();
    Check(create(), "deferral: create for the other-thread case");
    Check(MCCVR_BeginHookEnableDeferral() == TRUE, "deferral opens on this thread");
    struct Arg { unsigned char* target; MH_STATUS status; } arg{b, MH_UNKNOWN};
    HANDLE other = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto* x = static_cast<Arg*>(p);
        x->status = MCCVR_EnableHook(x->target);
        return 0;
    }, &arg, 0, nullptr);
    Check(other && WaitForSingleObject(other, 5000) == WAIT_OBJECT_0, "the other thread ran");
    if (other)
        CloseHandle(other);
    Check(arg.status == MH_OK && fb() == 43, "another thread's enable is live at once");
    Check(MCCVR_EndHookEnableDeferral(TRUE, &immediate) == 0, "and was not this deferral's");
    Check(remove(), "deferral: remove after the other-thread case");
}

// THIS PROCESS'S THREADS, AND THE FREEZE THAT USES THEM (minhook_lifecycle.h, MCCVR_ListOwnThreadIds).
static std::vector<DWORD> SystemSnapshotOwnIds()
{
    std::vector<DWORD> ids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return ids;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry))
        do
        {
            if (entry.th32OwnerProcessID == GetCurrentProcessId())
                ids.push_back(entry.th32ThreadID);
            entry.dwSize = sizeof(entry);
        } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
    std::sort(ids.begin(), ids.end());
    return ids;
}

static double MsSince(const LARGE_INTEGER& began)
{
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(now.QuadPart - began.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

static void OwnThreadListTests(unsigned char* page)
{
    // Sixteen threads that wait, so the process's thread set holds still while it is listed both ways.
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<HANDLE> waiting;
    for (int i = 0; i < 16; ++i)
        waiting.push_back(CreateThread(nullptr, 0, [](LPVOID event) -> DWORD {
            WaitForSingleObject(static_cast<HANDLE>(event), INFINITE);
            return 0;
        }, release, 0, nullptr));
    Sleep(50);

    LARGE_INTEGER began{};
    QueryPerformanceCounter(&began);
    const std::vector<DWORD> before = SystemSnapshotOwnIds();
    const double systemMs = MsSince(began);
    DWORD* ids = nullptr;
    UINT count = 0;
    QueryPerformanceCounter(&began);
    const BOOL listed = MCCVR_ListOwnThreadIds(&ids, &count);
    const double ownMs = MsSince(began);
    const std::vector<DWORD> after = SystemSnapshotOwnIds();
    Check(listed == TRUE && ids && count >= 17, "this process's threads are listed without a system-wide snapshot");
    std::vector<DWORD> own(ids, ids + count);
    MCCVR_FreeThreadIds(ids);
    std::sort(own.begin(), own.end());
    Check(std::adjacent_find(own.begin(), own.end()) == own.end(), "each thread is listed once");
    bool everyStayingThread = true, nothingForeign = true;
    for (DWORD id : before)
        if (std::binary_search(after.begin(), after.end(), id) && !std::binary_search(own.begin(), own.end(), id))
            everyStayingThread = false;
    for (DWORD id : own)
        if (!std::binary_search(before.begin(), before.end(), id) && !std::binary_search(after.begin(), after.end(), id))
            nothingForeign = false;
    Check(everyStayingThread, "every thread the system snapshot saw, before and after, is in the list");
    Check(nothingForeign, "and the list holds no thread the snapshot never saw");
    Check(std::binary_search(own.begin(), own.end(), GetCurrentThreadId()), "the calling thread is in it");
    for (HANDLE thread : waiting)
        Check(thread && std::binary_search(own.begin(), own.end(), GetThreadId(thread)), "every waiting thread is in it");
    std::printf("threads of this process: %zu; system-wide snapshot %.2f ms, this process's own list %.3f ms\n",
                own.size(), systemMs, ownMs);

    // Into the caller's buffer, allocating nothing (a freeze lists again with every other thread suspended).
    DWORD few[4]{};
    UINT fewCount = 0;
    Check(MCCVR_ListOwnThreadIdsInto(few, 4, &fewCount) == MCCVR_OWN_THREADS_OVERFLOW && fewCount == 4,
          "a buffer too small for the threads says so");
    std::vector<DWORD> room(4096);
    UINT roomCount = 0;
    Check(MCCVR_ListOwnThreadIdsInto(room.data(), 4096, &roomCount) == MCCVR_OWN_THREADS_COMPLETE &&
          roomCount >= 17, "a buffer with room holds them all");

    MCCVR_SetOwnThreadListEnabled(FALSE);
    Check(MCCVR_ListOwnThreadIdsInto(room.data(), 4096, &roomCount) == MCCVR_OWN_THREADS_FAILED && roomCount == 0,
          "switched off, the walk into a buffer fails too");
    ids = nullptr;
    Check(MCCVR_ListOwnThreadIds(&ids, &count) == FALSE && !ids && count == 0, "switched off, the list fails");
    MCCVR_SetOwnThreadListEnabled(TRUE);

    // A thread spinning INSIDE the bytes a hook rewrites, through enables and disables. Every one of them must
    // suspend it and move its place into or out of the trampoline (upstream's ProcessThreadIPs); a thread the
    // freeze missed would run into the middle of the jump being written and fault. The function:
    //   0: 80 39 00   cmp byte ptr [rcx], 0
    //   3: 74 FB      je 0
    //   5: B8 07..    mov eax, 7
    //   A: C3         ret
    // MinHook copies the first five bytes (both instructions) into the trampoline, keeping the loop there.
    using Spin = int (*)(volatile char*);
    const unsigned char spin[]{0x80, 0x39, 0x00, 0x74, 0xFB, 0xB8, 7, 0, 0, 0, 0xC3};
    unsigned char* const target = page + 0x800;
    std::memset(page, 0x90, 4096);
    std::memcpy(target, spin, sizeof(spin));
    static volatile char stop = 0;
    static volatile LONG spinResult = 0;
    stop = 0;
    spinResult = 0;
    static Spin s_spin = nullptr;
    s_spin = reinterpret_cast<Spin>(target);
    HANDLE spinner = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        InterlockedExchange(&spinResult, s_spin(&stop));
        return 0;
    }, nullptr, 0, nullptr);
    Sleep(20);
    void* original = nullptr;
    MCCVR_FREEZE_STATS stats{};
    MCCVR_TakeFreezeStats(&stats);
    Check(MH_CreateHook(target, reinterpret_cast<void*>(&Detour), &original) == MH_OK, "spin: create");
    bool cycled = true;
    for (int i = 0; i < 25 && cycled; ++i)
        cycled = MH_EnableHook(target) == MH_OK && target[0] == 0xE9 && MH_DisableHook(target) == MH_OK &&
                 target[0] == 0x80;
    Check(cycled, "spin: 25 enables and disables while a thread runs inside the rewritten bytes");
    Check(MH_EnableHook(target) == MH_OK, "spin: enabled once more");
    MCCVR_TakeFreezeStats(&stats);
    Check(stats.freezes == 51 && stats.ownLists == 51 && stats.systemSnapshots == 0,
          "every freeze listed this process's threads, none took the system-wide snapshot");
    Check(stats.threads >= 51u * 17u, "and each list held every thread");
    std::printf("51 freezes: listing %.2f ms in all, suspending and reading %.2f ms in all, slowest thread %.3f ms\n",
                stats.listMs, stats.suspendMs, stats.slowestThreadMs);
    stop = 1;
    Check(spinner && WaitForSingleObject(spinner, 5000) == WAIT_OBJECT_0, "spin: the spinning thread left the loop");
    Check(spinResult == 7, "spin: and returned through the trampoline as the game's own code would");
    Check(reinterpret_cast<int (*)()>(target)() == 42, "spin: the hook is live");
    Check(MCCVR_DisableHookForRetirement(target) == MH_OK && MH_RemoveHook(target) == MH_OK, "spin: retired");
    if (spinner)
        CloseHandle(spinner);

    // Switched off, a freeze takes the system-wide snapshot exactly as upstream does.
    MCCVR_SetOwnThreadListEnabled(FALSE);
    MCCVR_TakeFreezeStats(&stats);
    std::memcpy(target, spin, sizeof(spin));
    Check(MH_CreateHook(target, reinterpret_cast<void*>(&Detour), &original) == MH_OK &&
          MH_EnableHook(target) == MH_OK, "fallback: enabled");
    MCCVR_TakeFreezeStats(&stats);
    Check(stats.freezes == 1 && stats.ownLists == 0 && stats.systemSnapshots == 1 && stats.threads == 0,
          "fallback: the freeze took the system-wide snapshot");
    std::printf("one freeze through the system-wide snapshot: listing %.2f ms\n", stats.listMs);
    Check(MCCVR_DisableHookForRetirement(target) == MH_OK && MH_RemoveHook(target) == MH_OK, "fallback: retired");
    MCCVR_SetOwnThreadListEnabled(TRUE);
    MCCVR_TakeFreezeStats(&stats);

    SetEvent(release);
    for (HANDLE thread : waiting)
        if (thread)
        {
            WaitForSingleObject(thread, 5000);
            CloseHandle(thread);
        }
    CloseHandle(release);
}

int main()
{
    Check(MH_Initialize() == MH_OK, "initialize local test hooks");
    // Entirely synthetic executable memory in THIS test process. No MCC
    // process, module, config, installation or game data is accessed.
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) return 1;
    const unsigned char native[]{0xB8, 7, 0, 0, 0, 0xC3}; // mov eax,7; ret
    using Function = int(*)();
    const auto function = reinterpret_cast<Function>(page);
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        std::memset(page, 0x90, 4096);
        std::memcpy(page, native, sizeof(native));
        Function original = nullptr;
        Check(MH_CreateHook(page, reinterpret_cast<void*>(&Detour),
            reinterpret_cast<void**>(&original)) == MH_OK, "create hook");
        Check(MH_EnableHook(page) == MH_OK && function() == 42, "live detour");
        if (scenario == 1) // MCC released the mapping before the worker polled
            Check(VirtualFree(page, 4096, MEM_DECOMMIT) != FALSE, "decommit target");
        if (scenario == 2) // Reload at the same address, original bytes restored
            std::memcpy(page, native, sizeof(native));
        if (scenario == 3) // A foreign replacement must never be overwritten
        {
            std::memset(page, 0xCC, sizeof(native));
            Check(MCCVR_DisableHookForRetirement(page) == MH_ERROR_MEMORY_PROTECT,
                "reject unknown replacement");
            Check(page[0] == 0xCC && page[5] == 0xCC, "foreign bytes unchanged");
            std::memcpy(page, native, sizeof(native));
        }
        if (scenario == 4) // Already disabled by a prior partial cleanup
            Check(MH_DisableHook(page) == MH_OK, "ordinary disable");
        if (scenario == 5) // Failed install before its queued hooks went live
        {
            Check(MH_DisableHook(page) == MH_OK, "prepare staged hook");
            Check(MH_QueueEnableHook(page) == MH_OK, "queue future activation");
        }
        const auto retired = MCCVR_DisableHookForRetirement(page);
        Check(retired == MH_OK || retired == MH_ERROR_DISABLED, "retire safely");
        if (scenario == 5)
            Check(MH_ApplyQueued() == MH_OK && function() == 7,
                "retirement cancels queued activation");
        if (scenario != 1)
        {
            Check(std::memcmp(page, native, sizeof(native)) == 0, "native bytes intact");
            Check(function() == 7 && original() == 7, "trampoline retained until removal");
        }
        // No other threads call these test functions, so they are quiescent.
        Check(MH_RemoveHook(page) == MH_OK, "retired record can be removed");
        Check(MCCVR_DisableHookForRetirement(page) == MH_ERROR_NOT_CREATED,
            "removed record is absent");
        if (scenario == 1)
            Check(VirtualAlloc(page, 4096, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == page,
                "reuse original address");
    }
    DeferralTests(page);
    OwnThreadListTests(page);
    VirtualFree(page, 0, MEM_RELEASE);
    Check(MH_Uninitialize() == MH_OK, "shutdown");
    wchar_t eventName[96]{};
    ManualVrRecoveryEventName(eventName, GetCurrentProcessId());
    HANDLE receiver = CreateEventW(nullptr, FALSE, FALSE, eventName);
    HANDLE sender = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    Check(receiver && sender, "launcher and DLL agree on recovery endpoint");
    if (receiver && sender)
    {
        Check(SetEvent(sender) && SetEvent(sender), "coalesce repeated recovery clicks");
        Check(WaitForSingleObject(receiver, 0) == WAIT_OBJECT_0,
            "worker receives recovery request");
        Check(WaitForSingleObject(receiver, 0) == WAIT_TIMEOUT,
            "request is consumed once");
    }
    if (sender) CloseHandle(sender);
    if (receiver) CloseHandle(receiver);
    return failures ? 1 : 0;
}
