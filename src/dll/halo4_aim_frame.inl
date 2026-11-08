// AIM FRAME ZOOM in Halo 4 (config.h aim_frame_zoom; game.cpp AimFrameThisFrame). Halo 3's experience: while the game
// is zoomed, the headset shows the game's zoomed picture as a frame centred on the aim.
//
// Halo 4's own way to it. Each pass writes the observer record and runs the native per-window setup, which is what
// the lens does (halo4_scope.inl), so the frame's cameras simply replace the eye cameras for this frame, and the
// setup's own copy of them still has to match byte for byte. The observer's field of view reaches the projection
// through the measured gain; the width is the engine's, so the picture has the shape it reads back as. The game's
// zoomed view is the stock observer's field of view through that gain. Halo 4's visibility comes from the same setup,
// with no cover of its own to widen: the lens has drawn this narrow an aimed pass without one, but whether anything
// goes missing at the frame's edges is for the headset to say.
std::atomic<uint32_t> g_halo4AimFrameOffGeneration{0};
std::atomic<uint32_t> g_halo4AimFrameGivenBack{0};
aim_frame::RefusalRun g_halo4AimFrameRefusals;   // the render thread

// A frame's outcome, from the render thread. The run ending the frame gives the zoom back to the lens for this level.
void Halo4AimFrameNote(bool refused)
{
    if (g_halo4AimFrameRefusals.Note(refused))
    {
        g_halo4AimFrameOffGeneration.store(g_halo4Camera.generation.load(std::memory_order_acquire),
                                           std::memory_order_release);
        g_halo4AimFrameGivenBack.fetch_add(1, std::memory_order_relaxed);
    }
}

// `root`: the game's own camera, before the headset pose (its aim and its zoomed field of view). `center`: the rendered
// centre eye. On a frame: `eyeCameras` are the frame's, and `observerFov` the field of view that asks for its picture.
bool Halo4AimFrameCameras(const Halo4FovCalibration& calibration, const Halo4CameraBasis& root,
                          const Halo4CameraBasis& center, const Halo4VrRenderSnapshot& snapshot, float worldScale,
                          bool theater, Halo4CameraBasis (&eyeCameras)[2], AimFrame& frame, float& observerFov)
{
    constexpr GameTitle kTitle = GameTitle::Halo4;
    float nativeX = 0.0f, nativeY = 0.0f;
    const bool native = !theater && calibration.learned &&
        aim_frame::Halo4NativeTangents(root.verticalFov, calibration.gain, calibration.ratio, nativeX, nativeY);
    // As the lens does: along the intent ray from the rendered eye while the converged aim is written, else Chief's aim.
    float aim[3]{root.forward[0], root.forward[1], root.forward[2]};
    float convergedForward[3]{};
    if (g_config.aim_frame_zoom && Sight_ScopeConvergedDirection(kTitle, convergedForward))
        memcpy(aim, convergedForward, sizeof(aim));
    if (!AimFrameThisFrame(kTitle, native, nativeX, nativeY, center.up, frame, aim, true))
    {
        (void)AimFrameMeetWu(kTitle, false, 1.0f);
        return false;
    }
    const bool stereo = g_config.aim_frame_stereo;
    const float spacing = stereo ? g_config.aim_frame_3d_strength : 1.0f;
    const float meetWu = AimFrameMeetWu(kTitle, stereo, spacing);
    if (!aim_frame::Halo4ObserverFovFor(frame.tanY, calibration.gain, observerFov))
    {
        Halo4AimFrameNote(true);
        return false;
    }
    const float right[3]{frame.forward[1] * frame.up[2] - frame.forward[2] * frame.up[1],
                         frame.forward[2] * frame.up[0] - frame.forward[0] * frame.up[2],
                         frame.forward[0] * frame.up[1] - frame.forward[1] * frame.up[0]};
    Halo4CameraBasis cameras[2]{};
    for (int eye = 0; eye < 2; ++eye)
    {
        Halo4CameraBasis& camera = cameras[eye];
        camera = center;
        memcpy(camera.forward, frame.forward, sizeof(camera.forward));
        memcpy(camera.up, frame.up, sizeof(camera.up));
        if (stereo)
        {
            aim_frame::EyePosition(center.position, right, frame.up, frame.forward,
                                   snapshot.stereoEyePosition[eye], worldScale * spacing, camera.position);
            if (meetWu > 0.0f)
                AimFrameTurnEyeIn(center.position, camera.position, frame, meetWu, camera.forward, camera.up);
        }
        if (!Halo4ValidateCameraBasis(camera))
        {
            Halo4AimFrameNote(true);
            return false;
        }
    }
    eyeCameras[0] = cameras[0];
    eyeCameras[1] = cameras[1];
    return true;
}

// The cold worker.
void Halo4AimFrameColdTick()
{
    const uint32_t given = g_halo4AimFrameGivenBack.exchange(0, std::memory_order_relaxed);
    if (given)
        LOG("Halo 4 Aim Frame Zoom StockFallback: the frame kept being refused; the lens has the zoom for this level "
            "and the stereo core is retained");
}
