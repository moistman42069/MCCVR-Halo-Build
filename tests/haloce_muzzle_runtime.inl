static unsigned ceMuzzleChecks{},ceMuzzleFires{},ceMuzzleQueries{},ceMuzzleMarkers{},ceMuzzleAims{};
static alignas(8) uint8_t ceMuzzleUnit[0x400]{},ceMuzzleReplacement[0x400]{};
static uint8_t* ceMuzzleStorage=ceMuzzleUnit;
static bool ceMuzzleExpected=true,ceMuzzleNested{},ceMuzzleQueryFault{},ceMuzzleFireFault{},ceMuzzleAimFault{},
    ceMuzzleOmitQuery{},ceMuzzleReplace{},ceMuzzleNativeChange{},ceMuzzleRevoke{};
static int16_t ceMuzzleCount=1;
static constexpr uint32_t ceOwner=0x12340002,ceWeapon=0x56780003,ceTarget=0x43210004;
static void CeMuzzleCheck(bool ok,const char* why)
{++ceMuzzleChecks;if(!ok){std::fprintf(stderr,"CE muzzle failed: %s\n",why);std::exit(1);}}
static uintptr_t __fastcall CeMuzzleObject(uint32_t handle,uint32_t kinds)
{return handle==ceOwner&&kinds==3?reinterpret_cast<uintptr_t>(ceMuzzleStorage):0;}
static uint8_t __fastcall CeMuzzleSearch(const void*,const Vec3* point,const Vec3* ray,uint32_t unit,uint16_t,void* out)
{
    CeMuzzleCheck(unit==ceOwner&&point&&ray&&point->x==2&&point->y==2.5f&&ray->y==1,"native search receives clipped actual barrel");
    std::memset(out,0,0x50);std::memcpy(out,&ceTarget,4);return 1;
}
static void __fastcall CeMuzzleQuery(int32_t input,int16_t zoom,Vec3* control,void* target)
{
    ++ceMuzzleQueries;CeMuzzleCheck(input==2&&zoom==3&&control&&target,"native full query ABI");
    if(ceMuzzleQueryFault)RaiseException(0xE000CE81,0,0,nullptr);
    alignas(8) uint8_t search[0x50]{};Vec3 nativePoint{20,30,40},nativeRay{1,0,0};
    if(!ceMuzzleOmitQuery)TargetQueryBody(nullptr,&nativePoint,&nativeRay,ceOwner,7,search,moduleBase+0xB680FD);
    std::memset(target,0,0x10);const float strength=.5f;std::memcpy(target,&strength,4);
    std::memcpy(static_cast<uint8_t*>(target)+12,&ceTarget,4);control->x=.1f;
    if(ceMuzzleReplace)ceMuzzleStorage=ceMuzzleReplacement;
    if(ceMuzzleRevoke)targetPlayer.weapon^=0x10000;
}
static void __fastcall CeMuzzleDirectQuery(int32_t input,int16_t zoom,void* target)
{Vec3 control{};CeMuzzleQuery(input,zoom,&control,target);}
static void __fastcall CeMuzzleAim(uint32_t unit,Vec3* point,Vec3* ray,Vec3* velocity,const Vec3* offset,bool project,bool unitAim)
{
    ++ceMuzzleAims;if(ceMuzzleAimFault)RaiseException(0xE000CE82,0,0,nullptr);
    CeMuzzleCheck(unit==ceOwner&&point&&point->x==2&&point->z==4&&ray&&ray->y==1&&velocity&&!offset&&!project&&!unitAim,
        "native modern obstruction ABI preserves velocity destination");
    point->y=2.5f;velocity->z=12;
}
static void __fastcall CeMuzzleLegacy(uint32_t unit,Vec3* point,Vec3* ray,float* speed,bool project,bool unitAim)
{
    CeMuzzleCheck(unit==ceOwner&&point&&point->y==2.5f&&ray->y==1&&speed&&!project&&!unitAim,"legacy origin and native speed ABI");*speed=7;
}
static int16_t __fastcall CeMuzzleAssist(uint32_t unit,Vec3* point,Vec3* ray)
{CeMuzzleCheck(unit==ceOwner,"native director owner");*point={22,33,44};*ray={1,0,0};return 2;}
static int16_t __fastcall CeMuzzleMarker(uint32_t weapon,const char* name,void* data,int16_t capacity)
{++ceMuzzleMarkers;CeMuzzleCheck(weapon==ceWeapon&&std::strcmp(name,"primary trigger")==0&&capacity==64,"CE native marker ABI");std::memset(data,0x5A,0x6C);return ceMuzzleCount;}
static void __fastcall CeMuzzleFire(uint32_t weapon,int16_t barrel,uint32_t simulation,int16_t mode,const void* replicated,uint8_t predicted)
{
    ++ceMuzzleFires;CeMuzzleCheck(weapon==ceWeapon&&barrel==0&&simulation==0xABCDEF12&&mode==2&&predicted==0xAA,"CE native six-argument outer fire ABI");
    alignas(float) uint8_t marker[0x6C]{};
    CeMuzzleCheck(MuzzleMarkersBody(weapon,"primary trigger",marker,64,moduleBase+0xB7A574)==ceMuzzleCount,"signed native count retained");
    if(ceMuzzleExpected)
    {
        CeMuzzleCheck(*reinterpret_cast<float*>(marker+0x64)==2.5f&&*reinterpret_cast<float*>(marker+0x40)==1,"CE final native marker drives downstream origin");
        for(size_t i=0;i<0x3C;++i)CeMuzzleCheck(marker[i]==0x5A,"local matrix and world scale retained");
        CeMuzzleCheck(*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x1EC)==ceTarget,"native assist sees per-shot cached target");
        Vec3 point{},ray{},velocity{},camera{};float speed{};
        ModernRayBody(ceOwner,&point,&ray,&velocity,&camera,true,true,moduleBase+0xB7A796);
        CeMuzzleCheck(point.y==2.5f&&ray.y==1&&velocity.z==12,"actual modern helper uses barrel and native velocity");
        LegacyRayBody(ceOwner,&point,&ray,&speed,true,true,moduleBase+0xB7A858);
        CeMuzzleCheck(speed==7,"legacy native speed preserved");
        CeMuzzleCheck(AssistRayBody(ceOwner,&point,&ray,moduleBase+0xB67BE4)==2&&point.y==2.5f&&ray.y==1,"later assist retains native perspective and barrel ray");
        if(ceMuzzleNested)
        {
            ceMuzzleNested=false;const auto* lease=muzzleRequest.lease;
            MuzzleFireHook(weapon,barrel,simulation,mode,replicated,predicted);
            CeMuzzleCheck(muzzleRequest.lease==lease&&muzzleShot.active,"nested fire restores outer shot scope");
        }
    }
    else for(auto b:marker)CeMuzzleCheck(b==0x5A,"refused optional marker stays byte-identical");
    if(ceMuzzleNativeChange)*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x1EC)=0x98760004;
    if(ceMuzzleFireFault)RaiseException(0xE000CE83,0,0,nullptr);
}
static void ResetCeMuzzle()
{
    testTitle=GameTitle::HaloCE;generation=testGeneration;installed=active=aimInstalled=targetInstalled=true;
    retiring=targetRetiring=false;muzzleInstalled=true;muzzleFaulted=false;muzzleInstalledAt=1;
    gameplayValid=contextValid=renderContextValid=true;
    testContext.tracking.generation=testGeneration;testContext.tracking.spaceEpoch=4;testContext.tracking.serial=12;
    testContext.tracking.predictedDisplayTimeNs=1000000000;
    testContext.tracking.controllers.controlsPresentationBlocked=false;
    testContext.tracking.controllers.gunBarrelAim=true;testContext.tracking.controllers.leftHanded=false;
    // Muzzle publication reconstructs the visual and neutral controller frames.
    // Supply a complete, matching frame instead of inheriting earlier test state.
    testContext.camera={};testContext.camera.forward={1,0,0};testContext.camera.up={0,0,1};
    testContext.camera.viewport={0,0,800,1000};testContext.camera.window=testContext.camera.viewport;
    testContext.camera.verticalFov=1;testContext.camera.nearPlane=.01f;testContext.camera.farPlane=1000;
    testContext.reference={};testContext.reference.generation=testGeneration;testContext.reference.spaceEpoch=4;
    testContext.unitsPerMeter=1;testContext.positional=true;testContext.tracking.headPosition={};
    testContext.tracking.controllers.primaryAim={true,{.3f,1.2f,-.6f},{}};
    testContext.tracking.controllers.visualPitchDeg=0;
    testContext.tracking.controllers.visualYawDeg=0;
    testContext.tracking.controllers.visualRollDeg=0;
    gameplayContext=testContext;
    targetPlayer={};targetPlayerValid=true;targetPlayer.generation=testGeneration;targetPlayer.unit=ceOwner;targetPlayer.weapon=ceWeapon;
    targetPlayer.inputUser=2;targetPlayer.hasControlledUnit=targetPlayer.onFoot=targetPlayer.nativePreparesFirstPerson=true;
    targetPlayer.nativeInputBlocked=targetPlayer.nativeLookBlocked=false;
    std::memset(ceMuzzleUnit,0,sizeof(ceMuzzleUnit));*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x308)=UINT32_MAX;
    *reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x1EC)=0x55550005;std::memcpy(ceMuzzleReplacement,ceMuzzleUnit,sizeof(ceMuzzleUnit));ceMuzzleStorage=ceMuzzleUnit;
    const auto now=GetTickCount64();paletteReceipt.Publish({testContext,now,1});lastApplied=now;
    MuzzlePalette p{};p.context=testContext;p.unit=ceOwner;p.weapon=ceWeapon;
    p.palette.barrels[0]={GameTitle::HaloCE,testGeneration,ceOwner,ceWeapon,1,4,12,now,1000000000,0,0,false,{{2,3,4},{0,1,0},{0,0,1}}};muzzlePalette.Publish(p);
    muzzleShot={};muzzleRequest={};muzzleQueryContext={testGeneration,ceOwner,2,3,false,now};
    ceMuzzleExpected=true;ceMuzzleCount=1;ceMuzzleFires=ceMuzzleQueries=ceMuzzleMarkers=ceMuzzleAims=0;
    ceMuzzleNested=ceMuzzleQueryFault=ceMuzzleFireFault=ceMuzzleAimFault=ceMuzzleOmitQuery=ceMuzzleReplace=ceMuzzleNativeChange=ceMuzzleRevoke=false;
    muzzleFireHook.original=reinterpret_cast<void*>(&CeMuzzleFire);muzzleMarkersHook.original=reinterpret_cast<void*>(&CeMuzzleMarker);
    muzzleQueryHook.original=reinterpret_cast<void*>(&CeMuzzleQuery);muzzleDirectQueryHook.original=reinterpret_cast<void*>(&CeMuzzleDirectQuery);
    modernRayHook.original=reinterpret_cast<void*>(&CeMuzzleAim);legacyRayHook.original=reinterpret_cast<void*>(&CeMuzzleLegacy);
    assistRayHook.original=reinterpret_cast<void*>(&CeMuzzleAssist);targetQueryHook.original=reinterpret_cast<void*>(&CeMuzzleSearch);
}
static void ShootCeMuzzle(const void* replicated=nullptr)
{MuzzleFireHook(ceWeapon,0,0xABCDEF12,2,replicated,0xAA);}
static bool FaultCeMuzzle(bool ray=false)
{__try{if(ray)ModernRayHook(ceOwner,nullptr,nullptr,nullptr,nullptr,false,false);else ShootCeMuzzle();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
static void RunCeMuzzleTests()
{
    uint8_t jump[]{0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};const auto stub=reinterpret_cast<uintptr_t>(&CeMuzzleObject);std::memcpy(jump+2,&stub,8);
    auto* service=reinterpret_cast<void*>(moduleBase+contract::player_state::state_object_try_get);DWORD old{};
    CeMuzzleCheck(VirtualProtect(service,sizeof(jump),PAGE_EXECUTE_READWRITE,&old)!=0,"private fixture service protection");
    std::memcpy(service,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),service,sizeof(jump));
    ResetCeMuzzle();ShootCeMuzzle();CeMuzzleCheck(ceMuzzleFires==1&&ceMuzzleQueries==1&&ceMuzzleAims==2&&callbacks==0&&*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x1EC)==0x55550005,"native fire executes once and target restores");
    ResetCeMuzzle();ceMuzzleNested=true;muzzleQueryContext.direct=true;ShootCeMuzzle();CeMuzzleCheck(ceMuzzleFires==2&&ceMuzzleQueries==2&&!muzzleShot.active&&callbacks==0,"direct query and nested leases restore");
    for(int reject=0;reject<22;++reject)
    {
        ResetCeMuzzle();ceMuzzleExpected=false;
        switch(reject){case 0:ceMuzzleCount=0;break;case 1:ceMuzzleCount=2;break;case 2:ceMuzzleCount=-1;break;
        case 3:muzzleInstalled=false;break;case 4:muzzleFaulted=true;break;case 5:gameplayContext.tracking.controllers.gunBarrelAim=false;break;
        case 6:gameplayContext.tracking.controllers.controlsPresentationBlocked=true;break;case 7:targetPlayer.onFoot=false;break;
        case 8:targetPlayer.weapon^=0x10000;break;case 9:targetPlayer.nativeCinematicFlag=true;break;
        case 10:muzzleQueryContext.at=GetTickCount64()-200;break;case 11:muzzleQueryContext.generation++;break;
        case 12:gameplayContext.referenceRevision++;break;case 13:gameplayContext.rendererEpoch++;break;
        case 14:gameplayContext.tracking.controllers.leftHanded=true;break;case 15:ceMuzzleOmitQuery=true;break;
        case 16:ceMuzzleReplace=true;break;case 17:ceMuzzleRevoke=true;break;
        case 18:*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x308)=ceTarget;break;
        case 19:lastApplied=0;break;case 20:gameplayContext.tracking.predictedDisplayTimeNs+=100000001;break;
        case 21:muzzleQueryContext.at=GetTickCount64()+1000;break;}
        ShootCeMuzzle();CeMuzzleCheck(ceMuzzleFires==1&&!muzzleShot.active&&!callbacks,"rejected shot stays native without replay or leaking scope");
    }
    ResetCeMuzzle();ceMuzzleExpected=false;ShootCeMuzzle(reinterpret_cast<void*>(1));CeMuzzleCheck(!ceMuzzleQueries,"replicated targeting stays native");
    ResetCeMuzzle();ceMuzzleExpected=false;ceMuzzleQueryFault=true;ShootCeMuzzle();CeMuzzleCheck(muzzleFaulted&&!callbacks,"private query exception isolates feature");
    ResetCeMuzzle();ceMuzzleExpected=false;ceMuzzleAimFault=true;ShootCeMuzzle();CeMuzzleCheck(muzzleFaulted&&!callbacks,"private obstruction exception isolates feature");
    ResetCeMuzzle();ceMuzzleFireFault=true;CeMuzzleCheck(FaultCeMuzzle()&&!callbacks&&ceMuzzleFires==1&&*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x1EC)==0x55550005,"native fire exception restores once");
    ResetCeMuzzle();ceMuzzleAimFault=true;CeMuzzleCheck(FaultCeMuzzle(true)&&!callbacks&&ceMuzzleAims==1,"actual aim hook SEH callback retirement without replay");
    ResetCeMuzzle();ceMuzzleNativeChange=true;ShootCeMuzzle();CeMuzzleCheck(*reinterpret_cast<uint32_t*>(ceMuzzleUnit+0x1EC)==0x98760004,"new native target wins over lease release");
    for(const auto& marker:weapon_muzzle::kMarkers)
    {
        if(marker.title!=GameTitle::HaloCE)continue;
        for(int hand=0;hand<2;++hand)for(int renderer=0;renderer<2;++renderer)
        {
            ResetCeMuzzle();testContext.tracking.controllers.leftHanded=hand!=0;testContext.rendererEpoch+=renderer;gameplayContext=testContext;
            const uint64_t now=GetTickCount64();paletteReceipt.Publish({testContext,now,marker.identity});lastApplied=now;
            Scope source{};source.context=testContext;source.muzzleUnit=ceOwner;source.muzzleWeapon=ceWeapon;
            FirstPersonBinding binding{};binding.nodeIdentity=marker.identity;binding.count=marker.nodeCount;
            NodeMatrix palette[64]{};for(auto& n:palette){n.scale=2;n.forward={1,0,0};n.left={0,1,0};n.up={0,0,1};n.position={2,3,4};}
            PublishMuzzlePalette(source,binding,palette,now);weapon_muzzle::Receipt result{};
            CeMuzzleCheck(ReadMuzzle(ceOwner,ceWeapon,marker.barrel,result)&&result.identity==marker.identity&&result.leftHanded==(hand!=0),"both renderer epochs and handedness read actual CE marker catalog");
            source.muzzleWeapon^=0x10000;PublishMuzzlePalette(source,binding,palette,now);
            CeMuzzleCheck(!ReadMuzzle(ceOwner,ceWeapon,marker.barrel,result),"weapon replacement before commit invalidates muzzle");
        }
    }
    ResetCeMuzzle();muzzleQueryContext={};CaptureMuzzleQuery(1,3,false);CeMuzzleCheck(!muzzleQueryContext.at,"foreign input query rejected");
    CaptureMuzzleQuery(2,3,true);CeMuzzleCheck(muzzleQueryContext.unit==ceOwner&&muzzleQueryContext.direct,"own query mode and zoom captured");
    // Existing broad retirement fixture follows; these stubs have no installed hooks.
    muzzleFireHook=muzzleMarkersHook=muzzleQueryHook=muzzleDirectQueryHook={};muzzleInstalled=false;
    modernRayHook=legacyRayHook=assistRayHook=targetQueryHook={};aimInstalled=targetInstalled=false;
    std::printf("PASS: %u production CE muzzle checks (native services stubbed)\n",ceMuzzleChecks);
}
