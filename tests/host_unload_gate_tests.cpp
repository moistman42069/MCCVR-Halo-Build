// The unload gate's decisions (src/common/host_unload_gate_logic.h): which load of a game DLL waits for this
// mod's teardown, how a wait ends, when a reference stops making loads wait, and the title adapter's rule for a
// DLL that was unloaded and loaded again between two module polls. The loader, the clock and the hook around
// them are src/dll/host_unload_gate.cpp and are exercised only by a second game in MCC.
#include "../src/common/host_unload_gate_logic.h"
#include <cstdio>
#include <cwchar>

using namespace host_unload_gate;
namespace
{
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", why);
    }
}
bool Names(const wchar_t* pin, const wchar_t* name)
{
    return NamesFile(pin, static_cast<int>(std::wcslen(pin)), name, static_cast<int>(std::wcslen(name)));
}
} // namespace

int main()
{
    // WHOSE DLL. The loader is handed a bare name or a path, in any case.
    const wchar_t* const reach = L"haloreach.dll";
    Check(Names(reach, L"haloreach.dll"), "the bare file name");
    Check(Names(reach, L"HaloReach.DLL"), "whatever the case");
    Check(Names(reach, L"D:\\SteamLibrary\\steamapps\\common\\Halo The Master Chief Collection\\haloreach\\haloreach.dll"),
          "a full path");
    Check(Names(reach, L"..\\haloreach/HALOREACH.dll"), "either separator");
    Check(!Names(reach, L"xhaloreach.dll"), "a longer file name that ends the same is another DLL");
    Check(!Names(reach, L"D:\\mods\\nothaloreach.dll"), "also behind a path");
    Check(!Names(reach, L"reach.dll") && !Names(reach, L"haloreach.dl") && !Names(reach, L""),
          "a shorter name is not it");
    Check(!Names(reach, L"haloreach.dll.bak") && !Names(reach, L"halo3.dll") && !Names(reach, L"haloreach_dll"),
          "nor a different one");
    Check(!Names(reach, L"haloreach.dll\\") && !Names(reach, L"haloreach.dll\\other.dll"),
          "the pin's name as a FOLDER is not the file");
    Check(!NamesFile(nullptr, 13, L"haloreach.dll", 13) && !NamesFile(reach, 13, nullptr, 13) &&
              !NamesFile(reach, 0, L"haloreach.dll", 13) && !NamesFile(reach, 13, L"haloreach.dll", 0),
          "nothing to compare is no match");
    // A counted string need not be terminated: only `chars` characters are read.
    Check(NamesFile(reach, 13, L"haloreach.dllXYZ", 13) && !NamesFile(reach, 13, L"haloreach.dllXYZ", 16),
          "the loader's string is counted, not terminated");

    // THE CLOCKS.
    Check(FrameAge(0, 5000) == 0, "no frame yet is never 'long ago'");
    Check(FrameAge(5000, 4000) == 0, "nor is a clock that is ahead");
    Check(FrameAge(4000, 5000) == 1000 && FrameAge(5000, 5000) == 0, "otherwise it is the time since the last frame");
    Check(HostClosing(true, 0, 1) && HostClosing(true, 100, 100 + 10 * kClosingMs), "a destroyed window is for good");
    Check(!HostClosing(false, 0, 123456), "no close message: MCC is not closing");
    Check(HostClosing(false, 1000, 1000) && HostClosing(false, 1000, 1000 + kClosingMs),
          "a close message counts for thirty seconds");
    Check(!HostClosing(false, 1000, 1001 + kClosingMs), "after that MCC was not closing, and the gate works again");
    Check(!HostClosing(false, 2000, 1000), "a close message stamped ahead of the clock does not count");

    // WHICH LOAD WAITS.
    PinView pin;
    Check(Decide(pin) == Load::Pass, "nobody holds the DLL: the load goes on");
    pin.held = true;
    pin.mayAsk = true;
    Check(Decide(pin) == Load::Pass, "the title is drawing (or has drawn nothing yet): no wait");
    pin.frameAgeMs = kStillMs - 1;
    Check(Decide(pin) == Load::Pass, "a frame a moment ago: still playing");
    pin.frameAgeMs = kStillMs;
    Check(Decide(pin) == Load::WaitAndAsk,
          "no frame for half a second and the DLL is loaded by name: the game ended; ask for the teardown and wait");
    pin.tearingDown = true;
    Check(Decide(pin) == Load::Wait, "the core is already tearing down: just wait");
    pin.frameAgeMs = 0;
    Check(Decide(pin) == Load::Wait, "whatever the frame clock says");
    // A wait-only reference (a core whose game end was not measured): the gate never starts its teardown.
    PinView waitOnly;
    waitOnly.held = true;
    waitOnly.frameAgeMs = 60000;
    Check(Decide(waitOnly) == Load::Pass, "a wait-only reference is never asked, however long the title has been still");
    waitOnly.tearingDown = true;
    Check(Decide(waitOnly) == Load::Wait, "it waits once its own core has begun to tear down");
    // After a wait ran out, no further load waits for that reference.
    pin.gaveUp = true;
    Check(Decide(pin) == Load::Pass, "a reference that gave up makes no load wait while tearing down");
    pin.tearingDown = false;
    pin.frameAgeMs = 60000;
    Check(Decide(pin) == Load::Pass, "nor is its core asked again");
    waitOnly.gaveUp = true;
    Check(Decide(waitOnly) == Load::Pass, "the same for a wait-only reference");
    PinView gone;
    gone.tearingDown = true;
    gone.gaveUp = false;
    Check(Decide(gone) == Load::Pass, "a reference that is no longer held makes nothing wait");

    // HOW A WAIT ENDS, in the order the outcomes are asked.
    Check(WaitStep(0, true, false, false, false) == Wait::Continue, "the core still holds the DLL: keep waiting");
    Check(WaitStep(kWaitMs - 1, true, false, false, false) == Wait::Continue, "until the last millisecond");
    Check(WaitStep(kWaitMs, true, false, false, false) == Wait::TimedOut, "then the wait has run out");
    Check(WaitStep(10, false, false, false, false) == Wait::Released, "the core let go and the image is gone");
    Check(WaitStep(10, false, true, false, false) == Wait::ReleasedStillLoaded,
          "the core let go but someone else holds the image");
    Check(WaitStep(kWaitMs + 5000, false, false, true, true) == Wait::Released,
          "letting go wins over everything else, even late");
    Check(WaitStep(10, true, false, true, false) == Wait::Abandoned, "the core said it cannot let go");
    Check(WaitStep(10, true, false, true, true) == Wait::Abandoned, "that is asked before MCC's closing");
    Check(WaitStep(10, true, false, false, true) == Wait::HostClosed, "MCC is closing: nothing is held back");
    Check(WaitStep(kWaitMs, true, false, false, true) == Wait::HostClosed, "also at the time-out");
    Check(WaitStep(10, true, true, false, false) == Wait::Continue,
          "'still loaded' means nothing while the core itself holds the image");

    // WHEN A REFERENCE STOPS MAKING LOADS WAIT.
    Check(GivesUp(Wait::TimedOut) && GivesUp(Wait::Abandoned), "a teardown that did not finish, or cannot");
    Check(!GivesUp(Wait::Released) && !GivesUp(Wait::ReleasedStillLoaded) && !GivesUp(Wait::HostClosed) &&
              !GivesUp(Wait::Continue),
          "not one that finished, and not MCC closing");
    // A stuck teardown over five returns to the menus: one wait, then four loads pass.
    {
        PinView stuck;
        stuck.held = true;
        stuck.tearingDown = true;
        int waited = 0;
        for (int i = 0; i < 5; ++i)
            if (Decide(stuck) != Load::Pass)
            {
                ++waited;
                stuck.gaveUp = GivesUp(WaitStep(kWaitMs, true, true, false, false));
            }
        Check(waited == 1, "a stuck teardown costs one wait, not one per reload");
    }

    // THE TITLE ADAPTER'S GAP.
    Check(ReloadedUnseen(true, 0x7FF800000000, 0x7FF800000000),
          "unloaded since the last poll and here again at the same base: loaded again in between");
    Check(!ReloadedUnseen(false, 0x7FF800000000, 0x7FF800000000), "nothing was unloaded: it is the same image");
    Check(!ReloadedUnseen(true, 0, 0x7FF800000000), "unloaded and gone: the poll sees that itself");
    Check(!ReloadedUnseen(true, 0x7FF900000000, 0x7FF800000000), "at another base: the poll sees that too");
    Check(!ReloadedUnseen(true, 0x7FF800000000, 0), "it was not there at the last poll: an ordinary arrival");
    Check(!ReloadedUnseen(true, 0, 0), "an empty slot is not a reload");

    std::printf("Unload gate: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
