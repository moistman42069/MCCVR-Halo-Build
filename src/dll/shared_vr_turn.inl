void ApplyVrTurn(const VrPadState& pad)
{
    if (!g_vrAim.load())
        return;
    if (!pad.valid)
        return;
    // Smooth turn needs a sub-frame timebase. GetTickCount only updates on
    // the ~15.6 ms system tick, but this runs several times per 11 ms (90 Hz)
    // frame from CamCopyHook, so a GetTickCount delta was 0 on most frames
    // and ~15 ms in a lump on others -> a visible ~5 Hz yaw stutter. The
    // performance counter gives the true elapsed time between calls, so the
    // yaw advances evenly regardless of how many calls land in a frame.
    static LARGE_INTEGER freq{}, last{};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = last.QuadPart == 0 ? 0.0f
                   : (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
    last = now;
    if (dt > 0.1f) dt = 0.1f;

    // The stick remains Halo's steering fallback in an authored driver
    // seat. While the two-hand wheel is actively supplying steering, that
    // stick is free and resumes the configured snap/smooth VR turn. Keep
    // the QPC clock warm across both states so taking the wheel cannot
    // inherit a capped 100 ms turn step.
    const float x = pad.turnX; // stick right = turn right = yaw decreases
    // A seat the stick aims outright (vehicle_control = stick) keeps it too, in any seat.
    const bool turnOwnsStick = !vehicle_control::StickBusy(
        Halo3SeatAuthorsSteeringNow(), Halo3SeatStickAimsNow(), Game_Halo3VehicleWheelActive());
    static bool snapLatched = false;
    if (!turnOwnsStick)
    {
        Halo3ConsumeSnapTurn(false, x, snapLatched);
        return;
    }
    if (UseSmoothVrTurn(g_config.turn_smooth,g_config.vehicle_smooth_turn,
            g_config.vehicle_smooth_turn&&SharedVrTurnSeated()))
    {
        // Track the held/centred state across a runtime mode switch too.
        Halo3ConsumeSnapTurn(false, x, snapLatched);
        if (fabsf(x) > 0.15f)
            g_gameYawRef = WrapPi(g_gameYawRef -
                x * (g_config.turn_smooth_deg_s / 57.2958f) * dt);
    }
    else
    {
        if (Halo3ConsumeSnapTurn(true, x, snapLatched))
        {
            g_gameYawRef = WrapPi(g_gameYawRef -
                (x > 0 ? 1.0f : -1.0f) * g_config.turn_snap_deg / 57.2958f);
        }
    }
}
