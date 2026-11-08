// Included in game.cpp (anonymous namespace) after reach_muzzle_lifecycle.inl. Dev builds only (the body compiles to
// nothing otherwise).
//
// AS SEEN, Halo Reach in VR (dev). Flat parity for Reach on foot (reach_fire_eye.inl) moves the shot point
// with a MEASURED fire eye; whether that matches what the player sees -- and whether Reach's presented camera trails the
// simulation the way Halo 2's does -- is measured here with the same code as the flat probe (as_seen_logic.h sections
// 4-7 and the VR witness core, section 10), so the VR and flat numbers compare bin for bin:
//
//   * at the fire: the round the projectile transaction 0x4C2710 creates (object_new 0x46D6E0, only its two calls from
//     that transaction, returns 0x4C3CE6 / 0x4C3CF3), ours when the unit-adjust named our unit the shooter (t_reachFire,
//     shot_lag::FireMark) and the unit is on foot; its spawn line, and the aim line it was fired through: the provider's
//     actuated intent ray (its rendered eye H_P along u) and that record's engine eye E_P with its stamp and velocity;
//   * at the hit: the round's own query through collision_test_vector 0x12969C (ReachCollisionVectorDetour, after the
//     native call), matched by geometry (as_seen::QueryOnRound); the hit point = start + fraction x vector;
//   * the FIRST VR frame whose world is past the hit: noted when both eyes of a prepared frame completed
//     (ReachPublishCompletedFrameFeedback), with the head centre camera the eyes were built from, the head orientation
//     their views were submitted with, the stock camera's eye (E) and forward, and MCC's frame clock read at that
//     render (the tick counter of the world it shows and the interpolation factor its camera was built at);
//   * the reticle the compositor drew for that frame's serial (Game_DevNoteTracedReticle).
//
// MCC's frame clock (mcc_frame_clock.h) is decoded from the engine's own code and captured from the game thread's TLS
// block; the camera census then answers directly whether the presented eye moves on frames that ran no tick (it does if
// Reach interpolates its camera, as the static analysis in as_seen_logic.h section 8 says it does).
//
// object_new forwards its one argument exactly (`py -3 tools/audit_caller_area.py haloreach.dll 46D6E0:1:i`: no
// caller-area slot touched, 49 native callers, none relies on a volatile register across the call) and only reads.
// ADDR-HYGIENE: dev-only (a witness; decides nothing). Every anchor is unique and at its pinned RVA, the placement
// offsets decoded from object_new's own copy.

#if HALOMCCVR_DEV_DIAGNOSTICS
using ReachImpactObjectNewFn = int32_t(__fastcall*)(void*);

struct ReachImpactWitness
{
    void* target = nullptr;
    ReachImpactObjectNewFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    uintptr_t fireReturnA = 0, fireReturnB = 0;
    uint32_t placePos = 0, placeFwd = 0, placeVel = 0;
    std::atomic<uint32_t> live{0};   // mirrors core.live: the collision note takes the lock only while any
    SRWLOCK lock = SRWLOCK_INIT;
    as_seen::Witness core{};
    uint32_t seatedFrames = 0, clockMisses = 0;
} g_reachImpact;

// MCC's frame clock in haloreach.dll (dev).
mcc_clock::Clock g_reachClock;

void ReachClockRead(int64_t& tick, float& factor, float* tickMs = nullptr)
{
    (void)g_reachClock.Capture();   // a no-op off the game's threads
    g_reachClock.Read(tick, factor, tickMs);
}

// The aim line a local shot was fired through: the provider's last actuated intent (its ray origin, the rendered eye,
// along u; its engine eye E_P, stamped at its camera sample, with the eye's velocity). Seqlock read.
bool ReachAsSeenReadAim(as_seen::WitnessAim& aim)
{
    auto& a = g_reachSightActuated;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = a.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        as_seen::WitnessAim read{};
        read.head[0] = a.ox.load(std::memory_order_relaxed);
        read.head[1] = a.oy.load(std::memory_order_relaxed);
        read.head[2] = a.oz.load(std::memory_order_relaxed);
        read.dir[0] = a.dx.load(std::memory_order_relaxed);
        read.dir[1] = a.dy.load(std::memory_order_relaxed);
        read.dir[2] = a.dz.load(std::memory_order_relaxed);
        read.eye[0] = a.ex.load(std::memory_order_relaxed);
        read.eye[1] = a.ey.load(std::memory_order_relaxed);
        read.eye[2] = a.ez.load(std::memory_order_relaxed);
        read.vel[0] = a.vx.load(std::memory_order_relaxed);
        read.vel[1] = a.vy.load(std::memory_order_relaxed);
        read.vel[2] = a.vz.load(std::memory_order_relaxed);
        read.velValid = a.velValid.load(std::memory_order_relaxed);
        const uint64_t atUs = a.atUs.load(std::memory_order_relaxed);
        read.camUs = a.eyeStampUs.load(std::memory_order_relaxed);
        if (a.sequence.load(std::memory_order_acquire) != before)
            continue;
        const uint64_t nowUs = Sight_NowUs();
        read.valid = atUs && nowUs >= atUs && nowUs - atUs <= kSightIntentShotMaxAgeMs * 1000ull &&
                     shot_lag::Finite3(read.head) && shot_lag::Finite3(read.dir) && shot_lag::Finite3(read.eye);
        aim = read;
        return read.valid;
    }
    return false;
}

void ReachImpactNoteSpawn(const float spawn[3], const float fwd[3], const float* velocity)
{
    auto& w = g_reachImpact;
    const int32_t local = LegacyCollisionIgnoredObject(GameTitle::HaloReach);
    bool onFoot = false;
    __try
    {
        onFoot = local != -1 && g_reachCamera.unitInVehicle && !g_reachCamera.unitInVehicle(local);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        onFoot = false;
    }
    const bool ours = t_reachFire == shot_lag::kFireLocal && onFoot;
    as_seen::WitnessAim aim{};
    if (ours)
        (void)ReachAsSeenReadAim(aim);
    int64_t tick = -1;
    float factor = NAN;
    ReachClockRead(tick, factor);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    w.core.Spawn(ours, aim, spawn, fwd, velocity, nowUs, tick);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

__declspec(noinline) int32_t __fastcall ReachImpactObjectNewDetour(void* placement)
{
    auto& w = g_reachImpact;
    w.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool fromFire = (caller == w.fireReturnA || caller == w.fireReturnB) && placement &&
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
            ReachImpactNoteSpawn(spawn, fwd, velRead ? vel : nullptr);
    }
    __finally
    {
        w.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// The hit (ReachCollisionVectorDetour, after the native query; every query of every unit comes through, so the lock is
// taken only while a tracked round is in flight). Result: fraction +4 (the layout the trace already reads).
void ReachImpactNoteQuery(const float* start, const float* vector, const void* result, uint8_t hit)
{
    auto& w = g_reachImpact;
    if (!w.ready.load(std::memory_order_acquire) || !w.live.load(std::memory_order_acquire) || !start || !vector)
        return;
    float s[3]{}, v[3]{}, point[3]{};
    bool landed = false;
    __try
    {
        memcpy(s, start, sizeof(s));
        memcpy(v, vector, sizeof(v));
        float fraction = NAN;
        if (hit && result)
        {
            memcpy(&fraction, static_cast<const uint8_t*>(result) + 4, sizeof(fraction));
            if (std::isfinite(fraction) && fraction >= 0.0f && fraction <= 1.0f)
            {
                for (int axis = 0; axis < 3; ++axis)
                    point[axis] = s[axis] + v[axis] * fraction;
                landed = shot_lag::Finite3(point);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    int64_t tick = -1;
    float factor = NAN;
    ReachClockRead(tick, factor);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    (void)w.core.Query(s, v, landed, point, nowUs, tick);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

// The frame: both eyes of prepared frame `serial` completed (ReachPublishCompletedFrameFeedback, the render thread).
void ReachAsSeenNoteFrameImpl(uint32_t generation, uint64_t serial, const ReachOwnerScope& owner)
{
    auto& w = g_reachImpact;
    if (!w.ready.load(std::memory_order_acquire) || !generation || !serial)
        return;
    if (owner.vehicleViewApplied || owner.vehicle.active || owner.cutsceneTheater)
    {
        AcquireSRWLockExclusive(&w.lock);
        ++w.seatedFrames;
        ReleaseSRWLockExclusive(&w.lock);
        return;
    }
    as_seen::WitnessFrame f{};
    f.valid = true;
    f.serial = serial;
    f.stampUs = f.cameraUs = Sight_NowUs();
    memcpy(f.head, owner.headCenter + 0x00, sizeof(f.head));
    memcpy(f.fwd, owner.headCenter + 0x0C, sizeof(f.fwd));
    memcpy(f.up, owner.headCenter + 0x18, sizeof(f.up));
    memcpy(f.quat, owner.asSeenHeadQuat, sizeof(f.quat));
    memcpy(f.eye, owner.presentedCameraBasePosition, sizeof(f.eye));
    memcpy(f.aimFwd, owner.presentedCameraForward, sizeof(f.aimFwd));
    f.eyeValid = shot_lag::Finite3(f.eye) && shot_lag::Finite3(f.aimFwd);
    ReachClockRead(f.tick, f.factor);
    AcquireSRWLockExclusive(&w.lock);
    if (f.tick < 0)
        ++w.clockMisses;
    w.core.Frame(f);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}
void ReachAsSeenNoteFrame(uint32_t generation, uint64_t serial, const ReachOwnerScope& owner)
{
    __try
    {
        ReachAsSeenNoteFrameImpl(generation, serial, owner);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// The reticle the compositor drew for `serial` (Game_DevNoteTracedReticle).
void ReachAsSeenNoteReticle(uint64_t serial, const float dir[3])
{
    auto& w = g_reachImpact;
    if (!w.ready.load(std::memory_order_acquire) || !dir)
        return;
    AcquireSRWLockExclusive(&w.lock);
    w.core.Reticle(serial, Sight_NowUs(), dir);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

// ---- install / remove (with the Reach core: after the muzzle lane, before it on teardown) ------------------------
bool InstallReachImpactWitness(uintptr_t base, size_t size)
{
    auto& w = g_reachImpact;
    const char* clockWhy = nullptr;
    if (!g_reachClock.Resolve(base, size, mcc_clock::kReach, &clockWhy))
        LOG("Reach AS SEEN witness (dev): MCC's frame clock did not resolve (%s); impacts are framed by time and the "
            "census has no ticks", clockWhy ? clockWhy : "?");
    else
        LOG("Reach AS SEEN witness (dev): MCC's frame clock decoded from code -- TLS index +0x%X, game_time TLS+0x%X "
            "(tick +0x%X, fraction +0x%X, seconds per tick +0x%X), interpolator TLS+0x%X (factor +0x%X)",
            g_reachClock.layout.tlsIndexRva, g_reachClock.layout.gameTimeSlot, g_reachClock.layout.tickOffset,
            g_reachClock.layout.fractionOffset, g_reachClock.layout.secondsPerTickOffset,
            g_reachClock.layout.interpSlot, g_reachClock.layout.factorOffset);
    if (w.target || size <= 0x4C3D00)
        return false;
    struct Binding { uint32_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x46D6E0, "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC A8 00 00 00 8B 41 18 49 83 CC FF 45 33 C0 C1 E8 04"},
        // object_new copies FIVE placement vectors with the same eighteen bytes each (position, forward, up,
        // velocity, angular velocity). The pattern must hold all FIVE: one that holds four also matches one
        // copy later (+0x46D9B8), so its own uniqueness re-scan calls it ambiguous on the retail image and the
        // witness logs "StockFallback: binding +0x46D9A6 missing or ambiguous" at every Reach level.
        // All five copies match once, here.
        {0x46D9A6, "F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47 ?? F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 "
                   "47 ?? F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47 ?? F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B "
                   "46 ?? 89 47 ?? F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47"},
        {0x4C3CDA, "48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? 45 33 DB E9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B F8 3B C3 0F 84"}};
    for (const auto& binding : bindings)
    {
        const uintptr_t match = sig::Find(base, size, binding.pattern);
        if (match != base + binding.rva || sig::Find(match + 1, base + size - match - 1, binding.pattern))
        {
            LOG("Reach AS SEEN witness (dev) StockFallback: binding +0x%X missing or ambiguous", binding.rva);
            return false;
        }
    }
    // object_new's copy: position, forward, up, velocity -- each a movsd + mov of one placement vector into the
    // object, the four placement offsets 0xC apart (decoded: +0x1C, +0x28, +0x34, +0x40 on the pinned module).
    const auto* c = reinterpret_cast<const uint8_t*>(base + 0x46D9A6);
    const uint32_t placePos = c[5], placeFwd = c[23], placeUp = c[41], placeVel = c[59];
    const bool coherent = c[14] == placePos + 8 && placeFwd == placePos + 0xC && c[32] == placeFwd + 8 &&
                          placeUp == placeFwd + 0xC && c[50] == placeUp + 8 && placeVel == placeUp + 0xC;
    const auto rel32 = [](uintptr_t at) {
        int32_t rel = 0;
        memcpy(&rel, reinterpret_cast<const void*>(at), sizeof(rel));
        return rel;
    };
    const uintptr_t callA = base + 0x4C3CDA + 7, callB = base + 0x4C3CDA + 20;
    const bool callsObjectNew = callA + 5 + static_cast<intptr_t>(rel32(callA + 1)) == base + 0x46D6E0 &&
                                callB + 5 + static_cast<intptr_t>(rel32(callB + 1)) == base + 0x46D6E0;
    // Both calls hand object_new the same placement block (the first: lea rcx,[rbp+disp32] right before it).
    if (!coherent || !callsObjectNew)
    {
        LOG("Reach AS SEEN witness (dev) StockFallback: the decoded layout did not prove (placement copy coherent %d, "
            "the transaction's calls target object_new %d)", coherent ? 1 : 0, callsObjectNew ? 1 : 0);
        return false;
    }
    w.fireReturnA = callA + 5;
    w.fireReturnB = callB + 5;
    w.placePos = placePos;
    w.placeFwd = placeFwd;
    w.placeVel = placeVel;
    void* const target = reinterpret_cast<void*>(base + 0x46D6E0);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&ReachImpactObjectNewDetour),
                      reinterpret_cast<void**>(&w.original)) != MH_OK)
    {
        w.original = nullptr;
        LOG("Reach AS SEEN witness (dev) StockFallback: object_new hook creation failed");
        return false;
    }
    w.target = target;
    if (MH_EnableHook(target) != MH_OK)
    {
        (void)MH_RemoveHook(target);
        w.target = nullptr;
        w.original = nullptr;
        LOG("Reach AS SEEN witness (dev) StockFallback: object_new hook enable failed");
        return false;
    }
    AcquireSRWLockExclusive(&w.lock);
    w.core.Clear();
    w.core.radiusDeg = g_config.crosshair_size_deg * 0.5f;
    w.live.store(0, std::memory_order_release);
    w.seatedFrames = w.clockMisses = 0;
    ReleaseSRWLockExclusive(&w.lock);
    w.ready.store(true, std::memory_order_release);
    LOG("Reach AS SEEN witness (dev) Installed: object_new +0x46D6E0 read for the projectile transaction's rounds only "
        "(returns +0x%llX, +0x%llX; placement position +0x%X forward +0x%X velocity +0x%X); impacts from "
        "collision_test_vector, frames from the completed stereo pair, the reticle from the compositor",
        static_cast<unsigned long long>(w.fireReturnA - base), static_cast<unsigned long long>(w.fireReturnB - base),
        placePos, placeFwd, placeVel);
    return true;
}

bool RemoveReachImpactWitness()
{
    auto& w = g_reachImpact;
    w.ready.store(false, std::memory_order_release);
    g_reachClock.Reset();
    if (!w.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(w.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("Reach AS SEEN witness (dev) CleanupRequired: disable failed");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&ReachImpactObjectNewDetour)};
    const void* originals[]{reinterpret_cast<const void*>(w.original)};
    if (!WaitForNativeDetourQuiescence(functions, originals, 1, w.callbacks))
    {
        LOG("Reach AS SEEN witness (dev) CleanupRequired: callbacks or ingress busy");
        return false;
    }
    const auto removed = MH_RemoveHook(w.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("Reach AS SEEN witness (dev) CleanupRequired: removal failed");
        return false;
    }
    w.target = nullptr;
    w.original = nullptr;
    return true;
}

// The report (ReportReachSightTrace, the cold worker).
void ReportReachAsSeen()
{
    auto& w = g_reachImpact;
    if (!w.ready.load(std::memory_order_acquire))
        return;
    AcquireSRWLockExclusive(&w.lock);
    w.core.Expire(Sight_NowUs());
    w.live.store(w.core.live, std::memory_order_release);
    const as_seen::Window window = w.core.window;
    char census[1024]{}, counts[640]{};
    const bool censusAny = w.core.census.Format(census, sizeof(census));
    const bool countsAny = w.core.FormatCounts(counts, sizeof(counts));
    const uint32_t seatedFrames = w.seatedFrames, clockMisses = w.clockMisses;
    w.core.ResetCounts();
    w.seatedFrames = w.clockMisses = 0;
    ReleaseSRWLockExclusive(&w.lock);
    static char line[4096];
    char label[320];
    std::snprintf(label, sizeof(label),
                  "Halo Reach VR [vs the reticle the compositor drew in the first frame whose world is past the hit; "
                  "STOCK = the engine eye along the stock camera's forward in that frame; radii = crosshair_size_deg "
                  "/ 2 = %.2f deg; ticks from MCC's frame clock]", static_cast<double>(g_config.crosshair_size_deg * 0.5f));
    if (window.Format(label, line, sizeof(line)))
        LOG("%s", line);
    if (censusAny)
        LOG("AS SEEN Halo Reach VR camera (dev; per completed frame, the stock camera's eye): %s", census);
    if (countsAny)
        LOG("AS SEEN Halo Reach VR witness (dev): %s; frames in a seat or cutscene (not noted) %u, frames without the "
            "frame clock %u (clock captures %u)", counts, seatedFrames, clockMisses,
            g_reachClock.captures.load(std::memory_order_relaxed));
}
#endif // HALOMCCVR_DEV_DIAGNOSTICS
