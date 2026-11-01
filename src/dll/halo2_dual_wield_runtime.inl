// H2EK 49C960(weapon, barrel, projectile, predicted) -> 47DC20(unit, ...).
// Retail 8E4940 -> 8F0F70; verified separately from dormant C-H2-43 code.
// With two verified owned weapons, each role overrides its own native ray.
// A single equipped weapon retains the existing native aiming path.
using Halo2DualFireFn = void(__fastcall*)(uint32_t, int16_t, int32_t, uint8_t);
using Halo2DualCameraFn = int32_t(__fastcall*)(uint32_t,float*,float*);
using Halo2DualLocationFn = const void*(__fastcall*)(uint32_t);
struct Halo2DualRuntime
{
    uintptr_t base = 0;
    void* fireTarget = nullptr;
    void* aimTarget = nullptr;
    Halo2DualFireFn fireOriginal = nullptr;
    Halo2WeaponAimHelperFn aimOriginal = nullptr;
    void* cameraTarget=nullptr;
    void* locationTarget=nullptr;
    Halo2DualCameraFn cameraOriginal=nullptr;
    Halo2DualLocationFn locationOriginal=nullptr;
    uint32_t generation=0;
    uint64_t installedAtMs=0;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> primaryRays{0}, secondaryRays{0}, refused{0};
    std::atomic<uint64_t> nativeQueries{0},targetRestoresRefused{0};
} g_halo2Dual;
thread_local uint32_t g_halo2FiringWeapon = UINT32_MAX;

// This independently verifies the complete salt and parent-unit identity.
// H2EK object_get + type mask4 and retail 8E4940 prove weapon type2,
// object parent flag+130 and parent full handle+158. No copied H3 fields.
bool Halo2DualWeaponOwned(uint32_t weapon, uint32_t unit)
{
    if (weapon == UINT32_MAX || !(weapon >> 16) || unit == UINT32_MAX) return false;
    const auto* table = *reinterpret_cast<const uint8_t* const*>(
        g_halo2Dual.base + kHalo2ObjectsDataArrayPointerRva);
    if (!table || !table[0x29] || *reinterpret_cast<const int32_t*>(table+0x24) != 0xC)
        return false;
    const int32_t capacity = *reinterpret_cast<const int32_t*>(table+0x20);
    const uintptr_t offset = *reinterpret_cast<const uintptr_t*>(table+0x48);
    if (capacity <= 0 || capacity > 0x2800 || (weapon & 0xFFFF) >= uint32_t(capacity) ||
        !offset || offset > UINTPTR_MAX-reinterpret_cast<uintptr_t>(table)-size_t(capacity)*0xC)
        return false;
    const auto* entry = table+offset+(weapon & 0xFFFF)*0xC;
    if (*reinterpret_cast<const uint16_t*>(entry) != uint16_t(weapon>>16) || entry[3] != 2)
        return false;
    const auto accessor = reinterpret_cast<Halo2ObjectDatumAccessorFn>(
        g_objectDatumAccessor.load(std::memory_order_acquire));
    const auto* object = accessor ? static_cast<const uint8_t*>(accessor(entry)) : nullptr;
    return object && (object[0x130] & 1u) &&
        *reinterpret_cast<const uint32_t*>(object+0x158) == unit;
}

__declspec(noinline) void __fastcall Halo2DualFireDetour(
    uint32_t weapon, int16_t barrel, int32_t projectile, uint8_t predicted)
{
    g_halo2Dual.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const uint32_t previous = g_halo2FiringWeapon;
    g_halo2FiringWeapon = weapon;
    __try
    {
        if (g_halo2Dual.fireOriginal)
            g_halo2Dual.fireOriginal(weapon, barrel, projectile, predicted);
    }
    __finally
    {
        g_halo2FiringWeapon = previous;
        g_halo2Dual.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

__declspec(noinline) void __fastcall Halo2DualAimDetour(uint32_t unit,
    float* origin, float* direction, uint64_t marker, float* offset,
    uint8_t projectOrigin, uint8_t useUnitAim, uint8_t collisionAdjust)
{
    g_halo2Dual.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!g_halo2Dual.aimOriginal) __leave;
        g_halo2Dual.aimOriginal(unit, origin, direction, marker, offset,
            projectOrigin, useUnitAim, collisionAdjust);
        if (!g_halo2Dual.enabled.load(std::memory_order_acquire) ||
            g_halo2Dual.faulted.load(std::memory_order_acquire) ||
            reinterpret_cast<uintptr_t>(_ReturnAddress()) != g_halo2Dual.base+0x8E4FCD ||
            !origin || !direction || !Game_Halo2ControllerAimActive() ||
            !Halo2Observer6Dof_FinalPaletteArmed() ||
            !Halo2Observer6Dof_DirectWeaponAimArmed()) __leave;
        __try
        {
            const auto* users = *reinterpret_cast<const uint8_t* const*>(
                g_halo2Dual.base+kHalo2FirstPersonUserDataPointerRva);
            if (!users) __leave;
            const auto* primary = users+kOwnedUser*kHalo2FirstPersonUserStride+
                kHalo2FirstPersonWeaponDataOffset;
            const auto* secondary = primary+kHalo2FirstPersonWeaponSlotStride;
            const uint32_t primaryHandle = *reinterpret_cast<const uint32_t*>(
                primary+kHalo2FirstPersonWeaponObjectOffset);
            const uint32_t secondaryHandle = *reinterpret_cast<const uint32_t*>(
                secondary+kHalo2FirstPersonWeaponObjectOffset);
            const int slot = ResolveEquippedWeaponSlot(g_halo2FiringWeapon, primaryHandle,
                    secondaryHandle, primaryHandle != UINT32_MAX, (secondary[0]&1u)!=0);
            if (slot < 0 || (secondary[0]&1u)==0 ||
                !Halo2DualWeaponOwned(primaryHandle, unit) ||
                !Halo2DualWeaponOwned(secondaryHandle, unit)) __leave;
            Halo2ObserverPosePublication publication{};
            VrContactTrackingSnapshot tracking{};
            Halo2CameraBasis carrier{};
            float orientation[4]{}, candidate[3]{};
            if (!Halo2Observer6Dof_ReadPublishedPose(publication) ||
                !VR_GetContactTrackingSnapshot(tracking) ||
                !Halo2DualAimPublicationFresh(publication, g_generation.load(),
                    tracking.referenceEpoch, tracking.timeNs) ||
                !tracking.hands[slot == 0 ? 1 : 0].valid)
            {
                g_halo2Dual.refused.fetch_add(1, std::memory_order_relaxed);
                __leave;
            }
            const auto& snapshot = publication.snapshot;
            const float* position = slot == 0 ? snapshot.rightAimPosition
                                              : snapshot.leftControllerPosition;
            bool aimValid = false;
            if (slot == 0)
            {
                aimValid = snapshot.independentRightAimValid;
                memcpy(orientation, snapshot.independentRightAimOrientation, sizeof(orientation));
            }
            else
                aimValid = Halo2BuildMirroredLeftAimOrientation(
                    snapshot.leftControllerOrientation, g_config.gun_yaw_deg,
                    g_config.gun_pitch_deg, g_config.gun_roll_deg, orientation);
            if (!aimValid ||
                !Halo2BuildStableControllerCarrier(Halo2ControllerReference(publication),
                    publication.referenceOrientation, publication.referencePosition,
                    orientation, position,
                    Game_GetWorldScale(), 0.0f, carrier) ||
                !Halo2BuildControllerShotDirection(origin, carrier,
                    std::clamp(g_config.crosshair_distance_m, 2.0f, 50.0f)*Game_GetWorldScale(), candidate))
            {
                g_halo2Dual.refused.fetch_add(1, std::memory_order_relaxed);
                __leave;
            }
            memcpy(direction, candidate, sizeof(candidate));
            (slot == 0 ? g_halo2Dual.primaryRays : g_halo2Dual.secondaryRays)
                .fetch_add(1, std::memory_order_relaxed);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        { g_halo2Dual.faulted.store(true, std::memory_order_release); }
    }
    __finally { g_halo2Dual.callbacks.fetch_sub(1, std::memory_order_acq_rel); }
}

#include "halo2_fire_eye.inl"
#include "halo2_seat_fire.inl"
#include "halo2_independent_shots.inl"

bool RemoveHalo2DualAim();

bool InstallHalo2DualAimLegacy(uintptr_t base, size_t size)
{
    g_halo2Dual.enabled.store(false, std::memory_order_release);
    if (g_halo2Dual.fireTarget || g_halo2Dual.aimTarget) return false;
    struct Binding { uint32_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x8E4940, "44 88 4C 24 20 44 89 44 24 18 66 89 54 24 10 89 4C 24 08 55 53 41 55 41 56 48 8D AC 24 A8 E1 FF"},
        {0x8F0F70, "48 8B C4 48 89 58 20 55 56 41 55 48 8D 68 C1 48 81 EC C0 00 00 00 48 89 78 08 4D 8B E9 4C 89 70"}};
    for (const auto& binding : bindings)
    {
        uintptr_t match = 0; uint32_t count = 0;
        if (!CountPatternMatches(base, size, binding.pattern, match, count) ||
            count != 1 || match != base+binding.rva)
        { LOG("Halo 2 dual aim StockFallback: native binding +0x%X missing/ambiguous", binding.rva); return false; }
    }
    const auto* call = reinterpret_cast<const uint8_t*>(base+0x8E4FC8);
    if (call[0] != 0xE8 || base+0x8E4FCD+*reinterpret_cast<const int32_t*>(call+1) != base+0x8F0F70)
    { LOG("Halo 2 dual aim StockFallback: firing caller mismatch"); return false; }
    g_halo2Dual.base = base;
    g_halo2Dual.faulted.store(false);
    void* aim = reinterpret_cast<void*>(base+0x8F0F70);
    if (MH_CreateHook(aim, reinterpret_cast<void*>(&Halo2DualAimDetour),
            reinterpret_cast<void**>(&g_halo2Dual.aimOriginal)) != MH_OK)
    { LOG("Halo 2 dual aim StockFallback: helper hook creation failed"); return false; }
    g_halo2Dual.aimTarget = aim;
    void* fire = reinterpret_cast<void*>(base+0x8E4940);
    if (MH_CreateHook(fire, reinterpret_cast<void*>(&Halo2DualFireDetour),
            reinterpret_cast<void**>(&g_halo2Dual.fireOriginal)) != MH_OK)
    {
        LOG("Halo 2 dual aim StockFallback: firing hook creation failed");
        (void)RemoveHalo2DualAim();
        return false;
    }
    g_halo2Dual.fireTarget = fire;
    if (MH_EnableHook(aim) != MH_OK || MH_EnableHook(fire) != MH_OK)
    {
        LOG("Halo 2 dual aim StockFallback: hook enable failed");
        (void)RemoveHalo2DualAim();
        return false;
    }
    g_halo2Dual.enabled.store(true, std::memory_order_release);
    LOG("Halo 2 dual aim installed: full owned-weapon firing scope, independent primary/secondary rays while dual wielding; single weapon retains native aim");
    return true;
}

bool RemoveHalo2DualAimLegacy()
{
    g_halo2Dual.enabled.store(false, std::memory_order_release);
    const void* functions[]{reinterpret_cast<const void*>(&Halo2DualFireDetour),
        reinterpret_cast<const void*>(&Halo2DualAimDetour)};
    const void* originals[]{reinterpret_cast<const void*>(g_halo2Dual.fireOriginal),
        reinterpret_cast<const void*>(g_halo2Dual.aimOriginal)};
    void** targets[]{&g_halo2Dual.fireTarget, &g_halo2Dual.aimTarget};
    bool any = false;
    for (auto target : targets)
    {
        if (!*target) continue;
        any = true;
        const auto status = MCCVR_DisableHookForRetirement(*target);
        if (status != MH_OK && status != MH_ERROR_DISABLED)
        { LOG("Halo 2 dual aim cleanup pending: hook disable failed"); return false; }
    }
    if (!any) return true;
    if (!WaitForNativeDetourQuiescence(functions, originals, 2, g_halo2Dual.callbacks))
    { LOG("Halo 2 dual aim cleanup pending: native callbacks busy"); return false; }
    for (auto target : targets)
    {
        if (!*target) continue;
        if (MH_RemoveHook(*target) != MH_OK)
        { LOG("Halo 2 dual aim cleanup pending: hook removal failed"); return false; }
        *target = nullptr;
    }
    g_halo2Dual.fireOriginal = nullptr;
    g_halo2Dual.aimOriginal = nullptr;
    return true;
}

#include "halo2_muzzle_lifecycle.inl"
#include "halo2_dual_lifecycle.inl"
