// Included in game.cpp (anonymous namespace) after halo4_fire_eye.inl. Dev builds only (the body compiles to nothing
// otherwise).
//
// AS SEEN, Halo 4 in VR (dev): the same measurement as Reach's (reach_impact_witness.inl) and the flat probe,
// with the shared VR witness core (as_seen_logic.h section 10):
//
//   * at the fire: the round the fire routine 0x6176B8 creates (object_new 0x5D2034, only its two calls from that
//     routine, returns 0x618C88 / 0x618C92), ours when the fire's own aim call (0x5F3510 from 0x617E90, the muzzle
//     lane's Halo4MuzzleAimDetour) named our on-foot unit the shooter in this fire on this thread; the aim line it was
//     fired through: the published intent (its rendered eye H_P along u) and that intent's camera sample E_P (the
//     controller root: the stock observer camera before the HMD pose), with its stamp and velocity;
//   * at the hit: the round's own ray through PhysicsRayCast 0x1C1D4C (Halo4PhysicsRayCastDetour, after the native
//     call; Halo 4 collides with Havok rays), matched by geometry (as_seen::QueryOnRound); the hit point = start +
//     fraction x (end - start);
//   * the FIRST VR frame whose world is past the hit: noted when Halo4StereoTransaction completed both eyes, with the
//     centre camera the eyes were built from, the head orientation of the prepared snapshot, the stock observer eye and
//     forward (the controller root), and MCC's frame clock read at that render;
//   * the reticle the compositor drew for that frame's serial (Game_DevNoteTracedReticle).
//
// object_new forwards its one argument exactly (`py -3 tools/audit_caller_area.py halo4.dll 5D2034:1:i`: no caller-area
// slot touched, 74 native callers, none relies on a volatile register across the call) and only reads.
// ADDR-HYGIENE: dev-only (a witness; decides nothing). Anchors unique at their pinned RVAs, placement offsets decoded
// from object_new's own copy.

#if HALOMCCVR_DEV_DIAGNOSTICS
using Halo4ImpactObjectNewFn = int32_t(__fastcall*)(void*);

struct Halo4ImpactWitness
{
    void* target = nullptr;
    Halo4ImpactObjectNewFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    uintptr_t fireReturnA = 0, fireReturnB = 0;
    uint32_t placePos = 0, placeFwd = 0, placeVel = 0;
    std::atomic<uint32_t> live{0};
    SRWLOCK lock = SRWLOCK_INIT;
    as_seen::Witness core{};
    uint32_t seatedFrames = 0, clockMisses = 0, raysNoted = 0;
} g_halo4Impact;

// The fire's aim call named its shooter (Halo4MuzzleAimDetour): our on-foot unit, for this fire on this thread.
void Halo4AsSeenNoteFireUnit(uint32_t unit)
{
    t_halo4AsSeenFireUnit = unit;
}

bool Halo4AsSeenReadAim(as_seen::WitnessAim& aim)
{
    SightIntent intent{};
    if (!Sight_ReadIntent(GameTitle::Halo4, intent) || intent.p.seated || !intent.p.pointEyeValid ||
        intent.p.generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
        return false;
    const uint64_t nowUs = Sight_NowUs();
    if (!intent.p.stampUs || nowUs < intent.p.stampUs || nowUs - intent.p.stampUs > kSightIntentShotMaxAgeMs * 1000ull)
        return false;
    memcpy(aim.head, intent.p.head, sizeof(aim.head));
    memcpy(aim.dir, intent.p.dir, sizeof(aim.dir));
    memcpy(aim.eye, intent.p.pointEye, sizeof(aim.eye));
    aim.camUs = intent.p.pointEyeStampUs;
    aim.velValid = intent.p.eyeVelValid;
    memcpy(aim.vel, intent.p.eyeVel, sizeof(aim.vel));
    aim.valid = shot_lag::Finite3(aim.head) && shot_lag::Finite3(aim.dir) && shot_lag::Finite3(aim.eye);
    return aim.valid;
}

void Halo4ImpactNoteSpawn(const float spawn[3], const float fwd[3], const float* velocity)
{
    auto& w = g_halo4Impact;
    const uint32_t shooter = t_halo4AsSeenFireUnit;
    const int32_t local = g_halo4SightUnit.load(std::memory_order_relaxed);
    const bool ours = shooter != UINT32_MAX && local != -1 && shooter == static_cast<uint32_t>(local);
    as_seen::WitnessAim aim{};
    if (ours)
        (void)Halo4AsSeenReadAim(aim);
    int64_t tick = -1;
    float factor = NAN;
    Halo4ClockRead(tick, factor);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    w.core.Spawn(ours, aim, spawn, fwd, velocity, nowUs, tick);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

__declspec(noinline) int32_t __fastcall Halo4ImpactObjectNewDetour(void* placement)
{
    auto& w = g_halo4Impact;
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
            Halo4ImpactNoteSpawn(spawn, fwd, velRead ? vel : nullptr);
    }
    __finally
    {
        w.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// The hit (Halo4PhysicsRayCastDetour, after the native ray; every engine ray comes through, so the lock is taken only
// while a tracked round is in flight).
void Halo4ImpactNoteRay(const Halo4PhysicsRayCastInput* input, const Halo4PhysicsRayCastResult* output, bool returned)
{
    auto& w = g_halo4Impact;
    if (!w.ready.load(std::memory_order_acquire) || !w.live.load(std::memory_order_acquire) || !input)
        return;
    float s[3]{}, v[3]{}, point[3]{};
    bool landed = false;
    __try
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            s[axis] = input->start[axis];
            v[axis] = input->end[axis] - input->start[axis];
        }
        if (returned && output && std::isfinite(output->fraction) && output->fraction >= 0.0f &&
            output->fraction <= 1.0f)
        {
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = s[axis] + v[axis] * output->fraction;
            landed = shot_lag::Finite3(point);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    int64_t tick = -1;
    float factor = NAN;
    Halo4ClockRead(tick, factor);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    ++w.raysNoted;
    (void)w.core.Query(s, v, landed, point, nowUs, tick);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

// The frame (Halo4StereoTransaction, both eyes complete; the render thread).
void Halo4AsSeenNoteFrameImpl(uint64_t serial, const Halo4CameraBasis& center, const float headQuat[4],
                              const Halo4CameraBasis& engine)
{
    auto& w = g_halo4Impact;
    if (!w.ready.load(std::memory_order_acquire) || !serial || !headQuat)
        return;
    Halo4VehicleInputState seat{};
    if (!Halo4ReadVehicleInput(seat) || seat.seated)
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
    memcpy(f.head, center.position, sizeof(f.head));
    memcpy(f.fwd, center.forward, sizeof(f.fwd));
    memcpy(f.up, center.up, sizeof(f.up));
    memcpy(f.quat, headQuat, sizeof(f.quat));
    memcpy(f.eye, engine.position, sizeof(f.eye));
    memcpy(f.aimFwd, engine.forward, sizeof(f.aimFwd));
    f.eyeValid = shot_lag::Finite3(f.eye) && shot_lag::Finite3(f.aimFwd);
    Halo4ClockRead(f.tick, f.factor);
    AcquireSRWLockExclusive(&w.lock);
    if (f.tick < 0)
        ++w.clockMisses;
    w.core.Frame(f);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}
void Halo4AsSeenNoteFrame(uint64_t serial, const Halo4CameraBasis& center, const float headQuat[4],
                          const Halo4CameraBasis& engine)
{
    __try
    {
        Halo4AsSeenNoteFrameImpl(serial, center, headQuat, engine);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void Halo4AsSeenNoteReticle(uint64_t serial, const float dir[3])
{
    auto& w = g_halo4Impact;
    if (!w.ready.load(std::memory_order_acquire) || !dir)
        return;
    AcquireSRWLockExclusive(&w.lock);
    w.core.Reticle(serial, Sight_NowUs(), dir);
    w.live.store(w.core.live, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

// ---- install / remove (after the muzzle lane, whose aim detour names the shooter; before it on teardown) ----------
bool InstallHalo4ImpactWitness(uintptr_t base, size_t size)
{
    auto& w = g_halo4Impact;
    if (w.target || size <= 0x618D00)
        return false;
    struct Binding { uint32_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x5D2034, "48 8B C4 48 89 48 08 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC C8 00 00 00 0F 29 70 A8 4C 8D 69 18 "
                   "41 8B 45 00"},
        {0x5D22D9, "F2 41 0F 10 46 ?? F2 0F 11 46 ?? 41 8B 46 ?? 89 46 ?? F2 41 0F 10 46 ?? F2 0F 11 46 ?? 41 8B 46 ?? 89 "
                   "46 ?? F2 41 0F 10 46 ?? F2 0F 11 46 ?? 41 8B 46 ?? 89 86 ?? ?? 00 00 F2 41 0F 10 46 ?? F2 0F 11 86 "
                   "?? ?? 00 00 41 8B 46 ?? 89 86"},
        {0x618C7C, "48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B E0 3B C3 0F 84"}};
    for (const auto& binding : bindings)
    {
        const uintptr_t match = sig::Find(base, size, binding.pattern);
        if (match != base + binding.rva || sig::Find(match + 1, base + size - match - 1, binding.pattern))
        {
            LOG("Halo 4 AS SEEN witness (dev) StockFallback: binding +0x%X missing or ambiguous", binding.rva);
            return false;
        }
    }
    const auto* c = reinterpret_cast<const uint8_t*>(base + 0x5D22D9);
    const uint32_t placePos = c[5], placeFwd = c[23], placeUp = c[41], placeVel = c[62];
    const bool coherent = c[14] == placePos + 8 && placeFwd == placePos + 0xC && c[32] == placeFwd + 8 &&
                          placeUp == placeFwd + 0xC && c[50] == placeUp + 8 && placeVel == placeUp + 0xC &&
                          c[74] == placeVel + 8;
    const auto rel32 = [](uintptr_t at) {
        int32_t rel = 0;
        memcpy(&rel, reinterpret_cast<const void*>(at), sizeof(rel));
        return rel;
    };
    const uintptr_t callA = base + 0x618C7C + 7, callB = base + 0x618C7C + 17;
    const bool callsObjectNew = callA + 5 + static_cast<intptr_t>(rel32(callA + 1)) == base + 0x5D2034 &&
                                callB + 5 + static_cast<intptr_t>(rel32(callB + 1)) == base + 0x5D2034;
    if (!coherent || !callsObjectNew)
    {
        LOG("Halo 4 AS SEEN witness (dev) StockFallback: the decoded layout did not prove (placement copy coherent "
            "%d, the fire's calls target object_new %d)", coherent ? 1 : 0, callsObjectNew ? 1 : 0);
        return false;
    }
    w.fireReturnA = callA + 5;
    w.fireReturnB = callB + 5;
    w.placePos = placePos;
    w.placeFwd = placeFwd;
    w.placeVel = placeVel;
    void* const target = reinterpret_cast<void*>(base + 0x5D2034);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Halo4ImpactObjectNewDetour),
                      reinterpret_cast<void**>(&w.original)) != MH_OK)
    {
        w.original = nullptr;
        LOG("Halo 4 AS SEEN witness (dev) StockFallback: object_new hook creation failed");
        return false;
    }
    w.target = target;
    if (MH_EnableHook(target) != MH_OK)
    {
        (void)MH_RemoveHook(target);
        w.target = nullptr;
        w.original = nullptr;
        LOG("Halo 4 AS SEEN witness (dev) StockFallback: object_new hook enable failed");
        return false;
    }
    AcquireSRWLockExclusive(&w.lock);
    w.core.Clear();
    w.core.radiusDeg = g_config.crosshair_size_deg * 0.5f;
    w.live.store(0, std::memory_order_release);
    w.seatedFrames = w.clockMisses = w.raysNoted = 0;
    ReleaseSRWLockExclusive(&w.lock);
    w.ready.store(true, std::memory_order_release);
    LOG("Halo 4 AS SEEN witness (dev) Installed: object_new +0x5D2034 read for the fire routine's rounds only (returns "
        "+0x%llX, +0x%llX; placement position +0x%X forward +0x%X velocity +0x%X); impacts from PhysicsRayCast%s, "
        "frames from the completed stereo transaction, the reticle from the compositor",
        static_cast<unsigned long long>(w.fireReturnA - base), static_cast<unsigned long long>(w.fireReturnB - base),
        placePos, placeFwd, placeVel,
        g_halo4WorldCollision.originalRayCast ? "" : " (NOT hooked: the world contact lane is off, so no impact can be "
                                                     "matched)");
    return true;
}

bool RemoveHalo4ImpactWitness()
{
    auto& w = g_halo4Impact;
    w.ready.store(false, std::memory_order_release);
    if (!w.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(w.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 4 AS SEEN witness (dev) CleanupRequired: disable failed");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo4ImpactObjectNewDetour)};
    const void* originals[]{reinterpret_cast<const void*>(w.original)};
    if (!WaitForNativeDetourQuiescence(functions, originals, 1, w.callbacks))
    {
        LOG("Halo 4 AS SEEN witness (dev) CleanupRequired: callbacks or ingress busy");
        return false;
    }
    const auto removed = MH_RemoveHook(w.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 4 AS SEEN witness (dev) CleanupRequired: removal failed");
        return false;
    }
    w.target = nullptr;
    w.original = nullptr;
    return true;
}

void ReportHalo4AsSeen()
{
    auto& w = g_halo4Impact;
    if (!w.ready.load(std::memory_order_acquire))
        return;
    AcquireSRWLockExclusive(&w.lock);
    w.core.Expire(Sight_NowUs());
    w.live.store(w.core.live, std::memory_order_release);
    const as_seen::Window window = w.core.window;
    char census[1024]{}, counts[640]{};
    const bool censusAny = w.core.census.Format(census, sizeof(census));
    const bool countsAny = w.core.FormatCounts(counts, sizeof(counts));
    const uint32_t seatedFrames = w.seatedFrames, clockMisses = w.clockMisses, rays = w.raysNoted;
    w.core.ResetCounts();
    w.seatedFrames = w.clockMisses = w.raysNoted = 0;
    ReleaseSRWLockExclusive(&w.lock);
    static char line[4096];
    char label[320];
    std::snprintf(label, sizeof(label),
                  "Halo 4 VR [vs the reticle the compositor drew in the first frame whose world is past the hit; STOCK = "
                  "the observer eye along the observer forward in that frame; radii = crosshair_size_deg / 2 = %.2f "
                  "deg; ticks from MCC's frame clock]", static_cast<double>(g_config.crosshair_size_deg * 0.5f));
    if (window.Format(label, line, sizeof(line)))
        LOG("%s", line);
    if (censusAny)
        LOG("AS SEEN Halo 4 VR camera (dev; per completed frame, the observer eye): %s", census);
    if (countsAny)
        LOG("AS SEEN Halo 4 VR witness (dev): %s; rays read while a round flew %u; frames in a seat (not noted) %u, "
            "frames without the frame clock %u (clock captures %u)", counts, rays, seatedFrames, clockMisses,
            g_halo4Clock.captures.load(std::memory_order_relaxed));
}
#endif // HALOMCCVR_DEV_DIAGNOSTICS
