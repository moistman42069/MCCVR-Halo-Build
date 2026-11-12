#include "../src/common/legacy_weapon_visual_defaults.h"
#include "../src/common/visual_aim.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>
#include "../src/common/dual_weapon_aim_logic.h"
#include "../src/common/weapon_muzzle.h"
#include "../src/common/native_shot_target_lease.h"
#include "../src/common/exclusive_input.h"
static unsigned checks{};
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
static constexpr uint32_t owner=0x12340001,primary=0x56780002,secondary=0x789A0003,target=0x22220005;
static constexpr uintptr_t imageBase=0x180000000;
static constexpr size_t kOdstTlsObjectTableOffset=0x20;
static std::atomic<uint32_t> g_odstRuntimeGeneration{7};
static std::atomic<bool> g_vrAim{true},g_enabled{true},g_odstMuzzleBindingsReady{true};
static struct {float barrel_pitch_deg=0,barrel_yaw_deg=0,barrel_roll_deg=0;bool gun_barrel_aim=true,left_handed=false,independent_dual_aim=false;
    float crosshair_distance_m=10,gun_yaw_deg=0,gun_pitch_deg=0,gun_roll_deg=0;bool shots_from_muzzle=false;} g_config;
// The three shot switches as they are in effect (sight_layer.h); here, the test's own config.
[[maybe_unused]] static bool Sight_ShotsFromMuzzle(){return g_config.shots_from_muzzle;}
[[maybe_unused]] static bool Sight_GunBarrelAim(){return g_config.gun_barrel_aim;}
[[maybe_unused]] static bool Sight_IndependentDualAim(){return g_config.independent_dual_aim;}
static float Game_GetWorldScale(){return 1.f;}
static std::atomic<bool> g_baseCamValid{true};
static std::atomic<float> g_worldScale{1},g_baseCamX{0},g_baseCamY{0},g_baseCamZ{0};
static float BaseCamHandsZ(){return g_baseCamZ.load();}
static float g_headYawRef=0,g_gameYawRef=0,g_headPosRef[3]{};
static void BuildTrackedGameBasisFromFrame(const float* orientation,bool,bool,int,int,float* basis)
{std::memcpy(basis,orientation,12);basis[4]=basis[8]=1;}
static void BasisFromAngles(float,float,float,float* basis){basis[0]=basis[4]=basis[8]=1;}
static void MultiplyBases(const float* basis,const float*,float* result){std::memcpy(result,basis,36);}
static void VR_ObserveSecondaryWeaponPresentation(GameTitle,uint32_t){}
static weapon_muzzle::Store g_odstMuzzles;
static thread_local bool g_legacyCollisionOwnedQuery=false;
static alignas(8) uint8_t tlsBytes[0x600]{},table[0x60]{},entries[6*0x18]{},users[0x4F38]{},
    unitBytes[0x400]{},replacementBytes[0x400]{},primaryBytes[0x200]{},secondaryBytes[0x200]{};
static uint8_t* slots[]{tlsBytes};static uint32_t tlsIndex=0;static uint32_t* g_odstEngineTlsIndex=&tlsIndex;
static uint32_t localUnit=owner;static bool seated=false,cinematic=false,stereo=true,validTracking=true;
static GameTitle title=GameTitle::Halo3ODST;
static int32_t Player(int32_t){return int32_t(localUnit);}
static bool Seated(int32_t){return seated;}
static auto g_odstPlayerUnitGetter=&Player;static auto g_odstUnitInVehicle=&Seated;
static const uint8_t* OdstContactTls(){return tlsBytes;}
static bool OdstContactObject(uint32_t handle)
{
    if(handle==target)return true;
    if(handle==UINT32_MAX||(handle&0xFFFF)>=4||!(handle>>16))return false;
    const auto* entry=entries+(handle&0xFFFF)*0x18;
    return *reinterpret_cast<const uint16_t*>(entry)==uint16_t(handle>>16)&&*reinterpret_cast<const void* const*>(entry+0x10);
}
static GameTitle TitleAdapter_GetActiveTitle(){return title;}
static uint32_t TitleAdapter_GetGeneration(GameTitle){return 7;}
// Shots from the muzzle (sight_layer.h): admitted only with the barrel aim off; the converged point itself is
// sight_layer's (aim_solve_tests), so here it keeps the barrel's direction and counts the asks.
static unsigned muzzleAimCalls=0;static bool muzzleAimRefuses=false;
static bool Sight_MuzzleShotsConverge(){return g_config.shots_from_muzzle&&!g_config.gun_barrel_aim;}
static bool Sight_AimMuzzleShot(GameTitle,uint32_t,const float*,const float*,float*,float*)
{++muzzleAimCalls;return !muzzleAimRefuses;}
// The seat guard (game.cpp SeatMuzzleShotHeld): a seat held at its stop or a turret refuses the redirect.
static unsigned seatHeldCalls=0;static bool seatHeld=false;
static bool SeatMuzzleShotHeld(GameTitle){++seatHeldCalls;return seatHeld;}
static bool VR_IsStereoEnabled(){return stereo;}
static CinematicControlState ReadOdstCinematicControl(int32_t&,int32_t&)
{return cinematic?CinematicControlState::AuthoredLocked:CinematicControlState::PlayerControlled;}
struct VrContactTrackingSnapshot
{uint64_t serial=11,referenceEpoch=3;int64_t timeNs=1000000000;
    struct {bool valid=true;float position[3]{},orientation[4]{1,0,0,0};} hands[2];};
static VrContactTrackingSnapshot tracking;
static bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out){out=tracking;return validTracking;}
static int ResolveEquippedWeaponSlot(uint32_t weapon,uint32_t a,uint32_t b,bool,bool)
{return weapon==a?0:weapon==b?1:-1;}
struct BoneMatrix {float scale=1,rotation[9]{1,0,0,0,1,0,0,0,1},translation[3]{};};
static bool DesiredWristWorld(bool,BoneMatrix& visual,float& scale,float*,BoneMatrix* neutral)
{visual={};*neutral=visual;scale=1;return true;}
struct FpInterpolationContext
{bool valid=true;int player=0,slot=0;uint32_t generation=7,muzzleUnit=owner,muzzleWeapon=primary;};
static struct {bool armed=true;VrContactTrackingSnapshot anatomicalTracking;} g_fpStereoSolveScope;
static uint32_t liveChecksum=0;static int liveCount=0;
static int LegacyAnatomicalRenderNodeCount(GameTitle value,uint16_t tag,uint32_t* checksum)
{Check(value==GameTitle::Halo3ODST&&tag==123,"ODST model lookup");*checksum=liveChecksum;return liveCount;}
#include "../src/dll/odst_muzzle_ownership.inl"
#define __readgsqword(offset) reinterpret_cast<uint64_t>(::slots)
#include "../src/dll/odst_muzzle_publication.inl"
#undef __readgsqword
// The shot-lag fix (sight_layer.h / game.cpp): the fire's `predicted` flag rides a thread-local for the witness.
enum class SightShotPath : uint8_t {Native=0,CrosshairAssist,Muzzle,UnitAdjust,Count};
static thread_local int t_legacyFirePredicted=-1;
static unsigned shotLagNotes{};
static void Sight_NoteShotLagFromIntent(GameTitle,SightShotPath,const float*,const float*,int,bool,int32_t)
{++shotLagNotes;}
static int32_t LegacyCollisionIgnoredObject(GameTitle){return -1;}
// THE LOCAL PLAYER'S FIRE (shot_lag_logic.h FireMark; game.cpp t_odstFire): opened by the fire, marked by its line.
#include "../src/common/shot_lag_logic.h"
static thread_local int8_t t_odstFire=shot_lag::kFireNone;
static thread_local uintptr_t caller{};
#define _ReturnAddress() reinterpret_cast<void*>(::caller)
// Aim Frame Zoom's record of the game's zoom level (game.cpp); the lane only reports it.
static int g_notedZoomLevel=-2;
static void NoteGameZoomLevel(GameTitle,int16_t level,uint32_t){g_notedZoomLevel=level;}
#include "../src/dll/odst_muzzle_shots.inl"
#include "../src/dll/odst_independent_publication.inl"
#undef _ReturnAddress

static unsigned fireCalls{},queryCalls{},markerCalls{},aimCalls{},cameraCalls{},viewCalls{};
static uint64_t markerCount=1;
static bool expectMuzzle=true,queryFault=false,preflightFault=false,markerFault=false,fireFault=false,
    nestedFire=false,omitView=false,replaceStorage=false,changeOwner=false,changeGeneration=false,
    nativeChangesTarget=false,invalidTarget=false,nonfiniteTarget=false;
static void __fastcall NativeView(uint32_t unit,uint64_t,float*,float* point,float* camera)
{++viewCalls;Check(unit==owner,"native query owner");point[0]=point[1]=point[2]=10;camera[0]=camera[1]=camera[2]=20;}
static int32_t __fastcall NativeCamera(uint32_t,float* point,float* direction)
{++cameraCalls;point[0]=point[1]=point[2]=10;direction[0]=1;direction[1]=direction[2]=0;return 17;}
static void __fastcall NativeQuery(int32_t user,uint8_t flags,float* direction,int16_t zoom,float* control,void* out)
{
    ++queryCalls;Check(user==2&&zoom==3,"native input user/zoom retained independently of output user");
    const bool active=g_odstMuzzleShot.active;
    if(active)Check(g_legacyCollisionOwnedQuery,"auxiliary query cannot schedule extra collision/melee");
    if(queryFault)RaiseException(0xE0421111,0,0,nullptr);
    float point[3]{},camera[3]{};const auto previous=caller;caller=imageBase+0x160647;
    if(!omitView)OdstMuzzleViewDetour(owner,0x12345678,direction,point,camera);
    if(active&&!omitView)Check(point[0]==2&&point[1]==2.5f&&camera[2]==4,"query starts at clipped visible muzzle");
    if(flags&2)
    {
        caller=imageBase+0x160A5D;OdstMuzzleViewDetour(owner,0,direction,point,camera);
        if(active)Check(point[1]==2.5f&&camera[0]==2,"native lead query shares muzzle origin");
    }
    caller=previous;std::memset(out,0,0x28);
    auto* words=static_cast<uint32_t*>(out);words[0]=1;words[2]=invalidTarget?0xABCD0005:target;words[1]=words[3]=UINT32_MAX;
    auto* values=static_cast<float*>(out);values[4]=.25f;values[5]=.75f;
    if(nonfiniteTarget)values[8]=NAN;
    control[0]=.1f;control[1]=.2f;control[2]=.3f;
    if(replaceStorage)*reinterpret_cast<uint8_t**>(entries+0x18+0x10)=replacementBytes;
    if(changeOwner)*reinterpret_cast<uint32_t*>(primaryBytes+0x160)=owner^0x10000;
    if(changeGeneration)g_odstRuntimeGeneration=8;
}
static void __fastcall NativeAim(uint32_t unit,float* point,float* direction,uint64_t velocity,float* offset,uint8_t project,uint8_t use)
{
    ++aimCalls;
    if(velocity!=0xFEDCBA9876543210ull)
    {
        Check(g_legacyCollisionOwnedQuery&&unit==owner&&velocity&&!offset&&!project&&!use,"ODST private obstruction preflight ABI");
        if(preflightFault)RaiseException(0xE0422222,0,0,nullptr);
        Check(point[0]==2&&point[1]==3&&point[2]==4&&direction[1]==1,"native clamp receives authored muzzle");
        reinterpret_cast<float*>(velocity)[0]=11;point[1]=2.5f;return;
    }
    if(expectMuzzle)Check(!offset&&!project&&!use&&point[1]==2.5f&&direction[1]==1,"later origin helper preserves clamped barrel without native projection");
    else Check(offset&&project&&use,"stock helper arguments retained");
}
static uint64_t __fastcall NativeMarkers(uint32_t object,uint32_t name,void* data,int16_t capacity,uint8_t original,uint8_t interpolate)
{
    ++markerCalls;Check((object==primary||object==secondary)&&name==0xD5&&capacity==64&&original==0xAA&&interpolate==0xBB,
        "ODST six-argument native marker ABI preserved");
    if(markerFault)RaiseException(0xE0423333,0,0,nullptr);
    std::memset(data,0x5A,0x70);return markerCount;
}
static uint64_t __fastcall NativeFire(uint32_t weapon,int16_t barrel,void* data,int32_t index,uint8_t predicted)
{
    ++fireCalls;Check(barrel==0&&data==reinterpret_cast<void*>(0x1234)&&index==-7&&predicted==0xFA,"ODST five-argument outer fire ABI");
    const auto previous=caller;alignas(float) uint8_t marker[0x70]{};caller=imageBase+0x3AF4CA;
    Check(OdstMuzzleMarkersDetour(weapon,0xD5,marker,64,0xAA,0xBB)==markerCount,"full native marker result preserved");
    if(expectMuzzle)
    {
        Check(reinterpret_cast<float*>(marker+0x60)[1]==2.5f&&reinterpret_cast<float*>(marker+0x3C)[1]==1&&
            reinterpret_cast<float*>(marker+0x54)[2]==1,"committed muzzle with native clipping and authored roll");
        for(size_t i=0;i<0x3C;++i)Check(marker[i]==0x5A,"local marker and world scale untouched");
        for(size_t i=0x6C;i<0x70;++i)Check(marker[i]==0x5A,"native marker flags untouched");
        Check(*reinterpret_cast<uint32_t*>(unitBytes+0x230)==target,"ODST direct homing sees independent native target");
    }
    else for(auto value:marker)Check(value==0x5A,"refused muzzle preserves every native marker byte");
    float point[3]{},direction[3]{},offset[3]{};caller=imageBase+0x3AEB2B;
    OdstMuzzleAimDetour(owner,point,direction,0xFEDCBA9876543210ull,offset,1,1);
    caller=imageBase+0x161204;Check(OdstMuzzleCameraDetour(owner,point,direction)==17,"assist perspective result retained");
    if(expectMuzzle)Check(point[0]==2&&point[1]==2.5f&&point[2]==4&&direction[1]==1,"later assist uses clamped muzzle");
    if(nestedFire&&weapon==primary)
    {
        const auto request=g_odstMuzzleRequest;
        Check(OdstMuzzleFireDetour(secondary,0,data,-7,0xFA)==0x123456789ABCDEF0ull,"nested native return retained");
        Check(g_odstMuzzleRequest.weapon==primary&&g_odstMuzzleRequest.lease==request.lease&&g_odstMuzzleShot.active,
            "nested firing restores outer target and scope");
    }
    if(nativeChangesTarget)*reinterpret_cast<uint32_t*>(unitBytes+0x230)=0xABCD0006;
    caller=previous;if(fireFault)RaiseException(0xE0424444,0,0,nullptr);return 0x123456789ABCDEF0ull;
}
static void Reset(bool dual=false)
{
    std::memset(unitBytes,0,sizeof(unitBytes));std::memset(entries,0,sizeof(entries));
    *reinterpret_cast<uint8_t**>(tlsBytes+kOdstTlsObjectTableOffset)=table;
    *reinterpret_cast<uint8_t**>(table+0x48)=entries;*reinterpret_cast<uint8_t**>(tlsBytes+0x598)=users;
    uint32_t handles[]{owner,primary,secondary};uint8_t* objects[]{unitBytes,primaryBytes,secondaryBytes};
    for(int i=0;i<3;++i){auto* entry=entries+(i+1)*0x18;*reinterpret_cast<uint16_t*>(entry)=uint16_t(handles[i]>>16);entry[3]=i?2:0;*reinterpret_cast<uint8_t**>(entry+0x10)=objects[i];}
    unitBytes[0x276]=0;unitBytes[0x277]=dual?1:0xFF;
    *reinterpret_cast<uint32_t*>(unitBytes+0x27C)=primary;*reinterpret_cast<uint32_t*>(unitBytes+0x280)=secondary;
    *reinterpret_cast<uint32_t*>(unitBytes+0x2B8)=UINT32_MAX;*reinterpret_cast<uint32_t*>(unitBytes+0x230)=0x44440007;
    for(auto* weapon:{primaryBytes,secondaryBytes}){weapon[0x155]=1;*reinterpret_cast<uint32_t*>(weapon+0x160)=owner;}
    *reinterpret_cast<uint32_t*>(users+0x3C)=primary;*reinterpret_cast<uint32_t*>(users+0x2740+0x3C)=secondary;
    localUnit=owner;seated=cinematic=false;stereo=validTracking=true;title=GameTitle::Halo3ODST;
    g_config={};g_config.gun_barrel_aim=true;tracking={};g_fpStereoSolveScope.anatomicalTracking=tracking;
    g_odstMuzzleBindingsReady=true;g_odstRuntimeGeneration=7;g_enabled=g_vrAim=true;exclusive_input::active=false;
    auto& feature=g_odstMuzzle;feature.base=imageBase;feature.generation=7;feature.installedAtMs=1;
    feature.enabled=true;feature.faulted=false;feature.callbacks=0;
    feature.fireOriginal=NativeFire;feature.aimOriginal=NativeAim;feature.queryOriginal=NativeQuery;
    feature.viewOriginal=NativeView;feature.cameraOriginal=NativeCamera;feature.markersOriginal=NativeMarkers;
    g_odstMuzzleShot={};g_odstMuzzleRequest={};g_odstMuzzleCapture={};g_odstMuzzleQueryContext={owner,7,2,0,3,GetTickCount64()};
    g_legacyCollisionOwnedQuery=false;fireCalls=queryCalls=markerCalls=aimCalls=cameraCalls=viewCalls=0;markerCount=1;
    expectMuzzle=true;queryFault=preflightFault=markerFault=fireFault=nestedFire=omitView=replaceStorage=changeOwner=changeGeneration=nativeChangesTarget=invalidTarget=nonfiniteTarget=false;
    for(uint8_t slot=0;slot<2;++slot)
    {
        weapon_muzzle::Palette sample{};sample.barrels[0]={GameTitle::Halo3ODST,7,owner,slot?secondary:primary,1,3,11,GetTickCount64(),1000000000,slot,0,false,{{2,3,4},{0,1,0},{0,0,1}}};
        Check(g_odstMuzzles.Publish(GameTitle::Halo3ODST,slot,sample),"committed ODST fixture");
    }
}
static uint64_t Shoot(){return OdstMuzzleFireDetour(primary,0,reinterpret_cast<void*>(0x1234),-7,0xFA);}
static bool FaultingShot(){__try{(void)Shoot();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
static void ShotTests()
{
    Reset();Check(Shoot()==0x123456789ABCDEF0ull,"native fire return retained");
    Check(fireCalls==1&&queryCalls==1&&markerCalls==1&&*reinterpret_cast<uint32_t*>(unitBytes+0x230)==0x44440007,"single shot restores native target");
    Reset(true);nestedFire=true;g_odstMuzzleQueryContext.flags=2;(void)Shoot();
    Check(fireCalls==2&&queryCalls==2&&*reinterpret_cast<uint32_t*>(unitBytes+0x230)==0x44440007&&!g_odstMuzzleShot.active,"nested and lead queries restore LIFO");
    for(int reason=0;reason<24;++reason)
    {
        Reset();expectMuzzle=false;
        switch(reason)
        {
        case 0:markerCount=0;break;case 1:markerCount=2;break;case 2:g_odstMuzzle.enabled=false;break;
        case 3:g_odstMuzzle.faulted=true;break;case 4:g_odstMuzzle.generation=8;break;case 5:g_config.gun_barrel_aim=false;break;
        case 6:tracking.referenceEpoch=4;break;case 7:g_config.left_handed=true;break;case 8:exclusive_input::active=true;break;
        case 9:cinematic=true;break;case 10:seated=true;break;case 11:g_odstMuzzleQueryContext.sampleMs=GetTickCount64()-200;break;
        case 12:unitBytes[0x276]=4;break;case 13:unitBytes[0x277]=4;break;case 14:primaryBytes[0x155]=0;break;
        case 15:*reinterpret_cast<uint32_t*>(primaryBytes+0x160)=owner^0x10000;break;
        case 16:*reinterpret_cast<uint32_t*>(unitBytes+0x2B8)=0xABCD0006;break;
        case 17:omitView=true;break;case 18:replaceStorage=true;break;case 19:changeOwner=true;break;
        case 20:changeGeneration=true;break;case 21:invalidTarget=true;break;case 22:nonfiniteTarget=true;break;
        case 23:tracking.hands[1].valid=false;break;
        }
        (void)Shoot();Check(fireCalls==1&&!g_odstMuzzleShot.active&&!g_odstMuzzleRequest.lease&&!g_legacyCollisionOwnedQuery,"refused adaptation forwards once and balances all scopes");
    }
    for(int fault=0;fault<4;++fault)
    {
        Reset();queryFault=fault==0;preflightFault=fault==1;markerFault=fault==2;fireFault=fault==3;
        if(fault<2)expectMuzzle=false;
        Check(FaultingShot()==(fault>=2),"optional faults isolate; native faults propagate");
        Check(fireCalls==1&&!g_odstMuzzle.callbacks&&!g_odstMuzzleShot.active&&!g_odstMuzzleRequest.lease&&!g_legacyCollisionOwnedQuery,"fault never replays native fire or leaks scopes");
        if(fault<2)Check(g_odstMuzzle.faulted,"optional fault recorded");
    }
    Reset();nativeChangesTarget=true;(void)Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x230)==0xABCD0006,"native targeting change wins over old lease");
    Reset();float dir[3]{0,1,0},control[3]{};uint8_t nativeTarget[0x28]{};g_odstMuzzleQueryContext={};
    OdstMuzzleQueryDetour(2,0,dir,3,control,nativeTarget);Check(g_odstMuzzleQueryContext.unit==owner&&g_odstMuzzleQueryContext.inputUser==2,"ordinary native query captures its real owner and input user");
    g_odstMuzzleQueryContext={};OdstMuzzleQueryDetour(2,1,dir,3,control,nativeTarget);Check(!g_odstMuzzleQueryContext.sampleMs,"lead-only fallback does not replace ordinary context");
    // Aim Frame Zoom: the local player's ordinary query reports the game's zoom level; nobody else's, and never ours.
    g_notedZoomLevel=-2;OdstMuzzleQueryDetour(2,0,dir,3,control,nativeTarget);Check(g_notedZoomLevel==-2,"another player's query is not the zoom level");
    g_odstMuzzle.queryOriginal=nullptr;   // the native stand-in insists on the owner's user and zoom
    OdstMuzzleQueryDetour(0,1,dir,1,control,nativeTarget);Check(g_notedZoomLevel==-2,"a lead-only query is not the zoom level");
    g_legacyCollisionOwnedQuery=true;OdstMuzzleQueryDetour(0,0,dir,1,control,nativeTarget);g_legacyCollisionOwnedQuery=false;Check(g_notedZoomLevel==-2,"our own query is not the zoom level");
    OdstMuzzleQueryDetour(0,0,dir,1,control,nativeTarget);Check(g_notedZoomLevel==1,"the local player's query is");
    OdstMuzzleQueryDetour(0,0,dir,-1,control,nativeTarget);Check(g_notedZoomLevel==-1,"not zoomed is -1");
    g_odstMuzzle.queryOriginal=NativeQuery;g_odstMuzzleQueryContext={};
    Reset();std::thread isolated([]{Check(!g_odstMuzzleQueryContext.sampleMs&&!g_odstMuzzleRequest.lease,"native query and firing scope are thread local");});isolated.join();
}
static void PublicationTests()
{
    Reset(true);BoneMatrix destination[64];
    for(auto& node:destination){node.scale=2;node.translation[0]=2;node.translation[1]=3;node.translation[2]=4;}
    for(const auto& marker:weapon_muzzle::kMarkers)
    {
        if(marker.title!=GameTitle::Halo3ODST)continue;liveChecksum=uint32_t(marker.identity);liveCount=marker.nodeCount;
        for(uint8_t slot=0;slot<2;++slot)
        {
            FpInterpolationContext context;context.slot=slot;context.muzzleWeapon=slot?secondary:primary;
            OdstPublishMuzzlePalette(123,context,destination,7);weapon_muzzle::Receipt receipt{};weapon_muzzle::Ray expected{};
            Check(weapon_muzzle::Transform(marker,2,destination[marker.node].rotation,destination[marker.node].translation,expected),"authored ODST marker setup");
            Check(g_odstMuzzles.Read(GameTitle::Halo3ODST,7,owner,context.muzzleWeapon,3,GetTickCount64(),1000000000,slot,marker.barrel,false,receipt)&&receipt.ray.position[0]==expected.position[0],"final ODST destination and full weapon identity published");
            context.muzzleWeapon^=0x10000;OdstPublishMuzzlePalette(123,context,destination,7);
            Check(!g_odstMuzzles.Read(GameTitle::Halo3ODST,7,owner,slot?secondary:primary,3,GetTickCount64(),1000000000,slot,marker.barrel,false,receipt),"weapon swap invalidates stale ODST palette");
        }
    }
    uint32_t unit=0,weapon=0;*reinterpret_cast<uint32_t*>(users+0x3C)=primary^0x10000;
    Check(!OdstCaptureMuzzleOwner(0,0,unit,weapon),"full FP slot and inventory must agree");
    Check(!OdstCaptureMuzzleOwner(1,0,unit,weapon)&&!OdstCaptureMuzzleOwner(0,2,unit,weapon),"foreign player and slot rejected");
}

static uintptr_t lifecycleBase{};
static unsigned bindingScan{},createCalls{},removeCalls{},logged{};
static int missingBinding=-1,ambiguousBinding=-1,failCreate=-1,failEnable=-1,failDisable=-1,failRemove=-1;
static bool ingress=false,quiesced=false,hookExists[6]{},hookEnabled[6]{};
static constexpr uint32_t hookRvas[]{0x3AF230,0x396B7C,0x1604E0,0x160FA0,0x242340,0x37F514};
namespace sig
{
static uintptr_t Find(uintptr_t base,size_t,const char*)
{
    constexpr uint32_t bindings[]{0x139F3C,0x3AF230,0x396B7C,0x1604E0,0x160FA0,0x242340,0x37F514,
        0x1610D4,0x2E9E65,0x2E9E8E,0x39AB60,0x3AE994,0x3AEA59,0x3AE9F7,0x160FF8,0x3AB1CC};
    const unsigned number=bindingScan++/2;Check(number<16,"bounded own ODST binding inventory");
    if(int(number)==missingBinding)return 0;
    return base==lifecycleBase?base+bindings[number]:int(number)==ambiguousBinding?base+0x100:0;
}
}
static int HookIndex(void* target)
{
    for(int i=0;i<6;++i)if(reinterpret_cast<uintptr_t>(target)==lifecycleBase+hookRvas[i])return i;
    Check(false,"only verified ODST optional targets");return -1;
}
static MH_STATUS FixtureCreate(void* target,void* detour,void** original)
{
    const int i=HookIndex(target);++createCalls;
    const void* expected[]{reinterpret_cast<void*>(&OdstMuzzleFireDetour),reinterpret_cast<void*>(&OdstMuzzleAimDetour),
        reinterpret_cast<void*>(&OdstMuzzleQueryDetour),reinterpret_cast<void*>(&OdstMuzzleViewDetour),
        reinterpret_cast<void*>(&OdstMuzzleCameraDetour),reinterpret_cast<void*>(&OdstMuzzleMarkersDetour)};
    Check(detour==expected[i]&&original,"exact ODST hook ABI entry");
    if(i==failCreate)return MH_ERROR_MEMORY_ALLOC;
    hookExists[i]=true;*original=reinterpret_cast<void*>(uintptr_t(0x4000+i*0x100));return MH_OK;
}
static MH_STATUS FixtureEnable(void* target)
{const int i=HookIndex(target);Check(hookExists[i],"created before enable");if(i==failEnable)return MH_ERROR_MEMORY_PROTECT;hookEnabled[i]=true;return MH_OK;}
static MH_STATUS MCCVR_DisableHookForRetirement(void* target)
{const int i=HookIndex(target);if(i==failDisable)return MH_ERROR_MEMORY_PROTECT;hookEnabled[i]=false;return MH_OK;}
static MH_STATUS FixtureRemove(void* target)
{
    const int i=HookIndex(target);++removeCalls;
    Check(!hookEnabled[i]&&quiesced&&!ingress&&!g_odstMuzzle.callbacks,"no ODST trampoline removed before complete drain");
    if(i==failRemove)return MH_ERROR_MEMORY_PROTECT;hookExists[i]=false;return MH_OK;
}
static bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const* originals,size_t count,const std::atomic<uint32_t>& callbacks)
{
    Check(count==7,"all six detours and controller publisher ingress ranges protected");
    const void* expected[]{reinterpret_cast<void*>(&OdstMuzzleFireDetour),reinterpret_cast<void*>(&OdstMuzzleAimDetour),
        reinterpret_cast<void*>(&OdstMuzzleQueryDetour),reinterpret_cast<void*>(&OdstMuzzleViewDetour),
        reinterpret_cast<void*>(&OdstMuzzleCameraDetour),reinterpret_cast<void*>(&OdstMuzzleMarkersDetour)};
    for(int i=0;i<6;++i)Check(functions[i]==expected[i]&&(!hookExists[i]||originals[i]==reinterpret_cast<void*>(uintptr_t(0x4000+i*0x100))),"exact retained detour and trampoline pairs");
    Check(functions[6]==reinterpret_cast<void*>(&PublishOdstIndependentAim)&&!originals[6],"publisher drains before retirement");
    quiesced=!callbacks.load()&&!ingress;return quiesced;
}
#define MH_CreateHook FixtureCreate
#define MH_EnableHook FixtureEnable
#define MH_RemoveHook FixtureRemove
#define LOG(...) (++logged)
#include "../src/dll/odst_muzzle_lifecycle.inl"
#undef MH_CreateHook
#undef MH_EnableHook
#undef MH_RemoveHook
#undef LOG
static void LifecycleTests()
{
    constexpr size_t size=0x400000;auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(image!=nullptr,"bounded lifecycle image");lifecycleBase=reinterpret_cast<uintptr_t>(image);
    const uint32_t edges[][2]{{0x3AF4C5,0x37F514},{0x3AF889,0x3AE8A4},{0x3AEB26,0x396B7C},{0x3AED8A,0x1610D4},
        {0x110719,0x1604E0},{0x160642,0x160FA0},{0x160A58,0x160FA0},{0x1611FF,0x242340}};
    for(const auto& edge:edges){image[edge[0]]=0xE8;*reinterpret_cast<int32_t*>(image+edge[0]+1)=int32_t(edge[1]-edge[0]-5);}
    auto reset=[]
    {
        bindingScan=createCalls=removeCalls=0;quiesced=ingress=false;
        missingBinding=ambiguousBinding=failCreate=failEnable=failDisable=failRemove=-1;
        std::memset(hookExists,0,sizeof(hookExists));std::memset(hookEnabled,0,sizeof(hookEnabled));
        for(auto& target:g_odstMuzzle.targets)target=nullptr;
        g_odstMuzzle.fireOriginal=nullptr;g_odstMuzzle.aimOriginal=nullptr;g_odstMuzzle.queryOriginal=nullptr;
        g_odstMuzzle.viewOriginal=nullptr;g_odstMuzzle.cameraOriginal=nullptr;g_odstMuzzle.markersOriginal=nullptr;
        g_odstMuzzle.enabled=false;g_odstMuzzleBindingsReady=false;g_odstMuzzle.callbacks=0;
    };
    for(int i=0;i<16;++i)
    {
        reset();missingBinding=i;Check(!InstallOdstMuzzle(lifecycleBase,size,7)&&createCalls==0,"missing own proof stays stock");
        reset();ambiguousBinding=i;Check(!InstallOdstMuzzle(lifecycleBase,size,7)&&createCalls==0,"ambiguous own proof stays stock");
    }
    for(int i=0;i<6;++i)
    {
        reset();failCreate=i;Check(!InstallOdstMuzzle(lifecycleBase,size,7)&&!g_odstMuzzle.enabled&&!g_odstMuzzleBindingsReady,"partial create remains disabled");
        Check(RemoveOdstMuzzle(),"partial create cleanup");
        reset();failEnable=i;Check(!InstallOdstMuzzle(lifecycleBase,size,7)&&!g_odstMuzzle.enabled&&!g_odstMuzzleBindingsReady,"partial enable remains disabled");
        Check(RemoveOdstMuzzle(),"partial enable cleanup");
        reset();Check(InstallOdstMuzzle(lifecycleBase,size,7),"all exact hooks installed");failDisable=i;
        Check(!RemoveOdstMuzzle()&&removeCalls==0,"failed disable retains all trampoline dependencies");failDisable=-1;
        Check(RemoveOdstMuzzle(),"disable retry succeeds");
        reset();Check(InstallOdstMuzzle(lifecycleBase,size,7),"all exact hooks installed");failRemove=i;
        Check(!RemoveOdstMuzzle()&&g_odstMuzzle.targets[i],"failed removal retains exact hook for retry");failRemove=-1;
        Check(RemoveOdstMuzzle(),"removal retry succeeds");
    }
    for(const auto& edge:edges)
    {
        reset();image[edge[0]]^=1;Check(!InstallOdstMuzzle(lifecycleBase,size,7)&&createCalls==0,"changed downstream edge prevents adaptation");image[edge[0]]^=1;
    }
    reset();Check(InstallOdstMuzzle(lifecycleBase,size,7),"fixture installed");ingress=true;
    Check(!RemoveOdstMuzzle()&&removeCalls==0,"zero counter with thread in ingress retains code");ingress=false;
    g_odstMuzzle.callbacks=1;Check(!RemoveOdstMuzzle()&&removeCalls==0,"active callback retains code");
    g_odstMuzzle.callbacks=0;Check(RemoveOdstMuzzle()&&!g_odstMuzzleBindingsReady,"drained cleanup clears publication authority");
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"lifecycle fixture released");
}
#include "odst_independent_runtime.inl"
int main()
{
    ShotTests();PublicationTests();
    // Shots from the muzzle: the control asks for the converged point; a seat held at its stop or a turret
    // (SeatMuzzleShotHeld; unit_in_vehicle reads false in a stationary turret) refuses before asking.
    Reset();g_config.gun_barrel_aim=false;g_config.shots_from_muzzle=true;muzzleAimCalls=0;seatHeld=false;
    (void)Shoot();Check(muzzleAimCalls==1&&fireCalls==1,"ODST shots from the muzzle ask for the point (control)");
    Reset();g_config.gun_barrel_aim=false;g_config.shots_from_muzzle=true;muzzleAimCalls=0;seatHeld=true;
    seatHeldCalls=0;expectMuzzle=false;(void)Shoot();
    Check(seatHeldCalls==1&&muzzleAimCalls==0&&fireCalls==1&&*reinterpret_cast<uint32_t*>(unitBytes+0x230)==0x44440007,
        "ODST seat held at its stop or a turret refuses the redirect; native fire forwarded once");
    seatHeld=false;
    Reset(true);g_config.gun_barrel_aim=false;g_config.independent_dual_aim=true;expectMuzzle=false;
    Check(!kEnableOdstIndependentAim,"ODST dual expansion stays disabled after scope correction");
    (void)Shoot();Check(fireCalls==1&&queryCalls==0,"H2/H3 dual option does not adapt ODST firing");
    LifecycleTests();
    std::printf("PASS: %u production ODST muzzle checks (native services stubbed)\n",checks);
}
