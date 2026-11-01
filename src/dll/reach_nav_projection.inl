// REACH NAVIGATION MARKERS: THE MARKER PROJECTION ITSELF -- a hook on the game's one world-point-to-HUD function
// (HREK reach_tag_test.exe +0x92D980, chud_draw.cpp; retail +0x2E1430). The sites, their proof and the pure logic
// are in src/common/reach_nav_marker_logic.h; the write-up is docs/REACH-NAV-MARKERS-2026-10-07.md.
//
// THE OFF-SCREEN ARROW (the write-up's part 2). A caller that wants an off-screen indicator has the game bring a
// far-off point onto a ring, laid out about each eye's own axis. Measured in a game (simulated runtime): 41.6 degrees
// out sideways and 30.0 up and down for a 100 degree eye, the two eyes agreeing, which puts a sideways arrow
// past the 40 degrees the other eye sees toward the nose: in one eye only. With the arrow switch on, for a point
// outside a ring about the HEAD's axis (at most 30 degrees out), the detour hands the game a stand-in for that
// one call: the point's direction brought onto the head's ring, at the point's own distance. Both eyes project
// the same stand-in; the game draws its own arrow on it. When the game returns, the record's point is put back
// and its off-screen value set by the game's own formula.
//   * WHOSE CALL: the navpoint list's (objective markers and marked players, by a static read; the hill
//     marker, in a game) and the object-anchored markers' wrapper call. Each call site is proved at install and
//     the detour compares its own return address with the addresses decoded then; when the object markers'
//     site is not found, only the navpoint list's acts. The projection's other callers (aim points, damage
//     indicators, name tags and the rest: reach_nav_marker_logic.h) keep the game's own ring and are counted
//     by reason;
//   * what is written: twelve bytes of the caller's record before the call and the same twelve after it (the
//     game's own point again, in a __finally), plus the record's off-screen float. Nothing else;
//   * every projection lands in exactly one counted verdict (reach_nav_markers::Ring); only Moved writes.
//
// THE WITNESS (dev builds, with nav_marker_witness = 1). It READS. For a point the game drew on the point
// itself: the game's pixel against the pixel the eye's own picture shows that point at, by the projection matrix
// the eye was rasterised with. For an arrow (the game's own ring, or a stand-in): the direction each eye drew it
// in, and the difference between the two eyes in arc-minutes, across and up.

// void project(record*, int user, bool ring, bool behind, bool worldPitch). tools/audit_caller_area.py
// haloreach.dll 2E1430:9:v is clean: no caller slot past the fifth argument, eleven native callers, none keeps a
// volatile register across the call or reads a return value.
using ReachNavProjectionFn = void (__fastcall*)(void*, int, uint8_t, uint8_t, uint8_t);
constexpr size_t kReachNavRingCount = static_cast<size_t>(reach_nav_markers::Ring::Count);
constexpr size_t kReachNavWitnessCount = static_cast<size_t>(reach_nav_markers::Witness::Count);
#if HALOMCCVR_DEV_DIAGNOSTICS
constexpr int kReachNavCallerCapacity = 16;  // the retail image has eleven
#endif
struct ReachNavProjectionRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;                 // ReachNavProjectionFn
    reach_nav_markers::ProjectionSites sites;  // decoded from the game's own instructions at install
    // Where the navpoint list's call of the projection returns: decoded at install (never a constant), before
    // the hook is enabled. The stand-in acts for that caller alone.
    uintptr_t navpointReturn = 0;
    // Where the object markers' call of the projection returns (the anchor producer's wrapper); 0 = not found
    // on this image, and those markers keep the game's own ring. Decoded at install, before the hook is enabled.
    uintptr_t objectMarkerReturn = 0;
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    // The window the report drains: every projection lands in exactly one of these. The render thread.
    std::atomic<uint32_t> ring[kReachNavRingCount]{};
    std::atomic<uint32_t> restoreFaults{0};
    // The last point brought onto the ring: its own angles off the head's axis, how far out, and the ring.
    std::atomic<float> lastYawDeg{0.0f}, lastPitchDeg{0.0f}, lastRatio{0.0f}, lastDistance{0.0f};
    std::atomic<float> ringYawDeg{0.0f}, ringPitchDeg{0.0f}, fieldYawDeg{0.0f}, fieldPitchDeg{0.0f};
    // The report's thread only.
    bool reportedFirstMove = false;
#if HALOMCCVR_DEV_DIAGNOSTICS
    std::atomic<uint32_t> seen[kReachNavWitnessCount]{};
    std::atomic<uint32_t> compared[2]{};                 // per eye
    std::atomic<float> errorSum[2]{}, errorMax[2]{};     // pixels of the raster
    // The last compared projection of each eye.
    std::atomic<float> lastOffAxisDeg[2]{}, lastTargetDistance[2]{};
    std::atomic<float> lastGame[2][2]{}, lastOwn[2][2]{};
    std::atomic<float> lastOffscreen[2]{};               // the last off-screen value the game left, per eye
    std::atomic<float> rasterWidth{0.0f}, rasterHeight{0.0f};
    // THE ARROW: a projection that ended with an off-screen value (the game's own ring, or a stand-in). Where
    // each eye drew it, as a direction, and whether the two eyes agree. Index 0 = left eye, 1 = right.
    std::atomic<uint32_t> arrowPairs{0}, arrowStandIns{0};
    std::atomic<float> arrowTargetDeg{0.0f};                   // the target, off the HEAD's axis
    std::atomic<float> arrowPixel[2][2]{}, arrowAxisDeg[2]{};  // its pixel; degrees from that eye's own axis
    std::atomic<float> arrowHeadDeg[2][2]{};                   // across / up in the head's frame, degrees
    std::atomic<float> arrowDiff[2]{}, arrowDiffMax[2]{};      // left minus right, arc-minutes: across, up
    std::atomic<float> arrowNatural{0.0f};  // across, arc-minutes, for a real point at the target's distance
    // Off the head's axis, this window: markers drawn on their point, and arrows.
    std::atomic<float> pointMinDeg{1.0e9f}, pointMaxDeg{-1.0f}, arrowMinDeg{1.0e9f}, arrowMaxDeg{-1.0f};
    std::atomic<uint32_t> pointSamples{0}, arrowSamples{0};
    // WHO CALLS (dev): every projection counted by the call site it returns to, with and without a ring asked
    // for. Index kReachNavCallerCapacity = a caller that is none of the image's direct call sites.
    uintptr_t callerReturns[kReachNavCallerCapacity]{};  // written at install, before the hook is enabled
    int callerCount = 0, callerFound = 0;
    std::atomic<uint32_t> callerRing[kReachNavCallerCapacity + 1]{}, callerPlain[kReachNavCallerCapacity + 1]{};
#endif
} g_reachNavProjection;
#if HALOMCCVR_DEV_DIAGNOSTICS
// The other eye's arrow of the same frame, kept between the two eye passes. Both passes run on the render
// thread in one call stack, so this needs no lock.
struct ReachNavArrowEye
{
    bool valid = false;
    uint32_t eye = 0;
    uint64_t serial = 0;
    float point[3]{};
    float headAcross = 0.0f, headUp = 0.0f;  // radians, in the head's frame
    float eyePosition[3]{};
};
thread_local ReachNavArrowEye g_reachNavArrowPrevious;
#endif

// What one call keeps between the two halves of the detour. Plain data, on the detour's stack.
struct ReachNavRingCall
{
    bool moved = false;
    float point[3]{};      // the game's own point, to put back
    float offscreen = 0.0f;
};
// The head's camera of this frame: the one both eyes were built from (the outer hook's committed centre).
void ReachNavHeadCamera(reach_nav_markers::Camera& head) noexcept
{
    const unsigned char* center = g_reachOwnerScope.headCenter;
    memcpy(head.position, center + 0x00, sizeof(head.position));
    memcpy(head.forward, center + kReachCompactCameraForwardOffset, sizeof(head.forward));
    memcpy(head.up, center + 0x18, sizeof(head.up));
    memcpy(&head.verticalFov, center + reach_nav_markers::kCameraVerticalFovOffset, sizeof(head.verticalFov));
}
// The head's ring from the two eyes' fields as OpenXR gave them for this frame. False = no usable field.
bool ReachNavHeadRing(float fraction, reach_nav_markers::RingField& ring, reach_nav_markers::RingField& shared) noexcept
{
    reach_nav_markers::EyeField eyes[2];
    for (int eye = 0; eye < 2; ++eye)
    {
        const ReachEyeCullFrustum& frustum = g_reachOwnerScope.eyes[eye].frustum;
        eyes[eye].angleLeft = frustum.angleLeft;
        eyes[eye].angleRight = frustum.angleRight;
        eyes[eye].angleUp = frustum.angleUp;
        eyes[eye].angleDown = frustum.angleDown;
        for (int i = 0; i < 4; ++i)
            eyes[eye].orientation[i] = frustum.relativeOrientation[i];
    }
    if (!reach_nav_markers::SharedField(eyes, shared))
        return false;
    ring = reach_nav_markers::HeadRing(shared, fraction);
    return true;
}

// The render thread, before the game projects `record`. No allocation, no logging, no lock. Moved is the only
// verdict that writes: the record's point, after the game's own was copied into `call`. `fromNavpoints`: the
// game's call returns to the navpoint list's call site.
__declspec(noinline) reach_nav_markers::Ring ReachNavRingBefore(void* record, int user, uint8_t wantsRing,
                                                                uint8_t worldPitch, bool fromNavpoints,
                                                                ReachNavRingCall& call) noexcept
{
    using namespace reach_nav_markers;
    auto& feature = g_reachNavProjection;
    Ring what = Ring::Fault;
    __try
    {
        // Its own switch, and part 1's as well: the stand-in is a point in the head's space, and without the
        // eye's field of view the game would project it 2.155 times too far out like any other point.
        const float fraction = g_config.nav_marker_ring;
        const bool switchOn = g_config.nav_marker_arrow_in_view && g_config.nav_markers_on_target &&
                              fraction > 0.0f && fraction <= 1.0f;
        const ReachFpCameraEyeScope& scope = g_reachFpCameraEyeScope;
        const bool eyeScope = ReachOwnsHudStereoTransaction() && scope.generation == feature.generation &&
                              g_reachOwnerScope.active;
        auto* bytes = static_cast<uint8_t*>(record);
        const bool valid = bytes[0] != 0;
        Camera head;
        RingField ring, shared;
        bool haveField = false;
        float point[3]{};
        if (switchOn && eyeScope && user == 0 && valid && wantsRing && fromNavpoints && !worldPitch)
        {
            memcpy(point, bytes + feature.sites.recordPoint, sizeof(point));
            ReachNavHeadCamera(head);
            haveField = ReachNavHeadRing(fraction, ring, shared);
        }
        RingMove move;
        what = RingPoint(switchOn, eyeScope, user, valid, wantsRing != 0, fromNavpoints, worldPitch != 0, head,
                         haveField, ring, point, move);
        if (what == Ring::Moved)
        {
            memcpy(call.point, point, sizeof(call.point));
            call.offscreen = move.offscreen;
            call.moved = true;  // before the write: whatever happens next, the game's own point goes back
            memcpy(bytes + feature.sites.recordPoint, move.point, sizeof(move.point));
            feature.lastYawDeg.store(move.yaw * kRadToDeg, std::memory_order_relaxed);
            feature.lastPitchDeg.store(move.pitch * kRadToDeg, std::memory_order_relaxed);
            feature.lastRatio.store(move.ratio, std::memory_order_relaxed);
            float distance = 0.0f, yaw = 0.0f, pitch = 0.0f;
            (void)RingAngles(head, point, yaw, pitch, &distance);
            feature.lastDistance.store(distance, std::memory_order_relaxed);
            feature.ringYawDeg.store(ring.halfYaw * kRadToDeg, std::memory_order_relaxed);
            feature.ringPitchDeg.store(ring.halfPitch * kRadToDeg, std::memory_order_relaxed);
            feature.fieldYawDeg.store(shared.halfYaw * kRadToDeg, std::memory_order_relaxed);
            feature.fieldPitchDeg.store(shared.halfPitch * kRadToDeg, std::memory_order_relaxed);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        what = Ring::Fault;
    }
    feature.ring[static_cast<size_t>(what)].fetch_add(1, std::memory_order_relaxed);
    return what;
}
// The render thread, after the game's projection returned or unwound. The game's own point goes back always;
// the off-screen value only when the game finished.
__declspec(noinline) void ReachNavRingAfter(void* record, const ReachNavRingCall& call, bool completed) noexcept
{
    auto& feature = g_reachNavProjection;
    __try
    {
        auto* bytes = static_cast<uint8_t*>(record);
        memcpy(bytes + feature.sites.recordPoint, call.point, sizeof(call.point));
        if (completed)
            memcpy(bytes + feature.sites.recordOffscreen, &call.offscreen, sizeof(call.offscreen));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        feature.restoreFaults.fetch_add(1, std::memory_order_relaxed);
    }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// One projection, by the game's call site. The render thread; counters only.
inline void ReachNavNoteCaller(uintptr_t caller, bool wantsRing) noexcept
{
    auto& feature = g_reachNavProjection;
    int site = kReachNavCallerCapacity;
    for (int i = 0; i < feature.callerCount; ++i)
        if (feature.callerReturns[i] == caller)
        {
            site = i;
            break;
        }
    (wantsRing ? feature.callerRing : feature.callerPlain)[site].fetch_add(1, std::memory_order_relaxed);
}
// The eye's raster as the stereo transaction committed it for the eye being rendered now.
bool ReachNavEyeRaster(const ReachFpCameraEyeScope& scope, reach_nav_markers::EyeRaster& eye) noexcept
{
    memcpy(eye.camera.position, scope.compact + 0x00, sizeof(eye.camera.position));
    memcpy(eye.camera.forward, scope.compact + kReachCompactCameraForwardOffset, sizeof(eye.camera.forward));
    memcpy(eye.camera.up, scope.compact + 0x18, sizeof(eye.camera.up));
    memcpy(&eye.camera.verticalFov, scope.compact + reach_nav_markers::kCameraVerticalFovOffset,
           sizeof(eye.camera.verticalFov));
    // The derived block's projection matrix: [0] and [5] are its two scales (the same pair the stereo transaction
    // decodes to tell OpenXR what the raster covers).
    const float* projection = reinterpret_cast<const float*>(scope.derived + kReachDerivedProjectionOffset);
    eye.xScale = fabsf(projection[0]);
    eye.yScale = fabsf(projection[5]);
    // The compact camera's window: y0, x0, y1, x1 as 16-bit pixels (retail +0x287C5C writes them in that order).
    int16_t window[4]{};
    memcpy(window, scope.compact + 0x38, sizeof(window));
    eye.width = static_cast<float>(window[3] - window[1]);
    eye.height = static_cast<float>(window[2] - window[0]);
    return eye.width > 0.0f && eye.height > 0.0f && isfinite(eye.xScale) && isfinite(eye.yScale) &&
           eye.xScale > 0.0f && eye.yScale > 0.0f;
}
// A float atomic's running minimum / maximum. One writer (the render thread).
inline void ReachNavWitnessLow(std::atomic<float>& low, float value) noexcept
{
    if (value < low.load(std::memory_order_relaxed))
        low.store(value, std::memory_order_relaxed);
}
inline void ReachNavWitnessHigh(std::atomic<float>& high, float value) noexcept
{
    if (value > high.load(std::memory_order_relaxed))
        high.store(value, std::memory_order_relaxed);
}
// An arrow, as one eye drew it: its pixel turned back into a direction, in that eye's frame and in the head's;
// and, once the other eye of the same frame has drawn the same target, the difference between the two.
// `point` is the game's own target (never the stand-in). Reads only; the caller's __try covers it.
void ReachNavWitnessArrow(const ReachFpCameraEyeScope& scope, const reach_nav_markers::EyeRaster& eye,
                          const float point[3], const float pixel[2], bool standIn) noexcept
{
    using namespace reach_nav_markers;
    auto& feature = g_reachNavProjection;
    const uint32_t eyeIndex = scope.eye;
    float across = 0.0f, up = 0.0f;
    if (!EyePixelTangents(eye, pixel, across, up))
        return;
    float ray[3];
    EyeRay(eye.camera, across, up, ray);
    Camera head;
    ReachNavHeadCamera(head);
    float headAcross = 0.0f, headUp = 0.0f;
    if (!g_reachOwnerScope.active || !HeadUsable(head) || !DirectionAngles(head, ray, headAcross, headUp))
        return;
    const float targetDeg = OffAxis(head, point) * kRadToDeg;
    feature.arrowSamples.fetch_add(1, std::memory_order_relaxed);
    ReachNavWitnessLow(feature.arrowMinDeg, targetDeg);
    ReachNavWitnessHigh(feature.arrowMaxDeg, targetDeg);
    feature.arrowPixel[eyeIndex][0].store(pixel[0], std::memory_order_relaxed);
    feature.arrowPixel[eyeIndex][1].store(pixel[1], std::memory_order_relaxed);
    feature.arrowAxisDeg[eyeIndex].store(atanf(hypotf(across, up)) * kRadToDeg, std::memory_order_relaxed);
    feature.arrowHeadDeg[eyeIndex][0].store(headAcross * kRadToDeg, std::memory_order_relaxed);
    feature.arrowHeadDeg[eyeIndex][1].store(headUp * kRadToDeg, std::memory_order_relaxed);
    ReachNavArrowEye& previous = g_reachNavArrowPrevious;
    if (previous.valid && previous.eye != eyeIndex && previous.serial == scope.preparedSerial &&
        memcmp(previous.point, point, sizeof(previous.point)) == 0)
    {
        // Left minus right. For a real point at distance d the left eye sees it further to the right than the
        // right eye does, by (eye separation / d): that is the difference a fused marker has.
        const bool currentIsLeft = eyeIndex == 0;
        const float leftAcross = currentIsLeft ? headAcross : previous.headAcross;
        const float rightAcross = currentIsLeft ? previous.headAcross : headAcross;
        const float leftUp = currentIsLeft ? headUp : previous.headUp;
        const float rightUp = currentIsLeft ? previous.headUp : headUp;
        const float diffAcross = (leftAcross - rightAcross) * kArcMinutesPerRadian;
        const float diffUp = (leftUp - rightUp) * kArcMinutesPerRadian;
        const float separation[3]{eye.camera.position[0] - previous.eyePosition[0],
                                  eye.camera.position[1] - previous.eyePosition[1],
                                  eye.camera.position[2] - previous.eyePosition[2]};
        const float toTarget[3]{point[0] - head.position[0], point[1] - head.position[1],
                                point[2] - head.position[2]};
        const float distance = sqrtf(Dot(toTarget, toTarget));
        feature.arrowNatural.store(
            distance > 1.0e-4f ? sqrtf(Dot(separation, separation)) / distance * kArcMinutesPerRadian : 0.0f,
            std::memory_order_relaxed);
        feature.arrowDiff[0].store(diffAcross, std::memory_order_relaxed);
        feature.arrowDiff[1].store(diffUp, std::memory_order_relaxed);
        ReachNavWitnessHigh(feature.arrowDiffMax[0], fabsf(diffAcross));
        ReachNavWitnessHigh(feature.arrowDiffMax[1], fabsf(diffUp));
        feature.arrowTargetDeg.store(targetDeg, std::memory_order_relaxed);
        feature.arrowPairs.fetch_add(1, std::memory_order_relaxed);
        if (standIn)
            feature.arrowStandIns.fetch_add(1, std::memory_order_relaxed);
    }
    previous.valid = true;
    previous.eye = eyeIndex;
    previous.serial = scope.preparedSerial;
    memcpy(previous.point, point, sizeof(previous.point));
    previous.headAcross = headAcross;
    previous.headUp = headUp;
    memcpy(previous.eyePosition, eye.camera.position, sizeof(previous.eyePosition));
}
// The render thread, right after the game projected `record` (and, for a stand-in, after the game's own point
// was put back). Reads only. `standIn`: this call was handed a stand-in point.
__declspec(noinline) void ReachNavWitnessProjection(const void* record, bool standIn) noexcept
{
    using namespace reach_nav_markers;
    auto& feature = g_reachNavProjection;
    Witness what = Witness::Fault;
    __try
    {
        const ReachFpCameraEyeScope& scope = g_reachFpCameraEyeScope;
        const auto* bytes = static_cast<const uint8_t*>(record);
        if (!ReachOwnsHudStereoTransaction() || scope.generation != feature.generation || scope.eye > 1)
            what = Witness::NoEyeScope;
        else if (!bytes[0])
            what = Witness::NotValid;
        else
        {
            const ProjectionSites& sites = feature.sites;
            const uint32_t eyeIndex = scope.eye;
            float point[3], pixel[2], offscreen = 0.0f, own[2];
            memcpy(point, bytes + sites.recordPoint, sizeof(point));
            memcpy(pixel, bytes + sites.recordPixel, sizeof(pixel));
            memcpy(&offscreen, bytes + sites.recordOffscreen, sizeof(offscreen));
            EyeRaster eye;
            if (!ReachNavEyeRaster(scope, eye))
                what = Witness::NoRaster;
            else if (offscreen > 0.0f)
            {
                feature.lastOffscreen[eyeIndex].store(offscreen, std::memory_order_relaxed);
                ReachNavWitnessArrow(scope, eye, point, pixel, standIn);
                what = Witness::OnRing;
            }
            else if (!EyeRasterPixel(eye, point, own))
                what = Witness::NotInFront;
            else
            {
                Camera head;
                ReachNavHeadCamera(head);
                if (g_reachOwnerScope.active && HeadUsable(head))
                {
                    const float targetDeg = OffAxis(head, point) * kRadToDeg;
                    feature.pointSamples.fetch_add(1, std::memory_order_relaxed);
                    ReachNavWitnessLow(feature.pointMinDeg, targetDeg);
                    ReachNavWitnessHigh(feature.pointMaxDeg, targetDeg);
                }
                const float error = hypotf(pixel[0] - own[0], pixel[1] - own[1]);
                feature.compared[eyeIndex].fetch_add(1, std::memory_order_relaxed);
                feature.errorSum[eyeIndex].store(
                    feature.errorSum[eyeIndex].load(std::memory_order_relaxed) + error, std::memory_order_relaxed);
                if (error > feature.errorMax[eyeIndex].load(std::memory_order_relaxed))
                    feature.errorMax[eyeIndex].store(error, std::memory_order_relaxed);
                const float d[3]{point[0] - eye.camera.position[0], point[1] - eye.camera.position[1],
                                 point[2] - eye.camera.position[2]};
                feature.lastOffAxisDeg[eyeIndex].store(OffAxis(eye.camera, point) * kRadToDeg,
                                                       std::memory_order_relaxed);
                feature.lastTargetDistance[eyeIndex].store(sqrtf(Dot(d, d)), std::memory_order_relaxed);
                for (int i = 0; i < 2; ++i)
                {
                    feature.lastGame[eyeIndex][i].store(pixel[i], std::memory_order_relaxed);
                    feature.lastOwn[eyeIndex][i].store(own[i], std::memory_order_relaxed);
                }
                feature.rasterWidth.store(eye.width, std::memory_order_relaxed);
                feature.rasterHeight.store(eye.height, std::memory_order_relaxed);
                what = Witness::Compared;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        what = Witness::Fault;
    }
    feature.seen[static_cast<size_t>(what)].fetch_add(1, std::memory_order_relaxed);
}
#endif

__declspec(noinline) void __fastcall ReachNavProjectionDetour(void* record, int user, uint8_t ring, uint8_t behind,
                                                              uint8_t worldPitch)
{
    // The hook enters here by a jump, so this is the game's own return address: which of its callers this is.
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto& feature = g_reachNavProjection;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const auto original = reinterpret_cast<ReachNavProjectionFn>(feature.original);
        if (!original)
            return;
        ReachNavRingCall call;
        const bool enabled = record && feature.enabled.load(std::memory_order_acquire);
        if (enabled)
        {
#if HALOMCCVR_DEV_DIAGNOSTICS
            ReachNavNoteCaller(caller, ring != 0);
#endif
            (void)ReachNavRingBefore(record, user, ring, worldPitch,
                                     caller == feature.navpointReturn ||
                                         (feature.objectMarkerReturn && caller == feature.objectMarkerReturn),
                                     call);
        }
        bool completed = false;
        __try
        {
            CallNativeWithSpareSlots(original, record, user, ring, behind, worldPitch);
            completed = true;
        }
        __finally
        {
            if (call.moved)
                ReachNavRingAfter(record, call, completed);
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        // What the game drew: a point where it stands (its pixel against the eye's own picture), or an arrow
        // (where each eye put it). After the game's own point is back in the record.
        if (enabled && g_config.nav_marker_witness)
            ReachNavWitnessProjection(record, call.moved);
#endif
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}
// Disables, waits for the detour to be quiet, removes. True when nothing is left installed.
bool RemoveReachNavProjection()
{
    auto& feature = g_reachNavProjection;
    feature.enabled.store(false, std::memory_order_release);
    if (!feature.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(feature.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("NAV MARKERS Reach: CleanupRequired: the marker projection's hook: disable failed (%d)",
            static_cast<int>(disabled));
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&ReachNavProjectionDetour)};
    const void* trampolines[]{feature.original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, feature.callbacks))
    {
        LOG("NAV MARKERS Reach: CleanupRequired: the marker projection's detour is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(feature.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("NAV MARKERS Reach: CleanupRequired: the marker projection's hook: removal failed (%d)",
            static_cast<int>(removed));
        return false;
    }
    feature.target = nullptr;
    feature.original = nullptr;
    return true;
}
// Cold, on the title's install thread, after the observer-camera hook's fate is known. Binds nothing unless every
// link proves itself on this image; a refusal is the game's own ring, said once, and part 1 stands either way.
bool InstallReachNavProjection(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace reach_nav_markers;
    auto& feature = g_reachNavProjection;
    if (feature.target)
    {
        LOG("NAV MARKERS Reach: StockFallback (the off-screen ring): the previous install was not retired");
        return false;
    }
    // Nothing is hooked unless somebody asked: the player's switch for the arrow, or (dev builds) the witness.
    // Both are read here, when the level loads.
    bool wanted = g_config.nav_marker_arrow_in_view;
#if HALOMCCVR_DEV_DIAGNOSTICS
    wanted = wanted || g_config.nav_marker_witness != 0;
#endif
    if (!wanted)
    {
        LOG("NAV MARKERS Reach: the off-screen arrow is the game's own (nav_marker_arrow_in_view = 0, read when a "
            "level loads); the marker projection is not hooked");
        return false;
    }
    const uintptr_t head = ReachHeadRelativeUnique(base, size, kProjectionHeadPattern);
    const uintptr_t camera = ReachHeadRelativeUnique(base, size, kProjectionCameraPattern);
    const uintptr_t ring = ReachHeadRelativeUnique(base, size, kProjectionRingPattern);
    const uintptr_t offscreen = ReachHeadRelativeUnique(base, size, kProjectionOffscreenPattern);
    const uintptr_t pixel = ReachHeadRelativeUnique(base, size, kProjectionPixelPattern);
    const uintptr_t navpoint = ReachHeadRelativeUnique(base, size, kNavpointCallPattern);
    uint8_t headBytes[kProjectionHeadBytes]{}, cameraBytes[kProjectionCameraBytes]{};
    uint8_t ringBytes[kProjectionRingBytes]{}, offscreenBytes[kProjectionOffscreenBytes]{};
    uint8_t pixelBytes[kProjectionPixelBytes]{};
    const bool headRead = ReachHeadRelativeRead(base, size, head, headBytes, kProjectionHeadBytes);
    const bool cameraRead = ReachHeadRelativeRead(base, size, camera, cameraBytes, kProjectionCameraBytes);
    const bool ringRead = ReachHeadRelativeRead(base, size, ring, ringBytes, kProjectionRingBytes);
    const bool offscreenRead = ReachHeadRelativeRead(base, size, offscreen, offscreenBytes, kProjectionOffscreenBytes);
    const bool pixelRead = ReachHeadRelativeRead(base, size, pixel, pixelBytes, kProjectionPixelBytes);
    const int64_t headRva = static_cast<int64_t>(head - base);
    ProjectionSites sites;
    ProjectionBind bind = DecodeProjectionSites(
        headRead ? headBytes : nullptr, headRva, headRead && ReachHeadRelativeFunctionRva(base, head) == headRva,
        cameraRead ? cameraBytes : nullptr, static_cast<int64_t>(camera - base),
        cameraRead ? ReachHeadRelativeFunctionRva(base, camera) : -1, ringRead ? ringBytes : nullptr,
        static_cast<int64_t>(ring - base), ringRead ? ReachHeadRelativeFunctionRva(base, ring) : -1,
        offscreenRead ? offscreenBytes : nullptr, static_cast<int64_t>(offscreen - base),
        offscreenRead ? ReachHeadRelativeFunctionRva(base, offscreen) : -1, pixelRead ? pixelBytes : nullptr,
        static_cast<int64_t>(pixel - base), pixelRead ? ReachHeadRelativeFunctionRva(base, pixel) : -1, sites);
    if (bind == ProjectionBind::Bound)
    {
        // The camera this function builds is the one the observer-camera hook corrects at its CHUD site.
        const uintptr_t observerHook = reinterpret_cast<uintptr_t>(g_reachCamera.observerCameraTarget);
        bind = CheckProjectionConverter(
            sites, observerHook > base ? static_cast<int64_t>(observerHook - base) : 0,
            static_cast<int64_t>(kReachObserverCameraReturnRvas[kReachObserverCameraChudSite]));
    }
    if (bind == ProjectionBind::Bound)
    {
        uint8_t fraction[sizeof(float)]{}, pi[sizeof(float)]{}, floor[sizeof(float)]{};
        const bool constantsRead =
            ReachHeadRelativeRead(base, size, base + static_cast<uintptr_t>(sites.fractionRva), fraction,
                                  sizeof(fraction)) &&
            ReachHeadRelativeRead(base, size, base + static_cast<uintptr_t>(sites.piRva), pi, sizeof(pi)) &&
            ReachHeadRelativeRead(base, size, base + static_cast<uintptr_t>(sites.floorRva), floor, sizeof(floor));
        bind = CheckProjectionConstants(constantsRead ? fraction : nullptr, pi, floor);
    }
    if (bind == ProjectionBind::Bound)
    {
        // The one caller the stand-in acts for: its call must land on the function bound above, from another.
        uint8_t navpointBytes[kNavpointCallBytes]{};
        const bool navpointRead = ReachHeadRelativeRead(base, size, navpoint, navpointBytes, kNavpointCallBytes);
        bind = DecodeNavpointCall(navpointRead ? navpointBytes : nullptr, static_cast<int64_t>(navpoint - base),
                                  navpointRead ? ReachHeadRelativeFunctionRva(base, navpoint) : -1, sites);
    }
    if (bind != ProjectionBind::Bound)
    {
        LOG("NAV MARKERS Reach: StockFallback (the off-screen ring): %s (head %s, camera %s, ring %s, off-screen %s, "
            "pixel %s, navpoint call %s); off-screen markers keep the game's own ring",
            ProjectionBindName(bind), head ? "matched" : "unmatched", camera ? "matched" : "unmatched",
            ring ? "matched" : "unmatched", offscreen ? "matched" : "unmatched", pixel ? "matched" : "unmatched",
            navpoint ? "matched" : "unmatched");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.sites = sites;
    feature.navpointReturn = base + static_cast<uintptr_t>(sites.navpointReturnRva);
    {
        // The object markers' call: optional, its own pattern, the same proof (it lands on the bound projection
        // from another function).
        const uintptr_t objectCall = ReachHeadRelativeUnique(base, size, kObjectMarkerCallPattern);
        uint8_t objectBytes[kObjectMarkerCallBytes]{};
        const bool objectRead = ReachHeadRelativeRead(base, size, objectCall, objectBytes, kObjectMarkerCallBytes);
        const int64_t objectReturnRva = DecodeObjectMarkerCall(
            objectRead ? objectBytes : nullptr, static_cast<int64_t>(objectCall - base),
            objectRead ? ReachHeadRelativeFunctionRva(base, objectCall) : -1, sites);
        feature.objectMarkerReturn = objectReturnRva > 0 ? base + static_cast<uintptr_t>(objectReturnRva) : 0;
        if (feature.objectMarkerReturn)
            LOG("NAV MARKERS Reach (the off-screen ring): the HUD's object-anchored markers (objective and "
                "fireteam objects, tracked objects, players) get the stand-in too: their call of the projection "
                "returns to haloreach.dll+0x%llX", static_cast<unsigned long long>(objectReturnRva));
        else
            LOG("NAV MARKERS Reach (the off-screen ring): StockFallback for the HUD's object-anchored markers "
                "only: their call of the projection was %s on this image, so their arrows keep the game's own "
                "ring; the navpoint list's stand-in is unaffected",
                objectCall ? "not a call that lands on the projection" : "not found or ambiguous");
    }
    feature.reportedFirstMove = false;
    for (auto& count : feature.ring)
        count.store(0, std::memory_order_relaxed);
    feature.restoreFaults.store(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    for (auto& count : feature.seen)
        count.store(0, std::memory_order_relaxed);
    for (int eye = 0; eye < 2; ++eye)
    {
        feature.compared[eye].store(0, std::memory_order_relaxed);
        feature.errorSum[eye].store(0.0f, std::memory_order_relaxed);
        feature.errorMax[eye].store(0.0f, std::memory_order_relaxed);
        feature.arrowDiffMax[eye].store(0.0f, std::memory_order_relaxed);
    }
    feature.arrowPairs.store(0, std::memory_order_relaxed);
    feature.arrowStandIns.store(0, std::memory_order_relaxed);
    feature.pointSamples.store(0, std::memory_order_relaxed);
    feature.arrowSamples.store(0, std::memory_order_relaxed);
    feature.pointMinDeg.store(1.0e9f, std::memory_order_relaxed);
    feature.pointMaxDeg.store(-1.0f, std::memory_order_relaxed);
    feature.arrowMinDeg.store(1.0e9f, std::memory_order_relaxed);
    feature.arrowMaxDeg.store(-1.0f, std::memory_order_relaxed);
    feature.callerFound = ReachHeadRelativeCallers(base, size, head, feature.callerReturns, kReachNavCallerCapacity);
    feature.callerCount = feature.callerFound < kReachNavCallerCapacity ? feature.callerFound : kReachNavCallerCapacity;
    for (int i = 0; i <= kReachNavCallerCapacity; ++i)
    {
        feature.callerRing[i].store(0, std::memory_order_relaxed);
        feature.callerPlain[i].store(0, std::memory_order_relaxed);
    }
#endif
    const auto created = MH_CreateHook(reinterpret_cast<void*>(head),
                                       reinterpret_cast<void*>(&ReachNavProjectionDetour), &feature.original);
    if (created != MH_OK)
    {
        feature.original = nullptr;
        LOG("NAV MARKERS Reach: StockFallback (the off-screen ring): hook creation failed (%d)",
            static_cast<int>(created));
        return false;
    }
    feature.target = reinterpret_cast<void*>(head);
    const auto enabled = MH_EnableHook(feature.target);
    if (enabled != MH_OK)
    {
        LOG("NAV MARKERS Reach: StockFallback (the off-screen ring): hook enable failed (%d)",
            static_cast<int>(enabled));
        (void)RemoveReachNavProjection();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("NAV MARKERS Reach: Installed (the off-screen ring): the marker projection haloreach.dll+0x%llX is hooked; "
        "its camera call returns to +0x%llX, the observer-camera hook's CHUD site, and lands on that hook's function "
        "+0x%llX; its ring reads the camera's vertical field of view at +0x%X; the record's point is at +0x%X, its "
        "pixel at +0x%X, its off-screen value at +0x%X; with the arrow switch on, a point outside %.2f of the field "
        "both eyes see (30 deg at most) is brought onto that ring about the head for the one call, for the "
        "navpoint list's call (it returns to +0x%llX) and the object-anchored markers' call (%s%llX); every other "
        "caller keeps the game's own ring; arrow switch=%d, field-of-view switch=%d",
        static_cast<unsigned long long>(sites.headRva), static_cast<unsigned long long>(sites.converterReturnRva),
        static_cast<unsigned long long>(sites.converterRva), sites.cameraFovOffset, sites.recordPoint,
        sites.recordPixel, sites.recordOffscreen, g_config.nav_marker_ring,
        static_cast<unsigned long long>(sites.navpointReturnRva),
        feature.objectMarkerReturn ? "it returns to +0x" : "NOT FOUND on this image: 0x",
        static_cast<unsigned long long>(feature.objectMarkerReturn ? feature.objectMarkerReturn - base : 0),
        g_config.nav_marker_arrow_in_view ? 1 : 0,
        g_config.nav_markers_on_target ? 1 : 0);
    return true;
}

const char* ReachNavRingName(size_t what)
{
    return reach_nav_markers::RingName(static_cast<reach_nav_markers::Ring>(what));
}
#if HALOMCCVR_DEV_DIAGNOSTICS
const char* ReachNavWitnessName(size_t what)
{
    return reach_nav_markers::WitnessName(static_cast<reach_nav_markers::Witness>(what));
}
#endif
// The worker's two-second report. Every build: one line the first time a point is brought onto the head's ring
// after an install. Dev builds: the window's verdicts, and the witness's comparison when it is switched on.
void ReportReachNavProjection()
{
    using reach_nav_markers::Ring;
    auto& feature = g_reachNavProjection;
    if (!feature.target)
        return;
    uint32_t verdicts[kReachNavRingCount]{}, total = 0;
    for (size_t i = 0; i < kReachNavRingCount; ++i)
    {
        verdicts[i] = feature.ring[i].exchange(0, std::memory_order_relaxed);
        total += verdicts[i];
    }
    const uint32_t restoreFaults = feature.restoreFaults.exchange(0, std::memory_order_relaxed);
    if (!total && !restoreFaults)
        return;
    const uint32_t moved = verdicts[static_cast<size_t>(Ring::Moved)];
    if (moved && !feature.reportedFirstMove)
    {
        feature.reportedFirstMove = true;
        LOG("NAV MARKERS Reach: off-screen markers ride a ring about the head: %.1f deg sideways, %.1f deg up and "
            "down (the field both eyes see is %.1f x %.1f deg); the first one was %.1f deg to the %s and %.1f deg "
            "%s of where the head faces",
            feature.ringYawDeg.load(std::memory_order_relaxed), feature.ringPitchDeg.load(std::memory_order_relaxed),
            feature.fieldYawDeg.load(std::memory_order_relaxed),
            feature.fieldPitchDeg.load(std::memory_order_relaxed),
            fabsf(feature.lastYawDeg.load(std::memory_order_relaxed)),
            feature.lastYawDeg.load(std::memory_order_relaxed) >= 0.0f ? "left" : "right",
            fabsf(feature.lastPitchDeg.load(std::memory_order_relaxed)),
            feature.lastPitchDeg.load(std::memory_order_relaxed) >= 0.0f ? "above" : "below");
    }
    if (restoreFaults)
        LOG("NAV MARKERS Reach: %u marker records could not be put back after the game's projection", restoreFaults);
#if HALOMCCVR_DEV_DIAGNOSTICS
    char reasons[320];
    const int length = ReachHeadRelativeReasons(reasons, sizeof(reasons), verdicts, kReachNavRingCount,
                                                static_cast<size_t>(Ring::Moved), &ReachNavRingName);
    LOG("NAV MARKERS Reach (dev): the marker projection ran %u times: %u brought onto the head's ring (last: %.1f "
        "deg left, %.1f deg up of the head's axis, %.2f times as far out as the ring, %.2f wu away; ring %.1f x "
        "%.1f deg); the others: %s; records not put back %u; ring fraction %.2f",
        total, moved, feature.lastYawDeg.load(std::memory_order_relaxed),
        feature.lastPitchDeg.load(std::memory_order_relaxed), feature.lastRatio.load(std::memory_order_relaxed),
        feature.lastDistance.load(std::memory_order_relaxed), feature.ringYawDeg.load(std::memory_order_relaxed),
        feature.ringPitchDeg.load(std::memory_order_relaxed), length ? reasons : "none", restoreFaults,
        g_config.nav_marker_ring);
    // Who called this window, by the game's call site: with a ring asked for, and without.
    char callers[768];
    int callersLength = 0;
    callers[0] = 0;
    for (int i = 0; i <= kReachNavCallerCapacity; ++i)
    {
        const uint32_t asked = feature.callerRing[i].exchange(0, std::memory_order_relaxed);
        const uint32_t plain = feature.callerPlain[i].exchange(0, std::memory_order_relaxed);
        if (!(asked + plain) || callersLength >= static_cast<int>(sizeof(callers)) - 112)
            continue;
        const bool known = i < feature.callerCount;
        const uintptr_t returns = known ? feature.callerReturns[i] : 0;
        const int wrote = _snprintf_s(
            callers + callersLength, sizeof(callers) - callersLength, _TRUNCATE, "%s%s+0x%llX%s: ring %u, no ring %u",
            callersLength ? "; " : "", known ? "returning to haloreach.dll" : "another caller ",
            static_cast<unsigned long long>(known ? returns - feature.base : 0),
            known && returns == feature.navpointReturn ? " (the navpoint list)"
                : known && returns == feature.objectMarkerReturn ? " (object markers)" : "", asked, plain);
        if (wrote > 0)
            callersLength += wrote;
    }
    LOG("NAV MARKERS Reach (dev): the projection's callers this window (%d direct call sites in the image): %s",
        feature.callerFound, callersLength ? callers : "none");
    using reach_nav_markers::Witness;
    uint32_t seen[kReachNavWitnessCount]{}, witnessed = 0;
    for (size_t i = 0; i < kReachNavWitnessCount; ++i)
    {
        seen[i] = feature.seen[i].exchange(0, std::memory_order_relaxed);
        witnessed += seen[i];
    }
    if (!witnessed)
        return;
    const int witnessLength = ReachHeadRelativeReasons(reasons, sizeof(reasons), seen, kReachNavWitnessCount,
                                                       static_cast<size_t>(Witness::Compared), &ReachNavWitnessName);
    for (int eye = 0; eye < 2; ++eye)
    {
        const uint32_t compared = feature.compared[eye].exchange(0, std::memory_order_relaxed);
        const float sum = feature.errorSum[eye].exchange(0.0f, std::memory_order_relaxed);
        const float maximum = feature.errorMax[eye].exchange(0.0f, std::memory_order_relaxed);
        if (!compared)
            continue;
        const float distance = feature.lastTargetDistance[eye].load(std::memory_order_relaxed);
        LOG("NAV MARKERS Reach (dev witness): eye %d: %u markers drawn on their point; the game's pixel against the "
            "eye's own picture: mean %.2f px, max %.2f px of a %.0f x %.0f raster; last: the point %.1f deg off the "
            "eye's axis, %.2f wu (%.1f m) away, drawn at (%.1f, %.1f), seen at (%.1f, %.1f); switch=%d",
            eye, compared, sum / static_cast<float>(compared), maximum,
            feature.rasterWidth.load(std::memory_order_relaxed), feature.rasterHeight.load(std::memory_order_relaxed),
            feature.lastOffAxisDeg[eye].load(std::memory_order_relaxed), distance, distance / kReachWorldUnitsPerMeter,
            feature.lastGame[eye][0].load(std::memory_order_relaxed),
            feature.lastGame[eye][1].load(std::memory_order_relaxed),
            feature.lastOwn[eye][0].load(std::memory_order_relaxed),
            feature.lastOwn[eye][1].load(std::memory_order_relaxed), g_config.nav_markers_on_target ? 1 : 0);
    }
    // THE ARROW, as each eye drew it this window (the last pair, and the largest difference between the eyes).
    const uint32_t pairs = feature.arrowPairs.exchange(0, std::memory_order_relaxed);
    const uint32_t standIns = feature.arrowStandIns.exchange(0, std::memory_order_relaxed);
    const float largestAcross = feature.arrowDiffMax[0].exchange(0.0f, std::memory_order_relaxed);
    const float largestUp = feature.arrowDiffMax[1].exchange(0.0f, std::memory_order_relaxed);
    if (pairs)
        LOG("NAV MARKERS Reach (dev witness): arrow (%s, %u pairs): target %.1f deg off the head's axis; left eye "
            "at (%.1f, %.1f), %.2f deg from its own axis; right eye at (%.1f, %.1f), %.2f deg; in the head's frame "
            "left (%.2f across, %.2f up) deg, right (%.2f across, %.2f up) deg; left minus right %.1f' across, "
            "%.1f' up (largest this window %.1f' / %.1f'); a real point at the target's distance: %.1f' across",
            standIns == pairs ? "stand-in" : standIns ? "MIXED" : "the game's own ring", pairs,
            feature.arrowTargetDeg.load(std::memory_order_relaxed),
            feature.arrowPixel[0][0].load(std::memory_order_relaxed),
            feature.arrowPixel[0][1].load(std::memory_order_relaxed),
            feature.arrowAxisDeg[0].load(std::memory_order_relaxed),
            feature.arrowPixel[1][0].load(std::memory_order_relaxed),
            feature.arrowPixel[1][1].load(std::memory_order_relaxed),
            feature.arrowAxisDeg[1].load(std::memory_order_relaxed),
            feature.arrowHeadDeg[0][0].load(std::memory_order_relaxed),
            feature.arrowHeadDeg[0][1].load(std::memory_order_relaxed),
            feature.arrowHeadDeg[1][0].load(std::memory_order_relaxed),
            feature.arrowHeadDeg[1][1].load(std::memory_order_relaxed),
            feature.arrowDiff[0].load(std::memory_order_relaxed), feature.arrowDiff[1].load(std::memory_order_relaxed),
            largestAcross, largestUp, feature.arrowNatural.load(std::memory_order_relaxed));
    // The hand-over between marker and arrow: how far off the head's axis each was drawn this window.
    const uint32_t pointSamples = feature.pointSamples.exchange(0, std::memory_order_relaxed);
    const uint32_t arrowSamples = feature.arrowSamples.exchange(0, std::memory_order_relaxed);
    const float pointLow = feature.pointMinDeg.exchange(1.0e9f, std::memory_order_relaxed);
    const float pointHigh = feature.pointMaxDeg.exchange(-1.0f, std::memory_order_relaxed);
    const float arrowLow = feature.arrowMinDeg.exchange(1.0e9f, std::memory_order_relaxed);
    const float arrowHigh = feature.arrowMaxDeg.exchange(-1.0f, std::memory_order_relaxed);
    if (pointSamples || arrowSamples)
        LOG("NAV MARKERS Reach (dev witness): target off the head's axis this window: drawn on its point %u times "
            "(%.1f to %.1f deg), drawn as an arrow %u times (%.1f to %.1f deg)",
            pointSamples, pointSamples ? pointLow : 0.0f, pointSamples ? pointHigh : 0.0f, arrowSamples,
            arrowSamples ? arrowLow : 0.0f, arrowSamples ? arrowHigh : 0.0f);
    LOG("NAV MARKERS Reach (dev witness): %u projections looked at; not compared: %s; the game's last off-screen "
        "values %.4f / %.4f",
        witnessed, witnessLength ? reasons : "none", feature.lastOffscreen[0].load(std::memory_order_relaxed),
        feature.lastOffscreen[1].load(std::memory_order_relaxed));
#endif
}
