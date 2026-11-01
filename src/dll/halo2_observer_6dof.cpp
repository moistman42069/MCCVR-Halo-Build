#include "../common/halo2_visual_aim.h"
#include "../common/weapon_alignment.h"
#include "../common/halo2_weapon_animation.h"
#include "native_reload_policy.h"
#include "../common/vr_interaction_refinement_logic.h"
#include "../common/exclusive_input.h"
#include "../common/halo2_datum_logic.h"
#include "../common/weapon_model_catalog.h"
#include "../common/legacy_weapon_visual_defaults.h"
#include "../common/weapon_muzzle.h"
#include "../common/visual_hand_config.h"
#include "../common/scope_logic.h"
#include "../common/weapon_hand_logic.h"
#include "../common/native_shot_target_lease.h"
#include "../common/halo2_contact_melee_logic.h"
#include "../common/halo2_snap_turn_logic.h"
#include "../common/halo2_vehicle_view.h"
#include "../common/halo2_foot_view.h"
#include "../common/halo2_head_relative_logic.h"
#if HALOMCCVR_DEV_DIAGNOSTICS
#include "../common/halo2_sound_listener_logic.h"   // the dev sound listener witness (halo2_sound_listener_witness.inl)
#endif
#include "../common/director_camera_logic.h"
#include "../common/aim_write_logic.h"
#include "../common/seat_drive_policy.h"
#include "../common/seat_barrel_logic.h"
#include "../common/halo2_vehicle_identity.h"
#include "contact_melee_queue.h"
#include "hook_quiescence.h"
#include "native_forward.h"
#include "../common/minhook_lifecycle.h"
#include "../common/manual_vr_recovery_logic.h"
#include "halo2_observer_6dof.h"
#include "native_vehicle_first_person.h"
#include "physical_crouch_camera.h"
#include "sigscan.h"
// Where the direct drive's code is in the loaded game DLL (halo2_direct_drive.inl): the tables and their
// resolution, and the DLL's side.
#include "../common/drive_sites.h"
#include "drive_cascade.h"
#include "drive_install.h"
#include "seat_frame_reader.h"
#include "seat_limit_publication.h"
#include "../common/physical_crouch_native_read.h"
#include "../common/physical_crouch_additional_witnesses.h"

#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <intrin.h>

#include "../common/halo2_render_logic.h"
#include "../common/halo2_world_collision_logic.h"
#include "../common/as_seen_logic.h"
#include "../common/config.h"
#include "../common/log.h"
#include "game.h"
#include "telemetry_recorder.h"
#include "title_adapter.h"
#include "roomscale.h"
#include "menu.h"
#include "halo2_stereo_core.h"
#include "vr.h"
#include "sight_layer.h"
#include "../common/session_read_logic.h"

#ifndef HALOMCCVR_HALO2_OBSERVER_6DOF
#define HALOMCCVR_HALO2_OBSERVER_6DOF 0
#endif

#if HALOMCCVR_HALO2_OBSERVER_6DOF

// Halo 2 direct drive (halo2_direct_drive.inl): halo2_player_control_hook.asm enters through Halo2PcHook,
// runs the native accumulator through g_halo2PcOrig, then calls Halo2PcApplyForUser.
extern "C"
{
    void* g_halo2PcOrig = nullptr;
    void Halo2PcHook();
    void Halo2PcApplyForUser(int user, uint64_t returnAddress);
}

namespace
{
    // void __fastcall observer_final_transform(int user_index)
    using Halo2ObserverFinalTransformFn = void(__fastcall*)(uint32_t);

    // Unique in the complete mapped image of both pinned retail editions.
    // Prologue plus `movsxd rbx,ecx` / `imul rdi,rbx,0x368`, so the observer
    // stride is inside the signature and a moved array cannot silently change
    // the element size. The `mov rax,[rip+disp32]` at +0x1A resolves the
    // director-space matrix pointer slot at RVA 0xDFCB58, which is verified as
    // a second, independent identity check on the match.
    constexpr char kFinalTransformPattern[] =
        "48 89 5C 24 18 48 89 74 24 20 55 57 41 57 48 8D 6C 24 D0 "
        "48 81 EC 30 01 00 00 48 8B 05 ?? ?? ?? ?? 48 63 D9 "
        "48 69 FB 68 03 00 00";
    constexpr uint32_t kFinalTransformMatrixDispOffset = 0x1D;
    constexpr uint32_t kFinalTransformMatrixNextOffset = 0x21;
    constexpr uint32_t kFinalTransformMatrixSlotRva = 0x00DFCB58;

    // Only the primary player's observer is owned. Split-screen guests keep
    // their stock camera rather than inheriting one headset's pose.
    constexpr uint32_t kOwnedUser = 0;

    // E-H2-23: the frame's first-person weapon placement.
    using Halo2FirstPersonWeaponsFn = void(__fastcall*)(
        uint32_t, uint32_t, uint8_t);

    enum class CoreState : uint8_t
    {
        StockFallback = 0,
        CleanupRequired,
        Installed,
    };

    struct HeadReference
    {
        float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        float position[3]{};
    };

    std::atomic<bool> g_installed{false};
    std::atomic<bool> g_armed{false};
    std::atomic<bool> g_levelLive{false};
    std::atomic<bool> g_teardownRequested{false};
    std::atomic<bool> g_recenterRequested{true};
    std::atomic<bool> g_manualRecenterRequested{false};   // the next recenter is the player's own
    std::atomic<bool> g_referenceValid{false};
    std::atomic<uint32_t> g_generation{0};
    std::atomic<uint32_t> g_vrFailureGeneration{0};
    std::atomic<uintptr_t> g_moduleBase{0};
    std::atomic<size_t> g_moduleSize{0};
    std::atomic<uintptr_t> g_originalAddress{0};
    std::atomic<uintptr_t> g_observerResult{0};
    std::atomic<uint32_t> g_activeCallbacks{0};

    // E-H2-75 / Stage 3AK: Halo 2 Classic's first-person muzzle effect enters
    // this particle renderer. The hook is optional and fail-open: its failure
    // never changes camera ownership. Four 32-bit arguments preserve the
    // pinned function's RCX/RDX/R8/R9 ABI; only RDX's low byte is classified.
    using Halo2ParticleRendererFn = void(__fastcall*)(
        uint32_t, uint32_t, uint32_t, uint32_t);
    constexpr uint32_t kHalo2ParticleRendererRva = 0x0076DC90;
    constexpr char kHalo2ParticleRendererPattern[] =
        "48 8B C4 88 50 10 89 48 08 55 53 56 57 41 55";
    void* g_particleTarget = nullptr;
    std::atomic<uintptr_t> g_particleOriginal{0};
    std::atomic<uint32_t> g_particleActiveCallbacks{0};
    std::atomic<uint64_t> g_particleSuppressed{0};
    std::atomic<uint64_t> g_particleReadFaults{0};
    std::atomic<bool> g_particleHitPending{false};
    uint32_t g_particleRejectedGeneration = 0;
    bool g_particleHitLogged = false;

    // E-H2-23 (C-H2-31): the weapon tick witness. first_person_weapons runs
    // at the game tick (~60/s, the C-H2-30 log: 1890 placements against ~200
    // observer updates per second) and places the weapon against the observer
    // record as it stands at that moment - the pose the mod published at the
    // last observer update before the tick. The witness records WHICH
    // publication that was (its serial), and the interpolator's first-person
    // slots are reset after every placement so the weapon the renderer draws
    // is that tick's placement and not a blend towards the previous one. The
    // Anniversary core renders the eyes from that publication and submits its
    // poses, so weapon and world are one pose and the compositor reprojects
    // both together. The observer update itself is UNTOUCHED (C-H2-30 fed it
    // the tick's sample and cut the camera to tick rate).
    void* g_weaponsTarget = nullptr;
    std::atomic<uintptr_t> g_weaponsOriginal{0};
    std::atomic<uint32_t> g_weaponsActiveCallbacks{0};
    std::atomic<uint64_t> g_weaponsCalls{0};
    std::atomic<uint64_t> g_weaponsWitnessed{0};
    std::atomic<uint64_t> g_weaponsRecordForeign{0};
    std::atomic<uint64_t> g_weaponsNoPublication{0};

    // generation << 32 is not enough for a 64-bit serial; two atomics, the
    // serial written last (release) and read first (acquire).
    std::atomic<uint32_t> g_weaponTickGeneration{0};
    std::atomic<uint64_t> g_weaponTickIndex{0};
    // E-H2-30: the tick BEFORE it, so the weapon's view can follow the
    // interpolator's blend between the two instead of snapping to the newest.
    std::atomic<uint64_t> g_weaponTickPreviousIndex{0};
    // E-H2-32: the tracked camera of the witnessed tick, seqlocked, and the
    // re-anchor hook that moves the drawn weapon from it to the frame camera.
    std::atomic<uint32_t> g_weaponTickBasisVersion{0};
    Halo2CameraBasis g_weaponTickBasis{};
    void* g_reanchorTarget = nullptr;
    std::atomic<uintptr_t> g_reanchorOriginal{0};
    std::atomic<uint32_t> g_reanchorActiveCallbacks{0};
    std::atomic<uint64_t> g_reanchorApplied{0};
    std::atomic<uint64_t> g_reanchorIdentity{0};
    std::atomic<uint64_t> g_reanchorSkipped{0};
    // C-H2-38: the C-H2-37 log reported "re-anchor applied 0, identity 0,
    // skipped 0" in both renderers - the detour's gate never opened and no
    // counter said which term closed it. Every entry is now classified and
    // the last call's arguments are kept for the report.
    std::atomic<uint64_t> g_reanchorEntered{0};
    std::atomic<uint64_t> g_reanchorUnhandled{0};
    std::atomic<uint64_t> g_reanchorOtherPlayer{0};
    std::atomic<uint64_t> g_reanchorNoNodes{0};
    std::atomic<uint64_t> g_reanchorNotLive{0};
    std::atomic<uint64_t> g_reanchorNoTick{0};
    std::atomic<int> g_reanchorLastPlayer{-1};
    std::atomic<int> g_reanchorLastId{-1};
    std::atomic<int> g_reanchorLastSlot{-1};
    std::atomic<uint32_t> g_reanchorLastHandled{0};
    std::atomic<uint32_t> g_reanchorLastCount{0};
    // E-H2-34 (C-H2-39): the pass cameras the owning core named (seqlocked),
    // the per-slot idempotency caches, and what the re-anchor found.
    std::atomic<uint32_t> g_passCamerasVersion{0};
    Halo2FirstPersonPassCameras g_passCameras{};
    std::atomic<bool> g_passCamerasSet{false};
    Halo2FirstPersonSlotCache g_slotCache[kHalo2FrameInterpolatorFirstPersonSlots]{};
    std::atomic<bool> g_slotCacheBusy{false};
    std::atomic<uint64_t> g_reanchorFromCache{0};
    std::atomic<uint64_t> g_reanchorCompensated{0};
    std::atomic<uint64_t> g_reanchorSpaceWorld{0};
    std::atomic<uint64_t> g_reanchorSpaceRelative{0};
    std::atomic<uint64_t> g_reanchorSpaceUnknown{0};
    std::atomic<uint64_t> g_reanchorPassCameras{0};
    std::atomic<uint64_t> g_reanchorFallbackFrame{0};
    std::atomic<uint64_t> g_reanchorBusy{0};
    std::atomic<uint64_t> g_reanchorBadSlot{0};
    std::atomic<uint64_t> g_publicationIndex{0};
    // E-H2-70 (C-H2-82): the camera the ENGINE handed the packet builder for
    // the pass whose packets the mod just owned - the frame the final
    // first-person nodes are composed against. Seqlocked; read by each
    // stereo core to compare with the camera its renderer will actually
    // draw those nodes from. Separate per renderer, because the two
    // builder calls are distinct engine calls with their own arguments.
    std::atomic<uint32_t> g_packetCameraVersion[2]{};
    Halo2CameraBasis g_packetCamera[2]{};
    std::atomic<bool> g_packetCameraValid[2]{};
    // C-H2-71: independent fail-open visibility-cover telemetry. The observer
    // pose remains owned even when this optional one-float write is refused.
    std::atomic<uint64_t> g_visibilityCoverApplied{0};
    std::atomic<uint64_t> g_visibilityCoverAlreadyWide{0};
    std::atomic<uint64_t> g_visibilityCoverRefused{0};
    std::atomic<uint32_t> g_visibilityCoverStockBits{0};
    std::atomic<uint32_t> g_visibilityCoverRequiredBits{0};
    std::atomic<uint32_t> g_visibilityCoverWrittenBits{0};
    std::atomic<uint64_t> g_weaponsSlotResets{0};
    std::atomic<uint64_t> g_weaponsResetsBypassedForFloaty{0};
    std::atomic<uint64_t> g_floatyApplied{0};
    std::atomic<uint64_t> g_floatyFailed{0};
    // C-H2-47 (E-H2-41): the engine's own animation-graph readers, resolved by
    // unique signature. They are pure leaf reads - no allocation, no lock, no
    // logging - which is why they are safe to call from inside the palette
    // detour. Zero means the left-hand transaction stays dormant and the
    // single-carrier C-H2-46 placement runs instead.
    using Halo2GraphDefinitionGetFn = const void*(__fastcall*)(uint32_t);
    using Halo2GetSkeletonNodeFn = const void*(__fastcall*)(const void*, int);
    using Halo2FindNodeByFlagsFn = int(__fastcall*)(const void*, uint32_t);
    std::atomic<uintptr_t> g_graphDefinitionGet{0};
    std::atomic<uintptr_t> g_graphGetSkeletonNode{0};
    std::atomic<uintptr_t> g_graphFindNodeByFlags{0};
    std::atomic<uint64_t> g_leftHandApplied{0};
    std::atomic<uint64_t> g_leftHandRigidFallback{0};
    std::atomic<uint64_t> g_armBindingRebuilds{0};
    std::atomic<int> g_armBindingLastReason{-1};
    // Cached per animation graph. Rebuilt only when the graph id or the node
    // count changes, so the hot path is a compare, not a scan.
    Halo2FirstPersonArmBinding g_armBinding{};
    int g_armBindingGraphId = 0;
    bool g_armBindingGraphIdValid = false;
    using Halo2MatrixComposeFn = void(__fastcall*)(
        const float*, const float*, float*);
    void* g_finalPaletteTarget = nullptr;
    std::atomic<uintptr_t> g_finalPaletteOriginal{0};
    std::atomic<bool> g_finalPaletteReady{false};
    std::atomic<uint32_t> g_finalPaletteActiveCallbacks{0};
    std::atomic<uint64_t> g_finalPaletteCalls{0};
    std::atomic<uint64_t> g_finalPaletteMovedRight{0};
    std::atomic<uint64_t> g_finalPaletteMovedLeft{0};
    std::atomic<uint64_t> g_finalPaletteCollapsed{0};
    std::atomic<uint64_t> g_finalPaletteCoLocatedArms{0};
    std::atomic<uint64_t> g_finalPaletteRefused{0};
    using Halo2FirstPersonPacketBuilderFn = int(__fastcall*)(
        uint32_t, uint32_t, const float*, const float*, const float*, int,
        uint32_t*, uint8_t);
    void* g_packetBuilderTarget = nullptr;
    std::atomic<uintptr_t> g_packetBuilderOriginal{0};
    // E-H2-71 (C-H2-83): the interpolated first-person FRAME getter the
    // builder prefers for Classic only. While the mod's Classic build runs,
    // it reports "none", which makes the builder compose against
    // user_data+0x20C8 - the exact frame Anniversary composes against.
    void* g_interpFrameTarget = nullptr;
    std::atomic<uintptr_t> g_interpFrameOriginal{0};
    std::atomic<uint32_t> g_interpFrameActiveCallbacks{0};
    std::atomic<uint64_t> g_interpFrameCalls{0};
    std::atomic<uint64_t> g_interpFrameForcedCurrent{0};
    thread_local bool g_classicBuildInProgress = false;
    // C-H2-85: Halo 2 classic-only mount trim application counters.
    std::atomic<uint64_t> g_classicTrimApplied{0};
    std::atomic<uint64_t> g_classicTrimRefused{0};
    // E-H2-74 (C-H2-87): barrel meter, per consumer (0 classic, 1
    // anniversary), millidegrees, last sample. Measured at the packet
    // build from the numbers actually in play - no theory:
    //   carrierVsCompose  angle(carrier.forward, compose camera forward)
    //   carrierElevation  signed elevation of carrier.forward above the
    //                     compose camera's forward, about its right axis
    //   stockVsCompose    angle(stock gun root +X, compose camera forward)
    //   stockElevation    signed elevation of the stock root +X likewise
    // If the classic gun is drawn tipped, one of these says by how much.
    std::atomic<int32_t> g_meterCarrierVsCompose[2]{};
    std::atomic<int32_t> g_meterCarrierElevation[2]{};
    std::atomic<int32_t> g_meterStockVsCompose[2]{};
    std::atomic<int32_t> g_meterStockElevation[2]{};

    void PublishBarrelMeter(
        int slot, const Halo2CameraBasis& compose,
        const Halo2CameraBasis& carrier, const float* gunRootNode) noexcept
    {
        if (slot < 0 || slot > 1) return;
        auto unit = [](const float v[3], float out[3]) -> bool {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (!std::isfinite(l) || l < 1e-5f) return false;
            out[0] = v[0] / l; out[1] = v[1] / l; out[2] = v[2] / l;
            return true;
        };
        float f[3], u[3];
        if (!unit(compose.forward, f) || !unit(compose.up, u)) return;
        const float r[3] = {
            f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2],
            f[0] * u[1] - f[1] * u[0]};
        auto publish = [&](const float dirIn[3], std::atomic<int32_t>& angle,
                           std::atomic<int32_t>& elevation) {
            float d[3];
            if (!unit(dirIn, d)) return;
            const float c = std::clamp(d[0] * f[0] + d[1] * f[1] + d[2] * f[2],
                                       -1.0f, 1.0f);
            const float total = std::acos(c) * 57.29578f;
            // elevation: angle of the direction above the compose forward
            // within the vertical plane (forward, up) - positive = higher
            const float upC = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
            const float fwdC = c;
            const float elev = std::atan2(upC, fwdC) * 57.29578f;
            (void)r;
            if (std::isfinite(total))
                angle.store(static_cast<int32_t>(total * 1000.0f),
                            std::memory_order_relaxed);
            if (std::isfinite(elev))
                elevation.store(static_cast<int32_t>(elev * 1000.0f),
                                std::memory_order_relaxed);
        };
        publish(carrier.forward, g_meterCarrierVsCompose[slot],
                g_meterCarrierElevation[slot]);
        if (gunRootNode)
        {
            // node layout: [0] scale, [1..3] column 0 (= root +X axis in
            // packet space, kHalo2FirstPersonNodeFloats stride)
            const float rootX[3] = {gunRootNode[1], gunRootNode[2], gunRootNode[3]};
            publish(rootX, g_meterStockVsCompose[slot],
                    g_meterStockElevation[slot]);
        }
    }
    std::atomic<uint32_t> g_packetBuilderActiveCallbacks{0};
    std::atomic<uint64_t> g_packetBuilderCalls{0};
    std::atomic<uint64_t> g_packetBuilderApplied{0};
    std::atomic<uint64_t> g_packetBuilderStock{0};
    std::atomic<uint64_t> g_packetBuilderGunNodes{0};
    std::atomic<uint64_t> g_packetBuilderLastAppliedMs{0};
    std::atomic<uint64_t> g_packetBuilderEligible{0};
    std::atomic<uint64_t> g_packetBuilderChiefContexts{0};
    std::atomic<uint64_t> g_packetBuilderEliteContexts{0};
    std::atomic<uint64_t> g_packetBuilderChiefApplied{0};
    std::atomic<uint64_t> g_packetBuilderEliteApplied{0};
    std::atomic<uint32_t> g_packetBuilderLastChiefFlags{0};
    std::atomic<uint32_t> g_packetBuilderLastEliteFlags{0};
    std::atomic<uint64_t> g_packetBuilderWeaponStateMiss{0};
    std::atomic<uint64_t> g_packetBuilderPublicationMiss{0};
    std::atomic<uint64_t> g_packetBuilderControllerSnapshotMiss{0};
    std::atomic<uint64_t> g_packetBuilderCarrierMiss{0};
    std::atomic<uint64_t> g_packetBuilderOwnershipMiss{0};
    std::atomic<uint32_t> g_packetBuilderRightDeltaMillimeters{0};
    std::atomic<uint32_t> g_packetBuilderLeftDeltaMillimeters{0};
    std::atomic<uint32_t> g_packetBuilderMaxRightDeltaMillimeters{0};
    std::atomic<uint32_t> g_packetBuilderMaxLeftDeltaMillimeters{0};
    using Halo2VisibleFirstPersonConsumerFn = void(__fastcall*)(
        uint32_t, uint32_t, uint32_t, int32_t, float*);
    void* g_visibleConsumerTarget = nullptr;
    std::atomic<uintptr_t> g_visibleConsumerOriginal{0};
    std::atomic<uint32_t> g_visibleConsumerActiveCallbacks{0};
    std::atomic<uint64_t> g_visibleConsumerCalls{0};
    std::atomic<uint64_t> g_visibleConsumerHandsApplied{0};
    std::atomic<uint64_t> g_visibleConsumerGunsApplied{0};

    struct Halo2VisibleConsumerContext
    {
        bool valid = false;
        uint32_t user = UINT32_MAX;
        uint32_t unitObject = UINT32_MAX;
        uint32_t weaponObject = UINT32_MAX;
        uint32_t secondaryWeaponObject = UINT32_MAX;
        uint32_t secondaryGunCount = 0;
        const int32_t* secondaryHandsRemap = nullptr;
        Halo2FirstPersonArmBinding secondaryBinding{};
        uint32_t handsCount = 0;
        uint32_t gunCount = 0;
        const int32_t* handsRemap = nullptr;
        Halo2FirstPersonArmBinding binding{};
        Halo2CameraBasis renderCamera{};
        Halo2CameraBasis rightCarrier{};
        Halo2CameraBasis leftCarrier{};
        bool muzzleVisualFrameValid=false;
        Halo2CameraBasis muzzleNeutralCarrier[2]{};
        Halo2CameraBasis muzzleVisualCarrier[2]{};
        Halo2CameraBasis collisionRightCarrier{};
        Halo2CameraBasis collisionLeftCarrier{};
        float rightCollisionCorrection[3]{};
        float leftCollisionCorrection[3]{};
        bool twoHandAimActive = false;
        // Persistent support grip: visible presentation follows the valid
        // weapon relationship, not the aim-authority result.
        bool supportGripAttached = false;
        bool handAlignment = false;
        ControllerFingerInput supportFingerInput{};
        contact_melee::Frame contactFrames[2]{};
        uint32_t muzzleGeneration=0;
        uint64_t muzzleSpace=0,muzzleSerial=0;
        int64_t muzzleTimeNs=0;
        bool muzzleLeftHanded=false;
        float rightScale = 1.0f;
        float leftScale = 1.0f;
        float worldScale = 1.0f;
        bool handsApplied = false;
        bool gunApplied = false;
        Halo2FirstPersonTransform rightDelta{};
        bool handsDeferred = false;
        uint32_t handsModelObject = UINT32_MAX;
        uint32_t handsOwnerObject = UINT32_MAX;
        float* handsMatrices = nullptr;
        float* primaryGunMatrices = nullptr;
        uint32_t primaryGunModelObject = UINT32_MAX;
        bool primaryGunDeferred = false;
    };
    thread_local Halo2VisibleConsumerContext g_visibleConsumerContext{};
#include "halo2_weapon_animation.inl"
    struct Halo2CollisionResult;
    bool Halo2ContactMeleeReady();
    bool InstallHalo2ContactMelee(uintptr_t base,size_t size,uint32_t generation);
    bool RemoveHalo2ContactMelee();
    void ReportHalo2ContactMelee();
    void Halo2PrepareContactFrames(const Halo2ObserverPosePublication& publication,
        bool independentPrimary,Halo2VisibleConsumerContext& context);
    void Halo2PublishContactSamples(int hand,const Halo2VisibleConsumerContext& context,
        const float points[][3],uint32_t count);
    void Halo2ContactTick(uint32_t unit);
    bool Halo2RedirectContactVector(uintptr_t caller,uint32_t flags,
        int32_t ignoredA,int32_t ignoredB,Halo2CollisionResult* result,uint8_t& returned);


    struct Halo2ClassicPacketContext
    {
        bool valid = false;
        bool eyeActive = false;
        float* hands = nullptr;
        float* gun = nullptr;
        float* secondaryGun = nullptr;
        uint32_t handsCount = 0;
        uint32_t gunCount = 0;
        uint32_t secondaryGunCount = 0;
        uint64_t appliedAtMs = 0;
        float handsBackup[
            kHalo2FirstPersonPaletteCapacity *
            kHalo2FirstPersonNodeFloats]{};
        float gunBackup[
            kHalo2FirstPersonPaletteCapacity *
            kHalo2FirstPersonNodeFloats]{};
        float secondaryGunBackup[
            kHalo2FirstPersonPaletteCapacity *
            kHalo2FirstPersonNodeFloats]{};
    };
    thread_local Halo2ClassicPacketContext g_classicPacketContext{};
    std::atomic<uint64_t> g_classicPacketCalls{0};
    std::atomic<uint64_t> g_classicPacketApplied{0};
    std::atomic<uint64_t> g_classicEyeCalls{0};
    std::atomic<uint64_t> g_classicEyeCompensated{0};
    std::atomic<uint64_t> g_classicEyeNoPacket{0};
    std::atomic<uint64_t> g_classicEyeNoPass{0};
    std::atomic<uint64_t> g_classicEyeRefused{0};

    using Halo2NativeAimUpdateFn = uint64_t(__fastcall*)(uint32_t);
    using Halo2ObjectDatumAccessorFn = void*(__fastcall*)(const void*);
    void* g_nativeAimTarget = nullptr;
    std::atomic<uintptr_t> g_nativeAimOriginal{0};
    std::atomic<uintptr_t> g_objectDatumAccessor{0};
    std::atomic<bool> g_vehicleSeatVerified{false};
    // One atomic publication: native sample time in the upper bits, seated in
    // bit zero. Input polling never dereferences a live engine object.
    std::atomic<uint64_t> g_vehicleSeatSample{0};
    // The game tick that sample was taken in (-1: unread), for Halo2SeatHeldThroughStall.
    std::atomic<int64_t> g_vehicleSeatSampleTick{-1};
    // Decoded from the independently verified generic loaded-tag resolver.
    // H2's tag blocks store word-addresses relative to this base.
    std::atomic<uintptr_t> g_halo2TagDataBaseSlot{0};

    // Official H2EK collisions.cpp `collision_test_vector` and
    // object_set_velocity(object, real, real, real), mapped independently to
    // the pinned retail module.  This is an optional, fail-open presentation
    // transaction: losing it leaves the camera, packets, aim and OpenXR live.
    struct Halo2CollisionResult
    {
        int32_t type = 0;
        float fraction = 1.0f;
        float position[3]{};
        uint8_t remainderToObject[0x40 - 0x14]{};
        int32_t objectIndex = -1;
        uint8_t remainder[0x60 - 0x44]{};
    };
    static_assert(sizeof(Halo2CollisionResult) == 0x60);
    static_assert(offsetof(Halo2CollisionResult, objectIndex) == 0x40);
    using Halo2CollisionVectorFn = uint8_t(__fastcall*)(
        uint32_t, const float*, const float*, int32_t, int32_t,
        Halo2CollisionResult*);
    using Halo2ObjectSetLocalVelocityFn = void(__fastcall*)(
        uint32_t, float, float, float);

    struct Halo2CollisionTargetPublication
    {
        std::atomic<uint32_t> version{0};
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        uint32_t sampleCount = 0;
        int32_t ignoredUnit = -1;
        int32_t ignoredWeapon = -1;
        float samples[kHalo2WorldCollisionMaxSamples][3]{};
    };
    struct Halo2CollisionCorrectionPublication
    {
        std::atomic<uint32_t> version{0};
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        bool contact = false;
        float source[3]{};
        float correction[3]{};
    };
    struct Halo2CollisionWorkerHand
    {
        ContactReleaseSmoothing smoothing{};
        bool seeded = false;
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        uint32_t sampleCount = 0;
        int32_t ignoredUnit = -1;
        int32_t ignoredWeapon = -1;
        float accepted[kHalo2WorldCollisionMaxSamples][3]{};
    };
    struct Halo2WorldCollisionFeature
    {
        void* target = nullptr;
        Halo2CollisionVectorFn original = nullptr;
        Halo2ObjectSetLocalVelocityFn objectSetLocalVelocity = nullptr;
        uint32_t generation = 0;
        std::atomic<bool> installed{false};
        std::atomic<bool> active{false};
        std::atomic<uint32_t> callbacks{0};
        std::atomic<uint64_t> nextQueryAtMs{0};
        Halo2CollisionTargetPublication targetPublication[2]{};
        Halo2CollisionCorrectionPublication correction[2]{};
        Halo2CollisionWorkerHand worker[2]{};
        std::atomic<uint64_t> queries{0};
        std::atomic<uint64_t> contacts[2]{};
        std::atomic<uint64_t> weaponContacts{0};
        std::atomic<uint64_t> weaponBoundsPublished{0};
        std::atomic<uint64_t> weaponBoundsFallbacks{0};
        std::atomic<uint64_t> applied[2]{};
        std::atomic<uint64_t> pushes{0};
        std::atomic<uint64_t> failures{0};
    };
    Halo2WorldCollisionFeature g_halo2WorldCollision;
    thread_local bool g_halo2CollisionOwnQuery = false;
    constexpr uint32_t kHalo2CollisionVectorRva = 0x0075B850;
    constexpr uint32_t kHalo2ObjectSetLocalVelocityRva = 0x0090A000;
    constexpr uint32_t kHalo2CollisionFlags = 0x2480000Fu;
    // THE SIGHT TRACE'S FLAGS: the PROJECTILE's own. The world-collision / melee flags above stop on every
    // biped; Halo 2's own rounds do not: their flight query adds bit 20, which skips a biped whose data +0x10A
    // has bit 2 (collision_test_vector's per-object test, 0x75C616; by position and by CE's source the damage
    // flags' DEAD bit -- read from code, the name inferred). Traced with the flags above, a body in front of a
    // far target pulls the convergence onto itself where the round flies on. Whatever that bit excludes, the
    // game's own rounds exclude: with the projectile's flags the intent ray measures the range a shot really
    // reaches. The projectile's flags are RESOLVED from the code that issues them, never baked:
    //   1. ANCHOR  the projectile flight wrapper's call (retail 0x75A164..0x75A185: `mov [rsp+28h],rax;
    //              mov ecx,imm32; mov eax,[rsp+D0h]; movss [rsp+34h],xmm1; mov [rsp+20h],eax;
    //              movss [rsp+38h],xmm0; call`) matches exactly once in the image, immediate wildcarded;
    //   2. CALLEE  its call reaches the collision_test_vector this install signature-matched and hooked;
    //   3. VALUE   the immediate is this lane's own pinned flags plus bit 20 alone, and decodes to structure +
    //              bipeds + vehicles (sight_hit::Halo2ProjectileFlagsAgree). The engine-ray census measured the
    //              same value on every shot (caller +0x75A18A, 0x2490000F; ALL-TITLES-AIM-2026-09-29.md).
    // Any rung failing keeps kHalo2CollisionFlags as the fallback (they hit every object type too; only the
    // projectile's biped exception is lost), and the install line says which.
    // ADDR-HYGIENE: resolved + guarded (unique signature, call edge to the hooked function, value agreement
    // with an independently pinned value; 0 = unresolved, the fallback flags).
    std::atomic<uint32_t> g_halo2SightProjectileFlags{0};
    // Both values the trace can run with stop on the level, on characters and on vehicles.
    static_assert(sight_hit::HitsUnitsAndVehicles(sight_hit::DecodeTraceFlags(
        sight_hit::Engine::Halo2, kHalo2CollisionFlags)));
    static_assert(sight_hit::Halo2ProjectileFlagsAgree(
        kHalo2CollisionFlags | sight_hit::kHalo2DeadBipedCullBit, kHalo2CollisionFlags));
    constexpr uint64_t kHalo2CollisionPublicationMaxAgeMs = 150;
    constexpr uint64_t kHalo2CollisionQueryIntervalMs = 33;
    constexpr float kHalo2CollisionHapticAmplitude = 0.18f;
    std::atomic<uint32_t> g_nativeAimActiveCallbacks{0};
    std::atomic<uint64_t> g_nativeAimCalls{0};
    std::atomic<uint64_t> g_nativeAimApplied{0};
    std::atomic<uint64_t> g_nativeAimNonOwned{0};
    std::atomic<uint64_t> g_nativeAimStockPassed{0};
    std::atomic<uint64_t> g_nativeAimRefused{0};
    // Trace-and-reaim (sight_layer.h): on-foot unit aims written converged on the traced point, and
    // written with the fixed-range steer because no fresh trace could resolve them.
    std::atomic<uint64_t> g_nativeAimConverged{0};
    std::atomic<uint64_t> g_nativeAimFixedRange{0};
    // The held weapon, published by the first-person packet builder, so the sight trace can ignore it
    // (a trace from the eye can pass through the character's own gun when the hand points low).
    std::atomic<uint32_t> g_sightWeaponObject{UINT32_MAX};
    std::atomic<uint64_t> g_sightWeaponAtMs{0};

    // E-H2-77: the H2EK/retail central aim-assist calculation. This optional
    // transaction only neutralizes its outputs for local user 0 while the
    // controller sight line owns Halo 2 VR. Every other call stays stock.
    using Halo2AimAssistCalculateFn = void(__fastcall*)(
        uint32_t, float*, Halo2AimAssistTargetingResult*);
    using Halo2AimAssistViewDirectionFn = void(__fastcall*)(
        uint32_t, float*);
    void* g_aimAssistTarget = nullptr;
    std::atomic<uintptr_t> g_aimAssistOriginal{0};
    std::atomic<uint32_t> g_aimAssistActiveCallbacks{0};
    std::atomic<uint64_t> g_aimAssistCalls{0};
    std::atomic<uint64_t> g_aimAssistSuppressed{0};
    std::atomic<uint64_t> g_aimAssistTargetSelected{0};
    std::atomic<uint64_t> g_aimAssistNoTarget{0};
    std::atomic<uint64_t> g_aimAssistStock{0};
    std::atomic<uint64_t> g_aimAssistRefused{0};
    void* g_aimAssistViewDirectionTarget = nullptr;
    std::atomic<uintptr_t> g_aimAssistViewDirectionOriginal{0};
    std::atomic<uint32_t> g_aimAssistViewDirectionActiveCallbacks{0};
    std::atomic<uint64_t> g_aimAssistViewDirectionCalls{0};
    std::atomic<uint64_t> g_aimAssistViewDirectionApplied{0};
    std::atomic<uint64_t> g_aimAssistViewDirectionStock{0};
    std::atomic<uint64_t> g_aimAssistViewDirectionRefused{0};

    struct Halo2AimAssistControllerRayScope
    {
        bool active = false;
        bool applied = false;
        float direction[3]{};
    };
    thread_local Halo2AimAssistControllerRayScope
        g_aimAssistControllerRayScope{};

    #include "halo2_independent_query_context.inl"

    struct Halo2FinalPaletteContext
    {
        bool valid = false;
        const float* source = nullptr;
        uint32_t count = 0;
        Halo2FirstPersonArmBinding binding{};
        Halo2CameraBasis rightCarrier{};
        Halo2CameraBasis leftCarrier{};
        bool rightDeltaValid = false;
        bool leftDeltaValid = false;
        float rightRotation[9]{};
        float leftRotation[9]{};
        float rightStockPosition[3]{};
        float leftStockPosition[3]{};
        float rightDesiredPosition[3]{};
        float leftDesiredPosition[3]{};
    };
    thread_local Halo2FinalPaletteContext g_finalPaletteContext{};
    std::atomic<uintptr_t> g_interpolatorResetAddress{0};
    // C-H2-43: H2EK-proven firing-only shot-direction result. Independent of
    // observer ownership and intentionally unable to move a camera or XInput.
    using Halo2WeaponAimHelperFn = void(__fastcall*)(
        uint32_t, float*, float*, uint64_t, float*, uint8_t, uint8_t, uint8_t);
    void* g_weaponAimTarget = nullptr;
    std::atomic<uintptr_t> g_weaponAimOriginal{0};
    std::atomic<uint32_t> g_weaponAimActiveCallbacks{0};
    std::atomic<uint64_t> g_weaponAimCalls{0};
    std::atomic<uint64_t> g_weaponAimApplied{0};
    std::atomic<uint64_t> g_weaponAimStock{0};
    std::atomic<uint64_t> g_weaponAimNonOwned{0};
    std::atomic<uint64_t> g_weaponAimNoOwnedUnit{0};
    std::atomic<uint64_t> g_weaponAimExceptions{0};
    std::atomic<uint64_t> g_weaponAimChanged{0};
    std::atomic<uint32_t> g_weaponAimMaxDeflectionMilliDegrees{0};
    uint64_t g_lastWitnessedReported = 0;

    // E-H2-6 publication: seqlock (even = stable), plain payload.
    std::atomic<uint32_t> g_publicationVersion{0};
    Halo2ObserverPosePublication g_publication{};
    std::atomic<uint64_t> g_publicationTornReads{0};
    // E-H2-21: the last few publications, newest first, so a renderer on
    // another thread can pick the sample the engine's OWN frame camera was
    // built from instead of whatever was published most recently.
    constexpr unsigned kPublicationRing = 8;
    Halo2SnapTurnState g_snapTurn{}; // observer thread only
    Halo2VehicleViewState g_vehicleView{}; // observer thread only
    std::atomic<bool> g_vehicleFrameVerified{false};
    std::atomic<bool> g_vehicleIdentityVerified{false};
    std::atomic<uint64_t> g_vehicleViewApplied{0},g_vehicleViewRefused{0};
    // Frames the seat was kept through a stalled simulation (Halo2SeatHeldThroughStall).
    std::atomic<uint64_t> g_vehicleViewHeld{0};
    bool ReadVehicleViewFrame(Halo2VehicleViewKey&,Halo2CameraBasis&) noexcept;
    // Roomscale re-seats of the reference (ApplyHeadPose): how many, and how far
    // the head had moved from the reference between its capture and the follow's
    // first admission -- the offset a re-seat removes from between view and legs.
    std::atomic<uint64_t> g_roomscaleReseats{0};
    std::atomic<uint32_t> g_roomscaleReseatMovedBits{0};
    std::atomic<uint64_t> g_snapTurns{0};
    std::atomic<uint64_t> g_snapSettled{0};
    std::atomic<uint32_t> g_ringVersion[kPublicationRing]{};
    Halo2ObserverPosePublication g_ring[kPublicationRing]{};
    std::atomic<unsigned> g_ringHead{0};

    void Halo2ApplyPhysicalCrouchCamera(float* position,float physicalDown,bool allowed) noexcept;

    // The simulation's clock as the camera was built: the game tick (game_time_globals +0x08; the slot
    // C-H2-1 proved from two independent anchors before this core could arm) and MCC's frame-interpolator factor
    // (0x164B300, E-H2-30: written once a frame, after the ticks, just before the observer update). Read guarded; -1 /
    // NaN when unreadable. ADDR-HYGIENE: guarded (the C-H2-1 slot proof; the factor is range-checked to [0, 1] and only
    // reported, never acted on).
    void Halo2ReadGameClock(int64_t& tick, float& factor) noexcept
    {
        tick = -1;
        factor = NAN;
        const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
        if (!base)
            return;
        __try
        {
            const uint8_t* time = *reinterpret_cast<const uint8_t* const*>(base + kHalo2GameTimeSlotRva);
            if (time && time[kHalo2GameTimeInitializedOffset] == 1)
                tick = *reinterpret_cast<const uint32_t*>(time + kHalo2GameTimeCurrentTickOffset);
            const float f = *reinterpret_cast<const float*>(base + kHalo2FrameInterpolatorFactorRva);
            if (std::isfinite(f) && f >= 0.0f && f <= 1.0f)
                factor = f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            tick = -1;
            factor = NAN;
        }
    }

    // THE SEAT THROUGH A STALLED SIMULATION. The seat sample is taken by the simulation, so it stops when the
    // simulation does -- a hitch, or in a co-op session this machine waiting for another's input -- while frames
    // and the input poll carry on. The player is still in the seat the last tick left him in; read as "not
    // seated", the view falls to the stock camera (whose heading under the seat drive is the hand's aim) and the
    // on-foot stick lanes take the seat's sticks until the next tick. True while `sample` said seated, is past
    // its window, and no tick beyond the one it was taken in has completed: a tick that ran without refreshing it
    // means the unit is no longer the player's. Not through a menu, a pause or the theater, where the seat's
    // consumers stand down as they always have. Any thread (guarded reads only).
    bool Halo2SeatHeldThroughStall(uint64_t sample, uint64_t now) noexcept
    {
        const uint64_t at = sample >> 1;
        if (!(sample & 1u) || !at || now < at || now - at <= 100)
            return false;
        if (Menu_IsOpen() || VR_IsPausePresentation() || VR_IsCutsceneTheaterActive())
            return false;
        const int64_t sampled = g_vehicleSeatSampleTick.load(std::memory_order_relaxed);
        int64_t tick = -1;
        float factor = 0.0f;
        Halo2ReadGameClock(tick, factor);
        return sampled >= 0 && tick >= sampled && tick - sampled <= 1;
    }

    void PublishPose(
        uint32_t generation, uint64_t serial, const Halo2CameraBasis& stock,
        const Halo2CameraBasis& tracked, const HeadReference& reference,
        const Halo2SynchronousVrRenderSnapshot& sample,
        const Halo2CameraBasis* vehicleReference=nullptr,
        const Halo2VehicleViewKey* vehicleKey=nullptr,
        const Halo2CameraBasis* footReference=nullptr,
        float viewLiftZ=0.0f) noexcept
    {
        g_publicationVersion.fetch_add(1, std::memory_order_acq_rel);
        g_publication.generation = generation;
        g_publication.serial = serial;
        g_publication.index =
            g_publicationIndex.fetch_add(1, std::memory_order_relaxed) + 1;
        g_publication.stock = stock;
        g_publication.tracked = tracked;
        g_publication.vehicleReferenceValid=vehicleReference&&vehicleKey;
        g_publication.vehicleReference=vehicleReference?*vehicleReference:Halo2CameraBasis{};
        g_publication.footReferenceValid=footReference!=nullptr;
        g_publication.footReference=footReference?*footReference:Halo2CameraBasis{};
        g_publication.viewLiftZ=std::isfinite(viewLiftZ)?viewLiftZ:0.0f;
        {
            // Flat parity: the engine eye's track on this (the observer) thread, on the sight clock.
            static shot_lag::EyeTrack s_engineEyeTrack;
            static uint32_t s_engineEyeGeneration = 0;
            if (s_engineEyeGeneration != generation)
            {
                s_engineEyeTrack.Reset();
                s_engineEyeGeneration = generation;
            }
            float engineEye[3]{};
            Halo2EngineEyePosition(g_publication, engineEye);
            g_publication.publishedUs = Sight_NowUs();
            s_engineEyeTrack.Push(g_publication.publishedUs, engineEye);
            g_publication.engineEyeVelValid =
                s_engineEyeTrack.Velocity(120000, 30000, g_publication.engineEyeVel);
            if (!g_publication.engineEyeVelValid)
                std::memset(g_publication.engineEyeVel, 0, sizeof(g_publication.engineEyeVel));
            Halo2ReadGameClock(g_publication.gameTick, g_publication.interpFactor);
        }
        g_publication.vehicleUnit=vehicleKey?vehicleKey->unit:UINT32_MAX;
        g_publication.vehicleParent=vehicleKey?vehicleKey->parent:UINT32_MAX;
        g_publication.vehicleSeat=vehicleKey?vehicleKey->seat:-1;
        g_publication.snapTurnPending = g_snapTurn.pending;
        g_publication.snapTurnTargetYaw = g_snapTurn.targetYaw;
        std::memcpy(g_publication.referenceOrientation, reference.orientation,
                    sizeof(g_publication.referenceOrientation));
        std::memcpy(g_publication.referencePosition, reference.position,
                    sizeof(g_publication.referencePosition));
        // E-H2-18: the sample this tracked camera was composed from, so the
        // Saber scene (another thread, another serial) renders the eyes from
        // the head pose the weapon was placed against and submits them as
        // that pose.
        Halo2ObserverPoseSnapshot& snapshot = g_publication.snapshot;
        snapshot.valid = sample.headPoseValid;
        snapshot.predictedDisplayTimeNs=sample.predictedDisplayTimeNs;
        snapshot.trackingSpaceEpoch=sample.trackingSpaceEpoch;
        std::memcpy(snapshot.headOrientation, sample.headOrientation,
                    sizeof(snapshot.headOrientation));
        std::memcpy(snapshot.headPosition, sample.headPosition,
                    sizeof(snapshot.headPosition));
        for (int eye = 0; eye < 2; ++eye)
        {
            std::memcpy(snapshot.eyeOffsetPosition[eye], sample.eyes[eye].position,
                        sizeof(snapshot.eyeOffsetPosition[eye]));
            std::memcpy(snapshot.eyeOffsetOrientation[eye],
                        sample.eyes[eye].orientation,
                        sizeof(snapshot.eyeOffsetOrientation[eye]));
            std::memcpy(snapshot.eyePosition[eye], sample.eyes[eye].absolutePosition,
                        sizeof(snapshot.eyePosition[eye]));
            std::memcpy(snapshot.eyeOrientation[eye],
                        sample.eyes[eye].absoluteOrientation,
                        sizeof(snapshot.eyeOrientation[eye]));
        }
        snapshot.rightAimValid = sample.rightAimValid;
        std::memcpy(snapshot.rightAimOrientation,
                    sample.rightAimOrientation,
                    sizeof(snapshot.rightAimOrientation));
        std::memcpy(snapshot.rightAimPosition, sample.rightAimPosition,
                    sizeof(snapshot.rightAimPosition));
        snapshot.twoHandAimActive = sample.twoHandAimActive;
        snapshot.handAlignment = sample.handAlignment;
        std::memcpy(snapshot.controllerFingers,sample.controllerFingers,
                    sizeof(snapshot.controllerFingers));
        snapshot.independentRightAimValid = sample.independentRightAimValid;
        std::memcpy(snapshot.independentRightAimOrientation,
            sample.independentRightAimOrientation, sizeof(snapshot.independentRightAimOrientation));
        snapshot.leftControllerValid = sample.leftControllerValid;
        std::memcpy(snapshot.leftControllerOrientation,
                    sample.leftControllerOrientation,
                    sizeof(snapshot.leftControllerOrientation));
        std::memcpy(snapshot.leftControllerPosition,
                    sample.leftControllerPosition,
                    sizeof(snapshot.leftControllerPosition));
        g_publicationVersion.fetch_add(1, std::memory_order_acq_rel);
        // The ring keeps one entry per DISTINCT tracked camera (the observer
        // runs many times per frame with the same sample).
        const unsigned head = g_ringHead.load(std::memory_order_acquire);
        const unsigned last = (head + kPublicationRing - 1) % kPublicationRing;
        const bool same = head != 0 &&
            std::memcmp(&g_ring[last].tracked, &tracked, sizeof(tracked)) == 0 &&
            g_ring[last].serial == serial;
        const unsigned slot = same ? last : head % kPublicationRing;
        g_ringVersion[slot].fetch_add(1, std::memory_order_acq_rel);
        g_ring[slot] = g_publication;
        g_ringVersion[slot].fetch_add(1, std::memory_order_acq_rel);
        if (!same)
            g_ringHead.store(head + 1, std::memory_order_release);
    }

    void ClearPublication() noexcept
    {
        g_publicationVersion.fetch_add(1, std::memory_order_acq_rel);
        g_publication = {};
        g_publicationVersion.fetch_add(1, std::memory_order_acq_rel);
        for (unsigned i = 0; i < kPublicationRing; ++i)
        {
            g_ringVersion[i].fetch_add(1, std::memory_order_acq_rel);
            g_ring[i] = {};
            g_ringVersion[i].fetch_add(1, std::memory_order_acq_rel);
        }
        g_ringHead.store(0, std::memory_order_release);
    }

    std::atomic<uint64_t> g_callbacks{0};
    std::atomic<uint64_t> g_appliedPoses{0};
    std::atomic<uint64_t> g_rejectedSamples{0};
    std::atomic<uint64_t> g_unreadableSamples{0};
    std::atomic<uint64_t> g_exceptions{0};
    uint64_t g_lastReportMs = 0;
    uint64_t g_lastAppliedReported = 0;
    uint64_t g_lastCallbacks = 0;

    // Written and read only from the owned detour, which the engine calls on
    // one thread inside observer_update_all.
    HeadReference g_reference{};
    // Set when g_reference is captured; cleared once roomscale's follow first
    // admits after it (ApplyHeadPose). Same thread as g_reference.
    bool g_referenceAwaitingFollow = false;

    // HALO 2 DIRECT DRIVE (halo2_direct_drive.inl): the aim goes into player control's desired angles,
    // right after the native accumulator. Its state is here because the observer reads it -- the view
    // takes its own heading once the drive is armed -- while the hook, the arm gate and the install are in
    // the .inl. The record: *slot + user * 0xB8, desired yaw +0x38 / pitch +0x3C (radians), the eased pitch
    // limits +0xA8 (low) / +0xAC (high).
    constexpr uintptr_t kHalo2PcStride = 0xB8;
    constexpr uintptr_t kHalo2PcDesiredYaw = 0x38, kHalo2PcDesiredPitch = 0x3C;
    constexpr uintptr_t kHalo2PcPitchLow = 0xA8, kHalo2PcPitchHigh = 0xAC;
    struct Halo2DriveState
    {
        void* target = nullptr;
        uint32_t entryRva = 0;         // where the install found the accumulator in this module
        uintptr_t slot = 0;            // the record's global pointer (from the prologue's rip load)
        uintptr_t controlReturn = 0;   // the control update's return address: the only call written after
        std::atomic<bool> installed{false}, armed{false};
        std::atomic<uintptr_t> expected{0};
        uint32_t agree = 0;            // sim thread only
        std::atomic<uint64_t> footControlMs{0};   // the control update last ran for an on-foot local unit
        // ...and found the hand's aim to write: VR owns Halo 2's aim (Insert switches it) and a hand direction
        // could be built (it needs both controllers tracked). The view holds its own heading -- and the right
        // stick -- only while this is fresh, so without a hand the stick aims, as with the drive off.
        std::atomic<uint64_t> handAimMs{0};
        std::atomic<uint64_t> liveMs{0};          // the last write
        // The game last ran the owned user's control update (on foot or seated; with the drive on or off). The
        // game skips that call while the director holds the player, so a stale stamp is a cinematic. One reader:
        // the head-relative motion tracker (halo2_head_relative.inl).
        std::atomic<uint64_t> controlMs{0};
        std::atomic<uint32_t> callbacks{0};
        std::atomic<uint32_t> calls{0}, kicks{0}, writes{0}, convergedWrites{0}, waits{0}, arms{0},
            disarms{0}, seated{0}, faults{0};
        std::atomic<float> nativeYaw{0.0f}, nativePitch{0.0f}, aimYaw{0.0f}, aimPitch{0.0f},
            wroteYaw{0.0f}, wrotePitch{0.0f};
        // SEATS (halo2_direct_drive.inl). Sim thread: this seat stint's per-axis check and the yaw latch (pitch is
        // clamped exactly to the record's own seat limits instead). Read by the input thread: when each axis
        // was last written, so the stick servo stands down on it.
        seat_drive::StintCheck stint{};
        bool stintActive = false;
        uint32_t stintKey = 0;
        bool stintPitchFailCounted = false;
        aim_write::LimitLatch yawLatch{};
        uint32_t yawTicks = 0;
        uint64_t stintSample = 0;      // the publication the stint last judged (a repeat is no evidence)
        uint32_t stintStaleUpdates = 0; // consecutive updates of this stint with its seat's view down
        std::atomic<uint64_t> seatYawMs{0}, seatPitchMs{0};
        std::atomic<uint32_t> seatWrites{0}, seatHeld{0}, seatWaits{0}, seatStints{0}, seatYawChecks{0},
            seatPitchChecks{0}, seatPitchFails{0};
        // Seat writes whose BOTH axes carried the converged aim (seat_converge_logic.h).
        std::atomic<uint32_t> seatConvergedWrites{0};
        // The seat proof's reference (seat_converge::SeatAimReference): seated updates judged against the unit's own
        // aiming vector, against the observer camera (the vector did not read), and the largest angle the camera sat
        // off the unit's aim in the window (degrees; about 5.1 measured in every seat).
        std::atomic<uint32_t> seatUnitAimRefs{0}, seatCameraRefs{0};
        std::atomic<float> seatCameraOffMaxDeg{0.0f};
        // THE SEAT'S STOPS for the reticle (seat_limit_publication.h): after each seat write, the yaw latch's stop and
        // the record's own eased pitch limits, in the record's frame, with the hand's world aim the write was for.
        // Not for an air vehicle (stintAircraft, read once per stint): no rotational limit is shown there.
        SeatStopsPublication stops{};
        bool stintAircraft = false;
    };
    Halo2DriveState g_halo2Drive;
    // The view's own heading on foot under the drive (halo2_foot_view.h). Observer thread only.
    Halo2FootViewState g_footView{};
    // A seat taken from it starts at that heading (Halo2VehicleViewState::Build's seed). Observer thread.
    bool g_footSeatSeedValid = false;
    float g_footSeatSeed = 0.0f;
    // When the observer last published a view at its own heading. The drive writes only while this is
    // fresh (written before it, the stock camera -- built from the record -- would swing the view with the
    // hand), and while it is fresh the right stick turns that heading instead of reaching the game.
    std::atomic<uint64_t> g_footViewEngagedMs{0};
    std::atomic<uint32_t> g_footViewEngages{0}, g_footViewReleases{0}, g_footViewFolds{0};
    // The same for a seat: the observer last published the seat's own view (Halo2VehicleViewState, the
    // vehicle reference), whose heading never reads the native look. The seat drive writes only while this
    // is fresh: without it the view is the stock camera, which the record turns.
    std::atomic<uint64_t> g_seatViewEngagedMs{0};

    bool Halo2DriveArmedForView() noexcept
    {
        return g_config.aim_direct_drive && g_halo2Drive.installed.load(std::memory_order_acquire) &&
            g_halo2Drive.armed.load(std::memory_order_acquire);
    }

    // Within ~4 control ticks (the update runs every ~33 ms): a cutscene, which stops it, releases the
    // heading after at most this long -- a pause or a hitch does not, because the camera is still the
    // player's (UpdateFootView).
    bool Halo2DriveFootControlFresh(uint64_t now) noexcept
    {
        const uint64_t at = g_halo2Drive.footControlMs.load(std::memory_order_acquire);
        return at && now >= at && now - at <= 120;
    }

    bool Halo2DriveHandAimFresh(uint64_t now) noexcept
    {
        const uint64_t at = g_halo2Drive.handAimMs.load(std::memory_order_acquire);
        return at && now >= at && now - at <= 150;
    }

    // The stock camera is still the player's own: it looks where the record says (the game builds it from
    // the record), within `tolerance` radians. A director camera (a cutscene, death) does not.
    bool Halo2DriveCameraFollowsRecord(const Halo2CameraBasis& stock, float tolerance) noexcept
    {
        if (!g_halo2Drive.installed.load(std::memory_order_acquire) || !g_halo2Drive.slot)
            return false;
        float yaw = 0.0f, pitch = 0.0f;
        __try
        {
            const uintptr_t globals = *reinterpret_cast<const volatile uintptr_t*>(g_halo2Drive.slot);
            if (!globals)
                return false;
            const uintptr_t pc = globals + kOwnedUser * kHalo2PcStride;
            yaw = *reinterpret_cast<const volatile float*>(pc + kHalo2PcDesiredYaw);
            pitch = *reinterpret_cast<const volatile float*>(pc + kHalo2PcDesiredPitch);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        const float horizontal = std::hypot(stock.forward[0], stock.forward[1]);
        if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(horizontal) ||
            horizontal < 1.0e-4f)
            return false;
        const float cameraYaw = std::atan2(stock.forward[1], stock.forward[0]);
        const float cameraPitch = std::atan2(stock.forward[2], horizontal);
        return std::fabs(std::remainder(cameraYaw - yaw, 6.28318531f)) < tolerance &&
            std::fabs(cameraPitch - pitch) < tolerance;
    }

    HMODULE g_moduleReference = nullptr;
    void* g_target = nullptr;
    CoreState g_coreState = CoreState::StockFallback;
    uint32_t g_rejectedGeneration = 0;
    uint32_t g_manualRecoveryGeneration = 0;
    uint32_t g_armedLoggedGeneration = 0;

    bool ReadFloats(uintptr_t address, float* out, size_t count) noexcept
    {
        __try
        {
            for (size_t index = 0; index < count; ++index)
            {
                out[index] =
                    *reinterpret_cast<const volatile float*>(
                        address + index * sizeof(float));
                if (!std::isfinite(out[index]))
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteFloats(
        uintptr_t address, const float* values, size_t count) noexcept
    {
        for (size_t index = 0; index < count; ++index)
            if (!std::isfinite(values[index]))
                return false;
        __try
        {
            for (size_t index = 0; index < count; ++index)
            {
                *reinterpret_cast<volatile float*>(
                    address + index * sizeof(float)) = values[index];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ValidHeadPose(
        const float orientation[4], const float position[3]) noexcept
    {
        float lengthSquared = 0.0f;
        for (int index = 0; index < 4; ++index)
        {
            if (!std::isfinite(orientation[index]))
                return false;
            lengthSquared += orientation[index] * orientation[index];
        }
        if (!std::isfinite(lengthSquared) ||
            std::fabs(lengthSquared - 1.0f) > 0.05f)
        {
            return false;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(position[axis]) ||
                std::fabs(position[axis]) > kHalo2MaxHeadPositionMeters)
            {
                return false;
            }
        }
        return true;
    }

    bool ReadObserverPose(
        uintptr_t result, Halo2CameraBasis& basis) noexcept
    {
        return ReadFloats(
                   result + kHalo2ObserverResultPositionOffset,
                   basis.position, 3) &&
            ReadFloats(
                   result + kHalo2ObserverResultForwardOffset,
                   basis.forward, 3) &&
            ReadFloats(
                   result + kHalo2ObserverResultUpOffset, basis.up, 3) &&
            Halo2ValidateCameraBasis(basis);
    }

    // The three owned spans are written independently and each is checked
    // against the allow-list, so no future edit can widen this into a
    // whole-struct copy over engine-owned fields.
    bool WriteObserverPose(
        uintptr_t result, const Halo2CameraBasis& basis) noexcept
    {
        if (!Halo2ValidateCameraBasis(basis))
            return false;
        struct Span
        {
            uint32_t offset;
            const float* values;
        };
        const Span spans[kHalo2ObserverOwnedSpanCount] = {
            {kHalo2ObserverResultPositionOffset, basis.position},
            {kHalo2ObserverResultForwardOffset, basis.forward},
            {kHalo2ObserverResultUpOffset, basis.up},
        };
        for (const Span& span : spans)
        {
            if (!Halo2ObserverSpanWriteAllowed(
                    span.offset, kHalo2CameraVectorBytes))
            {
                return false;
            }
            if (!WriteFloats(result + span.offset, span.values, 3))
                return false;
        }
        return true;
    }

    void ApplyUpstreamVisibilityCover(
        uintptr_t result,
        const Halo2SynchronousVrRenderSnapshot& snapshot) noexcept
    {
        if (!kHalo2C71UpstreamVisibilityCoverEnabled)
            return;
        if (!snapshot.eyes[0].fovValid || !snapshot.eyes[1].fovValid ||
            !Halo2ObserverVisibilityFovWriteAllowed(
                kHalo2ObserverResultVerticalFovOffset, sizeof(float)))
        {
            g_visibilityCoverRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        float stock = 0.0f;
        float cover = 0.0f;
        if (!ReadFloats(
                result + kHalo2ObserverResultVerticalFovOffset, &stock, 1) ||
            !Halo2DeriveObserverVisibilityVerticalFov(
                snapshot.eyes[0].fov, snapshot.eyes[1].fov, stock, cover,true))
        {
            g_visibilityCoverRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Halo2SaberEyeCover required{};
        if (!Halo2DeriveSaberEyeCover(
                snapshot.eyes[0].fov, snapshot.eyes[1].fov, required))
        {
            g_visibilityCoverRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const float requiredRadians = required.halfVerticalRadians * 2.0f;
        uint32_t bits = 0;
        std::memcpy(&bits, &stock, sizeof(bits));
        g_visibilityCoverStockBits.store(bits, std::memory_order_relaxed);
        std::memcpy(&bits, &requiredRadians, sizeof(bits));
        g_visibilityCoverRequiredBits.store(bits, std::memory_order_relaxed);
        std::memcpy(&bits, &cover, sizeof(bits));
        g_visibilityCoverWrittenBits.store(bits, std::memory_order_relaxed);
        if (cover <= stock + 1.0e-6f)
        {
            g_visibilityCoverAlreadyWide.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!WriteFloats(
                result + kHalo2ObserverResultVerticalFovOffset, &cover, 1))
        {
            g_visibilityCoverRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_visibilityCoverApplied.fetch_add(1, std::memory_order_relaxed);
    }

    uintptr_t OriginalAddress() noexcept
    {
        return g_originalAddress.load(std::memory_order_acquire);
    }

    // ON FOOT UNDER DIRECT DRIVE: whether the view keeps its own heading (halo2_foot_view.h) this frame.
    // Taken once the drive is armed and the player's control update runs for an on-foot unit, from the
    // heading the view has now (the stock one plus any snap still in flight, or the seat's when stepping
    // out). Kept through a pause: the game stops ticking, but the camera is still the player's. Dropped for
    // a seat, which starts from it; for a director camera (a cutscene, death), which the view follows as it
    // always has; and when the drive disarms or is switched off, then folded into the reference so the view
    // does not jump to the stock heading, which is the hand's last aim.
    void UpdateFootView(uint32_t generation, const Halo2CameraBasis& stock, float stockYaw, bool vehicle,
        uint64_t now) noexcept
    {
        const bool armed = Halo2DriveArmedForView();
        const bool controls = Halo2DriveFootControlFresh(now);
        const bool handAim = Halo2DriveHandAimFresh(now);
        const bool playerCamera = Halo2DriveCameraFollowsRecord(stock, 0.087f);
        const bool sameGeneration = g_footView.active && g_footView.generation == generation;
        // While the control update runs, the drive must also have the hand's aim to write (VR aim on, both
        // controllers tracked), or the view would keep the stick from a game nothing writes the aim into.
        // While it does not run (a pause, a hitch), the player's camera keeps the heading, as before.
        const bool keep = armed && !vehicle && generation &&
            (sameGeneration ? (controls ? handAim : playerCamera) : (controls && handAim));
        if (keep)
        {
            g_footSeatSeedValid = false;
            if (!sameGeneration)
            {
                const float seed = g_vehicleView.active ? g_vehicleView.lastYaw
                    : stockYaw + g_snapTurn.ViewOffset(stockYaw);
                if (g_footView.Engage(generation, seed))
                {
                    g_snapTurn = {};   // the seed took any snap still in flight
                    g_footViewEngages.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return;
        }
        if (!g_footView.active)
            return;
        if (vehicle)
        {
            g_footSeatSeed = g_footView.yaw;
            g_footSeatSeedValid = true;
        }
        else if (playerCamera && g_footView.generation == generation)
        {
            float folded[4]{};
            if (Halo2SnapReference(g_reference.orientation,
                    Halo2FootViewState::Wrap(g_footView.yaw - stockYaw), folded))
            {
                std::memcpy(g_reference.orientation, folded, sizeof(folded));
                g_footViewFolds.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_footView.Release();
        g_footViewReleases.fetch_add(1, std::memory_order_relaxed);
    }

    void ApplyHeadPose(uintptr_t result) noexcept
    {
        Halo2SynchronousVrRenderSnapshot snapshot{};
        bool snapshotReady = false;
        __try
        {
            snapshotReady = VR_Halo2GetSynchronousRenderSnapshot(snapshot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshotReady = false;
        }
        if (!snapshotReady || !snapshot.headPoseValid ||
            !ValidHeadPose(snapshot.headOrientation, snapshot.headPosition))
        {
            g_rejectedSamples.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        Halo2CameraBasis stock{};
        if (!ReadObserverPose(result, stock))
        {
            g_unreadableSamples.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        if (g_recenterRequested.exchange(false, std::memory_order_acq_rel) ||
            !g_referenceValid.load(std::memory_order_acquire))
        {
            // The head's turn from the OLD reference (yaw about +y), for the seat heading kept below.
            float headTurn = NAN;
            if (g_referenceValid.load(std::memory_order_acquire))
            {
                const auto yawAboutUp = [](const float q[4]) {
                    const float back[3]{0.0f, 0.0f, -1.0f};
                    float f[3]{};
                    Halo2RotateVectorByQuaternion(q, back, f);
                    return std::atan2(-f[0], -f[2]);
                };
                headTurn = std::remainder(yawAboutUp(snapshot.headOrientation) -
                    yawAboutUp(g_reference.orientation), 6.28318531f);
            }
            std::memcpy(
                g_reference.orientation, snapshot.headOrientation,
                sizeof(g_reference.orientation));
            std::memcpy(
                g_reference.position, snapshot.headPosition,
                sizeof(g_reference.position));
            // The neutral height is the standing one, not a crouch the recenter happened to catch
            // (VR_RecenterStandingLiftM; the same height in every Halo 2 core, whenever each captures).
            g_reference.position[1] += VR_RecenterStandingLiftM(GameTitle::Halo2);
            g_referenceValid.store(true, std::memory_order_release);
            g_referenceAwaitingFollow = true;
            g_snapTurn={};
            // A seat keeps its heading through a recenter (Suspend): every unpause and theater exit recenters,
            // and the stock heading it would re-take is, under the seat drive, the hand's last aim.
            g_vehicleView.Suspend();
            // The player's own recenter re-aligns the seat to the vehicle's nose (where they now face is
            // forward); an automatic one (an unpause, a theater exit) keeps the view they were looking along.
            // The nose is read NOW (the hull the view last followed is stale while the F1 menu, a pause or the
            // theater held the view down -- exactly when the F1 button is pressed), and only when the hull has a
            // usable heading (a vehicle on its side does not).
            bool reseeded = false;
            if (g_manualRecenterRequested.exchange(false, std::memory_order_acq_rel))
            {
                Halo2VehicleViewKey noseKey{};
                noseKey.generation = g_generation.load(std::memory_order_acquire);
                noseKey.space = snapshot.trackingSpaceEpoch;
                Halo2CameraBasis noseHull{};
                if (ReadVehicleViewFrame(noseKey, noseHull) &&
                    std::hypot(noseHull.forward[0], noseHull.forward[1]) > 0.2f)
                {
                    const float noseYaw = std::atan2(noseHull.forward[1], noseHull.forward[0]);
                    g_vehicleView.ReseedResume(noseYaw, noseYaw);
                    reseeded = true;
                }
            }
            if (!reseeded)
                g_vehicleView.FoldResume(headTurn);
            // Under direct drive a recentre re-takes the view's heading from the body (the hand's aim), as
            // Halo 3's re-seeds its game yaw from the stock heading; UpdateFootView takes it again below.
            g_footView.Release();
            g_snapSettled.store(0,std::memory_order_release);
        }

        // ROOMSCALE TAKES "THE BODY IS UNDER ME" AT ITS FIRST ADMISSION, so the
        // reference has to be there too. It is captured above when the observer
        // installs (every level load) or on a recentre, but the follow treats its
        // own first admitted update as the moment the player stands over the body
        // (roomscale_logic.h, initialLean), and admission comes later: on foot,
        // palette armed, fresh input -- about 3 s after a load. Whatever the
        // player moves in between would stay between the view and the legs
        // until an L3+R3 recentre (0.45-0.79 m measured).
        // So the first admission after a capture re-seats the reference on the
        // head: one step of the view onto the body, as a recentre makes.
        const bool roomscaleAllowed = g_config.roomscale_movement &&
            Game_RoomscaleCameraAllowed(GameTitle::Halo2);
        if (roomscaleAllowed && g_referenceAwaitingFollow)
        {
            float moved = 0.0f;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float d = snapshot.headPosition[axis] - g_reference.position[axis];
                moved += d * d;
            }
            moved = std::sqrt(moved);
            uint32_t movedBits = 0;
            std::memcpy(&movedBits, &moved, sizeof(movedBits));
            g_roomscaleReseatMovedBits.store(movedBits, std::memory_order_relaxed);
            g_roomscaleReseats.fetch_add(1, std::memory_order_relaxed);
            std::memcpy(
                g_reference.position, snapshot.headPosition,
                sizeof(g_reference.position));
            // Its height stays the standing one the recenter took (VR_RecenterStandingLiftM), not a
            // crouch this step happened to catch.
            g_reference.position[1] += VR_RecenterStandingLiftM(GameTitle::Halo2);
            g_referenceAwaitingFollow = false;
        }

        const float stockYaw=std::atan2(stock.forward[1],stock.forward[0]);
        const bool vehicle=Halo2Observer6Dof_VehicleControlActive();
        if (vehicle && Halo2SeatHeldThroughStall(g_vehicleSeatSample.load(std::memory_order_acquire),
                GetTickCount64()))
            g_vehicleViewHeld.fetch_add(1, std::memory_order_relaxed);
        UpdateFootView(g_generation.load(std::memory_order_acquire), stock, stockYaw, vehicle,
            GetTickCount64());
        const uint64_t settled=g_snapSettled.exchange(0,std::memory_order_acq_rel);
        uint32_t targetBits=0;
        memcpy(&targetBits,&g_snapTurn.targetYaw,sizeof(targetBits));
        if (!g_footView.active && g_snapTurn.pending && settled==
                ((uint64_t(snapshot.generation)<<32)|targetBits))
        {
            // Retain sub-deadzone residual in the reference so parking the
            // native body does not nudge the precisely snapped view backward.
            float parkedReference[4]{};
            if (Halo2SnapReference(g_reference.orientation,g_snapTurn.ViewOffset(stockYaw),
                    parkedReference))
            {
                memcpy(g_reference.orientation,parkedReference,sizeof(parkedReference));
                g_snapTurn.pending=false;
            }
        }
        if (vehicle || g_footView.active) g_snapTurn.pending=false;
        const bool turnOwnsStick=Game_IsHeadTracking() && VR_IsStereoEnabled() &&
            !Menu_IsOpen() && !VR_IsPausePresentation() &&
            !VR_IsCutsceneTheaterActive() && !vehicle;
        if (g_footView.active)
        {
            // Under direct drive the right stick turns the view's own heading, smooth or snap, and never
            // reaches the game (Game_VrOwnsLookStick); the body follows the hand through the drive. Smooth
            // turning runs at turn_smooth_deg_s, as in the other titles, on a sub-frame clock.
            // The clock runs only while the view owns the heading, so it restarts at each engagement: leaving
            // a seat or a cutscene, or the drive re-arming, must not turn by the whole idle span at once.
            static LARGE_INTEGER s_turnFrequency{}, s_lastTurn{};
            static uint32_t s_turnEngagement = 0;
            if (!s_turnFrequency.QuadPart)
                QueryPerformanceFrequency(&s_turnFrequency);
            LARGE_INTEGER turnNow{};
            QueryPerformanceCounter(&turnNow);
            const bool sameEngagement = s_turnEngagement == g_footView.engagements;
            s_turnEngagement = g_footView.engagements;
            const float dt = sameEngagement && s_lastTurn.QuadPart && s_turnFrequency.QuadPart
                ? static_cast<float>(turnNow.QuadPart - s_lastTurn.QuadPart) /
                      static_cast<float>(s_turnFrequency.QuadPart)
                : 0.0f;
            s_lastTurn = turnNow;
            if (g_footView.Turn(turnOwnsStick && snapshot.turnPadValid, snapshot.turnX,
                    !g_config.turn_smooth, g_config.turn_snap_deg, g_config.turn_smooth_deg_s, dt))
                g_snapTurns.fetch_add(1,std::memory_order_relaxed);
        }
        else if (g_snapTurn.Update(snapshot.generation,snapshot.preparedSerial,stockYaw,
                turnOwnsStick,!g_config.turn_smooth,snapshot.turnPadValid,
                snapshot.turnX,g_config.turn_snap_deg))
            g_snapTurns.fetch_add(1,std::memory_order_relaxed);
        HeadReference effectiveReference=g_reference;
        if (!Halo2SnapReference(g_reference.orientation,g_snapTurn.ViewOffset(stockYaw),
                effectiveReference.orientation)) return;
        Halo2TrackedHeadInput head{};
        std::memcpy(
            head.orientation, snapshot.headOrientation,
            sizeof(head.orientation));
        std::memcpy(
            head.position, snapshot.headPosition, sizeof(head.position));
        std::memcpy(
            head.referenceOrientation, effectiveReference.orientation,
            sizeof(head.referenceOrientation));
        std::memcpy(
            head.referencePosition, g_reference.position,
            sizeof(head.referencePosition));
        head.positional = Game_IsPositionalTracking();
        head.worldScale = Game_GetWorldScale();

        // The character's own eye this frame, before any correction of ours: the leash below is held
        // against it, so it rides his crouch (as Reach's, Halo 3's, ODST's and Halo 4's do).
        const float characterEyeZ=stock.position[2];
        Halo2ApplyPhysicalCrouchCamera(stock.position,
            -std::clamp(head.position[1]-head.referencePosition[1],
                -kHalo2MaxHeadTranslationMeters,kHalo2MaxHeadTranslationMeters)*head.worldScale,
            head.positional&&!vehicle&&turnOwnsStick);
        // What it gave back: stock now sits that far above the engine's eye (published as viewLiftZ).
        float viewLiftZ=stock.position[2]-characterEyeZ;
        if(!std::isfinite(viewLiftZ)||viewLiftZ<0.0f||viewLiftZ>1.5f) viewLiftZ=0.0f;
        Halo2CameraBasis viewBase=stock,hull{};
        // On foot under direct drive the stock camera follows the hand; the view takes its own heading.
        const bool ownsFootReference=g_footView.active&&g_footView.Basis(stock,viewBase);
        Halo2VehicleViewKey vehicleKey{};
        bool ownsVehicleReference=false;
        const bool vehicleRequested=vehicle&&g_config.vehicle_vr_controls&&Game_IsHeadTracking()&&VR_IsStereoEnabled()&&
            !Menu_IsOpen()&&!VR_IsPausePresentation()&&!VR_IsCutsceneTheaterActive();
        if(vehicleRequested) {
            vehicleKey.generation=g_generation.load(std::memory_order_acquire);
            vehicleKey.space=snapshot.trackingSpaceEpoch;
            ownsVehicleReference=ReadVehicleViewFrame(vehicleKey,hull)&&
                g_vehicleView.Build(vehicleKey,stock,hull,g_config.vehicle_view_follow,
                    GetTickCount64(),viewBase,g_footSeatSeedValid?&g_footSeatSeed:nullptr);
            if(ownsVehicleReference) {g_footSeatSeedValid=false;g_vehicleViewApplied.fetch_add(1,std::memory_order_relaxed);}
            else {g_vehicleView={};g_vehicleViewRefused.fetch_add(1,std::memory_order_relaxed);}
        }
        // Still seated, or not known to have left (a menu, a pause, the theater -- the seat sample goes stale
        // while the game is paused): keep the seat's heading for its return. A fresh on-foot sample forgets it.
        else if(!Halo2Observer6Dof_OnFootFresh()) g_vehicleView.Suspend();
        else g_vehicleView={};
        Halo2CameraBasis tracked{};
        if (!Halo2BuildTrackedCenterCamera(viewBase, head, tracked))
        {
            g_rejectedSamples.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (g_config.roomscale_movement||g_config.physical_running)
        {
            const HeadReference beforeRoomscale = g_reference;
            // The camera's own admission, which also admits physical running (0.7.0) with roomscale off; the
            // first-admission re-seat above stays roomscale's alone.
            Roomscale_Camera(GameTitle::Halo2, Game_RoomscaleCameraVerdict(GameTitle::Halo2),
                stock.position, snapshot.headPosition, snapshot.headOrientation, tracked.forward,
                g_reference.position, head.worldScale);
            memcpy(head.referencePosition, g_reference.position, sizeof(head.referencePosition));
            memcpy(effectiveReference.position, g_reference.position, sizeof(effectiveReference.position));
            Halo2CameraBasis followed{};
            if (Halo2BuildTrackedCenterCamera(viewBase, head, followed))
                tracked = followed;
            else
            {
                // Optional-follow refusal preserves this frame's valid tracked camera.
                g_reference = beforeRoomscale;
                memcpy(effectiveReference.position, g_reference.position, sizeof(effectiveReference.position));
                Roomscale_Camera(GameTitle::Halo2, false, stock.position,
                    snapshot.headPosition, snapshot.headOrientation, tracked.forward,
                    g_reference.position, head.worldScale);
            }
        }
        // THE LEASH BELOW THE CHARACTER'S EYE (Reach's ReachApplyHeadLook; Halo 3, ODST and Halo 4 alike): at
        // most head_leash_below_eye_m below his CURRENT eye, so a deep duck cannot take the view inside his
        // chest and legs. On foot and in first person -- roomscale's admission (Game_TitleOnFootPlayerControlled):
        // the first-person palette packet is the witness, so a death camera is not leashed. The published base
        // rises with it -- the hands and the stereo core's re-composition of the eyes are built on it -- and
        // viewLiftZ says by how much.
        {
            const float leash=g_config.head_leash_below_eye_m;
            if(!vehicle&&head.positional&&std::isfinite(leash)&&leash>=0.0f&&std::isfinite(characterEyeZ)&&
                !VR_IsCutsceneTheaterActive())
            {
                const float floorZ=characterEyeZ-leash*head.worldScale;
                if(tracked.position[2]<floorZ&&Halo2Observer6Dof_OnFootFresh()&&
                    Halo2Observer6Dof_FinalPaletteArmed())
                {
                    const float lift=floorZ-tracked.position[2];
                    tracked.position[2]=floorZ;
                    stock.position[2]+=lift;
                    viewBase.position[2]+=lift;
                    viewLiftZ+=lift;
                }
            }
        }
        if (!WriteObserverPose(result, tracked))
        {
            g_unreadableSamples.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        // Optional and fail-open: the pose publication never depends on the
        // visibility-cover write succeeding.
        ApplyUpstreamVisibilityCover(result, snapshot);
        PublishPose(
            g_generation.load(std::memory_order_acquire),
            snapshot.preparedSerial, stock, tracked, effectiveReference, snapshot,
            ownsVehicleReference?&viewBase:nullptr,ownsVehicleReference?&vehicleKey:nullptr,
            ownsFootReference?&viewBase:nullptr,viewLiftZ);
        // Published at the view's own heading: from here the drive may write the hand's aim into the
        // record, and the right stick stops reaching the game.
        g_footViewEngagedMs.store(ownsFootReference ? GetTickCount64() : 0, std::memory_order_release);
        g_seatViewEngagedMs.store(ownsVehicleReference ? GetTickCount64() : 0, std::memory_order_release);
        g_appliedPoses.fetch_add(1, std::memory_order_relaxed);
    }

    // E-H2-23 (C-H2-31). Runs around first_person_weapons. Before the
    // placement: record which publication the observer record holds right
    // now - that is the pose the weapon is about to be placed against. The
    // record is compared bit for bit with the newest publication's tracked
    // pose; if the engine moved it since, the tick is counted as foreign and
    // no witness is published for it. After the placement: reset the
    // interpolator's four first-person slots for the owned player (the
    // engine's own reset, pinned by entry bytes; a no-op while the
    // interpolator is unallocated), so the renderer draws this tick's
    // placement and not a blend towards the previous tick's.
    void WitnessWeaponTick() noexcept
    {
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        if (!generation)
            return;
        const uintptr_t base = g_observerResult.load(std::memory_order_acquire);
        if (!base)
            return;
        const uintptr_t result = base + Halo2ObserverRecordOffset(kOwnedUser);
        Halo2ObserverPosePublication published{};
        if (!Halo2Observer6Dof_ReadPublishedPose(published) ||
            published.generation != generation || !published.index)
        {
            // No witness for this tick: the previous one must not stand, or
            // the eyes would render from a pose older than both the weapon
            // and the frame camera until it left the ring.
            g_weaponTickIndex.store(0, std::memory_order_release);
            g_weaponsNoPublication.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Halo2CameraBasis current{};
        if (!ReadObserverPose(result, current) ||
            !Halo2CameraBasisMatchesExactly(current, published.tracked))
        {
            g_weaponTickIndex.store(0, std::memory_order_release);
            g_weaponsRecordForeign.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_weaponTickGeneration.store(generation, std::memory_order_relaxed);
        g_weaponTickBasisVersion.fetch_add(1, std::memory_order_acq_rel);
        g_weaponTickBasis = published.tracked;
        g_weaponTickBasisVersion.fetch_add(1, std::memory_order_acq_rel);
        const uint64_t previous =
            g_weaponTickIndex.exchange(published.index, std::memory_order_acq_rel);
        if (previous && previous != published.index)
            g_weaponTickPreviousIndex.store(previous, std::memory_order_release);
        g_weaponsWitnessed.fetch_add(1, std::memory_order_relaxed);
    }

    // E-H2-32 (C-H2-37): the weapon must share the world's tick-quantised
    // state. The game's own yaw only changes at the 60 Hz tick, so the WORLD
    // steps at the tick during a stick turn - but MCC's interpolator smooths
    // the WEAPON between ticks, so the weapon trails the world it is attached
    // to. That is the drag. Correcting the weapon's view cannot fix it,
    // because the mismatch is against the world, not the head: the weapon has
    // to be at the tick the world is at. C-H2-34 removed this reset to cure a
    // 60 Hz feel that was really the CAMERA being tied to the tick (fixed
    // separately in C-H2-33, and the camera stays at the player's frame rate).
    void ResetFirstPersonInterpolation() noexcept
    {
        using ResetFn = void(__fastcall*)(int, int);
        const auto reset = reinterpret_cast<ResetFn>(
            g_interpolatorResetAddress.load(std::memory_order_acquire));
        if (!reset)
            return;
        for (int slot = 0; slot < kHalo2FrameInterpolatorFirstPersonSlots; ++slot)
            reset(static_cast<int>(kOwnedUser), slot);
        g_weaponsSlotResets.fetch_add(1, std::memory_order_relaxed);
    }

    // C-H2-47. Resolve which first-person nodes are the LEFT hand, from the
    // engine's own animation graph. E-H2-41 proves the retail layout; nothing
    // here is a guessed index. Any deviation from the exact shape every shipped
    // rig has returns false, and the caller then places the whole slot on the
    // right controller exactly as C-H2-46 does.
    //
    // Runs inside the palette detour's existing SEH + busy guard. Bounded: one
    // pass over at most 64 nodes, cached on the graph id and node count.
    bool ResolveArmBinding(
        int graphId, uint32_t count,
        Halo2FirstPersonArmBinding& out) noexcept
    {
        auto fail = [&](int reason) {
            g_armBindingLastReason.store(reason, std::memory_order_relaxed);
            g_armBinding = Halo2FirstPersonArmBinding{};
            g_armBindingGraphId = graphId;
            g_armBindingGraphIdValid = true;
            return false;
        };
        if (g_armBindingGraphIdValid && g_armBindingGraphId == graphId &&
            g_armBinding.count == count)
        {
            if (!g_armBinding.valid)
                return false;
            out = g_armBinding;
            return true;
        }
        g_armBindingRebuilds.fetch_add(1, std::memory_order_relaxed);
        g_armBindingGraphIdValid = false;

        const auto graphGet = reinterpret_cast<Halo2GraphDefinitionGetFn>(
            g_graphDefinitionGet.load(std::memory_order_acquire));
        const auto nodeGet = reinterpret_cast<Halo2GetSkeletonNodeFn>(
            g_graphGetSkeletonNode.load(std::memory_order_acquire));
        if (!graphGet || !nodeGet)
            return fail(1);
        if (!count || count > kHalo2FirstPersonPaletteCapacity)
            return fail(2);
        const void* const graph = graphGet(static_cast<uint32_t>(graphId));
        if (!graph)
            return fail(3);
        // The group tag is never checked by the engine's own resolver, so a
        // wrong id would silently hand back some other tag's bytes. This
        // equality is the gate that catches it: the palette the interpolator
        // just returned must have exactly as many nodes as the graph declares.
        const uint32_t declared = *reinterpret_cast<const volatile uint16_t*>(
            reinterpret_cast<uintptr_t>(graph) +
            kHalo2AnimationGraphNodeCountOffset);
        if (declared != count)
            return fail(4);

        uint8_t modelFlags[kHalo2FirstPersonPaletteCapacity]{};
        int16_t parents[kHalo2FirstPersonPaletteCapacity]{};
        for (uint32_t index = 0; index < count; ++index)
        {
            const void* const node = nodeGet(graph, static_cast<int>(index));
            if (!node)
                return fail(5);
            const uintptr_t address = reinterpret_cast<uintptr_t>(node);
            parents[index] = *reinterpret_cast<const volatile int16_t*>(
                address + kHalo2AnimationNodeParentOffset);
            modelFlags[index] = *reinterpret_cast<const volatile uint8_t*>(
                address + kHalo2AnimationNodeModelFlagsOffset);
        }
        Halo2FirstPersonArmBinding candidate{};
        if (!Halo2BuildFirstPersonArmBinding(
                modelFlags, parents, count, candidate))
        {
            return fail(6);
        }
        // Cross-check against the engine's OWN lookup where it is available:
        // it must name the same two wrists we derived from the same bits.
        const auto findByFlags = reinterpret_cast<Halo2FindNodeByFlagsFn>(
            g_graphFindNodeByFlags.load(std::memory_order_acquire));
        if (findByFlags)
        {
            if (findByFlags(graph, kHalo2ModelFlagRightHand) !=
                    candidate.rightWrist ||
                findByFlags(graph, kHalo2ModelFlagLeftHand) !=
                    candidate.leftWrist)
            {
                return fail(7);
            }
        }
        g_armBinding = candidate;
        g_armBindingGraphId = graphId;
        g_armBindingGraphIdValid = true;
        g_armBindingLastReason.store(0, std::memory_order_relaxed);
        out = candidate;
        return true;
    }

    // Shared controller-to-Halo-2 transform for both visible placement and the
    // shot ray. The mesh receives gun_forward_m as a presentation trim. The
    // shot deliberately receives zero trim: the compositor places the visible
    // crosshair at `presented aim position + (-Z * distance)`, so adding the
    // mesh-only trim to that origin would create a second, parallel ray.
    //
    // VR_GetAimPose is the shared, mount-calibrated aim pose (gun_yaw_deg /
    // gun_pitch_deg / gun_roll_deg and two-handed aim are already folded into
    // it), and the firing caller supplies the exact stabilized pose the
    // compositor actually presented.
    bool BuildFirstPersonCarrierFromAimPose(
        const Halo2CameraBasis& renderCamera, const float aimOrientation[4],
        const float aimPosition[3], float forwardTrimMeters,
        float rightTrimMeters, float upTrimMeters,
        Halo2CameraBasis& carrier) noexcept
    {
        float headOrientation[4]{}, headPosition[3]{};
        if (!VR_GetHeadPose(headOrientation, headPosition))
        {
            return false;
        }
        return Halo2BuildControllerCarrier(
            renderCamera, headOrientation, headPosition, aimOrientation,
            aimPosition, Game_GetWorldScale(), forwardTrimMeters,
            rightTrimMeters, upTrimMeters, carrier);
    }

    bool BuildFirstPersonCarrier(
        const Halo2CameraBasis& renderCamera,
        Halo2CameraBasis& carrier) noexcept
    {
        float aimOrientation[4]{}, aimPosition[3]{};
        return VR_GetAimPose(aimOrientation, aimPosition) &&
            BuildFirstPersonCarrierFromAimPose(
                renderCamera, aimOrientation, aimPosition,
                weapon_alignment::Forward(false,g_config.gun_forward_m,0.f),
                weapon_alignment::Right(false,g_config.gun_right_m),
                weapon_alignment::Up(g_config.gun_up_m), carrier);
    }

    bool BuildStableFirstPersonCarriers(
        const Halo2ObserverPosePublication& publication,
        uint32_t generation,
        Halo2CameraBasis& rightCarrier,
        Halo2CameraBasis& leftCarrier, bool independentPrimary = false) noexcept
    {
        if (!Halo2ObserverControllerSnapshotUsable(
                publication, generation))
            return false;
        const Halo2ObserverPoseSnapshot& tracking = publication.snapshot;
        if (independentPrimary && !tracking.independentRightAimValid) return false;
        float leftOrientation[4]{};
        if (!Halo2BuildMirroredLeftAimOrientation(
                tracking.leftControllerOrientation,
                g_config.gun_yaw_deg, g_config.gun_pitch_deg,
                g_config.gun_roll_deg, leftOrientation))
            return false;
        const float worldScale = Game_GetWorldScale();
        return Halo2BuildStableControllerCarrier(
                   Halo2ControllerReference(publication), publication.referenceOrientation,
                   publication.referencePosition,
                   independentPrimary ? tracking.independentRightAimOrientation : tracking.rightAimOrientation,
                   tracking.rightAimPosition, worldScale,
                   weapon_alignment::Forward(false,g_config.gun_forward_m,0.f),
                   weapon_alignment::Right(false,g_config.gun_right_m),
                   weapon_alignment::Up(g_config.gun_up_m),
                   rightCarrier) &&
            Halo2BuildStableControllerCarrier(
                   Halo2ControllerReference(publication), publication.referenceOrientation,
                   publication.referencePosition, leftOrientation,
                   tracking.leftControllerPosition, worldScale,
                   weapon_alignment::Forward(true,g_config.left_gun_forward_m,g_config.left_hand_forward_m),
                   weapon_alignment::Right(false,g_config.left_gun_right_m),
                   weapon_alignment::Up(g_config.left_gun_up_m),leftCarrier);
    }

    void Halo2PublishCollisionCorrection(
        int hand, uint32_t generation, const float source[3],
        const float correction[3], bool contact) noexcept
    {
        if (hand < 0 || hand > 1 || !source || !correction) return;
        auto& publication = g_halo2WorldCollision.correction[hand];
        publication.version.fetch_add(1, std::memory_order_acq_rel);
        publication.generation = generation;
        publication.publishedAtMs = GetTickCount64();
        publication.contact = contact;
        std::memcpy(publication.source, source, sizeof(publication.source));
        std::memcpy(
            publication.correction, correction,
            sizeof(publication.correction));
        publication.version.fetch_add(1, std::memory_order_release);
    }

    bool Halo2ReadCollisionCorrection(
        int hand, uint32_t& generation, uint64_t& publishedAtMs,
        bool& contact, float source[3], float correction[3]) noexcept
    {
        if (hand < 0 || hand > 1 || !source || !correction) return false;
        const auto& publication = g_halo2WorldCollision.correction[hand];
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before =
                publication.version.load(std::memory_order_acquire);
            if (before & 1u) continue;
            generation = publication.generation;
            publishedAtMs = publication.publishedAtMs;
            contact = publication.contact;
            std::memcpy(source, publication.source, sizeof(publication.source));
            std::memcpy(
                correction, publication.correction,
                sizeof(publication.correction));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (publication.version.load(std::memory_order_acquire) == before)
                return Halo2WorldCollisionFinite(source) &&
                    Halo2WorldCollisionFinite(correction);
        }
        return false;
    }

    bool Halo2ApplyCollisionCorrection(
        int hand, uint32_t generation, float worldScale,
        Halo2CameraBasis& carrier, float applied[3]) noexcept
    {
        if (applied) std::memset(applied, 0, sizeof(float) * 3);
        if (hand < 0 || hand > 1 || !g_config.world_collision ||
            !g_halo2WorldCollision.installed.load(std::memory_order_acquire) ||
            !g_halo2WorldCollision.active.load(std::memory_order_acquire) ||
            !Halo2ValidateCameraBasis(carrier) ||
            !std::isfinite(worldScale) || worldScale <= 0.0f)
            return false;
        uint32_t correctionGeneration = 0;
        uint64_t correctionAtMs = 0;
        bool contact = false;
        float source[3]{}, correction[3]{};
        if (!Halo2ReadCollisionCorrection(
                hand, correctionGeneration, correctionAtMs, contact,
                source, correction) || !contact ||
            correctionGeneration != generation)
            return false;
        const uint64_t now = GetTickCount64();
        if (correctionAtMs > now ||
            now - correctionAtMs > kHalo2CollisionPublicationMaxAgeMs)
            return false;
        const float sourceDrift = Halo2WorldCollisionDistanceSquared(
            source, carrier.position);
        const float sourceLimit = 0.15f * worldScale;
        float correctionSquared = 0.0f;
        for (float value : correction)
            correctionSquared += value * value;
        const float correctionLimit = 0.75f * worldScale;
        if (sourceDrift < 0.0f || sourceDrift > sourceLimit * sourceLimit ||
            !std::isfinite(correctionSquared) ||
            correctionSquared > correctionLimit * correctionLimit)
            return false;
        Halo2CameraBasis corrected = carrier;
        for (int axis = 0; axis < 3; ++axis)
            corrected.position[axis] += correction[axis];
        if (!Halo2ValidateCameraBasis(corrected)) return false;
        carrier = corrected;
        if (applied)
            std::memcpy(applied, correction, sizeof(float) * 3);
        g_halo2WorldCollision.applied[hand].fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }

    void Halo2PublishCollisionTarget(
        int hand, uint32_t generation, int32_t ignoredUnit,
        int32_t ignoredWeapon, const float samples[][3],
        uint32_t sampleCount) noexcept
    {
        if (hand < 0 || hand > 1 || !generation || !samples ||
            sampleCount == 0 ||
            sampleCount > kHalo2WorldCollisionMaxSamples)
            return;
        for (uint32_t sample = 0; sample < sampleCount; ++sample)
            if (!Halo2WorldCollisionFinite(samples[sample])) return;
        auto& publication =
            g_halo2WorldCollision.targetPublication[hand];
        publication.version.fetch_add(1, std::memory_order_acq_rel);
        publication.generation = generation;
        publication.publishedAtMs = GetTickCount64();
        publication.sampleCount = sampleCount;
        publication.ignoredUnit = ignoredUnit;
        publication.ignoredWeapon = ignoredWeapon;
        std::memcpy(publication.samples, samples,
            sizeof(float) * sampleCount * 3);
        publication.version.fetch_add(1, std::memory_order_release);
    }

    bool Halo2ReadCollisionTarget(
        int hand, uint32_t& generation, uint64_t& publishedAtMs,
        int32_t& ignoredUnit, int32_t& ignoredWeapon,
        float samples[][3], uint32_t& sampleCount) noexcept
    {
        if (hand < 0 || hand > 1 || !samples) return false;
        const auto& publication =
            g_halo2WorldCollision.targetPublication[hand];
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before =
                publication.version.load(std::memory_order_acquire);
            if (before & 1u) continue;
            generation = publication.generation;
            publishedAtMs = publication.publishedAtMs;
            ignoredUnit = publication.ignoredUnit;
            ignoredWeapon = publication.ignoredWeapon;
            sampleCount = publication.sampleCount;
            if (!generation || sampleCount == 0 ||
                sampleCount > kHalo2WorldCollisionMaxSamples)
                return false;
            std::memcpy(samples, publication.samples,
                sizeof(float) * sampleCount * 3);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (publication.version.load(std::memory_order_acquire) != before)
                continue;
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
                if (!Halo2WorldCollisionFinite(samples[sample])) return false;
            return true;
        }
        return false;
    }

    bool Halo2BuildAuthoredWeaponCollisionSamples(
        uint32_t renderModelTag, const float* gunMatrices,
        const float correction[3], float output[][3]) noexcept
    {
        // ff6d1fd still produced zero authored bounds. Preserve the rejected
        // cache-block experiment dormant while retaining accepted hand samples.
        constexpr bool kEnableUnverifiedCompressionBlock = false;
        // The new binding decodes the exact native ADD instruction. The old
        // decoder never armed tagBaseSlot, so the bounds were never reached.
        constexpr bool kEnableInstructionDecodedCompressionBounds = false;
        constexpr bool kEnableRuntimeVerifiedCompressionLayout = true;
        if (!kEnableUnverifiedCompressionBlock &&
            !kEnableInstructionDecodedCompressionBounds &&
            !kEnableRuntimeVerifiedCompressionLayout) return false;
        if (renderModelTag == UINT32_MAX || !gunMatrices || !correction ||
            !output)
            return false;
        const auto graphGet = reinterpret_cast<Halo2GraphDefinitionGetFn>(
            g_graphDefinitionGet.load(std::memory_order_acquire));
        const auto tagBaseSlot = reinterpret_cast<unsigned char**>(
            g_halo2TagDataBaseSlot.load(std::memory_order_acquire));
        if (!graphGet || !tagBaseSlot)
            return false;

        Halo2FirstPersonTransform root{};
        if (!Halo2ReadFirstPersonTransform(gunMatrices, root))
            return false;

        __try
        {
            const auto* definition = static_cast<const uint8_t*>(
                graphGet(renderModelTag));
            unsigned char* const tagBase = *tagBaseSlot;
            if (!definition || !tagBase)
                return false;
            // Live H2 1c08837 evidence: the loaded mode compression block is
            // +0x14/+0x18. +0x10 was the preceding empty import-block address,
            // and therefore rejected EVERY weapon. The loaded fp_battle_rifle
            // record independently matches all six H2EK-exported bounds.
            // Keep both old addressing experiments dormant; the native tag
            // getter still proves signed BYTE addressing from tagBase.
            int32_t byteOffset=0;
            if constexpr(kEnableRuntimeVerifiedCompressionLayout)
            {
                if(!Halo2ReadWeaponCompressionHeader(
                    std::span<const uint8_t>(definition,0x1C),byteOffset))
                    return false;
            }
            else
            {
                const int32_t count=*reinterpret_cast<const int32_t*>(definition+0x10);
                byteOffset=*reinterpret_cast<const int32_t*>(definition+0x14);
                if(count!=1 || !byteOffset) return false;
            }
            const float* bounds = reinterpret_cast<const float*>(
                tagBase + static_cast<ptrdiff_t>(byteOffset));
            float minimum[3]{bounds[0], bounds[2], bounds[4]};
            float maximum[3]{bounds[1], bounds[3], bounds[5]};
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(minimum[axis]) ||
                    !std::isfinite(maximum[axis]) ||
                    minimum[axis] > maximum[axis] ||
                    maximum[axis] - minimum[axis] > 2.0f)
                    return false;

            float local[kHalo2WeaponCollisionBoundsSampleCount][3]{};
            int sample = 0;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int z = 0; z < 2; ++z)
                    {
                        local[sample][0] = x ? maximum[0] : minimum[0];
                        local[sample][1] = y ? maximum[1] : minimum[1];
                        local[sample][2] = z ? maximum[2] : minimum[2];
                        ++sample;
                    }
            const float centre[3]{
                (minimum[0] + maximum[0]) * 0.5f,
                (minimum[1] + maximum[1]) * 0.5f,
                (minimum[2] + maximum[2]) * 0.5f};
            for (int axis = 0; axis < 3; ++axis)
                for (int side = 0; side < 2; ++side)
                {
                    std::memcpy(local[sample], centre, sizeof(centre));
                    local[sample][axis] = side ? maximum[axis] : minimum[axis];
                    ++sample;
                }
            for (sample = 0;
                 sample < kHalo2WeaponCollisionBoundsSampleCount; ++sample)
            {
                for (int row = 0; row < 3; ++row)
                {
                    float rotated = 0.0f;
                    for (int column = 0; column < 3; ++column)
                        rotated += root.rotation[column * 3 + row] *
                            local[sample][column];
                    output[sample][row] = root.translation[row] -
                        correction[row] + root.scale * rotated;
                }
                if (!Halo2WorldCollisionFinite(output[sample]))
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    uint64_t Halo2ObserveReloadModel(uint32_t tag,uint32_t nodes,bool observePrimary=true) noexcept
    {
        if(tag==UINT32_MAX||!nodes) return 0;
        const auto get=reinterpret_cast<Halo2GraphDefinitionGetFn>(
            g_graphDefinitionGet.load(std::memory_order_acquire));
        const auto slot=reinterpret_cast<unsigned char**>(
            g_halo2TagDataBaseSlot.load(std::memory_order_acquire));
        uint64_t identity=0,visualIdentity=0;
        __try
        {
            const auto* definition=get?static_cast<const uint8_t*>(get(tag)):nullptr;
            int32_t offset=0;
            if(definition&&slot&&*slot&&Halo2ReadWeaponCompressionHeader(
                std::span<const uint8_t>(definition,0x1C),offset))
            {
                const auto* b=reinterpret_cast<const float*>(*slot+static_cast<ptrdiff_t>(offset));
                const float low[]{b[0],b[2],b[4]},high[]{b[1],b[3],b[5]};
                identity=weapon_model::Halo2LiveIdentity(low,high,nodes,tag);
                if(identity)visualIdentity=weapon_alignment::Halo2VisualIdentity(low,high,nodes);
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) { identity=visualIdentity=0; }
        if(observePrimary)
            VR_ObserveWeaponModel(GameTitle::Halo2,TitleAdapter_GetGeneration(GameTitle::Halo2),identity);
        else
            VR_ObserveSecondaryWeaponModel(GameTitle::Halo2,TitleAdapter_GetGeneration(GameTitle::Halo2),identity);
        VR_ObserveHalo2VisualModel(!observePrimary,TitleAdapter_GetGeneration(GameTitle::Halo2),visualIdentity);
        return identity;
    }

#include "halo2_muzzle_publication.inl"
#include "halo2_visual_hand_offset.inl"

    void Halo2PublishFinalPacketCollisionVolumes(
        const Halo2VisibleConsumerContext& context, float* handsMatrices,
        float* gunMatrices, uint32_t gunRenderModelTag,
        float* secondaryMatrices = nullptr,
        uint32_t secondaryRenderModelTag = UINT32_MAX) noexcept
    {
        if(context.valid&&handsMatrices&&gunMatrices)
        {
            const uint64_t identity=Halo2ObserveReloadModel(gunRenderModelTag,context.gunCount);
            // Visual defaults need the secondary identity even with barrel aiming disabled.
            const uint64_t secondaryIdentity=secondaryMatrices
                ? Halo2ObserveReloadModel(secondaryRenderModelTag,context.secondaryGunCount,false):0;
            if(Sight_GunBarrelAim()||Sight_ShotsFromMuzzle())
            {
                Halo2PublishMuzzlePalette(context,0,identity,gunMatrices,context.gunCount);
                Halo2PublishMuzzlePalette(context,1,secondaryIdentity,secondaryMatrices,context.secondaryGunCount);
            }
            const auto* model=weapon_model::Find(GameTitle::Halo2,identity);
            Halo2FirstPersonTransform root{};float point[3]{};
            if(g_config.manual_reload&&Halo2ReadFirstPersonTransform(gunMatrices,root)&&
                weapon_model::ReceiverPoint(model,root.scale,root.rotation,root.translation,point))
                VR_PublishReloadTarget(GameTitle::Halo2,TitleAdapter_GetGeneration(GameTitle::Halo2),
                    identity,context.contactFrames[1].serial,context.contactFrames[1].transform,point);
        }
        if ((!g_config.world_collision && !Sight_PhysicalMelee()) || !handsMatrices || !gunMatrices ||
            !context.valid || context.handsCount == 0 ||
            context.handsCount > kHalo2FirstPersonPaletteCapacity ||
            context.gunCount == 0 ||
            context.gunCount > kHalo2FirstPersonPaletteCapacity)
            return;
        float rightPoints[kHalo2FirstPersonPaletteCapacity * 2][3]{};
        float leftPoints[kHalo2FirstPersonPaletteCapacity * 2][3]{};
        int rightCount = 0;
        int leftCount = 0;
        for (uint32_t destination = 0; destination < context.handsCount;
             ++destination)
        {
            const int32_t source = context.handsRemap[destination];
            const uint64_t bit = source >= 0 && source < 64
                ? uint64_t{1} << source : 0;
            const bool right = bit && (context.binding.rightSubtree & bit);
            const int32_t leftSource = secondaryMatrices
                ? context.secondaryHandsRemap[destination] : source;
            const uint64_t leftBit = leftSource >= 0 && leftSource < 64
                ? uint64_t{1} << leftSource : 0;
            const bool left = leftBit & (secondaryMatrices
                ? context.secondaryBinding.leftSubtree : context.binding.leftSubtree);
            if (!right && !left) continue;
            Halo2FirstPersonTransform transform{};
            if (!Halo2ReadFirstPersonTransform(
                    handsMatrices + destination *
                        kHalo2FirstPersonNodeFloats,
                    transform))
                return;
            // Buffers and haptics use weapon roles, whereas the remaps name
            // actual anatomy. The primary gun below always stays in role 1.
            const bool primaryHand = context.handAlignment ? left : right;
            float* output = primaryHand ? rightPoints[rightCount++]
                                        : leftPoints[leftCount++];
            const float* correction = primaryHand
                ? context.rightCollisionCorrection
                : context.leftCollisionCorrection;
            for (int axis = 0; axis < 3; ++axis)
                output[axis] = transform.translation[axis] - correction[axis];
        }
        for (uint32_t node = 0; node < context.gunCount; ++node)
        {
            Halo2FirstPersonTransform transform{};
            if (!Halo2ReadFirstPersonTransform(
                    gunMatrices + node * kHalo2FirstPersonNodeFloats,
                    transform))
                return;
            float* output = rightPoints[rightCount++];
            for (int axis = 0; axis < 3; ++axis)
                output[axis] = transform.translation[axis] -
                    context.rightCollisionCorrection[axis];
        }
        if (!rightCount)
            std::memcpy(rightPoints[rightCount++],
                context.collisionRightCarrier.position, sizeof(float) * 3);
        if (secondaryMatrices)
        {
            if (!context.secondaryGunCount ||
                context.secondaryGunCount > kHalo2FirstPersonPaletteCapacity) return;
            for (uint32_t node = 0; node < context.secondaryGunCount; ++node)
            {
                Halo2FirstPersonTransform transform{};
                if (!Halo2ReadFirstPersonTransform(
                    secondaryMatrices + node * kHalo2FirstPersonNodeFloats, transform)) return;
                float* output = leftPoints[leftCount++];
                for (int axis = 0; axis < 3; ++axis)
                    output[axis] = transform.translation[axis] - context.leftCollisionCorrection[axis];
            }
        }
        if (!leftCount)
            std::memcpy(leftPoints[leftCount++],
                context.collisionLeftCarrier.position, sizeof(float) * 3);
        float rightHandSamples[kHalo2WorldCollisionSampleCount][3]{};
        float leftSamples[kHalo2WorldCollisionMaxSamples][3]{};
        if (Halo2SelectWorldCollisionExtrema(
                context.collisionRightCarrier.position, &rightPoints[0][0],
                rightCount, rightHandSamples,
                kHalo2WorldCollisionSampleCount) !=
                kHalo2WorldCollisionSampleCount ||
            Halo2SelectWorldCollisionExtrema(
                context.collisionLeftCarrier.position, &leftPoints[0][0],
                leftCount, leftSamples,
                kHalo2WorldCollisionSampleCount) !=
                kHalo2WorldCollisionSampleCount)
            return;
        float rightSamples[kHalo2WorldCollisionMaxSamples][3]{};
        std::memcpy(rightSamples, rightHandSamples,
            sizeof(rightHandSamples));
        uint32_t rightSampleCount = kHalo2WorldCollisionSampleCount;
        uint32_t leftSampleCount = kHalo2WorldCollisionSampleCount;
        float weaponSamples[kHalo2WeaponCollisionBoundsSampleCount][3]{};
        if (Halo2BuildAuthoredWeaponCollisionSamples(
                gunRenderModelTag, gunMatrices,
                context.rightCollisionCorrection, weaponSamples))
        {
            std::memcpy(rightSamples + rightSampleCount, weaponSamples,
                sizeof(weaponSamples));
            rightSampleCount += kHalo2WeaponCollisionBoundsSampleCount;
            g_halo2WorldCollision.weaponBoundsPublished.fetch_add(
                1, std::memory_order_relaxed);
        }
        else
        {
            g_halo2WorldCollision.weaponBoundsFallbacks.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (secondaryMatrices)
        {
            if (Halo2BuildAuthoredWeaponCollisionSamples(
                    secondaryRenderModelTag, secondaryMatrices,
                    context.leftCollisionCorrection, weaponSamples))
            {
                std::memcpy(leftSamples + leftSampleCount, weaponSamples, sizeof(weaponSamples));
                leftSampleCount += kHalo2WeaponCollisionBoundsSampleCount;
                g_halo2WorldCollision.weaponBoundsPublished.fetch_add(1, std::memory_order_relaxed);
            }
            else
                g_halo2WorldCollision.weaponBoundsFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
        const uint32_t generation =
            g_generation.load(std::memory_order_acquire);
        Halo2PublishContactSamples(0,context,leftSamples,leftSampleCount);
        Halo2PublishContactSamples(1,context,rightSamples,rightSampleCount);
        if(!g_config.world_collision) return;
        Halo2PublishCollisionTarget(
            0, generation, static_cast<int32_t>(context.unitObject),
            static_cast<int32_t>(secondaryMatrices ? context.secondaryWeaponObject : context.weaponObject),
            leftSamples, leftSampleCount);
        Halo2PublishCollisionTarget(
            1, generation, static_cast<int32_t>(context.unitObject),
            static_cast<int32_t>(context.weaponObject), rightSamples,
            rightSampleCount);
    }

    void* Halo2ObjectFromIndex(uint32_t objectIndex) noexcept
    {
        const uintptr_t module =
            g_moduleBase.load(std::memory_order_acquire);
        const auto accessor = reinterpret_cast<Halo2ObjectDatumAccessorFn>(
            g_objectDatumAccessor.load(std::memory_order_acquire));
        if (!module || !accessor || objectIndex == UINT32_MAX) return nullptr;
        const uintptr_t objectsData =
            *reinterpret_cast<const volatile uintptr_t*>(
                module + kHalo2ObjectsDataArrayPointerRva);
        const auto* entry=halo2_datum::Record(reinterpret_cast<const uint8_t*>(objectsData),
            objectIndex,kHalo2ObjectDataEntryStride,0x2800);
        if (!entry || entry[3]>=32) return nullptr;
        auto* object=static_cast<uint8_t*>(accessor(entry));
        return object && object[0xAA]==entry[3] ? object : nullptr;
    }

    uint32_t Halo2OwnedUnit() noexcept
    {
        const auto module=g_moduleBase.load(std::memory_order_acquire);
        if (!module) return UINT32_MAX;
        const auto globals=*reinterpret_cast<const uintptr_t*>(module+kHalo2PlayersGlobalsPointerRva);
        const auto* players=*reinterpret_cast<const uint8_t* const*>(module+kHalo2PlayersDataArrayPointerRva);
        if (!globals) return UINT32_MAX;
        const auto handle=*reinterpret_cast<const uint32_t*>(
            globals+kHalo2PlayerUserMappingOffset+kOwnedUser*sizeof(uint32_t));
        const auto* player=halo2_datum::Record(players,handle,kHalo2PlayerDatumStride,kHalo2MaximumPlayers);
        return player ? *reinterpret_cast<const uint32_t*>(player+kHalo2PlayerUnitIndexOffset) : UINT32_MAX;
    }

#include "halo2_physical_crouch.inl"

    bool ReadVehicleViewFrame(Halo2VehicleViewKey& key,Halo2CameraBasis& hull) noexcept
    {
        if(!g_vehicleFrameVerified.load(std::memory_order_acquire)) return false;
        __try {
            key.unit=Halo2OwnedUnit();
            const auto* unit=static_cast<const uint8_t*>(Halo2ObjectFromIndex(key.unit));
            if(!unit||unit[0xAA]!=0) return false;
            key.seat=*reinterpret_cast<const int16_t*>(unit+kHalo2UnitParentSeatOffset);
            key.parent=*reinterpret_cast<const uint32_t*>(unit+0x14);
            if(key.seat<0||key.parent==UINT32_MAX) return false;
            uint32_t object=key.parent,seen[8]{};
            for(unsigned depth=0;depth<8;++depth) {
                if(object==key.unit) return false;
                for(unsigned i=0;i<depth;++i) if(seen[i]==object) return false;
                seen[depth]=object;
                const auto* data=static_cast<const uint8_t*>(Halo2ObjectFromIndex(object));
                if(!data||data[0xAA]!=1) return false;
                const auto parent=*reinterpret_cast<const uint32_t*>(data+0x14);
                if(parent!=UINT32_MAX) {object=parent;continue;}
                key.root=object;
                hull={};
                memcpy(hull.forward,data+0x70,sizeof(hull.forward));
                memcpy(hull.up,data+0x7C,sizeof(hull.up));
                return key.generation==g_generation.load(std::memory_order_acquire)&&Halo2ValidateCameraBasis(hull);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        return false;
    }

    void Halo2TryPushCollisionObject(
        int32_t objectIndex, int32_t ignoredUnit, int32_t ignoredWeapon,
        const float previous[3], const float desired[3], uint64_t elapsedMs,
        float worldScale) noexcept
    {
        if (objectIndex == -1 || objectIndex == ignoredUnit ||
            objectIndex == ignoredWeapon || elapsedMs == 0 || elapsedMs > 250 ||
            !g_halo2WorldCollision.objectSetLocalVelocity ||
            !Halo2WorldCollisionFinite(previous) ||
            !Halo2WorldCollisionFinite(desired) ||
            !std::isfinite(worldScale) || worldScale <= 0.0f)
            return;
        float worldVelocity[3]{};
        const float inverseSeconds = 650.0f / static_cast<float>(elapsedMs);
        float lengthSquared = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            worldVelocity[axis] =
                (desired[axis] - previous[axis]) * inverseSeconds;
            lengthSquared += worldVelocity[axis] * worldVelocity[axis];
        }
        const float maximum = 2.0f * worldScale;
        if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f) return;
        if (lengthSquared > maximum * maximum)
        {
            const float scale = maximum / std::sqrt(lengthSquared);
            for (float& value : worldVelocity) value *= scale;
        }
        auto* object = static_cast<const uint8_t*>(
            Halo2ObjectFromIndex(static_cast<uint32_t>(objectIndex)));
        if (!object) return;
        const float forward[3]{
            *reinterpret_cast<const volatile float*>(object + 0x70),
            *reinterpret_cast<const volatile float*>(object + 0x74),
            *reinterpret_cast<const volatile float*>(object + 0x78)};
        const float up[3]{
            *reinterpret_cast<const volatile float*>(object + 0x7C),
            *reinterpret_cast<const volatile float*>(object + 0x80),
            *reinterpret_cast<const volatile float*>(object + 0x84)};
        if (!Halo2WorldCollisionFinite(forward) ||
            !Halo2WorldCollisionFinite(up))
            return;
        const float left[3]{
            up[1] * forward[2] - up[2] * forward[1],
            up[2] * forward[0] - up[0] * forward[2],
            up[0] * forward[1] - up[1] * forward[0]};
        auto dot = [&worldVelocity](const float axis[3]) {
            return worldVelocity[0] * axis[0] +
                worldVelocity[1] * axis[1] +
                worldVelocity[2] * axis[2];
        };
        g_halo2WorldCollision.objectSetLocalVelocity(
            static_cast<uint32_t>(objectIndex), dot(forward), dot(left),
            dot(up));
        g_halo2WorldCollision.pushes.fetch_add(1, std::memory_order_relaxed);
    }

    void Halo2WorldCollisionEngineContextTick() noexcept
    {
        if (!g_halo2WorldCollision.installed.load(std::memory_order_acquire) ||
            !g_halo2WorldCollision.active.load(std::memory_order_acquire) ||
            !g_config.world_collision ||
            !g_armed.load(std::memory_order_acquire) ||
            !g_levelLive.load(std::memory_order_acquire) ||
            g_teardownRequested.load(std::memory_order_acquire))
            return;
        const uint64_t now = GetTickCount64();
        uint64_t due =
            g_halo2WorldCollision.nextQueryAtMs.load(std::memory_order_relaxed);
        if (now < due ||
            !g_halo2WorldCollision.nextQueryAtMs.compare_exchange_strong(
                due, now + kHalo2CollisionQueryIntervalMs,
                std::memory_order_acq_rel, std::memory_order_relaxed))
            return;
        const uint32_t generation =
            g_generation.load(std::memory_order_acquire);
        const float worldScale = Game_GetWorldScale();
        if (!generation || generation != g_halo2WorldCollision.generation ||
            !std::isfinite(worldScale) || worldScale <= 0.0f)
            return;
        for (int hand = 0; hand < 2; ++hand)
        {
            uint32_t targetGeneration = 0, sampleCount = 0;
            uint64_t targetAtMs = 0;
            int32_t ignoredUnit = -1, ignoredWeapon = -1;
            float desired[kHalo2WorldCollisionMaxSamples][3]{};
            auto& worker = g_halo2WorldCollision.worker[hand];
            if (!Halo2ReadCollisionTarget(
                    hand, targetGeneration, targetAtMs, ignoredUnit,
                    ignoredWeapon, desired, sampleCount) ||
                targetGeneration != generation || targetAtMs > now ||
                now - targetAtMs > kHalo2CollisionPublicationMaxAgeMs)
            {
                worker.seeded = false;
                const float zero[3]{};
                Halo2PublishCollisionCorrection(
                    hand, generation, zero, zero, false);
                continue;
            }
            if (!worker.seeded || worker.generation != generation ||
                worker.sampleCount != sampleCount ||
                worker.ignoredUnit != ignoredUnit ||
                worker.ignoredWeapon != ignoredWeapon ||
                Halo2WorldCollisionMovementIsTeleport(
                    worker.accepted[0], desired[0], worldScale))
            {
                worker.smoothing.Reset();
                worker.seeded = true;
                worker.generation = generation;
                worker.publishedAtMs = targetAtMs;
                worker.sampleCount = sampleCount;
                worker.ignoredUnit = ignoredUnit;
                worker.ignoredWeapon = ignoredWeapon;
                std::memcpy(worker.accepted, desired,
                    sizeof(float) * sampleCount * 3);
                const float zero[3]{};
                Halo2PublishCollisionCorrection(
                    hand, generation, desired[0], zero, false);
                continue;
            }
            bool contact = false;
            uint32_t contactSample = 0;
            int32_t contactObject = -1;
            float strongest[3]{};
            float strongestSquared = 0.0f;
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
            {
                if (Halo2WorldCollisionDistanceSquared(
                        worker.accepted[sample], desired[sample]) < 1.0e-10f)
                    continue;
                float delta[3]{};
                for (int axis = 0; axis < 3; ++axis)
                    delta[axis] = desired[sample][axis] -
                        worker.accepted[sample][axis];
                Halo2CollisionResult result{};
                bool hit = false;
                g_halo2CollisionOwnQuery = true;
                __try
                {
                    hit = g_halo2WorldCollision.original(
                        kHalo2CollisionFlags, worker.accepted[sample], delta,
                        ignoredUnit, ignoredWeapon, &result) != 0;
                }
                __finally
                {
                    // A fault in an optional probe is isolated by the caller,
                    // but it must never leave this thread marked as an owned
                    // query and suppress all future engine-context ticks.
                    g_halo2CollisionOwnQuery = false;
                }
                g_halo2WorldCollision.queries.fetch_add(
                    1, std::memory_order_relaxed);
                const Halo2WorldCollisionResolution resolution =
                    Halo2ResolveWorldCollision(
                        worker.accepted[sample], desired[sample], hit,
                        result.fraction, worldScale);
                if (!resolution.valid)
                {
                    g_halo2WorldCollision.failures.fetch_add(
                        1, std::memory_order_relaxed);
                    g_halo2WorldCollision.active.store(
                        false, std::memory_order_release);
                    return;
                }
                if (!resolution.contact) continue;
                float correctionSquared = 0.0f;
                for (float value : resolution.correction)
                    correctionSquared += value * value;
                if (!contact || correctionSquared > strongestSquared)
                {
                    contact = true;
                    contactSample = sample;
                    contactObject = result.type == 4
                        ? result.objectIndex : -1;
                    strongestSquared = correctionSquared;
                    std::memcpy(strongest, resolution.correction,
                        sizeof(strongest));
                }
            }
            const uint64_t elapsedMs = targetAtMs > worker.publishedAtMs
                ? targetAtMs - worker.publishedAtMs : 0;
            if (contact && contactObject != -1)
                Halo2TryPushCollisionObject(
                    contactObject, ignoredUnit, ignoredWeapon,
                    worker.accepted[contactSample], desired[contactSample],
                    elapsedMs, worldScale);
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
                for (int axis = 0; axis < 3; ++axis)
                    worker.accepted[sample][axis] = desired[sample][axis] +
                        (contact ? strongest[axis] : 0.0f);
            worker.publishedAtMs = targetAtMs;
            const bool visibleCorrection=worker.smoothing.Apply(now,contact,worldScale,strongest);
            Halo2PublishCollisionCorrection(
                hand, generation, desired[0], strongest, visibleCorrection);
            if (contact)
            {
                g_halo2WorldCollision.contacts[hand].fetch_add(
                    1, std::memory_order_relaxed);
                if (hand == 1 && contactSample != 0)
                    g_halo2WorldCollision.weaponContacts.fetch_add(
                        1, std::memory_order_relaxed);
                VR_PulseContactHaptics(
                    hand == 0, kHalo2CollisionHapticAmplitude);
            }
        }
    }

#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev AS SEEN (halo2_impact_witness.inl, included below): a tracked round's own query, after the native.
    void Halo2ImpactNoteQuery(const float* start, const float* vector, const Halo2CollisionResult* result, uint8_t hit);
#endif

    __declspec(noinline) uint8_t __fastcall Halo2CollisionVectorDetour(
        uint32_t flags, const float* start, const float* delta,
        int32_t ignoredObject1, int32_t ignoredObject2,
        Halo2CollisionResult* result)
    {
        g_halo2WorldCollision.callbacks.fetch_add(
            1, std::memory_order_acq_rel);
        uint8_t hit = 0;
        const auto original = g_halo2WorldCollision.original;
        __try
        {
            const bool redirected=Halo2RedirectContactVector(
                reinterpret_cast<uintptr_t>(_ReturnAddress()),flags,
                ignoredObject1,ignoredObject2,result,hit);
#if HALOMCCVR_DEV_DIAGNOSTICS
            // The engine-ray census (sight_layer.h), dev key sight_ray_census.
            if (g_config.sight_ray_census && !g_halo2CollisionOwnQuery && start && delta)
            {
                const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
                const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
                if (base && caller > base && caller < base + g_moduleSize.load(std::memory_order_acquire))
                {
                    const float end[3]{start[0] + delta[0], start[1] + delta[1], start[2] + delta[2]};
                    Sight_NoteEngineRay(GameTitle::Halo2, g_generation.load(std::memory_order_acquire),
                        static_cast<uint32_t>(caller - base), 0, flags, start, end);
                }
            }
#endif
            if (original && !redirected)
            {
                hit = original(flags, start, delta, ignoredObject1,
                    ignoredObject2, result);
#if HALOMCCVR_DEV_DIAGNOSTICS
                if (!g_halo2CollisionOwnQuery)
                    Halo2ImpactNoteQuery(start, delta, result, hit);
#endif
                if (!g_halo2CollisionOwnQuery)
                {
                    __try { Halo2WorldCollisionEngineContextTick(); }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        g_halo2WorldCollision.failures.fetch_add(
                            1, std::memory_order_relaxed);
                        g_halo2WorldCollision.active.store(
                            false, std::memory_order_release);
                    }
                }
            }
        }
        __finally
        { g_halo2WorldCollision.callbacks.fetch_sub(1, std::memory_order_acq_rel); }
        return hit;
    }

    bool PrepareFinalPaletteContext(
        const Halo2CameraBasis& renderCamera, int graphId,
        const float* source, uint32_t count) noexcept
    {
        g_finalPaletteContext = Halo2FinalPaletteContext{};
        if (!kHalo2FinalPaletteControllerOwnershipEnabled || !source ||
            !count || count > kHalo2FirstPersonPaletteCapacity ||
            !Game_Halo2ControllerAimActive() ||
            !g_finalPaletteReady.load(std::memory_order_acquire))
        {
            return false;
        }
        Halo2FinalPaletteContext candidate{};
        float leftOrientation[4]{}, leftPosition[3]{};
        float headOrientation[4]{}, headPosition[3]{};
        if (!ResolveArmBinding(graphId, count, candidate.binding) ||
            !BuildFirstPersonCarrier(renderCamera, candidate.rightCarrier) ||
            !VR_GetHeadPose(headOrientation, headPosition) ||
            !VR_GetLeftControllerPose(leftOrientation, leftPosition) ||
            !Halo2BuildControllerCarrier(
                renderCamera, headOrientation, headPosition, leftOrientation,
                leftPosition, Game_GetWorldScale(),
                weapon_alignment::Forward(true,g_config.left_gun_forward_m,g_config.left_hand_forward_m),
                weapon_alignment::Right(false,g_config.left_gun_right_m),
                weapon_alignment::Up(g_config.left_gun_up_m),candidate.leftCarrier))
        {
            return false;
        }
        candidate.source = source;
        candidate.count = count;
        candidate.valid = true;
        g_finalPaletteContext = candidate;
        return true;
    }

    __declspec(noinline) void __fastcall Halo2FinalPaletteComposeDetour(
        const float* root, const float* source, float* destination)
    {
        g_finalPaletteActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const auto original = reinterpret_cast<Halo2MatrixComposeFn>(
            g_finalPaletteOriginal.load(std::memory_order_acquire));
        if (original)
            original(root, source, destination);

        bool changed = false;
        __try
        {
            const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
            const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            Halo2FinalPaletteContext& context = g_finalPaletteContext;
            const bool admittedCaller = base &&
                (caller == base + kHalo2FirstPersonPrimaryComposeReturnRva ||
                 caller == base + kHalo2FirstPersonSecondaryComposeReturnRva);
            if (original && admittedCaller && destination && source &&
                context.valid && context.source && context.count &&
                context.count <= kHalo2FirstPersonPaletteCapacity)
            {
                const uintptr_t first =
                    reinterpret_cast<uintptr_t>(context.source);
                const uintptr_t address = reinterpret_cast<uintptr_t>(source);
                const uintptr_t bytes = static_cast<uintptr_t>(context.count) *
                    kHalo2FirstPersonNodeStride;
                if (first <= UINTPTR_MAX - bytes && address >= first &&
                    address < first + bytes &&
                    (address - first) % kHalo2FirstPersonNodeStride == 0)
                {
                    const uint32_t index = static_cast<uint32_t>(
                        (address - first) / kHalo2FirstPersonNodeStride);
                    const uint64_t bit = uint64_t{1} << index;
                    const bool right = (context.binding.rightSubtree & bit) != 0;
                    const bool left = (context.binding.leftSubtree & bit) != 0;
                    if (right && index == static_cast<uint32_t>(
                            context.binding.rightWrist))
                    {
                        context.rightDeltaValid =
                            Halo2BuildFinalPaletteWristDelta(
                                destination, context.rightCarrier,
                                context.rightRotation,
                                context.rightStockPosition,
                                context.rightDesiredPosition);
                    }
                    if (left && index == static_cast<uint32_t>(
                            context.binding.leftWrist))
                    {
                        context.leftDeltaValid =
                            Halo2BuildFinalPaletteWristDelta(
                                destination, context.leftCarrier,
                                context.leftRotation,
                                context.leftStockPosition,
                                context.leftDesiredPosition);
                    }
                    if (right && context.rightDeltaValid)
                    {
                        Halo2ReanchorFirstPersonNode(
                            destination, context.rightRotation,
                            context.rightStockPosition,
                            context.rightDesiredPosition);
                        g_finalPaletteMovedRight.fetch_add(
                            1, std::memory_order_relaxed);
                        changed = true;
                    }
                    else if (left && context.leftDeltaValid)
                    {
                        Halo2ReanchorFirstPersonNode(
                            destination, context.leftRotation,
                            context.leftStockPosition,
                            context.leftDesiredPosition);
                        g_finalPaletteMovedLeft.fetch_add(
                            1, std::memory_order_relaxed);
                        changed = true;
                    }
                    else if (!right && !left && std::isfinite(destination[0]))
                    {
                        // This is the final independent palette: no hierarchy
                        // is evaluated after it. Collapse every non-hand node,
                        // matching Halo 3/Reach/Halo 4, so no arm can remain on
                        // either controller while the held gun stays in the
                        // right-wrist subtree.
                        destination[0] *= 0.0001f;
                        g_finalPaletteCollapsed.fetch_add(
                            1, std::memory_order_relaxed);
                        changed = true;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
        }
        if (changed)
            g_finalPaletteCalls.fetch_add(1, std::memory_order_relaxed);
        g_finalPaletteActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    __declspec(noinline) void __fastcall Halo2VisibleFirstPersonConsumerDetour(
        uint32_t user, uint32_t modelObject, uint32_t ownerObject,
        int32_t weaponSlot, float* matrices)
    {
        g_visibleConsumerActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_visibleConsumerCalls.fetch_add(1, std::memory_order_relaxed);
        const auto original =
            reinterpret_cast<Halo2VisibleFirstPersonConsumerFn>(
                g_visibleConsumerOriginal.load(std::memory_order_acquire));
        auto& context = g_visibleConsumerContext;
        if(Game_IsScopeRendering()&&original)
        {
            // Three independent persistent banks: native packet submission can
            // retain the hands and both weapons until the subsequent draw.
            static thread_local float hidden[3][kHalo2FirstPersonPaletteCapacity*13];
            const int bank=weaponSlot==-1?0:weaponSlot==0?1:weaponSlot==1?2:-1;
            const uint32_t count=bank==0?context.handsCount:bank==1?context.gunCount:context.secondaryGunCount;
            __try
            {
                if(bank>=0&&context.valid&&user==context.user&&matrices&&count>0&&count<=kHalo2FirstPersonPaletteCapacity)
                {
                    memcpy(hidden[bank],matrices,count*kHalo2FirstPersonNodeStride);
                    for(uint32_t i=0;i<count;++i)hidden[bank][i*13]=.0001f;
                    original(user,modelObject,ownerObject,weaponSlot,hidden[bank]);
                }
                else {VR_EndHalo2Scope(false);original(user,modelObject,ownerObject,weaponSlot,matrices);}
            }
            __finally {g_visibleConsumerActiveCallbacks.fetch_sub(1,std::memory_order_acq_rel);}
            return;
        }
        bool callCurrentOriginal = true;
        if (original && matrices && context.valid && user == context.user &&
            g_armed.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) &&
            !g_teardownRequested.load(std::memory_order_acquire))
        {
            __try
            {
                if (!context.handsDeferred && weaponSlot == -1 &&
                    ownerObject == context.unitObject)
                {
                    context.handsDeferred = true;
                    context.handsModelObject = modelObject;
                    context.handsOwnerObject = ownerObject;
                    context.handsMatrices = matrices;
                    callCurrentOriginal = false;
                }
                else if (!context.gunApplied && context.handsDeferred &&
                         context.secondaryWeaponObject != UINT32_MAX &&
                         !context.primaryGunDeferred && weaponSlot == 0 &&
                         ownerObject == context.weaponObject)
                {
                    context.primaryGunMatrices = matrices;
                    context.primaryGunModelObject = modelObject;
                    context.primaryGunDeferred = true;
                    callCurrentOriginal = false;
                }
                else if (!context.gunApplied && context.handsDeferred &&
                         ((context.secondaryWeaponObject == UINT32_MAX &&
                           weaponSlot == 0 && ownerObject == context.weaponObject) ||
                          (context.primaryGunDeferred && weaponSlot == 1 &&
                           ownerObject == context.secondaryWeaponObject)))
                {
                    const bool dual = context.primaryGunDeferred;
                    float* const primaryMatrices = dual ? context.primaryGunMatrices : matrices;
                    const uint32_t primaryModel = dual ? context.primaryGunModelObject : modelObject;
                    Halo2FinalPacketOwnershipResult result{};
                    PublishBarrelMeter(
                        1, context.renderCamera, context.rightCarrier,
                        primaryMatrices);
                    Halo2AnimateWeaponCarriers(context,context.handsMatrices,primaryMatrices,dual?matrices:nullptr,1,primaryModel,modelObject);
                    const Halo2AuthoredGripCapture grip(context,context.handsMatrices);
                    const bool owned = dual
                        ? Halo2OwnDualFirstPersonPackets(
                            context.handsMatrices, context.handsCount,
                            context.handsRemap, context.binding, primaryMatrices,
                            context.gunCount, context.secondaryHandsRemap,
                            context.secondaryBinding, matrices, context.secondaryGunCount,
                            context.renderCamera, context.rightCarrier, context.leftCarrier,
                            context.rightScale, context.leftScale, context.worldScale, result, context.handAlignment)
                        : Halo2OwnFinalFirstPersonPackets(
                            context.handsMatrices, context.handsCount,
                            context.handsRemap, context.binding, primaryMatrices,
                            context.gunCount, context.renderCamera,
                            context.rightCarrier, context.leftCarrier,
                            context.supportGripAttached, context.rightScale,
                            context.leftScale, context.worldScale, result,
                            context.handAlignment,
                            !g_config.floating_hands && g_config.arm_ik,
                            g_config.experimental_body_ik,
                            context.supportGripAttached,
                            context.supportFingerInput);
                    if (owned)
                    {
                        grip.Publish(context,context.handsMatrices);
                        Halo2PublishFinalPacketCollisionVolumes(
                            context, context.handsMatrices, primaryMatrices,
                            primaryModel, dual ? matrices : nullptr,
                            dual ? modelObject : UINT32_MAX);
                        Halo2ApplyVisualHandOffsets(context,context.handsMatrices,dual);
                        context.handsApplied = true;
                        context.gunApplied = true;
                        g_visibleConsumerHandsApplied.fetch_add(
                            1, std::memory_order_relaxed);
                        g_visibleConsumerGunsApplied.fetch_add(
                            1, std::memory_order_relaxed);
                        g_finalPaletteCalls.fetch_add(1, std::memory_order_relaxed);
                        g_finalPaletteMovedRight.fetch_add(
                            result.rightNodes, std::memory_order_relaxed);
                        g_finalPaletteMovedLeft.fetch_add(
                            result.leftNodes, std::memory_order_relaxed);
                        g_finalPaletteCollapsed.fetch_add(
                            result.collapsedNodes, std::memory_order_relaxed);
                        g_finalPaletteCoLocatedArms.fetch_add(
                            result.coLocatedArmNodes, std::memory_order_relaxed);
                        g_packetBuilderGunNodes.fetch_add(
                            result.gunNodes, std::memory_order_relaxed);
                        const auto millimeters = [&context](float world) {
                            return static_cast<uint32_t>(std::min(
                                world / context.worldScale * 1000.0f,
                                100000.0f));
                        };
                        const uint32_t rightMm =
                            millimeters(result.rightWristDeltaWorld);
                        const uint32_t leftMm =
                            millimeters(result.leftWristDeltaWorld);
                        g_packetBuilderRightDeltaMillimeters.store(
                            rightMm, std::memory_order_relaxed);
                        g_packetBuilderLeftDeltaMillimeters.store(
                            leftMm, std::memory_order_relaxed);
                        auto publishMaximum = [](
                            std::atomic<uint32_t>& destination,
                            uint32_t value) {
                            uint32_t previous =
                                destination.load(std::memory_order_relaxed);
                            while (previous < value &&
                                   !destination.compare_exchange_weak(
                                       previous, value,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed))
                            {
                            }
                        };
                        publishMaximum(
                            g_packetBuilderMaxRightDeltaMillimeters, rightMm);
                        publishMaximum(
                            g_packetBuilderMaxLeftDeltaMillimeters, leftMm);
                        g_packetBuilderLastAppliedMs.store(
                            GetTickCount64(), std::memory_order_release);
                    }
                    context.handsDeferred = false;
                    original(user, context.handsModelObject,
                        context.handsOwnerObject, -1, context.handsMatrices);
                    context.handsMatrices = nullptr;
                    if (dual)
                    {
                        context.primaryGunDeferred = false;
                        original(user, primaryModel, context.weaponObject, 0, primaryMatrices);
                        context.primaryGunMatrices = nullptr;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (original && callCurrentOriginal)
        {
            __try
            {
                original(user, modelObject, ownerObject, weaponSlot, matrices);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_visibleConsumerActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    // E-H2-71 (C-H2-83). Signature: bool(user_index, position_index, out).
    using Halo2InterpolatedFrameFn = uint8_t(__fastcall*)(int, int, void*);

    __declspec(noinline) uint8_t __fastcall Halo2InterpolatedFrameDetour(
        int userIndex, int positionIndex, void* out)
    {
        g_interpFrameActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_interpFrameCalls.fetch_add(1, std::memory_order_relaxed);
        const auto original = reinterpret_cast<Halo2InterpolatedFrameFn>(
            g_interpFrameOriginal.load(std::memory_order_acquire));
        uint8_t result = 0;
        // Only the mod's own Classic first-person packet build is changed.
        // Every other caller - the world, the other renderer, any other
        // user - keeps the engine's interpolated frame exactly.
        const bool forceCurrent = g_classicBuildInProgress &&
            userIndex == static_cast<int>(kOwnedUser) &&
            g_armed.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) &&
            !g_teardownRequested.load(std::memory_order_acquire);
        if (forceCurrent)
        {
            g_interpFrameForcedCurrent.fetch_add(1, std::memory_order_relaxed);
        }
        else if (original)
        {
            __try { result = original(userIndex, positionIndex, out); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
                result = 0;
            }
        }
        g_interpFrameActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    // Persistent support grip: durable relationship compatibility for one
    // packet-builder invocation. Read-only; the shared OpenXR input path owns
    // all durable mutation. When no relationship is engaged the existing
    // behaviour is preserved, and for a title this feature is not wired for
    // (or with the feature off) the decision is the constant base one.
    struct Halo2SupportUse
    {
        bool relationshipEngaged = false;
        bool ownerUsable = true;
        bool readable = false;
        bool detach = false;
    };
    Halo2SupportUse Halo2EvaluateSupportUse(
        const Halo2ObserverPosePublication& publication,
        uint32_t unitObject, uint32_t weaponObject) noexcept
    {
        Halo2SupportUse result{};
        if (!VR_SupportGripWiredForTitle(GameTitle::Halo2))
            return result;
        SupportGripRelationshipSnapshot relationship{};
        result.readable = VR_GetSupportGripRelationship(relationship);
        bool ownerTrusted = false;
        if (result.readable && relationship.engaged)
        {
            result.relationshipEngaged = true;
            result.ownerUsable = support_grip::InvocationOwnerTrusted(
                relationship.engaged,
                support_grip::OwnerTuple{relationship.title,
                    relationship.generation, relationship.unit,
                    relationship.weapon},
                support_grip::OwnerEvidence::KnownPresent,
                support_grip::OwnerTuple{GameTitle::Halo2, publication.generation,
                    unitObject, weaponObject});
            ownerTrusted = result.ownerUsable;
        }
        // F15: a failed relationship read is continuity-unprovable, not "no
        // relationship", so it detaches (the shared unit-tested matrix).
        result.detach = support_grip::SupportInvocationMustDetach(result.readable,
            result.relationshipEngaged, ownerTrusted,
            publication.snapshot.twoHandAimActive);
        return result;
    }
    bool Halo2SupportCarrierMustDetach(
        const Halo2ObserverPosePublication& publication,
        uint32_t unitObject, uint32_t weaponObject) noexcept
    {
        const Halo2SupportUse use =
            Halo2EvaluateSupportUse(publication, unitObject, weaponObject);
        return use.detach;
    }
    // Persistent support grip: owner-qualified detach decision for the live
    // carrier consumers outside the packet builder (native aim/aim assist and
    // the scope camera). A relationship read failure is continuity-unprovable,
    // not "no relationship", so it detaches. A readable disengaged
    // relationship detaches only when this publication's frozen snapshot still
    // carries support-derived geometry (the release discontinuity); an
    // ordinary one-hand publication stays valid. While engaged this reads the
    // CURRENT owner with the same production reader the packet builder uses,
    // so only this invocation's own proof can authorize a support carrier; a
    // stale globally published evidence record is never an input. Explicit
    // absence, Unknown evidence and any owner mismatch fail closed and detach
    // to the independent one-hand carrier. The release/read-failure decision is
    // the shared unit-tested support_grip::SupportInvocationMustDetach, so both
    // H2 helper forms use the same matrix. With the feature off or the title
    // unwired the decision is the constant base one (never detach).
    bool Halo2SupportCarrierMustDetach(
        const Halo2ObserverPosePublication& publication) noexcept
    {
        if (!VR_SupportGripWiredForTitle(GameTitle::Halo2))
            return false;
        SupportGripRelationshipSnapshot relationship{};
        const bool readable = VR_GetSupportGripRelationship(relationship);
        const bool engaged = readable && relationship.engaged;
        bool ownerTrusted = false;
        if (engaged)
        {
            PrimaryWeaponEvidence currentEvidence{};
            const bool readOk = Game_ReadPrimaryWeaponEvidence(
                GameTitle::Halo2, currentEvidence);
            const support_grip::OwnerEvidence evidenceState = readOk
                ? static_cast<support_grip::OwnerEvidence>(currentEvidence.state)
                : support_grip::OwnerEvidence::Unknown;
            ownerTrusted = !support_grip::SupportCarrierMustDetachForInvocation(
                relationship.engaged,
                support_grip::OwnerTuple{relationship.title,
                    relationship.generation, relationship.unit,
                    relationship.weapon},
                evidenceState,
                support_grip::OwnerTuple{GameTitle::Halo2,
                    currentEvidence.generation, currentEvidence.unit,
                    currentEvidence.weapon});
        }
        return support_grip::SupportInvocationMustDetach(readable, engaged,
            ownerTrusted, publication.snapshot.twoHandAimActive);
    }

    // ---- Weapon-order diagnostic tranche (read-only evidence) ----
    void Halo2DiagnosticPublish(uint8_t kind, uint8_t status,
        uint64_t preparedSerial, uint32_t unit, uint32_t weapon, uint64_t aux0,
        uint64_t aux1) noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return;
        Telemetry_PublishWeaponEvent(kind, status,
            static_cast<uint8_t>(GameTitle::Halo2),
            g_generation.load(std::memory_order_acquire), preparedSerial,
            unit, weapon, aux0, aux1);
    }

    __declspec(noinline) int __fastcall Halo2FirstPersonPacketBuilderDetour(
        uint32_t user, uint32_t unitObject, const float* position,
        const float* forward, const float* up, int packetCapacity,
        uint32_t* packets, uint8_t publishToRenderer)
    {
        if (user == kOwnedUser)
            Halo2DiagnosticPublish(
                static_cast<uint8_t>(WeaponOrderEventKind::FpEntry),
                static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation),
                0, UINT32_MAX, UINT32_MAX, publishToRenderer, 0);
        g_packetBuilderActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_packetBuilderCalls.fetch_add(1, std::memory_order_relaxed);
        const auto original = reinterpret_cast<Halo2FirstPersonPacketBuilderFn>(
            g_packetBuilderOriginal.load(std::memory_order_acquire));
        g_visibleConsumerContext = Halo2VisibleConsumerContext{};
        const bool anniversaryConsumer = publishToRenderer != 0;
        const bool classicConsumer = !anniversaryConsumer &&
            Halo2Stereo_Armed() && !g_classicPacketContext.eyeActive;
        Halo2VisibleConsumerContext candidate{};
        if (classicConsumer)
        {
            g_classicPacketCalls.fetch_add(1, std::memory_order_relaxed);
            g_classicPacketContext.valid = false;
        }
        if (original && user == kOwnedUser &&
            (anniversaryConsumer || classicConsumer) && packets &&
            packetCapacity > 0 && g_armed.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) &&
            !g_teardownRequested.load(std::memory_order_acquire) &&
            g_finalPaletteReady.load(std::memory_order_acquire))
        {
            __try
            {
                const uintptr_t module =
                    g_moduleBase.load(std::memory_order_acquire);
                const uintptr_t userArray = module
                    ? *reinterpret_cast<const volatile uintptr_t*>(
                          module + kHalo2FirstPersonUserDataPointerRva)
                    : 0;
                const uintptr_t weaponData = userArray
                    ? userArray + static_cast<uintptr_t>(user) *
                          kHalo2FirstPersonUserStride +
                          kHalo2FirstPersonWeaponDataOffset
                    : 0;
                const uint8_t flags = weaponData
                    ? *reinterpret_cast<const volatile uint8_t*>(weaponData)
                    : 0;
                const uint32_t weaponObject = weaponData
                    ? *reinterpret_cast<const volatile uint32_t*>(
                          weaponData + kHalo2FirstPersonWeaponObjectOffset)
                    : UINT32_MAX;
                const int graphId = weaponData
                    ? *reinterpret_cast<const volatile int32_t*>(
                          weaponData + kHalo2FirstPersonWeaponGraphOffset)
                    : -1;
                const uint32_t gunCount = weaponData
                    ? *reinterpret_cast<const volatile uint32_t*>(
                          weaponData + kHalo2FirstPersonWeaponNodeCountOffset)
                    : 0;
                const uint32_t handsCount = weaponData
                    ? *reinterpret_cast<const volatile uint32_t*>(
                          weaponData + kHalo2FirstPersonHandsNodeCountOffset)
                    : 0;
                const uint32_t animationCount = weaponData
                    ? *reinterpret_cast<const volatile uint32_t*>(
                          weaponData +
                          kHalo2FirstPersonAnimationNodeCountOffset)
                    : 0;
                const int32_t* const handsRemap = weaponData
                    ? reinterpret_cast<const int32_t*>(
                          weaponData + kHalo2FirstPersonHandsRemapOffset)
                    : nullptr;
                const uintptr_t secondaryDataForAim = weaponData
                    ? weaponData + kHalo2FirstPersonWeaponSlotStride : 0;
                const bool independentPrimary = secondaryDataForAim &&
                    (*reinterpret_cast<const volatile uint8_t*>(secondaryDataForAim) & 1u) &&
                    *reinterpret_cast<const volatile uint32_t*>(secondaryDataForAim +
                        kHalo2FirstPersonWeaponObjectOffset) != UINT32_MAX;
                Halo2CameraBasis renderCamera{};
                if (position && forward && up)
                {
                    std::memcpy(renderCamera.position, position, 12);
                    std::memcpy(renderCamera.forward, forward, 12);
                    std::memcpy(renderCamera.up, up, 12);
                }
                Halo2FirstPersonArmBinding binding{};
                Halo2CameraBasis rightCarrier{}, leftCarrier{};
                Halo2ObserverPosePublication publication{};
                const uint32_t generation =
                    g_generation.load(std::memory_order_acquire);
                g_packetBuilderEligible.fetch_add(1, std::memory_order_relaxed);
                // The packet builder call plus its exact unit/weapon packet
                // identities are the ownership proof. The old 0x07 byte gate
                // described Chief's weapon_data state, but H2EK does not make
                // that byte a character-independent packet contract; applying
                // it here silently excluded Arbiter before his authored graph
                // and packets could be validated.
                if (weaponObject == UINT32_MAX || !gunCount || !handsCount ||
                    !handsRemap ||
                    !ResolveArmBinding(graphId, animationCount, binding) ||
                    !Halo2ValidateCameraBasis(renderCamera))
                {
                    g_packetBuilderWeaponStateMiss.fetch_add(
                        1, std::memory_order_relaxed);
                }
                else if (!Halo2Observer6Dof_ReadPublishedPose(publication))
                {
                    g_packetBuilderPublicationMiss.fetch_add(
                        1, std::memory_order_relaxed);
                }
                else if (!Halo2ObserverControllerSnapshotUsable(
                             publication, generation))
                {
                    g_packetBuilderControllerSnapshotMiss.fetch_add(
                        1, std::memory_order_relaxed);
                }
                else if (!BuildStableFirstPersonCarriers(
                             publication, generation, rightCarrier, leftCarrier,
                             independentPrimary ||
                                 Halo2SupportCarrierMustDetach(publication,
                                     unitObject, weaponObject)))
                {
                    g_packetBuilderCarrierMiss.fetch_add(
                        1, std::memory_order_relaxed);
                }
                else
                {
                    // Keep a separate pre-visual frame for barrel aiming. The
                    // committed mesh still owns collision, effects and reload.
                    candidate.muzzleNeutralCarrier[0]=rightCarrier;
                    candidate.muzzleNeutralCarrier[1]=leftCarrier;
                    {
                        // Classic: the fixed correction plus any saved halo2_classic_* adjustment;
                        // each hand also gets its weapon's default visual angles.
                        float ry=0,rp=0,rr=0,ly=0,lp=0,lr=0;
                        VR_LegacyDefaultVisualAngles(false,classicConsumer,ry,rp,rr);
                        VR_LegacyDefaultVisualAngles(true,classicConsumer,ly,lp,lr);
                        const float pitch=g_config.barrel_pitch_deg+
                            (classicConsumer?weapon_alignment::kH2ClassicVisualPitch+
                                (g_config.halo2_classic_gun_pitch_deg-weapon_alignment::kH2ClassicLegacyPitch):0.0f);
                        Halo2CameraBasis trimmedRight{},trimmedLeft{};
                        const float yaw=g_config.barrel_yaw_deg+
                            (classicConsumer?weapon_alignment::kH2ClassicVisualYaw+
                                (g_config.halo2_classic_gun_yaw_deg-weapon_alignment::kH2ClassicLegacyYaw):0.0f);
                        const float classicRoll=g_config.barrel_roll_deg+
                            (classicConsumer?g_config.halo2_classic_gun_roll_deg:0.0f);
                        const float classicForward=classicConsumer?g_config.halo2_classic_gun_forward_m:0.0f;
                        const float classicRight=classicConsumer?g_config.halo2_classic_gun_right_m:0.0f;
                        const float classicUp=classicConsumer?g_config.halo2_classic_gun_up_m:0.0f;
                        if (Halo2ApplyCarrierTrim(rightCarrier,pitch+rp,yaw+ry,
                                classicRoll+rr,classicForward,classicRight,classicUp,Game_GetWorldScale(),trimmedRight) &&
                            Halo2ApplyCarrierTrim(leftCarrier,pitch+lp,yaw+ly,
                                classicRoll+lr,classicForward,classicRight,classicUp,Game_GetWorldScale(),trimmedLeft)) {
                            rightCarrier=trimmedRight;leftCarrier=trimmedLeft;
                            g_classicTrimApplied.fetch_add(1,std::memory_order_relaxed);
                        } else g_classicTrimRefused.fetch_add(1,std::memory_order_relaxed);
                    }
                    candidate.muzzleVisualCarrier[0]=rightCarrier;
                    candidate.muzzleVisualCarrier[1]=leftCarrier;
                    candidate.muzzleVisualFrameValid=true;
                    auto& context = candidate;
                    // H2EK 306E04 enumerates two weapon_data records before
                    // publishing one merged hands packet and separate guns.
                    const uintptr_t secondaryData = weaponData +
                        kHalo2FirstPersonWeaponSlotStride;
                    const uint32_t secondaryObject =
                        *reinterpret_cast<const volatile uint32_t*>(
                            secondaryData + kHalo2FirstPersonWeaponObjectOffset);
                    if (secondaryObject != UINT32_MAX &&
                        (*reinterpret_cast<const volatile uint8_t*>(secondaryData) & 1u))
                    {
                        const uint32_t secondaryHandsCount =
                            *reinterpret_cast<const volatile uint32_t*>(secondaryData +
                                kHalo2FirstPersonHandsNodeCountOffset);
                        context.secondaryGunCount =
                            *reinterpret_cast<const volatile uint32_t*>(secondaryData +
                                kHalo2FirstPersonWeaponNodeCountOffset);
                        const int32_t secondaryGraph =
                            *reinterpret_cast<const volatile int32_t*>(secondaryData +
                                kHalo2FirstPersonWeaponGraphOffset);
                        const uint32_t secondaryAnimationCount =
                            *reinterpret_cast<const volatile uint32_t*>(secondaryData +
                                kHalo2FirstPersonAnimationNodeCountOffset);
                        if (secondaryObject == weaponObject ||
                            secondaryHandsCount != handsCount ||
                            !context.secondaryGunCount ||
                            context.secondaryGunCount > kHalo2FirstPersonPaletteCapacity ||
                            !ResolveArmBinding(secondaryGraph, secondaryAnimationCount,
                                context.secondaryBinding))
                        {
                            g_packetBuilderWeaponStateMiss.fetch_add(1, std::memory_order_relaxed);
                            __leave;
                        }
                        context.secondaryWeaponObject = secondaryObject;
                        VR_ObserveSecondaryWeaponPresentation(GameTitle::Halo2, generation);
                        context.secondaryHandsRemap = reinterpret_cast<const int32_t*>(
                            secondaryData + kHalo2FirstPersonHandsRemapOffset);
                    }
                    context.user = user;
                    context.unitObject = unitObject;
                    context.weaponObject = weaponObject;
                    g_sightWeaponObject.store(weaponObject, std::memory_order_relaxed);
                    g_sightWeaponAtMs.store(GetTickCount64(), std::memory_order_release);
                    context.handsCount = handsCount;
                    context.gunCount = gunCount;
                    context.handsRemap = handsRemap;
                    context.binding = binding;
                    context.renderCamera = renderCamera;
                    // E-H2-70: publish this renderer's packet-build camera.
                    {
                        const int slot = anniversaryConsumer ? 1 : 0;
                        g_packetCameraVersion[slot].fetch_add(
                            1, std::memory_order_acq_rel);
                        g_packetCamera[slot] = renderCamera;
                        g_packetCameraVersion[slot].fetch_add(
                            1, std::memory_order_acq_rel);
                        g_packetCameraValid[slot].store(
                            true, std::memory_order_release);
                    }
                    context.collisionRightCarrier = rightCarrier;
                    context.collisionLeftCarrier = leftCarrier;
                    (void)Halo2ApplyCollisionCorrection(
                        1, generation, Game_GetWorldScale(), rightCarrier,
                        context.rightCollisionCorrection);
                    (void)Halo2ApplyCollisionCorrection(
                        0, generation, Game_GetWorldScale(), leftCarrier,
                        context.leftCollisionCorrection);
                    context.rightCarrier = rightCarrier;
                    context.leftCarrier = leftCarrier;
                    context.handAlignment = publication.snapshot.handAlignment;
                    context.supportFingerInput=publication.snapshot.controllerFingers[0];
                    const Halo2SupportUse supportUse =
                        Halo2EvaluateSupportUse(publication, unitObject,
                            weaponObject);
                    // A released relationship with a stale support-derived
                    // publication and an unreadable snapshot both present no
                    // support hand and no two-hand aim authority. An ordinary
                    // one-hand publication keeps the existing presentation.
                    context.supportGripAttached = !independentPrimary && !supportUse.detach &&
                        (supportUse.relationshipEngaged
                            ? supportUse.ownerUsable
                            : publication.snapshot.twoHandAimActive);
                    context.twoHandAimActive = !independentPrimary && !supportUse.detach &&
                        supportUse.ownerUsable && publication.snapshot.twoHandAimActive;
                    // The durable evidence publication deliberately does NOT
                    // happen from this render hook: the writer-side slot
                    // protocol is documented as never called from a render or
                    // palette hook. Halo 2's liveness producer is the level-live
                    // observer poll (see Halo2Observer6Dof_Poll), which also
                    // runs while no weapon packet is admitted (F09) and reads
                    // the same guarded direct reader.
                    context.rightScale =
                        std::clamp(g_config.gun_scale, 0.3f, 3.0f);
                    context.leftScale =
                        std::clamp(g_config.left_hand_scale, 0.3f, 3.0f);
                    context.worldScale = Game_GetWorldScale();
                    context.muzzleGeneration=publication.generation;
                    context.muzzleSpace=publication.snapshot.trackingSpaceEpoch;
                    context.muzzleSerial=publication.serial;
                    context.muzzleTimeNs=publication.snapshot.predictedDisplayTimeNs;
                    context.muzzleLeftHanded=g_config.left_handed;
                    Halo2PrepareContactFrames(publication,independentPrimary,context);
                    context.valid = true;
                    // Diagnostic-only FP weapon commit: admitted packet
                    // identity for this renderer (no gameplay effect).
                    Halo2DiagnosticPublish(
                        static_cast<uint8_t>(
                            WeaponOrderEventKind::FpWeaponCommit),
                        static_cast<uint8_t>(
                            WeaponOrderEventStatus::Success),
                        publication.serial, unitObject,
                        context.weaponObject,
                        (anniversaryConsumer ? 1u : 0u) |
                            ((context.secondaryWeaponObject != UINT32_MAX) ?
                                2u : 0u),
                        0);
                    if (binding.rigKind ==
                        Halo2FirstPersonRigKind::MasterChief)
                    {
                        g_packetBuilderChiefContexts.fetch_add(
                            1, std::memory_order_relaxed);
                        g_packetBuilderLastChiefFlags.store(
                            flags, std::memory_order_relaxed);
                    }
                    else if (binding.rigKind ==
                             Halo2FirstPersonRigKind::Elite)
                    {
                        g_packetBuilderEliteContexts.fetch_add(
                            1, std::memory_order_relaxed);
                        g_packetBuilderLastEliteFlags.store(
                            flags, std::memory_order_relaxed);
                    }
                    if (anniversaryConsumer)
                        g_visibleConsumerContext = context;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }

        int packetCount = 0;
        if (original)
        {
            // E-H2-71 (C-H2-83): for the CLASSIC build only, the engine
            // prefers the frame-INTERPOLATED first-person frame while
            // Anniversary always composes against the current tick frame
            // (user_data+0x20C8). Identical packets expressed in two
            // different frames read as a rotation - the muzzle rise. Mark
            // the classic build so the interpolated-frame getter reports
            // none, which is the engine's own documented fallback to that
            // same current frame.
            const bool markClassicBuild = classicConsumer;
            if (markClassicBuild)
                g_classicBuildInProgress = true;
            __try
            {
                packetCount = original(
                    user, unitObject, position, forward, up, packetCapacity,
                    packets, publishToRenderer);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
            }
            if (markClassicBuild)
                g_classicBuildInProgress = false;
        }
        // The Anniversary callback normally publishes hands then gun. If a
        // malformed/empty packet set omits the gun, fail this optional alignment
        // transaction open by publishing the untouched deferred hands now.
        if (anniversaryConsumer && g_visibleConsumerContext.handsDeferred)
        {
            const auto consumer =
                reinterpret_cast<Halo2VisibleFirstPersonConsumerFn>(
                    g_visibleConsumerOriginal.load(std::memory_order_acquire));
            if (consumer)
            {
                __try
                {
                    consumer(user, g_visibleConsumerContext.handsModelObject,
                        g_visibleConsumerContext.handsOwnerObject, -1,
                        g_visibleConsumerContext.handsMatrices);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    g_finalPaletteRefused.fetch_add(
                        1, std::memory_order_relaxed);
                }
            }
            g_visibleConsumerContext.handsDeferred = false;
            g_visibleConsumerContext.handsMatrices = nullptr;
        }
        if (anniversaryConsumer && g_visibleConsumerContext.primaryGunDeferred)
        {
            // Missing/invalid secondary packet: submit the untouched primary
            // after the hands, exactly once, instead of losing the weapon.
            auto& context = g_visibleConsumerContext;
            context.primaryGunDeferred = false;
            const auto consumer = reinterpret_cast<Halo2VisibleFirstPersonConsumerFn>(
                g_visibleConsumerOriginal.load(std::memory_order_acquire));
            if (consumer)
            {
                __try
                {
                    consumer(user, context.primaryGunModelObject,
                        context.weaponObject, 0, context.primaryGunMatrices);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
                }
            }
            context.primaryGunMatrices = nullptr;
        }
        bool applied = anniversaryConsumer &&
            g_visibleConsumerContext.handsApplied &&
            g_visibleConsumerContext.gunApplied;
        if (classicConsumer && candidate.valid && packetCount > 0 &&
            packetCount <= packetCapacity &&
            packetCount <= kHalo2FrameInterpolatorFirstPersonSlots + 1)
        {
            __try
            {
                float* handsMatrices = nullptr;
                float* gunMatrices = nullptr;
                float* secondaryMatrices = nullptr;
                uint32_t gunRenderModelTag = UINT32_MAX;
                uint32_t secondaryRenderModelTag = UINT32_MAX;
                for (int packetIndex = 0; packetIndex < packetCount;
                     ++packetIndex)
                {
                    uint8_t* const packet =
                        reinterpret_cast<uint8_t*>(packets) +
                        static_cast<uintptr_t>(packetIndex) *
                            kHalo2FirstPersonRenderPacketStride;
                    const uint32_t packetObject =
                        *reinterpret_cast<const uint32_t*>(packet + 4);
                    if (!handsMatrices && packetObject == candidate.unitObject)
                    {
                        handsMatrices = reinterpret_cast<float*>(
                            packet + kHalo2FirstPersonRenderPacketHeaderBytes);
                    }
                    else if (!gunMatrices &&
                             packetObject == candidate.weaponObject)
                    {
                        gunRenderModelTag =
                            *reinterpret_cast<const uint32_t*>(packet);
                        gunMatrices = reinterpret_cast<float*>(
                            packet + kHalo2FirstPersonRenderPacketHeaderBytes);
                    }
                    else if (!secondaryMatrices &&
                             candidate.secondaryWeaponObject != UINT32_MAX &&
                             packetObject == candidate.secondaryWeaponObject)
                    {
                        secondaryRenderModelTag = *reinterpret_cast<const uint32_t*>(packet);
                        secondaryMatrices = reinterpret_cast<float*>(
                            packet + kHalo2FirstPersonRenderPacketHeaderBytes);
                    }
                }
                Halo2FinalPacketOwnershipResult packetResult{};
                float stockHands[
                    kHalo2FirstPersonPaletteCapacity *
                    kHalo2FirstPersonNodeFloats]{};
                float stockGun[
                    kHalo2FirstPersonPaletteCapacity *
                    kHalo2FirstPersonNodeFloats]{};
                const size_t handsBytes =
                    static_cast<size_t>(candidate.handsCount) *
                    kHalo2FirstPersonNodeStride;
                const size_t gunBytes =
                    static_cast<size_t>(candidate.gunCount) *
                    kHalo2FirstPersonNodeStride;
                const bool packetBoundsValid = handsMatrices && gunMatrices &&
                    candidate.handsCount > 0 &&
                    candidate.handsCount <= kHalo2FirstPersonPaletteCapacity &&
                    candidate.gunCount > 0 &&
                    candidate.gunCount <= kHalo2FirstPersonPaletteCapacity;
                if (packetBoundsValid)
                {
                    std::memcpy(stockHands, handsMatrices, handsBytes);
                    std::memcpy(stockGun, gunMatrices, gunBytes);
                    PublishBarrelMeter(
                        0, candidate.renderCamera, candidate.rightCarrier,
                        stockGun);
                }
                const bool dual = candidate.secondaryWeaponObject != UINT32_MAX;
                if(packetBoundsValid)
                    Halo2AnimateWeaponCarriers(candidate,handsMatrices,gunMatrices,dual?secondaryMatrices:nullptr,0,gunRenderModelTag,secondaryRenderModelTag);
                const Halo2AuthoredGripCapture grip(candidate,handsMatrices);
                const bool packetsOwned = packetBoundsValid && (dual
                    ? Halo2OwnDualFirstPersonPackets(
                        handsMatrices, candidate.handsCount,
                        candidate.handsRemap, candidate.binding, gunMatrices,
                        candidate.gunCount, candidate.secondaryHandsRemap,
                        candidate.secondaryBinding, secondaryMatrices,
                        candidate.secondaryGunCount, candidate.renderCamera,
                        candidate.rightCarrier, candidate.leftCarrier,
                        candidate.rightScale, candidate.leftScale,
                        candidate.worldScale, packetResult, candidate.handAlignment)
                    : Halo2OwnFinalFirstPersonPackets(
                        handsMatrices, candidate.handsCount,
                        candidate.handsRemap, candidate.binding, gunMatrices,
                        candidate.gunCount, candidate.renderCamera,
                        candidate.rightCarrier, candidate.leftCarrier,
                        candidate.supportGripAttached, candidate.rightScale,
                        candidate.leftScale, candidate.worldScale, packetResult,
                        candidate.handAlignment,
                        !g_config.floating_hands && g_config.arm_ik,
                        g_config.experimental_body_ik,
                        candidate.supportGripAttached,
                        candidate.supportFingerInput));
                if (packetsOwned)
                {
                    grip.Publish(candidate,handsMatrices);
                    Halo2PublishFinalPacketCollisionVolumes(
                        candidate, handsMatrices, gunMatrices,
                        gunRenderModelTag, dual ? secondaryMatrices : nullptr,
                        secondaryRenderModelTag);
                    Halo2ApplyVisualHandOffsets(candidate,handsMatrices,dual);
                    auto& classic = g_classicPacketContext;
                    classic.hands = handsMatrices;
                    classic.gun = gunMatrices;
                    classic.secondaryGun = dual ? secondaryMatrices : nullptr;
                    classic.handsCount = candidate.handsCount;
                    classic.gunCount = candidate.gunCount;
                    classic.secondaryGunCount = dual ? candidate.secondaryGunCount : 0;
                    classic.appliedAtMs = GetTickCount64();
                    classic.eyeActive = false;
                    classic.valid = true;
                    applied = true;
                    g_classicPacketApplied.fetch_add(
                        1, std::memory_order_relaxed);
                    g_finalPaletteCalls.fetch_add(1, std::memory_order_relaxed);
                    g_finalPaletteMovedRight.fetch_add(
                        packetResult.rightNodes, std::memory_order_relaxed);
                    g_finalPaletteMovedLeft.fetch_add(
                        packetResult.leftNodes, std::memory_order_relaxed);
                    g_finalPaletteCollapsed.fetch_add(
                        packetResult.collapsedNodes, std::memory_order_relaxed);
                    g_finalPaletteCoLocatedArms.fetch_add(
                        packetResult.coLocatedArmNodes,
                        std::memory_order_relaxed);
                    g_packetBuilderGunNodes.fetch_add(
                        packetResult.gunNodes, std::memory_order_relaxed);
                    g_packetBuilderLastAppliedMs.store(
                        classic.appliedAtMs, std::memory_order_release);
                }
                else if (packetBoundsValid)
                {
                    std::memcpy(handsMatrices, stockHands, handsBytes);
                    std::memcpy(gunMatrices, stockGun, gunBytes);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_finalPaletteRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (((anniversaryConsumer && g_visibleConsumerContext.valid) ||
             (classicConsumer && candidate.valid)) && !applied)
            g_packetBuilderOwnershipMiss.fetch_add(1, std::memory_order_relaxed);
        if (applied && candidate.valid)
        {
            if (candidate.binding.rigKind ==
                Halo2FirstPersonRigKind::MasterChief)
                g_packetBuilderChiefApplied.fetch_add(
                    1, std::memory_order_relaxed);
            else if (candidate.binding.rigKind ==
                     Halo2FirstPersonRigKind::Elite)
                g_packetBuilderEliteApplied.fetch_add(
                    1, std::memory_order_relaxed);
        }
        if (anniversaryConsumer)
            g_visibleConsumerContext = Halo2VisibleConsumerContext{};
        (applied ? g_packetBuilderApplied : g_packetBuilderStock)
            .fetch_add(1, std::memory_order_relaxed);
        g_packetBuilderActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return packetCount;
    }

    // E-H2-32 (C-H2-37): the re-anchor. Normally the interpolation reset holds
    // the weapon exactly at the witnessed tick's placement; C-H2-41 keeps the
    // banks live only for floaty hands. This detour runs at the per-frame read
    // the renderer makes and moves the node geometry from
    // the tick camera to the CURRENT frame camera - so the weapon follows a
    // head turn at the frame rate and jumps with the world at every tick,
    // in both renderers, without touching any view matrix.
    using Halo2InterpolatorReadFn = uint8_t(__fastcall*)(
        int, int, int, uintptr_t*, uint32_t*);

    __declspec(noinline) uint8_t __fastcall Halo2InterpolatorReadDetour(
        int player, int id, int slot, uintptr_t* nodesOut, uint32_t* countOut)
    {
        g_reanchorActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const auto original = reinterpret_cast<Halo2InterpolatorReadFn>(
            g_reanchorOriginal.load(std::memory_order_acquire));
        if (!original)
        {
            g_reanchorActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
            return 0;
        }
        uint8_t handled = 0;
        __try { handled = original(player, id, slot, nodesOut, countOut); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_exceptions.fetch_add(1, std::memory_order_relaxed);
            handled = 0;
        }
        g_reanchorEntered.fetch_add(1, std::memory_order_relaxed);
        g_reanchorLastPlayer.store(player, std::memory_order_relaxed);
        g_reanchorLastId.store(id, std::memory_order_relaxed);
        g_reanchorLastSlot.store(slot, std::memory_order_relaxed);
        g_reanchorLastHandled.store(handled, std::memory_order_relaxed);
        const bool nodesPresent = nodesOut && countOut && *nodesOut;
        if (slot == 0)
            g_finalPaletteContext = Halo2FinalPaletteContext{};
        if (nodesPresent)
            g_reanchorLastCount.store(*countOut, std::memory_order_relaxed);
        const bool live = g_armed.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) &&
            !g_teardownRequested.load(std::memory_order_acquire);
        // C-H2-38 measured the read's return value: 0 on every one of 6388
        // calls that handed back 42 valid nodes. It is not a success flag;
        // the gate is the node array itself.
        if (!nodesOut || !countOut || !*nodesOut)
            g_reanchorUnhandled.fetch_add(1, std::memory_order_relaxed);
        else if (player != static_cast<int>(kOwnedUser))
            g_reanchorOtherPlayer.fetch_add(1, std::memory_order_relaxed);
        else if (!nodesPresent)
            g_reanchorNoNodes.fetch_add(1, std::memory_order_relaxed);
        else if (!live)
            g_reanchorNotLive.fetch_add(1, std::memory_order_relaxed);
        else if (g_weaponTickIndex.load(std::memory_order_acquire) == 0)
            g_reanchorNoTick.fetch_add(1, std::memory_order_relaxed);
        else if (slot < 0 ||
                 slot >= kHalo2FrameInterpolatorFirstPersonSlots)
            g_reanchorBadSlot.fetch_add(1, std::memory_order_relaxed);
        else
        {
            Halo2CameraBasis tick{};
            bool tickValid = false;
            for (int attempt = 0; attempt < 4 && !tickValid; ++attempt)
            {
                const uint32_t before =
                    g_weaponTickBasisVersion.load(std::memory_order_acquire);
                if (before & 1u)
                    continue;
                Halo2CameraBasis candidate = g_weaponTickBasis;
                if (g_weaponTickBasisVersion.load(std::memory_order_acquire) ==
                    before)
                {
                    tick = candidate;
                    tickValid = true;
                }
            }
            // E-H2-34: the pass cameras the owning core named; the newest
            // publication only when no core has named a pass.
            Halo2FirstPersonPassCameras pass{};
            bool passValid = false;
            if (g_passCamerasSet.load(std::memory_order_acquire))
            {
                for (int attempt = 0; attempt < 4 && !passValid; ++attempt)
                {
                    const uint32_t before =
                        g_passCamerasVersion.load(std::memory_order_acquire);
                    if (before & 1u)
                        continue;
                    Halo2FirstPersonPassCameras candidate = g_passCameras;
                    if (g_passCamerasVersion.load(std::memory_order_acquire) ==
                        before)
                    {
                        pass = candidate;
                        passValid = candidate.frameValid &&
                            Halo2ValidateCameraBasis(candidate.frame);
                    }
                }
            }
            if (passValid)
            {
                g_reanchorPassCameras.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                Halo2ObserverPosePublication frame{};
                if (Halo2Observer6Dof_ReadPublishedPose(frame) &&
                    frame.generation ==
                        g_generation.load(std::memory_order_acquire) &&
                    Halo2ValidateCameraBasis(frame.tracked))
                {
                    pass = Halo2FirstPersonPassCameras{};
                    pass.frame = frame.tracked;
                    pass.frameValid = true;
                    passValid = true;
                    g_reanchorFallbackFrame.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (tickValid && passValid)
            {
                if (slot == 0)
                {
                    (void)PrepareFinalPaletteContext(
                        pass.frame, id,
                        reinterpret_cast<const float*>(*nodesOut), *countOut);
                }
                if (!pass.compensate &&
                    Halo2CameraBasesNearlyEqual(tick, pass.frame))
                {
                    g_reanchorIdentity.fetch_add(1, std::memory_order_relaxed);
                }
                bool expected = false;
                if (g_slotCacheBusy.compare_exchange_strong(
                        expected, true, std::memory_order_acq_rel))
                {
                    Halo2FirstPersonReanchorResult outcome{};
                    bool ok = false;
                    __try
                    {
                        // C-H2-46: only first-person slot 0 - the authored
                        // hands and the gun mesh they hold - is ever moved.
                        // No body, no other slot, no world geometry.
                        const bool floaty = slot == 0 &&
                            kHalo2RejectedInterpolatorControllerOwnershipEnabled &&
                            Game_Halo2ControllerAimActive();
                        if (floaty)
                        {
                            Halo2CameraBasis carrier{};
                            ok = BuildFirstPersonCarrier(pass.frame, carrier);
                            if (ok)
                            {
                                // The exact two-subtree binding is required for
                                // this presentation feature. A failure leaves
                                // the complete palette stock for this frame;
                                // falling back to C-H2-46's whole-slot carrier
                                // is what put both arms on the right hand.
                                Halo2FirstPersonArmBinding binding{};
                                Halo2CameraBasis leftCarrier{};
                                float leftOrientation[4]{}, leftPosition[3]{};
                                float headOrientation[4]{}, headPosition[3]{};
                                bool split = false;
                                if (ResolveArmBinding(id, *countOut, binding) &&
                                    VR_GetHeadPose(
                                        headOrientation, headPosition) &&
                                    VR_GetLeftControllerPose(
                                        leftOrientation, leftPosition) &&
                                    Halo2BuildControllerCarrier(
                                        pass.frame, headOrientation,
                                        headPosition, leftOrientation,
                                        leftPosition, Game_GetWorldScale(),
                                        weapon_alignment::Forward(true,g_config.left_gun_forward_m,g_config.left_hand_forward_m),
                                        weapon_alignment::Right(false,g_config.left_gun_right_m),
                                        weapon_alignment::Up(g_config.left_gun_up_m),leftCarrier))
                                {
                                    split =
                                        Halo2PlaceFirstPersonSlotOnTwoControllers(
                                            reinterpret_cast<float*>(*nodesOut),
                                            *countOut, pass.frame, carrier,
                                            leftCarrier, binding,
                                            std::clamp(
                                                g_config.gun_scale, 0.3f, 3.0f),
                                            std::clamp(
                                                g_config.left_hand_scale,
                                                0.3f, 3.0f),
                                            g_slotCache[slot], outcome);
                                }
                                if (split)
                                {
                                    ok = true;
                                    g_leftHandApplied.fetch_add(
                                        1, std::memory_order_relaxed);
                                }
                                else
                                {
                                    ok = false;
                                    g_leftHandRigidFallback.fetch_add(
                                        1, std::memory_order_relaxed);
                                }
                            }
                            (ok ? g_floatyApplied : g_floatyFailed)
                                .fetch_add(1, std::memory_order_relaxed);
                        }
                        else
                        {
                            ok = Halo2ReanchorFirstPersonSlot(
                                reinterpret_cast<float*>(*nodesOut), *countOut,
                                tick, pass, g_slotCache[slot], outcome);
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        g_exceptions.fetch_add(1, std::memory_order_relaxed);
                        ok = false;
                    }
                    g_slotCacheBusy.store(false, std::memory_order_release);
                    switch (outcome.space)
                    {
                    case Halo2FirstPersonNodeSpace::World:
                        g_reanchorSpaceWorld.fetch_add(1, std::memory_order_relaxed);
                        break;
                    case Halo2FirstPersonNodeSpace::CameraRelative:
                        g_reanchorSpaceRelative.fetch_add(1, std::memory_order_relaxed);
                        break;
                    default:
                        g_reanchorSpaceUnknown.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    if (ok && outcome.applied)
                    {
                        g_reanchorApplied.fetch_add(1, std::memory_order_relaxed);
                        if (outcome.fromCache)
                            g_reanchorFromCache.fetch_add(1, std::memory_order_relaxed);
                        if (outcome.compensated)
                            g_reanchorCompensated.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        g_reanchorSkipped.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else
                {
                    g_reanchorBusy.fetch_add(1, std::memory_order_relaxed);
                }
            }
            else
            {
                g_reanchorSkipped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_reanchorActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return handled;
    }

    __declspec(noinline) void __fastcall Halo2FirstPersonWeaponsDetour(
        uint32_t a, uint32_t b, uint8_t c)
    {
        // Count the callback BEFORE taking the trampoline, so RemoveCore's
        // drain cannot free it under a thread that already holds it.
        g_weaponsActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const auto original = reinterpret_cast<Halo2FirstPersonWeaponsFn>(
            g_weaponsOriginal.load(std::memory_order_acquire));
        if (!original)
        {
            g_weaponsActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        g_weaponsCalls.fetch_add(1, std::memory_order_relaxed);
        const bool owned = g_armed.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) &&
            !g_teardownRequested.load(std::memory_order_acquire);
        if (owned)
        {
            __try { WitnessWeaponTick(); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        __try { original(a, b, c); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_exceptions.fetch_add(1, std::memory_order_relaxed);
        }
        if (owned)
        {
            bool floatyReady = false;
            if (kHalo2RejectedInterpolatorControllerOwnershipEnabled &&
                Game_Halo2ControllerAimActive() &&
                g_reanchorOriginal.load(std::memory_order_acquire))
            {
                float hq[4]{}, hp[3]{}, aq[4]{}, ap[3]{};
                floatyReady = VR_GetHeadPose(hq, hp) && VR_GetAimPose(aq, ap);
            }
            __try
            {
                if (floatyReady)
                {
                    // H2EK's read returns nodes only while the two frame banks
                    // remain valid. The old reset deliberately invalidates
                    // them; bypass it only while the independently installed
                    // floaty transaction has a live controller sample.
                    g_weaponsResetsBypassedForFloaty.fetch_add(
                        1, std::memory_order_relaxed);
                }
                else
                {
                    ResetFirstPersonInterpolation();
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_weaponsActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    // TRACE-AND-REAIM, on foot (sight_layer.h). The intent ray starts at the rendered centre eye -- the
    // observer's tracked camera -- and runs along the aiming hand's carrier. The engine eye is the stock
    // camera: the point this builder has always aimed the local player's unit from. Everything comes from
    // one observer publication, so the three can never be sampled at different instants.
    bool Halo2SightGeometry(const Halo2ObserverPosePublication& publication,
        const Halo2CameraBasis& rightCarrier, float head[3], float dir[3],
        float eye[3]) noexcept
    {
        float lengthSquared = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            lengthSquared += rightCarrier.forward[axis] * rightCarrier.forward[axis];
        const float length = std::sqrt(lengthSquared);
        if (!std::isfinite(length) || length < 1.0e-4f)
            return false;
        // The engine eye is the stock camera WITHOUT the view's lift (physical crouch give-back, leash):
        // that is where Halo 2 fires from.
        Halo2EngineEyePosition(publication, eye);
        for (int axis = 0; axis < 3; ++axis)
        {
            head[axis] = publication.tracked.position[axis];
            dir[axis] = rightCarrier.forward[axis] / length;
            if (!std::isfinite(head[axis]) || !std::isfinite(dir[axis]) ||
                !std::isfinite(eye[axis]))
                return false;
        }
        return true;
    }

    // Simulation thread, inside the native aim update for the owned on-foot unit: trace the intent ray
    // (at most every 30 ms) with the same central collision_test_vector the physical-melee and
    // world-collision queries already run from this hook, and the PROJECTILE's own flags when they resolve
    // (g_halo2SightProjectileFlags), else those queries' flags -- structure, and objects of every type, front
    // faces only, invisible surfaces ignored (bits decoded from 0x75B850 itself: bit 0 structure, bit 3
    // objects, bits 4-16 the object types, none set meaning all; bit 23 front faces; bit 26 ignore
    // invisible; the projectile adds bit 20, its own biped exception). It ignores the unit and its held
    // weapon (the engine skips an ignored object with its children). A seated unit keeps the stock updater, but
    // its seat's ray is traced from the same hook (the note on Halo2SightTrace below). The call joins the
    // collision feature's own callback count, so the collision teardown cannot free the trampoline under it.
    uint32_t Halo2SightTraceFlags() noexcept
    {
        const uint32_t projectile = g_halo2SightProjectileFlags.load(std::memory_order_acquire);
        return projectile ? projectile : kHalo2CollisionFlags;
    }

#if HALOMCCVR_DEV_DIAGNOSTICS
    // The type of an object a trace hit (dev: the SIGHT HITS line): the datum's type byte, which the
    // salt-checked accessor already matched against the object's own +0xAA. -1: a stale handle. Guarded.
    int Halo2SightObjectType(int32_t object) noexcept
    {
        __try
        {
            const auto* data = static_cast<const uint8_t*>(
                Halo2ObjectFromIndex(static_cast<uint32_t>(object)));
            return data ? data[0xAA] : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }
#endif

    // IN A SEAT (seat_converge_logic.h) the caller hands the vehicle hierarchy's ULTIMATE PARENT
    // (`seatedRoot`) and the seat's stance key: the same ray, from the seat view's rendered eye, with that root in
    // the first ignore slot -- what Halo 2's own crosshair test passes (0x8D6A50 at 0x75998F, the second slot -1) --
    // and Halo 2 skips an ignored object with everything attached to it (0x75C59E): the occupants, their weapons,
    // a turret. So the ray starts inside our vehicle and still stops on another vehicle or a biped.
    void Halo2SightTrace(uint32_t unit, uint32_t seatedRoot = UINT32_MAX, uint32_t stance = 0) noexcept
    {
        constexpr GameTitle kTitle = GameTitle::Halo2;
        const uint64_t now = GetTickCount64();
        const bool seated = stance != 0;
        if (Sight_TraceFaulted(kTitle) ||
            !g_halo2WorldCollision.installed.load(std::memory_order_acquire) ||
            (seated && seatedRoot == UINT32_MAX) ||
            !Sight_TraceDue(kTitle, Sight_NowMs()))
            return;
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        Halo2ObserverPosePublication publication{};
        Halo2CameraBasis rightCarrier{}, leftCarrier{};
        float head[3]{}, dir[3]{}, eye[3]{};
        // The carrier the aim itself uses (BuildPublishedControllerAimDirection): with the persistent support
        // grip detached, the independent one-hand pose, or the trace measures a ray nothing aims along.
        if (!generation || !Halo2Observer6Dof_ReadPublishedPose(publication) ||
            (seated && !Halo2ObserverControllerSnapshotUsable(publication, generation)) ||
            !BuildStableFirstPersonCarriers(
                publication, generation, rightCarrier, leftCarrier,
                Halo2SupportCarrierMustDetach(publication)) ||
            !Halo2SightGeometry(publication, rightCarrier, head, dir, eye))
            return;
        // A range measured on foot never bends a seat's aim, nor a seat's the walk after it (Sight_SetStance).
        Sight_SetStance(kTitle, stance);
        int32_t weapon = -1;
        const uint64_t weaponAtMs = g_sightWeaponAtMs.load(std::memory_order_acquire);
        if (!seated && weaponAtMs && now >= weaponAtMs && now - weaponAtMs <= 250)
            weapon = static_cast<int32_t>(
                g_sightWeaponObject.load(std::memory_order_relaxed));
        const int32_t ignored = seated ? static_cast<int32_t>(seatedRoot) : static_cast<int32_t>(unit);
#if HALOMCCVR_DEV_DIAGNOSTICS
        // The ray this trace measures, for the engine-ray census (the aim path does not publish one).
        if (g_config.sight_ray_census)
        {
            SightRay ray{};
            ray.generation = generation;
            ray.sampleMs = Sight_NowMs();
            ray.unit = static_cast<int32_t>(unit);
            for (int axis = 0; axis < 3; ++axis)
            {
                ray.head[axis] = head[axis];
                ray.dir[axis] = dir[axis];
                ray.eye[axis] = eye[axis];
            }
            Sight_PublishRay(kTitle, ray);
        }
#endif
        const float length = kSightTraceLengthWu;
        const float start[3]{head[0] + dir[0] * kSightTraceSkipWu,
            head[1] + dir[1] * kSightTraceSkipWu, head[2] + dir[2] * kSightTraceSkipWu};
        const float delta[3]{dir[0] * length, dir[1] * length, dir[2] * length};
        alignas(16) Halo2CollisionResult result{};
        bool hit = false, called = false;
        const uint32_t flags = Halo2SightTraceFlags();
        LARGE_INTEGER before{}, after{};
        g_halo2WorldCollision.callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            const auto original = g_halo2WorldCollision.original;
            if (original &&
                g_halo2WorldCollision.installed.load(std::memory_order_acquire) &&
                g_halo2WorldCollision.active.load(std::memory_order_acquire))
            {
                QueryPerformanceCounter(&before);
                hit = original(flags, start, delta, ignored, weapon, &result) != 0;
                QueryPerformanceCounter(&after);
                called = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            called = false;
            Sight_NoteTraceFault(kTitle);
        }
        g_halo2WorldCollision.callbacks.fetch_sub(1, std::memory_order_acq_rel);
        if (!called)
            return;
        hit = hit && std::isfinite(result.fraction) &&
            result.fraction >= 0.0f && result.fraction <= 1.0f;
        Sight_PublishTrace(kTitle, generation, hit,
            hit ? kSightTraceSkipWu + result.fraction * length : 0.0f,
            static_cast<uint64_t>(after.QuadPart - before.QuadPart));
#if HALOMCCVR_DEV_DIAGNOSTICS
        // SIGHT HITS (dev): what the ray stopped on, by class (collision_test_vector leaves the result's
        // object index at -1 unless an object was hit) -- after the cost was taken, so the trace's time is its own.
        {
            const bool objectHit = hit && result.objectIndex != -1;
            const int type = objectHit ? Halo2SightObjectType(result.objectIndex) : -1;
            Sight_NoteTraceHit(kTitle, sight_hit::ClassifyHit(hit, objectHit, type), type,
                hit ? kSightTraceSkipWu + result.fraction * length : 0.0f, seated);
        }
#endif
    }

    // The ultimate parent of `object` (Halo 2's own walk, 0x8D6A50: the parent handle at +0x14, up to the root),
    // through the salt-checked accessor. UINT32_MAX when a link does not resolve or the chain does not end.
    uint32_t Halo2UltimateParent(uint32_t object) noexcept
    {
        for (int depth = 0; depth < 8; ++depth)
        {
            const auto* data = static_cast<const uint8_t*>(Halo2ObjectFromIndex(object));
            if (!data)
                return UINT32_MAX;
            const uint32_t parent = *reinterpret_cast<const uint32_t*>(data + 0x14);
            if (parent == UINT32_MAX)
                return object;
            object = parent;
        }
        return UINT32_MAX;
    }

    // Seat convergence (halo2_direct_drive.inl, included below): the seat's converged aim and its occupancy read.
    seat_converge::Why Halo2BuildSeatConvergedAim(uint32_t unit, uint32_t stance, float direction[3],
                                                  float* point, float* head = nullptr) noexcept;
    int Halo2DriveOccupancy(uint32_t& parent, int16_t& seat) noexcept;
    // Aiming a seat's gun through the point (seat_barrel_logic.h; halo2_direct_drive.inl): what a fire shows.
    void Halo2SeatBarrelNoteEye(const float* eye) noexcept;
    void Halo2SeatBarrelLearn(uint32_t firer, uint32_t weapon, int16_t barrel, const float* muzzle,
                              const float* direction) noexcept;
    bool Halo2SeatBarrelOurFire(uint32_t firer, uint32_t unit) noexcept;
    bool Halo2SeatShotPoint(uint32_t unit, float point[3]);   // halo2_seat_fire.inl
#if HALOMCCVR_DEV_DIAGNOSTICS
    void Halo2SeatBarrelNoteRound(const float spawn[3], const float forward[3], const float* velocity) noexcept;
    bool Halo2SeatBarrelKnownGun(uint32_t weapon) noexcept;
    bool Halo2SeatIntentRay(float head[3], float dir[3]) noexcept;
#endif

    // The direction the owned on-foot unit aims along. Trace-and-reaim whenever it can resolve: the line
    // from the stock camera through the point the eye-anchored intent ray hits (`converged`). Without a
    // fresh trace, the older fixed-range steer: the stock camera's line through the hand ray at the
    // reticle distance, which is right at that one distance only. `allowConverged` false always takes the
    // steer: a SEAT's converged aim is built by Halo2BuildSeatConvergedAim instead (from the seat's own engine
    // eye and the seat's own trace: halo2_direct_drive.inl), and this is its fallback.
    bool BuildPublishedControllerAimDirection(float direction[3],
        bool* converged = nullptr, bool allowConverged = true) noexcept
    {
        if (converged)
            *converged = false;
        if (!direction)
            return false;
        Halo2ObserverPosePublication publication{};
        Halo2CameraBasis rightCarrier{}, leftCarrier{};
        const uint32_t generation =
            g_generation.load(std::memory_order_acquire);
        // Persistent support grip: this used to default to the support-capable
        // carrier regardless of owner trust, so an engaged relationship with
        // no matching current owner kept steering native aim and aim assist
        // through support geometry. The shared decision is evaluated against
        // this exact publication (its frozen support-derived flag and its
        // owner identity), so a failed read, a release discontinuity or an
        // owner mismatch detaches to the independent one-hand carrier; with
        // no relationship engaged an ordinary publication is preserved.
        if (!Halo2Observer6Dof_ReadPublishedPose(publication) ||
            !Halo2ObserverControllerSnapshotUsable(publication, generation))
        {
            return false;
        }
        const bool independentPrimary =
            Halo2SupportCarrierMustDetach(publication);
        if (!BuildStableFirstPersonCarriers(
                publication, generation, rightCarrier, leftCarrier,
                independentPrimary))
        {
            return false;
        }
        float head[3]{}, dir[3]{}, eye[3]{}, bent[3]{};
        if (allowConverged && Halo2SightGeometry(publication, rightCarrier, head, dir, eye) &&
            Sight_ConvergedDirection(GameTitle::Halo2, generation, head, dir, eye, bent))
        {
            std::memcpy(direction, bent, sizeof(bent));
            if (converged)
                *converged = true;
            return true;
        }
        const float range = std::clamp(
            g_config.crosshair_distance_m, 2.0f, 50.0f) *
            Game_GetWorldScale();
        float engineEye[3]{};
        Halo2EngineEyePosition(publication, engineEye);
        return Halo2BuildControllerShotDirection(
            engineEye, rightCarrier, range, direction);
    }

    // FLAT PARITY's proof feed (halo2_fire_eye.inl, included below): our on-foot unit's own tick while it stands still.
    void Halo2NoteFireEyeTick(uint32_t unit) noexcept;

    void Halo2DualShakeTick(uint32_t unit) noexcept;

    __declspec(noinline) uint64_t __fastcall Halo2NativeAimUpdateDetour(
        uint32_t objectIndex)
    {
        g_nativeAimActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_nativeAimCalls.fetch_add(1, std::memory_order_relaxed);
        // Every unit's aim updates once per tick: the measured tick a flat-parity muzzle shot carries the eye by.
        Sight_NoteTitleTick(GameTitle::Halo2);
        const auto original = reinterpret_cast<Halo2NativeAimUpdateFn>(
            g_nativeAimOriginal.load(std::memory_order_acquire));
        uint64_t result = 0;
        // C-H2-72 diagnostic: when VR owns Halo 2's local-player aim, do not
        // let stock unit_update_aiming run first. That stock transaction owns
        // more than the two direction vectors and can retain Halo 2's
        // controller aim-assist / target-acquisition state even though the mod
        // overwrites +0x168/+0x174 afterwards. Reports from the accepted 0.3.5
        // line describe aim assist remaining active and melee/lunge behaving
        // incorrectly. In the VR-owned case the controller publication is
        // already the authoritative sight line, so write the two H2EK-proven
        // native aim vectors directly. Outside VR-owned gameplay the original
        // updater remains completely stock.
        const bool directVrAim =
            original && Game_Halo2ControllerAimActive() &&
            Halo2Observer6Dof_DirectWeaponAimArmed();
        bool applied = false;
        bool ownedForContact = false;
        if (original && Halo2Observer6Dof_DirectWeaponAimArmed())
        {
            __try
            {
                const uint32_t ownedUnit=Halo2OwnedUnit();
                if (ownedUnit == UINT32_MAX || objectIndex != ownedUnit)
                {
                    g_nativeAimNonOwned.fetch_add(1, std::memory_order_relaxed);
                    __leave;
                }


                auto* const unit=static_cast<uint8_t*>(Halo2ObjectFromIndex(objectIndex));
                if (!unit || unit[0xAA]!=0) __leave;
                ownedForContact = true;
                // The session as the game holds it, for the log (src/common/session_read_logic.h). Halo 2 keeps
                // the three pointers in module globals: game globals, the players array, "players globals".
                // ADDR-HYGIENE: guarded (session_read::Read believes the count only beside a players array
                // whose own name, signature, maximum and datum size check) and log only: nothing acts on it.
                {
                    constexpr uintptr_t kGameGlobalsRva=0xE80A78,kPlayersArrayRva=0xE80A28,kPlayersGlobalsRva=0xE80A20;
                    const uintptr_t base=g_moduleBase.load(std::memory_order_acquire);
                    if (base)
                    {
                        const session_read::Session session=session_read::Read(
                            *reinterpret_cast<const uint8_t* const*>(base+kGameGlobalsRva),
                            *reinterpret_cast<const uint8_t* const*>(base+kPlayersArrayRva),
                            *reinterpret_cast<const uint8_t* const*>(base+kPlayersGlobalsRva),session_read::kHalo2);
                        Sight_NoteSession(GameTitle::Halo2,session.type,session.players,session.others);
                    }
                }
                if (g_vehicleSeatVerified.load(std::memory_order_acquire))
                {
                    const bool seated = *reinterpret_cast<const int16_t*>(
                        unit + kHalo2UnitParentSeatOffset) != -1;
                    {
                        int64_t tick = -1;
                        float factor = 0.0f;
                        Halo2ReadGameClock(tick, factor);
                        g_vehicleSeatSampleTick.store(tick, std::memory_order_relaxed);
                    }
                    g_vehicleSeatSample.store((GetTickCount64() << 1) | uint64_t(seated),
                        std::memory_order_release);
                    // Vehicle/turret input owns native aiming in the seated
                    // path. Keep the complete stock updater for those units.
                    if (seated)
                    {
                        // ...but the seat's intent ray is traced here, on the simulation thread, like the
                        // on-foot one (seat_converge_logic.h): the seat drive bends its write through the point.
                        // Only while the hand aims the seat; the trace itself changes nothing in the unit.
                        if (Sight_AimEnabled() && Game_Halo2SeatAimActive())
                        {
                            const uint32_t parent = *reinterpret_cast<const uint32_t*>(unit + 0x14);
                            const int16_t seatIndex = *reinterpret_cast<const int16_t*>(
                                unit + kHalo2UnitParentSeatOffset);
                            if (parent != UINT32_MAX && seatIndex >= 0)
                                Halo2SightTrace(objectIndex, Halo2UltimateParent(parent),
                                    seat_converge::StanceKey(parent, seatIndex));
                        }
                        __leave;
                    }
                }
                Halo2DualShakeTick(objectIndex);
                if (!directVrAim) __leave;
                Halo2SightTrace(objectIndex);
                // Co-op safe: the unit's two aim vectors are simulation state, and writing them here changes it on
                // this machine only. The stock updater then builds them from the control record the direct
                // drive writes (the converged aim, halo2_direct_drive.inl), as it does for every other player;
                // the trace above still ran for that write.
                if (!Sight_FireTimeChangesAllowed()) __leave;
                float direction[3]{};
                bool converged = false;
                if (!BuildPublishedControllerAimDirection(direction, &converged)) __leave;
                std::memcpy(
                    unit + kHalo2UnitDesiredAimingVectorOffset,
                    direction, sizeof(direction));
                std::memcpy(
                    unit + kHalo2UnitAimingVectorOffset,
                    direction, sizeof(direction));
                applied = true;
                result |= 1u;
                // The aim in effect is the converged one: the compositor draws the traced reticle.
                if (converged)
                {
                    g_nativeAimConverged.fetch_add(1, std::memory_order_relaxed);
                    Sight_MarkConverged(GameTitle::Halo2, Game_GetWorldScale());
                    Sight_MarkAimWritten(GameTitle::Halo2);   // written into the unit just above
                }
                else
                {
                    g_nativeAimFixedRange.fetch_add(1, std::memory_order_relaxed);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_nativeAimRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (applied)
            g_nativeAimApplied.fetch_add(1, std::memory_order_relaxed);
        else if (original)
        {
            // VR admission alone is not ownership of this object. The native
            // updater also serves enemies/allies: every non-owned unit and
            // every failed local override must receive its original update.
            __try
            {
                result = original(objectIndex);
                g_nativeAimStockPassed.fetch_add(1, std::memory_order_relaxed);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_nativeAimRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if(ownedForContact) Halo2ContactTick(objectIndex);
        // The aim was ours to write (our unit, on foot, the controller owns it): the fire eye's proof samples it at rest.
        if (applied)
            Halo2NoteFireEyeTick(objectIndex);
        g_nativeAimActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    __declspec(noinline) void __fastcall Halo2AimAssistViewDirectionDetour(
        uint32_t userIndex, float* direction)
    {
        g_aimAssistViewDirectionActiveCallbacks.fetch_add(
            1, std::memory_order_acq_rel);
        g_aimAssistViewDirectionCalls.fetch_add(1, std::memory_order_relaxed);
        const auto original =
            reinterpret_cast<Halo2AimAssistViewDirectionFn>(
                g_aimAssistViewDirectionOriginal.load(
                    std::memory_order_acquire));
        bool originalCompleted = false;
        if (original)
        {
            __try
            {
                original(userIndex, direction);
                originalCompleted = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_aimAssistViewDirectionRefused.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }

        bool applied = false;
        if (originalCompleted && userIndex == kOwnedUser &&
            g_aimAssistControllerRayScope.active)
        {
            __try
            {
                applied = Halo2OverrideAimAssistViewDirection(
                    true, g_aimAssistControllerRayScope.direction, direction);
                g_aimAssistControllerRayScope.applied = applied;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_aimAssistViewDirectionRefused.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        if (applied)
            g_aimAssistViewDirectionApplied.fetch_add(
                1, std::memory_order_relaxed);
        else if (originalCompleted)
            g_aimAssistViewDirectionStock.fetch_add(
                1, std::memory_order_relaxed);
        g_aimAssistViewDirectionActiveCallbacks.fetch_sub(
            1, std::memory_order_acq_rel);
    }

    __declspec(noinline) void __fastcall Halo2AimAssistCalculateDetour(
        uint32_t userIndex, float* control,
        Halo2AimAssistTargetingResult* targeting)
    {
        g_aimAssistActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_aimAssistCalls.fetch_add(1, std::memory_order_relaxed);
        const auto original = reinterpret_cast<Halo2AimAssistCalculateFn>(
            g_aimAssistOriginal.load(std::memory_order_acquire));
        // Co-op safe: the calculation runs as the game built it, from the view the control record gives it (the
        // hand's aim, under the direct drive). Feeding it the controller ray, or blanking its result, changes the
        // target the simulation works from on this machine only.
        const bool suppress = original && userIndex == kOwnedUser && control &&
            targeting && Game_Halo2ControllerAimActive() &&
            Halo2Observer6Dof_DirectWeaponAimArmed() && Sight_FireTimeChangesAllowed();

        bool originalCompleted = false;
        bool originalStarted = false;
        bool completed = false;
        if (suppress)
        {
            __try
            {
                float controllerDirection[3]{};
                const bool controllerTargeting =
                    kHalo2ControllerAimAssistTargetingEnabled &&
                    g_aimAssistViewDirectionOriginal.load(
                        std::memory_order_acquire) &&
                    BuildPublishedControllerAimDirection(
                        controllerDirection);
                if (controllerTargeting)
                {
                    bool controllerRayApplied = false;
                    g_aimAssistControllerRayScope.active = true;
                    g_aimAssistControllerRayScope.applied = false;
                    std::memcpy(
                        g_aimAssistControllerRayScope.direction,
                        controllerDirection, sizeof(controllerDirection));
                    __try
                    {
                        originalStarted = true;
                        original(userIndex, control, targeting);
                        originalCompleted = true;
                    }
                    __finally
                    {
                        controllerRayApplied =
                            g_aimAssistControllerRayScope.applied;
                        g_aimAssistControllerRayScope = {};
                    }
                    if (originalCompleted && controllerRayApplied)
                    {
                        completed = Halo2SuppressCameraAimAssist(control);
                        if (completed) RecordHalo2IndependentQuery();
                        if (completed)
                        {
                            if (targeting->identifiers[0] != UINT32_MAX)
                                g_aimAssistTargetSelected.fetch_add(
                                    1, std::memory_order_relaxed);
                            else
                                g_aimAssistNoTarget.fetch_add(
                                    1, std::memory_order_relaxed);
                        }
                    }
                    // If the scoped helper was not consumed, never retain a
                    // stock-camera target. Preserve the accepted C-H2-90
                    // camera result and fail this targeting feature to no
                    // target for this calculation only.
                    if (!completed)
                        completed = Halo2WriteNeutralAimAssistResults(
                            control, targeting);
                }
                else
                {
                    // Controller publication or the optional direction hook
                    // is unavailable. Retain the accepted camera-only fix and
                    // do not manufacture a target from the head/body camera.
                    completed = Halo2WriteNeutralAimAssistResults(
                        control, targeting);
                }

                if constexpr (kHalo2RetainAimAssistTargetForMelee)
                {
                    // Rejected C-H2-91 behavior remains deliberately dormant:
                    // unscoped stock-camera target retention fed the stock
                    // lunge and worsened the headset result.
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_aimAssistRefused.fetch_add(1, std::memory_order_relaxed);
            }
            if (completed)
                g_aimAssistSuppressed.fetch_add(1, std::memory_order_relaxed);
        }

        if (!completed && original && !originalStarted)
        {
            __try
            {
                original(userIndex, control, targeting);
                g_aimAssistStock.fetch_add(1, std::memory_order_relaxed);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_aimAssistRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_aimAssistActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    __declspec(noinline) void __fastcall Halo2WeaponAimHelperDetour(
        uint32_t objectIndex, float* origin, float* direction, uint64_t marker,
        float* offset, uint8_t projectOrigin, uint8_t useUnitAim,
        uint8_t collisionAdjust)
    {
        g_weaponAimActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_weaponAimCalls.fetch_add(1, std::memory_order_relaxed);
        const auto original = reinterpret_cast<Halo2WeaponAimHelperFn>(
            g_weaponAimOriginal.load(std::memory_order_acquire));
        bool originalCompleted = false;
        if (original)
        {
            __try
            {
                original(objectIndex, origin, direction, marker, offset,
                         projectOrigin, useUnitAim, collisionAdjust);
                originalCompleted = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_weaponAimExceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }

        bool applied = false;
        if (Halo2ShouldAttemptDirectShotOwnership(
                originalCompleted, origin && direction,
                Game_Halo2ControllerAimActive(),
                Halo2Observer6Dof_DirectWeaponAimArmed(), useUnitAim))
        {
            __try
            {
                // E-H2-37: the helper is shared by every firing unit. Resolve
                // output user 0 through H2's own players_globals and datum
                // layout, then require the helper's unit handle to match it.
                // This is a read-only hot-path guard: no scan, lock, or call
                // into another engine subsystem occurs here.
                const uint32_t ownedUnit=Halo2OwnedUnit();
                if (ownedUnit == UINT32_MAX)
                {
                    g_weaponAimNoOwnedUnit.fetch_add(
                        1, std::memory_order_relaxed);
                    __leave;
                }
                if (objectIndex != ownedUnit)
                {
                    g_weaponAimNonOwned.fetch_add(
                        1, std::memory_order_relaxed);
                    __leave;
                }

                // The bullet must meet the crosshair the player can actually
                // see. The compositor may stabilize that displayed pose, so
                // using a newer raw VR_GetAimPose here creates two different
                // rays whenever aim_stabilization is non-zero. Rebuild the
                // carrier from the most recently PRESENTED reticle pose and
                // converge the engine-owned muzzle origin on that exact ray.
                // This helper's completed direction is the final firing
                // boundary for both values of `useUnitAim`. Restricting
                // ownership to the true case left 19 C-H2-55 helper calls as
                // unclassified stock before the local-unit guard could even
                // run. The guard below now decides ownership for both values.
                Halo2ObserverPosePublication publication{};
                Halo2CameraBasis carrier{};
                float presentedOrientation[4]{};
                float presentedPosition[3]{};
                uint64_t presentedAtMs = 0;
                const uint64_t nowMs = GetTickCount64();
                if (Halo2Observer6Dof_ReadPublishedPose(publication) &&
                    publication.generation ==
                        g_generation.load(std::memory_order_acquire) &&
                    VR_GetPresentedReticleAimPose(
                        presentedOrientation, presentedPosition,
                        presentedAtMs) &&
                    presentedAtMs <= nowMs && nowMs - presentedAtMs <= 250 &&
                    Halo2BuildStableControllerCarrier(
                        Halo2ControllerReference(publication), publication.referenceOrientation,
                        publication.referencePosition, presentedOrientation,
                        presentedPosition, Game_GetWorldScale(), 0.0f,
                        carrier))
                {
                    const float range = std::clamp(
                        g_config.crosshair_distance_m, 2.0f, 50.0f) *
                        Game_GetWorldScale();
                    float candidate[3]{};
                    if (Halo2BuildControllerShotDirection(
                            origin, carrier, range, candidate))
                    {
                        float stockLengthSquared = 0.0f;
                        float candidateLengthSquared = 0.0f;
                        float dot = 0.0f;
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            stockLengthSquared += direction[axis] * direction[axis];
                            candidateLengthSquared +=
                                candidate[axis] * candidate[axis];
                            dot += direction[axis] * candidate[axis];
                        }
                        const float denominator = std::sqrt(
                            stockLengthSquared * candidateLengthSquared);
                        if (std::isfinite(denominator) && denominator > 1.0e-6f)
                        {
                            dot = std::clamp(dot / denominator, -1.0f, 1.0f);
                            const float degrees =
                                std::acos(dot) * 57.29577951308232f;
                            if (std::isfinite(degrees))
                            {
                                const uint32_t milliDegrees =
                                    static_cast<uint32_t>(std::min(
                                        degrees * 1000.0f, 180000.0f));
                                if (milliDegrees >= 250)
                                    g_weaponAimChanged.fetch_add(
                                        1, std::memory_order_relaxed);
                                uint32_t previous =
                                    g_weaponAimMaxDeflectionMilliDegrees.load(
                                        std::memory_order_relaxed);
                                while (previous < milliDegrees &&
                                       !g_weaponAimMaxDeflectionMilliDegrees.
                                           compare_exchange_weak(
                                               previous, milliDegrees,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed))
                                {
                                }
                            }
                        }
                        std::memcpy(direction, candidate, sizeof(candidate));
                        applied = true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_weaponAimExceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        (applied ? g_weaponAimApplied : g_weaponAimStock)
            .fetch_add(1, std::memory_order_relaxed);
        g_weaponAimActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    void DetourBody(uint32_t user) noexcept
    {
        const auto original =
            reinterpret_cast<Halo2ObserverFinalTransformFn>(OriginalAddress());
        if (!original)
            return;

        // The engine's own transform always runs first and unchanged. The mod
        // only post-processes its result, so a withheld injection is exactly
        // stock behavior rather than a missing camera.
        original(user);
        g_callbacks.fetch_add(1, std::memory_order_relaxed);

        if (user != kOwnedUser)
            return;
        if (!g_armed.load(std::memory_order_acquire) ||
            !g_levelLive.load(std::memory_order_acquire) ||
            g_teardownRequested.load(std::memory_order_acquire))
        {
            return;
        }
        const uintptr_t base = g_observerResult.load(std::memory_order_acquire);
        if (!base)
            return;
        ApplyHeadPose(base + Halo2ObserverRecordOffset(user));
    }

    __declspec(noinline) void __fastcall Halo2ObserverFinalTransformDetour(
        uint32_t user)
    {
        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            __try
            {
                DetourBody(user);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        __finally
        {
            g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

#include "halo2_particle_suppression.inl"

    bool IsReadableProtection(DWORD protect) noexcept
    {
        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        const DWORD mask = protect & 0xFF;
        return mask == PAGE_READONLY || mask == PAGE_READWRITE ||
            mask == PAGE_WRITECOPY || mask == PAGE_EXECUTE_READ ||
            mask == PAGE_EXECUTE_READWRITE || mask == PAGE_EXECUTE_WRITECOPY;
    }

    int HexNibble(char value) noexcept
    {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    }

    struct FixedPattern
    {
        static constexpr size_t kMaxLength = 96;
        uint8_t bytes[kMaxLength]{};
        uint8_t wild[kMaxLength]{};
        size_t length = 0;
    };

    bool CompilePattern(const char* text, FixedPattern& out) noexcept
    {
        out.length = 0;
        for (const char* cursor = text; *cursor;)
        {
            if (*cursor == ' ')
            {
                ++cursor;
                continue;
            }
            if (out.length >= FixedPattern::kMaxLength)
                return false;
            if (cursor[0] == '?')
            {
                out.bytes[out.length] = 0;
                out.wild[out.length] = 1;
                ++out.length;
                ++cursor;
                if (*cursor == '?')
                    ++cursor;
                continue;
            }
            const int high = HexNibble(cursor[0]);
            const int low = cursor[1] ? HexNibble(cursor[1]) : -1;
            if (high < 0 || low < 0)
                return false;
            out.bytes[out.length] =
                static_cast<uint8_t>((high << 4) | low);
            out.wild[out.length] = 0;
            ++out.length;
            cursor += 2;
        }
        return out.length != 0;
    }

    // Region-walking and SEH-guarded: halo2.dll's mapped image contains gaps,
    // and a raw sweep across them faults.
    bool CountPatternMatches(
        uintptr_t base, size_t size, const char* pattern,
        uintptr_t& firstMatch, uint32_t& matchCount) noexcept
    {
        firstMatch = 0;
        matchCount = 0;
        FixedPattern compiled{};
        if (!base || !size || base > UINTPTR_MAX - size ||
            !CompilePattern(pattern, compiled) || size < compiled.length)
        {
            return false;
        }
        const uintptr_t imageEnd = base + size;
        uintptr_t cursor = base;
        __try
        {
            while (cursor < imageEnd)
            {
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(
                        reinterpret_cast<const void*>(cursor), &info,
                        sizeof(info)) ||
                    !info.RegionSize)
                {
                    return false;
                }
                const uintptr_t regionBase =
                    reinterpret_cast<uintptr_t>(info.BaseAddress);
                if (regionBase > UINTPTR_MAX - info.RegionSize)
                    return false;
                uintptr_t regionEnd = regionBase + info.RegionSize;
                if (regionEnd > imageEnd)
                    regionEnd = imageEnd;
                if (regionEnd <= cursor)
                    return false;
                if (info.State == MEM_COMMIT &&
                    IsReadableProtection(info.Protect) &&
                    compiled.length <= regionEnd - cursor)
                {
                    const auto* data =
                        reinterpret_cast<const uint8_t*>(cursor);
                    const size_t last =
                        (regionEnd - cursor) - compiled.length;
                    for (size_t offset = 0; offset <= last; ++offset)
                    {
                        size_t index = 0;
                        for (; index < compiled.length; ++index)
                        {
                            if (!compiled.wild[index] &&
                                data[offset + index] != compiled.bytes[index])
                            {
                                break;
                            }
                        }
                        if (index != compiled.length)
                            continue;
                        if (!firstMatch)
                            firstMatch = cursor + offset;
                        if (++matchCount >= 2)
                            return true;
                    }
                }
                cursor = regionEnd;
            }
            // The count above is of the bytes in memory. Where this mod has an
            // enabled hook those are our jump, not the game's code: a proof
            // of a function head run after another hook patched it would
            // read "missing" on a healthy game (sigscan.h). Make it the
            // count over the game's own bytes; a match that needed them is
            // reported once as PROOF AFTER HOOK.
            sig::CorrectLiveCountForHooks(
                base, size, compiled.bytes, compiled.wild, compiled.length,
                firstMatch, matchCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            firstMatch = 0;
            matchCount = 0;
            return false;
        }
        return true;
    }

#include "halo2_vehicle_identity.inl"

    bool DecodeRipRelative(
        uintptr_t match, uint32_t dispOffset, uint32_t nextOffset,
        uintptr_t& target) noexcept
    {
        __try
        {
            const int32_t displacement =
                *reinterpret_cast<const int32_t*>(match + dispOffset);
            target = match + nextOffset + displacement;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool DrainCallbacks() noexcept
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            if (!g_activeCallbacks.load(std::memory_order_acquire))
                return true;
            Sleep(10);
        }
        return false;
    }

    #include "halo2_contact_melee_runtime.inl"
    // A publication as the camera sample a shot site built its point from (flat parity): its engine eye, when it was
    // published and that eye's velocity.
    SightCameraSample Halo2PublicationCameraSample(const Halo2ObserverPosePublication& publication) noexcept
    {
        SightCameraSample camera{};
        Halo2EngineEyePosition(publication, camera.eye);
        camera.stampUs = publication.publishedUs;
        camera.valid = publication.publishedUs != 0 && shot_lag::Finite3(camera.eye);
        camera.velValid = publication.engineEyeVelValid;
        std::memcpy(camera.vel, publication.engineEyeVel, sizeof(camera.vel));
        return camera;
    }
    // SHOTS FROM THE MUZZLE (sight_layer.h), for halo2_muzzle_shots.inl: the newest publication's intent ray
    // (rendered eye along the aiming hand's carrier) and engine eye, through Sight_AimMuzzleShotFromRay.
    // `distance` is how far along the new direction the converged point lies.
    // FLAT PARITY: the converged point P is a WORLD point fixed at this publication's camera; the round leaves at
    // the fire, one tick of travel (or more) later, so a strafing muzzle shot aimed through P would trail flat by
    // that travel (felt as shot lag with shots_from_muzzle on). It flies through P' = P + (E_fire - E_P) instead
    // (halo2_fire_eye.inl: E_fire the firing assist's own camera evaluator asked now, once proven, else E_P
    // carried a tick). The unit aim this builder writes is a DIRECTION from E_P, which the game applies at its own
    // fire eye: flat parity already, untouched.
    bool Halo2AimMuzzleShot(const float before[3], const float position[3], float direction[3], float up[3],
                            float& distance) noexcept
    {
        distance = 0.0f;
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        Halo2ObserverPosePublication publication{};
        Halo2CameraBasis primary{}, secondary{};
        float head[3]{}, dir[3]{}, eye[3]{};
        if (!generation || !Halo2Observer6Dof_ReadPublishedPose(publication) ||
            !Halo2ObserverControllerSnapshotUsable(publication, generation) ||
            !BuildStableFirstPersonCarriers(publication, generation, primary, secondary,
                Halo2SupportCarrierMustDetach(publication)) ||
            !Halo2SightGeometry(publication, primary, head, dir, eye))
        {
            Sight_NoteMuzzleShotUnconverged(GameTitle::Halo2);
            return false;
        }
        // The firing assist aims at the point `distance` along the shot: the point the shot flies through (P', or P
        // with the transport off), from the same trace sample the direction was built from.
        const SightCameraSample camera = Halo2PublicationCameraSample(publication);
        const bool aimed = Sight_AimMuzzleShotFromRay(GameTitle::Halo2, TitleAdapter_GetGeneration(GameTitle::Halo2),
                                                      head, dir, eye, before, position, direction, up, &distance,
                                                      &camera);
        if (aimed)   // dev SHOT LAG: the muzzle shot against the point it was aimed through
            Sight_NoteShotLagFromRay(GameTitle::Halo2, SightShotPath::Muzzle, position, direction, head, dir, eye,
                                     false);
        return aimed;
    }

    #include "halo2_dual_wield_runtime.inl"
    #include "halo2_impact_witness.inl"
    #include "halo2_weapon_haptics.inl"
    #include "halo2_haptic_lifecycle.inl"

    // The projectile's own collision flags (g_halo2SightProjectileFlags, above), read from the flight wrapper's
    // call. Cold path, once per install. nullptr = resolved (`flags` set); otherwise why not, and `flags` is
    // whatever immediate was read (0 when none), for the log. Kept free of C++ objects so __try is legal.
    const char* Halo2ResolveProjectileFlags(uintptr_t base, size_t size, uintptr_t collisionTest,
        uint32_t& flags, uint32_t& matches) noexcept
    {
        constexpr char kProjectileQueryPattern[] =
            "48 89 44 24 28 B9 ?? ?? ?? ?? 8B 84 24 D0 00 00 00 "
            "F3 0F 11 4C 24 34 89 44 24 20 F3 0F 11 44 24 38 E8";
        constexpr size_t kImmediateOffset = 6, kCallOffset = 33;
        flags = 0;
        matches = 0;
        uintptr_t match = 0;
        if (!collisionTest)
            return "collision_test_vector is not pinned on this build";
        if (!CountPatternMatches(base, size, kProjectileQueryPattern, match, matches) || !matches)
            return "the projectile flight query's signature is missing";
        if (matches != 1)
            return "the projectile flight query's signature is ambiguous";
        if (match < base || match - base > size || size - (match - base) < kCallOffset + 5)
            return "the projectile flight query's match runs off the image";
        __try
        {
            const auto* code = reinterpret_cast<const uint8_t*>(match);
            int32_t relative = 0;
            std::memcpy(&relative, code + kCallOffset + 1, sizeof(relative));
            std::memcpy(&flags, code + kImmediateOffset, sizeof(flags));
            if (match + kCallOffset + 5 + static_cast<intptr_t>(relative) != collisionTest)
                return "the matched call does not reach the hooked collision_test_vector";
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return "a fault while reading the projectile flight query";
        }
        if (!sight_hit::Halo2ProjectileFlagsAgree(flags, kHalo2CollisionFlags))
            return "its flags are not the world-collision flags plus the dead-biped bit alone";
        return nullptr;
    }

    bool InstallHalo2WorldCollision(
        uintptr_t base, size_t size, uint32_t generation) noexcept
    {
        if (g_halo2WorldCollision.target)
            return true;
        constexpr char kCollisionPattern[] =
            "48 8B C4 44 89 48 20 55 53 41 56 48 8D A8 A8 FB FF FF "
            "48 81 EC 40 05 00 00 48 8B 9D 88 04 00 00 45 32 F6";
        constexpr char kObjectVelocityPattern[] =
            "83 F9 FF 0F 84 ?? ?? ?? ?? 48 8B C4 53 48 81 EC A0 00 00 00 "
            "44 0F 29 50 A8 44 0F 28 D3 44 0F 29 58 98 44 0F 28 DA "
            "44 0F 29 60 88 44 0F 28 E1";
        uintptr_t collisionMatch = 0, velocityMatch = 0;
        uint32_t collisionCount = 0, velocityCount = 0;
        const bool collisionPinned = CountPatternMatches(
                base, size, kCollisionPattern, collisionMatch,
                collisionCount) && collisionCount == 1 &&
            collisionMatch == base + kHalo2CollisionVectorRva;
        const bool velocityPinned = CountPatternMatches(
                base, size, kObjectVelocityPattern, velocityMatch,
                velocityCount) && velocityCount == 1 &&
            velocityMatch == base + kHalo2ObjectSetLocalVelocityRva;
        if (!collisionPinned)
        {
            LOG("Halo 2 world collision StockFallback: H2EK-mapped central "
                "collision vector signature matched %u times%s; camera, "
                "first-person packets, aim and OpenXR remain active",
                collisionCount,
                collisionCount == 1 ? " at a non-pinned RVA" : "");
            return false;
        }
        Halo2CollisionVectorFn trampoline = nullptr;
        void* const target = reinterpret_cast<void*>(collisionMatch);
        const MH_STATUS created = MH_CreateHook(
            target, reinterpret_cast<void*>(&Halo2CollisionVectorDetour),
            reinterpret_cast<void**>(&trampoline));
        if (created != MH_OK || !trampoline)
        {
            if (created == MH_OK) (void)MH_RemoveHook(target);
            LOG("Halo 2 world collision StockFallback: optional collision "
                "hook create failed (%d); Halo 2 VR remains active",
                static_cast<int>(created));
            return false;
        }
        g_halo2WorldCollision.target = target;
        g_halo2WorldCollision.original = trampoline;
        g_halo2WorldCollision.objectSetLocalVelocity = velocityPinned
            ? reinterpret_cast<Halo2ObjectSetLocalVelocityFn>(velocityMatch)
            : nullptr;
        g_halo2WorldCollision.generation = generation;
        g_halo2WorldCollision.worker[0] = Halo2CollisionWorkerHand{};
        g_halo2WorldCollision.worker[1] = Halo2CollisionWorkerHand{};
        g_halo2WorldCollision.nextQueryAtMs.store(0, std::memory_order_release);
        g_halo2WorldCollision.active.store(true, std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            g_halo2WorldCollision.active.store(false, std::memory_order_release);
            (void)MH_RemoveHook(target);
            g_halo2WorldCollision.target = nullptr;
            g_halo2WorldCollision.original = nullptr;
            g_halo2WorldCollision.objectSetLocalVelocity = nullptr;
            g_halo2WorldCollision.generation = 0;
            LOG("Halo 2 world collision StockFallback: optional collision "
                "hook enable failed; Halo 2 VR remains active");
            return false;
        }
        g_halo2WorldCollision.installed.store(true, std::memory_order_release);
        LOG("Halo 2 world collision Installed for Classic and Anniversary: "
            "official-H2EK central vector test at +0x%X, flags 0x%08X; "
            "final visible packet extents drive hands/weapon clamping and "
            "contact haptics; dynamic object velocity %s at +0x%X",
            kHalo2CollisionVectorRva, kHalo2CollisionFlags,
            velocityPinned ? "armed" : "StockFallback",
            kHalo2ObjectSetLocalVelocityRva);
        // The sight trace's flags: the projectile's own when they resolve, else the flags above.
        {
            uint32_t projectileFlags = 0, projectileMatches = 0;
            const char* const why = Halo2ResolveProjectileFlags(
                base, size, collisionMatch, projectileFlags, projectileMatches);
            g_halo2SightProjectileFlags.store(why ? 0u : projectileFlags, std::memory_order_release);
            if (why)
                LOG("SIGHT Halo 2 projectile flags NOT resolved: %s (%u signature match(es), immediate "
                    "0x%08X, expected 0x%08X); the intent ray keeps the world-collision flags 0x%08X, which "
                    "also stop on the bipeds a round flies through",
                    why, projectileMatches, projectileFlags,
                    kHalo2CollisionFlags | sight_hit::kHalo2DeadBipedCullBit, kHalo2CollisionFlags);
            Sight_AnnounceTraceQuery(GameTitle::Halo2, sight_hit::Engine::Halo2, Halo2SightTraceFlags(),
                "collision_test_vector",
                why ? "the world-collision / melee query's flags, a proxy: the projectile's own did not resolve"
                    : "the engine's own projectile flight query, resolved and value-checked",
                "on foot it ignores the unit and its held weapon (the engine skips an ignored object with its "
                "children); in a seat the vehicle's ultimate parent, as the engine's own crosshair test does "
                "(everything attached to it is skipped with it)");
        }
        return true;
    }

    bool RemoveHalo2WorldCollision() noexcept
    {
        g_halo2WorldCollision.active.store(false, std::memory_order_release);
        g_halo2WorldCollision.installed.store(false, std::memory_order_release);
        g_halo2SightProjectileFlags.store(0, std::memory_order_release);   // a new install resolves again
        void* const target = g_halo2WorldCollision.target;
        if (!target)
        {
            g_halo2WorldCollision.original = nullptr;
            g_halo2WorldCollision.objectSetLocalVelocity = nullptr;
            g_halo2WorldCollision.generation = 0;
            return true;
        }
        const MH_STATUS disabled = MCCVR_DisableHookForRetirement(target);
        if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
            disabled != MH_ERROR_DISABLED)
            return false;
        const void* functions[]{reinterpret_cast<const void*>(&Halo2CollisionVectorDetour)};
        const void* originals[]{reinterpret_cast<const void*>(g_halo2WorldCollision.original)};
        if (!WaitForNativeDetourQuiescence(functions,originals,1,g_halo2WorldCollision.callbacks))
            return false;
        const MH_STATUS removed = MH_RemoveHook(target);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
            return false;
        g_halo2WorldCollision.target = nullptr;
        g_halo2WorldCollision.original = nullptr;
        g_halo2WorldCollision.objectSetLocalVelocity = nullptr;
        g_halo2WorldCollision.generation = 0;
        return true;
    }

    bool InstallParticleGate(
        uintptr_t base, size_t size, uint32_t generation) noexcept
    {
        if (g_particleTarget)
            return true;
        if (g_particleRejectedGeneration == generation)
            return false;

        uintptr_t match = 0;
        uint32_t matchCount = 0;
        if (!CountPatternMatches(
                base, size, kHalo2ParticleRendererPattern, match,
                matchCount) ||
            matchCount != 1 || match != base + kHalo2ParticleRendererRva)
        {
            LOG("Halo 2 Classic muzzle suppression StockFallback: particle "
                "renderer signature matched %u times%s; camera and every "
                "other feature remain active",
                matchCount,
                (matchCount == 1 &&
                 match != base + kHalo2ParticleRendererRva)
                    ? " but moved from its pinned RVA" : "");
            g_particleRejectedGeneration = generation;
            return false;
        }

        bool gateReadable = false;
        uint8_t gate = 0xFF;
        __try
        {
            gate = *reinterpret_cast<const volatile uint8_t*>(
                base + kHalo2ClassicRenderDisabledByteRva);
            gateReadable = gate <= 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gateReadable = false;
        }
        if (!gateReadable)
        {
            LOG("Halo 2 Classic muzzle suppression StockFallback: live "
                "renderer gate +0x%X was unreadable or invalid; camera and "
                "every other feature remain active",
                static_cast<unsigned>(kHalo2ClassicRenderDisabledByteRva));
            g_particleRejectedGeneration = generation;
            return false;
        }

        void* trampoline = nullptr;
        void* const target = reinterpret_cast<void*>(match);
        const MH_STATUS created = MH_CreateHook(
            target, reinterpret_cast<void*>(&Halo2ParticleRendererDetour),
            &trampoline);
        if (created != MH_OK || !trampoline)
        {
            LOG("Halo 2 Classic muzzle suppression StockFallback: hook "
                "create=%d; camera and every other feature remain active",
                static_cast<int>(created));
            if (created == MH_OK)
                (void)MH_RemoveHook(target);
            g_particleRejectedGeneration = generation;
            return false;
        }

        g_particleOriginal.store(
            reinterpret_cast<uintptr_t>(trampoline),
            std::memory_order_release);
        const MH_STATUS enabled = MH_EnableHook(target);
        if (enabled != MH_OK)
        {
            LOG("Halo 2 Classic muzzle suppression StockFallback: hook "
                "enable=%d; camera and every other feature remain active",
                static_cast<int>(enabled));
            (void)MH_RemoveHook(target);
            g_particleOriginal.store(0, std::memory_order_release);
            g_particleRejectedGeneration = generation;
            return false;
        }

        g_particleTarget = target;
        g_particleSuppressed.store(0, std::memory_order_relaxed);
        g_particleReadFaults.store(0, std::memory_order_relaxed);
        g_particleHitPending.store(false, std::memory_order_release);
        g_particleHitLogged = false;
        LOG("Halo 2 Classic muzzle suppression Installed (Stage 3AK): "
            "particle renderer +0x%X is skipped only for nonzero current-user "
            "first-person calls while live renderer gate +0x%X is 0; "
            "Hide muzzle flash setting controls this fallback; "
            "Anniversary and all stock/world callers remain stock",
            static_cast<unsigned>(kHalo2ParticleRendererRva),
            static_cast<unsigned>(kHalo2ClassicRenderDisabledByteRva));
        return true;
    }

    bool RemoveHalo2Drive() noexcept;   // halo2_direct_drive.inl; the live RemoveCore retires it
    bool RemoveHalo2HeadRelative() noexcept;   // halo2_head_relative.inl; the live RemoveCore retires it
#if HALOMCCVR_DEV_DIAGNOSTICS
    void RetireHalo2ListenerWitness() noexcept;   // halo2_sound_listener_witness.inl; reads only, nothing to drain
#endif
    #include "halo2_observer_cleanup.inl"

    // Preserved old counter-only retirement for evidence; no callers.
    bool RemoveParticleGateLegacy() noexcept
    {
        if (!g_particleTarget)
        {
            g_particleOriginal.store(0, std::memory_order_release);
            return true;
        }
        const MH_STATUS disabled = MH_DisableHook(g_particleTarget);
        if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
            disabled != MH_ERROR_DISABLED)
        {
            LOG("Halo 2 Classic muzzle suppression: disable failed (%d); "
                "ownership retained for cleanup", static_cast<int>(disabled));
            return false;
        }
        for (int attempt = 0; attempt < 200 &&
             g_particleActiveCallbacks.load(std::memory_order_acquire);
             ++attempt)
        {
            Sleep(10);
        }
        if (g_particleActiveCallbacks.load(std::memory_order_acquire))
        {
            LOG("Halo 2 Classic muzzle suppression: callbacks did not drain; "
                "ownership retained for cleanup");
            return false;
        }
        const MH_STATUS removed = MH_RemoveHook(g_particleTarget);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
        {
            LOG("Halo 2 Classic muzzle suppression: remove failed (%d); "
                "ownership retained for cleanup", static_cast<int>(removed));
            return false;
        }
        g_particleTarget = nullptr;
        g_particleOriginal.store(0, std::memory_order_release);
        LOG("Halo 2 Classic muzzle suppression removed: %llu first-person "
            "Classic particle calls suppressed; stock renderer restored",
            static_cast<unsigned long long>(
                g_particleSuppressed.load(std::memory_order_relaxed)));
        return true;
    }

    #include "halo2_direct_drive.inl"
    #include "halo2_director_perspective.inl"
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev: the sound listener witness samples from the tracker's accessor detour (one call, once a game tick).
    void Halo2ListenerWitnessTick(int32_t user) noexcept;
#endif
    // The motion tracker follows the head instead of the aim (retail-derived lead, proves itself at run time).
    #include "halo2_head_relative.inl"
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev only: is Halo 2's sound listener oriented by the head, by the aim or by something else? Reads only.
    #include "halo2_sound_listener_witness.inl"
#endif

    bool RemoveCoreLegacy(const char* reason) noexcept
    {
        g_coreState = CoreState::CleanupRequired;
        g_armed.store(false, std::memory_order_release);
        g_teardownRequested.store(true, std::memory_order_release);
        // Optional and failure-isolated: restore the stock engine boolean even
        // if a later hook cleanup needs to remain pending.
        if constexpr (kHalo2DebugGlobalAimAssistOverrideEnabled)
            Game_Halo2RestoreAimAssist();
        if (!RemoveParticleGate())
            return false;
        if (!RemoveHalo2ContactMelee())
        {
            LOG("Halo 2 contact melee cleanup pending; retaining dependent collision hooks");
            return false;
        }
        if (!RemoveHalo2WeaponHaptics())
        {LOG("Halo 2 weapon haptics CleanupRequired: native callbacks retained");return false;}
        if (!RemoveHalo2DualAim())
            return false;
        if (!RemoveHalo2WorldCollision())
        {
            LOG("Halo 2 world collision cleanup pending; camera teardown "
                "waits so no collision trampoline can be freed in flight");
            return false;
        }
        if (g_target)
        {
            const MH_STATUS disabled = MH_DisableHook(g_target);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 observer 6DOF: disable failed (%d); ownership "
                    "retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            if (!DrainCallbacks())
            {
                LOG("Halo 2 observer 6DOF: callbacks did not drain; "
                    "ownership retained for cleanup");
                return false;
            }
            const MH_STATUS removed = MH_RemoveHook(g_target);
            if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
            {
                LOG("Halo 2 observer 6DOF: remove failed (%d); ownership "
                    "retained for cleanup", static_cast<int>(removed));
                return false;
            }
            g_target = nullptr;
        }
        g_finalPaletteReady.store(false, std::memory_order_release);
        if (g_interpFrameTarget)
        {
            (void)MH_DisableHook(g_interpFrameTarget);
            while (g_interpFrameActiveCallbacks.load(std::memory_order_acquire))
                Sleep(1);
            (void)MH_RemoveHook(g_interpFrameTarget);
            g_interpFrameTarget = nullptr;
            g_interpFrameOriginal.store(0, std::memory_order_release);
        }
        if (g_packetBuilderTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_packetBuilderTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 final-packet hands: disable failed (%d); ownership "
                    "retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_packetBuilderActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
            {
                Sleep(10);
            }
            if (g_packetBuilderActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 final-packet callbacks did not drain; ownership "
                    "retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_packetBuilderTarget);
            g_packetBuilderTarget = nullptr;
            g_packetBuilderOriginal.store(0, std::memory_order_release);
            g_packetBuilderLastAppliedMs.store(0, std::memory_order_release);
        }
        if (g_visibleConsumerTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_visibleConsumerTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 visible consumer: disable failed (%d); ownership "
                    "retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_visibleConsumerActiveCallbacks.load(
                     std::memory_order_acquire); ++attempt)
                Sleep(10);
            if (g_visibleConsumerActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 visible-consumer callbacks did not drain; "
                    "ownership retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_visibleConsumerTarget);
            g_visibleConsumerTarget = nullptr;
            g_visibleConsumerOriginal.store(0, std::memory_order_release);
            g_visibleConsumerContext = Halo2VisibleConsumerContext{};
        }
        if (g_finalPaletteTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_finalPaletteTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 final-palette hands: disable failed (%d); "
                    "ownership retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_finalPaletteActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
            {
                Sleep(10);
            }
            if (g_finalPaletteActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 final-palette callbacks did not drain; ownership "
                    "retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_finalPaletteTarget);
            g_finalPaletteTarget = nullptr;
            g_finalPaletteOriginal.store(0, std::memory_order_release);
            g_finalPaletteContext = Halo2FinalPaletteContext{};
        }
        if (g_weaponAimTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_weaponAimTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 direct weapon aim: disable failed (%d); ownership "
                    "retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_weaponAimActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
            {
                Sleep(10);
            }
            if (g_weaponAimActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 direct weapon aim callbacks did not drain; "
                    "ownership retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_weaponAimTarget);
            g_weaponAimTarget = nullptr;
            g_weaponAimOriginal.store(0, std::memory_order_release);
        }
        if (g_aimAssistTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_aimAssistTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 aim-assist bypass: disable failed (%d); ownership "
                    "retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_aimAssistActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
                Sleep(10);
            if (g_aimAssistActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 aim-assist bypass callbacks did not drain; "
                    "ownership retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_aimAssistTarget);
            g_aimAssistTarget = nullptr;
            g_aimAssistOriginal.store(0, std::memory_order_release);
        }
        if (g_aimAssistViewDirectionTarget)
        {
            const MH_STATUS disabled =
                MH_DisableHook(g_aimAssistViewDirectionTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 controller melee view direction: disable failed "
                    "(%d); ownership retained for cleanup",
                    static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_aimAssistViewDirectionActiveCallbacks.load(
                     std::memory_order_acquire);
                 ++attempt)
            {
                Sleep(10);
            }
            if (g_aimAssistViewDirectionActiveCallbacks.load(
                    std::memory_order_acquire))
            {
                LOG("Halo 2 controller melee view-direction callbacks did "
                    "not drain; ownership retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_aimAssistViewDirectionTarget);
            g_aimAssistViewDirectionTarget = nullptr;
            g_aimAssistViewDirectionOriginal.store(
                0, std::memory_order_release);
            g_aimAssistControllerRayScope = {};
        }
        // The drive reads the unit through the accessor the unit-aim hook owns: retire it first.
        if (!RemoveHalo2Drive())
            return false;
        RetireHalo2DirectorPerspective();   // a read-only binding: nothing to drain
#if HALOMCCVR_DEV_DIAGNOSTICS
        RetireHalo2ListenerWitness();
#endif
        g_footViewEngagedMs.store(0, std::memory_order_release);
        g_seatViewEngagedMs.store(0, std::memory_order_release);
        if (g_nativeAimTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_nativeAimTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 native controller aim: disable failed (%d); "
                    "ownership retained for cleanup", static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_nativeAimActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
                Sleep(10);
            if (g_nativeAimActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 native controller-aim callbacks did not drain; "
                    "ownership retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_nativeAimTarget);
            g_nativeAimTarget = nullptr;
            g_nativeAimOriginal.store(0, std::memory_order_release);
            g_objectDatumAccessor.store(0, std::memory_order_release);
            g_vehicleSeatVerified.store(false, std::memory_order_release);
            g_vehicleFrameVerified.store(false,std::memory_order_release);
            g_vehicleIdentityVerified.store(false,std::memory_order_release);
            g_vehicleSeatSample.store(0, std::memory_order_release);
        }
        if (g_reanchorTarget)
        {
            const MH_STATUS disabled = MCCVR_DisableHookForRetirement(g_reanchorTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 observer 6DOF: re-anchor disable failed (%d); "
                    "ownership retained for cleanup",
                    static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_reanchorActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
            {
                Sleep(10);
            }
            if (g_reanchorActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 observer 6DOF: re-anchor callbacks did not "
                    "drain; ownership retained for cleanup");
                return false;
            }
            const void* functions[]{reinterpret_cast<const void*>(&Halo2InterpolatorReadDetour)};
            const void* originals[]{reinterpret_cast<const void*>(g_reanchorOriginal.load())};
            if (!WaitForNativeDetourQuiescence(functions, originals, 1, g_reanchorActiveCallbacks))
            {
                LOG("Halo 2 observer re-anchor cleanup: ingress still busy; retained");
                return false;
            }
            const auto removed = MH_RemoveHook(g_reanchorTarget);
            if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
            {
                LOG("Halo 2 observer re-anchor cleanup: removal failed (%d); retained", static_cast<int>(removed));
                return false;
            }
            g_reanchorTarget = nullptr;
            g_reanchorOriginal.store(0, std::memory_order_release);
            g_passCamerasSet.store(false, std::memory_order_release);
            for (auto& cache : g_slotCache)
                cache.valid = false;
        }
        if (g_weaponsTarget)
        {
            const MH_STATUS disabled = MH_DisableHook(g_weaponsTarget);
            if (disabled != MH_OK && disabled != MH_ERROR_NOT_CREATED &&
                disabled != MH_ERROR_DISABLED)
            {
                LOG("Halo 2 observer 6DOF: first-person weapons disable "
                    "failed (%d); ownership retained for cleanup",
                    static_cast<int>(disabled));
                return false;
            }
            for (int attempt = 0; attempt < 200 &&
                 g_weaponsActiveCallbacks.load(std::memory_order_acquire);
                 ++attempt)
            {
                Sleep(10);
            }
            if (g_weaponsActiveCallbacks.load(std::memory_order_acquire))
            {
                LOG("Halo 2 observer 6DOF: first-person weapons callbacks did "
                    "not drain; ownership retained for cleanup");
                return false;
            }
            (void)MH_RemoveHook(g_weaponsTarget);
            g_weaponsTarget = nullptr;
        }
        g_weaponsOriginal.store(0, std::memory_order_release);
        g_interpolatorResetAddress.store(0, std::memory_order_release);
        g_weaponTickIndex.store(0, std::memory_order_release);
        g_weaponTickPreviousIndex.store(0, std::memory_order_release);
        g_weaponTickGeneration.store(0, std::memory_order_release);
        g_originalAddress.store(0, std::memory_order_release);
        g_observerResult.store(0, std::memory_order_release);
        g_moduleBase.store(0, std::memory_order_release);
        g_moduleSize.store(0, std::memory_order_release);
        g_generation.store(0, std::memory_order_release);
        g_installed.store(false, std::memory_order_release);
        g_referenceValid.store(false, std::memory_order_release);
        g_recenterRequested.store(true, std::memory_order_release);
        g_coreState = CoreState::StockFallback;
        // Non-owning identity only. Holding a loader reference here prevents
        // MCC from unloading halo2.dll between levels and makes the eventual
        // release race the next load.
        g_moduleReference = nullptr;
        if (reason)
            LOG("Halo 2 observer 6DOF removed (%s); stock camera restored",
                reason);
        return true;
    }

    bool InstallCore(
        uintptr_t base, size_t size, uint32_t generation,
        uintptr_t observerResultArray) noexcept
    {
        if (g_rejectedGeneration == generation)
            return false;
        g_halo2TagDataBaseSlot.store(0, std::memory_order_release);
        // A binding from an earlier module instance never outlives an install attempt (it is rebound below once
        // the core is in; its reads are also tied to the installed module's base).
        RetireHalo2DirectorPerspective();
#if HALOMCCVR_DEV_DIAGNOSTICS
        RetireHalo2ListenerWitness();
#endif

        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &module) ||
            !module)
        {
            return false;
        }

        uintptr_t match = 0;
        uint32_t matchCount = 0;
        if (!CountPatternMatches(
                base, size, kFinalTransformPattern, match, matchCount) ||
            matchCount != 1 || match != base + kHalo2ObserverFinalTransformRva)
        {
            LOG("Halo 2 observer 6DOF WITHHELD: the observer final-transform "
                "signature matched %u times%s; the stock camera is untouched",
                matchCount,
                (matchCount == 1 && match != base +
                     kHalo2ObserverFinalTransformRva)
                    ? " but moved from its pinned RVA" : "");
            g_rejectedGeneration = generation;
            return false;
        }

        // Second, independent identity check on the same match.
        uintptr_t matrixSlot = 0;
        if (!DecodeRipRelative(
                match, kFinalTransformMatrixDispOffset,
                kFinalTransformMatrixNextOffset, matrixSlot) ||
            matrixSlot != base + kFinalTransformMatrixSlotRva)
        {
            LOG("Halo 2 observer 6DOF WITHHELD: the director-space matrix "
                "slot decoded to RVA 0x%llX instead of 0x%X; the stock camera "
                "is untouched",
                matrixSlot ? static_cast<unsigned long long>(matrixSlot - base)
                           : 0ull,
                kFinalTransformMatrixSlotRva);
            g_rejectedGeneration = generation;
            return false;
        }

        if (!observerResultArray ||
            observerResultArray != base + kHalo2ObserverResultArrayRva)
        {
            LOG("Halo 2 observer 6DOF WITHHELD: the observer result array was "
                "not signature-proven for this generation; the stock camera "
                "is untouched");
            g_rejectedGeneration = generation;
            return false;
        }

        void* const target = reinterpret_cast<void*>(match);
        void* trampoline = nullptr;
        const MH_STATUS created = MH_CreateHook(
            target,
            reinterpret_cast<void*>(&Halo2ObserverFinalTransformDetour),
            &trampoline);
        if (created != MH_OK || !trampoline)
        {
            LOG("Halo 2 observer 6DOF WITHHELD: hook create=%d",
                static_cast<int>(created));
            if (created == MH_OK)
                (void)MH_RemoveHook(target);
            g_rejectedGeneration = generation;
            return false;
        }

        g_moduleReference = module;
        g_target = target;
        g_originalAddress.store(
            reinterpret_cast<uintptr_t>(trampoline),
            std::memory_order_release);
        g_observerResult.store(
            observerResultArray, std::memory_order_release);
        g_moduleBase.store(base, std::memory_order_release);
        g_moduleSize.store(size, std::memory_order_release);
        g_generation.store(generation, std::memory_order_release);
        Halo2PreparePhysicalCrouchCamera(base,size);
        g_teardownRequested.store(false, std::memory_order_release);
        g_referenceValid.store(false, std::memory_order_release);
        g_recenterRequested.store(true, std::memory_order_release);
        g_installed.store(true, std::memory_order_release);
        g_coreState = CoreState::CleanupRequired;

        const MH_STATUS enabled = MH_EnableHook(target);
        if (enabled != MH_OK)
        {
            LOG("Halo 2 observer 6DOF WITHHELD: hook enable=%d; rolling back",
                static_cast<int>(enabled));
            if (!RemoveCore("install rollback"))
                g_coreState = CoreState::CleanupRequired;
            g_rejectedGeneration = generation;
            return false;
        }

        // E-H2-23: the weapon tick witness. first_person_weapons is
        // identified by being the SINGLE caller of the interpolator's
        // first-person slot reset, so the reset's entry bytes remain part of
        // the identity proof. C-H2-41 bypasses the call only while its optional
        // floaty-hand transaction is live; every stock/failed-feature path
        // retains the accepted C-H2-40 reset behavior.
        const uintptr_t resetAddress =
            base + kHalo2FrameInterpolatorResetFirstPersonSlotRva;
        uint8_t resetBytes[sizeof(kHalo2FrameInterpolatorResetFirstPersonSlotEntryBytes)]{};
        bool resetReadable = false;
        __try
        {
            std::memcpy(
                resetBytes, reinterpret_cast<const void*>(resetAddress),
                sizeof(resetBytes));
            resetReadable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { resetReadable = false; }
        const bool resetPinned = resetReadable &&
            std::memcmp(
                resetBytes, kHalo2FrameInterpolatorResetFirstPersonSlotEntryBytes,
                sizeof(resetBytes)) == 0;
        g_interpolatorResetAddress.store(
            resetPinned ? resetAddress : 0, std::memory_order_release);
        const uintptr_t weapons = base + kHalo2FirstPersonWeaponsRva;
        uint8_t weaponsBytes[sizeof(kHalo2FirstPersonWeaponsEntryBytes)]{};
        bool weaponsReadable = false;
        __try
        {
            std::memcpy(
                weaponsBytes, reinterpret_cast<const void*>(weapons),
                sizeof(weaponsBytes));
            weaponsReadable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { weaponsReadable = false; }
        if (!weaponsReadable ||
            std::memcmp(
                weaponsBytes, kHalo2FirstPersonWeaponsEntryBytes,
                sizeof(weaponsBytes)) != 0)
        {
            LOG("Halo 2 weapon tick witness WITHHELD: the entry bytes at "
                "+0x%X are not first_person_weapons in this module; the "
                "Anniversary weapon stays placed against a pose the eyes "
                "cannot name",
                static_cast<unsigned>(kHalo2FirstPersonWeaponsRva));
        }
        else
        {
            void* const weaponsTarget = reinterpret_cast<void*>(weapons);
            void* weaponsTrampoline = nullptr;
            const MH_STATUS weaponsCreated = MH_CreateHook(
                weaponsTarget,
                reinterpret_cast<void*>(&Halo2FirstPersonWeaponsDetour),
                &weaponsTrampoline);
            if (weaponsCreated != MH_OK || !weaponsTrampoline)
            {
                LOG("Halo 2 weapon tick witness WITHHELD: hook create=%d",
                    static_cast<int>(weaponsCreated));
                if (weaponsCreated == MH_OK)
                    (void)MH_RemoveHook(weaponsTarget);
            }
            else
            {
                g_weaponsTarget = weaponsTarget;
                g_weaponsOriginal.store(
                    reinterpret_cast<uintptr_t>(weaponsTrampoline),
                    std::memory_order_release);
                const MH_STATUS weaponsEnabled = MH_EnableHook(weaponsTarget);
                if (weaponsEnabled != MH_OK)
                {
                    LOG("Halo 2 weapon tick witness WITHHELD: hook enable=%d",
                        static_cast<int>(weaponsEnabled));
                    (void)MH_RemoveHook(weaponsTarget);
                    g_weaponsTarget = nullptr;
                    g_weaponsOriginal.store(0, std::memory_order_release);
                }
                else
                {
                    const uintptr_t readSide =
                        base + kHalo2FrameInterpolatorReadRva;
                    uint8_t readBytes[sizeof(kHalo2FrameInterpolatorReadEntryBytes)]{};
                    bool readReadable = false;
                    __try
                    {
                        std::memcpy(readBytes,
                                    reinterpret_cast<const void*>(readSide),
                                    sizeof(readBytes));
                        readReadable = true;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) { readReadable = false; }
                    if (!readReadable ||
                        std::memcmp(readBytes,
                                    kHalo2FrameInterpolatorReadEntryBytes,
                                    sizeof(readBytes)) != 0)
                    {
                        LOG("Halo 2 weapon re-anchor WITHHELD: the entry bytes "
                            "at +0x%X are not the interpolator read side; the "
                            "weapon stays at the tick camera and will lag a "
                            "head turn by up to one tick",
                            static_cast<unsigned>(kHalo2FrameInterpolatorReadRva));
                    }
                    else
                    {
                        void* const readTarget =
                            reinterpret_cast<void*>(readSide);
                        void* readTrampoline = nullptr;
                        const MH_STATUS readCreated = MH_CreateHook(
                            readTarget,
                            reinterpret_cast<void*>(&Halo2InterpolatorReadDetour),
                            &readTrampoline);
                        if (readCreated != MH_OK || !readTrampoline)
                        {
                            LOG("Halo 2 weapon re-anchor WITHHELD: hook "
                                "create=%d", static_cast<int>(readCreated));
                            if (readCreated == MH_OK)
                                (void)MH_RemoveHook(readTarget);
                        }
                        else
                        {
                            g_reanchorTarget = readTarget;
                            g_reanchorOriginal.store(
                                reinterpret_cast<uintptr_t>(readTrampoline),
                                std::memory_order_release);
                            if (MH_EnableHook(readTarget) != MH_OK)
                            {
                                LOG("Halo 2 weapon re-anchor WITHHELD: hook "
                                    "enable failed");
                                (void)MH_RemoveHook(readTarget);
                                g_reanchorTarget = nullptr;
                                g_reanchorOriginal.store(
                                    0, std::memory_order_release);
                            }
                            else
                            {
                                LOG("Halo 2 weapon re-anchor installed on the "
                                    "first-person interpolator read +0x%X "
                                    "(node stride 0x%X, axes +0x%X, position "
                                    "+0x%X): every frame the drawn weapon is "
                                    "moved rigidly from the witnessed tick's "
                                    "camera to the frame's, so it follows a "
                                    "head turn at the player's frame rate and "
                                    "jumps with the world at every tick, in "
                                    "both renderers",
                                    static_cast<unsigned>(
                                        kHalo2FrameInterpolatorReadRva),
                                    static_cast<unsigned>(
                                        kHalo2FirstPersonNodeStride),
                                    static_cast<unsigned>(
                                        kHalo2FirstPersonNodeAxesOffset),
                                    static_cast<unsigned>(
                                        kHalo2FirstPersonNodePositionOffset));
                                if (kHalo2RejectedInterpolatorControllerOwnershipEnabled)
                                {
                                    LOG("Halo 2 floating hands ARMED "
                                        "(C-H2-48): first-person slot 0 only "
                                        "- exact left/right wrist subtrees "
                                        "ride their controllers; upper-arm and "
                                        "forearm nodes collapse, and the gun remains "
                                        "inside the right-hand subtree "
                                        "carrier (gun_forward_m %.3f m, "
                                        "gun_scale %.2f, mount trim "
                                        "%.1f/%.1f/%.1f deg). floating_hands "
                                        "in halomccvr.cfg is %d and is NOT "
                                        "consulted, as in Reach. No visible "
                                        "arms/body, no "
                                        "other slot, no world geometry, no "
                                        "XInput, no camera field is touched",
                                        g_config.gun_forward_m,
                                        g_config.gun_scale,
                                        g_config.gun_yaw_deg,
                                        g_config.gun_pitch_deg,
                                        g_config.gun_roll_deg,
                                        g_config.floating_hands ? 1 : 0);
                                }
                            }
                        }
                    }
                    LOG("Halo 2 weapon tick witness installed on "
                        "first_person_weapons +0x%X (the game tick's weapon "
                        "placement, the only caller of the interpolator's "
                        "first-person slot reset +0x%X, %s): every tick "
                        "records which observer publication the weapon was "
                        "placed against%s; the observer update itself is "
                        "untouched",
                        static_cast<unsigned>(kHalo2FirstPersonWeaponsRva),
                        static_cast<unsigned>(
                            kHalo2FrameInterpolatorResetFirstPersonSlotRva),
                        resetPinned ? "identity proven"
                                    : "identity NOT proven",
                        "; the engine's inter-tick blending is left ON so the "
                        "weapon stays smooth at the player's frame rate, and "
                        "the weapon's own view cancels the head motion");
                }
            }
        }

        // E-H2-41 (C-H2-47/48): resolve the engine's own animation-graph readers so
        // the LEFT hand can be identified from Halo 2's own data instead of a
        // hardcoded node index. These are read-only leaf calls; nothing here is
        // hooked or patched. All three must match exactly once and sit at their
        // pinned RVAs, or the complete optional floating-hand presentation stays
        // stock for that frame.
        if (kHalo2VisibleConsumerControllerOwnershipEnabled)
        {
            // The 8 leading padding bytes disambiguate the standalone function
            // from the two copies the optimizer inlined elsewhere.
            constexpr char kGraphDefinitionGetPattern[] =
                "CC CC CC CC CC CC CC CC 48 8B 05 ?? ?? ?? ?? 0F B7 C9 48 03 "
                "C9 48 63 44 C8 08 48 03 05 ?? ?? ?? ?? C3";
            constexpr uint32_t kGraphDefinitionGetSkew = 8;
            constexpr char kGetSkeletonNodePattern[] =
                "48 63 41 10 33 C9 83 F8 FF 74 26 85 C0 79 18 8B C8 48 63 C2 "
                "0F BA F1 1F 48 C1 E0 05 48 03 0D ?? ?? ?? ?? 48 03 C1 C3 48 "
                "8B C8 48 03 0D ?? ?? ?? ?? 48 63 C2 48 C1 E0 05 48 03 C1 C3";
            constexpr char kFindNodeByFlagsPattern[] =
                "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 48 63 41 0C 45 "
                "33 DB 85 C0 7E 53 4C 63 41 10 45 33 C9 48 8B 35 ?? ?? ?? ?? "
                "45 33 D2 48 8B 3D ?? ?? ?? ?? 48 8B D8 33 C0 41 83 F8 FF 74 "
                "15 45 85 C0 79 0C 41 8B C0 0F BA F0 1F 48 03 C6 EB 04 4A 8D "
                "04 07 41 0F B6 4C 02 0A 23 CA 3B CA 74 24";
            uintptr_t graphGetMatch = 0, nodeGetMatch = 0, findMatch = 0;
            uint32_t graphGetCount = 0, nodeGetCount = 0, findCount = 0;
            const bool graphGetOk = CountPatternMatches(
                    base, size, kGraphDefinitionGetPattern, graphGetMatch,
                    graphGetCount) &&
                graphGetCount == 1 &&
                graphGetMatch + kGraphDefinitionGetSkew ==
                    base + kHalo2AnimationGraphDefinitionGetRva;
            const bool nodeGetOk = CountPatternMatches(
                    base, size, kGetSkeletonNodePattern, nodeGetMatch,
                    nodeGetCount) &&
                nodeGetCount == 1 &&
                nodeGetMatch == base + kHalo2AnimationGraphGetSkeletonNodeRva;
            const bool findOk = CountPatternMatches(
                    base, size, kFindNodeByFlagsPattern, findMatch,
                    findCount) &&
                findCount == 1 &&
                findMatch == base + kHalo2AnimationGraphFindNodeByFlagsRva;
            if (graphGetOk && nodeGetOk)
            {
                const uintptr_t graphGetAddress =
                    base + kHalo2AnimationGraphDefinitionGetRva;
                // Preserve the rejected decoder dormant: +20 included opcode
                // byte 05 in the displacement and +24 was not instruction end.
                constexpr bool kEnableRejectedTagBaseDecoder = false;
                const uintptr_t tagBaseSlot = kEnableRejectedTagBaseDecoder
                    ? graphGetAddress + 24 +
                        *reinterpret_cast<const int32_t*>(graphGetAddress + 20)
                    : Halo2CollisionTagBaseSlot(base, size,
                        kHalo2AnimationGraphDefinitionGetRva,
                        {reinterpret_cast<const uint8_t*>(graphGetAddress), 26});
                g_graphDefinitionGet.store(
                    graphGetAddress,
                    std::memory_order_release);
                g_graphGetSkeletonNode.store(
                    base + kHalo2AnimationGraphGetSkeletonNodeRva,
                    std::memory_order_release);
                if (tagBaseSlot >= base &&
                    tagBaseSlot + sizeof(void*) <= base + size)
                {
                    g_halo2TagDataBaseSlot.store(
                        tagBaseSlot, std::memory_order_release);
                }
                LOG("Halo 2 weapon bounds tag-data binding: %s, native slot RVA +0x%llX",
                    tagBaseSlot ? "Installed" : "StockFallback",
                    static_cast<unsigned long long>(tagBaseSlot ? tagBaseSlot-base : 0));
                if (findOk)
                {
                    g_graphFindNodeByFlags.store(
                        base + kHalo2AnimationGraphFindNodeByFlagsRva,
                        std::memory_order_release);
                }
                LOG("Halo 2 hand binding readers ARMED (C-H2-60): the left and right "
                    "wrist descendants are identified from the "
                    "engine's own animation-graph model flags (node +0x%X, "
                    "left-hand bit 0x%02X, right-hand bit 0x%02X, stride "
                    "0x%X) - no hardcoded node index. Graph reader +0x%X, node "
                    "reader +0x%X, engine flag lookup %s. The final packet "
                    "transaction maps those source flags through H2's authored "
                    "hands remap; any failure leaves the complete optional "
                    "presentation stock",
                    static_cast<unsigned>(
                        kHalo2AnimationNodeModelFlagsOffset),
                    static_cast<unsigned>(kHalo2ModelFlagLeftHand),
                    static_cast<unsigned>(kHalo2ModelFlagRightHand),
                    static_cast<unsigned>(kHalo2AnimationNodeStride),
                    static_cast<unsigned>(
                        kHalo2AnimationGraphDefinitionGetRva),
                    static_cast<unsigned>(
                        kHalo2AnimationGraphGetSkeletonNodeRva),
                    findOk ? "cross-checking" : "NOT located (skipped)");
                LOG("Halo 2 optional left-hand alignment: final native Chief palm markers; "
                    "Elite right palm derived from its own bilateral local-Y rig; "
                    "hand-only correction preserves both renderers' gun packets");
            }
            else
            {
                LOG("Halo 2 final-packet hand binding WITHHELD: animation-graph "
                    "reader signatures matched %u / %u / %u times; hands, gun "
                    "and firing stay stock",
                    graphGetCount, nodeGetCount, findCount);
            }
        }

        // E-H2-53: +0x8181F0 calls the registered +0x6BB40 consumer before it
        // returns. The outer detour only establishes exact same-thread identity
        // and controller context; the consumer detour owns the matrices before
        // the renderer copies them. There is no post-return ownership path.
        g_finalPaletteReady.store(false, std::memory_order_release);
        uintptr_t packetMatch = 0;
        uint32_t packetMatchCount = 0;
        uintptr_t consumerMatch = 0;
        uint32_t consumerMatchCount = 0;
        constexpr char kPacketBuilderPattern[] =
            "48 89 5C 24 18 4C 89 4C 24 20 89 54 24 10 89 4C 24 08 "
            "55 56 57 41 54 41 55 41 56 41 57";
        constexpr char kVisibleConsumerPattern[] =
            "48 8B C4 89 48 08 55 41 54 41 55 41 56 41 57 48 8D A8 "
            "48 FE FF FF 48 81 EC 90 02 00 00";
        if (!kHalo2VisibleConsumerControllerOwnershipEnabled)
        {
            LOG("Halo 2 visible-consumer hands NOT installed: build switch off");
        }
        else if (!CountPatternMatches(
                     base, size, kPacketBuilderPattern, packetMatch,
                     packetMatchCount) ||
                 packetMatchCount != 1 ||
                 packetMatch != base + kHalo2FirstPersonPacketBuilderRva ||
                 !CountPatternMatches(
                     base, size, kVisibleConsumerPattern, consumerMatch,
                     consumerMatchCount) ||
                 consumerMatchCount != 1 ||
                 consumerMatch !=
                     base + kHalo2FirstPersonVisibleConsumerRva)
        {
            LOG("Halo 2 visible-consumer hands WITHHELD: builder / consumer "
                "identities matched %u / %u times",
                packetMatchCount, consumerMatchCount);
        }
        else
        {
            void* packetTrampoline = nullptr;
            void* consumerTrampoline = nullptr;
            void* const packetTarget = reinterpret_cast<void*>(packetMatch);
            void* const consumerTarget =
                reinterpret_cast<void*>(consumerMatch);
            const MH_STATUS consumerCreated = MH_CreateHook(
                consumerTarget,
                reinterpret_cast<void*>(
                    &Halo2VisibleFirstPersonConsumerDetour),
                &consumerTrampoline);
            const MH_STATUS packetCreated = MH_CreateHook(
                packetTarget,
                reinterpret_cast<void*>(&Halo2FirstPersonPacketBuilderDetour),
                &packetTrampoline);
            if (consumerCreated == MH_OK && consumerTrampoline)
                g_visibleConsumerOriginal.store(
                    reinterpret_cast<uintptr_t>(consumerTrampoline),
                    std::memory_order_release);
            if (packetCreated == MH_OK && packetTrampoline)
                g_packetBuilderOriginal.store(
                    reinterpret_cast<uintptr_t>(packetTrampoline),
                    std::memory_order_release);
            const MH_STATUS consumerEnabled =
                consumerCreated == MH_OK && consumerTrampoline
                ? MH_EnableHook(consumerTarget)
                : MH_ERROR_NOT_CREATED;
            const MH_STATUS packetEnabled =
                packetCreated == MH_OK && packetTrampoline
                ? MH_EnableHook(packetTarget)
                : MH_ERROR_NOT_CREATED;
            if (consumerCreated == MH_OK && consumerTrampoline &&
                packetCreated == MH_OK && packetTrampoline &&
                consumerEnabled == MH_OK && packetEnabled == MH_OK)
            {
                g_visibleConsumerTarget = consumerTarget;
                g_packetBuilderTarget = packetTarget;
                g_finalPaletteReady.store(true, std::memory_order_release);
                // E-H2-71 (C-H2-83): the Classic-only interpolated frame.
                // Optional and fail-open: without it the hands/gun keep
                // C-H2-81 behaviour; nothing else is gated on it.
                {
                    uintptr_t frameMatch = 0;
                    uint32_t frameMatchCount = 0;
                    // 28 bytes: the 23-byte prologue is shared with the
                    // sibling getter at +0x7227A0 (that is what made
                    // C-H2-83 match twice and refuse to install).
                    constexpr char kInterpFramePattern[] =
                        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 "
                        "48 83 EC 20 48 83 3D D4 8B F2 00 00";
                    if (!CountPatternMatches(
                            base, size, kInterpFramePattern, frameMatch,
                            frameMatchCount) ||
                        frameMatchCount != 1 ||
                        frameMatch !=
                            base + kHalo2FrameInterpolatorFirstPersonFrameRva)
                    {
                        LOG("Halo 2 classic composition frame NOT owned: the "
                            "interpolated first-person frame getter matched "
                            "%u times (expected 1 at +0x%X); Classic keeps "
                            "the engine's interpolated frame",
                            frameMatchCount,
                            static_cast<unsigned>(
                                kHalo2FrameInterpolatorFirstPersonFrameRva));
                    }
                    else
                    {
                        void* frameTrampoline = nullptr;
                        void* const frameTarget =
                            reinterpret_cast<void*>(frameMatch);
                        const MH_STATUS frameCreated = MH_CreateHook(
                            frameTarget,
                            reinterpret_cast<void*>(
                                &Halo2InterpolatedFrameDetour),
                            &frameTrampoline);
                        if (frameCreated == MH_OK && frameTrampoline)
                        {
                            g_interpFrameOriginal.store(
                                reinterpret_cast<uintptr_t>(frameTrampoline),
                                std::memory_order_release);
                            if (MH_EnableHook(frameTarget) == MH_OK)
                            {
                                g_interpFrameTarget = frameTarget;
                                LOG("Halo 2 classic composition frame OWNED "
                                    "(C-H2-83, E-H2-71): +0x%X reports no "
                                    "interpolated first-person frame during "
                                    "the mod's Classic packet build, so the "
                                    "builder composes the weapon against "
                                    "user_data+0x%X - the exact frame "
                                    "Anniversary composes against. Hands and "
                                    "gun therefore carry the same orientation "
                                    "in both renderers",
                                    static_cast<unsigned>(
                                        kHalo2FrameInterpolatorFirstPersonFrameRva),
                                    static_cast<unsigned>(
                                        kHalo2FirstPersonCurrentFrameOffset));
                            }
                            else
                            {
                                (void)MH_RemoveHook(frameTarget);
                                g_interpFrameOriginal.store(
                                    0, std::memory_order_release);
                                LOG("Halo 2 classic composition frame NOT "
                                    "owned: enable failed");
                            }
                        }
                        else
                        {
                            LOG("Halo 2 classic composition frame NOT owned: "
                                "create failed %d", static_cast<int>(frameCreated));
                        }
                    }
                }
                LOG("Halo 2 renderer-selected hands ARMED (C-H2-65): builder "
                     "+0x%X establishes exact packet context; Anniversary "
                     "consumer +0x%X owns matrices before copy, while Classic "
                     "owns its returned persistent packets at each eye draw; "
                     "rejected generic C-H2-64 alignment is disabled and the "
                     "accepted C-H2-63 presentation is restored",
                    static_cast<unsigned>(kHalo2FirstPersonPacketBuilderRva),
                    static_cast<unsigned>(
                        kHalo2FirstPersonVisibleConsumerRva));
            }
            else
            {
                LOG("Halo 2 visible-consumer hands WITHHELD: callback create/"
                    "enable %d/%d, builder create/enable %d/%d",
                    static_cast<int>(consumerCreated),
                    static_cast<int>(consumerEnabled),
                    static_cast<int>(packetCreated),
                    static_cast<int>(packetEnabled));
                if (packetCreated == MH_OK)
                {
                    (void)MH_DisableHook(packetTarget);
                    (void)MH_RemoveHook(packetTarget);
                }
                if (consumerCreated == MH_OK)
                {
                    (void)MH_DisableHook(consumerTarget);
                    (void)MH_RemoveHook(consumerTarget);
                }
                g_packetBuilderOriginal.store(0, std::memory_order_release);
                g_visibleConsumerOriginal.store(0, std::memory_order_release);
            }
        }

        // E-H2-54: own Halo 2's native desired/current aiming vectors at their
        // updater, rather than rewriting one later firing-helper result. This
        // single engine aim reaches crosshair, weapon simulation and projectile
        // construction together, using the same publication as the mesh.
        uintptr_t aimMatch = 0;
        uint32_t aimMatchCount = 0;
        uintptr_t accessorMatch = 0;
        uint32_t accessorMatchCount = 0;
        constexpr char kNativeAimPattern[] =
            "40 55 53 56 57 41 54 48 8D 6C 24 D0 48 81 EC 30 01 00 00 "
            "48 8B 05 ?? ?? ?? ?? 33 FF 44 8B E1";
        constexpr char kObjectAccessorPattern[] =
            "8B 51 08 48 8B 0D ?? ?? ?? ?? E9 ?? ?? ?? ??";
        if (!kHalo2VisibleConsumerControllerOwnershipEnabled ||
            !g_finalPaletteReady.load(std::memory_order_acquire))
        {
            LOG("Halo 2 native controller aim NOT installed: visible consumer "
                "ownership is unavailable");
        }
        else if (!CountPatternMatches(
                base, size, kNativeAimPattern, aimMatch, aimMatchCount) ||
            aimMatchCount != 1 ||
            aimMatch != base + kHalo2NativeAimUpdateRva ||
            !CountPatternMatches(
                base, size, kObjectAccessorPattern, accessorMatch,
                accessorMatchCount) ||
            accessorMatchCount != 1 ||
            accessorMatch != base + kHalo2ObjectDatumAccessorRva)
        {
            LOG("Halo 2 native controller aim WITHHELD: updater / object "
                "accessor identities matched %u / %u times",
                aimMatchCount, accessorMatchCount);
        }
        else
        {
            void* trampoline = nullptr;
            void* const aimTarget = reinterpret_cast<void*>(aimMatch);
            const MH_STATUS created = MH_CreateHook(
                aimTarget, reinterpret_cast<void*>(&Halo2NativeAimUpdateDetour),
                &trampoline);
            if (created != MH_OK || !trampoline)
            {
                LOG("Halo 2 native controller aim WITHHELD: hook create=%d",
                    static_cast<int>(created));
                if (created == MH_OK)
                    (void)MH_RemoveHook(aimTarget);
            }
            else
            {
                g_nativeAimTarget = aimTarget;
                g_nativeAimOriginal.store(
                    reinterpret_cast<uintptr_t>(trampoline),
                    std::memory_order_release);
                g_objectDatumAccessor.store(
                    accessorMatch, std::memory_order_release);
                const MH_STATUS enabled = MH_EnableHook(aimTarget);
                if (enabled != MH_OK)
                {
                    LOG("Halo 2 native controller aim WITHHELD: hook enable=%d",
                        static_cast<int>(enabled));
                    (void)MH_RemoveHook(aimTarget);
                    g_nativeAimTarget = nullptr;
                    g_nativeAimOriginal.store(0, std::memory_order_release);
                    g_objectDatumAccessor.store(0, std::memory_order_release);
                }
                else
                {
                    LOG("Halo 2 native controller aim installed (C-H2-62): "
                        "unit aim updater +0x%X writes local user 0 desired/"
                        "current aim +0x%X/+0x%X from the same stable "
                        "controller sight line as the visible mesh; the old "
                        "firing-helper substitution is not installed",
                        static_cast<unsigned>(kHalo2NativeAimUpdateRva),
                        static_cast<unsigned>(
                            kHalo2UnitDesiredAimingVectorOffset),
                        static_cast<unsigned>(kHalo2UnitAimingVectorOffset));
                }
            }
        }

        uintptr_t seatMatch = 0;
        uint32_t seatMatches = 0;
        const bool seatVerified = g_nativeAimOriginal.load(std::memory_order_acquire) &&
            CountPatternMatches(base, size, kHalo2UnitInVehiclePattern, seatMatch, seatMatches) &&
            seatMatches == 1 && seatMatch == base + kHalo2UnitInVehicleRva;
        g_vehicleSeatSample.store(0, std::memory_order_release);
        g_vehicleSeatVerified.store(seatVerified, std::memory_order_release);
        // H2EK sound_manager.cpp 0042CCB0 names parent_object_index and
        // parent_seat_index. Retail 6E49B0 reads +14/+210 and resolves that
        // parent with the same verified object accessor used above.
        constexpr char parentPattern[]="44 8B 43 14 4C 8B F8 41 83 F8 FF 0F 84 81 01 00 00 "
            "66 83 BB 10 02 00 00 FF 0F 84 73 01 00 00 66 85 FF 0F 84 6A 01 00 00";
        uintptr_t parentMatch=0;uint32_t parentMatches=0;
        const bool frameVerified=seatVerified&&CountPatternMatches(base,size,parentPattern,parentMatch,parentMatches)&&
            parentMatches==1&&parentMatch==base+0x6e4ad2;
        g_vehicleFrameVerified.store(frameVerified,std::memory_order_release);
        const bool identityVerified=frameVerified&&VerifyHalo2VehicleIdentity(base,size);
        g_vehicleIdentityVerified.store(identityVerified,std::memory_order_release);
        LOG("Halo 2 persistent vehicle model identity: %s; unknown/ambiguous models retain per-game trims",
            identityVerified?"native chain verified":"unavailable");
        // Halo 2 direct drive (halo2_direct_drive.inl): the aim into player control's desired angles. It
        // reads the local unit through the accessor and seat check verified just above.
        if (g_nativeAimOriginal.load(std::memory_order_acquire) && seatVerified)
            (void)InstallHalo2Drive(base, size);
        else
            LOG("Halo 2 DRIVE: not installed: the unit aim and seat check it reads are not (aim=%d seat=%d)",
                g_nativeAimOriginal.load(std::memory_order_acquire) ? 1 : 0, seatVerified ? 1 : 0);
        // The game's own first-person answer (halo2_director_perspective.inl): reads only, and independent of
        // every hook above; failure keeps the first-person palette packet rung.
        (void)BindHalo2DirectorPerspective(base, size);
        // The motion tracker by the head's heading (halo2_head_relative.inl). Optional and feature-local: a
        // refusal leaves the game's own tracker and nothing else changes. After the drive: it holds its record
        // against the one the drive bound, and the drive's written yaw is its run-time reference.
        (void)InstallHalo2HeadRelative(base, size, generation);
#if HALOMCCVR_DEV_DIAGNOSTICS
        // Dev: the sound listener witness (halo2_sound_listener_witness.inl). Reads only; a refusal measures
        // nothing and changes nothing. After the tracker: its accessor detour is the witness's sampling point.
        (void)BindHalo2ListenerWitness(base, size, generation, observerResultArray);
#endif
        LOG("Halo 2 vehicle view reference: %s (parent/seat matches=%u); native aim feedback retained separately",
            frameVerified?"verified":"stock fallback",parentMatches);
        LOG("Halo 2 vehicle controller steering: %s (native seat predicate matches=%u); "
            "only fresh seated local-player samples admit the stick loop",
            seatVerified ? "available for test" : "stock fallback", seatMatches);

        // E-H2-77 through E-H2-80 / C-H2-92: Halo 3 parity means the
        // controller sight remains authoritative without the stock camera
        // being pulled toward targets. H2EK's central aim_assist calculation
        // is paired with its player-control view-vector helper as one optional
        // transaction. Only that helper call inside the owned calculation sees
        // the controller ray; all other callers stay stock.
        uintptr_t assistMatch = 0;
        uint32_t assistMatchCount = 0;
        uintptr_t viewDirectionMatch = 0;
        uint32_t viewDirectionMatchCount = 0;
        constexpr char kAimAssistCalculatePattern[] =
            "48 89 54 24 10 55 53 56 57 41 54 48 8D AC 24 80 E7 FF FF "
            "B8 80 19 00 00 E8 ?? ?? ?? ?? 48 2B E0 49 8B F8 48 8B DA "
            "8B F1 E8 ?? ?? ?? ?? 33 C9 45 33 E4 48 89 0B 89 4B 08 4C "
            "89 67 1C 48 C7 07 FF FF FF FF C7 47 08 FF FF FF FF 66 44 "
            "89 67 18";
        constexpr char kAimAssistViewDirectionPattern[] =
            "48 89 5C 24 08 57 48 83 EC 20 48 63 D9 48 8B FA 8B CB "
            "E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 8B C8 4C 69 C3 B8 "
            "00 00 00 48 83 C2 38 49 03 D0 4C 8B C7";
        if (!g_nativeAimOriginal.load(std::memory_order_acquire))
        {
            LOG("Halo 2 aim-assist suppression StockFallback: native "
                "controller aim ownership is unavailable");
        }
        else if (!CountPatternMatches(
                base, size, kAimAssistCalculatePattern, assistMatch,
                assistMatchCount) ||
            assistMatchCount != 1 ||
            assistMatch != base + kHalo2AimAssistCalculateRva)
        {
            LOG("Halo 2 aim-assist suppression StockFallback: central "
                "calculation identity matched %u times (expected one at "
                "+0x%X)", assistMatchCount,
                static_cast<unsigned>(kHalo2AimAssistCalculateRva));
        }
        else
        {
            void* const assistTarget = reinterpret_cast<void*>(assistMatch);
            void* assistTrampoline = nullptr;
            bool viewInstalled = false;

            const bool viewIdentity = CountPatternMatches(
                    base, size, kAimAssistViewDirectionPattern,
                    viewDirectionMatch, viewDirectionMatchCount) &&
                viewDirectionMatchCount == 1 &&
                viewDirectionMatch ==
                    base + kHalo2AimAssistViewDirectionRva;
            if (viewIdentity)
            {
                void* const viewDirectionTarget =
                    reinterpret_cast<void*>(viewDirectionMatch);
                void* viewDirectionTrampoline = nullptr;
                const MH_STATUS viewCreated = MH_CreateHook(
                    viewDirectionTarget,
                    reinterpret_cast<void*>(
                        &Halo2AimAssistViewDirectionDetour),
                    &viewDirectionTrampoline);
                if (viewCreated == MH_OK && viewDirectionTrampoline)
                {
                    g_aimAssistViewDirectionTarget = viewDirectionTarget;
                    g_aimAssistViewDirectionOriginal.store(
                        reinterpret_cast<uintptr_t>(
                            viewDirectionTrampoline),
                        std::memory_order_release);
                    const MH_STATUS viewEnabled =
                        MH_EnableHook(viewDirectionTarget);
                    viewInstalled = viewEnabled == MH_OK;
                    if (!viewInstalled)
                    {
                        LOG("Halo 2 controller melee targeting StockFallback: "
                            "view-direction hook enable=%d; accepted camera "
                            "suppression remains eligible",
                            static_cast<int>(viewEnabled));
                        (void)MH_RemoveHook(viewDirectionTarget);
                        g_aimAssistViewDirectionTarget = nullptr;
                        g_aimAssistViewDirectionOriginal.store(
                            0, std::memory_order_release);
                    }
                }
                else
                {
                    LOG("Halo 2 controller melee targeting StockFallback: "
                        "view-direction hook create=%d; accepted camera "
                        "suppression remains eligible",
                        static_cast<int>(viewCreated));
                    if (viewCreated == MH_OK)
                        (void)MH_RemoveHook(viewDirectionTarget);
                }
            }
            else
            {
                LOG("Halo 2 controller melee targeting StockFallback: "
                    "view-direction identity matched %u times (expected one "
                    "at +0x%X); accepted camera suppression remains eligible",
                    viewDirectionMatchCount,
                    static_cast<unsigned>(
                        kHalo2AimAssistViewDirectionRva));
            }

            const MH_STATUS assistCreated = MH_CreateHook(
                assistTarget,
                reinterpret_cast<void*>(&Halo2AimAssistCalculateDetour),
                &assistTrampoline);
            if (assistCreated != MH_OK || !assistTrampoline)
            {
                LOG("Halo 2 aim-assist suppression StockFallback: central "
                    "hook create=%d; camera/stereo/aim/hands/HUD/OpenXR "
                    "remain armed",
                    static_cast<int>(assistCreated));
                if (assistCreated == MH_OK)
                    (void)MH_RemoveHook(assistTarget);
            }
            else
            {
                g_aimAssistTarget = assistTarget;
                g_aimAssistOriginal.store(
                    reinterpret_cast<uintptr_t>(assistTrampoline),
                    std::memory_order_release);
                const MH_STATUS assistEnabled = MH_EnableHook(assistTarget);
                if (assistEnabled != MH_OK)
                {
                    LOG("Halo 2 aim-assist suppression StockFallback: central "
                        "hook enable=%d; camera/stereo/aim/hands/HUD/OpenXR "
                        "remain armed",
                        static_cast<int>(assistEnabled));
                    (void)MH_RemoveHook(assistTarget);
                    g_aimAssistTarget = nullptr;
                    g_aimAssistOriginal.store(0, std::memory_order_release);
                }
                else if (viewInstalled)
                {
                    LOG("Halo 2 controller melee targeting Installed "
                        "(C-H2-92): central aim_assist.cpp +0x%X receives "
                        "the controller ray through scoped player-control "
                        "view helper +0x%X, retains its native melee target, "
                        "and returns neutral camera-assist control for "
                        "VR-owned local user 0; every other call is stock",
                        static_cast<unsigned>(
                            kHalo2AimAssistCalculateRva),
                        static_cast<unsigned>(
                            kHalo2AimAssistViewDirectionRva));
                }
                else
                {
                    LOG("Halo 2 aim-assist suppression Installed (C-H2-90 "
                        "fallback): controller melee targeting is "
                        "StockFallback, so VR-owned local user 0 receives "
                        "neutral camera-assist and no-target results");
                }
            }

            // The direction detour is useful only to the central transaction.
            // If that transaction failed, remove it after first preventing new
            // entries and draining any unscoped stock-forwarding call.
            if (!g_aimAssistOriginal.load(std::memory_order_acquire) &&
                g_aimAssistViewDirectionTarget)
            {
                (void)MH_DisableHook(g_aimAssistViewDirectionTarget);
                for (int attempt = 0; attempt < 200 &&
                     g_aimAssistViewDirectionActiveCallbacks.load(
                         std::memory_order_acquire);
                     ++attempt)
                {
                    Sleep(10);
                }
                (void)MH_RemoveHook(g_aimAssistViewDirectionTarget);
                g_aimAssistViewDirectionTarget = nullptr;
                g_aimAssistViewDirectionOriginal.store(
                    0, std::memory_order_release);
            }
        }

        // Optional feature transaction: refusal here leaves the proven camera,
        // stereo, packets, input and aim paths installed. The detour runs the
        // H2EK-native probes only from a stock collision-call engine context.
        // A new generation: forget the last one's trace fault and range (the collision hook itself may
        // persist across generations, so this cannot live in its one-time install).
        Sight_ResetTitle(GameTitle::Halo2);
        (void)InstallHalo2WorldCollision(base, size, generation);
        (void)InstallHalo2ContactMelee(base, size, generation);
        (void)InstallHalo2DualAim(base, size);
        (void)InstallHalo2WeaponHaptics(base, size);

        // Optional feature transaction: refusal here leaves the proven camera,
        // stereo, input and hand paths installed and loudly retains stock
        // particles for this generation.
        (void)InstallParticleGate(base, size, generation);

        // C-H2-89 is deliberately a separate optional transaction. A missing
        // or invalid debug-global slot leaves stock aim assist active without
        // disarming any of the proven Halo 2 VR paths.
        if constexpr (kHalo2DebugGlobalAimAssistOverrideEnabled)
            (void)Game_Halo2TryDisableAimAssist(base, size);

        g_coreState = CoreState::Installed;
        LOG("Halo 2 observer 6DOF installed: observer final transform "
            "+0x%X, observer results +0x%X stride 0x%X, user %u. Three "
            "12-byte spans (position +0x%X, forward +0x%X, up +0x%X) are "
            "written after the engine's own transform; C-H2-71 may additionally "
            "expand only vertical FOV +0x%X to the headset visibility cover "
            "(never narrows it); aspect and every other observer field stay "
            "engine-owned",
            static_cast<unsigned>(kHalo2ObserverFinalTransformRva),
            static_cast<unsigned>(kHalo2ObserverResultArrayRva),
            static_cast<unsigned>(kHalo2ObserverStride), kOwnedUser,
            static_cast<unsigned>(kHalo2ObserverResultPositionOffset),
            static_cast<unsigned>(kHalo2ObserverResultForwardOffset),
            static_cast<unsigned>(kHalo2ObserverResultUpOffset),
            static_cast<unsigned>(kHalo2ObserverResultVerticalFovOffset));
        return true;
    }

    void ReportTelemetry() noexcept
    {
        const uint64_t now = GetTickCount64();
        if (now - g_lastReportMs < 5000)
            return;
        g_lastReportMs = now;
        ReportHalo2ContactMelee();
        ReportHalo2Drive();
        ReportHalo2WeaponHaptics();
        ReportHalo2FireEye();
#if HALOMCCVR_DEV_DIAGNOSTICS
        ReportHalo2AsSeen();
#endif
        static uint64_t s_reportedReseats = 0;
        const uint64_t reseats = g_roomscaleReseats.load(std::memory_order_relaxed);
        if (reseats != s_reportedReseats)
        {
            s_reportedReseats = reseats;
            const uint32_t movedBits =
                g_roomscaleReseatMovedBits.load(std::memory_order_relaxed);
            float moved = 0.0f;
            std::memcpy(&moved, &movedBits, sizeof(moved));
            LOG("Halo 2 roomscale: reference re-seated on the head at the follow's first "
                "admission (%llu so far); the head had moved %.3f m from it since it was "
                "captured, which would otherwise have stayed between the view and the legs",
                static_cast<unsigned long long>(reseats), moved);
        }
        LOG("Halo 2 dual aim: enabled=%d fault=%d primaryRays=%llu secondaryRays=%llu refused=%llu",
            g_halo2Dual.enabled.load()?1:0, g_halo2Dual.faulted.load()?1:0,
            g_halo2Dual.primaryRays.exchange(0),
            g_halo2Dual.secondaryRays.exchange(0), g_halo2Dual.refused.exchange(0));
        LOG("Halo 2 dual native targeting: queries=%llu restoreRefused=%llu option=%d",
            g_halo2Dual.nativeQueries.exchange(0),g_halo2Dual.targetRestoresRefused.exchange(0),
            Sight_IndependentDualAim()?1:0);
        {
            const uint64_t seatApplied=g_halo2SeatFireApplied.exchange(0),seatThrough=g_halo2SeatFireThrough.exchange(0),
                seatSkipped=g_halo2SeatFireSkipped.exchange(0),seatFromEye=g_halo2SeatFireFromEye.exchange(0);
            if(seatApplied||seatThrough||seatSkipped||seatFromEye)
                LOG("Halo 2 seat fire: the engine's crosshair line at a seated fire replaced by the player's own (from "
                    "the rendered eye through the traced point) %llu, re-aimed from the seat camera through the point "
                    "%llu, already through it or geometrically refused (SHOT REAIM names which) %llu, left as the game "
                    "built it (the seat's aim was not converged) %llu",seatFromEye,seatApplied,seatThrough,seatSkipped);
        }
        LOG("Halo 2 barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu option=%d",
            g_halo2Muzzle.enabled.load()?1:0,g_halo2Muzzle.faulted.load()?1:0,
            g_halo2Muzzle.applied.exchange(0),g_halo2Muzzle.refused.exchange(0),Sight_GunBarrelAim()?1:0);
        const uint64_t applied = g_appliedPoses.load(std::memory_order_relaxed);
        // Report even when nothing is being applied. A silent zero is exactly
        // the failure this telemetry exists to expose: it distinguishes "the
        // hook never fired" from "the hook fired but every head sample was
        // rejected", which look identical from the player's seat.
        static bool reportedOnce = false;
        const uint64_t witnessed = g_weaponsWitnessed.load(std::memory_order_relaxed);
        if (reportedOnce && applied == g_lastAppliedReported &&
            g_callbacks.load(std::memory_order_relaxed) == g_lastCallbacks &&
            witnessed == g_lastWitnessedReported)
        {
            return;
        }
        g_lastWitnessedReported = witnessed;
        reportedOnce = true;
        g_lastAppliedReported = applied;
        g_lastCallbacks = g_callbacks.load(std::memory_order_relaxed);
        // The measured lean: how far the camera the engine now holds sits
        // from the camera it computed, in metres. This is the number that
        // says whether the head moves the world, not the applied count.
        Halo2ObserverPosePublication published{};
        float leanMeters = -1.0f;
        if (Halo2Observer6Dof_ReadPublishedPose(published))
        {
            float sum = 0.0f;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float d = published.tracked.position[axis] -
                    published.stock.position[axis];
                sum += d * d;
            }
            leanMeters = std::sqrt(sum) * kHalo2MetersPerWorldUnit;
        }
        LOG("Halo 2 weapon animation: applied=%llu settling=%llu",
            static_cast<unsigned long long>(g_h2WeaponAnimationApplied.load()),
            static_cast<unsigned long long>(g_h2WeaponAnimationSettling.load()));
        LOG("Halo 2 observer 6DOF: %llu observer transforms seen, %llu head "
            "poses applied, %llu rejected samples, %llu unreadable, %llu "
            "exceptions; lean now %.3f m at world_scale %.3f wu/m (positional "
            "%s); weapon tick witness: %llu placements, %llu witnessed, %llu "
            "with a record the engine had moved, %llu with no publication, "
            "%llu interpolation resets (the weapon shares the world's tick); "
            "re-anchor applied %llu, identity %llu, skipped %llu (read detour "
            "entered %llu: unhandled %llu, other player %llu, no nodes %llu, "
            "not live %llu, no witnessed tick %llu, bad slot %llu; last call "
            "player %d id %d slot %d handled %u count %u); geometry: from "
            "cache %llu, classic stale-camera compensated %llu, node space "
            "world %llu / camera-relative %llu / unknown %llu, pass cameras "
            "named %llu, newest-publication fallback %llu, busy %llu; last "
            "witnessed publication #%llu; controller placement: %llu applied, "
            "%llu failed, %llu interpolation resets bypassed; left hand: "
            "%llu on its own controller, %llu stock fallback, %llu binding "
            "rebuilds, last binding reason %d; final packet: %llu builder calls, "
            "%llu owned, %llu stock, %llu gun nodes; packet gates: %llu eligible, "
            "%llu weapon-state miss, %llu publication miss, %llu controller "
            "snapshot miss, %llu carrier miss, %llu ownership miss; rig packets: "
            "Chief %llu contexts/%llu owned flags 0x%02X, Arbiter %llu contexts/"
            "%llu owned flags 0x%02X; wrist motion "
            "right %u/%u mm last/max, left %u/%u mm last/max; palette result: "
            "%llu changed, %llu right, %llu left, %llu collapsed (%llu Elite "
            "arm records co-located), %llu refused; "
            "visible consumer: %llu calls, %llu hands owned, %llu guns owned; "
            "barrel meter [classic] carrier vs compose %.2f deg (elev %+.2f), "
            "stock root+X vs compose %.2f deg (elev %+.2f); [anniv] carrier "
            "vs compose %.2f deg (elev %+.2f), stock root+X vs compose %.2f "
            "deg (elev %+.2f); "
            "classic trim %llu applied / %llu refused; "
            "classic composition frame forced to current %llu of %llu "
            "getter calls; classic packet: %llu calls, %llu owned; classic eyes: %llu calls, "
            "%llu compensated, %llu no-packet, %llu no-pass, %llu refused; "
            "native aim: %llu calls, %llu applied, %llu non-owned, %llu refused, %llu native updates passed",
            static_cast<unsigned long long>(
                g_callbacks.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(applied),
            static_cast<unsigned long long>(
                g_rejectedSamples.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_unreadableSamples.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_exceptions.load(std::memory_order_relaxed)),
            leanMeters, Game_GetWorldScale(),
            Game_IsPositionalTracking() ? "ON" : "OFF",
            static_cast<unsigned long long>(
                g_weaponsCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_weaponsWitnessed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_weaponsRecordForeign.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_weaponsNoPublication.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_weaponsSlotResets.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorIdentity.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorSkipped.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorEntered.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorUnhandled.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorOtherPlayer.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorNoNodes.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorNotLive.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorNoTick.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorBadSlot.load(std::memory_order_relaxed)),
            g_reanchorLastPlayer.load(std::memory_order_relaxed),
            g_reanchorLastId.load(std::memory_order_relaxed),
            g_reanchorLastSlot.load(std::memory_order_relaxed),
            g_reanchorLastHandled.load(std::memory_order_relaxed),
            g_reanchorLastCount.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(
                g_reanchorFromCache.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorCompensated.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorSpaceWorld.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorSpaceRelative.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorSpaceUnknown.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorPassCameras.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorFallbackFrame.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_reanchorBusy.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_weaponTickIndex.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_floatyApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_floatyFailed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_weaponsResetsBypassedForFloaty.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_leftHandApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_leftHandRigidFallback.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_armBindingRebuilds.load(std::memory_order_relaxed)),
            g_armBindingLastReason.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(
                g_packetBuilderCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderStock.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderGunNodes.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderEligible.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderWeaponStateMiss.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderPublicationMiss.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderControllerSnapshotMiss.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderCarrierMiss.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderOwnershipMiss.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderChiefContexts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderChiefApplied.load(std::memory_order_relaxed)),
            g_packetBuilderLastChiefFlags.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(
                g_packetBuilderEliteContexts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_packetBuilderEliteApplied.load(std::memory_order_relaxed)),
            g_packetBuilderLastEliteFlags.load(std::memory_order_relaxed),
            g_packetBuilderRightDeltaMillimeters.load(
                std::memory_order_relaxed),
            g_packetBuilderMaxRightDeltaMillimeters.load(
                std::memory_order_relaxed),
            g_packetBuilderLeftDeltaMillimeters.load(
                std::memory_order_relaxed),
            g_packetBuilderMaxLeftDeltaMillimeters.load(
                std::memory_order_relaxed),
            static_cast<unsigned long long>(
                g_finalPaletteCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_finalPaletteMovedRight.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_finalPaletteMovedLeft.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_finalPaletteCollapsed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_finalPaletteCoLocatedArms.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_finalPaletteRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_visibleConsumerCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_visibleConsumerHandsApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_visibleConsumerGunsApplied.load(std::memory_order_relaxed)),
            g_meterCarrierVsCompose[0].load(std::memory_order_relaxed) / 1000.0,
            g_meterCarrierElevation[0].load(std::memory_order_relaxed) / 1000.0,
            g_meterStockVsCompose[0].load(std::memory_order_relaxed) / 1000.0,
            g_meterStockElevation[0].load(std::memory_order_relaxed) / 1000.0,
            g_meterCarrierVsCompose[1].load(std::memory_order_relaxed) / 1000.0,
            g_meterCarrierElevation[1].load(std::memory_order_relaxed) / 1000.0,
            g_meterStockVsCompose[1].load(std::memory_order_relaxed) / 1000.0,
            g_meterStockElevation[1].load(std::memory_order_relaxed) / 1000.0,
            static_cast<unsigned long long>(
                g_classicTrimApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicTrimRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_interpFrameForcedCurrent.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_interpFrameCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicPacketCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicPacketApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicEyeCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicEyeCompensated.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicEyeNoPacket.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicEyeNoPass.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_classicEyeRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_nativeAimCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_nativeAimApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_nativeAimNonOwned.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_nativeAimRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_nativeAimStockPassed.load(std::memory_order_relaxed)));
        LOG("Halo 2 trace-and-reaim: %llu on-foot aims converged on the traced point, %llu on the "
            "fixed-range steer (no fresh trace)",
            static_cast<unsigned long long>(
                g_nativeAimConverged.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_nativeAimFixedRange.load(std::memory_order_relaxed)));
        LOG("Halo 2 C-H2-92 controller melee targeting: %llu central calls, "
            "%llu camera assists suppressed for VR-owned local user 0 "
            "(%llu controller-ray targets selected, %llu controller-ray no "
            "target), %llu stock, %llu refused; scoped view helper %llu "
            "calls / %llu controller overrides / %llu stock / %llu refused",
            static_cast<unsigned long long>(
                g_aimAssistCalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistSuppressed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistTargetSelected.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistNoTarget.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistStock.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistViewDirectionCalls.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistViewDirectionApplied.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistViewDirectionStock.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_aimAssistViewDirectionRefused.load(
                    std::memory_order_relaxed)));
        LOG("Halo 2 snap turn: mode=%s accepted=%llu; common observer reference, "
            "native body-yaw convergence; vehicle steering retains stick ownership",
            g_config.turn_smooth ? "smooth" : "snap",g_snapTurns.exchange(0));
        LOG("Halo 2 world collision: installed=%d active=%d requested=%d, "
            "%llu native queries, left/right contacts %llu/%llu, weapon "
            "contacts %llu, authored weapon bounds %llu published / %llu "
            "hand-node fallback, corrections applied %llu/%llu, dynamic "
            "pushes %llu, isolated failures %llu",
            g_halo2WorldCollision.installed.load(std::memory_order_relaxed)
                ? 1 : 0,
            g_halo2WorldCollision.active.load(std::memory_order_relaxed)
                ? 1 : 0,
            g_config.world_collision ? 1 : 0,
            static_cast<unsigned long long>(
                g_halo2WorldCollision.queries.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.contacts[0].load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.contacts[1].load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.weaponContacts.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.weaponBoundsPublished.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.weaponBoundsFallbacks.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.applied[0].load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.applied[1].load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.pushes.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo2WorldCollision.failures.load(
                    std::memory_order_relaxed)));
        uint32_t coverBits[3] = {
            g_visibilityCoverStockBits.load(std::memory_order_relaxed),
            g_visibilityCoverRequiredBits.load(std::memory_order_relaxed),
            g_visibilityCoverWrittenBits.load(std::memory_order_relaxed)};
        float coverRadians[3]{};
        std::memcpy(coverRadians, coverBits, sizeof(coverRadians));
        constexpr float kDegrees = 57.29577951308232f;
        LOG("Halo 2 vehicle reference: %llu applied, %llu refused; the seat was kept on %llu frames while the "
            "simulation was not ticking (a hitch, or a co-op session waiting for another player's input); "
            "view/hands/controller use one occupation reference, native aim remains feedback",
            static_cast<unsigned long long>(g_vehicleViewApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_vehicleViewRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_vehicleViewHeld.load(std::memory_order_relaxed)));
        LOG("Halo 2 C-H2-71 upstream visibility cover: %llu expanded, %llu "
            "already wide, %llu refused; last stock %.1f deg, headset %.1f "
            "deg, selected %.1f deg at observer_result+0x%X (pose/stereo stay "
            "owned if this optional feature refuses)",
            static_cast<unsigned long long>(
                g_visibilityCoverApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_visibilityCoverAlreadyWide.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_visibilityCoverRefused.load(std::memory_order_relaxed)),
            coverRadians[0] * kDegrees, coverRadians[1] * kDegrees,
            coverRadians[2] * kDegrees,
            static_cast<unsigned>(kHalo2ObserverResultVerticalFovOffset));
    }
}

extern "C" void Halo2PcApplyForUser(int user, uint64_t returnAddress)
{
    Halo2DriveApply(user, returnAddress);
}

bool Halo2Observer6Dof_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool activeAndRange, bool levelRunning, bool coldPassed,
    uintptr_t observerResultArray) noexcept
{
    uint32_t vrFailureGeneration =
        g_vrFailureGeneration.load(std::memory_order_acquire);
    if (vrFailureGeneration && generation && generation != vrFailureGeneration)
    {
        g_vrFailureGeneration.compare_exchange_strong(
            vrFailureGeneration, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        vrFailureGeneration =
            g_vrFailureGeneration.load(std::memory_order_acquire);
    }

    const bool identityValid = moduleBase && generation &&
        moduleSize == kHalo2RetailImageSize;
    const bool vrAvailable =
        !vrFailureGeneration || generation != vrFailureGeneration;
    if (PollManualVrRecovery(g_manualRecoveryGeneration, generation,
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && vrAvailable,
            identityValid && activeAndRange,
            [] { return RemoveCore("manual VR recovery"); },
            [] { g_rejectedGeneration = 0; }) == ManualVrRecoveryPoll::Waiting)
        return false;
    const bool desired = identityValid && activeAndRange && levelRunning &&
        coldPassed && vrAvailable && observerResultArray != 0;

    g_levelLive.store(levelRunning, std::memory_order_release);

    const uint32_t owned = g_generation.load(std::memory_order_acquire);
    const bool ownsDifferentModule = g_target &&
        (owned != generation ||
         g_moduleBase.load(std::memory_order_acquire) != moduleBase);

    if (!desired || ownsDifferentModule || g_coreState == CoreState::CleanupRequired)
    {
        if (g_installed.load(std::memory_order_acquire) ||
            g_coreState != CoreState::StockFallback)
        {
            if (!RemoveCore(
                ownsDifferentModule ? "module generation changed"
                                    : "level or title no longer eligible"))
                return false;
        }
        if (TitleAdapter_GetActiveTitle() != GameTitle::Halo2 || generation != g_rejectedGeneration)
            g_rejectedGeneration = 0;
        return false;
    }

    if (g_coreState != CoreState::Installed)
    {
        if (!InstallCore(
                moduleBase, moduleSize, generation, observerResultArray))
        {
            return false;
        }
    }

    g_armed.store(true, std::memory_order_release);
    // Map/script initialization can rewrite debug globals after core install.
    // This poll runs on the title worker, not an eye/render hook.
    if constexpr (kHalo2DebugGlobalAimAssistOverrideEnabled)
        Game_Halo2MaintainDisabledAimAssist();
    if (g_armedLoggedGeneration != generation)
    {
        g_armedLoggedGeneration = generation;
        LOG("Halo 2 observer 6DOF armed: the headset now owns the Halo 2 "
            "camera's position and orientation in BOTH graphics modes, "
            "because the observer is the one camera root the classic Blam "
            "renderer and the remastered Anniversary renderer both consume");
    }
    if (!g_particleHitLogged &&
        g_particleHitPending.exchange(false, std::memory_order_acq_rel))
    {
        g_particleHitLogged = true;
        LOG("Halo 2 Classic muzzle suppression HIT: first-person Classic "
            "particle renderer calls are being skipped; Anniversary remains "
            "stock");
    }
    // Persistent support grip (F09): the packet transaction only publishes
    // while a first-person weapon packet is admitted, so a weapon->unarmed
    // transition could leave a stale KnownPresent cached forever. This poll
    // runs on the title worker for as long as the level is live, independent
    // of any weapon packet, and H2's guarded direct reader is proven safe
    // outside the TLS-restricted titles (A016). Publishing here - including an
    // explicit KnownAbsent for an empty raw slot and Unknown when the reader
    // cannot prove itself - is the liveness guarantee for that transition.
    // With the feature off or the title unwired this is not called at all.
    if (VR_SupportGripWiredForTitle(GameTitle::Halo2) &&
        TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        Game_PublishSupportGripOwnerEvidence(GameTitle::Halo2);
    ReportHalo2HeadRelative();   // its own 2 s clock (the telemetry below runs every 5 s)
#if HALOMCCVR_DEV_DIAGNOSTICS
    ReportHalo2ListenerWitness();   // dev: its own 2 s clock
#endif
    ReportTelemetry();
    return true;
}

bool Halo2Observer6Dof_Installed() noexcept
{
    return g_installed.load(std::memory_order_acquire);
}

bool Halo2Observer6Dof_SecondaryShotsOwnLine() noexcept
{
    if(!g_halo2Dual.enabled.load(std::memory_order_acquire)||g_halo2Dual.faulted.load(std::memory_order_acquire)||
       g_halo2Dual.generation!=g_generation.load())return false;
    return Sight_IndependentDualAim()||(Sight_GunBarrelAim()&&
        g_halo2Muzzle.enabled.load(std::memory_order_acquire)&&!g_halo2Muzzle.faulted.load(std::memory_order_acquire));
}

std::atomic<bool>* Halo2Observer6Dof_TeardownRequested() noexcept
{
    return &g_teardownRequested;
}

bool Halo2Observer6Dof_Armed() noexcept
{
    return g_armed.load(std::memory_order_acquire);
}

bool Halo2Observer6Dof_FootViewEngaged() noexcept
{
    const uint64_t at = g_footViewEngagedMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return at && now >= at && now - at <= 100;
}

bool Halo2Observer6Dof_SeatStops(seat_limit::Stops& stops, uint32_t& seatKey, float& recordYaw, float& targetYaw,
                                  float& targetPitch) noexcept
{
    if (!g_halo2Drive.armed.load(std::memory_order_acquire))
        return false;
    return g_halo2Drive.stops.read(GetTickCount64(), 100, stops, seatKey, recordYaw, &targetYaw, &targetPitch) &&
        std::isfinite(targetYaw) && std::isfinite(targetPitch);
}

bool Halo2Observer6Dof_SeatDriveAxes(bool& yaw, bool& pitch) noexcept
{
    const uint64_t now = GetTickCount64();
    const auto fresh = [now](uint64_t at) { return at && now >= at && now - at <= 150; };
    const bool armed = g_halo2Drive.armed.load(std::memory_order_acquire);
    yaw = armed && fresh(g_halo2Drive.seatYawMs.load(std::memory_order_acquire));
    pitch = armed && fresh(g_halo2Drive.seatPitchMs.load(std::memory_order_acquire));
    return yaw || pitch;
}

void Halo2Observer6Dof_SnapTurnSettled(uint32_t generation, float targetYaw) noexcept
{
    if (!generation || !std::isfinite(targetYaw)) return;
    uint32_t bits=0;
    memcpy(&bits,&targetYaw,sizeof(bits));
    g_snapSettled.store((uint64_t(generation)<<32)|bits,std::memory_order_release);
}

bool Halo2Observer6Dof_DirectWeaponAimArmed() noexcept
{
    return g_nativeAimOriginal.load(std::memory_order_acquire) != 0 &&
        g_objectDatumAccessor.load(std::memory_order_acquire) != 0 &&
        g_armed.load(std::memory_order_acquire) &&
        g_levelLive.load(std::memory_order_acquire) &&
        !g_teardownRequested.load(std::memory_order_acquire);
}

bool Halo2Observer6Dof_FinalPaletteArmed() noexcept
{
    const uint64_t appliedAt =
        g_packetBuilderLastAppliedMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return g_finalPaletteReady.load(std::memory_order_acquire) &&
        g_packetBuilderOriginal.load(std::memory_order_acquire) != 0 &&
        g_visibleConsumerOriginal.load(std::memory_order_acquire) != 0 &&
        appliedAt && appliedAt <= now && now - appliedAt <= 250 &&
        g_armed.load(std::memory_order_acquire) &&
        g_levelLive.load(std::memory_order_acquire) &&
        !g_teardownRequested.load(std::memory_order_acquire);
}

// fp_view::Signal as a byte: the game's own perspective for user 0 (halo2_director_perspective.inl).
uint8_t Halo2Observer6Dof_FirstPersonCamera() noexcept
{
    return static_cast<uint8_t>(ReadHalo2DirectorPerspective());
}

void* Halo2Observer6Dof_ReloadWeapon(uint32_t weapon) noexcept
{
    if (!Halo2Observer6Dof_OnFootFresh()) return nullptr;
    __try
    {
        const auto owner=Halo2OwnedUnit();
        const auto* object=Halo2ContactObject(weapon);
        if (!Halo2ContactBiped(owner) || !object || object[0xAA]!=2 || !(object[0x130]&1) ||
            *reinterpret_cast<const uint32_t*>(object+0x158)!=owner) return nullptr;
        return const_cast<uint8_t*>(object);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
void Halo2Observer6Dof_DevGameClock(long long& tick, float& factor) noexcept
{
    int64_t now = -1;
    Halo2ReadGameClock(now, factor);
    tick = now;
}
#endif

bool Halo2Observer6Dof_OnFootFresh() noexcept
{
    const uint64_t sample = g_vehicleSeatSample.load(std::memory_order_acquire);
    const uint64_t at = sample >> 1, now = GetTickCount64();
    return Halo2Observer6Dof_Armed() && g_vehicleSeatVerified.load() &&
        !(sample & 1u) && at && now >= at && now - at <= 100;
}

// The last seat sample said seated, however old (a pause stops the samples; Game_TitleSeated).
bool Halo2Observer6Dof_LastKnownSeated() noexcept
{
    // No answer (the observer is down, no sample yet) fails closed: seated (Game_TitleSeated -> no lift).
    const uint64_t sample = g_vehicleSeatSample.load(std::memory_order_acquire);
    if (!Halo2Observer6Dof_Armed() || sample == 0)
        return true;
    return (sample & 1u) != 0;
}

// Whether LastKnownSeated() above is a real answer rather than its fail-closed default.
bool Halo2Observer6Dof_LastKnownSeatValid() noexcept
{
    return Halo2Observer6Dof_Armed() &&
        g_vehicleSeatSample.load(std::memory_order_acquire) != 0;
}

bool Halo2Observer6Dof_VehicleControlActive() noexcept
{
    if (!Halo2Observer6Dof_DirectWeaponAimArmed() ||
        !g_vehicleSeatVerified.load(std::memory_order_acquire)) return false;
    const uint64_t sample = g_vehicleSeatSample.load(std::memory_order_acquire);
    const uint64_t at = sample >> 1;
    const uint64_t now = GetTickCount64();
    if ((sample & 1u) && at && now >= at && now - at <= 100)
        return true;
    return Halo2SeatHeldThroughStall(sample, now);
}

bool Halo2Observer6Dof_WorldCollisionActive() noexcept
{
    return g_halo2WorldCollision.installed.load(std::memory_order_acquire) &&
        g_halo2WorldCollision.active.load(std::memory_order_acquire);
}

void Halo2Observer6Dof_DevNoteAsSeenFrame(int renderer, uint64_t serial, const Halo2CameraBasis& center,
    const float headQuat[4], const Halo2ObserverPosePublication* publication) noexcept
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    __try { Halo2AsSeenNoteFrameImpl(renderer, serial, center, headQuat, publication); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
#else
    (void)renderer; (void)serial; (void)center; (void)headQuat; (void)publication;
#endif
}

void Halo2Observer6Dof_DevNoteAsSeenReticle(uint64_t serial, const float center[3], const float dir[3]) noexcept
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    __try { Halo2AsSeenNoteReticleImpl(serial, center, dir); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
#else
    (void)serial; (void)center; (void)dir;
#endif
}

bool Halo2Observer6Dof_ReadPublishedPose(
    Halo2ObserverPosePublication& out) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before =
            g_publicationVersion.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        Halo2ObserverPosePublication candidate = g_publication;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_publicationVersion.load(std::memory_order_acquire) != before)
            continue;
        if (!candidate.generation || !candidate.serial ||
            !g_armed.load(std::memory_order_acquire))
        {
            return false;
        }
        out = candidate;
        return true;
    }
    g_publicationTornReads.fetch_add(1, std::memory_order_relaxed);
    return false;
}

int Halo2Observer6Dof_ReadPublishedPoses(
    Halo2ObserverPosePublication* out, int maximum) noexcept
{
    if (!out || maximum <= 0 || !g_armed.load(std::memory_order_acquire))
        return 0;
    const unsigned head = g_ringHead.load(std::memory_order_acquire);
    int count = 0;
    for (unsigned back = 1; back <= kPublicationRing && back <= head &&
         count < maximum; ++back)
    {
        const unsigned slot = (head - back) % kPublicationRing;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = g_ringVersion[slot].load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            Halo2ObserverPosePublication candidate = g_ring[slot];
            std::atomic_thread_fence(std::memory_order_acquire);
            if (g_ringVersion[slot].load(std::memory_order_acquire) != before)
                continue;
            if (candidate.generation && candidate.serial)
                out[count++] = candidate;
            break;
        }
    }
    return count;
}

bool Halo2Observer6Dof_WeaponTickPublication(
    uint32_t generation, uint64_t& index, uint64_t& previousIndex) noexcept
{
    index = g_weaponTickIndex.load(std::memory_order_acquire);
    previousIndex = g_weaponTickPreviousIndex.load(std::memory_order_acquire);
    return index != 0 && generation != 0 &&
        g_weaponTickGeneration.load(std::memory_order_relaxed) == generation;
}

void Halo2Observer6Dof_SetFirstPersonPassCameras(
    const Halo2FirstPersonPassCameras* cameras) noexcept
{
    if (!cameras)
    {
        g_passCamerasSet.store(false, std::memory_order_release);
        return;
    }
    g_passCamerasVersion.fetch_add(1, std::memory_order_acq_rel);
    g_passCameras = *cameras;
    g_passCamerasVersion.fetch_add(1, std::memory_order_acq_rel);
    g_passCamerasSet.store(true, std::memory_order_release);
}

// E-H2-67 (C-H2-79): the camera the classic weapon pass was MEASURED to
// hold at draw time, replacing whichever camera the core predicted. Only
// the viewing term changes; the frame/correct cameras the owning core
// named stand.
bool Halo2Observer6Dof_BuildScopeCamera(Halo2CameraBasis& camera) noexcept
{
    Halo2ObserverPosePublication p{};Halo2CameraBasis primary{},secondary{};
    const auto gen=g_generation.load(std::memory_order_acquire);
    // The scope camera is a live carrier consumer too: it uses the same
    // owner-qualified support decision as native aim/aim assist instead of
    // resampling the support-capable carrier independently.
    if(!Halo2Observer6Dof_ReadPublishedPose(p)||!Halo2ObserverControllerSnapshotUsable(p,gen)||
        !BuildStableFirstPersonCarriers(p,gen,primary,secondary,
            Halo2SupportCarrierMustDetach(p)))return false;
    float direction[3]{},basis[9]{};
    // SCOPE CONVERGENCE (F1 > Scope): the lens moves to the rendered eye and looks along the intent ray, which
    // leaves that same eye (Halo2SightGeometry's head is p.tracked), like every other title's. Otherwise the
    // earlier lens: the engine eye, steered through the hand ray at the crosshair distance.
    float engineEye[3]{};
    Halo2EngineEyePosition(p,engineEye);
    const float* origin=engineEye;
    float head[3]{},dir[3]{},eye[3]{};
    if(Halo2SightGeometry(p,primary,head,dir,eye)&&Sight_ScopeConvergedLive(GameTitle::Halo2))
    {
        memcpy(direction,dir,sizeof(direction));
        origin=p.tracked.position;
    }
    else if(!Halo2BuildControllerShotDirection(engineEye,primary,
        std::clamp(g_config.crosshair_distance_m,2.f,50.f)*Game_GetWorldScale(),direction))return false;
    memcpy(basis,primary.forward,12);memcpy(basis+6,primary.up,12);
    ScopeCameraPose pose{};
    if(!ComputeScopeCameraPose(basis,origin,direction,pose))return false;
    camera=p.stock;memcpy(camera.position,pose.position,12);memcpy(camera.forward,pose.forward,12);
    memcpy(camera.up,pose.up,12);return Halo2ValidateCameraBasis(camera);
}

bool Halo2Observer6Dof_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept
{
    if (!Halo2Observer6Dof_Armed()) return false;
    __try {
        Halo2VehicleViewKey key{};Halo2CameraBasis hull{};
        key.generation=g_generation.load(std::memory_order_acquire);
        if (!ReadVehicleViewFrame(key,hull)) return false;
        const auto* object=Halo2ObjectFromIndex(key.unit);
        if (!object||Halo2OwnedUnit()!=key.unit||!Halo2Observer6Dof_Armed()) return false;
        owner={key.generation,key.unit,key.parent,key.seat,reinterpret_cast<uintptr_t>(object),
            ReadHalo2VehicleIdentity(key.parent),key.root};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Halo2Observer6Dof_ReadPacketBuildCamera(
    bool anniversary, Halo2CameraBasis& out) noexcept
{
    const int slot = anniversary ? 1 : 0;
    if (!g_packetCameraValid[slot].load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before =
            g_packetCameraVersion[slot].load(std::memory_order_acquire);
        if (before & 1u) continue;
        const Halo2CameraBasis candidate = g_packetCamera[slot];
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_packetCameraVersion[slot].load(std::memory_order_acquire) ==
            before)
        {
            if (!Halo2ValidateCameraBasis(candidate))
                return false;
            out = candidate;
            return true;
        }
    }
    return false;
}

void Halo2Observer6Dof_SetMeasuredFirstPersonViewingCamera(
    const Halo2CameraBasis& viewing, bool admitCompensation) noexcept
{
    if (!g_passCamerasSet.load(std::memory_order_acquire) ||
        !Halo2ValidateCameraBasis(viewing))
        return;
    // The owning core decides admission (E-H2-69). A measurement can only
    // name the camera; it can never turn on a transform the core refused.
    g_passCamerasVersion.fetch_add(1, std::memory_order_acq_rel);
    g_passCameras.viewing = viewing;
    g_passCameras.compensate = admitCompensation && g_passCameras.frameValid;
    g_passCamerasVersion.fetch_add(1, std::memory_order_acq_rel);
}

bool Halo2Observer6Dof_BeginClassicFirstPersonEye() noexcept
{
    g_classicEyeCalls.fetch_add(1, std::memory_order_relaxed);
    auto& classic = g_classicPacketContext;
    const uint64_t now = GetTickCount64();
    if (!g_armed.load(std::memory_order_acquire) ||
        !g_levelLive.load(std::memory_order_acquire) ||
        g_teardownRequested.load(std::memory_order_acquire) ||
        !classic.valid || classic.eyeActive || !classic.hands || !classic.gun ||
        classic.handsCount == 0 ||
        classic.handsCount > kHalo2FirstPersonPaletteCapacity ||
        classic.gunCount == 0 ||
        classic.gunCount > kHalo2FirstPersonPaletteCapacity ||
        (classic.secondaryGunCount && (!classic.secondaryGun ||
            classic.secondaryGunCount > kHalo2FirstPersonPaletteCapacity)) ||
        !classic.appliedAtMs || classic.appliedAtMs > now ||
        now - classic.appliedAtMs > 250)
    {
        g_classicEyeNoPacket.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    Halo2FirstPersonPassCameras pass{};
    bool passValid = false;
    if (g_passCamerasSet.load(std::memory_order_acquire))
    {
        for (int attempt = 0; attempt < 4 && !passValid; ++attempt)
        {
            const uint32_t before =
                g_passCamerasVersion.load(std::memory_order_acquire);
            if (before & 1u) continue;
            const Halo2FirstPersonPassCameras candidate = g_passCameras;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (g_passCamerasVersion.load(std::memory_order_acquire) == before)
            {
                pass = candidate;
                passValid = candidate.frameValid && candidate.compensate &&
                    Halo2ValidateCameraBasis(candidate.correct) &&
                    Halo2ValidateCameraBasis(candidate.viewing);
            }
        }
    }
    if (!passValid)
    {
        g_classicEyeNoPass.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool compensated = false;
    bool backupsCaptured = false;
    __try
    {
        const size_t handsBytes =
            static_cast<size_t>(classic.handsCount) *
            kHalo2FirstPersonNodeStride;
        const size_t gunBytes =
            static_cast<size_t>(classic.gunCount) *
            kHalo2FirstPersonNodeStride;
        std::memcpy(classic.handsBackup, classic.hands, handsBytes);
        std::memcpy(classic.gunBackup, classic.gun, gunBytes);
        if (classic.secondaryGunCount)
            std::memcpy(classic.secondaryGunBackup, classic.secondaryGun,
                static_cast<size_t>(classic.secondaryGunCount) * kHalo2FirstPersonNodeStride);
        backupsCaptured = true;
        compensated = Halo2CompensateClassicFirstPersonEye(
                classic.hands, classic.handsCount, pass) &&
            Halo2CompensateClassicFirstPersonEye(
                classic.gun, classic.gunCount, pass) &&
            (!classic.secondaryGunCount || Halo2CompensateClassicFirstPersonEye(
                classic.secondaryGun, classic.secondaryGunCount, pass));
        if (!compensated)
        {
            std::memcpy(classic.hands, classic.handsBackup, handsBytes);
            std::memcpy(classic.gun, classic.gunBackup, gunBytes);
            if (classic.secondaryGunCount)
                std::memcpy(classic.secondaryGun, classic.secondaryGunBackup,
                    static_cast<size_t>(classic.secondaryGunCount) * kHalo2FirstPersonNodeStride);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        compensated = false;
        if (backupsCaptured)
        {
            __try
            {
                std::memcpy(
                    classic.hands, classic.handsBackup,
                    static_cast<size_t>(classic.handsCount) *
                        kHalo2FirstPersonNodeStride);
                std::memcpy(
                    classic.gun, classic.gunBackup,
                    static_cast<size_t>(classic.gunCount) *
                        kHalo2FirstPersonNodeStride);
                if (classic.secondaryGunCount)
                    std::memcpy(classic.secondaryGun, classic.secondaryGunBackup,
                        static_cast<size_t>(classic.secondaryGunCount) * kHalo2FirstPersonNodeStride);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        classic.valid = false;
    }
    if (!compensated)
    {
        g_classicEyeRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    classic.eyeActive = true;
    g_classicEyeCompensated.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void Halo2Observer6Dof_EndClassicFirstPersonEye() noexcept
{
    auto& classic = g_classicPacketContext;
    if (!classic.eyeActive) return;
    __try
    {
        std::memcpy(
            classic.hands, classic.handsBackup,
            static_cast<size_t>(classic.handsCount) *
                kHalo2FirstPersonNodeStride);
        std::memcpy(
            classic.gun, classic.gunBackup,
            static_cast<size_t>(classic.gunCount) *
                kHalo2FirstPersonNodeStride);
        if (classic.secondaryGunCount)
            std::memcpy(classic.secondaryGun, classic.secondaryGunBackup,
                static_cast<size_t>(classic.secondaryGunCount) * kHalo2FirstPersonNodeStride);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        classic.valid = false;
        g_classicEyeRefused.fetch_add(1, std::memory_order_relaxed);
    }
    classic.eyeActive = false;
}

void Halo2Observer6Dof_RequestRecenter() noexcept
{
    g_recenterRequested.store(true, std::memory_order_release);
}

void Halo2Observer6Dof_MarkManualRecenter() noexcept
{
    g_manualRecenterRequested.store(true, std::memory_order_release);
}

void Halo2Observer6Dof_RequestRecovery(uint32_t generation) noexcept
{ g_manualRecoveryGeneration = generation; }
bool Halo2Observer6Dof_RecoveryPending() noexcept
{ return g_manualRecoveryGeneration != 0; }

void Halo2Observer6Dof_ShutdownForVrFailure() noexcept
{
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    if (generation)
        g_vrFailureGeneration.store(generation, std::memory_order_release);
    g_armed.store(false, std::memory_order_release);
    g_teardownRequested.store(true, std::memory_order_release);
}

// ---- Weapon-order diagnostic tranche (read-only evidence) ----
// Independent datum/inventory view: owned local unit plus the FP user-data
// primary slot validated through the native object table. The FP packet slot
// is deliberately NOT consulted here; packet evidence arrives separately via
// FpWeaponCommit so the analyser observes raw agreement or disagreement.
bool Halo2DiagnosticReadPrimaryWeapon(uint32_t& unitOut, uint32_t& weaponOut,
    uint32_t& detailOut, bool* primaryAbsentOut) noexcept
{
    unitOut = UINT32_MAX;
    weaponOut = UINT32_MAX;
    detailOut = 0;
    if (primaryAbsentOut) *primaryAbsentOut = false;
    // Lifecycle-guard failures are reported distinctly (bit30 ->
    // GuardRejected) so they cannot be confused with a reader that ran and
    // returned false; bit31 is reserved for native faults.
    if (!g_armed.load(std::memory_order_acquire) ||
        !g_levelLive.load(std::memory_order_acquire) ||
        g_teardownRequested.load(std::memory_order_acquire))
    {
        detailOut = 0x40000000u;
        return false;
    }
    const uint32_t generationBefore =
        g_generation.load(std::memory_order_acquire);
    if (!generationBefore)
    {
        detailOut = 0x40000000u;
        return false;
    }
    if (TitleAdapter_GetActiveTitle() != GameTitle::Halo2 ||
        TitleAdapter_GetGeneration(GameTitle::Halo2) != generationBefore)
    {
        detailOut = 0x40000000u;
        return false;
    }
    const uintptr_t module = g_moduleBase.load(std::memory_order_acquire);
    if (!module)
    {
        detailOut = 0x40000000u;
        return false;
    }
    __try
    {
        const uint32_t unit = Halo2OwnedUnit();
        if (unit == UINT32_MAX || !(unit >> 16))
            return false;
        const uintptr_t userArray =
            *reinterpret_cast<const volatile uintptr_t*>(module +
                kHalo2FirstPersonUserDataPointerRva);
        if (!userArray)
            return false;
        const uintptr_t weaponData = userArray +
            static_cast<uintptr_t>(kOwnedUser) *
                kHalo2FirstPersonUserStride +
            kHalo2FirstPersonWeaponDataOffset;
        const uint32_t primary =
            *reinterpret_cast<const volatile uint32_t*>(weaponData +
                kHalo2FirstPersonWeaponObjectOffset);
        const uintptr_t secondaryData =
            weaponData + kHalo2FirstPersonWeaponSlotStride;
        const uint32_t secondaryObject =
            *reinterpret_cast<const volatile uint32_t*>(secondaryData +
                kHalo2FirstPersonWeaponObjectOffset);
        const bool secondaryPresent =
            (*reinterpret_cast<const volatile uint8_t*>(secondaryData) & 1u) &&
            secondaryObject != UINT32_MAX;
        // The raw native slot is the only explicit absence claim; a non-null
        // candidate that fails validation stays uncertainty.
        if (primaryAbsentOut) *primaryAbsentOut = primary == UINT32_MAX;
        // Datum ownership mirroring Halo2DualWeaponOwned, resolved through
        // this core's own module base (no dual-aim install dependency).
        bool owned = false;
        if (primary != UINT32_MAX && (primary >> 16))
        {
            const uint8_t* object = static_cast<const uint8_t*>(
                Halo2ObjectFromIndex(primary));
            owned = object && object[0xAA] == 2 && (object[0x130] & 1u) &&
                *reinterpret_cast<const uint32_t*>(object + 0x158) == unit;
        }
        if (g_generation.load(std::memory_order_acquire) != generationBefore)
        {
            detailOut = 0x40000000u;
            return false;
        }
        if (!owned)
            return false;
        unitOut = unit;
        weaponOut = primary;
        detailOut = (secondaryPresent ? 1u : 0u) | 2u;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        detailOut = 0x80000000u;
        return false;
    }
}

#else

bool Halo2Observer6Dof_Poll(
    uintptr_t, size_t, uint32_t, bool, bool, bool, uintptr_t) noexcept
{
    return false;
}

bool Halo2Observer6Dof_Installed() noexcept { return false; }
bool Halo2Observer6Dof_SecondaryShotsOwnLine() noexcept { return false; }
void Halo2Observer6Dof_RequestRecovery(uint32_t) noexcept {}
bool Halo2Observer6Dof_RecoveryPending() noexcept { return false; }
bool Halo2Observer6Dof_Armed() noexcept { return false; }
bool Halo2Observer6Dof_FootViewEngaged() noexcept { return false; }
bool Halo2Observer6Dof_SeatDriveAxes(bool& yaw, bool& pitch) noexcept { yaw = pitch = false; return false; }
bool Halo2Observer6Dof_SeatStops(seat_limit::Stops&, uint32_t&, float&, float&, float&) noexcept { return false; }
bool Halo2Observer6Dof_LastKnownSeated() noexcept { return false; }
// A definite "not seated", as this build always answered: Game_TitleSeated keeps its observer-off result.
bool Halo2Observer6Dof_LastKnownSeatValid() noexcept { return true; }
void Halo2Observer6Dof_SnapTurnSettled(uint32_t, float) noexcept {}
bool Halo2Observer6Dof_DirectWeaponAimArmed() noexcept { return false; }
bool Halo2Observer6Dof_FinalPaletteArmed() noexcept { return false; }
uint8_t Halo2Observer6Dof_FirstPersonCamera() noexcept { return 0; }   // fp_view::Signal::Unavailable
bool Halo2Observer6Dof_BuildScopeCamera(Halo2CameraBasis&) noexcept { return false; }
bool Halo2Observer6Dof_OnFootFresh() noexcept { return false; }
void* Halo2Observer6Dof_ReloadWeapon(uint32_t) noexcept { return nullptr; }
bool Halo2Observer6Dof_WorldCollisionActive() noexcept { return false; }
bool Halo2Observer6Dof_ReadPublishedPose(
    Halo2ObserverPosePublication&) noexcept { return false; }
void Halo2Observer6Dof_DevNoteAsSeenFrame(int, uint64_t, const Halo2CameraBasis&, const float*,
    const Halo2ObserverPosePublication*) noexcept {}
void Halo2Observer6Dof_DevNoteAsSeenReticle(uint64_t, const float*, const float*) noexcept {}
int Halo2Observer6Dof_ReadPublishedPoses(
    Halo2ObserverPosePublication*, int) noexcept { return 0; }
bool Halo2Observer6Dof_WeaponTickPublication(
    uint32_t, uint64_t& index, uint64_t& previousIndex) noexcept
{
    index = 0;
    previousIndex = 0;
    return false;
}
void Halo2Observer6Dof_SetFirstPersonPassCameras(
    const Halo2FirstPersonPassCameras*) noexcept {}
void Halo2Observer6Dof_SetMeasuredFirstPersonViewingCamera(
    const Halo2CameraBasis&, bool) noexcept {}
bool Halo2Observer6Dof_ReadPacketBuildCamera(
    bool, Halo2CameraBasis&) noexcept { return false; }
bool Halo2Observer6Dof_BeginClassicFirstPersonEye() noexcept { return false; }
void Halo2Observer6Dof_EndClassicFirstPersonEye() noexcept {}
void Halo2Observer6Dof_RequestRecenter() noexcept {}
void Halo2Observer6Dof_MarkManualRecenter() noexcept {}
void Halo2Observer6Dof_ShutdownForVrFailure() noexcept {}
bool Halo2DiagnosticReadPrimaryWeapon(uint32_t& unitOut, uint32_t& weaponOut,
    uint32_t& detailOut, bool* primaryAbsentOut) noexcept
{
    unitOut = UINT32_MAX;
    weaponOut = UINT32_MAX;
    detailOut = 0;
    if (primaryAbsentOut) *primaryAbsentOut = false;
    return false;
}

#endif
