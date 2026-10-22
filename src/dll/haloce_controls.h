#pragma once
#include <cstddef>
#include <cstdint>
#include "vr.h"
#include "../common/haloce_frame_context.h"
#include "../common/support_grip_logic.h"
#include "../common/vehicle_boom_logic.h"

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
// The seated local player's own control yaw as a horizontal world direction (halo_ce::Reference::seatHeading): the
// angle the game's look update owns for the player's input user, which the turn stick moves and the hand's aim never
// does. False on foot, with no controlled unit, or when the record does not read.
bool HaloCEControls_SeatHeading(uint32_t expectedGeneration,halo_ce::Vec3& heading) noexcept;
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

// THE SEAT THE LOCAL PLAYER SITS IN, for the vehicle cameras' cold worker (game.cpp haloce_vehicle_cameras.inl): what
// the camera file names it by and the role it plays (haloce_vehicle_camera_logic.h). Guarded; the game's thread or
// the cold worker. These seat reads write nothing of their own, but the player state they start from asks the game
// for the player's perspective (state_user_perspective), which stores what it works out: keep them off the render
// paths. OnFoot: the player has no vehicle (or no unit). Unknown: not readable now (the caller keeps what it had).
enum class HaloCEVehicleSeatRead : uint8_t { OnFoot, Seated, Unknown };
struct HaloCEVehicleSeatFacts
{
    uint32_t generation{},unit{0xffffffffu},parent{0xffffffffu},root{0xffffffffu};
    int16_t seat{-1};
    uint32_t definition{0xffffffffu}; // the vehicle's definition datum (its object's first word)
    bool seatFlagsRead{};
    uint32_t seatFlags{};
    bool drivesIt{},aimsIt{};         // the vehicle's two occupant slots hold the player's unit
    float boundsRadius{};             // world units, the vehicle definition's; 0 = not read
    uint64_t modelIdentity{};         // haloce_vehicle_identity.inl's node-name fingerprint; 0 = not read
    char tagPath[128]{};              // the vehicle definition's tag path; "" = not read
};
HaloCEVehicleSeatRead HaloCEControls_ReadVehicleSeatFacts(HaloCEVehicleSeatFacts& facts) noexcept;
// The same seat for a camera hook, every frame: who sits where, and the vehicle's size; no tag path, flags or
// fingerprint. False on foot or when not readable. Guarded; the game's thread (see above).
struct HaloCEVehicleSeatNow
{
    uint32_t generation{},unit{0xffffffffu},parent{0xffffffffu},root{0xffffffffu},definition{0xffffffffu};
    int16_t seat{-1};
    float boundsRadius{}; // world units, the vehicle definition's; 0 = not read
};
bool HaloCEControls_ReadVehicleSeatNow(HaloCEVehicleSeatNow& seat) noexcept;
// THE SEAT CAMERA'S TRACK, for the vehicle cameras' boom (haloce_vehicle_camera_logic.h): the track the game's
// following camera hangs its camera on in seat `seat` of the vehicle `parent`, as camera_track_splut picks it -- the
// seat camera's first track, else the game globals' default. Positions and orientations of up to
// vehicle_boom::kMaxTrackPoints control points; a track with more is read with track.count 0. False when the seat is
// not readable now (another generation, the vehicle gone). Guarded; the cold worker (it reads tags, not the player).
struct HaloCEVehicleSeatTrack
{
    // The seat camera's look limits (radians, up positive), both 0 = none of its own: the player's look pitch is held
    // to them, each inside the flat game's own +-85.5 deg (vehicle_boom::LookRange).
    float pitchMin{},pitchMax{};
    int32_t tracks{};       // tracks the seat camera lists
    bool defaultTrack{};    // it lists none: the game globals' default
    int32_t points{};       // control points in the track
    vehicle_boom::Track track{};
};
bool HaloCEControls_ReadVehicleSeatTrack(uint32_t generation,uint32_t parent,int16_t seat,
    HaloCEVehicleSeatTrack& track) noexcept;
