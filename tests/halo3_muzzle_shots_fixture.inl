static uint64_t muzzleCount=1;
static unsigned markerCalls{},muzzleFires{};
static bool expectMuzzle=true,muzzleNativeFault=false,muzzleNested=false,muzzlePreflightFault=false;
static uint64_t NativeMarkers(uint32_t object,uint32_t name,void* data,int16_t capacity,uint8_t original,uint8_t interpolated)
{
    ++markerCalls;
    Check((object==primary||object==secondary)&&name==0xD4&&capacity==64&&original==0xAA&&interpolated==0xBB,
        "all six native H3 marker arguments forwarded");
    if(muzzleNativeFault)RaiseException(0xE0424444,0,0,nullptr);
    std::memset(data,0x5A,0x70);return muzzleCount;
}
static void NativeMuzzleAim(uint32_t unit,float* position,float* direction,uint64_t velocity,
    float* offset,uint8_t project,uint8_t use)
{
    ++aimCalls;
    if(velocity!=0xFEDCBA9876543210ull)
    {
        Check(g_legacyCollisionOwnedQuery,"H3 obstruction preflight cannot schedule collision/melee work");
        if(muzzlePreflightFault)RaiseException(0xE0425555,0,0,nullptr);
        Check(unit==owner&&velocity&&!offset&&!project&&!use,"private H3 obstruction preflight ABI");
        Check(position[0]==2&&position[1]==3&&position[2]==4&&direction[1]==1,"native H3 clamp sees authored muzzle");
        auto* output=reinterpret_cast<float*>(velocity);output[0]=10;output[1]=20;output[2]=30;
        position[1]=2.5f;return;
    }
    if(expectMuzzle)
    {
        Check(unit==owner&&!offset&&!project&&!use&&position[0]==2&&position[1]==2.5f&&position[2]==4&&direction[1]==1,
            "ordinary H3 helper retains clipped origin without camera projection");
    }
    else
    {
        Check(offset&&project==1&&use==1,"refused H3 muzzle leaves ordinary helper arguments native");
        position[0]=position[1]=position[2]=0;direction[0]=0;direction[1]=1;direction[2]=0;
    }
}
static uint64_t NativeMuzzleFire(uint32_t weapon,int16_t barrel,void* data,int32_t index,uint8_t predicted)
{
    ++muzzleFires;
    Check(barrel==0&&data==reinterpret_cast<void*>(0x1234)&&index==-7&&predicted==0xFA,"H3 five-argument firing ABI preserved");
    alignas(float) uint8_t marker[0x70]{};
    caller=imageBase+0x368651;
    Check(Halo3MuzzleMarkersDetour(weapon,0xD4,marker,64,0xAA,0xBB)==muzzleCount,"full native marker return preserved");
    if(expectMuzzle)
    {
        const float* forward=reinterpret_cast<const float*>(marker+0x3C);
        const float* up=reinterpret_cast<const float*>(marker+0x54);
        const float* point=reinterpret_cast<const float*>(marker+0x60);
        Check(forward[0]==0&&forward[1]==1&&up[2]==1&&point[0]==2&&point[1]==2.5f&&point[2]==4,
            "H3 firing marker uses authored barrel and native obstruction result");
        for(unsigned i=0;i<0x3C;++i)Check(marker[i]==0x5A,"native H3 local marker and scale untouched");
        for(unsigned i=0x6C;i<0x70;++i)Check(marker[i]==0x5A,"native H3 marker flags untouched");
        Check(g_halo3IndependentShot.barrel&&*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x22220005,
            "H3 native homing reads per-muzzle acquired target");
    }
    else for(auto value:marker)Check(value==0x5A,"refused H3 marker retains every byte");
    float point[3]{},direction[3]{},offset[3]{};
    caller=imageBase+0x368B97;
    Halo3IndependentAimDetour(owner,point,direction,0xFEDCBA9876543210ull,offset,1,1);
    caller=imageBase+0x13BC0D;
    Check(Halo3IndependentCameraDetour(owner,point,direction)==17,"H3 native assist perspective preserved");
    if(expectMuzzle)Check(point[0]==2&&point[1]==2.5f&&point[2]==4&&direction[1]==1,
        "later H3 native assist uses clipped muzzle and barrel direction");
    if(muzzleNested&&weapon==primary)
    {
        const auto previous=g_halo3MuzzleRequest;
        Check(Halo3IndependentFireDetour(secondary,0,data,-7,0xFA)==0x123456789ABCDEF0ull,"nested H3 return preserved");
        Check(g_halo3MuzzleRequest.weapon==primary&&g_halo3MuzzleRequest.lease==previous.lease&&
            g_halo3IndependentShot.slot==0&&g_halo3IndependentShot.barrel,"nested H3 restores outer muzzle scope");
    }
    return 0x123456789ABCDEF0ull;
}
static void ResetMuzzle(bool dual=false)
{
    Reset();g_config.gun_barrel_aim=true;g_config.independent_dual_aim=dual;g_config.left_handed=false;
    g_halo3Muzzle.base=imageBase;g_halo3Muzzle.generation=7;g_halo3Muzzle.original=NativeMarkers;
    g_halo3Muzzle.enabled=true;g_halo3Muzzle.faulted=false;g_halo3MuzzleRequest={};
    g_halo3Dual.fireOriginal=NativeMuzzleFire;g_halo3Dual.aimOriginal=NativeMuzzleAim;
    markerCalls=muzzleFires=queryCalls=0;muzzleCount=1;expectMuzzle=true;muzzleNativeFault=muzzleNested=muzzlePreflightFault=false;
    for(uint8_t slot=0;slot<2;++slot)
    {
        weapon_muzzle::Palette sample{};
        sample.barrels[0]={GameTitle::Halo3,7,owner,slot?secondary:primary,1,3,11,GetTickCount64(),
            1000000000,slot,0,false,{{2,3,4},{0,1,0},{0,0,1}}};
        Check(g_halo3Muzzles.Publish(GameTitle::Halo3,slot,sample),"H3 committed muzzle fixture");
    }
    if(!dual)liveWeapons[1]=UINT32_MAX;
}
static uint64_t ShootMuzzle(){return Halo3IndependentFireDetour(primary,0,reinterpret_cast<void*>(0x1234),-7,0xFA);}
static bool FaultingMuzzle(){__try{(void)ShootMuzzle();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
static void MuzzleTests()
{
    ResetMuzzle();Check(ShootMuzzle()==0x123456789ABCDEF0ull,"muzzle preserves H3 native return");
    Check(markerCalls==1&&queryCalls==1&&muzzleFires==1&&*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x44440007,
        "single H3 weapon muzzle query and target restoration");
    ResetMuzzle(true);muzzleNested=true;(void)ShootMuzzle();
    Check(muzzleFires==2&&queryCalls==4&&*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x44440007,
        "nested H3 dual and muzzle target leases restore in reverse order");
    for(int reason=0;reason<11;++reason)
    {
        ResetMuzzle();expectMuzzle=false;
        switch(reason)
        {
        case 0:muzzleCount=0;break;case 1:muzzleCount=2;break;case 2:g_halo3Muzzle.enabled=false;break;
        case 3:g_halo3Muzzle.faulted=true;break;case 4:g_halo3Muzzle.generation=8;break;
        case 5:g_config.gun_barrel_aim=false;break;case 6:tracking.referenceEpoch=4;break;
        case 7:g_config.left_handed=true;break;case 8:exclusive_input::active=true;break;
        case 9:cinematic=true;break;case 10:seated=true;break;
        }
        (void)ShootMuzzle();Check(queryCalls==0&&muzzleFires==1,"unavailable H3 muzzle preserves native firing");
        exclusive_input::active=false;
    }
    ResetMuzzle();expectMuzzle=false;queryFault=true;(void)ShootMuzzle();
    Check(g_halo3Muzzle.faulted&&!g_halo3Dual.faulted&&muzzleFires==1,"H3 muzzle acquisition failure stays isolated");
    ResetMuzzle();expectMuzzle=false;muzzlePreflightFault=true;(void)ShootMuzzle();
    Check(g_halo3Muzzle.faulted&&!g_halo3Dual.faulted&&!g_legacyCollisionOwnedQuery&&queryCalls==0&&muzzleFires==1,
        "H3 failed obstruction preflight restores scheduler scope and forwards native fire once");
    ResetMuzzle();muzzleNativeFault=true;
    Check(FaultingMuzzle()&&markerCalls==1&&muzzleFires==1&&!g_halo3Muzzle.callbacks&&!g_halo3Dual.callbacks&&
        !g_halo3MuzzleRequest.lease&&!g_legacyCollisionOwnedQuery,"H3 native marker exception propagates once with balanced scopes");
    // Shots from the muzzle: the control asks for the converged point; a seat held at its stop or a turret
    // (SeatMuzzleShotHeld) refuses before asking, and the shot leaves as the game fires it.
    ResetMuzzle();g_config.gun_barrel_aim=false;g_config.shots_from_muzzle=true;muzzleAimCalls=0;seatHeld=false;
    (void)ShootMuzzle();Check(muzzleAimCalls==1&&muzzleFires==1,"H3 shots from the muzzle ask for the point (control)");
    ResetMuzzle();g_config.gun_barrel_aim=false;g_config.shots_from_muzzle=true;muzzleAimCalls=0;seatHeld=true;
    seatHeldCalls=0;expectMuzzle=false;(void)ShootMuzzle();
    Check(seatHeldCalls==1&&muzzleAimCalls==0&&muzzleFires==1&&*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x44440007,
        "H3 seat held at its stop or a turret refuses the redirect; native fire forwarded once");
    seatHeld=false;g_config.shots_from_muzzle=false;
    g_config.gun_barrel_aim=false;g_halo3Muzzle.enabled=false;
}
