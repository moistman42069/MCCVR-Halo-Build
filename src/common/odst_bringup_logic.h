#pragma once

#include <cmath>
#include <cstdint>

constexpr uint64_t kOdstCameraFreshMs = 500;
constexpr uint64_t kOdstCameraSoftTimeoutMs = 750;
constexpr uint64_t kOdstCameraHardTimeoutMs = 5000;
constexpr uint64_t kOdstCameraStableMs = 1000;
// ODST's camera tail boolean toggles roughly every 100ms during ordinary play
// (observed live: tail=[0,1,0,0,0,0,0,0] alternating with
// tail=[0,1,0,0,0,0,0,1] about ten times a second). A single not-fresh poll is
// therefore normal engine behaviour, not a lost camera. Gaps shorter than this
// do not restart the stability interval; anything longer is a genuine loss - a
// pause, a level unload or a title exit - and does.
constexpr uint64_t kOdstCameraFreshGapToleranceMs = 350;
// Native-pause REINSTALL debounce (stage 1). A pause returns to the exact camera
// the hooks were just removed from -- not a fresh level -- so the reinstall gate
// does not need the full fresh-level stability interval. The real arm-safety guard
// is unchanged: after reinstall the render thread still holds the accepted
// one-second kOdstCameraStableMs fresh-ordinary-camera interval (stage 2) before
// stereo arms. Shortening only stage 1 cuts the post-pause VR drop-out without
// letting the mod arm into a still-transitional pause-exit camera.
constexpr uint64_t kOdstPauseRearmStableMs = 250;
constexpr float kOdstFirstPersonBlendMin = 0.95f;

struct OdstFpSkeletonLayout
{
    int rightShoulder = 2;
    int rightElbow = 4;
    int rightWrist = 6;
    int leftShoulder = 1;
    int leftElbow = 3;
    int leftWrist = 5;
    int cameraControl = -1;
    uint64_t rightHandAndWeaponDescendants = 0;
    uint64_t leftHandDescendants = 0;
};

// H3ODSTEK proves that the ODST FP body occupies nodes 0..36. Sampled
// first-person animation graphs append the held weapon at node 37 and leave
// camera_control as the final root child.
inline bool ComputeOdstFpSkeletonLayout(
    int combinedNodeCount, OdstFpSkeletonLayout& out)
{
    // At least one weapon node (37) must precede camera_control. The shared
    // visible-palette solver is intentionally bounded to its 64-record banks.
    if (combinedNodeCount < 39 || combinedNodeCount > 64)
        return false;

    OdstFpSkeletonLayout layout{};
    layout.cameraControl = combinedNodeCount - 1;
    const int leftNodes[] = {
        5, 7, 8, 9, 10, 11, 17, 18, 19, 20, 21, 27, 28, 29, 30, 31};
    const int rightNodes[] = {
        6, 12, 13, 14, 15, 16, 22, 23, 24, 25, 26, 32, 33, 34, 35, 36};
    for (int node : leftNodes)
        layout.leftHandDescendants |= uint64_t{1} << node;
    for (int node : rightNodes)
        layout.rightHandAndWeaponDescendants |= uint64_t{1} << node;
    for (int node = 37; node < layout.cameraControl; ++node)
        layout.rightHandAndWeaponDescendants |= uint64_t{1} << node;
    out = layout;
    return true;
}

struct OdstHalo3FovMatch
{
    float compactVerticalInput = 0.0f;
    float compactReferenceInput = 0.0f;
    float projectionX = 0.0f;
    float projectionY = 0.0f;
};

// Halo 3's headset-confirmed path feeds tan(half-FOV) for both compact camera
// scalars, then writes their reciprocals into the final projection. ODST's
// builder is instruction-identical, but its two source fields have different
// stock semantics. This private experiment matches Halo 3's numeric inputs as
// a pair instead of mixing a widened world FOV with ODST's stock FP reference.
inline bool ComputeOdstHalo3FovMatch(
    float halfX, float halfY, OdstHalo3FovMatch& out)
{
    if (!std::isfinite(halfX) || !std::isfinite(halfY) ||
        halfX <= 0.01f || halfX >= 1.55f ||
        halfY <= 0.01f || halfY >= 1.55f)
        return false;
    const float tanX = std::tan(halfX);
    const float tanY = std::tan(halfY);
    if (!std::isfinite(tanX) || !std::isfinite(tanY) ||
        tanX <= 0.01f || tanY <= 0.01f)
        return false;
    out.compactVerticalInput = tanX;
    out.compactReferenceInput = tanY;
    out.projectionX = 1.0f / tanX;
    out.projectionY = 1.0f / tanY;
    return true;
}

struct OdstHalo3LookAngles
{
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

// The headset-confirmed Halo 3 camera owns pitch and roll absolutely. Only yaw
// is relative to the recentered game heading; no stock pitch/roll is an input.
inline bool ComputeOdstHalo3LookAngles(
    float gameYawReference, float headYawReference, float headYaw,
    float headPitch, float headRoll, float yawSign, float pitchSign,
    float pitchTrim, OdstHalo3LookAngles& out)
{
    const float values[] = {
        gameYawReference, headYawReference, headYaw, headPitch, headRoll,
        yawSign, pitchSign, pitchTrim};
    for (float value : values)
        if (!std::isfinite(value))
            return false;
    float yawDelta = headYaw - headYawReference;
    while (yawDelta > 3.14159265f)
        yawDelta -= 6.2831853f;
    while (yawDelta < -3.14159265f)
        yawDelta += 6.2831853f;
    out.yaw = gameYawReference + yawSign * yawDelta;
    const float pitch = pitchSign * headPitch + pitchTrim;
    out.pitch = pitch < -1.5f ? -1.5f : (pitch > 1.5f ? 1.5f : pitch);
    out.roll = headRoll;
    return true;
}

enum class OdstHeartbeatAction
{
    None,
    LevelUnloaded,
    NoFirstHeartbeat,
};

enum class OdstStereoFrameAction
{
    RenderStockWithoutCapture,
    RenderStereoAndValidate,
};

inline OdstStereoFrameAction EvaluateOdstStereoFrame(bool runtimeShouldRender)
{
    return runtimeShouldRender
        ? OdstStereoFrameAction::RenderStereoAndValidate
        : OdstStereoFrameAction::RenderStockWithoutCapture;
}

inline bool OdstCameraOnlyScopeRequired(
    bool privateBuildEnabled, bool adapterReportsOdst,
    bool runtimeStateOwned)
{
    return runtimeStateOwned ||
        (privateBuildEnabled && adapterReportsOdst);
}

inline bool OdstManualArmEligible(
    bool cameraStable, bool headTracking, bool stereoEnabled,
    bool teardownRequested)
{
    return cameraStable && headTracking && stereoEnabled &&
        !teardownRequested;
}

// Ordinary native pause is a presentation suspension, not a title boundary.
// Keep the verified hook core installed while the compositor shows its stable
// head-locked screen. Title exit, level unload, and heartbeat loss retain
// their independent teardown paths.
inline bool OdstNativePauseSuspendsPrivateCore(
    bool pauseKnown, bool nativePaused)
{
    return pauseKnown && nativePaused;
}

inline bool OdstPrivateCameraMutationAllowed(
    bool armed, bool teardownRequested, bool headTracking,
    bool pausePresentationTarget)
{
    return armed && !teardownRequested && headTracking &&
        !pausePresentationTarget;
}

inline bool OdstVrOwnsLookStick(bool cameraOnlyContext, bool headTracking)
{
    return cameraOnlyContext && headTracking;
}

// Motion-controller weapon aim is the first ODST gameplay capability layered on
// top of the camera-only core. It is intentionally narrower than full shared
// gameplay: it only steers the game's internal aim heading (bullets, target
// logic, and the floating reticle) through the injected right stick while the
// HMD keeps owning the rendered view. Requires the camera-only context owned,
// hooks armed, head tracking on, and no teardown in progress. Movement mapping
// and every other shared transform stay stock for ODST regardless of this.
inline bool OdstMotionAimEligible(
    bool cameraOnlyContext, bool armed, bool headTracking,
    bool teardownRequested)
{
    return cameraOnlyContext && armed && headTracking && !teardownRequested;
}

// First-person render classification only. On foot reports 1.0 and camera
// transitions can pass near 0.998; the settled vehicle camera is blend 0 and
// receives aim through the separate Halo 3 active-camera ownership rule.
inline bool OdstFirstPersonControlBlend(float fpBlend)
{
    return std::isfinite(fpBlend) && fpBlend >= kOdstFirstPersonBlendMin;
}

// The ODST layout locator is a cold Present-thread path. It may begin as soon
// as this title owns a fresh camera heartbeat, matching Halo 3; it does not
// wait for stereo arming. Public/foreign/teardown states remain fail-closed.
inline bool OdstHudLayoutEligible(
    bool privateBuildEnabled, bool cameraOnlyContext, bool installed,
    bool cameraFresh, bool teardownRequested, bool nativePaused)
{
    return privateBuildEnabled && cameraOnlyContext && installed &&
        cameraFresh && !teardownRequested && !nativePaused;
}

inline bool OdstMustClearForeignPause(
    bool cameraOnlyContext, bool pauseTarget, bool pausePresentation)
{
    return cameraOnlyContext && (pauseTarget || pausePresentation);
}

// Camera policy. A live render frame is NEVER a teardown by itself. Slot 0 may
// use either the ordinary internal scene-color path (first person/vehicles) or
// ODST's direct-to-backbuffer path (the third-person death camera). Both are
// stereo-redirectable after their camera layout has passed the same single-user
// and nested-source checks.
inline bool OdstShouldStereoRedirect(
    bool ownsPrimarySlot, bool singleUserTailValid,
    bool nestedSourceMatches, bool compactIsStereoRedirectable)
{
    return ownsPrimarySlot && singleUserTailValid &&
        nestedSourceMatches && compactIsStereoRedirectable;
}

// The camera-copy path tears down only when our slot-0 view object no longer
// matches the single-user layout -- a genuine level unload/transition. An
// active third-person camera in a still-valid slot-0 object is NOT a teardown:
// it renders stock and keeps the core armed for automatic 3D recovery.
inline bool OdstCamCopyRequestsTeardown(
    bool armed, bool ownsPrimarySlot, bool singleUserTailValid,
    bool pausePresentationTarget)
{
    return armed && ownsPrimarySlot && !singleUserTailValid &&
        !pausePresentationTarget;
}

inline bool OdstNestedSourceIsCompatible(
    uintptr_t nestedSource, uintptr_t expectedSource)
{
    return nestedSource == 0 || nestedSource == expectedSource;
}

inline bool OdstInactiveCameraSlotsAreSafe(
    bool slot1Active, bool slot2Active, bool slot3Active)
{
    return !slot1Active && !slot2Active && !slot3Active;
}

inline OdstHeartbeatAction EvaluateOdstHeartbeat(
    uint64_t now, uint64_t installedAt, uint64_t lastCamera,
    bool sawCamera, bool cameraReady)
{
    if (!installedAt || now < installedAt)
        return OdstHeartbeatAction::None;
    if (!sawCamera)
    {
        const uint64_t installedAge = now - installedAt;
        if (installedAge > kOdstCameraSoftTimeoutMs && !cameraReady)
            return OdstHeartbeatAction::LevelUnloaded;
        if (installedAge > kOdstCameraHardTimeoutMs)
            return OdstHeartbeatAction::NoFirstHeartbeat;
        return OdstHeartbeatAction::None;
    }
    if (!lastCamera || now < lastCamera)
        return OdstHeartbeatAction::None;
    const uint64_t cameraAge = now - lastCamera;
    if (cameraAge > kOdstCameraSoftTimeoutMs &&
        (!cameraReady || cameraAge > kOdstCameraHardTimeoutMs))
        return OdstHeartbeatAction::LevelUnloaded;
    return OdstHeartbeatAction::None;
}

class OdstFreshCameraDebounce
{
public:
    bool Update(uint64_t now, bool cameraFresh)
    {
        if (cameraFresh)
        {
            m_lastFreshMs = now;
            if (!m_freshSince)
                m_freshSince = now;
        }
        else
        {
            // Restarting on every not-fresh poll meant the interval could
            // never complete while the tail boolean toggled, so ODST armed
            // only when it happened to catch a lucky quiet gap - slow, and
            // frequently never at all after a quick pause and unpause.
            const bool briefGap = m_lastFreshMs != 0 &&
                now >= m_lastFreshMs &&
                now - m_lastFreshMs <= kOdstCameraFreshGapToleranceMs;
            if (!briefGap)
            {
                m_freshSince = 0;
                m_lastFreshMs = 0;
                return false;
            }
        }
        return m_freshSince != 0 && now >= m_freshSince &&
            now - m_freshSince > kOdstCameraStableMs;
    }

    void Reset()
    {
        m_freshSince = 0;
        m_lastFreshMs = 0;
    }

private:
    uint64_t m_freshSince = 0;
    uint64_t m_lastFreshMs = 0;
};

// A level-unload fallback must not accept the same stale camera bytes as a
// fresh level. Rearm only after observing the camera array inactive and then
// active again, or after the title DLL has genuinely left the process.
class OdstCameraRearmGate
{
public:
    void BlockUntilReload(bool cameraActiveNow)
    {
        m_blocked = true;
        m_sawInactive = !cameraActiveNow;
        m_requireTitleExit = false;
    }

    void BlockUntilTitleExit()
    {
        m_blocked = true;
        m_sawInactive = false;
        m_requireTitleExit = true;
    }

    void Observe(bool titleActive, bool cameraActive)
    {
        if (!titleActive)
        {
            m_blocked = false;
            m_sawInactive = false;
            m_requireTitleExit = false;
            return;
        }
        if (!m_blocked)
            return;
        if (m_requireTitleExit)
            return;
        if (!cameraActive)
            m_sawInactive = true;
        else if (m_sawInactive)
        {
            m_blocked = false;
            m_sawInactive = false;
        }
    }

    bool CanAttemptInstall() const { return !m_blocked; }
    bool IsBlocked() const { return m_blocked; }

private:
    bool m_blocked = false;
    bool m_sawInactive = false;
    bool m_requireTitleExit = false;
};

// Native pause is a safe pre-shutdown boundary for the private camera hooks.
// After removing them, do not reinstall on a stale pause-menu camera: require
// the native pause byte to clear, the ordinary camera to be seen live at least
// once, and a short settle window (kOdstPauseRearmStableMs) to elapse. The gate
// is deliberately flicker-tolerant: with the copy hook removed the live camera
// array is sampled directly and reads ready/not-ready frame-to-frame, so ONLY a
// genuine re-pause restarts the window -- a momentary not-ready sample does not
// (headset log 2026-07-24: a continuous-ready reset stalled the rearm for tens
// of seconds). Stage 2's fresh-camera arm debounce still proves real stability
// before stereo re-engages, so a slightly-early reinstall cannot arm on garbage.
class OdstPauseRearmGate
{
public:
    void Block()
    {
        m_blocked = true;
        m_pauseClearedSince = 0;
        m_readyObserved = false;
    }

    void Observe(uint64_t now, bool titleActive, bool nativePaused,
                 bool cameraActive)
    {
        if (!titleActive)
        {
            m_blocked = false;
            m_pauseClearedSince = 0;
            m_readyObserved = false;
            return;
        }
        if (!m_blocked)
            return;
        // A genuine (re-)pause is the ONLY event that restarts the settle
        // window. A single not-ready sample from the live, un-hooked camera
        // array is flicker, not a re-pause, and must not reset progress.
        if (nativePaused)
        {
            m_pauseClearedSince = 0;
            m_readyObserved = false;
            return;
        }
        // Native pause has cleared: start the settle window on the first tick,
        // and latch that the ordinary camera has been seen live at least once.
        if (!m_pauseClearedSince)
            m_pauseClearedSince = now;
        if (cameraActive)
            m_readyObserved = true;
        if (m_readyObserved && now >= m_pauseClearedSince &&
            now - m_pauseClearedSince > kOdstPauseRearmStableMs)
        {
            m_blocked = false;
            m_pauseClearedSince = 0;
            m_readyObserved = false;
        }
    }

    bool CanAttemptInstall() const { return !m_blocked; }
    bool IsBlocked() const { return m_blocked; }

private:
    bool m_blocked = false;
    uint64_t m_pauseClearedSince = 0;
    bool m_readyObserved = false;
};
