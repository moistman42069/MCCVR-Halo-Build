static uint16_t muzzleCount=1;
static unsigned markerCalls{},muzzleFires{};
static bool expectMuzzle=true,muzzleNativeFault=false,muzzleNested=false,muzzlePreflightFault=false;
static uint32_t markerObject=primary;
static bool parentMarker=false;
static uint32_t overrideMarker=UINT32_MAX;
static float ExpectedMuzzleX(){return g_halo2MuzzleRequest.weapon==secondary?-2.0f:2.0f;}
static uint16_t NativeMarkers(uint32_t object,uint32_t name,void* data,int16_t capacity)
{
    ++markerCalls;
    Check(object==markerObject&&name==0xF0000DB&&capacity==64,"native marker ABI preserved");
    if(muzzleNativeFault)RaiseException(0xE0424444,0,0,nullptr);
    std::memset(data,0x5A,0x70);return muzzleCount;
}
static void NativeMuzzleAim(uint32_t unit,float* position,float* direction,uint64_t velocity,
    float* offset,uint8_t project,uint8_t use,uint8_t collision)
{
    ++aimCalls;
    if(velocity!=0xFEDCBA9876543210ull)
    {
        Check(g_halo2CollisionOwnQuery,"H2 obstruction preflight cannot schedule collision/melee work");
        if(muzzlePreflightFault)RaiseException(0xE0425555,0,0,nullptr);
        Check(unit==owner&&velocity&&!offset&&!project&&!use&&collision==1,
            "marker preflight uses private native velocity storage and obstruction clamp");
        Check(position[0]==ExpectedMuzzleX()&&position[1]==3&&position[2]==4&&direction[1]==1,
            "preflight sees visible marker before any native origin restoration");
        auto* output=reinterpret_cast<float*>(velocity);
        output[0]=10;output[1]=20;output[2]=30;
        position[1]=2.5f;return;
    }
    if(expectMuzzle)
    {
        Check(unit==owner&&velocity&&!offset&&!project&&!use&&collision==1,
            "muzzle passes native velocity and collision while disabling camera relocation");
        Check(position[0]==ExpectedMuzzleX()&&position[1]==2.5f&&position[2]==4&&direction[0]==0&&direction[1]==1,
            "native clamp starts at actual authored muzzle and direction");
        position[1]=2.5f; // Native obstruction result must survive every later override.
    }
    else
    {
        Check(offset&&project==1&&use==1&&collision==1,"refused muzzle preserves all native helper arguments");
        position[0]=0;position[1]=0;position[2]=1;
        direction[0]=1;direction[1]=0;direction[2]=0;
    }
}
static void NativeMuzzleFire(uint32_t weapon,int16_t barrel,int32_t projectile,uint8_t predicted)
{
    ++muzzleFires;
    Check(barrel==0&&projectile==-7&&predicted==0xFA,"muzzle preserves complete native fire ABI");
    alignas(float) uint8_t nativeMarker[0x70]{};
    caller=g_halo2Muzzle.base+0x8E4BAF;
    markerObject=overrideMarker!=UINT32_MAX?overrideMarker:parentMarker?owner:weapon;
    Check(Halo2MuzzleMarkersDetour(markerObject,0xF0000DB,nativeMarker,64)==muzzleCount,"native marker count unchanged");
    if(expectMuzzle)
    {
        const float* forward=reinterpret_cast<const float*>(nativeMarker+0x3C);
        const float* up=reinterpret_cast<const float*>(nativeMarker+0x54);
        const float* position=reinterpret_cast<const float*>(nativeMarker+0x60);
        Check(forward[0]==0&&forward[1]==1&&up[2]==1&&position[0]==ExpectedMuzzleX()&&position[1]==2.5f&&position[2]==4,
            "native firing buffer receives visible muzzle position and orientation");
        for(unsigned i=0;i<0x3C;++i)Check(nativeMarker[i]==0x5A,"local marker and world scale retained");
        for(unsigned i=0x6C;i<0x70;++i)Check(nativeMarker[i]==0x5A,"native marker flags retained");
        Check(g_halo2IndependentShot.barrel&&*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x22220005,
            "direct native homing sees muzzle acquisition target");
    }
    else for(auto value:nativeMarker)Check(value==0x5A,"refused marker buffer remains native");
    float origin[3]{},direction[3]{},offset[3]{};
    caller=g_halo2Dual.base+0x8E4FCD;
    Halo2IndependentAimDetour(owner,origin,direction,0xFEDCBA9876543210ull,offset,1,1,1);
    if(expectMuzzle)Check(origin[1]==2.5f&&direction[1]==1,"native wall clipping survives early helper");
    caller=g_halo2Dual.base+0x7597BD;
    float cameraPoint[3]{},cameraForward[3]{};
    Check(Halo2IndependentCameraDetour(owner,cameraPoint,cameraForward)==0x12345678,"native assist camera return unchanged");
    if(expectMuzzle)Check(cameraPoint[0]==ExpectedMuzzleX()&&cameraPoint[1]==2.5f&&cameraPoint[2]==4&&cameraForward[1]==1,
        "downstream native assist uses clipped muzzle and exact barrel direction");
    if(muzzleNested&&weapon==primary)
    {
        const auto outer=g_halo2MuzzleRequest;const auto shot=g_halo2IndependentShot;
        Halo2IndependentFireDetour(secondary,0,-7,0xFA);
        Check(g_halo2MuzzleRequest.lease==outer.lease&&g_halo2MuzzleRequest.weapon==primary&&
            g_halo2IndependentShot.slot==shot.slot&&g_halo2IndependentShot.barrel,
            "nested muzzle shot restores outer lease and weapon role");
    }
}
static void ResetMuzzle(bool dual=false)
{
    Reset();g_config.gun_barrel_aim=true;g_config.independent_dual_aim=dual;g_config.left_handed=false;
    g_halo2Muzzle.base=g_halo2Dual.base;g_halo2Muzzle.generation=7;g_halo2Muzzle.original=NativeMarkers;
    g_halo2Muzzle.enabled=true;g_halo2Muzzle.faulted=false;g_halo2MuzzleRequest={};
    g_halo2Dual.fireOriginal=NativeMuzzleFire;g_halo2Dual.aimOriginal=NativeMuzzleAim;
    markerCalls=muzzleFires=0;muzzleCount=1;expectMuzzle=true;muzzleNativeFault=muzzleNested=muzzlePreflightFault=false;
    parentMarker=false;overrideMarker=UINT32_MAX;
    for(uint8_t slot=0;slot<2;++slot)
    {
        weapon_muzzle::Palette sample{};
        sample.barrels[0]={GameTitle::Halo2,7,owner,slot?secondary:primary,1,3,11,GetTickCount64(),
            1000000000,slot,0,false,{{slot?-2.0f:2.0f,3,4},{0,1,0},{0,0,1}}};
        Check(g_halo2Muzzles.Publish(GameTitle::Halo2,slot,sample),"fixture committed muzzle publication");
    }
    RecordHalo2IndependentQuery();
    if(!dual)users[kHalo2FirstPersonWeaponDataOffset+kHalo2FirstPersonWeaponSlotStride]=0;
}
static void ShootMuzzle(uint32_t weapon=primary){Halo2IndependentFireDetour(weapon,0,-7,0xFA);}
static bool FaultingMuzzle(){__try{ShootMuzzle();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
static void MuzzleTests()
{
    ResetMuzzle();ShootMuzzle();
    Check(markerCalls==1&&queryCalls==1&&muzzleFires==1&&
        *reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x44440007,"single weapon muzzle query and LIFO cleanup");
    Check(!g_halo2IndependentShot.active&&!g_halo2MuzzleRequest.lease&&!g_halo2Muzzle.callbacks&&!g_halo2Dual.callbacks&&!g_halo2CollisionOwnQuery,
        "muzzle scopes and native callbacks balanced");
    ResetMuzzle(true);muzzleNested=true;ShootMuzzle();
    Check(muzzleFires==2&&queryCalls==4&&*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x44440007,
        "dual and muzzle leases restore in reverse order across nested guns");
    ResetMuzzle(true);parentMarker=true;ShootMuzzle(primary);ShootMuzzle(secondary);
    Check(muzzleFires==2&&markerCalls==2&&queryCalls==4,
        "native parent markers retain separate primary and secondary visible origins");
    for(uint32_t foreign:{owner+0x10000u,secondary,0x11110004u}) {
        ResetMuzzle();overrideMarker=foreign;expectMuzzle=false;ShootMuzzle();
        Check(queryCalls==0&&muzzleFires==1,"foreign or other-weapon marker does not gain local ownership");
    }
    for(int reason=0;reason<9;++reason)
    {
        ResetMuzzle();expectMuzzle=false;
        switch(reason)
        {
        case 0:muzzleCount=0;break;case 1:muzzleCount=2;break;case 2:g_halo2Muzzle.enabled=false;break;
        case 3:g_halo2Muzzle.faulted=true;break;case 4:g_halo2Muzzle.generation=8;break;
        case 5:g_config.gun_barrel_aim=false;break;case 6:tracking.referenceEpoch=4;break;
        case 7:g_config.left_handed=true;break;case 8:exclusive_input::active=true;break;
        }
        ShootMuzzle();Check(queryCalls==0&&muzzleFires==1,"unavailable muzzle forwards native shot once without query");
    }
    ResetMuzzle();expectMuzzle=false;queryFault=true;ShootMuzzle();
    Check(g_halo2Muzzle.faulted&&!g_halo2Dual.faulted&&muzzleFires==1,
        "muzzle query exception isolates failure from independent firing");
    ResetMuzzle();expectMuzzle=false;muzzlePreflightFault=true;ShootMuzzle();
    Check(g_halo2Muzzle.faulted&&!g_halo2Dual.faulted&&!g_halo2CollisionOwnQuery&&queryCalls==0&&muzzleFires==1,
        "H2 failed obstruction preflight restores scheduler scope and forwards native fire once");
    ResetMuzzle();muzzleNativeFault=true;
    Check(FaultingMuzzle()&&markerCalls==1&&muzzleFires==1&&!g_halo2Muzzle.callbacks&&!g_halo2Dual.callbacks&&
        !g_halo2MuzzleRequest.lease,"native marker exception propagates once after cleanup");
    g_config.gun_barrel_aim=false;g_halo2Muzzle.enabled=false;
}
