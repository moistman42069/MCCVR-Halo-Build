#pragma once

// PROJECT RECLAIMER 0.10'S RECOMPILED HALO 3, FOR THE MOD'S HALO 3 CODE (docs/RECLAIMER-0.10-ENGINE-EVIDENCE.md).
//
// From 0.10.0 the client runs no halo3.dll. It keeps halo3.dll's image (header, sections, relocated to its base) inside
// its own executable's mapping, so the mod's signature searches and data reads work there unchanged; but none of that
// image's code runs. Every function runs as a translated copy elsewhere, with its own calling convention
// (src/common/lifted_engine_logic.h). This layer is what the mod's Halo 3 code needs on top:
//
//   Halo3Module()   the guest image, which stands in for GetModuleHandle(L"halo3.dll").
//   Hooks           MinHook calls aimed at a function start in the guest image (lifted_minhook.h routes them
//                   here) hook its translated body instead, and the detour and its "original" pointer keep their
//                   ordinary Windows x64 signatures.
//   Callable()      a game function the mod calls itself: the address to call instead of the guest image's.
//   GuestTlsSlots() and GuestReturnAddress(): what __readgsqword(0x58) and _ReturnAddress() give in MCC, for the
//                   game call the mod's code is running inside.
//
// Nothing here does anything outside a Project Reclaimer client whose engine has been found: every call is then the
// stock behaviour (the target itself, MinHook itself).

#include <windows.h>
#include <MinHook.h>
#include <cstdint>

namespace lifted
{
// Looks for the recompiled engine (worker thread; cheap once found or ruled out). True once it is in use.
bool Poll();
bool Active();
// The guest image (halo3.dll's) while the engine is in use, else null.
HMODULE Halo3Module();
// Whether `address` lies in the guest image's code section.
bool InGuestCode(uintptr_t address);
// sig::ModuleRange's fallback (dllmain.cpp registers it): the guest image for "halo3.dll" while in use.
bool ModuleRangeFallback(const wchar_t* moduleName, uintptr_t& base, size_t& size);
// sig::SetScanShadow's source (dllmain.cpp registers it): the guest image's code section with the client's traps
// over each function's start replaced by halo3.dll's own bytes, while in use.
bool ScanShadow(uintptr_t& start, size_t& size, const uint8_t*& copy);

// The hook calls lifted_minhook.h routes here. A target that is not in the guest image's code goes to MinHook.
MH_STATUS CreateHook(LPVOID target, LPVOID detour, LPVOID* original);
MH_STATUS EnableHook(LPVOID target);
MH_STATUS DisableHook(LPVOID target);
MH_STATUS RemoveHook(LPVOID target);
MH_STATUS QueueEnableHook(LPVOID target);
MH_STATUS QueueDisableHook(LPVOID target);
// The address MinHook itself patched for `target` (the translated body), or `target` when it is not a lifted hook.
LPVOID PatchedAddress(LPVOID target);
// A detour that changes its stack arguments (the fifth on) must say how many before its hook is created, so the call
// to the original writes them back; by default none is written back.
void SetStackArguments(LPVOID target, unsigned count);

// What to call for a game function at `target` (an address in the guest image). `target` itself otherwise. The call
// works only from inside a hooked game call on the same thread (it runs on that call's guest registers and stack);
// from anywhere else, or for an address that is not a translated function's start, it returns 0 without running
// anything, and the log says so. Never null for an address in the guest image's code.
void* Callable(void* target);
template <typename Fn>
inline Fn Callable(Fn target)
{
    return reinterpret_cast<Fn>(Callable(reinterpret_cast<void*>(target)));
}

// The guest thread's TLS array (what gs:[0x58] holds in MCC), for the innermost hooked game call on this thread;
// null outside one.
void** GuestTlsSlots();
// VirtualProtect, except that the guest image's code (which never runs) is refused: a byte patch there would report
// success and change nothing. The refusal is logged once per address and fails as VirtualProtect fails.
BOOL GuardedVirtualProtect(LPVOID address, SIZE_T size, DWORD protect, PDWORD previous);
// RtlLookupFunctionEntry, except for an address in the guest image's code: Windows finds the client's executable
// there (the image lies inside it) and none of halo3.dll's functions, so the entry comes from the image's own
// exception directory, with the image's base.
PRUNTIME_FUNCTION LookupFunctionEntry(DWORD64 address, PDWORD64 imageBase, PUNWIND_HISTORY_TABLE history);
// The guest return address of the innermost hooked game call on this thread (what _ReturnAddress() gives in the
// detour in MCC); 0 outside one.
uintptr_t GuestReturnAddress();
} // namespace lifted

// HALOMCCVR_GAME_TLS_ARRAY() and HALOMCCVR_GAME_RETURN_ADDRESS(), for the mod's game code.
#include "game_tls.h"
