// Included in game.cpp at file scope, right after legacy_direct_drive.inl, in DEV BUILDS ONLY (the #include
// sits inside #if HALOMCCVR_DEV_DIAGNOSTICS: a player build contains none of this).
//
// THE TYPE-13 PILOT CONTROLLERS, LOG-ONLY (docs/TYPE13-FLIGHT-CONTROLLERS-2026-10-01.md, section 4 Phase B).
// Reach's Sabre/Seraph and Halo 4's Broadsword are physics type 13. Each title's per-user control dispatcher
// (Reach 0x6350C, Halo 4 0xA8828) calls a dedicated attitude controller for a type-13 pilot -- Reach
// haloreach+0x64828, Halo 4 halo4+0xAA524 -- and jumps past the desired-angles accumulator the direct-drive
// hook sits on, so the pilot is never driven and stays on the stick servo. Before anything is written
// there, the running game has to answer what static analysis cannot: whether the record the controller
// writes is what the hull/physics steers by (the LINK), how fast the stick can turn the craft (the RATE, for
// a slew cap), the forced-steer/redirect windows, the roll mode, the time arguments, the tag limits, Halo
// 4's biped physics mode and runtime type override, and which dispatcher callers run in a tick.
//
// Each controller's one hook is the direct drive's (reach_type13_drive.inl, halo4_type13_drive.inl, every build).
// With type13_log = 1 its detour calls this file's observer around the native call (T13ReachBefore / T13ReachAfter,
// T13Halo4Before / T13Halo4After), before the drive writes anything; with 0 the observer is never made ready and is
// never called.
// The observers write NOTHING to the game. The native runs unchanged (through CallNativeWithSpareSlots:
// seven arguments, three of them on the stack; tools/audit_caller_area.py 64828:11:v and aa524:11:v clean,
// 6 slots a violation at entry+0x38); they read under SEH before and after it, and push POD events into a
// lock-free single-producer ring (src/common/type13_flight_log.h); the cold worker formats every line. Only
// one owner thread observes a lane (claimed by compare_exchange, taken over after 500 ms of silence), and a
// busy flag keeps the producer single-threaded even across a takeover. The worker takes the same flag, only
// once a lane has had no call for 500 ms, to end the open stint: its END line and last partial window go out
// then, not at the next flight's first call (or never). They never stamp g_reachPcLiveMs or
// g_reachLastCamCopyMs, so the Reach heartbeat question (Phase A3) stays measurable.
//
// ADDR-HYGIENE: dev-only, resolved (src/common/drive_sites.h: kT13ReachAnchors, kT13Halo4Anchors, ResolveT13).
// No recorded RVA says where anything is. The install FINDS every anchor in the loaded module: the ones that
// locate the entry, the call site or a shared global by signature anywhere in its code, exactly once; the short
// ones that read a field inside the function they belong to. On what was found it proves that the dispatcher's
// type-13 call (rel32) lands on the entry, that the TLS index, tag pages and tag instances globals agree across
// independent sites (the TLS index also with the module's own PE TLS directory), and that the
// caller-attribution frame derived from the dispatcher's prologue is the one its unwind info gives. Every struct
// offset the observer reads stands in an anchor's own displacement bytes, which are exact. Any miss: the
// observer is not made ready, one log line names what failed, and nothing else changes. The offline check
// (tools/drive_address_check.cpp) runs the same tables against the game DLLs: run it on any new game build.

#include "../common/type13_flight_log.h"

namespace
{
constexpr size_t kT13RingSize = 256;
constexpr uint64_t kT13TakeoverMs = 500;

// One title's observer. Install/remove fields belong to the worker; the producer, the ring's producer
// side and the claim to the sim thread that owns the lane; the counters are read by the worker.
struct T13Lane
{
    explicit T13Lane(uint8_t laneTitle) : title(laneTitle), producer(laneTitle) {}
    const uint8_t title;                     // 0 Reach, 1 Halo 4 (the events' title)
    // Install (worker). Read by the observer only while enabled (released after they are set).
    uintptr_t base = 0;
    std::atomic<uint32_t> generation{0};
    uintptr_t ctrlRet = 0;                   // the controller's return address in the dispatcher
    uint32_t dispSlot = 0;                   // the dispatcher's own return address: entry rsp + slot * 8
    t13::CallerTable<4> callers{};
    uintptr_t pages = 0, instances = 0;      // the tag globals (pages: u64[16]; instances: u64 pointer)
    const uint32_t* tlsIndex = nullptr;
    std::atomic<bool> enabled{false};        // proven and ready (the controller's own hook decides whether it runs)
    // Sim thread.
    t13::OwnerClaim owner;
    std::atomic<bool> busy{false};
    std::atomic<bool> resetPending{false};      // a new owner's reset, applied once it holds the busy flag
    std::atomic<uint32_t> previousOwner{0};
    // The last observed call's time (written under the busy flag): the worker's cue to end a stale stint;
    // 0 once it has found nothing open, until the next observed call.
    std::atomic<uint64_t> lastObservedMs{0};
    t13::Producer<kT13RingSize> producer;
    t13::SpscRing<t13::Event, kT13RingSize> ring;
    // Counters (sim thread -> worker, every 2 s).
    std::atomic<uint32_t> calls{0}, ticks{0}, dups{0}, byCaller[5]{}, foreignRet{0}, otherThreads{0},
        otherUsers{0}, busyHits{0}, faults{0}, badTime{0};
    std::atomic<uint32_t> lastVehicle{0}, lastParent{0xFFFFFFFFu};
    std::atomic<int32_t> lastSeat{-1}, master{-1};
    // The latest facing and craft forward (Halo 4 CAM line): a seqlock, one writer (the owner).
    std::atomic<uint32_t> snapSeq{0};
    std::atomic<float> snapFacingYaw{0.0f}, snapFacingPitch{0.0f}, snapCraftYaw{0.0f}, snapCraftPitch{0.0f};
    std::atomic<uint64_t> snapMs{0};
};
T13Lane g_t13Reach{0}, g_t13Halo4{1};

// One controller call's working state (POD: it lives inside the detours' __try).
struct T13Call
{
    t13::Sample s;
    uintptr_t record = 0;          // Reach: player_control (PC); Halo 4: the controller's record (pc+0x120)
    const uint8_t* tls = nullptr;
    const uint8_t* vehicle = nullptr;
    uintptr_t dispRet = 0;
    uint32_t thread = 0;
};

using T13ControllerFn = void (*)(int, uint32_t, float, float, const void*, float, float);

bool T13Executable(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
        return false;
    const DWORD protection = info.Protect & 0xFFu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

// What one title's install found in the loaded module (drive_sites.h: ResolveT13): every row of its table, and
// what is derived from them.
template <size_t N>
struct T13Found
{
    uint32_t rows[N]{};
    drive_sites::T13 t13;
};

// Finds a table in the loaded module. False: the module could not be read at all (resolution.why says why).
template <size_t N>
bool T13Resolve(uintptr_t base, size_t size, GameTitle title, const drive_sites::Anchor (&rows)[N],
                const drive_sites::FlightSites& flight, const drive_sites::T13Sites& sites,
                drive_cascade::Resolution<T13Found<N>>& resolution)
{
    const drive_cascade::Drive drive{title, "pilot-observer"};
    return drive_cascade::Resolve(base, size, drive, resolution, [&](drive_resolve::Resolver& r) {
        T13Found<N> f;
        f.t13 = drive_sites::ResolveT13(r, rows, f.rows, flight, sites);
        return f;
    });
}

// Every row was found, at an address the loaded module executes; the names of the ones that were not go into
// `failed` (comma-separated).
template <size_t N>
bool T13RowsFound(uintptr_t base, const drive_sites::Anchor (&rows)[N], const uint32_t (&found)[N], char* failed,
                  size_t failedSize)
{
    bool all = true;
    size_t used = 0;
    failed[0] = 0;
    for (size_t i = 0; i < N; ++i)
    {
        if (found[i] && T13Executable(base + found[i]))
            continue;
        all = false;
        const int n = snprintf(failed + used, failedSize - used, "%s%s", used ? "," : "", rows[i].name);
        if (n > 0 && used + static_cast<size_t>(n) < failedSize)
            used += static_cast<size_t>(n);
    }
    return all;
}

// The target of a rip-relative operand: the instruction at insnRva, its disp32 at +dispOffset, `length` bytes
// long. 0 unless it lands inside the module. Read only after the anchor covering the instruction was found.
uintptr_t T13Rip(uintptr_t base, size_t size, uint32_t insnRva, uint32_t dispOffset, uint32_t length)
{
    if (static_cast<size_t>(insnRva) + length > size)
        return 0;
    int32_t disp = 0;
    if (!drive_install::Read(base + insnRva + dispOffset, &disp, sizeof(disp)))   // guarded, as every read after the plan
        return 0;
    const uintptr_t target = base + insnRva + length + static_cast<intptr_t>(disp);
    return target >= base && target < base + size ? target : 0;
}

// A shared global, read again from the loaded module at every one of its sites (each inside a found row): all of
// them must name the address the resolution settled on.
template <size_t N, size_t M>
bool T13GlobalAgrees(uintptr_t base, size_t size, const uint32_t (&rows)[N], const drive_sites::RipSite (&sites)[M],
                     uint32_t foundRva)
{
    if (!foundRva)
        return false;
    for (const drive_sites::RipSite& site : sites)
    {
        const uint32_t row = rows[site.row];
        if (!row || T13Rip(base, size, row + site.offset, site.displacement, site.length) != base + foundRva)
            return false;
    }
    return true;
}

// The target of an `E8 rel32` at callRva; 0 when it is not one or lands outside the module.
uintptr_t T13CallTarget(uintptr_t base, size_t size, uint32_t callRva)
{
    if (!callRva || static_cast<size_t>(callRva) + 5 > size)
        return 0;
    uint8_t p[5];
    if (!drive_install::Read(base + callRva, p, sizeof(p)) || p[0] != 0xE8)
        return 0;
    int32_t rel = 0;
    memcpy(&rel, p + 1, sizeof(rel));
    const uintptr_t target = base + callRva + 5 + static_cast<intptr_t>(rel);
    return target >= base && target < base + size ? target : 0;
}

// The caller-attribution slot: the found call site's own function (the loaded module's function table) must
// begin at the found dispatcher, and the slot is derived from that dispatcher's prologue
// (t13::DispatcherReturnSlot).
bool T13DispatcherSlot(uintptr_t base, uint32_t dispatcherRva, size_t prologueBytes, uint32_t callRva, uint32_t& slot)
{
    slot = 0;
    if (!dispatcherRva || !callRva)
        return false;
    DWORD64 imageBase = 0;
    const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(base + callRva, &imageBase, nullptr);
    if (!function || imageBase != base || function->BeginAddress != dispatcherRva)
        return false;
    uint8_t prologue[32];
    if (prologueBytes > sizeof(prologue) || !drive_install::Read(base + dispatcherRva, prologue, prologueBytes))
        return false;
    return t13::DispatcherReturnSlot(prologue, prologueBytes, slot);
}

// Dispatcher callers -> CallerTable (labels only, never a gate): each found call must be an E8 to the found
// dispatcher. `calls` is in the order of the title's caller names.
template <size_t N>
void T13Callers(uintptr_t base, size_t size, uint32_t dispatcherRva, const uint32_t (&calls)[drive_sites::kMaxCallers],
                T13Lane& lane, bool (&ok)[N])
{
    static_assert(N <= 4 && N <= drive_sites::kMaxCallers);
    for (size_t i = 0; i < 4; ++i)
        lane.callers.ret[i] = 0;
    for (size_t i = 0; i < N; ++i)
    {
        ok[i] = dispatcherRva && calls[i] && T13CallTarget(base, size, calls[i]) == base + dispatcherRva;
        lane.callers.ret[i] = ok[i] ? base + calls[i] + 5 : 0;
    }
}

void T13Push(T13Lane& lane, const t13::Event& e) { lane.ring.Push(e); }

// The common front of both detours, after the title's own admission: user, return address, owner, busy.
// True = this call is observed, and the busy flag is held until T13Release.
bool T13Claim(T13Lane& lane, int user, const uintptr_t* entry, T13Call& call)
{
    lane.calls.fetch_add(1, std::memory_order_relaxed);
    if (user != 0)
    {
        lane.otherUsers.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (entry[0] != lane.ctrlRet)   // our own entry stack: the native's return address
    {
        lane.foreignRet.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    call.s.nowMs = GetTickCount64();
    call.thread = GetCurrentThreadId();
    uint32_t previous = 0;
    const t13::Claim claim = lane.owner.Step(call.thread, call.s.nowMs, kT13TakeoverMs, &previous);
    if (claim == t13::Claim::Other)
    {
        lane.otherThreads.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (claim == t13::Claim::NewOwner)
    {
        // Applied below once this thread holds the busy flag: the old owner may still be mid-observation.
        lane.previousOwner.store(previous, std::memory_order_relaxed);
        lane.resetPending.store(true, std::memory_order_release);
    }
    if (lane.busy.exchange(true, std::memory_order_acquire))
    {
        lane.busyHits.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (lane.resetPending.exchange(false, std::memory_order_acq_rel))
    {
        // A new owner starts from nothing: the old owner's stint ends here.
        lane.producer.EndStint(t13::StintEndReason::Owner, lane.ring);
        lane.producer.Reset();
        const uint32_t from = lane.previousOwner.load(std::memory_order_relaxed);
        if (from)
        {
            t13::Event e{};
            e.title = lane.title;
            e.kind = t13::Kind::Owner;
            e.u.owner = {from, call.thread};
            T13Push(lane, e);
        }
    }
    if (lane.producer.SkipFaulted(call.s.vehicle, call.s.nowMs))
    {
        lane.busy.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void T13Release(T13Lane& lane) { lane.busy.store(false, std::memory_order_release); }

// A read faulted: count it, end the stint (it observes nothing more until another one), release.
void T13Fault(T13Lane& lane, const T13Call& call, uint32_t where)
{
    lane.faults.fetch_add(1, std::memory_order_relaxed);
    lane.producer.MarkFaulted(call.s.vehicle, call.s.nowMs, where, lane.ring);
    T13Release(lane);
}

// After both reads: the producer's tick and stint bookkeeping, the stint's one tag read, the events.
template <class ReadTag>
void T13Observe(T13Lane& lane, T13Call& call, ReadTag readTag)
{
    const t13::CallStep step = lane.producer.Begin(call.s, call.thread, lane.ring);
    (step.newTick ? lane.ticks : lane.dups).fetch_add(1, std::memory_order_relaxed);
    lane.byCaller[lane.callers.Index(call.dispRet)].fetch_add(1, std::memory_order_relaxed);
    if (step.needTag)
    {
        t13::Event e{};
        e.title = lane.title;
        e.kind = t13::Kind::Tag;
        e.user = call.s.user;
        e.vehicle = call.s.vehicle;
        e.tick = call.s.tick;
        t13::TagFacts facts;
        bool faulted = false;
        const bool ok = call.vehicle && readTag(call.vehicle, e.u.tag, facts, faulted);
        if (faulted)
        {
            T13Fault(lane, call, 2);
            return;
        }
        e.u.tag.valid = ok ? 1 : 0;
        T13Push(lane, e);
        lane.producer.SetTag(facts);   // asked once per stint, read or not
    }
    lane.producer.Finish(call.s, step, lane.ring);
    lane.lastVehicle.store(call.s.vehicle, std::memory_order_relaxed);
    lane.lastParent.store(call.s.parent, std::memory_order_relaxed);
    lane.lastSeat.store(call.s.seat, std::memory_order_relaxed);
    if (step.newTick)
    {
        lane.snapSeq.fetch_add(1, std::memory_order_acq_rel);
        lane.snapFacingYaw.store(lane.producer.facingYaw, std::memory_order_relaxed);
        lane.snapFacingPitch.store(lane.producer.facingPitch, std::memory_order_relaxed);
        lane.snapCraftYaw.store(lane.producer.craftYaw, std::memory_order_relaxed);
        lane.snapCraftPitch.store(lane.producer.craftPitch, std::memory_order_relaxed);
        lane.snapMs.store(call.s.nowMs, std::memory_order_relaxed);
        lane.snapSeq.fetch_add(1, std::memory_order_release);
    }
    lane.lastObservedMs.store(call.s.nowMs, std::memory_order_relaxed);
    T13Release(lane);
}

// Install (worker; the hook is created but not yet enabled, so nothing else touches the producer): a stint
// left open by the previous install ends here with its last window, then the lane starts from nothing.
void T13ResetForInstall(T13Lane& lane)
{
    lane.owner.Reset();
    lane.busy.store(false, std::memory_order_release);
    lane.producer.EndStint(t13::StintEndReason::Retired, lane.ring);
    lane.producer.Reset();
    lane.lastObservedMs.store(0, std::memory_order_relaxed);
}

t13::Vec3 T13Vec(const uint8_t* p) { return {reinterpret_cast<const float*>(p)[0], reinterpret_cast<const float*>(p)[1], reinterpret_cast<const float*>(p)[2]}; }
template <class T>
T T13Read(const uint8_t* p, size_t offset)
{
    T value{};
    memcpy(&value, p + offset, sizeof(value));
    return value;
}

// The tag chain both controllers compile: instance = instances[index]; def = pages[enc >> 28] + enc * 4.
const uint8_t* T13TagAddress(const T13Lane& lane, uint32_t enc)
{
    if (!enc)
        return nullptr;
    const auto* page = T13Read<const uint8_t*>(reinterpret_cast<const uint8_t*>(lane.pages), (enc >> 28) * 8u);
    return page ? page + static_cast<uint64_t>(enc) * 4u : nullptr;
}
const uint8_t* T13TagDefinition(const T13Lane& lane, uint16_t index)
{
    const auto* instances = T13Read<const uint8_t*>(reinterpret_cast<const uint8_t*>(lane.instances), 0);
    return instances ? T13TagAddress(lane, T13Read<uint32_t>(instances, static_cast<size_t>(index) * 8u + 4u)) : nullptr;
}
// The type accessors' own walk (Reach 0x4AC1E4, Halo 4 0x62DAD0): the first of 15 twelve-byte blocks with a
// nonzero count; 16 when none.
int32_t T13TagType(const uint8_t* def, size_t blocks)
{
    for (int32_t i = 0; i < 15; ++i)
        if (T13Read<int32_t>(def, blocks + static_cast<size_t>(i) * 12u) != 0)
            return i;
    return 16;
}
} // namespace

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
// ============================================================================================================
// REACH: haloreach+0x64828, the Sabre/Seraph pilot attitude controller.
// ============================================================================================================
namespace
{
// ADDR-HYGIENE: dev-only, resolved (drive_sites.h: kT13ReachAnchors, kT13ReachFlight, kT13ReachSites). The install
// below finds every row in the loaded module and reads only what it found. This is where the tables record the
// controller, for the lines written when it was not looked for or was not found. (The whole Reach image is also
// hash-pinned before InstallReachCameraCore reaches this install: the preflight token.)
constexpr uint32_t kT13ReachEntryRecorded = drive_sites::kT13ReachAnchors[drive_sites::kT13ReachFlight.entryRow].rva;
// The dispatcher's prologue: the row the caller-attribution frame is derived from, and how many bytes it holds.
constexpr int kT13ReachDispRow = drive_sites::RowIndex(drive_sites::kT13ReachAnchors, "DISP");
constexpr size_t kT13ReachDispBytes = 25;
static_assert(kT13ReachDispRow >= 0);
// Dispatcher callers (labels, named by their return addresses in the recorded build): the action/kit consumer
// 0x5DF80, then 0x61A38's two calls ([I] the second is the no-unit path, the analogue of Halo 4's 0xA7A01). In
// the order the resolution gives them (kT13ReachFlight.order).
constexpr const char* kT13ReachCallerNames[4] = {"alt+5EE48", "main+6246C", "nounit?+628A8", "-"};
static_assert(drive_sites::kT13ReachFlight.order[0] == aim_caller::Caller::Impulse &&
              drive_sites::kT13ReachFlight.order[1] == aim_caller::Caller::Stick &&
              drive_sites::kT13ReachFlight.order[2] == aim_caller::Caller::NoUnit);

bool T13ReachAdmitted()
{
    const auto& lane = g_t13Reach;
    return g_reachT13Enabled.load(std::memory_order_acquire) && lane.enabled.load(std::memory_order_acquire) &&
           g_config.type13_log && !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
           g_reachCamera.generation.load(std::memory_order_acquire) == lane.generation.load(std::memory_order_acquire);
}

// Before the native: the record the controller is about to update, its flags, the roll axis, the tick.
bool T13ReachReadPre(int user, const void* input, const uintptr_t* entry, T13Call& call, bool& faulted)
{
    faulted = false;
    __try
    {
        const auto& lane = g_t13Reach;
        auto& s = call.s;
        call.dispRet = lane.dispSlot ? entry[lane.dispSlot] : 0;
        auto** slots = reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
        const uint32_t index = *lane.tlsIndex;
        const uint8_t* tls = (slots && index < 0x200) ? slots[index] : nullptr;
        if (!tls)
            return false;
        const uintptr_t globals = T13Read<uintptr_t>(tls, 0xD8);   // REC
        if (!globals)
            return false;
        call.tls = tls;
        call.record = globals + static_cast<uintptr_t>(user) * 0x180;
        const auto* pc = reinterpret_cast<const uint8_t*>(call.record);
        for (int i = 0; i < 3; ++i)
            s.pre[i] = T13Read<float>(pc, 0x94 + 4u * i);          // ANGPTR / EXIT / WRAPP / WRAPR
        s.rollMode = ((T13Read<uint8_t>(pc, 0x88) >> 2) & 1u) != 0; // PC88
        s.moveX = input ? T13Read<float>(static_cast<const uint8_t*>(input), 4) : 0.0f;   // ROLLIN
        const auto* time = T13Read<const uint8_t*>(tls, 0xA0);      // WIN
        if (!time)
            return false;
        s.tick = T13Read<int32_t>(time, 0xC);
        s.spt = T13Read<float>(time, 8);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}

// After the native: what it wrote, its timers, and the vehicle fields the hull, the camera and the forced
// steer read.
bool T13ReachReadPost(T13Call& call, bool& faulted)
{
    faulted = false;
    __try
    {
        auto& s = call.s;
        const auto* pc = reinterpret_cast<const uint8_t*>(call.record);
        for (int i = 0; i < 3; ++i)
            s.post[i] = T13Read<float>(pc, 0x94 + 4u * i);
        s.idle = T13Read<float>(pc, 0x140);       // IDLE
        s.rollRate = T13Read<float>(pc, 0x144);   // RRATE
        s.hold = T13Read<float>(pc, 0x148);       // HOLD
        s.latch = T13Read<uint8_t>(pc, 0x14C);    // LATCH
        uint8_t kind = 0;
        const unsigned char* veh = ReachVehicleObjectData(static_cast<int32_t>(s.vehicle), kind);
        if (veh && kind == kReachObjectKindVehicle)
        {
            call.vehicle = veh;
            s.haveVehicle = true;
            s.facing = T13Vec(veh + 0x1E4);              // HX
            s.rollField = T13Read<float>(veh, 0x250);   // HX
            s.craftFwd = T13Vec(veh + 0x50);             // HFWD ([I] the nose)
            s.aim = T13Vec(veh + 0x214);                 // AIM
            s.stamp = T13Read<int32_t>(veh, 0xABC);     // STAMP
            s.stampDir = T13Vec(veh + 0xAC0);            // STAMP
            const int16_t off = T13Read<int16_t>(veh, 0x1AE);   // STATE
            s.stateOff = off;
            s.state = (off > 0 && off < 0x2000) ? T13Read<int32_t>(veh, static_cast<size_t>(off)) : t13::kUnread;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}

// The stint's tag: the type-13 block (sf) the controller and the hull read, and the vehicle's hull scale.
bool T13ReachReadTag(const uint8_t* veh, t13::TagData& d, t13::TagFacts& f, bool& faulted)
{
    faulted = false;
    __try
    {
        const auto& lane = g_t13Reach;
        d.def = T13Read<uint16_t>(veh, 0);                 // TAGDEF
        const uint8_t* def = T13TagDefinition(lane, d.def);
        if (!def)
            return false;
        d.tagType = T13TagType(def, 0x4D0);                // TYPEACC
        d.n13 = T13Read<int32_t>(def, 0x4D0 + 13 * 12);    // the type-13 block's count, def+0x56C
        const uint8_t* sf = d.n13 >= 1 ? T13TagAddress(lane, T13Read<uint32_t>(def, 0x570)) : nullptr;   // SF
        if (!sf)
            return false;
        static constexpr uint32_t kControl[11]{0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84};
        for (int i = 0; i < 11; ++i)
            d.ctl[i] = T13Read<float>(sf, kControl[i]);
        d.projWord = T13Read<uint32_t>(sf, 0xA8);         // SFA8
        d.window = T13Read<float>(sf, 0xAC);              // WIN
        static constexpr uint32_t kHull[6]{0x10, 0x14, 0x38, 0x88, 0x8C, 0x90};
        for (int i = 0; i < 6; ++i)
            d.phys[i] = T13Read<float>(sf, kHull[i]);
        d.vehScale = T13Read<float>(veh, 0x3A0);          // H38
        f.valid = true;
        f.window = d.window;
        f.projBit = static_cast<int32_t>(d.projWord & 1u);
        f.tagType = d.tagType;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}

// The pre-call half. True = observe; the busy flag is then held.
bool T13ReachBefore(int user, uint32_t vehicle, float yawD, float pitchD, const void* input, float dtA, float dtB,
                    const uintptr_t* entry, T13Call& call)
{
    auto& lane = g_t13Reach;
    if (!T13ReachAdmitted())
        return false;
    call.s.vehicle = vehicle;
    call.s.user = static_cast<uint16_t>(user);
    if (!T13Claim(lane, user, entry, call))
        return false;
    call.s.yawD = yawD;
    call.s.pitchD = pitchD;
    call.s.dtA = dtA;
    call.s.dtB = dtB;
    bool faulted = false;
    if (!T13ReachReadPre(user, input, entry, call, faulted))
    {
        if (faulted)
            T13Fault(lane, call, 0);
        else
            T13Release(lane);
        return false;
    }
    if (!(call.s.spt > 0.001f && call.s.spt < 0.2f))
    {
        lane.badTime.fetch_add(1, std::memory_order_relaxed);
        T13Release(lane);
        return false;
    }
    lane.master.store(call.record == g_reachPcExpected.load(std::memory_order_acquire) ? 1 : 0,
                      std::memory_order_relaxed);
    return true;
}

void T13ReachAfter(T13Call& call)
{
    auto& lane = g_t13Reach;
    bool faulted = false;
    if (!T13ReachReadPost(call, faulted))
    {
        if (faulted)
            T13Fault(lane, call, 1);
        else
            T13Release(lane);
        return;
    }
    T13Observe(lane, call, T13ReachReadTag);
}
} // namespace

// The observer is called from the controller's one detour, ReachType13Detour (reach_type13_drive.inl): Before, the
// native, After. Each read has its own __except.

// Worker, inside InstallReachCameraCore (whole-image-hashed module, g_reachCamera.base/generation set), BEFORE the
// controller's hook is installed: the observer is proven and made ready here, and creates no hook of its own.
bool ReachType13LogInstall(uintptr_t base, size_t size, uint32_t generation)
{
    auto& lane = g_t13Reach;
    if (!g_config.type13_log)
    {
        lane.enabled.store(false, std::memory_order_release);
        LOG("REACH SF: type13_log 0: the type-13 pilot controller (recorded at +0x%X) is not observed",
            kT13ReachEntryRecorded);
        return false;
    }
    if (g_reachT13Target)
    {
        // The controller's hook outlived a cleanup and may be running: the observer keeps what it proved on that
        // module, and nothing of it is rebuilt under a live detour.
        if (lane.base == base && lane.tlsIndex)
        {
            lane.generation.store(generation, std::memory_order_release);
            return lane.enabled.load(std::memory_order_acquire);
        }
        lane.enabled.store(false, std::memory_order_release);
        LOG("REACH SF: the controller's hook is still installed on another module; the observer stays off");
        return false;
    }
    // No hook exists, so no detour can be observing: off until this install has proven it again.
    lane.enabled.store(false, std::memory_order_release);
    const uint64_t started = GetTickCount64();
    // WHERE EVERYTHING IS IN THIS MODULE (drive_sites.h, by the rungs of drive_known.h): verified at the addresses
    // it is known to stand at, or found by search; every proof is made on those addresses either way. Nothing
    // below trusts a recorded RVA.
    drive_cascade::Resolution<T13Found<drive_sites::kT13ReachRows>> resolution;
    const bool opened = T13Resolve(base, size, GameTitle::HaloReach, drive_sites::kT13ReachAnchors,
                                   drive_sites::kT13ReachFlight, drive_sites::kT13ReachSites, resolution);
    const drive_sites::T13& found = resolution.found.t13;
    const uint32_t (&rows)[drive_sites::kT13ReachRows] = resolution.found.rows;
    if (opened)
        drive_cascade::Announce(drive_cascade::kReachObserverName, resolution);
    char failed[512] = "";
    const bool anchorsOk = opened && T13RowsFound(base, drive_sites::kT13ReachAnchors, rows, failed, sizeof(failed));
    // The dispatcher's type-13 call lands on the found entry: read again from the loaded module.
    const bool callOk = anchorsOk && found.flight.entry &&
                        T13CallTarget(base, size, found.flight.call) == base + found.flight.entry;
    // The shared globals, each from independent sites and read again from the loaded module: TLS index (ENTRY,
    // HPAGES, the module's own PE TLS directory, and the RVA this lane's readers use), tag pages (ENTRY, HPAGES,
    // TYPEACC), tag instances (TAGDEF, HDEF, TYPEACC).
    const bool tlsOk = anchorsOk && found.tlsIndex == kReachEngineTlsIndexRva &&
                       T13GlobalAgrees(base, size, rows, drive_sites::kT13ReachSites.tlsIndex, found.tlsIndex);
    const bool pagesOk = anchorsOk && T13GlobalAgrees(base, size, rows, drive_sites::kT13ReachSites.pages, found.pages);
    const bool instOk = anchorsOk &&
                        T13GlobalAgrees(base, size, rows, drive_sites::kT13ReachSites.instances, found.instances);
    if (!anchorsOk || !callOk || !tlsOk || !pagesOk || !instOk)
    {
        char first[280] = "";
        if (opened)
            drive_cascade::FirstFailure(resolution.report, first, sizeof(first));
        LOG("REACH SF: type-13 pilot controller (recorded at +0x%X) NOT observed (%s; anchors failed: %s; dispatcher "
            "call=%d tls=%d pages=%d instances=%d); the log-only lane is off",
            kT13ReachEntryRecorded, opened ? first : (resolution.why ? resolution.why : "the module did not open"),
            failed[0] ? failed : "none", callOk, tlsOk, pagesOk, instOk);
        return false;
    }
    bool callerOk[3]{};
    T13Callers(base, size, found.flight.dispatcher, found.flight.callerCall, lane, callerOk);
    // The dispatcher's own return address, for caller attribution: derived from the prologue the DISP row holds,
    // which must be what the found dispatcher begins with, and equal to what its unwind info gives (the resolution).
    uint32_t slot = 0;
    const bool frameOk = rows[kT13ReachDispRow] == found.flight.dispatcher &&
                         T13DispatcherSlot(base, found.flight.dispatcher, kT13ReachDispBytes, found.flight.call, slot) &&
                         slot * 8u == found.flight.frame;
    // Everything the observer reads is set before it can be admitted; a fresh owner, producer and counters.
    lane.base = base;
    lane.ctrlRet = base + found.flight.call + 5;
    lane.dispSlot = frameOk ? slot : 0;
    lane.pages = base + found.pages;
    lane.instances = base + found.instances;
    lane.tlsIndex = reinterpret_cast<const uint32_t*>(base + found.tlsIndex);
    lane.generation.store(generation, std::memory_order_release);
    T13ResetForInstall(lane);
    lane.enabled.store(true, std::memory_order_release);
    LOG("REACH SF: type-13 pilot controller +0x%X observed LOG-ONLY, proven in %llu ms (the observer writes "
        "nothing; the hook is the direct drive's; type13_log %d); dispatcher return at entry rsp+0x%X %s; caller "
        "labels alt %d main %d nounit? %d",
        found.flight.entry, static_cast<unsigned long long>(GetTickCount64() - started), g_config.type13_log,
        lane.dispSlot * 8u, frameOk ? "(derived from the dispatcher prologue)" : "UNAVAILABLE (caller attribution off)",
        callerOk[0], callerOk[1], callerOk[2]);
    return true;
}
#endif // HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
// ============================================================================================================
// HALO 4: halo4+0xAA524, the Broadsword pilot attitude controller.
// ============================================================================================================
namespace
{
// ADDR-HYGIENE: dev-only, resolved (drive_sites.h: kT13Halo4Anchors, kT13Halo4Flight, kT13Halo4Sites). The install
// below finds every row in the loaded module and reads only what it found. This is where the tables record the
// controller, for the lines written when it was not looked for or was not found. (Halo 4's module is identified by
// PE timestamp, size and the camera anchors, not by a whole-image hash, so every offset read here has its own row.)
constexpr uint32_t kT13Halo4EntryRecorded = drive_sites::kT13Halo4Anchors[drive_sites::kT13Halo4Flight.entryRow].rva;
// The dispatcher's prologue: the row the caller-attribution frame is derived from, and how many bytes it holds.
constexpr int kT13Halo4DispRow = drive_sites::RowIndex(drive_sites::kT13Halo4Anchors, "DISP");
constexpr size_t kT13Halo4DispBytes = 26;
static_assert(kT13Halo4DispRow >= 0);
// Dispatcher callers (labels, named by their return addresses in the recorded build): the action/kit consumer
// 0xA2670; 0xA6BA8's aim-SWAY call (0xA5B90 computes sinf of tag-driven phases; not recoil), its main stick update
// and its no-unit path. In the order the resolution gives them (kT13Halo4Flight.order).
constexpr const char* kT13Halo4CallerNames[4] = {"alt+A34A3", "sway+A7756", "main+A7779", "nounit+A7A06"};
static_assert(drive_sites::kT13Halo4Flight.order[0] == aim_caller::Caller::Impulse &&
              drive_sites::kT13Halo4Flight.order[1] == aim_caller::Caller::Sway &&
              drive_sites::kT13Halo4Flight.order[2] == aim_caller::Caller::Stick &&
              drive_sites::kT13Halo4Flight.order[3] == aim_caller::Caller::NoUnit);

bool T13Halo4Admitted()
{
    const auto& lane = g_t13Halo4;
    return Halo4Type13HookEnabled() && lane.enabled.load(std::memory_order_acquire) && g_config.type13_log &&
           !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
           g_halo4Camera.generation.load(std::memory_order_acquire) == lane.generation.load(std::memory_order_acquire) &&
           TitleAdapter_GetActiveTitle() == GameTitle::Halo4;
}

bool T13Halo4ReadPre(int user, const void* input, const uintptr_t* entry, T13Call& call, bool& faulted)
{
    faulted = false;
    __try
    {
        const auto& lane = g_t13Halo4;
        auto& s = call.s;
        call.dispRet = lane.dispSlot ? entry[lane.dispSlot] : 0;
        auto** slots = reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
        const uint32_t index = *lane.tlsIndex;
        const uint8_t* tls = (slots && index < 0x200) ? slots[index] : nullptr;
        if (!tls)
            return false;
        const uintptr_t globals = T13Read<uintptr_t>(tls, 0xB0);   // TLSIDX / RECSETUP
        if (!globals)
            return false;
        call.tls = tls;
        call.record = globals + 0x120 + static_cast<uintptr_t>(user) * 0x1F0;
        const auto* rec = reinterpret_cast<const uint8_t*>(call.record);
        for (int i = 0; i < 3; ++i)
            s.pre[i] = T13Read<float>(rec, 0x18 + 4u * i);          // ANGPTR / YAWWRAP
        s.rollMode = ((T13Read<uint8_t>(rec, 0x08) >> 2) & 1u) != 0; // PC08
        s.moveX = input ? T13Read<float>(static_cast<const uint8_t*>(input), 4) : 0.0f;   // ROLLIN
        const auto* time = T13Read<const uint8_t*>(tls, 0x90);       // WIN
        if (!time)
            return false;
        s.tick = T13Read<int32_t>(time, 0xC);
        s.spt = T13Read<float>(time, 8);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}

bool T13Halo4ReadPost(T13Call& call, bool& faulted)
{
    faulted = false;
    __try
    {
        auto& s = call.s;
        const auto* rec = reinterpret_cast<const uint8_t*>(call.record);
        for (int i = 0; i < 3; ++i)
            s.post[i] = T13Read<float>(rec, 0x18 + 4u * i);
        s.idle = T13Read<float>(rec, 0x118);       // IDLE
        s.rollRate = T13Read<float>(rec, 0x11C);   // RRATE
        s.hold = T13Read<float>(rec, 0x120);       // HOLD
        s.latch = T13Read<uint8_t>(rec, 0x124);    // LATCH
        const auto* table = T13Read<const uint8_t*>(call.tls, 0x18);   // VEH
        const uint8_t* veh = Halo4VehicleObject(table, s.vehicle, 0x2);
        if (veh)
        {
            call.vehicle = veh;
            s.haveVehicle = true;
            s.facing = T13Vec(veh + 0x4BC);                 // PX
            s.rollField = T13Read<float>(veh, 0x528);      // PX
            s.craftFwd = T13Vec(veh + 0x70);                // PFWD ([I] the nose)
            s.stamp = T13Read<int32_t>(veh, 0x104C);       // REDIR / REDIRW
            s.stampDir = T13Vec(veh + 0x1050);              // REDIR
            s.redirectFlag = T13Read<uint8_t>(veh, 0x105C);   // REDIRW
            s.gateBit = static_cast<int32_t>((T13Read<uint32_t>(veh, 0x16C) >> 2) & 1u);   // GATE16C
            s.overrideSet = T13Read<uint8_t>(veh, 0xF58);  // TYPEOVR
            s.overrideType = T13Read<int8_t>(veh, 0xF59);  // TYPEOVR
        }
        s.unit = T13Read<uint32_t>(rec, 0);                 // RECUNIT
        const uint8_t* unit = Halo4VehicleObject(table, s.unit, 0x1);
        if (unit)
        {
            s.parent = T13Read<uint32_t>(unit, 0x24);       // Halo4ReadVehicleInputMemory's own reads
            s.seat = T13Read<int16_t>(unit, 0x2C);
            s.haveUnitFacing = true;
            s.unitFacing = T13Vec(unit + 0x4BC);            // UNIT4BC
            const uint16_t off = T13Read<uint16_t>(unit, 0x47A);   // PHYSMODE
            s.physOff = off;
            s.physMode = (off >= 0x10 && off < 0x2000) ? T13Read<int32_t>(unit, off) : t13::kUnread;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}

bool T13Halo4ReadTag(const uint8_t* veh, t13::TagData& d, t13::TagFacts& f, bool& faulted)
{
    faulted = false;
    __try
    {
        const auto& lane = g_t13Halo4;
        d.def = T13Read<uint16_t>(veh, 8);                 // TAGDEF
        const uint8_t* def = T13TagDefinition(lane, d.def);
        if (!def)
            return false;
        d.tagType = T13TagType(def, 0x5D0);                // TAGTYPE (lea r8,[rdx+0x174])
        d.n13 = T13Read<int32_t>(def, 0x5D0 + 13 * 12);    // the type-13 block's count, def+0x66C
        const uint8_t* t = d.n13 >= 1 ? T13TagAddress(lane, T13Read<uint32_t>(def, 0x670)) : nullptr;   // T
        if (!t)
            return false;
        static constexpr uint32_t kControl[11]{0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84, 0x88};
        for (int i = 0; i < 11; ++i)
            d.ctl[i] = T13Read<float>(t, kControl[i]);
        d.projWord = T13Read<uint32_t>(t, 0xC8);          // TC8
        d.window = T13Read<float>(t, 0xCC);               // WIN
        static constexpr uint32_t kPhysics[4]{0x10, 0x14, 0x3C, 0xA8};
        for (int i = 0; i < 4; ++i)
            d.phys[i] = T13Read<float>(t, kPhysics[i]);
        d.phys[4] = d.phys[5] = std::numeric_limits<float>::quiet_NaN();
        d.physWord = T13Read<uint32_t>(t, 0xA4);          // PA4
        d.vehScale = T13Read<float>(veh, 0x6A4);          // P3C
        f.valid = true;
        f.window = d.window;
        f.projBit = static_cast<int32_t>(d.projWord & 1u);
        f.tagType = d.tagType;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}

bool T13Halo4Before(int user, uint32_t vehicle, float yawD, float pitchD, const void* input, float dtA, float dtB,
                    const uintptr_t* entry, T13Call& call)
{
    auto& lane = g_t13Halo4;
    if (!T13Halo4Admitted())
        return false;
    call.s.vehicle = vehicle;
    call.s.user = static_cast<uint16_t>(user);
    if (!T13Claim(lane, user, entry, call))
        return false;
    call.s.yawD = yawD;
    call.s.pitchD = pitchD;
    call.s.dtA = dtA;
    call.s.dtB = dtB;
    bool faulted = false;
    if (!T13Halo4ReadPre(user, input, entry, call, faulted))
    {
        if (faulted)
            T13Fault(lane, call, 0);
        else
            T13Release(lane);
        return false;
    }
    if (!(call.s.spt > 0.001f && call.s.spt < 0.2f))
    {
        lane.badTime.fetch_add(1, std::memory_order_relaxed);
        T13Release(lane);
        return false;
    }
    // The legacy-drive record is this record - 0x120 (its angles at +0x138 are this record's +0x18).
    lane.master.store(call.record - 0x120 == g_legacyDrive[2].expected.load(std::memory_order_acquire) ? 1 : 0,
                      std::memory_order_relaxed);
    return true;
}

void T13Halo4After(T13Call& call)
{
    auto& lane = g_t13Halo4;
    bool faulted = false;
    if (!T13Halo4ReadPost(call, faulted))
    {
        if (faulted)
            T13Fault(lane, call, 1);
        else
            T13Release(lane);
        return;
    }
    T13Observe(lane, call, T13Halo4ReadTag);
}

// The observer is called from the controller's one detour, Halo4Type13Detour (halo4_type13_drive.inl): Before, the
// native, After. Each read has its own __except.
} // namespace

// Worker, inside InstallHalo4CameraCore after InstallHalo4VehicleInput (the module size, cold observation and
// g_halo4EngineTlsIndex are established), BEFORE the controller's hook is installed: the observer is proven and
// made ready here, and creates no hook of its own.
bool Halo4Type13LogInstall(uintptr_t base, size_t size, uint32_t generation)
{
    auto& lane = g_t13Halo4;
    if (!g_config.type13_log)
    {
        lane.enabled.store(false, std::memory_order_release);
        LOG("H4 T13: type13_log 0: the type-13 pilot controller (recorded at +0x%X) is not observed",
            kT13Halo4EntryRecorded);
        return false;
    }
    if (Halo4Type13HookEnabled())
    {
        // The controller's hook is live: the observer keeps what it proved on that module, and nothing of it is
        // rebuilt under a running detour.
        if (lane.base == base && lane.tlsIndex)
        {
            lane.generation.store(generation, std::memory_order_release);
            return lane.enabled.load(std::memory_order_acquire);
        }
        lane.enabled.store(false, std::memory_order_release);
        LOG("H4 T13: the controller's hook is live on another module; the observer stays off");
        return false;
    }
    // The hook admits nothing, so no detour can be observing: off until this install has proven it again.
    lane.enabled.store(false, std::memory_order_release);
    if (!g_halo4EngineTlsIndex)
    {
        LOG("H4 T13: type-13 pilot controller (recorded at +0x%X) NOT observed (the camera core's TLS index is "
            "unresolved); the log-only lane is off", kT13Halo4EntryRecorded);
        return false;
    }
    const uint64_t started = GetTickCount64();
    // WHERE EVERYTHING IS IN THIS MODULE (drive_sites.h, by the rungs of drive_known.h): verified at the addresses
    // it is known to stand at, or found by search; every proof is made on those addresses either way. Nothing
    // below trusts a recorded RVA.
    drive_cascade::Resolution<T13Found<drive_sites::kT13Halo4Rows>> resolution;
    const bool opened = T13Resolve(base, size, GameTitle::Halo4, drive_sites::kT13Halo4Anchors,
                                   drive_sites::kT13Halo4Flight, drive_sites::kT13Halo4Sites, resolution);
    const drive_sites::T13& found = resolution.found.t13;
    const uint32_t (&rows)[drive_sites::kT13Halo4Rows] = resolution.found.rows;
    if (opened)
        drive_cascade::Announce(drive_cascade::kHalo4ObserverName, resolution);
    char failed[512] = "";
    const bool anchorsOk = opened && T13RowsFound(base, drive_sites::kT13Halo4Anchors, rows, failed, sizeof(failed));
    // The dispatcher's type-13 call lands on the found entry: read again from the loaded module.
    const bool callOk = anchorsOk && found.flight.entry &&
                        T13CallTarget(base, size, found.flight.call) == base + found.flight.entry;
    // The shared globals, each from independent sites and read again from the loaded module: TLS index (TLSIDX,
    // PPAGES, the module's own PE TLS directory, the camera core's resolution and the RVA this lane's readers use),
    // pages (ENTRY, TAGTYPE, PPAGES), instances (TAGDEF, TAGTYPE, PPAGES).
    const bool tlsOk = anchorsOk && found.tlsIndex == kHalo4EngineTlsIndexRva &&
                       base + found.tlsIndex == reinterpret_cast<uintptr_t>(g_halo4EngineTlsIndex) &&
                       T13GlobalAgrees(base, size, rows, drive_sites::kT13Halo4Sites.tlsIndex, found.tlsIndex);
    const bool pagesOk = anchorsOk && T13GlobalAgrees(base, size, rows, drive_sites::kT13Halo4Sites.pages, found.pages);
    const bool instOk = anchorsOk &&
                        T13GlobalAgrees(base, size, rows, drive_sites::kT13Halo4Sites.instances, found.instances);
    if (!anchorsOk || !callOk || !tlsOk || !pagesOk || !instOk)
    {
        char first[280] = "";
        if (opened)
            drive_cascade::FirstFailure(resolution.report, first, sizeof(first));
        LOG("H4 T13: type-13 pilot controller (recorded at +0x%X) NOT observed (%s; anchors failed: %s; dispatcher "
            "call=%d tls=%d pages=%d instances=%d); the log-only lane is off",
            kT13Halo4EntryRecorded, opened ? first : (resolution.why ? resolution.why : "the module did not open"),
            failed[0] ? failed : "none", callOk, tlsOk, pagesOk, instOk);
        return false;
    }
    bool callerOk[4]{};
    T13Callers(base, size, found.flight.dispatcher, found.flight.callerCall, lane, callerOk);
    // The dispatcher's own return address, for caller attribution: derived from the prologue the DISP row holds,
    // which must be what the found dispatcher begins with, and equal to what its unwind info gives (the resolution).
    uint32_t slot = 0;
    const bool frameOk = rows[kT13Halo4DispRow] == found.flight.dispatcher &&
                         T13DispatcherSlot(base, found.flight.dispatcher, kT13Halo4DispBytes, found.flight.call, slot) &&
                         slot * 8u == found.flight.frame;
    // Everything the observer reads is set before it can be admitted; a fresh owner, producer and counters.
    lane.base = base;
    lane.ctrlRet = base + found.flight.call + 5;
    lane.dispSlot = frameOk ? slot : 0;
    lane.pages = base + found.pages;
    lane.instances = base + found.instances;
    lane.tlsIndex = g_halo4EngineTlsIndex;
    lane.generation.store(generation, std::memory_order_release);
    T13ResetForInstall(lane);
    lane.enabled.store(true, std::memory_order_release);
    LOG("H4 T13: type-13 pilot controller +0x%X observed LOG-ONLY, proven in %llu ms (the observer writes nothing; "
        "the hook is the direct drive's; type13_log %d); dispatcher return at entry rsp+0x%X %s; caller labels alt "
        "%d sway %d main %d nounit %d",
        found.flight.entry, static_cast<unsigned long long>(GetTickCount64() - started), g_config.type13_log,
        lane.dispSlot * 8u, frameOk ? "(derived from the dispatcher prologue)" : "UNAVAILABLE (caller attribution off)",
        callerOk[0], callerOk[1], callerOk[2], callerOk[3]);
    return true;
}

#endif // HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA

// ============================================================================================================
// THE COLD WORKER: drains both rings every loop (~50 ms) and formats every line; counters every 2 s.
// ============================================================================================================
namespace
{
const char* T13Prefix(uint8_t title) { return title == 0 ? "REACH SF" : "H4 T13"; }
float T13Deg(float rad) { return rad * t13::kRadToDeg; }
const char* T13EndReason(uint8_t reason)
{
    switch (static_cast<t13::StintEndReason>(reason))
    {
    case t13::StintEndReason::Vehicle: return "another vehicle";
    case t13::StintEndReason::Discontinuity: return "the tick went back or jumped: a load or revert";
    case t13::StintEndReason::Gap: return "no call for more than 500 ms";
    case t13::StintEndReason::Owner: return "another thread took the lane over";
    case t13::StintEndReason::Fault: return "a read faulted";
    case t13::StintEndReason::Retired: return "the hook was removed and reinstalled";
    }
    return "?";
}

void T13Format(const t13::Event& e)
{
    const char* p = T13Prefix(e.title);
    const bool reach = e.title == 0;
    switch (e.kind)
    {
    case t13::Kind::StintBegin:
        LOG("%s stint BEGIN veh 0x%08X user %u tick %d thread %u", p, e.vehicle, e.user, e.tick, e.u.stint.thread);
        break;
    case t13::Kind::StintEnd:
        LOG("%s stint END veh 0x%08X: %u ticks, %u calls (%s)", p, e.vehicle, e.u.stint.ticks, e.u.stint.calls,
            T13EndReason(e.u.stint.reason));
        break;
    case t13::Kind::Link:
    {
        const auto& d = e.u.link;
        if (reach)
            LOG("REACH SF LINK t=%d pc(y %.4f p %.4f r %.4f) facing(+0x1E4)(y %.4f p %.4f) err %.3f deg errPrev %.3f "
                "deg veh250 %.4f dRoll %.3f deg; hullFwd(+0x50)-facing %.2f deg",
                e.tick, d.post[0], d.post[1], d.post[2], d.facingYaw, d.facingPitch, T13Deg(d.err), T13Deg(d.errPrev),
                d.rollField, T13Deg(d.dRoll), T13Deg(d.craftErr));
        else
            LOG("H4 T13 LINK t=%d rec(y %.4f p %.4f r %.4f) facing(+0x4BC)(y %.4f p %.4f) err %.3f deg errPrev %.3f "
                "deg roll528 %.4f dRoll %.3f deg unit4BC err %.3f deg gate16Cbit2 %d; craftFwd(+0x70)-facing %.2f deg",
                e.tick, d.post[0], d.post[1], d.post[2], d.facingYaw, d.facingPitch, T13Deg(d.err), T13Deg(d.errPrev),
                d.rollField, T13Deg(d.dRoll), T13Deg(d.unitErr), d.gateBit, T13Deg(d.craftErr));
        break;
    }
    case t13::Kind::Window:
    {
        const auto& d = e.u.window;
        if (reach)
            LOG("REACH SF LINK 2s n %u err max %.3f mean %.3f errPrev max %.3f mean %.3f deg |dRoll| max %.3f deg; "
                "hullFwd-facing max %.2f mean %.2f deg",
                d.linkN, T13Deg(d.errMax), T13Deg(d.errMean), T13Deg(d.errPrevMax), T13Deg(d.errPrevMean),
                T13Deg(d.dRollMax), T13Deg(d.craftMax), T13Deg(d.craftMean));
        else
            LOG("H4 T13 LINK 2s n %u err max %.3f mean %.3f errPrev max %.3f mean %.3f deg |dRoll| max %.3f deg "
                "unit4BC err max %.3f deg; craftFwd-facing max %.2f mean %.2f deg",
                d.linkN, T13Deg(d.errMax), T13Deg(d.errMean), T13Deg(d.errPrevMax), T13Deg(d.errPrevMean),
                T13Deg(d.dRollMax), T13Deg(d.unitErrMax), T13Deg(d.craftMax), T13Deg(d.craftMean));
        LOG("%s RATE %.1fs maxTurn %.1f maxYaw %.1f maxPitch %.1f deg/s; stick args maxYaw %.1f maxPitch %.1f deg/s "
            "(|delta|/dtA); ticks %u excluded %u (stint start, %s window ticks %u); change lines held back %u",
            p, d.seconds, T13Deg(d.maxTurn), T13Deg(d.maxYaw), T13Deg(d.maxPitch), T13Deg(d.maxArgYaw),
            T13Deg(d.maxArgPitch), d.rateTicks, d.rateExcluded, reach ? "forced-steer" : "redirect", d.windowTicks,
            d.heldBack);
        break;
    }
    case t13::Kind::Tag:
    {
        const auto& d = e.u.tag;
        if (!d.valid)
        {
            LOG("%s TAG veh 0x%08X: the tag chain did not resolve (def 0x%04X n13 %d); window unknown, so forced-steer "
                "or redirect windows are not marked this stint", p, e.vehicle, d.def, d.n13);
            break;
        }
        if (reach)
            LOG("REACH SF TAG def 0x%04X n13 %d tagType %d | 5C %g 60 %g 64 %g 68 %g 6C %g 70 %g 74 %g 78 %g 7C %g "
                "80 %g 84 %g | A8 0x%08X (bit0 %u) AC %g | hull 10 %g 14 %g 38 %g 88 %g 8C %g 90 %g | veh3A0 %g",
                d.def, d.n13, d.tagType, d.ctl[0], d.ctl[1], d.ctl[2], d.ctl[3], d.ctl[4], d.ctl[5], d.ctl[6], d.ctl[7],
                d.ctl[8], d.ctl[9], d.ctl[10], d.projWord, d.projWord & 1u, d.window, d.phys[0], d.phys[1], d.phys[2],
                d.phys[3], d.phys[4], d.phys[5], d.vehScale);
        else
            LOG("H4 T13 TAG def 0x%04X n13 %d tagType %d | 60 %g 64 %g 68 %g 6C %g 70 %g 74 %g 78 %g 7C %g 80 %g "
                "84 %g 88 %g | C8 0x%08X (bit0 %u) CC %g | phys 10 %g 14 %g 3C %g A8 %g A4 0x%08X (bit0 %u) | "
                "veh6A4 %g",
                d.def, d.n13, d.tagType, d.ctl[0], d.ctl[1], d.ctl[2], d.ctl[3], d.ctl[4], d.ctl[5], d.ctl[6], d.ctl[7],
                d.ctl[8], d.ctl[9], d.ctl[10], d.projWord, d.projWord & 1u, d.window, d.phys[0], d.phys[1], d.phys[2],
                d.phys[3], d.physWord, d.physWord & 1u, d.vehScale);
        break;
    }
    case t13::Kind::Stamp:
    {
        const auto& d = e.u.stampEv;
        char was[16];
        if (d.was == t13::kUnread)
            snprintf(was, sizeof(was), "stint start");
        else
            snprintf(was, sizeof(was), "%d", d.was);
        if (reach)
            LOG("REACH SF FORCED t=%d stamp(+0xABC) %d (was %s) age %.3f s window(sf+0xAC) %.3f open %u "
                "dir(+0xAC0)(%.3f %.3f %.3f) projBit(sf+0xA8) %d aim214(%.3f %.3f %.3f) nativeJump %.2f deg",
                e.tick, d.stamp, was, d.age, d.window, d.open, d.dir.x, d.dir.y, d.dir.z, d.projBit, d.aim.x, d.aim.y,
                d.aim.z, T13Deg(d.nativeJump));
        else
            LOG("H4 T13 REDIRECT t=%d stamp(+0x104C) %d (was %s) age %.3f s window(T+0xCC) %.3f open %u "
                "dir(+0x1050)(%.3f %.3f %.3f) flag105C %d projBit(T+0xC8) %d nativeJump %.2f deg",
                e.tick, d.stamp, was, d.age, d.window, d.open, d.dir.x, d.dir.y, d.dir.z, d.flag, d.projBit,
                T13Deg(d.nativeJump));
        break;
    }
    case t13::Kind::Roll:
    {
        const auto& d = e.u.roll;
        if (reach)
            LOG("REACH SF ROLL t=%d pc88bit2 %u moveX %.3f rate(+0x144) %.4f idle(+0x140) %.3f hold(+0x148) %.3f "
                "latch(+0x14C) %u", e.tick, d.bit2, d.moveX, d.rate, d.idle, d.hold, d.latch);
        else
            LOG("H4 T13 ROLL t=%d rec08bit2 %u moveX %.3f rate(+0x11C) %.4f idle(+0x118) %.3f hold(+0x120) %.3f "
                "latch(+0x124) %u", e.tick, d.bit2, d.moveX, d.rate, d.idle, d.hold, d.latch);
        break;
    }
    case t13::Kind::Dt:
        LOG("%s DT t=%d a6(dtA) %.6f a7(dtB) %.6f spt %.6f", p, e.tick, e.u.dt.dtA, e.u.dt.dtB, e.u.dt.spt);
        break;
    case t13::Kind::State:
    {
        const auto& d = e.u.state;
        if (d.state == t13::kUnread)
            LOG("REACH SF STATE t=%d obj[+0x1AE] offset %d out of the read range: unread", e.tick, d.off);
        else if (d.was == t13::kUnread)
            LOG("REACH SF STATE t=%d obj[+0x1AE=0x%X] %d at the stint start (0x1B = hull steering skipped)", e.tick,
                d.off, d.state);
        else
            LOG("REACH SF STATE t=%d obj[+0x1AE=0x%X] -> %d (was %d; 0x1B = hull steering skipped)", e.tick, d.off,
                d.state, d.was);
        break;
    }
    case t13::Kind::Phys:
    {
        const auto& d = e.u.phys;
        char was[16];
        if (d.was == t13::kUnread)
            snprintf(was, sizeof(was), "stint start");
        else
            snprintf(was, sizeof(was), "%d", d.was);
        if (d.mode == t13::kUnread)
            LOG("H4 T13 BIPED t=%d unit 0x%08X physOff 0x%X out of the read range: unread", e.tick, d.unit, d.off);
        else
            LOG("H4 T13 BIPED t=%d unit 0x%08X physOff 0x%X mode %d (was %s; 7/8 = the record's angles are rebased, "
                "NOT world)", e.tick, d.unit, d.off, d.mode, was);
        break;
    }
    case t13::Kind::Type:
        LOG("H4 T13 TYPE t=%d tagType %d override F58 %d F59 %d (the hook firing = runtime type 13)", e.tick,
            e.u.type.tagType, e.u.type.f58, e.u.type.f59);
        break;
    case t13::Kind::Owner:
        LOG("%s owner thread %u -> %u (the previous owner was silent for more than %llu ms)", p, e.u.owner.from,
            e.u.owner.to, static_cast<unsigned long long>(kT13TakeoverMs));
        break;
    case t13::Kind::Fault:
        LOG("%s FAULT reading %s (veh 0x%08X); this stint observes nothing more", p,
            e.u.fault.where == 0 ? "before the native" : e.u.fault.where == 1 ? "after the native" : "the tag",
            e.vehicle);
        break;
    }
}

// A stint whose calls stopped (a landing, leaving the craft, the level ending) is ended here once its last
// call is more than the stint gap old, so its END line and its last partial window (LINK 2s, RATE) go out
// now. The busy flag makes the worker the producer for that moment; it is tried only after the lane has been
// silent for the gap, so it never contends with a flight in progress (a first call landing in that instant
// counts as `busy` and is skipped, and it begins a new stint either way).
void T13EndStaleStint(T13Lane& lane, uint64_t now)
{
    const uint64_t last = lane.lastObservedMs.load(std::memory_order_relaxed);
    if (!last || !(now >= last && now - last > t13::Producer<kT13RingSize>::kStintGapMs))
        return;
    if (lane.busy.exchange(true, std::memory_order_acquire))
        return;   // a call is being observed right now: not stale
    (void)lane.producer.EndIfStale(now, lane.ring);
    if (!lane.producer.Active())
        lane.lastObservedMs.store(0, std::memory_order_relaxed);   // nothing open: stop checking until a call
    lane.busy.store(false, std::memory_order_release);
}

void T13Drain(T13Lane& lane)
{
    t13::Event e;
    for (unsigned n = 0; n < kT13RingSize && lane.ring.Pop(e); ++n)
        T13Format(e);
}

struct T13Counts
{
    uint32_t calls, ticks, dups, byCaller[5], foreignRet, otherThreads, otherUsers, busy, faults, badTime, drops;
};
T13Counts T13Take(T13Lane& lane)
{
    T13Counts c{};
    c.calls = lane.calls.exchange(0, std::memory_order_relaxed);
    c.ticks = lane.ticks.exchange(0, std::memory_order_relaxed);
    c.dups = lane.dups.exchange(0, std::memory_order_relaxed);
    for (int i = 0; i < 5; ++i)
        c.byCaller[i] = lane.byCaller[i].exchange(0, std::memory_order_relaxed);
    c.foreignRet = lane.foreignRet.exchange(0, std::memory_order_relaxed);
    c.otherThreads = lane.otherThreads.exchange(0, std::memory_order_relaxed);
    c.otherUsers = lane.otherUsers.exchange(0, std::memory_order_relaxed);
    c.busy = lane.busyHits.exchange(0, std::memory_order_relaxed);
    c.faults = lane.faults.exchange(0, std::memory_order_relaxed);
    c.badTime = lane.badTime.exchange(0, std::memory_order_relaxed);
    c.drops = lane.ring.TakeDropped();
    return c;
}
} // namespace

// Cold worker, every loop (after LegacyDriveReport).
void Type13LogReport()
{
    const uint64_t now = GetTickCount64();
    T13EndStaleStint(g_t13Reach, now);
    T13EndStaleStint(g_t13Halo4, now);
    T13Drain(g_t13Reach);
    T13Drain(g_t13Halo4);
    static uint64_t s_last = 0;
    if (now - s_last < 2000)
        return;
    s_last = now;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    {
        const T13Counts c = T13Take(g_t13Reach);
        if (c.calls)
        {
            const uint64_t beat = g_reachLastCamCopyMs.load(std::memory_order_relaxed);
            LOG("REACH SF calls %u ticks %u dup %u /2s; callers %s %u %s %u %s %u unknown %u; foreignRet %u "
                "otherThreads %u otherUsers %u busy %u badTime %u; veh 0x%08X pcMaster %d armed %d; faults %u "
                "ringDrops %u; heartbeatAge %llu ms (read only, never stamped)",
                c.calls, c.ticks, c.dups, kT13ReachCallerNames[0], c.byCaller[0], kT13ReachCallerNames[1], c.byCaller[1],
                kT13ReachCallerNames[2], c.byCaller[2], c.byCaller[4] + c.byCaller[3], c.foreignRet, c.otherThreads,
                c.otherUsers, c.busy, c.badTime, g_t13Reach.lastVehicle.load(std::memory_order_relaxed),
                g_t13Reach.master.load(std::memory_order_relaxed), g_reachPcArmed.load(std::memory_order_acquire) ? 1 : 0,
                c.faults, c.drops, static_cast<unsigned long long>(beat && now >= beat ? now - beat : 0));
        }
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    {
        auto& lane = g_t13Halo4;
        const T13Counts c = T13Take(lane);
        const uint32_t parent = lane.lastParent.load(std::memory_order_relaxed);
        const int32_t seat = lane.lastSeat.load(std::memory_order_relaxed);
        const bool viewOwned = NativeVehicleFirstPerson_SeatViewOwned(GameTitle::Halo4, parent, seat);
        if (c.calls)
            LOG("H4 T13 calls %u ticks %u dup %u /2s; callers %s %u %s %u %s %u %s %u unknown %u; foreignRet %u "
                "otherThreads %u otherUsers %u busy %u badTime %u; veh 0x%08X parent 0x%08X seat %d viewOwned %d "
                "recMaster %d; faults %u ringDrops %u",
                c.calls, c.ticks, c.dups, kT13Halo4CallerNames[0], c.byCaller[0], kT13Halo4CallerNames[1], c.byCaller[1],
                kT13Halo4CallerNames[2], c.byCaller[2], kT13Halo4CallerNames[3], c.byCaller[3], c.byCaller[4],
                c.foreignRet, c.otherThreads, c.otherUsers, c.busy, c.badTime,
                lane.lastVehicle.load(std::memory_order_relaxed), parent, seat, viewOwned ? 1 : 0,
                lane.master.load(std::memory_order_relaxed), c.faults, c.drops);
        // H4 CAM: the game's own aim (the camera forward the servo closes on, published with direct drive)
        // against the facing the physics steers by and the craft forward, while a stint is live.
        float facing[2]{}, craft[2]{};
        uint64_t snapMs = 0;
        bool snapOk = false;
        for (int attempt = 0; attempt < 2 && !snapOk; ++attempt)
        {
            const uint32_t before = lane.snapSeq.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            facing[0] = lane.snapFacingYaw.load(std::memory_order_relaxed);
            facing[1] = lane.snapFacingPitch.load(std::memory_order_relaxed);
            craft[0] = lane.snapCraftYaw.load(std::memory_order_relaxed);
            craft[1] = lane.snapCraftPitch.load(std::memory_order_relaxed);
            snapMs = lane.snapMs.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            snapOk = lane.snapSeq.load(std::memory_order_relaxed) == before;
        }
        if (snapOk && snapMs && now >= snapMs && now - snapMs <= 2500)
        {
            const auto& drive = g_legacyDrive[2];
            float cam[2]{};
            uint64_t setMs = 0;
            bool camOk = false;
            for (int attempt = 0; attempt < 2 && !camOk; ++attempt)
            {
                const uint32_t before = drive.seq.load(std::memory_order_acquire);
                if (before & 1u)
                    continue;
                cam[0] = drive.aimYaw.load(std::memory_order_relaxed);
                cam[1] = drive.aimPitch.load(std::memory_order_relaxed);
                setMs = drive.setMs.load(std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_acquire);
                camOk = drive.seq.load(std::memory_order_relaxed) == before;
            }
            const uint64_t camAge = setMs && now >= setMs ? now - setMs : ~0ull;
            if (!camOk || camAge > 500)
                LOG("H4 T13 CAM camFwd: no publication (the game's aim is published with aim_direct_drive=1) "
                    "facing(y %.3f p %.3f) craftFwd(y %.3f p %.3f) viewOwned %d",
                    facing[0], facing[1], craft[0], craft[1], viewOwned ? 1 : 0);
            else
            {
                const t13::Vec3 fwd = t13::ForwardFromYawPitch(cam[0], cam[1]);
                LOG("H4 T13 CAM camFwd(y %.3f p %.3f) age %llu ms vs facing %.2f deg vs craftFwd %.2f deg viewOwned %d",
                    cam[0], cam[1], static_cast<unsigned long long>(camAge),
                    T13Deg(t13::AngleBetween(fwd, t13::ForwardFromYawPitch(facing[0], facing[1]))),
                    T13Deg(t13::AngleBetween(fwd, t13::ForwardFromYawPitch(craft[0], craft[1]))), viewOwned ? 1 : 0);
            }
        }
    }
#endif
}
