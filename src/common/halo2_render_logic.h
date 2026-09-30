#pragma once
#include "vr_interaction_refinement_logic.h"
#include "controller_finger_input.h"
#include "controller_finger_pose_logic.h"
#include "finger_joint_identity.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Halo 2-only render evidence and cold-observation policy. This header is
// deliberately pure: no Windows APIs, logging, allocation, hooks, or engine
// writes. Every address and layout below is derived from the official H2EK and
// verified against both pinned retail halo2.dll images; see
// docs/HALO2-SIGNATURE-EVIDENCE.md.

inline constexpr size_t kHalo2RetailFileSize = 15807960;
inline constexpr size_t kHalo2RetailImageSize = 0x02A38000;
inline constexpr uint32_t kHalo2RetailPeTimestamp = 0x68A0F0F2;

// H2EK units.cpp 0x480D40 selects damage_target{-1, breakable_index,
// structure_index, feature} from collision_result. The result tail is
// title-specific; these offsets were read from that official consumer and
// matched to retail collision_test_vector's 0x60-byte result.
struct Halo2BreakableSurfaceIdentity
{
    uint32_t type=0;
    int32_t structureIndex=-1;
    int32_t feature=-1;
    uint32_t breakableSurfaceIndex=UINT32_MAX;
    uint16_t material=UINT16_MAX;
    bool valid=false;
};

inline bool Halo2ReadBreakableSurfaceIdentity(const void* result,
    size_t resultBytes,Halo2BreakableSurfaceIdentity& out) noexcept
{
    out={};
    if(!result||resultBytes<0x60)return false;
    const auto* bytes=static_cast<const uint8_t*>(result);
    int32_t type=0;
    std::memcpy(&type,bytes,sizeof(type));
    // H2EK damage_target accepts collision types 1 and 3 as world geometry.
    if(type!=1&&type!=3)return false;
    if((bytes[0x58]&8u)==0)return false;
    int16_t material=-1;
    std::memcpy(&material,bytes+0x24,sizeof(material));
    if(material<0)return false;
    int32_t structure=-1,feature=-1;
    std::memcpy(&structure,bytes+0x3C,sizeof(structure));
    std::memcpy(&feature,bytes+0x50,sizeof(feature));
    out.type=static_cast<uint32_t>(type);
    out.structureIndex=structure;
    out.feature=feature;
    out.breakableSurfaceIndex=bytes[0x59];
    out.material=static_cast<uint16_t>(material);
    out.valid=true;
    return true;
}

inline constexpr const char* kHalo2RetailModuleSha256[] = {
    // Steam
    "DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946",
    // Microsoft Store / Xbox app (Game Pass)
    "81E5F41A7F8409D27A5454A28BFBECB8CD273E389366FB9865DD1D01E6BE689D",
};

inline constexpr char kHalo2KitBuildTag[] =
    "2023.06.20.176294.1-Release";
inline constexpr char kHalo2KitTagTestSha256[] =
    "D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36";

// C-H2-89: the official H2EK script surface exposes
// `sim_disable_aim_assist` as a boolean. Retail carries the same unique name
// and typed debug-global catalog entry. Runtime code resolves the live value
// slot by this name and exact type; these pure helpers pin the only two legal
// values and the teardown rule used by the optional override.
inline constexpr char kHalo2DisableAimAssistDebugVar[] =
    "sim_disable_aim_assist";
inline constexpr uint64_t kHalo2DebugVarTypeBoolean = 5;
// C-H2-89 headset result: retail's catalog record exists, but its value slot
// remains null in MCC gameplay, so the by-name transaction cannot apply.
// Keep the evidence-backed code dormant; do not retry this disproven path.
inline constexpr bool kHalo2DebugGlobalAimAssistOverrideEnabled = false;

inline constexpr bool Halo2AimAssistDebugValueValid(uint8_t value) noexcept
{
    return value <= 1;
}

inline constexpr uint8_t Halo2AimAssistDebugValue(
    bool vrOwnsGameplay, uint8_t stockValue) noexcept
{
    return vrOwnsGameplay ? uint8_t{1} : stockValue;
}

// E-H2-77 / C-H2-90: official H2EK aim_assist.cpp initializes these two
// result blocks at the start of its central aim-assist calculation. The
// pinned retail homolog has the identical three-argument ABI and field writes
// at +0x759260. Neutral output means no camera friction/adhesion and no target
// acquisition; bytes the engine does not initialize are deliberately left
// untouched here too.
inline constexpr uint32_t kHalo2AimAssistCalculateRva = 0x00759260;

// E-H2-80 / C-H2-92: official H2EK player_control.cpp +0x72C30 converts
// local-player desired angles into the view vector consumed once by the
// central aim-assist calculation. Its pinned retail homolog is +0x6C0DF0
// with the same (user index, float[3] output) ABI. The optional scoped hook
// substitutes the already-owned controller sight only during that central
// calculation, so Halo 2 can choose its native melee/lunge target along the
// hand ray without restoring camera adhesion.
inline constexpr uint32_t kHalo2AimAssistViewDirectionRva = 0x006C0DF0;
inline constexpr bool kHalo2ControllerAimAssistTargetingEnabled = true;

struct Halo2AimAssistTargetingResult
{
    uint32_t identifiers[3]{};
    uint8_t engineScratch[12]{};
    uint16_t flags = 0;
    uint16_t engineScratch1A = 0;
    float magnetismHorizontal = 0.0f;
    float magnetismVertical = 0.0f;
};

static_assert(sizeof(Halo2AimAssistTargetingResult) == 0x24);
static_assert(offsetof(Halo2AimAssistTargetingResult, flags) == 0x18);
static_assert(
    offsetof(Halo2AimAssistTargetingResult, magnetismHorizontal) == 0x1C);
static_assert(
    offsetof(Halo2AimAssistTargetingResult, magnetismVertical) == 0x20);

inline bool Halo2WriteNeutralAimAssistResults(
    float* control, Halo2AimAssistTargetingResult* targeting) noexcept
{
    if (!control || !targeting)
        return false;
    control[0] = 0.0f;
    control[1] = 0.0f;
    control[2] = 0.0f;
    targeting->identifiers[0] = UINT32_MAX;
    targeting->identifiers[1] = UINT32_MAX;
    targeting->identifiers[2] = UINT32_MAX;
    targeting->flags = 0;
    targeting->magnetismHorizontal = 0.0f;
    targeting->magnetismVertical = 0.0f;
    return true;
}

// E-H2-78 / C-H2-91: the headset-confirmed C-H2-90 camera fix proved that
// these three control outputs are the unwanted camera-assist transaction.
// Halo 2 separately carries the selected target into player_action's
// melee-target-unit field. Preserve that engine result so the existing
// controller-owned unit sight can select the normal melee/lunge target.
inline bool Halo2SuppressCameraAimAssist(float* control) noexcept
{
    if (!control)
        return false;
    control[0] = 0.0f;
    control[1] = 0.0f;
    control[2] = 0.0f;
    return true;
}

inline bool Halo2OverrideAimAssistViewDirection(
    bool scopedControllerTargeting, const float controllerDirection[3],
    float* engineDirection) noexcept
{
    if (!scopedControllerTargeting || !controllerDirection ||
        !engineDirection)
    {
        return false;
    }
    float lengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(controllerDirection[axis]))
            return false;
        lengthSquared += controllerDirection[axis] * controllerDirection[axis];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.999f ||
        lengthSquared > 1.001f)
    {
        return false;
    }
    std::memcpy(
        engineDirection, controllerDirection, 3 * sizeof(float));
    return true;
}

// C-H2-91 headset rejection: retaining the engine-selected target restored
// Halo 2's stock melee/lunge snap and made the player's camera turn/miss feel
// worse. Keep the proven implementation available as evidence, but never run
// it in a candidate descended from that result.
inline constexpr bool kHalo2RetainAimAssistTargetForMelee = false;

// Halo 2 does not yet own a title-native pause signal. A stale head-locked
// presentation can be inherited while switching in from another MCC engine,
// so clear that FOREIGN state exactly when H2 first enters its stereo claim
// context. Do not keep clearing while H2 remains active: Y+B uses the shared
// edge fallback to inject Start and intentionally requests the same head-locked
// presentation for Halo 2's own pause menu. The old continuous clear treated
// H2's own request as foreign and immediately kicked the headset back to stereo,
// leaving the pause menu visible only on the desktop.
inline bool Halo2MustClearForeignPause(
    bool stereoContext, bool enteringStereoContext, bool pauseTarget,
    bool pausePresentation)
{
    return stereoContext && enteringStereoContext &&
        (pauseTarget || pausePresentation);
}

inline bool Halo2ShouldRequestForeignPauseClear(
    bool stereoContext, bool enteringStereoContext, bool pauseTarget,
    bool pausePresentation, bool clearAlreadyRequested)
{
    return Halo2MustClearForeignPause(
               stereoContext, enteringStereoContext, pauseTarget,
               pausePresentation) &&
        !clearAlreadyRequested;
}

// C-H2-72: once the comfort fade has actually switched to Halo 2's
// head-locked pause presentation, the desktop/backbuffer is the intended
// source because it contains the native pause menu. The synchronous eye-pair
// safety policy may still carry a Claim/Drop from the fade edge; that must not
// suppress the deliberately requested flat pause screen. Pending pause targets
// do not qualify -- the opaque fade still protects that transition.
inline constexpr bool Halo2PausePresentationOwnsStockScreen(
    bool activeHalo2, bool pausePresentation) noexcept
{
    return activeHalo2 && pausePresentation;
}

// C-H2-73: Halo 2 still lacks a proven native pause boolean, but its official
// H2EK game_time_globals clock is already identity-proven and read coherently
// by the level-load gate. The Y+B fallback therefore treats a *frozen* native
// game-time clock as evidence that the displayed head-locked screen really is
// a paused simulation, then restores stereo only after that same native clock
// advances through two distinct ticks again. This deliberately does NOT key
// from A/B/menu buttons: those inputs can navigate Settings and other pause
// submenus without resuming gameplay.
inline constexpr uint8_t kHalo2PauseFreezeConfirmSamples = 4;
inline constexpr uint8_t kHalo2PauseResumeTickChanges = 2;

struct Halo2PauseResumeClock
{
    uint32_t lastTick = 0;
    bool haveTick = false;
    bool frozenObserved = false;
    uint8_t stableSamples = 0;
    uint8_t advancingTicks = 0;

    void Reset() noexcept
    {
        lastTick = 0;
        stableSamples = 0;
        advancingTicks = 0;
        haveTick = false;
        frozenObserved = false;
    }

    bool Update(
        bool activeHalo2, bool pausePresentation, bool sampleValid,
        bool initialized, uint32_t tick) noexcept
    {
        if (!activeHalo2 || !pausePresentation || !sampleValid || !initialized)
        {
            Reset();
            return false;
        }

        if (!haveTick)
        {
            lastTick = tick;
            stableSamples = 0;
            advancingTicks = 0;
            haveTick = true;
            frozenObserved = false;
            return false;
        }

        if (tick == lastTick)
        {
            if (!frozenObserved)
            {
                if (stableSamples < UINT8_MAX)
                    ++stableSamples;
                if (stableSamples >= kHalo2PauseFreezeConfirmSamples)
                    frozenObserved = true;
            }
            // Once a genuine freeze has been witnessed, repeated samples at
            // the same newly-advanced tick do not erase progress. At H2's
            // 30-Hz simulation and a different headset cadence, duplicates
            // between two distinct resumed ticks are normal.
            return false;
        }

        lastTick = tick;
        stableSamples = 0;
        if (!frozenObserved)
        {
            advancingTicks = 0;
            return false;
        }

        if (advancingTicks < UINT8_MAX)
            ++advancingTicks;
        if (advancingTicks < kHalo2PauseResumeTickChanges)
            return false;

        Reset();
        return true;
    }
};

inline constexpr float kHalo2MinimumAppCadenceHz = 72.0f;
inline constexpr float kHalo2MaximumAppCadenceHz = 144.0f;

inline bool Halo2RefreshCadenceSupported(float appCadenceHz)
{
    return std::isfinite(appCadenceHz) &&
        appCadenceHz >= kHalo2MinimumAppCadenceHz &&
        appCadenceHz <= kHalo2MaximumAppCadenceHz;
}

inline constexpr uint64_t kHalo2NanosecondsPerSecond = 1000000000ull;
// XrDuration is an integer count of nanoseconds. The prepared-frame cadence
// is ADVISORY: it only proves the runtime is alive and pacing (a period
// between 1000 Hz and 4 Hz). It used to demand 72-144 Hz on both the
// xrWaitFrame target and the predicted-display delta to keep a half-rate
// alternate-eye mode out; same-frame stereo makes that mode impossible by
// construction, and on 2026-08-21 the strict band blocked 22 times in two
// minutes (the game at 64-70 fps, SteamVR throttling to 30 Hz) and put the
// player on the flat screen each time - 864 of Anniversary's 920 stock
// passes. A slow frame now still gets its true per-eye pair; the
// compositor reprojects it.
inline constexpr uint64_t kHalo2FastestCadencePeriodNs = 1000000ull;
inline constexpr uint64_t kHalo2SlowestCadencePeriodNs = 250000000ull;

inline constexpr bool Halo2CadencePeriodSupported(uint64_t periodNs) noexcept
{
    return periodNs >= kHalo2FastestCadencePeriodNs &&
        periodNs <= kHalo2SlowestCadencePeriodNs;
}

inline constexpr bool Halo2PreparedCadenceSupported(
    uint64_t targetPeriodNs, uint64_t /*predictedDisplayDeltaNs*/) noexcept
{
    // The predicted-display delta between two prepared frames is NOT a
    // liveness signal: the runtime legitimately predicts the same display
    // time twice (a delta of nanoseconds, logged as 1e9 Hz) and skips
    // display periods. Only the xrWaitFrame target period says whether the
    // runtime is pacing at all.
    return Halo2CadencePeriodSupported(targetPeriodNs);
}

inline float Halo2CadenceHz(uint64_t periodNs) noexcept
{
    return periodNs
        ? static_cast<float>(
              static_cast<double>(kHalo2NanosecondsPerSecond) /
              static_cast<double>(periodNs))
        : 0.0f;
}

inline bool Halo2PresentationMayClaim(
    bool stereoContext, bool coreUsable, bool presentationIntended,
    bool pauseTarget, bool pausePresentation, uint64_t targetPeriodNs,
    uint64_t predictedDisplayDeltaNs)
{
    return stereoContext && coreUsable && presentationIntended &&
        !pauseTarget && !pausePresentation &&
        Halo2PreparedCadenceSupported(
            targetPeriodNs, predictedDisplayDeltaNs);
}

// Forward progress only. This used to demand exactly previousCompleted + 1,
// which was wrong: OpenXR prepared serials advance at the HEADSET's rate while
// the game renders at its own. A title running at 34 fps against a 90 Hz panel
// skips prepared serials as a matter of course, and the strict rule quarantined
// Halo 2 stereo 140 ms after its first accepted pair - observed 2026-08-21,
// "expected prepared serial 7031 after the first complete pair, saw 7038".
//
// A gap is not half-rate stereo. Half-rate is prevented by the two guarantees
// that actually describe it: both eyes must be freshly rendered inside ONE game
// frame at the CURRENT prepared serial, and the cadence gate must hold. What
// must still be refused is going backwards or repeating a serial, because that
// is what a stale or reused eye looks like.
inline constexpr bool Halo2PreparedSerialMayFollowCompletedPair(
    uint64_t lastCompletedSerial, uint64_t preparedSerial) noexcept
{
    if (!preparedSerial)
        return false;
    if (!lastCompletedSerial)
        return true;
    return lastCompletedSerial != UINT64_MAX &&
        preparedSerial > lastCompletedSerial;
}

// A completed engine eye is not the only point of no safe retry. If an owned
// camera span cannot be restored, or an H2 raster scope cannot be closed, the
// current frame may stay screen-visible but this module generation must not
// attempt another title transaction. A clean zero-eye failure remains eligible
// for the proven stock replay.
inline constexpr bool Halo2StructuralFailureRequiresQuarantine(
    uint32_t renderViewCalls, bool ownedStateRestoreFailed,
    bool rasterScopeCloseFailed, bool transactionExceptionSeen,
    bool transactionShapeFailed) noexcept
{
    return renderViewCalls > 0 || ownedStateRestoreFailed ||
        rasterScopeCloseFailed || transactionExceptionSeen ||
        transactionShapeFailed;
}

// The engine stores one heap-allocated 0x2C-byte game_time_globals object
// behind this module pointer slot. The official H2EK establishes the layout;
// retail's unique incrementer and level initializer independently decode the
// same slot.
inline constexpr uint32_t kHalo2GameTimeSlotRva = 0x015FE008;
inline constexpr uint32_t kHalo2GameTimeGlobalsSize = 0x2C;
inline constexpr uint32_t kHalo2GameTimeInitializedOffset = 0x00;
inline constexpr uint32_t kHalo2GameTimeTickRateOffset = 0x02;
inline constexpr uint32_t kHalo2GameTimeSecondsPerTickOffset = 0x04;
inline constexpr uint32_t kHalo2GameTimeCurrentTickOffset = 0x08;

// Exact H2EK-to-retail render transaction. Win64 passes `window` in RCX and
// the byte flag in low DL. The entry has one other retail caller, so entry-AOB
// uniqueness alone never authorizes a stereo transaction: the detour must also
// see the ordinary render-frame return address and the primary player index.
using Halo2RenderPlayerWindowFn = void (*)(void* window, uint8_t flag);
inline constexpr uint32_t kHalo2KitRenderPlayerWindowRva = 0x0029EAD0;
inline constexpr uint32_t kHalo2RetailRenderPlayerWindowRva = 0x007E2130;
inline constexpr uint32_t kHalo2RetailRenderPlayerWindowCallRva = 0x007E1706;
inline constexpr uint32_t kHalo2RetailRenderPlayerWindowReturnRva = 0x007E170B;
inline constexpr uint32_t kHalo2RetailRenderPlayerWindowOtherCallRva =
    0x007F0A60;
inline constexpr uint32_t kHalo2KitRenderViewRva = 0x002A0160;
inline constexpr uint32_t kHalo2RetailRenderViewRva = 0x007E30D0;
inline constexpr uint32_t kHalo2RetailRenderViewCallRva = 0x007E2412;
inline constexpr uint32_t kHalo2RetailRenderViewReturnRva = 0x007E2417;
// H2EK's final postprocess helper selects the swapchain backbuffer RTV. The
// retail homolog loads the same pointer slot here; C-H2-3 resolves it from a
// unique full-image signature and uses only the resulting exact RTV pointer in
// the OM hot hook.
inline constexpr uint32_t kHalo2RetailFinalOutputRtvLoadRva = 0x00975297;
inline constexpr uint32_t kHalo2RetailFinalOutputRtvSlotRva = 0x0197EE58;

// Official H2EK: chud_draw_screen at +0x2EA955 is called with
// (player/window selector, draw selector, rectangle*). Retail's homolog has
// the same three-argument contract at +0x7FFD70. The Stage 3B visibility test
// incorrectly called it as a one-argument function with a null rectangle;
// these entry bytes and the complete ABI are the proof required before the
// native HUD is replayed into an eye.
inline constexpr uint32_t kHalo2KitNativeChudDrawRva = 0x002EA955;
inline constexpr uint32_t kHalo2RetailNativeChudDrawRva = 0x007FFD70;
inline constexpr uint8_t kHalo2RetailNativeChudDrawEntryBytes[] = {
    0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x50, 0x4D, 0x8B, 0xF0, 0x8B, 0xF2, 0x8B, 0xD9};

// H2EK +0x30C365 and retail +0x829490 are the semantic pair that resolves a
// new-HUD widget anchor to its screen-space float x/y basis. The official tags
// name the categories (health/shield, weapon HUD, motion sensor, crosshair).
// Hooking this returned point is what makes the shared HUD layout controls
// affect native widgets; chud_draw_screen's rectangle is not that basis.
inline constexpr uint32_t kHalo2KitHudAnchorBasisRva = 0x0030C365;
inline constexpr uint32_t kHalo2RetailHudAnchorBasisRva = 0x00829490;
inline constexpr uint8_t kHalo2RetailHudAnchorBasisEntryBytes[] = {
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x48, 0x8D, 0x68,
    0xA1, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00,
    0x48, 0x89, 0x70, 0x08, 0x49, 0x8B, 0xD8};

// Retail camera/window facts. The two cameras occupy adjacent 0x74-byte
// records inside a 0x120-byte window. C-H2-3 owns only their three 12-byte pose
// vectors plus the 4-byte vertical-FOV cover, restoring every field it writes.
inline constexpr uint32_t kHalo2RetailWindowStride = 0x120;
inline constexpr uint32_t kHalo2WindowTypeOffset = 0x00;
inline constexpr uint32_t kHalo2WindowPlayerIndexOffset = 0x04;
inline constexpr uint32_t kHalo2WindowOutputUserOffset = 0x08;
inline constexpr uint32_t kHalo2RenderCameraOffset = 0x0C;
inline constexpr uint32_t kHalo2RasterCameraOffset = 0x80;
inline constexpr uint32_t kHalo2WindowTrailingViewArgumentOffset = 0xF8;
inline constexpr uint32_t kHalo2CameraBytes = 0x74;
inline constexpr uint32_t kHalo2CameraPositionOffset = 0x00;
inline constexpr uint32_t kHalo2CameraForwardOffset = 0x0C;
inline constexpr uint32_t kHalo2CameraUpOffset = 0x18;
inline constexpr uint32_t kHalo2CameraVectorBytes = 0x0C;
inline constexpr uint32_t kHalo2CameraVerticalFovOffset = 0x28;
inline constexpr uint32_t kHalo2CameraViewportRectangleOffset = 0x30;
inline constexpr uint32_t kHalo2CameraWindowRectangleOffset = 0x38;
inline constexpr uint32_t kHalo2CameraRectangleBytes = 0x08;
inline constexpr uint32_t kHalo2RectangleY0Offset = 0x00;
inline constexpr uint32_t kHalo2RectangleX0Offset = 0x02;
inline constexpr uint32_t kHalo2RectangleY1Offset = 0x04;
inline constexpr uint32_t kHalo2RectangleX1Offset = 0x06;
inline constexpr uint32_t kHalo2CameraZNearOffset = 0x40;
inline constexpr uint32_t kHalo2CameraZFarOffset = 0x44;
inline constexpr uint32_t kHalo2CameraAsymmetricEnableOffset = 0x58;
inline constexpr uint32_t kHalo2CameraFrustumCenterXOffset = 0x5C;
inline constexpr uint32_t kHalo2CameraFrustumCenterYOffset = 0x60;
inline constexpr uint32_t kHalo2CameraFrustumExtentScaleOffset = 0x64;
inline constexpr uint32_t kHalo2CameraPixelOffsetEnableOffset = 0x68;
inline constexpr uint32_t kHalo2CameraPixelOffsetXOffset = 0x6C;
inline constexpr uint32_t kHalo2CameraPixelOffsetYOffset = 0x70;

inline constexpr uint32_t kHalo2WindowRenderPositionOffset =
    kHalo2RenderCameraOffset + kHalo2CameraPositionOffset;
inline constexpr uint32_t kHalo2WindowRasterPositionOffset =
    kHalo2RasterCameraOffset + kHalo2CameraPositionOffset;
inline constexpr uint32_t kHalo2WindowRenderForwardOffset =
    kHalo2RenderCameraOffset + kHalo2CameraForwardOffset;
inline constexpr uint32_t kHalo2WindowRenderUpOffset =
    kHalo2RenderCameraOffset + kHalo2CameraUpOffset;
inline constexpr uint32_t kHalo2WindowRasterForwardOffset =
    kHalo2RasterCameraOffset + kHalo2CameraForwardOffset;
inline constexpr uint32_t kHalo2WindowRasterUpOffset =
    kHalo2RasterCameraOffset + kHalo2CameraUpOffset;
inline constexpr uint32_t kHalo2WindowRenderVerticalFovOffset =
    kHalo2RenderCameraOffset + kHalo2CameraVerticalFovOffset;
inline constexpr uint32_t kHalo2WindowRasterVerticalFovOffset =
    kHalo2RasterCameraOffset + kHalo2CameraVerticalFovOffset;

static_assert(kHalo2RenderCameraOffset + kHalo2CameraBytes ==
    kHalo2RasterCameraOffset);
static_assert(kHalo2RasterCameraOffset + kHalo2CameraBytes <=
    kHalo2RetailWindowStride);
static_assert(kHalo2CameraPixelOffsetYOffset + sizeof(float) ==
    kHalo2CameraBytes);

// ---------------------------------------------------------------------------
// E-H2-12: the projection the classic renderer actually rasterised with.
// render_view 0x7E30D0 copies the raster camera (window+0x80, r8) into a
// 0x318-byte raster context block (camera at +0x18), builds that block's
// projection at +0x100 with 0x7DF7A0 (P[0][0] = 1/(aspect*tan(fov/2)) at
// +0x78, P[1][1] = 1/tan(fov/2) at +0x8C; the builder reads camera +0x28
// directly, no further FOV scale) and commits it with 0x955EC0: the int32
// depth at 0xE19208 is incremented and the block is copied verbatim into
// slot[depth] of the stack at 0x1996D30. 0x955F60 pops (depth -= 1) without
// clearing the slot, then rebuilds g_projection 0x165C2D4 from the RESTORED
// outer camera - so g_projection is the wrong thing to read after the call.
// slot[depth + 1] still holds the eye pass's raster camera and the projection
// the engine built from it; the camera bytes identify the slot as this eye's.
// ---------------------------------------------------------------------------
inline constexpr uint32_t kHalo2ClassicRasterContextDepthRva = 0x00E19208;
inline constexpr uint32_t kHalo2ClassicRasterContextStackRva = 0x01996D30;
inline constexpr uint32_t kHalo2ClassicRasterContextStride = 0x318;
inline constexpr int32_t kHalo2ClassicRasterContextCapacity = 3;
inline constexpr uint32_t kHalo2ClassicRasterContextCameraOffset = 0x18;
inline constexpr uint32_t kHalo2ClassicRasterContextProjectionOffset = 0x100;
inline constexpr uint32_t kHalo2ClassicProjectionScaleXOffset = 0x78;
inline constexpr uint32_t kHalo2ClassicProjectionScaleYOffset = 0x8C;
// The stack ends before the window array (E-H2-3: RVA 0x19976E0).
static_assert(kHalo2ClassicRasterContextStackRva +
    static_cast<uint32_t>(kHalo2ClassicRasterContextCapacity) *
        kHalo2ClassicRasterContextStride <= 0x019976E0);
static_assert(kHalo2ClassicRasterContextCameraOffset + kHalo2CameraBytes <=
    kHalo2ClassicRasterContextProjectionOffset);
static_assert(kHalo2ClassicRasterContextProjectionOffset +
    kHalo2ClassicProjectionScaleYOffset + sizeof(float) <=
    kHalo2ClassicRasterContextStride);

// The slot the engine just popped: depth after render_view returned, plus
// one. A depth below -1 or a slot past the three-entry stack is refused.
inline bool Halo2ClassicPoppedRasterContextSlot(
    int32_t depthAfterPop, uint32_t& slotRva) noexcept
{
    // Compared, not added: depth + 1 would overflow for a garbage depth.
    if (depthAfterPop < -1 ||
        depthAfterPop >= kHalo2ClassicRasterContextCapacity - 1)
    {
        return false;
    }
    slotRva = kHalo2ClassicRasterContextStackRva +
        static_cast<uint32_t>(depthAfterPop + 1) *
            kHalo2ClassicRasterContextStride;
    return true;
}

// Halo 2-specific metric evidence, not an inherited constant. H2EK's unique
// 3.048f is consumed by a source-backed metre/kilometre formatter; retail has
// the same unique constant and behavior, plus the unique reciprocal.
inline constexpr float kHalo2MetersPerWorldUnit = 3.048f;
inline constexpr float kHalo2WorldUnitsPerMeter =
    1.0f / kHalo2MetersPerWorldUnit;
// OpenXR eye poses are only centimetres from the midpoint. This deliberately
// generous bound turns a corrupt/torn runtime pose into a stock frame instead
// of allowing an unbounded engine-camera write.
inline constexpr float kHalo2MaxEyeOffsetMeters = 0.5f;
// Room-scale lean is CLAMPED per axis to this, never rejected: Halo 3,
// Reach and Halo 4 all clamp, and a rejected sample here costs the frame
// its stereo pair instead of a few centimetres of lean.
inline constexpr float kHalo2MaxHeadTranslationMeters = 4.0f;
// Sanity bound on the ABSOLUTE tracked head position (a torn or corrupt
// pose), deliberately far beyond any play space so a long walk from the
// tracking origin never disables the headset camera.
inline constexpr float kHalo2MaxHeadPositionMeters = 64.0f;
inline constexpr float kHalo2MaxHeadTranslationWorldUnits = 1.5f;
inline constexpr uint32_t kHalo2KitMetersPerWorldUnitRva = 0x007AD4F8;
inline constexpr uint32_t kHalo2RetailMetersPerWorldUnitFileOffset =
    0x00B13AF4;
inline constexpr uint32_t kHalo2RetailMetersPerWorldUnitRva = 0x00B14CF4;
inline constexpr uint32_t kHalo2RetailWorldUnitsPerMeterFileOffset =
    0x00B5B754;
inline constexpr uint32_t kHalo2RetailWorldUnitsPerMeterRva = 0x00B5C954;

inline constexpr int kHalo2LeftEye = 0;
inline constexpr int kHalo2RightEye = 1;
inline constexpr int kHalo2EyeCount = 2;

// Exact first 0x24 bytes of either retail camera. Projection fields remain
// separate so the scoped restore can never overwrite the engine-owned z_far.
struct Halo2CameraBasis
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
};
static_assert(sizeof(Halo2CameraBasis) == 0x24);
static_assert(offsetof(Halo2CameraBasis, position) ==
    kHalo2CameraPositionOffset);
static_assert(offsetof(Halo2CameraBasis, forward) ==
    kHalo2CameraForwardOffset);
static_assert(offsetof(Halo2CameraBasis, up) == kHalo2CameraUpOffset);

// H2 camera producers supply an orthonormal basis. A torn or corrupt read must
// never turn an otherwise bounded IPD into arbitrary engine memory writes.
inline bool Halo2ValidateCameraBasis(const Halo2CameraBasis& basis) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(basis.position[axis]) ||
            !std::isfinite(basis.forward[axis]) ||
            !std::isfinite(basis.up[axis]))
        {
            return false;
        }
    }

    const float forwardLengthSquared =
        basis.forward[0] * basis.forward[0] +
        basis.forward[1] * basis.forward[1] +
        basis.forward[2] * basis.forward[2];
    const float upLengthSquared =
        basis.up[0] * basis.up[0] + basis.up[1] * basis.up[1] +
        basis.up[2] * basis.up[2];
    const float forwardUpDot =
        basis.forward[0] * basis.up[0] +
        basis.forward[1] * basis.up[1] +
        basis.forward[2] * basis.up[2];
    return std::isfinite(forwardLengthSquared) &&
        std::isfinite(upLengthSquared) && std::isfinite(forwardUpDot) &&
        std::fabs(forwardLengthSquared - 1.0f) < 0.05f &&
        std::fabs(upLengthSquared - 1.0f) < 0.05f &&
        std::fabs(forwardUpDot) < 0.05f;
}

struct Halo2TemporalEyePositions
{
    float render[3]{};
    float raster[3]{};
};

// eyePositionMeters is the selected OpenXR view origin relative to the stereo
// midpoint (+X right, +Y up, -Z forward). Both engine cameras are displaced in
// their own validated basis so visibility and raster consumers stay coherent:
//   delta = (cross(forward,up)*x + up*y - forward*z) / 3.048.
// Failure leaves `out` untouched.
inline bool Halo2BuildTemporalEyePositions(
    const Halo2CameraBasis& renderCamera,
    const Halo2CameraBasis& rasterCamera,
    const float eyePositionMeters[3], Halo2TemporalEyePositions& out) noexcept
{
    if (!eyePositionMeters || !Halo2ValidateCameraBasis(renderCamera) ||
        !Halo2ValidateCameraBasis(rasterCamera))
    {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(eyePositionMeters[axis]) ||
            std::fabs(eyePositionMeters[axis]) > kHalo2MaxEyeOffsetMeters)
            return false;

    Halo2TemporalEyePositions candidate{};
    const auto buildPosition = [&](const Halo2CameraBasis& camera,
                                   float position[3]) noexcept {
        const float right[3] = {
            camera.forward[1] * camera.up[2] -
                camera.forward[2] * camera.up[1],
            camera.forward[2] * camera.up[0] -
                camera.forward[0] * camera.up[2],
            camera.forward[0] * camera.up[1] -
                camera.forward[1] * camera.up[0]};
        for (int axis = 0; axis < 3; ++axis)
        {
            position[axis] = camera.position[axis] +
                (right[axis] * eyePositionMeters[0] +
                 camera.up[axis] * eyePositionMeters[1] -
                 camera.forward[axis] * eyePositionMeters[2]) *
                    kHalo2WorldUnitsPerMeter;
            if (!std::isfinite(position[axis]))
                return false;
        }
        return true;
    };
    if (!buildPosition(renderCamera, candidate.render) ||
        !buildPosition(rasterCamera, candidate.raster))
    {
        return false;
    }
    out = candidate;
    return true;
}

// Retail stores both camera rectangles as signed 16-bit y0/x0/y1/x1. The
// projection builder treats +0x28 as a full vertical FOV in radians.
struct Halo2CameraRectangle
{
    int16_t y0 = 0;
    int16_t x0 = 0;
    int16_t y1 = 0;
    int16_t x1 = 0;
};
static_assert(sizeof(Halo2CameraRectangle) == kHalo2CameraRectangleBytes);
static_assert(offsetof(Halo2CameraRectangle, y0) == kHalo2RectangleY0Offset);
static_assert(offsetof(Halo2CameraRectangle, x0) == kHalo2RectangleX0Offset);
static_assert(offsetof(Halo2CameraRectangle, y1) == kHalo2RectangleY1Offset);
static_assert(offsetof(Halo2CameraRectangle, x1) == kHalo2RectangleX1Offset);

struct Halo2SymmetricHalfFovs
{
    float horizontal = 0.0f;
    float vertical = 0.0f;
};

// Half-angles from the two diagonal scales of a perspective projection:
// P[0][0] = 1/tan(halfX), P[1][1] = 1/tan(halfY). Both Halo 2 renderers bake
// them exactly this way (classic 0x7DF7A0, Saber 0x1C82C0), so reading the
// matrix the engine built gives the frustum it rasterised with, independent
// of whether it honoured the camera field the mod wrote. Sign is irrelevant.
inline bool Halo2HalfFovsFromProjectionScales(
    float scaleX, float scaleY, Halo2SymmetricHalfFovs& out) noexcept
{
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY))
        return false;
    const float magnitudeX = std::fabs(scaleX);
    const float magnitudeY = std::fabs(scaleY);
    // 1/1000 is a half-angle of 89.94 degrees: not a frustum.
    if (magnitudeX < 1.0e-3f || magnitudeY < 1.0e-3f)
        return false;
    const float halfX = std::atan(1.0f / magnitudeX);
    const float halfY = std::atan(1.0f / magnitudeY);
    if (!std::isfinite(halfX) || !std::isfinite(halfY) ||
        halfX <= 0.01f || halfY <= 0.01f || halfX >= 1.55f || halfY >= 1.55f)
    {
        return false;
    }
    out = {halfX, halfY};
    return true;
}

inline bool Halo2HalfFovsAgree(
    const Halo2SymmetricHalfFovs& left, const Halo2SymmetricHalfFovs& right,
    float toleranceRadians) noexcept
{
    return std::isfinite(toleranceRadians) && toleranceRadians >= 0.0f &&
        std::fabs(left.horizontal - right.horizontal) <= toleranceRadians &&
        std::fabs(left.vertical - right.vertical) <= toleranceRadians;
}

// Written cover versus engine-held projection, by more than half a degree on
// either axis, is the read-back disagreement the log must name.
inline constexpr float kHalo2ProjectionReadbackToleranceRadians = 0.0087f;

// This is only truthful while both native off-center controls are disabled.
// The generic rectangle parameter lets the caller use the camera's proven
// viewport rectangle without inventing a packed engine-camera C++ type.
inline bool Halo2DeriveSymmetricHalfFovs(
    float verticalFov, const Halo2CameraRectangle& rectangle,
    Halo2SymmetricHalfFovs& out) noexcept
{
    constexpr float kPi = 3.14159265f;
    if (!std::isfinite(verticalFov) || verticalFov <= 1.0e-4f ||
        verticalFov >= kPi - 1.0e-4f)
    {
        return false;
    }
    const int32_t width =
        static_cast<int32_t>(rectangle.x1) - rectangle.x0;
    const int32_t height =
        static_cast<int32_t>(rectangle.y1) - rectangle.y0;
    if (width <= 0 || height <= 0)
        return false;

    const float aspect =
        static_cast<float>(width) / static_cast<float>(height);
    const float halfVertical = verticalFov * 0.5f;
    const float verticalTangent = std::tan(halfVertical);
    const float halfHorizontal = std::atan(verticalTangent * aspect);
    if (!std::isfinite(aspect) || aspect <= 0.0f ||
        !std::isfinite(verticalTangent) || verticalTangent <= 0.0f ||
        !std::isfinite(halfHorizontal) || halfHorizontal <= 0.0f ||
        halfHorizontal >= kPi * 0.5f)
    {
        return false;
    }
    out = {halfHorizontal, halfVertical};
    return true;
}

constexpr bool Halo2StockProjectionIsSymmetric(
    uint8_t renderAsymmetricEnable, uint8_t renderPixelOffsetEnable,
    uint8_t rasterAsymmetricEnable, uint8_t rasterPixelOffsetEnable) noexcept
{
    return renderAsymmetricEnable == 0 && renderPixelOffsetEnable == 0 &&
        rasterAsymmetricEnable == 0 && rasterPixelOffsetEnable == 0;
}

// Serial parity, not callback count, owns the eye. Prepared serial zero is an
// invalid/unpublished frame. `rightEyeFirst` flips the odd/even assignment in
// the same way as the established title setting.
constexpr int Halo2TemporalEyeForSerial(
    uint64_t preparedSerial, bool rightEyeFirst) noexcept
{
    if (preparedSerial == 0)
        return -1;
    const int oddSerialEye = rightEyeFirst ? kHalo2RightEye : kHalo2LeftEye;
    return (preparedSerial & 1u) != 0 ? oddSerialEye : 1 - oddSerialEye;
}

struct Halo2TemporalEyeStamp
{
    uint32_t generation = 0;
    uint64_t preparedSerial = 0;
    int eye = -1;
    bool complete = false;
};

constexpr bool Halo2TemporalEyeStampValid(
    const Halo2TemporalEyeStamp& stamp, bool rightEyeFirst) noexcept
{
    return stamp.complete && stamp.generation != 0 &&
        stamp.preparedSerial != 0 && stamp.eye >= kHalo2LeftEye &&
        stamp.eye <= kHalo2RightEye &&
        stamp.eye == Halo2TemporalEyeForSerial(
            stamp.preparedSerial, rightEyeFirst);
}

enum class Halo2TemporalPairAction : uint8_t
{
    RejectCurrent = 0,
    SeedWithCurrent,
    PublishAdjacentPair,
};

// `previous` is the opposite-eye cache before `current` is committed. Current
// must match the caller's exact active generation and prepared serial. A first
// eye, generation boundary, or forward serial gap becomes a clean new seed;
// replay/out-of-order input is rejected. Only exactly N-1/N, opposite-parity
// completed eyes may publish.
constexpr Halo2TemporalPairAction SelectHalo2TemporalPairAction(
    const Halo2TemporalEyeStamp& previous,
    const Halo2TemporalEyeStamp& current, uint32_t activeGeneration,
    uint64_t expectedCurrentSerial, bool rightEyeFirst) noexcept
{
    if (!activeGeneration || !expectedCurrentSerial ||
        !Halo2TemporalEyeStampValid(current, rightEyeFirst) ||
        current.generation != activeGeneration ||
        current.preparedSerial != expectedCurrentSerial)
    {
        return Halo2TemporalPairAction::RejectCurrent;
    }
    if (!Halo2TemporalEyeStampValid(previous, rightEyeFirst) ||
        previous.generation != current.generation)
    {
        return Halo2TemporalPairAction::SeedWithCurrent;
    }
    if (previous.preparedSerial >= current.preparedSerial)
        return Halo2TemporalPairAction::RejectCurrent;
    if (current.preparedSerial - previous.preparedSerial != 1 ||
        previous.eye == current.eye)
    {
        return Halo2TemporalPairAction::SeedWithCurrent;
    }
    return Halo2TemporalPairAction::PublishAdjacentPair;
}

enum class Halo2CameraPositionWrite : uint8_t
{
    Reject = 0,
    RenderPosition,
    RasterPosition,
};

// A mechanical allow-list for the only two engine writes C-H2-2 owns.
constexpr Halo2CameraPositionWrite SelectHalo2CameraPositionWrite(
    uint32_t windowRelativeOffset, size_t bytes) noexcept
{
    if (bytes != kHalo2CameraVectorBytes)
        return Halo2CameraPositionWrite::Reject;
    if (windowRelativeOffset == kHalo2WindowRenderPositionOffset)
        return Halo2CameraPositionWrite::RenderPosition;
    if (windowRelativeOffset == kHalo2WindowRasterPositionOffset)
        return Halo2CameraPositionWrite::RasterPosition;
    return Halo2CameraPositionWrite::Reject;
}

enum class Halo2TemporalTransactionAction : uint8_t
{
    CallStockOnce = 0,
    RejectTemporalFrameAndCallStockOnce,
    ScopedPositionsAndCallOnce,
};

// Everything the hot detour must prove before touching either position. A
// foreign caller is simply stock; a malformed transaction on the exact player
// edge revokes that temporal frame but never disarms the title core.
struct Halo2TemporalTransactionInput
{
    bool stereoRequested = false;
    bool hookArmed = false;
    bool coldObservationPassed = false;
    bool exactCaller = false;
    bool flagValid = false;
    bool windowReadable = false;
    int32_t playerIndex = -1;
    bool levelLive = false;
    bool captureReady = false;
    bool teardownRequested = false;
    bool serialAlreadyClaimed = false;
    uint32_t activeGeneration = 0;
    uint32_t snapshotGeneration = 0;
    uint64_t preparedSerial = 0;
    int eye = -1;
    bool rightEyeFirst = false;
    bool renderCameraValid = false;
    bool rasterCameraValid = false;
    bool eyePositionValid = false;
    bool stockProjectionSymmetric = false;
    bool halfFovsValid = false;
};

constexpr Halo2TemporalTransactionAction SelectHalo2TemporalTransactionAction(
    const Halo2TemporalTransactionInput& input) noexcept
{
    if (!input.stereoRequested || !input.hookArmed)
        return Halo2TemporalTransactionAction::CallStockOnce;
    if (!input.exactCaller)
        return Halo2TemporalTransactionAction::CallStockOnce;
    if (!input.coldObservationPassed || !input.flagValid ||
        !input.windowReadable ||
        input.playerIndex != 0 || !input.levelLive || !input.captureReady ||
        input.teardownRequested || input.serialAlreadyClaimed ||
        input.activeGeneration == 0 ||
        input.snapshotGeneration != input.activeGeneration ||
        input.preparedSerial == 0 || input.eye < kHalo2LeftEye ||
        input.eye > kHalo2RightEye ||
        input.eye != Halo2TemporalEyeForSerial(
            input.preparedSerial, input.rightEyeFirst) ||
        !input.renderCameraValid || !input.rasterCameraValid ||
        !input.eyePositionValid || !input.stockProjectionSymmetric ||
        !input.halfFovsValid)
    {
        return Halo2TemporalTransactionAction::
            RejectTemporalFrameAndCallStockOnce;
    }
    return Halo2TemporalTransactionAction::ScopedPositionsAndCallOnce;
}

// Publication is permitted only after one (never zero or two) original call,
// successful return, both selective writes, and byte-restoration of both saved
// positions. Whole-camera restore or any other camera write fails this proof.
struct Halo2TemporalTransactionResult
{
    uint32_t originalCalls = 0;
    bool originalReturned = false;
    bool renderPositionWritten = false;
    bool rasterPositionWritten = false;
    bool renderPositionRestored = false;
    bool rasterPositionRestored = false;
    bool otherCameraBytesWritten = false;
};

constexpr bool Halo2TemporalTransactionResultMatches(
    Halo2TemporalTransactionAction action,
    const Halo2TemporalTransactionResult& result) noexcept
{
    if (result.originalCalls != 1 || !result.originalReturned ||
        result.otherCameraBytesWritten)
    {
        return false;
    }
    if (action == Halo2TemporalTransactionAction::ScopedPositionsAndCallOnce)
    {
        return result.renderPositionWritten && result.rasterPositionWritten &&
            result.renderPositionRestored && result.rasterPositionRestored;
    }
    if (action == Halo2TemporalTransactionAction::CallStockOnce ||
        action == Halo2TemporalTransactionAction::
            RejectTemporalFrameAndCallStockOnce)
    {
        return !result.renderPositionWritten &&
            !result.rasterPositionWritten &&
            !result.renderPositionRestored &&
            !result.rasterPositionRestored;
    }
    return false;
}

// C-H2-3 same-frame stereo + 6DOF. These are title-local camera operations:
// H2EK proves position/forward/up and retail preserves those exact offsets.
// OpenXR quaternions are expressed in +X right, +Y up, -Z forward axes.
inline bool Halo2NormalizeQuaternion(
    const float input[4], float output[4]) noexcept
{
    if (!input || !output)
        return false;
    float lengthSquared = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (!std::isfinite(input[i]))
            return false;
        lengthSquared += input[i] * input[i];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.5f ||
        lengthSquared > 1.5f)
    {
        return false;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    for (int i = 0; i < 4; ++i)
        output[i] = input[i] * inverseLength;
    return true;
}

inline void Halo2MultiplyQuaternion(
    const float left[4], const float right[4], float output[4]) noexcept
{
    output[0] = left[3] * right[0] + left[0] * right[3] +
        left[1] * right[2] - left[2] * right[1];
    output[1] = left[3] * right[1] - left[0] * right[2] +
        left[1] * right[3] + left[2] * right[0];
    output[2] = left[3] * right[2] + left[0] * right[1] -
        left[1] * right[0] + left[2] * right[3];
    output[3] = left[3] * right[3] - left[0] * right[0] -
        left[1] * right[1] - left[2] * right[2];
}

inline void Halo2RotateVectorByQuaternion(
    const float quaternion[4], const float input[3], float output[3]) noexcept
{
    const float qx = quaternion[0], qy = quaternion[1];
    const float qz = quaternion[2], qw = quaternion[3];
    const float tx = 2.0f * (qy * input[2] - qz * input[1]);
    const float ty = 2.0f * (qz * input[0] - qx * input[2]);
    const float tz = 2.0f * (qx * input[1] - qy * input[0]);
    output[0] = input[0] + qw * tx + (qy * tz - qz * ty);
    output[1] = input[1] + qw * ty + (qz * tx - qx * tz);
    output[2] = input[2] + qw * tz + (qx * ty - qy * tx);
}

inline void Halo2RotateAboutAxis(
    float vector[3], const float axis[3], float cosine, float sine) noexcept
{
    const float dot = vector[0] * axis[0] + vector[1] * axis[1] +
        vector[2] * axis[2];
    const float cross[3] = {
        axis[1] * vector[2] - axis[2] * vector[1],
        axis[2] * vector[0] - axis[0] * vector[2],
        axis[0] * vector[1] - axis[1] * vector[0]};
    for (int i = 0; i < 3; ++i)
        vector[i] = vector[i] * cosine + cross[i] * sine +
            axis[i] * dot * (1.0f - cosine);
}

inline bool Halo2ApplyLocalQuaternion(
    Halo2CameraBasis& camera, const float localQuaternion[4]) noexcept
{
    if (!Halo2ValidateCameraBasis(camera))
        return false;
    float quaternion[4]{};
    if (!Halo2NormalizeQuaternion(localQuaternion, quaternion))
        return false;
    const float sineHalf = std::sqrt(
        quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] +
        quaternion[2] * quaternion[2]);
    if (sineHalf < 1.0e-6f)
        return true;
    float angle = 2.0f * std::atan2(sineHalf, quaternion[3]);
    constexpr float kPi = 3.14159265358979323846f;
    if (angle > kPi)
        angle -= 2.0f * kPi;
    const float localAxis[3] = {
        quaternion[0] / sineHalf,
        quaternion[1] / sineHalf,
        quaternion[2] / sineHalf};
    const float right[3] = {
        camera.forward[1] * camera.up[2] -
            camera.forward[2] * camera.up[1],
        camera.forward[2] * camera.up[0] -
            camera.forward[0] * camera.up[2],
        camera.forward[0] * camera.up[1] -
            camera.forward[1] * camera.up[0]};
    const float worldAxis[3] = {
        right[0] * localAxis[0] + camera.up[0] * localAxis[1] -
            camera.forward[0] * localAxis[2],
        right[1] * localAxis[0] + camera.up[1] * localAxis[1] -
            camera.forward[1] * localAxis[2],
        right[2] * localAxis[0] + camera.up[2] * localAxis[1] -
            camera.forward[2] * localAxis[2]};
    const float cosine = std::cos(angle), sine = std::sin(angle);
    Halo2RotateAboutAxis(camera.forward, worldAxis, cosine, sine);
    Halo2RotateAboutAxis(camera.up, worldAxis, cosine, sine);
    return Halo2ValidateCameraBasis(camera);
}

struct Halo2TrackedHeadInput
{
    float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float position[3]{};
    float referenceOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float referencePosition[3]{};
    // The universal halomccvr.cfg / F-key knobs every accepted title reads:
    // the F6 positional (leaning) toggle and world_scale, which IS the
    // metres-to-world-units factor (Halo 3 multiplies lean and eye offset
    // by it directly; its default 0.33 is 1/3.048 to two places).
    bool positional = true;
    float worldScale = kHalo2WorldUnitsPerMeter;
    // C-H2-23: the headset owns pitch and roll. The engine's own look pitch
    // (right stick / aim) is flattened out of the stock camera before the
    // head orientation is composed, so looking up or down is the head's
    // alone and the stick's pitch never adds on top of it. Yaw still adds,
    // so the stick turns the body and the head turns relative to it.
    bool headOwnsPitch = true;
};

// Engine look pitch in radians from a Halo-world (+Z up) camera forward:
// positive looks up. Used by the pitch loop that keeps the engine's aim on
// the headset's view (the shot line follows the view).
inline bool Halo2EnginePitchRadians(
    const Halo2CameraBasis& camera, float& pitch) noexcept
{
    const float horizontal = std::sqrt(
        camera.forward[0] * camera.forward[0] +
        camera.forward[1] * camera.forward[1]);
    if (!std::isfinite(horizontal) || !std::isfinite(camera.forward[2]))
        return false;
    pitch = std::atan2(camera.forward[2], horizontal);
    return std::isfinite(pitch);
}

// Headset pitch in radians from an OpenXR orientation (+Y up, -Z forward):
// positive looks up, the same convention as Halo2EnginePitchRadians.
inline bool Halo2HeadPitchRadians(const float q[4], float& pitch) noexcept
{
    float n[4]{};
    if (!q || !Halo2NormalizeQuaternion(q, n))
        return false;
    const float x = n[0], y = n[1], z = n[2], w = n[3];
    float forwardY = 2.0f * (w * x - y * z);
    if (!std::isfinite(forwardY))
        return false;
    forwardY = forwardY < -1.0f ? -1.0f : (forwardY > 1.0f ? 1.0f : forwardY);
    pitch = std::asin(forwardY);
    return std::isfinite(pitch);
}

// Yaw of the tracked camera relative to the stock camera, in the game's
// horizontal plane: how far the head has turned away from the body's facing.
// Walking is rotated by this so forward goes where the player looks.
inline bool Halo2HeadYawDeltaRadians(
    const Halo2CameraBasis& stock, const Halo2CameraBasis& tracked,
    float& delta) noexcept
{
    constexpr float kPi = 3.14159265f;
    const float stockYaw = std::atan2(stock.forward[1], stock.forward[0]);
    const float trackedYaw = std::atan2(tracked.forward[1], tracked.forward[0]);
    if (!std::isfinite(stockYaw) || !std::isfinite(trackedYaw))
        return false;
    float d = trackedYaw - stockYaw;
    while (d > kPi) d -= 2.0f * kPi;
    while (d < -kPi) d += 2.0f * kPi;
    delta = d;
    return std::isfinite(delta);
}

// The yaw-only part of an OpenXR head orientation: the rotation about room
// +Y that carries (0,0,-1) onto the head's horizontal forward. Halo 3, Reach
// and Halo 4 all recenter against yaw only, so a recenter taken with the
// head pitched or rolled does not tilt the world. Looking straight up or
// down has no horizontal forward; identity yaw is returned for that pole.
inline void Halo2YawOnlyQuaternion(
    const float orientation[4], float output[4]) noexcept
{
    const float forwardAxis[3] = {0.0f, 0.0f, -1.0f};
    float forward[3]{};
    Halo2RotateVectorByQuaternion(orientation, forwardAxis, forward);
    const float horizontal =
        std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (!std::isfinite(horizontal) || horizontal < 1.0e-4f)
    {
        output[0] = output[1] = output[2] = 0.0f;
        output[3] = 1.0f;
        return;
    }
    // Rotation about +Y by theta maps (0,0,-1) to (-sin theta, 0, -cos theta).
    const float theta = std::atan2(-forward[0], -forward[2]);
    output[0] = 0.0f;
    output[1] = std::sin(theta * 0.5f);
    output[2] = 0.0f;
    output[3] = std::cos(theta * 0.5f);
}

// Builds a tracked center camera from the stock H2 camera, the way the
// three accepted titles do it:
//  - orientation is the head's orientation relative to a YAW-ONLY recenter
//    reference, composed onto the game camera in its local axes, so pitch
//    and roll are the headset's own and yaw adds to the game's;
//  - lean is the room-space displacement from the recenter point, taken in
//    the recentered horizontal frame and re-applied in the GAME's horizontal
//    frame (room up -> world +Z) at world_scale world units per metre
//    (0.33 = 1/3.048 by default), clamped per axis and never rejected.
// A camera looking straight along world Z has no horizontal frame; the
// displacement then follows the camera basis itself.
inline bool Halo2BuildTrackedCenterCamera(
    const Halo2CameraBasis& stock, const Halo2TrackedHeadInput& input,
    Halo2CameraBasis& output) noexcept
{
    if (!Halo2ValidateCameraBasis(stock))
        return false;
    float current[4]{}, reference[4]{};
    if (!Halo2NormalizeQuaternion(input.orientation, current) ||
        !Halo2NormalizeQuaternion(input.referenceOrientation, reference) ||
        !std::isfinite(input.worldScale) || input.worldScale <= 0.0f)
    {
        return false;
    }
    float yawReference[4]{};
    Halo2YawOnlyQuaternion(reference, yawReference);
    const float inverseYawReference[4] = {
        -yawReference[0], -yawReference[1], -yawReference[2],
        yawReference[3]};

    Halo2CameraBasis candidate = stock;
    if (input.positional)
    {
        float rawDelta[3]{};
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(input.position[axis]) ||
                !std::isfinite(input.referencePosition[axis]))
            {
                return false;
            }
            rawDelta[axis] =
                input.position[axis] - input.referencePosition[axis];
            if (!std::isfinite(rawDelta[axis]))
                return false;
            if (rawDelta[axis] > kHalo2MaxHeadTranslationMeters)
                rawDelta[axis] = kHalo2MaxHeadTranslationMeters;
            if (rawDelta[axis] < -kHalo2MaxHeadTranslationMeters)
                rawDelta[axis] = -kHalo2MaxHeadTranslationMeters;
        }
        // Room displacement in the recentered frame: +x right, +y up,
        // -z forward.
        float local[3]{};
        Halo2RotateVectorByQuaternion(inverseYawReference, rawDelta, local);
        const float rightMeters = local[0];
        const float upMeters = local[1];
        const float forwardMeters = -local[2];

        float forwardAxis[3]{};
        float rightAxis[3]{};
        float upAxis[3]{};
        const float horizontal = std::sqrt(
            stock.forward[0] * stock.forward[0] +
            stock.forward[1] * stock.forward[1]);
        if (std::isfinite(horizontal) && horizontal >= 1.0e-4f)
        {
            // Halo world: +Z up. Horizontal forward (cos yaw, sin yaw, 0),
            // right = forward x up = (sin yaw, -cos yaw, 0).
            forwardAxis[0] = stock.forward[0] / horizontal;
            forwardAxis[1] = stock.forward[1] / horizontal;
            forwardAxis[2] = 0.0f;
            rightAxis[0] = forwardAxis[1];
            rightAxis[1] = -forwardAxis[0];
            rightAxis[2] = 0.0f;
            upAxis[0] = 0.0f;
            upAxis[1] = 0.0f;
            upAxis[2] = 1.0f;
        }
        else
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                forwardAxis[axis] = stock.forward[axis];
                upAxis[axis] = stock.up[axis];
            }
            rightAxis[0] = stock.forward[1] * stock.up[2] -
                stock.forward[2] * stock.up[1];
            rightAxis[1] = stock.forward[2] * stock.up[0] -
                stock.forward[0] * stock.up[2];
            rightAxis[2] = stock.forward[0] * stock.up[1] -
                stock.forward[1] * stock.up[0];
        }
        const float scale = input.worldScale;
        for (int axis = 0; axis < 3; ++axis)
        {
            float offset =
                (forwardAxis[axis] * forwardMeters +
                 rightAxis[axis] * rightMeters +
                 upAxis[axis] * upMeters) * scale;
            if (!std::isfinite(offset))
                return false;
            if (offset > kHalo2MaxHeadTranslationWorldUnits)
                offset = kHalo2MaxHeadTranslationWorldUnits;
            if (offset < -kHalo2MaxHeadTranslationWorldUnits)
                offset = -kHalo2MaxHeadTranslationWorldUnits;
            candidate.position[axis] += offset;
        }
    }

    if (input.headOwnsPitch)
    {
        // Keep only the engine's yaw: horizontal forward, world +Z up. A
        // camera looking straight along world Z has no yaw; keep it whole.
        const float horizontal = std::sqrt(
            stock.forward[0] * stock.forward[0] +
            stock.forward[1] * stock.forward[1]);
        if (std::isfinite(horizontal) && horizontal >= 1.0e-4f)
        {
            candidate.forward[0] = stock.forward[0] / horizontal;
            candidate.forward[1] = stock.forward[1] / horizontal;
            candidate.forward[2] = 0.0f;
            candidate.up[0] = 0.0f;
            candidate.up[1] = 0.0f;
            candidate.up[2] = 1.0f;
        }
    }

    float relativeOrientation[4]{};
    Halo2MultiplyQuaternion(
        inverseYawReference, current, relativeOrientation);
    if (!Halo2ApplyLocalQuaternion(candidate, relativeOrientation))
        return false;
    output = candidate;
    return true;
}

// The master switch for Halo 2's controller-owned first-person feature: the
// floating hands + gun mesh on the right controller, and the bullet direction
// that follows them.
//
// History. C-H2-41 also synthesized the right stick from the hand ray, which
// took ordinary character/camera turning away from the player and was rejected.
// C-H2-43/C-H2-44 stopped doing that (`Game_ComputeAimStick` refuses Halo 2
// unconditionally, and that refusal is permanent) but were rejected too: the
// C-H2-44 frame dump shows its `relativeOrientation[0] = -x` mirror had the
// gun tilted far off the hand, and the visible mesh and the bullet were built
// from two DIFFERENT poses, so shots did not follow the gun.
//
// C-H2-49 reverts the complete C-H2-46/47/48 transaction after the headset
// proved all three generations wrong. The +0x722850 buffer accepted thousands
// of writes but did not provide the assumed visual/coordinate ownership, and
// the firing substitution likewise did not make shots follow the visible
// crosshair. Keep the implementation dormant for evidence work; do not build a
// replacement on this boundary.
inline constexpr bool kHalo2RejectedInterpolatorControllerOwnershipEnabled = false;

// C-H2-52 replaces C-H2-50's witness-dependent generic-composer experiment
// with the complete H2EK first-person packet boundary used by both renderers.
// This switch arms the replacement transaction; neither rejected upstream
// implementation is re-enabled.
// C-H2-57 safety revert: C-H2-56 executed continuously but placed the gun on
// the player's face, while 69/69 admitted direction writes still did not move
// the visible shot. Keep both disproven implementations available as dormant
// evidence, but arm neither one while the replacement boundary is developed.
inline constexpr bool kHalo2FinalPaletteControllerOwnershipEnabled = false;

// C-H2-60 is a replacement transaction, not a re-arm of either rejected
// controller path above. It consumes one exact prepared-frame controller
// snapshot, builds physical targets from the observer's pre-HMD stock origin
// and recenter reference, and applies ownership only to the final packets.
inline constexpr bool kHalo2StableFinalPacketControllerOwnershipEnabled = false;

// C-H2-62/C-H2-63: this is the sole live controller-ownership transaction.
// Anniversary publishes through the builder's registered render-model callback;
// Classic retains the builder's persistent packets and consumes them directly.
// Both use the same exact object/binding context and split hands/gun ownership;
// the native unit aiming-vector updater owns the corresponding engine aim.
inline constexpr bool kHalo2VisibleConsumerControllerOwnershipEnabled = true;

// C-H2-65 safety revert. The C-H2-64 generic palm turnover/live-wrist snap was
// visibly different in both renderers but did not align the left hand, seat the
// double-barrel support hand, or put the rendered barrel on the crosshair. Keep
// the bounded helper as dormant evidence; the accepted C-H2-63 presentation is
// selected until weapon-authored geometry supplies all three targets.
inline constexpr bool kHalo2C64GenericLeftPresentationEnabled = false;

// C-H2-67 safety revert. C-H2-66's live thumb-ray pi rotation was visibly
// wrong for both Classic and Anniversary, and the same combined presentation
// did not attach the Arbiter arms like the Chief path. Retain the bounded
// authored-geometry helpers as evidence, but select C-H2-65/C-H2-63 placement
// until the rig-specific marker solve below is enabled by a later candidate.
inline constexpr bool kHalo2C66AuthoredAlignmentEnabled = false;

// C-H2-68 replaces the rejected generic/thumb-axis turnover with Halo 2's own
// semantic hand attachment frames. Chief and Elite author different marker
// transforms, so the final wrist solve must select the matching rig profile.
inline constexpr bool kHalo2C68RigMarkerAlignmentEnabled = false;

// C-H2-70 corrects C-H2-68's camera/hand axis permutation and prevents the
// Elite's continuously skinned wrist from spanning separately collapsed arm
// pivots. Arbiter packet admission remains the independently proven C-H2-68
// transaction in the DLL boundary.
inline constexpr bool kHalo2C70CorrectedHandFrameEnabled = true;
// C-H2-71: the eye raster already covers the headset, but both renderers build
// world visibility earlier from observer_result.  Lift only that upstream
// vertical FOV to the same proven headset cover; all non-Halo-2 paths are
// structurally unable to call this helper.
inline constexpr bool kHalo2C71UpstreamVisibilityCoverEnabled = true;

// C-H2-41: the controller carrier in Halo 2's own camera frame. H2EK's
// first_person_weapons.cpp builds absolute first-person node matrices in
// camera-relative space; its render path supplies the camera as the assembly
// root. Therefore the native equivalent of the other titles' floating-hand
// carrier is the controller pose relative to the tracked centre camera.
//
// Both OpenXR poses are sampled together. Their relative orientation and
// translation are mapped through the exact Halo 2 camera basis already used by
// the observer, so no retail object/bone layout is inherited from another game.
inline bool Halo2BuildControllerCarrier(
    const Halo2CameraBasis& trackedCamera, const float headOrientation[4],
    const float headPosition[3], const float controllerOrientation[4],
    const float controllerPosition[3], float worldScale, float forwardTrimMeters,
    float rightTrimMeters, float upTrimMeters,
    Halo2CameraBasis& output) noexcept
{
    if (!Halo2ValidateCameraBasis(trackedCamera) ||
        !std::isfinite(worldScale) || worldScale <= 0.0f ||
        !std::isfinite(forwardTrimMeters) ||
        !std::isfinite(rightTrimMeters) || !std::isfinite(upTrimMeters))
    {
        return false;
    }
    float head[4]{}, controller[4]{};
    if (!Halo2NormalizeQuaternion(headOrientation, head) ||
        !Halo2NormalizeQuaternion(controllerOrientation, controller))
    {
        return false;
    }
    const float inverseHead[4] = {-head[0], -head[1], -head[2], head[3]};
    float relativeOrientation[4]{};
    Halo2MultiplyQuaternion(inverseHead, controller, relativeOrientation);

    // C-H2-45 removes C-H2-44's `relativeOrientation[0] = -relativeOrientation[0]`.
    // Negating only a quaternion's x term is not "invert pitch": it produces
    // F * R^-1 * F (a mirrored INVERSE rotation), so it also reversed how the
    // hand's yaw and roll composed onto the camera. It was written from a
    // reasoned sign argument, not from a measurement, and the headset reported
    // no improvement at all from it. The carrier is left as the plain
    // head-relative controller rotation again.

    Halo2CameraBasis candidate = trackedCamera;
    if (!Halo2ApplyLocalQuaternion(candidate, relativeOrientation))
        return false;

    float roomDelta[3]{};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(headPosition[axis]) ||
            !std::isfinite(controllerPosition[axis]))
        {
            return false;
        }
        roomDelta[axis] = controllerPosition[axis] - headPosition[axis];
        if (!std::isfinite(roomDelta[axis]) ||
            std::fabs(roomDelta[axis]) > kHalo2MaxHeadTranslationMeters)
        {
            return false;
        }
    }
    float headLocal[3]{};
    Halo2RotateVectorByQuaternion(inverseHead, roomDelta, headLocal);
    const float right[3] = {
        trackedCamera.forward[1] * trackedCamera.up[2] -
            trackedCamera.forward[2] * trackedCamera.up[1],
        trackedCamera.forward[2] * trackedCamera.up[0] -
            trackedCamera.forward[0] * trackedCamera.up[2],
        trackedCamera.forward[0] * trackedCamera.up[1] -
            trackedCamera.forward[1] * trackedCamera.up[0]};
    const float candidateRight[3] = {
        candidate.forward[1] * candidate.up[2] -
            candidate.forward[2] * candidate.up[1],
        candidate.forward[2] * candidate.up[0] -
            candidate.forward[0] * candidate.up[2],
        candidate.forward[0] * candidate.up[1] -
            candidate.forward[1] * candidate.up[0]};
    for (int axis = 0; axis < 3; ++axis)
    {
        candidate.position[axis] = trackedCamera.position[axis] +
            (right[axis] * headLocal[0] +
             trackedCamera.up[axis] * headLocal[1] -
             trackedCamera.forward[axis] * headLocal[2] +
             candidate.forward[axis] * forwardTrimMeters +
             candidateRight[axis] * rightTrimMeters +
             candidate.up[axis] * upTrimMeters) * worldScale;
    }
    if (!Halo2ValidateCameraBasis(candidate))
        return false;
    output = candidate;
    return true;
}

inline bool Halo2BuildControllerCarrier(
    const Halo2CameraBasis& trackedCamera, const float headOrientation[4],
    const float headPosition[3], const float controllerOrientation[4],
    const float controllerPosition[3], float worldScale, float forwardTrimMeters,
    Halo2CameraBasis& output) noexcept
{
    return Halo2BuildControllerCarrier(
        trackedCamera, headOrientation, headPosition, controllerOrientation,
        controllerPosition, worldScale, forwardTrimMeters, 0.0f, 0.0f, output);
}

// Apply the accepted mirrored presentation trim to the independent left
// controller in OpenXR local axes. The right aim snapshot already contains the
// ordinary gun yaw/pitch/roll exactly once; applying it again would rotate only
// the mesh. This is the same sign contract used by Halo 3, Reach and Halo 4:
// mirrored yaw and roll, unchanged pitch.
inline bool Halo2BuildMirroredLeftAimOrientation(
    const float controllerOrientation[4], float gunYawDeg,
    float gunPitchDeg, float gunRollDeg, float output[4]) noexcept
{
    float controller[4]{};
    if (!controllerOrientation || !output ||
        !Halo2NormalizeQuaternion(controllerOrientation, controller) ||
        !std::isfinite(gunYawDeg) || !std::isfinite(gunPitchDeg) ||
        !std::isfinite(gunRollDeg))
    {
        return false;
    }
    constexpr float kDegreesToRadians = 0.01745329251994329577f;
    const float yaw = -gunYawDeg * kDegreesToRadians;
    const float pitch = gunPitchDeg * kDegreesToRadians;
    const float roll = gunRollDeg * kDegreesToRadians;
    const float yawQ[4] = {
        0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    const float pitchQ[4] = {
        std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f)};
    // ComputeAimPose uses -roll for the right weapon. Mirroring the configured
    // roll therefore makes this local Z rotation positive.
    const float rollQ[4] = {
        0.0f, 0.0f, std::sin(roll * 0.5f), std::cos(roll * 0.5f)};
    float yawPitch[4]{}, trim[4]{}, candidate[4]{};
    Halo2MultiplyQuaternion(yawQ, pitchQ, yawPitch);
    Halo2MultiplyQuaternion(yawPitch, rollQ, trim);
    Halo2MultiplyQuaternion(controller, trim, candidate);
    return Halo2NormalizeQuaternion(candidate, output);
}

// Halo 2's stable controller target. Unlike the rejected carrier above, this
// never uses current tracked camera + (controller - current head). The observer
// publication already owns the exact pre-HMD gameplay camera and recenter pair;
// feed the controller through that title-native mapping, then apply only the
// configured controller-local forward/right/up visual standoff.
inline bool Halo2BuildStableControllerCarrier(
    const Halo2CameraBasis& stockCamera,
    const float referenceOrientation[4], const float referencePosition[3],
    const float controllerOrientation[4], const float controllerPosition[3],
    float worldScale, float forwardTrimMeters, float rightTrimMeters,
    float upTrimMeters, Halo2CameraBasis& output) noexcept
{
    if (!referenceOrientation || !referencePosition ||
        !controllerOrientation || !controllerPosition ||
        !std::isfinite(forwardTrimMeters) || !std::isfinite(rightTrimMeters) ||
        !std::isfinite(upTrimMeters))
    {
        return false;
    }
    Halo2TrackedHeadInput input{};
    std::memcpy(input.orientation, controllerOrientation,
                sizeof(input.orientation));
    std::memcpy(input.position, controllerPosition, sizeof(input.position));
    std::memcpy(input.referenceOrientation, referenceOrientation,
                sizeof(input.referenceOrientation));
    std::memcpy(input.referencePosition, referencePosition,
                sizeof(input.referencePosition));
    input.positional = true;
    input.worldScale = worldScale;
    input.headOwnsPitch = true;
    Halo2CameraBasis candidate{};
    if (!Halo2BuildTrackedCenterCamera(stockCamera, input, candidate))
        return false;
    const float trimWorld = forwardTrimMeters * worldScale;
    if (!std::isfinite(trimWorld))
        return false;
    const float rightWorld = rightTrimMeters * worldScale;
    const float upWorld = upTrimMeters * worldScale;
    const float candidateRight[3] = {
        candidate.forward[1] * candidate.up[2] - candidate.forward[2] * candidate.up[1],
        candidate.forward[2] * candidate.up[0] - candidate.forward[0] * candidate.up[2],
        candidate.forward[0] * candidate.up[1] - candidate.forward[1] * candidate.up[0]};
    if (!std::isfinite(rightWorld) || !std::isfinite(upWorld)) return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        candidate.position[axis] += candidate.forward[axis] * trimWorld +
            candidateRight[axis] * rightWorld + candidate.up[axis] * upWorld;
        if (!std::isfinite(candidate.position[axis]))
            return false;
    }
    if (!Halo2ValidateCameraBasis(candidate))
        return false;
    output = candidate;
    return true;
}

inline bool Halo2BuildStableControllerCarrier(
    const Halo2CameraBasis& stockCamera,
    const float referenceOrientation[4], const float referencePosition[3],
    const float controllerOrientation[4], const float controllerPosition[3],
    float worldScale, float forwardTrimMeters,
    Halo2CameraBasis& output) noexcept
{
    return Halo2BuildStableControllerCarrier(
        stockCamera, referenceOrientation, referencePosition,
        controllerOrientation, controllerPosition, worldScale,
        forwardTrimMeters, 0.0f, 0.0f, output);
}

// C-H2-43: preserve the game's authored projectile origin and converge it on
// the controller ray at the configured reticle distance. This changes only a
// firing helper's direction result; it has no camera or input side effect.
inline bool Halo2BuildControllerShotDirection(
    const float origin[3], const Halo2CameraBasis& carrier,
    float rangeWorld, float output[3]) noexcept
{
    if (!origin || !output || !Halo2ValidateCameraBasis(carrier) ||
        !std::isfinite(rangeWorld) || rangeWorld <= 0.0f)
    {
        return false;
    }
    float candidate[3]{};
    float lengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(origin[axis]))
            return false;
        const float target = carrier.position[axis] +
            carrier.forward[axis] * rangeWorld;
        candidate[axis] = target - origin[axis];
        lengthSquared += candidate[axis] * candidate[axis];
    }
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length) || length <= 1.0e-4f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        candidate[axis] /= length;
        if (!std::isfinite(candidate[axis]))
            return false;
    }
    std::memcpy(output, candidate, sizeof(candidate));
    return true;
}

// E-H2-49: `useUnitAim` selects how Halo 2 authors the stock direction before
// the firing helper returns; it does not select whether the guarded local
// player's completed direction may follow the presented VR crosshair. Keeping
// the flag in this pure policy makes that deliberate independence testable.
inline bool Halo2ShouldAttemptDirectShotOwnership(
    bool originalCompleted, bool outputValid, bool featureActive,
    bool directAimArmed, uint8_t useUnitAim) noexcept
{
    (void)useUnitAim;
    return originalCompleted && outputValid && featureActive && directAimArmed;
}

// E-H2-6 pose ownership. The observer core publishes, once per game frame,
// the engine's camera as it found it (`stock`) and the camera it wrote
// (`tracked`), with the recenter reference it used. A per-eye core then
// decides, from the camera it actually holds, whether that camera already
// carries the head pose. This is what stops a frame the observer skipped
// (snapshot not ready yet - 576 of ~5300 frames on 2026-08-21) from being
// rendered as if it were tracked, and what keeps a director/playback camera
// (0x960780's override branch, not observer-derived) from being mistaken
// for one.
// E-H2-18: the exact headset sample the observer composed its tracked camera
// from, so a renderer on another thread (the Saber scene) can draw and
// submit from the SAME head pose the engine placed the first-person weapon
// against, instead of rebuilding the centre from a newer sample.
struct Halo2ObserverPoseSnapshot
{
    bool valid = false;
    int64_t predictedDisplayTimeNs = 0;
    uint64_t trackingSpaceEpoch = 0;
    float headOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float headPosition[3]{};
    // Per eye: offset from the head (metres, head-local) and the absolute
    // OpenXR view pose of that sample, the pose the image is submitted with.
    float eyeOffsetPosition[2][3]{};
    float eyeOffsetOrientation[2][4]{{0.0f, 0.0f, 0.0f, 1.0f},
                                     {0.0f, 0.0f, 0.0f, 1.0f}};
    float eyePosition[2][3]{};
    float eyeOrientation[2][4]{{0.0f, 0.0f, 0.0f, 1.0f},
                               {0.0f, 0.0f, 0.0f, 1.0f}};
    // C-H2-60: controller data belongs to the observer publication that was
    // built from the same prepared sample. Anniversary's scene/packet thread
    // frequently consumes that publication after a newer VR serial exists, so
    // consulting only the latest VR snapshot creates an ordering-dependent
    // permanent stock fallback after transitions.
    bool rightAimValid = false;
    float rightAimOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float rightAimPosition[3]{};
    bool twoHandAimActive = false;
    bool handAlignment = false;
    ControllerFingerInput controllerFingers[2]{};
    bool leftControllerValid = false;
    float leftControllerOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float leftControllerPosition[3]{};
    bool independentRightAimValid = false;
    float independentRightAimOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
};

struct Halo2ObserverPosePublication
{
    bool snapTurnPending = false;
    float snapTurnTargetYaw = 0.0f;
    uint32_t generation = 0;
    uint64_t serial = 0;
    // E-H2-23 (C-H2-32): one number per ring WRITE. Several ring entries can
    // share a VR serial (the observer runs ~200/s against 90-120 VR frames),
    // so a witness keyed on the serial could name the wrong entry; this key
    // names exactly one.
    uint64_t index = 0;
    Halo2CameraBasis stock{};
    Halo2CameraBasis tracked{};
    // Native stock remains exact aim feedback. A seated controller and the
    // view share this separate reference when the vehicle adapter owns it.
    bool vehicleReferenceValid=false;
    uint32_t vehicleUnit=UINT32_MAX,vehicleParent=UINT32_MAX;
    int16_t vehicleSeat=-1;
    Halo2CameraBasis vehicleReference{};
    float referenceOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float referencePosition[3]{};
    Halo2ObserverPoseSnapshot snapshot{};
};

inline const Halo2CameraBasis& Halo2ControllerReference(
    const Halo2ObserverPosePublication& publication) noexcept
{
    return publication.vehicleReferenceValid ? publication.vehicleReference : publication.stock;
}

inline bool Halo2ObserverControllerSnapshotUsable(
    const Halo2ObserverPosePublication& publication,
    uint32_t generation) noexcept
{
    // Deliberately no comparison with a separately sampled/latest VR serial.
    // The controller and stock body camera are already one immutable observer
    // publication; that is the coherence boundary Halo 2 actually consumes.
    return generation != 0 && publication.generation == generation &&
        publication.serial != 0 && publication.snapshot.valid &&
        publication.snapshot.rightAimValid &&
        publication.snapshot.leftControllerValid;
}

// A firing callback may consume the preceding observer sample. Use that
// immutable camera/controller pair; the latest tracking sample only checks
// its age and tracking-space identity, never supplies replacement pose data.
inline bool Halo2DualAimPublicationFresh(
    const Halo2ObserverPosePublication& publication, uint32_t generation,
    uint64_t currentEpoch, int64_t currentTimeNs) noexcept
{
    const auto& snapshot = publication.snapshot;
    if (!Halo2ObserverControllerSnapshotUsable(publication, generation) ||
        !currentEpoch || snapshot.trackingSpaceEpoch != currentEpoch ||
        snapshot.predictedDisplayTimeNs <= 0 || currentTimeNs <= 0)
        return false;
    const uint64_t sample = static_cast<uint64_t>(snapshot.predictedDisplayTimeNs);
    const uint64_t current = static_cast<uint64_t>(currentTimeNs);
    const uint64_t age = current >= sample ? current - sample : sample - current;
    return age <= 100000000; // 100 ms; also bounds reverse publication ordering.
}

inline bool Halo2CameraBasisMatches(
    const Halo2CameraBasis& left, const Halo2CameraBasis& right) noexcept
{
    constexpr float kPositionEpsilonWorldUnits = 1.0e-3f;
    constexpr float kMinimumAxisDot = 0.9999f;
    float forwardDot = 0.0f;
    float upDot = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(left.position[axis]) ||
            !std::isfinite(right.position[axis]) ||
            std::fabs(left.position[axis] - right.position[axis]) >
                kPositionEpsilonWorldUnits)
        {
            return false;
        }
        forwardDot += left.forward[axis] * right.forward[axis];
        upDot += left.up[axis] * right.up[axis];
    }
    return std::isfinite(forwardDot) && std::isfinite(upDot) &&
        forwardDot > kMinimumAxisDot && upDot > kMinimumAxisDot;
}

enum class Halo2PoseOwnerDecision : uint8_t
{
    // No publication, or the camera in hand is not the observer's tracked
    // camera (observer skipped this frame, or a non-observer camera): this
    // core tracks the centre itself from its own snapshot.
    SelfTrack = 0,
    // The camera in hand is the observer's tracked camera for this exact
    // prepared serial: use it as the centre and add only the eye offset.
    UsePublishedTracked,
    // The camera in hand is the observer's tracked camera, but for a
    // different prepared serial: rebuild the centre from the published stock
    // with this core's own (current) snapshot and the observer's reference.
    RederiveFromPublishedStock,
    // The publication claims to describe this camera but belongs to another
    // module generation: nothing can be trusted, render stock this frame.
    NoPose,
};

inline Halo2PoseOwnerDecision Halo2SelectPoseOwner(
    bool published, uint32_t publishedGeneration, uint64_t publishedSerial,
    const Halo2CameraBasis& publishedTracked,
    const Halo2CameraBasis& engineCamera,
    uint32_t generation, uint64_t serial) noexcept
{
    if (!published || !publishedSerial)
        return Halo2PoseOwnerDecision::SelfTrack;
    if (!Halo2CameraBasisMatches(engineCamera, publishedTracked))
        return Halo2PoseOwnerDecision::SelfTrack;
    if (publishedGeneration != generation || !generation)
        return Halo2PoseOwnerDecision::NoPose;
    return publishedSerial == serial
        ? Halo2PoseOwnerDecision::UsePublishedTracked
        : Halo2PoseOwnerDecision::RederiveFromPublishedStock;
}

// `worldUnitsPerMeter` is the universal world_scale, exactly as Halo 3 scales
// its eye offset (game.cpp eyeScale = g_worldScale).
inline bool Halo2BuildSynchronousEyeCamera(
    const Halo2CameraBasis& trackedCenter, const float eyePositionMeters[3],
    const float eyeOrientation[4], Halo2CameraBasis& output,
    float worldUnitsPerMeter = kHalo2WorldUnitsPerMeter) noexcept
{
    if (!eyePositionMeters || !eyeOrientation ||
        !Halo2ValidateCameraBasis(trackedCenter) ||
        !std::isfinite(worldUnitsPerMeter) || worldUnitsPerMeter <= 0.0f)
    {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(eyePositionMeters[axis]) ||
            std::fabs(eyePositionMeters[axis]) > kHalo2MaxEyeOffsetMeters)
            return false;
    Halo2CameraBasis candidate = trackedCenter;
    const float right[3] = {
        candidate.forward[1] * candidate.up[2] -
            candidate.forward[2] * candidate.up[1],
        candidate.forward[2] * candidate.up[0] -
            candidate.forward[0] * candidate.up[2],
        candidate.forward[0] * candidate.up[1] -
            candidate.forward[1] * candidate.up[0]};
    for (int axis = 0; axis < 3; ++axis)
    {
        candidate.position[axis] +=
            (right[axis] * eyePositionMeters[0] +
             candidate.up[axis] * eyePositionMeters[1] -
             candidate.forward[axis] * eyePositionMeters[2]) *
            worldUnitsPerMeter;
    }
    if (!Halo2ApplyLocalQuaternion(candidate, eyeOrientation))
        return false;
    output = candidate;
    return true;
}

enum class Halo2CameraPoseWrite : uint8_t
{
    Reject = 0,
    RenderPosition,
    RenderForward,
    RenderUp,
    RasterPosition,
    RasterForward,
    RasterUp,
    RenderVerticalFov,
    RasterVerticalFov,
};

constexpr Halo2CameraPoseWrite SelectHalo2CameraPoseWrite(
    uint32_t windowRelativeOffset, size_t bytes) noexcept
{
    if (bytes == kHalo2CameraVectorBytes)
    {
        if (windowRelativeOffset == kHalo2WindowRenderPositionOffset)
            return Halo2CameraPoseWrite::RenderPosition;
        if (windowRelativeOffset == kHalo2WindowRenderForwardOffset)
            return Halo2CameraPoseWrite::RenderForward;
        if (windowRelativeOffset == kHalo2WindowRenderUpOffset)
            return Halo2CameraPoseWrite::RenderUp;
        if (windowRelativeOffset == kHalo2WindowRasterPositionOffset)
            return Halo2CameraPoseWrite::RasterPosition;
        if (windowRelativeOffset == kHalo2WindowRasterForwardOffset)
            return Halo2CameraPoseWrite::RasterForward;
        if (windowRelativeOffset == kHalo2WindowRasterUpOffset)
            return Halo2CameraPoseWrite::RasterUp;
    }
    else if (bytes == sizeof(float))
    {
        if (windowRelativeOffset == kHalo2WindowRenderVerticalFovOffset)
            return Halo2CameraPoseWrite::RenderVerticalFov;
        if (windowRelativeOffset == kHalo2WindowRasterVerticalFovOffset)
            return Halo2CameraPoseWrite::RasterVerticalFov;
    }
    return Halo2CameraPoseWrite::Reject;
}

struct Halo2SameFramePairProof
{
    uint32_t generation = 0;
    uint64_t preparedSerial = 0;
    uint64_t attemptToken = 0;
    uint64_t eyeAttemptToken[2]{};
    uint64_t renderSerial[2]{};
    uint64_t captureSerial[2]{};
    uint8_t eyeMask = 0;
    uint8_t eyeRenderCount = 0;
    uint8_t freshEyeCount = 0;
    bool allPoseSpansRestored = false;
};

constexpr bool Halo2SameFramePairMatches(
    const Halo2SameFramePairProof& proof, uint32_t activeGeneration,
    uint64_t currentPreparedSerial) noexcept
{
    return activeGeneration != 0 && currentPreparedSerial != 0 &&
        proof.generation == activeGeneration &&
        proof.preparedSerial == currentPreparedSerial &&
        proof.attemptToken != 0 &&
        proof.eyeAttemptToken[0] == proof.attemptToken &&
        proof.eyeAttemptToken[1] == proof.attemptToken &&
        proof.renderSerial[0] == currentPreparedSerial &&
        proof.renderSerial[1] == currentPreparedSerial &&
        proof.captureSerial[0] == currentPreparedSerial &&
        proof.captureSerial[1] == currentPreparedSerial &&
        proof.eyeMask == 0x3 && proof.eyeRenderCount == 2 &&
        proof.freshEyeCount == 2 && proof.allPoseSpansRestored;
}

struct Halo2RetailAnchor
{
    const char* name;
    const char* pattern;
    uint32_t rva;
    uint8_t relativeDispOffset;
    uint32_t relativeTargetRva;
};

inline constexpr size_t kHalo2AnchorGameTimeIncrement = 0;
inline constexpr size_t kHalo2AnchorGameTimeInit = 1;
inline constexpr size_t kHalo2AnchorRenderFrame = 2;
inline constexpr size_t kHalo2AnchorPlayerWindow = 3;
inline constexpr size_t kHalo2AnchorRenderView = 4;
inline constexpr size_t kHalo2AnchorAsymmetricFrustum = 5;

// Every pattern matched exactly once over each complete mapped retail image.
// A nonzero relativeDispOffset names a disp32 inside the match; its instruction
// ends at offset+4, so the same decode covers RIP-relative data operands.
inline constexpr Halo2RetailAnchor kHalo2RetailAnchors[] = {
    { "game-time-increment",
      "48 8B 05 ?? ?? ?? ?? FF 40 08 C3",
      0x7067F0, 0x03, kHalo2GameTimeSlotRva },
    { "game-time-level-init",
      "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 33 C9 48 89 08 48 89 48 08 48 "
      "89 48 10 48 89 48 18 48 89 48 20 89 48 28 E8 ?? ?? ?? ?? 48 8B "
      "15 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 0F BF 48 08 66 89 4A 02 "
      "C7 42 0C 00 00 80 3F C6 02 01",
      0x706910, 0x07, kHalo2GameTimeSlotRva },
    { "render-frame",
      "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 "
      "41 56 48 83 EC 40 41 8B F9 48 63 EA 41 8B F0 8B D9",
      0x7E1600, 0, 0 },
    { "render-player-window",
      "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 00 "
      "02 00 00",
      0x7E2130, 0, 0 },
    { "render-view",
      "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 44 88 4C 24 20 "
      "55 41 54 41 55 41 56 41 57 48 8D AC 24 B0 FD FF FF 48 81 EC 50 "
      "03 00 00",
      0x7E30D0, 0, 0 },
    { "asymmetric-frustum-helper",
      "48 89 5C 24 08 57 48 83 EC 40 0F BF 41 3A 48 8B F9 44 0F BF 49 "
      "3C 48 8B DA 0F BF 51 32 44 0F BF 41 3E",
      0x7DFCD0, 0, 0 },
};

inline constexpr size_t kHalo2RetailAnchorCount =
    sizeof(kHalo2RetailAnchors) / sizeof(kHalo2RetailAnchors[0]);

static_assert(kHalo2RetailAnchors[kHalo2AnchorPlayerWindow].rva ==
    kHalo2RetailRenderPlayerWindowRva);
static_assert(kHalo2RetailAnchors[kHalo2AnchorRenderView].rva ==
    kHalo2RetailRenderViewRva);

constexpr uint32_t Halo2RetailAnchorRelativeTargetCount()
{
    uint32_t count = 0;
    for (const Halo2RetailAnchor& anchor : kHalo2RetailAnchors)
        if (anchor.relativeDispOffset != 0)
            ++count;
    return count;
}

inline constexpr uint32_t kHalo2RetailAnchorRelativeTargets =
    Halo2RetailAnchorRelativeTargetCount();

struct Halo2ColdObservationResult
{
    bool moduleRangeValid = false;
    bool peIdentity = false;
    uint32_t anchorsMatchedOnce = 0;
    uint32_t anchorsAtPinnedRva = 0;
    uint32_t relativeTargetsAtPinnedRva = 0;
    bool postInitializationTickObserved = false;
    bool mappingStable = false;
};

constexpr bool Halo2ColdObservationPass(
    const Halo2ColdObservationResult& result)
{
    return result.moduleRangeValid && result.peIdentity &&
        result.anchorsMatchedOnce == kHalo2RetailAnchorCount &&
        result.anchorsAtPinnedRva == kHalo2RetailAnchorCount &&
        result.relativeTargetsAtPinnedRva ==
            kHalo2RetailAnchorRelativeTargets &&
        result.postInitializationTickObserved && result.mappingStable;
}

constexpr bool Halo2ColdObservationNeedsImageScan(
    bool completedForModuleInstance)
{
    return !completedForModuleInstance;
}

// The expensive loaded-image proof is valid for a physical module instance,
// but level/title rearm deliberately clears level-derived live bindings. When
// MCC begins another level without unloading/rebasing halo2.dll, reconstruct
// those bindings once from the already-proven image instead of either repeating
// the complete proof or leaving every dependent hook permanently stock.
constexpr bool Halo2ColdObservationNeedsDerivedRebind(
    bool completedForModuleInstance, bool cachedProofPassed,
    bool rebindAttemptedForGate, bool derivedBindingsValid)
{
    return completedForModuleInstance && cachedProofPassed &&
        !rebindAttemptedForGate && !derivedBindingsValid;
}

// Pure decision core for the read-only game-time liveness gate. The false->true
// initialized transition is only a new baseline: it is never mistaken for a
// game tick. A later different tick proves game_update reached the official
// active-update tail. `!=` is deliberate: the official code permits save-state
// restoration and uint32 wrap, neither of which invalidates the clock. If
// observation starts mid-level, six seconds of uninterrupted tick changes is
// the conservative already-running path used by the established title gates.
class Halo2GameTimeGateLogic
{
public:
    enum class Decision : uint8_t
    {
        Hold = 0,
        OpenAfterBoundaryThenTick,
        OpenAlreadyRunning,
    };

    // Six seconds at the title worker's 50 ms cadence. Kept equal to the
    // established gate, but implemented independently because H2's fast path
    // accepts ONLY the engine's explicit uninitialized lifecycle state; an
    // unchanged initialized clock is not allowed to manufacture that boundary.
    static constexpr uint32_t kAlreadyRunningSamples = 120;

    Decision Observe(bool initialized, uint32_t tick)
    {
        if (!initialized)
        {
            // halo2.dll normally remains resident while MCC leaves a level.
            // Process the engine's explicit level-dispose state even after a
            // prior open so liveness cannot remain falsely latched in menus or
            // carry across a later load.
            m_sawUninitialized = true;
            m_haveInitializedSample = false;
            m_changeRun = 0;
            ++m_stillRun;
            m_open = false;
            m_lastDecision = Decision::Hold;
            return Decision::Hold;
        }
        if (m_open)
            return m_lastDecision;
        if (!m_haveInitializedSample)
        {
            m_haveInitializedSample = true;
            m_tick = tick;
            return Decision::Hold;
        }
        const bool changed = tick != m_tick;
        m_tick = tick;
        if (!changed)
        {
            m_changeRun = 0;
            ++m_stillRun;
            return Decision::Hold;
        }
        m_stillRun = 0;
        if (m_sawUninitialized)
        {
            m_open = true;
            m_lastDecision = Decision::OpenAfterBoundaryThenTick;
            return m_lastDecision;
        }
        if (++m_changeRun >= kAlreadyRunningSamples)
        {
            m_open = true;
            m_lastDecision = Decision::OpenAlreadyRunning;
            return m_lastDecision;
        }
        return Decision::Hold;
    }

    // A null/unreadable/racy engine sample must not manufacture a still frame.
    // Preserve prior genuine frozen evidence, but force the next readable
    // initialized value to become a new baseline.
    void InvalidateSample()
    {
        m_haveInitializedSample = false;
        // The already-running proof is explicitly consecutive. A missing,
        // unreadable, or incoherent sample breaks that run even when a prior
        // real uninitialized lifecycle boundary remains valid evidence.
        m_changeRun = 0;
        m_stillRun = 0;
        m_open = false;
        m_lastDecision = Decision::Hold;
    }

    void Reset()
    {
        m_tick = 0;
        m_changeRun = 0;
        m_stillRun = 0;
        m_haveInitializedSample = false;
        m_sawUninitialized = false;
        m_open = false;
        m_lastDecision = Decision::Hold;
    }

    bool IsOpen() const { return m_open; }
    bool SawUninitialized() const { return m_sawUninitialized; }
    uint32_t ChangeRun() const { return m_changeRun; }
    uint32_t StillRun() const { return m_stillRun; }

private:
    uint32_t m_tick = 0;
    uint32_t m_changeRun = 0;
    uint32_t m_stillRun = 0;
    bool m_haveInitializedSample = false;
    bool m_sawUninitialized = false;
    bool m_open = false;
    Decision m_lastDecision = Decision::Hold;
};

// ---------------------------------------------------------------------------
// E-H2-3: Halo 2 ships two renderers in one module, and the classic Blam tree
// is gated off whole while the remastered (Saber GroundHog) renderer owns the
// frame. Every constant below is byte-proven against the pinned retail module;
// see docs/HALO2-SIGNATURE-EVIDENCE.md. Nothing here reads memory - these are
// pure descriptions plus pure decision logic so the policy stays testable.
// ---------------------------------------------------------------------------

enum class Halo2GraphicsMode : uint8_t
{
    Unknown = 0,
    Classic,
    Remastered,
};

// The classic per-frame driver 0x95FEC0 bails on its second instruction when
// this byte is non-zero. The mode dword is written by the render-mode applier
// 0x511E0, which passes (appliedMode == 0) to the Saber SSL argument named
// "isLegacy" - that is what proves the polarity.
inline constexpr uint32_t kHalo2ClassicRenderDriverRva = 0x0095FEC0;
inline constexpr uint32_t kHalo2ClassicRenderDisabledByteRva = 0x00E70CF8;
inline constexpr uint32_t kHalo2AppliedRenderModeRva = 0x00E21280;
inline constexpr uint32_t kHalo2RequestedRenderModeRva = 0x00E21278;
inline constexpr uint32_t kHalo2RenderModeApplierRva = 0x000511E0;

// Unique in the complete mapped image of both pinned retail editions.
// Bytes: push rbx / sub rsp,0x20 / cmp byte [rip+disp32],0 / jne / call.
// The cmp disp32 sits at +8 and is based at +13.
inline constexpr char kHalo2ClassicRenderGatePattern[] =
    "40 53 48 83 EC 20 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? E8";
inline constexpr uint32_t kHalo2ClassicRenderGateDispOffset = 8;
inline constexpr uint32_t kHalo2ClassicRenderGateNextOffset = 13;

// E-H2-19: the applier's entry (push rbx/rsi/rdi; sub rsp,0x80; mov [rsp+0x30],-2;
// mov ebx,[request]) and the two "forced legacy fading" globals the frame
// driver consults: the flag 0x15A3D91 (0x6AFA00 returns it; 0x6B03B0(1)
// clears it every frame it is honoured) and the fade target byte at
// *(0x15A3D88)+0x12A (0x6AFA10), which becomes the request when the flag is
// set. Read for evidence only; never written.
inline constexpr uint8_t kHalo2RenderModeApplierEntryBytes[] = {
    0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0x44, 0x24, 0x30, 0xFE, 0xFF, 0xFF, 0xFF, 0x8B, 0x1D};
// E-H2-20: classic draw_first_person 0x7E0C60 (H2EK 0x29D8F0, profile
// string "draw_first_person") copies the render camera globals, overwrites
// the copy's vertical FOV (+0x28) with the .rdata float at 0xB3D99C =
// 0x3F5D9734 = 0.86557 rad = 49.594 degrees - the SAME constant the Saber
// renderer bakes into its first-person projection - rebuilds the projection
// with the world's own builders (0x7DFCD0 / 0x7DF7A0), sets it as the
// rasterizer camera (0x955590) and draws the first-person models, then puts
// the world camera back. The constant has exactly one reader, the MOVSS at
// 0x7E0F35 (F3 0F 10 05 disp32 -> 0xB3D99C). While a classic eye pair is
// being rendered the constant holds the eye's vertical cover, so the weapon
// is drawn through the world's frustum; it is restored with the cameras.
inline constexpr uint32_t kHalo2ClassicDrawFirstPersonRva = 0x007E0C60;
inline constexpr uint8_t kHalo2ClassicDrawFirstPersonEntryBytes[] = {
    0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x01, 0x00, 0x00, 0x8B, 0x0D};
inline constexpr uint32_t kHalo2ClassicFirstPersonFovConstantRva = 0x00B3D99C;
inline constexpr uint32_t kHalo2ClassicFirstPersonFovLoadRva = 0x007E0F35;
inline constexpr uint8_t kHalo2ClassicFirstPersonFovLoadBytes[] = {
    0xF3, 0x0F, 0x10, 0x05, 0x5F, 0xCA, 0x35, 0x00};
inline constexpr uint32_t kHalo2ClassicFirstPersonFovStockBits = 0x3F5D9734u;
static_assert(0x007E0F35 + 8 + 0x0035CA5F == 0x00B3D99C,
    "the MOVSS at 0x7E0F35 must address the first-person FOV constant");

// E-H2-22: MCC's halo_frame_interpolator (H2EK main\halo_frame_interpolator.cpp)
// keeps four first-person-weapon slots per local player, stride 0x33D4, in
// two buffers (previous 0x164B2E8, current 0x164B2F0; 0x164B2E0 non-null
// while the interpolator is allocated). The weapon the Saber renderer draws
// is INTERPOLATED between the previous and current game tick while the
// observer camera is per frame - the Anniversary weapon "swims" against the
// world by the interpolation. 0x723010(player, slot) is the engine's own
// slot reset (H2EK 0xD8320, called by first_person_weapons on a weapon
// change): it zeroes the slot's sample stamp (+0x1A06008) in the current
// buffer so the next render uses the current tick's state un-interpolated.
inline constexpr uint32_t kHalo2FrameInterpolatorResetFirstPersonSlotRva = 0x00723010;
inline constexpr uint8_t kHalo2FrameInterpolatorResetFirstPersonSlotEntryBytes[] = {
    0x48, 0x83, 0x3D, 0xC8, 0x82, 0xF2, 0x00, 0x00, 0x74, 0x23,
    0x48, 0x63, 0xC2, 0x48, 0x63, 0xC9, 0x48, 0x8D, 0x0C, 0x88};
inline constexpr uint32_t kHalo2FrameInterpolatorAllocatedSlotRva = 0x0164B2E0;
inline constexpr int kHalo2FrameInterpolatorFirstPersonSlots = 4;

inline constexpr uint32_t kHalo2ForcedLegacyFadingFlagRva = 0x015A3D91;
inline constexpr uint32_t kHalo2ForcedLegacyFadeStateSlotRva = 0x015A3D88;
inline constexpr uint32_t kHalo2ForcedLegacyFadeTargetOffset = 0x12A;

// The switch inputs the mod can see at the instant the applier runs.
struct Halo2SwitchInputEvidence
{
    bool tabDown = false;              // GetAsyncKeyState(VK_TAB) high bit
    bool physicalBackDown = false;     // raw pad Back in the most recent poll
    uint64_t physicalBackAgeMs = UINT64_MAX;   // since the raw pad last had Back
    bool physicalBackPassThrough = false;      // halo2_gamepad_graphics_switch
    uint64_t virtualBackAgeMs = UINT64_MAX;    // since the mod last FED Back
};

// A switch has an input behind it when Tab is held, the physical pad's Back
// is held AND passes through, or the mod itself fed Back within the last
// 250 ms (the head-gesture held click).
constexpr uint64_t kHalo2SwitchInputWindowMs = 250;
constexpr bool Halo2SwitchInputPresent(const Halo2SwitchInputEvidence& e) noexcept
{
    return e.tabDown ||
        (e.physicalBackDown && e.physicalBackPassThrough) ||
        e.virtualBackAgeMs <= kHalo2SwitchInputWindowMs;
}

enum class Halo2RenderModeSwitchDecision : uint8_t
{
    NoChange = 0,   // request equals the applied mode: nothing to decide
    Honour,         // a switch input is present: the engine switches
    Suppress,       // no input behind it: write the request back
};

constexpr Halo2RenderModeSwitchDecision Halo2DecideRenderModeSwitch(
    int32_t requested, int32_t applied, bool switchInputPresent) noexcept
{
    if (requested == applied)
        return Halo2RenderModeSwitchDecision::NoChange;
    return switchInputPresent ? Halo2RenderModeSwitchDecision::Honour
                              : Halo2RenderModeSwitchDecision::Suppress;
}

constexpr Halo2GraphicsMode Halo2GraphicsModeFromAppliedMode(
    int32_t appliedMode) noexcept
{
    return appliedMode == 0 ? Halo2GraphicsMode::Classic
                            : Halo2GraphicsMode::Remastered;
}

// The gate byte is the render-side truth: it is what the classic driver
// actually tests. A candidate must agree with it, not with the mode dword
// alone, because the applier writes the dword and the byte in that order.
constexpr bool Halo2ClassicRenderTreeRuns(
    uint8_t classicDisabledByte) noexcept
{
    return classicDisabledByte == 0;
}

// Stage 3AK: the particle renderer's second register argument is the engine's
// current-user / first-person classification. Suppression is deliberately the
// conjunction of that classification and the live Classic render gate: world
// particles, non-player callers and Anniversary all retain stock behavior.
constexpr bool Halo2ShouldSuppressClassicFirstPersonParticle(
    uint8_t classicDisabledByte,
    uint8_t currentUserFirstPerson) noexcept
{
    return currentUserFirstPerson != 0 &&
        Halo2ClassicRenderTreeRuns(classicDisabledByte);
}

constexpr bool Halo2GraphicsModeIsCoherent(
    int32_t appliedMode, uint8_t classicDisabledByte) noexcept
{
    return Halo2ClassicRenderTreeRuns(classicDisabledByte) ==
        (Halo2GraphicsModeFromAppliedMode(appliedMode) ==
         Halo2GraphicsMode::Classic);
}

// A classic-path render hook receiving zero callbacks is expected, not broken,
// while the remastered renderer owns the frame. This is the decision that four
// earlier candidates lacked.
constexpr bool Halo2ClassicRenderHookCanFire(
    Halo2GraphicsMode mode, uint8_t classicDisabledByte) noexcept
{
    return mode == Halo2GraphicsMode::Classic &&
        Halo2ClassicRenderTreeRuns(classicDisabledByte);
}

// ---------------------------------------------------------------------------
// E-H2-4: the observer is the one camera root upstream of BOTH renderers.
// ---------------------------------------------------------------------------

inline constexpr uint32_t kHalo2ObserverArrayRva = 0x015F28B8;
inline constexpr uint32_t kHalo2ObserverStride = 0x368;
inline constexpr uint32_t kHalo2ObserverCount = 4;
inline constexpr uint32_t kHalo2ObserverResultOffset = 0xC4;
inline constexpr uint32_t kHalo2ObserverResultArrayRva = 0x015F297C;
inline constexpr uint32_t kHalo2ObserverHeaderSignature = 0x72616421;
inline constexpr uint32_t kHalo2ObserverTrailerOffset = 0x360;
inline constexpr uint32_t kHalo2ObserverUpdatedForFrameOffset = 0xC0;

// observer_get_result(user): imul rax,rbx,0x368 / lea rcx,[rip+disp32] /
// add rax,rcx. Unique in the complete mapped image; the stride is inside the
// signature itself, so a moved array cannot silently change the element size.
inline constexpr char kHalo2ObserverResultAccessorPattern[] =
    "48 69 C3 68 03 00 00 48 8D 0D ?? ?? ?? ?? 48 03 C1";
inline constexpr uint32_t kHalo2ObserverResultAccessorRva = 0x006F0E79;
inline constexpr uint32_t kHalo2ObserverResultAccessorDispOffset = 10;
inline constexpr uint32_t kHalo2ObserverResultAccessorNextOffset = 14;
inline constexpr uint32_t kHalo2ObserverResultAccessorStrideOffset = 3;

// s_observer_result. The camera builder 0x7DF5A0 copies exactly these fields.
inline constexpr uint32_t kHalo2ObserverResultPositionOffset = 0x00;
inline constexpr uint32_t kHalo2ObserverResultForwardOffset = 0x20;
inline constexpr uint32_t kHalo2ObserverResultUpOffset = 0x2C;
inline constexpr uint32_t kHalo2ObserverResultHorizontalFovOffset = 0x38;
inline constexpr uint32_t kHalo2ObserverResultAspectOffset = 0x3C;
inline constexpr uint32_t kHalo2ObserverResultVerticalFovOffset = 0x4C;
inline constexpr uint32_t kHalo2ObserverResultFovRatioOffset = 0x50;
inline constexpr uint32_t kHalo2ObserverResultOwnedBytes = 0x54;

constexpr bool Halo2ObserverVisibilityFovWriteAllowed(
    uint32_t resultRelativeOffset, uint32_t bytes) noexcept
{
    return resultRelativeOffset == kHalo2ObserverResultVerticalFovOffset &&
        bytes == sizeof(float);
}

// The last per-frame writer of position/forward/up. Injecting after this
// function returns is the only point where no engine code overwrites the pose
// again before either renderer consumes it.
inline constexpr uint32_t kHalo2ObserverFinalTransformRva = 0x006F0250;
inline constexpr uint32_t kHalo2ObserverUpdateAllRva = 0x006F1A60;
inline constexpr uint32_t kHalo2ObserverResultDeriveRva = 0x006F10E0;

constexpr uint64_t Halo2ObserverRecordOffset(uint32_t user) noexcept
{
    return static_cast<uint64_t>(user) * kHalo2ObserverStride;
}

constexpr uint64_t Halo2ObserverResultOffsetForUser(uint32_t user) noexcept
{
    return Halo2ObserverRecordOffset(user) + kHalo2ObserverResultOffset;
}

constexpr bool Halo2ObserverUserValid(uint32_t user) noexcept
{
    return user < kHalo2ObserverCount;
}

// The three owned observer spans, expressed exactly as the engine stores them.
// They are NOT contiguous, so a whole-struct copy is forbidden: fields between
// them belong to the engine.
enum class Halo2ObserverSpan : uint8_t
{
    Position = 0,
    Forward,
    Up,
};
inline constexpr uint32_t kHalo2ObserverOwnedSpanCount = 3;

constexpr uint32_t Halo2ObserverSpanOffset(Halo2ObserverSpan span) noexcept
{
    return span == Halo2ObserverSpan::Position
        ? kHalo2ObserverResultPositionOffset
        : (span == Halo2ObserverSpan::Forward
               ? kHalo2ObserverResultForwardOffset
               : kHalo2ObserverResultUpOffset);
}

constexpr uint32_t Halo2ObserverSpanBytes(Halo2ObserverSpan) noexcept
{
    return kHalo2CameraVectorBytes;
}

// Only the three pose vectors may ever be written. Field of view, aspect,
// cluster/leaf indices and velocity remain engine-owned: the vertical FOV at
// +0x4C is derived by 0x6F1700 from +0x38 and +0x3C, so writing one without
// the other produces an incoherent projection.
constexpr bool Halo2ObserverSpanWriteAllowed(
    uint32_t resultRelativeOffset, size_t bytes) noexcept
{
    if (bytes != kHalo2CameraVectorBytes)
        return false;
    return resultRelativeOffset == kHalo2ObserverResultPositionOffset ||
        resultRelativeOffset == kHalo2ObserverResultForwardOffset ||
        resultRelativeOffset == kHalo2ObserverResultUpOffset;
}

// ---------------------------------------------------------------------------
// The Blam to Saber camera hand-off, replicated exactly from halo2.dll
// 0x5F510 so no conversion is invented. Saber works in metres and swaps axes:
//   saber(v) = (v.x, v.z, -v.y)
// row0 = saber(forward) x saber(up), row1 = saber(up), row2 = saber(forward),
// row3 = saber(position) * 3.048 + offsets + forwardScale * saber(forward).
// ---------------------------------------------------------------------------

inline constexpr uint32_t kHalo2SaberCameraBridgeRva = 0x0005F510;
inline constexpr uint32_t kHalo2SaberCameraPushRva = 0x00051510;
inline constexpr uint32_t kHalo2SaberFrameDriverRva = 0x000515E0;
inline constexpr uint32_t kHalo2SaberSceneSlotRva = 0x01E91210;
inline constexpr uint32_t kHalo2SaberSceneCameraListOffset = 0x100;
inline constexpr uint32_t kHalo2SaberSceneCameraCountOffset = 0x108;
inline constexpr uint32_t kHalo2SaberCameraMatrixOffset = 0x00;
inline constexpr uint32_t kHalo2SaberCameraFovChangedOffset = 0x14C;
// E-H2-7 (2026-08-21): +0x150 is the HORIZONTAL and +0x154 the VERTICAL
// field of view in degrees, proven three ways: the projection builder
// 0x1C82C0 maps +0x150 to P[0][0] (0x1C8386) and +0x154 to P[1][1]
// (0x1C8390); the constructor's aspect is 0.75 for 640x480, i.e. HEIGHT /
// WIDTH (0xBC162); and 0xBC4F0(cam, 80) writes +0x150 = 80 and +0x154 =
// 2*atan(tan 40 deg * 0.75) = 64 deg, the classic 4:3 pair. The earlier
// constants had the two names swapped.
inline constexpr uint32_t kHalo2SaberCameraHorizontalFovDegreesOffset = 0x150;
inline constexpr uint32_t kHalo2SaberCameraVerticalFovDegreesOffset = 0x154;
inline constexpr uint32_t kHalo2SaberCameraAspectOffset = 0x158;
inline constexpr uint32_t kHalo2SaberCameraNearOffset = 0x80;
// 0xBC560(cam, verticalDegrees): +0x154 = V, +0x150 = 2*atan(tan(V/2) /
// aspect), then jmp 0xBC380.
inline constexpr uint32_t kHalo2SaberCameraSetFovRva = 0x000BC560;
// 0xBC380(cam): refreshes the near-plane rectangle/polygon and the
// pixels-per-metre fields (+0x88..+0xA8, +0x12C..+0x138, +0x15C/+0x160)
// from +0x150/+0x154/+0x80/+0x144/+0x148. Reads neither +0x158 nor the
// matrix. Writing both degree fields and calling this is the engine's own
// way to a field of view that is not aspect-locked.
inline constexpr uint32_t kHalo2SaberCameraRefreshRectRva = 0x000BC380;
inline constexpr uint8_t kHalo2SaberCameraRefreshRectEntryBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0xF3, 0x0F, 0x10, 0x81, 0x50, 0x01,
    0x00, 0x00};
inline constexpr uint32_t kHalo2SaberCameraCommitRva = 0x000BC2B0;
inline constexpr uint8_t kHalo2SaberCameraCommitEntryBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
    0x83, 0xEC, 0x40};

// Saber render thread: frame entry -> view loop -> scene render. The view loop
// already runs the scene render more than once per frame, which is why a
// per-eye second pass is native rather than a replay.
inline constexpr uint32_t kHalo2SaberFrameRenderRva = 0x002DC3D0;
inline constexpr uint32_t kHalo2SaberViewLoopRva = 0x002DEC00;
inline constexpr uint32_t kHalo2SaberSceneRenderRva = 0x002DF190;

// Engine-owned translation constants read by 0x5F510. They are module globals,
// not literals, so a candidate must read them rather than bake them in.
inline constexpr uint32_t kHalo2SaberOffsetXRva = 0x01E8D058;
inline constexpr uint32_t kHalo2SaberOffsetZRva = 0x01E8D05C;
inline constexpr uint32_t kHalo2SaberOffsetYRva = 0x01E8D060;
inline constexpr uint32_t kHalo2SaberForwardScaleRva = 0x01E8D08C;

struct Halo2SaberViewMatrix
{
    float m[16]{};
};

struct Halo2SaberCameraConstants
{
    float offsetX = 0.0f;
    float offsetZ = 0.0f;
    float offsetY = 0.0f;
    float forwardScale = 0.0f;
};

inline bool Halo2SaberCameraConstantsFinite(
    const Halo2SaberCameraConstants& constants) noexcept
{
    return std::isfinite(constants.offsetX) &&
        std::isfinite(constants.offsetZ) &&
        std::isfinite(constants.offsetY) &&
        std::isfinite(constants.forwardScale);
}

inline bool Halo2BuildSaberViewMatrix(
    const Halo2CameraBasis& basis,
    const Halo2SaberCameraConstants& constants,
    Halo2SaberViewMatrix& out) noexcept
{
    if (!Halo2ValidateCameraBasis(basis) ||
        !Halo2SaberCameraConstantsFinite(constants))
    {
        return false;
    }

    const float forward[3] = {
        basis.forward[0], basis.forward[2], -basis.forward[1]};
    const float up[3] = {basis.up[0], basis.up[2], -basis.up[1]};
    const float right[3] = {
        forward[1] * up[2] - forward[2] * up[1],
        forward[2] * up[0] - forward[0] * up[2],
        forward[0] * up[1] - forward[1] * up[0]};

    Halo2SaberViewMatrix candidate{};
    candidate.m[0] = right[0];
    candidate.m[1] = right[1];
    candidate.m[2] = right[2];
    candidate.m[3] = 0.0f;
    candidate.m[4] = up[0];
    candidate.m[5] = up[1];
    candidate.m[6] = up[2];
    candidate.m[7] = 0.0f;
    candidate.m[8] = forward[0];
    candidate.m[9] = forward[1];
    candidate.m[10] = forward[2];
    candidate.m[11] = 0.0f;
    candidate.m[12] = basis.position[0] * kHalo2MetersPerWorldUnit +
        constants.offsetX + constants.forwardScale * forward[0];
    candidate.m[13] = basis.position[2] * kHalo2MetersPerWorldUnit +
        constants.offsetZ + constants.forwardScale * forward[1];
    candidate.m[14] = (constants.offsetY -
                       basis.position[1] * kHalo2MetersPerWorldUnit) +
        constants.forwardScale * forward[2];
    candidate.m[15] = 1.0f;

    for (int index = 0; index < 16; ++index)
        if (!std::isfinite(candidate.m[index]))
            return false;
    out = candidate;
    return true;
}

// 0xBC560(cam, V) stores the VERTICAL field of view and derives the
// HORIZONTAL one as 2*atan(tan(V/2) / aspect), where the Saber aspect is
// HEIGHT / WIDTH (0.75 for 640x480). This mirrors that derivation exactly;
// degrees, as the engine stores.
inline bool Halo2SaberHorizontalFovDegreesFromVertical(
    float verticalFovDegrees, float aspectHeightOverWidth,
    float& outHorizontalDegrees) noexcept
{
    constexpr float kPi = 3.14159265f;
    if (!std::isfinite(verticalFovDegrees) ||
        !std::isfinite(aspectHeightOverWidth) ||
        verticalFovDegrees <= 0.0f || verticalFovDegrees >= 180.0f ||
        aspectHeightOverWidth <= 0.0f)
    {
        return false;
    }
    const float halfRadians = verticalFovDegrees * 0.5f * kPi / 180.0f;
    const float tangent = std::tan(halfRadians);
    if (!std::isfinite(tangent) || tangent <= 0.0f)
        return false;
    const float horizontal = std::atan(tangent / aspectHeightOverWidth);
    if (!std::isfinite(horizontal) || horizontal <= 0.0f)
        return false;
    outHorizontalDegrees = (horizontal + horizontal) * 180.0f / kPi;
    return std::isfinite(outHorizontalDegrees) &&
        outHorizontalDegrees > 0.0f && outHorizontalDegrees < 180.0f;
}

// The headset cover for one Saber eye pass: the smallest symmetric
// horizontal/vertical pair that contains both eyes' native frusta, each
// axis solved on its own because 0x1C82C0 takes the two fields
// independently (no aspect lock). Half-angles in radians for the
// compositor, full angles in degrees for the camera record.
struct Halo2SaberEyeCover
{
    float halfHorizontalRadians = 0.0f;
    float halfVerticalRadians = 0.0f;
    float horizontalDegrees = 0.0f;
    float verticalDegrees = 0.0f;
};

inline bool Halo2DeriveSaberEyeCover(
    const float leftFov[4], const float rightFov[4],
    Halo2SaberEyeCover& out) noexcept
{
    constexpr float kPi = 3.14159265f;
    constexpr float kMaximumAngle = 1.56f;
    constexpr float kCoverMargin = 1.002f;
    if (!leftFov || !rightFov)
        return false;
    float requiredTanX = 0.0f;
    float requiredTanY = 0.0f;
    const float* views[2] = {leftFov, rightFov};
    for (const float* fov : views)
    {
        // OpenXR order: left (<0), right (>0), up (>0), down (<0).
        if (!(fov[0] < 0.0f) || !(fov[1] > 0.0f) || !(fov[2] > 0.0f) ||
            !(fov[3] < 0.0f))
        {
            return false;
        }
        for (int index = 0; index < 4; ++index)
            if (!std::isfinite(fov[index]) ||
                std::fabs(fov[index]) >= kMaximumAngle)
                return false;
        requiredTanX = (std::max)(requiredTanX,
            (std::max)(std::fabs(std::tan(fov[0])),
                       std::fabs(std::tan(fov[1]))));
        requiredTanY = (std::max)(requiredTanY,
            (std::max)(std::fabs(std::tan(fov[2])),
                       std::fabs(std::tan(fov[3]))));
    }
    Halo2SaberEyeCover candidate{};
    candidate.halfHorizontalRadians =
        std::atan(requiredTanX * kCoverMargin);
    candidate.halfVerticalRadians =
        std::atan(requiredTanY * kCoverMargin);
    candidate.horizontalDegrees =
        2.0f * candidate.halfHorizontalRadians * 180.0f / kPi;
    candidate.verticalDegrees =
        2.0f * candidate.halfVerticalRadians * 180.0f / kPi;
    if (!std::isfinite(candidate.halfHorizontalRadians) ||
        !std::isfinite(candidate.halfVerticalRadians) ||
        candidate.halfHorizontalRadians <= 0.01f ||
        candidate.halfVerticalRadians <= 0.01f ||
        candidate.halfHorizontalRadians >= 1.55f ||
        candidate.halfVerticalRadians >= 1.55f)
    {
        return false;
    }
    out = candidate;
    return true;
}

// E-H2-13: the same cover, ASPECT-LOCKED the way the engine's own setter
// 0xBC560 keeps it (+0x150 = 2*atan(tan(+0x154/2) / aspect), aspect =
// height/width at +0x158). C-H2-12/17 wrote the two axes independently
// (108 x 110 deg on a 2912x2100 raster); the scene projection honoured that,
// but the first-person weapon came out squashed tall - a consumer that
// derives one axis from the other and the aspect, as 0xBC560 does, sees a
// different frustum than the scene. The vertical cover is the smallest that
// contains both eyes on both axes once the horizontal is derived from it.
inline bool Halo2DeriveSaberAspectLockedEyeCover(
    const float leftFov[4], const float rightFov[4],
    float aspectHeightOverWidth, Halo2SaberEyeCover& out) noexcept
{
    constexpr float kPi = 3.14159265f;
    Halo2SaberEyeCover perAxis{};
    if (!std::isfinite(aspectHeightOverWidth) ||
        aspectHeightOverWidth <= 1.0e-3f ||
        !Halo2DeriveSaberEyeCover(leftFov, rightFov, perAxis))
    {
        return false;
    }
    // Required tangents already carry the margin; lock: tanX = tanY / aspect.
    const float requiredTanX = std::tan(perAxis.halfHorizontalRadians);
    const float requiredTanY = std::tan(perAxis.halfVerticalRadians);
    const float lockedTanY =
        (std::max)(requiredTanY, requiredTanX * aspectHeightOverWidth);
    Halo2SaberEyeCover candidate{};
    candidate.halfVerticalRadians = std::atan(lockedTanY);
    candidate.verticalDegrees =
        2.0f * candidate.halfVerticalRadians * 180.0f / kPi;
    if (!Halo2SaberHorizontalFovDegreesFromVertical(
            candidate.verticalDegrees, aspectHeightOverWidth,
            candidate.horizontalDegrees))
    {
        return false;
    }
    candidate.halfHorizontalRadians =
        candidate.horizontalDegrees * 0.5f * kPi / 180.0f;
    if (!std::isfinite(candidate.halfHorizontalRadians) ||
        !std::isfinite(candidate.halfVerticalRadians) ||
        candidate.halfHorizontalRadians <= 0.01f ||
        candidate.halfVerticalRadians <= 0.01f ||
        candidate.halfHorizontalRadians >= 1.55f ||
        candidate.halfVerticalRadians >= 1.55f ||
        // Containment on both axes, with the per-axis margin as the floor.
        std::tan(candidate.halfHorizontalRadians) + 1.0e-4f < requiredTanX ||
        std::tan(candidate.halfVerticalRadians) + 1.0e-4f < requiredTanY)
    {
        return false;
    }
    out = candidate;
    return true;
}

// E-H2-62 / C-H2-71. observer_result +0x4C is the shared upstream visibility
// FOV: Classic copies it to camera +0x28, while the Blam->Saber bridge passes it
// through 0xBC560 before 0x1C7740 copies that camera into a view record.  The
// late eye transaction already rasterises the cover returned here.  Preserve
// an authored engine FOV when it is wider; this helper can only expand culling.
inline bool Halo2DeriveObserverVisibilityVerticalFov(
    const float leftFov[4], const float rightFov[4],
    float stockVerticalRadians, float& outVerticalRadians,
    bool guardBand = false) noexcept
{
    constexpr float kPi = 3.14159265f;
    if (!std::isfinite(stockVerticalRadians) ||
        stockVerticalRadians <= 1.0e-4f ||
        stockVerticalRadians >= kPi - 1.0e-4f)
    {
        return false;
    }
    Halo2SaberEyeCover cover{};
    if (!Halo2DeriveSaberEyeCover(leftFov, rightFov, cover))
        return false;
    const float required = guardBand
        ? 2.0f*std::atan(ExpandVisibilityTangent(std::tan(cover.halfVerticalRadians)))
        : cover.verticalDegrees * kPi / 180.0f;
    if (!std::isfinite(required) || required <= 1.0e-4f ||
        required >= kPi - 1.0e-4f)
    {
        return false;
    }
    outVerticalRadians = (std::max)(stockVerticalRadians, required);
    return std::isfinite(outVerticalRadians);
}

// ---------------------------------------------------------------------------
// The remastered renderer's per-view record. Byte-verified at 0x2DF2C5:
//   record = *(halo2 + kHalo2SaberViewCollectionSlotRva)
//          + kHalo2SaberViewRecordArrayOffset
//          + viewIndex * kHalo2SaberViewRecordStride
// The scene render latches the embedded camera copy at record+0x20 and the
// record itself into its render context, and never reads the Saber camera
// object. A per-eye camera therefore lives here, not in that object.
// ---------------------------------------------------------------------------
inline constexpr uint32_t kHalo2SaberViewCollectionSlotRva = 0x01A250F8;
inline constexpr uint32_t kHalo2SaberViewCountOffset = 0x148;
inline constexpr uint32_t kHalo2SaberViewRecordArrayOffset = 0x150;
inline constexpr uint32_t kHalo2SaberViewRecordStride = 0x758;
inline constexpr uint32_t kHalo2SaberViewRecordCapacity = 0x50;
// E-H2-21: the view record's first dword is its flags; 0x2DEC00 renders a
// record as a MAIN view only when (flags & 0x4C) == 0 and as a water-mirror
// view when bit 3 is set. Only a main view may claim the eye pair.
inline constexpr uint32_t kHalo2SaberViewRecordFlagsOffset = 0x0;
inline constexpr uint32_t kHalo2SaberViewRecordNonMainFlags = 0x4C;
constexpr bool Halo2SaberViewRecordIsMainView(uint32_t flags) noexcept
{
    return (flags & kHalo2SaberViewRecordNonMainFlags) == 0;
}

// E-H2-21: two Saber camera matrices describe the same pose when their
// rotation rows agree to `rotationTolerance` and their translations (metres)
// to `translationToleranceMeters`.
inline bool Halo2SaberViewMatricesMatch(
    const float a[16], const float b[16], float rotationTolerance,
    float translationToleranceMeters) noexcept
{
    if (!a || !b)
        return false;
    for (int i = 0; i < 16; ++i)
    {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
            return false;
        const bool translation = i >= 12 && i < 15;
        const bool homogeneous = (i % 4) == 3;
        if (homogeneous)
            continue;
        const float tolerance =
            translation ? translationToleranceMeters : rotationTolerance;
        if (std::fabs(a[i] - b[i]) > tolerance)
            return false;
    }
    return true;
}

inline constexpr uint32_t kHalo2SaberViewRecordCameraOffset = 0x20;
inline constexpr uint32_t kHalo2SaberViewRecordCameraBytes = 0x398;
inline constexpr uint32_t kHalo2SaberViewRecordViewIdOffset = 0x10;
// E-H2-7/E-H2-12: 0x1C6D80 bakes the record's projection at +0x4AC through
// 0x1C82C0 (puVar15 = record + 299*4), row-vector float[16]: [0] =
// 1/tan(horizontal/2), [5] = 1/tan(vertical/2). Read after the scene render
// returns, it is the frustum that eye was drawn with.
inline constexpr uint32_t kHalo2SaberViewRecordProjectionOffset = 0x4AC;
inline constexpr uint32_t kHalo2SaberProjectionScaleXOffset =
    kHalo2SaberViewRecordProjectionOffset + 0 * sizeof(float);
inline constexpr uint32_t kHalo2SaberProjectionScaleYOffset =
    kHalo2SaberViewRecordProjectionOffset + 5 * sizeof(float);
static_assert(kHalo2SaberViewRecordProjectionOffset + 16 * sizeof(float) <=
    kHalo2SaberViewRecordStride);

// The engine's own rebuild of every derived matrix in the record. It takes
// FOUR arguments: 0x1C7740 clears r9 too (0x1C790D xor r9d,r9d) and
// 0x1C6D80 stores r9 (0x1C6DD7) and, when non-null, dereferences it as a
// float[4] clip plane and rewrites the projection. It must be called with
// all three pointers null. It reads +0x150/+0x154 in DEGREES through
// 0x1C82C0 (P[0][0] = 1/tan(h/2), P[1][1] = 1/tan(v/2)); no clamp exists.
inline constexpr uint32_t kHalo2SaberRebuildViewMatricesRva = 0x001C6D80;
inline constexpr uint8_t kHalo2SaberRebuildViewMatricesEntryBytes[] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x49, 0x8D, 0xAB, 0xA8, 0xFB, 0xFF, 0xFF};

// E-H2-18: 0x1C6D80 builds TWO projections into the view record. The world
// one (record+0x4AC, from the camera's +0x150/+0x154 degrees) and a
// first-person one at record+0x4EC built from a camera copy whose vertical
// FOV is the literal 0x424660D5 = 49.594 degrees (tanf(0.43279424) = tan of
// its half-angle is precomputed) with the horizontal derived through the
// record's own aspect. It then stores view-without-translation x world
// projection at +0x56C (the product whose inverse goes to +0x5AC) and
// view-without-translation x first-person projection at +0x5EC. MCC keeps
// the first-person weapon at a constant screen fraction across its FOV
// slider with this second projection; at the headset's 126 x 110 degree
// cover that draws the weapon tan(55)/tan(24.8) = 3.1x larger than the
// world around it, and moves it 3.1x further per eye than the world does.
// E-H2-26 (C-H2-33): row-major 4x4 multiply and the view-without-translation
// of a Saber camera matrix. The engine's own +0x56C is
// viewNoTranslation(frame camera) x world projection; the mod reconstructs
// that same product and only uses its own matrices once the reconstruction
// MATCHES what the engine wrote, so no matrix convention is assumed.
inline void Halo2MultiplyMatrix4x4(
    const float* a, const float* b, float* out) noexcept
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[row * 4 + k] * b[k * 4 + column];
            out[row * 4 + column] = sum;
        }
    }
}

// The camera matrix holds basis ROWS and a translation row; the view without
// translation is therefore the transpose of its 3x3 rotation.
inline void Halo2SaberViewWithoutTranslation(
    const float* cameraMatrix, float* out) noexcept
{
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            out[row * 4 + column] = cameraMatrix[column * 4 + row];
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f;
    out[12] = 0.0f; out[13] = 0.0f; out[14] = 0.0f; out[15] = 1.0f;
}

inline bool Halo2MatricesClose(
    const float* a, const float* b, float tolerance) noexcept
{
    for (int index = 0; index < 16; ++index)
    {
        if (!std::isfinite(a[index]) || !std::isfinite(b[index]))
            return false;
        const float scale = std::fabs(b[index]) > 1.0f ? std::fabs(b[index]) : 1.0f;
        if (std::fabs(a[index] - b[index]) > tolerance * scale)
            return false;
    }
    return true;
}

// E-H2-27 (C-H2-33): a typeless render target cannot back a default shader
// resource view, so the classic capture could never sample the engine's own
// scene target (the 93cdc1a/8cb89b6 census: bound target #0 is 3788x2732
// format 90 = B8G8R8A8_TYPELESS, two pixels wider than the backbuffer, and
// every probe of it reported "not sampled").
inline uint32_t Halo2ConcreteFormat(uint32_t format) noexcept
{
    switch (format)
    {
    case 90: return 87;   // B8G8R8A8_TYPELESS -> B8G8R8A8_UNORM
    case 92: return 88;   // B8G8R8X8_TYPELESS -> B8G8R8X8_UNORM
    case 27: return 28;   // R8G8B8A8_TYPELESS -> R8G8B8A8_UNORM
    case 10: return 11;   // R16G16B16A16_TYPELESS -> _UNORM
    case 1:  return 2;    // R32G32B32A32_TYPELESS -> _FLOAT
    case 23: return 24;   // R10G10B10A2_TYPELESS -> _UNORM
    default: return format;
    }
}

inline bool Halo2FormatIsTypeless(uint32_t format) noexcept
{
    return Halo2ConcreteFormat(format) != format;
}

inline constexpr uint32_t kHalo2SaberViewRecordFirstPersonProjectionOffset = 0x4EC;
inline constexpr uint32_t kHalo2SaberViewRecordViewProjectionNoTranslationOffset = 0x56C;
inline constexpr uint32_t kHalo2SaberViewRecordFirstPersonViewProjectionOffset = 0x5EC;
inline constexpr uint32_t kHalo2SaberMatrixFloats = 16;
inline constexpr float kHalo2SaberFirstPersonVerticalFovDegrees = 49.594f;

// The engine's stock first-person half-angles for a record aspect (h/w):
// vertical = 49.594/2, horizontal = atan(tan(vertical) / aspect).
inline bool Halo2SaberFirstPersonStockHalfFovs(
    float aspectHeightOverWidth, Halo2SymmetricHalfFovs& out) noexcept
{
    if (!std::isfinite(aspectHeightOverWidth) || aspectHeightOverWidth <= 0.05f ||
        aspectHeightOverWidth > 20.0f)
    {
        return false;
    }
    const float vertical =
        kHalo2SaberFirstPersonVerticalFovDegrees * 0.5f * 3.14159265f / 180.0f;
    out.vertical = vertical;
    out.horizontal = std::atan(std::tan(vertical) / aspectHeightOverWidth);
    return std::isfinite(out.horizontal) && out.horizontal > 0.0f;
}

// Two Saber projections describe the same frustum when their diagonal scales
// agree (P[0] = 1/tan(h/2), P[5] = 1/tan(v/2)); the depth terms are shared.
inline bool Halo2SaberProjectionScalesMatch(
    const float a[16], const float b[16], float relativeTolerance) noexcept
{
    if (!a || !b || !std::isfinite(relativeTolerance) || relativeTolerance < 0.0f)
        return false;
    const int diagonals[2] = {0, 5};
    for (int d : diagonals)
    {
        if (!std::isfinite(a[d]) || !std::isfinite(b[d]) ||
            std::fabs(a[d]) < 1.0e-3f || std::fabs(b[d]) < 1.0e-3f)
        {
            return false;
        }
        if (std::fabs(a[d] - b[d]) > relativeTolerance * std::fabs(b[d]))
            return false;
    }
    return true;
}

// E-H2-18: the Saber host's post-scene callback. 0x2819A0 (reached from the
// frame at 0x2DEC00 after every view has rendered, on the Saber render
// thread) calls the function pointer the host stored at 0x1A6E538 during
// start-up (0x69730): 0x696A0 -> 0x69540 -> 0x960230(1) -> 0x7E1990 ->
// 0x831CB0, the same Blam interface/HUD draw the classic render_view
// (0x7E30D0) calls. It runs ONCE per frame over whatever the backbuffer
// holds, which is why the remastered HUD never reached the eye images.
inline constexpr uint32_t kHalo2SaberHostUiCallbackRva = 0x000696A0;
inline constexpr uint8_t kHalo2SaberHostUiCallbackEntryBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x3D};
// Normalises the camera basis rows and produces the inverse at camera+0x40.
inline constexpr uint32_t kHalo2SaberCameraCommitRvaAlias =
    kHalo2SaberCameraCommitRva;

// The scene render consumes a once-per-frame latch and clears it to -1, so a
// second pass in the same frame must restore it or silently skip work.
inline constexpr uint32_t kHalo2SaberSceneOnceLatchRva = 0x00E21D30;
// Its callers zero this span of the render context before every call.
inline constexpr uint32_t kHalo2SaberSceneContextResetOffset = 0x10;
inline constexpr uint32_t kHalo2SaberSceneContextResetBytes = 0xC8;

// Screen-space reflections read a single frame-to-frame history colour target.
// One engine-owned byte disables that pass outright, which is the only way to
// keep two eyes in one frame from sharing one history.
inline constexpr uint32_t kHalo2SaberRenderSettingsSlotRva = 0x01A6E588;
inline constexpr uint32_t kHalo2SaberSsrEnableOffset = 0x108;

// E-H2-23 (C-H2-30): the first-person weapon placement, and the frame's own
// head sample.
//
// Retail proof, independent of the H2EK kit: the interpolator's first-person
// slot reset (kHalo2FrameInterpolatorResetFirstPersonSlotRva) has exactly ONE
// caller, 0x818CA0 - that function IS first_person_weapons. Its two callers
// (0x81AE30 and 0x81BF20) both reach the main frame function 0x67A220 at
// +0x3FD (0x67A220 -> 0x3CAB0 -> 0x81BF20 -> 0x818CA0), while
// observer_update_all (0x6F1A60, the only caller of the observer transform
// 0x6F0250) runs LATER in the same frame at 0x67A220+0x428. So the weapon is
// placed against the observer record as it stands BEFORE this frame's observer
// update, and the Saber camera push (0x51510 -> 0x5F510) then carries the
// record as it stands AFTER it. Whatever head sample the observer applies at
// its own update time is therefore one frame newer than the one the weapon was
// placed with, and the observer republishes thousands of times a second - which
// is why choosing an older publication for the eyes (C-H2-29) could not make
// the weapon rigid: it only moved which end of the pair was stale.
//
// C-H2-30 latches ONE head sample per game frame at the weapon placement and
// makes the observer update consume that same sample, so the weapon, the
// camera push and the submitted eye poses are all built from one sample.
inline constexpr uint32_t kHalo2FirstPersonWeaponsRva = 0x00818CA0;
inline constexpr uint8_t kHalo2FirstPersonWeaponsEntryBytes[] = {
    0x48, 0x8B, 0xC4, 0x44, 0x88, 0x40, 0x18, 0x89, 0x50, 0x10,
    0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57};

// The record the weapon is placed against still holds the tracked pose the mod
// wrote at the previous observer update. Re-deriving from it would apply the
// head delta twice, so the latch rewrites it only while it still matches that
// exact published pose, from the stock pose published with it.
inline constexpr float kHalo2FrameLatchPoseEpsilon = 1.0e-6f;

inline bool Halo2CameraBasisMatchesExactly(
    const Halo2CameraBasis& a, const Halo2CameraBasis& b) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(a.position[axis] - b.position[axis]) >
                kHalo2FrameLatchPoseEpsilon ||
            std::fabs(a.forward[axis] - b.forward[axis]) >
                kHalo2FrameLatchPoseEpsilon ||
            std::fabs(a.up[axis] - b.up[axis]) > kHalo2FrameLatchPoseEpsilon)
        {
            return false;
        }
    }
    return true;
}

// E-H2-24 (C-H2-30): the classic eye image is not in either slot the capture
// learned from.
//
// The 93cdc1a Steam log, Classic: `IDENTICAL eyes (0/40527 samples differ)` on
// every check, with the two quarter-size dumps byte-identical on disk, while
// the draw census counted a full render inside EACH eye (118327 / 118704 draws)
// and the projection read-back AGREED for both. So the engine received the
// per-eye cameras and drew two different frames, but neither the final-output
// slot 0x197EE58 nor the primary scene slot 0x197EE60 changed between the two
// passes (`capture probes: ... pair changed 0.0%`). The classic final output
// 0x975230 has exactly that shape: when the render-to-texture flag 0x1996A17
// is set it resolves the frame into another target (0x9519B0 -> 0x9513D0) and
// returns WITHOUT drawing to the bound output; only the interface (0x831CB0)
// and the CHUD (0x7FFD70) reach the backbuffer afterwards. The per-eye image
// therefore lives in a target the two learned candidates never name.
//
// (The 0x975230 reading above is RETRACTED in E-H2-24's addendum: that
// early-out is the render-to-texture path, flag 0 for the player window.)
// C-H2-30 stops enumerating two fixed slots: the eye scope records every
// distinct slot-0 target the engine binds inside the eye, and the two probe
// caches rotate across that whole set until a candidate's two eyes differ.
// The rotation costs the same two probe copies per eye as before.
inline constexpr uint32_t kHalo2ClassicRenderToTextureFlagRva = 0x01996A17;

// E-H2-29 (C-H2-35): the classic render-target SELECTOR 0x9540F0 resolves the
// world pass's target id 0 through a once-per-frame byte:
//
//     case 0:
//       if (DAT_181994935 == 0) target = *0x197EE60;   // the scene target
//       else                    target = *0x197EE58;   // the backbuffer
//
// render_player_window (0x7E2130) clears that byte at 0x7E2368, before its one
// render_view; the postprocess 0x951EC0 sets it at 0x951F97, inside render_view.
// The classic stereo core calls render_view TWICE inside one
// render_player_window, so the byte is already 1 when the second eye starts and
// that eye's world pass binds a different target from the first eye's. It is
// the exact shape of the Saber once-per-frame latch the Anniversary core
// re-arms between its two passes (kHalo2SaberSceneOnceLatchRva).
inline constexpr uint32_t kHalo2ClassicSceneTargetLatchRva = 0x01994935;

// E-H2-30 (C-H2-36): MCC's frame interpolator blend factor. 0x723580 (called
// once per frame from the main frame function 0x67A220+0x618) does
// `movss [rip+0xF27D6F], xmm0` at 0x723589 -> RVA 0x164B300, and the
// interpolator's read side 0x722850 passes exactly that float as the third
// argument of 0x723040(previous, current, factor, out) - which takes it in
// xmm2. So the weapon the Saber renderer draws is not at the tick pose: it is
// at lerp(previous tick, current tick, factor). Drawing it with the view of
// the CURRENT tick therefore trails by up to a whole tick, which is what a
// fast turn exposes. The weapon's view is built from the same blend.
inline constexpr uint32_t kHalo2FrameInterpolatorFactorRva = 0x0164B300;

// E-H2-32 (C-H2-37): the first-person interpolator READ side.
// 0x722850(player, id, slot, node**, count*) hands the renderer the blended
// node array for one first-person slot: stride 0x34 per node, and 0x72A9C0 /
// 0x723040 prove the layout - +0x00 scale, +0x04 the node frame's three
// world-axis VECTORS in sequence (a quat (x,y,z,w) becomes [1-2(y2+z2),
// 2(xy+wz), 2(xz-wy)] first, i.e. the world image of the X axis, then Y, then
// Z), +0x28 the world position. Both of its call sites (0x81858D, 0x81872E in
// the first-person render 0x8181F0) run per FRAME, after the tick placed the
// weapon - the single point where the drawn weapon geometry can be re-anchored
// from the tick's camera to the frame's.
// E-H2-71 (C-H2-83): the interpolated FIRST-PERSON FRAME getter - the one
// the packet builder prefers for Classic and never uses for Anniversary.
// Kit homolog `halo_frame_interpolator.cpp` FUN_004d7a90 (kit RVA 0xD7A90);
// matched to the pinned retail module by the builder's own call order
// (build frame 0x729BA0 -> THIS -> inverse 0x729C90 -> compose 0x72A150,
// exactly the kit's sequence) and verified by body: identical 0x38 bank
// stride, identical bank/validity bases 0x1A39D44 / 0x1A39D48, the same
// validity-pair test, the same blend call, bool return.
//
// Retail call site 0x81843F inside the pinned builder 0x8181F0:
//     lea rbx, [r13 + 0x20C8]     ; the CURRENT tick frame
//     test r15b, r15b / je        ; renderer selector
//     call 0x7226F0               ; the interpolated frame
//     test al, al / cmove rdx, rbx; a FALSE result selects +0x20C8
// So reporting false makes the builder compose against exactly the frame
// Anniversary composes against.
inline constexpr uint32_t kHalo2FrameInterpolatorFirstPersonFrameRva =
    0x007226F0;
// The first 23 bytes are NOT unique: the immediately following sibling
// getter at 0x7227A0 shares that prologue, which is why C-H2-83's 23-byte
// pattern matched twice and the hook correctly refused to install. 24 bytes
// is the shortest unique prefix in the LOADED IMAGE (the raw file is not
// the right thing to count in); 28 is used for margin.
inline constexpr uint8_t
    kHalo2FrameInterpolatorFirstPersonFrameEntryBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x83, 0x3D, 0xD4, 0x8B, 0xF2, 0x00, 0x00};
inline constexpr uint32_t kHalo2FirstPersonCurrentFrameOffset = 0x20C8;

inline constexpr uint32_t kHalo2FrameInterpolatorReadRva = 0x00722850;
inline constexpr uint8_t kHalo2FrameInterpolatorReadEntryBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
    0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20};
inline constexpr uint32_t kHalo2FirstPersonNodeStride = 0x34;
inline constexpr uint32_t kHalo2FirstPersonNodeFloats =
    kHalo2FirstPersonNodeStride / 4;
inline constexpr uint32_t kHalo2FirstPersonNodeAxesOffset = 0x04;
inline constexpr uint32_t kHalo2FirstPersonNodePositionOffset = 0x28;
inline constexpr int kHalo2FirstPersonNodeLimit = 256;

// E-H2-43 (C-H2-50): final visible first-person palette composition. The
// generic matrix composer is admitted only from the two H2EK-homologous node
// loops in first_person render; all of its other callers remain byte-for-byte
// stock. The source pointer is additionally required to fall inside the exact
// interpolator bank witnessed immediately before the composition.
inline constexpr uint32_t kHalo2MatrixComposeRva = 0x0072A150;
inline constexpr uint32_t kHalo2FirstPersonPrimaryComposeReturnRva = 0x00818623;
inline constexpr uint32_t kHalo2FirstPersonSecondaryComposeReturnRva = 0x00818773;
inline constexpr uint8_t kHalo2MatrixComposeEntryBytes[] = {
    0x48, 0x83, 0xEC, 0x48, 0x49, 0x3B, 0xC8, 0x75, 0x24, 0x0F, 0x10,
    0x01, 0x8B, 0x41, 0x30, 0x0F, 0x10, 0x49, 0x10, 0x89, 0x44, 0x24,
    0x30, 0x0F, 0x11, 0x04, 0x24, 0x0F, 0x10, 0x41, 0x20, 0x48};

// E-H2-45: H2EK first_person_weapons.cpp's packet builder is the boundary
// handed to both the Classic renderer and the Anniversary/Saber bridge. Retail
// +0x8181F0 is its verified homolog and returns the number of 0xD0C-byte
// packets written to argument 7. Each packet is a 12-byte header followed by
// at most 64 final, root-composed 0x34-byte matrices. Owning this completed
// packet removes C-H2-50's hidden dependency on the optional interpolator read.
inline constexpr uint32_t kHalo2FirstPersonPacketBuilderRva = 0x008181F0;
inline constexpr uint8_t kHalo2FirstPersonPacketBuilderEntryBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x4C, 0x89, 0x4C, 0x24, 0x20,
    0x89, 0x54, 0x24, 0x10, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56,
    0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
// E-H2-53/E-H2-55: +0x8181F0 calls this registered renderer callback only when
// publish_to_renderer is true (Anniversary). Its supplied matrices are copied
// before the builder returns. Classic caller +0x7E5430 passes false and retains
// the returned persistent packet array for its own direct renderer consumer.
inline constexpr uint32_t kHalo2ClassicFirstPersonPacketCallerRva = 0x007E5430;
inline constexpr uint32_t kHalo2FirstPersonVisibleConsumerRva = 0x0006BB40;
inline constexpr uint8_t kHalo2FirstPersonVisibleConsumerEntryBytes[] = {
    0x48, 0x8B, 0xC4, 0x89, 0x48, 0x08, 0x55, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0x48, 0xFE,
    0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x90, 0x02, 0x00, 0x00};
inline constexpr uint32_t kHalo2FirstPersonUserDataPointerRva = 0x0187C300;
inline constexpr uint32_t kHalo2FirstPersonUserStride = 0x20FC;
inline constexpr uint32_t kHalo2FirstPersonWeaponDataOffset = 0x0C;
inline constexpr uint32_t kHalo2FirstPersonWeaponSlotStride = 0x1028;
inline constexpr uint32_t kHalo2FirstPersonWeaponObjectOffset = 0x04;
inline constexpr uint32_t kHalo2FirstPersonWeaponGraphOffset = 0x7C;
inline constexpr uint32_t kHalo2FirstPersonWeaponNodeCountOffset = 0x10C;
inline constexpr uint32_t kHalo2FirstPersonHandsNodeCountOffset = 0x110;
inline constexpr uint32_t kHalo2FirstPersonHandsRemapOffset = 0x214;
inline constexpr uint32_t kHalo2FirstPersonAnimationNodeCountOffset = 0x31C;
inline constexpr uint32_t kHalo2FirstPersonRenderPacketHeaderBytes = 0x0C;
inline constexpr uint32_t kHalo2FirstPersonRenderPacketStride = 0x0D0C;

// E-H2-54: official H2EK units.cpp `unit_update_aiming` (kit +0x48E350)
// updates desired_aiming_vector +0x168 and aiming_vector +0x174.  BSim maps it
// to retail +0x8FDF50; the retail decompile preserves those exact members.
inline constexpr uint32_t kHalo2NativeAimUpdateRva = 0x008FDF50;
inline constexpr uint8_t kHalo2NativeAimUpdateEntryBytes[] = {
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x8D, 0x6C,
    0x24, 0xD0, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00, 0x48,
    0x8B, 0x05};
inline constexpr uint32_t kHalo2ObjectsDataArrayPointerRva = 0x018B7398;
inline constexpr uint32_t kHalo2ObjectDataEntryStride = 0x0C;
inline constexpr uint32_t kHalo2ObjectDatumAccessorRva = 0x008D7000;
inline constexpr uint8_t kHalo2ObjectDatumAccessorEntryBytes[] = {
    0x8B, 0x51, 0x08, 0x48, 0x8B, 0x0D};
inline constexpr uint32_t kHalo2UnitDesiredAimingVectorOffset = 0x168;
inline constexpr uint32_t kHalo2UnitAimingVectorOffset = 0x174;
// H2EK unit_in_vehicle: HS evaluator 221D60 -> native 4DA690.
// Retail evaluator 785650 -> native 93FB20 checks this exact seat member.
inline constexpr uint32_t kHalo2UnitInVehicleRva = 0x0093FB20;
inline constexpr uint32_t kHalo2UnitParentSeatOffset = 0x210;
inline constexpr char kHalo2UnitInVehiclePattern[] =
    "40 53 48 83 EC 20 32 DB 83 F9 FF 74 45 48 8B 05 ?? ?? ?? ?? "
    "48 8B 50 48 48 85 D2 74 06 4C 8D 04 02 EB 03 45 33 C0 "
    "0F B7 C9 48 8D 04 49 49 8D 0C 80 E8 ?? ?? ?? ?? 0F B6 CB "
    "BA 01 00 00 00 66 83 B8 10 02 00 00 FF 0F 45 CA";

// ---------------------------------------------------------------------------
// E-H2-40 / E-H2-41 (C-H2-47): Halo 2 tags its own hands.
//
// Every animation-graph skeleton node carries a `model flags` byte whose bits
// are named, in the official H2EK's own flag-name array, primary model /
// secondary model / local root / left hand / right hand / left arm member, plus
// a parent index. The retail engine reads exactly those fields for exactly
// those purposes - `animation_graph_find_node_by_model_flags(graph, mask)` at
// +0x79E8D0 scans nodes at stride 0x20 testing `byte[node+0x0A] & mask == mask`,
// and its caller at +0x81A8E0 passes 0x10 (right hand) and 0x08 (left hand) and
// culls a hands table to 0x20 (left arm member).
//
// So the left arm is identified from the engine's own data, with NO hardcoded
// node index. That matters: across nine shipped first-person rigs the wrists sat
// at indices 5 and 6 every time, but the Elite rig has 36 nodes instead of 42
// and puts the held weapon at index 31 instead of 37. Counts and weapon indices
// move; the flags do not. Bind on the flags.
inline constexpr uint32_t kHalo2AnimationGraphDefinitionGetRva = 0x0079EEA0;
inline constexpr uint32_t kHalo2AnimationGraphGetSkeletonNodeRva = 0x0079F430;
inline constexpr uint32_t kHalo2AnimationGraphGetNodeCountRva = 0x0079F470;
inline constexpr uint32_t kHalo2AnimationGraphFindNodeByFlagsRva = 0x0079E8D0;
// Cache format shifts the skeleton-node block 8 bytes down from the kit's
// +0x14, because a cache tag_reference is 8 bytes where an editing-format
// tag_block is 16. Proven on retail: get_node_count is literally
// `MOVZX EAX, word [RCX+0x0C]; RET`.
inline constexpr uint32_t kHalo2AnimationGraphNodeCountOffset = 0x0C;
inline constexpr uint32_t kHalo2AnimationNodeStride = 0x20;
inline constexpr uint32_t kHalo2AnimationNodeParentOffset = 0x08;
inline constexpr uint32_t kHalo2AnimationNodeModelFlagsOffset = 0x0A;
// +0x04 next sibling and +0x06 first child are NOT read by any located retail
// code, so they stay unverified and unused; the parent walk gives the same
// descendant sets.
inline constexpr uint8_t kHalo2ModelFlagPrimary = 0x01;
inline constexpr uint8_t kHalo2ModelFlagSecondary = 0x02;
inline constexpr uint8_t kHalo2ModelFlagLocalRoot = 0x04;
inline constexpr uint8_t kHalo2ModelFlagLeftHand = 0x08;
inline constexpr uint8_t kHalo2ModelFlagRightHand = 0x10;
inline constexpr uint8_t kHalo2ModelFlagLeftArmMember = 0x20;
// The engine's own first-person matrix palette is 64 entries
// (weapon_data +0x320, stride 0x34, struct size 0x1028). Refuse anything past
// it - and it is also what makes a 64-bit subtree mask exact rather than a
// bound chosen for convenience.
inline constexpr uint32_t kHalo2FirstPersonPaletteCapacity = 64;

// Which first-person nodes ride which controller. `leftSubtree` bit i means
// node i belongs to the left wrist and its descendants; every other node keeps
// the right-controller transform, which is where the gun already hangs.
enum class Halo2FirstPersonRigKind : uint8_t
{
    Unknown = 0,
    MasterChief,
    Elite,
};

struct Halo2FirstPersonArmBinding
{
    bool valid = false;
    int leftWrist = -1;
    int rightWrist = -1;
    int leftElbow = -1;
    int rightElbow = -1;
    int leftShoulder = -1;
    int rightShoulder = -1;
    uint32_t count = 0;
    uint64_t leftSubtree = 0;
    uint64_t rightSubtree = 0;
    uint64_t leftDirectChildren = 0;
    uint64_t leftArmAncestors = 0;
    uint64_t rightArmAncestors = 0;
    uint64_t armAncestors = 0;
    Halo2FirstPersonRigKind rigKind = Halo2FirstPersonRigKind::Unknown;
    // Digit chains are discovered from each exact graph's hand-child tree.
    // H2EK proves the shipped Chief/Elite digit counts and parent chains, but
    // not a stable semantic index-vs-grip mapping across both rigs.
    int16_t fingerNodes[2][5][3]{}; // anatomical left/right; generic authored chains
    uint64_t fingerMasks[2][5]{};
    uint8_t fingerCount[2]{};
    bool fingerPoseSupported=false;
    uint64_t fingerRigIdentity=0;
    int16_t parentIndices[kHalo2FirstPersonPaletteCapacity]{};
    finger_joint::Inventory fingerInventory[2]{};
};

// Pure, allocation-free, and deliberately unforgiving: anything that is not the
// exact shape every shipped Halo 2 first-person rig has - one left hand, one
// right hand, a well-formed parent tree, the right wrist outside the left
// subtree - returns false and leaves the caller on its single-carrier path.
inline bool Halo2BuildFirstPersonArmBinding(
    const uint8_t* modelFlags, const int16_t* parents, uint32_t count,
    Halo2FirstPersonArmBinding& out) noexcept
{
    out = Halo2FirstPersonArmBinding{};
    if (!modelFlags || !parents || count == 0 ||
        count > kHalo2FirstPersonPaletteCapacity)
    {
        return false;
    }
    int left = -1;
    int right = -1;
    for (uint32_t index = 0; index < count; ++index)
    {
        const int parent = parents[index];
        if (parent < -1 || parent >= static_cast<int>(count) ||
            parent == static_cast<int>(index))
        {
            return false;
        }
        if (modelFlags[index] & kHalo2ModelFlagLeftHand)
        {
            if (left >= 0)
                return false;
            left = static_cast<int>(index);
        }
        if (modelFlags[index] & kHalo2ModelFlagRightHand)
        {
            if (right >= 0)
                return false;
            right = static_cast<int>(index);
        }
    }
    if (left < 0 || right < 0 || left == right)
        return false;

    // Every parent chain must reach the root within `count` steps. Stopping
    // early at the left wrist would hide a cycle that runs through it, so the
    // well-formedness of the whole tree is established first, on its own.
    for (uint32_t index = 0; index < count; ++index)
    {
        int node = parents[index];
        uint32_t guard = 0;
        while (node >= 0)
        {
            node = parents[node];
            if (++guard > count)
                return false;
        }
    }

    uint64_t leftMask = 1ull << left;
    uint64_t rightMask = 1ull << right;
    for (uint32_t index = 0; index < count; ++index)
    {
        int node = parents[index];
        while (node >= 0)
        {
            if (node == left)
            {
                leftMask |= 1ull << index;
                break;
            }
            if (node == right)
            {
                rightMask |= 1ull << index;
                break;
            }
            node = parents[node];
        }
    }
    if ((leftMask & rightMask) != 0)
        return false;

    // The only geometry that spans the stock body anchor to the independently
    // tracked wrists is the ancestry between each wrist and the root. Exclude
    // both wrists and exclude the root itself: camera_control and unrelated
    // control nodes are neither transformed nor collapsed at this boundary.
    uint64_t leftArmMask = 0;
    uint64_t rightArmMask = 0;
    for (int wrist : {left, right})
    {
        uint64_t& wristArmMask = wrist == left ? leftArmMask : rightArmMask;
        int node = parents[wrist];
        while (node >= 0 && parents[node] >= 0)
        {
            wristArmMask |= 1ull << node;
            node = parents[node];
        }
    }
    const uint64_t armMask = leftArmMask | rightArmMask;
    if ((armMask & (leftMask | rightMask)) != 0)
        return false;

    out.valid = true;
    out.leftWrist = left;
    out.rightWrist = right;
    // H2EK Chief and Elite fp render-models both author the exact sequence
    // upperarm -> forearm -> hand. Derive the two solver joints from this
    // same live graph parent table; malformed/custom chains remain valid for
    // the established hand-only path but have no arm-IK eligibility.
    const int leftElbow = parents[left];
    const int rightElbow = parents[right];
    if (leftElbow >= 0 && rightElbow >= 0 &&
        parents[leftElbow] >= 0 && parents[rightElbow] >= 0 &&
        leftElbow != rightElbow && parents[leftElbow] != parents[rightElbow] &&
        parents[leftElbow] != left && parents[rightElbow] != right)
    {
        out.leftElbow = leftElbow;
        out.rightElbow = rightElbow;
        out.leftShoulder = parents[leftElbow];
        out.rightShoulder = parents[rightElbow];
    }
    out.count = count;
    out.leftSubtree = leftMask;
    out.rightSubtree = rightMask;
    for (uint32_t index = 0; index < count; ++index)
        if (parents[index] == left)
            out.leftDirectChildren |= 1ull << index;
    if (!out.leftDirectChildren)
        return false;
    uint32_t directChildCount = 0;
    for (uint64_t children = out.leftDirectChildren; children;
         children &= children - 1)
        ++directChildCount;
    // The official H2EK first-person graphs distinguish the shipped rigs by
    // their authored left-hand anatomy: Chief has five direct digit bases;
    // Elite has four. Unknown shapes remain bindable for the safety path but
    // are never admitted to the optional marker-specific transaction.
    if (directChildCount == 5)
        out.rigKind = Halo2FirstPersonRigKind::MasterChief;
    else if (directChildCount == 4)
        out.rigKind = Halo2FirstPersonRigKind::Elite;
    if(out.rigKind!=Halo2FirstPersonRigKind::Unknown)
    {
        const int wrists[2]{left,right};
        bool exact=true;
        for(unsigned side=0;side<2&&exact;++side)
        {
            int roots[5]{};unsigned rootCount=0;
            for(uint32_t node=0;node<count;++node)
                if(parents[node]==wrists[side]&&
                    !(modelFlags[node]&kHalo2ModelFlagSecondary))
                {
                    if(rootCount>=5){exact=false;break;}
                    roots[rootCount++]=static_cast<int>(node);
                }
            if(rootCount!=directChildCount){exact=false;break;}
            for(unsigned finger=0;finger<rootCount&&exact;++finger)
            {
                int middle=-1,tip=-1;unsigned middleCount=0,tipCount=0;
                for(uint32_t node=0;node<count;++node)
                    if(parents[node]==roots[finger]){middle=static_cast<int>(node);++middleCount;}
                if(middleCount!=1){exact=false;break;}
                for(uint32_t node=0;node<count;++node)
                    if(parents[node]==middle){tip=static_cast<int>(node);++tipCount;}
                if(tipCount!=1){exact=false;break;}
                for(uint32_t node=0;node<count;++node)
                    if(parents[node]==tip){exact=false;break;}
                if(!exact)break;
                out.fingerNodes[side][finger][0]=static_cast<int16_t>(roots[finger]);
                out.fingerNodes[side][finger][1]=static_cast<int16_t>(middle);
                out.fingerNodes[side][finger][2]=static_cast<int16_t>(tip);
                out.fingerMasks[side][finger]=(uint64_t{1}<<roots[finger])|
                    (uint64_t{1}<<middle)|(uint64_t{1}<<tip);
                out.fingerCount[side]=static_cast<uint8_t>(rootCount);
            }
        }
        out.fingerPoseSupported=exact&&out.fingerCount[0]==directChildCount&&
            out.fingerCount[1]==directChildCount;
        if(out.fingerPoseSupported)
        {
            uint64_t hash=14695981039346656037ull;
            auto append=[&](uint8_t value){hash=(hash^value)*1099511628211ull;};
            append(static_cast<uint8_t>(count));append(static_cast<uint8_t>(count>>8));
            for(uint32_t node=0;node<count;++node)
            {
                append(modelFlags[node]);const uint16_t parent=static_cast<uint16_t>(parents[node]);
                append(static_cast<uint8_t>(parent));append(static_cast<uint8_t>(parent>>8));
                out.parentIndices[node]=parents[node];
            }
            out.fingerRigIdentity=hash?hash:1;
            constexpr finger_joint::Digit unknownDigits[5]{finger_joint::Digit::Unknown,
                finger_joint::Digit::Unknown,finger_joint::Digit::Unknown,
                finger_joint::Digit::Unknown,finger_joint::Digit::Unknown};
            for(unsigned side=0;side<2;++side)
            {
                uint16_t chains[5][4]{};uint8_t lengths[5]{};
                for(unsigned digit=0;digit<out.fingerCount[side];++digit)
                {
                    lengths[digit]=3;
                    for(unsigned joint=0;joint<3;++joint)
                        chains[digit][joint]=static_cast<uint16_t>(out.fingerNodes[side][digit][joint]);
                }
                const finger_joint::RigKey key{GameTitle::Halo2,out.fingerRigIdentity,
                    static_cast<uint16_t>(count),finger_joint::Palette::FirstPerson};
                if(!finger_joint::Build(key,side,static_cast<unsigned>(side?right:left),
                    parents,chains,lengths,unknownDigits,out.fingerInventory[side]))
                    out.fingerPoseSupported=false;
            }
        }
    }
    out.leftArmAncestors = leftArmMask;
    out.rightArmAncestors = rightArmMask;
    out.armAncestors = armMask;
    return true;
}

inline bool Halo2DescribeFirstPersonFingerJoints(
    const Halo2FirstPersonArmBinding& binding,unsigned side,
    finger_joint::Inventory& output) noexcept
{
    output={};
    if(side>1||!binding.valid||!binding.fingerPoseSupported||
        !binding.fingerRigIdentity)return false;
    const auto& inventory=binding.fingerInventory[side];
    if(inventory.rig.title!=GameTitle::Halo2||
        inventory.rig.checksum!=binding.fingerRigIdentity||
        inventory.rig.nodeCount!=binding.count||
        inventory.rig.palette!=finger_joint::Palette::FirstPerson||!inventory.count)return false;
    output=inventory;return true;
}

inline bool Halo2MapFingerInventoryToPacket(
    const finger_joint::Inventory& source,const int32_t* destinationToSource,
    uint32_t packetCount,finger_joint::Inventory& output) noexcept
{
    output={};
    if(!destinationToSource||!packetCount||packetCount>64||!source.count||
        source.count>20||!source.rig.nodeCount||source.rig.nodeCount>64||
        source.rig.title!=GameTitle::Halo2||
        source.rig.palette!=finger_joint::Palette::FirstPerson)return false;
    int sourceToDestination[64]{};
    for(int& destination:sourceToDestination)destination=-1;
    for(uint32_t destination=0;destination<packetCount;++destination)
    {
        const int node=destinationToSource[destination];
        if(node< -1||node>=source.rig.nodeCount)return false;
        if(node>=0)
        {
            if(sourceToDestination[node]>=0)return false;
            sourceToDestination[node]=static_cast<int>(destination);
        }
    }
    finger_joint::Inventory candidate=source;
    candidate.rig.nodeCount=static_cast<uint16_t>(packetCount);
    for(unsigned i=0;i<candidate.count;++i)
    {
        auto& record=candidate.records[i];
        const unsigned sourceNode=record.paletteIndex,parent=record.parentIndex;
        if(sourceNode>=source.rig.nodeCount||parent>=source.rig.nodeCount||
            sourceToDestination[sourceNode]<0||sourceToDestination[parent]<0)return false;
        record.sourcePaletteIndex=static_cast<uint16_t>(sourceNode);
        record.paletteIndex=static_cast<uint16_t>(sourceToDestination[sourceNode]);
        record.parentIndex=static_cast<uint16_t>(sourceToDestination[parent]);
        record.rig=candidate.rig;
    }
    output=candidate;return true;
}

// Build the rigid world-space motion that places an already root-composed
// wrist matrix on a controller carrier. This deliberately consumes the final
// matrix, not the interpolator's local source. It is the same accepted shape as
// Halo 3/Reach/Halo 4: D = desired * inverse(stock wrist), then D is carried
// over the complete hand/held-object subtree.
inline bool Halo2BuildWorldDeltaRotation(
    const Halo2CameraBasis& tick, const Halo2CameraBasis& frame,
    float out[9]) noexcept;

inline bool Halo2BuildFinalPaletteWristDelta(
    const float* stockWrist, const Halo2CameraBasis& desired,
    float rotation[9], float stockPosition[3], float desiredPosition[3]) noexcept
{
    if (!stockWrist || !rotation || !stockPosition || !desiredPosition ||
        !Halo2ValidateCameraBasis(desired) || !std::isfinite(stockWrist[0]) ||
        std::fabs(stockWrist[0]) < 1.0e-6f)
    {
        return false;
    }
    Halo2CameraBasis stock{};
    stock.forward[0] = stockWrist[4];
    stock.forward[1] = stockWrist[5];
    stock.forward[2] = stockWrist[6];
    stock.up[0] = stockWrist[7];
    stock.up[1] = stockWrist[8];
    stock.up[2] = stockWrist[9];
    for (int axis = 0; axis < 3; ++axis)
    {
        stock.position[axis] = stockWrist[10 + axis];
        stockPosition[axis] = stock.position[axis];
        desiredPosition[axis] = desired.position[axis];
        if (!std::isfinite(stock.position[axis]))
            return false;
    }
    if (!Halo2ValidateCameraBasis(stock) ||
        !Halo2BuildWorldDeltaRotation(stock, desired, rotation))
    {
        return false;
    }
    return true;
}

// C-H2-60 transform algebra for the final 0x34-byte packet matrices. Scale is
// part of the affine wrist delta, not a per-node afterthought: this keeps
// gun_scale / left_hand_scale centred on the owned wrist and scales every
// child translation with the mesh instead of leaving the weapon's nodes behind.
struct Halo2FirstPersonTransform
{
    float scale = 1.0f;
    float rotation[9]{1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,1.0f};
    float translation[3]{};
};

inline bool Halo2FirstPersonTransformValid(
    const Halo2FirstPersonTransform& transform) noexcept
{
    if (!std::isfinite(transform.scale) ||
        std::fabs(transform.scale) < 1.0e-6f)
        return false;
    for (float value : transform.translation)
        if (!std::isfinite(value)) return false;
    float determinant =
        transform.rotation[0] *
            (transform.rotation[4] * transform.rotation[8] -
             transform.rotation[7] * transform.rotation[5]) -
        transform.rotation[3] *
            (transform.rotation[1] * transform.rotation[8] -
             transform.rotation[7] * transform.rotation[2]) +
        transform.rotation[6] *
            (transform.rotation[1] * transform.rotation[5] -
             transform.rotation[4] * transform.rotation[2]);
    if (!std::isfinite(determinant) || determinant < 0.8f ||
        determinant > 1.2f)
        return false;
    for (int column = 0; column < 3; ++column)
    {
        float lengthSquared = 0.0f;
        for (int row = 0; row < 3; ++row)
        {
            const float value = transform.rotation[column * 3 + row];
            if (!std::isfinite(value)) return false;
            lengthSquared += value * value;
        }
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.8f ||
            lengthSquared > 1.2f)
            return false;
    }
    return true;
}

inline bool Halo2ReadFirstPersonTransform(
    const float* matrix, Halo2FirstPersonTransform& transform) noexcept
{
    if (!matrix) return false;
    Halo2FirstPersonTransform candidate{};
    candidate.scale = matrix[0];
    std::memcpy(candidate.rotation, matrix + 1, sizeof(candidate.rotation));
    std::memcpy(
        candidate.translation, matrix + 10, sizeof(candidate.translation));
    if (!Halo2FirstPersonTransformValid(candidate)) return false;
    transform = candidate;
    return true;
}

inline void Halo2WriteFirstPersonTransform(
    const Halo2FirstPersonTransform& transform, float* matrix) noexcept
{
    matrix[0] = transform.scale;
    std::memcpy(matrix + 1, transform.rotation, sizeof(transform.rotation));
    std::memcpy(matrix + 10, transform.translation,
                sizeof(transform.translation));
}

inline void Halo2MultiplyFirstPersonBases(
    const float left[9], const float right[9], float output[9]) noexcept
{
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
        {
            float value = 0.0f;
            for (int inner = 0; inner < 3; ++inner)
                value += left[inner * 3 + row] *
                    right[column * 3 + inner];
            output[column * 3 + row] = value;
        }
}

inline bool Halo2CameraToFirstPersonBasis(
    const Halo2CameraBasis& camera, float output[9]) noexcept
{
    if (!output || !Halo2ValidateCameraBasis(camera)) return false;
    const float right[3] = {
        camera.forward[1] * camera.up[2] -
            camera.forward[2] * camera.up[1],
        camera.forward[2] * camera.up[0] -
            camera.forward[0] * camera.up[2],
        camera.forward[0] * camera.up[1] -
            camera.forward[1] * camera.up[0]};
    std::memcpy(output, right, sizeof(right));
    std::memcpy(output + 3, camera.forward, sizeof(camera.forward));
    std::memcpy(output + 6, camera.up, sizeof(camera.up));
    return true;
}

inline bool Halo2ComposeFirstPersonTransforms(
    const Halo2FirstPersonTransform& left,
    const Halo2FirstPersonTransform& right,
    Halo2FirstPersonTransform& output) noexcept
{
    if (!Halo2FirstPersonTransformValid(left) ||
        !Halo2FirstPersonTransformValid(right))
        return false;
    Halo2FirstPersonTransform result{};
    result.scale = left.scale * right.scale;
    Halo2MultiplyFirstPersonBases(
        left.rotation, right.rotation, result.rotation);
    for (int row = 0; row < 3; ++row)
    {
        float rotated = 0.0f;
        for (int column = 0; column < 3; ++column)
            rotated += left.rotation[column * 3 + row] *
                right.translation[column];
        result.translation[row] =
            left.translation[row] + left.scale * rotated;
    }
    if (!Halo2FirstPersonTransformValid(result)) return false;
    output = result;
    return true;
}

inline bool Halo2BuildFirstPersonWorldDelta(
    const Halo2FirstPersonTransform& desired,
    const Halo2FirstPersonTransform& stock,
    Halo2FirstPersonTransform& delta) noexcept
{
    if (!Halo2FirstPersonTransformValid(desired) ||
        !Halo2FirstPersonTransformValid(stock))
        return false;
    Halo2FirstPersonTransform result{};
    result.scale = desired.scale / stock.scale;
    float inverseStockRotation[9]{};
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            inverseStockRotation[column * 3 + row] =
                stock.rotation[row * 3 + column];
    Halo2MultiplyFirstPersonBases(
        desired.rotation, inverseStockRotation, result.rotation);
    for (int row = 0; row < 3; ++row)
    {
        float rotated = 0.0f;
        for (int column = 0; column < 3; ++column)
            rotated += result.rotation[column * 3 + row] *
                stock.translation[column];
        result.translation[row] =
            desired.translation[row] - result.scale * rotated;
    }
    if (!Halo2FirstPersonTransformValid(result)) return false;
    delta = result;
    return true;
}

// Halo 4's working mount rule, expressed in Halo 2's proven final-packet
// layout: retain only the live authored root-to-wrist ROTATION and use the
// physical controller target for translation. Composing the full authored
// relation here is the rejected face-gun regression.
inline bool Halo2BuildControllerRerootedWristTarget(
    const Halo2CameraBasis& controllerTarget,
    const Halo2CameraBasis& authoredRoot,
    const Halo2FirstPersonTransform& stockWrist, float meshScale,
    Halo2FirstPersonTransform& desiredWrist) noexcept
{
    if (!std::isfinite(meshScale) || meshScale <= 0.0f ||
        !Halo2FirstPersonTransformValid(stockWrist))
        return false;
    float controllerRotation[9]{}, rootRotation[9]{}, inverseRoot[9]{};
    if (!Halo2CameraToFirstPersonBasis(
            controllerTarget, controllerRotation) ||
        !Halo2CameraToFirstPersonBasis(authoredRoot, rootRotation))
        return false;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            inverseRoot[column * 3 + row] =
                rootRotation[row * 3 + column];
    float rootLocalWrist[9]{};
    Halo2MultiplyFirstPersonBases(
        inverseRoot, stockWrist.rotation, rootLocalWrist);
    Halo2FirstPersonTransform result = stockWrist;
    result.scale = stockWrist.scale * meshScale;
    Halo2MultiplyFirstPersonBases(
        controllerRotation, rootLocalWrist, result.rotation);
    std::memcpy(result.translation, controllerTarget.position,
                sizeof(result.translation));
    if (!Halo2FirstPersonTransformValid(result)) return false;
    desiredWrist = result;
    return true;
}

// In two-hand mode the support controller owns the shared right-aim line, but
// the visible left hand keeps Halo 2's authored grip. Carry the right wrist's
// exact RIGID motion to the stock left wrist, then apply left_hand_scale about
// that owned wrist independently.
inline bool Halo2BuildRigidSupportWristTarget(
    const Halo2FirstPersonTransform& desiredRight,
    const Halo2FirstPersonTransform& stockRight,
    const Halo2FirstPersonTransform& stockLeft, float leftScale,
    Halo2FirstPersonTransform& desiredLeft) noexcept
{
    if (!std::isfinite(leftScale) || leftScale <= 0.0f) return false;
    Halo2FirstPersonTransform rigidRight = desiredRight;
    rigidRight.scale = stockRight.scale;
    Halo2FirstPersonTransform rigidDelta{}, result{};
    if (!Halo2BuildFirstPersonWorldDelta(
            rigidRight, stockRight, rigidDelta) ||
        !Halo2ComposeFirstPersonTransforms(rigidDelta, stockLeft, result))
        return false;
    result.scale = stockLeft.scale * leftScale;
    if (!Halo2FirstPersonTransformValid(result)) return false;
    desiredLeft = result;
    return true;
}

// Minimal rotation taking unit vector `from` onto unit vector `to`, stored
// column-major like every other basis here. The same construction Halo 3,
// ODST and Reach use for their automatic barrel alignment.
inline void Halo2ShortestArcRotation(
    const float from[3], const float to[3], float out[9]) noexcept
{
    const float c = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
    const float v[3] = {
        from[1] * to[2] - from[2] * to[1],
        from[2] * to[0] - from[0] * to[2],
        from[0] * to[1] - from[1] * to[0]};
    for (int i = 0; i < 9; ++i) out[i] = 0.0f;
    if (c > 0.99999f)
    {
        out[0] = out[4] = out[8] = 1.0f;
        return;
    }
    if (c < -0.99999f)
    {
        // 180 degrees: rotate about any axis perpendicular to `from`.
        float ax[3] = {1.0f, 0.0f, 0.0f};
        if (std::fabs(from[0]) > 0.9f) { ax[0] = 0.0f; ax[1] = 1.0f; }
        float p[3] = {
            from[1] * ax[2] - from[2] * ax[1],
            from[2] * ax[0] - from[0] * ax[2],
            from[0] * ax[1] - from[1] * ax[0]};
        const float pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (pl < 1e-5f) { out[0] = out[4] = out[8] = 1.0f; return; }
        for (float& e : p) e /= pl;
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row)
                out[col * 3 + row] = 2.0f * p[row] * p[col] - (row == col ? 1.0f : 0.0f);
        return;
    }
    // Rodrigues: R = I + [v]x + [v]x^2 / (1 + c)
    const float k = 1.0f / (1.0f + c);
    const float vx[9] = {  // column-major [v]x
        0.0f,  v[2], -v[1],
       -v[2],  0.0f,  v[0],
        v[1], -v[0],  0.0f};
    float vx2[9];
    Halo2MultiplyFirstPersonBases(vx, vx, vx2);
    for (int i = 0; i < 9; ++i)
        out[i] = vx[i] + vx2[i] * k;
    out[0] += 1.0f; out[4] += 1.0f; out[8] += 1.0f;
}

// Experimental final-packet arm solve. The wrist target and all three source
// joint matrices are already root-composed packet transforms. It edits only
// shoulder/elbow; callers stage the complete packet and publish only when both
// arms succeed, leaving the existing rigid hands+gun transaction as fallback.
inline bool Halo2SolveFinalPacketArm(
    Halo2FirstPersonTransform& shoulder,
    Halo2FirstPersonTransform& elbow,
    const Halo2FirstPersonTransform& stockWrist,
    const Halo2FirstPersonTransform& desiredWrist,
    float maximumStretch = 1.8f) noexcept
{
    if (!Halo2FirstPersonTransformValid(shoulder) ||
        !Halo2FirstPersonTransformValid(elbow) ||
        !Halo2FirstPersonTransformValid(stockWrist) ||
        !Halo2FirstPersonTransformValid(desiredWrist) ||
        !std::isfinite(maximumStretch) || maximumStretch < 1.0f ||
        maximumStretch > 2.0f)
        return false;
    const float* s=shoulder.translation;
    const float* e=elbow.translation;
    const float* w=stockWrist.translation;
    const float* t=desiredWrist.translation;
    auto length=[](const float v[3]) noexcept {
        return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    };
    auto distance=[&](const float a[3],const float b[3]) noexcept {
        const float v[3]{b[0]-a[0],b[1]-a[1],b[2]-a[2]};
        return length(v);
    };
    const float upper=distance(s,e),lower=distance(e,w);
    float axis[3]{t[0]-s[0],t[1]-s[1],t[2]-s[2]};
    const float targetDistance=length(axis),total=upper+lower;
    if (!std::isfinite(upper)||!std::isfinite(lower)||upper<1.0e-4f||
        lower<1.0e-4f||targetDistance<1.0e-4f||!std::isfinite(total)||
        targetDistance>total*maximumStretch)
        return false;
    const float stretch=targetDistance>total ? targetDistance/total : 1.0f;
    const float solvedUpper=upper*stretch,solvedLower=lower*stretch;
    for(float& value:axis)value/=targetDistance;
    const float along=(solvedUpper*solvedUpper-solvedLower*solvedLower+
        targetDistance*targetDistance)/(2.0f*targetDistance);
    float heightSquared=solvedUpper*solvedUpper-along*along;
    if (!std::isfinite(heightSquared)||heightSquared< -1.0e-5f)return false;
    const float height=std::sqrt(std::max(0.0f,heightSquared));
    float pole[3]{e[0]-(s[0]+t[0])*0.5f,
                  e[1]-(s[1]+t[1])*0.5f,
                  e[2]-(s[2]+t[2])*0.5f};
    const float projection=pole[0]*axis[0]+pole[1]*axis[1]+pole[2]*axis[2];
    for(int i=0;i<3;++i)pole[i]-=projection*axis[i];
    float poleLength=length(pole);
    if(poleLength<1.0e-5f)
    {
        const float fallback[3]{0.0f,0.0f,1.0f};
        const float fallbackProjection=fallback[0]*axis[0]+
            fallback[1]*axis[1]+fallback[2]*axis[2];
        for(int i=0;i<3;++i)pole[i]=fallback[i]-fallbackProjection*axis[i];
        poleLength=length(pole);
    }
    if(!std::isfinite(poleLength)||poleLength<1.0e-5f)return false;
    for(float& value:pole)value/=poleLength;
    Halo2FirstPersonTransform solvedShoulder=shoulder,solvedElbow=elbow;
    for(int i=0;i<3;++i)
        solvedElbow.translation[i]=s[i]+axis[i]*along+pole[i]*height;
    const float oldUpper[3]{e[0]-s[0],e[1]-s[1],e[2]-s[2]};
    const float newUpper[3]{solvedElbow.translation[0]-s[0],
        solvedElbow.translation[1]-s[1],solvedElbow.translation[2]-s[2]};
    const float oldLower[3]{w[0]-e[0],w[1]-e[1],w[2]-e[2]};
    const float newLower[3]{t[0]-solvedElbow.translation[0],
        t[1]-solvedElbow.translation[1],t[2]-solvedElbow.translation[2]};
    auto normalize=[](float v[3]) noexcept {
        const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(!std::isfinite(n)||n<1.0e-5f)return false;
        for(int i=0;i<3;++i)v[i]/=n;
        return true;
    };
    float oldUpperUnit[3]{oldUpper[0],oldUpper[1],oldUpper[2]};
    float oldLowerUnit[3]{oldLower[0],oldLower[1],oldLower[2]};
    float newUpperUnit[3]{newUpper[0],newUpper[1],newUpper[2]};
    float newLowerUnit[3]{newLower[0],newLower[1],newLower[2]};
    if(!normalize(oldUpperUnit)||!normalize(oldLowerUnit)||
       !normalize(newUpperUnit)||!normalize(newLowerUnit))return false;
    float shoulderTurn[9]{},elbowTurn[9]{},newShoulderRotation[9]{},
          newElbowRotation[9]{};
    Halo2ShortestArcRotation(oldUpperUnit,newUpperUnit,shoulderTurn);
    Halo2ShortestArcRotation(oldLowerUnit,newLowerUnit,elbowTurn);
    for(int column=0;column<3;++column)
        for(int row=0;row<3;++row)
            for(int k=0;k<3;++k)
            {
                newShoulderRotation[column*3+row]+=
                    shoulderTurn[k*3+row]*shoulder.rotation[column*3+k];
                newElbowRotation[column*3+row]+=
                    elbowTurn[k*3+row]*elbow.rotation[column*3+k];
            }
    std::memcpy(solvedShoulder.rotation,newShoulderRotation,
                sizeof(newShoulderRotation));
    std::memcpy(solvedElbow.rotation,newElbowRotation,
                sizeof(newElbowRotation));
    if(!Halo2FirstPersonTransformValid(solvedShoulder)||
       !Halo2FirstPersonTransformValid(solvedElbow))return false;
    shoulder=solvedShoulder;
    elbow=solvedElbow;
    return true;
}

// Halo 2's controller mount. First the held gun's authored root frame is
// mapped onto the controller aim frame and the whole assembly pivots at the
// physical right hand. Then - exactly as Halo 3, ODST and Reach do - the
// STOCK barrel direction is swung onto the aim ray: in the stock game the
// first-person weapon points down the camera it was composed against (that
// is where its shots go), so `composeCamera.forward` IS the authored barrel
// line in packet space. The minimal rotation that carries it onto
// `rightCarrier.forward` - the same ray the crosshair and the shot use -
// puts the drawn barrel on the crosshair by construction. Classic and
// Anniversary each supply their own compose camera, so each renderer's art
// is aligned on its own terms. The user's weapon pitch/yaw/roll remain
// mount calibration that moves gun and crosshair together, as everywhere.
inline bool Halo2BuildAuthoredBarrelDelta(
    const Halo2FirstPersonTransform& stockRight,
    const Halo2FirstPersonTransform& stockGunRoot,
    const Halo2CameraBasis& composeCamera,
    const Halo2CameraBasis& rightCarrier, float rightScale,
    Halo2FirstPersonTransform& delta,
    Halo2FirstPersonTransform& desiredRight) noexcept
{
    if (!Halo2FirstPersonTransformValid(stockRight) ||
        !Halo2FirstPersonTransformValid(stockGunRoot) ||
        !Halo2ValidateCameraBasis(rightCarrier) ||
        !std::isfinite(rightScale) || rightScale <= 0.0f)
        return false;
    float right[3] = {
        rightCarrier.forward[1] * rightCarrier.up[2] -
            rightCarrier.forward[2] * rightCarrier.up[1],
        rightCarrier.forward[2] * rightCarrier.up[0] -
            rightCarrier.forward[0] * rightCarrier.up[2],
        rightCarrier.forward[0] * rightCarrier.up[1] -
            rightCarrier.forward[1] * rightCarrier.up[0]};
    float targetGun[9] = {
        rightCarrier.forward[0], rightCarrier.forward[1], rightCarrier.forward[2],
        -right[0], -right[1], -right[2],
        rightCarrier.up[0], rightCarrier.up[1], rightCarrier.up[2]};
    float inverseStockGun[9]{};
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            inverseStockGun[column * 3 + row] =
                stockGunRoot.rotation[row * 3 + column];
    Halo2FirstPersonTransform result{};
    result.scale = rightScale;
    float rootMapped[9]{};
    Halo2MultiplyFirstPersonBases(targetGun, inverseStockGun, rootMapped);
    std::memcpy(result.rotation, rootMapped, sizeof(rootMapped));
    // C-H2-86's automatic swing (stock barrel = compose camera forward) is
    // DISABLED: the headset showed it tilting Anniversary, which had been
    // correct, so its premise is false for the packet as built. The barrel
    // meter (E-H2-74) now measures the real relationship instead of
    // assuming it. Disabled, not deleted.
    constexpr bool kHalo2AutoBarrelSwing = false;
    if (kHalo2AutoBarrelSwing && Halo2ValidateCameraBasis(composeCamera))
    {
        float stockBarrel[3] = {
            composeCamera.forward[0], composeCamera.forward[1],
            composeCamera.forward[2]};
        float ray[3] = {
            rightCarrier.forward[0], rightCarrier.forward[1],
            rightCarrier.forward[2]};
        const float bl = std::sqrt(stockBarrel[0] * stockBarrel[0] +
            stockBarrel[1] * stockBarrel[1] + stockBarrel[2] * stockBarrel[2]);
        const float rl = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] +
            ray[2] * ray[2]);
        if (std::isfinite(bl) && bl > 1e-4f && std::isfinite(rl) && rl > 1e-4f)
        {
            for (float& e : stockBarrel) e /= bl;
            for (float& e : ray) e /= rl;
            float mountedBarrel[3] = {0.0f, 0.0f, 0.0f};
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < 3; ++row)
                    mountedBarrel[row] += rootMapped[column * 3 + row] *
                        stockBarrel[column];
            const float ml = std::sqrt(mountedBarrel[0] * mountedBarrel[0] +
                mountedBarrel[1] * mountedBarrel[1] +
                mountedBarrel[2] * mountedBarrel[2]);
            if (std::isfinite(ml) && ml > 1e-4f)
            {
                for (float& e : mountedBarrel) e /= ml;
                float swing[9], aligned[9];
                Halo2ShortestArcRotation(mountedBarrel, ray, swing);
                Halo2MultiplyFirstPersonBases(swing, rootMapped, aligned);
                bool finite = true;
                for (int i = 0; i < 9; ++i)
                    if (!std::isfinite(aligned[i])) { finite = false; break; }
                if (finite)
                    std::memcpy(result.rotation, aligned, sizeof(aligned));
            }
        }
    }
    for (int row = 0; row < 3; ++row)
    {
        float rotated = 0.0f;
        for (int column = 0; column < 3; ++column)
            rotated += result.rotation[column * 3 + row] *
                stockRight.translation[column];
        result.translation[row] = rightCarrier.position[row] -
            result.scale * rotated;
    }
    Halo2FirstPersonTransform movedRight{};
    if (!Halo2FirstPersonTransformValid(result) ||
        !Halo2ComposeFirstPersonTransforms(result, stockRight, movedRight))
        return false;
    delta = result;
    desiredRight = movedRight;
    return true;
}

// The official Master Chief and Elite first-person rigs both author the thumb
// base as the closest direct child of l_hand.  Turn the free palm by pi about
// that live anatomical ray, matching the accepted other-title rule without
// guessing a controller/world axis.  Translation and scale remain untouched.
inline bool Halo2BuildAnatomicalFreeLeftTarget(
    const Halo2FirstPersonTransform& stockWrist,
    const Halo2FirstPersonTransform& stockThumbBase,
    const Halo2FirstPersonTransform& controllerTarget,
    Halo2FirstPersonTransform& desiredWrist) noexcept
{
    if (!Halo2FirstPersonTransformValid(stockWrist) ||
        !Halo2FirstPersonTransformValid(stockThumbBase) ||
        !Halo2FirstPersonTransformValid(controllerTarget))
        return false;
    float thumbWorld[3]{};
    float lengthSquared = 0.0f;
    for (int row = 0; row < 3; ++row)
    {
        thumbWorld[row] = stockThumbBase.translation[row] -
            stockWrist.translation[row];
        lengthSquared += thumbWorld[row] * thumbWorld[row];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f)
        return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    for (float& value : thumbWorld) value *= inverseLength;
    float thumbLocal[3]{};
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            thumbLocal[column] += stockWrist.rotation[column * 3 + row] *
                thumbWorld[row];
    float palmFlip[9]{};
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            palmFlip[column * 3 + row] =
                2.0f * thumbLocal[row] * thumbLocal[column] -
                (row == column ? 1.0f : 0.0f);
    Halo2FirstPersonTransform result = controllerTarget;
    Halo2MultiplyFirstPersonBases(
        controllerTarget.rotation, palmFlip, result.rotation);
    if (!Halo2FirstPersonTransformValid(result)) return false;
    desiredWrist = result;
    return true;
}

inline bool Halo2QuaternionToFirstPersonBasis(
    const float input[4], float output[9]) noexcept
{
    float quaternion[4]{};
    if (!input || !output ||
        !Halo2NormalizeQuaternion(input, quaternion))
        return false;
    const float x = quaternion[0], y = quaternion[1];
    const float z = quaternion[2], w = quaternion[3];
    const float rows[9] = {
        1.0f - 2.0f * (y * y + z * z),
        2.0f * (x * y - z * w), 2.0f * (x * z + y * w),
        2.0f * (x * y + z * w),
        1.0f - 2.0f * (x * x + z * z),
        2.0f * (y * z - x * w), 2.0f * (x * z - y * w),
        2.0f * (y * z + x * w),
        1.0f - 2.0f * (x * x + y * y)};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            output[column * 3 + row] = rows[row * 3 + column];
    return true;
}

// H2EK-authored semantic mount solve:
//
//     desiredWrist * markerLocal = controllerMount
//     desiredWrist = controllerMount * inverse(markerLocal)
//
// Chief's `left_hand` marker and Arbiter's `left_hand_elite` marker have
// different rotations and offsets. Solving the complete marker transform seats
// the visible palm on the physical controller without copying a wrist axis or
// a constant flip from another title.
inline bool Halo2BuildRigMarkerFreeLeftTarget(
    Halo2FirstPersonRigKind rigKind,
    const Halo2CameraBasis& leftCarrier,
    const Halo2FirstPersonTransform& stockWrist, float leftScale,
    Halo2FirstPersonTransform& desiredWrist) noexcept
{
    if (!Halo2ValidateCameraBasis(leftCarrier) ||
        !Halo2FirstPersonTransformValid(stockWrist) ||
        !std::isfinite(leftScale) || leftScale <= 0.0f)
        return false;
    const float* markerTranslation = nullptr;
    const float* markerQuaternion = nullptr;
    static constexpr float kChiefTranslation[3] = {
        0.022750f, -0.008561f, 0.000398f};
    static constexpr float kChiefQuaternion[4] = {
        0.016078f, 0.073613f, 0.085080f, -0.993521f};
    static constexpr float kEliteTranslation[3] = {
        0.033088f, -0.009315f, 0.000442f};
    static constexpr float kEliteQuaternion[4] = {
        -0.001854f, 0.001501f, -0.001833f, -0.999995f};
    if (rigKind == Halo2FirstPersonRigKind::MasterChief)
    {
        markerTranslation = kChiefTranslation;
        markerQuaternion = kChiefQuaternion;
    }
    else if (rigKind == Halo2FirstPersonRigKind::Elite)
    {
        markerTranslation = kEliteTranslation;
        markerQuaternion = kEliteQuaternion;
    }
    else
    {
        return false;
    }
    float carrierRotation[9]{}, markerRotation[9]{}, inverseMarker[9]{};
    if (!Halo2CameraToFirstPersonBasis(leftCarrier, carrierRotation) ||
        !Halo2QuaternionToFirstPersonBasis(
            markerQuaternion, markerRotation))
        return false;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            inverseMarker[column * 3 + row] =
                markerRotation[row * 3 + column];
    Halo2FirstPersonTransform result{};
    result.scale = stockWrist.scale * leftScale;
    Halo2MultiplyFirstPersonBases(
        carrierRotation, inverseMarker, result.rotation);
    for (int row = 0; row < 3; ++row)
    {
        float markerOffset = 0.0f;
        for (int column = 0; column < 3; ++column)
            markerOffset += result.rotation[column * 3 + row] *
                markerTranslation[column];
        result.translation[row] = leftCarrier.position[row] -
            result.scale * markerOffset;
    }
    if (!Halo2FirstPersonTransformValid(result)) return false;
    desiredWrist = result;
    return true;
}

inline bool Halo2BuildControllerHandMountBasis(
    const Halo2CameraBasis& carrier, float output[9]) noexcept
{
    if (!output || !Halo2ValidateCameraBasis(carrier)) return false;
    const float right[3] = {
        carrier.forward[1] * carrier.up[2] -
            carrier.forward[2] * carrier.up[1],
        carrier.forward[2] * carrier.up[0] -
            carrier.forward[0] * carrier.up[2],
        carrier.forward[0] * carrier.up[1] -
            carrier.forward[1] * carrier.up[0]};
    // Halo 3/ODST/Reach's accepted semantic hand frame: forward, left, up.
    std::memcpy(output, carrier.forward, sizeof(carrier.forward));
    for (int axis = 0; axis < 3; ++axis)
        output[3 + axis] = -right[axis];
    std::memcpy(output + 6, carrier.up, sizeof(carrier.up));
    return true;
}

inline bool Halo2BuildCorrectedRigMarkerFreeLeftTarget(
    Halo2FirstPersonRigKind rigKind,
    const Halo2CameraBasis& leftCarrier,
    const Halo2FirstPersonTransform& stockWrist, float leftScale,
    Halo2FirstPersonTransform& desiredWrist) noexcept
{
    if (!Halo2ValidateCameraBasis(leftCarrier) ||
        !Halo2FirstPersonTransformValid(stockWrist) ||
        !std::isfinite(leftScale) || leftScale <= 0.0f)
        return false;
    const float* markerTranslation = nullptr;
    const float* markerQuaternion = nullptr;
    static constexpr float kChiefTranslation[3] = {
        0.022750f, -0.008561f, 0.000398f};
    static constexpr float kChiefQuaternion[4] = {
        0.016078f, 0.073613f, 0.085080f, -0.993521f};
    static constexpr float kEliteTranslation[3] = {
        0.033088f, -0.009315f, 0.000442f};
    static constexpr float kEliteQuaternion[4] = {
        -0.001854f, 0.001501f, -0.001833f, -0.999995f};
    if (rigKind == Halo2FirstPersonRigKind::MasterChief)
    {
        markerTranslation = kChiefTranslation;
        markerQuaternion = kChiefQuaternion;
    }
    else if (rigKind == Halo2FirstPersonRigKind::Elite)
    {
        markerTranslation = kEliteTranslation;
        markerQuaternion = kEliteQuaternion;
    }
    else
    {
        return false;
    }
    float mountRotation[9]{}, markerRotation[9]{}, inverseMarker[9]{};
    if (!Halo2BuildControllerHandMountBasis(leftCarrier, mountRotation) ||
        !Halo2QuaternionToFirstPersonBasis(markerQuaternion, markerRotation))
        return false;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            inverseMarker[column * 3 + row] =
                markerRotation[row * 3 + column];
    Halo2FirstPersonTransform result{};
    result.scale = stockWrist.scale * leftScale;
    Halo2MultiplyFirstPersonBases(
        mountRotation, inverseMarker, result.rotation);
    for (int row = 0; row < 3; ++row)
    {
        float markerOffset = 0.0f;
        for (int column = 0; column < 3; ++column)
            markerOffset += result.rotation[column * 3 + row] *
                markerTranslation[column];
        result.translation[row] = leftCarrier.position[row] -
            result.scale * markerOffset;
    }
    if (!Halo2FirstPersonTransformValid(result)) return false;
    desiredWrist = result;
    return true;
}

// C-H2-64 keeps Halo 2's two presentation states separate, matching the
// headset-confirmed policy in the other titles instead of treating an idle
// left hand as though it were already gripping a weapon.
//
// In two-hand mode the authored support-hand rotation remains rigidly locked
// to the gun, but its wrist is seated at the live support carrier.  The shared
// aim solve already points the barrel through that carrier, so this removes the
// remaining stock first-person lateral gap without inventing a weapon-specific
// marker or moving the right hand/gun.
//
// In free mode the independently tracked hand keeps its controller-owned
// translation and scale, then turns over by pi around the live controller
// forward axis.  That preserves pointing direction and handedness while
// correcting Halo 2's upside-down idle palm.  The operation is a proper
// determinant-+1 rotation and is never selected during the support grip.
inline bool Halo2BuildLeftPresentationWristTarget(
    bool twoHandAimActive,
    const Halo2FirstPersonTransform& desiredRight,
    const Halo2FirstPersonTransform& stockRight,
    const Halo2FirstPersonTransform& stockLeft,
    const Halo2CameraBasis& authoredRoot,
    const Halo2CameraBasis& leftCarrier, float leftScale,
    Halo2FirstPersonTransform& desiredLeft) noexcept
{
    if (!kHalo2C64GenericLeftPresentationEnabled)
    {
        return twoHandAimActive
            ? Halo2BuildRigidSupportWristTarget(
                desiredRight, stockRight, stockLeft, leftScale, desiredLeft)
            : Halo2BuildControllerRerootedWristTarget(
                leftCarrier, authoredRoot, stockLeft, leftScale, desiredLeft);
    }
    Halo2FirstPersonTransform result{};
    if (twoHandAimActive)
    {
        if (!Halo2BuildRigidSupportWristTarget(
                desiredRight, stockRight, stockLeft, leftScale, result))
            return false;
        std::memcpy(result.translation, leftCarrier.position,
                    sizeof(result.translation));
    }
    else
    {
        if (!Halo2BuildControllerRerootedWristTarget(
                leftCarrier, authoredRoot, stockLeft, leftScale, result))
            return false;
        const float axisLengthSquared =
            leftCarrier.forward[0] * leftCarrier.forward[0] +
            leftCarrier.forward[1] * leftCarrier.forward[1] +
            leftCarrier.forward[2] * leftCarrier.forward[2];
        if (!std::isfinite(axisLengthSquared) || axisLengthSquared < 1.0e-8f)
            return false;
        const float inverseLength = 1.0f / std::sqrt(axisLengthSquared);
        float axis[3] = {
            leftCarrier.forward[0] * inverseLength,
            leftCarrier.forward[1] * inverseLength,
            leftCarrier.forward[2] * inverseLength};
        float turnover[9]{};
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                turnover[column * 3 + row] =
                    2.0f * axis[column] * axis[row] -
                    (column == row ? 1.0f : 0.0f);
        float turned[9]{};
        Halo2MultiplyFirstPersonBases(turnover, result.rotation, turned);
        std::memcpy(result.rotation, turned, sizeof(result.rotation));
    }
    if (!Halo2FirstPersonTransformValid(result)) return false;
    desiredLeft = result;
    return true;
}

inline bool Halo2FirstPersonWristDeltaPlausible(
    const Halo2FirstPersonTransform& stock,
    const Halo2FirstPersonTransform& desired, float worldScale) noexcept
{
    if (!Halo2FirstPersonTransformValid(stock) ||
        !Halo2FirstPersonTransformValid(desired) ||
        !std::isfinite(worldScale) || worldScale <= 0.0f)
        return false;
    float distanceSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float delta = desired.translation[axis] - stock.translation[axis];
        distanceSquared += delta * delta;
    }
    const float maximum = std::max(2.0f, worldScale * 10.0f);
    return std::isfinite(distanceSquared) &&
        distanceSquared <= maximum * maximum;
}

// E-H2-37 (C-H2-43): H2EK weapons.cpp firing helper RVA 0x47DC20 copies the
// owning unit's aiming_vector into the shot direction. BSim maps its exact x64
// homolog to retail +0x8F0F70; the 22-byte entry below occurs once in the
// pinned module and the retail function has the same sole firing caller at
// +0x8E4FC8 and the same +0x174 copy. Its result is a shot-only ownership
// boundary: no XInput, observer, or camera field is touched.
inline constexpr uint32_t kHalo2WeaponAimHelperRva = 0x008F0F70;
inline constexpr uint8_t kHalo2WeaponAimHelperEntryBytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x55, 0x56, 0x41, 0x55,
    0x48, 0x8D, 0x68, 0xC1, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};

// The local-player guard for that shared firing helper is engine-native too.
// H2EK players.cpp maps output users through players_globals +0x0C, stores a
// player's unit handle at player datum +0x2C, and bounds players to 16. Its
// player-update homolog is the high-confidence BSim match at retail +0x6A3910
// (similarity 0.4873, significance 243.5); retail preserves the 0x224 player
// datum stride and resolves its storage from data-array +0x48. The output-user
// iterator at +0x6A1D50 independently verifies the retail globals pointer.
inline constexpr uint32_t kHalo2PlayerUserIteratorRva = 0x006A1D50;
inline constexpr uint32_t kHalo2PlayerUpdateRva = 0x006A3910;
inline constexpr uint32_t kHalo2PlayersGlobalsPointerRva = 0x00E80A20;
inline constexpr uint32_t kHalo2PlayersDataArrayPointerRva = 0x00E80A28;
inline constexpr uint32_t kHalo2PlayerUserMappingOffset = 0x0C;
inline constexpr uint32_t kHalo2DataArrayStorageOffset = 0x48;
inline constexpr uint32_t kHalo2PlayerDatumStride = 0x224;
inline constexpr uint32_t kHalo2PlayerUnitIndexOffset = 0x2C;
inline constexpr uint32_t kHalo2MaximumPlayers = 16;

// The world rotation that takes the tick camera's frame to the frame
// camera's: R = F_frame * F_tick^T with F = [right forward up] as world
// columns. Written as nine floats, three world COLUMN vectors in sequence
// (the image of world X, then Y, then Z) - the same layout the nodes hold.
inline bool Halo2BuildWorldDeltaRotation(
    const Halo2CameraBasis& tick, const Halo2CameraBasis& frame,
    float out[9]) noexcept
{
    if (!out || !Halo2ValidateCameraBasis(tick) ||
        !Halo2ValidateCameraBasis(frame))
    {
        return false;
    }
    float tickAxes[9];
    float frameAxes[9];
    const Halo2CameraBasis* const cameras[2] = {&tick, &frame};
    float* const axes[2] = {tickAxes, frameAxes};
    for (int index = 0; index < 2; ++index)
    {
        const Halo2CameraBasis& camera = *cameras[index];
        float right[3] = {
            camera.forward[1] * camera.up[2] - camera.forward[2] * camera.up[1],
            camera.forward[2] * camera.up[0] - camera.forward[0] * camera.up[2],
            camera.forward[0] * camera.up[1] - camera.forward[1] * camera.up[0]};
        for (int axis = 0; axis < 3; ++axis)
        {
            axes[index][axis * 3 + 0] = right[axis];
            axes[index][axis * 3 + 1] = camera.forward[axis];
            axes[index][axis * 3 + 2] = camera.up[axis];
        }
    }
    // out = frameAxes * tickAxes^T; rows of `axes` are world components, so
    // element (r,c) = sum_k frameAxes[r][k] * tickAxes[c][k].
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k)
                sum += frameAxes[row * 3 + k] * tickAxes[column * 3 + k];
            if (!std::isfinite(sum))
                return false;
            // stored column-major: out column c = image of world axis c.
            out[column * 3 + row] = sum;
        }
    }
    return true;
}

// Applies the rigid re-anchor to one interpolated first-person node:
// axes' = R * axes, position' = R * (position - tickCamera) + frameCamera.
inline void Halo2ReanchorFirstPersonNode(
    float* node, const float rotation[9], const float tickPosition[3],
    const float framePosition[3]) noexcept
{
    float* const axes = node + kHalo2FirstPersonNodeAxesOffset / 4;
    float* const position = node + kHalo2FirstPersonNodePositionOffset / 4;
    for (int vec = 0; vec < 4; ++vec)
    {
        float source[3];
        if (vec < 3)
        {
            source[0] = axes[vec * 3 + 0];
            source[1] = axes[vec * 3 + 1];
            source[2] = axes[vec * 3 + 2];
        }
        else
        {
            source[0] = position[0] - tickPosition[0];
            source[1] = position[1] - tickPosition[1];
            source[2] = position[2] - tickPosition[2];
        }
        float rotated[3];
        for (int row = 0; row < 3; ++row)
        {
            rotated[row] = rotation[0 * 3 + row] * source[0] +
                rotation[1 * 3 + row] * source[1] +
                rotation[2 * 3 + row] * source[2];
        }
        if (vec < 3)
        {
            axes[vec * 3 + 0] = rotated[0];
            axes[vec * 3 + 1] = rotated[1];
            axes[vec * 3 + 2] = rotated[2];
        }
        else
        {
            position[0] = rotated[0] + framePosition[0];
            position[1] = rotated[1] + framePosition[1];
            position[2] = rotated[2] + framePosition[2];
        }
    }
}

struct Halo2FinalPacketOwnershipResult
{
    bool applied = false;
    uint32_t rightNodes = 0;
    uint32_t leftNodes = 0;
    uint32_t collapsedNodes = 0;
    uint32_t coLocatedArmNodes = 0;
    uint32_t gunNodes = 0;
    bool armsSolved = false;
    float rightWristDeltaWorld = 0.0f;
    float leftWristDeltaWorld = 0.0f;
};

// C-H2-62 split transaction for the registered visible consumer.  Retail
// publishes the hands packet first and the held-model packet afterwards.  The
// hands call therefore owns the two wrist subtrees and publishes the exact
// right-wrist affine delta for the immediately following held-model call.
inline bool Halo2OwnVisibleFirstPersonHands(
    float* handsMatrices, uint32_t handsCount, const int32_t* handsRemap,
    const Halo2FirstPersonArmBinding& binding,
    const Halo2CameraBasis& authoredRoot,
    const Halo2CameraBasis& rightCarrier,
    const Halo2CameraBasis& leftCarrier, bool twoHandAimActive,
    float rightScale, float leftScale, float worldScale,
    Halo2FirstPersonTransform& rightDeltaOut,
    Halo2FinalPacketOwnershipResult& out) noexcept
{
    out = Halo2FinalPacketOwnershipResult{};
    rightDeltaOut = Halo2FirstPersonTransform{};
    if (!handsMatrices || !handsRemap || !binding.valid || binding.count == 0 ||
        binding.count > kHalo2FirstPersonPaletteCapacity || handsCount == 0 ||
        handsCount > kHalo2FirstPersonPaletteCapacity ||
        !Halo2ValidateCameraBasis(authoredRoot) ||
        !Halo2ValidateCameraBasis(rightCarrier) ||
        !Halo2ValidateCameraBasis(leftCarrier) || !std::isfinite(rightScale) ||
        !std::isfinite(leftScale) || !std::isfinite(worldScale) ||
        rightScale <= 0.0f || leftScale <= 0.0f || worldScale <= 0.0f)
        return false;

    int rightWristDestination = -1;
    int leftWristDestination = -1;
    for (uint32_t destination = 0; destination < handsCount; ++destination)
    {
        const int32_t source = handsRemap[destination];
        if (source < -1 || source >= static_cast<int32_t>(binding.count))
            return false;
        if (source == binding.rightWrist)
            rightWristDestination = static_cast<int>(destination);
        if (source == binding.leftWrist)
            leftWristDestination = static_cast<int>(destination);
    }
    if (rightWristDestination < 0 || leftWristDestination < 0)
        return false;

    float stagedHands[
        kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    std::memcpy(stagedHands, handsMatrices,
                static_cast<size_t>(handsCount) * kHalo2FirstPersonNodeStride);
    const float* const rightWristMatrix = stagedHands +
        static_cast<uint32_t>(rightWristDestination) *
            kHalo2FirstPersonNodeFloats;
    const float* const leftWristMatrix = stagedHands +
        static_cast<uint32_t>(leftWristDestination) *
            kHalo2FirstPersonNodeFloats;
    Halo2FirstPersonTransform stockRight{}, stockLeft{};
    Halo2FirstPersonTransform desiredRight{}, desiredLeft{};
    if (!Halo2ReadFirstPersonTransform(rightWristMatrix, stockRight) ||
        !Halo2ReadFirstPersonTransform(leftWristMatrix, stockLeft) ||
        !Halo2BuildControllerRerootedWristTarget(
            rightCarrier, authoredRoot, stockRight, rightScale, desiredRight) ||
        !Halo2BuildLeftPresentationWristTarget(
            twoHandAimActive, desiredRight, stockRight, stockLeft,
            authoredRoot, leftCarrier, leftScale, desiredLeft) ||
        !Halo2FirstPersonWristDeltaPlausible(
            stockRight, desiredRight, worldScale) ||
        !Halo2FirstPersonWristDeltaPlausible(
            stockLeft, desiredLeft, worldScale))
        return false;

    for (int axis = 0; axis < 3; ++axis)
    {
        const float right =
            desiredRight.translation[axis] - stockRight.translation[axis];
        const float left =
            desiredLeft.translation[axis] - stockLeft.translation[axis];
        out.rightWristDeltaWorld += right * right;
        out.leftWristDeltaWorld += left * left;
    }
    out.rightWristDeltaWorld = std::sqrt(out.rightWristDeltaWorld);
    out.leftWristDeltaWorld = std::sqrt(out.leftWristDeltaWorld);
    Halo2FirstPersonTransform leftDelta{};
    if (!std::isfinite(out.rightWristDeltaWorld) ||
        !std::isfinite(out.leftWristDeltaWorld) ||
        !Halo2BuildFirstPersonWorldDelta(
            desiredRight, stockRight, rightDeltaOut) ||
        !Halo2BuildFirstPersonWorldDelta(desiredLeft, stockLeft, leftDelta))
        return false;

    for (uint32_t destination = 0; destination < handsCount; ++destination)
    {
        float* const node =
            stagedHands + destination * kHalo2FirstPersonNodeFloats;
        const int32_t source = handsRemap[destination];
        const uint64_t bit = source >= 0 ? uint64_t{1} << source : 0;
        Halo2FirstPersonTransform stockNode{}, moved{};
        if (bit && (binding.rightSubtree & bit))
        {
            if (!Halo2ReadFirstPersonTransform(node, stockNode) ||
                !Halo2ComposeFirstPersonTransforms(
                    rightDeltaOut, stockNode, moved))
                return false;
            Halo2WriteFirstPersonTransform(moved, node);
            ++out.rightNodes;
        }
        else if (bit && (binding.leftSubtree & bit))
        {
            if (!Halo2ReadFirstPersonTransform(node, stockNode) ||
                !Halo2ComposeFirstPersonTransforms(leftDelta, stockNode, moved))
                return false;
            Halo2WriteFirstPersonTransform(moved, node);
            ++out.leftNodes;
        }
        else
        {
            node[0] *= 0.0001f;
            ++out.collapsedNodes;
        }
    }
    out.applied = out.rightNodes != 0 && out.leftNodes != 0;
    if (!out.applied)
        return false;
    std::memcpy(handsMatrices, stagedHands,
                static_cast<size_t>(handsCount) * kHalo2FirstPersonNodeStride);
    return true;
}

inline bool Halo2OwnVisibleFirstPersonGun(
    float* gunMatrices, uint32_t gunCount,
    const Halo2FirstPersonTransform& rightDelta,
    Halo2FinalPacketOwnershipResult& out) noexcept
{
    out = Halo2FinalPacketOwnershipResult{};
    if (!gunMatrices || gunCount == 0 ||
        gunCount > kHalo2FirstPersonPaletteCapacity ||
        !Halo2FirstPersonTransformValid(rightDelta))
        return false;
    float stagedGun[
        kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    std::memcpy(stagedGun, gunMatrices,
                static_cast<size_t>(gunCount) * kHalo2FirstPersonNodeStride);
    for (uint32_t nodeIndex = 0; nodeIndex < gunCount; ++nodeIndex)
    {
        float* const node =
            stagedGun + nodeIndex * kHalo2FirstPersonNodeFloats;
        Halo2FirstPersonTransform stockNode{}, moved{};
        if (!Halo2ReadFirstPersonTransform(node, stockNode) ||
            !Halo2ComposeFirstPersonTransforms(rightDelta, stockNode, moved))
            return false;
        Halo2WriteFirstPersonTransform(moved, node);
        ++out.gunNodes;
    }
    out.applied = out.gunNodes != 0;
    if (!out.applied)
        return false;
    std::memcpy(gunMatrices, stagedGun,
                static_cast<size_t>(gunCount) * kHalo2FirstPersonNodeStride);
    return true;
}

// H2EK's Chief model authors both semantic grip markers. Dervish authors only
// left_hand_elite; its own bilateral wrist/digit axes prove local Y as the
// anatomical reflection plane. The right Elite marker below is a deliberate
// VR-side reflection of that rig's left marker, not a claimed native marker.
// See LEFT-HAND-H2-2026-09-15.md. Marker scale is unity here, matching the
// accepted C-H2-70 semantic solve; authored marker metadata stores zero.
inline bool Halo2AnatomicalGripMarker(Halo2FirstPersonRigKind rigKind,
    bool leftHand, Halo2FirstPersonTransform& marker) noexcept
{
    const float* translation = nullptr;
    const float* quaternion = nullptr;
    constexpr float chiefLeftPosition[3]{0.022750f, -0.008561f, 0.000398f};
    constexpr float chiefLeftRotation[4]{0.016078f, 0.073613f, 0.085080f, -0.993521f};
    constexpr float chiefRightPosition[3]{0.022747f, 0.011516f, 0.000398f};
    constexpr float chiefRightRotation[4]{0.008948f, 0.074815f, -0.010537f, -0.997102f};
    constexpr float eliteLeftPosition[3]{0.033088f, -0.009315f, 0.000442f};
    constexpr float eliteLeftRotation[4]{-0.001854f, 0.001501f, -0.001833f, -0.999995f};
    if (rigKind == Halo2FirstPersonRigKind::MasterChief)
    {
        translation = leftHand ? chiefLeftPosition : chiefRightPosition;
        quaternion = leftHand ? chiefLeftRotation : chiefRightRotation;
    }
    else if (rigKind == Halo2FirstPersonRigKind::Elite)
    {
        translation = eliteLeftPosition;
        quaternion = eliteLeftRotation;
    }
    else return false;
    Halo2FirstPersonTransform result{};
    if (!Halo2QuaternionToFirstPersonBasis(quaternion, result.rotation)) return false;
    std::memcpy(result.translation, translation, sizeof(result.translation));
    if (rigKind == Halo2FirstPersonRigKind::Elite && !leftHand)
    {
        result.translation[1] = -result.translation[1];
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                if ((column == 1) != (row == 1)) result.rotation[column * 3 + row] *= -1.0f;
    }
    if (!Halo2FirstPersonTransformValid(result)) return false;
    marker = result;
    return true;
}

// E-H2-40/45 provide the anatomical subtrees and destination remaps. The
// completed wrist frames already contain the native weapon grip and every
// controller/mount adjustment. Exchange their semantic PALMS, not their wrist
// origins: desiredWrist * ownMarker = oppositeWrist * oppositeMarker.
// Gun packets never enter this optional anatomical transaction.
inline bool Halo2RouteLeftHandedPacketHands(
    float* hands, uint32_t count, const int32_t* primaryRemap,
    const Halo2FirstPersonArmBinding& primaryBinding,
    const int32_t* secondaryRemap,
    const Halo2FirstPersonArmBinding& secondaryBinding,
    const Halo2CameraBasis& primaryCarrier,
    const Halo2CameraBasis& supportCarrier) noexcept
{
    if (!hands || !primaryRemap || !secondaryRemap || !count || count > 64 ||
        !primaryBinding.valid || !secondaryBinding.valid ||
        !primaryBinding.count || !secondaryBinding.count ||
        primaryBinding.count > 64 || secondaryBinding.count > 64 ||
        primaryBinding.rigKind != secondaryBinding.rigKind ||
        !Halo2ValidateCameraBasis(primaryCarrier) ||
        !Halo2ValidateCameraBasis(supportCarrier)) return false;
    int wrist[2]{-1, -1};
    uint64_t mask[2]{};
    for (uint32_t i = 0; i < count; ++i)
    {
        const int r = primaryRemap[i], l = secondaryRemap[i];
        if (r < -1 || r >= int(primaryBinding.count) ||
            l < -1 || l >= int(secondaryBinding.count)) return false;
        const bool right = r >= 0 && (primaryBinding.rightSubtree & (uint64_t{1} << r));
        const bool left = l >= 0 && (secondaryBinding.leftSubtree & (uint64_t{1} << l));
        if (right && left) return false;
        if (right) mask[1] |= uint64_t{1} << i;
        if (left) mask[0] |= uint64_t{1} << i;
        if (r == primaryBinding.rightWrist)
        {
            if (wrist[1] >= 0) return false;
            wrist[1] = int(i);
        }
        if (l == secondaryBinding.leftWrist)
        {
            if (wrist[0] >= 0) return false;
            wrist[0] = int(i);
        }
    }
    if (wrist[0] < 0 || wrist[1] < 0 ||
        !(mask[0] & (uint64_t{1} << wrist[0])) ||
        !(mask[1] & (uint64_t{1} << wrist[1]))) return false;
    Halo2FirstPersonTransform marker[2]{}, stock[2]{}, desired[2]{}, delta[2]{};
    if (!Halo2AnatomicalGripMarker(primaryBinding.rigKind, true, marker[0]) ||
        !Halo2AnatomicalGripMarker(primaryBinding.rigKind, false, marker[1]))
        return false;
    for (int hand = 0; hand < 2; ++hand)
        if (!Halo2ReadFirstPersonTransform(
                hands + wrist[hand] * kHalo2FirstPersonNodeFloats, stock[hand]))
            return false;
    for (int hand = 0; hand < 2; ++hand)
    {
        Halo2FirstPersonTransform oppositePalm{};
        if (!Halo2ComposeFirstPersonTransforms(stock[1 - hand], marker[1 - hand], oppositePalm) ||
            !Halo2BuildFirstPersonWorldDelta(oppositePalm, marker[hand], desired[hand]))
            return false;
        if (!Halo2BuildFirstPersonWorldDelta(desired[hand], stock[hand], delta[hand]))
            return false;
    }
    float staged[kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    std::memcpy(staged, hands, count * kHalo2FirstPersonNodeStride);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t bit = uint64_t{1} << i;
        const int hand = (mask[0] & bit) ? 0 : (mask[1] & bit) ? 1 : -1;
        if (hand >= 0)
        {
            Halo2FirstPersonTransform node{}, moved{};
            if (!Halo2ReadFirstPersonTransform(hands + i * kHalo2FirstPersonNodeFloats, node) ||
                !Halo2ComposeFirstPersonTransforms(delta[hand], node, moved)) return false;
            Halo2WriteFirstPersonTransform(moved, staged + i * kHalo2FirstPersonNodeFloats);
        }
        else if (primaryBinding.rigKind == Halo2FirstPersonRigKind::Elite)
        {
            const int source = primaryRemap[i];
            const uint64_t sourceBit = source >= 0 ? uint64_t{1} << source : 0;
            if (sourceBit & primaryBinding.armAncestors)
            {
                auto hidden = desired[(sourceBit & primaryBinding.leftArmAncestors) ? 0 : 1];
                hidden.scale *= 0.0001f;
                Halo2WriteFirstPersonTransform(hidden, staged + i * kHalo2FirstPersonNodeFloats);
            }
        }
    }
    std::memcpy(hands, staged, count * kHalo2FirstPersonNodeStride);
    return true;
}

inline bool Halo2RotateFingerSubtree(float* hands,uint32_t handsCount,
    const int32_t* remap,uint32_t sourceCount,uint64_t sourceMask,int pivotSource,
    const float axis[3],float angle) noexcept
{
    if(!hands||!remap||!handsCount||handsCount>64||!sourceCount||sourceCount>64||
        pivotSource<0||pivotSource>=static_cast<int>(sourceCount)||!sourceMask||
        !axis||!std::isfinite(angle))return false;
    const float axisLength=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    if(!std::isfinite(axisLength)||axisLength<1.0e-5f)return false;
    const float n[3]{axis[0]/axisLength,axis[1]/axisLength,axis[2]/axisLength};
    Halo2FirstPersonTransform pivot{};
    int pivotDestination=-1;
    for(uint32_t d=0;d<handsCount;++d)if(remap[d]==pivotSource)
    { if(pivotDestination>=0)return false;pivotDestination=static_cast<int>(d); }
    if(pivotDestination<0||!Halo2ReadFirstPersonTransform(
        hands+static_cast<uint32_t>(pivotDestination)*kHalo2FirstPersonNodeFloats,pivot))return false;
    const float c=std::cos(angle),s=std::sin(angle),v=1-c;
    Halo2FirstPersonTransform delta{};
    delta.rotation[0]=c+n[0]*n[0]*v;
    delta.rotation[1]=n[1]*n[0]*v+n[2]*s;
    delta.rotation[2]=n[2]*n[0]*v-n[1]*s;
    delta.rotation[3]=n[0]*n[1]*v-n[2]*s;
    delta.rotation[4]=c+n[1]*n[1]*v;
    delta.rotation[5]=n[2]*n[1]*v+n[0]*s;
    delta.rotation[6]=n[0]*n[2]*v+n[1]*s;
    delta.rotation[7]=n[1]*n[2]*v-n[0]*s;
    delta.rotation[8]=c+n[2]*n[2]*v;
    for(int row=0;row<3;++row)
        delta.translation[row]=pivot.translation[row]-
            (delta.rotation[row]*pivot.translation[0]+
             delta.rotation[3+row]*pivot.translation[1]+
             delta.rotation[6+row]*pivot.translation[2]);
    if(!Halo2FirstPersonTransformValid(delta))return false;
    float staged[64*kHalo2FirstPersonNodeFloats]{};
    std::memcpy(staged,hands,static_cast<size_t>(handsCount)*kHalo2FirstPersonNodeStride);
    bool movedAny=false;
    for(uint32_t d=0;d<handsCount;++d)
    {
        const int source=remap[d];
        if(source<0||source>=static_cast<int>(sourceCount)||
            !(sourceMask&(uint64_t{1}<<source)))continue;
        Halo2FirstPersonTransform stock,moved{};
        if(!Halo2ReadFirstPersonTransform(hands+d*kHalo2FirstPersonNodeFloats,stock)||
            !Halo2ComposeFirstPersonTransforms(delta,stock,moved))return false;
        Halo2WriteFirstPersonTransform(moved,staged+d*kHalo2FirstPersonNodeFloats);
        movedAny=true;
    }
    if(!movedAny)return false;
    std::memcpy(hands,staged,static_cast<size_t>(handsCount)*kHalo2FirstPersonNodeStride);
    return true;
}

inline bool Halo2ApplyFreeSupportFingerGripPose(
    const Halo2FirstPersonArmBinding& binding,float* hands,uint32_t handsCount,
    const int32_t* remap,bool handAlignment,bool supportGripAttached,
    const ControllerFingerInput& input) noexcept
{
    if(!binding.fingerPoseSupported||supportGripAttached||!input.valid)return true;
    if(!hands||!remap||!handsCount||handsCount>64||!binding.count||binding.count>64)
        return false;
    if(!std::isfinite(input.grip)||input.grip<0||input.grip>1)return false;
    if(input.grip<=0.001f)return true;
    const unsigned side=handAlignment?1u:0u;
    const auto& sourceInventory=binding.fingerInventory[side];
    if(!sourceInventory.count||sourceInventory.rig.nodeCount!=binding.count||
        sourceInventory.rig.checksum!=binding.fingerRigIdentity)return false;
    int sourceToDestination[64]{};
    for(int& destination:sourceToDestination)destination=-1;
    for(uint32_t destination=0;destination<handsCount;++destination)
    {
        const int source=remap[destination];
        if(source< -1||source>=static_cast<int>(binding.count))return false;
        if(source>=0)
        {
            if(sourceToDestination[source]>=0)return false;
            sourceToDestination[source]=static_cast<int>(destination);
        }
    }
    const int wristSource=side?binding.rightWrist:binding.leftWrist;
    if(wristSource<0||wristSource>=static_cast<int>(binding.count)||
        sourceToDestination[wristSource]<0)return false;
    int16_t packetParents[64]{};
    for(unsigned node=0;node<handsCount;++node)packetParents[node]=-1;
    for(unsigned destination=0;destination<handsCount;++destination)
    {
        const int source=remap[destination];
        if(source<0)continue;
        const int parent=binding.parentIndices[source];
        if(parent>=0&&parent<static_cast<int>(binding.count)&&
            sourceToDestination[parent]>=0)
            packetParents[destination]=static_cast<int16_t>(sourceToDestination[parent]);
    }
    finger_joint::Inventory packetInventory{};
    if(!Halo2MapFingerInventoryToPacket(sourceInventory,remap,handsCount,packetInventory))return false;
    uint16_t chains[5][4]{};uint8_t lengths[5]{};
    for(unsigned slot=0;slot<5;++slot)
        for(unsigned joint=0;joint<4;++joint)
        {
            const auto* record=packetInventory.FindNative(slot,joint);
            if(!record)break;
            chains[slot][joint]=record->paletteIndex;
            ++lengths[slot];
        }
    Halo2FirstPersonTransform source[64]{},posed[64]{};
    for(unsigned node=0;node<handsCount;++node)
        if(!Halo2ReadFirstPersonTransform(hands+node*kHalo2FirstPersonNodeFloats,source[node]))return false;
    Halo2FirstPersonTransform marker{},palm{};
    if(!Halo2AnatomicalGripMarker(binding.rigKind,side==0,marker)||
        !Halo2ComposeFirstPersonTransforms(source[sourceToDestination[wristSource]],marker,palm))return false;
    const float down[]{-palm.rotation[6],-palm.rotation[7],-palm.rotation[8]};
    ControllerFingerInput gripOnly{true,0.0f,input.grip};
    if(!controller_finger_pose::Apply(source,handsCount,
        static_cast<size_t>(sourceToDestination[wristSource]),packetParents,chains,
        lengths,down,gripOnly,posed,static_cast<const Halo2FirstPersonTransform*>(nullptr),&packetInventory))return false;
    float staged[64*kHalo2FirstPersonNodeFloats]{};
    for(unsigned node=0;node<handsCount;++node)
        Halo2WriteFirstPersonTransform(posed[node],staged+node*kHalo2FirstPersonNodeFloats);
    std::memcpy(hands,staged,static_cast<size_t>(handsCount)*kHalo2FirstPersonNodeStride);
    return true;
}

// E-H2-45: transform the already root-composed render packets, using the
// engine-authored hands remap to carry the animation graph's invariant hand
// flags into destination-model node indices. This is deliberately independent
// of the frame interpolator: H2EK proves the packet builder uses weapon_data's
// authored palette whenever an interpolated bank is unavailable.
inline bool Halo2OwnFinalFirstPersonPackets(
    float* handsMatrices, uint32_t handsCount, const int32_t* handsRemap,
    const Halo2FirstPersonArmBinding& binding, float* gunMatrices,
    uint32_t gunCount, const Halo2CameraBasis& authoredRoot,
    const Halo2CameraBasis& rightCarrier,
    const Halo2CameraBasis& leftCarrier, bool twoHandAimActive,
    float rightScale, float leftScale, float worldScale,
    Halo2FinalPacketOwnershipResult& out, bool handAlignment = false,
    bool showTrackedArms = false, bool poseFreeSupportFingers = false,
    bool supportGripAttached = false,
    const ControllerFingerInput& supportFingerInput = {}) noexcept
{
    out = Halo2FinalPacketOwnershipResult{};
    if (!handsMatrices || !handsRemap || !binding.valid ||
        binding.count == 0 || binding.count > kHalo2FirstPersonPaletteCapacity ||
        handsCount == 0 || handsCount > kHalo2FirstPersonPaletteCapacity ||
        gunCount > kHalo2FirstPersonPaletteCapacity ||
        (gunCount && !gunMatrices) ||
        !Halo2ValidateCameraBasis(authoredRoot) ||
        !Halo2ValidateCameraBasis(rightCarrier) ||
        !Halo2ValidateCameraBasis(leftCarrier) || !std::isfinite(rightScale) ||
        !std::isfinite(leftScale) || !std::isfinite(worldScale) ||
        rightScale <= 0.0f || leftScale <= 0.0f || worldScale <= 0.0f)
    {
        return false;
    }

    int rightWristDestination = -1;
    int leftWristDestination = -1;
    for (uint32_t destination = 0; destination < handsCount; ++destination)
    {
        const int32_t source = handsRemap[destination];
        if (source < -1 || source >= static_cast<int32_t>(binding.count))
            return false;
        if (source == binding.rightWrist)
            rightWristDestination = static_cast<int>(destination);
        if (source == binding.leftWrist)
            leftWristDestination = static_cast<int>(destination);
    }
    if (rightWristDestination < 0 || leftWristDestination < 0)
        return false;

    // Stage the complete transaction. Any invalid final node leaves the stock
    // packets byte-identical; a half-moved hand/gun is never published.
    float stagedHands[
        kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    float stagedGun[
        kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    std::memcpy(stagedHands, handsMatrices,
                static_cast<size_t>(handsCount) * kHalo2FirstPersonNodeStride);
    if (gunCount)
        std::memcpy(stagedGun, gunMatrices,
                    static_cast<size_t>(gunCount) *
                        kHalo2FirstPersonNodeStride);

    const float* const rightWristMatrix = stagedHands +
        static_cast<uint32_t>(rightWristDestination) *
            kHalo2FirstPersonNodeFloats;
    const float* const leftWristMatrix = stagedHands +
        static_cast<uint32_t>(leftWristDestination) *
            kHalo2FirstPersonNodeFloats;
    Halo2FirstPersonTransform stockRight{}, stockLeft{}, stockGunRoot{};
    Halo2FirstPersonTransform desiredRight{}, desiredLeft{};
    if (!Halo2ReadFirstPersonTransform(rightWristMatrix, stockRight) ||
        !Halo2ReadFirstPersonTransform(leftWristMatrix, stockLeft) ||
        !Halo2ReadFirstPersonTransform(stagedGun, stockGunRoot))
        return false;

    int thumbDestination = -1;
    float closestThumbSquared = std::numeric_limits<float>::infinity();
    Halo2FirstPersonTransform stockThumb{};
    for (uint32_t destination = 0; destination < handsCount; ++destination)
    {
        const int32_t source = handsRemap[destination];
        const uint64_t bit = source >= 0 ? uint64_t{1} << source : 0;
        if (!(bit && (binding.leftDirectChildren & bit))) continue;
        Halo2FirstPersonTransform candidateThumb{};
        if (!Halo2ReadFirstPersonTransform(
                stagedHands + destination * kHalo2FirstPersonNodeFloats,
                candidateThumb))
            return false;
        float distanceSquared = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float distance = candidateThumb.translation[axis] -
                stockLeft.translation[axis];
            distanceSquared += distance * distance;
        }
        if (distanceSquared < closestThumbSquared)
        {
            closestThumbSquared = distanceSquared;
            thumbDestination = static_cast<int>(destination);
            stockThumb = candidateThumb;
        }
    }
    Halo2FirstPersonTransform rightDelta{};
    if (kHalo2C70CorrectedHandFrameEnabled)
    {
        if (!Halo2BuildAuthoredBarrelDelta(
                stockRight, stockGunRoot, authoredRoot, rightCarrier,
                rightScale, rightDelta, desiredRight))
            return false;
        if (twoHandAimActive)
        {
            if (!Halo2ComposeFirstPersonTransforms(
                    rightDelta, stockLeft, desiredLeft))
                return false;
            desiredLeft.scale = stockLeft.scale * leftScale;
        }
        else if (!Halo2BuildCorrectedRigMarkerFreeLeftTarget(
                     binding.rigKind, leftCarrier, stockLeft, leftScale,
                     desiredLeft))
        {
            return false;
        }
    }
    else if (kHalo2C68RigMarkerAlignmentEnabled)
    {
        if (!Halo2BuildAuthoredBarrelDelta(
                stockRight, stockGunRoot, authoredRoot, rightCarrier,
                rightScale, rightDelta, desiredRight))
            return false;
        if (twoHandAimActive)
        {
            if (!Halo2ComposeFirstPersonTransforms(
                    rightDelta, stockLeft, desiredLeft))
                return false;
            desiredLeft.scale = stockLeft.scale * leftScale;
        }
        else if (!Halo2BuildRigMarkerFreeLeftTarget(
                     binding.rigKind, leftCarrier, stockLeft, leftScale,
                     desiredLeft))
        {
            return false;
        }
    }
    else if (!kHalo2C66AuthoredAlignmentEnabled)
    {
        if (!Halo2BuildControllerRerootedWristTarget(
                rightCarrier, authoredRoot, stockRight, rightScale,
                desiredRight) ||
            !Halo2BuildLeftPresentationWristTarget(
                twoHandAimActive, desiredRight, stockRight, stockLeft,
                authoredRoot, leftCarrier, leftScale, desiredLeft) ||
            !Halo2BuildFirstPersonWorldDelta(
                desiredRight, stockRight, rightDelta))
            return false;
    }
    else
    {
        if (thumbDestination < 0 ||
            !Halo2BuildAuthoredBarrelDelta(
                stockRight, stockGunRoot, authoredRoot, rightCarrier,
                rightScale, rightDelta, desiredRight))
            return false;
        if (twoHandAimActive)
        {
            if (!Halo2ComposeFirstPersonTransforms(
                    rightDelta, stockLeft, desiredLeft))
                return false;
            desiredLeft.scale = stockLeft.scale * leftScale;
        }
        else
        {
            Halo2FirstPersonTransform controllerLeft{};
            if (!Halo2BuildControllerRerootedWristTarget(
                    leftCarrier, authoredRoot, stockLeft, leftScale,
                    controllerLeft) ||
                !Halo2BuildAnatomicalFreeLeftTarget(
                    stockLeft, stockThumb, controllerLeft, desiredLeft))
                return false;
        }
    }
    if (!Halo2FirstPersonTransformValid(desiredLeft) ||
        !Halo2FirstPersonWristDeltaPlausible(
            stockRight, desiredRight, worldScale) ||
        !Halo2FirstPersonWristDeltaPlausible(
            stockLeft, desiredLeft, worldScale))
    {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        const float rightDelta =
            desiredRight.translation[axis] - stockRight.translation[axis];
        const float leftDelta =
            desiredLeft.translation[axis] - stockLeft.translation[axis];
        out.rightWristDeltaWorld += rightDelta * rightDelta;
        out.leftWristDeltaWorld += leftDelta * leftDelta;
    }
    out.rightWristDeltaWorld = std::sqrt(out.rightWristDeltaWorld);
    out.leftWristDeltaWorld = std::sqrt(out.leftWristDeltaWorld);
    if (!std::isfinite(out.rightWristDeltaWorld) ||
        !std::isfinite(out.leftWristDeltaWorld))
        return false;
    Halo2FirstPersonTransform leftDelta{};
    if (!Halo2BuildFirstPersonWorldDelta(
            desiredLeft, stockLeft, leftDelta))
        return false;

    const bool jointIndicesValid=binding.leftShoulder>=0&&
        binding.leftShoulder<64&&binding.leftShoulder<binding.count&&
        binding.leftElbow>=0&&binding.leftElbow<64&&
        binding.leftElbow<binding.count&&binding.rightShoulder>=0&&
        binding.rightShoulder<64&&binding.rightShoulder<binding.count&&
        binding.rightElbow>=0&&binding.rightElbow<64&&
        binding.rightElbow<binding.count&&
        binding.leftShoulder!=binding.rightShoulder&&
        binding.leftElbow!=binding.rightElbow&&
        binding.leftShoulder!=binding.leftElbow&&
        binding.rightShoulder!=binding.rightElbow;
    const uint64_t solveJointMask = jointIndicesValid ?
        ((uint64_t{1} << binding.leftShoulder) |
         (uint64_t{1} << binding.leftElbow) |
         (uint64_t{1} << binding.rightShoulder) |
         (uint64_t{1} << binding.rightElbow)) : 0;
    const bool solveArms = showTrackedArms &&
        binding.rigKind != Halo2FirstPersonRigKind::Unknown &&
        jointIndicesValid &&
        solveJointMask != 0;

    for (uint32_t destination = 0; destination < handsCount; ++destination)
    {
        float* const node = stagedHands +
            destination * kHalo2FirstPersonNodeFloats;
        const int32_t source = handsRemap[destination];
        const uint64_t bit = source >= 0 ? uint64_t{1} << source : 0;
        if (bit && (binding.rightSubtree & bit))
        {
            Halo2FirstPersonTransform stockNode{}, moved{};
            if (!Halo2ReadFirstPersonTransform(node, stockNode) ||
                !Halo2ComposeFirstPersonTransforms(
                    rightDelta, stockNode, moved))
                return false;
            Halo2WriteFirstPersonTransform(moved, node);
            ++out.rightNodes;
        }
        else if (bit && (binding.leftSubtree & bit))
        {
            Halo2FirstPersonTransform stockNode{}, moved{};
            if (!Halo2ReadFirstPersonTransform(node, stockNode) ||
                !Halo2ComposeFirstPersonTransforms(
                    leftDelta, stockNode, moved))
                return false;
            Halo2WriteFirstPersonTransform(moved, node);
            ++out.leftNodes;
        }
        else
        {
            if (solveArms && bit && (solveJointMask & bit))
            {
                // Keep only verified upperarm/forearm joints for the optional
                // final-packet solver. Every other non-hand node retains the
                // established hidden-body behavior.
            }
            else if (binding.rigKind == Halo2FirstPersonRigKind::Elite && bit &&
                (binding.armAncestors & bit))
            {
                const Halo2FirstPersonTransform& wrist =
                    (binding.leftArmAncestors & bit) ? desiredLeft : desiredRight;
                Halo2FirstPersonTransform hidden = wrist;
                hidden.scale *= 0.0001f;
                Halo2WriteFirstPersonTransform(hidden, node);
                ++out.coLocatedArmNodes;
            }
            else
            {
                node[0] *= 0.0001f;
            }
            ++out.collapsedNodes;
        }
    }
    for (uint32_t nodeIndex = 0; nodeIndex < gunCount; ++nodeIndex)
    {
        float* const node = stagedGun +
            nodeIndex * kHalo2FirstPersonNodeFloats;
        Halo2FirstPersonTransform stockNode{}, moved{};
        if (!Halo2ReadFirstPersonTransform(node, stockNode) ||
            !Halo2ComposeFirstPersonTransforms(
                rightDelta, stockNode, moved))
            return false;
        Halo2WriteFirstPersonTransform(moved, node);
        ++out.gunNodes;
    }
    out.applied = out.rightNodes && out.leftNodes && out.gunNodes;
    if (!out.applied) return false;
    if (handAlignment && !Halo2RouteLeftHandedPacketHands(stagedHands, handsCount,
            handsRemap, binding, handsRemap, binding, rightCarrier, leftCarrier))
    {
        out.applied = false;
        return false;
    }
    if (solveArms)
    {
        auto destinationForSource=[&](int source)->int {
            if(source<0)return -1;
            for(uint32_t destination=0;destination<handsCount;++destination)
                if(handsRemap[destination]==source)
                    return static_cast<int>(destination);
            return -1;
        };
        const int leftShoulder=destinationForSource(binding.leftShoulder);
        const int leftElbow=destinationForSource(binding.leftElbow);
        const int leftWrist=destinationForSource(binding.leftWrist);
        const int rightShoulder=destinationForSource(binding.rightShoulder);
        const int rightElbow=destinationForSource(binding.rightElbow);
        const int rightWrist=destinationForSource(binding.rightWrist);
        const size_t handBytes=static_cast<size_t>(handsCount)*
            kHalo2FirstPersonNodeStride;
        float beforeSolve[kHalo2FirstPersonPaletteCapacity*
            kHalo2FirstPersonNodeFloats]{};
        std::memcpy(beforeSolve,stagedHands,handBytes);
        bool solved=false;
        if(leftShoulder>=0&&leftElbow>=0&&leftWrist>=0&&
           rightShoulder>=0&&rightElbow>=0&&rightWrist>=0)
        {
            Halo2FirstPersonTransform ls{},le{},lw{},rs{},re{},rw{};
            solved=Halo2ReadFirstPersonTransform(stagedHands+
                    static_cast<uint32_t>(leftShoulder)*
                        kHalo2FirstPersonNodeFloats,ls)&&
                Halo2ReadFirstPersonTransform(stagedHands+
                    static_cast<uint32_t>(leftElbow)*
                        kHalo2FirstPersonNodeFloats,le)&&
                Halo2ReadFirstPersonTransform(stagedHands+
                    static_cast<uint32_t>(leftWrist)*
                        kHalo2FirstPersonNodeFloats,lw)&&
                Halo2ReadFirstPersonTransform(stagedHands+
                    static_cast<uint32_t>(rightShoulder)*
                        kHalo2FirstPersonNodeFloats,rs)&&
                Halo2ReadFirstPersonTransform(stagedHands+
                    static_cast<uint32_t>(rightElbow)*
                        kHalo2FirstPersonNodeFloats,re)&&
                Halo2ReadFirstPersonTransform(stagedHands+
                    static_cast<uint32_t>(rightWrist)*
                        kHalo2FirstPersonNodeFloats,rw)&&
                Halo2SolveFinalPacketArm(ls,le,stockLeft,lw)&&
                Halo2SolveFinalPacketArm(rs,re,stockRight,rw);
            if(solved)
            {
                Halo2WriteFirstPersonTransform(ls,stagedHands+
                    static_cast<uint32_t>(leftShoulder)*
                        kHalo2FirstPersonNodeFloats);
                Halo2WriteFirstPersonTransform(le,stagedHands+
                    static_cast<uint32_t>(leftElbow)*
                        kHalo2FirstPersonNodeFloats);
                Halo2WriteFirstPersonTransform(rs,stagedHands+
                    static_cast<uint32_t>(rightShoulder)*
                        kHalo2FirstPersonNodeFloats);
                Halo2WriteFirstPersonTransform(re,stagedHands+
                    static_cast<uint32_t>(rightElbow)*
                        kHalo2FirstPersonNodeFloats);
            }
        }
        if(!solved)
        {
            std::memcpy(stagedHands,beforeSolve,handBytes);
            for(uint32_t destination=0;destination<handsCount;++destination)
            {
                const int source=handsRemap[destination];
                const uint64_t bit=source>=0?uint64_t{1}<<source:0;
                if(!(bit&(binding.armAncestors)))continue;
                if(binding.rigKind==Halo2FirstPersonRigKind::Elite)
                {
                    const Halo2FirstPersonTransform& wrist=
                        (bit&binding.leftArmAncestors)?desiredLeft:desiredRight;
                    Halo2FirstPersonTransform hidden=wrist;
                    hidden.scale*=0.0001f;
                    Halo2WriteFirstPersonTransform(hidden,stagedHands+
                        destination*kHalo2FirstPersonNodeFloats);
                }
                else
                    stagedHands[destination*kHalo2FirstPersonNodeFloats]=0.0001f;
            }
        }
        out.armsSolved=solved;
    }
    if(poseFreeSupportFingers&&!supportGripAttached)
        (void)Halo2ApplyFreeSupportFingerGripPose(binding,stagedHands,handsCount,
            handsRemap,handAlignment,false,supportFingerInput);
    std::memcpy(handsMatrices, stagedHands,
                static_cast<size_t>(handsCount) * kHalo2FirstPersonNodeStride);
    std::memcpy(gunMatrices, stagedGun,
                static_cast<size_t>(gunCount) * kHalo2FirstPersonNodeStride);
    return out.applied;
}

// H2EK 306E04 merges both slots' authored hand remaps into one hands packet,
// then emits a separate gun packet for each weapon. Slot 1 may omit the right
// wrist entirely (confirmed by the 2026-09-05 live dual-slot capture). Preserve
// the primary transaction and replace only the secondary's actual left subtree
// with a rigid hand/gun transform derived from that gun's completed native root.
inline bool Halo2OwnDualFirstPersonPackets(
    float* hands, uint32_t handsCount, const int32_t* primaryRemap,
    const Halo2FirstPersonArmBinding& primaryBinding,
    float* primaryGun, uint32_t primaryCount,
    const int32_t* secondaryRemap,
    const Halo2FirstPersonArmBinding& secondaryBinding,
    float* secondaryGun, uint32_t secondaryCount,
    const Halo2CameraBasis& authoredRoot,
    const Halo2CameraBasis& rightCarrier, const Halo2CameraBasis& leftCarrier,
    float rightScale, float leftScale, float worldScale,
    Halo2FinalPacketOwnershipResult& out, bool handAlignment = false) noexcept
{
    out={};
    if(!hands || !primaryRemap || !primaryGun || !secondaryRemap || !secondaryGun ||
        !handsCount || handsCount>kHalo2FirstPersonPaletteCapacity ||
        !primaryCount || primaryCount>kHalo2FirstPersonPaletteCapacity ||
        !secondaryCount || secondaryCount>kHalo2FirstPersonPaletteCapacity ||
        !primaryBinding.valid || !primaryBinding.count ||
        primaryBinding.count>kHalo2FirstPersonPaletteCapacity ||
        !secondaryBinding.valid || !secondaryBinding.count ||
        secondaryBinding.count>kHalo2FirstPersonPaletteCapacity ||
        secondaryBinding.rigKind!=primaryBinding.rigKind)
        return false;
    int leftWrist=-1;
    uint64_t leftDestinations=0;
    for(uint32_t destination=0;destination<handsCount;++destination)
    {
        const int32_t source=secondaryRemap[destination];
        const int32_t primarySource=primaryRemap[destination];
        if(source < -1 || source>=static_cast<int32_t>(secondaryBinding.count) ||
            primarySource < -1 || primarySource>=static_cast<int32_t>(primaryBinding.count))
            return false;
        if(source==secondaryBinding.leftWrist)
        {
            if(leftWrist!=-1) return false;
            leftWrist=static_cast<int>(destination);
        }
        if(source>=0 && (secondaryBinding.leftSubtree&(uint64_t{1}<<source)))
        {
            // A malformed map may not transfer a primary right-hand bone to
            // the secondary transaction.
            if(primarySource>=0 &&
                (primaryBinding.rightSubtree&(uint64_t{1}<<primarySource)))
                return false;
            leftDestinations|=uint64_t{1}<<destination;
        }
    }
    if(leftWrist<0 || !(leftDestinations&(uint64_t{1}<<leftWrist))) return false;
    Halo2FirstPersonTransform stockLeft{},stockGun{},leftDelta{},desiredLeft{};
    if(!Halo2ReadFirstPersonTransform(
            hands+leftWrist*kHalo2FirstPersonNodeFloats,stockLeft) ||
        !Halo2ReadFirstPersonTransform(secondaryGun,stockGun) ||
        !Halo2BuildAuthoredBarrelDelta(stockLeft,stockGun,authoredRoot,
            leftCarrier,leftScale,leftDelta,desiredLeft) ||
        !Halo2FirstPersonWristDeltaPlausible(stockLeft,desiredLeft,worldScale))
        return false;
    float stagedHands[kHalo2FirstPersonPaletteCapacity*kHalo2FirstPersonNodeFloats]{};
    float stagedPrimary[kHalo2FirstPersonPaletteCapacity*kHalo2FirstPersonNodeFloats]{};
    float stagedSecondary[kHalo2FirstPersonPaletteCapacity*kHalo2FirstPersonNodeFloats]{};
    const size_t handsBytes=handsCount*kHalo2FirstPersonNodeStride;
    const size_t primaryBytes=primaryCount*kHalo2FirstPersonNodeStride;
    const size_t secondaryBytes=secondaryCount*kHalo2FirstPersonNodeStride;
    std::memcpy(stagedHands,hands,handsBytes);
    std::memcpy(stagedPrimary,primaryGun,primaryBytes);
    Halo2FinalPacketOwnershipResult result{};
    if(!Halo2OwnFinalFirstPersonPackets(stagedHands,handsCount,primaryRemap,
            primaryBinding,stagedPrimary,primaryCount,authoredRoot,
            rightCarrier,leftCarrier,false,rightScale,leftScale,worldScale,result))
        return false;
    result.leftNodes=0;
    for(uint32_t destination=0;destination<handsCount;++destination)
    {
        if(!(leftDestinations&(uint64_t{1}<<destination))) continue;
        Halo2FirstPersonTransform stock{},moved{};
        if(!Halo2ReadFirstPersonTransform(
                hands+destination*kHalo2FirstPersonNodeFloats,stock) ||
            !Halo2ComposeFirstPersonTransforms(leftDelta,stock,moved))
            return false;
        Halo2WriteFirstPersonTransform(moved,
            stagedHands+destination*kHalo2FirstPersonNodeFloats);
        ++result.leftNodes;
    }
    for(uint32_t node=0;node<secondaryCount;++node)
    {
        Halo2FirstPersonTransform stock{},moved{};
        if(!Halo2ReadFirstPersonTransform(
                secondaryGun+node*kHalo2FirstPersonNodeFloats,stock) ||
            !Halo2ComposeFirstPersonTransforms(leftDelta,stock,moved))
            return false;
        Halo2WriteFirstPersonTransform(moved,
            stagedSecondary+node*kHalo2FirstPersonNodeFloats);
    }
    float distanceSquared=0;
    for(int axis=0;axis<3;++axis)
    {
        const float delta=desiredLeft.translation[axis]-stockLeft.translation[axis];
        distanceSquared+=delta*delta;
    }
    result.leftWristDeltaWorld=std::sqrt(distanceSquared);
    result.gunNodes+=secondaryCount;
    if (handAlignment && !Halo2RouteLeftHandedPacketHands(stagedHands, handsCount,
            primaryRemap, primaryBinding, secondaryRemap, secondaryBinding,
            rightCarrier, leftCarrier)) return false;
    std::memcpy(hands,stagedHands,handsBytes);
    std::memcpy(primaryGun,stagedPrimary,primaryBytes);
    std::memcpy(secondaryGun,stagedSecondary,secondaryBytes);
    out=result;
    return true;
}

// True when the two bases are so close the re-anchor would be a no-op.
inline bool Halo2CameraBasesNearlyEqual(
    const Halo2CameraBasis& a, const Halo2CameraBasis& b) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(a.position[axis] - b.position[axis]) > 1.0e-5f ||
            std::fabs(a.forward[axis] - b.forward[axis]) > 1.0e-6f ||
            std::fabs(a.up[axis] - b.up[axis]) > 1.0e-6f)
        {
            return false;
        }
    }
    return true;
}

// E-H2-31 (C-H2-36/37): the camera globals the classic first-person pass uses.
// 0x1996A28 is the PUSHED RASTER camera draw_first_person copies and rebuilds
// its projection from (E-H2-20). 0x165C260 is the RENDER camera global that
// render_view writes from the window's render camera and that the rest of the
// pipeline reads. C-H2-36 centred only the raster one and the measured weapon
// disparity did not move (eye dumps: world 3-24 px, weapon >240 px at full
// resolution), so the placement is taken from the other. Both are centred.
inline constexpr uint32_t kHalo2ClassicFirstPersonCameraGlobalRva = 0x01996A28;
inline constexpr uint32_t kHalo2ClassicRenderCameraGlobalRva = 0x0165C260;

// E-H2-69 (C-H2-81): when the classic per-eye compensation may run at all.
//
// The compensation `x' = R_v R_c^T (x - c) + v` is the ONLY mechanism that
// can give the classic weapon its own per-eye parallax (E-H2-68: writing the
// camera globals at draw time was measured inert, and a projection change
// cannot create parallax). It is also what displaced and rotated the rig in
// C-H2-63/78 when the viewing camera was predicted wrongly. This admits it
// only in the exact shape that can add depth without being able to move the
// rig anywhere the user would see as displaced:
//
//   * the viewing camera's ORIENTATION must equal the correct eye's, so the
//     delta rotation is the identity and no rotation can ever be introduced;
//   * its POSITION may differ by at most `maxSeparation` - the live distance
//     between this eye and the pair centre, i.e. one half-IPD, which is
//     exactly the parallax being restored. Anything larger is a stale or
//     foreign camera and is refused.
//
// A refusal leaves the packet byte-identical to Anniversary's (C-H2-80
// parity). An admission reproduces the image the correct eye camera would
// have produced of that same packet - which is the image Anniversary draws -
// so placement and rotation are preserved while the eyes differ by the true
// eye offset.
inline bool Halo2ClassicEyeCompensationAdmissible(
    const Halo2CameraBasis& correct, const Halo2CameraBasis& viewing,
    float maxSeparation) noexcept
{
    if (!Halo2ValidateCameraBasis(correct) ||
        !Halo2ValidateCameraBasis(viewing) ||
        !std::isfinite(maxSeparation) || maxSeparation <= 0.0f)
    {
        return false;
    }
    // Orientation must agree to within float noise on both axes.
    constexpr float kAxisTolerance = 1.0e-4f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(correct.forward[axis] - viewing.forward[axis]) >
                kAxisTolerance ||
            std::fabs(correct.up[axis] - viewing.up[axis]) > kAxisTolerance)
        {
            return false;
        }
    }
    float separationSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float delta = viewing.position[axis] - correct.position[axis];
        separationSquared += delta * delta;
    }
    if (!std::isfinite(separationSquared))
        return false;
    return separationSquared <= maxSeparation * maxSeparation;
}

// C-H2-85: the Halo 2 CLASSIC-only trim. Rotates and offsets a controller
// carrier about its OWN axes, so the visible gun and the hands riding it
// move together and Classic can be lined up with what Anniversary shows.
// Anniversary never calls this. All-zero returns the carrier unchanged, so
// the default is exactly the previous behaviour.
//
// Axes are the carrier's own: forward (barrel), right, up. Pitch raises or
// lowers the muzzle, yaw swings it sideways, roll spins it about the barrel.
// Offsets are metres along the same axes, converted by the caller's world
// scale. The pivot is the carrier's position - the physical controller - so
// the grip stays in the hand while the muzzle swings, which is what a mount
// correction should do.
inline bool Halo2ApplyCarrierTrim(
    const Halo2CameraBasis& carrier, float pitchDegrees, float yawDegrees,
    float rollDegrees, float forwardMeters, float rightMeters,
    float upMeters, float worldScale, Halo2CameraBasis& out) noexcept
{
    out = carrier;
    if (!Halo2ValidateCameraBasis(carrier) || !std::isfinite(pitchDegrees) ||
        !std::isfinite(yawDegrees) || !std::isfinite(rollDegrees) ||
        !std::isfinite(forwardMeters) || !std::isfinite(rightMeters) ||
        !std::isfinite(upMeters) || !std::isfinite(worldScale) ||
        worldScale <= 0.0f)
    {
        return false;
    }
    const bool identity = pitchDegrees == 0.0f && yawDegrees == 0.0f &&
        rollDegrees == 0.0f && forwardMeters == 0.0f &&
        rightMeters == 0.0f && upMeters == 0.0f;
    if (identity)
        return true;

    float right[3] = {
        carrier.forward[1] * carrier.up[2] - carrier.forward[2] * carrier.up[1],
        carrier.forward[2] * carrier.up[0] - carrier.forward[0] * carrier.up[2],
        carrier.forward[0] * carrier.up[1] - carrier.forward[1] * carrier.up[0]};
    float forward[3] = {
        carrier.forward[0], carrier.forward[1], carrier.forward[2]};
    float up[3] = {carrier.up[0], carrier.up[1], carrier.up[2]};

    constexpr float kRadians = 0.01745329252f;
    // Rotate the axis triple about its own axes, in a fixed order so the
    // sliders stay predictable: roll (about forward), then pitch (about
    // right), then yaw (about up).
    auto rotateAbout = [](const float axis[3], float degrees, float vector[3]) {
        if (degrees == 0.0f) return;
        const float angle = degrees * kRadians;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float dot = axis[0] * vector[0] + axis[1] * vector[1] +
            axis[2] * vector[2];
        const float cross[3] = {
            axis[1] * vector[2] - axis[2] * vector[1],
            axis[2] * vector[0] - axis[0] * vector[2],
            axis[0] * vector[1] - axis[1] * vector[0]};
        for (int component = 0; component < 3; ++component)
        {
            vector[component] = vector[component] * c + cross[component] * s +
                axis[component] * dot * (1.0f - c);
        }
    };

    const float rollAxis[3] = {forward[0], forward[1], forward[2]};
    rotateAbout(rollAxis, rollDegrees, up);
    rotateAbout(rollAxis, rollDegrees, right);
    const float pitchAxis[3] = {right[0], right[1], right[2]};
    rotateAbout(pitchAxis, pitchDegrees, forward);
    rotateAbout(pitchAxis, pitchDegrees, up);
    const float yawAxis[3] = {up[0], up[1], up[2]};
    rotateAbout(yawAxis, yawDegrees, forward);
    rotateAbout(yawAxis, yawDegrees, right);

    Halo2CameraBasis trimmed{};
    for (int axis = 0; axis < 3; ++axis)
    {
        trimmed.forward[axis] = forward[axis];
        trimmed.up[axis] = up[axis];
        trimmed.position[axis] = carrier.position[axis] +
            (forward[axis] * forwardMeters + right[axis] * rightMeters +
             up[axis] * upMeters) * worldScale;
    }
    if (!Halo2ValidateCameraBasis(trimmed))
        return false;
    out = trimmed;
    return true;
}

// Blends two camera bases the way the interpolator blends the weapon, and
// re-orthonormalises, so the result is always a valid basis.
inline bool Halo2LerpCameraBasis(
    const Halo2CameraBasis& previous, const Halo2CameraBasis& current,
    float factor, Halo2CameraBasis& out) noexcept
{
    if (!Halo2ValidateCameraBasis(previous) ||
        !Halo2ValidateCameraBasis(current) || !std::isfinite(factor))
    {
        return false;
    }
    const float t = factor < 0.0f ? 0.0f : (factor > 1.0f ? 1.0f : factor);
    Halo2CameraBasis candidate{};
    for (int axis = 0; axis < 3; ++axis)
    {
        candidate.position[axis] = previous.position[axis] +
            (current.position[axis] - previous.position[axis]) * t;
        candidate.forward[axis] = previous.forward[axis] +
            (current.forward[axis] - previous.forward[axis]) * t;
        candidate.up[axis] = previous.up[axis] +
            (current.up[axis] - previous.up[axis]) * t;
    }
    float forwardLength = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        forwardLength += candidate.forward[axis] * candidate.forward[axis];
    forwardLength = std::sqrt(forwardLength);
    if (!std::isfinite(forwardLength) || forwardLength < 1.0e-4f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        candidate.forward[axis] /= forwardLength;
    float dot = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        dot += candidate.up[axis] * candidate.forward[axis];
    for (int axis = 0; axis < 3; ++axis)
        candidate.up[axis] -= candidate.forward[axis] * dot;
    float upLength = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        upLength += candidate.up[axis] * candidate.up[axis];
    upLength = std::sqrt(upLength);
    if (!std::isfinite(upLength) || upLength < 1.0e-4f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        candidate.up[axis] /= upLength;
    if (!Halo2ValidateCameraBasis(candidate))
        return false;
    out = candidate;
    return true;
}
inline constexpr int kHalo2EyeBoundRtvSlots = 6;
inline constexpr int kHalo2CaptureCandidateSlots = 2 + kHalo2EyeBoundRtvSlots;

// Advances the probe pair over the candidate set, skipping the live source.
// Returns the candidate index to probe for `slot` (0 or 1), or -1 if the set
// is too small to fill that slot.
inline int Halo2ProbeCandidateForSlot(
    int rotation, int slot, int source, int candidateCount) noexcept
{
    if (slot < 0 || slot > 1 || candidateCount <= 1 || rotation < 0)
        return -1;
    int seen = 0;
    for (int step = 0; step < candidateCount; ++step)
    {
        const int candidate = (rotation + step) % candidateCount;
        if (candidate == source)
            continue;
        if (seen == slot)
            return candidate;
        ++seen;
    }
    return -1;
}

// One full sweep of the probe pair is `candidateCount - 1` candidates, so the
// rotation advances by the number of slots actually filled.
inline int Halo2NextProbeRotation(
    int rotation, int candidateCount, int filledSlots) noexcept
{
    if (candidateCount <= 1 || filledSlots <= 0)
        return 0;
    const int next = rotation + filledSlots;
    return next % candidateCount;
}

// E-H2-34 (C-H2-39): the cameras of the eye pass being rendered, handed to
// the weapon re-anchor by whichever stereo core owns the frame.
//   frame   - the tracked CENTRE camera the eyes are rendered from (world
//             units). The interpolated weapon geometry is moved rigidly from
//             the witnessed tick's camera to it, so the weapon follows the
//             head at the player's frame rate in both renderers.
//   correct - this eye's own camera (centre + eye offset).
//   viewing - the camera the first-person pass will ACTUALLY draw from.
// `compensate` names a classic defect measured in the C-H2-36 eye pictures:
// the classic first-person pass draws from the camera of the PREVIOUS
// render_view (the gun sat 150 px to the right in eye 1 while the world
// matched - the full eye offset, the wrong way round). When set, the
// geometry is additionally moved so that the stale camera's image of it is
// exactly the correct camera's image: x' = F_v F_c^T (x - c) + v.
struct Halo2FirstPersonPassCameras
{
    Halo2CameraBasis frame{};
    Halo2CameraBasis correct{};
    Halo2CameraBasis viewing{};
    bool frameValid = false;
    bool compensate = false;
};

// C-H2-63 Classic eye ownership. The Classic renderer consumes the persistent
// packet built with publish_to_renderer=false, then draw_first_person renders
// that same packet once for each eye. Its pushed first-person camera is the
// previous render_view camera (E-H2-34), so move the final world palette by
// viewing * inverse(correct). The stale viewing camera then produces exactly
// the image the correct eye camera would have produced. Work is staged so a
// bad matrix cannot leave half of an eye palette transformed.
inline bool Halo2CompensateClassicFirstPersonEye(
    float* matrices, uint32_t count,
    const Halo2FirstPersonPassCameras& pass) noexcept
{
    if (!matrices || count == 0 ||
        count > kHalo2FirstPersonPaletteCapacity || !pass.frameValid ||
        !pass.compensate || !Halo2ValidateCameraBasis(pass.correct) ||
        !Halo2ValidateCameraBasis(pass.viewing))
    {
        return false;
    }
    float staged[
        kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    const size_t floats =
        static_cast<size_t>(count) * kHalo2FirstPersonNodeFloats;
    const size_t bytes = floats * sizeof(float);
    std::memcpy(staged, matrices, bytes);
    for (size_t index = 0; index < floats; ++index)
        if (!std::isfinite(staged[index])) return false;

    float compensation[9]{};
    if (!Halo2BuildWorldDeltaRotation(
            pass.correct, pass.viewing, compensation))
    {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        Halo2ReanchorFirstPersonNode(
            staged + index * kHalo2FirstPersonNodeFloats, compensation,
            pass.correct.position, pass.viewing.position);
    }
    for (size_t index = 0; index < floats; ++index)
        if (!std::isfinite(staged[index])) return false;
    std::memcpy(matrices, staged, bytes);
    return true;
}

// E-H2-66 (C-H2-78): the classic pass draws BOTH eyes' weapons from one
// common camera (the tracked centre), so the stereo core names that centre
// as `viewing` for every pass; the previous-eye model doubled the weapon's
// disparity (measured 2026-08-31).

enum class Halo2FirstPersonNodeSpace : uint8_t
{
    Unknown = 0,
    World = 1,
    CameraRelative = 2,
};

// Decides from the root node's position and the tick camera whether the
// interpolated nodes carry WORLD positions (within a few world units of the
// camera) or positions RELATIVE to the camera (near the origin while the
// camera is far from it). Anything else is Unknown and the nodes are left
// exactly as the engine produced them.
inline Halo2FirstPersonNodeSpace Halo2ClassifyFirstPersonNodeSpace(
    const float rootPosition[3], const float tickCamera[3]) noexcept
{
    constexpr float kNearSquared = 4.0f * 4.0f;   // 4 wu ~ 12 m
    constexpr float kFarSquared = 8.0f * 8.0f;
    float fromCamera = 0.0f;
    float fromOrigin = 0.0f;
    float cameraFromOrigin = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(rootPosition[axis]) || !std::isfinite(tickCamera[axis]))
            return Halo2FirstPersonNodeSpace::Unknown;
        const float d = rootPosition[axis] - tickCamera[axis];
        fromCamera += d * d;
        fromOrigin += rootPosition[axis] * rootPosition[axis];
        cameraFromOrigin += tickCamera[axis] * tickCamera[axis];
    }
    if (!std::isfinite(fromCamera) || !std::isfinite(fromOrigin) ||
        !std::isfinite(cameraFromOrigin))
    {
        return Halo2FirstPersonNodeSpace::Unknown;
    }
    if (fromCamera < kNearSquared)
        return Halo2FirstPersonNodeSpace::World;
    if (fromOrigin < kNearSquared && cameraFromOrigin > kFarSquared)
        return Halo2FirstPersonNodeSpace::CameraRelative;
    return Halo2FirstPersonNodeSpace::Unknown;
}

// Per-slot cache that makes the re-anchor idempotent. The interpolator may
// hand the renderer the same node array it returned last time (not
// re-blended), in which case the array still holds the mod's previous
// output; the engine's own values are kept here and every call re-derives
// its output from them, so a transform can never accumulate.
struct Halo2FirstPersonSlotCache
{
    uint32_t count = 0;
    bool valid = false;
    float original[kHalo2FirstPersonNodeLimit * kHalo2FirstPersonNodeFloats]{};
    float written[kHalo2FirstPersonNodeLimit * kHalo2FirstPersonNodeFloats]{};
};

struct Halo2FirstPersonReanchorResult
{
    bool applied = false;
    bool fromCache = false;
    bool compensated = false;
    Halo2FirstPersonNodeSpace space = Halo2FirstPersonNodeSpace::Unknown;
};

// Places one complete camera-relative first-person assembly on a controller
// carrier. H2EK proves these are absolute per-node matrices (0x34 bytes each),
// so one rigid transform applied to every node preserves the authored weapon,
// hand and animation relationship without requiring a guessed wrist index.
// Only the caller decides which first-person slot is controller-owned.
// The rigid transform that carries first-person geometry from the render
// camera's local frame onto a controller carrier: R = H^T C, t = H^T (Cp - Hp),
// with camera columns ordered [right, forward, up].
inline bool Halo2ComputeCarrierDelta(
    const Halo2CameraBasis& trackedCamera, const Halo2CameraBasis& carrier,
    float rotation[9], float translation[3]) noexcept
{
    if (!Halo2ValidateCameraBasis(trackedCamera) ||
        !Halo2ValidateCameraBasis(carrier))
        return false;

    // These nodes are camera-relative, so the required rotation is H^T C:
    // controller axes expressed in the tracked-camera frame. The old code
    // called Halo2BuildWorldDeltaRotation, which returns C H^T. That is a
    // WORLD-space delta and only appears correct while H is the identity.
    // After a body/camera turn it conjugates the controller rotation through
    // the old forward frame, producing the headset-reported tracking failure.
    float trackedAxes[3][3]{};
    float carrierAxes[3][3]{};
    const Halo2CameraBasis* const cameras[2] = {&trackedCamera, &carrier};
    float (*const axes[2])[3] = {trackedAxes, carrierAxes};
    for (int cameraIndex = 0; cameraIndex < 2; ++cameraIndex)
    {
        const Halo2CameraBasis& camera = *cameras[cameraIndex];
        axes[cameraIndex][0][0] =
            camera.forward[1] * camera.up[2] -
            camera.forward[2] * camera.up[1];
        axes[cameraIndex][0][1] =
            camera.forward[2] * camera.up[0] -
            camera.forward[0] * camera.up[2];
        axes[cameraIndex][0][2] =
            camera.forward[0] * camera.up[1] -
            camera.forward[1] * camera.up[0];
        for (int axis = 0; axis < 3; ++axis)
        {
            axes[cameraIndex][1][axis] = camera.forward[axis];
            axes[cameraIndex][2][axis] = camera.up[axis];
        }
    }
    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            float value = 0.0f;
            for (int world = 0; world < 3; ++world)
                value += trackedAxes[row][world] * carrierAxes[column][world];
            if (!std::isfinite(value))
                return false;
            rotation[column * 3 + row] = value;
        }
    }
    const float worldDelta[3] = {
        carrier.position[0] - trackedCamera.position[0],
        carrier.position[1] - trackedCamera.position[1],
        carrier.position[2] - trackedCamera.position[2]};
    const float headRight[3] = {
        trackedCamera.forward[1] * trackedCamera.up[2] -
            trackedCamera.forward[2] * trackedCamera.up[1],
        trackedCamera.forward[2] * trackedCamera.up[0] -
            trackedCamera.forward[0] * trackedCamera.up[2],
        trackedCamera.forward[0] * trackedCamera.up[1] -
            trackedCamera.forward[1] * trackedCamera.up[0]};
    const float* const headAxes[3] = {
        headRight, trackedCamera.forward, trackedCamera.up};
    for (int axis = 0; axis < 3; ++axis)
    {
        translation[axis] = headAxes[axis][0] * worldDelta[0] +
            headAxes[axis][1] * worldDelta[1] +
            headAxes[axis][2] * worldDelta[2];
        if (!std::isfinite(translation[axis]))
            return false;
    }
    return true;
}

// C-H2-47: the same rigid placement, split across BOTH controllers.
//
// The two exact wrist subtrees ride their respective controllers. Only the
// proven wrist-to-root arm ancestors collapse, leaving the left hand and the
// right hand + weapon visible without mutating root, camera_control, or any
// unrelated control node. Moving every non-left node on the right controller
// was C-H2-47's defect: it explicitly included both upper arms and forearms.
inline bool Halo2PlaceFirstPersonSlotOnTwoControllers(
    float* nodes, uint32_t count, const Halo2CameraBasis& trackedCamera,
    const Halo2CameraBasis& rightCarrier, const Halo2CameraBasis& leftCarrier,
    const Halo2FirstPersonArmBinding& binding, float meshScale,
    float leftMeshScale, Halo2FirstPersonSlotCache& cache,
    Halo2FirstPersonReanchorResult& result) noexcept
{
    result = Halo2FirstPersonReanchorResult{};
    result.space = Halo2FirstPersonNodeSpace::CameraRelative;
    if (!nodes || count == 0 ||
        count > static_cast<uint32_t>(kHalo2FirstPersonNodeLimit) ||
        !binding.valid || binding.count != count ||
        binding.leftWrist < 0 ||
        static_cast<uint32_t>(binding.leftWrist) >= count ||
        binding.rightWrist < 0 ||
        static_cast<uint32_t>(binding.rightWrist) >= count ||
        !Halo2ValidateCameraBasis(trackedCamera) ||
        !Halo2ValidateCameraBasis(rightCarrier) ||
        !Halo2ValidateCameraBasis(leftCarrier) ||
        !std::isfinite(meshScale) || meshScale < 0.3f || meshScale > 3.0f ||
        !std::isfinite(leftMeshScale) || leftMeshScale < 0.3f ||
        leftMeshScale > 3.0f)
    {
        return false;
    }
    const size_t floats =
        static_cast<size_t>(count) * kHalo2FirstPersonNodeFloats;
    const size_t bytes = floats * sizeof(float);
    if (cache.valid && cache.count == count &&
        std::memcmp(nodes, cache.written, bytes) == 0)
    {
        result.fromCache = true;
    }
    else
    {
        std::memcpy(cache.original, nodes, bytes);
        cache.count = count;
        cache.valid = true;
    }
    std::memcpy(cache.written, cache.original, bytes);
    float* const work = cache.written;

    float rightRotation[9]{};
    float rightTranslation[3]{};
    float leftRotation[9]{};
    float leftTranslation[3]{};
    if (!Halo2ComputeCarrierDelta(
            trackedCamera, rightCarrier, rightRotation, rightTranslation) ||
        !Halo2ComputeCarrierDelta(
            trackedCamera, leftCarrier, leftRotation, leftTranslation))
    {
        return false;
    }

    const float zero[3]{};
    for (uint32_t index = 0; index < count; ++index)
    {
        float* const node = work + index * kHalo2FirstPersonNodeFloats;
        float* const position = node + kHalo2FirstPersonNodePositionOffset / 4;
        const bool onLeft = index < kHalo2FirstPersonPaletteCapacity &&
            (binding.leftSubtree & (1ull << index)) != 0;
        const bool onRight = index < kHalo2FirstPersonPaletteCapacity &&
            (binding.rightSubtree & (1ull << index)) != 0;
        if (!std::isfinite(node[0]))
            return false;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(position[axis]))
                return false;
        }
        if (onLeft || onRight)
        {
            const float scale = onLeft ? leftMeshScale : meshScale;
            node[0] *= scale;
            for (int axis = 0; axis < 3; ++axis)
                position[axis] *= scale;
            Halo2ReanchorFirstPersonNode(
                node, onLeft ? leftRotation : rightRotation, zero,
                onLeft ? leftTranslation : rightTranslation);
        }
        else if (index < kHalo2FirstPersonPaletteCapacity &&
                 (binding.armAncestors & (1ull << index)) != 0)
        {
            node[0] *= 0.0001f;
        }
    }
    for (size_t index = 0; index < floats; ++index)
        if (!std::isfinite(work[index]))
            return false;
    std::memcpy(nodes, work, bytes);
    result.applied = true;
    return true;
}

inline bool Halo2PlaceFirstPersonSlotOnController(
    float* nodes, uint32_t count, const Halo2CameraBasis& trackedCamera,
    const Halo2CameraBasis& carrier, float meshScale,
    Halo2FirstPersonSlotCache& cache,
    Halo2FirstPersonReanchorResult& result) noexcept
{
    result = Halo2FirstPersonReanchorResult{};
    result.space = Halo2FirstPersonNodeSpace::CameraRelative;
    if (!nodes || count == 0 ||
        count > static_cast<uint32_t>(kHalo2FirstPersonNodeLimit) ||
        !Halo2ValidateCameraBasis(trackedCamera) ||
        !Halo2ValidateCameraBasis(carrier) || !std::isfinite(meshScale) ||
        meshScale < 0.3f || meshScale > 3.0f)
    {
        return false;
    }
    const size_t floats = static_cast<size_t>(count) * kHalo2FirstPersonNodeFloats;
    const size_t bytes = floats * sizeof(float);
    if (cache.valid && cache.count == count &&
        std::memcmp(nodes, cache.written, bytes) == 0)
    {
        result.fromCache = true;
    }
    else
    {
        std::memcpy(cache.original, nodes, bytes);
        cache.count = count;
        cache.valid = true;
    }
    std::memcpy(cache.written, cache.original, bytes);
    float* const work = cache.written;

    // R = H^T C and t = H^T(Cp-Hp), with camera columns ordered
    // [right, forward, up]. This is the carrier expressed in the render
    // camera's local first-person space.
    float rotation[9]{};
    if (!Halo2BuildWorldDeltaRotation(trackedCamera, carrier, rotation))
        return false;
    float translation[3]{};
    const float worldDelta[3] = {
        carrier.position[0] - trackedCamera.position[0],
        carrier.position[1] - trackedCamera.position[1],
        carrier.position[2] - trackedCamera.position[2]};
    const float headRight[3] = {
        trackedCamera.forward[1] * trackedCamera.up[2] -
            trackedCamera.forward[2] * trackedCamera.up[1],
        trackedCamera.forward[2] * trackedCamera.up[0] -
            trackedCamera.forward[0] * trackedCamera.up[2],
        trackedCamera.forward[0] * trackedCamera.up[1] -
            trackedCamera.forward[1] * trackedCamera.up[0]};
    const float* const headAxes[3] = {
        headRight, trackedCamera.forward, trackedCamera.up};
    for (int axis = 0; axis < 3; ++axis)
        translation[axis] = headAxes[axis][0] * worldDelta[0] +
            headAxes[axis][1] * worldDelta[1] +
            headAxes[axis][2] * worldDelta[2];

    const float zero[3]{};
    for (uint32_t index = 0; index < count; ++index)
    {
        float* const node = work + index * kHalo2FirstPersonNodeFloats;
        float* const position = node + kHalo2FirstPersonNodePositionOffset / 4;
        if (!std::isfinite(node[0]))
            return false;
        node[0] *= meshScale;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(position[axis]))
                return false;
            position[axis] *= meshScale;
        }
        Halo2ReanchorFirstPersonNode(
            node, rotation, zero, translation);
    }
    for (size_t index = 0; index < floats; ++index)
        if (!std::isfinite(work[index]))
            return false;
    std::memcpy(nodes, work, bytes);
    result.applied = true;
    return true;
}

// Re-anchors one interpolator slot in place: every node is moved rigidly
// from the tick camera to the pass's frame camera (rotation only for
// camera-relative nodes), then - for a pass that draws from a stale camera -
// from the correct eye camera to the viewing one. Returns false, with the
// engine's nodes untouched, when the inputs or the node space cannot be
// trusted. Allocation-free; the cache is the caller's static storage.
inline bool Halo2ReanchorFirstPersonSlot(
    float* nodes, uint32_t count, const Halo2CameraBasis& tick,
    const Halo2FirstPersonPassCameras& pass, Halo2FirstPersonSlotCache& cache,
    Halo2FirstPersonReanchorResult& result) noexcept
{
    result = Halo2FirstPersonReanchorResult{};
    if (!nodes || count == 0 ||
        count > static_cast<uint32_t>(kHalo2FirstPersonNodeLimit) ||
        !pass.frameValid)
    {
        return false;
    }
    const size_t floats = static_cast<size_t>(count) * kHalo2FirstPersonNodeFloats;
    const size_t bytes = floats * sizeof(float);
    // Source: the engine's fresh output, or the cached engine values when
    // the engine handed back exactly what the mod wrote last time.
    if (cache.valid && cache.count == count &&
        std::memcmp(nodes, cache.written, bytes) == 0)
    {
        result.fromCache = true;
    }
    else
    {
        std::memcpy(cache.original, nodes, bytes);
        cache.count = count;
        cache.valid = true;
    }
    std::memcpy(cache.written, cache.original, bytes);
    float* const work = cache.written;
    result.space = Halo2ClassifyFirstPersonNodeSpace(
        work + kHalo2FirstPersonNodePositionOffset / 4, tick.position);
    if (result.space == Halo2FirstPersonNodeSpace::Unknown)
        return false;
    float rotation[9];
    if (!Halo2BuildWorldDeltaRotation(tick, pass.frame, rotation))
        return false;
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const bool world = result.space == Halo2FirstPersonNodeSpace::World;
    for (uint32_t index = 0; index < count; ++index)
    {
        Halo2ReanchorFirstPersonNode(
            work + index * kHalo2FirstPersonNodeFloats, rotation,
            world ? tick.position : zero, world ? pass.frame.position : zero);
    }
    if (pass.compensate && world)
    {
        float compensation[9];
        if (!Halo2BuildWorldDeltaRotation(pass.correct, pass.viewing, compensation))
            return false;
        for (uint32_t index = 0; index < count; ++index)
        {
            Halo2ReanchorFirstPersonNode(
                work + index * kHalo2FirstPersonNodeFloats, compensation,
                pass.correct.position, pass.viewing.position);
        }
        result.compensated = true;
    }
    for (size_t index = 0; index < floats; ++index)
    {
        if (!std::isfinite(work[index]))
            return false;
    }
    std::memcpy(nodes, work, bytes);
    result.applied = true;
    return true;
}
