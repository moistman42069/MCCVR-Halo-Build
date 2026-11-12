void CaptureMuzzleQuery(int32_t input,int16_t zoom,bool direct)
{
    HaloCELocalPlayerState state{};RenderContext context{};
    if(muzzleInstalled.load()&&!muzzleFaulted.load()&&HaloCEControls_GetLocalPlayerState(state)&&
        input==state.inputUser&&LocalOnFootShooter(state.unit)&&HaloCE_GetGameplayContext(context)&&
        (context.tracking.controllers.gunBarrelAim||context.tracking.controllers.muzzleShots))
        muzzleQueryContext={generation.load(),state.unit,input,zoom,direct,GetTickCount64()};
}
__declspec(noinline) void __fastcall MuzzleQueryHook(int32_t input,int16_t zoom,Vec3* control,void* target)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!muzzleQueryHook.original)__leave;
        reinterpret_cast<MuzzleQueryFn>(muzzleQueryHook.original)(input,zoom,control,target);
        CaptureMuzzleQuery(input,zoom,false);
    }
    __finally{callbacks.fetch_sub(1,std::memory_order_release);}
}
__declspec(noinline) void __fastcall MuzzleDirectQueryHook(int32_t input,int16_t zoom,void* target)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!muzzleDirectQueryHook.original)__leave;
        reinterpret_cast<MuzzleDirectQueryFn>(muzzleDirectQueryHook.original)(input,zoom,target);
        CaptureMuzzleQuery(input,zoom,true);
    }
    __finally{callbacks.fetch_sub(1,std::memory_order_release);}
}
bool ApplyMuzzleMarker(uint32_t object,int16_t count,void* markers,int16_t capacity)
{
    const auto request=muzzleRequest;const auto query=muzzleQueryContext;const uint64_t now=GetTickCount64();
    if(!request.lease||request.lease->active||request.replicated||object!=request.weapon||
        count!=1||capacity<1||!markers||query.generation!=generation.load()||!query.at||
        query.at<muzzleInstalledAt||now<query.at||now-query.at>100||
        !modernRayHook.original||!targetInstalled.load()||targetRetiring.load())return false;
    weapon_muzzle::Receipt receipt{};
    if(!ReadMuzzle(query.unit,request.weapon,request.barrel,receipt,true))return false;
    void* storage=MuzzleTargetStorage(query.unit);if(!storage)return false;
    auto& shot=muzzleShot;shot={};shot.active=true;shot.unit=query.unit;
    std::memcpy(&shot.position,receipt.ray.position,12);std::memcpy(&shot.direction,receipt.ray.direction,12);
    Vec3 velocity{};
    // HCEEK 8CD510 / retail B00880: native obstruction and velocity output,
    // with native camera projection and weapon offset disabled for this ray.
    reinterpret_cast<ModernRayFn>(modernRayHook.original)(query.unit,&shot.position,&shot.direction,&velocity,nullptr,false,false);
    if(!Finite(shot.position)||!Finite(shot.direction))return false;
    // SHOTS FROM THE MUZZLE (sight_layer.h): keep the origin the obstruction check left, but fly through the
    // converged point instead of along the barrel. A refusal refuses the barrel shot: it leaves the eye.
    // Only the barrel aim fires along the barrel: when this second read fails, or the keys changed since
    // ReadMuzzle admitted the shot, the shot is refused (it leaves the eye), as in the other titles.
    {
        RenderContext context{};
        if(!HaloCE_GetGameplayContext(context))return false;
        if(!context.tracking.controllers.gunBarrelAim)
        {
            if(!context.tracking.controllers.muzzleShots)return false;
            Vec3 head{},dir{},eye{};
            // The carrier the aim itself uses (ControllerShotDirection): with the persistent support grip
            // detached, the independent one-hand pose. Unreadable while the grip is wired: the eye shot.
            HaloCELocalPlayerState player{};
            const bool wired=VR_SupportGripWiredForTitle(GameTitle::HaloCE);
            const bool havePlayer=HaloCEControls_GetLocalPlayerState(player);
            const CeSupportInvocation support=havePlayer?CeEvaluateSupportInvocation(wired,player,
                context.tracking.controllers.primaryAimSupportDerived):CeSupportInvocation{};
            const bool detach=support.detach;
            if((wired&&!havePlayer)||(detach&&!context.tracking.controllers.independentPrimaryAim.valid))
            {Sight_NoteMuzzleShotUnconverged(GameTitle::HaloCE);return false;}
            const ControllerPose& carrier=detach?context.tracking.controllers.independentPrimaryAim:
                context.tracking.controllers.primaryAim;
            if(context.tracking.controllers.controlsPresentationBlocked||
                !BuildSightGeometry(context.camera,context.tracking,context.reference,
                    carrier,context.unitsPerMeter,context.positional,head,dir,eye))
            {Sight_NoteMuzzleShotUnconverged(GameTitle::HaloCE);return false;}
            const float h[3]{head.x,head.y,head.z},d[3]{dir.x,dir.y,dir.z},e[3]{eye.x,eye.y,eye.z};
            float through[3]{shot.direction.x,shot.direction.y,shot.direction.z};
            // FLAT PARITY: the point is a world point fixed at this context's camera; the round leaves after the fire
            // tick's move, so it flies through P moved with the eye to the fire (CeFireEyeForShot).
            const SightCameraSample camera=CeContextCameraSample(context);
            if(!Sight_AimMuzzleShotFromRay(GameTitle::HaloCE,TitleAdapter_GetGeneration(GameTitle::HaloCE),h,d,e,
                receipt.ray.position,&shot.position.x,through,receipt.ray.up,nullptr,&camera))return false;
            shot.direction={through[0],through[1],through[2]};
            // Dev SHOT LAG: the muzzle shot against the point it was aimed through.
            Sight_NoteShotLagFromRay(GameTitle::HaloCE,SightShotPath::Muzzle,&shot.position.x,through,h,d,e,false);
        }
    }
    alignas(8) unsigned char target[0x10]{};Vec3 control{};shot.query=true;
    __try
    {
        if(query.direct)
        {
            if(!muzzleDirectQueryHook.original)__leave;
            reinterpret_cast<MuzzleDirectQueryFn>(muzzleDirectQueryHook.original)(query.input,query.zoom,target);
        }
        else
        {
            if(!muzzleQueryHook.original)__leave;
            reinterpret_cast<MuzzleQueryFn>(muzzleQueryHook.original)(query.input,query.zoom,&control,target);
        }
    }
    __finally{shot.query=false;}
    if(!shot.queryApplied||!MuzzleOwner(query.unit,request.weapon)||query.generation!=generation.load()||
        MuzzleTargetStorage(query.unit)!=storage||!std::isfinite(*reinterpret_cast<float*>(target)))return false;
    // Native query constructs its complete16-byte result, including no-target.
    if(!request.lease->Apply(query.generation,query.unit,storage,target))return false;
    auto* bytes=static_cast<uint8_t*>(markers);Vec3 up{};std::memcpy(&up,receipt.ray.up,12);
    const Vec3 side=Cross(up,shot.direction);
    // Own CE0x6C record: node/pad0, local4, world38; no later-title tail flags.
    std::memcpy(bytes+0x3C,&shot.direction,12);std::memcpy(bytes+0x48,&side,12);
    std::memcpy(bytes+0x54,&up,12);std::memcpy(bytes+0x60,&shot.position,12);return true;
}
int16_t MuzzleMarkersBody(uint32_t object,const char* name,void* markers,int16_t capacity,uintptr_t caller)
{
    if(!muzzleMarkersHook.original)return 0;
    const int16_t result=reinterpret_cast<MuzzleMarkersFn>(muzzleMarkersHook.original)(object,name,markers,capacity);
    if(caller!=moduleBase+0xB7A574||!muzzleRequest.lease)return result;
    const auto previous=muzzleShot;bool applied=false;
    __try{applied=ApplyMuzzleMarker(object,result,markers,capacity);}
    __except(EXCEPTION_EXECUTE_HANDLER){muzzleFaulted=true;}
    if(applied)++muzzleApplied;
    else{RestoreMuzzleTarget(*muzzleRequest.lease);muzzleShot=previous;++muzzleRefused;}
    return result;
}
__declspec(noinline) int16_t __fastcall MuzzleMarkersHook(uint32_t object,const char* name,void* markers,int16_t capacity)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);int16_t result=0;
    __try{result=MuzzleMarkersBody(object,name,markers,capacity,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally{callbacks.fetch_sub(1,std::memory_order_release);}
    return result;
}
__declspec(noinline) void __fastcall MuzzleFireHook(uint32_t weapon,int16_t barrel,uint32_t simulation,
    int16_t mode,const void* replicated,uint8_t predicted)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    const auto previousShot=muzzleShot;const auto previousRequest=muzzleRequest;
    NativeShotTargetLease<0x10> lease{};muzzleShot={};muzzleRequest={weapon,barrel,replicated!=nullptr,&lease};
    __try
    {if(muzzleFireHook.original)reinterpret_cast<MuzzleFireFn>(muzzleFireHook.original)(weapon,barrel,simulation,mode,replicated,predicted);}
    __finally
    {RestoreMuzzleTarget(lease);muzzleShot=previousShot;muzzleRequest=previousRequest;callbacks.fetch_sub(1,std::memory_order_release);}
}
