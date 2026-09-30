// H2EK 4FE0D0/4FE420: acquisition and subsequent firing assist. The early
// helper alone is insufficient. Keep the previous detours inert for evidence.
// The T-2 shot event uses the recorder's fixed payload declarations and the
// one shared publisher (wired in game.cpp; reached through the game.h door
// because this file is compiled into the observer translation unit).
#include "telemetry_recorder.h"
struct Halo2IndependentShotScope
{
    bool active=false,query=false,locationApplied=false;
    uint32_t unit=UINT32_MAX;
    int slot=-1;
    Halo2CameraBasis carrier{};
    float distance=0;
    bool barrel=false;
};
thread_local Halo2IndependentShotScope g_halo2IndependentShot;

void RecordHalo2IndependentQuery()
{
    if(g_config.independent_dual_aim || g_config.gun_barrel_aim)
        g_halo2IndependentQueryContext={Halo2OwnedUnit(),g_generation.load(),GetTickCount64()};
}

__declspec(noinline) void MarkHalo2IndependentShotFault()
{g_halo2Dual.faulted.store(true,std::memory_order_release);}

// This is a pointer to the complete native observer location, including BSP
// location data, not just a point. Preserve it exactly. Only the acquisition
// direction changes, converging from that native point toward the hand ray.
__declspec(noinline) const void* __fastcall Halo2IndependentLocationDetour(uint32_t user)
{
    auto& feature=g_halo2Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const void* result=nullptr;
    __try
    {
        if(!feature.locationOriginal)__leave;
        result=feature.locationOriginal(user);
        auto& shot=g_halo2IndependentShot;
        if(!result || !shot.active || !shot.query || user!=kOwnedUser ||
            reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x75932A)__leave;
        shot.locationApplied=Halo2BuildControllerShotDirection(static_cast<const float*>(result),
            shot.carrier,shot.distance,g_aimAssistControllerRayScope.direction);
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}

__declspec(noinline) int32_t __fastcall Halo2IndependentCameraDetour(
    uint32_t unit,float* position,float* direction)
{
    auto& feature=g_halo2Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    int32_t result=0;
    __try
    {
        if(!feature.cameraOriginal)__leave;
        result=feature.cameraOriginal(unit,position,direction);
        const auto& shot=g_halo2IndependentShot;
        if(!shot.active || shot.query || unit!=shot.unit || !position || !direction ||
            reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x7597BD)__leave;
        float candidate[3]{};
        if(shot.barrel)
        {
            std::memcpy(position,shot.carrier.position,12);
            std::memcpy(direction,shot.carrier.forward,12);
            __leave;
        }
        if(Halo2BuildControllerShotDirection(position,shot.carrier,shot.distance,candidate))
            std::memcpy(direction,candidate,sizeof(candidate));
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}

void* Halo2IndependentTargetStorage(uint32_t unit)
{
    if(unit==UINT32_MAX || unit!=Halo2OwnedUnit())return nullptr;
    auto* data=static_cast<uint8_t*>(Halo2ObjectFromIndex(unit));
    // H2EK and retail firing follow +260 before reading +1D4. Only the owned,
    // unseated biped without a controlling parent is admitted.
    if(!data || data[0xAA]!=0 || *reinterpret_cast<const uint32_t*>(data+0x260)!=UINT32_MAX ||
        (*reinterpret_cast<const uint32_t*>(data+0x14)!=UINT32_MAX &&
         *reinterpret_cast<const int16_t*>(data+0x210)!=-1))return nullptr;
    return data+0x1D4;
}

bool Halo2ReadIndependentWeapons(uint32_t unit,uint32_t (&weapons)[2],bool requireDual)
{
    const auto* users=*reinterpret_cast<const uint8_t* const*>(
        g_halo2Dual.base+kHalo2FirstPersonUserDataPointerRva);
    if(!users)return false;
    const auto* primary=users+kOwnedUser*kHalo2FirstPersonUserStride+kHalo2FirstPersonWeaponDataOffset;
    const auto* secondary=primary+kHalo2FirstPersonWeaponSlotStride;
    weapons[0]=*reinterpret_cast<const uint32_t*>(primary+kHalo2FirstPersonWeaponObjectOffset);
    weapons[1]=*reinterpret_cast<const uint32_t*>(secondary+kHalo2FirstPersonWeaponObjectOffset);
    if(!(secondary[0]&1))weapons[1]=UINT32_MAX;
    return weapons[0]!=weapons[1] && Halo2DualWeaponOwned(weapons[0],unit) &&
        (weapons[1]==UINT32_MAX ? !requireDual : Halo2DualWeaponOwned(weapons[1],unit));
}
bool Halo2ReadIndependentPair(uint32_t unit,uint32_t (&weapons)[2])
{return Halo2ReadIndependentWeapons(unit,weapons,true);}

bool AcquireHalo2IndependentShot(uint32_t weapon,NativeShotTargetLease<0x24>& lease,
    const weapon_muzzle::Receipt* muzzle=nullptr)
{
    auto& feature=g_halo2Dual;
    const auto query=reinterpret_cast<Halo2AimAssistCalculateFn>(g_aimAssistOriginal.load());
    if(!feature.enabled.load() || feature.faulted.load() || (!muzzle&&!g_config.independent_dual_aim) ||
        !Game_Halo2ControllerAimActive() || !Halo2Observer6Dof_FinalPaletteArmed() ||
        !Halo2Observer6Dof_DirectWeaponAimArmed() || !query ||
        !g_aimAssistViewDirectionOriginal.load() || feature.generation!=g_generation.load())return false;
    const auto context=g_halo2IndependentQueryContext;
    const auto now=GetTickCount64();
    if(!context.sampleMs || context.sampleMs<feature.installedAtMs || now<context.sampleMs ||
        now-context.sampleMs>100 || context.generation!=feature.generation)return false;
    void* storage=Halo2IndependentTargetStorage(context.unit);
    uint32_t weapons[2]{};
    if(!storage || !Halo2ReadIndependentWeapons(context.unit,weapons,!muzzle))return false;
    const int slot=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    Halo2ObserverPosePublication publication{};
    VrContactTrackingSnapshot tracking{};
    Halo2CameraBasis primary{},secondary{};
    if(slot<0 || !Halo2Observer6Dof_ReadPublishedPose(publication) ||
        !VR_GetContactTrackingSnapshot(tracking) || exclusive_input::Active() ||
        !tracking.hands[slot==0?1:0].valid ||
        !Halo2DualAimPublicationFresh(publication,feature.generation,tracking.referenceEpoch,tracking.timeNs))return false;
    Halo2CameraBasis carrier{};
    if(muzzle)
    {
        if(!weapon_muzzle::Fresh(*muzzle,GameTitle::Halo2,feature.generation,context.unit,weapon,
            tracking.referenceEpoch,now,tracking.timeNs,uint8_t(slot),muzzle->barrel,g_config.left_handed))return false;
        std::memcpy(carrier.position,muzzle->ray.position,12);
        std::memcpy(carrier.forward,muzzle->ray.direction,12);
        std::memcpy(carrier.up,muzzle->ray.up,12);
    }
    else
    {
        if(!BuildStableFirstPersonCarriers(publication,feature.generation,primary,secondary,true))return false;
        carrier=slot==0?primary:secondary;
    }
    g_halo2IndependentShot={true,true,false,context.unit,slot,carrier,
        std::clamp(g_config.crosshair_distance_m,2.0f,50.0f)*Game_GetWorldScale(),muzzle!=nullptr};
    const auto previousRay=g_aimAssistControllerRayScope;
    g_aimAssistControllerRayScope={};
    g_aimAssistControllerRayScope.active=true;
    float control[3]{};
    Halo2AimAssistTargetingResult targeting{};
    bool applied=false;
    const bool previousCollisionQuery=g_halo2CollisionOwnQuery;
    g_halo2CollisionOwnQuery=true;
    __try
    {
        query(kOwnedUser,control,&targeting);
        applied=g_aimAssistControllerRayScope.applied && g_halo2IndependentShot.locationApplied;
    }
    __finally
    {
        g_aimAssistControllerRayScope=previousRay;
        g_halo2IndependentShot.query=false;
        g_halo2CollisionOwnQuery=previousCollisionQuery;
    }
    uint32_t current[2]{};
    if(!applied || feature.generation!=g_generation.load() ||
        storage!=Halo2IndependentTargetStorage(context.unit) ||
        !Halo2ReadIndependentWeapons(context.unit,current,!muzzle) || std::memcmp(weapons,current,sizeof(weapons)) ||
        !std::isfinite(targeting.magnetismHorizontal) || !std::isfinite(targeting.magnetismVertical) ||
        (targeting.identifiers[0]!=UINT32_MAX && !Halo2ObjectFromIndex(targeting.identifiers[0])))return false;
    if(!lease.Apply(feature.generation,context.unit,storage,&targeting))return false;
    feature.nativeQueries.fetch_add(1,std::memory_order_relaxed);
    return true;
}
bool PrepareHalo2IndependentShot(uint32_t weapon,NativeShotTargetLease<0x24>& lease)
{return AcquireHalo2IndependentShot(weapon,lease);}

void RestoreHalo2IndependentTarget(NativeShotTargetLease<0x24>& lease)
{
    if(!lease.active)return;
    __try
    {
        const auto generation=g_generation.load();
        void* storage=generation==lease.generation?Halo2IndependentTargetStorage(lease.owner):nullptr;
        if(!lease.Restore(generation,Halo2OwnedUnit(),storage))
            g_halo2Dual.targetRestoresRefused.fetch_add(1,std::memory_order_relaxed);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        lease.active=false;
        MarkHalo2IndependentShotFault();
        g_halo2Dual.targetRestoresRefused.fetch_add(1,std::memory_order_relaxed);
    }
}

#include "halo2_muzzle_shots.inl"

__declspec(noinline) void __fastcall Halo2IndependentFireDetour(
    uint32_t weapon,int16_t barrel,int32_t projectile,uint8_t predicted)
{
    auto& feature=g_halo2Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previous=g_halo2IndependentShot;
    g_halo2IndependentShot={};
    NativeShotTargetLease<0x24> lease{};
    NativeShotTargetLease<0x24> muzzleLease{};
    const auto previousMuzzle=g_halo2MuzzleRequest;
    g_halo2MuzzleRequest={weapon,barrel,&muzzleLease};
    __try
    {
        bool ready=false;
        __try {ready=PrepareHalo2IndependentShot(weapon,lease);}
        __except(EXCEPTION_EXECUTE_HANDLER) {MarkHalo2IndependentShotFault();}
        if(!ready)
        {
            RestoreHalo2IndependentTarget(lease);
            g_halo2IndependentShot={};
            feature.refused.fetch_add(1,std::memory_order_relaxed);
        }
        // Native exceptions propagate after cleanup. Never replay a firing call.
        if(feature.fireOriginal)feature.fireOriginal(weapon,barrel,projectile,predicted);
    }
    __finally
    {
        RestoreHalo2IndependentTarget(muzzleLease);
        RestoreHalo2IndependentTarget(lease);
        g_halo2IndependentShot=previous;
        g_halo2MuzzleRequest=previousMuzzle;
        feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);
    }
}

// T-2 shot event for Halo 2. The disabled gate is passed FIRST: with recording
// off this returns on a single atomic load, before the firing-context read
// that follows. The final origin/direction are read after the native aim call
// (and after the mod's own substitution where it applied), so the event
// carries the ray the engine's projectile builder consumes. Weapon and barrel
// ride the per-shot firing context this call is nested in (the engine's own
// arguments to the firing transaction); the slot is the acquisition scope's
// resolved slot when that scope owns this unit, else unknown. The aim call
// carries no prediction flag, so bit0 stays "unknown" (0) here.
void PublishHalo2ShotDiagnostic(uint32_t unit,const float origin[3],
    const float direction[3],int slot,bool substituted,bool firesFromCamera,
    bool unitAim)
{
    if(!Telemetry_WeaponEventsAccepting())return;
    const auto request=g_halo2MuzzleRequest;
    const uint8_t wireSlot=(slot>=0&&slot<=1)?uint8_t(slot):
        kTelemetryShotIndexUnknown;
    const uint8_t wireBarrel=(request.barrel>=0&&request.barrel<2)?
        uint8_t(request.barrel):kTelemetryShotIndexUnknown;
    Game_PublishShotDiagnostic(GameTitle::Halo2,
        g_generation.load(std::memory_order_acquire),unit,request.weapon,
        wireSlot,wireBarrel,false,substituted,firesFromCamera,unitAim,origin,
        direction);
}

__declspec(noinline) void __fastcall Halo2IndependentAimDetour(uint32_t unit,
    float* origin,float* direction,uint64_t marker,float* offset,
    uint8_t projectOrigin,uint8_t useUnitAim,uint8_t collisionAdjust)
{
    auto& feature=g_halo2Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.aimOriginal)__leave;
        auto& shot=g_halo2IndependentShot;
        const bool firingCall=
            reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x8E4FCD;
        if(shot.active && !shot.query && shot.barrel && unit==shot.unit && origin && direction &&
            firingCall)
        {
            std::memcpy(origin,shot.carrier.position,12);
            std::memcpy(direction,shot.carrier.forward,12);
            // Keep H2's native obstruction clamp and velocity output. Camera
            // projection/unit aim/authored offset would move the real muzzle.
            feature.aimOriginal(unit,origin,direction,marker,nullptr,0,0,collisionAdjust);
            if(std::isfinite(origin[0])&&std::isfinite(origin[1])&&std::isfinite(origin[2]))
                std::memcpy(shot.carrier.position,origin,12);
            (shot.slot==0?feature.primaryRays:feature.secondaryRays).fetch_add(1,std::memory_order_relaxed);
            // The committed muzzle ray was this mod's input to the native
            // call, and the native call has already produced the final ray.
            // The call's own projectOrigin/useUnitAim arguments were replaced
            // by the literal 0/0 above, so those flags are reported as false.
            PublishHalo2ShotDiagnostic(unit,origin,direction,shot.slot,true,
                false,false);
            __leave;
        }
        feature.aimOriginal(unit,origin,direction,marker,offset,projectOrigin,useUnitAim,collisionAdjust);
        if(!firingCall || !origin || !direction)__leave;
        const bool owned=shot.active && !shot.query && unit==shot.unit;
        bool substituted=false;
        if(owned)
        {
            float candidate[3]{};
            if(Halo2BuildControllerShotDirection(origin,shot.carrier,shot.distance,candidate))
            {
                std::memcpy(direction,candidate,sizeof(candidate));
                (shot.slot==0?feature.primaryRays:feature.secondaryRays).fetch_add(1,std::memory_order_relaxed);
                substituted=true;
            }
        }
        // One event per firing invocation this detour serves, after the final
        // ray is decided, whether or not the mod substituted it. Invocations
        // that never reach the firing call-site (query/prep passes) publish
        // nothing.
        PublishHalo2ShotDiagnostic(unit,origin,direction,owned?shot.slot:-1,
            substituted,projectOrigin!=0,useUnitAim!=0);
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
