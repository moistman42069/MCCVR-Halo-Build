// Included in game.cpp (anonymous namespace) right after halo4_seat_converge.inl, whose sample, seated trace and
// solve it witnesses; the muzzle lane's view, aim and fire detours call into it.
//
// HALO 4 SEAT WITNESSES. DEV BUILDS ONLY: a player build compiles none of this file. Seated convergence itself --
// the aim-camera sample, the seated trace and the solve -- is halo4_seat_converge.inl, in every build; nothing
// here decides anything. How Halo 4 fires from a seat and every pure decision these witnesses use are in
// src/common/halo4_seat_aim_logic.h; this file is their engine half:
//
//   ANCHORS (InstallHalo4SeatSight, after the muzzle and the convergence lanes). Each is a unique signature plus
//   the native call edge that proves its arguments, each its own feature bit, each failing closed with one LOG line:
//     seat flags    0x5FB2D4 (seat, body) + the dispatcher's call 0xA88F1        -> class Unknown
//     controlled    0x5F9694 + 0x1DC9E4's own call 0x1DCA0E                      -> class Unknown
//     object frame  0x5D84D0 + the record rebase's call 0x5F98DB (0x5F98C2)      -> the slope witness
//     barrel        0x6121B8 + the fire routine's marker block 0x61797F          -> assist verdict unknown
//     aim unit      the fire routine's aim-unit block 0x617DA7 (+0x49C)          -> assist verdict unknown
//     aim camera    the convergence lane's own proof (0x2061A8 whole, its selector call, 0x1DCA25)
//                                                                                -> no first-person witness, no census
//     assist exit   0x1DCB8C + the fire routine's call 0x618103 (a dev hook)     -> no SHOT Halo 4 seated line
//     shot line     0x5F3510's unit-aim block (0x5F351B..0x5F3601: +0x4EC/+0x4E0 into its direction argument)
//                   + the fire routine's aim call 0x617E60 (direction [rbp+0x30], call 0x617E90) and assist
//                   call 0x6180CC (argument 8 = the same [rbp+0x30], call 0x618103)   -> assist verdict unknown
//     (0x5F3510's first bytes carry the muzzle lane's hook: its entry was proven by that lane before hooking,
//     and this signature starts past the patched bytes.)
//   The weapon-slot, owner, aiming-unit and barrel-marker natives (0x5FA1C4, 0x612238, 0x5F1ABC, 0x610F6C) and
//   the marker query 0x5D5B74 are the muzzle lane's own proven bindings; without that lane nothing here runs.
//
//   THE CENSUS SAMPLE (Halo4SeatSightNoteAimCamera, from the muzzle's view detour on the game's targeting calls
//   0x1DB9A6 / 0x1DBDB6, never on the mod's own query or a barrel shot): the game's own aim camera for the
//   local unit, the first-person witness (which camera the selector chose for that call), the seat flags, the
//   controlled unit, the vehicle's up, whether the aiming unit carries the player, the barrel's forward and its
//   offset from the camera, and the SHOT LINE the native assist will turn (the barrel's forward or the aiming
//   unit's aim, whichever the last fire of this weapon was handed). Seqlock. It calls natives the convergence
//   lane does not, which is why it is a dev census and not a part of that lane.
//
//   THE SHOT FORM (Halo4SeatSightNoteFiringAim, the muzzle's aim detour on the fire routine's call 0x617E90):
//   per fire of the local seat's weapon, the unit-aim flag the native aim call was actually handed. Halo 4
//   decides it from owner+0x694, a barrel flag in the weapon tag and an actor test; the mod never re-derives
//   it, it applies what the last fire of the same weapon, owner and aiming unit was handed.
//
//   THE RENDER EYE (Halo4PublishSeatEyeRoot, from the stereo transaction): the controller root plus the native
//   seat eye's render-time offset (NativeVehicleFirstPerson_SeatEyeDelta), judged against the census sample
//   (halo4_seat_aim::JudgeEye). It agrees only where the simulation aims the seat from its first-person eye: a
//   seat the game shows in first person, or any seat with coop_safe_shots off. Where the simulation aims from
//   its following camera the sample's witness says "chase", and the claim witness and the assessment below --
//   which reason from that eye -- stand down. The convergence lane solves from the sample itself in both cases.
//
//   THE SHOT WITNESS (Halo4SeatAssistDetour, 0x1DCB8C filtered to the fire routine's return 0x618108):
//   the shot's direction before and after the native seated assist, against the converged point (the seated
//   trace's own last result, which the convergence lane hands over: Halo4PublishSeatTrace).
#if HALOMCCVR_DEV_DIAGNOSTICS

using Halo4SeatFlagFn = uint8_t(__fastcall*)(uint32_t, int16_t, uint32_t);       // 0x5FB2D4(vehicle, seat, bit)
using Halo4SeatHandleFn = uint32_t(__fastcall*)(uint32_t);                         // 0x5F9694 0x6121B8 0x612238 0x5F1ABC
using Halo4SeatObjectFrameFn = void(__fastcall*)(uint32_t, float*, float*);        // 0x5D84D0(object, forward, up)
using Halo4SeatWeaponInSlotFn = uint32_t(__fastcall*)(uint32_t, int32_t);          // 0x5FA1C4(unit, role)
using Halo4SeatBarrelMarkerFn = uint32_t(__fastcall*)(uint32_t, int32_t);          // 0x610F6C(weapon, barrel)
using Halo4SeatAssistFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                                uint64_t, uint64_t);               // 0x1DCB8C, 8 arguments

enum : uint32_t
{
    kH4SeatFlags = 1u << 0,
    kH4SeatControlled = 1u << 1,
    kH4SeatObjectFrame = 1u << 2,
    kH4SeatBarrel = 1u << 3,
    kH4SeatAimUnit = 1u << 4,
    kH4SeatAimCamera = 1u << 5,
    kH4SeatAssistWitness = 1u << 6,
    kH4SeatShotLine = 1u << 7,
    kH4SeatAllAnchors = (1u << 8) - 1u,
};

// The aiming unit's aim vectors 0x5F3510 copies into the shot's direction under the unit-aim flag (0x5F35D5;
// 0x5F35EC for a simulation-1 fire). Proven by the shot-line anchor; read only behind kH4SeatShotLine.
inline constexpr size_t kH4UnitAimVector = 0x4EC;
inline constexpr size_t kH4UnitAimVectorSimulation = 0x4E0;

// The simulation sample (one per tick for the local unit). World units; plain data, copied whole.
struct Halo4SeatAimSample
{
    uint32_t sequence = 0, generation = 0;
    uint64_t ms = 0;
    uint32_t unit = UINT32_MAX, parent = UINT32_MAX;
    int16_t seat = -1;
    bool seated = false;
    float origin[3]{}, camera[3]{}, dir[3]{};   // 0x1DC9E4's outputs: the eye (chase: pushed), the camera, its aim
    int8_t witness = -1;                        // 1 exact first person, 0 the selector chose chase, -1 no proof
    bool pushed = false;                        // the chase push moved the eye off the camera
    float speedMps = -1.0f;                     // the camera's speed over the last tick, metres/s (-1 unknown:
                                                // the first sample of a stint, or after a pause or hitch)
    bool flagsKnown = false;
    uint32_t flags = 0;
    uint32_t controlled = UINT32_MAX;
    halo4_seat_aim::Controlled controlledKind = halo4_seat_aim::Controlled::Unknown;
    bool upKnown = false;
    float up[3]{};
    bool weaponKnown = false, hasWeapon = false;
    uint32_t weapon = UINT32_MAX, owner = UINT32_MAX, aiming = UINT32_MAX;   // the shot's weapon, owner, aiming unit
    bool aimUnitKnown = false, assisted = false;   // the aiming unit is this unit and carries its player
    bool barrelKnown = false;
    float barrelForward[3]{}, barrelOffset[3]{};   // the barrel marker's forward, and its origin minus `camera`
    // The line the native assist will turn: the barrel's forward, or the aiming unit's aim
    // vector when the last fire of this weapon/owner/aiming unit was handed the unit-aim flag.
    halo4_seat_aim::ShotLine shotLine = halo4_seat_aim::ShotLine::Unknown;
    bool shotSimulationVector = false;            // the unit-aim line is +0x4E0 (a simulation-1 fire)
    bool shotKnown = false;
    float shotForward[3]{};
};

struct Halo4SeatSightRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    std::atomic<uint32_t> ready{0};
    Halo4SeatFlagFn seatFlag = nullptr;
    Halo4SeatHandleFn controlled = nullptr, markerOwner = nullptr, weaponOwner = nullptr, aimingUnit = nullptr;
    Halo4SeatObjectFrameFn objectFrame = nullptr;
    Halo4SeatWeaponInSlotFn weaponInSlot = nullptr;
    Halo4SeatBarrelMarkerFn barrelMarker = nullptr;
    Halo4MuzzleMarkersFn markers = nullptr;
    // The assist-exit witness hook (dev).
    void* assistTarget = nullptr;
    Halo4SeatAssistFn assistOriginal = nullptr;
    std::atomic<uint32_t> assistCallbacks{0};
    // The widest turn the native seated assist was seen to make for one weapon: the weapon's handle in the low
    // half, the angle's float bits in the high half (a lower bound of that weapon's real cap).
    std::atomic<uint64_t> widestTurn{0};
    // The sample: a seqlock with a try-lock writer (two per-tick targeting sites, one thread in practice).
    std::atomic_flag sampleWriting = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> sampleVersion{0};
    Halo4SeatAimSample sample{};   // written only between the odd and even version stores
    Halo4SeatAimSample last{};     // the writer's own previous sample (the speed)
    uint64_t lastQpc = 0;
    // Counters since the last report (dev lines).
    std::atomic<uint64_t> samples{0}, samplesSeated{0}, firstPerson{0}, chase{0}, noWitness{0}, pushes{0},
        sampleFaults{0}, writerBusy{0};
    std::atomic<uint32_t> lastFlags{0}, lastClass{0}, lastControlledKind{0}, lastAssisted{2}, lastBarrel{0},
        lastUpZ1e4{0}, lastShotLine{0}, lastShotKnown{0};
} g_h4SeatSight;

// ---- the solve's eye (render transaction -> input thread) ---------------------------------------------------
struct Halo4SeatEyeRootPublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0}, parent{UINT32_MAX};
    std::atomic<int32_t> seat{-1};
    std::atomic<uint64_t> ms{0};
    std::atomic<float> position[3]{}, delta[3]{};
};
Halo4SeatEyeRootPublication g_halo4SeatEyeRoot;
// A render-time offset older than this is not this frame's camera any more.
inline constexpr uint64_t kHalo4SeatEyeDeltaMaxAgeMs = 50;

// Stereo transaction (render thread): the game's own aim-line origin for this frame's seat, as the controller
// root (the observer before head pose: head plus trims in an owned seat) plus the native eye's offset from
// that placed point. Nothing when the seat stamp is not seated or the offset is not this seat's or is stale.
void Halo4PublishSeatEyeRoot(bool seatRead, const Halo4VehicleInputState& seat, const float rootPosition[3])
{
    NativeVehicleSeatEyeDelta d{};
    const uint64_t now = GetTickCount64();
    if (!seatRead || !seat.seated || seat.parent == UINT32_MAX || seat.seat < 0 ||
        !NativeVehicleFirstPerson_SeatEyeDelta(GameTitle::Halo4, d) || d.parent != seat.parent ||
        d.seat != seat.seat || now < d.ms || now - d.ms > kHalo4SeatEyeDeltaMaxAgeMs)
        return;
    float position[3]{};
    for (int axis = 0; axis < 3; ++axis)
    {
        position[axis] = rootPosition[axis] + d.delta[axis];
        if (!std::isfinite(position[axis]))
            return;
    }
    auto& p = g_halo4SeatEyeRoot;
    p.sequence.fetch_add(1, std::memory_order_acq_rel);
    p.generation.store(d.generation, std::memory_order_relaxed);
    p.parent.store(d.parent, std::memory_order_relaxed);
    p.seat.store(d.seat, std::memory_order_relaxed);
    for (int axis = 0; axis < 3; ++axis)
    {
        p.position[axis].store(position[axis], std::memory_order_relaxed);
        p.delta[axis].store(d.delta[axis], std::memory_order_relaxed);
    }
    p.ms.store(now, std::memory_order_relaxed);
    p.sequence.fetch_add(1, std::memory_order_release);
}

// Any thread: the solve's eye for exactly this seat, at most maxAgeMs old.
bool Halo4LoadSeatEyeRoot(uint32_t parent, int seat, uint64_t maxAgeMs, float out[3], float delta[3])
{
    const auto& p = g_halo4SeatEyeRoot;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = p.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        float position[3]{}, d[3]{};
        for (int axis = 0; axis < 3; ++axis)
        {
            position[axis] = p.position[axis].load(std::memory_order_relaxed);
            d[axis] = p.delta[axis].load(std::memory_order_relaxed);
        }
        const uint32_t generation = p.generation.load(std::memory_order_relaxed);
        const uint32_t publishedParent = p.parent.load(std::memory_order_relaxed);
        const int32_t publishedSeat = p.seat.load(std::memory_order_relaxed);
        const uint64_t at = p.ms.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.sequence.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t now = GetTickCount64();
        if (!at || now < at || now - at > maxAgeMs || publishedParent != parent || publishedSeat != seat ||
            generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
            return false;
        memcpy(out, position, sizeof(position));
        if (delta)
            memcpy(delta, d, sizeof(d));
        return true;
    }
    return false;
}

// ---- the simulation sample -------------------------------------------------------------------------------
bool Halo4ReadSeatAimSample(Halo4SeatAimSample& out)
{
    const auto& f = g_h4SeatSight;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = f.sampleVersion.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        Halo4SeatAimSample s;
        memcpy(&s, &f.sample, sizeof(s));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (f.sampleVersion.load(std::memory_order_relaxed) != before)
            continue;
        out = s;
        return true;
    }
    return false;
}

bool Halo4SeatFinite3(const float* v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// ---- the shot form (fire routine -> the sample) ---------------------------------------------------------------
// The last fire of the local seat's weapon (barrel 0): what the aim call 0x5F3510 was handed. A try-lock seqlock;
// the fire routine and the view detour both run on the simulation thread in practice.
struct Halo4SeatShotFormRecord
{
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0}, weapon{UINT32_MAX}, owner{UINT32_MAX}, aiming{UINT32_MAX},
        simulation{0}, flags{0};   // flags: bit 0 the unit-aim flag, bit 1 the mod's muzzle shot
} g_halo4SeatShotForm;

void Halo4PublishSeatShotForm(uint32_t generation, uint32_t weapon, uint32_t owner, uint32_t aiming,
                              uint32_t simulation, bool unitAim, bool substituted)
{
    auto& r = g_halo4SeatShotForm;
    if (r.writing.test_and_set(std::memory_order_acquire))
        return;   // another fire is writing it: that one stands
    r.sequence.fetch_add(1, std::memory_order_acq_rel);
    r.generation.store(generation, std::memory_order_relaxed);
    r.weapon.store(weapon, std::memory_order_relaxed);
    r.owner.store(owner, std::memory_order_relaxed);
    r.aiming.store(aiming, std::memory_order_relaxed);
    r.simulation.store(simulation, std::memory_order_relaxed);
    r.flags.store((unitAim ? 1u : 0u) | (substituted ? 2u : 0u), std::memory_order_relaxed);
    r.sequence.fetch_add(1, std::memory_order_release);
    r.writing.clear(std::memory_order_release);
}

bool Halo4ReadSeatShotForm(halo4_seat_aim::ShotForm& out, uint32_t& simulation)
{
    const auto& r = g_halo4SeatShotForm;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = r.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        halo4_seat_aim::ShotForm form{};
        form.known = true;
        form.generation = r.generation.load(std::memory_order_relaxed);
        form.weapon = r.weapon.load(std::memory_order_relaxed);
        form.owner = r.owner.load(std::memory_order_relaxed);
        form.aiming = r.aiming.load(std::memory_order_relaxed);
        const uint32_t sim = r.simulation.load(std::memory_order_relaxed);
        const uint32_t flags = r.flags.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (r.sequence.load(std::memory_order_relaxed) != before)
            continue;
        form.unitAim = (flags & 1u) != 0;
        form.substituted = (flags & 2u) != 0;
        out = form;
        simulation = sim;
        return true;
    }
    return false;
}

// Simulation thread, inside the view detour's callbacks lease. Every native here is one the anchors proved,
// called only with a handle the salt-checked object table still holds (they validate nothing themselves).
// Guarded; plain data only, so __try is legal.
bool Halo4SeatSightBuildSample(uint32_t unit, const Halo4VehicleInputState& seat, const float* origin,
                               const float* dir, const float* camera, const NativeVehicleSelectWitness& witness,
                               uint32_t ready, Halo4SeatAimSample& s)
{
    auto& f = g_h4SeatSight;
    __try
    {
        memcpy(s.origin, origin, sizeof(s.origin));
        memcpy(s.camera, camera, sizeof(s.camera));
        memcpy(s.dir, dir, sizeof(s.dir));
        if (!Halo4SeatFinite3(s.origin) || !Halo4SeatFinite3(s.camera) || !Halo4SeatFinite3(s.dir))
            return false;
        s.unit = unit;
        s.parent = seat.parent;
        s.seat = seat.seat;
        s.seated = seat.seated;
        const float pushed[3]{s.origin[0] - s.camera[0], s.origin[1] - s.camera[1], s.origin[2] - s.camera[2]};
        s.pushed = pushed[0] * pushed[0] + pushed[1] * pushed[1] + pushed[2] * pushed[2] > 1.0e-8f;
        const uint8_t* unitData = Halo4MuzzleObject(unit, 1);
        if (!unitData)
            return false;
        // THE FIRST-PERSON WITNESS: exactly one selector call, for this unit, nested in this view call; and
        // 0x2061A8 then took the selector's branch, not the unit's camera override (+0x490 bit 31, which it
        // tests AFTER the selector call: the aim-camera anchor proves the offset and the order).
        if (witness.calls == 1 && witness.unit == unit)
        {
            uint32_t override490 = 0;
            memcpy(&override490, unitData + 0x490, sizeof(override490));
            s.witness = (override490 >> 31) ? int8_t(-1) : (witness.result == 0 ? int8_t(1) : int8_t(0));
        }
        if (!s.seated)
            return true;
        if (ready & kH4SeatFlags)
        {
            for (const uint8_t bit : halo4_seat_aim::kSeatFlagBitsRead)
                if (f.seatFlag(seat.parent, seat.seat, bit))
                    s.flags |= 1u << bit;
            s.flagsKnown = true;
        }
        if (ready & kH4SeatObjectFrame)
        {
            float up[3]{};
            f.objectFrame(seat.parent, nullptr, up);
            const float l = up[0] * up[0] + up[1] * up[1] + up[2] * up[2];
            if (Halo4SeatFinite3(up) && l > 0.81f && l < 1.21f)
            {
                memcpy(s.up, up, sizeof(up));
                s.upKnown = true;
            }
        }
        if (!(ready & kH4SeatControlled))
            return true;
        s.controlled = f.controlled(unit);
        const uint8_t* controlledData =
            s.controlled != UINT32_MAX ? Halo4MuzzleObject(s.controlled, 0x2003) : nullptr;
        if (!controlledData)
        {
            s.controlled = UINT32_MAX;
            return true;
        }
        s.controlledKind = halo4_seat_aim::ClassifyControlled(static_cast<int32_t>(s.controlled),
                                                              static_cast<int32_t>(unit),
                                                              static_cast<int32_t>(seat.parent));
        // The controlled unit's current weapon: its role byte, as the selector (0x10D3BB) and the muzzle lane
        // read it, then the native slot lookup. 0xFF is the native "no weapon".
        const int8_t role = static_cast<int8_t>(controlledData[0x63A]);
        if (role != -1 && (role < 0 || role > 3))
            return true;
        const uint32_t weapon = f.weaponInSlot(s.controlled, role);
        if (weapon != UINT32_MAX && !Halo4MuzzleObject(weapon, 4))
            return true;
        s.weaponKnown = true;
        s.hasWeapon = weapon != UINT32_MAX;
        if (!s.hasWeapon)
            return true;
        s.weapon = weapon;
        if (ready & kH4SeatAimUnit)
        {
            // THE SHOT'S AIMING UNIT, exactly as the fire routine names it (0x617DAA): the weapon's owner, then
            // its controlling-unit chain. The native seated assist runs only when that unit carries a player.
            const uint32_t owner = f.weaponOwner(weapon);
            if (owner != UINT32_MAX && Halo4MuzzleObject(owner, UINT32_MAX))
            {
                const uint32_t aiming = f.aimingUnit(owner);
                s.aimUnitKnown = true;
                s.owner = owner;
                s.aiming = aiming;
                if (aiming == unit)
                {
                    int32_t player = -1;
                    memcpy(&player, unitData + 0x49C, sizeof(player));
                    s.assisted = player != -1;
                }
            }
        }
        if (ready & kH4SeatBarrel)
        {
            // THE BARREL, exactly as the fire routine reads it for barrel 0 (0x617986 -> 0x6179B2): the barrel's
            // marker name on the object that carries the weapon's markers. A missing marker is refused (the
            // native fills a fallback transform; for names 0 and 0x22D it even counts it).
            const uint32_t name = f.barrelMarker(weapon, 0);
            const uint32_t carrier = f.markerOwner(weapon);
            if (name && name != 0x22D && carrier != UINT32_MAX && Halo4MuzzleObject(carrier, UINT32_MAX))
            {
                alignas(16) uint8_t record[0x100]{};
                const int16_t count = f.markers(carrier, name, record, 1, 0, 0, 0);
                float forward[3]{}, position[3]{};
                memcpy(forward, record + 0x3C, sizeof(forward));
                memcpy(position, record + 0x60, sizeof(position));
                const float l = forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2];
                if (count >= 1 && Halo4SeatFinite3(forward) && Halo4SeatFinite3(position) && l > 0.81f && l < 1.21f)
                {
                    memcpy(s.barrelForward, forward, sizeof(forward));
                    for (int axis = 0; axis < 3; ++axis)
                        s.barrelOffset[axis] = position[axis] - s.camera[axis];
                    s.barrelKnown = true;
                }
            }
        }
        if ((ready & kH4SeatShotLine) && s.aimUnitKnown && s.barrelKnown)
        {
            // THE SHOT LINE: what the native seated assist will turn. The barrel's forward,
            // unless the last fire of this weapon, owner and aiming unit was handed the unit-aim flag: then
            // 0x5F3510 replaced it with the aiming unit's aim vector, read here exactly as it reads it. Before
            // that fire (or for another weapon) the line is unknown: the assist witness cannot judge the shot.
            halo4_seat_aim::ShotForm form{};
            uint32_t simulation = 0;
            if (Halo4ReadSeatShotForm(form, simulation))
                s.shotLine = halo4_seat_aim::ResolveShotLine(form, s.generation, weapon, s.owner, s.aiming);
            if (s.shotLine == halo4_seat_aim::ShotLine::Barrel)
            {
                memcpy(s.shotForward, s.barrelForward, sizeof(s.shotForward));
                s.shotKnown = true;
            }
            else if (s.shotLine == halo4_seat_aim::ShotLine::UnitAim)
            {
                s.shotSimulationVector = simulation == 1;
                const uint8_t* aimingData = Halo4MuzzleObject(s.aiming, 0x2003);
                if (aimingData)
                {
                    float v[3]{};
                    memcpy(v, aimingData + (s.shotSimulationVector ? kH4UnitAimVectorSimulation : kH4UnitAimVector),
                           sizeof(v));
                    const float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                    if (Halo4SeatFinite3(v) && l > 0.81f && l < 1.21f)
                    {
                        memcpy(s.shotForward, v, sizeof(v));
                        s.shotKnown = true;
                    }
                }
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        f.sampleFaults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

// The muzzle's view detour, before the native call: arm the first-person witness for a per-tick targeting
// call that is neither the mod's own query nor a barrel shot.
bool Halo4SeatSightArmAimCamera(uintptr_t caller)
{
    const auto& f = g_h4SeatSight;
    const uintptr_t base = g_halo4Barrel.base;
    if (!(f.ready.load(std::memory_order_acquire) & kH4SeatAimCamera) || !base ||
        (caller != base + 0x1DB9A6 && caller != base + 0x1DBDB6) || g_halo4MuzzleOwnedQuery ||
        g_halo4BarrelShot.active || TitleAdapter_GetActiveTitle() != GameTitle::Halo4)
        return false;
    NativeVehicleFirstPerson_ArmSelectWitness();
    return true;
}

// Armed view calls closer than this to the speed's reference sample belong to the same simulation tick: one
// tick spaces them by microseconds, while ticks are ~33 ms apart (17 ms even at 60 Hz).
inline constexpr double kHalo4SeatSameTickS = 0.008;

// The muzzle's view detour, after the native call and BEFORE any barrel substitution touches the outputs.
void Halo4SeatSightNoteAimCamera(uint32_t unit, const float* origin, const float* dir, const float* camera)
{
    const NativeVehicleSelectWitness witness = NativeVehicleFirstPerson_TakeSelectWitness();
    auto& f = g_h4SeatSight;
    const uint32_t ready = f.ready.load(std::memory_order_acquire);
    if (!(ready & kH4SeatAimCamera) || !origin || !dir || !camera ||
        f.generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
        return;
    // The local unit only: a remote co-op unit is never sampled.
    Halo4VehicleInputState seat{};
    if (!Halo4ReadVehicleInput(seat) || seat.unit != unit)
        return;
    Halo4SeatAimSample s{};
    s.generation = f.generation;
    s.ms = GetTickCount64();
    if (!Halo4SeatSightBuildSample(unit, seat, origin, dir, camera, witness, ready, s))
        return;
    LARGE_INTEGER qpc{}, frequency{};
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&frequency);
    if (f.sampleWriting.test_and_set(std::memory_order_acquire))
    {
        f.writerBusy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // The camera's speed since this writer's previous sample of the same occupant and seat (same clock). One
    // tick makes several armed view calls microseconds apart (0x1DB840's own, the live target track's
    // 0x1DBCB0 -> 0x1DBDB1, and the dispatcher's second 0x1DB840 at 0xA3723), and the input thread reads the
    // LAST of them: a call this close to the reference sample is the same tick, so it carries the reference's
    // speed and never becomes the reference itself (a same-tick pair measures no travel over no time).
    bool sameTick = false;
    if (f.lastQpc && f.last.unit == s.unit && f.last.parent == s.parent && f.last.seat == s.seat &&
        frequency.QuadPart > 0 && qpc.QuadPart >= static_cast<LONGLONG>(f.lastQpc))
    {
        const double dt = double(qpc.QuadPart - static_cast<LONGLONG>(f.lastQpc)) / double(frequency.QuadPart);
        const float d[3]{s.camera[0] - f.last.camera[0], s.camera[1] - f.last.camera[1],
                         s.camera[2] - f.last.camera[2]};
        if (dt < kHalo4SeatSameTickS)
        {
            sameTick = true;
            s.speedMps = f.last.speedMps;
        }
        else if (dt < 0.5)
            s.speedMps = static_cast<float>(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 3.048 / dt);
    }
    if (!sameTick)
    {
        f.last = s;
        f.lastQpc = static_cast<uint64_t>(qpc.QuadPart);
    }
    s.sequence = f.sampleVersion.load(std::memory_order_relaxed) / 2u + 1u;
    f.sampleVersion.fetch_add(1, std::memory_order_acq_rel);   // odd: writing
    memcpy(&f.sample, &s, sizeof(s));
    f.sampleVersion.fetch_add(1, std::memory_order_release);   // even: published
    f.sampleWriting.clear(std::memory_order_release);
    f.samples.fetch_add(1, std::memory_order_relaxed);
    if (s.seated)
    {
        f.samplesSeated.fetch_add(1, std::memory_order_relaxed);
        (s.witness > 0 ? f.firstPerson : s.witness == 0 ? f.chase : f.noWitness).fetch_add(1, std::memory_order_relaxed);
        if (s.pushed)
            f.pushes.fetch_add(1, std::memory_order_relaxed);
        f.lastFlags.store(s.flagsKnown ? s.flags : 0xFFFFFFFFu, std::memory_order_relaxed);
        f.lastClass.store(static_cast<uint32_t>(halo4_seat_aim::ClassifySeat(s.flagsKnown, s.flags, s.controlledKind,
            s.weaponKnown, s.hasWeapon)), std::memory_order_relaxed);
        f.lastControlledKind.store(static_cast<uint32_t>(s.controlledKind), std::memory_order_relaxed);
        f.lastAssisted.store(s.aimUnitKnown ? (s.assisted ? 1u : 0u) : 2u, std::memory_order_relaxed);
        f.lastBarrel.store(s.barrelKnown ? 1u : 0u, std::memory_order_relaxed);
        f.lastShotLine.store(static_cast<uint32_t>(s.shotLine) | (s.shotSimulationVector ? 8u : 0u),
                             std::memory_order_relaxed);
        f.lastShotKnown.store(s.shotKnown ? 1u : 0u, std::memory_order_relaxed);
        f.lastUpZ1e4.store(s.upKnown ? static_cast<uint32_t>(std::lround((s.up[2] + 1.0f) * 10000.0f)) : 0u,
                           std::memory_order_relaxed);
    }
}

// ---- the seated trace's last result ------------------------------------------------------------------------
// The seated trace (halo4_seat_converge.inl) hands each result over with the ray it traced: the shot witness
// measures against it when no seated trace is published to the sight layer (the key off). Single writer (the
// thread of the seat's control update).
struct Halo4SeatTraceRecord
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> ms{0};
    std::atomic<bool> hit{false};
    std::atomic<float> rangeWu{0.0f}, head[3]{}, dir[3]{}, eye[3]{};
} g_halo4SeatTrace;

void Halo4PublishSeatTrace(uint32_t generation, bool hit, float rangeWu, const SightRay& ray)
{
    auto& r = g_halo4SeatTrace;
    r.sequence.fetch_add(1, std::memory_order_acq_rel);
    r.generation.store(generation, std::memory_order_relaxed);
    r.hit.store(hit, std::memory_order_relaxed);
    r.rangeWu.store(hit ? rangeWu : kSightTraceLengthWu, std::memory_order_relaxed);
    for (int axis = 0; axis < 3; ++axis)
    {
        r.head[axis].store(ray.head[axis], std::memory_order_relaxed);
        r.dir[axis].store(ray.dir[axis], std::memory_order_relaxed);
        r.eye[axis].store(ray.eye[axis], std::memory_order_relaxed);
    }
    r.ms.store(GetTickCount64(), std::memory_order_relaxed);
    r.sequence.fetch_add(1, std::memory_order_release);
}

// The last seated trace as a ray and a range, at most maxAgeMs old and of this generation.
bool Halo4ReadSeatTrace(uint32_t generation, uint64_t maxAgeMs, SightRay& ray, float& rangeWu)
{
    const auto& r = g_halo4SeatTrace;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = r.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        SightRay out{};
        for (int axis = 0; axis < 3; ++axis)
        {
            out.head[axis] = r.head[axis].load(std::memory_order_relaxed);
            out.dir[axis] = r.dir[axis].load(std::memory_order_relaxed);
            out.eye[axis] = r.eye[axis].load(std::memory_order_relaxed);
        }
        const float range = r.rangeWu.load(std::memory_order_relaxed);
        const uint32_t g = r.generation.load(std::memory_order_relaxed);
        const uint64_t at = r.ms.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (r.sequence.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t now = GetTickCount64();
        if (!generation || g != generation || !at || now < at || now - at > maxAgeMs || !std::isfinite(range))
            return false;
        ray = out;
        rangeWu = range;
        return true;
    }
    return false;
}

// ---- the shot witness (0x1DCB8C's exit, the fire routine's call only) --------------------------------------
// The fire routine's aim call (0x617E95) names the aiming unit: the local seated occupant arms this thread's
// tag, which the assist exit consumes in the same fire. The fire detour clears it.
struct Halo4SeatShotTag
{
    bool active = false;
    uint32_t unit = UINT32_MAX;
};
thread_local Halo4SeatShotTag t_halo4SeatShot{};

struct Halo4SeatShotWindow
{
    SRWLOCK lock = SRWLOCK_INIT;
    uint32_t shots = 0, unmeasured = 0, capBound = 0, unassisted = 0, converged = 0, smoothed = 0;
    float turnSumDeg = 0.0f, turnMaxDeg = 0.0f, missSumMm = 0.0f, missMaxMm = 0.0f;
    float lastRangeM = 0.0f, lastMissMm = 0.0f, lastStart[3]{};   // [along, right, up] mm from the solve's eye
    // The shot forms recorded (by the line the fire was handed: barrel, unit aim, unit aim from the simulation-1
    // vector, the mod's muzzle shot), and the sample's predicted line against the direction each fire actually
    // left the aim call with -- the one the assist turns.
    uint32_t fires[4]{}, lineCompared = 0, lineChanged = 0;
    float lineMaxDeg = 0.0f;
} g_halo4SeatShots;

// The fire's weapon owner, as the fire routine names it (0x6177AB: 0x612238(weapon)). Guarded.
uint32_t Halo4SeatSightFireOwner(uint32_t weapon)
{
    __try
    {
        return weapon != UINT32_MAX && Halo4MuzzleObject(weapon, 4) ? g_h4SeatSight.weaponOwner(weapon) : UINT32_MAX;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_h4SeatSight.sampleFaults.fetch_add(1, std::memory_order_relaxed);
        return UINT32_MAX;
    }
}

// THE SHOT FORM. The fire routine's aim call, after the native aim, for a fire the local seat
// aims (the occupant, or our own vehicle) from barrel 0 -- the barrel the sample reads: record the unit-aim flag
// the native was handed, and (dev) measure the sample's predicted line against the direction it really left.
void Halo4SeatSightRecordShotForm(uint32_t unit, const float direction[3], bool unitAim, uint32_t simulation,
                                  bool substituted)
{
    auto& f = g_h4SeatSight;
    const auto request = g_halo4BarrelRequest;
    if (request.barrel != 0 || request.weapon == UINT32_MAX ||
        f.generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
        return;
    const uint32_t owner = Halo4SeatSightFireOwner(request.weapon);
    if (owner == UINT32_MAX)
        return;
    // The sample's prediction was made from the previous form: compare before replacing it.
    Halo4SeatAimSample sample{};
    float deg = -1.0f;
    bool changed = false;
    if (direction && Halo4ReadSeatAimSample(sample) && sample.generation == f.generation &&
        sample.weapon == request.weapon && sample.owner == owner && sample.aiming == unit && sample.shotKnown)
    {
        const float* p = sample.shotForward;
        const float a = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        const float b = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (std::isfinite(a) && a > 1.0e-4f && std::isfinite(b) && b > 1.0e-4f)
        {
            float c = (direction[0] * p[0] + direction[1] * p[1] + direction[2] * p[2]) / (a * b);
            c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
            deg = std::acos(c) * 57.29578f;
        }
        const auto fired = substituted ? halo4_seat_aim::ShotLine::Substituted
            : unitAim ? halo4_seat_aim::ShotLine::UnitAim : halo4_seat_aim::ShotLine::Barrel;
        changed = sample.shotLine != fired || (fired == halo4_seat_aim::ShotLine::UnitAim &&
                                               sample.shotSimulationVector != (simulation == 1));
    }
    Halo4PublishSeatShotForm(f.generation, request.weapon, owner, unit, simulation, unitAim, substituted);
    auto& w = g_halo4SeatShots;
    AcquireSRWLockExclusive(&w.lock);
    ++w.fires[substituted ? 3 : !unitAim ? 0 : simulation == 1 ? 2 : 1];
    if (deg >= 0.0f)
    {
        ++w.lineCompared;
        w.lineMaxDeg = std::fmax(w.lineMaxDeg, deg);
    }
    if (changed)
        ++w.lineChanged;
    ReleaseSRWLockExclusive(&w.lock);
}

// Halo4MuzzleAimDetour, the fire routine's call, after the native aim: whose shot is this? `direction` is what
// the aim call left (the buffer the assist turns), `unitAim`/`simulation` what it was handed, `substituted`
// whether the mod's own muzzle shot replaced its inputs.
void Halo4SeatSightNoteFiringAim(uint32_t unit, const Halo4VehicleInputState& seat, const float direction[3],
                                 bool unitAim, uint32_t simulation, bool substituted)
{
    const uint32_t ready = g_h4SeatSight.ready.load(std::memory_order_acquire);
    if (!seat.seated)
        return;
    if ((ready & kH4SeatShotLine) && (seat.unit == unit || seat.parent == unit))
        Halo4SeatSightRecordShotForm(unit, direction, unitAim, simulation, substituted);
    if (!(ready & kH4SeatAssistWitness))
        return;
    if (seat.unit == unit)
    {
        t_halo4SeatShot = {true, unit};
        return;
    }
    if (seat.parent == unit)
    {
        // The aiming unit is our own vehicle: no player on it, so the native assist is skipped and the shot
        // flies along its shot line -- the barrel's, or the vehicle's own aim under the
        // unit-aim flag (a vehicle nobody controls is its own aiming unit).
        AcquireSRWLockExclusive(&g_halo4SeatShots.lock);
        ++g_halo4SeatShots.unassisted;
        ReleaseSRWLockExclusive(&g_halo4SeatShots.lock);
    }
}

void Halo4SeatSightClearShot()
{
    t_halo4SeatShot = {};
}

// THE WEAPON'S OWN CAP. The native limit is max(21 deg, the weapon's own angle 0x1DC2F4 leaves at out+4), and the
// mod cannot read that angle; but every turn the assist makes is inside it, so the widest turn seen for a weapon
// is a lower bound of its cap. One weapon at a time (the seat's): another weapon's turn starts over.
void Halo4SeatNoteAssistTurn(uint32_t weapon, float turnRad)
{
    if (weapon == UINT32_MAX || !std::isfinite(turnRad) || turnRad <= 0.0f)
        return;
    auto& slot = g_h4SeatSight.widestTurn;
    uint64_t prior = slot.load(std::memory_order_relaxed);
    for (;;)
    {
        float widest = 0.0f;
        const uint32_t bits = static_cast<uint32_t>(prior >> 32);
        memcpy(&widest, &bits, sizeof(widest));
        if (static_cast<uint32_t>(prior) == weapon && prior && widest >= turnRad)
            return;
        uint32_t next = 0;
        memcpy(&next, &turnRad, sizeof(next));
        if (slot.compare_exchange_weak(prior, (uint64_t(next) << 32) | weapon, std::memory_order_relaxed))
            return;
    }
}

// The cap to judge `weapon` against (halo4_seat_aim::SeatAssistCap): 21 deg, or the widest turn seen for it.
float Halo4SeatAssistCapFor(uint32_t weapon)
{
    const uint64_t v = g_h4SeatSight.widestTurn.load(std::memory_order_relaxed);
    float widest = std::numeric_limits<float>::quiet_NaN();
    if (v && weapon != UINT32_MAX && static_cast<uint32_t>(v) == weapon)
    {
        const uint32_t bits = static_cast<uint32_t>(v >> 32);
        memcpy(&widest, &bits, sizeof(widest));
    }
    return halo4_seat_aim::SeatAssistCap(widest);
}

void Halo4SeatSightMeasureShot(const float pre[3], const float post[3], const float origin[3])
{
    const auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const float preLength = std::sqrt(dot(pre, pre)), postLength = std::sqrt(dot(post, post));
    if (g_halo4BarrelRequest.barrel == 0 && std::isfinite(preLength) && preLength > 1.0e-6f &&
        std::isfinite(postLength) && postLength > 1.0e-6f)
    {
        // The turn the assist made bounds this weapon's cap from below (it is the fire routine's own weapon). Barrel
        // 0 only, like everything the claim judges: another barrel may fire another projectile at another angle.
        float c0 = dot(pre, post) / (preLength * postLength);
        c0 = c0 > 1.0f ? 1.0f : (c0 < -1.0f ? -1.0f : c0);
        Halo4SeatNoteAssistTurn(g_halo4BarrelRequest.weapon, std::acos(c0));
    }
    auto& w = g_halo4SeatShots;
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
    SightRay ray{};
    SightTrace trace{};
    // The point: the published seated trace's smoothed range, the one the aim was bent through (stage 1);
    // otherwise the seated trace's own last result along the ray it traced (stage 0: nothing is bent, and the
    // miss is the unconverged aim's).
    float range = 0.0f;
    bool smoothed = false;
    bool measurable = std::isfinite(preLength) && preLength > 1.0e-6f && std::isfinite(postLength) &&
        postLength > 1.0e-6f && Halo4SeatFinite3(origin);
    if (measurable)
    {
        if (Sight_ReadRay(GameTitle::Halo4, generation, kSightRayFreshMs, ray) && ray.ignoreB != -1 &&
            Sight_ReadTrace(GameTitle::Halo4, generation, kSightFreshMs, trace))
        {
            range = trace.AimRangeWu();
            smoothed = true;
        }
        else
            measurable = Halo4ReadSeatTrace(generation, kSightFreshMs, ray, range);
    }
    if (!measurable)
    {
        AcquireSRWLockExclusive(&w.lock);
        ++w.unmeasured;
        ReleaseSRWLockExclusive(&w.lock);
        return;
    }
    const float d[3]{post[0] / postLength, post[1] / postLength, post[2] / postLength};
    float c = dot(pre, post) / (preLength * postLength);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    const float turnDeg = std::acos(c) * 57.29578f;
    // The point under the reticle's ray at that range.
    const float p[3]{ray.head[0] + ray.dir[0] * range, ray.head[1] + ray.dir[1] * range,
                     ray.head[2] + ray.dir[2] * range};
    const float toPoint[3]{p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
    const float t = dot(toPoint, d);
    const float miss[3]{origin[0] + d[0] * t - p[0], origin[1] + d[1] * t - p[1], origin[2] + d[2] * t - p[2]};
    const float missMm = std::sqrt(dot(miss, miss)) * 3048.0f;
    // The shot's start relative to the solve's eye, in the line's frame (along, level right, up).
    float right[3]{d[1], -d[0], 0.0f};
    float rl = std::sqrt(right[0] * right[0] + right[1] * right[1]);
    if (!(rl > 1.0e-4f)) { right[0] = 1.0f; right[1] = 0.0f; rl = 1.0f; }
    right[0] /= rl;
    right[1] /= rl;
    const float up[3]{right[1] * d[2] - right[2] * d[1], right[2] * d[0] - right[0] * d[2],
                      right[0] * d[1] - right[1] * d[0]};
    const float se[3]{origin[0] - ray.eye[0], origin[1] - ray.eye[1], origin[2] - ray.eye[2]};
    // SHOT LAG: the seated shot as the native assist left it, against the same point, from the eye the solve used.
    Sight_NoteShotLag(GameTitle::Halo4, SightShotPath::CrosshairAssist, origin, post, p, ray.eye,
                      ray.eyeVelValid ? ray.eyeVel : nullptr, -1, true, ray.ignoreB);
    AcquireSRWLockExclusive(&w.lock);
    ++w.shots;
    if (smoothed)
        ++w.smoothed;
    if (turnDeg >= halo4_seat_aim::kSeatAssistCapRad * 57.29578f - 0.05f)
        ++w.capBound;
    if (Sight_ConvergedLive(GameTitle::Halo4))
        ++w.converged;
    w.turnSumDeg += turnDeg;
    w.turnMaxDeg = std::fmax(w.turnMaxDeg, turnDeg);
    if (std::isfinite(missMm))
    {
        w.missSumMm += missMm;
        w.missMaxMm = std::fmax(w.missMaxMm, missMm);
    }
    w.lastMissMm = missMm;
    w.lastRangeM = std::sqrt(dot(toPoint, toPoint)) * 3.048f;
    w.lastStart[0] = dot(se, d) * 3048.0f;
    w.lastStart[1] = dot(se, right) * 3048.0f;
    w.lastStart[2] = dot(se, up) * 3048.0f;
    ReleaseSRWLockExclusive(&w.lock);
}

// 0x1DCB8C: forwards every caller exactly (all eight arguments as 64-bit values, plus the spare slots;
// tools/audit_caller_area.py halo4.dll 1DCB8C:12:i is clean: highest caller-area slot entry+0x40 = argument 8,
// no native caller relies on a volatile register across the call). Measures only the fire routine's call
// (return 0x618108) of a shot the local seated occupant aims.
__declspec(noinline) uint64_t __fastcall Halo4SeatAssistDetour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
    uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
    auto& f = g_h4SeatSight;
    f.assistCallbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        bool measure = false;
        float pre[3]{};
        auto* direction = reinterpret_cast<float*>(a8);
        const auto* origin = reinterpret_cast<const float*>(a6);
        if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == f.base + 0x618108 && t_halo4SeatShot.active &&
            direction && origin)
        {
            memcpy(pre, direction, sizeof(pre));
            measure = true;
        }
        if (f.assistOriginal)
            result = CallNativeWithSpareSlots(f.assistOriginal, a1, a2, a3, a4, a5, a6, a7, a8);
        if (measure)
        {
            t_halo4SeatShot = {};
            float post[3]{}, from[3]{};
            memcpy(post, direction, sizeof(post));
            memcpy(from, origin, sizeof(from));
            Halo4SeatSightMeasureShot(pre, post, from);
        }
    }
    __finally
    {
        f.assistCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// ---- the input thread (Game_ComputeAimStick) -------------------------------------------------------------------
// What the witnesses know this poll about a Halo 4 seat. Nothing of it reaches the aim: the seated ray, the bend
// and the servo's parking are halo4_seat_converge.inl's.
struct Halo4SeatSightPoll
{
    bool seated = false;   // the camera hook's stamp names the local unit seated (fresh)
    bool ray = false;      // our seat view, the rendered head known, seats aimed
    bool solve = false;    // every proof of the RENDER eye holds, with the key on: the claim witness and the
                           // assessment, which reason from that eye, may judge this poll
    bool turret = false;   // a slaved-turret seat (the claim witness counts it apart)
    uint32_t unit = UINT32_MAX, parent = UINT32_MAX;
    int16_t seat = -1;
    float head[3]{}, eye[3]{};          // H (the rendered centre eye) and the ray's eye (E when it is known)
    bool eyeKnown = false;
    bool assistKnown = false, assisted = false;   // the shot's aiming unit carries the player (the native assist)
    bool shotKnown = false;                       // the line the assist turns and the barrel are known
    float shotForward[3]{}, barrelOrigin[3]{};    // that line, and the barrel at E + the sample's offset
    uint32_t weapon = UINT32_MAX;                 // the shot's weapon (its observed cap)
    unsigned why = ~0u;   // Halo4SeatSightWhy: the first proof that failed (~0u: Prepare did not judge a seat)
};

// Why a seat did not solve this poll: the first proof that failed (counted for the dev line).
enum Halo4SeatSightWhy : unsigned
{
    kH4SeatWhyView, kH4SeatWhyHead, kH4SeatWhySwitch, kH4SeatWhyEye, kH4SeatWhySample, kH4SeatWhyWitness,
    kH4SeatWhyClass, kH4SeatWhyRecord, kH4SeatWhyKey, kH4SeatWhySolved, kH4SeatWhyCount
};

// The native assist's verdict on a converged seat aim (Game_ComputeAimStick, stage 1): a WITNESS only --
// the claim does not wait on it (a gun the assist cannot bring onto the point fires where it can,
// as in flat Halo).
enum Halo4SeatClaimWhy : unsigned
{
    kH4ClaimYes, kH4ClaimTurret, kH4ClaimUnknown, kH4ClaimUnassisted, kH4ClaimCap, kH4ClaimCount
};

struct Halo4SeatEyeWitness
{
    std::atomic<uint64_t> polls{0}, provenPolls{0}, why[kH4SeatWhyCount]{}, claim[kH4ClaimCount]{};
    // Polls whose dev assessment reticle (honest_reticle_assess) showed the game's actual aim: composed from the
    // shot's line (Halo4SeatSightHonestAlongAim), or left on the game's aim because that could not be composed.
    std::atomic<uint64_t> honestAlongAim{0}, honestGameAim{0};
    // Solved polls on a record that is NOT in world angles now (a tilted vehicle without seat flag bit 4): the
    // 0x5F97DC rebase, which the seat drive answers by writing R^-1 of the aim (seat_frame_reader.h, case b).
    // Natural; counted, never refused.
    std::atomic<uint64_t> solvedTilted{0};
    // Polls whose seat facts came from the stint's last sample, held across a gap in the samples (a weapon switch).
    std::atomic<uint64_t> heldPolls{0};
    std::atomic<float> maxGapMm{0.0f}, speedMps{0.0f}, eyeFromHead[3]{}, maxDirDeg{0.0f}, maxBarrelDeg{0.0f};
    std::atomic<uint32_t> evidence[4]{};   // halo4_seat_aim::EyeEvidence counts over new samples
} g_h4SeatEyeWitness;

void Halo4SeatSightStoreMax(std::atomic<float>& slot, float value)
{
    if (std::isfinite(value) && value > slot.load(std::memory_order_relaxed))
        slot.store(value, std::memory_order_relaxed);
}

// A seated poll this long after the last one starts a new seat stint, even in the same seat: leaving and
// re-entering a seat takes far longer, and while no poll ran nothing was proven (the aim path was not here).
inline constexpr uint64_t kHalo4SeatStintGapMs = 150;
// How long a seat's last aim-camera sample stands in for a missing one (a weapon switch is about a second). Past
// it the sampler is taken to have stopped -- our code -- and the seat solves no more until a sample comes.
inline constexpr uint64_t kHalo4SeatSampleHoldMs = 3000;

// Input thread. True while seated (out filled); false on foot or with no fresh stamp (out untouched beyond
// its defaults), which leaves the on-foot path exactly as it was.
bool Halo4SeatSightPrepare(Halo4SeatSightPoll& out, const float observerForward[3])
{
    struct State
    {
        uint32_t unit = UINT32_MAX, parent = UINT32_MAX, generation = 0, sampleSequence = 0;
        int16_t seat = -1;
        uint64_t lastSeatedMs = 0;   // the last seated poll of this stint
        bool turret = false;         // a fresh sample of this stint classed the seat a slaved turret (latched)
        halo4_seat_aim::SeatEyeAgreement agreement{};
        // The last fresh sample of THIS seat, held across a natural gap in the samples (a weapon switch: the
        // view call that carries them runs behind the same weapon test as the shot's assist).
        Halo4SeatAimSample seatSample{};
        uint64_t seatSampleMs = 0;
    };
    static State s_state;   // Game_ComputeAimStick's thread only, like the servo's own statics
    Halo4VehicleInputState stamp{};
    if (!Halo4ReadSeatStamp(stamp))
        return false;   // no fresh stamp: the stint survives only if a seated poll follows within the gap
    if (!stamp.seated || stamp.parent == UINT32_MAX || stamp.seat < 0)
    {
        s_state = {};   // a fresh stamp says on foot: the stint is over
        return false;
    }
    out.seated = true;
    out.unit = stamp.unit;
    out.parent = stamp.parent;
    out.seat = stamp.seat;
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
    const uint64_t now = GetTickCount64();
    if (s_state.unit != stamp.unit || s_state.parent != stamp.parent || s_state.seat != stamp.seat ||
        s_state.generation != generation || !s_state.lastSeatedMs || now < s_state.lastSeatedMs ||
        now - s_state.lastSeatedMs > kHalo4SeatStintGapMs)
    {
        // A new seat stint (another seat, or the same one after a gap): nothing proven in the last one carries
        // over -- not the proof, not its disagreement timer.
        s_state = {};
        s_state.unit = stamp.unit;
        s_state.parent = stamp.parent;
        s_state.seat = stamp.seat;
        s_state.generation = generation;
    }
    s_state.lastSeatedMs = now;
    auto& w = g_h4SeatEyeWitness;
    w.polls.fetch_add(1, std::memory_order_relaxed);
    const bool viewOwned = NativeVehicleFirstPerson_SeatViewOwned(GameTitle::Halo4, stamp.parent, stamp.seat);
    BoneMatrix center{}, body{};
    const bool headKnown = Halo4LoadCenterRoot(1.0f, center);
    float delta[3]{};
    out.eyeKnown = Halo4LoadSeatEyeRoot(stamp.parent, stamp.seat, 100, out.eye, delta);
    if (headKnown)
    {
        memcpy(out.head, center.translation, sizeof(out.head));
        if (!out.eyeKnown)
        {
            // The ray's eye only (the census's and the shot witness's reference): the controller root.
            if (Halo4LoadControllerRoot(1.0f, body))
                memcpy(out.eye, body.translation, sizeof(out.eye));
            else
                memcpy(out.eye, out.head, sizeof(out.eye));
        }
    }
    out.ray = viewOwned && headKnown && g_config.reach_vehicle_aim > 0;
    // The simulation's aim-camera sample for this seat.
    Halo4SeatAimSample sample{};
    const bool fresh = Halo4ReadSeatAimSample(sample) && sample.generation == generation && sample.seated &&
        sample.ms && now >= sample.ms && now - sample.ms <= kSightFreshMs;
    const bool sameSeat = fresh && sample.unit == stamp.unit && sample.parent == stamp.parent &&
        sample.seat == stamp.seat;
    if (fresh && sample.sequence != s_state.sampleSequence)
    {
        s_state.sampleSequence = sample.sequence;
        float gapM = std::numeric_limits<float>::quiet_NaN();
        if (out.eyeKnown)
        {
            const float g[3]{sample.camera[0] - out.eye[0], sample.camera[1] - out.eye[1],
                             sample.camera[2] - out.eye[2]};
            gapM = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) * 3.048f;
        }
        // An unmeasured speed (the stint's first sample, or after a pause) makes the gap no evidence either way,
        // as does a speed past the rail's cap; the rail widens by the sample's age as it is judged.
        const float speed = sample.speedMps >= 0.0f ? sample.speedMps : std::numeric_limits<float>::quiet_NaN();
        const float ageS = static_cast<float>(now - sample.ms) / 1000.0f;
        const auto evidence = halo4_seat_aim::JudgeEye(sample.witness, sameSeat && out.eyeKnown, gapM, speed, ageS);
        s_state.agreement.step(evidence, now);
        w.evidence[static_cast<unsigned>(evidence) & 3u].fetch_add(1, std::memory_order_relaxed);
        if (sameSeat && out.eyeKnown && std::isfinite(speed) && std::isfinite(gapM) &&
            gapM * 1000.0f > w.maxGapMm.load(std::memory_order_relaxed))
        {
            // The largest gap that was judged, and the speed it was judged at (the dev line pairs them).
            w.maxGapMm.store(gapM * 1000.0f, std::memory_order_relaxed);
            w.speedMps.store(speed, std::memory_order_relaxed);
        }
        // The game's aim direction on the tick against the observer forward the render transaction read.
        if (sameSeat && observerForward)
        {
            const float a = std::sqrt(sample.dir[0] * sample.dir[0] + sample.dir[1] * sample.dir[1] +
                                      sample.dir[2] * sample.dir[2]);
            const float b = std::sqrt(observerForward[0] * observerForward[0] +
                                      observerForward[1] * observerForward[1] +
                                      observerForward[2] * observerForward[2]);
            if (a > 1.0e-4f && b > 1.0e-4f)
            {
                float c = (sample.dir[0] * observerForward[0] + sample.dir[1] * observerForward[1] +
                           sample.dir[2] * observerForward[2]) / (a * b);
                c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
                Halo4SeatSightStoreMax(w.maxDirDeg, std::acos(c) * 57.29578f);
            }
        }
    }
    // No sample is no evidence: the proof is neither aged out nor renewed. Were it counted as a
    // disagreement, a weapon switch -- the view call that carries the sample runs behind the same weapon test as
    // the shot's assist -- would unprove the eye and switch the seat's convergence off for a natural cause.
    if (sameSeat)
    {
        s_state.seatSample = sample;
        s_state.seatSampleMs = now;
    }
    // This seat's facts (its class, its flags, its frame anchor): this poll's sample, or the last one of this
    // stint across a gap of up to kHalo4SeatSampleHoldMs. Seat-constant facts do not change while no sample comes;
    // a longer silence is no longer a natural gap but our sampler not running, and the seat solves no more.
    const bool seatSampleHeld = !sameSeat && s_state.seatSampleMs && now >= s_state.seatSampleMs &&
        now - s_state.seatSampleMs <= kHalo4SeatSampleHoldMs;
    const bool seatFactsKnown = sameSeat || seatSampleHeld;
    const Halo4SeatAimSample& seatFacts = sameSeat ? sample : s_state.seatSample;
    if (seatSampleHeld)
        w.heldPolls.fetch_add(1, std::memory_order_relaxed);
    if (headKnown && out.eyeKnown)
    {
        // E relative to the rendered head, in the head's frame (forward, right, up), for the dev line.
        const float e[3]{out.eye[0] - out.head[0], out.eye[1] - out.head[1], out.eye[2] - out.head[2]};
        const float* forward = center.rotation;
        const float* left = center.rotation + 3;
        const float* up = center.rotation + 6;
        w.eyeFromHead[0].store((e[0] * forward[0] + e[1] * forward[1] + e[2] * forward[2]) * 3048.0f,
                               std::memory_order_relaxed);
        w.eyeFromHead[1].store(-(e[0] * left[0] + e[1] * left[1] + e[2] * left[2]) * 3048.0f,
                               std::memory_order_relaxed);
        w.eyeFromHead[2].store((e[0] * up[0] + e[1] * up[1] + e[2] * up[2]) * 3048.0f, std::memory_order_relaxed);
    }
    const auto seatClass = seatFactsKnown
        ? halo4_seat_aim::ClassifySeat(seatFacts.flagsKnown, seatFacts.flags, seatFacts.controlledKind,
                                       seatFacts.weaponKnown, seatFacts.hasWeapon)
        : halo4_seat_aim::SeatClass::Unknown;
    // The gate is what our code must prove (the flags, the frame anchor); a tilt is natural and only counted
    // (SeatRecordIsWorld: the slope-frame witness; the seat drive handles the 0x5F97DC rebase itself).
    const halo4_seat_aim::Vec3 sampleUp{seatFacts.up[0], seatFacts.up[1], seatFacts.up[2]};
    const bool recordSolvable = seatFactsKnown &&
        halo4_seat_aim::SeatRecordSolvable(seatFacts.flagsKnown, seatFacts.flags, seatFacts.upKnown, sampleUp);
    const bool recordTilted = recordSolvable &&
        !halo4_seat_aim::SeatRecordIsWorld(seatFacts.flagsKnown, seatFacts.flags, seatFacts.upKnown, sampleUp);
    if (s_state.agreement.proven)
        w.provenPolls.fetch_add(1, std::memory_order_relaxed);
    // THE PROOFS OF THE RENDER EYE, every one required before the claim witness and the assessment judge a poll
    // from it. They gate no aim: a seat converges on the convergence lane's own facts.
    const bool key = g_config.halo4_seat_converge != 0;
    unsigned why = kH4SeatWhySolved;
    if (!viewOwned) why = kH4SeatWhyView;
    else if (!headKnown) why = kH4SeatWhyHead;
    else if (!out.ray || !Sight_AimEnabled()) why = kH4SeatWhySwitch;
    else if (!out.eyeKnown) why = kH4SeatWhyEye;
    else if (!seatFactsKnown) why = kH4SeatWhySample;
    else if (!s_state.agreement.proven) why = kH4SeatWhyWitness;
    else if (seatClass == halo4_seat_aim::SeatClass::Unknown || seatClass == halo4_seat_aim::SeatClass::NoWeapon)
        why = kH4SeatWhyClass;
    else if (!recordSolvable) why = kH4SeatWhyRecord;
    else if (!key) why = kH4SeatWhyKey;   // every proof holds; only the key is off
    w.why[why].fetch_add(1, std::memory_order_relaxed);
    out.why = why;
    out.solve = why == kH4SeatWhySolved;
    if (out.solve && recordTilted)
        w.solvedTilted.fetch_add(1, std::memory_order_relaxed);
    // A slaved turret's aim camera IS its barrel's line (0x124DE6): the servo parks it as a turret, whatever the
    // eye proofs say (its pitch is on the servo). The class is LATCHED for the stint:
    // set by a fresh sample of this seat that says bit 7, released only by a fresh sample of this seat with
    // another known class (or a new stint) -- never because this poll's sample was stale or its read collided,
    // which would flip the servo's turret mode (resetting its measured quantum and logging armed/released). Nor
    // does it wait on the rendered head: the turret's servo uses the game's own aim, so only the switches and
    // our seat view gate it.
    if (sameSeat && seatClass != halo4_seat_aim::SeatClass::Unknown)
        s_state.turret = seatClass == halo4_seat_aim::SeatClass::SlavedTurret;
    out.turret = key && viewOwned && g_config.reach_vehicle_aim > 0 && Sight_AimEnabled() && s_state.turret;
    // The shot side's facts for the claim (stage 1).
    out.assistKnown = sameSeat && sample.aimUnitKnown;
    out.assisted = out.assistKnown && sample.assisted;
    // The shot's line is the one the native assist will turn (the barrel's forward, or the aiming unit's aim when
    // the fire is handed the unit-aim flag); unknown until a fire of this weapon has said which.
    out.shotKnown = sameSeat && out.eyeKnown && sample.barrelKnown && sample.shotKnown;
    if (out.shotKnown)
    {
        out.weapon = sample.weapon;
        memcpy(out.shotForward, sample.shotForward, sizeof(out.shotForward));
        for (int axis = 0; axis < 3; ++axis)
            out.barrelOrigin[axis] = out.eye[axis] + sample.barrelOffset[axis];
    }
    return true;
}

// Stage 1, Game_ComputeAimStick, after the aim was bent through `point` (the intent ray at the converged range):
// WITNESS ONLY -- would the native seated assist bring the shot onto the point? It
// turns the shot from its shot line (the barrel's forward, or the aiming unit's aim under the unit-aim flag)
// toward the crosshair point by at most max(21 deg, the weapon's own angle), and only while the shot's aiming
// unit carries the player; "on the point" with 2 deg to spare inside 21 deg or the widest turn seen for the
// weapon. It must not withhold the converged claim or move the reticle: it is counted for
// the dev line and decides nothing (a capped or unassisted gun fires where it can, as in flat Halo).
void Halo4SeatSightClaimWitness(const Halo4SeatSightPoll& seat, const float point[3])
{
    using halo4_seat_aim::Vec3;
    auto& w = g_h4SeatEyeWitness;
    unsigned why = kH4ClaimYes;
    if (seat.turret)
        why = kH4ClaimTurret;
    else if (!seat.shotKnown || !seat.assistKnown)
        why = kH4ClaimUnknown;
    else
    {
        const Vec3 barrelOrigin{seat.barrelOrigin[0], seat.barrelOrigin[1], seat.barrelOrigin[2]};
        const Vec3 target{point[0], point[1], point[2]};
        // Against 21 deg, or the widest turn the assist was seen to make for this weapon (a lower bound of its
        // real cap max(21 deg, the weapon's own angle)): beyond it the honest line is a lower bound of the turn.
        const auto verdict = halo4_seat_aim::JudgeBarrel(
            {seat.shotForward[0], seat.shotForward[1], seat.shotForward[2]}, barrelOrigin, target,
            seat.assisted, Halo4SeatAssistCapFor(seat.weapon));
        if (!verdict.valid)
            why = kH4ClaimUnknown;
        else
        {
            Halo4SeatSightStoreMax(w.maxBarrelDeg, verdict.angleRad * 57.29578f);
            if (!seat.assisted)
                why = kH4ClaimUnassisted;
            else if (!verdict.claim)
                why = kH4ClaimCap;
        }
    }
    w.claim[why].fetch_add(1, std::memory_order_relaxed);
}

// [assess-only begin]
// Stage 1, DEV ASSESSMENT ONLY (honest_reticle_assess = 2): where the shot really goes for a seat whose proofs all
// hold. The game's aim is not the shot's line: the native assist's crosshair trace runs from
// the game's eye E along the unit's actual aim, and the shot turns from its own line (the barrel's, or the aiming
// unit's aim) toward that point by at most the cap -- or not at all, unassisted. The point is taken on the actual
// aim at the converged point's range (the native trace's own range is not known; the range only moves the
// parallax between H, E and the barrel). honestDir: from the rendered head H to where that shot is at the range.
// False -- the caller keeps the game's aim -- for a turret (the game's aim is its barrel), or with the shot
// line, the assist or the eye unknown.
// `aimRangeWu` > 0: the native trace's range IS known -- the dev assessment's own trace along the game's aim from
// the seat's eye (Sight_ReadAssessTrace) -- and is used instead of the converged point's. `shotPoint`, when given,
// receives the point the shot reaches, Q = B + shot * |T - B| (world units), so the assessment can draw it at its
// own depth.
bool Halo4SeatSightHonestAlongAim(const Halo4SeatSightPoll& seat, const float point[3], const float aimForward[3],
                                  float honestDir[3], float aimRangeWu = 0.0f, float* shotPoint = nullptr)
{
    using halo4_seat_aim::Vec3;
    if (seat.turret || !seat.shotKnown || !seat.assistKnown || !seat.eyeKnown || !point || !aimForward)
        return false;
    const Vec3 eye{seat.eye[0], seat.eye[1], seat.eye[2]};
    Vec3 aim{};
    if (!halo4_seat_aim::Normalize({aimForward[0], aimForward[1], aimForward[2]}, aim))
        return false;
    const Vec3 toPoint = halo4_seat_aim::Sub({point[0], point[1], point[2]}, eye);
    const float range = std::isfinite(aimRangeWu) && aimRangeWu > 0.0f ? aimRangeWu
                                                                        : std::sqrt(halo4_seat_aim::Dot(toPoint, toPoint));
    if (!std::isfinite(range) || !(range > 1.0e-3f))
        return false;
    const Vec3 target = halo4_seat_aim::Add(eye, halo4_seat_aim::Scale(aim, range));
    const Vec3 barrelOrigin{seat.barrelOrigin[0], seat.barrelOrigin[1], seat.barrelOrigin[2]};
    const auto verdict = halo4_seat_aim::JudgeBarrel({seat.shotForward[0], seat.shotForward[1], seat.shotForward[2]},
        barrelOrigin, target, seat.assisted, Halo4SeatAssistCapFor(seat.weapon));
    Vec3 dir{};
    if (!verdict.valid ||
        !halo4_seat_aim::ShotReticleDirection({seat.head[0], seat.head[1], seat.head[2]}, barrelOrigin,
                                              verdict.shot, target, dir))
        return false;
    if (shotPoint)
    {
        Vec3 shot{};
        const Vec3 toTarget = halo4_seat_aim::Sub(target, barrelOrigin);
        const float reach = std::sqrt(halo4_seat_aim::Dot(toTarget, toTarget));
        if (!halo4_seat_aim::Normalize(verdict.shot, shot) || !std::isfinite(reach))
            return false;
        const Vec3 q = halo4_seat_aim::Add(barrelOrigin, halo4_seat_aim::Scale(shot, reach));
        shotPoint[0] = q.x;
        shotPoint[1] = q.y;
        shotPoint[2] = q.z;
    }
    honestDir[0] = dir.x;
    honestDir[1] = dir.y;
    honestDir[2] = dir.z;
    return true;
}
// [assess-only end]

// Cold worker, every 2 s: the two dev lines (the seated trace's own is the convergence lane's).
void Halo4SeatSightReport()
{
    static uint64_t s_last = 0;
    const uint64_t now = GetTickCount64();
    if (now - s_last < 2000)
        return;
    s_last = now;
    auto& f = g_h4SeatSight;
    if (!f.ready.load(std::memory_order_acquire) && !f.assistTarget)
        return;
    auto& w = g_h4SeatEyeWitness;
    const uint64_t samples = f.samplesSeated.exchange(0, std::memory_order_relaxed);
    const uint64_t polls = w.polls.exchange(0, std::memory_order_relaxed);
    f.samples.exchange(0, std::memory_order_relaxed);
    if (samples || polls)
    {
        static const char* const kControlled[]{"unknown", "the occupant (own weapon)", "the vehicle", "a turret"};
        const uint32_t flags = f.lastFlags.load(std::memory_order_relaxed);
        const uint32_t assisted = f.lastAssisted.load(std::memory_order_relaxed);
        const uint32_t upZ = f.lastUpZ1e4.load(std::memory_order_relaxed);
        uint64_t why[kH4SeatWhyCount]{};
        for (unsigned i = 0; i < kH4SeatWhyCount; ++i)
            why[i] = w.why[i].exchange(0, std::memory_order_relaxed);
        uint32_t evidence[4]{};
        for (unsigned i = 0; i < 4; ++i)
            evidence[i] = w.evidence[i].exchange(0, std::memory_order_relaxed);
        char flagText[16], upText[16];
        if (flags == 0xFFFFFFFFu)
            snprintf(flagText, sizeof(flagText), "unknown");
        else
            snprintf(flagText, sizeof(flagText), "0x%02X", flags);
        if (upZ)
            snprintf(upText, sizeof(upText), "%.4f", double(upZ) / 10000.0 - 1.0);
        else
            snprintf(upText, sizeof(upText), "unknown");
        // The shot line the assist will turn, as the last sample named it.
        const uint32_t shotLine = f.lastShotLine.load(std::memory_order_relaxed);
        char shotText[64];
        snprintf(shotText, sizeof(shotText), "%s%s%s",
                 halo4_seat_aim::ShotLineName(static_cast<halo4_seat_aim::ShotLine>(shotLine & 7u)),
                 (shotLine & 7u) == static_cast<uint32_t>(halo4_seat_aim::ShotLine::UnitAim)
                     ? ((shotLine & 8u) ? " +0x4E0" : " +0x4EC") : "",
                 (shotLine & 7u) && !f.lastShotKnown.load(std::memory_order_relaxed) ? " (unreadable)" : "");
        LOG("SIGHT Halo 4 seat eye: %llu sim samples (first person %llu, chase %llu, no witness %llu; chase pushes "
            "%llu; faults %llu, writer busy %llu); seat flags %s, class %s, controls %s, aiming unit carries the "
            "player %s, barrel %s, shot line %s, vehicle up.z %s; eye vs sim: agree %u disagree %u chase %u no evidence (speed unmeasured or past 40 m/s) %u, "
            "max |E_sim - E_render| %.0f mm at %.1f m/s, E_render - head [along,right,up] %.0f,%.0f,%.0f mm, sim aim vs "
            "observer forward max %.2f deg; %llu polls, eye proven in %llu; not solved: view %llu, head %llu, "
            "switches %llu, eye %llu, sample %llu, witness %llu, class %llu, record frame unproven %llu, key off %llu; "
            "facts held across a sample gap %llu; "
            "every render-eye proof held %llu (on a tilted record %llu); assist witness (decides nothing): on the point %llu, turret %llu, shot unknown %llu, no assist "
            "%llu, beyond the cap %llu (max shot line->point %.1f deg); assessment reticle (honest_reticle_assess) on "
            "the shot %llu, on the game's aim %llu",
            static_cast<unsigned long long>(samples), static_cast<unsigned long long>(f.firstPerson.exchange(0)),
            static_cast<unsigned long long>(f.chase.exchange(0)), static_cast<unsigned long long>(f.noWitness.exchange(0)),
            static_cast<unsigned long long>(f.pushes.exchange(0)), static_cast<unsigned long long>(f.sampleFaults.exchange(0)),
            static_cast<unsigned long long>(f.writerBusy.exchange(0)), flagText,
            halo4_seat_aim::SeatClassName(static_cast<halo4_seat_aim::SeatClass>(f.lastClass.load())),
            kControlled[f.lastControlledKind.load() & 3u], assisted == 2 ? "unknown" : assisted ? "yes" : "NO",
            f.lastBarrel.load() ? "known" : "unknown", shotText, upText, evidence[1], evidence[2], evidence[3], evidence[0],
            double(w.maxGapMm.exchange(0.0f)), double(w.speedMps.exchange(0.0f)), double(w.eyeFromHead[0].load()),
            double(w.eyeFromHead[1].load()), double(w.eyeFromHead[2].load()), double(w.maxDirDeg.exchange(0.0f)),
            static_cast<unsigned long long>(polls), static_cast<unsigned long long>(w.provenPolls.exchange(0)),
            static_cast<unsigned long long>(why[kH4SeatWhyView]), static_cast<unsigned long long>(why[kH4SeatWhyHead]),
            static_cast<unsigned long long>(why[kH4SeatWhySwitch]),
            static_cast<unsigned long long>(why[kH4SeatWhyEye]), static_cast<unsigned long long>(why[kH4SeatWhySample]),
            static_cast<unsigned long long>(why[kH4SeatWhyWitness]), static_cast<unsigned long long>(why[kH4SeatWhyClass]),
            static_cast<unsigned long long>(why[kH4SeatWhyRecord]), static_cast<unsigned long long>(why[kH4SeatWhyKey]),
            static_cast<unsigned long long>(w.heldPolls.exchange(0)),
            static_cast<unsigned long long>(why[kH4SeatWhySolved]),
            static_cast<unsigned long long>(w.solvedTilted.exchange(0)),
            static_cast<unsigned long long>(w.claim[kH4ClaimYes].exchange(0)),
            static_cast<unsigned long long>(w.claim[kH4ClaimTurret].exchange(0)),
            static_cast<unsigned long long>(w.claim[kH4ClaimUnknown].exchange(0)),
            static_cast<unsigned long long>(w.claim[kH4ClaimUnassisted].exchange(0)),
            static_cast<unsigned long long>(w.claim[kH4ClaimCap].exchange(0)), double(w.maxBarrelDeg.exchange(0.0f)),
            static_cast<unsigned long long>(w.honestAlongAim.exchange(0)),
            static_cast<unsigned long long>(w.honestGameAim.exchange(0)));
    }
    auto& s = g_halo4SeatShots;
    AcquireSRWLockExclusive(&s.lock);
    const Halo4SeatShotWindow shots = [&] {
        Halo4SeatShotWindow copy{};
        copy.shots = s.shots; copy.unmeasured = s.unmeasured; copy.capBound = s.capBound;
        copy.unassisted = s.unassisted; copy.converged = s.converged; copy.smoothed = s.smoothed;
        copy.turnSumDeg = s.turnSumDeg;
        copy.turnMaxDeg = s.turnMaxDeg; copy.missSumMm = s.missSumMm; copy.missMaxMm = s.missMaxMm;
        copy.lastRangeM = s.lastRangeM; copy.lastMissMm = s.lastMissMm;
        memcpy(copy.lastStart, s.lastStart, sizeof(copy.lastStart));
        memcpy(copy.fires, s.fires, sizeof(copy.fires));
        copy.lineCompared = s.lineCompared; copy.lineChanged = s.lineChanged; copy.lineMaxDeg = s.lineMaxDeg;
        return copy;
    }();
    s.shots = s.unmeasured = s.capBound = s.unassisted = s.converged = s.smoothed = 0;
    s.turnSumDeg = s.turnMaxDeg = s.missSumMm = s.missMaxMm = 0.0f;
    memset(s.fires, 0, sizeof(s.fires));
    s.lineCompared = s.lineChanged = 0;
    s.lineMaxDeg = 0.0f;
    ReleaseSRWLockExclusive(&s.lock);
    const uint32_t fires = shots.fires[0] + shots.fires[1] + shots.fires[2] + shots.fires[3];
    if (shots.shots || shots.unmeasured || shots.unassisted || fires)
    {
        const float n = shots.shots ? float(shots.shots) : 1.0f;
        const uint64_t widest = f.widestTurn.load(std::memory_order_relaxed);
        float widestRad = 0.0f;
        const uint32_t widestBits = static_cast<uint32_t>(widest >> 32);
        memcpy(&widestRad, &widestBits, sizeof(widestRad));
        LOG("SHOT Halo 4 seated: %u shot(s) through the native seated assist (+%u unmeasured; %u while converged; "
            "%u against the published smoothed range, the rest against the seated trace's own last hit), turned "
            "avg %.2f / max %.2f deg from the shot line, %u at or past the 21 deg minimum cap (widest turn seen for "
            "the current weapon %.2f deg: its cap is at least that); the final line misses the point under "
            "the reticle by avg %.0f / max %.0f mm (last %.0f mm at %.2f m); the last left [along,right,up] "
            "%+.0f,%+.0f,%+.0f mm from the solve's eye; %u seated fire(s) aimed by our vehicle (no assist: the "
            "shot line itself); fires handed the assist the barrel's forward %u, the aiming unit's aim +0x4EC %u / "
            "+0x4E0 %u, the mod's muzzle shot %u; predicted line vs the fire's %u compared, max %.2f deg, %u from "
            "a different form",
            shots.shots, shots.unmeasured, shots.converged, shots.smoothed, double(shots.turnSumDeg / n),
            double(shots.turnMaxDeg),
            shots.capBound, double(widestRad) * 57.29578, double(shots.missSumMm / n), double(shots.missMaxMm), double(shots.lastMissMm),
            double(shots.lastRangeM), double(shots.lastStart[0]), double(shots.lastStart[1]),
            double(shots.lastStart[2]), shots.unassisted, shots.fires[0], shots.fires[1], shots.fires[2],
            shots.fires[3], shots.lineCompared, double(shots.lineMaxDeg), shots.lineChanged);
    }
}

// ---- install / remove ----------------------------------------------------------------------------------------
bool Halo4SeatSightUnique(uintptr_t base, size_t size, uintptr_t rva, const char* pattern)
{
    const uintptr_t hit = sig::Find(base, size, pattern);
    return hit == base + rva && !sig::Find(hit + 1, base + size - hit - 1, pattern);
}

bool Halo4SeatSightCalls(uintptr_t base, uintptr_t call, uintptr_t target)
{
    const auto* code = reinterpret_cast<const uint8_t*>(base + call);
    return code[0] == 0xE8 && base + call + 5 + *reinterpret_cast<const int32_t*>(code + 1) == base + target;
}

bool RemoveHalo4SeatSight()
{
    auto& f = g_h4SeatSight;
    f.ready.store(0, std::memory_order_release);
    if (!f.assistTarget)
        return true;
    const auto status = MCCVR_DisableHookForRetirement(f.assistTarget);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
    {
        LOG("SIGHT Halo 4 seats CleanupRequired: the assist witness hook did not disable");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo4SeatAssistDetour)};
    const void* trampolines[]{reinterpret_cast<const void*>(f.assistOriginal)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, f.assistCallbacks))
    {
        LOG("SIGHT Halo 4 seats CleanupRequired: the assist witness is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(f.assistTarget);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("SIGHT Halo 4 seats CleanupRequired: the assist witness hook was not removed");
        return false;
    }
    f.assistTarget = nullptr;
    f.assistOriginal = nullptr;
    return true;
}

// After the muzzle lane (its bindings are this lane's too). Every anchor its own feature bit; whatever fails
// says so once and leaves that piece -- and therefore seated convergence -- off.
void InstallHalo4SeatSight(uintptr_t base, size_t size, uint32_t generation)
{
    auto& f = g_h4SeatSight;
    f.ready.store(0, std::memory_order_release);
    if (f.assistTarget)
    {
        LOG("SIGHT Halo 4 seats: a previous assist witness has not retired; seated witnesses off");
        return;
    }
    if (!g_halo4BarrelBindingsReady.load(std::memory_order_acquire) || g_halo4Barrel.base != base)
    {
        LOG("SIGHT Halo 4 seats: the muzzle lane is not installed (its view detour carries the sample); seated "
            "witnesses off");
        return;
    }
    f.base = base;
    f.generation = generation;
    f.sampleVersion.store(0, std::memory_order_release);
    f.lastQpc = 0;
    f.last = {};
    f.widestTurn.store(0, std::memory_order_relaxed);   // handles of another level say nothing about this one
    g_halo4SeatEyeRoot.ms.store(0, std::memory_order_release);
    uint32_t ready = 0;
    // The muzzle lane proved these entries at exactly these addresses.
    f.weaponInSlot = reinterpret_cast<Halo4SeatWeaponInSlotFn>(base + 0x5FA1C4);
    f.weaponOwner = reinterpret_cast<Halo4SeatHandleFn>(base + 0x612238);
    f.aimingUnit = reinterpret_cast<Halo4SeatHandleFn>(base + 0x5F1ABC);
    f.barrelMarker = reinterpret_cast<Halo4SeatBarrelMarkerFn>(base + 0x610F6C);
    f.markers = reinterpret_cast<Halo4MuzzleMarkersFn>(base + 0x5D5B74);
    // ADDR-HYGIENE: resolved -- each entry below is a unique signature at its address AND the native call edge
    // that proves how the game calls it; nothing is assumed on another build.
    if (Halo4SeatSightUnique(base, size, 0x5FB2D4, "48 89 5C 24 08 57 48 83 EC 20 0F B7 DA 41 8B F8 BA 03 20 00 00 "
            "E8 ?? ?? ?? ?? 48 85 C0 74 5A") &&
        Halo4SeatSightUnique(base, size, 0x5FB31D, "42 3B 84 82 10 05 00 00 7D 26 42 8B 94 82 14 05 00 00 48 0F BF "
            "C3 48 6B C8 5B 48 03 CA 48 C1 EA 1C 49 8B 04 D2 8B 04 88 0F A3 F8 0F 92 C0") &&
        Halo4SeatSightUnique(base, size, 0xA88E4, "0F B7 55 CB 41 B8 02 00 00 00 8B 4D C7 E8 ?? ?? ?? ?? 84 C0 74 3B") &&
        Halo4SeatSightCalls(base, 0xA88F1, 0x5FB2D4))
    {
        f.seatFlag = reinterpret_cast<Halo4SeatFlagFn>(base + 0x5FB2D4);
        ready |= kH4SeatFlags;
    }
    else
        LOG("SIGHT Halo 4 seats: the seat-flag accessor (0x5FB2D4, its body, the dispatcher's call 0xA88F1) did "
            "not prove; every seat is class Unknown to the witnesses");
    if (Halo4SeatSightUnique(base, size, 0x5F9694, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 56 48 83 "
            "EC 20 41 83 CE FF 8B F9 41 3B CE 0F 84 ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BE 18 00 "
            "00 00 0F B7 CF 48 8B 04 D0 48 8B 34 06 48 8D 04 49 48 8B 56 50 4C 8B 44 C2 10") &&
        Halo4SeatSightCalls(base, 0x1DCA0E, 0x5F9694))
    {
        f.controlled = reinterpret_cast<Halo4SeatHandleFn>(base + 0x5F9694);
        ready |= kH4SeatControlled;
    }
    else
        LOG("SIGHT Halo 4 seats: the controlled-unit query (0x5F9694, the aim view's call 0x1DCA0E) did not prove; "
            "seats are class Unknown to the witnesses");
    if (Halo4SeatSightUnique(base, size, 0x5D84D0, "48 83 EC 28 44 8B 0D ?? ?? ?? ?? 4D 8B D0 65 48 8B 04 25 58 00 00 "
            "00 4C 8B C2 BA 18 00 00 00 0F B7 C9 4A 8B 04 C8 4C 8B 0C 02 48 8D 04 49 49 8B 51 50 4C 8B 4C C2 10 41 83 "
            "79 24 FF 75 38") &&
        Halo4SeatSightUnique(base, size, 0x5F98C2, "41 C1 E8 04 41 F6 C0 01 0F 85 ?? ?? ?? ?? 4C 8D 45 97 41 8B CE 48 "
            "8D 55 A7 E8 ?? ?? ?? ??") &&
        Halo4SeatSightCalls(base, 0x5F98DB, 0x5D84D0))
    {
        f.objectFrame = reinterpret_cast<Halo4SeatObjectFrameFn>(base + 0x5D84D0);
        ready |= kH4SeatObjectFrame;
    }
    else
        LOG("SIGHT Halo 4 seats: the object frame (0x5D84D0, the record rebase's call 0x5F98DB) did not prove; "
            "the slope witness reads only seats whose record is in world angles (flag bit 4)");
    if (Halo4SeatSightUnique(base, size, 0x6121B8, "48 83 EC 28 65 48 8B 04 25 58 00 00 00 44 8B C9 8B 0D ?? ?? ?? ?? "
            "41 B8 18 00 00 00 48 8B 04 C8 41 0F B7 C9 4D 8B 04 00 48 8D 14 49 49 8B 40 50 4C 8B 54 D0 10 48 8B 05 ?? "
            "?? ?? ??") &&
        Halo4SeatSightUnique(base, size, 0x61797F, "8B 54 24 78 41 8B CD E8 ?? ?? ?? ?? 41 8B CD 8B D8 E8 ?? ?? ?? ?? "
            "33 C9 4C 8D 85 E0 06 00 00 88 4C 24 30 8B D3 88 4C 24 28 88 4C 24 20 44 8D 49 40 8B C8 E8 ?? ?? ?? ??") &&
        Halo4SeatSightCalls(base, 0x617986, 0x610F6C) && Halo4SeatSightCalls(base, 0x617990, 0x6121B8) &&
        Halo4SeatSightCalls(base, 0x6179B2, 0x5D5B74))
    {
        f.markerOwner = reinterpret_cast<Halo4SeatHandleFn>(base + 0x6121B8);
        ready |= kH4SeatBarrel;
    }
    else
        LOG("SIGHT Halo 4 seats: the barrel marker read (0x6121B8, the fire routine's marker block 0x61797F) did "
            "not prove; the assist witness cannot judge seated shots (it decides nothing)");
    if (Halo4SeatSightUnique(base, size, 0x617DA7, "41 8B CC E8 ?? ?? ?? ?? 8B C8 8B D8 E8 ?? ?? ?? ?? 48 8B 48 10 8B 81 "
            "9C 04 00 00 44 8B A1 84 04 00 00 89 44 24 6C 48 8D 81 A8 05 00 00") &&
        Halo4SeatSightCalls(base, 0x617DAA, 0x5F1ABC))
        ready |= kH4SeatAimUnit;
    else
        LOG("SIGHT Halo 4 seats: the fire routine's aiming unit (0x617DA7: 0x5F1ABC, its +0x49C player) did not "
            "prove; the assist witness cannot judge seated shots (it decides nothing)");
    // The aim camera (0x2061A8 whole, its selector call and +0x490 test, the aim view's call 0x1DCA25): the
    // convergence lane proved it for this image, or said why not.
    if (g_h4SeatConverge.ready.load(std::memory_order_acquire) && g_h4SeatConverge.base == base)
        ready |= kH4SeatAimCamera;
    else
        LOG("SIGHT Halo 4 seats: the aim camera did not prove (the seat converge line above says which part); no "
            "first-person witness and no census sample");
    if (Halo4SeatSightUnique(base, size, 0x1DCB8C, "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 56 41 57 48 8D 68 "
            "C1 48 81 EC E0 00 00 00 48 8B 5D 7F 33 FF 0F 29 70 D8 45 85 C9 44 8A 4D 67 0F 29 78 C8 44 0F 29 40 B8 41 8B "
            "00 4C 8D 45 5F 89 45 5F 0F 94 C0") &&
        Halo4SeatSightCalls(base, 0x618103, 0x1DCB8C))
    {
        void* const target = reinterpret_cast<void*>(base + 0x1DCB8C);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&Halo4SeatAssistDetour),
                reinterpret_cast<void**>(&f.assistOriginal)) == MH_OK)
        {
            f.assistTarget = target;
            if (MH_EnableHook(target) == MH_OK)
                ready |= kH4SeatAssistWitness;
            else
                LOG("SIGHT Halo 4 seats: the assist witness hook did not enable; no SHOT Halo 4 seated line");
        }
        else
        {
            f.assistOriginal = nullptr;
            LOG("SIGHT Halo 4 seats: the assist witness hook was not created; no SHOT Halo 4 seated line");
        }
    }
    else
        LOG("SIGHT Halo 4 seats: the seated assist (0x1DCB8C, the fire routine's call 0x618103) did not prove; no "
            "SHOT Halo 4 seated line");
    // THE SHOT LINE. 0x5F3510 under its unit-aim flag (argument 8, [rbp+0x77]) copies the
    // unit's +0x4EC -- +0x4E0 when argument 9 ([rbp+0x7F]) is 1 -- into its direction argument (r8 -> rdi), and
    // the fire routine hands that call and the seated assist's argument 8 the SAME buffer, [rbp+0x30] (nothing
    // between 0x617E95 and 0x618103 writes it). The body signature starts at 0x5F351B, past the bytes the muzzle
    // lane's hook patched (that lane proved the entry, the six pushes and `lea rbp,[rax-0x37]`, before hooking).
    if (Halo4SeatSightUnique(base, size, 0x5F351B, "48 8D 68 C9 48 81 EC D8 00 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 "
            "98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 5C 24 70 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 8F 65 "
            "48 8B 04 25 58 00 00 00 48 8B DA 44 8B F1 49 8B F8 8B 0D ?? ?? ?? ?? 4D 8B F9 4C 8B 45 67 BA 18 00 00 00 "
            "48 8B 04 C8 48 8B 0C 02 48 8B 51 50 41 0F B7 CE 48 8D 04 49 48 8B 74 C2 10 4D 85 C0 74 14 F2 41 0F 10 00 "
            "41 8B 40 08 F2 0F 11 44 24 50 89 45 87 EB 21 0F B7 86 7A 04 00 00 48 8D 54 24 50 41 8B CE 83 3C 30 06 75 "
            "07 E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? 80 7D 77 00 74 30 83 7D 7F 01 F2 0F 10 86 EC 04 00 00 F2 0F 11 07 "
            "8B 86 F4 04 00 00 89 47 08 75 15 F2 0F 10 86 E0 04 00 00 F2 0F 11 07 8B 86 E8 04 00 00 89 47 08") &&
        Halo4SeatSightUnique(base, size, 0x617E60, "41 8A C6 44 88 44 24 38 F6 D8 48 8D 85 08 01 00 00 44 88 7C 24 30 "
            "48 1B C9 4C 8D 45 30 48 23 C8 48 8B 45 E8 48 89 4C 24 28 8B CB 48 89 44 24 20 E8 ?? ?? ?? ??") &&
        Halo4SeatSightCalls(base, 0x617E90, 0x5F3510) &&
        Halo4SeatSightUnique(base, size, 0x6180CC, "44 0F B6 4C 24 56 48 8D 45 30 48 8B 55 00 4C 8D 45 08 48 89 44 24 "
            "38 44 33 CE 48 8D 45 40 89 7D 08 48 89 44 24 30 41 8B CD 48 8D 45 50 48 89 44 24 28 44 88 7C 24 20 E8 ?? "
            "?? ?? ??") &&
        Halo4SeatSightCalls(base, 0x618103, 0x1DCB8C))
        ready |= kH4SeatShotLine;
    else
        LOG("SIGHT Halo 4 seats: the shot line (0x5F3510's unit-aim block 0x5F351B, the fire routine's aim call "
            "0x617E60 and assist call 0x6180CC sharing [rbp+0x30]) did not prove; the line the assist turns is "
            "unknown, so the assist witness cannot judge seated shots (it decides nothing)");
    f.ready.store(ready, std::memory_order_release);
    LOG("SIGHT Halo 4 seats (dev): witnesses %s -- seat flags %d, controlled unit %d, object frame %d, barrel %d, "
        "aiming unit %d, aim camera %d, assist exit %d, shot line %d",
        ready == kH4SeatAllAnchors ? "all proven" : "PARTIAL", (ready & kH4SeatFlags) ? 1 : 0,
        (ready & kH4SeatControlled) ? 1 : 0, (ready & kH4SeatObjectFrame) ? 1 : 0, (ready & kH4SeatBarrel) ? 1 : 0,
        (ready & kH4SeatAimUnit) ? 1 : 0, (ready & kH4SeatAimCamera) ? 1 : 0, (ready & kH4SeatAssistWitness) ? 1 : 0,
        (ready & kH4SeatShotLine) ? 1 : 0);
}

#endif   // HALOMCCVR_DEV_DIAGNOSTICS
