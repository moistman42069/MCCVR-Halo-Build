// Included in game.cpp (anonymous namespace) after reach_fire_eye.inl and before reach_contact_melee_runtime.inl.
// Dev builds only: the file compiles to nothing in a player build.
//
// SIM TRACE (dev key dev_sim_trace = 1), Halo: Reach. At the first unit update of every simulation tick, what
// every object in the game holds -- where it is, how it is turned and moving, its skeleton, a unit's aim -- is
// appended to HaloMCCVR-simtrace-<stamp>.bin beside the log (tools/simtrace_compare.py reads two of them).
//
// What it is for. A synchronised co-op session ends when the two machines' simulations part, and nothing on one
// machine can show that. A saved film can: it replays a game through the simulation from its recorded actions
// alone, so every replay of one film holds the same objects on every tick unless something changed the
// simulation. THE REFERENCE is a replay with nothing of the mod installed but this recorder (dev_sim_minimal); a
// replay with the mod installed as usual is compared with it, never with another replay of its own kind (two of
// those agree with each other whatever the mod changes). dev_sim_bisect makes one group of the mod's detours at a
// time a plain pass-through, to find which of them a difference belongs to; dev_sim_probe makes one of the mod's
// own calls on demand, to test a call by itself.
//
// The film's player is the local player to the mod in Theater (the output user's unit), so the first-person lanes
// run on that unit as they do in play. No control pass runs in Theater: the aim drive does not arm there, and
// nothing scheduled from the control pass is exercised by a replay.

#if HALOMCCVR_DEV_DIAGNOSTICS

// ADDR-HYGIENE: dev-only. The tick ([[TLS + 0xA0]] + 0xC, the anchors AimCallerTickProofAt checks at install) and the
// object datum's fields (parent +0x14, position +0x44, the node block's size and offset +0x178 / +0x17A: read from
// the pinned module's object-origin getters 0x471D38 / 0x471DC0) are read under SEH and only ever written to the
// trace file. A wrong offset shows as two replays of one film that never agree.
inline constexpr size_t kReachSimTickTlsSlot = 0xA0, kReachSimTickField = 0x0C;
inline constexpr size_t kReachSimObjectParent = 0x14, kReachSimObjectPosition = 0x44, kReachSimObjectFrame = 0x50,
    kReachSimObjectMotion = 0x68, kReachSimObjectNodeBytes = 0x178, kReachSimObjectNodeOffset = 0x17A;
inline constexpr uint32_t kReachSimTraceMagic = 0x544D4953u;   // "SIMT"
inline constexpr size_t kReachSimTraceCapacity = size_t(1) << 30;

#pragma pack(push, 1)
struct ReachSimTraceHeader
{
    uint32_t magic, generation, tick, count;
    uint64_t wallMs;
    uint32_t probe;        // dev_sim_probe as it stood on this tick
    uint32_t probesRun;    // probe calls made so far in this process (low 32 bits)
};
struct ReachSimTraceRecord
{
    uint16_t index;
    int16_t salt;
    uint8_t kind, reserved;
    uint16_t nodeBytes;
    int32_t parent;
    float position[3];
    uint32_t hashFrame, hashMotion, hashNodes, hashAim;
};
#pragma pack(pop)
static_assert(sizeof(ReachSimTraceHeader) == 32 && sizeof(ReachSimTraceRecord) == 40);

struct ReachSimTraceState
{
    uint8_t* buffer = nullptr;             // one arena for the process, never freed: the sim thread appends
    std::atomic<bool> ready{false};
    std::atomic<size_t> head{0};           // bytes written; the worker reads below it
    size_t flushed = 0;                    // worker only
    HANDLE file = INVALID_HANDLE_VALUE;    // worker only
    wchar_t path[MAX_PATH]{};              // worker only
    uint32_t lastTick = 0, lastGeneration = 0;   // sim thread
    bool haveTick = false;
    std::atomic<uint64_t> ticks{0}, records{0}, dropped{0}, faults{0};
    std::atomic<uint64_t> probes{0}, probeTraces{0}, probeHits{0}, probeRefused{0};
    // The between-ticks site: the sim thread (as the per-unit update saw it), and why a camera build was passed over.
    std::atomic<uint32_t> simThread{0};
    std::atomic<uint64_t> frameOtherThread{0}, frameOtherCaller{0}, frameInUpdate{0};
} g_reachSimTrace;
// Set around the game's own per-unit update (the sim thread): a camera build asked for from inside one is in a tick.
thread_local int t_reachSimInUnitUpdate = 0;

// THE REFERENCE (dev key dev_sim_minimal = 1). Nothing of the mod is installed in Reach -- no camera core, no aim
// drive, no other hook -- except one detour on the per-unit update that notes the tick's objects and runs the
// game's own function. A replay recorded this way is the game's own; a replay with the mod installed as usual must
// match it. The detour is never removed (one film per launch: the process is closed afterwards).
struct ReachSimMinimalState
{
    void* target = nullptr;
    uint8_t (__fastcall* original)(uint32_t) = nullptr;
    uintptr_t base = 0;
    bool refused = false;
} g_reachSimMinimal;

inline uint32_t ReachSimHash(const uint8_t* bytes, size_t count)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < count; ++i)
        h = (h ^ bytes[i]) * 16777619u;
    return h;
}

// Sim thread, from the per-unit update: once per tick, before that tick's first unit update runs.
void ReachSimTraceNote()
{
    if (!g_config.dev_sim_trace)
        return;
    ReachSimTraceState& t = g_reachSimTrace;
    const uintptr_t moduleBase = g_reachCamera.base ? g_reachCamera.base : g_reachSimMinimal.base;
    if (!t.ready.load(std::memory_order_acquire) || !moduleBase)
        return;
    __try
    {
        auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
        const uint32_t tlsIndex = *reinterpret_cast<const uint32_t*>(moduleBase + kReachEngineTlsIndexRva);
        const uint8_t* tls = (slots && tlsIndex < 0x200) ? static_cast<const uint8_t*>(slots[tlsIndex]) : nullptr;
        const uintptr_t time = tls ? *reinterpret_cast<const uintptr_t*>(tls + kReachSimTickTlsSlot) : 0;
        if (!time)
            __leave;
        const uint32_t tick = *reinterpret_cast<const uint32_t*>(time + kReachSimTickField);
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_relaxed);
        if (t.haveTick && tick == t.lastTick && generation == t.lastGeneration)
            __leave;
        t.haveTick = true;
        t.lastTick = tick;
        t.lastGeneration = generation;
        t.simThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        const uint8_t* collection = *reinterpret_cast<const uint8_t* const*>(tls + kReachTlsObjectCollectionOffset);
        if (!collection || !*(collection + kReachDataCollectionInitializedOffset) ||
            *reinterpret_cast<const uint64_t*>(collection + kReachDataCollectionStrideOffset) != kReachObjectEntryStride)
            __leave;
        const int32_t count = *reinterpret_cast<const int32_t*>(collection + kReachDataCollectionCountOffset);
        const uint8_t* entries = *reinterpret_cast<const uint8_t* const*>(collection + kReachDataCollectionEntriesOffset);
        if (!entries || count <= 0 || count > 0x10000)
            __leave;
        const size_t head = t.head.load(std::memory_order_relaxed);
        if (head + sizeof(ReachSimTraceHeader) + size_t(count) * sizeof(ReachSimTraceRecord) > kReachSimTraceCapacity)
        {
            t.dropped.fetch_add(1, std::memory_order_relaxed);
            __leave;
        }
        auto* header = reinterpret_cast<ReachSimTraceHeader*>(t.buffer + head);
        auto* out = reinterpret_cast<ReachSimTraceRecord*>(header + 1);
        uint32_t written = 0;
        for (int32_t i = 0; i < count; ++i)
        {
            const uint8_t* entry = entries + size_t(i) * kReachObjectEntryStride;
            const int16_t salt = *reinterpret_cast<const int16_t*>(entry + kReachObjectEntrySaltOffset);
            const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(entry + kReachObjectEntryDataOffset);
            if (!salt || !data)
                continue;
            ReachSimTraceRecord& r = out[written];
            r.index = static_cast<uint16_t>(i);
            r.salt = salt;
            r.kind = *(entry + kReachObjectEntryKindOffset);
            r.reserved = 0;
            memcpy(&r.parent, data + kReachSimObjectParent, sizeof(r.parent));
            memcpy(r.position, data + kReachSimObjectPosition, sizeof(r.position));
            r.hashFrame = ReachSimHash(data + kReachSimObjectFrame, 0x18);
            r.hashMotion = ReachSimHash(data + kReachSimObjectMotion, 0x18);
            int16_t nodeBytes = 0, nodeOffset = 0;
            memcpy(&nodeBytes, data + kReachSimObjectNodeBytes, sizeof(nodeBytes));
            memcpy(&nodeOffset, data + kReachSimObjectNodeOffset, sizeof(nodeOffset));
            const bool nodes = nodeBytes > 0 && nodeBytes <= 0x34 * 255 && nodeBytes % 0x34 == 0 && nodeOffset > 0;
            r.nodeBytes = nodes ? static_cast<uint16_t>(nodeBytes) : 0;
            r.hashNodes = nodes ? ReachSimHash(data + nodeOffset, static_cast<size_t>(nodeBytes)) : 0;
            r.hashAim = r.kind <= kReachObjectKindVehicle ? ReachSimHash(data + kReachUnitAimingVectorOffset, 12) : 0;
            ++written;
        }
        header->magic = kReachSimTraceMagic;
        header->generation = generation;
        header->tick = tick;
        header->count = written;
        header->wallMs = GetTickCount64();
        header->probe = static_cast<uint32_t>(g_config.dev_sim_probe);
        header->probesRun = static_cast<uint32_t>(t.probes.load(std::memory_order_relaxed));
        t.head.store(head + sizeof(ReachSimTraceHeader) + size_t(written) * sizeof(ReachSimTraceRecord),
                     std::memory_order_release);
        t.ticks.fetch_add(1, std::memory_order_relaxed);
        t.records.fetch_add(written, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        t.faults.fetch_add(1, std::memory_order_relaxed);
    }
}

// THE PROBE (dev key dev_sim_probe, live through the dev pad's `set`). One of the mod's own calls, once per tick,
// for the local player's unit:
//   bit 0  the aim trace INSIDE the tick, the way play makes it: in the unit's own update, after the game's.
//          Reach's collision_test_vector with the crosshair trace's flags, 1 km from the unit's camera along its
//          aim, ignoring what the real trace ignores
//   bit 1  the unit camera-position evaluator alone, inside the tick (bit 0 asks it too, for the ray's start)
//   bit 2  the same aim trace BETWEEN ticks: when the game builds this unit's camera (0x48A4B4's call of the
//          evaluator: the control pass and the director, once a frame on the sim thread), never inside a unit update
//   bit 3  the ray test inside the tick WITHOUT the evaluator: the ray starts 0.6 world units above the unit's own
//          origin (an on-foot unit only), so the collision test is the only call made
// The local unit, looked up at most every 100 ms (sim thread).
int32_t ReachSimProbeLocalUnit()
{
    static uint64_t s_localAtMs = 0;
    static int32_t s_local = -1;
    const uint64_t now = GetTickCount64();
    if (!s_localAtMs || now < s_localAtMs || now - s_localAtMs > 100)
    {
        s_local = LegacyCollisionIgnoredObject(GameTitle::HaloReach);
        s_localAtMs = now;
    }
    return s_local;
}

enum class ReachSimProbeKind : uint8_t { EvaluatorAndTrace, EvaluatorOnly, TraceFromOrigin };
void ReachSimProbeTrace(int32_t local, ReachSimProbeKind kind);

void ReachSimProbe(uint32_t unit)
{
    const int probe = g_config.dev_sim_probe;
    if (!(probe & 0xB))
        return;
    ReachSimTraceState& t = g_reachSimTrace;
    static uint64_t s_tracedTick = ~0ull;
    const int32_t local = ReachSimProbeLocalUnit();
    if (local == -1 || unit != static_cast<uint32_t>(local))
        return;
    if (t.haveTick && s_tracedTick == t.lastTick)
        return;
    s_tracedTick = t.lastTick;
    if (probe & 3)
        ReachSimProbeTrace(local, (probe & 1) ? ReachSimProbeKind::EvaluatorAndTrace : ReachSimProbeKind::EvaluatorOnly);
    if (probe & 8)
        ReachSimProbeTrace(local, ReachSimProbeKind::TraceFromOrigin);
}

// From the unit camera-position hook (any thread, any caller): bit 2's site.
void ReachSimProbeFrame(int32_t unit, uintptr_t caller)
{
    if (!(g_config.dev_sim_probe & 4) || !g_reachCamera.base)
        return;
    ReachSimTraceState& t = g_reachSimTrace;
    if (GetCurrentThreadId() != t.simThread.load(std::memory_order_relaxed))
    {
        t.frameOtherThread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // ADDR-HYGIENE: dev-only. The return address of 0x48A4B4's call of the evaluator (call at 0x48A696), measured
    // on the pinned Steam module; a build where it differs counts every call as "other caller" and probes nothing.
    if (caller != g_reachCamera.base + 0x48A69B)
    {
        t.frameOtherCaller.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (t_reachSimInUnitUpdate)
    {
        t.frameInUpdate.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    static uint64_t s_tracedTick = ~0ull;
    const int32_t local = ReachSimProbeLocalUnit();
    if (local == -1 || unit != local)
        return;
    if (t.haveTick && s_tracedTick == t.lastTick)
        return;
    s_tracedTick = t.lastTick;
    ReachSimProbeTrace(local, ReachSimProbeKind::EvaluatorAndTrace);
}

void ReachSimProbeTrace(int32_t local, ReachSimProbeKind probeKind)
{
    ReachSimTraceState& t = g_reachSimTrace;
    const int32_t s_local = local;
    float eye[3]{};
    if (probeKind != ReachSimProbeKind::TraceFromOrigin)
    {
        if (!ReachCallUnitCameraEvaluator(s_local, eye))
        {
            t.probeRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        t.probes.fetch_add(1, std::memory_order_relaxed);
        if (probeKind == ReachSimProbeKind::EvaluatorOnly)
            return;
    }
    int32_t mode = 0;
    const uint64_t flags = ReachSightActiveFlags(mode);
    const auto original = reinterpret_cast<LegacyCollisionTestVectorFn>(g_reachWorldCollision.original);
    uint8_t kind = 0;
    const unsigned char* data = ReachVehicleObjectData(s_local, kind);
    float aim[3]{};
    int32_t parent = -1;
    if (!flags || !original || !g_reachWorldCollision.originalIsVectorTest || !data ||
        !ReachDevReadVec3(reinterpret_cast<const float*>(data + kReachUnitAimingVectorOffset), aim))
    {
        t.probeRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    __try { memcpy(&parent, data + kReachSimObjectParent, sizeof(parent)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { parent = -1; }
    if (probeKind == ReachSimProbeKind::TraceFromOrigin)
    {
        // No evaluator: an on-foot unit's own origin, lifted to about eye height.
        if (parent != -1 ||
            !ReachDevReadVec3(reinterpret_cast<const float*>(data + kReachSimObjectPosition), eye))
        {
            t.probeRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        eye[2] += 0.6f;
        t.probes.fetch_add(1, std::memory_order_relaxed);
    }
    const float length = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (!(length > 0.5f && length < 1.5f))
    {
        t.probeRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    int32_t ignoreA = s_local, ignoreB = -1, ignoreC = -1;
    if (parent != -1)
    {
        const int32_t root = ReachSightRootOf(s_local);
        if (root == -1)
        {
            t.probeRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        ignoreA = root;
        ignoreB = s_local;
        ignoreC = parent != root ? parent : -1;
    }
    const float start[3]{eye[0] + aim[0] / length * kReachSightTraceSkipWu, eye[1] + aim[1] / length * kReachSightTraceSkipWu,
                         eye[2] + aim[2] / length * kReachSightTraceSkipWu};
    const float vector[3]{aim[0] / length * kReachSightTraceLengthWu, aim[1] / length * kReachSightTraceLengthWu,
                          aim[2] / length * kReachSightTraceLengthWu};
    alignas(16) uint8_t result[0x400]{};
    uint8_t returned = 0;
    if (!ReachSightGuardedTrace(original, flags, mode, start, vector, ignoreA, ignoreB, ignoreC, result, returned, false))
    {
        t.probeRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    t.probeTraces.fetch_add(1, std::memory_order_relaxed);
    if (returned)
        t.probeHits.fetch_add(1, std::memory_order_relaxed);
}

__declspec(noinline) uint8_t __fastcall ReachSimMinimalDetour(uint32_t unit)
{
    ReachSimTraceNote();
    return g_reachSimMinimal.original ? g_reachSimMinimal.original(unit) : 0;
}

// Cold worker, in place of the camera core's install while dev_sim_minimal is set. The per-unit update is found the
// way the contact-melee lane finds it: its signature at its address, unique, and its one known caller's call.
void ReachSimMinimalInstall(uintptr_t base, size_t size)
{
    ReachSimMinimalState& m = g_reachSimMinimal;
    if (m.target || m.refused || !base)
        return;
    constexpr uintptr_t kUpdate = 0x49F138, kCaller = 0x4A04D0;
    const char* const pattern =
        "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 8B 15 ?? ?? ?? ?? 40 32 FF";
    const uintptr_t hit = sig::Find(base, size, pattern);
    bool callOk = false;
    __try
    {
        const auto* call = reinterpret_cast<const uint8_t*>(base + kCaller);
        int32_t rel = 0;
        memcpy(&rel, call + 1, sizeof(rel));
        callOk = kCaller + 5 < size && call[0] == 0xE8 && base + kCaller + 5 + rel == base + kUpdate;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { callOk = false; }
    if (hit != base + kUpdate || sig::Find(hit + 1, base + size - hit - 1, pattern) || !callOk)
    {
        m.refused = true;
        LOG("SIM TRACE (dev) reference mode: the per-unit update was not found at +0x%llX on this build; nothing is "
            "installed and nothing is traced", static_cast<unsigned long long>(kUpdate));
        return;
    }
    void* const target = reinterpret_cast<void*>(base + kUpdate);
    m.base = base;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&ReachSimMinimalDetour),
                      reinterpret_cast<void**>(&m.original)) != MH_OK || MH_EnableHook(target) != MH_OK)
    {
        m.refused = true;
        LOG("SIM TRACE (dev) reference mode: the detour could not be installed; nothing is traced");
        return;
    }
    m.target = target;
    LOG("SIM TRACE (dev) REFERENCE MODE (dev_sim_minimal = 1): nothing of the mod is installed in Reach but one "
        "detour on the per-unit update (+0x%llX) that notes each tick's objects. No VR view, no aim, no other hook: "
        "this launch is for replaying one film", static_cast<unsigned long long>(kUpdate));
}

// Cold worker, every report: the arena on first use, then whatever the sim thread has finished since.
void ReachSimTraceFlush()
{
    ReachSimTraceState& t = g_reachSimTrace;
    if (!t.buffer)
    {
        if (!g_config.dev_sim_trace)
            return;
        t.buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, kReachSimTraceCapacity, MEM_RESERVE | MEM_COMMIT,
                                                      PAGE_READWRITE));
        if (!t.buffer)
        {
            LOG("SIM TRACE (dev): the %llu MB arena could not be had; nothing is traced",
                static_cast<unsigned long long>(kReachSimTraceCapacity >> 20));
            g_config.dev_sim_trace = 0;
            return;
        }
        SYSTEMTIME st{};
        GetLocalTime(&st);
        swprintf_s(t.path, L"%s\\HaloMCCVR-simtrace-%02u%02u%02u-%02u%02u%02u.bin", LogDirectory(), st.wYear % 100,
                   st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        t.file = CreateFileW(t.path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
        t.ready.store(true, std::memory_order_release);
        LOG("SIM TRACE (dev): on. Every object's state at each simulation tick goes to %ls (compare two with "
            "tools/simtrace_compare.py); dev_sim_probe = %d", t.path, g_config.dev_sim_probe);
    }
    const size_t head = t.head.load(std::memory_order_acquire);
    if (head > t.flushed && t.file != INVALID_HANDLE_VALUE)
    {
        size_t at = t.flushed;
        while (at < head)
        {
            const DWORD chunk = static_cast<DWORD>((std::min)(head - at, size_t(64) << 20));
            DWORD done = 0;
            if (!WriteFile(t.file, t.buffer + at, chunk, &done, nullptr) || !done)
                break;
            at += done;
        }
        t.flushed = at;
    }
    static uint64_t s_lastTicks = ~0ull, s_lastProbes = ~0ull;
    const uint64_t ticks = t.ticks.load(std::memory_order_relaxed);
    const uint64_t probes = t.probes.load(std::memory_order_relaxed);
    if (ticks != s_lastTicks || probes != s_lastProbes)
    {
        s_lastTicks = ticks;
        s_lastProbes = probes;
        LOG("SIM TRACE (dev): %llu ticks, %llu object records, %llu MB written, %llu dropped (arena full), %llu "
            "faults; probe %d: %llu calls, %llu traces (%llu hits), %llu refused; camera builds passed over: %llu on "
            "another thread, %llu from another caller, %llu inside a unit update; local unit 0x%08X, sim thread %u",
            static_cast<unsigned long long>(ticks),
            static_cast<unsigned long long>(t.records.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.flushed >> 20),
            static_cast<unsigned long long>(t.dropped.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.faults.load(std::memory_order_relaxed)), g_config.dev_sim_probe,
            static_cast<unsigned long long>(probes),
            static_cast<unsigned long long>(t.probeTraces.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.probeHits.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.probeRefused.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.frameOtherThread.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.frameOtherCaller.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(t.frameInUpdate.load(std::memory_order_relaxed)),
            static_cast<unsigned>(LegacyCollisionIgnoredObject(GameTitle::HaloReach)),
            static_cast<unsigned>(t.simThread.load(std::memory_order_relaxed)));
    }
}

#endif   // HALOMCCVR_DEV_DIAGNOSTICS
