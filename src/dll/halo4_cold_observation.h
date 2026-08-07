#pragma once

#include <cstddef>
#include <cstdint>

// C-H4-2: Halo 4 cold observation. Verifies the pinned retail identity and
// the E-H4-4 anchor table against the LOADED halo4.dll image, once per module
// instance, only after the level-load gate has PROVEN the level is running.
// Read-only: no hook, no write, no config change, and a preflight failure
// changes nothing except the log. The 50 ms title worker is the only caller;
// scanning never runs from Present or any game render callback.
//
// gateArrayProven must be the gate's own report of whether it resolved the
// player-view array. A fail-open gate (array unprovable, likely an MCC
// update) must NOT admit this preflight: the less the evidence matches the
// module, the more likely a scan lands in a loading screen, which is the
// exact touch the load-bounce rule forbids. When withheld it says so, once.
void Halo4ColdObservation_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool halo4LevelRunning, bool gateArrayProven) noexcept;

// True while no COMPLETED observation exists for this generation. A failed
// module pin does not complete the attempt (it retries); a finished PASS or
// FAIL verdict does. Lets the worker skip module-range queries once settled.
bool Halo4ColdObservation_Pending(uint32_t generation) noexcept;
