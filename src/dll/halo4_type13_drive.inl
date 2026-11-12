// Included in game.cpp at file scope, after legacy_direct_drive.inl and type13_flight_log.inl (which only a dev
// build includes).
//
// THE BROADSWORD'S DIRECT DRIVE. Halo 4's per-user control dispatcher 0xA8828 calls a dedicated attitude controller,
// 0xAA524, for the pilot of a physics type-13 craft (the Broadsword) and jumps past the desired-angles accumulator
// the direct drive's hook sits on. So that pilot was never written and stayed on the stick servo. This detour runs
// the game's controller unchanged and then, for the first local player, writes the hand's yaw and pitch into the
// direct drive's own record (legacy_direct_drive.inl: *(TLS + 0xB0) + user * 0x1F0, angles at +0x138; the
// controller's record is that + 0x120 and its Euler output its +0x18) -- after the controller has written them and
// before the dispatcher derives its deltas from them, exactly as on foot. Roll stays the game's, and so does
// everything that limits the craft: its physics chases the desired facing through its own gain and torque limit,
// downstream of the write.
//
// THE RECORD IS IN WORLD ANGLES here: the controller builds its attitude from yaw, pitch and roll and writes the
// Euler angles of the result back, and the craft's desired facing (vehicle +0x4BC, the vector its physics steers by)
// is the forward of those angles a tick or two later. No seat frame applies and the controller has no pitch stop,
// so BOTH axes are the hand's: the input thread leaves a Halo 4 seat's pitch on the stick servo except while this
// hook reports its pilot (Halo4FlightPilotLive).
//
// WHAT A FLIGHT WAITS FOR (a stint: one craft flown without a break; a reload, which reuses the craft's handle,
// starts another):
//   * THE LINK. That the record written here is what the craft steers by is measured, not assumed: on each new game
//     tick the craft's desired facing has to equal the forward of the record as one of the last two calls before
//     that tick left it, for kHalo4FlightLinkTicks ticks running. Until then nothing is written and no axis is
//     reported written, so the stick servo keeps aiming. kHalo4FlightLinkMissTicks ticks out of agreement end the
//     proof.
//   * THE GAME'S OWN STEERING. The game stamps a direction on the craft (vehicle +0x104C the tick, +0x1050 the
//     direction) and the controller turns it there for the tag's window. While a stamp is that fresh the update
//     writes nothing; the axes stay reported, so the stick stays neutral and the craft is the game's to turn.
// THE WRITE'S RATE. The craft's physics follows its desired facing with the same stiff gain as Reach's Sabre (the
// tag's 30 pi), so the written direction moves at most kHalo4FlightMaxTurnDegPerSecond: the Sabre's measured stick
// rate, and half of what this seat's own look limits allow the stick once it has run up.
//
// Keys: the seat direct drive's own (aim_direct_drive, seat_direct_drive, and reach_vehicle_aim = 2 for a pilot,
// who is a look-steered driver), and the mod's first-person seat view, which the input thread requires before it
// gives the seat any axis. With any of them off the detour only runs the game's controller.
//
// The detour is the one hook on the controller in every build. In a dev build with type13_log = 1 it also feeds
// the log-only observer (type13_flight_log.inl) around the native call, before anything here is written.

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
namespace
{
// ADDR-HYGIENE: resolved (src/common/drive_sites.h: kHalo4FlightAnchors, ResolveHalo4Flight). No recorded RVA says
// where anything is. Before a hook exists the install FINDS, in the module that is loaded: the controller's entry,
// the dispatcher's type-13 call and the TLS index load by their signatures (each exactly once; the entry where a
// function starts, the TLS load inside it); the dispatcher as the function that call sits in (.pdata), which must
// begin with the dispatcher's own prologue; the dispatcher's callers by theirs; and every other row -- each the
// instruction that reads a field this file reads, its displacement bytes exact -- inside the function it belongs
// to. Then it proves, on what it found: the call's rel32 lands on the entry, the TLS index is the module's own and
// the direct drive's, the dispatcher's frame agrees between its unwind info and its prologue, and its stick-update
// caller calls the dispatcher. A miss: no hook, one line naming what failed, the pilot keeps the stick servo.
using drive_sites::kHalo4FlightAnchors;
// Where the tables record the controller, for the lines that are written before anything is resolved.
constexpr uint32_t kHalo4FlightEntryRecorded = kHalo4FlightAnchors[drive_sites::kHalo4FlightSites.entryRow].rva;
// The dispatcher's own callers, as this file holds them (drive_sites::kHalo4FlightSites): [0] the action / kit
// consumer and [1] the weapon's aim sway (extra calls), [2] the stick update, [3] the path with no unit. [2] must be
// proven (it is the update); the others read as unknown when they are not.
static_assert(drive_sites::kHalo4FlightSites.order[2] == aim_caller::Caller::Stick &&
              drive_sites::kHalo4FlightSites.order[3] == aim_caller::Caller::NoUnit);

// The tag's steering window (the type-13 block's +0xCC): half a second for the campaign's craft, one second for the
// Forge one, as measured. The longer is used for both: a craft with the shorter window is handed back half a second
// late, and nothing else follows from the constant.
constexpr float kHalo4FlightSteerWindowSeconds = 1.0f;
constexpr uint32_t kHalo4FlightLinkTicks = 20;
constexpr uint32_t kHalo4FlightLinkMissTicks = 3;
constexpr float kHalo4FlightLinkAgreeDeg = 0.5f;
constexpr float kHalo4FlightMaxTurnDegPerSecond = 80.0f;
// A flight's calls come once a rendered frame: a silence this long is a death, a reload or a pause, and the next
// call starts a flight afresh.
constexpr uint64_t kHalo4FlightGapMs = 250;
constexpr uint64_t kHalo4FlightPilotFreshMs = 150;

using Halo4FlightControllerFn = void (*)(int, uint32_t, float, float, const void*, float, float);

struct Halo4FlightDrive
{
    // Install (worker). Read by the hook only while it is enabled.
    void* target = nullptr;
    void* original = nullptr;
    uintptr_t base = 0;
    uint32_t entryRva = 0;            // where the install found the controller in this module
    uint32_t dispatcherFrame = 0;     // the dispatcher's own return address: controller entry rsp + this many bytes
    uintptr_t callerRet[4]{};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint32_t> resetSeq{0};
    // Simulation thread.
    uint32_t seenReset = 0;
    uint32_t vehicle = UINT32_MAX;    // the stint's craft
    int32_t lastTick = INT32_MIN;
    uint64_t lastCallMs = 0;
    uint32_t agree = 0, misses = 0;
    bool proven = false;
    int ends = 0;                     // how many of endRecord are known
    float endRecord[2][2]{};          // the record as the last call before this tick, and before the tick before, left it
    float lastRecord[2]{};            // the record as the latest call left it
    bool haveLast = false;
    // Simulation thread -> input thread: the proven pilot (Halo4FlightPilotLive).
    std::atomic<uint64_t> pilotMs{0};
    std::atomic<uint32_t> pilotVehicle{UINT32_MAX};
    // Simulation thread -> worker.
    std::atomic<uint32_t> calls{0}, ticks{0}, stints{0}, gated{0}, unread{0}, waiting{0}, linkMisses{0}, lateTicks{0},
        yielded{0}, updates{0}, wrote{0}, noAxes{0}, stale{0}, extras{0}, noUnit{0}, unknownCaller{0}, capped{0},
        putBacks{0}, faults{0};
    std::atomic<uint32_t> state{0};   // 0 keys off / no flight, 1 waiting for the link, 2 proven
    std::atomic<float> noseMaxDeg{0.0f}, linkMaxDeg{0.0f};
} g_halo4Flight;

bool Halo4FlightWanted()
{
    const auto& d = g_halo4Flight;
    return d.enabled.load(std::memory_order_acquire) && g_config.legacy_direct_drive != 0 &&
           g_config.seat_direct_drive != 0 && g_config.reach_vehicle_aim == 2 &&
           !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
           g_halo4Camera.generation.load(std::memory_order_acquire) == d.generation.load(std::memory_order_acquire) &&
           TitleAdapter_GetActiveTitle() == GameTitle::Halo4;
}

struct Halo4FlightState
{
    const float* record;  // the direct drive's record: yaw, pitch
    float facing[3];      // the craft's desired facing (what its physics steers by)
    float nose[3];        // the craft's own forward
    int32_t stamp;        // the game's own steering: its tick (-1: none)
    int32_t tick;
    float secondsPerTick;
};

// Sim thread, guarded: everything the decision needs, from the calling thread's own TLS and the craft's datum.
bool Halo4FlightRead(uint32_t vehicle, Halo4FlightState& s)
{
    __try
    {
        const auto& st = g_legacyDrive[2];
        const auto& site = kLegacyDriveSites[2];
        if (!st.installed.load(std::memory_order_acquire) || !st.tlsIndexAddress)
            return false;
        const uint8_t* tls = nullptr;
        const uintptr_t pc = LegacyDriveRecord(st, site, 0, &tls);   // RECSETUP
        if (!pc || !tls)
            return false;
        s.record = reinterpret_cast<const float*>(pc + site.angles);                       // ANGPTR
        const auto* time = *reinterpret_cast<const uint8_t* const*>(tls + 0x90);           // WIN
        const auto* table = *reinterpret_cast<const uint8_t* const*>(tls + 0x18);          // VEH
        if (!time || !table)
            return false;
        memcpy(&s.tick, time + 0xC, sizeof(s.tick));
        memcpy(&s.secondsPerTick, time + 8, sizeof(s.secondsPerTick));
        const uint8_t* craft = Halo4VehicleObject(table, vehicle, 0x2);
        if (!craft)
            return false;
        memcpy(s.facing, craft + 0x4BC, sizeof(s.facing));            // PX
        memcpy(s.nose, craft + 0x70, sizeof(s.nose));                 // PFWD
        memcpy(&s.stamp, craft + 0x104C, sizeof(s.stamp));            // REDIR
        return std::isfinite(s.record[0]) && std::isfinite(s.record[1]) && std::isfinite(s.facing[0]) &&
               std::isfinite(s.facing[1]) && std::isfinite(s.facing[2]) && std::isfinite(s.nose[0]) &&
               std::isfinite(s.nose[1]) && std::isfinite(s.nose[2]) && s.secondsPerTick > 0.001f &&
               s.secondsPerTick < 0.2f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The angle between two vectors of any length, degrees; 180 when either is undefined.
float Halo4FlightAngleDeg(const float a[3], const float b[3])
{
    const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (!(la > 1.0e-4f) || !(lb > 1.0e-4f) || !std::isfinite(la) || !std::isfinite(lb))
        return 180.0f;
    const float dot = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
    return std::acos((std::max)(-1.0f, (std::min)(1.0f, dot))) * 57.29578f;
}

float Halo4FlightOffDeg(const float record[2], const float facing[3])
{
    const float cp = std::cos(record[1]);
    const float forward[3]{cp * std::cos(record[0]), cp * std::sin(record[0]), std::sin(record[1])};
    return Halo4FlightAngleDeg(forward, facing);
}

void Halo4FlightMax(std::atomic<float>& slot, float value)
{
    float prior = slot.load(std::memory_order_relaxed);
    while (value > prior && !slot.compare_exchange_weak(prior, value, std::memory_order_relaxed)) {}
}

// Sim thread: one call of the pilot's controller, through the direct drive's own state (legacy_direct_drive.inl):
// its record, the input thread's setpoint and seat axes, its put-back slot, and the per-axis write stamps the input
// thread reads to hold the stick neutral (LegacyDriveSeatAxes). `gameSteers`: an update the game's own steering owns.
void Halo4FlightApply(aim_caller::Action action, bool gameSteers, float maxStepRad)
{
    auto& d = g_halo4Flight;
    auto& st = g_legacyDrive[2];
    const auto& site = kLegacyDriveSites[2];
    st.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        do
        {
            if (!st.installed.load(std::memory_order_acquire) || !st.tlsIndexAddress)
                break;
            const uint8_t* tls = nullptr;
            const uintptr_t pc = LegacyDriveRecord(st, site, 0, &tls);
            LegacyDriveNoteSession(site.title, tls);
            if (!pc)
                break;
            float* pair = reinterpret_cast<float*>(pc + site.angles);
            const LegacyDrivePublication pub = LegacyDriveReadPublication(st);
            const uint64_t now = GetTickCount64();
            const bool fresh = pub.coherent && pub.valid && pub.setMs && now >= pub.setMs && now - pub.setMs <= 200;
            // The axes the input thread says the hand owns in this seat. None (the seat's view is not the mod's, the
            // stick is steering, a key is off, nothing fresh): nothing is written and the servo keeps the seat.
            const uint32_t axes = fresh && pub.seated ? pub.axesBits : 0u;
            if (action == aim_caller::Action::RestoreOnly)
            {
                // AN EXTRA CALL in the same update: the last write is put back over what it added.
                if (axes && st.putBack.valid)
                {
                    const float native[2]{pair[0], pair[1]};
                    const aim_caller::Refusal why = st.putBack.check(pc, pub.context(), st.stickSeq, AimCallerClockNow(),
                                                                     native, st.putBack.max_age);
                    if (why == aim_caller::Refusal::None)
                    {
                        if (st.putBack.axis[0])
                            pair[0] = st.putBack.value[0];
                        if (st.putBack.axis[1])
                            pair[1] = st.putBack.value[1];
                        d.putBacks.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                break;
            }
            // AN UPDATE: nothing written before it may be put back after it.
            ++st.stickSeq;
            st.putBack.invalidate();
            if (!fresh)
                d.stale.fetch_add(1, std::memory_order_relaxed);
            else if (!axes)
                d.noAxes.fetch_add(1, std::memory_order_relaxed);
            if (!axes)
                break;
            const bool yawOwned = (axes & kLegacyDriveAxisYaw) != 0, pitchOwned = (axes & kLegacyDriveAxisPitch) != 0;
            if (action == aim_caller::Action::EndUpdate)
            {
                // The game steers the craft itself: nothing is written, and the axes stay the drive's, so the stick
                // stays neutral instead of the servo pushing against the game's turn.
                if (gameSteers)
                {
                    st.seatYawWriteMs.store(yawOwned ? now : 0, std::memory_order_release);
                    st.seatPitchWriteMs.store(pitchOwned ? now : 0, std::memory_order_release);
                    st.seatLimitedMs.store(0, std::memory_order_release);
                    st.flightMs.store(now, std::memory_order_release);
                }
                break;
            }
            float target[2]{pub.yaw, pub.pitch};
            if (!std::isfinite(target[0]) || !std::isfinite(target[1]))
                break;
            // THE TURN RATE: the written direction moves at most maxStepRad from the record as the game's controller
            // left it this update, along the straight line to the target.
            if (maxStepRad > 0.0f)
            {
                const float dy = std::remainder(target[0] - pair[0], 6.28318531f);
                const float dp = target[1] - pair[1];
                const float across = dy * std::cos(pair[1]);
                const float angle = std::sqrt(across * across + dp * dp);
                if (std::isfinite(angle) && angle > maxStepRad)
                {
                    const float part = maxStepRad / angle;
                    target[0] = pair[0] + dy * part;
                    target[1] = pair[1] + dp * part;
                    d.capped.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // On the branch the game's yaw is on, then inside its own [0, 2 pi) (its action packer clamps a yaw
            // outside it); pitch inside the controller's own clamp.
            const float y = aim_write::wrap_0_2pi(aim_write::unwrap_to_branch(target[0], pair[0]));
            const float p = aim_write::clamp_pitch(target[1], 1.45f);
            if (!std::isfinite(y) || !std::isfinite(p))
                break;
            if (yawOwned)
                pair[0] = y;
            if (pitchOwned)
                pair[1] = p;
            const bool putAxes[2]{yawOwned, pitchOwned};
            const float putValues[2]{y, p};
            st.putBack.wrote(pc, putAxes, putValues, pub.context(), st.stickSeq, AimCallerClockNow());
            st.seatYawWriteMs.store(yawOwned ? now : 0, std::memory_order_release);
            st.seatPitchWriteMs.store(pitchOwned ? now : 0, std::memory_order_release);
            st.seatLimitedMs.store(0, std::memory_order_release);
            st.flightMs.store(now, std::memory_order_release);
            // The last two writes, as the world angles they put into effect (the input thread's witnesses).
            st.wroteSeq.fetch_add(1, std::memory_order_acq_rel);
            st.wroteYaw[1].store(st.wroteYaw[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
            st.wrotePitch[1].store(st.wrotePitch[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
            st.wroteValid[1].store(st.wroteValid[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
            st.wroteYaw[0].store(pair[0], std::memory_order_relaxed);
            st.wrotePitch[0].store(pair[1], std::memory_order_relaxed);
            st.wroteValid[0].store(true, std::memory_order_relaxed);
            st.wroteSeq.fetch_add(1, std::memory_order_release);
            d.wrote.fetch_add(1, std::memory_order_relaxed);
        } while (false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        st.putBack.invalidate();
        d.faults.fetch_add(1, std::memory_order_relaxed);
    }
    st.callbacks.fetch_sub(1, std::memory_order_acq_rel);
}

// Sim thread, after the game's controller has written the record. `entry`: the controller's own entry rsp;
// `dtA`: the controller's own time step for this update.
void Halo4FlightAfterController(int user, uint32_t vehicle, float dtA, const uintptr_t* entry)
{
    auto& d = g_halo4Flight;
    d.calls.fetch_add(1, std::memory_order_relaxed);
    if (user != 0)
        return;
    if (!Halo4FlightWanted())
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
    Halo4FlightState s{};
    if (!Halo4FlightRead(vehicle, s))
    {
        d.unread.fetch_add(1, std::memory_order_relaxed);
        d.vehicle = UINT32_MAX;
        d.state.store(0, std::memory_order_relaxed);
        return;
    }
    const uint64_t now = GetTickCount64();
    // A new flight: another craft, the game's tick gone back (a reload) or a silence (a death, a pause).
    if (vehicle != d.vehicle || s.tick < d.lastTick || now - d.lastCallMs > kHalo4FlightGapMs)
    {
        d.vehicle = vehicle;
        d.lastTick = s.tick;      // judged from the next tick on: nothing is known of the record before this call
        d.agree = d.misses = 0;
        d.proven = false;
        d.ends = 0;
        d.haveLast = false;
        d.stints.fetch_add(1, std::memory_order_relaxed);
    }
    d.lastCallMs = now;
    // The link, once a game tick: the ticks that just ran steered by the record as a call before them left it --
    // the last one, or the one before it where the craft's copy trails a tick more.
    if (s.tick != d.lastTick)
    {
        d.lastTick = s.tick;
        d.ticks.fetch_add(1, std::memory_order_relaxed);
        if (d.haveLast)
        {
            // The newest end record becomes this tick's reference; the one it replaces is kept as the older.
            d.endRecord[1][0] = d.endRecord[0][0];
            d.endRecord[1][1] = d.endRecord[0][1];
            d.endRecord[0][0] = d.lastRecord[0];
            d.endRecord[0][1] = d.lastRecord[1];
            d.ends = (std::min)(d.ends + 1, 2);
        }
        if (d.ends)
        {
            float off = Halo4FlightOffDeg(d.endRecord[0], s.facing);
            if (d.ends > 1)
            {
                const float older = Halo4FlightOffDeg(d.endRecord[1], s.facing);
                if (older < off)
                {
                    if (off >= kHalo4FlightLinkAgreeDeg)
                        d.lateTicks.fetch_add(1, std::memory_order_relaxed);
                    off = older;
                }
            }
            Halo4FlightMax(d.linkMaxDeg, off);
            if (off < kHalo4FlightLinkAgreeDeg)
            {
                d.misses = 0;
                if (d.agree < kHalo4FlightLinkTicks && ++d.agree == kHalo4FlightLinkTicks)
                    d.proven = true;
            }
            else
            {
                d.linkMisses.fetch_add(1, std::memory_order_relaxed);
                if (++d.misses >= kHalo4FlightLinkMissTicks)
                {
                    d.agree = 0;
                    d.proven = false;
                }
            }
        }
        Halo4FlightMax(d.noseMaxDeg, Halo4FlightAngleDeg(s.nose, s.facing));
    }
    d.state.store(d.proven ? 2u : 1u, std::memory_order_relaxed);
    if (d.proven)
    {
        // For the input thread: this craft's pilot is the drive's, so the seat's pitch is the hand's too.
        d.pilotVehicle.store(vehicle, std::memory_order_relaxed);
        d.pilotMs.store(now, std::memory_order_release);
        // Which of the dispatcher's callers this is.
        const uintptr_t dispatcherReturn = d.dispatcherFrame ? entry[d.dispatcherFrame / sizeof(uintptr_t)] : 0;
        const float step = std::isfinite(dtA) && dtA > 0.0f ? (std::min)(dtA, 0.1f) : s.secondsPerTick;
        const float maxStepRad = kHalo4FlightMaxTurnDegPerSecond * 0.01745329f * step;
        if (dispatcherReturn && dispatcherReturn == d.callerRet[2])
        {
            // THE UPDATE -- unless the game is steering the craft itself (a stamp younger than its window).
            const bool steering = s.stamp != -1 && s.tick >= s.stamp &&
                static_cast<float>(s.tick - s.stamp) * s.secondsPerTick < kHalo4FlightSteerWindowSeconds;
            (steering ? d.yielded : d.updates).fetch_add(1, std::memory_order_relaxed);
            Halo4FlightApply(steering ? aim_caller::Action::EndUpdate : aim_caller::Action::StepAndWrite, steering,
                             maxStepRad);
        }
        else if (dispatcherReturn && dispatcherReturn == d.callerRet[3])
        {
            d.noUnit.fetch_add(1, std::memory_order_relaxed);
            Halo4FlightApply(aim_caller::Action::EndUpdate, false, 0.0f);
        }
        else
        {
            // An extra call in the same update (or a caller this build does not know): the last write is put back.
            (dispatcherReturn && (dispatcherReturn == d.callerRet[0] || dispatcherReturn == d.callerRet[1])
                 ? d.extras : d.unknownCaller).fetch_add(1, std::memory_order_relaxed);
            Halo4FlightApply(aim_caller::Action::RestoreOnly, false, 0.0f);
        }
    }
    else
        d.waiting.fetch_add(1, std::memory_order_relaxed);   // nothing written or reported: the stick servo aims
    // The record as this call left it (after any write).
    __try
    {
        d.lastRecord[0] = s.record[0];
        d.lastRecord[1] = s.record[1];
        d.haveLast = std::isfinite(d.lastRecord[0]) && std::isfinite(d.lastRecord[1]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        d.haveLast = false;
    }
}

// MinHook's relay jumps here, so _AddressOfReturnAddress() is the native's own entry rsp. The callback count spans
// the whole call, the native included: the removal's quiescence proof covers it. The native takes seven arguments,
// three of them on the stack, and reads them at entry +0x28 / +0x30 / +0x38, so it is forwarded through
// CallNativeWithSpareSlots (native_forward.h).
__declspec(noinline) void Halo4Type13Detour(int user, uint32_t vehicle, float yawD, float pitchD, const void* input,
                                            float dtA, float dtB)
{
    const auto* entry = static_cast<const uintptr_t*>(_AddressOfReturnAddress());
    auto& d = g_halo4Flight;
#if HALOMCCVR_DEV_DIAGNOSTICS
    volatile bool observing = false;
#endif
    d.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const auto native = reinterpret_cast<Halo4FlightControllerFn>(d.original);
        if (!native)
            return;
#if HALOMCCVR_DEV_DIAGNOSTICS
        T13Call call{};
        observing = T13Halo4Before(user, vehicle, yawD, pitchD, input, dtA, dtB, entry, call);
#endif
        CallNativeWithSpareSlots(native, user, vehicle, yawD, pitchD, input, dtA, dtB);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (observing)
        {
            observing = false;
            T13Halo4After(call);   // releases the observer's busy flag; it reads what the native wrote, before ours
        }
#endif
        Halo4FlightAfterController(user, vehicle, dtA, entry);
    }
    __finally
    {
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (observing)
            T13Release(g_t13Halo4);
#endif
        d.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
} // namespace

// The controller's hook is installed and admitting calls (the dev observer rides it).
bool Halo4Type13HookEnabled()
{
    return g_halo4Flight.enabled.load(std::memory_order_acquire);
}

// Input thread: the flight drive has proven and is writing the pilot of this craft (within the last 150 ms), so a
// Halo 4 seat on it has its pitch written as well as its heading.
bool Halo4FlightPilotLive(uint32_t vehicle)
{
    const auto& d = g_halo4Flight;
    const uint64_t at = d.pilotMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return at && now >= at && now - at <= kHalo4FlightPilotFreshMs &&
           d.pilotVehicle.load(std::memory_order_relaxed) == vehicle;
}

// Worker, inside InstallHalo4CameraCore after LegacyDriveInstall (the record and the TLS index are the direct
// drive's own).
bool Halo4Type13Install(uintptr_t base, size_t size, uint32_t generation)
{
    auto& d = g_halo4Flight;
    if (d.target)
    {
        if (d.base == base)
        {
            d.generation.store(generation, std::memory_order_release);
            d.resetSeq.fetch_add(1, std::memory_order_acq_rel);
            d.enabled.store(true, std::memory_order_release);
            return true;
        }
        LOG("Halo 4 Broadsword direct drive: the pilot controller's hook is still installed on another module; not "
            "reinstalled");
        return false;
    }
    const auto& st = g_legacyDrive[2];
    if (!st.installed.load(std::memory_order_acquire) || !st.tlsIndexAddress || st.base != base)
    {
        LOG("Halo 4 Broadsword direct drive: the pilot's controller (recorded at +0x%X) was NOT hooked (the direct "
            "drive's own hook is not installed on this module, so there is no record to write through); a "
            "Broadsword's pilot keeps the stick servo", kHalo4FlightEntryRecorded);
        return false;
    }
    // WHERE EVERYTHING IS IN THIS MODULE (drive_sites.h, by the rungs of drive_known.h): verified at the addresses
    // it is known to stand at, or found by search; every proof is made on those addresses either way. Nothing
    // below trusts a recorded RVA.
    drive_cascade::Resolution<drive_sites::Halo4Flight> resolution;
    const bool opened = drive_cascade::Resolve(
        base, size, drive_cascade::Drive{GameTitle::Halo4, "pilot-controller"}, resolution,
        [](drive_resolve::Resolver& r) { return drive_sites::ResolveHalo4Flight(r); });
    const drive_sites::Flight& found = resolution.found.flight;
    if (opened)
        drive_cascade::Announce(drive_cascade::kHalo4FlightName, resolution);
    // THE DECISION (drive_install.h; the offline check runs the same function on halo4.dll as the loader maps it):
    // every row found; the dispatcher's call landing on the controller; the controller's TLS index the module's
    // own (the resolution proved its load names it) and the direct drive's (so its record is the one written
    // here); the frame through which the dispatcher's own return address is read from the controller's entry rsp
    // out of the loaded module's function table and the one the resolution proved; the stick update's caller.
    const drive_install::FlightDecision decision = drive_install::DecideFlight(
        base, size, opened, kHalo4FlightAnchors, resolution.found.rows, found, st.tlsIndexAddress, false, 4, 2);
    const char* const failed = decision.failed;
    const bool anchorsOk = decision.rowsOk, callOk = decision.callOk, tlsOk = decision.callOk && decision.tlsOk,
               frameOk = decision.frameOk;
    const uint32_t frame = decision.frame;
    const char* const frameWhy = decision.frameWhy;
    const uintptr_t* const callerRet = decision.callerRet;
    if (!decision.hook)
    {
        char first[280] = "";
        if (opened)
            drive_cascade::FirstFailure(resolution.report, first, sizeof(first));
        LOG("Halo 4 Broadsword direct drive: the pilot's controller (recorded at +0x%X) was NOT hooked (%s; anchors "
            "failed: %s; dispatcher call=%d tls=%d frame=%d%s%s stick-update caller=%d); a Broadsword's pilot keeps "
            "the stick servo, nothing else changes",
            kHalo4FlightEntryRecorded, opened ? first : (resolution.why ? resolution.why : "the module did not open"),
            failed[0] ? failed : "none", callOk, tlsOk, frameOk, frameWhy ? ": " : "", frameWhy ? frameWhy : "",
            callerRet[2] ? 1 : 0);
        return false;
    }
    void* target = reinterpret_cast<void*>(base + found.entry);
    const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(&Halo4Type13Detour), &d.original);
    if (created != MH_OK)
    {
        d.original = nullptr;
        LOG("Halo 4 Broadsword direct drive: the pilot controller's hook FAILED (create=%d); a Broadsword's pilot "
            "keeps the stick servo", static_cast<int>(created));
        return false;
    }
    // Everything the hook reads is set before it can run.
    d.base = base;
    d.entryRva = found.entry;
    d.dispatcherFrame = frame;
    for (int i = 0; i < 4; ++i)
        d.callerRet[i] = callerRet[i];
    d.generation.store(generation, std::memory_order_release);
    d.resetSeq.fetch_add(1, std::memory_order_acq_rel);
    if (MH_EnableHook(target) != MH_OK)
    {
        MH_RemoveHook(target);
        d.original = nullptr;
        LOG("Halo 4 Broadsword direct drive: the pilot controller's hook FAILED to enable; removed, a Broadsword's "
            "pilot keeps the stick servo");
        return false;
    }
    d.target = target;
    d.enabled.store(true, std::memory_order_release);
    LOG("Halo 4 Broadsword direct drive: the pilot's controller +0x%X is hooked (dispatcher return at entry rsp+0x%X; "
        "callers: extra %d, sway %d, stick update 1, no unit %d). After the game's own controller the hand's yaw and "
        "pitch are written to the direct drive's record (keys: aim_direct_drive, seat_direct_drive, reach_vehicle_aim "
        "2, and the first-person seat view), once a flight has shown that record to be what the craft steers by; at "
        "most %.0f deg/s; roll, and the game's own steering of the craft, stay the game's",
        found.entry, frame, callerRet[0] ? 1 : 0, callerRet[1] ? 1 : 0, callerRet[3] ? 1 : 0,
        static_cast<double>(kHalo4FlightMaxTurnDegPerSecond));
    return true;
}

// Worker, inside RemoveHalo4CameraCore before LegacyDriveRemove: admission off, disable without touching foreign
// bytes, prove quiescence (callback count + frozen-thread ingress scan of the detour and the trampoline), then
// remove; the trampoline is forgotten only after MH_RemoveHook.
bool Halo4Type13Remove()
{
    auto& d = g_halo4Flight;
    d.enabled.store(false, std::memory_order_release);
    d.pilotMs.store(0, std::memory_order_release);
    if (!d.target)
        return true;
    const MH_STATUS disabled = MCCVR_DisableHookForRetirement(d.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
        return false;
    const void* functions[]{reinterpret_cast<const void*>(&Halo4Type13Detour)};
    const void* trampolines[]{d.original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, d.callbacks))
        return false;
    const MH_STATUS removed = MH_RemoveHook(d.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
        return false;
    d.target = nullptr;
    d.original = nullptr;
    return true;
}

// Cold worker, with the direct drive's own report: what the pilot's detour did in the last window.
//
// NO HOOK WATCHDOG HERE (drive_cascade.h: HookWatch), deliberately. A watchdog may count only time in which the
// hooked code must have been called, and for this hook that is "the player pilots a Broadsword" -- which nothing
// but the hook itself knows: Halo 4's vehicles and seat roles are not mapped, and the aim provider learns of a
// flight from Halo4FlightPilotLive, that is from this hook. "Seated" alone would count every other vehicle against
// a healthy address. A controller that is never called shows as no "DRIVE Halo 4 Broadsword" line during a flight.
void Halo4FlightDriveReport()
{
    auto& d = g_halo4Flight;
    static unsigned long long s_last = 0;
    const unsigned long long now = GetTickCount64();
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
    const uint32_t late = d.lateTicks.exchange(0, std::memory_order_relaxed);
    const uint32_t yielded = d.yielded.exchange(0, std::memory_order_relaxed);
    const uint32_t updates = d.updates.exchange(0, std::memory_order_relaxed);
    const uint32_t wrote = d.wrote.exchange(0, std::memory_order_relaxed);
    const uint32_t noAxes = d.noAxes.exchange(0, std::memory_order_relaxed);
    const uint32_t stale = d.stale.exchange(0, std::memory_order_relaxed);
    const uint32_t extras = d.extras.exchange(0, std::memory_order_relaxed);
    const uint32_t noUnit = d.noUnit.exchange(0, std::memory_order_relaxed);
    const uint32_t unknown = d.unknownCaller.exchange(0, std::memory_order_relaxed);
    const uint32_t capped = d.capped.exchange(0, std::memory_order_relaxed);
    const uint32_t putBacks = d.putBacks.exchange(0, std::memory_order_relaxed);
    const uint32_t faults = d.faults.exchange(0, std::memory_order_relaxed);
    const float nose = d.noseMaxDeg.exchange(0.0f, std::memory_order_relaxed);
    const float link = d.linkMaxDeg.exchange(0.0f, std::memory_order_relaxed);
    if (!calls)
        return;
    const uint32_t state = d.state.load(std::memory_order_relaxed);
    LOG("DRIVE Halo 4 Broadsword: %u controller calls in %u ticks (new flights %u); the record %s (the craft's facing "
        "stood at most %.2f deg off it, %u ticks out of agreement, %u matched only the call before); updates %u: wrote "
        "%u (%u held to the turn rate), no axis the hand's %u, no fresh aim %u; left to the game's own steering %u, "
        "waiting for the link %u, keys off %u, unreadable %u; other callers: extra %u (put back %u), no unit %u, "
        "unknown %u; faults %u; the craft's nose trailed its facing by at most %.1f deg",
        calls, ticks, stints,
        state == 2 ? "is PROVEN to be what the craft steers by" :
            state == 1 ? "is not yet proven to be what the craft steers by" : "is not being judged",
        static_cast<double>(link), misses, late, updates, wrote, capped, noAxes, stale, yielded, waiting, gated, unread,
        extras, putBacks, noUnit, unknown, faults, static_cast<double>(nose));
}
#else
void Halo4FlightDriveReport() {}
#endif // HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
