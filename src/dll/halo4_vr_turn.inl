// The heading reference turned by `delta` (radians): an add, so a turn of the reference elsewhere in the same moment
// (the vehicle cameras' tethering, on the render thread) is never overwritten.
void Halo4AddGameYawReference(float delta)
{
    float reference = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
    while (!g_halo4Camera.gameYawReference.compare_exchange_weak(reference, WrapPi(reference + delta),
                                                                 std::memory_order_relaxed))
    {
    }
}

void Halo4ApplyVrTurn(const VrPadState& pad)
{
    if (!Halo4ControllerAimActive() || !pad.valid)
        return;
    // Same sub-frame timebase reasoning as Halo 3's ApplyVrTurn: this runs
    // several times per frame, and GetTickCount's ~15.6 ms granularity
    // turned smooth turn into a visible ~5 Hz stutter there.
    static LARGE_INTEGER freq{}, last{};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = last.QuadPart == 0
        ? 0.0f
        : (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
    last = now;
    if (dt > 0.1f) dt = 0.1f;

    const float x = pad.turnX; // stick right = turn right = yaw decreases
    static bool snapLatched = false;
    Halo4VehicleInputState seat{};
    // The camera hook's seat stamp: this input-side caller has no engine TLS of its own to read the seat with.
    const bool seated=g_config.vehicle_smooth_turn&&Halo4ReadSeatStamp(seat)&&seat.seated;
    if (UseSmoothVrTurn(g_config.turn_smooth,g_config.vehicle_smooth_turn,seated))
    {
        Halo3ConsumeSnapTurn(false, x, snapLatched);
        if (fabsf(x) > 0.15f)
        {
            Halo4AddGameYawReference(-x * (g_config.turn_smooth_deg_s / 57.2958f) * dt);
            g_halo4Camera.vrTurns.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if (Halo3ConsumeSnapTurn(true, x, snapLatched))
    {
        Halo4AddGameYawReference(-(x > 0 ? 1.0f : -1.0f) * g_config.turn_snap_deg / 57.2958f);
        g_halo4Camera.vrTurns.fetch_add(1, std::memory_order_relaxed);
    }
}
