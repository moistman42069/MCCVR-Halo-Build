#include "../common/dlss_logic.h"
#include "halo2_stereo_core.h"
#include "hook_quiescence.h"
#include "../common/minhook_lifecycle.h"
#include "../common/manual_vr_recovery_logic.h"

#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <intrin.h>

#include "../common/config.h"
#include "../common/scope_logic.h"
#include "../common/halo2_hud_logic.h"
#include "../common/halo2_render_logic.h"
#include "../common/log.h"
#include "game.h"
#include "title_adapter.h"
#include "d3d11_hook.h"
#include "halo2_observer_6dof.h"
#include "vr.h"

#ifndef HALOMCCVR_HALO2_STEREO6DOF
#define HALOMCCVR_HALO2_STEREO6DOF 0
#endif

#if HALOMCCVR_HALO2_STEREO6DOF

#pragma intrinsic(_ReturnAddress)

namespace
{
    constexpr uint64_t kTelemetryPeriodMs = 2000;
    constexpr unsigned kCallbackDrainLimitMs = 2000;
    constexpr unsigned kOwnedCameraSpanCount = 8;
    constexpr uint32_t kHeadReferenceResetSerialMask = 0x3FFFFFFFu;
    constexpr uint32_t kHalo2RetailRenderViewReturnRva =
        kHalo2RetailRenderViewCallRva + 5;

    // E-H2-2: the final-output helper's unique load of
    // global_d3d_surface_backbuffer. The signed disp32 at +3 is based at +7.
    constexpr uint32_t kHalo2FinalOutputBackbufferLoadRva = 0x00975297;
    constexpr uint32_t kHalo2BackbufferRtvSlotRva = 0x0197EE58;
    constexpr char kHalo2BackbufferRtvLoadPattern[] =
        "48 8B 1D ?? ?? ?? ?? 48 89 B4 24 B0 00 00 00 48 8B CB "
        "0F 29 B4 24 90 00 00 00 0F 29 BC 24 80 00 00 00";

    static_assert(kHalo2RetailRenderViewReturnRva == 0x007E2417);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::atomic<uintptr_t>::is_always_lock_free);

    using Halo2RenderViewFn = void (__fastcall *)(
        uint32_t, void*, void*, uint8_t,
        uint32_t, uint32_t, uint8_t, float (*)[4],
        uint16_t, int32_t, uint32_t, uint8_t, uint32_t,
        void*, uint8_t, int16_t, void*, uint8_t, void*);
    using NativeChudDrawFn = void (__fastcall *)(
        int32_t, int32_t, const Halo2CameraRectangle*);
    using NativeHudAnchorBasisFn = void (__fastcall *)(
        int32_t, int32_t, float*, uint8_t);

    enum class CoreState : uint8_t
    {
        StockFallback = 0,
        CleanupRequired,
        Installed,
    };

    struct HookTelemetry
    {
        std::atomic<uint64_t> outerCallbacks{};
        std::atomic<uint64_t> innerCallbacks{};
        std::atomic<uint64_t> stockOuterCalls{};
        std::atomic<uint64_t> stockInnerCalls{};
        std::atomic<uint64_t> missingSnapshots{};
        std::atomic<uint64_t> foreignOuterCallers{};
        std::atomic<uint64_t> foreignInnerCallers{};
        std::atomic<uint64_t> invalidWindows{};
        std::atomic<uint64_t> duplicateSerials{};
        std::atomic<uint64_t> claimedPairs{};
        std::atomic<uint64_t> claimedInnerCalls{};
        std::atomic<uint64_t> renderedEyes{};
        std::atomic<uint64_t> completedPairs{};
        std::atomic<uint64_t> droppedPairs{};
        std::atomic<uint64_t> restoreFailures{};
        std::atomic<uint64_t> transactionExceptions{};
        std::atomic<uint64_t> posePublished{};
        std::atomic<uint64_t> poseRederived{};
        std::atomic<uint64_t> poseSelfTracked{};
        std::atomic<uint64_t> poseUnavailable{};
        // E-H2-12: per eye pass, the projection read back from the engine's
        // popped raster context. Unreadable means the crop used the written
        // cover instead; a mismatch means the engine rasterised a different
        // frustum than the cover the mod wrote, which the crop then follows.
        std::atomic<uint64_t> projectionReadbacks{};
        std::atomic<uint64_t> projectionUnreadable{};
        std::atomic<uint64_t> projectionMismatches{};
    };

    struct TelemetrySnapshot
    {
        uint64_t outerCallbacks = 0;
        uint64_t innerCallbacks = 0;
        uint64_t stockOuterCalls = 0;
        uint64_t stockInnerCalls = 0;
        uint64_t missingSnapshots = 0;
        uint64_t foreignOuterCallers = 0;
        uint64_t foreignInnerCallers = 0;
        uint64_t invalidWindows = 0;
        uint64_t duplicateSerials = 0;
        uint64_t claimedPairs = 0;
        uint64_t claimedInnerCalls = 0;
        uint64_t renderedEyes = 0;
        uint64_t completedPairs = 0;
        uint64_t droppedPairs = 0;
        uint64_t restoreFailures = 0;
        uint64_t transactionExceptions = 0;
        uint64_t posePublished = 0;
        uint64_t poseRederived = 0;
        uint64_t poseSelfTracked = 0;
        uint64_t poseUnavailable = 0;
        uint64_t projectionReadbacks = 0;
        uint64_t projectionUnreadable = 0;
        uint64_t projectionMismatches = 0;
    };

    struct CameraReadback
    {
        Halo2CameraBasis basis{};
        float verticalFov = 0.0f;
        Halo2CameraRectangle viewport{};
        uint8_t asymmetricEnable = 0;
        uint8_t pixelOffsetEnable = 0;
    };

    struct EyeCameraPair
    {
        Halo2CameraBasis render{};
        Halo2CameraBasis raster{};
    };

    struct HeadReference
    {
        float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        float position[3]{};
    };

    struct StereoScope
    {
        uint32_t generation = 0;
        uint32_t headReferenceResetSerial = 0;
        uint64_t serial = 0;
        bool rightEyeFirst = false;
        bool pairBegun = false;
        bool invalidated = false;
        bool outerReturned = false;
        bool pairCompleted = false;
        bool presentationClaimPublished = false;
        bool exceptionSeen = false;
        bool ownedStateRestoreFailed = false;
        bool rasterScopeCloseFailed = false;
        bool transactionShapeFailed = false;
        uint32_t exactInnerInvocations = 0;
        uint32_t renderViewCalls = 0;
        uint32_t renderViewReturns = 0;
        uint8_t completedEyeMask = 0;
        void* window = nullptr;
        void* renderCamera = nullptr;
        void* rasterCamera = nullptr;
        Halo2CameraBasis savedRender{};
        Halo2CameraBasis savedRaster{};
        float savedRenderVerticalFov = 0.0f;
        float savedRasterVerticalFov = 0.0f;
        float renderCoverVerticalFov = 0.0f;
        float rasterCoverVerticalFov = 0.0f;
        EyeCameraPair eyes[kHalo2EyeCount]{};
        Halo2SymmetricHalfFovs halfFovs{};
        // The frustum the engine actually rasterised each eye with, read
        // from its popped raster context after render_view returned.
        Halo2SymmetricHalfFovs engineHalfFovs[kHalo2EyeCount]{};
        bool engineHalfFovsValid[kHalo2EyeCount]{};
        // DLSS: the whole 4x4 the engine rasterised each eye with, from the
        // same popped raster context (the matrix whose [0]/[5] are the
        // scale reads above, +0x78 inside the context's projection block),
        // so the eye camera is published with the engine's own depth terms.
        float engineProjection[kHalo2EyeCount][16]{};
        bool engineProjectionValid[kHalo2EyeCount]{};
        bool dirty[kOwnedCameraSpanCount]{};
        // E-H2-29: the engine's once-per-frame scene-target latch as it stood
        // when this pair began, and whether it was readable.
        uint8_t sceneTargetLatch = 0;
        bool sceneTargetLatchValid = false;
        // E-H2-31: the tracked camera WITHOUT the per-eye offset. The
        // first-person weapon is drawn from this, so both eyes place it
        // identically - the classic equivalent of the Saber renderer's
        // view-WITHOUT-translation first-person pass.
        Halo2CameraBasis rasterCenter{};
        Halo2CameraBasis renderCenter{};
        bool rasterCenterValid = false;
    };

    HookTelemetry g_telemetry;
    TelemetrySnapshot g_lastTelemetry{};
    uint64_t g_lastTelemetryMs = 0;

    std::atomic<bool> g_installed{false};
    std::atomic<bool> g_armed{false};
    std::atomic<bool> g_stereoRequested{false};
    std::atomic<bool> g_presentationReady{false};
    std::atomic<bool> g_levelLive{false};
    std::atomic<bool> g_coldObservationPassed{false};
    std::atomic<bool> g_teardownRequested{false};
    std::atomic<bool> g_rightEyeFirst{false};
    std::atomic<uint32_t> g_generation{0};
    std::atomic<uint32_t> g_vrFailureGeneration{0};
    std::atomic<uintptr_t> g_moduleBase{0};
    std::atomic<uintptr_t> g_outerOriginalAddress{0};
    std::atomic<uintptr_t> g_innerOriginalAddress{0};
    std::atomic<uintptr_t> g_backbufferRtvSlotAddress{0};
    std::atomic<uint32_t> g_activeCallbacks{0};
    std::atomic<uint64_t> g_seenSerial{0};
    // Zero admits the first complete pair at any prepared serial so loading
    // remains stock/fail-open. Once a pair completes, every later stereo
    // transaction must consume exactly the next OpenXR prepared serial.
    std::atomic<uint64_t> g_lastCompletedPairSerial{0};
    std::atomic<uint64_t> g_serialGapExpected{};
    std::atomic<uint64_t> g_serialGapObserved{};
    // High 32 bits are the module generation; low 32 bits are the first
    // Halo2StereoQuarantineReason published for it. One atomic makes the
    // cross-thread reason/generation observation indivisible.
    std::atomic<uint64_t> g_runtimeQuarantine{0};

    // Low bits: 0 empty/old, 1 writer owns publication, 2 ready. The upper
    // bits are the module generation. Components are stored as atomic bit
    // patterns so a worker transition cannot race an ordinary float access.
    std::atomic<uint64_t> g_headReferenceState{0};
    std::atomic<uint32_t> g_headReferenceBits[7]{};
    std::atomic<uint32_t> g_headReferenceResetSerial{1};
    // Last projection read-back, for the telemetry line: written cover and
    // engine-held half-angles as float bits, and its status (0 none,
    // 1 agrees, 2 mismatch, 3 unreadable). Written on the render thread,
    // read by the reporter; a torn pair only misreports one line.
    std::atomic<uint32_t> g_projectionReadbackWrittenBits[2]{};
    std::atomic<uint32_t> g_projectionReadbackEngineBits[2]{};
    std::atomic<uint32_t> g_projectionReadbackStatus{0};
    // E-H2-24 (C-H2-31): the two render-camera positions written for the
    // last pair, as float bits, so the telemetry line states the eye
    // separation the engine was actually given (not the one intended).
    std::atomic<uint32_t> g_lastEyePositionBits[2][3]{};
    // E-H2-24 (C-H2-31): the window render camera the engine built for the
    // last pair against the observer pose the mod published - if the lean
    // never reaches the window camera, the classic world cannot follow it.
    std::atomic<uint32_t> g_lastWindowPositionBits[3]{};
    std::atomic<uint32_t> g_lastPublishedPositionBits[3]{};
    std::atomic<bool> g_lastPublishedValid{false};
    // E-H2-14: claimed frames dropped this generation (reset on install).
    std::atomic<uint32_t> g_claimedFrameFailures{0};
    // E-H2-29: how often the second eye's scene-target latch had to be
    // re-armed, and how often it could not be.
    // E-H2-31: the first-person weapon pass.
    void* g_firstPersonTarget = nullptr;
    std::atomic<uintptr_t> g_firstPersonOriginal{0};
    std::atomic<uint64_t> g_firstPersonCentred{0};
    std::atomic<uint64_t> g_firstPersonUnreadable{0};
    // E-H2-34: first-pass eyes rendered with no known previous pop (the
    // weapon keeps the stale camera's offset for that one pass).
    std::atomic<uint64_t> g_firstPersonUncompensated{0};
    // E-H2-35: read back after each eye's render_view, before the restore -
    // was the first-person FOV constant still the eye's cover when the pass
    // drew the weapon (held), or stock (lost)? Per eye.
    std::atomic<uint64_t> g_fpConstantHeld[kHalo2EyeCount]{};
    std::atomic<uint64_t> g_fpConstantLost[kHalo2EyeCount]{};
    std::atomic<uint64_t> g_fpConstantUnreadable{0};
    std::atomic<uint64_t> g_sceneLatchRearmed{0};
    std::atomic<uint64_t> g_sceneLatchUnreadable{0};
    uint64_t g_claimedFrameFailureLogMs = 0;
    uint32_t g_projectionReadbackLoggedStatus = 0;
    uint32_t g_projectionReadbackLoggedBits[4]{};

    HMODULE g_moduleReference = nullptr;
    // Stage 3I: a short-lived *owning* reference used only while the Classic
    // MinHook targets are being restored/removed. InstallCore intentionally
    // keeps g_moduleReference non-owning so MCC remains free to manage the
    // title DLL during normal play. The cleanup pin closes the unload race
    // proven by the Stage 3H headset log (MH_ERROR_MEMORY_PROTECT on both
    // Classic hook targets after level liveness closed).
    HMODULE g_cleanupModulePin = nullptr;
    void* g_outerTarget = nullptr;
    void* g_innerTarget = nullptr;
    CoreState g_coreState = CoreState::StockFallback;
    uint32_t g_rejectedGeneration = 0;
    uint32_t g_manualRecoveryGeneration = 0;
    uint32_t g_pinFailureLoggedGeneration = 0;
    // E-H2-20: the classic first-person FOV constant (see the header).
    std::atomic<uintptr_t> g_fpConstantAddress{0};
    std::atomic<uint32_t> g_fpConstantWrittenBits{0};
    std::atomic<uint64_t> g_fpConstantWrites{0};
    std::atomic<uint64_t> g_fpConstantRestores{0};
    std::atomic<uint64_t> g_fpConstantWriteFailures{0};
    std::atomic<bool> g_fpConstantPinned{false};
    uint32_t g_fpConstantLoggedBits = 0;

    // C-H2-77: renderer-independent ownership of the proven Blam CHUD draw.
    // The Stage 3D anchor-basis candidate was conclusively rejected after this
    // draw ran thousands of times while its alleged nested anchor routine ran
    // zero times. The anchor fields remain only for defensive cleanup of an
    // interrupted older install; this build never creates or enables that hook.
    HMODULE g_nativeHudModuleReference = nullptr;
    void* g_nativeChudTarget = nullptr;
    void* g_nativeHudAnchorTarget = nullptr;
    std::atomic<uintptr_t> g_nativeChudOriginal{0};
    std::atomic<uintptr_t> g_nativeHudAnchorOriginal{0};
    std::atomic<bool> g_nativeHudInstalled{false};
    std::atomic<bool> g_nativeHudArmed{false};
    std::atomic<bool> g_nativeHudTeardown{false};
    std::atomic<uint32_t> g_nativeHudGeneration{0};
    std::atomic<uintptr_t> g_nativeHudModuleBase{0};
    std::atomic<uint32_t> g_nativeHudActiveCallbacks{0};
    std::atomic<uint64_t> g_nativeHudScopedDraws{0};
    std::atomic<uint64_t> g_nativeHudStockDraws{0};
    std::atomic<uint64_t> g_nativeHudAnchorsAdjusted{0};
    std::atomic<uint64_t> g_nativeHudCrosshairsPreserved{0};
    std::atomic<uint64_t> g_nativeHudAnchorFailures{0};
    uint32_t g_nativeHudRejectedGeneration = 0;
    uint64_t g_nativeHudLastReportMs = 0;
    uint64_t g_nativeHudLastReportedTotal = UINT64_MAX;
    uint32_t g_nativeHudCrosshairPreparedGeneration = 0;
    uint32_t g_nativeHudCrosshairPreparationFailedGeneration = 0;

    struct NativeHudEyeContext
    {
        bool active = false;
        bool layoutActive = false;
        Halo2SymmetricHalfFovs cover{};
        Halo2CameraRectangle source{};
        Halo2CameraRectangle layout{};
    };
    thread_local NativeHudEyeContext g_nativeHudEyeContext{};

    template <size_t N>
    bool NativeHudEntryMatches(
        uintptr_t address, const uint8_t (&expected)[N]) noexcept
    {
        uint8_t entry[N]{};
        __try
        {
            std::memcpy(entry, reinterpret_cast<const void*>(address), N);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return std::memcmp(entry, expected, N) == 0;
    }

    bool BuildAdjustedNativeHudRectangle(
        const Halo2CameraRectangle* source,
        const Halo2SymmetricHalfFovs& cover,
        Halo2CameraRectangle& adjusted) noexcept
    {
        if (!source)
            return false;
        Halo2CameraRectangle input{};
        __try
        {
            std::memcpy(&input, source, sizeof(input));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        float eyeFov[2][4]{};
        if (!VR_GetEyeFov(0, eyeFov[0]) ||
            !VR_GetEyeFov(1, eyeFov[1]))
        {
            return false;
        }
        return halo2_hud::ComputeVisibleLayoutRectangle(
            input, cover, eyeFov[0], eyeFov[1],
            g_config.hud_size, g_config.hud_aspect,
            g_config.hud_vertical_offset, adjusted);
    }

    __declspec(noinline) void __fastcall NativeChudDrawDetour(
        int32_t player, int32_t drawSelector,
        const Halo2CameraRectangle* rectangle)
    {
        const auto original = reinterpret_cast<NativeChudDrawFn>(
            g_nativeChudOriginal.load(std::memory_order_acquire));
        if (!original)
            return;
        g_nativeHudActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            Halo2CameraRectangle source{};
            Halo2CameraRectangle layout{};
            if (g_nativeHudEyeContext.active &&
                g_nativeHudArmed.load(std::memory_order_acquire) &&
                !g_nativeHudTeardown.load(std::memory_order_acquire) &&
                drawSelector == -1 &&
                BuildAdjustedNativeHudRectangle(
                    rectangle, g_nativeHudEyeContext.cover, layout))
            {
                __try
                {
                    std::memcpy(&source, rectangle, sizeof(source));
                    g_nativeHudEyeContext.source = source;
                    g_nativeHudEyeContext.layout = layout;
                    g_nativeHudEyeContext.layoutActive = true;
                    // Preserve the engine's original CHUD call contract. The
                    // D3D11 hooks consume this TLS source/layout pair only for
                    // the exact field-proven HUD pixel shaders while original()
                    // is actively issuing their draws.
                    original(player, drawSelector, rectangle);
                    g_nativeHudScopedDraws.fetch_add(
                        1, std::memory_order_relaxed);
                }
                __finally
                {
                    g_nativeHudEyeContext.layoutActive = false;
                }
            }
            else
            {
                original(player, drawSelector, rectangle);
                g_nativeHudStockDraws.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        __finally
        {
            g_nativeHudActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall NativeHudAnchorBasisDetour(
        int32_t anchor, int32_t selector, float* output, uint8_t special)
    {
        const auto original = reinterpret_cast<NativeHudAnchorBasisFn>(
            g_nativeHudAnchorOriginal.load(std::memory_order_acquire));
        if (!original)
            return;
        g_nativeHudActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            original(anchor, selector, output, special);
            if (!g_nativeHudEyeContext.layoutActive || !output)
                __leave;
            if (anchor == halo2_hud::kCrosshairAnchor)
            {
                g_nativeHudCrosshairsPreserved.fetch_add(
                    1, std::memory_order_relaxed);
                __leave;
            }
            bool mapped = false;
            __try
            {
                float x = output[0];
                float y = output[1];
                mapped = halo2_hud::MapNativeAnchorPoint(
                    g_nativeHudEyeContext.source,
                    g_nativeHudEyeContext.layout, anchor, x, y);
                if (mapped)
                {
                    output[0] = x;
                    output[1] = y;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                mapped = false;
            }
            if (mapped)
                g_nativeHudAnchorsAdjusted.fetch_add(1, std::memory_order_relaxed);
            else
                g_nativeHudAnchorFailures.fetch_add(1, std::memory_order_relaxed);
        }
        __finally
        {
            g_nativeHudActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool WriteDwordGuarded(uintptr_t address, uint32_t value) noexcept
    {
        if (!address)
            return false;
        __try
        {
            *reinterpret_cast<volatile uint32_t*>(address) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ReadByteGuarded(uintptr_t address, uint8_t& value) noexcept
    {
        if (!address)
            return false;
        __try
        {
            value = *reinterpret_cast<const volatile uint8_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool WriteByteGuarded(uintptr_t address, uint8_t value) noexcept
    {
        if (!address)
            return false;
        __try
        {
            *reinterpret_cast<volatile uint8_t*>(address) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ReadDwordGuarded(uintptr_t address, uint32_t& value) noexcept
    {
        if (!address)
            return false;
        __try
        {
            value = *reinterpret_cast<const volatile uint32_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Pins the constant by its only reader and the stock value, and makes
    // its page writable. Anything that does not match leaves the weapon as
    // the engine draws it and says so once per generation.
    bool PinFirstPersonFovConstant(uintptr_t base, uint32_t generation) noexcept
    {
        g_fpConstantPinned.store(false, std::memory_order_release);
        g_fpConstantAddress.store(0, std::memory_order_release);
        const uintptr_t load = base + kHalo2ClassicFirstPersonFovLoadRva;
        const uintptr_t entry = base + kHalo2ClassicDrawFirstPersonRva;
        const uintptr_t constant = base + kHalo2ClassicFirstPersonFovConstantRva;
        uint8_t loadBytes[sizeof(kHalo2ClassicFirstPersonFovLoadBytes)]{};
        uint8_t entryBytes[sizeof(kHalo2ClassicDrawFirstPersonEntryBytes)]{};
        uint32_t stock = 0;
        __try
        {
            std::memcpy(loadBytes, reinterpret_cast<const void*>(load), sizeof(loadBytes));
            std::memcpy(entryBytes, reinterpret_cast<const void*>(entry), sizeof(entryBytes));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (std::memcmp(loadBytes, kHalo2ClassicFirstPersonFovLoadBytes, sizeof(loadBytes)) != 0 ||
            std::memcmp(entryBytes, kHalo2ClassicDrawFirstPersonEntryBytes, sizeof(entryBytes)) != 0 ||
            !ReadDwordGuarded(constant, stock) ||
            (stock != kHalo2ClassicFirstPersonFovStockBits &&
             stock != g_fpConstantWrittenBits.load(std::memory_order_relaxed)))
        {
            LOG("Halo 2 classic first-person FOV constant NOT pinned for generation "
                "%u: draw_first_person entry / its MOVSS / the stock value do not "
                "match the proven module; the weapon keeps the engine's 49.6 deg",
                generation);
            return false;
        }
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(constant), sizeof(uint32_t),
                            PAGE_READWRITE, &oldProtect))
        {
            LOG("Halo 2 classic first-person FOV constant NOT pinned: VirtualProtect "
                "failed (%lu)", GetLastError());
            return false;
        }
        g_fpConstantAddress.store(constant, std::memory_order_release);
        g_fpConstantPinned.store(true, std::memory_order_release);
        LOG("Halo 2 classic first-person FOV constant pinned: draw_first_person "
            "+0x%X loads %.3f deg from +0x%X (its only reader, MOVSS at +0x%X); "
            "while an eye pair renders it holds the eye's vertical cover so the "
            "weapon is drawn through the world's frustum, restored with the cameras",
            static_cast<unsigned>(kHalo2ClassicDrawFirstPersonRva),
            49.594f,
            static_cast<unsigned>(kHalo2ClassicFirstPersonFovConstantRva),
            static_cast<unsigned>(kHalo2ClassicFirstPersonFovLoadRva));
        return true;
    }

    bool WriteFirstPersonFovConstant(float verticalCoverRadians) noexcept
    {
        const uintptr_t address = g_fpConstantAddress.load(std::memory_order_acquire);
        if (!address || !g_fpConstantPinned.load(std::memory_order_acquire))
            return true;   // not pinned: the engine's own constant stays
        if (!std::isfinite(verticalCoverRadians) || verticalCoverRadians < 0.1f ||
            verticalCoverRadians > 3.0f)
            return false;
        uint32_t bits = 0;
        std::memcpy(&bits, &verticalCoverRadians, sizeof(bits));
        if (!WriteDwordGuarded(address, bits))
        {
            g_fpConstantWriteFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        g_fpConstantWrittenBits.store(bits, std::memory_order_relaxed);
        g_fpConstantWrites.fetch_add(1, std::memory_order_relaxed);
        if (bits != g_fpConstantLoggedBits)
        {
            g_fpConstantLoggedBits = bits;
            LOG("Halo 2 classic first-person weapon: FOV constant written %.1f deg "
                "(the eye's vertical cover) for every eye pass - read back per eye "
                "as fpFovHeld/fpFovLost in the stereo core line",
                verticalCoverRadians * 57.29578f);
        }
        return true;
    }

    bool RestoreFirstPersonFovConstant() noexcept
    {
        const uintptr_t address = g_fpConstantAddress.load(std::memory_order_acquire);
        if (!address)
            return true;
        if (!WriteDwordGuarded(address, kHalo2ClassicFirstPersonFovStockBits))
        {
            g_fpConstantWriteFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        g_fpConstantRestores.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    uint32_t g_remasteredNoticeGeneration = 0;

    thread_local StereoScope* g_stereoScope = nullptr;
    std::atomic<uint32_t> g_zoomFaultGeneration{0},g_zoomFaults{0};
    thread_local uint32_t g_suppressedOuterDepth = 0;
    thread_local uint32_t g_innerDepth = 0;
    // E-H2-34: the eye camera the last render_view on this thread popped -
    // the camera the classic first-person pass draws the NEXT pass from.
    thread_local Halo2CameraBasis g_lastPassEyeCamera{};
    thread_local bool g_lastPassEyeCameraValid = false;
    // E-H2-67 (C-H2-79): the eye whose render_view is running, so the draw
    // hook can name which camera it measured. -1 outside an eye pass.
    thread_local int g_currentPassEye = -1;
    // What the classic weapon pass was MEASURED to hold at draw time.
    std::atomic<uint64_t> g_fpCameraMeasured{0};
    std::atomic<uint64_t> g_fpCameraUnreadable{0};
    std::atomic<uint64_t> g_fpCameraMatchedCentre{0};
    std::atomic<uint64_t> g_fpCameraMatchedThisEye{0};
    std::atomic<uint64_t> g_fpCameraMatchedOtherEye{0};
    std::atomic<uint64_t> g_fpCameraMatchedNone{0};
    // E-H2-69: per-eye weapon parallax admitted / refused by the guard.
    std::atomic<uint64_t> g_fpDepthAdmitted{0};
    std::atomic<uint64_t> g_fpDepthRefused{0};
    // E-H2-70: angle between the camera the weapon's nodes were composed
    // against (the engine's packet-build camera for this renderer) and the
    // camera this renderer's first-person pass draws them from. Non-zero
    // means the weapon is tipped in this renderer alone. Millidegrees.
    std::atomic<uint32_t> g_fpPitchMilliDegrees{0};
    std::atomic<uint32_t> g_fpPitchMaxMilliDegrees{0};

    // Angle between two camera forward axes, published as millidegrees
    // (last and running maximum). Allocation-free; hot-path safe.
    void PublishCameraOrientationDelta(
        const Halo2CameraBasis& reference, const Halo2CameraBasis& actual,
        std::atomic<uint32_t>& last, std::atomic<uint32_t>& maximum) noexcept
    {
        float dot = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            dot += reference.forward[axis] * actual.forward[axis];
        if (!std::isfinite(dot))
            return;
        dot = std::clamp(dot, -1.0f, 1.0f);
        const float degrees = std::acos(dot) * 57.29578f;
        if (!std::isfinite(degrees) || degrees < 0.0f || degrees > 180.0f)
            return;
        const uint32_t milli = static_cast<uint32_t>(degrees * 1000.0f);
        last.store(milli, std::memory_order_relaxed);
        uint32_t previous = maximum.load(std::memory_order_relaxed);
        while (previous < milli &&
               !maximum.compare_exchange_weak(
                   previous, milli, std::memory_order_relaxed,
                   std::memory_order_relaxed))
        {
        }
    }
    // E-H2-31/E-H2-34: centring the camera globals inside draw_first_person
    // never moved the weapon (the pass draws from the previous render_view's
    // popped camera, not from the globals at draw time) and the Saber
    // first-person pass has a real eye separation anyway. Disabled, not
    // deleted; the hook stays pinned.
    constexpr bool kHalo2ClassicCentreFirstPersonCameras = false;
    // C-H2-78 (E-H2-66): the camera the classic first-person pass actually
    // draws from is ONE common camera for both eyes - the tracked centre -
    // never a written per-eye camera. C-H2-40's eye pictures measured it
    // (weapon 0 px between the eyes while the world carried parallax), and
    // the 2026-08-31 Steam parity run measured what the E-H2-34
    // previous-eye model does to the compensation: every stable Classic
    // sample showed the weapon box at -16 px between the eyes against the
    // world band's / Anniversary's steady -8 px - exactly doubled
    // disparity, each pass over-shifted by half the eye baseline, the
    // whole hands/gun rig fusing at half its true distance ("barrel too
    // high"). With this ON, both passes compensate correct eye -> centre,
    // so the common camera's image is exactly this eye's image and Classic
    // places the rig where Anniversary does. OFF restores the E-H2-34
    // previous-eye model (disabled, not deleted).
    constexpr bool kHalo2ClassicCommonFirstPersonCamera = true;
    // C-H2-80 (E-H2-68): the user's requirement is that Halo 2 Classic show
    // the hands and gun at VISUALLY the same position and rotation as
    // Anniversary. Both renderers already consume the SAME final packet
    // (E-H2-45); Anniversary draws it untouched, while Classic alone then
    // applied this per-eye compensation - a rotation AND a translation of
    // the whole rig. Any such transform makes Classic differ from
    // Anniversary by construction, so no choice of viewing camera can
    // deliver parity; only applying none can. With this false,
    // BeginClassicFirstPersonEye finds no compensating pass and leaves the
    // packet exactly as the shared mount produced it, which is the same
    // geometry Anniversary draws - parity by construction, not by tuning.
    //
    // Known cost, stated plainly: this is C-H2-40's classic behavior, where
    // the weapon carried no per-eye disparity of its own (it is drawn from
    // whatever single camera the pass holds - see the fpCamIs* counters).
    // Placement parity is the user's explicit priority over that depth.
    // The complete compensation implementation, its measured viewing camera
    // and its telemetry all remain compiled and inert.
    constexpr bool kHalo2ClassicFirstPersonEyeCompensation = false;
    // C-H2-81 (E-H2-69): the user asked for that depth back on top of
    // parity. The compensation is the only mechanism that can produce it
    // (camera-global writes at draw time were measured inert - E-H2-31/34;
    // a projection change cannot create parallax), so it is admitted again
    // - but only in the shape that provably cannot displace or rotate the
    // rig: the measured viewing camera must share the eye's orientation
    // exactly, and sit within one live half-IPD of it. Every other case
    // keeps C-H2-80's byte-identical Anniversary parity for that pass.
    constexpr bool kHalo2ClassicFirstPersonEyeDepth = true;

    void ResetHeadReferenceAtomic() noexcept
    {
        g_headReferenceResetSerial.fetch_add(1, std::memory_order_acq_rel);
        g_headReferenceState.store(0, std::memory_order_release);
    }

    Halo2RenderPlayerWindowFn OriginalPlayerWindow() noexcept
    {
        return reinterpret_cast<Halo2RenderPlayerWindowFn>(
            g_outerOriginalAddress.load(std::memory_order_acquire));
    }

    Halo2RenderViewFn OriginalRenderView() noexcept
    {
        return reinterpret_cast<Halo2RenderViewFn>(
            g_innerOriginalAddress.load(std::memory_order_acquire));
    }

    uint32_t FloatBits(float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    float BitsFloat(uint32_t bits) noexcept
    {
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool NormalizeQuaternion(const float input[4], float output[4]) noexcept
    {
        if (!input || !output)
            return false;
        float lengthSquared = 0.0f;
        for (int component = 0; component < 4; ++component)
        {
            if (!std::isfinite(input[component]))
                return false;
            lengthSquared += input[component] * input[component];
        }
        if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f)
            return false;
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        for (int component = 0; component < 4; ++component)
            output[component] = input[component] * inverseLength;
        return true;
    }

    bool ValidHeadPose(
        const float orientation[4], const float position[3]) noexcept
    {
        float normalized[4]{};
        if (!NormalizeQuaternion(orientation, normalized) || !position)
            return false;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(position[axis]))
                return false;
        return true;
    }

    bool AcquireHeadReference(
        uint32_t generation, const float orientation[4],
        const float position[3], HeadReference& output,
        uint32_t& outputResetSerial) noexcept
    {
        if (!generation || !ValidHeadPose(orientation, position))
            return false;

        constexpr uint64_t kPublishing = 1;
        constexpr uint64_t kReady = 2;
        const uint32_t resetSerial =
            g_headReferenceResetSerial.load(std::memory_order_acquire) &
            kHeadReferenceResetSerialMask;
        const uint64_t referenceKey =
            (static_cast<uint64_t>(generation) << 32) |
            (static_cast<uint64_t>(resetSerial) << 2);
        const uint64_t publishing = referenceKey | kPublishing;
        const uint64_t ready = referenceKey | kReady;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            uint64_t state =
                g_headReferenceState.load(std::memory_order_acquire);
            if (state == ready)
            {
                HeadReference candidate{};
                for (int component = 0; component < 4; ++component)
                {
                    candidate.orientation[component] = BitsFloat(
                        g_headReferenceBits[component].load(
                            std::memory_order_relaxed));
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    candidate.position[axis] = BitsFloat(
                        g_headReferenceBits[4 + axis].load(
                            std::memory_order_relaxed));
                }
                if (g_headReferenceState.load(std::memory_order_acquire) !=
                        ready ||
                    (g_headReferenceResetSerial.load(
                         std::memory_order_acquire) &
                     kHeadReferenceResetSerialMask) !=
                        resetSerial ||
                    !ValidHeadPose(
                        candidate.orientation, candidate.position))
                {
                    continue;
                }
                output = candidate;
                outputResetSerial = resetSerial;
                return true;
            }
            if ((state & 3u) == kPublishing)
                return false;
            if (!g_headReferenceState.compare_exchange_strong(
                    state, publishing, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                continue;
            }

            float normalized[4]{};
            NormalizeQuaternion(orientation, normalized);
            for (int component = 0; component < 4; ++component)
            {
                g_headReferenceBits[component].store(
                    FloatBits(normalized[component]),
                    std::memory_order_relaxed);
                output.orientation[component] = normalized[component];
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                g_headReferenceBits[4 + axis].store(
                    FloatBits(position[axis]), std::memory_order_relaxed);
                output.position[axis] = position[axis];
            }
            if ((g_headReferenceResetSerial.load(
                     std::memory_order_acquire) &
                 kHeadReferenceResetSerialMask) !=
                resetSerial)
            {
                uint64_t expected = publishing;
                (void)g_headReferenceState.compare_exchange_strong(
                    expected, 0, std::memory_order_acq_rel,
                    std::memory_order_acquire);
                return false;
            }
            uint64_t expected = publishing;
            if (!g_headReferenceState.compare_exchange_strong(
                    expected, ready, std::memory_order_release,
                    std::memory_order_acquire) ||
                (g_headReferenceResetSerial.load(
                     std::memory_order_acquire) &
                 kHeadReferenceResetSerialMask) != resetSerial ||
                g_headReferenceState.load(std::memory_order_acquire) != ready)
            {
                return false;
            }
            outputResetSerial = resetSerial;
            return true;
        }
        return false;
    }

    // E-H2-6: the classic window cameras are BUILT from the observer result
    // record (0x960230 -> 0x6F0E60 -> 0x960780 -> 0x7DF5A0). When the observer
    // core wrote the head pose into that record this frame, the camera in
    // hand is already the tracked centre and applying the head delta again
    // doubles it (87.0 observer poses/s against 87.6 window callbacks/s,
    // 1:1, on 2026-08-21). When the observer skipped the frame, or the
    // engine substituted a non-observer camera, the camera in hand is NOT
    // tracked and this core must track it itself. The publication decides.
    bool ResolveTrackedCenter(
        const Halo2CameraBasis& stock, const HeadReference& reference,
        const Halo2SynchronousVrRenderSnapshot& snapshot,
        uint32_t generation, bool published,
        const Halo2ObserverPosePublication& publication,
        Halo2CameraBasis& center) noexcept
    {
        if (!snapshot.headPoseValid || !Halo2ValidateCameraBasis(stock))
            return false;
        Halo2TrackedHeadInput head{};
        std::memcpy(
            head.orientation, snapshot.headOrientation,
            sizeof(head.orientation));
        std::memcpy(
            head.position, snapshot.headPosition,
            sizeof(head.position));
        head.positional = Game_IsPositionalTracking();
        head.worldScale = Game_GetWorldScale();

        switch (Halo2SelectPoseOwner(
            published, publication.generation, publication.serial,
            publication.tracked, stock, generation,
            snapshot.preparedSerial))
        {
        case Halo2PoseOwnerDecision::UsePublishedTracked:
            g_telemetry.posePublished.fetch_add(1, std::memory_order_relaxed);
            center = stock;
            return true;
        case Halo2PoseOwnerDecision::RederiveFromPublishedStock:
            g_telemetry.poseRederived.fetch_add(1, std::memory_order_relaxed);
            std::memcpy(
                head.referenceOrientation, publication.referenceOrientation,
                sizeof(head.referenceOrientation));
            std::memcpy(
                head.referencePosition, publication.referencePosition,
                sizeof(head.referencePosition));
            return Halo2BuildTrackedCenterCamera(
                Halo2ControllerReference(publication), head, center);
        case Halo2PoseOwnerDecision::SelfTrack:
            g_telemetry.poseSelfTracked.fetch_add(1, std::memory_order_relaxed);
            std::memcpy(
                head.referenceOrientation, reference.orientation,
                sizeof(head.referenceOrientation));
            std::memcpy(
                head.referencePosition, reference.position,
                sizeof(head.referencePosition));
            return Halo2BuildTrackedCenterCamera(stock, head, center);
        case Halo2PoseOwnerDecision::NoPose:
        default:
            g_telemetry.poseUnavailable.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    bool BuildEyeCamera(
        const Halo2CameraBasis& trackedCenter,
        const Halo2SynchronousVrRenderSnapshot& snapshot, int eye,
        Halo2CameraBasis& output) noexcept
    {
        if (eye < kHalo2LeftEye || eye > kHalo2RightEye ||
            !snapshot.headPoseValid ||
            !Halo2ValidateCameraBasis(trackedCenter))
        {
            return false;
        }
        return Halo2BuildSynchronousEyeCamera(
            trackedCenter, snapshot.eyes[eye].position,
            snapshot.eyes[eye].orientation, output, Game_GetWorldScale());
    }

    bool DeriveRuntimeCover(
        const Halo2SynchronousVrRenderSnapshot& snapshot,
        const Halo2CameraRectangle& rectangle,
        float& verticalFov, Halo2SymmetricHalfFovs& halfFovs) noexcept
    {
        constexpr float kMaximumFovAngle = 1.56f;
        const int width = static_cast<int>(rectangle.x1) - rectangle.x0;
        const int height = static_cast<int>(rectangle.y1) - rectangle.y0;
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        float requiredTanX = 0.0f;
        float requiredTanY = 0.0f;
        for (int eye = 0; eye < kHalo2EyeCount; ++eye)
        {
            const Halo2SynchronousVrEyeSnapshot& view = snapshot.eyes[eye];
            if (!view.fovValid || !(view.fov[0] < 0.0f) ||
                !(view.fov[1] > 0.0f) || !(view.fov[2] > 0.0f) ||
                !(view.fov[3] < 0.0f))
            {
                return false;
            }
            for (float angle : view.fov)
            {
                if (!std::isfinite(angle) ||
                    std::fabs(angle) >= kMaximumFovAngle)
                {
                    return false;
                }
            }
            requiredTanX = (std::max)(requiredTanX,
                (std::max)(std::fabs(std::tan(view.fov[0])),
                           std::fabs(std::tan(view.fov[1]))));
            requiredTanY = (std::max)(requiredTanY,
                (std::max)(std::fabs(std::tan(view.fov[2])),
                           std::fabs(std::tan(view.fov[3]))));
        }

        Halo2SymmetricHalfFovs stockHalfFovs{};
        // The title's builder defines horizontal extent from vertical FOV and
        // this rectangle. Derive its tangent aspect through the proven helper
        // instead of assuming that pixel aspect and tangent aspect coincide.
        if (!Halo2DeriveSymmetricHalfFovs(
                1.0f, rectangle, stockHalfFovs))
        {
            return false;
        }
        const float stockTanY = std::tan(stockHalfFovs.vertical);
        const float stockTanX = std::tan(stockHalfFovs.horizontal);
        const float tangentAspect = stockTanX / stockTanY;
        if (!std::isfinite(tangentAspect) || tangentAspect <= 1.0e-4f)
            return false;
        constexpr float kCoverMargin = 1.002f;
        const float verticalTangent = (std::max)(
            requiredTanY, requiredTanX / tangentAspect) * kCoverMargin;
        const float candidateFov = 2.0f * std::atan(verticalTangent);
        Halo2SymmetricHalfFovs candidateHalfFovs{};
        if (!std::isfinite(candidateFov) ||
            !Halo2DeriveSymmetricHalfFovs(
                candidateFov, rectangle, candidateHalfFovs))
        {
            return false;
        }
        const float actualTanX = std::tan(candidateHalfFovs.horizontal);
        const float actualTanY = std::tan(candidateHalfFovs.vertical);
        constexpr float kCoverEpsilon = 1.0e-4f;
        if (actualTanX + kCoverEpsilon < requiredTanX ||
            actualTanY + kCoverEpsilon < requiredTanY)
        {
            return false;
        }
        verticalFov = candidateFov;
        halfFovs = candidateHalfFovs;
        return true;
    }

    bool ReadPlayerIndex(void* window, int32_t& playerIndex) noexcept
    {
        if (!window)
            return false;
        __try
        {
            std::memcpy(
                &playerIndex,
                static_cast<const uint8_t*>(window) +
                    kHalo2WindowPlayerIndexOffset,
                sizeof(playerIndex));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadCamera(
        void* window, uint32_t cameraOffset, CameraReadback& output) noexcept
    {
        if (!window || reinterpret_cast<uintptr_t>(window) >
                UINTPTR_MAX - kHalo2RetailWindowStride)
        {
            return false;
        }
        CameraReadback candidate{};
        __try
        {
            const auto* camera = static_cast<const uint8_t*>(window) +
                cameraOffset;
            std::memcpy(
                &candidate.basis, camera + kHalo2CameraPositionOffset,
                sizeof(candidate.basis));
            std::memcpy(
                &candidate.verticalFov,
                camera + kHalo2CameraVerticalFovOffset,
                sizeof(candidate.verticalFov));
            std::memcpy(
                &candidate.viewport,
                camera + kHalo2CameraViewportRectangleOffset,
                sizeof(candidate.viewport));
            std::memcpy(
                &candidate.asymmetricEnable,
                camera + kHalo2CameraAsymmetricEnableOffset,
                sizeof(candidate.asymmetricEnable));
            std::memcpy(
                &candidate.pixelOffsetEnable,
                camera + kHalo2CameraPixelOffsetEnableOffset,
                sizeof(candidate.pixelOffsetEnable));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        output = candidate;
        return true;
    }

    bool GuardedCopyExact(
        void* destination, const void* source, size_t bytes) noexcept
    {
        if (!destination || !source ||
            (bytes != kHalo2CameraVectorBytes && bytes != sizeof(float)))
            return false;
        __try
        {
            std::memcpy(destination, source, bytes);
            return std::memcmp(destination, source, bytes) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    uint32_t OwnedSpanRelativeOffset(unsigned index) noexcept
    {
        const uint32_t cameraOffset = index < 4
            ? kHalo2RenderCameraOffset
            : kHalo2RasterCameraOffset;
        const unsigned fieldIndex = index % 4;
        const uint32_t vectorOffset = fieldIndex == 0
            ? kHalo2CameraPositionOffset
            : (fieldIndex == 1
                   ? kHalo2CameraForwardOffset
                   : (fieldIndex == 2
                          ? kHalo2CameraUpOffset
                          : kHalo2CameraVerticalFovOffset));
        return cameraOffset + vectorOffset;
    }

    void* OwnedSpanAddress(const StereoScope& scope, unsigned index) noexcept
    {
        return static_cast<uint8_t*>(scope.window) +
            OwnedSpanRelativeOffset(index);
    }

    const float* SavedSpan(const StereoScope& scope, unsigned index) noexcept
    {
        const Halo2CameraBasis& camera =
            index < 4 ? scope.savedRender : scope.savedRaster;
        switch (index % 4)
        {
        case 0: return camera.position;
        case 1: return camera.forward;
        case 2: return camera.up;
        default:
            return index < 4
                ? &scope.savedRenderVerticalFov
                : &scope.savedRasterVerticalFov;
        }
    }

    const float* EyeSpan(
        const StereoScope& scope, int eye, unsigned index) noexcept
    {
        const Halo2CameraBasis& camera =
            index < 4 ? scope.eyes[eye].render : scope.eyes[eye].raster;
        switch (index % 4)
        {
        case 0: return camera.position;
        case 1: return camera.forward;
        case 2: return camera.up;
        default:
            return index < 4
                ? &scope.renderCoverVerticalFov
                : &scope.rasterCoverVerticalFov;
        }
    }

    size_t OwnedSpanBytes(unsigned index) noexcept
    {
        return index % 4 == 3
            ? sizeof(float)
            : kHalo2CameraVectorBytes;
    }

    bool OwnedSpanAllowed(unsigned index) noexcept
    {
        Halo2CameraPoseWrite expected = Halo2CameraPoseWrite::Reject;
        switch (index)
        {
        case 0: expected = Halo2CameraPoseWrite::RenderPosition; break;
        case 1: expected = Halo2CameraPoseWrite::RenderForward; break;
        case 2: expected = Halo2CameraPoseWrite::RenderUp; break;
        case 3: expected = Halo2CameraPoseWrite::RenderVerticalFov; break;
        case 4: expected = Halo2CameraPoseWrite::RasterPosition; break;
        case 5: expected = Halo2CameraPoseWrite::RasterForward; break;
        case 6: expected = Halo2CameraPoseWrite::RasterUp; break;
        case 7: expected = Halo2CameraPoseWrite::RasterVerticalFov; break;
        default: return false;
        }
        return SelectHalo2CameraPoseWrite(
                   OwnedSpanRelativeOffset(index), OwnedSpanBytes(index)) ==
            expected;
    }

    bool RestoreOwnedSpans(StereoScope& scope) noexcept
    {
        bool restored = RestoreFirstPersonFovConstant();
        for (unsigned index = 0; index < kOwnedCameraSpanCount; ++index)
        {
            if (!scope.dirty[index])
                continue;
            if (!OwnedSpanAllowed(index))
            {
                restored = false;
                continue;
            }
            const bool spanRestored = GuardedCopyExact(
                OwnedSpanAddress(scope, index), SavedSpan(scope, index),
                OwnedSpanBytes(index));
            if (spanRestored)
                scope.dirty[index] = false;
            else
                restored = false;
        }
        if (!restored)
        {
            scope.ownedStateRestoreFailed = true;
            g_telemetry.restoreFailures.fetch_add(1, std::memory_order_relaxed);
        }
        return restored;
    }

    bool WriteEyeSpans(StereoScope& scope, int eye) noexcept
    {
        // E-H2-35 (C-H2-40): RestoreOwnedSpans runs after EVERY eye pass and
        // puts the first-person FOV constant back to the stock 49.6 deg, but
        // the constant was only written once per pair (WriteOuterCoverFovs).
        // Since C-H2-27 the second eye's weapon has therefore been drawn 3.1x
        // the size of the first eye's - one giant gun, one normal, in the
        // same pair. The constant is written for every eye, like the cover.
        if (!WriteFirstPersonFovConstant(scope.rasterCoverVerticalFov))
        {
            RestoreOwnedSpans(scope);
            return false;
        }
        for (unsigned index = 0; index < kOwnedCameraSpanCount; ++index)
        {
            if (!OwnedSpanAllowed(index))
                return false;
            // Mark before the guarded write. Even a partial fault is followed
            // by an independent restore attempt for this and every prior span.
            scope.dirty[index] = true;
            if (!GuardedCopyExact(
                    OwnedSpanAddress(scope, index), EyeSpan(scope, eye, index),
                    OwnedSpanBytes(index)))
            {
                RestoreOwnedSpans(scope);
                return false;
            }
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &scope.eyes[eye].render.position[axis], sizeof(bits));
            g_lastEyePositionBits[eye][axis].store(bits, std::memory_order_relaxed);
        }
        return true;
    }

    bool WriteOuterCoverFovs(StereoScope& scope) noexcept
    {
        constexpr unsigned kRenderFovSpan = 3;
        constexpr unsigned kRasterFovSpan = 7;
        const unsigned spans[2] = {kRenderFovSpan, kRasterFovSpan};
        // E-H2-20: the first-person weapon's FOV follows the raster cover.
        if (!WriteFirstPersonFovConstant(scope.rasterCoverVerticalFov))
        {
            RestoreOwnedSpans(scope);
            return false;
        }
        for (unsigned index : spans)
        {
            if (!OwnedSpanAllowed(index))
                return false;
            scope.dirty[index] = true;
            if (!GuardedCopyExact(
                    OwnedSpanAddress(scope, index), EyeSpan(scope, 0, index),
                    OwnedSpanBytes(index)))
            {
                RestoreOwnedSpans(scope);
                return false;
            }
        }
        return true;
    }

    bool ClaimSerial(uint64_t serial) noexcept
    {
        if (!serial)
            return false;
        uint64_t observed = g_seenSerial.load(std::memory_order_acquire);
        while (observed < serial)
        {
            if (g_seenSerial.compare_exchange_weak(
                    observed, serial, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return true;
            }
        }
        return false;
    }

    uint64_t PackRuntimeQuarantine(
        uint32_t generation, Halo2StereoQuarantineReason reason) noexcept
    {
        return (static_cast<uint64_t>(generation) << 32) |
            static_cast<uint32_t>(reason);
    }

    uint32_t RuntimeQuarantineGeneration(uint64_t packed) noexcept
    {
        return static_cast<uint32_t>(packed >> 32);
    }

    Halo2StereoQuarantineReason RuntimeQuarantineReason(
        uint64_t packed) noexcept
    {
        return static_cast<Halo2StereoQuarantineReason>(
            static_cast<uint32_t>(packed));
    }

    bool QuarantineReasonIsKnown(
        Halo2StereoQuarantineReason reason) noexcept
    {
        switch (reason)
        {
        case Halo2StereoQuarantineReason::CoreClaimedTransactionFailed:
        case Halo2StereoQuarantineReason::CorePreparedSerialGap:
        case Halo2StereoQuarantineReason::VrClaimedPresentationUnavailable:
        case Halo2StereoQuarantineReason::VrClaimedPairUnavailable:
        case Halo2StereoQuarantineReason::VrClaimedSwapchainFailed:
        case Halo2StereoQuarantineReason::VrClaimedProjectionFailed:
        case Halo2StereoQuarantineReason::VrClaimedSubmissionFailed:
            return true;
        default:
            return false;
        }
    }

    const char* QuarantineReasonName(
        Halo2StereoQuarantineReason reason) noexcept
    {
        switch (reason)
        {
        case Halo2StereoQuarantineReason::CoreClaimedTransactionFailed:
            return "CoreClaimedTransactionFailed";
        case Halo2StereoQuarantineReason::CorePreparedSerialGap:
            return "CorePreparedSerialGap";
        case Halo2StereoQuarantineReason::VrClaimedPresentationUnavailable:
            return "VrClaimedPresentationUnavailable";
        case Halo2StereoQuarantineReason::VrClaimedPairUnavailable:
            return "VrClaimedPairUnavailable";
        case Halo2StereoQuarantineReason::VrClaimedSwapchainFailed:
            return "VrClaimedSwapchainFailed";
        case Halo2StereoQuarantineReason::VrClaimedProjectionFailed:
            return "VrClaimedProjectionFailed";
        case Halo2StereoQuarantineReason::VrClaimedSubmissionFailed:
            return "VrClaimedSubmissionFailed";
        default:
            return "UnknownQuarantineReason";
        }
    }

    void PublishRuntimeQuarantine(
        uint32_t generation, Halo2StereoQuarantineReason reason) noexcept
    {
        if (!generation || !QuarantineReasonIsKnown(reason) ||
            g_generation.load(std::memory_order_acquire) != generation)
        {
            return;
        }

        const uint64_t desired = PackRuntimeQuarantine(generation, reason);
        uint64_t observed =
            g_runtimeQuarantine.load(std::memory_order_acquire);
        for (;;)
        {
            if (RuntimeQuarantineGeneration(observed) == generation)
                return;
            if (g_generation.load(std::memory_order_acquire) != generation)
                return;
            if (g_runtimeQuarantine.compare_exchange_weak(
                    observed, desired, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                // HotStateMatches also reads the packed request directly. This
                // store makes the ordinary admission gate close immediately as
                // well, without waiting for worker teardown.
                g_stereoRequested.store(false, std::memory_order_release);
                return;
            }
        }
    }

    void InvalidateScope(StereoScope& scope) noexcept
    {
        if (scope.invalidated)
            return;
        scope.invalidated = true;
        scope.pairCompleted = false;
        if (scope.generation && scope.serial)
        {
            __try
            {
                VR_Halo2InvalidateSynchronousPair(
                    scope.generation, scope.serial);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                scope.exceptionSeen = true;
                g_telemetry.transactionExceptions.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
    }

    // E-H2-31: draw_first_person copies the pushed raster camera (0x1996A28)
    // into its own copy, rebuilds the projection from it and draws the weapon.
    // With a true per-eye position there, a weapon a few centimetres from the
    // eye gets a disparity far past what the eyes can fuse - the classic
    // "double vision on the gun". The Saber renderer never has it because its
    // first-person pass uses a view WITHOUT translation, so the weapon has no
    // eye separation at all. This gives the classic pass the same: the eye's
    // rotation, the pair's centre position. The engine restores its own copy;
    // the mod restores what it overwrote either way.
    __declspec(noinline) void __fastcall Halo2DrawFirstPersonDetour()
    {
        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const auto original = reinterpret_cast<void(__fastcall*)()>(
            g_firstPersonOriginal.load(std::memory_order_acquire));
        if (!original)
        {
            g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        StereoScope* const scope = g_stereoScope;
        const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
        // Both camera globals, whole basis. C-H2-36 centred the position of
        // the raster camera alone and the weapon's disparity did not move, so
        // the weapon is placed from the other global; with both carrying the
        // pair's centre the two eyes run the pass with byte-identical camera
        // state and the weapon cannot differ between them.
        struct Centred
        {
            uintptr_t address = 0;
            float saved[9]{};
            bool active = false;
        };
        Centred owned[2];
        bool centred = false;
        if (kHalo2ClassicCentreFirstPersonCameras && scope &&
            scope->rasterCenterValid && base && g_innerDepth)
        {
            const uintptr_t globals[2] = {
                base + kHalo2ClassicFirstPersonCameraGlobalRva,
                base + kHalo2ClassicRenderCameraGlobalRva};
            const Halo2CameraBasis* const centres[2] = {
                &scope->rasterCenter, &scope->renderCenter};
            for (int index = 0; index < 2; ++index)
            {
                float replacement[9]{};
                std::memcpy(replacement, centres[index]->position, sizeof(float) * 3);
                std::memcpy(replacement + 3, centres[index]->forward, sizeof(float) * 3);
                std::memcpy(replacement + 6, centres[index]->up, sizeof(float) * 3);
                Centred& entry = owned[index];
                entry.address = globals[index];
                const uintptr_t position = entry.address + kHalo2CameraPositionOffset;
                const uintptr_t forward = entry.address + kHalo2CameraForwardOffset;
                const uintptr_t up = entry.address + kHalo2CameraUpOffset;
                if (GuardedCopyExact(entry.saved,
                                     reinterpret_cast<const float*>(position),
                                     kHalo2CameraVectorBytes) &&
                    GuardedCopyExact(entry.saved + 3,
                                     reinterpret_cast<const float*>(forward),
                                     kHalo2CameraVectorBytes) &&
                    GuardedCopyExact(entry.saved + 6,
                                     reinterpret_cast<const float*>(up),
                                     kHalo2CameraVectorBytes) &&
                    GuardedCopyExact(reinterpret_cast<void*>(position),
                                     replacement, kHalo2CameraVectorBytes) &&
                    GuardedCopyExact(reinterpret_cast<void*>(forward),
                                     replacement + 3, kHalo2CameraVectorBytes) &&
                    GuardedCopyExact(reinterpret_cast<void*>(up),
                                     replacement + 6, kHalo2CameraVectorBytes))
                {
                    entry.active = true;
                    centred = true;
                }
                else
                {
                    g_firstPersonUnreadable.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (centred)
                g_firstPersonCentred.fetch_add(1, std::memory_order_relaxed);
        }
        // E-H2-67 (C-H2-79): READ the camera this pass will draw the weapon
        // from, instead of predicting it. draw_first_person copies the
        // first-person camera global whole (E-H2-20/E-H2-31), so its
        // position/forward/up at entry IS the pass's camera. Publishing it
        // makes the compensation exact whichever camera the engine left
        // there - centre, an eye, or a stale one - and the counters below
        // record which of those it matched, as evidence rather than theory.
        if (scope && g_innerDepth && base)
        {
            Halo2CameraBasis measured{};
            const uintptr_t camera =
                base + kHalo2ClassicFirstPersonCameraGlobalRva;
            if (GuardedCopyExact(
                    measured.position,
                    reinterpret_cast<const float*>(
                        camera + kHalo2CameraPositionOffset),
                    kHalo2CameraVectorBytes) &&
                GuardedCopyExact(
                    measured.forward,
                    reinterpret_cast<const float*>(
                        camera + kHalo2CameraForwardOffset),
                    kHalo2CameraVectorBytes) &&
                GuardedCopyExact(
                    measured.up,
                    reinterpret_cast<const float*>(
                        camera + kHalo2CameraUpOffset),
                    kHalo2CameraVectorBytes) &&
                Halo2ValidateCameraBasis(measured))
            {
                const int eye = g_currentPassEye;
                const bool eyeKnown = eye == 0 || eye == 1;
                if (Halo2CameraBasesNearlyEqual(measured, scope->renderCenter))
                    g_fpCameraMatchedCentre.fetch_add(
                        1, std::memory_order_relaxed);
                else if (eyeKnown && Halo2CameraBasesNearlyEqual(
                             measured, scope->eyes[eye].render))
                    g_fpCameraMatchedThisEye.fetch_add(
                        1, std::memory_order_relaxed);
                else if (eyeKnown && Halo2CameraBasesNearlyEqual(
                             measured, scope->eyes[1 - eye].render))
                    g_fpCameraMatchedOtherEye.fetch_add(
                        1, std::memory_order_relaxed);
                else
                    g_fpCameraMatchedNone.fetch_add(
                        1, std::memory_order_relaxed);
                // E-H2-69 (C-H2-81): restore the weapon's own per-eye
                // parallax, but only in the shape that cannot displace or
                // rotate the rig - identical orientation, and a position
                // difference no larger than this eye's own offset from the
                // pair centre (one half-IPD, live). Anything else keeps
                // C-H2-80's exact Anniversary parity for that pass.
                bool admit = false;
                if (kHalo2ClassicFirstPersonEyeDepth && eyeKnown)
                {
                    float separation = 0.0f;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const float d = scope->eyes[eye].render.position[axis] -
                            scope->renderCenter.position[axis];
                        separation += d * d;
                    }
                    separation = std::sqrt(separation);
                    admit = Halo2ClassicEyeCompensationAdmissible(
                        scope->eyes[eye].render, measured,
                        separation * 1.5f + 1.0e-4f);
                    (admit ? g_fpDepthAdmitted : g_fpDepthRefused)
                        .fetch_add(1, std::memory_order_relaxed);
                }
                Halo2Observer6Dof_SetMeasuredFirstPersonViewingCamera(
                    measured, admit);
                g_fpCameraMeasured.fetch_add(1, std::memory_order_relaxed);
                // E-H2-70: the weapon's nodes were composed against the
                // camera the engine handed THIS renderer's packet build.
                // If the camera this pass draws them from points elsewhere,
                // the weapon is tipped in this renderer alone - a muzzle
                // rise no repositioning can remove. Publish that angle.
                Halo2CameraBasis packetCamera{};
                if (Halo2Observer6Dof_ReadPacketBuildCamera(
                        false, packetCamera))
                {
                    PublishCameraOrientationDelta(
                        packetCamera, measured, g_fpPitchMilliDegrees,
                        g_fpPitchMaxMilliDegrees);
                }
            }
            else
            {
                g_fpCameraUnreadable.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const bool classicPaletteOwned =
            Halo2Observer6Dof_BeginClassicFirstPersonEye();
        __try { original(); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_telemetry.transactionExceptions.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (classicPaletteOwned)
            Halo2Observer6Dof_EndClassicFirstPersonEye();
        for (const Centred& entry : owned)
        {
            if (!entry.active)
                continue;
            (void)GuardedCopyExact(
                reinterpret_cast<void*>(entry.address + kHalo2CameraPositionOffset),
                entry.saved, kHalo2CameraVectorBytes);
            (void)GuardedCopyExact(
                reinterpret_cast<void*>(entry.address + kHalo2CameraForwardOffset),
                entry.saved + 3, kHalo2CameraVectorBytes);
            (void)GuardedCopyExact(
                reinterpret_cast<void*>(entry.address + kHalo2CameraUpOffset),
                entry.saved + 6, kHalo2CameraVectorBytes);
        }
        g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    bool HotStateMatches(uint32_t generation) noexcept
    {
        const uint64_t quarantine =
            g_runtimeQuarantine.load(std::memory_order_acquire);
        if (!(generation &&
            g_installed.load(std::memory_order_acquire) &&
            g_armed.load(std::memory_order_acquire) &&
            g_stereoRequested.load(std::memory_order_acquire) &&
            g_presentationReady.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) &&
            g_coldObservationPassed.load(std::memory_order_acquire) &&
            !g_teardownRequested.load(std::memory_order_acquire) &&
            RuntimeQuarantineGeneration(quarantine) != generation &&
            g_generation.load(std::memory_order_acquire) == generation))
        {
            return false;
        }

        // These getters are atomic-only. Checking pause here closes the gap
        // between Presents; checking exact prepared timing prevents a nominal
        // 90 Hz target with 45 Hz predicted-display delivery from claiming.
        Halo2PreparedCadenceSnapshot cadence{};
        return !VR_IsPausePresentationTarget() &&
            !VR_IsPausePresentation() &&
            VR_Halo2GetCurrentPreparedCadence(cadence) &&
            Halo2PreparedCadenceSupported(
                cadence.predictedDisplayPeriodNs,
                cadence.predictedDisplayDeltaNs);
    }

    bool HeadReferenceIsCurrent(const StereoScope& scope) noexcept
    {
        return (g_headReferenceResetSerial.load(
                    std::memory_order_acquire) &
                kHeadReferenceResetSerialMask) ==
            scope.headReferenceResetSerial;
    }

    bool SnapshotTokenIsCurrent(
        uint32_t generation, uint64_t serial) noexcept
    {
        Halo2SynchronousVrRenderSnapshot current{};
        if (!HotStateMatches(generation))
            return false;
        __try
        {
            return VR_Halo2GetSynchronousRenderSnapshot(current) &&
                current.generation == generation &&
                current.preparedSerial == serial &&
                Halo2PreparedCadenceSupported(
                    current.predictedDisplayPeriodNs,
                    current.predictedDisplayDeltaNs);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadCurrentBackbufferRtv(
        ID3D11RenderTargetView*& output) noexcept
    {
        output = nullptr;
        const uintptr_t slot =
            g_backbufferRtvSlotAddress.load(std::memory_order_acquire);
        if (!slot)
            return false;
        __try
        {
            std::memcpy(&output, reinterpret_cast<const void*>(slot),
                        sizeof(output));
            return output != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            output = nullptr;
            return false;
        }
    }

    void RejectToken(uint32_t generation, uint64_t serial) noexcept
    {
        if (!generation || !serial)
            return;
        __try
        {
            VR_Halo2InvalidateSynchronousPair(generation, serial);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_telemetry.transactionExceptions.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    bool GuardedBeginPair(
        uint32_t generation, uint64_t serial,
        ID3D11RenderTargetView* backbufferRtv) noexcept
    {
        // The primary scene slot sits 8 bytes past the final-output slot
        // (0x197EE60); lent as the second capture candidate (E-H2-8).
        ID3D11RenderTargetView* sceneRtv = nullptr;
        const uintptr_t slot =
            g_backbufferRtvSlotAddress.load(std::memory_order_acquire);
        __try
        {
            if (slot)
                std::memcpy(&sceneRtv, reinterpret_cast<const void*>(slot + 8),
                            sizeof(sceneRtv));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { sceneRtv = nullptr; }
        __try
        {
            return VR_Halo2BeginSynchronousPair(
                generation, serial, backbufferRtv, sceneRtv);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedBeginRasterEye(
        uint32_t generation, uint64_t serial, int eye) noexcept
    {
        __try
        {
            return VR_Halo2BeginSynchronousEye(
                generation, serial, eye);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedClaimPresentation(
        uint32_t generation, uint64_t serial) noexcept
    {
        __try
        {
            return VR_Halo2ClaimSynchronousPairForPresentation(
                generation, serial);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedEndRasterEye() noexcept
    {
        __try
        {
            VR_EndRasterEye();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GuardedCompleteEye(
        uint32_t generation, uint64_t serial, int eye,
        const Halo2SymmetricHalfFovs& halfFovs) noexcept
    {
        __try
        {
            return VR_Halo2CompleteSynchronousEye(
                generation, serial, eye,
                halfFovs.horizontal, halfFovs.vertical);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // E-H2-12: after render_view returns, the raster context it pushed has
    // been popped but not cleared. Find it by depth, prove it is this eye's
    // by its camera bytes (the raster basis and cover FOV the mod wrote are
    // copied verbatim into the block), then take the projection the engine
    // built from it. SEH-contained; any doubt leaves the eye on the written
    // cover and is counted, never silent.
    bool ReadEngineProjection(StereoScope& scope, int eye) noexcept
    {
        scope.engineHalfFovsValid[eye] = false;
        scope.engineProjectionValid[eye] = false;
        const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
        if (!base)
            return false;
        __try
        {
            const int32_t depth = *reinterpret_cast<const volatile int32_t*>(
                base + kHalo2ClassicRasterContextDepthRva);
            uint32_t slotRva = 0;
            if (!Halo2ClassicPoppedRasterContextSlot(depth, slotRva))
                return false;
            const uint8_t* slot = reinterpret_cast<const uint8_t*>(base + slotRva);
            const uint8_t* camera = slot + kHalo2ClassicRasterContextCameraOffset;
            Halo2CameraBasis held{};
            float heldFov = 0.0f;
            std::memcpy(&held, camera + kHalo2CameraPositionOffset, sizeof(held));
            std::memcpy(&heldFov, camera + kHalo2CameraVerticalFovOffset,
                        sizeof(heldFov));
            if (std::memcmp(&held, &scope.eyes[eye].raster, sizeof(held)) != 0 ||
                std::memcmp(&heldFov, &scope.rasterCoverVerticalFov,
                            sizeof(heldFov)) != 0)
            {
                return false;
            }
            const uint8_t* projection =
                slot + kHalo2ClassicRasterContextProjectionOffset;
            float scaleX = 0.0f;
            float scaleY = 0.0f;
            std::memcpy(&scaleX, projection + kHalo2ClassicProjectionScaleXOffset,
                        sizeof(scaleX));
            std::memcpy(&scaleY, projection + kHalo2ClassicProjectionScaleYOffset,
                        sizeof(scaleY));
            if (!Halo2HalfFovsFromProjectionScales(
                    scaleX, scaleY, scope.engineHalfFovs[eye]))
            {
                return false;
            }
            scope.engineHalfFovsValid[eye] = true;
            // The 16 floats starting at the scale-X read (64 bytes, inside
            // the 0x318-byte context); dlss::DecodeProjection validates
            // them as a perspective matrix or the log shows the rows.
            std::memcpy(scope.engineProjection[eye],
                        projection + kHalo2ClassicProjectionScaleXOffset,
                        sizeof(scope.engineProjection[eye]));
            scope.engineProjectionValid[eye] = true;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Optional DLSS: publish the camera this eye was rasterised with (the
    // raster basis the mod wrote, verified above against the popped context)
    // together with the engine's own projection. Runs after render_view and
    // before the raster-eye scope closes, so the eye's depth copy follows.
    // Classic writes no projection of ours before the draw, so it is
    // unjittered.
    void PublishDlssEyeCamera(const StereoScope& scope, int eye) noexcept
    {
        if (!VR_DlssWantsEyeCamera() || eye < 0 || eye >= kHalo2EyeCount ||
            !scope.engineProjectionValid[eye])
            return;
        VrEyeCameraSample sample{};
        std::memcpy(sample.position, scope.eyes[eye].raster.position,
                    sizeof(sample.position));
        std::memcpy(sample.forward, scope.eyes[eye].raster.forward,
                    sizeof(sample.forward));
        std::memcpy(sample.up, scope.eyes[eye].raster.up, sizeof(sample.up));
        std::memcpy(sample.projection, scope.engineProjection[eye],
                    sizeof(sample.projection));
        VR_PublishEyeCamera(eye, sample);
    }

    // The half-angles the compositor crops with: the engine's own projection
    // when it could be read, else the written cover. Telemetry records which.
    Halo2SymmetricHalfFovs NoteProjectionReadback(
        StereoScope& scope, int eye) noexcept
    {
        g_telemetry.projectionReadbacks.fetch_add(1, std::memory_order_relaxed);
        uint32_t writtenBits[2]{};
        std::memcpy(&writtenBits[0], &scope.halfFovs.horizontal, sizeof(float));
        std::memcpy(&writtenBits[1], &scope.halfFovs.vertical, sizeof(float));
        g_projectionReadbackWrittenBits[0].store(writtenBits[0], std::memory_order_relaxed);
        g_projectionReadbackWrittenBits[1].store(writtenBits[1], std::memory_order_relaxed);
        if (!scope.engineHalfFovsValid[eye])
        {
            g_telemetry.projectionUnreadable.fetch_add(1, std::memory_order_relaxed);
            g_projectionReadbackStatus.store(3, std::memory_order_release);
            return scope.halfFovs;
        }
        const Halo2SymmetricHalfFovs& engine = scope.engineHalfFovs[eye];
        uint32_t engineBits[2]{};
        std::memcpy(&engineBits[0], &engine.horizontal, sizeof(float));
        std::memcpy(&engineBits[1], &engine.vertical, sizeof(float));
        g_projectionReadbackEngineBits[0].store(engineBits[0], std::memory_order_relaxed);
        g_projectionReadbackEngineBits[1].store(engineBits[1], std::memory_order_relaxed);
        const bool agrees = Halo2HalfFovsAgree(
            engine, scope.halfFovs, kHalo2ProjectionReadbackToleranceRadians);
        if (!agrees)
            g_telemetry.projectionMismatches.fetch_add(1, std::memory_order_relaxed);
        g_projectionReadbackStatus.store(agrees ? 1u : 2u, std::memory_order_release);
        return engine;
    }

    bool GuardedCompletePair(
        uint32_t generation, uint64_t serial) noexcept
    {
        __try
        {
            return VR_Halo2CompleteSynchronousPair(generation, serial);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int NoteClaimedTransactionException() noexcept
    {
        g_telemetry.transactionExceptions.fetch_add(
            1, std::memory_order_relaxed);
        if (g_stereoScope)
        {
            g_stereoScope->exceptionSeen = true;
            if (!g_stereoScope->presentationClaimPublished)
            {
                g_stereoScope->presentationClaimPublished =
                    GuardedClaimPresentation(
                        g_stereoScope->generation,
                        g_stereoScope->serial);
            }
            PublishRuntimeQuarantine(
                g_stereoScope->generation,
                Halo2StereoQuarantineReason::
                    CoreClaimedTransactionFailed);
            InvalidateScope(*g_stereoScope);
            return EXCEPTION_EXECUTE_HANDLER;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void CallStockInner(
        Halo2RenderViewFn original,
        uint32_t argument01, void* argument02, void* argument03,
        uint8_t argument04, uint32_t argument05, uint32_t argument06,
        uint8_t argument07, float (*argument08)[4], uint16_t argument09,
        int32_t argument10, uint32_t argument11, uint8_t argument12,
        uint32_t argument13, void* argument14, uint8_t argument15,
        int16_t argument16, void* argument17, uint8_t argument18,
        void* argument19)
    {
        if (!original)
            return;
        g_telemetry.stockInnerCalls.fetch_add(1, std::memory_order_relaxed);
        // A stock render_view pops the stock camera: whatever the next
        // first-person pass draws from is no longer an eye of ours.
        g_lastPassEyeCameraValid = false;
        original(
            argument01, argument02, argument03, argument04,
            argument05, argument06, argument07, argument08,
            argument09, argument10, argument11, argument12, argument13,
            argument14, argument15, argument16, argument17, argument18,
            argument19);
    }

    void Halo2RenderViewDetourBody(
        uintptr_t callerReturn,
        uint32_t argument01, void* argument02, void* argument03,
        uint8_t argument04, uint32_t argument05, uint32_t argument06,
        uint8_t argument07, float (*argument08)[4], uint16_t argument09,
        int32_t argument10, uint32_t argument11, uint8_t argument12,
        uint32_t argument13, void* argument14, uint8_t argument15,
        int16_t argument16, void* argument17, uint8_t argument18,
        void* argument19)
    {
        g_telemetry.innerCallbacks.fetch_add(1, std::memory_order_relaxed);
        Halo2RenderViewFn original = OriginalRenderView();
        if (!original)
            return;

        StereoScope* scope = g_stereoScope;
        if (!scope || g_suppressedOuterDepth || g_innerDepth)
        {
            CallStockInner(
                original, argument01, argument02, argument03, argument04,
                argument05, argument06, argument07, argument08, argument09,
                argument10, argument11, argument12, argument13, argument14,
                argument15, argument16, argument17, argument18, argument19);
            return;
        }

        const uintptr_t moduleBase =
            g_moduleBase.load(std::memory_order_acquire);
        const bool exactCaller = moduleBase &&
            callerReturn == moduleBase + kHalo2RetailRenderViewReturnRva;
        if (!exactCaller)
        {
            g_telemetry.foreignInnerCallers.fetch_add(
                1, std::memory_order_relaxed);
            CallStockInner(
                original, argument01, argument02, argument03, argument04,
                argument05, argument06, argument07, argument08, argument09,
                argument10, argument11, argument12, argument13, argument14,
                argument15, argument16, argument17, argument18, argument19);
            return;
        }

        const bool argumentsMatch = argument01 == 0 && argument04 == 0 &&
            argument02 == scope->renderCamera &&
            argument03 == scope->rasterCamera;
        if (!argumentsMatch)
        {
            InvalidateScope(*scope);
            if (RestoreOwnedSpans(*scope))
            {
                CallStockInner(
                    original, argument01, argument02, argument03, argument04,
                    argument05, argument06, argument07, argument08, argument09,
                    argument10, argument11, argument12, argument13, argument14,
                    argument15, argument16, argument17, argument18, argument19);
            }
            return;
        }

        ++scope->exactInnerInvocations;
        if (scope->exactInnerInvocations != 1)
        {
            // The proven player edge contains one render_view call. Suppress a
            // duplicate instead of creating a third game render, and revoke
            // any pair the first invocation may already have completed.
            scope->transactionShapeFailed = true;
            InvalidateScope(*scope);
            return;
        }
        g_telemetry.claimedInnerCalls.fetch_add(
            1, std::memory_order_relaxed);

        if (!scope->pairBegun || scope->invalidated ||
            !HeadReferenceIsCurrent(*scope) ||
            !SnapshotTokenIsCurrent(scope->generation, scope->serial))
        {
            InvalidateScope(*scope);
            if (RestoreOwnedSpans(*scope))
            {
                CallStockInner(
                    original, argument01, argument02, argument03, argument04,
                    argument05, argument06, argument07, argument08, argument09,
                    argument10, argument11, argument12, argument13, argument14,
                    argument15, argument16, argument17, argument18, argument19);
            }
            return;
        }

        bool finalSpansRestored = false;
        bool rasterScopesClosed = true;
        ++g_innerDepth;
        __try
        {
            const int firstEye = scope->rightEyeFirst
                ? kHalo2RightEye
                : kHalo2LeftEye;
            for (int pass = 0; pass < kHalo2EyeCount; ++pass)
            {
                const int eye = pass == 0 ? firstEye : 1 - firstEye;
                if (scope->invalidated ||
                    !HeadReferenceIsCurrent(*scope) ||
                    !SnapshotTokenIsCurrent(
                        scope->generation, scope->serial) ||
                    !WriteEyeSpans(*scope, eye))
                {
                    InvalidateScope(*scope);
                    break;
                }
                // E-H2-34: name this pass's cameras for the weapon re-anchor.
                // The classic first-person pass draws from the camera the
                // PREVIOUS render_view popped (C-H2-36 pictures: the gun
                // carried the full eye offset the wrong way round while the
                // world was right), so the pass's viewing camera is the
                // other eye's - the previous pair's last eye for the first
                // pass, this pair's first eye for the second.
                {
                    Halo2FirstPersonPassCameras passCameras{};
                    passCameras.frame = scope->renderCenter;
                    passCameras.frameValid = scope->rasterCenterValid;
                    passCameras.correct = scope->eyes[eye].render;
                    if (!kHalo2ClassicFirstPersonEyeCompensation)
                    {
                        // C-H2-80: no Classic-only transform, so the packet
                        // Classic draws is the packet Anniversary draws.
                        passCameras.viewing = passCameras.correct;
                        passCameras.compensate = false;
                    }
                    else if (kHalo2ClassicCommonFirstPersonCamera)
                    {
                        // C-H2-78: both passes draw the weapon from the
                        // common tracked centre, so that is the camera the
                        // geometry must be moved to for this eye's image
                        // to be correct. See the constant's evidence note.
                        passCameras.viewing = scope->renderCenter;
                        passCameras.compensate = scope->rasterCenterValid;
                    }
                    else if (pass == 0)
                    {
                        passCameras.viewing = g_lastPassEyeCamera;
                        passCameras.compensate = g_lastPassEyeCameraValid;
                    }
                    else
                    {
                        passCameras.viewing = scope->eyes[firstEye].render;
                        passCameras.compensate = true;
                    }
                    if (!passCameras.compensate)
                        g_firstPersonUncompensated.fetch_add(1, std::memory_order_relaxed);
                    Halo2Observer6Dof_SetFirstPersonPassCameras(&passCameras);
                    g_currentPassEye = eye;
                }

                // E-H2-29: the world pass's target id 0 resolves through the
                // engine's once-per-frame latch, which the FIRST eye's
                // postprocess has already flipped. Put it back to what
                // render_player_window left, or this eye draws into a
                // different target from the other one - which is exactly what
                // "identical eyes with correct per-eye cameras" looked like.
                if (scope->sceneTargetLatchValid)
                {
                    const uintptr_t base =
                        g_moduleBase.load(std::memory_order_acquire);
                    uint8_t current = 0;
                    if (base &&
                        ReadByteGuarded(
                            base + kHalo2ClassicSceneTargetLatchRva, current))
                    {
                        if (current != scope->sceneTargetLatch)
                        {
                            if (WriteByteGuarded(
                                    base + kHalo2ClassicSceneTargetLatchRva,
                                    scope->sceneTargetLatch))
                            {
                                g_sceneLatchRearmed.fetch_add(
                                    1, std::memory_order_relaxed);
                            }
                            else
                            {
                                g_sceneLatchUnreadable.fetch_add(
                                    1, std::memory_order_relaxed);
                            }
                        }
                    }
                    else
                    {
                        g_sceneLatchUnreadable.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                }
                bool rasterEyeBegun = false;
                bool rasterEyeAttempted = false;
                bool originalReturned = false;
                bool rasterEyeEnded = false;
                bool spansRestored = false;
                __try
                {
                    rasterEyeAttempted = true;
                    rasterEyeBegun = GuardedBeginRasterEye(
                        scope->generation, scope->serial, eye);
                    if (rasterEyeBegun)
                    {
                        // Publish the exact resource-free claim immediately
                        // before the first original eye call. If publication
                        // fails, run no eye and use the stock replay below.
                        if (!scope->presentationClaimPublished)
                        {
                            scope->presentationClaimPublished =
                                GuardedClaimPresentation(
                                    scope->generation, scope->serial);
                        }
                        if (scope->presentationClaimPublished)
                        {
                            ++scope->renderViewCalls;
                            __try
                            {
                                Halo2NativeHud_BeginEye(scope->halfFovs);
                                __try
                                {
                                    original(
                                        argument01, argument02, argument03, argument04,
                                        argument05, argument06, argument07, argument08,
                                        argument09, argument10, argument11, argument12,
                                        argument13, argument14, argument15, argument16,
                                        argument17, argument18, argument19);
                                    originalReturned = true;
                                    ++scope->renderViewReturns;
                                }
                                __finally
                                {
                                    Halo2NativeHud_EndEye();
                                }
                            }
                            __except (NoteClaimedTransactionException())
                            {
                            }
                            // Before anything else can push another raster
                            // context: the popped slot is this eye's only now.
                            if (originalReturned)
                            {
                                (void)ReadEngineProjection(*scope, eye);
                                PublishDlssEyeCamera(*scope, eye);
                                // E-H2-34: this pop is what the next
                                // first-person pass will draw from.
                                g_lastPassEyeCamera = scope->eyes[eye].render;
                                g_lastPassEyeCameraValid = true;
                                // E-H2-35: what FOV did this eye's weapon
                                // pass find in the constant? Read before the
                                // restore below puts stock back.
                                const uintptr_t constant = g_fpConstantAddress.load(
                                    std::memory_order_acquire);
                                uint32_t held = 0;
                                if (!constant)
                                {
                                    // not pinned: nothing to hold
                                }
                                else if (!ReadDwordGuarded(constant, held))
                                {
                                    g_fpConstantUnreadable.fetch_add(
                                        1, std::memory_order_relaxed);
                                }
                                else if (held == g_fpConstantWrittenBits.load(
                                             std::memory_order_relaxed))
                                {
                                    g_fpConstantHeld[eye].fetch_add(
                                        1, std::memory_order_relaxed);
                                }
                                else
                                {
                                    g_fpConstantLost[eye].fetch_add(
                                        1, std::memory_order_relaxed);
                                }
                            }
                        }
                    }
                }
                __finally
                {
                    // End plus all six vector and two FOV restores are
                    // independently SEH-contained; one bad destination cannot
                    // strand the other camera or projection input.
                    if (rasterEyeAttempted)
                        rasterEyeEnded = GuardedEndRasterEye();
                    if (rasterEyeAttempted && !rasterEyeEnded)
                        scope->rasterScopeCloseFailed = true;
                    rasterScopesClosed = rasterScopesClosed &&
                        (!rasterEyeAttempted || rasterEyeEnded);
                    spansRestored = RestoreOwnedSpans(*scope);
                }

                if (!rasterEyeBegun || !originalReturned || !rasterEyeEnded ||
                    !spansRestored || scope->invalidated ||
                    !HeadReferenceIsCurrent(*scope) ||
                    !SnapshotTokenIsCurrent(
                        scope->generation, scope->serial) ||
                    !GuardedCompleteEye(
                        scope->generation, scope->serial, eye,
                        NoteProjectionReadback(*scope, eye)))
                {
                    InvalidateScope(*scope);
                    break;
                }

                scope->completedEyeMask = static_cast<uint8_t>(
                    scope->completedEyeMask | (1u << eye));
                g_telemetry.renderedEyes.fetch_add(
                    1, std::memory_order_relaxed);
            }
#include "halo2_classic_scope_pass.inl"

        }
        __finally
        {
            Halo2Observer6Dof_SetFirstPersonPassCameras(nullptr);
            g_currentPassEye = -1;
            finalSpansRestored = RestoreOwnedSpans(*scope);
            --g_innerDepth;
        }

        // If no eye original ran, preserve the engine's one required
        // render_view transaction only after every owned field is stock again.
        // Once either eye has run, failure drops this VR pair; it never adds a
        // third replay.
        if (scope->renderViewCalls == 0 && finalSpansRestored &&
            rasterScopesClosed)
        {
            InvalidateScope(*scope);
            CallStockInner(
                original, argument01, argument02, argument03, argument04,
                argument05, argument06, argument07, argument08, argument09,
                argument10, argument11, argument12, argument13, argument14,
                argument15, argument16, argument17, argument18, argument19);
        }
    }

    __declspec(noinline) void __fastcall Halo2RenderViewDetour(
        uint32_t argument01, void* argument02, void* argument03,
        uint8_t argument04, uint32_t argument05, uint32_t argument06,
        uint8_t argument07, float (*argument08)[4], uint16_t argument09,
        int32_t argument10, uint32_t argument11, uint8_t argument12,
        uint32_t argument13, void* argument14, uint8_t argument15,
        int16_t argument16, void* argument17, uint8_t argument18,
        void* argument19)
    {
        const uintptr_t callerReturn =
            reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            __try
            {
                Halo2RenderViewDetourBody(
                    callerReturn,
                    argument01, argument02, argument03, argument04,
                    argument05, argument06, argument07, argument08,
                    argument09, argument10, argument11, argument12,
                    argument13, argument14, argument15, argument16,
                    argument17, argument18, argument19);
            }
            __except (NoteClaimedTransactionException())
            {
            }
        }
        __finally
        {
            g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool BuildStereoScope(
        void* window, uint32_t generation, bool rightEyeFirst,
        const Halo2SynchronousVrRenderSnapshot& snapshot,
        StereoScope& scope) noexcept
    {
        CameraReadback render{};
        CameraReadback raster{};
        if (!ReadCamera(window, kHalo2RenderCameraOffset, render) ||
            !ReadCamera(window, kHalo2RasterCameraOffset, raster) ||
            !Halo2ValidateCameraBasis(render.basis) ||
            !Halo2ValidateCameraBasis(raster.basis) ||
            !Halo2StockProjectionIsSymmetric(
                render.asymmetricEnable, render.pixelOffsetEnable,
                raster.asymmetricEnable, raster.pixelOffsetEnable))
        {
            return false;
        }

        float renderCoverVerticalFov = 0.0f;
        float rasterCoverVerticalFov = 0.0f;
        Halo2SymmetricHalfFovs renderHalfFovs{};
        Halo2SymmetricHalfFovs rasterHalfFovs{};
        if (!DeriveRuntimeCover(
                snapshot, render.viewport,
                renderCoverVerticalFov, renderHalfFovs) ||
            !DeriveRuntimeCover(
                snapshot, raster.viewport,
                rasterCoverVerticalFov, rasterHalfFovs))
        {
            return false;
        }

        HeadReference reference{};
        uint32_t headReferenceResetSerial = 0;
        if (!snapshot.headPoseValid ||
            !AcquireHeadReference(
                generation, snapshot.headOrientation,
                snapshot.headPosition, reference,
                headReferenceResetSerial))
        {
            return false;
        }

        Halo2ObserverPosePublication publication{};
        bool published = false;
        __try
        {
            published = Halo2Observer6Dof_ReadPublishedPose(publication);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            published = false;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &render.basis.position[axis], sizeof(bits));
            g_lastWindowPositionBits[axis].store(bits, std::memory_order_relaxed);
            std::memcpy(&bits, &publication.tracked.position[axis], sizeof(bits));
            g_lastPublishedPositionBits[axis].store(bits, std::memory_order_relaxed);
        }
        g_lastPublishedValid.store(published, std::memory_order_relaxed);
        Halo2CameraBasis renderCenter{};
        Halo2CameraBasis rasterCenter{};
        if (!ResolveTrackedCenter(
                render.basis, reference, snapshot, generation, published,
                publication, renderCenter) ||
            !ResolveTrackedCenter(
                raster.basis, reference, snapshot, generation, published,
                publication, rasterCenter))
        {
            return false;
        }

        EyeCameraPair eyes[kHalo2EyeCount]{};
        for (int eye = 0; eye < kHalo2EyeCount; ++eye)
        {
            if (!BuildEyeCamera(
                    renderCenter, snapshot, eye, eyes[eye].render) ||
                !BuildEyeCamera(
                    rasterCenter, snapshot, eye, eyes[eye].raster))
            {
                return false;
            }
        }

        StereoScope candidate{};
        // E-H2-29: the latch as render_player_window left it, before the first
        // eye's postprocess flips it.
        {
            const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
            uint8_t latch = 0;
            if (base && ReadByteGuarded(base + kHalo2ClassicSceneTargetLatchRva, latch))
            {
                candidate.sceneTargetLatch = latch;
                candidate.sceneTargetLatchValid = true;
            }
        }
        candidate.generation = generation;
        candidate.headReferenceResetSerial = headReferenceResetSerial;
        candidate.serial = snapshot.preparedSerial;
        candidate.rightEyeFirst = rightEyeFirst;
        candidate.window = window;
        candidate.renderCamera = static_cast<uint8_t*>(window) +
            kHalo2RenderCameraOffset;
        candidate.rasterCamera = static_cast<uint8_t*>(window) +
            kHalo2RasterCameraOffset;
        candidate.rasterCenter = rasterCenter;
        candidate.renderCenter = renderCenter;
        candidate.rasterCenterValid = true;
        candidate.savedRender = render.basis;
        candidate.savedRaster = raster.basis;
        candidate.savedRenderVerticalFov = render.verticalFov;
        candidate.savedRasterVerticalFov = raster.verticalFov;
        candidate.renderCoverVerticalFov = renderCoverVerticalFov;
        candidate.rasterCoverVerticalFov = rasterCoverVerticalFov;
        candidate.halfFovs = rasterHalfFovs;
        for (int eye = 0; eye < kHalo2EyeCount; ++eye)
            candidate.eyes[eye] = eyes[eye];
        scope = candidate;
        return true;
    }

    void CallStockOuter(
        Halo2RenderPlayerWindowFn original, void* window, uint8_t flag)
    {
        if (!original)
            return;
        g_telemetry.stockOuterCalls.fetch_add(1, std::memory_order_relaxed);
        original(window, flag);
    }

    void RejectCurrentSnapshotIfAny() noexcept
    {
        Halo2SynchronousVrRenderSnapshot snapshot{};
        __try
        {
            if (VR_Halo2GetSynchronousRenderSnapshot(snapshot))
            {
                RejectToken(snapshot.generation, snapshot.preparedSerial);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_telemetry.transactionExceptions.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    void Halo2PlayerWindowDetourBody(
        void* window, uint8_t flag, uintptr_t callerReturn)
    {
        g_telemetry.outerCallbacks.fetch_add(1, std::memory_order_relaxed);
        Halo2RenderPlayerWindowFn original = OriginalPlayerWindow();
        if (!original)
            return;

        // Preserve a nested stock player transaction without letting its
        // render_view invocation borrow the outer transaction's cameras/token.
        if (g_stereoScope)
        {
            ++g_suppressedOuterDepth;
            __try
            {
                CallStockOuter(original, window, flag);
            }
            __finally
            {
                --g_suppressedOuterDepth;
            }
            return;
        }

        const uint32_t generation =
            g_generation.load(std::memory_order_acquire);
        const uintptr_t moduleBase =
            g_moduleBase.load(std::memory_order_acquire);
        const bool exactCaller = moduleBase &&
            callerReturn ==
                moduleBase + kHalo2RetailRenderPlayerWindowReturnRva;
        if (!HotStateMatches(generation) || !exactCaller)
        {
            if (HotStateMatches(generation) && !exactCaller)
            {
                g_telemetry.foreignOuterCallers.fetch_add(
                    1, std::memory_order_relaxed);
            }
            CallStockOuter(original, window, flag);
            return;
        }

        int32_t playerIndex = -1;
        if (flag != 0 || !ReadPlayerIndex(window, playerIndex) ||
            playerIndex != 0)
        {
            g_telemetry.invalidWindows.fetch_add(
                1, std::memory_order_relaxed);
            RejectCurrentSnapshotIfAny();
            CallStockOuter(original, window, flag);
            return;
        }

        Halo2SynchronousVrRenderSnapshot snapshot{};
        bool snapshotReady = false;
        __try
        {
            snapshotReady =
                VR_Halo2GetSynchronousRenderSnapshot(snapshot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshotReady = false;
        }
        if (!snapshotReady || !snapshot.preparedSerial ||
            snapshot.generation != generation ||
            !Halo2PreparedCadenceSupported(
                snapshot.predictedDisplayPeriodNs,
                snapshot.predictedDisplayDeltaNs))
        {
            g_telemetry.missingSnapshots.fetch_add(
                1, std::memory_order_relaxed);
            RejectToken(snapshot.generation, snapshot.preparedSerial);
            CallStockOuter(original, window, flag);
            return;
        }

        const uint64_t lastCompletedPairSerial =
            g_lastCompletedPairSerial.load(std::memory_order_acquire);
        if (!Halo2PreparedSerialMayFollowCompletedPair(
                lastCompletedPairSerial, snapshot.preparedSerial))
        {
            // No eye has rendered and no presentation claim exists, so this
            // frame stays on the stock path. The worker removes this generation
            // before any later serial can resume a half-rate sequence.
            g_serialGapExpected.store(
                lastCompletedPairSerial + 1, std::memory_order_relaxed);
            g_serialGapObserved.store(
                snapshot.preparedSerial, std::memory_order_release);
            PublishRuntimeQuarantine(
                generation,
                Halo2StereoQuarantineReason::CorePreparedSerialGap);
            RejectToken(generation, snapshot.preparedSerial);
            CallStockOuter(original, window, flag);
            return;
        }

        StereoScope scope{};
        ID3D11RenderTargetView* exactBackbufferRtv = nullptr;
        if (!BuildStereoScope(
                window, generation,
                g_rightEyeFirst.load(std::memory_order_acquire),
                snapshot, scope) ||
            !ReadCurrentBackbufferRtv(exactBackbufferRtv))
        {
            g_telemetry.invalidWindows.fetch_add(
                1, std::memory_order_relaxed);
            RejectToken(generation, snapshot.preparedSerial);
            CallStockOuter(original, window, flag);
            return;
        }

        if (!ClaimSerial(snapshot.preparedSerial))
        {
            g_telemetry.duplicateSerials.fetch_add(
                1, std::memory_order_relaxed);
            RejectToken(generation, snapshot.preparedSerial);
            CallStockOuter(original, window, flag);
            return;
        }
        if (!SnapshotTokenIsCurrent(generation, snapshot.preparedSerial) ||
            !GuardedBeginPair(
                generation, snapshot.preparedSerial, exactBackbufferRtv))
        {
            RejectToken(generation, snapshot.preparedSerial);
            CallStockOuter(original, window, flag);
            return;
        }

        scope.pairBegun = true;
        g_telemetry.claimedPairs.fetch_add(1, std::memory_order_relaxed);
        if (!WriteOuterCoverFovs(scope))
        {
            const bool restored = RestoreOwnedSpans(scope);
            if (!restored)
            {
                scope.presentationClaimPublished =
                    GuardedClaimPresentation(
                        generation, snapshot.preparedSerial);
                PublishRuntimeQuarantine(
                    generation,
                    Halo2StereoQuarantineReason::
                        CoreClaimedTransactionFailed);
            }
            RejectToken(generation, snapshot.preparedSerial);
            if (restored)
                CallStockOuter(original, window, flag);
            else
                g_telemetry.droppedPairs.fetch_add(
                    1, std::memory_order_relaxed);
            return;
        }
        g_stereoScope = &scope;
        __try
        {
            __try
            {
                CallStockOuter(original, window, flag);
                scope.outerReturned = true;
            }
            __except (NoteClaimedTransactionException())
            {
            }
        }
        __finally
        {
            if (!RestoreOwnedSpans(scope))
                InvalidateScope(scope);
            g_stereoScope = nullptr;
        }

        const bool completeShape = !scope.invalidated &&
            !scope.exceptionSeen && scope.outerReturned &&
            HeadReferenceIsCurrent(scope) &&
            scope.exactInnerInvocations == 1 &&
            scope.renderViewCalls == kHalo2EyeCount &&
            scope.renderViewReturns == kHalo2EyeCount &&
            scope.completedEyeMask ==
                ((1u << kHalo2EyeCount) - 1u) &&
            SnapshotTokenIsCurrent(generation, snapshot.preparedSerial);
        if (completeShape &&
            GuardedCompletePair(generation, snapshot.preparedSerial))
        {
            scope.pairCompleted = true;
            g_lastCompletedPairSerial.store(
                snapshot.preparedSerial, std::memory_order_release);
            g_telemetry.completedPairs.fetch_add(
                1, std::memory_order_relaxed);
            return;
        }

        if (Halo2StructuralFailureRequiresQuarantine(
                scope.renderViewCalls, scope.ownedStateRestoreFailed,
                scope.rasterScopeCloseFailed, scope.exceptionSeen,
                scope.transactionShapeFailed))
        {
            PublishRuntimeQuarantine(
                generation,
                Halo2StereoQuarantineReason::
                    CoreClaimedTransactionFailed);
        }
        RejectToken(generation, snapshot.preparedSerial);
        g_telemetry.droppedPairs.fetch_add(1, std::memory_order_relaxed);
    }

    __declspec(noinline) void __fastcall Halo2PlayerWindowDetour(
        void* window, uint8_t flag)
    {
        const uintptr_t callerReturn =
            reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            __try
            {
                Halo2PlayerWindowDetourBody(window, flag, callerReturn);
            }
            __except (NoteClaimedTransactionException())
            {
            }
        }
        __finally
        {
            g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    TelemetrySnapshot ReadTelemetry() noexcept
    {
        TelemetrySnapshot out{};
#define H2_READ_TELEMETRY(field) \
        out.field = g_telemetry.field.load(std::memory_order_relaxed)
        H2_READ_TELEMETRY(outerCallbacks);
        H2_READ_TELEMETRY(innerCallbacks);
        H2_READ_TELEMETRY(stockOuterCalls);
        H2_READ_TELEMETRY(stockInnerCalls);
        H2_READ_TELEMETRY(missingSnapshots);
        H2_READ_TELEMETRY(foreignOuterCallers);
        H2_READ_TELEMETRY(foreignInnerCallers);
        H2_READ_TELEMETRY(invalidWindows);
        H2_READ_TELEMETRY(duplicateSerials);
        H2_READ_TELEMETRY(claimedPairs);
        H2_READ_TELEMETRY(claimedInnerCalls);
        H2_READ_TELEMETRY(renderedEyes);
        H2_READ_TELEMETRY(completedPairs);
        H2_READ_TELEMETRY(droppedPairs);
        H2_READ_TELEMETRY(restoreFailures);
        H2_READ_TELEMETRY(transactionExceptions);
        H2_READ_TELEMETRY(posePublished);
        H2_READ_TELEMETRY(poseRederived);
        H2_READ_TELEMETRY(poseSelfTracked);
        H2_READ_TELEMETRY(poseUnavailable);
        H2_READ_TELEMETRY(projectionReadbacks);
        H2_READ_TELEMETRY(projectionUnreadable);
        H2_READ_TELEMETRY(projectionMismatches);
#undef H2_READ_TELEMETRY
        return out;
    }

    void ResetTelemetryForInstall() noexcept
    {
#define H2_RESET_TELEMETRY(field) \
        g_telemetry.field.store(0, std::memory_order_relaxed)
        g_claimedFrameFailures.store(0, std::memory_order_relaxed);
        g_claimedFrameFailureLogMs = 0;
        H2_RESET_TELEMETRY(outerCallbacks);
        H2_RESET_TELEMETRY(innerCallbacks);
        H2_RESET_TELEMETRY(stockOuterCalls);
        H2_RESET_TELEMETRY(stockInnerCalls);
        H2_RESET_TELEMETRY(missingSnapshots);
        H2_RESET_TELEMETRY(foreignOuterCallers);
        H2_RESET_TELEMETRY(foreignInnerCallers);
        H2_RESET_TELEMETRY(invalidWindows);
        H2_RESET_TELEMETRY(duplicateSerials);
        H2_RESET_TELEMETRY(claimedPairs);
        H2_RESET_TELEMETRY(claimedInnerCalls);
        H2_RESET_TELEMETRY(renderedEyes);
        H2_RESET_TELEMETRY(completedPairs);
        H2_RESET_TELEMETRY(droppedPairs);
        H2_RESET_TELEMETRY(restoreFailures);
        H2_RESET_TELEMETRY(transactionExceptions);
        H2_RESET_TELEMETRY(posePublished);
        H2_RESET_TELEMETRY(poseRederived);
        H2_RESET_TELEMETRY(poseSelfTracked);
        H2_RESET_TELEMETRY(poseUnavailable);
#undef H2_RESET_TELEMETRY
        g_lastTelemetry = {};
        g_lastTelemetryMs = 0;
    }

    bool TelemetryChanged(
        const TelemetrySnapshot& left,
        const TelemetrySnapshot& right) noexcept
    {
        return std::memcmp(&left, &right, sizeof(left)) != 0;
    }

    void ReportTelemetry() noexcept
    {
        if (!g_installed.load(std::memory_order_acquire))
            return;
        const uint64_t now = GetTickCount64();
        if (g_lastTelemetryMs && now - g_lastTelemetryMs < kTelemetryPeriodMs)
            return;
        const TelemetrySnapshot current = ReadTelemetry();
        // The first post-install sample is evidence even when every counter is
        // zero: it distinguishes an installed hook that has not been reached
        // yet (for example, a loading/cinematic path) from a camera transaction
        // that entered and failed later. Subsequent quiet zero samples stay
        // silent; the shared VR worker reports the active unclaimed screen path.
        if (!g_lastTelemetryMs || TelemetryChanged(current, g_lastTelemetry))
        {
            LOG("Halo 2 stereo core: outer=%llu inner=%llu stockOuter=%llu "
                "stockInner=%llu snapshotMiss=%llu foreignOuter=%llu "
                "foreignInner=%llu invalid=%llu duplicate=%llu claimed=%llu "
                "innerClaimed=%llu eyes=%llu complete=%llu dropped=%llu "
                "restoreFail=%llu exception=%llu firstPersonCentred=%llu "
                "firstPersonUnreadable=%llu firstPassUncompensated=%llu "
                "fpFovHeld(eye0/eye1)=%llu/%llu fpFovLost(eye0/eye1)=%llu/%llu "
                "fpFovUnreadable=%llu sceneLatchRearmed=%llu "
                "sceneLatchUnreadable=%llu nativeHudScopedDraws=%llu "
                "nativeHudAnchorsMapped=%llu nativeHudReticlesStock=%llu "
                "fpCamMeasured=%llu fpCamUnreadable=%llu "
                "fpCamIsCentre=%llu fpCamIsThisEye=%llu fpCamIsOtherEye=%llu "
                "fpCamIsOther=%llu fpDepthOn=%llu fpDepthOff=%llu "
                "fpComposeVsDraw=%.3f deg (max %.3f) "
                "poseOwner=%s "
                "posePublished=%llu poseRederived=%llu poseSelf=%llu "
                "poseUnavailable=%llu",
                static_cast<unsigned long long>(current.outerCallbacks),
                static_cast<unsigned long long>(current.innerCallbacks),
                static_cast<unsigned long long>(current.stockOuterCalls),
                static_cast<unsigned long long>(current.stockInnerCalls),
                static_cast<unsigned long long>(current.missingSnapshots),
                static_cast<unsigned long long>(current.foreignOuterCallers),
                static_cast<unsigned long long>(current.foreignInnerCallers),
                static_cast<unsigned long long>(current.invalidWindows),
                static_cast<unsigned long long>(current.duplicateSerials),
                static_cast<unsigned long long>(current.claimedPairs),
                static_cast<unsigned long long>(current.claimedInnerCalls),
                static_cast<unsigned long long>(current.renderedEyes),
                static_cast<unsigned long long>(current.completedPairs),
                static_cast<unsigned long long>(current.droppedPairs),
                static_cast<unsigned long long>(current.restoreFailures),
                static_cast<unsigned long long>(current.transactionExceptions),
                static_cast<unsigned long long>(
                    g_firstPersonCentred.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_firstPersonUnreadable.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_firstPersonUncompensated.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpConstantHeld[0].load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpConstantHeld[1].load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpConstantLost[0].load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpConstantLost[1].load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpConstantUnreadable.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_sceneLatchRearmed.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_sceneLatchUnreadable.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_nativeHudScopedDraws.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_nativeHudAnchorsAdjusted.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_nativeHudCrosshairsPreserved.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpCameraMeasured.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpCameraUnreadable.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpCameraMatchedCentre.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpCameraMatchedThisEye.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpCameraMatchedOtherEye.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpCameraMatchedNone.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpDepthAdmitted.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    g_fpDepthRefused.load(std::memory_order_relaxed)),
                g_fpPitchMilliDegrees.load(std::memory_order_relaxed) / 1000.0,
                g_fpPitchMaxMilliDegrees.load(std::memory_order_relaxed) / 1000.0,
                Halo2Observer6Dof_Armed() ? "observer" : "classicCore",
                static_cast<unsigned long long>(current.posePublished),
                static_cast<unsigned long long>(current.poseRederived),
                static_cast<unsigned long long>(current.poseSelfTracked),
                static_cast<unsigned long long>(current.poseUnavailable));
            {
                float eyePosition[2][3]{};
                for (int eye = 0; eye < 2; ++eye)
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const uint32_t bits =
                            g_lastEyePositionBits[eye][axis].load(std::memory_order_relaxed);
                        std::memcpy(&eyePosition[eye][axis], &bits, sizeof(bits));
                    }
                float separation = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float d = eyePosition[1][axis] - eyePosition[0][axis];
                    separation += d * d;
                }
                separation = std::sqrt(separation);
                float windowPosition[3]{};
                float publishedPosition[3]{};
                for (int axis = 0; axis < 3; ++axis)
                {
                    uint32_t bits = g_lastWindowPositionBits[axis].load(std::memory_order_relaxed);
                    std::memcpy(&windowPosition[axis], &bits, sizeof(bits));
                    bits = g_lastPublishedPositionBits[axis].load(std::memory_order_relaxed);
                    std::memcpy(&publishedPosition[axis], &bits, sizeof(bits));
                }
                float windowDelta = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float d = windowPosition[axis] - publishedPosition[axis];
                    windowDelta += d * d;
                }
                windowDelta = std::sqrt(windowDelta);
                LOG("Halo 2 stereo core: render cameras written for the last "
                    "pair - eye 0 (%.4f, %.4f, %.4f) eye 1 (%.4f, %.4f, %.4f), "
                    "separation %.4f wu = %.1f mm; the engine's window camera "
                    "stood at (%.4f, %.4f, %.4f), %.1f mm from the observer "
                    "pose the mod published%s",
                    eyePosition[0][0], eyePosition[0][1], eyePosition[0][2],
                    eyePosition[1][0], eyePosition[1][1], eyePosition[1][2],
                    separation, separation * kHalo2MetersPerWorldUnit * 1000.0f,
                    windowPosition[0], windowPosition[1], windowPosition[2],
                    windowDelta * kHalo2MetersPerWorldUnit * 1000.0f,
                    g_lastPublishedValid.load(std::memory_order_relaxed)
                        ? "" : " (no publication was available)");
            }
            g_lastTelemetry = current;
        }
        // E-H2-12: the projection read-back, whenever its verdict or its
        // numbers change. This line, not the cover the mod wrote, says what
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
                LOG("Halo 2 classic eye projection read-back: UNREADABLE "
                    "(popped raster context slot missing or not this eye's); "
                    "the headset crop uses the written cover %.1f x %.1f deg "
                    "(full angles) - unreadable=%llu of %llu",
                    2.0f * values[0] * kDegrees, 2.0f * values[1] * kDegrees,
                    static_cast<unsigned long long>(current.projectionUnreadable),
                    static_cast<unsigned long long>(current.projectionReadbacks));
            }
            else
            {
                LOG("Halo 2 classic eye projection read-back: written cover "
                    "%.1f x %.1f deg, engine rasterised %.1f x %.1f deg (full "
                    "angles) - %s; the headset crop follows the engine "
                    "(mismatches=%llu of %llu)",
                    2.0f * values[0] * kDegrees, 2.0f * values[1] * kDegrees,
                    2.0f * values[2] * kDegrees, 2.0f * values[3] * kDegrees,
                    status == 1 ? "AGREES" : "MISMATCH: the engine did not "
                                             "render the cover the mod wrote",
                    static_cast<unsigned long long>(current.projectionMismatches),
                    static_cast<unsigned long long>(current.projectionReadbacks));
            }
        }
        g_lastTelemetryMs = now;
    }

    bool IsReadableProtection(DWORD protection) noexcept
    {
        if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        switch (protection & 0xFFu)
        {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    bool IsExecutableProtection(DWORD protection) noexcept
    {
        if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        switch (protection & 0xFFu)
        {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    struct ParsedPattern
    {
        uint8_t bytes[96]{};
        bool wildcard[96]{};
        size_t length = 0;
    };

    int HexDigit(char value) noexcept
    {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    }

    bool ParsePattern(const char* text, ParsedPattern& output) noexcept
    {
        if (!text)
            return false;
        ParsedPattern parsed{};
        const char* cursor = text;
        while (*cursor)
        {
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (!*cursor)
                break;
            if (parsed.length >= sizeof(parsed.bytes))
                return false;
            if (cursor[0] == '?' && cursor[1] == '?')
            {
                parsed.wildcard[parsed.length++] = true;
                cursor += 2;
            }
            else
            {
                const int high = HexDigit(cursor[0]);
                const int low = HexDigit(cursor[1]);
                if (high < 0 || low < 0)
                    return false;
                parsed.bytes[parsed.length++] = static_cast<uint8_t>(
                    (high << 4) | low);
                cursor += 2;
            }
            if (*cursor && *cursor != ' ' && *cursor != '\t')
                return false;
        }
        if (!parsed.length)
            return false;
        output = parsed;
        return true;
    }

    bool PatternMatches(
        const uint8_t* bytes, const ParsedPattern& pattern) noexcept
    {
        for (size_t index = 0; index < pattern.length; ++index)
        {
            if (!pattern.wildcard[index] &&
                bytes[index] != pattern.bytes[index])
            {
                return false;
            }
        }
        return true;
    }

    bool CountPattern(
        uintptr_t base, size_t size, const char* text,
        uintptr_t& first, uint32_t& count) noexcept
    {
        first = 0;
        count = 0;
        ParsedPattern pattern{};
        if (!ParsePattern(text, pattern) || !base ||
            base > UINTPTR_MAX - size || size < pattern.length)
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
                        sizeof(info)) || !info.RegionSize)
                {
                    return false;
                }
                const uintptr_t regionBase =
                    reinterpret_cast<uintptr_t>(info.BaseAddress);
                if (regionBase > UINTPTR_MAX - info.RegionSize)
                    return false;
                const uintptr_t rawRegionEnd = regionBase + info.RegionSize;
                const uintptr_t regionEnd =
                    rawRegionEnd < imageEnd ? rawRegionEnd : imageEnd;
                if (regionEnd <= cursor)
                    return false;

                if (info.State == MEM_COMMIT &&
                    reinterpret_cast<uintptr_t>(info.AllocationBase) == base &&
                    IsReadableProtection(info.Protect) &&
                    regionEnd - cursor >= pattern.length)
                {
                    const auto* bytes =
                        reinterpret_cast<const uint8_t*>(cursor);
                    const size_t last = static_cast<size_t>(
                        regionEnd - cursor - pattern.length);
                    for (size_t offset = 0; offset <= last; ++offset)
                    {
                        if (!PatternMatches(bytes + offset, pattern))
                            continue;
                        if (!first)
                            first = cursor + offset;
                        if (++count >= 2)
                            return true;
                    }
                }
                cursor = regionEnd;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            first = 0;
            count = 0;
            return false;
        }
        return true;
    }

    bool VerifyPeIdentity(uintptr_t base, size_t size) noexcept
    {
        if (!base || size != kHalo2RetailImageSize)
            return false;
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
                static_cast<size_t>(dos->e_lfanew) >
                    size - sizeof(IMAGE_NT_HEADERS64))
            {
                return false;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<uint32_t>(dos->e_lfanew));
            return nt->Signature == IMAGE_NT_SIGNATURE &&
                nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                nt->FileHeader.TimeDateStamp == kHalo2RetailPeTimestamp &&
                nt->OptionalHeader.SizeOfImage == kHalo2RetailImageSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VerifyExecutableTarget(
        uintptr_t base, size_t size, uint32_t targetRva) noexcept
    {
        if (!base || targetRva >= size || base > UINTPTR_MAX - size)
            return false;
        const uintptr_t target = base + targetRva;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(
                reinterpret_cast<const void*>(target), &info, sizeof(info)) ||
            info.State != MEM_COMMIT ||
            reinterpret_cast<uintptr_t>(info.AllocationBase) != base ||
            !IsExecutableProtection(info.Protect))
        {
            return false;
        }
        const uintptr_t regionBase =
            reinterpret_cast<uintptr_t>(info.BaseAddress);
        return regionBase <= target &&
            regionBase <= UINTPTR_MAX - info.RegionSize &&
            target < regionBase + info.RegionSize;
    }

    bool VerifyCallEdge(
        uintptr_t base, size_t size, uint32_t callRva,
        uint32_t targetRva) noexcept
    {
        if (!base || callRva > size || size - callRva < 5)
            return false;
        __try
        {
            const auto* call = reinterpret_cast<const uint8_t*>(
                base + callRva);
            if (call[0] != 0xE8)
                return false;
            int32_t displacement = 0;
            std::memcpy(&displacement, call + 1, sizeof(displacement));
            const uintptr_t returnAddress = base + callRva + 5;
            const uintptr_t decoded = static_cast<uintptr_t>(
                static_cast<intptr_t>(returnAddress) + displacement);
            return decoded == base + targetRva;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VerifyUniqueAnchor(
        uintptr_t base, size_t size, size_t anchorIndex) noexcept
    {
        const Halo2RetailAnchor& anchor = kHalo2RetailAnchors[anchorIndex];
        uintptr_t first = 0;
        uint32_t count = 0;
        return CountPattern(base, size, anchor.pattern, first, count) &&
            count == 1 && first == base + anchor.rva;
    }

    bool VerifyBackbufferRtvSlot(
        uintptr_t base, size_t size, uintptr_t& slotAddress) noexcept
    {
        slotAddress = 0;
        uintptr_t first = 0;
        uint32_t count = 0;
        if (!CountPattern(
                base, size, kHalo2BackbufferRtvLoadPattern,
                first, count) ||
            count != 1 ||
            first != base + kHalo2FinalOutputBackbufferLoadRva)
        {
            return false;
        }
        __try
        {
            int32_t displacement = 0;
            std::memcpy(
                &displacement, reinterpret_cast<const void*>(first + 3),
                sizeof(displacement));
            const uintptr_t decoded = static_cast<uintptr_t>(
                static_cast<intptr_t>(first + 7) + displacement);
            if (decoded != base + kHalo2BackbufferRtvSlotRva ||
                kHalo2BackbufferRtvSlotRva > size ||
                size - kHalo2BackbufferRtvSlotRva < sizeof(void*))
            {
                return false;
            }
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(
                    reinterpret_cast<const void*>(decoded), &info,
                    sizeof(info)) ||
                info.State != MEM_COMMIT ||
                reinterpret_cast<uintptr_t>(info.AllocationBase) != base ||
                !IsReadableProtection(info.Protect))
            {
                return false;
            }
            slotAddress = decoded;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            slotAddress = 0;
            return false;
        }
    }

    bool DisableStatusIsSafe(MH_STATUS status) noexcept
    {
        return status == MH_OK || status == MH_ERROR_DISABLED ||
            status == MH_ERROR_NOT_CREATED;
    }

    bool RemoveStatusIsSafe(MH_STATUS status) noexcept
    {
        return status == MH_OK || status == MH_ERROR_NOT_CREATED;
    }

    void AcquireCleanupModulePin() noexcept
    {
        if (g_cleanupModulePin || !g_moduleReference)
            return;

        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(g_moduleReference), &module))
        {
            return;
        }
        if (module != g_moduleReference)
        {
            if (module)
                FreeLibrary(module);
            return;
        }
        g_cleanupModulePin = module;
    }

    void ReleaseCleanupModulePin() noexcept
    {
        HMODULE module = g_cleanupModulePin;
        g_cleanupModulePin = nullptr;
        if (module)
            FreeLibrary(module);
    }

    bool DrainCallbacks() noexcept
    {
        const uint64_t deadline = GetTickCount64() + kCallbackDrainLimitMs;
        while (g_activeCallbacks.load(std::memory_order_acquire) != 0 &&
               GetTickCount64() < deadline)
        {
            Sleep(1);
        }
        return g_activeCallbacks.load(std::memory_order_acquire) == 0;
    }

    bool RemoveCore(const char* reason) noexcept
    {
        // Stage 3I: hold halo2.dll mapped across MinHook disable/remove. The
        // pin is retained on cleanup failure and released only after every
        // Classic hook record has been retired.
        AcquireCleanupModulePin();
        (void)RestoreFirstPersonFovConstant();
        g_fpConstantPinned.store(false, std::memory_order_release);
        g_stereoRequested.store(false, std::memory_order_release);
        g_presentationReady.store(false, std::memory_order_release);
        g_teardownRequested.store(true, std::memory_order_release);
        g_armed.store(false, std::memory_order_release);
        ResetHeadReferenceAtomic();
        VR_ResetHalo2SynchronousStereo();

        if (g_firstPersonTarget)
            (void)MH_DisableHook(g_firstPersonTarget);
        const MH_STATUS outerDisabled = g_outerTarget
            ? MH_DisableHook(g_outerTarget)
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS innerDisabled = g_innerTarget
            ? MH_DisableHook(g_innerTarget)
            : MH_ERROR_NOT_CREATED;
        if (!DisableStatusIsSafe(outerDisabled) ||
            !DisableStatusIsSafe(innerDisabled))
        {
            g_coreState = CoreState::CleanupRequired;
            LOG("Halo 2 stereo cleanup REQUIRED (%s): disable outer=%d "
                "inner=%d; module pin and hook records retained",
                reason ? reason : "unspecified",
                static_cast<int>(outerDisabled),
                static_cast<int>(innerDisabled));
            return false;
        }

        if (!DrainCallbacks())
        {
            g_coreState = CoreState::CleanupRequired;
            LOG("Halo 2 stereo cleanup REQUIRED (%s): %u callbacks did not "
                "drain within %u ms; module pin and trampolines retained",
                reason ? reason : "unspecified",
                g_activeCallbacks.load(std::memory_order_relaxed),
                kCallbackDrainLimitMs);
            return false;
        }

        bool removed = true;
        if (g_firstPersonTarget)
        {
            const MH_STATUS status = MH_RemoveHook(g_firstPersonTarget);
            if (RemoveStatusIsSafe(status))
            {
                g_firstPersonTarget = nullptr;
                g_firstPersonOriginal.store(0, std::memory_order_release);
            }
            else
            {
                removed = false;
                LOG("Halo 2 stereo cleanup REQUIRED (%s): first-person "
                    "remove=%d", reason ? reason : "unspecified",
                    static_cast<int>(status));
            }
        }
        if (g_innerTarget)
        {
            const MH_STATUS status = MH_RemoveHook(g_innerTarget);
            if (RemoveStatusIsSafe(status))
            {
                g_innerTarget = nullptr;
                g_innerOriginalAddress.store(0, std::memory_order_release);
            }
            else
            {
                removed = false;
                LOG("Halo 2 stereo cleanup REQUIRED (%s): inner remove=%d",
                    reason ? reason : "unspecified",
                    static_cast<int>(status));
            }
        }
        if (g_outerTarget)
        {
            const MH_STATUS status = MH_RemoveHook(g_outerTarget);
            if (RemoveStatusIsSafe(status))
            {
                g_outerTarget = nullptr;
                g_outerOriginalAddress.store(0, std::memory_order_release);
            }
            else
            {
                removed = false;
                LOG("Halo 2 stereo cleanup REQUIRED (%s): outer remove=%d",
                    reason ? reason : "unspecified",
                    static_cast<int>(status));
            }
        }
        if (!removed || g_innerTarget || g_outerTarget || g_firstPersonTarget)
        {
            g_coreState = CoreState::CleanupRequired;
            return false;
        }

        g_installed.store(false, std::memory_order_release);
        g_generation.store(0, std::memory_order_release);
        g_moduleBase.store(0, std::memory_order_release);
        g_backbufferRtvSlotAddress.store(0, std::memory_order_release);
        g_seenSerial.store(0, std::memory_order_release);
        g_lastCompletedPairSerial.store(0, std::memory_order_release);
        g_serialGapExpected.store(0, std::memory_order_release);
        g_serialGapObserved.store(0, std::memory_order_release);
        g_coreState = CoreState::StockFallback;
        // Non-owning identity; MCC alone owns the title DLL lifetime.
        g_moduleReference = nullptr;
        ReleaseCleanupModulePin();
        LOG("Halo 2 stereo core removed (%s); stock rendering restored",
            reason ? reason : "unspecified");
        return true;
    }

    bool RetainFailedCreateForCleanup(
        HMODULE module, uintptr_t base, uint32_t generation,
        void* target, bool innerTarget) noexcept
    {
        g_moduleReference = module;
        g_moduleBase.store(base, std::memory_order_release);
        g_generation.store(generation, std::memory_order_release);
        g_installed.store(true, std::memory_order_release);
        g_teardownRequested.store(true, std::memory_order_release);
        g_coreState = CoreState::CleanupRequired;
        g_rejectedGeneration = generation;
        if (innerTarget)
            g_innerTarget = target;
        else
            g_outerTarget = target;
        return false;
    }

    bool InstallCore(
        uintptr_t base, size_t size, uint32_t generation) noexcept
    {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &module) ||
            reinterpret_cast<uintptr_t>(module) != base)
        {
            if (g_pinFailureLoggedGeneration != generation)
            {
                LOG("Halo 2 stereo install WITHHELD: module pin failed for "
                    "generation %u", generation);
                g_pinFailureLoggedGeneration = generation;
            }
            g_rejectedGeneration = generation;
            return false;
        }

        uintptr_t backbufferSlot = 0;
        const bool peIdentity = VerifyPeIdentity(base, size);
        const bool playerAnchor = VerifyUniqueAnchor(
            base, size, kHalo2AnchorPlayerWindow);
        const bool renderViewAnchor = VerifyUniqueAnchor(
            base, size, kHalo2AnchorRenderView);
        const bool playerEdge = VerifyCallEdge(
            base, size, kHalo2RetailRenderPlayerWindowCallRva,
            kHalo2RetailRenderPlayerWindowRva);
        const bool renderViewEdge = VerifyCallEdge(
            base, size, kHalo2RetailRenderViewCallRva,
            kHalo2RetailRenderViewRva);
        const bool playerExecutable = VerifyExecutableTarget(
            base, size, kHalo2RetailRenderPlayerWindowRva);
        const bool renderViewExecutable = VerifyExecutableTarget(
            base, size, kHalo2RetailRenderViewRva);
        const bool backbufferProof = VerifyBackbufferRtvSlot(
            base, size, backbufferSlot);
        if (!peIdentity || !playerAnchor || !renderViewAnchor ||
            !playerEdge || !renderViewEdge || !playerExecutable ||
            !renderViewExecutable || !backbufferProof)
        {
            LOG("Halo 2 stereo install WITHHELD: generation %u proof "
                "pe=%d playerAnchor=%d viewAnchor=%d playerEdge=%d "
                "viewEdge=%d playerExec=%d viewExec=%d backbuffer=%d",
                generation, peIdentity ? 1 : 0, playerAnchor ? 1 : 0,
                renderViewAnchor ? 1 : 0, playerEdge ? 1 : 0,
                renderViewEdge ? 1 : 0, playerExecutable ? 1 : 0,
                renderViewExecutable ? 1 : 0,
                backbufferProof ? 1 : 0);
            g_rejectedGeneration = generation;
            return false;
        }
        if (g_outerTarget || g_innerTarget || g_firstPersonTarget ||
            g_moduleReference ||
            g_outerOriginalAddress.load(std::memory_order_acquire) ||
            g_innerOriginalAddress.load(std::memory_order_acquire))
        {
            LOG("Halo 2 stereo install WITHHELD: prior hook ownership remains; "
                "worker cleanup must finish first");
            g_coreState = CoreState::CleanupRequired;
            return false;
        }

        void* const innerTarget = reinterpret_cast<void*>(
            base + kHalo2RetailRenderViewRva);
        void* const outerTarget = reinterpret_cast<void*>(
            base + kHalo2RetailRenderPlayerWindowRva);
        void* innerTrampoline = nullptr;
        MH_STATUS createdInner = MH_CreateHook(
            innerTarget, reinterpret_cast<void*>(&Halo2RenderViewDetour),
            &innerTrampoline);
        if (createdInner != MH_OK || !innerTrampoline)
        {
            MH_STATUS rollback = MH_ERROR_NOT_CREATED;
            if (createdInner == MH_OK)
                rollback = MH_RemoveHook(innerTarget);
            if (createdInner == MH_OK && !RemoveStatusIsSafe(rollback))
            {
                LOG("Halo 2 stereo install: inner create returned no "
                    "trampoline and rollback failed (%d); retaining pin",
                    static_cast<int>(rollback));
                return RetainFailedCreateForCleanup(
                    module, base, generation, innerTarget, true);
            }
            LOG("Halo 2 stereo install WITHHELD: inner create=%d rollback=%d",
                static_cast<int>(createdInner), static_cast<int>(rollback));
            g_rejectedGeneration = generation;
            return false;
        }

        void* outerTrampoline = nullptr;
        MH_STATUS createdOuter = MH_CreateHook(
            outerTarget, reinterpret_cast<void*>(&Halo2PlayerWindowDetour),
            &outerTrampoline);
        if (createdOuter != MH_OK || !outerTrampoline)
        {
            MH_STATUS outerRollback = MH_ERROR_NOT_CREATED;
            if (createdOuter == MH_OK)
                outerRollback = MH_RemoveHook(outerTarget);
            const MH_STATUS innerRollback = MH_RemoveHook(innerTarget);
            if ((createdOuter == MH_OK &&
                 !RemoveStatusIsSafe(outerRollback)) ||
                !RemoveStatusIsSafe(innerRollback))
            {
                g_moduleReference = module;
                g_moduleBase.store(base, std::memory_order_release);
                g_generation.store(generation, std::memory_order_release);
                g_installed.store(true, std::memory_order_release);
                g_teardownRequested.store(true, std::memory_order_release);
                if (!RemoveStatusIsSafe(outerRollback))
                    g_outerTarget = outerTarget;
                if (!RemoveStatusIsSafe(innerRollback))
                    g_innerTarget = innerTarget;
                g_coreState = CoreState::CleanupRequired;
                g_rejectedGeneration = generation;
                LOG("Halo 2 stereo install cleanup REQUIRED: outer create=%d "
                    "outer rollback=%d inner rollback=%d",
                    static_cast<int>(createdOuter),
                    static_cast<int>(outerRollback),
                    static_cast<int>(innerRollback));
                return false;
            }
            LOG("Halo 2 stereo install WITHHELD: outer create=%d rollback=%d",
                static_cast<int>(createdOuter),
                static_cast<int>(outerRollback));
            g_rejectedGeneration = generation;
            return false;
        }

        // E-H2-33 (C-H2-38): the FOV constant pin verifies draw_first_person's
        // entry bytes, and the weapon-pass hook below REWRITES those bytes.
        // C-H2-36 and C-H2-37 hooked first and pinned second, so the pin
        // failed every run ("NOT pinned ... the weapon keeps the engine's
        // 49.6 deg") and the classic gun was drawn 3.1x the eye's frustum.
        // The pin must read the module before anything of ours is in it.
        (void)PinFirstPersonFovConstant(base, generation);

        // E-H2-31: the first-person weapon pass, pinned by the same entry
        // bytes the FOV constant pin has just verified. Its absence is loud
        // and leaves the weapon exactly as the previous candidate drew it.
        {
            const uintptr_t firstPerson = base + kHalo2ClassicDrawFirstPersonRva;
            uint8_t entry[sizeof(kHalo2ClassicDrawFirstPersonEntryBytes)]{};
            bool readable = false;
            __try
            {
                std::memcpy(entry, reinterpret_cast<const void*>(firstPerson),
                            sizeof(entry));
                readable = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { readable = false; }
            if (!readable ||
                std::memcmp(entry, kHalo2ClassicDrawFirstPersonEntryBytes,
                            sizeof(entry)) != 0)
            {
                LOG("Halo 2 classic first-person weapon pass NOT owned: the "
                    "entry bytes at +0x%X are not draw_first_person in this "
                    "module; the weapon keeps a full per-eye separation",
                    static_cast<unsigned>(kHalo2ClassicDrawFirstPersonRva));
            }
            else
            {
                void* const target = reinterpret_cast<void*>(firstPerson);
                void* trampoline = nullptr;
                const MH_STATUS created = MH_CreateHook(
                    target,
                    reinterpret_cast<void*>(&Halo2DrawFirstPersonDetour),
                    &trampoline);
                if (created != MH_OK || !trampoline)
                {
                    LOG("Halo 2 classic first-person weapon pass NOT owned: "
                        "hook create=%d", static_cast<int>(created));
                    if (created == MH_OK)
                        (void)MH_RemoveHook(target);
                }
                else
                {
                    g_firstPersonTarget = target;
                    g_firstPersonOriginal.store(
                        reinterpret_cast<uintptr_t>(trampoline),
                        std::memory_order_release);
                    if (MH_EnableHook(target) != MH_OK)
                    {
                        LOG("Halo 2 classic first-person weapon pass NOT "
                            "owned: hook enable failed");
                        (void)MH_RemoveHook(target);
                        g_firstPersonTarget = nullptr;
                        g_firstPersonOriginal.store(0, std::memory_order_release);
                    }
                    else
                    {
                        LOG("Halo 2 classic first-person weapon pass pinned at "
                            "draw_first_person +0x%X (camera centring %s): the "
                            "weapon geometry is instead moved per eye pass by "
                            "the interpolator re-anchor, from the previous "
                            "render_view's popped camera - the one this pass "
                            "draws from (E-H2-34) - to this eye's own",
                            static_cast<unsigned>(kHalo2ClassicDrawFirstPersonRva),
                            kHalo2ClassicCentreFirstPersonCameras ? "ON" : "OFF");
                    }
                }
            }
        }

        g_moduleReference = module;
        g_innerTarget = innerTarget;
        g_outerTarget = outerTarget;
        g_innerOriginalAddress.store(
            reinterpret_cast<uintptr_t>(innerTrampoline),
            std::memory_order_release);
        g_outerOriginalAddress.store(
            reinterpret_cast<uintptr_t>(outerTrampoline),
            std::memory_order_release);
        g_moduleBase.store(base, std::memory_order_release);
        g_generation.store(generation, std::memory_order_release);
        g_backbufferRtvSlotAddress.store(
            backbufferSlot, std::memory_order_release);
        g_seenSerial.store(0, std::memory_order_release);
        g_lastCompletedPairSerial.store(0, std::memory_order_release);
        g_serialGapExpected.store(0, std::memory_order_release);
        g_serialGapObserved.store(0, std::memory_order_release);
        g_teardownRequested.store(false, std::memory_order_release);
        g_installed.store(true, std::memory_order_release);
        g_coreState = CoreState::CleanupRequired;

        // Reset the generation's baseline before MH_ApplyQueued can expose
        // either detour. Resetting after Apply races the first live callback and
        // can erase exactly the evidence this telemetry exists to preserve.
        ResetTelemetryForInstall();
        const MH_STATUS queuedInner = MH_QueueEnableHook(innerTarget);
        const MH_STATUS queuedOuter = queuedInner == MH_OK
            ? MH_QueueEnableHook(outerTarget)
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS applied =
            queuedInner == MH_OK && queuedOuter == MH_OK
                ? MH_ApplyQueued()
                : MH_ERROR_NOT_CREATED;
        if (queuedInner != MH_OK || queuedOuter != MH_OK || applied != MH_OK)
        {
            LOG("Halo 2 stereo install: enable failed inner=%d outer=%d "
                "apply=%d; rolling back",
                static_cast<int>(queuedInner),
                static_cast<int>(queuedOuter), static_cast<int>(applied));
            if (!RemoveCore("install rollback"))
                g_coreState = CoreState::CleanupRequired;
            g_rejectedGeneration = generation;
            return false;
        }

        g_coreState = CoreState::Installed;
        ResetHeadReferenceAtomic();
        g_armed.store(true, std::memory_order_release);
        LOG("Halo 2 stereo installed: outer +0x%X/return +0x%X, inner "
            "+0x%X/return +0x%X, exact backbuffer RTV slot +0x%X; one "
            "player scope, two same-serial render_view calls; six independent "
            "12-byte position/forward/up spans plus two independent 4-byte "
            "vertical-FOV +0x28 cover fields are restored; z_far stays owned "
            "by H2",
            static_cast<unsigned>(kHalo2RetailRenderPlayerWindowRva),
            static_cast<unsigned>(kHalo2RetailRenderPlayerWindowReturnRva),
            static_cast<unsigned>(kHalo2RetailRenderViewRva),
            static_cast<unsigned>(kHalo2RetailRenderViewReturnRva),
            static_cast<unsigned>(kHalo2BackbufferRtvSlotRva));
        return true;
    }

    bool DrainNativeHudCallbacks() noexcept
    {
        const uint64_t deadline = GetTickCount64() + kCallbackDrainLimitMs;
        while (g_nativeHudActiveCallbacks.load(std::memory_order_acquire) != 0 &&
               GetTickCount64() < deadline)
        {
            Sleep(1);
        }
        return g_nativeHudActiveCallbacks.load(std::memory_order_acquire) == 0;
    }

    void ReportNativeHud() noexcept
    {
        const uint64_t now = GetTickCount64();
        if (g_nativeHudLastReportMs &&
            now - g_nativeHudLastReportMs < kTelemetryPeriodMs)
        {
            return;
        }
        const uint64_t scoped =
            g_nativeHudScopedDraws.load(std::memory_order_relaxed);
        const uint64_t stock =
            g_nativeHudStockDraws.load(std::memory_order_relaxed);
        uint64_t gameplayShaders = 0;
        uint64_t crosshairShaders = 0;
        uint64_t rasterDraws = 0;
        uint64_t crosshairDraws = 0;
        uint64_t stateFailures = 0;
        D3D_GetHalo2HudTelemetry(
            gameplayShaders, crosshairShaders, rasterDraws,
            crosshairDraws, stateFailures);
        const uint64_t total = scoped + stock + gameplayShaders +
            crosshairShaders + rasterDraws + crosshairDraws + stateFailures;
        if (total != g_nativeHudLastReportedTotal)
        {
            LOG("Halo 2 native HUD CHUD scope telemetry: scopedDraws=%llu "
                "stockDraws=%llu gameplayShaders=%llu crosshairShaders=%llu "
                "rasterTransforms=%llu nativeCrosshairCaptures=%llu "
                "stateFailures=%llu",
                static_cast<unsigned long long>(scoped),
                static_cast<unsigned long long>(stock),
                static_cast<unsigned long long>(gameplayShaders),
                static_cast<unsigned long long>(crosshairShaders),
                static_cast<unsigned long long>(rasterDraws),
                static_cast<unsigned long long>(crosshairDraws),
                static_cast<unsigned long long>(stateFailures));
            g_nativeHudLastReportedTotal = total;
        }
        g_nativeHudLastReportMs = now;
    }

    bool RemoveNativeHud(const char* reason) noexcept
    {
        g_nativeHudArmed.store(false, std::memory_order_release);
        g_nativeHudTeardown.store(true, std::memory_order_release);
        const MH_STATUS drawDisabled = g_nativeChudTarget
            ? MCCVR_DisableHookForRetirement(g_nativeChudTarget)
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS anchorDisabled = g_nativeHudAnchorTarget
            ? MCCVR_DisableHookForRetirement(g_nativeHudAnchorTarget)
            : MH_ERROR_NOT_CREATED;
        if (!DisableStatusIsSafe(drawDisabled) ||
            !DisableStatusIsSafe(anchorDisabled))
        {
            LOG("Halo 2 native HUD cleanup REQUIRED (%s): disable draw=%d "
                "anchor=%d; ownership retained",
                reason ? reason : "unspecified",
                static_cast<int>(drawDisabled),
                static_cast<int>(anchorDisabled));
            return false;
        }
        if (!DrainNativeHudCallbacks())
        {
            LOG("Halo 2 native HUD cleanup REQUIRED (%s): %u callbacks did "
                "not drain; ownership retained",
                reason ? reason : "unspecified",
                g_nativeHudActiveCallbacks.load(std::memory_order_relaxed));
            return false;
        }

        if (g_nativeChudTarget || g_nativeHudAnchorTarget)
        {
            const void* functions[]{reinterpret_cast<const void*>(&NativeChudDrawDetour),
                reinterpret_cast<const void*>(&NativeHudAnchorBasisDetour)};
            const void* originals[]{reinterpret_cast<const void*>(g_nativeChudOriginal.load()),
                reinterpret_cast<const void*>(g_nativeHudAnchorOriginal.load())};
            if (!WaitForNativeDetourQuiescence(functions, originals, 2, g_nativeHudActiveCallbacks))
            {
                LOG("Halo 2 native HUD cleanup: ingress still busy; retained");
                return false;
            }
        }
        bool removed = true;
        if (g_nativeHudAnchorTarget)
        {
            const MH_STATUS status = MH_RemoveHook(g_nativeHudAnchorTarget);
            if (RemoveStatusIsSafe(status))
            {
                g_nativeHudAnchorTarget = nullptr;
                g_nativeHudAnchorOriginal.store(0, std::memory_order_release);
            }
            else
            {
                removed = false;
                LOG("Halo 2 native HUD cleanup REQUIRED (%s): anchor "
                    "remove=%d", reason ? reason : "unspecified",
                    static_cast<int>(status));
            }
        }
        if (g_nativeChudTarget)
        {
            const MH_STATUS status = MH_RemoveHook(g_nativeChudTarget);
            if (RemoveStatusIsSafe(status))
            {
                g_nativeChudTarget = nullptr;
                g_nativeChudOriginal.store(0, std::memory_order_release);
            }
            else
            {
                removed = false;
                LOG("Halo 2 native HUD cleanup REQUIRED (%s): draw remove=%d",
                    reason ? reason : "unspecified", static_cast<int>(status));
            }
        }
        if (!removed || g_nativeChudTarget || g_nativeHudAnchorTarget)
            return false;

        g_nativeHudInstalled.store(false, std::memory_order_release);
        g_nativeHudGeneration.store(0, std::memory_order_release);
        g_nativeHudModuleBase.store(0, std::memory_order_release);
        g_nativeHudModuleReference = nullptr;
        LOG("Halo 2 native HUD CHUD draw ownership removed (%s); stock layout "
            "restored", reason ? reason : "unspecified");
        return true;
    }

    bool InstallNativeHud(
        uintptr_t base, size_t size, uint32_t generation) noexcept
    {
        if (g_nativeHudRejectedGeneration == generation)
            return false;
        const uintptr_t draw = base + kHalo2RetailNativeChudDrawRva;
        const bool drawPinned = VerifyExecutableTarget(
                base, size, kHalo2RetailNativeChudDrawRva) &&
            NativeHudEntryMatches(
                draw, kHalo2RetailNativeChudDrawEntryBytes);
        if (!drawPinned)
        {
            LOG("Halo 2 native HUD WITHHELD: proven retail CHUD draw "
                "(+0x%X) did not match; stock layout remains",
                static_cast<unsigned>(kHalo2RetailNativeChudDrawRva));
            g_nativeHudRejectedGeneration = generation;
            return false;
        }
        if (g_nativeChudTarget || g_nativeHudAnchorTarget ||
            g_nativeHudModuleReference)
        {
            LOG("Halo 2 native HUD WITHHELD: prior shared ownership remains; "
                "cleanup must finish first");
            return false;
        }

        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &module) ||
            reinterpret_cast<uintptr_t>(module) != base)
        {
            LOG("Halo 2 native HUD WITHHELD: module identity pin failed");
            g_nativeHudRejectedGeneration = generation;
            return false;
        }

        void* drawTrampoline = nullptr;
        const MH_STATUS createdDraw = MH_CreateHook(
            reinterpret_cast<void*>(draw),
            reinterpret_cast<void*>(&NativeChudDrawDetour), &drawTrampoline);
        if (createdDraw != MH_OK || !drawTrampoline)
        {
            MH_STATUS drawRollback = MH_ERROR_NOT_CREATED;
            if (createdDraw == MH_OK)
                drawRollback = MH_RemoveHook(reinterpret_cast<void*>(draw));
            LOG("Halo 2 native HUD WITHHELD: CHUD draw hook create=%d "
                "rollback=%d",
                static_cast<int>(createdDraw),
                static_cast<int>(drawRollback));
            if (createdDraw == MH_OK &&
                !RemoveStatusIsSafe(drawRollback))
            {
                g_nativeHudModuleReference = module;
                g_nativeChudTarget = reinterpret_cast<void*>(draw);
                g_nativeChudOriginal.store(
                    reinterpret_cast<uintptr_t>(drawTrampoline),
                    std::memory_order_release);
                g_nativeHudGeneration.store(
                    generation, std::memory_order_release);
                g_nativeHudModuleBase.store(base, std::memory_order_release);
                g_nativeHudInstalled.store(true, std::memory_order_release);
                g_nativeHudTeardown.store(true, std::memory_order_release);
            }
            g_nativeHudRejectedGeneration = generation;
            return false;
        }

        g_nativeHudModuleReference = module;
        g_nativeChudTarget = reinterpret_cast<void*>(draw);
        g_nativeChudOriginal.store(
            reinterpret_cast<uintptr_t>(drawTrampoline),
            std::memory_order_release);
        g_nativeHudGeneration.store(generation, std::memory_order_release);
        g_nativeHudModuleBase.store(base, std::memory_order_release);
        g_nativeHudTeardown.store(false, std::memory_order_release);
        g_nativeHudInstalled.store(true, std::memory_order_release);
        g_nativeHudScopedDraws.store(0, std::memory_order_relaxed);
        g_nativeHudStockDraws.store(0, std::memory_order_relaxed);
        g_nativeHudAnchorsAdjusted.store(0, std::memory_order_relaxed);
        g_nativeHudCrosshairsPreserved.store(0, std::memory_order_relaxed);
        g_nativeHudAnchorFailures.store(0, std::memory_order_relaxed);
        g_nativeHudLastReportMs = 0;
        g_nativeHudLastReportedTotal = UINT64_MAX;

        const MH_STATUS queuedDraw = MH_QueueEnableHook(g_nativeChudTarget);
        const MH_STATUS applied = queuedDraw == MH_OK
            ? MH_ApplyQueued()
            : MH_ERROR_NOT_CREATED;
        if (queuedDraw != MH_OK || applied != MH_OK)
        {
            LOG("Halo 2 native HUD WITHHELD: CHUD draw enable=%d apply=%d",
                static_cast<int>(queuedDraw), static_cast<int>(applied));
            (void)RemoveNativeHud("install rollback");
            g_nativeHudRejectedGeneration = generation;
            return false;
        }
        g_nativeHudArmed.store(true, std::memory_order_release);
        LOG("Halo 2 native gameplay HUD draw scope ON at +0x%X across "
            "Classic/Anniversary; Stage 3D's zero-callback anchor hook is "
            "disabled and exact proven D3D11 HUD shaders own layout",
            static_cast<unsigned>(kHalo2RetailNativeChudDrawRva));
        return true;
    }
}

bool Halo2NativeHud_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool activeAndRange, bool levelRunning, bool coldPassed) noexcept
{
    const bool desired = moduleBase && generation &&
        moduleSize == kHalo2RetailImageSize && activeAndRange &&
        levelRunning && coldPassed;
    const uint32_t owned =
        g_nativeHudGeneration.load(std::memory_order_acquire);
    const bool foreignModule = g_nativeHudInstalled.load(std::memory_order_acquire) &&
        (owned != generation ||
         g_nativeHudModuleBase.load(std::memory_order_acquire) != moduleBase);
    if (!desired || foreignModule)
    {
        if (g_nativeHudInstalled.load(std::memory_order_acquire))
        {
            (void)RemoveNativeHud(foreignModule
                ? "module generation changed"
                : "level or title no longer eligible");
        }
        if (generation != g_nativeHudRejectedGeneration)
            g_nativeHudRejectedGeneration = 0;
        return false;
    }
    if (g_nativeHudInstalled.load(std::memory_order_acquire) &&
        g_nativeHudTeardown.load(std::memory_order_acquire) &&
        !RemoveNativeHud("cleanup retry"))
    {
        return false;
    }
    if (!g_nativeHudInstalled.load(std::memory_order_acquire) &&
        !InstallNativeHud(moduleBase, moduleSize, generation))
    {
        return false;
    }
    // Resource allocation/OpenXR swapchain creation stays on this worker, not
    // in Draw/DrawIndexed. The render hook uses only the prepared capture entry
    // and otherwise leaves the native draw plus procedural fallback unchanged.
    if (D3D_Halo2HudShaderPathAvailable() &&
        g_nativeHudCrosshairPreparedGeneration != generation &&
        VR_CanPrepareAuthoredReticleResources())
    {
        const AuthoredReticlePreparationResult preparation =
            VR_PrepareAuthoredReticleResources();
        if (preparation == AuthoredReticlePreparationResult::Ready)
        {
            g_nativeHudCrosshairPreparedGeneration = generation;
            LOG("Halo 2 native crosshair capture resources cold-prepared for "
                "generation %u", generation);
        }
        else if (preparation == AuthoredReticlePreparationResult::Failed &&
                 g_nativeHudCrosshairPreparationFailedGeneration != generation)
        {
            g_nativeHudCrosshairPreparationFailedGeneration = generation;
            LOG("Halo 2 native crosshair capture resources FAILED for "
                "generation %u; retaining the procedural fallback", generation);
        }
    }
    const bool armed = g_nativeHudInstalled.load(std::memory_order_acquire) &&
        g_nativeHudGeneration.load(std::memory_order_acquire) == generation &&
        !g_nativeHudTeardown.load(std::memory_order_acquire);
    g_nativeHudArmed.store(armed, std::memory_order_release);
    if (armed)
        ReportNativeHud();
    return armed;
}

bool Halo2NativeHud_Armed() noexcept
{
    return g_nativeHudArmed.load(std::memory_order_acquire) &&
        g_nativeHudInstalled.load(std::memory_order_acquire) &&
        !g_nativeHudTeardown.load(std::memory_order_acquire);
}

bool Halo2NativeHud_GetRasterLayout(
    Halo2CameraRectangle& source,
    Halo2CameraRectangle& layout) noexcept
{
    if (!Halo2NativeHud_Armed() ||
        !g_nativeHudEyeContext.active ||
        !g_nativeHudEyeContext.layoutActive)
    {
        return false;
    }
    source = g_nativeHudEyeContext.source;
    layout = g_nativeHudEyeContext.layout;
    return true;
}

void Halo2NativeHud_BeginEye(const Halo2SymmetricHalfFovs& cover) noexcept
{
    g_nativeHudEyeContext = {};
    if (Halo2NativeHud_Armed())
    {
        g_nativeHudEyeContext.active = true;
        g_nativeHudEyeContext.cover = cover;
    }
}

void Halo2NativeHud_EndEye() noexcept
{
    g_nativeHudEyeContext = {};
}

bool Halo2NativeHud_DrawPlayer(
    uint32_t generation, int32_t player,
    const Halo2CameraRectangle& source,
    const Halo2SymmetricHalfFovs& cover) noexcept
{
    if (!Halo2NativeHud_Armed() ||
        g_nativeHudGeneration.load(std::memory_order_acquire) != generation ||
        !g_nativeChudTarget)
    {
        return false;
    }
    const auto draw = reinterpret_cast<NativeChudDrawFn>(g_nativeChudTarget);
    bool ok = false;
    __try
    {
        Halo2NativeHud_BeginEye(cover);
        __try
        {
            // H2EK +0x2EA955 / retail +0x7FFD70: player selector, -1 to
            // derive the gameplay CHUD slot, and the original render bounds.
            draw(player, -1, &source);
            ok = true;
        }
        __finally
        {
            Halo2NativeHud_EndEye();
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

bool Halo2Stereo_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool activeAndRange, bool levelRunning, bool coldPassed,
    bool classicRenderTreeRuns) noexcept
{
    static uint32_t reportedScopeFaults=0;
    const auto scopeFaults=g_zoomFaults.load();
    if(scopeFaults!=reportedScopeFaults)
    { reportedScopeFaults=scopeFaults;LOG("Halo 2 Classic zoom stock fallback: optional scope faults=%u generation=%u; stereo core retained",scopeFaults,g_zoomFaultGeneration.load()); }
    const uint32_t ownedGeneration =
        g_generation.load(std::memory_order_acquire);
    uint32_t vrFailureGeneration =
        g_vrFailureGeneration.load(std::memory_order_acquire);
    if (vrFailureGeneration && generation != vrFailureGeneration && generation)
    {
        g_vrFailureGeneration.compare_exchange_strong(
            vrFailureGeneration, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        vrFailureGeneration =
            g_vrFailureGeneration.load(std::memory_order_acquire);
    }

    uint64_t runtimeQuarantine =
        g_runtimeQuarantine.load(std::memory_order_acquire);
    const uint32_t quarantineGeneration =
        RuntimeQuarantineGeneration(runtimeQuarantine);
    if (quarantineGeneration && generation &&
        quarantineGeneration != generation)
    {
        g_runtimeQuarantine.compare_exchange_strong(
            runtimeQuarantine, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        runtimeQuarantine =
            g_runtimeQuarantine.load(std::memory_order_acquire);
    }

    const bool identityValid = moduleBase && generation &&
        moduleSize == kHalo2RetailImageSize;
    const bool vrAvailable = !vrFailureGeneration ||
        generation != vrFailureGeneration;
    if (PollManualVrRecovery(g_manualRecoveryGeneration, generation,
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && vrAvailable,
            identityValid && activeAndRange,
            [] { return RemoveCore("manual VR recovery"); },
            [] { g_rejectedGeneration = 0; }) == ManualVrRecoveryPoll::Waiting)
        return false;
    // E-H2-3: this core owns render_player_window / render_view, which the
    // engine skips entirely while the remastered renderer is live. Without
    // this gate the hooks install, arm, take over presentation, and then
    // receive zero callbacks - exactly the headset-rejected C-H2-6 result.
    const bool desired = identityValid && activeAndRange && levelRunning &&
        coldPassed && vrAvailable && classicRenderTreeRuns;
    if (identityValid && activeAndRange && levelRunning && coldPassed &&
        vrAvailable && !classicRenderTreeRuns &&
        g_remasteredNoticeGeneration != generation)
    {
        g_remasteredNoticeGeneration = generation;
        LOG("Halo 2 stereo stays STOCK this generation: the remastered "
            "Anniversary renderer owns the frame, so the classic "
            "render_player_window/render_view transaction this core hooks "
            "is never executed. Switch Halo 2 to Classic graphics to use "
            "this stereo path; Anniversary stereo needs its own binding");
    }
    const bool runtimeQuarantined = generation &&
        RuntimeQuarantineGeneration(runtimeQuarantine) == generation;
    const bool ownsDifferentModule = (g_outerTarget || g_innerTarget) &&
        (ownedGeneration != generation ||
         g_moduleBase.load(std::memory_order_acquire) != moduleBase);
    const bool hotEligible = desired && !runtimeQuarantined &&
        !ownsDifferentModule && g_coreState == CoreState::Installed &&
        g_presentationReady.load(std::memory_order_acquire);

    g_levelLive.store(levelRunning, std::memory_order_release);
    g_coldObservationPassed.store(coldPassed, std::memory_order_release);
    g_rightEyeFirst.store(
        g_config.right_eye_first, std::memory_order_release);
    g_stereoRequested.store(hotEligible, std::memory_order_release);
    ReportTelemetry();

    if (runtimeQuarantined)
    {
        // E-H2-14 (C-H2-20): a failed claimed frame is DROPPED, never a
        // reason to disarm. The generation-wide quarantine this used to
        // raise unhooked the classic core after one failed frame and left
        // the rest of the level on the stock flat screen (16:55:12 in the
        // C-H2-18 log: "classic dropped out of the hook"). AGENTS.md:
        // "reject means drop that frame and keep going". The frame that
        // failed was already rejected by its transaction; clear the word,
        // count it, say so at most every two seconds, and stay armed.
        const Halo2StereoQuarantineReason quarantineReason =
            RuntimeQuarantineReason(runtimeQuarantine);
        const char* const reason = QuarantineReasonName(quarantineReason);
        g_runtimeQuarantine.compare_exchange_strong(
            runtimeQuarantine, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        const uint32_t drops =
            g_claimedFrameFailures.fetch_add(1, std::memory_order_relaxed) + 1;
        const uint64_t nowMs = GetTickCount64();
        if (!g_claimedFrameFailureLogMs ||
            nowMs - g_claimedFrameFailureLogMs >= 2000)
        {
            g_claimedFrameFailureLogMs = nowMs;
            if (quarantineReason ==
                Halo2StereoQuarantineReason::CorePreparedSerialGap)
            {
                LOG("Halo 2 stereo: claimed frame DROPPED (%s, drop #%u this "
                    "generation): expected prepared serial %llu after the "
                    "last complete pair, saw %llu; the core stays armed and "
                    "the next frame renders its own pair",
                    reason, drops,
                    static_cast<unsigned long long>(
                        g_serialGapExpected.load(std::memory_order_acquire)),
                    static_cast<unsigned long long>(
                        g_serialGapObserved.load(std::memory_order_acquire)));
            }
            else
            {
                LOG("Halo 2 stereo: claimed frame DROPPED (%s, drop #%u this "
                    "generation); the core stays armed and the next frame "
                    "renders its own pair - it is never quarantined",
                    reason, drops);
            }
        }
    }

    if ((g_outerTarget || g_innerTarget) &&
        (!desired || ownsDifferentModule ||
         g_coreState == CoreState::CleanupRequired))
    {
        const char* reason = ownsDifferentModule
            ? "module generation changed"
            : (!vrAvailable
                   ? "OpenXR runtime failed"
                   : (!activeAndRange
                          ? "Halo 2 is no longer uniquely active"
                          : (!levelRunning
                                 ? "level liveness closed"
                                 : (!coldPassed
                                        ? "cold proof was revoked"
                                        : "cleanup retry"))));
        if (!RemoveCore(reason))
            return false;
    }

    if (TitleAdapter_GetActiveTitle() != GameTitle::Halo2) g_rejectedGeneration = 0;
    if (!desired || g_rejectedGeneration == generation)
        return false;
    if (!g_outerTarget && !g_innerTarget &&
        !InstallCore(moduleBase, moduleSize, generation))
    {
        return false;
    }
    const bool armed = g_coreState == CoreState::Installed &&
        g_installed.load(std::memory_order_acquire) &&
        g_armed.load(std::memory_order_acquire) &&
        g_generation.load(std::memory_order_acquire) == generation;
    g_stereoRequested.store(
        armed && g_presentationReady.load(std::memory_order_acquire),
        std::memory_order_release);
    return armed;
}

bool Halo2Stereo_Installed() noexcept
{
    return g_installed.load(std::memory_order_acquire);
}

void Halo2Stereo_RequestRecovery(uint32_t generation) noexcept
{ g_manualRecoveryGeneration = generation; }
bool Halo2Stereo_RecoveryPending() noexcept
{ return g_manualRecoveryGeneration != 0; }

bool Halo2Stereo_Armed() noexcept
{
    return g_armed.load(std::memory_order_acquire);
}

bool Halo2Stereo_ScopeAvailable() noexcept
{ return Halo2Stereo_Armed()&&g_generation.load()!=0&&g_zoomFaultGeneration.load()!=g_generation.load(); }

uint32_t Halo2Stereo_Generation() noexcept
{
    return g_generation.load(std::memory_order_acquire);
}

void Halo2Stereo_ShutdownForVrFailure() noexcept
{
    const uint32_t generation =
        g_generation.load(std::memory_order_acquire);
    if (generation)
        g_vrFailureGeneration.store(generation, std::memory_order_release);
    g_stereoRequested.store(false, std::memory_order_release);
    g_presentationReady.store(false, std::memory_order_release);
    g_teardownRequested.store(true, std::memory_order_release);
    g_armed.store(false, std::memory_order_release);
    ResetHeadReferenceAtomic();
    VR_ResetHalo2SynchronousStereo();
}

void Halo2Stereo_SetPresentationReady(bool ready) noexcept
{
    g_presentationReady.store(ready, std::memory_order_release);
    if (!ready)
        g_stereoRequested.store(false, std::memory_order_release);
}

void Halo2Stereo_RequestGenerationQuarantine(
    uint32_t generation, Halo2StereoQuarantineReason reason) noexcept
{
    PublishRuntimeQuarantine(generation, reason);
}

void Halo2Stereo_RequestRecenter() noexcept
{
    ResetHeadReferenceAtomic();
}

#else

bool Halo2NativeHud_Poll(
    uintptr_t, size_t, uint32_t, bool, bool, bool) noexcept
{
    return false;
}
bool Halo2NativeHud_Armed() noexcept { return false; }
void Halo2NativeHud_BeginEye(const Halo2SymmetricHalfFovs&) noexcept {}
void Halo2NativeHud_EndEye() noexcept {}
bool Halo2NativeHud_DrawPlayer(
    uint32_t, int32_t, const Halo2CameraRectangle&,
    const Halo2SymmetricHalfFovs&) noexcept
{
    return false;
}

bool Halo2Stereo_Poll(
    uintptr_t, size_t, uint32_t, bool, bool, bool, bool) noexcept
{
    return false;
}

bool Halo2Stereo_Installed() noexcept { return false; }
bool Halo2Stereo_Armed() noexcept { return false; }
bool Halo2Stereo_ScopeAvailable() noexcept { return false; }
uint32_t Halo2Stereo_Generation() noexcept { return 0; }
void Halo2Stereo_ShutdownForVrFailure() noexcept {}
void Halo2Stereo_RequestRecovery(uint32_t) noexcept {}
bool Halo2Stereo_RecoveryPending() noexcept { return false; }
void Halo2Stereo_SetPresentationReady(bool) noexcept {}
void Halo2Stereo_RequestGenerationQuarantine(
    uint32_t, Halo2StereoQuarantineReason) noexcept {}
void Halo2Stereo_RequestRecenter() noexcept {}

#endif
