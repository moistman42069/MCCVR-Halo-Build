#pragma once
#include <cstddef>
#include <cstdint>
#include "vr.h"
#include "../common/haloce_frame_context.h"
#include "../common/support_grip_logic.h"

// Native state is independently verified and available even if optional
// controller-shot, hand-palette or turn hooks cannot be installed.
struct HaloCELocalPlayerState
{
    uint32_t generation{},weapon{0xffffffffu},unit{0xffffffffu},player{0xffffffffu},parent{0xffffffffu};
    int16_t nativePerspective{-1},inputUser{-1};
    bool firstPersonVisible{},nativePreparesFirstPerson{},onFoot{},hasControlledUnit{};
    bool nativeInputBlocked{true},nativeLookBlocked{true},nativePaused{},nativeCinematicFlag{};
    // Tri-state ownership evidence support (persistent support grip):
    // hasFirstPersonUserRecord is false when the native FP user record is
    // unavailable (Unknown, never absence). weaponSlotPresent records the raw
    // native slot before validation, so an empty slot is distinguishable from
    // a non-null candidate that failed object/owner validation.
    bool hasFirstPersonUserRecord{},weaponSlotPresent{};
};

// CE current-invocation support decision plus the relationship receipt the
// caller used, so a two-read transaction can require continuity before it
// commits geometry built from support-derived aim.
// `frozenPrimaryAimSupportDerived` is the provenance of the frozen primaryAim
// this invocation would consume (true only when its solve accepted
// support-derived two-hand geometry), never a live re-sample. Never mutates
// the durable relationship.
struct CeSupportInvocation
{
    bool detach = false;
    bool readable = false;
    bool engaged = false;
    uint64_t epoch = 0;
    uint32_t generation = 0, unit = 0xffffffffu, weapon = 0xffffffffu;
};

// `featureWired` is VR_SupportGripWiredForTitle(GameTitle::HaloCE) evaluated by
// the caller: with the feature off, or for a title that has no wired
// persistent-grip slice, this returns the base (no-detach) decision without
// reading any durable state, so the caller's behaviour is byte-for-byte the
// base path (A0 applicability gate / PG-off parity).
inline CeSupportInvocation CeEvaluateSupportInvocation(bool featureWired,
    const HaloCELocalPlayerState& state,
    bool frozenPrimaryAimSupportDerived) noexcept
{
    CeSupportInvocation result{};
    if (!featureWired)
        return result;
    SupportGripRelationshipSnapshot relationship{};
    result.readable = VR_GetSupportGripRelationship(relationship);
    if (!result.readable)
    {
        // Snapshot unavailable: relationship continuity cannot be proven, so
        // the support-capable carrier is not consumed for this invocation.
        result.detach = true;
        return result;
    }
    result.engaged = relationship.engaged;
    result.epoch = relationship.epoch;
    result.generation = relationship.generation;
    result.unit = relationship.unit;
    result.weapon = relationship.weapon;
    bool ownerTrusted = false;
    if (relationship.engaged)
    {
        const support_grip::OwnerEvidence evidence = support_grip::CeOwnerEvidence(
            state.generation != 0, state.hasFirstPersonUserRecord,
            state.weaponSlotPresent, state.weapon);
        ownerTrusted = !support_grip::SupportCarrierMustDetachForInvocation(true,
            support_grip::OwnerTuple{relationship.title, relationship.generation,
                relationship.unit, relationship.weapon},
            evidence,
            support_grip::OwnerTuple{GameTitle::HaloCE, state.generation,
                state.unit, state.weapon});
    }
    result.detach = support_grip::SupportInvocationMustDetach(result.readable,
        result.engaged, ownerTrusted, frozenPrimaryAimSupportDerived);
    return result;
}

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
