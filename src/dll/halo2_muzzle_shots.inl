// H2EK 867EB0 / retail 8D11A0: s_model_marker has a 0x70 stride,
// with world matrix at +38, forward +3C, up +54 and translation +60.
// Only the weapon firing caller of the four-argument wrapper is admitted.
using Halo2MuzzleMarkersFn=uint16_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t);
struct Halo2MuzzleRuntime
{
    uintptr_t base=0;uint32_t generation=0;
    void* target=nullptr;Halo2MuzzleMarkersFn original=nullptr;
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0},refused{0};
} g_halo2Muzzle;
struct Halo2MuzzleRequest
{
    uint32_t weapon=UINT32_MAX;int16_t barrel=-1;
    NativeShotTargetLease<0x24>* lease=nullptr;
};
thread_local Halo2MuzzleRequest g_halo2MuzzleRequest;
__declspec(noinline) void MarkHalo2MuzzleFault()
{g_halo2Muzzle.faulted.store(true,std::memory_order_release);}

bool ApplyHalo2MuzzleMarker(uint32_t object,uint16_t count,void* markers,int16_t capacity)
{
    auto& feature=g_halo2Muzzle;
    const auto request=g_halo2MuzzleRequest;
    if(!feature.enabled.load() || feature.faulted.load() || !g_config.gun_barrel_aim ||
        !g_halo2Dual.enabled.load() || g_halo2Dual.faulted.load() || exclusive_input::Active() ||
        !Game_Halo2ControllerAimActive() || !Halo2Observer6Dof_FinalPaletteArmed() ||
        !Halo2Observer6Dof_DirectWeaponAimArmed() ||
        feature.generation!=g_generation.load() || !request.lease || request.lease->active ||
        request.barrel<0 || request.barrel>=2 ||
        count!=1 || capacity<1 || !markers)return false;
    const auto context=g_halo2IndependentQueryContext;
    // H2EK 4A0F50 / retail 8E8990 selects the firing weapon OR its
    // parent as the native marker owner. The firing scope still identifies
    // the exact weapon/role; a parent marker must not force the secondary
    // weapon back to its ordinary native origin. No other object is admitted.
    if(object!=request.weapon && object!=context.unit)return false;
    uint32_t weapons[2]{};
    VrContactTrackingSnapshot tracking{};
    if(!Halo2ReadIndependentWeapons(context.unit,weapons,false) ||
        !VR_GetContactTrackingSnapshot(tracking))return false;
    const int slot=ResolveEquippedWeaponSlot(request.weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if(slot<0 || !g_halo2Muzzles.Read(GameTitle::Halo2,feature.generation,context.unit,request.weapon,
        tracking.referenceEpoch,GetTickCount64(),tracking.timeNs,uint8_t(slot),uint8_t(request.barrel),
        g_config.left_handed,muzzle))return false;
    if(!g_halo2Dual.aimOriginal || !Halo2IndependentTargetStorage(context.unit))return false;
    // Some authored barrel flags skip the later unit-adjust call or restore
    // the marker afterwards. Clamp the marker itself through the proven native
    // helper first, using private velocity storage; its body only reads unit
    // state and writes caller-owned origin/direction/velocity. The ordinary
    // call later still supplies the engine's actual projectile velocity.
    float velocity[3]{},direction[3]{};
    std::memcpy(direction,muzzle.ray.direction,12);
    const bool previousCollisionQuery=g_halo2CollisionOwnQuery;
    g_halo2CollisionOwnQuery=true;
    __try
    {
        g_halo2Dual.aimOriginal(context.unit,muzzle.ray.position,direction,
            reinterpret_cast<uint64_t>(velocity),nullptr,0,0,1);
    }
    __finally {g_halo2CollisionOwnQuery=previousCollisionQuery;}
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(muzzle.ray.position[axis])||!std::isfinite(direction[axis]))return false;
    if(!AcquireHalo2IndependentShot(request.weapon,*request.lease,&muzzle))return false;
    // Preserve the native local matrix, node/region identity, scale and flags.
    // Its world orientation/position now matches the verified visible marker.
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

__declspec(noinline) uint16_t __fastcall Halo2MuzzleMarkersDetour(
    uint32_t object,uint32_t name,void* markers,int16_t capacity)
{
    auto& feature=g_halo2Muzzle;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint16_t result=0;
    __try
    {
        if(!feature.original)__leave;
        result=feature.original(object,name,markers,capacity);
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x8E4BAF ||
            !g_halo2MuzzleRequest.lease)__leave;
        const auto previous=g_halo2IndependentShot;
        bool applied=false;
        __try {applied=ApplyHalo2MuzzleMarker(object,result,markers,capacity);}
        __except(EXCEPTION_EXECUTE_HANDLER) {MarkHalo2MuzzleFault();}
        if(applied)feature.applied.fetch_add(1,std::memory_order_relaxed);
        else
        {
            RestoreHalo2IndependentTarget(*g_halo2MuzzleRequest.lease);
            g_halo2IndependentShot=previous;
            feature.refused.fetch_add(1,std::memory_order_relaxed);
        }
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
