// Included in game.cpp (anonymous namespace) right after the Halo 4 muzzle lane, whose view detour calls into it
// (forward-declared there), and before the dev seat witnesses (halo4_seat_sight.inl), which read what it
// publishes. EVERY BUILD.
//
// HALO 4 SEATED CONVERGENCE (the player key halo4_seat_converge, default 0). A Halo 4 seat's aim is bent so
// that the line the SIMULATION aims the seat along passes through the point the hand's intent ray hits, and the
// reticle sits on that point. The mechanism, the fixed point and its rate are in
// src/common/halo4_seat_converge_logic.h; this file is the engine half:
//
//   THE SAMPLE (Halo4SeatConvergeBeforeView / AfterView, from the muzzle lane's detour on the aim view 0x1DC9E4).
//   The game calls the view for the local unit from its control pass (its targeting, return addresses 0x1DB9A6
//   and 0x1DBDB6): once a rendered frame, ahead of that frame's simulation ticks (the main loop 0x60B84 calls
//   the control pass at 0x610EC and the tick loop at 0x61112). Those calls are sampled as the game made them --
//   the aim camera's position, the line's direction and where the game's own trace starts on it. While
//   one-machine changes are not allowed (coop_safe_shots) the first-person seat override is stood down for
//   exactly those calls and for the local seated fire's own, so the sample is the camera the fire aims from on
//   every machine; the control pass's call then also answers the game's own targeting as an unmodded game
//   would. Nothing is called to take the sample, and nothing is written but this lane's own record.
//
//   THE SEATED TRACE (Halo4SeatConvergeControlUpdate, from the direct-drive hook's stick call). The seat's
//   control update runs in that same control pass, where the game casts its own targeting rays (0x1DB840 ->
//   0x1DD998 -> 0x1DF988 -> PhysicsRayCast). The published seated intent ray is traced there with that query:
//   the vehicle's root, the occupant and the seat's parent ignored, our own vehicle looked past (three passes;
//   exhaustion is a MISS), a boarder kept. The object table is walked by reading it; the ray cast is the only
//   native call. (The unit the seat controls, which the game's own view asks 0x5F9694 for, is not asked for: that
//   would be a second native call. A part of our own vehicle is looked past whether or not it is named.)
//   Every direct caller up from the seat's control update (the accumulator 0xA8B34) leads to the main
//   loop's control pass and none to the tick; a control update that arrives inside a tick all the same, or while
//   the tick cannot be read, is counted and not traced.
//
//   THE POLL (Halo4SeatConvergePrepare / EndPoll, Game_ComputeAimStick): the solve's start from the newest
//   sample (halo4_seat_converge::SolveStart), held across a gap in the samples, and the first reason a poll does
//   not solve.
//
//   THE WITNESSES, counters only: each new sample's line against the point while solved; each local seated
//   fire's own aim line (the view's call from the fire's assist, return address 0x1DC8C4, inside the tick)
//   against the point; and the camera a tick-side call saw against the control pass's.
//
// NOTHING HERE RUNS A NATIVE CALL, CHANGES A RETURN VALUE OR WRITES GAME MEMORY INSIDE A SIMULATION TICK. The
// only outputs are the aim the direct drive writes into the control record and the stick -- shared input.
// The lane tells a tick from the control pass by the title's own in-tick byte (NativeVehicleFirstPerson_
// InSimulationTick). Where that byte cannot be read it neither samples nor traces, so no seat solves.

// ---- small pieces ---------------------------------------------------------------------------------------------
// A seqlock over a trivially copyable value, every word atomic: one writer, any reader; a torn read retries once,
// then reports nothing. Clear is any thread's and is not a write: it only withdraws the value until the next
// Publish, and never touches the sequence -- a Clear that landed inside a Publish would otherwise leave the
// sequence odd at rest and even during every later write, and a reader would then accept a torn value. (A
// Publish that a Clear lands inside shows its value again when it ends. No reader takes a value without holding
// it to the seat it names, and to its stamp and generation wherever its age matters.)
template <class T>
struct Halo4SeatBlob
{
    static constexpr size_t kWords = (sizeof(T) + 7) / 8;
    std::atomic<uint32_t> sequence{0};
    std::atomic<bool> live{false};
    std::atomic<uint64_t> words[kWords]{};
    void Publish(const T& value)
    {
        uint64_t raw[kWords]{};
        memcpy(raw, &value, sizeof(T));
        sequence.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_release);
        for (size_t i = 0; i < kWords; ++i)
            words[i].store(raw[i], std::memory_order_relaxed);
        sequence.fetch_add(1, std::memory_order_release);
        live.store(true, std::memory_order_release);
    }
    void Clear() { live.store(false, std::memory_order_release); }
    bool Read(T& out) const
    {
        if (!live.load(std::memory_order_acquire))
            return false;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            uint64_t raw[kWords]{};
            for (size_t i = 0; i < kWords; ++i)
                raw[i] = words[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence.load(std::memory_order_relaxed) != before)
                continue;
            memcpy(&out, raw, sizeof(T));
            return true;
        }
        return false;
    }
};

// One sample of the aim view for the local seat (the control pass's).
struct Halo4SeatCameraRecord
{
    uint32_t sequence = 0, generation = 0, unit = UINT32_MAX, parent = UINT32_MAX;
    int32_t seat = -1;
    uint32_t stock = 0;   // the game's own camera selector answered (the seat override stood down for the call)
    halo4_seat_converge::CameraSample camera{};
};

// The point the aim was last solved through, for the fire witness.
struct Halo4SeatSolvedPoint
{
    uint64_t stampUs = 0;
    uint32_t generation = 0, seatKey = 0;
    float point[3]{};
};

// The seat's control update as the stick call last saw it.
struct Halo4SeatControlRecord
{
    uint64_t ms = 0;        // the sight clock
    uint32_t seatKey = 0, flags = 0;
    uint32_t flagsKnown = 0;
};

// A window's sum and largest size of one signed quantity (milli-units), written by one thread, drained by the
// cold worker.
struct Halo4SeatSpread
{
    std::atomic<uint64_t> count{0}, sumAbsMilli{0};
    std::atomic<int64_t> sumMilli{0};
    std::atomic<uint32_t> maxAbsMilli{0};
    void Add(float value)
    {
        if (!std::isfinite(value))
            return;
        const float size = std::fabs(value) * 1000.0f;
        const uint32_t milli = size > 4.0e9f ? 4000000000u : static_cast<uint32_t>(size);
        count.fetch_add(1, std::memory_order_relaxed);
        sumAbsMilli.fetch_add(milli, std::memory_order_relaxed);
        sumMilli.fetch_add(value < 0.0f ? -static_cast<int64_t>(milli) : static_cast<int64_t>(milli),
                           std::memory_order_relaxed);
        if (milli > maxAbsMilli.load(std::memory_order_relaxed))
            maxAbsMilli.store(milli, std::memory_order_relaxed);
    }
    struct Taken
    {
        uint64_t count = 0;
        double meanAbs = 0.0, mean = 0.0, maxAbs = 0.0;
    };
    Taken Take()
    {
        Taken t{};
        t.count = count.exchange(0, std::memory_order_relaxed);
        const uint64_t sumAbs = sumAbsMilli.exchange(0, std::memory_order_relaxed);
        const int64_t sum = sumMilli.exchange(0, std::memory_order_relaxed);
        t.maxAbs = double(maxAbsMilli.exchange(0, std::memory_order_relaxed)) / 1000.0;
        if (t.count)
        {
            t.meanAbs = double(sumAbs) / 1000.0 / double(t.count);
            t.mean = double(sum) / 1000.0 / double(t.count);
        }
        return t;
    }
};

struct Halo4SeatConvergeRuntime
{
    // Written by the install, read on the game's threads (a level reload reinstalls under live detours).
    std::atomic<uintptr_t> base{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> ready{false};   // the aim camera's anchors proved on this image
    // THE SAMPLE. One writer in practice (the control pass's thread); the try-lock only keeps a second caller of
    // the view, should one ever appear, from tearing the track: its call is then not sampled.
    std::atomic_flag sampling = ATOMIC_FLAG_INIT;
    Halo4SeatBlob<Halo4SeatCameraRecord> sample;
    shot_lag::EyeTrack track;         // the eye's track, the sampling thread's
    uint32_t trackUnit = UINT32_MAX, trackParent = UINT32_MAX, sampleSequence = 0;
    int32_t trackSeat = -1;
    Halo4SeatCameraRecord last{};     // the sampling thread's own copy of the newest sample
    // THE CONTROL UPDATE and the seated trace (the stick call's thread).
    Halo4SeatBlob<Halo4SeatControlRecord> control;
    uint64_t lastTraceMs = 0;
    // The seated trace's own fault latch. A native fault in a seat's ray stops the seated trace until the title
    // reinstalls and leaves the on-foot trace, which has its own latch (Sight_TraceFaulted), running.
    std::atomic<bool> traceFaulted{false};
    // THE SOLVED POINT (the aim path's thread writes, the fire witness reads).
    Halo4SeatBlob<Halo4SeatSolvedPoint> solved;
    // Counters since the last report.
    std::atomic<uint64_t> samples{0}, samplesStock{0}, samplesTickUnread{0}, samplesFollowing{0}, pushMilliWu{0},
        samplerBusy{0}, tickSideCalls{0};
    std::atomic<uint64_t> controlUpdates{0}, controlInTick{0}, controlTickUnread{0}, traces{0}, traceHits{0},
        traceMisses{0}, lookPasts{0}, exhausted{0}, noRay{0}, otherRay{0}, rootFallbacks{0}, costTicks{0},
        costMaxTicks{0}, faults{0};
    std::atomic<uint64_t> polls{0}, why[static_cast<size_t>(halo4_seat_converge::Why::Count)]{}, heldPolls{0},
        ageMilliMs{0}, ageSamples{0};
    std::atomic<uint32_t> ageMaxMilliMs{0}, eyeSpeedMilliMps{0}, lastFlags{0}, lastFlagsKnown{0};
    std::atomic<uint64_t> fires{0}, firesInTick{0}, firesFollowing{0}, firesNoPoint{0};
    // New samples whose heading residual lay on the other side of the point from the last one's (both over 0.2
    // deg). A hand sweeping back and forth makes a few; a solve that starts past the camera's pivot makes one
    // nearly every sample.
    std::atomic<uint64_t> sampleSideChanges{0};
    Halo4SeatSpread sampleYaw, samplePitch, fireYaw, firePitch, fireMm, fireAlongMm, fireImpliedMs, tickSideMs;
} g_h4SeatConverge;

// The player key, live.
inline bool Halo4SeatConvergeOn()
{
    return g_config.halo4_seat_converge != 0;
}

// A dev build also runs the sample, the seated ray and the seated trace with the key off, bending nothing, so
// its witnesses (halo4_seat_sight.inl) have a seated trace to measure against.
inline bool Halo4SeatConvergeMeasures()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    return true;
#else
    return Halo4SeatConvergeOn();
#endif
}

// No seat can be traced: the seated trace's own latch, or the aim's. Nothing is solved for a seat then, so
// nothing is stood down for one either.
inline bool Halo4SeatConvergeTraceOff()
{
    return g_h4SeatConverge.traceFaulted.load(std::memory_order_acquire) || Sight_TraceFaulted(GameTitle::Halo4);
}

// The seat as every lane keys it (the direct drive's own key for the seat; never 0, which is the on-foot stance).
inline uint32_t Halo4SeatConvergeKey(uint32_t parent, int seat)
{
    const uint32_t key = seat_drive::SeatKey(static_cast<int>(parent), seat, false);
    return key ? key : 1u;
}

// ---- the sample (the aim view's detour) -----------------------------------------------------------------------
// Before the native view call. `caller`: the detour's own return address.
Halo4SeatConvergeCall Halo4SeatConvergeBeforeView(uintptr_t caller, uint32_t unit)
{
    Halo4SeatConvergeCall call{};
    const auto& f = g_h4SeatConverge;
    if (!Halo4SeatConvergeMeasures() || !f.ready.load(std::memory_order_acquire))
        return call;
    const uintptr_t base = g_halo4Barrel.base;
    if (!base || base != f.base)
        return call;
    // ADDR-HYGIENE: resolved -- the three return addresses below are the muzzle lane's own call edges into the
    // view, proven at its install on this image; this lane is ready only after that lane is, on the same base.
    const bool targeting = caller == base + 0x1DB9A6 || caller == base + 0x1DBDB6;
    // The fire's own assist (0x1DC86C's call), and only inside a native weapon fire on this thread: the assist
    // has other callers.
    const bool fire = caller == base + 0x1DC8C4 && g_halo4BarrelRequest.lease != nullptr;
    if ((!targeting && !fire) || g_halo4MuzzleOwnedQuery || g_halo4BarrelShot.active)
        return call;
    // The local seated unit only (one relaxed load for every other unit): a remote player's unit is never
    // sampled, and never stood down for.
    if (g_halo4SeatStamp.unit.load(std::memory_order_relaxed) != unit ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo4 ||
        f.generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
        return call;
    Halo4VehicleInputState stamp{};
    if (!Halo4ReadSeatStamp(stamp) || !stamp.seated || stamp.unit != unit || stamp.parent == UINT32_MAX ||
        stamp.seat < 0)
        return call;
    call.seat = stamp;
    call.sample = targeting;
    call.fire = fire;
    // While one-machine changes are not allowed, the seat's aim line is the game's own camera's: the control
    // pass's call is answered by it, and so is the fire's. Inside a tick the seat override already stands down
    // by the title's in-tick byte, so for the fire this changes nothing while that byte is being written; the
    // lane does not rest on it, since the sample and the fire must be answered by the same camera. Only with the
    // key on and the seat's aim the hand's (converged_aim, reach_vehicle_aim): a dev build measuring with the key
    // off, a seat nothing is solved for, and a seated trace latched off leave both calls exactly as they were.
    if (halo4_seat_converge::SampleWithGameCamera(
            Halo4SeatConvergeOn() && !Halo4SeatConvergeTraceOff() && Sight_AimEnabled() &&
                g_config.reach_vehicle_aim > 0,
            Sight_FireTimeChangesAllowed()))
    {
        NativeVehicleFirstPerson_SetStockOnThisThread(true);
        call.stock = true;
    }
    return call;
}

// The detour's unwind: a view call that raised leaves no stand-down behind.
void Halo4SeatConvergeViewUnwound(Halo4SeatConvergeCall& call)
{
    if (call.stock)
    {
        NativeVehicleFirstPerson_SetStockOnThisThread(false);
        call.stock = false;
    }
}

bool Halo4SeatConvergeFinite3(const float* v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// View calls closer than this belong to one control pass (passes are a rendered frame apart: 4 ms at 240 fps).
inline constexpr uint64_t kHalo4SeatSamePassUs = 2000;

// A control-pass sample (a targeting call made outside a simulation tick): published for the solve. The sampling
// thread.
void Halo4SeatConvergeNoteSample(const Halo4SeatConvergeCall& call, uint32_t unit, const float* origin,
                                 const float* dir, const float* camera, bool stock)
{
    auto& f = g_h4SeatConverge;
    if (f.sampling.test_and_set(std::memory_order_acquire))
    {
        f.samplerBusy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint64_t nowUs = Sight_NowUs();
    if (f.trackUnit != unit || f.trackParent != call.seat.parent || f.trackSeat != call.seat.seat ||
        (f.last.camera.valid && (f.last.stock != 0) != stock))
    {
        // Another seat (or the first sample), or the other camera answering now (the key, co-op safe shots or an
        // aim switch changed live): this eye is no continuation of the last one, and a velocity across the two
        // would be the jump between them.
        f.track.Reset();
        f.trackUnit = unit;
        f.trackParent = call.seat.parent;
        f.trackSeat = call.seat.seat;
        f.last = {};
    }
    else if (f.last.camera.valid && nowUs >= f.last.camera.stampUs &&
             nowUs - f.last.camera.stampUs < kHalo4SeatSamePassUs)
    {
        // One control pass calls the view several times, microseconds apart, for the same camera: the first of
        // them is the pass's sample.
        f.sampling.clear(std::memory_order_release);
        return;
    }
    Halo4SeatCameraRecord r{};
    r.sequence = ++f.sampleSequence;
    r.generation = f.generation;
    r.unit = unit;
    r.parent = call.seat.parent;
    r.seat = call.seat.seat;
    r.stock = stock ? 1u : 0u;
    r.camera.valid = true;
    r.camera.stampUs = nowUs;
    memcpy(r.camera.camera, camera, sizeof(r.camera.camera));
    memcpy(r.camera.dir, dir, sizeof(r.camera.dir));
    memcpy(r.camera.eye, origin, sizeof(r.camera.eye));
    f.track.Push(nowUs, r.camera.eye);
    r.camera.eyeVelValid = f.track.Velocity(120000, 30000, r.camera.eyeVel);
    f.last = r;
    f.sample.Publish(r);
    f.sampling.clear(std::memory_order_release);
    f.samples.fetch_add(1, std::memory_order_relaxed);
    if (stock)
        f.samplesStock.fetch_add(1, std::memory_order_relaxed);
    const float push = halo4_seat_converge::PushAlongLine(r.camera);
    if (push > 0.0f)
    {
        f.samplesFollowing.fetch_add(1, std::memory_order_relaxed);
        f.pushMilliWu.fetch_add(static_cast<uint64_t>(push * 1000.0f), std::memory_order_relaxed);
    }
    if (r.camera.eyeVelValid)
        f.eyeSpeedMilliMps.store(static_cast<uint32_t>(std::fmin(
            shot_lag::Length(r.camera.eyeVel) * shot_lag::kMetresPerWu * 1000.0f, 4.0e9f)),
            std::memory_order_relaxed);
}

// A call the game made INSIDE a simulation tick (its live target track, 0x5D0440 -> 0x1DBD00): never the
// solve's sample -- inside a tick the camera's pivot is the simulation's own, outside one the frame-interpolated
// one, and one track must not mix the two. Witness only: how far that camera stands along its travel from the
// control pass's newest, carried by its age alone. That is the interpolation's share of the carry
// (halo4_seat_converge::kCarryTicks), measured.
void Halo4SeatConvergeNoteTickSide(uint32_t unit, const float* origin, const float* dir)
{
    auto& f = g_h4SeatConverge;
    f.tickSideCalls.fetch_add(1, std::memory_order_relaxed);
    if (f.sampling.test_and_set(std::memory_order_acquire))
        return;
    const Halo4SeatCameraRecord last = f.last;
    f.sampling.clear(std::memory_order_release);
    const uint64_t nowUs = Sight_NowUs();
    if (!last.camera.valid || last.unit != unit || !last.camera.eyeVelValid || nowUs < last.camera.stampUs ||
        nowUs - last.camera.stampUs > halo4_seat_converge::kSampleFreshUs)
        return;
    const float speed = shot_lag::Length(last.camera.eyeVel);
    // Only while the eye travels (over 2 m/s) and the aim has not turned between the two (the eye swings with it).
    if (!std::isfinite(speed) || speed < 2.0f * shot_lag::kWuPerMetre ||
        seat_upstream::AngleDeg(last.camera.dir, dir) > 0.5f)
        return;
    const float ageS = float(nowUs - last.camera.stampUs) * 1.0e-6f;
    float along = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        along += (origin[axis] - (last.camera.eye[axis] + last.camera.eyeVel[axis] * ageS)) *
                 last.camera.eyeVel[axis];
    f.tickSideMs.Add(along / (speed * speed) * 1000.0f);
}

// The local seated fire's own aim line, as the fire's assist was handed it (inside the tick, before the game's
// own magnetism turns it): how far it passes from the point the aim was solved through. Reads only.
void Halo4SeatConvergeNoteFire(const Halo4SeatConvergeCall& call, const float* origin, const float* dir,
                               const float* camera, int inTick)
{
    auto& f = g_h4SeatConverge;
    f.fires.fetch_add(1, std::memory_order_relaxed);
    if (inTick == 1)
        f.firesInTick.fetch_add(1, std::memory_order_relaxed);
    const float pushed[3]{origin[0] - camera[0], origin[1] - camera[1], origin[2] - camera[2]};
    if (pushed[0] * pushed[0] + pushed[1] * pushed[1] + pushed[2] * pushed[2] > 1.0e-8f)
        f.firesFollowing.fetch_add(1, std::memory_order_relaxed);
    Halo4SeatSolvedPoint solved{};
    const bool solvedRead = f.solved.Read(solved);
    const uint64_t nowUs = Sight_NowUs();   // after the read: a point published meanwhile is not from the future
    if (!solvedRead || solved.generation != f.generation ||
        solved.seatKey != Halo4SeatConvergeKey(call.seat.parent, call.seat.seat) || nowUs < solved.stampUs ||
        nowUs - solved.stampUs > kSightFreshMs * 1000ull)
    {
        f.firesNoPoint.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // The travel: the control pass's eye velocity, when the newest sample has one.
    float velocity[3]{};
    bool velocityValid = false;
    if (!f.sampling.test_and_set(std::memory_order_acquire))
    {
        if (f.last.camera.valid && f.last.camera.eyeVelValid && f.last.unit == call.seat.unit)
        {
            memcpy(velocity, f.last.camera.eyeVel, sizeof(velocity));
            velocityValid = true;
        }
        f.sampling.clear(std::memory_order_release);
    }
    const halo4_seat_converge::LineMiss miss =
        halo4_seat_converge::MeasureLine(camera, dir, solved.point, velocityValid ? velocity : nullptr);
    if (!miss.valid)
    {
        f.firesNoPoint.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    f.fireYaw.Add(miss.yawDeg);
    f.firePitch.Add(miss.pitchDeg);
    f.fireMm.Add(miss.mm);
    if (miss.travel)
    {
        f.fireAlongMm.Add(miss.alongMm);
        f.fireImpliedMs.Add(miss.impliedMs);
    }
}

// After the native view call, before anything substitutes its outputs.
void Halo4SeatConvergeAfterView(Halo4SeatConvergeCall& call, uint32_t unit, const float* origin, const float* dir,
                                const float* camera)
{
    const bool stock = call.stock;
    Halo4SeatConvergeViewUnwound(call);
    if ((!call.sample && !call.fire) || !origin || !dir || !camera || !Halo4SeatConvergeFinite3(origin) ||
        !Halo4SeatConvergeFinite3(dir) || !Halo4SeatConvergeFinite3(camera))
        return;
    const int inTick = NativeVehicleFirstPerson_InSimulationTick(GameTitle::Halo4);
    if (call.fire)
        Halo4SeatConvergeNoteFire(call, origin, dir, camera, inTick);
    else if (inTick == 0)
        Halo4SeatConvergeNoteSample(call, unit, origin, dir, camera, stock);
    else if (inTick == 1)
        Halo4SeatConvergeNoteTickSide(unit, origin, dir);
    else
        // The tick cannot be read, so which side of it this call was made on is not known: it is neither the
        // sample (a tick-side call must never enter the sample's track) nor the tick-side witness.
        g_h4SeatConverge.samplesTickUnread.fetch_add(1, std::memory_order_relaxed);
}

// ---- the seated trace (the stick call) ------------------------------------------------------------------------
// This thread's object table entry, read and salt-checked without a native call: the engine TLS block's table
// at +0x18 (stride 0x18: salt word +0, type byte +4, data +0x10), as the title's own accessor 0x43AD8 walks it.
// Guarded. False: no such object now.
// ADDR-HYGIENE: guarded -- the layout is a reading of one build, so nothing is taken on trust: a table that is
// not valid or whose stride word is not 0x18, an index past its count, a salt that does not match, a type past
// the engine's range and an unreadable page all read as "no such object". The trace then names the seat's parent
// as the root, and a hit it cannot place is kept as the hit (never looked past).
bool Halo4SeatConvergeObject(const uint8_t* tls, uint32_t handle, const uint8_t*& data, int& type)
{
    __try
    {
        const auto* table = tls ? Halo4VehicleRead<const uint8_t*>(tls, 0x18) : nullptr;
        if (!table || handle == UINT32_MAX || !(handle >> 16) || !table[0x31] ||
            Halo4VehicleRead<uint64_t>(table, 0x20) != 0x18)
            return false;
        const auto count = Halo4VehicleRead<int32_t>(table, 0x44);
        const auto storage = Halo4VehicleRead<uintptr_t>(table, 0x50);
        if (count <= 0 || count > 0x10000 || (handle & 0xFFFF) >= static_cast<uint32_t>(count) || !storage)
            return false;
        const auto* entry = reinterpret_cast<const uint8_t*>(storage) + (handle & 0xFFFF) * 0x18;
        if (Halo4VehicleRead<uint16_t>(entry, 0) != static_cast<uint16_t>(handle >> 16) || entry[4] >= 32)
            return false;
        data = Halo4VehicleRead<const uint8_t*>(entry, 0x10);
        type = entry[4];
        return data != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The root of an object's parent chain (+0x24, the link the seat readers walk). -1 on a stale link or a chain
// deeper than any vehicle. Guarded. ADDR-HYGIENE: guarded -- every link is an object the table check above
// accepted, and a failed walk is reported as -1, never as a root.
int32_t Halo4SeatConvergeRootOf(const uint8_t* tls, int32_t object)
{
    if (object == -1)
        return -1;
    int32_t current = object;
    for (int depth = 0; depth < 8; ++depth)
    {
        const uint8_t* data = nullptr;
        int type = -1;
        if (!Halo4SeatConvergeObject(tls, static_cast<uint32_t>(current), data, type))
            return -1;
        int32_t parent = -1;
        __try
        {
            parent = Halo4VehicleRead<int32_t>(data, 0x24);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
        if (parent == -1)
            return current;
        current = parent;
    }
    return -1;
}

int Halo4SeatConvergeTypeOf(const uint8_t* tls, int32_t object)
{
    const uint8_t* data = nullptr;
    int type = -1;
    return object != -1 && Halo4SeatConvergeObject(tls, static_cast<uint32_t>(object), data, type) ? type : -1;
}

// One native ray, guarded. False: a fault (the trace latches off).
bool Halo4SeatConvergeRayCast(Halo4PhysicsRayCastInput* input, Halo4PhysicsRayCastResult* output, bool& returned)
{
    __try
    {
        returned = g_halo4WorldCollision.originalRayCast(input, output);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// The dev shot witness's reference (halo4_seat_sight.inl): each seated trace's own result and the ray it traced.
void Halo4PublishSeatTrace(uint32_t generation, bool hit, float rangeWu, const SightRay& ray);
#endif

// The seated intent ray, traced. `tls`: this thread's engine TLS block; `seat`: the record's own unit, read on
// this thread. `publish`: the key is on (a dev build measuring with it off keeps the result to itself).
void Halo4SeatConvergeTrace(const uint8_t* tls, const Halo4VehicleInputState& seat, uint32_t seatKey, bool publish)
{
    constexpr GameTitle kTitle = GameTitle::Halo4;
    auto& f = g_h4SeatConverge;
    if (!LegacySight(kTitle).flags.load(std::memory_order_acquire) || !g_halo4Contact.flags ||
        !g_halo4WorldCollision.originalRayCast || Halo4SeatConvergeTraceOff())
        return;
    // The seat's own rate limit, on the sight clock: the on-foot trace's slot is not this one's to take.
    const uint64_t nowMs = Sight_NowMs();
    if (f.lastTraceMs && nowMs >= f.lastTraceMs && nowMs - f.lastTraceMs < kSightTraceIntervalMs)
        return;
    const uint32_t generation = TitleAdapter_GetGeneration(kTitle);
    SightRay ray{};
    if (!Sight_ReadRay(kTitle, generation, kSightRayFreshMs, ray))
    {
        f.noRay.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (ray.ignoreB != static_cast<int32_t>(seat.parent) || ray.unit != static_cast<int32_t>(seat.unit))
    {
        // A ray the aim path did not publish for this seat (the on-foot ray, or another seat's): not traced.
        f.otherRay.fetch_add(1, std::memory_order_relaxed);
        LegacySight(kTitle).seatUnnamed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    f.lastTraceMs = nowMs;
    const int32_t unit = static_cast<int32_t>(seat.unit);
    int32_t root = Halo4SeatConvergeRootOf(tls, unit);
    if (root == -1 || root == unit)
    {
        // The walk failed: the seat's parent.
        root = static_cast<int32_t>(seat.parent);
        f.rootFallbacks.fetch_add(1, std::memory_order_relaxed);
        LegacySight(kTitle).rootFallbacks.fetch_add(1, std::memory_order_relaxed);
    }
    int32_t chain[4]{};
    const int chainCount = halo4_seat_aim::BuildSeatIgnores(root, unit, static_cast<int32_t>(seat.parent), -1, chain);
    uint64_t collisionFlags = g_halo4SightShotFlags.load(std::memory_order_acquire);
    if (!collisionFlags)
        memcpy(&collisionFlags, g_halo4Contact.flags, sizeof(collisionFlags));
    LARGE_INTEGER before{}, after{};
    int32_t lastObject = -1;   // the object the last pass stopped on (-1: structure, or nothing)
    QueryPerformanceCounter(&before);
    const auto castFrom = [&](const float origin[3], const float dir[3], float alongWu, float remainingWu,
                              int32_t* objectOut) {
        halo4_seat_aim::TracePass pass{};
        Halo4PhysicsRayCastInput input{};
        Halo4PhysicsRayCastResult output{};
        input.profile = 0x1A;
        memcpy(&input.collisionFlags, &collisionFlags, sizeof(collisionFlags));
        for (int axis = 0; axis < 3; ++axis)
        {
            input.start[axis] = origin[axis] + dir[axis] * alongWu;
            input.end[axis] = input.start[axis] + dir[axis] * remainingWu;
        }
        for (int i = 0; i < chainCount; ++i)
            input.ignoredObjects[i] = chain[i];
        input.ignoredObjectCount = static_cast<uint8_t>(chainCount);
        input.resultOptions[0] = 1;
        bool returned = false;
        pass.completed = Halo4SeatConvergeRayCast(&input, &output, returned);
        pass.hit = returned;
        pass.fraction = output.fraction;
        // An object hit (type 4, as the contact backend reads it) names its handle; anything else is structure.
        pass.object = output.type == 4 ? output.objectIndex : -1;
        if (objectOut)
            *objectOut = returned ? pass.object : -1;
        return pass;
    };
    // THE BOARDER RULE (sight_hit::LookPastOwnHierarchy): PhysicsRayCast's own predicate already skips everything
    // that descends from an ignored object except a boarder, so a biped of our hierarchy that is not our own unit is
    // a target, not a part of our vehicle: it is handed on with a root that is not ours (-2), and the look-past
    // keeps it as the hit. Everything else of ours is still looked past.
    const auto rootOf = [&](int32_t object) {
        const int32_t hitRoot = Halo4SeatConvergeRootOf(tls, object);
        if (hitRoot != root)
            return hitRoot;
        const int type = Halo4SeatConvergeTypeOf(tls, object);
        if (!sight_hit::LookPastOwnHierarchy(true, object == unit, type))
            return -2;
        Sight_NoteLookPast(kTitle, Sight_ObjectHitClass(type));   // dev: what was stepped past, by class
        return hitRoot;
    };
    const auto result = halo4_seat_aim::TraceLookingPast(
        kSightTraceLengthWu, kSightTraceSkipWu, kLegacySightSelfStepWu, kLegacySightSelfPasses, root, chain,
        chainCount,
        [&](float alongWu, float remainingWu) { return castFrom(ray.head, ray.dir, alongWu, remainingWu, &lastObject); },
        rootOf);
    QueryPerformanceCounter(&after);
    if (!result.completed)
    {
        f.traceFaulted.store(true, std::memory_order_release);
        f.faults.fetch_add(1, std::memory_order_relaxed);
        LOG("SIGHT Halo 4 seated trace: the native ray raised a fault; the seated trace stays off until the title "
            "reinstalls (the on-foot trace is untouched)");
        return;
    }
    const uint64_t cost = static_cast<uint64_t>(after.QuadPart - before.QuadPart);
    f.traces.fetch_add(1, std::memory_order_relaxed);
    (result.hit ? f.traceHits : f.traceMisses).fetch_add(1, std::memory_order_relaxed);
    f.lookPasts.fetch_add(static_cast<uint64_t>(result.selfHits), std::memory_order_relaxed);
    if (result.exhausted)
        f.exhausted.fetch_add(1, std::memory_order_relaxed);
    f.costTicks.fetch_add(cost, std::memory_order_relaxed);
    if (cost > f.costMaxTicks.load(std::memory_order_relaxed))
        f.costMaxTicks.store(cost, std::memory_order_relaxed);
    auto& legacy = LegacySight(kTitle);
    legacy.seatedTraces.fetch_add(1, std::memory_order_relaxed);
    legacy.selfHits.fetch_add(static_cast<uint64_t>(result.selfHits), std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // SIGHT HITS (dev): what the seated ray stopped on, by class (the last pass's object when it was kept as the
    // hit), and the dev shot witness's reference.
    {
        const bool objectHit = result.hit && lastObject != -1;
        const int type = objectHit ? Halo4SeatConvergeTypeOf(tls, lastObject) : -1;
        Sight_NoteTraceHit(kTitle, sight_hit::ClassifyHit(result.hit, objectHit, type), type,
                           result.hit ? result.rangeWu : 0.0f, true);
        Halo4PublishSeatTrace(generation, result.hit, result.rangeWu, ray);
    }
#endif
    // Not publishing (a dev build measuring with the key off): the result above went to the dev witnesses, and
    // the rays below, which belong to a seat that solves, are not traced.
    if (!publish)
        return;
    // This seat's stance first: a range measured on foot, or in another seat, is withdrawn before this one is
    // published, so it never bends this seat's aim.
    Sight_SetStance(kTitle, seatKey);
    Sight_PublishTrace(kTitle, generation, result.hit, result.hit ? result.rangeWu : 0.0f, cost);
    // AUXILIARY RAYS (sight_layer.h SightAuxRay), looking past our own vehicle exactly as the intent trace does.
    // False: a native fault -- the caller latches its own trace off.
    const auto auxTrace = [&](const SightAuxRay& aux, bool& hit, float& rangeWu) {
        const auto auxResult = halo4_seat_aim::TraceLookingPast(
            kSightTraceLengthWu, kSightTraceSkipWu, kLegacySightSelfStepWu, kLegacySightSelfPasses, root, chain,
            chainCount,
            [&](float alongWu, float remainingWu) { return castFrom(aux.origin, aux.dir, alongWu, remainingWu, nullptr); },
            [&](int32_t object) { return Halo4SeatConvergeRootOf(tls, object); });
        if (!auxResult.completed)
            return false;
        hit = auxResult.hit;
        rangeWu = auxResult.hit ? auxResult.rangeWu : 0.0f;
        return true;
    };
    // THE SEAT LIMIT RAY: while the seat's reticle is clamped at a natural stop, the ray its gun really aims along.
    SightAuxRay limit{};
    if (Sight_ReadLimitRay(kTitle, generation, kSightRayFreshMs, limit))
    {
        bool limitHit = false;
        float limitRangeWu = 0.0f;
        if (!auxTrace(limit, limitHit, limitRangeWu))
        {
            f.traceFaulted.store(true, std::memory_order_release);
            f.faults.fetch_add(1, std::memory_order_relaxed);
            LOG("SIGHT Halo 4 seated seat limit trace: the native ray raised a fault; the seated trace stays off "
                "until the title reinstalls (the on-foot trace is untouched)");
            return;
        }
        Sight_PublishLimitTrace(kTitle, generation, limitHit, limitRangeWu);
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    // DEV ASSESSMENT (honest_reticle_assess): the game's own aim from the seat's eye. A native fault here latches only
    // the ASSESSMENT trace off (Sight_NoteAssessTraceFault); the seat's intent trace and its fault count are the aim's.
    SightAssessRay assess{};
    if (!Sight_AssessTraceFaulted(kTitle) && Sight_ReadAssessRay(kTitle, generation, kSightRayFreshMs, assess))
    {
        bool assessHit = false;
        float assessRangeWu = 0.0f;
        if (!auxTrace(assess, assessHit, assessRangeWu))
        {
            Sight_NoteAssessTraceFault(kTitle);
            return;
        }
        Sight_PublishAssessTrace(kTitle, generation, assessHit, assessRangeWu);
    }
    // [assess-only end]
#endif
}

// The direct-drive hook's stick call for a record whose own unit is seated: the seat's control update reached
// the mod. Its heartbeat and the seat's live flags go to the aim path; the seated trace runs.
void Halo4SeatConvergeControlUpdate(const uint8_t* tls, const Halo4VehicleInputState& seat, bool flagsKnown,
                                    uint32_t flags)
{
    auto& f = g_h4SeatConverge;
    // Without the aim camera's anchors nothing can be sampled and no seat solves: nothing is traced either.
    if (!Halo4SeatConvergeMeasures() || !f.ready.load(std::memory_order_acquire) || !tls || !seat.seated ||
        seat.parent == UINT32_MAX || seat.seat < 0 || TitleAdapter_GetActiveTitle() != GameTitle::Halo4 ||
        f.generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
        return;
    const uint32_t seatKey = Halo4SeatConvergeKey(seat.parent, seat.seat);
    Halo4SeatControlRecord record{};
    record.ms = Sight_NowMs();
    record.seatKey = seatKey;
    record.flagsKnown = flagsKnown ? 1u : 0u;
    record.flags = flagsKnown ? flags : 0u;
    f.control.Publish(record);
    f.controlUpdates.fetch_add(1, std::memory_order_relaxed);
    f.lastFlags.store(record.flags, std::memory_order_relaxed);
    f.lastFlagsKnown.store(record.flagsKnown, std::memory_order_relaxed);
    // The trace is the lane's one native call, and it is made only where the tick is KNOWN not to be running.
    // The control update is not expected inside a tick at all, so the first count only moves if the game ever
    // reaches it another way; the second moves where the title's in-tick byte cannot be read.
    const int inTick = NativeVehicleFirstPerson_InSimulationTick(GameTitle::Halo4);
    if (inTick != 0)
    {
        (inTick == 1 ? f.controlInTick : f.controlTickUnread).fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Halo4SeatConvergeTrace(tls, seat, seatKey, Halo4SeatConvergeOn());
}

// ---- the poll (Game_ComputeAimStick's thread) ------------------------------------------------------------------
// What the aim path needs this poll for a Halo 4 seat. Seated, the on-foot eyes are never used: the engine eye
// there is the point of the simulation's own aim line the solve starts from.
struct Halo4SeatConvergePoll
{
    bool seated = false;   // a seat this lane handles this poll (the key on, or a dev build measuring)
    bool ray = false;      // publish the seated intent ray: our seat view, the rendered head known, seats aimed
    bool solve = false;    // try the bend through the traced point
    bool turret = false;   // the seat's camera is on its gun (flag bit 7): the pitch servo parks, as a turret's
    bool held = false;     // the start is the one held across a gap in the samples
    uint32_t unit = UINT32_MAX, parent = UINT32_MAX, seatKey = 0;
    int16_t seat = -1;
    float head[3]{}, eye[3]{};   // the rendered centre eye, and the solve's start
    halo4_seat_converge::Why why = halo4_seat_converge::Why::Count;   // Count: no seat was judged
};

// A seat's control update counts as reaching the mod while its last one is this fresh (they come once a frame).
inline constexpr uint64_t kHalo4SeatControlFreshMs = 250;
// A seated poll this long after the last one starts a new seat stint, even in the same seat.
inline constexpr uint64_t kHalo4SeatConvergeStintGapUs = 150000;

// True while seated and this lane handles the seat (out filled); false on foot, with no fresh stamp, or with the
// lane off, which leaves the on-foot path exactly as it was. `rayDir`: the seated intent ray's direction (unit).
bool Halo4SeatConvergePrepare(Halo4SeatConvergePoll& out, const float rayDir[3])
{
    struct State
    {
        uint32_t unit = UINT32_MAX, parent = UINT32_MAX, generation = 0, sampleSequence = 0;
        int16_t seat = -1;
        uint64_t lastUs = 0;
        float lastYawDeg = 0.0f;   // the last new sample's heading residual (the side-change witness)
        halo4_seat_converge::StartHold hold{};
        halo4_seat_converge::GunCameraLatch gunCamera{};
    };
    static State s_state;   // this thread only, like the servo's own statics
    // The lane off, or its anchors unproven on this build: the seat is left exactly as with the key off (the
    // install's one line says so).
    if (!Halo4SeatConvergeMeasures() || !g_h4SeatConverge.ready.load(std::memory_order_acquire))
    {
        s_state = {};
        return false;
    }
    Halo4VehicleInputState stamp{};
    if (!Halo4ReadSeatStamp(stamp))
        return false;   // no fresh stamp: the stint survives only if a seated poll follows within the gap
    if (!stamp.seated || stamp.parent == UINT32_MAX || stamp.seat < 0)
    {
        s_state = {};   // a fresh stamp says on foot: the stint is over
        return false;
    }
    constexpr GameTitle kTitle = GameTitle::Halo4;
    auto& f = g_h4SeatConverge;
    const bool key = Halo4SeatConvergeOn();
    const uint32_t generation = TitleAdapter_GetGeneration(kTitle);
    const uint64_t nowUs = Sight_NowUs();
    if (s_state.unit != stamp.unit || s_state.parent != stamp.parent || s_state.seat != stamp.seat ||
        s_state.generation != generation || !s_state.lastUs || nowUs < s_state.lastUs ||
        nowUs - s_state.lastUs > kHalo4SeatConvergeStintGapUs)
    {
        // A new seat stint: nothing of the last one carries over.
        s_state = {};
        s_state.unit = stamp.unit;
        s_state.parent = stamp.parent;
        s_state.seat = stamp.seat;
        s_state.generation = generation;
    }
    s_state.lastUs = nowUs;
    out.seated = true;
    out.unit = stamp.unit;
    out.parent = stamp.parent;
    out.seat = stamp.seat;
    out.seatKey = Halo4SeatConvergeKey(stamp.parent, stamp.seat);
    halo4_seat_converge::PollFacts facts{};
    facts.aimOn = Sight_AimEnabled() && g_config.reach_vehicle_aim > 0;
    facts.viewOurs = NativeVehicleFirstPerson_SeatViewOwned(kTitle, stamp.parent, stamp.seat);
    BoneMatrix center{};
    facts.headKnown = Halo4LoadCenterRoot(1.0f, center);
    if (facts.headKnown)
        memcpy(out.head, center.translation, sizeof(out.head));
    memcpy(out.eye, out.head, sizeof(out.eye));
    facts.traceArmed = LegacySight(kTitle).flags.load(std::memory_order_acquire) != 0;
    facts.anchors = f.ready.load(std::memory_order_acquire) && f.generation == generation;
    facts.traceFaulted = Halo4SeatConvergeTraceOff();
    facts.stanceOurs = Sight_Stance(kTitle) == out.seatKey;
    Halo4SeatControlRecord control{};
    const bool controlRead = f.control.Read(control) && control.seatKey == out.seatKey;
    const uint64_t nowMs = Sight_NowMs();   // after the read, as below
    facts.controlUpdate = controlRead && nowMs >= control.ms && nowMs - control.ms <= kHalo4SeatControlFreshMs;
    // THE POINT, when this seat's trace has a range: the intent ray at the range the aim converges on.
    float point[3]{};
    bool pointKnown = false;
    SightTrace trace{};
    if (facts.headKnown && facts.stanceOurs && rayDir && Sight_ReadTrace(kTitle, generation, kSightFreshMs, trace) &&
        trace.AimRangeWu() > 0.0f)
    {
        for (int axis = 0; axis < 3; ++axis)
            point[axis] = out.head[axis] + rayDir[axis] * trace.AimRangeWu();
        pointKnown = Halo4SeatConvergeFinite3(point);
    }
    // THE START: the newest sample of this seat, carried; else the last start, held against the rendered head.
    Halo4SeatCameraRecord sample{};
    const bool sampleRead = f.sample.Read(sample) && sample.generation == generation &&
        sample.unit == stamp.unit && sample.parent == stamp.parent && sample.seat == stamp.seat;
    const float tickS = float(Sight_TickPeriodUs(kTitle, 16667)) * 1.0e-6f;
    // The sample's age on a clock read AFTER the sample: one published since this poll began would otherwise be
    // "from the future", refused, and counted as a gap in the samples.
    const uint64_t sampleNowUs = Sight_NowUs();
    const float carryS =
        sampleRead ? halo4_seat_converge::CarrySeconds(sample.camera.stampUs, sampleNowUs, tickS) : -1.0f;
    if (sampleRead && carryS >= 0.0f)
    {
        const halo4_seat_converge::Start start =
            halo4_seat_converge::SolveStart(sample.camera, pointKnown ? point : nullptr, carryS);
        if (start.valid)
        {
            memcpy(out.eye, start.pos, sizeof(out.eye));
            facts.start = true;
            if (facts.headKnown)
                s_state.hold.Note(nowUs, start.pos, out.head, rayDir, start.pushWu > 0.0f);
            const uint32_t ageMilli = static_cast<uint32_t>(sampleNowUs - sample.camera.stampUs);   // us = milli-ms
            f.ageMilliMs.fetch_add(ageMilli, std::memory_order_relaxed);
            f.ageSamples.fetch_add(1, std::memory_order_relaxed);
            if (ageMilli > f.ageMaxMilliMs.load(std::memory_order_relaxed))
                f.ageMaxMilliMs.store(ageMilli, std::memory_order_relaxed);
        }
    }
    if (!facts.start && facts.headKnown && s_state.hold.Get(nowUs, out.head, rayDir, out.eye))
    {
        facts.start = true;
        out.held = true;
    }
    out.why = halo4_seat_converge::DecidePoll(facts);
    out.ray = facts.viewOurs && facts.headKnown && g_config.reach_vehicle_aim > 0;
    out.solve = key && out.why == halo4_seat_converge::Why::Solved;
    // A seat whose camera is on its gun: the frame's aim, which the pitch servo closes on, is then the gun, slewing
    // at its own rate. The servo parks instead of hunting (its reticle stays on the hand's point like every
    // seat's). From the stick call's own read of the seat's flags, held for the stint (GunCameraLatch).
    s_state.gunCamera.Note(controlRead, controlRead && control.flagsKnown != 0, control.flags);
    out.turret = key && facts.aimOn && facts.viewOurs && s_state.gunCamera.Turret();
    // THE WITNESS: each new sample's own line against the point, while the seat solves.
    if (sampleRead && sample.sequence != s_state.sampleSequence)
    {
        s_state.sampleSequence = sample.sequence;
        if (out.solve && pointKnown)
        {
            const halo4_seat_converge::LineMiss miss =
                halo4_seat_converge::MeasureLine(sample.camera.camera, sample.camera.dir, point);
            if (miss.valid)
            {
                f.sampleYaw.Add(miss.yawDeg);
                f.samplePitch.Add(miss.pitchDeg);
                if (std::fabs(miss.yawDeg) > 0.2f && std::fabs(s_state.lastYawDeg) > 0.2f &&
                    (miss.yawDeg > 0.0f) != (s_state.lastYawDeg > 0.0f))
                    f.sampleSideChanges.fetch_add(1, std::memory_order_relaxed);
                s_state.lastYawDeg = miss.yawDeg;
            }
        }
    }
    return true;
}

// The end of a seated poll: what became of it. `bent`: the aim was bent through the traced point; `bendWhy`: the
// sight layer's reason when a tried bend declined; `point`: the point it was bent through (null: none).
void Halo4SeatConvergeEndPoll(const Halo4SeatConvergePoll& poll, bool bent, converge::ConvergeOff bendWhy,
                              const float* point)
{
    if (!poll.seated || !Halo4SeatConvergeOn())
        return;
    auto& f = g_h4SeatConverge;
    halo4_seat_converge::Why why = poll.why;
    if (poll.solve)
        why = bent ? halo4_seat_converge::Why::Solved
                   : halo4_seat_converge::FromBend(bendWhy == converge::ConvergeOff::None
                                                       ? converge::ConvergeOff::TraceStale : bendWhy);
    f.polls.fetch_add(1, std::memory_order_relaxed);
    if (why < halo4_seat_converge::Why::Count)
        f.why[static_cast<size_t>(why)].fetch_add(1, std::memory_order_relaxed);
    if (poll.held)
        f.heldPolls.fetch_add(1, std::memory_order_relaxed);
    if (bent && point && Halo4SeatConvergeFinite3(point))
    {
        Halo4SeatSolvedPoint solved{};
        solved.stampUs = Sight_NowUs();
        solved.generation = f.generation;
        solved.seatKey = poll.seatKey;
        memcpy(solved.point, point, sizeof(solved.point));
        f.solved.Publish(solved);
    }
}

// ---- the report (cold worker) -------------------------------------------------------------------------------
// Every 2 s while the lane is on and a seat was polled, sampled or updated: one line that says whether it runs
// and, if not, why.
void Halo4SeatConvergeReport()
{
    using halo4_seat_converge::Why;
    static uint64_t s_last = 0;
    static int s_keySeen = -1;
    auto& f = g_h4SeatConverge;
    if (TitleAdapter_GetActiveTitle() != GameTitle::Halo4 || !f.base)
        return;
    const int key = Halo4SeatConvergeOn() ? 1 : 0;
    if (key != s_keySeen)
    {
        // The switch is live: say when it changes, so a log shows which stretch ran with it.
        if (s_keySeen >= 0 || key)
            LOG("SIGHT Halo 4 seat converge: %s (halo4_seat_converge = %d)%s", key ? "ON" : "OFF", key,
                key && !f.ready.load(std::memory_order_acquire)
                    ? "; the aim camera did not prove on this build, so no seat will solve" : "");
        s_keySeen = key;
    }
    const uint64_t now = GetTickCount64();
    if (now - s_last < 2000)
        return;
    s_last = now;
    const uint64_t polls = f.polls.exchange(0, std::memory_order_relaxed);
    const uint64_t samples = f.samples.exchange(0, std::memory_order_relaxed);
    const uint64_t updates = f.controlUpdates.exchange(0, std::memory_order_relaxed);
    const uint64_t updatesInTick = f.controlInTick.exchange(0, std::memory_order_relaxed);
    const uint64_t updatesTickUnread = f.controlTickUnread.exchange(0, std::memory_order_relaxed);
    uint64_t why[static_cast<size_t>(Why::Count)]{};
    for (size_t i = 0; i < static_cast<size_t>(Why::Count); ++i)
        why[i] = f.why[i].exchange(0, std::memory_order_relaxed);
    const uint64_t samplesStock = f.samplesStock.exchange(0, std::memory_order_relaxed);
    const uint64_t tickUnread = f.samplesTickUnread.exchange(0, std::memory_order_relaxed);
    const uint64_t following = f.samplesFollowing.exchange(0, std::memory_order_relaxed);
    const uint64_t pushMilli = f.pushMilliWu.exchange(0, std::memory_order_relaxed);
    const uint64_t busy = f.samplerBusy.exchange(0, std::memory_order_relaxed);
    const uint64_t tickSide = f.tickSideCalls.exchange(0, std::memory_order_relaxed);
    const uint64_t held = f.heldPolls.exchange(0, std::memory_order_relaxed);
    const uint64_t ageSum = f.ageMilliMs.exchange(0, std::memory_order_relaxed);
    const uint64_t ageCount = f.ageSamples.exchange(0, std::memory_order_relaxed);
    const uint32_t ageMax = f.ageMaxMilliMs.exchange(0, std::memory_order_relaxed);
    const uint64_t traces = f.traces.exchange(0, std::memory_order_relaxed);
    const uint64_t hits = f.traceHits.exchange(0, std::memory_order_relaxed);
    const uint64_t misses = f.traceMisses.exchange(0, std::memory_order_relaxed);
    const uint64_t lookPasts = f.lookPasts.exchange(0, std::memory_order_relaxed);
    const uint64_t exhausted = f.exhausted.exchange(0, std::memory_order_relaxed);
    const uint64_t noRay = f.noRay.exchange(0, std::memory_order_relaxed);
    const uint64_t otherRay = f.otherRay.exchange(0, std::memory_order_relaxed);
    const uint64_t rootFallbacks = f.rootFallbacks.exchange(0, std::memory_order_relaxed);
    const uint64_t cost = f.costTicks.exchange(0, std::memory_order_relaxed);
    const uint64_t costMax = f.costMaxTicks.exchange(0, std::memory_order_relaxed);
    const uint64_t faults = f.faults.exchange(0, std::memory_order_relaxed);
    const uint64_t fires = f.fires.exchange(0, std::memory_order_relaxed);
    const uint64_t firesInTick = f.firesInTick.exchange(0, std::memory_order_relaxed);
    const uint64_t firesFollowing = f.firesFollowing.exchange(0, std::memory_order_relaxed);
    const uint64_t firesNoPoint = f.firesNoPoint.exchange(0, std::memory_order_relaxed);
    const uint64_t sideChanges = f.sampleSideChanges.exchange(0, std::memory_order_relaxed);
    const auto sampleYaw = f.sampleYaw.Take(), samplePitch = f.samplePitch.Take();
    const auto fireYaw = f.fireYaw.Take(), firePitch = f.firePitch.Take(), fireMm = f.fireMm.Take();
    const auto fireAlong = f.fireAlongMm.Take(), fireImplied = f.fireImpliedMs.Take();
    const auto tickSideMs = f.tickSideMs.Take();
    if (!key || (!polls && !samples && !updates && !tickUnread))
        return;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const double usPerTick = frequency.QuadPart ? 1.0e6 / double(frequency.QuadPart) : 0.0;
    const uint32_t flags = f.lastFlags.load(std::memory_order_relaxed);
    const bool flagsKnown = f.lastFlagsKnown.load(std::memory_order_relaxed) != 0;
    char pollText[640], sampleText[560], traceText[520], witnessText[760];
    snprintf(pollText, sizeof(pollText),
             "%llu seated polls, %llu SOLVED; not solved: %s %llu, %s %llu, %s %llu, %s %llu, %s %llu, %s %llu, "
             "%s %llu, %s %llu, %s %llu, %s %llu, %s %llu",
             static_cast<unsigned long long>(polls), static_cast<unsigned long long>(why[static_cast<size_t>(Why::Solved)]),
             halo4_seat_converge::WhyName(Why::AimOff), static_cast<unsigned long long>(why[static_cast<size_t>(Why::AimOff)]),
             halo4_seat_converge::WhyName(Why::ViewNotOurs),
             static_cast<unsigned long long>(why[static_cast<size_t>(Why::ViewNotOurs)]),
             halo4_seat_converge::WhyName(Why::NoHead), static_cast<unsigned long long>(why[static_cast<size_t>(Why::NoHead)]),
             halo4_seat_converge::WhyName(Why::TraceOff), static_cast<unsigned long long>(why[static_cast<size_t>(Why::TraceOff)]),
             halo4_seat_converge::WhyName(Why::NoAimCamera),
             static_cast<unsigned long long>(why[static_cast<size_t>(Why::NoAimCamera)]),
             halo4_seat_converge::WhyName(Why::NoSample), static_cast<unsigned long long>(why[static_cast<size_t>(Why::NoSample)]),
             halo4_seat_converge::WhyName(Why::NoControlUpdate),
             static_cast<unsigned long long>(why[static_cast<size_t>(Why::NoControlUpdate)]),
             halo4_seat_converge::WhyName(Why::TraceFaulted),
             static_cast<unsigned long long>(why[static_cast<size_t>(Why::TraceFaulted)]),
             halo4_seat_converge::WhyName(Why::OtherStance),
             static_cast<unsigned long long>(why[static_cast<size_t>(Why::OtherStance)]),
             halo4_seat_converge::WhyName(Why::NoTrace), static_cast<unsigned long long>(why[static_cast<size_t>(Why::NoTrace)]),
             halo4_seat_converge::WhyName(Why::NoRange), static_cast<unsigned long long>(why[static_cast<size_t>(Why::NoRange)]));
    snprintf(sampleText, sizeof(sampleText),
             "aim-camera samples %llu, all outside a tick (the game's own camera %llu, the seat view's %llu; "
             "following camera %llu, mean push %.2f m; sampler busy %llu; calls not sampled because the tick could "
             "not be read %llu), age at the poll avg %.1f / "
             "max %.1f ms, start held across a sample gap in %llu polls, eye speed %.1f m/s",
             static_cast<unsigned long long>(samples), static_cast<unsigned long long>(samplesStock),
             static_cast<unsigned long long>(samples - (samplesStock < samples ? samplesStock : samples)),
             static_cast<unsigned long long>(following),
             following ? double(pushMilli) / 1000.0 / double(following) * double(shot_lag::kMetresPerWu) : 0.0,
             static_cast<unsigned long long>(busy), static_cast<unsigned long long>(tickUnread),
             ageCount ? double(ageSum) / 1000.0 / double(ageCount) : 0.0,
             double(ageMax) / 1000.0, static_cast<unsigned long long>(held),
             double(f.eyeSpeedMilliMps.load(std::memory_order_relaxed)) / 1000.0);
    snprintf(traceText, sizeof(traceText),
             "control updates %llu (not traced: %llu inside a tick, %llu with the tick unread), seated traces %llu "
             "(%llu hits, %llu misses, %llu of them after three passes on our own vehicle; looked past it %llu "
             "times; no ray %llu, a ray that is not this seat's %llu, root fallbacks %llu, faults %llu; cost avg "
             "%.1f / max %.1f us)",
             static_cast<unsigned long long>(updates), static_cast<unsigned long long>(updatesInTick),
             static_cast<unsigned long long>(updatesTickUnread), static_cast<unsigned long long>(traces),
             static_cast<unsigned long long>(hits), static_cast<unsigned long long>(misses),
             static_cast<unsigned long long>(exhausted), static_cast<unsigned long long>(lookPasts),
             static_cast<unsigned long long>(noRay), static_cast<unsigned long long>(otherRay),
             static_cast<unsigned long long>(rootFallbacks), static_cast<unsigned long long>(faults),
             traces ? double(cost) / double(traces) * usPerTick : 0.0, double(costMax) * usPerTick);
    snprintf(witnessText, sizeof(witnessText),
             "the sampled line against the point over %llu new samples while solved: heading avg %.2f / max %.2f "
             "deg (changed sides %llu times), elevation avg %.2f / max %.2f deg; local seated fires %llu (inside a "
             "tick %llu, from a following "
             "camera %llu, no point to judge %llu): the fire's own aim line passed the point by heading avg %.2f / "
             "max %.2f deg, elevation avg %.2f / max %.2f deg, avg %.0f / max %.0f mm; along the eye's travel %+.0f "
             "mm, %+.1f ms over %llu moving fires (negative: the carry was short); tick-side calls %llu, their eye "
             "ahead of the control pass's by %+.1f ms of travel over %llu",
             static_cast<unsigned long long>(sampleYaw.count), sampleYaw.meanAbs, sampleYaw.maxAbs,
             static_cast<unsigned long long>(sideChanges), samplePitch.meanAbs, samplePitch.maxAbs,
             static_cast<unsigned long long>(fires),
             static_cast<unsigned long long>(firesInTick), static_cast<unsigned long long>(firesFollowing),
             static_cast<unsigned long long>(firesNoPoint), fireYaw.meanAbs, fireYaw.maxAbs, firePitch.meanAbs,
             firePitch.maxAbs, fireMm.meanAbs, fireMm.maxAbs, fireAlong.mean, fireImplied.mean,
             static_cast<unsigned long long>(fireImplied.count), static_cast<unsigned long long>(tickSide),
             tickSideMs.mean, static_cast<unsigned long long>(tickSideMs.count));
    // The last field is the rule as it stands now (sight_layer.h Sight_FireTimeChangesAllowed), not a switch's
    // position: with the seat's aim the hand's, it decides which camera answers the sampled call.
    LOG("SIGHT Halo 4 seat converge: %s; %s; %s; %s; seat flags 0x%X (%s); one-machine changes %s",
        pollText, sampleText, traceText, witnessText, flagsKnown ? flags : 0u,
        halo4_seat_converge::RoleName(flagsKnown, flags),
        Sight_FireTimeChangesAllowed() ? "allowed" : "not allowed");
}

// ---- install / remove ------------------------------------------------------------------------------------------
bool Halo4SeatConvergeUnique(uintptr_t base, size_t size, uintptr_t rva, const char* pattern)
{
    const uintptr_t hit = sig::Find(base, size, pattern);
    return hit == base + rva && !sig::Find(hit + 1, base + size - hit - 1, pattern);
}

bool Halo4SeatConvergeCalls(uintptr_t base, uintptr_t call, uintptr_t target)
{
    const auto* code = reinterpret_cast<const uint8_t*>(base + call);
    return code[0] == 0xE8 && base + call + 5 + *reinterpret_cast<const int32_t*>(code + 1) == base + target;
}

void RemoveHalo4SeatConverge()
{
    auto& f = g_h4SeatConverge;
    f.ready.store(false, std::memory_order_release);
    f.sample.Clear();
    f.control.Clear();
    f.solved.Clear();
}

// After the muzzle lane: the sample rides its view detour, whose entry and three call edges that lane proved.
void InstallHalo4SeatConverge(uintptr_t base, size_t size, uint32_t generation)
{
    auto& f = g_h4SeatConverge;
    RemoveHalo4SeatConverge();
    f.base = base;
    f.generation = generation;
    f.traceFaulted.store(false, std::memory_order_release);   // a reinstall gives the seated trace another try
    // (The trace's rate limiter is the stick call's thread's alone and needs no reset: a last trace it cannot
    // place against the clock never holds the next one back.)
    // The sampler's own state, under the sampler's flag: a view call of the last generation that was already past
    // its checks may still be finishing. A view call holds the flag for well under a microsecond; one that never
    // lets go is a thread that ended inside it, and this install then gives up instead of holding the worker (and
    // every other title's installs and reports with it) for ever.
    const uint64_t waitedFromMs = GetTickCount64();
    while (f.sampling.test_and_set(std::memory_order_acquire))
    {
        if (GetTickCount64() - waitedFromMs > 200)
        {
            LOG("SIGHT Halo 4 seat converge: the aim-camera sampler's state stayed in use for 200 ms at install; no "
                "Halo 4 seat converges until the next install");
            return;
        }
        YieldProcessor();
    }
    f.track.Reset();
    f.trackUnit = f.trackParent = UINT32_MAX;
    f.trackSeat = -1;
    f.last = {};
    f.sampling.clear(std::memory_order_release);
    if (!g_halo4BarrelBindingsReady.load(std::memory_order_acquire) || g_halo4Barrel.base != base)
    {
        LOG("SIGHT Halo 4 seat converge: the muzzle lane is not installed (its view detour carries the aim-camera "
            "sample); no Halo 4 seat can converge");
        return;
    }
    // ADDR-HYGIENE: resolved -- each is a signature that must match exactly once at its address, plus the native
    // call edge that proves how the game reaches it; nothing is assumed on another build, and a miss leaves every
    // Halo 4 seat on its old aim with the one line below.
    //   * the aim view's own body (0x1DCA13): it hands the aim camera 0x2061A8 its position and direction
    //     arguments, then copies the position to its sixth argument BEFORE the following camera's push -- so the
    //     sixth argument is the camera and the fourth the trace's start;
    //   * the aim camera 0x2061A8, whole: it asks the selector 0x10D268 (the one the first-person seat override
    //     hooks, so standing the override down for a call restores the game's own answer) and builds the eye or
    //     the following camera from it.
    if (Halo4SeatConvergeUnique(base, size, 0x1DCA13, "44 8B CB 48 8B D6 48 8B 5D 77 8B CF 4C 8B C3 44 8B F0 E8 ?? "
            "?? ?? ?? 48 8B 55 7F F2 0F 10 06 F2 0F 11 02 8B 4E 08 89 4A 08 83 F8 01 0F 85") &&
        Halo4SeatConvergeCalls(base, 0x1DCA25, 0x2061A8) &&
        Halo4SeatConvergeUnique(base, size, 0x2061A8, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 48 8B FA "
            "8B F1 48 8D 54 24 40 45 8B F1 49 8B D8 E8 ?? ?? ?? ?? 44 8B 1D ?? ?? ?? ?? 8B E8 65 4C 8B 14 25 58 00 00 "
            "00 44 0F B7 C6 B9 18 00 00 00 4B 8B 04 DA 4B 8D 14 40 48 8B 0C 01 48 8B 49 50 4C 8B 44 D1 10 41 8B 88 90 "
            "04 00 00 48 C1 E9 1F F6 C1 01 74 3C B9 08 00 00 00 48 8B 0C 01 41 0F B7 80 9C 04 00 00 48 69 D0 18 0F 00 "
            "00 48 03 51 50 F2 0F 10 42 40 F2 0F 11 07 8B 42 48 89 47 08 F2 0F 10 42 4C F2 0F 11 03 8B 42 54 89 43 08 "
            "EB 23 45 33 C9 4C 8B C3 48 8B D7 8B CE 85 ED 75 0F 41 83 FE 01 41 0F 94 C1 E8 ?? ?? ?? ?? EB 05 E8 ?? ?? "
            "?? ?? 48 8B 5C 24 48 8B C5 48 8B 6C 24 50 48 83 C4 20 41 5E 5F 5E C3") &&
        Halo4SeatConvergeCalls(base, 0x2061CA, 0x10D268))
    {
        f.ready.store(true, std::memory_order_release);
        LOG("SIGHT Halo 4 seat converge: ready (the aim view and the aim camera proved); halo4_seat_converge = %d",
            Halo4SeatConvergeOn() ? 1 : 0);
    }
    else
        LOG("SIGHT Halo 4 seat converge: the aim camera (the aim view's body 0x1DCA13 and its call 0x1DCA25, the "
            "camera 0x2061A8 and its selector call 0x2061CA) did not prove on this build; no Halo 4 seat can "
            "converge (halo4_seat_converge has no effect)");
}
