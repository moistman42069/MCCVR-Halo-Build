// Included in game.cpp (anonymous namespace) BEFORE halo3_contact_melee_runtime.inl and
// odst_contact_melee_runtime.inl, whose per-unit update hooks call LegacySightTraceTick (each hands it its
// own object-table reader, which is defined there).
//
// TRACE-AND-REAIM for Halo 3 and ODST, on foot and in C20 seats (sight_layer.h) -- Reach's aim
// (reach_sight_trace.inl) for two titles whose bent aim is written by direct drive (aim_direct_drive, default 1;
// legacy_direct_drive.inl) or, with it off or not yet armed, steered by the right-stick servo.
//
// The player points along the intent ray: from the rendered centre eye (g_cam*, the camera after head
// look) along the controller. The engine fires from its own eye (g_baseCam*, the camera before head look:
// the seat re-origin in Game_ComputeAimStick already treats it as the shot origin), so a servo that chases
// the controller's direction leaves every shot parallel to the sight line, off by the head's offset. The
// servo chases the direction from the engine eye through the point the intent ray hits:
//
//      aim = normalize((H - E) + u * d)        H the rendered eye, u the controller, E the engine eye
//
// d is measured by tracing the intent ray with the title's own collision_test_vector and the engine's own
// crosshair-trace flags, on the simulation thread, from inside the per-unit update the contact-melee lane
// already hooks (a native context in which the game itself runs collision queries). The input thread
// publishes the ray (Sight_PublishRay) and bends the servo target with the latest fresh range.
//
// THE FLAGS, resolved per build and value-checked (never baked in). Both titles trace their crosshair the
// way Reach does (halo3 0x13BAD0, the firing-assist function the dual-aim lane hooks; odst 0x161488 and
// 0x3A9128): `mov rax, [flags global]; mov ecx, eax; or ecx, 0x202000; shr rax, 32` feeding a call to
// collision_test_vector, mode 0, ignoring the unit's root. The global is built by a CRT initializer from
// 0x809 (| 0x24000, high | 1) -- exactly Reach's HREK value, 0x0000000100024809 (read statically from both
// retail modules: halo3 0x5714 -> 0x5730 -> 0x5778, odst 0x5A38). The rungs, each failing CLOSED (no trace;
// the servo keeps its old target):
//   1. ANCHORS  every `or ecx, 0x202000` in .text passes ReachInspectAimTraceSite against the hooked
//               collision_test_vector (Halo 3 has one; ODST two), and all of them name one global;
//   2. PINNED   that global is the flags global the contact-melee install pinned independently, from its
//               own call site (halo3 0x35B265, odst 0x39FE8E);
//   3. VALUE    it holds 0x0000000100024809 (ValueAgreement).
// ADDR-HYGIENE: resolved + guarded (the anchors and the value are the same HREK-derived facts Reach uses;
// the retail offsets above are only where the anchors matched, recorded for the next reader).

struct LegacySightState
{
    std::atomic<uint64_t> flags{0};                       // 0 = unresolved: the trace stays off
    std::atomic<const char*> why{"the title is not installed"};
    std::atomic<uint32_t> anchors{0}, globalRva{0};
    std::atomic<uint64_t> converged{0}, fallback{0};      // servo targets, counted on the input thread
    // Seated traces (sim thread): how many ran, how many passes met a part of our own vehicle and looked
    // past it, how many seats were refused because the aim path named no vehicle, and how many fell back
    // to the published seat chain because the object table could not be walked.
    std::atomic<uint64_t> seatedTraces{0}, selfHits{0}, seatUnnamed{0}, rootFallbacks{0};
    // Dev: the return address of each crosshair trace's collision call (the engine-crosshair check).
    std::atomic<uintptr_t> siteReturn[4]{};
};
// [0] Halo 3, [1] ODST, [2] Halo 4 (Halo 4's query is PhysicsRayCast with the contact lane's pinned melee
// flags, see halo4_contact_melee_runtime.inl: Halo4SightTraceTick; its `flags` is only "available").
LegacySightState g_legacySight[3];

// The simulation tick period of the local unit's per-unit update (Halo 3, ODST), microseconds, smoothed; 0 until
// measured. Every build: the record recomposition (legacy_direct_drive.inl) carries the game's eye one tick ahead.
std::atomic<uint32_t> g_legacyTickPeriodUs[2]{};
// The local unit's simulation ticks seen (Halo 3, ODST), wrapping: which stick call's record a later tick consumed
// (seat_upstream::WriteRing, legacy_direct_drive.inl's seated record upstream of replication).
std::atomic<uint32_t> g_legacySimTicks[2]{};

LegacySightState& LegacySight(GameTitle title)
{
    return g_legacySight[title == GameTitle::Halo3ODST ? 1 : title == GameTitle::Halo4 ? 2 : 0];
}

const char* LegacySightTitleName(GameTitle title)
{
    return title == GameTitle::Halo3ODST ? "ODST" : title == GameTitle::Halo4 ? "Halo 4" : "Halo 3";
}

// Install path (cold, once per title install, after the contact install). Clears first, so a failed resolve
// on a new build never inherits an earlier answer.
void LegacyResolveSightFlags(GameTitle title, uintptr_t base, size_t size,
                             const LegacyWorldCollisionFeature& collision, const uint64_t* pinnedGlobal)
{
    auto& state = LegacySight(title);
    state.flags.store(0, std::memory_order_release);
    state.anchors.store(0, std::memory_order_relaxed);
    state.globalRva.store(0, std::memory_order_relaxed);
    for (auto& site : state.siteReturn)
        site.store(0, std::memory_order_relaxed);
    Sight_ResetTitle(title);
    static constexpr char kAnchor[] = "81 C9 00 20 20 00";   // or ecx, 0x202000
    const uintptr_t collisionTest = reinterpret_cast<uintptr_t>(collision.target);
    uintptr_t textBegin = 0, textEnd = 0, global = 0;
    uint32_t stamp = 0, anchors = 0;
    size_t imageSize = 0;
    uint64_t value = 0;
    const char* why = nullptr;
    if (!collision.installed.load(std::memory_order_acquire) || !collision.originalIsVectorTest || !collisionTest)
        why = "collision_test_vector is not hooked on this build";
    else if (!pinnedGlobal)
        why = "the contact-melee install did not pin a collision-flags global";
    else if (!ResolveOdstTextRange(base, size, textBegin, textEnd, stamp, imageSize))
        why = "the module has no .text section";
    else
    {
        for (uintptr_t at = sig::Find(textBegin, textEnd - textBegin, kAnchor); at && !why;
             at = at + 1 < textEnd ? sig::Find(at + 1, textEnd - at - 1, kAnchor) : 0)
        {
            uintptr_t named = 0;
            if ((why = ReachInspectAimTraceSite(at, textBegin, textEnd, base, size, collisionTest, named)) != nullptr)
                break;
            if (global && named != global)
                why = "two crosshair traces name different flags globals";
            global = named;
            // The collision call the inspector matched: its return address identifies the site's queries.
            if (anchors < 4)
            {
                const auto* code = reinterpret_cast<const uint8_t*>(at);
                for (uintptr_t fwd = 6; fwd <= 0x100 && at + fwd + 5 <= textEnd; ++fwd)
                {
                    int32_t rel = 0;
                    if (code[fwd] != 0xE8)
                        continue;
                    memcpy(&rel, code + fwd + 1, sizeof(rel));
                    if (at + fwd + 5 + static_cast<intptr_t>(rel) == collisionTest)
                    {
                        state.siteReturn[anchors].store(at + fwd + 5, std::memory_order_relaxed);
                        break;
                    }
                }
            }
            if (++anchors > 4)
                why = "more crosshair-trace anchors than any known build has";
        }
        if (!why && !anchors)
            why = "the anchor `or ecx, 0x202000` is missing";
        else if (!why && global != reinterpret_cast<uintptr_t>(pinnedGlobal))
            why = "the crosshair trace's flags global is not the one the contact install pinned";
        else if (!why && !ReachGuardedReadQword(global, value))
            why = "the flags global is unreadable";
        else if (!why && value != kReachCollisionTestFlagsInit)
            why = "the flags global disagrees with the editing kit's initial value";
    }
    if (why)
    {
        state.why.store(why, std::memory_order_release);
        LOG("SIGHT %s aim-trace flags NOT resolved: %s (%u anchors, global +0x%llX = 0x%llX, expected 0x%llX); "
            "no trace, the aim servo keeps its old target",
            LegacySightTitleName(title), why, anchors,
            global ? static_cast<unsigned long long>(global - base) : 0ull, static_cast<unsigned long long>(value),
            static_cast<unsigned long long>(kReachCollisionTestFlagsInit));
        return;
    }
    const uint64_t flags = value | kReachAimTraceFlagsOr;
    state.anchors.store(anchors, std::memory_order_relaxed);
    state.globalRva.store(static_cast<uint32_t>(global - base), std::memory_order_relaxed);
    state.why.store(nullptr, std::memory_order_release);
    state.flags.store(flags, std::memory_order_release);
    LOG("SIGHT %s aim-trace flags resolved: %u crosshair-trace anchor(s), global +0x%llX = 0x%llX (the editing "
        "kit's value, and the global the contact install pinned); tracing with 0x%llX, mode %d",
        LegacySightTitleName(title), anchors, static_cast<unsigned long long>(global - base),
        static_cast<unsigned long long>(value), static_cast<unsigned long long>(flags), kReachAimTraceMode);
    // What those flags let the ray stop on, decoded with this title's own layout (sight_hit_class_logic.h).
    Sight_AnnounceTraceQuery(title, title == GameTitle::Halo3ODST ? sight_hit::Engine::ODST : sight_hit::Engine::Halo3,
        flags, "collision_test_vector, mode 0", "the engine's own crosshair trace, resolved and value-checked",
        "on foot it ignores the unit (the engine skips the held weapon with it); seated it ignores the vehicle's "
        "root, the occupant and the seat's parent, the engine skips the rest of that vehicle, and a boarder on it "
        "stays a target");
}

// Seated, a pass that meets our own vehicle continues 5 cm past the contact, at most this many passes
// (Reach's kReachSightSelfPasses / kReachSightSelfStepWu).
inline constexpr int kLegacySightSelfPasses = 3;
inline constexpr float kLegacySightSelfStepWu = 0.05f * kReachWorldUnitsPerMeter;
// The parent link in an object's data: +0x10 in both titles (kHalo3ObjectParentOffset,
// kOdstObjectParentOffset; the seat samplers already walk it).
inline constexpr size_t kLegacyObjectParentOffset = 0x10;
using LegacyObjectDataFn = const uint8_t* (*)(uint32_t handle);
// An object's type from the title's object table (the header byte the engine's own collision predicate tests:
// 0 biped, 1 vehicle), or -1 when the table no longer holds the handle.
using LegacyObjectTypeFn = int (*)(uint32_t handle);

// The type of an object a trace hit, guarded (kept free of C++ objects so __try is legal). -1: unknown.
int LegacySightObjectType(LegacyObjectTypeFn objectType, int32_t object)
{
    if (object == -1 || !objectType)
        return -1;
    __try
    {
        return objectType(static_cast<uint32_t>(object));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The root of an object's parent chain (the vehicle a seated unit rides; on foot the unit itself), walked
// through the title's own object table: objectData maps a handle the table still holds to that object's
// data, or null. -1 when any link is stale or the chain is deeper than any vehicle. Simulation thread;
// guarded, and kept free of C++ objects so __try is legal.
int32_t LegacySightRootOf(LegacyObjectDataFn objectData, int32_t object)
{
    if (object == -1 || !objectData)
        return -1;
    __try
    {
        int32_t current = object;
        for (int depth = 0; depth < 8; ++depth)
        {
            const uint8_t* data = objectData(static_cast<uint32_t>(current));
            if (!data)
                return -1;
            int32_t parent = -1;
            memcpy(&parent, data + kLegacyObjectParentOffset, sizeof(parent));
            if (parent == -1)
                return current;
            current = parent;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return -1;
}

// Simulation thread, inside the title's per-unit contact update (counted by that hook's callbacks, whose
// teardown drains before the collision trampoline is freed). Traces the local unit's published intent ray
// at most every 30 ms: on foot, or in a seat whose vehicle the aim path named.
//
// SEATED, the ray starts at the rendered eye INSIDE the vehicle, so it must not stop on the vehicle itself (in a
// Banshee the intent ray would otherwise collide with the Banshee). It ignores the vehicle's ROOT in the first
// slot -- the slot both engines' own crosshair traces fill (Reach's engine query is (root, -1, -1); Halo 3's
// ignores the unit's root) -- then the occupant and the seat's own parent when that is not the root (a
// Warthog's turret is a vehicle of its own); and any other part of the same vehicle the ray still meets
// (another occupant, an attached gun) is looked past, as Reach's trace does. The root must be in the FIRST slot
// and the hit object must be read: with the vehicle only in the second slot the ray stops on it.
//
// WHAT THE ENGINE ALREADY DOES WITH THOSE IGNORES (static read of both modules; sight_hit_class_logic.h):
// collision_test_vector's per-object predicate (halo3 0x1FC430, odst 0x22E4F4) compares all three slots alike, then
// walks the hit object's parent chain and skips anything that descends from an ignored object -- EXCEPT a biped,
// vehicle or giant in a seat whose definition names a boarding seat. So the engine itself passes through our
// vehicle, its turret, its other occupants and every held weapon, and the one thing of our own hierarchy it still
// returns is a BOARDER. The look-past therefore keeps a biped that is not our own unit as the hit
// (sight_hit::LookPastOwnHierarchy): stepping past a hijacker hanging on our hood would converge the aim on the
// wall behind him. Inferred from the retail binaries; not confirmed in game (dev: SIGHT HITS counts what was
// looked past by class, so one boarding shows it).
void LegacySightTraceTick(GameTitle title, uint32_t unit, int32_t localUnit, bool onFoot,
                          const LegacyWorldCollisionFeature& collision, LegacyObjectDataFn objectData,
                          LegacyObjectTypeFn objectType)
{
    auto& state = LegacySight(title);
    const uint64_t flags = state.flags.load(std::memory_order_acquire);
    if (!flags || localUnit == -1 || unit != static_cast<uint32_t>(localUnit) ||
        TitleAdapter_GetActiveTitle() != title)
        return;
    // The local unit's tick (once per simulation tick): its period, smoothed over ~8 ticks; a stall (over 250 ms)
    // or a repeat inside one tick (under 2 ms) is no period.
    {
        static uint64_t s_lastUs[2]{};
        const int t = title == GameTitle::Halo3ODST ? 1 : 0;
        const uint64_t nowUs = Sight_NowUs();
        if (s_lastUs[t] && nowUs > s_lastUs[t] + 2000 && nowUs - s_lastUs[t] < 250000)
        {
            const uint32_t period = static_cast<uint32_t>(nowUs - s_lastUs[t]);
            const uint32_t prior = g_legacyTickPeriodUs[t].load(std::memory_order_relaxed);
            g_legacyTickPeriodUs[t].store(prior ? prior + (static_cast<int32_t>(period - prior) / 8) : period,
                                          std::memory_order_relaxed);
        }
        if (!s_lastUs[t] || nowUs > s_lastUs[t] + 2000 || nowUs < s_lastUs[t])
            s_lastUs[t] = nowUs;
        g_legacySimTicks[t].fetch_add(1, std::memory_order_release);
        Sight_NoteSimTick(title);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (title == GameTitle::Halo3)
            Halo3ImpactWitnessTick();   // dev SHOT IMPACT: re-read the tracked rounds once a tick
#endif
    }
    if (Sight_TraceFaulted(title) || !Sight_TraceDue(title, Sight_NowMs()))
        return;
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightRay ray{};
    if (!Sight_ReadRay(title, generation, kSightRayFreshMs, ray))
        return;
    // Seated is the unit's own parent link. unit_in_vehicle (the caller's onFoot) answers false in a
    // stationary turret, which then traced as on foot -- with nothing but the unit ignored, into the turret.
    // When the table cannot be walked, the caller's answer stands.
    int32_t root = LegacySightRootOf(objectData, static_cast<int32_t>(unit));
    const bool seated = root != -1 ? root != static_cast<int32_t>(unit) : !onFoot;
    if (seated && ray.ignoreB == -1)
    {
        // A seat the aim path did not name: no trace (the aim keeps its fixed-distance steer).
        state.seatUnnamed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto original = reinterpret_cast<LegacyCollisionTestVectorFn>(collision.original);
    if (!original || !collision.originalIsVectorTest || !collision.installed.load(std::memory_order_acquire))
        return;
    int32_t ignoreA = static_cast<int32_t>(unit), ignoreB = -1, ignoreC = -1;
    if (seated)
    {
        if (root == -1 || root == static_cast<int32_t>(unit))
        {
            // The walk failed: the published seat chain's top (the carrier when the seat's parent is
            // attached to one, else the parent).
            root = ray.ignoreC != -1 ? ray.ignoreC : ray.ignoreB;
            state.rootFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
        ignoreA = root;
        ignoreB = static_cast<int32_t>(unit);
        ignoreC = ray.ignoreB != root ? ray.ignoreB : -1;
        state.seatedTraces.fetch_add(1, std::memory_order_relaxed);
    }
    const float length = kSightTraceLengthWu;
    // A hit object that is part of our own vehicle: its root is our root, or -- when that walk fails, as it did for
    // the unit on the fallback path above -- it is one of the seat chain's own handles.
    // ...and not a boarder: a biped of our hierarchy the engine still returned (the note above) is a target.
    const auto isOurs = [&](int32_t object) {
        const int32_t hitRoot = LegacySightRootOf(objectData, object);
        const bool sameRoot = object != -1 && (hitRoot != -1 ? hitRoot == root
            : object == root || object == ignoreB || object == ignoreC || object == ray.ignoreB ||
              object == ray.ignoreC);
        if (!sameRoot)
            return false;
        const int type = LegacySightObjectType(objectType, object);
        const bool past = sight_hit::LookPastOwnHierarchy(true, object == static_cast<int32_t>(unit), type);
        if (past)
            Sight_NoteLookPast(title, Sight_ObjectHitClass(type));   // dev: what was stepped past, by class
        return past;
    };
    // Native collision initializes fields through +63 (type +0, fraction +4, point +8, object +0x40).
    alignas(16) uint8_t result[0x80]{};
    bool hit = false;
    float rangeWu = 0.0f;
    float alongWu = kSightTraceSkipWu;   // where this pass starts, measured from the eye
    int32_t hitObject = -1;              // the object the published hit is on, or -1 (structure, or a miss)
    LARGE_INTEGER before{}, after{};
    QueryPerformanceCounter(&before);
    for (int pass = 0; pass < (seated ? kLegacySightSelfPasses : 1); ++pass)
    {
        const float remainingWu = length - (alongWu - kSightTraceSkipWu);
        if (!(remainingWu > 0.0f))
            break;
        const float start[3]{ray.head[0] + ray.dir[0] * alongWu, ray.head[1] + ray.dir[1] * alongWu,
                             ray.head[2] + ray.dir[2] * alongWu};
        const float vector[3]{ray.dir[0] * remainingWu, ray.dir[1] * remainingWu, ray.dir[2] * remainingWu};
        memset(result, 0, sizeof(result));
        uint8_t returned = 0;
        const bool completed = ReachSightGuardedTrace(original, flags, kReachAimTraceMode, start, vector,
                                                      ignoreA, ignoreB, ignoreC, result, returned, false);
        if (!completed)
        {
            Sight_NoteTraceFault(title);
            LOG("SIGHT %s: the aim trace raised a native fault; it stays off until the title reinstalls",
                LegacySightTitleName(title));
            return;
        }
        float fraction = 0.0f;
        memcpy(&fraction, result + 4, sizeof(fraction));
        hit = returned && std::isfinite(fraction) && fraction >= 0.0f && fraction <= 1.0f;
        if (!hit)
            break;
        rangeWu = alongWu + fraction * remainingWu;
        // The result's type (+0) is 4 for an object (both modules' per-object test writes it), whose index is at +0x40.
        int32_t resultType = 0, object = -1;
        memcpy(&resultType, result, sizeof(resultType));
        memcpy(&object, result + 0x40, sizeof(object));
        hitObject = resultType == 4 ? object : -1;
        if (!seated)
            break;
        if (!isOurs(object))
            break;
        // A part of our own vehicle the ignores did not cover: not a target. Look past it.
        state.selfHits.fetch_add(1, std::memory_order_relaxed);
        hit = false;
        rangeWu = 0.0f;
        hitObject = -1;
        alongWu += fraction * remainingWu + kLegacySightSelfStepWu;
    }
    QueryPerformanceCounter(&after);
    Sight_PublishTrace(title, generation, hit, hit ? rangeWu : 0.0f,
                       static_cast<uint64_t>(after.QuadPart - before.QuadPart));
#if HALOMCCVR_DEV_DIAGNOSTICS
    // SIGHT HITS (dev): what the ray stopped on, by class -- after the cost was taken, so the trace's time is its own.
    {
        const int type = hit && hitObject != -1 ? LegacySightObjectType(objectType, hitObject) : -1;
        Sight_NoteTraceHit(title, sight_hit::ClassifyHit(hit, hitObject != -1, type), type, hit ? rangeWu : 0.0f,
                           seated);
    }
#endif
    // THE SEAT LIMIT RAY (seat_limit_reticle_logic.h): while a seat's reticle is clamped at a natural stop, the ray
    // its gun really aims along -- from the seat's shot origin along the limit-clamped direction -- in this same tick,
    // with these same ignores. Only while the aim path published one; nothing the aim reads moves.
    SightAuxRay limit{};
    if (Sight_ReadLimitRay(title, generation, kSightRayFreshMs, limit))
    {
        bool limitHit = false;
        float limitRangeWu = 0.0f;
        if (!SightAuxTraceVector(original, flags, kReachAimTraceMode, limit, length, kSightTraceSkipWu,
                                 kLegacySightSelfStepWu, seated ? kLegacySightSelfPasses : 1, ignoreA, ignoreB,
                                 ignoreC, isOurs, limitHit, limitRangeWu))
        {
            Sight_NoteTraceFault(title);
            LOG("SIGHT %s: the seat limit trace raised a native fault; the trace stays off until the title "
                "reinstalls", LegacySightTitleName(title));
            return;
        }
        Sight_PublishLimitTrace(title, generation, limitHit, limitHit ? limitRangeWu : 0.0f);
    }
    // THE VEHICLE CAMERA RAY (vehicle_cameras.inl): from the point a chase camera is measured from toward where it
    // sits, while one with "collide" is up. The same query, flags and ignores as the intent ray, so the player's own
    // vehicle and its parts are looked past. Only while the camera thread published one.
    SightAuxRay cameraRay{};
    if (Sight_ReadCameraRay(title, generation, kSightRayFreshMs, cameraRay))
    {
        bool cameraHit = false;
        float cameraRangeWu = 0.0f;
        if (!SightAuxTraceVector(original, flags, kReachAimTraceMode, cameraRay, length, kSightTraceSkipWu,
                                 kLegacySightSelfStepWu, seated ? kLegacySightSelfPasses : 1, ignoreA, ignoreB,
                                 ignoreC, isOurs, cameraHit, cameraRangeWu))
        {
            Sight_NoteTraceFault(title);
            LOG("SIGHT %s: the vehicle camera trace raised a native fault; the trace stays off until the title "
                "reinstalls", LegacySightTitleName(title));
            return;
        }
        Sight_PublishCameraTrace(title, generation, cameraHit, cameraHit ? cameraRangeWu : 0.0f);
    }
    // THE VEHICLE AIM RING RAY (vehicle_cameras.inl): from the eye the view is drawn from along the way the vehicle
    // aims, while the ring is shown, for the depth it is drawn at. The same query, flags and ignores, so the player's
    // own vehicle is looked past. Only while the camera thread published one.
    SightAuxRay markerRay{};
    if (Sight_ReadMarkerRay(title, generation, kSightRayFreshMs, markerRay))
    {
        bool markerHit = false;
        float markerRangeWu = 0.0f;
        if (!SightAuxTraceVector(original, flags, kReachAimTraceMode, markerRay, length, kSightTraceSkipWu,
                                 kLegacySightSelfStepWu, seated ? kLegacySightSelfPasses : 1, ignoreA, ignoreB,
                                 ignoreC, isOurs, markerHit, markerRangeWu))
        {
            Sight_NoteTraceFault(title);
            LOG("SIGHT %s: the vehicle aim ring trace raised a native fault; the trace stays off until the title "
                "reinstalls", LegacySightTitleName(title));
            return;
        }
        Sight_PublishMarkerTrace(title, generation, markerHit, markerHit ? markerRangeWu : 0.0f);
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    // DEV ASSESSMENT (honest_reticle_assess): the game's own aim ray from its shot origin (the engine eye, or the
    // rendered eye for an ODST passenger whose shot O6 moves there), in this same tick, with these same ignores.
    // Only while the key published one; nothing the aim reads moves. A native fault here latches only the
    // ASSESSMENT trace off (Sight_NoteAssessTraceFault); the intent trace's latch is the aim's.
    SightAssessRay assess{};
    if (!Sight_AssessTraceFaulted(title) && Sight_ReadAssessRay(title, generation, kSightRayFreshMs, assess))
    {
        bool assessHit = false;
        float assessRangeWu = 0.0f;
        if (!SightAuxTraceVector(original, flags, kReachAimTraceMode, assess, length, kSightTraceSkipWu,
                                 kLegacySightSelfStepWu, seated ? kLegacySightSelfPasses : 1, ignoreA, ignoreB,
                                 ignoreC, isOurs, assessHit, assessRangeWu))
        {
            Sight_NoteAssessTraceFault(title);
            return;
        }
        Sight_PublishAssessTrace(title, generation, assessHit, assessHit ? assessRangeWu : 0.0f);
    }
    // [assess-only end]
#endif
}

// ---- engine-crosshair check (dev): does the TITLE'S OWN crosshair trace pass through the traced point? ----
// Halo 3 and ODST trace their crosshair with the resolved flags. Our own trace calls the ORIGINAL through
// the trampoline, so every query the collision hook sees with those flags is the game's. While our aim is
// converged each one is compared with the point the intent ray hit (H + u * range): how far that point sits
// off the game's line, where the line starts relative to the engine eye we solved with, and its angle from
// the eye -> point direction. Pass = the point ~0 mm off the line.
#if HALOMCCVR_DEV_DIAGNOSTICS
struct LegacyEngineTraceWindow
{
    std::atomic<uint32_t> siteCalls{0};
    std::atomic<uint64_t> siteFlags{0};
    std::atomic<uint32_t> seen{0}, compared{0};
    // Converged queries skipped because a natural cause (a seat's latch, a turret's slew, a stall) held the game's
    // aim off the point: the converged mark stays on through those, so they are not misses of the solve.
    std::atomic<uint32_t> heldOff{0};
    std::atomic<float> missMm{0.0f}, maxMissMm{0.0f}, rangeM{0.0f}, angleDeg{0.0f};
    std::atomic<float> startAlongMm{0.0f}, startRightMm{0.0f}, startUpMm{0.0f};
};
LegacyEngineTraceWindow g_legacyEngineTrace[2];   // [0] Halo 3, [1] ODST

// ---- the turret's own shots (dev, Halo 3): does the GUN's shot line pass through the traced point? ----------------
// In a gun-slaved seat (flag bit 7) the camera is the gun's marker, so the solve's eye is that marker -- but the shot
// leaves the BARREL, along the gun's own aim. The firing call's final (origin, direction) is compared with the point
// the intent ray hit: the miss off the shot's line, the range, the shot's start relative to the solve's eye (a large
// offset means a parallel miss no aim angle can fix -- the case for a muzzle-origin solve), and the shot's angle off
// the eye -> point direction. Shots while the gun is still slewing onto the writes are counted apart: those miss by
// design. The input thread publishes the seat (the turret vehicle that fires) every gun-slaved poll.
struct LegacyTurretShotWindow
{
    std::atomic<uint32_t> shots{0}, settled{0}, noPoint{0};
    std::atomic<uint32_t> maxMissSettledBits{0}, maxMissSlewingBits{0};
    std::atomic<float> missMm{0.0f}, rangeM{0.0f}, angleDeg{0.0f};
    std::atomic<float> startAlongMm{0.0f}, startRightMm{0.0f}, startUpMm{0.0f};
};
LegacyTurretShotWindow g_legacyTurretShots;
std::atomic<int32_t> g_legacyTurretShotParent{-1};
std::atomic<uint64_t> g_legacyTurretShotSeatMs{0};
std::atomic<bool> g_legacyTurretShotGunSettled{false};
#endif

// Input thread, every poll of a Halo 3 gun-slaved seat: the turret vehicle (it fires the gun) and whether the gun has
// settled on the writes.
void LegacySightPublishTurretSeat(int32_t parentHandle, bool gunSettled)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    g_legacyTurretShotParent.store(parentHandle, std::memory_order_relaxed);
    g_legacyTurretShotGunSettled.store(gunSettled, std::memory_order_relaxed);
    g_legacyTurretShotSeatMs.store(GetTickCount64(), std::memory_order_release);
#else
    (void)parentHandle; (void)gunSettled;
#endif
}

// The firing call (Halo 3's native weapon fire, after its final ray): one shot of the gun `shooter` holds -- the firing
// weapon's owner (+0x168). Not the unit the shot-line call receives: that is the OCCUPANT (the firing routine walks the
// gunner chain +0x2A4 down to it, 0x368B82 `mov ecx, esi`), so comparing that unit with the turret matches no shot at
// all and the line never logs.
void LegacySightNoteTurretShot(GameTitle title, uint32_t shooter, const float* origin, const float* direction)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (title != GameTitle::Halo3 || !origin || !direction)
        return;
    const uint64_t at = g_legacyTurretShotSeatMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    const int32_t parent = g_legacyTurretShotParent.load(std::memory_order_relaxed);
    if (!at || now < at || now - at > 250 || parent == -1 || static_cast<int32_t>(shooter) != parent)
        return;
    auto& w = g_legacyTurretShots;
    w.shots.fetch_add(1, std::memory_order_relaxed);
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightRay ray{};
    SightTrace trace{};
    if (!Sight_ReadRay(title, generation, kSightRayFreshMs, ray) ||
        !Sight_ReadTrace(title, generation, kSightFreshMs, trace))
    {
        w.noPoint.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float dl = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (!std::isfinite(dl) || dl < 1.0e-6f)
        return;
    const float d[3]{direction[0] / dl, direction[1] / dl, direction[2] / dl};
    const float range = trace.AimRangeWu();
    const float p[3]{ray.head[0] + ray.dir[0] * range, ray.head[1] + ray.dir[1] * range,
                     ray.head[2] + ray.dir[2] * range};
    const float v[3]{p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
    const float c[3]{v[1] * d[2] - v[2] * d[1], v[2] * d[0] - v[0] * d[2], v[0] * d[1] - v[1] * d[0]};
    const float toMm = 3048.0f;   // 1 world unit = 3.048 m
    const float miss = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) * toMm;
    const bool settled = g_legacyTurretShotGunSettled.load(std::memory_order_relaxed);
    if (settled)
        w.settled.fetch_add(1, std::memory_order_relaxed);
    aim_caller::AtomicMaxFloat(settled ? w.maxMissSettledBits : w.maxMissSlewingBits, miss);
    w.missMm.store(miss, std::memory_order_relaxed);
    w.rangeM.store(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * 3.048f, std::memory_order_relaxed);
    const float a[3]{p[0] - ray.eye[0], p[1] - ray.eye[1], p[2] - ray.eye[2]};
    const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float dot = al > 1.0e-6f ? (a[0] * d[0] + a[1] * d[1] + a[2] * d[2]) / al : 1.0f;
    dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
    w.angleDeg.store(std::acos(dot) * 57.29578f, std::memory_order_relaxed);
    float right[3]{d[1], -d[0], 0.0f};
    float rl = std::sqrt(right[0] * right[0] + right[1] * right[1]);
    if (!(rl > 1.0e-4f)) { right[0] = 1.0f; right[1] = 0.0f; rl = 1.0f; }
    right[0] /= rl; right[1] /= rl;
    const float up[3]{right[1] * d[2] - right[2] * d[1], right[2] * d[0] - right[0] * d[2],
                      right[0] * d[1] - right[1] * d[0]};
    const float se[3]{origin[0] - ray.eye[0], origin[1] - ray.eye[1], origin[2] - ray.eye[2]};
    w.startAlongMm.store((se[0] * d[0] + se[1] * d[1] + se[2] * d[2]) * toMm, std::memory_order_relaxed);
    w.startRightMm.store((se[0] * right[0] + se[1] * right[1] + se[2] * right[2]) * toMm, std::memory_order_relaxed);
    w.startUpMm.store((se[0] * up[0] + se[1] * up[1] + se[2] * up[2]) * toMm, std::memory_order_relaxed);
#else
    (void)title; (void)shooter; (void)origin; (void)direction;
#endif
}

void LegacySightNoteEngineTrace(GameTitle title, uint64_t flags, const float* start, const float* vector,
                                uintptr_t caller)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Cheapest gate first: this runs for every collision query the game makes.
    if (title != GameTitle::Halo3 && title != GameTitle::Halo3ODST)
        return;
    auto& state = LegacySight(title);
    auto& window = g_legacyEngineTrace[title == GameTitle::Halo3ODST ? 1 : 0];
    for (const auto& site : state.siteReturn)
    {
        const uintptr_t at = site.load(std::memory_order_relaxed);
        if (at && at == caller)
        {
            window.siteCalls.fetch_add(1, std::memory_order_relaxed);
            window.siteFlags.store(flags, std::memory_order_relaxed);
            break;
        }
    }
    const uint64_t resolved = state.flags.load(std::memory_order_acquire);
    if (!resolved || flags != resolved || !start || !vector)
        return;
    window.seen.fetch_add(1, std::memory_order_relaxed);
    if (TitleAdapter_GetActiveTitle() != title || !Sight_ConvergedLive(title))
        return;
    if (ConvergeNaturalHeld(title))
    {
        window.heldOff.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightRay ray{};
    SightTrace trace{};
    if (!Sight_ReadRay(title, generation, kSightRayFreshMs, ray) ||
        !Sight_ReadTrace(title, generation, kSightFreshMs, trace))
        return;
    const float vl = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    if (!std::isfinite(vl) || vl < 1.0e-6f)
        return;
    const float d[3]{vector[0] / vl, vector[1] / vl, vector[2] / vl};
    const float aimRange = trace.AimRangeWu();
    const float p[3]{ray.head[0] + ray.dir[0] * aimRange, ray.head[1] + ray.dir[1] * aimRange,
                     ray.head[2] + ray.dir[2] * aimRange};
    const float w[3]{p[0] - start[0], p[1] - start[1], p[2] - start[2]};
    const float c[3]{w[1] * d[2] - w[2] * d[1], w[2] * d[0] - w[0] * d[2], w[0] * d[1] - w[1] * d[0]};
    const float toMm = 3048.0f;   // 1 world unit = 3.048 m
    // The eye -> point direction the solve aimed along, against the line the game traced.
    const float a[3]{p[0] - ray.eye[0], p[1] - ray.eye[1], p[2] - ray.eye[2]};
    const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float dot = al > 1.0e-6f ? (a[0] * d[0] + a[1] * d[1] + a[2] * d[2]) / al : 1.0f;
    dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
    // The line's start relative to the solve's eye, in the line's frame (along, horizontal right, up).
    float right[3]{d[1], -d[0], 0.0f};
    float rl = std::sqrt(right[0] * right[0] + right[1] * right[1]);
    if (!(rl > 1.0e-4f)) { right[0] = 1.0f; right[1] = 0.0f; rl = 1.0f; }
    right[0] /= rl; right[1] /= rl;
    const float up[3]{right[1] * d[2] - right[2] * d[1], right[2] * d[0] - right[0] * d[2],
                      right[0] * d[1] - right[1] * d[0]};
    const float se[3]{start[0] - ray.eye[0], start[1] - ray.eye[1], start[2] - ray.eye[2]};
    const float miss = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) * toMm;
    window.missMm.store(miss, std::memory_order_relaxed);
    if (miss > window.maxMissMm.load(std::memory_order_relaxed))
        window.maxMissMm.store(miss, std::memory_order_relaxed);
    window.rangeM.store(std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) * 3.048f, std::memory_order_relaxed);
    window.angleDeg.store(std::acos(dot) * 57.29578f, std::memory_order_relaxed);
    window.startAlongMm.store((se[0] * d[0] + se[1] * d[1] + se[2] * d[2]) * toMm, std::memory_order_relaxed);
    window.startRightMm.store((se[0] * right[0] + se[1] * right[1] + se[2] * right[2]) * toMm,
                              std::memory_order_relaxed);
    window.startUpMm.store((se[0] * up[0] + se[1] * up[1] + se[2] * up[2]) * toMm, std::memory_order_relaxed);
    window.compared.fetch_add(1, std::memory_order_release);
#else
    (void)title; (void)flags; (void)start; (void)vector; (void)caller;
#endif
}

// ---- THE SHOT, RE-AIMED THROUGH THE INTENT POINT (every build; shot_lag_logic.h) ------------------------------
// Halo 3 and ODST decide a shot's line at its fire tick: the weapon's fire (Halo 3 0x3683A0) calls the firing
// assist (0x13BAD0, from 0x368DFE), whose crosshair query -- the resolved crosshair site, siteReturn above -- traces
// from the shooter's camera along the record's aim, and the shot is turned at whatever that query hits. The record's
// angles were solved a frame or more earlier from a render-time eye, so from the tick's camera they trace a line
// parallel to the one meant, moved by the eye's travel (in multiplayer, also by however far the rendered camera
// leads or trails the simulation). Re-aiming THAT QUERY from its own start through the intent point puts the hit --
// and so the shot -- on the point the player meant, whatever the mode, from whatever origin the game uses:
//
//      vector' = normalize(P - start) * |vector|
//
// On foot P is the frame-coherent intent point; in a seat it is the point the solve bent the record through
// (SightIntent::solvePoint), so a seat's line is only ever corrected for the eye's travel, never re-targeted -- except
// in a gun-slaved turret, whose query traces the GUN's camera, which sits off the record and moves at the fire (the
// gauss turret; converge::SeatAssistReaimRefuses): there the re-aim carries its shot onto the reticle's point. A
// query already within 10 mm of P is left exactly as the game built it (shot_lag::Reaim::AlreadyThrough): Halo 3's
// campaign vehicles are coherent without it and stay byte-identical. Left alone (fail closed) when
// the point is stale (over 150 ms) or for another generation, the converged aim is not in effect, the solve held at
// the pitch rail, a seat is held at its stop (a turret is NOT refused for being one: the assist's own
// seat cap bounds a shot fired while the gun catches up), the fire or the
// query is not the local player's (shot_lag::FireMark, QueryIsLocal: by identity, never by distance), another gun's
// own line owns the fire (an off-hand gun, a barrel or muzzle shot: LegacyOwnShotLineActive), or no forward line
// through P exists (behind the start, inside 30 cm). No magnitude limit: shot_lag::ReaimRails. Every refusal is
// counted by reason on the SHOT REAIM line.
// Collision-hook thread (the game's own query); lock-free reads only. The thread-local below carries the native
// fire's `predicted` flag to the dev witness (Halo 3's fire detour sets it).
thread_local int t_legacyFirePredicted = -1;
bool LegacyDriveRecordSnapshot(GameTitle title, float& nativeYaw, float& nativePitch, float& setYaw, float& setPitch);

#if HALOMCCVR_DEV_DIAGNOSTICS
// THE HORNET CAPTURE (dev, Halo 3 seats): for the 2-5 deg the crosshair line can sit off the solve in a flying seat.
// Each seated crosshair query, before the re-aim: its start and direction, the point, the driver record and the
// setpoint, the hull's velocity (the eye's) and tilt. A ring; the cold worker prints what is new every 5 s.
struct LegacySeatQueryRecord
{
    float start[3]{}, dir[3]{}, point[3]{}, solveEye[3]{}, vel[3]{};
    float recordYaw = 0.0f, recordPitch = 0.0f, setYaw = 0.0f, setPitch = 0.0f, hullYaw = 0.0f, hullPitch = 0.0f;
    float missMm = 0.0f, bendDeg = 0.0f;
    uint8_t recordValid = 0, hullValid = 0, velValid = 0, reaimed = 0;
    int32_t ignoredA = -1, vehicle = -1;
    uint64_t atUs = 0;
};
inline constexpr uint32_t kLegacySeatQueryRing = 16;
struct LegacySeatQueryRing
{
    SRWLOCK lock = SRWLOCK_INIT;
    LegacySeatQueryRecord records[kLegacySeatQueryRing]{};
    uint32_t written = 0;
    uint32_t printed = 0;   // cold worker only
} g_legacySeatQueries;
#endif

#if HALOMCCVR_DEV_DIAGNOSTICS
// THE MULTIPLAYER RESIDUAL, PER SEAT (dev; seat_upstream_logic.h): every local seated crosshair query, as the game
// built it, against the seat's P -- exactly what a host firing from our record alone would trace -- and what the
// fire-site re-aim then still had to do. Windows by seat (vehicle, seat index, mounted, gun-slaved, and whether the
// seat's STOCK tag is third person: an unmodded host would then query from its chase camera, which this cannot
// measure). The cold worker prints them every 5 s (LegacySightReport).
inline constexpr uint32_t kSeatTagMounted = 1u, kSeatTagGunSlaved = 2u, kSeatTagStockChase = 4u;
struct LegacyStockLineWindows
{
    SRWLOCK lock = SRWLOCK_INIT;
    seat_upstream::StockLineWindow windows[4]{};
} g_legacyStockLines;

uint32_t LegacyHalo3SeatTag()
{
    Halo3SeatSnapshot seat;
    if (!Halo3ReadSeatSnapshot(seat) || seat.parentHandle == -1)
        return 0;
    const uint32_t flags = Halo3OccupiedSeatFlags();
    return (seat.identity & 0xFFu) << 16 | (static_cast<uint32_t>(seat.seatIndex + 1) & 0xFFu) << 8 |
           (seat.mounted ? kSeatTagMounted : 0u) | (gun_slaved::CameraSlavedToGun(flags) ? kSeatTagGunSlaved : 0u) |
           ((flags >> 4) & 1u ? kSeatTagStockChase : 0u) | 0x80000000u;
}

// The evaluator's chase path, asked (halo3_independent_shots.inl, defined after this file).
bool Halo3AskChaseCamera(int32_t unit, float position[3], float direction[3]);

// `point`: the seat's P. For a stock third-person seat the chase line an unmodded host would trace is measured too.
void LegacySightNoteStockLine(const seat_upstream::StockLineSample& sample, bool reaimed, const float point[3])
{
    const uint32_t tag = LegacyHalo3SeatTag();
    if (!tag)
        return;
    seat_upstream::StockLineSample host{};
    bool unreproducible = false;
    if (tag & kSeatTagStockChase)
    {
        const auto stock = seat_upstream::StockCameraFor(Halo3OccupiedSeatFlags());
        float position[3]{}, direction[3]{};
        if (stock == seat_upstream::StockCamera::NotReproducible)
            unreproducible = true;
        else if (stock == seat_upstream::StockCamera::Chase &&
                 Halo3AskChaseCamera(LegacyCollisionIgnoredObject(GameTitle::Halo3), position, direction))
            host = seat_upstream::MeasureStockLine(position, direction, point, nullptr);
    }
    auto& w = g_legacyStockLines;
    AcquireSRWLockExclusive(&w.lock);
    seat_upstream::StockLineWindow* slot = nullptr;
    for (auto& window : w.windows)
        if (window.tag == tag)
            slot = &window;
    if (!slot)
        for (auto& window : w.windows)
            if (!window.tag)
            {
                slot = &window;
                break;
            }
    if (!slot)
    {
        slot = &w.windows[0];   // more seats than slots in one window: the oldest slot starts over
        *slot = seat_upstream::StockLineWindow{};
    }
    slot->tag = tag;
    slot->Add(sample);
    slot->AddHost(host);
    if (unreproducible)
        ++slot->hostUnreproducible;
    if (reaimed)
        ++slot->reaimed;
    else if (sample.missMm < shot_lag::ReaimRails{}.minOffsetWu * shot_lag::kMmPerWu)
        ++slot->through;
    ReleaseSRWLockExclusive(&w.lock);
}

void LegacySightReportStockLines()
{
    seat_upstream::StockLineWindow copy[4]{};
    auto& w = g_legacyStockLines;
    AcquireSRWLockExclusive(&w.lock);
    for (int i = 0; i < 4; ++i)
    {
        copy[i] = w.windows[i];
        w.windows[i] = seat_upstream::StockLineWindow{};
    }
    ReleaseSRWLockExclusive(&w.lock);
    for (const auto& window : copy)
    {
        if (!window.n)
            continue;
        const uint32_t id = (window.tag >> 16) & 0xFFu;
        const int seatIndex = static_cast<int>((window.tag >> 8) & 0xFFu) - 1;
        const bool mounted = (window.tag & kSeatTagMounted) != 0;
        char name[160]{};
        std::snprintf(name, sizeof(name), "%s %d%s (%s%s)", Halo3VehicleIdName(id), seatIndex,
                      mounted ? " mounted" : "",
                      Halo3SeatIsDriver(static_cast<Halo3VehicleId>(id), seatIndex, mounted) ? "driver"
                      : (window.tag & kSeatTagGunSlaved) ? "gun-slaved gunner"
                                                         : "passenger/gunner",
                      (window.tag & kSeatTagStockChase)
                          ? "; stock chase camera: an UNMODDED host queries this seat from its chase camera, not measured"
                          : "");
        char line[1024]{};
        if (window.Format("Halo 3", name, line, sizeof(line)))
            LOG("%s", line);
    }
}
#endif

// True when this query belongs to the local player (shot_lag::QueryIsLocal, by identity): on foot its first ignore
// is the unit itself; seated the vehicle's root or the seat's parent. Another occupant (an AI gunner, a co-op player
// in the next seat) names those too; the fire tag (LegacyFireTag) is what tells his fire from ours.
bool LegacySightQueryIsLocal(GameTitle title, int32_t ignoredA, const SightRay& ray, bool seated)
{
    return shot_lag::QueryIsLocal(seated, ignoredA, seated ? -1 : LegacyCollisionIgnoredObject(title), ray.ignoreB,
                                  ray.ignoreC);
}

// The fire thread's tag for this title's native fire (shot_lag::FireMark; game.cpp t_halo3Fire / t_odstFire).
int8_t LegacyFireTag(GameTitle title)
{
    return title == GameTitle::Halo3 ? t_halo3Fire : title == GameTitle::Halo3ODST ? t_odstFire : shot_lag::kFireNone;
}

// THE STOCK LINE'S START, AS THE GAME BUILT IT (Halo 3 seats; seat_upstream_logic.h 4b). With the seat's tag as
// loaded the game queries a third-person seat's crosshair from its chase camera -- on this machine, on a lockstep
// peer and on an unmodded host alike. That query's START for the local player's seat is published here from the
// crosshair site, with its direction, and read at the stick call (LegacyDriveRecomposeSeated). It is a point on the
// camera's line and not the camera (the query starts at the shot origin's projection on that line), and the game
// runs the query when the gun fires. So it is the last of the stick call's sources, and the line its asked chase
// camera is checked against. It needs no hook on the unit camera evaluator.
struct LegacySeatStockLine
{
    SRWLOCK lock = SRWLOCK_INIT;
    float start[3]{}, dir[3]{};
    uint64_t atUs = 0;
    uint32_t generation = 0;
};
LegacySeatStockLine g_legacySeatStockLine;

// `vector`: the query's own (any length; only its direction is kept).
void LegacySeatStockLinePublish(uint32_t generation, const float start[3], const float vector[3])
{
    float dir[3]{vector[0], vector[1], vector[2]};
    if (!generation || !shot_lag::Finite3(start) || !shot_lag::Finite3(dir) || !seat_upstream::Normalize(dir))
        return;
    auto& line = g_legacySeatStockLine;
    AcquireSRWLockExclusive(&line.lock);
    memcpy(line.start, start, sizeof(line.start));
    memcpy(line.dir, dir, sizeof(line.dir));
    line.atUs = Sight_NowUs();
    line.generation = generation;
    ReleaseSRWLockExclusive(&line.lock);
}
bool LegacySeatStockLineRead(uint32_t generation, uint64_t maxAgeUs, float start[3], float dir[3], uint64_t& atUs)
{
    auto& line = g_legacySeatStockLine;
    AcquireSRWLockShared(&line.lock);
    const bool fresh = generation && line.generation == generation && line.atUs;
    const uint64_t at = line.atUs;
    const float startCopy[3]{line.start[0], line.start[1], line.start[2]};
    const float dirCopy[3]{line.dir[0], line.dir[1], line.dir[2]};
    ReleaseSRWLockShared(&line.lock);
    const uint64_t now = Sight_NowUs();
    if (!fresh || now < at || now - at > maxAgeUs)
        return false;
    memcpy(start, startCopy, sizeof(startCopy));
    memcpy(dir, dirCopy, sizeof(dirCopy));
    atUs = at;
    return true;
}

// Returns true when `out` holds the re-aimed vector the native query must trace instead.
// THE SEAT'S SHOT LINE (every build, Halo 3): a shot goes where its crosshair query STOPS. In a seat the query starts
// at the game's own camera -- for a third-person seat, behind and above the vehicle -- and the record is solved so its
// line passes through the point the player aims at (LegacyDriveRecomposeSeated). Anything on that line short of the
// point -- a ridge, a ledge, an overhang the eye's own ray does not cross -- stops the query there, and the shot goes
// to it while the reticle stays on the point. Each local seated query is captured here (this thread), the game's trace
// result is read right after it (LegacySightNoteCrosshairResult), and the cold worker reports how many stopped short,
// by how much, and where that stop stood from the eye. Lock-free; a few multiplies per query.
struct LegacySeatShotLineCapture
{
    bool valid = false;
    float start[3]{}, point[3]{}, eye[3]{};
    int32_t own[3]{-1, -1, -1};   // the unit and its seat chain (the vehicle, a carrier)
};
thread_local LegacySeatShotLineCapture t_legacySeatShotLine;
struct LegacySeatShotLineWindow
{
    std::atomic<uint32_t> queries{0}, blocked{0}, byObject{0}, byOwn{0}, byYou{0}, above{0}, below{0}, passedAside{0};
    std::atomic<uint32_t> maxShortCm{0}, maxAngleCdeg{0}, lastStopCm{0}, lastPointCm{0};
    // Where the lines started, from the eye; and the last object one stopped on (its definition, for its tag name,
    // and its object type), 0xFFFFFFFF none.
    std::atomic<uint32_t> maxStartCm{0}, lastStartCm{0};
    std::atomic<uint32_t> lastObjectDef{0xFFFFFFFFu}, lastObjectType{0xFFFFFFFFu};
};
// halo3_contact_melee_runtime.inl (included after this file): the game thread's object table.
const uint8_t* Halo3SightObjectData(uint32_t handle);
int Halo3SightObjectType(uint32_t handle);
LegacySeatShotLineWindow g_legacySeatShotLine;
inline constexpr float kLegacySeatShotShortWu = 1.0f / 3.048f;   // a stop over 1 m short of the point

// A HALO 3 SEAT'S LINE FROM THE EYE (seat_fire_from_eye, as Halo 2's halo2_seat_fire.inl; only where fire-time changes
// are allowed -- never in a shared co-op simulation, Sight_FireTimeChangesAllowed). A stock seat's crosshair query
// starts at the game's OWN camera, which for a third-person-tagged seat (the Sparrowhawk's, most mod vehicles') stands
// metres behind and above the vehicle while the player looks from the seat; anything between that camera and the
// vehicle -- a parked Longsword, a hangar beam -- stops the line there, and the game turns the shot at that stop,
// behind or above the player (the player's own round then hits him). This query instead starts at the rendered eye
// the seat's point was traced from and runs through that point, so it sees what the player sees. A start already
// within kLegacySeatFromEyeKeepWu of the eye (a first-person seat) is left as the game built it.
struct LegacySeatFromEye
{
    bool valid = false;
    float start[3]{}, head[3]{}, point[3]{};
};
thread_local LegacySeatFromEye t_legacySeatFromEye;
std::atomic<uint32_t> g_legacySeatFromEyeLines{0};
inline constexpr float kLegacySeatFromEyeKeepWu = 0.5f / 3.048f;
inline constexpr float kLegacySeatShotAsideWu = 1.0f / 3.048f;   // a line passing over 1 m from the point

bool LegacySightReaimCrosshairQuery(GameTitle title, uint64_t flags, const float* start, const float* vector,
                                    int32_t ignoredA, uintptr_t caller, float out[3])
{
    t_legacySeatShotLine.valid = false;
    t_legacySeatFromEye.valid = false;
    if ((title != GameTitle::Halo3 && title != GameTitle::Halo3ODST) || !start || !vector)
        return false;
    auto& state = LegacySight(title);
    bool site = false;
    for (const auto& known : state.siteReturn)
    {
        const uintptr_t at = known.load(std::memory_order_relaxed);
        if (at && at == caller)
        {
            site = true;
            break;
        }
    }
    const uint64_t resolved = state.flags.load(std::memory_order_acquire);
    if (!site || !resolved || flags != resolved || TitleAdapter_GetActiveTitle() != title)
        return false;
    // A natural hold, in every build: a seat held at its stop points the gun elsewhere by design. A turret is not one
    // (converge::SeatAssistReaimRefuses): the assist's own seat cap bounds what the re-aim can bend.
    const bool naturalHeld = Game_SeatAssistReaimRefusal(title) != converge::SeatMuzzleRefusal::None;
    SightIntent intent{};
    const shot_lag::IntentUse use = Sight_IntentForShot(title, kSightIntentShotMaxAgeMs, naturalHeld, intent);
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightRay ray{};
    const bool rayRead = Sight_ReadRay(title, generation, 250, ray);
    const bool seated = intent.p.seated;
    const float* point = seated ? intent.solvePoint : intent.p.point;
    // Another unit's fire or query (an ally, an enemy, a co-op player in the next seat), or no ray to tell: never
    // ours.
    const bool localQuery = rayRead && LegacySightQueryIsLocal(title, ignoredA, ray, seated);
    // A Halo 3 seat whose simulation is the stock one (the tag as loaded): publish where the game's own line starts.
    // Another occupant of the same vehicle names the same objects, so a fire known to be someone else's is left
    // out; where no fire is tagged (the fire routine is not hooked) the ignore list alone decides.
    const bool stockSeat = title == GameTitle::Halo3 && seated && !Halo3SeatViewWritesTag();
    if (stockSeat && localQuery && LegacyFireTag(title) != shot_lag::kFireOther)
        LegacySeatStockLinePublish(generation, start, vector);
    if (title == GameTitle::Halo3 && seated && localQuery && use == shot_lag::IntentUse::Use &&
        LegacyFireTag(title) != shot_lag::kFireOther && point)
    {
        LegacySeatShotLineCapture& c = t_legacySeatShotLine;
        memcpy(c.start, start, sizeof(c.start));
        memcpy(c.point, point, sizeof(c.point));
        memcpy(c.eye, ray.head, sizeof(c.eye));
        c.own[0] = ray.unit;
        c.own[1] = ray.ignoreB;
        c.own[2] = ray.ignoreC;
        c.valid = true;
        // The stock seat's line may start at the eye instead (LegacySightSeatLineFromEye, called next by the
        // dispatch with the query as it then stands).
        if (stockSeat && g_config.seat_fire_from_eye && Sight_FireTimeChangesAllowed() &&
            shot_lag::Finite3(intent.p.head) && shot_lag::Finite3(point))
        {
            LegacySeatFromEye& f = t_legacySeatFromEye;
            memcpy(f.start, start, sizeof(f.start));
            memcpy(f.head, intent.p.head, sizeof(f.head));
            memcpy(f.point, point, sizeof(f.point));
            f.valid = true;
        }
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    // The capture first, from the query as the game built it.
    if (title == GameTitle::Halo3 && seated && use != shot_lag::IntentUse::Missing)
    {
        LegacySeatQueryRecord r{};
        memcpy(r.start, start, sizeof(r.start));
        const float vl = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        if (std::isfinite(vl) && vl > 1.0e-6f)
            for (int axis = 0; axis < 3; ++axis)
                r.dir[axis] = vector[axis] / vl;
        memcpy(r.point, point, sizeof(r.point));
        memcpy(r.solveEye, intent.p.eye, sizeof(r.solveEye));
        memcpy(r.vel, intent.p.eyeVel, sizeof(r.vel));
        r.velValid = intent.p.eyeVelValid ? 1 : 0;
        r.recordValid = LegacyDriveRecordSnapshot(title, r.recordYaw, r.recordPitch, r.setYaw, r.setPitch) ? 1 : 0;
        r.hullValid = Halo3ReadRollStableFollow(r.hullYaw, r.hullPitch) ? 1 : 0;
        const shot_lag::ShotLagSample m = shot_lag::MeasureShotLag(start, vector, point, nullptr, nullptr);
        r.missMm = m.valid ? m.missMm : -1.0f;
        r.ignoredA = ignoredA;
        r.vehicle = rayRead ? ray.ignoreB : -1;
        r.atUs = Sight_NowUs();
        r.reaimed = use == shot_lag::IntentUse::Use ? 1 : 0;
        auto& ring = g_legacySeatQueries;
        AcquireSRWLockExclusive(&ring.lock);
        ring.records[ring.written % kLegacySeatQueryRing] = r;
        ++ring.written;
        ReleaseSRWLockExclusive(&ring.lock);
    }
#endif
    // Another unit's fire or query (an ally, an enemy, a co-op player in the next seat), or no ray to tell: never ours
    // to touch. The local fire's own shot line (an off-hand gun under independent dual aim, a barrel / muzzle shot):
    // its assist traces that gun's ray, never the aim hand's point (shot_lag::GateCrosshairQuery).
    const shot_lag::QueryGate gate = shot_lag::GateCrosshairQuery(
        true, LegacyFireTag(title), localQuery, LegacyOwnShotLineActive(title));
    if (gate != shot_lag::QueryGate::Try)
    {
        if (gate != shot_lag::QueryGate::NotSite)
            Sight_NoteReaimSkipped(title, SightShotSite::CrosshairAssist, shot_lag::QueryGateSkip(gate));
        return false;
    }
    if (use != shot_lag::IntentUse::Use)
    {
        Sight_NoteReaimSkipped(title, SightShotSite::CrosshairAssist, use);
        return false;
    }
    memcpy(out, vector, sizeof(float) * 3);
    // shot_point_transport (sight_layer.h Sight_ShotPoint): through P moved with the eye since its camera sample --
    // the game's eye at this fire tick where it publishes one -- instead of the world point P. On in a player build,
    // on foot and in a seat (Sight_TransportWanted); counted on the dev SHOT LAG transport line.
    float transported[3]{};
    point = Sight_ShotPoint(title, SightTransportSite::CrosshairAssist, intent, seated, point, transported, start);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // THE MULTIPLAYER RESIDUAL (dev SHOT UPSTREAM): this seated query as the game built it -- what an unmodded host
    // firing from our record would trace -- against the seat's P, before anything here touches it.
    seat_upstream::StockLineSample stock{};
    if (title == GameTitle::Halo3 && seated)
        stock = seat_upstream::MeasureStockLine(start, vector, point, intent.p.eyeVelValid ? intent.p.eyeVel : nullptr);
    // Dev key seated_inherit_compensation (default 0; halo3_impact_witness.inl): a Halo 3 seated round that will
    // inherit its vehicle's sideways velocity is aimed through the point that makes its own velocity reach P.
    float compensated[3]{};
    if (title == GameTitle::Halo3 && seated && Halo3SeatedInheritPoint(point, compensated))
        point = compensated;
#endif
    // A STOCK SEAT'S QUERY IS LEFT AS THE GAME BUILT IT. Bending it here is a change only this machine makes: a
    // host fires its own line whatever this machine predicted, and a lockstep peer computes another shot. The
    // record was aimed along this very line upstream instead, at every control pass (LegacyDriveRecomposeSeated),
    // so the line already passes through the point as closely as that solve got. SHOT RECORD Halo 3 seated counts
    // the passes it could not solve, and the dev SHOT UPSTREAM line below measures what is left.
    const bool reaimed = !stockSeat &&
        Sight_ReaimThroughPoint(title, SightShotSite::CrosshairAssist, start, out, point, seated);
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (stock.valid)
        LegacySightNoteStockLine(stock, reaimed, intent.solvePoint);
#endif
    return reaimed;
}

// The dispatch, right after LegacySightReaimCrosshairQuery: a stock seat's query moved to start at the eye (see
// LegacySeatFromEye). True = trace from `outStart` along `outVector` (the query's own length) instead.
bool LegacySightSeatLineFromEye(GameTitle title, const float* start, const float* vector, float outStart[3],
                                float outVector[3])
{
    LegacySeatFromEye& f = t_legacySeatFromEye;
    if (!f.valid)
        return false;
    f.valid = false;
    if (title != GameTitle::Halo3 || !start || !vector || std::memcmp(start, f.start, sizeof(f.start)) != 0)
        return false;
    const float fromEye[3]{start[0] - f.head[0], start[1] - f.head[1], start[2] - f.head[2]};
    if (shot_lag::Length(fromEye) < kLegacySeatFromEyeKeepWu)
        return false;   // already at the eye
    const float toPoint[3]{f.point[0] - f.head[0], f.point[1] - f.head[1], f.point[2] - f.head[2]};
    const float range = shot_lag::Length(toPoint), length = shot_lag::Length(vector);
    if (!std::isfinite(range) || !(range > shot_lag::ReaimRails{}.minRangeWu) || !std::isfinite(length) ||
        !(length > 1.0e-6f))
        return false;
    const float scale = length / range;
    for (int axis = 0; axis < 3; ++axis)
    {
        outStart[axis] = f.head[axis];
        outVector[axis] = toPoint[axis] * scale;
    }
    // The SHOT LINE witness measures the line that is traced.
    if (t_legacySeatShotLine.valid)
        memcpy(t_legacySeatShotLine.start, outStart, sizeof(t_legacySeatShotLine.start));
    g_legacySeatFromEyeLines.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void LegacySightNoteCrosshairResult(GameTitle title, const float* start, const float* vector, const void* result,
                                    uint8_t returned)
{
    LegacySeatShotLineCapture& c = t_legacySeatShotLine;
    if (!c.valid)
        return;
    c.valid = false;
    if (title != GameTitle::Halo3 || !start || !vector || !result ||
        start[0] != c.start[0] || start[1] != c.start[1] || start[2] != c.start[2])
        return;
    const float vl = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    if (!std::isfinite(vl) || vl < 1.0e-6f)
        return;
    const float d[3]{vector[0] / vl, vector[1] / vl, vector[2] / vl};
    const float w[3]{c.point[0] - start[0], c.point[1] - start[1], c.point[2] - start[2]};
    const float along = w[0] * d[0] + w[1] * d[1] + w[2] * d[2];
    if (!(along > 0.0f))
        return;
    auto& win = g_legacySeatShotLine;
    win.queries.fetch_add(1, std::memory_order_relaxed);
    const float aside[3]{w[0] - d[0] * along, w[1] - d[1] * along, w[2] - d[2] * along};
    if (aside[0] * aside[0] + aside[1] * aside[1] + aside[2] * aside[2] > kLegacySeatShotAsideWu * kLegacySeatShotAsideWu)
        win.passedAside.fetch_add(1, std::memory_order_relaxed);
    {
        // Where this line started, from the eye the reticle is drawn for.
        const float s[3]{start[0] - c.eye[0], start[1] - c.eye[1], start[2] - c.eye[2]};
        const uint32_t startCm = static_cast<uint32_t>(std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]) * 304.8f + 0.5f);
        aim_caller::AtomicMaxU32(win.maxStartCm, startCm);
        win.lastStartCm.store(startCm, std::memory_order_relaxed);
    }
    const auto* bytes = static_cast<const uint8_t*>(result);
    float fraction = 0.0f;
    int32_t resultType = 0, object = -1;
    __try
    {
        memcpy(&fraction, bytes + 4, sizeof(fraction));
        memcpy(&resultType, bytes, sizeof(resultType));
        memcpy(&object, bytes + 0x40, sizeof(object));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if (!returned || !std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f)
        return;
    const float stop = fraction * vl;
    if (!(stop < along - kLegacySeatShotShortWu))
        return;
    win.blocked.fetch_add(1, std::memory_order_relaxed);
    if (resultType == 4)
    {
        win.byObject.fetch_add(1, std::memory_order_relaxed);
        // Ours: you, or any part of the vehicle you sit in -- an attached part has its own handle, so it is found by
        // its root, as the aim trace finds it.
        bool own = object != -1 && (object == c.own[0] || object == c.own[1] || object == c.own[2]);
        uint32_t definition = 0xFFFFFFFFu;
        int objectKind = -1;
        if (object != -1)
        {
            const int32_t hitRoot = LegacySightRootOf(Halo3SightObjectData, object);
            const int32_t ownRoot = LegacySightRootOf(Halo3SightObjectData, c.own[0] != -1 ? c.own[0] : c.own[1]);
            own = own || (hitRoot != -1 && hitRoot == ownRoot);
            __try
            {
                if (const uint8_t* data = Halo3SightObjectData(static_cast<uint32_t>(object)))
                    definition = *reinterpret_cast<const uint16_t*>(data);
                objectKind = Halo3SightObjectType(static_cast<uint32_t>(object));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                definition = 0xFFFFFFFFu;
            }
        }
        if (own)
            win.byOwn.fetch_add(1, std::memory_order_relaxed);
        if (object != -1 && object == c.own[0])
            win.byYou.fetch_add(1, std::memory_order_relaxed);
        win.lastObjectDef.store(definition, std::memory_order_relaxed);
        win.lastObjectType.store(static_cast<uint32_t>(objectKind), std::memory_order_relaxed);
    }
    const auto cm = [](float wu) { return static_cast<uint32_t>(std::fabs(wu) * 304.8f + 0.5f); };
    aim_caller::AtomicMaxU32(win.maxShortCm, cm(along - stop));
    win.lastStopCm.store(cm(stop), std::memory_order_relaxed);
    win.lastPointCm.store(cm(along), std::memory_order_relaxed);
    // Where the stop stood from the eye: its angle off the eye -> point line, and above or below it.
    const float b[3]{start[0] + d[0] * stop - c.eye[0], start[1] + d[1] * stop - c.eye[1],
                     start[2] + d[2] * stop - c.eye[2]};
    const float p[3]{c.point[0] - c.eye[0], c.point[1] - c.eye[1], c.point[2] - c.eye[2]};
    const float bl = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    const float pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (bl > 1.0e-4f && pl > 1.0e-4f)
    {
        float cosine = (b[0] * p[0] + b[1] * p[1] + b[2] * p[2]) / (bl * pl);
        cosine = cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine);
        aim_caller::AtomicMaxU32(win.maxAngleCdeg, static_cast<uint32_t>(std::acos(cosine) * 5729.578f));
        const float elevationStop = std::asin(b[2] / bl), elevationPoint = std::asin(p[2] / pl);
        if (elevationStop > elevationPoint)
            win.above.fetch_add(1, std::memory_order_relaxed);
        else
            win.below.fetch_add(1, std::memory_order_relaxed);
    }
}

// The cold worker's line: the window since the last report.
void LegacySightReportSeatShotLine()
{
    auto& win = g_legacySeatShotLine;
    const uint32_t queries = win.queries.exchange(0, std::memory_order_relaxed);
    if (!queries)
        return;
    const uint32_t blocked = win.blocked.exchange(0, std::memory_order_relaxed);
    const uint32_t byObject = win.byObject.exchange(0, std::memory_order_relaxed);
    const uint32_t byOwn = win.byOwn.exchange(0, std::memory_order_relaxed);
    const uint32_t byYou = win.byYou.exchange(0, std::memory_order_relaxed);
    const uint32_t maxStartCm = win.maxStartCm.exchange(0, std::memory_order_relaxed);
    const uint32_t lastStartCm = win.lastStartCm.load(std::memory_order_relaxed);
    const uint32_t objectDef = win.lastObjectDef.exchange(0xFFFFFFFFu, std::memory_order_relaxed);
    const uint32_t objectType = win.lastObjectType.exchange(0xFFFFFFFFu, std::memory_order_relaxed);
    // The last object a line stopped on, by its tag (the cold worker reads the map's names).
    char objectText[200] = "";
    if (byObject && objectDef != 0xFFFFFFFFu)
    {
        const std::string_view path = Halo3TagNameOf(objectDef);
        std::snprintf(objectText, sizeof(objectText), "; the last object one stopped on: %.*s (definition %04X, object "
                      "type %d)", static_cast<int>(path.empty() ? 9 : path.size()),
                      path.empty() ? "(unnamed)" : path.data(), objectDef, static_cast<int>(objectType));
    }
    const uint32_t above = win.above.exchange(0, std::memory_order_relaxed);
    const uint32_t below = win.below.exchange(0, std::memory_order_relaxed);
    const uint32_t aside = win.passedAside.exchange(0, std::memory_order_relaxed);
    const uint32_t shortCm = win.maxShortCm.exchange(0, std::memory_order_relaxed);
    const uint32_t angle = win.maxAngleCdeg.exchange(0, std::memory_order_relaxed);
    const uint32_t fromEye = g_legacySeatFromEyeLines.exchange(0, std::memory_order_relaxed);
    if (!blocked && !aside)
        return;
    LOG("SHOT LINE Halo 3 seated: of %u crosshair queries (the line each shot follows, through the point; %u of them "
        "started at your eye, seat_fire_from_eye, the rest at the game's own camera), %u stopped short of the point (%u on an object -- %u of them you or a part of your "
        "vehicle, %u you yourself -- and %u on the world; by up to %.1f m, the "
        "last at %.1f m of %.1f m), the stop up to %.1f deg off the point as the eye sees it (%u above it, %u below) "
        "-- a shot goes where its query stops, not to the reticle; %u lines passed over 1 m from the point; the lines "
        "started up to %.1f m from your eye (the last %.1f m)%s",
        queries, fromEye, blocked, byObject, byOwn, byYou, blocked - byObject, double(shortCm) / 100.0,
        double(win.lastStopCm.load(std::memory_order_relaxed)) / 100.0,
        double(win.lastPointCm.load(std::memory_order_relaxed)) / 100.0, double(angle) / 100.0, above, below, aside,
        double(maxStartCm) / 100.0, double(lastStartCm) / 100.0, objectText);
}

// The dev SHOT LAG line's crosshair-assist path: the query the game traced (after any re-aim), per local query at
// a resolved crosshair site. Collision-hook thread.
void LegacySightNoteCrosshairQuery(GameTitle title, uint64_t flags, const float* start, const float* vector,
                                   int32_t ignoredA, uintptr_t caller)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    if ((title != GameTitle::Halo3 && title != GameTitle::Halo3ODST) || !start || !vector)
        return;
    auto& state = LegacySight(title);
    bool site = false;
    for (const auto& known : state.siteReturn)
        if (const uintptr_t at = known.load(std::memory_order_relaxed); at && at == caller)
            site = true;
    // Another gun's own line (an off-hand or barrel shot) is not the aim hand's: its miss from P measures nothing.
    if (!site || flags != state.flags.load(std::memory_order_acquire) || TitleAdapter_GetActiveTitle() != title ||
        LegacyOwnShotLineActive(title) || LegacyFireTag(title) != shot_lag::kFireLocal)
        return;
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightRay ray{};
    SightIntent intent{};
    const bool seated = Sight_ReadIntent(title, intent) && intent.p.seated;
    if (!Sight_ReadRay(title, generation, 250, ray) ||
        !LegacySightQueryIsLocal(title, ignoredA, ray, seated))
        return;
    Sight_NoteShotLagFromIntent(title, SightShotPath::CrosshairAssist, start, vector, t_legacyFirePredicted, seated,
                                seated ? ray.ignoreB : -1);
#else
    (void)title; (void)flags; (void)start; (void)vector; (void)ignoredA; (void)caller;
#endif
}

// Cold worker, every 5 s: how the servo target was chosen per title, and why a title does not trace.
void LegacySightReport()
{
    static uint64_t s_last = 0;
    const uint64_t now = GetTickCount64();
    if (now - s_last < 5000)
        return;
    s_last = now;
    for (const GameTitle title : {GameTitle::Halo3, GameTitle::Halo3ODST, GameTitle::Halo4})
    {
        auto& state = LegacySight(title);
        const uint64_t converged = state.converged.exchange(0, std::memory_order_relaxed);
        const uint64_t fallback = state.fallback.exchange(0, std::memory_order_relaxed);
        const uint64_t seatedTraces = state.seatedTraces.exchange(0, std::memory_order_relaxed);
        const uint64_t selfHits = state.selfHits.exchange(0, std::memory_order_relaxed);
        const uint64_t seatUnnamed = state.seatUnnamed.exchange(0, std::memory_order_relaxed);
        const uint64_t rootFallbacks = state.rootFallbacks.exchange(0, std::memory_order_relaxed);
        if (!converged && !fallback && !seatedTraces && !seatUnnamed)
            continue;
        const char* why = state.why.load(std::memory_order_acquire);
        LOG("SIGHT %s aim servo: %llu targets converged on the traced point, %llu on the fixed-distance steer "
            "(no fresh trace)%s%s; seated traces %llu (looked past our own vehicle %llu times, %llu from the "
            "published seat chain), %llu seats untraced (no vehicle named)", LegacySightTitleName(title),
            static_cast<unsigned long long>(converged), static_cast<unsigned long long>(fallback),
            why ? "; trace off: " : "", why ? why : "", static_cast<unsigned long long>(seatedTraces),
            static_cast<unsigned long long>(selfHits), static_cast<unsigned long long>(rootFallbacks),
            static_cast<unsigned long long>(seatUnnamed));
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        // THE HORNET CAPTURE: what is new in the ring (at most 4 lines per report).
        auto& ring = g_legacySeatQueries;
        LegacySeatQueryRecord copy[4]{};
        uint32_t taken = 0, skipped = 0;
        AcquireSRWLockExclusive(&ring.lock);
        if (ring.written - ring.printed > kLegacySeatQueryRing)
            ring.printed = ring.written - kLegacySeatQueryRing;
        skipped = ring.written - ring.printed > 4 ? ring.written - ring.printed - 4 : 0;
        ring.printed += skipped;
        while (ring.printed != ring.written && taken < 4)
            copy[taken++] = ring.records[ring.printed++ % kLegacySeatQueryRing];
        ReleaseSRWLockExclusive(&ring.lock);
        for (uint32_t i = 0; i < taken; ++i)
        {
            const auto& r = copy[i];
            const float qYaw = std::atan2(r.dir[1], r.dir[0]) * 57.29578f;
            const float qPitch = std::asin(std::clamp(r.dir[2], -1.0f, 1.0f)) * 57.29578f;
            const float speed = std::sqrt(r.vel[0] * r.vel[0] + r.vel[1] * r.vel[1] + r.vel[2] * r.vel[2]) * 3.048f;
            const float se[3]{r.start[0] - r.solveEye[0], r.start[1] - r.solveEye[1], r.start[2] - r.solveEye[2]};
            LOG("SIGHT Halo 3 seat query (dev, the Hornet capture): start %.3f,%.3f,%.3f wu (minus the solve's eye "
                "%+.0f,%+.0f,%+.0f mm), dir yaw %.2f pitch %.2f deg; the point %.0f mm off it%s; record yaw %.2f pitch "
                "%.2f, setpoint yaw %.2f pitch %.2f deg%s; hull yaw %.2f pitch %.2f deg%s; eye velocity %.2f,%.2f,"
                "%.2f m/s (|%.1f|)%s; ignore %d vehicle %d",
                double(r.start[0]), double(r.start[1]), double(r.start[2]), double(se[0] * 3048.0f),
                double(se[1] * 3048.0f), double(se[2] * 3048.0f), double(qYaw), double(qPitch), double(r.missMm),
                r.reaimed ? " (re-aimed)" : " (left)", double(r.recordYaw * 57.29578f),
                double(r.recordPitch * 57.29578f), double(r.setYaw * 57.29578f), double(r.setPitch * 57.29578f),
                r.recordValid ? "" : " (no record)", double(r.hullYaw * 57.29578f), double(r.hullPitch * 57.29578f),
                r.hullValid ? "" : " (no hull frame)", double(r.vel[0] * 3.048f), double(r.vel[1] * 3.048f),
                double(r.vel[2] * 3.048f), double(speed), r.velValid ? "" : " (unknown)", int(r.ignoredA),
                int(r.vehicle));
        }
        if (skipped)
            LOG("SIGHT Halo 3 seat query (dev): %u more not printed", skipped);
    }
    LegacySightReportStockLines();   // dev SHOT UPSTREAM: the multiplayer residual per seat
    for (int slot = 0; slot < 2; ++slot)
    {
        auto& window = g_legacyEngineTrace[slot];
        const uint32_t sites = window.siteCalls.exchange(0, std::memory_order_relaxed);
        const uint32_t seen = window.seen.exchange(0, std::memory_order_relaxed);
        const uint32_t compared = window.compared.exchange(0, std::memory_order_acquire);
        const uint32_t heldOff = window.heldOff.exchange(0, std::memory_order_relaxed);
        if (!seen && !sites)
            continue;
        LOG("SIGHT %s engine crosshair: %u queries from the crosshair call site(s) (last flags 0x%llX); %u "
            "with the resolved flags, %u while converged (%u more skipped: a seat limit, turret or stall held the "
            "aim off the point); the point aimed at is %.0f mm off the game's line "
            "(max %.0f) at %.2f m; line vs eye->point %.2f deg; its start minus the solve's eye "
            "[along,right,up] %.0f,%.0f,%.0f mm", slot ? "ODST" : "Halo 3", sites,
            static_cast<unsigned long long>(window.siteFlags.load()), seen, compared, heldOff,
            double(window.missMm.load()), double(window.maxMissMm.exchange(0.0f)), double(window.rangeM.load()),
            double(window.angleDeg.load()), double(window.startAlongMm.load()),
            double(window.startRightMm.load()), double(window.startUpMm.load()));
    }
    {
        auto& w = g_legacyTurretShots;
        const uint32_t shots = w.shots.exchange(0, std::memory_order_relaxed);
        if (shots)
            LOG("SIGHT Halo 3 turret shots (dev): %u shots of the gun-slaved seat's gun (%u with the gun settled on "
                "the writes, %u with no fresh traced point); the point is %.0f mm off the barrel's line, before the "
                "firing assist turns it (max settled %.0f, max while slewing %.0f) at %.2f m; that line vs eye->point "
                "%.2f deg; its start minus the solve's eye (the gun's camera marker) [along,right,up] %.0f,%.0f,%.0f "
                "mm (where the shot really goes: SHOT Halo 3 assist / SHOT IMPACT SEATED)",
                shots, w.settled.exchange(0, std::memory_order_relaxed),
                w.noPoint.exchange(0, std::memory_order_relaxed), double(w.missMm.load()),
                double(aim_caller::BitsToFloat(w.maxMissSettledBits.exchange(0, std::memory_order_relaxed))),
                double(aim_caller::BitsToFloat(w.maxMissSlewingBits.exchange(0, std::memory_order_relaxed))),
                double(w.rangeM.load()), double(w.angleDeg.load()), double(w.startAlongMm.load()),
                double(w.startRightMm.load()), double(w.startUpMm.load()));
    }
#endif
}
