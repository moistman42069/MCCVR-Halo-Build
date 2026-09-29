// H4EK E977E0 / retail 6176B8: four-argument void firing scope.
// H4EK E66CD0 / retail 5F3510: nine arguments, including native output velocity.
// The T-2 shot event uses the recorder's fixed payload/kind declarations only
// (the gate and the shared publisher are wired in game.cpp).
#include "telemetry_recorder.h"
using Halo4MuzzleFireFn=void(__fastcall*)(uint32_t,int16_t,void*,uint8_t);
using Halo4MuzzleQueryFn=void(__fastcall*)(int32_t,uint8_t,float*,int16_t,float*,void*);
using Halo4MuzzleViewFn=void(__fastcall*)(uint32_t,uint8_t,uintptr_t,float*,float*,float*);
using Halo4MuzzleMarkersFn=int16_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t,uint8_t,uint8_t,uint8_t);
using Halo4MuzzleAimFn=void(__fastcall*)(uint32_t,float*,float*,float*,const float*,const float*,uint8_t,uint8_t,uint32_t);
struct Halo4MuzzleRuntime
{
    uintptr_t base=0;uint32_t generation=0;uint64_t installedAtMs=0;
    void* targets[5]{};
    Halo4MuzzleFireFn fireOriginal=nullptr;
    Halo4MuzzleQueryFn queryOriginal=nullptr;Halo4MuzzleViewFn viewOriginal=nullptr;
    Halo4MuzzleMarkersFn markersOriginal=nullptr;Halo4MuzzleAimFn aimOriginal=nullptr;
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0},refused{0},queries{0},restoreRefused{0};
} g_halo4Barrel;
struct Halo4MuzzleQueryContext
{uint32_t unit=UINT32_MAX,generation=0;int32_t inputUser=-1;uint8_t flags=0;int16_t zoom=-1;uint64_t sampleMs=0;};
struct Halo4MuzzleQueryCapture {bool active=false;uint32_t unit=UINT32_MAX,count=0;};
struct Halo4MuzzleShotScope
{bool active=false,query=false,viewApplied=false;uint32_t unit=UINT32_MAX;float position[3]{},direction[3]{};
 // Weapon slot the committed muzzle receipt resolved for this shot (0/1), or
 // kTelemetryShotIndexUnknown when no receipt owned it. T-2 evidence only.
 uint8_t slot=kTelemetryShotIndexUnknown;};
struct Halo4MuzzleRequest
{uint32_t weapon=UINT32_MAX;int16_t barrel=-1;uint8_t simulation=0;NativeShotTargetLease<0x28>* lease=nullptr;};
thread_local Halo4MuzzleQueryContext g_halo4BarrelQueryContext;
thread_local Halo4MuzzleQueryCapture g_halo4BarrelCapture;
thread_local Halo4MuzzleShotScope g_halo4BarrelShot;
thread_local Halo4MuzzleRequest g_halo4BarrelRequest;
__declspec(noinline) void MarkHalo4MuzzleFault(){g_halo4Barrel.faulted.store(true,std::memory_order_release);}

__declspec(noinline) void __fastcall Halo4MuzzleViewDetour(uint32_t unit,uint8_t flags,
    uintptr_t inputDirection,float* origin,float* cameraDirection,float* camera)
{
    auto& feature=g_halo4Barrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.viewOriginal)__leave;
        feature.viewOriginal(unit,flags,inputDirection,origin,cameraDirection,camera);
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool query=caller==feature.base+0x1DB9A6||caller==feature.base+0x1DBDB6;
        const bool assist=caller==feature.base+0x1DC8C4;
        if(!query&&!assist)__leave;
        if(g_halo4BarrelCapture.active&&caller==feature.base+0x1DB9A6)
        {g_halo4BarrelCapture.unit=unit;++g_halo4BarrelCapture.count;}
        auto& shot=g_halo4BarrelShot;
        if(!shot.active||shot.unit!=unit||!origin||!camera||!cameraDirection||
            (shot.query?!query:!assist))__leave;
        // H4EK 586DC0 / retail 1DC9E4: native third argument is unused. Replace
        // private projected position and camera-ray outputs in this shot only.
        std::memcpy(origin,shot.position,12);std::memcpy(camera,shot.position,12);
        std::memcpy(cameraDirection,shot.direction,12);
        shot.viewApplied=true;
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
__declspec(noinline) void __fastcall Halo4MuzzleQueryDetour(int32_t inputUser,uint8_t flags,
    float* direction,int16_t zoom,float* control,void* targeting)
{
    auto& feature=g_halo4Barrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previous=g_halo4BarrelCapture;g_halo4BarrelCapture={};
    const bool capture=feature.enabled.load()&&!feature.faulted.load()&&g_config.gun_barrel_aim&&
        inputUser>=0&&inputUser<4&&!(flags&1)&&direction&&control&&targeting;
    g_halo4BarrelCapture.active=capture;
    __try
    {
        if(!feature.queryOriginal)__leave;
        feature.queryOriginal(inputUser,flags,direction,zoom,control,targeting);
        if(capture&&g_halo4BarrelCapture.count==1)
            g_halo4BarrelQueryContext={g_halo4BarrelCapture.unit,feature.generation,inputUser,flags,zoom,GetTickCount64()};
    }
    __finally {g_halo4BarrelCapture=previous;feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
void RestoreHalo4MuzzleTarget(NativeShotTargetLease<0x28>& lease)
{
    if(!lease.active)return;
    __try
    {
        if(!lease.Restore(g_halo4Camera.generation.load(),lease.owner,Halo4MuzzleTargetStorage(lease.owner)))
            g_halo4Barrel.restoreRefused.fetch_add(1,std::memory_order_relaxed);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {lease.active=false;MarkHalo4MuzzleFault();g_halo4Barrel.restoreRefused.fetch_add(1,std::memory_order_relaxed);}
}
bool AcquireHalo4MuzzleTarget(const weapon_muzzle::Receipt& muzzle,NativeShotTargetLease<0x28>& lease)
{
    auto& feature=g_halo4Barrel;const auto context=g_halo4BarrelQueryContext;
    void* storage=Halo4MuzzleTargetStorage(context.unit);
    if(!storage||!feature.queryOriginal)return false;
    auto& shot=g_halo4BarrelShot;shot={};shot.active=true;shot.query=true;shot.unit=context.unit;
    shot.slot=muzzle.slot;
    std::memcpy(shot.position,muzzle.ray.position,12);std::memcpy(shot.direction,muzzle.ray.direction,12);
    alignas(8) unsigned char targeting[0x28]{};float control[3]{};
    const bool previous=g_halo4MuzzleOwnedQuery;g_halo4MuzzleOwnedQuery=true;
    __try {feature.queryOriginal(context.inputUser,context.flags,shot.direction,context.zoom,control,targeting);}
    __finally {g_halo4MuzzleOwnedQuery=previous;shot.query=false;}
    uint32_t weapons[2]{};
    if(!shot.viewApplied||feature.generation!=g_halo4Camera.generation.load()||
        Halo4MuzzleTargetStorage(context.unit)!=storage||!Halo4ReadMuzzleWeapons(context.unit,weapons)||
        weapons[muzzle.slot]!=muzzle.weapon)return false;
    // H4EK 585160 / retail 1DB840 initialize their own 0x28-byte target; flags +24.
    for(size_t at=0x10;at<=0x20;at+=4)
        if(!std::isfinite(*reinterpret_cast<const float*>(targeting+at)))return false;
    // H4EK 56F420/56F010 and retail 1CD454: object +8, model/marker +4.
    const uint32_t target=*reinterpret_cast<const uint32_t*>(targeting+8);
    if(target!=UINT32_MAX&&!Halo4BarrelValidObject(target))return false;
    if(!lease.Apply(feature.generation,context.unit,storage,targeting))return false;
    feature.queries.fetch_add(1,std::memory_order_relaxed);return true;
}
bool ApplyHalo4MuzzleMarker(uint32_t object,uint64_t count,void* markers,int16_t capacity)
{
    auto& feature=g_halo4Barrel;const auto request=g_halo4BarrelRequest;
    if(!feature.enabled.load()||feature.faulted.load()||!g_config.gun_barrel_aim||
        exclusive_input::Active()||!g_vrAim.load()||!g_enabled.load()||!VR_IsStereoEnabled()||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo4||feature.generation!=g_halo4Camera.generation.load()||
        !request.lease||request.lease->active||request.barrel<0||request.barrel>=2||
        object!=request.weapon||count!=1||capacity<1||!markers||!feature.aimOriginal)return false;
    if(!g_halo4Camera.armed.load()||ReadHalo4CinematicControl()!=CinematicControlState::PlayerControlled)return false;
    const auto context=g_halo4BarrelQueryContext;const auto now=GetTickCount64();
    if(!context.sampleMs||context.sampleMs<feature.installedAtMs||context.generation!=feature.generation||
        now<context.sampleMs||now-context.sampleMs>100||!Halo4MuzzleTargetStorage(context.unit))return false;
    uint32_t weapons[2]{};VrContactTrackingSnapshot tracking{};
    if(!Halo4ReadMuzzleWeapons(context.unit,weapons)||!VR_GetContactTrackingSnapshot(tracking))return false;
    const int slot=ResolveEquippedWeaponSlot(request.weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if(slot<0||!tracking.hands[slot==0?1:0].valid||!g_halo4BarrelMuzzles.Read(GameTitle::Halo4,feature.generation,
        context.unit,request.weapon,tracking.referenceEpoch,now,tracking.timeNs,uint8_t(slot),uint8_t(request.barrel),
        g_config.left_handed,muzzle))return false;
    float velocity[3]{},direction[3]{};std::memcpy(direction,muzzle.ray.direction,12);
    const bool previous=g_halo4MuzzleOwnedQuery;g_halo4MuzzleOwnedQuery=true;
    __try {feature.aimOriginal(context.unit,muzzle.ray.position,direction,velocity,nullptr,nullptr,0,0,request.simulation);}
    __finally {g_halo4MuzzleOwnedQuery=previous;}
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(muzzle.ray.position[axis])||!std::isfinite(direction[axis]))return false;
    if(!AcquireHalo4MuzzleTarget(muzzle,*request.lease))return false;
    const auto& ray=muzzle.ray;auto* bytes=static_cast<uint8_t*>(markers);
    const float side[3]{ray.up[1]*ray.direction[2]-ray.up[2]*ray.direction[1],
        ray.up[2]*ray.direction[0]-ray.up[0]*ray.direction[2],ray.up[0]*ray.direction[1]-ray.up[1]*ray.direction[0]};
    std::memcpy(bytes+0x3C,ray.direction,12);std::memcpy(bytes+0x48,side,12);
    std::memcpy(bytes+0x54,ray.up,12);std::memcpy(bytes+0x60,ray.position,12);return true;
}
__declspec(noinline) int16_t __fastcall Halo4MuzzleMarkersDetour(uint32_t object,uint32_t name,
    void* markers,int16_t capacity,uint8_t originalObject,uint8_t firstPerson,uint8_t interpolated)
{
    auto& feature=g_halo4Barrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);int16_t result=0;
    __try
    {
        if(!feature.markersOriginal)__leave;
        result=feature.markersOriginal(object,name,markers,capacity,originalObject,firstPerson,interpolated);
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x6179B7||!g_halo4BarrelRequest.lease)__leave;
        const auto previous=g_halo4BarrelShot;bool applied=false;
        __try {applied=ApplyHalo4MuzzleMarker(object,result,markers,capacity);}
        __except(EXCEPTION_EXECUTE_HANDLER){MarkHalo4MuzzleFault();}
        if(applied)feature.applied.fetch_add(1,std::memory_order_relaxed);
        else {RestoreHalo4MuzzleTarget(*g_halo4BarrelRequest.lease);g_halo4BarrelShot=previous;feature.refused.fetch_add(1,std::memory_order_relaxed);}
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
// T-2 shot event for Halo 4. The disabled gate is passed FIRST: with recording
// off this returns on a single atomic load, before any TLS/context read. The
// hooks exist regardless of `gun_barrel_aim`, so this fires on real shots with
// or without substitution; `substituted` says whether the committed muzzle
// barrel produced the final ray.
void PublishHalo4ShotDiagnostic(uint32_t unit,const float origin[3],
    const float direction[3],bool substituted,bool firesFromCamera,bool unitAim)
{
    if(!Telemetry_WeaponEventsAccepting())return;
    const auto request=g_halo4BarrelRequest;
    const auto& shot=g_halo4BarrelShot;
    const uint8_t slot=(shot.active && shot.unit==unit && shot.slot<=1)?
        uint8_t(shot.slot):kTelemetryShotIndexUnknown;
    const uint8_t barrel=(request.barrel>=0 && request.barrel<2)?
        uint8_t(request.barrel):kTelemetryShotIndexUnknown;
    // The four-argument Halo 4 fire scope carries no prediction flag, so bit0
    // stays "unknown" (0) rather than inventing one from the simulation byte.
    PublishShotDiagnostic(GameTitle::Halo4,
        g_halo4Camera.generation.load(std::memory_order_acquire),unit,
        request.weapon,slot,barrel,false,substituted,firesFromCamera,unitAim,
        origin,direction);
}
// Exact outer-fire call only. Native velocity, collision and simulation remain
// native; no replay when the native callback raises an exception.
__declspec(noinline) void __fastcall Halo4MuzzleAimDetour(uint32_t unit,float* origin,float* direction,float* velocity,
    const float* offset,const float* camera,uint8_t project,uint8_t unitAim,uint32_t simulation)
{
    auto& feature=g_halo4Barrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.aimOriginal)__leave;
        auto& shot=g_halo4BarrelShot;
        const bool firingCall=
            reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x617E95;
        const bool owned=firingCall&&
            shot.active&&!shot.query&&shot.unit==unit&&origin&&direction;
        if(owned)
        {
            std::memcpy(origin,shot.position,12);std::memcpy(direction,shot.direction,12);
            offset=camera=nullptr;project=unitAim=0;
        }
        feature.aimOriginal(unit,origin,direction,velocity,offset,camera,project,unitAim,simulation);
        if(owned&&std::isfinite(origin[0])&&std::isfinite(origin[1])&&std::isfinite(origin[2]))
            std::memcpy(shot.position,origin,12);
        // Publish once per actual shot invocation, after the final ray is
        // decided, whether or not the mod substituted it.
        if(firingCall&&origin&&direction)
            PublishHalo4ShotDiagnostic(unit,origin,direction,owned,
                project!=0,unitAim!=0);
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
__declspec(noinline) void __fastcall Halo4MuzzleFireDetour(uint32_t weapon,int16_t barrel,void* data,uint8_t simulation)
{
    auto& feature=g_halo4Barrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previousShot=g_halo4BarrelShot;const auto previousRequest=g_halo4BarrelRequest;
    NativeShotTargetLease<0x28> lease{};g_halo4BarrelShot={};g_halo4BarrelRequest={weapon,barrel,simulation,&lease};
    __try {if(feature.fireOriginal)feature.fireOriginal(weapon,barrel,data,simulation);}
    __finally
    {
        RestoreHalo4MuzzleTarget(lease);g_halo4BarrelShot=previousShot;g_halo4BarrelRequest=previousRequest;
        feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);
    }
}
