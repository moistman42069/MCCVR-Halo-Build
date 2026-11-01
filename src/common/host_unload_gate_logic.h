#pragma once
// The decisions of the unload gate (src/dll/host_unload_gate.cpp) and of the title adapter's "unloaded and loaded
// again between two polls" rule (src/dll/title_adapter.cpp), without the loader, the clock or the hook: what the
// unit tests can hold still. The .cpp files have the why and the evidence.
#include <cstdint>

namespace host_unload_gate
{
inline constexpr uint32_t kWaitMs = 6000;      // a teardown takes 3.5 s (measured); past this the old behaviour is kinder
inline constexpr uint64_t kStillMs = 500;      // no frame of the title for this long, and its DLL is loaded by name: the game ended
inline constexpr uint64_t kClosingMs = 30000;  // how long a WM_CLOSE means "MCC is closing"

// `buffer` (`chars` characters, not terminated) is a path or a bare file name; true if its file name is `pinLower`
// (`wanted` characters, already lower case), whatever the case. "xhaloreach.dll" is not "haloreach.dll".
inline bool NamesFile(const wchar_t* pinLower, int wanted, const wchar_t* buffer, int chars) noexcept
{
    if (!pinLower || !buffer || wanted <= 0 || chars < wanted)
        return false;
    const wchar_t* const tail = buffer + (chars - wanted);
    if (chars > wanted && tail[-1] != L'\\' && tail[-1] != L'/')
        return false;
    for (int i = 0; i < wanted; ++i)
    {
        wchar_t c = tail[i];
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c + (L'a' - L'A'));
        if (c != pinLower[i])
            return false;
    }
    return true;
}

// How long ago the title drew its last frame. 0: no frame yet, or the clock is ahead (never "long ago").
inline uint64_t FrameAge(uint64_t lastFrameMs, uint64_t nowMs) noexcept
{
    return lastFrameMs && nowMs >= lastFrameMs ? nowMs - lastFrameMs : 0;
}

// MCC itself is closing: its window was destroyed, or was asked to close within the last kClosingMs.
inline bool HostClosing(bool destroyed, uint64_t closeMs, uint64_t nowMs) noexcept
{
    return destroyed || (closeMs && nowMs >= closeMs && nowMs - closeMs <= kClosingMs);
}

// One registered reference, as a load of its DLL by name finds it.
struct PinView
{
    bool held = false;         // a core holds the DLL
    bool tearingDown = false;  // the core's own "teardown requested"
    // The gate may ask for the teardown itself when the title has stopped drawing. Only for a core whose game end
    // was measured that way (Reach). Others wait only for a teardown their own core has begun.
    bool mayAsk = false;
    // A wait for this reference already ran out, or the core said it cannot let go: no further load waits for it.
    // Without this, a teardown that is stuck made EVERY reload of that DLL (MCC reloads all six at every return to
    // its menus) stand still for the whole kWaitMs. It costs one wait instead. Cleared when the reference is let go.
    bool gaveUp = false;
    uint64_t frameAgeMs = 0;
};
enum class Load : uint8_t
{
    Pass,       // not a wait: the load goes on as it always did
    Wait,       // the core is tearing down: wait for it to let go
    WaitAndAsk, // the title stopped drawing and the core has not been told: tell it, then wait
};
inline Load Decide(const PinView& pin) noexcept
{
    if (!pin.held || pin.gaveUp)
        return Load::Pass;
    if (pin.tearingDown)
        return Load::Wait;
    return pin.mayAsk && pin.frameAgeMs >= kStillMs ? Load::WaitAndAsk : Load::Pass;
}

// One look during a wait, in the order the outcomes are asked.
enum class Wait : uint8_t
{
    Continue,
    Released,            // the core let go and the old image is gone: the load maps a fresh one
    ReleasedStillLoaded, // the core let go but someone else still holds the image
    Abandoned,           // the core said it cannot let go without the title's own thread
    HostClosed,          // MCC is closing: nothing is held back
    TimedOut,
};
inline Wait WaitStep(uint64_t elapsedMs, bool coreStillHolds, bool imageStillLoaded, bool abandoned,
                     bool hostClosing) noexcept
{
    if (!coreStillHolds)
        return imageStillLoaded ? Wait::ReleasedStillLoaded : Wait::Released;
    if (abandoned)
        return Wait::Abandoned;
    if (hostClosing)
        return Wait::HostClosed;
    return elapsedMs >= kWaitMs ? Wait::TimedOut : Wait::Continue;
}
// The outcomes after which this reference makes no further load wait (PinView::gaveUp).
inline bool GivesUp(Wait outcome) noexcept
{
    return outcome == Wait::TimedOut || outcome == Wait::Abandoned;
}

// THE TITLE ADAPTER'S GAP. A game DLL the loader unmapped since the last module poll (`unloadCounted`: the unload
// notification counted one for the base that poll saw) and that is present again at that same base was unloaded
// and loaded again between the two polls. The poll cannot see it (same base, still there), so the adapter
// publishes the set without it first and the title starts a new generation in its new image. Not a gap: the DLL
// is gone now (the poll sees that itself), it is at another base (the poll sees that too), or nothing was unloaded.
inline bool ReloadedUnseen(bool unloadCounted, uintptr_t baseNow, uintptr_t basePolled) noexcept
{
    return unloadCounted && baseNow != 0 && baseNow == basePolled;
}
} // namespace host_unload_gate
