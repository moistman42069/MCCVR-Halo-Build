#pragma once
#include "../common/title_runtime_state.h"
#include <cstdint>
// Cold worker; independent optional hooks, never grants or revokes camera ownership.
void NativeReloadPolicy_Poll();
// Simulation-thread ownership check. The caller independently proves owner getter.
void* Game_ReloadPolicyWeapon(GameTitle title, uint32_t weapon);
// Halo 3's first-person block as this build lays it out: the stride of its players and of their weapon slots (retail's
// 0x2430 / 0x11BC until the node capacity is proven; Project Reclaimer 0.10 holds twice the nodes).
void Game_Halo3FpStrides(uint32_t& userStride, uint32_t& slotStride);
