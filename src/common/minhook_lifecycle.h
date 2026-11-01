#pragma once
#include <MinHook.h>

#ifdef __cplusplus
extern "C" {
#endif
// Worker only. Disables an exact live MinHook patch normally, or marks an
// absent mapping / already restored entry disabled WITHOUT writing it.
// This never frees a trampoline. Callers must prove detour quiescence before
// MH_RemoveHook, including when this returns MH_ERROR_DISABLED.
MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID target);

// One of this mod's ENABLED MinHook patches: the bytes it overwrote (`address`, `size` of them) and what stood
// there before (`original`). A normal hook rewrites five bytes at the target (E9 rel32); a hot-patch hook rewrites
// the five bytes above it and two at it.
typedef struct MCCVR_HOOK_PATCH
{
    const unsigned char* address;
    unsigned char size;
    unsigned char original[8];
} MCCVR_HOOK_PATCH;
// Any thread. Copies up to `capacity` enabled patches that intersect [base, base + size) and returns how many
// there are in all (a return above `capacity` means the copy is incomplete). This is how a byte-pattern proof
// reads the GAME's code where this mod has hooked it: a signature is a statement about the image as it shipped,
// and after MH_EnableHook the head of a hooked function reads our jump instead (sigscan.h, sig::Find).
UINT WINAPI MCCVR_CopyEnabledHookPatches(LPCVOID base, SIZE_T size, MCCVR_HOOK_PATCH* out, UINT capacity);

// ENABLES DEFERRED TO ONE FREEZE. MH_EnableHook suspends every other thread of the process, one at a time, reads
// each one's context and resumes them: one whole-process freeze per hook. A title install that enables its optional
// hooks one by one pays that for every hook, at the moment a level starts, when the game's threads are busiest and
// slowest to suspend, and the game stalls for the length of each freeze.
//
// While a deferral is open on the calling thread, MCCVR_EnableHook queues the hook instead (MH_QueueEnableHook's
// state) and returns what MH_EnableHook returns for a created, disabled hook: MH_OK. The next MH_ApplyQueued
// enables it in the same single freeze as everything else queued. Refused as MH_EnableHook refuses:
// MH_ERROR_NOT_CREATED, MH_ERROR_ENABLED. Enabled at once, exactly as MH_EnableHook, when no deferral is open on
// this thread (every other thread and caller), the target is MH_ALL_HOOKS, the deferral is full, or the hook's
// patch bytes meet another hook's that is enabled or queued (two overlapping writes then keep their old order).
//
// A hook queued here is not live until MH_ApplyQueued. Its trampoline is valid at once (MH_CreateHook made it),
// the image still holds the game's own bytes there, and MCCVR_DisableHookForRetirement and MH_RemoveHook both take
// it off the queue.
//
// Begin returns FALSE (and changes nothing) when a deferral is already open on this thread. End closes it:
// `applyFollows` TRUE = the caller calls MH_ApplyQueued next; FALSE = every hook this deferral queued that is still
// queued is taken off the queue and enabled now, one at a time (the old path). Returns how many hooks were
// deferred, and in `immediate` how many inside the deferral were enabled at once for one of the reasons above.
// THIS PROCESS'S THREADS, WITHOUT A SYSTEM-WIDE SNAPSHOT. CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD) copies
// every thread of every process on the machine (some 12,000 here) to find this process's hundred or two; it was
// measured at 115-237 ms, and every MinHook freeze took one. This walks this process's threads alone (ntdll's
// NtGetNextThread) and returns their ids in a buffer for MCCVR_FreeThreadIds. FALSE (and nothing to free) when
// that route is unavailable, switched off, or did not reach the end of the list: the caller then takes the
// snapshot, as before. Like a snapshot it is a list at one moment: a thread that starts after it is not in it, one
// that ends after it is in it and fails to open, and callers treat both exactly as they treated the snapshot's.
// Allocates; call it before any thread is suspended.
BOOL WINAPI MCCVR_ListOwnThreadIds(DWORD** ids, UINT* count);
// The same walk into the caller's buffer, allocating nothing, so it may run while other threads are suspended.
// COMPLETE: every thread fitted (`count` of them); OVERFLOW: there are more than `capacity`; FAILED: the walk could
// not be made (the caller takes its old route).
#define MCCVR_OWN_THREADS_FAILED 0u
#define MCCVR_OWN_THREADS_COMPLETE 1u
#define MCCVR_OWN_THREADS_OVERFLOW 2u
UINT WINAPI MCCVR_ListOwnThreadIdsInto(DWORD* ids, UINT capacity, UINT* count);
VOID WINAPI MCCVR_FreeThreadIds(DWORD* ids);
// FALSE: every list fails, so every freeze takes the system-wide snapshot again (for A/B and tests).
VOID WINAPI MCCVR_SetOwnThreadListEnabled(BOOL enabled);

// What MinHook's freezes cost since the last take: how many there were, how many listed this process's threads
// directly and how many fell back to the system-wide snapshot, the threads listed, the time the lists took, and
// the time spent suspending threads and reading where each stood (summed, and the slowest single call). Every
// MinHook freeze counts (MH_EnableHook, MH_DisableHook, MH_ApplyQueued, MCCVR_DisableHookForRetirement).
typedef struct MCCVR_FREEZE_STATS
{
    UINT freezes;
    UINT ownLists;
    UINT systemSnapshots;
    UINT threads;
    double listMs;
    double suspendMs;
    double slowestThreadMs;
} MCCVR_FREEZE_STATS;
VOID WINAPI MCCVR_TakeFreezeStats(MCCVR_FREEZE_STATS* out);

BOOL WINAPI MCCVR_BeginHookEnableDeferral(VOID);
MH_STATUS WINAPI MCCVR_EnableHook(LPVOID target);
UINT WINAPI MCCVR_EndHookEnableDeferral(BOOL applyFollows, UINT* immediate);
#ifdef __cplusplus
}
#endif
