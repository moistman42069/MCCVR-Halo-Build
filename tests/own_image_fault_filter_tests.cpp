// The SEH containment decision used by every Halo 2 Anniversary __except that
// wraps a call into game code. Pure decision table first, then the real filter
// against real faults in this test executable (its own image plays the part of
// HaloMCCVR.dll; ntdll and kernel32 play the game).
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "../src/common/seh_fault_containment_logic.h"
#include "../src/dll/own_image_fault_filter.h"

static unsigned checks = 0;
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok)
    {
        std::printf("FAIL: %s\n", why);
        std::exit(1);
    }
}

static void PureDecision()
{
    constexpr FaultImageRange self{0x180000000ull, 0x1000};
    constexpr uintptr_t inside = 0x180000800ull;
    constexpr uintptr_t first = 0x180000000ull;
    constexpr uintptr_t last = 0x180000FFFull;
    constexpr uintptr_t pastEnd = 0x180001000ull;
    constexpr uintptr_t below = 0x17FFFFFFFull;
    constexpr uintptr_t game = 0x7FF82DD67449ull;   // halo2 +0x107449, the incident

    Check(FaultImageContains(self, first) && FaultImageContains(self, last) &&
          FaultImageContains(self, inside),
          "image range includes its first and last byte");
    Check(!FaultImageContains(self, pastEnd) && !FaultImageContains(self, below) &&
          !FaultImageContains(self, 0),
          "image range excludes base+size, base-1 and null");
    Check(!FaultImageContains({0, 0x1000}, 0x10) &&
          !FaultImageContains({0x180000000ull, 0}, 0x180000000ull),
          "an unknown (empty) image contains nothing");

    // The incident: the fault is in the game's renderer.
    Check(DecideFaultContainment(self, game, nullptr, 0, true) ==
              FaultContainment::Propagate,
          "a fault in game code propagates");
    Check(DecideFaultContainment(self, pastEnd, nullptr, 0, true) ==
              FaultContainment::Propagate,
          "a fault one byte past the image propagates");
    Check(DecideFaultContainment({}, inside, nullptr, 0, true) ==
              FaultContainment::Propagate,
          "an unknown own image never contains");

    // The mod's own fault, every discarded frame the mod's own.
    const uintptr_t ownFrames[] = {inside, first, last};
    Check(DecideFaultContainment(self, inside, nullptr, 0, true) ==
              FaultContainment::Contain,
          "a fault in the handler's own function is contained");
    Check(DecideFaultContainment(self, last, ownFrames, 3, true) ==
              FaultContainment::Contain,
          "an own fault through own frames is contained");

    // Mod code called back by the game, inside the guarded call.
    const uintptr_t callback[] = {inside, game, inside};
    Check(DecideFaultContainment(self, inside, callback, 3, true) ==
              FaultContainment::Propagate,
          "an own fault under a game frame propagates (would unwind the game)");
    Check(FirstForeignFaultAddress(self, inside, callback, 3) == game,
          "the foreign frame is named");
    Check(FirstForeignFaultAddress(self, game, callback, 3) == game,
          "a foreign fault names itself");
    Check(FirstForeignFaultAddress(self, inside, ownFrames, 3) == 0,
          "no foreign address for an own-only chain");

    // An unfinished walk proves nothing.
    Check(DecideFaultContainment(self, inside, ownFrames, 3, false) ==
              FaultContainment::Propagate,
          "an incomplete walk propagates");
    Check(DecideFaultContainment(self, inside, nullptr, 2, true) ==
              FaultContainment::Propagate,
          "frames claimed but not supplied propagates");
}

// ---- the real filter, real faults ----------------------------------------

static OwnImageFaultStats stats;
static volatile uintptr_t nullAddress = 0;
static volatile int sink = 0;

__declspec(noinline) static void OwnFault()
{
    sink = *reinterpret_cast<volatile int*>(nullAddress);
}

__declspec(noinline) static void OwnFaultDeeper()
{
    OwnFault();
    sink += 1;   // not a tail call
}

using MoveMemoryFn = void(WINAPI*)(void*, const void*, SIZE_T);
static MoveMemoryFn ntdllMove = nullptr;

__declspec(noinline) static void ForeignFault()
{
    char destination[16]{};
    ntdllMove(destination, reinterpret_cast<const void*>(nullAddress), sizeof(destination));
    sink += destination[0];
}

static BOOL CALLBACK FaultingLocaleCallback(LPWSTR)
{
    OwnFault();
    return FALSE;
}

__declspec(noinline) static void OwnFaultUnderForeignFrame()
{
    EnumSystemLocalesW(&FaultingLocaleCallback, LCID_INSTALLED);
}

// 1 = the filtered handler ran, 2 = it propagated to the outer handler,
// 0 = nothing faulted.
__declspec(noinline) static int Guarded(void (*body)())
{
    __try
    {
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        __try { body(); }
        __except (OwnImageFaultFilter(GetExceptionInformation(), faultAnchor, stats))
        {
            return 1;
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 2; }
}

// Two filtered handlers around one fault: it is offered to both.
__declspec(noinline) static int InnerFiltered(void (*body)())
{
    const uintptr_t faultAnchor = OwnImageFault_Anchor();
    __try { body(); }
    __except (OwnImageFaultFilter(GetExceptionInformation(), faultAnchor, stats))
    {
        return 1;
    }
    return 0;
}

__declspec(noinline) static int NestedGuarded(void (*body)())
{
    __try
    {
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        __try { return InnerFiltered(body); }
        __except (OwnImageFaultFilter(GetExceptionInformation(), faultAnchor, stats))
        {
            return 3;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 2; }
}

static bool InModule(uintptr_t address, const wchar_t* name)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module))
        return false;
    return module == GetModuleHandleW(name);
}

static void RealFilter()
{
    ntdllMove = reinterpret_cast<MoveMemoryFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlMoveMemory"));
    Check(ntdllMove != nullptr, "ntdll RtlMoveMemory resolved");

    const FaultImageRange self = OwnImageFault_SelfRange();
    Check(FaultImageContains(self, reinterpret_cast<uintptr_t>(&OwnFault)) &&
          !FaultImageContains(self, reinterpret_cast<uintptr_t>(ntdllMove)),
          "own image range is this executable");

    OwnImageFault_BeginScope();
    Check(Guarded(&OwnFault) == 1, "own fault in a direct callee is contained");
    Check(Guarded(&OwnFaultDeeper) == 1, "own fault two own frames deep is contained");
    Check(stats.propagated.load() == 0, "contained faults are not counted as propagated");

    OwnImageFault_BeginScope();
    Check(Guarded(&ForeignFault) == 2, "a fault in foreign code propagates past the filter");
    Check(stats.propagated.load() == 1, "the propagated fault is counted");
    const uintptr_t foreignAt = stats.lastFaultAddress.load();
    Check(InModule(foreignAt, L"ntdll.dll"), "the recorded fault address is in ntdll");
    Check(stats.lastForeignAddress.load() == foreignAt, "a foreign fault names itself");
    Check(stats.lastCode.load() == EXCEPTION_ACCESS_VIOLATION, "the code is recorded");

    OwnImageFault_BeginScope();
    Check(NestedGuarded(&ForeignFault) == 2,
          "nested filtered handlers both let a foreign fault through");
    Check(stats.propagated.load() == 2, "one fault offered to two filters counts once");

    OwnImageFault_BeginScope();
    const int callback = Guarded(&OwnFaultUnderForeignFrame);
    Check(callback == 2,
          "an own fault inside a foreign (kernel32/kernelbase) frame propagates");
    Check(stats.propagated.load() == 3, "the callback fault is counted");
    Check(FaultImageContains(self, stats.lastFaultAddress.load()) &&
          !FaultImageContains(self, stats.lastForeignAddress.load()) &&
          stats.lastForeignAddress.load() != 0,
          "the callback fault is ours and the foreign frame is named");
}

int main()
{
    PureDecision();
    RealFilter();
    std::printf("own_image_fault_filter_tests: %u checks passed\n", checks);
    return 0;
}
