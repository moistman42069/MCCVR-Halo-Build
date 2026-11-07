#pragma once

// The game's TLS array (as an address) and the caller of the game call a detour is in. In MCC they are exactly
// __readgsqword(0x58) and _ReturnAddress(). In Project Reclaimer 0.10 the game runs as translated code on a guest
// register file of its own (lifted_engine.h): its TLS array and the guest return address belong to the innermost
// hooked game call on the thread, and outside one the TLS array is null. Every reader already handles a null array.
//
// The DLL (HALOMCCVR_LIFTED_ENGINE, set for its target alone) gets both cases; a unit test that compiles a file of
// the DLL gets the plain intrinsics, whatever it includes first. Its includer has <intrin.h> (a test may stand in
// for _ReturnAddress with a macro of its own before it).

#include <cstdint>

#if defined(HALOMCCVR_LIFTED_ENGINE) && HALOMCCVR_LIFTED_ENGINE
#include "lifted_engine.h"
#define HALOMCCVR_GAME_TLS_ARRAY() \
    (lifted::Active() ? reinterpret_cast<uintptr_t>(lifted::GuestTlsSlots()) : static_cast<uintptr_t>(__readgsqword(0x58)))
#define HALOMCCVR_GAME_RETURN_ADDRESS() \
    (lifted::Active() ? lifted::GuestReturnAddress() : reinterpret_cast<uintptr_t>(_ReturnAddress()))
// A caller an assembly detour read from its own stack (`ret`): in Project Reclaimer 0.10 that is the lifted bridge,
// and the game's caller is the guest return address.
#define HALOMCCVR_GAME_CALLER(ret) (lifted::Active() ? lifted::GuestReturnAddress() : (ret))
#else
#define HALOMCCVR_GAME_TLS_ARRAY() static_cast<uintptr_t>(__readgsqword(0x58))
#define HALOMCCVR_GAME_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(_ReturnAddress())
#define HALOMCCVR_GAME_CALLER(ret) (ret)
#endif
