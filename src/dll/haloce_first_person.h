#pragma once
#include <cstddef>
#include <cstdint>
#include "haloce_controls.h"

// Independent optional transaction; false never revokes CE stereo ownership.
bool HaloCEFirstPerson_Poll(uintptr_t base,size_t size,uint32_t generation,
    bool active) noexcept;
bool HaloCEFirstPerson_Armed() noexcept;
bool HaloCEFirstPerson_AimArmed() noexcept;

// Narrow native ownership facts, not an invented all-purpose gameplay state.
// Perspective zero is proven to admit CE's own first-person prepare branch;
// other perspective values remain unnamed until separately established.
bool HaloCEFirstPerson_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept;
// THE ENGINE'S OWN CAMERA FOR A UNIT (seat convergence, seat_converge_logic.h): CE's deterministic director camera
// (0xB14F14), the call its player_aim_projectile makes for the player's biped at every fire (returning to 0xB67BE4)
// and the origin of its crosshair test. Asked now for `unit`, under SEH, on the simulation thread. Returns 1 with
// the camera's position and the perspective it took (0 first person, else following), 0 when the aim lane's hook on
// it is not installed, -1 when the call faulted.
int HaloCEFirstPerson_SeatEye(uint32_t unit,float position[3],int& perspective) noexcept;
// Last committed graph, bounded by title/generation/space and 150 ms age.
uint64_t HaloCEFirstPerson_WeaponGraph(uint32_t generation,uint64_t space,uint64_t now) noexcept;
