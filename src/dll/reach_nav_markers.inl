// REACH: NAVIGATION MARKERS ARE PROJECTED WITH THE EYE'S FIELD OF VIEW -- the runtime half. The pure logic is in
// src/common/reach_nav_marker_logic.h; the write-up is docs/REACH-NAV-MARKERS-2026-10-07.md.
//
// In short. Reach's HUD projects a world point to the screen with a camera it BUILDS for the purpose from the
// observer camera, inside the same pass that draws the HUD over each eye (one function, eleven direct callers in
// the image: read statically, see the write-up). The observer-camera hook already hands that camera the eye's
// position, forward and up at its call site. Here the same call site also hands it the eye's vertical
// field of view, so the game builds the projection the eye was rasterised with.
//
// Measured on one marker (the King of the Hill hill marker, on foot, in a simulated OpenXR runtime):
// with the game's own field of view the marker is drawn 2.155 times too far from the eye's axis (57.89 against
// 100.00 deg), measured at 9.6 to 22 deg; with the eye's, it is on its world point's pixel out to 31 deg. With
// the head still, sweeping the gun 45 deg either way moves the marker in neither case. Not confirmed for any
// other marker type, nor against the marker's target seen in the picture (the write-up, "Not proven").
//
// It rides a hook that already exists; no new hook, and nothing but the marker projection's own stack camera is
// written. A refusal leaves that call the game's own field of view, counted by reason.

constexpr size_t kReachNavFovCount = static_cast<size_t>(reach_nav_markers::Fov::Count);
struct ReachNavMarkersRuntime
{
    std::atomic<bool> installed{false};  // the observer-camera hook this rides is active for this load
    // The window the report drains: every marker projection's camera lands in exactly one of these.
    std::atomic<uint32_t> fov[kReachNavFovCount]{};
    // The last pair seen at the call site: the game's own vertical field of view and the eye's (radians).
    std::atomic<float> lastGameFov{0.0f}, lastEyeFov{0.0f};
    // The report's thread only.
    int reportedSwitch = -1;
    bool reportedFirstUse = false, reportedFirstRefusal = false;
} g_reachNavMarkers;

// The render thread, inside the observer-camera hook at the marker projection's call site, after the game built
// `camera` and the hook gave it the eye's position, forward and up. `eyeCompact` is the camera this eye was
// rasterised with. One float, in the caller's stack camera; no allocation, no logging, no lock.
void ReachNavMarkerEyeFov(unsigned char* camera, const unsigned char* eyeCompact) noexcept
{
    using namespace reach_nav_markers;
    auto& feature = g_reachNavMarkers;
    float gameFov = 0.0f, eyeFov = 0.0f, use = 0.0f;
    memcpy(&gameFov, camera + kCameraVerticalFovOffset, sizeof(gameFov));
    memcpy(&eyeFov, eyeCompact + kCameraVerticalFovOffset, sizeof(eyeFov));
    const Fov verdict = feature.installed.load(std::memory_order_acquire)
        ? EyeFov(g_config.nav_markers_on_target, gameFov, eyeFov, use)
        : Fov::SwitchOff;
    if (verdict == Fov::Used)
    {
        memcpy(camera + kCameraVerticalFovOffset, &use, sizeof(use));
        feature.lastGameFov.store(gameFov, std::memory_order_relaxed);
        feature.lastEyeFov.store(eyeFov, std::memory_order_relaxed);
    }
    feature.fov[static_cast<size_t>(verdict)].fetch_add(1, std::memory_order_relaxed);
}

// Cold, on the title's install thread, once the observer-camera hook's fate is known.
void InstallReachNavMarkers(bool observerCameraHookActive, uintptr_t chudSiteReturnRva)
{
    auto& feature = g_reachNavMarkers;
    for (auto& count : feature.fov)
        count.store(0, std::memory_order_relaxed);
    feature.reportedSwitch = -1;
    feature.reportedFirstUse = feature.reportedFirstRefusal = false;
    feature.installed.store(observerCameraHookActive, std::memory_order_release);
    if (!observerCameraHookActive)
    {
        LOG("NAV MARKERS Reach: StockFallback: the observer-camera hook is not installed, so the game's own marker "
            "projection stands (its flat-screen field of view: markers are drawn too far from the eye's axis); "
            "nothing else is affected");
        return;
    }
    LOG("NAV MARKERS Reach: Installed: the HUD's marker projection (the observer-camera hook's call site returning "
        "to haloreach.dll+0x%llX) is given the eye's vertical field of view (camera +0x%X) as well as its position, "
        "forward and up; switch=%d",
        static_cast<unsigned long long>(chudSiteReturnRva), reach_nav_markers::kCameraVerticalFovOffset,
        g_config.nav_markers_on_target ? 1 : 0);
}
void RemoveReachNavMarkers() { g_reachNavMarkers.installed.store(false, std::memory_order_release); }

const char* ReachNavFovName(size_t use)
{
    return reach_nav_markers::FovName(static_cast<reach_nav_markers::Fov>(use));
}
// The worker's two-second report. Every build: one line when the switch changes, and one the first time a
// marker is projected with the eye's field of view (or first refused) after an install. Dev builds: the window.
void ReportReachNavMarkers()
{
    using reach_nav_markers::Fov;
    auto& feature = g_reachNavMarkers;
    if (!feature.installed.load(std::memory_order_acquire))
        return;
    const int switchNow = g_config.nav_markers_on_target ? 1 : 0;
    if (feature.reportedSwitch != switchNow)
    {
        if (feature.reportedSwitch >= 0)
            LOG("NAV MARKERS Reach: switch %s: markers are now projected with %s", switchNow ? "on" : "off",
                switchNow ? "the eye's own field of view" : "the game's flat-screen field of view, as before");
        feature.reportedSwitch = switchNow;
    }
    uint32_t uses[kReachNavFovCount]{}, refused = 0;
    for (size_t i = 0; i < kReachNavFovCount; ++i)
    {
        uses[i] = feature.fov[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(Fov::Used))
            refused += uses[i];
    }
    const uint32_t used = uses[static_cast<size_t>(Fov::Used)];
    if (!used && !refused)
        return;
    char reasons[256];
    const int length = ReachHeadRelativeReasons(reasons, sizeof(reasons), uses, kReachNavFovCount,
                                                static_cast<size_t>(Fov::Used), &ReachNavFovName);
    const float gameFov = feature.lastGameFov.load(std::memory_order_relaxed);
    const float eyeFov = feature.lastEyeFov.load(std::memory_order_relaxed);
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("NAV MARKERS Reach: markers are projected with the eye's vertical field of view (%.1f deg; the game's "
            "own is %.1f deg, which draws a marker %.2f times too far from the eye's axis)",
            eyeFov * reach_nav_markers::kRadToDeg, gameFov * reach_nav_markers::kRadToDeg,
            reach_nav_markers::Magnification(gameFov, eyeFov));
    }
    if (!used && refused && !feature.reportedFirstRefusal)
    {
        feature.reportedFirstRefusal = true;
        LOG("NAV MARKERS Reach: the game's own marker projection for now: %s", length ? reasons : "no reason");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    // The pair of fields of view is the last one USED. With none used since the install there is no pair, and
    // printing the zeroes would read as a measured magnification of 1.000.
    if (eyeFov > 0.0f && gameFov > 0.0f)
        LOG("NAV MARKERS Reach (dev): %u marker projections with the eye's field of view (last used: eye %.2f "
            "deg, game %.2f deg, stock magnification %.3f); refused %u: %s; switch=%d",
            used, eyeFov * reach_nav_markers::kRadToDeg, gameFov * reach_nav_markers::kRadToDeg,
            reach_nav_markers::Magnification(gameFov, eyeFov), refused, length ? reasons : "none", switchNow);
    else
        LOG("NAV MARKERS Reach (dev): %u marker projections with the eye's field of view (none used since the "
            "install: no field-of-view pair to show); refused %u: %s; switch=%d",
            used, refused, length ? reasons : "none", switchNow);
#endif
}
