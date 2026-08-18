#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Reach-only render evidence and allocation-free policy. This file contains no
// Windows, COM, MinHook, logging, or engine writes, so the exact routing and
// rollback rules can be exhaustively tested before any detour is authorized.

inline constexpr size_t kReachRetailImageSize = 0x04EDA000;
inline constexpr uint32_t kReachRetailPeTimestamp = 0x68A0EFE1;
// One retail Reach build, signed once per storefront. A full byte diff of the
// Steam and Microsoft Store copies (docs/MCC-EDITIONS-EVIDENCE.md) differs only
// in the PE checksum field at 0x1C0 and inside the Authenticode certificate
// table at 0x00C99E00: zero code bytes, identical length, identical PE
// timestamp, identical RVAs. Both digests therefore describe exactly the code
// this file pins, and the Store copy the game loads out of WindowsApps hashes
// to the second one.
//
// This is not a loosening of the gate. Every other constant here - image size,
// PE timestamp, and every RVA below - is already specific to this single MCC
// build, so an MCC update invalidates the whole table either way. Listing the
// two signings of one build keeps the gate exactly as tight as it was while
// letting it recognise the edition it was always describing.
inline constexpr const char* kReachRetailModuleSha256[] = {
    // Steam
    "738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894",
    // Microsoft Store / Xbox app (Game Pass)
    "F9F39CF058FF28C298CC05964BC40898A57C30D33848FA54D950D8D6C2697E20",
};

// Reach's original title-native vehicle-input proof. Official HREK names and
// defines player_mapping_get_unit_by_output_user and unit_in_vehicle. The
// latter deliberately excludes vehicle type 6, so it covers ordinary vehicles
// (including the Banshee) but not every mounted/walk-up turret. Camera ownership
// below therefore uses unit_get_camera_info instead; this older predicate stays
// only as the fail-open input refinement until that richer sample is installed.
inline constexpr uintptr_t kReachPlayerUnitByOutputUserRva = 0x00053EF8;
inline constexpr uintptr_t kReachUnitInVehicleRva = 0x004F9368;
inline constexpr uintptr_t kReachUnitInVehicleEvaluatorRva = 0x0019EF28;
inline constexpr uintptr_t kReachUnitInVehicleEvaluatorCallRva = 0x0019EF5E;
inline constexpr uintptr_t kReachUnitInVehicleNameRva = 0x009FB710;
inline constexpr uintptr_t kReachUnitInVehicleDescriptorRva = 0x00A22B60;
inline constexpr uintptr_t kReachEngineTlsIndexRva = 0x00C17B18;

// HREK's unit_get_camera_info is the native seat-camera transaction. Its retail
// homolog returns the direct seated parent, signed raw seat index, a pointer to
// the occupied seat's camera struct (seat record +0x70), and the engine-computed
// world camera point. Unlike unit_in_vehicle it does not reject type-6 turrets.
// The exact body and ABI are pinned in REACH-SIGNATURE-EVIDENCE.md.
inline constexpr uintptr_t kReachUnitGetCameraInfoRva = 0x0048A4B4;
inline constexpr size_t kReachUnitGetCameraInfoBodySize = 0x45A;
inline constexpr char kReachUnitGetCameraInfoBodySha256[] =
    "455196B8372C26DBED9B067D42DC146FD7C71F3762C9AC784099C683A065E601";

// object_get_markers_by_string_id is the stock authored-marker primitive used
// by unit_get_camera_info itself. Its final boolean selects Reach's live
// render-interpolated node bank, so vehicle_cam_smoothing can choose the same
// native rendered/raw A/B without a synthetic predictor.
inline constexpr uintptr_t kReachObjectMarkerResolverRva = 0x0047044C;
inline constexpr size_t kReachObjectMarkerResolverBodySize = 0x2DB;
inline constexpr char kReachObjectMarkerResolverBodySha256[] =
    "10656BEFDF01B629240E0AD0C3E4774844232EC66C8DB810A2616B550BD8160E";

// object_get_ultimate_parent walks object datum +0x14 until the carrier root.
// This is distinct from the direct parent returned above: a Warthog/Wraith gun
// seat belongs to the attached gun object, while view-follow belongs to its
// carrier. The leaf function is 0x3F bytes (the PE's coarse unwind interval
// includes adjacent leaves).
inline constexpr uintptr_t kReachObjectUltimateParentRva = 0x00473DC0;
inline constexpr size_t kReachObjectUltimateParentBodySize = 0x3F;
inline constexpr char kReachObjectUltimateParentBodySha256[] =
    "4786C758FBDA4D57A52956164BA701811FC414BF980CE9F92DA7357F4DA13568";

inline constexpr uintptr_t kReachVehicleTypeAccessorRva = 0x004AC1E4;
inline constexpr size_t kReachVehicleTypeAccessorBodySize = 0x51;
inline constexpr char kReachVehicleTypeAccessorBodySha256[] =
    "61C18948736356611DF19F8DF2C0E2254B1C7D97D84DEF24EE0AA2B4496FECF4";
inline constexpr uintptr_t kReachTagGetRva = 0x00031AE8;
inline constexpr size_t kReachTagGetBodySize = 0x7B;
inline constexpr char kReachTagGetBodySha256[] =
    "1319ABAA28A84AA78172538E6D84A90D437BA9A8C5E4729C2FD769DA41792EC1";

enum class ReachVehicleInputState : uint32_t
{
    Unknown = 0,
    OnFoot = 1,
    Vehicle = 2,
};

inline constexpr uint64_t ReachVehicleInputSnapshot(
    uint32_t generation, ReachVehicleInputState state)
{
    return (static_cast<uint64_t>(generation) << 32) |
        static_cast<uint32_t>(state);
}

inline constexpr bool ReachVehicleInputSnapshotIsVehicle(
    uint64_t snapshot, uint32_t generation)
{
    return generation != 0 &&
        static_cast<uint32_t>(snapshot >> 32) == generation &&
        static_cast<uint32_t>(snapshot) ==
            static_cast<uint32_t>(ReachVehicleInputState::Vehicle);
}
inline constexpr uintptr_t kReachMainRenderViewRva = 0x000C31F4;
inline constexpr uintptr_t kReachNormalSetupCallRva = 0x000C36D6;
inline constexpr uintptr_t kReachNormalSetupTargetRva = 0x0026C204;
inline constexpr uintptr_t kReachNormalOuterCallRva = 0x000C3730;
inline constexpr uintptr_t kReachNormalOuterReturnRva = 0x000C3735;
inline constexpr uintptr_t kReachScreenshotOuterCallRva = 0x001D3864;
inline constexpr uintptr_t kReachScreenshotOuterReturnRva = 0x001D3869;
inline constexpr uintptr_t kReachPlayerViewRenderRva = 0x0026C6DC;
inline constexpr uintptr_t kReachPlayerViewRenderCallerRva = 0x000C33C4;
inline constexpr uintptr_t kReachPlayerViewRenderReturnRva = 0x000C33C9;
inline constexpr uintptr_t kReachOuterMainRenderCallRva = 0x000C2FAA;
inline constexpr uintptr_t kReachOuterMainRenderTargetRva = 0x000C33F8;
inline constexpr uintptr_t kReachOuterPresentCallRva = 0x000C3000;
inline constexpr uintptr_t kReachOuterPresentTargetRva = 0x0025113C;
inline constexpr uintptr_t kReachFrustumHelperRva = 0x00287F58;
// Reach's native pause flag, and the unique owner instruction that writes it.
//
// Identified 2026-07-27 by live observation of the running retail title, not by
// reading the stripped binary for behavior. Three independent lines agree:
//   1. A read-only writable-page differential over the loaded haloreach.dll
//      across three paused and two unpaused captures taken at different places
//      in the level. 2175 bytes survived as boolean and group-consistent.
//   2. Of those 2175, exactly four are referenced by any code at all (23
//      rip-relative byte-access instruction forms scanned across .text). This
//      one has one writer and eight readers spread across the engine - the
//      profile of a real global pause gate.
//   3. A 10 Hz live watch while the player paused and unpaused on a five-second
//      cadence: six clean alternating transitions 4.9-6.2 s apart, matching the
//      requested cadence exactly, with the other three candidates eliminated
//      (two flip several times per second, one moved once in 67 s).
// Polarity: 1 = paused, 0 = running. The flag lives past .data's raw size, so
// it is zero-filled at load, which is correct for "not paused at startup".
//
// The owner signature is the binding; the RVAs below are expected values used
// for logging and cross-checking only. The bare store instruction
// "44 88 2D ?? ?? ?? ??" matches 39 sites, so the TLS-member context is what
// makes this unique - verified exactly one match in the pinned module.
// Reach's single camera parent: render_camera_from_observer_camera.
//
// Stock Reach derives BOTH the world render camera and the CHUD world-to-screen
// projection camera from the same observer camera through this one function, so
// stock Reach has exactly ONE parent for everything the player sees. Our mod
// breaks that: weapon aim steers the observer camera onto the right controller
// ray, while head-look is applied to a PRIVATE render-side copy. The world then
// renders from the head while every other consumer of the observer camera - the
// CHUD marker projection, and the weather/rain pass - still follows the hand.
// The player sees name tags, objective markers and rain swing with the gun.
//
// Halo 3 and ODST do not have this defect because both apply head-look INSIDE
// their camera-copy hook and never restore the source; see the load-bearing
// comment at game.cpp "Do not scope this write again".
//
// The destination layout is proven by this function's own copy block
// (0x287E3B-0x287E6E), which is why the correction is a plain field copy:
//   observer +0x00 position -> render +0x00      (3 floats)
//   observer +0x28 forward  -> render +0x0C      (3 floats)
//   observer +0x34 up       -> render +0x18      (3 floats)
//   observer +0x6C vfov     -> render +0x28
// That destination is byte-identical to the compact camera layout this codebase
// already uses (pos +0x00, fwd +0x0C, up +0x18), the same one ReachApplyHeadLook
// writes - so the per-eye head camera can be copied straight in.
//
// Measured in the pinned module: the 23-byte prologue below is UNIQUE (exactly
// one match), and NONE of the six call sites reads a return value, so the
// function is void and the detour cannot clobber a result.
inline constexpr uintptr_t kReachRenderCameraFromObserverRva = 0x00287DFC;
// Return addresses of all six call sites, measured from their rel32 targets.
inline constexpr uintptr_t kReachObserverCameraReturnRvas[6] = {
    0x0025B404, 0x0025D406, 0x0026C2DE, 0x0026FA47, 0x0026FB13, 0x002E1525};
// 0x0026C2DE is the world render camera - the headset-accepted path, never
// corrected. 0x002E1525 is the CHUD world-to-screen projection camera.
// The remaining four are unidentified; the runtime tallies them by site so one
// headset session names them instead of a guess.
inline constexpr int kReachObserverCameraWorldSite = 2;
inline constexpr int kReachObserverCameraChudSite = 5;

// Pure, unit-testable: map a return RVA to its call-site index, or -1.
inline constexpr int ReachClassifyObserverCameraReturn(uintptr_t returnRva)
{
    for (int i = 0; i < 6; ++i)
        if (kReachObserverCameraReturnRvas[i] == returnRva)
            return i;
    return -1;
}

// The selective muzzle retarget: move the one odd first-person particle system
// onto the marker its siblings use.
//
// Game-wide fact from the full HREK weapon dump (19 firing effects): the
// assault rifle is the ONLY weapon whose first-person event places a
// camera-mode-1 particle system on a marker other than primary_trigger -
// muzzle_flash_long_brake sits on 'muzzle_flash' while muzzle_flash_round,
// muzzle_smoke and glow_soft sit on 'primary_trigger'. The three siblings
// provably track the controller-held gun; the odd one renders head-locked.
// This matches the player's observation that only some guns show the defect.
//
// The fix edits the LOADED tag data: find the effect definition whose
// first-person event contains exactly one mode-1 system at a different
// location than >=2 mode-1 siblings sharing one location, and write the
// siblings' location index over the odd one's. The engine then spawns it
// exactly like the siblings, which are proven to follow the gun.
//
// Runtime layout, read instruction-for-instruction from the engine's own
// emission gate at 0x001D4DB4 (the same function whose mode-1 patch removed
// both flashes, proving the layout):
//   handle  = *(u32*)(handleTable + index*8 + 4)
//   defBase = pool[handle >> 28] + handle*4
//   events block handle at defBase+0x30 (same *4 pool decode)
//   event element stride 0x40 bytes; particle-systems block handle at
//   event+0x38; particle-system element stride 0x70 bytes;
//   location u16 at element+0x14, camera mode u16 at element+0x1E.
// handleTable pointer and pool table are decoded from the unique 20-byte
// signature below (mov r13,[rip+d32] / lea r9,[rip+d32] / mov r12d,0x48),
// measured exactly one match; expected operands 0x00C1A600 / 0x04E39F20.
inline constexpr uintptr_t kReachEffectHandleTableRva = 0x00C1A600;
inline constexpr uintptr_t kReachEffectPoolTableRva = 0x04E39F20;
inline constexpr size_t kReachEffectEventStride = 0x40;
inline constexpr uintptr_t kReachEffectEventPsysBlockOffset = 0x38;
inline constexpr uintptr_t kReachEffectDefEventsBlockOffset = 0x30;
inline constexpr size_t kReachEffectPsysStride = 0x70;
inline constexpr uintptr_t kReachEffectPsysLocationOffset = 0x14;
inline constexpr uintptr_t kReachEffectPsysCameraModeOffset = 0x1E;
inline constexpr unsigned kReachEffectCameraModeFirstPerson = 1;
inline constexpr size_t kReachEffectMaxWalk = 16;

// Pure and unit-testable. Given the (cameraMode, location) pairs of one
// event's particle systems: find the majority location among mode-1 systems
// (ties broken toward the LOWER location index - primary_trigger is index 0 in
// every affected weapon: assault_rifle, dmr, needler, sniper_rifle,
// spartan_laser, spike_rifle per the game-wide HREK dump), then list every
// mode-1 system at any other location for retargeting onto the majority.
//
// The majority must have at least two members, so a weapon whose mode-1
// systems all share one marker (13 of the 19 weapons) is never touched, and a
// weapon with no agreeing pair is ambiguous and never touched.
struct ReachMuzzleRetargetPlan
{
    int count = 0;
    unsigned short newLocation = 0;
    int elements[kReachEffectMaxWalk] = {};
};

inline constexpr ReachMuzzleRetargetPlan ReachDecideMuzzleRetarget(
    const unsigned short* cameraModes, const unsigned short* locations,
    size_t count)
{
    ReachMuzzleRetargetPlan plan{};
    if (count < 3 || count > kReachEffectMaxWalk)
        return plan;
    int bestCount = 0;
    unsigned short bestLocation = 0xFFFF;
    for (size_t i = 0; i < count; ++i)
    {
        if (cameraModes[i] != kReachEffectCameraModeFirstPerson)
            continue;
        int same = 0;
        for (size_t j = 0; j < count; ++j)
            if (cameraModes[j] == kReachEffectCameraModeFirstPerson &&
                locations[j] == locations[i])
                ++same;
        if (same > bestCount ||
            (same == bestCount && locations[i] < bestLocation))
        {
            bestCount = same;
            bestLocation = locations[i];
        }
    }
    if (bestCount < 2)
        return plan;
    for (size_t i = 0; i < count; ++i)
    {
        if (cameraModes[i] == kReachEffectCameraModeFirstPerson &&
            locations[i] != bestLocation)
        {
            plan.elements[plan.count++] = static_cast<int>(i);
        }
    }
    plan.newLocation = bestLocation;
    return plan;
}

// The muzzle flash welded to the player's view, and the engine's own switch
// for it.
//
// Reach decides per particle system whether to emit, in the effect render pass
// at 0x001D4DB4. The relevant state, read straight from the disassembly:
//
//   r14d = (int8)effect[0x50] & (1 << playerIndex)   my first-person weapon?
//   r8d  = (int8)effect[0x51] & (1 << playerIndex)
//   r9b  = (r14d != 0)
//   camera mode = word [particleSystem + 0x1E]
//     0 -> independent of camera mode
//     1 -> only in first person   : allowed when r9b
//     2 -> only in third person   : denied when r14d or r8d is set
//
// So a third-person-only system is allowed whenever the effect is not flagged
// as the player's own FIRST-person weapon. The player's own WORLD weapon is not
// flagged either, because in flat Reach you never see your own body and it does
// not matter. In VR the world is rendered from the head and the player's own
// world weapon is right there, so its third-person muzzle flash renders at the
// body - fixed relative to the view, unaffected by aim, present on some weapons
// and not others. That is the reported defect, and it is why every attempt to
// move an effect LOCATION failed: the location was always correct.
//
// The intervention is the engine's own deny path. At 0x001D4F18 the mode-2 arm
// begins `test r14d,r14d / jne deny / test r8d,r8d / je allow`; replacing the
// first three bytes with a short jump to the deny at 0x001D4F22 makes every
// third-person-only particle system decline to emit. Flow only reaches
// 0x001D4F18 when camera mode is exactly 2, so modes 0 and 1 are untouched -
// the first-person flash on the controller-held gun still emits normally.
//
// Located by a signature, not the RVA: the 31 bytes below span the camera-mode
// switch and the mode-2 arm, and are measured UNIQUE (exactly one match). The
// patch site is the match plus kReachThirdPersonEffectPatchOffset.
inline constexpr uintptr_t kReachThirdPersonEffectDenyRva = 0x001D4F18;
inline constexpr uintptr_t kReachThirdPersonEffectPatchOffset = 0x12;
inline constexpr size_t kReachThirdPersonEffectPatchBytes = 3;
// jmp +8 -> lands on the engine's own `xor r10b, r10b` deny, then a nop so the
// replaced instruction's third byte is never a partial decode.
inline constexpr unsigned char kReachThirdPersonEffectPatch[3] = {
    0xEB, 0x08, 0x90};
inline constexpr unsigned char kReachThirdPersonEffectOriginal[3] = {
    0x45, 0x85, 0xF6};

// The FIRST-person arm of the same switch, at signature offset +0x21:
// `mov r10b, r9b` - allow a first-person-only system when the effect belongs
// to the local player's first-person weapon. The engine's first-person weapon
// renders in CAMERA SPACE and the mod never moves it (the controller-tracked
// gun is a render-time copy), so a first-person-only muzzle particle spawns
// head-locked and view-flat: the "trapped in 2D" flash. The mode-2 denial was
// verified applied and changed nothing, which is what isolates mode 1.
// `xor r10b, r10b` denies it. Mode 1 only ever renders for the local player's
// own weapon, so no other character's effects can be affected.
inline constexpr uintptr_t kReachFirstPersonEffectPatchOffset = 0x21;
inline constexpr unsigned char kReachFirstPersonEffectPatch[3] = {
    0x45, 0x32, 0xD2};
inline constexpr unsigned char kReachFirstPersonEffectOriginal[3] = {
    0x45, 0x8A, 0xD1};

// Reach's rain, and why it swings with head AND hand in VR.
//
// The rain particle renderer is retail 0x00288D60 (.pdata bounds
// 0x00288D60-0x00289367), dispatched from player_view_render at 0x0026CCC3
// behind the render_rain_particles debug var (0x00B4444C, entry 0x00B40FF8).
//
// It reads the TOP-OF-STACK render workspace's SECONDARY compact camera:
// position at workspace+0x154, forward at workspace+0x160 - i.e.
// kReachSecondaryCompactOffset plus the compact camera's own +0x00/+0x0C. It
// resolves that workspace exactly the way the engine does, from the camera
// stack: depth at 0x00B43ABC, pointer array at 0x00C878A8. During
// player_view_render the top of stack is 0x00C9FAE0, the default workspace.
//
// Then, measured at 0x00288DBA-0x00288E43, it computes
//
//     centre[i] = position[i] + forward[i] * (rainVolumeSize * 0.45f)
//
// and uploads that as the rain volume centre. THAT is the whole defect. In flat
// Reach the volume simply sits in front of the one camera and nobody notices.
// In VR, workspace+0x154 holds the mod's own per-eye camera, whose forward is
// head-look composed on top of the hand-steered observer camera - so the entire
// rain field translates with every head rotation AND every hand rotation.
//
// The correction zeroes ONLY that forward, for the duration of that one call,
// so the volume centres on the camera position and stops rotating with the
// view. Position tracking is retained: the rain must still follow the player.
//
// The plain prologue of 0x00288D60 matches 3 times and must NOT be used as a
// signature. The interior pattern below is measured UNIQUE (exactly one match,
// at 0x00288D9C); the entry is that match minus 0x3C.
//
// ABI, read from the entry and the dispatch site: the function consumes a
// single float in xmm2 (saved to xmm10 at 0x00288DA3, loaded by the caller at
// 0x0026CCBE) and reads no integer argument register, so a detour declared with
// two pass-through integer slots and a float third argument preserves the
// register state exactly.
inline constexpr uintptr_t kReachRainParticleRenderRva = 0x00288D60;
inline constexpr uintptr_t kReachRainRenderSigEntryOffset = 0x3C;
inline constexpr uintptr_t kReachCompactCameraForwardOffset = 0x0C;

// Reach's effect-location node-matrix resolver, and the second muzzle flash.
//
// The player's screenshot shows TWO muzzle elements: one correctly on the
// controller-held gun, one stuck at the face. HREK explains why there are two:
// effect_build_locations is ADDITIVE, not exclusive - an object-marker search
// runs first and unconditionally, and the first-person search is APPENDED into
// the same array using the SAME marker name. So one effect legitimately emits
// both a world location and a first-person location. The first-person one
// follows the controller because the mod already drives the FP weapon; the
// world one resolves against the stock, head-anchored sim weapon.
//
// Retail 0x00120EC4-0x00120FD8 is that resolver (homolog of HREK 0x36AB70,
// identified by HREK's own effects.cpp asserts 7630/7638/7649/7650).
// Disassembled in full; the branch is:
//
//   (int16)designator > -2                  -> WORLD path, calls 0x00471C30
//   else if (effect[0x50] & 0x0F) == 0      -> WORLD path (no FP user)
//   else if (effect[0x50] & 0xF0) == 0xF0   -> return, draws nothing
//   else                                    -> FIRST-PERSON path, tail-jmp
//        FpMarkerQuery(userIndex = (int8)effect[0x50] >> 4,
//                      objectIndex = effect[0x3C],
//                      markerIndex = designator & 0x7FFF,
//                      outMatrix)
//
// THE DISCRIMINATOR EXISTS, contrary to an earlier analysis that called this
// unhookable. effect[0x50] is first_person_weapon_user_mask (bits 0-3) and
// first_person_weapon_output_user_index (bits 4-7, 0xF = none). Those bits are
// a property of the EFFECT, not of the location, so they identify a
// first-person weapon effect regardless of which location is being resolved.
// A damage effect, explosion, or object/character spawn carries mask 0 and is
// therefore never touched.
//
// Both call sites in the world path (+0xBC and +0xE0) call 0x00471C30, the
// object marker query that IS shared with the projectile chain - which is why
// this fix redirects the resolver's own FP branch instead of touching 0x471C30
// or 0x0047044C (157 callers) at all.
//
// Measured in the pinned module: the 26-byte prologue is UNIQUE (exactly one
// match), and the tail jump at +0x46 decodes to the first-person marker query,
// so that address is derived from the match rather than hardcoded.
// The marker-not-found fallback, and the muzzle flash stuck at the player's
// view.
//
// In the resolver's world path, when the marker query 0x00471C30 returns NULL
// the code does `lea rcx,[identity]; cmovne rcx, rax` at 0x00120FAC/0x00120FB3
// and copies a GLOBAL IDENTITY real_matrix4x3 from haloreach+0x0098FA70. Its
// 13 floats are {scale 1; forward 1,0,0; left 0,1,0; up 0,0,1; position 0,0,0}
// - measured, not assumed. An effect handed that transform renders at the
// origin of whatever space it is composed in, which is why the player sees it
// pinned to the centre of their view, why it does not move when the gun is
// pointed off screen, and why it sits exactly where the flat crosshair used to.
//
// Only a location whose marker LOOKUP FAILED gets this matrix. Every effect
// that resolves a real marker - the flash on the controller-held gun, every
// other character's weapon, impacts, explosions - never touches it. That makes
// "did the resolver return the identity fallback" an exact, self-verifying test
// for the stuck element, with no tag, weapon, or camera-mode assumption behind
// it.
inline constexpr uintptr_t kReachEffectIdentityMatrixRva = 0x0098FA70;
inline constexpr size_t kReachEffectMatrixFloats = 13;
inline constexpr uintptr_t kReachEffectMatrixPositionOffset = 0x28;
// Far below any playable space, finite, and cheap to spot in a log.
inline constexpr float kReachEffectHiddenPositionZ = -1.0e6f;

inline constexpr uintptr_t kReachEffectLocationResolverRva = 0x00120EC4;
inline constexpr uintptr_t kReachEffectLocationFpJumpOffset = 0x46;
inline constexpr uintptr_t kReachEffectFpMarkerQueryRva = 0x002B0A58;
inline constexpr uintptr_t kReachEffectFpUserByteOffset = 0x50;
inline constexpr uintptr_t kReachEffectObjectIndexOffset = 0x3C;

// Pure and unit-testable: does this (effect fp byte, node designator) pair name
// a world location belonging to a first-person weapon effect? Only that pair is
// redirected onto the first-person weapon.
struct ReachEffectFpDecision
{
    bool redirect = false;
    int userIndex = -1;
    unsigned int markerIndex = 0;
};

inline constexpr ReachEffectFpDecision ReachDecideEffectLocation(
    unsigned char fpUserByte, unsigned int nodeDesignator)
{
    ReachEffectFpDecision decision{};
    const short designator = static_cast<short>(nodeDesignator & 0xFFFFu);
    // Only a genuine world marker index is redirected.
    //
    // designator <= -2 is already a first-person location - the engine's own
    // branch handles it. designator == -1 is the engine's "no marker at all"
    // case: the world path detects it (cmp ax, r10w / cmove r8w, ax) and
    // resolves the object origin instead of a marker. Feeding that to the
    // first-person marker query would ask for marker 0x7FFF, which is not the
    // same thing at all. Both stay with the engine.
    if (designator < 0)
        return decision;
    // Not a first-person weapon effect at all.
    if ((fpUserByte & 0x0Fu) == 0u)
        return decision;
    // No first-person output user; the engine would draw nothing on its own
    // FP path, so do not invent one here.
    if ((fpUserByte & 0xF0u) == 0xF0u)
        return decision;
    decision.redirect = true;
    decision.userIndex = static_cast<signed char>(fpUserByte) >> 4;
    decision.markerIndex = nodeDesignator & 0x7FFFu;
    return decision;
}

inline constexpr uintptr_t kReachNativePauseOwnerRva = 0x0000F5AD;
inline constexpr uintptr_t kReachNativePauseFlagRva = 0x00C1A0E2;
inline constexpr size_t kReachNativePauseOwnerSigLength = 45;
inline constexpr uintptr_t kReachNativePauseStoreOffset = 38;
// The remaining three stock camera-rebuild helpers, proven in
// REACH-SIGNATURE-EVIDENCE.md ("Inner stereo candidate and coherent rebuild
// constraints", steps 2-6). The projection builder is the direct call that
// follows the frustum helper at every one of its nine call sites; the
// camera-state updater and projection/matrix builder complete the stock
// pre-scope rebuild that writes player_view+0x3B0 and +0x490.
inline constexpr uintptr_t kReachProjectionBuilderRva = 0x002884BC;
inline constexpr uintptr_t kReachCameraStateUpdaterRva = 0x00286F9C;
inline constexpr uintptr_t kReachProjectionMatrixBuilderRva = 0x0028AF8C;
// Proven setup call sites (inside setup 0x26C204) that anchor the first two
// helpers by their exact rel32 target, so a mismatched image fails open.
inline constexpr uintptr_t kReachSetupFrustumCallRva = 0x0026C2FF;
inline constexpr uintptr_t kReachSetupProjectionCallRva = 0x0026C316;
inline constexpr uintptr_t kReachPlayerViewArrayRva = 0x029F2B90;
inline constexpr size_t kReachPlayerViewStride = 0x0A40;
inline constexpr uint32_t kReachPlayerViewCount = 4;
inline constexpr uintptr_t kReachDefaultWorkspaceRva = 0x00C9FAE0;
inline constexpr size_t kReachRenderScopeSnapshotSize = 0x02B0;
inline constexpr uintptr_t kReachCameraStackCallbackRva = 0x0026BFD4;
inline constexpr size_t kReachCameraStackCallbackBodySize = 0x006C;
inline constexpr char kReachCameraStackCallbackBodySha256[] =
    "6E2A249710A53498ADE7AFB12EE7414099D16315B2F06D90D8EC01D185E6B0C4";
inline constexpr uintptr_t kReachActiveViewRva = 0x04E389A8;
inline constexpr uintptr_t kReachCameraStackDepthRva = 0x00B43ABC;
inline constexpr uintptr_t kReachCameraStackPointersRva = 0x00C878A8;
inline constexpr uintptr_t kReachRenderCameraOwnerRva = 0x04E38A90;
inline constexpr uintptr_t kReachSelectedSpecializationRva = 0x04E38A08;
inline constexpr uintptr_t kReachDisplaySwapchainRva = 0x04E38868;
inline constexpr uintptr_t kReachDisplayGroupRva = 0x00C8E520;
inline constexpr uintptr_t kReachDisplaySelectedRtvRva = 0x00CA02E0;
inline constexpr uintptr_t kReachDisplaySurfaceCountOffset = 0x58;
inline constexpr uintptr_t kReachDisplaySurfaceArrayOffset = 0x60;
inline constexpr size_t kReachDisplaySurfaceRecordSize = 0x88;
inline constexpr uintptr_t kReachDisplaySurfaceRtvOffset = 0x08;
inline constexpr uintptr_t kReachDisplaySurfaceSrvOffset = 0x18;
inline constexpr uint32_t kReachDisplaySurfaceCount = 4;
inline constexpr uint32_t kReachDisplayFormatR8G8B8A8Unorm = 28;
// Reach's exact first-person camera transaction. Each visible FP wrapper copies
// the outer camera into a dedicated nested camera-stack workspace at 0xCFAC20,
// installs callback 0xC380 at +0x2A8, and passes the one global FP view at
// 0xBB8F68 to 0x286C6C while that workspace remains pushed. The rebuild copies
// the view's compact camera to nested+0, rebuilds nested+0x1E4, then tail-jumps
// to the two-pointer uploader at 0x282D60. Pinned HREK independently exposes
// the same nested transaction at 0x87C4A0/0x87C4B0 -> 0x8561A0.
inline constexpr uintptr_t kReachFpCameraRebuildRva = 0x00286C6C;
inline constexpr size_t kReachFpCameraRebuildBodySize = 0x0250;
inline constexpr char kReachFpCameraRebuildBodySha256[] =
    "125656AF65F61F02BA482830D307EBFDD00BBE2DF7264F155613B3FF8FAEFE58";
// Retail chud_draw_widget. HREK's own byte-compiled signatures (both official
// optimized builds, reach_tag_play and sapien_play) do not survive into MCC's
// differently-compiled retail haloreach.dll - verified, zero matches for even
// a cross-build-agreeing anchor. This address was instead found by tracing the
// real call graph forward from kReachPlayerViewRenderRva (proven correct every
// session Reach arms) rather than matching HREK's compiled bytes, and
// independently confirmed three ways against the documented HREK ABI:
//   1. Called from the real, verified player_view_render (0x26CEC9 -> a
//      per-player CHUD dispatcher at 0x2C29F8 -> this function at 0x2C2BF1).
//   2. Argument setup at the call site matches the documented five-argument
//      ABI exactly: RCX=player index, RDX=widget-record pointer (computed as
//      table_base + widget_index*32), R8D=that same widget index, R9D=a
//      computed flag, and a stack arg carrying a conditional draw-state
//      pointer - (user, descriptor, widget index, alternate path, draw state).
//   3. The function itself reads a SIGNED byte at [retained-descriptor + 4]
//      (movsx, at both 0x2DA6E7 and 0x2DA777) - exactly the documented
//      "signed_class_byte_offset = 0x04".
// Headset-pending: this is a newly-verified address, not yet proven in the
// installed game.
inline constexpr uintptr_t kReachHudDrawWidgetRva = 0x002DA364;
// Resolving a CHUD widget's real scripting class. Decoded from retail
// 0x2DA68A-0x2DA6EC, which is exactly what Reach itself does before drawing:
//
//   globalPtr  = *(module + kReachChudDefinitionTableRva)
//   handle     = *(globalPtr + (arg4 & 0xFFFF) * 8 + 4)
//   chudDef    = pool[handle >> 28] + handle * 4
//   collHandle = *(chudDef + 4)
//   collection = pool[collHandle >> 28]
//                  + (collHandle + descriptor[3] * kReachChudCollectionStride) * 4
//
// pool[i] is *(module + kReachChudPoolTableRva + i*8) - the same 4-bit-tag
// handle decode used everywhere else in this engine.
//
// The class byte then sits at collection + 4. The official tag definition
// gives the collection's field order and confirms that offset: a 4-byte
// "artist name" string id first, then the "scripting class" char enum.
//
// This matters because descriptor+4 is NOT the class - it is a widget index
// within the collection (0x2ED80C is a three-tier index accessor, strides
// 0x27/0x21/0x20). Comparing that index against 2 hid whichever widget
// happened to sit at index 2, which is why exactly one arc of the crosshair
// ever disappeared. Nearly every drawn widget authors its class as
// "undefined/use parent" (1092 of 1143 across the official exports), so the
// only correct source for the class is the owning collection.
inline constexpr uintptr_t kReachChudDefinitionTableRva = 0x00C1A600;
inline constexpr uintptr_t kReachChudPoolTableRva = 0x04E39F20;
inline constexpr uint32_t kReachChudCollectionStride = 0x37;
inline constexpr uintptr_t kReachChudCollectionClassOffset = 4;
inline constexpr uintptr_t kReachChudDescriptorCollectionByte = 3;
inline constexpr uintptr_t kReachFpCameraUploadRva = 0x00282D60;
inline constexpr size_t kReachFpCameraUploadBodySize = 0x0179;
inline constexpr char kReachFpCameraUploadBodySha256[] =
    "F9A6578992870A9F5BF8C733944A83A5A834CC95490D8D0CC7573021F452FD31";
inline constexpr uintptr_t kReachFpCameraWorkspaceRva = 0x00CFAC20;
inline constexpr uintptr_t kReachFpCameraViewRva = 0x00BB8F68;
inline constexpr uintptr_t kReachFpCameraWorkspaceCallbackRva = 0x0000C380;
inline constexpr uintptr_t kReachFpCameraWorkspaceCallbackOffset = 0x02A8;
inline constexpr uintptr_t kReachFpCameraCompactLeaRva = 0x00286C7F;
inline constexpr uintptr_t kReachFpCameraFrustumCallRva = 0x00286DD8;
inline constexpr uintptr_t kReachFpCameraProjectionCallRva = 0x00286DEF;
inline constexpr uintptr_t kReachFpCameraUploadCompactLeaRva = 0x00286E4F;
inline constexpr uintptr_t kReachFpCameraUploadJumpRva = 0x00286E6A;
struct ReachFpCameraWrapperBody
{
    uintptr_t rva;
    size_t size;
    const char* sha256;
};
inline constexpr std::array<ReachFpCameraWrapperBody, 3>
    kReachFpCameraWrapperBodies{{
        {0x0026DA08, 0x01F2,
         "03BCCB8401EBC487394F49C96DDB5B20309F3E1BB9D16F96DD5860D2155EA175"},
        {0x0026E2A0, 0x025D,
         "C36932D4D9D1357AD3D50F750A62C51DAE4530F350A7FA4E96AA04DC284171C2"},
        {0x0026EA78, 0x0314,
         "2B3033F4D4FB62E0AD709F38688D7A4613B2AFDE5CC67432254B51FB4F35A649"},
    }};
// Reach-native type-6 float debug variables. The pinned retail table contains
// one exact entry for each name; HREK independently corroborates the same two
// controls and authored values (docs/REACH-SIGNATURE-EVIDENCE.md). Production
// still resolves by name, then requires these exact value slots before writing.
inline constexpr uintptr_t kReachMotionBlurMaxEntryRva = 0x00B3A1C8;
inline constexpr uintptr_t kReachMotionBlurScaleEntryRva = 0x00B3A1E0;
inline constexpr uintptr_t kReachMotionBlurMaxValueRva = 0x00B44600;
inline constexpr uintptr_t kReachMotionBlurScaleValueRva = 0x00B44604;
// Reach's object lightmap-shadow pass writes dynamic caster shadows specifically
// onto static world lightmaps. That receiver boundary matched the headset fault,
// but candidate 839aed7 proved that suppressing the exact pass per eye leaves the
// black static-world defect unchanged. The binding remains as rejected evidence;
// production does not arm the scoped suppression.
//
// This is NOT `render_shadow_screenspace` or native SSAO/HDAO. HREK proves
// SSAO uses distinct `_surface_ssao*` intermediates, then composites its final
// result into shared `_surface_shadow_mask`. The earlier center-region readback
// and pre-player-view clear did not isolate this later producer, so it cannot
// reject SSAO. Official HREK publishes `render_lightmap_shadows` as a type-5
// boolean at descriptor
// 0x02014C30 -> value 0x02056C75. HREK's player-view wrapper checks it at
// 0x00836664 before calling the source-named lightmap-shadow renderer at
// 0x00865910. Retail repeats that exact control flow: its sole player-view call
// at 0x0026CB8F enters wrapper 0x0026E7B4, the compare at 0x0026E7C4 reads the
// live descriptor value below, and 0x0026E7E3 calls the retail renderer.
inline constexpr uintptr_t kReachLightmapShadowsNameRva = 0x009EB120;
inline constexpr uintptr_t kReachLightmapShadowsEntryRva = 0x00B41080;
inline constexpr uintptr_t kReachLightmapShadowsValueRva = 0x00B4444D;
inline constexpr uintptr_t kReachLightmapShadowsPlayerCallRva = 0x0026CB8F;
inline constexpr uintptr_t kReachLightmapShadowsWrapperRva = 0x0026E7B4;
inline constexpr uintptr_t kReachLightmapShadowsCompareRva = 0x0026E7C4;
inline constexpr uintptr_t kReachLightmapShadowsRenderCallRva = 0x0026E7E3;
inline constexpr uintptr_t kReachLightmapShadowsRenderRva = 0x0028B3D0;
// Reach native SSAO/HDAO. Official HREK's player-view wrapper has exactly one
// call at 0x008366A3 to 0x007FD180 after the first-person/depth work and the
// lightmap-shadow producer. The pinned retail image has the homologous sole
// call below. Its body is [0x2A13A0,0x2A1907), 0x567 bytes, with SHA-256
// 760D2BEC3AA13ABFA0AB2002E2873C9C8A9F1FEA9EE63238585C2A6C92943EE7.
// The candidate detours the callee and returns only inside an exact owned VR
// eye transaction; it mutates no native TLS flag and introduces no direct
// surface write or clear.
inline constexpr uintptr_t kReachSsaoCallRva = 0x0026E81D;
// This immediately following byte is `render_rain`, not a screen-space-shadow
// control. Official HREK descriptor/name mapping and the retail descriptor at
// 0x00B40FF0 both identify it. Keep the address only to pin SSAO call ordering.
inline constexpr uintptr_t kReachRainGateAfterSsaoRva = 0x0026E822;
inline constexpr uintptr_t kReachSsaoRva = 0x002A13A0;
inline constexpr uintptr_t kReachSsaoEndRva = 0x002A1907;
inline constexpr size_t kReachSsaoBodySize =
    kReachSsaoEndRva - kReachSsaoRva;
inline constexpr std::array<uintptr_t, 2> kReachSsaoShadowMaskCallRvas{
    0x002A169A, 0x002A1755};
inline constexpr uintptr_t kReachShadowMaskAcquireRva = 0x00252F08;
inline constexpr uint32_t kReachShadowMaskSurfaceIndex = 2;
inline constexpr char kReachSsaoBodySha256[] =
    "760D2BEC3AA13ABFA0AB2002E2873C9C8A9F1FEA9EE63238585C2A6C92943EE7";
// Proven first-person production anchors. 0x2B4EB0 is the final visible body
// palette consumer and 0x0CF1A4 publishes the live interpolation graph used by
// body, marker, and held-object consumers. The palette decodes the render model
// as modelHandle = *(u32*)(table + tag*8 + 4), then reads its bounded node count
// from descriptor+0x30.
// clamped to 120. It builds dest[i] = root(arg2) ∘ source[boneMap[i]] over count
// bones (BoneMatrix stride 0x34). The former 0x213224 special-bone composer
// experiment is a preserved negative result and is not part of this path.
inline constexpr uintptr_t kReachFpVisiblePaletteRva = 0x002B4EB0;
inline constexpr uintptr_t kReachFpInterpolateRva = 0x000CF1A4;
inline constexpr uintptr_t kReachRenderModelTableRva = 0x00C1A600;
inline constexpr uintptr_t kReachNodeRecordBlockTableRva = 0x04E39F20;
// Exact Reach counterpart to the accepted Halo 3/ODST native weapon-IK
// bypass. HREK's debug-variable table names the type-5 boolean
// `debug_animation_fp_weapon_ik_disable`; its homologous post-palette routine
// checks that byte and jumps to the existing no-weapon-IK epilogue before the
// support-hand solve. Retail repeats the same control flow at these RVAs.
inline constexpr uintptr_t kReachFpWeaponIkDecisionPreludeRva = 0x002B506E;
inline constexpr uintptr_t kReachFpWeaponIkDisableCompareRva = 0x002B507F;
inline constexpr uintptr_t kReachFpWeaponIkDisableBranchRva = 0x002B5085;
inline constexpr uintptr_t kReachFpWeaponIkDisabledEpilogueRva = 0x002B52D1;
inline constexpr uintptr_t kReachFpWeaponIkDisableNameRva = 0x009F2AD8;
inline constexpr uintptr_t kReachFpWeaponIkDisableEntryRva = 0x00B3AEB8;
inline constexpr uintptr_t kReachFpWeaponIkDisableValueRva = 0x04E38B61;
inline constexpr uint64_t kReachDebugBooleanType = 5;

// In free-hand mode Reach's visible left glove follows the left controller.
// During two-hand aim the controller instead steers the weapon ray; the model
// hand must stay in Reach's authored support grip and therefore remain inside
// the same rigid palette transform as the weapon.
inline constexpr bool ReachShouldBindVisibleLeftHandToController(
    bool twoHandAimActive) noexcept
{
    return !twoHandAimActive;
}

// Reach's screen colour/gamma publisher - the exact homologue of the Halo 3
// (+0x278EE0) and ODST (+0x2A6308) function the headset already proved drives
// game brightness. All three take (a0, a1, a2) in xmm0-2, shuffle them the same
// way, call the imported `powf` with a rodata base and a2, then publish the
// pairs (a0, powf(base, a2)) and (a1, a0*a1) as two 16-byte shader constants.
// Halo 3/ODST write a stack buffer and upload 0x280000/0x2D0000 and
// 0x280001/0x2D0001 inline; Reach stores the same terms into module globals,
// divides both leading terms by that same powf result, and tail-jumps to
// +0x252D64, which uploads 0x4E0000/0x540000 and 0x4E0001/0x540001. The input
// to published-brightness relationship is therefore identical, which is why one
// slider can scale a0/a1 in every title. The Halo 3 AOB finds nothing in Reach
// (the recompiled prologue is `sub rsp,0x48` with a different save layout), so
// this pinned RVA carries its own Reach-only signature.
inline constexpr uintptr_t kReachScreenColorUploadRva = 0x00252E28;

// Pinned HREK first-person arms layouts. The discovery-palette count describes
// the render_model output only; it is not the animation graph's live source
// count. Official weapon graphs retain the exact body prefix and append held
// object nodes (Spartan reaches 65 nodes), while the retail palette consumer
// permits a bounded source span through 120.
inline constexpr size_t kReachFpMaxSourceNodeCount = 120;
inline constexpr size_t kReachSpartanFpBodyNodeCount = 47;
inline constexpr size_t kReachEliteFpBodyNodeCount = 41;
inline constexpr size_t kReachFpBoneMatrixFloatCount = 13;

// Reach also submits a separate first-person lower-body render model. Official
// HREK's immutable runtime-import checksum and node count identify these exact
// tags. Count alone is insufficient: the ordinary world Spartan and Elite
// render models also contain 82 and 67 nodes respectively.
inline constexpr uint32_t kReachSpartanFpLegRuntimeImportChecksum =
    0x10041201u;
inline constexpr uint32_t kReachEliteFpLegRuntimeImportChecksum =
    0x1404030Eu;
inline constexpr int kReachSpartanFpLegNodeCount = 82;
inline constexpr int kReachEliteFpLegNodeCount = 67;

// Keep this identity separate from ReachFpBodyKind below. ReachFpBodyKind is
// the exact 47/41-node arms-layout fingerprint used for articulation; this
// enum identifies only the independently submitted lower-body palette.
enum class ReachFpLegPaletteKind : uint8_t
{
    None = 0,
    Spartan,
    Elite,
};

inline constexpr ReachFpLegPaletteKind ClassifyReachFpLegPalette(
    uint32_t runtimeImportChecksum, int nodeCount) noexcept
{
    if (runtimeImportChecksum ==
            kReachSpartanFpLegRuntimeImportChecksum &&
        nodeCount == kReachSpartanFpLegNodeCount)
    {
        return ReachFpLegPaletteKind::Spartan;
    }
    if (runtimeImportChecksum == kReachEliteFpLegRuntimeImportChecksum &&
        nodeCount == kReachEliteFpLegNodeCount)
    {
        return ReachFpLegPaletteKind::Elite;
    }
    return ReachFpLegPaletteKind::None;
}

// The pair boundary freezes an exact checksum/count-qualified lower-body tag.
// Only that tag may take the centered native-body path when seated legs are
// enabled. Arms, held-object attachments, unknown tags, and every disabled or
// unproven state retain the existing floating-hands collapse policy. View
// Follow is deliberately absent: it changes orientation, not presentation.
inline constexpr bool ReachFpShouldCollapseVisiblePalette(
    bool showSeatedLegs, ReachFpLegPaletteKind frozenKind,
    uint16_t frozenTag, uint16_t observedTag) noexcept
{
    return !showSeatedLegs || frozenKind == ReachFpLegPaletteKind::None ||
        observedTag != frozenTag;
}

inline constexpr bool ReachFpLegNodeCountMatchesKind(
    ReachFpLegPaletteKind kind, int nodeCount) noexcept
{
    return (kind == ReachFpLegPaletteKind::Spartan &&
            nodeCount == kReachSpartanFpLegNodeCount) ||
        (kind == ReachFpLegPaletteKind::Elite &&
         nodeCount == kReachEliteFpLegNodeCount);
}

inline constexpr bool ReachFpLegObservationCanValidate(
    bool observationValid, uint32_t observationGeneration,
    uint64_t observationPreparedSerial, uint32_t pairGeneration,
    uint64_t pairPreparedSerial) noexcept
{
    return observationValid && observationGeneration != 0 &&
        observationGeneration == pairGeneration &&
        observationPreparedSerial != 0 &&
        pairPreparedSerial > observationPreparedSerial;
}

inline bool ReachFpPackedGraphFinite(
    std::span<const float> packedRecords, size_t recordCount) noexcept
{
    if (!recordCount || recordCount > kReachFpMaxSourceNodeCount ||
        packedRecords.size() != recordCount * kReachFpBoneMatrixFloatCount)
        return false;
    for (size_t record = 0; record < recordCount; ++record)
    {
        const size_t begin = record * kReachFpBoneMatrixFloatCount;
        const float scale = packedRecords[begin];
        if (!std::isfinite(scale) || std::fabs(scale) <= 0.001f)
            return false;
        for (size_t component = 1;
             component < kReachFpBoneMatrixFloatCount; ++component)
            if (!std::isfinite(packedRecords[begin + component]))
                return false;
    }
    return true;
}

// Validate the complete candidate before invoking the caller's commit. This
// keeps the live engine graph byte-identical on NaNs, invalid roots/scales, or
// a partial transform failure. The commit callback is deliberately supplied by
// the runtime so the shared policy performs no engine memory access itself.
template <typename Commit>
inline bool ReachFpCommitGraphIfFinite(
    std::span<const float> packedCandidate, size_t recordCount,
    Commit&& commit)
{
    if (!ReachFpPackedGraphFinite(packedCandidate, recordCount))
        return false;
    return static_cast<bool>(commit());
}

// Render-model output index -> animation-graph source index. The Spartan
// fingerprint is confirmed by the live 47-node retail call and matches the
// official HREK render_model/animation graph names exactly. The Elite
// fingerprint is the corresponding exact HREK name mapping.
inline constexpr std::array<int32_t, kReachSpartanFpBodyNodeCount>
    kReachSpartanFpBodyBoneMap{{
        0, 3, 1, 2, 5, 4, 6, 9, 10, 8, 7, 13, 12, 14, 11, 22,
        26, 18, 21, 24, 19, 20, 15, 23, 16, 17, 25, 36, 35, 32,
        28, 33, 29, 27, 34, 31, 30, 46, 38, 42, 37, 40, 39, 43,
        45, 44, 41,
    }};
inline constexpr std::array<int32_t, kReachEliteFpBodyNodeCount>
    kReachEliteFpBodyBoneMap{{
        0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 14, 12, 11, 13, 22,
        20, 18, 21, 19, 16, 23, 24, 17, 15, 27, 28, 25, 31, 32,
        30, 26, 29, 35, 34, 39, 33, 40, 36, 38, 37,
    }};

// Hand descendants expressed first in render-model output order. The resolver
// maps these through the validated boneMap to source-space masks; callers must
// not apply a palette-order mask directly to the animation source.
inline constexpr uint64_t kReachSpartanLeftHandPaletteMask =
    0x0000475B43724000ull;
inline constexpr uint64_t kReachSpartanRightHandPaletteMask =
    0x000038A4BC8D8800ull;
inline constexpr uint64_t kReachEliteLeftHandPaletteMask =
    0x000000568F9A2000ull;
inline constexpr uint64_t kReachEliteRightHandPaletteMask =
    0x000001A97065C000ull;
// The official Spartan and Elite FP graphs share these exact hidden arm-source
// prefixes. HREK vertex weights prove each four-node set closes the skinning
// edges from its visible hand subtree. They remain outside the visible masks,
// but Reach must co-locate their collapsed records at the corresponding solved
// wrist so blended glove/undersuit vertices cannot bridge separate pivots.
inline constexpr uint64_t kReachLeftControllerOwnedAuxiliarySourceMask =
    0x00000000000011A0ull;
inline constexpr uint64_t kReachRightControllerOwnedAuxiliarySourceMask =
    0x0000000000004640ull;

enum class ReachFpBodyKind : uint8_t
{
    None = 0,
    Spartan,
    Elite,
};

struct ReachFpBodyLayout
{
    ReachFpBodyKind kind = ReachFpBodyKind::None;
    size_t paletteBodyNodeCount = 0;
    size_t liveSourceNodeCount = 0;
    int32_t rightShoulderSource = -1;
    int32_t rightElbowSource = -1;
    int32_t rightWristSource = -1;
    int32_t leftShoulderSource = -1;
    int32_t leftElbowSource = -1;
    int32_t leftWristSource = -1;
    int32_t cameraControlSource = -1;
    uint64_t rightHandPaletteDescendants = 0;
    uint64_t leftHandPaletteDescendants = 0;
    uint64_t rightHandSourceDescendants = 0;
    uint64_t leftHandSourceDescendants = 0;
    uint64_t rightControllerOwnedSourceBranch = 0;
    uint64_t leftControllerOwnedSourceBranch = 0;

    constexpr bool Valid() const noexcept
    {
        const bool kindMatchesBody =
            (kind == ReachFpBodyKind::Spartan &&
             paletteBodyNodeCount == kReachSpartanFpBodyNodeCount) ||
            (kind == ReachFpBodyKind::Elite &&
             paletteBodyNodeCount == kReachEliteFpBodyNodeCount);
        if (!kindMatchesBody)
            return false;
        const uint64_t bodySourceMask =
            (uint64_t{1} << paletteBodyNodeCount) - uint64_t{1};
        return
            liveSourceNodeCount >= paletteBodyNodeCount &&
            liveSourceNodeCount <= kReachFpMaxSourceNodeCount &&
            rightControllerOwnedSourceBranch ==
                (rightHandSourceDescendants |
                 kReachRightControllerOwnedAuxiliarySourceMask) &&
            leftControllerOwnedSourceBranch ==
                (leftHandSourceDescendants |
                 kReachLeftControllerOwnedAuxiliarySourceMask) &&
            (rightHandSourceDescendants &
             kReachRightControllerOwnedAuxiliarySourceMask) == 0 &&
            (leftHandSourceDescendants &
             kReachLeftControllerOwnedAuxiliarySourceMask) == 0 &&
            (leftControllerOwnedSourceBranch &
             rightHandSourceDescendants) == 0 &&
            (rightControllerOwnedSourceBranch &
             leftControllerOwnedSourceBranch) == 0 &&
            (rightControllerOwnedSourceBranch & ~bodySourceMask) == 0 &&
            (leftControllerOwnedSourceBranch & ~bodySourceMask) == 0;
    }
};

inline bool ResolveReachFpBodyLayout(
    std::span<const int32_t> paletteBoneMap,
    size_t liveSourceNodeCount,
    ReachFpBodyLayout& out) noexcept
{
    out = {};
    const size_t paletteBodyNodeCount = paletteBoneMap.size();
    if ((paletteBodyNodeCount != kReachSpartanFpBodyNodeCount &&
         paletteBodyNodeCount != kReachEliteFpBodyNodeCount) ||
        liveSourceNodeCount < paletteBodyNodeCount ||
        liveSourceNodeCount > kReachFpMaxSourceNodeCount)
    {
        return false;
    }

    // A body fingerprint is a complete permutation of its fixed source prefix.
    // Validate that invariant separately from exact equality so duplicates and
    // appended/out-of-range source indices always fail closed.
    std::array<bool, kReachFpMaxSourceNodeCount> seen{};
    for (int32_t source : paletteBoneMap)
    {
        if (source < 0 ||
            static_cast<size_t>(source) >= paletteBodyNodeCount ||
            seen[static_cast<size_t>(source)])
        {
            return false;
        }
        seen[static_cast<size_t>(source)] = true;
    }

    ReachFpBodyKind kind = ReachFpBodyKind::None;
    uint64_t leftPaletteMask = 0;
    uint64_t rightPaletteMask = 0;
    if (paletteBodyNodeCount == kReachSpartanFpBodyNodeCount)
    {
        for (size_t i = 0; i < paletteBodyNodeCount; ++i)
            if (paletteBoneMap[i] != kReachSpartanFpBodyBoneMap[i])
                return false;
        kind = ReachFpBodyKind::Spartan;
        leftPaletteMask = kReachSpartanLeftHandPaletteMask;
        rightPaletteMask = kReachSpartanRightHandPaletteMask;
    }
    else
    {
        for (size_t i = 0; i < paletteBodyNodeCount; ++i)
            if (paletteBoneMap[i] != kReachEliteFpBodyBoneMap[i])
                return false;
        kind = ReachFpBodyKind::Elite;
        leftPaletteMask = kReachEliteLeftHandPaletteMask;
        rightPaletteMask = kReachEliteRightHandPaletteMask;
    }

    uint64_t leftSourceMask = 0;
    uint64_t rightSourceMask = 0;
    for (size_t output = 0; output < paletteBodyNodeCount; ++output)
    {
        const uint64_t outputBit = uint64_t{1} << output;
        const uint64_t sourceBit =
            uint64_t{1} << static_cast<uint32_t>(paletteBoneMap[output]);
        if (leftPaletteMask & outputBit)
            leftSourceMask |= sourceBit;
        if (rightPaletteMask & outputBit)
            rightSourceMask |= sourceBit;
    }

    ReachFpBodyLayout layout{};
    layout.kind = kind;
    layout.paletteBodyNodeCount = paletteBodyNodeCount;
    layout.liveSourceNodeCount = liveSourceNodeCount;
    // The official Spartan and Elite animation graphs share this exact
    // first-person arm/camera prefix.
    layout.rightShoulderSource = 6;
    layout.rightElbowSource = 9;
    layout.rightWristSource = 13;
    layout.leftShoulderSource = 5;
    layout.leftElbowSource = 7;
    layout.leftWristSource = 11;
    layout.cameraControlSource = 4;
    layout.rightHandPaletteDescendants = rightPaletteMask;
    layout.leftHandPaletteDescendants = leftPaletteMask;
    layout.rightHandSourceDescendants = rightSourceMask;
    layout.leftHandSourceDescendants = leftSourceMask;
    layout.rightControllerOwnedSourceBranch =
        rightSourceMask | kReachRightControllerOwnedAuxiliarySourceMask;
    layout.leftControllerOwnedSourceBranch =
        leftSourceMask | kReachLeftControllerOwnedAuxiliarySourceMask;
    out = layout;
    return true;
}

inline constexpr bool ReachFpSourceIndexIsHeldObject(
    const ReachFpBodyLayout& layout, int32_t sourceIndex) noexcept
{
    return layout.Valid() && sourceIndex >= 0 &&
        static_cast<size_t>(sourceIndex) >= layout.paletteBodyNodeCount &&
        static_cast<size_t>(sourceIndex) < layout.liveSourceNodeCount;
}

enum class ReachFpPairLayoutDecision : uint8_t
{
    Stock = 0,
    Active,
};

// Layout discovery is deliberately stock-only. A generation-tagged layout may
// enter a frozen stereo pair only after its discovery serial, and invalidation
// takes effect at the next pair boundary. The decision is pair-scoped and has
// no eye input, so left-first and right-first rendering select identically.
inline constexpr ReachFpPairLayoutDecision DecideReachFpPairLayout(
    bool learnedLayoutValid,
    bool invalidateNextPair,
    uint32_t learnedGeneration,
    uint64_t learnedPreparedSerial,
    uint32_t pairGeneration,
    uint64_t pairPreparedSerial) noexcept
{
    return learnedLayoutValid &&
            !invalidateNextPair &&
            learnedGeneration != 0 &&
            learnedGeneration == pairGeneration &&
            learnedPreparedSerial != 0 &&
            pairPreparedSerial > learnedPreparedSerial
        ? ReachFpPairLayoutDecision::Active
        : ReachFpPairLayoutDecision::Stock;
}

enum class ReachFpPaletteAction : uint8_t
{
    PassThroughLive = 0,
    LearnStockOnly,
    ArticulateKnownTransaction,
    RestoreStockAndInvalidate,
};

// H3/ODST reconstruct every final palette associated with a captured source,
// not only the palette that originally identified the skeleton. Reach's first
// palette can therefore learn the exact 47/41-node arms fingerprint, while the
// separately submitted first-person body palette uses that frozen animation
// layout on the next pair. A known body tag with an altered map fails closed.
inline constexpr ReachFpPaletteAction DecideReachFpPaletteAction(
    bool contextCurrent,
    bool frozenLayoutValid,
    bool graphTransformed,
    bool exactSupportedBody,
    bool exactBodyMatchesFrozen,
    bool learnedBodyTagMatches) noexcept
{
    if (!contextCurrent)
        return ReachFpPaletteAction::PassThroughLive;
    if (exactSupportedBody && !frozenLayoutValid)
        return ReachFpPaletteAction::LearnStockOnly;
    if (!frozenLayoutValid)
        return ReachFpPaletteAction::PassThroughLive;
    if ((exactSupportedBody && !exactBodyMatchesFrozen) ||
        (learnedBodyTagMatches && !exactSupportedBody))
        return ReachFpPaletteAction::RestoreStockAndInvalidate;
    return graphTransformed
        ? ReachFpPaletteAction::ArticulateKnownTransaction
        : ReachFpPaletteAction::PassThroughLive;
}

// Retail apply_distortions divides the maximum by the scale at both sites.
// The scale must therefore remain positive even when the maximum is zeroed.
inline constexpr uintptr_t kReachMotionBlurMaxOverScaleDivideRva = 0x00287561;
inline constexpr uintptr_t kReachMotionBlurScaledMaxDivideRva = 0x002875AD;
inline constexpr float kReachMotionBlurMinimumUsableScale = 1.0e-6f;
// Reach's screen-aligned patchy-fog renderer is independently gated inside
// player_view_render. Retail tests bit 0x08 and skips the helper when it is set.
// HREK names the matching resources `_surface_patchy_fog_buffer0/1` and the
// matching parameter block `Patchy Fog Global Parameters`. The exact retail
// player_view body hash pins the test/jump bytes; the preflight additionally
// proves the helper call edge and mapped flag byte before publication.
inline constexpr uintptr_t kReachPatchyFogGateTestRva = 0x0026CC59;
inline constexpr uintptr_t kReachPatchyFogSkipJumpRva = 0x0026CC60;
inline constexpr uintptr_t kReachPatchyFogCallRva = 0x0026CC65;
inline constexpr uintptr_t kReachPatchyFogTargetRva = 0x0026EFEC;
inline constexpr uintptr_t kReachPatchyFogFlagsRva = 0x00CA0240;
inline constexpr uint8_t kReachPatchyFogSkipMask = 0x08;

inline constexpr uint8_t ReachPatchyFogSuppressedFlags(
    uint8_t flags) noexcept
{
    return static_cast<uint8_t>(flags | kReachPatchyFogSkipMask);
}

inline constexpr uint8_t ReachPatchyFogRestoredFlags(
    uint8_t current, uint8_t original) noexcept
{
    return static_cast<uint8_t>(
        (current & static_cast<uint8_t>(~kReachPatchyFogSkipMask)) |
        (original & kReachPatchyFogSkipMask));
}

// Reach's ATMOSPHERIC fog, which is a different system from the screen-aligned
// patchy fog above and stays enabled when patchy fog is skipped.
//
// Official HREK names both, in one place: `bin/debug_menu_init.txt` ships a
// "Fog and Weather" menu whose entries are
//     command "enable render atmosphere fog"  -> `render_atmosphere_fog 1`
//     command "disable render atmosphere fog" -> `render_atmosphere_fog 0`
//     global  "render rain"                   -> `render_rain`
// so atmosphere fog is a debug COMMAND and rain is a debug GLOBAL. That
// distinction is load-bearing. A command carries no value slot, and in the
// pinned retail image `render_atmosphere_fog`'s descriptor (0x00A0FEB8) has
// type word 0 with a "value" pointer of 0x00198F2C - which is inside `.text`.
// Resolving this name through the float debug-var path would hand back CODE,
// so the atmosphere control is bound from its gate instead, never by name.
//
// The command handler is retail 0x001B4CF4 and does exactly one thing:
//     mov  edx, [0x00C17B18]   ; render TLS index
//     mov  rcx, gs:[0x58]
//     mov  r8d, 0x168
//     mov  rcx, [rcx + rdx*8]  ; this thread's render TLS block
//     mov  rdx, [r8 + rcx]     ; block + 0x168 -> the render flags byte
//     or   cl, 4  /  and cl, 0xFB
//     mov  [rdx], cl
// i.e. bit 0x04 of `*(renderTls + 0x168)`; a nonzero argument SETS it.
//
// Its consumer is the atmosphere helper retail 0x0026D5B4, called from
// player_view_render at 0x0026CC54 - the call immediately before the patchy-fog
// gate above. The helper opens with the identical TLS walk and then
//     0x0026D5F3  test byte ptr [rax], 4
//     0x0026D5F6  je   0x0026D645        ; skip the whole atmosphere fog upload
// so a CLEAR bit means "no atmospheric fog", exactly matching the menu labels.
//
// **Polarity is the opposite of the patchy-fog byte**, where a SET bit means
// skip. These are two different structures - one a module global, one reached
// through per-thread TLS - and must never be treated as the same flags word.
inline constexpr uintptr_t kReachAtmosphereFogHelperRva = 0x0026D5B4;
inline constexpr uintptr_t kReachAtmosphereFogHelperCallRva = 0x0026CC54;
inline constexpr uintptr_t kReachAtmosphereFogTlsIndexLoadRva = 0x0026D5DF;
inline constexpr uintptr_t kReachAtmosphereFogTlsIndexRva = 0x00C17B18;
inline constexpr uintptr_t kReachAtmosphereFogSlotLoadRva = 0x0026D5E5;
inline constexpr uintptr_t kReachAtmosphereFogGateTestRva = 0x0026D5F3;
inline constexpr uintptr_t kReachAtmosphereFogSkipJumpRva = 0x0026D5F6;
inline constexpr uintptr_t kReachAtmosphereFogFlagsSlotOffset = 0x168;
inline constexpr uint8_t kReachAtmosphereFogEnableMask = 0x04;
// The same bound the ODST cinematic TLS proof uses; a static TLS slot this far
// out is a decoded-wrong index, not a real one.
inline constexpr uint32_t kReachRenderTlsIndexLimit = 256;

inline constexpr uint8_t ReachAtmosphereFogSuppressedFlags(
    uint8_t flags) noexcept
{
    return static_cast<uint8_t>(
        flags & static_cast<uint8_t>(~kReachAtmosphereFogEnableMask));
}

inline constexpr uint8_t ReachAtmosphereFogRestoredFlags(
    uint8_t current) noexcept
{
    return static_cast<uint8_t>(current | kReachAtmosphereFogEnableMask);
}

// Reach's rain master switch. `render_rain` is a type-5 (boolean) debug var:
// descriptor 0x00B40FF0, name string 0x009EB110, value BYTE at 0x00B4444C. It
// is resolved BY NAME at runtime; the RVA below only cross-checks that result
// against the pinned image, exactly as the motion-blur slots do.
//
// The value is one byte and 0x00B4444D holds a different boolean, so this must
// be written a byte at a time. A four-byte float store would clobber the
// neighbour.
//
// It has exactly five readers in the pinned image, every one of them a
// `cmp byte ptr [render_rain], 0` gate:
//     0x00259A1C, 0x0025E339, 0x0026CC92, 0x0026E822, 0x0026E978.
// 0x0026CC92 is the one inside player_view_render, and the call it skips is
// 0x00288D60 - kReachRainParticleRenderRva, the same renderer the view-
// decoupling detour below wraps. Clearing the byte removes the rain draw
// outright, and that detour simply stops being reached.
//
// Every other rain sub-toggle HREK's menu lists (`render_rain_particles`,
// `render_light_volume_rain_particles`, `render_light_volume_rain_sheets`)
// resolves to a NULL backing global in retail and gives no leverage; measured
// 2026-07-27 and re-measured here. `render_rain` is the only live one.
inline constexpr uint64_t kReachDebugVarTypeBoolean = 5;
inline constexpr uintptr_t kReachRenderRainValueRva = 0x00B4444C;
inline constexpr uintptr_t kReachRenderRainGateRva = 0x0026CC92;

// Official HREK model_definitions.cpp exposes the engine-wide
// "If sky attaches to camera" model flag (bit 6) and "Sky parallax percent".
// Its model-to-object-property conversion is uniquely homologous in the pinned
// retail image: it reads model +0x15C, quantizes model +0x1B4, then stores the
// result in the generic object property byte at +0x0B. This headset candidate
// tests whether that non-zero, mono-camera-authored parallax is the source of
// the reported binocular conflict. Neutralizing only the quantization result
// preserves the semantic camera-attachment class while requesting its documented
// zero-parallax endpoint; ordinary world skies and all non-attached models never
// take this path.
inline constexpr uintptr_t kReachSkyParallaxSignatureRva = 0x0024B5C4;
inline constexpr uintptr_t kReachSkyParallaxQuantizeRva = 0x0024B5DB;
inline constexpr uintptr_t kReachModelFlagsOffset = 0x015C;
inline constexpr uintptr_t kReachModelSkyParallaxOffset = 0x01B4;
inline constexpr uintptr_t kReachObjectSkyParallaxByteOffset = 0x000B;
inline constexpr uint32_t kReachModelAttachToCameraMask = 0x00000040u;
inline constexpr std::array<uint8_t, 4> kReachSkyParallaxQuantizeOriginal{
    0xF3, 0x0F, 0x2C, 0xC0}; // cvttss2si eax,xmm0
inline constexpr std::array<uint8_t, 4> kReachSkyParallaxQuantizeNeutral{
    0x31, 0xC0, 0x90, 0x90}; // xor eax,eax; nop; nop

// Halo's authored physical scale is one world unit per ten feet. Reach must use
// the same exact conversion for every tracked position so room-scale motion and
// runtime IPD cannot disagree. This is title-local: accepted Halo 3 calibration
// remains independently adjustable through g_worldScale.
inline constexpr float kReachWorldUnitsPerMeter = 1.0f / 3.048f;
inline constexpr uintptr_t kReachPlayerViewCameraStateOffset = 0x03B0;
inline constexpr uintptr_t kReachPlayerViewCurrentMatricesOffset = 0x0490;
inline constexpr uintptr_t kReachPlayerViewPreviousMatricesOffset = 0x0760;
inline constexpr uintptr_t kReachLastWindowFlagOffset = 0x0A30;
inline constexpr size_t kReachPlayerViewCameraStateSize = 0x00C8;
inline constexpr size_t kReachPlayerViewMatrixBlockSize = 0x02D0;
// Zeroed projection-offset pair inside the camera-state envelope; passed as the
// projection/matrix builder's fifth argument.
inline constexpr uintptr_t kReachPlayerViewProjectionOffsetPairOffset = 0x0470;
// Rasterizer-workspace sub-block layout (REACH-SIGNATURE-EVIDENCE.md, the 0x2B0
// render-scope snapshot table): primary compact +0x000/0x90, primary derived
// +0x090/0xC4, secondary compact +0x154/0x90, secondary derived +0x1E4/0xC4.
inline constexpr uintptr_t kReachCompactCameraSize = 0x0090;
inline constexpr uintptr_t kReachPrimaryDerivedOffset = 0x0090;
inline constexpr size_t kReachDerivedBlockSize = 0x00C4;
inline constexpr uintptr_t kReachSecondaryCompactOffset = 0x0154;
inline constexpr uintptr_t kReachSecondaryDerivedOffset = 0x01E4;
// The four camera blocks end immediately before the engine-owned camera-stack
// callback at +0x2A8. This is the exact bounded unit an outer camera owner may
// snapshot without including that callback.
inline constexpr size_t kReachCameraPairDataSize = 0x02A8;
static_assert(
    kReachSecondaryDerivedOffset + kReachDerivedBlockSize ==
    kReachCameraPairDataSize);
static_assert(
    kReachFpCameraWorkspaceCallbackOffset == kReachCameraPairDataSize);
static_assert(
    kReachFpCameraWorkspaceCallbackOffset + sizeof(uintptr_t) ==
    kReachRenderScopeSnapshotSize);
static_assert(kReachFpCameraWorkspaceCallbackRva <= kReachFpCameraWorkspaceRva);
static_assert(kReachFpCameraViewRva <= kReachFpCameraWorkspaceRva);

// Select only the exact nested workspace/view/callback combination proven by
// all three retail FP wrappers. A stock, screenshot, stale-title, outer-camera,
// or unexpected nested call fails open without touching either camera pair.
inline uintptr_t SelectReachFpCameraNestedWorkspace(
    uintptr_t moduleBase, size_t moduleSize, uintptr_t stackTop,
    uintptr_t workspaceCallback, uintptr_t fpView) noexcept
{
    if (!moduleBase || moduleSize != kReachRetailImageSize ||
        moduleBase >
            std::numeric_limits<uintptr_t>::max() -
                kReachFpCameraWorkspaceRva)
    {
        return 0;
    }
    const uintptr_t expectedWorkspace =
        moduleBase + kReachFpCameraWorkspaceRva;
    return stackTop == expectedWorkspace &&
            workspaceCallback ==
                moduleBase + kReachFpCameraWorkspaceCallbackRva &&
            fpView == moduleBase + kReachFpCameraViewRva
        ? expectedWorkspace
        : 0;
}
// Exact retail evidence for the pre-inner visibility consumer. The first call
// resolves the camera cluster from workspace+0x154; the second builds the
// visibility collection from workspace+0x154 and workspace+0x1E4.
inline constexpr uintptr_t kReachVisibilityClusterLookupCallRva = 0x000C3320;
inline constexpr uintptr_t kReachVisibilityClusterLookupTargetRva = 0x00273458;
inline constexpr uintptr_t kReachVisibilitySecondaryCompactLeaRva = 0x00273468;
inline constexpr uintptr_t kReachVisibilityBuildCallRva = 0x000C335C;
inline constexpr uintptr_t kReachVisibilityBuildTargetRva = 0x0027F408;
inline constexpr uintptr_t kReachVisibilitySecondaryDerivedLeaRva = 0x000C3339;
inline constexpr uintptr_t kReachVisibilitySecondaryCompactAddressRva =
    kReachDefaultWorkspaceRva + kReachSecondaryCompactOffset;
inline constexpr uintptr_t kReachVisibilitySecondaryDerivedAddressRva =
    kReachDefaultWorkspaceRva + kReachSecondaryDerivedOffset;
// Fields the projection/matrix builder consumes: the derived projection matrix
// at derived+0x78 and the compact render bounds at compact+0x4C.
inline constexpr uintptr_t kReachDerivedProjectionOffset = 0x0078;
inline constexpr uintptr_t kReachCompactRenderBoundsOffset = 0x004C;
inline constexpr uint64_t kReachRenderFreshnessMaxGapMs = 500;
inline constexpr uint64_t kReachRenderSafetyIntervalMs = 1000;

struct ReachSymmetricFovCover
{
    bool valid = false;
    float verticalFov = 0.0f;
    float requiredHalfHorizontal = 0.0f;
    float requiredHalfVertical = 0.0f;
};

struct ReachEyeCullFrustum
{
    float angleLeft = 0.0f;
    float angleRight = 0.0f;
    float angleUp = 0.0f;
    float angleDown = 0.0f;
    // OpenXR x/y/z/w orientation of this eye relative to the stereo midpoint.
    std::array<float, 4> relativeOrientation{0.0f, 0.0f, 0.0f, 1.0f};
};

// Converts the symmetric projection Reach actually rasterizes into the eye
// frustum used by the outer binocular cull. The raw asymmetric OpenXR FOV is
// intentionally not reused here: widening either axis for Reach's fixed-aspect
// projection widens the real raster corners that visibility must retain.
inline bool BuildReachSymmetricRasterCullFrustum(
    const ReachSymmetricFovCover& rasterCover,
    const std::array<float, 4>& relativeOrientation,
    uint32_t renderWidth, uint32_t renderHeight,
    ReachEyeCullFrustum& result) noexcept
{
    result = {};
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kMinimumQuaternionLengthSquared = 1.0e-12;
    if (!rasterCover.valid ||
        !std::isfinite(rasterCover.verticalFov) ||
        !std::isfinite(rasterCover.requiredHalfHorizontal) ||
        !std::isfinite(rasterCover.requiredHalfVertical) ||
        rasterCover.verticalFov <= 0.0001f ||
        rasterCover.verticalFov >= kPi ||
        rasterCover.requiredHalfHorizontal <= 0.0f ||
        rasterCover.requiredHalfVertical <= 0.0f ||
        !renderWidth || !renderHeight)
    {
        return false;
    }

    double quaternionLengthSquared = 0.0;
    for (float component : relativeOrientation)
    {
        if (!std::isfinite(component))
            return false;
        quaternionLengthSquared +=
            static_cast<double>(component) * component;
    }
    if (!std::isfinite(quaternionLengthSquared) ||
        quaternionLengthSquared <= kMinimumQuaternionLengthSquared)
    {
        return false;
    }

    const double aspect =
        static_cast<double>(renderWidth) / static_cast<double>(renderHeight);
    const double halfVertical =
        static_cast<double>(rasterCover.verticalFov) * 0.5;
    const double verticalTangent = std::tan(halfVertical);
    const double halfHorizontal =
        std::atan(verticalTangent * aspect);
    if (!std::isfinite(aspect) || aspect <= 0.0 ||
        !std::isfinite(verticalTangent) || verticalTangent <= 0.0 ||
        !std::isfinite(halfHorizontal) || halfHorizontal <= 0.0 ||
        halfHorizontal >= kPi * 0.5)
    {
        return false;
    }

    result.angleLeft = -static_cast<float>(halfHorizontal);
    result.angleRight = static_cast<float>(halfHorizontal);
    result.angleUp = static_cast<float>(halfVertical);
    result.angleDown = -static_cast<float>(halfVertical);
    result.relativeOrientation = relativeOrientation;
    return true;
}

// Reach's compact camera stores one vertical FOV and derives its horizontal
// scale from render_pixel_bounds. Choose the smallest symmetric frustum that
// covers all four OpenXR angles at that exact render aspect. This is the same
// conservative cover used by Halo 3/ODST, expressed in Reach's proven layout.
inline ReachSymmetricFovCover SelectReachSymmetricFovCover(
    float angleLeft, float angleRight, float angleUp, float angleDown,
    uint32_t renderWidth, uint32_t renderHeight) noexcept
{
    ReachSymmetricFovCover result{};
    if (!std::isfinite(angleLeft) || !std::isfinite(angleRight) ||
        !std::isfinite(angleUp) || !std::isfinite(angleDown) ||
        angleLeft >= 0.0f || angleRight <= 0.0f ||
        angleUp <= 0.0f || angleDown >= 0.0f ||
        !renderWidth || !renderHeight)
    {
        return result;
    }

    const float requiredHalfHorizontal =
        -angleLeft > angleRight ? -angleLeft : angleRight;
    const float requiredHalfVertical =
        angleUp > -angleDown ? angleUp : -angleDown;
    const float horizontalTangent = std::tan(requiredHalfHorizontal);
    const float verticalTangent = std::tan(requiredHalfVertical);
    const float aspect =
        static_cast<float>(renderWidth) / static_cast<float>(renderHeight);
    if (!std::isfinite(horizontalTangent) ||
        !std::isfinite(verticalTangent) || !std::isfinite(aspect) ||
        horizontalTangent <= 0.0f || verticalTangent <= 0.0f ||
        aspect <= 0.0f)
    {
        return result;
    }

    const float horizontalCoverageTangent = horizontalTangent / aspect;
    const float selectedVerticalTangent =
        verticalTangent > horizontalCoverageTangent
            ? verticalTangent : horizontalCoverageTangent;
    const float verticalFov = 2.0f * std::atan(selectedVerticalTangent);
    if (!std::isfinite(verticalFov) ||
        verticalFov <= 0.0001f || verticalFov >= 3.1414928436f)
    {
        return result;
    }

    result.valid = true;
    result.verticalFov = verticalFov;
    result.requiredHalfHorizontal = requiredHalfHorizontal;
    result.requiredHalfVertical = requiredHalfVertical;
    return result;
}

// Builds one head-centre symmetric frustum that contains both complete OpenXR
// eye frusta. Every eye corner is rotated into midpoint space by its normalized
// relative-eye quaternion before the angular envelope is measured, so canted
// and asymmetric views cannot be clipped by an identity-orientation shortcut.
inline ReachSymmetricFovCover SelectReachStereoCullFovCover(
    const std::array<ReachEyeCullFrustum, 2>& eyes,
    uint32_t renderWidth, uint32_t renderHeight) noexcept
{
    ReachSymmetricFovCover result{};
    if (!renderWidth || !renderHeight)
        return result;

    const double aspect =
        static_cast<double>(renderWidth) / static_cast<double>(renderHeight);
    if (!std::isfinite(aspect) || aspect <= 0.0)
        return result;

    constexpr double kHalfPi = 1.57079632679489661923;
    constexpr double kMinimumQuaternionLengthSquared = 1.0e-12;
    double maximumHorizontalTangent = 0.0;
    double maximumVerticalTangent = 0.0;

    for (const ReachEyeCullFrustum& eye : eyes)
    {
        if (!std::isfinite(eye.angleLeft) ||
            !std::isfinite(eye.angleRight) ||
            !std::isfinite(eye.angleUp) ||
            !std::isfinite(eye.angleDown) ||
            eye.angleLeft >= 0.0f || eye.angleRight <= 0.0f ||
            eye.angleUp <= 0.0f || eye.angleDown >= 0.0f ||
            eye.angleLeft <= -kHalfPi || eye.angleRight >= kHalfPi ||
            eye.angleUp >= kHalfPi || eye.angleDown <= -kHalfPi)
        {
            return result;
        }

        double quaternion[4]{};
        double quaternionLengthSquared = 0.0;
        for (size_t component = 0; component < 4; ++component)
        {
            if (!std::isfinite(eye.relativeOrientation[component]))
                return result;
            quaternion[component] = eye.relativeOrientation[component];
            quaternionLengthSquared +=
                quaternion[component] * quaternion[component];
        }
        if (!std::isfinite(quaternionLengthSquared) ||
            quaternionLengthSquared <= kMinimumQuaternionLengthSquared)
        {
            return result;
        }
        const double inverseQuaternionLength =
            1.0 / std::sqrt(quaternionLengthSquared);
        for (double& component : quaternion)
            component *= inverseQuaternionLength;

        const double horizontalAngles[2]{
            eye.angleLeft, eye.angleRight};
        const double verticalAngles[2]{
            eye.angleDown, eye.angleUp};
        for (double horizontalAngle : horizontalAngles)
        {
            const double sourceX = std::tan(horizontalAngle);
            if (!std::isfinite(sourceX))
                return result;
            for (double verticalAngle : verticalAngles)
            {
                const double sourceY = std::tan(verticalAngle);
                if (!std::isfinite(sourceY))
                    return result;

                // Rotate (sourceX, sourceY, -1) using
                // v' = v + 2*w*cross(q.xyz,v) +
                //      2*cross(q.xyz,cross(q.xyz,v)).
                const double crossX =
                    quaternion[1] * -1.0 - quaternion[2] * sourceY;
                const double crossY =
                    quaternion[2] * sourceX + quaternion[0];
                const double crossZ =
                    quaternion[0] * sourceY -
                    quaternion[1] * sourceX;
                const double twiceCrossX = 2.0 * crossX;
                const double twiceCrossY = 2.0 * crossY;
                const double twiceCrossZ = 2.0 * crossZ;
                const double rotatedX = sourceX +
                    quaternion[3] * twiceCrossX +
                    quaternion[1] * twiceCrossZ -
                    quaternion[2] * twiceCrossY;
                const double rotatedY = sourceY +
                    quaternion[3] * twiceCrossY +
                    quaternion[2] * twiceCrossX -
                    quaternion[0] * twiceCrossZ;
                const double rotatedZ = -1.0 +
                    quaternion[3] * twiceCrossZ +
                    quaternion[0] * twiceCrossY -
                    quaternion[1] * twiceCrossX;
                const double forwardDepth = -rotatedZ;
                if (!std::isfinite(rotatedX) ||
                    !std::isfinite(rotatedY) ||
                    !std::isfinite(forwardDepth) ||
                    forwardDepth <= 0.0)
                {
                    return result;
                }

                const double horizontalTangent =
                    std::fabs(rotatedX) / forwardDepth;
                const double verticalTangent =
                    std::fabs(rotatedY) / forwardDepth;
                if (!std::isfinite(horizontalTangent) ||
                    !std::isfinite(verticalTangent))
                {
                    return result;
                }
                if (horizontalTangent > maximumHorizontalTangent)
                    maximumHorizontalTangent = horizontalTangent;
                if (verticalTangent > maximumVerticalTangent)
                    maximumVerticalTangent = verticalTangent;
            }
        }
    }

    const double selectedVerticalTangent =
        maximumVerticalTangent >
                maximumHorizontalTangent / aspect
            ? maximumVerticalTangent
            : maximumHorizontalTangent / aspect;
    const double requiredHalfHorizontal =
        std::atan(maximumHorizontalTangent);
    const double requiredHalfVertical =
        std::atan(maximumVerticalTangent);
    const double verticalFov = 2.0 * std::atan(selectedVerticalTangent);
    if (!std::isfinite(requiredHalfHorizontal) ||
        !std::isfinite(requiredHalfVertical) ||
        !std::isfinite(verticalFov) ||
        requiredHalfHorizontal <= 0.0 ||
        requiredHalfVertical <= 0.0 ||
        verticalFov <= 0.0001 ||
        verticalFov >= 3.1414928436)
    {
        return result;
    }

    result.valid = true;
    result.verticalFov = static_cast<float>(verticalFov);
    result.requiredHalfHorizontal =
        static_cast<float>(requiredHalfHorizontal);
    result.requiredHalfVertical =
        static_cast<float>(requiredHalfVertical);
    return result;
}

struct ReachProjectionHalfFovs
{
    bool valid = false;
    float horizontal = 0.0f;
    float vertical = 0.0f;
};

// The proven Reach derived block holds its projection matrix at +0x78. Decode
// the actual symmetric raster scales rather than telling OpenXR what we hoped
// the engine built. Invalid or under-covering matrices fail the eye transaction.
inline ReachProjectionHalfFovs DecodeReachProjectionHalfFovs(
    float projection00, float projection11) noexcept
{
    ReachProjectionHalfFovs result{};
    const float horizontalScale = std::fabs(projection00);
    const float verticalScale = std::fabs(projection11);
    if (!std::isfinite(horizontalScale) || !std::isfinite(verticalScale) ||
        horizontalScale <= 0.0001f || verticalScale <= 0.0001f)
    {
        return result;
    }

    const float horizontal = std::atan(1.0f / horizontalScale);
    const float vertical = std::atan(1.0f / verticalScale);
    if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
        horizontal <= 0.0f || vertical <= 0.0f)
    {
        return result;
    }
    result.valid = true;
    result.horizontal = horizontal;
    result.vertical = vertical;
    return result;
}

inline bool ReachProjectionCoversOpenXr(
    const ReachProjectionHalfFovs& projection,
    const ReachSymmetricFovCover& requested,
    float toleranceRadians = 0.001f) noexcept
{
    return projection.valid && requested.valid &&
        std::isfinite(toleranceRadians) && toleranceRadians >= 0.0f &&
        projection.horizontal + toleranceRadians >=
            requested.requiredHalfHorizontal &&
        projection.vertical + toleranceRadians >=
            requested.requiredHalfVertical;
}

inline constexpr size_t kReachMainRenderViewBodySize = 515;
inline constexpr char kReachMainRenderViewBodySha256[] =
    "95DF3EFFF9AC6EE29887D1272CCA8D7BF3E58F87041BAD8032107825B733FE89";
inline constexpr size_t kReachPlayerViewRenderBodySize = 2314;
inline constexpr char kReachPlayerViewRenderBodySha256[] =
    "2628D1189621EACED7C95A1F295815D70E7783054F1C3CBA46799F838CC33C60";

inline constexpr std::array<uint8_t, 32> kReachMainRenderViewAob{
    0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x80,
    0x00, 0x00, 0x00, 0x0F, 0x29, 0x74, 0x24, 0x70,
    0x48, 0x8B, 0x05, 0x05, 0x6E, 0xA3, 0x00, 0x48,
    0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x68, 0x41,
};

inline constexpr std::array<uint8_t, 69> kReachPlayerViewRenderAob{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
    0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0xA8, 0x28, 0xFF, 0xFF, 0xFF, 0x48,
    0x81, 0xEC, 0xB0, 0x01, 0x00, 0x00, 0x0F, 0x29,
    0x70, 0xC8, 0x0F, 0x29, 0x78, 0xB8, 0x48, 0x8B,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4,
    0x48, 0x89, 0x85, 0x80, 0x00, 0x00, 0x00, 0x8B,
    0x81, 0xA4, 0x03, 0x00, 0x00,
};

inline constexpr std::array<uint8_t, 28> kReachCameraStackCallbackAob{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x4D, 0x8B, 0xCA, 0x4C,
    0x89, 0x54, 0x24, 0x30,
};

inline constexpr auto kReachCameraStackCallbackAobMask = []
{
    std::array<uint8_t, kReachCameraStackCallbackAob.size()> mask{};
    for (auto& byte : mask)
        byte = 0xFF;
    for (size_t index = 9; index <= 12; ++index)
        mask[index] = 0;
    for (size_t index = 16; index <= 19; ++index)
        mask[index] = 0;
    return mask;
}();

inline constexpr auto kReachPlayerViewRenderAobMask = []
{
    std::array<uint8_t, kReachPlayerViewRenderAob.size()> mask{};
    for (auto& byte : mask)
        byte = 0xFF;
    for (size_t index = 49; index <= 52; ++index)
        mask[index] = 0;
    return mask;
}();

inline constexpr std::array<uint8_t, 25> kReachFrustumHelperAob{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x44, 0x0F,
    0xBF, 0x49, 0x62, 0x4C, 0x8B, 0xD9, 0x4C, 0x8B,
    0x41, 0x38, 0x48, 0x8B, 0xDA, 0x0F, 0xBF, 0x51,
    0x50,
};

inline constexpr std::array<uint8_t, 34> kReachFpCameraRebuildAob{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x4C,
    0x8B, 0x41, 0x08, 0x48, 0x8D, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x74,
    0x24, 0x30,
};

inline constexpr auto kReachFpCameraRebuildAobMask = []
{
    std::array<uint8_t, kReachFpCameraRebuildAob.size()> mask{};
    for (auto& byte : mask)
        byte = 0xFF;
    for (size_t index = 22; index <= 25; ++index)
        mask[index] = 0;
    return mask;
}();

inline constexpr std::array<uint8_t, 37> kReachFpCameraUploadAob{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x55,
    0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xC0,
    0x00, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xE8, 0x4C,
    0x8D, 0x45, 0xF7, 0x0F, 0x29, 0x78, 0xD8, 0x48,
    0x8B, 0xD9, 0x48, 0x8B, 0xC2,
};

inline size_t CountReachMaskedPattern(
    const uint8_t* data, size_t dataSize, const uint8_t* pattern,
    const uint8_t* mask, size_t patternSize) noexcept
{
    if (!data || !pattern || !mask || !patternSize || dataSize < patternSize)
        return 0;

    size_t count = 0;
    for (size_t offset = 0; offset <= dataSize - patternSize; ++offset)
    {
        size_t index = 0;
        for (; index < patternSize; ++index)
        {
            if (mask[index] && data[offset + index] != pattern[index])
                break;
        }
        if (index == patternSize)
            ++count;
    }
    return count;
}

struct ReachRenderCandidateProof
{
    bool retailIdentity = false;
    uint32_t mainRenderViewMatchCount = 0;
    bool mainRenderViewAtExpectedRva = false;
    bool mainRenderViewBodyHash = false;
    uint32_t playerViewRenderMatchCount = 0;
    bool playerViewRenderAtExpectedRva = false;
    bool playerViewRenderBodyHash = false;
    uint32_t cameraStackCallbackMatchCount = 0;
    bool cameraStackCallbackAtExpectedRva = false;
    bool cameraStackCallbackBodyHash = false;
    uint32_t frustumHelperMatchCount = 0;
    bool frustumHelperAtExpectedRva = false;
    bool frustumHelperExecutableRange = false;
    uint32_t fpCameraRebuildMatchCount = 0;
    bool fpCameraRebuildAtExpectedRva = false;
    bool fpCameraRebuildBodyHash = false;
    uint32_t fpCameraUploadMatchCount = 0;
    bool fpCameraUploadAtExpectedRva = false;
    bool fpCameraUploadBodyHash = false;
    bool fpCameraWrapperBodyHashes = false;
    bool exactFpCameraFlowEdges = false;
    bool exactOuterCallerEdges = false;
    bool exactInnerCallerEdge = false;
    bool fixedDataRanges = false;
};

inline bool ReachRenderCandidateProofComplete(
    const ReachRenderCandidateProof& proof) noexcept
{
    return proof.retailIdentity &&
        proof.mainRenderViewMatchCount == 1 &&
        proof.mainRenderViewAtExpectedRva &&
        proof.mainRenderViewBodyHash &&
        proof.playerViewRenderMatchCount == 1 &&
        proof.playerViewRenderAtExpectedRva &&
        proof.playerViewRenderBodyHash &&
        proof.cameraStackCallbackMatchCount == 1 &&
        proof.cameraStackCallbackAtExpectedRva &&
        proof.cameraStackCallbackBodyHash &&
        proof.frustumHelperMatchCount == 1 &&
        proof.frustumHelperAtExpectedRva &&
        proof.frustumHelperExecutableRange &&
        proof.fpCameraRebuildMatchCount == 1 &&
        proof.fpCameraRebuildAtExpectedRva &&
        proof.fpCameraRebuildBodyHash &&
        proof.fpCameraUploadMatchCount == 1 &&
        proof.fpCameraUploadAtExpectedRva &&
        proof.fpCameraUploadBodyHash &&
        proof.fpCameraWrapperBodyHashes &&
        proof.exactFpCameraFlowEdges &&
        proof.exactOuterCallerEdges &&
        proof.exactInnerCallerEdge &&
        proof.fixedDataRanges;
}

struct ReachModuleEpoch
{
    uintptr_t moduleBase = 0;
    uint32_t generation = 0;
};

inline bool ReachModuleEpochValid(const ReachModuleEpoch& epoch) noexcept
{
    return epoch.moduleBase != 0 && epoch.generation != 0;
}

inline bool ReachSameModuleEpoch(
    const ReachModuleEpoch& left, const ReachModuleEpoch& right) noexcept
{
    return ReachModuleEpochValid(left) &&
        left.moduleBase == right.moduleBase &&
        left.generation == right.generation;
}

class ReachPreflightPublication;

class ReachPreflightToken
{
public:
    ReachPreflightToken() noexcept = default;

    bool Complete() const noexcept { return m_complete; }
    ReachModuleEpoch Epoch() const noexcept { return m_epoch; }
    uint64_t PublicationNonce() const noexcept
    {
        return m_publicationNonce;
    }
    bool IsCurrent() const noexcept;

private:
    ReachPreflightToken(
        const ReachPreflightPublication* publication,
        const ReachModuleEpoch& epoch,
        uint64_t publicationNonce) noexcept
        : m_publication(publication),
          m_epoch(epoch),
          m_publicationNonce(publicationNonce),
          m_complete(true)
    {
    }

    const ReachPreflightPublication* m_publication = nullptr;
    ReachModuleEpoch m_epoch{};
    uint64_t m_publicationNonce = 0;
    bool m_complete = false;

    friend class ReachPreflightPublication;
};

// Single-writer, multi-reader publication capability for a completed loaded
// image preflight. Every successful publication receives a strictly newer
// nonce. Invalidation clears the live nonce before changing the epoch, so a
// copied token becomes unusable before the next publication can be observed.
class ReachPreflightPublication
{
public:
    ReachPreflightPublication() noexcept = default;
    ReachPreflightPublication(const ReachPreflightPublication&) = delete;
    ReachPreflightPublication& operator=(
        const ReachPreflightPublication&) = delete;

    bool Publish(
        const ReachModuleEpoch& epoch,
        const ReachRenderCandidateProof& proof) noexcept
    {
        Invalidate();
        if (!ReachModuleEpochValid(epoch) ||
            !ReachRenderCandidateProofComplete(proof))
        {
            return false;
        }

        const uint64_t nonce = NextNonce();
        if (!nonce)
            return false;
        m_moduleBase.store(epoch.moduleBase, std::memory_order_relaxed);
        m_generation.store(epoch.generation, std::memory_order_relaxed);
        m_currentNonce.store(nonce, std::memory_order_release);
        return true;
    }

    void Invalidate() noexcept
    {
        m_currentNonce.store(0, std::memory_order_release);
        m_moduleBase.store(0, std::memory_order_relaxed);
        m_generation.store(0, std::memory_order_relaxed);
    }

    ReachPreflightToken Get(
        const ReachModuleEpoch& epoch) const noexcept
    {
        if (!ReachModuleEpochValid(epoch))
            return {};
        for (unsigned attempt = 0; attempt < 4; ++attempt)
        {
            const uint64_t before =
                m_currentNonce.load(std::memory_order_acquire);
            if (!before)
                return {};
            const uintptr_t moduleBase =
                m_moduleBase.load(std::memory_order_relaxed);
            const uint32_t generation =
                m_generation.load(std::memory_order_relaxed);
            const uint64_t after =
                m_currentNonce.load(std::memory_order_acquire);
            if (before != after)
                continue;
            if (moduleBase != epoch.moduleBase ||
                generation != epoch.generation)
            {
                return {};
            }
            return ReachPreflightToken(this, epoch, before);
        }
        return {};
    }

    bool IsCurrent(const ReachPreflightToken& token) const noexcept
    {
        if (!token.Complete() || token.m_publication != this ||
            !token.m_publicationNonce)
        {
            return false;
        }
        for (unsigned attempt = 0; attempt < 4; ++attempt)
        {
            const uint64_t before =
                m_currentNonce.load(std::memory_order_acquire);
            if (!before || before != token.m_publicationNonce)
                return false;
            const uintptr_t moduleBase =
                m_moduleBase.load(std::memory_order_relaxed);
            const uint32_t generation =
                m_generation.load(std::memory_order_relaxed);
            const uint64_t after =
                m_currentNonce.load(std::memory_order_acquire);
            if (before != after)
                continue;
            return moduleBase == token.m_epoch.moduleBase &&
                generation == token.m_epoch.generation;
        }
        return false;
    }

    bool HasCurrent() const noexcept
    {
        return m_currentNonce.load(std::memory_order_acquire) != 0;
    }

    uint64_t LastPublicationNonce() const noexcept
    {
        return m_nonceCounter.load(std::memory_order_relaxed);
    }

private:
    uint64_t NextNonce() noexcept
    {
        uint64_t previous =
            m_nonceCounter.load(std::memory_order_relaxed);
        for (;;)
        {
            if (previous == std::numeric_limits<uint64_t>::max())
                return 0;
            if (m_nonceCounter.compare_exchange_weak(
                    previous, previous + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
            {
                return previous + 1;
            }
        }
    }

    std::atomic<uint64_t> m_nonceCounter{0};
    std::atomic<uint64_t> m_currentNonce{0};
    std::atomic<uintptr_t> m_moduleBase{0};
    std::atomic<uint32_t> m_generation{0};
};

inline bool ReachPreflightToken::IsCurrent() const noexcept
{
    return m_publication && m_publication->IsCurrent(*this);
}

inline bool IsPreflightCurrent(
    const ReachPreflightToken& token) noexcept
{
    return token.IsCurrent();
}

class ReachFreshCameraToken
{
public:
    ReachFreshCameraToken() noexcept = default;

    bool Stable() const noexcept { return m_stable; }
    ReachModuleEpoch Epoch() const noexcept { return m_epoch; }
    uint64_t PreparedFrameSerial() const noexcept
    {
        return m_preparedFrameSerial;
    }
    uint64_t Nonce() const noexcept { return m_nonce; }
    uint64_t ObservedAtMs() const noexcept { return m_observedAtMs; }

private:
    ReachFreshCameraToken(
        const ReachModuleEpoch& epoch, uint64_t preparedFrameSerial,
        uint64_t nonce, uint64_t observedAtMs) noexcept
        : m_epoch(epoch),
          m_preparedFrameSerial(preparedFrameSerial),
          m_nonce(nonce),
          m_observedAtMs(observedAtMs),
          m_stable(true)
    {
    }

    ReachModuleEpoch m_epoch{};
    uint64_t m_preparedFrameSerial = 0;
    uint64_t m_nonce = 0;
    uint64_t m_observedAtMs = 0;
    bool m_stable = false;

    friend class ReachRenderFreshnessGate;
};

class ReachPreparedFrameToken
{
public:
    ReachPreparedFrameToken() noexcept = default;

    static ReachPreparedFrameToken Create(
        const ReachModuleEpoch& epoch, uint64_t serial,
        bool ready) noexcept
    {
        return ReachModuleEpochValid(epoch) && serial && ready
            ? ReachPreparedFrameToken(epoch, serial)
            : ReachPreparedFrameToken{};
    }

    bool Ready() const noexcept { return m_ready; }
    ReachModuleEpoch Epoch() const noexcept { return m_epoch; }
    uint64_t Serial() const noexcept { return m_serial; }

private:
    ReachPreparedFrameToken(
        const ReachModuleEpoch& epoch, uint64_t serial) noexcept
        : m_epoch(epoch), m_serial(serial), m_ready(true)
    {
    }

    ReachModuleEpoch m_epoch{};
    uint64_t m_serial = 0;
    bool m_ready = false;
};

class ReachDirectCopyToken
{
public:
    ReachDirectCopyToken() noexcept = default;

    bool Ready() const noexcept { return m_ready; }
    ReachModuleEpoch Epoch() const noexcept { return m_epoch; }
    uint64_t PreparedFrameSerial() const noexcept
    {
        return m_preparedFrameSerial;
    }
    uint64_t ResourceRevision() const noexcept
    {
        return m_resourceRevision;
    }
    uint64_t Nonce() const noexcept { return m_nonce; }

private:
    ReachDirectCopyToken(
        const ReachModuleEpoch& epoch,
        uint64_t preparedFrameSerial, uint64_t resourceRevision,
        uint64_t nonce) noexcept
        : m_epoch(epoch),
          m_preparedFrameSerial(preparedFrameSerial),
          m_resourceRevision(resourceRevision),
          m_nonce(nonce),
          m_ready(true)
    {
    }

    ReachModuleEpoch m_epoch{};
    uint64_t m_preparedFrameSerial = 0;
    uint64_t m_resourceRevision = 0;
    uint64_t m_nonce = 0;
    bool m_ready = false;

    friend class ReachDirectCopyGate;
};

struct ReachCopyShape
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    uint32_t arraySize = 0;
    uint32_t format = 0;
    uint32_t sampleCount = 0;
    uint32_t sampleQuality = 0;
};

inline bool ReachSameCopyShape(
    const ReachCopyShape& left, const ReachCopyShape& right) noexcept
{
    return left.width == right.width &&
        left.height == right.height &&
        left.mipLevels == right.mipLevels &&
        left.arraySize == right.arraySize &&
        left.format == right.format &&
        left.sampleCount == right.sampleCount &&
        left.sampleQuality == right.sampleQuality;
}

inline bool ReachDisplayCopyShapeValid(
    const ReachCopyShape& shape) noexcept
{
    return shape.width != 0 && shape.height != 0 &&
        shape.mipLevels == 1 && shape.arraySize == 1 &&
        shape.format == kReachDisplayFormatR8G8B8A8Unorm &&
        shape.sampleCount == 1 && shape.sampleQuality == 0;
}

struct ReachDisplayContinuity
{
    ReachModuleEpoch epoch{};
    uint64_t resourceRevision = 0;
    uint64_t lifecycleSerial = 0;
    uintptr_t swapchainIdentity = 0;
    uintptr_t buffer0Identity = 0;
    uintptr_t surfaceArrayIdentity = 0;
    uintptr_t record0RtvIdentity = 0;
    uintptr_t record0SrvIdentity = 0;
    uintptr_t selectedRtvIdentity = 0;
    uintptr_t deviceIdentity = 0;
    uintptr_t immediateContextIdentity = 0;
    uintptr_t eyeResourceIdentities[2]{};
    uint32_t specializationCount = 0;
    uint32_t selectedSpecialization = 0;
    bool teardownRequested = false;
};

struct ReachDisplaySurfaceProof
{
    ReachDisplayContinuity continuity{};
    ReachPreflightToken preflight{};
    uintptr_t immediateContextIdentity = 0;
    uintptr_t eyeResourceIdentities[2]{};
    ReachCopyShape source{};
    ReachCopyShape eyes[2]{};
    uint32_t readyEyeMask = 0;
    bool engineSwapchainMatchesPresent = false;
    bool selectedRtvMatchesRecord0 = false;
    bool swapchainContract = false;
    bool sameDevice = false;
    bool immediateContext = false;
};

inline bool ReachDisplayContinuityValid(
    const ReachDisplayContinuity& continuity) noexcept
{
    return ReachModuleEpochValid(continuity.epoch) &&
        continuity.resourceRevision != 0 &&
        continuity.lifecycleSerial != 0 &&
        continuity.lifecycleSerial !=
            std::numeric_limits<uint64_t>::max() &&
        continuity.swapchainIdentity != 0 &&
        continuity.buffer0Identity != 0 &&
        continuity.surfaceArrayIdentity != 0 &&
        continuity.record0RtvIdentity != 0 &&
        continuity.record0SrvIdentity != 0 &&
        continuity.selectedRtvIdentity ==
            continuity.record0RtvIdentity &&
        continuity.deviceIdentity != 0 &&
        continuity.immediateContextIdentity != 0 &&
        continuity.eyeResourceIdentities[0] != 0 &&
        continuity.eyeResourceIdentities[1] != 0 &&
        continuity.eyeResourceIdentities[0] !=
            continuity.eyeResourceIdentities[1] &&
        continuity.eyeResourceIdentities[0] !=
            continuity.buffer0Identity &&
        continuity.eyeResourceIdentities[1] !=
            continuity.buffer0Identity &&
        continuity.specializationCount == kReachDisplaySurfaceCount &&
        continuity.selectedSpecialization == 0 &&
        !continuity.teardownRequested;
}

inline bool ReachSameDisplayContinuity(
    const ReachDisplayContinuity& left,
    const ReachDisplayContinuity& right) noexcept
{
    return ReachDisplayContinuityValid(left) &&
        ReachDisplayContinuityValid(right) &&
        ReachSameModuleEpoch(left.epoch, right.epoch) &&
        left.resourceRevision == right.resourceRevision &&
        left.lifecycleSerial == right.lifecycleSerial &&
        left.swapchainIdentity == right.swapchainIdentity &&
        left.buffer0Identity == right.buffer0Identity &&
        left.surfaceArrayIdentity == right.surfaceArrayIdentity &&
        left.record0RtvIdentity == right.record0RtvIdentity &&
        left.record0SrvIdentity == right.record0SrvIdentity &&
        left.selectedRtvIdentity == right.selectedRtvIdentity &&
        left.deviceIdentity == right.deviceIdentity &&
        left.immediateContextIdentity ==
            right.immediateContextIdentity &&
        left.eyeResourceIdentities[0] ==
            right.eyeResourceIdentities[0] &&
        left.eyeResourceIdentities[1] ==
            right.eyeResourceIdentities[1] &&
        left.specializationCount == right.specializationCount &&
        left.selectedSpecialization == right.selectedSpecialization;
}

inline bool ReachDisplaySurfaceProofComplete(
    const ReachDisplaySurfaceProof& proof) noexcept
{
    return proof.preflight.Complete() &&
        IsPreflightCurrent(proof.preflight) &&
        ReachSameModuleEpoch(
            proof.preflight.Epoch(), proof.continuity.epoch) &&
        ReachDisplayContinuityValid(proof.continuity) &&
        proof.immediateContextIdentity ==
            proof.continuity.immediateContextIdentity &&
        proof.eyeResourceIdentities[0] ==
            proof.continuity.eyeResourceIdentities[0] &&
        proof.eyeResourceIdentities[1] ==
            proof.continuity.eyeResourceIdentities[1] &&
        ReachDisplayCopyShapeValid(proof.source) &&
        ReachSameCopyShape(proof.source, proof.eyes[0]) &&
        ReachSameCopyShape(proof.source, proof.eyes[1]) &&
        proof.readyEyeMask == 0x3u &&
        proof.engineSwapchainMatchesPresent &&
        proof.selectedRtvMatchesRecord0 &&
        proof.swapchainContract &&
        proof.sameDevice &&
        proof.immediateContext;
}

// Owns the live display-resource revision. A copied readiness token becomes
// unusable immediately when the swapchain, buffer, engine views, ResizeBuffers,
// title epoch, or device/context proof is invalidated.
class ReachDirectCopyGate
{
public:
    bool AdvanceEpoch(const ReachModuleEpoch& epoch) noexcept
    {
        if (ReachModuleEpochValid(m_epoch) ||
            !ReachModuleEpochValid(epoch) ||
            epoch.generation <= m_highestGeneration)
        {
            return false;
        }
        m_epoch = epoch;
        m_highestGeneration = epoch.generation;
        m_lastResourceRevision = 0;
        m_continuity = {};
        m_preflight = {};
        m_ready = false;
        return true;
    }

    bool Publish(const ReachDisplaySurfaceProof& proof) noexcept
    {
        // A refresh attempt replaces the old capability atomically from the
        // policy's perspective: even a failed replacement revokes prior
        // readiness and all retained live identities.
        m_ready = false;
        m_continuity = {};
        m_preflight = {};
        if (!ReachSameModuleEpoch(proof.continuity.epoch, m_epoch) ||
            !ReachDisplaySurfaceProofComplete(proof) ||
            proof.continuity.resourceRevision <=
                m_lastResourceRevision ||
            m_nonceCounter == std::numeric_limits<uint64_t>::max())
        {
            return false;
        }
        ++m_nonceCounter;
        m_lastResourceRevision = proof.continuity.resourceRevision;
        m_continuity = proof.continuity;
        m_preflight = proof.preflight;
        m_nonce = m_nonceCounter;
        m_ready = true;
        return Ready();
    }

    ReachDirectCopyToken Prepare(
        const ReachPreparedFrameToken& preparedFrame,
        const ReachDisplayContinuity& live) const noexcept
    {
        return Ready() && preparedFrame.Ready() &&
                ReachSameModuleEpoch(preparedFrame.Epoch(), m_epoch) &&
                ReachSameDisplayContinuity(live, m_continuity)
            ? ReachDirectCopyToken(
                m_epoch, preparedFrame.Serial(),
                m_continuity.resourceRevision, m_nonce)
            : ReachDirectCopyToken{};
    }

    bool IsCurrent(
        const ReachDirectCopyToken& token,
        const ReachDisplayContinuity& live) const noexcept
    {
        return Ready() && token.Ready() &&
            ReachSameModuleEpoch(token.Epoch(), m_epoch) &&
            token.ResourceRevision() ==
                m_continuity.resourceRevision &&
            token.Nonce() == m_nonce &&
            ReachSameDisplayContinuity(live, m_continuity);
    }

    bool Invalidate(const ReachModuleEpoch& epoch) noexcept
    {
        if (!ReachSameModuleEpoch(epoch, m_epoch))
            return false;
        m_ready = false;
        m_continuity = {};
        m_preflight = {};
        return true;
    }

    bool Teardown(const ReachModuleEpoch& epoch) noexcept
    {
        if (!ReachSameModuleEpoch(epoch, m_epoch))
            return false;
        m_ready = false;
        m_continuity = {};
        m_preflight = {};
        m_epoch = {};
        m_lastResourceRevision = 0;
        return true;
    }

    bool Ready() const noexcept
    {
        return m_ready && IsPreflightCurrent(m_preflight);
    }
    ReachDisplayContinuity Continuity() const noexcept
    {
        return m_continuity;
    }
    uint64_t LastResourceRevision() const noexcept
    {
        return m_lastResourceRevision;
    }

private:
    ReachModuleEpoch m_epoch{};
    uint32_t m_highestGeneration = 0;
    uint64_t m_lastResourceRevision = 0;
    uint64_t m_nonceCounter = 0;
    uint64_t m_nonce = 0;
    ReachDisplayContinuity m_continuity{};
    ReachPreflightToken m_preflight{};
    bool m_ready = false;
};

enum class ReachCleanupDisposition : uint8_t
{
    None = 0,
    Completed,
    Aborted,
};

class ReachCleanupToken
{
public:
    ReachCleanupToken() noexcept = default;

    bool Valid() const noexcept
    {
        return m_disposition != ReachCleanupDisposition::None;
    }
    ReachModuleEpoch Epoch() const noexcept { return m_epoch; }
    uint64_t PreparedFrameSerial() const noexcept
    {
        return m_preparedFrameSerial;
    }
    ReachCleanupDisposition Disposition() const noexcept
    {
        return m_disposition;
    }

private:
    ReachCleanupToken(
        const ReachModuleEpoch& epoch, uint64_t preparedFrameSerial,
        ReachCleanupDisposition disposition) noexcept
        : m_epoch(epoch),
          m_preparedFrameSerial(preparedFrameSerial),
          m_disposition(disposition)
    {
    }

    ReachModuleEpoch m_epoch{};
    uint64_t m_preparedFrameSerial = 0;
    ReachCleanupDisposition m_disposition = ReachCleanupDisposition::None;

    friend class ReachRollbackGate;
};

enum class ReachOuterRenderCaller : uint8_t
{
    Unknown = 0,
    NormalPlayer,
    ScreenshotTileBloom,
};

inline bool ReachAddressFromRva(
    uintptr_t moduleBase, size_t moduleSize, uintptr_t rva,
    uintptr_t& address) noexcept
{
    if (!moduleBase || rva >= moduleSize ||
        rva > std::numeric_limits<uintptr_t>::max() - moduleBase)
    {
        return false;
    }
    address = moduleBase + rva;
    return true;
}

inline bool ReachMotionBlurSlotsMatchPinnedImage(
    uintptr_t moduleBase, size_t moduleSize,
    uintptr_t scaleSlot, uintptr_t maxSlot) noexcept
{
    if (moduleSize != kReachRetailImageSize || scaleSlot == maxSlot)
        return false;
    uintptr_t expectedScale = 0;
    uintptr_t expectedMax = 0;
    return ReachAddressFromRva(
               moduleBase, moduleSize,
               kReachMotionBlurScaleValueRva, expectedScale) &&
        ReachAddressFromRva(
               moduleBase, moduleSize,
               kReachMotionBlurMaxValueRva, expectedMax) &&
        scaleSlot == expectedScale && maxSlot == expectedMax;
}

// Cross-check the BY-NAME `render_rain` resolution against the pinned image,
// the same way the motion-blur slots are checked. The name scan is what binds
// the control; this only refuses a result that disagrees with the exact module
// the rest of the Reach core is pinned to.
inline bool ReachRenderRainSlotMatchesPinnedImage(
    uintptr_t moduleBase, size_t moduleSize, uintptr_t rainSlot) noexcept
{
    if (moduleSize != kReachRetailImageSize)
        return false;
    uintptr_t expected = 0;
    return ReachAddressFromRva(
               moduleBase, moduleSize, kReachRenderRainValueRva, expected) &&
        rainSlot == expected;
}

inline bool ReachMotionBlurScaleUsable(float scale) noexcept
{
    return std::isfinite(scale) &&
        scale > kReachMotionBlurMinimumUsableScale;
}

inline bool ReachMotionBlurSuppressionValuesValid(
    float scale, float maximum) noexcept
{
    return ReachMotionBlurScaleUsable(scale) &&
        std::isfinite(maximum) && maximum >= 0.0f;
}

inline ReachOuterRenderCaller ClassifyReachOuterRenderCaller(
    uintptr_t moduleBase, size_t moduleSize, uintptr_t returnAddress) noexcept
{
    if (moduleSize != kReachRetailImageSize)
        return ReachOuterRenderCaller::Unknown;

    uintptr_t normalReturn = 0;
    uintptr_t screenshotReturn = 0;
    if (!ReachAddressFromRva(moduleBase, moduleSize,
                            kReachNormalOuterReturnRva, normalReturn) ||
        !ReachAddressFromRva(moduleBase, moduleSize,
                            kReachScreenshotOuterReturnRva, screenshotReturn))
    {
        return ReachOuterRenderCaller::Unknown;
    }
    if (returnAddress == normalReturn)
        return ReachOuterRenderCaller::NormalPlayer;
    if (returnAddress == screenshotReturn)
        return ReachOuterRenderCaller::ScreenshotTileBloom;
    return ReachOuterRenderCaller::Unknown;
}

class ReachRenderFreshnessGate
{
public:
    bool AdvanceEpoch(const ReachModuleEpoch& epoch) noexcept
    {
        if (ReachModuleEpochValid(m_epoch) ||
            !ReachModuleEpochValid(epoch) ||
            epoch.generation <= m_highestGeneration)
        {
            return false;
        }
        m_epoch = epoch;
        m_highestGeneration = epoch.generation;
        m_lastObservedSerial = 0;
        ResetWindow();
        return true;
    }

    ReachFreshCameraToken Observe(
        uint64_t nowMs, const ReachModuleEpoch& epoch,
        uint64_t preparedFrameSerial,
        bool exactNormalSlotZero, bool cameraValid) noexcept
    {
        if (!ReachSameModuleEpoch(epoch, m_epoch))
            return {};

        m_currentToken = {};
        if (!preparedFrameSerial ||
            preparedFrameSerial <= m_lastObservedSerial)
        {
            ResetWindow();
            return {};
        }
        m_lastObservedSerial = preparedFrameSerial;

        if (!nowMs || !exactNormalSlotZero || !cameraValid)
        {
            ResetWindow();
            return {};
        }

        if (!m_transactionCount || nowMs <= m_lastMs ||
            nowMs - m_lastMs >= kReachRenderFreshnessMaxGapMs)
        {
            m_transactionCount = 1;
            m_firstMs = nowMs;
            m_lastMs = nowMs;
            return {};
        }

        if (m_transactionCount != std::numeric_limits<uint32_t>::max())
            ++m_transactionCount;
        m_lastMs = nowMs;
        if (m_lastMs - m_firstMs <= kReachRenderSafetyIntervalMs ||
            m_nonceCounter == std::numeric_limits<uint64_t>::max())
        {
            return {};
        }

        ++m_nonceCounter;
        m_currentToken = ReachFreshCameraToken(
            m_epoch, preparedFrameSerial, m_nonceCounter, nowMs);
        return m_currentToken;
    }

    bool IsCurrent(const ReachFreshCameraToken& token) const noexcept
    {
        return token.Stable() && m_currentToken.Stable() &&
            ReachSameModuleEpoch(token.Epoch(), m_epoch) &&
            ReachSameModuleEpoch(token.Epoch(), m_currentToken.Epoch()) &&
            token.PreparedFrameSerial() ==
                m_currentToken.PreparedFrameSerial() &&
            token.Nonce() == m_currentToken.Nonce();
    }

    bool Consume(
        const ReachFreshCameraToken& fresh,
        const ReachPreparedFrameToken& preparedFrame,
        uint64_t nowMs) noexcept
    {
        if (!IsCurrent(fresh) || !preparedFrame.Ready() ||
            !ReachSameModuleEpoch(fresh.Epoch(), preparedFrame.Epoch()) ||
            fresh.PreparedFrameSerial() != preparedFrame.Serial() ||
            !nowMs || nowMs < fresh.ObservedAtMs() ||
            nowMs - fresh.ObservedAtMs() >= kReachRenderFreshnessMaxGapMs)
        {
            m_currentToken = {};
            return false;
        }
        m_currentToken = {};
        return true;
    }

    bool Teardown(const ReachModuleEpoch& epoch) noexcept
    {
        if (!ReachSameModuleEpoch(epoch, m_epoch))
            return false;
        m_epoch = {};
        m_lastObservedSerial = 0;
        ResetWindow();
        return true;
    }

    void ResetWindow() noexcept
    {
        m_transactionCount = 0;
        m_firstMs = 0;
        m_lastMs = 0;
        m_currentToken = {};
    }

    uint32_t TransactionCount() const noexcept { return m_transactionCount; }
    uint64_t CurrentSpanMs() const noexcept
    {
        return m_transactionCount && m_lastMs >= m_firstMs
            ? m_lastMs - m_firstMs
            : 0;
    }

private:
    ReachModuleEpoch m_epoch{};
    uint32_t m_highestGeneration = 0;
    uint64_t m_lastObservedSerial = 0;
    uint64_t m_nonceCounter = 0;
    ReachFreshCameraToken m_currentToken{};
    uint32_t m_transactionCount = 0;
    uint64_t m_firstMs = 0;
    uint64_t m_lastMs = 0;
};

struct ReachOuterRenderInput
{
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    uintptr_t returnAddress = 0;
    uintptr_t workspace = 0;
    uintptr_t playerView = 0;
    uint32_t playerWindowIndex = 0;
    int32_t cameraStackDepthBefore = -1;
    uint64_t nowMs = 0;
    ReachPreflightToken preflight{};
    ReachFreshCameraToken freshCamera{};
    ReachPreparedFrameToken preparedFrame{};
    bool teardownRequested = false;
};

class ReachRenderOwnerToken
{
public:
    ReachRenderOwnerToken() noexcept = default;

    bool Active() const noexcept { return m_active; }
    ReachModuleEpoch Epoch() const noexcept { return m_epoch; }
    uint64_t PreparedFrameSerial() const noexcept
    {
        return m_preparedFrameSerial;
    }
    uintptr_t Workspace() const noexcept { return m_workspace; }
    uintptr_t PlayerView() const noexcept { return m_playerView; }
    int32_t CameraStackDepthBefore() const noexcept
    {
        return m_cameraStackDepthBefore;
    }

private:
    bool m_active = false;
    ReachModuleEpoch m_epoch{};
    uint64_t m_preparedFrameSerial = 0;
    uintptr_t m_workspace = 0;
    uintptr_t m_playerView = 0;
    int32_t m_cameraStackDepthBefore = -1;

    friend class ReachRenderOwnerGate;
};

inline bool ReachNormalOuterInputMatches(
    const ReachOuterRenderInput& input) noexcept
{
    const ReachModuleEpoch epoch = input.preflight.Epoch();
    if (!input.preflight.Complete() ||
        !IsPreflightCurrent(input.preflight) ||
        !input.freshCamera.Stable() ||
        !input.preparedFrame.Ready() || input.teardownRequested ||
        !input.nowMs ||
        !ReachModuleEpochValid(epoch) ||
        !ReachSameModuleEpoch(epoch, input.freshCamera.Epoch()) ||
        !ReachSameModuleEpoch(epoch, input.preparedFrame.Epoch()) ||
        !input.preparedFrame.Serial() ||
        input.freshCamera.PreparedFrameSerial() !=
            input.preparedFrame.Serial() ||
        input.moduleBase != epoch.moduleBase ||
        input.moduleSize != kReachRetailImageSize ||
        input.playerWindowIndex != 0 ||
        input.cameraStackDepthBefore < -1 ||
        input.cameraStackDepthBefore >= 3 ||
        ClassifyReachOuterRenderCaller(
            input.moduleBase, input.moduleSize, input.returnAddress) !=
            ReachOuterRenderCaller::NormalPlayer)
    {
        return false;
    }

    uintptr_t expectedWorkspace = 0;
    uintptr_t expectedPlayerView = 0;
    return ReachAddressFromRva(input.moduleBase, input.moduleSize,
                               kReachDefaultWorkspaceRva,
                               expectedWorkspace) &&
        ReachAddressFromRva(input.moduleBase, input.moduleSize,
                            kReachPlayerViewArrayRva,
                            expectedPlayerView) &&
        input.workspace == expectedWorkspace &&
        input.playerView == expectedPlayerView;
}

// Fixed-storage TLS-ready gate for the exact outer normal owner. It consumes a
// current freshness capability tied to the same OpenXR prepared-frame serial;
// nested, stale, and replayed owners stay stock.
class ReachRenderOwnerGate
{
public:
    bool AdvanceEpoch(const ReachModuleEpoch& epoch) noexcept
    {
        if (m_token.m_active || ReachModuleEpochValid(m_epoch) ||
            !ReachModuleEpochValid(epoch) ||
            epoch.generation <= m_highestGeneration)
        {
            return false;
        }
        m_epoch = epoch;
        m_highestGeneration = epoch.generation;
        m_lastCompletedSerial = 0;
        return true;
    }

    bool TryBegin(
        const ReachOuterRenderInput& input,
        ReachRenderFreshnessGate& freshness) noexcept
    {
        if (m_token.m_active || !ReachNormalOuterInputMatches(input))
            return false;

        if (!ReachSameModuleEpoch(input.preflight.Epoch(), m_epoch) ||
            input.preparedFrame.Serial() <= m_lastCompletedSerial ||
            !freshness.Consume(
                input.freshCamera, input.preparedFrame, input.nowMs))
            return false;

        m_token.m_active = true;
        m_token.m_epoch = input.preflight.Epoch();
        m_token.m_preparedFrameSerial = input.preparedFrame.Serial();
        m_token.m_workspace = input.workspace;
        m_token.m_playerView = input.playerView;
        m_token.m_cameraStackDepthBefore = input.cameraStackDepthBefore;
        return true;
    }

    bool Finish(
        const ReachRenderOwnerToken& token,
        const ReachCleanupToken& cleanup) noexcept
    {
        return cleanup.Valid() &&
            cleanup.Disposition() == ReachCleanupDisposition::Completed &&
            ReachSameModuleEpoch(cleanup.Epoch(), token.Epoch()) &&
            cleanup.PreparedFrameSerial() == token.PreparedFrameSerial() &&
            Release(token);
    }

    bool Abort(
        const ReachRenderOwnerToken& token,
        const ReachCleanupToken& cleanup) noexcept
    {
        // A partially entered serial is consumed so it cannot be replayed
        // after fallback or rollback.
        return cleanup.Valid() &&
            cleanup.Disposition() == ReachCleanupDisposition::Aborted &&
            ReachSameModuleEpoch(cleanup.Epoch(), token.Epoch()) &&
            cleanup.PreparedFrameSerial() == token.PreparedFrameSerial() &&
            Release(token);
    }

    bool Teardown(const ReachModuleEpoch& epoch) noexcept
    {
        if (m_token.m_active || !ReachSameModuleEpoch(epoch, m_epoch))
            return false;
        m_epoch = {};
        m_lastCompletedSerial = 0;
        return true;
    }

    ReachRenderOwnerToken Token() const noexcept { return m_token; }
    uint64_t LastCompletedSerial() const noexcept
    {
        return m_lastCompletedSerial;
    }

    bool IsCurrent(const ReachRenderOwnerToken& token) const noexcept
    {
        return m_token.m_active && token.m_active &&
            ReachSameModuleEpoch(m_token.m_epoch, token.m_epoch) &&
            m_token.m_preparedFrameSerial ==
                token.m_preparedFrameSerial &&
            m_token.m_workspace == token.m_workspace &&
            m_token.m_playerView == token.m_playerView &&
            m_token.m_cameraStackDepthBefore ==
                token.m_cameraStackDepthBefore;
    }

private:
    bool Release(const ReachRenderOwnerToken& token) noexcept
    {
        if (!IsCurrent(token))
            return false;
        m_lastCompletedSerial = token.m_preparedFrameSerial;
        m_token = {};
        return true;
    }

    ReachRenderOwnerToken m_token{};
    ReachModuleEpoch m_epoch{};
    uint32_t m_highestGeneration = 0;
    uint64_t m_lastCompletedSerial = 0;
};

struct ReachInnerRenderInput
{
    uintptr_t returnAddress = 0;
    uintptr_t playerView = 0;
    uintptr_t activeView = 0;
    int32_t cameraStackDepth = -1;
    uintptr_t topWorkspace = 0;
    uintptr_t workspaceCallback = 0;
    uintptr_t renderCameraOwner = 0;
    uint32_t selectedSpecialization = 0;
    bool primaryCameraValid = false;
    bool secondaryCameraValid = false;
    ReachPreparedFrameToken preparedFrame{};
    ReachDirectCopyToken directCopy{};
    ReachDisplayContinuity displayContinuity{};
    bool teardownRequested = false;
};

inline bool ReachInnerScopeMatches(
    const ReachRenderOwnerGate& owner,
    const ReachRenderOwnerToken& token,
    const ReachDirectCopyGate& directCopyGate,
    const ReachInnerRenderInput& input) noexcept
{
    if (!owner.IsCurrent(token) ||
        !ReachModuleEpochValid(token.Epoch()) || !token.Workspace() ||
        !token.PlayerView() || token.CameraStackDepthBefore() < -1 ||
        token.CameraStackDepthBefore() >= 3 ||
        !input.preparedFrame.Ready() || !input.directCopy.Ready() ||
        !ReachSameModuleEpoch(
            input.preparedFrame.Epoch(), token.Epoch()) ||
        !ReachSameModuleEpoch(input.directCopy.Epoch(), token.Epoch()) ||
        input.preparedFrame.Serial() != token.PreparedFrameSerial() ||
        input.directCopy.PreparedFrameSerial() !=
            token.PreparedFrameSerial() ||
        !directCopyGate.IsCurrent(
            input.directCopy, input.displayContinuity) ||
        input.playerView != token.PlayerView() ||
        input.activeView != token.PlayerView() ||
        input.cameraStackDepth != token.CameraStackDepthBefore() + 1 ||
        input.cameraStackDepth < 0 || input.cameraStackDepth > 3 ||
        input.topWorkspace != token.Workspace() ||
        input.selectedSpecialization != 0 ||
        !input.primaryCameraValid || !input.secondaryCameraValid ||
        input.teardownRequested)
    {
        return false;
    }

    uintptr_t expectedCallback = 0;
    uintptr_t expectedReturn = 0;
    if (!ReachAddressFromRva(token.Epoch().moduleBase, kReachRetailImageSize,
                            kReachCameraStackCallbackRva,
                            expectedCallback) ||
        !ReachAddressFromRva(token.Epoch().moduleBase, kReachRetailImageSize,
                            kReachPlayerViewRenderReturnRva,
                            expectedReturn) ||
        input.returnAddress != expectedReturn ||
        input.workspaceCallback != expectedCallback ||
        token.PlayerView() > std::numeric_limits<uintptr_t>::max() -
            kReachPlayerViewCameraStateOffset)
    {
        return false;
    }

    return input.renderCameraOwner ==
        token.PlayerView() + kReachPlayerViewCameraStateOffset;
}

enum class ReachRenderAction : uint8_t
{
    StockOnce = 0,
    StereoTransaction,
};

inline ReachRenderAction SelectReachRenderAction(
    bool runtimeHooksPermitted, const ReachPreflightToken& preflight,
    const ReachRenderOwnerGate& owner,
    const ReachRenderOwnerToken& token,
    const ReachDirectCopyGate& directCopyGate,
    const ReachInnerRenderInput& input) noexcept
{
    return runtimeHooksPermitted && preflight.Complete() &&
            IsPreflightCurrent(preflight) &&
            owner.IsCurrent(token) &&
            ReachSameModuleEpoch(preflight.Epoch(), token.Epoch()) &&
            ReachInnerScopeMatches(
                owner, token, directCopyGate, input)
        ? ReachRenderAction::StereoTransaction
        : ReachRenderAction::StockOnce;
}

inline int ReachEyeForPass(uint32_t pass, bool rightEyeFirst) noexcept
{
    if (pass > 1)
        return -1;
    const int firstEye = rightEyeFirst ? 1 : 0;
    return pass == 0 ? firstEye : 1 - firstEye;
}

struct ReachStereoPassPolicy
{
    bool valid = false;
    int eye = -1;
    bool writeLastWindow = false;
    uint8_t lastWindowInput = 0;
    bool restoreLastWindowAfterPass = true;
};

inline ReachStereoPassPolicy SelectReachStereoPassPolicy(
    uint32_t pass, bool rightEyeFirst, uint8_t originalStockValue) noexcept
{
    const int eye = ReachEyeForPass(pass, rightEyeFirst);
    if (eye < 0)
    {
        // Invalid pass indices authorize no write and require any pending
        // first-pass mutation to be rolled back.
        return {false, -1, false, originalStockValue, true};
    }

    // The first eye is an inserted pass and restores the stock byte for the
    // final eye. The final eye's actual post-call byte must persist.
    return {
        true, eye, true, pass == 0 ? uint8_t{0} : originalStockValue,
        pass == 0
    };
}

struct ReachRollbackLayout
{
    size_t workspaceSize;
    uintptr_t cameraStateOffset;
    size_t cameraStateSize;
    uintptr_t currentMatricesOffset;
    size_t currentMatricesSize;
    uintptr_t previousMatricesOffset;
    size_t previousMatricesSize;
    uintptr_t excludedLastWindowOffset;
};

inline constexpr ReachRollbackLayout kReachRollbackLayout{
    kReachRenderScopeSnapshotSize,
    kReachPlayerViewCameraStateOffset,
    kReachPlayerViewCameraStateSize,
    kReachPlayerViewCurrentMatricesOffset,
    kReachPlayerViewMatrixBlockSize,
    kReachPlayerViewPreviousMatricesOffset,
    kReachPlayerViewMatrixBlockSize,
    kReachLastWindowFlagOffset,
};

inline constexpr bool ReachSpanFits(
    uintptr_t offset, size_t size, size_t limit) noexcept
{
    return offset <= limit && size <= limit - offset;
}

static_assert(kReachRenderScopeSnapshotSize == 0x02B0);
static_assert(ReachSpanFits(
    kReachPlayerViewCameraStateOffset, kReachPlayerViewCameraStateSize,
    kReachPlayerViewStride));
static_assert(ReachSpanFits(
    kReachPlayerViewCurrentMatricesOffset, kReachPlayerViewMatrixBlockSize,
    kReachPlayerViewStride));
static_assert(ReachSpanFits(
    kReachPlayerViewPreviousMatricesOffset, kReachPlayerViewMatrixBlockSize,
    kReachPlayerViewStride));
static_assert(
    kReachPlayerViewCameraStateOffset + kReachPlayerViewCameraStateSize <=
    kReachPlayerViewCurrentMatricesOffset);
static_assert(
    kReachPlayerViewCurrentMatricesOffset + kReachPlayerViewMatrixBlockSize <=
    kReachPlayerViewPreviousMatricesOffset);
static_assert(
    kReachPlayerViewPreviousMatricesOffset + kReachPlayerViewMatrixBlockSize <=
    kReachLastWindowFlagOffset);
static_assert(kReachLastWindowFlagOffset < kReachPlayerViewStride);

// Pure cleanup state for a future detour. It tracks both passes as dirty until
// an explicit restore transition and is the only producer of the exact cleanup
// capability required to finish or abort the live owner.
class ReachRollbackGate
{
public:
    bool Bind(
        const ReachRenderOwnerGate& owner,
        const ReachRenderOwnerToken& token) noexcept
    {
        if (m_phase != Phase::Inactive || !owner.IsCurrent(token))
            return false;
        m_epoch = token.Epoch();
        m_preparedFrameSerial = token.PreparedFrameSerial();
        m_phase = Phase::ReadyClean;
        return true;
    }

    bool BeginFirstPass(const ReachRenderOwnerToken& token) noexcept
    {
        if (!Matches(token) || m_phase != Phase::ReadyClean)
            return false;
        m_phase = Phase::FirstDirty;
        return true;
    }

    bool MarkFirstPassRestored(
        const ReachRenderOwnerToken& token) noexcept
    {
        if (!Matches(token) || m_phase != Phase::FirstDirty)
            return false;
        m_phase = Phase::BetweenPassesClean;
        return true;
    }

    bool BeginFinalPass(const ReachRenderOwnerToken& token) noexcept
    {
        if (!Matches(token) || m_phase != Phase::BetweenPassesClean)
            return false;
        m_phase = Phase::FinalDirty;
        return true;
    }

    ReachCleanupToken MarkFinalPassRestoredAndComplete(
        const ReachRenderOwnerToken& token) noexcept
    {
        if (!Matches(token) || m_phase != Phase::FinalDirty)
            return {};
        m_phase = Phase::CompletedClean;
        return ReachCleanupToken(
            m_epoch, m_preparedFrameSerial,
            ReachCleanupDisposition::Completed);
    }

    bool MarkDirtyPassRestoredForAbort(
        const ReachRenderOwnerToken& token) noexcept
    {
        if (!Matches(token) ||
            (m_phase != Phase::FirstDirty &&
             m_phase != Phase::FinalDirty))
        {
            return false;
        }
        m_phase = Phase::AbortReadyClean;
        return true;
    }

    ReachCleanupToken AbortClean(
        const ReachRenderOwnerToken& token) noexcept
    {
        if (!Matches(token) ||
            (m_phase != Phase::ReadyClean &&
             m_phase != Phase::BetweenPassesClean &&
             m_phase != Phase::AbortReadyClean))
        {
            return {};
        }
        m_phase = Phase::AbortedClean;
        return ReachCleanupToken(
            m_epoch, m_preparedFrameSerial,
            ReachCleanupDisposition::Aborted);
    }

    bool NeedsRollback() const noexcept
    {
        return m_phase == Phase::FirstDirty ||
            m_phase == Phase::FinalDirty;
    }
    bool Finished() const noexcept
    {
        return m_phase == Phase::CompletedClean;
    }
    bool Aborted() const noexcept
    {
        return m_phase == Phase::AbortedClean;
    }

private:
    enum class Phase : uint8_t
    {
        Inactive = 0,
        ReadyClean,
        FirstDirty,
        BetweenPassesClean,
        FinalDirty,
        AbortReadyClean,
        CompletedClean,
        AbortedClean,
    };

    bool Matches(const ReachRenderOwnerToken& token) const noexcept
    {
        return m_phase != Phase::Inactive && token.Active() &&
            ReachSameModuleEpoch(m_epoch, token.Epoch()) &&
            m_preparedFrameSerial == token.PreparedFrameSerial();
    }

    ReachModuleEpoch m_epoch{};
    uint64_t m_preparedFrameSerial = 0;
    Phase m_phase = Phase::Inactive;
};
