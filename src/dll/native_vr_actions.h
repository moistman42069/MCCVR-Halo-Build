#pragma once
#include "../common/vr_action_mapping.h"

// Optional post-device action bridge. Transport routing remains in use until
// the converter and owned abstract-state reader have both been observed on
// the input thread for the current title generation.
void NativeVrActions_Poll();
// Worker, right after the Halo 3 core retired its hooks: the Halo 3 bridge lets go of halo3.dll in the same pass,
// while MCC still owns the DLL, so the next game gets a fresh image.
void NativeVrActions_Halo3CoreRetired();
void NativeVrActions_BeginPoll() noexcept;
void NativeVrActions_EndPoll() noexcept;
bool NativeVrActions_CanRoute(GameTitle title, uint64_t now) noexcept;
void NativeVrActions_Publish(const void* rawState, GameTitle title, uint64_t now,
    uint32_t actions, bool direct, uint32_t blockedActions=0) noexcept;
bool NativeVrActions_GestureRouting(GameTitle title, uint64_t now) noexcept;
constexpr uint32_t NativeVrActions_GestureBit(vr_mapping::Action action) noexcept
{ return action < vr_mapping::Count ? 1u << (18u + unsigned(action)) : 0; }
