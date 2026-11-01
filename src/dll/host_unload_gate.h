#pragma once
// MCC'S RELOAD OF A GAME DLL WAITS FOR OUR TEARDOWN (host_unload_gate.cpp has the why and the evidence).
//
// A title core that keeps a loader reference on its game DLL while its hooks are installed registers that
// reference here. When the game ends MCC loads the DLL again for its menus; while the core is tearing down and
// still holds the DLL, that load waits, bounded, until the core has let go, so the old image unloads first and
// the load maps a fresh one, as it does without the mod.
#include <windows.h>
#include <atomic>
#include <cstdint>

// Once, after MH_Initialize. False: the gate is not installed and everything behaves as before.
bool HostUnloadGate_Install();
// Worker: `module` is the reference the core holds. `tearingDown` is the core's own "teardown requested" flag:
// a load of that DLL waits while it is set, and the gate SETS it when the DLL is loaded by name after the title
// has drawn nothing for half a second (the game has ended and the heartbeat has not said so yet). `lastFrameMs`
// is the GetTickCount64 of the title's last frame (0 = none yet). `title` is a string literal for the log.
// `mayAsk` false: a WAIT-ONLY reference. A load waits only while the core's own teardown is already under way;
// the gate never sets `tearingDown` (for a core whose game end has not been measured: ODST).
void HostUnloadGate_Pin(HMODULE module, std::atomic<bool>* tearingDown,
                        const std::atomic<uint64_t>* lastFrameMs, const char* title, bool mayAsk);
// Worker, AFTER the core's own FreeLibrary of that reference: a waiting load may go on.
void HostUnloadGate_Unpin(HMODULE module);
// Worker: the core cannot let go without the title's own thread (a restore that only that thread can make), so a
// load waiting for it stops waiting now instead of at the time-out. The reference stays registered.
void HostUnloadGate_Abandon(HMODULE module);
// Any thread (the game window's procedure): MCC itself is closing. No load waits, and one that is waiting goes
// on at once: a shutdown must not be held back. `destroyed` = WM_DESTROY (for good); false = WM_CLOSE, which
// counts for 30 s (a close MCC did not act on must not switch the gate off for the session).
void HostUnloadGate_HostClosing(bool destroyed) noexcept;
// Cold worker: one line per game end (when the reload came, how long it waited and how the wait ended).
void HostUnloadGate_Report();
