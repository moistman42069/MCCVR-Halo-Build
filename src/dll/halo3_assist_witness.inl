// Included in game.cpp (inside its anonymous namespace) right after halo3_dual_wield_runtime.inl: it reads the firing
// weapon (g_halo3FiringWeapon, Halo3WeaponOwner) that lane keeps, and the gun band's summary (halo3_vehicle_gun_band.inl).
//
// THE FIRING ASSIST WITNESS, Halo 3 (dev builds only). The firing assist 0x13BAD0, called once per shot by
// the fire routine at 0x368DFE (return 0x368E03), turns the shot's direction (argument 7, in place) from the barrel's
// line toward the point the seat camera's crosshair trace hits -- in a seat by at most max(21 deg, Q) (the gun band's
// cap). This hook records the direction before and after the native for every shot of a SEATED local player (its own
// weapon, the vehicle gun it controls, a turret), and scores it:
//
//   SHOT Halo 3 assist (dev): N seated shots; turned mean X max Y deg; cap hits C (the cap Z deg); residual to the
//   traced point mean A max B deg (M without a fresh point)
//
// "Residual" is the angle between the shot as it leaves and the line from its origin to the point the seated intent
// ray's trace hit -- the crosshair point while the aim is converged: ~0 when the assist could bring the shot there, the
// overshoot past the band when it could not. Nothing reads any of it back.
//
// The hook forwards every caller exactly, through CallNativeWithSpareSlots: `py -3 tools/audit_caller_area.py
// halo3.dll 13BAD0:11:i` is clean (highest caller-area slot entry+0x38 = argument 7; one native caller, 0x368DFE,
// none relies on a volatile register across the call). Its entry is the dual lane's unique pinned prologue, and the
// call edge 0x368DFE -> 0x13BAD0 is checked again here. ADDR-HYGIENE: dev-only (a witness; decides nothing).

#if HALOMCCVR_DEV_DIAGNOSTICS
using Halo3AssistFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

struct Halo3AssistWitness
{
    void* target = nullptr;
    Halo3AssistFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    uintptr_t fireReturn = 0;
    // The window (cold worker, every 2 s).
    std::atomic<uint32_t> shots{0}, capHits{0}, noPoint{0}, faults{0};
    std::atomic<uint64_t> turnSumMicro{0}, residualSumMicro{0};   // microdegrees
    std::atomic<uint32_t> residualShots{0};
    std::atomic<uint32_t> turnMaxBits{0}, residualMaxBits{0};
    std::atomic<float> capDeg{0.0f};
};
Halo3AssistWitness g_halo3AssistWitness;

// The assist the fire routine is running on this thread (dev; the shot impact witness, halo3_impact_witness.inl):
// the shot's origin and the fire routine's stack pointer (its frame holds the inheritance the round will get), set
// only for the fire routine's own call and only while it runs. `last*`: that fire's assist output, for its spawn.
struct Halo3AssistShotContext
{
    bool active = false;
    float origin[3]{};
    uintptr_t fireRsp = 0;
    bool lastValid = false;
    float lastOrigin[3]{}, lastPost[3]{};
};
thread_local Halo3AssistShotContext t_halo3AssistShot;

namespace
{
float Halo3AssistAngleDeg(const float a[3], const float b[3])
{
    return seat_frame::SeparationDeg(a, b);
}

// Whose shot: the local player's, seated -- its own weapon, or a gun the vehicle or turret it sits in holds. Guarded.
bool Halo3AssistSeatedShot(uint32_t owner)
{
    __try
    {
        if (!g_halo3PlayerUnitGetter || owner == UINT32_MAX)
            return false;
        const uint32_t player = static_cast<uint32_t>(g_halo3PlayerUnitGetter(0));
        const auto* occupant = Halo3MeleeSelectionObject(player, 0);
        if (!occupant)
            return false;
        const uint32_t parent = *reinterpret_cast<const uint32_t*>(occupant + kHalo3ObjectParentOffset);
        return parent != UINT32_MAX && (owner == player || owner == parent);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_halo3AssistWitness.faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

void Halo3AssistWitnessMeasure(const float pre[3], const float post[3], const float origin[3])
{
    auto& w = g_halo3AssistWitness;
    const uint32_t owner = Halo3WeaponOwner(g_halo3FiringWeapon);
    if (!seat_frame::Finite3(pre) || !seat_frame::Finite3(post) || !seat_frame::Finite3(origin) ||
        !Halo3AssistSeatedShot(owner))
        return;
    const float turned = Halo3AssistAngleDeg(pre, post);
    if (!std::isfinite(turned))
        return;
    w.shots.fetch_add(1, std::memory_order_relaxed);
    w.turnSumMicro.fetch_add(static_cast<uint64_t>(turned * 1.0e6f), std::memory_order_relaxed);
    aim_caller::AtomicMaxFloat(w.turnMaxBits, turned);
    // The cap: the gun band's (its vehicle's own weapon angle) when this is that vehicle's shot, else the floor.
    float cap = g_halo3GunBand.floor > 0.0f ? g_halo3GunBand.floor : 0.36651915f;
    const auto& band = g_halo3GunBandSummary;
    if (band.vehicle.load(std::memory_order_relaxed) == owner && band.state.load(std::memory_order_relaxed) == 2)
    {
        const float bandCap = band.cap.load(std::memory_order_relaxed);
        if (std::isfinite(bandCap) && bandCap > 0.0f)
            cap = bandCap;
    }
    const float capDeg = cap * 57.29578f;
    w.capDeg.store(capDeg, std::memory_order_relaxed);
    if (turned >= capDeg - 0.05f)
        w.capHits.fetch_add(1, std::memory_order_relaxed);
    // The point the seated intent ray's trace hit (the crosshair point while converged).
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo3);
    SightRay ray{};
    SightTrace trace{};
    if (!Sight_ReadRay(GameTitle::Halo3, generation, kSightRayFreshMs, ray) ||
        !Sight_ReadTrace(GameTitle::Halo3, generation, kSightFreshMs, trace))
    {
        w.noPoint.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float range = trace.AimRangeWu();
    const float toPoint[3]{ray.head[0] + ray.dir[0] * range - origin[0], ray.head[1] + ray.dir[1] * range - origin[1],
                           ray.head[2] + ray.dir[2] * range - origin[2]};
    const float residual = Halo3AssistAngleDeg(post, toPoint);
    if (!std::isfinite(residual))
    {
        w.noPoint.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    w.residualShots.fetch_add(1, std::memory_order_relaxed);
    w.residualSumMicro.fetch_add(static_cast<uint64_t>(residual * 1.0e6f), std::memory_order_relaxed);
    aim_caller::AtomicMaxFloat(w.residualMaxBits, residual);
}
} // namespace

__declspec(noinline) uint64_t __fastcall Halo3AssistWitnessDetour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                                                   uint64_t a5, uint64_t a6, uint64_t a7)
{
    auto& w = g_halo3AssistWitness;
    w.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        auto* direction = reinterpret_cast<float*>(a7);
        const auto* origin = reinterpret_cast<const float*>(a5);
        float pre[3]{};
        const bool measure = reinterpret_cast<uintptr_t>(_ReturnAddress()) == w.fireReturn && direction && origin;
        if (measure)
        {
            memcpy(pre, direction, sizeof(pre));
            auto& shot = t_halo3AssistShot;
            memcpy(shot.origin, origin, sizeof(shot.origin));
            // The fire routine's rsp at its call: our entry rsp holds its return address, its rsp is one slot above.
            shot.fireRsp = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) + sizeof(void*);
            shot.active = true;
        }
        if (w.original)
            result = CallNativeWithSpareSlots(w.original, a1, a2, a3, a4, a5, a6, a7);
        if (measure)
        {
            float post[3]{}, from[3]{};
            memcpy(post, direction, sizeof(post));
            memcpy(from, origin, sizeof(from));
            auto& shot = t_halo3AssistShot;
            shot.lastValid = true;
            memcpy(shot.lastOrigin, from, sizeof(shot.lastOrigin));
            memcpy(shot.lastPost, post, sizeof(shot.lastPost));
            Halo3AssistWitnessMeasure(pre, post, from);
        }
    }
    __finally
    {
        t_halo3AssistShot.active = false;
        w.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

bool RemoveHalo3AssistWitness()
{
    auto& w = g_halo3AssistWitness;
    if (!w.target)
        return true;
    const auto status = MCCVR_DisableHookForRetirement(w.target);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
    {
        LOG("SHOT Halo 3 assist witness CleanupRequired: the hook did not disable");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo3AssistWitnessDetour)};
    const void* trampolines[]{reinterpret_cast<const void*>(w.original)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, w.callbacks))
    {
        LOG("SHOT Halo 3 assist witness CleanupRequired: the witness is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(w.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("SHOT Halo 3 assist witness CleanupRequired: the hook was not removed");
        return false;
    }
    w.target = nullptr;
    w.original = nullptr;
    return true;
}

// After the dual lane (it proved the assist's entry and edge first). Its own transaction: failing it costs only the line.
void InstallHalo3AssistWitness(uintptr_t base, size_t size)
{
    auto& w = g_halo3AssistWitness;
    if (w.target)
    {
        LOG("SHOT Halo 3 assist witness: a previous hook has not retired; no assist line");
        return;
    }
    constexpr uint32_t kAssist = 0x13BAD0, kFireCall = 0x368DFE;
    static constexpr char kPrologue[] = "48 8B C4 44 88 48 20 44 89 40 18 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 "
                                        "D8 FE FF FF 48 81 EC E8 01 00 00 44 8B 15";
    const uintptr_t hit = sig::Find(base, size, kPrologue);
    const auto* call = reinterpret_cast<const uint8_t*>(base + kFireCall);
    int32_t rel = 0;
    if (kFireCall + 5 <= size)
        memcpy(&rel, call + 1, sizeof(rel));
    if (hit != base + kAssist || sig::Find(hit + 1, base + size - hit - 1, kPrologue) || kFireCall + 5 > size ||
        call[0] != 0xE8 || base + kFireCall + 5 + rel != base + kAssist)
    {
        LOG("SHOT Halo 3 assist witness: the assist (+0x%X) or the fire routine's call (+0x%X) did not prove; no assist "
            "line", kAssist, kFireCall);
        return;
    }
    w.fireReturn = base + kFireCall + 5;
    void* const target = reinterpret_cast<void*>(base + kAssist);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Halo3AssistWitnessDetour),
                      reinterpret_cast<void**>(&w.original)) != MH_OK)
    {
        w.original = nullptr;
        LOG("SHOT Halo 3 assist witness: the hook was not created; no assist line");
        return;
    }
    w.target = target;
    if (MCCVR_EnableHook(target) != MH_OK)
    {
        LOG("SHOT Halo 3 assist witness: the hook did not enable; no assist line");
        (void)RemoveHalo3AssistWitness();
        return;
    }
    LOG("SHOT Halo 3 assist witness (dev): hooked +0x%X for the fire routine's call +0x%X: every seated local shot's turn, "
        "cap hits and residual to the traced point", kAssist, kFireCall);
}

// Cold worker (GunAimReport): the window.
void Halo3AssistWitnessReport()
{
    auto& w = g_halo3AssistWitness;
    const uint32_t shots = w.shots.exchange(0, std::memory_order_relaxed);
    const uint32_t faults = w.faults.exchange(0, std::memory_order_relaxed);
    if (!shots && !faults)
        return;
    const uint32_t capHits = w.capHits.exchange(0, std::memory_order_relaxed);
    const uint32_t noPoint = w.noPoint.exchange(0, std::memory_order_relaxed);
    const uint32_t residualShots = w.residualShots.exchange(0, std::memory_order_relaxed);
    const double turnSum = static_cast<double>(w.turnSumMicro.exchange(0, std::memory_order_relaxed)) * 1.0e-6;
    const double residualSum = static_cast<double>(w.residualSumMicro.exchange(0, std::memory_order_relaxed)) * 1.0e-6;
    const float turnMax = aim_caller::BitsToFloat(w.turnMaxBits.exchange(0, std::memory_order_relaxed));
    const float residualMax = aim_caller::BitsToFloat(w.residualMaxBits.exchange(0, std::memory_order_relaxed));
    LOG("SHOT Halo 3 assist (dev): %u seated shots; turned mean %.2f max %.2f deg; cap hits %u (the cap %.1f deg); "
        "residual to the traced point mean %.2f max %.2f deg (%u without a fresh point); faults %u",
        shots, shots ? turnSum / shots : 0.0, turnMax, capHits, w.capDeg.load(std::memory_order_relaxed),
        residualShots ? residualSum / residualShots : 0.0, residualMax, noPoint, faults);
}
#endif
