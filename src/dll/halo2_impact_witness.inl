// Included in halo2_observer_6dof.cpp (its anonymous namespace) after halo2_dual_wield_runtime.inl: it reads the fire
// lane's thread state (g_halo2MuzzleRequest), its ownership proof (Halo2DualWeaponOwned, Halo2IndependentTargetStorage)
// and the observer publication; it is fed by the collision detour (Halo2CollisionVectorDetour), both stereo cores
// (Halo2Observer6Dof_DevNoteAsSeenFrame) and the compositor (Game_DevNoteTracedReticle).
//
// AS SEEN, Halo 2 in VR (dev builds only). With flat parity on for Halo 2 (P' = P + (E_fire - E_P),
// halo2_fire_eye.inl), shots in Halo 2 Anniversary can still seem to trail a strafe.
// The SHOT LAG transport line proves the shift is applied (+110..+140 mm on every shot); it cannot say what the player
// SEES. This witness measures that, with the same code as the flat probe (src/common/as_seen_logic.h), so the VR and
// flat numbers compare bin for bin and renderer for renderer:
//
//   * at the fire: the round the fire routine 0x8E4940 creates for OUR unit's weapon (object_new 0x8D79D0, filtered by
//     the fire's return 0x8E5A74; the weapon proven ours by the fire lane's own ownership check, the unit on foot):
//     its spawn line and speed, P's line (the newest publication's rendered eye along the aiming hand, the line the
//     muzzle shot aims through) and that publication's engine eye E_P, stamp, tick and interpolation factor;
//   * at the hit: the round's own collision query (as_seen::QueryOnRound) through collision_test_vector 0x75B850;
//   * the FIRST VR frame showing the hit: the first prepared frame whose world is from a tick after the hit (its
//     publication's tick is past the hit's; by time when a tick is unknown), noted by the stereo core that rendered
//     it with the centre camera its eyes were built from and the head orientation its views were submitted with;
//   * the reticle the compositor drew for THAT serial: its tracking direction mapped into the world through that
//     frame's own camera (as it shows over the scene), from the frame's rendered eye.
// SEEN = the impact against that reticle; STOCK = against the game's own crosshair line in that frame (the engine eye
// along the stock camera's forward, which direct drive turns with the hand); AT FIRE = against P's line. The clocks
// in ms AND in simulation ticks (game_time), and the frame camera's interpolation factor. One line per renderer.
//
// The hook forwards its one argument exactly (`py -3 tools/audit_caller_area.py halo2.dll 8D79D0:1:i` is clean: no
// caller-area slot touched, 32 native callers, none relies on a volatile register across the call) and only reads.
// ADDR-HYGIENE: dev-only (a witness; decides nothing). Its anchors are the flat probe's, each unique and at its pinned
// RVA, the offsets decoded from object_new's own copy.

#if HALOMCCVR_DEV_DIAGNOSTICS
using Halo2ImpactObjectNewFn = int32_t(__fastcall*)(void*);

struct Halo2AsSeenAim
{
    bool valid = false;
    float head[3]{}, dir[3]{}, eye[3]{};   // H_P, u, E_P
    uint64_t camUs = 0;
    int64_t camTick = -1;
    float camFactor = NAN;
    bool velValid = false;
    float vel[3]{};
};
struct Halo2AsSeenRound
{
    bool used = false;
    uint64_t fireUs = 0;
    int64_t fireTick = -1;
    float spawn[3]{}, fwd[3]{};
    uint32_t queries = 0;
    Halo2AsSeenAim aim{};
};
struct Halo2AsSeenFrame
{
    bool valid = false;
    int renderer = 0;              // 0 classic, 1 Anniversary
    uint64_t serial = 0, stampUs = 0, cameraUs = 0;
    int64_t tick = -1;             // the publication's tick (the world this frame shows)
    float factor = NAN;
    float head[3]{}, fwd[3]{}, up[3]{}, quat[4]{};
    bool eyeValid = false;
    float eye[3]{}, aimFwd[3]{};
};
struct Halo2AsSeenPending
{
    bool used = false, framed = false;
    float impact[3]{};
    uint64_t impactUs = 0;
    int64_t impactTick = -1;
    Halo2AsSeenRound round{};
    Halo2AsSeenFrame frame{};
};
struct Halo2AsSeenReticle
{
    bool valid = false;
    uint64_t serial = 0, stampUs = 0;
    float center[3]{}, dir[3]{};
};

struct Halo2ImpactWitness
{
    void* target = nullptr;
    Halo2ImpactObjectNewFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    uintptr_t fireReturn = 0;
    uint32_t placePos = 0, placeFwd = 0, placeVel = 0;
    std::atomic<uint32_t> live{0};   // rounds + pendings in use: the collision note takes the lock only while any
    SRWLOCK lock = SRWLOCK_INIT;
    static constexpr int kRounds = 16, kPending = 16, kFrames = 32, kReticles = 16;
    Halo2AsSeenRound rounds[kRounds]{};
    Halo2AsSeenPending pending[kPending]{};
    Halo2AsSeenFrame frames[kFrames]{};
    uint32_t frameNext = 0;
    Halo2AsSeenReticle reticles[kReticles]{};
    uint32_t reticleNext = 0;
    as_seen::Window window[2]{};
    as_seen::CameraCensus census[2]{};
    // Window counters.
    uint32_t spawns = 0, ours = 0, notOurs = 0, noAim = 0, evicted = 0, lost = 0, matched = 0, ambiguous = 0;
    uint32_t impacts = 0, framesNoted[2]{}, framesNoPublication = 0, reticlesNoted = 0, noReticle = 0;
    uint32_t speedN = 0;
    double speedSum = 0.0;
} g_halo2Impact;

// ---- helpers (under g_halo2Impact.lock) ---------------------------------------------------------------------------
void Halo2AsSeenRotateInverse(const float q[4], const float v[3], float out[3])
{
    const float qv[3]{-q[0], -q[1], -q[2]};
    const float w = q[3];
    const float t[3]{2.0f * (qv[1] * v[2] - qv[2] * v[1]), 2.0f * (qv[2] * v[0] - qv[0] * v[2]),
                     2.0f * (qv[0] * v[1] - qv[1] * v[0])};
    out[0] = v[0] + w * t[0] + (qv[1] * t[2] - qv[2] * t[1]);
    out[1] = v[1] + w * t[1] + (qv[2] * t[0] - qv[0] * t[2]);
    out[2] = v[2] + w * t[2] + (qv[0] * t[1] - qv[1] * t[0]);
}

// The reticle's tracking direction as the compositor shows it over this frame: into the head's frame (OpenXR: +X
// right, +Y up, -Z forward) with the head orientation the views were submitted with, then out through the centre
// camera the eyes were built from (forward, up, left = up x forward). As halo3_impact_witness.inl does.
bool Halo2AsSeenReticleWorld(const Halo2AsSeenFrame& frame, const float trackingDir[3], float out[3])
{
    const float ql = std::sqrt(frame.quat[0] * frame.quat[0] + frame.quat[1] * frame.quat[1] +
                               frame.quat[2] * frame.quat[2] + frame.quat[3] * frame.quat[3]);
    if (!std::isfinite(ql) || ql < 0.5f || !shot_lag::Finite3(trackingDir) || !shot_lag::Finite3(frame.fwd) ||
        !shot_lag::Finite3(frame.up))
        return false;
    const float q[4]{frame.quat[0] / ql, frame.quat[1] / ql, frame.quat[2] / ql, frame.quat[3] / ql};
    float local[3]{};
    Halo2AsSeenRotateInverse(q, trackingDir, local);
    const float* f = frame.fwd;
    const float* u = frame.up;
    const float left[3]{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = -local[2] * f[axis] + local[1] * u[axis] - local[0] * left[axis];
    const float l = shot_lag::Length(out);
    if (!std::isfinite(l) || l < 1.0e-6f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] /= l;
    return true;
}

// The engine eye's velocity at `frame` from the ring (50-200 ms back, same renderer), else `fallback`.
bool Halo2AsSeenEyeVel(const Halo2AsSeenFrame& frame, const float* fallback, float out[3])
{
    const Halo2AsSeenFrame* best = nullptr;
    for (const auto& f : g_halo2Impact.frames)
    {
        if (!f.valid || !f.eyeValid || f.renderer != frame.renderer || f.cameraUs >= frame.cameraUs)
            continue;
        const uint64_t dt = frame.cameraUs - f.cameraUs;
        if (dt < 50000 || dt > 200000)
            continue;
        if (!best || f.cameraUs > best->cameraUs)
            best = &f;
    }
    if (best && frame.eyeValid)
    {
        const float dt = float(frame.cameraUs - best->cameraUs) * 1.0e-6f;
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = (frame.eye[axis] - best->eye[axis]) / dt;
        if (shot_lag::Finite3(out))
            return true;
    }
    if (fallback && shot_lag::Finite3(fallback))
    {
        memcpy(out, fallback, sizeof(float) * 3);
        return true;
    }
    return false;
}

void Halo2AsSeenRelease(Halo2AsSeenPending& p)
{
    if (p.used)
        g_halo2Impact.live.fetch_sub(1, std::memory_order_acq_rel);
    p = Halo2AsSeenPending{};
}

// A framed impact and the reticle drawn for its frame: one sample.
void Halo2AsSeenEvaluate(Halo2AsSeenPending& p, const Halo2AsSeenReticle& r)
{
    auto& w = g_halo2Impact;
    const Halo2AsSeenFrame& frame = p.frame;
    const int renderer = frame.renderer ? 1 : 0;
    float drawn[3]{};
    if (!Halo2AsSeenReticleWorld(frame, r.dir, drawn))
    {
        ++w.window[renderer].noLine;
        return;
    }
    float vel[3]{};
    const bool velKnown = Halo2AsSeenEyeVel(frame, p.round.aim.velValid ? p.round.aim.vel : nullptr, vel);
    const float* v = velKnown ? vel : nullptr;
    as_seen::Sample sample{};
    sample.seen = as_seen::Measure(p.impact, frame.head, drawn, v);
    sample.stockValid = frame.eyeValid && shot_lag::Finite3(frame.aimFwd);
    if (sample.stockValid)
        sample.stock = as_seen::Measure(p.impact, frame.eye, frame.aimFwd, v);
    sample.atFireValid = p.round.aim.valid;
    if (sample.atFireValid)
        sample.atFire = as_seen::Measure(p.impact, p.round.aim.head, p.round.aim.dir, v);
    sample.aimCamUs = p.round.aim.camUs;
    sample.fireUs = p.round.fireUs;
    sample.impactUs = p.impactUs;
    sample.frameCamUs = frame.cameraUs;
    sample.presentUs = r.stampUs;   // VR: the compositor building this serial's layers (its submit), not a photon
    sample.ticksFireToImpact = p.round.queries ? static_cast<int32_t>(p.round.queries - 1) : -1;
    if (frame.tick >= 0 && p.round.aim.camTick >= 0 && frame.tick >= p.round.aim.camTick)
        sample.ticksAimToFrame = static_cast<int32_t>(frame.tick - p.round.aim.camTick);
    if (frame.tick >= 0 && p.impactTick >= 0 && frame.tick >= p.impactTick)
        sample.ticksImpactToFrame = static_cast<int32_t>(frame.tick - p.impactTick);
    sample.frameAlpha = frame.factor;
    if (p.round.aim.valid && frame.eyeValid)
    {
        const float moved[3]{frame.eye[0] - p.round.aim.eye[0], frame.eye[1] - p.round.aim.eye[1],
                             frame.eye[2] - p.round.aim.eye[2]};
        sample.eyeMovedAlongMm = as_seen::AlongTravelMm(moved, drawn, v, &sample.eyeMovedKnown);
    }
    ++w.impacts;
    w.window[renderer].Add(sample, g_config.crosshair_size_deg * 0.5f);
}

// Does `frame` show an impact? Its world is the state its publication was taken in: past the impact's tick when both
// ticks are known (the counter steps at the end of each tick, so the hit's tick reads one less than any later
// publication's), else by the camera's time.
bool Halo2AsSeenFrameShows(const Halo2AsSeenFrame& frame, const Halo2AsSeenPending& p)
{
    if (frame.tick >= 0 && p.impactTick >= 0)
        return frame.tick > p.impactTick;
    return p.impactUs <= frame.cameraUs;
}

// ---- the spawn: object_new from the fire routine --------------------------------------------------------------------
void Halo2ImpactNoteSpawn(const float spawn[3], const float fwd[3], const float* velocity)
{
    // A seated fire's round against the barrel it left (halo2_direct_drive.inl).
    Halo2SeatBarrelNoteRound(spawn, fwd, velocity);
    auto& w = g_halo2Impact;
    const auto request = g_halo2MuzzleRequest;
    const uint32_t owned = Halo2OwnedUnit();
    bool ours = false;
    __try
    {
        ours = request.weapon != UINT32_MAX && owned != UINT32_MAX && Halo2DualWeaponOwned(request.weapon, owned) &&
               Halo2IndependentTargetStorage(owned) != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ours = false;
    }
    // A gun our seat fires is ours too (a seat's round): P's line is then the seat's intent ray. Another
    // occupant's weapon is not: only a gun the seat's own fire has shown (this fire's aim call came first).
    const bool seatGun = !ours && owned != UINT32_MAX && Halo2SeatBarrelKnownGun(request.weapon);
    Halo2AsSeenAim aim{};
    if (seatGun)
    {
        Halo2ObserverPosePublication publication{};
        aim.valid = Halo2SeatIntentRay(aim.head, aim.dir);
        memcpy(aim.eye, aim.head, sizeof(aim.eye));
        if (aim.valid && Halo2Observer6Dof_ReadPublishedPose(publication))
        {
            aim.camUs = publication.publishedUs;
            aim.camTick = publication.gameTick;
            aim.camFactor = publication.interpFactor;
        }
        ours = true;
    }
    else if (ours)
    {
        // P's line: the newest publication's sight geometry (the line Halo2AimMuzzleShot aims through at this fire).
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        Halo2ObserverPosePublication publication{};
        Halo2CameraBasis primary{}, secondary{};
        aim.valid = generation && Halo2Observer6Dof_ReadPublishedPose(publication) &&
                    publication.generation == generation && !publication.vehicleReferenceValid &&
                    Halo2ObserverControllerSnapshotUsable(publication, generation) &&
                    BuildStableFirstPersonCarriers(publication, generation, primary, secondary,
                                                   Halo2SupportCarrierMustDetach(publication)) &&
                    Halo2SightGeometry(publication, primary, aim.head, aim.dir, aim.eye);
        if (aim.valid)
        {
            aim.camUs = publication.publishedUs;
            aim.camTick = publication.gameTick;
            aim.camFactor = publication.interpFactor;
            aim.velValid = publication.engineEyeVelValid;
            memcpy(aim.vel, publication.engineEyeVel, sizeof(aim.vel));
        }
    }
    int64_t tick = -1;
    float factor = NAN;
    Halo2ReadGameClock(tick, factor);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    ++w.spawns;
    if (!ours)
        ++w.notOurs;
    else if (!aim.valid)
        ++w.noAim;
    else
    {
        ++w.ours;
        if (velocity && shot_lag::Finite3(velocity))
        {
            ++w.speedN;
            w.speedSum += double(shot_lag::Length(velocity)) * shot_lag::kMetresPerWu;
        }
        Halo2AsSeenRound* slot = nullptr;
        for (auto& r : w.rounds)
            if (!r.used)
            {
                slot = &r;
                break;
            }
        if (!slot)
        {
            slot = &w.rounds[0];
            for (auto& r : w.rounds)
                if (r.fireUs < slot->fireUs)
                    slot = &r;
            ++w.evicted;
        }
        else
            w.live.fetch_add(1, std::memory_order_acq_rel);
        *slot = Halo2AsSeenRound{};
        slot->used = true;
        slot->fireUs = nowUs;
        slot->fireTick = tick;
        memcpy(slot->spawn, spawn, sizeof(slot->spawn));
        memcpy(slot->fwd, fwd, sizeof(slot->fwd));
        slot->aim = aim;
    }
    ReleaseSRWLockExclusive(&w.lock);
}

__declspec(noinline) int32_t __fastcall Halo2ImpactObjectNewDetour(void* placement)
{
    auto& w = g_halo2Impact;
    w.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        const bool fromFire = reinterpret_cast<uintptr_t>(_ReturnAddress()) == w.fireReturn && placement &&
                              w.ready.load(std::memory_order_acquire);
        float spawn[3]{}, fwd[3]{}, vel[3]{};
        bool placed = false, velRead = false;
        if (fromFire)
        {
            __try
            {
                const auto* bytes = static_cast<const uint8_t*>(placement);
                memcpy(spawn, bytes + w.placePos, sizeof(spawn));
                memcpy(fwd, bytes + w.placeFwd, sizeof(fwd));
                memcpy(vel, bytes + w.placeVel, sizeof(vel));
                placed = shot_lag::Finite3(spawn) && shot_lag::Finite3(fwd);
                velRead = shot_lag::Finite3(vel);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                placed = false;
            }
        }
        if (w.original)
            result = w.original(placement);
        if (placed && result != -1)
            Halo2ImpactNoteSpawn(spawn, fwd, velRead ? vel : nullptr);
    }
    __finally
    {
        w.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// ---- the hit: the round's own collision query (Halo2CollisionVectorDetour, after the native) ----------------------
void Halo2ImpactNoteQuery(const float* start, const float* vector, const Halo2CollisionResult* result, uint8_t hit)
{
    auto& w = g_halo2Impact;
    if (!w.ready.load(std::memory_order_acquire) || !w.live.load(std::memory_order_acquire) || !start || !vector)
        return;
    float s[3]{}, v[3]{}, point[3]{};
    bool landed = false;
    __try
    {
        memcpy(s, start, sizeof(s));
        memcpy(v, vector, sizeof(v));
        if (hit && result && std::isfinite(result->fraction) && result->fraction >= 0.0f && result->fraction <= 1.0f)
        {
            memcpy(point, result->position, sizeof(point));
            landed = shot_lag::Finite3(point);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    int64_t tick = -1;
    float factor = NAN;
    Halo2ReadGameClock(tick, factor);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    Halo2AsSeenRound* best = nullptr;
    float bestMiss = 0.0f;
    bool ambiguous = false;
    for (auto& r : w.rounds)
    {
        if (!r.used)
            continue;
        if (nowUs - r.fireUs > 3000000)
        {
            ++w.lost;
            r = Halo2AsSeenRound{};
            w.live.fetch_sub(1, std::memory_order_acq_rel);
            continue;
        }
        float miss = 0.0f;
        if (!as_seen::QueryOnRound(r.spawn, r.fwd, s, v, &miss))
            continue;
        if (best && std::fabs(miss - bestMiss) < 0.01f)
        {
            ambiguous = true;
            if (r.fireUs < best->fireUs)
            {
                best = &r;
                bestMiss = miss;
            }
        }
        else if (!best || miss < bestMiss)
        {
            best = &r;
            bestMiss = miss;
        }
    }
    if (best)
    {
        ++w.matched;
        if (ambiguous)
            ++w.ambiguous;
        ++best->queries;
        if (landed)
        {
            Halo2AsSeenPending* slot = nullptr;
            for (auto& p : w.pending)
                if (!p.used)
                {
                    slot = &p;
                    break;
                }
            if (!slot)
            {
                slot = &w.pending[0];
                for (auto& p : w.pending)
                    if (p.impactUs < slot->impactUs)
                        slot = &p;
                ++w.window[slot->framed && slot->frame.renderer ? 1 : 0].unframed;
                w.live.fetch_sub(1, std::memory_order_acq_rel);
            }
            *slot = Halo2AsSeenPending{};
            slot->used = true;
            memcpy(slot->impact, point, sizeof(point));
            slot->impactUs = nowUs;
            slot->impactTick = tick;
            slot->round = *best;
            *best = Halo2AsSeenRound{};   // its count moves to the pending: `live` unchanged
        }
    }
    ReleaseSRWLockExclusive(&w.lock);
}

// ---- install / remove (with the dual-aim lane: the fire routine's owner proof is that lane's) ---------------------
bool InstallHalo2ImpactWitness(uintptr_t base, size_t size)
{
    auto& w = g_halo2Impact;
    if (w.target || size <= 0x8E5A80)
        return false;
    struct Binding { uint32_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x8D79D0, "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC F8 00 00 00 48 C7 44 24 40 FE FF FF FF 48 8B F1 "
                   "BF FF FF FF FF C6 84 24 48 01 00 00 00 F6 41 18 10"},
        {0x8D7C3E, "F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? "
                   "F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 83 ?? ?? 00 00 F2 0F 10 46 ?? F2 0F 11 83 ?? ?? 00 00 "
                   "8B 46 ?? 89 83"},
        {0x8E5A6B, "48 8D 4D ?? E8 ?? ?? ?? ?? 8B F0 83 F8 FF 0F 84"}};
    for (const auto& binding : bindings)
    {
        uintptr_t match = 0;
        uint32_t count = 0;
        if (!CountPatternMatches(base, size, binding.pattern, match, count) || count != 1 || match != base + binding.rva)
        {
            LOG("Halo 2 AS SEEN witness (dev) StockFallback: binding +0x%X missing or ambiguous", binding.rva);
            return false;
        }
    }
    const auto* c = reinterpret_cast<const uint8_t*>(base + 0x8D7C3E);
    const uint32_t placePos = c[4], placeFwd = c[20], placeUp = c[36], placeVel = c[55];
    const bool coherent = c[12] == placePos + 8 && placeFwd == placePos + 0xC && c[28] == placeFwd + 8 &&
                          placeUp == placeFwd + 0xC && c[44] == placeUp + 8 && placeVel == placeUp + 0xC &&
                          c[66] == placeVel + 8;
    int32_t rel = 0;
    memcpy(&rel, reinterpret_cast<const void*>(base + 0x8E5A6B + 5), sizeof(rel));
    const bool callsObjectNew = base + 0x8E5A6B + 9 + static_cast<intptr_t>(rel) == base + 0x8D79D0;
    if (!coherent || !callsObjectNew)
    {
        LOG("Halo 2 AS SEEN witness (dev) StockFallback: the decoded layout did not prove (placement copy coherent "
            "%d, the fire's call targets object_new %d)", coherent ? 1 : 0, callsObjectNew ? 1 : 0);
        return false;
    }
    w.fireReturn = base + 0x8E5A6B + 9;
    w.placePos = placePos;
    w.placeFwd = placeFwd;
    w.placeVel = placeVel;
    void* const target = reinterpret_cast<void*>(base + 0x8D79D0);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Halo2ImpactObjectNewDetour),
                      reinterpret_cast<void**>(&w.original)) != MH_OK)
    {
        w.original = nullptr;
        LOG("Halo 2 AS SEEN witness (dev) StockFallback: object_new hook creation failed");
        return false;
    }
    w.target = target;
    if (MH_EnableHook(target) != MH_OK)
    {
        (void)MH_RemoveHook(target);
        w.target = nullptr;
        w.original = nullptr;
        LOG("Halo 2 AS SEEN witness (dev) StockFallback: object_new hook enable failed");
        return false;
    }
    AcquireSRWLockExclusive(&w.lock);
    for (auto& r : w.rounds)
        r = Halo2AsSeenRound{};
    for (auto& p : w.pending)
        p = Halo2AsSeenPending{};
    w.live.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
    w.ready.store(true, std::memory_order_release);
    LOG("Halo 2 AS SEEN witness (dev) Installed: object_new +0x8D79D0 read for the fire routine's rounds only (return "
        "+0x8E5A74; placement position +0x%X forward +0x%X velocity +0x%X); impacts from collision_test_vector, "
        "frames from both stereo cores, the reticle from the compositor", placePos, placeFwd, placeVel);
    return true;
}

bool RemoveHalo2ImpactWitness()
{
    auto& w = g_halo2Impact;
    w.ready.store(false, std::memory_order_release);
    if (!w.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(w.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 2 AS SEEN witness (dev) CleanupRequired: disable failed");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo2ImpactObjectNewDetour)};
    const void* originals[]{reinterpret_cast<const void*>(w.original)};
    if (!WaitForNativeDetourQuiescence(functions, originals, 1, w.callbacks))
    {
        LOG("Halo 2 AS SEEN witness (dev) CleanupRequired: callbacks or ingress busy");
        return false;
    }
    const auto removed = MH_RemoveHook(w.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 2 AS SEEN witness (dev) CleanupRequired: removal failed");
        return false;
    }
    w.target = nullptr;
    w.original = nullptr;
    return true;
}

// ---- the frame: a stereo core completed a pair for `serial` (its render thread) --------------------------------------
// `center` the camera the eyes were built from, `headQuat` the head orientation the views were submitted with,
// `publication` the observer publication behind it (null: the core tracked a camera of its own). Every impact whose hit
// this frame's world is past gets this frame; its reticle may already be here (the compositor ran first).
void Halo2AsSeenNoteFrameImpl(int renderer, uint64_t serial, const Halo2CameraBasis& center, const float headQuat[4],
                              const Halo2ObserverPosePublication* publication)
{
    auto& w = g_halo2Impact;
    if (!w.ready.load(std::memory_order_acquire) || !serial || !headQuat)
        return;
    const uint64_t nowUs = Sight_NowUs();
    Halo2AsSeenFrame f{};
    f.valid = true;
    f.renderer = renderer ? 1 : 0;
    f.serial = serial;
    f.stampUs = nowUs;
    f.cameraUs = nowUs;
    memcpy(f.head, center.position, sizeof(f.head));
    memcpy(f.fwd, center.forward, sizeof(f.fwd));
    memcpy(f.up, center.up, sizeof(f.up));
    memcpy(f.quat, headQuat, sizeof(f.quat));
    if (publication)
    {
        Halo2EngineEyePosition(*publication, f.eye);
        memcpy(f.aimFwd, publication->stock.forward, sizeof(f.aimFwd));
        f.eyeValid = shot_lag::Finite3(f.eye) && shot_lag::Finite3(f.aimFwd);
        f.tick = publication->gameTick;
        f.factor = publication->interpFactor;
        if (publication->publishedUs)
            f.cameraUs = publication->publishedUs;
    }
    AcquireSRWLockExclusive(&w.lock);
    ++w.framesNoted[f.renderer];
    if (!publication)
        ++w.framesNoPublication;
    w.frames[w.frameNext++ % Halo2ImpactWitness::kFrames] = f;
    if (f.eyeValid)
        w.census[f.renderer].Add(f.eye, f.cameraUs, f.tick, f.factor);
    if (w.live.load(std::memory_order_acquire))
        for (auto& p : w.pending)
        {
            if (!p.used || p.framed || !Halo2AsSeenFrameShows(f, p))
                continue;
            p.framed = true;
            p.frame = f;
            for (const auto& r : w.reticles)
                if (r.valid && r.serial == serial)
                {
                    Halo2AsSeenEvaluate(p, r);
                    Halo2AsSeenRelease(p);
                    break;
                }
        }
    ReleaseSRWLockExclusive(&w.lock);
}

// ---- the reticle the compositor drew for `serial` (Game_DevNoteTracedReticle) ----------------------------------------
void Halo2AsSeenNoteReticleImpl(uint64_t serial, const float center[3], const float dir[3])
{
    auto& w = g_halo2Impact;
    if (!w.ready.load(std::memory_order_acquire) || !center || !dir)
        return;
    AcquireSRWLockExclusive(&w.lock);
    ++w.reticlesNoted;
    Halo2AsSeenReticle& kept = w.reticles[w.reticleNext++ % Halo2ImpactWitness::kReticles];
    kept.valid = true;
    kept.serial = serial;
    kept.stampUs = Sight_NowUs();
    memcpy(kept.center, center, sizeof(kept.center));
    memcpy(kept.dir, dir, sizeof(kept.dir));
    if (w.live.load(std::memory_order_acquire))
        for (auto& p : w.pending)
        {
            if (!p.used || !p.framed)
                continue;
            if (p.frame.serial == serial)
            {
                Halo2AsSeenEvaluate(p, kept);
                Halo2AsSeenRelease(p);
            }
            else if (p.frame.serial + 3 < serial)
            {
                ++w.noReticle;   // its frame drew no traced reticle (off, a seat, a miss of the gate)
                ++w.window[p.frame.renderer ? 1 : 0].noLine;
                Halo2AsSeenRelease(p);
            }
        }
    ReleaseSRWLockExclusive(&w.lock);
}

// ---- the report (ReportTelemetry, the cold worker) ------------------------------------------------------------------
void ReportHalo2AsSeen()
{
    auto& w = g_halo2Impact;
    if (!w.ready.load(std::memory_order_acquire))
        return;
    AcquireSRWLockExclusive(&w.lock);
    // An impact no frame showed within half a second, or whose frame drew no traced reticle, is dropped (counted).
    const uint64_t nowUs = Sight_NowUs();
    for (auto& p : w.pending)
        if (p.used && nowUs > p.impactUs + 500000)
        {
            ++w.window[p.framed && p.frame.renderer ? 1 : 0].unframed;
            Halo2AsSeenRelease(p);
        }
    as_seen::Window windows[2]{w.window[0], w.window[1]};
    w.window[0] = w.window[1] = as_seen::Window{};
    char census[2][1024]{};
    bool censusAny[2]{};
    for (int r = 0; r < 2; ++r)
    {
        censusAny[r] = w.census[r].Format(census[r], sizeof(census[r]));
        w.census[r].ResetCounts();
    }
    const uint32_t spawns = w.spawns, ours = w.ours, notOurs = w.notOurs, noAim = w.noAim, evicted = w.evicted,
                   lost = w.lost, matched = w.matched, ambiguous = w.ambiguous, impacts = w.impacts,
                   classicFrames = w.framesNoted[0], annivFrames = w.framesNoted[1],
                   noPublication = w.framesNoPublication, reticles = w.reticlesNoted, noReticle = w.noReticle,
                   speedN = w.speedN;
    const double speed = speedN ? w.speedSum / speedN : 0.0;
    w.spawns = w.ours = w.notOurs = w.noAim = w.evicted = w.lost = w.matched = w.ambiguous = w.impacts =
        w.framesNoted[0] = w.framesNoted[1] = w.framesNoPublication = w.reticlesNoted = w.noReticle = w.speedN = 0;
    w.speedSum = 0.0;
    ReleaseSRWLockExclusive(&w.lock);
    static const char* const kNames[2]{"Halo 2 VR CLASSIC", "Halo 2 VR ANNIVERSARY"};
    static char line[4096];
    for (int r = 0; r < 2; ++r)
    {
        char label[320];
        std::snprintf(label, sizeof(label),
                      "%s [vs the reticle the compositor drew in the first frame whose world is past the hit; STOCK "
                      "= the engine eye along the stock camera's aim in that frame; radii = crosshair_size_deg / 2 = "
                      "%.2f deg; ticks from game_time]", kNames[r], static_cast<double>(g_config.crosshair_size_deg * 0.5f));
        if (windows[r].Format(label, line, sizeof(line)))
            LOG("%s", line);
        if (censusAny[r])
            LOG("AS SEEN %s camera (dev; per noted frame, the engine eye of the publication it rendered): %s",
                kNames[r], census[r]);
    }
    if (spawns || classicFrames || annivFrames)
        LOG("AS SEEN Halo 2 VR witness (dev): rounds from the fire routine %u: ours %u (mean speed %.0f m/s), not ours "
            "%u, ours but no publication to aim through %u, evicted %u, lost %u; their queries matched %u (%u "
            "ambiguous); impacts judged %u; frames noted CLASSIC %u, ANNIVERSARY %u (without a publication %u); "
            "reticles noted %u, frames whose reticle never came %u",
            spawns, ours, speed, notOurs, noAim, evicted, lost, matched, ambiguous, impacts, classicFrames,
            annivFrames, noPublication, reticles, noReticle);
}
#endif // HALOMCCVR_DEV_DIAGNOSTICS
