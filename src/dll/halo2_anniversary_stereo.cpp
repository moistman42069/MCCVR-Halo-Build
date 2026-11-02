#include "halo2_anniversary_stereo.h"
#include "../common/manual_vr_recovery_logic.h"
#include "../common/minhook_lifecycle.h"

#include <windows.h>
#include <d3d11.h>

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "../common/config.h"
#include "../common/scope_logic.h"
#include "../common/halo2_render_logic.h"
#include "../common/log.h"
#include "game.h"
#include "title_adapter.h"
#include "halo2_observer_6dof.h"
#include "halo2_saber_camera.h"
#include "halo2_stereo_core.h"
#include "hook_quiescence.h"
#include "own_image_fault_filter.h"
#include "vr.h"

#ifndef HALOMCCVR_HALO2_ANNIVERSARY_STEREO
#define HALOMCCVR_HALO2_ANNIVERSARY_STEREO 0
#endif

#if HALOMCCVR_HALO2_ANNIVERSARY_STEREO

namespace
{
    // E-H2-34 (C-H2-39): draw the weapon with the tick's view (C-H2-33/36)
    // instead of the frame's. Off: the geometry is re-anchored to the frame.
    constexpr bool kHalo2AnniversaryWeaponTickView = false;
    // Recovered from all three engine call sites, which set only rcx and r8d:
    //   lea rcx,[r15+0x10] / xor edx,edx / mov r8d,0xC8 / call memset
    //   mov r8d,ebx        / mov rcx,r15 / call 0x2DF190
    // so rdx carries nothing and the view index is the third integer register.
    using SaberSceneFn = void(__fastcall*)(void*, void*, uint32_t);
    // 0x1C7740 calls this with ALL THREE pointer arguments null:
    //   xor r9d,r9d / xor r8d,r8d / xor edx,edx / lea rcx,[rsp+0x30] /
    //   call 0x1C6D80
    // The fourth (r9) is a float[4] clip plane that 0x1C6D80 stores and, when
    // non-null, dereferences to rewrite the projection. A three-argument
    // call left r9 as garbage.
    using RebuildViewMatricesFn = void(__fastcall*)(void*, void*, void*, void*);
    using CameraCommitFn = void(__fastcall*)(void*);
    using CameraRefreshRectFn = void(__fastcall*)(void*);
    using HostUiCallbackFn = void(__fastcall*)(void*);

    enum class CoreState : uint8_t { StockFallback = 0, CleanupRequired, Installed };

    struct HeadReference
    {
        float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        float position[3]{};
    };

    std::atomic<bool> g_installed{false};
    std::atomic<uint32_t> g_zoomFaultGeneration{0},g_zoomFaults{0};
    std::atomic<bool> g_armed{false};
    std::atomic<bool> g_levelLive{false};
    std::atomic<bool> g_remasteredLive{false};
    std::atomic<bool> g_teardown{false};
    std::atomic<bool> g_recenterRequested{true};
    std::atomic<bool> g_referenceValid{false};
    std::atomic<uint32_t> g_generation{0};
    std::atomic<uint32_t> g_vrFailureGeneration{0};
    std::atomic<uintptr_t> g_moduleBase{0};
    std::atomic<uintptr_t> g_observerResult{0};
    std::atomic<uintptr_t> g_originalScene{0};
    // E-H2-18: trampolines of the two extra detours (view-record rebuild,
    // host UI callback) and the record whose first-person projection the
    // rebuild detour unifies with the world projection (0 = none).
    std::atomic<uintptr_t> g_originalRebuild{0};
    std::atomic<uintptr_t> g_originalHostUi{0};
    std::atomic<uint64_t> g_nativeChudDraws{0};
    std::atomic<uint64_t> g_nativeChudFailures{0};
    std::atomic<uintptr_t> g_fpPatchRecord{0};
    std::atomic<uint64_t> g_fpPatches{0};
    std::atomic<uint64_t> g_fpReadbacks{0};
    std::atomic<uint64_t> g_fpOverwritten{0};
    std::atomic<uint32_t> g_fpReadbackStatus{0};   // 1 AGREES, 2 OVERWRITTEN, 3 UNREADABLE
    std::atomic<uint32_t> g_fpStockBits[2]{};       // engine's stock FP scales (last mismatch)
    std::atomic<uint32_t> g_fpWorldBits[2]{};       // world scales the FP was unified to
    uint32_t g_fpLoggedStatus = 0;
    std::atomic<uint64_t> g_hudCallbacks{0};
    std::atomic<uint64_t> g_hudReplays{0};
    std::atomic<uint64_t> g_hudStockPasses{0};
    std::atomic<uint64_t> g_hudFailures{0};
    const char* g_hudLastReason = "none";
    uint64_t g_hudLoggedReplays = UINT64_MAX;
    std::atomic<uint64_t> g_posePublishedOlderSerial{0};
    // E-H2-21: which published sample the frame's own camera matched.
    std::atomic<uint64_t> g_sampleMatchedLatest{0};
    std::atomic<uint64_t> g_sampleMatchedOlder{0};
    std::atomic<uint64_t> g_sampleUnmatched{0};
    std::atomic<uint64_t> g_sampleNoOlder{0};
    // E-H2-23 (C-H2-31): the pair was rendered from the publication the
    // game tick's weapon placement was witnessed against / that serial was
    // not in the ring any more (the frame's own sample was used instead).
    std::atomic<uint64_t> g_sampleWitnessed{0};
    std::atomic<uint64_t> g_sampleWitnessMissing{0};
    // E-H2-26 (C-H2-33): the first-person weapon's own view. The eyes render
    // from the FRAME's sample (full frame rate); the weapon was placed at the
    // game tick against an older one, so its view-projection is rebuilt from
    // THAT pose. Written on the Saber thread before each eye's rebuild.
    float g_weaponViewNoTranslation[kHalo2SaberMatrixFloats]{};
    std::atomic<bool> g_weaponViewValid{false};
    std::atomic<uint64_t> g_weaponViewApplied{0};
    std::atomic<uint64_t> g_weaponViewBlended{0};
    std::atomic<uint64_t> g_weaponViewUnblended{0};
    std::atomic<uint64_t> g_weaponViewRejected{0};
    std::atomic<uint32_t> g_weaponViewCheckGeneration{0};
    std::atomic<bool> g_weaponViewCheckPassed{false};
    std::atomic<uintptr_t> g_rebuildMatrices{0};
    std::atomic<uintptr_t> g_cameraCommit{0};
    std::atomic<uintptr_t> g_cameraRefreshRect{0};
    std::atomic<uint32_t> g_coverLoggedGeneration{0};
    std::atomic<uint32_t> g_activeCallbacks{0};
    std::atomic<uint64_t> g_lastCompletedSerial{0};
    // MONO RENDERING: the eye the frame at g_lastCompletedSerial was drawn as when it was one pass; -1 a pair.
    // Stored before that serial, read after it.
    std::atomic<int> g_lastCompletedMonoEye{-1};
    std::atomic<uint64_t> g_monoFrames{0};
    std::atomic<uint64_t> g_monoDrops{0};
    // Of the HUD replays, those over a mono frame's one picture (drawn once, not per eye).
    std::atomic<uint64_t> g_hudMonoReplays{0};

    std::atomic<uint64_t> g_callbacks{0};
    std::atomic<uint64_t> g_pairs{0};
    std::atomic<uint64_t> g_drops{0};
    std::atomic<uint64_t> g_stockPasses{0};
    // Faults CONTAINED by this core's handlers: only HaloMCCVR's own (see
    // GameFault below). A fault in the game's code is no longer counted here.
    std::atomic<uint64_t> g_exceptions{0};
    // Every __except here that wraps a call into the game's code uses
    // GameFault() as its filter. The build bd2aac6 freeze: the renderer
    // dereferenced null twice inside the scene render (halo2 +0x107449,
    // +0x3D0730) ~190 ms after a cinematic (StockScreen) frame. The old
    // EXCEPTION_EXECUTE_HANDLER swallowed both ("2 drops, 2 exceptions"),
    // unwinding the renderer mid-frame past its own lock/event cleanup, and
    // the render thread never presented again - a silent hang instead of a
    // crash with a dump. Now such a fault propagates to the game's / the OS's
    // own handling, counted here and reported by the cold poll.
    OwnImageFaultStats g_gameFaults{};
#define GameFault(anchor) \
    OwnImageFaultFilter(GetExceptionInformation(), (anchor), g_gameFaults)
    // E-H2-12: per eye pass, the projection read back from record+0x4AC
    // after the scene render returned. Unreadable means that eye's crop used
    // the written cover; a mismatch means Saber rasterised a different
    // frustum than the degrees the mod wrote, which the crop then follows.
    std::atomic<uint64_t> g_projectionReadbacks{0};
    std::atomic<uint64_t> g_projectionUnreadable{0};
    std::atomic<uint64_t> g_projectionMismatches{0};
    // E-H2-70 (C-H2-82): angle between the camera the engine handed THIS
    // renderer's packet build (the frame the weapon nodes were composed
    // against) and the eye camera this renderer draws them from. The
    // classic core publishes the same number; comparing the two names the
    // renderer whose weapon is tipped. Millidegrees, last and maximum.
    std::atomic<uint32_t> g_annivPitchMilliDegrees{0};
    std::atomic<uint32_t> g_annivPitchMaxMilliDegrees{0};
    std::atomic<uint32_t> g_projectionReadbackWrittenBits[2]{};
    std::atomic<uint32_t> g_projectionReadbackEngineBits[2]{};
    std::atomic<uint32_t> g_projectionReadbackStatus{0};
    uint32_t g_projectionReadbackLoggedStatus = 0;
    uint32_t g_projectionReadbackLoggedBits[4]{};
    // Every way EyeLoopBody can decline to render a pair, counted by
    // reason. 790 of 791 callbacks took one of these on 2026-08-21 and the
    // log could not say which; that is never allowed to happen again.
    enum class Bail : uint8_t
    {
        NoBinding = 0,
        SnapshotNotReady,
        HeadPoseInvalid,
        GenerationMismatch,
        SerialZero,
        SerialRepeated,
        CadenceUnsupported,
        ViewRecordUnresolved,
        ObserverBasisInvalid,
        ConstantsUnreadable,
        TrackedCameraFailed,
        EyeCameraFailed,
        CameraSaveFailed,
        CoverFovInvalid,
        FinalRtvNull,
        PairBeginRefused,
        SecondPassPreambleFaulted,
        EyeApplyFailed,
        EyeBeginRefused,
        SceneRenderFaulted,
        EyeCompleteRefused,
        CameraRestoreFailed,
        PairCompleteRefused,
        NotMainView,
        ViewSlotsEmptied,
        Count
    };
    constexpr const char* kBailNames[] = {
        "noBinding", "snapshotNotReady", "headPoseInvalid",
        "generationMismatch", "serialZero", "serialRepeated",
        "cadenceUnsupported", "viewRecordUnresolved",
        "observerBasisInvalid", "constantsUnreadable",
        "trackedCameraFailed", "eyeCameraFailed", "cameraSaveFailed",
        "coverFovInvalid", "finalRtvNull", "pairBeginRefused", "secondPassPreambleFaulted",
        "eyeApplyFailed", "eyeBeginRefused", "sceneRenderFaulted",
        "eyeCompleteRefused", "cameraRestoreFailed", "pairCompleteRefused",
        "notMainView", "viewSlotsEmptied"};
    static_assert(sizeof(kBailNames) / sizeof(kBailNames[0]) ==
        static_cast<size_t>(Bail::Count));
    std::atomic<uint64_t> g_bails[static_cast<size_t>(Bail::Count)]{};
    // E-H2-6 pose ownership outcomes and view-selection evidence.
    std::atomic<uint64_t> g_posePublished{0};
    std::atomic<uint64_t> g_poseRederived{0};
    std::atomic<uint64_t> g_poseSelfTracked{0};
    std::atomic<uint32_t> g_viewIndexMask{0};
    std::atomic<uint32_t> g_lastViewIndex{0};
    std::atomic<uint32_t> g_censusGeneration{0};
    void CountBail(Bail reason) noexcept
    {
        g_bails[static_cast<size_t>(reason)].fetch_add(
            1, std::memory_order_relaxed);
    }
    // The renderer's view slots seen filled since the level went live (ViewSlotsIntact); cleared while it is not.
    std::atomic<uint32_t> g_viewSlotsSeen{0};
    std::atomic<bool> g_viewSlotsLossLogged{false};
    uint64_t g_lastReportMs = 0;
    uint64_t g_lastPairsReported = UINT64_MAX;
    uint64_t g_lastCallbacksReported = UINT64_MAX;

    HeadReference g_reference{};
    HMODULE g_moduleReference = nullptr;
    void* g_sceneTarget = nullptr;
    void* g_rebuildTarget = nullptr;
    void* g_hostUiTarget = nullptr;
    CoreState g_coreState = CoreState::StockFallback;
    uint32_t g_rejectedGeneration = 0;
    uint32_t g_manualRecoveryGeneration = 0;
    uint32_t g_armedLoggedGeneration = 0;

    // Re-entry guard. The engine itself calls the scene render once per player
    // window; our own second pass must never be mistaken for one of those.
    thread_local bool g_insideEyeLoop = false;

    template <typename T>
    bool ReadGuarded(uintptr_t address, T& value) noexcept
    {
        if (!address)
            return false;
        __try
        {
            value = *reinterpret_cast<const volatile T*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool CopyGuarded(void* destination, const void* source, size_t bytes) noexcept
    {
        __try
        {
            std::memcpy(destination, source, bytes);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool WriteFloatsGuarded(uintptr_t address, const float* v, size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
            if (!std::isfinite(v[i]))
                return false;
        __try
        {
            for (size_t i = 0; i < n; ++i)
                *reinterpret_cast<volatile float*>(address + i * sizeof(float)) = v[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ValidHeadPose(const float q[4], const float p[3]) noexcept
    {
        float lengthSquared = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (!std::isfinite(q[i]))
                return false;
            lengthSquared += q[i] * q[i];
        }
        if (!std::isfinite(lengthSquared) || std::fabs(lengthSquared - 1.0f) > 0.05f)
            return false;
        for (int a = 0; a < 3; ++a)
            if (!std::isfinite(p[a]) ||
                std::fabs(p[a]) > kHalo2MaxHeadPositionMeters)
                return false;
        return true;
    }

    // record = *(base + 0x1A250F8) + 0x150 + viewIndex * 0x758, byte-verified
    // against the scene render's own arithmetic at 0x2DF2C5.
    bool ResolveViewRecord(
        uintptr_t base, uint32_t viewIndex, uintptr_t& record) noexcept
    {
        uintptr_t collection = 0;
        if (!ReadGuarded(base + kHalo2SaberViewCollectionSlotRva, collection) ||
            !collection)
        {
            return false;
        }
        int32_t count = 0;
        if (!ReadGuarded(collection + kHalo2SaberViewCountOffset, count) ||
            count <= 0 ||
            static_cast<uint32_t>(count) > kHalo2SaberViewRecordCapacity ||
            viewIndex >= static_cast<uint32_t>(count))
        {
            return false;
        }
        record = collection + kHalo2SaberViewRecordArrayOffset +
            static_cast<uintptr_t>(viewIndex) * kHalo2SaberViewRecordStride;
        return true;
    }

    bool ReadObserverBasis(uintptr_t result, Halo2CameraBasis& basis) noexcept
    {
        __try
        {
            const auto* p = reinterpret_cast<const volatile float*>(
                result + kHalo2ObserverResultPositionOffset);
            const auto* f = reinterpret_cast<const volatile float*>(
                result + kHalo2ObserverResultForwardOffset);
            const auto* u = reinterpret_cast<const volatile float*>(
                result + kHalo2ObserverResultUpOffset);
            for (int a = 0; a < 3; ++a)
            {
                basis.position[a] = p[a];
                basis.forward[a] = f[a];
                basis.up[a] = u[a];
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return Halo2ValidateCameraBasis(basis);
    }

    // One eye: write the record's embedded camera world matrix and the
    // headset cover as the engine's own horizontal/vertical degrees, let
    // the engine refresh its near-plane rectangle (0xBC380), normalise the
    // basis and derive the inverse (0xBC2B0), then rebuild every matrix the
    // record carries (0x1C6D80, four null-safe arguments). The per-view
    // consumers that re-derive tangents from the degree fields (0x1D6530,
    // 0x1CB0F0, 0x247150, the shadow builders) therefore see the same
    // frustum the projection does. No matrix is invented here.
    bool ApplyEyeCamera(
        uintptr_t record, const Halo2CameraBasis& eye,
        const Halo2SaberCameraConstants& constants,
        const Halo2SaberEyeCover& cover) noexcept
    {
        Halo2SaberViewMatrix matrix{};
        if (!Halo2BuildSaberViewMatrix(eye, constants, matrix))
            return false;
        const uintptr_t camera = record + kHalo2SaberViewRecordCameraOffset;
        if (!WriteFloatsGuarded(camera, matrix.m, 16) ||
            !WriteFloatsGuarded(
                camera + kHalo2SaberCameraHorizontalFovDegreesOffset,
                &cover.horizontalDegrees, 1) ||
            !WriteFloatsGuarded(
                camera + kHalo2SaberCameraVerticalFovDegreesOffset,
                &cover.verticalDegrees, 1))
        {
            return false;
        }
        const auto refresh = reinterpret_cast<CameraRefreshRectFn>(
            g_cameraRefreshRect.load(std::memory_order_acquire));
        const auto commit = reinterpret_cast<CameraCommitFn>(
            g_cameraCommit.load(std::memory_order_acquire));
        const auto rebuild = reinterpret_cast<RebuildViewMatricesFn>(
            g_rebuildMatrices.load(std::memory_order_acquire));
        if (!refresh || !commit || !rebuild)
            return false;
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        __try
        {
            refresh(reinterpret_cast<void*>(camera));
            commit(reinterpret_cast<void*>(camera));
            rebuild(reinterpret_cast<void*>(record), nullptr, nullptr, nullptr);
            return true;
        }
        __except (GameFault(faultAnchor)) { return false; }
    }

    bool RestoreCamera(
        uintptr_t record, const uint8_t* savedCamera) noexcept
    {
        const uintptr_t camera = record + kHalo2SaberViewRecordCameraOffset;
        if (!CopyGuarded(
                reinterpret_cast<void*>(camera), savedCamera,
                kHalo2SaberViewRecordCameraBytes))
        {
            return false;
        }
        const auto rebuild = reinterpret_cast<RebuildViewMatricesFn>(
            g_rebuildMatrices.load(std::memory_order_acquire));
        if (!rebuild)
            return false;
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        __try
        {
            rebuild(reinterpret_cast<void*>(record), nullptr, nullptr, nullptr);
            return true;
        }
        __except (GameFault(faultAnchor)) { return false; }
    }

    // Whether an eye pass may run now: no view slot the scene render reads (kHalo2SaberViewSlotsRva) that was filled
    // earlier this level is empty. A pass started after the game emptied one, on its way to a loading screen, faults
    // in game code at halo2+0x107449. Slots that cannot be read count as empty only if they were once read filled.
    bool ViewSlotsIntact(uintptr_t base) noexcept
    {
        uint32_t filled = 0;
        for (uint32_t i = 0; i < kHalo2SaberViewSlotCount; ++i)
        {
            uintptr_t pointer = 0;
            if (ReadGuarded(base + kHalo2SaberViewSlotsRva + i * kHalo2SaberViewSlotStride +
                                kHalo2SaberViewSlotPointerOffset, pointer) && pointer)
                filled |= 1u << i;
        }
        const uint32_t seen = g_viewSlotsSeen.load(std::memory_order_relaxed);
        if (!Halo2SaberViewSlotsIntact(seen, filled))
        {
            if (!g_viewSlotsLossLogged.exchange(true, std::memory_order_relaxed))
                LOG("Halo 2 Anniversary stereo: the game emptied a view target the scene render needs (slots 0x%03X "
                    "of 0x%03X); the game draws these frames alone until the level is live again",
                    seen & ~filled, seen);
            return false;
        }
        g_viewSlotsSeen.store(seen | filled, std::memory_order_relaxed);
        return true;
    }

    void CallStock(
        SaberSceneFn original, void* ctx, void* rdx, uint32_t viewIndex) noexcept
    {
        g_stockPasses.fetch_add(1, std::memory_order_relaxed);
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        __try { original(ctx, rdx, viewIndex); }
        __except (GameFault(faultAnchor))
        {
            g_exceptions.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // E-H2-18 read-back of what the engine holds for the weapon after the
    // scene render: first-person projection vs world projection diagonals.
    void ReadBackFirstPersonProjection(uintptr_t record) noexcept
    {
        g_fpReadbacks.fetch_add(1, std::memory_order_relaxed);
        float world[kHalo2SaberMatrixFloats]{};
        float firstPerson[kHalo2SaberMatrixFloats]{};
        if (!CopyGuarded(world,
                         reinterpret_cast<const void*>(
                             record + kHalo2SaberViewRecordProjectionOffset),
                         sizeof(world)) ||
            !CopyGuarded(firstPerson,
                         reinterpret_cast<const void*>(
                             record + kHalo2SaberViewRecordFirstPersonProjectionOffset),
                         sizeof(firstPerson)))
        {
            g_fpReadbackStatus.store(3u, std::memory_order_release);
            return;
        }
        uint32_t bits[2]{};
        std::memcpy(&bits[0], &world[0], sizeof(float));
        std::memcpy(&bits[1], &world[5], sizeof(float));
        g_fpWorldBits[0].store(bits[0], std::memory_order_relaxed);
        g_fpWorldBits[1].store(bits[1], std::memory_order_relaxed);
        const bool agrees = Halo2SaberProjectionScalesMatch(firstPerson, world, 0.01f);
        if (!agrees)
        {
            g_fpOverwritten.fetch_add(1, std::memory_order_relaxed);
            std::memcpy(&bits[0], &firstPerson[0], sizeof(float));
            std::memcpy(&bits[1], &firstPerson[5], sizeof(float));
            g_fpStockBits[0].store(bits[0], std::memory_order_relaxed);
            g_fpStockBits[1].store(bits[1], std::memory_order_relaxed);
        }
        g_fpReadbackStatus.store(agrees ? 1u : 2u, std::memory_order_release);
    }

    void EyeLoopBody(
        SaberSceneFn original, void* ctx, void* rdx, uint32_t viewIndex) noexcept
    {
        // The scene render below (and the scope pass's) is the game's code.
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        const uintptr_t observer = g_observerResult.load(std::memory_order_acquire);
        if (!base || !generation || !observer)
        {
            CountBail(Bail::NoBinding);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }

        Halo2SynchronousVrRenderSnapshot snapshot{};
        bool ready = false;
        __try { ready = VR_Halo2GetSynchronousRenderSnapshot(snapshot); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ready = false; }

        uintptr_t record = 0;
        uint32_t recordFlags = 0;
        Halo2CameraBasis stock{};
        Halo2SaberCameraConstants constants{};
        const uint64_t serial = snapshot.preparedSerial;
        // Sequential so the FIRST failing condition is the one counted.
        Bail bail = Bail::Count;
        if (!ready)
            bail = Bail::SnapshotNotReady;
        else if (!snapshot.headPoseValid ||
                 !ValidHeadPose(snapshot.headOrientation, snapshot.headPosition))
            bail = Bail::HeadPoseInvalid;
        else if (snapshot.generation != generation)
            bail = Bail::GenerationMismatch;
        else if (serial == 0)
            bail = Bail::SerialZero;
        else if (serial == g_lastCompletedSerial.load(std::memory_order_acquire))
            bail = Bail::SerialRepeated;
        else if (!Halo2PreparedCadenceSupported(
                     snapshot.predictedDisplayPeriodNs,
                     snapshot.predictedDisplayDeltaNs))
            bail = Bail::CadenceUnsupported;
        else if (!ResolveViewRecord(base, viewIndex, record))
            bail = Bail::ViewRecordUnresolved;
        else if (!ReadGuarded(record + kHalo2SaberViewRecordFlagsOffset, recordFlags) ||
                 !Halo2SaberViewRecordIsMainView(recordFlags))
            bail = Bail::NotMainView;
        else if (!ReadObserverBasis(observer, stock))
            bail = Bail::ObserverBasisInvalid;
        else if (!Halo2SaberCamera_ReadConstants(base, constants))
            bail = Bail::ConstantsUnreadable;
        else if (!ViewSlotsIntact(base))
            bail = Bail::ViewSlotsEmptied;
        if (bail != Bail::Count)
        {
            CountBail(bail);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }

        if (g_recenterRequested.exchange(false, std::memory_order_acq_rel) ||
            !g_referenceValid.load(std::memory_order_acquire))
        {
            std::memcpy(g_reference.orientation, snapshot.headOrientation,
                        sizeof(g_reference.orientation));
            std::memcpy(g_reference.position, snapshot.headPosition,
                        sizeof(g_reference.position));
            g_reference.position[1] += VR_RecenterStandingLiftM(GameTitle::Halo2);   // the standing height, as the observer's
            g_referenceValid.store(true, std::memory_order_release);
        }

        Halo2TrackedHeadInput head{};
        std::memcpy(head.orientation, snapshot.headOrientation, sizeof(head.orientation));
        std::memcpy(head.position, snapshot.headPosition, sizeof(head.position));
        std::memcpy(head.referenceOrientation, g_reference.orientation,
                    sizeof(head.referenceOrientation));
        std::memcpy(head.referencePosition, g_reference.position,
                    sizeof(head.referencePosition));

        head.positional = Game_IsPositionalTracking();
        head.worldScale = Game_GetWorldScale();

        // E-H2-6: `stock` was read from the observer result record. When the
        // observer core wrote this frame's head pose into it, tracking it
        // again doubles the pose exactly as it did in the classic core. The
        // Saber scene renders on its own thread, so its serial frequently
        // differs from the observer's: rebuild from the published stock
        // with THIS snapshot and the observer's own reference in that case.
        // E-H2-21: the engine built THIS record's camera (the matrix at
        // record+0x20, read before anything is written) from the observer
        // pose its frame pushed. Among the recent publications, the one
        // whose tracked camera reproduces that matrix is the sample every
        // object of this frame - the first-person weapon included - was
        // placed against; the eyes are built from it. The most recent
        // publication is newer than the frame on the frames where the
        // observer ran again before the Saber thread got here.
        Halo2ObserverPosePublication ring[8]{};
        int ringCount = 0;
        __try { ringCount = Halo2Observer6Dof_ReadPublishedPoses(ring, 8); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ringCount = 0; }
        Halo2ObserverPosePublication publication{};
        bool published = false;
        Halo2CameraBasis weaponBasis{};
        bool weaponBasisValid = false;
        {
            float frameMatrix[16]{};
            const bool haveFrameMatrix = CopyGuarded(
                frameMatrix,
                reinterpret_cast<const void*>(record + kHalo2SaberViewRecordCameraOffset),
                sizeof(frameMatrix));
            int matched = -1;
            if (haveFrameMatrix)
            {
                for (int i = 0; i < ringCount && matched < 0; ++i)
                {
                    Halo2SaberViewMatrix candidate{};
                    if (Halo2BuildSaberViewMatrix(ring[i].tracked, constants, candidate) &&
                        Halo2SaberViewMatricesMatch(candidate.m, frameMatrix, 2.0e-3f, 0.02f))
                        matched = i;
                }
            }
            if (matched >= 0)
            {
                // E-H2-23 (C-H2-31): the weapon was placed at the game tick
                // against the publication the observer core witnessed there,
                // and its interpolation is reset each tick, so the weapon the
                // Saber renderer draws is placed against exactly that
                // publication. The eyes are rendered from it and submitted
                // as its poses; the world is then at most one tick behind the
                // head and the compositor reprojects weapon and world
                // together. If that serial has already left the ring, the
                // frame's own sample is used and the miss is counted.
                // C-H2-33: the eyes ALWAYS render from the frame's own
                // sample, so the camera runs at the frame rate the player
                // set (72-144 Hz). C-H2-31/32 rendered them from the game
                // tick's sample and cut the camera to the 60 Hz tick - the
                // "fighting to follow my head". The tick's sample is used
                // for the WEAPON's own view instead (weaponBasis below).
                weaponBasisValid = false;
                uint64_t witnessIndex = 0;
                uint64_t witnessPreviousIndex = 0;
                bool witnessed = false;
                __try
                {
                    witnessed = Halo2Observer6Dof_WeaponTickPublication(
                        generation, witnessIndex, witnessPreviousIndex);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) { witnessed = false; }
                if (witnessed)
                {
                    int found = -1;
                    int foundPrevious = -1;
                    for (int i = 0; i < ringCount; ++i)
                    {
                        if (found < 0 && ring[i].index == witnessIndex)
                            found = i;
                        if (foundPrevious < 0 && witnessPreviousIndex &&
                            ring[i].index == witnessPreviousIndex)
                            foundPrevious = i;
                    }
                    if (found >= 0)
                    {
                        weaponBasis = ring[found].tracked;
                        weaponBasisValid = true;
                        // E-H2-32 (C-H2-37): the interpolator is reset at the
                        // placement again, so the weapon IS at the tick pose -
                        // no blend belongs here. The factor is still read and
                        // reported, because a non-zero blend with the reset
                        // armed would mean the reset stopped working.
                        float factor = 0.0f;
                        if (ReadGuarded(base + kHalo2FrameInterpolatorFactorRva, factor) &&
                            std::isfinite(factor) && factor > 0.0f && factor < 1.0f)
                        {
                            g_weaponViewBlended.fetch_add(1, std::memory_order_relaxed);
                        }
                        else
                        {
                            g_weaponViewUnblended.fetch_add(1, std::memory_order_relaxed);
                        }
                        (void)foundPrevious;
                        g_sampleWitnessed.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        g_sampleWitnessMissing.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                publication = ring[matched];
                published = true;
                // The camera in hand becomes that sample's tracked camera
                // for the owner decision below (the view record is
                // rewritten per eye anyway).
                stock = publication.tracked;
                if (matched == 0)
                    g_sampleMatchedLatest.fetch_add(1, std::memory_order_relaxed);
                else
                    g_sampleMatchedOlder.fetch_add(1, std::memory_order_relaxed);
            }
            else if (ringCount > 0)
            {
                // No sample reproduces the frame's camera (a non-observer
                // camera, or the observer skipped): counted, and the latest
                // publication is offered to the owner decision as before.
                g_sampleUnmatched.fetch_add(1, std::memory_order_relaxed);
                publication = ring[0];
                published = true;
            }
        }
        Halo2CameraBasis tracked{};
        Halo2CameraBasis eyes[kHalo2EyeCount]{};
        bool centerOk = false;
        bool usePublishedSample = false;
        switch (Halo2SelectPoseOwner(
            published, publication.generation, publication.serial,
            publication.tracked, stock, generation, serial))
        {
        case Halo2PoseOwnerDecision::UsePublishedTracked:
        case Halo2PoseOwnerDecision::RederiveFromPublishedStock:
            // E-H2-18: the camera in hand IS the observer's tracked camera,
            // the one the engine placed the first-person weapon against. The
            // eyes are built from it with the observer's OWN sample (eye
            // offsets) and submitted as that sample's view poses - never
            // rebuilt from a newer head sample, which put the weapon and the
            // world at two different head poses on every frame whose serial
            // differed from the observer's (the C-H2-24 "gun shifting").
            if (!publication.snapshot.valid)
            {
                centerOk = false;
                break;
            }
            g_posePublished.fetch_add(1, std::memory_order_relaxed);
            if (publication.serial != serial)
                g_posePublishedOlderSerial.fetch_add(1, std::memory_order_relaxed);
            tracked = stock;
            usePublishedSample = true;
            centerOk = true;
            break;
        case Halo2PoseOwnerDecision::SelfTrack:
            g_poseSelfTracked.fetch_add(1, std::memory_order_relaxed);
            centerOk = Halo2BuildTrackedCenterCamera(
                Halo2SelfTrackBase(published, publication, generation, stock), head, tracked);
            break;
        case Halo2PoseOwnerDecision::NoPose:
        default:
            centerOk = false;
            break;
        }
        if (!centerOk)
        {
            CountBail(Bail::TrackedCameraFailed);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }
        // The sample the eyes are built from and submitted as: the
        // observer's published one when its tracked camera is in hand, this
        // core's own prepared-serial sample otherwise (self-tracked).
        const float* eyeOffsetPosition[kHalo2EyeCount]{};
        const float* eyeOffsetOrientation[kHalo2EyeCount]{};
        const float* submitPosition[kHalo2EyeCount]{};
        const float* submitOrientation[kHalo2EyeCount]{};
        for (int eye = 0; eye < kHalo2EyeCount; ++eye)
        {
            if (usePublishedSample)
            {
                eyeOffsetPosition[eye] = publication.snapshot.eyeOffsetPosition[eye];
                eyeOffsetOrientation[eye] = publication.snapshot.eyeOffsetOrientation[eye];
                submitPosition[eye] = publication.snapshot.eyePosition[eye];
                submitOrientation[eye] = publication.snapshot.eyeOrientation[eye];
            }
            else
            {
                eyeOffsetPosition[eye] = snapshot.eyes[eye].position;
                eyeOffsetOrientation[eye] = snapshot.eyes[eye].orientation;
                submitPosition[eye] = snapshot.eyes[eye].absolutePosition;
                submitOrientation[eye] = snapshot.eyes[eye].absoluteOrientation;
            }
            if (!Halo2BuildSynchronousEyeCamera(
                    tracked, eyeOffsetPosition[eye],
                    eyeOffsetOrientation[eye], eyes[eye],
                    Game_GetWorldScale()))
            {
                CountBail(Bail::EyeCameraFailed);
                CallStock(original, ctx, rdx, viewIndex);
                return;
            }
        }

        // Everything the two passes disturb is saved before the first of them:
        // the record's embedded camera, and the 0xC8 context bytes the
        // engine's caller prepared for THIS call (one call site also stores a
        // resource pointer at ctx+0x98 after its memset, which the scene pass
        // consumes; zeroing it for the second eye would not be what the
        // engine handed the first).
        static thread_local uint8_t savedCamera[kHalo2SaberViewRecordCameraBytes];
        static thread_local uint8_t savedContext[kHalo2SaberSceneContextResetBytes];
        if (!CopyGuarded(
                savedCamera,
                reinterpret_cast<const void*>(
                    record + kHalo2SaberViewRecordCameraOffset),
                kHalo2SaberViewRecordCameraBytes) ||
            !CopyGuarded(
                savedContext,
                reinterpret_cast<const uint8_t*>(ctx) +
                    kHalo2SaberSceneContextResetOffset,
                kHalo2SaberSceneContextResetBytes))
        {
            CountBail(Bail::CameraSaveFailed);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }
        // The cover each eye renders with is the headset's: both eyes' native
        // frusta, per axis, as the engine's own horizontal/vertical degrees
        // (E-H2-7). The half-angles go to the compositor as this eye's
        // symmetric cover. VR_Halo2GetSynchronousHalfFovs cannot be used
        // here: the pair token holds the RESERVED serial until the pair
        // completes. The stock degrees are read only to be logged once.
        // E-H2-13: aspect-locked to the camera's own +0x158, as 0xBC560
        // keeps it, so every consumer that derives one axis from the other
        // sees the frustum the scene was drawn with (C-H2-17's independent
        // axes squashed the first-person weapon).
        float stockAspect = 0.0f;
        std::memcpy(&stockAspect,
                    savedCamera + kHalo2SaberCameraAspectOffset,
                    sizeof(stockAspect));
        Halo2SaberEyeCover cover{};
        if (!snapshot.eyes[0].fovValid || !snapshot.eyes[1].fovValid ||
            !Halo2DeriveSaberAspectLockedEyeCover(
                snapshot.eyes[0].fov, snapshot.eyes[1].fov, stockAspect, cover))
        {
            CountBail(Bail::CoverFovInvalid);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }
        const float halfFovX = cover.halfHorizontalRadians;
        const float halfFovY = cover.halfVerticalRadians;
        if (g_coverLoggedGeneration.load(std::memory_order_acquire) != generation)
        {
            g_coverLoggedGeneration.store(generation, std::memory_order_release);
            float stockHorizontalDegrees = 0.0f;
            float stockVerticalDegrees = 0.0f;
            std::memcpy(&stockHorizontalDegrees,
                        savedCamera + kHalo2SaberCameraHorizontalFovDegreesOffset,
                        sizeof(stockHorizontalDegrees));
            std::memcpy(&stockVerticalDegrees,
                        savedCamera + kHalo2SaberCameraVerticalFovDegreesOffset,
                        sizeof(stockVerticalDegrees));
            LOG("Halo 2 Anniversary eye cover: engine stock %.1f x %.1f deg "
                "(aspect h/w %.3f) -> per-eye %.1f x %.1f deg, aspect-locked "
                "like 0xBC560, written into the view record camera "
                "(+0x150/+0x154) and refreshed by the engine's own "
                "0xBC380/0xBC2B0/0x1C6D80",
                stockHorizontalDegrees, stockVerticalDegrees, stockAspect,
                cover.horizontalDegrees, cover.verticalDegrees);
        }
        int32_t savedLatch = 0;
        const bool haveLatch =
            ReadGuarded(base + kHalo2SaberSceneOnceLatchRva, savedLatch);

        uintptr_t finalRtv = 0;
        if (!ReadGuarded(base + kHalo2RetailFinalOutputRtvSlotRva, finalRtv) ||
            !finalRtv)
        {
            CountBail(Bail::FinalRtvNull);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }
        uintptr_t sceneRtv = 0;
        (void)ReadGuarded(base + kHalo2RetailFinalOutputRtvSlotRva + 8, sceneRtv);
        if (!VR_Halo2BeginSynchronousPair(
                generation, serial,
                reinterpret_cast<ID3D11RenderTargetView*>(finalRtv),
                reinterpret_cast<ID3D11RenderTargetView*>(sceneRtv)))
        {
            CountBail(Bail::PairBeginRefused);
            CallStock(original, ctx, rdx, viewIndex);
            return;
        }

        // MONO RENDERING (mono_view_logic.h): asked once for this frame, and only once the capture source is settled
        // (learning it compares the two eyes' pictures). One pass, as eye 0 (this core always draws eye 0 first),
        // from the tracked centre: the midpoint of the eyes, turned the head's way, under the same cover. Each eye
        // is still submitted at its own pose.
        bool mono = false;
        if (VR_Halo2CaptureSourceSettled())
        {
            float monoCover[4]{};
            mono = VR_MonoRenderThisFrame(GameTitle::Halo2, monoCover);
            if (mono)
                eyes[0] = tracked;
        }
        const int passes = static_cast<int>(Halo2ExpectedEyePasses(mono));

        bool ok = true;
        // E-H2-34: the weapon re-anchor moves the first-person geometry to
        // the tracked centre THESE eyes are rendered from; no stale-camera
        // compensation - the Saber record is rebuilt per eye by the engine.
        {
            Halo2FirstPersonPassCameras passCameras{};
            passCameras.frame = tracked;
            passCameras.frameValid = true;
            Halo2Observer6Dof_SetFirstPersonPassCameras(&passCameras);
        }
        for (int eye = 0; eye < passes && ok; ++eye)
        {
            // The first pass can be the one in which the game empties a view slot; the next must not run after it.
            if (eye != 0 && !ViewSlotsIntact(base))
            {
                CountBail(Bail::ViewSlotsEmptied);
                ok = false;
                break;
            }
            if (eye != 0)
            {
                // Reproduce the caller preamble the engine performs before every
                // scene render, and re-arm the once-per-frame latch the first
                // pass consumed, or the second eye silently skips work.
                __try
                {
                    std::memcpy(
                        reinterpret_cast<uint8_t*>(ctx) +
                            kHalo2SaberSceneContextResetOffset,
                        savedContext, kHalo2SaberSceneContextResetBytes);
                    if (haveLatch)
                    {
                        *reinterpret_cast<volatile int32_t*>(
                            base + kHalo2SaberSceneOnceLatchRva) = savedLatch;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    CountBail(Bail::SecondPassPreambleFaulted);
                    ok = false;
                    break;
                }
            }
            // E-H2-18: every rebuild of THIS record while the eye is being
            // prepared and rendered unifies the first-person projection
            // with the eye's world projection (RebuildDetour).
            g_fpPatchRecord.store(record, std::memory_order_release);
            // C-H2-33: the weapon's view for THIS eye - the tick pose the
            // weapon was placed against, plus this eye's own offset, so the
            // weapon sits where the engine put it while the world moves at
            // frame rate. Cleared when there is no witness, and the rebuild
            // then keeps the engine's own first-person view.
            g_weaponViewValid.store(false, std::memory_order_release);
            // E-H2-34 (C-H2-39): the weapon GEOMETRY is now re-anchored to
            // the frame camera by the interpolator-read hook, so the weapon
            // must be drawn with the frame's own view - the engine's +0x56C -
            // and not the tick's. The tick view stays available, disabled.
            if (weaponBasisValid && kHalo2AnniversaryWeaponTickView)
            {
                Halo2CameraBasis weaponEye{};
                Halo2SaberViewMatrix weaponMatrix{};
                if (Halo2BuildSynchronousEyeCamera(
                        weaponBasis, eyeOffsetPosition[eye],
                        eyeOffsetOrientation[eye], weaponEye,
                        Game_GetWorldScale()) &&
                    Halo2BuildSaberViewMatrix(weaponEye, constants, weaponMatrix))
                {
                    Halo2SaberViewWithoutTranslation(
                        weaponMatrix.m, g_weaponViewNoTranslation);
                    g_weaponViewValid.store(true, std::memory_order_release);
                }
            }
            if (!ApplyEyeCamera(record, eyes[eye], constants, cover))
            {
                CountBail(Bail::EyeApplyFailed);
                ok = false;
                break;
            }
            {
                // E-H2-70: measure this renderer's own compose-vs-draw
                // camera angle, the exact quantity the classic core
                // publishes, so the two are directly comparable.
                Halo2CameraBasis packetCamera{};
                if (Halo2Observer6Dof_ReadPacketBuildCamera(true, packetCamera))
                {
                    float dot = 0.0f;
                    for (int axis = 0; axis < 3; ++axis)
                        dot += packetCamera.forward[axis] * eyes[eye].forward[axis];
                    if (std::isfinite(dot))
                    {
                        dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);
                        const float degrees = std::acos(dot) * 57.29578f;
                        if (std::isfinite(degrees) && degrees >= 0.0f &&
                            degrees <= 180.0f)
                        {
                            const uint32_t milli =
                                static_cast<uint32_t>(degrees * 1000.0f);
                            g_annivPitchMilliDegrees.store(
                                milli, std::memory_order_relaxed);
                            uint32_t previous = g_annivPitchMaxMilliDegrees.load(
                                std::memory_order_relaxed);
                            while (previous < milli &&
                                   !g_annivPitchMaxMilliDegrees
                                        .compare_exchange_weak(
                                            previous, milli,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed))
                            {
                            }
                        }
                    }
                }
            }
            if (!VR_Halo2BeginSynchronousEye(generation, serial, eye))
            {
                CountBail(Bail::EyeBeginRefused);
                ok = false;
                break;
            }
            bool rendered = false;
            __try
            {
                original(ctx, rdx, viewIndex);
                rendered = true;
            }
            __except (GameFault(faultAnchor))
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
            // The raster-eye scope is closed whether or not the engine
            // returned normally; that is where the finished eye is copied.
            VR_EndRasterEye();
            // Once per generation, name what the engine actually holds at
            // this moment: the three classic output slots and the target
            // bound right now. Whether 0x197EE58 is the Saber frame when
            // the scene render returns is NOT proven either way; this is
            // the evidence, next to the pixel check.
            if (eye == 0 && rendered &&
                g_censusGeneration.load(std::memory_order_acquire) != generation)
            {
                g_censusGeneration.store(generation, std::memory_order_release);
                uintptr_t slots[3]{};
                (void)ReadGuarded(base + kHalo2RetailFinalOutputRtvSlotRva, slots[0]);
                (void)ReadGuarded(base + kHalo2RetailFinalOutputRtvSlotRva + 8, slots[1]);
                (void)ReadGuarded(base + kHalo2RetailFinalOutputRtvSlotRva + 0x30, slots[2]);
                VR_Halo2LogTargetCensusOnce(
                    "Anniversary eye 0 end", slots, viewIndex,
                    g_viewIndexMask.load(std::memory_order_relaxed));
            }
            if (!rendered)
            {
                CountBail(Bail::SceneRenderFaulted);
                ok = false;
                break;
            }
            // E-H2-12: the crop follows the projection the engine baked for
            // this eye, not the degrees the mod wrote. Read before the
            // restore below rebuilds the record from the stock camera.
            float eyeHalfFovX = halfFovX;
            float eyeHalfFovY = halfFovY;
            {
                g_projectionReadbacks.fetch_add(1, std::memory_order_relaxed);
                uint32_t writtenBits[2]{};
                std::memcpy(&writtenBits[0], &halfFovX, sizeof(float));
                std::memcpy(&writtenBits[1], &halfFovY, sizeof(float));
                g_projectionReadbackWrittenBits[0].store(
                    writtenBits[0], std::memory_order_relaxed);
                g_projectionReadbackWrittenBits[1].store(
                    writtenBits[1], std::memory_order_relaxed);
                float scaleX = 0.0f;
                float scaleY = 0.0f;
                Halo2SymmetricHalfFovs engine{};
                if (ReadGuarded(record + kHalo2SaberProjectionScaleXOffset, scaleX) &&
                    ReadGuarded(record + kHalo2SaberProjectionScaleYOffset, scaleY) &&
                    Halo2HalfFovsFromProjectionScales(scaleX, scaleY, engine))
                {
                    uint32_t engineBits[2]{};
                    std::memcpy(&engineBits[0], &engine.horizontal, sizeof(float));
                    std::memcpy(&engineBits[1], &engine.vertical, sizeof(float));
                    g_projectionReadbackEngineBits[0].store(
                        engineBits[0], std::memory_order_relaxed);
                    g_projectionReadbackEngineBits[1].store(
                        engineBits[1], std::memory_order_relaxed);
                    const Halo2SymmetricHalfFovs written{halfFovX, halfFovY};
                    const bool agrees = Halo2HalfFovsAgree(
                        engine, written, kHalo2ProjectionReadbackToleranceRadians);
                    if (!agrees)
                        g_projectionMismatches.fetch_add(1, std::memory_order_relaxed);
                    g_projectionReadbackStatus.store(
                        agrees ? 1u : 2u, std::memory_order_release);
                    eyeHalfFovX = engine.horizontal;
                    eyeHalfFovY = engine.vertical;
                }
                else
                {
                    g_projectionUnreadable.fetch_add(1, std::memory_order_relaxed);
                    g_projectionReadbackStatus.store(3u, std::memory_order_release);
                }
            }
            // E-H2-18: what the engine holds NOW for the weapon: the
            // first-person projection must equal the world projection the
            // scene was rasterised with, or a rebuild this core did not see
            // rebuilt the stock 49.6-degree one.
            ReadBackFirstPersonProjection(record);
            if (!VR_Halo2CompleteSynchronousEye(
                    generation, serial, eye, eyeHalfFovX, eyeHalfFovY,
                    submitPosition[eye], submitOrientation[eye]))
            {
                CountBail(Bail::EyeCompleteRefused);
                ok = false;
                break;
            }
        }

#include "halo2_anniversary_scope_pass.inl"

        // The engine's own state is put back before anything else, always:
        // the stock camera AND the stock first-person projection.
        Halo2Observer6Dof_SetFirstPersonPassCameras(nullptr);
        g_fpPatchRecord.store(0, std::memory_order_release);
        const bool restored = RestoreCamera(record, savedCamera);
        if (haveLatch)
        {
            __try
            {
                *reinterpret_cast<volatile int32_t*>(
                    base + kHalo2SaberSceneOnceLatchRva) = savedLatch;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (ok && !restored)
            CountBail(Bail::CameraRestoreFailed);
        if (ok && restored)
        {
            if (mono
                    ? VR_Halo2CompleteSynchronousMonoPair(
                          generation, serial, 0, submitPosition[1], submitOrientation[1])
                    : VR_Halo2CompleteSynchronousPair(generation, serial))
            {
                g_lastCompletedMonoEye.store(mono ? 0 : -1, std::memory_order_release);
                g_lastCompletedSerial.store(serial, std::memory_order_release);
                g_pairs.fetch_add(1, std::memory_order_relaxed);
                if (mono)
                    g_monoFrames.fetch_add(1, std::memory_order_relaxed);
                else
                    VR_NoteStereoFrameRendered(GameTitle::Halo2);
#if HALOMCCVR_DEV_DIAGNOSTICS
                // Dev AS SEEN (halo2_impact_witness.inl): this serial's centre camera, the head its views were
                // submitted with, and the publication the frame's camera matched.
                Halo2Observer6Dof_DevNoteAsSeenFrame(1, serial, tracked,
                    usePublishedSample ? publication.snapshot.headOrientation : snapshot.headOrientation,
                    published && publication.generation == generation ? &publication : nullptr);
#endif
                return;
            }
            CountBail(Bail::PairCompleteRefused);
        }

        VR_Halo2InvalidateSynchronousPair(generation, serial);
        g_drops.fetch_add(1, std::memory_order_relaxed);
        if (mono)
        {
            g_monoDrops.fetch_add(1, std::memory_order_relaxed);
            VR_NoteMonoFrameRejected(GameTitle::Halo2);
        }
    }

    // E-H2-18: detour on the view-record rebuild 0x1C6D80. The engine (and
    // this core's ApplyEyeCamera) rebuild the record from its camera; after
    // the original has built both projections, the record being prepared
    // for an eye gets its first-person projection (+0x4EC) and first-person
    // view-projection (+0x5EC) replaced by the world ones (+0x4AC, +0x56C),
    // so the weapon is drawn with the eye's frustum. A rebuild with a clip
    // plane (the water-mirror view) and every other record pass untouched.
    __declspec(noinline) void __fastcall RebuildDetour(
        void* record, void* viewMatrix, void* projectionMatrix, void* clipPlane)
    {
        const auto original = reinterpret_cast<RebuildViewMatricesFn>(
            g_originalRebuild.load(std::memory_order_acquire));
        if (!original)
            return;
        original(record, viewMatrix, projectionMatrix, clipPlane);
        const uintptr_t target = g_fpPatchRecord.load(std::memory_order_acquire);
        if (!target || reinterpret_cast<uintptr_t>(record) != target || clipPlane)
            return;
        float world[kHalo2SaberMatrixFloats]{};
        float viewProjection[kHalo2SaberMatrixFloats]{};
        if (!CopyGuarded(world,
                         reinterpret_cast<const void*>(
                             target + kHalo2SaberViewRecordProjectionOffset),
                         sizeof(world)) ||
            !CopyGuarded(viewProjection,
                         reinterpret_cast<const void*>(
                             target + kHalo2SaberViewRecordViewProjectionNoTranslationOffset),
                         sizeof(viewProjection)))
        {
            return;
        }
        // C-H2-33: the weapon's view-projection. The engine has just written
        // viewNoTranslation(this eye) x world projection at +0x56C; the mod
        // reconstructs exactly that product from the eye camera it wrote and
        // compares. Only when the reconstruction MATCHES (proving the
        // convention) is the weapon's own product - built from the tick pose
        // the weapon was placed against - used instead. Otherwise the engine's
        // own +0x56C stands, exactly as C-H2-26 shipped it.
        float weaponViewProjection[kHalo2SaberMatrixFloats]{};
        bool useWeaponView = false;
        if (g_weaponViewValid.load(std::memory_order_acquire))
        {
            float eyeCamera[kHalo2SaberMatrixFloats]{};
            float eyeViewNoTranslation[kHalo2SaberMatrixFloats]{};
            float reconstructed[kHalo2SaberMatrixFloats]{};
            if (CopyGuarded(eyeCamera,
                            reinterpret_cast<const void*>(
                                target + kHalo2SaberViewRecordCameraOffset),
                            sizeof(eyeCamera)))
            {
                Halo2SaberViewWithoutTranslation(eyeCamera, eyeViewNoTranslation);
                Halo2MultiplyMatrix4x4(eyeViewNoTranslation, world, reconstructed);
                const bool matches =
                    Halo2MatricesClose(reconstructed, viewProjection, 2.0e-3f);
                const uint32_t generation = g_generation.load(std::memory_order_acquire);
                if (g_weaponViewCheckGeneration.exchange(
                        generation, std::memory_order_acq_rel) != generation)
                {
                    g_weaponViewCheckPassed.store(matches, std::memory_order_release);
                    LOG("Halo 2 Anniversary first-person weapon view: the mod's "
                        "reconstruction of the engine's own view-projection "
                        "(+0x%X) %s, so the weapon %s drawn with the view of the "
                        "game tick it was placed at while the world keeps the "
                        "frame's own view",
                        static_cast<unsigned>(
                            kHalo2SaberViewRecordViewProjectionNoTranslationOffset),
                        matches ? "MATCHES" : "does NOT match",
                        matches ? "is" : "is NOT");
                }
                if (matches)
                {
                    Halo2MultiplyMatrix4x4(
                        g_weaponViewNoTranslation, world, weaponViewProjection);
                    useWeaponView = true;
                }
            }
        }
        if (!WriteFloatsGuarded(
                target + kHalo2SaberViewRecordFirstPersonProjectionOffset,
                world, kHalo2SaberMatrixFloats) ||
            !WriteFloatsGuarded(
                target + kHalo2SaberViewRecordFirstPersonViewProjectionOffset,
                useWeaponView ? weaponViewProjection : viewProjection,
                kHalo2SaberMatrixFloats))
        {
            return;
        }
        if (useWeaponView)
            g_weaponViewApplied.fetch_add(1, std::memory_order_relaxed);
        else
            g_weaponViewRejected.fetch_add(1, std::memory_order_relaxed);
        g_fpPatches.fetch_add(1, std::memory_order_relaxed);
    }

    // E-H2-18: detour on the Saber host's post-scene callback 0x696A0, the
    // Blam interface/HUD draw (-> 0x960230(1) -> 0x7E1990 -> 0x831CB0). The
    // engine runs it once over the backbuffer, which at this point holds the
    // LAST eye rendered (eye 1). Replay: let it draw over eye 1 and recapture
    // eye 1; put eye 0's finished scene back into the backbuffer, run it
    // again, recapture eye 0. Both eyes then carry the HUD, the way the
    // classic render_view draws it per eye. A frame without a complete pair
    // of its own runs the callback once, untouched.
    bool DrawNativeGameplayHud(
        uint32_t generation, uint64_t serial) noexcept
    {
        Halo2CameraRectangle source{};
        float halfX[2]{}, halfY[2]{};
        if (!VR_Halo2GetHudSourceRectangle(generation, serial, source) ||
            !VR_Halo2GetSynchronousHalfFovs(
                generation, serial, halfX, halfY))
        {
            g_nativeChudFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const Halo2SymmetricHalfFovs cover{
            std::max(halfX[0], halfX[1]),
            std::max(halfY[0], halfY[1])};
        const bool ok = Halo2NativeHud_DrawPlayer(
            generation, 0, source, cover);
        if (ok)
            g_nativeChudDraws.fetch_add(1, std::memory_order_relaxed);
        else
            g_nativeChudFailures.fetch_add(1, std::memory_order_relaxed);
        return ok;
    }

    thread_local bool g_insideHudReplay = false;
    // The armed half of HostUiDetour. Its caller restores the in-replay flag
    // and the callback count in a __finally, so they are right even when a
    // game fault propagates out of here and something above handles it.
    __declspec(noinline) void HostUiReplay(
        HostUiCallbackFn original, void* param) noexcept
    {
        // The host UI callback (original) is the game's code.
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        const uint64_t serial = g_lastCompletedSerial.load(std::memory_order_acquire);
        const int monoEye = g_lastCompletedMonoEye.load(std::memory_order_acquire);
        const char* reason = "none";
        bool eligible = false;
        __try { eligible = VR_Halo2HudReplayEligible(generation, serial, &reason); }
        __except (EXCEPTION_EXECUTE_HANDLER) { eligible = false; reason = "eligibility faulted"; }
        if (!eligible)
        {
            g_hudStockPasses.fetch_add(1, std::memory_order_relaxed);
            g_hudLastReason = reason;
            __try { original(param); }
            __except (GameFault(faultAnchor))
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        bool ok = true;
        __try
        {
            if (monoEye == 0 || monoEye == 1)
            {
                // MONO: one picture, drawn as `monoEye`, shown to both eyes: the interface goes over it once.
                ok = VR_Halo2RestoreEyeToFinalTarget(generation, serial, monoEye);
                if (!ok)
                    reason = "mono eye restore refused";
                if (ok)
                {
                    original(param);
                    (void)DrawNativeGameplayHud(generation, serial);
                    ok = VR_Halo2RecaptureEyeFromFinalTarget(generation, serial, monoEye);
                    if (!ok)
                        reason = "mono eye recapture refused";
                }
            }
            else
            {
                // E-H2-19: the backbuffer is NOT guaranteed to hold eye 1 here -
                // a second Saber view rendered after the pair (serialRepeated
                // stock passes, the C-H2-26 "flash to the flat screen" in one
                // eye). Eye 1's finished scene is put back first, always.
                ok = VR_Halo2RestoreEyeToFinalTarget(generation, serial, 1);
                if (!ok)
                    reason = "eye 1 restore refused";
                if (ok)
                {
                    original(param);                         // interface over eye 1
                    (void)DrawNativeGameplayHud(generation, serial);
                }
                if (ok)
                    ok = VR_Halo2RecaptureEyeFromFinalTarget(generation, serial, 1);
                if (!ok)
                    reason = "eye 1 recapture refused";
                if (ok && !VR_Halo2RestoreEyeToFinalTarget(generation, serial, 0))
                {
                    ok = false;
                    reason = "eye 0 restore refused";
                }
                if (ok)
                {
                    original(param);                         // interface over eye 0
                    (void)DrawNativeGameplayHud(generation, serial);
                    ok = VR_Halo2RecaptureEyeFromFinalTarget(generation, serial, 0);
                    if (!ok)
                        reason = "eye 0 recapture refused";
                }
            }
        }
        __except (GameFault(faultAnchor))
        {
            g_exceptions.fetch_add(1, std::memory_order_relaxed);
            ok = false;
            reason = "replay faulted";
        }
        if (ok)
        {
            g_hudReplays.fetch_add(1, std::memory_order_relaxed);
            if (monoEye == 0 || monoEye == 1)
                g_hudMonoReplays.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            g_hudFailures.fetch_add(1, std::memory_order_relaxed);
            g_hudLastReason = reason;
        }
    }

    __declspec(noinline) void __fastcall HostUiDetour(void* param)
    {
        const auto original = reinterpret_cast<HostUiCallbackFn>(
            g_originalHostUi.load(std::memory_order_acquire));
        if (!original)
            return;
        g_hudCallbacks.fetch_add(1, std::memory_order_relaxed);
        if (g_insideHudReplay || g_insideEyeLoop ||
            !g_armed.load(std::memory_order_acquire) ||
            !g_levelLive.load(std::memory_order_acquire) ||
            !g_remasteredLive.load(std::memory_order_acquire) ||
            g_teardown.load(std::memory_order_acquire))
        {
            original(param);
            return;
        }
        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_insideHudReplay = true;
        OwnImageFault_BeginScope();
        __try { HostUiReplay(original, param); }
        __finally
        {
            g_insideHudReplay = false;
            g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall SaberSceneDetour(
        void* ctx, void* rdx, uint32_t viewIndex)
    {
        const auto original =
            reinterpret_cast<SaberSceneFn>(
                g_originalScene.load(std::memory_order_acquire));
        if (!original)
            return;

        if (viewIndex < 32)
            g_viewIndexMask.fetch_or(1u << viewIndex, std::memory_order_relaxed);
        g_lastViewIndex.store(viewIndex, std::memory_order_relaxed);
        if (g_callbacks.fetch_add(1, std::memory_order_relaxed) == 0)
        {
            LOG("Halo 2 Anniversary stereo: the remastered scene render "
                "detour FIRED for the first time; the binding is live");
        }

        if (!g_insideEyeLoop && !g_levelLive.load(std::memory_order_acquire))
        {
            g_viewSlotsSeen.store(0, std::memory_order_relaxed);
            g_viewSlotsLossLogged.store(false, std::memory_order_relaxed);
        }
        // Our own second pass re-enters this detour; it must run stock.
        if (g_insideEyeLoop || !g_armed.load(std::memory_order_acquire) ||
            !g_levelLive.load(std::memory_order_acquire) ||
            !g_remasteredLive.load(std::memory_order_acquire) ||
            g_teardown.load(std::memory_order_acquire))
        {
            original(ctx, rdx, viewIndex);
            return;
        }

        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_insideEyeLoop = true;
        OwnImageFault_BeginScope();
        // EyeLoopBody calls the game's scene render; this outer handler must
        // not catch what the inner GameFault filters let through.
        const uintptr_t faultAnchor = OwnImageFault_Anchor();
        __try
        {
            __try { EyeLoopBody(original, ctx, rdx, viewIndex); }
            __except (GameFault(faultAnchor))
            {
                g_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        __finally
        {
            g_insideEyeLoop = false;
            g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool EntryBytesMatch(
        uintptr_t address, const uint8_t* expected, size_t bytes) noexcept
    {
        uint8_t actual[32]{};
        if (!address || !expected || bytes == 0 || bytes > sizeof(actual))
            return false;
        if (!CopyGuarded(actual, reinterpret_cast<const void*>(address), bytes))
            return false;
        return std::memcmp(actual, expected, bytes) == 0;
    }

    bool IsExecutable(uintptr_t address) noexcept
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)))
            return false;
        if (info.State != MEM_COMMIT)
            return false;
        const DWORD mask = info.Protect & 0xFF;
        return !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mask == PAGE_EXECUTE_READ || mask == PAGE_EXECUTE_READWRITE ||
             mask == PAGE_EXECUTE_WRITECOPY || mask == PAGE_EXECUTE);
    }

#include "halo2_anniversary_cleanup.inl"

    bool InstallCore(
        uintptr_t base, uint32_t generation, uintptr_t observerResult) noexcept
    {
        if (g_rejectedGeneration == generation)
            return false;

        const uintptr_t scene = base + kHalo2SaberSceneRenderRva;
        const uintptr_t rebuild = base + kHalo2SaberRebuildViewMatricesRva;
        const uintptr_t commit = base + kHalo2SaberCameraCommitRva;
        const uintptr_t refresh = base + kHalo2SaberCameraRefreshRectRva;
        const uintptr_t hostUi = base + kHalo2SaberHostUiCallbackRva;
        if (!IsExecutable(scene) || !IsExecutable(rebuild) ||
            !IsExecutable(commit) || !IsExecutable(refresh) ||
            !IsExecutable(hostUi))
        {
            LOG("Halo 2 Anniversary stereo WITHHELD: scene=%d rebuild=%d "
                "commit=%d refresh=%d executable; stock rendering is untouched",
                IsExecutable(scene) ? 1 : 0, IsExecutable(rebuild) ? 1 : 0,
                IsExecutable(commit) ? 1 : 0, IsExecutable(refresh) ? 1 : 0);
            g_rejectedGeneration = generation;
            return false;
        }
        // The three engine helpers the eye pass CALLS are pinned to their
        // proven entry bytes; a module that differs keeps stock rendering.
        const bool entriesPinned =
            EntryBytesMatch(rebuild, kHalo2SaberRebuildViewMatricesEntryBytes,
                            sizeof(kHalo2SaberRebuildViewMatricesEntryBytes)) &&
            EntryBytesMatch(commit, kHalo2SaberCameraCommitEntryBytes,
                            sizeof(kHalo2SaberCameraCommitEntryBytes)) &&
            EntryBytesMatch(refresh, kHalo2SaberCameraRefreshRectEntryBytes,
                            sizeof(kHalo2SaberCameraRefreshRectEntryBytes)) &&
            EntryBytesMatch(hostUi, kHalo2SaberHostUiCallbackEntryBytes,
                            sizeof(kHalo2SaberHostUiCallbackEntryBytes));
        if (!entriesPinned)
        {
            LOG("Halo 2 Anniversary stereo WITHHELD: the engine's matrix "
                "rebuild / camera commit / rect refresh entry bytes do not "
                "match the proven module; stock rendering is untouched");
            g_rejectedGeneration = generation;
            return false;
        }
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &module) || !module)
        {
            return false;
        }

        void* trampoline = nullptr;
        const MH_STATUS created = MH_CreateHook(
            reinterpret_cast<void*>(scene),
            reinterpret_cast<void*>(&SaberSceneDetour), &trampoline);
        if (created != MH_OK || !trampoline)
        {
            LOG("Halo 2 Anniversary stereo WITHHELD: scene hook create=%d",
                static_cast<int>(created));
            if (created == MH_OK)
                (void)MH_RemoveHook(reinterpret_cast<void*>(scene));
            g_rejectedGeneration = generation;
            return false;
        }

        // E-H2-18: the rebuild and host-UI detours. All three hooks are
        // created before any is enabled; a failure leaves nothing installed.
        void* rebuildTrampoline = nullptr;
        void* hostUiTrampoline = nullptr;
        const MH_STATUS createdRebuild = MH_CreateHook(
            reinterpret_cast<void*>(rebuild),
            reinterpret_cast<void*>(&RebuildDetour), &rebuildTrampoline);
        const MH_STATUS createdHostUi = createdRebuild == MH_OK
            ? MH_CreateHook(reinterpret_cast<void*>(hostUi),
                            reinterpret_cast<void*>(&HostUiDetour),
                            &hostUiTrampoline)
            : MH_ERROR_NOT_CREATED;
        if (createdRebuild != MH_OK || !rebuildTrampoline ||
            createdHostUi != MH_OK || !hostUiTrampoline)
        {
            LOG("Halo 2 Anniversary stereo WITHHELD: rebuild hook create=%d, "
                "host UI hook create=%d",
                static_cast<int>(createdRebuild), static_cast<int>(createdHostUi));
            (void)MH_RemoveHook(reinterpret_cast<void*>(scene));
            if (createdRebuild == MH_OK)
                (void)MH_RemoveHook(reinterpret_cast<void*>(rebuild));
            if (createdHostUi == MH_OK)
                (void)MH_RemoveHook(reinterpret_cast<void*>(hostUi));
            g_rejectedGeneration = generation;
            return false;
        }

        g_moduleReference = module;
        g_sceneTarget = reinterpret_cast<void*>(scene);
        g_rebuildTarget = reinterpret_cast<void*>(rebuild);
        g_hostUiTarget = reinterpret_cast<void*>(hostUi);
        g_originalScene.store(
            reinterpret_cast<uintptr_t>(trampoline), std::memory_order_release);
        g_originalRebuild.store(
            reinterpret_cast<uintptr_t>(rebuildTrampoline), std::memory_order_release);
        g_originalHostUi.store(
            reinterpret_cast<uintptr_t>(hostUiTrampoline), std::memory_order_release);
        g_nativeChudDraws.store(0, std::memory_order_relaxed);
        g_nativeChudFailures.store(0, std::memory_order_relaxed);
        g_fpPatchRecord.store(0, std::memory_order_release);
        g_rebuildMatrices.store(rebuild, std::memory_order_release);
        g_cameraCommit.store(commit, std::memory_order_release);
        g_cameraRefreshRect.store(refresh, std::memory_order_release);
        g_moduleBase.store(base, std::memory_order_release);
        g_observerResult.store(observerResult, std::memory_order_release);
        g_generation.store(generation, std::memory_order_release);
        g_teardown.store(false, std::memory_order_release);
        g_referenceValid.store(false, std::memory_order_release);
        g_recenterRequested.store(true, std::memory_order_release);
        g_lastCompletedMonoEye.store(-1, std::memory_order_release);
        g_lastCompletedSerial.store(0, std::memory_order_release);
        g_installed.store(true, std::memory_order_release);
        g_coreState = CoreState::CleanupRequired;

        if (MH_EnableHook(reinterpret_cast<void*>(scene)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void*>(rebuild)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void*>(hostUi)) != MH_OK)
        {
            LOG("Halo 2 Anniversary stereo WITHHELD: hook enable failed");
            (void)RemoveCore("install rollback");
            g_rejectedGeneration = generation;
            return false;
        }
        g_coreState = CoreState::Installed;
        if (Halo2NativeHud_Armed())
        {
            LOG("Halo 2 native gameplay HUD replay ON in Anniversary: each "
                "restored eye uses the shared H2EK-mapped anchor layout");
        }
        else
        {
            LOG("Halo 2 native gameplay HUD replay WITHHELD in Anniversary: "
                "shared anchor ownership is not armed; interface replay and "
                "stereo remain active");
        }
        LOG("Halo 2 Anniversary stereo installed: scene render +0x%X run twice "
            "per frame, per-eye camera written into the view record at "
            "collection+0x150+view*0x758+0x20 and rebuilt by the engine's own "
            "+0x%X (detoured: the eye's first-person projection +0x4EC/+0x5EC "
            "is unified with its world projection +0x4AC/+0x56C); host UI "
            "callback +0x%X (the Blam interface/HUD draw) replayed per eye; "
            "every owned byte is restored",
            static_cast<unsigned>(kHalo2SaberSceneRenderRva),
            static_cast<unsigned>(kHalo2SaberRebuildViewMatricesRva),
            static_cast<unsigned>(kHalo2SaberHostUiCallbackRva));
        return true;
    }

    void Report() noexcept
    {
        const uint64_t now = GetTickCount64();
        if (now - g_lastReportMs < 2000)
            return;
        g_lastReportMs = now;
        const uint64_t pairs = g_pairs.load(std::memory_order_relaxed);
        const uint64_t callbacks = g_callbacks.load(std::memory_order_relaxed);
        // Report on ANY movement, not just completed pairs. "Zero pairs" and
        // "the hook never fired" look identical from the player's seat, and
        // telling them apart is the whole reason four earlier Halo 2
        // candidates were expensive to diagnose.
        if (pairs == g_lastPairsReported && callbacks == g_lastCallbacksReported)
            return;
        g_lastPairsReported = pairs;
        g_lastCallbacksReported = callbacks;
        // Fixed-size, stack-only: one line per report, every nonzero reason.
        char reasons[512];
        size_t used = 0;
        reasons[0] = '\0';
        for (size_t i = 0; i < static_cast<size_t>(Bail::Count); ++i)
        {
            const uint64_t n = g_bails[i].load(std::memory_order_relaxed);
            if (!n || used >= sizeof(reasons) - 1)
                continue;
            const int written = _snprintf_s(
                reasons + used, sizeof(reasons) - used, _TRUNCATE,
                "%s%s=%llu", used ? " " : "", kBailNames[i],
                static_cast<unsigned long long>(n));
            if (written > 0)
                used += static_cast<size_t>(written);
        }
        if (g_monoFrames.load(std::memory_order_relaxed) || g_monoDrops.load(std::memory_order_relaxed))
        {
            // MONO RENDERING: of the eye pairs, the frames drawn as one pass; of the drops, the mono frames.
            LOG("Halo 2 Anniversary mono: %llu frames drawn as one pass, %llu mono frames dropped",
                static_cast<unsigned long long>(g_monoFrames.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(g_monoDrops.load(std::memory_order_relaxed)));
        }
        LOG("Halo 2 Anniversary stereo: %llu scene callbacks, %llu eye pairs, "
            "%llu drops, %llu stock passes, %llu exceptions; pose published=%llu "
            "(of which %llu rendered from an older observer serial, as that "
            "sample) self=%llu; frame camera matched the latest sample %llu "
            "times, an older sample %llu times, no sample %llu times; eyes "
            "rendered from the frame's own sample; the weapon's view came "
            "from the witnessed game tick %llu times, the witness was out of "
            "the ring %llu times (weapon view applied %llu, engine's own view "
            "kept %llu; the interpolator reported a mid-tick blend %llu times, "
            "the tick pose %llu times); views seen mask=0x%X "
            "last=%u; weapon compose-vs-draw angle %.3f deg (max %.3f); "
            "bail reasons: %s",
            static_cast<unsigned long long>(g_callbacks.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(pairs),
            static_cast<unsigned long long>(g_drops.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_stockPasses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_exceptions.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_posePublished.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_posePublishedOlderSerial.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_poseSelfTracked.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_sampleMatchedLatest.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_sampleMatchedOlder.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_sampleUnmatched.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_sampleWitnessed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_sampleWitnessMissing.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_weaponViewApplied.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_weaponViewRejected.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_weaponViewBlended.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_weaponViewUnblended.load(std::memory_order_relaxed)),
            g_viewIndexMask.load(std::memory_order_relaxed),
            g_lastViewIndex.load(std::memory_order_relaxed),
            g_annivPitchMilliDegrees.load(std::memory_order_relaxed) / 1000.0,
            g_annivPitchMaxMilliDegrees.load(std::memory_order_relaxed) / 1000.0,
            used ? reasons : "none");
        // E-H2-12: the projection read-back, whenever its verdict or its
        // numbers change. This line, not the written cover, says what
        // frustum the headset image really has.
        const uint32_t status =
            g_projectionReadbackStatus.load(std::memory_order_acquire);
        uint32_t bits[4] = {
            g_projectionReadbackWrittenBits[0].load(std::memory_order_relaxed),
            g_projectionReadbackWrittenBits[1].load(std::memory_order_relaxed),
            g_projectionReadbackEngineBits[0].load(std::memory_order_relaxed),
            g_projectionReadbackEngineBits[1].load(std::memory_order_relaxed)};
        if (status &&
            (status != g_projectionReadbackLoggedStatus ||
             std::memcmp(bits, g_projectionReadbackLoggedBits, sizeof(bits)) != 0))
        {
            g_projectionReadbackLoggedStatus = status;
            std::memcpy(g_projectionReadbackLoggedBits, bits, sizeof(bits));
            float values[4]{};
            std::memcpy(values, bits, sizeof(values));
            constexpr float kDegrees = 57.29578f;
            if (status == 3)
            {
                LOG("Halo 2 Anniversary eye projection read-back: UNREADABLE "
                    "(record+0x4AC); the headset crop uses the written cover "
                    "%.1f x %.1f deg (full angles) - unreadable=%llu of %llu",
                    2.0f * values[0] * kDegrees, 2.0f * values[1] * kDegrees,
                    static_cast<unsigned long long>(
                        g_projectionUnreadable.load(std::memory_order_relaxed)),
                    static_cast<unsigned long long>(
                        g_projectionReadbacks.load(std::memory_order_relaxed)));
            }
            else
            {
                LOG("Halo 2 Anniversary eye projection read-back: written "
                    "cover %.1f x %.1f deg, engine rasterised %.1f x %.1f deg "
                    "(full angles) - %s; the headset crop follows the engine "
                    "(mismatches=%llu of %llu)",
                    2.0f * values[0] * kDegrees, 2.0f * values[1] * kDegrees,
                    2.0f * values[2] * kDegrees, 2.0f * values[3] * kDegrees,
                    status == 1 ? "AGREES" : "MISMATCH: the engine did not "
                                             "render the cover the mod wrote",
                    static_cast<unsigned long long>(
                        g_projectionMismatches.load(std::memory_order_relaxed)),
                    static_cast<unsigned long long>(
                        g_projectionReadbacks.load(std::memory_order_relaxed)));
            }
        }
        // E-H2-18: the first-person projection, whenever its verdict changes.
        const uint32_t fpStatus = g_fpReadbackStatus.load(std::memory_order_acquire);
        if (fpStatus && fpStatus != g_fpLoggedStatus)
        {
            g_fpLoggedStatus = fpStatus;
            constexpr float kDegrees = 57.29578f;
            float worldScales[2]{}, stockScales[2]{};
            uint32_t wb[2] = {g_fpWorldBits[0].load(std::memory_order_relaxed),
                              g_fpWorldBits[1].load(std::memory_order_relaxed)};
            uint32_t sb[2] = {g_fpStockBits[0].load(std::memory_order_relaxed),
                              g_fpStockBits[1].load(std::memory_order_relaxed)};
            std::memcpy(worldScales, wb, sizeof(worldScales));
            std::memcpy(stockScales, sb, sizeof(stockScales));
            Halo2SymmetricHalfFovs world{}, stock{};
            const bool worldOk = Halo2HalfFovsFromProjectionScales(
                worldScales[0], worldScales[1], world);
            const bool stockOk = Halo2HalfFovsFromProjectionScales(
                stockScales[0], stockScales[1], stock);
            if (fpStatus == 1)
            {
                LOG("Halo 2 Anniversary first-person weapon projection: the engine "
                    "builds it from a hard-coded %.3f deg vertical FOV (0x1C6D80, "
                    "record+0x4EC), %.1fx the eye's frustum; unified with the "
                    "eye's world projection %.1f x %.1f deg on %llu rebuilds; "
                    "read-back after the scene render AGREES (%llu of %llu)",
                    kHalo2SaberFirstPersonVerticalFovDegrees,
                    worldOk ? std::tan(world.vertical) /
                                  std::tan(kHalo2SaberFirstPersonVerticalFovDegrees *
                                           0.5f / kDegrees)
                            : 0.0f,
                    worldOk ? 2.0f * world.horizontal * kDegrees : 0.0f,
                    worldOk ? 2.0f * world.vertical * kDegrees : 0.0f,
                    static_cast<unsigned long long>(g_fpPatches.load(std::memory_order_relaxed)),
                    static_cast<unsigned long long>(
                        g_fpReadbacks.load(std::memory_order_relaxed) -
                        g_fpOverwritten.load(std::memory_order_relaxed)),
                    static_cast<unsigned long long>(g_fpReadbacks.load(std::memory_order_relaxed)));
            }
            else if (fpStatus == 2)
            {
                LOG("Halo 2 Anniversary first-person weapon projection: OVERWRITTEN - "
                    "after the scene render the record holds %.1f x %.1f deg at "
                    "+0x4EC against the world's %.1f x %.1f deg; a rebuild this "
                    "core did not detour rebuilt the stock one (%llu of %llu "
                    "read-backs, %llu patches applied)",
                    stockOk ? 2.0f * stock.horizontal * kDegrees : 0.0f,
                    stockOk ? 2.0f * stock.vertical * kDegrees : 0.0f,
                    worldOk ? 2.0f * world.horizontal * kDegrees : 0.0f,
                    worldOk ? 2.0f * world.vertical * kDegrees : 0.0f,
                    static_cast<unsigned long long>(g_fpOverwritten.load(std::memory_order_relaxed)),
                    static_cast<unsigned long long>(g_fpReadbacks.load(std::memory_order_relaxed)),
                    static_cast<unsigned long long>(g_fpPatches.load(std::memory_order_relaxed)));
            }
            else
            {
                LOG("Halo 2 Anniversary first-person weapon projection: UNREADABLE "
                    "(record+0x4AC/+0x4EC)");
            }
        }
        // E-H2-18: the HUD replay, whenever the replay count moves.
        const uint64_t replays = g_hudReplays.load(std::memory_order_relaxed);
        if (g_hudCallbacks.load(std::memory_order_relaxed) &&
            replays != g_hudLoggedReplays)
        {
            g_hudLoggedReplays = replays;
            LOG("Halo 2 Anniversary HUD: host UI callback +0x%X (-> 0x960230(1) "
                "-> 0x7E1990 interface draw) fired %llu times; replayed per eye "
                "(eye 1 restored+drawn+recaptured, eye 0 restored+drawn+recaptured) on "
                "%llu frames (%llu of them mono: drawn once over the one picture), "
                "run once untouched on %llu (no complete pair), "
                "%llu interface failures; native gameplay HUD draws=%llu "
                "failures=%llu; last reason: %s",
                static_cast<unsigned>(kHalo2SaberHostUiCallbackRva),
                static_cast<unsigned long long>(g_hudCallbacks.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(replays),
                static_cast<unsigned long long>(g_hudMonoReplays.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(g_hudStockPasses.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(g_hudFailures.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_nativeChudDraws.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_nativeChudFailures.load(std::memory_order_relaxed)),
                g_hudLastReason);
        }
    }
}

    // Cold poll only: names the last fault the GameFault filters let through.
    // Only reached when the game (or the OS) survived it; otherwise the
    // first-chance record in HaloMCCVR-native-faults.log is the evidence.
    static void ReportPropagatedGameFaults(uintptr_t moduleBase, size_t moduleSize) noexcept
    {
        static uint64_t reported = 0;
        const uint64_t propagated =
            g_gameFaults.propagated.load(std::memory_order_acquire);
        if (propagated == reported)
            return;
        reported = propagated;
        if (!moduleBase)
        {
            moduleBase = g_moduleBase.load(std::memory_order_acquire);
            moduleSize = kHalo2RetailImageSize;
        }
        const FaultImageRange game{moduleBase, moduleSize};
        const FaultImageRange self = OwnImageFault_SelfRange();
        const auto describe = [&](uintptr_t address, char* out, size_t bytes)
        {
            if (FaultImageContains(game, address))
                _snprintf_s(out, bytes, _TRUNCATE, "halo2+0x%llX",
                    static_cast<unsigned long long>(address - game.base));
            else if (FaultImageContains(self, address))
                _snprintf_s(out, bytes, _TRUNCATE, "HaloMCCVR+0x%llX",
                    static_cast<unsigned long long>(address - self.base));
            else
                _snprintf_s(out, bytes, _TRUNCATE, "0x%llX",
                    static_cast<unsigned long long>(address));
        };
        const uintptr_t fault =
            g_gameFaults.lastFaultAddress.load(std::memory_order_relaxed);
        const uintptr_t foreign =
            g_gameFaults.lastForeignAddress.load(std::memory_order_relaxed);
        const unsigned code =
            g_gameFaults.lastCode.load(std::memory_order_relaxed);
        const unsigned long long contained =
            g_exceptions.load(std::memory_order_relaxed);
        char at[64]{}, frame[64]{};
        describe(fault, at, sizeof(at));
        describe(foreign, frame, sizeof(frame));
        if (foreign == fault)
        {
            LOG("H2A stereo: a fault in %s code at %s propagated (not "
                "contained); code=0x%08X, %llu propagated in total, %llu "
                "contained in HaloMCCVR code",
                FaultImageContains(game, fault) ? "game" : "non-HaloMCCVR",
                at, code, static_cast<unsigned long long>(propagated), contained);
        }
        else if (foreign)
        {
            LOG("H2A stereo: a fault in HaloMCCVR code at %s propagated (not "
                "contained): it ran inside a game frame at %s that the handler "
                "would have unwound; code=0x%08X, %llu propagated in total, "
                "%llu contained in HaloMCCVR code",
                at, frame, code, static_cast<unsigned long long>(propagated),
                contained);
        }
        else
        {
            LOG("H2A stereo: a fault in HaloMCCVR code at %s propagated (not "
                "contained): its frames up to the handler could not be proven "
                "HaloMCCVR's own; code=0x%08X, %llu propagated in total, %llu "
                "contained in HaloMCCVR code",
                at, code, static_cast<unsigned long long>(propagated), contained);
        }
    }

#include "halo2_anniversary_poll.inl"

bool Halo2AnniversaryStereo_Installed() noexcept
{
    return g_installed.load(std::memory_order_acquire);
}

void Halo2AnniversaryStereo_RequestRecovery(uint32_t generation) noexcept
{ g_manualRecoveryGeneration = generation; }

bool Halo2AnniversaryStereo_RecoveryPending() noexcept
{ return g_manualRecoveryGeneration != 0; }

bool Halo2AnniversaryStereo_Armed() noexcept
{
    return g_armed.load(std::memory_order_acquire);
}

bool Halo2AnniversaryStereo_ScopeAvailable() noexcept
{ return Halo2AnniversaryStereo_Armed()&&g_generation.load()!=0&&g_zoomFaultGeneration.load()!=g_generation.load(); }

uint32_t Halo2AnniversaryStereo_Generation() noexcept
{
    return g_generation.load(std::memory_order_acquire);
}

void Halo2AnniversaryStereo_RequestRecenter() noexcept
{
    g_recenterRequested.store(true, std::memory_order_release);
}

void Halo2AnniversaryStereo_ShutdownForVrFailure() noexcept
{
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    if (generation)
        g_vrFailureGeneration.store(generation, std::memory_order_release);
    g_armed.store(false, std::memory_order_release);
    g_teardown.store(true, std::memory_order_release);
}

#else

bool Halo2AnniversaryStereo_Poll(
    uintptr_t, size_t, uint32_t, bool, bool, bool, bool, uintptr_t) noexcept
{
    return false;
}
bool Halo2AnniversaryStereo_Installed() noexcept { return false; }
bool Halo2AnniversaryStereo_Armed() noexcept { return false; }
bool Halo2AnniversaryStereo_ScopeAvailable() noexcept { return false; }
uint32_t Halo2AnniversaryStereo_Generation() noexcept { return 0; }
void Halo2AnniversaryStereo_RequestRecenter() noexcept {}
void Halo2AnniversaryStereo_ShutdownForVrFailure() noexcept {}
void Halo2AnniversaryStereo_RequestRecovery(uint32_t) noexcept {}
bool Halo2AnniversaryStereo_RecoveryPending() noexcept { return false; }

#endif
