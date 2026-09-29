// H3EK 411390 / retail 13B08C: native acquisition. This implementation keeps
// the older early-helper-only experiment above dormant. Native query parameters
// originate on the same engine thread; no input/output-user mapping is assumed.
// The T-2 shot event uses the recorder's fixed payload/kind declarations only
// (the gate and the shared publisher are wired in game.cpp).
#include "telemetry_recorder.h"
struct Halo3NativeQueryContext
{
    uint32_t unit=UINT32_MAX,generation=0;
    int32_t inputUser=-1;
    uint8_t flags=0;
    int16_t zoom=-1;
    uint64_t sampleMs=0;
};
struct Halo3QueryCapture
{
    bool active=false;
    uint32_t unit=UINT32_MAX,count=0;
};
struct Halo3IndependentShotScope
{
    bool active=false,query=false,queryCameraApplied=false;
    uint32_t unit=UINT32_MAX;
    int slot=-1;
    float position[3]{},direction[3]{};
    bool barrel=false;
};
thread_local Halo3NativeQueryContext g_halo3NativeQueryContext;
thread_local Halo3QueryCapture g_halo3QueryCapture;
thread_local Halo3IndependentShotScope g_halo3IndependentShot;

__declspec(noinline) void MarkHalo3IndependentShotFault()
{g_halo3Dual.faulted.store(true,std::memory_order_release);}

__declspec(noinline) int32_t __fastcall Halo3IndependentCameraDetour(
    uint32_t unit,float* position,float* direction)
{
    auto& feature=g_halo3Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    int32_t result=0;
    __try
    {
        if(!feature.cameraOriginal)__leave;
        result=feature.cameraOriginal(unit,position,direction);
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        if(caller==feature.base+0x13B1DC && g_halo3QueryCapture.active)
        {
            g_halo3QueryCapture.unit=unit;
            ++g_halo3QueryCapture.count;
        }
        const auto& shot=g_halo3IndependentShot;
        if(!shot.active || unit!=shot.unit || !position || !direction ||
            (caller!=feature.base+0x13B1DC && caller!=feature.base+0x13BC0D))__leave;
        // Perspective and all native side effects have already been retained.
        std::memcpy(position,shot.position,sizeof(shot.position));
        std::memcpy(direction,shot.direction,sizeof(shot.direction));
        if(shot.query && caller==feature.base+0x13B1DC)
            g_halo3IndependentShot.queryCameraApplied=true;
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}

__declspec(noinline) void __fastcall Halo3IndependentQueryDetour(
    int32_t inputUser,uint8_t flags,float* direction,int16_t zoom,float* control,void* targeting)
{
    auto& feature=g_halo3Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previousCapture=g_halo3QueryCapture;
    g_halo3QueryCapture={};
    // Record only the ordinary query, not the subsequent lead-only fallback.
    // A TLS record is usable only on this same native thread at firing time.
    const bool capture=feature.enabled.load(std::memory_order_acquire) &&
        !feature.faulted.load(std::memory_order_acquire) &&
        (g_config.independent_dual_aim || g_config.gun_barrel_aim) &&
        inputUser>=0 && inputUser<4 && !(flags&1) && direction && control && targeting;
    g_halo3QueryCapture.active=capture;
    __try
    {
        if(!feature.queryOriginal)__leave;
        feature.queryOriginal(inputUser,flags,direction,zoom,control,targeting);
        if(capture && g_halo3QueryCapture.count==1)
        {
            g_halo3NativeQueryContext={g_halo3QueryCapture.unit,feature.generation,
                inputUser,flags,zoom,GetTickCount64()};
        }
    }
    __finally
    {
        g_halo3QueryCapture=previousCapture;
        feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);
    }
}

// Full local identity is checked again before restoring: a saved address is
// never used after a native checkpoint/respawn replaces its unit allocation.
void* Halo3IndependentTargetStorage(uint32_t unit)
{
    if(!g_halo3PlayerUnitGetter || !g_halo3UnitInVehicle ||
        unit!=static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)) ||
        g_halo3UnitInVehicle(static_cast<int32_t>(unit)))return nullptr;
    auto* data=const_cast<uint8_t*>(Halo3MeleeSelectionObject(unit,0));
    // Native firing follows controlling parents before reading +218. This
    // implementation owns only the ordinary local biped, never a parent unit.
    if(!data || *reinterpret_cast<const uint32_t*>(data+0x2A4)!=UINT32_MAX)return nullptr;
    return data+0x218;
}

bool AcquireHalo3IndependentShot(uint32_t weapon,NativeShotTargetLease<0x24>& lease,
    const weapon_muzzle::Receipt* muzzle=nullptr)
{
    auto& feature=g_halo3Dual;
    if(!feature.enabled.load(std::memory_order_acquire) || feature.faulted.load() ||
        (!muzzle&&!g_config.independent_dual_aim) || exclusive_input::Active() ||
        !g_vrAim.load() || !g_enabled.load() || !VR_IsStereoEnabled() ||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo3 ||
        feature.generation!=g_halo3RuntimeGeneration.load() || !feature.queryOriginal)
        return false;
    int32_t scene=-1,shot=-1;
    if(ReadCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled)return false;
    const auto context=g_halo3NativeQueryContext;
    const auto now=GetTickCount64();
    if(!context.sampleMs || context.sampleMs<feature.installedAtMs ||
        context.generation!=feature.generation || now<context.sampleMs || now-context.sampleMs>100)
        return false;
    void* storage=Halo3IndependentTargetStorage(context.unit);
    uint32_t weapons[2]{};
    DualWeaponAimSnapshot sample{};
    VrContactTrackingSnapshot tracking{};
    if(!storage || !Halo3ReadOwnedWeapons(context.unit,weapons,!muzzle))return false;
    const int slot=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    if(slot<0 || !VR_GetContactTrackingSnapshot(tracking) || !tracking.hands[slot==0?1:0].valid)return false;
    if(muzzle)
    {
        if(!weapon_muzzle::Fresh(*muzzle,GameTitle::Halo3,feature.generation,context.unit,weapon,
            tracking.referenceEpoch,now,tracking.timeNs,uint8_t(slot),muzzle->barrel,g_config.left_handed))return false;
        std::memcpy(sample.positions[slot],muzzle->ray.position,12);
        std::memcpy(sample.directions[slot],muzzle->ray.direction,12);
    }
    else if(!feature.aim.Read(sample) ||
        !DualWeaponAimFresh(sample,feature.generation,context.unit,tracking.referenceEpoch,
            now,feature.installedAtMs,tracking.timeNs) ||
        sample.weapons[0]!=weapons[0] || sample.weapons[1]!=weapons[1])return false;
    float normalized[3]{};
    if(!BuildIndependentWeaponDirection(sample.positions[slot],sample.positions[slot],
        sample.directions[slot],1.0f,normalized))return false;
    g_halo3IndependentShot={};
    auto& scope=g_halo3IndependentShot;
    scope.active=true;scope.query=true;scope.unit=context.unit;scope.slot=slot;
    scope.barrel=muzzle!=nullptr;
    std::memcpy(scope.position,sample.positions[slot],sizeof(scope.position));
    std::memcpy(scope.direction,normalized,sizeof(scope.direction));
    alignas(8) unsigned char targeting[0x24]{};
    float ignoredControl[3]{};
    // Run the proven native query on the native thread that supplied its input
    // parameters. No render/worker query, unit inventory swap, or fake target.
    const bool previousCollisionQuery=g_legacyCollisionOwnedQuery;
    g_legacyCollisionOwnedQuery=true;
    __try
    {
        feature.queryOriginal(context.inputUser,context.flags,scope.direction,context.zoom,
            ignoredControl,targeting);
    }
    __finally
    {
        g_legacyCollisionOwnedQuery=previousCollisionQuery;
        scope.query=false;
    }
    if(!scope.queryCameraApplied || feature.generation!=g_halo3RuntimeGeneration.load() ||
        Halo3IndependentTargetStorage(context.unit)!=storage)return false;
    uint32_t currentWeapons[2]{};
    if(!Halo3ReadOwnedWeapons(context.unit,currentWeapons,!muzzle) ||
        currentWeapons[0]!=weapons[0] || currentWeapons[1]!=weapons[1])return false;
    // 0xC/0x10 are native primary/secondary strengths; 0x14..1C lead offset.
    for(size_t at=0xC;at<=0x1C;at+=sizeof(float))
        if(!std::isfinite(*reinterpret_cast<const float*>(targeting+at)))return false;
    const uint32_t target=*reinterpret_cast<const uint32_t*>(targeting+4);
    if(target!=UINT32_MAX && !Halo3ContactObject(target))return false;
    if(!lease.Apply(feature.generation,context.unit,storage,targeting))return false;
    feature.nativeQueries.fetch_add(1,std::memory_order_relaxed);
    return true;
}
bool PrepareHalo3IndependentShot(uint32_t weapon,NativeShotTargetLease<0x24>& lease)
{return AcquireHalo3IndependentShot(weapon,lease);}

void RestoreHalo3IndependentShot(NativeShotTargetLease<0x24>& lease)
{
    if(!lease.active)return;
    __try
    {
        if(!lease.Restore(g_halo3RuntimeGeneration.load(),lease.owner,
            Halo3IndependentTargetStorage(lease.owner)))
            g_halo3Dual.targetRestoresRefused.fetch_add(1,std::memory_order_relaxed);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        lease.active=false;
        MarkHalo3IndependentShotFault();
        g_halo3Dual.targetRestoresRefused.fetch_add(1,std::memory_order_relaxed);
    }
}

#include "halo3_muzzle_shots.inl"

__declspec(noinline) uint64_t __fastcall Halo3IndependentFireDetour(
    uint32_t weapon,int16_t barrel,void* data,int32_t index,uint8_t predicted)
{
    auto& feature=g_halo3Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previous=g_halo3IndependentShot;
    g_halo3IndependentShot={};
    NativeShotTargetLease<0x24> lease{};
    NativeShotTargetLease<0x24> muzzleLease{};
    const auto previousMuzzle=g_halo3MuzzleRequest;
    g_halo3MuzzleRequest={weapon,barrel,predicted,&muzzleLease};
    uint64_t result=0;
    __try
    {
        bool ready=false;
        __try {ready=PrepareHalo3IndependentShot(weapon,lease);}
        __except(EXCEPTION_EXECUTE_HANDLER)
        {MarkHalo3IndependentShotFault();}
        if(!ready)
        {
            RestoreHalo3IndependentShot(lease);
            g_halo3IndependentShot={};
            feature.refused.fetch_add(1,std::memory_order_relaxed);
        }
        // Never catch and replay a native firing exception. Ammo/effects and
        // projectile creation execute exactly once, even if that call faults.
        if(feature.fireOriginal)
        {
            feature.fireEntries.fetch_add(1,std::memory_order_relaxed);
            if(data)feature.fireWithData.fetch_add(1,std::memory_order_relaxed);
            if(predicted)feature.firePredicted.fetch_add(1,std::memory_order_relaxed);
            __try
            {
                result=feature.fireOriginal(weapon,barrel,data,index,predicted);
                feature.fireReturns.fetch_add(1,std::memory_order_relaxed);
            }
            __finally
            {
                if(AbnormalTermination())feature.fireUnwinds.fetch_add(1,std::memory_order_relaxed);
            }
        }
    }
    __finally
    {
        RestoreHalo3IndependentShot(muzzleLease);
        RestoreHalo3IndependentShot(lease);
        g_halo3IndependentShot=previous;
        g_halo3MuzzleRequest=previousMuzzle;
        feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);
    }
    return result;
}

// T-2 shot event for Halo 3. The disabled gate is passed FIRST: with recording
// off this returns on a single atomic load, before any TLS/context read. The
// final origin/direction come from the firing invocation's own aim call
// (post-substitution, post-native), and `substituted` says whether this mod's
// independent path produced that ray. Weapon/barrel/predicted ride the
// existing per-shot muzzle request; slot rides the acquisition scope and is
// kTelemetryShotIndexUnknown when no scope owned this shot.
void PublishHalo3ShotDiagnostic(uint32_t unit,const float origin[3],
    const float direction[3],bool substituted,bool firesFromCamera,bool unitAim)
{
    if(!Telemetry_WeaponEventsAccepting())return;
    const auto request=g_halo3MuzzleRequest;
    const auto& scope=g_halo3IndependentShot;
    const uint8_t slot=(scope.active && scope.unit==unit && scope.slot>=0 &&
        scope.slot<=1)?uint8_t(scope.slot):kTelemetryShotIndexUnknown;
    const uint8_t barrel=(request.barrel>=0 && request.barrel<2)?
        uint8_t(request.barrel):kTelemetryShotIndexUnknown;
    PublishShotDiagnostic(GameTitle::Halo3,
        g_halo3RuntimeGeneration.load(std::memory_order_acquire),unit,
        request.weapon,slot,barrel,request.predicted!=0,substituted,
        firesFromCamera,unitAim,origin,direction);
}

__declspec(noinline) void __fastcall Halo3IndependentAimDetour(uint32_t unit,
    float* origin,float* direction,uint64_t marker,float* offset,uint8_t project,uint8_t unitAim)
{
    auto& feature=g_halo3Dual;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.aimOriginal)__leave;
        auto& scope=g_halo3IndependentShot;
        // The one native firing call site is the shot boundary for this event:
        // preparation/query/marker passes call the same helper but return to
        // other addresses, and they are not shots.
        const bool firingCall=
            reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x368B97;
        if(scope.active && !scope.query && scope.barrel && scope.unit==unit && origin && direction &&
            firingCall)
        {
            std::memcpy(origin,scope.position,12);
            std::memcpy(direction,scope.direction,12);
            feature.aimOriginal(unit,origin,direction,marker,nullptr,0,0);
            if(std::isfinite(origin[0])&&std::isfinite(origin[1])&&std::isfinite(origin[2]))
                std::memcpy(scope.position,origin,12);
            feature.rays[scope.slot].fetch_add(1,std::memory_order_relaxed);
            // The muzzle ray is what the engine consumed, and the native call
            // has already produced the final origin/direction.
            PublishHalo3ShotDiagnostic(unit,origin,direction,true,false,false);
            __leave;
        }
        feature.aimOriginal(unit,origin,direction,marker,offset,project,unitAim);
        bool substituted=false;
        if(scope.active && !scope.query && scope.unit==unit && origin && direction &&
            scope.slot>=0 && scope.slot<=1 && firingCall)
        {
            float candidate[3]{};
            if(BuildIndependentWeaponDirection(origin,scope.position,scope.direction,
                std::clamp(g_config.crosshair_distance_m,2.0f,50.0f)*Game_GetWorldScale(),candidate))
            {
                std::memcpy(direction,candidate,sizeof(candidate));
                feature.rays[scope.slot].fetch_add(1,std::memory_order_relaxed);
                substituted=true;
            }
        }
        // Publish once per actual shot invocation, after the final ray is
        // decided, whether or not the mod substituted it.
        if(firingCall)
            PublishHalo3ShotDiagnostic(unit,origin,direction,substituted,
                project!=0,unitAim!=0);
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
