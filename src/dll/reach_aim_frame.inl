// AIM FRAME ZOOM in Halo: Reach (config.h aim_frame_zoom; game.cpp AimFrameThisFrame). Halo 3's experience: while the
// game is zoomed, the headset shows the game's zoomed picture as a frame centred on the aim.
//
// Reach's own way to it. Reach builds visibility once, outside the eye passes, from the head-centre camera (the lens
// widens that camera for the same reason: reach_scope.inl), and the head camera is also what the hands' root, the
// head sample and the seat's reticle are placed against, so it keeps looking where the head looks: its field of view
// is widened to cover the frame, and the frame's own camera is written only in the eye passes. The projection helper
// takes a vertical field of view and derives the width from the render surface, so the picture has the surface's
// shape, as read back, not the flat game's 16:9. The game's zoomed view is the stock projection the engine built for
// this frame, before any of ours.
std::atomic<uint32_t> g_reachAimFrameOffGeneration{0};
std::atomic<uint32_t> g_reachAimFrameGivenBack{0};
aim_frame::RefusalRun g_reachAimFrameRefusals;   // the render thread

enum class ReachAimFramePrepare
{
    Off,        // no frame this frame, and nothing wrong: not zoomed, or the head turned too far from the aim to cover
    Built,
    Refused,    // the game is zoomed and this frame's picture could not be built
};

ReachAimFramePrepare ReachPrepareAimFrameBody(ReachOwnerScope& frame, unsigned char* headDerived,
                                              const unsigned char* stockCompact, const float* stockProjection,
                                              const ReachVrRenderSnapshot& tracking)
{
    constexpr GameTitle kTitle = GameTitle::HaloReach;
    const ReachProjectionHalfFovs stockFov = DecodeReachProjectionHalfFovs(stockProjection[0], stockProjection[5]);
    const bool native = stockFov.valid && !frame.cutsceneTheater;
    const float nativeX = native ? tanf(stockFov.horizontal) : 0.0f;
    const float nativeY = native ? tanf(stockFov.vertical) : 0.0f;
    // As the lens does: along the intent ray from the head while the converged aim is written, else the aim feedback
    // this frame's head camera has just published.
    float convergedForward[3]{};
    const bool converged = g_config.aim_frame_zoom && Game_ReachScopeConvergedDirection(convergedForward);
    AimFrame aim{};
    if (!AimFrameThisFrame(kTitle, native, nativeX, nativeY, reinterpret_cast<const float*>(stockCompact + 0x18), aim,
                           converged ? convergedForward : nullptr, true))
    {
        (void)AimFrameMeetWu(kTitle, false, 1.0f);
        return ReachAimFramePrepare::Off;
    }

    // The frame's camera, checked against what the engine's own helpers build from it.
    alignas(16) unsigned char compact[kReachCompactCameraBytes];
    alignas(16) unsigned char derived[kReachDerivedBlockSize];
    memcpy(compact, frame.headCenter, sizeof(compact));
    memcpy(compact + 0x0C, aim.forward, sizeof(aim.forward));
    memcpy(compact + 0x18, aim.up, sizeof(aim.up));
    *reinterpret_cast<float*>(compact + 0x28) = 2.0f * atanf(aim.tanY);
    ReachCompactCameraObservation observed{};
    float bounds[4]{};
    if (!ValidateReachCompactCamera(compact, kReachCompactCameraBytes, observed) ||
        !g_reachHelpers.frustum(compact, bounds))
        return ReachAimFramePrepare::Refused;
    for (float v : bounds)
        if (!std::isfinite(v))
            return ReachAimFramePrepare::Refused;
    g_reachHelpers.projection(compact, bounds, derived, 0.0f);
    const float* built = reinterpret_cast<const float*>(derived + kReachDerivedProjectionOffset);
    const ReachProjectionHalfFovs builtFov = DecodeReachProjectionHalfFovs(built[0], built[5]);
    if (!builtFov.valid || !aim_frame::TangentsAgree(aim.tanY, tanf(builtFov.vertical)))
        return ReachAimFramePrepare::Refused;

    // Visibility: the head camera's field of view grown to hold the frame, as for the lens. A square cover around the
    // picture, as Halo 3's, for the eyes' spacing and for what stands just outside it.
    ScopeCameraPose pose{};
    memcpy(pose.position, frame.headCenter, sizeof(pose.position));
    memcpy(pose.forward, aim.forward, sizeof(pose.forward));
    memcpy(pose.up, aim.up, sizeof(pose.up));
    ScopeProjectionTangents lens{tanf(builtFov.horizontal) * kAimFrameCullingCover,
                                 tanf(builtFov.vertical) * kAimFrameCullingCover};
    const float* headProjection = reinterpret_cast<const float*>(headDerived + kReachDerivedProjectionOffset);
    ScopeProjectionTangents cover{1.0f / fabsf(headProjection[0]), 1.0f / fabsf(headProjection[5])};
    const float headAspect = cover.horizontal / cover.vertical;
    // The head turned from the aim past what one perspective camera can cover: the frame is out of sight anyway.
    if (!ExpandScopeCullTangents(reinterpret_cast<const float*>(frame.headCenter + 0x0C),
                                 reinterpret_cast<const float*>(frame.headCenter + 0x18), pose, lens, cover))
        return ReachAimFramePrepare::Off;
    const float vertical = 2.0f * atanf(fmaxf(cover.vertical, cover.horizontal / headAspect));
    if (!std::isfinite(vertical) || vertical >= 3.0f)
        return ReachAimFramePrepare::Off;
    alignas(16) unsigned char expanded[kReachCompactCameraBytes];
    alignas(16) unsigned char expandedDerived[kReachDerivedBlockSize];
    memcpy(expanded, frame.headCenter, sizeof(expanded));
    *reinterpret_cast<float*>(expanded + 0x28) = vertical;
    if (!g_reachHelpers.frustum(expanded, bounds))
        return ReachAimFramePrepare::Refused;
    for (float v : bounds)
        if (!std::isfinite(v))
            return ReachAimFramePrepare::Refused;
    g_reachHelpers.projection(expanded, bounds, expandedDerived, 0.0f);
    const float* widened = reinterpret_cast<const float*>(expandedDerived + kReachDerivedProjectionOffset);
    if (!std::isfinite(widened[0]) || !std::isfinite(widened[5]) || fabsf(widened[0]) <= 0.0f ||
        fabsf(widened[5]) <= 0.0f || 1.0f / fabsf(widened[0]) + 1.0e-4f < cover.horizontal ||
        1.0f / fabsf(widened[5]) + 1.0e-4f < cover.vertical)
        return ReachAimFramePrepare::Refused;

    // Commit only after all of it is valid.
    memcpy(frame.headCenter, expanded, sizeof(expanded));
    memcpy(headDerived, expandedDerived, sizeof(expandedDerived));
    ReachAimFrame& out = frame.aimFrame;
    out.frame = aim;
    out.frame.tanX = tanf(builtFov.horizontal);
    out.frame.tanY = tanf(builtFov.vertical);
    out.stereo = g_config.aim_frame_stereo;
    const float spacing = out.stereo ? g_config.aim_frame_3d_strength : 1.0f;
    out.scale = g_worldScale.load(std::memory_order_relaxed) * spacing;
    out.right[0] = aim.forward[1] * aim.up[2] - aim.forward[2] * aim.up[1];
    out.right[1] = aim.forward[2] * aim.up[0] - aim.forward[0] * aim.up[2];
    out.right[2] = aim.forward[0] * aim.up[1] - aim.forward[1] * aim.up[0];
    for (int eye = 0; eye < 2; ++eye)
        memcpy(out.eyeOffset[eye], tracking.eyes[eye].stereoPosition, sizeof(out.eyeOffset[eye]));
    // The eyes meet at the point the aim is on: Reach's own trace, in the mode that keeps one.
    float rangeM = 0.0f, rangeWu = 0.0f;
    bool hit = false;
    if (out.stereo && Game_ReachSightReticleRange(rangeM, hit) && hit)
        rangeWu = rangeM * kReachWorldUnitsPerMeter;
    out.meetWu = AimFrameMeetWu(kTitle, out.stereo, spacing, rangeWu);
    out.active = true;
    return ReachAimFramePrepare::Built;
}

// A frame's outcome, from the render thread. The run ending the frame gives the zoom back to the lens for this level.
void ReachAimFrameNote(bool refused)
{
    if (g_reachAimFrameRefusals.Note(refused))
    {
        g_reachAimFrameOffGeneration.store(g_reachCamera.generation.load(std::memory_order_acquire),
                                           std::memory_order_release);
        g_reachAimFrameGivenBack.fetch_add(1, std::memory_order_relaxed);
    }
}

void ReachPrepareAimFrame(ReachOwnerScope& frame, unsigned char* headDerived, const unsigned char* stockCompact,
                          const float* stockProjection, const ReachVrRenderSnapshot& tracking)
{
    frame.aimFrame = {};
    __try
    {
        const ReachAimFramePrepare prepared =
            ReachPrepareAimFrameBody(frame, headDerived, stockCompact, stockProjection, tracking);
        if (prepared == ReachAimFramePrepare::Refused)
            ReachAimFrameNote(true);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        frame.aimFrame = {};
        g_reachAimFrameOffGeneration.store(g_reachCamera.generation.load(std::memory_order_acquire),
                                           std::memory_order_release);
        g_reachAimFrameGivenBack.fetch_add(1, std::memory_order_relaxed);
    }
}

// This pass's camera: `compact` holds the head centre. No head orientation and no cant: the frame is a flat screen
// with one frustum for both eyes. In 3D each eye stands off along the aim's basis and turns in to where they meet.
void ReachAimFrameEyeCamera(unsigned char* compact, const unsigned char* center, const ReachAimFrame& aimFrame,
                            uint32_t eye)
{
    const AimFrame& frame = aimFrame.frame;
    memcpy(compact + 0x0C, frame.forward, sizeof(frame.forward));
    memcpy(compact + 0x18, frame.up, sizeof(frame.up));
    *reinterpret_cast<float*>(compact + 0x28) = 2.0f * atanf(frame.tanY);
    if (!aimFrame.stereo)
        return;
    const float* centre = reinterpret_cast<const float*>(center);
    float* position = reinterpret_cast<float*>(compact);
    aim_frame::EyePosition(centre, aimFrame.right, frame.up, frame.forward, aimFrame.eyeOffset[eye], aimFrame.scale,
                           position);
    if (aimFrame.meetWu > 0.0f)
        AimFrameTurnEyeIn(centre, position, frame, aimFrame.meetWu, reinterpret_cast<float*>(compact + 0x0C),
                          reinterpret_cast<float*>(compact + 0x18));
}

// The cold worker. The offscreen target a crosshair that must not be held is drawn into while the frame hands back
// (ReachHudDrawWidgetDetour); without it the capture simply carries on as it does without the frame.
void ReachAimFrameColdTick()
{
    const uint32_t given = g_reachAimFrameGivenBack.exchange(0, std::memory_order_relaxed);
    if (given)
        LOG("Reach Aim Frame Zoom StockFallback: the frame kept being refused; the lens has the zoom for this level "
            "and the camera core is retained");
    static uint32_t preparedGeneration = 0;
    static uint64_t lastTryMs = 0;
    const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
    if (!g_config.aim_frame_zoom || !generation || preparedGeneration == generation)
        return;
    const uint64_t now = GetTickCount64();
    if (now - lastTryMs < 1000)
        return;
    lastTryMs = now;
    if (VR_PrepareAuthoredReticleSuppressionResources())
        preparedGeneration = generation;
}
