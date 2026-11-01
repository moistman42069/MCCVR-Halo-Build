// ODSTEK B0FCB0 / retail 3AF230: five-argument outer firing scope.
// The six-argument 3AE8A4 is a downstream firing-data helper, not outer fire.
// September 18 scope correction: independent dual aim is H2/H3 only.
// Retain this unshipped experiment dormant; barrel aiming remains supported.
constexpr bool kEnableOdstIndependentAim=false;
using OdstMuzzleFireFn=uint64_t(__fastcall*)(uint32_t,int16_t,void*,int32_t,uint8_t);
using OdstMuzzleAimFn=void(__fastcall*)(uint32_t,float*,float*,uint64_t,float*,uint8_t,uint8_t);
using OdstMuzzleQueryFn=void(__fastcall*)(int32_t,uint8_t,float*,int16_t,float*,void*);
using OdstMuzzleViewFn=void(__fastcall*)(uint32_t,uint64_t,float*,float*,float*);
using OdstMuzzleCameraFn=int32_t(__fastcall*)(uint32_t,float*,float*);
using OdstMuzzleMarkersFn=uint64_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t,uint8_t,uint8_t);
struct OdstIndependentAimSnapshot
{DualWeaponAimSnapshot aim{};bool leftHanded=false;};
struct OdstMuzzleRuntime
{
    uintptr_t base=0;uint32_t generation=0;uint64_t installedAtMs=0;
    void* targets[6]{};
    OdstMuzzleFireFn fireOriginal=nullptr;OdstMuzzleAimFn aimOriginal=nullptr;
    OdstMuzzleQueryFn queryOriginal=nullptr;OdstMuzzleViewFn viewOriginal=nullptr;
    OdstMuzzleCameraFn cameraOriginal=nullptr;OdstMuzzleMarkersFn markersOriginal=nullptr;
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0},refused{0},queries{0},restoreRefused{0};
    halo_ce::Snapshot<OdstIndependentAimSnapshot> controllerAim;
} g_odstMuzzle;
struct OdstMuzzleQueryContext
{uint32_t unit=UINT32_MAX,generation=0;int32_t inputUser=-1;uint8_t flags=0;int16_t zoom=-1;uint64_t sampleMs=0;};
struct OdstMuzzleQueryCapture {bool active=false;uint32_t unit=UINT32_MAX,count=0;};
struct OdstMuzzleShotScope
{bool active=false,query=false,viewApplied=false;uint32_t unit=UINT32_MAX;float position[3]{},direction[3]{};bool barrel=false;};
struct OdstMuzzleRequest
{uint32_t weapon=UINT32_MAX;int16_t barrel=-1;NativeShotTargetLease<0x28>* lease=nullptr;};
thread_local OdstMuzzleQueryContext g_odstMuzzleQueryContext;
thread_local OdstMuzzleQueryCapture g_odstMuzzleCapture;
thread_local OdstMuzzleShotScope g_odstMuzzleShot;
thread_local OdstMuzzleRequest g_odstMuzzleRequest;
__declspec(noinline) void MarkOdstMuzzleFault(){g_odstMuzzle.faulted.store(true,std::memory_order_release);}

__declspec(noinline) void __fastcall OdstMuzzleViewDetour(uint32_t unit,uint64_t flags,
    float* direction,float* origin,float* camera)
{
    auto& feature=g_odstMuzzle;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.viewOriginal)__leave;
        feature.viewOriginal(unit,flags,direction,origin,camera);
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        if(caller!=feature.base+0x160647&&caller!=feature.base+0x160A5D)__leave;
        if(g_odstMuzzleCapture.active&&caller==feature.base+0x160647)
        {g_odstMuzzleCapture.unit=unit;++g_odstMuzzleCapture.count;}
        auto& shot=g_odstMuzzleShot;
        if(!shot.active||!shot.query||shot.unit!=unit||!origin||!camera)__leave;
        // The native helper projects its camera through the world object's
        // centre. Replace its private outputs after that projection; direction
        // remains the query's own input, including native lead calculations.
        std::memcpy(origin,shot.position,12);std::memcpy(camera,shot.position,12);
        shot.viewApplied=true;
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
__declspec(noinline) int32_t __fastcall OdstMuzzleCameraDetour(uint32_t unit,float* position,float* direction)
{
    auto& feature=g_odstMuzzle;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);int32_t result=0;
    __try
    {
        if(!feature.cameraOriginal)__leave;
        result=feature.cameraOriginal(unit,position,direction);
        const auto& shot=g_odstMuzzleShot;
        if(!shot.active||shot.query||shot.unit!=unit||!position||!direction||
            reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x161204)__leave;
        std::memcpy(position,shot.position,12);std::memcpy(direction,shot.direction,12);
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
__declspec(noinline) void __fastcall OdstMuzzleQueryDetour(int32_t inputUser,uint8_t flags,
    float* direction,int16_t zoom,float* control,void* targeting)
{
    auto& feature=g_odstMuzzle;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previous=g_odstMuzzleCapture;g_odstMuzzleCapture={};
    const bool capture=feature.enabled.load()&&!feature.faulted.load()&&
        (Sight_GunBarrelAim()||Sight_MuzzleShotsConverge()||(kEnableOdstIndependentAim&&Sight_IndependentDualAim()))&&
        inputUser>=0&&inputUser<4&&!(flags&1)&&direction&&control&&targeting;
    g_odstMuzzleCapture.active=capture;
    // The game's own zoom level for the local player (Aim Frame Zoom): the ordinary query only, never ours.
    if(inputUser==0&&!(flags&1)&&!g_legacyCollisionOwnedQuery)
        NoteGameZoomLevel(GameTitle::Halo3ODST,zoom,feature.generation);
    __try
    {
        if(!feature.queryOriginal)__leave;
        feature.queryOriginal(inputUser,flags,direction,zoom,control,targeting);
        if(capture&&g_odstMuzzleCapture.count==1)
            g_odstMuzzleQueryContext={g_odstMuzzleCapture.unit,feature.generation,inputUser,flags,zoom,GetTickCount64()};
    }
    __finally {g_odstMuzzleCapture=previous;feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
void RestoreOdstMuzzleTarget(NativeShotTargetLease<0x28>& lease)
{
    if(!lease.active)return;
    __try
    {
        if(!lease.Restore(g_odstRuntimeGeneration.load(),lease.owner,OdstMuzzleTargetStorage(lease.owner)))
            g_odstMuzzle.restoreRefused.fetch_add(1,std::memory_order_relaxed);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {lease.active=false;MarkOdstMuzzleFault();g_odstMuzzle.restoreRefused.fetch_add(1,std::memory_order_relaxed);}
}
bool AcquireOdstMuzzleTarget(const weapon_muzzle::Receipt& muzzle,NativeShotTargetLease<0x28>& lease)
{
    auto& feature=g_odstMuzzle;const auto context=g_odstMuzzleQueryContext;
    void* storage=OdstMuzzleTargetStorage(context.unit);
    uint32_t before[2]{};
    if(!storage||!feature.queryOriginal||muzzle.slot>=2||!OdstReadMuzzleWeapons(context.unit,before)||
        before[muzzle.slot]!=muzzle.weapon)return false;
    auto& shot=g_odstMuzzleShot;shot={};shot.active=true;shot.query=true;shot.unit=context.unit;
    std::memcpy(shot.position,muzzle.ray.position,12);std::memcpy(shot.direction,muzzle.ray.direction,12);
    alignas(8) unsigned char targeting[0x28]{};float control[3]{};
    const bool previous=g_legacyCollisionOwnedQuery;g_legacyCollisionOwnedQuery=true;
    __try {feature.queryOriginal(context.inputUser,context.flags,shot.direction,context.zoom,control,targeting);}
    __finally {g_legacyCollisionOwnedQuery=previous;shot.query=false;}
    uint32_t weapons[2]{};
    if(!shot.viewApplied||feature.generation!=g_odstRuntimeGeneration.load()||
        OdstMuzzleTargetStorage(context.unit)!=storage||!OdstReadMuzzleWeapons(context.unit,weapons)||
        weapons[0]!=before[0]||weapons[1]!=before[1])return false;
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(shot.position[axis])||!std::isfinite(shot.direction[axis]))return false;
    // ODST strengths +10/+14 and lead +18..20, flags +24: 0x28 bytes.
    for(size_t at=0x10;at<=0x20;at+=4)
        if(!std::isfinite(*reinterpret_cast<const float*>(targeting+at)))return false;
    // ODSTEK 41D850/41D860: object handle +8, marker +4.
    const uint32_t target=*reinterpret_cast<const uint32_t*>(targeting+8);
    if(target!=UINT32_MAX&&!OdstContactObject(target))return false;
    if(!lease.Apply(feature.generation,context.unit,storage,targeting))return false;
    feature.queries.fetch_add(1,std::memory_order_relaxed);return true;
}
bool ApplyOdstMuzzleMarker(uint32_t object,uint64_t count,void* markers,int16_t capacity)
{
    auto& feature=g_odstMuzzle;const auto request=g_odstMuzzleRequest;
    if(!feature.enabled.load()||feature.faulted.load()||!(Sight_GunBarrelAim()||Sight_MuzzleShotsConverge())||
        exclusive_input::Active()||!g_vrAim.load()||!g_enabled.load()||!VR_IsStereoEnabled()||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo3ODST||feature.generation!=g_odstRuntimeGeneration.load()||
        !request.lease||request.lease->active||request.barrel<0||request.barrel>=2||
        object!=request.weapon||count!=1||capacity<1||!markers||!feature.aimOriginal)return false;
    int32_t scene=-1,shot=-1;
    if(ReadOdstCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled)return false;
    const auto context=g_odstMuzzleQueryContext;const auto now=GetTickCount64();
    if(!context.sampleMs||context.sampleMs<feature.installedAtMs||context.generation!=feature.generation||
        now<context.sampleMs||now-context.sampleMs>100||!OdstMuzzleTargetStorage(context.unit))return false;
    uint32_t weapons[2]{};VrContactTrackingSnapshot tracking{};
    if(!OdstReadMuzzleWeapons(context.unit,weapons)||!VR_GetContactTrackingSnapshot(tracking))return false;
    const int slot=ResolveEquippedWeaponSlot(request.weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if(slot<0||!tracking.hands[slot==0?1:0].valid||!g_odstMuzzles.Read(GameTitle::Halo3ODST,feature.generation,
        context.unit,request.weapon,tracking.referenceEpoch,now,tracking.timeNs,uint8_t(slot),uint8_t(request.barrel),
        g_config.left_handed,muzzle))return false;
    float velocity[3]{},direction[3]{};std::memcpy(direction,muzzle.ray.direction,12);
    float before[3]{};std::memcpy(before,muzzle.ray.position,12);
    const bool previous=g_legacyCollisionOwnedQuery;g_legacyCollisionOwnedQuery=true;
    __try {feature.aimOriginal(context.unit,muzzle.ray.position,direction,reinterpret_cast<uint64_t>(velocity),nullptr,0,0);}
    __finally {g_legacyCollisionOwnedQuery=previous;}
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(muzzle.ray.position[axis])||!std::isfinite(direction[axis]))return false;
    // SHOTS FROM THE MUZZLE (sight_layer.h): keep the origin the obstruction check left, but fly through the
    // converged point instead of along the barrel. A refusal refuses the barrel shot: it leaves the eye.
    // The second gun under independent dual aim follows its own controller (the shot that lane prepared).
    // A seat held at its stop or a turret (unit_in_vehicle reads false there) refuses it: SeatMuzzleShotHeld.
    if(!Sight_GunBarrelAim()&&
        ((slot==1&&kEnableOdstIndependentAim&&Sight_IndependentDualAim())||
         SeatMuzzleShotHeld(GameTitle::Halo3ODST)||
         !Sight_AimMuzzleShot(GameTitle::Halo3ODST,TitleAdapter_GetGeneration(GameTitle::Halo3ODST),
            before,muzzle.ray.position,muzzle.ray.direction,muzzle.ray.up)))return false;
    if(!AcquireOdstMuzzleTarget(muzzle,*request.lease))return false;
    const auto& ray=muzzle.ray;auto* bytes=static_cast<uint8_t*>(markers);
    const float side[3]{ray.up[1]*ray.direction[2]-ray.up[2]*ray.direction[1],
        ray.up[2]*ray.direction[0]-ray.up[0]*ray.direction[2],ray.up[0]*ray.direction[1]-ray.up[1]*ray.direction[0]};
    std::memcpy(bytes+0x3C,ray.direction,12);std::memcpy(bytes+0x48,side,12);
    std::memcpy(bytes+0x54,ray.up,12);std::memcpy(bytes+0x60,ray.position,12);
    g_odstMuzzleShot.barrel=true;return true;
}

// Same player behavior as H3: each owned gun queries native targeting from its
// controller, while the normal native origin/obstruction path remains intact.
// This uses ODST's own 0x28 target and full-salt inventory; no H3 layouts.
bool PrepareOdstIndependentShot(uint32_t weapon,NativeShotTargetLease<0x28>& lease)
{
    if constexpr(!kEnableOdstIndependentAim)return false;
    auto& feature=g_odstMuzzle;
    if(!feature.enabled.load()||feature.faulted.load()||!Sight_IndependentDualAim()||
        exclusive_input::Active()||!g_vrAim.load()||!g_enabled.load()||!VR_IsStereoEnabled()||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo3ODST||feature.generation!=g_odstRuntimeGeneration.load())return false;
    int32_t scene=-1,shot=-1;
    if(ReadOdstCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled)return false;
    const auto context=g_odstMuzzleQueryContext;const auto now=GetTickCount64();
    if(!context.sampleMs||context.sampleMs<feature.installedAtMs||context.generation!=feature.generation||
        now<context.sampleMs||now-context.sampleMs>100||!OdstMuzzleTargetStorage(context.unit))return false;
    uint32_t weapons[2]{};VrContactTrackingSnapshot tracking{};OdstIndependentAimSnapshot sample{};
    if(!OdstReadMuzzleWeapons(context.unit,weapons)||weapons[1]==UINT32_MAX||
        !VR_GetContactTrackingSnapshot(tracking)||!feature.controllerAim.Read(sample)||
        sample.leftHanded!=g_config.left_handed||
        !DualWeaponAimFresh(sample.aim,feature.generation,context.unit,tracking.referenceEpoch,
            now,feature.installedAtMs,tracking.timeNs)||
        sample.aim.weapons[0]!=weapons[0]||sample.aim.weapons[1]!=weapons[1])return false;
    const int slot=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,true);
    if(slot<0||!tracking.hands[slot==0?1:0].valid)return false;
    weapon_muzzle::Receipt controller{};controller.slot=uint8_t(slot);controller.weapon=weapon;
    std::memcpy(controller.ray.position,sample.aim.positions[slot],12);
    if(!BuildIndependentWeaponDirection(controller.ray.position,controller.ray.position,
        sample.aim.directions[slot],1.f,controller.ray.direction))return false;
    for(float coordinate:controller.ray.position)if(!std::isfinite(coordinate))return false;
    return AcquireOdstMuzzleTarget(controller,lease);
}
__declspec(noinline) uint64_t __fastcall OdstMuzzleMarkersDetour(uint32_t object,uint32_t name,
    void* markers,int16_t capacity,uint8_t originalObject,uint8_t interpolated)
{
    auto& feature=g_odstMuzzle;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);uint64_t result=0;
    __try
    {
        if(!feature.markersOriginal)__leave;
        result=feature.markersOriginal(object,name,markers,capacity,originalObject,interpolated);
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())!=feature.base+0x3AF4CA||!g_odstMuzzleRequest.lease)__leave;
        const auto previous=g_odstMuzzleShot;bool applied=false;
        __try {applied=ApplyOdstMuzzleMarker(object,result,markers,capacity);}
        __except(EXCEPTION_EXECUTE_HANDLER){MarkOdstMuzzleFault();}
        if(applied)feature.applied.fetch_add(1,std::memory_order_relaxed);
        else {RestoreOdstMuzzleTarget(*g_odstMuzzleRequest.lease);g_odstMuzzleShot=previous;feature.refused.fetch_add(1,std::memory_order_relaxed);}
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
__declspec(noinline) void __fastcall OdstMuzzleAimDetour(uint32_t unit,float* origin,float* direction,
    uint64_t velocity,float* offset,uint8_t project,uint8_t useUnitAim)
{
    auto& feature=g_odstMuzzle;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.aimOriginal)__leave;
        auto& shot=g_odstMuzzleShot;
        // The fire's shot line (0x396B7C from 0x3AEB26) names the shooter before its assist (0x3AED8A) traces the
        // crosshair: the local player's fire only from here (shot_lag::FireMark). Seated, the shooter is taken to be
        // the occupant as in Halo 3 (ODST's fire also walks the controlling parent, +0x2B8 at 0x3AEA59); were it the
        // vehicle, a seated query simply stays as the game built it (fail closed).
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x3AEB2B)
            t_odstFire=shot_lag::FireMark(t_odstFire,unit!=UINT32_MAX &&
                static_cast<int32_t>(unit)==LegacyCollisionIgnoredObject(GameTitle::Halo3ODST));
        if(shot.active&&!shot.query&&shot.barrel&&shot.unit==unit&&origin&&direction&&
            reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x3AEB2B)
        {
            std::memcpy(origin,shot.position,12);std::memcpy(direction,shot.direction,12);
            feature.aimOriginal(unit,origin,direction,velocity,nullptr,0,0);
            if(std::isfinite(origin[0])&&std::isfinite(origin[1])&&std::isfinite(origin[2]))std::memcpy(shot.position,origin,12);
            // Dev SHOT LAG, the muzzle path: the final muzzle ray.
            Sight_NoteShotLagFromIntent(GameTitle::Halo3ODST,SightShotPath::Muzzle,origin,direction,
                t_legacyFirePredicted,false,-1);
        }
        else
        {
            feature.aimOriginal(unit,origin,direction,velocity,offset,project,useUnitAim);
            if(shot.active&&!shot.query&&!shot.barrel&&shot.unit==unit&&origin&&direction&&
                reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x3AEB2B)
            {
                float candidate[3]{};
                if(BuildIndependentWeaponDirection(origin,shot.position,shot.direction,
                    std::clamp(g_config.crosshair_distance_m,2.f,50.f)*Game_GetWorldScale(),candidate))
                    std::memcpy(direction,candidate,12);
            }
#if HALOMCCVR_DEV_DIAGNOSTICS
            // Dev SHOT LAG, the native path: the local player's own shot, as the record aimed it from the fire tick's
            // origin (a seat's shot leaves through the crosshair-assist line, measured there).
            if(origin&&direction&&reinterpret_cast<uintptr_t>(_ReturnAddress())==feature.base+0x3AEB2B&&
                static_cast<int32_t>(unit)==LegacyCollisionIgnoredObject(GameTitle::Halo3ODST))
                Sight_NoteShotLagFromIntent(GameTitle::Halo3ODST,SightShotPath::Native,origin,direction,
                    t_legacyFirePredicted,false,-1);
#endif
        }
    }
    __finally {feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
__declspec(noinline) uint64_t __fastcall OdstMuzzleFireDetour(uint32_t weapon,int16_t barrel,void* data,int32_t index,uint8_t predicted)
{
    auto& feature=g_odstMuzzle;feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previousShot=g_odstMuzzleShot;const auto previousRequest=g_odstMuzzleRequest;
    // The shooter is unknown until the shot line names it (OdstMuzzleAimDetour): shot_lag::FireMark.
    const int8_t previousFire=t_odstFire;t_odstFire=shot_lag::kFireOther;
    NativeShotTargetLease<0x28> lease{},controllerLease{};g_odstMuzzleShot={};g_odstMuzzleRequest={weapon,barrel,&lease};uint64_t result=0;
    __try
    {
        bool ready=false;
        __try {ready=PrepareOdstIndependentShot(weapon,controllerLease);}
        __except(EXCEPTION_EXECUTE_HANDLER){MarkOdstMuzzleFault();}
        if(!ready){RestoreOdstMuzzleTarget(controllerLease);g_odstMuzzleShot={};}
        if(feature.fireOriginal)
        {
            // The shot-lag witness labels this fire's crosshair query and aim with the fire's own flag.
            const int previousPredicted=t_legacyFirePredicted;
            t_legacyFirePredicted=predicted?1:0;
            __try {result=feature.fireOriginal(weapon,barrel,data,index,predicted);}
            __finally {t_legacyFirePredicted=previousPredicted;}
        }
    }
    __finally
    {
        RestoreOdstMuzzleTarget(lease);RestoreOdstMuzzleTarget(controllerLease);
        g_odstMuzzleShot=previousShot;g_odstMuzzleRequest=previousRequest;t_odstFire=previousFire;
        feature.callbacks.fetch_sub(1,std::memory_order_acq_rel);
    }
    return result;
}
