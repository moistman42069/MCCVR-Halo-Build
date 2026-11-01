// Included in game.cpp (inside its anonymous namespace) after halo3_contact_melee_runtime.inl (Halo3MeleeSelectionObject)
// and before halo3_dual_wield_runtime.inl, whose shot-line detour (halo3_independent_shots.inl) calls
// Halo3GunBandNoteShot. legacy_direct_drive.inl (much later) stamps the band alive on a driver's update; the aim poll
// (Game_ComputeAimStick -> SeatLimitReticleStep) reads it.
//
// THE GUN BAND of a look-steered land driver, Halo 3 -- the simulation thread's half (src/common/vehicle_gun_band_logic.h
// has the why; docs/HALO3-VEHICLE-EVIDENCE.md "Vehicle guns: the barrel, the assist and its seat cap").
//
//   * THE CAP, bound at install. The firing assist 0x13BAD0 caps a seated shot's turn at max(floor, Q): the cap block
//     0x13C2EB loads the floor (rip-relative: 0.36651915 rad, 21 deg) and maxes it with [rbp+0x3C], the assist's query
//     0x13C518's output +0x1C (handed [rbp+0x20] at 0x13BBD7), which the tag read 0x13C6FC fills from the weapon tag:
//     +0x340, maxed with +0x328 unless the flags +0x18C carry bit 25, over the zoom magnification (1 unzoomed). Each
//     site is a signature UNIQUE in the module and matched at its RVA; the floor's address, the two frame offsets, the
//     tag offsets and the bit are DECODED from them and must agree (the cap reads the very slot the query fills, which
//     the tag read writes; the floor holds 21 deg). ADDR-HYGIENE: resolved (unique pinned signatures, decoded and
//     cross-checked) + guarded (ValueAgreement on the floor; every tag read is plausibility-checked: TagAngle).
//     Not proven: no band, one LOG line. Q is read unzoomed: a zoomed Q is smaller, so the cap can only be wider than
//     the game's (fails open toward the old, unclamped reticle; vehicles here do not zoom).
//   * THE BARRELS, from shots (Halo3GunBandNoteShot, the shot line's firing call, 0x368B97): a shot of the local
//     player's ROOT vehicle with the unit-aim flag clear leaves along its barrel marker, before the assist turns it.
//     Its direction and origin go into the vehicle's object frame (+0x50 position, +0x5C forward, +0x68 up, the frame
//     the seat sampler validates) and teach gun_band::Learner (animated barrels refused).
//   * THE STAMP: a look-steered driver's control update marks the band alive (Halo3GunBandMarkDriver); the aim poll
//     uses it only within 150 ms of that, for the same vehicle handle and definition its own snapshot names.
// Simulation thread only (the firing routine and the control update); the summary is a seqlock for the input thread.

struct Halo3GunBandBinding
{
    std::atomic<bool> ready{false};
    uint32_t generation = 0;
    int32_t tagFlags = 0, tagPrimary = 0, tagSecondary = 0;
    uint8_t tagShift = 0;
    float floor = 0.0f;
    uintptr_t base = 0;
};
Halo3GunBandBinding g_halo3GunBand;

// The learner (simulation thread only: the firing routine runs there).
gun_band::Learner g_halo3GunBandLearner;
uint32_t g_halo3GunBandLearnerGeneration = 0;

// What the shot hook learned, for the aim poll and the cold worker (seqlock: one writer, any reader).
struct Halo3GunBandSummary
{
    std::atomic<uint32_t> seq{0};
    std::atomic<uint32_t> generation{0}, vehicle{0xFFFFFFFFu}, definition{0xFFFFFFFFu};
    // 0 none, 1 learning (a shot seen, no band yet), 2 band, 3 animated, 4 the weapon's angle did not read (no band)
    std::atomic<uint32_t> state{0};
    std::atomic<float> lo{0.0f}, hi{0.0f};            // the band, elevation relative to the hull's up (radians)
    std::atomic<float> elevLo{0.0f}, elevHi{0.0f};    // the barrels' own range
    std::atomic<float> cap{0.0f}, q{0.0f}, animatedDeg{0.0f};
    std::atomic<float> origin[3]{};                   // the shots' origin, hull frame (world units)
    std::atomic<uint32_t> shots{0};
    // The driver's control update, last time (ms): the band is current only near it.
    std::atomic<uint64_t> aliveMs{0};
    // Shot counters for the cold worker (relaxed).
    std::atomic<uint32_t> seen{0}, learned{0}, deviant{0}, refused{0}, faults{0};
};
Halo3GunBandSummary g_halo3GunBandSummary;

constexpr uint64_t kHalo3GunBandFreshMs = 150;

void RemoveHalo3GunBand()
{
    g_halo3GunBand.ready.store(false, std::memory_order_release);
    g_halo3GunBandSummary.aliveMs.store(0, std::memory_order_release);
}

// Install thread: bind the assist's cap (the header). Fail closed: no band, said once.
bool InstallHalo3GunBand(uintptr_t base, size_t size, uint32_t generation)
{
    auto& b = g_halo3GunBand;
    b.ready.store(false, std::memory_order_release);
    const auto unique = [&](uint32_t rva, const char* pattern) {
        const uintptr_t hit = sig::Find(base, size, pattern);
        return hit == base + rva && !sig::Find(hit + 1, base + size - hit - 1, pattern);
    };
    const auto i32 = [&](uintptr_t at) {
        int32_t v = 0;
        memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
        return v;
    };
    constexpr uint32_t kAssist = 0x13BAD0, kQueryCall = 0x13BBD7, kTagRead = 0x13C6FC, kCapBlock = 0x13C2EB;
    const char* why = nullptr;
    uint32_t queryRva = 0;
    if (!unique(kAssist, "48 8B C4 44 88 48 20 44 89 40 18 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 FE FF FF "
                         "48 81 EC E8 01 00 00 44 8B 15"))
        why = "the firing assist's entry";
    else if (!unique(kQueryCall, "4C 8D 4D ?? 45 0F B7 C7 48 8D 95 30 01 00 00 8B CB E8 ?? ?? ?? ??"))
        why = "the assist's call to its weapon query";
    else if (!unique(kTagRead, "8B 86 ?? ?? 00 00 F3 0F 10 86 ?? ?? 00 00 C1 E8 ?? 41 84 C7 75 08 F3 0F 5F 86 ?? ?? 00 00 "
                               "F3 0F 59 E0 F3 0F 11 63 ??"))
        why = "the query's weapon-tag read";
    else if (!unique(kCapBlock, "F3 0F 10 35 ?? ?? ?? ?? F3 0F 5F 75 ?? 0F 28 C6 E8 ?? ?? ?? ?? F3 0F 10 5C 24 44 0F 28 F8"))
        why = "the assist's seat cap";
    int32_t outDisp = 0, outOff = 0, capDisp = 0;
    float floor = 0.0f;
    if (!why)
    {
        const uintptr_t call = base + kQueryCall + 17;
        queryRva = static_cast<uint32_t>(call + 5 + i32(call + 1) - base);
        outDisp = *reinterpret_cast<const uint8_t*>(base + kQueryCall + 3);
        outOff = *reinterpret_cast<const uint8_t*>(base + kTagRead + 38);
        capDisp = *reinterpret_cast<const uint8_t*>(base + kCapBlock + 12);
        const uintptr_t floorAt = base + kCapBlock + 8 + i32(base + kCapBlock + 4);
        if (floorAt < base || floorAt + sizeof(float) > base + size)
            why = "the cap's floor lies outside the module";
        else
            memcpy(&floor, reinterpret_cast<const void*>(floorAt), sizeof(floor));
        if (!why && !(kTagRead > queryRva && kTagRead - queryRva < 0x400))
            why = "the weapon-tag read is not in the query the assist calls";
        else if (!why && !(kCapBlock > kAssist && kCapBlock - kAssist < 0x900))
            why = "the seat cap is not in the assist";
        else if (!why && capDisp != outDisp + outOff)
            why = "the cap does not read the slot the tag read fills";
        else if (!why && !(std::fabs(floor - 0.36651915f) < 1.0e-5f))
            why = "the cap's floor is not 21 deg";
    }
    if (why)
    {
        LOG("Halo 3 gun band StockFallback: %s did not prove; a look-steered driver's reticle is not held to its guns' "
            "elevation (as before)", why);
        return false;
    }
    b.tagFlags = i32(base + kTagRead + 2);
    b.tagPrimary = i32(base + kTagRead + 10);
    b.tagShift = *reinterpret_cast<const uint8_t*>(base + kTagRead + 16);
    b.tagSecondary = i32(base + kTagRead + 26);
    b.floor = floor;
    b.base = base;
    b.generation = generation;
    if (b.tagShift > 31 || b.tagFlags <= 0 || b.tagPrimary <= 0 || b.tagSecondary <= 0 || b.tagFlags > 0x1000 ||
        b.tagPrimary > 0x1000 || b.tagSecondary > 0x1000)
    {
        LOG("Halo 3 gun band StockFallback: the weapon-tag offsets did not decode plausibly; no band");
        return false;
    }
    g_halo3GunBandLearner = gun_band::Learner{};
    g_halo3GunBandLearnerGeneration = 0;
    b.ready.store(true, std::memory_order_release);
    LOG("Halo 3 gun band Installed: a seated shot turns at most max(%.1f deg (the cap +0x%X), the weapon tag's +0x%X "
        "maxed with +0x%X unless flags +0x%X bit %u) -- the slot the query +0x%X fills (+0x%X) -- so a look-steered "
        "driver's reticle is held to its guns' elevation +/- that cap, learned from the barrels' shots",
        floor * 57.29578f, kCapBlock, b.tagPrimary, b.tagSecondary, b.tagFlags, b.tagShift, queryRva, outOff);
    return true;
}

namespace
{
// The weapon's own angle Q from its tag (unzoomed), or NaN. Guarded.
float Halo3GunBandWeaponAngle(uint32_t weapon)
{
    const auto& b = g_halo3GunBand;
    __try
    {
        const auto* object = Halo3MeleeSelectionObject(weapon, 2);
        const auto* tag = object ? Halo3LoadedTagDefinition(*reinterpret_cast<const uint32_t*>(object)) : nullptr;
        if (!tag)
            return std::nanf("");
        uint32_t flags = 0;
        float primary = 0.0f, secondary = 0.0f;
        memcpy(&flags, tag + b.tagFlags, sizeof(flags));
        memcpy(&primary, tag + b.tagPrimary, sizeof(primary));
        memcpy(&secondary, tag + b.tagSecondary, sizeof(secondary));
        return gun_band::TagAngle(flags, b.tagShift, primary, secondary);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return std::nanf("");
    }
}

// The vehicle `owner` the local player `unit` sits in directly, as a ROOT vehicle: its definition and object frame.
// False for anything else (another unit's shot, a turret attached to a carrier, a stale handle). Guarded.
bool Halo3GunBandVehicle(uint32_t unit, uint32_t owner, uint32_t& definition, float pos[3], float fwd[3], float up[3])
{
    __try
    {
        if (!g_halo3PlayerUnitGetter || unit != static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)))
            return false;
        const auto* occupant = Halo3MeleeSelectionObject(unit, 0);
        const auto* vehicle = Halo3MeleeSelectionObject(owner, kHalo3ObjectKindVehicle);
        if (!occupant || !vehicle || *reinterpret_cast<const uint32_t*>(occupant + kHalo3ObjectParentOffset) != owner ||
            *reinterpret_cast<const int32_t*>(vehicle + kHalo3ObjectParentOffset) != -1)
            return false;
        definition = *reinterpret_cast<const uint16_t*>(vehicle);
        memcpy(pos, vehicle + kHalo3ObjectPositionOffset, 3 * sizeof(float));
        memcpy(fwd, vehicle + kHalo3ObjectForwardOffset, 3 * sizeof(float));
        memcpy(up, vehicle + kHalo3ObjectUpOffset, 3 * sizeof(float));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_halo3GunBandSummary.faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

void Halo3GunBandPublish(const gun_band::Learner& l, uint32_t generation, float q)
{
    auto& s = g_halo3GunBandSummary;
    // Q unreadable: NO band (never the bare floor -- the weapon's own angle may be wider, and a cap narrower than the
    // game's would hold the reticle short of shots that land).
    const float cap = std::isfinite(q) ? gun_band::Cap(g_halo3GunBand.floor, q) : std::nanf("");
    float lo = 0.0f, hi = 0.0f, elevLo = 0.0f, elevHi = 0.0f, origin[3]{};
    const bool band = gun_band::Band(l, cap, lo, hi) && l.elevationRange(elevLo, elevHi) && l.originLocal(origin);
    const bool barrels = l.learned();
    s.seq.fetch_add(1, std::memory_order_acq_rel);
    s.generation.store(generation, std::memory_order_relaxed);
    s.vehicle.store(l.vehicle, std::memory_order_relaxed);
    s.definition.store(l.definition, std::memory_order_relaxed);
    s.state.store(l.animated ? 3u : band ? 2u : (barrels && !std::isfinite(q)) ? 4u : 1u, std::memory_order_relaxed);
    s.lo.store(lo, std::memory_order_relaxed);
    s.hi.store(hi, std::memory_order_relaxed);
    s.elevLo.store(elevLo, std::memory_order_relaxed);
    s.elevHi.store(elevHi, std::memory_order_relaxed);
    s.cap.store(cap, std::memory_order_relaxed);
    s.q.store(q, std::memory_order_relaxed);
    s.animatedDeg.store(l.animatedDeg, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        s.origin[i].store(origin[i], std::memory_order_relaxed);
    s.shots.store(l.shots, std::memory_order_relaxed);
    s.seq.fetch_add(1, std::memory_order_release);
}
} // namespace

// The shot line's firing call (0x368B97), after the native aim and before the assist: one shot of `weapon`, held by
// `owner`, that the occupant `unit` fires. Learns the barrel's line in the hull frame when the shot is the local
// player's root vehicle's own barrel shot (the unit-aim flag clear).
void Halo3GunBandNoteShot(uint32_t unit, uint32_t owner, uint32_t weapon, int16_t barrel, const float* origin,
                          const float* direction, bool unitAim)
{
    auto& s = g_halo3GunBandSummary;
    if (!g_halo3GunBand.ready.load(std::memory_order_acquire) || unitAim || !origin || !direction ||
        owner == UINT32_MAX || weapon == UINT32_MAX)
        return;
    uint32_t definition = 0xFFFFFFFFu;
    float pos[3]{}, fwd[3]{}, up[3]{};
    if (!Halo3GunBandVehicle(unit, owner, definition, pos, fwd, up))
        return;
    s.seen.fetch_add(1, std::memory_order_relaxed);
    seat_frame::Frame hull{};
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                   direction[2] * direction[2]);
    if (!gun_band::HullFrame(fwd, up, hull) || !std::isfinite(length) || !(length > 1.0e-4f))
    {
        s.refused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float world[3]{direction[0] / length, direction[1] / length, direction[2] / length};
    const float offset[3]{origin[0] - pos[0], origin[1] - pos[1], origin[2] - pos[2]};
    float localDir[3]{}, localOrigin[3]{};
    gun_band::ToLocal(hull, world, localDir);
    gun_band::ToLocal(hull, offset, localOrigin);
    const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
    auto& l = g_halo3GunBandLearner;
    if (l.vehicle != owner || l.definition != definition || g_halo3GunBandLearnerGeneration != generation)
    {
        l.reset(owner, definition);
        g_halo3GunBandLearnerGeneration = generation;
    }
    switch (l.step(barrel, localDir, localOrigin))
    {
    case gun_band::Shot::Learned: s.learned.fetch_add(1, std::memory_order_relaxed); break;
    case gun_band::Shot::Deviant: s.deviant.fetch_add(1, std::memory_order_relaxed); break;
    case gun_band::Shot::Refused: s.refused.fetch_add(1, std::memory_order_relaxed); return;
    case gun_band::Shot::Animated: break;
    }
    Halo3GunBandPublish(l, generation, Halo3GunBandWeaponAngle(weapon));
}

// The control update of a look-steered driver's seat write (legacy_direct_drive.inl): the band is current now.
void Halo3GunBandMarkDriver(uint64_t nowMs)
{
    g_halo3GunBandSummary.aliveMs.store(nowMs ? nowMs : 1, std::memory_order_release);
}

// Input thread: the band of the vehicle `vehicle` / `definition` (the seat snapshot's direct parent), when the shot
// hook learned it in this generation, its cap is known, the barrels are hull-fixed, and a driver update stamped it
// within 150 ms. `lo`/`hi`: elevation relative to the hull's up; `originLocal`: the shots' origin, hull frame.
bool Halo3GunBandRead(int32_t vehicle, uint32_t definition, float& lo, float& hi, float originLocal[3])
{
    const auto& s = g_halo3GunBandSummary;
    if (!g_halo3GunBand.ready.load(std::memory_order_acquire) || vehicle == -1)
        return false;
    const uint64_t alive = s.aliveMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    if (!alive || now < alive || now - alive > kHalo3GunBandFreshMs)
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = s.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint32_t generation = s.generation.load(std::memory_order_relaxed);
        const uint32_t v = s.vehicle.load(std::memory_order_relaxed);
        const uint32_t d = s.definition.load(std::memory_order_relaxed);
        const uint32_t state = s.state.load(std::memory_order_relaxed);
        const float l = s.lo.load(std::memory_order_relaxed), h = s.hi.load(std::memory_order_relaxed);
        float o[3];
        for (int i = 0; i < 3; ++i)
            o[i] = s.origin[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) != before)
            continue;
        if (state != 2 || generation != g_halo3RuntimeGeneration.load(std::memory_order_acquire) ||
            v != static_cast<uint32_t>(vehicle) || d != definition || !std::isfinite(l) || !std::isfinite(h) || l > h ||
            !seat_frame::Finite3(o))
            return false;
        lo = l;
        hi = h;
        for (int i = 0; i < 3; ++i)
            originLocal[i] = o[i];
        return true;
    }
    return false;
}

// Cold worker (GunAimReport): the band's edges, one line per change of vehicle or state, and (dev) the shot counts.
void Halo3GunBandReport()
{
    auto& s = g_halo3GunBandSummary;
    static uint32_t s_vehicle = 0xFFFFFFFFu, s_definition = 0xFFFFFFFFu, s_state = 0;
    const uint32_t before = s.seq.load(std::memory_order_acquire);
    if (!(before & 1u))
    {
        const uint32_t vehicle = s.vehicle.load(std::memory_order_relaxed);
        const uint32_t definition = s.definition.load(std::memory_order_relaxed);
        const uint32_t state = s.state.load(std::memory_order_relaxed);
        const float elevLo = s.elevLo.load(std::memory_order_relaxed) * 57.29578f;
        const float elevHi = s.elevHi.load(std::memory_order_relaxed) * 57.29578f;
        const float lo = s.lo.load(std::memory_order_relaxed) * 57.29578f;
        const float hi = s.hi.load(std::memory_order_relaxed) * 57.29578f;
        const float cap = s.cap.load(std::memory_order_relaxed) * 57.29578f;
        const float q = s.q.load(std::memory_order_relaxed) * 57.29578f;
        const float animated = s.animatedDeg.load(std::memory_order_relaxed);
        const uint32_t shots = s.shots.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        const bool coherent = s.seq.load(std::memory_order_relaxed) == before;
        if (coherent && state && (vehicle != s_vehicle || definition != s_definition || state != s_state))
        {
            s_vehicle = vehicle;
            s_definition = definition;
            s_state = state;
            if (state == 2)
                LOG("SEAT LIMIT Halo 3 gun band: vehicle 0x%04X's guns learned from %u shots, barrel elevation %+.1f..%+.1f "
                    "deg of the hull; the assist's cap %.1f deg (the weapon's own %.1f, the floor %.1f): a driver's "
                    "reticle elevation is held to %+.1f..%+.1f deg of the hull's up", definition & 0xFFFFu, shots,
                    elevLo, elevHi, cap, q, g_halo3GunBand.floor * 57.29578f, lo, hi);
            else if (state == 3)
                LOG("SEAT LIMIT Halo 3 gun band: vehicle 0x%04X's barrels move with the aim (%.1f deg between shots): no "
                    "band for it, the reticle stays on the hand's point", definition & 0xFFFFu, animated);
            else if (state == 4)
                LOG("SEAT LIMIT Halo 3 gun band: vehicle 0x%04X's weapon angle did not read from its tag: no band for it, "
                    "the reticle stays on the hand's point", definition & 0xFFFFu);
        }
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    const uint32_t seen = s.seen.exchange(0, std::memory_order_relaxed);
    const uint32_t learned = s.learned.exchange(0, std::memory_order_relaxed);
    const uint32_t deviant = s.deviant.exchange(0, std::memory_order_relaxed);
    const uint32_t refused = s.refused.exchange(0, std::memory_order_relaxed);
    const uint32_t faults = s.faults.exchange(0, std::memory_order_relaxed);
    if (seen || faults)
        LOG("SEAT LIMIT Halo 3 gun band (dev): %u barrel shots of the driver's own vehicle (on the line %u, off it %u, "
            "refused %u; faults %u); state %u (1 learning, 2 band, 3 animated, 4 no weapon angle)", seen, learned, deviant,
            refused, faults,
            s.state.load(std::memory_order_relaxed));
#endif
}
