// MCC'S RELOAD OF A GAME DLL WAITS FOR OUR TEARDOWN.
//
// THE PROBLEM (docs/REACH-SECOND-GAME-2026-10-07.md). Without this gate a second Halo: Reach game in one MCC
// session bounces back to the lobby at the first try and starts at the next. Every game is meant to start in a
// fresh image of its DLL. When a game ends, everyone lets go of the DLL (measured, in ms after the last frame: a
// CRT worker of the game at 530, MCC's own FreeLibrary between 1080 and 1800, the game's main thread with
// FreeLibraryAndExitThread at 1750), it unloads, and MCC then loads all six game DLLs again for its menus (2840).
// Reach's camera core keeps a loader reference on haloreach.dll for as long as MinHook owns addresses inside it,
// and lets go when its teardown has finished, 4.4 s after the last frame. MCC's reload comes before that: it
// finds the old image still there, takes it, and the next game starts in a module whose own shutdown has already
// run. Halo 3's core holds no such reference: its DLL unloads and its second game starts.
//
// THE GATE keeps the reference (the hooks still need the image mapped while they are removed) and makes the reload
// wait for it: one hook on ntdll's LdrLoadDll, where every way of loading a DLL arrives. A load of a DLL that a
// core has registered, while that core is tearing down, waits until the core has let go. The old image is gone
// by then, and the load maps a fresh one. (The reload follows the unload within a millisecond and lands on the
// same base, so the module poll cannot see it: TitleAdapter_NoteImageUnloading tells the adapter.)
//
//   * WHO WAITS: whoever loads that DLL by name while the core's teardown is under way, OR after the title has
//     drawn nothing for kStillMs. In the second case the gate asks for the teardown itself: the core's own
//     heartbeat asks 1 s after the last frame, or up to 3 s after an unpause, and MCC's reload comes
//     2.2 to 3.1 s after the last frame (measured), so a game ended soon after an unpause can be reloaded
//     before the core has been told. A load of a game's DLL by name while that game has stopped drawing IS the
//     game's end. A teardown always ends, so the wait does: when the core lets go, when it says it cannot
//     (HostUnloadGate_Abandon), or after kWaitMs. Then the load goes on unchanged. Nothing
//     waits while the title is drawing.
//   * WHAT IT COSTS: MCC's loading thread stands still for the rest of the teardown, once per game end (1.7 s
//     measured). Every other DLL load in the process pays one pointer compare per registered core.
//   * IT CANNOT MAKE THINGS WORSE THAN NO GATE: a load it lets through unwaited, or after a time-out, gets the
//     old image, which is what every load gets without the gate.
//   * TWO ALTERNATIVES THAT DO NOT WORK. (1) A hook on FreeLibrary that makes "any caller that is not
//     this mod" wait: FreeLibrary on a game DLL is called a thousand times a minute during play by this mod's
//     own scoped references, and they arrive through Steam's overlay (gameoverlayrenderer64.dll hooks
//     FreeLibrary), so the caller is never this mod's address, and the core is torn down seconds into a
//     level. (2) Making the game's main thread wait before it leaves the DLL: it waits, leaves last, and the
//     image still stays, because MCC's reload has already taken it. The dev census below shows both.
//   * NOT WHILE MCC CLOSES. Closing MCC from inside a game ends the game the same way, reload and all, while
//     the rest of MCC shuts down around it. A reload held back there has coincided (observed once)
//     with a first-chance access violation in MCC's own code (MCC-Win64-Shipping.exe+0x8AD697 reading
//     0x540, on one of MCC's task threads). So the game window's WM_CLOSE / WM_DESTROY ends every wait and
//     prevents new ones (HostUnloadGate_HostClosing): at a close the gate does nothing.
//     A WM_CLOSE only counts for kClosingMs: if MCC is still running after that it was not closing (a quit
//     that was cancelled, a stray message), and the gate works again. WM_DESTROY is for good.
//   * A STUCK TEARDOWN COSTS ONE WAIT, NOT ONE PER RELOAD. MCC loads all six game DLLs at every return to its
//     menus. If a core's teardown cannot finish (a step that keeps returning "try again"), each of those loads
//     would stand still for the whole kWaitMs. So the first wait that runs out, or that the core ends with
//     "I cannot let go", marks the reference: no further load waits for it until the core has let go and
//     taken a new one (Pin::gaveUp; the report says so).
//   * OTHER CORES. Reach's game end is measured and its core may be ASKED to tear down (the rule above).
//     ODST's core holds halo3odst.dll the same way and registers a WAIT-ONLY reference: a load waits only
//     while that core's own teardown is already under way, and the gate never starts one. ODST's second
//     game is not confirmed in game. Halo 3's and Halo 4's cores hold no owning reference. Halo CE's eleven
//     feature lanes each hold one and are not registered: whether CE's second game bounces is not measured.
//   * The decisions are host_unload_gate_logic.h (tests/host_unload_gate_tests.cpp); this file is the loader,
//     the clock and the hook around them.
//   * title_unload_wait = 0: no wait; every load takes whatever image is there.
//   * No logging and no allocation in the hook; the cold worker reports each game end once.
#include "host_unload_gate.h"
#include "../common/host_unload_gate_logic.h"
#include "../common/config.h"
#include "../common/log.h"
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
namespace logic = host_unload_gate;
using logic::kStillMs;
using logic::kWaitMs;
constexpr int kPins = 4;             // one per title core that holds its DLL
constexpr int kNameChars = 40;

struct LoaderString   // ntdll's UNICODE_STRING
{
    USHORT length, maximumLength;   // bytes
    PWSTR buffer;
};
using LdrLoadDllFn = LONG(NTAPI*)(PWSTR, PULONG, LoaderString*, PVOID*);

struct Pin
{
    std::atomic<HMODULE> module{nullptr};
    std::atomic<std::atomic<bool>*> tearingDown{nullptr};   // the core's own "teardown requested"
    std::atomic<const std::atomic<uint64_t>*> lastFrameMs{nullptr};   // GetTickCount64 of the title's last frame
    std::atomic<uint32_t> abandoned{0};   // counts the core's "I cannot let go" notices
    std::atomic<bool> mayAsk{false};      // the gate may ask this core to tear down (Reach)
    std::atomic<bool> gaveUp{false};      // a wait for this reference ran out: no further load waits for it
    std::atomic<const char*> title{""};
    wchar_t name[kNameChars]{};   // the DLL's file name, lower case; written before `module`
    int nameLength = 0;
};

struct Gate
{
    LdrLoadDllFn original = nullptr;
    uintptr_t selfBase = 0, selfSize = 0;
    Pin pins[kPins];
    // Since the last report. A game end is one wait; the counters exist so nothing is lost if two come close.
    std::atomic<uint32_t> released{0}, releasedStillLoaded{0}, timedOut{0}, abandoned{0}, switchedOff{0};
    std::atomic<uint32_t> hostClosed{0};
    std::atomic<uint32_t> gaveUpPassed{0};   // loads let through because their reference had given up
    std::atomic<uint32_t> asked{0};   // waits in which the gate asked for the teardown itself
    std::atomic<bool> hostDestroyed{false};
    std::atomic<uint64_t> hostCloseMs{0};   // GetTickCount64 of the last WM_CLOSE
    std::atomic<uint32_t> longestMs{0}, arrivedMs{0}, waiting{0};
    std::atomic<const char*> lastTitle{""};
} g_gate;

// MCC itself is closing: its window was destroyed, or was asked to close a moment ago.
bool HostClosing(const Gate& gate, uint64_t now) noexcept
{
    return logic::HostClosing(gate.hostDestroyed.load(std::memory_order_acquire),
                              gate.hostCloseMs.load(std::memory_order_acquire), now);
}

uint64_t FrameAge(const Pin& pin, uint64_t now) noexcept
{
    const std::atomic<uint64_t>* const frame = pin.lastFrameMs.load(std::memory_order_acquire);
    return logic::FrameAge(frame ? frame->load(std::memory_order_acquire) : 0, now);
}

// `name` is a path or a bare file name; true if its file name is the pin's, whatever the case.
bool NamesPin(const Pin& pin, const LoaderString& name) noexcept
{
    return logic::NamesFile(pin.name, pin.nameLength, name.buffer, name.length / 2);
}

// Someone loads a game DLL whose core is tearing down: the game has ended and MCC wants the DLL again.
__declspec(noinline) void LoadWaits(Pin& pin, HMODULE module, std::atomic<bool>* askTeardown) noexcept
{
    Gate& gate = g_gate;
    gate.lastTitle.store(pin.title.load(std::memory_order_relaxed), std::memory_order_relaxed);
    const ULONGLONG start = GetTickCount64();
    gate.arrivedMs.store(static_cast<uint32_t>(FrameAge(pin, start)), std::memory_order_relaxed);
    if (!g_config.title_unload_wait)
    {
        gate.switchedOff.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (HostClosing(gate, start))
    {
        gate.hostClosed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (askTeardown)
    {
        // The title has stopped drawing and its DLL is being loaded again: the game has ended, and the core's
        // heartbeat has not said so yet. Say it (the same flag the heartbeat sets; the worker does the rest).
        askTeardown->store(true, std::memory_order_release);
        gate.asked.fetch_add(1, std::memory_order_relaxed);
    }
    const uint32_t abandonedAtEntry = pin.abandoned.load(std::memory_order_acquire);
    gate.waiting.fetch_add(1, std::memory_order_acq_rel);
    logic::Wait how = logic::Wait::Continue;
    for (;;)
    {
        const bool coreStillHolds = pin.module.load(std::memory_order_acquire) == module;
        bool imageStillLoaded = false;
        if (!coreStillHolds)
        {
            // The core has let go. Is the old image gone? (Nothing of ours is held here: no loader lock.)
            HMODULE still = nullptr;
            imageStillLoaded = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                  reinterpret_cast<LPCWSTR>(module), &still) && still == module;
        }
        const ULONGLONG now = GetTickCount64();
        how = logic::WaitStep(now - start, coreStillHolds, imageStillLoaded,
                              pin.abandoned.load(std::memory_order_acquire) != abandonedAtEntry,
                              HostClosing(gate, now));
        if (how != logic::Wait::Continue)
            break;
        Sleep(1);
    }
    // A teardown that did not finish in time, or cannot: this reference makes no further load wait. Only
    // while the core still holds this same reference (a new one starts clean in HostUnloadGate_Pin).
    if (logic::GivesUp(how) && pin.module.load(std::memory_order_acquire) == module)
        pin.gaveUp.store(true, std::memory_order_release);
    std::atomic<uint32_t>* const outcome = how == logic::Wait::Released ? &gate.released
        : how == logic::Wait::ReleasedStillLoaded ? &gate.releasedStillLoaded
        : how == logic::Wait::Abandoned ? &gate.abandoned
        : how == logic::Wait::HostClosed ? &gate.hostClosed
        : &gate.timedOut;
    const uint32_t waited = static_cast<uint32_t>(GetTickCount64() - start);
    uint32_t longest = gate.longestMs.load(std::memory_order_relaxed);
    while (waited > longest && !gate.longestMs.compare_exchange_weak(longest, waited, std::memory_order_relaxed))
    {
    }
    outcome->fetch_add(1, std::memory_order_relaxed);
    gate.waiting.fetch_sub(1, std::memory_order_acq_rel);
}

LONG NTAPI LdrLoadDllDetour(PWSTR path, PULONG flags, LoaderString* name, PVOID* handle)
{
    Gate& gate = g_gate;
    if (name)
        for (Pin& pin : gate.pins)
        {
            const HMODULE module = pin.module.load(std::memory_order_acquire);
            if (!module)
                continue;
            std::atomic<bool>* const tearingDown = pin.tearingDown.load(std::memory_order_acquire);
            if (!tearingDown || !NamesPin(pin, *name))
                continue;
            logic::PinView view;
            view.held = true;
            view.tearingDown = tearingDown->load(std::memory_order_acquire);
            view.mayAsk = pin.mayAsk.load(std::memory_order_acquire);
            view.gaveUp = pin.gaveUp.load(std::memory_order_acquire);
            view.frameAgeMs = FrameAge(pin, GetTickCount64());
            const logic::Load what = logic::Decide(view);
            if (what == logic::Load::Wait)
                LoadWaits(pin, module, nullptr);
            else if (what == logic::Load::WaitAndAsk)
                LoadWaits(pin, module, tearingDown);   // a frame has been drawn, and none for kStillMs
            else if (view.gaveUp && (view.tearingDown || view.frameAgeMs >= kStillMs))
                gate.gaveUpPassed.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    return gate.original(path, flags, name, handle);
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// WHO LETS GO OF A HELD GAME DLL ONCE ITS FRAMES HAVE STOPPED (dev, read-only). Every way into the loader ends in
// ntdll's LdrUnloadDll: FreeLibrary, FreeLibraryAndExitThread, a direct call. One row per distinct first frame
// outside the system's own loader modules and Steam's overlay (which relays everyone's FreeLibrary), with the
// stack it came by. Calls with a frame of this mod in them (its own scoped references) are left out.
using LdrUnloadDllFn = LONG(NTAPI*)(PVOID);
constexpr int kCensusRows = 8, kCensusFrames = 10, kCensusSkipped = 4;
struct CensusRow
{
    std::atomic<uintptr_t> key{0};
    std::atomic<uint32_t> count{0}, thread{0}, stillMs{0}, depth{0};
    void* frames[kCensusFrames]{};
};
struct Census
{
    LdrUnloadDllFn original = nullptr;
    uintptr_t skippedBase[kCensusSkipped]{}, skippedSize[kCensusSkipped]{};
    CensusRow rows[kCensusRows];
    std::atomic<uint32_t> overflow{0};
} g_census;

bool CensusSkippedFrame(uintptr_t address) noexcept
{
    for (int i = 0; i < kCensusSkipped; ++i)
        if (address - g_census.skippedBase[i] < g_census.skippedSize[i])
            return true;
    return false;
}

LONG NTAPI LdrUnloadDllDetour(PVOID base)
{
    Gate& gate = g_gate;
    if (base)
        for (Pin& pin : gate.pins)
            if (pin.module.load(std::memory_order_acquire) == base)
            {
                const uint64_t age = FrameAge(pin, GetTickCount64());
                if (age < kStillMs)
                    break;   // frames are still coming (or none has come): a scoped reference, not an unload
                void* stack[kCensusFrames]{};
                const USHORT depth = CaptureStackBackTrace(1, kCensusFrames, stack, nullptr);
                uintptr_t key = 0;
                bool ours = false;
                for (USHORT i = 0; i < depth; ++i)
                {
                    const uintptr_t address = reinterpret_cast<uintptr_t>(stack[i]);
                    ours |= address - gate.selfBase < gate.selfSize;
                    if (!key && !CensusSkippedFrame(address))
                        key = address;
                }
                if (!key || ours)
                    break;
                bool placed = false;
                for (CensusRow& row : g_census.rows)
                {
                    uintptr_t held = row.key.load(std::memory_order_acquire);
                    if (!held && row.key.compare_exchange_strong(held, key, std::memory_order_acq_rel))
                    {
                        row.thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
                        row.stillMs.store(static_cast<uint32_t>(age), std::memory_order_relaxed);
                        memcpy(row.frames, stack, sizeof(stack));
                        row.depth.store(depth, std::memory_order_release);
                        held = key;
                    }
                    if (held == key)
                    {
                        row.count.fetch_add(1, std::memory_order_relaxed);
                        placed = true;
                        break;
                    }
                }
                if (!placed)
                    g_census.overflow.fetch_add(1, std::memory_order_relaxed);
                break;
            }
    return g_census.original(base);
}

void CensusInstall() noexcept
{
    // The overlay may not be there (a launch outside Steam): its row then stays empty.
    const wchar_t* const names[kCensusSkipped]{L"ntdll.dll", L"kernelbase.dll", L"kernel32.dll",
                                                L"gameoverlayrenderer64.dll"};
    for (int i = 0; i < kCensusSkipped; ++i)
        if (const HMODULE module = GetModuleHandleW(names[i]))
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            const auto* nt =
                reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
            g_census.skippedBase[i] = reinterpret_cast<uintptr_t>(module);
            g_census.skippedSize[i] = nt->OptionalHeader.SizeOfImage;
        }
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    void* const target = ntdll ? reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrUnloadDll")) : nullptr;
    if (!target || MH_CreateHook(target, reinterpret_cast<void*>(&LdrUnloadDllDetour),
                                 reinterpret_cast<void**>(&g_census.original)) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
        LOG("UNLOAD GATE (dev): the unload census could not hook ntdll's LdrUnloadDll");
}

void CensusReport() noexcept
{
    for (CensusRow& row : g_census.rows)
    {
        const uint32_t depth = row.depth.load(std::memory_order_acquire);
        const uint32_t count = row.count.exchange(0, std::memory_order_relaxed);
        if (!depth || !count)
            continue;
        char text[900];
        int length = 0;
        text[0] = 0;
        for (uint32_t i = 0; i < depth && length < static_cast<int>(sizeof(text)) - 80; ++i)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(row.frames[i]);
            HMODULE module = nullptr;
            char file[MAX_PATH]{};
            const char* name = "?";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(address), &module) && module &&
                GetModuleFileNameA(module, file, MAX_PATH))
            {
                name = file;
                for (const char* p = file; *p; ++p)
                    if (*p == '\\' || *p == '/')
                        name = p + 1;
            }
            const int wrote = _snprintf_s(text + length, sizeof(text) - length, _TRUNCATE, "%s%s+0x%llX",
                                          i ? " < " : "", name,
                                          static_cast<unsigned long long>(
                                              module ? address - reinterpret_cast<uintptr_t>(module) : address));
            if (wrote > 0)
                length += wrote;
        }
        LOG("UNLOAD GATE (dev census): a held game DLL was let go %u time%s after its frames had stopped (the first "
            "of them %u ms after the last frame, on thread %u) by: %s",
            count, count == 1 ? "" : "s", row.stillMs.load(std::memory_order_relaxed),
            row.thread.load(std::memory_order_relaxed), text);
    }
    if (const uint32_t overflow = g_census.overflow.exchange(0, std::memory_order_relaxed))
        LOG("UNLOAD GATE (dev census): %u more came from callers past the table's %d rows", overflow, kCensusRows);
}
#endif
} // namespace

bool HostUnloadGate_Install()
{
    Gate& gate = g_gate;
    if (gate.original)
        return true;
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&HostUnloadGate_Install), &self) && self)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
        const auto* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(self) + dos->e_lfanew);
        gate.selfBase = reinterpret_cast<uintptr_t>(self);
        gate.selfSize = nt->OptionalHeader.SizeOfImage;
    }
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    void* const target = ntdll ? reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrLoadDll")) : nullptr;
    const char* const fallback = "a game DLL a core holds stays loaded past the game's end, as before (a second Halo: "
                                 "Reach game may bounce to the lobby once)";
    if (!target || MH_CreateHook(target, reinterpret_cast<void*>(&LdrLoadDllDetour),
                                 reinterpret_cast<void**>(&gate.original)) != MH_OK)
    {
        gate.original = nullptr;
        LOG("UNLOAD GATE: StockFallback: ntdll's LdrLoadDll could not be hooked; %s", fallback);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK)
    {
        MH_RemoveHook(target);
        gate.original = nullptr;
        LOG("UNLOAD GATE: StockFallback: the LdrLoadDll hook did not enable; %s", fallback);
        return false;
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    CensusInstall();
#endif
    LOG("UNLOAD GATE: Installed: when a game ends MCC loads its DLL again for the menus. While a title core of this "
        "mod is tearing down and still holds that DLL, the load waits (at most %u ms) for the core to let go, so the "
        "old image unloads first and the next game starts in a fresh one. title_unload_wait is %s",
        kWaitMs, g_config.title_unload_wait ? "on" : "off");
    return true;
}

void HostUnloadGate_Pin(HMODULE module, std::atomic<bool>* tearingDown,
                        const std::atomic<uint64_t>* lastFrameMs, const char* title, bool mayAsk)
{
    if (!module || !tearingDown)
        return;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    const wchar_t* file = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            file = p + 1;
    const int nameLength = static_cast<int>(wcslen(file));
    if (!nameLength || nameLength >= kNameChars)
        return;
    for (Pin& pin : g_gate.pins)
    {
        if (pin.module.load(std::memory_order_acquire) == module)
            return;
        if (!pin.module.load(std::memory_order_acquire))
        {
            // Everything else first: a hook that sees the module must see the rest. One thread registers (the
            // worker), so an empty slot stays empty until the store below.
            for (int i = 0; i < nameLength; ++i)
                pin.name[i] = file[i] >= L'A' && file[i] <= L'Z' ? static_cast<wchar_t>(file[i] + (L'a' - L'A'))
                                                                 : file[i];
            pin.nameLength = nameLength;
            pin.tearingDown.store(tearingDown, std::memory_order_release);
            pin.mayAsk.store(mayAsk, std::memory_order_release);
            pin.gaveUp.store(false, std::memory_order_release);
            pin.lastFrameMs.store(lastFrameMs, std::memory_order_release);
            pin.title.store(title, std::memory_order_release);
            pin.module.store(module, std::memory_order_release);
            return;
        }
    }
}

void HostUnloadGate_Unpin(HMODULE module)
{
    if (!module)
        return;
    for (Pin& pin : g_gate.pins)
    {
        HMODULE held = module;
        (void)pin.module.compare_exchange_strong(held, nullptr, std::memory_order_acq_rel);
    }
}

void HostUnloadGate_Abandon(HMODULE module)
{
    if (!module)
        return;
    for (Pin& pin : g_gate.pins)
        if (pin.module.load(std::memory_order_acquire) == module)
            pin.abandoned.fetch_add(1, std::memory_order_acq_rel);
}

void HostUnloadGate_HostClosing(bool destroyed) noexcept
{
    if (destroyed)
        g_gate.hostDestroyed.store(true, std::memory_order_release);
    else
        g_gate.hostCloseMs.store(GetTickCount64(), std::memory_order_release);
}

void HostUnloadGate_Report()
{
    Gate& gate = g_gate;
#if HALOMCCVR_DEV_DIAGNOSTICS
    CensusReport();
#endif
    if (gate.waiting.load(std::memory_order_acquire))
        return;   // one line per game end, once its wait is over
    const uint32_t released = gate.released.exchange(0, std::memory_order_relaxed);
    const uint32_t stillLoaded = gate.releasedStillLoaded.exchange(0, std::memory_order_relaxed);
    const uint32_t timedOut = gate.timedOut.exchange(0, std::memory_order_relaxed);
    const uint32_t abandoned = gate.abandoned.exchange(0, std::memory_order_relaxed);
    const uint32_t switchedOff = gate.switchedOff.exchange(0, std::memory_order_relaxed);
    const uint32_t hostClosed = gate.hostClosed.exchange(0, std::memory_order_relaxed);
    const uint32_t asked = gate.asked.exchange(0, std::memory_order_relaxed);
    const uint32_t gaveUpPassed = gate.gaveUpPassed.exchange(0, std::memory_order_relaxed);
    if (gaveUpPassed)
        LOG("UNLOAD GATE: %u reload%s of a game DLL %s not made to wait: an earlier wait for this mod's teardown "
            "of that title ran out or was abandoned, and the teardown has still not finished. The load takes the "
            "old image as before", gaveUpPassed, gaveUpPassed == 1 ? "" : "s", gaveUpPassed == 1 ? "was" : "were");
    if (!released && !stillLoaded && !timedOut && !abandoned && !switchedOff && !hostClosed)
        return;
    const uint32_t longest = gate.longestMs.exchange(0, std::memory_order_relaxed);
    const uint32_t arrived = gate.arrivedMs.load(std::memory_order_relaxed);
    const char* const title = gate.lastTitle.load(std::memory_order_relaxed);
    const char* const stays = "the load takes the old image as before and the next game of this title may bounce to "
                              "the lobby once";
    // A teardown this gate started is named in whichever line reports the wait (AGENTS.md: a path that disarms
    // a core says so, naming itself). The core's own heartbeat line is not printed on that path.
    const char* const askedNote = asked ? " THE UNLOAD GATE ASKED FOR THIS TEARDOWN: the title had drawn nothing "
                                          "for half a second when its DLL was loaded by name, and the core's own "
                                          "heartbeat had not asked yet." : "";
    if (released)
        LOG("UNLOAD GATE %s: the game ended; MCC's reload of the game DLL came %u ms after the last frame and waited "
            "%u ms for this mod's teardown. The old image had unloaded by then, so the next game starts in a fresh "
            "one (%u load%s waited).%s", title, arrived, longest, released, released == 1 ? "" : "s", askedNote);
    if (stillLoaded)
        LOG("UNLOAD GATE %s: the game ended; MCC's reload of the game DLL came %u ms after the last frame and waited "
            "%u ms for this mod's teardown, but the old image was STILL LOADED when the mod had let go (someone else "
            "holds it): %s.%s", title, arrived, longest, stays, askedNote);
    if (timedOut)
        LOG("UNLOAD GATE %s: the game ended; MCC's reload of the game DLL came %u ms after the last frame, waited the "
            "whole %u ms and this mod's teardown had not finished: %s. No further reload waits for it until the "
            "teardown has finished.%s", title, arrived, longest, stays, askedNote);
    if (abandoned)
        LOG("UNLOAD GATE %s: the game ended; MCC's reload of the game DLL came %u ms after the last frame and stopped "
            "waiting after %u ms because this mod's teardown cannot finish without the game's own thread: %s. No "
            "further reload waits for it until the teardown has finished.%s",
            title, arrived, longest, stays, askedNote);
    if (hostClosed)
        LOG("UNLOAD GATE %s: MCC is closing; its reload of the game DLL came %u ms after the last frame and was not "
            "held back (waited %u ms)", title, arrived, longest);
    if (switchedOff)
        LOG("UNLOAD GATE %s: the game ended; MCC's reload of the game DLL came %u ms after the last frame and was not "
            "made to wait (title_unload_wait = 0): %s", title, arrived, stays);
}
