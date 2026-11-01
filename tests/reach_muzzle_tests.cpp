#include "../src/common/reach_weapon_defaults.h"
#include "../src/common/visual_aim.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "../src/common/weapon_muzzle.h"
#include "../src/common/native_shot_target_lease.h"
#include "../src/common/exclusive_input.h"
static unsigned checks{};
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
static constexpr uint32_t owner=0x12340001,primary=0x56780002,secondary=0x789A0003,target=0x22220005;
static constexpr uintptr_t imageBase=0x180000000;
static constexpr size_t kReachEngineTlsIndexRva=0;
static uint32_t tlsIndex=0,localUnit=owner;
static bool seated=false,stereo=true,validTracking=true;
static int32_t Player(int32_t){return int32_t(localUnit);}
static bool Seated(int32_t){return seated;}
static struct {
    uintptr_t base=reinterpret_cast<uintptr_t>(&tlsIndex);
    std::atomic<uint32_t> generation{7};std::atomic<bool> armed{true};
    int32_t(*playerUnitByOutputUser)(int32_t)=Player;bool(*unitInVehicle)(int32_t)=Seated;
} g_reachCamera;
static std::atomic<bool> g_vrAim{true},g_enabled{true},g_reachBarrelBindingsReady{true},g_reachCinematicLocked{false},g_reachVehicleShotRedirectEnabled{true};
static struct {bool gun_barrel_aim=true,left_handed=false,shots_from_muzzle=false;} g_config;
// The three shot switches as they are in effect (sight_layer.h); here, the test's own config.
[[maybe_unused]] static bool Sight_ShotsFromMuzzle(){return g_config.shots_from_muzzle;}
[[maybe_unused]] static bool Sight_GunBarrelAim(){return g_config.gun_barrel_aim;}
[[maybe_unused]] static bool Sight_IndependentDualAim(){return false;}
static weapon_muzzle::Store g_reachBarrelMuzzles;
static thread_local bool g_legacyCollisionOwnedQuery=false;
static alignas(8) uint8_t tlsBytes[0x700]{},users[0x53A8]{},unitBytes[0x400]{},replacementBytes[0x400]{},
    primaryBytes[0x400]{},secondaryBytes[0x400]{},targetBytes[0x400]{};
static uint8_t* slots[]{tlsBytes};static uint8_t* currentUnit=unitBytes;
static uint32_t liveHandles[]{owner,primary,secondary,target};
static unsigned char* ReachVehicleObjectData(int32_t handle,uint8_t& kind)
{
    uint8_t* data[]{currentUnit,primaryBytes,secondaryBytes,targetBytes};
    for(int i=0;i<4;++i)if(uint32_t(handle)==liveHandles[i]){kind=i==0?0:i==3?1:2;return data[i];}
    return nullptr;
}
static GameTitle title=GameTitle::HaloReach;
static GameTitle TitleAdapter_GetActiveTitle(){return title;}
// Shots from the muzzle (sight_layer.h): admitted only with the barrel aim off; the converged point itself is
// sight_layer's (aim_solve_tests), so here it keeps the barrel's direction and counts the asks.
static unsigned muzzleAimCalls=0;static bool muzzleAimRefuses=false;
static bool Sight_MuzzleShotsConverge(){return g_config.shots_from_muzzle&&!g_config.gun_barrel_aim;}
static uint32_t muzzleAimUnit=UINT32_MAX;
static bool ReachAimMuzzleShot(uint32_t unit,const float*,const float*,float*,float*)
{++muzzleAimCalls;muzzleAimUnit=unit;return !muzzleAimRefuses;}
static void ReachSightMeasureShot(int32_t,const float*,const float*,const float*,uint8_t,uint8_t){}
static bool VR_IsStereoEnabled(){return stereo;}
struct VrContactTrackingSnapshot
{uint64_t serial=11,referenceEpoch=3;int64_t timeNs=1000000000;struct {bool valid=true;} hands[2];};
static VrContactTrackingSnapshot tracking;
static bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out){out=tracking;return validTracking;}
static int ResolveEquippedWeaponSlot(uint32_t weapon,uint32_t a,uint32_t b,bool,bool)
{return weapon==a?0:weapon==b?1:-1;}
struct BoneMatrix {float scale=1,rotation[9]{1,0,0,0,1,0,0,0,1},translation[3]{};};
struct ReachFpInterpolationContext {
    bool valid=true;uint32_t generation=7,muzzleUnit=owner,muzzleWeapon=primary;
    int interpolationView=0,interpolationSlot=0;uint64_t preparedSerial=11;
    // contactReference is Reach's melee hash of the tracking-space epoch (3 here): never equal to it.
    struct {BoneMatrix aimVisual{},aimNeutral{},rightWrist{};uint64_t contactSerial=11,contactReference=0x9E3779B97F4A7C15ull,contactSpaceEpoch=3;int64_t contactTimeNs=1000000000;bool rightWristValid=true;} targets;
};
static struct {bool armed=true;uint64_t preparedSerial=11;} g_reachFpPairScope;
static uint32_t liveChecksum=0;static int liveCount=0;
static bool ReachReadRenderModelIdentity(uint16_t tag,uint32_t& checksum,int& count)
{Check(tag==123,"Reach native model identity");checksum=liveChecksum;count=liveCount;return true;}
using ReachUnitAdjustFn=void(__fastcall*)(int32_t,float*,float*,const float*,const float*,const float*,uint8_t,uint8_t,uint8_t,uint32_t);
static ReachUnitAdjustFn g_origReachUnitAdjust=nullptr;
static void* g_reachUnitAdjustTarget=reinterpret_cast<void*>(imageBase+0x484F24);
static thread_local uintptr_t caller=0;
// THE LOCAL PLAYER'S FIRE (shot_lag_logic.h FireMark; game.cpp t_reachFire): opened and restored by the fire.
#include "../src/common/shot_lag_logic.h"
static thread_local int8_t t_reachFire=shot_lag::kFireNone;
// Flat parity's per-fire eye (game.cpp t_reachFireEye): opened empty by each fire, restored at its exit.
static thread_local shot_lag::FireEyeScope t_reachFireEye{};
#define _ReturnAddress() reinterpret_cast<void*>(::caller)
#define __readgsqword(offset) reinterpret_cast<uint64_t>(::slots)
#include "../src/dll/reach_muzzle_ownership.inl"
#include "../src/dll/reach_muzzle_publication.inl"
#include "../src/dll/reach_muzzle_shots.inl"
#undef __readgsqword
#undef _ReturnAddress
static bool expectMuzzle=true,queryFault=false,preflightFault=false,fireFault=false,markerFault=false,nested=false,
    omitView=false,replaceStorage=false,changeGeneration=false,changeOwner=false,nativeChangesTarget=false,invalidTarget=false,nonfiniteTarget=false;
static unsigned fireCalls=0,queryCalls=0,markerCalls=0,aimCalls=0,viewCalls=0;
static int16_t markerCount=1;
static void __fastcall NativeView(uint32_t unit,uint8_t flags,float*,float* point,float* direction,float* camera)
{++viewCalls;Check(unit==owner&&flags==0,"six-argument Reach native view");point[0]=21;direction[0]=1;camera[0]=22;}
static void __fastcall NativeQuery(int32_t input,uint8_t flags,float* direction,int16_t zoom,float* control,void* out)
{
    ++queryCalls;Check(input==2&&zoom==3,"native input/zoom context retained");
    if(queryFault)RaiseException(0xE0421111,0,0,nullptr);
    const bool active=g_reachBarrelShot.query;float point[3]{},view[3]{},camera[3]{};
    const auto previous=caller;caller=imageBase+0x10EACD;
    if(!omitView)ReachMuzzleViewDetour(owner,0,direction,point,view,camera);
    if(active&&!omitView)Check(g_legacyCollisionOwnedQuery&&point[1]==2.5f&&camera[0]==2&&view[1]==1,"Reach query from clipped visible barrel");
    if(flags&2){caller=imageBase+0x10EEF1;ReachMuzzleViewDetour(owner,0,direction,point,view,camera);}
    caller=previous;std::memset(out,0,0x28);auto* words=static_cast<uint32_t*>(out);
    words[0]=1;words[1]=words[3]=UINT32_MAX;words[2]=invalidTarget?0xABCD0005:target;
    auto* values=static_cast<float*>(out);values[4]=.25f;values[5]=.75f;if(nonfiniteTarget)values[8]=NAN;
    control[0]=.1f;control[1]=.2f;control[2]=.3f;
    if(replaceStorage)currentUnit=replacementBytes;
    if(changeGeneration)g_reachCamera.generation=8;
    if(changeOwner)*reinterpret_cast<uint32_t*>(primaryBytes+0x32C)=owner^0x10000;
}
static void __fastcall NativeAim(int32_t unit,float* point,float* direction,const float* velocity,const float* offset,
    const float* camera,uint8_t project,uint8_t use,uint8_t collision,uint32_t simulation)
{
    ++aimCalls;Check(uint32_t(unit)==owner&&velocity&&!offset&&!camera&&!project&&!use&&collision==1&&simulation==0xFA,
        "Reach native ten-argument obstruction ABI and real simulation value");
    if(g_legacyCollisionOwnedQuery&&preflightFault)RaiseException(0xE0422222,0,0,nullptr);
    Check(point[0]==2&&point[2]==4&&direction[1]==1,"native obstruction receives authored barrel");
    const_cast<float*>(velocity)[0]=11;point[1]=2.5f;
}
static int16_t __fastcall NativeMarkers(uint32_t object,uint32_t name,void* data,int16_t capacity,uint8_t original,uint8_t interpolate)
{
    ++markerCalls;Check((object==primary||object==secondary)&&name==0x101&&capacity==64&&original==0xAA&&interpolate==0xBB,"Reach marker ABI");
    if(markerFault)RaiseException(0xE0423333,0,0,nullptr);std::memset(data,0x5A,0x70);return markerCount;
}
static void __fastcall NativeFire(uint32_t weapon,int16_t barrel,void* data,uint8_t simulation)
{
    ++fireCalls;Check(barrel==0&&data==reinterpret_cast<void*>(0x1234)&&simulation==0xFA,"Reach four-argument void fire ABI");
    const auto previous=caller;alignas(float) uint8_t marker[0x70]{};caller=imageBase+0x4C2AA0;
    Check(ReachMuzzleMarkersDetour(weapon,0x101,marker,64,0xAA,0xBB)==markerCount,"native signed marker count retained");
    if(expectMuzzle)
    {
        Check(reinterpret_cast<float*>(marker+0x60)[1]==2.5f&&reinterpret_cast<float*>(marker+0x3C)[1]==1&&
            reinterpret_cast<float*>(marker+0x54)[2]==1,"actual barrel marker retains roll and clipping");
        for(size_t i=0;i<0x3C;++i)Check(marker[i]==0x5A,"local marker and world scale retained");
        for(size_t i=0x6C;i<0x70;++i)Check(marker[i]==0x5A,"native marker flags retained");
        Check(*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)==target,"native direct homing sees own per-shot target");
    }
    else for(auto value:marker)Check(value==0x5A,"refused barrel leaves native marker unchanged");
    float point[3]{},direction[3]{0,1,0},velocity[3]{},camera[3]{},cameraDirection[3]{};
    Check(ReachApplyBarrelAim(owner,point,direction,velocity,1,simulation)==expectMuzzle,"shared core hook only claims owned barrel shot");
    if(expectMuzzle)Check(velocity[0]==11,"real native velocity output retained");
    caller=imageBase+0x10F954;ReachMuzzleViewDetour(owner,0,direction,point,cameraDirection,camera);
    if(expectMuzzle)Check(point[0]==2&&point[1]==2.5f&&cameraDirection[1]==1&&camera[0]==2,"later native assist shares barrel ray");
    // The crosshair re-aim at this assist: inside an open fire (never kFireNone), and a barrel shot's own line.
    Check(t_reachFire!=shot_lag::kFireNone,"the fire is open while its assist runs");
    {
        float eye[3]{};
        if(!nested||weapon==primary)
            Check(t_reachFireEye.For(int32_t(owner),eye)==shot_lag::FireEye::NotRead,
                "each fire opens its fire-eye scope empty: no earlier fire's eye is reused");
        const float mine[3]{1,2,3};t_reachFireEye.Store(int32_t(owner),true,mine);
    }
    Check(g_reachBarrelShot.active==expectMuzzle,"the barrel's own line owns exactly the barrel shot's assist");
    if(nested&&weapon==primary)
    {
        const auto request=g_reachBarrelRequest;ReachMuzzleFireDetour(secondary,0,data,simulation);
        Check(g_reachBarrelRequest.weapon==primary&&g_reachBarrelRequest.lease==request.lease&&g_reachBarrelShot.active,"nested fire restores outer scope");
        float eye[3]{};
        Check(t_reachFireEye.For(int32_t(owner),eye)==shot_lag::FireEye::Measured&&eye[2]==3,
            "a nested fire restores the outer fire's eye");
    }
    if(nativeChangesTarget)*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)=0xABCD0006;
    caller=previous;if(fireFault)RaiseException(0xE0424444,0,0,nullptr);
}
static void Reset(bool dual=false)
{
    std::memset(unitBytes,0,sizeof(unitBytes));currentUnit=unitBytes;localUnit=owner;liveHandles[1]=primary;
    unitBytes[0x34A]=0;unitBytes[0x34B]=dual?1:0xFF;
    *reinterpret_cast<uint32_t*>(unitBytes+0x350)=primary;*reinterpret_cast<uint32_t*>(unitBytes+0x354)=secondary;
    *reinterpret_cast<uint32_t*>(unitBytes+0x390)=UINT32_MAX;*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)=0x44440007;
    std::memcpy(replacementBytes,unitBytes,sizeof(unitBytes));
    for(auto* weapon:{primaryBytes,secondaryBytes}){*reinterpret_cast<uint32_t*>(weapon+0x32C)=owner;weapon[0x1A9]=1;*reinterpret_cast<uint32_t*>(weapon+0x1B4)=owner;}
    *reinterpret_cast<uint8_t**>(tlsBytes+0x6A0)=users;*reinterpret_cast<uint32_t*>(users+0x3C)=primary;*reinterpret_cast<uint32_t*>(users+0x2978+0x3C)=secondary;
    g_reachCamera.base=reinterpret_cast<uintptr_t>(&tlsIndex);g_reachCamera.generation=7;g_reachCamera.armed=true;g_reachVehicleShotRedirectEnabled=true;
    g_reachUnitAdjustTarget=reinterpret_cast<void*>(imageBase+0x484F24);g_origReachUnitAdjust=NativeAim;
    seated=false;stereo=validTracking=true;g_reachCinematicLocked=false;title=GameTitle::HaloReach;
    g_config={};g_config.gun_barrel_aim=true;tracking={};g_reachFpPairScope={};g_reachFpPairScope.armed=true;g_reachFpPairScope.preparedSerial=11;
    g_reachBarrelBindingsReady=true;g_enabled=g_vrAim=true;exclusive_input::active=false;
    auto& f=g_reachBarrel;f.base=imageBase;f.generation=7;f.installedAtMs=1;f.enabled=true;f.faulted=false;f.callbacks=0;
    f.fireOriginal=NativeFire;f.queryOriginal=NativeQuery;f.viewOriginal=NativeView;f.markersOriginal=NativeMarkers;
    g_reachBarrelShot={};g_reachBarrelRequest={};g_reachBarrelCapture={};g_reachBarrelQueryContext={owner,7,2,0,3,GetTickCount64()};
    g_legacyCollisionOwnedQuery=false;fireCalls=queryCalls=markerCalls=aimCalls=viewCalls=0;markerCount=1;expectMuzzle=true;
    queryFault=preflightFault=fireFault=markerFault=nested=omitView=replaceStorage=changeGeneration=changeOwner=nativeChangesTarget=invalidTarget=nonfiniteTarget=false;
    for(uint8_t slot=0;slot<2;++slot){weapon_muzzle::Palette p{};p.barrels[0]={GameTitle::HaloReach,7,owner,slot?secondary:primary,1,3,11,GetTickCount64(),1000000000,slot,0,false,{{2,3,4},{0,1,0},{0,0,1}}};
        Check(g_reachBarrelMuzzles.Publish(GameTitle::HaloReach,slot,p),"Reach fixture receipt");}
}
static void Shoot(){ReachMuzzleFireDetour(primary,0,reinterpret_cast<void*>(0x1234),0xFA);}
static bool FaultingShot(){__try{Shoot();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
static void ShotTests()
{
    Reset();Shoot();Check(fireCalls==1&&queryCalls==1&&markerCalls==1&&aimCalls==2&&*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)==0x44440007,"single native fire and exact target restoration");
    Check(t_reachFire==shot_lag::kFireNone,"the fire's exit restores the local-fire tag");
    {
        float eye[3]{};
        Check(t_reachFireEye.For(int32_t(owner),eye)==shot_lag::FireEye::NotRead,"the fire's exit restores its eye scope");
    }
    Reset(true);nested=true;g_reachBarrelQueryContext.flags=2;Shoot();Check(fireCalls==2&&queryCalls==2&&!g_reachBarrelShot.active&&*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)==0x44440007,"nested native target leases restore LIFO");
    for(int reason=0;reason<26;++reason)
    {
        Reset();expectMuzzle=false;
        switch(reason){
        case 0:markerCount=0;break;case 1:markerCount=2;break;case 2:markerCount=-1;break;
        case 3:g_reachBarrel.enabled=false;break;case 4:g_reachBarrel.faulted=true;break;case 5:g_reachBarrel.generation=8;break;
        case 6:g_config.gun_barrel_aim=false;break;case 7:tracking.referenceEpoch=4;break;case 8:g_config.left_handed=true;break;
        case 9:exclusive_input::active=true;break;case 10:g_reachCinematicLocked=true;break;case 11:seated=true;break;
        case 12:g_reachBarrelQueryContext.sampleMs=GetTickCount64()-200;break;case 13:unitBytes[0x34A]=4;break;case 14:unitBytes[0x34B]=4;break;
        case 15:*reinterpret_cast<uint32_t*>(primaryBytes+0x32C)=owner^0x10000;break;case 16:g_reachCamera.armed=false;break;
        case 17:liveHandles[1]^=0x10000;break;case 18:invalidTarget=true;break;case 19:nonfiniteTarget=true;break;
        case 20:omitView=true;break;case 21:replaceStorage=true;break;case 22:changeGeneration=true;break;case 23:changeOwner=true;break;
        case 24:g_reachBarrelQueryContext.sampleMs=GetTickCount64()+1000;break;case 25:title=GameTitle::Halo3;break;}
        Shoot();Check(fireCalls==1&&!g_reachBarrelShot.active&&!g_reachBarrel.callbacks,"failed optional ownership never replays native fire");
    }
    Reset();queryFault=true;expectMuzzle=false;Shoot();Check(g_reachBarrel.faulted&&!g_legacyCollisionOwnedQuery,"query exception isolated with collision ownership restored");
    Reset();preflightFault=true;expectMuzzle=false;Shoot();Check(g_reachBarrel.faulted&&!g_legacyCollisionOwnedQuery,"native preflight failure stays feature-local");
    Reset();markerFault=true;Check(FaultingShot()&&fireCalls==1&&!g_reachBarrel.callbacks,"native marker exception propagates without replay");
    Reset();fireFault=true;Check(FaultingShot()&&fireCalls==1&&*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)==0x44440007,"native fire exception restores lease once");
    Reset();nativeChangesTarget=true;Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x2C0)==0xABCD0006,"new native target survives lease release");
    Reset();g_reachBarrelQueryContext={};float direction[3]{0,1,0},control[3]{};uint8_t output[0x28]{};
    ReachMuzzleQueryDetour(2,0,direction,3,control,output);Check(g_reachBarrelQueryContext.unit==owner,"ordinary native query captures actual view owner");
    g_reachBarrelQueryContext={};ReachMuzzleQueryDetour(2,1,direction,3,control,output);Check(!g_reachBarrelQueryContext.sampleMs,"fallback query cannot seed shot context");
    g_reachBarrelQueryContext={owner,7,2,0,3,GetTickCount64()};std::thread worker([]{Check(!g_reachBarrelQueryContext.sampleMs,"native query context stays on native thread");});worker.join();
}
static void PublicationTests()
{
    Reset();BoneMatrix destination[64];for(auto& n:destination){n.scale=2;n.translation[0]=2;n.translation[1]=3;n.translation[2]=4;}
    for(const auto& marker:weapon_muzzle::kMarkers)
    {
        if(marker.title!=GameTitle::HaloReach)continue;liveChecksum=uint32_t(marker.identity);liveCount=marker.nodeCount;
        ReachFpInterpolationContext context;ReachPublishMuzzlePalette(123,context,destination);weapon_muzzle::Receipt sample{};weapon_muzzle::Ray expected{};
        Check(weapon_muzzle::Transform(marker,2,destination[marker.node].rotation,destination[marker.node].translation,expected),"Reach authored marker fixture");
        Check(g_reachBarrelMuzzles.Read(GameTitle::HaloReach,7,owner,primary,3,GetTickCount64(),1000000000,0,marker.barrel,false,sample)&&sample.ray.position[0]==expected.position[0],"actual committed Reach node publishes correct barrel");
        // The lane reads the receipt back with the tracking snapshot's epoch (3). Published under Reach's melee
        // hash of that epoch, as it was, no read could ever match: the lane refused every shot it was asked for.
        Check(!g_reachBarrelMuzzles.Read(GameTitle::HaloReach,7,owner,primary,0x9E3779B97F4A7C15ull,GetTickCount64(),1000000000,0,marker.barrel,false,sample),
            "the receipt's space is the tracking-space epoch, not the melee hash of it");
        context.muzzleWeapon^=0x10000;ReachPublishMuzzlePalette(123,context,destination);
        Check(!g_reachBarrelMuzzles.Read(GameTitle::HaloReach,7,owner,primary,3,GetTickCount64(),1000000000,0,marker.barrel,false,sample),"replacement weapon invalidates old marker");
    }
    uint32_t unit=0,weapon=0;*reinterpret_cast<uint32_t*>(users+0x3C)=primary^0x10000;
    Check(!ReachCaptureMuzzleOwner(0,0,unit,weapon),"FP slot must match complete current inventory handle");
    Check(!ReachCaptureMuzzleOwner(1,0,unit,weapon)&&!ReachCaptureMuzzleOwner(0,2,unit,weapon),"foreign view/slot refused");
}

static uintptr_t lifecycleBase{};
static unsigned bindingScan{},createCalls{},removeCalls{},logged{};
static int missingBinding=-1,ambiguousBinding=-1,failCreate=-1,failEnable=-1,failDisable=-1,failRemove=-1;
static bool ingress=false,quiesced=false,hookExists[4]{},hookEnabled[4]{};
static constexpr uint32_t hookRvas[]{0x4C2710,0x10E970,0x10FA74,0x47044C};
namespace sig
{
static uintptr_t Find(uintptr_t base,size_t,const char*)
{
    constexpr uint32_t bindings[]{0x4C2710,0x10E970,0x10FA74,0x47044C,0x484F24,
        0x2B1234,0x48B3D0,0x4C2F57,0x48379E,0x4C2A4F,0x4C31B0,0x4BE0E8,0x106F04,0x10F8F4};
    const unsigned number=bindingScan++/2;Check(number<14,"bounded own Reach binding inventory");
    if(int(number)==missingBinding)return 0;
    return base==lifecycleBase?base+bindings[number]:int(number)==ambiguousBinding?base+0x100:0;
}
}
static int HookIndex(void* target)
{
    for(int i=0;i<4;++i)if(reinterpret_cast<uintptr_t>(target)==lifecycleBase+hookRvas[i])return i;
    Check(false,"only verified Reach optional targets");return -1;
}
static MH_STATUS FixtureCreate(void* target,void* detour,void** original)
{
    const int i=HookIndex(target);++createCalls;
    const void* expected[]{reinterpret_cast<void*>(&ReachMuzzleFireDetour),
        reinterpret_cast<void*>(&ReachMuzzleQueryDetour),reinterpret_cast<void*>(&ReachMuzzleViewDetour),
        reinterpret_cast<void*>(&ReachMuzzleMarkersDetour)};
    Check(detour==expected[i]&&original,"exact Reach hook ABI entry");
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
    Check(!hookEnabled[i]&&quiesced&&!ingress&&!g_reachBarrel.callbacks,"no Reach trampoline removed before complete drain");
    if(i==failRemove)return MH_ERROR_MEMORY_PROTECT;hookExists[i]=false;return MH_OK;
}
static bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const* originals,size_t count,const std::atomic<uint32_t>& callbacks)
{
    Check(count==4,"all four detour ingress ranges protected");
    const void* expected[]{reinterpret_cast<void*>(&ReachMuzzleFireDetour),
        reinterpret_cast<void*>(&ReachMuzzleQueryDetour),reinterpret_cast<void*>(&ReachMuzzleViewDetour),
        reinterpret_cast<void*>(&ReachMuzzleMarkersDetour)};
    for(int i=0;i<4;++i)Check(functions[i]==expected[i]&&(!hookExists[i]||originals[i]==reinterpret_cast<void*>(uintptr_t(0x4000+i*0x100))),"exact retained detour and trampoline pairs");
    quiesced=!callbacks.load()&&!ingress;return quiesced;
}
#define MH_CreateHook FixtureCreate
#define MH_EnableHook FixtureEnable
#define MH_RemoveHook FixtureRemove
#define LOG(...) (++logged)
#include "../src/dll/reach_muzzle_lifecycle.inl"
#undef MH_CreateHook
#undef MH_EnableHook
#undef MH_RemoveHook
#undef LOG
static void LifecycleTests()
{
    constexpr size_t size=0x500000;auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(image!=nullptr,"bounded lifecycle image");lifecycleBase=reinterpret_cast<uintptr_t>(image);
    const uint32_t edges[][2]{{0x4C2A9B,0x47044C},{0x4C303A,0x484F24},{0x4C3293,0x10FB94},
        {0x5EF44,0x10E970},{0x10EAC8,0x10FA74},{0x10EEEC,0x10FA74},{0x10F94F,0x10FA74},{0x10F583,0x10F8F4}};
    for(const auto& edge:edges){image[edge[0]]=0xE8;*reinterpret_cast<int32_t*>(image+edge[0]+1)=int32_t(edge[1]-edge[0]-5);}
    auto reset=[]
    {
        g_origReachUnitAdjust=NativeAim;g_reachUnitAdjustTarget=reinterpret_cast<void*>(imageBase+0x484F24);g_reachVehicleShotRedirectEnabled=true;
        bindingScan=createCalls=removeCalls=0;quiesced=ingress=false;
        missingBinding=ambiguousBinding=failCreate=failEnable=failDisable=failRemove=-1;
        std::memset(hookExists,0,sizeof(hookExists));std::memset(hookEnabled,0,sizeof(hookEnabled));
        for(auto& target:g_reachBarrel.targets)target=nullptr;
        g_reachBarrel.fireOriginal=nullptr;g_reachBarrel.queryOriginal=nullptr;
        g_reachBarrel.viewOriginal=nullptr;g_reachBarrel.markersOriginal=nullptr;
        g_reachBarrel.enabled=false;g_reachBarrelBindingsReady=false;g_reachBarrel.callbacks=0;
    };
    reset();g_reachVehicleShotRedirectEnabled=false;
    Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"disabled shared core helper cannot authorize muzzle");
    for(int i=0;i<14;++i)
    {
        reset();missingBinding=i;Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"missing own proof stays stock");
        reset();ambiguousBinding=i;Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"ambiguous own proof stays stock");
    }
    for(int i=0;i<4;++i)
    {
        reset();failCreate=i;Check(!InstallReachMuzzle(lifecycleBase,size,7)&&!g_reachBarrel.enabled&&!g_reachBarrelBindingsReady,"partial create remains disabled");
        Check(RemoveReachMuzzle(),"partial create cleanup");
        reset();failEnable=i;Check(!InstallReachMuzzle(lifecycleBase,size,7)&&!g_reachBarrel.enabled&&!g_reachBarrelBindingsReady,"partial enable remains disabled");
        Check(RemoveReachMuzzle(),"partial enable cleanup");
        reset();Check(InstallReachMuzzle(lifecycleBase,size,7),"all exact hooks installed");failDisable=i;
        Check(!RemoveReachMuzzle()&&removeCalls==0,"failed disable retains all trampoline dependencies");failDisable=-1;
        Check(RemoveReachMuzzle(),"disable retry succeeds");
        reset();Check(InstallReachMuzzle(lifecycleBase,size,7),"all exact hooks installed");failRemove=i;
        Check(!RemoveReachMuzzle()&&g_reachBarrel.targets[i],"failed removal retains exact hook for retry");failRemove=-1;
        Check(RemoveReachMuzzle(),"removal retry succeeds");
    }
    for(const auto& edge:edges)
    {
        reset();image[edge[0]]^=1;Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"changed downstream edge prevents adaptation");image[edge[0]]^=1;
    }
    // THE ORDER THE GAME INSTALLS IN. The proof is taken, THEN the core hooks the unit-adjust, whose head (binding 4,
    // +0x484F24) reads our jump from then on. Proving after the hook is what logged "missing/ambiguous binding
    // +484F24" in every Reach session; the preflight is what the lane installs on now.
    reset();g_reachMuzzleImageProof={};ReachMuzzlePreflight(lifecycleBase,size,7);
    Check(g_reachMuzzleImageProof.proven&&bindingScan==28,"preflight proves all fourteen patterns and their uniqueness");
    bindingScan=0;missingBinding=4;
    Check(InstallReachMuzzle(lifecycleBase,size,7)&&bindingScan==0&&createCalls==4,
        "the proof taken before the unit-adjust hook authorises the lane; nothing is read again through the hook");
    missingBinding=-1;Check(RemoveReachMuzzle(),"preflight install cleanup");
    reset();g_reachMuzzleImageProof={};missingBinding=4;
    Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"control: a proof read after the hook sees the jump and refuses");
    reset();g_reachMuzzleImageProof={};ReachMuzzlePreflight(lifecycleBase,size,6);bindingScan=0;missingBinding=4;
    Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"another generation's proof is not reused");
    reset();g_reachMuzzleImageProof={};ReachMuzzlePreflight(lifecycleBase+0x1000,size,7);bindingScan=0;missingBinding=4;
    Check(!InstallReachMuzzle(lifecycleBase,size,7)&&createCalls==0,"another image's proof is not reused");
    reset();g_reachMuzzleImageProof={};missingBinding=9;ReachMuzzlePreflight(lifecycleBase,size,7);
    missingBinding=-1;bindingScan=0;
    Check(!g_reachMuzzleImageProof.proven&&g_reachMuzzleImageProof.failedRva==0x4C2A4F&&!g_reachMuzzleImageProof.failedEdge,
        "a failed preflight names the binding");
    Check(!InstallReachMuzzle(lifecycleBase,size,7)&&bindingScan==0&&createCalls==0,"a failed preflight is not retried behind the hooks");
    reset();g_reachMuzzleImageProof={};image[0x4C303A]^=1;ReachMuzzlePreflight(lifecycleBase,size,7);image[0x4C303A]^=1;
    Check(!g_reachMuzzleImageProof.proven&&g_reachMuzzleImageProof.failedEdge&&g_reachMuzzleImageProof.failedRva==0x4C303A,
        "a changed edge at the preflight is named as an edge");
    g_reachMuzzleImageProof={};
    reset();Check(InstallReachMuzzle(lifecycleBase,size,7),"fixture installed");ingress=true;
    Check(!RemoveReachMuzzle()&&removeCalls==0,"zero counter with thread in ingress retains code");ingress=false;
    g_reachBarrel.callbacks=1;Check(!RemoveReachMuzzle()&&removeCalls==0,"active callback retains code");
    g_reachBarrel.callbacks=0;Check(RemoveReachMuzzle()&&!g_reachBarrelBindingsReady,"drained cleanup clears publication authority");
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"lifecycle fixture released");
}
int main()
{
    ShotTests();PublicationTests();LifecycleTests();
    std::printf("PASS: %u production Reach muzzle checks (native services stubbed)\n",checks);
}
