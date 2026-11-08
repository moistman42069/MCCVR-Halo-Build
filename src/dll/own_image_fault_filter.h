#pragma once

// SEH filter for a mod-owned __except that wraps a call into game code.
//
//     const uintptr_t anchor = OwnImageFault_Anchor();
//     __try { gameFunction(...); }
//     __except (OwnImageFaultFilter(GetExceptionInformation(), anchor, stats))
//     { ...only the mod's own faults land here... }
//
// The handler runs only when the fault is provably HaloMCCVR's own (see
// seh_fault_containment_logic.h for why, and for the incident). A fault in the
// game, or in mod code the game called back into, returns
// EXCEPTION_CONTINUE_SEARCH: it reaches the game's / the OS's own handling,
// i.e. a crash with a dump instead of a silently half-unwound game.
//
// Runs during exception dispatch on the faulting thread: no logging, no
// allocation, no locks. It only counts and remembers the last address for a
// cold worker to report. Vectored handlers (native_fault_probe's first-chance
// observer) run before any frame-based filter, so first-chance evidence is
// recorded whatever this filter decides.

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../common/seh_fault_containment_logic.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

struct OwnImageFaultStats
{
    std::atomic<uint64_t> propagated{0};
    std::atomic<uintptr_t> lastFaultAddress{0};
    // The fault itself when it is foreign; otherwise the first foreign frame
    // between the fault and the handler (mod code called back by the game);
    // 0 when the walk could not finish through mod frames.
    std::atomic<uintptr_t> lastForeignAddress{0};
    std::atomic<uint32_t> lastCode{0};
};

// One exception is offered to every enclosing filter in turn; count it once.
// Cleared at each detour entry so a recurrence in a later frame counts again.
inline thread_local const EXCEPTION_RECORD* t_ownImageFaultLastRecord = nullptr;

inline void OwnImageFault_BeginScope() noexcept
{
    t_ownImageFaultLastRecord = nullptr;
}

// The handler function's own return-address slot. Every frame the handler's
// unwind discards lies strictly below it; unwinding the handler function
// itself lands above it.
#define OwnImageFault_Anchor() \
    (reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()))

inline FaultImageRange OwnImageFault_SelfRange() noexcept
{
    const auto* dos = &__ImageBase;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
    return {reinterpret_cast<uintptr_t>(dos),
            static_cast<size_t>(nt->OptionalHeader.SizeOfImage)};
}

constexpr size_t kOwnImageFaultMaxFrames = 64;

// Unwinds ONLY frames whose code is in our own image (our own unwind data, our
// own stack slots), so a corrupt game stack is never interpreted. Stops at the
// first foreign return address (recorded) or when the handler's frame is
// reached. Returns the number of return addresses written to `frames`.
inline size_t OwnImageFault_WalkToHandler(
    const CONTEXT& faultContext, uintptr_t anchor, const FaultImageRange& self,
    uintptr_t* frames, size_t capacity, bool& reachedHandler) noexcept
{
    reachedHandler = false;
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t stackLow = reinterpret_cast<uintptr_t>(tib->StackLimit);
    const uintptr_t stackHigh = reinterpret_cast<uintptr_t>(tib->StackBase);
    CONTEXT context = faultContext;
    size_t count = 0;
    while (count < capacity)
    {
        if (!FaultImageContains(self, static_cast<uintptr_t>(context.Rip)) ||
            context.Rsp < stackLow || context.Rsp + sizeof(DWORD64) > stackHigh)
            break;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION entry =
            RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (entry)
        {
            PVOID handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
                             &context, &handlerData, &establisher, nullptr);
        }
        else
        {
            // Leaf function: the return address is at RSP.
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += sizeof(DWORD64);
        }
        if (context.Rsp > anchor)
        {
            // That was the handler function's own frame; its caller is not
            // discarded by the handler and is not judged.
            reachedHandler = true;
            break;
        }
        frames[count++] = static_cast<uintptr_t>(context.Rip);
        if (!FaultImageContains(self, static_cast<uintptr_t>(context.Rip)))
            break;
    }
    return count;
}

inline int OwnImageFaultFilter(
    EXCEPTION_POINTERS* exception, uintptr_t anchor,
    OwnImageFaultStats& stats) noexcept
{
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD* record = exception->ExceptionRecord;
    const uintptr_t faultAddress =
        reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    const FaultImageRange self = OwnImageFault_SelfRange();
    uintptr_t frames[kOwnImageFaultMaxFrames];
    size_t frameCount = 0;
    bool reachedHandler = false;
    // A stack overflow leaves no stack to walk with; it is never contained.
    if (record->ExceptionCode != EXCEPTION_STACK_OVERFLOW &&
        FaultImageContains(self, faultAddress))
    {
        frameCount = OwnImageFault_WalkToHandler(
            *exception->ContextRecord, anchor, self, frames,
            kOwnImageFaultMaxFrames, reachedHandler);
    }
    if (DecideFaultContainment(self, faultAddress, frames, frameCount,
                               reachedHandler) == FaultContainment::Contain)
        return EXCEPTION_EXECUTE_HANDLER;
    if (t_ownImageFaultLastRecord != record)
    {
        t_ownImageFaultLastRecord = record;
        stats.lastFaultAddress.store(faultAddress, std::memory_order_relaxed);
        stats.lastForeignAddress.store(
            FirstForeignFaultAddress(self, faultAddress, frames, frameCount),
            std::memory_order_relaxed);
        stats.lastCode.store(record->ExceptionCode, std::memory_order_relaxed);
        stats.propagated.fetch_add(1, std::memory_order_release);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
