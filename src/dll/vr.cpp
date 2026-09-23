#include "../common/vr_interaction_refinement_logic.h"
#include "../common/weapon_hand_logic.h"
#include "../common/weapon_interaction_logic.h"
#include "../common/weapon_model_observation.h"
#include "../common/weapon_reload_target.h"
#include "weapon_accessory_renderer.h"
#include "../common/title_runtime_state.h"
#include <windows.h>
#include "../common/virtual_stock_logic.h"
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <vector>
#include <array>
#include <string>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <limits>
#include <type_traits>
#include "vr.h"
#include "menu.h"
#include "native_menu_pointer.h"
#include "native_subtitles.h"
#include "../common/subtitle_logic.h"
#include "../common/dual_reticle_logic.h"
#include "../common/game_menu_pointer.h"
#include "../common/exclusive_input.h"
#include "../common/game_menu_pointer_xr.h"
#include "game.h"
#include "haloce_stereo_core.h"
#include "../common/haloce_pause_logic.h"
#include "../common/haloce_reticle_logic.h"
#include "../common/vr_blit_shader.h"
#include "haloce_first_person.h"
#include "haloce_hud.h"
#include "haloce_hud_layout.h"
#include "d3d11_hook.h"
#include "d3d_state.h"
#include "smaa_resource.h"
#include "dlss.h"
#include "dlss_camera_ps.h"
#include "dlss_debug_ps.h"
#include "dlss_copy_ps.h"
#include "gpu_eye_timing.h"
#include "../common/dlss_logic.h"
#include "../common/dlss_sampler_logic.h"
#include "../common/dlss_frame_result.h"
#include "AreaTex.h"
#include "SearchTex.h"
#include "title_adapter.h"
#include <../common/authored_reticle_logic.h>
#ifndef HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
#define HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO 0
#endif
#ifndef HALOMCCVR_HALO2_STEREO6DOF
#define HALOMCCVR_HALO2_STEREO6DOF 0
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO && \
    HALOMCCVR_HALO2_STEREO6DOF
#error Halo 2 temporal and synchronous stereo paths are mutually exclusive
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
#include "../common/halo2_render_logic.h"
#endif
#if HALOMCCVR_HALO2_STEREO6DOF
#include "../common/halo2_render_logic.h"
#include "halo2_stereo_core.h"
#endif
#ifndef HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
#define HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE 0
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
#include "reach_render_candidate.h"
#endif
#include "../common/reach_chud_logic.h"
#include "../common/log.h"
#include "../common/config.h"
#include "../common/cutscene_theater_logic.h"
#include "../common/frame_pacing_logic.h"
#include "../common/halo4_render_logic.h"
#include "../common/halo4_cui_reticle_logic.h"
#include "../common/halo4_world_collision_logic.h"
#include "../common/input_logic.h"
#include "../common/reach_vehicle_logic.h"
#include "../common/scope_logic.h"
#include "../common/view_cache_logic.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

// M0 "virtual cinema": every frame the game presents, we copy its backbuffer
// into an OpenXR swapchain and submit it as a world-locked quad layer (a flat
// rectangle floating in space). The ImGui menu is a second, smaller quad.
// The headset compositor does all the reprojection; there is no stereo yet.

namespace
{
    enum class State { Uninitialized, Ready, Failed };

    State g_state = State::Uninitialized;

    // The OpenXR instance/system are created on a background thread (see
    // VR_InitInstance) because xrCreateInstance can take 20+ seconds while
    // SteamVR spins up — doing that on the game's render thread freezes the
    // game and it gets killed as unresponsive. These flags let the render
    // thread know when the instance is ready without ever blocking on it.
    std::atomic<bool> g_instanceReady{false};
    std::atomic<bool> g_instanceFailed{false};

    // OpenXR core
    XrInstance g_instance = XR_NULL_HANDLE;
    XrSystemId g_systemId = XR_NULL_SYSTEM_ID;
    XrSession g_session = XR_NULL_HANDLE;
    XrSpace g_localSpace = XR_NULL_HANDLE; // world-fixed, origin = headset pose at session start
    std::atomic<int64_t> g_contactSpaceChangeAtNs{0};
    std::atomic<uint64_t> g_contactSpaceEpoch{1};
    VrContactTrackingSnapshot g_contactTrackingSnapshots[2]{};
    std::atomic<uint32_t> g_contactTrackingStates[2]{};
    std::atomic<uint32_t> g_contactTrackingIndex{2};
    constexpr uint32_t kContactTrackingWriter=UINT32_MAX;
    XrSpace g_viewSpace = XR_NULL_HANDLE;  // follows the headset; used for recentering
    XrActionSet g_gameplayActions = XR_NULL_HANDLE;
    XrAction g_rightAimAction = XR_NULL_HANDLE;
    XrSpace g_rightAimSpace = XR_NULL_HANDLE;
    XrAction g_leftAimAction = XR_NULL_HANDLE;
    XrSpace g_leftAimSpace = XR_NULL_HANDLE;
    XrAction g_hapticAction = XR_NULL_HANDLE;
    XrAction g_actMenu = XR_NULL_HANDLE;
    XrAction g_actLeftThumbrest = XR_NULL_HANDLE;
    XrAction g_actFacePadL=XR_NULL_HANDLE,g_actFacePadR=XR_NULL_HANDLE;
    XrAction g_actFaceClickL=XR_NULL_HANDLE,g_actFaceClickR=XR_NULL_HANDLE;
    std::atomic<uint64_t> g_thumbrestDpadSampleMs{0};
    XrPath g_leftHandPath = XR_NULL_PATH;
    XrPath g_rightHandPath = XR_NULL_PATH;
    std::atomic<vr_mapping::Profile> g_controllerProfiles[2]{};
    std::atomic<uint32_t> g_controllerProfileEpoch{1};
    uint32_t WeaponGestureBinding(GameTitle title,vr_mapping::Action action,uint64_t now)
    {
        const int index=weapon_interaction::TitleIndex(title);
        if(index<0) return 0;
        if(g_config.vr_action_mapping)
            return Game_VrActionTransport(action,now);
        return weapon_interaction::Button(action==vr_mapping::Reload?
            g_config.weapon_reload_button[index]:g_config.weapon_switch_button[index]);
    }
    bool g_touchProProfileEnabled = false;
    // The headset's real panel rate, read once from the runtime (0 = the runtime
    // does not expose it). Compared against the rate xrWaitFrame targets us at,
    // which is a FRACTION of this whenever the runtime reprojects. No rate is
    // ever assumed: 72, 80, 90, 120, 144 all come from these two numbers.
    bool g_refreshRateExtEnabled = false;
    std::atomic<float> g_panelRefreshHz{0.0f};
    XrEnvironmentBlendMode g_blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool g_sessionRunning = false;
    XrSessionState g_sessionState = XR_SESSION_STATE_UNKNOWN;
    std::atomic<float> g_requestedHaptics{0.0f};
    // Peak-hold companion to g_requestedHaptics: the maximum amplitude requested
    // since the last applied VR frame, so a short gunfire pulse that arrives and
    // clears between two frame samples is not aliased to zero. See SampleHapticPeak.
    std::atomic<float> g_peakHaptics{0.0f};
    // Optional world-contact feedback is hand-specific. These are one-shot
    // peak latches raised by a title's cold collision worker and consumed by
    // the existing OpenXR frame path; no engine thread calls OpenXR directly.
    std::atomic<float> g_contactHaptics[2]{};
    void StopControllerHaptics();
    void LogHeadsetPanelRate();
    bool StartFrameWaitThread();
    bool StopFrameWaitThread();

    // M2 stereo: per-eye recommended render size and per-eye pose/FOV.
    std::vector<XrViewConfigurationView> g_viewConfigs;
    std::vector<XrView> g_views;

    // D3D11 (the game's device — we never create our own for rendering)
    ID3D11Device* g_device = nullptr;
    IDXGIAdapter3* g_memoryAdapter = nullptr;
    ID3D11DeviceContext* g_context = nullptr;

    // XR swapchains + cached render target views of their images
    int64_t g_xrFormat = 0;
    XrSwapchain g_screenChain = XR_NULL_HANDLE;
    uint32_t g_screenW = 0, g_screenH = 0;
    std::vector<ID3D11Texture2D*> g_screenImages;
    std::vector<ID3D11RenderTargetView*> g_screenRtvs;
    XrSwapchain g_nativeCursorChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_nativeCursorImages;
    std::vector<ID3D11RenderTargetView*> g_nativeCursorRtvs;
    bool g_nativeCursorReady = false;
    XrSwapchain g_nativeSubtitleChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_nativeSubtitleImages;
    std::vector<ID3D11RenderTargetView*> g_nativeSubtitleRtvs;
    uint64_t g_nativeSubtitleUploaded = 0, g_nativeSubtitleRetryMs = 0;
    uint32_t g_nativeSubtitleAcquiredIndex = 0;
    bool g_nativeSubtitleAcquired = false, g_nativeSubtitleFailed = false;
    bool g_nativeTheatreCaptionReady = false;
    XrSwapchain g_dualReticleChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_dualReticleImages;
    std::vector<ID3D11RenderTargetView*> g_dualReticleRtvs;
    bool g_dualReticleAcquired=false,g_dualReticleReady=false,g_dualReticleFailed=false;
    uint32_t g_dualReticleIndex=0;
    uint64_t g_dualReticleRetryMs=0;
    float g_dualReticleColor[3]{-1,-1,-1};
    XrPosef g_secondaryReticlePose{};
    bool g_secondaryReticlePoseValid=false;
    uint64_t g_secondaryReticleSpace=0;
    uint32_t g_secondaryReticleGeneration=0;
    GameTitle g_secondaryReticleTitle=GameTitle::None;
    halo_ce::Snapshot<dual_reticle::ProjectedRay> g_weaponReticleRays[7][2];
    XrSwapchain g_menuChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_menuImages;
    std::vector<ID3D11RenderTargetView*> g_menuRtvs;
    XrSwapchain g_fadeChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_fadeImages;
    std::vector<ID3D11RenderTargetView*> g_fadeRtvs;

    // M3 aim crosshair: a tiny static reticle image floated as a quad layer
    // along the weapon's true aim ray. Drawn once; the compositor keeps
    // re-showing the last released image, so it costs nothing per frame.
    XrSwapchain g_reticleChain = XR_NULL_HANDLE;
    std::atomic<bool> g_reticleChainFailed{false};
    std::atomic<bool> g_authoredReticlePreparationReady{false};
    // A strict world-swapchain failure aborts the current layer transaction and
    // enters the existing fatal session drain. Some failures (for example a
    // wait timeout) can leave an image acquired; other non-XR_SUCCESS results
    // (for example XR_SESSION_LOSS_PENDING from release) can still mean the
    // operation completed. In either case, submit no layers and pair a begun
    // frame with one empty xrEndFrame before session recovery.
    bool g_abortFrameForSwapchainFailure = false;
    std::vector<ID3D11Texture2D*> g_reticleImages;
    std::vector<ID3D11RenderTargetView*> g_reticleRtvs;
    constexpr uint32_t kReticleSize = 512;

    // Universal gun scope. A private scene cache receives the refresh-limited
    // third render; an isolated upload pipeline center-crops it to the fixed
    // 1024x768 screen and paints the aiming mark without touching either eye.
    XrSwapchain g_scopeScreenChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_scopeScreenImages;
    std::vector<ID3D11RenderTargetView*> g_scopeScreenRtvs;
    constexpr uint32_t kScopeScreenWidth = 1024;
    constexpr uint32_t kScopeScreenHeight = 1024;
    std::atomic<bool> g_scopeActive{false};
    ID3D11Texture2D* g_scopeCache = nullptr;
    ID3D11RenderTargetView* g_scopeCacheRtv = nullptr;
    ID3D11ShaderResourceView* g_scopeCacheSrv = nullptr;
    D3D11_TEXTURE2D_DESC g_scopeCacheDesc{};
    std::atomic<bool> g_rasterScope{false};
    bool g_scopeRedirected = false;
    std::atomic<bool> g_scopeHasImage{false};
    GameTitle g_scopeImageTitle=GameTitle::None,g_scopeRequestTitle=GameTitle::None;
    uint32_t g_scopeImageGeneration=0,g_scopeRequestGeneration=0;
    ScopeRefreshScheduler g_scopeRefreshScheduler;
    ScopeZoomResolver g_scopeZoomResolver;
    ScopeZoomController g_scopeZoomController;
    std::atomic<float> g_scopeRuntimeZoom{3.39f};
    std::atomic<float> g_scopeZoomStickY{0.0f};
    uint64_t g_scopeZoomLastMs = 0;
    std::atomic<uint64_t> g_scopeToggleSerial{0};
    uint64_t g_scopeToggleObserved = 0;
    std::atomic<bool> g_scopeResetRequested{false};
    // Color the reticle was last painted with, so we repaint only when the
    // user changes it (not every frame). Sentinel forces the first paint.
    float g_reticlePaintedColor[3] = {-1.0f, -1.0f, -1.0f};
    float g_reticlePaintedOpacity = -1.0f; // last painted opacity; sentinel forces first paint
    bool g_reticleEnemyPainted = false; // which color is currently on the image
    // Set by the game layer when the crosshair is over an enemy (the engine's
    // target-lock state). While set, the reticle repaints red like the OG HUD.
    std::atomic<bool> g_reticleEnemy{false};

    // Halo's class-2 CHUD widget is rendered into this small transparent
    // target instead of either eye. The resulting game-owned, per-weapon
    // artwork is uploaded to the existing controller-ray OpenXR quad.
    ID3D11Texture2D* g_authoredReticleTexture = nullptr;
    ID3D11RenderTargetView* g_authoredReticleRtv = nullptr;
    // Halo 4's CUI reticle is bracketed around a subtree rather than guarded by
    // a visibility predicate. The non-capture eye therefore executes the full
    // subtree against this prepared throwaway target. It must not share the
    // authored target: drawing the second eye there would double-blend or
    // replace the pixels selected for the gun-ray quad.
    ID3D11Texture2D* g_authoredReticleDiscardTexture = nullptr;
    ID3D11RenderTargetView* g_authoredReticleDiscardRtv = nullptr;
    // GitHub #70. Two candidates guarded the capture's IDENTITY and its PIECE
    // COUNT, and the runtime log proved both inert: across a whole combat
    // session the key never left 8E28E5B60B57DCA3 and the count never left
    // `pieces 5, held 5`, while the quad read SUBMITTED with held art
    // throughout - and the player still lost the crosshair on every hit. The
    // same five widgets kept drawing, so what changed can only be the PIXELS
    // they produced. Measure that directly instead of guessing which engine
    // state did it: a capture that painted nothing visible must never reach
    // the quad. This also covers the crosshair being kicked outside the
    // magnified centre crop, which no identity or count can see either.
    ID3D11ShaderResourceView* g_authoredReticleSrv = nullptr;
    // Last capture measured to actually contain art. The swapchain is only
    // ever fed from here, so a blank capture cannot become the held image.
    ID3D11Texture2D* g_authoredReticleGoodTexture = nullptr;
    bool g_authoredReticleGoodValid = false;
    // 8x8 mip readback, summed. HOW MUCH ink the capture holds is the measure
    // that matters, not whether any survives: Reach's crosshair is five
    // widgets - ar_reticule plus l/r/t/b_crosshair - and the four petals are
    // animated by `weapon barrel error scale` (official HREK
    // `ui\chud\*.chud_definition`: every one carries
    // `external input B = weapon barrel error scale` and drives its `active`
    // animation from `extern 2`). Taking a hit spikes barrel error, the petals
    // bloom outward, and they leave the magnified centre crop this capture
    // reads - while the centre reticle can stay. A "did anything survive"
    // test would pass on that leftover dot and publish a crosshair with its
    // petals missing, which is the same disappearance from the player's seat.
    ID3D11Texture2D* g_authoredReticleProbeStaging = nullptr;
    // The probe is deliberately pipelined. A refresh queues its tiny readback;
    // later frames poll this fence without flushing or waiting, while the last
    // known-good crosshair stays on the quad.
    ID3D11Query* g_authoredReticleProbeFence = nullptr;
    bool g_authoredReticleProbePending = false;
    constexpr uint32_t kAuthoredReticleCoveragePending = UINT32_MAX;
    constexpr uint32_t kAuthoredReticleProbeSize = 8;
    // Absolute floor for "there is nothing here at all".
    constexpr uint32_t kAuthoredReticleArtAlphaThreshold = 2;
    // A capture holding less than this fraction of the known-good ink is the
    // crosshair blooming out of the crop, not a new crosshair.
    constexpr uint32_t kAuthoredReticleInkNumerator = 1;
    constexpr uint32_t kAuthoredReticleInkDenominator = 2;
    // ...but a genuinely simpler crosshair (weapon swap) must not be held out
    // forever. After this many consecutive refusals the capture is accepted
    // and becomes the new reference.
    constexpr uint32_t kAuthoredReticleMaxConsecutiveHolds = 24;
    uint32_t g_authoredReticleGoodInk = 0;
    uint32_t g_authoredReticleConsecutiveHolds = 0;
    uint32_t g_authoredReticleLastCoverage = 0;
    uint32_t g_authoredReticleLastAlphaInk = 0;
    uint32_t g_authoredReticleLastColorInk = 0;
    uint32_t g_authoredReticleBlankHeld = 0;
    bool g_authoredReticleProbeUsable = true;
    // Set when an upload was deliberately withheld because the capture had no
    // visible art. The caller must tell this apart from a real upload failure,
    // which repaints the chain and would erase the crosshair itself.
    bool g_authoredReticleHeldBlank = false;
    bool g_authoredReticleReady = false;
    uint64_t g_authoredReticleSerial = 0;
    uint64_t g_authoredReticleUploadedSerial = 0;
    bool g_reticleContainsAuthored = false;
    std::atomic<uint64_t> g_reticleOwnerEpoch{1};
    struct ReticleCaptureState
    {
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        ID3D11DepthStencilView* dsv = nullptr;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = 0;
        D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT scissorCount = 0;
        bool active = false;
        bool publishesAuthored = false;
        bool cePrivateRaster = false;
        bool ceRestoring = false;
        bool ceStateValid = false;
        CeHudTargetSnapshot ceTarget;
        // The ONE viewport/scissor this capture was opened with. Halo 4 rebinds
        // its scene target up to 3 times inside a single captured replay, and
        // each rebind can carry whatever viewport the engine's OWN prior pass
        // left behind (proven by SCENEPROBE: the same learned RTV was seen
        // bound with BOTH a 947x683 and a full-raster viewport across
        // different binds). Left alone, those 3 rebinds draw at 3 different,
        // uncorrelated scales into the SAME 512x512 texture - not a wrong crop,
        // a smeared composite of unrelated passes. Re-applying this ONE
        // retained viewport at each Halo-4 rebind (see VR_RedirectRenderTargets)
        // keeps every rebind consistent with the one this capture computed.
        D3D11_VIEWPORT captureViewport{};
        D3D11_RECT captureScissor{};
        bool framingCaptured = false;
    };
    ReticleCaptureState g_reticleCaptureState{};
    std::atomic<bool> g_halo4AuthoredReticleTargetBound{false};
    std::atomic<uint64_t> g_authoredReticleOmReroutes{0};
    std::atomic<uint64_t> g_authoredReticleFramingReasserts{0};

    // The CHUD steal-and-requad machinery (capture texture, shader
    // classifier, hand-HUD swapchain) was removed 2026-07-18: it removed the
    // native HUD from both eyes, never displayed its quad, and its
    // calibration retry loop cost ~30 fps. The native HUD renders untouched;
    // per-eye FP camera substitution in game.cpp gives it (and the gun)
    // stereo-correct rendering.

    // M2 eye targets. Each eye is a separate swapchain because the headset may
    // recommend a different size for each view. They are allocated now so the
    // later render hook can draw directly into them; the mono quad remains the
    // active layer until both contain genuine per-eye game renders.
    struct EyeChain
    {
        XrSwapchain chain = XR_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<ID3D11Texture2D*> images;
        std::vector<ID3D11RenderTargetView*> rtvs;
    };
    std::vector<EyeChain> g_eyeChains;
    XrSwapchain g_stereoChain = XR_NULL_HANDLE;
    uint32_t g_stereoW = 0, g_stereoH = 0;
    std::vector<ID3D11Texture2D*> g_stereoImages;
    std::vector<std::array<ID3D11RenderTargetView*, 2>> g_stereoRtvs;
    ID3D11Texture2D* g_eyeCache[2] = {nullptr, nullptr};
    ID3D11RenderTargetView* g_eyeCacheRtvs[2] = {nullptr, nullptr};
    D3D11_TEXTURE2D_DESC g_eyeCacheDesc{};
    // Logical sampling/RTV format remains in g_eyeCacheDesc. Halo 3's native
    // DLSS cache has typeless storage, an sRGB RTV, and UAV access for NGX.
    bool g_eyeCacheNativeDlss = false;
    bool g_eyeCacheDlssRequested = false;

    // Removed: the per-eye post-process history machinery (cross-pass
    // discovery, frame-level blanking, and the learned scene-snapshot
    // pairs). Every one of them was disproven in a headset session, and
    // together they held two full-resolution shadow textures per learned
    // pair (~25 MB each) plus 96 AddRef'd candidate textures, and re-copied
    // them on every eye pass. See docs/CONTINUATION.md for what each probe
    // ruled out; do not rebuild them without new evidence.
    std::atomic<bool> g_stereoEnabled{false};
    int g_renderEye = 0;
    bool g_eyeHasImage[2] = {false, false};
#if HALOMCCVR_HALO2_STEREO6DOF
    constexpr uint64_t kHalo2SynchronousReservationBit = uint64_t{1} << 63;
    // One eye is complete when the engine's FINISHED frame for it has been
    // copied into the eye cache exactly once, after render_view returned.
    constexpr uint32_t kHalo2SynchronousRequiredCaptures = 1;
    constexpr bool Halo2SynchronousSerialIsReserved(uint64_t value) noexcept
    {
        return (value & kHalo2SynchronousReservationBit) != 0;
    }
    constexpr uint64_t Halo2ReservedSynchronousSerial(uint64_t serial) noexcept
    {
        return serial | kHalo2SynchronousReservationBit;
    }
    // 0 = no pair, high-bit(serial) = the render hook owns an unpublished pair,
    // serial = both eyes and their FOVs are complete and compositor-admissible.
    std::atomic<uint64_t> g_halo2SynchronousPairToken{};
    std::atomic<uint32_t> g_halo2SynchronousPairGeneration{};
    std::atomic<uint32_t> g_halo2SynchronousPairResourceEpoch{};
    std::atomic<uint64_t> g_halo2SynchronousTargetSerial{};
    std::atomic<ID3D11RenderTargetView*> g_halo2SynchronousFinalRtv{};
    std::atomic<ID3D11RenderTargetView*> g_halo2SynchronousSceneRtv{};
    // The last complete pair the compositor accepted, as rendered: poses,
    // covers, serial and time. Render-thread-owned (SubmitPreparedFrame).
    struct Halo2LastPair
    {
        bool valid = false;
        uint32_t generation = 0;
        uint64_t serial = 0;
        uint64_t tickMs = 0;
        XrPosef pose[2]{};
        float halfX[2]{};
        float halfY[2]{};
    };
    Halo2LastPair g_halo2LastPair{};
    constexpr uint64_t kHalo2RepeatPairMaxAgeMs = 150;
    std::atomic<uint64_t> g_halo2XrPairsRepeated{};
    // E-H2-8 draw census counters (see VR_Halo2NoteDraw).
    std::atomic<uint64_t> g_halo2DrawsInsideEye[2]{};
    std::atomic<uint64_t> g_halo2DrawsOutsideEye{0};
    std::atomic<uint64_t> g_halo2SynchronousEyeSerial[2]{};
    std::atomic<uint32_t> g_halo2SynchronousEyeGeneration[2]{};
    std::atomic<uint32_t> g_halo2SynchronousEyeResourceEpoch[2]{};
    std::atomic<float> g_halo2SynchronousEyeHalfFovX[2]{};
    std::atomic<float> g_halo2SynchronousEyeHalfFovY[2]{};
    // E-H2-18: the absolute OpenXR view pose each eye image was rendered
    // from, when the renderer named one (position xyz, orientation xyzw).
    std::atomic<float> g_halo2SynchronousEyePose[2][7]{};
    std::atomic<bool> g_halo2SynchronousEyePoseOwned[2]{};
    std::atomic<uint64_t> g_halo2XrPairsPosedFromObserver{0};
    std::atomic<uint64_t> g_halo2SynchronousCaptureSerial[2]{};
    std::atomic<uint32_t> g_halo2SynchronousCaptureCount[2]{};
    std::atomic<uint32_t> g_halo2SynchronousResourceEpoch{1};
    // Resource-free presentation history. The even sequence is the publication
    // anchor; odd means a writer owns the payload. Unlike pair/cache state this
    // exact integer-only stamp deliberately survives reset until a newer
    // process-monotonic prepared serial replaces it.
    std::atomic<uint64_t> g_halo2SynchronousPresentationSequence{};
    std::atomic<uint64_t> g_halo2SynchronousPresentationSerial{};
    std::atomic<uint32_t> g_halo2SynchronousPresentationGeneration{};
    std::atomic<uint32_t> g_halo2SynchronousPresentationDisposition{};
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> g_halo2XrPairsSubmitted{};
    std::atomic<uint64_t> g_halo2XrPairsDropped{};
    std::atomic<uint32_t> g_halo2LastDropReason{};
    std::atomic<bool> g_halo2FirstPairAccepted{};

    struct Halo2SynchronousEyeScope
    {
        bool active = false;
        bool completed = false;
        uint32_t generation = 0;
        uint32_t resourceEpoch = 0;
        uint64_t preparedSerial = 0;
        int eye = -1;
        uint32_t captures = 0;
        ID3D11RenderTargetView* finalRtv = nullptr;
        ID3D11RenderTargetView* sceneRtv = nullptr;
        // E-H2-24: every DISTINCT slot-0 target the engine bound while this
        // eye's scope was open, oldest first (pointers only; recorded in the
        // OM hot hook). The classic per-eye image is in one of these, not in
        // either of the two slots the capture used to choose between.
        ID3D11RenderTargetView* lastBoundRtv = nullptr;
        ID3D11RenderTargetView* bound[kHalo2EyeBoundRtvSlots]{};
        int boundCount = 0;
        uint32_t binds = 0;
    };
    thread_local Halo2SynchronousEyeScope g_halo2SynchronousEyeScope{};
    // E-H2-8 capture-source learning. 0 = final-output slot 0x197EE58,
    // 1 = primary scene slot 0x197EE60, 2 = last target bound in the eye.
    std::atomic<int> g_halo2CaptureSource{0};
    ID3D11Texture2D* g_halo2ProbeCache[2][2]{};   // [probe slot][eye]
    bool g_halo2ProbeHasImage[2][2]{};
    // E-H2-24: which candidate each probe slot copied, the pointer it copied,
    // how many candidates the eye offered, and where the rotation stands.
    // [probe slot][eye]: the candidate index AND the RTV copied, so the
    // 2 s comparison only compares two copies of the SAME target.
    std::atomic<int> g_halo2ProbeCandidate[2][2]{{{-1}, {-1}}, {{-1}, {-1}}};
    std::atomic<uintptr_t> g_halo2ProbeCandidatePtr[2][2]{};
    std::atomic<uint64_t> g_halo2CaptureSourceFallbacks{0};
    // E-H2-27: the probe used to demand the backbuffer's exact shape, so the
    // engine's own scene target (3788x2732, typeless) was never sampled -
    // every probe of it read "not sampled". A small centre box copied with
    // CopySubresourceRegion works for ANY shape and any 32-bit format.
    constexpr unsigned kHalo2ProbeBoxSize = 256;
    ID3D11Texture2D* g_halo2ProbeBox[2][2]{};
    DXGI_FORMAT g_halo2ProbeBoxFormat[2][2]{};
    bool g_halo2ProbeBoxHasImage[2][2]{};
    std::atomic<bool> g_halo2PrePairCreateFailed{false};
    std::atomic<int> g_halo2CaptureCandidateCount{2};
    std::atomic<int> g_halo2ProbeRotation{0};
    // E-H2-24 (C-H2-31): the capture source as it stood BEFORE the pair's
    // first eye rendered, copied on request (one copy every pixel check, not
    // every frame). Comparing it with the two eye copies says whether the
    // engine's eye pass changed the target at all (capture-side problem) or
    // changed it to the same picture twice (camera-side problem).
    ID3D11Texture2D* g_halo2PrePairCache = nullptr;
    ID3D11Texture2D* g_halo2ScopeSource=nullptr;
    ID3D11RenderTargetView* g_halo2ScopeSourceRtv=nullptr; // identity only; source owns lifetime
    D3D11_TEXTURE2D_DESC g_halo2ScopeSourceDesc{};
    uint32_t g_halo2ScopeGeneration=0;
    uint64_t g_halo2ScopeSerial=0;
    bool g_halo2ScopeCopyActive=false,g_halo2ScopeSourceBound=false;
    std::atomic<bool> g_halo2PrePairWanted{false};
    // E-H2-34: a one-shot eye-picture request (tick count it is due at).
    std::atomic<uint64_t> g_halo2EyeDumpDueMs{0};
    // Stage 3AM: full-resolution GPU->CPU validation is bounded capture-source
    // discovery, never steady-state telemetry. A source fallback rearms it.
    std::atomic<bool> g_halo2HeavyValidationRetired{false};
    std::atomic<int> g_halo2HeavyValidationSource{-1};
    std::atomic<unsigned> g_halo2HeavyValidationChecks{0};
    std::atomic<bool> g_halo2PrePairHasImage{false};
    std::atomic<uint64_t> g_halo2PrePairSerial{0};
    D3D11_TEXTURE2D_DESC g_halo2ProbeDesc{};
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
    constexpr uint64_t kHalo2PendingReservationBit = uint64_t{1} << 63;
    constexpr bool Halo2PendingSerialIsReserved(uint64_t value) noexcept
    {
        return (value & kHalo2PendingReservationBit) != 0;
    }
    constexpr uint64_t Halo2ReservedPendingSerial(uint64_t serial) noexcept
    {
        return serial | kHalo2PendingReservationBit;
    }
    // The H2 hook writes a completed token only after its single stock render
    // returns and its two 12-byte camera-position writes are restored. Present
    // consumes that exact token into the shared persistent eye caches.
    std::atomic<uint64_t> g_halo2PendingSerial{};
    std::atomic<uint32_t> g_halo2PendingGeneration{};
    std::atomic<int> g_halo2PendingEye{-1};
    std::atomic<float> g_halo2PendingHalfFovX{};
    std::atomic<float> g_halo2PendingHalfFovY{};
    std::atomic<uint64_t> g_halo2EyeSerial[2]{};
    std::atomic<uint32_t> g_halo2EyeGeneration[2]{};
    std::atomic<float> g_halo2EyeHalfFovX[2]{};
    std::atomic<float> g_halo2EyeHalfFovY[2]{};
    std::atomic<uint64_t> g_halo2XrPairsSubmitted{};
    std::atomic<uint64_t> g_halo2XrPairsDropped{};
    std::atomic<uint32_t> g_halo2LastDropReason{};
    std::atomic<bool> g_halo2FirstPairAccepted{};
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
    constexpr uint32_t kHalo2RenderSnapshotWriter = 0x80000000u;
    constexpr uint32_t kHalo2RenderSnapshotInvalid = 0xFFFFFFFFu;
    Halo2VrRenderSnapshot g_halo2RenderSnapshots[2]{};
    std::atomic<uint32_t> g_halo2RenderSnapshotStates[2]{};
    std::atomic<uint32_t> g_halo2RenderSnapshotIndex{
        kHalo2RenderSnapshotInvalid};
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<Halo2VrRenderSnapshot>);
#endif
#if HALOMCCVR_HALO2_STEREO6DOF
    // Invalidate-before-write publication. Readers accept the three duration
    // fields only when both serial reads agree with the exact prepared frame.
    std::atomic<uint64_t> g_halo2PreparedCadenceSerial{};
    std::atomic<uint64_t> g_halo2PreparedTargetPeriodNs{};
    std::atomic<uint64_t> g_halo2PreparedDisplayDeltaNs{};
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    std::atomic<uint64_t> g_halo4EyeSerial[2]{};
    enum class Halo4XrPairDropReason : uint32_t
    {
        None = 0,
        EyeUploadIncomplete,
        ProjectionIncomplete,
    };
    std::atomic<uint64_t> g_halo4XrPairsSubmitted{};
    std::atomic<uint64_t> g_halo4XrPairsDropped{};
    std::atomic<uint32_t> g_halo4LastXrPairDropReason{};
#endif
    bool g_stereoValidationDone = false;
    std::atomic<int> g_rasterEye{-1};
    std::atomic<DWORD> g_rasterOwnerThread{0};
    bool g_rasterRedirected[2] = {false, false};
    IDXGISwapChain* g_gameSwapchain = nullptr; // borrowed; owned by the game
    // The internal scene-color RTV is stable after the first eye render. Keep
    // one reference and use a pointer comparison in the OMSetRenderTargets
    // hook. The retired census path performed GetBuffer/GetResource/QI/GetDesc
    // plus a 128-entry linear scan on nearly every RTV bind and could collapse
    // stereo from 90 fps into the 20s.
    ID3D11RenderTargetView* g_sceneColorRtv = nullptr;

    // --- Optional NVIDIA DLSS eye resolve (config.upscaler) ----------------
    // Everything below is render-thread state. The OM hook only ever does
    // pointer compares here (plus one bounded description per newly seen
    // depth view); allocation, NGX and logging happen at eye end or in the
    // Present-time publish.
    struct DlssEyeState
    {
        dlss::CameraSample current{};
        dlss::CameraSample previous{};
        bool published = false;   // a camera sample arrived for this eye render
        bool depthValid = false;  // the depth copy landed for this eye render
        bool depthDirect = false; // read the engine SRV before the next eye
        bool resetPending = true; // tell DLSS to drop its history next time
        ID3D11Texture2D* depthCopy = nullptr;            // engine depth, typeless
        ID3D11ShaderResourceView* depthCopySrv = nullptr;
        ID3D11ShaderResourceView* borrowedCeDepth = nullptr; // exact CE color/depth lease only
        D3D11_TEXTURE2D_DESC depthCopyDesc{};
        ID3D11Texture2D* colorIn = nullptr;              // UNORM twin of an sRGB eye
        D3D11_TEXTURE2D_DESC colorInDesc{};
        ID3D11Texture2D* motion = nullptr;               // RG16F, previous - current
        ID3D11RenderTargetView* motionRtv = nullptr;
        ID3D11ShaderResourceView* motionSrv = nullptr;
        ID3D11Texture2D* depthOut = nullptr;             // R32F raw depth for DLSS
        ID3D11RenderTargetView* depthOutRtv = nullptr;
        ID3D11Texture2D* output = nullptr;               // DLSS result, UAV-capable
        D3D11_TEXTURE2D_DESC outputDesc{};
        // This render's colour as a view of colorIn (the source of the flow
        // input copy), and the optical-flow input frames the backend hands
        // out (BGRA8; each render's colour is copied into the one
        // Dlss_FlowInputIndex names). Null without object motion.
        ID3D11ShaderResourceView* colorInSrv = nullptr;
        ID3D11Texture2D* flowInput[2]{};
        ID3D11RenderTargetView* flowInputRtv[2]{};
        // The last shape whose textures could not be created; that shape is
        // not retried every frame (the DLSS-9 log: two lines per eye per
        // frame, 30,000 lines in two minutes).
        uint32_t failedRenderW = 0, failedRenderH = 0, failedOutputW = 0, failedOutputH = 0;
    };
    DlssEyeState g_dlssEye[2];

    // One fresh-left + fresh-right atlas evaluated by one temporal feature.
    // The halves never alternate across frames: eye 0 always occupies the
    // left half and eye 1 the right half, so NGX history remains stable.
    struct DlssStereoBatch
    {
        ID3D11Texture2D* color = nullptr;
        ID3D11Texture2D* motion = nullptr;
        ID3D11Texture2D* depth = nullptr;
        ID3D11Texture2D* output = nullptr;
        ID3D11Texture2D* eyeOutput[2]{};
        ID3D11RenderTargetView* motionRtv = nullptr;
        ID3D11RenderTargetView* depthRtv = nullptr;
        D3D11_TEXTURE2D_DESC inputDesc{};
        D3D11_TEXTURE2D_DESC outputDesc{};
        D3D11_TEXTURE2D_DESC eyeOutputDesc{};
        dlss::StereoBatchPlan plan{};
        bool eyePrepared[2]{};
        bool reset = false;
    };
    DlssStereoBatch g_dlssBatch;

    // An eye whose upscale is in flight. DlssKickEye submits it the moment
    // the eye's render ends; DlssFinishEye, at present, waits once for both
    // eyes and stretches the finished image into the headset image.
    struct DlssKickedEye
    {
        ID3D11Texture2D* output = nullptr;  // null: already written (debug view)
        D3D11_TEXTURE2D_DESC outputDesc{};
    };
    dlss::FrameResult<DlssKickedEye> g_dlssKicked[2];
    // The depth view the game binds together with the learned scene-colour
    // target. Kept alive with one reference exactly like g_sceneColorRtv.
    ID3D11DepthStencilView* g_dlssSceneDepthDsv = nullptr;
    bool g_dlssEyeSceneDepthSeen = false;
    // Fallback rule: the last full-raster depth view bound inside the eye.
    // Every distinct view is described once and then referenced so its
    // pointer cannot be recycled for a different-shaped buffer.
    struct DlssDepthViewEntry
    {
        ID3D11DepthStencilView* view = nullptr;
        ID3D11Device* device = nullptr; // borrowed while retained DSV is alive
        bool fullRaster = false;
        D3D11_TEXTURE2D_DESC desc{};
    };
    constexpr int kDlssDepthViewSlots = 8;
    DlssDepthViewEntry g_dlssDepthViews[kDlssDepthViewSlots]{};
    int g_dlssDepthViewCount = 0;
    bool g_dlssDepthViewTableFull = false;
    ID3D11DepthStencilView* g_dlssEyeLastFullRasterDsv = nullptr;
    // The eye whose DLSS depth window is open (Reach renders its eyes
    // outside the raster-eye scope); -1 otherwise. Read by the OM hook.
    std::atomic<int> g_dlssDepthWindowEye{-1};
    ID3D11PixelShader* g_dlssMotionPs = nullptr;
    ID3D11PixelShader* g_dlssDebugPs = nullptr;
    ID3D11PixelShader* g_dlssCopyPs = nullptr;   // eye colour -> optical-flow input
    ID3D11Buffer* g_dlssMotionCb = nullptr;
    bool g_dlssMotionInitFailed = false;
    uint32_t g_dlssJitterPhase = 0;
    // Titles jitter their projection only after a frame in which both eyes
    // actually resolved through DLSS; a jittered image with no consumer would
    // just shake.
    bool g_dlssJitterArmed = false;
    uint32_t g_dlssEyesResolvedThisFrame = 0;
    uint64_t g_dlssResolvedEyes = 0;
    uint64_t g_dlssFallbackEyes = 0;
    uint64_t g_dlssResets = 0;
    bool g_dlssDepthSourceLogged = false;
    bool g_dlssResourcesLogged = false;
    uint64_t g_dlssFallbackLogMs = 0;
    char g_dlssFallbackReason[160]{};
    // A persistent optional-feature failure must return the game to its full
    // render size, rather than stretching the reduced DLSS input forever.
    bool g_dlssBatchAttemptedThisFrame = false;
    uint32_t g_dlssBatchFailedFrames = 0;
    bool g_dlssBatchSuppressed = false;
    // Plain-language state for the F1 menu.
    SRWLOCK g_dlssStatusLock = SRWLOCK_INIT;
    char g_dlssStatusText[256] = "off";
    uint64_t g_dlssStatusMs = 0;

    // Halo 4 scene-target discovery state. Two-phase, because deciding at bind
    // time picked an early pass and put unlit geometry in the headset: phase 1
    // watches a whole eye and remembers the LAST qualifying target, phase 2
    // redirects it once two consecutive eyes name the same one. Declared here
    // so both the resize/detach reset and the end-of-eye resolver can reach it.
    ID3D11RenderTargetView* g_h4LastCandidate = nullptr;
    D3D11_TEXTURE2D_DESC g_h4LastCandidateDesc{};
    DXGI_FORMAT g_h4LastCandidateViewFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11RenderTargetView* g_h4PreviousEyeCandidate = nullptr;
    unsigned g_h4CandidatesThisEye = 0;
    unsigned g_h4LearningEyes = 0;

    // Only ever holds raw comparison pointers, never a reference: the latched
    // target is the one that gets AddRef'd, and it lives in g_sceneColorRtv.
    void Halo4ResetSceneTargetDiscovery()
    {
        g_h4LastCandidate = nullptr;
        g_h4LastCandidateDesc = {};
        g_h4LastCandidateViewFormat = DXGI_FORMAT_UNKNOWN;
        g_h4PreviousEyeCandidate = nullptr;
        g_h4CandidatesThisEye = 0;
        g_h4LearningEyes = 0;
    }
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    ID3D11Resource* g_sceneColorResource = nullptr;
    struct NativeHudEyeRouteState
    {
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11RenderTargetView* phaseOutputRtv = nullptr;
        D3D11_VIEWPORT viewports[
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        D3D11_RECT scissors[
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = 0;
        UINT scissorCount = 0;
        int eye = -1;
        bool active = false;
        bool bypassOmRedirect = false;
        bool targetCopy = false;
    };
    thread_local NativeHudEyeRouteState g_nativeHudEyeRoute{};
    std::atomic<unsigned> g_nativeHudPhaseScopes{0};
    std::atomic<unsigned> g_nativeHudProvenOmMatches{0};
    std::atomic<unsigned> g_nativeHudExactCopyScopes{0};
    std::atomic<unsigned> g_nativeHudCopySubstitutions{0};
#endif
    D3D11_TEXTURE2D_DESC g_gameBackbufferDesc{};
    bool g_gameBackbufferDescValid = false;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    ReachDirectCopyGate g_reachDirectCopyGate;
    ReachModuleEpoch g_reachDisplayEpoch{};
    uint64_t g_reachDisplayResourceRevision = 0;
    uint64_t g_reachDisplayNextAttemptMs = 0;
    uint64_t g_reachDisplayLastFailureLogMs = 0;
    bool g_reachDisplayReadyLogged = false;
    SRWLOCK g_reachDisplayResourceLock = SRWLOCK_INIT;
    std::atomic<bool> g_reachPresentSelectedEligible{false};
    std::atomic<uint64_t> g_reachPresentAvailabilitySetEpochMs{0};
    std::atomic<uint32_t> g_reachPresentGeneration{0};
    std::atomic<uintptr_t> g_reachPresentModuleBase{0};
    std::atomic<uint64_t> g_reachDisplayLifecycleSerial{1};
    std::atomic<uint64_t> g_reachPresentNextSnapshotMs{0};
    std::atomic<bool> g_reachResizeActive{false};
    ID3D11Texture2D* g_reachEyeCache[2]{};
    ID3D11Texture2D* g_reachCaptureSource = nullptr;
    ID3D11Texture2D* g_reachCaptureEyes[2]{};
    ID3D11DeviceContext* g_reachCaptureContext = nullptr;
    ReachDisplaySurfaceProof g_reachCaptureProof{};
    D3D11_TEXTURE2D_DESC g_reachCaptureDesc{};
    std::atomic<bool> g_reachCaptureEnabled{false};
    std::atomic<uint32_t> g_reachCaptureUsers{0};
    std::atomic<uint64_t> g_reachEyeSerial[2]{};
#endif
    // Retained immediately after Present using the flip chain's current buffer
    // index. ODST can copy it after each death-camera eye draw without COM
    // discovery in the hot render path.
    std::atomic<ID3D11Texture2D*> g_nextGameBackbuffer{nullptr};
    IDXGISwapChain* g_flipIndexOwner = nullptr;
    IDXGISwapChain3* g_flipIndexChain = nullptr;

    // Where the virtual screen sits: yaw-only orientation + head position,
    // captured once at start (and again on "re-center").
    XrQuaternionf g_centerRot{0, 0, 0, 1};
    XrVector3f g_centerPos{0, 0, 0};
    bool g_haveCenter = false;
    // Recenter requests can originate on the input/game camera threads. Only
    // Present owns g_haveCenter and the OpenXR locate used to replace it.
    std::atomic<bool> g_recenterRequested{false};
    std::atomic<uint32_t> g_crouchCalibrationEpoch{1};

    // Screen placement while head tracking is on. World-locked (default) reads
    // as natural because turning your head shifts the screen in your view to
    // match your head motion; head-locked keeps it pinned in front but feels
    // disconnected. Toggle with F10.
    std::atomic<bool> g_screenFollow{false};
    // Input threads request a target; the render thread owns the 200 ms
    // fade-out, presentation switch, and fade-in.
    std::atomic<int> g_pauseRequest{-1};
    std::atomic<bool> g_pausePresentation{false};
    std::atomic<bool> g_pauseTarget{false};
    std::atomic<bool> g_cutsceneTheaterActive{false};
    CutsceneTheaterTransition g_cutsceneTheaterTransition;
    // Render-thread-only admission latches. A failed entry recenter blocks only
    // that one qualified cinematic; the next unqualified frame clears it.
    bool g_cutsceneTheaterWasQualified = false;
    bool g_cutsceneTheaterEntryBlocked = false;
    float g_cutsceneTheaterAuthoredAspect = 0.0f;

    // Latest head pose in the LOCAL space, captured every frame on the render
    // thread and read by the game camera hook (M1) on the game thread — hence
    // the lock. Orientation is a quaternion, position is in meters.
    CRITICAL_SECTION g_headCs;
    bool g_headCsInit = false;
    XrPosef g_headPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_headPoseValid = false;
    // Protected by g_headCs, or read by the sole pose-publishing frame thread.
    // Optional stock geometry may only combine poses located for the same time.
    XrTime g_stockHeadPoseTime = 0;
    XrTime g_stockControllerPoseTime = 0;
    std::atomic<uint64_t> g_roomscaleHeadSampleMs{0};
    XrPosef g_rightAimPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_rightAimPoseValid = false;
    PhysicalMeleeSpeedHistory g_meleeSpeedHistory[2]{};
    // Title interfaces use right=primary, left=support/secondary roles.
    std::atomic<bool> g_capturedLeftHanded{false};
    XrPosef g_physicalControllerPose[2]{};
    bool g_physicalControllerValid[2]{};
    uint64_t g_physicalControllerAtMs = 0;
    XrVector3f g_rightAimLinearVelocity{};
    bool g_rightAimLinearVelocityValid = false;
    uint64_t g_rightAimLinearVelocityAtMs = 0;
    XrPosef g_leftAimPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_leftAimPoseValid = false;
    XrVector3f g_leftAimLinearVelocity{};
    bool g_leftAimLinearVelocityValid = false;
    uint64_t g_leftAimLinearVelocityAtMs = 0;
    struct ControllerPoseVelocityHistory
    {
        bool valid = false;
        XrTime sampleTime = 0;
        XrVector3f position{};
    };
    ControllerPoseVelocityHistory g_controllerPoseVelocityHistory[2]{};
    // Render-thread-only filtered copy for the compositor crosshair. Keeping it
    // separate is intentional: weapon steering and bullets stay on raw aim.
    XrPosef g_reticleAimPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_reticleAimPoseValid = false;
    struct ReticleAimPosePublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<uint8_t> valid{0};
        std::atomic<float> qx{0.0f};
        std::atomic<float> qy{0.0f};
        std::atomic<float> qz{0.0f};
        std::atomic<float> qw{1.0f};
        std::atomic<float> px{0.0f};
        std::atomic<float> py{0.0f};
        std::atomic<float> pz{0.0f};
    };
    ReticleAimPosePublication g_presentedReticleAimPose;

    void PublishPresentedReticleAimPose(const XrPosef* pose)
    {
        auto& published = g_presentedReticleAimPose;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        const bool valid = pose != nullptr;
        published.sampleMs.store(
            valid ? GetTickCount64() : 0, std::memory_order_relaxed);
        published.valid.store(valid ? 1u : 0u, std::memory_order_relaxed);
        published.qx.store(valid ? pose->orientation.x : 0.0f,
                           std::memory_order_relaxed);
        published.qy.store(valid ? pose->orientation.y : 0.0f,
                           std::memory_order_relaxed);
        published.qz.store(valid ? pose->orientation.z : 0.0f,
                           std::memory_order_relaxed);
        published.qw.store(valid ? pose->orientation.w : 1.0f,
                           std::memory_order_relaxed);
        published.px.store(valid ? pose->position.x : 0.0f,
                           std::memory_order_relaxed);
        published.py.store(valid ? pose->position.y : 0.0f,
                           std::memory_order_relaxed);
        published.pz.store(valid ? pose->position.z : 0.0f,
                           std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    // Blit (copy-with-format-conversion) resources, created on demand
    ID3D11VertexShader* g_blitVs = nullptr;
    ID3D11PixelShader* g_blitPsLinearize = nullptr; // sRGB-decodes in the shader
    ID3D11PixelShader* g_blitPsPass = nullptr;
    ID3D11SamplerState* g_blitSampler = nullptr;
    ID3D11RasterizerState* g_blitRasterizer = nullptr;
    ID3D11DepthStencilState* g_blitDepthOff = nullptr;
    // Source views for the upload path, and SRV-capable intermediate copies for
    // sources we cannot sample directly.
    //
    // Both used to be a SINGLE slot. The eye publish alternates between
    // g_eyeCache[0] and g_eyeCache[1], so a one-entry cache keyed on the source
    // pointer missed on every eye of every frame and asked the device for a new
    // view each time - a COM call per eye per frame in a hot hook. The
    // intermediate was keyed on shape, so two different-sized slow-path sources
    // in one frame destroyed and recreated a FULL-RESOLUTION texture every
    // frame, which the comment in Blit() below already names as the kind of
    // cost that halves the frame rate on level load.
    //
    // VRAM, deliberately: the two capacities are NOT the same kind of thing.
    //
    // A source view is a descriptor, not an image - a few hundred bytes of
    // driver memory - so 32 of them cost nothing measurable. What a view DOES
    // cost is a strong reference on its texture, which is why every site that
    // destroys a sampled texture calls ForgetSourceView. In steady play the
    // cached set is the two eyes, the menu, the screen quad, the reticle and
    // the scope: all textures that are alive regardless. Added VRAM: none.
    //
    // An intermediate IS a full copy of its source, so this number is a
    // CEILING, not a preallocation - and that distinction is the whole reason
    // 32 is safe here. Slots fill lazily, one per distinct shape actually
    // requested, and only sources we cannot sample directly ever reach this
    // pool at all: a non-SRV-capable or multisampled texture. The eye caches
    // are neither (the scene target carries a UAV binding, which forces
    // single-sampling, and they are created SRV-capable), so the whole stereo
    // path skips this pool. In practice one or two shapes are ever live.
    //
    // An earlier revision capped this at 4, sized against a worst case of 32
    // full-resolution copies at ~29 MB each. That worst case cannot occur: it
    // would need 32 genuinely different shapes in one session, and the pool
    // holds only what is asked for. Sizing against an unreachable worst case
    // just reintroduced a smaller version of the thrash this pool exists to
    // remove. The live slot count and its real byte total are logged every two
    // seconds, so the actual figure is read from a session rather than argued
    // about here - and LRU eviction plus the full drain on resize and detach
    // bound it either way.
    constexpr std::size_t kSrcViewCacheCapacity = 32;
    constexpr std::size_t kIntermediatePoolCapacity = 32;
    ViewCacheTable<kSrcViewCacheCapacity> g_srcSrvCache;
    IntermediatePoolTable<kIntermediatePoolCapacity> g_intermediatePool;
    ID3D11Texture2D* g_intermediateTex[kIntermediatePoolCapacity]{};
    ID3D11ShaderResourceView* g_intermediateSrvs[kIntermediatePoolCapacity]{};
    // Steady-state creation must reach zero. These counters are what prove it
    // from one session instead of from an argument.
    uint64_t g_srcViewCreated = 0;
    uint64_t g_intermediateCreated = 0;

    // Image-quality pipeline: sharp bicubic resolve + selectable FXAA/SMAA 1x +
    // RCAS-based sharpen, applied when each eye is expanded into the headset.
    // SMAA is lazy/opt-in and adds its third eye-sized target only while selected.
    ID3D11PixelShader* g_iqResolveSharp = nullptr;   // sharp Keys bicubic resolve/upscale
    ID3D11PixelShader* g_iqResolveLinear = nullptr;  // linear resolve (matches old blit)
    ID3D11PixelShader* g_iqFxaa = nullptr;
    ID3D11PixelShader* g_iqRcas = nullptr;
    ID3D11PixelShader* g_iqDlssRcas = nullptr;
    ID3D11Buffer* g_iqCb = nullptr;
    ID3D11Texture2D* g_iqChain[3] = {nullptr, nullptr, nullptr};
    ID3D11RenderTargetView* g_iqChainRtv[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* g_iqChainSrv[3] = {nullptr, nullptr, nullptr};
    D3D11_TEXTURE2D_DESC g_iqChainDesc{};

    // Theatre is rasterized into the ordinary two-view projection layer. The
    // default IQ configuration samples the title eye caches directly, so this
    // costs the same two output draws as immersive presentation. Non-default
    // IQ modes retain their existing resolve/AA/sharpen chain and use these two
    // private textures only for its final room-screen draw.
    ID3D11Texture2D* g_theaterResolved[2] = {nullptr, nullptr};
    ID3D11RenderTargetView* g_theaterResolvedRtv[2] = {nullptr, nullptr};
    ID3D11ShaderResourceView* g_theaterResolvedSrv[2] = {nullptr, nullptr};
    D3D11_TEXTURE2D_DESC g_theaterResolvedDesc{};
    ID3D11Texture2D* g_theaterDirectSourceKey[2] = {nullptr, nullptr};
    ID3D11ShaderResourceView* g_theaterDirectSourceSrv[2] = {nullptr, nullptr};
    // Subtitles are interface text, not a CHUD widget, and the title draws them
    // into its finished backbuffer after both eye captures - which is why they
    // reach the monitor and never the headset. Keep a copy of the lower band of
    // that backbuffer while theatre is active and let the projection shader put
    // the text back on the room-fixed screen at the position the title chose.
    // Every failure here leaves the existing stereo theatre exactly as it was.
    ID3D11Texture2D* g_theaterSubtitleBand = nullptr;
    ID3D11ShaderResourceView* g_theaterSubtitleBandSrv = nullptr;
    D3D11_TEXTURE2D_DESC g_theaterSubtitleBandDesc{};
    // First backbuffer V the band holds; negative means no band this frame.
    float g_theaterSubtitleBandStartV = -1.0f;
    ID3D11VertexShader* g_theaterProjectionVs = nullptr;
    ID3D11PixelShader* g_theaterProjectionPs = nullptr;
    ID3D11Buffer* g_theaterProjectionCb = nullptr;
    struct TheaterProjectionParams
    {
        float clipPositions[4][4]{};
        // x: source is sRGB; y: non-sRGB XR target needs perceptual output;
        // z: source already passed through the normal IQ output encoding.
        float color[4]{};
        // x/y: first and last source V the cine bars leave showing; z: nonzero
        // once those bars are active at all; w: first V covered by the subtitle
        // band, or negative when no band was captured.
        float matte[4]{};
        // x: paint the captured band unfiltered instead of selecting glyphs.
        float band[4]{};
    };
    static_assert(sizeof(TheaterProjectionParams) == 112);

    // Official SMAA 1x shaders and immutable lookup tables. They are created on
    // first SMAA selection and remain tiny; the third eye-sized target is the
    // mode-scoped allocation released when SMAA is deselected.
    ID3D11VertexShader* g_smaaEdgesVs = nullptr;
    ID3D11VertexShader* g_smaaWeightsVs = nullptr;
    ID3D11VertexShader* g_smaaNeighborhoodVs = nullptr;
    ID3D11PixelShader* g_smaaEdgesPs = nullptr;
    ID3D11PixelShader* g_smaaWeightsPs = nullptr;
    ID3D11PixelShader* g_smaaNeighborhoodPs = nullptr;
    ID3D11SamplerState* g_smaaPointSampler = nullptr;
    ID3D11Texture2D* g_smaaAreaTex = nullptr;
    ID3D11ShaderResourceView* g_smaaAreaSrv = nullptr;
    ID3D11Texture2D* g_smaaSearchTex = nullptr;
    ID3D11ShaderResourceView* g_smaaSearchSrv = nullptr;
    bool g_smaaInitFailed = false;
    struct IqParams // matches cbuffer IqParams (b0); 32 bytes
    {
        float srcW, srcH, dstW, dstH;
        float sharpness, srcIsSrgb, outPerceptual, aaStrength;
    };

    // Deliberately separate from the eye/menu blitter: scope upload failures
    // cannot poison the proven stereo presentation pipeline.
    ID3D11VertexShader* g_scopeUploadVs = nullptr;
    ID3D11PixelShader* g_scopeUploadPs = nullptr;
    ID3D11PixelShader* g_scopeUploadPsLinearize = nullptr;
    ID3D11SamplerState* g_scopeUploadSampler = nullptr;
    ID3D11RasterizerState* g_scopeUploadRasterizer = nullptr;
    ID3D11DepthStencilState* g_scopeUploadDepthOff = nullptr;

    // Status shown in the menu (only touched on the render thread)
    VrStatus g_status{};
    LARGE_INTEGER g_fpsTimer{};
    int g_fpsFrames = 0;

    // One OpenXR frame stays begun while Halo renders. VR_BeforePresent
    // submits it; after DXGI Present returns, VR_AfterPresent obtains the
    // runtime's exact prediction for the next Halo render.
    struct PreparedFrame
    {
        XrFrameState state{XR_TYPE_FRAME_STATE};
        bool begun = false;
        bool viewsValid = false;
        uint32_t viewCount = 0;
        uint64_t serial = 0;
        XrDuration predictedDisplayDelta = 0;
    };
    PreparedFrame g_preparedFrame{};
    uint64_t g_nextPreparedSerial = 0;
    std::atomic<bool> g_preparedShouldRender{false};
    std::atomic<uint64_t> g_preparedViewSerialPublished{0};

    template <size_t N>
    struct TimingRing
    {
        std::array<double, N> values{};
        size_t next = 0;
        size_t count = 0;
        void Add(double value)
        {
            if (!std::isfinite(value) || value < 0.0)
                return;
            values[next] = value;
            next = (next + 1) % N;
            if (count < N) ++count;
        }
    };
    TimingRing<512> g_presentIntervalsMs;
    TimingRing<512> g_presentDurationsMs;
    TimingRing<512> g_waitDurationsMs;
    TimingRing<512> g_endFrameDurationsMs;
    TimingRing<512> g_renderWindowMs;
    TimingRing<512> g_predictionErrorMs;
    LARGE_INTEGER g_qpcFrequency{};
    LARGE_INTEGER g_lastBeforePresentQpc{};
    LARGE_INTEGER g_beginFrameQpc{};
    uint64_t g_timingLogStartMs = 0;
    XrTime g_lastPredictedDisplayTime = 0;
    // OpenXR's application-frame period, straight from xrWaitFrame. It is the
    // cadence the runtime is currently asking this app to submit at, and can be
    // a fraction of the separately queried physical panel refresh. No rate is
    // assumed: 72/80/90/120/144 and fractional cadences remain raw runtime data.
    std::atomic<uint64_t> g_displayPeriodNs{0};

    // Exact transition capture. The render thread publishes one immutable POD
    // record only after the corresponding DXGI Present returns. The existing
    // 50 ms title worker drains this SPSC queue and is solely responsible for
    // transition detection, formatting, allocation, and LOG/file I/O.
    struct FramePacingRecord
    {
        uint64_t serial = 0;
        uint64_t waitSequence = 0;
        uint64_t overlappingWorkerWaitSequence = 0;
        uint64_t nextWaitDispatchSequence = 0;
        uint32_t sessionEpoch = 0;
        uint32_t layerCount = 0;
        int64_t predictedDisplayTimeNs = 0;
        int64_t predictedDisplayPeriodNs = 0;
        int64_t waitCallStartQpc = 0;
        int64_t waitCallEndQpc = 0;
        int64_t waitReadyQpc = 0;
        int64_t renderWaitStartQpc = 0;
        int64_t renderWaitEndQpc = 0;
        int64_t nextWaitDispatchGateStartQpc = 0;
        int64_t nextWaitDispatchGateEndQpc = 0;
        int64_t beginStartQpc = 0;
        int64_t beginEndQpc = 0;
        int64_t consumedSignalQpc = 0;
        int64_t prepareEndQpc = 0;
        int64_t beforePresentQpc = 0;
        int64_t endStartQpc = 0;
        int64_t endEndQpc = 0;
        int64_t presentStartQpc = 0;
        int64_t presentEndQpc = 0;
        int64_t afterPresentQpc = 0;
        int32_t waitResult = XR_SUCCESS;
        int32_t beginResult = XR_SUCCESS;
        int32_t endResult = XR_SUCCESS;
        int32_t presentResult = S_OK;
        uint32_t eventWaitResult = WAIT_OBJECT_0;
        uint32_t nextWaitEventResult = WAIT_OBJECT_0;
        uint8_t title = static_cast<uint8_t>(GameTitle::None);
        bool waitPacketCoherent = true;
        bool nextWaitDispatchObservedBeforeBegin = false;
        bool shouldRender = false;
        bool focused = false;
        bool stereo = false;
        bool headTracking = false;
        bool scopeActive = false;
        bool submitted = false;
        GameFramePerfCounters perf{};
    };
    static_assert(std::is_trivially_copyable_v<FramePacingRecord>);

    // The raw transition capture produced the evidence it was added for. Keep
    // the implementation available for a future targeted investigation, but
    // compile its render-thread producers and worker consumer out of normal
    // builds. The independent wait-pipeline fault reporting remains active.
    constexpr bool kEnableFramePacingTransitionCapture = false;
    // The raster-order trace produced its discovery evidence. Keep the bounded
    // implementation dormant without leaving atomics or logging in render hooks.
    constexpr bool kEnableRetiredRasterTrace = false;
    constexpr uint32_t kFramePacingQueueSize = 1024;
    std::array<FramePacingRecord, kFramePacingQueueSize> g_framePacingQueue{};
    std::atomic<uint32_t> g_framePacingQueueHead{0};
    std::atomic<uint32_t> g_framePacingQueueTail{0};
    std::atomic<uint64_t> g_framePacingQueueDrops{0};
    FramePacingRecord g_framePacingPending{}; // render thread only
    GameFramePerfCounters g_framePacingPerfStart{}; // render thread only
    std::atomic<uint32_t> g_framePacingSessionEpoch{0};

    void SubtractPerfCounters(const GameFramePerfCounters& after,
                              const GameFramePerfCounters& before,
                              GameFramePerfCounters& delta)
    {
        auto subtract = [](uint64_t end, uint64_t start) {
            return end >= start ? end - start : 0;
        };
        delta = {};
        delta.viewRenders = subtract(after.viewRenders, before.viewRenders);
        for (int eye = 0; eye < 3; ++eye)
        {
            delta.fpPaletteRequests[eye] = subtract(
                after.fpPaletteRequests[eye], before.fpPaletteRequests[eye]);
            delta.fpPaletteFullSolves[eye] = subtract(
                after.fpPaletteFullSolves[eye],
                before.fpPaletteFullSolves[eye]);
            delta.fpPaletteCacheHits[eye] = subtract(
                after.fpPaletteCacheHits[eye], before.fpPaletteCacheHits[eye]);
            delta.fpPaletteCacheStores[eye] = subtract(
                after.fpPaletteCacheStores[eye],
                before.fpPaletteCacheStores[eye]);
            delta.fpPaletteCacheFull[eye] = subtract(
                after.fpPaletteCacheFull[eye], before.fpPaletteCacheFull[eye]);
        }
        delta.zoomLogWrites = subtract(
            after.zoomLogWrites, before.zoomLogWrites);
        delta.viewRateLogWrites = subtract(
            after.viewRateLogWrites, before.viewRateLogWrites);
        delta.paletteRateLogWrites = subtract(
            after.paletteRateLogWrites, before.paletteRateLogWrites);
        delta.cameraRateLogWrites = subtract(
            after.cameraRateLogWrites, before.cameraRateLogWrites);
        delta.fpDriverRateLogWrites = subtract(
            after.fpDriverRateLogWrites, before.fpDriverRateLogWrites);
    }

    void PublishFramePacingRecord(const FramePacingRecord& record)
    {
        const uint32_t head =
            g_framePacingQueueHead.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kFramePacingQueueSize;
        if (next == g_framePacingQueueTail.load(std::memory_order_acquire))
        {
            g_framePacingQueueDrops.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_framePacingQueue[head] = record;
        g_framePacingQueueHead.store(next, std::memory_order_release);
    }

    bool ConsumeFramePacingRecord(FramePacingRecord& record)
    {
        const uint32_t tail =
            g_framePacingQueueTail.load(std::memory_order_relaxed);
        if (tail == g_framePacingQueueHead.load(std::memory_order_acquire))
            return false;
        record = g_framePacingQueue[tail];
        g_framePacingQueueTail.store(
            (tail + 1) % kFramePacingQueueSize, std::memory_order_release);
        return true;
    }

    constexpr size_t kFramePacingPreFrames = 64;
    constexpr size_t kFramePacingPostFrames = 128;
    constexpr size_t kFramePacingCaptureFrames =
        kFramePacingPreFrames + 1 + kFramePacingPostFrames;
    struct FramePacingWorkerState
    {
        std::array<FramePacingRecord, kFramePacingPreFrames> history{};
        size_t historyNext = 0;
        size_t historyCount = 0;
        std::array<FramePacingRecord, kFramePacingCaptureFrames> capture{};
        size_t captureCount = 0;
        size_t triggerIndex = 0;
        size_t postRemaining = 0;
        int64_t oldPeriodNs = 0;
        int64_t newPeriodNs = 0;
        uint64_t dropsAtStart = 0;
        uint64_t lastDataMs = 0;
        bool capturing = false;
        bool exact = true;
    };
    FramePacingWorkerState g_framePacingWorker{}; // title worker only

    bool IsEligiblePacingRecord(const FramePacingRecord& record)
    {
        return record.predictedDisplayPeriodNs > 0 && record.shouldRender &&
            record.focused && record.stereo && record.headTracking;
    }

    bool IsExactWaitRecord(const FramePacingRecord& record)
    {
        return record.waitPacketCoherent && record.waitSequence != 0 &&
            record.nextWaitDispatchObservedBeforeBegin &&
            record.nextWaitEventResult == WAIT_OBJECT_0;
    }

    bool IsExactWaitPair(const FramePacingRecord& previous,
                         const FramePacingRecord& current)
    {
        return IsExactWaitRecord(previous) && IsExactWaitRecord(current) &&
            current.waitSequence == previous.waitSequence + 1;
    }

    const FramePacingRecord* LastPacingHistory(
        const FramePacingWorkerState& worker)
    {
        if (!worker.historyCount)
            return nullptr;
        const size_t index =
            (worker.historyNext + kFramePacingPreFrames - 1) %
            kFramePacingPreFrames;
        return &worker.history[index];
    }

    void AddPacingHistory(FramePacingWorkerState& worker,
                          const FramePacingRecord& record)
    {
        worker.history[worker.historyNext] = record;
        worker.historyNext =
            (worker.historyNext + 1) % kFramePacingPreFrames;
        if (worker.historyCount < kFramePacingPreFrames)
            ++worker.historyCount;
    }

    void ClearPacingHistory(FramePacingWorkerState& worker)
    {
        worker.historyNext = 0;
        worker.historyCount = 0;
    }

    void BeginPacingCapture(FramePacingWorkerState& worker,
                            const FramePacingRecord& trigger,
                            int64_t oldPeriodNs)
    {
        worker.captureCount = 0;
        worker.exact = true;
        const size_t first =
            (worker.historyNext + kFramePacingPreFrames -
             worker.historyCount) % kFramePacingPreFrames;
        for (size_t i = 0; i < worker.historyCount; ++i)
        {
            const FramePacingRecord& prior =
                worker.history[(first + i) % kFramePacingPreFrames];
            if (worker.captureCount &&
                !IsExactWaitPair(
                    worker.capture[worker.captureCount - 1], prior))
            {
                worker.exact = false;
            }
            worker.capture[worker.captureCount++] = prior;
            if (!IsExactWaitRecord(prior))
                worker.exact = false;
        }
        worker.triggerIndex = worker.captureCount;
        worker.capture[worker.captureCount++] = trigger;
        worker.postRemaining = kFramePacingPostFrames;
        worker.oldPeriodNs = oldPeriodNs;
        worker.newPeriodNs = trigger.predictedDisplayPeriodNs;
        worker.dropsAtStart =
            g_framePacingQueueDrops.load(std::memory_order_relaxed);
        worker.lastDataMs = GetTickCount64();
        worker.capturing = true;
        if (!IsExactWaitRecord(trigger) ||
            (worker.triggerIndex && !IsExactWaitPair(
                worker.capture[worker.triggerIndex - 1], trigger)))
        {
            worker.exact = false;
        }
    }

    double PacingQpcMs(int64_t end, int64_t start, int64_t frequency)
    {
        if (!end || !start || !frequency)
            return -1.0;
        return static_cast<double>(end - start) * 1000.0 /
            static_cast<double>(frequency);
    }

    void LogPacingCapture(const FramePacingWorkerState& worker,
                          const char* completion)
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const uint64_t dropsNow =
            g_framePacingQueueDrops.load(std::memory_order_relaxed);
        const uint64_t dropsDuring = dropsNow - worker.dropsAtStart;
        const bool exact = worker.exact && dropsDuring == 0;
        const double oldHz = worker.oldPeriodNs > 0
            ? 1000000000.0 / static_cast<double>(worker.oldPeriodNs) : 0.0;
        const double newHz = worker.newPeriodNs > 0
            ? 1000000000.0 / static_cast<double>(worker.newPeriodNs) : 0.0;
        const size_t postCount = worker.captureCount > worker.triggerIndex
            ? worker.captureCount - worker.triggerIndex - 1 : 0;

        std::string text;
        text.reserve(worker.captureCount * 640 + 2048);
        char line[1536]{};
        snprintf(line, sizeof(line),
            "PACING TRANSITION CAPTURE: %lldns (%.3fHz) -> %lldns "
            "(%.3fHz), triggerSerial=%llu, pre=%zu post=%zu, "
            "completion=%s exact=%d queueDrops=%llu panel=%.3fHz\n",
            static_cast<long long>(worker.oldPeriodNs), oldHz,
            static_cast<long long>(worker.newPeriodNs), newHz,
            static_cast<unsigned long long>(
                worker.capture[worker.triggerIndex].serial),
            worker.triggerIndex, postCount, completion, exact ? 1 : 0,
            static_cast<unsigned long long>(dropsDuring),
            static_cast<double>(
                g_panelRefreshHz.load(std::memory_order_relaxed)));
        text.append(line);
        text.append(
            "PACING F fields: rel serial epoch edge periodNs appHz predDeltaMs "
            "beginStepMs presentStepMs waitSeq source conflictSeq dispatchSeq "
            "dispatchSeen coherent workerWaitMs readyAgeMs eventWaitMs "
            "dispatchAckMs "
            "beginMs pairExact resumeMs "
            "waitStartVsPrevBeginMs waitEndVsPrevBeginMs prepareMs "
            "gameMs submitBuildMs renderWindowMs endMs preDxgiMs dxgiMs "
            "afterDxgiMs flags(S/F/T/H/C) layers title xr(wait/begin/end) "
            "eventWait/nextWait dxgiHr\n");
        text.append(
            "PACING C fields: serial views requests[e0/e1/out] "
            "fullSolves[e0/e1/out] hits[e0/e1/out] stores[e0/e1/out] "
            "cacheFull[e0/e1/out] hotLogs[zoom/view/palette/camera/fpDriver]\n");
        text.append(
            "PACING C semantics: views=Halo3 outer stereo transactions; "
            "fullSolves=successful non-explicit arm-IK misses; "
            "out=eye -1 (includes scope and other outside-eye work)\n");

        for (size_t i = 0; i < worker.captureCount; ++i)
        {
            const FramePacingRecord& record = worker.capture[i];
            const FramePacingRecord* previous = i ? &worker.capture[i - 1] : nullptr;
            const bool contiguous = previous &&
                previous->sessionEpoch == record.sessionEpoch &&
                previous->serial + 1 == record.serial;
            const bool edge = contiguous && IsMaterialFramePeriodTransition(
                static_cast<uint64_t>(previous->predictedDisplayPeriodNs),
                static_cast<uint64_t>(record.predictedDisplayPeriodNs));
            const double predictedDeltaMs = contiguous
                ? static_cast<double>(record.predictedDisplayTimeNs -
                    previous->predictedDisplayTimeNs) / 1000000.0 : -1.0;
            const double beginStepMs = contiguous
                ? PacingQpcMs(record.beginEndQpc, previous->beginEndQpc,
                              frequency.QuadPart) : -1.0;
            const double presentStepMs = contiguous
                ? PacingQpcMs(record.presentStartQpc,
                              previous->presentStartQpc,
                              frequency.QuadPart) : -1.0;
            const bool exactPair = contiguous &&
                IsExactWaitPair(*previous, record);
            const double resumeMs = exactPair
                ? PacingQpcMs(record.waitCallStartQpc,
                              previous->consumedSignalQpc,
                              frequency.QuadPart) : -1.0;
            const double waitStartVsPreviousBeginMs = exactPair
                ? PacingQpcMs(record.waitCallStartQpc,
                              previous->beginStartQpc,
                              frequency.QuadPart) : -1.0;
            const double waitEndVsPreviousBeginMs = exactPair
                ? PacingQpcMs(record.waitCallEndQpc,
                              previous->beginEndQpc,
                              frequency.QuadPart) : -1.0;
            const long long relative = static_cast<long long>(i) -
                static_cast<long long>(worker.triggerIndex);
            const double appHz = record.predictedDisplayPeriodNs > 0
                ? 1000000000.0 /
                    static_cast<double>(record.predictedDisplayPeriodNs) : 0.0;

            snprintf(line, sizeof(line),
                "PACING F rel=%+lld serial=%llu epoch=%u edge=%d "
                "periodNs=%lld appHz=%.3f predDeltaMs=%.3f "
                "beginStepMs=%.3f presentStepMs=%.3f waitSeq=%llu "
                "source=W conflictSeq=%llu dispatchSeq=%llu dispatchSeen=%d "
                "coherent=%d workerWaitMs=%.3f readyAgeMs=%.3f "
                "eventWaitMs=%.3f dispatchAckMs=%.3f beginMs=%.3f "
                "pairExact=%d resumeMs=%.3f waitStartVsPrevBeginMs=%.3f "
                "waitEndVsPrevBeginMs=%.3f prepareMs=%.3f gameMs=%.3f "
                "submitBuildMs=%.3f renderWindowMs=%.3f endMs=%.3f "
                "preDxgiMs=%.3f dxgiMs=%.3f afterDxgiMs=%.3f "
                "flags=%d/%d/%d/%d/%d layers=%u title=%u "
                "xr=%d/%d/%d event=%lu/%lu dxgi=0x%08X\n",
                relative, static_cast<unsigned long long>(record.serial),
                record.sessionEpoch, edge ? 1 : 0,
                static_cast<long long>(record.predictedDisplayPeriodNs), appHz,
                predictedDeltaMs, beginStepMs, presentStepMs,
                static_cast<unsigned long long>(record.waitSequence),
                static_cast<unsigned long long>(
                    record.overlappingWorkerWaitSequence),
                static_cast<unsigned long long>(
                    record.nextWaitDispatchSequence),
                record.nextWaitDispatchObservedBeforeBegin ? 1 : 0,
                record.waitPacketCoherent ? 1 : 0,
                PacingQpcMs(record.waitCallEndQpc,
                            record.waitCallStartQpc, frequency.QuadPart),
                PacingQpcMs(record.renderWaitStartQpc,
                            record.waitReadyQpc, frequency.QuadPart),
                PacingQpcMs(record.renderWaitEndQpc,
                            record.renderWaitStartQpc, frequency.QuadPart),
                PacingQpcMs(record.nextWaitDispatchGateEndQpc,
                            record.nextWaitDispatchGateStartQpc,
                            frequency.QuadPart),
                PacingQpcMs(record.beginEndQpc,
                            record.beginStartQpc, frequency.QuadPart),
                exactPair ? 1 : 0,
                resumeMs,
                waitStartVsPreviousBeginMs,
                waitEndVsPreviousBeginMs,
                PacingQpcMs(record.prepareEndQpc,
                            record.beginEndQpc, frequency.QuadPart),
                PacingQpcMs(record.beforePresentQpc,
                            record.prepareEndQpc, frequency.QuadPart),
                PacingQpcMs(record.endStartQpc,
                            record.beforePresentQpc, frequency.QuadPart),
                PacingQpcMs(record.endStartQpc,
                            record.beginEndQpc, frequency.QuadPart),
                PacingQpcMs(record.endEndQpc,
                            record.endStartQpc, frequency.QuadPart),
                PacingQpcMs(record.presentStartQpc,
                            record.endEndQpc, frequency.QuadPart),
                PacingQpcMs(record.presentEndQpc,
                            record.presentStartQpc, frequency.QuadPart),
                PacingQpcMs(record.afterPresentQpc,
                            record.presentEndQpc, frequency.QuadPart),
                record.shouldRender ? 1 : 0, record.focused ? 1 : 0,
                record.stereo ? 1 : 0, record.headTracking ? 1 : 0,
                record.scopeActive ? 1 : 0, record.layerCount,
                static_cast<unsigned>(record.title), record.waitResult,
                record.beginResult, record.endResult,
                static_cast<unsigned long>(record.eventWaitResult),
                static_cast<unsigned long>(record.nextWaitEventResult),
                static_cast<unsigned>(record.presentResult));
            text.append(line);

            const auto& perf = record.perf;
            snprintf(line, sizeof(line),
                "PACING C serial=%llu views=%llu requests=%llu/%llu/%llu "
                "fullSolves=%llu/%llu/%llu hits=%llu/%llu/%llu "
                "stores=%llu/%llu/%llu cacheFull=%llu/%llu/%llu "
                "hotLogs=%llu/%llu/%llu/%llu/%llu\n",
                static_cast<unsigned long long>(record.serial),
                static_cast<unsigned long long>(perf.viewRenders),
                static_cast<unsigned long long>(perf.fpPaletteRequests[0]),
                static_cast<unsigned long long>(perf.fpPaletteRequests[1]),
                static_cast<unsigned long long>(perf.fpPaletteRequests[2]),
                static_cast<unsigned long long>(perf.fpPaletteFullSolves[0]),
                static_cast<unsigned long long>(perf.fpPaletteFullSolves[1]),
                static_cast<unsigned long long>(perf.fpPaletteFullSolves[2]),
                static_cast<unsigned long long>(perf.fpPaletteCacheHits[0]),
                static_cast<unsigned long long>(perf.fpPaletteCacheHits[1]),
                static_cast<unsigned long long>(perf.fpPaletteCacheHits[2]),
                static_cast<unsigned long long>(perf.fpPaletteCacheStores[0]),
                static_cast<unsigned long long>(perf.fpPaletteCacheStores[1]),
                static_cast<unsigned long long>(perf.fpPaletteCacheStores[2]),
                static_cast<unsigned long long>(perf.fpPaletteCacheFull[0]),
                static_cast<unsigned long long>(perf.fpPaletteCacheFull[1]),
                static_cast<unsigned long long>(perf.fpPaletteCacheFull[2]),
                static_cast<unsigned long long>(perf.zoomLogWrites),
                static_cast<unsigned long long>(perf.viewRateLogWrites),
                static_cast<unsigned long long>(perf.paletteRateLogWrites),
                static_cast<unsigned long long>(perf.cameraRateLogWrites),
                static_cast<unsigned long long>(perf.fpDriverRateLogWrites));
            text.append(line);
        }
        LOG("%s", text.c_str());
    }

    void SeedHistoryFromCapture(FramePacingWorkerState& worker)
    {
        const size_t count = worker.captureCount;
        const size_t first = count > kFramePacingPreFrames
            ? count - kFramePacingPreFrames : 0;
        ClearPacingHistory(worker);
        for (size_t i = first; i < count; ++i)
        {
            const FramePacingRecord& record = worker.capture[i];
            const FramePacingRecord* previous = LastPacingHistory(worker);
            if (!IsEligiblePacingRecord(record) ||
                (previous && (previous->sessionEpoch != record.sessionEpoch ||
                              previous->serial + 1 != record.serial)))
            {
                ClearPacingHistory(worker);
                if (!IsEligiblePacingRecord(record))
                    continue;
            }
            AddPacingHistory(worker, record);
        }
    }

    void FinishPacingCapture(FramePacingWorkerState& worker,
                             const char* completion)
    {
        LogPacingCapture(worker, completion);
        SeedHistoryFromCapture(worker);
        worker.capturing = false;
        worker.captureCount = 0;
        worker.postRemaining = 0;
        worker.exact = true;
    }

    // --- Frame-wait pipelining ------------------------------------------
    // xrWaitFrame blocks until the runtime's next display time. Calling it
    // inline on the game's render thread meant the game sat IDLE inside it:
    // measured 11.4ms of a 17.6ms frame while the real work (render + present
    // + blit) was only ~6ms, with prediction error collapsing to 0.03ms -- the
    // loop was locked onto every other display interval instead of merely being
    // slow. This thread absorbs that block so the wait overlaps the game's
    // rendering: by the time the render thread asks for a frame state, the wait
    // has already happened and it proceeds immediately.
    //
    // Pairing stays legal: after the render thread claims Wait(N), it releases
    // the worker immediately BEFORE Begin(N). OpenXR requires the subsequent
    // Wait(N+1) to block until Begin(N), so pacing remains on the worker and the
    // overlap covers exactly the game's own render window.
    //
    // NO refresh rate is referenced here. Whatever period the runtime reports
    // is what we use, so 72/80/90/120/144 and anything future behave the same.
    HANDLE g_waitThread = nullptr;
    HANDLE g_waitReadyEvent = nullptr;    // wait thread -> render thread
    HANDLE g_waitConsumedEvent = nullptr; // render thread -> wait thread
    HANDLE g_waitStartedEvent = nullptr;  // wait thread -> render thread
    std::atomic<bool> g_waitThreadStop{false};
    XrFrameState g_waitedFrameState{XR_TYPE_FRAME_STATE};
    std::atomic<bool> g_waitedStateValid{false};
    std::atomic<bool> g_waitedStateFailed{false};
    std::atomic<XrResult> g_waitedStateResult{XR_SUCCESS};
    std::atomic<uint64_t> g_waitPacketMisses{0};
    // A parked wait worker means the game has not reached Present yet. Normal
    // parks are one frame period; this is the threshold above which the player
    // is looking at a frozen reprojected image and thinks the game crashed.
    constexpr uint64_t kFrameStallNoticeMs = 1000;
    std::atomic<uint64_t> g_frameStalls{0};
    std::atomic<uint64_t> g_frameStallWorstMs{0};
    // Read by the wait worker while the render thread is stuck inside the game,
    // so a stall report can say whether the headset was actually SHOWING us. It
    // usually was not: a stall recorded while the runtime has the session merely
    // synchronized (app not visible, shouldRender=0, zero layers submitted) is
    // not what any player saw, and reading one as if it were cost real time on
    // 2026-07-28. The old wording asserted the player was staring at a frozen
    // frame; these two let the line state what was true instead.
    std::atomic<int> g_sessionStateShared{XR_SESSION_STATE_UNKNOWN};
    std::atomic<int> g_lastShouldRenderShared{-1};
    std::atomic<uint64_t> g_waitFailuresObserved{0};
    std::atomic<uint64_t> g_waitEventSignalFailures{0};
    std::atomic<uint64_t> g_waitCallSequence{0};
    std::atomic<uint64_t> g_waitCallInFlight{0};
    std::atomic<uint64_t> g_waitConsumedSequence{0};
    std::atomic<bool> g_waitPipelineFaulted{false};
    std::atomic<uint64_t> g_waitedPacketSequence{0};
    std::atomic<uint64_t> g_waitedPacketVersion{0};
    std::atomic<uint32_t> g_waitedSessionEpochStart{0};
    std::atomic<uint32_t> g_waitedSessionEpochEnd{0};
    std::atomic<int64_t> g_waitedCallStartQpc{0};
    std::atomic<int64_t> g_waitedCallEndQpc{0};
    std::atomic<int64_t> g_waitedReadyQpc{0};
    // Render-thread owned. A fatal frame-loop error requests STOPPING first so
    // PollEvents can end the running session before Fail switches VR off.
    const char* g_frameWaitFatalExitReason = nullptr;
    bool g_frameWaitExitRequestAccepted = false;
    uint64_t g_frameWaitExitLastRequestMs = 0;
    uint64_t g_frameWaitExitLastLogMs = 0;

    uint64_t g_missedPredictions = 0;
    uint64_t g_duplicatePredictions = 0;
    uint64_t g_frameOrderFailures = 0;
    std::atomic<uint64_t> g_preparedSerialPublished{0};
    std::atomic<uint64_t> g_prepareQpcPublished{0};
    std::atomic<uint64_t> g_cameraSerialObserved{0};
    std::atomic<uint64_t> g_firstCameraDelayUs{0};
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // Two fixed publication slots carry a coherent head/pad/eye sample from
    // PrepareNextFrame into Reach's later render transaction. The high state
    // bit is an exclusive writer claim; the remaining bits count pinned
    // readers. This closes the stale-reader-before-pin race that a bare
    // two-slot reader counter would leave.
    constexpr uint32_t kReachRenderSnapshotWriter = 0x80000000u;
    constexpr uint32_t kReachRenderSnapshotInvalid = 0xFFFFFFFFu;
    ReachVrRenderSnapshot g_reachRenderSnapshots[2]{};
    std::atomic<uint32_t> g_reachRenderSnapshotStates[2]{};
    std::atomic<uint32_t> g_reachRenderSnapshotIndex{
        kReachRenderSnapshotInvalid};
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<ReachVrRenderSnapshot>);
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    constexpr uint32_t kHalo4RenderSnapshotWriter = 0x80000000u;
    constexpr uint32_t kHalo4RenderSnapshotInvalid = 0xFFFFFFFFu;
    Halo4VrRenderSnapshot g_halo4RenderSnapshots[2]{};
    std::atomic<uint32_t> g_halo4RenderSnapshotStates[2]{};
    std::atomic<uint32_t> g_halo4RenderSnapshotIndex{
        kHalo4RenderSnapshotInvalid};
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<Halo4VrRenderSnapshot>);
#endif

    // ---------------------------------------------------------------- utils

    const char* XrStr(XrResult r)
    {
        static char buf[XR_MAX_RESULT_STRING_SIZE];
        if (g_instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(g_instance, r, buf)))
            return buf;
        snprintf(buf, sizeof(buf), "XrResult(%d)", (int)r);
        return buf;
    }

    double QpcMs(LONGLONG ticks)
    {
        if (!g_qpcFrequency.QuadPart)
            QueryPerformanceFrequency(&g_qpcFrequency);
        return ticks * 1000.0 / static_cast<double>(g_qpcFrequency.QuadPart);
    }

    const char* SessionStateName(XrSessionState s); // defined below

    template <size_t N>
    double TimingPercentile(const TimingRing<N>& ring, double percentile)
    {
        if (!ring.count)
            return 0.0;
        std::array<double, N> sorted{};
        for (size_t i = 0; i < ring.count; ++i)
            sorted[i] = ring.values[i];
        std::sort(sorted.begin(), sorted.begin() + ring.count);
        const size_t index = static_cast<size_t>(
            std::clamp(percentile, 0.0, 1.0) * static_cast<double>(ring.count - 1));
        return sorted[index];
    }

    // Tell the user VR failed without freezing the game (own thread) and let
    // the game keep running flat.
    void Fail(const char* what, XrResult r = XR_SUCCESS)
    {
        char msg[512];
        if (r != XR_SUCCESS)
            snprintf(msg, sizeof(msg), "%s (%s)", what, XrStr(r));
        else
            snprintf(msg, sizeof(msg), "%s", what);
        LOG("VR FAILED: %s", msg);
        g_authoredReticlePreparationReady.store(
            false, std::memory_order_release);
        g_state = State::Failed;

        static char popupText[640];
        snprintf(popupText, sizeof(popupText),
                 "Halo MCC VR mod could not start VR:\n\n%s\n\n"
                 "The game will keep running flat on the monitor.\n"
                 "Details are in HaloMCCVR.log next to the mod DLL.", msg);
        CreateThread(nullptr, 0,
                     [](LPVOID p) -> DWORD {
                         MessageBoxA(nullptr, (const char*)p, "Halo MCC VR mod", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                         return 0;
                     },
                     popupText, 0, nullptr);
    }

    bool IsSrgb(DXGI_FORMAT f)
    {
        return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }

    // Only used to report how much VRAM the intermediate pool is holding, so
    // the log states a real byte total instead of a guess. The four-byte
    // default covers every format this path has been observed to carry; a
    // wider format is reported low rather than wrongly precise.
    uint32_t DxgiBytesPerPixel(DXGI_FORMAT f)
    {
        if (f == DXGI_FORMAT_R32G8X24_TYPELESS || f == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
            f == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS) return 8;
        switch (VrBlitFormatFamily(f))
        {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return 8;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        default:
            return 4;
        }
    }

    DXGI_FORMAT UnormSibling(DXGI_FORMAT f)
    {
        if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_TYPELESS)
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        if (f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_TYPELESS)
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        return f;
    }

    XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
    {
        const XrVector3f u{q.x, q.y, q.z};
        auto cross = [](const XrVector3f& a, const XrVector3f& b) {
            return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        };
        XrVector3f c1 = cross(u, v);
        c1.x += q.w * v.x; c1.y += q.w * v.y; c1.z += q.w * v.z;
        const XrVector3f c2 = cross(u, c1);
        return {v.x + 2 * c2.x, v.y + 2 * c2.y, v.z + 2 * c2.z};
    }

    bool NormalizeTrackedPose(XrPosef& pose)
    {
        const float ql2 = pose.orientation.x * pose.orientation.x +
                          pose.orientation.y * pose.orientation.y +
                          pose.orientation.z * pose.orientation.z +
                          pose.orientation.w * pose.orientation.w;
        if (!std::isfinite(ql2) || ql2 < 1e-8f ||
            !std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
            !std::isfinite(pose.position.z))
            return false;
        const float inv = 1.0f / sqrtf(ql2);
        pose.orientation.x *= inv;
        pose.orientation.y *= inv;
        pose.orientation.z *= inv;
        pose.orientation.w *= inv;
        return true;
    }

    // One-pole previous-frame blend. `historyWeight` is deliberately expressed
    // as the UI percentage: 0 = raw current pose, .05 = 5% previous + 95% current.
    // Quaternion sign correction keeps equivalent q/-q samples from cancelling.
    XrPosef SmoothTrackedPose(const XrPosef& current, const XrPosef& previous,
                              float historyWeight)
    {
        const float h = std::clamp(historyWeight, 0.0f, 0.95f);
        const float n = 1.0f - h;
        float sign = current.orientation.x * previous.orientation.x +
                     current.orientation.y * previous.orientation.y +
                     current.orientation.z * previous.orientation.z +
                     current.orientation.w * previous.orientation.w < 0.0f ? -1.0f : 1.0f;
        XrPosef result{};
        result.orientation = {
            previous.orientation.x * h + current.orientation.x * n * sign,
            previous.orientation.y * h + current.orientation.y * n * sign,
            previous.orientation.z * h + current.orientation.z * n * sign,
            previous.orientation.w * h + current.orientation.w * n * sign};
        result.position = {
            previous.position.x * h + current.position.x * n,
            previous.position.y * h + current.position.y * n,
            previous.position.z * h + current.position.z * n};
        NormalizeTrackedPose(result);
        return result;
    }

    const char* SessionStateName(XrSessionState s)
    {
        switch (s)
        {
        case XR_SESSION_STATE_IDLE: return "idle";
        case XR_SESSION_STATE_READY: return "ready";
        case XR_SESSION_STATE_SYNCHRONIZED: return "synchronized";
        case XR_SESSION_STATE_VISIBLE: return "visible";
        case XR_SESSION_STATE_FOCUSED: return "focused";
        case XR_SESSION_STATE_STOPPING: return "stopping";
        case XR_SESSION_STATE_LOSS_PENDING: return "loss pending";
        case XR_SESSION_STATE_EXITING: return "exiting";
        default: return "unknown";
        }
    }

    // ------------------------------------------------------------- blitting

    bool EnsureBlitPipeline()
    {
        if (g_blitVs && g_blitPsLinearize && g_blitPsPass &&
            g_blitSampler && g_blitRasterizer && g_blitDepthOff)
            return true;
        auto release=[&](auto*& object)
        {
            if (object) object->Release();
            object=nullptr;
        };
        release(g_blitVs);
        release(g_blitPsLinearize);
        release(g_blitPsPass);
        release(g_blitSampler);
        release(g_blitRasterizer);
        release(g_blitDepthOff);

        const char* src = kVrBlitShader;
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        auto compile = [&](const char* entry, const char* target) -> ID3DBlob* {
            ID3DBlob* out = nullptr;
            if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, 0, 0, &out, &err)))
            {
                LOG("blit shader '%s' failed to compile: %s", entry, err ? (const char*)err->GetBufferPointer() : "?");
                if (err) { err->Release(); err = nullptr; }
                return nullptr;
            }
            return out;
        };

        blob = compile("vs_main", "vs_5_0");
        if (!blob) return false;
        HRESULT hr = g_device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_blitVs);
        blob->Release();
        if (FAILED(hr)) return false;

        blob = compile("ps_linearize", "ps_5_0");
        if (!blob) return false;
        hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_blitPsLinearize);
        blob->Release();
        if (FAILED(hr)) return false;

        blob = compile("ps_pass", "ps_5_0");
        if (!blob) return false;
        hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_blitPsPass);
        blob->Release();
        if (FAILED(hr)) return false;

        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(g_device->CreateSamplerState(&smp, &g_blitSampler))) return false;

        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        if (FAILED(g_device->CreateRasterizerState(&rs, &g_blitRasterizer))) return false;

        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = FALSE;
        if (FAILED(g_device->CreateDepthStencilState(&ds, &g_blitDepthOff))) return false;

        return true;
    }

    void ReleaseSourceViews()
    {
        // Drains every cached view and intermediate. A cached view holds a
        // strong reference on its source, so this is also what stops the table
        // pinning a resource the game is about to destroy - every caller below
        // runs on a resize or a detach, before the backbuffer goes away.
        while (void* view = g_srcSrvCache.TakeAny())
            static_cast<ID3D11ShaderResourceView*>(view)->Release();
        for (std::size_t i = 0; i < kIntermediatePoolCapacity; ++i)
        {
            if (g_intermediateSrvs[i])
            {
                g_intermediateSrvs[i]->Release();
                g_intermediateSrvs[i] = nullptr;
            }
            if (g_intermediateTex[i])
            {
                g_intermediateTex[i]->Release();
                g_intermediateTex[i] = nullptr;
            }
        }
        g_intermediatePool.Clear();
    }

    // Release a slot's resources so the pool can rebuild it into a new shape.
    void ReleaseIntermediateSlot(std::size_t slot)
    {
        if (slot >= kIntermediatePoolCapacity)
            return;
        if (g_intermediateSrvs[slot])
        {
            g_intermediateSrvs[slot]->Release();
            g_intermediateSrvs[slot] = nullptr;
        }
        if (g_intermediateTex[slot])
        {
            g_intermediateTex[slot]->Release();
            g_intermediateTex[slot] = nullptr;
        }
    }

    // Drop one source's cached view. The owner calls this before it destroys or
    // recreates a texture we may have sampled, so the table never keeps a dead
    // resource alive and never matches a recycled address.
    void ForgetSourceView(ID3D11Texture2D* source)
    {
        if (!source)
            return;
        if (void* view = g_srcSrvCache.Forget(source))
            static_cast<ID3D11ShaderResourceView*>(view)->Release();
    }

    // Once per two seconds, say what the upload path actually reused. This is
    // the line that settles the change from one session: `created` must stop
    // rising once a level is up, and `miss` must fall to 0 per window. If
    // either keeps climbing, the working set is bigger than the table and the
    // capacity is wrong - which is a number to read, not to argue about.
    // `intermediates` is the only part that holds real VRAM, so its live slot
    // count and byte total are printed rather than assumed.
    void ReportUploadResourceReuse()
    {
        static uint64_t lastMs = 0;
        static uint64_t lastCreatedViews = 0;
        static uint64_t lastCreatedIntermediates = 0;
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastMs < 2000)
            return;
        lastMs = nowMs;

        if (g_memoryAdapter)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
            if (SUCCEEDED(g_memoryAdapter->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
                LOG("VRAM: process local usage %.1f MiB / budget %.1f MiB; render %ux%u; DLSS %s, preset %s",
                    double(memory.CurrentUsage) / 1048576.0, double(memory.Budget) / 1048576.0,
                    g_gameBackbufferDesc.Width, g_gameBackbufferDesc.Height,
                    g_config.upscaler == dlss::kUpscalerDlss ? "on" : "off",
                    dlss::PresetName(g_config.dlss_preset));
        }

        uint64_t intermediateBytes = 0;
        std::size_t intermediateLive = 0;
        for (std::size_t i = 0; i < kIntermediatePoolCapacity; ++i)
        {
            if (!g_intermediateTex[i])
                continue;
            ++intermediateLive;
            D3D11_TEXTURE2D_DESC d{};
            g_intermediateTex[i]->GetDesc(&d);
            intermediateBytes += static_cast<uint64_t>(d.Width) * d.Height *
                                 DxgiBytesPerPixel(d.Format);
        }

        const ViewCacheStats& views = g_srcSrvCache.Stats();
        LOG("upload reuse: views %zu/%zu resident, %llu hit / %llu miss / %llu "
            "evicted, %llu created (+%llu this window); intermediates %zu/%zu "
            "live = %llu KB, %llu created (+%llu this window)",
            g_srcSrvCache.Size(), kSrcViewCacheCapacity,
            static_cast<unsigned long long>(views.hits),
            static_cast<unsigned long long>(views.misses),
            static_cast<unsigned long long>(views.evictions),
            static_cast<unsigned long long>(g_srcViewCreated),
            static_cast<unsigned long long>(g_srcViewCreated - lastCreatedViews),
            intermediateLive, kIntermediatePoolCapacity,
            static_cast<unsigned long long>(intermediateBytes / 1024),
            static_cast<unsigned long long>(g_intermediateCreated),
            static_cast<unsigned long long>(g_intermediateCreated -
                                            lastCreatedIntermediates));
        lastCreatedViews = g_srcViewCreated;
        lastCreatedIntermediates = g_intermediateCreated;
        g_srcSrvCache.ResetStats();
    }

    // Acquire an SRV we can sample `src` from. Backbuffers usually can't be used
    // as shader input directly, so we may need an SRV-capable intermediate copy.
    // Returns a borrowed SRV, owned by the caches above; never release it here.
    ID3D11ShaderResourceView* AcquireSrcSrv(ID3D11Texture2D* src,
                                            const D3D11_TEXTURE2D_DESC& srcDesc)
    {
        // A null source is rejected up front. The cache treats a null key as
        // "do not store" and hands the view straight back, which the caller
        // below would then release and return - a use-after-free. Refusing here
        // is cheaper than making every caller prove it.
        if (!src || !g_device)
            return nullptr;
        if ((srcDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && srcDesc.SampleDesc.Count <= 1)
        {
            // A hit is the whole point: both eyes, the menu, the screen quad
            // and the reticle each keep their own view, so a settled frame
            // creates nothing at all here.
            const ViewCacheLookup cached = g_srcSrvCache.Find(src);
            if (cached.hit)
                return static_cast<ID3D11ShaderResourceView*>(cached.view);

            ID3D11ShaderResourceView* fresh = nullptr;
            // E-H2-27: a TYPELESS target has no default view; name the
            // concrete format explicitly or the source can never be sampled.
            D3D11_SHADER_RESOURCE_VIEW_DESC typedDesc{};
            const bool typeless =
                Halo2FormatIsTypeless(static_cast<uint32_t>(srcDesc.Format));
            if (typeless || IsSrgb(srcDesc.Format))
            {
                typedDesc.Format = typeless ? static_cast<DXGI_FORMAT>(
                    Halo2ConcreteFormat(static_cast<uint32_t>(srcDesc.Format))) : srcDesc.Format;
                typedDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                typedDesc.Texture2D.MostDetailedMip = 0;
                typedDesc.Texture2D.MipLevels = 1;
            }
            if (SUCCEEDED(g_device->CreateShaderResourceView(
                    src, (typeless || IsSrgb(srcDesc.Format)) ? &typedDesc : nullptr, &fresh)) &&
                fresh)
            {
                ++g_srcViewCreated;
                if (void* displaced = g_srcSrvCache.Insert(src, fresh))
                    static_cast<ID3D11ShaderResourceView*>(displaced)->Release();
                return fresh;
            }
            // Creation failed; fall through to the intermediate copy rather
            // than caching a null and reporting a hit that hands back nothing.
        }

        IntermediateShape shape{};
        shape.width = srcDesc.Width;
        shape.height = srcDesc.Height;
        shape.format = static_cast<uint32_t>(srcDesc.Format);
        const IntermediatePoolSlot slot = g_intermediatePool.Acquire(shape);
        if (!slot.valid)
            return nullptr;
        if (slot.needsCreate)
        {
            // The slot may still hold a resource of the evicted shape.
            ReleaseIntermediateSlot(slot.index);
            D3D11_TEXTURE2D_DESC d{};
            d.Width = srcDesc.Width;
            d.Height = srcDesc.Height;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = srcDesc.Format;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(g_device->CreateTexture2D(&d, nullptr,
                                                 &g_intermediateTex[slot.index])) ||
                FAILED(g_device->CreateShaderResourceView(
                    g_intermediateTex[slot.index], nullptr,
                    &g_intermediateSrvs[slot.index])))
            {
                LOG("blit: intermediate texture creation failed (fmt %d)", (int)srcDesc.Format);
                ReleaseIntermediateSlot(slot.index);
                g_intermediatePool.Abandon(slot.index);
                return nullptr;
            }
            ++g_intermediateCreated;
            g_intermediatePool.Commit(slot.index, shape);
        }
        if (srcDesc.SampleDesc.Count > 1)
            g_context->ResolveSubresource(g_intermediateTex[slot.index], 0, src, 0,
                                          srcDesc.Format);
        else
            g_context->CopyResource(g_intermediateTex[slot.index], src);
        return g_intermediateSrvs[slot.index];
    }

    #include "vr_blit.inl"

    #include "haloce_desktop_mirror.inl"

    // Image-quality pipeline. Compiles the resolve/AA/sharpen pixel shaders and
    // the params constant buffer once; reuses the blit VS/sampler/rasterizer/
    // depth. All passes operate in the display's perceptual (sRGB-encoded) space
    // via UNORM intermediates, decoding to linear only at the final write to the
    // sRGB XR target. The sharp bicubic resolve deliberately uses the LINEAR
    // sampler (its 9-tap form combines bilinear fetches).
    bool EnsureIqPipeline()
    {
        if (g_iqResolveSharp && g_iqResolveLinear && g_iqFxaa && g_iqRcas &&
            g_iqCb)
            return true;
        if (!EnsureBlitPipeline())
            return false;
        auto release=[&](auto*& o){ if (o) o->Release(); o=nullptr; };
        release(g_iqResolveSharp);
        release(g_iqResolveLinear);
        release(g_iqFxaa);
        release(g_iqRcas);
        release(g_iqDlssRcas);
        release(g_iqCb);

        static const char* src = R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
cbuffer IqParams : register(b0)
{
    float2 srcSize;      // dims of the texture being sampled this pass
    float2 dstSize;      // dims of the render target this pass
    float  sharpness;    // 0..1 UI range; RCAS correction is 2x overdriven
    float  srcIsSrgb;    // 1 = sampling srcTex already returns linear
    float  outPerceptual;// 1 = write perceptual (to UNORM); 0 = write linear (to sRGB RTV)
    float  aaStrength;   // 0 = FXAA, 1 = FXAA Strong
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float3 toLin(float3 c){ return float3(
    c.r <= 0.04045 ? c.r/12.92 : pow((c.r+0.055)/1.055, 2.4),
    c.g <= 0.04045 ? c.g/12.92 : pow((c.g+0.055)/1.055, 2.4),
    c.b <= 0.04045 ? c.b/12.92 : pow((c.b+0.055)/1.055, 2.4)); }
float3 toSrgb(float3 c){ c=saturate(c); return float3(
    c.r <= 0.0031308 ? c.r*12.92 : 1.055*pow(c.r,1.0/2.4)-0.055,
    c.g <= 0.0031308 ? c.g*12.92 : 1.055*pow(c.g,1.0/2.4)-0.055,
    c.b <= 0.0031308 ? c.b*12.92 : 1.055*pow(c.b,1.0/2.4)-0.055); }
float3 srcLinear(float4 s){ return srcIsSrgb > 0.5 ? s.rgb : toLin(s.rgb); }
float3 encodeOut(float3 lin){ return outPerceptual > 0.5 ? toSrgb(lin) : lin; }
float3 finishPerceptual(float3 c){ return outPerceptual > 0.5 ? c : toLin(c); }
float lumaP(float3 c){ return dot(c, float3(0.299, 0.587, 0.114)); }

// Keys bicubic with a=-0.75. Catmull-Rom (a=-0.5) was mathematically valid but
// too close to linear at the modest horizontal scale used by this headset.
// The stronger negative lobe is still bounded, while making the F1 A/B visible.
float4 sampleSharpBicubic(float2 uv)
{
    float2 texSize = srcSize;
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texPos1;
    const float a = -0.75;
    float2 w0 = a * f * (1.0 - f) * (1.0 - f);
    float2 w1 = 1.0 - (a + 3.0) * f * f + (a + 2.0) * f * f * f;
    float2 g = 1.0 - f;
    float2 w2 = 1.0 - (a + 3.0) * g * g + (a + 2.0) * g * g * g;
    float2 w3 = a * f * f * (1.0 - f);
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;
    float2 p0 = (texPos1 - 1.0) / texSize;
    float2 p3 = (texPos1 + 2.0) / texSize;
    float2 p12 = (texPos1 + offset12) / texSize;
    float4 r = float4(0,0,0,0);
    r += srcTex.SampleLevel(smp, float2(p0.x,  p0.y ), 0) * (w0.x  * w0.y );
    r += srcTex.SampleLevel(smp, float2(p12.x, p0.y ), 0) * (w12.x * w0.y );
    r += srcTex.SampleLevel(smp, float2(p3.x,  p0.y ), 0) * (w3.x  * w0.y );
    r += srcTex.SampleLevel(smp, float2(p0.x,  p12.y), 0) * (w0.x  * w12.y);
    r += srcTex.SampleLevel(smp, float2(p12.x, p12.y), 0) * (w12.x * w12.y);
    r += srcTex.SampleLevel(smp, float2(p3.x,  p12.y), 0) * (w3.x  * w12.y);
    r += srcTex.SampleLevel(smp, float2(p0.x,  p3.y ), 0) * (w0.x  * w3.y );
    r += srcTex.SampleLevel(smp, float2(p12.x, p3.y ), 0) * (w12.x * w3.y );
    r += srcTex.SampleLevel(smp, float2(p3.x,  p3.y ), 0) * (w3.x  * w3.y );
    return r;
}

float4 ps_resolve_sharp(VSOut i) : SV_Target
{
    float4 s = sampleSharpBicubic(i.uv);
    return float4(encodeOut(srcLinear(s)), s.a);
}
float4 ps_resolve_linear(VSOut i) : SV_Target
{
    float4 s = srcTex.SampleLevel(smp, i.uv, 0);
    return float4(encodeOut(srcLinear(s)), s.a);
}

// FXAA 3.11 console-quality edge smoothing, in perceptual space.
float4 ps_fxaa(VSOut i) : SV_Target
{
    float2 rcp = 1.0 / srcSize;
    float2 uv = i.uv;
    float3 m  = srcTex.SampleLevel(smp, uv, 0).rgb;
    float3 nw = srcTex.SampleLevel(smp, uv + float2(-rcp.x,-rcp.y), 0).rgb;
    float3 ne = srcTex.SampleLevel(smp, uv + float2( rcp.x,-rcp.y), 0).rgb;
    float3 sw = srcTex.SampleLevel(smp, uv + float2(-rcp.x, rcp.y), 0).rgb;
    float3 se = srcTex.SampleLevel(smp, uv + float2( rcp.x, rcp.y), 0).rgb;
    float lM=lumaP(m), lNW=lumaP(nw), lNE=lumaP(ne), lSW=lumaP(sw), lSE=lumaP(se);
    float lMin = min(lM, min(min(lNW,lNE), min(lSW,lSE)));
    float lMax = max(lM, max(max(lNW,lNE), max(lSW,lSE)));
    float edgeThresholdMin = lerp(0.0312, 0.0156, saturate(aaStrength));
    float edgeThreshold = lerp(0.125, 0.063, saturate(aaStrength));
    if ((lMax - lMin) < max(edgeThresholdMin, lMax * edgeThreshold))
        return float4(finishPerceptual(m), 1);
    float2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));
    float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.03125), 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    float span = lerp(8.0, 12.0, saturate(aaStrength));
    dir = clamp(dir * rcpDirMin, -span, span) * rcp;
    float3 rgbA = 0.5 * (
        srcTex.SampleLevel(smp, uv + dir * (1.0/3.0 - 0.5), 0).rgb +
        srcTex.SampleLevel(smp, uv + dir * (2.0/3.0 - 0.5), 0).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        srcTex.SampleLevel(smp, uv + dir * -0.5, 0).rgb +
        srcTex.SampleLevel(smp, uv + dir *  0.5, 0).rgb);
    float lB = lumaP(rgbB);
    float3 result = (lB < lMin || lB > lMax) ? rgbA : rgbB;
    return float4(finishPerceptual(result), 1);
}

float3 loadClamped(int2 p)
{
    p = clamp(p, int2(0, 0), int2(srcSize) - 1);
    return srcTex.Load(int3(p, 0)).rgb;
}

// AMD FidelityFX FSR1 RCAS 32-bit limiter/resolve, followed by a 2x correction
// overdrive. This retains RCAS's safe lobe/denominator and the same five loads;
// the user can pull the intentionally aggressive top end back with the slider.
float4 ps_rcas(VSOut i) : SV_Target
{
    int2 p = int2(i.pos.xy);
    float3 b = loadClamped(p + int2( 0,-1));
    float3 d = loadClamped(p + int2(-1, 0));
    float3 e = loadClamped(p);
    float3 f = loadClamped(p + int2( 1, 0));
    float3 h = loadClamped(p + int2( 0, 1));
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));

    // Solve the negative lobe that cannot clip either side of [0,1], then
    // apply RCAS's documented -0.1875 natural-result limit.
    float3 hitMin = min(mn4, e) / max(4.0 * mx4, 1e-5);
    float3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, -1e-5);
    float3 lobes = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobes.r, max(lobes.g, lobes.b)), 0.0));
    float scaledLobe = lobe * saturate(sharpness);
    float3 rcas = (scaledLobe * (b + d + f + h) + e) /
                  (4.0 * scaledLobe + 1.0);
    float3 res = saturate(e + 2.0 * (rcas - e));
    return float4(finishPerceptual(res), 1);
}
// Fuse the linear resolve and RCAS for perceptual DLSS input. Each tap is
// the resolved UNORM8 pixel, including quantisation (neighbor UV arithmetic
// can differ by a hardware interpolation step from the separate pass).
// Destination-grid offsets preserve the sharpening radius.
float3 resolvedClamped(float2 uv)
{
    uv=clamp(uv,0.5/dstSize,1.0-0.5/dstSize);
    float3 c=srcTex.SampleLevel(smp,uv,0).rgb;
    return round(saturate(c)*255.0)/255.0;
}
float4 ps_dlss_resolve_rcas(VSOut i) : SV_Target
{
    float2 p=i.uv,step=1.0/dstSize;
    float3 b=resolvedClamped(p+float2(0,-step.y)),d=resolvedClamped(p+float2(-step.x,0));
    float3 e=resolvedClamped(p),f=resolvedClamped(p+float2(step.x,0)),h=resolvedClamped(p+float2(0,step.y));
    float3 mn4=min(min(b,d),min(f,h)),mx4=max(max(b,d),max(f,h));
    float3 hitMin=min(mn4,e)/max(4.0*mx4,1e-5);
    float3 hitMax=(1.0-max(mx4,e))/min(4.0*mn4-4.0,-1e-5);
    float3 lobes=max(-hitMin,hitMax);
    float lobe=max(-0.1875,min(max(lobes.r,max(lobes.g,lobes.b)),0.0));
    float scaledLobe=lobe*saturate(sharpness);
    float3 rcas=(scaledLobe*(b+d+f+h)+e)/(4.0*scaledLobe+1.0);
    return float4(finishPerceptual(saturate(e+2.0*(rcas-e))),1);
}
)";
        ID3DBlob* err = nullptr;
        auto compile = [&](const char* entry) -> ID3DBlob* {
            ID3DBlob* out = nullptr;
            const HRESULT hr = D3DCompile(
                src, strlen(src), nullptr, nullptr, nullptr, entry, "ps_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
            if (FAILED(hr))
            {
                LOG("IQ shader '%s' failed to compile: %s", entry,
                    err ? (const char*)err->GetBufferPointer() : "?");
                if (err) { err->Release(); err = nullptr; }
                return nullptr;
            }
            if (err) { err->Release(); err = nullptr; }
            return out;
        };
        struct Target { const char* entry; ID3D11PixelShader** out; };
        const Target targets[] = {
            {"ps_resolve_sharp",   &g_iqResolveSharp},
            {"ps_resolve_linear",  &g_iqResolveLinear},
            {"ps_fxaa",            &g_iqFxaa},
            {"ps_rcas",            &g_iqRcas},
        };
        for (const auto& t : targets)
        {
            ID3DBlob* blob = compile(t.entry);
            if (!blob) return false;
            HRESULT hr = g_device->CreatePixelShader(blob->GetBufferPointer(),
                                                     blob->GetBufferSize(), nullptr, t.out);
            blob->Release();
            if (FAILED(hr)) return false;
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(IqParams);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_device->CreateBuffer(&cb, nullptr, &g_iqCb)))
            return false;
        // Optional optimization: compilation/allocation failure retains the
        // already working ordinary resolve, independently of VR ownership.
        if (ID3DBlob* blob=dlss::kEnableDlssFullOutputResolve ? compile("ps_dlss_resolve_rcas") : nullptr)
        {
            const HRESULT hr=g_device->CreatePixelShader(blob->GetBufferPointer(),
                blob->GetBufferSize(),nullptr,&g_iqDlssRcas);
            blob->Release();
            if (FAILED(hr)) LOG("DLSS: fused output shader unavailable (0x%08X); ordinary resolve stays",unsigned(hr));
        }
        return true;
    }

    void ReleaseSmaaPipeline()
    {
        auto release = [](auto*& o) { if (o) o->Release(); o = nullptr; };
        release(g_smaaEdgesVs);
        release(g_smaaWeightsVs);
        release(g_smaaNeighborhoodVs);
        release(g_smaaEdgesPs);
        release(g_smaaWeightsPs);
        release(g_smaaNeighborhoodPs);
        release(g_smaaPointSampler);
        release(g_smaaAreaSrv);
        release(g_smaaAreaTex);
        release(g_smaaSearchSrv);
        release(g_smaaSearchTex);
    }

    // Genuine SMAA 1x: official three-stage edge -> blend-weight -> neighborhood
    // pipeline with the highest-coverage 1x preset and official lookup tables.
    // Lazy creation keeps initial Off/FXAA startup unchanged; deselected modes
    // run no SMAA passes and retain no SMAA-sized eye target.
    bool EnsureSmaaPipeline()
    {
        if (g_smaaEdgesVs && g_smaaWeightsVs && g_smaaNeighborhoodVs &&
            g_smaaEdgesPs && g_smaaWeightsPs && g_smaaNeighborhoodPs &&
            g_smaaPointSampler && g_smaaAreaSrv && g_smaaSearchSrv)
            return true;
        if (g_smaaInitFailed)
            return false;

        ReleaseSmaaPipeline();
        auto fail = [&]() {
            ReleaseSmaaPipeline();
            g_smaaInitFailed = true;
            return false;
        };

        HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);
        auto resourceBytes = [&](int id, const char* name,
                                 const void*& bytes, DWORD& byteCount) {
            HRSRC resource = FindResourceW(
                self, MAKEINTRESOURCEW(id), RT_RCDATA);
            HGLOBAL handle = resource ? LoadResource(self, resource) : nullptr;
            bytes = handle ? LockResource(handle) : nullptr;
            byteCount = resource ? SizeofResource(self, resource) : 0;
            if (!bytes || !byteCount)
                LOG("IQ ERROR: embedded SMAA shader '%s' unavailable (winerr=%lu)",
                    name, GetLastError());
            return bytes && byteCount;
        };

        auto createVs = [&](int id, const char* name, ID3D11VertexShader** out) {
            const void* bytes = nullptr;
            DWORD byteCount = 0;
            if (!resourceBytes(id, name, bytes, byteCount))
                return false;
            const HRESULT hr = g_device->CreateVertexShader(
                bytes, byteCount, nullptr, out);
            if (FAILED(hr))
                LOG("IQ ERROR: SMAA vertex shader '%s' creation failed (0x%08X)",
                    name, (unsigned)hr);
            return SUCCEEDED(hr);
        };
        auto createPs = [&](int id, const char* name, ID3D11PixelShader** out) {
            const void* bytes = nullptr;
            DWORD byteCount = 0;
            if (!resourceBytes(id, name, bytes, byteCount))
                return false;
            const HRESULT hr = g_device->CreatePixelShader(
                bytes, byteCount, nullptr, out);
            if (FAILED(hr))
                LOG("IQ ERROR: SMAA pixel shader '%s' creation failed (0x%08X)",
                    name, (unsigned)hr);
            return SUCCEEDED(hr);
        };

        if (!createVs(IDR_SMAA_EDGES_VS, "edges-vs", &g_smaaEdgesVs) ||
            !createVs(IDR_SMAA_WEIGHTS_VS, "weights-vs", &g_smaaWeightsVs) ||
            !createVs(IDR_SMAA_NEIGHBORHOOD_VS, "neighborhood-vs", &g_smaaNeighborhoodVs) ||
            !createPs(IDR_SMAA_EDGES_PS, "edges-ps", &g_smaaEdgesPs) ||
            !createPs(IDR_SMAA_WEIGHTS_PS, "weights-ps", &g_smaaWeightsPs) ||
            !createPs(IDR_SMAA_NEIGHBORHOOD_PS, "neighborhood-ps", &g_smaaNeighborhoodPs))
            return fail();

        D3D11_SAMPLER_DESC point{};
        point.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        point.AddressU = point.AddressV = point.AddressW =
            D3D11_TEXTURE_ADDRESS_CLAMP;
        HRESULT hr = g_device->CreateSamplerState(&point, &g_smaaPointSampler);
        if (FAILED(hr))
        {
            LOG("IQ ERROR: SMAA point sampler creation failed (0x%08X)",
                (unsigned)hr);
            return fail();
        }

        auto createLut = [&](const unsigned char* bytes, UINT width, UINT height,
                             UINT pitch, DXGI_FORMAT format, const char* name,
                             ID3D11Texture2D** texture,
                             ID3D11ShaderResourceView** srv) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = width;
            d.Height = height;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = format;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_IMMUTABLE;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{};
            init.pSysMem = bytes;
            init.SysMemPitch = pitch;
            HRESULT lutHr = g_device->CreateTexture2D(&d, &init, texture);
            if (SUCCEEDED(lutHr))
                lutHr = g_device->CreateShaderResourceView(*texture, nullptr, srv);
            if (FAILED(lutHr))
                LOG("IQ ERROR: SMAA %s lookup texture creation failed (0x%08X)",
                    name, (unsigned)lutHr);
            return SUCCEEDED(lutHr);
        };

        static_assert(sizeof(areaTexBytes) == AREATEX_SIZE);
        static_assert(sizeof(searchTexBytes) == SEARCHTEX_SIZE);
        if (!createLut(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT,
                       AREATEX_PITCH, DXGI_FORMAT_R8G8_UNORM, "area",
                       &g_smaaAreaTex, &g_smaaAreaSrv) ||
            !createLut(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                       SEARCHTEX_PITCH, DXGI_FORMAT_R8_UNORM, "search",
                       &g_smaaSearchTex, &g_smaaSearchSrv))
            return fail();

        LOG("IQ: SMAA 1x pipeline ready -- threshold=0.05 search=32 diag=16 "
            "color-edge upstream=71c806a");
        return true;
    }

    void ReleaseIqChain()
    {
        for (auto*& s : g_iqChainSrv) { if (s) s->Release(); s = nullptr; }
        for (auto*& r : g_iqChainRtv) { if (r) r->Release(); r = nullptr; }
        for (auto*& t : g_iqChain)    { if (t) t->Release(); t = nullptr; }
        g_iqChainDesc = {};
    }

    void ReleaseIqChainSlot(int i)
    {
        if (g_iqChainSrv[i]) { g_iqChainSrv[i]->Release(); g_iqChainSrv[i] = nullptr; }
        if (g_iqChainRtv[i]) { g_iqChainRtv[i]->Release(); g_iqChainRtv[i] = nullptr; }
        if (g_iqChain[i]) { g_iqChain[i]->Release(); g_iqChain[i] = nullptr; }
    }

    // Allocate only the intermediates this exact pass graph needs. The old
    // fixed two-target minimum kept another full XR-eye texture alive for a
    // simple resolve+RCAS path. This depends on pass liveness, not on input
    // resolution, DLSS model, or the dormant stereo-batch experiment.
    bool EnsureIqChain(uint32_t w, uint32_t h, int targetCount)
    {
        const DXGI_FORMAT format = UnormSibling((DXGI_FORMAT)g_xrFormat);
        if (g_iqChainDesc.Width != w || g_iqChainDesc.Height != h ||
            g_iqChainDesc.Format != format)
            ReleaseIqChain();
        if (targetCount < 1 || targetCount > 3)
            return false;
        for (int i = targetCount; i < 3; ++i)
            ReleaseIqChainSlot(i);

        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        for (int i = 0; i < targetCount; ++i)
        {
            if (g_iqChain[i] && g_iqChainRtv[i] && g_iqChainSrv[i])
                continue;
            ReleaseIqChainSlot(i);
            if (FAILED(g_device->CreateTexture2D(&d, nullptr, &g_iqChain[i])) ||
                FAILED(g_device->CreateRenderTargetView(g_iqChain[i], nullptr, &g_iqChainRtv[i])) ||
                FAILED(g_device->CreateShaderResourceView(g_iqChain[i], nullptr, &g_iqChainSrv[i])))
            {
                LOG("IQ ERROR: intermediate chain slot %d creation failed (%ux%u)",
                    i, w, h);
                ReleaseIqChain();
                return false;
            }
        }
        g_iqChainDesc = d;
        return true;
    }

    // --- GPU timing for the eye publish ------------------------------------
    //
    // Until now NOTHING in this repo measured GPU time. Every number in the log
    // - renderWindow, xrEndFrame, fps - is CPU wall clock, so the cost of the
    // work we actually ask the GPU to do had never once been observed. That is
    // why three reasoned performance passes in a row missed and two made it
    // worse (AGENTS.md). This prices the eye publish directly: the resolve pass
    // and the post passes (FXAA/SMAA/RCAS) are timed separately, per eye, so
    // "what does the second pass cost?" becomes a number instead of an
    // argument.
    //
    // It never blocks. Results are read back several frames later with
    // DONOTFLUSH and simply skipped while the GPU is still busy, so a query
    // that is not ready costs a load and nothing else. A disjoint frame (the
    // GPU changed clocks mid-measurement) is discarded rather than reported.
    constexpr std::size_t kIqTimerDepth = 4;      // frames in flight
    constexpr std::size_t kIqTimerStamps = 6;     // 3 per eye, 2 eyes

    struct IqTimerSlot
    {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* stamps[kIqTimerStamps]{};
        ID3D11Query* dlssWait[4]{};
        uint32_t waitMask = 0;
        uint32_t written = 0;
        bool pending = false;
    };

    IqTimerSlot g_iqTimer[kIqTimerDepth];
    GpuEyeTiming g_eyeGpuTiming;
    std::size_t g_iqTimerCursor = 0;   // which slot this frame is recording into
    uint32_t g_iqTimerStampIndex = 0;  // stamps written so far this frame
    bool g_iqTimerArmed = false;
    bool g_iqTimerUsable = false;
    bool g_iqTimerTried = false;
    // Accumulated over the reporting window, in microseconds.
    double g_iqResolveUs = 0.0;
    double g_iqPostUs = 0.0;
    double g_iqDlssWaitUs = 0.0;
    uint32_t g_iqDlssWaitEyes = 0;
    uint32_t g_iqTimedFrames = 0;
    uint32_t g_iqDisjointFrames = 0;

    bool EnsureIqTimer()
    {
        if (g_iqTimerTried)
            return g_iqTimerUsable;
        g_iqTimerTried = true;
        if (!g_device)
            return false;
        D3D11_QUERY_DESC disjointDesc{};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC stampDesc{};
        stampDesc.Query = D3D11_QUERY_TIMESTAMP;
        for (auto& slot : g_iqTimer)
        {
            if (FAILED(g_device->CreateQuery(&disjointDesc, &slot.disjoint)))
                return false;
            for (auto*& stamp : slot.stamps)
            {
                if (FAILED(g_device->CreateQuery(&stampDesc, &stamp)))
                    return false;
            }
            for (auto*& stamp : slot.dlssWait)
                if (FAILED(g_device->CreateQuery(&stampDesc, &stamp))) return false;
        }
        g_iqTimerUsable = true;
        g_eyeGpuTiming.Init(g_device);
        LOG("IQ TIMING: GPU timestamps armed; the eye publish now reports real "
            "GPU milliseconds, split resolve vs post");
        return true;
    }

    void ReleaseIqTimer()
    {
        g_eyeGpuTiming.Release();
        for (auto& slot : g_iqTimer)
        {
            if (slot.disjoint) { slot.disjoint->Release(); slot.disjoint = nullptr; }
            for (auto*& stamp : slot.stamps)
            {
                if (stamp) { stamp->Release(); stamp = nullptr; }
            }
            slot.written = 0;
            for (auto*& stamp : slot.dlssWait)
            { if (stamp) stamp->Release(); stamp = nullptr; }
            slot.waitMask = 0;
            slot.pending = false;
        }
        g_iqTimerArmed = false;
        g_iqTimerUsable = false;
        g_iqTimerTried = false;
    }

    // Harvest whichever in-flight slots the GPU has finished with. Never waits.
    void CollectIqTimings()
    {
        if (!g_iqTimerUsable || !g_context)
            return;
        for (auto& slot : g_iqTimer)
        {
            if (!slot.pending || slot.written < 2)
                continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            if (g_context->GetData(slot.disjoint, &dj, sizeof(dj),
                                   D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                continue;  // still busy; try again next frame

            bool complete = true;
            uint64_t values[kIqTimerStamps]{};
            for (uint32_t i = 0; i < slot.written && complete; ++i)
            {
                complete = g_context->GetData(slot.stamps[i], &values[i],
                                              sizeof(values[i]),
                                              D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            }
            if (!complete)
                continue;

            uint64_t waits[4]{};
            for (uint32_t i = 0; i < 4; ++i)
                if (slot.waitMask & (1u << i))
                    complete &= g_context->GetData(slot.dlssWait[i], &waits[i], sizeof(waits[i]),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (!complete) continue;

            slot.pending = false;
            if (dj.Disjoint || dj.Frequency == 0)
            {
                ++g_iqDisjointFrames;
                continue;
            }
            const double toUs = 1e6 / static_cast<double>(dj.Frequency);
            for (uint32_t i = 0; i < 4; i += 2)
                if ((slot.waitMask & (3u << i)) == (3u << i) && waits[i + 1] >= waits[i])
                {
                    g_iqDlssWaitUs += (waits[i + 1] - waits[i]) * toUs;
                    ++g_iqDlssWaitEyes;
                }
            // Stamps arrive in threes: start, after the resolve, after the
            // post passes. A partial triple (an eye that failed early) is
            // skipped rather than mis-attributed.
            for (uint32_t base = 0; base + 3 <= slot.written; base += 3)
            {
                g_iqResolveUs += (values[base + 1] - values[base]) * toUs;
                g_iqPostUs += (values[base + 2] - values[base + 1]) * toUs;
            }
            ++g_iqTimedFrames;
        }
    }

    void IqTimerBeginFrame()
    {
        g_iqTimerArmed = false;
        g_iqTimerStampIndex = 0;
        if (!EnsureIqTimer() || !g_context)
            return;
        CollectIqTimings();
        // Never overwrite a slot the GPU has not finished reporting.
        IqTimerSlot& slot = g_iqTimer[g_iqTimerCursor];
        if (slot.pending)
            return;
        slot.written = 0;
        slot.waitMask = 0;
        g_context->Begin(slot.disjoint);
        g_iqTimerArmed = true;
    }

    // One timestamp inside the publish. Out of stamps is not an error: the
    // theatre path runs extra blits and simply goes untimed.
    void IqTimerStamp()
    {
        if (!g_iqTimerArmed || g_iqTimerStampIndex >= kIqTimerStamps)
            return;
        IqTimerSlot& slot = g_iqTimer[g_iqTimerCursor];
        g_context->End(slot.stamps[g_iqTimerStampIndex]);
        ++g_iqTimerStampIndex;
        slot.written = g_iqTimerStampIndex;
    }

    void IqTimerDlssWait(int eye, bool end)
    {
        if (!g_iqTimerArmed || eye < 0 || eye > 1) return;
        auto& slot = g_iqTimer[g_iqTimerCursor];
        const unsigned index = eye * 2 + (end ? 1 : 0);
        if (slot.waitMask & (1u << index)) return;
        g_context->End(slot.dlssWait[index]);
        slot.waitMask |= 1u << index;
    }

    // The eye publish, in real GPU time, on the same two-second window as the
    // other steady reports. `resolve` is the bicubic/linear expand into headset
    // resolution; `post` is FXAA/SMAA/RCAS layered on top. Both are summed over
    // BOTH eyes, so together they are what this mod costs the GPU each frame
    // beyond the game's own rendering.
    //
    // What to do with the number: `post` IS the price of the second pass. It is
    // paid whenever sharpness > 0, which is every configuration ever shipped or
    // logged, and the sharpness slider in the F1 menu turns it off live. So one
    // sitting and one slider move settles what removing that pass is worth -
    // without guessing, and without another build.
    void ReportIqGpuTiming()
    {
        static uint64_t lastMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastMs < 2000)
            return;
        lastMs = nowMs;
        if (!g_iqTimerUsable || !g_iqTimedFrames)
            return;
        const double frames = static_cast<double>(g_iqTimedFrames);
        double dlssCpuUs = 0.0;
        uint32_t dlssEvaluations = 0;
        Dlss_TakeCpuTime(dlssCpuUs, dlssEvaluations);
        const auto raster = g_eyeGpuTiming.Collect(g_context,
            g_config.upscaler == dlss::kUpscalerDlss,
            g_gameBackbufferDesc.Width, g_gameBackbufferDesc.Height);
        if (raster.eyes)
            LOG("DLSS BUDGET: %s render %ux%u: graphics raster %.3f ms/eye, "
                "input preparation/submission %.3f ms/eye, %u sampled eyes; "
                "GPU intervals include scheduling contention; compare the same scene",
                raster.dlss ? "ON" : "OFF", raster.width, raster.height,
                raster.rasterUs / raster.eyes / 1000.0,
                raster.prepareUs / raster.eyes / 1000.0, raster.eyes);
        DlssGpuTiming dlssGpu{};
        Dlss_TakeGpuTime(dlssGpu);
        if (g_iqDlssWaitEyes)
            LOG("DLSS BUDGET: graphics residual wait for compute/flow %.3f ms/eye over %u eyes "
                "(already overlaps other work; do not add the full compute cost again)",
                g_iqDlssWaitUs / g_iqDlssWaitEyes / 1000.0, g_iqDlssWaitEyes);
        g_iqDlssWaitUs = 0; g_iqDlssWaitEyes = 0;
        if (dlssGpu.evaluations)
            LOG("DLSS BUDGET: compute merge %.3f ms/eye, transformer/evaluate %.3f ms/eye, "
                "%u evaluations (GPU intervals, excludes optical-flow wait)",
                dlssGpu.mergeMicroseconds / dlssGpu.evaluations / 1000.0,
                dlssGpu.evaluateMicroseconds / dlssGpu.evaluations / 1000.0,
                dlssGpu.evaluations);
        LOG("IQ GPU: eye publish %.3f ms/frame (resolve %.3f + post %.3f, both "
            "eyes) over %u timed frames%s; DLSS CPU %.3f ms/frame inside %u evaluations",
            (g_iqResolveUs + g_iqPostUs) / frames / 1000.0,
            g_iqResolveUs / frames / 1000.0,
            g_iqPostUs / frames / 1000.0,
            g_iqTimedFrames,
            g_iqDisjointFrames
                ? " (some frames discarded: the GPU changed clocks)" : "",
            dlssEvaluations ? dlssCpuUs / frames / 1000.0 : 0.0,
            dlssEvaluations);
        g_iqResolveUs = 0.0;
        g_iqPostUs = 0.0;
        g_iqTimedFrames = 0;
        g_iqDisjointFrames = 0;
    }

    void IqTimerEndFrame()
    {
        if (!g_iqTimerArmed)
            return;
        IqTimerSlot& slot = g_iqTimer[g_iqTimerCursor];
        g_context->End(slot.disjoint);
        // A frame that produced no complete triple is not worth collecting.
        slot.pending = slot.written >= 3;
        g_iqTimerArmed = false;
        g_iqTimerCursor = (g_iqTimerCursor + 1) % kIqTimerDepth;
    }

    // Expand one captured eye (src, render resolution) into the XR eye image
    // (dst, headset resolution) with the user's chosen resolve filter, optional
    // anti-aliasing, and optional sharpening. Universal to every title. This
    // function always owns the eye resolve; missing prerequisites fail loudly.
    bool BlitImageQuality(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc,
                          ID3D11Texture2D* dst, uint32_t dstW, uint32_t dstH,
                          ID3D11RenderTargetView* dstRtv, bool reconstructed = false)
    {
        reconstructed = reconstructed && dlss::kEnableDlssFullOutputResolve;
        const bool xrSrgb = IsSrgb((DXGI_FORMAT)g_xrFormat);
        const bool finalPerceptual = !xrSrgb;
        const bool wantSharp = g_config.upscale_filter == 1;     // sharp bicubic resolve vs linear
        // Preserve the user's explicit post-AA setting. The combined DLSS
        // resolve is selected only when no additional AA pass was requested.
        const bool wantSmaa =
            (g_config.aa_mode == 3 || g_config.aa_mode == 4);
        const bool wantFxaa =
            (g_config.aa_mode == 1 || g_config.aa_mode == 2 ||
             g_config.aa_mode == 4);
        const bool fxaaStrong = g_config.aa_mode == 2 || g_config.aa_mode == 4;
        const bool wantAa = wantSmaa || wantFxaa;
        const bool wantRcas = g_config.sharpness > 0.001f;
        const bool post = wantAa || wantRcas;
        // DLSS already produced perceptual pixels at the destination size.
        // RCAS can consume those directly: the identity resolve otherwise
        // decodes/encodes every pixel and writes a redundant full-size target.
        const bool directRcas = reconstructed && wantRcas && !wantAa &&
            srcDesc.Width == dstW && srcDesc.Height == dstH &&
            srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;

        // NO FALLBACKS (project policy). The IQ path always renders the eye --
        // "everything off" still runs the linear resolve pass, it never calls
        // back into the stock blit. A genuinely missing prerequisite is a BUG and
        // is surfaced LOUDLY, not hidden by silently reverting to the old look.
        if (!EnsureIqPipeline())
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("IQ ERROR: shader pipeline unavailable; eye NOT processed"); }
            return false;
        }
        const DXGI_FORMAT chainFormat=UnormSibling(static_cast<DXGI_FORMAT>(g_xrFormat));
        const bool fusedRcas = reconstructed && wantRcas && !wantAa && !wantSharp &&
            g_iqDlssRcas && srcDesc.Format==DXGI_FORMAT_R8G8B8A8_UNORM &&
            (chainFormat==DXGI_FORMAT_R8G8B8A8_UNORM || chainFormat==DXGI_FORMAT_B8G8R8A8_UNORM);
        if (wantSmaa && !EnsureSmaaPipeline())
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("IQ ERROR: SMAA 1x pipeline unavailable; eye NOT processed"); }
            return false;
        }
        ID3D11ShaderResourceView* srcSrv = AcquireSrcSrv(src, srcDesc);
        if (!srcSrv)
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("IQ ERROR: source SRV unavailable; eye NOT processed"); }
            return false;
        }
        const int intermediateCount = dlss::ResolveIntermediateCount(
            wantSmaa, wantFxaa, wantRcas, directRcas || fusedRcas);
        if (intermediateCount == 0)
            ReleaseIqChain();
        else if (!EnsureIqChain(dstW, dstH, intermediateCount))
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("IQ ERROR: intermediate chain unavailable; eye NOT processed"); }
            return false;
        }

        // One-time and on-change: prove in the log exactly what the eye pass does.
        {
            const char* aaName = "off";
            if (g_config.aa_mode == 1) aaName = "fxaa";
            else if (g_config.aa_mode == 2) aaName = "fxaa-strong";
            else if (g_config.aa_mode == 3) aaName = "smaa1x";
            else if (g_config.aa_mode == 4) aaName = "smaa1x+fxaa-strong";
            static int lf = -99, la = -99; static float ls = -1.0f;
            static uint32_t lsw = 0, lsh = 0, ldw = 0, ldh = 0;
            static int lc = -1, li = -1;
            const int combined = directRcas || fusedRcas ? 1 : 0;
            if (lf != g_config.upscale_filter || la != g_config.aa_mode ||
                ls != g_config.sharpness || lsw != srcDesc.Width || ldw != dstW ||
                lsh != srcDesc.Height || ldh != dstH ||
                lc != combined || li != intermediateCount)
            {
                lf = g_config.upscale_filter; la = g_config.aa_mode;
                ls = g_config.sharpness; lsw = srcDesc.Width; ldw = dstW;
                lsh = srcDesc.Height; ldh = dstH; lc = combined; li = intermediateCount;
                LOG("IQ: eye pass active -- resolve=%s aa=%s(%d) sharpen=%.2f "
                    "rcasGain=2.00 src=%ux%u dst=%ux%u post=%d xrSrgb=%d combined=%d "
                    "intermediates=%d (%.1f MiB allocated)",
                    wantSharp ? "bicubic-a075" : "linear", aaName,
                    g_config.aa_mode, g_config.sharpness, srcDesc.Width,
                    srcDesc.Height, dstW, dstH, post ? 1 : 0, xrSrgb ? 1 : 0,
                    combined, intermediateCount,
                    double(dstW) * dstH * DxgiBytesPerPixel(chainFormat) *
                        intermediateCount / (1024.0 * 1024.0));
            }
        }

        const float srcIsSrgb = IsSrgb(srcDesc.Format) ? 1.0f : 0.0f;

        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedPsCb0 = nullptr;
        ID3D11Buffer* savedVsCb0 = nullptr;
        ID3D11ShaderResourceView* savedExtraSrvs[2]{};
        ID3D11SamplerState* savedPointSampler = nullptr;
        g_context->PSGetConstantBuffers(0, 1, &savedPsCb0);
        if (wantSmaa)
        {
            g_context->VSGetConstantBuffers(0, 1, &savedVsCb0);
            g_context->PSGetShaderResources(1, 2, savedExtraSrvs);
            g_context->PSGetSamplers(1, 1, &savedPointSampler);
        }

        // Shared pipeline state for every pass.
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        if (wantSmaa)
        {
            g_context->PSSetSamplers(1, 1, &g_smaaPointSampler);
            g_context->VSSetConstantBuffers(0, 1, &g_iqCb);
        }
        g_context->PSSetConstantBuffers(0, 1, &g_iqCb);

        auto pass = [&](ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                         ID3D11ShaderResourceView* in0,
                         ID3D11ShaderResourceView* in1,
                         ID3D11ShaderResourceView* in2,
                         ID3D11RenderTargetView* out, uint32_t inW, uint32_t inH,
                         uint32_t outW, uint32_t outH, bool outPerceptual, float inIsSrgb) {
            IqParams p{};
            p.srcW = (float)inW; p.srcH = (float)inH;
            p.dstW = (float)outW; p.dstH = (float)outH;
            p.sharpness = g_config.sharpness;
            p.srcIsSrgb = inIsSrgb;
            p.outPerceptual = outPerceptual ? 1.0f : 0.0f;
            p.aaStrength = fxaaStrong ? 1.0f : 0.0f;
            g_context->UpdateSubresource(g_iqCb, 0, nullptr, &p, 0, 0);
            g_context->OMSetRenderTargets(1, &out, nullptr);
            D3D11_VIEWPORT vp{0, 0, (float)outW, (float)outH, 0, 1};
            g_context->RSSetViewports(1, &vp);
            g_context->VSSetShader(vs, nullptr, 0);
            g_context->PSSetShader(ps, nullptr, 0);
            ID3D11ShaderResourceView* inputs[3] = {in0, in1, in2};
            const UINT inputCount = (in1 || in2) ? 3u : 1u;
            g_context->PSSetShaderResources(0, inputCount, inputs);
            g_context->Draw(3, 0);
            ID3D11ShaderResourceView* nullSrvs[3]{}; // unbind before an input becomes an RTV
            g_context->PSSetShaderResources(0, inputCount, nullSrvs);
        };

        ID3D11PixelShader* resolve = wantSharp ? g_iqResolveSharp : g_iqResolveLinear;
        // Exactly three stamps per eye on BOTH branches, so the collector can
        // read them as fixed triples: before the resolve, after the resolve,
        // after everything. On the single-pass branch the third equals the
        // second and post reads as zero, which is the answer we want it to give.
        IqTimerStamp();
        if (directRcas || fusedRcas)
        {
            IqTimerStamp(); // no separate resampling stage
            pass(g_blitVs, directRcas ? g_iqRcas : g_iqDlssRcas, srcSrv, nullptr, nullptr, dstRtv,
                 srcDesc.Width, srcDesc.Height, dstW, dstH, finalPerceptual, 0.0f);
        }
        else if (!post)
        {
            // One pass straight to XR: linear for an sRGB RTV, perceptual for
            // the runtime's non-sRGB fallback formats.
            pass(g_blitVs, resolve, srcSrv, nullptr, nullptr, dstRtv,
                 srcDesc.Width, srcDesc.Height, dstW, dstH,
                 finalPerceptual, srcIsSrgb);
            IqTimerStamp();
        }
        else
        {
            int cur = 0;
            pass(g_blitVs, resolve, srcSrv, nullptr, nullptr, g_iqChainRtv[cur],
                 srcDesc.Width, srcDesc.Height, dstW, dstH, true, srcIsSrgb);
            IqTimerStamp();

            if (wantSmaa)
            {
                // Preserve resolved color in slot 0; edge data goes to slot 1,
                // official blend weights to slot 2, then slot 1 is reused for
                // neighborhood output if another pass follows.
                pass(g_smaaEdgesVs, g_smaaEdgesPs, g_iqChainSrv[0], nullptr,
                     nullptr, g_iqChainRtv[1], dstW, dstH, dstW, dstH, true, 1.0f);
                pass(g_smaaWeightsVs, g_smaaWeightsPs, g_iqChainSrv[1],
                     g_smaaAreaSrv, g_smaaSearchSrv, g_iqChainRtv[2],
                     dstW, dstH, dstW, dstH, true, 1.0f);

                const bool smaaFinal = !wantFxaa && !wantRcas;
                ID3D11RenderTargetView* smaaOut =
                    smaaFinal ? dstRtv : g_iqChainRtv[1];
                pass(g_smaaNeighborhoodVs, g_smaaNeighborhoodPs,
                     g_iqChainSrv[0], g_iqChainSrv[2], nullptr, smaaOut,
                     dstW, dstH, dstW, dstH,
                     smaaFinal ? finalPerceptual : true, 1.0f);
                cur = 1;
            }

            if (wantFxaa)
            {
                const bool fxaaFinal = !wantRcas;
                const int next = 1 - cur;
                ID3D11RenderTargetView* fxaaOut =
                    fxaaFinal ? dstRtv : g_iqChainRtv[next];
                pass(g_blitVs, g_iqFxaa, g_iqChainSrv[cur], nullptr, nullptr,
                     fxaaOut, dstW, dstH, dstW, dstH,
                     fxaaFinal ? finalPerceptual : true, 1.0f);
                cur = next;
            }
            if (wantRcas)
            {
                pass(g_blitVs, g_iqRcas, g_iqChainSrv[cur], nullptr, nullptr,
                     dstRtv, dstW, dstH, dstW, dstH,
                     finalPerceptual, 1.0f);
            }
        }
        IqTimerStamp();

        backup.Restore(g_context);
        g_context->PSSetConstantBuffers(0, 1, &savedPsCb0);
        if (savedPsCb0) savedPsCb0->Release();
        if (wantSmaa)
        {
            g_context->VSSetConstantBuffers(0, 1, &savedVsCb0);
            g_context->PSSetShaderResources(1, 2, savedExtraSrvs);
            g_context->PSSetSamplers(1, 1, &savedPointSampler);
            if (savedVsCb0) savedVsCb0->Release();
            for (auto*& srv : savedExtraSrvs) if (srv) srv->Release();
            if (savedPointSampler) savedPointSampler->Release();
        }
        return true;
    }

    // =====================================================================
    // Optional NVIDIA DLSS eye resolve
    //
    // Replaces BlitImageQuality for one eye when config.upscaler selects DLSS
    // and everything DLSS needs exists for that eye: the title published the
    // camera it rasterized with (VR_PublishEyeCamera), the eye's depth buffer
    // was copied at eye end (DlssCaptureEyeDepth), NGX initialized on this
    // device and a feature exists for this render/output shape. Any missing
    // piece falls back to the mod's own resolve for that eye and frame and
    // says why, at most once every two seconds. Nothing here can disarm a
    // title, end the OpenXR session, or change what the game renders except
    // the sub-pixel jitter the title asks for through VR_GetEyeJitter.
    //
    // Halo 3 at eye end: direct eye colour + direct engine-depth SRV ->
    // camera/depth pass -> native D3D11 NGX evaluation. Other title paths keep
    // their input captures. The ordinary configured output resolve runs at
    // Present. Image-search stays disabled. The independently measured
    // full-output resolve combines resampling/sharpening when compatible.
    struct DlssMotionParams
    {
        float curPos[4], curRight[4], curUp[4], curFwd[4], curProj[4], curDepth[4];
        float prevPos[4], prevRight[4], prevUp[4], prevFwd[4], prevProj[4], prevDepth[4];
        float size[4];   // w, h, 1/w, 1/h
        float debug[4];  // x: motion-vector scale for the diagnostic view
    };
    static_assert(sizeof(DlssMotionParams) == 14 * 16);

    const char* DlssTitleName(GameTitle title)
    {
        switch (title)
        {
        case GameTitle::Halo3: return "Halo 3";
        case GameTitle::Halo3ODST: return "Halo 3: ODST";
        case GameTitle::HaloReach: return "Halo: Reach";
        case GameTitle::Halo4: return "Halo 4";
        case GameTitle::HaloCE: return "Halo: CE";
        case GameTitle::Halo2: return "Halo 2";
        default: return "no title";
        }
    }

    void SetDlssStatus(const char* text)
    {
        AcquireSRWLockExclusive(&g_dlssStatusLock);
        snprintf(g_dlssStatusText, sizeof(g_dlssStatusText), "%s", text);
        ReleaseSRWLockExclusive(&g_dlssStatusLock);
    }

    // One bounded line per reason change or per two seconds, plus the F1
    // status, so a persistent fallback is visible without flooding.
    void NoteDlssFallback(int eye, const char* reason)
    {
        ++g_dlssFallbackEyes;
        const uint64_t nowMs = GetTickCount64();
        const bool changed = strncmp(g_dlssFallbackReason, reason,
                                     sizeof(g_dlssFallbackReason) - 1) != 0;
        if (changed || nowMs - g_dlssFallbackLogMs >= 2000)
        {
            g_dlssFallbackLogMs = nowMs;
            snprintf(g_dlssFallbackReason, sizeof(g_dlssFallbackReason), "%s",
                     reason);
            LOG("DLSS: eye %d uses the mod's own resolve: %s", eye, reason);
            char text[256];
            snprintf(text, sizeof(text), "not running: %s", reason);
            SetDlssStatus(text);
        }
    }

    bool EnsureDlssMotionPipeline()
    {
        if (g_dlssMotionPs && g_dlssDebugPs && g_dlssCopyPs && g_dlssMotionCb)
            return true;
        if (g_dlssMotionInitFailed || !g_device || !EnsureBlitPipeline())
            return false;
        auto release = [](auto*& object) {
            if (object) object->Release();
            object = nullptr;
        };
        release(g_dlssMotionPs);
        release(g_dlssDebugPs);
        release(g_dlssCopyPs);
        release(g_dlssMotionCb);

        if (FAILED(g_device->CreatePixelShader(g_dlssCameraPs, sizeof(g_dlssCameraPs), nullptr, &g_dlssMotionPs)) ||
            FAILED(g_device->CreatePixelShader(g_dlssDebugPsCode, sizeof(g_dlssDebugPsCode), nullptr, &g_dlssDebugPs)) ||
            FAILED(g_device->CreatePixelShader(g_dlssCopyPsCode, sizeof(g_dlssCopyPsCode), nullptr, &g_dlssCopyPs)))
        {
            g_dlssMotionInitFailed = true;
            return false;
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(DlssMotionParams);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_device->CreateBuffer(&cb, nullptr, &g_dlssMotionCb)))
        {
            g_dlssMotionInitFailed = true;
            return false;
        }
        LOG("DLSS: camera-motion pipeline ready");
        return true;
    }

    void ReleaseDlssEyeTextures(DlssEyeState& state)
    {
        // Revoke before Release: pause or a skipped submission can leave an
        // unconsumed result pointing at this texture when resize destroys it.
        g_dlssKicked[static_cast<int>(&state - g_dlssEye)].Reset();
        auto release = [](auto*& object) {
            if (object) object->Release();
            object = nullptr;
        };
        // Blit() may have cached a view of the output; drop ours first.
        if (state.output)
            ForgetSourceView(state.output);
        release(state.depthCopySrv);
        release(state.depthCopy);
        release(state.colorIn);
        release(state.motionSrv);
        release(state.motionRtv);
        release(state.motion);
        release(state.depthOutRtv);
        release(state.depthOut);
        release(state.output);
        release(state.colorInSrv);
        for (int i = 0; i < 2; ++i)
        {
            release(state.flowInputRtv[i]);
            release(state.flowInput[i]);
        }
        // Shared D3D12 textures: the backend drops its own references too.
        Dlss_ReleaseEyeTextures(static_cast<int>(&state - g_dlssEye));
        state.depthCopyDesc = {};
        state.colorInDesc = {};
        state.outputDesc = {};
        state.depthValid = false;
        state.depthDirect = false;
        state.resetPending = true;
    }

    void ReleaseDlssStereoBatch()
    {
        g_dlssKicked[0].Reset();
        g_dlssKicked[1].Reset();
        auto release = [](auto*& object) {
            if (object) object->Release();
            object = nullptr;
        };
        if (g_dlssBatch.output)
            ForgetSourceView(g_dlssBatch.output);
        for (auto* output : g_dlssBatch.eyeOutput)
            if (output) ForgetSourceView(output);
        release(g_dlssBatch.motionRtv);
        release(g_dlssBatch.depthRtv);
        release(g_dlssBatch.color);
        release(g_dlssBatch.motion);
        release(g_dlssBatch.depth);
        release(g_dlssBatch.output);
        release(g_dlssBatch.eyeOutput[0]);
        release(g_dlssBatch.eyeOutput[1]);
        g_dlssBatch = {};
    }

    void ReleaseDlssDepthTracking()
    {
        if (g_dlssSceneDepthDsv)
        {
            g_dlssSceneDepthDsv->Release();
            g_dlssSceneDepthDsv = nullptr;
        }
        for (int i = 0; i < g_dlssDepthViewCount; ++i)
        {
            if (g_dlssDepthViews[i].view)
                g_dlssDepthViews[i].view->Release();
            g_dlssDepthViews[i] = {};
        }
        g_dlssDepthViewCount = 0;
        g_dlssDepthViewTableFull = false;
        g_dlssEyeSceneDepthSeen = false;
        g_dlssEyeLastFullRasterDsv = nullptr;
        g_dlssDepthSourceLogged = false;
    }

    // Resize, title detach, or the user turning the upscaler off: every
    // DLSS-owned texture, feature and depth reference goes. NGX itself stays
    // initialized for the device; the shaders are device-lifetime.
    void ReleaseDlssResources()
    {
        g_dlssKicked[0].Reset();
        g_dlssKicked[1].Reset();
        Dlss_ReleaseFeatures();
        ReleaseDlssStereoBatch();
        for (DlssEyeState& state : g_dlssEye)
        {
            ReleaseDlssEyeTextures(state);
            state.previous = {};
            state.published = false;
        }
        ReleaseDlssDepthTracking();
        g_dlssJitterArmed = false;
        g_dlssEyesResolvedThisFrame = 0;
        g_dlssResourcesLogged = false;
    }

    // Whether a depth view is the size the eye is rasterized at RIGHT NOW:
    // the backbuffer once Present re-learned it, else the render plan. A
    // live resize binds the new depth views before the first Present at the
    // new size, so this is evaluated on every bind, never stored: the
    // DLSS-4 log classified all four views as "not full raster" during that
    // window and DLSS then had no depth for the rest of the session.
    bool DlssDepthIsFullRaster(const D3D11_TEXTURE2D_DESC& desc)
    {
        if (!desc.Width || !desc.Height)
            return false;
        if (g_gameBackbufferDescValid)
            return desc.Width == g_gameBackbufferDesc.Width &&
                desc.Height == g_gameBackbufferDesc.Height;
        unsigned renderW = 0, renderH = 0, outputW = 0, outputH = 0;
        bool dlss = false, runtimePresent = false;
        D3D_GetRenderPlan(renderW, renderH, outputW, outputH, dlss, runtimePresent);
        return renderW && desc.Width == renderW && desc.Height == renderH;
    }

    // OM hook, inside an eye. `sceneBound` is true when this bind carried the
    // learned scene-colour target.
    void DlssNoteEyeDepth(ID3D11DepthStencilView* depth, bool sceneBound)
    {
        if (!depth)
            return;
        if (sceneBound)
        {
            if (depth != g_dlssSceneDepthDsv)
            {
                depth->AddRef();
                if (g_dlssSceneDepthDsv)
                    g_dlssSceneDepthDsv->Release();
                g_dlssSceneDepthDsv = depth;
            }
            g_dlssEyeSceneDepthSeen = true;
            return;
        }
        for (int i = 0; i < g_dlssDepthViewCount; ++i)
        {
            if (g_dlssDepthViews[i].view == depth)
            {
                if (DlssDepthIsFullRaster(g_dlssDepthViews[i].desc))
                    g_dlssEyeLastFullRasterDsv = depth;
                return;
            }
        }
        if (g_dlssDepthViewCount >= kDlssDepthViewSlots)
        {
            g_dlssDepthViewTableFull = true;
            return;
        }
        // First sight of this view: describe it once (the only COM work this
        // hook ever does for depth) and pin it.
        ID3D11Resource* resource = nullptr;
        depth->GetResource(&resource);
        ID3D11Texture2D* texture = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        if (resource &&
            SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void**>(&texture))))
        {
            texture->GetDesc(&desc);
        }
        const bool fullRaster = texture && DlssDepthIsFullRaster(desc);
        if (texture) texture->Release();
        if (resource) resource->Release();
        depth->AddRef();
        DlssDepthViewEntry& entry = g_dlssDepthViews[g_dlssDepthViewCount++];
        entry.view = depth;
        entry.fullRaster = fullRaster;
        entry.desc = desc;
        if (fullRaster)
            g_dlssEyeLastFullRasterDsv = depth;
    }

    // Eye end, render thread: copy the depth buffer that represents this eye
    // before the other eye or the next frame overwrites it.
    void DlssCaptureEyeDepth(int eye)
    {
        if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera() ||
            !g_context || !g_device)
            return;
        DlssEyeState& state = g_dlssEye[eye];
        state.depthValid = false;
        if (!state.published)
            return; // no camera for this eye: the resolve would not run anyway
        bool usedSceneBound = false;
        if (!dlss::ChooseDepthSource(
                g_dlssEyeSceneDepthSeen && g_dlssSceneDepthDsv != nullptr,
                g_dlssEyeLastFullRasterDsv != nullptr, usedSceneBound))
        {
            static uint64_t loggedMs = 0;
            const uint64_t nowMs = GetTickCount64();
            if (nowMs - loggedMs >= 5000)
            {
                loggedMs = nowMs;
                LOG("DLSS: eye %d saw no usable depth view (%d classified%s; "
                    "raster %ux%u%s)",
                    eye, g_dlssDepthViewCount,
                    g_dlssDepthViewTableFull ? ", table full" : "",
                    g_gameBackbufferDescValid ? g_gameBackbufferDesc.Width : 0u,
                    g_gameBackbufferDescValid ? g_gameBackbufferDesc.Height : 0u,
                    g_gameBackbufferDescValid ? "" : " not yet re-learned");
                for (int i = 0; i < g_dlssDepthViewCount && i < kDlssDepthViewSlots; ++i)
                {
                    LOG("DLSS:   depth view %d: %ux%u fmt %u samples %u",
                        i, g_dlssDepthViews[i].desc.Width, g_dlssDepthViews[i].desc.Height,
                        static_cast<unsigned>(g_dlssDepthViews[i].desc.Format),
                        g_dlssDepthViews[i].desc.SampleDesc.Count);
                }
            }
            return;
        }
        ID3D11DepthStencilView* dsv =
            usedSceneBound ? g_dlssSceneDepthDsv : g_dlssEyeLastFullRasterDsv;
        ID3D11Resource* resource = nullptr;
        dsv->GetResource(&resource);
        if (!resource)
            return;
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(&texture))))
        {
            resource->Release();
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        uint32_t copyFormat = 0, viewFormat = 0;
        const bool formatOk = dlss::DepthCopyFormats(
            static_cast<uint32_t>(desc.Format), copyFormat, viewFormat);
        const bool shapeOk = desc.SampleDesc.Count == 1 && desc.Width && desc.Height;
        if (!formatOk || !shapeOk)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("DLSS: eye depth %ux%u fmt %u samples %u cannot be copied "
                    "(%s); DLSS stays off for this title",
                    desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                    desc.SampleDesc.Count,
                    formatOk ? "multisampled" : "not a depth format");
            }
            texture->Release();
            resource->Release();
            return;
        }
        // The old per-eye Halo 3 path could borrow this view because it
        // evaluated before the other eye rendered. A stereo batch defers its
        // one evaluation until both eyes exist, so each eye's depth must be
        // retained independently.
        unsigned planRenderW = 0, planRenderH = 0, planOutputW = 0;
        unsigned planOutputH = 0;
        bool plannedDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutputW, planOutputH,
                          plannedDlss, runtimePresent);
        const bool direct = TitleAdapter_GetActiveTitle() == GameTitle::Halo3 &&
            !(dlss::kEnableDlss21StereoBatch && plannedDlss) && !g_config.dlss_debug_view &&
            !Dlss_UsesSharedTextures() &&
            (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
            desc.Format == static_cast<DXGI_FORMAT>(copyFormat);
        if (direct && (!state.depthDirect || state.depthCopy != texture))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = static_cast<DXGI_FORMAT>(viewFormat);
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(g_device->CreateShaderResourceView(texture, &view, &srv)))
            {
                if (state.depthCopySrv) state.depthCopySrv->Release();
                if (state.depthCopy) state.depthCopy->Release();
                texture->AddRef();
                state.depthCopy = texture; state.depthCopySrv = srv;
                state.depthCopyDesc = desc; state.depthDirect = true;
            }
        }
        const bool usingDirect = direct && state.depthDirect && state.depthCopy == texture;
        if (!usingDirect && (!state.depthCopy || state.depthDirect || state.depthCopyDesc.Width != desc.Width ||
            state.depthCopyDesc.Height != desc.Height ||
            state.depthCopyDesc.Format != static_cast<DXGI_FORMAT>(copyFormat)))
        {
            state.depthDirect = false;
            if (state.depthCopySrv) { state.depthCopySrv->Release(); state.depthCopySrv = nullptr; }
            if (state.depthCopy) { state.depthCopy->Release(); state.depthCopy = nullptr; }
            D3D11_TEXTURE2D_DESC copyDesc{};
            copyDesc.Width = desc.Width;
            copyDesc.Height = desc.Height;
            copyDesc.MipLevels = 1;
            copyDesc.ArraySize = 1;
            copyDesc.Format = static_cast<DXGI_FORMAT>(copyFormat);
            copyDesc.SampleDesc.Count = 1;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = static_cast<DXGI_FORMAT>(viewFormat);
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(g_device->CreateTexture2D(&copyDesc, nullptr, &state.depthCopy)) ||
                FAILED(g_device->CreateShaderResourceView(
                    state.depthCopy, &srvDesc, &state.depthCopySrv)))
            {
                LOG("DLSS ERROR: eye %d depth copy (%ux%u fmt %u) could not be created",
                    eye, desc.Width, desc.Height, copyFormat);
                if (state.depthCopySrv) { state.depthCopySrv->Release(); state.depthCopySrv = nullptr; }
                if (state.depthCopy) { state.depthCopy->Release(); state.depthCopy = nullptr; }
                state.depthCopyDesc = {};
                texture->Release();
                resource->Release();
                return;
            }
            state.depthCopyDesc = copyDesc;
        }
        // Whole-subresource copy: the only copy D3D11 allows from a depth
        // buffer, and it needs no unbinding.
        if (!usingDirect)
            g_context->CopySubresourceRegion(state.depthCopy, 0, 0, 0, 0, texture, 0, nullptr);
        state.depthValid = true;
        if (!g_dlssDepthSourceLogged)
        {
            g_dlssDepthSourceLogged = true;
            LOG("DLSS: depth input %s", usingDirect ? "direct engine SRV; no depth copy" : "captured per eye");
            LOG("DLSS: eye depth source is %s: %ux%u fmt %u (copy fmt %u, view fmt %u); "
                "%d depth view(s) classified%s",
                usedSceneBound
                    ? "the depth view bound with the learned scene-colour target"
                    : "the last full-raster depth view bound inside the eye",
                desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                copyFormat, viewFormat, g_dlssDepthViewCount,
                g_dlssDepthViewTableFull ? " (table full)" : "");
        }
        texture->Release();
        resource->Release();
    }

    bool EnsureDlssEyeResources(
        int eye, DlssEyeState& state, const D3D11_TEXTURE2D_DESC& srcDesc,
        const dlss::OutputSize& out)
    {
        const bool directColor = !Dlss_UsesSharedTextures() && g_eyeCacheNativeDlss &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo3;
        const bool needColorCopy = !directColor && (IsSrgb(srcDesc.Format) ||
            !(srcDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS));
        const bool renderMatches = state.motion &&
            state.colorInDesc.Width == srcDesc.Width &&
            state.colorInDesc.Height == srcDesc.Height &&
            (!needColorCopy || state.colorIn);
        const bool outputMatches = state.output &&
            state.outputDesc.Width == out.width &&
            state.outputDesc.Height == out.height;
        if (renderMatches && outputMatches)
            return true;
        if (state.failedRenderW == srcDesc.Width && state.failedRenderH == srcDesc.Height &&
            state.failedOutputW == out.width && state.failedOutputH == out.height)
            return false;
        // Keep the depth copy: it belongs to the engine's buffer shape.
        ID3D11Texture2D* depthCopy = state.depthCopy;
        ID3D11ShaderResourceView* depthCopySrv = state.depthCopySrv;
        const D3D11_TEXTURE2D_DESC depthCopyDesc = state.depthCopyDesc;
        const bool depthValid = state.depthValid;
        const bool depthDirect = state.depthDirect;
        state.depthCopy = nullptr;
        state.depthCopySrv = nullptr;
        ReleaseDlssEyeTextures(state);
        state.depthCopy = depthCopy;
        state.depthCopySrv = depthCopySrv;
        state.depthCopyDesc = depthCopyDesc;
        state.depthValid = depthValid;
        state.depthDirect = depthDirect;

        D3D11_TEXTURE2D_DESC d{};
        d.Width = srcDesc.Width;
        d.Height = srcDesc.Height;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        bool ok = true;
        D3D11_TEXTURE2D_DESC o = d;
        o.Width = out.width;
        o.Height = out.height;
        o.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        o.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_UNORDERED_ACCESS;
        if (Dlss_UsesSharedTextures())
        {
            // D3D12 backend: the textures live on the D3D12 heap and are
            // opened here; the eye is always copied into the shared colour
            // input (the game's own target cannot be shared). With object
            // motion the backend also hands out the two flow input frames.
            DlssEyeTextures shared{};
            ok = Dlss_CreateEyeTextures(
                eye, srcDesc.Width, srcDesc.Height, out.width, out.height,
                needColorCopy ? UnormSibling(srcDesc.Format) : srcDesc.Format, shared);
            if (ok)
            {
                state.colorIn = shared.color;
                state.motion = shared.motion;
                state.depthOut = shared.depth;
                state.output = shared.output;
                state.flowInput[0] = shared.flowInput[0];
                state.flowInput[1] = shared.flowInput[1];
                d.Format = needColorCopy ? UnormSibling(srcDesc.Format) : srcDesc.Format;
                ok = SUCCEEDED(g_device->CreateRenderTargetView(state.motion, nullptr, &state.motionRtv)) &&
                    SUCCEEDED(g_device->CreateShaderResourceView(state.motion, nullptr, &state.motionSrv)) &&
                    SUCCEEDED(g_device->CreateRenderTargetView(state.depthOut, nullptr, &state.depthOutRtv));
                for (int i = 0; i < 2 && ok && state.flowInput[i]; ++i)
                    ok = SUCCEEDED(g_device->CreateRenderTargetView(state.flowInput[i], nullptr, &state.flowInputRtv[i]));
            }
        }
        else
        {
            if (needColorCopy)
            {
                // Every DLSS input carries UAV access as well: NGX's D3D11
                // layer rebuilt a "cache helper" for inputs without it on
                // every evaluation (DLSS-1 log); with the flags the count
                // fell to a handful per session (DLSS-7 log).
                d.Format = UnormSibling(srcDesc.Format);
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                ok = SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &state.colorIn));
            }
            d.Format = DXGI_FORMAT_R16G16_FLOAT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_UNORDERED_ACCESS;
            ok = ok && SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &state.motion)) &&
                SUCCEEDED(g_device->CreateRenderTargetView(state.motion, nullptr, &state.motionRtv)) &&
                SUCCEEDED(g_device->CreateShaderResourceView(state.motion, nullptr, &state.motionSrv));
            d.Format = DXGI_FORMAT_R32_FLOAT;
            ok = ok && SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &state.depthOut)) &&
                SUCCEEDED(g_device->CreateRenderTargetView(state.depthOut, nullptr, &state.depthOutRtv));
            ok = ok && SUCCEEDED(g_device->CreateTexture2D(&o, nullptr, &state.output));
        }
        if (ok && state.colorIn)
            ok = SUCCEEDED(g_device->CreateShaderResourceView(state.colorIn, nullptr, &state.colorInSrv));
        state.colorInDesc = d;
        state.outputDesc = o;
        if (!ok)
        {
            LOG("DLSS ERROR: eye %d textures could not be created (%ux%u -> %ux%u); "
                "this shape is not retried",
                eye, srcDesc.Width, srcDesc.Height, out.width, out.height);
            ReleaseDlssEyeTextures(state);
            state.failedRenderW = srcDesc.Width;
            state.failedRenderH = srcDesc.Height;
            state.failedOutputW = out.width;
            state.failedOutputH = out.height;
            return false;
        }
        state.failedRenderW = state.failedRenderH = 0;
        state.failedOutputW = state.failedOutputH = 0;
        state.resetPending = true;
        if (!g_dlssResourcesLogged || eye == 1)
        {
            g_dlssResourcesLogged = true;
            const double renderMb = static_cast<double>(srcDesc.Width) * srcDesc.Height *
                ((state.depthDirect ? 0.0 : DxgiBytesPerPixel(state.depthCopyDesc.Format)) +
                 4.0 + 4.0 + (state.colorIn ? 4.0 : 0.0) + (state.flowInput[0] ? 12.0 : 0.0)) /
                (1024.0 * 1024.0);
            const double outMb = static_cast<double>(out.width) * out.height * 4.0 /
                (1024.0 * 1024.0);
            LOG("DLSS: eye %d resources: render %ux%u (%s%s%s), output %ux%u%s; "
                "about %.0f MB per eye before DLSS's own allocations",
                eye, srcDesc.Width, srcDesc.Height,
                directColor ? "direct eye colour, " : (needColorCopy ? "eye copied to UNORM, " : ""),
                "motion RG16F + depth R32F",
                Dlss_HasImageMotion(eye) ? ", GPU image motion (private history/pyramids excluded from estimate)"
                    : (state.flowInput[0] ? ", external optical flow" : ", camera-only motion"),
                out.width, out.height,
                out.exact ? " (exact headset slice)" : " (raster aspect, stretched into the slice)",
                renderMb + outMb);
        }
        return true;
    }

    // Eye colour -> the optical-flow input frame for this render (the
    // engine's BGRA8 format; the render target view does the channel order).
    // True when the frame was written.
    bool RunDlssFlowCopyPass(DlssEyeState& state, int index, uint32_t width, uint32_t height)
    {
        ID3D11RenderTargetView* target =
            (index == 0 || index == 1) ? state.flowInputRtv[index] : nullptr;
        if (!target || !state.colorInSrv || !g_dlssCopyPs)
            return false;
        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11ShaderResourceView* savedSrv3 = nullptr;
        g_context->PSGetShaderResources(3, 1, &savedSrv3);
        g_context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT vp{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        g_context->RSSetViewports(1, &vp);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_dlssCopyPs, nullptr, 0);
        g_context->PSSetShaderResources(3, 1, &state.colorInSrv);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        g_context->PSSetShaderResources(3, 1, &nullSrv);
        ID3D11RenderTargetView* nullRtv = nullptr;
        g_context->OMSetRenderTargets(1, &nullRtv, nullptr);
        backup.Restore(g_context);
        g_context->PSSetShaderResources(3, 1, &savedSrv3);
        if (savedSrv3) savedSrv3->Release();
        return true;
    }

    void RunDlssMotionPass(DlssEyeState& state, const dlss::CameraSample& previous,
                           uint32_t width, uint32_t height,
                           ID3D11RenderTargetView* motionTarget = nullptr,
                           ID3D11RenderTargetView* depthTarget = nullptr,
                           uint32_t viewportX = 0)
    {
        const dlss::CameraSample& cur = state.current;
        DlssMotionParams p{};
        auto put3 = [](float out[4], const float in[3]) {
            out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = 0.0f;
        };
        put3(p.curPos, cur.position); put3(p.curRight, cur.right);
        put3(p.curUp, cur.up); put3(p.curFwd, cur.forward);
        p.curProj[0] = cur.projection.tanX; p.curProj[1] = cur.projection.tanY;
        p.curProj[2] = cur.projection.centerX; p.curProj[3] = cur.projection.centerY;
        p.curDepth[0] = cur.projection.depthA; p.curDepth[1] = cur.projection.depthB;
        put3(p.prevPos, previous.position); put3(p.prevRight, previous.right);
        put3(p.prevUp, previous.up); put3(p.prevFwd, previous.forward);
        p.prevProj[0] = previous.projection.tanX; p.prevProj[1] = previous.projection.tanY;
        p.prevProj[2] = previous.projection.centerX; p.prevProj[3] = previous.projection.centerY;
        p.prevDepth[0] = previous.projection.depthA; p.prevDepth[1] = previous.projection.depthB;
        p.size[0] = static_cast<float>(width); p.size[1] = static_cast<float>(height);
        // Classic's final full-raster output can be resized from its native
        // world raster. Both have the same proven full-view UV mapping.
        p.size[2] = static_cast<float>(state.depthCopyDesc.Width) / width;
        p.size[3] = static_cast<float>(state.depthCopyDesc.Height) / height;
        p.debug[0] = 1.0f / 16.0f; // +-16 render pixels spans the debug colour range
        p.debug[1] = cur.jitterNdcX;
        p.debug[2] = cur.jitterNdcY;
        p.debug[3] = static_cast<float>(viewportX);
        g_context->UpdateSubresource(g_dlssMotionCb, 0, nullptr, &p, 0, 0);

        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedCb = nullptr;
        ID3D11ShaderResourceView* savedSrv1 = nullptr;
        g_context->PSGetConstantBuffers(0, 1, &savedCb);
        g_context->PSGetShaderResources(1, 1, &savedSrv1);

        ID3D11RenderTargetView* targets[2] = {
            motionTarget ? motionTarget : state.motionRtv,
            depthTarget ? depthTarget : state.depthOutRtv};
        g_context->OMSetRenderTargets(2, targets, nullptr);
        D3D11_VIEWPORT vp{static_cast<float>(viewportX), 0,
            static_cast<float>(width), static_cast<float>(height), 0, 1};
        g_context->RSSetViewports(1, &vp);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_dlssMotionPs, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_dlssMotionCb);
        ID3D11ShaderResourceView* inputs[1] = {state.borrowedCeDepth?state.borrowedCeDepth:state.depthCopySrv};
        g_context->PSSetShaderResources(0, 1, inputs);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrvs[1]{};
        g_context->PSSetShaderResources(0, 1, nullSrvs);
        ID3D11RenderTargetView* nullRtvs[2]{};
        g_context->OMSetRenderTargets(2, nullRtvs, nullptr);

        backup.Restore(g_context);
        g_context->PSSetConstantBuffers(0, 1, &savedCb);
        g_context->PSSetShaderResources(1, 1, &savedSrv1);
        if (savedCb) savedCb->Release();
        if (savedSrv1) savedSrv1->Release();
    }

    // Diagnostic view: the motion vectors as colour, straight into the eye.
    void RunDlssDebugPass(DlssEyeState& state, ID3D11RenderTargetView* dstRtv,
                          uint32_t dstW, uint32_t dstH)
    {
        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedCb = nullptr;
        ID3D11ShaderResourceView* savedSrv1 = nullptr;
        g_context->PSGetConstantBuffers(0, 1, &savedCb);
        g_context->PSGetShaderResources(1, 1, &savedSrv1);
        g_context->OMSetRenderTargets(1, &dstRtv, nullptr);
        D3D11_VIEWPORT vp{0, 0, static_cast<float>(dstW), static_cast<float>(dstH), 0, 1};
        g_context->RSSetViewports(1, &vp);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_dlssDebugPs, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_dlssMotionCb);
        ID3D11ShaderResourceView* inputs[6] = {nullptr, state.motionSrv, nullptr,
                                               nullptr, nullptr, nullptr};
        g_context->PSSetShaderResources(0, 6, inputs);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrvs[6]{};
        g_context->PSSetShaderResources(0, 6, nullSrvs);
        backup.Restore(g_context);
        g_context->PSSetConstantBuffers(0, 1, &savedCb);
        g_context->PSSetShaderResources(1, 1, &savedSrv1);
        if (savedCb) savedCb->Release();
        if (savedSrv1) savedSrv1->Release();
    }

    // The DLSS output for one raster: the planned headset picture (native x
    // resolution_scale, the size the launcher shrank the render from) when the
    // raster has its shape and is no larger; otherwise the raster-shaped fit
    // into the slice, for a raster the plan did not produce (the fit is off
    // and MCC chose its own resolution, an old launcher, a supersampled
    // raster, which then runs as DLAA).
    dlss::OutputSize DlssOutputFor(uint32_t renderW, uint32_t renderH,
                                   uint32_t sliceW, uint32_t sliceH)
    {
        unsigned planRenderW = 0, planRenderH = 0, planOutW = 0, planOutH = 0;
        bool planDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutW, planOutH,
                          planDlss, runtimePresent);
        dlss::OutputSize out = dlss::PlannedOutput(
            renderW, renderH, planOutW, planOutH, sliceW, sliceH);
        if (!out.width || !out.height)
            out = dlss::FitOutput(renderW, renderH, sliceW, sliceH);
        return out;
    }

    // Once per frame: the render plan the config asks for (headset shape,
    // Resolution scale, Upscaler, DLSS mode) against the one in force. After
    // the wanted plan has been the same for half a second (a slider drag
    // changes it every frame) and it still differs, ask MCC to re-size.
    // D3D_RequestRenderPlan ignores a pending or refused size, so this is
    // idempotent and never spams.
    RuntimeMode DlssPresentationMode()
    {
        return g_pausePresentation.load() ? RuntimeMode::Paused
                                         : TitleAdapter_GetRuntimeMode();
    }

    void TickRenderPlan()
    {
        // A mode/model/size/debug change is an explicit retry. A persistent
        // failure otherwise remains at full resolution until the player makes
        // one of those changes or toggles DLSS off and on.
        static int lastUpscaler = g_config.upscaler;
        static int lastMode = g_config.dlss_mode;
        static int lastPreset = g_config.dlss_preset;
        static float lastScale = g_config.resolution_scale;
        static bool lastDebug = g_config.dlss_debug_view;
        const bool debugChanged = lastDebug != g_config.dlss_debug_view;
        const bool dlssSettingChanged = lastUpscaler != g_config.upscaler ||
            lastMode != g_config.dlss_mode || lastPreset != g_config.dlss_preset ||
            lastScale != g_config.resolution_scale ||
            debugChanged;
        if (dlssSettingChanged)
        {
            // Debug uses the old eye-local motion surfaces; normal operation
            // uses the compact atlas. Drop the other graph on either edge so
            // toggling the diagnostic cannot retain both allocations.
            if (debugChanged && g_config.upscaler == dlss::kUpscalerDlss)
                ReleaseDlssResources();
            if (g_dlssBatchSuppressed &&
                g_config.upscaler == dlss::kUpscalerDlss)
            {
                LOG("DLSS: settings changed; retrying the eye resolve");
                SetDlssStatus(
                    "retrying the eye resolve after a settings change");
            }
            g_dlssBatchSuppressed = false;
            g_dlssBatchFailedFrames = 0;
            lastUpscaler = g_config.upscaler;
            lastMode = g_config.dlss_mode;
            lastPreset = g_config.dlss_preset;
            lastScale = g_config.resolution_scale;
            lastDebug = g_config.dlss_debug_view;
        }
        int nativeW = 0, nativeH = 0;
        nativeW = kNativeRenderWidth; nativeH = kNativeRenderHeight;
        unsigned planRenderW = 0, planRenderH = 0, planOutW = 0, planOutH = 0;
        bool planDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutW, planOutH,
                          planDlss, runtimePresent);
        const dlss::RenderPlan world = dlss::PlanRender(
            nativeW, nativeH, g_config.resolution_scale, g_config.upscaler,
            g_config.dlss_mode, runtimePresent && !g_dlssBatchSuppressed &&
            dlss::TitleHasCameraContract(TitleAdapter_GetActiveTitle()) &&
            Dlss_EnsureInitialized(g_device));
        const RuntimeMode presentation = DlssPresentationMode();
        static dlss::RenderPlan lastWant{};
        static uint64_t stableSinceMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (!dlss::CanChangeRenderPlan(presentation))
        {
            // Rejected transition-resize hold, retained dormant. The policy
            // currently admits all modes, preserving full-resolution menus.
            lastWant = {};
            stableSinceMs = nowMs;
            return;
        }
        const dlss::RenderPlan want = dlss::PlanPresentation(world, presentation);
        if (!want.outputW || !want.outputH)
            return;
        if (want.renderW != lastWant.renderW || want.renderH != lastWant.renderH ||
            want.outputW != lastWant.outputW || want.outputH != lastWant.outputH ||
            want.dlss != lastWant.dlss)
        {
            lastWant = want;
            stableSinceMs = nowMs;
            return;
        }
        const bool differs =
            static_cast<unsigned>(want.renderW) != planRenderW ||
            static_cast<unsigned>(want.renderH) != planRenderH ||
            static_cast<unsigned>(want.outputW) != planOutW ||
            static_cast<unsigned>(want.outputH) != planOutH ||
            want.dlss != planDlss;
        if (!differs || nowMs - stableSinceMs < 500)
            return;
        const D3DRenderPlanResult result = D3D_RequestRenderPlan(
            static_cast<unsigned>(want.renderW), static_cast<unsigned>(want.renderH),
            static_cast<unsigned>(want.outputW), static_cast<unsigned>(want.outputH),
            want.dlss);
        if (result == D3DRenderPlanResult::Requested)
        {
            LOG("render plan queued: %s requests %dx%d canvas; world input %dx%d, output %dx%d",
                RuntimeModeName(presentation), want.renderW, want.renderH,
                world.renderW, world.renderH, world.outputW, world.outputH);
            Menu_PostLiveResize();
        }
    }

    bool EnsureDlssStereoBatchResources(const dlss::StereoBatchPlan& plan,
                                         DXGI_FORMAT sourceFormat);
    bool DlssStereoBatchResourcesMatch(const dlss::StereoBatchPlan& plan,
                                       DXGI_FORMAT sourceFormat);

    bool DlssKickStereoBatchEye(int eye, ID3D11Texture2D* src,
                                const D3D11_TEXTURE2D_DESC& srcDesc)
    {
        g_dlssBatchAttemptedThisFrame = true;
        auto fallback = [&](const char* why) {
            NoteDlssFallback(eye, why);
            g_dlssBatch.eyePrepared[eye < 0 || eye > 1 ? 0 : eye] = false;
            return false;
        };
        if (eye < 0 || eye > 1 || !src || !g_device || !g_context)
            return fallback("no device or eye image for the stereo batch");
        if (!Dlss_EnsureInitialized(g_device))
            return fallback(Dlss_StatusText());
        DlssEyeState& state = g_dlssEye[eye];
        if (!state.published || !state.current.valid)
            return fallback("no eye camera arrived for this stereo-batch eye");
        if (!state.depthValid || !state.depthCopy || !state.depthCopySrv)
            return fallback("no depth buffer was captured for this stereo-batch eye");
        if (srcDesc.SampleDesc.Count != 1)
            return fallback("the stereo-batch eye image is multisampled");

        unsigned planRenderW = 0, planRenderH = 0, planOutW = 0, planOutH = 0;
        bool plannedDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutW, planOutH,
                          plannedDlss, runtimePresent);
        const dlss::Quality mode = dlss::ModeFromConfig(g_config.dlss_mode);
        const dlss::StereoBatchPlan plan = dlss::PlanStereoBatch(
            static_cast<int>(planOutW), static_cast<int>(planOutH), mode);
        if (!plannedDlss || !runtimePresent || !plan.eyeRenderW ||
            srcDesc.Width != static_cast<UINT>(plan.eyeRenderW) ||
            srcDesc.Height != static_cast<UINT>(plan.eyeRenderH))
            return fallback("waiting for the stereo-batch render size");
        if (state.depthCopyDesc.Width != srcDesc.Width ||
            state.depthCopyDesc.Height != srcDesc.Height)
            return fallback("the stereo-batch eye depth and colour sizes differ");

        const int quality = static_cast<int>(mode);
        DlssFeatureDesc desc{};
        desc.renderWidth = plan.atlasRenderW;
        desc.renderHeight = plan.atlasRenderH;
        desc.outputWidth = plan.atlasOutputW;
        desc.outputHeight = plan.atlasOutputH;
        desc.quality = quality;
        desc.preset = g_config.dlss_preset;
        desc.depthInverted = state.current.projection.depthInverted;
        bool recreated = false;
        char reason[160]{};
        DlssFeatureDesc cached{};
        const bool featureMatches = Dlss_GetFeatureDesc(0, cached) &&
            cached.renderWidth == desc.renderWidth &&
            cached.renderHeight == desc.renderHeight &&
            cached.outputWidth == desc.outputWidth &&
            cached.outputHeight == desc.outputHeight &&
            cached.quality == desc.quality && cached.preset == desc.preset &&
            cached.depthInverted == desc.depthInverted;
        if (!featureMatches)
        {
            uint32_t minW = 0, minH = 0, maxW = 0, maxH = 0;
            uint32_t optW = 0, optH = 0;
            if (!Dlss_QueryRenderRange(plan.atlasOutputW, plan.atlasOutputH,
                                       quality, minW, minH, maxW, maxH,
                                       optW, optH) ||
                !dlss::RenderSizeAdmitted(plan.atlasRenderW,
                                          plan.atlasRenderH,
                                          minW, minH, maxW, maxH))
                return fallback(
                    "the stereo atlas is outside the selected DLSS mode range");
            if (!Dlss_EnsureFeature(0, g_context, desc, recreated, reason,
                                    sizeof(reason)))
                return fallback(reason);
        }
        const bool resourcesRecreated =
            !DlssStereoBatchResourcesMatch(plan, srcDesc.Format);
        if (!EnsureDlssMotionPipeline() ||
            !EnsureDlssStereoBatchResources(plan, srcDesc.Format))
            return fallback("the stereo-batch textures or motion shader are unavailable");

        D3D11_BOX sourceBox{};
        sourceBox.right = srcDesc.Width;
        sourceBox.bottom = srcDesc.Height;
        sourceBox.back = 1;
        g_context->CopySubresourceRegion(g_dlssBatch.color, 0,
            static_cast<UINT>(eye) * srcDesc.Width, 0, 0, src, 0, &sourceBox);

        const bool reset = recreated || resourcesRecreated ||
            state.resetPending ||
            dlss::ShouldReset(state.current, state.previous);
        const dlss::CameraSample& previous = reset ? state.current : state.previous;
        RunDlssMotionPass(state, previous, srcDesc.Width, srcDesc.Height,
                          g_dlssBatch.motionRtv, g_dlssBatch.depthRtv,
                          static_cast<UINT>(eye) * srcDesc.Width);
        g_dlssBatch.eyePrepared[eye] = true;
        g_dlssBatch.reset = g_dlssBatch.reset || reset;
        if (!g_dlssBatch.eyePrepared[0] || !g_dlssBatch.eyePrepared[1])
            return true;

        if (g_dlssEye[0].current.projection.depthInverted !=
            g_dlssEye[1].current.projection.depthInverted)
        {
            g_dlssBatch.eyePrepared[0] =
                g_dlssBatch.eyePrepared[1] = false;
            g_dlssBatch.reset = false;
            return fallback("the two eye depth conventions disagree");
        }

        float leftJitterX = 0.0f, leftJitterY = 0.0f;
        float rightJitterX = 0.0f, rightJitterY = 0.0f;
        dlss::NdcToJitterPixels(g_dlssEye[0].current.jitterNdcX,
            g_dlssEye[0].current.jitterNdcY, srcDesc.Width, srcDesc.Height,
            leftJitterX, leftJitterY);
        dlss::NdcToJitterPixels(g_dlssEye[1].current.jitterNdcX,
            g_dlssEye[1].current.jitterNdcY, srcDesc.Width, srcDesc.Height,
            rightJitterX, rightJitterY);
        if (fabsf(leftJitterX - rightJitterX) > 0.001f ||
            fabsf(leftJitterY - rightJitterY) > 0.001f)
        {
            g_dlssBatch.eyePrepared[0] = g_dlssBatch.eyePrepared[1] = false;
            g_dlssBatch.reset = false;
            return fallback("the two eye jitter samples disagree");
        }

        DlssEvalInputs inputs{};
        inputs.color = g_dlssBatch.color;
        inputs.depth = g_dlssBatch.depth;
        inputs.motion = g_dlssBatch.motion;
        inputs.output = g_dlssBatch.output;
        inputs.jitterPixelX = leftJitterX;
        inputs.jitterPixelY = leftJitterY;
        inputs.frameTimeMs = static_cast<float>(
            static_cast<double>(g_displayPeriodNs.load(std::memory_order_relaxed)) / 1.0e6);
        inputs.reset = g_dlssBatch.reset;
        uint32_t failure = 0;
        if (!Dlss_Evaluate(0, g_context, inputs, failure))
        {
            g_dlssEye[0].resetPending = g_dlssEye[1].resetPending = true;
            g_dlssBatch.eyePrepared[0] = g_dlssBatch.eyePrepared[1] = false;
            g_dlssBatch.reset = false;
            char text[96];
            snprintf(text, sizeof(text), "stereo-batch evaluation failed (0x%08X)", failure);
            return fallback(text);
        }

        D3D11_BOX half{};
        half.right = static_cast<UINT>(plan.eyeOutputW);
        half.bottom = static_cast<UINT>(plan.eyeOutputH);
        half.back = 1;
        for (int outputEye = 0; outputEye < 2; ++outputEye)
        {
            half.left = static_cast<UINT>(outputEye * plan.eyeOutputW);
            half.right = half.left + static_cast<UINT>(plan.eyeOutputW);
            g_context->CopySubresourceRegion(g_dlssBatch.eyeOutput[outputEye], 0,
                0, 0, 0, g_dlssBatch.output, 0, &half);
            DlssKickedEye kicked{};
            kicked.output = g_dlssBatch.eyeOutput[outputEye];
            kicked.outputDesc = g_dlssBatch.eyeOutputDesc;
            g_dlssKicked[outputEye].Publish(g_preparedFrame.serial, kicked);
        }
        if (g_dlssBatch.reset)
            ++g_dlssResets;
        g_dlssEye[0].resetPending = g_dlssEye[1].resetPending = false;
        g_dlssBatch.eyePrepared[0] = g_dlssBatch.eyePrepared[1] = false;
        g_dlssBatch.reset = false;
        g_dlssEyesResolvedThisFrame += 2;
        g_dlssResolvedEyes += 2;
        g_dlssFallbackReason[0] = '\0';
        SetDlssStatus("active: one fresh stereo atlas evaluation per frame");
        return true;
    }

    // Starts one eye's upscale: colour copy, motion pass and the DLSS
    // submission. Called at the end of that eye's render, so the upscale
    // runs on the compute queue while the game rasters the other eye. False
    // means nothing was submitted and the caller keeps the mod's own resolve
    // for this eye and frame. `debugRtv` is only needed for the motion-vector
    // view, which draws straight into the headset image.
    bool DlssKickEye(int eye, ID3D11Texture2D* src,
                     const D3D11_TEXTURE2D_DESC& srcDesc,
                     uint32_t dstW, uint32_t dstH,
                     ID3D11RenderTargetView* debugRtv)
    {
        if (!VR_DlssWantsEyeCamera())
            return false;
        if (!g_config.dlss_debug_view)
        {
            if (g_dlssBatchSuppressed)
                return false;
            unsigned rw = 0, rh = 0, ow = 0, oh = 0;
            bool plannedDlss = false, runtimePresent = false;
            D3D_GetRenderPlan(rw, rh, ow, oh, plannedDlss, runtimePresent);
            if (dlss::kEnableDlss21StereoBatch && plannedDlss)
                return DlssKickStereoBatchEye(eye, src, srcDesc);
        }
        auto fallback = [&](const char* why) {
            NoteDlssFallback(eye, why);
            return false;
        };
        if (eye < 0 || eye > 1 || !src || !g_device || !g_context || !dstW || !dstH)
            return fallback("no device or eye image");
        if (g_config.dlss_debug_view && !debugRtv)
            return false; // the view needs the headset image; submit-time kick
        if (!dlss::UsesWorldRender(DlssPresentationMode()))
        {
            g_dlssJitterArmed = false;
            return false; // Menus are full-resolution, non-temporal canvases.
        }
        {
            int nativeW = 0, nativeH = 0;
            nativeW = kNativeRenderWidth; nativeH = kNativeRenderHeight;
            unsigned rw = 0, rh = 0, ow = 0, oh = 0;
            bool plannedDlss = false, runtimePresent = false;
            D3D_GetRenderPlan(rw, rh, ow, oh, plannedDlss, runtimePresent);
            const auto wanted = dlss::PlanRender(nativeW, nativeH, g_config.resolution_scale,
                g_config.upscaler, g_config.dlss_mode, runtimePresent);
            // The old path allocated DLAA features for the full-size raster
            // during the debounce, then immediately destroyed them on resize.
            if (wanted.dlss && (srcDesc.Width != static_cast<unsigned>(wanted.renderW) ||
                srcDesc.Height != static_cast<unsigned>(wanted.renderH) ||
                ow != static_cast<unsigned>(wanted.outputW) || oh != static_cast<unsigned>(wanted.outputH)))
            {
                g_dlssJitterArmed = false;
                return fallback(D3D_FitActive()
                    ? "waiting for the selected render size"
                    : "world resize unavailable; enable Fit desktop window and restart");
            }
        }
        // Count only admitted-size world attempts; waiting for a requested
        // resize must never trip the feature failure backoff.
        g_dlssBatchAttemptedThisFrame = true;
        if (!Dlss_EnsureInitialized(g_device))
            return fallback(Dlss_StatusText());
        DlssEyeState& state = g_dlssEye[eye];
        if (!state.published || !state.current.valid)
            return fallback("no eye camera arrived for this eye render "
                            "(the title's hook did not publish, or its "
                            "projection was rejected; see the log)");
        if (!state.depthValid || (!state.borrowedCeDepth&&(!state.depthCopy || !state.depthCopySrv)))
            return fallback("no depth buffer was captured for this eye");
        if (!state.borrowedCeDepth&&(state.depthCopyDesc.Width != srcDesc.Width ||
            state.depthCopyDesc.Height != srcDesc.Height))
            return fallback("the eye's depth and colour buffers differ in size");
        if (srcDesc.SampleDesc.Count != 1)
            return fallback("the eye image is multisampled");
        dlss::OutputSize out =
            DlssOutputFor(srcDesc.Width, srcDesc.Height, dstW, dstH);
        if (g_config.dlss_debug_view)
        {
            out.width = srcDesc.Width;
            out.height = srcDesc.Height;
            out.exact = false;
        }
        if (!out.width || !out.height)
            return fallback("no valid DLSS output size");

        const float ratio = static_cast<float>(out.width) / static_cast<float>(srcDesc.Width);
        dlss::Quality order[5];
        const int count = dlss::QualitySearchOrder(dlss::QualityForRatio(ratio), order);
        int quality = -1;
        DlssFeatureDesc cachedFeature{};
        if (Dlss_GetFeatureDesc(eye, cachedFeature) &&
            cachedFeature.renderWidth == srcDesc.Width && cachedFeature.renderHeight == srcDesc.Height &&
            cachedFeature.outputWidth == out.width && cachedFeature.outputHeight == out.height)
            quality = cachedFeature.quality; // shape already admitted; NGX queries belong to resize
        for (int i = 0; i < count && quality < 0; ++i)
        {
            uint32_t minW = 0, minH = 0, maxW = 0, maxH = 0, optW = 0, optH = 0;
            if (!Dlss_QueryRenderRange(out.width, out.height, static_cast<int>(order[i]),
                                       minW, minH, maxW, maxH, optW, optH))
                continue;
            if (dlss::RenderSizeAdmitted(srcDesc.Width, srcDesc.Height, minW, minH, maxW, maxH))
                quality = static_cast<int>(order[i]);
        }
        if (g_config.dlss_debug_view)
            quality = static_cast<int>(dlss::Quality::Dlaa);
        if (quality < 0)
            return fallback("the render size fits no DLSS range for this picture; "
                            "change DLSS mode or Resolution scale and restart");

        DlssFeatureDesc desc{};
        desc.renderWidth = srcDesc.Width;
        desc.renderHeight = srcDesc.Height;
        desc.outputWidth = out.width;
        desc.outputHeight = out.height;
        desc.quality = quality;
        desc.preset = g_config.dlss_preset;
        desc.depthInverted = state.current.projection.depthInverted;
        bool recreated = false;
        char reason[160];
        if (!g_config.dlss_debug_view &&
            !Dlss_EnsureFeature(eye, g_context, desc, recreated, reason, sizeof(reason)))
            return fallback(reason);
        if (!EnsureDlssMotionPipeline())
            return fallback("the motion-vector shader is unavailable");
        if (!EnsureDlssEyeResources(eye, state, srcDesc, out))
            return fallback("DLSS textures could not be created");

        // GPU work from here on. No IQ stamps here: this runs at the end of
        // the eye's render, outside the timed publish; the final stretch in
        // DlssFinishEye is timed exactly like the DLSS-off resolve.
        ID3D11Resource* colorInput = src;
        if (state.colorIn)
        {
            g_context->CopyResource(state.colorIn, src);
            colorInput = state.colorIn;
        }
        const bool reset = recreated || state.resetPending ||
            dlss::ShouldReset(state.current, state.previous);
        const dlss::CameraSample& previous = reset ? state.current : state.previous;
        // The active image-motion backend consumes shared colour directly.
        // This optional copy only belongs to the dormant external flow path.
        const int flowIndex = Dlss_FlowInputIndex(eye);
        const bool flowWritten = flowIndex >= 0 &&
            RunDlssFlowCopyPass(state, flowIndex, srcDesc.Width, srcDesc.Height);
        RunDlssMotionPass(state, previous, srcDesc.Width, srcDesc.Height);

        if (g_config.dlss_debug_view)
        {
            RunDlssDebugPass(state, debugRtv, dstW, dstH);
            g_dlssKicked[eye].Publish(g_preparedFrame.serial, {});
        }
        else
        {
            DlssEvalInputs inputs{};
            inputs.color = colorInput;
            inputs.depth = state.depthOut;
            inputs.motion = state.motion;
            inputs.output = state.output;
            dlss::NdcToJitterPixels(state.current.jitterNdcX, state.current.jitterNdcY,
                                    srcDesc.Width, srcDesc.Height,
                                    inputs.jitterPixelX, inputs.jitterPixelY);
            if (!reset)
                dlss::JitterDeltaPixels(state.current, previous, srcDesc.Width, srcDesc.Height,
                                        inputs.jitterDeltaX, inputs.jitterDeltaY);
            inputs.flowInputWritten = flowWritten;
            inputs.frameTimeMs = static_cast<float>(
                static_cast<double>(g_displayPeriodNs.load(std::memory_order_relaxed)) / 1.0e6);
            inputs.reset = reset;
            uint32_t failure = 0;
            if (!Dlss_Evaluate(eye, g_context, inputs, failure))
            {
                state.resetPending = true;
                char text[96];
                snprintf(text, sizeof(text), "DLSS evaluation failed (0x%08X)", failure);
                return fallback(text);
            }
            g_dlssKicked[eye].Publish(g_preparedFrame.serial,
                {state.output, state.outputDesc});
        }
        if (reset)
            ++g_dlssResets;
        state.resetPending = false;
        ++g_dlssEyesResolvedThisFrame;
        ++g_dlssResolvedEyes;
        g_dlssFallbackReason[0] = '\0';

        const uint64_t nowMs = GetTickCount64();
        if (eye == 1 && nowMs - g_dlssStatusMs >= 2000)
        {
            g_dlssStatusMs = nowMs;
            char text[256];
            const bool jitterApplied = state.current.jitterNdcX != 0.0f ||
                state.current.jitterNdcY != 0.0f;
            snprintf(text, sizeof(text),
                     "active: %ux%u -> %ux%u (%s slot, %s), jitter %s, depth %s, "
                     "motion %s, resets %llu, fallbacks %llu%s",
                     srcDesc.Width, srcDesc.Height, out.width, out.height,
                     dlss::QualityName(static_cast<dlss::Quality>(quality)),
                     dlss::PresetName(g_config.dlss_preset),
                     !g_config.dlss_jitter ? "off"
                     : (!g_dlssJitterArmed ? "arming"
                        : (jitterApplied ? "on" : "not written by this title")),
                     desc.depthInverted ? "reversed" : "standard",
                     Dlss_HasImageMotion(eye) ? "image+camera" : "camera only",
                     static_cast<unsigned long long>(g_dlssResets),
                     static_cast<unsigned long long>(g_dlssFallbackEyes),
                     g_config.dlss_debug_view ? " [motion-vector view]" : "");
            SetDlssStatus(text);
        }
        return true;
    }

    // A completed eye belongs to the source eye, independent of whether its
    // consumer is immersive projection or a theatre screen. Consume it once.
    bool DlssTakeEyeOutput(int eye, DlssKickedEye& kicked)
    {
        if (eye < 0 || eye > 1 ||
            !g_dlssKicked[eye].Take(g_preparedFrame.serial, kicked))
            return false;
        if (kicked.output)
        {
            IqTimerDlssWait(eye, false);
            Dlss_SyncD3D11(g_context, eye);
            IqTimerDlssWait(eye, true);
        }
        return true;
    }

    bool SameStereoBatchPlan(const dlss::StereoBatchPlan& a,
                             const dlss::StereoBatchPlan& b)
    {
        return a.eyeRenderW == b.eyeRenderW && a.eyeRenderH == b.eyeRenderH &&
            a.atlasRenderW == b.atlasRenderW && a.atlasRenderH == b.atlasRenderH &&
            a.atlasOutputW == b.atlasOutputW && a.atlasOutputH == b.atlasOutputH;
    }

    bool DlssStereoBatchResourcesMatch(const dlss::StereoBatchPlan& plan,
                                       DXGI_FORMAT sourceFormat)
    {
        return g_dlssBatch.color && g_dlssBatch.motion && g_dlssBatch.depth &&
            g_dlssBatch.output && g_dlssBatch.eyeOutput[0] &&
            g_dlssBatch.eyeOutput[1] && g_dlssBatch.motionRtv &&
            g_dlssBatch.depthRtv &&
            SameStereoBatchPlan(g_dlssBatch.plan, plan) &&
            g_dlssBatch.inputDesc.Format == UnormSibling(sourceFormat);
    }

    bool EnsureDlssStereoBatchResources(const dlss::StereoBatchPlan& plan,
                                        DXGI_FORMAT sourceFormat)
    {
        if (DlssStereoBatchResourcesMatch(plan, sourceFormat))
            return true;

        ReleaseDlssStereoBatch();
        D3D11_TEXTURE2D_DESC input{};
        input.Width = static_cast<UINT>(plan.atlasRenderW);
        input.Height = static_cast<UINT>(plan.atlasRenderH);
        input.MipLevels = input.ArraySize = input.SampleDesc.Count = 1;
        input.Usage = D3D11_USAGE_DEFAULT;
        input.Format = UnormSibling(sourceFormat);
        input.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bool ok = SUCCEEDED(g_device->CreateTexture2D(
            &input, nullptr, &g_dlssBatch.color));

        D3D11_TEXTURE2D_DESC field = input;
        field.Format = DXGI_FORMAT_R16G16_FLOAT;
        field.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_UNORDERED_ACCESS;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(
            &field, nullptr, &g_dlssBatch.motion)) &&
            SUCCEEDED(g_device->CreateRenderTargetView(
                g_dlssBatch.motion, nullptr, &g_dlssBatch.motionRtv));
        field.Format = DXGI_FORMAT_R32_FLOAT;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(
            &field, nullptr, &g_dlssBatch.depth)) &&
            SUCCEEDED(g_device->CreateRenderTargetView(
                g_dlssBatch.depth, nullptr, &g_dlssBatch.depthRtv));

        D3D11_TEXTURE2D_DESC output = input;
        output.Width = static_cast<UINT>(plan.atlasOutputW);
        output.Height = static_cast<UINT>(plan.atlasOutputH);
        output.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        output.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_UNORDERED_ACCESS;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(
            &output, nullptr, &g_dlssBatch.output));
        D3D11_TEXTURE2D_DESC eyeOutput = output;
        eyeOutput.Width = static_cast<UINT>(plan.eyeOutputW);
        eyeOutput.Height = static_cast<UINT>(plan.eyeOutputH);
        eyeOutput.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(
            &eyeOutput, nullptr, &g_dlssBatch.eyeOutput[0])) &&
            SUCCEEDED(g_device->CreateTexture2D(
                &eyeOutput, nullptr, &g_dlssBatch.eyeOutput[1]));
        if (!ok)
        {
            ReleaseDlssStereoBatch();
            return false;
        }
        g_dlssBatch.inputDesc = input;
        g_dlssBatch.outputDesc = output;
        g_dlssBatch.eyeOutputDesc = eyeOutput;
        g_dlssBatch.plan = plan;
        const double bytes = static_cast<double>(plan.atlasRenderW) * plan.atlasRenderH * 16.0 +
            static_cast<double>(plan.atlasOutputW) * plan.atlasOutputH * 8.0;
        LOG("DLSS: stereo batch resources: two %dx%d eyes -> one %dx%d atlas -> "
            "two %dx%d reconstructed halves; %.1f MiB before NGX",
            plan.eyeRenderW, plan.eyeRenderH, plan.atlasRenderW, plan.atlasRenderH,
            plan.eyeOutputW, plan.eyeOutputH, bytes / (1024.0 * 1024.0));
        return true;
    }

    // At present: stretches one eye's finished upscale into the headset
    // image, after one GPU wait for that eye's evaluation (the first eye's
    // is normally long complete; the second's finishes while the first is
    // stretched). False means no upscale is in flight for this eye.
    bool DlssFinishEye(int eye, ID3D11Texture2D* dst, uint32_t dstW, uint32_t dstH,
                       ID3D11RenderTargetView* dstRtv)
    {
        DlssKickedEye kicked{};
        if (!DlssTakeEyeOutput(eye, kicked))
            return false;
        if (!kicked.output)
            return true; // the motion-vector view already drew into dst
        if (!dst || !dstRtv || !dstW || !dstH)
            return false;
        // The same resolve as DLSS-off (linear, then the Sharpening slider).
        if (!BlitImageQuality(kicked.output, kicked.outputDesc, dst, dstW, dstH, dstRtv,
            true))
        {
            LOG("DLSS ERROR: the finished DLSS image could not be copied into "
                "the headset image");
            return false;
        }
        return true;
    }

    // Programming Guide 3.5: while an eye is rasterised for DLSS, the game's
    // texture samplers carry the mip bias for the headset picture, so the
    // textures arrive as sharp as a native render and DLSS reconstructs
    // detail instead of sharpening blur. Cleared before the mod's own passes.
    void DlssBeginEyeSamplerBias()
    {
        // DLSS-18 was rejected for cost/smear. Disable its newly armed
        // sampler substitution before the next experiment; keep the dormant
        // implementation for a separately measured texture-quality change.
        if (!dlss_sampler::kEnabled)
            return;
        if (g_config.upscaler != dlss::kUpscalerDlss || !Dlss_IsAvailable())
        {
            D3D_SetEyeSamplerBias(0.0f);
            return;
        }
        unsigned renderW = 0, renderH = 0, outputW = 0, outputH = 0;
        bool dlss = false, runtimePresent = false;
        D3D_GetRenderPlan(renderW, renderH, outputW, outputH, dlss, runtimePresent);
        D3D_SetEyeSamplerBias(dlss ? dlss::MipBias(renderW, outputW) : 0.0f);
    }

    void DlssEndEyeSamplerBias()
    {
        D3D_SetEyeSamplerBias(0.0f);
    }

    // The eye-end kick. Every title except Reach publishes its eye into
    // g_eyeCache by the time VR_EndRasterEye runs (that copy exists so the
    // other eye cannot overwrite it), so the upscale can start right here.
    // Reach's eye capture is read at present; it kicks there instead.
    void DlssKickEyeAtEyeEnd(int eye)
    {
        if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera() ||
            g_config.dlss_debug_view)
            return;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
            return;
        if (!g_eyeCache[eye] || !g_eyeCacheDesc.Width || !g_eyeCacheDesc.Height ||
            !g_stereoW || !g_stereoH)
            return;
        DlssKickEye(eye, g_eyeCache[eye], g_eyeCacheDesc, g_stereoW, g_stereoH, nullptr);
    }

    // Every Present exit, including flat/no-render/failed submissions. Retire
    // results and advance history only for a completed, consumed eye pair.
    void DlssEndFrame()
    {
        static int lastDisabledReason = -1;
        const bool outputsConsumed =
            !g_dlssKicked[0].Ready(g_preparedFrame.serial) &&
            !g_dlssKicked[1].Ready(g_preparedFrame.serial);
        g_dlssKicked[0].Reset();
        g_dlssKicked[1].Reset();
        g_dlssBatch.eyePrepared[0] = g_dlssBatch.eyePrepared[1] = false;
        g_dlssBatch.reset = false;
        if (!VR_DlssWantsEyeCamera())
        {
            g_dlssBatchAttemptedThisFrame = false;
            g_dlssBatchFailedFrames = 0;
            g_dlssBatchSuppressed = false;
            if (g_dlssEye[0].output || g_dlssEye[1].output || g_dlssSceneDepthDsv ||
                g_dlssDepthViewCount)
            {
                ReleaseDlssResources();
                LOG("DLSS: disabled or optional runtime absent; resources released");
            }
            g_dlssJitterArmed = false;
            g_dlssEyesResolvedThisFrame = 0;
            const int disabledReason = g_config.upscaler != dlss::kUpscalerDlss ? 0 :
                (!dlss::TitleHasCameraContract(TitleAdapter_GetActiveTitle()) ? 2 : 1);
            if (disabledReason != lastDisabledReason)
            {
                lastDisabledReason = disabledReason;
                SetDlssStatus(disabledReason == 2
                    ? "not running: this title has no verified DLSS camera adapter; full-size VR resolve"
                    : disabledReason == 1
                        ? "unavailable: optional nvngx_dlss.dll missing; normal VR resolve"
                        : "off");
            }
            return;
        }
        lastDisabledReason = -1;
        if (g_dlssBatchAttemptedThisFrame && !g_config.dlss_debug_view)
        {
            if (g_dlssEyesResolvedThisFrame == 2)
            {
                g_dlssBatchFailedFrames = 0;
            }
            else if (!g_dlssBatchSuppressed && ++g_dlssBatchFailedFrames >= 16)
            {
                g_dlssBatchSuppressed = true;
                ReleaseDlssResources();
                LOG("DLSS: eye resolve failed for 16 consecutive admitted "
                    "frames; releasing it and requesting the full-resolution "
                    "stock resolve (change a DLSS setting to retry)");
                SetDlssStatus("not running: persistent eye-resolve failure; "
                    "returning to full resolution (change a DLSS setting to retry)");
            }
        }
        g_dlssBatchAttemptedThisFrame = false;
        if (g_config.dlss_debug_view && g_dlssBatch.output)
        {
            Dlss_ReleaseFeatures();
            ReleaseDlssStereoBatch();
            LOG("DLSS: stereo batch released while the motion diagnostic is active");
        }
        g_dlssJitterArmed = outputsConsumed && g_dlssEyesResolvedThisFrame == 2;
        g_dlssEyesResolvedThisFrame = 0;
        for (DlssEyeState& state : g_dlssEye)
        {
            if (g_dlssJitterArmed && state.published && state.current.valid)
                state.previous = state.current;
            else
                state.resetPending = true;
            state.published = false;
            state.depthValid = false;
        }
        ++g_dlssJitterPhase;
    }

    // (The HUD "capture-diff panel" machinery that lived here — per-eye pre-HUD
    // snapshots, ps_huddiff extraction, union blend, head-locked panel quad,
    // native-HUD erase — was headset-DISPROVEN 2026-07-19: the diff carried only
    // the objective text, the rest of the HUD vanished, and the capture copies
    // cost real GPU time every frame. Removed at the user's direction. The HUD
    // ships native and full-size; only the reticle element is hidden via the
    // verified 0x2EDF24 element hook. See docs/RE-notes.md HUD dead ends.)

    void ReleaseTheaterResolvedResources()
    {
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_theaterResolvedSrv[eye])
                g_theaterResolvedSrv[eye]->Release();
            if (g_theaterResolvedRtv[eye])
                g_theaterResolvedRtv[eye]->Release();
            if (g_theaterResolved[eye])
                g_theaterResolved[eye]->Release();
            g_theaterResolvedSrv[eye] = nullptr;
            g_theaterResolvedRtv[eye] = nullptr;
            g_theaterResolved[eye] = nullptr;
        }
        g_theaterResolvedDesc = {};
    }

    void ReleaseTheaterSubtitleBand()
    {
        if (g_theaterSubtitleBandSrv)
            g_theaterSubtitleBandSrv->Release();
        if (g_theaterSubtitleBand)
            g_theaterSubtitleBand->Release();
        g_theaterSubtitleBandSrv = nullptr;
        g_theaterSubtitleBand = nullptr;
        g_theaterSubtitleBandDesc = {};
        g_theaterSubtitleBandStartV = -1.0f;
    }

    // Copy the lower band of the title's finished backbuffer. The band shares
    // the backbuffer's format and covers full width, so its U is the screen's U
    // and the text keeps the horizontal position the title gave it.
    bool CaptureTheaterSubtitleBand(
        ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& backbufferDesc)
    {
        if (!g_device || !g_context || !backbuffer ||
            !backbufferDesc.Width || !backbufferDesc.Height ||
            // A multisampled backbuffer cannot be region-copied; the theatre
            // keeps its stereo image and this feature stays off.
            backbufferDesc.SampleDesc.Count != 1)
            return false;
        const float bandFraction = std::clamp(
            g_config.cutscene_theater_subtitle_band, 0.05f, 1.0f);
        const UINT bandHeight = static_cast<UINT>(
            static_cast<float>(backbufferDesc.Height) * bandFraction);
        if (!bandHeight || bandHeight > backbufferDesc.Height)
            return false;
        if (g_theaterSubtitleBandDesc.Width != backbufferDesc.Width ||
            g_theaterSubtitleBandDesc.Height != bandHeight ||
            g_theaterSubtitleBandDesc.Format != backbufferDesc.Format)
        {
            ReleaseTheaterSubtitleBand();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = backbufferDesc.Width;
            desc.Height = bandHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            // CopySubresourceRegion needs an identical format, so the band is
            // created from the backbuffer's own. A typeless backbuffer would
            // fail the view below and disable only this feature.
            desc.Format = backbufferDesc.Format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = g_device->CreateTexture2D(
                &desc, nullptr, &g_theaterSubtitleBand);
            if (SUCCEEDED(hr))
                hr = g_device->CreateShaderResourceView(
                    g_theaterSubtitleBand, nullptr, &g_theaterSubtitleBandSrv);
            if (FAILED(hr))
            {
                LOG("cutscene theatre: subtitle band unavailable "
                    "(HRESULT 0x%08X); the theatre keeps its stereo image and "
                    "the title's subtitles stay on the monitor only",
                    static_cast<unsigned>(hr));
                ReleaseTheaterSubtitleBand();
                return false;
            }
            g_theaterSubtitleBandDesc = desc;
            LOG("cutscene theatre: subtitle band armed (%ux%u from the bottom "
                "%.0f%% of the %ux%u backbuffer)%s",
                desc.Width, desc.Height, bandFraction * 100.0f,
                backbufferDesc.Width, backbufferDesc.Height,
                g_config.cutscene_theater_subtitle_debug
                    ? "; DIAGNOSTIC view on - the strip is painted unfiltered"
                    : "");
        }
        D3D11_BOX box{};
        box.left = 0;
        box.right = backbufferDesc.Width;
        box.top = backbufferDesc.Height - bandHeight;
        box.bottom = backbufferDesc.Height;
        box.front = 0;
        box.back = 1;
        g_context->CopySubresourceRegion(
            g_theaterSubtitleBand, 0, 0, 0, 0, backbuffer, 0, &box);
        g_theaterSubtitleBandStartV = 1.0f - bandFraction;
        return true;
    }

    void ReleaseTheaterProjectionResources()
    {
        ReleaseTheaterResolvedResources();
        ReleaseTheaterSubtitleBand();
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_theaterDirectSourceSrv[eye])
                g_theaterDirectSourceSrv[eye]->Release();
            g_theaterDirectSourceSrv[eye] = nullptr;
            g_theaterDirectSourceKey[eye] = nullptr;
        }
        auto release = [](auto*& object) {
            if (object) object->Release();
            object = nullptr;
        };
        release(g_theaterProjectionVs);
        release(g_theaterProjectionPs);
        release(g_theaterProjectionCb);
    }

    bool TheaterProjectionCanSampleDirectly(
        const D3D11_TEXTURE2D_DESC& sourceDesc) noexcept
    {
        return g_config.upscale_filter == 0 && g_config.aa_mode == 0 &&
            g_config.sharpness <= 0.001f &&
            (sourceDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
            sourceDesc.SampleDesc.Count == 1;
    }

    bool EnsureTheaterProjectionResources(
        uint32_t width, uint32_t height, bool needResolved)
    {
        if (!width || !height || !g_device || !EnsureBlitPipeline())
            return false;
        if (!g_theaterProjectionVs || !g_theaterProjectionPs ||
            !g_theaterProjectionCb)
        {
            static const char* shader = R"(
Texture2D srcTex : register(t0);
Texture2D bandTex : register(t1);
SamplerState smp : register(s0);
cbuffer TheaterProjectionParams : register(b0)
{
    float4 clipPositions[4];
    float4 colorParams;
    float4 matteParams;
    float4 bandParams;
}
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID)
{
    VSOut o; o.pos=clipPositions[id];
    o.uv=float2(id & 1, (id >> 1) & 1); return o;
}
float lin(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float enc(float c)
{
    c = max(c, 0.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0/2.4) - 0.055;
}
float3 recode(float3 c)
{
    if (colorParams.z < 0.5)
    {
        if (colorParams.x < 0.5)
            c = float3(lin(c.r), lin(c.g), lin(c.b));
        if (colorParams.y > 0.5)
            c = float3(enc(c.r), enc(c.g), enc(c.b));
    }
    return c;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 ps_main(VSOut i) : SV_Target
{
    // Cine bars: the strips of the authored frame a monitor never shows are
    // opaque black. Subtitles still draw over them, exactly as on a monitor.
    float3 outColor = 0.0;
    bool inBar = matteParams.z > 0.5 &&
        (i.uv.y < matteParams.x || i.uv.y > matteParams.y);
    if (!inBar)
        outColor = recode(srcTex.SampleLevel(smp, i.uv, 0).rgb);
    if (matteParams.w >= 0.0 && i.uv.y >= matteParams.w)
    {
        // The band came from the same frame at the same U, so the text lands
        // where the title put it. Take only bright, near-neutral pixels that
        // are also clearly brighter than what the stereo image already shows
        // there: that is glyph over background, and nothing else in a frame
        // satisfies all three at once.
        float2 bandUv = float2(
            i.uv.x, (i.uv.y - matteParams.w) / max(1.0 - matteParams.w, 0.0001));
        float3 b = recode(bandTex.SampleLevel(smp, bandUv, 0).rgb);
        // Diagnostic: show the captured strip exactly as it arrived, so a
        // headset report can say whether the text is in it at all, and where.
        if (bandParams.x > 0.5)
            return float4(b, 1.0);
        float bandLuma = luma(b);
        float chroma = max(max(b.r, b.g), b.b) - min(min(b.r, b.g), b.b);
        float key = saturate((bandLuma - 0.72) / 0.28) *
                    saturate((0.16 - chroma) / 0.16) *
                    saturate((bandLuma - luma(outColor)) * 4.0);
        outColor = lerp(outColor, b, key);
    }
    return float4(outColor, 1.0);
}
)";
            ID3DBlob* errors = nullptr;
            auto compile = [&](const char* entry, const char* target) {
                ID3DBlob* blob = nullptr;
                const HRESULT result = D3DCompile(
                    shader, strlen(shader), nullptr, nullptr, nullptr,
                    entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                    &blob, &errors);
                if (FAILED(result))
                    LOG("theatre projection shader '%s' failed: %s", entry,
                        errors ? static_cast<const char*>(
                            errors->GetBufferPointer()) : "no compiler text");
                if (errors) { errors->Release(); errors = nullptr; }
                return blob;
            };
            ID3DBlob* blob = compile("vs_main", "vs_5_0");
            HRESULT hr = blob ? g_device->CreateVertexShader(
                blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                &g_theaterProjectionVs) : E_FAIL;
            if (blob) blob->Release();
            blob = SUCCEEDED(hr) ? compile("ps_main", "ps_5_0") : nullptr;
            if (blob)
            {
                hr = g_device->CreatePixelShader(
                    blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                    &g_theaterProjectionPs);
                blob->Release();
            }
            D3D11_BUFFER_DESC cb{};
            cb.ByteWidth = sizeof(TheaterProjectionParams);
            cb.Usage = D3D11_USAGE_DEFAULT;
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (SUCCEEDED(hr))
                hr = g_device->CreateBuffer(&cb, nullptr, &g_theaterProjectionCb);
            if (FAILED(hr))
            {
                LOG("theatre projection pipeline unavailable (HRESULT 0x%08X)",
                    static_cast<unsigned>(hr));
                ReleaseTheaterProjectionResources();
                return false;
            }
        }

        if (!needResolved)
        {
            ReleaseTheaterResolvedResources();
            return true;
        }
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(g_xrFormat);
        if (g_theaterResolvedDesc.Width != width ||
            g_theaterResolvedDesc.Height != height ||
            g_theaterResolvedDesc.Format != format)
        {
            ReleaseTheaterResolvedResources();
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=1;
        desc.ArraySize=1; desc.Format=format; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_theaterResolved[eye] && g_theaterResolvedRtv[eye] &&
                g_theaterResolvedSrv[eye])
                continue;
            HRESULT hr = g_device->CreateTexture2D(
                &desc, nullptr, &g_theaterResolved[eye]);
            if (SUCCEEDED(hr)) hr = g_device->CreateRenderTargetView(
                g_theaterResolved[eye], nullptr, &g_theaterResolvedRtv[eye]);
            if (SUCCEEDED(hr)) hr = g_device->CreateShaderResourceView(
                g_theaterResolved[eye], nullptr, &g_theaterResolvedSrv[eye]);
            if (FAILED(hr))
            {
                LOG("theatre projection eye %d target unavailable "
                    "(HRESULT 0x%08X)", eye, static_cast<unsigned>(hr));
                ReleaseTheaterProjectionResources();
                return false;
            }
        }
        g_theaterResolvedDesc = desc;
        return true;
    }

    ID3D11ShaderResourceView* EnsureTheaterDirectSourceSrv(
        uint32_t sourceEye, ID3D11Texture2D* source)
    {
        if (sourceEye > 1 || !source)
            return nullptr;
        if (g_theaterDirectSourceKey[sourceEye] != source)
        {
            if (g_theaterDirectSourceSrv[sourceEye])
                g_theaterDirectSourceSrv[sourceEye]->Release();
            g_theaterDirectSourceSrv[sourceEye] = nullptr;
            g_theaterDirectSourceKey[sourceEye] = nullptr;
            if (FAILED(g_device->CreateShaderResourceView(
                    source, nullptr, &g_theaterDirectSourceSrv[sourceEye])))
            {
                return nullptr;
            }
            g_theaterDirectSourceKey[sourceEye] = source;
        }
        return g_theaterDirectSourceSrv[sourceEye];
    }

    bool CompositeTheaterProjectionEye(
        uint32_t targetEye, ID3D11ShaderResourceView* sourceSrv,
        const D3D11_TEXTURE2D_DESC& sourceDesc, bool sourceAlreadyXrEncoded,
        const XrView& nativeView, ID3D11RenderTargetView* targetRtv)
    {
        if (targetEye > 1 || !sourceSrv || !targetRtv ||
            !g_theaterProjectionVs || !g_theaterProjectionPs ||
            !g_theaterProjectionCb)
            return false;
        const float width = g_config.cutscene_theater_width_m;
        const float height = CutsceneTheaterHeightFromAspect(
            width, g_cutsceneTheaterAuthoredAspect);
        const XrVector3f offset = Rotate(
            g_centerRot, {0.0f, 0.0f,
                          -g_config.cutscene_theater_distance_m});
        const float eyePosition[3]{
            nativeView.pose.position.x, nativeView.pose.position.y,
            nativeView.pose.position.z};
        const float eyeOrientation[4]{
            nativeView.pose.orientation.x, nativeView.pose.orientation.y,
            nativeView.pose.orientation.z, nativeView.pose.orientation.w};
        const float screenCenter[3]{
            g_centerPos.x+offset.x, g_centerPos.y+offset.y,
            g_centerPos.z+offset.z};
        const float screenOrientation[4]{
            g_centerRot.x, g_centerRot.y, g_centerRot.z, g_centerRot.w};
        const float fov[4]{
            nativeView.fov.angleLeft, nativeView.fov.angleRight,
            nativeView.fov.angleUp, nativeView.fov.angleDown};
        CutsceneTheaterClipVertex vertices[4]{};
        if (!BuildCutsceneTheaterProjectionQuad(
                eyePosition, eyeOrientation, screenCenter, screenOrientation,
                width, height, fov, vertices))
            return false;

        TheaterProjectionParams params{};
        for (int i = 0; i < 4; ++i)
        {
            params.clipPositions[i][0]=vertices[i].x;
            params.clipPositions[i][1]=vertices[i].y;
            params.clipPositions[i][2]=vertices[i].w*0.5f;
            params.clipPositions[i][3]=vertices[i].w;
        }
        params.color[0] = IsSrgb(sourceDesc.Format) ? 1.0f : 0.0f;
        params.color[1] = IsSrgb(static_cast<DXGI_FORMAT>(g_xrFormat))
            ? 0.0f : 1.0f;
        params.color[2] = sourceAlreadyXrEncoded ? 1.0f : 0.0f;
        const CutsceneTheaterMatte matte = ComputeCutsceneTheaterMatte(
            g_cutsceneTheaterAuthoredAspect,
            g_config.cutscene_theater_matte_aspect,
            g_config.cutscene_theater_matte_offset);
        params.matte[0] = matte.vMin;
        params.matte[1] = matte.vMax;
        params.matte[2] = matte.active ? 1.0f : 0.0f;
        const bool bandReady = !g_nativeTheatreCaptionReady && g_theaterSubtitleBandSrv &&
            g_theaterSubtitleBandStartV >= 0.0f;
        params.matte[3] = bandReady ? g_theaterSubtitleBandStartV : -1.0f;
        params.band[0] = bandReady && g_config.cutscene_theater_subtitle_debug
            ? 1.0f : 0.0f;
        g_context->UpdateSubresource(
            g_theaterProjectionCb, 0, nullptr, &params, 0, 0);

        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedVsCb0 = nullptr;
        ID3D11Buffer* savedPsCb0 = nullptr;
        // D3DStateBackup restores pixel shader resource slot 0 only, and the
        // subtitle band binds slot 1. Return that slot to the title itself.
        ID3D11ShaderResourceView* savedPsSrv1 = nullptr;
        g_context->VSGetConstantBuffers(0, 1, &savedVsCb0);
        g_context->PSGetConstantBuffers(0, 1, &savedPsCb0);
        g_context->PSGetShaderResources(1, 1, &savedPsSrv1);
        const float black[4]{0,0,0,1};
        g_context->ClearRenderTargetView(targetRtv, black);
        g_context->OMSetRenderTargets(1, &targetRtv, nullptr);
        D3D11_VIEWPORT viewport{
            0.0f, 0.0f, static_cast<float>(g_stereoW),
            static_cast<float>(g_stereoH), 0.0f, 1.0f};
        g_context->RSSetViewports(1, &viewport);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff,0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_context->VSSetShader(g_theaterProjectionVs,nullptr,0);
        g_context->VSSetConstantBuffers(0,1,&g_theaterProjectionCb);
        g_context->GSSetShader(nullptr,nullptr,0);
        g_context->PSSetShader(g_theaterProjectionPs,nullptr,0);
        g_context->PSSetConstantBuffers(0,1,&g_theaterProjectionCb);
        ID3D11ShaderResourceView* projectionSrvs[2]{
            sourceSrv, bandReady ? g_theaterSubtitleBandSrv : nullptr};
        g_context->PSSetShaderResources(0,2,projectionSrvs);
        g_context->PSSetSamplers(0,1,&g_blitSampler);
        g_context->Draw(4,0);
        backup.Restore(g_context);
        g_context->VSSetConstantBuffers(0,1,&savedVsCb0);
        g_context->PSSetConstantBuffers(0,1,&savedPsCb0);
        g_context->PSSetShaderResources(1,1,&savedPsSrv1);
        if (savedVsCb0) savedVsCb0->Release();
        if (savedPsCb0) savedPsCb0->Release();
        if (savedPsSrv1) savedPsSrv1->Release();
        static bool logged = false;
        if (!logged && targetEye == 1)
        {
            logged = true;
            LOG("cutscene theatre: native-FOV two-view projection active; "
                "no eye-selective layers or private theatre swapchain");
        }
        return true;
    }
    // ------------------------------------------------------- XR swapchains

    void DestroyChain(XrSwapchain& chain, std::vector<ID3D11Texture2D*>& images,
                      std::vector<ID3D11RenderTargetView*>& rtvs)
    {
        for (auto* rtv : rtvs)
            if (rtv) rtv->Release();
        rtvs.clear();
        images.clear(); // owned by the runtime, not AddRef'd
        if (chain != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(chain);
            chain = XR_NULL_HANDLE;
        }
    }

    bool CreateChain(uint32_t w, uint32_t h, XrSwapchain& chain,
                     std::vector<ID3D11Texture2D*>& images, std::vector<ID3D11RenderTargetView*>& rtvs,
                     const char* what)
    {
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                        XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format = g_xrFormat;
        ci.sampleCount = 1;
        ci.width = w;
        ci.height = h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        XrResult r = xrCreateSwapchain(g_session, &ci, &chain);
        if (XR_FAILED(r))
        {
            LOG("xrCreateSwapchain(%s, %ux%u) failed: %s", what, w, h, XrStr(r));
            return false;
        }
        uint32_t count = 0;
        r=xrEnumerateSwapchainImages(chain,0,&count,nullptr);
        if (XR_FAILED(r) || !count)
        {
            LOG("xrEnumerateSwapchainImages(%s) count failed: %s",what,XrStr(r));
            DestroyChain(chain,images,rtvs);
            return false;
        }
        std::vector<XrSwapchainImageD3D11KHR> xrImages(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        r = xrEnumerateSwapchainImages(chain, count, &count,
                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data()));
        if (XR_FAILED(r))
        {
            LOG("xrEnumerateSwapchainImages(%s) failed: %s", what, XrStr(r));
            DestroyChain(chain,images,rtvs);
            return false;
        }
        images.clear();
        rtvs.assign(count, nullptr);
        for (auto& img : xrImages)
            images.push_back(img.texture);
        LOG("XR swapchain '%s' created: %ux%u, %u images", what, w, h, count);
        return true;
    }

    #include "vr_swapchain_rtv.inl"

    void ReleaseScopeCache()
    {
        if (g_scopeCacheSrv) { g_scopeCacheSrv->Release(); g_scopeCacheSrv = nullptr; }
        if (g_scopeCacheRtv) { g_scopeCacheRtv->Release(); g_scopeCacheRtv = nullptr; }
        if (g_scopeCache) { g_scopeCache->Release(); g_scopeCache = nullptr; }
        g_scopeCacheDesc = {};
        g_scopeHasImage.store(false);
    }

    bool EnsureScopeCache(const D3D11_TEXTURE2D_DESC& sourceDesc)
    {
        if (!g_device || !sourceDesc.Width || !sourceDesc.Height || sourceDesc.SampleDesc.Count!=1)
            return false;
        if (g_scopeCache && g_scopeCacheRtv && g_scopeCacheSrv &&
            g_scopeCacheDesc.Width == sourceDesc.Width &&
            g_scopeCacheDesc.Height == sourceDesc.Height &&
            g_scopeCacheDesc.Format == sourceDesc.Format)
            return true;
        ReleaseScopeCache();
        D3D11_TEXTURE2D_DESC desc = sourceDesc;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &g_scopeCache);
        if (SUCCEEDED(hr))
            hr = g_device->CreateRenderTargetView(g_scopeCache, nullptr, &g_scopeCacheRtv);
        if (SUCCEEDED(hr))
            hr = g_device->CreateShaderResourceView(g_scopeCache, nullptr, &g_scopeCacheSrv);
        if (FAILED(hr))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope cache creation failed: HRESULT 0x%08X (%ux%u format %d)",
                    (unsigned)hr, desc.Width, desc.Height, (int)desc.Format);
            }
            ReleaseScopeCache();
            return false;
        }
        g_scopeCacheDesc = desc;
        LOG("scope private render cache created: %ux%u format %d",
            desc.Width, desc.Height, (int)desc.Format);
        return true;
    }

    bool EnsureScopeUploadPipeline()
    {
        if (g_scopeUploadVs && g_scopeUploadPs && g_scopeUploadPsLinearize &&
            g_scopeUploadSampler && g_scopeUploadRasterizer && g_scopeUploadDepthOff)
            return true;
        static bool failed = false;
        if (failed || !g_device) return false;
        static const char* source = R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID) {
    VSOut o; float2 uv=float2((id<<1)&2,id&2);
    o.pos=float4(uv*float2(2,-2)+float2(-1,1),0,1); o.uv=uv; return o;
}
float lin(float c) { return c<=0.04045 ? c/12.92 : pow((c+0.055)/1.055,2.4); }
float4 paint(float2 uv, bool decode) {
    uint sw,sh; srcTex.GetDimensions(sw,sh);
    float sa=(float)sw/max(1.0,(float)sh), da=1.0;
    float2 scale=sa>da ? float2(da/sa,1) : float2(1,sa/da);
    float3 rgb=srcTex.Sample(smp,0.5+(uv-0.5)*scale).rgb;
    if(decode) rgb=float3(lin(rgb.r),lin(rgb.g),lin(rgb.b));
    float2 px=abs((uv-0.5)*1024);
    float outer=max((1-step(3,px.x))*(1-step(16,px.y)),
                    (1-step(3,px.y))*(1-step(16,px.x)));
    float inner=max((1-step(1.2,px.x))*(1-step(12,px.y)),
                    (1-step(1.2,px.y))*(1-step(12,px.x)));
    rgb=lerp(rgb,float3(0,0,0),outer);
    rgb=lerp(rgb,float3(0.35,1,0.35),inner);
    // Analytic antialiasing gives a clean circular lens on the alpha quad.
    float radius=length((uv-0.5)*2);
    float aa=max(fwidth(radius),1.0/1024.0);
    float alpha=1-smoothstep(0.985-aa,0.985+aa,radius);
    float rim=smoothstep(0.973-aa,0.973+aa,radius);
    rgb=lerp(rgb,float3(0.025,0.025,0.025),rim);
    return float4(rgb*alpha,alpha);
}
float4 ps_scope(VSOut i):SV_Target { return paint(i.uv,false); }
float4 ps_scope_linearize(VSOut i):SV_Target { return paint(i.uv,true); }
)";
        HRESULT hr = S_OK;
        auto compile = [&](const char* entry, const char* target)->ID3DBlob* {
            ID3DBlob *blob=nullptr,*errors=nullptr;
            hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,entry,target,0,0,&blob,&errors);
            if(FAILED(hr)) LOG("scope shader %s failed: HRESULT 0x%08X: %s",entry,
                (unsigned)hr,errors?(const char*)errors->GetBufferPointer():"no compiler text");
            if(errors) errors->Release(); return blob;
        };
        ID3DBlob* blob=compile("vs_main","vs_5_0");
        if(blob) { hr=g_device->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),
                                                   nullptr,&g_scopeUploadVs); blob->Release(); }
        if(SUCCEEDED(hr)) blob=compile("ps_scope","ps_5_0");
        if(SUCCEEDED(hr) && blob) { hr=g_device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),
                                                            nullptr,&g_scopeUploadPs); blob->Release(); }
        if(SUCCEEDED(hr)) blob=compile("ps_scope_linearize","ps_5_0");
        if(SUCCEEDED(hr) && blob) { hr=g_device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),
                                                            nullptr,&g_scopeUploadPsLinearize); blob->Release(); }
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        if(SUCCEEDED(hr)) hr=g_device->CreateSamplerState(&sampler,&g_scopeUploadSampler);
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID;
        raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        if(SUCCEEDED(hr)) hr=g_device->CreateRasterizerState(&raster,&g_scopeUploadRasterizer);
        D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=FALSE;
        if(SUCCEEDED(hr)) hr=g_device->CreateDepthStencilState(&depth,&g_scopeUploadDepthOff);
        if(FAILED(hr))
        {
            failed=true;
            LOG("scope isolated upload pipeline failed: HRESULT 0x%08X",(unsigned)hr);
            auto release=[](auto*& p){if(p)p->Release();p=nullptr;};
            release(g_scopeUploadVs); release(g_scopeUploadPs); release(g_scopeUploadPsLinearize);
            release(g_scopeUploadSampler); release(g_scopeUploadRasterizer); release(g_scopeUploadDepthOff);
            return false;
        }
        return true;
    }

    bool UploadScopeToRtv(ID3D11RenderTargetView* rtv)
    {
        if(!rtv || !EnsureScopeUploadPipeline()) return false;
        const bool linearize=!IsSrgb(g_scopeCacheDesc.Format) &&
                             IsSrgb((DXGI_FORMAT)g_xrFormat);
        ID3D11PixelShader* ps=linearize?g_scopeUploadPsLinearize:g_scopeUploadPs;
        D3DStateBackup backup; backup.Capture(g_context);
        g_context->OMSetRenderTargets(1,&rtv,nullptr);
        D3D11_VIEWPORT viewport{0,0,(float)kScopeScreenWidth,
                                (float)kScopeScreenHeight,0,1};
        g_context->RSSetViewports(1,&viewport);
        g_context->RSSetState(g_scopeUploadRasterizer);
        g_context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_scopeUploadDepthOff,0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_scopeUploadVs,nullptr,0);
        g_context->GSSetShader(nullptr,nullptr,0);
        g_context->PSSetShader(ps,nullptr,0);
        g_context->PSSetShaderResources(0,1,&g_scopeCacheSrv);
        g_context->PSSetSamplers(0,1,&g_scopeUploadSampler);
        g_context->Draw(3,0);
        backup.Restore(g_context);
        return true;
    }

    bool PrepareScopeImageDelivery()
    {
        static bool creationFailed = false;
        if (creationFailed || !g_scopeHasImage.load() ||
            g_scopeImageTitle!=TitleAdapter_GetActiveTitle()||
            g_scopeImageGeneration!=TitleAdapter_GetGeneration(g_scopeImageTitle)||!g_scopeCache ||
            !g_scopeCacheSrv || !EnsureScopeUploadPipeline())
            return false;
        if (g_scopeScreenChain == XR_NULL_HANDLE &&
            !CreateChain(kScopeScreenWidth, kScopeScreenHeight, g_scopeScreenChain,
                         g_scopeScreenImages, g_scopeScreenRtvs, "scope 4:3 screen"))
        {
            creationFailed = true;
            return false;
        }

        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

        XrResult result = xrAcquireSwapchainImage(g_scopeScreenChain, &acquire, &index);
        if (XR_FAILED(result))
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("scope image acquire failed: %s", XrStr(result)); }
            return false;
        }
        result = xrWaitSwapchainImage(g_scopeScreenChain, &wait);
        if (XR_FAILED(result))
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("scope image wait failed: %s", XrStr(result)); }
            xrReleaseSwapchainImage(g_scopeScreenChain, &release);
            return false;
        }

        ID3D11RenderTargetView* rtv = nullptr;
        if (index < g_scopeScreenImages.size() && index < g_scopeScreenRtvs.size())
        {
            rtv = g_scopeScreenRtvs[index];
            if (!rtv)
            {
                D3D11_RENDER_TARGET_VIEW_DESC desc{};
                desc.Format = (DXGI_FORMAT)g_xrFormat;
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = 0;
                const HRESULT hr = g_device->CreateRenderTargetView(
                    g_scopeScreenImages[index], &desc, &g_scopeScreenRtvs[index]);
                if (FAILED(hr))
                {
                    static bool logged = false;
                    if (!logged)
                    {
                        logged = true;
                        LOG("scope image RTV creation failed: HRESULT 0x%08X format %d",
                            (unsigned)hr, (int)g_xrFormat);
                    }
                }
                rtv = g_scopeScreenRtvs[index];
            }
        }

        const bool uploaded = UploadScopeToRtv(rtv);
        if (!uploaded)
        {
            static bool logged = false;
            if (!logged) { logged=true; LOG("scope image upload failed: no valid output RTV"); }
        }

        const XrResult releaseResult = xrReleaseSwapchainImage(g_scopeScreenChain, &release);
        if (XR_FAILED(releaseResult))
        {
            static bool logged = false;
            if (!logged) { logged = true; LOG("scope image release failed: %s", XrStr(releaseResult)); }
            return false;
        }
        return uploaded;
    }

    bool EnsureScreenChain(uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0) // game momentarily has a 0x0 backbuffer (e.g. intro video / mode switch)
            return false;
        if (g_screenChain != XR_NULL_HANDLE && g_screenW == w && g_screenH == h)
            return true;
        DestroyChain(g_screenChain, g_screenImages, g_screenRtvs);
        if (!CreateChain(w, h, g_screenChain, g_screenImages, g_screenRtvs, "screen"))
            return false;
        g_screenW = w;
        g_screenH = h;
        g_status.gameWidth = w;
        g_status.gameHeight = h;
        return true;
    }

    bool CreateEyeChains(uint32_t testWidth = 0, uint32_t testHeight = 0)
    {
        if (g_viewConfigs.size() != 2)
        {
            LOG("M2: expected 2 stereo views, runtime reported %u; eye targets disabled",
                (unsigned)g_viewConfigs.size());
            return false;
        }

        g_eyeChains.resize(g_viewConfigs.size());
        for (uint32_t i = 0; i < (uint32_t)g_eyeChains.size(); ++i)
        {
            EyeChain& eye = g_eyeChains[i];
            // During bring-up, allow the known-good game backbuffer shape to
            // isolate a SteamVR/D3D11 issue with the recommended near-square
            // eye size from a multiple-swapchain issue.
            eye.width = testWidth ? testWidth : g_viewConfigs[i].recommendedImageRectWidth;
            eye.height = testHeight ? testHeight : g_viewConfigs[i].recommendedImageRectHeight;
            char name[32];
            snprintf(name, sizeof(name), "eye %u", i);
            if (!CreateChain(eye.width, eye.height, eye.chain, eye.images, eye.rtvs, name))
            {
                for (EyeChain& made : g_eyeChains)
                    DestroyChain(made.chain, made.images, made.rtvs);
                g_eyeChains.clear();
                return false;
            }
        }
        LOG("M2: stereo eye swapchains ready (projection submission held until per-eye rendering is ready)");
        return true;
    }

    bool CreateStereoArrayChain()
    {
        if (g_viewConfigs.size() != 2)
            return false;
        g_stereoW = g_viewConfigs[0].recommendedImageRectWidth;
        g_stereoH = g_viewConfigs[0].recommendedImageRectHeight;
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format = g_xrFormat;
        ci.sampleCount = 1;
        ci.width = g_stereoW;
        ci.height = g_stereoH;
        ci.faceCount = 1;
        ci.arraySize = 2;
        ci.mipCount = 1;
        XrResult r = xrCreateSwapchain(g_session, &ci, &g_stereoChain);
        if (XR_FAILED(r))
        {
            LOG("M2: xrCreateSwapchain(stereo array) failed: %s", XrStr(r));
            return false;
        }
        uint32_t count = 0;
        xrEnumerateSwapchainImages(g_stereoChain, 0, &count, nullptr);
        std::vector<XrSwapchainImageD3D11KHR> xrImages(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        r = xrEnumerateSwapchainImages(g_stereoChain, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data()));
        if (XR_FAILED(r))
            return false;
        g_stereoImages.clear();
        for (auto& image : xrImages)
            g_stereoImages.push_back(image.texture);
        g_stereoRtvs.resize(count);
        for (auto& pair : g_stereoRtvs)
            pair = {nullptr, nullptr};
        LOG("M2: stereo 2-slice array swapchain created: %ux%u, %u images",
            g_stereoW, g_stereoH, count);
        return true;
    }

    ID3D11RenderTargetView* GetStereoRtv(uint32_t image, uint32_t eye)
    {
        if (image >= g_stereoImages.size() || eye >= 2)
            return nullptr;
        if (!g_stereoRtvs[image][eye])
        {
            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = (DXGI_FORMAT)g_xrFormat;
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = 0;
            desc.Texture2DArray.FirstArraySlice = eye;
            desc.Texture2DArray.ArraySize = 1;
            if (FAILED(g_device->CreateRenderTargetView(g_stereoImages[image], &desc,
                                                        &g_stereoRtvs[image][eye])))
                return nullptr;
        }
        return g_stereoRtvs[image][eye];
    }

    bool EnsureEyeCaches(const D3D11_TEXTURE2D_DESC& source)
    {
        const bool nativeDlss = VR_DlssWantsEyeCamera() &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo3 &&
            source.SampleDesc.Count == 1 &&
            (source.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
             source.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
        if (g_eyeCache[0] && g_eyeCache[1] && g_eyeCacheRtvs[0] &&
            g_eyeCacheRtvs[1] &&
            g_eyeCacheDesc.Width == source.Width &&
            g_eyeCacheDesc.Height == source.Height &&
            g_eyeCacheDesc.Format == source.Format && g_eyeCacheDlssRequested == nativeDlss)
            return true;
#if HALOMCCVR_HALO2_STEREO6DOF
        // Revoke admission before any pointer is released. A new nonzero epoch
        // prevents pointer reuse after resize from validating an old eye stamp.
        VR_ResetHalo2SynchronousStereo();
        g_halo2SynchronousResourceEpoch.fetch_add(
            1, std::memory_order_acq_rel);
#endif
        for (auto*& texture : g_eyeCache)
        {
            // Drop the cached source view first. Ours is a strong reference, so
            // without this the old eye texture would survive the Release below
            // and stay pinned at its old resolution.
            ForgetSourceView(texture);
            if (texture) texture->Release();
            texture = nullptr;
        }
        for (auto*& rtv : g_eyeCacheRtvs)
        {
            if (rtv) rtv->Release();
            rtv = nullptr;
        }
        D3D11_TEXTURE2D_DESC desc = source;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = source.Format;
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (nativeDlss)
        {
            desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }
        auto create = [&](bool direct) {
            for (int eye = 0; eye < 2; ++eye)
            {
                if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_eyeCache[eye])) ||
                    FAILED(g_device->CreateRenderTargetView(g_eyeCache[eye],
                        direct ? &view : nullptr, &g_eyeCacheRtvs[eye]))) return false;
            }
            return true;
        };
        bool direct = nativeDlss;
        bool created = create(direct);
        if (!created && direct)
        {
            for (int eye = 0; eye < 2; ++eye)
            {
                if (g_eyeCacheRtvs[eye]) g_eyeCacheRtvs[eye]->Release();
                if (g_eyeCache[eye]) g_eyeCache[eye]->Release();
                g_eyeCacheRtvs[eye] = nullptr; g_eyeCache[eye] = nullptr;
            }
            desc.Format = source.Format;
            desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
            direct = false;
            created = create(false);
            LOG("DLSS: direct eye allocation unavailable; using the ordinary eye cache and colour copy");
        }
        if (!created)
        {
            LOG("M2: failed to create persistent eye frame caches");
            return false;
        }
        g_eyeCacheDesc = desc;
        g_eyeCacheDesc.Format = source.Format;
        g_eyeCacheNativeDlss = direct;
        g_eyeCacheDlssRequested = nativeDlss;
        g_eyeHasImage[0] = g_eyeHasImage[1] = false;
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
        g_halo2PendingSerial.store(0, std::memory_order_release);
        for (int eye = 0; eye < 2; ++eye)
        {
            g_halo2EyeSerial[eye].store(0, std::memory_order_release);
            g_halo2EyeGeneration[eye].store(0, std::memory_order_release);
        }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
        g_halo4EyeSerial[0].store(0, std::memory_order_release);
        g_halo4EyeSerial[1].store(0, std::memory_order_release);
#endif
        g_stereoValidationDone = false;
        LOG("M2: persistent eye frame caches created: %ux%u", desc.Width, desc.Height);
        return true;
    }

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    template <typename T>
    class ReachComRef
    {
    public:
        ReachComRef() = default;
        ~ReachComRef() { Reset(); }
        ReachComRef(const ReachComRef&) = delete;
        ReachComRef& operator=(const ReachComRef&) = delete;

        T* Get() const noexcept { return m_value; }
        T* Detach() noexcept
        {
            T* value = m_value;
            m_value = nullptr;
            return value;
        }
        T** Put() noexcept
        {
            Reset();
            return &m_value;
        }
        void Reset() noexcept
        {
            if (m_value)
            {
                m_value->Release();
                m_value = nullptr;
            }
        }

    private:
        T* m_value = nullptr;
    };

    enum class ReachDisplayFailure : uint8_t
    {
        None = 0,
        StaticPreflight,
        ModuleReference,
        EngineFields,
        SwapchainContract,
        EyeAllocation,
        DeviceContext,
        Publication,
    };

    const char* ReachDisplayFailureName(ReachDisplayFailure failure)
    {
        switch (failure)
        {
        case ReachDisplayFailure::None: return "none";
        case ReachDisplayFailure::StaticPreflight: return "static-preflight";
        case ReachDisplayFailure::ModuleReference: return "module-reference";
        case ReachDisplayFailure::EngineFields: return "engine-fields";
        case ReachDisplayFailure::SwapchainContract: return "swapchain-contract";
        case ReachDisplayFailure::EyeAllocation: return "eye-allocation";
        case ReachDisplayFailure::DeviceContext: return "device-context";
        case ReachDisplayFailure::Publication: return "resource-publication";
        default: return "unknown";
        }
    }

    class ReachModuleReference
    {
    public:
        ~ReachModuleReference() = default;

        bool Acquire(uintptr_t moduleBase) noexcept
        {
            if (m_module || !moduleBase)
                return false;
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(moduleBase), &module) ||
                reinterpret_cast<uintptr_t>(module) != moduleBase)
            {
                return false;
            }
            m_module = module;
            return true;
        }

    private:
        HMODULE m_module = nullptr;
    };

    class ReachExclusiveResourceLock
    {
    public:
        explicit ReachExclusiveResourceLock(SRWLOCK& lock) noexcept
            : m_lock(&lock)
        {
            AcquireSRWLockExclusive(m_lock);
        }
        ~ReachExclusiveResourceLock()
        {
            ReleaseSRWLockExclusive(m_lock);
        }
        ReachExclusiveResourceLock(const ReachExclusiveResourceLock&) = delete;
        ReachExclusiveResourceLock& operator=(
            const ReachExclusiveResourceLock&) = delete;

    private:
        PSRWLOCK m_lock;
    };

#include "reach_display_admission.inl"

    void BumpReachDisplayLifecycleSerial() noexcept
    {
        uint64_t current = g_reachDisplayLifecycleSerial.load(
            std::memory_order_relaxed);
        while (current != std::numeric_limits<uint64_t>::max() &&
               !g_reachDisplayLifecycleSerial.compare_exchange_weak(
                   current, current + 1,
                   std::memory_order_release,
                   std::memory_order_relaxed))
        {
        }
    }

    void InvalidateReachPresentAdmission() noexcept
    {
        g_reachPresentSelectedEligible.store(false, std::memory_order_release);
        g_reachPresentAvailabilitySetEpochMs.store(0, std::memory_order_release);
        g_reachPresentGeneration.store(0, std::memory_order_release);
        g_reachPresentModuleBase.store(0, std::memory_order_release);
        BumpReachDisplayLifecycleSerial();
    }

    void CaptureReachDisplaySnapshot(
        IDXGISwapChain* presentSwapchain,
        const ReachDisplayAdmission& admission) noexcept;

    // Present contributes only a throttled safe-boundary snapshot with retained
    // swapchain-owned interfaces and descriptors. Hashing, allocation,
    // publication, and logging stay on WaitThread.
    void ObserveReachPresentSwapchain(IDXGISwapChain* presentSwapchain) noexcept
    {
        ReachDisplayAdmission admission{};
        const bool eligible =
            ReadReachDisplayAdmission(admission) && admission.selected;
        const uint64_t availabilitySetEpochMs = eligible
            ? admission.availability.availabilitySetEpochMs : 0;
        const uint32_t generation = eligible
            ? admission.epoch.generation : 0;
        const uintptr_t moduleBase = eligible
            ? admission.epoch.moduleBase : 0;
        const bool changed =
            g_reachPresentSelectedEligible.load(
                std::memory_order_relaxed) != eligible ||
            g_reachPresentAvailabilitySetEpochMs.load(
                std::memory_order_relaxed) != availabilitySetEpochMs ||
            g_reachPresentGeneration.load(
                std::memory_order_relaxed) != generation ||
            g_reachPresentModuleBase.load(
                std::memory_order_relaxed) != moduleBase;
        if (changed)
        {
            g_reachPresentSelectedEligible.store(
                eligible, std::memory_order_release);
            g_reachPresentAvailabilitySetEpochMs.store(
                availabilitySetEpochMs, std::memory_order_release);
            g_reachPresentGeneration.store(
                generation, std::memory_order_release);
            g_reachPresentModuleBase.store(
                moduleBase, std::memory_order_release);
            BumpReachDisplayLifecycleSerial();
        }
        if (!eligible || !presentSwapchain)
            return;
        CaptureReachDisplaySnapshot(presentSwapchain, admission);
    }

    void ReleaseReachEyeCaches() noexcept
    {
        for (auto*& cache : g_reachEyeCache)
        {
            if (cache)
                cache->Release();
            cache = nullptr;
        }
    }

    bool InvalidateReachCaptureResources() noexcept
    {
        g_reachCaptureEnabled.store(false, std::memory_order_seq_cst);
        for (unsigned spin = 0;
             g_reachCaptureUsers.load(std::memory_order_seq_cst) != 0 &&
             spin < 2000; ++spin)
        {
            Sleep(1);
        }
        if (g_reachCaptureUsers.load(std::memory_order_seq_cst) != 0)
            return false;
        if (g_reachCaptureSource)
            g_reachCaptureSource->Release();
        for (auto*& eye : g_reachCaptureEyes)
        {
            if (eye)
                eye->Release();
            eye = nullptr;
        }
        if (g_reachCaptureContext)
            g_reachCaptureContext->Release();
        g_reachCaptureSource = nullptr;
        g_reachCaptureContext = nullptr;
        g_reachCaptureProof = {};
        g_reachCaptureDesc = {};
        g_reachEyeSerial[0].store(0, std::memory_order_release);
        g_reachEyeSerial[1].store(0, std::memory_order_release);
        return true;
    }

    template <typename T>
    bool ReadReachLocal(uintptr_t address, T& value) noexcept
    {
        if (!address || sizeof(T) >
                std::numeric_limits<uintptr_t>::max() - address)
            return false;
        SIZE_T received = 0;
        return ReadProcessMemory(
                   GetCurrentProcess(),
                   reinterpret_cast<const void*>(address),
                   &value, sizeof(value), &received) &&
            received == sizeof(value);
    }

    uintptr_t ReachComIdentity(IUnknown* value) noexcept
    {
        if (!value)
            return 0;
        IUnknown* identity = nullptr;
        const HRESULT hr = value->QueryInterface(
            __uuidof(IUnknown), reinterpret_cast<void**>(&identity));
        const uintptr_t result = SUCCEEDED(hr) && identity
            ? reinterpret_cast<uintptr_t>(identity)
            : 0;
        if (identity)
            identity->Release();
        return result;
    }

    uintptr_t ReachResourceDeviceIdentity(
        ID3D11Resource* resource) noexcept
    {
        if (!resource)
            return 0;
        ReachComRef<ID3D11Device> device;
        resource->GetDevice(device.Put());
        return ReachComIdentity(device.Get());
    }

    ReachCopyShape ReachShape(
        const D3D11_TEXTURE2D_DESC& desc) noexcept
    {
        return {
            desc.Width,
            desc.Height,
            desc.MipLevels,
            desc.ArraySize,
            static_cast<uint32_t>(desc.Format),
            desc.SampleDesc.Count,
            desc.SampleDesc.Quality,
        };
    }

    // WaitThread only. Preserve the exact allocation rejection stage without
    // putting logging or device queries in the eye/render hooks.
    void NoteReachEyeAllocationFailure(
        const char* stage, HRESULT result, ID3D11Device* device,
        const D3D11_TEXTURE2D_DESC& desc) noexcept
    {
        static uint64_t lastReportMs = 0;
        static HRESULT lastResult = S_OK;
        static const char* lastStage = nullptr;
        const uint64_t now = GetTickCount64();
        if (stage == lastStage && result == lastResult &&
            now - lastReportMs < 5000)
            return;
        lastReportMs = now;
        lastStage = stage;
        lastResult = result;
        const HRESULT removed = device ? device->GetDeviceRemovedReason() : E_POINTER;
        LOG("Reach eye-allocation: stage=%s hr=0x%08X removed=0x%08X "
            "size=%ux%u fmt=%u samples=%u/%u mips=%u array=%u "
            "usage=%u bind=0x%X cpu=0x%X misc=0x%X",
            stage, static_cast<unsigned>(result), static_cast<unsigned>(removed),
            desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
            desc.SampleDesc.Count, desc.SampleDesc.Quality, desc.MipLevels,
            desc.ArraySize, static_cast<unsigned>(desc.Usage), desc.BindFlags,
            desc.CPUAccessFlags, desc.MiscFlags);
    }

    bool EnsureReachEyeCaches(
        ID3D11Device* device,
        const D3D11_TEXTURE2D_DESC& sourceDesc,
        const ReachCopyShape& sourceShape) noexcept
    {
        const uintptr_t deviceIdentity = ReachComIdentity(device);
        bool reusable = deviceIdentity != 0;
        uintptr_t existingIdentities[2]{};
        for (size_t eye = 0; eye < 2 && reusable; ++eye)
        {
            if (!g_reachEyeCache[eye])
            {
                reusable = false;
                break;
            }
            D3D11_TEXTURE2D_DESC eyeDesc{};
            g_reachEyeCache[eye]->GetDesc(&eyeDesc);
            existingIdentities[eye] =
                ReachComIdentity(g_reachEyeCache[eye]);
            reusable = existingIdentities[eye] != 0 &&
                ReachSameCopyShape(sourceShape, ReachShape(eyeDesc)) &&
                ReachResourceDeviceIdentity(g_reachEyeCache[eye]) ==
                    deviceIdentity;
        }
        if (reusable &&
            existingIdentities[0] != existingIdentities[1])
        {
            return true;
        }

        D3D11_TEXTURE2D_DESC cacheDesc = sourceDesc;
        cacheDesc.Usage = D3D11_USAGE_DEFAULT;
        cacheDesc.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        cacheDesc.CPUAccessFlags = 0;
        cacheDesc.MiscFlags = 0;
        cacheDesc.MipLevels = 1;
        cacheDesc.ArraySize = 1;
        ReachComRef<ID3D11Texture2D> replacement[2];
        if (!device)
        {
            NoteReachEyeAllocationFailure("device-missing", E_POINTER, device, cacheDesc);
            return false;
        }
        for (int eye = 0; eye < 2; ++eye)
        {
            const HRESULT created = device->CreateTexture2D(
                &cacheDesc, nullptr, replacement[eye].Put());
            if (FAILED(created) || !replacement[eye].Get())
            {
                NoteReachEyeAllocationFailure(eye ? "create-right" : "create-left",
                    FAILED(created) ? created : E_POINTER, device, cacheDesc);
                return false;
            }
        }

        const uintptr_t replacementIdentities[2] = {
            ReachComIdentity(replacement[0].Get()),
            ReachComIdentity(replacement[1].Get())};
        D3D11_TEXTURE2D_DESC replacementDesc[2]{};
        replacement[0].Get()->GetDesc(&replacementDesc[0]);
        replacement[1].Get()->GetDesc(&replacementDesc[1]);
        if (!replacementIdentities[0] || !replacementIdentities[1] ||
            replacementIdentities[0] == replacementIdentities[1] ||
            !ReachSameCopyShape(
                sourceShape, ReachShape(replacementDesc[0])) ||
            !ReachSameCopyShape(
                sourceShape, ReachShape(replacementDesc[1])) ||
            ReachResourceDeviceIdentity(replacement[0].Get()) !=
                deviceIdentity ||
            ReachResourceDeviceIdentity(replacement[1].Get()) !=
                deviceIdentity)
        {
            NoteReachEyeAllocationFailure("replacement-identity-shape-device",
                E_FAIL, device, cacheDesc);
            return false;
        }

        // Commit only after both replacements pass every check. Reach never
        // releases or mutates Halo 3/ODST's shared g_eyeCache resources.
        ReleaseReachEyeCaches();
        g_reachEyeCache[0] = replacement[0].Detach();
        g_reachEyeCache[1] = replacement[1].Detach();
        return true;
    }

    struct ReachDisplayFields
    {
        IDXGISwapChain* engineSwapchain = nullptr;
        uint32_t specializationCount = 0;
        uintptr_t surfaceArray = 0;
        ID3D11RenderTargetView* record0Rtv = nullptr;
        ID3D11ShaderResourceView* record0Srv = nullptr;
        ID3D11RenderTargetView* selectedRtv = nullptr;
        uint32_t selectedSpecialization = 0;
    };

    bool ReadReachDisplayFields(
        const ReachModuleEpoch& epoch,
        ReachDisplayFields& fields) noexcept
    {
        if (!ReachModuleEpochValid(epoch))
            return false;
        const uintptr_t group = epoch.moduleBase + kReachDisplayGroupRva;
        if (!ReadReachLocal(
                epoch.moduleBase + kReachDisplaySwapchainRva,
                fields.engineSwapchain) ||
            !ReadReachLocal(
                group + kReachDisplaySurfaceCountOffset,
                fields.specializationCount) ||
            !ReadReachLocal(
                group + kReachDisplaySurfaceArrayOffset,
                fields.surfaceArray) ||
            !fields.surfaceArray ||
            fields.surfaceArray >
                std::numeric_limits<uintptr_t>::max() -
                    kReachDisplaySurfaceSrvOffset ||
            !ReadReachLocal(
                fields.surfaceArray + kReachDisplaySurfaceRtvOffset,
                fields.record0Rtv) ||
            !ReadReachLocal(
                fields.surfaceArray + kReachDisplaySurfaceSrvOffset,
                fields.record0Srv) ||
            !ReadReachLocal(
                epoch.moduleBase + kReachDisplaySelectedRtvRva,
                fields.selectedRtv) ||
            !ReadReachLocal(
                epoch.moduleBase + kReachSelectedSpecializationRva,
                fields.selectedSpecialization))
        {
            return false;
        }
        return fields.engineSwapchain && fields.record0Rtv &&
            fields.record0Srv && fields.selectedRtv;
    }

    bool ReachPresentMatchesEngineSwapchain(
        const ReachModuleEpoch& epoch,
        IDXGISwapChain* presentSwapchain) noexcept
    {
        if (!ReachModuleEpochValid(epoch) || !presentSwapchain)
            return false;
        IDXGISwapChain* engineSwapchain = nullptr;
        return ReadReachLocal(
                   epoch.moduleBase + kReachDisplaySwapchainRva,
                   engineSwapchain) &&
            engineSwapchain == presentSwapchain;
    }

    enum class ReachDisplaySnapshotState : uint8_t
    {
        Free = 0,
        Writing,
        Ready,
        Reading,
    };

    struct ReachDisplaySnapshot
    {
        std::atomic<uint8_t> state{
            static_cast<uint8_t>(ReachDisplaySnapshotState::Free)};
        ReachModuleEpoch epoch{};
        uint64_t availabilitySetEpochMs = 0;
        uint64_t lifecycleSerial = 0;
        ReachDisplayFields fields{};
        ID3D11Texture2D* buffer0 = nullptr;
        ID3D11Device* device = nullptr;
        D3D11_TEXTURE2D_DESC buffer0Desc{};
        DXGI_SWAP_CHAIN_DESC swapchainDesc{};
        uintptr_t buffer0Identity = 0;
        uintptr_t deviceIdentity = 0;
        uintptr_t immediateContextIdentity = 0;
        uint32_t deviceCreationFlags = 0;
    };

    constexpr size_t kReachDisplaySnapshotCapacity = 3;
    std::array<ReachDisplaySnapshot,
               kReachDisplaySnapshotCapacity> g_reachDisplaySnapshots{};

    bool ReachSameDisplayFields(
        const ReachDisplayFields& left,
        const ReachDisplayFields& right) noexcept
    {
        return left.engineSwapchain == right.engineSwapchain &&
            left.specializationCount == right.specializationCount &&
            left.surfaceArray == right.surfaceArray &&
            left.record0Rtv == right.record0Rtv &&
            left.record0Srv == right.record0Srv &&
            left.selectedRtv == right.selectedRtv &&
            left.selectedSpecialization == right.selectedSpecialization;
    }

    void ReleaseReachDisplaySnapshot(
        ReachDisplaySnapshot& snapshot) noexcept
    {
        if (snapshot.device)
            snapshot.device->Release();
        if (snapshot.buffer0)
            snapshot.buffer0->Release();
        if (snapshot.fields.engineSwapchain)
            snapshot.fields.engineSwapchain->Release();
        snapshot.epoch = {};
        snapshot.availabilitySetEpochMs = 0;
        snapshot.lifecycleSerial = 0;
        snapshot.fields = {};
        snapshot.buffer0 = nullptr;
        snapshot.device = nullptr;
        snapshot.buffer0Desc = {};
        snapshot.swapchainDesc = {};
        snapshot.buffer0Identity = 0;
        snapshot.deviceIdentity = 0;
        snapshot.immediateContextIdentity = 0;
        snapshot.deviceCreationFlags = 0;
        snapshot.state.store(
            static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
            std::memory_order_release);
    }

    class ReachDisplaySnapshotLease
    {
    public:
        ~ReachDisplaySnapshotLease()
        {
            if (m_snapshot)
                ReleaseReachDisplaySnapshot(*m_snapshot);
        }

        bool Acquire() noexcept
        {
            if (m_snapshot)
                return false;
            for (auto& snapshot : g_reachDisplaySnapshots)
            {
                uint8_t expected =
                    static_cast<uint8_t>(ReachDisplaySnapshotState::Ready);
                if (snapshot.state.compare_exchange_strong(
                        expected,
                        static_cast<uint8_t>(
                            ReachDisplaySnapshotState::Reading),
                        std::memory_order_acquire,
                        std::memory_order_relaxed))
                {
                    m_snapshot = &snapshot;
                    return true;
                }
            }
            return false;
        }

        const ReachDisplaySnapshot* Get() const noexcept
        {
            return m_snapshot;
        }

    private:
        ReachDisplaySnapshot* m_snapshot = nullptr;
    };

    void DiscardReachDisplaySnapshots(bool waitForWriters) noexcept
    {
        for (auto& snapshot : g_reachDisplaySnapshots)
        {
            uint8_t state = snapshot.state.load(std::memory_order_acquire);
            if (waitForWriters)
            {
                unsigned spins = 0;
                while (state == static_cast<uint8_t>(
                                    ReachDisplaySnapshotState::Writing))
                {
                    if (++spins < 1024)
                        YieldProcessor();
                    else
                        SwitchToThread();
                    state = snapshot.state.load(std::memory_order_acquire);
                }
            }
            uint8_t expected =
                static_cast<uint8_t>(ReachDisplaySnapshotState::Ready);
            if (snapshot.state.compare_exchange_strong(
                    expected,
                    static_cast<uint8_t>(
                        ReachDisplaySnapshotState::Reading),
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                ReleaseReachDisplaySnapshot(snapshot);
            }
        }
    }

    void CaptureReachDisplaySnapshot(
        IDXGISwapChain* presentSwapchain,
        const ReachDisplayAdmission& admission) noexcept
    {
        if (!presentSwapchain || !admission.selected ||
            g_reachResizeActive.load(std::memory_order_acquire))
        {
            return;
        }

        // An overlay/video Present must never consume the Reach snapshot
        // throttle. This is a raw identity read only; the full stable-boundary
        // snapshot and COM retention still happen after the exact match.
        if (!ReachPresentMatchesEngineSwapchain(
                admission.epoch, presentSwapchain))
        {
            return;
        }

        const uint64_t now = GetTickCount64();
        uint64_t next = g_reachPresentNextSnapshotMs.load(
            std::memory_order_relaxed);
        if (now < next ||
            !g_reachPresentNextSnapshotMs.compare_exchange_strong(
                next, now + 250,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return;
        }

        ReachDisplaySnapshot* destination = nullptr;
        for (auto& snapshot : g_reachDisplaySnapshots)
        {
            uint8_t expected =
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free);
            if (snapshot.state.compare_exchange_strong(
                    expected,
                    static_cast<uint8_t>(
                        ReachDisplaySnapshotState::Writing),
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                destination = &snapshot;
                break;
            }
        }
        if (!destination)
            return;
        if (g_reachResizeActive.load(std::memory_order_acquire))
        {
            destination->state.store(
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
                std::memory_order_release);
            return;
        }

        const uint64_t lifecycleSerial =
            g_reachDisplayLifecycleSerial.load(
                std::memory_order_acquire);
        ReachDisplayFields initial{};
        const bool initialReady =
            lifecycleSerial != 0 &&
            lifecycleSerial != std::numeric_limits<uint64_t>::max() &&
            ReadReachDisplayFields(admission.epoch, initial) &&
            initial.engineSwapchain == presentSwapchain &&
            initial.specializationCount == kReachDisplaySurfaceCount &&
            initial.selectedSpecialization == 0 &&
            initial.selectedRtv == initial.record0Rtv;
        if (!initialReady)
        {
            destination->state.store(
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
                std::memory_order_release);
            return;
        }

        // Capture only interfaces whose lifetime is owned by this live Present
        // receiver. Raw engine RTV/SRV values remain structural identities and
        // are never dereferenced across whole-rasterizer recovery.
        ReachComRef<ID3D11Texture2D> buffer0;
        ReachComRef<ID3D11Device> device;
        ReachComRef<ID3D11DeviceContext> immediateContext;
        DXGI_SWAP_CHAIN_DESC swapchainDesc{};
        D3D11_TEXTURE2D_DESC buffer0Desc{};
        const bool d3dReady =
            SUCCEEDED(presentSwapchain->GetBuffer(
                0, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(buffer0.Put()))) &&
            buffer0.Get() &&
            SUCCEEDED(presentSwapchain->GetDevice(
                __uuidof(ID3D11Device),
                reinterpret_cast<void**>(device.Put()))) &&
            device.Get() &&
            SUCCEEDED(presentSwapchain->GetDesc(&swapchainDesc));
        if (!d3dReady)
        {
            destination->state.store(
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
                std::memory_order_release);
            return;
        }
        const uint32_t deviceCreationFlags = device.Get()->GetCreationFlags();
        if (deviceCreationFlags & D3D11_CREATE_DEVICE_SINGLETHREADED)
        {
            destination->state.store(
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
                std::memory_order_release);
            return;
        }
        buffer0.Get()->GetDesc(&buffer0Desc);
        device.Get()->GetImmediateContext(immediateContext.Put());
        const uintptr_t buffer0Identity = ReachComIdentity(buffer0.Get());
        const uintptr_t deviceIdentity = ReachComIdentity(device.Get());
        const uintptr_t immediateContextIdentity =
            ReachComIdentity(immediateContext.Get());
        if (!buffer0Identity || !deviceIdentity ||
            !immediateContextIdentity)
        {
            destination->state.store(
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
                std::memory_order_release);
            return;
        }

        ReachDisplayFields final{};
        const bool finalReady =
            !g_reachResizeActive.load(std::memory_order_acquire) &&
            ReadReachDisplayFields(admission.epoch, final) &&
            ReachSameDisplayFields(initial, final) &&
            g_reachDisplayLifecycleSerial.load(
                std::memory_order_acquire) == lifecycleSerial &&
            g_reachPresentSelectedEligible.load(
                std::memory_order_acquire) &&
            g_reachPresentAvailabilitySetEpochMs.load(
                std::memory_order_acquire) ==
                admission.availability.availabilitySetEpochMs &&
            g_reachPresentGeneration.load(
                std::memory_order_acquire) == admission.epoch.generation &&
            g_reachPresentModuleBase.load(
                std::memory_order_acquire) == admission.epoch.moduleBase;
        if (!finalReady)
        {
            destination->state.store(
                static_cast<uint8_t>(ReachDisplaySnapshotState::Free),
                std::memory_order_release);
            return;
        }

        destination->epoch = admission.epoch;
        destination->availabilitySetEpochMs =
            admission.availability.availabilitySetEpochMs;
        destination->lifecycleSerial = lifecycleSerial;
        destination->fields = initial;
        presentSwapchain->AddRef();
        destination->buffer0 = buffer0.Detach();
        destination->device = device.Detach();
        destination->buffer0Desc = buffer0Desc;
        destination->swapchainDesc = swapchainDesc;
        destination->buffer0Identity = buffer0Identity;
        destination->deviceIdentity = deviceIdentity;
        destination->immediateContextIdentity = immediateContextIdentity;
        destination->deviceCreationFlags = deviceCreationFlags;
        destination->state.store(
            static_cast<uint8_t>(ReachDisplaySnapshotState::Ready),
            std::memory_order_release);
    }

    ReachDisplayContinuity MakeReachDisplayContinuity(
        const ReachModuleEpoch& epoch, uint64_t revision,
        uintptr_t buffer0Identity,
        const ReachDisplayFields& fields) noexcept
    {
        ReachDisplayContinuity continuity{};
        continuity.epoch = epoch;
        continuity.resourceRevision = revision;
        continuity.swapchainIdentity =
            reinterpret_cast<uintptr_t>(fields.engineSwapchain);
        continuity.buffer0Identity = buffer0Identity;
        continuity.surfaceArrayIdentity = fields.surfaceArray;
        continuity.record0RtvIdentity =
            reinterpret_cast<uintptr_t>(fields.record0Rtv);
        continuity.record0SrvIdentity =
            reinterpret_cast<uintptr_t>(fields.record0Srv);
        continuity.selectedRtvIdentity =
            reinterpret_cast<uintptr_t>(fields.selectedRtv);
        continuity.specializationCount = fields.specializationCount;
        continuity.selectedSpecialization =
            fields.selectedSpecialization;
        return continuity;
    }

    bool ResetReachDisplayCandidateLocked(
        bool teardown, bool releaseResources) noexcept
    {
        if (ReachModuleEpochValid(g_reachDisplayEpoch))
        {
            if (teardown)
                g_reachDirectCopyGate.Teardown(g_reachDisplayEpoch);
            else
                g_reachDirectCopyGate.Invalidate(g_reachDisplayEpoch);
        }
        if (teardown)
        {
            g_reachDisplayEpoch = {};
            g_reachDisplayResourceRevision = 0;
        }
        bool resourcesReleased = true;
        if (releaseResources)
        {
            resourcesReleased = InvalidateReachCaptureResources();
            if (resourcesReleased)
            {
                ReleaseReachEyeCaches();
                DiscardReachDisplaySnapshots(false);
            }
        }
        g_reachDisplayNextAttemptMs = 0;
        g_reachDisplayReadyLogged = false;
        return resourcesReleased;
    }

    bool BuildReachDisplayProof(
        const ReachDisplaySnapshot& snapshot,
        const ReachModuleEpoch& epoch,
        const ReachPreflightToken& preflight,
        ReachDisplaySurfaceProof& proof,
        ReachDisplayFailure& failure)
    {
        const ReachDisplayFields& initialFields = snapshot.fields;
        const uint64_t lifecycleSerial = snapshot.lifecycleSerial;
        failure = ReachDisplayFailure::EngineFields;
        if (!initialFields.engineSwapchain || !snapshot.buffer0 ||
            !snapshot.device || !snapshot.buffer0Identity ||
            !snapshot.deviceIdentity ||
            !snapshot.immediateContextIdentity ||
            (snapshot.deviceCreationFlags &
             D3D11_CREATE_DEVICE_SINGLETHREADED) != 0 ||
            initialFields.specializationCount !=
                kReachDisplaySurfaceCount ||
            initialFields.selectedSpecialization != 0 ||
            initialFields.selectedRtv != initialFields.record0Rtv)
            return false;

        const uintptr_t bufferIdentity = snapshot.buffer0Identity;
        const DXGI_SWAP_CHAIN_DESC& swapchainDesc = snapshot.swapchainDesc;
        const D3D11_TEXTURE2D_DESC& sourceDesc = snapshot.buffer0Desc;
        // The injected retail run from d0a5434 is authoritative over the
        // earlier static constructor reading: MCC's live Reach Present owner
        // is a two-buffer flip-discard chain. Keep this exact rather than
        // broadening admission to arbitrary flip-model swapchains.
        const bool swapchainContract =
            swapchainDesc.BufferCount == 2 &&
            swapchainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD &&
            swapchainDesc.BufferDesc.Format ==
                DXGI_FORMAT_R8G8B8A8_UNORM &&
            swapchainDesc.SampleDesc.Count == 1 &&
            swapchainDesc.SampleDesc.Quality == 0 &&
            (swapchainDesc.BufferUsage &
             (DXGI_USAGE_SHADER_INPUT |
              DXGI_USAGE_RENDER_TARGET_OUTPUT)) ==
                (DXGI_USAGE_SHADER_INPUT |
                 DXGI_USAGE_RENDER_TARGET_OUTPUT);
        const ReachCopyShape sourceShape = ReachShape(sourceDesc);
        const bool sourceContract =
            ReachDisplayCopyShapeValid(sourceShape) &&
            sourceDesc.Usage == D3D11_USAGE_DEFAULT &&
            sourceDesc.CPUAccessFlags == 0 &&
            (sourceDesc.BindFlags &
             (D3D11_BIND_RENDER_TARGET |
              D3D11_BIND_SHADER_RESOURCE)) ==
                (D3D11_BIND_RENDER_TARGET |
                 D3D11_BIND_SHADER_RESOURCE);
        if (!swapchainContract || !sourceContract)
        {
            failure = ReachDisplayFailure::SwapchainContract;
            return false;
        }

        if (!EnsureReachEyeCaches(
                snapshot.device, sourceDesc, sourceShape) ||
            !g_reachEyeCache[0] || !g_reachEyeCache[1])
        {
            failure = ReachDisplayFailure::EyeAllocation;
            return false;
        }

        D3D11_TEXTURE2D_DESC eyeDesc[2]{};
        g_reachEyeCache[0]->GetDesc(&eyeDesc[0]);
        g_reachEyeCache[1]->GetDesc(&eyeDesc[1]);
        const ReachCopyShape eyeShape[2] = {
            ReachShape(eyeDesc[0]), ReachShape(eyeDesc[1])};
        const uintptr_t eyeIdentity[2] = {
            ReachComIdentity(g_reachEyeCache[0]),
            ReachComIdentity(g_reachEyeCache[1])};
        if (!eyeIdentity[0] || !eyeIdentity[1] ||
            eyeIdentity[0] == eyeIdentity[1] ||
            eyeIdentity[0] == bufferIdentity ||
            eyeIdentity[1] == bufferIdentity ||
            !ReachSameCopyShape(sourceShape, eyeShape[0]) ||
            !ReachSameCopyShape(sourceShape, eyeShape[1]))
        {
            NoteReachEyeAllocationFailure("published-eye-identity-shape",
                E_FAIL, snapshot.device, sourceDesc);
            failure = ReachDisplayFailure::EyeAllocation;
            return false;
        }

        const uintptr_t deviceIdentity = snapshot.deviceIdentity;
        const uintptr_t contextIdentity =
            snapshot.immediateContextIdentity;
        const bool sameDevice = deviceIdentity && contextIdentity &&
            ReachResourceDeviceIdentity(snapshot.buffer0) == deviceIdentity &&
            ReachResourceDeviceIdentity(g_reachEyeCache[0]) == deviceIdentity &&
            ReachResourceDeviceIdentity(g_reachEyeCache[1]) == deviceIdentity;
        const bool immediate = contextIdentity != 0;
        if (!sameDevice || !immediate)
        {
            NoteReachEyeAllocationFailure("published-eye-device-context",
                E_FAIL, snapshot.device, sourceDesc);
            failure = ReachDisplayFailure::DeviceContext;
            return false;
        }

        ReachDisplayFields finalFields{};
        if (!ReadReachDisplayFields(epoch, finalFields) ||
            finalFields.engineSwapchain != initialFields.engineSwapchain ||
            finalFields.specializationCount !=
                initialFields.specializationCount ||
            finalFields.surfaceArray != initialFields.surfaceArray ||
            finalFields.record0Rtv != initialFields.record0Rtv ||
            finalFields.record0Srv != initialFields.record0Srv ||
            finalFields.selectedRtv != initialFields.selectedRtv ||
            finalFields.selectedSpecialization !=
                initialFields.selectedSpecialization)
        {
            failure = ReachDisplayFailure::EngineFields;
            return false;
        }
        if (g_reachDisplayResourceRevision ==
            std::numeric_limits<uint64_t>::max())
        {
            failure = ReachDisplayFailure::Publication;
            return false;
        }
        ++g_reachDisplayResourceRevision;
        proof = {};
        proof.continuity = MakeReachDisplayContinuity(
            epoch, g_reachDisplayResourceRevision,
            bufferIdentity, finalFields);
        proof.continuity.lifecycleSerial = lifecycleSerial;
        proof.continuity.deviceIdentity = deviceIdentity;
        proof.continuity.immediateContextIdentity = contextIdentity;
        proof.continuity.eyeResourceIdentities[0] = eyeIdentity[0];
        proof.continuity.eyeResourceIdentities[1] = eyeIdentity[1];
        proof.preflight = preflight;
        proof.immediateContextIdentity = contextIdentity;
        proof.eyeResourceIdentities[0] = eyeIdentity[0];
        proof.eyeResourceIdentities[1] = eyeIdentity[1];
        proof.source = sourceShape;
        proof.eyes[0] = eyeShape[0];
        proof.eyes[1] = eyeShape[1];
        proof.readyEyeMask = 0x3u;
        proof.engineSwapchainMatchesPresent = true;
        proof.selectedRtvMatchesRecord0 = true;
        proof.swapchainContract = true;
        proof.sameDevice = true;
        proof.immediateContext = true;
        failure = ReachDisplayFailure::None;
        return ReachDisplaySurfaceProofComplete(proof);
    }

    bool PublishReachCaptureResources(
        const ReachDisplaySnapshot& snapshot,
        const ReachDisplaySurfaceProof& proof) noexcept
    {
        if (!snapshot.buffer0 || !snapshot.device ||
            !g_reachEyeCache[0] || !g_reachEyeCache[1] ||
            !ReachDisplaySurfaceProofComplete(proof))
            return false;
        if (g_reachCaptureEnabled.load(std::memory_order_acquire) &&
            g_reachCaptureSource == snapshot.buffer0 &&
            g_reachCaptureEyes[0] == g_reachEyeCache[0] &&
            g_reachCaptureEyes[1] == g_reachEyeCache[1] &&
            ReachSameModuleEpoch(g_reachCaptureProof.continuity.epoch,
                                 proof.continuity.epoch) &&
            g_reachCaptureProof.continuity.lifecycleSerial ==
                proof.continuity.lifecycleSerial)
            return true;

        if (!InvalidateReachCaptureResources())
            return false;
        ID3D11DeviceContext* context = nullptr;
        snapshot.device->GetImmediateContext(&context);
        if (!context || ReachComIdentity(context) !=
                proof.continuity.immediateContextIdentity)
        {
            if (context)
                context->Release();
            return false;
        }
        snapshot.buffer0->AddRef();
        g_reachEyeCache[0]->AddRef();
        g_reachEyeCache[1]->AddRef();
        g_reachCaptureSource = snapshot.buffer0;
        g_reachCaptureEyes[0] = g_reachEyeCache[0];
        g_reachCaptureEyes[1] = g_reachEyeCache[1];
        g_reachCaptureContext = context;
        g_reachCaptureProof = proof;
        g_reachCaptureDesc = snapshot.buffer0Desc;
        g_reachCaptureEnabled.store(true, std::memory_order_seq_cst);
        return true;
    }

    void PollReachDisplayCandidate()
    {
        ReachExclusiveResourceLock lock(g_reachDisplayResourceLock);
        ReachDisplayAdmission admission{};
        if (!ReadReachDisplayAdmission(admission))
        {
            if (ReachModuleEpochValid(g_reachDisplayEpoch))
                g_reachDirectCopyGate.Invalidate(g_reachDisplayEpoch);
            g_reachDisplayReadyLogged = false;
            return;
        }

        if (!admission.resident)
        {
            ResetReachDisplayCandidateLocked(true, true);
            return;
        }
        if (!admission.selected)
        {
            if (ReachSameModuleEpoch(
                    admission.epoch, g_reachDisplayEpoch))
            {
                // H3/Reach overlap is normal during MCC title transitions.
                // Preserve the resident Reach epoch and its revision high-water
                // so the same generation can safely re-arm after ambiguity.
                ResetReachDisplayCandidateLocked(false, true);
            }
            else
            {
                ResetReachDisplayCandidateLocked(true, true);
            }
            return;
        }

        static ReachModuleEpoch loggedAdmissionEpoch{};
        static uint64_t loggedAvailabilityEpoch = 0;
        if (!ReachSameModuleEpoch(loggedAdmissionEpoch, admission.epoch) ||
            loggedAvailabilityEpoch != admission.availability.availabilitySetEpochMs)
        {
            loggedAdmissionEpoch = admission.epoch;
            loggedAvailabilityEpoch = admission.availability.availabilitySetEpochMs;
            LOG("Reach display admission: selected Reach generation %u with resident mask 0x%X; "
                "verifying current native Present resources",
                admission.epoch.generation, admission.availability.availabilityMask);
        }

        if (!ReachSameModuleEpoch(
                admission.epoch, g_reachDisplayEpoch))
        {
            ResetReachDisplayCandidateLocked(true, true);
            if (!g_reachDirectCopyGate.AdvanceEpoch(admission.epoch))
                return;
            g_reachDisplayEpoch = admission.epoch;
        }

        const uint64_t lifecycleSerial =
            g_reachDisplayLifecycleSerial.load(
                std::memory_order_acquire);
        const bool presentAdmissionMatches =
            lifecycleSerial != std::numeric_limits<uint64_t>::max() &&
            g_reachPresentSelectedEligible.load(
                std::memory_order_acquire) &&
            g_reachPresentAvailabilitySetEpochMs.load(
                std::memory_order_acquire) ==
                admission.availability.availabilitySetEpochMs &&
            g_reachPresentGeneration.load(
                std::memory_order_acquire) ==
                admission.epoch.generation &&
            g_reachPresentModuleBase.load(
                std::memory_order_acquire) ==
                admission.epoch.moduleBase;
        const ReachPreflightToken preflight =
            ReachRenderCandidate_GetPreflight(admission.epoch);
        if (!presentAdmissionMatches || !preflight.Complete() ||
            !ReachRenderCandidate_IsPreflightCurrent(preflight))
        {
            g_reachDirectCopyGate.Invalidate(admission.epoch);
            g_reachDisplayReadyLogged = false;
            const uint64_t waitingNow = GetTickCount64();
            if (!g_reachDisplayLastFailureLogMs ||
                waitingNow - g_reachDisplayLastFailureLogMs >= 2000)
            {
                g_reachDisplayLastFailureLogMs = waitingNow;
                LOG("Reach display worker waiting: generation=%u residentMask=0x%X "
                    "currentPresent=%d preflight=%d",
                    admission.epoch.generation, admission.availability.availabilityMask,
                    presentAdmissionMatches ? 1 : 0,
                    preflight.Complete() && ReachRenderCandidate_IsPreflightCurrent(preflight) ? 1 : 0);
            }
            return;
        }

        const uint64_t now = GetTickCount64();
        if (now < g_reachDisplayNextAttemptMs)
            return;
        g_reachDisplayNextAttemptMs = now + 250;
        // Every cold refresh revokes the old capability before touching any
        // engine or COM object. A failed replacement can never leave the prior
        // resource proof current.
        g_reachDirectCopyGate.Invalidate(admission.epoch);

        ReachModuleReference moduleReference;
        ReachDisplayFailure failure = ReachDisplayFailure::ModuleReference;
        if (!moduleReference.Acquire(admission.epoch.moduleBase))
        {
            g_reachDisplayReadyLogged = false;
        }
        else
        {
            ReachDisplaySnapshotLease snapshotLease;
            const bool haveSnapshot = snapshotLease.Acquire();
            const ReachDisplaySnapshot* snapshot = snapshotLease.Get();
            const bool snapshotMatches = haveSnapshot && snapshot &&
                ReachSameModuleEpoch(snapshot->epoch, admission.epoch) &&
                snapshot->availabilitySetEpochMs ==
                    admission.availability.availabilitySetEpochMs &&
                snapshot->lifecycleSerial == lifecycleSerial;
            failure = snapshotMatches
                ? ReachDisplayFailure::SwapchainContract
                : ReachDisplayFailure::EngineFields;

            ReachDisplaySurfaceProof proof{};
            const bool built = snapshotMatches &&
                BuildReachDisplayProof(
                    *snapshot, admission.epoch, preflight,
                    proof, failure);
            ReachDisplayAdmission finalAdmission{};
            const bool admissionStillCurrent =
                ReadReachDisplayAdmission(finalAdmission) &&
                ReachSameDisplayAdmission(admission, finalAdmission) &&
                g_reachDisplayLifecycleSerial.load(
                    std::memory_order_acquire) == lifecycleSerial &&
                g_reachPresentSelectedEligible.load(
                    std::memory_order_acquire) &&
                g_reachPresentAvailabilitySetEpochMs.load(
                    std::memory_order_acquire) ==
                    admission.availability.availabilitySetEpochMs &&
                g_reachPresentGeneration.load(
                    std::memory_order_acquire) ==
                    admission.epoch.generation &&
                g_reachPresentModuleBase.load(
                    std::memory_order_acquire) ==
                    admission.epoch.moduleBase &&
                ReachRenderCandidate_IsPreflightCurrent(preflight);
            const bool published = built && admissionStillCurrent &&
                g_reachDirectCopyGate.Publish(proof) &&
                PublishReachCaptureResources(*snapshot, proof);
            if (published)
            {
                if (!g_reachDisplayReadyLogged)
                {
                    g_reachDisplayReadyLogged = true;
                    LOG("Reach display worker proof PASS: exact Present "
                        "buffer0, specialization0 structural continuity, "
                        "same device/context identity, and two private exact "
                        "eye caches; camera core remains gated by armed state "
                        "and exact current-frame render access");
                }
                return;
            }
            if (built && !admissionStillCurrent)
                failure = ReachDisplayFailure::Publication;
            else if (built)
                failure = ReachDisplayFailure::Publication;
            g_reachDisplayReadyLogged = false;
        }

        if (!g_reachDisplayLastFailureLogMs ||
            now - g_reachDisplayLastFailureLogMs >= 2000)
        {
            g_reachDisplayLastFailureLogMs = now;
            LOG("Reach display worker proof waiting (%s); stock Reach remains active",
                ReachDisplayFailureName(failure));
        }
    }
#endif

    void ValidateStereoImagesOnce()
    {
        if (g_stereoValidationDone || !g_eyeHasImage[0] || !g_eyeHasImage[1] ||
            !g_eyeCache[0] || !g_eyeCache[1])
            return;
        if (g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            LOG("M2 VALIDATION: unsupported eye format %u", (unsigned)g_eyeCacheDesc.Format);
            g_stereoValidationDone = true;
            return;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = g_eyeCacheDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        ID3D11Texture2D* staging[2]{};
        if (FAILED(g_device->CreateTexture2D(&stagingDesc, nullptr, &staging[0])) ||
            FAILED(g_device->CreateTexture2D(&stagingDesc, nullptr, &staging[1])))
        {
            if (staging[0]) staging[0]->Release();
            if (staging[1]) staging[1]->Release();
            LOG("M2 VALIDATION: staging allocation failed");
            g_stereoValidationDone = true;
            return;
        }

        g_context->CopyResource(staging[0], g_eyeCache[0]);
        g_context->CopyResource(staging[1], g_eyeCache[1]);
        D3D11_MAPPED_SUBRESOURCE mapped[2]{};
        const HRESULT hr0 = g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]);
        const HRESULT hr1 = g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]);
        if (SUCCEEDED(hr0) && SUCCEEDED(hr1))
        {
            unsigned long long rgbDelta = 0;
            unsigned samples = 0, changed = 0;
            const unsigned step = 16;
            for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
            {
                const auto* left = static_cast<const unsigned char*>(mapped[0].pData) + y * mapped[0].RowPitch;
                const auto* right = static_cast<const unsigned char*>(mapped[1].pData) + y * mapped[1].RowPitch;
                for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                {
                    const unsigned o = x * 4;
                    unsigned pixelDelta = 0;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const int d = (int)left[o + c] - (int)right[o + c];
                        pixelDelta += (unsigned)(d < 0 ? -d : d);
                    }
                    rgbDelta += pixelDelta;
                    if (pixelDelta > 12) ++changed;
                    ++samples;
                }
            }
            const double meanChannelDelta = samples ? (double)rgbDelta / (samples * 3.0) : 0.0;
            const double changedPercent = samples ? (100.0 * changed / samples) : 0.0;
            LOG("M2 VALIDATION: distinct eye pixels mean RGB delta=%.3f, changed samples=%.1f%% (%u/%u)",
                meanChannelDelta, changedPercent, changed, samples);
        }
        else
        {
            LOG("M2 VALIDATION: staging map failed (0x%08X, 0x%08X)",
                (unsigned)hr0, (unsigned)hr1);
        }
        if (SUCCEEDED(hr0)) g_context->Unmap(staging[0], 0);
        if (SUCCEEDED(hr1)) g_context->Unmap(staging[1], 0);
        staging[0]->Release();
        staging[1]->Release();
        g_stereoValidationDone = true;
    }

#if HALOMCCVR_HALO2_STEREO6DOF
    // E-H2-8: where do the classic draws happen relative to the per-eye
    // render_view scopes? Zero inside and thousands outside means the world
    // is submitted AFTER render_view returns, which is what identical eye
    // copies plus a stock-FOV picture look like.
    void ReportHalo2DrawCensus()
    {
        static uint64_t lastInside[2]{};
        static uint64_t lastOutside = 0;
        const uint64_t inside0 =
            g_halo2DrawsInsideEye[0].load(std::memory_order_relaxed);
        const uint64_t inside1 =
            g_halo2DrawsInsideEye[1].load(std::memory_order_relaxed);
        const uint64_t outside =
            g_halo2DrawsOutsideEye.load(std::memory_order_relaxed);
        const uint64_t dInside0 = inside0 - lastInside[0];
        const uint64_t dInside1 = inside1 - lastInside[1];
        const uint64_t dOutside = outside - lastOutside;
        lastInside[0] = inside0;
        lastInside[1] = inside1;
        lastOutside = outside;
        if (!dInside0 && !dInside1 && !dOutside)
            return;
        LOG("Halo 2 draw census (last 2s): DrawIndexed/Draw inside eye 0 = "
            "%llu, inside eye 1 = %llu, outside any eye scope = %llu",
            static_cast<unsigned long long>(dInside0),
            static_cast<unsigned long long>(dInside1),
            static_cast<unsigned long long>(dOutside));
    }

    // Halo 2 acceptance is "two different images from two different cameras",
    // not "our hooks ran". Stage 3AM bounds this full-resolution synchronous
    // readback to capture-source discovery, then retires it. A source fallback
    // or an explicit diagnostic request rearms it.
    void ReportHalo2DrawCensus();
    void ValidateHalo2EyePairPeriodic()
    {
        static ID3D11Texture2D* staging[2]{};
        static D3D11_TEXTURE2D_DESC stagingDesc{};
        static uint64_t lastCheckMs = 0;
        static unsigned identicalRuns = 0;
        const uint64_t nowMs = GetTickCount64();
        const int sourceBefore =
            g_halo2CaptureSource.load(std::memory_order_relaxed);
        const int trackedSource =
            g_halo2HeavyValidationSource.load(std::memory_order_relaxed);
        if (sourceBefore != trackedSource)
        {
            g_halo2HeavyValidationSource.store(
                sourceBefore, std::memory_order_relaxed);
            g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
            g_halo2HeavyValidationRetired.store(
                false, std::memory_order_release);
        }
        const bool explicitDumpPending =
            g_halo2EyeDumpDueMs.load(std::memory_order_acquire) != 0;
        if (g_halo2HeavyValidationRetired.load(std::memory_order_acquire) &&
            !explicitDumpPending)
        {
            return;
        }
        if (nowMs - lastCheckMs < 500)
            return;
        lastCheckMs = nowMs;
        ReportHalo2DrawCensus();
        if (!g_device || !g_context || !g_eyeHasImage[0] ||
            !g_eyeHasImage[1] || !g_eyeCache[0] || !g_eyeCache[1])
        {
            return;
        }
        if (g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        {
            static bool loggedFormat = false;
            if (!loggedFormat)
            {
                loggedFormat = true;
                LOG("Halo 2 eye-pair pixel check: unsupported eye format %u; "
                    "the check is disabled for this shape",
                    static_cast<unsigned>(g_eyeCacheDesc.Format));
            }
            return;
        }
        if (!staging[0] || !staging[1] ||
            stagingDesc.Width != g_eyeCacheDesc.Width ||
            stagingDesc.Height != g_eyeCacheDesc.Height ||
            stagingDesc.Format != g_eyeCacheDesc.Format)
        {
            for (auto*& texture : staging)
            {
                if (texture) texture->Release();
                texture = nullptr;
            }
            D3D11_TEXTURE2D_DESC desc = g_eyeCacheDesc;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging[0])) ||
                FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging[1])))
            {
                for (auto*& texture : staging)
                {
                    if (texture) texture->Release();
                    texture = nullptr;
                }
                LOG("Halo 2 eye-pair pixel check: staging allocation failed "
                    "for %ux%u", desc.Width, desc.Height);
                return;
            }
            stagingDesc = desc;
        }

        g_context->CopyResource(staging[0], g_eyeCache[0]);
        g_context->CopyResource(staging[1], g_eyeCache[1]);
        D3D11_MAPPED_SUBRESOURCE mapped[2]{};
        const HRESULT hr0 =
            g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]);
        const HRESULT hr1 =
            g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]);
        // The probe and pre-pair comparisons below unmap and re-map these;
        // the final Unmap follows the LAST map result, not the first.
        bool stagingMapped[2] = {SUCCEEDED(hr0), SUCCEEDED(hr1)};
        if (SUCCEEDED(hr0) && SUCCEEDED(hr1))
        {
            unsigned long long rgbDelta = 0;
            unsigned samples = 0, changed = 0, lit = 0;
            const unsigned step = 16;
            for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
            {
                const auto* left =
                    static_cast<const unsigned char*>(mapped[0].pData) +
                    y * mapped[0].RowPitch;
                const auto* right =
                    static_cast<const unsigned char*>(mapped[1].pData) +
                    y * mapped[1].RowPitch;
                for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                {
                    const unsigned o = x * 4;
                    unsigned pixelDelta = 0;
                    unsigned sum = 0;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const int d = (int)left[o + c] - (int)right[o + c];
                        pixelDelta += (unsigned)(d < 0 ? -d : d);
                        sum += left[o + c];
                    }
                    rgbDelta += pixelDelta;
                    if (pixelDelta > 12) ++changed;
                    if (sum > 24) ++lit;
                    ++samples;
                }
            }
            const double meanChannelDelta =
                samples ? (double)rgbDelta / (samples * 3.0) : 0.0;
            const double changedPercent =
                samples ? (100.0 * changed / samples) : 0.0;
            const double litPercent =
                samples ? (100.0 * lit / samples) : 0.0;
            // E-H2-34: the first-person weapon's disparity, measured. The
            // lower-right box (where the gun is) and an upper world band are
            // each searched for the horizontal shift that best matches eye 1
            // to eye 0. Positive = eye 1's content sits to the RIGHT of eye
            // 0's, which for a near object is the wrong way round (eye 0 is
            // the left eye); the world band gives the sign a true stereo
            // pair has. Sampled every 16 px; once every 2 s.
            auto bestShift = [&](unsigned x0, unsigned x1, unsigned y0, unsigned y1,
                                 int range, int stride, double& bestMatch,
                                 double& zeroMatch) -> int {
                int best = 0;
                bestMatch = 1.0e9;
                zeroMatch = 0.0;
                const unsigned W = g_eyeCacheDesc.Width;
                for (int dx = -range; dx <= range; dx += stride)
                {
                    unsigned long long acc = 0;
                    unsigned n = 0;
                    for (unsigned y = y0; y < y1; y += 16)
                    {
                        const auto* l =
                            static_cast<const unsigned char*>(mapped[0].pData) +
                            y * mapped[0].RowPitch;
                        const auto* r =
                            static_cast<const unsigned char*>(mapped[1].pData) +
                            y * mapped[1].RowPitch;
                        for (unsigned x = x0; x < x1; x += 16)
                        {
                            const int xr = static_cast<int>(x) + dx;
                            if (xr < 0 || xr >= static_cast<int>(W))
                                continue;
                            const unsigned o = x * 4;
                            const unsigned p = static_cast<unsigned>(xr) * 4;
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                const int d = (int)l[o + c] - (int)r[p + c];
                                acc += (unsigned)(d < 0 ? -d : d);
                            }
                            ++n;
                        }
                    }
                    const double m = n ? (double)acc / (n * 3.0) : 1.0e9;
                    if (dx == 0)
                        zeroMatch = m;
                    if (m < bestMatch)
                    {
                        bestMatch = m;
                        best = dx;
                    }
                }
                return best;
            };
            const unsigned Wd = g_eyeCacheDesc.Width;
            const unsigned Hd = g_eyeCacheDesc.Height;
            double weaponMatch = 0.0, weaponZero = 0.0, worldMatch = 0.0, worldZero = 0.0;
            const int weaponShift = bestShift(
                Wd * 45 / 100, Wd * 80 / 100, Hd * 50 / 100, Hd * 95 / 100,
                400, 8, weaponMatch, weaponZero);
            const int worldShift = bestShift(
                Wd * 30 / 100, Wd * 70 / 100, Hd * 8 / 100, Hd * 30 / 100,
                64, 4, worldMatch, worldZero);
            // E-H2-13: an explicitly requested picture, quarter size, next to
            // the log (HaloMCCVR-halo2-eye0.bmp / eye1.bmp). "Cropped" and
            // "goggles" are judged by looking at the frame the mod actually
            // published, not by counters. Staging is already mapped here.
            {
                const uint64_t dueMs = g_halo2EyeDumpDueMs.load(std::memory_order_acquire);
                const bool requested = dueMs != 0 && nowMs >= dueMs;
                if (requested && LogDirectory()[0])
                {
                    g_halo2EyeDumpDueMs.store(0, std::memory_order_release);
                    const bool bgr =
                        g_eyeCacheDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                        g_eyeCacheDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                    const unsigned scale = 4;
                    const unsigned w = g_eyeCacheDesc.Width / scale;
                    const unsigned h = g_eyeCacheDesc.Height / scale;
                    for (int eye = 0; eye < 2 && w && h; ++eye)
                    {
                        wchar_t path[MAX_PATH];
                        _snwprintf_s(path, _TRUNCATE, L"%sHaloMCCVR-halo2-eye%d.bmp",
                                     LogDirectory(), eye);
                        FILE* file = _wfsopen(path, L"wb", _SH_DENYNO);
                        if (!file)
                            continue;
                        const uint32_t rowBytes = (w * 3 + 3) & ~3u;
                        const uint32_t imageBytes = rowBytes * h;
                        uint8_t header[54] = {'B', 'M'};
                        auto put32 = [&](size_t at, uint32_t v) {
                            std::memcpy(header + at, &v, 4);
                        };
                        auto put16 = [&](size_t at, uint16_t v) {
                            std::memcpy(header + at, &v, 2);
                        };
                        put32(2, 54 + imageBytes);
                        put32(10, 54);
                        put32(14, 40);
                        put32(18, w);
                        put32(22, h);
                        put16(26, 1);
                        put16(28, 24);
                        put32(34, imageBytes);
                        fwrite(header, 1, sizeof(header), file);
                        std::vector<uint8_t> row(rowBytes, 0);
                        const auto* base =
                            static_cast<const unsigned char*>(mapped[eye].pData);
                        for (unsigned y = h; y-- > 0;)
                        {
                            const auto* src = base + (y * scale) * mapped[eye].RowPitch;
                            for (unsigned x = 0; x < w; ++x)
                            {
                                const unsigned o = x * scale * 4;
                                row[x * 3 + 0] = bgr ? src[o + 0] : src[o + 2];
                                row[x * 3 + 1] = src[o + 1];
                                row[x * 3 + 2] = bgr ? src[o + 2] : src[o + 0];
                            }
                            fwrite(row.data(), 1, rowBytes, file);
                        }
                        fclose(file);
                    }
                    static bool loggedDump = false;
                    if (!loggedDump)
                    {
                        loggedDump = true;
                        LOG("Halo 2 eye frames dumped on explicit request as "
                            "HaloMCCVR-halo2-eye0.bmp / eye1.bmp next to the log "
                            "(%ux%u, quarter size of the %ux%u capture)",
                            w, h, g_eyeCacheDesc.Width, g_eyeCacheDesc.Height);
                    }
                }
            }
            const bool identical = changed == 0;
            identicalRuns = identical ? identicalRuns + 1 : 0;
            // E-H2-8: compare the probe candidates the same way. If the
            // published pair is identical while a probe pair differs, the
            // probe is the target that carries per-eye content: switch.
            const int currentSource =
                g_halo2CaptureSource.load(std::memory_order_relaxed);
            double probeChanged[2] = {-1.0, -1.0};
            bool boxCompared[2] = {false, false};
            // E-H2-27: the shape-agnostic centre box. Any candidate the
            // full-size copy could not take is compared here instead, so
            // the engine's own scene target is finally sampled.
            for (int probe = 0; probe < 2; ++probe)
            {
                if (!g_halo2ProbeBoxHasImage[probe][0] ||
                    !g_halo2ProbeBoxHasImage[probe][1] ||
                    !g_halo2ProbeBox[probe][0] || !g_halo2ProbeBox[probe][1] ||
                    g_halo2ProbeBoxFormat[probe][0] != g_halo2ProbeBoxFormat[probe][1])
                {
                    continue;
                }
                D3D11_MAPPED_SUBRESOURCE bm[2]{};
                if (FAILED(g_context->Map(g_halo2ProbeBox[probe][0], 0, D3D11_MAP_READ, 0, &bm[0])))
                    continue;
                if (FAILED(g_context->Map(g_halo2ProbeBox[probe][1], 0, D3D11_MAP_READ, 0, &bm[1])))
                {
                    g_context->Unmap(g_halo2ProbeBox[probe][0], 0);
                    continue;
                }
                unsigned bSamples = 0, bChanged = 0;
                for (unsigned y = 0; y < kHalo2ProbeBoxSize; y += 2)
                {
                    const auto* l = static_cast<const unsigned char*>(bm[0].pData) +
                        y * bm[0].RowPitch;
                    const auto* r = static_cast<const unsigned char*>(bm[1].pData) +
                        y * bm[1].RowPitch;
                    for (unsigned x = 0; x < kHalo2ProbeBoxSize; x += 2)
                    {
                        const unsigned o = x * 4;
                        unsigned d = 0;
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            const int dd = (int)l[o + c] - (int)r[o + c];
                            d += (unsigned)(dd < 0 ? -dd : dd);
                        }
                        if (d > 12) ++bChanged;
                        ++bSamples;
                    }
                }
                g_context->Unmap(g_halo2ProbeBox[probe][0], 0);
                g_context->Unmap(g_halo2ProbeBox[probe][1], 0);
                if (bSamples)
                {
                    probeChanged[probe] = 100.0 * bChanged / bSamples;
                    boxCompared[probe] = true;
                }
            }
            if (staging[0] && staging[1])
            {
                for (int probe = 0; probe < 2; ++probe)
                {
                    if (!g_halo2ProbeHasImage[probe][0] ||
                        !g_halo2ProbeHasImage[probe][1] ||
                        !g_halo2ProbeCache[probe][0] ||
                        !g_halo2ProbeCache[probe][1] ||
                        g_halo2ProbeDesc.Width != g_eyeCacheDesc.Width ||
                        g_halo2ProbeDesc.Height != g_eyeCacheDesc.Height ||
                        g_halo2ProbeDesc.Format != g_eyeCacheDesc.Format ||
                        // Both eyes must have copied the SAME target, or the
                        // "pair" compares two different textures.
                        g_halo2ProbeCandidate[probe][0].load(std::memory_order_relaxed) !=
                            g_halo2ProbeCandidate[probe][1].load(std::memory_order_relaxed) ||
                        g_halo2ProbeCandidatePtr[probe][0].load(std::memory_order_relaxed) !=
                            g_halo2ProbeCandidatePtr[probe][1].load(std::memory_order_relaxed))
                    {
                        continue;
                    }
                    g_context->Unmap(staging[0], 0);
                    g_context->Unmap(staging[1], 0);
                    g_context->CopyResource(staging[0], g_halo2ProbeCache[probe][0]);
                    g_context->CopyResource(staging[1], g_halo2ProbeCache[probe][1]);
                    D3D11_MAPPED_SUBRESOURCE pm[2]{};
                    if (FAILED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &pm[0])) ||
                        FAILED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &pm[1])))
                    {
                        // Leave both mapped-state flags consistent for the
                        // unmap below by remapping the eye caches.
                        g_context->CopyResource(staging[0], g_eyeCache[0]);
                        g_context->CopyResource(staging[1], g_eyeCache[1]);
                        stagingMapped[0] = SUCCEEDED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]));
                        stagingMapped[1] = SUCCEEDED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]));
                        break;
                    }
                    unsigned pSamples = 0, pChanged = 0;
                    for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
                    {
                        const auto* l = static_cast<const unsigned char*>(pm[0].pData) +
                            y * pm[0].RowPitch;
                        const auto* r = static_cast<const unsigned char*>(pm[1].pData) +
                            y * pm[1].RowPitch;
                        for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                        {
                            const unsigned o = x * 4;
                            unsigned d = 0;
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                const int dd = (int)l[o + c] - (int)r[o + c];
                                d += (unsigned)(dd < 0 ? -dd : dd);
                            }
                            if (d > 12) ++pChanged;
                            ++pSamples;
                        }
                    }
                    probeChanged[probe] = pSamples ? 100.0 * pChanged / pSamples : 0.0;
                    g_context->Unmap(staging[0], 0);
                    g_context->Unmap(staging[1], 0);
                    boxCompared[probe] = false;
                    g_context->CopyResource(staging[0], g_eyeCache[0]);
                    g_context->CopyResource(staging[1], g_eyeCache[1]);
                    stagingMapped[0] = SUCCEEDED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]));
                    stagingMapped[1] = SUCCEEDED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]));
                }
                // E-H2-24: which candidate each probe slot actually copied
                // this round (the rotation walks the whole candidate set, two
                // at a time, so the probe cost per eye is unchanged).
                // A slot whose two eyes copied different targets reports -1
                // (its comparison was skipped above).
                int probeCandidate[2] = {-1, -1};
                for (int probe = 0; probe < 2; ++probe)
                {
                    const int c0 = g_halo2ProbeCandidate[probe][0].load(std::memory_order_relaxed);
                    const int c1 = g_halo2ProbeCandidate[probe][1].load(std::memory_order_relaxed);
                    const uintptr_t p0 = g_halo2ProbeCandidatePtr[probe][0].load(std::memory_order_relaxed);
                    const uintptr_t p1 = g_halo2ProbeCandidatePtr[probe][1].load(std::memory_order_relaxed);
                    probeCandidate[probe] = (c0 == c1 && p0 == p1) ? c0 : -1;
                }
                const int candidateCount =
                    g_halo2CaptureCandidateCount.load(std::memory_order_relaxed);
                auto candidateName =
                    [](int candidate, uintptr_t pointer, char* out, size_t bytes)
                {
                    if (candidate < 0)
                        _snprintf_s(out, bytes, _TRUNCATE, "-");
                    else if (candidate == 0)
                        _snprintf_s(out, bytes, _TRUNCATE,
                                    "final-output slot 0x197EE58");
                    else if (candidate == 1)
                        _snprintf_s(out, bytes, _TRUNCATE,
                                    "primary scene slot 0x197EE60");
                    else
                        _snprintf_s(out, bytes, _TRUNCATE,
                                    "target #%d bound in the eye (%p)",
                                    candidate - 2,
                                    reinterpret_cast<void*>(pointer));
                };
                char names[3][96];
                candidateName(currentSource, 0, names[0], sizeof(names[0]));
                candidateName(
                    probeCandidate[0],
                    g_halo2ProbeCandidatePtr[0][1].load(std::memory_order_relaxed),
                    names[1], sizeof(names[1]));
                candidateName(
                    probeCandidate[1],
                    g_halo2ProbeCandidatePtr[1][1].load(std::memory_order_relaxed),
                    names[2], sizeof(names[2]));
                LOG("Halo 2 capture probes: source=%s; %s pair changed %.1f%%%s; "
                    "%s pair changed %.1f%%%s (negative = not sampled); %d "
                    "candidates this eye, rotation at %d",
                    names[0], names[1], probeChanged[0],
                    boxCompared[0] ? " (centre box)" : "",
                    names[2], probeChanged[1],
                    boxCompared[1] ? " (centre box)" : "", candidateCount,
                    g_halo2ProbeRotation.load(std::memory_order_relaxed));
                if (identical && identicalRuns >= 2)
                {
                    int best = -1;
                    double bestChanged = 5.0;
                    for (int probe = 0; probe < 2; ++probe)
                        if (probeChanged[probe] > bestChanged &&
                            probeCandidate[probe] >= 0)
                        {
                            best = probeCandidate[probe];
                            bestChanged = probeChanged[probe];
                        }
                    if (best >= 0)
                    {
                        char bestName[96];
                        candidateName(
                            best,
                            g_halo2ProbeCandidatePtr[
                                probeChanged[0] == bestChanged ? 0 : 1][1]
                                .load(std::memory_order_relaxed),
                            bestName, sizeof(bestName));
                        g_halo2CaptureSource.store(best, std::memory_order_relaxed);
                        g_halo2ProbeRotation.store(0, std::memory_order_relaxed);
                        LOG("Halo 2 capture source SWITCHED to %s: the published "
                            "pair was identical twice while that target's pair "
                            "differed by %.1f%% of samples - it is the one the "
                            "engine draws per eye",
                            bestName, bestChanged);
                        identicalRuns = 0;
                    }
                    else
                    {
                        // Nothing in this pair of candidates carried per-eye
                        // content: walk the rotation on so the next round
                        // probes the next two, instead of re-copying the same
                        // two targets forever.
                        int filled = 0;
                        for (int probe = 0; probe < 2; ++probe)
                            if (probeCandidate[probe] >= 0)
                                ++filled;
                        g_halo2ProbeRotation.store(
                            Halo2NextProbeRotation(
                                g_halo2ProbeRotation.load(
                                    std::memory_order_relaxed),
                                candidateCount, filled),
                            std::memory_order_relaxed);
                    }
                }
            }
            // E-H2-24 (C-H2-31): the pre-pair copy against each eye. Both
            // eyes equal to the pre-pair picture means the eye pass never
            // changed the captured target; both eyes different from it but
            // equal to each other means two passes drew the same picture.
            double prePairVsEye[2] = {-1.0, -1.0};
            if (staging[0] && staging[1] && g_halo2PrePairCache &&
                g_halo2PrePairHasImage.load(std::memory_order_acquire))
            {
                D3D11_TEXTURE2D_DESC preDesc{};
                g_halo2PrePairCache->GetDesc(&preDesc);
                if (preDesc.Width == g_eyeCacheDesc.Width &&
                    preDesc.Height == g_eyeCacheDesc.Height &&
                    preDesc.Format == g_eyeCacheDesc.Format)
                {
                    g_context->Unmap(staging[0], 0);
                    g_context->Unmap(staging[1], 0);
                    g_context->CopyResource(staging[0], g_halo2PrePairCache);
                    for (int eye = 0; eye < 2; ++eye)
                    {
                        g_context->CopyResource(staging[1], g_eyeCache[eye]);
                        D3D11_MAPPED_SUBRESOURCE pm[2]{};
                        if (FAILED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &pm[0])))
                            break;
                        if (FAILED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &pm[1])))
                        {
                            g_context->Unmap(staging[0], 0);
                            break;
                        }
                        unsigned pSamples = 0, pChanged = 0;
                        for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
                        {
                            const auto* l = static_cast<const unsigned char*>(pm[0].pData) +
                                y * pm[0].RowPitch;
                            const auto* r = static_cast<const unsigned char*>(pm[1].pData) +
                                y * pm[1].RowPitch;
                            for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                            {
                                const unsigned o = x * 4;
                                unsigned d = 0;
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    const int dd = (int)l[o + c] - (int)r[o + c];
                                    d += (unsigned)(dd < 0 ? -dd : dd);
                                }
                                if (d > 12) ++pChanged;
                                ++pSamples;
                            }
                        }
                        prePairVsEye[eye] = pSamples ? 100.0 * pChanged / pSamples : 0.0;
                        g_context->Unmap(staging[0], 0);
                        g_context->Unmap(staging[1], 0);
                    }
                    g_context->CopyResource(staging[0], g_eyeCache[0]);
                    g_context->CopyResource(staging[1], g_eyeCache[1]);
                    stagingMapped[0] = SUCCEEDED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]));
                    stagingMapped[1] = SUCCEEDED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]));
                }
                g_halo2PrePairHasImage.store(false, std::memory_order_release);
            }
            g_halo2PrePairWanted.store(true, std::memory_order_release);
            const int sourceAfter =
                g_halo2CaptureSource.load(std::memory_order_relaxed);
            const int validationSource =
                g_halo2HeavyValidationSource.load(std::memory_order_relaxed);
            if (sourceAfter != validationSource)
            {
                // The validator just learned a different source. Give that
                // source one fresh comparison before retiring discovery.
                g_halo2HeavyValidationSource.store(
                    sourceAfter, std::memory_order_relaxed);
                g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
            }
            else
            {
                const unsigned checks =
                    g_halo2HeavyValidationChecks.fetch_add(
                        1, std::memory_order_relaxed) + 1;
                if (!identical || checks >= 6)
                {
                    g_halo2HeavyValidationRetired.store(
                        true, std::memory_order_release);
                    LOG("Halo 2 Stage 3AM performance gate: heavy eye validation "
                        "retired after source %d was %s; periodic GPU readback "
                        "and alternate-target probes are dormant",
                        sourceAfter, identical ? "bounded" : "proven distinct");
                }
            }
            if (identical)
            {
                LOG("Halo 2 eye-pair pixel check: IDENTICAL eyes (0/%u "
                    "samples differ, %.1f%% lit) - this is FAKE stereo: the "
                    "engine produced the same image for both eye passes%s; "
                    "captured target before the pair vs eye 0: %.1f%% changed, "
                    "vs eye 1: %.1f%% changed (negative = not sampled; ~0 = "
                    "the eye pass never wrote the captured target)",
                    samples, litPercent,
                    identicalRuns >= 3
                        ? "; persistent, the per-eye camera is NOT reaching "
                          "the renderer"
                        : "",
                    prePairVsEye[0], prePairVsEye[1]);
            }
            else
            {
                LOG("Halo 2 eye-pair pixel check: DISTINCT eyes, mean RGB "
                    "delta=%.3f, changed samples=%.1f%% (%u/%u), %.1f%% lit "
                    "- true per-eye rendering; weapon box (lower right) eye 1 "
                    "vs eye 0 best shift %+d px (match %.1f, at zero %.1f), "
                    "world band best shift %+d px (match %.1f, at zero %.1f) "
                    "- a near object must shift the SAME way as the world, "
                    "further",
                    meanChannelDelta, changedPercent, changed, samples,
                    litPercent, weaponShift, weaponMatch, weaponZero,
                    worldShift, worldMatch, worldZero);
            }
        }
        else
        {
            LOG("Halo 2 eye-pair pixel check: staging map failed "
                "(0x%08X, 0x%08X)", (unsigned)hr0, (unsigned)hr1);
        }
        if (stagingMapped[0]) g_context->Unmap(staging[0], 0);
        if (stagingMapped[1]) g_context->Unmap(staging[1], 0);
    }
#endif

    void ResetPreparedFrame()
    {
        g_preparedShouldRender.store(false, std::memory_order_release);
        g_preparedViewSerialPublished.store(0, std::memory_order_release);
#if HALOMCCVR_HALO2_STEREO6DOF
        g_halo2PreparedCadenceSerial.store(0, std::memory_order_release);
#endif
        g_preparedFrame.begun = false;
        g_preparedFrame.viewsValid = false;
        g_preparedFrame.viewCount = 0;
        g_preparedFrame.predictedDisplayDelta = 0;
    }

    void EndPreparedFrameWithoutLayers(const char* reason)
    {
        if (!g_preparedFrame.begun || g_session == XR_NULL_HANDLE)
        {
            ResetPreparedFrame();
            return;
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = g_preparedFrame.state.predictedDisplayTime;
        end.environmentBlendMode = g_blendMode;
        const XrResult r = xrEndFrame(g_session, &end);
        if (XR_FAILED(r))
        {
            ++g_frameOrderFailures;
            LOG("timing: empty xrEndFrame during %s failed: %s", reason, XrStr(r));
        }
        ResetPreparedFrame();
    }

    void EnterFrameWaitFatalDrain(const char* reason)
    {
        g_waitPipelineFaulted.store(true, std::memory_order_release);
        g_waitThreadStop.store(true, std::memory_order_release);
        if (g_waitConsumedEvent)
            SetEvent(g_waitConsumedEvent);
        if (g_waitReadyEvent)
            SetEvent(g_waitReadyEvent);
        if (g_waitStartedEvent)
            SetEvent(g_waitStartedEvent);
        g_authoredReticlePreparationReady.store(
            false, std::memory_order_release);
        g_sessionRunning = false;
        if (!g_frameWaitFatalExitReason)
            g_frameWaitFatalExitReason = reason;
        g_frameWaitExitRequestAccepted = false;
        g_frameWaitExitLastRequestMs = 0;
        g_frameWaitExitLastLogMs = 0;
        Game_DetachForVrRuntimeFailure();
    }

    bool RequireExactSwapchainCompletion(
        XrResult result, const char* failureReason)
    {
        // Strict title transactions accept only exact XR_SUCCESS.
        // XR_TIMEOUT_EXPIRED does not
        // complete a wait; XR_SESSION_LOSS_PENDING is a successful operation
        // result but signals an unhealthy session. Both abort this candidate's
        // complete layer transaction and enter terminal session recovery. Do
        // not attempt another acquire/release from this frame.
        if (result == XR_SUCCESS)
            return true;
        g_abortFrameForSwapchainFailure = true;
        EnterFrameWaitFatalDrain(failureReason);
        return false;
    }

#if HALOMCCVR_HALO2_STEREO6DOF
    void HandleHalo2StockScreenFailure(
        const char* failureReason,
        Halo2StockScreenXrOwnership ownership) noexcept
    {
        const char* reason = failureReason
            ? failureReason : "unspecified failure";
        if (Halo2StockScreenNeedsFatalDrain(ownership))
        {
            LOG("Halo 2 stock-screen transaction FATAL: %s; XR image/frame "
                "ownership is unresolved, entering OpenXR session recovery",
                reason);
            g_abortFrameForSwapchainFailure = true;
            EnterFrameWaitFatalDrain(failureReason);
            return;
        }
        LOG("Halo 2 stock-screen transaction dropped: %s; no XR image "
            "ownership is unresolved, keeping the OpenXR session active",
            reason);
    }

    bool RequireHalo2StockScreenCompletion(
        XrResult result, const char* stage,
        const char* failureReason,
        Halo2StockScreenXrOwnership ownershipOnFailure) noexcept
    {
        if (result == XR_SUCCESS)
            return true;
        LOG("Halo 2 stock-screen transaction %s did not complete: %s",
            stage ? stage : "OpenXR operation", XrStr(result));
        HandleHalo2StockScreenFailure(
            failureReason, ownershipOnFailure);
        return false;
    }
#endif

    // -------------------------------------------------------------- events

    void PollEvents()
    {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_instance, &ev) == XR_SUCCESS)
        {
            switch (ev.type)
            {
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            {
                const auto& change = *reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
                if (change.session == g_session && change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
                {
                    // Inhibit contact motion until the new space actually
                    // takes effect; an epoch bump at notification time alone
                    // would allow a later origin jump to look like a punch.
                    g_contactSpaceChangeAtNs.store(change.changeTime, std::memory_order_release);
                    g_contactSpaceEpoch.fetch_add(1, std::memory_order_acq_rel);
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            {
                auto& sc = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
                if(g_sessionState!=sc.state) g_controllerProfileEpoch.fetch_add(1,std::memory_order_acq_rel);
                g_sessionState = sc.state;
                g_sessionStateShared.store(static_cast<int>(sc.state),
                                           std::memory_order_relaxed);
                strcpy_s(g_status.sessionState, SessionStateName(sc.state));
                LOG("XR session state -> %s", SessionStateName(sc.state));
                if (sc.state != XR_SESSION_STATE_FOCUSED)
                {
                    g_thumbrestDpadSampleMs.store(0, std::memory_order_release);
                    StopControllerHaptics();
                }
                if (sc.state == XR_SESSION_STATE_READY)
                {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XrResult r = xrBeginSession(g_session, &bi);
                    if (XR_SUCCEEDED(r))
                    {
                        g_framePacingSessionEpoch.fetch_add(
                            1, std::memory_order_release);
                        if (StartFrameWaitThread())
                        {
                            g_sessionRunning = true;
                            // Publish only after xrBeginSession and the exclusive
                            // frame-wait worker are both live. SteamVR can accept
                            // earlier swapchains yet present them black.
                            g_authoredReticlePreparationReady.store(
                                true, std::memory_order_release);
                        }
                        else
                        {
                            EnterFrameWaitFatalDrain(
                                "The exclusive OpenXR frame-wait worker could "
                                "not start");
                        }
                    }
                    else
                        LOG("xrBeginSession failed: %s", XrStr(r));
                }
                else if (sc.state == XR_SESSION_STATE_STOPPING)
                {
                    const char* fatalExitReason =
                        g_frameWaitFatalExitReason;
                    StopControllerHaptics();
                    EndPreparedFrameWithoutLayers("session stopping");
                    g_authoredReticlePreparationReady.store(
                        false, std::memory_order_release);
                    g_sessionRunning = false;
                    if (!StopFrameWaitThread())
                    {
                        Game_DetachForVrRuntimeFailure();
                        Fail("The OpenXR frame-wait worker did not stop; refusing "
                             "to end the session while a frame call may be active");
                        break;
                    }
                    const XrResult endResult = xrEndSession(g_session);
                    if (XR_FAILED(endResult))
                        LOG("xrEndSession failed: %s", XrStr(endResult));
                    if (fatalExitReason)
                    {
                        g_frameWaitFatalExitReason = nullptr;
                        g_frameWaitExitRequestAccepted = false;
                        g_frameWaitExitLastRequestMs = 0;
                        g_frameWaitExitLastLogMs = 0;
                        Game_DetachForVrRuntimeFailure();
                        Fail(fatalExitReason);
                    }
                }
                else if (sc.state == XR_SESSION_STATE_EXITING || sc.state == XR_SESSION_STATE_LOSS_PENDING)
                {
                    StopControllerHaptics();
                    ResetPreparedFrame();
                    g_authoredReticlePreparationReady.store(
                        false, std::memory_order_release);
                    g_sessionRunning = false;
                    StopFrameWaitThread();
                    Fail("The VR runtime ended the session (headset off / SteamVR closed?)");
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                StopControllerHaptics();
                ResetPreparedFrame();
                g_authoredReticlePreparationReady.store(
                    false, std::memory_order_release);
                g_sessionRunning = false;
                StopFrameWaitThread();
                Fail("The OpenXR runtime is shutting down");
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            {
                auto logProfile = [](XrPath hand, const char* label, unsigned index) {
                    g_controllerProfiles[index].store(vr_mapping::Profile::Unknown,std::memory_order_release);
                    XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
                    if (XR_FAILED(xrGetCurrentInteractionProfile(g_session, hand, &state)) ||
                        state.interactionProfile == XR_NULL_PATH)
                    {
                        LOG("controller profile %s: unavailable", label);
                        return;
                    }
                    char path[XR_MAX_PATH_LENGTH]{};
                    uint32_t written = 0;
                    if (XR_SUCCEEDED(xrPathToString(g_instance, state.interactionProfile,
                        (uint32_t)sizeof(path), &written, path)))
                    {
                        g_controllerProfiles[index].store(vr_mapping::DetectProfile(path),std::memory_order_release);
                        LOG("controller profile %s: %s", label, path);
                    }
                };
                logProfile(g_leftHandPath, "left",0);
                logProfile(g_rightHandPath, "right",1);
                g_controllerProfileEpoch.fetch_add(1,std::memory_order_acq_rel);
                if (g_actMenu != XR_NULL_HANDLE)
                {
                    XrBoundSourcesForActionEnumerateInfo info{
                        XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
                    info.action = g_actMenu;
                    uint32_t count = 0;
                    if (XR_SUCCEEDED(xrEnumerateBoundSourcesForAction(
                            g_session, &info, 0, &count, nullptr)) && count > 0)
                    {
                        std::vector<XrPath> sources(count);
                        if (XR_SUCCEEDED(xrEnumerateBoundSourcesForAction(
                                g_session, &info, count, &count, sources.data())))
                        {
                            for (XrPath source : sources)
                            {
                                char path[XR_MAX_PATH_LENGTH]{};
                                uint32_t written = 0;
                                if (XR_SUCCEEDED(xrPathToString(g_instance, source,
                                        (uint32_t)sizeof(path), &written, path)))
                                    LOG("Menu/Start bound source: %s", path);
                            }
                        }
                    }
                    else
                        LOG("Menu/Start bound source: none");
                }
                break;
            }
            default:
                break;
            }
            ev = {XR_TYPE_EVENT_DATA_BUFFER};
        }

        // A structural frame-loop failure disables new frame calls but keeps
        // event polling alive until the runtime accepts the exit request and
        // reports STOPPING. Retry a rejected request without flooding the log.
        if (g_state == State::Ready && g_frameWaitFatalExitReason &&
            !g_frameWaitExitRequestAccepted &&
            g_sessionState != XR_SESSION_STATE_STOPPING &&
            g_sessionState != XR_SESSION_STATE_EXITING &&
            g_sessionState != XR_SESSION_STATE_LOSS_PENDING)
        {
            const uint64_t nowMs = GetTickCount64();
            if (!g_frameWaitExitLastRequestMs ||
                nowMs - g_frameWaitExitLastRequestMs >= 250)
            {
                g_frameWaitExitLastRequestMs = nowMs;
                const XrResult exitResult = xrRequestExitSession(g_session);
                if (XR_SUCCEEDED(exitResult))
                {
                    g_frameWaitExitRequestAccepted = true;
                }
                else if (!g_frameWaitExitLastLogMs ||
                         nowMs - g_frameWaitExitLastLogMs >= 2000)
                {
                    g_frameWaitExitLastLogMs = nowMs;
                    LOG("pacing: fatal frame-loop drain is retrying "
                        "xrRequestExitSession: %s", XrStr(exitResult));
                }
            }
        }
    }

    bool TryRecenter(XrTime time)
    {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(g_viewSpace, g_localSpace, time, &loc)))
            return false;
        constexpr XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if ((loc.locationFlags & need) != need)
            return false;
        // Keep only the yaw (left/right) part of the head orientation so the
        // screen is level, straight ahead of wherever the user is facing.
        const XrVector3f fwd = Rotate(loc.pose.orientation, {0, 0, -1});
        float yaw = 0.0f;
        if (fabsf(fwd.x) > 1e-4f || fabsf(fwd.z) > 1e-4f)
            yaw = atan2f(-fwd.x, -fwd.z);
        g_centerRot = {0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f)};
        g_centerPos = loc.pose.position;
        g_haveCenter = true;
        LOG("screen recentered (yaw %.1f deg)", yaw * 57.2958f);
        return true;
    }

    // Store the head pose for the game camera hook to read. Called once near
    // the end of Present with the NEXT frame's predicted display time, so Halo
    // renders the upcoming image from its matching pose instead of a stale one.
    bool CaptureHeadPose(XrTime time)
    {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(g_viewSpace, g_localSpace, time, &loc)))
        { g_roomscaleHeadSampleMs.store(0, std::memory_order_release); return false; }
        constexpr XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if ((loc.locationFlags & need) != need)
        { g_roomscaleHeadSampleMs.store(0, std::memory_order_release); return false; }
        if (!NormalizeTrackedPose(loc.pose))
        { g_roomscaleHeadSampleMs.store(0, std::memory_order_release); return false; }
        EnterCriticalSection(&g_headCs);
        // Filter exactly once per OpenXR frame. CamCopyHook can run several
        // times inside that frame, so smoothing there would compound and vary
        // with Halo's number of camera passes.
        const float smoothing = std::clamp(g_config.headset_smoothing, 0.0f, 0.10f);
        g_headPose = g_headPoseValid && smoothing > 0.0f
            ? SmoothTrackedPose(loc.pose, g_headPose, smoothing)
            : loc.pose;
        g_headPoseValid = true;
        g_stockHeadPoseTime = time;
        LeaveCriticalSection(&g_headCs);

        constexpr XrSpaceLocationFlags physicalTracking =
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        g_roomscaleHeadSampleMs.store(
            (loc.locationFlags & physicalTracking) == physicalTracking ? GetTickCount64() : 0,
            std::memory_order_release);

        // Runtime proof for headset logs: successful pose sampling must equal
        // the OpenXR/game presentation rate. Camera-copy transforms are logged
        // independently in game.cpp and normally exceed this count.
        static uint64_t rateStartMs = 0;
        static unsigned samples = 0;
        ++samples;
        const uint64_t now = GetTickCount64();
        if (!rateStartMs) rateStartMs = now;
        else if (now - rateStartMs >= 10000)
        {
            LOG("M1 timing: HMD pose samples %.1f/sec (next-display prediction for each game frame)",
                samples * 1000.0 / (now - rateStartMs));
            samples = 0;
            rateStartMs = now;
        }
        return true;
    }

    // Create the crosshair swapchain on first use (lazily, once the session is
    // running — SteamVR presented pre-session eye chains black) and paint the
    // reticle into it a single time.  Four cyan arc segments and short inner
    // ticks echo Halo 3's original blue CHUD reticle, but at a VR-friendly
    // angular size. A dark-blue outline keeps it legible without a black disc.
    // Paint the reticle image in the given color (0-1 per channel). The bright
    // arcs/ticks take the color; the outline is a darkened version of the same
    // hue so it reads at any color. Called on first use and whenever the color
    // changes (user edit, or the enemy-red switch below) — not per frame.
    std::vector<uint32_t> BuildReticlePixels(float cr, float cg, float cb, float opacity)
    {
        std::vector<uint32_t> px(kReticleSize * kReticleSize);
        const float c = (kReticleSize - 1) * 0.5f;
        const float scale = kReticleSize / 64.0f;
        // coverage: 1 inside the shape, 0 outside, ~1px linear edge for AA
        auto cov = [scale](float d, float halfWidth) {
            const float v = (halfWidth - d) * scale + 0.5f;
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };
        // Bright fill = the requested color at full brightness; outline = the
        // same color at ~28% (a legible dark rim without a black disc).
        const float brR=fminf(cr,1.0f)*255.0f, brG=fminf(cg,1.0f)*255.0f,
                    brB=fminf(cb,1.0f)*255.0f;
        const float olR=brR*0.28f, olG=brG*0.28f, olB=brB*0.28f;
        for (uint32_t y = 0; y < kReticleSize; ++y)
            for (uint32_t x = 0; x < kReticleSize; ++x)
            {
                const float dx = (x - c) / scale, dy = (y - c) / scale;
                const float r = sqrtf(dx * dx + dy * dy);
                const float ax=fabsf(dx), ay=fabsf(dy);
                const bool cardinal=ax>ay*1.22f || ay>ax*1.22f;
                const float dRing=fabsf(r-19.0f);
                const float arc=cardinal?cov(dRing,1.55f):0.0f;
                const float arcOutline=cardinal?cov(dRing,3.0f):0.0f;
                const float tickDistance=(ax>ay)?ax:ay;
                const float tickWidth=(ax>ay)?ay:ax;
                const float tick=(tickDistance>=7.0f && tickDistance<=11.5f)?cov(tickWidth,1.2f):0.0f;
                const float tickOutline=(tickDistance>=5.8f && tickDistance<=12.7f)?cov(tickWidth,2.6f):0.0f;
                const float bright=fmaxf(arc,tick);
                const float outline=fmaxf(arcOutline,tickOutline);
                const uint32_t r8=(uint32_t)(opacity*
                    fminf(255.0f,olR*outline+brR*bright)+0.5f);
                const uint32_t g8=(uint32_t)(opacity*
                    fminf(255.0f,olG*outline+brG*bright)+0.5f);
                const uint32_t b8=(uint32_t)(opacity*
                    fminf(255.0f,olB*outline+brB*bright)+0.5f);
                const uint32_t a8=(uint32_t)(opacity*outline*255.0f+0.5f);
                // OpenXR's preferred swapchain is RGBA8 on this runtime.
                px[y*kReticleSize+x]=(a8<<24)|(b8<<16)|(g8<<8)|r8;
            }
        return px;
    }

    bool PaintReticle(float cr, float cg, float cb, float opacity)
    {
        const auto px=BuildReticlePixels(cr,cg,cb,opacity);
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = 1000000000;
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(g_reticleChain, &ai, &idx)) ||
            XR_FAILED(xrWaitSwapchainImage(g_reticleChain, &wi)))
            return false;
        g_context->UpdateSubresource(g_reticleImages[idx], 0, nullptr, px.data(),
                                     kReticleSize * 4, 0);
        xrReleaseSwapchainImage(g_reticleChain, &ri);
        return true;
    }

    bool PrepareDualReticleImage()
    {
        if(g_dualReticleFailed) return false;
        const bool rgba=g_xrFormat==DXGI_FORMAT_R8G8B8A8_UNORM || g_xrFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra=g_xrFormat==DXGI_FORMAT_B8G8R8A8_UNORM || g_xrFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if(!rgba&&!bgra) return false;
        if(g_dualReticleChain==XR_NULL_HANDLE) {
            const uint64_t now=GetTickCount64();
            if(now<g_dualReticleRetryMs) return false;
            g_dualReticleRetryMs=now+5000;
            if(!CreateChain(kReticleSize,kReticleSize,g_dualReticleChain,
                g_dualReticleImages,g_dualReticleRtvs,"independent dual-weapon reticles")) return false;
        }
        const float color[3]{g_config.reticle_r,g_config.reticle_g,g_config.reticle_b};
        const bool dirty=!g_dualReticleReady || color[0]!=g_dualReticleColor[0] ||
            color[1]!=g_dualReticleColor[1] || color[2]!=g_dualReticleColor[2];
        if(!dirty&&!g_dualReticleAcquired) return true;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        wait.timeout=0;
        if(!g_dualReticleAcquired) {
            if(xrAcquireSwapchainImage(g_dualReticleChain,&acquire,&g_dualReticleIndex)!=XR_SUCCESS) return false;
            g_dualReticleAcquired=true;
        }
        const XrResult ready=xrWaitSwapchainImage(g_dualReticleChain,&wait);
        if(ready==XR_TIMEOUT_EXPIRED) return false;
        if(ready!=XR_SUCCESS) {
            g_dualReticleFailed=true;
            LOG("Dual reticles: optional image wait failed (%d); existing crosshair retained",ready);
            return false;
        }
        const bool valid=g_dualReticleIndex<g_dualReticleImages.size();
        if(valid) {
            const auto pixels=BuildReticlePixels(bgra?color[2]:color[0],color[1],bgra?color[0]:color[2],1.f);
            g_context->UpdateSubresource(g_dualReticleImages[g_dualReticleIndex],0,nullptr,pixels.data(),kReticleSize*4,0);
        }
        const XrResult released=xrReleaseSwapchainImage(g_dualReticleChain,&release);
        g_dualReticleAcquired=false;
        if(!valid || released!=XR_SUCCESS) {
            g_dualReticleFailed=true;
            LOG("Dual reticles: optional image release failed (%d); existing crosshair retained",released);
            return false;
        }
        std::copy(std::begin(color),std::end(color),g_dualReticleColor);
        g_dualReticleReady=true;
        return true;
    }

    void ReleaseAuthoredReticleProbe()
    {
        if (g_authoredReticleSrv)
        {
            g_authoredReticleSrv->Release();
            g_authoredReticleSrv = nullptr;
        }
        if (g_authoredReticleGoodTexture)
        {
            // The reticle publish samples this texture, so its cached view
            // holds a reference; drop that before releasing ours.
            ForgetSourceView(g_authoredReticleGoodTexture);
            g_authoredReticleGoodTexture->Release();
            g_authoredReticleGoodTexture = nullptr;
        }
        if (g_authoredReticleProbeStaging)
        {
            g_authoredReticleProbeStaging->Release();
            g_authoredReticleProbeStaging = nullptr;
        }
        if (g_authoredReticleProbeFence)
        {
            g_authoredReticleProbeFence->Release();
            g_authoredReticleProbeFence = nullptr;
        }
        g_authoredReticleProbePending = false;
        g_authoredReticleGoodValid = false;
    }

    bool EnsureAuthoredReticleTexture()
    {
        if (g_authoredReticleTexture && g_authoredReticleRtv)
            return true;
        if (!g_device)
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        desc.Width = kReticleSize;
        desc.Height = kReticleSize;
        // A full mip chain is what makes the content check cheap: the engine
        // downsamples the capture for us and we read one tiny level.
        desc.MipLevels = 0;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        const HRESULT textureResult = g_device->CreateTexture2D(
            &desc, nullptr, &g_authoredReticleTexture);
        const HRESULT rtvResult = SUCCEEDED(textureResult)
            ? g_device->CreateRenderTargetView(
                g_authoredReticleTexture, &rtvDesc,
                &g_authoredReticleRtv)
            : E_FAIL;
        if (FAILED(textureResult) || FAILED(rtvResult))
        {
            static std::atomic<bool> loggedCreationFailure{false};
            if (!loggedCreationFailure.exchange(
                    true, std::memory_order_relaxed))
            {
                LOG("M3: authored crosshair capture target creation failed "
                    "(texture=0x%08X rtv=0x%08X); optional title capture "
                    "will retry without affecting stereo",
                    static_cast<unsigned>(textureResult),
                    static_cast<unsigned>(rtvResult));
            }
            if (g_authoredReticleRtv)
            {
                g_authoredReticleRtv->Release();
                g_authoredReticleRtv = nullptr;
            }
            if (g_authoredReticleTexture)
            {
                g_authoredReticleTexture->Release();
                g_authoredReticleTexture = nullptr;
            }
            return false;
        }

        // The content check and the known-good copy are OPTIONAL. If any of
        // them cannot be created, the mip-chained capture target is torn back
        // down and rebuilt exactly as it was before this candidate - the
        // swapchain copy requires matching mip counts, so a half-built guard
        // must not be left in place. A diagnostic must never be able to take
        // the crosshair down (AGENTS.md failure isolation).
        D3D11_TEXTURE2D_DESC goodDesc = desc;
        goodDesc.MipLevels = 1;
        goodDesc.MiscFlags = 0;
        goodDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_TEXTURE2D_DESC probeDesc{};
        probeDesc.Width = kAuthoredReticleProbeSize;
        probeDesc.Height = kAuthoredReticleProbeSize;
        probeDesc.MipLevels = 1;
        probeDesc.ArraySize = 1;
        probeDesc.Format = desc.Format;
        probeDesc.SampleDesc.Count = 1;
        probeDesc.Usage = D3D11_USAGE_STAGING;
        probeDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        D3D11_QUERY_DESC probeQueryDesc{};
        probeQueryDesc.Query = D3D11_QUERY_EVENT;
        g_authoredReticleProbeUsable =
            SUCCEEDED(g_device->CreateShaderResourceView(
                g_authoredReticleTexture, nullptr, &g_authoredReticleSrv)) &&
            SUCCEEDED(g_device->CreateTexture2D(
                &goodDesc, nullptr, &g_authoredReticleGoodTexture)) &&
            SUCCEEDED(g_device->CreateTexture2D(
                &probeDesc, nullptr, &g_authoredReticleProbeStaging)) &&
            SUCCEEDED(g_device->CreateQuery(
                &probeQueryDesc, &g_authoredReticleProbeFence));
        g_authoredReticleGoodValid = false;

        if (!g_authoredReticleProbeUsable)
        {
            ReleaseAuthoredReticleProbe();
            g_authoredReticleRtv->Release();
            g_authoredReticleRtv = nullptr;
            g_authoredReticleTexture->Release();
            g_authoredReticleTexture = nullptr;
            desc.MipLevels = 1;
            desc.MiscFlags = 0;
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr,
                                                 &g_authoredReticleTexture)) ||
                FAILED(g_device->CreateRenderTargetView(
                    g_authoredReticleTexture, nullptr,
                    &g_authoredReticleRtv)))
            {
                if (g_authoredReticleRtv)
                {
                    g_authoredReticleRtv->Release();
                    g_authoredReticleRtv = nullptr;
                }
                if (g_authoredReticleTexture)
                {
                    g_authoredReticleTexture->Release();
                    g_authoredReticleTexture = nullptr;
                }
                return false;
            }
        }
        LOG("M3: authored crosshair capture target ready (%ux%u); blank-art "
            "guard %s",
            kReticleSize, kReticleSize,
            g_authoredReticleProbeUsable
                ? "armed"
                : "UNAVAILABLE - crosshair keeps its previous behavior");
        return true;
    }

    bool EnsureAuthoredReticleDiscardTexture()
    {
        if (g_authoredReticleDiscardTexture &&
            g_authoredReticleDiscardRtv)
            return true;
        if (!g_device)
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kReticleSize;
        desc.Height = kReticleSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(g_device->CreateTexture2D(
                &desc, nullptr, &g_authoredReticleDiscardTexture)) ||
            FAILED(g_device->CreateRenderTargetView(
                g_authoredReticleDiscardTexture, nullptr,
                &g_authoredReticleDiscardRtv)))
        {
            if (g_authoredReticleDiscardRtv)
            {
                g_authoredReticleDiscardRtv->Release();
                g_authoredReticleDiscardRtv = nullptr;
            }
            if (g_authoredReticleDiscardTexture)
            {
                g_authoredReticleDiscardTexture->Release();
                g_authoredReticleDiscardTexture = nullptr;
            }
            return false;
        }
        LOG("M3: authored crosshair suppression target ready (%ux%u)",
            kReticleSize, kReticleSize);
        return true;
    }

    // How much crosshair ink does the capture hold? Returns the summed alpha
    // of the downsampled capture. Once a valid image is held, the readback is
    // enqueued then polled on later frames: this returns the explicit pending
    // sentinel instead of ever stalling the headset render thread.
    uint32_t MeasureAuthoredReticleCoverage()
    {
        if (!g_authoredReticleProbeUsable || !g_context ||
            !g_authoredReticleSrv || !g_authoredReticleProbeStaging ||
            !g_authoredReticleProbeFence || !g_authoredReticleTexture)
        {
            return 0;
        }

        D3D11_TEXTURE2D_DESC desc{};
        g_authoredReticleTexture->GetDesc(&desc);
        uint32_t probeMip = 0;
        uint32_t width = desc.Width;
        while (width > kAuthoredReticleProbeSize && probeMip + 1 < desc.MipLevels)
        {
            width >>= 1;
            ++probeMip;
        }
        if (width != kAuthoredReticleProbeSize)
            return 0;

        if (g_authoredReticleGoodValid)
        {
            if (!g_authoredReticleProbePending)
            {
                g_context->GenerateMips(g_authoredReticleSrv);
                g_context->CopySubresourceRegion(
                    g_authoredReticleProbeStaging, 0, 0, 0, 0,
                    g_authoredReticleTexture, probeMip, nullptr);
                g_context->End(g_authoredReticleProbeFence);
                g_authoredReticleProbePending = true;
                return kAuthoredReticleCoveragePending;
            }

            BOOL complete = FALSE;
            if (g_context->GetData(g_authoredReticleProbeFence, &complete,
                                   sizeof(complete),
                                   D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                !complete)
            {
                return kAuthoredReticleCoveragePending;
            }
            g_authoredReticleProbePending = false;
        }
        else
        {
            // Bootstrap once so the player receives art before the defer policy
            // has a known-good image to hold.
            g_context->GenerateMips(g_authoredReticleSrv);
            g_context->CopySubresourceRegion(
                g_authoredReticleProbeStaging, 0, 0, 0, 0,
                g_authoredReticleTexture, probeMip, nullptr);
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(g_authoredReticleProbeStaging, 0,
                                  D3D11_MAP_READ, 0, &mapped)))
        {
            return 0;
        }
        uint32_t alphaInk = 0;
        uint32_t colorInk = 0;
        const auto* rows = static_cast<const uint8_t*>(mapped.pData);
        for (uint32_t y = 0; y < kAuthoredReticleProbeSize; ++y)
        {
            const uint8_t* texel = rows + static_cast<size_t>(y) * mapped.RowPitch;
            for (uint32_t x = 0; x < kAuthoredReticleProbeSize; ++x, texel += 4)
            {
                alphaInk += texel[3];
                colorInk += static_cast<uint32_t>(texel[0]) + texel[1] +
                    texel[2];
            }
        }
        g_context->Unmap(g_authoredReticleProbeStaging, 0);
        g_authoredReticleLastAlphaInk=alphaInk;
        g_authoredReticleLastColorInk=colorInk;
        // CE's native bitmap pass writes RGB only. Like Halo 4's CUI, its
        // transparent capture cannot be judged by the untouched alpha plane.
        // This is independently proven by CE's native blend descriptor.
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
            return halo_ce::ReticleVisibleInk(alphaInk,colorInk);
        // Other titles retain their accepted coverage policy.
        return VrResolveAuthoredReticleCoverage(
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4,
            alphaInk, colorInk);
    }
    bool EnsureReticleChain()
    {
        if (g_reticleChainFailed.load(std::memory_order_acquire))
            return false;
        if (g_reticleChain == XR_NULL_HANDLE)
        {
            if (!CreateChain(kReticleSize, kReticleSize, g_reticleChain, g_reticleImages,
                             g_reticleRtvs, "crosshair"))
            {
                g_reticleChainFailed.store(true, std::memory_order_release);
                return false;
            }
        }
        const GameTitle reticleTitle = TitleAdapter_GetActiveTitle();
        const bool titleHasAuthoredCapture =
            Game_TitleCapturesAuthoredCrosshair();
        // A live optional Halo 4 capture hook is not proof that valid authored
        // pixels have reached this swapchain. On its bootstrap frame we must
        // paint the procedural gun-ray reticle before a deferred or rejected
        // upload can leave the newly-created swapchain as undefined pixels.
        const bool halo4ProceduralBootstrap =
            reticleTitle == GameTitle::Halo4 &&
            Halo4CuiReticleUsesProceduralFallback(
                titleHasAuthoredCapture, g_config.crosshair,
                g_config.kill_reticle);
        const bool ceProceduralBootstrap = reticleTitle == GameTitle::HaloCE &&
            halo_ce::ReticleNeedsProceduralBootstrap(
                titleHasAuthoredCapture, g_authoredReticleGoodValid,
                g_reticleContainsAuthored);
        const bool proceduralBootstrap = halo4ProceduralBootstrap || ceProceduralBootstrap;
        const bool authoredThisFrame =
            g_authoredReticleReady &&
            g_authoredReticleSerial == g_preparedFrame.serial;
        if (authoredThisFrame && !proceduralBootstrap)
            return true;

        // Halo can omit the authored widget in some of the repeated FP passes
        // of one displayed frame. Retain the last authored image across that
        // short gap; otherwise the render thread alternates upload/clear and
        // pays a swapchain repaint every frame. A genuine death/loading gap
        // still clears once after this small grace window.
        // This must comfortably exceed the authored upload's frame cap. At 2,
        // the grace expired between capped uploads, the procedural reticle was
        // repainted over the authored art, and the next upload put it back -
        // the two crosshairs visibly alternating at the beat frequency of the
        // two numbers. Holding the last authored image instead is also what
        // removes the per-frame swapchain repaint this comment warns about,
        // which is the same cost Halo 3 and ODST pay.
        constexpr uint64_t kAuthoredReticleGraceFrames = 24;
        const bool authoredCaptureRecent = g_authoredReticleSerial != 0 &&
            g_preparedFrame.serial >= g_authoredReticleSerial &&
            g_preparedFrame.serial - g_authoredReticleSerial <=
                kAuthoredReticleGraceFrames;
        if (g_reticleContainsAuthored && authoredCaptureRecent && !ceProceduralBootstrap)
            return true;

        // Once the swapchain holds real authored art, LEAVE IT ALONE. For a
        // title that captures its crosshair, repainting can only ever destroy
        // that art - it paints the procedural reticle, which for these titles
        // is fully transparent, so the crosshair simply vanishes until the next
        // upload restores it. That is the flashing, and the repaint is also
        // pure per-frame cost for something that does not change.
        //
        // Reach legitimately stops drawing its crosshair during reloads, melee
        // and similar, which made the old grace window expire and wipe the art
        // mid-fight. Holding the last art is both correct and free; a genuine
        // change (weapon swap, zoom, colour) re-uploads through the key path.
        if (g_reticleContainsAuthored && titleHasAuthoredCapture && !ceProceduralBootstrap)
            return true;

        // Halo can stop drawing its authored widget during death and other
        // non-gameplay states. Keep the old procedural fallback fully
        // transparent so it cannot appear close to the viewer; authored
        // crosshairs use UploadAuthoredReticle below and are unaffected.
        // The procedural reticle is normally transparent: titles with the
        // authored CHUD capture (Halo 3) get their visible crosshair from
        // UploadAuthoredReticle, and a visible procedural fallback could flash
        // during death/loading gaps. The private ODST camera core installs no
        // authored capture yet, so there the procedural reticle IS the
        // crosshair and must be opaque to be seen at all.
        // Reach DOES capture authored art now - its crosshair widgets are
        // found through the owning collection's scripting class rather than a
        // class-2 gate - so it is in the same case as Halo 3: the authored
        // widget is the crosshair and the procedural one must stay invisible.
        // While Reach was still listed here as having no capture, it painted
        // the procedural reticle fully opaque into the SAME swapchain the
        // authored art lives in, wiping that art out on every repaint. That is
        // what showed the old crosshair, made the two alternate, and cost a
        // swapchain repaint per frame.
        const float kProceduralOpacity =
            titleHasAuthoredCapture && !proceduralBootstrap ? 0.0f : 1.0f;
        const bool enemy = g_reticleEnemy.load(std::memory_order_relaxed);
        const float wantR = enemy ? 1.0f : g_config.reticle_r;
        const float wantG = enemy ? 0.18f : g_config.reticle_g;
        const float wantB = enemy ? 0.14f : g_config.reticle_b;
        // Repaint only when the desired color OR opacity changed (compositor
        // keeps showing the last released image, so a static reticle costs
        // nothing per frame). Opacity is included so a Halo 3 -> ODST transition
        // repaints the swapchain from transparent to visible.
        const bool colorChanged = g_reticleContainsAuthored ||
            g_reticleEnemyPainted != enemy ||
            g_reticlePaintedOpacity != kProceduralOpacity ||
            (!enemy && (g_reticlePaintedColor[0] != g_config.reticle_r ||
                        g_reticlePaintedColor[1] != g_config.reticle_g ||
                        g_reticlePaintedColor[2] != g_config.reticle_b));
        if (!colorChanged)
            return true;
        const bool clearingAuthored = g_reticleContainsAuthored;
        if (!PaintReticle(wantR, wantG, wantB, kProceduralOpacity))
        {
            g_reticleChainFailed.store(true, std::memory_order_release);
            return false;
        }
        g_reticlePaintedColor[0]=wantR;
        g_reticlePaintedColor[1]=wantG;
        g_reticlePaintedColor[2]=wantB;
        g_reticlePaintedOpacity=kProceduralOpacity;
        g_reticleEnemyPainted=enemy;
        g_reticleContainsAuthored=false;
        if (clearingAuthored)
            LOG("M3: authored crosshair cleared after capture stopped");
        return true;
    }

    bool UploadAuthoredReticle(bool requireSuccessfulRelease,
                               bool identityChanged)
    {
        // Stage 3BS: Halo 4 publishes the fixed sentinel identity 1 while its
        // legitimate bloom and target tint can swing ink by more than 10x.
        // Treat each H4 sample like a fresh identity so the shared half-ink
        // guard cannot freeze animation. The absolute nonblank bar remains.
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
            identityChanged = true;
        const bool probePending = g_authoredReticleProbePending;
        if ((!probePending && (!g_authoredReticleReady ||
                               g_authoredReticleSerial != g_preparedFrame.serial)) ||
            !g_authoredReticleTexture ||
            g_reticleChain == XR_NULL_HANDLE)
            return false;
        g_authoredReticleHeldBlank = false;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
            (!g_authoredReticleProbeUsable || !g_authoredReticleGoodTexture))
        {
            // Optional capture measurement failure keeps CE's visible gun-ray
            // fallback. Never label an unmeasured texture as authored artwork.
            g_authoredReticleHeldBlank = true;
            return false;
        }
        if (!probePending &&
            g_authoredReticleUploadedSerial == g_authoredReticleSerial)
            return true;

        // Decide what this upload is allowed to publish BEFORE touching the
        // swapchain. A capture with visible pixels becomes the new known-good
        // art; one without is refused outright, so the quad keeps showing the
        // last crosshair the player could actually see instead of the blank
        // the engine just handed us.
        ID3D11Texture2D* source = g_authoredReticleTexture;
        if (g_authoredReticleProbeUsable && g_authoredReticleGoodTexture)
        {
            const uint32_t ink = MeasureAuthoredReticleCoverage();
            if (ink == kAuthoredReticleCoveragePending)
            {
                // The released known-good image remains visible. Defer is not a
                // failure and must never let a blank capture replace it.
                g_authoredReticleHeldBlank = true;
                return false;
            }
            g_authoredReticleLastCoverage = ink;
            // The bar to clear: something at all, and at least half the ink of
            // the crosshair currently on the quad. The second half is what
            // catches the bloom - the petals leaving the crop takes most of
            // the ink with them even when the centre reticle stays behind.
            // ...but only when this is the SAME crosshair. The half-ink bar
            // exists to catch a crosshair blooming out of the crop, which by
            // definition redraws the same widgets and so folds the same
            // identity key. A DIFFERENT crosshair - a weapon swap - legitimately
            // carries less ink, and holding it to the old one's coverage is
            // what left the previous weapon's reticle on the quad. A preserved
            // Halo 3 log measured exactly that: ink fell 446 -> 106 across a
            // switch and the guard held for 24 refusals (~13 s) before letting
            // it through. ODST samples one frame in thirty, so the same 24
            // refusals there are hundreds of frames.
            const uint32_t required =
                (g_authoredReticleGoodValid && !identityChanged)
                ? (g_authoredReticleGoodInk * kAuthoredReticleInkNumerator) /
                      kAuthoredReticleInkDenominator
                : 0;
            const bool hasAnyArt = ink >= kAuthoredReticleArtAlphaThreshold;
            // The escape hatch below exists so a genuinely simpler crosshair
            // (a weapon swap) is never held out forever. It must NEVER apply
            // to a capture holding nothing at all: the 0dab3d7 log caught it
            // doing exactly that - `art 0` with 6 and 7 uploads in the same
            // window - so the guard held the good crosshair 24 times and then
            // published the empty capture anyway, roughly three times a
            // second. An empty capture is never a crosshair, however long it
            // persists; while good art is held it is refused unconditionally.
            // A new crosshair must not inherit refusals accumulated against
            // the previous one: the counter is otherwise cleared only on a
            // successful publish, so a weapon switch could start life most of
            // the way through its own escape hatch.
            if (identityChanged)
                g_authoredReticleConsecutiveHolds = 0;
            const bool staleEnoughToAccept =
                hasAnyArt && g_authoredReticleConsecutiveHolds >=
                                 kAuthoredReticleMaxConsecutiveHolds;
            const bool hasArt =
                hasAnyArt && (ink >= required || staleEnoughToAccept);
            const bool bootstrapNeedsMeasuredArt =
                (TitleAdapter_GetActiveTitle() == GameTitle::Halo4 ||
                 TitleAdapter_GetActiveTitle() == GameTitle::HaloCE) &&
                !g_authoredReticleGoodValid;
            if (!hasArt &&
                (g_authoredReticleGoodValid ||
                 bootstrapNeedsMeasuredArt))
            {
                // Halo 4 and CE also hold before the first known-good capture. A
                // blank or below-threshold bootstrap sample is not authored
                // art and must never replace its procedural fallback with an
                // invisible but "authored" image. The previously shipped
                // titles retain their accepted bootstrap behavior; once any
                // title has good art, this remains the shared bloom guard.
                // This is not a transport failure, so leave the currently
                // released swapchain image untouched.
                ++g_authoredReticleConsecutiveHolds;
                ++g_authoredReticleBlankHeld;
                g_authoredReticleHeldBlank = true;
                return false;
            }
            // Publish: the capture is measured good, or its non-empty
            // reduction has persisted long enough to be a real change rather
            // than a bloom. Mip 0 only: the capture carries a mip chain for
            // the check and the swapchain image does not.
            g_context->CopySubresourceRegion(
                g_authoredReticleGoodTexture, 0, 0, 0, 0,
                g_authoredReticleTexture, 0, nullptr);
            g_authoredReticleConsecutiveHolds = 0;
            if (ink >= kAuthoredReticleArtAlphaThreshold)
            {
                g_authoredReticleGoodValid = true;
                g_authoredReticleGoodInk = ink;
            }
            source = g_authoredReticleGoodTexture;
        }

        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const XrResult acquireResult =
            xrAcquireSwapchainImage(g_reticleChain, &acquire, &index);
        if (requireSuccessfulRelease && acquireResult != XR_SUCCESS)
        {
            if (XR_SUCCEEDED(acquireResult))
            {
                (void)RequireExactSwapchainCompletion(
                    acquireResult,
                    "Reach authored-reticle swapchain acquire did not complete");
            }
            return false;
        }
        if (XR_FAILED(acquireResult))
            return false;
        const XrResult waitResult =
            xrWaitSwapchainImage(g_reticleChain, &wait);
        if (requireSuccessfulRelease)
        {
            if (!RequireExactSwapchainCompletion(
                    waitResult,
                    "Reach authored-reticle swapchain wait did not complete"))
            {
                return false;
            }
        }
        else if (XR_FAILED(waitResult))
            return false;

        D3D11_TEXTURE2D_DESC sourceDesc{};
        source->GetDesc(&sourceDesc);
        const bool copied = Blit(source, sourceDesc,
                                 g_reticleImages[index], kReticleSize,
                                 kReticleSize,
                                 GetRtv(
                                     g_reticleImages, g_reticleRtvs, index,
                                     TitleAdapter_GetActiveTitle() ==
                                         GameTitle::Halo4 ||
                                     TitleAdapter_GetActiveTitle() ==
                                         GameTitle::HaloCE), true);
        const XrResult releaseResult =
            xrReleaseSwapchainImage(g_reticleChain, &release);
        const bool released = requireSuccessfulRelease
            ? RequireExactSwapchainCompletion(
                  releaseResult,
                  "Reach authored-reticle swapchain release did not complete")
            : XR_SUCCEEDED(releaseResult);
        if (!copied || ((requireSuccessfulRelease ||
                         TitleAdapter_GetActiveTitle() == GameTitle::HaloCE) && !released))
            return false;
        g_authoredReticleUploadedSerial = g_authoredReticleSerial;
        g_reticleContainsAuthored = true;
        return true;
    }

    XrCompositionLayerQuad MakeQuad(XrSwapchain chain, int32_t imgW, int32_t imgH,
                                    float widthMeters, float distMeters, float yOffset,
                                    XrCompositionLayerFlags flags, bool headLocked,
                                    float xOffset = 0.0f)
    {
        XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = flags;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = chain;
        q.subImage.imageRect = {{0, 0}, {imgW, imgH}};
        q.subImage.imageArrayIndex = 0;
        if (headLocked)
        {
            // Pinned in front of the head (VIEW space); the game camera looks around.
            q.space = g_viewSpace;
            q.pose.orientation = {0, 0, 0, 1};
            q.pose.position = {xOffset, yOffset, -distMeters};
        }
        else
        {
            q.space = g_localSpace;
            q.pose.orientation = g_centerRot;
            const XrVector3f off = Rotate(g_centerRot, {xOffset, yOffset, -distMeters});
            q.pose.position = {g_centerPos.x + off.x, g_centerPos.y + off.y, g_centerPos.z + off.z};
        }
        q.size = {widthMeters, widthMeters * (float)imgH / (float)imgW};
        return q;
    }

    bool PrepareNativeSubtitle(XrCompositionLayerQuad& quad)
    {
        const auto image = NativeSubtitles_Read();
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint64_t now = GetTickCount64();
        if (g_nativeSubtitleFailed || !image ||
            !subtitles::Current(image->title, image->generation, image->expiresAtMs,
                title, TitleAdapter_GetGeneration(title), now) ||
            image->theatre != VR_IsCutsceneTheaterActive() ||
            (image->theatre ? !g_config.cutscene_theater_subtitles : !g_config.vr_gameplay_subtitles)) return false;
        const bool format = g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM ||
            g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
            g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (!format || !image->rgba || image->rgba->size() != image->width * image->height * 4) return false;
        if (g_nativeSubtitleChain == XR_NULL_HANDLE) {
            if (now < g_nativeSubtitleRetryMs) return false;
            g_nativeSubtitleRetryMs = now + 5000;
            if (!CreateChain(image->width,image->height,g_nativeSubtitleChain,
                    g_nativeSubtitleImages,g_nativeSubtitleRtvs,"native subtitles")) return false;
        }
        if (g_nativeSubtitleUploaded != image->revision || g_nativeSubtitleAcquired) {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            // Caption readiness must never delay or reject the working eye pair.
            // A timeout retains the acquire and retries its wait next frame.
            wait.timeout = 0;
            if (!g_nativeSubtitleAcquired) {
                const XrResult r=xrAcquireSwapchainImage(g_nativeSubtitleChain,&acquire,&g_nativeSubtitleAcquiredIndex);
                if (r != XR_SUCCESS) return false;
                g_nativeSubtitleAcquired=true;
            }
            const XrResult ready=xrWaitSwapchainImage(g_nativeSubtitleChain,&wait);
            if (ready == XR_TIMEOUT_EXPIRED) return false;
            if (ready != XR_SUCCESS) {
                g_nativeSubtitleFailed=true;
                LOG("Native subtitles: optional swapchain wait failed (%d); stock captions and VR retained",ready);
                return false;
            }
            const bool valid=g_nativeSubtitleAcquiredIndex<g_nativeSubtitleImages.size();
            if (valid) g_context->UpdateSubresource(g_nativeSubtitleImages[g_nativeSubtitleAcquiredIndex],
                0,nullptr,image->rgba->data(),image->width*4,0);
            const XrResult released=xrReleaseSwapchainImage(g_nativeSubtitleChain,&release);
            g_nativeSubtitleAcquired=false;
            if (!valid || released != XR_SUCCESS) {
                g_nativeSubtitleFailed=true;
                LOG("Native subtitles: optional swapchain release failed (%d, image=%d); stock captions and VR retained",released,valid?1:0);
                return false;
            }
            g_nativeSubtitleUploaded=image->revision;
        }
        const float scale=image->theatre?g_config.vr_theatre_subtitle_scale:g_config.vr_gameplay_subtitle_scale;
        const float x=image->theatre?g_config.vr_theatre_subtitle_x:g_config.vr_gameplay_subtitle_x;
        const float y=image->theatre?g_config.vr_theatre_subtitle_y:g_config.vr_gameplay_subtitle_y;
        const bool head=image->theatre?g_config.vr_theatre_subtitle_anchor==1:g_config.vr_gameplay_subtitle_anchor==0;
        const float distance=image->theatre?g_config.cutscene_theater_distance_m:2.0f;
        quad=MakeQuad(g_nativeSubtitleChain,image->width,image->height,1.5f*scale,
            distance,-0.55f+y,XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,head,x);
        g_nativeTheatreCaptionReady=image->theatre;
        return true;
    }

    float UpdatePauseTransition()
    {
        enum class Phase { Idle, FadeOut, FadeIn };
        static Phase phase = Phase::Idle;
        static bool targetPaused = false;
        static uint64_t phaseStartMs = 0;
        const uint64_t now = GetTickCount64();
        const int requested = g_pauseRequest.exchange(-1);
        if (requested >= 0)
        {
            targetPaused = requested != 0;
            phase = Phase::FadeOut;
            phaseStartMs = now;
            g_requestedHaptics = 0.0f;
            g_peakHaptics = 0.0f;
            g_contactHaptics[0] = 0.0f;
            g_contactHaptics[1] = 0.0f;
            LOG("pause transition: fade out -> %s",
                targetPaused ? "head-locked 2D" : "stereo 3D");
        }
        if (phase == Phase::Idle)
            return 0.0f;

        constexpr float fadeMs = 200.0f;
        float t = static_cast<float>(now - phaseStartMs) / fadeMs;
        if (phase == Phase::FadeOut)
        {
            if (t < 1.0f)
                return t;
            g_pausePresentation = targetPaused;
            if (!targetPaused)
            {
                Game_Recenter();
            }
            phase = Phase::FadeIn;
            phaseStartMs = now;
            LOG("pause transition: presentation switched to %s",
                targetPaused ? "head-locked 2D" : "stereo 3D");
            return 1.0f;
        }
        if (t < 1.0f)
            return 1.0f - t;
        phase = Phase::Idle;
        LOG("pause transition: comfort fade complete");
        return 0.0f;
    }

    bool AppendComfortFade(float alpha, XrCompositionLayerQuad& quad,
                           std::vector<XrCompositionLayerBaseHeader*>& layers)
    {
        if (alpha <= 0.001f)
            return true;
        if (g_fadeChain == XR_NULL_HANDLE &&
            !CreateChain(4, 4, g_fadeChain, g_fadeImages, g_fadeRtvs, "comfort fade"))
            return false;

        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(g_fadeChain, &acquire, &idx)) ||
            XR_FAILED(xrWaitSwapchainImage(g_fadeChain, &wait)))
            return false;
        const float black[4] = {0.0f, 0.0f, 0.0f,
            std::clamp(alpha, 0.0f, 1.0f)};
        g_context->ClearRenderTargetView(GetRtv(g_fadeImages, g_fadeRtvs, idx), black);
        xrReleaseSwapchainImage(g_fadeChain, &release);
        quad = MakeQuad(g_fadeChain, 4, 4, 20.0f, 0.25f, 0.0f,
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
            true);
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
        return true;
    }

    // ---------------------------------------------------------------- init

    // SteamVR fronts every streamed headset, so "runtime: SteamVR/OpenXR" with a
    // system name of "oculus" is what Meta Link (wired or Air) and ALVR BOTH
    // report -- two different pipelines, different failure modes, and nothing in
    // the log to tell them apart. On 2026-07-28 that cost a real finding: a
    // session that spawned upside down could not be attributed to either, and
    // the log had already rotated away before it could be re-run.
    //
    // The streaming host is its own process, so name the ones that are running.
    // Deliberately "present", not "in use": Meta's OVRServer runs whenever the
    // Meta software is installed and started, even while you stream over
    // something else. One name here plus the headset line settles a session;
    // several means ask which was used. Every name below was read off a live
    // process or install on the development machine -- none are guessed, and a
    // streamer with no distinct process of its own (Steam Link streams from
    // Steam itself) is deliberately absent rather than matched on a guess.
    void LogStreamingHosts()
    {
        struct StreamingHost
        {
            const wchar_t* exe;
            const char* name;
        };
        static constexpr StreamingHost kHosts[] = {
            {L"ALVR Dashboard.exe", "ALVR"},
            {L"VirtualDesktop.Streamer.exe", "Virtual Desktop"},
            {L"OVRServer_x64.exe", "Meta Link / Air Link (Oculus runtime)"},
        };
        constexpr size_t kHostCount = sizeof(kHosts) / sizeof(kHosts[0]);

        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            LOG("streaming host: could not enumerate processes (%lu), so this "
                "session cannot be told apart from another on the same runtime",
                static_cast<unsigned long>(GetLastError()));
            return;
        }
        bool found[kHostCount] = {};
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry))
        {
            do
            {
                for (size_t i = 0; i < kHostCount; ++i)
                    if (_wcsicmp(entry.szExeFile, kHosts[i].exe) == 0)
                        found[i] = true;
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);

        char list[256] = {};
        for (size_t i = 0; i < kHostCount; ++i)
        {
            if (!found[i])
                continue;
            if (list[0])
                strcat_s(list, ", ");
            strcat_s(list, kHosts[i].name);
        }
        if (list[0])
            LOG("streaming host present: %s", list);
        else
            LOG("streaming host present: none recognised (a tethered headset, "
                "or a streamer with no process of its own)");
    }

    // Part 1 (background thread): create the OpenXR instance and find the
    // headset. No D3D device needed here, so it can run while the game loads.
    bool InitInstance()
    {
        uint32_t extensionCount = 0;
        std::vector<XrExtensionProperties> availableExtensions;
        if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(
                nullptr, 0, &extensionCount, nullptr)) && extensionCount > 0)
        {
            availableExtensions.resize(extensionCount);
            for (auto& extension : availableExtensions)
                extension.type = XR_TYPE_EXTENSION_PROPERTIES;
            xrEnumerateInstanceExtensionProperties(nullptr, extensionCount,
                &extensionCount, availableExtensions.data());
        }
        auto hasExtension = [&](const char* name) {
            return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                [&](const XrExtensionProperties& extension) {
                    return strcmp(extension.extensionName, name) == 0;
                });
        };

        std::vector<const char*> enabledExtensions{
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        g_touchProProfileEnabled =
            hasExtension(XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME);
        if (g_touchProProfileEnabled)
            enabledExtensions.push_back(XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME);
        // Read-only: lets us log the headset's REAL panel rate. xrWaitFrame's
        // predictedDisplayPeriod reports the rate the runtime is TARGETING the
        // app at, which is a fraction of the panel rate whenever SteamVR engages
        // reprojection (120 panel -> 60 or 40 targeted). Logging both side by
        // side is the only way to tell "the headset is slow" apart from "the
        // runtime halved us", which look identical from the period alone.
        g_refreshRateExtEnabled =
            hasExtension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if (g_refreshRateExtEnabled)
            enabledExtensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

        XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ici.applicationInfo.applicationName, "HaloMCCVR");
        ici.applicationInfo.applicationVersion = 1;
        strcpy_s(ici.applicationInfo.engineName, "halo3xr");
        ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        ici.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        ici.enabledExtensionNames = enabledExtensions.data();
        LOG("creating OpenXR instance (this can take a while as SteamVR starts)...");
        XrResult r = xrCreateInstance(&ici, &g_instance);
        if (XR_FAILED(r))
        {
            Fail("No OpenXR runtime available. Is SteamVR installed and set as the\n"
                 "default OpenXR runtime? (SteamVR -> Settings -> OpenXR)", r);
            return false;
        }

        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        xrGetInstanceProperties(g_instance, &ip);
        snprintf(g_status.runtime, sizeof(g_status.runtime), "%s %u.%u.%u", ip.runtimeName,
                 XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
                 XR_VERSION_PATCH(ip.runtimeVersion));
        LOG("OpenXR runtime: %s", g_status.runtime);
        LOG("Quest Touch Pro interaction profile: %s",
            g_touchProProfileEnabled ? "enabled" : "not advertised by runtime");

        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        r = xrGetSystem(g_instance, &sgi, &g_systemId);
        // The launcher injects the mod at process start, before the game window
        // exists, so the runtime and headset are often still coming online the
        // first time we ask. XR_ERROR_FORM_FACTOR_UNAVAILABLE means "no HMD yet",
        // which is transient, so poll for up to ~60s instead of giving up on the
        // first race. This runs on the background init thread and never blocks
        // the game; a headset connected shortly after launch still brings up VR.
        for (int waited = 0;
             r == XR_ERROR_FORM_FACTOR_UNAVAILABLE && waited < 60;
             ++waited)
        {
            if (waited % 10 == 0)
                LOG("waiting for a headset on OpenXR runtime '%s' "
                    "(connect it now if it is off)...", g_status.runtime);
            Sleep(1000);
            r = xrGetSystem(g_instance, &sgi, &g_systemId);
        }
        if (XR_FAILED(r))
        {
            char msg[320];
            snprintf(msg, sizeof(msg),
                "No headset found on the active OpenXR runtime '%s' after 60s.\n\n"
                "The mod uses whichever runtime owns OpenXR. If your headset runs\n"
                "through SteamVR (PSVR2, Index, Vive, etc.), set SteamVR -> Settings\n"
                "-> OpenXR -> Set SteamVR as OpenXR Runtime. If you stream with\n"
                "Virtual Desktop, connect it before launching.",
                g_status.runtime);
            Fail(msg, r);
            return false;
        }
        // Record WHICH headset this is, not just which runtime. SteamVR fronts
        // PSVR2, Index, Vive, Virtual Desktop, Steam Link and ALVR alike, so
        // "OpenXR runtime: SteamVR/OpenXR" identifies almost nothing. On
        // 2026-07-28 that cost hours: preserved logs were attributed to a
        // headset from a code comment rather than evidence, and two theories
        // were built on the wrong attribution before the user caught it. A log
        // that cannot tell its own sessions apart cannot settle an argument.
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(
                xrGetSystemProperties(g_instance, g_systemId, &systemProperties)))
        {
            LOG("headset: '%s' (vendor 0x%04X) on runtime %s",
                systemProperties.systemName, systemProperties.vendorId,
                g_status.runtime);
        }
        else
        {
            // Not fatal and not silent: every session must still say something
            // about which headset produced it.
            LOG("headset: model NOT reported by runtime %s; identify this "
                "session by its per-eye FOV instead", g_status.runtime);
        }
        LogStreamingHosts();
        LOG("OpenXR instance ready; headset found on runtime %s", g_status.runtime);
        return true;
    }

    // M3 gamepad-replacement actions: sticks, buttons, triggers, grips.
    XrAction g_actMove = XR_NULL_HANDLE, g_actTurn = XR_NULL_HANDLE;
    XrAction g_actTrigL = XR_NULL_HANDLE, g_actTrigR = XR_NULL_HANDLE;
    XrAction g_actGripL = XR_NULL_HANDLE, g_actGripR = XR_NULL_HANDLE;
    XrAction g_actA = XR_NULL_HANDLE, g_actB = XR_NULL_HANDLE;
    XrAction g_actX = XR_NULL_HANDLE, g_actY = XR_NULL_HANDLE;
    XrAction g_actClickL = XR_NULL_HANDLE, g_actClickR = XR_NULL_HANDLE;
    VrPadState g_padState{};
    float g_nativePointerTrigger = 0;

    bool CreateControllerActions()
    {
        XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(setInfo.actionSetName, "gameplay");
        strcpy_s(setInfo.localizedActionSetName, "Halo MCC VR Gameplay");
        setInfo.priority = 0;
        if (XR_FAILED(xrCreateActionSet(g_instance, &setInfo, &g_gameplayActions)))
        {
            LOG("M3: failed to create OpenXR gameplay action set");
            return false;
        }
        if (XR_FAILED(xrStringToPath(g_instance, "/user/hand/right", &g_rightHandPath)) ||
            XR_FAILED(xrStringToPath(g_instance, "/user/hand/left", &g_leftHandPath)))
            return false;

        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(actionInfo.actionName, "right_aim_pose");
        strcpy_s(actionInfo.localizedActionName, "Right Hand Aim Pose");
        actionInfo.countSubactionPaths = 1;
        actionInfo.subactionPaths = &g_rightHandPath;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &actionInfo, &g_rightAimAction)))
        {
            LOG("M3: failed to create right-controller pose action");
            return false;
        }
        strcpy_s(actionInfo.actionName, "left_aim_pose");
        strcpy_s(actionInfo.localizedActionName, "Left Hand Aim Pose");
        actionInfo.subactionPaths = &g_leftHandPath;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &actionInfo, &g_leftAimAction)))
            g_leftAimAction = XR_NULL_HANDLE; // non-fatal: D-pad gesture falls back to right

        auto makeAction = [&](XrAction& out, XrActionType type, const char* name,
                              const char* label) {
            XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
            ai.actionType = type;
            strcpy_s(ai.actionName, name);
            strcpy_s(ai.localizedActionName, label);
            if (XR_FAILED(xrCreateAction(g_gameplayActions, &ai, &out)))
                out = XR_NULL_HANDLE;
        };
        makeAction(g_actMove,   XR_ACTION_TYPE_VECTOR2F_INPUT, "move",       "Move (left stick)");
        makeAction(g_actTurn,   XR_ACTION_TYPE_VECTOR2F_INPUT, "turn",       "Turn (right stick)");
        makeAction(g_actTrigL,  XR_ACTION_TYPE_FLOAT_INPUT,    "trigger_l",  "Left Trigger");
        makeAction(g_actTrigR,  XR_ACTION_TYPE_FLOAT_INPUT,    "trigger_r",  "Right Trigger");
        makeAction(g_actGripL,  XR_ACTION_TYPE_FLOAT_INPUT,    "grip_l",     "Left Grip");
        makeAction(g_actGripR,  XR_ACTION_TYPE_FLOAT_INPUT,    "grip_r",     "Right Grip");
        makeAction(g_actA,      XR_ACTION_TYPE_BOOLEAN_INPUT,  "btn_a",      "A (right lower)");
        makeAction(g_actB,      XR_ACTION_TYPE_BOOLEAN_INPUT,  "btn_b",      "B (right upper)");
        makeAction(g_actX,      XR_ACTION_TYPE_BOOLEAN_INPUT,  "btn_x",      "X (left lower)");
        makeAction(g_actY,      XR_ACTION_TYPE_BOOLEAN_INPUT,  "btn_y",      "Y (left upper)");
        makeAction(g_actClickL, XR_ACTION_TYPE_BOOLEAN_INPUT,  "click_l",    "Left Stick Click");
        makeAction(g_actClickR, XR_ACTION_TYPE_BOOLEAN_INPUT,  "click_r",    "Right Stick Click");
        makeAction(g_actMenu,   XR_ACTION_TYPE_BOOLEAN_INPUT,  "menu",       "Menu / Start");
        makeAction(g_actFacePadL,XR_ACTION_TYPE_VECTOR2F_INPUT,"face_pad_l","Left Touchpad Face Zones");
        makeAction(g_actFacePadR,XR_ACTION_TYPE_VECTOR2F_INPUT,"face_pad_r","Right Touchpad Face Zones");
        makeAction(g_actFaceClickL,XR_ACTION_TYPE_BOOLEAN_INPUT,"face_click_l","Left Touchpad Face Click");
        makeAction(g_actFaceClickR,XR_ACTION_TYPE_BOOLEAN_INPUT,"face_click_r","Right Touchpad Face Click");
        // Always create the optional action so the F1 checkbox works without
        // recreating a session. Failure must not affect existing controller input.
        makeAction(g_actLeftThumbrest, XR_ACTION_TYPE_BOOLEAN_INPUT,
            "left_thumbrest_touch", "Left Thumb Rest Touch (D-pad)");
        if (g_actLeftThumbrest == XR_NULL_HANDLE)
            LOG("D-pad thumb rest: optional touch action unavailable; head gesture and normal input retained");

        XrPath hapticPaths[2] = {g_leftHandPath, g_rightHandPath};
        XrActionCreateInfo hapticInfo{XR_TYPE_ACTION_CREATE_INFO};
        hapticInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
        strcpy_s(hapticInfo.actionName, "game_haptics");
        strcpy_s(hapticInfo.localizedActionName, "Game Haptics");
        hapticInfo.countSubactionPaths = 2;
        hapticInfo.subactionPaths = hapticPaths;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &hapticInfo, &g_hapticAction)))
        {
            g_hapticAction = XR_NULL_HANDLE;
            LOG("M3: portable haptic output unavailable; controller input remains active");
        }

        // Per-profile suggested bindings. SteamVR remaps these onto PSVR2
        // Sense automatically (users can rebind in SteamVR controller
        // settings); the Touch/Index layouts are the closest templates.
        struct Bind { XrAction action; const char* path; };
        auto suggest = [&](const char* profile, const Bind* binds, size_t count) -> bool {
            XrPath profilePath = XR_NULL_PATH;
            if (XR_FAILED(xrStringToPath(g_instance, profile, &profilePath)))
                return false;
            std::vector<XrActionSuggestedBinding> out;
            for (size_t i = 0; i < count; ++i)
            {
                XrPath p = XR_NULL_PATH;
                if (binds[i].action != XR_NULL_HANDLE &&
                    XR_SUCCEEDED(xrStringToPath(g_instance, binds[i].path, &p)))
                    out.push_back({binds[i].action, p});
            }
            if (out.empty())
                return false;
            XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggestion.interactionProfile = profilePath;
            suggestion.suggestedBindings = out.data();
            suggestion.countSuggestedBindings = (uint32_t)out.size();
            return XR_SUCCEEDED(xrSuggestInteractionProfileBindings(g_instance, &suggestion));
        };

        const Bind touch[] = {
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            {g_actMove,   "/user/hand/left/input/thumbstick"},
            {g_actTurn,   "/user/hand/right/input/thumbstick"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/value"},
            {g_actGripR,  "/user/hand/right/input/squeeze/value"},
            {g_actA,      "/user/hand/right/input/a/click"},
            {g_actB,      "/user/hand/right/input/b/click"},
            {g_actX,      "/user/hand/left/input/x/click"},
            {g_actY,      "/user/hand/left/input/y/click"},
            {g_actClickL, "/user/hand/left/input/thumbstick/click"},
            {g_actClickR, "/user/hand/right/input/thumbstick/click"},
            {g_actMenu,   "/user/hand/left/input/menu/click"},
            {g_actMenu,   "/user/hand/right/input/system/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind index[] = {
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            {g_actMove,   "/user/hand/left/input/thumbstick"},
            {g_actTurn,   "/user/hand/right/input/thumbstick"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/value"},
            {g_actGripR,  "/user/hand/right/input/squeeze/value"},
            {g_actA,      "/user/hand/right/input/a/click"},
            {g_actB,      "/user/hand/right/input/b/click"},
            {g_actX,      "/user/hand/left/input/a/click"},
            {g_actY,      "/user/hand/left/input/b/click"},
            {g_actClickL, "/user/hand/left/input/thumbstick/click"},
            {g_actClickR, "/user/hand/right/input/thumbstick/click"},
            {g_actMenu,   "/user/hand/left/input/system/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind wmr[] = {
            {g_actFacePadL,"/user/hand/left/input/trackpad"},
            {g_actFacePadR,"/user/hand/right/input/trackpad"},
            {g_actFaceClickL,"/user/hand/left/input/trackpad/click"},
            {g_actFaceClickR,"/user/hand/right/input/trackpad/click"},
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            {g_actMove,   "/user/hand/left/input/thumbstick"},
            {g_actTurn,   "/user/hand/right/input/thumbstick"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/click"},
            {g_actGripR,  "/user/hand/right/input/squeeze/click"},
            {g_actClickL, "/user/hand/left/input/thumbstick/click"},
            {g_actClickR, "/user/hand/right/input/thumbstick/click"},
            {g_actMenu,   "/user/hand/left/input/menu/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind vive[] = {
            {g_actFacePadL,"/user/hand/left/input/trackpad"},
            {g_actFacePadR,"/user/hand/right/input/trackpad"},
            {g_actFaceClickL,"/user/hand/left/input/trackpad/click"},
            {g_actFaceClickR,"/user/hand/right/input/trackpad/click"},
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            {g_actMove,   "/user/hand/left/input/trackpad"},
            {g_actTurn,   "/user/hand/right/input/trackpad"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/click"},
            {g_actGripR,  "/user/hand/right/input/squeeze/click"},
            {g_actClickL, "/user/hand/left/input/trackpad/click"},
            {g_actClickR, "/user/hand/right/input/trackpad/click"},
            {g_actMenu,   "/user/hand/left/input/menu/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind simple[] = {
            {g_rightAimAction, "/user/hand/right/input/grip/pose"},
            {g_leftAimAction, "/user/hand/left/input/grip/pose"},
            {g_actTrigR,  "/user/hand/right/input/select/click"},
            {g_actMenu,   "/user/hand/left/input/menu/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const auto suggestTouch = [&](const char* profile) {
            if (g_actLeftThumbrest != XR_NULL_HANDLE)
            {
                std::vector<Bind> withThumbrest(std::begin(touch), std::end(touch));
                withThumbrest.push_back({g_actLeftThumbrest,
                    "/user/hand/left/input/thumbrest/touch"});
                if (suggest(profile, withThumbrest.data(), withThumbrest.size()))
                {
                    LOG("D-pad thumb rest: optional left touch binding accepted for %s", profile);
                    return true;
                }
                LOG("D-pad thumb rest: optional binding rejected for %s; retrying complete original controls", profile);
            }
            // Suggestions replace a profile's complete binding list. Retrying
            // only the new action would discard poses, buttons and haptics.
            return suggest(profile, touch, _countof(touch));
        };
        unsigned accepted = 0;
        if (g_touchProProfileEnabled)
            accepted += suggestTouch("/interaction_profiles/facebook/touch_controller_pro");
        accepted += suggestTouch("/interaction_profiles/oculus/touch_controller");
        accepted += suggest("/interaction_profiles/valve/index_controller", index, _countof(index));
        accepted += suggest("/interaction_profiles/microsoft/motion_controller", wmr, _countof(wmr));
        accepted += suggest("/interaction_profiles/htc/vive_controller", vive, _countof(vive));
        accepted += suggest("/interaction_profiles/khr/simple_controller", simple, _countof(simple));

        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &g_gameplayActions;
        if (XR_FAILED(xrAttachSessionActionSets(g_session, &attach)))
        {
            LOG("M3: failed to attach OpenXR gameplay action set");
            return false;
        }
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = g_rightAimAction;
        spaceInfo.subactionPath = g_rightHandPath;
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(xrCreateActionSpace(g_session, &spaceInfo, &g_rightAimSpace)))
        {
            LOG("M3: failed to create right-controller aim space");
            return false;
        }
        if (g_leftAimAction != XR_NULL_HANDLE)
        {
            spaceInfo.action = g_leftAimAction;
            spaceInfo.subactionPath = g_leftHandPath;
            if (XR_FAILED(xrCreateActionSpace(g_session, &spaceInfo, &g_leftAimSpace)))
                g_leftAimSpace = XR_NULL_HANDLE;
        }
        LOG("M3: right-controller aim action ready (%u interaction profiles accepted)", accepted);
        return true;
    }

    // Two-handed aim state. `latched` = two-hand engaged this frame; decided
    // ONCE per frame in UpdateTwoHandLatch (edge detection can't live in the
    // multi-call aim getter). `active` mirrors it for the menu indicator.
    std::atomic<bool> g_twoHandLatched{false};
    std::atomic<bool> g_twoHandActive{false};
    weapon_interaction::State g_weaponInteraction;
    weapon_model::Observations g_weaponModels;
    weapon_interaction::ReloadTargets g_reloadTargets;
    WeaponAccessoryRenderer g_weaponAccessoryRenderer;
    weapon_accessory::Presentation g_weaponAccessory; // compositor thread only
    std::atomic<HRESULT> g_weaponAccessoryResult{S_FALSE};
    std::atomic<unsigned> g_weaponGestureGrabs{0},g_weaponReloadRequests{0},g_weaponSwapRequests{0};
    std::atomic<unsigned> g_authoredInsertionRequests{0},g_fallbackInsertionRequests{0};
    std::atomic<uint64_t> g_weaponGestureReadyMs{0};
    std::array<RecentSecondaryWeaponPresentation, kTitleRuntimeSlotCount>
        g_secondaryWeaponPresentation;

    bool SecondaryWeaponPresentationActive() noexcept
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const size_t slot = TitleRuntimeSlotIndex(title);
        return slot < kTitleRuntimeSlotCount &&
            g_secondaryWeaponPresentation[slot].Active(
                TitleAdapter_GetGeneration(title), GetTickCount64());
    }

    // Called once per frame from the pose capture. Toggle mode (default): a
    // left-grip press while the left hand is inside the thin/long barrel zone
    // flips two-hand ON; the next left-grip press flips it OFF (anywhere). Hold
    // mode: the zone acquires the hold, which stays engaged until grip release.
    // Physical controller points now own activation and aim independently of
    // rendered hand seating. Keep the old offset helper available for explicit
    // geometry calculations; current aiming inputs pass zero offsets.
    XrVector3f LeftHandPointWithOffsets(
        const XrPosef& lpose, float handForwardM, float gripForwardM)
    {
        const XrVector3f lfwd = Rotate(lpose.orientation, {0,0,-1});
        // Hand-target correction PLUS the rendered wrist-to-palm depth: the
        // two-hand line and grab zone meet the visible PALM, not the wrist
        // bone the hand target anchors (23:26 headset result).
        const float k = std::clamp(handForwardM, -0.15f, 0.30f)
                      + std::clamp(gripForwardM, -0.05f, 0.25f);
        return {lpose.position.x + lfwd.x*k,
                lpose.position.y + lfwd.y*k,
                lpose.position.z + lfwd.z*k};
    }

    XrVector3f LeftHandPoint(const XrPosef& lpose)
    {
        // Grip acquisition uses the same physical point as the aiming line.
        // Moving the rendered support hand cannot engage/disengage aim.
        return lpose.position;
    }

    struct AimPoseInputs
    {
        bool rightValid = false;
        XrPosef right{{0, 0, 0, 1}, {0, 0, 0}};
        bool leftValid = false;
        XrPosef left{{0, 0, 0, 1}, {0, 0, 0}};
        bool twoHandEnabled = false;
        bool twoHandLatched = false;
        float leftHandForwardM = 0.0f;
        float leftGripForwardM = 0.0f;
        float gunYawDeg = 0.0f;
        float gunPitchDeg = 0.0f;
        float gunRollDeg = 0.0f;
        XrVector3f supportPosition{0, 0, 0};
        bool virtualStockEnabled = false;
        float virtualStockStrength = 1.0f;
        float virtualStockRearHeightM = 0.0f;
        int virtualStockRearReference = 0;
        float virtualStockShoulderBackM = 0.005f;
        float virtualStockShoulderSideM = 0.015f;
        float virtualStockChestHeightM = -0.320f;
        float virtualStockChestBackM = 0.000f;
        float virtualStockChestSideM = 0.015f;
        float virtualStockAdaptiveTopHeightM = -0.180f;
        float virtualStockAdaptiveBottomHeightM = -0.450f;
        float virtualStockAdaptiveTopHalfWidthM = 0.080f;
        float virtualStockAdaptiveBottomHalfWidthM = 0.140f;
        bool virtualStockLeftHanded = false;
        bool virtualStockProximityRelease = false;
        float virtualStockProximityFullM = 0.250f;
        float virtualStockProximityReleaseM = 0.450f;
        bool headValid = false;
        XrVector3f headPosition{0, 0, 0};
        XrQuaternionf headOrientation{0, 0, 0, 1};
    };

    struct AimPoseResult
    {
        bool valid = false;
        // False when the right pose itself is invalid; callers then preserve
        // the existing two-hand activity indicator exactly as before.
        bool updateTwoHandActivity = false;
        bool twoHandActive = false;
        bool rejectedExtreme = false;
        float rejectedAgreement = 0.0f;
        XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
    };

    AimPoseInputs CurrentAimPoseInputs(
        bool rightValid, const XrPosef& right,
        bool leftValid, const XrPosef& left) noexcept
    {
        AimPoseInputs inputs{};
        inputs.rightValid = rightValid;
        inputs.right = right;
        inputs.leftValid = leftValid;
        inputs.left = left;
        inputs.twoHandEnabled = g_config.two_handed_aim &&
            !SecondaryWeaponPresentationActive();
        inputs.twoHandLatched = g_twoHandLatched.load();
        // Visual hand seating must not change the two-controller aiming line.
        inputs.leftHandForwardM = 0.0f;
        inputs.leftGripForwardM = 0.0f;
        inputs.gunYawDeg = g_config.gun_yaw_deg;
        inputs.gunPitchDeg = g_config.gun_pitch_deg;
        inputs.gunRollDeg = g_config.gun_roll_deg;
        return inputs;
    }

    #include "virtual_stock_aim.inl"

    // The OpenXR frame thread owns these globals at snapshot publication. The
    // public getter below instead passes one copy taken under g_headCs.
    AimPoseInputs CurrentFrameStockAimPoseInputs(
        bool rightValid, const XrPosef& right,
        bool leftValid, const XrPosef& left) noexcept
    {
        return CurrentStockAimPoseInputs(rightValid, right, leftValid, left,
            g_headPoseValid && g_stockHeadPoseTime != 0 &&
                g_stockHeadPoseTime == g_stockControllerPoseTime,
            g_headPose.position, g_headPose.orientation);
    }

    // Pure aim calculation shared by the lock-taking public getter and Reach's
    // exact-serial snapshot publisher. It reads no globals, takes no locks, and
    // performs no logging or state publication.
    AimPoseResult ComputeAimPose(const AimPoseInputs& inputs) noexcept
    {
        if (inputs.virtualStockEnabled)
            return ComputeStockAimPose(inputs);
        AimPoseResult result{};
        if (!inputs.rightValid)
            return result;

        result.updateTwoHandActivity = true;
        result.pose = inputs.right;

        auto finishAimPose = [&]() {
            auto multiply = [](const XrQuaternionf& a,
                               const XrQuaternionf& b) {
                return XrQuaternionf{
                    a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                    a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                    a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
                    a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
            };
            constexpr float kDegToRad = 0.01745329252f;
            const float yaw = inputs.gunYawDeg * kDegToRad;
            const float pitch = inputs.gunPitchDeg * kDegToRad;
            const float roll = inputs.gunRollDeg * kDegToRad;
            const XrQuaternionf qYaw{
                0.0f, sinf(yaw*0.5f), 0.0f, cosf(yaw*0.5f)};
            const XrQuaternionf qPitch{
                sinf(pitch*0.5f), 0.0f, 0.0f, cosf(pitch*0.5f)};
            const XrQuaternionf qRoll{
                0.0f, 0.0f, sinf(-roll*0.5f), cosf(roll*0.5f)};
            const XrQuaternionf corrected = multiply(
                result.pose.orientation,
                multiply(multiply(qYaw, qPitch), qRoll));
            const float length = sqrtf(
                corrected.x*corrected.x + corrected.y*corrected.y +
                corrected.z*corrected.z + corrected.w*corrected.w);
            if (!std::isfinite(length) || length < 1e-5f)
                return;
            result.pose.orientation = {
                corrected.x/length, corrected.y/length,
                corrected.z/length, corrected.w/length};
            result.valid = true;
        };

        if (!inputs.twoHandEnabled || !inputs.leftValid ||
            !inputs.twoHandLatched)
        {
            finishAimPose();
            return result;
        }

        // Match the activation point: measure the two-hand line to the HAND,
        // not the wrist (same forward shift used by the latch).
        const XrVector3f lp = LeftHandPointWithOffsets(
            inputs.left, inputs.leftHandForwardM,
            inputs.leftGripForwardM);
        const XrQuaternionf rq = inputs.right.orientation;
        const XrVector3f rp = inputs.right.position;
        const XrVector3f rup = Rotate(rq, {0,1,0});
        XrVector3f v{lp.x-rp.x, lp.y-rp.y, lp.z-rp.z};
        const float len = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z);
        if (len < 1e-4f)
        {
            finishAimPose();
            return result;
        }

        XrVector3f af{v.x/len, v.y/len, v.z/len};
        const XrVector3f rawForward = Rotate(rq, {0,0,-1});
        const float agreement =
            af.x*rawForward.x + af.y*rawForward.y + af.z*rawForward.z;
        if (!std::isfinite(agreement) || agreement < 0.35f)
        {
            result.rejectedExtreme = true;
            result.rejectedAgreement = agreement;
            finishAimPose();
            return result;
        }

        auto cross=[](const XrVector3f& a, const XrVector3f& b) {
            return XrVector3f{
                a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z,
                a.x*b.y-a.y*b.x};
        };
        XrVector3f xa = cross(af, rup);
        const float xl = sqrtf(xa.x*xa.x+xa.y*xa.y+xa.z*xa.z);
        if (xl < 1e-4f)
        {
            finishAimPose();
            return result;
        }
        xa = {xa.x/xl, xa.y/xl, xa.z/xl};
        const XrVector3f ya = cross(xa, af);
        const XrVector3f za{-af.x, -af.y, -af.z};

        const float m00=xa.x,m10=xa.y,m20=xa.z;
        const float m01=ya.x,m11=ya.y,m21=ya.z;
        const float m02=za.x,m12=za.y,m22=za.z;
        const float tr=m00+m11+m22;
        float qx,qy,qz,qw;
        if (tr>0)
        {
            const float s=sqrtf(tr+1.0f)*2;
            qw=0.25f*s; qx=(m21-m12)/s;
            qy=(m02-m20)/s; qz=(m10-m01)/s;
        }
        else if (m00>m11 && m00>m22)
        {
            const float s=sqrtf(1.0f+m00-m11-m22)*2;
            qw=(m21-m12)/s; qx=0.25f*s;
            qy=(m01+m10)/s; qz=(m02+m20)/s;
        }
        else if (m11>m22)
        {
            const float s=sqrtf(1.0f+m11-m00-m22)*2;
            qw=(m02-m20)/s; qx=(m01+m10)/s;
            qy=0.25f*s; qz=(m12+m21)/s;
        }
        else
        {
            const float s=sqrtf(1.0f+m22-m00-m11)*2;
            qw=(m10-m01)/s; qx=(m02+m20)/s;
            qy=(m12+m21)/s; qz=0.25f*s;
        }
        const float ql=sqrtf(qx*qx+qy*qy+qz*qz+qw*qw);
        if (ql < 1e-5f)
        {
            finishAimPose();
            return result;
        }
        result.pose.orientation = {
            qx/ql, qy/ql, qz/ql, qw/ql};
        result.twoHandActive = true;
        finishAimPose();
        return result;
    }

    void UpdateTwoHandLatch(bool rightValid, const XrPosef& rpose,
                            bool leftValid, const XrPosef& lpose, float gripL,
                            bool rolesChanged, bool weaponGesture = false)
    {
        // Track the physical grip edge even while tracking/handedness resets
        // invalidate the poses. A held grip is not a new grab on recovery.
        static bool prevGrip = false;
        const bool gripHeld = gripL > 0.5f;
        const bool rising = gripHeld && !prevGrip;
        prevGrip = gripHeld;
        static SupportGripAdmission admission;
        const bool grabAllowed = admission.Observe(
            gripHeld, SecondaryWeaponPresentationActive(), rolesChanged);
        if (!grabAllowed || !g_config.two_handed_aim || !rightValid || !leftValid || weaponGesture)
        {
            g_twoHandLatched.store(false);
            g_twoHandActive.store(false);
            return;
        }
        const XrVector3f rfwd = Rotate(rpose.orientation, {0,0,-1});
        // Grab-zone side nudge: the visible barrel can sit beside the raw aim
        // ray (headset report: the AR's barrel was right of the zone), so the
        // zone axis shifts along the controller's +X by the F1-tuned amount.
        const float zr = std::clamp(g_config.two_hand_zone_right_m, -0.10f, 0.10f);
        const XrVector3f rright = Rotate(rpose.orientation, {1,0,0});
        const XrVector3f origin{rpose.position.x+rright.x*zr,
                                rpose.position.y+rright.y*zr,
                                rpose.position.z+rright.z*zr};
        auto inZoneAt = [&](const XrVector3f& p) -> bool {
            const XrVector3f v{p.x-origin.x, p.y-origin.y, p.z-origin.z};
            const float along = v.x*rfwd.x + v.y*rfwd.y + v.z*rfwd.z;
            const XrVector3f perp{v.x-along*rfwd.x, v.y-along*rfwd.y, v.z-along*rfwd.z};
            const float lateral = sqrtf(perp.x*perp.x+perp.y*perp.y+perp.z*perp.z);
            return along>0.08f && along<0.80f && lateral<0.09f;
        };
        const bool inZone = inZoneAt(LeftHandPoint(lpose));

        if (g_config.two_hand_toggle)
        {
            if (rising)
            {
                if (g_twoHandLatched.load()) g_twoHandLatched.store(false);      // toggle off
                else if (inZone)             g_twoHandLatched.store(true);       // toggle on
            }
        }
        else // hold mode
        {
            g_twoHandLatched.store(UpdateTwoHandHold(
                g_twoHandLatched.load(), gripHeld, inZone));
        }
    }

    void StopControllerHaptics()
    {
        if (g_session == XR_NULL_HANDLE || g_hapticAction == XR_NULL_HANDLE)
            return;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        info.action = g_hapticAction;
        info.subactionPath = g_leftHandPath;
        xrStopHapticFeedback(g_session, &info);
        info.subactionPath = g_rightHandPath;
        xrStopHapticFeedback(g_session, &info);
    }

    void ApplyControllerHaptics(bool trackingValid)
    {
        static bool active[2]{false, false};
        static uint64_t lastApplyMs = 0;
        static RuntimeMode previousMode = RuntimeMode::Shell;
        const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
        const bool modeAllows = mode == RuntimeMode::Gameplay ||
            mode == RuntimeMode::Vehicle || mode == RuntimeMode::Turret;
        const bool capabilityAllows =
            Game_HasTitleCapability(TitleCapability_Haptics);
        if (!capabilityAllows)
        {
            // Stop is not enough: the requested amplitude is persistent. Drop
            // it while ownership/arming is absent so it cannot be replayed
            // after a title transition without a fresh XInput request. The
            // accumulated peak is dropped for the same reason.
            g_requestedHaptics.store(0.0f, std::memory_order_release);
            g_peakHaptics.store(0.0f, std::memory_order_release);
            g_contactHaptics[0].store(0.0f, std::memory_order_release);
            g_contactHaptics[1].store(0.0f, std::memory_order_release);
        }
        const float intensity =
            std::clamp(g_config.haptic_intensity, 0.0f, 1.0f);
        // Peak-hold: peek (without consuming) the max amplitude requested since
        // the last applied frame so a short gunfire pulse (SetState(high) then
        // SetState(0) between two frame samples) cannot be aliased to zero.
        const float latest = capabilityAllows
            ? std::clamp(
                g_requestedHaptics.load(std::memory_order_acquire),
                0.0f, 1.0f)
            : 0.0f;
        const float peekPeak = capabilityAllows
            ? g_peakHaptics.load(std::memory_order_acquire)
            : 0.0f;
        const float contactPeek[2]{
            g_contactHaptics[0].load(std::memory_order_acquire),
            g_contactHaptics[1].load(std::memory_order_acquire)};
        const float gamePeek = SampleHapticPeak(peekPeak, latest).apply;
        const float peekAmplitude[2]{
            MergeHapticAmplitude(gamePeek, contactPeek[0]) * intensity,
            MergeHapticAmplitude(gamePeek, contactPeek[1]) * intensity};
        const bool anyAmplitude =
            peekAmplitude[0] > 0.0f || peekAmplitude[1] > 0.0f;
        const bool feedbackUnavailable = !trackingValid || !modeAllows ||
            !capabilityAllows || Menu_IsOpen() ||
            g_sessionState != XR_SESSION_STATE_FOCUSED;
        const bool mustStop = !anyAmplitude || feedbackUnavailable;
        if (mustStop)
        {
            // Collision pulses are momentary contact evidence. Never replay a
            // pulse that arrived while tracking, focus, or gameplay was absent.
            // Do not clear on the ordinary zero-amplitude path: a collision
            // worker may race the peek above and its newly raised peak must be
            // preserved for the next VR frame.
            if (feedbackUnavailable)
            {
                g_contactHaptics[0].store(0.0f, std::memory_order_release);
                g_contactHaptics[1].store(0.0f, std::memory_order_release);
            }
            if (active[0] || active[1] || mode != previousMode)
                StopControllerHaptics();
            active[0] = false;
            active[1] = false;
            previousMode = mode;
            return;
        }
        previousMode = mode;

        const uint64_t now = GetTickCount64();
        if ((active[0] || active[1]) && now - lastApplyMs < 40)
            return; // keep accumulating the peak; apply on the next unthrottled frame
        // Applying now: consume the peak and carry the latest sustained value
        // forward, so a one-shot pulse fires exactly once and a held rumble
        // persists across the 40 ms re-apply throttle.
        const float appliedPeak =
            g_peakHaptics.exchange(latest, std::memory_order_acq_rel);
        const float gameAmplitude =
            SampleHapticPeak(appliedPeak, latest).apply;
        const float contactAmplitude[2]{
            g_contactHaptics[0].exchange(0.0f, std::memory_order_acq_rel),
            g_contactHaptics[1].exchange(0.0f, std::memory_order_acq_rel)};
        const float amplitude[2]{
            MergeHapticAmplitude(gameAmplitude, contactAmplitude[0]) * intensity,
            MergeHapticAmplitude(gameAmplitude, contactAmplitude[1]) * intensity};
        const bool leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        const XrPath paths[2]{leftHanded ? g_rightHandPath : g_leftHandPath,
                              leftHanded ? g_leftHandPath : g_rightHandPath};
        for (int hand = 0; hand < 2; ++hand)
        {
            XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
            info.action = g_hapticAction;
            info.subactionPath = paths[hand];
            if (amplitude[hand] > 0.0f)
            {
                XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
                vibration.amplitude = amplitude[hand];
                vibration.duration = 50000000;
                vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
                xrApplyHapticFeedback(g_session, &info,
                    reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
                active[hand] = true;
            }
            else if (active[hand])
            {
                xrStopHapticFeedback(g_session, &info);
                active[hand] = false;
            }
        }
        lastApplyMs = now;
    }

    bool ReadLeftThumbrestTouched()
    {
        if (g_actLeftThumbrest == XR_NULL_HANDLE) return false;
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = g_actLeftThumbrest;
        XrActionStateBoolean touch{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(g_session, &info, &touch)) &&
            touch.isActive && touch.currentState == XR_TRUE;
    }

    void InvalidateWeaponInteractionSample()
    {
        exclusive_input::active.store(false,std::memory_order_release);
        g_weaponAccessory={};
        g_weaponInteraction.Cancel();
        g_weaponGestureReadyMs.store(0,std::memory_order_release);
        EnterCriticalSection(&g_headCs);
        g_padState.weaponSampleMs=0;
        g_padState.weaponButtons=0;
        g_padState.weaponConsumePrimary=g_padState.weaponConsumeSupport=false;
        LeaveCriticalSection(&g_headCs);
    }

    bool CaptureRightControllerPose(XrTime time)
    {
        if (g_gameplayActions == XR_NULL_HANDLE || g_rightAimAction == XR_NULL_HANDLE ||
            g_rightAimSpace == XR_NULL_HANDLE)
        {
            g_thumbrestDpadSampleMs.store(0, std::memory_order_release);
            InvalidateWeaponInteractionSample();
            return false;
        }
        XrActiveActionSet active{g_gameplayActions, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        if (XR_FAILED(xrSyncActions(g_session, &sync)))
        {
            g_thumbrestDpadSampleMs.store(0, std::memory_order_release);
            InvalidateWeaponInteractionSample();
            return false;
        }
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = g_rightAimAction;
        get.subactionPath = g_rightHandPath;
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        bool valid = false;
        XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION, &velocity};
        if (XR_SUCCEEDED(xrGetActionStatePose(g_session, &get, &state)) && state.isActive &&
            XR_SUCCEEDED(xrLocateSpace(g_rightAimSpace, g_localSpace, time, &location)))
        {
            constexpr XrSpaceLocationFlags required =
                XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
            valid = (location.locationFlags & required) == required &&
                    NormalizeTrackedPose(location.pose);
        }
        // Left hand: position only matters (D-pad gesture), same locate path.
        bool leftValid = false;
        XrSpaceVelocity leftVelocity{XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation leftLocation{
            XR_TYPE_SPACE_LOCATION, &leftVelocity};
        if (g_leftAimAction != XR_NULL_HANDLE && g_leftAimSpace != XR_NULL_HANDLE)
        {
            get.action = g_leftAimAction;
            get.subactionPath = g_leftHandPath;
            XrActionStatePose leftState{XR_TYPE_ACTION_STATE_POSE};
            if (XR_SUCCEEDED(xrGetActionStatePose(g_session, &get, &leftState)) &&
                leftState.isActive &&
                XR_SUCCEEDED(xrLocateSpace(g_leftAimSpace, g_localSpace, time, &leftLocation)))
            {
                constexpr XrSpaceLocationFlags required =
                    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
                leftValid = (leftLocation.locationFlags & required) == required &&
                            NormalizeTrackedPose(leftLocation.pose);
            }
        }

        auto selectVelocity = [&](int hand, bool poseValid,
                                  const XrPosef& pose,
                                  const XrSpaceVelocity& native,
                                  XrVector3f& selected,
                                  bool& selectedValid,
                                  bool& usedPoseDerived)
        {
            ControllerPoseVelocityHistory& history =
                g_controllerPoseVelocityHistory[hand];
            float derived[3]{};
            const float current[3]{pose.position.x, pose.position.y,
                                   pose.position.z};
            const float previous[3]{history.position.x, history.position.y,
                                    history.position.z};
            const bool derivedValid = poseValid && history.valid &&
                time > history.sampleTime &&
                Halo4DeriveControllerPoseVelocity(
                    previous,current,time-history.sampleTime,derived);
            if(poseValid)
            {
                history.valid=true;
                history.sampleTime=time;
                history.position=pose.position;
            }
            else
            {
                history={};
            }
            const bool nativeValid = poseValid &&
                (native.velocityFlags &
                    XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0 &&
                std::isfinite(native.linearVelocity.x) &&
                std::isfinite(native.linearVelocity.y) &&
                std::isfinite(native.linearVelocity.z);
            const float nativeValue[3]{native.linearVelocity.x,
                                       native.linearVelocity.y,
                                       native.linearVelocity.z};
            float chosen[3]{};
            selectedValid=Halo4SelectControllerLinearVelocity(
                nativeValid,nativeValue,derivedValid,derived,chosen,
                usedPoseDerived);
            if(selectedValid)
                selected={chosen[0],chosen[1],chosen[2]};
        };
        XrVector3f selectedRightVelocity{},selectedLeftVelocity{};
        bool selectedRightVelocityValid=false;
        bool selectedLeftVelocityValid=false;
        bool rightUsedPoseDerived=false,leftUsedPoseDerived=false;
        selectVelocity(1,valid,location.pose,velocity,
            selectedRightVelocity,selectedRightVelocityValid,
            rightUsedPoseDerived);
        selectVelocity(0,leftValid,leftLocation.pose,leftVelocity,
            selectedLeftVelocity,selectedLeftVelocityValid,
            leftUsedPoseDerived);

        const bool leftHanded = g_config.left_handed;
        const bool handChanged = leftHanded !=
            g_capturedLeftHanded.load(std::memory_order_acquire);
        if (handChanged)
        {
            StopControllerHaptics();
            LOG("Weapon handedness: physical %s hand is primary; physical %s hand is support/secondary",
                leftHanded ? "left" : "right", leftHanded ? "right" : "left");
            g_contactSpaceEpoch.fetch_add(1, std::memory_order_acq_rel);
            g_twoHandLatched.store(false, std::memory_order_release);
            g_contactHaptics[0].store(0.0f, std::memory_order_release);
            g_contactHaptics[1].store(0.0f, std::memory_order_release);
        }
        EnterCriticalSection(&g_headCs);
        g_physicalControllerPose[0] = leftLocation.pose;
        g_physicalControllerPose[1] = location.pose;
        g_physicalControllerValid[0] = leftValid;
        g_physicalControllerValid[1] = valid;
        g_physicalControllerAtMs = GetTickCount64();
        if (leftHanded)
        {
            std::swap(location.pose, leftLocation.pose);
            std::swap(valid, leftValid);
            std::swap(selectedRightVelocity, selectedLeftVelocity);
            std::swap(selectedRightVelocityValid, selectedLeftVelocityValid);
        }
        g_capturedLeftHanded.store(leftHanded, std::memory_order_release);
        if (handChanged)
        {
            // Prevent a toggle from appearing as a swing between controllers.
            valid = leftValid = false;
            selectedRightVelocityValid = selectedLeftVelocityValid = false;
            g_meleeSpeedHistory[0] = {};
            g_meleeSpeedHistory[1] = {};
        }
        g_rightAimPoseValid = valid;
        g_stockControllerPoseTime = valid && leftValid ? time : 0;
        if (valid)
            g_rightAimPose = location.pose;
        g_rightAimLinearVelocityValid = selectedRightVelocityValid;
        if (g_rightAimLinearVelocityValid)
            g_rightAimLinearVelocity = selectedRightVelocity;
        g_rightAimLinearVelocityAtMs = g_rightAimLinearVelocityValid
            ? GetTickCount64() : 0;
        g_leftAimPoseValid = leftValid;
        if (leftValid)
            g_leftAimPose = leftLocation.pose;
        g_leftAimLinearVelocityValid = selectedLeftVelocityValid;
        if (g_leftAimLinearVelocityValid)
            g_leftAimLinearVelocity = selectedLeftVelocity;
        g_leftAimLinearVelocityAtMs = g_leftAimLinearVelocityValid
            ? GetTickCount64() : 0;
        const auto recordMeleeSpeed=[](int hand,bool valid,const XrVector3f& velocity)
        {
            const float speed=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z);
            g_meleeSpeedHistory[hand].Update(GetTickCount64(),
                valid && (g_config.physical_melee || g_config.gesture_melee),speed);
        };
        recordMeleeSpeed(0,g_leftAimLinearVelocityValid,g_leftAimLinearVelocity);
        recordMeleeSpeed(1,g_rightAimLinearVelocityValid,g_rightAimLinearVelocity);
        LeaveCriticalSection(&g_headCs);
        static bool loggedPoseVelocityFallback=false;
        if((rightUsedPoseDerived || leftUsedPoseDerived) &&
            !loggedPoseVelocityFallback)
        {
            const XrVector3f& selected=rightUsedPoseDerived
                ? selectedRightVelocity:selectedLeftVelocity;
            const float selectedSpeed=std::sqrt(
                selected.x*selected.x+selected.y*selected.y+
                selected.z*selected.z);
            if(std::isfinite(selectedSpeed) && selectedSpeed>=0.10f)
            {
                LOG("OpenXR controller velocity: pose-delta fallback active "
                    "because the runtime advertised a zero/missing linear "
                    "velocity; meaningful native velocity remains preferred");
                loggedPoseVelocityFallback=true;
            }
        }
        static bool logged = false;
        if (valid && !logged)
        {
            LOG("M3: main-weapon controller tracking active pose=(%.3f,%.3f,%.3f)",
                location.pose.position.x, location.pose.position.y, location.pose.position.z);
            logged = true;
        }

        // Read the gamepad-replacement actions (already synced above).
        VrPadState pad{};
        auto getV2 = [&](XrAction action, float& outX, float& outY) {
            if (action == XR_NULL_HANDLE) return;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = action;
            XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (XR_SUCCEEDED(xrGetActionStateVector2f(g_session, &gi, &st)) && st.isActive)
            { outX = st.currentState.x; outY = st.currentState.y; pad.valid = true; }
        };
        auto getF = [&](XrAction action, float& out) {
            if (action == XR_NULL_HANDLE) return false;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = action;
            XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
            if (XR_SUCCEEDED(xrGetActionStateFloat(g_session, &gi, &st)) && st.isActive)
            { out = st.currentState; pad.valid = true; return true; }
            return false;
        };
        auto getB = [&](XrAction action, bool& out) {
            if (action == XR_NULL_HANDLE) return;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = action;
            XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
            if (XR_SUCCEEDED(xrGetActionStateBoolean(g_session, &gi, &st)) && st.isActive)
            { out = st.currentState == XR_TRUE; pad.valid = true; }
        };
        getV2(g_actMove, pad.moveX, pad.moveY);
        getV2(g_actTurn, pad.turnX, pad.turnY);
        getF(g_actTrigL, pad.trigL);
        getF(g_actTrigR, pad.trigR);
        const bool supportGripActive = getF(g_actGripL, pad.gripL);
        const bool primaryGripActive = getF(g_actGripR, pad.gripR);
        if (leftHanded)
        {
            std::swap(pad.trigL, pad.trigR);
            std::swap(pad.gripL, pad.gripR);
        }
        getB(g_actA, pad.a);
        getB(g_actB, pad.b);
        getB(g_actX, pad.x);
        getB(g_actY, pad.y);
        getB(g_actClickL, pad.clickL);
        getB(g_actClickR, pad.clickR);
        getB(g_actMenu, pad.menu);
        {
            float lx=0,ly=0,rx=0,ry=0;bool lc=false,rc=false;
            getV2(g_actFacePadL,lx,ly);getV2(g_actFacePadR,rx,ry);
            getB(g_actFaceClickL,lc);getB(g_actFaceClickR,rc);
            static vr_mapping::PadFaces faces[2];
            const auto leftProfile=g_controllerProfiles[0].load(std::memory_order_acquire);
            const auto rightProfile=g_controllerProfiles[1].load(std::memory_order_acquire);
            const bool leftPad=leftProfile==vr_mapping::Profile::Vive||leftProfile==vr_mapping::Profile::Wmr;
            const bool rightPad=rightProfile==vr_mapping::Profile::Vive||rightProfile==vr_mapping::Profile::Wmr;
            const int leftZone=faces[0].Update(ly,leftPad&&lc);
            const int rightZone=faces[1].Update(ry,rightPad&&rc);
            if(leftZone) {pad.x|=leftZone>0;pad.y|=leftZone<0;}
            if(rightZone) {pad.a|=rightZone>0;pad.b|=rightZone<0;}
            // Vive uses the same pad for locomotion and faces. A clicked
            // face zone owns those axes for its full hold; WMR retains its
            // physically separate thumbstick. Center clicks keep L3/R3.
            if(leftProfile==vr_mapping::Profile::Vive&&leftZone)
            {pad.clickL=false;pad.moveX=pad.moveY=0;}
            if(rightProfile==vr_mapping::Profile::Vive&&rightZone)
            {pad.clickR=false;pad.turnX=pad.turnY=0;}
        }
        const DpadStickInput thumbrest = ConsumeThumbrestDpad(
            g_config.quest_thumbrest_dpad, ReadLeftThumbrestTouched(),
            pad.valid && valid && leftValid && g_sessionState == XR_SESSION_STATE_FOCUSED &&
                !Menu_IsOpen()&&!g_weaponInteraction.HasClaimedGrip(), pad.turnX, pad.turnY);
        pad.thumbrestDpad = thumbrest.active;
        pad.dpadX = thumbrest.x;
        pad.dpadY = thumbrest.y;
        static VrPadState previousPad{};
        static bool previousRawMenu = false;
        static uint64_t odstMenuPulseUntil = 0;
        const uint64_t inputNow = GetTickCount64();
        const bool rawMenuEdge = pad.menu && !previousRawMenu;
        if (rawMenuEdge)
        {
            LOG("controller edge: Menu/Start");
            if (Game_IsCameraOnlyBringup())
                odstMenuPulseUntil = inputNow + 350;
        }
        previousRawMenu = pad.menu;
        if (Game_IsCameraOnlyBringup() && inputNow < odstMenuPulseUntil)
            pad.menu = true;
        if (pad.a && !previousPad.a) LOG("controller edge: A");
        if (pad.b && !previousPad.b) LOG("controller edge: B");
        if (pad.x && !previousPad.x) LOG("controller edge: X");
        if (pad.y && !previousPad.y) LOG("controller edge: Y");
        previousPad = pad;
        // Keep the pointer click private while suppressing all gameplay
        // consumers (including camera snapshots that bypass XInput).
        g_nativePointerTrigger=pad.trigR;
        const bool pointerMode=g_config.game_menu_pointer&&!Menu_IsOpen()&&
            g_sessionState==XR_SESSION_STATE_FOCUSED&&valid&&
            NativeMenuPointer_ConsumesTrigger()&&
            game_menu_pointer::MenuMode(TitleAdapter_GetRuntimeMode(),
                VR_IsPausePresentation()&&VR_IsPausePresentationTarget())&&
            !VR_IsCutsceneTheaterActive();
        pad.exclusiveInput=pad.thumbrestDpad||pointerMode;
        exclusive_input::active.store(pad.exclusiveInput||Menu_IsOpen(),std::memory_order_release);
        const bool f1Chord=pointerMode&&pad.clickL&&pad.clickR;
        static ExclusiveInputHolds consumedControls;
        // Include the raw physical right axes in the hold latch, even though
        // ConsumeThumbrestDpad already moved them to the D-pad fields.
        if(pad.thumbrestDpad) {pad.turnX=pad.dpadX;pad.turnY=pad.dpadY;}
        consumedControls.ApplyVr(pad,pad.exclusiveInput,pointerMode&&!pad.thumbrestDpad);
        if(f1Chord) pad.clickL=pad.clickR=true;
        const float rawSupportGrip=pad.gripL;
        weapon_interaction::Output weaponGesture{};
        if(g_config.manual_reload || g_config.weapon_holsters || g_weaponInteraction.HasClaimedGrip())
        {
            // Use the same predicted-time head and controller space. This
            // optional extra locate does not change the late camera sample.
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
            constexpr XrSpaceLocationFlags tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            const bool headValid=XR_SUCCEEDED(xrLocateSpace(g_viewSpace,g_localSpace,time,&head))&&
                (head.locationFlags&tracked)==tracked&&NormalizeTrackedPose(head.pose);
            weapon_interaction::Sample sample{};
            sample.now=inputNow;sample.title=TitleAdapter_GetActiveTitle();
            sample.generation=TitleAdapter_GetGeneration(sample.title);
            sample.space=g_contactSpaceEpoch.load(std::memory_order_acquire);
            sample.weaponGraph=VR_GetWeaponModelIdentity(sample.title,sample.generation,sample.space,inputNow);
            sample.dualWield=SecondaryWeaponPresentationActive();
            sample.ready=!pad.exclusiveInput&&pad.valid&&valid&&leftValid&&headValid&&!handChanged&&
                primaryGripActive&&supportGripActive&&
                (location.locationFlags&tracked)==tracked&&(leftLocation.locationFlags&tracked)==tracked&&
                g_sessionState==XR_SESSION_STATE_FOCUSED&&Game_IsHeadTracking()&&VR_IsStereoEnabled()&&
                TitleAdapter_GetRuntimeMode()==RuntimeMode::Gameplay&&!Menu_IsOpen()&&
                !VR_IsPausePresentation()&&!VR_IsPausePresentationTarget()&&!VR_IsCutsceneTheaterActive();
            sample.head={head.pose.position.x,head.pose.position.y,head.pose.position.z};
            sample.headRotation={head.pose.orientation.x,head.pose.orientation.y,head.pose.orientation.z,head.pose.orientation.w};
            sample.primary={location.pose.position.x,location.pose.position.y,location.pose.position.z};
            sample.primaryRotation={location.pose.orientation.x,location.pose.orientation.y,location.pose.orientation.z,location.pose.orientation.w};
            sample.support={leftLocation.pose.position.x,leftLocation.pose.position.y,leftLocation.pose.position.z};
            sample.supportRotation={leftLocation.pose.orientation.x,leftLocation.pose.orientation.y,
                leftLocation.pose.orientation.z,leftLocation.pose.orientation.w};
            (void)g_reloadTargets.Read(sample,leftHanded);
            sample.primaryGrip=pad.gripR;sample.supportGrip=pad.gripL;
            if(!primaryGripActive||!supportGripActive)
                sample.primaryGrip=sample.supportGrip=std::numeric_limits<float>::quiet_NaN();
            sample.otherAction=pad.trigL>0.15f||pad.trigR>0.15f||pad.menu||pad.a||pad.b||pad.x||pad.y||
                pad.clickL||pad.clickR||pad.thumbrestDpad;
            weapon_interaction::Settings settings{};
            settings.reload=g_config.manual_reload;settings.holsters=g_config.weapon_holsters;
            settings.leftHanded=leftHanded;settings.pouchDown=g_config.weapon_pouch_down_m;
            settings.pouchLocation=g_config.weapon_pouch_location;
            settings.pouchOffset={g_config.weapon_pouch_offset_x_m,g_config.weapon_pouch_offset_y_m,g_config.weapon_pouch_offset_z_m};
            settings.zoneRadius=g_config.weapon_body_zone_radius_m;
            settings.holsterRadius=g_config.weapon_holster_radius_m;
            settings.insertRadius=g_config.weapon_insert_radius_m;
            settings.drawDistance=g_config.weapon_holster_draw_m;
            settings.holsterSlide=g_config.weapon_holster_slide;
            settings.holsterClick=g_config.weapon_holster_click;
            settings.needleShake=g_config.weapon_needler_shake;
            settings.genericVisual=g_config.weapon_unknown_reload_visual;
            settings.shakeTravel=g_config.weapon_shake_travel_m;
            settings.holsterLocation=g_config.weapon_holster_location;
            const int index=weapon_interaction::TitleIndex(sample.title);
            settings.reloadButton=WeaponGestureBinding(sample.title,vr_mapping::Reload,inputNow);
            settings.swapButton=WeaponGestureBinding(sample.title,vr_mapping::SwitchWeapon,inputNow);
            weaponGesture=g_weaponInteraction.Update(sample,settings);
            const auto& supportQ=leftLocation.pose.orientation;
            g_weaponAccessory=weapon_accessory::Build(sample,settings,weaponGesture,
                {supportQ.x,supportQ.y,supportQ.z,supportQ.w});
            pad.weaponButtons=weaponGesture.buttons;
            pad.weaponSampleMs=inputNow;pad.weaponPulseUntilMs=weaponGesture.pulseUntil;
            pad.weaponTitle=sample.title;pad.weaponGeneration=sample.generation;
            pad.weaponSpace=sample.space;
            pad.weaponGraph=sample.weaponGraph;
            pad.weaponOptions=(settings.reload?1u:0u)|(settings.holsters?2u:0u)|(leftHanded?4u:0u)|
                (!settings.holsterSlide?8u:0u)|(settings.holsterClick?16u:0u)|(settings.needleShake?32u:0u);
            pad.weaponReloadBinding=settings.reloadButton;pad.weaponSwitchBinding=settings.swapButton;
            pad.weaponConsumePrimary=weaponGesture.consumePrimary;
            pad.weaponConsumeSupport=weaponGesture.consumeSupport;
            // Consume before publishing to ALL native/title consumers. A
            // cancelled/stale command must not resurrect its original bumper.
            if(pad.weaponConsumePrimary) pad.gripR=0;
            if(pad.weaponConsumeSupport) pad.gripL=0;
            if(weaponGesture.primaryHaptic>0) VR_PulseContactHaptics(false,weaponGesture.primaryHaptic);
            if(weaponGesture.supportHaptic>0) VR_PulseContactHaptics(true,weaponGesture.supportHaptic);
            if(weaponGesture.pickedMagazine||weaponGesture.grabbedHolster)
                g_weaponGestureGrabs.fetch_add(1,std::memory_order_relaxed);
            if(weaponGesture.reloadRequested)
            {
                g_weaponReloadRequests.fetch_add(1,std::memory_order_relaxed);
                if(!weapon_interaction::NeedleWeapon(sample.title,sample.weaponGraph))
                    (sample.receiverValid?g_authoredInsertionRequests:g_fallbackInsertionRequests)
                        .fetch_add(1,std::memory_order_relaxed);
            }
            if(weaponGesture.swapRequested) g_weaponSwapRequests.fetch_add(1,std::memory_order_relaxed);
            g_weaponGestureReadyMs.store(sample.ready&&!sample.dualWield?inputNow:0,std::memory_order_release);
        }
        else
        {
            g_weaponInteraction.Reset();
            g_weaponAccessory={};
            g_weaponGestureReadyMs.store(0,std::memory_order_release);
        }
        g_scopeZoomStickY.store(pad.valid?pad.turnY:0.0f,
                                std::memory_order_release);
        const bool supportWasLatched=g_twoHandLatched.load(std::memory_order_acquire);
        UpdateTwoHandLatch(valid, location.pose, leftValid, leftLocation.pose,
                           rawSupportGrip, handChanged, weapon_interaction::BlocksSupportGrab(weaponGesture));
        EnterCriticalSection(&g_headCs);
        pad.profileEpoch=g_controllerProfileEpoch.load(std::memory_order_acquire);
        // The same grip edge that acquires OR releases support must not also
        // activate a flashlight. Publish this frame's decision, not last frame's.
        pad.supportAimActive=supportWasLatched||g_twoHandLatched.load(std::memory_order_acquire);
        g_padState = pad;
        g_thumbrestDpadSampleMs.store(pad.thumbrestDpad ? inputNow : 0,
            std::memory_order_release);
        LeaveCriticalSection(&g_headCs);
        ApplyControllerHaptics(valid && leftValid);
        static bool padLogged = false;
        if (pad.valid && !padLogged)
        {
            LOG("M3: controller inputs active (sticks/buttons feeding the virtual gamepad)");
            padLogged = true;
        }
        return true;
    }

    // Optional native-menu input has no effect on any eye/camera transaction.
    // Publish only after a real stock-screen quad was successfully submitted.
    struct NativePointerFrame
    {
        bool active = false, pressed = false;
        float u = 0, v = 0;
    } g_nativePointerFrame;

    [[maybe_unused]] void UpdateNativeMenuPointerLegacy(const XrCompositionLayerQuad& quad)
    {
        // User reports 59f2a82 native pointing failed. Disable that optional
        // behavior before correcting it; preserve all existing camera/F1 code.
        return;
        static bool pressed = false;
        const bool headLocked = quad.space == g_viewSpace;
        if (!g_config.game_menu_pointer || Menu_IsOpen() ||
            g_sessionState != XR_SESSION_STATE_FOCUSED || !g_padState.valid ||
            GetTickCount64()-g_physicalControllerAtMs > 200 ||
            !g_rightAimPoseValid || (headLocked && !g_headPoseValid) ||
            !game_menu_pointer::MenuMode(TitleAdapter_GetRuntimeMode(),
                VR_IsPausePresentation() && VR_IsPausePresentationTarget()) ||
            VR_IsCutsceneTheaterActive()) { pressed = false; return; }
        if (g_nativePointerTrigger >= 0.65f) pressed = true;
        else if (g_nativePointerTrigger <= 0.35f) pressed = false;

        XrVector3f origin = g_rightAimPose.position;
        XrVector3f direction = Rotate(g_rightAimPose.orientation, {0, 0, -1});
        if (headLocked) {
            const XrQuaternionf inverseHead{-g_headPose.orientation.x,
                -g_headPose.orientation.y, -g_headPose.orientation.z, g_headPose.orientation.w};
            origin = Rotate(inverseHead, {origin.x-g_headPose.position.x,
                origin.y-g_headPose.position.y, origin.z-g_headPose.position.z});
            direction = Rotate(inverseHead, direction);
        }
        const XrQuaternionf inverseQuad{-quad.pose.orientation.x,
            -quad.pose.orientation.y, -quad.pose.orientation.z, quad.pose.orientation.w};
        origin = Rotate(inverseQuad, {origin.x-quad.pose.position.x,
            origin.y-quad.pose.position.y, origin.z-quad.pose.position.z});
        direction = Rotate(inverseQuad, direction);
        if (direction.z >= -1e-5f || origin.z <= 0) return;
        // Translate the quad-local z=0 plane to the existing helper's z=-1.
        const float o[]{origin.x, origin.y, origin.z-1.0f};
        const float d[]{direction.x, direction.y, direction.z};
        const auto hit = IntersectMenuQuad(o, d, 1.0f, quad.size.width, quad.size.height, 0);
        g_nativePointerFrame = {hit.hit, pressed, hit.u, hit.v};
    }

    void UpdateNativeMenuPointer(const XrCompositionLayerQuad& quad)
    {
        static bool pressed = false;
        const bool headLocked = quad.space == g_viewSpace;
        if (!g_config.game_menu_pointer || Menu_IsOpen() || g_padState.thumbrestDpad) {
            NativeMenuPointer_FrameStatus("disabled-F1-or-Dpad"); pressed=false; return;
        }
        if (!game_menu_pointer::MenuMode(TitleAdapter_GetRuntimeMode(),
                VR_IsPausePresentation() && VR_IsPausePresentationTarget()) ||
            VR_IsCutsceneTheaterActive()) {
            NativeMenuPointer_FrameStatus("non-menu-mode"); pressed=false; return;
        }
        if (g_sessionState != XR_SESSION_STATE_FOCUSED || !g_padState.valid ||
            GetTickCount64()-g_physicalControllerAtMs > 200 ||
            !g_rightAimPoseValid || (headLocked && !g_headPoseValid)) {
            NativeMenuPointer_FrameStatus("focus-or-tracking"); pressed=false; return;
        }
        if (g_nativePointerTrigger >= 0.65f) pressed = true;
        else if (g_nativePointerTrigger <= 0.35f) pressed = false;
        float u=0,v=0;
        const bool hit=game_menu_pointer::RayHit(quad,g_rightAimPose,g_headPose,headLocked,u,v);
        g_nativePointerFrame = {hit, pressed, u, v};
        NativeMenuPointer_FrameStatus(hit ? "hit" : "ray-miss");
    }

    void UpdateMenuPointer(bool headLocked)
    {
        static bool triggerPressed = false;
        static bool hadHit = false;
        static float smoothU = 0.5f, smoothV = 0.5f;
        static uint64_t lastDiagMs = 0;
        static bool dragging = false;
        static float grabOffsetX = 0.0f, grabOffsetY = 0.0f;
        if (!g_rightAimPoseValid || (headLocked && !g_headPoseValid) ||
            (!headLocked && !g_haveCenter))
        {
            triggerPressed = false;
            hadHit = false;
            if (dragging)
            {
                // Tracking dropped mid-drag. Keep wherever the panel got to
                // rather than leaving the handle stuck to a stale ray.
                dragging = false;
                Menu_SetPanelDragging(false);
                ConfigSave();
            }
            Menu_ClearVrPointer();
            return;
        }

        const XrPosef anchor = headLocked
            ? g_headPose
            : XrPosef{g_centerRot, g_centerPos};
        const XrQuaternionf inverseAnchor{
            -anchor.orientation.x, -anchor.orientation.y,
            -anchor.orientation.z, anchor.orientation.w};
        const XrVector3f relative{
            g_rightAimPose.position.x - anchor.position.x,
            g_rightAimPose.position.y - anchor.position.y,
            g_rightAimPose.position.z - anchor.position.z};
        const XrVector3f localOrigin = Rotate(inverseAnchor, relative);
        const XrVector3f worldDirection =
            Rotate(g_rightAimPose.orientation, {0.0f, 0.0f, -1.0f});
        const XrVector3f localDirection = Rotate(inverseAnchor, worldDirection);
        const float origin[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
        const float direction[3] = {localDirection.x, localDirection.y, localDirection.z};
        const MenuPointerHit hit = IntersectMenuQuad(origin, direction,
            g_config.menu_distance_m, g_config.menu_width_m,
            g_config.menu_width_m * MENU_H / MENU_W,
            g_config.menu_height_m, g_config.menu_side_m);

        if (hit.hit)
        {
            if (!hadHit)
            {
                smoothU = hit.u;
                smoothV = hit.v;
            }
            else
            {
                smoothU += (hit.u - smoothU) * 0.35f;
                smoothV += (hit.v - smoothV) * 0.35f;
            }
        }
        hadHit = hit.hit;

        const bool wasTriggerPressed = triggerPressed;
        if (!triggerPressed && g_padState.trigR >= 0.65f)
            triggerPressed = true;
        else if (triggerPressed && g_padState.trigR <= 0.35f)
            triggerPressed = false;
        const float stickY = std::fabs(g_padState.turnY) > 0.25f
            ? g_padState.turnY
            : 0.0f;

        // Panel drag. The trigger only starts a drag when it goes down on the
        // grab handle; anywhere else on the panel it is an ordinary click, so
        // nothing moves by accident. While dragging, the panel keeps the offset
        // it had when grabbed, and the stick pushes it further away or pulls it
        // in -- the ray recomputes at the new distance, so it stays under the
        // pointer.
        auto planeHit = [&](float distance, float& outX, float& outY) -> bool {
            if (std::fabs(direction[2]) < 1e-5f)
                return false;
            const float t = (-distance - origin[2]) / direction[2];
            if (t <= 0.0f)
                return false;
            outX = origin[0] + direction[0] * t;
            outY = origin[1] + direction[1] * t;
            return true;
        };
        if (!dragging && !wasTriggerPressed && triggerPressed &&
            hit.hit && Menu_PointerOverGrabHandle())
        {
            float hx = 0.0f, hy = 0.0f;
            if (planeHit(g_config.menu_distance_m, hx, hy))
            {
                grabOffsetX = g_config.menu_side_m - hx;
                grabOffsetY = g_config.menu_height_m - hy;
                dragging = true;
                Menu_SetPanelDragging(true);
                LOG("menu panel: grabbed at distance %.2f m", g_config.menu_distance_m);
            }
        }
        if (dragging)
        {
            if (stickY != 0.0f)
            {
                // ~0.6 m/s at full deflection, frame-rate independent.
                static uint64_t lastPushMs = 0;
                const uint64_t nowPush = GetTickCount64();
                const float dt = lastPushMs ? (float)(nowPush - lastPushMs) * 0.001f : 0.0f;
                lastPushMs = nowPush;
                if (dt > 0.0f && dt < 0.25f)
                    g_config.menu_distance_m = std::clamp(
                        g_config.menu_distance_m - stickY * 0.6f * dt,
                        kMenuDistanceMin, kMenuDistanceMax);
            }
            float hx = 0.0f, hy = 0.0f;
            if (planeHit(g_config.menu_distance_m, hx, hy))
            {
                g_config.menu_side_m = std::clamp(hx + grabOffsetX,
                                                  -kMenuOffsetLimit, kMenuOffsetLimit);
                g_config.menu_height_m = std::clamp(hy + grabOffsetY,
                                                    -kMenuOffsetLimit, kMenuOffsetLimit);
            }
            if (!triggerPressed)
            {
                dragging = false;
                Menu_SetPanelDragging(false);
                ConfigSave();
                LOG("menu panel: released at distance %.2f m, side %.2f m, height %.2f m",
                    g_config.menu_distance_m, g_config.menu_side_m, g_config.menu_height_m);
            }
            // Swallow the click and the scroll so dragging never also drives a
            // widget underneath the handle.
            Menu_SetVrPointer(hit.hit, smoothU, smoothV, false, 0.0f);
            return;
        }

        const float scroll = stickY * 0.12f;
        Menu_SetVrPointer(hit.hit, smoothU, smoothV, triggerPressed, scroll);

        const uint64_t now = GetTickCount64();
        if (now - lastDiagMs >= 2000)
        {
            lastDiagMs = now;
            LOG("menu pointer: hit=%d uv=(%.3f,%.3f) origin=(%.2f,%.2f,%.2f) "
                "dir=(%.2f,%.2f,%.2f) trigger=%.2f stickY=%.2f headLocked=%d",
                hit.hit ? 1 : 0, hit.u, hit.v,
                origin[0], origin[1], origin[2], direction[0], direction[1], direction[2],
                g_padState.trigR, g_padState.turnY, headLocked ? 1 : 0);
        }
    }

    // Part 2 (render thread, first frame): now that we have the game's D3D
    // device, create the session and everything that hangs off it. This is
    // fast (~100 ms), so running it on the render thread is fine.
    bool InitSession(IDXGISwapChain* sc)
    {
        // The game's device is the one we hand to OpenXR: the runtime then
        // reads our textures without any cross-device copying.
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_device)))
        {
            Fail("Could not get the game's D3D11 device");
            return false;
        }
        g_device->GetImmediateContext(&g_context);
        IDXGIDevice* memoryDevice = nullptr;
        IDXGIAdapter* memoryAdapter = nullptr;
        if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&memoryDevice))))
        {
            if (SUCCEEDED(memoryDevice->GetAdapter(&memoryAdapter)))
            {
                if (g_memoryAdapter) { g_memoryAdapter->Release(); g_memoryAdapter = nullptr; }
                memoryAdapter->QueryInterface(IID_PPV_ARGS(&g_memoryAdapter));
                memoryAdapter->Release();
            }
            memoryDevice->Release();
        }

        DXGI_SWAP_CHAIN_DESC scd{};
        sc->GetDesc(&scd);
        LOG("game swapchain: %ux%u fmt %d windowed=%d swapeffect=%d bufcount=%u hwnd %p",
            scd.BufferDesc.Width, scd.BufferDesc.Height, (int)scd.BufferDesc.Format,
            (int)scd.Windowed, (int)scd.SwapEffect, scd.BufferCount, (void*)scd.OutputWindow);

        XrResult r;
        // Required call before creating a D3D11 session; also tells us which
        // GPU the runtime wants (must match the game's).
        PFN_xrGetD3D11GraphicsRequirementsKHR pfnReq = nullptr;
        xrGetInstanceProcAddr(g_instance, "xrGetD3D11GraphicsRequirementsKHR",
                              reinterpret_cast<PFN_xrVoidFunction*>(&pfnReq));
        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        if (!pfnReq || XR_FAILED(pfnReq(g_instance, g_systemId, &req)))
        {
            Fail("The OpenXR runtime does not support D3D11");
            return false;
        }
        IDXGIDevice* dxgiDev = nullptr;
        if (SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)))
        {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)))
            {
                DXGI_ADAPTER_DESC ad{};
                adapter->GetDesc(&ad);
                if (memcmp(&ad.AdapterLuid, &req.adapterLuid, sizeof(LUID)) != 0)
                    LOG("WARNING: game GPU differs from the headset's GPU; session may fail");
                adapter->Release();
            }
            dxgiDev->Release();
        }

        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = g_device;
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &binding;
        sci.systemId = g_systemId;
        r = xrCreateSession(g_instance, &sci, &g_session);
        if (XR_FAILED(r))
        {
            Fail("Could not create the VR session", r);
            return false;
        }

        XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        rsci.poseInReferenceSpace.orientation.w = 1.0f;
        if (XR_FAILED(xrCreateReferenceSpace(g_session, &rsci, &g_localSpace)))
        {
            Fail("Could not create the LOCAL reference space");
            return false;
        }
        g_contactSpaceChangeAtNs.store(0, std::memory_order_release);
        g_contactSpaceEpoch.fetch_add(1, std::memory_order_acq_rel);
        rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        if (XR_FAILED(xrCreateReferenceSpace(g_session, &rsci, &g_viewSpace)))
        {
            Fail("Could not create the VIEW reference space");
            return false;
        }

        // Controller actions are optional for M0-M2: failure must never take
        // down the already-working headset/stereo path.
        if (!CreateControllerActions())
            LOG("M3: controller tracking unavailable; head/stereo remain enabled");

        uint32_t modeCount = 0;
        xrEnumerateEnvironmentBlendModes(g_instance, g_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                         1, &modeCount, &g_blendMode);

        // M2: the headset's two eyes — recommended per-eye render size. We'll
        // render the game once per eye into swapchains of this size.
        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(g_instance, g_systemId,
                                          XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        g_viewConfigs.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(g_instance, g_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                          viewCount, &viewCount, g_viewConfigs.data());
        g_views.assign(viewCount, {XR_TYPE_VIEW});
        for (uint32_t i = 0; i < viewCount; i++)
            LOG("M2: eye %u recommended render size %ux%u", i,
                g_viewConfigs[i].recommendedImageRectWidth, g_viewConfigs[i].recommendedImageRectHeight);

        // Pick the image format for our XR swapchains, preferring sRGB
        // variants so colors in the headset match the monitor.
        uint32_t fmtCount = 0;
        xrEnumerateSwapchainFormats(g_session, 0, &fmtCount, nullptr);
        std::vector<int64_t> formats(fmtCount);
        xrEnumerateSwapchainFormats(g_session, fmtCount, &fmtCount, formats.data());
        const int64_t preferred[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                                     DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
        g_xrFormat = 0;
        for (int64_t want : preferred)
        {
            for (int64_t have : formats)
                if (have == want) { g_xrFormat = want; break; }
            if (g_xrFormat) break;
        }
        if (!g_xrFormat && fmtCount > 0)
            g_xrFormat = formats[0];
        if (!g_xrFormat)
        {
            Fail("The runtime offered no usable swapchain formats");
            return false;
        }
        LOG("XR swapchain format: %d", (int)g_xrFormat);

        // Menu: fixed-size texture on its own quad. Its render target uses the
        // non-sRGB sibling format so a raw GPU copy lands with correct gamma.
        if (!CreateChain(MENU_W, MENU_H, g_menuChain, g_menuImages, g_menuRtvs, "menu"))
        {
            Fail("Could not create the menu swapchain");
            return false;
        }
        if (!Menu_Init(scd.OutputWindow, g_device, g_context, UnormSibling((DXGI_FORMAT)g_xrFormat)))
            LOG("WARNING: menu failed to initialize; F1 menu unavailable");

        // Optional cursor uploads once. Reuse its released image each frame;
        // any initialization failure leaves the native input and VR core alive.
        g_nativeCursorReady=false;
        const bool cursorFormat=g_xrFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            g_xrFormat==DXGI_FORMAT_R8G8B8A8_UNORM || g_xrFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            g_xrFormat==DXGI_FORMAT_B8G8R8A8_UNORM;
        if (cursorFormat && CreateChain(32,32,g_nativeCursorChain,g_nativeCursorImages,g_nativeCursorRtvs,"game menu cursor")) {
            uint32_t index=0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout=1000000000;
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if (xrAcquireSwapchainImage(g_nativeCursorChain,&acquire,&index)==XR_SUCCESS &&
                xrWaitSwapchainImage(g_nativeCursorChain,&wait)==XR_SUCCESS) {
                if(index<g_nativeCursorImages.size()&&g_nativeCursorImages[index]) {
                    uint32_t pixels[32*32];
                    for(int y=0;y<32;++y) for(int x=0;x<32;++x)
                        pixels[y*32+x]=game_menu_pointer::CursorPixel(x,y);
                    g_context->UpdateSubresource(g_nativeCursorImages[index],0,nullptr,pixels,32*4,0);
                    g_nativeCursorReady=xrReleaseSwapchainImage(g_nativeCursorChain,&release)==XR_SUCCESS;
                } else xrReleaseSwapchainImage(g_nativeCursorChain,&release);
            }
        }
        if(!g_nativeCursorReady) DestroyChain(g_nativeCursorChain,g_nativeCursorImages,g_nativeCursorRtvs);
        LOG("game menu cursor: %s; optional visual only",g_nativeCursorReady?"ready":"unavailable");

        strcpy_s(g_status.sessionState, "starting");
        LogHeadsetPanelRate();
        LOG("OpenXR session created");
        return true;
    }

    // Read the headset's REAL panel rate once, so the log can separate two very
    // different things that look identical from the frame period alone:
    //   - the headset is genuinely running slow, versus
    //   - the runtime is TARGETING us at a fraction of the panel (reprojection),
    //     which pins us to panel/2 or panel/3 no matter what the GPU could do.
    // Works for any rate (72, 80, 90, 120, 144); nothing here assumes a number.
    void LogHeadsetPanelRate()
    {
        if (!g_refreshRateExtEnabled)
        {
            LOG("headset: this runtime does not expose %s, so the panel rate is "
                "unknown; only the rate it targets us at can be read",
                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
            return;
        }
        PFN_xrGetDisplayRefreshRateFB getRate = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(
                g_instance, "xrGetDisplayRefreshRateFB",
                (PFN_xrVoidFunction*)&getRate)) || !getRate)
        {
            LOG("headset: xrGetDisplayRefreshRateFB missing though the extension "
                "is enabled; panel rate unknown");
            return;
        }
        float hz = 0.0f;
        const XrResult r = getRate(g_session, &hz);
        if (XR_FAILED(r) || hz <= 0.0f)
        {
            LOG("headset: panel rate query failed (%s); panel rate unknown",
                XrStr(r));
            return;
        }
        g_panelRefreshHz.store(hz, std::memory_order_relaxed);
        LOG("headset: panel is running at %.1fHz", hz);
    }

    // --------------------------------------------------------------- frame

    // Log the first few frames step by step so if anything dies on the render
    // thread we can see the exact call it died on. Silent afterward.
    int g_frameNo = 0;
    inline void FLog(const char* step)
    {
        if (g_frameNo <= 3)
            LOG("frame %d: %s", g_frameNo, step);
    }

#if HALOMCCVR_HALO2_STEREO6DOF
    void ReportHalo2SynchronousDisposition(
        bool synchronousPresentationActive,
        Halo2SynchronousPresentationDecision decision,
        uint64_t preparedSerial) noexcept
    {
        // SubmitPreparedFrame is render-thread-owned, so these diagnostics need
        // no synchronization. Log the transition immediately, then only once
        // per two seconds while a non-player/cinematic path continues to omit
        // the exact player-window pair.
        static Halo2SynchronousPresentationDecision previous =
            Halo2SynchronousPresentationDecision::SharedDefault;
        static uint64_t lastStateLogMs = 0;
        const uint64_t nowMs = GetTickCount64();
        const bool reportable =
            decision == Halo2SynchronousPresentationDecision::StockScreen ||
            decision == Halo2SynchronousPresentationDecision::Drop;
        if (reportable)
        {
            if (decision != previous || nowMs - lastStateLogMs >= 2000)
            {
                if (decision ==
                    Halo2SynchronousPresentationDecision::StockScreen)
                {
                    LOG("Halo 2 C-H2-6 pre-stereo screen: prepared serial "
                        "%llu was not claimed by an eye render transaction; "
                        "submitting the stock screen-quad path from the "
                        "available game backbuffer (no stale/N-1 stereo-pair "
                        "reuse)",
                        static_cast<unsigned long long>(preparedSerial));
                }
                else
                {
                    LOG("Halo 2 C-H2-6 frame-local drop: prepared serial %llu "
                        "was claimed immediately before its original eye render "
                        "and did not produce a live admissible pair; submitting "
                        "no world layer and not sampling "
                        "the unproven game backbuffer",
                        static_cast<unsigned long long>(preparedSerial));
                }
                lastStateLogMs = nowMs;
            }
            previous = decision;
            return;
        }

        const bool previousWasReportable =
            previous == Halo2SynchronousPresentationDecision::StockScreen ||
            previous == Halo2SynchronousPresentationDecision::Drop;
        if (!previousWasReportable)
        {
            previous = decision;
            return;
        }
        if (synchronousPresentationActive &&
            (decision ==
                 Halo2SynchronousPresentationDecision::SynchronousStereo ||
             decision ==
                 Halo2SynchronousPresentationDecision::RepeatLastPair))
        {
            if (previous == Halo2SynchronousPresentationDecision::Drop)
            {
                LOG("Halo 2 C-H2-6 frame-local drop ended: prepared serial "
                    "%llu has a projection-ready exact-current-serial pair; "
                    "resuming synchronous stereo",
                    static_cast<unsigned long long>(preparedSerial));
            }
            else
            {
                LOG("Halo 2 C-H2-6 pre-stereo screen ended: prepared serial "
                    "%llu has a projection-ready exact-current-serial pair; "
                    "resuming synchronous stereo",
                    static_cast<unsigned long long>(preparedSerial));
            }
        }
        else
        {
            LOG("Halo 2 C-H2-6 %s ended: synchronous Halo 2 "
                "presentation is no longer active",
                previous == Halo2SynchronousPresentationDecision::Drop
                    ? "drop" : "pre-stereo screen");
        }
        previous = decision;
        lastStateLogMs = 0;
    }
#endif

    bool LocateViewsForUpcomingRender(XrTime displayTime);

    void PublishContactTrackingSnapshot(uint64_t serial,bool trackingFresh) noexcept
    {
        VrContactTrackingSnapshot next{};
        next.serial=serial;
        next.timeNs=g_preparedFrame.state.predictedDisplayTime;
        const int64_t pending=g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        if(!pending || next.timeNs>=pending)
            next.referenceEpoch=g_contactSpaceEpoch.load(std::memory_order_acquire);
        const bool rightFresh=trackingFresh && g_rightAimPoseValid;
        const bool leftFresh=trackingFresh && g_leftAimPoseValid;
        next.rawPrimaryValid=rightFresh;
        if(rightFresh)
        {
            const auto& q=g_rightAimPose.orientation;
            const auto& p=g_rightAimPose.position;
            const float orientation[]{q.x,q.y,q.z,q.w},position[]{p.x,p.y,p.z};
            memcpy(next.rawPrimaryOrientation,orientation,sizeof(orientation));
            memcpy(next.rawPrimaryPosition,position,sizeof(position));
        }
        const AimPoseResult aim=ComputeAimPose(CurrentFrameStockAimPoseInputs(
            rightFresh,g_rightAimPose,leftFresh,g_leftAimPose));
        next.twoHandAimActive=aim.valid && aim.twoHandActive;
        next.leftHanded=g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.primaryAimValid=aim.valid;
        if (aim.valid)
        {
            const float q[]{aim.pose.orientation.x, aim.pose.orientation.y,
                            aim.pose.orientation.z, aim.pose.orientation.w};
            memcpy(next.primaryAimOrientation, q, sizeof(q));
        }
        auto physicalInputs=CurrentAimPoseInputs(rightFresh,g_rightAimPose,leftFresh,g_leftAimPose);
        physicalInputs.twoHandEnabled=false;
        const AimPoseResult physical=ComputeAimPose(physicalInputs);
        const XrPosef poses[2]{g_leftAimPose,physical.pose};
        next.hands[0].valid=leftFresh;
        next.hands[1].valid=physical.valid;
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!next.hands[hand].valid) continue;
            const auto& pose=poses[hand];
            const float q[]{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w};
            const float p[]{pose.position.x,pose.position.y,pose.position.z};
            memcpy(next.hands[hand].orientation,q,sizeof(q));
            memcpy(next.hands[hand].position,p,sizeof(p));
        }
        const uint32_t current=g_contactTrackingIndex.load(std::memory_order_seq_cst);
        const uint32_t target=current<2 ? 1-current : 0;
        uint32_t expected=0;
        if(!g_contactTrackingStates[target].compare_exchange_strong(
                expected,kContactTrackingWriter,std::memory_order_seq_cst)) return;
        g_contactTrackingSnapshots[target]=next;
        g_contactTrackingIndex.store(target,std::memory_order_seq_cst);
        g_contactTrackingStates[target].store(0,std::memory_order_seq_cst);
    }

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    bool PublishReachRenderSnapshot(
        uint64_t preparedSerial, bool padFresh) noexcept
    {
        if (!preparedSerial || !g_headPoseValid || g_views.size() != 2)
            return false;

        ReachVrRenderSnapshot next{};
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.preparedSerial = preparedSerial;
        auto physicalInputs=CurrentAimPoseInputs(padFresh && g_rightAimPoseValid,
            g_rightAimPose,padFresh && g_leftAimPoseValid,g_leftAimPose);
        physicalInputs.twoHandEnabled=false;
        const AimPoseResult physical=ComputeAimPose(physicalInputs);
        next.rightPhysicalValid=physical.valid;
        if(physical.valid)
        {
            next.rightPhysicalOrientation[0]=physical.pose.orientation.x;
            next.rightPhysicalOrientation[1]=physical.pose.orientation.y;
            next.rightPhysicalOrientation[2]=physical.pose.orientation.z;
            next.rightPhysicalOrientation[3]=physical.pose.orientation.w;
        }
        next.predictedDisplayTimeNs = g_preparedFrame.state.predictedDisplayTime;
        const int64_t spaceChangeAt = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        if (!spaceChangeAt || next.predictedDisplayTimeNs >= spaceChangeAt)
        {
            next.trackingSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
            if (spaceChangeAt)
                g_contactSpaceChangeAtNs.store(0, std::memory_order_release);
        }
        next.headOrientation[0] = g_headPose.orientation.x;
        next.headOrientation[1] = g_headPose.orientation.y;
        next.headOrientation[2] = g_headPose.orientation.z;
        next.headOrientation[3] = g_headPose.orientation.w;
        next.headPosition[0] = g_headPose.position.x;
        next.headPosition[1] = g_headPose.position.y;
        next.headPosition[2] = g_headPose.position.z;
        // This publisher runs on the same OpenXR frame thread that just wrote
        // the controller poses in CaptureRightControllerPose. Compute from
        // those owned values directly: Reach's render reader remains lock-free,
        // and the aim/head/eyes all belong to this exact prepared serial.
        // A failed action sync leaves the last successful globals intact for
        // the legacy getters.  Never admit those stale values into a newly
        // prepared Reach serial: this snapshot is an exact-frame contract.
        const bool rightPoseFresh = padFresh && g_rightAimPoseValid;
        const bool leftPoseFresh = padFresh && g_leftAimPoseValid;
        const AimPoseResult aim = ComputeAimPose(CurrentFrameStockAimPoseInputs(
            rightPoseFresh, g_rightAimPose,
            leftPoseFresh, g_leftAimPose));
        next.rightAimValid = aim.valid;
        next.twoHandAimActive = aim.valid && aim.twoHandActive;
        if (aim.valid)
        {
            next.rightAimOrientation[0] = aim.pose.orientation.x;
            next.rightAimOrientation[1] = aim.pose.orientation.y;
            next.rightAimOrientation[2] = aim.pose.orientation.z;
            next.rightAimOrientation[3] = aim.pose.orientation.w;
            next.rightAimPosition[0] = aim.pose.position.x;
            next.rightAimPosition[1] = aim.pose.position.y;
            next.rightAimPosition[2] = aim.pose.position.z;
        }
        next.leftControllerValid = leftPoseFresh;
        if (leftPoseFresh)
        {
            next.leftControllerOrientation[0] =
                g_leftAimPose.orientation.x;
            next.leftControllerOrientation[1] =
                g_leftAimPose.orientation.y;
            next.leftControllerOrientation[2] =
                g_leftAimPose.orientation.z;
            next.leftControllerOrientation[3] =
                g_leftAimPose.orientation.w;
            next.leftControllerPosition[0] = g_leftAimPose.position.x;
            next.leftControllerPosition[1] = g_leftAimPose.position.y;
            next.leftControllerPosition[2] = g_leftAimPose.position.z;
        }
        if (padFresh)
            next.pad = g_padState;

        for (int eye = 0; eye < 2; ++eye)
        {
            if (!VR_GetEyeViewOffset(
                    eye, next.eyes[eye].position,
                    next.eyes[eye].orientation) ||
                !VR_GetEyeFov(eye, next.eyes[eye].fov))
            {
                return false;
            }
        }

        const uint32_t current =
            g_reachRenderSnapshotIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1u - current : 0u;
        uint32_t expectedState = 0;
        if (!g_reachRenderSnapshotStates[target].compare_exchange_strong(
                expectedState, kReachRenderSnapshotWriter,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst))
        {
            // A reader still owns the old non-current slot. Skipping this
            // publication is safer than waiting in the OpenXR frame path; the
            // exact-serial Reach reader will fall open for this frame.
            return false;
        }

        g_reachRenderSnapshots[target] = next;
        g_reachRenderSnapshotIndex.store(target, std::memory_order_seq_cst);
        g_reachRenderSnapshotStates[target].store(
            0, std::memory_order_seq_cst);
        return true;
    }
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
    bool PublishHalo2RenderSnapshot(
        uint64_t preparedSerial, bool padFresh) noexcept
    {
        if (!preparedSerial || !g_headPoseValid || g_views.size() != 2 ||
            TitleAdapter_GetActiveTitle() != GameTitle::Halo2)
        {
            return false;
        }
        const uint32_t generation =
            TitleAdapter_GetGeneration(GameTitle::Halo2);
        if (!generation)
            return false;

        Halo2VrRenderSnapshot next{};
        next.turnPadValid = padFresh && g_padState.valid;
        next.turnX = next.turnPadValid ? g_padState.turnX : 0.0f;
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.preparedSerial = preparedSerial;
        next.predictedDisplayTimeNs = g_preparedFrame.state.predictedDisplayTime;
        const int64_t pendingSpaceChange = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        if(!pendingSpaceChange || next.predictedDisplayTimeNs>=pendingSpaceChange)
            next.trackingSpaceEpoch=g_contactSpaceEpoch.load(std::memory_order_acquire);
        next.predictedDisplayPeriodNs =
            g_preparedFrame.state.predictedDisplayPeriod > 0
            ? static_cast<uint64_t>(
                  g_preparedFrame.state.predictedDisplayPeriod)
            : 0;
        next.predictedDisplayDeltaNs =
            g_preparedFrame.predictedDisplayDelta > 0
            ? static_cast<uint64_t>(g_preparedFrame.predictedDisplayDelta)
            : 0;
        next.generation = generation;
        AimPoseInputs independentInputs = CurrentAimPoseInputs(
            padFresh && g_rightAimPoseValid, g_rightAimPose,
            padFresh && g_leftAimPoseValid, g_leftAimPose);
        independentInputs.twoHandEnabled = false;
        const AimPoseResult independentAim = ComputeAimPose(independentInputs);
        next.independentRightAimValid = independentAim.valid;
        if (independentAim.valid)
        {
            next.independentRightAimOrientation[0] = independentAim.pose.orientation.x;
            next.independentRightAimOrientation[1] = independentAim.pose.orientation.y;
            next.independentRightAimOrientation[2] = independentAim.pose.orientation.z;
            next.independentRightAimOrientation[3] = independentAim.pose.orientation.w;
        }
        next.headOrientation[0] = g_headPose.orientation.x;
        next.headOrientation[1] = g_headPose.orientation.y;
        next.headOrientation[2] = g_headPose.orientation.z;
        next.headOrientation[3] = g_headPose.orientation.w;
        next.headPosition[0] = g_headPose.position.x;
        next.headPosition[1] = g_headPose.position.y;
        next.headPosition[2] = g_headPose.position.z;
        next.headPoseValid = true;
        // Publish the same immutable aim packet Reach and Halo 4 consume. A
        // failed action sync cannot leak an older controller pose into a new
        // Halo 2 prepared serial.
        const bool rightPoseFresh = padFresh && g_rightAimPoseValid;
        const bool leftPoseFresh = padFresh && g_leftAimPoseValid;
        const AimPoseResult aim = ComputeAimPose(CurrentFrameStockAimPoseInputs(
            rightPoseFresh, g_rightAimPose,
            leftPoseFresh, g_leftAimPose));
        next.rightAimValid = aim.valid;
        next.twoHandAimActive = aim.valid && aim.twoHandActive;
        if (aim.valid)
        {
            next.rightAimOrientation[0] = aim.pose.orientation.x;
            next.rightAimOrientation[1] = aim.pose.orientation.y;
            next.rightAimOrientation[2] = aim.pose.orientation.z;
            next.rightAimOrientation[3] = aim.pose.orientation.w;
            next.rightAimPosition[0] = aim.pose.position.x;
            next.rightAimPosition[1] = aim.pose.position.y;
            next.rightAimPosition[2] = aim.pose.position.z;
        }
        next.leftControllerValid = leftPoseFresh;
        if (leftPoseFresh)
        {
            next.leftControllerOrientation[0] = g_leftAimPose.orientation.x;
            next.leftControllerOrientation[1] = g_leftAimPose.orientation.y;
            next.leftControllerOrientation[2] = g_leftAimPose.orientation.z;
            next.leftControllerOrientation[3] = g_leftAimPose.orientation.w;
            next.leftControllerPosition[0] = g_leftAimPose.position.x;
            next.leftControllerPosition[1] = g_leftAimPose.position.y;
            next.leftControllerPosition[2] = g_leftAimPose.position.z;
        }
        for (int eye = 0; eye < 2; ++eye)
        {
            if (!VR_GetEyeViewOffset(
                    eye, next.eyes[eye].position,
                    next.eyes[eye].orientation) ||
                !VR_GetEyeFov(eye, next.eyes[eye].fov))
            {
                return false;
            }
            next.eyes[eye].fovValid = true;
            next.eyes[eye].absolutePosition[0] = g_views[eye].pose.position.x;
            next.eyes[eye].absolutePosition[1] = g_views[eye].pose.position.y;
            next.eyes[eye].absolutePosition[2] = g_views[eye].pose.position.z;
            next.eyes[eye].absoluteOrientation[0] = g_views[eye].pose.orientation.x;
            next.eyes[eye].absoluteOrientation[1] = g_views[eye].pose.orientation.y;
            next.eyes[eye].absoluteOrientation[2] = g_views[eye].pose.orientation.z;
            next.eyes[eye].absoluteOrientation[3] = g_views[eye].pose.orientation.w;
        }

        const uint32_t current =
            g_halo2RenderSnapshotIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1u - current : 0u;
        uint32_t expectedState = 0;
        if (!g_halo2RenderSnapshotStates[target].compare_exchange_strong(
                expectedState, kHalo2RenderSnapshotWriter,
                std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
            return false;
        }
        g_halo2RenderSnapshots[target] = next;
        g_halo2RenderSnapshotIndex.store(target, std::memory_order_seq_cst);
        g_halo2RenderSnapshotStates[target].store(
            0, std::memory_order_seq_cst);
        return true;
    }
#endif

#if HALOMCCVR_HALO2_STEREO6DOF
    void PublishHalo2PreparedCadence(
        uint64_t preparedSerial, XrDuration targetPeriod,
        XrDuration predictedDisplayDelta) noexcept
    {
        g_halo2PreparedCadenceSerial.store(0, std::memory_order_release);
        g_halo2PreparedTargetPeriodNs.store(
            targetPeriod > 0 ? static_cast<uint64_t>(targetPeriod) : 0,
            std::memory_order_relaxed);
        g_halo2PreparedDisplayDeltaNs.store(
            predictedDisplayDelta > 0
                ? static_cast<uint64_t>(predictedDisplayDelta)
                : 0,
            std::memory_order_relaxed);
        g_halo2PreparedCadenceSerial.store(
            preparedSerial, std::memory_order_release);
    }
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    bool PublishHalo4RenderSnapshot(
        uint64_t preparedSerial, bool padFresh) noexcept
    {
        if (!preparedSerial || g_views.size() != 2)
            return false;

        Halo4VrRenderSnapshot next{};
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.preparedSerial = preparedSerial;
        for (int eye = 0; eye < 2; ++eye)
        {
            if (!VR_GetEyeViewOffset(
                    eye, next.eyes[eye].position,
                    next.eyes[eye].orientation))
            {
                return false;
            }
            // Optional by design: a runtime that cannot report this eye's FOV
            // costs C-H4-8 only its native-cover widening, and the camera core
            // renders at Halo 4's stock FOV instead of dropping the frame.
            next.eyes[eye].fovValid =
                VR_GetEyeFov(eye, next.eyes[eye].fov);
        }
        // Same frame as the eye offsets above. Head tracking degrades to the
        // engine's own camera if this is unavailable; it never fails the pair.
        next.headPoseValid = VR_GetHeadPose(
            next.headOrientation, next.headPosition);
        // The publisher owns the OpenXR action values for this prepared frame.
        // Copy them here exactly as Reach does, so Halo 4's palette hook never
        // enters the tracking critical section and cannot mix controller poses
        // from two prepared serials.
        const bool rightPoseFresh = padFresh && g_rightAimPoseValid;
        const bool leftPoseFresh = padFresh && g_leftAimPoseValid;
        const AimPoseResult aim = ComputeAimPose(CurrentFrameStockAimPoseInputs(
            rightPoseFresh, g_rightAimPose,
            leftPoseFresh, g_leftAimPose));
        next.rightAimValid = aim.valid;
        next.twoHandAimActive = aim.valid && aim.twoHandActive;
        if (aim.valid)
        {
            next.rightAimOrientation[0] = aim.pose.orientation.x;
            next.rightAimOrientation[1] = aim.pose.orientation.y;
            next.rightAimOrientation[2] = aim.pose.orientation.z;
            next.rightAimOrientation[3] = aim.pose.orientation.w;
            next.rightAimPosition[0] = aim.pose.position.x;
            next.rightAimPosition[1] = aim.pose.position.y;
            next.rightAimPosition[2] = aim.pose.position.z;
        }
        next.leftControllerValid = leftPoseFresh;
        if (leftPoseFresh)
        {
            next.leftControllerOrientation[0] =
                g_leftAimPose.orientation.x;
            next.leftControllerOrientation[1] =
                g_leftAimPose.orientation.y;
            next.leftControllerOrientation[2] =
                g_leftAimPose.orientation.z;
            next.leftControllerOrientation[3] =
                g_leftAimPose.orientation.w;
            next.leftControllerPosition[0] = g_leftAimPose.position.x;
            next.leftControllerPosition[1] = g_leftAimPose.position.y;
            next.leftControllerPosition[2] = g_leftAimPose.position.z;
        }

        const uint32_t current =
            g_halo4RenderSnapshotIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1u - current : 0u;
        uint32_t expectedState = 0;
        if (!g_halo4RenderSnapshotStates[target].compare_exchange_strong(
                expectedState, kHalo4RenderSnapshotWriter,
                std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
            // The old slot still has a reader. Never wait in the OpenXR frame
            // path; the exact-serial camera hook will render stock this frame.
            return false;
        }
        g_halo4RenderSnapshots[target] = next;
        g_halo4RenderSnapshotIndex.store(target, std::memory_order_seq_cst);
        g_halo4RenderSnapshotStates[target].store(
            0, std::memory_order_seq_cst);
        return true;
    }
#endif

    // The wait thread: block in xrWaitFrame here instead of on the game's render
    // thread, then hand the state over. Once the render thread claims Wait(N),
    // it releases this worker immediately before Begin(N). The next Wait(N+1)
    // is dispatched at that boundary and then overlaps the game's render work.
    DWORD WINAPI FrameWaitThread(LPVOID)
    {
        while (!g_waitThreadStop.load(std::memory_order_acquire))
        {
            XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState state{XR_TYPE_FRAME_STATE};
            const uint64_t waitSequence =
                g_waitCallSequence.fetch_add(1, std::memory_order_relaxed) + 1;
            const uint32_t waitSessionEpoch =
                g_framePacingSessionEpoch.load(std::memory_order_acquire);
            LARGE_INTEGER waitStart{}, waitEnd{};
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&waitStart);
            g_waitCallInFlight.store(waitSequence, std::memory_order_release);
            if (!SetEvent(g_waitStartedEvent))
                g_waitEventSignalFailures.fetch_add(1, std::memory_order_relaxed);
            const XrResult r = xrWaitFrame(g_session, &waitInfo, &state);
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&waitEnd);
            const uint32_t returnSessionEpoch =
                g_framePacingSessionEpoch.load(std::memory_order_acquire);
            g_waitCallInFlight.store(0, std::memory_order_release);
            if (g_waitThreadStop.load(std::memory_order_acquire))
                break;
            if (XR_FAILED(r))
            {
                // Don't spin on a dead session. Report the failed worker wait;
                // the render thread never substitutes its own xrWaitFrame.
                g_waitedStateValid.store(false, std::memory_order_release);
                g_waitedStateResult.store(r, std::memory_order_relaxed);
                g_waitedStateFailed.store(true, std::memory_order_release);
                SetEvent(g_waitReadyEvent);
                Sleep(4);
                continue;
            }
            LARGE_INTEGER ready{};
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&ready);
            // Version the existing packet handoff so a consumed-event timeout
            // and overwrite cannot be mislabeled as an exact diagnostic row.
            g_waitedPacketVersion.fetch_add(1, std::memory_order_acq_rel);
            g_waitedFrameState = state;
            if constexpr (kEnableFramePacingTransitionCapture)
            {
                g_waitedCallStartQpc.store(
                    waitStart.QuadPart, std::memory_order_relaxed);
                g_waitedCallEndQpc.store(
                    waitEnd.QuadPart, std::memory_order_relaxed);
                g_waitedReadyQpc.store(
                    ready.QuadPart, std::memory_order_relaxed);
            }
            g_waitedSessionEpochStart.store(
                waitSessionEpoch, std::memory_order_relaxed);
            g_waitedSessionEpochEnd.store(
                returnSessionEpoch, std::memory_order_relaxed);
            g_waitedPacketSequence.store(
                waitSequence, std::memory_order_release);
            g_waitedPacketVersion.fetch_add(1, std::memory_order_release);
            g_waitedStateFailed.store(false, std::memory_order_release);
            g_waitedStateValid.store(true, std::memory_order_release);
            SetEvent(g_waitReadyEvent);
            // The event is only a wakeup; the exact sequence acknowledgement is
            // the permit. A timeout or stale event must never authorize another
            // xrWaitFrame, because concurrent waits are undefined by OpenXR.
            //
            // Parking here means the render thread has not claimed this packet,
            // which means the game has not reached its Present. A park of a few
            // milliseconds is the normal cadence; seconds of it is the freeze
            // that reads as a crash to the player. Time it and name it, because
            // nothing else in the log can: `status:` is emitted on the game's
            // render thread and the XInput diagnostic is driven by the game's
            // own polling, so both simply stop and leave an unexplained gap.
            // This is observation only - it never claims the packet, and no
            // frame submission behaviour changes.
            const uint64_t parkStartMs = GetTickCount64();
            bool parkStallReported = false;
            for (;;)
            {
                if (g_waitThreadStop.load(std::memory_order_acquire))
                    return 0;
                const FrameWaitPermit permit = ClassifyFrameWaitPermit(
                    waitSequence,
                    g_waitConsumedSequence.load(std::memory_order_acquire));
                if (permit == FrameWaitPermit::StartNextWait)
                    break;
                if (permit == FrameWaitPermit::Fault)
                {
                    g_waitPipelineFaulted.store(true, std::memory_order_release);
                    SetEvent(g_waitReadyEvent);
                    return 0;
                }
                const DWORD wake = WaitForSingleObject(g_waitConsumedEvent, 50);
                if (wake == WAIT_FAILED)
                {
                    g_waitPipelineFaulted.store(true, std::memory_order_release);
                    SetEvent(g_waitReadyEvent);
                    return 0;
                }
                if (!parkStallReported &&
                    GetTickCount64() - parkStartMs >= kFrameStallNoticeMs)
                {
                    parkStallReported = true;
                    g_frameStalls.fetch_add(1, std::memory_order_relaxed);
                    // Say what was true, not what is usually true. A stall while
                    // the runtime has us not-visible is real but invisible: no
                    // player experienced it, and it must never be quoted as
                    // evidence of an in-headset freeze.
                    const int shouldRender =
                        g_lastShouldRenderShared.load(std::memory_order_relaxed);
                    const char* state = SessionStateName(static_cast<XrSessionState>(
                        g_sessionStateShared.load(std::memory_order_relaxed)));
                    if (shouldRender == 1)
                        LOG("STALL: the game has not presented for %llums while "
                            "VISIBLE (session %s) - the headset is holding the "
                            "last frame we submitted and the player IS seeing a "
                            "freeze (this is the game, not the VR runtime)",
                            static_cast<unsigned long long>(kFrameStallNoticeMs),
                            state);
                    else
                        LOG("STALL: the game has not presented for %llums while "
                            "NOT VISIBLE (session %s, shouldRender=%d) - we were "
                            "submitting no layers, so the headset was not showing "
                            "the game at all. Nobody saw this one; do not read it "
                            "as an in-headset freeze",
                            static_cast<unsigned long long>(kFrameStallNoticeMs),
                            state, shouldRender);
                }
            }
            if (parkStallReported)
            {
                const uint64_t stalledMs = GetTickCount64() - parkStartMs;
                if (stalledMs > g_frameStallWorstMs.load(std::memory_order_relaxed))
                    g_frameStallWorstMs.store(stalledMs, std::memory_order_relaxed);
                LOG("STALL ENDED: the game resumed presenting after %llums",
                    static_cast<unsigned long long>(stalledMs));
            }
        }
        return 0;
    }

    bool StartFrameWaitThread()
    {
        if (g_waitThread)
            return true;
        g_waitThreadStop.store(false, std::memory_order_release);
        g_waitedStateValid.store(false, std::memory_order_release);
        g_waitedStateFailed.store(false, std::memory_order_release);
        g_waitedStateResult.store(XR_SUCCESS, std::memory_order_release);
        g_waitPacketMisses.store(0, std::memory_order_release);
        g_waitFailuresObserved.store(0, std::memory_order_release);
        g_waitEventSignalFailures.store(0, std::memory_order_release);
        g_waitCallSequence.store(0, std::memory_order_release);
        g_waitCallInFlight.store(0, std::memory_order_release);
        g_waitConsumedSequence.store(0, std::memory_order_release);
        g_waitPipelineFaulted.store(false, std::memory_order_release);
        g_abortFrameForSwapchainFailure = false;
        g_waitedPacketSequence.store(0, std::memory_order_release);
        g_waitedPacketVersion.store(0, std::memory_order_release);
        g_waitReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_waitConsumedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_waitStartedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_waitReadyEvent || !g_waitConsumedEvent || !g_waitStartedEvent)
        {
            LOG("pacing: could not create frame-wait events (%lu); XR frame "
                "preparation will remain disabled (no inline wait)",
                static_cast<unsigned long>(GetLastError()));
            if (g_waitReadyEvent)
                CloseHandle(g_waitReadyEvent);
            if (g_waitConsumedEvent)
                CloseHandle(g_waitConsumedEvent);
            if (g_waitStartedEvent)
                CloseHandle(g_waitStartedEvent);
            g_waitReadyEvent = nullptr;
            g_waitConsumedEvent = nullptr;
            g_waitStartedEvent = nullptr;
            return false;
        }
        g_waitThread = CreateThread(nullptr, 0, &FrameWaitThread, nullptr, 0, nullptr);
        if (!g_waitThread)
        {
            LOG("pacing: could not start the frame-wait thread (%lu); XR frame "
                "preparation will remain disabled (no inline wait)",
                static_cast<unsigned long>(GetLastError()));
            CloseHandle(g_waitReadyEvent);
            CloseHandle(g_waitConsumedEvent);
            CloseHandle(g_waitStartedEvent);
            g_waitReadyEvent = nullptr;
            g_waitConsumedEvent = nullptr;
            g_waitStartedEvent = nullptr;
            return false;
        }
        LOG("pacing: frame-wait thread started; the game's render thread no "
            "longer blocks in xrWaitFrame (whatever rate the runtime reports)");
        return true;
    }

    bool StopFrameWaitThread()
    {
        if (!g_waitThread)
            return true;

        g_waitThreadStop.store(true, std::memory_order_release);
        if (g_waitConsumedEvent)
            SetEvent(g_waitConsumedEvent);
        if (g_waitReadyEvent)
            SetEvent(g_waitReadyEvent);
        if (g_waitStartedEvent)
            SetEvent(g_waitStartedEvent);
        const DWORD joined = WaitForSingleObject(g_waitThread, 2000);
        if (joined != WAIT_OBJECT_0)
        {
            g_waitPipelineFaulted.store(true, std::memory_order_release);
            LOG("pacing: frame-wait worker did not stop cleanly (wait=%lu)",
                static_cast<unsigned long>(joined));
            return false;
        }

        CloseHandle(g_waitThread);
        g_waitThread = nullptr;
        if (g_waitReadyEvent)
            CloseHandle(g_waitReadyEvent);
        if (g_waitConsumedEvent)
            CloseHandle(g_waitConsumedEvent);
        if (g_waitStartedEvent)
            CloseHandle(g_waitStartedEvent);
        g_waitReadyEvent = nullptr;
        g_waitConsumedEvent = nullptr;
        g_waitStartedEvent = nullptr;
        g_waitedStateValid.store(false, std::memory_order_release);
        g_waitedStateFailed.store(false, std::memory_order_release);
        g_waitCallInFlight.store(0, std::memory_order_release);
        return true;
    }

    void PrepareNextFrame()
    {
        if (g_preparedFrame.begun)
            return;
        // Resource creation belongs at the frame preparation boundary, never
        // inside a title's optional third-camera callback.
        if(g_config.scope_enabled)
        {
            D3D11_TEXTURE2D_DESC scopeSource=g_eyeCacheDesc;
#if HALOMCCVR_HALO2_STEREO6DOF
            if(TitleAdapter_GetActiveTitle()==GameTitle::Halo2)scopeSource=g_halo2ScopeSourceDesc;
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
            if(TitleAdapter_GetActiveTitle()==GameTitle::HaloReach)
            {
                scopeSource={};
                g_reachCaptureUsers.fetch_add(1,std::memory_order_seq_cst);
                if(g_reachCaptureEnabled.load(std::memory_order_seq_cst))scopeSource=g_reachCaptureDesc;
                g_reachCaptureUsers.fetch_sub(1,std::memory_order_seq_cst);
            }
#endif
            (void)EnsureScopeCache(scopeSource);
        }

        if constexpr (kEnableFramePacingTransitionCapture)
            g_framePacingPending = {};
        ++g_frameNo;
        FLog("xrWaitFrame after DXGI Present");
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        bool haveState = false;
        bool workerPacketClaimed = false;
        bool waitPacketCoherent = true;
        uint64_t waitSequence = 0;
        uint64_t overlappingWorkerWaitSequence = 0;
        uint32_t waitSessionEpoch = 0;
        int64_t waitCallStartQpc = 0;
        int64_t waitCallEndQpc = 0;
        int64_t waitReadyQpc = 0;
        LARGE_INTEGER consumedSignal{};
        LARGE_INTEGER nextWaitDispatchGateStart{}, nextWaitDispatchGateEnd{};
        DWORD eventWaitResult = WAIT_OBJECT_0;
        DWORD nextWaitEventResult = WAIT_OBJECT_0;
        uint64_t nextWaitDispatchSequence = 0;
        bool nextWaitDispatchObservedBeforeBegin = false;
        bool nextWaitDispatchGateFailed = false;
        LARGE_INTEGER waitStart{}, waitEnd{};
        QueryPerformanceCounter(&waitStart);
        if (g_waitThread &&
            !g_waitPipelineFaulted.load(std::memory_order_acquire))
        {
            // Events are wakeup hints only. Always inspect packet status before
            // and after waiting so a coalesced or just-missed signal cannot
            // strand a valid packet while the exact-sequence worker is parked.
            if (!g_waitedStateValid.load(std::memory_order_acquire) &&
                !g_waitedStateFailed.load(std::memory_order_acquire))
            {
                // Normally already signalled. The timeout is only a bounded
                // observation point; it never transfers Wait ownership.
                eventWaitResult = WaitForSingleObject(g_waitReadyEvent, 1000);
            }
            // A successful packet supersedes any stale failure notification.
            if (g_waitedStateValid.exchange(false, std::memory_order_acq_rel))
            {
                g_waitedStateFailed.store(false, std::memory_order_release);
                const uint64_t versionBefore =
                    g_waitedPacketVersion.load(std::memory_order_acquire);
                const uint64_t sequenceBefore =
                    g_waitedPacketSequence.load(std::memory_order_acquire);
                frameState = g_waitedFrameState;
                if constexpr (kEnableFramePacingTransitionCapture)
                {
                    waitCallStartQpc = g_waitedCallStartQpc.load(
                        std::memory_order_relaxed);
                    waitCallEndQpc = g_waitedCallEndQpc.load(
                        std::memory_order_relaxed);
                    waitReadyQpc = g_waitedReadyQpc.load(
                        std::memory_order_relaxed);
                }
                const uint32_t epochAtStart =
                    g_waitedSessionEpochStart.load(std::memory_order_relaxed);
                const uint32_t epochAtEnd =
                    g_waitedSessionEpochEnd.load(std::memory_order_relaxed);
                const uint64_t sequenceAfter =
                    g_waitedPacketSequence.load(std::memory_order_acquire);
                const uint64_t versionAfter =
                    g_waitedPacketVersion.load(std::memory_order_acquire);
                overlappingWorkerWaitSequence =
                    g_waitCallInFlight.load(std::memory_order_acquire);
                const uint32_t currentEpoch =
                    g_framePacingSessionEpoch.load(std::memory_order_acquire);
                waitSequence = sequenceAfter;
                waitSessionEpoch = epochAtStart;
                waitPacketCoherent = sequenceBefore != 0 &&
                    sequenceBefore == sequenceAfter &&
                    versionBefore == versionAfter &&
                    (versionBefore & 1u) == 0 && epochAtStart != 0 &&
                    epochAtStart == epochAtEnd &&
                    epochAtStart == currentEpoch &&
                    overlappingWorkerWaitSequence == 0;
                haveState = true;
                workerPacketClaimed = waitPacketCoherent;
            }
            else if (g_waitedStateFailed.exchange(
                        false, std::memory_order_acq_rel))
            {
                QueryPerformanceCounter(&waitEnd);
                g_waitDurationsMs.Add(QpcMs(waitEnd.QuadPart - waitStart.QuadPart));
                ++g_frameOrderFailures;
                g_waitFailuresObserved.fetch_add(
                    1, std::memory_order_relaxed);
                return;
            }
        }
        if (haveState && !waitPacketCoherent)
        {
            // Do not acknowledge an incoherent or cross-session packet. The
            // worker remains parked, so drain and reset the running session.
            ++g_frameOrderFailures;
            EnterFrameWaitFatalDrain(
                "The OpenXR wait worker published an incoherent or stale "
                "frame packet");
            return;
        }
        if (!haveState)
        {
            if (g_waitPipelineFaulted.load(std::memory_order_acquire) ||
                (g_waitThread &&
                 WaitForSingleObject(g_waitThread, 0) == WAIT_OBJECT_0))
            {
                ++g_frameOrderFailures;
                EnterFrameWaitFatalDrain(
                    "The exclusive OpenXR wait-worker pipeline stopped");
                return;
            }
            // The worker is the sole xrWaitFrame owner. Never substitute an
            // inline wait: it can race an in-flight worker wait, and it restores
            // the exact render-thread pacing stall this pipeline exists to fix.
            QueryPerformanceCounter(&waitEnd);
            g_waitDurationsMs.Add(QpcMs(waitEnd.QuadPart - waitStart.QuadPart));
            g_waitPacketMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        QueryPerformanceCounter(&waitEnd);
        g_waitDurationsMs.Add(QpcMs(waitEnd.QuadPart - waitStart.QuadPart));

        // A subsequent xrWaitFrame is explicitly legal before this Begin and
        // must block until this frame has begun. Dispatch it now so the
        // runtime applies cadence delay on the worker, not inside xrBeginFrame
        // on Halo's render thread. Publish the exact claimed sequence before
        // waking; stale auto-reset event credits cannot advance the worker. Do
        // not rely on scheduler luck alone: observe the exact Wait(N+1)
        // dispatch boundary before entering Begin(N), then use cross-frame
        // timing to verify the runtime-call relationship in the headset.
        if (ShouldReleaseFrameWaitWorkerBeforeBegin(
                g_waitThread != nullptr, workerPacketClaimed))
        {
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&consumedSignal);
            g_waitConsumedSequence.store(waitSequence, std::memory_order_release);
            if (!SetEvent(g_waitConsumedEvent))
            {
                ++g_frameOrderFailures;
                g_waitEventSignalFailures.fetch_add(
                    1, std::memory_order_relaxed);
            }

            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&nextWaitDispatchGateStart);
            const uint64_t dispatchDeadlineMs = GetTickCount64() + 1000;
            for (;;)
            {
                nextWaitDispatchSequence =
                    g_waitCallInFlight.load(std::memory_order_acquire);
                if (IsExpectedNextFrameWaitDispatch(
                        waitSequence, nextWaitDispatchSequence))
                {
                    nextWaitDispatchObservedBeforeBegin = true;
                    break;
                }
                if (nextWaitDispatchSequence != 0 ||
                    g_waitPipelineFaulted.load(std::memory_order_acquire) ||
                    g_waitThreadStop.load(std::memory_order_acquire) ||
                    g_waitedStateFailed.load(std::memory_order_acquire))
                {
                    break;
                }
                const uint64_t dispatchNowMs = GetTickCount64();
                if (dispatchNowMs >= dispatchDeadlineMs)
                {
                    nextWaitEventResult = WAIT_TIMEOUT;
                    break;
                }
                nextWaitEventResult =
                    WaitForSingleObject(
                        g_waitStartedEvent,
                        static_cast<DWORD>(dispatchDeadlineMs - dispatchNowMs));
                if (nextWaitEventResult == WAIT_FAILED)
                    break;
            }
            nextWaitDispatchSequence =
                g_waitCallInFlight.load(std::memory_order_acquire);
            nextWaitDispatchObservedBeforeBegin =
                IsExpectedNextFrameWaitDispatch(
                    waitSequence, nextWaitDispatchSequence);
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&nextWaitDispatchGateEnd);
            if (!nextWaitDispatchObservedBeforeBegin)
            {
                ++g_frameOrderFailures;
                nextWaitDispatchGateFailed = true;
            }
        }

        FLog("xrBeginFrame before Halo render");
        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        LARGE_INTEGER beginStart{}, beginEnd{};
        if constexpr (kEnableFramePacingTransitionCapture)
            QueryPerformanceCounter(&beginStart);
        const XrResult beginResult = xrBeginFrame(g_session, &beginInfo);
        QueryPerformanceCounter(&beginEnd);
        if (XR_FAILED(beginResult))
        {
            ++g_frameOrderFailures;
            LOG("timing: xrBeginFrame failed: %s", XrStr(beginResult));
            EnterFrameWaitFatalDrain(
                "xrBeginFrame failed after the exclusive wait worker was "
                "released");
            return;
        }

        g_beginFrameQpc = beginEnd;

        g_preparedFrame.state = frameState;
        g_preparedFrame.begun = true;
        g_preparedFrame.serial = ++g_nextPreparedSerial;
        g_preparedFrame.predictedDisplayDelta = 0;
        g_preparedShouldRender.store(
            frameState.shouldRender == XR_TRUE, std::memory_order_release);

        if constexpr (kEnableFramePacingTransitionCapture)
        {
            FramePacingRecord& pacing = g_framePacingPending;
            pacing.serial = g_preparedFrame.serial;
            pacing.waitSequence = waitSequence;
            pacing.overlappingWorkerWaitSequence =
                overlappingWorkerWaitSequence;
            pacing.nextWaitDispatchSequence = nextWaitDispatchSequence;
            pacing.sessionEpoch = waitSessionEpoch;
            pacing.predictedDisplayTimeNs = frameState.predictedDisplayTime;
            pacing.predictedDisplayPeriodNs = frameState.predictedDisplayPeriod;
            pacing.waitCallStartQpc = waitCallStartQpc;
            pacing.waitCallEndQpc = waitCallEndQpc;
            pacing.waitReadyQpc = waitReadyQpc;
            pacing.renderWaitStartQpc = waitStart.QuadPart;
            pacing.renderWaitEndQpc = waitEnd.QuadPart;
            pacing.nextWaitDispatchGateStartQpc =
                nextWaitDispatchGateStart.QuadPart;
            pacing.nextWaitDispatchGateEndQpc =
                nextWaitDispatchGateEnd.QuadPart;
            pacing.beginStartQpc = beginStart.QuadPart;
            pacing.beginEndQpc = beginEnd.QuadPart;
            pacing.consumedSignalQpc = consumedSignal.QuadPart;
            pacing.waitResult = XR_SUCCESS;
            pacing.beginResult = beginResult;
            pacing.eventWaitResult = eventWaitResult;
            pacing.nextWaitEventResult = nextWaitEventResult;
            pacing.title = static_cast<uint8_t>(TitleAdapter_GetActiveTitle());
            pacing.waitPacketCoherent = waitPacketCoherent;
            pacing.nextWaitDispatchObservedBeforeBegin =
                nextWaitDispatchObservedBeforeBegin;
            pacing.shouldRender = frameState.shouldRender == XR_TRUE;
            pacing.focused = g_sessionState == XR_SESSION_STATE_FOCUSED;
            pacing.stereo = g_stereoEnabled.load(std::memory_order_relaxed);
            pacing.headTracking = Game_IsHeadTrackingApplied();
            pacing.scopeActive = g_scopeActive.load(std::memory_order_relaxed);
            Game_ReadFramePerfCounters(g_framePacingPerfStart);
        }

        if (nextWaitDispatchGateFailed)
        {
            EndPreparedFrameWithoutLayers("next wait dispatch gate failed");
            EnterFrameWaitFatalDrain(
                "The exclusive OpenXR wait worker did not dispatch the exact "
                "next wait before xrBeginFrame");
            return;
        }

        if (g_lastPredictedDisplayTime)
        {
            const XrDuration delta =
                frameState.predictedDisplayTime - g_lastPredictedDisplayTime;
            const XrDuration period = frameState.predictedDisplayPeriod;
            if (delta > 0)
                g_preparedFrame.predictedDisplayDelta = delta;
            if (period > 0)
                g_displayPeriodNs.store(static_cast<uint64_t>(period),
                                        std::memory_order_relaxed);
            if (delta <= 0)
                ++g_duplicatePredictions;
            else if (period > 0)
            {
                g_predictionErrorMs.Add(
                    std::fabs(static_cast<double>(delta - period)) / 1000000.0);
                if (delta > period + period / 2)
                    g_missedPredictions += static_cast<uint64_t>(
                        std::max<XrDuration>(1, delta / period - 1));
            }
        }
        g_lastPredictedDisplayTime = frameState.predictedDisplayTime;

        // Input first, then views/head as late as possible. Every locate uses
        // the exact predicted time associated with this begun frame.
        const bool upcomingPadFresh =
            CaptureRightControllerPose(frameState.predictedDisplayTime);
        const bool upcomingViewsValid =
            LocateViewsForUpcomingRender(frameState.predictedDisplayTime);
        g_preparedFrame.viewsValid = upcomingViewsValid;
        g_preparedFrame.viewCount = upcomingViewsValid
            ? static_cast<uint32_t>(g_views.size()) : 0;
        g_preparedViewSerialPublished.store(
            upcomingViewsValid ? g_preparedFrame.serial : 0,
            std::memory_order_release);
        const bool upcomingHeadValid =
            CaptureHeadPose(frameState.predictedDisplayTime);
        {
            halo_ce::Tracking ce{};
            const bool enabled=TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
                upcomingViewsValid&&upcomingHeadValid&&g_views.size()==2&&
                frameState.shouldRender&&g_stereoEnabled.load()&&Game_IsHeadTracking()&&
                !g_pausePresentation.load()&&!VR_IsPausePresentationTarget();
            if (enabled)
            {
                ce.serial=g_preparedFrame.serial;
                ce.generation=TitleAdapter_GetGeneration(GameTitle::HaloCE);
                ce.spaceEpoch=g_contactSpaceEpoch.load(std::memory_order_acquire);
                ce.predictedDisplayTimeNs=frameState.predictedDisplayTime;
                ce.motionBlur=g_config.motion_blur;
                ce.disableAnniversaryLensFlares=g_config.ce_anniversary_disable_lens_flares;
                ce.hud={g_config.hud_size,g_config.hud_aspect,g_config.hud_curvature,
                    g_config.hud_vertical_offset,g_config.hide_hud};
                ce.headPosition={g_headPose.position.x,g_headPose.position.y,g_headPose.position.z};
                ce.headOrientation={g_headPose.orientation.x,g_headPose.orientation.y,g_headPose.orientation.z,g_headPose.orientation.w};
                // Same action-sync, role routing, mount calibration and support
                // grip as H2/Reach/H4, frozen with the eyes for native hot hooks.
                // Missing controller tracking affects hands/aim only.
                auto& rig=ce.controllers;
                rig.leftHanded=g_capturedLeftHanded.load(std::memory_order_acquire);
                rig.handAlignment=rig.leftHanded&&g_config.experimental_hand_alignment;
                rig.turnSmooth=g_config.turn_smooth;
                rig.vehicleSmoothTurn=g_config.vehicle_smooth_turn;
                rig.turnSnapDeg=g_config.turn_snap_deg;
                rig.turnSmoothDegS=g_config.turn_smooth_deg_s;
                rig.vehicleMotion=g_config.vehicle_motion;
                rig.vehicleViewFollow=g_config.vehicle_view_follow;
                // CE bring-up: physical body following is deferred until the
                // basic VR implementation has passed the user's headset test.
                rig.roomscaleEnabled=false;
                rig.controlsPresentationBlocked=!Game_CeControllerFeaturesRequested()||
                    Menu_IsOpen()||VR_IsPausePresentation()||VR_IsPausePresentationTarget()||
                    VR_IsCutsceneTheaterActive();
                rig.armIk=g_config.arm_ik;
                rig.floatingHands=g_config.floating_hands;
                rig.shoulderLevel=g_config.shoulder_level;
                rig.gunScale=g_config.gun_scale;
                rig.supportScale=g_config.left_hand_scale;
                rig.supportForwardM=g_config.left_hand_forward_m;
                rig.visualLeftHandOffset[0]=g_config.left_hand_mesh_x_m;
                rig.visualLeftHandOffset[1]=g_config.left_hand_mesh_y_m;
                rig.visualLeftHandOffset[2]=g_config.left_hand_mesh_z_m;
                rig.visualRightHandOffset[0]=g_config.right_hand_mesh_x_m;
                rig.visualRightHandOffset[1]=g_config.right_hand_mesh_y_m;
                rig.visualRightHandOffset[2]=g_config.right_hand_mesh_z_m;
                rig.visualPitchDeg=g_config.barrel_pitch_deg;
                rig.gunBarrelAim=g_config.gun_barrel_aim;
                rig.visualYawDeg=g_config.barrel_yaw_deg;
                rig.visualRollDeg=g_config.barrel_roll_deg;
                rig.supportMountPitchDeg=g_config.gun_pitch_deg;
                rig.supportMountYawDeg=g_config.gun_yaw_deg;
                rig.supportMountRollDeg=g_config.gun_roll_deg;
                rig.gunForwardM=g_config.gun_forward_m;
                rig.gunRightM=g_config.gun_right_m;
                rig.gunUpM=g_config.gun_up_m;
                rig.shoulderBackM=g_config.shoulder_back_m;
                rig.primaryShoulderDrop=g_config.right_shoulder_drop;
                const auto pose=[](const XrPosef& source,bool valid) {
                    halo_ce::ControllerPose result{};
                    if (!valid) return result;
                    result.position={source.position.x,source.position.y,source.position.z};
                    result.orientation={source.orientation.x,source.orientation.y,source.orientation.z,source.orientation.w};
                    result.valid=halo_ce::Finite(result.position)&&halo_ce::Valid(result.orientation);
                    return result;
                };
                auto aimInputs=CurrentFrameStockAimPoseInputs(
                    upcomingPadFresh&&g_rightAimPoseValid,g_rightAimPose,
                    upcomingPadFresh&&g_leftAimPoseValid,g_leftAimPose);
                const auto aim=ComputeAimPose(aimInputs);
                rig.primaryAim=pose(aim.pose,aim.valid);
                rig.twoHandAimActive=rig.primaryAim.valid&&aim.twoHandActive;
                aimInputs.twoHandEnabled=false;
                const auto independent=ComputeAimPose(aimInputs);
                rig.independentPrimaryAim=pose(independent.pose,independent.valid);
                rig.support=pose(g_leftAimPose,upcomingPadFresh&&g_leftAimPoseValid);
                for (int hand=0;hand<2;++hand)
                    rig.physical[hand]=pose(g_physicalControllerPose[hand],
                        upcomingPadFresh&&g_physicalControllerValid[hand]);
                rig.padValid=upcomingPadFresh&&g_padState.valid;
                if (rig.padValid)
                {
                    rig.turnX=g_padState.turnX;
                    rig.moveX=g_padState.moveX;
                    rig.moveY=g_padState.moveY;
                }
                for (int eye=0;eye<2;++eye)
                {
                    const auto& pose=g_views[eye].pose; const auto& fov=g_views[eye].fov;
                    ce.eyes[eye].offset={pose.position.x-ce.headPosition.x,
                        pose.position.y-ce.headPosition.y,pose.position.z-ce.headPosition.z};
                    ce.eyes[eye].orientation={pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w};
                    ce.eyes[eye].fov[0]=fov.angleLeft; ce.eyes[eye].fov[1]=fov.angleRight;
                    ce.eyes[eye].fov[2]=fov.angleUp; ce.eyes[eye].fov[3]=fov.angleDown;
                }
            }
            HaloCE_PublishTracking(ce,enabled);
        }
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
        if (upcomingViewsValid && upcomingHeadValid &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            PublishHalo2RenderSnapshot(
                g_preparedFrame.serial, upcomingPadFresh);
        }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
        // MUST stay below CaptureHeadPose. That call is the only writer of the
        // head pose this snapshot carries, so publishing above it would hand
        // Halo 4 the PREVIOUS frame's head while its eye offsets, its FOV and
        // the layer pose submitted later all describe THIS frame - a full frame
        // of head latency plus a render/layer pose mismatch the compositor
        // reprojects against, which reads as the world swimming when you turn.
        // Reach's publish sits below it for the same reason.
        //
        // Deliberately NOT gated on upcomingHeadValid, unlike Reach: for Halo 4
        // the head pose is optional. Without it the snapshot simply carries
        // headPoseValid=false and the eyes still render from the engine's own
        // camera, so a tracking dropout costs head tracking and not stereo.
        if (upcomingViewsValid &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        {
            PublishHalo4RenderSnapshot(
                g_preparedFrame.serial, upcomingPadFresh);
        }
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
        if (upcomingViewsValid && upcomingHeadValid)
            PublishReachRenderSnapshot(
                g_preparedFrame.serial, upcomingPadFresh);
#endif

        PublishContactTrackingSnapshot(g_preparedFrame.serial,
            upcomingPadFresh && upcomingHeadValid && upcomingViewsValid);
        LARGE_INTEGER preparedAt{};
        QueryPerformanceCounter(&preparedAt);
        g_prepareQpcPublished.store(static_cast<uint64_t>(preparedAt.QuadPart),
                                    std::memory_order_release);
#if HALOMCCVR_HALO2_STEREO6DOF
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            PublishHalo2PreparedCadence(
                g_preparedFrame.serial,
                g_preparedFrame.state.predictedDisplayPeriod,
                g_preparedFrame.predictedDisplayDelta);
        }
        else
        {
            g_halo2PreparedCadenceSerial.store(0, std::memory_order_release);
        }
#endif
        g_preparedSerialPublished.store(g_preparedFrame.serial,
                                        std::memory_order_release);
        if (g_frameNo == 1)
            LOG("timing: exact OpenXR pipeline active; headset smoothing %.1f%%",
                std::clamp(g_config.headset_smoothing, 0.0f, 0.10f) * 100.0f);
        if constexpr (kEnableFramePacingTransitionCapture)
        {
            LARGE_INTEGER prepareEnd{};
            QueryPerformanceCounter(&prepareEnd);
            g_framePacingPending.prepareEndQpc = prepareEnd.QuadPart;
        }
    }

    bool LocateViewsForUpcomingRender(XrTime displayTime)
    {
        if (g_views.empty())
            return false;
        XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
        info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        info.displayTime = displayTime;
        info.space = g_localSpace;
        XrViewState state{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        const XrResult result = xrLocateViews(
            g_session, &info, &state, static_cast<uint32_t>(g_views.size()),
            &count, g_views.data());
        return XR_SUCCEEDED(result) && count == g_views.size() &&
            (state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
            (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    }

    void SubmitPreparedFrame(IDXGISwapChain* sc)
    {
        if (!g_preparedFrame.begun)
            return;
        const XrFrameState fs = g_preparedFrame.state;
        HaloCE_SetDlssRequested(TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
            g_config.upscaler==dlss::kUpscalerDlss&&Dlss_IsAvailable());
        HaloCE_PresentResources(g_device,g_context);
        struct CeLease
        {
            halo_ce::EyeCache::Completed pair;
            ~CeLease() {
                for(auto& eye:g_dlssEye) eye.borrowedCeDepth=nullptr;
                if (pair.borrowId) HaloCE_ReleasePair(pair.borrowId);
            }
        } ceLease{};
        const bool ceTitle=TitleAdapter_GetActiveTitle()==GameTitle::HaloCE;
        const bool ceImages=ceTitle&&HaloCE_AcquirePair(g_context,g_preparedFrame.serial,
            g_contactSpaceEpoch.load(std::memory_order_acquire),ceLease.pair);
        const bool ceOwned=ceTitle&&HaloCE_OwnsPresentation();
        if(ceTitle) {
            static uint64_t previousSerial=0,previousEpoch=0,previousSpace=0;
            static uint32_t previousGeneration=0;
            const bool depthPair=ceImages&&ceLease.pair.depthHistoryEpoch&&
                ceLease.pair.depthViews[0]&&ceLease.pair.depthViews[1]&&
                ceLease.pair.depthCameras[0].valid&&ceLease.pair.depthCameras[1].valid;
            for(int eye=0;eye<2;++eye) {
                auto& state=g_dlssEye[eye];
                state.published=state.depthValid=depthPair;
                if(depthPair) {
                    state.current=ceLease.pair.depthCameras[eye];
                    state.borrowedCeDepth=ceLease.pair.depthViews[eye];
                    state.depthCopyDesc=ceLease.pair.depthDescriptor;
                    state.resetPending|=previousSerial>=ceLease.pair.key.serial||
                        previousEpoch!=ceLease.pair.depthHistoryEpoch||previousSpace!=ceLease.pair.key.spaceEpoch||
                        previousGeneration!=ceLease.pair.key.generation;
                }
            }
            if(depthPair) {
                previousSerial=ceLease.pair.key.serial;previousEpoch=ceLease.pair.depthHistoryEpoch;
                previousSpace=ceLease.pair.key.spaceEpoch;previousGeneration=ceLease.pair.key.generation;
            }
        }
        float comfortFadeAlpha = UpdatePauseTransition();

        // M2: per-eye pose + field of view for this frame (foundation for
        // stereo rendering — not used to render yet).
        bool viewsValid = false;
        uint32_t locatedViewCount = 0;
        if (!g_views.empty())
        {
            XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = g_localSpace;
            XrViewState vs{XR_TYPE_VIEW_STATE};
            if (XR_SUCCEEDED(xrLocateViews(g_session, &vli, &vs, (uint32_t)g_views.size(),
                                           &locatedViewCount, g_views.data())) &&
                locatedViewCount == g_views.size() &&
                (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
                (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
            {
                viewsValid = true;
                static bool loggedEyes = false;
                if (!loggedEyes)
                {
                    for (uint32_t i = 0; i < locatedViewCount; i++)
                        LOG("M2: eye %u pose(%.3f,%.3f,%.3f) fov L%.1f R%.1f U%.1f D%.1f deg", i,
                            g_views[i].pose.position.x, g_views[i].pose.position.y, g_views[i].pose.position.z,
                            g_views[i].fov.angleLeft * 57.2958f, g_views[i].fov.angleRight * 57.2958f,
                            g_views[i].fov.angleUp * 57.2958f, g_views[i].fov.angleDown * 57.2958f);
                    // Interpupillary distance = horizontal gap between the eye poses.
                    if (locatedViewCount >= 2)
                        LOG("M2: eye separation (IPD) = %.1f mm",
                            (g_views[1].pose.position.x - g_views[0].pose.position.x) * 1000.0f);
                    loggedEyes = true;
                }
            }
        }

        XrCompositionLayerQuad screenQuad, menuQuad, reticleQuad, secondaryReticleQuad, scopeQuad, fadeQuad, nativeCursorQuad, nativeSubtitleQuad;
        XrCompositionLayerQuad theaterQuads[2]{};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjection theaterProjection{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView theaterProjectionViews[2]{
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        // Reused across frames (Frame runs only on the render thread) so the
        // per-frame layer assembly allocates nothing in steady state.
        static std::vector<XrCompositionLayerProjectionView> projectionViews;
        static std::vector<XrCompositionLayerBaseHeader*> layers;
        projectionViews.clear();
        layers.clear();
        g_nativeTheatreCaptionReady=false;
        const bool nativeSubtitleReady=fs.shouldRender && comfortFadeAlpha<=0.01f &&
            !Menu_IsOpen() && PrepareNativeSubtitle(nativeSubtitleQuad);
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
        bool halo2ProjectionQueued = false;
#endif
#if HALOMCCVR_HALO2_STEREO6DOF
        bool halo2StrictStockScreenQueued = false;
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
        // Count a headset-visible C-H4-8 pair only after the frame that queues
        // its projection layer is accepted by xrEndFrame.
        bool halo4ProjectionQueued = false;
#endif

        // Build the descriptors every frame from the predicted eye poses/FOV.
        // A later M2 render hook only needs to fill/release both swapchain
        // images and replace the mono quad in `layers` with `projection`.
        if (viewsValid && g_stereoChain != XR_NULL_HANDLE && locatedViewCount == 2)
        {
            projection.space = g_localSpace;
            projectionViews.assign(locatedViewCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

            // RenderViewHook positions and rotates each raster camera from the
            // same per-eye view offsets (VR_GetEyeViewOffset), so the images
            // are canted the way PSVR2 reports its views — submit the real
            // per-eye orientations. Submitting a shared midpoint orientation
            // here under-covers the outward-angled lens edge and shows as a
            // black border at the outer edge of each eye.
            // The current compositor contract accepts a symmetric raster cover.
            // Established title paths render one wide enough to contain the
            // headset's asymmetric per-eye angles; H4 C-H4-7 instead admits only
            // a runtime-confirmed zero-center native projection.
            //
            // We used to submit the cover itself as the layer FOV over the whole
            // slice. That is legal OpenXR and SteamVR's compositor resolves it
            // correctly, but ALVR does not: it lens-corrects and reprojects using
            // its own view parameters rather than the layer's, so a layer whose
            // FOV is not the native view FOV gets sampled with the wrong frustum -
            // warped, stretched, and wrong differently per eye, which the viewer
            // cannot fuse. That is alvr-org/ALVR#1306, open since 2022: "ALVR does
            // not account for the missing space in the frame when the FOV is any
            // lower than 100%".
            //
            // Measured 2026-07-28, one Quest 3, one build (2458ed8), identical
            // reported optics (L-54.0 R40.0 U44.0 D-55.0, IPD 69.3 mm): clean
            // through Virtual Desktop and Steam Link, doubled through ALVR at both
            // 90 and 120 Hz, and the image snaps correct the instant the SteamVR
            // dashboard composites it.
            //
            // So submit the runtime's OWN per-eye FOV and let imageRect select the
            // sub-rectangle of the cover that corresponds to it. Every other VR
            // application submits the FOV the runtime handed it; doing the same
            // costs no resolution, because the compositor was already cropping to
            // exactly this region, and it makes the layer correct for any
            // compositor rather than only for those that honour a custom FOV.
            float haloHalfX[2] = {atanf(1.091595f), atanf(1.091595f)};
            float haloHalfY[2] = {atanf(1.114286f), atanf(1.114286f)};
            const bool renderFovsValid = ceTitle ? ceImages : Game_GetRenderHalfFovs(
                g_preparedFrame.serial, haloHalfX, haloHalfY);
            if (ceImages)
                for (int eye=0;eye<2;++eye)
                { haloHalfX[eye]=ceLease.pair.covers[eye].halfX; haloHalfY[eye]=ceLease.pair.covers[eye].halfY; }
            bool nativeFovValid = renderFovsValid;
            for (uint32_t i = 0; nativeFovValid && i < locatedViewCount; ++i)
            {
                // Tangent space: the cover maps linearly across the slice, x from
                // -coverX (left edge) to +coverX, y from +coverY (TOP row) down to
                // -coverY. Solve for the native frustum's edges in pixels.
                XrFovf native = g_views[i].fov;
                if (ceImages)
                {
                    const auto& f=ceLease.pair.tracking.eyes[i].fov;
                    native={f[0],f[1],f[2],f[3]};
                }
                const float coverX = tanf(haloHalfX[i]);
                const float coverY = tanf(haloHalfY[i]);
                const float nl = tanf(native.angleLeft);
                const float nr = tanf(native.angleRight);
                const float nu = tanf(native.angleUp);
                const float nd = tanf(native.angleDown);
                const float w = static_cast<float>(g_stereoW);
                const float h = static_cast<float>(g_stereoH);
                if (!std::isfinite(coverX) || !std::isfinite(coverY) ||
                    coverX <= 0.0f || coverY <= 0.0f || w <= 0.0f || h <= 0.0f ||
                    !std::isfinite(nl) || !std::isfinite(nr) ||
                    !std::isfinite(nu) || !std::isfinite(nd) ||
                    nr <= nl || nu <= nd ||
                    // The cover must actually contain the native frustum. If it
                    // does not, the raster is missing pixels the headset wants and
                    // cropping to it would show a hard edge, so keep the whole
                    // slice for this frame instead of inventing coverage.
                    nl < -coverX || nr > coverX || nu > coverY || nd < -coverY)
                {
                    nativeFovValid = false;
                    break;
                }

                auto clampToSlice = [](long value, long lo, long hi) -> int32_t {
                    return static_cast<int32_t>(
                        value < lo ? lo : (value > hi ? hi : value));
                };
                int32_t x0 = clampToSlice(
                    lroundf(w * (nl + coverX) / (2.0f * coverX)), 0, (long)g_stereoW - 1);
                int32_t x1 = clampToSlice(
                    lroundf(w * (nr + coverX) / (2.0f * coverX)), x0 + 1, (long)g_stereoW);
                int32_t y0 = clampToSlice(
                    lroundf(h * (coverY - nu) / (2.0f * coverY)), 0, (long)g_stereoH - 1);
                int32_t y1 = clampToSlice(
                    lroundf(h * (coverY - nd) / (2.0f * coverY)), y0 + 1, (long)g_stereoH);

                // Rounding to whole pixels moves the edges by up to half a pixel,
                // so derive the submitted FOV back from the integer rect. The pair
                // then describes the same frustum exactly, which is the property
                // the compositor relies on.
                projectionViews[i].pose = g_views[i].pose;
                if (ceImages)
                {
                    const auto& tracking=ceLease.pair.tracking; const auto& eye=tracking.eyes[i];
                    projectionViews[i].pose.position={tracking.headPosition.x+eye.offset.x,
                        tracking.headPosition.y+eye.offset.y,tracking.headPosition.z+eye.offset.z};
                    projectionViews[i].pose.orientation={eye.orientation.x,eye.orientation.y,eye.orientation.z,eye.orientation.w};
                }
                projectionViews[i].fov = {
                    atanf(coverX * (2.0f * x0 / w - 1.0f)),
                    atanf(coverX * (2.0f * x1 / w - 1.0f)),
                    atanf(coverY * (1.0f - 2.0f * y0 / h)),
                    atanf(coverY * (1.0f - 2.0f * y1 / h))};
                projectionViews[i].subImage.swapchain = g_stereoChain;
                projectionViews[i].subImage.imageRect = {
                    {x0, y0}, {x1 - x0, y1 - y0}};
                projectionViews[i].subImage.imageArrayIndex = i;

                // Log on the first frame and again whenever the cover or
                // the rect changes (a graphics-mode switch, a new title),
                // at most every two seconds; a once-ever line could not
                // describe Halo 2's two renderers.
                static float loggedCoverX = -1.0f, loggedCoverY = -1.0f;
                static int32_t loggedRectW = -1, loggedRectH = -1;
                static uint64_t loggedNativeFovMs = 0;
                const uint64_t nativeFovNowMs = GetTickCount64();
                const bool nativeFovChanged =
                    loggedCoverX != coverX || loggedCoverY != coverY ||
                    loggedRectW != x1 - x0 || loggedRectH != y1 - y0;
                if (i + 1 == locatedViewCount && nativeFovChanged &&
                    nativeFovNowMs - loggedNativeFovMs >= 2000)
                {
                    loggedCoverX = coverX;
                    loggedCoverY = coverY;
                    loggedRectW = x1 - x0;
                    loggedRectH = y1 - y0;
                    loggedNativeFovMs = nativeFovNowMs;
                    LOG("M2: submitting native per-eye FOV; eye %u cover "
                        "%.1f/%.1f deg -> rect (%d,%d)+%dx%d of %ux%u, fov "
                        "L%.1f R%.1f U%.1f D%.1f deg",
                        i, haloHalfX[i] * 57.2958f, haloHalfY[i] * 57.2958f,
                        x0, y0, x1 - x0, y1 - y0, g_stereoW, g_stereoH,
                        projectionViews[i].fov.angleLeft * 57.2958f,
                        projectionViews[i].fov.angleRight * 57.2958f,
                        projectionViews[i].fov.angleUp * 57.2958f,
                        projectionViews[i].fov.angleDown * 57.2958f);
                }
            }
            if (renderFovsValid && !nativeFovValid)
            {
                // Loud, never silent: fall back to submitting the full cover, which
                // is what shipped through 0.3.0 and is correct on SteamVR.
                for (uint32_t i = 0; i < locatedViewCount; ++i)
                {
                    projectionViews[i].pose = g_views[i].pose;
                    projectionViews[i].fov = {
                        -haloHalfX[i], haloHalfX[i], haloHalfY[i], -haloHalfY[i]};
                    projectionViews[i].subImage.swapchain = g_stereoChain;
                    projectionViews[i].subImage.imageRect = {
                        {0, 0}, {(int32_t)g_stereoW, (int32_t)g_stereoH}};
                    projectionViews[i].subImage.imageArrayIndex = i;
                }
                static bool loggedCoverFallback = false;
                if (!loggedCoverFallback)
                {
                    loggedCoverFallback = true;
                    LOG("M2 WARNING: the symmetric raster cover does not contain "
                        "the headset's native per-eye frustum, so the whole slice "
                        "is submitted at the cover FOV. Compositors that ignore a "
                        "custom layer FOV (ALVR) will show a doubled image.");
                }
            }
            if (renderFovsValid)
            {
                // Also cover the symmetric-FOV fallback: asynchronous CE
                // preparation always submits the pose that drew these pixels.
                if (ceImages)
                    for (int index=0;index<2;++index)
                    {
                        const auto& tracking=ceLease.pair.tracking; const auto& eye=tracking.eyes[index];
                        projectionViews[index].pose.position={tracking.headPosition.x+eye.offset.x,
                            tracking.headPosition.y+eye.offset.y,tracking.headPosition.z+eye.offset.z};
                        projectionViews[index].pose.orientation={eye.orientation.x,eye.orientation.y,eye.orientation.z,eye.orientation.w};
                    }
                projection.viewCount = locatedViewCount;
                projection.views = projectionViews.data();
            }
        }

        if (fs.shouldRender)
        {
            ID3D11Texture2D* backbuffer = nullptr;
            const HRESULT backbufferResult = sc->GetBuffer(
                0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
            (void)backbufferResult;
            if (backbuffer)
            {
                D3D11_TEXTURE2D_DESC bd{};
                backbuffer->GetDesc(&bd);
                HaloCE_RecordPresentationTexture(backbuffer,bd);
                if (g_recenterRequested.exchange(
                        false, std::memory_order_acq_rel))
                {
                    g_haveCenter = false;
                }
                if (!g_haveCenter)
                    TryRecenter(fs.predictedDisplayTime);
                // SteamVR accepted eye swapchains made before xrBeginSession
                // but presented them black. Create them lazily only once the
                // session is running, matching the known-good screen chain.
                if (g_stereoEnabled.load() && g_stereoChain == XR_NULL_HANDLE)
                {
                    if (!CreateStereoArrayChain())
                    {
                        LOG("M2: eye swapchain allocation failed; returning to mono screen");
                        g_stereoEnabled = false;
                        Game_SetStereoEye(-1);
                    }
                }
                TickRenderPlan();
                const bool pausedPresentation = g_pausePresentation.load();
                const bool stereo = !pausedPresentation && g_stereoEnabled.load() && viewsValid &&
                                    g_stereoChain != XR_NULL_HANDLE && Game_IsHeadTracking();
#if HALOMCCVR_HALO2_STEREO6DOF
                const bool currentHalo2Title =
                    TitleAdapter_GetActiveTitle() == GameTitle::Halo2;
                Halo2SynchronousPresentationStamp halo2Presentation{};
                (void)VR_Halo2GetSynchronousPresentationStamp(
                    halo2Presentation);
                const bool halo2DurableExactSerial =
                    halo2Presentation.preparedSerial ==
                        g_preparedFrame.serial &&
                    halo2Presentation.generation != 0 &&
                    (halo2Presentation.disposition ==
                         Halo2SynchronousFrameDisposition::Claimed ||
                     halo2Presentation.disposition ==
                         Halo2SynchronousFrameDisposition::Complete);
                // An exact durable claim owns this frame even if AutoVrTick
                // changed the active title immediately before Submit.
                const bool halo2Title =
                    currentHalo2Title || halo2DurableExactSerial;
                const uint32_t halo2Generation = halo2DurableExactSerial
                    ? halo2Presentation.generation
                    : currentHalo2Title
                        ? TitleAdapter_GetGeneration(GameTitle::Halo2) : 0;
                float halo2HalfX[2]{}, halo2HalfY[2]{};
                // Revalidate the raw pair independently of late presentation
                // eligibility. A completed synchronous pair has replaced the
                // stock output even if pause/head/session/view state changed
                // after rendering, so that case must classify Complete and drop
                // rather than masquerade as an unclaimed flat-screen frame.
                const bool halo2LiveExactPair = currentHalo2Title &&
                    VR_Halo2GetSynchronousHalfFovs(
                        halo2Generation, g_preparedFrame.serial,
                        halo2HalfX, halo2HalfY);
                const bool halo2ProjectionReady = projection.viewCount == 2;
                // E-H2-18: a pair the Anniversary core rendered from the
                // observer's published sample is submitted AS that sample's
                // view poses, so the compositor reprojects the image from
                // the pose it was really drawn at (the weapon and the world
                // share it); the located views of this serial are newer.
                if (halo2LiveExactPair)
                {
                    float pairPosition[2][3]{};
                    float pairOrientation[2][4]{};
                    if (VR_Halo2GetSynchronousPairPoses(
                            halo2Generation, g_preparedFrame.serial,
                            pairPosition, pairOrientation))
                    {
                        for (size_t view = 0;
                             view < projectionViews.size() && view < 2; ++view)
                        {
                            projectionViews[view].pose.position = {
                                pairPosition[view][0], pairPosition[view][1],
                                pairPosition[view][2]};
                            projectionViews[view].pose.orientation = {
                                pairOrientation[view][0], pairOrientation[view][1],
                                pairOrientation[view][2], pairOrientation[view][3]};
                        }
                        g_halo2XrPairsPosedFromObserver.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                }
                // The game runs below the panel rate, so some prepared
                // serials have no pair of their own. Re-presenting the last
                // complete pair with its own pose is what every title does
                // on a missed frame; showing the flat screen quad for that
                // one frame was a visible flash about once a second.
                float halo2RepeatHalfX[2]{}, halo2RepeatHalfY[2]{};
                const bool halo2RepeatablePair = currentHalo2Title &&
                    !halo2LiveExactPair &&
                    VR_Halo2GetRepeatableHalfFovs(
                        halo2Generation, g_preparedFrame.serial,
                        halo2RepeatHalfX, halo2RepeatHalfY);
                const bool halo2PauseStockScreen =
                    Halo2PausePresentationOwnsStockScreen(
                        halo2Title, pausedPresentation);
                const Halo2SynchronousPresentationDecision
                    halo2PresentationDecision = halo2PauseStockScreen
                        ? Halo2SynchronousPresentationDecision::SharedDefault
                        : Halo2SynchronousSelectPresentation(
                            stereo, halo2ProjectionReady, halo2Title,
                            halo2LiveExactPair,
                            halo2Generation, g_preparedFrame.serial,
                            halo2Presentation, halo2RepeatablePair);
                const bool halo2RepeatPair =
                    halo2PresentationDecision ==
                    Halo2SynchronousPresentationDecision::RepeatLastPair;
                const bool halo2Images =
                    !halo2Title || halo2LiveExactPair || halo2RepeatPair;
                if (halo2RepeatPair)
                {
                    // The images are the last pair's; so must the poses be.
                    // The cover/crop above already came from that pair via
                    // Game_GetRenderHalfFovs.
                    for (size_t view = 0;
                         view < projectionViews.size() && view < 2; ++view)
                    {
                        projectionViews[view].pose = g_halo2LastPair.pose[view];
                    }
                    g_halo2XrPairsRepeated.fetch_add(
                        1, std::memory_order_relaxed);
                }
                const Halo2SynchronousFrameDisposition
                    halo2FrameDisposition =
                        Halo2SynchronousClassifyFrame(
                            halo2LiveExactPair, halo2Generation,
                            g_preparedFrame.serial, halo2Presentation);
                const bool halo2StrictStockScreen =
                    Halo2SynchronousRequiresStrictStockScreen(
                        halo2Title, halo2FrameDisposition,
                        halo2PresentationDecision);
                const bool stereoWorldFrame =
                    halo2PresentationDecision ==
                        Halo2SynchronousPresentationDecision::SharedDefault
                    ? stereo
                    : (halo2PresentationDecision ==
                           Halo2SynchronousPresentationDecision::SynchronousStereo ||
                       halo2PresentationDecision ==
                           Halo2SynchronousPresentationDecision::RepeatLastPair);
                const bool allowStockScreenFrame =
                    halo2PresentationDecision !=
                        Halo2SynchronousPresentationDecision::Drop;
                if (Halo2SynchronousDropRequiresQuarantine(
                        halo2Generation, g_preparedFrame.serial,
                        halo2Presentation, halo2PresentationDecision))
                {
                    const Halo2StereoQuarantineReason reason =
                        !halo2LiveExactPair
                        ? Halo2StereoQuarantineReason::
                              VrClaimedPairUnavailable
                        : !halo2ProjectionReady
                            ? Halo2StereoQuarantineReason::
                                  VrClaimedProjectionFailed
                            : Halo2StereoQuarantineReason::
                                  VrClaimedPresentationUnavailable;
                    Halo2Stereo_RequestGenerationQuarantine(
                        halo2Generation, reason);
                }
                ReportHalo2SynchronousDisposition(
                    stereo && halo2Title, halo2PresentationDecision,
                    g_preparedFrame.serial);
#else
                const bool stereoWorldFrame = stereo;
                constexpr bool allowStockScreenFrame = true;
#endif
                float requestedTheaterAspect = 0.0f;
                const bool theaterQualified = stereoWorldFrame &&
                    Game_GetCutsceneTheaterPresentation(
                        requestedTheaterAspect);
                if (theaterQualified)
                    g_cutsceneTheaterAuthoredAspect = requestedTheaterAspect;
                if (theaterQualified && !g_cutsceneTheaterWasQualified)
                {
                    g_cutsceneTheaterEntryBlocked =
                        !TryRecenter(fs.predictedDisplayTime);
                    if (g_cutsceneTheaterEntryBlocked)
                    {
                        LOG("cutscene theatre: current head pose unavailable; "
                            "keeping this cinematic immersive");
                    }
                }
                else if (!theaterQualified)
                {
                    g_cutsceneTheaterEntryBlocked = false;
                }
                g_cutsceneTheaterWasQualified = theaterQualified;

                const CutsceneTheaterTransitionOutput theaterTransition =
                    g_cutsceneTheaterTransition.Update(
                        GetTickCount64(), theaterQualified &&
                            !g_cutsceneTheaterEntryBlocked);
                g_cutsceneTheaterActive.store(
                    theaterTransition.active, std::memory_order_release);
                comfortFadeAlpha = std::max(
                    comfortFadeAlpha, theaterTransition.fadeAlpha);
                if (theaterTransition.switched)
                {
                    Game_OnCutsceneTheaterPresentationChanged();
                    if (theaterTransition.active)
                    {
                        LOG("cutscene theatre: entered room-fixed stereo presentation");
                        // The authored aspect is the shape the title actually
                        // rasterized this cutscene into, so report it beside
                        // the cine bars it produced. A report can then say
                        // which of the two is wrong without another build.
                        const CutsceneTheaterMatte entryMatte =
                            ComputeCutsceneTheaterMatte(
                                g_cutsceneTheaterAuthoredAspect,
                                g_config.cutscene_theater_matte_aspect,
                                g_config.cutscene_theater_matte_offset);
                        if (entryMatte.active)
                        {
                            LOG("cutscene theatre: authored frame %.3f:1, cine "
                                "bars leave %.3f:1 showing (V %.3f..%.3f, "
                                "%.1f%% hidden top, %.1f%% bottom)",
                                g_cutsceneTheaterAuthoredAspect,
                                g_config.cutscene_theater_matte_aspect,
                                entryMatte.vMin, entryMatte.vMax,
                                entryMatte.vMin * 100.0f,
                                (1.0f - entryMatte.vMax) * 100.0f);
                        }
                        else
                        {
                            LOG("cutscene theatre: authored frame %.3f:1, no "
                                "cine bars (requested %.3f:1)",
                                g_cutsceneTheaterAuthoredAspect,
                                g_config.cutscene_theater_matte_aspect);
                        }
                    }
                    else
                    {
                        LOG("cutscene theatre: returned to immersive presentation");
                        ReleaseTheaterResolvedResources();
                        ReleaseTheaterSubtitleBand();
                        Game_Recenter();
                    }
                }
                const bool theaterPresentation =
                    theaterTransition.active && g_haveCenter;
                bool theaterProjectionAttempted = false;
                bool theaterProjectionReady = false;
                if (theaterPresentation && viewsValid &&
                    locatedViewCount == 2 && g_stereoChain != XR_NULL_HANDLE)
                {
                    // This descriptor is intentionally independent from the
                    // title-authored raster projection above. The rejected
                    // 387e5e3 path reused that narrow cutscene FOV here, then
                    // asked the runtime to reinterpret it as the headset view.
                    // Project and submit against the native runtime views
                    // instead, with one complete array slice per physical eye.
                    theaterProjection.space = g_localSpace;
                    theaterProjection.viewCount = 2;
                    theaterProjection.views = theaterProjectionViews;
                    for (uint32_t eye = 0; eye < 2; ++eye)
                    {
                        theaterProjectionViews[eye].pose = g_views[eye].pose;
                        theaterProjectionViews[eye].fov = g_views[eye].fov;
                        theaterProjectionViews[eye].subImage.swapchain =
                            g_stereoChain;
                        theaterProjectionViews[eye].subImage.imageRect = {
                            {0, 0}, {static_cast<int32_t>(g_stereoW),
                                     static_cast<int32_t>(g_stereoH)}};
                        theaterProjectionViews[eye].subImage.imageArrayIndex =
                            eye;
                    }
                }
                if (stereoWorldFrame&&(!ceTitle||ceImages))
                {
#if HALOMCCVR_HALO2_STEREO6DOF
                    // The exact-current H2 pair and projection descriptor were
                    // admitted together immediately above. This branch never
                    // sees a missing, late, partial, or N-1 H2 pair.
#elif HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
                    const bool halo2Title =
                        TitleAdapter_GetActiveTitle() == GameTitle::Halo2;
                    const uint32_t halo2Generation = halo2Title
                        ? TitleAdapter_GetGeneration(GameTitle::Halo2) : 0;
                    float halo2HalfX[2]{}, halo2HalfY[2]{};
                    const bool halo2Images = !halo2Title ||
                        VR_Halo2GetTemporalHalfFovs(
                            halo2Generation, g_preparedFrame.serial,
                            halo2HalfX, halo2HalfY);
#else
                    const bool halo2Title = false;
                    const bool halo2Images = true;
                    const uint32_t halo2Generation = 0;
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
                    ReachVrRenderAccess reachAccess{};
                    bool reachImages = false;
                    uint32_t reachGeneration = 0;
                    const bool reachTitle =
                        TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
                    if (reachTitle)
                    {
                        constexpr size_t reachSlot =
                            TitleRuntimeSlotIndex(GameTitle::HaloReach);
                        const TitleRuntimeAvailabilitySnapshot availability =
                            TitleAdapter_GetAvailability();
                        const ReachModuleEpoch epoch{
                            availability.moduleBases[reachSlot],
                            TitleAdapter_GetGeneration(GameTitle::HaloReach)};
                        reachGeneration = epoch.generation;
                        const ReachPreparedFrameToken prepared =
                            ReachPreparedFrameToken::Create(
                                epoch, g_preparedFrame.serial, true);
                        reachImages = VR_ReachBeginRenderAccess(
                                epoch, prepared, reachAccess) &&
                            g_reachEyeSerial[0].load(
                                std::memory_order_acquire) ==
                                g_preparedFrame.serial &&
                            g_reachEyeSerial[1].load(
                                std::memory_order_acquire) ==
                                g_preparedFrame.serial;
                        if (!reachImages && reachAccess.active)
                            VR_ReachEndRenderAccess(reachAccess);
                    }
#else
                    const bool reachTitle = false;
                    const bool reachImages = false;
                    const uint32_t reachGeneration = 0;
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
                    const bool halo4Title =
                        TitleAdapter_GetActiveTitle() == GameTitle::Halo4;
                    const bool halo4Images = !halo4Title ||
                        Halo4PreparedPairMatches(
                            g_preparedFrame.serial,
                            g_halo4EyeSerial[0].load(
                                std::memory_order_acquire),
                            g_halo4EyeSerial[1].load(
                                std::memory_order_acquire));
#else
                    const bool halo4Title = false;
                    const bool halo4Images = true;
#endif
                    // Halo 2 has its own bounded source-learning validator;
                    // running the generic full-resolution check as well is a
                    // redundant synchronous GPU stall.
                    if (!reachTitle && !halo2Title && !ceTitle)
                        ValidateStereoImagesOnce();
#if HALOMCCVR_HALO2_STEREO6DOF
                    if (halo2Title && halo2LiveExactPair)
                        ValidateHalo2EyePairPeriodic();
#endif
                    // Once per frame, but it only writes a line every two
                    // seconds; the same shape every other steady report here
                    // uses. All three titles reach this point.
                    ReportUploadResourceReuse();
                    uint32_t idx = 0;
                    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    XrSwapchainImageWaitInfo swi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    swi.timeout = 1000000000;
                    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    bool reachStereoUploadComplete = !reachTitle;
                    const D3D11_TEXTURE2D_DESC& capturedEyeDesc = ceImages
                        ? ceLease.pair.descriptor : reachTitle
                        ? g_reachCaptureDesc : g_eyeCacheDesc;
                    const bool theaterDirectSampling = theaterPresentation &&
                        TheaterProjectionCanSampleDirectly(capturedEyeDesc);
                    theaterProjectionAttempted =
                        theaterPresentation &&
                        theaterProjection.viewCount == 2 &&
                        EnsureTheaterProjectionResources(
                            g_stereoW, g_stereoH,
                            !theaterDirectSampling);
                    // The backbuffer in hand is the frame the title just
                    // finished, subtitles included. Copy the band before the
                    // theatre composite reads it; a failure only clears the
                    // band and leaves the stereo image alone.
                    g_theaterSubtitleBandStartV = -1.0f;
                    if (theaterProjectionAttempted &&
                        g_config.cutscene_theater_subtitles &&
                        !CaptureTheaterSubtitleBand(backbuffer, bd))
                    {
                        g_theaterSubtitleBandStartV = -1.0f;
                    }
                    // A synchronous H2 frame without two exact-current-serial
                    // images owns no world swapchain operation at all. It is a
                    // clean frame drop, not an opportunity to upload N-1.
                    const bool attemptStereoAcquire =
                        !halo2Title || halo2Images;
                    XrResult stereoAcquire = XR_ERROR_CALL_ORDER_INVALID;
                    if (attemptStereoAcquire)
                    {
                        stereoAcquire =
                            xrAcquireSwapchainImage(g_stereoChain, &ai, &idx);
                    }
                    const bool exactStereoTransaction =
                        reachTitle || halo4Title || halo2Title || ceTitle;
                    // An ordinary failed acquire owns no image and is therefore
                    // a frame-local miss. An exact-title positive non-success
                    // may nevertheless have acquired one (for example session
                    // loss pending), so enter the named runtime-recovery path
                    // rather than guessing at its call-order state. This keeps
                    // Reach's established acquire behavior byte-for-behavior.
                    const bool stereoAcquired = attemptStereoAcquire &&
                        (exactStereoTransaction
                            ? stereoAcquire == XR_SUCCESS
                            : XR_SUCCEEDED(stereoAcquire));
                    if (attemptStereoAcquire && exactStereoTransaction &&
                        stereoAcquire != XR_SUCCESS &&
                        XR_SUCCEEDED(stereoAcquire))
                    {
                        (void)RequireExactSwapchainCompletion(
                            stereoAcquire,
                            ceTitle ? "CE world swapchain acquire did not complete" : halo2Title
                                ? "Halo 2 synchronous world swapchain acquire did "
                                  "not complete"
                                : halo4Title
                                    ? "Halo 4 world swapchain acquire did not "
                                      "complete"
                                    : "Reach world swapchain acquire did not "
                                      "complete");
                    }
                    bool stereoWaitCompleted = false;
                    if (stereoAcquired)
                    {
                        const XrResult stereoWait =
                            xrWaitSwapchainImage(g_stereoChain, &swi);
                        stereoWaitCompleted = exactStereoTransaction
                            ? RequireExactSwapchainCompletion(
                                  stereoWait,
                                  ceTitle ? "CE world swapchain wait did not complete" : halo2Title
                                      ? "Halo 2 synchronous world swapchain wait "
                                        "did not complete"
                                      : halo4Title
                                          ? "Halo 4 world swapchain wait did "
                                            "not complete"
                                          : "Reach world swapchain wait did not "
                                            "complete")
                            : XR_SUCCEEDED(stereoWait);
                    }
                    bool everyEyeUploaded = false;
                    bool stereoReleased = false;
                    if (stereoAcquired && stereoWaitCompleted)
                    {
                        // Bracket only the two-eye publish. Everything the GPU
                        // does between these two calls is what this mod adds on
                        // top of the game's own rendering.
                        IqTimerBeginFrame();
                        bool everyReachEyeUploaded = reachImages;
                        everyEyeUploaded = true;
                        // A stereo batch needs both fresh sources before either
                        // target eye is presented. Eye-end normally completed
                        // this already; Reach publishes its retained sources
                        // here, so prepare both ahead of the presentation loop.
                        if (dlss::kEnableDlss21StereoBatch &&
                            VR_DlssWantsEyeCamera() &&
                            !g_config.dlss_debug_view &&
                            (!g_dlssKicked[0].Ready(g_preparedFrame.serial) ||
                             !g_dlssKicked[1].Ready(g_preparedFrame.serial)))
                        {
                            for (int batchEye = 0; batchEye < 2; ++batchEye)
                            {
                                const bool batchHasImage = reachImages ||
                                    (!reachTitle && (!halo4Title || halo4Images) &&
                                     (!halo2Title || halo2Images) &&
                                     g_eyeHasImage[batchEye]);
                                ID3D11Texture2D* batchSource = reachImages
                                    ? reachAccess.eyes[batchEye]
                                    : (reachTitle ? nullptr : g_eyeCache[batchEye]);
                                const D3D11_TEXTURE2D_DESC batchDesc = reachImages
                                    ? g_reachCaptureDesc : g_eyeCacheDesc;
                                if (batchHasImage && batchSource)
                                    DlssKickEye(batchEye, batchSource, batchDesc,
                                                g_stereoW, g_stereoH, nullptr);
                            }
                        }
                        Microsoft::WRL::ComPtr<ID3D11CommandList> accessoryCommands[2];
                        HRESULT accessoryPairResult=S_FALSE;
                        for (uint32_t targetEye = 0; targetEye < 2; ++targetEye)
                        {
                            const uint32_t sourceEye = theaterProjectionAttempted
                                ? CutsceneTheaterImageIndex(
                                      targetEye,
                                      g_config.cutscene_theater_flip_depth)
                                : targetEye;
                            const bool haveImage = ceImages || reachImages ||
                                (!reachTitle && (!halo4Title || halo4Images) &&
                                 (!halo2Title || halo2Images) &&
                                 g_eyeHasImage[sourceEye]);
                            ID3D11Texture2D* source = ceImages ? ceLease.pair.eyes[sourceEye] : reachImages
                                ? reachAccess.eyes[sourceEye]
                                : (reachTitle ? nullptr : g_eyeCache[sourceEye]);
                            D3D11_TEXTURE2D_DESC sourceDesc = ceImages ? ceLease.pair.descriptor : reachImages
                                ? g_reachCaptureDesc : g_eyeCacheDesc;
                            bool eyeUploaded = false;
                            bool sourceReconstructed = false;
                            if (haveImage && source)
                            {
                                // Select the reconstructed surface BEFORE selecting
                                // the presentation. Previously theatre paid for NGX
                                // at eye end, then displayed the low-resolution cache
                                // and discarded both completed outputs at EndFrame.
                                if (VR_DlssWantsEyeCamera() &&
                                    !g_config.dlss_debug_view)
                                {
                                    const int dlssEye = static_cast<int>(sourceEye);
                                    if (g_dlssKicked[dlssEye].Ready(g_preparedFrame.serial) ||
                                        DlssKickEye(dlssEye, source, sourceDesc,
                                            g_stereoW, g_stereoH, nullptr))
                                    {
                                        DlssKickedEye reconstructed{};
                                        if (DlssTakeEyeOutput(dlssEye, reconstructed) &&
                                            reconstructed.output)
                                        {
                                            source = reconstructed.output;
                                            sourceDesc = reconstructed.outputDesc;
                                            sourceReconstructed = true;
                                        }
                                    }
                                }
                                ID3D11RenderTargetView* targetRtv =
                                    GetStereoRtv(idx, targetEye);
                                if (theaterProjectionAttempted && targetRtv)
                                {
                                    if (theaterDirectSampling)
                                    {
                                        ID3D11ShaderResourceView* sourceSrv =
                                            AcquireSrcSrv(source, sourceDesc);
                                        eyeUploaded =
                                            CompositeTheaterProjectionEye(
                                                targetEye, sourceSrv,
                                                sourceDesc, false,
                                                g_views[targetEye], targetRtv);
                                    }
                                    else if (g_theaterResolved[targetEye] &&
                                            g_theaterResolvedRtv[targetEye] &&
                                            g_theaterResolvedSrv[targetEye])
                                    {
                                        const bool resolved = BlitImageQuality(
                                             source, sourceDesc,
                                             g_theaterResolved[targetEye],
                                             g_stereoW, g_stereoH,
                                             g_theaterResolvedRtv[targetEye],
                                             sourceReconstructed);
                                        eyeUploaded = resolved &&
                                            CompositeTheaterProjectionEye(
                                                targetEye,
                                                g_theaterResolvedSrv[targetEye],
                                                g_theaterResolvedDesc, true,
                                                g_views[targetEye], targetRtv);
                                    }
                                }
                                else if (!theaterProjectionAttempted && targetRtv)
                                {
                                    // DLSS first when selected; any reason it
                                    // cannot run this eye falls back to the
                                    // mod's own resolve (fail-open per eye).
                                    if (VR_DlssWantsEyeCamera() &&
                                        g_config.dlss_debug_view)
                                    {
                                        const int dlssEye = static_cast<int>(sourceEye);
                                        eyeUploaded =
                                            DlssFinishEye(dlssEye, g_stereoImages[idx],
                                                          g_stereoW, g_stereoH, targetRtv) ||
                                            (DlssKickEye(dlssEye, source, sourceDesc,
                                                         g_stereoW, g_stereoH, targetRtv) &&
                                             DlssFinishEye(dlssEye, g_stereoImages[idx],
                                                           g_stereoW, g_stereoH, targetRtv));
                                    }
                                    if (!eyeUploaded)
                                        eyeUploaded = BlitImageQuality(
                                            source, sourceDesc, g_stereoImages[idx],
                                            g_stereoW, g_stereoH, targetRtv,
                                            sourceReconstructed);
                                    // Optional accessory failure never changes eyeUploaded,
                                    // ownership, XR submission or native weapon rendering.
                                    const uint64_t now=GetTickCount64();
                                    const GameTitle title=TitleAdapter_GetActiveTitle();
                                    const uint32_t generation=TitleAdapter_GetGeneration(title);
                                    const uint64_t space=g_contactSpaceEpoch.load(std::memory_order_acquire);
                                    if(eyeUploaded && targetEye<projectionViews.size() &&
                                        weapon_accessory::Current(g_weaponAccessory,title,generation,space,
                                            VR_GetWeaponModelIdentity(title,generation,space,now),now,
                                            g_config.manual_reload,
                                            g_sessionState==XR_SESSION_STATE_FOCUSED &&
                                            TitleAdapter_GetRuntimeMode()==RuntimeMode::Gameplay &&
                                            !Menu_IsOpen()&&!VR_IsPausePresentation()&&
                                            !VR_IsPausePresentationTarget()&&!SecondaryWeaponPresentationActive()))
                                    {
                                        if(g_weaponAccessory.model==&weapon_model::kGenericReloadModel &&
                                            !g_config.weapon_unknown_reload_visual) g_weaponAccessory={};
                                        const auto& view=projectionViews[targetEye];
                                        const auto& p=view.pose.position;const auto& q=view.pose.orientation;
                                        weapon_accessory::Constants constants{};
                                        if(weapon_accessory::Projection(g_weaponAccessory,
                                            {{p.x,p.y,p.z},{q.x,q.y,q.z,q.w}},view.fov.angleLeft,
                                            view.fov.angleRight,view.fov.angleDown,view.fov.angleUp,constants))
                                        {
                                            const auto& rect=view.subImage.imageRect;
                                            const D3D11_VIEWPORT viewport{static_cast<float>(rect.offset.x),
                                                static_cast<float>(rect.offset.y),static_cast<float>(rect.extent.width),
                                                static_cast<float>(rect.extent.height),0,1};
                                            const HRESULT accessoryResult=g_weaponAccessoryRenderer.Draw(
                                                g_device,g_context,targetRtv,g_stereoW,g_stereoH,viewport,
                                                g_weaponAccessory,constants,&accessoryCommands[targetEye]);
                                            if(!FAILED(accessoryPairResult)) accessoryPairResult=accessoryResult;
                                        }
                                    }
                                }
                            }
                            everyEyeUploaded = everyEyeUploaded && eyeUploaded;
                            if (reachTitle)
                                everyReachEyeUploaded =
                                    everyReachEyeUploaded && eyeUploaded;
                        }
                        // Commit both optional eyes together, only after all
                        // world copies and accessory draws were prepared.
                        // A failed accessory leaves the complete world pair intact.
                        if(everyEyeUploaded&&accessoryCommands[0]&&accessoryCommands[1])
                        {
                            g_context->ExecuteCommandList(accessoryCommands[0].Get(),TRUE);
                            g_context->ExecuteCommandList(accessoryCommands[1].Get(),TRUE);
                        }
                        if(accessoryPairResult!=S_FALSE)
                            g_weaponAccessoryResult.store(accessoryPairResult,std::memory_order_release);
                        IqTimerEndFrame();
                        ReportIqGpuTiming();
                        theaterProjectionReady =
                            theaterProjectionAttempted && everyEyeUploaded;
                        const XrResult stereoRelease =
                            xrReleaseSwapchainImage(g_stereoChain, &ri);
                        stereoReleased = exactStereoTransaction
                            ? RequireExactSwapchainCompletion(
                                  stereoRelease,
                                  ceTitle ? "CE world swapchain release did not complete" : halo2Title
                                      ? "Halo 2 synchronous world swapchain release "
                                        "did not complete"
                                      : halo4Title
                                          ? "Halo 4 world swapchain release did "
                                            "not complete"
                                          : "Reach world swapchain release did "
                                            "not complete")
                            : XR_SUCCEEDED(stereoRelease);
                        if (theaterProjectionAttempted &&
                            !theaterProjectionReady)
                        {
                            static uint64_t lastProjectionSkipLogMs = 0;
                            const uint64_t nowMs = GetTickCount64();
                            if (nowMs - lastProjectionSkipLogMs >= 2000)
                            {
                                lastProjectionSkipLogMs = nowMs;
                                LOG("cutscene theatre frame skipped: the "
                                    "headset-agnostic projection did not "
                                    "complete; immersive VR remains armed");
                            }
                        }
                        if (reachTitle)
                            reachStereoUploadComplete =
                                everyReachEyeUploaded && stereoReleased &&
                                (!theaterProjectionAttempted ||
                                 theaterProjectionReady);
                    }
                    const bool halo4StereoUploadComplete = !halo4Title ||
                        Halo4XrPairUploadComplete(
                            stereoAcquired, stereoWaitCompleted,
                            everyEyeUploaded, stereoReleased);
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
                    const bool halo2StereoUploadComplete = !halo2Title ||
                        (stereoAcquired && stereoWaitCompleted &&
                         everyEyeUploaded && stereoReleased);
#else
                    const bool halo2StereoUploadComplete = true;
#endif

                    // A Reach frame that cannot expose a complete stereo pair
                    // is SKIPPED, not fatal. Revoke this frame's copied eye
                    // serials so nothing stale or partial is queued, then let
                    // the next frame try again.
                    //
                    // This used to call Game_RejectReachAuthoredReticle, which
                    // disarms the core and unhooks the mod. projection.viewCount
                    // is only set to 2 once Game_GetRenderHalfFovs matches the
                    // prepared frame serial, and Reach publishes those FOVs
                    // while its eyes render -- so an ordinary ordering gap on a
                    // single frame permanently destroyed Reach VR. That is the
                    // teardown behind every unexplained "stereo OFF" on
                    // 2026-07-26. Halo 3 and ODST skip such a frame and recover.
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
                    if (reachTitle && reachImages &&
                        (!reachStereoUploadComplete || projection.viewCount != 2))
                    {
                        g_reachEyeSerial[0].store(
                            0, std::memory_order_release);
                        g_reachEyeSerial[1].store(
                            0, std::memory_order_release);
                        g_authoredReticleReady = false;
                        g_authoredReticleSerial = 0;
                        // Loud but rate-limited: never a silent degrade.
                        static std::atomic<uint64_t> lastSkipLogMs{0};
                        const uint64_t nowMs = GetTickCount64();
                        uint64_t previousMs =
                            lastSkipLogMs.load(std::memory_order_relaxed);
                        if (nowMs - previousMs >= 2000 &&
                            lastSkipLogMs.compare_exchange_strong(
                                previousMs, nowMs, std::memory_order_relaxed))
                        {
                            LOG("Reach frame skipped (%s); the camera core "
                                "stays armed and the next frame retries",
                                !reachStereoUploadComplete
                                    ? "eye pair did not finish uploading to the "
                                      "XR swapchain"
                                    : "projection did not expose two views");
                        }
                    }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
                    if (halo4Title && halo4Images &&
                        (!halo4StereoUploadComplete ||
                         projection.viewCount != 2))
                    {
                        VR_InvalidateHalo4PreparedFrame();
                        if (!g_abortFrameForSwapchainFailure)
                        {
                            g_halo4XrPairsDropped.fetch_add(
                                1, std::memory_order_relaxed);
                            g_halo4LastXrPairDropReason.store(
                                static_cast<uint32_t>(
                                    !halo4StereoUploadComplete
                                        ? Halo4XrPairDropReason::EyeUploadIncomplete
                                        : Halo4XrPairDropReason::ProjectionIncomplete),
                                std::memory_order_relaxed);
                        }
                    }
#endif
#if HALOMCCVR_HALO2_STEREO6DOF
                    if (halo2Title && halo2Images &&
                        (!halo2StereoUploadComplete ||
                         projection.viewCount != 2))
                    {
                        Halo2Stereo_RequestGenerationQuarantine(
                            halo2Generation,
                            !halo2StereoUploadComplete
                                ? Halo2StereoQuarantineReason::
                                      VrClaimedSwapchainFailed
                                : Halo2StereoQuarantineReason::
                                      VrClaimedProjectionFailed);
                        VR_Halo2InvalidateSynchronousPair(
                            halo2Generation, g_preparedFrame.serial);
                        g_halo2XrPairsDropped.fetch_add(
                            1, std::memory_order_relaxed);
                        g_halo2LastDropReason.store(
                            !halo2StereoUploadComplete ? 2u : 3u,
                            std::memory_order_relaxed);
                    }
#elif HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
                    if (halo2Title && halo2Images &&
                        (!halo2StereoUploadComplete ||
                         projection.viewCount != 2))
                    {
                        VR_ResetHalo2TemporalStereo();
                        g_halo2XrPairsDropped.fetch_add(
                            1, std::memory_order_relaxed);
                        g_halo2LastDropReason.store(
                            !halo2StereoUploadComplete ? 2u : 3u,
                            std::memory_order_relaxed);
                    }
#endif
                    const bool projectionImagesReady = ceTitle
                        ? ceImages&&stereoAcquired&&stereoWaitCompleted&&everyEyeUploaded&&stereoReleased
                        : reachTitle
                        ? reachImages && reachStereoUploadComplete
                        : halo2Title
                            ? halo2Images && halo2StereoUploadComplete &&
                                g_eyeHasImage[0] && g_eyeHasImage[1]
                            : halo4Title
                                ? halo4Images && halo4StereoUploadComplete &&
                                    g_eyeHasImage[0] && g_eyeHasImage[1]
                                : g_eyeHasImage[0] && g_eyeHasImage[1];
                    if (projectionImagesReady &&
                        projection.viewCount == 2)
                    {
                        // Use the same predicted OpenXR controller pose captured
                        // for this displayed frame. Routing the quad through
                        // Halo's virtual-stick aim introduced visible catch-up
                        // lag and placed its ray origin at the player's head.
                        // Start from the same aim pose as bullet steering, then
                        // optionally stabilize ONLY the displayed reticle. At
                        // 0% it is the exact current bullet ray; higher values
                        // deliberately trade visual reticle response for calm.
                        float aimQ[4], aimP[3];
                        const bool haveAim = VR_GetAimPose(aimQ, aimP);
                        const bool authoredReticleThisFrame =
                            g_authoredReticleReady &&
                            g_authoredReticleSerial == g_preparedFrame.serial;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
                        // Reachability proof for the whole block below. If the
                        // quad heartbeat is silent while THIS is alive, the
                        // difference is the `reachTitle` test; if both are
                        // silent, the compositor is not reaching Reach's
                        // authored path at all and every conclusion drawn from
                        // that silence is void.
                        {
                            static uint64_t lastAliveMs = 0;
                            const uint64_t aliveNow = GetTickCount64();
                            if (aliveNow - lastAliveMs >= 2000)
                            {
                                lastAliveMs = aliveNow;
                                LOG("REACHHUD: compositor alive "
                                    "(reachTitle=%d reachImages=%d "
                                    "ownsReach=%d authoredThisFrame=%d "
                                    "heldArt=%d key=%016llX color=%X)",
                                    reachTitle ? 1 : 0,
                                    reachImages ? 1 : 0,
                                    Game_OwnsReachAuthoredReticle() ? 1 : 0,
                                    authoredReticleThisFrame ? 1 : 0,
                                    g_reticleContainsAuthored ? 1 : 0,
                                    static_cast<unsigned long long>(
                                        Game_GetAuthoredCrosshairKey()),
                                    Game_GetAuthoredCrosshairColorState());
                            }
                        }
#endif
                        const bool reachAuthoredReticleThisFrame =
                            reachTitle && reachImages &&
                            authoredReticleThisFrame &&
                            Game_OwnsReachAuthoredReticle();
                        // Reach reaches the shared aim-ray quad through the
                        // PROCEDURAL path only. The authored branch below --
                        // and the transaction rejection inside its failure
                        // handler -- stays Halo 3 / ODST only, so a crosshair
                        // problem can never disarm Reach's camera core.
                        // ODST does not consult the capability system for its
                        // reticle either -- it uses a direct ownership check
                        // (Game_IsCameraOnlyBringup), which is why it worked
                        // immediately. Reach gets the same treatment: ask the
                        // Reach core directly whether it owns this frame.
                        // Game_HasTitleCapability keeps returning false for
                        // Reach because the shared runtime snapshot spends much
                        // of its time "pending", which reports zero
                        // capabilities by design.
                        const bool reticleTitleAdmitted =
                            Game_HasTitleCapability(
                                TitleCapability_ControllerAim) ||
                            (ceImages && HaloCEFirstPerson_AimArmed() && HaloCEHud_CapturedCrosshair()) ||
                            Game_OwnsReachAuthoredReticle();
                        const bool titlePositionsNativeReticle =
                            Game_TitlePositionsNativeCrosshair();
                        const bool reticleUploadAdmitted =
                            reticleTitleAdmitted && !titlePositionsNativeReticle &&
                            g_config.crosshair && !g_config.hide_hud && haveAim &&
                            EnsureReticleChain();
                        // Reach uploads its captured widget art exactly like
                        // Halo 3 and ODST. The previous "!reachTitle" excluded
                        // Reach from ever publishing the art it captures, which
                        // is why its crosshair stayed procedural even once the
                        // capture worked. The exclusion dated from when the
                        // Reach CHUD hook could not run at all.
                        // Re-uploading the authored reticle costs an OpenXR
                        // swapchain acquire, a BLOCKING wait, a copy and a
                        // release - every frame, on a second swapchain, on top
                        // of Reach's own stereo swapchain. Reach's crosshair art
                        // is static between weapon and state changes, so that
                        // work is almost always spent re-publishing an image
                        // identical to the one already in the swapchain. Skip it
                        // while the captured art's identity is unchanged; the
                        // released image stays valid and the quad keeps showing
                        // it. The refresh policy below is title-specific.
                        static AuthoredReticleRefreshState s_refreshState{};
                        static uint64_t s_uploadsDone = 0;
                        static uint64_t s_uploadsSkipped = 0;
                        static uint64_t s_lastUploadLogMs = 0;
                        // Every authored title would pay a blocking swapchain
                        // upload here unless its refresh policy proves the held
                        // image is still current.
                        const bool titleCapturesArt =
                            Game_TitleCapturesAuthoredCrosshair();
                        const uint64_t authoredCrosshairKey =
                            titleCapturesArt ? Game_GetAuthoredCrosshairKey() : 0;
                        const uint32_t authoredColorState = titleCapturesArt
                            ? Game_GetAuthoredCrosshairColorState() : 0;
                        const uint32_t authoredCrosshairDraws = titleCapturesArt
                            ? Game_GetAuthoredCrosshairDrawCount() : 0;
                        const GameTitle reticleTitle =
                            TitleAdapter_GetActiveTitle();
                        const bool halo3AnimatesReticle =
                            reticleTitle == GameTitle::Halo3 &&
                            ResolveAuthoredAnimationGapFrames(
                                g_config.crosshair_animation_frames) != 0;
                        const AuthoredReticleRefreshPolicy refreshPolicy =
                            reticleTitle == GameTitle::HaloReach ||
                            reticleTitle == GameTitle::Halo4 ||
                            reticleTitle == GameTitle::Halo2 ||
                            reticleTitle == GameTitle::HaloCE ||
                            halo3AnimatesReticle
                                ? AuthoredReticleRefreshPolicy::BoundedAnimation
                                : reticleTitle == GameTitle::Halo3ODST
                                    ? AuthoredReticleRefreshPolicy::IdentityImmediate
                                    : AuthoredReticleRefreshPolicy::IdentityAndColorState;
                        // Halo 3 and Halo 4's authored crosshairs animate - they
                        // kick on fire and carry target/hit colour state - and
                        // none of that necessarily changes WHICH pieces drew.
                        // Sampling and publishing on the same bounded cadence
                        // keeps the animation live without paying the blocking
                        // swapchain upload every frame.
                        // With crosshair_animation_frames=0, Halo 4 keeps a
                        // slow 30-frame sample so a fixed-key weapon swap can
                        // replace held art; it does not animate every 6 frames.
                        // ODST's proven identity key also folds its native
                        // alternate-path state, so it publishes only when that
                        // key changes and has no steady blocking upload. Reach
                        // retains its working bounded animation cadence.
                        // Scope zoom is a separate pipeline and is not a trigger.
                        const uint64_t reticleOwnerEpoch =
                            g_reticleOwnerEpoch.load(std::memory_order_acquire);
                        const bool shouldUploadAuthoredReticle =
                            ShouldUploadAuthoredReticle(
                                refreshPolicy,
                                authoredReticleThisFrame,
                                reticleUploadAdmitted,
                                authoredCrosshairKey,
                                authoredColorState,
                                authoredCrosshairDraws,
                                g_preparedFrame.serial,
                                reticleOwnerEpoch,
                                s_refreshState);
                        const bool authoredArtAlreadyPublished =
                            authoredReticleThisFrame && reticleUploadAdmitted &&
                            !shouldUploadAuthoredReticle;
                        const bool reticleProbePending =
                            g_authoredReticleProbePending;
                        // A different crosshair, not the same one redrawn: the
                        // coverage bar must not judge a new weapon's reticle
                        // against the old one's ink. Reach is excluded because
                        // it folds its key per DRAWN widget, so its documented
                        // thinning case also changes the key and still needs
                        // the coverage guard behind it.
                        const bool authoredIdentityChanged =
                            authoredCrosshairKey != 0 &&
                            authoredCrosshairKey !=
                                s_refreshState.lastPublishedKey &&
                            reticleTitle != GameTitle::HaloReach;
                        bool authoredUploadFailed = false;
                        if ((shouldUploadAuthoredReticle || reticleProbePending) &&
                            UploadAuthoredReticle(false,
                                                  authoredIdentityChanged))
                        {
                            if (authoredCrosshairKey != 0)
                            {
                                MarkAuthoredReticleUploaded(
                                    s_refreshState, authoredCrosshairKey,
                                    authoredColorState, authoredCrosshairDraws,
                                    g_preparedFrame.serial);
                            }
                            ++s_uploadsDone;
                        }
                        else if ((shouldUploadAuthoredReticle || reticleProbePending) &&
                                 !g_authoredReticleHeldBlank)
                        {
                            // Never expose stale or undefined swapchain
                            // contents: repaint the chain, exactly as the
                            // accepted Halo 3 / ODST path does. A crosshair
                            // upload failure must not disarm Reach's camera
                            // core or drop its world projection - failure
                            // isolation, per AGENTS.md. It is reported loudly
                            // rather than passing silently.
                            g_authoredReticleReady = false;
                            EnsureReticleChain();
                            static std::atomic<bool> loggedUploadLoss{false};
                            if (!loggedUploadLoss.exchange(
                                    true, std::memory_order_relaxed))
                                LOG("authored reticle upload FAILED; the VR "
                                    "reticle is showing its procedural art "
                                    "this frame instead of the game's widget");
                        }
                        // Halo 3 reports the same counters as Reach: a headset
                        // report about the animated crosshair is only
                        // actionable next to the rate it actually published at.
                        // ODST is included too: its ink has never been measured
                        // in a headset session, and a stale-crosshair report is
                        // only actionable next to the coverage the guard saw.
                        if (reachTitle || reticleTitle == GameTitle::HaloCE || reticleTitle == GameTitle::Halo2 ||
                            reticleTitle == GameTitle::Halo3 ||
                            reticleTitle == GameTitle::Halo3ODST ||
                            reticleTitle == GameTitle::Halo4)
                        {
                            if (authoredArtAlreadyPublished)
                                ++s_uploadsSkipped;
                            const uint64_t nowMs = GetTickCount64();
                            if (nowMs - s_lastUploadLogMs >= 2000)
                            {
                                s_lastUploadLogMs = nowMs;
                                // `pieces` is what makes a "the crosshair
                                // vanished" report actionable: held < pieces
                                // means the capture thinned out and the
                                // completeness guard held the good art.
                                // `art` is the measured maximum alpha of the
                                // capture and `blankHeld` counts the publishes
                                // refused because it had none. Together they
                                // say, without another round trip, whether the
                                // engine stopped painting a visible crosshair
                                // and whether the guard caught it.
                                LOG("%s reticle upload: %llu uploaded, %llu "
                                    "skipped in the last window (key %llX, "
                                    "pieces %u, held %u, art %u, blankHeld %u)",
                                    reachTitle ? "Reach" :
                                        reticleTitle == GameTitle::HaloCE
                                            ? "CE" :
                                        reticleTitle == GameTitle::Halo2
                                            ? "Halo 2" :
                                        reticleTitle == GameTitle::Halo4
                                            ? "Halo 4" :
                                        reticleTitle == GameTitle::Halo3ODST
                                            ? "ODST" : "Halo 3",
                                    static_cast<unsigned long long>(
                                        s_uploadsDone),
                                    static_cast<unsigned long long>(
                                        s_uploadsSkipped),
                                    static_cast<unsigned long long>(
                                        authoredCrosshairKey),
                                    authoredCrosshairDraws,
                                    s_refreshState.lastPublishedDraws,
                                    g_authoredReticleLastCoverage,
                                    g_authoredReticleBlankHeld);
                                g_authoredReticleBlankHeld = 0;
                                s_uploadsDone = 0;
                                s_uploadsSkipped = 0;
                            }
                        }
                        const bool liveReachOwnerAfterUpload =
                            !reachTitle || Game_OwnsReachAuthoredReticle();
                        if (reachTitle && !liveReachOwnerAfterUpload)
                        {
                            // A title/lifecycle transition can complete while
                            // OpenXR waits for the authored upload. Revoke the
                            // pair again after that wait and age out any uploaded
                            // Reach art so the next title clears it before use.
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
                            g_reachEyeSerial[0].store(
                                0, std::memory_order_release);
                            g_reachEyeSerial[1].store(
                                0, std::memory_order_release);
#endif
                            if (reachAuthoredReticleThisFrame)
                            {
                                g_authoredReticleReady = false;
                                g_authoredReticleSerial = 0;
                            }
                        }
                        const bool reachProjectionAdmitted =
                            ReachCanSubmitCompleteProjection(
                                reachTitle, reachStereoUploadComplete,
                                authoredUploadFailed,
                                liveReachOwnerAfterUpload);
                        const bool reticleOwnerAdmitted =
                            reticleTitleAdmitted &&
                            // Reach shows its crosshair only alongside its own
                            // admitted world projection.
                            (!reachTitle || reachProjectionAdmitted);
                        if (reachProjectionAdmitted)
                        {
                            if (theaterPresentation)
                            {
                                if (theaterProjectionReady)
                                {
                                    layers.push_back(
                                        reinterpret_cast<XrCompositionLayerBaseHeader*>(
                                            &theaterProjection));
                                }
                                else if (!theaterProjectionAttempted)
                                {
                                    // Core-spec fallback if the private
                                    // projection resources could not be made.
                                    // It preserves the previously accepted
                                    // SteamVR path and names the degradation.
                                    for (uint32_t eye = 0; eye < 2; ++eye)
                                    {
                                        theaterQuads[eye] = MakeQuad(
                                            g_stereoChain,
                                            static_cast<int32_t>(g_stereoW),
                                            static_cast<int32_t>(g_stereoH),
                                            g_config.cutscene_theater_width_m,
                                            g_config.cutscene_theater_distance_m,
                                            0.0f, 0, false);
                                        theaterQuads[eye].eyeVisibility = eye == 0
                                            ? XR_EYE_VISIBILITY_LEFT
                                            : XR_EYE_VISIBILITY_RIGHT;
                                        theaterQuads[eye].subImage.imageArrayIndex =
                                            CutsceneTheaterImageIndex(
                                                eye,
                                                g_config.cutscene_theater_flip_depth);
                                        theaterQuads[eye].size.height =
                                            CutsceneTheaterHeightFromAspect(
                                                g_config.cutscene_theater_width_m,
                                                g_cutsceneTheaterAuthoredAspect);
                                        // This path has no shader of ours, so
                                        // the cine bars become the sub-rect the
                                        // quad samples. Same retained window,
                                        // same picture scale.
                                        const CutsceneTheaterMatte quadMatte =
                                            ComputeCutsceneTheaterMatte(
                                                g_cutsceneTheaterAuthoredAspect,
                                                g_config.cutscene_theater_matte_aspect,
                                                g_config.cutscene_theater_matte_offset);
                                        if (quadMatte.active)
                                        {
                                            const int32_t top =
                                                static_cast<int32_t>(
                                                    quadMatte.vMin *
                                                    static_cast<float>(g_stereoH));
                                            int32_t height =
                                                static_cast<int32_t>(
                                                    (quadMatte.vMax - quadMatte.vMin) *
                                                    static_cast<float>(g_stereoH));
                                            if (height > 0 &&
                                                top + height <=
                                                    static_cast<int32_t>(g_stereoH))
                                            {
                                                theaterQuads[eye].subImage
                                                    .imageRect.offset.y = top;
                                                theaterQuads[eye].subImage
                                                    .imageRect.extent.height = height;
                                                theaterQuads[eye].size.height =
                                                    CutsceneTheaterHeightFromAspect(
                                                        g_config.cutscene_theater_width_m,
                                                        g_config.cutscene_theater_matte_aspect);
                                            }
                                        }
                                        layers.push_back(
                                            reinterpret_cast<
                                                XrCompositionLayerBaseHeader*>(
                                                &theaterQuads[eye]));
                                    }
                                    static bool loggedFallback = false;
                                    if (!loggedFallback)
                                    {
                                        loggedFallback = true;
                                        LOG("cutscene theatre: native-FOV projection "
                                            "unavailable; using the core "
                                            "eye-selective quad fallback");
                                    }
                                }
                            }
                            else
                            {
                                layers.push_back(
                                    reinterpret_cast<XrCompositionLayerBaseHeader*>(
                                        &projection));
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
                                if (halo2Title)
                                    halo2ProjectionQueued = true;
#endif
#if HALOMCCVR_HALO2_STEREO6DOF
                                if (halo2Title && halo2LiveExactPair &&
                                    projectionViews.size() >= 2)
                                {
                                    g_halo2LastPair.valid = true;
                                    g_halo2LastPair.generation = halo2Generation;
                                    g_halo2LastPair.serial = g_preparedFrame.serial;
                                    g_halo2LastPair.tickMs = GetTickCount64();
                                    for (int view = 0; view < 2; ++view)
                                    {
                                        g_halo2LastPair.pose[view] =
                                            projectionViews[view].pose;
                                        g_halo2LastPair.halfX[view] = halo2HalfX[view];
                                        g_halo2LastPair.halfY[view] = halo2HalfY[view];
                                    }
                                }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
                                if (halo4Title)
                                    halo4ProjectionQueued = true;
#endif
                            }
                        }
                        const bool reticleChainAdmitted =
                            reticleUploadAdmitted &&
                            AuthoredReticleLayerHasContent(
                                titleCapturesArt &&
                                    !(reticleTitle == GameTitle::HaloCE &&
                                      halo_ce::ReticleNeedsProceduralBootstrap(
                                          titleCapturesArt, g_authoredReticleGoodValid,
                                          g_reticleContainsAuthored)) &&
                                    !(reticleTitle == GameTitle::Halo4 &&
                                      Halo4CuiReticleUsesProceduralFallback(
                                          titleCapturesArt,
                                          g_config.crosshair,
                                          g_config.kill_reticle)),
                                g_reticleContainsAuthored);
                        XrPosef secondaryAim{};
                        float secondaryQ[4]{},secondaryP[3]{};
                        const bool secondaryTracked=dual_reticle::Supported(reticleTitle) &&
                            VR_GetLeftControllerPose(secondaryQ,secondaryP) &&
                            Halo2BuildMirroredLeftAimOrientation(secondaryQ,g_config.gun_yaw_deg,
                                g_config.gun_pitch_deg,g_config.gun_roll_deg,secondaryQ);
                        secondaryAim={{secondaryQ[0],secondaryQ[1],secondaryQ[2],secondaryQ[3]},
                            {secondaryP[0],secondaryP[1],secondaryP[2]}};
                        const bool dualReticles=dual_reticle::Eligible(reticleTitle,
                            SecondaryWeaponPresentationActive(),haveAim,secondaryTracked,
                            g_config.crosshair,g_config.hide_hud,theaterPresentation) &&
                            PrepareDualReticleImage();
                        const uint64_t reticleSpace=g_contactSpaceEpoch.load(std::memory_order_acquire);
                        const uint32_t reticleGeneration=TitleAdapter_GetGeneration(reticleTitle);
                        if(!dualReticles || g_secondaryReticleSpace!=reticleSpace ||
                            g_secondaryReticleGeneration!=reticleGeneration || g_secondaryReticleTitle!=reticleTitle)
                            g_secondaryReticlePoseValid=false;
                        g_secondaryReticleSpace=reticleSpace;g_secondaryReticleGeneration=reticleGeneration;
                        g_secondaryReticleTitle=reticleTitle;
                        const bool reticleQuadSubmitted =
                            reticleOwnerAdmitted &&
                            !titlePositionsNativeReticle &&
                            g_config.crosshair && !g_config.hide_hud &&
                            // Halo 4 uses kill_reticle=0 as an explicit request
                            // for the stock face-centred CUI reticle. Never add
                            // a held authored gun-ray quad on top of it.
                            (reticleTitle != GameTitle::Halo4 ||
                             g_config.kill_reticle) &&
                            haveAim && (reticleChainAdmitted||dualReticles) && !theaterPresentation;
                        if (ceTitle)
                        {
                            static uint64_t lastCeReticleLogMs = 0;
                            const uint64_t nowMs = GetTickCount64();
                            if (nowMs - lastCeReticleLogMs >= 2000)
                            {
                                lastCeReticleLogMs = nowMs;
                                LOG("CE crosshair presentation: quad=%d aim=%d nativeOwned=%d "
                                    "measuredArt=%d heldArt=%d configured=%d lastAlpha=%u lastRGB=%u",
                                    reticleQuadSubmitted ? 1 : 0, haveAim ? 1 : 0,
                                    titleCapturesArt ? 1 : 0, g_authoredReticleGoodValid ? 1 : 0,
                                    g_reticleContainsAuthored ? 1 : 0, g_config.crosshair ? 1 : 0,
                                    g_authoredReticleLastAlphaInk,g_authoredReticleLastColorInk);
                            }
                        }
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
                        // The 2026-07-27 objective blackout is proven NOT to be
                        // the capture, the class resolution, or the redirect:
                        // during it Reach draws every other HUD widget at its
                        // normal rate and simply emits no crosshair, while the
                        // authored art stays held in the swapchain and the
                        // repaint guard leaves it alone. So the art exists and
                        // the quad still stops reaching the eye - which means
                        // one of these gates is refusing. Name it rather than
                        // guess a third time. Throttled state-change only, on
                        // the compositor thread that already logs status lines.
                        if (reachTitle)
                        {
                            // Heartbeat as well as state change. Twice this
                            // point produced ZERO lines, which was ambiguous
                            // between "the decision never changed" and "this
                            // code never runs" - and a conclusion was drawn
                            // from it anyway. A periodic line removes the
                            // ambiguity: silence now means only that this
                            // point is unreachable.
                            static bool loggedSubmitted = true;
                            static uint64_t lastHeartbeatMs = 0;
                            const uint64_t nowMs = GetTickCount64();
                            const bool heartbeat =
                                nowMs - lastHeartbeatMs >= 2000;
                            if (heartbeat)
                                lastHeartbeatMs = nowMs;
                            if (reticleQuadSubmitted != loggedSubmitted ||
                                heartbeat)
                            {
                                loggedSubmitted = reticleQuadSubmitted;
                                LOG("REACHHUD: crosshair quad %s "
                                    "(ownerAdmitted=%d projectionAdmitted=%d "
                                    "cfgCrosshair=%d haveAim=%d chain=%d "
                                    "authoredThisFrame=%d heldArt=%d)",
                                    reticleQuadSubmitted ? "SUBMITTED"
                                                         : "NOT submitted",
                                    reticleOwnerAdmitted ? 1 : 0,
                                    reachProjectionAdmitted ? 1 : 0,
                                    g_config.crosshair ? 1 : 0,
                                    haveAim ? 1 : 0,
                                    reticleChainAdmitted ? 1 : 0,
                                    authoredReticleThisFrame ? 1 : 0,
                                    g_reticleContainsAuthored ? 1 : 0);
                            }
                        }
#endif
                        if (reticleQuadSubmitted)
                        {
                            XrPosef rawAim{{aimQ[0],aimQ[1],aimQ[2],aimQ[3]},
                                           {aimP[0],aimP[1],aimP[2]}};
                            const float smoothing =
                                std::clamp(g_config.aim_stabilization, 0.0f, 0.95f);
                            g_reticleAimPose = g_reticleAimPoseValid && smoothing > 0.0f
                                ? SmoothTrackedPose(rawAim, g_reticleAimPose, smoothing)
                                : rawAim;
                            g_reticleAimPoseValid = true;
                            PublishPresentedReticleAimPose(&g_reticleAimPose);
                            const XrVector3f aimRay = Rotate(
                                g_reticleAimPose.orientation, {0.0f,0.0f,-1.0f});
                            float aimDir[3] = {aimRay.x,aimRay.y,aimRay.z};
                            weapon_muzzle::Ray committedPrimary{},committedSecondary{};
                            VrContactTrackingSnapshot reticleTracking{};
                            bool primaryBarrel=false,secondaryBarrel=false;
                            if(g_config.gun_barrel_aim && dual_reticle::Supported(reticleTitle) &&
                                VR_GetContactTrackingSnapshot(reticleTracking)) {
                                for(unsigned slot=0;slot<2;++slot) {
                                    dual_reticle::ProjectedRay projected{};
                                    if(g_weaponReticleRays[static_cast<unsigned>(reticleTitle)][slot].Read(projected) &&
                                        dual_reticle::Current(projected,reticleTitle,reticleGeneration,
                                            reticleTracking.referenceEpoch,reticleTracking.serial,
                                            reticleTracking.timeNs,GetTickCount64(),slot,g_config.left_handed)) {
                                        if(slot==0) {committedPrimary=projected.tracking;primaryBarrel=true;}
                                        else {committedSecondary=projected.tracking;secondaryBarrel=true;}
                                    }
                                }
                            }
                            if(primaryBarrel) std::copy(std::begin(committedPrimary.direction),
                                std::end(committedPrimary.direction),aimDir);
                            ReachStereoCenterPose reachCenter{};
                            bool reachNativeAim = false;
                            if (reachTitle && projection.viewCount == 2 &&
                                projectionViews.size() == 2)
                            {
                                const XrPosef& left =
                                    projectionViews[0].pose;
                                const XrPosef& right =
                                    projectionViews[1].pose;
                                const float leftQuaternion[4] = {
                                    left.orientation.x, left.orientation.y,
                                    left.orientation.z, left.orientation.w};
                                const float rightQuaternion[4] = {
                                    right.orientation.x, right.orientation.y,
                                    right.orientation.z, right.orientation.w};
                                const float leftPosition[3] = {
                                    left.position.x, left.position.y,
                                    left.position.z};
                                const float rightPosition[3] = {
                                    right.position.x, right.position.y,
                                    right.position.z};
                                float cameraLocalAim[3]{};
                                if (ReachBuildStereoCenterPose(
                                        leftQuaternion, leftPosition,
                                        rightQuaternion, rightPosition,
                                        reachCenter) &&
                                    Game_GetReachVehicleReticleAimDirection(
                                        g_preparedFrame.serial,
                                        cameraLocalAim))
                                {
                                    const XrVector3f rotated = Rotate(
                                        {reachCenter.orientation[0],
                                         reachCenter.orientation[1],
                                         reachCenter.orientation[2],
                                         reachCenter.orientation[3]},
                                        {cameraLocalAim[0],
                                         cameraLocalAim[1],
                                         cameraLocalAim[2]});
                                    const float lengthSquared =
                                        rotated.x * rotated.x +
                                        rotated.y * rotated.y +
                                        rotated.z * rotated.z;
                                    if (std::isfinite(lengthSquared) &&
                                        lengthSquared > 1.0e-8f)
                                    {
                                        const float inverseLength =
                                            1.0f / sqrtf(lengthSquared);
                                        aimDir[0] = rotated.x * inverseLength;
                                        aimDir[1] = rotated.y * inverseLength;
                                        aimDir[2] = rotated.z * inverseLength;
                                        reachNativeAim = true;
                                    }
                                }
                            }
                            // A vehicle seat bounds the weapon to a cone around
                            // the hull, so a hand outside it asks for an angle
                            // the engine will never reach. While that limit is
                            // holding, show where the gun ACTUALLY points: the
                            // reticle is meant to be the truth, and this is the
                            // one case where the hand ray is not.
                            float clampedAim[3];
                            if (!reachNativeAim &&
                                Game_GetClampedAimDirection(clampedAim))
                            {
                                aimDir[0] = clampedAim[0];
                                aimDir[1] = clampedAim[1];
                                aimDir[2] = clampedAim[2];
                            }
                            const float dist = g_config.crosshair_distance_m;
                            const float yaw = atan2f(aimDir[0], -aimDir[2]);
                            const float sp = fminf(fmaxf(aimDir[1], -1.0f), 1.0f);
                            const float pitch = asinf(sp);
                            // Orientation whose local -Z runs along the ray
                            // (quad faces the player): global yaw about +Y
                            // (angle -yaw, same convention as TryRecenter),
                            // then local pitch about +X.
                            const XrQuaternionf qy{0, sinf(-yaw * 0.5f), 0, cosf(-yaw * 0.5f)};
                            const XrQuaternionf qp{sinf(pitch * 0.5f), 0, 0, cosf(pitch * 0.5f)};
                            const XrQuaternionf q{
                                qy.w * qp.x + qy.x * qp.w + qy.y * qp.z - qy.z * qp.y,
                                qy.w * qp.y - qy.x * qp.z + qy.y * qp.w + qy.z * qp.x,
                                qy.w * qp.z + qy.x * qp.y - qy.y * qp.x + qy.z * qp.w,
                                qy.w * qp.w - qy.x * qp.x - qy.y * qp.y - qy.z * qp.z};
                            reticleQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                            reticleQuad.layerFlags =
                                XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                            reticleQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                            reticleQuad.space = g_localSpace;
                            reticleQuad.subImage.swapchain = dualReticles?g_dualReticleChain:g_reticleChain;
                            reticleQuad.subImage.imageRect =
                                {{0, 0}, {(int32_t)kReticleSize, (int32_t)kReticleSize}};
                            reticleQuad.subImage.imageArrayIndex = 0;
                            reticleQuad.pose.orientation = q;
                            XrVector3f reticleOrigin =
                                g_reticleAimPose.position;
                            if(primaryBarrel) reticleOrigin={committedPrimary.position[0],
                                committedPrimary.position[1],committedPrimary.position[2]};
                            if (reachNativeAim)
                            {
                                // The direction and origin share one validated
                                // predicted-display-time stereo-center pose.
                                // Vehicle barrels retain their native origin
                                // and therefore their honest close parallax.
                                reticleOrigin = {
                                    reachCenter.position[0],
                                    reachCenter.position[1],
                                    reachCenter.position[2]};
                            }
                            reticleQuad.pose.position = {
                                reticleOrigin.x + aimDir[0] * dist,
                                reticleOrigin.y + aimDir[1] * dist,
                                reticleOrigin.z + aimDir[2] * dist};
                            const float w = 2.0f * dist *
                                tanf(g_config.crosshair_size_deg * 0.5f * 0.0174533f);
                            reticleQuad.size = {w, w};
                            layers.push_back(
                                reinterpret_cast<XrCompositionLayerBaseHeader*>(&reticleQuad));
                            if(dualReticles) {
                                g_secondaryReticlePose=g_secondaryReticlePoseValid&&smoothing>0
                                    ?SmoothTrackedPose(secondaryAim,g_secondaryReticlePose,smoothing):secondaryAim;
                                g_secondaryReticlePoseValid=true;
                                XrVector3f direction=Rotate(g_secondaryReticlePose.orientation,{0,0,-1});
                                XrVector3f origin=g_secondaryReticlePose.position;
                                if(secondaryBarrel) {
                                    direction={committedSecondary.direction[0],committedSecondary.direction[1],committedSecondary.direction[2]};
                                    origin={committedSecondary.position[0],committedSecondary.position[1],committedSecondary.position[2]};
                                }
                                const float secondaryYaw=atan2f(direction.x,-direction.z);
                                const float secondaryPitch=asinf(std::clamp(direction.y,-1.f,1.f));
                                const XrQuaternionf sy{0,sinf(-secondaryYaw*.5f),0,cosf(-secondaryYaw*.5f)};
                                const XrQuaternionf sp{sinf(secondaryPitch*.5f),0,0,cosf(secondaryPitch*.5f)};
                                secondaryReticleQuad=reticleQuad;
                                secondaryReticleQuad.pose.orientation={sy.w*sp.x,sy.y*sp.w,
                                    -sy.y*sp.x,sy.w*sp.w};
                                secondaryReticleQuad.pose.position={origin.x+direction.x*dist,
                                    origin.y+direction.y*dist,origin.z+direction.z*dist};
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&secondaryReticleQuad));
                            }
                            static bool loggedDual=false;
                            if(loggedDual!=dualReticles) {
                                loggedDual=dualReticles;
                                LOG("Dual-weapon crosshairs: %s; single-glyph pair follows primary/secondary weapon roles; barrel receipts=%d/%d",
                                    dualReticles?"two independent symbols":"ordinary authored reticle",primaryBarrel,secondaryBarrel);
                            }
                        }
                        else
                        {
                            // Never blend from a stale pose after tracking or
                            // the crosshair is restored.
                            g_reticleAimPoseValid = false;
                            g_secondaryReticlePoseValid = false;
                            PublishPresentedReticleAimPose(nullptr);
                        }

                        if (reachProjectionAdmitted &&
                            !theaterPresentation &&
                            Game_HasScopeRenderer() &&
                            g_config.scope_enabled &&
                            g_scopeActive.load() &&
                            !Menu_IsOpen() && haveAim && PrepareScopeImageDelivery())
                        {
                            const ScopeQuadTransform transform = ComputeScopeQuadTransform(
                                aimQ, aimP,
                                g_config.left_handed?-g_config.scope_screen_right_m:g_config.scope_screen_right_m,
                                g_config.scope_screen_up_m,
                                g_config.scope_screen_forward_m,
                                g_config.scope_screen_width_m);
                            scopeQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                            scopeQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                            scopeQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                            scopeQuad.space = g_localSpace;
                            scopeQuad.subImage.swapchain = g_scopeScreenChain;
                            scopeQuad.subImage.imageRect = {
                                {0, 0}, {(int32_t)kScopeScreenWidth, (int32_t)kScopeScreenHeight}};
                            scopeQuad.subImage.imageArrayIndex = 0;
                            scopeQuad.pose.orientation = {aimQ[0], aimQ[1], aimQ[2], aimQ[3]};
                            scopeQuad.pose.position = {
                                transform.position[0], transform.position[1], transform.position[2]};
                            scopeQuad.size = {transform.width, transform.height};
                            layers.push_back(
                                reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuad));
                            static bool logged = false;
                            if (!logged)
                            {
                                logged = true;
                                LOG("scope 4:3 zoom screen submitted: %.3fm x %.3fm at local offsets %.3f/%.3f/%.3f",
                                    transform.width, transform.height,
                                    g_config.scope_screen_right_m,
                                    g_config.scope_screen_up_m,
                                    g_config.scope_screen_forward_m);
                            }
                        }
                    }
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
                    if (reachAccess.active)
                        VR_ReachEndRenderAccess(reachAccess);
#endif
                }
                else if (halo_ce::AllowStockScreen(allowStockScreenFrame,ceOwned,pausedPresentation))
                {
#if HALOMCCVR_HALO2_STEREO6DOF
                    if (halo2StrictStockScreen)
                    {
                        if (!EnsureScreenChain(bd.Width, bd.Height))
                        {
                            HandleHalo2StockScreenFailure(
                                "screen swapchain creation failed",
                                Halo2StockScreenXrOwnership::None);
                        }
                        else
                        {
                            uint32_t idx = 0;
                            XrSwapchainImageAcquireInfo ai{
                                XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                            XrSwapchainImageWaitInfo wi2{
                                XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                            wi2.timeout = 1000000000;
                            XrSwapchainImageReleaseInfo ri{
                                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                            FLog("acquire H2 stock-screen image");
                            const XrResult acquireResult =
                                xrAcquireSwapchainImage(
                                    g_screenChain, &ai, &idx);
                            if (RequireHalo2StockScreenCompletion(
                                    acquireResult, "acquire",
                                    "screen swapchain acquire did not complete",
                                    Halo2StockScreenXrOwnership::
                                        AcquiredUnresolved))
                            {
                                const XrResult waitResult =
                                    xrWaitSwapchainImage(g_screenChain, &wi2);
                                if (RequireHalo2StockScreenCompletion(
                                        waitResult, "wait",
                                        "screen swapchain wait did not complete",
                                        Halo2StockScreenXrOwnership::
                                            AcquiredUnresolved))
                                {
                                    const bool indexValid =
                                        g_device && g_context &&
                                        idx < g_screenImages.size();
                                    const bool directCopy =
                                        VrBlitCanUseDirectCopy(
                                            bd.Width, bd.Height, bd.Format,
                                            bd.SampleDesc.Count,
                                            g_screenW, g_screenH,
                                            static_cast<DXGI_FORMAT>(
                                                g_xrFormat));
                                    ID3D11RenderTargetView* targetRtv =
                                        indexValid && !directCopy &&
                                            idx < g_screenRtvs.size()
                                        ? GetRtv(
                                              g_screenImages,
                                              g_screenRtvs, idx)
                                        : nullptr;
                                    const bool resourcesValid =
                                        indexValid &&
                                        VrBlitResourcesReady(
                                            backbuffer != nullptr,
                                            g_screenImages[idx] != nullptr,
                                            targetRtv != nullptr,
                                            directCopy);
                                    bool blitComplete = false;
                                    if (resourcesValid)
                                    {
                                        FLog("blit backbuffer -> H2 stock screen");
                                        blitComplete = Blit(
                                            backbuffer, bd,
                                            g_screenImages[idx],
                                            g_screenW, g_screenH,
                                            targetRtv);
                                    }
                                    const XrResult releaseResult =
                                        xrReleaseSwapchainImage(
                                            g_screenChain, &ri);
                                    const bool releaseComplete =
                                        RequireHalo2StockScreenCompletion(
                                            releaseResult, "release",
                                            "screen swapchain release did not "
                                            "complete",
                                            Halo2StockScreenXrOwnership::
                                                AcquiredUnresolved);
                                    if (releaseComplete && !resourcesValid)
                                    {
                                        HandleHalo2StockScreenFailure(
                                            directCopy
                                                ? "direct-copy path has no valid "
                                                  "source or destination texture"
                                                : "shader path has no valid "
                                                  "source, destination texture, "
                                                  "or RTV",
                                            Halo2StockScreenXrOwnership::
                                                Released);
                                    }
                                    else if (releaseComplete && !blitComplete)
                                    {
                                        HandleHalo2StockScreenFailure(
                                            "stock backbuffer blit failed",
                                            Halo2StockScreenXrOwnership::
                                                Released);
                                    }
                                    else if (releaseComplete)
                                    {
                                        FLog("H2 stock-screen image released");
                                        const bool headLock = !g_haveCenter ||
                                            pausedPresentation ||
                                            (g_screenFollow.load() &&
                                             Game_IsHeadTracking());
                                        const XrSpace screenSpace = headLock
                                            ? g_viewSpace : g_localSpace;
                                        if (screenSpace == XR_NULL_HANDLE)
                                        {
                                            HandleHalo2StockScreenFailure(
                                                headLock
                                                    ? "VIEW space is unavailable"
                                                    : "LOCAL space is unavailable",
                                                Halo2StockScreenXrOwnership::
                                                    Released);
                                        }
                                        else
                                        {
                                            screenQuad = MakeQuad(
                                                g_screenChain,
                                                (int32_t)g_screenW,
                                                (int32_t)g_screenH,
                                                g_config.screen_width_m,
                                                g_config.screen_distance_m,
                                                0.0f, 0, headLock);
                                            layers.push_back(
                                                reinterpret_cast<
                                                    XrCompositionLayerBaseHeader*>(
                                                    &screenQuad));
                                            halo2StrictStockScreenQueued = true;
                                        }
                                    }
                                }
                                // A failed wait may still own the acquired
                                // image. Never release or acquire again here;
                                // the fatal session-recovery path owns cleanup.
                            }
                        }
                    }
                    else
#endif
                    if (g_haveCenter &&
                        EnsureScreenChain(bd.Width, bd.Height))
                    {
                        uint32_t idx = 0;
                        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                        XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                        wi2.timeout = 1000000000; // 1 second in ns; never hang the render thread
                        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        FLog("acquire+wait screen image");
                        if (XR_SUCCEEDED(xrAcquireSwapchainImage(g_screenChain, &ai, &idx)) &&
                            XR_SUCCEEDED(xrWaitSwapchainImage(g_screenChain, &wi2)))
                        {
                            FLog("blit backbuffer -> screen");
                            Blit(backbuffer, bd, g_screenImages[idx], g_screenW, g_screenH,
                                 GetRtv(g_screenImages, g_screenRtvs, idx));
                            xrReleaseSwapchainImage(g_screenChain, &ri);
                            FLog("screen image released");
                            const bool headLock = pausedPresentation ||
                                (g_screenFollow.load() && Game_IsHeadTracking());
                            screenQuad = MakeQuad(g_screenChain, (int32_t)g_screenW, (int32_t)g_screenH,
                                                  g_config.screen_width_m, g_config.screen_distance_m, 0.0f, 0,
                                                  headLock);
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&screenQuad));
                        }
                    }
                }

                // H3/H2's native final output already contains one full eye.
                // CE leaves its packed native atlas here instead. Present one
                // completed eye only after all headset/screen consumers above
                // have read their inputs, while the same pair borrow is held.
                // A desktop failure is independent of XR ownership/submission.
                if (ceImages&&ceOwned&&stereoWorldFrame)
                {
                    const bool mirrored=MirrorCeDesktop(ceLease.pair,backbuffer,bd);
                    static int lastCeMirrorState=-1;
                    static uint64_t lastCeMirrorLogMs=0;
                    const uint64_t nowMs=GetTickCount64();
                    if (lastCeMirrorState!=static_cast<int>(mirrored)||
                        (!mirrored&&nowMs-lastCeMirrorLogMs>=2000))
                    {
                        lastCeMirrorState=static_cast<int>(mirrored);lastCeMirrorLogMs=nowMs;
                        LOG("CE desktop mirror: %s; left eye %ux%u -> desktop %ux%u; XR pair unchanged",
                            mirrored?"one view":"copy failed; native output retained",
                            ceLease.pair.descriptor.Width,ceLease.pair.descriptor.Height,bd.Width,bd.Height);
                    }
                }

                if (g_abortFrameForSwapchainFailure)
                {
#if HALOMCCVR_HALO2_STEREO6DOF
                    // A later exact swapchain transaction can invalidate the
                    // already-queued H2 world layer. The shared fatal drain
                    // ends this frame; quarantine also closes H2 admission
                    // immediately so recovery cannot claim another frame in
                    // the same title generation.
                    if (halo2ProjectionQueued)
                    {
                        Halo2SynchronousPresentationStamp failedPresentation{};
                        if (VR_Halo2GetSynchronousPresentationStamp(
                                failedPresentation) &&
                            failedPresentation.preparedSerial ==
                                g_preparedFrame.serial)
                        {
                            Halo2Stereo_RequestGenerationQuarantine(
                                failedPresentation.generation,
                                Halo2StereoQuarantineReason::
                                    VrClaimedSwapchainFailed);
                        }
                    }
#endif
                    // Do not acquire menu/fade images or submit any partial
                    // exact-title layer set. xrBeginFrame still requires a matching
                    // xrEndFrame; an empty layer list references no potentially
                    // outstanding swapchain image.
                    Menu_ClearVrPointer();
                    backbuffer->Release();
                    EndPreparedFrameWithoutLayers(
                        "world swapchain transaction failed");
                    return;
                }

                // One-shot welcome page for this launch. The latch is a
                // process-lifetime static that is never cleared, so quitting a
                // level or returning to MCC's menu can never bring the page
                // back -- that is the entire "startup only" mechanism, and it
                // needs no level-load detection.
                //
                // The first focused frame is MCC's own shell menu on any real
                // launch, because the session is created during MCC's startup
                // Present. Deliberately NOT also gated on RuntimeMode or head
                // tracking: a title that failed to publish a lifecycle would
                // then silently never show the message, and a predictable rule
                // beats a "sometimes it doesn't appear" mode.
                static bool welcomeConsidered = false;
                if (!welcomeConsidered && g_sessionState == XR_SESSION_STATE_FOCUSED)
                {
                    welcomeConsidered = true;
                    if (g_config.show_welcome)
                        Menu_OpenWelcome();
                    else
                        LOG("menu: welcome page suppressed by show_welcome=0");
                }

                // The menu is submitted in BOTH modes (it used to live only in
                // the mono-screen branch, which made F1 invisible in stereo).
                // In stereo it head-locks so it is always in front of you.
                if (Menu_IsOpen())
                {
                    const bool menuHeadLocked =
                        pausedPresentation || stereo ||
                        (g_screenFollow.load() && Game_IsHeadTracking());
                    UpdateMenuPointer(menuHeadLocked);
                    if (ID3D11Texture2D* menuTex = Menu_Render())
                    {
                        uint32_t idx = 0;
                        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                        XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                        wi2.timeout = 1000000000;
                        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        D3D11_TEXTURE2D_DESC md{};
                        menuTex->GetDesc(&md);
                        if (XR_SUCCEEDED(xrAcquireSwapchainImage(g_menuChain, &ai, &idx)) &&
                            XR_SUCCEEDED(xrWaitSwapchainImage(g_menuChain, &wi2)))
                        {
                            Blit(menuTex, md, g_menuImages[idx], MENU_W, MENU_H,
                                 GetRtv(g_menuImages, g_menuRtvs, idx));
                            xrReleaseSwapchainImage(g_menuChain, &ri);
                            // Placement comes from the config, which the grab
                            // handle writes. UpdateMenuPointer above raycasts
                            // against the same four values, so the pointer can
                            // never drift off the visible panel.
                            menuQuad = MakeQuad(g_menuChain, MENU_W, MENU_H,
                                                g_config.menu_width_m,
                                                g_config.menu_distance_m,
                                                g_config.menu_height_m,
                                                XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                                    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
                                                menuHeadLocked,
                                                g_config.menu_side_m);
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&menuQuad));
                        }
                    }
                }
                else
                {
                    Menu_ClearVrPointer();
                }
                backbuffer->Release();
            }
#if HALOMCCVR_HALO2_STEREO6DOF
            else
            {
                // The exact H2 eye pair can be complete even if DXGI no longer
                // yields buffer 0 at submission. Its durable Claim/Complete
                // still owns this serial, so drop it once and close admission
                // before the next render instead of producing a black run.
                Halo2SynchronousPresentationStamp failedPresentation{};
                if (VR_Halo2GetSynchronousPresentationStamp(
                        failedPresentation) &&
                    failedPresentation.preparedSerial ==
                        g_preparedFrame.serial)
                {
                    Halo2Stereo_RequestGenerationQuarantine(
                        failedPresentation.generation,
                        Halo2StereoQuarantineReason::
                            VrClaimedPresentationUnavailable);
                }
                else if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
                {
                    LOG("Halo 2 pre-stereo screen GetBuffer(0) failed: "
                        "HRESULT 0x%08X, backbuffer=%p",
                        static_cast<unsigned>(backbufferResult),
                        static_cast<void*>(backbuffer));
                    HandleHalo2StockScreenFailure(
                        "DXGI GetBuffer(0) returned no stock backbuffer",
                        Halo2StockScreenXrOwnership::None);
                    EndPreparedFrameWithoutLayers(
                        "Halo 2 stock-screen backbuffer unavailable");
                    return;
                }
            }
#endif
        }
        if (nativeSubtitleReady && !layers.empty())
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&nativeSubtitleQuad));
        // Evaluate against this frame's actual native quad before composition.
        // The cursor follows the last successfully delivered smoothed mouse UV.
        if (comfortFadeAlpha <= 0.01f &&
            std::find(layers.begin(),layers.end(),
                reinterpret_cast<XrCompositionLayerBaseHeader*>(&screenQuad)) != layers.end()) {
            UpdateNativeMenuPointer(screenQuad);
            float u=0,v=0;bool pressed=false;
            if(g_nativePointerFrame.active && g_nativeCursorReady &&
                NativeMenuPointer_ReadVisual(u,v,pressed)) {
                nativeCursorQuad=game_menu_pointer::CursorQuad(screenQuad,g_nativeCursorChain,u,v,pressed);
                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&nativeCursorQuad));
            }
        }
        if (fs.shouldRender)
            AppendComfortFade(comfortFadeAlpha, fadeQuad, layers);

        // Heartbeat: log the session state + whether the runtime wants us to
        // render + how many layers we submitted, on any change and at least
        // every couple of seconds. This shows whether we ever go VISIBLE.
        {
            static XrSessionState lastState = XR_SESSION_STATE_UNKNOWN;
            static int lastShould = -1;
            static LARGE_INTEGER last{}, freq{};
            LARGE_INTEGER now;
            if (freq.QuadPart == 0)
                QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&now);
            // Publish for the wait worker, which reports stalls while this
            // thread is stuck inside the game and cannot speak for itself.
            g_lastShouldRenderShared.store((int)fs.shouldRender,
                                           std::memory_order_relaxed);
            const bool changed = g_sessionState != lastState || (int)fs.shouldRender != lastShould;
            const bool tick = last.QuadPart == 0 || (now.QuadPart - last.QuadPart) >= 2 * freq.QuadPart;
            if (changed || tick)
            {
                LOG("status: session=%s shouldRender=%d layers=%u", SessionStateName(g_sessionState),
                    (int)fs.shouldRender, (unsigned)layers.size());
                lastState = g_sessionState;
                lastShould = (int)fs.shouldRender;
                last = now;
            }
        }

        FLog("xrEndFrame before DXGI Present");
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
        ei.displayTime = fs.predictedDisplayTime;
        ei.environmentBlendMode = g_blendMode;
        ei.layerCount = (uint32_t)layers.size();
        ei.layers = layers.data();
        // Preserve the legacy rolling submit timer. Transition diagnosis uses
        // the correlated raw-QPC record above; this aggregate cannot identify
        // which logical frame changed cadence.
        LARGE_INTEGER endStart{}, endEnd{};
        QueryPerformanceCounter(&endStart);
        if (g_beginFrameQpc.QuadPart)
            g_renderWindowMs.Add(QpcMs(endStart.QuadPart - g_beginFrameQpc.QuadPart));
        XrResult r = xrEndFrame(g_session, &ei);
        if (r != XR_SUCCESS) {
            if(std::find(layers.begin(),layers.end(),
                reinterpret_cast<XrCompositionLayerBaseHeader*>(&nativeCursorQuad)) != layers.end()) {
                // Drop this frame, then retire only the optional cursor layer.
                // Never let a rejected extra layer repeatedly poison core VR.
                g_nativeCursorReady=false;
                NativeMenuPointer_CursorFailed();
            }
            g_nativePointerFrame={};
            NativeMenuPointer_FrameStatus("submission-failed");
        }
        QueryPerformanceCounter(&endEnd);
        g_endFrameDurationsMs.Add(QpcMs(endEnd.QuadPart - endStart.QuadPart));
#if HALOMCCVR_HALO2_STEREO6DOF
        if (halo2StrictStockScreenQueued && r != XR_SUCCESS)
        {
            LOG("Halo 2 stock-screen transaction xrEndFrame did not complete: %s",
                XrStr(r));
            HandleHalo2StockScreenFailure(
                "stock-screen xrEndFrame did not complete",
                Halo2StockScreenXrOwnership::
                    FrameSubmissionUnresolved);
        }
        if (halo2ProjectionQueued && r != XR_SUCCESS)
        {
            Halo2SynchronousPresentationStamp failedPresentation{};
            if (VR_Halo2GetSynchronousPresentationStamp(
                    failedPresentation) &&
                failedPresentation.preparedSerial ==
                    g_preparedFrame.serial)
            {
                Halo2Stereo_RequestGenerationQuarantine(
                    failedPresentation.generation,
                    Halo2StereoQuarantineReason::
                        VrClaimedSubmissionFailed);
            }
        }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
        if (halo2ProjectionQueued && r == XR_SUCCESS)
        {
#if HALOMCCVR_HALO2_STEREO6DOF
            // C-H2-6 runtime ownership is earned by a headset-admitted pair,
            // not by locating/installing/arming the title hook. Revalidate the
            // exact current serial after xrEndFrame so a concurrent reset or
            // title-generation transition cannot resurrect Gameplay.
            const uint32_t acceptedGeneration =
                TitleAdapter_GetGeneration(GameTitle::Halo2);
            float acceptedHalfX[2]{}, acceptedHalfY[2]{};
            const bool exactPairStillCurrent =
                TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
                VR_Halo2GetSynchronousHalfFovs(
                    acceptedGeneration, g_preparedFrame.serial,
                    acceptedHalfX, acceptedHalfY);
            if (exactPairStillCurrent)
            {
                const uint64_t nowMs = GetTickCount64();
                TitleAdapter_PublishHeartbeat(
                    GameTitle::Halo2, acceptedGeneration, nowMs);
                TitleAdapter_PublishMode(
                    GameTitle::Halo2, acceptedGeneration,
                    RuntimeMode::Gameplay);
                g_halo2XrPairsSubmitted.fetch_add(
                    1, std::memory_order_relaxed);
                if (!g_halo2FirstPairAccepted.exchange(
                        true, std::memory_order_acq_rel))
                {
                    LOG("Halo 2 C-H2-6 simultaneous stereo + 6DOF active");
                    LOG("Halo 2 C-H2-6 first exact-current-serial two-eye pair "
                        "accepted by xrEndFrame; no temporal eye reuse or "
                        "cadence division");
                }
            }
#else
            g_halo2XrPairsSubmitted.fetch_add(
                1, std::memory_order_relaxed);
            if (!g_halo2FirstPairAccepted.exchange(
                    true, std::memory_order_acq_rel))
            {
                LOG("Halo 2 C-H2-2 stereo active: first adjacent temporal eye "
                    "pair accepted by xrEndFrame; stock head pose remains in "
                    "use (6DOF is not enabled in this candidate)");
            }
#endif
        }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
        if (Halo4XrPairSubmissionAccepted(
                halo4ProjectionQueued, r == XR_SUCCESS))
        {
            g_halo4XrPairsSubmitted.fetch_add(
                1, std::memory_order_relaxed);
        }
#endif
        if constexpr (kEnableFramePacingTransitionCapture)
        {
            if (g_framePacingPending.serial == g_preparedFrame.serial)
            {
                g_framePacingPending.endStartQpc = endStart.QuadPart;
                g_framePacingPending.endEndQpc = endEnd.QuadPart;
                g_framePacingPending.endResult = r;
                g_framePacingPending.layerCount =
                    static_cast<uint32_t>(layers.size());
                g_framePacingPending.submitted = true;
            }
        }
        ResetPreparedFrame();
        if (XR_FAILED(r))
        {
            ++g_frameOrderFailures;
            static bool logged = false;
            if (!logged)
            {
                LOG("xrEndFrame failed: %s", XrStr(r));
                logged = true;
            }
        }
        FLog("frame complete");
        if (g_frameNo == 3)
            LOG("first 3 frames submitted OK; going quiet now");
    }
} // namespace

void VR_InitInstance()
{
    if (!g_headCsInit)
    {
        InitializeCriticalSection(&g_headCs);
        g_headCsInit = true;
    }
    // Runs on the DLL's background init thread, in parallel with the game
    // loading. Never touches the render thread or the game's D3D device.
    if (InitInstance())
        g_instanceReady = true;
    else
        g_instanceFailed = true; // Fail() already showed a message
}

#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
static bool ReadHalo2RenderSnapshot(Halo2VrRenderSnapshot& snapshot)
{
    snapshot = {};
    const uint64_t serial =
        g_preparedSerialPublished.load(std::memory_order_acquire);
    if (!serial || !g_stereoEnabled.load(std::memory_order_acquire) ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }

    const uint32_t index =
        g_halo2RenderSnapshotIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;
    uint32_t state =
        g_halo2RenderSnapshotStates[index].load(std::memory_order_seq_cst);
    if ((state & kHalo2RenderSnapshotWriter) != 0 ||
        state >= kHalo2RenderSnapshotWriter - 1 ||
        !g_halo2RenderSnapshotStates[index].compare_exchange_strong(
            state, state + 1, std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        return false;
    }
    if (g_halo2RenderSnapshotIndex.load(std::memory_order_seq_cst) != index)
    {
        g_halo2RenderSnapshotStates[index].fetch_sub(
            1, std::memory_order_seq_cst);
        return false;
    }
    const Halo2VrRenderSnapshot local = g_halo2RenderSnapshots[index];
    g_halo2RenderSnapshotStates[index].fetch_sub(
        1, std::memory_order_seq_cst);

    const uint32_t generation =
        TitleAdapter_GetGeneration(GameTitle::Halo2);
    if (local.preparedSerial != serial || !generation ||
        local.generation != generation ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo2 ||
        g_preparedSerialPublished.load(std::memory_order_acquire) != serial ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }
    snapshot = local;
    return true;
}
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
bool VR_Halo2GetRenderSnapshot(Halo2VrRenderSnapshot& snapshot)
{
    return ReadHalo2RenderSnapshot(snapshot);
}

bool VR_Halo2CompleteTemporalEye(
    uint32_t generation, uint64_t preparedSerial, int eye,
    float halfFovX, float halfFovY)
{
    if (!generation || !preparedSerial ||
        Halo2PendingSerialIsReserved(preparedSerial) ||
        eye < 0 || eye > 1 ||
        eye != Halo2TemporalEyeForSerial(
            preparedSerial, g_config.right_eye_first) ||
        !std::isfinite(halfFovX) || !std::isfinite(halfFovY) ||
        halfFovX <= 0.0f || halfFovX >= 1.5707f ||
        halfFovY <= 0.0f || halfFovY >= 1.5707f)
    {
        return false;
    }
    uint64_t expected = 0;
    const uint64_t reserved = Halo2ReservedPendingSerial(preparedSerial);
    if (!g_halo2PendingSerial.compare_exchange_strong(
            expected, reserved, std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        // Two player windows (or any duplicate primary transaction) in one
        // prepared frame are not a stereo image. Revoke the first token.
        (void)g_halo2PendingSerial.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        for (int cachedEye = 0; cachedEye < 2; ++cachedEye)
        {
            if (g_halo2EyeSerial[cachedEye].load(
                    std::memory_order_acquire) == preparedSerial)
            {
                g_halo2EyeSerial[cachedEye].store(
                    0, std::memory_order_release);
            }
        }
        return false;
    }
    g_halo2PendingGeneration.store(generation, std::memory_order_relaxed);
    g_halo2PendingEye.store(eye, std::memory_order_relaxed);
    g_halo2PendingHalfFovX.store(halfFovX, std::memory_order_relaxed);
    g_halo2PendingHalfFovY.store(halfFovY, std::memory_order_relaxed);
    expected = reserved;
    return g_halo2PendingSerial.compare_exchange_strong(
        expected, preparedSerial, std::memory_order_release,
        std::memory_order_acquire);
}

void VR_InvalidateHalo2TemporalFrame(
    uint32_t generation, uint64_t preparedSerial)
{
    uint64_t pending =
        g_halo2PendingSerial.load(std::memory_order_acquire);
    const uint32_t pendingGeneration =
        g_halo2PendingGeneration.load(std::memory_order_relaxed);
    const uint64_t reserved = Halo2ReservedPendingSerial(preparedSerial);
    if ((pendingGeneration == generation && pending == preparedSerial) ||
        pending == reserved)
    {
        (void)g_halo2PendingSerial.compare_exchange_strong(
            pending, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2EyeGeneration[eye].load(std::memory_order_acquire) ==
                generation &&
            g_halo2EyeSerial[eye].load(std::memory_order_acquire) ==
                preparedSerial)
        {
            g_halo2EyeSerial[eye].store(0, std::memory_order_release);
        }
    }
}

void VR_ResetHalo2TemporalStereo()
{
    g_halo2PendingSerial.store(0, std::memory_order_release);
    g_halo2PendingGeneration.store(0, std::memory_order_release);
    g_halo2PendingEye.store(-1, std::memory_order_release);
    for (int eye = 0; eye < 2; ++eye)
    {
        g_halo2EyeSerial[eye].store(0, std::memory_order_release);
        g_halo2EyeGeneration[eye].store(0, std::memory_order_release);
    }
}

bool VR_Halo2GetTemporalHalfFovs(
    uint32_t generation, uint64_t preparedSerial,
    float halfX[2], float halfY[2])
{
    if (!generation || preparedSerial < 2 || !halfX || !halfY)
        return false;
    const int currentEye = Halo2TemporalEyeForSerial(
        preparedSerial, g_config.right_eye_first);
    if (currentEye < 0)
        return false;
    const int previousEye = 1 - currentEye;
    if (g_halo2EyeGeneration[currentEye].load(std::memory_order_acquire) !=
            generation ||
        g_halo2EyeGeneration[previousEye].load(std::memory_order_acquire) !=
            generation ||
        g_halo2EyeSerial[currentEye].load(std::memory_order_acquire) !=
            preparedSerial ||
        g_halo2EyeSerial[previousEye].load(std::memory_order_acquire) !=
            preparedSerial - 1 ||
        !g_eyeHasImage[currentEye] || !g_eyeHasImage[previousEye])
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        halfX[eye] =
            g_halo2EyeHalfFovX[eye].load(std::memory_order_relaxed);
        halfY[eye] =
            g_halo2EyeHalfFovY[eye].load(std::memory_order_relaxed);
        if (!std::isfinite(halfX[eye]) || !std::isfinite(halfY[eye]) ||
            halfX[eye] <= 0.0f || halfX[eye] >= 1.5707f ||
            halfY[eye] <= 0.0f || halfY[eye] >= 1.5707f)
        {
            return false;
        }
    }
    return true;
}
#endif

#if HALOMCCVR_HALO2_STEREO6DOF
namespace
{
    bool Halo2SynchronousContextCurrent(
        uint32_t generation, uint64_t preparedSerial,
        uint32_t resourceEpoch) noexcept
    {
        return generation != 0 && preparedSerial != 0 &&
            !Halo2SynchronousSerialIsReserved(preparedSerial) &&
            resourceEpoch != 0 &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
            TitleAdapter_GetGeneration(GameTitle::Halo2) == generation &&
            g_stereoEnabled.load(std::memory_order_acquire) &&
            g_preparedSerialPublished.load(std::memory_order_acquire) ==
                preparedSerial &&
            g_preparedViewSerialPublished.load(std::memory_order_acquire) ==
                preparedSerial &&
            g_preparedShouldRender.load(std::memory_order_acquire) &&
            g_halo2SynchronousResourceEpoch.load(std::memory_order_acquire) ==
                resourceEpoch &&
            g_eyeCache[0] && g_eyeCache[1] &&
            g_eyeCacheRtvs[0] && g_eyeCacheRtvs[1];
    }

    bool Halo2SynchronousHalfFovValid(float halfFov) noexcept
    {
        return std::isfinite(halfFov) && halfFov > 0.0f &&
            halfFov < 1.5707f;
    }

    bool PublishHalo2SynchronousPresentation(
        uint32_t generation, uint64_t preparedSerial,
        Halo2SynchronousFrameDisposition disposition) noexcept
    {
        if (!generation || !preparedSerial ||
            Halo2SynchronousSerialIsReserved(preparedSerial) ||
            (disposition != Halo2SynchronousFrameDisposition::Claimed &&
             disposition != Halo2SynchronousFrameDisposition::Complete))
        {
            return false;
        }

        // Writers are only expected on H2's render thread. Keep the bounded CAS
        // nevertheless: failure to acquire the slot makes the first-eye path
        // take its zero-eye pre-stereo screen path, while a failed Complete
        // publication
        // leaves the already-published conservative Claim in force.
        uint64_t sequence =
            g_halo2SynchronousPresentationSequence.load(
                std::memory_order_acquire);
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            if ((sequence & 1u) != 0)
            {
                sequence = g_halo2SynchronousPresentationSequence.load(
                    std::memory_order_acquire);
                continue;
            }
            if (g_halo2SynchronousPresentationSequence.compare_exchange_weak(
                    sequence, sequence + 1, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                const uint64_t currentSerial =
                    g_halo2SynchronousPresentationSerial.load(
                        std::memory_order_relaxed);
                const uint32_t currentGeneration =
                    g_halo2SynchronousPresentationGeneration.load(
                        std::memory_order_relaxed);
                const auto currentDisposition =
                    static_cast<Halo2SynchronousFrameDisposition>(
                        g_halo2SynchronousPresentationDisposition.load(
                            std::memory_order_relaxed));

                bool accepted = false;
                bool replace = false;
                if (!currentSerial || preparedSerial > currentSerial)
                {
                    accepted = true;
                    replace = true;
                }
                else if (preparedSerial == currentSerial &&
                         generation == currentGeneration)
                {
                    // Complete is terminal and never regresses to Claimed.
                    accepted = true;
                    replace = currentDisposition !=
                                  Halo2SynchronousFrameDisposition::Complete &&
                        disposition ==
                            Halo2SynchronousFrameDisposition::Complete;
                }

                if (replace)
                {
                    g_halo2SynchronousPresentationGeneration.store(
                        generation, std::memory_order_relaxed);
                    g_halo2SynchronousPresentationDisposition.store(
                        static_cast<uint32_t>(disposition),
                        std::memory_order_relaxed);
                    g_halo2SynchronousPresentationSerial.store(
                        preparedSerial, std::memory_order_relaxed);
                }
                g_halo2SynchronousPresentationSequence.store(
                    sequence + 2, std::memory_order_release);
                return accepted;
            }
        }
        return false;
    }
}

bool VR_Halo2GetSynchronousRenderSnapshot(
    Halo2SynchronousVrRenderSnapshot& snapshot)
{
    return ReadHalo2RenderSnapshot(snapshot);
}

bool VR_Halo2GetCurrentPreparedCadence(
    Halo2PreparedCadenceSnapshot& cadence) noexcept
{
    cadence = {};
    const uint64_t serialBefore =
        g_halo2PreparedCadenceSerial.load(std::memory_order_acquire);
    if (!serialBefore)
        return false;
    const uint64_t targetPeriod =
        g_halo2PreparedTargetPeriodNs.load(std::memory_order_relaxed);
    const uint64_t displayDelta =
        g_halo2PreparedDisplayDeltaNs.load(std::memory_order_relaxed);
    const uint64_t serialAfter =
        g_halo2PreparedCadenceSerial.load(std::memory_order_acquire);
    if (serialBefore != serialAfter ||
        g_preparedSerialPublished.load(std::memory_order_acquire) !=
            serialBefore ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) !=
            serialBefore ||
        !g_preparedShouldRender.load(std::memory_order_acquire) ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo2)
    {
        return false;
    }
    cadence.preparedSerial = serialBefore;
    cadence.predictedDisplayPeriodNs = targetPeriod;
    cadence.predictedDisplayDeltaNs = displayDelta;
    return true;
}

static ID3D11Texture2D* Halo2TextureBehind(
    ID3D11RenderTargetView* view, D3D11_TEXTURE2D_DESC& desc);

bool VR_Halo2BeginSynchronousPair(
    uint32_t generation, uint64_t preparedSerial,
    ID3D11RenderTargetView* finalRtv, ID3D11RenderTargetView* sceneRtv)
{
    const uint32_t resourceEpoch =
        g_halo2SynchronousResourceEpoch.load(std::memory_order_acquire);
    if (!finalRtv || !Halo2SynchronousContextCurrent(
                         generation, preparedSerial, resourceEpoch) ||
        finalRtv == g_eyeCacheRtvs[0] || finalRtv == g_eyeCacheRtvs[1])
    {
        return false;
    }

    const uint64_t reserved =
        Halo2ReservedSynchronousSerial(preparedSerial);
    uint64_t observed =
        g_halo2SynchronousPairToken.load(std::memory_order_acquire);
    for (;;)
    {
        // A live reservation belongs to an in-flight callback. An equal or
        // newer published serial is a duplicate/out-of-order outer call.
        if (Halo2SynchronousSerialIsReserved(observed) ||
            (observed != 0 && observed >= preparedSerial))
        {
            return false;
        }
        if (g_halo2SynchronousPairToken.compare_exchange_weak(
                observed, reserved, std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            break;
        }
    }

    // The reservation first makes every prior pair inadmissible. Publish the
    // borrowed target and exact resource identity only for this serial, then
    // clear both eye stamps before either engine render can start.
    g_halo2SynchronousPairGeneration.store(
        generation, std::memory_order_relaxed);
    g_halo2SynchronousPairResourceEpoch.store(
        resourceEpoch, std::memory_order_relaxed);
    g_halo2SynchronousFinalRtv.store(finalRtv, std::memory_order_relaxed);
    if (g_halo2PrePairWanted.exchange(false, std::memory_order_acq_rel) &&
        g_context && g_device)
    {
        // The comparison must be against the SAME target the eyes are copied
        // from. Only the two named slots exist at pair begin; a learned
        // bound-target source leaves the pre-pair copy unsampled.
        const int source = g_halo2CaptureSource.load(std::memory_order_relaxed);
        ID3D11RenderTargetView* const sourceRtv =
            source == 0 ? finalRtv : (source == 1 ? sceneRtv : nullptr);
        D3D11_TEXTURE2D_DESC desc{};
        ID3D11Texture2D* texture =
            sourceRtv ? Halo2TextureBehind(sourceRtv, desc) : nullptr;
        if (texture)
        {
            if (g_halo2PrePairCache)
            {
                D3D11_TEXTURE2D_DESC have{};
                g_halo2PrePairCache->GetDesc(&have);
                if (have.Width != desc.Width || have.Height != desc.Height ||
                    have.Format != desc.Format)
                {
                    g_halo2PrePairCache->Release();
                    g_halo2PrePairCache = nullptr;
                }
            }
            if (!g_halo2PrePairCache)
            {
                D3D11_TEXTURE2D_DESC make = desc;
                make.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                make.MiscFlags = 0;
                make.CPUAccessFlags = 0;
                make.Usage = D3D11_USAGE_DEFAULT;
                make.MipLevels = 1;
                make.ArraySize = 1;
                if (FAILED(g_device->CreateTexture2D(&make, nullptr, &g_halo2PrePairCache)))
                {
                    g_halo2PrePairCache = nullptr;
                    if (!g_halo2PrePairCreateFailed.exchange(true, std::memory_order_relaxed))
                    {
                        LOG("Halo 2 pre-pair witness: the %ux%u fmt %u copy "
                            "could not be created; 'captured target before the "
                            "pair' stays unsampled",
                            desc.Width, desc.Height,
                            static_cast<unsigned>(desc.Format));
                    }
                }
            }
            if (g_halo2PrePairCache)
            {
                g_context->CopyResource(g_halo2PrePairCache, texture);
                g_halo2PrePairSerial.store(preparedSerial, std::memory_order_relaxed);
                g_halo2PrePairHasImage.store(true, std::memory_order_release);
            }
            texture->Release();
        }
    }
    g_halo2SynchronousSceneRtv.store(
        (sceneRtv == g_eyeCacheRtvs[0] || sceneRtv == g_eyeCacheRtvs[1])
            ? nullptr : sceneRtv,
        std::memory_order_relaxed);
    g_halo2SynchronousTargetSerial.store(
        preparedSerial, std::memory_order_release);
    for (int eye = 0; eye < 2; ++eye)
    {
        g_halo2SynchronousEyeSerial[eye].store(
            0, std::memory_order_release);
        g_halo2SynchronousEyeGeneration[eye].store(
            0, std::memory_order_relaxed);
        g_halo2SynchronousEyeResourceEpoch[eye].store(
            0, std::memory_order_relaxed);
        g_halo2SynchronousCaptureSerial[eye].store(
            0, std::memory_order_release);
        g_halo2SynchronousCaptureCount[eye].store(
            0, std::memory_order_relaxed);
        g_eyeHasImage[eye] = false;
    }

    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            reserved ||
        g_halo2SynchronousTargetSerial.load(std::memory_order_acquire) !=
            preparedSerial ||
        g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed) !=
            finalRtv ||
        !Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        VR_Halo2InvalidateSynchronousPair(generation, preparedSerial);
        return false;
    }
    return true;
}

bool VR_Halo2BeginSynchronousEye(
    uint32_t generation, uint64_t preparedSerial, int eye)
{
    if (eye < 0 || eye > 1 ||
        g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            Halo2ReservedSynchronousSerial(preparedSerial) ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
            generation)
    {
        return false;
    }
    VR_BeginRasterEye(eye);
    return g_halo2SynchronousEyeScope.active &&
        g_halo2SynchronousEyeScope.generation == generation &&
        g_halo2SynchronousEyeScope.preparedSerial == preparedSerial &&
        g_halo2SynchronousEyeScope.eye == eye;
}

bool VR_Halo2CompleteSynchronousEye(
    uint32_t generation, uint64_t preparedSerial, int eye,
    float halfFovX, float halfFovY,
    const float* renderedPosition, const float* renderedOrientation)
{
    if (eye < 0 || eye > 1 ||
        !Halo2SynchronousHalfFovValid(halfFovX) ||
        !Halo2SynchronousHalfFovValid(halfFovY))
    {
        return false;
    }
    const bool poseOwned = renderedPosition && renderedOrientation;
    if (poseOwned)
    {
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(renderedPosition[i]))
                return false;
        float lengthSquared = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (!std::isfinite(renderedOrientation[i]))
                return false;
            lengthSquared += renderedOrientation[i] * renderedOrientation[i];
        }
        if (std::fabs(lengthSquared - 1.0f) > 0.05f)
            return false;
    }
    const uint32_t resourceEpoch =
        g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    const uint64_t reserved =
        Halo2ReservedSynchronousSerial(preparedSerial);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            reserved ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
            generation ||
        g_halo2SynchronousCaptureSerial[eye].load(
            std::memory_order_acquire) != preparedSerial ||
        g_halo2SynchronousCaptureCount[eye].load(
            std::memory_order_relaxed) <
                kHalo2SynchronousRequiredCaptures ||
        !Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        return false;
    }

    g_halo2SynchronousEyeGeneration[eye].store(
        generation, std::memory_order_relaxed);
    g_halo2SynchronousEyeResourceEpoch[eye].store(
        resourceEpoch, std::memory_order_relaxed);
    g_halo2SynchronousEyeHalfFovX[eye].store(
        halfFovX, std::memory_order_relaxed);
    g_halo2SynchronousEyeHalfFovY[eye].store(
        halfFovY, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        g_halo2SynchronousEyePose[eye][i].store(
            poseOwned ? renderedPosition[i] : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i)
        g_halo2SynchronousEyePose[eye][3 + i].store(
            poseOwned ? renderedOrientation[i] : (i == 3 ? 1.0f : 0.0f),
            std::memory_order_relaxed);
    g_halo2SynchronousEyePoseOwned[eye].store(
        poseOwned, std::memory_order_relaxed);
    g_halo2SynchronousEyeSerial[eye].store(
        preparedSerial, std::memory_order_release);

    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            reserved ||
        !Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        uint64_t expected = preparedSerial;
        (void)g_halo2SynchronousEyeSerial[eye].compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        return false;
    }
    g_eyeHasImage[eye] = true;
    return true;
}

bool VR_Halo2CompleteSynchronousPair(
    uint32_t generation, uint64_t preparedSerial)
{
    const uint64_t reserved =
        Halo2ReservedSynchronousSerial(preparedSerial);
    const uint32_t resourceEpoch =
        g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            reserved ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
            generation ||
        !Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeSerial[eye].load(
                    std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousEyeGeneration[eye].load(
                    std::memory_order_relaxed) != generation ||
            g_halo2SynchronousEyeResourceEpoch[eye].load(
                    std::memory_order_relaxed) != resourceEpoch ||
            !g_eyeHasImage[eye])
        {
            return false;
        }
    }
    uint64_t expected = reserved;
    if (!g_halo2SynchronousPairToken.compare_exchange_strong(
            expected, preparedSerial, std::memory_order_release,
            std::memory_order_acquire))
    {
        return false;
    }
    return PublishHalo2SynchronousPresentation(
        generation, preparedSerial,
        Halo2SynchronousFrameDisposition::Complete);
}

bool VR_Halo2ClaimSynchronousPairForPresentation(
    uint32_t generation, uint64_t preparedSerial)
{
    if (!generation || !preparedSerial ||
        Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return false;
    }
    const uint32_t resourceEpoch =
        g_halo2SynchronousResourceEpoch.load(std::memory_order_acquire);
    if (!Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        return false;
    }

    const uint64_t reserved =
        Halo2ReservedSynchronousSerial(preparedSerial);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != reserved ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
            generation)
    {
        return false;
    }
    return PublishHalo2SynchronousPresentation(
        generation, preparedSerial,
        Halo2SynchronousFrameDisposition::Claimed);
}

bool VR_Halo2GetSynchronousPresentationStamp(
    Halo2SynchronousPresentationStamp& presentation)
{
    presentation = {};
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        const uint64_t sequenceBefore =
            g_halo2SynchronousPresentationSequence.load(
                std::memory_order_acquire);
        if ((sequenceBefore & 1u) != 0)
            continue;

        Halo2SynchronousPresentationStamp candidate{};
        candidate.preparedSerial =
            g_halo2SynchronousPresentationSerial.load(
                std::memory_order_relaxed);
        candidate.generation =
            g_halo2SynchronousPresentationGeneration.load(
                std::memory_order_relaxed);
        candidate.disposition =
            static_cast<Halo2SynchronousFrameDisposition>(
                g_halo2SynchronousPresentationDisposition.load(
                    std::memory_order_relaxed));
        const uint64_t sequenceAfter =
            g_halo2SynchronousPresentationSequence.load(
                std::memory_order_acquire);
        if (sequenceBefore != sequenceAfter || (sequenceAfter & 1u) != 0)
            continue;
        if (!candidate.generation || !candidate.preparedSerial ||
            (candidate.disposition !=
                 Halo2SynchronousFrameDisposition::Claimed &&
             candidate.disposition !=
                 Halo2SynchronousFrameDisposition::Complete))
        {
            return false;
        }
        presentation = candidate;
        return true;
    }
    return false;
}

void VR_Halo2InvalidateSynchronousPair(
    uint32_t generation, uint64_t preparedSerial)
{
    if (!preparedSerial ||
        Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return;
    }
    const uint64_t reserved =
        Halo2ReservedSynchronousSerial(preparedSerial);
    uint64_t observed =
        g_halo2SynchronousPairToken.load(std::memory_order_acquire);
    if (observed == reserved || observed == preparedSerial)
    {
        (void)g_halo2SynchronousPairToken.compare_exchange_strong(
            observed, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeGeneration[eye].load(
                    std::memory_order_relaxed) == generation)
        {
            uint64_t eyeSerial = preparedSerial;
            (void)g_halo2SynchronousEyeSerial[eye].compare_exchange_strong(
                eyeSerial, 0, std::memory_order_acq_rel,
                std::memory_order_acquire);
        }
        uint64_t redirectSerial = preparedSerial;
        (void)g_halo2SynchronousCaptureSerial[eye].compare_exchange_strong(
            redirectSerial, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
    uint64_t targetSerial = preparedSerial;
    (void)g_halo2SynchronousTargetSerial.compare_exchange_strong(
        targetSerial, 0, std::memory_order_acq_rel,
        std::memory_order_acquire);
}

void VR_ResetHalo2SynchronousStereo()
{
    if(g_halo2ScopeSource){g_halo2ScopeSource->Release();g_halo2ScopeSource=nullptr;}
    g_halo2ScopeSourceRtv=nullptr;g_halo2ScopeGeneration=0;g_halo2ScopeSerial=0;
    g_halo2ScopeCopyActive=g_halo2ScopeSourceBound=false;
    g_halo2LastPair = {};
    g_halo2SynchronousSceneRtv.store(nullptr, std::memory_order_release);
    std::memset(g_halo2ProbeHasImage, 0, sizeof(g_halo2ProbeHasImage));
    for (auto& slot : g_halo2ProbeBox)
        for (auto*& box : slot)
        {
            if (box) box->Release();
            box = nullptr;
        }
    std::memset(g_halo2ProbeBoxHasImage, 0, sizeof(g_halo2ProbeBoxHasImage));
    std::memset(g_halo2ProbeBoxFormat, 0, sizeof(g_halo2ProbeBoxFormat));
    if (g_halo2PrePairCache)
    {
        g_halo2PrePairCache->Release();
        g_halo2PrePairCache = nullptr;
    }
    g_halo2PrePairHasImage.store(false, std::memory_order_release);
    const uint32_t generation =
        g_halo2SynchronousPairGeneration.load(std::memory_order_acquire);
    g_halo2SynchronousPairToken.store(0, std::memory_order_release);
    g_halo2SynchronousPairGeneration.store(0, std::memory_order_release);
    g_halo2SynchronousPairResourceEpoch.store(0, std::memory_order_release);
    g_halo2SynchronousTargetSerial.store(0, std::memory_order_release);
    g_halo2SynchronousFinalRtv.store(nullptr, std::memory_order_release);
    // Do not clear g_halo2SynchronousPresentation*. Game_AutoVrTick, resize, or
    // cache recreation can reset live resources before Submit consumes this
    // exact serial. The integer-only stamp owns no RTV/resource and cannot
    // match a later process-monotonic prepared serial.
    g_halo2FirstPairAccepted.store(false, std::memory_order_release);
    for (int eye = 0; eye < 2; ++eye)
    {
        g_halo2SynchronousEyeSerial[eye].store(
            0, std::memory_order_release);
        g_halo2SynchronousEyeGeneration[eye].store(
            0, std::memory_order_release);
        g_halo2SynchronousEyeResourceEpoch[eye].store(
            0, std::memory_order_release);
        g_halo2SynchronousCaptureSerial[eye].store(
            0, std::memory_order_release);
        g_halo2SynchronousCaptureCount[eye].store(
            0, std::memory_order_release);
    }
    // Clear the runtime-owner proof along the same reset edge. The stamped
    // Gameplay mode is no longer eligible once its heartbeat is revoked, and
    // a later pair must pass the full render/upload/xrEndFrame transaction to
    // publish a new one. ClearHeartbeat is bounded atomic-only state work.
    if (generation)
        TitleAdapter_ClearHeartbeat(GameTitle::Halo2, generation);
}

bool VR_Halo2GetRepeatableHalfFovs(
    uint32_t generation, uint64_t preparedSerial,
    float halfX[2], float halfY[2])
{
    const Halo2LastPair& last = g_halo2LastPair;
    if (!last.valid || !generation || last.generation != generation ||
        !preparedSerial || preparedSerial <= last.serial)
    {
        return false;
    }
    const uint64_t nowMs = GetTickCount64();
    if (nowMs < last.tickMs || nowMs - last.tickMs > kHalo2RepeatPairMaxAgeMs)
        return false;
    // A reserved token means a newer pair is being rendered right now and
    // may already have overwritten one eye cache: never mix two frames.
    if (Halo2SynchronousSerialIsReserved(
            g_halo2SynchronousPairToken.load(std::memory_order_acquire)))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (!g_eyeHasImage[eye] ||
            g_halo2SynchronousCaptureSerial[eye].load(
                std::memory_order_acquire) != last.serial ||
            !Halo2SynchronousHalfFovValid(last.halfX[eye]) ||
            !Halo2SynchronousHalfFovValid(last.halfY[eye]))
        {
            return false;
        }
        halfX[eye] = last.halfX[eye];
        halfY[eye] = last.halfY[eye];
    }
    return true;
}

bool VR_Halo2GetSynchronousPairPoses(
    uint32_t generation, uint64_t preparedSerial,
    float position[2][3], float orientation[2][4])
{
    if (!position || !orientation || !generation || !preparedSerial ||
        Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return false;
    }
    const uint32_t resourceEpoch =
        g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            preparedSerial ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
            generation ||
        !Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeSerial[eye].load(
                    std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousEyeGeneration[eye].load(
                    std::memory_order_relaxed) != generation ||
            !g_halo2SynchronousEyePoseOwned[eye].load(
                    std::memory_order_relaxed))
        {
            return false;
        }
        for (int i = 0; i < 3; ++i)
            position[eye][i] =
                g_halo2SynchronousEyePose[eye][i].load(std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i)
            orientation[eye][i] =
                g_halo2SynchronousEyePose[eye][3 + i].load(std::memory_order_relaxed);
    }
    return g_halo2SynchronousPairToken.load(std::memory_order_acquire) ==
        preparedSerial;
}

bool VR_Halo2GetSynchronousHalfFovs(
    uint32_t generation, uint64_t preparedSerial,
    float halfX[2], float halfY[2])
{
    if (!halfX || !halfY || !generation || !preparedSerial ||
        Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return false;
    }
    const uint32_t resourceEpoch =
        g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
            preparedSerial ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
            generation ||
        !Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeSerial[eye].load(
                    std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousEyeGeneration[eye].load(
                    std::memory_order_relaxed) != generation ||
            g_halo2SynchronousEyeResourceEpoch[eye].load(
                    std::memory_order_relaxed) != resourceEpoch ||
            !g_eyeHasImage[eye])
        {
            return false;
        }
        halfX[eye] = g_halo2SynchronousEyeHalfFovX[eye].load(
            std::memory_order_relaxed);
        halfY[eye] = g_halo2SynchronousEyeHalfFovY[eye].load(
            std::memory_order_relaxed);
        if (!Halo2SynchronousHalfFovValid(halfX[eye]) ||
            !Halo2SynchronousHalfFovValid(halfY[eye]))
        {
            return false;
        }
    }
    return g_halo2SynchronousPairToken.load(std::memory_order_acquire) ==
            preparedSerial &&
        Halo2SynchronousContextCurrent(
            generation, preparedSerial, resourceEpoch);
}
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
bool VR_Halo4GetRenderSnapshot(Halo4VrRenderSnapshot& snapshot)
{
    snapshot = {};
    const uint64_t serial =
        g_preparedSerialPublished.load(std::memory_order_acquire);
    if (!serial ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }

    const uint32_t index =
        g_halo4RenderSnapshotIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;
    uint32_t state =
        g_halo4RenderSnapshotStates[index].load(std::memory_order_seq_cst);
    if ((state & kHalo4RenderSnapshotWriter) != 0 ||
        state >= kHalo4RenderSnapshotWriter - 1 ||
        !g_halo4RenderSnapshotStates[index].compare_exchange_strong(
            state, state + 1, std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        return false;
    }
    if (g_halo4RenderSnapshotIndex.load(std::memory_order_seq_cst) != index)
    {
        g_halo4RenderSnapshotStates[index].fetch_sub(
            1, std::memory_order_seq_cst);
        return false;
    }
    const Halo4VrRenderSnapshot local = g_halo4RenderSnapshots[index];
    g_halo4RenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);

    // Close publication rollover on both sides of the copy. A snapshot from a
    // prior or newly prepared frame is never admitted into this transaction.
    if (local.preparedSerial != serial ||
        g_preparedSerialPublished.load(std::memory_order_acquire) != serial ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }
    snapshot = local;
    return true;
}
#endif

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
void VR_ReachRenderCandidate_ColdPoll()
{
    PollReachDisplayCandidate();
}

ReachPreparedFrameToken VR_ReachPreparedFrame(
    const ReachModuleEpoch& epoch)
{
    const uint64_t serial =
        g_preparedSerialPublished.load(std::memory_order_acquire);
    const uint64_t viewSerial =
        g_preparedViewSerialPublished.load(std::memory_order_acquire);
    return ReachPreparedFrameToken::Create(
        epoch, serial,
        serial != 0 &&
        viewSerial == serial &&
        g_preparedShouldRender.load(std::memory_order_acquire));
}

bool VR_ReachGetRenderSnapshot(
    const ReachPreparedFrameToken& prepared,
    ReachVrRenderSnapshot& snapshot)
{
    snapshot = {};
    if (!prepared.Ready() || !prepared.Serial())
        return false;

    const uint32_t index =
        g_reachRenderSnapshotIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;

    uint32_t state =
        g_reachRenderSnapshotStates[index].load(std::memory_order_seq_cst);
    if ((state & kReachRenderSnapshotWriter) != 0 ||
        state >= kReachRenderSnapshotWriter - 1)
    {
        return false;
    }
    if (!g_reachRenderSnapshotStates[index].compare_exchange_strong(
            state, state + 1,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        return false;
    }

    // A publication can switch slots between our index load and reader pin.
    // Revalidate before touching plain snapshot storage. A writer cannot claim
    // this slot while the reader count is nonzero.
    if (g_reachRenderSnapshotIndex.load(std::memory_order_seq_cst) != index)
    {
        g_reachRenderSnapshotStates[index].fetch_sub(
            1, std::memory_order_seq_cst);
        return false;
    }

    const ReachVrRenderSnapshot local = g_reachRenderSnapshots[index];
    g_reachRenderSnapshotStates[index].fetch_sub(
        1, std::memory_order_seq_cst);
    if (local.preparedSerial != prepared.Serial())
        return false;
    snapshot = local;
    return true;
}

bool VR_ReachDisplayReady(const ReachModuleEpoch& epoch)
{
    // Pin the plain proof fields exactly like BeginRenderAccess. The worker
    // disables publication and waits for this reader count before releasing or
    // zeroing them, so readiness cannot race resource invalidation.
    g_reachCaptureUsers.fetch_add(1, std::memory_order_seq_cst);
    const bool ready =
        g_reachCaptureEnabled.load(std::memory_order_seq_cst) &&
        ReachSameModuleEpoch(g_reachCaptureProof.continuity.epoch, epoch) &&
        ReachRenderCandidate_IsPreflightCurrent(
            g_reachCaptureProof.preflight);
    g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
    return ready;
}

bool VR_ReachBeginRenderAccess(
    const ReachModuleEpoch& epoch,
    const ReachPreparedFrameToken& prepared,
    ReachVrRenderAccess& access)
{
    access = {};
    g_reachCaptureUsers.fetch_add(1, std::memory_order_seq_cst);
    if (!g_reachCaptureEnabled.load(std::memory_order_seq_cst) ||
        !prepared.Ready() ||
        !ReachSameModuleEpoch(prepared.Epoch(), epoch) ||
        !g_reachCaptureSource || !g_reachCaptureContext ||
        !g_reachCaptureEyes[0] || !g_reachCaptureEyes[1] ||
        !ReachSameModuleEpoch(g_reachCaptureProof.continuity.epoch, epoch) ||
        !ReachRenderCandidate_IsPreflightCurrent(
            g_reachCaptureProof.preflight))
    {
        g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    access.proof = g_reachCaptureProof;
    access.source = g_reachCaptureSource;
    access.eyes[0] = g_reachCaptureEyes[0];
    access.eyes[1] = g_reachCaptureEyes[1];
    access.context = g_reachCaptureContext;
    access.preparedSerial = prepared.Serial();
    access.active = true;
    return true;
}

bool VR_ReachScopeReady(const ReachVrRenderAccess& access)
{
    return access.active && access.context && access.source && g_scopeCache &&
        g_scopeCacheSrv && g_reachCaptureEnabled.load(std::memory_order_seq_cst) &&
        g_scopeCacheDesc.Width==g_reachCaptureDesc.Width &&
        g_scopeCacheDesc.Height==g_reachCaptureDesc.Height &&
        g_scopeCacheDesc.Format==g_reachCaptureDesc.Format;
}
bool VR_ReachCopyScope(ReachVrRenderAccess& access)
{
    if(!VR_ReachScopeReady(access))return false;
    // Reach owns the final native target. Never redirect a render-target bind
    // or publish an eye serial for the optional mono image.
    access.context->CopyResource(g_scopeCache,access.source);
    g_scopeImageTitle=GameTitle::HaloReach;
    g_scopeImageGeneration=access.proof.continuity.epoch.generation;
    g_scopeHasImage.store(true,std::memory_order_release);
    return true;
}
bool VR_ReachCopyEye(ReachVrRenderAccess& access, int eye)
{
    if (!access.active || eye < 0 || eye > 1 ||
        !access.context || !access.source || !access.eyes[eye] ||
        !g_reachCaptureEnabled.load(std::memory_order_seq_cst))
        return false;
    access.context->CopyResource(access.eyes[eye], access.source);
    g_reachEyeSerial[eye].store(
        access.preparedSerial, std::memory_order_release);
    return true;
}

void VR_ReachEndRenderAccess(ReachVrRenderAccess& access)
{
    if (!access.active)
        return;
    access = {};
    g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
}
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
namespace
{
    void CaptureHalo2TemporalEyeBeforeSubmit()
    {
        const uint64_t serial =
            g_halo2PendingSerial.load(std::memory_order_acquire);
        if (!serial || Halo2PendingSerialIsReserved(serial))
            return;
        const uint32_t generation =
            g_halo2PendingGeneration.load(std::memory_order_relaxed);
        const int eye = g_halo2PendingEye.load(std::memory_order_relaxed);
        const float halfFovX =
            g_halo2PendingHalfFovX.load(std::memory_order_relaxed);
        const float halfFovY =
            g_halo2PendingHalfFovY.load(std::memory_order_relaxed);
        const bool current =
            generation != 0 && eye == Halo2TemporalEyeForSerial(
                serial, g_config.right_eye_first) &&
            serial == g_preparedFrame.serial &&
            serial == g_halo2PendingSerial.load(std::memory_order_acquire) &&
            generation == TitleAdapter_GetGeneration(GameTitle::Halo2) &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2;
        if (!current)
        {
            VR_ResetHalo2TemporalStereo();
            return;
        }

        // Claim this exact completed token before touching the backbuffer. A
        // duplicate hook call or worker teardown cancels by replacing the
        // sentinel with zero; the final CAS below then prevents cache-stamp
        // resurrection after that cancellation.
        const uint64_t reserved = Halo2ReservedPendingSerial(serial);
        uint64_t expectedPending = serial;
        if (!g_halo2PendingSerial.compare_exchange_strong(
                expectedPending, reserved, std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return;
        }

        ID3D11Texture2D* backbuffer =
            g_nextGameBackbuffer.load(std::memory_order_acquire);
        const bool captured = backbuffer && g_context &&
            g_gameBackbufferDescValid &&
            EnsureEyeCaches(g_gameBackbufferDesc) && g_eyeCache[eye] &&
            g_eyeCacheRtvs[eye] &&
            Blit(backbuffer, g_gameBackbufferDesc, g_eyeCache[eye],
                 g_eyeCacheDesc.Width, g_eyeCacheDesc.Height,
                 g_eyeCacheRtvs[eye]);
        if (!captured)
        {
            expectedPending = reserved;
            (void)g_halo2PendingSerial.compare_exchange_strong(
                expectedPending, 0, std::memory_order_acq_rel,
                std::memory_order_acquire);
            g_halo2XrPairsDropped.fetch_add(1, std::memory_order_relaxed);
            g_halo2LastDropReason.store(1, std::memory_order_relaxed);
            return;
        }

        g_halo2EyeGeneration[eye].store(
            generation, std::memory_order_relaxed);
        g_halo2EyeHalfFovX[eye].store(halfFovX, std::memory_order_relaxed);
        g_halo2EyeHalfFovY[eye].store(halfFovY, std::memory_order_relaxed);
        g_eyeHasImage[eye] = true;
        g_halo2EyeSerial[eye].store(serial, std::memory_order_release);
        expectedPending = reserved;
        if (!g_halo2PendingSerial.compare_exchange_strong(
                expectedPending, 0, std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            // Teardown/duplicate invalidation won the race. Revoke only the
            // just-copied stamp; exact serial checks keep any newer token safe.
            if (g_halo2EyeGeneration[eye].load(
                    std::memory_order_acquire) == generation &&
                g_halo2EyeSerial[eye].load(
                    std::memory_order_acquire) == serial)
            {
                g_halo2EyeSerial[eye].store(0, std::memory_order_release);
                g_halo2EyeGeneration[eye].store(0, std::memory_order_release);
                g_eyeHasImage[eye] = false;
            }
            return;
        }

        float halfX[2]{}, halfY[2]{};
        if (VR_Halo2GetTemporalHalfFovs(
                generation, serial, halfX, halfY))
        {
            const uint64_t nowMs = GetTickCount64();
            TitleAdapter_PublishHeartbeat(
                GameTitle::Halo2, generation, nowMs);
            TitleAdapter_PublishMode(
                GameTitle::Halo2, generation, RuntimeMode::Gameplay);
        }
    }
}
#endif

void VR_BeforePresent(IDXGISwapChain* sc)
{
    // DLSS frame retirement belongs to Present, not to successful stereo
    // upload. This covers pause/loading, shouldRender=false and every early
    // return, including failed XR acquisition, without altering VR ownership.
    struct DlssFrameCleanup
    {
        ~DlssFrameCleanup() { DlssEndFrame(); }
    } dlssFrameCleanup;
    g_nativePointerFrame = {};
    NativeMenuPointer_FrameStatus("no-native-screen");
    struct PublishNativePointerOnExit {
        ~PublishNativePointerOnExit() {
            NativeMenuPointer_Publish(g_nativePointerFrame.active,
                g_nativePointerFrame.u, g_nativePointerFrame.v, g_nativePointerFrame.pressed);
        }
    } publishNativePointerOnExit;
    if constexpr (kEnableFramePacingTransitionCapture)
    {
        LARGE_INTEGER pacingBeforePresent{};
        QueryPerformanceCounter(&pacingBeforePresent);
        if (g_framePacingPending.serial)
        {
            g_framePacingPending.beforePresentQpc =
                pacingBeforePresent.QuadPart;
            g_framePacingPending.scopeActive =
                g_scopeActive.load(std::memory_order_relaxed);
            GameFramePerfCounters perfEnd{};
            Game_ReadFramePerfCounters(perfEnd);
            SubtractPerfCounters(
                perfEnd, g_framePacingPerfStart, g_framePacingPending.perf);
        }
    }
    g_gameSwapchain = sc;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // This observation is intentionally before every OpenXR/session early
    // return. It rejects unrelated Presents with one raw engine-swapchain
    // identity read, then at most every 250 ms captures a fixed-storage retained
    // buffer/device snapshot at the exact Reach Present boundary. It performs
    // no file/signature scan, resource allocation, lock, or logging.
    ObserveReachPresentSwapchain(sc);
#endif
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    // Presentation cleanup must not depend on a healthy/running OpenXR session.
    // The D3D Present hook still reaches this path when instance/session setup
    // failed, so it can acknowledge ODST teardown and restore shared title
    // policy without leaving camera-only ownership latched indefinitely.
    Game_ProcessPresentationDetachRequest();
#endif
    if (!g_gameBackbufferDescValid && sc)
    {
        ID3D11Texture2D* backbuffer = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backbuffer))) && backbuffer)
        {
            backbuffer->GetDesc(&g_gameBackbufferDesc);
            g_gameBackbufferDescValid = true;
            backbuffer->Release();
        }
    }
    if (g_state == State::Failed)
        return;
    if (g_state == State::Uninitialized)
    {
        // Wait for the background thread to finish creating the OpenXR
        // instance. Until then, do nothing so the game renders normally to
        // the monitor instead of freezing.
        if (g_instanceFailed)
        {
            g_state = State::Failed;
            return;
        }
        if (!g_instanceReady)
            return;
        static bool announced = false;
        if (!announced)
        {
            LOG("instance ready; creating VR session on the render thread");
            announced = true;
        }
        if (!InitSession(sc))
            return; // state is Failed, message shown
        g_state = State::Ready;
    }

    // FPS counter for the menu status line
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (g_lastBeforePresentQpc.QuadPart)
        g_presentIntervalsMs.Add(
            QpcMs(now.QuadPart - g_lastBeforePresentQpc.QuadPart));
    g_lastBeforePresentQpc = now;
    g_fpsFrames++;
    if (g_fpsTimer.QuadPart == 0)
        g_fpsTimer = now;
    else if (now.QuadPart - g_fpsTimer.QuadPart >= freq.QuadPart)
    {
        g_status.fps = (float)g_fpsFrames * freq.QuadPart / (float)(now.QuadPart - g_fpsTimer.QuadPart);
        g_fpsFrames = 0;
        g_fpsTimer = now;
        // Every 10 s, put the frame rate in the log so performance reports
        // ("fps has been lower") can be tied to a session and a build.
        static int fpsLogCountdown = 10;
        if (--fpsLogCountdown <= 0)
        {
            fpsLogCountdown = 10;
            LOG("fps %.0f (stereo %s); app cadence %.1fHz "
                "(xrWaitFrame period), panel %.1fHz",
                g_status.fps, g_stereoEnabled.load() ? "on" : "off",
                VR_HeadsetRefreshHz(),
                g_panelRefreshHz.load(std::memory_order_relaxed));
        }
    }

    const uint64_t timingNowMs = GetTickCount64();
    if (!g_timingLogStartMs)
        g_timingLogStartMs = timingNowMs;
    else if (timingNowMs - g_timingLogStartMs >= 10000)
    {
        // Backward-compatible rolling summaries. These are the latest 512
        // samples (not a fixed 10-second window) and are intentionally not used
        // to attribute a transition; the PACING capture correlates exact rows.
        LOG("timing: frame interval p95 %.2fms p99 %.2fms; "
            "renderWindow p95 %.2fms; xrEndFrame p95 %.2fms; "
            "DXGI Present p95 %.2fms; wait handoff p95 %.2fms; "
            "prediction error p95 %.3fms; missed=%llu duplicate=%llu "
            "orderFailures=%llu firstCamera=%.3fms stalls=%llu worstStall=%llums",
            TimingPercentile(g_presentIntervalsMs, 0.95),
            TimingPercentile(g_presentIntervalsMs, 0.99),
            TimingPercentile(g_renderWindowMs, 0.95),
            TimingPercentile(g_endFrameDurationsMs, 0.95),
            TimingPercentile(g_presentDurationsMs, 0.95),
            TimingPercentile(g_waitDurationsMs, 0.95),
            TimingPercentile(g_predictionErrorMs, 0.95),
            static_cast<unsigned long long>(g_missedPredictions),
            static_cast<unsigned long long>(g_duplicatePredictions),
            static_cast<unsigned long long>(g_frameOrderFailures),
            g_firstCameraDelayUs.load(std::memory_order_relaxed) / 1000.0,
            static_cast<unsigned long long>(
                g_frameStalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_frameStallWorstMs.load(std::memory_order_relaxed)));
        g_timingLogStartMs = timingNowMs;
    }

    PollEvents();
    if (g_state != State::Ready || !g_sessionRunning)
        return;

    // Auto-enter/exit VR when a level loads/unloads (no F2/F11 needed).
    Game_AutoVrTick();
#if HALOMCCVR_HALO2_STEREO6DOF
    // Allocate/recreate the two exact-output caches at Present, before the next
    // prepared frame can reach Halo 2's render hooks. BeginPair is therefore a
    // pointer-only proof and never performs COM work or allocation.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
        g_stereoEnabled.load(std::memory_order_acquire) &&
        g_gameBackbufferDescValid && g_device)
    {
        (void)EnsureEyeCaches(g_gameBackbufferDesc);
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
    // H2's one stock render has returned by this Present boundary. Copy only a
    // matching completed token; no camera hook performs COM or D3D work.
    CaptureHalo2TemporalEyeBeforeSubmit();
#endif
    if (!g_config.scope_enabled || !Game_IsHeadTracking())
        VR_SetScopeActive(false);

    SubmitPreparedFrame(sc);
}

void VR_AfterPresent(IDXGISwapChain* sc, int64_t presentStartQpc,
                     int64_t presentEndQpc, HRESULT presentResult)
{
    if (presentEndQpc >= presentStartQpc && presentStartQpc)
        g_presentDurationsMs.Add(
            QpcMs(presentEndQpc - presentStartQpc));

    if constexpr (kEnableFramePacingTransitionCapture)
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (g_framePacingPending.serial && g_framePacingPending.submitted)
        {
            g_framePacingPending.presentStartQpc = presentStartQpc;
            g_framePacingPending.presentEndQpc = presentEndQpc;
            g_framePacingPending.afterPresentQpc = now.QuadPart;
            g_framePacingPending.presentResult = presentResult;
            PublishFramePacingRecord(g_framePacingPending);
        }
        g_framePacingPending = {};
    }

    // Present has advanced the flip-model chain. Retain its reported current
    // buffer outside all camera/render hooks for ODST's direct death capture.
    if (sc)
    {
        UINT currentIndex = 0;
        if (g_flipIndexOwner != sc)
        {
            if (g_flipIndexChain)
                g_flipIndexChain->Release();
            g_flipIndexChain = nullptr;
            g_flipIndexOwner = sc;
            sc->QueryInterface(__uuidof(IDXGISwapChain3),
                reinterpret_cast<void**>(&g_flipIndexChain));
        }
        if (g_flipIndexChain)
            currentIndex = g_flipIndexChain->GetCurrentBackBufferIndex();
        ID3D11Texture2D* next = nullptr;
        if (SUCCEEDED(sc->GetBuffer(currentIndex, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&next))) && next)
        {
            ID3D11Texture2D* previous =
                g_nextGameBackbuffer.exchange(next, std::memory_order_acq_rel);
            if (previous)
                previous->Release();
        }
    }

    if (g_state != State::Ready || !g_sessionRunning)
        return;
    PrepareNextFrame();
}

void VR_FramePacingWorkerPoll()
{
    NativeMenuPointer_ReportFailures();
    // Keep exceptional pipeline reporting off the game's render path. Packet
    // misses are counted there with one atomic increment and summarized here.
    static uint64_t reportedPacketMisses = 0;
    static uint64_t reportedWaitFailures = 0;
    static uint64_t reportedSignalFailures = 0;
    static uint64_t lastPacketMissReportMs = 0;
    static uint64_t lastWaitFailureReportMs = 0;
    static uint64_t lastSignalFailureReportMs = 0;
    static bool pipelineFaultReported = false;
    const uint64_t workerNowMs = GetTickCount64();
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO || \
    HALOMCCVR_HALO2_STEREO6DOF
    static uint64_t lastHalo2XrPairReportMs = 0;
    if (!lastHalo2XrPairReportMs ||
        workerNowMs - lastHalo2XrPairReportMs >= 2000)
    {
        const uint64_t submitted = g_halo2XrPairsSubmitted.exchange(
            0, std::memory_order_relaxed);
        const uint64_t dropped = g_halo2XrPairsDropped.exchange(
            0, std::memory_order_relaxed);
#if HALOMCCVR_HALO2_STEREO6DOF
        const uint64_t repeated = g_halo2XrPairsRepeated.exchange(
            0, std::memory_order_relaxed);
#endif
        uint32_t reason = g_halo2LastDropReason.exchange(
            0, std::memory_order_relaxed);
        static const char* const kDropReasons[] = {
            "none",
#if HALOMCCVR_HALO2_STEREO6DOF
            "render-target redirect incomplete",
#else
            "backbuffer capture incomplete",
#endif
            "eye upload incomplete", "projection incomplete"};
        if (reason >= sizeof(kDropReasons) / sizeof(kDropReasons[0]))
            reason = 0;
        if (submitted || dropped ||
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
#if HALOMCCVR_HALO2_STEREO6DOF
            LOG("Halo 2 C-H2-6 synchronous XR publish: %llu exact-current-"
                "serial pairs submitted, %llu last-pair repeats (game below "
                "panel rate), %llu recoverable frame drops in 2s; last drop: "
                "%s (two renders/frame; no cross-frame eye mixing; drops "
                "keep the core/session armed)",
                static_cast<unsigned long long>(submitted),
                static_cast<unsigned long long>(repeated),
                static_cast<unsigned long long>(dropped),
                kDropReasons[reason]);
#else
            LOG("Halo 2 C-H2-2 temporal XR publish: %llu adjacent pairs "
                "submitted, %llu recoverable frame drops in 2s; last drop: %s "
                "(one render/frame; recoverable drops do not request teardown)",
                static_cast<unsigned long long>(submitted),
                static_cast<unsigned long long>(dropped),
                kDropReasons[reason]);
#endif
        }
        lastHalo2XrPairReportMs = workerNowMs;
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    static uint64_t lastHalo4XrPairReportMs = 0;
    if (!lastHalo4XrPairReportMs ||
        workerNowMs - lastHalo4XrPairReportMs >= 2000)
    {
        const uint64_t submitted = g_halo4XrPairsSubmitted.exchange(
            0, std::memory_order_relaxed);
        const uint64_t dropped = g_halo4XrPairsDropped.exchange(
            0, std::memory_order_relaxed);
        uint32_t reason = g_halo4LastXrPairDropReason.exchange(
            0, std::memory_order_relaxed);
        static const char* const kDropReasons[] = {
            "none", "eye upload incomplete", "projection incomplete"};
        if (reason >= sizeof(kDropReasons) / sizeof(kDropReasons[0]))
            reason = 0;
        if (submitted || dropped ||
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        {
            LOG("Halo 4 C-H4-8 XR publish: %llu pairs submitted, %llu "
                "recoverable frame drops in 2s; last drop: %s (recoverable "
                "drops keep the core/session armed)",
                static_cast<unsigned long long>(submitted),
                static_cast<unsigned long long>(dropped),
                kDropReasons[reason]);
        }
        lastHalo4XrPairReportMs = workerNowMs;
    }
#endif
    const uint64_t packetMisses =
        g_waitPacketMisses.load(std::memory_order_relaxed);
    if (!packetMisses)
    {
        reportedPacketMisses = 0;
        lastPacketMissReportMs = 0;
    }
    else if (packetMisses != reportedPacketMisses &&
             (!lastPacketMissReportMs ||
              workerNowMs - lastPacketMissReportMs >= 1000))
    {
        LOG("pacing: wait-worker packet unavailable; skipped XR preparation "
            "without an inline wait (%llu misses total)",
            static_cast<unsigned long long>(packetMisses));
        reportedPacketMisses = packetMisses;
        lastPacketMissReportMs = workerNowMs;
    }

    const uint64_t waitFailures =
        g_waitFailuresObserved.load(std::memory_order_relaxed);
    if (!waitFailures)
    {
        reportedWaitFailures = 0;
        lastWaitFailureReportMs = 0;
    }
    else if (waitFailures != reportedWaitFailures &&
             (!lastWaitFailureReportMs ||
              workerNowMs - lastWaitFailureReportMs >= 1000))
    {
        LOG("timing: worker xrWaitFrame failure observed (%llu total): %s",
            static_cast<unsigned long long>(waitFailures),
            XrStr(g_waitedStateResult.load(std::memory_order_acquire)));
        reportedWaitFailures = waitFailures;
        lastWaitFailureReportMs = workerNowMs;
    }

    const uint64_t signalFailures =
        g_waitEventSignalFailures.load(std::memory_order_relaxed);
    if (!signalFailures)
    {
        reportedSignalFailures = 0;
        lastSignalFailureReportMs = 0;
    }
    else if (signalFailures != reportedSignalFailures &&
             (!lastSignalFailureReportMs ||
              workerNowMs - lastSignalFailureReportMs >= 1000))
    {
        LOG("pacing: frame-wait event signalling failed (%llu failures total); "
            "exact sequence predicates remain authoritative",
            static_cast<unsigned long long>(signalFailures));
        reportedSignalFailures = signalFailures;
        lastSignalFailureReportMs = workerNowMs;
    }

    const bool pipelineFaulted =
        g_waitPipelineFaulted.load(std::memory_order_acquire);
    if (pipelineFaulted && !pipelineFaultReported)
    {
        LOG("pacing: exact wait-worker pipeline faulted; no render-thread "
            "xrWaitFrame will be substituted");
        pipelineFaultReported = true;
    }
    else if (!pipelineFaulted)
    {
        pipelineFaultReported = false;
    }

    if constexpr (!kEnableFramePacingTransitionCapture)
        return;

    FramePacingWorkerState& worker = g_framePacingWorker;
    FramePacingRecord record{};
    while (ConsumeFramePacingRecord(record))
    {
        if (worker.capturing)
        {
            const FramePacingRecord& previous =
                worker.capture[worker.captureCount - 1];
            if (record.sessionEpoch != previous.sessionEpoch ||
                !IsEligiblePacingRecord(record))
            {
                FinishPacingCapture(worker, "eligibility-ended");
            }
            else
            {
                if (record.serial != previous.serial + 1 ||
                    !IsExactWaitPair(previous, record))
                {
                    worker.exact = false;
                }
                if (worker.captureCount < worker.capture.size())
                    worker.capture[worker.captureCount++] = record;
                else
                    worker.exact = false;
                worker.lastDataMs = GetTickCount64();
                if (worker.postRemaining)
                    --worker.postRemaining;
                if (!worker.postRemaining ||
                    worker.captureCount == worker.capture.size())
                {
                    FinishPacingCapture(worker, "post-roll-complete");
                }
                continue;
            }
        }

        if (!IsEligiblePacingRecord(record))
        {
            ClearPacingHistory(worker);
            continue;
        }

        const FramePacingRecord* previous = LastPacingHistory(worker);
        if (previous &&
            (previous->sessionEpoch != record.sessionEpoch ||
             previous->serial + 1 != record.serial))
        {
            ClearPacingHistory(worker);
            previous = nullptr;
        }
        if (previous && IsMaterialFramePeriodTransition(
                static_cast<uint64_t>(previous->predictedDisplayPeriodNs),
                static_cast<uint64_t>(record.predictedDisplayPeriodNs)))
        {
            BeginPacingCapture(
                worker, record, previous->predictedDisplayPeriodNs);
        }
        else
        {
            AddPacingHistory(worker, record);
        }
    }

    if (worker.capturing && worker.lastDataMs &&
        GetTickCount64() - worker.lastDataMs >= 3000)
    {
        worker.exact = false;
        FinishPacingCapture(worker, "post-roll-timeout");
    }
}

void VR_NotifyCameraTransform()
{
    const uint64_t serial =
        g_preparedSerialPublished.load(std::memory_order_acquire);
    if (!serial)
        return;
    uint64_t observed =
        g_cameraSerialObserved.load(std::memory_order_relaxed);
    if (observed == serial ||
        !g_cameraSerialObserved.compare_exchange_strong(
            observed, serial, std::memory_order_acq_rel))
        return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const uint64_t prepared =
        g_prepareQpcPublished.load(std::memory_order_acquire);
    if (prepared && static_cast<uint64_t>(now.QuadPart) >= prepared)
    {
        const double delayMs =
            QpcMs(now.QuadPart - static_cast<LONGLONG>(prepared));
        g_firstCameraDelayUs.store(
            static_cast<uint64_t>(delayMs * 1000.0),
            std::memory_order_relaxed);
    }
}

bool VR_OnResizeBuffers(IDXGISwapChain* sc)
{
    HaloCE_ForgetPresentationTexture();
    ReleaseCeDesktopMirror();
#if HALOMCCVR_HALO2_STEREO6DOF
    VR_ResetHalo2SynchronousStereo();
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
    VR_ResetHalo2TemporalStereo();
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // Resize is rare and must exclude the worker while DXGI owns the buffer
    // transition. VR_AfterResizeBuffers releases this lock after the real call.
    AcquireSRWLockExclusive(&g_reachDisplayResourceLock);
    g_reachResizeActive.store(true, std::memory_order_release);
    InvalidateReachPresentAdmission();
    DiscardReachDisplaySnapshots(true);
#endif
    // The game is about to destroy its backbuffer; anything of ours that
    // references it must go first or the resize fails. The tracked history
    // targets are resolution-dependent too — drop and re-learn them.
    ReleaseSourceViews();
    ReleaseTheaterProjectionResources();
    // DLSS holds references to the game's depth views and eye-shaped
    // textures; they go with the scene target they describe.
    ReleaseDlssResources();
    if (g_sceneColorRtv)
    {
        g_sceneColorRtv->Release();
        g_sceneColorRtv = nullptr;
    }
    // Halo 4 must re-learn its scene target from scratch for the next level:
    // the retained candidate pointers belong to resources that are going away.
    Halo4ResetSceneTargetDiscovery();
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    if (g_sceneColorResource)
    {
        g_sceneColorResource->Release();
        g_sceneColorResource = nullptr;
    }
#endif
    g_gameBackbufferDesc = {};
    g_gameBackbufferDescValid = false;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // Buffer 0 and the engine's record-0 views are invalid at this edge. A
    // copied readiness token must fail immediately even if COM reuses every
    // pointer value after ResizeBuffers.
    // Reach retains buffer 0 in its capture lease. Release that graph before
    // DXGI resizes the active chain, after the existing bounded reader drain.
    const bool activeReachSwapchain = sc &&
        (g_reachCaptureProof.continuity.swapchainIdentity ==
            reinterpret_cast<uintptr_t>(sc) ||
         (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
          ReachPresentMatchesEngineSwapchain(g_reachDisplayEpoch, sc)));
    const bool resourcesReleased =
        ResetReachDisplayCandidateLocked(false, activeReachSwapchain);
#endif
    if (ID3D11Texture2D* retained =
            g_nextGameBackbuffer.exchange(nullptr, std::memory_order_acq_rel))
        retained->Release();
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    return resourcesReleased;
#else
    return true;
#endif
}

void VR_AfterResizeBuffers(IDXGISwapChain*)
{
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    g_reachPresentNextSnapshotMs.store(0, std::memory_order_relaxed);
    g_reachResizeActive.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_reachDisplayResourceLock);
#endif
}

void VR_RequestRecenter()
{
    VR_RecalibratePhysicalCrouch();
    g_recenterRequested.store(true, std::memory_order_release);
}

void VR_RequestPausePresentation(bool paused)
{
    const bool previous = g_pauseTarget.exchange(paused);
    if (previous != paused || g_pausePresentation.load() != paused)
        g_pauseRequest = paused ? 1 : 0;
}

bool VR_IsPausePresentation()
{
    return g_pausePresentation.load();
}

bool VR_IsPausePresentationTarget()
{
    return g_pauseTarget.load();
}

void VR_ToggleScreenFollow()
{
    const bool on = !g_screenFollow.load();
    g_screenFollow = on;
    LOG("screen-follow %s", on ? "on (screen follows head)" : "off (screen world-locked)");
}

void VR_ToggleStereo()
{
    const bool on = !g_stereoEnabled.load();
    g_stereoEnabled = on;
    if (on && !Game_IsStereoGeometryOnlyBringup() &&
        !Game_UsesTitleOwnedHeadTracking())
        Game_ForcePositional();
    g_renderEye = 0;
    g_eyeHasImage[0] = g_eyeHasImage[1] = false;
#if HALOMCCVR_HALO2_STEREO6DOF
    VR_ResetHalo2SynchronousStereo();
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
    VR_ResetHalo2TemporalStereo();
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    g_halo4EyeSerial[0].store(0, std::memory_order_release);
    g_halo4EyeSerial[1].store(0, std::memory_order_release);
#endif
    Game_SetStereoEye(on ? 0 : -1);
    if (on && Game_IsStereoGeometryOnlyBringup())
        LOG("Halo 2 C-H2-2 temporal stereo presentation ON (stock head pose; "
            "one render per frame, adjacent-eye pair)");
#if HALOMCCVR_HALO2_STEREO6DOF
    else if (on && TitleAdapter_GetActiveTitle()==GameTitle::HaloCE)
        LOG("CE Original/Anniversary stereo + positional 6DoF presentation ON");
    else if (on && Game_UsesTitleOwnedHeadTracking())
        LOG("Halo 2 C-H2-6 synchronous stereo+6DOF presentation ON "
            "(title-owned headset pose; exact-current-serial eye pair)");
#endif
    else
        LOG("M2 alternate-eye stereo %s%s", on ? "ON" : "OFF",
            on && !Game_IsHeadTracking()
                ? " (enable head tracking with F2)" : "");
}

bool VR_IsStereoEnabled()
{
    return g_stereoEnabled.load();
}

bool VR_IsCutsceneTheaterActive()
{
    return g_cutsceneTheaterActive.load(std::memory_order_acquire);
}

bool VR_ShouldRenderPreparedFrame()
{
    return g_preparedShouldRender.load(std::memory_order_acquire);
}

float VR_HeadsetRefreshHz()
{
    const uint64_t period = g_displayPeriodNs.load(std::memory_order_relaxed);
    return period ? (float)(1000000000.0 / (double)period) : 0.0f;
}

bool VR_IsFramePacingOwned()
{
    // True once the runtime is driving our cadence through xrWaitFrame. Until
    // then the desktop present must keep the game's own timing untouched.
    return g_state == State::Ready && g_sessionRunning &&
           g_displayPeriodNs.load(std::memory_order_relaxed) != 0;
}

void VR_DetachGamePresentation()
{
    // Game_AutoVrTick calls this from Present, after Halo has stopped issuing
    // camera renders and before this frame is submitted to OpenXR. Do not tear
    // down the session or shared MCC D3D hooks: the flat shell still needs
    // them. Only disarm Halo's per-eye work and release its retained render
    // target so a different MCC engine can own the shared device cleanly.
    g_cutsceneTheaterActive.store(false, std::memory_order_release);
    g_cutsceneTheaterTransition.Reset(false);
    g_cutsceneTheaterWasQualified = false;
    g_cutsceneTheaterEntryBlocked = false;
    g_cutsceneTheaterAuthoredAspect = 0.0f;
    g_authoredReticleReady = false;
    g_authoredReticleSerial = 0;
    g_authoredReticleUploadedSerial = 0;
    g_reticleContainsAuthored = false;
    // Known-good art belongs to the title that captured it; a new title must
    // measure its own before anything is held on its behalf.
    g_authoredReticleGoodValid = false;
    g_authoredReticleProbePending = false;
    g_authoredReticleHeldBlank = false;
    g_authoredReticleGoodInk = 0;
    g_authoredReticleConsecutiveHolds = 0;
    g_reticlePaintedOpacity = -1.0f;
    Game_ResetAuthoredCrosshairKey();
    g_reticleOwnerEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (g_stereoEnabled.load())
        VR_ToggleStereo();
    else
        Game_SetStereoEye(-1);
    g_renderEye = 0;
    g_eyeHasImage[0] = g_eyeHasImage[1] = false;
#if HALOMCCVR_HALO2_STEREO6DOF
    VR_ResetHalo2SynchronousStereo();
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
    VR_ResetHalo2TemporalStereo();
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    g_halo4EyeSerial[0].store(0, std::memory_order_release);
    g_halo4EyeSerial[1].store(0, std::memory_order_release);
#endif
    g_stereoValidationDone = false;
    g_rasterOwnerThread.store(0, std::memory_order_release);
    g_rasterEye = -1;
    g_rasterRedirected[0] = g_rasterRedirected[1] = false;
    g_rasterScope = false;
    g_scopeRedirected = false;
    g_scopeActive = false;
    g_scopeHasImage = false;
    g_scopeResetRequested = true;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // The worker owns gate/resource mutation. Present only revokes the
    // admission stamp so a proof racing this detach cannot publish.
    InvalidateReachPresentAdmission();
#endif
    HaloCE_ForgetPresentationTexture();
    ReleaseCeDesktopMirror();
    ReleaseSourceViews();
    ReleaseIqChain();
    ReleaseIqTimer();
    ReleaseTheaterProjectionResources();
    // DLSS holds references to the game's depth views and eye-shaped
    // textures; they go with the scene target they describe.
    ReleaseDlssResources();
    if (g_sceneColorRtv)
    {
        g_sceneColorRtv->Release();
        g_sceneColorRtv = nullptr;
    }
    // Halo 4 must re-learn its scene target from scratch for the next level:
    // the retained candidate pointers belong to resources that are going away.
    Halo4ResetSceneTargetDiscovery();
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    if (g_sceneColorResource)
    {
        g_sceneColorResource->Release();
        g_sceneColorResource = nullptr;
    }
#endif
}

bool VR_CaptureRenderedEye(int eye)
{
    if (eye < 0 || eye > 1)
        return false;
    if (g_rasterRedirected[eye] && g_eyeCache[eye])
    {
        g_eyeHasImage[eye] = true;
        return true;
    }
    static bool loggedMissing = false;
    if (!loggedMissing)
    {
        LOG("M2 RASTER: no internal scene-color RTV redirect occurred; refusing fake eye copy");
        loggedMissing = true;
    }
    return false;
}

void VR_PublishEyeCamera(int eye, const VrEyeCameraSample& sample)
{
    if (eye < 0 || eye > 1)
        return;
    DlssEyeState& state = g_dlssEye[eye];
    if (!VR_DlssWantsEyeCamera())
    {
        state.published = false;
        return;
    }
    state.current = dlss::MakeCameraSample(
        sample.position, sample.forward, sample.up, sample.projection,
        sample.jitterNdcX, sample.jitterNdcY);
    state.published = state.current.valid;
    // One-time evidence per eye and per title: the decoded terms, or the
    // rejected rows (the evidence needed when a title's layout differs).
    static uint8_t loggedValid[2]{};
    static uint8_t loggedRejected[2]{};
    const GameTitle title = TitleAdapter_GetActiveTitle();
    const uint8_t titleBit =
        static_cast<uint8_t>(1u << (static_cast<unsigned>(title) & 7u));
    if (state.current.valid && !(loggedValid[eye] & titleBit))
    {
        loggedValid[eye] |= titleBit;
        const dlss::ProjectionTerms& t = state.current.projection;
        LOG("DLSS: %s eye %d camera published%s: tan %.5f/%.5f centre %.6f/%.6f "
            "depth A=%.7f B=%.5f (%s Z), jitter ndc %.6f/%.6f, "
            "pos (%.2f, %.2f, %.2f) fwd (%.3f, %.3f, %.3f) up (%.3f, %.3f, %.3f)",
            DlssTitleName(title), eye,
            t.transposed ? " (column-vector projection, transposed)" : "",
            t.tanX, t.tanY, t.centerX, t.centerY, t.depthA, t.depthB,
            t.depthInverted ? "reversed" : "standard",
            sample.jitterNdcX, sample.jitterNdcY,
            sample.position[0], sample.position[1], sample.position[2],
            sample.forward[0], sample.forward[1], sample.forward[2],
            sample.up[0], sample.up[1], sample.up[2]);
    }
    else if (!state.current.valid && !(loggedRejected[eye] & titleBit))
    {
        loggedRejected[eye] |= titleBit;
        const float* p = sample.projection;
        LOG("DLSS: %s eye %d camera sample REJECTED; projection rows "
            "[%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f] "
            "[%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f], "
            "pos (%.2f, %.2f, %.2f) fwd (%.3f, %.3f, %.3f) up (%.3f, %.3f, %.3f)",
            DlssTitleName(title), eye,
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15],
            sample.position[0], sample.position[1], sample.position[2],
            sample.forward[0], sample.forward[1], sample.forward[2],
            sample.up[0], sample.up[1], sample.up[2]);
    }
}

bool VR_DlssWantsEyeCamera()
{
    if (g_config.upscaler != dlss::kUpscalerDlss ||
        !dlss::TitleHasCameraContract(TitleAdapter_GetActiveTitle()))
        return false;
    unsigned rw = 0, rh = 0, ow = 0, oh = 0;
    bool plannedDlss = false, runtimePresent = false;
    D3D_GetRenderPlan(rw, rh, ow, oh, plannedDlss, runtimePresent);
    return runtimePresent;
}

void VR_DlssBeginEyeDepthWindow(int eye)
{
    if (eye >= 0 && eye < 2)
        g_dlssKicked[eye].Reset();
    if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera())
    {
        g_dlssDepthWindowEye.store(-1, std::memory_order_relaxed);
        D3D_SetEyeSamplerBias(0.0f);
        return;
    }
    // Same contract as VR_BeginRasterEye: this eye's camera and depth must
    // both arrive during THIS render.
    g_dlssEye[eye].published = false;
    g_dlssEye[eye].depthValid = false;
    g_dlssEyeSceneDepthSeen = false;
    g_dlssEyeLastFullRasterDsv = nullptr;
    g_dlssDepthWindowEye.store(eye, std::memory_order_relaxed);
    DlssBeginEyeSamplerBias();
}

void VR_DlssEndEyeDepthWindow(int eye)
{
    if (g_dlssDepthWindowEye.load(std::memory_order_relaxed) != eye)
        return;
    g_dlssDepthWindowEye.store(-1, std::memory_order_relaxed);
    DlssEndEyeSamplerBias();
    DlssCaptureEyeDepth(eye);
}

bool VR_GetEyeJitter(int eye, float& outNdcX, float& outNdcY)
{
    outNdcX = 0.0f;
    outNdcY = 0.0f;
    if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera() ||
        !g_config.dlss_jitter || !g_dlssJitterArmed || !Dlss_IsAvailable() ||
        !dlss::UsesWorldRender(DlssPresentationMode()) ||
        !g_gameBackbufferDescValid || !g_stereoW || !g_stereoH)
        return false;
    const uint32_t renderW = g_gameBackbufferDesc.Width;
    const uint32_t renderH = g_gameBackbufferDesc.Height;
    const dlss::OutputSize out = DlssOutputFor(renderW, renderH, g_stereoW, g_stereoH);
    uint32_t jitterOutputW = out.width;
    unsigned planRenderW = 0, planRenderH = 0, planOutputW = 0;
    unsigned planOutputH = 0;
    bool plannedDlss = false, runtimePresent = false;
    D3D_GetRenderPlan(planRenderW, planRenderH, planOutputW, planOutputH,
                      plannedDlss, runtimePresent);
    if (dlss::kEnableDlss21StereoBatch && plannedDlss)
    {
        const dlss::StereoBatchPlan batch = dlss::PlanStereoBatch(
            static_cast<int>(planOutputW), static_cast<int>(planOutputH),
            dlss::ModeFromConfig(g_config.dlss_mode));
        if (batch.eyeOutputW > 0)
            jitterOutputW = static_cast<uint32_t>(batch.eyeOutputW);
    }
    const uint32_t phases = dlss::JitterPhaseCount(renderW, jitterOutputW);
    float pixelX = 0.0f, pixelY = 0.0f;
    dlss::JitterPixels(g_dlssJitterPhase, phases, pixelX, pixelY);
    dlss::JitterToNdc(pixelX, pixelY, renderW, renderH, outNdcX, outNdcY);
    return true;
}

void VR_GetDlssStatus(char* out, size_t bytes)
{
    if (!out || !bytes)
        return;
    AcquireSRWLockShared(&g_dlssStatusLock);
    snprintf(out, bytes, "%s", g_dlssStatusText);
    ReleaseSRWLockShared(&g_dlssStatusLock);
}

bool VR_GetDlssRenderPlan(VrDlssRenderPlan& out)
{
    out = {};
    D3D_GetRenderPlan(out.plannedRenderW, out.plannedRenderH, out.outputW,
                      out.outputH, out.plannedDlss, out.runtimePresent);
    if (g_gameBackbufferDescValid)
    {
        out.liveRenderW = g_gameBackbufferDesc.Width;
        out.liveRenderH = g_gameBackbufferDesc.Height;
    }
    return out.outputW != 0 && out.outputH != 0;
}

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
bool VR_CaptureHalo4RenderedEye(int eye, uint64_t preparedSerial)
{
    if (eye < 0 || eye > 1 || !preparedSerial)
        return false;
    if (!Halo4EyeCaptureIsCurrent(
            eye, g_rasterEye.load(std::memory_order_acquire),
            g_rasterRedirected[eye], g_eyeCache[eye] != nullptr))
    {
        g_halo4EyeSerial[eye].store(0, std::memory_order_release);
        return false;
    }
    g_eyeHasImage[eye] = true;
    g_halo4EyeSerial[eye].store(preparedSerial, std::memory_order_release);
    return true;
}

void VR_InvalidateHalo4PreparedFrame()
{
    g_halo4EyeSerial[0].store(0, std::memory_order_release);
    g_halo4EyeSerial[1].store(0, std::memory_order_release);
    g_eyeHasImage[0] = false;
    g_eyeHasImage[1] = false;
}
#endif

bool VR_CaptureBackbufferEye(int eye)
{
    if (eye < 0 || eye > 1 || !g_context || !g_gameBackbufferDescValid ||
        !g_eyeCache[eye] || !g_eyeCacheRtvs[eye])
        return false;
    ID3D11Texture2D* backbuffer =
        g_nextGameBackbuffer.load(std::memory_order_acquire);
    if (!backbuffer ||
        g_eyeCacheDesc.Width != g_gameBackbufferDesc.Width ||
        g_eyeCacheDesc.Height != g_gameBackbufferDesc.Height)
        return false;
    if (!Blit(backbuffer, g_gameBackbufferDesc, g_eyeCache[eye],
              g_eyeCacheDesc.Width, g_eyeCacheDesc.Height,
              g_eyeCacheRtvs[eye]))
        return false;
    g_eyeHasImage[eye] = true;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_relaxed))
        LOG("M2 RASTER: ODST direct backbuffer death-camera capture active");
    return true;
}

void VR_TraceEvent(const char* tag, int a, int b)
{
    if constexpr (!kEnableRetiredRasterTrace)
        return;
    // Arms ~8s after first call (a level is up), then logs the next 60 events
    // and disarms forever. One burst, zero steady-state cost beyond two loads.
    static std::atomic<DWORD> firstMs{0};
    static std::atomic<int> budget{-1};
    DWORD f = firstMs.load();
    if (f == 0 && firstMs.compare_exchange_strong(f, GetTickCount())) f = firstMs.load();
    int have = budget.load();
    if (have == -1)
    {
        if (GetTickCount() - f < 8000) return;
        int expect = -1;
        if (!budget.compare_exchange_strong(expect, 60)) {}
        have = budget.load();
    }
    if (have <= 0) return;
    if (budget.fetch_sub(1) <= 0) return;
    LOG("TRACE %s a=%d b=%d rasterEye=%d redir0=%d", tag, a, b,
        g_rasterEye.load(), g_rasterRedirected[0] ? 1 : 0);
}

bool VR_IsEyeRasterActive(ID3D11DeviceContext* context) noexcept
{
    const int eye=g_rasterEye.load(std::memory_order_acquire);
    return context && context==g_context && eye>=0 && eye<2 &&
        g_rasterOwnerThread.load(std::memory_order_acquire)==GetCurrentThreadId();
}

void VR_BeginRasterEye(int eye)
{
#if HALOMCCVR_HALO2_STEREO6DOF
    g_halo2SynchronousEyeScope = {};
#endif
    // Revoke the prior scope before any resource precondition. Otherwise a
    // transient resize/no-device return can leave yesterday's redirect flag
    // looking like a capture from this prepared frame.
    g_rasterOwnerThread.store(0, std::memory_order_release);
    g_rasterEye.store(-1, std::memory_order_release);
    if (eye >= 0 && eye < 2)
    {
        g_rasterRedirected[eye] = false;
        g_dlssKicked[eye].Reset();
    }
    if (eye < 0 || eye > 1 || !g_gameSwapchain || !g_device)
        return;
    // Eye caches are created lazily when Halo binds its final scene-color RTV.
    // That RTV's typed view format (not the swapchain resource format) controls
    // the required sRGB conversion.
    g_rasterEye.store(eye, std::memory_order_release);
    g_rasterOwnerThread.store(GetCurrentThreadId(), std::memory_order_release);
    g_eyeGpuTiming.Begin(g_context, VR_DlssWantsEyeCamera(),
        g_gameBackbufferDesc.Width, g_gameBackbufferDesc.Height);
    DlssBeginEyeSamplerBias();
    // DLSS: this eye's camera and depth must both arrive during THIS render.
    g_dlssEye[eye].published = false;
    g_dlssEye[eye].depthValid = false;
    g_dlssEyeSceneDepthSeen = false;
    g_dlssEyeLastFullRasterDsv = nullptr;
#if HALOMCCVR_HALO2_STEREO6DOF
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        const uint64_t token =
            g_halo2SynchronousPairToken.load(std::memory_order_acquire);
        if (Halo2SynchronousSerialIsReserved(token))
        {
            const uint64_t serial =
                token & ~kHalo2SynchronousReservationBit;
            const uint32_t generation =
                g_halo2SynchronousPairGeneration.load(
                    std::memory_order_relaxed);
            const uint32_t resourceEpoch =
                g_halo2SynchronousPairResourceEpoch.load(
                    std::memory_order_relaxed);
            ID3D11RenderTargetView* const finalRtv =
                g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed);
            if (finalRtv &&
                g_halo2SynchronousTargetSerial.load(
                    std::memory_order_acquire) == serial &&
                Halo2SynchronousContextCurrent(
                    generation, serial, resourceEpoch) &&
                g_halo2SynchronousPairToken.load(
                    std::memory_order_acquire) == token)
            {
                Halo2SynchronousEyeScope& scope =
                    g_halo2SynchronousEyeScope;
                scope.active = true;
                scope.generation = generation;
                scope.resourceEpoch = resourceEpoch;
                scope.preparedSerial = serial;
                scope.eye = eye;
                scope.finalRtv = finalRtv;
                scope.sceneRtv =
                    g_halo2SynchronousSceneRtv.load(std::memory_order_relaxed);
                scope.lastBoundRtv = nullptr;
                scope.binds = 0;
            }
        }
    }
#endif
    if constexpr (kEnableRetiredRasterTrace)
        VR_TraceEvent("eye-begin", eye, 0);
}


// Decides Halo 4's scene target at the END of an eye, when the whole bind
// sequence has been seen. Called only from VR_EndRasterEye.
static void Halo4ResolveSceneTargetAtEyeEnd()
{
    if (g_sceneColorRtv)
        return;
    ID3D11RenderTargetView* const candidate = g_h4LastCandidate;
    const unsigned seen = g_h4CandidatesThisEye;
    g_h4LastCandidate = nullptr;
    g_h4CandidatesThisEye = 0;
    if (!candidate)
        return;
    ++g_h4LearningEyes;
    if (candidate != g_h4PreviousEyeCandidate)
    {
        // First sighting, or the sequence is still settling. Report the shape
        // so an unstable pipeline is visible rather than silent.
        g_h4PreviousEyeCandidate = candidate;
        LOG("SCENEPROBE: Halo 4 learning eye %u ended; %u qualifying "
            "full-size colour target(s) bound, last = RTV %p (%ux%u fmt=%u "
            "viewfmt=%u). Waiting for a second eye to agree before latching",
            g_h4LearningEyes, seen, candidate,
            g_h4LastCandidateDesc.Width, g_h4LastCandidateDesc.Height,
            static_cast<UINT>(g_h4LastCandidateDesc.Format),
            static_cast<UINT>(g_h4LastCandidateViewFormat));
        return;
    }

    D3D11_TEXTURE2D_DESC eyeDesc = g_h4LastCandidateDesc;
    eyeDesc.Format = g_h4LastCandidateViewFormat;
    if (!EnsureEyeCaches(eyeDesc))
    {
        LOG("SCENEPROBE: Halo 4 scene target RTV %p agreed twice but the eye "
            "caches could not be created for viewfmt=%u; not latching",
            candidate, static_cast<UINT>(g_h4LastCandidateViewFormat));
        return;
    }
    candidate->AddRef();
    g_sceneColorRtv = candidate;
    LOG("M2 RASTER: learned Halo 4 scene-color RTV %p as the LAST qualifying "
        "target of the eye (%ux%u fmt=%u viewfmt=%u), agreed by two "
        "consecutive eyes after %u learning eye(s); steady-state redirect is "
        "pointer-only",
        g_sceneColorRtv, g_h4LastCandidateDesc.Width,
        g_h4LastCandidateDesc.Height,
        static_cast<UINT>(g_h4LastCandidateDesc.Format),
        static_cast<UINT>(g_h4LastCandidateViewFormat), g_h4LearningEyes);
}

#if HALOMCCVR_HALO2_STEREO6DOF
// Copies the engine's finished Halo 2 frame for one eye into that eye's cache.
// Called on the render thread from VR_EndRasterEye, i.e. after render_view has
// returned and every draw of this eye (world, resolve, postprocess, final
// output) has been issued. Resolves the texture behind the proven final-output
// RTV each call and holds NO reference to it: a held reference would pin the
// swapchain buffer and make the game's own ResizeBuffers fail.
// Resolves the single-sample 2D texture behind an RTV, or null. Holds no
// reference beyond the returned one; the caller releases it.
static ID3D11Texture2D* Halo2TextureBehind(
    ID3D11RenderTargetView* rtv, D3D11_TEXTURE2D_DESC& desc)
{
    if (!rtv)
        return nullptr;
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource)
    {
        (void)resource->QueryInterface(
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        resource->Release();
    }
    if (!texture)
        return nullptr;
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || !desc.Width ||
        !desc.Height)
    {
        texture->Release();
        return nullptr;
    }
    return texture;
}

static bool Halo2EnsureProbeCaches(const D3D11_TEXTURE2D_DESC& source)
{
    if (g_halo2ProbeCache[0][0] && g_halo2ProbeCache[1][1] &&
        g_halo2ProbeDesc.Width == source.Width &&
        g_halo2ProbeDesc.Height == source.Height &&
        g_halo2ProbeDesc.Format == source.Format)
    {
        return true;
    }
    for (auto& pair : g_halo2ProbeCache)
        for (auto*& texture : pair)
        {
            if (texture) texture->Release();
            texture = nullptr;
        }
    D3D11_TEXTURE2D_DESC desc = source;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    for (auto& pair : g_halo2ProbeCache)
        for (auto*& texture : pair)
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &texture)))
                return false;
    g_halo2ProbeDesc = desc;
    std::memset(g_halo2ProbeHasImage, 0, sizeof(g_halo2ProbeHasImage));
    return true;
}

static bool Halo2CopyFinishedEyeFrame(const Halo2SynchronousEyeScope& scope)
{
    static bool loggedShape = false;
    static bool loggedFailure = false;
    static uint32_t censusGeneration = 0;
    if (!g_context || !g_device || !scope.finalRtv ||
        scope.eye < 0 || scope.eye > 1)
    {
        return false;
    }
    // The three candidates for "the finished eye": the final-output slot,
    // the primary scene slot, and the last target the engine bound during
    // this eye. Which one carries per-eye content is LEARNED from the
    // readback comparison, never assumed (E-H2-8).
    // E-H2-24: the two named slots first (their indices are stable across
    // builds and logs), then every distinct target the engine bound inside
    // this eye that is not already one of them.
    ID3D11RenderTargetView* candidates[kHalo2CaptureCandidateSlots] = {
        scope.finalRtv, scope.sceneRtv};
    int candidateCount = 2;
    for (int i = 0; i < scope.boundCount &&
         candidateCount < kHalo2CaptureCandidateSlots; ++i)
    {
        bool duplicate = false;
        for (int j = 0; j < candidateCount && !duplicate; ++j)
            duplicate = candidates[j] == scope.bound[i];
        if (!duplicate && scope.bound[i])
            candidates[candidateCount++] = scope.bound[i];
    }
    g_halo2CaptureCandidateCount.store(candidateCount, std::memory_order_relaxed);
    int source = g_halo2CaptureSource.load(std::memory_order_relaxed);
    if (source < 0 || source >= candidateCount || !candidates[source])
    {
        const uint64_t n =
            g_halo2CaptureSourceFallbacks.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n % 600) == 0)
        {
            LOG("Halo 2 eye capture: learned source #%d is not available in "
                "this eye (%d candidates); the final-output slot is used and "
                "re-learned from here (fallback #%llu)",
                source, candidateCount, static_cast<unsigned long long>(n));
        }
        source = 0;
        g_halo2CaptureSource.store(0, std::memory_order_relaxed);
        g_halo2ProbeRotation.store(0, std::memory_order_relaxed);
        g_halo2HeavyValidationSource.store(0, std::memory_order_relaxed);
        g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
        g_halo2HeavyValidationRetired.store(false, std::memory_order_release);
    }
    if (censusGeneration != scope.generation)
    {
        censusGeneration = scope.generation;
        LOG("Halo 2 eye capture: %d distinct targets were bound inside this "
            "eye; %d capture candidates (final slot, scene slot, then the "
            "bound targets)", scope.boundCount, candidateCount);
        for (int i = 0; i < scope.boundCount; ++i)
        {
            D3D11_TEXTURE2D_DESC boundDesc{};
            ID3D11Texture2D* boundTexture =
                Halo2TextureBehind(scope.bound[i], boundDesc);
            LOG("Halo 2 eye capture: bound target #%d = RTV %p -> tex %p "
                "%ux%u fmt %u samples %u%s%s", i, scope.bound[i],
                boundTexture, boundDesc.Width, boundDesc.Height,
                static_cast<unsigned>(boundDesc.Format),
                boundDesc.SampleDesc.Count,
                scope.bound[i] == scope.finalRtv ? " (= final slot)" : "",
                scope.bound[i] == scope.sceneRtv ? " (= scene slot)" : "");
            if (boundTexture)
                boundTexture->Release();
        }
        uintptr_t slots[3] = {
            reinterpret_cast<uintptr_t>(scope.finalRtv),
            reinterpret_cast<uintptr_t>(scope.sceneRtv), 0};
        VR_Halo2LogTargetCensusOnce("eye end, classic/anniversary capture",
                                    slots, static_cast<uint32_t>(scope.eye),
                                    scope.binds);
        LOG("Halo 2 eye capture: last target bound inside this eye = %p "
            "(%u binds); final slot %p; scene slot %p; current source %d",
            scope.lastBoundRtv, scope.binds, scope.finalRtv, scope.sceneRtv,
            source);
    }
    D3D11_TEXTURE2D_DESC desc{};
    ID3D11Texture2D* texture = Halo2TextureBehind(candidates[source], desc);
    if (!texture)
    {
        if (!loggedFailure)
        {
            loggedFailure = true;
            LOG("Halo 2 eye capture FAILED: capture source %d RTV %p is not "
                "backed by a single-sample 2D texture; no eye image is "
                "published", source, candidates[source]);
        }
        return false;
    }
    // Probe copies of the other two candidates, for the comparison that
    // decides the source. Same shape only; a differently shaped candidate
    // cannot be the finished eye.
    if (!g_halo2HeavyValidationRetired.load(std::memory_order_acquire) &&
        Halo2EnsureProbeCaches(desc))
    {
        const int rotation = g_halo2ProbeRotation.load(std::memory_order_relaxed);
        for (int slot = 0; slot < 2; ++slot)
        {
            const int candidate = Halo2ProbeCandidateForSlot(
                rotation, slot, source, candidateCount);
            g_halo2ProbeCandidate[slot][scope.eye].store(
                candidate, std::memory_order_relaxed);
            g_halo2ProbeCandidatePtr[slot][scope.eye].store(
                candidate >= 0
                    ? reinterpret_cast<uintptr_t>(candidates[candidate])
                    : 0,
                std::memory_order_relaxed);
            if (candidate < 0)
            {
                g_halo2ProbeHasImage[slot][scope.eye] = false;
                g_halo2ProbeBoxHasImage[slot][scope.eye] = false;
                continue;
            }
            g_halo2ProbeHasImage[slot][scope.eye] = false;
            g_halo2ProbeBoxHasImage[slot][scope.eye] = false;
            D3D11_TEXTURE2D_DESC otherDesc{};
            ID3D11Texture2D* other =
                Halo2TextureBehind(candidates[candidate], otherDesc);
            if (!other)
                continue;
            if (otherDesc.Width == desc.Width && otherDesc.Height == desc.Height &&
                otherDesc.Format == desc.Format && other != texture)
            {
                g_context->CopyResource(g_halo2ProbeCache[slot][scope.eye], other);
                g_halo2ProbeHasImage[slot][scope.eye] = true;
            }
            // E-H2-27: shape-agnostic centre box, for candidates the
            // same-shape copy above cannot take (the engine's scene target).
            if (otherDesc.Width >= kHalo2ProbeBoxSize &&
                otherDesc.Height >= kHalo2ProbeBoxSize &&
                otherDesc.SampleDesc.Count == 1)
            {
                ID3D11Texture2D*& box = g_halo2ProbeBox[slot][scope.eye];
                if (box && g_halo2ProbeBoxFormat[slot][scope.eye] != otherDesc.Format)
                {
                    box->Release();
                    box = nullptr;
                }
                if (!box)
                {
                    D3D11_TEXTURE2D_DESC boxDesc{};
                    boxDesc.Width = kHalo2ProbeBoxSize;
                    boxDesc.Height = kHalo2ProbeBoxSize;
                    boxDesc.MipLevels = 1;
                    boxDesc.ArraySize = 1;
                    boxDesc.Format = otherDesc.Format;
                    boxDesc.SampleDesc.Count = 1;
                    boxDesc.Usage = D3D11_USAGE_STAGING;
                    boxDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    if (SUCCEEDED(g_device->CreateTexture2D(&boxDesc, nullptr, &box)))
                        g_halo2ProbeBoxFormat[slot][scope.eye] = otherDesc.Format;
                    else
                        box = nullptr;
                }
                if (box)
                {
                    D3D11_BOX region{};
                    region.left = (otherDesc.Width - kHalo2ProbeBoxSize) / 2;
                    region.top = (otherDesc.Height - kHalo2ProbeBoxSize) / 2;
                    region.right = region.left + kHalo2ProbeBoxSize;
                    region.bottom = region.top + kHalo2ProbeBoxSize;
                    region.front = 0;
                    region.back = 1;
                    g_context->CopySubresourceRegion(
                        box, 0, 0, 0, 0, other, 0, &region);
                    g_halo2ProbeBoxHasImage[slot][scope.eye] = true;
                }
            }
            other->Release();
        }
    }
    // E-H2-27: the eye caches keep the FINAL slot's shape, so learning a
    // differently shaped source (the engine's scene target) cannot churn every
    // Halo 2 resource per eye; the blit resizes and resolves the format.
    D3D11_TEXTURE2D_DESC cacheDesc = desc;
    if (source != 0 && candidates[0])
    {
        D3D11_TEXTURE2D_DESC finalDesc{};
        ID3D11Texture2D* finalTexture = Halo2TextureBehind(candidates[0], finalDesc);
        if (finalTexture)
        {
            cacheDesc = finalDesc;
            finalTexture->Release();
        }
    }
    if (Halo2FormatIsTypeless(static_cast<uint32_t>(cacheDesc.Format)))
    {
        cacheDesc.Format = static_cast<DXGI_FORMAT>(
            Halo2ConcreteFormat(static_cast<uint32_t>(cacheDesc.Format)));
    }
    bool copied = false;
    if (EnsureEyeCaches(cacheDesc) && g_eyeCache[scope.eye])
    {
        if (desc.Width == g_eyeCacheDesc.Width &&
            desc.Height == g_eyeCacheDesc.Height &&
            desc.Format == g_eyeCacheDesc.Format)
        {
            g_context->CopyResource(g_eyeCache[scope.eye], texture);
            copied = true;
        }
        else
        {
            copied = Blit(texture, desc, g_eyeCache[scope.eye],
                          g_eyeCacheDesc.Width, g_eyeCacheDesc.Height,
                          g_eyeCacheRtvs[scope.eye]);
            static bool loggedBlit = false;
            if (copied && !loggedBlit)
            {
                loggedBlit = true;
                LOG("Halo 2 eye capture: the learned source is %ux%u fmt %u "
                    "and the eye cache is %ux%u fmt %u, so the finished eye is "
                    "blitted rather than copied",
                    desc.Width, desc.Height,
                    static_cast<unsigned>(desc.Format),
                    g_eyeCacheDesc.Width, g_eyeCacheDesc.Height,
                    static_cast<unsigned>(g_eyeCacheDesc.Format));
            }
        }
        if (copied && !loggedShape)
        {
            loggedShape = true;
            LOG("Halo 2 eye capture: copying the engine's finished %ux%u "
                "fmt %u frame into eye cache %d after render_view returns; "
                "no render target is redirected",
                desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                scope.eye);
        }
    }
    else if (!loggedFailure)
    {
        loggedFailure = true;
        LOG("Halo 2 eye capture FAILED: eye caches for %ux%u fmt %u could "
            "not be created",
            desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
    }
    if(copied)
    {
        // Transfer the reference already acquired by the eye copy. No extra
        // COM discovery/AddRef is performed by the optional scope callback.
        if(g_halo2ScopeSource)g_halo2ScopeSource->Release();
        g_halo2ScopeSource=texture;g_halo2ScopeSourceRtv=candidates[source];
        g_halo2ScopeSourceDesc=desc;g_halo2ScopeGeneration=scope.generation;
        g_halo2ScopeSerial=scope.preparedSerial;
    }
    else texture->Release();
    return copied;
}
#endif

#if HALOMCCVR_HALO2_STEREO6DOF
void VR_Halo2RequestEyeDump(uint32_t delayMs)
{
    g_halo2EyeDumpDueMs.store(GetTickCount64() + delayMs, std::memory_order_release);
}

void VR_Halo2NoteDraw()
{
    const int eye = g_rasterEye.load(std::memory_order_relaxed);
    if (eye == 0 || eye == 1)
        g_halo2DrawsInsideEye[eye].fetch_add(1, std::memory_order_relaxed);
    else
        g_halo2DrawsOutsideEye.fetch_add(1, std::memory_order_relaxed);
}
#endif

static void Halo2DescribeRtv(
    ID3D11RenderTargetView* rtv, char* out, size_t outBytes)
{
    if (!rtv)
    {
        _snprintf_s(out, outBytes, _TRUNCATE, "null");
        return;
    }
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource)
    {
        (void)resource->QueryInterface(
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        resource->Release();
    }
    if (!texture)
    {
        _snprintf_s(out, outBytes, _TRUNCATE, "%p (not a 2D texture)", rtv);
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    _snprintf_s(out, outBytes, _TRUNCATE, "%p tex %p %ux%u fmt %u samples %u",
                rtv, texture, desc.Width, desc.Height,
                static_cast<unsigned>(desc.Format), desc.SampleDesc.Count);
    texture->Release();
}

void VR_Halo2LogTargetCensusOnce(
    const char* tag, const uintptr_t slots[3], uint32_t viewIndex,
    uint32_t viewMask)
{
    if (!g_context || !slots)
        return;
    char described[4][160]{};
    for (int index = 0; index < 3; ++index)
    {
        Halo2DescribeRtv(
            reinterpret_cast<ID3D11RenderTargetView*>(slots[index]),
            described[index], sizeof(described[index]));
    }
    ID3D11RenderTargetView* bound[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth = nullptr;
    g_context->OMGetRenderTargets(
        D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, bound, &depth);
    Halo2DescribeRtv(bound[0], described[3], sizeof(described[3]));
    for (auto* view : bound)
        if (view) view->Release();
    if (depth) depth->Release();
    LOG("Halo 2 target census (%s, view %u, views seen mask 0x%X): "
        "backbuffer slot 0x197EE58 = %s | primary scene slot 0x197EE60 = %s | "
        "resolved scene slot 0x197EE88 = %s | OM slot 0 bound now = %s",
        tag ? tag : "?", viewIndex, viewMask,
        described[0], described[1], described[2], described[3]);
}

void VR_EndRasterEye()
{
    const int completedEye = g_rasterEye.load(std::memory_order_relaxed);
    const bool halo4CompletedEye =
        (completedEye == 0 || completedEye == 1) &&
        TitleAdapter_GetActiveTitle() == GameTitle::Halo4;
    // Stage3BU deliberately writes back only after the scene target was
    // already proven before this eye began.  The eye that first latches the
    // target remains a learning eye; treating it as steady state would copy an
    // eye cache that was not produced by the redirect transaction.
    const bool halo4HadSceneTarget =
        halo4CompletedEye && g_sceneColorRtv != nullptr;
    if constexpr (kEnableRetiredRasterTrace)
        VR_TraceEvent("eye-end", completedEye, 0);
    if (halo4CompletedEye)
        Halo4ResolveSceneTargetAtEyeEnd();

    // Retained Stage3BU behavior: once the real Halo 4 scene target is known,
    // put the completed per-eye image back into the texture behind it.  The
    // engine consumes that texture after render_view returns for the desktop
    // mirror and damage effects.  This is intentionally in addition to the VR
    // eye cache, not an alternative capture path.
    if (VrHalo4SceneWritebackEligible(
            halo4CompletedEye, completedEye, g_context != nullptr,
            halo4HadSceneTarget && g_sceneColorRtv != nullptr,
            halo4CompletedEye && g_eyeCache[completedEye] != nullptr))
    {
        ID3D11Resource* sceneResource = nullptr;
        g_sceneColorRtv->GetResource(&sceneResource);
        if (sceneResource)
        {
            g_context->CopyResource(sceneResource, g_eyeCache[completedEye]);
            sceneResource->Release();
        }
    }
#if HALOMCCVR_HALO2_STEREO6DOF
    Halo2SynchronousEyeScope& halo2Scope = g_halo2SynchronousEyeScope;
    if (halo2Scope.active && halo2Scope.eye >= 0 && halo2Scope.eye <= 1)
    {
        const uint64_t reserved =
            Halo2ReservedSynchronousSerial(halo2Scope.preparedSerial);
        if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) ==
                reserved &&
            g_halo2SynchronousPairGeneration.load(
                std::memory_order_relaxed) == halo2Scope.generation &&
            g_halo2SynchronousPairResourceEpoch.load(
                std::memory_order_relaxed) == halo2Scope.resourceEpoch)
        {
            // render_view has returned, so the engine's own pipeline for
            // this eye - world pass, resolve, postprocess, final output -
            // has all been issued into its own backbuffer. Copy that
            // finished frame. Nothing of the engine's was redirected.
            if (Halo2CopyFinishedEyeFrame(halo2Scope))
                halo2Scope.captures = 1;
            g_halo2SynchronousCaptureCount[halo2Scope.eye].store(
                halo2Scope.captures, std::memory_order_relaxed);
            g_halo2SynchronousCaptureSerial[halo2Scope.eye].store(
                halo2Scope.preparedSerial, std::memory_order_release);
        }
        halo2Scope = {};
    }
#endif
    // Promote any newly identified history, then save this eye's copies of
    // every tracked target before the other eye (or next frame) overwrites
    // them. A ping-pong pair only reveals its read side a frame after its
    // write side, so discovery stays open for a fixed window (~2 s of
    // stereo) instead of closing at the first find.
    DlssEndEyeSamplerBias();
    g_eyeGpuTiming.EndRaster(g_context);
    DlssCaptureEyeDepth(completedEye);
    DlssKickEyeAtEyeEnd(completedEye);
    // A borrowed engine depth view expires when this eye returns. A failed
    // kick must not retry at Present using the next eye's overwritten depth.
    if (completedEye >= 0 && completedEye < 2 && g_dlssEye[completedEye].depthDirect)
        g_dlssEye[completedEye].depthValid = false;
    g_eyeGpuTiming.End(g_context);
    g_rasterOwnerThread.store(0, std::memory_order_release);
    g_rasterEye = -1;
}

#if HALOMCCVR_HALO2_STEREO6DOF
// E-H2-18 HUD replay support. All three run on the Saber render thread,
// after the pair for `preparedSerial` completed on that same thread.
namespace
{
    bool Halo2HudReplayTarget(
        uint32_t generation, uint64_t preparedSerial, const char** reason,
        ID3D11Texture2D** outTexture)
    {
        if (outTexture)
            *outTexture = nullptr;
        auto fail = [&](const char* why) {
            if (reason)
                *reason = why;
            return false;
        };
        if (!g_context || !generation || !preparedSerial ||
            Halo2SynchronousSerialIsReserved(preparedSerial))
            return fail("no context or serial");
        const uint32_t resourceEpoch =
            g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
        if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) !=
                preparedSerial ||
            g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) !=
                generation ||
            !Halo2SynchronousContextCurrent(
                generation, preparedSerial, resourceEpoch))
            return fail("the pair for this serial is not the complete one");
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_halo2SynchronousEyeSerial[eye].load(std::memory_order_acquire) !=
                    preparedSerial ||
                !g_eyeHasImage[eye] || !g_eyeCache[eye])
                return fail("an eye image of this serial is missing");
        }
        ID3D11RenderTargetView* finalRtv =
            g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed);
        if (!finalRtv)
            return fail("no final-output target recorded for the pair");
        D3D11_TEXTURE2D_DESC desc{};
        ID3D11Texture2D* texture = Halo2TextureBehind(finalRtv, desc);
        if (!texture)
            return fail("the final-output target is not a 2D texture");
        D3D11_TEXTURE2D_DESC cacheDesc{};
        g_eyeCache[0]->GetDesc(&cacheDesc);
        if (cacheDesc.Width != desc.Width || cacheDesc.Height != desc.Height ||
            cacheDesc.Format != desc.Format || desc.SampleDesc.Count != 1)
        {
            texture->Release();
            return fail("the final-output target no longer matches the eye cache");
        }
        if (outTexture)
            *outTexture = texture;
        else
            texture->Release();
        return true;
    }
}

bool VR_Halo2HudReplayEligible(
    uint32_t generation, uint64_t preparedSerial, const char** reason)
{
    return Halo2HudReplayTarget(generation, preparedSerial, reason, nullptr);
}

bool VR_Halo2GetHudSourceRectangle(
    uint32_t generation, uint64_t preparedSerial,
    Halo2CameraRectangle& rectangle)
{
    if (!Halo2HudReplayTarget(
            generation, preparedSerial, nullptr, nullptr) ||
        !g_eyeCache[0])
    {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    g_eyeCache[0]->GetDesc(&desc);
    if (!desc.Width || !desc.Height ||
        desc.Width > static_cast<UINT>(INT16_MAX) ||
        desc.Height > static_cast<UINT>(INT16_MAX))
    {
        return false;
    }
    rectangle = {
        0, 0, static_cast<int16_t>(desc.Height),
        static_cast<int16_t>(desc.Width)};
    return true;
}

bool VR_Halo2RecaptureEyeFromFinalTarget(
    uint32_t generation, uint64_t preparedSerial, int eye)
{
    if (eye < 0 || eye > 1)
        return false;
    ID3D11Texture2D* texture = nullptr;
    if (!Halo2HudReplayTarget(generation, preparedSerial, nullptr, &texture))
        return false;
    g_context->CopyResource(g_eyeCache[eye], texture);
    texture->Release();
    return true;
}

bool VR_Halo2RestoreEyeToFinalTarget(
    uint32_t generation, uint64_t preparedSerial, int eye)
{
    if (eye < 0 || eye > 1)
        return false;
    ID3D11Texture2D* texture = nullptr;
    if (!Halo2HudReplayTarget(generation, preparedSerial, nullptr, &texture))
        return false;
    g_context->CopyResource(texture, g_eyeCache[eye]);
    texture->Release();
    return true;
}
#endif

#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
bool VR_BeginNativeHudEyeDraw(int eye)
{
    auto& route = g_nativeHudEyeRoute;
    if (!g_context || eye < 0 || eye > 1 || !g_eyeCacheRtvs[eye] || route.active)
        return false;

    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                  route.rtvs, &route.dsv);
    route.viewportCount =
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&route.viewportCount, route.viewports);
    route.scissorCount =
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetScissorRects(&route.scissorCount, route.scissors);
    // ODST's unique prepare callback owns slot 0 at both native CHUD boundaries:
    // it establishes target 1 immediately before secondary, then performs its
    // engine-owned transition before primary. A non-null slot-0 RTV captured
    // only at these TLS-scoped hooks is therefore the title-proven phase output,
    // even when its view pointer differs from the later scene-color view.
    if (!route.rtvs[0])
    {
        for (auto*& rtv : route.rtvs)
        {
            if (rtv) rtv->Release();
            rtv = nullptr;
        }
        if (route.dsv)
        {
            route.dsv->Release();
            route.dsv = nullptr;
        }
        route.viewportCount = 0;
        route.scissorCount = 0;
        return false;
    }

    route.phaseOutputRtv = route.rtvs[0];
    route.eye = eye;
    ID3D11RenderTargetView* routed[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        routed[i] = route.rtvs[i];
    routed[0] = g_eyeCacheRtvs[eye];
    route.bypassOmRedirect = true;
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                  routed, route.dsv);
    route.bypassOmRedirect = false;
    route.targetCopy = false;
    route.active = true;
    return true;
}

void VR_EndNativeHudEyeDraw()
{
    auto& route = g_nativeHudEyeRoute;
    if (!route.active || !g_context)
        return;

    route.active = false;
    route.targetCopy = false;
    route.bypassOmRedirect = true;
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                  route.rtvs, route.dsv);
    route.bypassOmRedirect = false;
    g_context->RSSetViewports(route.viewportCount,
                              route.viewportCount ? route.viewports : nullptr);
    g_context->RSSetScissorRects(route.scissorCount,
                                 route.scissorCount ? route.scissors : nullptr);
    for (auto*& rtv : route.rtvs)
    {
        if (rtv) rtv->Release();
        rtv = nullptr;
    }
    if (route.dsv)
    {
        route.dsv->Release();
        route.dsv = nullptr;
    }
    route.phaseOutputRtv = nullptr;
    route.viewportCount = 0;
    route.scissorCount = 0;
    route.eye = -1;
    g_nativeHudPhaseScopes.fetch_add(1, std::memory_order_release);
}

void VR_BeginNativeHudTargetCopy()
{
    auto& route = g_nativeHudEyeRoute;
    route.targetCopy = route.active;
}

void VR_EndNativeHudTargetCopy()
{
    auto& route = g_nativeHudEyeRoute;
    if (!route.targetCopy)
        return;
    route.targetCopy = false;
    g_nativeHudExactCopyScopes.fetch_add(1, std::memory_order_relaxed);
}

ID3D11Resource* VR_RedirectNativeHudCopySource(ID3D11Resource* source)
{
    const auto& route = g_nativeHudEyeRoute;
    if (!route.active || !route.targetCopy || route.eye < 0 || route.eye > 1 ||
        !g_eyeCache[route.eye] || !g_sceneColorResource ||
        source != g_sceneColorResource)
        return source;

    g_nativeHudCopySubstitutions.fetch_add(1, std::memory_order_relaxed);
    return g_eyeCache[route.eye];
}

void VR_GetNativeHudRouteStats(unsigned& completedPhaseScopes,
                               unsigned& provenOmMatches,
                               unsigned& exactCopyScopes,
                               unsigned& copySubstitutions)
{
    completedPhaseScopes =
        g_nativeHudPhaseScopes.load(std::memory_order_acquire);
    provenOmMatches = g_nativeHudProvenOmMatches.load(std::memory_order_relaxed);
    exactCopyScopes =
        g_nativeHudExactCopyScopes.load(std::memory_order_relaxed);
    copySubstitutions =
        g_nativeHudCopySubstitutions.load(std::memory_order_relaxed);
}

#endif

bool VR_ScopeShouldRenderThisFrame()
{
    const auto title=TitleAdapter_GetActiveTitle();
    const auto generation=TitleAdapter_GetGeneration(title);
    if(title!=g_scopeRequestTitle||generation!=g_scopeRequestGeneration)
    {
        g_scopeRequestTitle=title;g_scopeRequestGeneration=generation;
        VR_SetScopeActive(false);
    }
    const bool enabled=g_config.scope_enabled && !Menu_IsOpen() &&
                       Game_IsHeadTracking() && Game_HasScopeRenderer();
    if(g_scopeResetRequested.exchange(false,std::memory_order_acq_rel))
    {
        g_scopeZoomResolver.Reset();
        g_scopeToggleObserved=g_scopeToggleSerial.load(std::memory_order_acquire);
    }
    const uint64_t requested=g_scopeToggleSerial.load(std::memory_order_acquire);
    if(requested!=g_scopeToggleObserved)
    {
        g_scopeToggleObserved=requested;
        g_scopeZoomResolver.RequestToggle();
    }
    // Native Halo zoom is deliberately suppressed by the input layer: it hides
    // the VR body/viewmodel. This resolver now supplies only the delayed R3
    // toggle that keeps input and render-thread ownership race-free.
    const bool active=g_scopeZoomResolver.Update(enabled,false);
    const bool previous=g_scopeActive.exchange(active,std::memory_order_acq_rel);
    if(previous && !active)
        g_scopeHasImage.store(false,std::memory_order_release);
    const uint64_t now=GetTickCount64();
    const float deltaSeconds=active && g_scopeZoomLastMs
        ? static_cast<float>(now-g_scopeZoomLastMs)/1000.0f : 0.0f;
    g_scopeZoomLastMs=active?now:0;
    const float zoom=g_scopeZoomController.Update(
        active,g_scopeZoomStickY.load(std::memory_order_acquire),
        deltaSeconds,g_config.scope_zoom);
    g_scopeRuntimeZoom.store(zoom,std::memory_order_release);
    // A fixed workload each frame avoids alternating cheap two-eye frames and
    // expensive scope-refresh frames. Keep the old config key readable only.
    const bool render=g_scopeRefreshScheduler.Advance(active,1);
    if(render)g_scopeHasImage.store(false,std::memory_order_release);
    return render;
}

bool VR_BeginScopeRaster()
{
    if(!g_gameSwapchain || !g_sceneColorRtv || !g_scopeCache || !g_scopeCacheRtv || !g_scopeCacheSrv ||
        g_scopeCacheDesc.Width!=g_eyeCacheDesc.Width||g_scopeCacheDesc.Height!=g_eyeCacheDesc.Height||
        g_scopeCacheDesc.Format!=g_eyeCacheDesc.Format)
        return false;
    g_scopeRedirected=false;
    g_rasterScope.store(true,std::memory_order_release);
    return true;
}

void VR_CaptureScope()
{
    if(g_scopeRedirected && g_scopeCache)
    {
        g_scopeImageTitle=TitleAdapter_GetActiveTitle();
        g_scopeImageGeneration=TitleAdapter_GetGeneration(g_scopeImageTitle);
        g_scopeHasImage.store(true,std::memory_order_release);
    }
}

void VR_EndScopeRaster()
{
    g_rasterScope.store(false,std::memory_order_release);
}

bool VR_GetScopeRenderAspect(float& outAspect)
{
    if (!g_scopeCacheDesc.Width || !g_scopeCacheDesc.Height)
        return false;
    outAspect = static_cast<float>(g_scopeCacheDesc.Width) /
                static_cast<float>(g_scopeCacheDesc.Height);
    return std::isfinite(outAspect) && outAspect > 0.0f;
}

void VR_InvalidateScopeImage()
{
    g_scopeHasImage.store(false,std::memory_order_release);
}

bool VR_BeginHalo2Scope(uint32_t generation,uint64_t serial)
{
#if HALOMCCVR_HALO2_STEREO6DOF
    if(!g_context||!g_scopeCache||!g_halo2ScopeSource||!generation||!serial||
        generation!=g_halo2ScopeGeneration||serial!=g_halo2ScopeSerial||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo2||
        g_scopeCacheDesc.Width!=g_halo2ScopeSourceDesc.Width||
        g_scopeCacheDesc.Height!=g_halo2ScopeSourceDesc.Height||
        g_scopeCacheDesc.Format!=g_halo2ScopeSourceDesc.Format)return false;
    g_halo2ScopeCopyActive=true;g_halo2ScopeSourceBound=false;return true;
#else
    return false;
#endif
}
void VR_EndHalo2Scope(bool completed)
{
#if HALOMCCVR_HALO2_STEREO6DOF
    if(completed&&g_halo2ScopeCopyActive&&g_halo2ScopeSourceBound&&g_context&&g_scopeCache&&g_halo2ScopeSource)
    {
        g_context->CopyResource(g_scopeCache,g_halo2ScopeSource);
        g_scopeImageTitle=GameTitle::Halo2;g_scopeImageGeneration=g_halo2ScopeGeneration;
        g_scopeHasImage.store(true,std::memory_order_release);
    }
    g_halo2ScopeCopyActive=false;g_halo2ScopeSourceBound=false;
#endif
}


// FSR diagnostic (fsr_probe, off by default). Log-only: observe slot 0 of every
// OMSetRenderTargets bind and record each DISTINCT (width, height, format,
// bind-flags) SCENE-SCALE render target, plus the viewport and current raster
// eye at that bind. When MCC's built-in FSR is toggled, a newly logged tuple
// shows whether FSR renders the scene into a smaller target (proven: it does)
// and, via the raster-eye column, whether that target is bound inside the
// per-eye render loop. Runs on the render thread only, allocation-free and
// lock-free: the table is a fixed static seen only from that thread.
//
// Two guards keep the log bounded (the first version dumped 1.16M lines / 145MB
// because MCC binds hundreds of tiny transient post-fx/shadow targets and the
// 12-slot table saturated instantly): skip anything narrower than ~40% of the
// backbuffer (below any real FSR render scale, which bottoms out at 50%), and
// dedup into a 64-slot table.
static void ProbeFsrTargets(ID3D11DeviceContext* context, UINT count,
                            ID3D11RenderTargetView* const* input)
{
    if (!context || !count || !input)
        return;
    // Normally an opt-in FSR investigation aid. It ALSO self-arms whenever a
    // per-eye redirect scope is active but no scene-colour target has ever
    // been learned - that exact combination IS the discovery failure, and this
    // census is the only thing that can name a new title's real scene target.
    // Self-arming matters because handing the user a config flag to prove a
    // mod bug is forbidden here: the answer has to arrive from a normal run.
    // Bounded three ways: only while discovery is failing, only for
    // scene-scale targets, and only 96 distinct shapes in the process.
    const int rasterEye = g_rasterEye.load(std::memory_order_relaxed);
    const bool discoveryFailing = rasterEye >= 0 && !g_sceneColorRtv;
    // Keep censusing for a few eyes AFTER Halo 4 latches. Stopping at the
    // latch is what made the previous run undiagnosable: exactly one line was
    // logged, the one target we then wrongly picked, and the rest of the
    // pipeline was never described. g_h4LearningEyes only leaves zero for
    // Halo 4, so this window cannot arm for any other title.
    const bool halo4CensusWindow =
        rasterEye >= 0 && g_h4LearningEyes > 0 && g_h4LearningEyes <= 6;
    if (!g_config.fsr_probe && !discoveryFailing && !halo4CensusWindow)
        return;

    // When self-armed, walk every bound slot rather than slot 0 alone: a
    // deferred renderer binds an MRT set, and assuming the scene lands on
    // slot 0 is precisely the assumption that failed.
    const UINT slots = (discoveryFailing || halo4CensusWindow) ? count : 1u;
    const UINT bbW = g_gameBackbufferDescValid ? g_gameBackbufferDesc.Width : 0;
    const UINT bbH = g_gameBackbufferDescValid ? g_gameBackbufferDesc.Height : 0;
    const UINT minWidth = bbW ? (bbW * 2) / 5 : 800u;

    for (UINT slot = 0; slot < slots; ++slot)
    {
        if (!input[slot])
            continue;
        ID3D11Resource* resource = nullptr;
        input[slot]->GetResource(&resource);
        if (!resource)
            continue;
        ID3D11Texture2D* tex = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        const bool isTexture = SUCCEEDED(resource->QueryInterface(
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)));
        if (isTexture)
            tex->GetDesc(&desc);
        if (tex)
            tex->Release();
        resource->Release();
        if (!isTexture)
            continue;

        // Only scene-scale targets are interesting. Anything below ~40% of the
        // backbuffer width is a shadow/post-fx buffer, not a candidate scene
        // render (FSR's lowest tier is 50%). Fail-open if the backbuffer size
        // is not known yet: keep a fixed 800px floor.
        if (desc.Width < minWidth)
            continue;

        // Dedup on (slot, size, format, bind, rasterEye) so each distinct
        // large target is logged once PER slot and eye-context. The raster eye
        // tells us whether a target is bound inside the per-eye redirect scope
        // (0/1) or outside it (-1).
        struct SeenTarget { UINT slot, w, h, format, bind; int eye; };
        static SeenTarget seen[96]{};
        static unsigned seenCount = 0;
        const UINT descFormat = static_cast<UINT>(desc.Format);
        bool already = false;
        for (unsigned i = 0; i < seenCount && !already; ++i)
            already = seen[i].slot == slot && seen[i].w == desc.Width &&
                seen[i].h == desc.Height && seen[i].format == descFormat &&
                seen[i].bind == desc.BindFlags && seen[i].eye == rasterEye;
        if (already)
            continue;

        D3D11_VIEWPORT vp{};
        UINT vpCount = 1;
        context->RSGetViewports(&vpCount, &vp);

        // The RTV's own view format matters as much as the resource format: a
        // TYPELESS resource is only usable through its typed view, and that is
        // what the eye cache has to be created as.
        D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
        input[slot]->GetDesc(&viewDesc);

        LOG("SCENEPROBE: slot%u RT %ux%u fmt=%u viewfmt=%u bind=0x%X samples=%u "
            "| viewport %.0fx%.0f at (%.0f,%.0f) | backbuffer %ux%u "
            "| rasterEye=%d | rtCount=%u",
            slot, desc.Width, desc.Height, descFormat,
            static_cast<UINT>(viewDesc.Format), desc.BindFlags,
            desc.SampleDesc.Count,
            vpCount ? vp.Width : 0.0f, vpCount ? vp.Height : 0.0f,
            vpCount ? vp.TopLeftX : 0.0f, vpCount ? vp.TopLeftY : 0.0f,
            bbW, bbH, rasterEye, count);

        if (seenCount < 96)
            seen[seenCount++] = {slot, desc.Width, desc.Height, descFormat,
                                 desc.BindFlags, rasterEye};
    }
}

void VR_Halo4NoteBoundRenderTargets(
    ID3D11DeviceContext* context, UINT count,
    ID3D11RenderTargetView* const* rtvs)
{
    if (context != g_context)
        return;
    if (!g_reticleCaptureState.active ||
        !g_reticleCaptureState.framingCaptured)
    {
        g_halo4AuthoredReticleTargetBound.store(
            false, std::memory_order_release);
        return;
    }
    const uintptr_t boundSlot0 = count && rtvs
        ? reinterpret_cast<uintptr_t>(rtvs[0]) : 0;
    // D3D11 immediate-context calls are ordered on one render thread. The D3D
    // hooks publish this only after the real state mutation and invalidate it
    // on the alternate RTV/UAV setter and ClearState as well. This retains
    // Stage 3BH's exact slot-0, mode-selected target gate without putting an
    // OMGetRenderTargets AddRef/Release pair in every admitted Draw.
    g_halo4AuthoredReticleTargetBound.store(
        VrHalo4AuthoredReticleDrawTargetMatches(
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4,
            g_reticleCaptureState.active,
            g_reticleCaptureState.framingCaptured,
            g_reticleCaptureState.publishesAuthored,
            boundSlot0,
            reinterpret_cast<uintptr_t>(g_authoredReticleRtv),
            reinterpret_cast<uintptr_t>(g_authoredReticleDiscardRtv)),
        std::memory_order_release);
}

void VR_Halo4PrepareAuthoredReticleDraw(ID3D11DeviceContext* context)
{
    if (context != g_context ||
        !g_reticleCaptureState.active ||
        !g_reticleCaptureState.framingCaptured ||
        !g_halo4AuthoredReticleTargetBound.load(std::memory_order_acquire) ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo4)
    {
        return;
    }

    const D3D11_VIEWPORT& base = g_reticleCaptureState.captureViewport;
    const Halo4CuiCaptureViewport input{
        base.TopLeftX, base.TopLeftY, base.Width, base.Height,
        base.MinDepth, base.MaxDepth};
    Halo4CuiCaptureViewport framed{};
    const uint32_t backbufferWidth = g_gameBackbufferDescValid
        ? g_gameBackbufferDesc.Width : 0;
    const uint32_t backbufferHeight = g_gameBackbufferDescValid
        ? g_gameBackbufferDesc.Height : 0;
    // 3CX fold-in: the vertical bias and the presentation zoom follow the
    // LIVE layout the visible-pass hide recorded; before any hide runs the
    // helpers return the calibrated 3BR constants.
    float liveBaseY = 0.0f;
    float liveHideShift = 0.0f;
    (void)Game_Halo4LiveCuiCanvas(liveBaseY, liveHideShift);
    if (!Halo4BuildCuiCaptureDrawViewport(
            input, backbufferWidth, backbufferHeight,
            Halo4CuiLiveVerticalBiasRatio(liveBaseY),
            Halo4CuiLiveCaptureScale(liveHideShift), framed))
    {
        return;
    }

    const D3D11_VIEWPORT viewport{
        framed.topLeftX, framed.topLeftY, framed.width, framed.height,
        framed.minDepth, framed.maxDepth};
    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(
        1, &g_reticleCaptureState.captureScissor);
    g_authoredReticleFramingReasserts.fetch_add(
        1, std::memory_order_relaxed);
}

bool VR_Halo4AuthoredReticleFeatureHealthy()
{
    // InstallHalo4CuiReticle arms only after every capture/suppression resource
    // is prepared. The only permanent runtime invalidation is this atomic
    // latch; checking it from the CUI hook is sufficient and avoids racing on
    // the render thread's raw D3D/OpenXR resource pointers.
    return !g_reticleChainFailed.load(std::memory_order_acquire);
}

void VR_ObserveReachAlternateDepthBind(
    ID3D11DeviceContext* context, UINT renderTargetCount,
    ID3D11DepthStencilView* depth)
{
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    const int eye = g_dlssDepthWindowEye.load(std::memory_order_relaxed);
    if (!depth || eye < 0 || eye > 1)
        return;
    const bool reachActive =
        TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
    if (!reachActive)
        return;
    const bool dlssDepthWanted = VR_DlssWantsEyeCamera();
    const bool currentEyeCameraPublished = g_dlssEye[eye].published;
    const bool exactContext = context && context == g_context;
    const bool depthStateMutated =
        renderTargetCount != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL;
    DlssDepthViewEntry* knownEntry = nullptr;
    for (int i = 0; i < g_dlssDepthViewCount && !knownEntry; ++i)
        if (g_dlssDepthViews[i].view == depth)
            knownEntry = &g_dlssDepthViews[i];
    const bool knownView = knownEntry != nullptr;
    const bool depthTrackingCapacity =
        knownView || g_dlssDepthViewCount < kDlssDepthViewSlots;
    bool exactDevice = knownEntry && knownEntry->device == g_device;
    // Known views were device-checked before retention. A distinct DSV pays
    // one GetDevice AddRef/Release here, then DlssNoteEyeDepth describes and
    // pins it once; recurring binds stay pointer/atomic-only.
    if (depth && reachActive && dlssDepthWanted &&
        currentEyeCameraPublished && exactContext && depthStateMutated &&
        depthTrackingCapacity && !exactDevice)
    {
        ID3D11Device* device = nullptr;
        depth->GetDevice(&device);
        exactDevice = device && device == g_device;
        if (exactDevice && knownEntry)
            knownEntry->device = g_device;
        if (device)
            device->Release();
    }
    const bool admitted = dlss::ReachAlternateDepthBindEligible(
        reachActive, dlssDepthWanted, eye, currentEyeCameraPublished,
        exactContext, exactDevice, depthStateMutated, true,
        depthTrackingCapacity);
    if (admitted)
    {
        DlssNoteEyeDepth(depth, false);
        // The classifier retains a distinct view before returning. Record the
        // already-proven device as borrowed metadata so recurring binds remain
        // pointer-only while the retained DSV keeps that device alive.
        for (int i = 0; i < g_dlssDepthViewCount; ++i)
            if (g_dlssDepthViews[i].view == depth)
                g_dlssDepthViews[i].device = g_device;
    }
#else
    (void)context;
    (void)renderTargetCount;
    (void)depth;
#endif
}

bool VR_RedirectRenderTargets(ID3D11DeviceContext* context, UINT count,
                              ID3D11RenderTargetView* const* input,
                              ID3D11DepthStencilView* depth,
                              ID3D11RenderTargetView** output)
{
#if HALOMCCVR_HALO2_STEREO6DOF
    // Halo 2 never redirects a render target. Its classic renderer draws the
    // world straight into the backbuffer (D-H2-1 census: the primary scene
    // RTV slot 0x197EE60 and the backbuffer slot 0x197EE58 hold the SAME
    // view), and its postprocess then reads that texture back through the
    // engine's own SRV. Redirecting the binds put each eye's world pass in
    // our texture while the postprocess re-read the engine's untouched
    // backbuffer and painted it over both eyes: M2 VALIDATION measured
    // 0/23842 differing pixels, i.e. identical eyes. Each eye is instead
    // copied whole after render_view returns (VR_EndRasterEye). Halo 2 is
    // also never fed through Halo 3's 0xA8 shape discovery below.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        if(g_halo2ScopeCopyActive&&input)
            for(UINT i=0;i<count;++i)if(input[i]==g_halo2ScopeSourceRtv)g_halo2ScopeSourceBound=true;
        Halo2SynchronousEyeScope& halo2Scope = g_halo2SynchronousEyeScope;
        if (halo2Scope.active && count && input && input[0])
        {
            halo2Scope.lastBoundRtv = input[0];
            ++halo2Scope.binds;
            bool known = false;
            for (int i = 0; i < halo2Scope.boundCount && !known; ++i)
                known = halo2Scope.bound[i] == input[0];
            if (!known && halo2Scope.boundCount < kHalo2EyeBoundRtvSlots)
                halo2Scope.bound[halo2Scope.boundCount++] = input[0];
        }
        // DLSS depth for Halo 2: both renderers draw into the final target
        // directly, so the depth view bound together with it is the scene
        // depth; any other full-raster depth inside the eye is the fallback
        // rule. Pointer compares only, inside a raster-eye scope only.
        if (depth && VR_DlssWantsEyeCamera())
        {
            const int halo2Eye = g_rasterEye.load(std::memory_order_relaxed);
            if (halo2Eye >= 0 && halo2Eye <= 1)
            {
                ID3D11RenderTargetView* const finalRtv =
                    g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed);
                DlssNoteEyeDepth(depth, finalRtv && count && input &&
                                            input[0] == finalRtv);
            }
        }
        return false;
    }
#endif
    ProbeFsrTargets(context, count, input);
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    auto& hudRoute = g_nativeHudEyeRoute;
    if (hudRoute.bypassOmRedirect)
        return false;
#endif
    const int eye = g_rasterEye.load();
    const bool scope=g_rasterScope.load(std::memory_order_acquire);
    // DLSS depth for a title that renders its eyes outside the raster-eye
    // scope (Reach): inside its depth window every depth view bound is
    // classified by the full-raster rule. Pointer compares only.
    if (depth && eye < 0 && VR_DlssWantsEyeCamera() &&
        g_dlssDepthWindowEye.load(std::memory_order_relaxed) >= 0)
        DlssNoteEyeDepth(depth, false);
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    const bool nativeHud = hudRoute.active && hudRoute.eye >= 0 &&
        hudRoute.eye <= 1;
#else
    constexpr bool nativeHud = false;
#endif
    if ((!scope && !nativeHud && (eye < 0 || eye > 1)) || !input || !output ||
        !g_gameSwapchain)
        return false;

    // Halo 4 rebinds the learned scene target inside CUI playback. While the
    // face-crosshair art is being captured, route that exact bind to the
    // authored texture instead of the eye. This is the only ownership change:
    // the existing VR crosshair still owns all placement.
    if (g_reticleCaptureState.active &&
        TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
    {
        ID3D11RenderTargetView* const captureTarget =
            g_reticleCaptureState.publishesAuthored
                ? g_authoredReticleRtv : g_authoredReticleDiscardRtv;
        bool captureChanged = false;
        for (UINT i = 0; i < count; ++i)
        {
            output[i] = input[i];
            if (AuthoredReticleCaptureOwnsSceneBind(
                    true, input[i] && input[i] == g_sceneColorRtv,
                    captureTarget != nullptr))
            {
                output[i] = captureTarget;
                captureChanged = true;
            }
        }
        if (captureChanged)
        {
            g_authoredReticleOmReroutes.fetch_add(1, std::memory_order_relaxed);
            // Halo 4 rebinds this exact scene target up to 3 times inside one
            // captured replay (measured: "9 exact capture OM reroutes" against
            // 3 captures). Each rebind can carry whatever viewport the engine's
            // own preceding pass left set - SCENEPROBE measured the SAME
            // learned RTV bound with a 947x683 viewport at one point and a
            // full-raster viewport at another. Left alone, the 3 rebinds paint
            // 3 differently-scaled passes into the same 512x512 texture, which
            // is a smeared composite, not a wrong crop - this is the direct
            // cause of the "some random asset" result. Putting the ONE viewport
            // this capture opened with back here, immediately after our own OM
            // rewrite and therefore after the engine's own preceding
            // RSSetViewports for this same rebind, keeps every rebind
            // consistent with the others instead of independently scaled.
            if (g_reticleCaptureState.framingCaptured && context == g_context)
            {
                g_context->RSSetViewports(
                    1, &g_reticleCaptureState.captureViewport);
                g_context->RSSetScissorRects(
                    1, &g_reticleCaptureState.captureScissor);
                g_authoredReticleFramingReasserts.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        return captureChanged;
    }
    int targetEye = eye;
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    if (nativeHud)
        targetEye = hudRoute.eye;
#endif
    ID3D11RenderTargetView* target = scope ? g_scopeCacheRtv :
        g_eyeCacheRtvs[targetEye];
    bool changed = false;      // any slot rewritten (scene color or sun shaft)
    bool sceneChanged = false; // scene-color redirect only: marks the eye image valid
    for (UINT i = 0; i < count; ++i)
    {
        output[i] = input[i];
        if (!input[i]) continue;

        // ODST's CHUD phase is logically inside this eye render just like Halo
        // 3, but its recompiled phase can bind the flat output RTV. Redirect
        // only the exact target saved at the proven CHUD phase boundary. This
        // is a pointer comparison in the OM hot hook; all COM work occurs once
        // at phase entry/exit.
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
        if (nativeHud)
        {
            if (target && input[i] == hudRoute.phaseOutputRtv)
            {
                output[i] = target;
                g_nativeHudProvenOmMatches.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
            // A phase-local target which is not the captured proven pointer
            // stays stock. In particular, never enter scene-target discovery
            // while a CHUD phase is active.
            continue;
        }
#endif

        // Normal steady-state path: two pointer comparisons and no COM calls.
        if (g_sceneColorRtv)
        {
            if (input[i] == g_sceneColorRtv && target)
            {
                output[i] = target;
                changed = true;
                sceneChanged = true;
            }
            continue;
        }

        // One-time discovery, before the exact scene RTV has been learned.
        ID3D11Resource* resource = nullptr;
        input[i]->GetResource(&resource);
        ID3D11Texture2D* candidate = nullptr;
        D3D11_TEXTURE2D_DESC candidateDesc{};
        const bool isTexture = resource &&
            SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void**>(&candidate)));
        if (isTexture)
            candidate->GetDesc(&candidateDesc);

        // Halo 3's completed frame is the unique full-resolution typeless RGBA
        // resource with RTV+SRV+UAV bindings at the end of the inner render.  The
        // preceding typed RGBA RT is an intermediate and can remain black.  Keep
        // our eye caches typed (from the game backbuffer) so they can be sampled
        // directly by the OpenXR blit.
        const bool isInternalSceneColor = i == 0 && candidate &&
            g_gameBackbufferDescValid &&
            candidateDesc.Width == g_gameBackbufferDesc.Width &&
            candidateDesc.Height == g_gameBackbufferDesc.Height &&
            candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS &&
            (candidateDesc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
                                        D3D11_BIND_UNORDERED_ACCESS)) ==
                (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
                 D3D11_BIND_UNORDERED_ACCESS);

        // Halo 4 does NOT present that shape, and picking its scene target by
        // "first thing that looks right" is measurably WRONG: C-H4-4 latched
        // the FIRST full-size 8-bit colour target bound in the eye and the
        // headset showed unlit meshes with no lighting, shadows, post or HUD
        // (run 68daa27, 2026-08-07). That target is an early pass; everything
        // after it composites into other targets we never touched.
        //
        // So Halo 4 does not decide at bind time at all. It OBSERVES a whole
        // eye, remembers the LAST qualifying target bound in it, and latches
        // that once two consecutive eyes agree - the last full-size colour
        // target written inside the eye scope is the composited result, not an
        // input to it. Qualification stays narrow: full backbuffer size,
        // single-sampled, renderable AND samplable, 8-bit RGBA/BGRA (MCC's
        // backbuffer here is R8G8B8A8_UNORM, so the composited image is in
        // that family; anything wider still has tonemapping ahead of it).
        const bool rgba8Family =
            candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ||
            candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            candidateDesc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
            candidateDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            candidateDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool halo4Learning = !isInternalSceneColor && i == 0 &&
            candidate && g_gameBackbufferDescValid && eye >= 0 && eye <= 1 &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4 &&
            candidateDesc.Width == g_gameBackbufferDesc.Width &&
            candidateDesc.Height == g_gameBackbufferDesc.Height &&
            candidateDesc.SampleDesc.Count == 1 && rgba8Family &&
            (candidateDesc.BindFlags & (D3D11_BIND_RENDER_TARGET |
                                        D3D11_BIND_SHADER_RESOURCE)) ==
                (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        if (halo4Learning)
        {
            // Remember it and keep going. The eye's LAST one wins, decided in
            // VR_EndRasterEye. Nothing is redirected during a learning eye.
            D3D11_RENDER_TARGET_VIEW_DESC lastViewDesc{};
            input[i]->GetDesc(&lastViewDesc);
            g_h4LastCandidate = input[i];
            g_h4LastCandidateDesc = candidateDesc;
            g_h4LastCandidateViewFormat = lastViewDesc.Format;
            ++g_h4CandidatesThisEye;
        }
        // Never latch at bind time for Halo 4.
        const bool isHalo4SceneColor = false;

        const bool learnSceneColor =
            isInternalSceneColor || isHalo4SceneColor;
        D3D11_RENDER_TARGET_VIEW_DESC sceneViewDesc{};
        if (learnSceneColor)
            input[i]->GetDesc(&sceneViewDesc);
        D3D11_TEXTURE2D_DESC eyeDesc = candidateDesc;
        if (learnSceneColor)
            eyeDesc.Format = sceneViewDesc.Format;

        if (learnSceneColor && EnsureEyeCaches(eyeDesc) && g_eyeCacheRtvs[eye])
        {
            input[i]->AddRef();
            g_sceneColorRtv = input[i];
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
            g_sceneColorResource = resource;
            resource = nullptr;
#endif
            output[i] = g_eyeCacheRtvs[eye];
            changed = true;
            sceneChanged = true;
            // Say WHICH rule matched and with what shape. If the relaxed rule
            // ever latches the wrong target, this line plus the SCENEPROBE
            // census above are what identify the right one without another
            // guess.
            LOG("M2 RASTER: learned scene-color RTV %p via the %s rule "
                "(%ux%u fmt=%u viewfmt=%u bind=0x%X); steady-state redirect "
                "is pointer-only",
                g_sceneColorRtv,
                isInternalSceneColor ? "Halo 3 exact-shape" : "Halo 4 relaxed",
                candidateDesc.Width, candidateDesc.Height,
                static_cast<UINT>(candidateDesc.Format),
                static_cast<UINT>(sceneViewDesc.Format),
                candidateDesc.BindFlags);
        }
        if (candidate) candidate->Release();
        if (resource) resource->Release();
    }
    if (sceneChanged)
    {
        if(scope)
        {
            g_scopeRedirected=true;
        }
        else
        {
            g_rasterRedirected[eye] = true;
            if constexpr (kEnableRetiredRasterTrace)
            {
                VR_TraceEvent("rtv-redirect", eye, 0);
                static std::atomic<unsigned> logged{0};
                if (logged.fetch_add(1) < 4)
                    LOG("M2 RASTER: redirected internal scene-color RTV to eye %d target", eye);
            }
        }
    }
    // DLSS depth tracking: pointer compares only, and only inside a real eye
    // render while the upscaler is selected.
    if (depth && !scope && eye >= 0 && eye <= 1 &&
        VR_DlssWantsEyeCamera())
        DlssNoteEyeDepth(depth, sceneChanged);
    return changed;
}

uint64_t VR_TakeAuthoredReticleOmReroutes()
{
    return g_authoredReticleOmReroutes.exchange(0, std::memory_order_relaxed);
}

uint64_t VR_TakeAuthoredReticleFramingReasserts()
{
    return g_authoredReticleFramingReasserts.exchange(
        0, std::memory_order_relaxed);
}

bool VR_GetHeadPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool ok = g_headPoseValid;
    if (ok)
    {
        outQuat[0] = g_headPose.orientation.x;
        outQuat[1] = g_headPose.orientation.y;
        outQuat[2] = g_headPose.orientation.z;
        outQuat[3] = g_headPose.orientation.w;
        outPos[0] = g_headPose.position.x;
        outPos[1] = g_headPose.position.y;
        outPos[2] = g_headPose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

const char* VR_ControllerProfileName(bool left)
{
    return vr_mapping::ProfileName(g_controllerProfiles[left?0:1].load(std::memory_order_acquire));
}

void VR_GetPadState(VrPadState& out)
{
    if (!g_headCsInit)
    {
        out = {};
        return;
    }
    EnterCriticalSection(&g_headCs);
    out = g_padState;
    // The menu trigger must not also navigate native tabs or fire on resume.
    // Drain a consumed hold through release, even if the option is switched off.
    static std::atomic<bool> nativeMenuTriggerHeld{false};
    if (out.trigR <= 0.35f) nativeMenuTriggerHeld.store(false);
    else if (NativeMenuPointer_ConsumesTrigger()) nativeMenuTriggerHeld.store(true);
    if (nativeMenuTriggerHeld.load()) out.trigR = 0;
    const uint64_t now = GetTickCount64();
    const GameTitle weaponTitle=TitleAdapter_GetActiveTitle();
    const int weaponIndex=weapon_interaction::TitleIndex(weaponTitle);
    const unsigned weaponOptions=(g_config.manual_reload?1u:0u)|(g_config.weapon_holsters?2u:0u)|(g_config.left_handed?4u:0u)|
        (!g_config.weapon_holster_slide?8u:0u)|(g_config.weapon_holster_click?16u:0u)|(g_config.weapon_needler_shake?32u:0u);
    if(out.weaponOptions!=weaponOptions||out.weaponSpace!=g_contactSpaceEpoch.load(std::memory_order_acquire)||
        weaponIndex<0||out.weaponReloadBinding!=WeaponGestureBinding(weaponTitle,vr_mapping::Reload,now)||
        out.weaponSwitchBinding!=WeaponGestureBinding(weaponTitle,vr_mapping::SwitchWeapon,now)||
        !weapon_interaction::Fresh(now,out.weaponSampleMs,weaponTitle,out.weaponTitle,
        TitleAdapter_GetGeneration(weaponTitle),out.weaponGeneration,
        (g_config.manual_reload||g_config.weapon_holsters)&&Game_IsHeadTracking()&&VR_IsStereoEnabled()&&
        TitleAdapter_GetRuntimeMode()==RuntimeMode::Gameplay&&
        g_sessionStateShared.load(std::memory_order_acquire)==XR_SESSION_STATE_FOCUSED&&
        !Menu_IsOpen()&&!VR_IsPausePresentation()&&!VR_IsPausePresentationTarget()&&!VR_IsCutsceneTheaterActive()))
    {
        out.weaponButtons=0;
        out.weaponConsumePrimary=out.weaponConsumeSupport=false;
    }
    if(now>=out.weaponPulseUntilMs) out.weaponButtons=0;
    if(g_config.manual_reload&&
        out.weaponGraph!=VR_GetWeaponModelIdentity(weaponTitle,out.weaponGeneration,out.weaponSpace,now))
        out.weaponButtons=0;
    const uint64_t sampled = g_thumbrestDpadSampleMs.load(std::memory_order_acquire);
    if (!g_config.quest_thumbrest_dpad || !sampled || now < sampled || now-sampled > 250 ||
        g_sessionStateShared.load(std::memory_order_acquire) != XR_SESSION_STATE_FOCUSED || Menu_IsOpen())
    {
        out.thumbrestDpad = false;
        out.dpadX = out.dpadY = 0.0f;
    }
    LeaveCriticalSection(&g_headCs);
}

void VR_ObserveWeaponModel(GameTitle title,uint32_t generation,uint64_t identity) noexcept
{
    if((!g_config.manual_reload&&!g_config.per_gun_alignment)||title!=TitleAdapter_GetActiveTitle()||
        generation!=TitleAdapter_GetGeneration(title)) return;
    (void)g_weaponModels.Publish({title,generation,identity,
        g_contactSpaceEpoch.load(std::memory_order_acquire),GetTickCount64()});
}

uint64_t VR_GetWeaponModelIdentity(GameTitle title,uint32_t generation,uint64_t space,uint64_t now) noexcept
{
    if(title==GameTitle::HaloCE) return HaloCEFirstPerson_WeaponGraph(generation,space,now);
    return g_weaponModels.Read(title,generation,space,now);
}

void VR_PublishWeaponReticleRay(GameTitle title,uint8_t slot,const weapon_muzzle::Receipt& receipt,
    const contact_melee::TrackingToWorld& transform) noexcept
{
    if(!dual_reticle::Supported(title)||slot>1) return;
    dual_reticle::ProjectedRay projected{};
    if(receipt.title==title&&receipt.slot==slot)
        (void)dual_reticle::Project(receipt,transform,projected);
    (void)g_weaponReticleRays[static_cast<unsigned>(title)][slot].Publish(projected);
}

void VR_PublishReloadTarget(GameTitle title,uint32_t generation,uint64_t identity,uint64_t serial,
    const contact_melee::TrackingToWorld& trackingToWorld,const float world[3]) noexcept
{
    if(!g_config.manual_reload||!world||title!=TitleAdapter_GetActiveTitle()||
        generation!=TitleAdapter_GetGeneration(title)) return;
    VrContactTrackingSnapshot current{};
    contact_melee::TrackingToWorld primaryController{};
    if(!VR_GetContactTrackingSnapshot(current)||current.serial!=serial||!current.rawPrimaryValid||
        !primaryController.SetPose(current.rawPrimaryOrientation,current.rawPrimaryPosition)) return;
    weapon_interaction::ReloadTarget target{};
    if(weapon_interaction::BuildReloadTarget(title,generation,identity,
        current.referenceEpoch,GetTickCount64(),current.leftHanded,{world[0],world[1],world[2]},
        trackingToWorld,primaryController,target)) (void)g_reloadTargets.Publish(target);
}

void VR_UpdateWeaponAlignment()
{
    static GameTitle previousTitle=GameTitle::None;
    static uint32_t previousGeneration=0;
    static uint64_t previousIdentity=0;
    const auto title=TitleAdapter_GetActiveTitle();
    const auto generation=TitleAdapter_GetGeneration(title);
    if(title!=previousTitle||generation!=previousGeneration)
        previousIdentity=0;
    previousTitle=title;previousGeneration=generation;
    const uint64_t identity=VR_GetWeaponModelIdentity(title,generation,
        g_contactSpaceEpoch.load(std::memory_order_acquire),GetTickCount64());
    // A pause/F1 menu may stop native palette submission. Retain only the
    // previously admitted gun in this generation while the player edits it.
    if(identity||(!Menu_IsOpen()&&!VR_IsPausePresentation()&&!VR_IsPausePresentationTarget()))
        previousIdentity=identity;
    Config_ApplyWeaponProfile(previousIdentity);
}

void VR_ReportWeaponInteractions(uint64_t nowMs)
{
    static HRESULT previousAccessory=S_FALSE;
    const HRESULT accessory=g_weaponAccessoryResult.load(std::memory_order_acquire);
    if(accessory!=previousAccessory)
    {
        LOG("Reload accessory: %s HRESULT=0x%08X; isolated authored geometry and surface atlas, simple accessory lighting; "
            "native animation/ammo and camera unaffected",
            SUCCEEDED(accessory)?"compositor draw ready":"stock gesture fallback",static_cast<unsigned>(accessory));
        previousAccessory=accessory;
    }
    static uint64_t lastReport=0;
    static GameTitle previousTitle=GameTitle::None;
    static unsigned previousOptions=0,previousGrabs=0,previousReloads=0,previousSwaps=0;
    static uint64_t previousBindings=0;
    static bool previousReady=false;
    const GameTitle title=TitleAdapter_GetActiveTitle();
    const int index=weapon_interaction::TitleIndex(title);
    const unsigned options=(g_config.manual_reload?1u:0u)|(g_config.weapon_holsters?2u:0u)|(g_config.left_handed?4u:0u)|
        (!g_config.weapon_holster_slide?8u:0u)|(g_config.weapon_holster_click?16u:0u)|(g_config.weapon_needler_shake?32u:0u);
    const uint32_t reloadBinding=WeaponGestureBinding(title,vr_mapping::Reload,nowMs);
    const uint32_t switchBinding=WeaponGestureBinding(title,vr_mapping::SwitchWeapon,nowMs);
    const uint64_t bindings=uint64_t(reloadBinding)|(uint64_t(switchBinding)<<32);
    const bool changed=options!=previousOptions||title!=previousTitle||bindings!=previousBindings;
    if(!changed&&nowMs>=lastReport&&nowMs-lastReport<5000) return;
    const uint64_t readyAt=g_weaponGestureReadyMs.load(std::memory_order_acquire);
    const bool ready=readyAt&&nowMs>=readyAt&&nowMs-readyAt<=150;
    static uint64_t previousModel=0;
    const uint64_t modelIdentity=VR_GetWeaponModelIdentity(title,TitleAdapter_GetGeneration(title),
        g_contactSpaceEpoch.load(std::memory_order_acquire),nowMs);
    if((options&1)&&(changed||previousModel!=modelIdentity))
    {
        const auto* model=weapon_model::Find(title,modelIdentity);
        LOG("Reload model: title=%s identity=%016llX name=%s visual=%s needleShakeEligible=%u; "
            "native reload/ammo rules unchanged",
            index>=0?weapon_interaction::kTitleNames[index]:"none",static_cast<unsigned long long>(modelIdentity),
            model?model->name:(modelIdentity?"unfamiliar held model":"no fresh held-model receipt"),
            model?(model->vertexCount?"isolated authored part":"gesture only"):
                (modelIdentity&&g_config.weapon_unknown_reload_visual?"generic blue reload item":"gesture only"),
            model&&model->needles?1u:0u);
    }
    previousModel=modelIdentity;
    const unsigned grabs=g_weaponGestureGrabs.load(std::memory_order_relaxed);
    const unsigned reloads=g_weaponReloadRequests.load(std::memory_order_relaxed);
    if((options&1)&&(changed||reloads!=previousReloads))
        LOG("Reload insertion receivers: authoredAssembly=%u genericControllerFallback=%u; "
            "fallback preserves the existing gesture when no fresh authored receiver is available",
            g_authoredInsertionRequests.load(std::memory_order_relaxed),
            g_fallbackInsertionRequests.load(std::memory_order_relaxed));
    const unsigned swaps=g_weaponSwapRequests.load(std::memory_order_relaxed);
    if(((options|previousOptions)&3)&&(changed||ready!=previousReady||grabs!=previousGrabs||
        reloads!=previousReloads||swaps!=previousSwaps))
        LOG("Weapon gestures: title=%s reload=%u holsters=%u leftHanded=%u readiness=%s "
            "configuredReload=0x%05X configuredSwitch=0x%05X grabs=%u reloadRequests=%u swapRequests=%u "
            "slide=%u click=%u pouchRadius=%.2f insertRadius=%.2f holsterRadius=%.2f drawMin=%.2f needleShake=%u "
            "(requests only; native ammo/inventory determine outcome)",
            index>=0?weapon_interaction::kTitleNames[index]:"none",options&1,(options>>1)&1,(options>>2)&1,
            ready?"ready":"stock input; waiting for focused tracked on-foot single-weapon play",
            reloadBinding,switchBinding,grabs,reloads,swaps,
            g_config.weapon_holster_slide?1u:0u,g_config.weapon_holster_click?1u:0u,
            g_config.weapon_body_zone_radius_m,g_config.weapon_insert_radius_m,
            g_config.weapon_holster_radius_m,g_config.weapon_holster_draw_m,g_config.weapon_needler_shake?1u:0u);
    previousOptions=options;previousTitle=title;previousBindings=bindings;previousReady=ready;
    previousGrabs=grabs;previousReloads=reloads;previousSwaps=swaps;lastReport=nowMs;
}

void VR_SetScopeActive(bool active)
{
    g_scopeActive.store(active,std::memory_order_release);
    if(active) return;
    g_scopeResetRequested.store(true,std::memory_order_release);
    g_scopeHasImage.store(false,std::memory_order_release);
}

bool VR_IsScopeActive()
{
    return g_scopeActive.load(std::memory_order_acquire);
}

void VR_RequestScopeToggle()
{
    g_scopeToggleSerial.fetch_add(1,std::memory_order_release);
}

void VR_SetGameHaptics(float amplitude)
{
    const float v = std::clamp(amplitude, 0.0f, 1.0f);
    g_requestedHaptics.store(v, std::memory_order_release);
    // Peak-hold: raise the running peak so a pulse that arrives and clears
    // between two VR-frame samples still registers. Lock-free CAS max keeps
    // this XInput SetState hook thread hot-path-safe (no lock/alloc/logging).
    float cur = g_peakHaptics.load(std::memory_order_relaxed);
    while (v > cur && !g_peakHaptics.compare_exchange_weak(cur, v,
        std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

void VR_PulseContactHaptics(bool left, float amplitude)
{
    const float v = std::clamp(amplitude, 0.0f, 1.0f);
    std::atomic<float>& peak = g_contactHaptics[left ? 0 : 1];
    float current = peak.load(std::memory_order_relaxed);
    while (v > current && !peak.compare_exchange_weak(
        current, v, std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

bool VR_GetPhysicalControllerPose(int hand, float outQuat[4], float outPos[3])
{
    if (!g_headCsInit || hand < 0 || hand > 1 || !outQuat || !outPos)
        return false;
    EnterCriticalSection(&g_headCs);
    const uint64_t now = GetTickCount64();
    const bool valid = g_physicalControllerValid[hand] &&
        g_physicalControllerAtMs && now >= g_physicalControllerAtMs &&
        now - g_physicalControllerAtMs <= 250;
    if (valid)
    {
        const auto& pose = g_physicalControllerPose[hand];
        outQuat[0] = pose.orientation.x; outQuat[1] = pose.orientation.y;
        outQuat[2] = pose.orientation.z; outQuat[3] = pose.orientation.w;
        outPos[0] = pose.position.x; outPos[1] = pose.position.y;
        outPos[2] = pose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return valid;
}

bool VR_GetRightControllerPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool ok = g_rightAimPoseValid;
    if (ok)
    {
        outQuat[0] = g_rightAimPose.orientation.x;
        outQuat[1] = g_rightAimPose.orientation.y;
        outQuat[2] = g_rightAimPose.orientation.z;
        outQuat[3] = g_rightAimPose.orientation.w;
        outPos[0] = g_rightAimPose.position.x;
        outPos[1] = g_rightAimPose.position.y;
        outPos[2] = g_rightAimPose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

bool VR_GetPhysicalMeleeSpeed(bool left,float& speed)
{
    speed=-1.0f;
    if(!g_headCsInit) return false;
    EnterCriticalSection(&g_headCs);
    const bool valid=g_meleeSpeedHistory[left?0:1].Read(GetTickCount64(),speed);
    LeaveCriticalSection(&g_headCs);
    return valid;
}

bool VR_GetControllerLinearVelocity(bool left, float outVelocity[3])
{
    if (!g_headCsInit || !outVelocity)
        return false;
    EnterCriticalSection(&g_headCs);
    const uint64_t capturedAtMs = left ? g_leftAimLinearVelocityAtMs :
        g_rightAimLinearVelocityAtMs;
    const uint64_t nowMs = GetTickCount64();
    const bool ok = (left ? g_leftAimLinearVelocityValid :
        g_rightAimLinearVelocityValid) && capturedAtMs != 0 &&
        nowMs >= capturedAtMs && nowMs - capturedAtMs <= 100;
    if (ok)
    {
        const XrVector3f& velocity = left ? g_leftAimLinearVelocity :
            g_rightAimLinearVelocity;
        outVelocity[0] = velocity.x;
        outVelocity[1] = velocity.y;
        outVelocity[2] = velocity.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

bool VR_GetEyeViewOffset(int eye, float outPosition[3], float outQuat[4])
{
    if (eye < 0 || eye > 1 || !outPosition || !outQuat || g_views.size() < 2)
        return false;
    const XrQuaternionf& a = g_views[0].pose.orientation;
    const XrQuaternionf& b = g_views[1].pose.orientation;
    // Quaternions q and -q encode the same rotation. Align their signs before
    // averaging so a runtime choosing opposite representations cannot collapse
    // the midpoint to zero.
    const float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    float cx = a.x + b.x*sign, cy = a.y + b.y*sign;
    float cz = a.z + b.z*sign, cw = a.w + b.w*sign;
    const float len = sqrtf(cx * cx + cy * cy + cz * cz + cw * cw);
    if (!std::isfinite(len) || len < 1e-5f)
        return false;
    cx /= len; cy /= len; cz /= len; cw /= len;

    // The VIEW reference origin is the centroid of the stereo view origins.
    // Reconstruct the same midpoint from this atomic xrLocateViews result, then
    // express each eye's position in that midpoint's local axes. This preserves
    // the runtime's actual, possibly adjustable IPD and any non-horizontal eye
    // offset instead of imposing the PSVR2 baseline on every headset.
    const XrVector3f& ap = g_views[0].pose.position;
    const XrVector3f& bp = g_views[1].pose.position;
    const float separationX = bp.x-ap.x;
    const float separationY = bp.y-ap.y;
    const float separationZ = bp.z-ap.z;
    const float separation = sqrtf(separationX*separationX +
                                   separationY*separationY +
                                   separationZ*separationZ);
    if (!std::isfinite(separation) || separation < 0.03f || separation > 0.10f)
        return false;
    const XrVector3f& ep = g_views[eye].pose.position;
    const XrVector3f delta{
        ep.x - (ap.x+bp.x)*0.5f,
        ep.y - (ap.y+bp.y)*0.5f,
        ep.z - (ap.z+bp.z)*0.5f};
    const XrVector3f local = Rotate({-cx,-cy,-cz,cw},delta);
    if (!std::isfinite(local.x) || !std::isfinite(local.y) ||
        !std::isfinite(local.z))
        return false;
    outPosition[0]=local.x;
    outPosition[1]=local.y;
    outPosition[2]=local.z;

    // relative = conj(center) * eye
    const XrQuaternionf& e = g_views[eye].pose.orientation;
    outQuat[0] = cw * e.x - cx * e.w - cy * e.z + cz * e.y;
    outQuat[1] = cw * e.y + cx * e.z - cy * e.w - cz * e.x;
    outQuat[2] = cw * e.z - cx * e.y + cy * e.x - cz * e.w;
    outQuat[3] = cw * e.w + cx * e.x + cy * e.y + cz * e.z;
    return std::isfinite(outQuat[0]) && std::isfinite(outQuat[1]) &&
           std::isfinite(outQuat[2]) && std::isfinite(outQuat[3]);
}

float VR_GetScopeZoom()
{
    return g_scopeRuntimeZoom.load(std::memory_order_acquire);
}

bool VR_CanPrepareAuthoredReticleResources()
{
    return g_authoredReticlePreparationReady.load(
        std::memory_order_acquire);
}

AuthoredReticlePreparationResult VR_PrepareAuthoredReticleResources()
{
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    ReachExclusiveResourceLock lock(g_reachDisplayResourceLock);
#endif
    if (!VR_CanPrepareAuthoredReticleResources() ||
        !g_device || !g_context || g_session == XR_NULL_HANDLE)
    {
        return AuthoredReticlePreparationResult::NotReady;
    }
    if (g_reticleChainFailed.load(std::memory_order_acquire))
        return AuthoredReticlePreparationResult::Failed;
    if (g_reticleChain == XR_NULL_HANDLE &&
        !CreateChain(kReticleSize, kReticleSize, g_reticleChain,
                     g_reticleImages, g_reticleRtvs, "crosshair"))
    {
        g_reticleChainFailed.store(true, std::memory_order_release);
        return AuthoredReticlePreparationResult::Failed;
    }
    if (g_reticleImages.empty() ||
        g_reticleRtvs.size() != g_reticleImages.size())
    {
        return AuthoredReticlePreparationResult::Failed;
    }
    // Capture and suppression render only into their private D3D targets.
    // The OpenXR swapchain RTV is needed later, after acquire identifies the
    // one image being uploaded. C-H4-43i eagerly created every image RTV here;
    // one refusal prevented both CUI hooks from installing for the whole level
    // even though this capture transaction never uses those views. Match the
    // accepted Halo 3/ODST lazy upload path and validate only the resources the
    // hot redirect actually consumes.
    return EnsureAuthoredReticleTexture()
        ? AuthoredReticlePreparationResult::Ready
        : AuthoredReticlePreparationResult::Failed;
}

void VR_InvalidatePreparedAuthoredReticleCapture()
{
    // Called only on the render thread before opening a new admitted title
    // transaction. Halo 3/ODST retain their accepted serial lifecycle.
    g_authoredReticleReady = false;
    g_authoredReticleSerial = 0;
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        g_authoredReticleProbePending = false;
}

static bool BeginAuthoredReticleCaptureInternal(
    bool requirePreparedResources, bool requireCrosshairEnabled,
    bool publishAuthored)
{
    // `crosshair=0` means "do not show a VR crosshair". For Halo 3/ODST,
    // refusing here is the whole implementation: they hide their native
    // reticle through a separate visibility predicate, so declining the
    // redirect correctly leaves stock drawing alone.
    //
    // Reach has no such predicate, and its CHUD alpha field is inert when
    // written (proven: 1.000 across 860 live samples while the mod wrote 0).
    // This redirect is therefore the ONLY thing that keeps Reach's native
    // crosshair off the eye. Refusing it for `crosshair=0` meant the setting
    // that asks for no crosshair produced the stock flat one instead - and,
    // before the failure-isolation fix, tore VR down as well. Reach passes
    // requireCrosshairEnabled=false so the redirect always runs; whether the
    // captured art is ever SHOWN is decided separately by the compositor,
    // which still requires g_config.crosshair before uploading or submitting
    // the quad. So `crosshair=0` in Reach now means exactly what it says:
    // no crosshair anywhere, flat or VR.
    // Halo 4 follows the same prepared-only rule at its CUI subtree boundary;
    // its non-capture paths bind the distinct discard target below.
    if (!g_context ||
        (requireCrosshairEnabled && !g_config.crosshair) ||
        !g_stereoEnabled.load(std::memory_order_relaxed) ||
        !g_preparedFrame.begun || g_reticleCaptureState.active)
        return false;

    if (requirePreparedResources)
    {
        // A latched chain failure belongs only to this optional feature. Both
        // prepared Halo 4 paths must refuse before redirecting the CUI target,
        // allowing the unmodified native reticle to draw while stereo stays
        // armed.
        if (g_reticleChainFailed.load(std::memory_order_acquire) ||
            g_reticleChain == XR_NULL_HANDLE || g_reticleImages.empty() ||
            g_reticleRtvs.size() != g_reticleImages.size() ||
            !g_authoredReticleTexture || !g_authoredReticleRtv ||
            (!publishAuthored && (!g_authoredReticleDiscardTexture ||
                                  !g_authoredReticleDiscardRtv)))
        {
            return false;
        }
        // XR image views are intentionally lazy. Only UploadAuthoredReticle,
        // after xrAcquireSwapchainImage chooses an index, needs one.
    }
    else
    {
        // Preserve the accepted Halo 3/ODST lazy preparation path. Reach calls
        // only the prepared entry above, so allocation and logging never occur
        // from its HREK widget hook.
        if (!EnsureReticleChain() || !EnsureAuthoredReticleTexture())
            return false;
    }

    ID3D11RenderTargetView* const redirectRtv = publishAuthored
        ? g_authoredReticleRtv : g_authoredReticleDiscardRtv;
    if (!redirectRtv)
        return false;

    const GameTitle captureTitle = TitleAdapter_GetActiveTitle();
    auto& saved = g_reticleCaptureState;
    const bool ceCapture=captureTitle==GameTitle::HaloCE;
    if (ceCapture&&(!HaloCEHudLayout_CopyState(g_context,&saved.viewportCount,
        saved.viewports,&saved.scissorCount,saved.scissors)||saved.viewportCount!=1||
        !HaloCEHud_ReadTarget(g_context,saved.ceTarget)||!saved.ceTarget.count))
        return false;

    const uint64_t serial = g_preparedFrame.serial;
    if (publishAuthored && g_authoredReticleSerial != serial)
    {
        const float clear[4] = {0, 0, 0, 0};
        g_context->ClearRenderTargetView(g_authoredReticleRtv, clear);
        g_authoredReticleSerial = serial;
        g_authoredReticleReady = false;
        // New displayed frame: start the art-identity accumulation clean so a
        // static crosshair produces the same key every frame.
        Game_ResetAuthoredCrosshairKey();
    }

    if (!ceCapture)
    {
        g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                      saved.rtvs, &saved.dsv);
        saved.viewportCount =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        g_context->RSGetViewports(&saved.viewportCount, saved.viewports);
        saved.scissorCount =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        g_context->RSGetScissorRects(&saved.scissorCount, saved.scissors);
    }

    D3D11_VIEWPORT captureViewport{};
    // Halo 3/ODST/Reach hook a widget-scoped draw, so the viewport already
    // live when their capture begins IS that widget's own viewport - it is a
    // meaningful, consistent quantity. Halo 4 has no such narrow hook (its
    // whole CUI stream replays through here), so the live viewport at capture
    // entry is only ever whatever the PRECEDING unrelated pass left bound -
    // measured this session ranging from the full 4834x3486 raster down to a
    // 1209x872 slice depending on exact timing, non-deterministic capture to
    // capture. The reticle container's OWN measured transform base
    // (-halfRasterWidth/+halfRasterHeight, proven across two separate
    // sessions at two different raster sizes) is positioned directly in
    // raster-pixel space, so a fixed region centred on the raster is a
    // meaningful, timing-independent source for Halo 4 alone.
    if (captureTitle == GameTitle::Halo4)
    {
        // Stage 3BH: use Halo 4's own measured CUI framing, not the rejected
        // square 2048x2048 guess. The draw hook derives raster aspect and the
        // accepted 2.5x scale from this immutable per-capture base.
        captureViewport.Width = kHalo4CuiCaptureWidth;
        captureViewport.Height = kHalo4CuiCaptureHeight;
        captureViewport.MinDepth = 0.0f;
        captureViewport.MaxDepth = 1.0f;
    }
    else if (saved.viewportCount)
        captureViewport = saved.viewports[0];
    else
    {
        captureViewport.Width =
            static_cast<float>(g_gameBackbufferDesc.Width);
        captureViewport.Height =
            static_cast<float>(g_gameBackbufferDesc.Height);
        captureViewport.MinDepth = 0.0f;
        captureViewport.MaxDepth = 1.0f;
    }
    // The outer quad retains the universal crosshair_size_deg and distance
    // sliders. Halo 3 and ODST share 4x internal authored-art occupancy so the
    // same slider values produce matching apparent size. Reach retains its
    // independently calibrated 2x occupancy. Halo 4 now uses that SAME proven
    // 4x ratio, baked directly into the fixed region above rather than
    // applied as a further multiplier, because C-H4-47 measured that
    // shrinking the WHOLE raster (~9.4x this session) into the capture
    // produced a totally blank result - consistent with a thin/hollow
    // reticle outline falling below rasterizer coverage at that extreme a
    // minification. Scope zoom remains separate.
    const float authoredCaptureScale =
        captureTitle == GameTitle::Halo3 ||
        captureTitle == GameTitle::Halo3ODST ? 4.0f :
        captureTitle == GameTitle::HaloReach ? 2.0f :
        captureTitle == GameTitle::Halo4 ? 1.0f : 1.0f;
    captureViewport.Width *= authoredCaptureScale;
    captureViewport.Height *= authoredCaptureScale;
    // This symmetric centring is what maps the SOURCE's own centre - where any
    // FPS crosshair sits in its own normal full-screen render, independent of
    // whatever viewport is used to display it - to the capture texture's
    // centre, regardless of the chosen source width/height.
    captureViewport.TopLeftX =
        (static_cast<float>(kReticleSize) - captureViewport.Width) * 0.5f;
    captureViewport.TopLeftY =
        (static_cast<float>(kReticleSize) - captureViewport.Height) * 0.5f;
    const D3D11_RECT captureScissor{
        0, 0, static_cast<LONG>(kReticleSize),
        static_cast<LONG>(kReticleSize)};
    saved.publishesAuthored = publishAuthored;
    // Retained so a later Halo-4 scene-target rebind DURING this same capture
    // (see VR_RedirectRenderTargets) can put the SAME viewport back instead of
    // drawing with whatever the engine's own prior pass left bound.
    saved.captureViewport = captureViewport;
    saved.captureScissor = captureScissor;
    saved.framingCaptured = true;
    saved.active = true;
    saved.cePrivateRaster=ceCapture;
    saved.ceRestoring=false;
    saved.ceStateValid=ceCapture;
    if (saved.cePrivateRaster) HaloCEHudLayout_BeginPrivateRaster();
    // Publish the complete capture mode before the OM hook observes this bind;
    // otherwise its exact selected-target comparison would use the preceding
    // capture's authored/discard mode and miss the first required draw pin.
    g_context->OMSetRenderTargets(1, &redirectRtv, nullptr);
    g_context->RSSetViewports(1, &captureViewport);
    g_context->RSSetScissorRects(1, &captureScissor);
    return true;
}

bool VR_ShouldCaptureAuthoredReticleThisFrame()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        return halo_ce::ReticleCanReplaceCapture(g_authoredReticleProbePending);

    // Until valid art is held there is nothing to fall back on, so never skip.
    if (!g_reticleContainsAuthored)
        return true;

    // Do not overwrite the captured source while its prior coverage request is
    // in flight; SubmitPreparedFrame will poll it without blocking.
    if (g_authoredReticleProbePending)
        return false;

    // ODST's captured quad is static between weapon and state changes, so it
    // samples only often enough to notice one. Halo 3 and Halo 4's authored
    // crosshairs animate, so they sample at the cadence the user asked for -
    // also the cadence at which art can publish, avoiding discarded work.
    constexpr uint64_t kOdstCaptureSampleGapFrames = 30;
    uint64_t gapFrames = 0;
    switch (TitleAdapter_GetActiveTitle())
    {
    case GameTitle::Halo3:
        gapFrames = ResolveAuthoredAnimationGapFrames(
            g_config.crosshair_animation_frames);
        // Holding one image still needs a slow re-sample so a weapon swap is
        // eventually noticed; it just never animates between those.
        if (gapFrames == 0)
            gapFrames = kOdstCaptureSampleGapFrames;
        break;
    case GameTitle::Halo4:
        gapFrames = ResolveAuthoredAnimationGapFrames(
            g_config.crosshair_animation_frames);
        // H4EK's transform ID is not proven art identity. With animation off,
        // retain only a slow sample so a weapon swap can still replace art.
        if (gapFrames == 0)
            gapFrames = kOdstCaptureSampleGapFrames;
        break;
    case GameTitle::Halo3ODST:
        gapFrames = kOdstCaptureSampleGapFrames;
        break;
    default:
        return true;
    }

    // On a skipped frame the title predicate hides the flat widget without
    // redirecting a render target or drawing any authored crosshair pixels.
    static std::atomic<uint64_t> lastSampleSerial{0};
    const uint64_t serial = g_preparedFrame.serial;
    uint64_t last = lastSampleSerial.load(std::memory_order_relaxed);
    for (;;)
    {
        if (!ShouldSampleAuthoredCapture(gapFrames, last, serial))
            return false;
        if (serial == last)
            return true;
        if (lastSampleSerial.compare_exchange_weak(
                last, serial, std::memory_order_relaxed,
                std::memory_order_relaxed))
            return true;
    }
}

bool VR_BeginAuthoredReticleCapture()
{
    return BeginAuthoredReticleCaptureInternal(false, true, true);
}

// Reach's hide-the-widget entry: lazily creates whatever is missing (so a
// transient resource state can never fail it - that regression cost a build)
// and never refuses because the crosshair is switched off, because refusing
// is what lets Reach's flat crosshair through.
bool VR_BeginAuthoredReticleRedirect()
{
    return BeginAuthoredReticleCaptureInternal(false, false, true);
}

bool VR_BeginPreparedAuthoredReticleCapture()
{
    return BeginAuthoredReticleCaptureInternal(true, false, true);
}
bool VR_CeAuthoredReticleFrameMatches(ID3D11DeviceContext* context,uint64_t serial)
{
    return TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&context&&context==g_context&&
        halo_ce::ReticleReceiptSerialCurrent(serial,g_preparedFrame.serial)&&g_preparedFrame.begun&&
        g_preparedFrame.state.shouldRender&&g_stereoEnabled.load(std::memory_order_acquire)&&
        !g_pausePresentation.load(std::memory_order_acquire);
}

#include "haloce_reticle_redirect.inl"

static bool EndAuthoredReticleCaptureInternal(
    bool allowFirstCaptureLog, bool expectAuthored)
{
    auto& saved = g_reticleCaptureState;
    if (!saved.active || !g_context)
        return false;

    const bool ceCapture=saved.cePrivateRaster;
    bool restored=true;
    bool restoreEntryRaster=true;
    if (ceCapture)
    {
        // The native descriptor owns its views. Replay the verified binder
        // while this HUD callback still retains CE; no borrowed COM release.
        saved.ceRestoring=true;
        // The binder's native setters now publish its real numeric state.
        // Crosshair suspension prevents the outer HUD affine during restore.
        HaloCEHudLayout_EndPrivateRaster();
        const auto result=HaloCEHudTarget_Restore(saved.ceTarget);
        restored=result==CeHudTargetRestoreResult::Unchanged&&saved.ceStateValid;
        restoreEntryRaster=result==CeHudTargetRestoreResult::Unchanged;
        if (result==CeHudTargetRestoreResult::Unavailable)
        {
            g_context->OMSetRenderTargets(0,nullptr,nullptr);
            HaloCEHudLayout_InvalidateState(g_context);
        }
    }
    else
        g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                      saved.rtvs, saved.dsv);
    if (restoreEntryRaster&&(saved.viewportCount||ceCapture))
        g_context->RSSetViewports(saved.viewportCount, saved.viewports);
    if (restoreEntryRaster&&(saved.scissorCount||ceCapture))
        g_context->RSSetScissorRects(saved.scissorCount, saved.scissors);
    saved.cePrivateRaster=false;
    saved.ceRestoring=false;
    saved.ceStateValid=false;
    saved.ceTarget={};

    if (!ceCapture)
    {
        for (auto*& rtv : saved.rtvs)
        {
            if (rtv) rtv->Release();
            rtv = nullptr;
        }
        if (saved.dsv)
        {
            saved.dsv->Release();
            saved.dsv = nullptr;
        }
    }
    saved.viewportCount = 0;
    saved.scissorCount = 0;
    saved.framingCaptured = false;
    const bool modeMatches = saved.publishesAuthored == expectAuthored;
    const bool publishedAuthored = saved.publishesAuthored;
    saved.active = false;
    saved.publishesAuthored = false;
    if (publishedAuthored&&restored)
        g_authoredReticleReady = true;
    if (ceCapture&&!restored) VR_InvalidatePreparedAuthoredReticleCapture();
    static bool logged = false;
    if (allowFirstCaptureLog && !logged)
    {
        LOG("M3: Halo authored per-weapon crosshair redirected to VR aim quad");
        logged = true;
    }
    return modeMatches&&restored;
}

void VR_EndAuthoredReticleCapture()
{
    (void)EndAuthoredReticleCaptureInternal(true, true);
}

bool VR_EndPreparedAuthoredReticleCapture()
{
    return EndAuthoredReticleCaptureInternal(false, true);
}

bool VR_PrepareAuthoredReticleSuppressionResources()
{
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    ReachExclusiveResourceLock lock(g_reachDisplayResourceLock);
#endif
    // This entry is Halo 4's final cold install gate. Unlike the legacy lazy
    // title paths, its optional hook must not arm without the coverage probe:
    // otherwise a first blank capture could be indistinguishable from valid
    // authored art and replace the visible procedural fallback. Failure here
    // leaves Halo 4's native CUI reticle wholly stock.
    return !g_reticleChainFailed.load(std::memory_order_acquire) &&
        VR_CanPrepareAuthoredReticleResources() && g_device && g_context &&
        g_authoredReticleProbeUsable && g_authoredReticleGoodTexture &&
        EnsureAuthoredReticleDiscardTexture();
}

bool VR_BeginPreparedAuthoredReticleSuppression()
{
    return BeginAuthoredReticleCaptureInternal(true, false, false);
}

bool VR_EndPreparedAuthoredReticleSuppression()
{
    return EndAuthoredReticleCaptureInternal(false, false);
}

void VR_SetReticleEnemy(bool enemy)
{
    g_reticleEnemy.store(enemy, std::memory_order_relaxed);
}

void VR_ObserveSecondaryWeaponPresentation(GameTitle title, uint32_t generation)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot < kTitleRuntimeSlotCount && generation &&
        TitleAdapter_GetActiveTitle() == title &&
        TitleAdapter_GetGeneration(title) == generation)
        g_secondaryWeaponPresentation[slot].Publish(generation, GetTickCount64());
}

bool VR_IsTwoHandAiming()
{
    return g_twoHandActive.load() && !SecondaryWeaponPresentationActive();
}

bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& snapshot)
{
    snapshot={};
    const uint32_t index=g_contactTrackingIndex.load(std::memory_order_seq_cst);
    if(index>=2) return false;
    uint32_t readers=g_contactTrackingStates[index].load(std::memory_order_seq_cst);
    if(readers>=64 || !g_contactTrackingStates[index].compare_exchange_strong(
            readers,readers+1,std::memory_order_seq_cst)) return false;
    bool valid=false;
    if(index==g_contactTrackingIndex.load(std::memory_order_seq_cst))
    {
        snapshot=g_contactTrackingSnapshots[index];
        valid=snapshot.serial && snapshot.serial==
            g_preparedSerialPublished.load(std::memory_order_acquire);
    }
    g_contactTrackingStates[index].fetch_sub(1,std::memory_order_seq_cst);
    if(!valid) snapshot={};
    return valid;
}

// The weapon-hand aim pose used by ALL aim consumers (bullet steering, the
// reticle, and the visible-gun barrel). Position is always the right hand.
// Orientation is the right controller's — UNLESS two-handed aim is engaged, in
// which case -Z is swung onto the line from the right hand to the left (support)
// hand, with roll kept from the right controller. Two-hand engages smoothly by
// pose (support hand up near the barrel line) so there is no button to hold.
bool VR_GetAimPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool okR = g_rightAimPoseValid;
    const XrPosef right = g_rightAimPose;
    const bool okL = g_leftAimPoseValid;
    const XrPosef left = g_leftAimPose;
    const XrPosef head = g_headPose;
    const bool stockHeadValid = g_headPoseValid && g_stockHeadPoseTime != 0 &&
        g_stockHeadPoseTime == g_stockControllerPoseTime;
    LeaveCriticalSection(&g_headCs);

    const AimPoseResult aim = ComputeAimPose(
        CurrentStockAimPoseInputs(okR, right, okL, left, stockHeadValid,
            head.position, head.orientation));
    if (!aim.updateTwoHandActivity)
        return false;

    // Preserve the getter's existing output contract: once the right pose is
    // valid, publish the best pose even if final quaternion validation fails.
    outQuat[0] = aim.pose.orientation.x;
    outQuat[1] = aim.pose.orientation.y;
    outQuat[2] = aim.pose.orientation.z;
    outQuat[3] = aim.pose.orientation.w;
    outPos[0] = aim.pose.position.x;
    outPos[1] = aim.pose.position.y;
    outPos[2] = aim.pose.position.z;

    if (aim.twoHandActive)
    {
        const bool wasActive = g_twoHandActive.exchange(true);
        if (!wasActive)
            LOG("M3: two-handed aim engaged (left grip held, hand on barrel)");
    }
    else
    {
        g_twoHandActive.store(false);
    }
    if (aim.rejectedExtreme)
    {
        static uint64_t lastRejectLogMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - lastRejectLogMs >= 2000)
        {
            LOG("M3: rejected extreme two-hand aim (ray agreement %.2f); "
                "using right controller", aim.rejectedAgreement);
            lastRejectLogMs = now;
        }
    }
    if (!aim.valid)
        return false;
    return true;
}

bool VR_GetPresentedReticleAimPose(
    float outQuat[4], float outPos[3], uint64_t& outSampleMs)
{
    if (!outQuat || !outPos)
        return false;
    auto& published = g_presentedReticleAimPose;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before =
            published.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const bool valid =
            published.valid.load(std::memory_order_relaxed) != 0;
        const uint64_t sampleMs =
            published.sampleMs.load(std::memory_order_relaxed);
        const float q[4] = {
            published.qx.load(std::memory_order_relaxed),
            published.qy.load(std::memory_order_relaxed),
            published.qz.load(std::memory_order_relaxed),
            published.qw.load(std::memory_order_relaxed)};
        const float p[3] = {
            published.px.load(std::memory_order_relaxed),
            published.py.load(std::memory_order_relaxed),
            published.pz.load(std::memory_order_relaxed)};
        if (published.sequence.load(std::memory_order_acquire) != before)
            continue;
        if (!valid || !sampleMs)
            return false;
        memcpy(outQuat, q, sizeof(q));
        memcpy(outPos, p, sizeof(p));
        outSampleMs = sampleMs;
        return true;
    }
    return false;
}

bool VR_GetLeftControllerPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool ok = g_leftAimPoseValid;
    if (ok)
    {
        outQuat[0] = g_leftAimPose.orientation.x;
        outQuat[1] = g_leftAimPose.orientation.y;
        outQuat[2] = g_leftAimPose.orientation.z;
        outQuat[3] = g_leftAimPose.orientation.w;
        outPos[0] = g_leftAimPose.position.x;
        outPos[1] = g_leftAimPose.position.y;
        outPos[2] = g_leftAimPose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

bool VR_GetEyeFov(int eye, float outFov[4])
{
    if (eye < 0 || eye > 1 || !outFov)
        return false;
    // xrLocateViews and the game render hook execute on the render thread in
    // this integration, so the latest located view is stable here.
    outFov[0] = g_views[eye].fov.angleLeft;
    outFov[1] = g_views[eye].fov.angleRight;
    outFov[2] = g_views[eye].fov.angleUp;
    outFov[3] = g_views[eye].fov.angleDown;
    return outFov[0] < 0.0f && outFov[1] > 0.0f &&
           outFov[2] > 0.0f && outFov[3] < 0.0f;
}

bool VR_GetGameRenderAspect(float& outAspect)
{
    if (!g_gameBackbufferDescValid || !g_gameBackbufferDesc.Width ||
        !g_gameBackbufferDesc.Height)
        return false;
    outAspect = static_cast<float>(g_gameBackbufferDesc.Width) /
                static_cast<float>(g_gameBackbufferDesc.Height);
    return isfinite(outAspect) && outAspect > 0.1f;
}

void VR_GetStatus(VrStatus& out)
{
    out = g_status;
}

bool VR_RoomscaleTrackingFresh() noexcept
{
    const auto at=g_roomscaleHeadSampleMs.load(std::memory_order_acquire);
    const auto now=GetTickCount64();
    return at && now>=at && now-at<=100 &&
        g_sessionStateShared.load(std::memory_order_acquire)==XR_SESSION_STATE_FOCUSED;
}
uint64_t VR_PhysicalCrouchEpoch() noexcept
{
    return (uint64_t(g_crouchCalibrationEpoch.load(std::memory_order_acquire))<<32)|
        uint32_t(g_contactSpaceEpoch.load(std::memory_order_acquire));
}
void VR_RecalibratePhysicalCrouch() noexcept
{g_crouchCalibrationEpoch.fetch_add(1,std::memory_order_acq_rel);}
