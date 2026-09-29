// HREK DE4290 / retail 4C2710: four-argument void outer firing scope.
// Reach shares its already-installed ten-argument core unit-adjust hook.
// The T-2 shot evidence uses the recorder's fixed index sentinel only (the
// gate and the shared publisher are wired in game.cpp).
#include "telemetry_recorder.h"
using ReachMuzzleFireFn=void(__fastcall*)(uint32_t,int16_t,void*,uint8_t);
using ReachMuzzleQueryFn=void(__fastcall*)(int32_t,uint8_t,float*,int16_t,float*,void*);
using ReachMuzzleViewFn=void(__fastcall*)(uint32_t,uint8_t,float*,float*,float*,float*);
using ReachMuzzleMarkersFn=int16_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t,uint8_t,uint8_t);
struct ReachMuzzleRuntime
{
    uintptr_t base=0;uint32_t generation=0;uint64_t installedAtMs=0;
    void* targets[4]{};
    ReachMuzzleFireFn fireOriginal=nullptr;
    ReachMuzzleQueryFn queryOriginal=nullptr;ReachMuzzleViewFn viewOriginal=nullptr;
    ReachMuzzleMarkersFn markersOriginal=nullptr;
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0},refused{0},queries{0},restoreRefused{0};
} g_reachBarrel;
struct ReachMuzzleQueryContext
{uint32_t unit=UINT32_MAX,generation=0;int32_t inputUser=-1;uint8_t flags=0;int16_t zoom=-1;uint64_t sampleMs=0;};
struct ReachMuzzleQueryCapture {bool active=false;uint32_t unit=UINT32_MAX,count=0;};
struct ReachMuzzleShotScope
{bool active=false,query=false,viewApplied=false;uint32_t unit=UINT32_MAX;float position[3]{},direction[3]{};};
struct ReachMuzzleRequest
{uint32_t weapon=UINT32_MAX;int16_t barrel=-1;uint8_t simulation=0;NativeShotTargetLease<0x28>* lease=nullptr;};
thread_local ReachMuzzleQueryContext g_reachBarrelQueryContext;
thread_local ReachMuzzleQueryCapture g_reachBarrelCapture;
thread_local ReachMuzzleShotScope g_reachBarrelShot;
thread_local ReachMuzzleRequest g_reachBarrelRequest;
__declspec(noinline) void MarkReachMuzzleFault(){g_reachBarrel.faulted.store(true,std::memory_order_release);}

// T-2 shot evidence (declared in game.cpp): the firing context of the
// projectile transaction on this thread. Both values are the engine's own
// arguments to the four-argument outer firing scope; a missing optional
// muzzle transaction leaves the sentinels, never a fabricated identity.
void ReachReadFiringContextForTelemetry(uint32_t& weapon,uint8_t& barrel)
{
    const auto request=g_reachBarrelRequest;
    weapon=request.weapon;
    barrel=(request.barrel>=0&&request.barrel<2)?
        uint8_t(request.barrel):kTelemetryShotIndexUnknown;
}

__declspec(noinline) void __fastcall ReachMuzzleViewDetour(uint32_t unit,uint8_t flags,
    float* direction,float* origin,float* cameraDirection,float* camera)
{
    auto& feature=g_reachBarrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.viewOriginal)__leave;
        feature.viewOriginal(unit,flags,direction,origin,cameraDirection,camera);
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool query=caller==feature.base+0x10EACD||caller==feature.base+0x10EEF1;
        const bool assist=caller==feature.base+0x10F954;
        if(!query&&!assist)__leave;
        if(g_reachBarrelCapture.active&&caller==feature.base+0x10EACD)
        {g_reachBarrelCapture.unit=unit;++g_reachBarrelCapture.count;}
        auto& shot=g_reachBarrelShot;
        if(!shot.active||shot.unit!=unit||!origin||!camera||!cameraDirection||!direction||
            (shot.query?!query:!assist))__leave;
        // HREK 4E3EE0: preserve the native view class/result and replace its
        // private projected position and camera-ray outputs in this shot only.
        std::memcpy(origin,shot.position,12);std::memcpy(camera,shot.position,12);
        std::memmove(cameraDirection,shot.query?direction:shot.direction,12);
        shot.viewApplied=true;
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
__declspec(noinline) void __fastcall ReachMuzzleQueryDetour(int32_t inputUser,uint8_t flags,
    float* direction,int16_t zoom,float* control,void* targeting)
{
    auto& feature=g_reachBarrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previous=g_reachBarrelCapture;g_reachBarrelCapture={};
    const bool capture=feature.enabled.load()&&!feature.faulted.load()&&g_config.gun_barrel_aim&&
        inputUser>=0&&inputUser<4&&!(flags&1)&&direction&&control&&targeting;
    g_reachBarrelCapture.active=capture;
    __try
    {
        if(!feature.queryOriginal)__leave;
        feature.queryOriginal(inputUser,flags,direction,zoom,control,targeting);
        if(capture&&g_reachBarrelCapture.count==1)
            g_reachBarrelQueryContext={g_reachBarrelCapture.unit,feature.generation,inputUser,flags,zoom,GetTickCount64()};
    }
    __finally {g_reachBarrelCapture=previous;feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
void RestoreReachMuzzleTarget(NativeShotTargetLease<0x28>& lease)
{
    if(!lease.active)return;
    __try
    {
        if(!lease.Restore(g_reachCamera.generation.load(),lease.owner,ReachMuzzleTargetStorage(lease.owner)))
            g_reachBarrel.restoreRefused.fetch_add(1,std::memory_order_relaxed);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {lease.active=false;MarkReachMuzzleFault();g_reachBarrel.restoreRefused.fetch_add(1,std::memory_order_relaxed);}
}
bool AcquireReachMuzzleTarget(const weapon_muzzle::Receipt& muzzle,NativeShotTargetLease<0x28>& lease)
{
    auto& feature=g_reachBarrel;const auto context=g_reachBarrelQueryContext;
    void* storage=ReachMuzzleTargetStorage(context.unit);
    if(!storage||!feature.queryOriginal)return false;
    auto& shot=g_reachBarrelShot;shot={};shot.active=true;shot.query=true;shot.unit=context.unit;
    std::memcpy(shot.position,muzzle.ray.position,12);std::memcpy(shot.direction,muzzle.ray.direction,12);
    alignas(8) unsigned char targeting[0x28]{};float control[3]{};
    const bool previous=g_legacyCollisionOwnedQuery;g_legacyCollisionOwnedQuery=true;
    __try {feature.queryOriginal(context.inputUser,context.flags,shot.direction,context.zoom,control,targeting);}
    __finally {g_legacyCollisionOwnedQuery=previous;shot.query=false;}
    uint32_t weapons[2]{};
    if(!shot.viewApplied||feature.generation!=g_reachCamera.generation.load()||
        ReachMuzzleTargetStorage(context.unit)!=storage||!ReachReadMuzzleWeapons(context.unit,weapons)||
        weapons[muzzle.slot]!=muzzle.weapon)return false;
    // HREK 4E2330 zeroes five qwords; Reach strengths +10/+14 and lead +18..20, flags +24: 0x28 bytes.
    for(size_t at=0x10;at<=0x20;at+=4)
        if(!std::isfinite(*reinterpret_cast<const float*>(targeting+at)))return false;
    // HREK 4C4790/4C47A0: object handle +8, marker +4.
    const uint32_t target=*reinterpret_cast<const uint32_t*>(targeting+8);
    if(target!=UINT32_MAX&&!ReachBarrelValidObject(target))return false;
    if(!lease.Apply(feature.generation,context.unit,storage,targeting))return false;
    feature.queries.fetch_add(1,std::memory_order_relaxed);return true;
}
bool ApplyReachMuzzleMarker(uint32_t object,uint64_t count,void* markers,int16_t capacity)
{
    auto& feature=g_reachBarrel;const auto request=g_reachBarrelRequest;
    if(!feature.enabled.load()||feature.faulted.load()||!g_config.gun_barrel_aim||
        exclusive_input::Active()||!g_vrAim.load()||!g_enabled.load()||!VR_IsStereoEnabled()||
        TitleAdapter_GetActiveTitle()!=GameTitle::HaloReach||feature.generation!=g_reachCamera.generation.load()||
        !request.lease||request.lease->active||request.barrel<0||request.barrel>=2||
        object!=request.weapon||count!=1||capacity<1||!markers||!g_origReachUnitAdjust)return false;
    if(!g_reachCamera.armed.load()||g_reachCinematicLocked.load())return false;
    const auto context=g_reachBarrelQueryContext;const auto now=GetTickCount64();
    if(!context.sampleMs||context.sampleMs<feature.installedAtMs||context.generation!=feature.generation||
        now<context.sampleMs||now-context.sampleMs>100||!ReachMuzzleTargetStorage(context.unit))return false;
    uint32_t weapons[2]{};VrContactTrackingSnapshot tracking{};
    if(!ReachReadMuzzleWeapons(context.unit,weapons)||!VR_GetContactTrackingSnapshot(tracking))return false;
    const int slot=ResolveEquippedWeaponSlot(request.weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if(slot<0||!tracking.hands[slot==0?1:0].valid||!g_reachBarrelMuzzles.Read(GameTitle::HaloReach,feature.generation,
        context.unit,request.weapon,tracking.referenceEpoch,now,tracking.timeNs,uint8_t(slot),uint8_t(request.barrel),
        g_config.left_handed,muzzle))return false;
    float velocity[3]{},direction[3]{};std::memcpy(direction,muzzle.ray.direction,12);
    const bool previous=g_legacyCollisionOwnedQuery;g_legacyCollisionOwnedQuery=true;
    __try {g_origReachUnitAdjust(int32_t(context.unit),muzzle.ray.position,direction,velocity,nullptr,nullptr,0,0,1,request.simulation);}
    __finally {g_legacyCollisionOwnedQuery=previous;}
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(muzzle.ray.position[axis])||!std::isfinite(direction[axis]))return false;
    if(!AcquireReachMuzzleTarget(muzzle,*request.lease))return false;
    const auto& ray=muzzle.ray;auto* bytes=static_cast<uint8_t*>(markers);
    const float side[3]{ray.up[1]*ray.direction[2]-ray.up[2]*ray.direction[1],
        ray.up[2]*ray.direction[0]-ray.up[0]*ray.direction[2],ray.up[0]*ray.direction[1]-ray.up[1]*ray.direction[0]};
    std::memcpy(bytes+0x3C,ray.direction,12);std::memcpy(bytes+0x48,side,12);
    std::memcpy(bytes+0x54,ray.up,12);std::memcpy(bytes+0x60,ray.position,12);return true;
}
__declspec(noinline) int16_t __fastcall ReachMuzzleMarkersDetour(uint32_t object,uint32_t name,
    void* markers,int16_t capacity,uint8_t originalObject,uint8_t interpolated)
{
    auto& feature=g_reachBarrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);int16_t result=0;
    __try
    {
        if(!feature.markersOriginal)__leave;
        result=feature.markersOriginal(object,name,markers,capacity,originalObject,interpolated);
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x4C2AA0||!g_reachBarrelRequest.lease)__leave;
        const auto previous=g_reachBarrelShot;bool applied=false;
        __try {applied=ApplyReachMuzzleMarker(object,result,markers,capacity);}
        __except(EXCEPTION_EXECUTE_HANDLER){MarkReachMuzzleFault();}
        if(applied)feature.applied.fetch_add(1,std::memory_order_relaxed);
        else {RestoreReachMuzzleTarget(*g_reachBarrelRequest.lease);g_reachBarrelShot=previous;feature.refused.fetch_add(1,std::memory_order_relaxed);}
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
// Invoked by the existing ReachUnitAdjustHook before its accepted fallback.
// The fourth native argument is an output velocity vector (HREK D48D40),
// despite the legacy core name basisForward; pass the real output unchanged.
bool ReachApplyBarrelAim(int32_t unit,float* origin,float* direction,const float* velocity,
    uint8_t collision,uint32_t simulation)
{
    auto& shot=g_reachBarrelShot;
    if(!shot.active||shot.query||shot.unit!=uint32_t(unit)||!origin||!direction||!g_origReachUnitAdjust)return false;
    std::memcpy(origin,shot.position,12);std::memcpy(direction,shot.direction,12);
    g_origReachUnitAdjust(unit,origin,direction,velocity,nullptr,nullptr,0,0,collision,simulation);
    if(std::isfinite(origin[0])&&std::isfinite(origin[1])&&std::isfinite(origin[2]))std::memcpy(shot.position,origin,12);
    return true;
}
__declspec(noinline) void __fastcall ReachMuzzleFireDetour(uint32_t weapon,int16_t barrel,void* data,uint8_t simulation)
{
    auto& feature=g_reachBarrel;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previousShot=g_reachBarrelShot;const auto previousRequest=g_reachBarrelRequest;
    NativeShotTargetLease<0x28> lease{};g_reachBarrelShot={};g_reachBarrelRequest={weapon,barrel,simulation,&lease};
    __try {if(feature.fireOriginal)feature.fireOriginal(weapon,barrel,data,simulation);}
    __finally
    {
        RestoreReachMuzzleTarget(lease);g_reachBarrelShot=previousShot;g_reachBarrelRequest=previousRequest;
        feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);
    }
}
