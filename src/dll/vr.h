#pragma once

#include <d3d11.h>
#include <cstdint>

struct IDXGISwapChain;
struct Halo2CameraRectangle;
enum class GameTitle : uint8_t;

// Successful local slot-1 rendering only: inhibits support-grip coupling,
// independently of collision/melee. Never authorizes inventory or firing.
void VR_ObserveSecondaryWeaponPresentation(GameTitle title, uint32_t generation);

#ifndef HALOMCCVR_HALO2_STEREO6DOF
#define HALOMCCVR_HALO2_STEREO6DOF 0
#endif

// Keep the direct-copy decision shared by resource validation and Blit(). The
// predicate is deliberately data-only so a caller can decide whether an XR
// image needs an RTV before touching D3D state.
constexpr DXGI_FORMAT VrBlitFormatFamily(DXGI_FORMAT format) noexcept
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default:
        return format;
    }
}

constexpr bool VrBlitCanUseDirectCopy(
    uint32_t srcWidth, uint32_t srcHeight, DXGI_FORMAT srcFormat,
    uint32_t srcSampleCount, uint32_t dstWidth, uint32_t dstHeight,
    DXGI_FORMAT dstFormat) noexcept
{
    return srcWidth == dstWidth && srcHeight == dstHeight &&
        VrBlitFormatFamily(srcFormat) == VrBlitFormatFamily(dstFormat) &&
        srcSampleCount <= 1;
}

// Source and destination textures are mandatory for both paths. Only the
// shader path consumes an RTV; CopyResource must not be rejected for lacking
// a view it never uses.
constexpr bool VrBlitResourcesReady(
    bool sourceTexture, bool destinationTexture, bool destinationRtv,
    bool directCopy) noexcept
{
    return sourceTexture && destinationTexture &&
        (directCopy || destinationRtv);
}

// Stage 3BO: Halo 4's authored CUI capture carries visible RGB with zero alpha.
// A 512x512 Halo 4 reticle upload must therefore use the shader path, where
// alpha is reconstructed, even when CopyResource would otherwise be legal.
constexpr bool VrBlitNeedsHalo4ReticleAlphaRepair(
    bool halo4Title, uint32_t destinationWidth,
    uint32_t destinationHeight) noexcept
{
    return halo4Title && destinationWidth == 512 && destinationHeight == 512;
}

// Stage 3BQ's explicit typed RTV retry is also selected by CE's verified RGB
// authored upload. Other swapchain consumers retain their established default-
// descriptor behavior even if their first view creation fails.
constexpr bool VrHalo4ReticleRtvNeedsTypedFallback(
    bool halo4ReticleUpload, bool defaultCreationFailed,
    bool viewStillMissing) noexcept
{
    return halo4ReticleUpload && defaultCreationFailed && viewStillMissing;
}

// Stage 3BL keeps the accepted alpha-only metric for every other title. Halo
// 4 alone admits RGB ink because its CUI target was measured writing visible
// colour with a zero alpha channel.
constexpr uint32_t VrResolveAuthoredReticleCoverage(
    bool halo4Title, uint32_t alphaInk, uint32_t colorInk) noexcept
{
    return halo4Title && colorInk > alphaInk ? colorInk : alphaInk;
}

// Stage 3BU writes a completed Halo 4 eye back only in steady state. The
// caller supplies the already-latched scene-target fact from before target
// discovery runs, so a learning eye can never pass this gate.
constexpr bool VrHalo4SceneWritebackEligible(
    bool halo4Title, int eye, bool contextReady, bool sceneTargetAlreadyLatched,
    bool eyeCacheReady) noexcept
{
    return halo4Title && (eye == 0 || eye == 1) && contextReady &&
        sceneTargetAlreadyLatched && eyeCacheReady;
}

// Allocation-free equivalent of Stage 3BH's actual OM target gate.  The D3D
// hooks keep slot 0 current across every state-changing entry point used by
// MCC; the Draw hook may reframe only the exact private target selected for
// this capture mode.  In particular, authored and discard targets are not
// interchangeable.
constexpr bool VrHalo4AuthoredReticleDrawTargetMatches(
    bool halo4Title, bool captureActive, bool framingCaptured,
    bool publishesAuthored, uintptr_t boundSlot0, uintptr_t authoredRtv,
    uintptr_t discardRtv) noexcept
{
    const uintptr_t expected = publishesAuthored ? authoredRtv : discardRtv;
    return halo4Title && captureActive && framingCaptured && expected != 0 &&
        boundSlot0 == expected;
}

#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
// One immutable, exact-prepared-serial tracking sample for Halo 2's two-eye
// render transaction. Publication is double-buffered and lock-free so the
// render hooks never enter the tracking critical section or mix two frames.
struct Halo2VrEyeSnapshot
{
    float position[3]{};
    float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float fov[4]{};
    bool fovValid = false;
    // E-H2-18: the absolute OpenXR view pose of this sample (what the pair
    // is submitted with when an eye was rendered from this sample).
    float absolutePosition[3]{};
    float absoluteOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
};

struct Halo2VrRenderSnapshot
{
    bool turnPadValid = false;
    float turnX = 0.0f;
    uint64_t preparedSerial = 0;
    int64_t predictedDisplayTimeNs = 0;
    uint64_t trackingSpaceEpoch = 0;
    // Exact OpenXR timing for this prepared serial. The period is the
    // xrWaitFrame target; the delta is current minus prior predicted display
    // time and exposes half-rate delivery before the game can claim the frame.
    uint64_t predictedDisplayPeriodNs = 0;
    uint64_t predictedDisplayDeltaNs = 0;
    uint32_t generation = 0;
    Halo2VrEyeSnapshot eyes[2]{};
    float headOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float headPosition[3]{};
    bool headPoseValid = false;
    // Exact-frame controller ownership, matching the working Reach/Halo 4
    // contract. The Halo 2 packet hook must never combine a prepared eye/head
    // sample with independently resampled controller globals.
    bool rightAimValid = false;
    float rightAimOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float rightAimPosition[3]{};
    bool twoHandAimActive = false;
    bool handAlignment = false; // experimental presentation, frozen with this frame
    bool leftHanded = false; // captured with these role-routed controller poses
    bool leftControllerValid = false;
    float leftControllerOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float leftControllerPosition[3]{};
    // Mount-calibrated primary aim without support-hand coupling, from this
    // same prepared frame. Dual weapons must not inherit a two-hand latch.
    bool independentRightAimValid = false;
    float independentRightAimOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
};
#endif

#if HALOMCCVR_HALO2_STEREO6DOF
using Halo2SynchronousVrEyeSnapshot = Halo2VrEyeSnapshot;
using Halo2SynchronousVrRenderSnapshot = Halo2VrRenderSnapshot;

enum class Halo2StockScreenXrOwnership : uint8_t
{
    None = 0,
    AcquiredUnresolved,
    Released,
    FrameSubmissionUnresolved,
};

// A title-local validation failure may drop the current screen layer, but it
// cannot end OpenXR. Fatal drain is reserved for unresolved XR ownership: an
// acquired swapchain image that cannot safely be released, or a submitted
// frame whose exact xrEndFrame completion is unknown.
constexpr bool Halo2StockScreenNeedsFatalDrain(
    Halo2StockScreenXrOwnership ownership) noexcept
{
    return ownership ==
            Halo2StockScreenXrOwnership::AcquiredUnresolved ||
        ownership ==
            Halo2StockScreenXrOwnership::FrameSubmissionUnresolved;
}

struct Halo2PreparedCadenceSnapshot
{
    uint64_t preparedSerial = 0;
    uint64_t predictedDisplayPeriodNs = 0;
    uint64_t predictedDisplayDeltaNs = 0;
};

enum class Halo2SynchronousFrameDisposition : uint8_t
{
    Unclaimed = 0,
    Claimed,
    Complete,
};

// Resource-free terminal history for one exact H2 prepared frame. The live
// pair token/FOV/cache state may be reset before Present consumes this stamp;
// generation+serial remain sufficient to forbid reuse by any later frame.
struct Halo2SynchronousPresentationStamp
{
    uint32_t generation = 0;
    uint64_t preparedSerial = 0;
    Halo2SynchronousFrameDisposition disposition =
        Halo2SynchronousFrameDisposition::Unclaimed;
};

constexpr bool Halo2SynchronousPresentationStampMatches(
    uint32_t generation, uint64_t preparedSerial,
    const Halo2SynchronousPresentationStamp& presentation) noexcept
{
    return generation != 0 && preparedSerial != 0 &&
        presentation.generation == generation &&
        presentation.preparedSerial == preparedSerial &&
        (presentation.disposition ==
             Halo2SynchronousFrameDisposition::Claimed ||
         presentation.disposition ==
             Halo2SynchronousFrameDisposition::Complete);
}

enum class Halo2SynchronousPresentationDecision : uint8_t
{
    SharedDefault = 0,
    SynchronousStereo,
    StockScreen,
    Drop,
    // No pair was rendered for this prepared serial (the game runs below
    // the panel rate), but the last complete pair is intact in the eye
    // caches and fresh: re-present it with the pose it was rendered with
    // and let the compositor reproject, exactly as a missed frame is
    // handled in every other title. Never mixes eyes from two frames.
    RepeatLastPair,
};

// C-H2-6 presentation admission is exact and frame-local. A currently live
// complete pair wins; otherwise only the durable stamp carrying this exact
// generation+serial has authority. A stale/foreign stamp is Unclaimed.
constexpr Halo2SynchronousFrameDisposition
Halo2SynchronousClassifyFrame(
    bool exactLivePair, uint32_t generation, uint64_t preparedSerial,
    const Halo2SynchronousPresentationStamp& presentation) noexcept
{
    if (exactLivePair)
        return Halo2SynchronousFrameDisposition::Complete;
    if (Halo2SynchronousPresentationStampMatches(
            generation, preparedSerial, presentation))
    {
        return presentation.disposition;
    }
    return Halo2SynchronousFrameDisposition::Unclaimed;
}

constexpr Halo2SynchronousPresentationDecision
Halo2SynchronousSelectPresentation(
    bool stereoRequested, bool projectionReady, bool activeHalo2,
    bool exactLivePair,
    uint32_t generation, uint64_t preparedSerial,
    const Halo2SynchronousPresentationStamp& presentation,
    bool repeatablePair = false) noexcept
{
    const Halo2SynchronousFrameDisposition disposition =
        Halo2SynchronousClassifyFrame(
            exactLivePair, generation, preparedSerial, presentation);
    // An exact durable H2 claim still owns this prepared frame if AutoVrTick
    // changed the active title immediately before submission.
    if (!activeHalo2 &&
        disposition == Halo2SynchronousFrameDisposition::Unclaimed)
    {
        return Halo2SynchronousPresentationDecision::SharedDefault;
    }
    switch (disposition)
    {
    case Halo2SynchronousFrameDisposition::Complete:
        // A completed pair has replaced the stock game output for this serial.
        // If late OpenXR/session state cannot admit it, dropping is the only
        // safe choice: falling through would sample that non-stock backbuffer.
        return exactLivePair && stereoRequested && projectionReady
            ? Halo2SynchronousPresentationDecision::SynchronousStereo
            : Halo2SynchronousPresentationDecision::Drop;
    case Halo2SynchronousFrameDisposition::Claimed:
        return Halo2SynchronousPresentationDecision::Drop;
    case Halo2SynchronousFrameDisposition::Unclaimed:
    default:
        // No original eye render ran. With stereo eligible and the last
        // complete pair intact and fresh, re-present that pair; otherwise
        // ordinary late-ineligible behavior presents the untouched stock
        // backbuffer, identified as the C-H2-6 pre-stereo screen path.
        if (stereoRequested && projectionReady && repeatablePair)
            return Halo2SynchronousPresentationDecision::RepeatLastPair;
        return stereoRequested
            ? Halo2SynchronousPresentationDecision::StockScreen
            : Halo2SynchronousPresentationDecision::SharedDefault;
    }
}

// A Drop is allowed to consume the current claimed frame, but it must also
// quarantine that H2 generation before another render callback can claim the
// next frame. Foreign/stale stamps and the ordinary unclaimed screen path do
// not own the current transaction and therefore cannot quarantine anything.
constexpr bool Halo2SynchronousDropRequiresQuarantine(
    uint32_t generation, uint64_t preparedSerial,
    const Halo2SynchronousPresentationStamp& presentation,
    Halo2SynchronousPresentationDecision decision) noexcept
{
    return decision == Halo2SynchronousPresentationDecision::Drop &&
        Halo2SynchronousPresentationStampMatches(
            generation, preparedSerial, presentation);
}

// Only H2's ordinary unclaimed pre-stereo path opts into the strict
// screen-swapchain
// transaction. Claimed/complete frames remain drops or synchronous stereo, and
// every other title retains the established shared screen delivery behavior.
constexpr bool Halo2SynchronousRequiresStrictStockScreen(
    bool halo2Frame,
    Halo2SynchronousFrameDisposition disposition,
    Halo2SynchronousPresentationDecision decision) noexcept
{
    return halo2Frame &&
        disposition == Halo2SynchronousFrameDisposition::Unclaimed &&
        (decision == Halo2SynchronousPresentationDecision::StockScreen ||
         decision == Halo2SynchronousPresentationDecision::SharedDefault);
}
#endif

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
#include "../common/reach_render_logic.h"

struct ReachVrRenderAccess
{
    ReachDisplaySurfaceProof proof{};
    ID3D11Texture2D* source = nullptr;
    ID3D11Texture2D* eyes[2]{};
    ID3D11DeviceContext* context = nullptr;
    uint64_t preparedSerial = 0;
    bool active = false;
};
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
// One immutable, exact-prepared-serial eye-offset sample for Halo 4's narrow
// setup+wrapper transaction. Publication is double-buffered and lock-free so
// the render hook never takes the tracking critical section.
struct Halo4VrEyeSnapshot
{
    float position[3]{};
    float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    // This eye's native frustum as the runtime reports it, left/right/up/down
    // in radians. C-H4-8 derives the raster cover from these, so nothing about
    // the FOV is headset-specific.
    float fov[4]{};
    bool fovValid = false;
};

struct Halo4VrRenderSnapshot
{
    uint64_t preparedSerial = 0;
    Halo4VrEyeSnapshot eyes[2]{};
    // The stereo midpoint's own pose, sampled in the same OpenXR frame as the
    // eye offsets above so head tracking and the IPD split can never disagree.
    float headOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float headPosition[3]{};
    bool headPoseValid = false;
    // Exact-frame controller poses. The final-palette hook consumes these
    // lock-free while the matching Halo 4 eye transaction is active.
    bool rightAimValid = false;
    float rightAimOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float rightAimPosition[3]{};
    // True only when this exact prepared frame's published right-aim pose used
    // the accepted two-hand support solve. Consumers must not resample the
    // asynchronous global latch after publication.
    bool twoHandAimActive = false;
    bool handAlignment = false; // experimental presentation, frozen with this frame
    bool leftHanded = false;
    bool leftControllerValid = false;
    float leftControllerOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float leftControllerPosition[3]{};
};

static_assert(sizeof(Halo4VrEyeSnapshot) == 0x30);
static_assert(offsetof(Halo4VrRenderSnapshot, eyes) +
                  offsetof(Halo4VrEyeSnapshot, fovValid) == 0x34);
static_assert(offsetof(Halo4VrRenderSnapshot, eyes) +
                  sizeof(Halo4VrEyeSnapshot) +
                  offsetof(Halo4VrEyeSnapshot, fovValid) == 0x64);
static_assert(offsetof(Halo4VrRenderSnapshot, headPoseValid) == 0x84);
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
// Exact-prepared-serial, lock-free snapshot consumed by Halo 2's hot render
// hook. No lock, allocation, logging, COM call, or engine scan occurs here.
bool VR_Halo2GetRenderSnapshot(Halo2VrRenderSnapshot& snapshot);
// The hook publishes only after its one stock player-window transaction has
// completed and both selectively overwritten camera positions were restored.
bool VR_Halo2CompleteTemporalEye(
    uint32_t generation, uint64_t preparedSerial, int eye,
    float halfFovX, float halfFovY);
// Revokes an incomplete/duplicate/split-screen transaction without disarming
// the title core or OpenXR session.
void VR_InvalidateHalo2TemporalFrame(
    uint32_t generation, uint64_t preparedSerial);
// Atomic-only worker/presentation teardown boundary. Revokes the pending token
// and both cached-eye generation/serial stamps; shared caches remain reusable.
void VR_ResetHalo2TemporalStereo();
// Truthful raster-cover publication for the adjacent temporal pair admitted by
// C-H2-2. Returns false for a partial, stale, non-adjacent, or foreign pair.
bool VR_Halo2GetTemporalHalfFovs(
    uint32_t generation, uint64_t preparedSerial,
    float halfX[2], float halfY[2]);
#endif

#if HALOMCCVR_HALO2_STEREO6DOF
// Exact-current-serial tracking packet consumed by Halo 2's synchronous outer
// owner. Every getter is lock-free and rejects a publication rollover.
bool VR_Halo2GetSynchronousRenderSnapshot(
    Halo2SynchronousVrRenderSnapshot& snapshot);
// Atomic-only exact-current publication for presentation admission before the
// synchronous hook is enabled. The render snapshot above carries the same
// fields and remains the authoritative per-transaction proof.
bool VR_Halo2GetCurrentPreparedCadence(
    Halo2PreparedCadenceSnapshot& cadence) noexcept;
// The game core resolves the title-proven final-output RTV outside its hot
// hooks and lends that raw pointer for this synchronous render transaction.
// No COM call, allocation, lock, scan, or log occurs in these functions.
// `sceneRtv` is the engine's primary scene target (slot 0x197EE60), lent as
// a second capture candidate: the capture learns at runtime which target
// actually carries per-eye content (E-H2-8) instead of assuming the
// final-output slot does.
bool VR_Halo2BeginSynchronousPair(
    uint32_t generation, uint64_t preparedSerial,
    ID3D11RenderTargetView* finalRtv,
    ID3D11RenderTargetView* sceneRtv = nullptr);
bool VR_Halo2BeginSynchronousEye(
    uint32_t generation, uint64_t preparedSerial, int eye);
// E-H2-18: `renderedPosition`/`renderedOrientation` name the absolute OpenXR
// view pose the eye image was rendered from when that is NOT the prepared
// frame's own located view (the Anniversary core renders from the observer's
// published sample); null means the located view of `preparedSerial`.
bool VR_Halo2CompleteSynchronousEye(
    uint32_t generation, uint64_t preparedSerial, int eye,
    float halfFovX, float halfFovY,
    const float* renderedPosition = nullptr,
    const float* renderedOrientation = nullptr);
// True only when BOTH eyes of the complete pair for `preparedSerial` were
// rendered from an explicitly named pose; fills those poses.
bool VR_Halo2GetSynchronousPairPoses(
    uint32_t generation, uint64_t preparedSerial,
    float position[2][3], float orientation[2][4]);
// E-H2-18 HUD replay (Anniversary). The complete pair for `preparedSerial`
// must exist with both eye images; the final-output target must be the
// texture the eyes were copied from. Recapture copies that target into the
// eye cache; Restore copies the eye cache back into that target.
bool VR_Halo2HudReplayEligible(
    uint32_t generation, uint64_t preparedSerial, const char** reason);
bool VR_Halo2GetHudSourceRectangle(
    uint32_t generation, uint64_t preparedSerial,
    Halo2CameraRectangle& rectangle);
bool VR_Halo2RecaptureEyeFromFinalTarget(
    uint32_t generation, uint64_t preparedSerial, int eye);
bool VR_Halo2RestoreEyeToFinalTarget(
    uint32_t generation, uint64_t preparedSerial, int eye);
bool VR_Halo2CompleteSynchronousPair(
    uint32_t generation, uint64_t preparedSerial);
// Claim immediately before the first original eye render_view call. A failed
// publication must take the zero-eye pre-stereo screen path; once published,
// this
// resource-free identity survives reset until its exact serial is submitted.
bool VR_Halo2ClaimSynchronousPairForPresentation(
    uint32_t generation, uint64_t preparedSerial);
bool VR_Halo2GetSynchronousPresentationStamp(
    Halo2SynchronousPresentationStamp& presentation);
void VR_Halo2InvalidateSynchronousPair(
    uint32_t generation, uint64_t preparedSerial);
void VR_ResetHalo2SynchronousStereo();
// Exact pair admission and raster-cover publication. Both eyes, the pair
// token, title generation, and cache-resource epoch must name this serial.
// The cover of the last complete pair, when it may be re-presented for a
// prepared serial that has no pair of its own: same generation, no pair
// reserved or in flight, both eye caches still holding that pair, and not
// older than the freshness bound. Also records the decision's freshness.
bool VR_Halo2GetRepeatableHalfFovs(
    uint32_t generation, uint64_t preparedSerial,
    float halfX[2], float halfY[2]);
bool VR_Halo2GetSynchronousHalfFovs(
    uint32_t generation, uint64_t preparedSerial,
    float halfX[2], float halfY[2]);
#endif

// Called once on the DLL's background init thread. Creates the OpenXR instance
// and finds the headset (slow), so the render thread never blocks on it.
void VR_InitInstance();

// Called around the game's real DXGI Present. The completed OpenXR frame is
// submitted before Present; after Present returns, OpenXR supplies the exact
// predicted display time that Halo will use while rendering its next frame.
void VR_BeforePresent(IDXGISwapChain* swapchain);
void VR_AfterPresent(IDXGISwapChain* swapchain, int64_t presentStartQpc,
                     int64_t presentEndQpc, HRESULT presentResult);
bool VR_OnResizeBuffers(IDXGISwapChain* swapchain);
void VR_AfterResizeBuffers(IDXGISwapChain* swapchain);

// Existing title worker only: drains and formats completed transition traces.
// Render, camera, and palette hooks only publish fixed-size POD records.
void VR_FramePacingWorkerPoll();

// Worker-thread-only Reach display/resource proof. The private candidate calls
// this after its loaded-image proof; normal builds never reference it.
void VR_ReachRenderCandidate_ColdPoll();
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
ReachPreparedFrameToken VR_ReachPreparedFrame(
    const ReachModuleEpoch& epoch);
bool VR_ReachDisplayReady(const ReachModuleEpoch& epoch);
bool VR_ReachBeginRenderAccess(
    const ReachModuleEpoch& epoch,
    const ReachPreparedFrameToken& prepared,
    ReachVrRenderAccess& access);
bool VR_ReachCopyEye(ReachVrRenderAccess& access, int eye);
void VR_ReachEndRenderAccess(ReachVrRenderAccess& access);
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
bool VR_Halo4GetRenderSnapshot(Halo4VrRenderSnapshot& snapshot);
// Captures only the already-redirected Halo 4 eye and stamps it with the
// prepared serial. No logging, discovery, or COM work occurs here.
bool VR_CaptureHalo4RenderedEye(int eye, uint64_t preparedSerial);
// Revokes partial/stale eye stamps when a claimed Halo 4 pair is dropped.
void VR_InvalidateHalo4PreparedFrame();
#endif

// Timing beacon from Halo's camera-copy hook. The first call after
// VR_AfterPresent proves how quickly the freshly predicted pose reaches the
// render camera; subsequent calls prove camera transforms keep up with FPS.
void VR_NotifyCameraTransform();

// HUD panel capture: called from the FP driver hook (render thread, left-eye
// pass, before the HUD elements draw). Snapshots the left-eye scene-only image;
// VR_OnPresent diffs it against the finished frame to extract Halo's HUD pixels
// for the floating HUD panel.

// Dormant one-shot frame trace retained for targeted render-order discovery.
// Normal builds compile out every call site so render hooks pay no atomic or
// logging cost.
void VR_TraceEvent(const char* tag, int a, int b);

// Called from the menu: re-place the virtual screen in front of where the
// user is currently looking.
void VR_RequestRecenter();

// Switch between immersive stereo gameplay and Halo's stable head-locked
// pause/menu screen. Requests are comfort-faded on the render thread.
void VR_RequestPausePresentation(bool paused);
bool VR_IsPausePresentation();
bool VR_IsPausePresentationTarget();

// F10: toggle whether the flat screen follows the head (vs. staying pinned in
// the world). Only matters while head tracking is on.
void VR_ToggleScreenFollow();

// F11: development stereo proof. Alternates left/right game renders and
// submits the two retained images as an OpenXR projection layer.
void VR_ToggleStereo();
bool VR_IsStereoEnabled();
// True only while the shared compositor is presenting a proven authored,
// player-locked cinematic on its room-fixed stereo theatre screen.
bool VR_IsCutsceneTheaterActive();
// True only for the begun OpenXR frame the runtime asked us to render. A
// synchronized/unfocused session commonly publishes false while the headset is
// idle; game hooks must not treat the resulting absent eye raster as failure.
bool VR_ShouldRenderPreparedFrame();
// The application-frame cadence implied by xrWaitFrame's reported period
// (0 until the first valid period). This is not the physical panel refresh.
float VR_HeadsetRefreshHz();
// True once xrWaitFrame is driving our cadence, i.e. the headset owns pacing and
// the desktop present is free to run unlocked.
bool VR_IsFramePacingOwned();
// Called on the render thread when Halo stops driving its level camera. Makes
// every 3D path inactive immediately and drops references to Halo's scene
// target before MCC switches to its shell or another resident game engine.
void VR_DetachGamePresentation();

// Called by the M2 game render hook immediately after each eye's scene pass,
// before the next eye overwrites the game backbuffer.
bool VR_CaptureRenderedEye(int eye);
// ODST's third-person death renderer bypasses the internal scene-color RTV and
// draws directly into the current swapchain buffer. Copy that completed draw
// into the eye cache without doing COM discovery in the game render hook.
bool VR_CaptureBackbufferEye(int eye);
void VR_BeginRasterEye(int eye);
// Exact immediate-context eye scope on its owning thread; no COM queries.
bool VR_IsEyeRasterActive(ID3D11DeviceContext* context) noexcept;
void VR_EndRasterEye();
// Halo 2 draw census (E-H2-8): counts every DrawIndexed/Draw the title
// issues, split by whether a per-eye raster scope was open, so the log can
// say whether the classic world is drawn INSIDE the render_view the core
// wraps or after it. One atomic increment per draw; reported every 2 s.
void VR_Halo2NoteDraw();
// E-H2-34: asks the periodic Halo 2 eye-pair check to write the eye
// pictures at its next run after `delayMs` (a renderer switch should leave
// a picture of the renderer it switched TO even when the visit is short).
void VR_Halo2RequestEyeDump(uint32_t delayMs);
// One-shot Halo 2 evidence: describes the textures behind three engine RTV
// slots and the render target bound at the moment of the call. COM work,
// caller gates it to once per module generation; never per frame.
void VR_Halo2LogTargetCensusOnce(
    const char* tag, const uintptr_t slots[3], uint32_t viewIndex,
    uint32_t viewMask);
// ODST's native CHUD phases are part of the same logical per-eye render as
// Halo 3, but ODST can rebind the flat output target while those phases run.
// Keep that title-specific phase on the active eye cache and restore every
// output-merger reference afterward. Called at CHUD-phase granularity, never
// per widget.
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
bool VR_BeginNativeHudEyeDraw(int eye);
void VR_EndNativeHudEyeDraw();
void VR_BeginNativeHudTargetCopy();
void VR_EndNativeHudTargetCopy();
ID3D11Resource* VR_RedirectNativeHudCopySource(ID3D11Resource* source);
void VR_GetNativeHudRouteStats(unsigned& completedPhaseScopes,
                               unsigned& provenOmMatches,
                               unsigned& exactCopyScopes,
                               unsigned& copySubstitutions);
#endif

// Universal scope: a refresh-limited third world render redirected into a
// private cache. The physical OpenXR quad continues tracking every frame.
bool VR_ScopeShouldRenderThisFrame();
bool VR_BeginScopeRaster();
void VR_CaptureScope();
void VR_EndScopeRaster();
bool VR_GetScopeRenderAspect(float& outAspect);
void VR_InvalidateScopeImage();
bool VR_BeginHalo2Scope(uint32_t generation,uint64_t serial);
void VR_EndHalo2Scope(bool completed);
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
bool VR_ReachScopeReady(const ReachVrRenderAccess& access);
bool VR_ReachCopyScope(ReachVrRenderAccess& access);
#endif
// Current non-persistent scope magnification. It resets to scope_zoom whenever
// R3 opens the scope and is adjusted by right-stick Y while active.
float VR_GetScopeZoom();

// Redirects the final scene-color RTV to the active eye's target and, while
// stereo_sun_shafts is off, neutralizes the sun-shaft occlusion pass (its
// radial blur uses a single per-frame sun position, streaking the other eye).
// The context is the one the game is binding on, so the neutralizing clear
// lands in the right command order.
// `depth` is the depth-stencil view the game binds alongside; the optional
// DLSS resolve remembers which depth buffer belongs to the scene target.
bool VR_RedirectRenderTargets(ID3D11DeviceContext* context, UINT count,
                              ID3D11RenderTargetView* const* input,
                              ID3D11DepthStencilView* depth,
                              ID3D11RenderTargetView** output);
// The D3D hook reports the effective OM targets without querying COM state on
// every Draw. During a Halo 4 authored capture, the existing shared Draw hooks
// then reassert Stage 3BR's proven framing immediately before vertices bake.
void VR_Halo4NoteBoundRenderTargets(
    ID3D11DeviceContext* context, UINT count,
    ID3D11RenderTargetView* const* rtvs);
void VR_Halo4PrepareAuthoredReticleDraw(ID3D11DeviceContext* context);
// The optional H4 hooks pass through completely after a fatal reticle-resource
// failure, leaving the stock CUI reticle visible without affecting camera,
// theatre, stereo, or OpenXR ownership.
bool VR_Halo4AuthoredReticleFeatureHealthy();
uint64_t VR_TakeAuthoredReticleOmReroutes();
uint64_t VR_TakeAuthoredReticleFramingReasserts();

// Latest head pose in the VR "local" space (captured each frame). Orientation
// is a quaternion (x,y,z,w), position is meters (x,y,z). Returns false until a
// valid pose has been read. Thread-safe; the game camera hook (M1) reads this.
bool VR_GetHeadPose(float outQuat[4], float outPos[3]);
// Latest right-controller aim pose in the same OpenXR local space as the head.
// This is tracking only; weapon/projectile application is performed by M3 game hooks.
bool VR_GetRightControllerPose(float outQuat[4], float outPos[3]);
// Physical identity, independent of the primary/support weapon role mapping.
bool VR_GetPhysicalControllerPose(int hand, float outQuat[4], float outPos[3]);
// Left controller pose (used by the D-pad gesture; false until tracked).
bool VR_GetLeftControllerPose(float outQuat[4], float outPos[3]);
// Runtime-reported tracking-space linear velocity in metres per second.
// Unlike a world-space hand delta, this excludes artificial locomotion.
bool VR_GetControllerLinearVelocity(bool left, float outVelocity[3]);
// Melee-only 60 ms tracked peak; ordinary motion velocity stays instantaneous.
bool VR_GetPhysicalMeleeSpeed(bool left,float& speed);
// Called only from Halo's already-validated class-2 CHUD path. The active
// weapon reticle is redirected into the controller-ray quad texture instead
// of being drawn at the center of either VR eye.
bool VR_BeginAuthoredReticleCapture();
// False on frames where the title already holds valid released crosshair art
// and its sample cadence says this frame does not need a fresh offscreen draw.
// The caller hides the flat widget instead; the held quad keeps showing.
bool VR_ShouldCaptureAuthoredReticleThisFrame();
// Reach-only hide entry: same lazy resource creation, but it never refuses
// because crosshair=0. Reach has no visibility predicate and its CHUD alpha
// write is inert, so this redirect is the only way its native crosshair can
// be kept off the eye - including when the user asks for no crosshair at all.
bool VR_BeginAuthoredReticleRedirect();
void VR_EndAuthoredReticleCapture();
// Reach and Halo 4 prepare every allocation and swapchain/RTV object on their
// cold title worker before installing a CUI/CHUD hook. The prepared begin/end
// pair performs only render-state redirection in the hot hook.
enum class AuthoredReticlePreparationResult : uint8_t
{
    NotReady,
    Ready,
    Failed,
};
bool VR_CanPrepareAuthoredReticleResources();
AuthoredReticlePreparationResult VR_PrepareAuthoredReticleResources();
// A title can issue more than one qualifying outer render in a prepared frame.
// Invalidate the prior attempt before each newly admitted stereo transaction so
// an authored no-crosshair state cannot inherit an earlier attempt's texture.
void VR_InvalidatePreparedAuthoredReticleCapture();
bool VR_BeginPreparedAuthoredReticleCapture();
// CE's native HUD must draw on the shared immediate context and the same XR
// serial before borrowing prepared capture resources; no context discovery.
bool VR_CeAuthoredReticleFrameMatches(ID3D11DeviceContext* context,uint64_t serial);
bool VR_CeRedirectAuthoredReticleTargets(ID3D11DeviceContext*,UINT,
    ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT&,
    ID3D11RenderTargetView**,ID3D11DepthStencilView*&);
bool VR_CeRedirectAuthoredReticleViewports(ID3D11DeviceContext*,UINT,const D3D11_VIEWPORT*,D3D11_VIEWPORT&);
bool VR_CeRedirectAuthoredReticleScissors(ID3D11DeviceContext*,UINT,const D3D11_RECT*,D3D11_RECT&);
void VR_CeInvalidateAuthoredReticleState(ID3D11DeviceContext*);
bool VR_EndPreparedAuthoredReticleCapture();
// Halo 4 must still execute the opposite-eye CUI reticle subtree while keeping
// its pixels out of both the eye and the selected-eye authored capture. These
// prepared calls bind a private discard target and restore the full D3D state;
// they never mark authored art ready for publication.
bool VR_PrepareAuthoredReticleSuppressionResources();
bool VR_BeginPreparedAuthoredReticleSuppression();
bool VR_EndPreparedAuthoredReticleSuppression();
// M3: the game layer sets this when the crosshair is over an enemy (engine
// target-lock). While true, the floating reticle repaints red like the OG HUD.
void VR_SetReticleEnemy(bool enemy);
// Weapon-hand aim pose shared by bullet steering, the reticle, and the visible
// barrel. Position = right hand; orientation = right controller, or the
// right->left two-hand line when two-handed aim engages. False until tracked.
bool VR_GetAimPose(float outQuat[4], float outPos[3]);
// Last controller pose actually used to place the floating reticle after
// aim_stabilization. Published lock-free by the compositor so Reach's firing
// hook can aim at the visible sight without taking the tracking lock.
bool VR_GetPresentedReticleAimPose(
    float outQuat[4], float outPos[3], uint64_t& outSampleMs);
bool VR_IsTwoHandAiming();
// Latest OpenXR per-eye FOV angles: left, right, up, down (radians).
bool VR_GetEyeFov(int eye, float outFov[4]);
// Pixel aspect of Halo's main render surface. Halo lays native 2D HUD geometry
// out for this shape, which can differ substantially from the headset's
// tangent-space view aspect.
bool VR_GetGameRenderAspect(float& outAspect);
// M3: snapshot of the VR controllers' gamepad-like inputs, read once per frame
// from the OpenXR action set. The XInput hook merges this into (or fabricates)
// the gamepad state MCC reads, so the Sense controllers drive menus and game.
struct VrPadState
{
    uint32_t profileEpoch = 0;
    bool supportAimActive = false;
    bool valid = false;      // controllers tracked and actions synced
    float moveX = 0, moveY = 0;  // left thumbstick
    float turnX = 0, turnY = 0;  // right thumbstick
    float trigL = 0, trigR = 0;  // triggers 0..1
    float gripL = 0, gripR = 0;  // grips 0..1
    bool a = false, b = false, x = false, y = false;
    bool clickL = false, clickR = false, menu = false;
    // Physical left thumb rest selects the physical right stick, independent
    // of weapon handedness. These axes have already been removed from turn.
    bool thumbrestDpad = false;
    bool exclusiveInput = false;
    float dpadX = 0.0f, dpadY = 0.0f;
    // Optional body-gesture output, validated again before XInput delivery.
    uint32_t weaponButtons = 0;
    uint64_t weaponSampleMs = 0;
    uint64_t weaponPulseUntilMs = 0;
    uint32_t weaponGeneration = 0;
    uint64_t weaponSpace = 0;
    uint64_t weaponGraph = 0;
    unsigned weaponOptions = 0;
    uint32_t weaponReloadBinding = 0, weaponSwitchBinding = 0;
    GameTitle weaponTitle{};
    bool weaponConsumePrimary = false, weaponConsumeSupport = false;
};
void VR_GetPadState(VrPadState& out);
const char* VR_ControllerProfileName(bool left);
// Cold worker only: optional gesture status/counters, never render-hook logs.
void VR_ReportWeaponInteractions(uint64_t nowMs);
void VR_UpdateWeaponAlignment(); // cold title worker, after title-profile selection
namespace contact_melee { struct TrackingToWorld; }
void VR_PublishReloadTarget(GameTitle title,uint32_t generation,uint64_t identity,uint64_t serial,
    const contact_melee::TrackingToWorld& trackingToWorld,const float world[3]) noexcept;
// Optional, read-only equipped-model observations. No inventory or animation writes.
void VR_ObserveWeaponModel(GameTitle title,uint32_t generation,uint64_t identity) noexcept;
uint64_t VR_GetWeaponModelIdentity(GameTitle title,uint32_t generation,uint64_t space,uint64_t now) noexcept;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
// One immutable, exact-serial OpenXR tracking snapshot for Reach's complete
// outer visibility + inner stereo transaction. The reader is lock-free and
// fails immediately if publication is changing; render hooks never wait on the
// controller/head-pose critical section.
struct ReachVrEyeSnapshot
{
    float position[3]{};
    float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float fov[4]{}; // left, right, up, down
};

struct ReachVrRenderSnapshot
{
    uint64_t preparedSerial = 0;
    float headOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float headPosition[3]{};
    // The shared weapon aim after two-hand adjustment and controller-local
    // mount calibration. Position remains the raw right-controller position.
    bool rightAimValid = false;
    // Physical pose for contact velocity, independent of two-hand visual aim.
    bool rightPhysicalValid = false;
    float rightPhysicalOrientation[4]{0,0,0,1};
    // True only when this exact prepared frame used the support-hand weapon
    // line. Reach's palette path must not resample the asynchronous global.
    bool twoHandAimActive = false;
    bool handAlignment = false; // experimental presentation, frozen with this frame
    bool leftHanded = false;
    float rightAimOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float rightAimPosition[3]{};
    // Raw tracked left-controller pose for title-specific support-hand work.
    bool leftControllerValid = false;
    float leftControllerOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float leftControllerPosition[3]{};
    VrPadState pad{};
    ReachVrEyeSnapshot eyes[2]{};
    int64_t predictedDisplayTimeNs = 0;
    uint64_t trackingSpaceEpoch = 0;
};

bool VR_ReachGetRenderSnapshot(
    const ReachPreparedFrameToken& prepared,
    ReachVrRenderSnapshot& snapshot);
#endif
// Immutable tracking sample for contact consumers in every title. Hand 0 is
// left; hand 1 is the weapon aim pose used by the visible right-hand carrier.
struct VrContactTrackingSnapshot
{
    struct Hand
    {
        bool valid=false;
        float orientation[4]{0,0,0,1};
        float position[3]{};
    } hands[2];
    uint64_t serial=0,referenceEpoch=0;
    int64_t timeNs=0;
    bool twoHandAimActive=false;
    bool handAlignment=false;
    bool leftHanded=false;
    bool primaryAimValid=false;
    float primaryAimOrientation[4]{0,0,0,1};
    bool rawPrimaryValid=false;
    float rawPrimaryOrientation[4]{0,0,0,1},rawPrimaryPosition[3]{};
};
bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& snapshot);
namespace weapon_muzzle { struct Receipt; }
// Called at a committed native palette; the compositor never reads engine objects.
void VR_PublishWeaponReticleRay(GameTitle title,uint8_t slot,const weapon_muzzle::Receipt& receipt,
    const contact_melee::TrackingToWorld& trackingToWorld) noexcept;

// Universal scope state is owned by the VR controller input path and consumed
// by the render/compositor path. It is independent of Halo's native zoom.
void VR_SetScopeActive(bool active);
bool VR_IsScopeActive();
void VR_RequestScopeToggle();
// Receives Halo's blended XInput rumble level (0..1). The render thread maps
// it to portable OpenXR feedback on both hands and owns all stop conditions.
void VR_SetGameHaptics(float amplitude);
// Adds a lock-free, one-shot collision pulse for one OpenXR hand. It is merged
// with (and never replaces) the title's ordinary XInput rumble.
void VR_PulseContactHaptics(bool left, float amplitude);

// Position and rotation of one eye relative to the midpoint of both OpenXR
// views. Position is in meters and both outputs use OpenXR view-local axes
// (+X right, +Y up, -Z forward). Runtimes may change these offsets when the
// headset's lens spacing changes, so stereo rendering must not assume one
// headset's fixed IPD or eye cant.
bool VR_GetEyeViewOffset(int eye, float outPosition[3], float outQuat[4]);

struct VrStatus
{
    char runtime[128];
    char sessionState[32];
    unsigned gameWidth, gameHeight;
    float fps;
};
void VR_GetStatus(VrStatus& out);

// Lock-free physical tracking/focus freshness for optional native walking.
bool VR_RoomscaleTrackingFresh() noexcept;
uint64_t VR_PhysicalCrouchEpoch() noexcept;
void VR_RecalibratePhysicalCrouch() noexcept;

// --- Optional NVIDIA DLSS eye resolve (config.upscaler = 1) ---------------
// A title's eye render hook publishes the exact camera it rasterized with:
// world position, unit forward and up, and the engine's 16-float projection
// matrix as held (the Halo row-vector layout, [0]/[5] inverse half-frustum
// tangents and [8]/[9] centre terms, or its transpose; the decoder accepts
// both and logs which). `jitterNdcX/Y` is whatever VR_GetEyeJitter handed
// the hook this render, or zero for a title that does not write its
// projection before the draw (Halo 4, Halo 2). Render thread only, once per
// eye render, inside that eye's raster scope or depth window, before or
// after the engine draws (DLSS consumes it at Present).
struct VrEyeCameraSample
{
    float position[3];
    float forward[3];
    float up[3];
    float projection[16];
    float jitterNdcX;
    float jitterNdcY;
};
void VR_PublishEyeCamera(int eye, const VrEyeCameraSample& sample);
// True with the projection-centre offsets (NDC units) the hook must ADD to
// projection[8] and [9] for this eye render. False means render unjittered:
// DLSS is off, unavailable, or did not resolve the previous frame.
bool VR_GetEyeJitter(int eye, float& outNdcX, float& outNdcY);
// True while the DLSS upscaler is selected, so a title hook can skip building
// a sample (and any guarded engine read behind it) when nothing consumes it.
bool VR_DlssWantsEyeCamera();
// A title that renders its eyes outside VR_BeginRasterEye/VR_EndRasterEye
// (Reach) brackets each eye render with these instead: inside the window the
// OM hook classifies every depth view bound, and the end copies the winner
// for that eye. Render thread only; the begin resets the eye's DLSS state
// exactly as VR_BeginRasterEye does, so publish the camera after it.
void VR_DlssBeginEyeDepthWindow(int eye);
void VR_DlssEndEyeDepthWindow(int eye);
// Plain-language DLSS state for the F1 menu ("off", "ready ...",
// "unavailable: ...", "active ...").
void VR_GetDlssStatus(char* out, size_t bytes);
// This session's render plan for the F1 menu: what the game is rendering now
// (zero until its backbuffer exists), what this start planned, the headset
// picture size the DLSS output has, whether the render was shrunk for DLSS,
// and whether nvngx_dlss.dll was beside the mod at startup. False before the
// plan exists.
struct VrDlssRenderPlan
{
    unsigned liveRenderW = 0, liveRenderH = 0;
    unsigned plannedRenderW = 0, plannedRenderH = 0;
    unsigned outputW = 0, outputH = 0;
    bool plannedDlss = false;
    bool runtimePresent = false;
};
bool VR_GetDlssRenderPlan(VrDlssRenderPlan& out);

void VR_ObserveReachAlternateDepthBind(ID3D11DeviceContext* context, UINT renderTargetCount, ID3D11DepthStencilView* depth);
