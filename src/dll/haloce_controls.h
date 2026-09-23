#pragma once
#include <cstddef>
#include <cstdint>
#include "../common/haloce_frame_context.h"

// Native state is independently verified and available even if optional
// controller-shot, hand-palette or turn hooks cannot be installed.
struct HaloCELocalPlayerState
{
    uint32_t generation{},weapon{0xffffffffu},unit{0xffffffffu},player{0xffffffffu},parent{0xffffffffu};
    int16_t nativePerspective{-1},inputUser{-1};
    bool firstPersonVisible{},nativePreparesFirstPerson{},onFoot{},hasControlledUnit{};
    bool nativeInputBlocked{true},nativeLookBlocked{true},nativePaused{},nativeCinematicFlag{};
};

bool HaloCEControls_Poll(uintptr_t base,size_t size,uint32_t generation,bool active) noexcept;
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept;
// Reads the verified native clock independently of player/unit/weapon state,
// which can be unavailable while the pause menu or restart screen is visible.
// Optional input suppression is independent of unit/weapon lifetime and is
// not by itself a menu-visible flag. Its validity is returned separately.
bool HaloCEControls_GetNativePaused(bool& paused,bool* suppressed=nullptr,
    bool* inputKnown=nullptr) noexcept;
bool HaloCEControls_OwnsLookStick() noexcept;
bool HaloCEControls_MapMoveStick(float x,float y,float& outputX,float& outputY) noexcept;
// Coherent on-foot owner and native CENTER camera/HMD/reference data. This
// camera position is not claimed to be the unit's collision/body origin.
bool HaloCEControls_GetLocomotionFrame(HaloCELocalPlayerState& state,
    halo_ce::RenderContext& context) noexcept;

// Optional read-only native height correction; no engine state writes.
float HaloCEControls_PhysicalCrouchCorrection(uint32_t generation,uint64_t epoch,
    float physicalDown,bool allowed) noexcept;
