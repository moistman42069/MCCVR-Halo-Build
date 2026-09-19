// H3EK A844A0 weapon_barrel_fire -> firing-data helper; pinned retail
// 3683A0 -> 3524B0 at 368B92. H3's five-argument outer ABI is distinct from
// H2 and ODST. See DUAL-WIELD-REFINEMENT-2026-09-09.md and offline verifier.
using Halo3DualFireFn = uint64_t(__fastcall*)(uint32_t, int16_t, void*, int32_t, uint8_t);
using Halo3DualAimFn = void(__fastcall*)(uint32_t, float*, float*, uint64_t, float*, uint8_t, uint8_t);
using Halo3DualQueryFn = void(__fastcall*)(int32_t,uint8_t,float*,int16_t,float*,void*);
using Halo3DualCameraFn = int32_t(__fastcall*)(uint32_t,float*,float*);
struct Halo3DualRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* fireTarget = nullptr;
    void* aimTarget = nullptr;
    Halo3DualFireFn fireOriginal = nullptr;
    Halo3DualAimFn aimOriginal = nullptr;
    void* queryTarget=nullptr;
    void* cameraTarget=nullptr;
    Halo3DualQueryFn queryOriginal=nullptr;
    Halo3DualCameraFn cameraOriginal=nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> rays[2]{}, refused{0};
    std::atomic<uint64_t> nativeQueries{0},targetRestoresRefused{0};
    std::atomic<uint64_t> fireEntries{0},fireReturns{0},fireUnwinds{0},fireWithData{0},firePredicted{0};
    DualWeaponAimPublication aim;
} g_halo3Dual;
thread_local uint32_t g_halo3FiringWeapon = UINT32_MAX;

// Read-only, full-salt native inventory. H3EK A5DE20/A844A0 and retail
// 35A9A4/3683A0/356388 prove role bytes +262/+263 and four handles +268.
// The object entry and owner fields are independently H3-proven, not H2 copies.
bool Halo3ReadOwnedWeapons(uint32_t owner, uint32_t weapons[2],bool requireDual)
{
    const auto* unit = Halo3MeleeSelectionObject(owner, 0);
    if (!unit) return false;
    const uint8_t roles[2]{unit[0x262], unit[0x263]};
    if (roles[0] >= 4 || (roles[1] >= 4 && (requireDual || roles[1]!=0xFF)) ||
        roles[0] == roles[1]) return false;
    for (int slot = 0; slot < 2; ++slot)
    {
        if(roles[slot]==0xFF) {weapons[slot]=UINT32_MAX;continue;}
        weapons[slot] = *reinterpret_cast<const uint32_t*>(unit + 0x268 + roles[slot] * 4);
        const auto* weapon = Halo3MeleeSelectionObject(weapons[slot], 2);
        if (!weapon || !weapon[0x15D] ||
            *reinterpret_cast<const uint32_t*>(weapon + 0x168) != owner) return false;
    }
    return weapons[0] != weapons[1];
}
bool Halo3ReadOwnedDualWeapons(uint32_t owner,uint32_t weapons[2])
{return Halo3ReadOwnedWeapons(owner,weapons,true);}

#include "halo3_muzzle_publication.inl"

// Runs at the existing admitted stereo boundary. The native firing thread
// receives one coherent pair of room-to-world rays and never takes a VR lock.
void PublishHalo3DualAimBody()
{
    auto& feature = g_halo3Dual;
    if (!feature.enabled.load(std::memory_order_acquire) ||
        feature.faulted.load(std::memory_order_acquire) || !g_vrAim.load() ||
        !g_config.independent_dual_aim ||
        !g_enabled.load() || !g_baseCamValid.load() ||
        feature.generation != g_halo3RuntimeGeneration.load()) return;
    __try
    {
        int32_t scene = -1, shot = -1;
        if (ReadCinematicControl(scene, shot) != CinematicControlState::PlayerControlled)
            return;
        if (!g_halo3PlayerUnitGetter || !g_halo3UnitInVehicle) return;
        DualWeaponAimSnapshot sample{};
        sample.unit = static_cast<uint32_t>(g_halo3PlayerUnitGetter(0));
        if (!Halo3ReadOwnedDualWeapons(sample.unit, sample.weapons) ||
            g_halo3UnitInVehicle(static_cast<int32_t>(sample.unit))) return;
        VrContactTrackingSnapshot tracking{};
        if (!VR_GetContactTrackingSnapshot(tracking) || !tracking.referenceEpoch ||
            !tracking.hands[0].valid || !tracking.hands[1].valid) return;
        sample.generation = feature.generation;
        sample.trackingEpoch = tracking.referenceEpoch;
        sample.timeNs = tracking.timeNs;
        sample.sampleMs = GetTickCount64();
        const float scale = g_worldScale.load();
        if (!std::isfinite(scale) || scale <= 0) return;
        const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
        const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
        const float base[3]{g_baseCamX.load(), g_baseCamY.load(), g_baseCamZ.load()};
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto& hand = tracking.hands[slot == 0 ? 1 : 0];
            float basis[9]{};
            BuildTrackedGameBasisFromFrame(hand.orientation, false, false, 0, 0, basis);
            if (slot == 1)
            {
                // Same mirrored support-role trajectory trim as H3's hand.
                float mount[9]{}, trimmed[9]{};
                BasisFromAngles(-g_config.gun_yaw_deg * 0.01745329252f,
                    g_config.gun_pitch_deg * 0.01745329252f,
                    -g_config.gun_roll_deg * 0.01745329252f, mount);
                MultiplyBases(basis, mount, trimmed);
                memcpy(basis, trimmed, sizeof(basis));
            }
            const float dx = hand.position[0] - g_headPosRef[0];
            const float dy = hand.position[1] - g_headPosRef[1];
            const float dz = hand.position[2] - g_headPosRef[2];
            const float forward = dx * sh - dz * ch;
            const float right = dx * ch + dz * sh;
            sample.positions[slot][0] = base[0] + (cg * forward + sg * right) * scale;
            sample.positions[slot][1] = base[1] + (sg * forward - cg * right) * scale;
            sample.positions[slot][2] = base[2] + dy * scale;
            memcpy(sample.directions[slot], basis, sizeof(sample.directions[slot]));
        }
        (void)feature.aim.Publish(sample);
        VR_ObserveSecondaryWeaponPresentation(GameTitle::Halo3, feature.generation);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    { feature.faulted.store(true, std::memory_order_release); }
}

__declspec(noinline) void PublishHalo3DualAim()
{
    g_halo3Dual.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try { PublishHalo3DualAimBody(); }
    __finally { g_halo3Dual.callbacks.fetch_sub(1, std::memory_order_acq_rel); }
}

__declspec(noinline) uint64_t __fastcall Halo3DualFireDetour(
    uint32_t weapon, int16_t barrel, void* data, int32_t index, uint8_t predicted)
{
    auto& feature = g_halo3Dual;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const uint32_t previous = g_halo3FiringWeapon;
    g_halo3FiringWeapon = weapon;
    uint64_t result = 0;
    __try
    {
        if (feature.fireOriginal)
            result = feature.fireOriginal(weapon, barrel, data, index, predicted);
    }
    __finally
    {
        g_halo3FiringWeapon = previous;
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

__declspec(noinline) void __fastcall Halo3DualAimDetour(uint32_t unit,
    float* origin, float* direction, uint64_t marker, float* offset,
    uint8_t projectOrigin, uint8_t useUnitAim)
{
    auto& feature = g_halo3Dual;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.aimOriginal) __leave;
        feature.aimOriginal(unit, origin, direction, marker, offset, projectOrigin, useUnitAim);
        if (!feature.enabled.load(std::memory_order_acquire) || feature.faulted.load() ||
            reinterpret_cast<uintptr_t>(_ReturnAddress()) != feature.base + 0x368B97 ||
            !origin || !direction || !g_vrAim.load() || !g_enabled.load() ||
            !VR_IsStereoEnabled() || TitleAdapter_GetActiveTitle() != GameTitle::Halo3 ||
            feature.generation != g_halo3RuntimeGeneration.load()) __leave;
        __try
        {
            uint32_t weapons[2]{};
            DualWeaponAimSnapshot sample{};
            VrContactTrackingSnapshot tracking{};
            if (!g_halo3PlayerUnitGetter || !g_halo3UnitInVehicle ||
                unit != static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)) ||
                g_halo3UnitInVehicle(static_cast<int32_t>(unit)) ||
                !Halo3ReadOwnedDualWeapons(unit, weapons)) __leave;
            const int slot = ResolveEquippedWeaponSlot(g_halo3FiringWeapon,
                weapons[0], weapons[1], true, true);
            if (slot < 0) __leave;
            float candidate[3]{};
            if (!feature.aim.Read(sample) || !VR_GetContactTrackingSnapshot(tracking) ||
                !tracking.hands[slot == 0 ? 1 : 0].valid ||
                !DualWeaponAimFresh(sample, feature.generation, unit, tracking.referenceEpoch,
                    GetTickCount64(), feature.installedAtMs, tracking.timeNs) ||
                sample.weapons[0] != weapons[0] || sample.weapons[1] != weapons[1] ||
                !BuildIndependentWeaponDirection(origin, sample.positions[slot],
                    sample.directions[slot],
                    std::clamp(g_config.crosshair_distance_m, 2.0f, 50.0f) * Game_GetWorldScale(),
                    candidate))
            {
                feature.refused.fetch_add(1, std::memory_order_relaxed);
                __leave;
            }
            memcpy(direction, candidate, sizeof(candidate));
            feature.rays[slot].fetch_add(1, std::memory_order_relaxed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        { feature.faulted.store(true, std::memory_order_release); }
    }
    __finally { feature.callbacks.fetch_sub(1, std::memory_order_acq_rel); }
}

#include "halo3_independent_shots.inl"

#include "halo3_muzzle_lifecycle.inl"
#include "halo3_dual_lifecycle.inl"
