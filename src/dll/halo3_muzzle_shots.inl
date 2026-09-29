// H3EK A3ABE0 and retail 343D74 independently establish this six-argument
// resolver and its 0x70 marker/world-matrix layout. It is not H2's wrapper ABI.
using Halo3MuzzleMarkersFn=uint64_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t,uint8_t,uint8_t);
struct Halo3MuzzleRuntime
{
    uintptr_t base=0;uint32_t generation=0;
    void* target=nullptr;Halo3MuzzleMarkersFn original=nullptr;
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0},refused{0};
} g_halo3Muzzle;
struct Halo3MuzzleRequest
{
    uint32_t weapon=UINT32_MAX;int16_t barrel=-1;
    // The fire call's own prediction flag (T-2 shot evidence). The muzzle
    // substitution path ignores it; it travels with the per-shot request so
    // the firing-path shot event reports what the native call carried.
    uint8_t predicted=0;
    NativeShotTargetLease<0x24>* lease=nullptr;
};
thread_local Halo3MuzzleRequest g_halo3MuzzleRequest;
__declspec(noinline) void MarkHalo3MuzzleFault()
{g_halo3Muzzle.faulted.store(true,std::memory_order_release);}

bool ApplyHalo3MuzzleMarker(uint32_t object,uint64_t count,void* markers,int16_t capacity)
{
    auto& feature=g_halo3Muzzle;
    const auto request=g_halo3MuzzleRequest;
    if(!feature.enabled.load() || feature.faulted.load() || !g_config.gun_barrel_aim ||
        !g_halo3Dual.enabled.load() || g_halo3Dual.faulted.load() || exclusive_input::Active() ||
        !g_vrAim.load() || !g_enabled.load() || !VR_IsStereoEnabled() ||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo3 ||
        feature.generation!=g_halo3RuntimeGeneration.load() || !request.lease || request.lease->active ||
        request.barrel<0 || request.barrel>=2 || object!=request.weapon ||
        count!=1 || capacity<1 || !markers)return false;
    int32_t scene=-1,shot=-1;
    if(ReadCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled)return false;
    const auto context=g_halo3NativeQueryContext;
    uint32_t weapons[2]{};
    VrContactTrackingSnapshot tracking{};
    if(!Halo3ReadOwnedWeapons(context.unit,weapons,false) ||
        !VR_GetContactTrackingSnapshot(tracking))return false;
    const int slot=ResolveEquippedWeaponSlot(request.weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if(slot<0 || !g_halo3Muzzles.Read(GameTitle::Halo3,feature.generation,context.unit,request.weapon,
        tracking.referenceEpoch,GetTickCount64(),tracking.timeNs,uint8_t(slot),uint8_t(request.barrel),
        g_config.left_handed,muzzle) || !g_halo3Dual.aimOriginal ||
        !Halo3IndependentTargetStorage(context.unit))return false;
    // H3's helper always performs its own obstruction test. Private velocity
    // output keeps this preflight separate from the ordinary firing call.
    float velocity[3]{},direction[3]{};
    std::memcpy(direction,muzzle.ray.direction,12);
    const bool previousCollisionQuery=g_legacyCollisionOwnedQuery;
    g_legacyCollisionOwnedQuery=true;
    __try
    {
        g_halo3Dual.aimOriginal(context.unit,muzzle.ray.position,direction,
            reinterpret_cast<uint64_t>(velocity),nullptr,0,0);
    }
    __finally {g_legacyCollisionOwnedQuery=previousCollisionQuery;}
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(muzzle.ray.position[axis])||!std::isfinite(direction[axis]))return false;
    if(!AcquireHalo3IndependentShot(request.weapon,*request.lease,&muzzle))return false;
    auto* bytes=static_cast<uint8_t*>(markers);
    const auto& ray=muzzle.ray;
    float side[3]{ray.up[1]*ray.direction[2]-ray.up[2]*ray.direction[1],
        ray.up[2]*ray.direction[0]-ray.up[0]*ray.direction[2],
        ray.up[0]*ray.direction[1]-ray.up[1]*ray.direction[0]};
    std::memcpy(bytes+0x3C,ray.direction,12);
    std::memcpy(bytes+0x48,side,12);
    std::memcpy(bytes+0x54,ray.up,12);
    std::memcpy(bytes+0x60,ray.position,12);
    return true;
}

__declspec(noinline) uint64_t __fastcall Halo3MuzzleMarkersDetour(
    uint32_t object,uint32_t name,void* markers,int16_t capacity,uint8_t originalObject,uint8_t interpolated)
{
    auto& feature=g_halo3Muzzle;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint64_t result=0;
    __try
    {
        if(!feature.original)__leave;
        result=feature.original(object,name,markers,capacity,originalObject,interpolated);
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x368651 ||
            !g_halo3MuzzleRequest.lease)__leave;
        const auto previous=g_halo3IndependentShot;
        bool applied=false;
        __try {applied=ApplyHalo3MuzzleMarker(object,result,markers,capacity);}
        __except(EXCEPTION_EXECUTE_HANDLER) {MarkHalo3MuzzleFault();}
        if(applied)feature.applied.fetch_add(1,std::memory_order_relaxed);
        else
        {
            RestoreHalo3IndependentShot(*g_halo3MuzzleRequest.lease);
            g_halo3IndependentShot=previous;
            feature.refused.fetch_add(1,std::memory_order_relaxed);
        }
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
