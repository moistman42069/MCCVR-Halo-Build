// Included in game.cpp at file scope, right after type13_flight_log.inl (which only a dev build includes).
//
// THE SABRE'S DIRECT DRIVE. Reach's per-user control dispatcher 0x6350C calls a dedicated attitude controller,
// 0x64828, for the pilot of a physics type-13 craft (the Sabre, the Seraph) and jumps past the desired-angles
// accumulator the direct drive's hook sits on. So that pilot was never written and stayed on the stick servo. This
// detour runs the game's controller unchanged and then, for the first local player, hands the same record to the
// direct drive's own apply (ReachPcApply): the hand's yaw and pitch go into player control's desired angles after
// the controller has written them and before the dispatcher derives its deltas from them, exactly as on foot and in
// every other seat. Roll stays the game's (the controller's own roll rate and auto-level), and so does everything
// that limits the craft: the hull chases the desired facing through its own gain and torque limit, downstream of
// the write.
//
// THE RECORD IS IN WORLD ANGLES here. The controller builds its attitude from yaw, pitch and roll and writes the
// Euler angles of the result back, so forward(yaw, pitch) is the craft's desired forward at any roll, and the craft's
// desired facing (vehicle +0x1E4, the vector the hull steers by) is that forward, one tick later. No seat frame
// applies and there is no seat limit to respect.
//
// WHAT A FLIGHT WAITS FOR (a stint: one craft flown without a break; a reload, which may reuse the craft's handle,
// starts another):
//   * THE LINK. That the record written here is what the hull steers by is measured, not assumed: on each new game
//     tick the craft's desired facing has to equal the forward of the record as the previous call left it, for
//     kReachSabreLinkTicks ticks running. Until then nothing is written and the drive's heartbeat is not stamped,
//     so the aim provider keeps the stick servo (a drive that reads ready and writes nothing would hold the stick
//     neutral: dead aim). kReachSabreLinkMissTicks ticks out of agreement end the proof, and the servo takes over
//     again.
//   * THE GAME'S OWN STEERING. Past the play space's edge the game stamps a direction on the craft (vehicle +0xABC
//     the tick, +0xAC0 the direction) and the controller turns it there for the tag's window. While a stamp is
//     that fresh the update writes nothing (the apply's no-write action), the stick stays neutral, and the craft is
//     the game's to turn.
// THE WRITE'S RATE. The hull steers on the angle between the craft and its desired facing with a stiff gain (the nose
// trails a turning facing by a few degrees), so a hand flicked across the cockpit would whip the craft. The written
// direction moves at most kReachSabreMaxTurnDegPerSecond, the fastest the game's own stick turned the record
// (78 degrees a second at full deflection, measured).
//
// Keys: the seat direct drive's own (aim_direct_drive, seat_direct_drive, and reach_vehicle_aim = 2 for a pilot,
// who is a look-steered driver). With any of them off the detour only runs the game's controller.
//
// The detour is the one hook on the controller in every build. In a dev build with type13_log = 1 it also feeds
// the log-only observer (type13_flight_log.inl) around the native call, before anything here is written.

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
namespace
{
// ADDR-HYGIENE: resolved (src/common/drive_sites.h: kReachSabreAnchors, ResolveReachSabre). No recorded RVA says
// where anything is. Before a hook exists the install FINDS, in the module that is loaded: the controller's entry,
// the dispatcher's type-13 call and the hull's read of the desired facing by their signatures (each exactly once;
// the entry where a function starts); the dispatcher as the function that call sits in (.pdata), which must begin
// with the dispatcher's own prologue; the dispatcher's callers by theirs; and every other row -- each the
// instruction that reads a field this file reads, its displacement bytes exact -- inside the function it belongs
// to. Then it proves, on what it found: the call's rel32 lands on the entry, the dispatcher's frame agrees between
// its unwind info and its prologue, its stick-update caller calls the dispatcher, and the engine TLS index is the
// module's own, the one the controller loads and the one this lane's readers use. A miss: no hook, one line naming
// what failed, the pilot keeps the stick servo. The whole Reach image is hash-pinned before the camera core
// installs as well.
using drive_sites::kReachSabreAnchors;
// ONE TLS INDEX: the tables' record of it and the constant this lane's run-time readers read through are the
// same number, and stay so.
static_assert(drive_sites::kReachTlsIndexRva == kReachEngineTlsIndexRva,
              "the drive tables and Reach's run-time readers must name one engine TLS index");
// Where the tables record the controller, for the line that is written when it could not be found.
constexpr uint32_t kReachSabreEntryRecorded = kReachSabreAnchors[drive_sites::kReachSabreSites.entryRow].rva;
// The dispatcher's own callers, as this file holds them (drive_sites::kReachSabreSites): [0] the action / kit
// consumer (an extra call), [1] the stick update, [2] the path with no unit. [1] must be proven (it is the update);
// the others read as unknown when they are not.
static_assert(drive_sites::kReachSabreSites.order[1] == aim_caller::Caller::Stick &&
              drive_sites::kReachSabreSites.order[2] == aim_caller::Caller::NoUnit);

// The tag's forced-steer window (the type-13 block's +0xAC) as measured in the retail Sabre: one second. The game
// re-stamps while the craft stays outside, so a craft whose tag gave another window is only handed back a little
// late or early; nothing else follows from the constant.
constexpr float kReachSabreSteerWindowSeconds = 1.0f;
constexpr uint32_t kReachSabreLinkTicks = 20;
constexpr uint32_t kReachSabreLinkMissTicks = 3;
constexpr float kReachSabreLinkAgreeDeg = 0.5f;
constexpr float kReachSabreMaxTurnDegPerSecond = 80.0f;
// A flight's calls come once a rendered frame: a silence this long is a death, a reload or a pause, and the next
// call starts a flight afresh.
constexpr uint64_t kReachSabreGapMs = 250;

using ReachSabreControllerFn = void (*)(int, uint32_t, float, float, const void*, float, float);

struct ReachSabreDrive
{
    // Install (worker). Read by the hook only while it is enabled.
    uintptr_t base = 0;
    uint32_t entryRva = 0;            // where the install found the controller in this module
    uint32_t installs = 0;            // how many times the hook has been installed (worker)
    uint32_t dispatcherFrame = 0;     // the dispatcher's own return address: controller entry rsp + this many bytes
    uintptr_t callerRet[3]{};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> resetSeq{0};
    // Simulation thread.
    uint32_t seenReset = 0;
    uint32_t vehicle = UINT32_MAX;    // the stint's craft
    int32_t lastTick = INT32_MIN;
    uint64_t lastCallMs = 0;
    uint32_t agree = 0, misses = 0;
    bool proven = false;
    bool haveEnd = false;
    float endRecord[2]{};             // the record as the last call left it: what the next tick steers by
    // Simulation thread -> worker.
    std::atomic<uint32_t> calls{0}, ticks{0}, stints{0}, gated{0}, unread{0}, waiting{0}, linkMisses{0}, yielded{0},
        updates{0}, extras{0}, noUnit{0}, unknownCaller{0}, capped{0};
    std::atomic<uint32_t> state{0};   // 0 keys off / no flight, 1 waiting for the link, 2 proven
    std::atomic<float> noseMaxDeg{0.0f}, linkMaxDeg{0.0f};
} g_reachSabre;

bool ReachSabreWanted()
{
    const auto& d = g_reachSabre;
    return g_reachT13Enabled.load(std::memory_order_acquire) && g_config.reach_direct_drive == 3 &&
           g_config.seat_direct_drive != 0 && g_config.reach_vehicle_aim == 2 &&
           !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
           g_reachCamera.generation.load(std::memory_order_acquire) == d.generation.load(std::memory_order_acquire);
}

struct ReachSabreState
{
    const uint8_t* pc;    // the first local player's player control
    float facing[3];      // the craft's desired facing (what the hull steers by)
    float nose[3];        // the craft's own forward
    int32_t stamp;        // the forced steer's tick (-1: none)
    int32_t tick;
    float secondsPerTick;
};

// Sim thread, guarded: everything the decision needs, from the calling thread's own TLS and the craft's datum.
bool ReachSabreRead(uint32_t vehicle, ReachSabreState& s)
{
    __try
    {
        auto** slots = reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
        const uint32_t index = *reinterpret_cast<const uint32_t*>(g_reachCamera.base + kReachEngineTlsIndexRva);
        const uint8_t* tls = (slots && index < 0x200) ? slots[index] : nullptr;
        if (!tls)
            return false;
        const uintptr_t globals = *reinterpret_cast<const uintptr_t*>(tls + kReachPlayerControlTlsOffset);   // REC
        const auto* time = *reinterpret_cast<const uint8_t* const*>(tls + 0xA0);                             // WIN
        if (!globals || !time)
            return false;
        s.pc = reinterpret_cast<const uint8_t*>(globals);
        memcpy(&s.tick, time + 0xC, sizeof(s.tick));
        memcpy(&s.secondsPerTick, time + 8, sizeof(s.secondsPerTick));
        uint8_t kind = 0;
        const unsigned char* craft = ReachVehicleObjectData(static_cast<int32_t>(vehicle), kind);
        if (!craft || kind != kReachObjectKindVehicle)
            return false;
        memcpy(s.facing, craft + 0x1E4, sizeof(s.facing));            // HX
        memcpy(s.nose, craft + 0x50, sizeof(s.nose));                 // HFWD
        memcpy(&s.stamp, craft + 0xABC, sizeof(s.stamp));             // STAMP
        return std::isfinite(s.facing[0]) && std::isfinite(s.facing[1]) && std::isfinite(s.facing[2]) &&
               std::isfinite(s.nose[0]) && std::isfinite(s.nose[1]) && std::isfinite(s.nose[2]) &&
               s.secondsPerTick > 0.001f && s.secondsPerTick < 0.2f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Sim thread, guarded: the record's yaw and pitch as they stand now (ANGPTR).
bool ReachSabreRecordNow(const uint8_t* pc, float out[2])
{
    __try
    {
        memcpy(out, pc + 0x94, 2 * sizeof(float));
        return std::isfinite(out[0]) && std::isfinite(out[1]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The angle between two vectors of any length, degrees; 180 when either is undefined.
float ReachSabreAngleDeg(const float a[3], const float b[3])
{
    const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (!(la > 1.0e-4f) || !(lb > 1.0e-4f) || !std::isfinite(la) || !std::isfinite(lb))
        return 180.0f;
    const float dot = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
    return std::acos((std::max)(-1.0f, (std::min)(1.0f, dot))) * 57.29578f;
}

void ReachSabreMax(std::atomic<float>& slot, float value)
{
    float prior = slot.load(std::memory_order_relaxed);
    while (value > prior && !slot.compare_exchange_weak(prior, value, std::memory_order_relaxed)) {}
}

// Sim thread, after the game's controller has written the record. `entry`: the controller's own entry rsp;
// `dtA`: the controller's own time step for this update.
void ReachSabreAfterController(int user, uint32_t vehicle, float dtA, const uintptr_t* entry)
{
    auto& d = g_reachSabre;
    d.calls.fetch_add(1, std::memory_order_relaxed);
    if (user != 0)
        return;
    if (!ReachSabreWanted())
    {
        d.gated.fetch_add(1, std::memory_order_relaxed);
        d.vehicle = UINT32_MAX;   // the next wanted call starts a stint afresh
        d.state.store(0, std::memory_order_relaxed);
        return;
    }
    const uint32_t reset = d.resetSeq.load(std::memory_order_acquire);
    if (reset != d.seenReset)
    {
        d.seenReset = reset;
        d.vehicle = UINT32_MAX;
    }
    ReachSabreState s{};
    if (!ReachSabreRead(vehicle, s))
    {
        d.unread.fetch_add(1, std::memory_order_relaxed);
        d.vehicle = UINT32_MAX;
        d.state.store(0, std::memory_order_relaxed);
        return;
    }
    const uint64_t now = GetTickCount64();
    // A new flight: another craft, the game's tick gone back (a reload) or a silence (a death, a pause).
    if (vehicle != d.vehicle || s.tick < d.lastTick || now - d.lastCallMs > kReachSabreGapMs)
    {
        d.vehicle = vehicle;
        d.lastTick = s.tick;      // judged from the next tick on: nothing is known of the record before this call
        d.agree = d.misses = 0;
        d.proven = false;
        d.haveEnd = false;
        d.stints.fetch_add(1, std::memory_order_relaxed);
    }
    d.lastCallMs = now;
    // The link, once a game tick: the tick that just ran steered by the record as the previous call left it.
    if (s.tick != d.lastTick)
    {
        d.lastTick = s.tick;
        d.ticks.fetch_add(1, std::memory_order_relaxed);
        if (d.haveEnd)
        {
            const float cp = std::cos(d.endRecord[1]);
            const float forward[3]{cp * std::cos(d.endRecord[0]), cp * std::sin(d.endRecord[0]),
                                   std::sin(d.endRecord[1])};
            const float off = ReachSabreAngleDeg(forward, s.facing);
            ReachSabreMax(d.linkMaxDeg, off);
            if (off < kReachSabreLinkAgreeDeg)
            {
                d.misses = 0;
                if (d.agree < kReachSabreLinkTicks && ++d.agree == kReachSabreLinkTicks)
                    d.proven = true;
            }
            else
            {
                d.linkMisses.fetch_add(1, std::memory_order_relaxed);
                if (++d.misses >= kReachSabreLinkMissTicks)
                {
                    d.agree = 0;
                    d.proven = false;
                }
            }
        }
        ReachSabreMax(d.noseMaxDeg, ReachSabreAngleDeg(s.nose, s.facing));
    }
    d.state.store(d.proven ? 2u : 1u, std::memory_order_relaxed);
    if (d.proven)
    {
        // Which of the dispatcher's callers this is.
        const uintptr_t dispatcherReturn = d.dispatcherFrame ? entry[d.dispatcherFrame / sizeof(uintptr_t)] : 0;
        ReachApplySite site{};
        const float step = std::isfinite(dtA) && dtA > 0.0f ? (std::min)(dtA, 0.1f) : s.secondsPerTick;
        site.maxStepRad = kReachSabreMaxTurnDegPerSecond * 0.01745329f * step;
        site.capped = &d.capped;
        if (dispatcherReturn && dispatcherReturn == d.callerRet[1])
        {
            // THE UPDATE -- unless the game is steering the craft itself (a stamp younger than its window).
            const bool steering = s.stamp != -1 && s.tick >= s.stamp &&
                static_cast<float>(s.tick - s.stamp) * s.secondsPerTick < kReachSabreSteerWindowSeconds;
            site.action = steering ? aim_caller::Action::EndUpdate : aim_caller::Action::StepAndWrite;
            (steering ? d.yielded : d.updates).fetch_add(1, std::memory_order_relaxed);
        }
        else if (dispatcherReturn && dispatcherReturn == d.callerRet[2])
        {
            site.action = aim_caller::Action::EndUpdate;
            d.noUnit.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            // An extra call in the same update (or a caller this build does not know): the last write is put back.
            site.action = aim_caller::Action::RestoreOnly;
            (dispatcherReturn && dispatcherReturn == d.callerRet[0] ? d.extras : d.unknownCaller)
                .fetch_add(1, std::memory_order_relaxed);
        }
        ReachPcApply(0, nullptr, &site);
    }
    else
        d.waiting.fetch_add(1, std::memory_order_relaxed);   // no apply, so no heartbeat: the stick servo aims
    d.haveEnd = ReachSabreRecordNow(s.pc, d.endRecord);
}
} // namespace

// MinHook's relay jumps here, so _AddressOfReturnAddress() is the native's own entry rsp. The callback count spans
// the whole call, the native included: the teardown's quiescence proof and its ingress scan cover it. The native
// takes seven arguments, three of them on the stack, and reads them at entry +0x28 / +0x30 / +0x38, so it is
// forwarded through CallNativeWithSpareSlots (native_forward.h).
__declspec(noinline) void ReachType13Detour(int user, uint32_t vehicle, float yawD, float pitchD, const void* input,
                                            float dtA, float dtB)
{
    const auto* entry = static_cast<const uintptr_t*>(_AddressOfReturnAddress());
#if HALOMCCVR_DEV_DIAGNOSTICS
    volatile bool observing = false;
#endif
    g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const auto native = reinterpret_cast<ReachSabreControllerFn>(g_reachT13Orig);
        if (!native)
            return;
#if HALOMCCVR_DEV_DIAGNOSTICS
        T13Call call{};
        observing = T13ReachBefore(user, vehicle, yawD, pitchD, input, dtA, dtB, entry, call);
#endif
        CallNativeWithSpareSlots(native, user, vehicle, yawD, pitchD, input, dtA, dtB);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (observing)
        {
            observing = false;
            T13ReachAfter(call);   // releases the observer's busy flag; it reads what the native wrote, before ours
        }
#endif
        ReachSabreAfterController(user, vehicle, dtA, entry);
    }
    __finally
    {
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (observing)
            T13Release(g_t13Reach);
#endif
        g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// Worker, inside InstallReachCameraCore (whole-image-hashed module, g_reachCamera.base / generation set).
bool ReachType13Install(uintptr_t base, size_t size, uint32_t generation)
{
    auto& d = g_reachSabre;
    if (g_reachT13Target)
    {
        if (d.base == base)
        {
            d.generation.store(generation, std::memory_order_release);
            d.resetSeq.fetch_add(1, std::memory_order_acq_rel);
            g_reachT13Enabled.store(true, std::memory_order_release);
            return true;
        }
        LOG("Reach Sabre direct drive: the pilot controller's hook is still installed on another module; not "
            "reinstalled");
        return false;
    }
    // WHERE EVERYTHING IS IN THIS MODULE (drive_sites.h, by the rungs of drive_known.h): verified at the addresses
    // it is known to stand at, or found by search; every proof is made on those addresses either way. Nothing
    // below trusts a recorded RVA.
    drive_cascade::Resolution<drive_sites::ReachSabre> resolution;
    const bool opened = drive_cascade::Resolve(
        base, size, drive_cascade::Drive{GameTitle::HaloReach, "pilot-controller"}, resolution,
        [](drive_resolve::Resolver& r) { return drive_sites::ResolveReachSabre(r); });
    const drive_sites::Flight& found = resolution.found.flight;
    if (opened)
        drive_cascade::Announce(drive_cascade::kReachSabreName, resolution);
    // THE DECISION (drive_install.h; the offline check runs the same function on haloreach.dll as the loader maps
    // it). The engine TLS index must be the module's own, loaded by the controller itself (the resolution), and
    // the one this lane's readers use (ReachSabreRead and the apply read it through kReachEngineTlsIndexRva);
    // every row is on a page of code; the dispatcher's call lands on the controller; the frame through which the
    // dispatcher's own return address is read from the controller's entry rsp comes out of the loaded module's
    // function table and is the one the resolution proved; the stick update's caller is proven.
    const drive_install::FlightDecision decision = drive_install::DecideFlight(
        base, size, opened, kReachSabreAnchors, resolution.found.rows, found, base + kReachEngineTlsIndexRva, true, 3, 1);
    char failed[96]{};
    const bool tlsFailed = opened && !decision.tlsOk;
    snprintf(failed, sizeof(failed), "%s%s%s", tlsFailed ? "TLS-INDEX" : "", tlsFailed && decision.failed[0] ? "," : "",
             decision.failed);
    const bool anchorsOk = decision.rowsOk && decision.tlsOk;
    if (opened && !decision.tlsOk)
    {
        // NOT FOLLOWED, and said as what it is: the code may all have been found.
        if (found.tlsIndex)
            LOG("Reach Sabre direct drive: NOT FOLLOWED. This game DLL keeps its engine TLS index at +0x%X, and this "
                "lane's run-time readers read it at the recorded +0x%llX: Reach's data moved. The drive is not hooked, "
                "even where every code site was found, until that constant is the module's",
                found.tlsIndex, static_cast<unsigned long long>(kReachEngineTlsIndexRva));
    }
    const bool callOk = anchorsOk && decision.callOk, frameOk = decision.frameOk;
    const uint32_t frame = decision.frame;
    const char* const frameWhy = decision.frameWhy;
    const uintptr_t* const callerRet = decision.callerRet;
    if (!decision.hook)
    {
        char first[280] = "";
        if (opened)
            drive_cascade::FirstFailure(resolution.report, first, sizeof(first));
        LOG("Reach Sabre direct drive: the pilot's controller (recorded at +0x%X) was NOT hooked (%s; anchors failed: "
            "%s; dispatcher call=%d frame=%d%s%s stick-update caller=%d); a Sabre's pilot keeps the stick servo, "
            "nothing else changes",
            kReachSabreEntryRecorded, opened ? first : (resolution.why ? resolution.why : "the module did not open"),
            failed[0] ? failed : "none", callOk, frameOk, frameWhy ? ": " : "", frameWhy ? frameWhy : "",
            callerRet[1] ? 1 : 0);
        return false;
    }
    void* target = reinterpret_cast<void*>(base + found.entry);
    // A NEW INSTALL is watched afresh (drive_cascade::HookWatch): nothing counted before it -- a moment ago, by
    // the hook this one replaces -- may read as a call of this one.
    ++d.installs;
    d.calls.store(0, std::memory_order_relaxed);
    const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(&ReachType13Detour), &g_reachT13Orig);
    if (created != MH_OK)
    {
        g_reachT13Orig = nullptr;
        LOG("Reach Sabre direct drive: the pilot controller's hook FAILED (create=%d); a Sabre's pilot keeps the "
            "stick servo", static_cast<int>(created));
        return false;
    }
    // Everything the hook reads is set before it can run.
    d.base = base;
    d.entryRva = found.entry;
    d.dispatcherFrame = frame;
    for (int i = 0; i < 3; ++i)
        d.callerRet[i] = callerRet[i];
    d.generation.store(generation, std::memory_order_release);
    d.resetSeq.fetch_add(1, std::memory_order_acq_rel);
    if (MH_EnableHook(target) != MH_OK)
    {
        MH_RemoveHook(target);
        g_reachT13Orig = nullptr;
        LOG("Reach Sabre direct drive: the pilot controller's hook FAILED to enable; removed, a Sabre's pilot keeps "
            "the stick servo");
        return false;
    }
    g_reachT13Target = target;
    g_reachT13Enabled.store(true, std::memory_order_release);
    LOG("Reach Sabre direct drive: the pilot's controller +0x%X is hooked (dispatcher return at entry rsp+0x%X; "
        "callers: extra %d, stick update 1, no unit %d). After the game's own controller the hand's yaw and pitch "
        "are written to the record every other seat is written through (keys: aim_direct_drive, seat_direct_drive, "
        "reach_vehicle_aim 2), once a flight has shown that record to be what the craft steers by; at most %.0f "
        "deg/s; roll, and the game's own turn-back at the play space's edge, stay the game's",
        found.entry, frame, callerRet[0] ? 1 : 0, callerRet[2] ? 1 : 0,
        static_cast<double>(kReachSabreMaxTurnDegPerSecond));
    return true;
}

// Cold worker: the game must be calling the pilot's controller right now. The player is in control of Reach
// (DriveWatchPlayerInControl), and the seat sampler -- the vehicle camera lane's, nothing this hook produces -- has
// a fresh sample of this generation that names the pilot's seat of a physics-type-13 craft. (It names a seat only
// while the first-person seat view is on: with vehicle_first_person off this is never true, and the watch below
// never counts.)
static bool ReachSabrePilotingNow(uint64_t nowMs)
{
    if (!DriveWatchPlayerInControl(GameTitle::HaloReach, nowMs) || !Game_ReachDirectDriveSeated())
        return false;
    ReachVehicleId identity = ReachVehicleId::Unknown;
    int seat = -1;
    return ReachCurrentSeat(identity, seat) && ReachVehicleExpectedPhysicsType(identity) == 13 &&
           ReachVehicleSeatIsDriver(identity, seat);
}

// Cold worker, with the direct drive's own report: what the pilot's detour did in the last window.
void ReachSabreDriveReport()
{
    auto& d = g_reachSabre;
    static unsigned long long s_last = 0;
    const unsigned long long now = GetTickCount64();
    {
        // "INSTALLED" IS NOT "RUNNING" (drive_cascade.h: HookWatch), on every loop, before the counters below are
        // read and cleared.
        static drive_cascade::HookWatch s_watch;
        const void* target = g_reachT13Target;
        if (s_watch.Pass(drive_cascade::HookWatch::Install(target, d.installs), target && ReachSabrePilotingNow(now),
                         d.calls.load(std::memory_order_relaxed) != 0, now))
            drive_cascade::SayNeverCalled("Reach Sabre direct drive", "the pilot's controller", d.entryRva,
                                          "the player was piloting a Sabre or a Seraph, where the game runs it every "
                                          "frame");
    }
    if (now - s_last < 2000)
        return;
    s_last = now;
    const uint32_t calls = d.calls.exchange(0, std::memory_order_relaxed);
    const uint32_t ticks = d.ticks.exchange(0, std::memory_order_relaxed);
    const uint32_t stints = d.stints.exchange(0, std::memory_order_relaxed);
    const uint32_t gated = d.gated.exchange(0, std::memory_order_relaxed);
    const uint32_t unread = d.unread.exchange(0, std::memory_order_relaxed);
    const uint32_t waiting = d.waiting.exchange(0, std::memory_order_relaxed);
    const uint32_t misses = d.linkMisses.exchange(0, std::memory_order_relaxed);
    const uint32_t yielded = d.yielded.exchange(0, std::memory_order_relaxed);
    const uint32_t updates = d.updates.exchange(0, std::memory_order_relaxed);
    const uint32_t extras = d.extras.exchange(0, std::memory_order_relaxed);
    const uint32_t noUnit = d.noUnit.exchange(0, std::memory_order_relaxed);
    const uint32_t unknown = d.unknownCaller.exchange(0, std::memory_order_relaxed);
    const uint32_t capped = d.capped.exchange(0, std::memory_order_relaxed);
    const float nose = d.noseMaxDeg.exchange(0.0f, std::memory_order_relaxed);
    const float link = d.linkMaxDeg.exchange(0.0f, std::memory_order_relaxed);
    if (!calls)
        return;
    const uint32_t state = d.state.load(std::memory_order_relaxed);
    LOG("REACHDRIVE Sabre: %u controller calls in %u ticks (new flights %u); the record %s (the craft's facing "
        "stood at most %.2f deg off it, %u ticks out of agreement); updates handed to the drive %u (%u of its "
        "writes held to the turn rate), left to the game's own turn-back %u, waiting for the link %u, keys off %u, "
        "unreadable %u; other callers: extra %u, no unit %u, unknown %u; the craft's nose trailed its facing by at "
        "most %.1f deg",
        calls, ticks, stints,
        state == 2 ? "is PROVEN to be what the craft steers by" :
            state == 1 ? "is not yet proven to be what the craft steers by" : "is not being judged",
        static_cast<double>(link), misses, updates, capped, yielded, waiting, gated, unread, extras, noUnit, unknown,
        static_cast<double>(nose));
}
#endif // HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
