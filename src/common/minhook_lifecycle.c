// Compile the pinned upstream implementation unchanged in this translation
// unit so the narrow retirement extension can use its lock and hook records.
#include <windows.h>
#include <tlhelp32.h>
#include "minhook_lifecycle.h"

// ---------------------------------------------------------------------------------------------------------------
// THIS PROCESS'S THREADS, LISTED WITHOUT A SYSTEM-WIDE SNAPSHOT (minhook_lifecycle.h, MCCVR_ListOwnThreadIds).
//
// ntdll's NtGetNextThread walks the threads of ONE process, handing out a handle to each in turn. Its prototype is
// stable since Windows Vista; it is resolved by name at run time, and when it is missing or answers anything but
// "a thread" or "no more threads", the caller is told the list failed and takes its old route.
typedef LONG(NTAPI* MccvrNtGetNextThread)(HANDLE process, HANDLE thread, ACCESS_MASK access, ULONG attributes,
                                           ULONG flags, PHANDLE next);
#define MCCVR_STATUS_NO_MORE_ENTRIES ((LONG)0x8000001AL)

static MccvrNtGetNextThread MccvrResolveNtGetNextThread(VOID)
{
    static MccvrNtGetNextThread s_resolved = NULL;
    static volatile LONG s_state = 0; // 0 not asked, 1 resolved, 2 absent
    if (s_state == 0)
    {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const FARPROC found = ntdll ? GetProcAddress(ntdll, "NtGetNextThread") : NULL;
        s_resolved = (MccvrNtGetNextThread)(void*)found;
        InterlockedExchange(&s_state, found ? 1 : 2);
    }
    return s_state == 1 ? s_resolved : NULL;
}

static volatile LONG g_mccvrOwnThreadListDisabled = 0;

VOID WINAPI MCCVR_SetOwnThreadListEnabled(BOOL enabled)
{
    InterlockedExchange(&g_mccvrOwnThreadListDisabled, enabled ? 0 : 1);
}

UINT WINAPI MCCVR_ListOwnThreadIdsInto(DWORD* ids, UINT capacity, UINT* count)
{
    const MccvrNtGetNextThread next = MccvrResolveNtGetNextThread();
    HANDLE current = NULL;
    UINT size = 0, result = MCCVR_OWN_THREADS_FAILED;
    if (count)
        *count = 0;
    if (!ids || !count || !next || g_mccvrOwnThreadListDisabled)
        return MCCVR_OWN_THREADS_FAILED;
    // No allocation in here: a caller may list while every other thread is suspended (one of them could hold a
    // heap lock). Only system calls: the walk, the thread's id, and closing each handle.
    for (;;)
    {
        HANDLE thread = NULL;
        DWORD id;
        const LONG status = next(GetCurrentProcess(), current, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &thread);
        if (current)
            CloseHandle(current);
        current = NULL;
        if (status == MCCVR_STATUS_NO_MORE_ENTRIES)
        {
            // An empty list cannot be right: the calling thread is in it.
            result = size ? MCCVR_OWN_THREADS_COMPLETE : MCCVR_OWN_THREADS_FAILED;
            break;
        }
        if (status != 0 || !thread)
            break;
        current = thread;
        id = GetThreadId(thread);
        if (!id)
            break;
        if (size == capacity)
        {
            result = MCCVR_OWN_THREADS_OVERFLOW;
            break;
        }
        ids[size++] = id;
    }
    if (current)
        CloseHandle(current);
    *count = result == MCCVR_OWN_THREADS_FAILED ? 0 : size;
    return result;
}

BOOL WINAPI MCCVR_ListOwnThreadIds(DWORD** ids, UINT* count)
{
    HANDLE heap = GetProcessHeap();
    UINT capacity = 256;
    if (!ids || !count)
        return FALSE;
    *ids = NULL;
    *count = 0;
    while (heap && capacity <= (1u << 20))
    {
        UINT listed = 0, result;
        DWORD* const items = (DWORD*)HeapAlloc(heap, 0, capacity * sizeof(DWORD));
        if (!items)
            return FALSE;
        result = MCCVR_ListOwnThreadIdsInto(items, capacity, &listed);
        if (result == MCCVR_OWN_THREADS_COMPLETE)
        {
            *ids = items;
            *count = listed;
            return TRUE;
        }
        HeapFree(heap, 0, items);
        if (result != MCCVR_OWN_THREADS_OVERFLOW)
            return FALSE;
        capacity *= 2;
    }
    return FALSE;
}

VOID WINAPI MCCVR_FreeThreadIds(DWORD* ids)
{
    if (ids)
        HeapFree(GetProcessHeap(), 0, ids);
}

// ---------------------------------------------------------------------------------------------------------------
// MINHOOK'S FREEZE, FED BY THAT LIST. Upstream's EnumerateThreads (hook.c, compiled below unchanged) takes
// CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0) - every thread of every process on the machine - and keeps the
// ones whose owner is this process. Inside this translation unit the calls it makes are routed here: a thread
// snapshot is answered with this process's own list, given back through the same Thread32First / Thread32Next
// walk (owner = this process, so upstream's filter keeps every one, and skips the calling thread as it always
// did). Freeze, its suspension, ProcessThreadIPs and Unfreeze are upstream's, unchanged. When the list cannot be
// made the real snapshot is taken, exactly as before. Every Freeze runs under MinHook's lock, so one list is open
// at a time; an open while one is (never expected) takes the real snapshot as well.
#define MCCVR_OWN_SNAPSHOT ((HANDLE)(LONG_PTR)-0x4D4343)
static DWORD* g_mccvrSnapIds = NULL;
static UINT g_mccvrSnapCount = 0;
static UINT g_mccvrSnapAt = 0;
static volatile LONG g_mccvrSnapOpen = 0;

// What the freezes cost, for the install's line (minhook_lifecycle.h, MCCVR_TakeFreezeStats). Written inside
// Freeze, under MinHook's lock; taken and reset under it too.
static MCCVR_FREEZE_STATS g_mccvrFreezeStats;

static double MccvrElapsedMs(LONGLONG from, LONGLONG to)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0 ? (double)(to - from) * 1000.0 / (double)frequency.QuadPart : 0.0;
}

static HANDLE WINAPI MccvrCreateThreadSnapshot(DWORD flags, DWORD processId)
{
    LARGE_INTEGER began, ended;
    HANDLE snapshot;
    QueryPerformanceCounter(&began);
    ++g_mccvrFreezeStats.freezes;
    if (flags == TH32CS_SNAPTHREAD && InterlockedCompareExchange(&g_mccvrSnapOpen, 1, 0) == 0)
    {
        if (MCCVR_ListOwnThreadIds(&g_mccvrSnapIds, &g_mccvrSnapCount))
        {
            g_mccvrSnapAt = 0;
            QueryPerformanceCounter(&ended);
            ++g_mccvrFreezeStats.ownLists;
            g_mccvrFreezeStats.threads += g_mccvrSnapCount;
            g_mccvrFreezeStats.listMs += MccvrElapsedMs(began.QuadPart, ended.QuadPart);
            return MCCVR_OWN_SNAPSHOT;
        }
        InterlockedExchange(&g_mccvrSnapOpen, 0);
    }
    snapshot = CreateToolhelp32Snapshot(flags, processId);
    QueryPerformanceCounter(&ended);
    ++g_mccvrFreezeStats.systemSnapshots;
    g_mccvrFreezeStats.listMs += MccvrElapsedMs(began.QuadPart, ended.QuadPart);
    return snapshot;
}

static BOOL MccvrOwnSnapshotEntry(LPTHREADENTRY32 entry)
{
    if (g_mccvrSnapAt >= g_mccvrSnapCount)
    {
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    if (!entry || entry->dwSize < sizeof(THREADENTRY32))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    ZeroMemory(entry, sizeof(THREADENTRY32));
    entry->dwSize = sizeof(THREADENTRY32);
    entry->th32ThreadID = g_mccvrSnapIds[g_mccvrSnapAt++];
    entry->th32OwnerProcessID = GetCurrentProcessId();
    return TRUE;
}

static BOOL WINAPI MccvrThread32First(HANDLE snapshot, LPTHREADENTRY32 entry)
{
    if (snapshot != MCCVR_OWN_SNAPSHOT)
        return Thread32First(snapshot, entry);
    g_mccvrSnapAt = 0;
    return MccvrOwnSnapshotEntry(entry);
}

static BOOL WINAPI MccvrThread32Next(HANDLE snapshot, LPTHREADENTRY32 entry)
{
    if (snapshot != MCCVR_OWN_SNAPSHOT)
        return Thread32Next(snapshot, entry);
    return MccvrOwnSnapshotEntry(entry);
}

static BOOL WINAPI MccvrCloseHandle(HANDLE handle)
{
    if (handle != MCCVR_OWN_SNAPSHOT)
        return CloseHandle(handle);
    MCCVR_FreeThreadIds(g_mccvrSnapIds);
    g_mccvrSnapIds = NULL;
    g_mccvrSnapCount = 0;
    g_mccvrSnapAt = 0;
    InterlockedExchange(&g_mccvrSnapOpen, 0);
    return TRUE;
}

// Each thread's suspension and the read of where it stands, timed: the other half of a freeze's cost.
static VOID MccvrCountThreadMs(LONGLONG from, LONGLONG to)
{
    const double ms = MccvrElapsedMs(from, to);
    g_mccvrFreezeStats.suspendMs += ms;
    if (ms > g_mccvrFreezeStats.slowestThreadMs)
        g_mccvrFreezeStats.slowestThreadMs = ms;
}

static DWORD WINAPI MccvrTimedSuspendThread(HANDLE thread)
{
    LARGE_INTEGER began, ended;
    DWORD result;
    QueryPerformanceCounter(&began);
    result = SuspendThread(thread);
    QueryPerformanceCounter(&ended);
    MccvrCountThreadMs(began.QuadPart, ended.QuadPart);
    return result;
}

static BOOL WINAPI MccvrTimedGetThreadContext(HANDLE thread, LPCONTEXT context)
{
    // The read waits until the thread has really stopped, so this is where a thread slow to suspend shows.
    LARGE_INTEGER began, ended;
    BOOL result;
    QueryPerformanceCounter(&began);
    result = GetThreadContext(thread, context);
    QueryPerformanceCounter(&ended);
    MccvrCountThreadMs(began.QuadPart, ended.QuadPart);
    return result;
}

#define CreateToolhelp32Snapshot MccvrCreateThreadSnapshot
#define Thread32First MccvrThread32First
#define Thread32Next MccvrThread32Next
#define CloseHandle MccvrCloseHandle
#define SuspendThread MccvrTimedSuspendThread
#define GetThreadContext MccvrTimedGetThreadContext
#include "hook.c"
#undef CreateToolhelp32Snapshot
#undef Thread32First
#undef Thread32Next
#undef CloseHandle
#undef SuspendThread
#undef GetThreadContext

VOID WINAPI MCCVR_TakeFreezeStats(MCCVR_FREEZE_STATS* out)
{
    EnterSpinLock();
    if (out)
        *out = g_mccvrFreezeStats;
    ZeroMemory(&g_mccvrFreezeStats, sizeof(g_mccvrFreezeStats));
    LeaveSpinLock();
}

MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID target)
{
    MH_STATUS status = MH_ERROR_NOT_INITIALIZED;
    EnterSpinLock();
    if (g_hHeap != NULL && target != MH_ALL_HOOKS)
    {
        UINT pos = FindHookEntry(target);
        status = MH_ERROR_NOT_CREATED;
        if (pos != INVALID_HOOK_POS)
        {
            PHOOK_ENTRY hook = &g_hooks.pItems[pos];
            LPBYTE patch = (LPBYTE)target;
            SIZE_T size = sizeof(JMP_REL);
            MEMORY_BASIC_INFORMATION memory;
            if (hook->patchAbove)
            {
                patch -= sizeof(JMP_REL);
                size += sizeof(JMP_REL_SHORT);
            }
            status = MH_ERROR_MEMORY_PROTECT;
            // Do not make an unreadable mapping writable just to restore
            // bytes from a previous module instance.
            if (VirtualQuery(patch, &memory, sizeof(memory)) == sizeof(memory))
            {
                BOOL absent = memory.State != MEM_COMMIT;
                BOOL restored = FALSE;
                BOOL owned = FALSE;
                if (!absent && !(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                    (SIZE_T)(patch - (LPBYTE)memory.BaseAddress) + size <= memory.RegionSize)
                {
                    __try
                    {
                        JMP_REL jump;
                        jump.opcode = 0xE9;
                        jump.operand = (UINT32)((LPBYTE)hook->pDetour -
                            (patch + sizeof(JMP_REL)));
                        restored = memcmp(patch, hook->backup, size) == 0;
                        owned = memcmp(patch, &jump, sizeof(jump)) == 0 &&
                            (!hook->patchAbove ||
                             (((LPBYTE)target)[0] == 0xEB &&
                              ((LPBYTE)target)[1] == (UINT8)-7));
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) { }
                }
                if (absent || restored)
                {
                    hook->isEnabled = FALSE;
                    hook->queueEnable = FALSE;
                    status = MH_ERROR_DISABLED;
                }
                else if (owned && hook->isEnabled)
                {
                    FROZEN_THREADS threads;
                    status = Freeze(&threads, pos, ACTION_DISABLE);
                    if (status == MH_OK)
                    {
                        // The loader may remove a mapping after VirtualQuery.
                        __try { status = EnableHookLL(pos, FALSE); }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        { status = MH_ERROR_MEMORY_PROTECT; }
                        Unfreeze(&threads);
                    }
                }
                // Foreign bytes are never overwritten, even if upstream's
                // bookkeeping says this entry is already disabled.
            }
        }
    }
    LeaveSpinLock();
    return status;
}

UINT WINAPI MCCVR_CopyEnabledHookPatches(LPCVOID base, SIZE_T size, MCCVR_HOOK_PATCH* out, UINT capacity)
{
    UINT count = 0;
    const ULONG_PTR lo = (ULONG_PTR)base;
    const ULONG_PTR hi = lo + size;
    if (!size || hi < lo)
        return 0;
    EnterSpinLock();
    if (g_hHeap != NULL)
    {
        UINT i;
        for (i = 0; i < g_hooks.size; ++i)
        {
            const PHOOK_ENTRY hook = &g_hooks.pItems[i];
            ULONG_PTR patch = (ULONG_PTR)hook->pTarget;
            SIZE_T length = sizeof(JMP_REL);
            if (!hook->isEnabled)
                continue;
            if (hook->patchAbove)
            {
                patch -= sizeof(JMP_REL);
                length += sizeof(JMP_REL_SHORT);
            }
            if (patch >= hi || patch + length <= lo)
                continue;
            if (out && count < capacity)
            {
                out[count].address = (const unsigned char*)patch;
                out[count].size = (unsigned char)length;
                memcpy(out[count].original, hook->backup, sizeof(out[count].original));
            }
            ++count;
        }
    }
    LeaveSpinLock();
    return count;
}

// ENABLES DEFERRED TO ONE FREEZE (minhook_lifecycle.h). The calling thread's own state: only the thread that opened
// the deferral defers, every other thread's MCCVR_EnableHook is MH_EnableHook as before.
#define MCCVR_DEFERRAL_CAPACITY 64
static __declspec(thread) BOOL t_mccvrDeferring = FALSE;
static __declspec(thread) UINT t_mccvrDeferredCount = 0;
static __declspec(thread) UINT t_mccvrImmediateCount = 0;
static __declspec(thread) LPVOID t_mccvrDeferred[MCCVR_DEFERRAL_CAPACITY];

// The bytes a hook's jump rewrites: five at the target, or five above it and two at it.
static VOID MccvrPatchRange(const HOOK_ENTRY* hook, ULONG_PTR* lo, ULONG_PTR* hi)
{
    *lo = (ULONG_PTR)hook->pTarget;
    *hi = *lo + sizeof(JMP_REL);
    if (hook->patchAbove)
    {
        *lo -= sizeof(JMP_REL);
        *hi = (ULONG_PTR)hook->pTarget + sizeof(JMP_REL_SHORT);
    }
}

// Under the lock. Whether the hook at `pos` rewrites a byte that another hook already rewrites, or is queued to.
static BOOL MccvrPatchOverlapsAnother(UINT pos)
{
    ULONG_PTR lo, hi;
    UINT i;
    MccvrPatchRange(&g_hooks.pItems[pos], &lo, &hi);
    for (i = 0; i < g_hooks.size; ++i)
    {
        ULONG_PTR otherLo, otherHi;
        const HOOK_ENTRY* other = &g_hooks.pItems[i];
        if (i == pos || (!other->isEnabled && !other->queueEnable))
            continue;
        MccvrPatchRange(other, &otherLo, &otherHi);
        if (otherLo < hi && lo < otherHi)
            return TRUE;
    }
    return FALSE;
}

BOOL WINAPI MCCVR_BeginHookEnableDeferral(VOID)
{
    if (t_mccvrDeferring)
        return FALSE;
    t_mccvrDeferring = TRUE;
    t_mccvrDeferredCount = 0;
    t_mccvrImmediateCount = 0;
    return TRUE;
}

MH_STATUS WINAPI MCCVR_EnableHook(LPVOID target)
{
    if (t_mccvrDeferring && target != MH_ALL_HOOKS)
    {
        MH_STATUS status = MH_ERROR_NOT_INITIALIZED;
        BOOL answered = TRUE;
        EnterSpinLock();
        if (g_hHeap != NULL)
        {
            const UINT pos = FindHookEntry(target);
            if (pos == INVALID_HOOK_POS)
                status = MH_ERROR_NOT_CREATED;
            else if (g_hooks.pItems[pos].isEnabled)
                status = MH_ERROR_ENABLED;
            else if (t_mccvrDeferredCount < MCCVR_DEFERRAL_CAPACITY && !MccvrPatchOverlapsAnother(pos))
            {
                g_hooks.pItems[pos].queueEnable = TRUE;
                t_mccvrDeferred[t_mccvrDeferredCount++] = target;
                status = MH_OK;
            }
            else
                answered = FALSE;
        }
        LeaveSpinLock();
        if (answered)
            return status;
        // No room, or its bytes meet another hook's: enabled now, exactly as without a deferral.
        ++t_mccvrImmediateCount;
    }
    return MH_EnableHook(target);
}

UINT WINAPI MCCVR_EndHookEnableDeferral(BOOL applyFollows, UINT* immediate)
{
    UINT i;
    const UINT count = t_mccvrDeferredCount;
    if (immediate)
        *immediate = t_mccvrImmediateCount;
    // Closed first: the enables below are the ordinary ones.
    t_mccvrDeferring = FALSE;
    t_mccvrDeferredCount = 0;
    t_mccvrImmediateCount = 0;
    if (applyFollows)
        return count;
    for (i = 0; i < count; ++i)
    {
        const LPVOID target = t_mccvrDeferred[i];
        BOOL stillQueued = FALSE;
        EnterSpinLock();
        if (g_hHeap != NULL)
        {
            const UINT pos = FindHookEntry(target);
            if (pos != INVALID_HOOK_POS && !g_hooks.pItems[pos].isEnabled && g_hooks.pItems[pos].queueEnable)
            {
                // Taken off the queue so a later MH_ApplyQueued (another install's) does not enable it as a side
                // effect, then enabled on its own, as the feature asked.
                g_hooks.pItems[pos].queueEnable = FALSE;
                stillQueued = TRUE;
            }
        }
        LeaveSpinLock();
        if (stillQueued)
            (void)MH_EnableHook(target);
    }
    return count;
}
