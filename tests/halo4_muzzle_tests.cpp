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
static uint32_t tlsIndex=0,*g_halo4EngineTlsIndex=&tlsIndex,localUnit=owner;
static bool seated=false,stereo=true,validTracking=true,cinematic=false;
static struct {std::atomic<uint32_t> generation{7};std::atomic<bool> armed{true};} g_halo4Camera;
static struct {std::atomic<bool> ready{true};} g_halo4VehicleInput;
static std::atomic<bool> g_vrAim{true},g_enabled{true};
static struct {bool gun_barrel_aim=true,left_handed=false;} g_config;
static thread_local bool g_halo4MuzzleOwnedQuery=false;
static alignas(8) uint8_t tlsBytes[0x700]{},users[0x5F48]{},unitBytes[0x700]{},replacementBytes[0x700]{},
    primaryBytes[0x700]{},secondaryBytes[0x700]{},targetBytes[0x700]{},table[0x80]{},entries[6*0x18]{};
static uint8_t* slots[]{tlsBytes};
template<class T> static void Put(uint8_t* bytes,size_t at,T value){std::memcpy(bytes+at,&value,sizeof(value));}
#include "../src/common/halo4_vehicle_input_logic.h"
static bool Halo4ReadVehicleInput(Halo4VehicleInputState& out)
{if(!g_halo4VehicleInput.ready)return false;out.unit=localUnit;out.seated=seated;return true;}
static CinematicControlState ReadHalo4CinematicControl()
{return cinematic?CinematicControlState::Unknown:CinematicControlState::PlayerControlled;}
static GameTitle title=GameTitle::Halo4;
static GameTitle TitleAdapter_GetActiveTitle(){return title;}
static bool VR_IsStereoEnabled(){return stereo;}
struct VrContactTrackingSnapshot
{uint64_t serial=11,referenceEpoch=3;int64_t timeNs=1000000000;struct {bool valid=true;} hands[2];};
static VrContactTrackingSnapshot tracking;
static bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out){out=tracking;return validTracking;}
static int ResolveEquippedWeaponSlot(uint32_t weapon,uint32_t a,uint32_t b,bool,bool)
{return weapon==a?0:weapon==b?1:-1;}
struct BoneMatrix {float scale=1,rotation[9]{1,0,0,0,1,0,0,0,1},translation[3]{};};
static constexpr int kHalo4FirstPersonBankTransforms=80;
static bool pairCurrent=true;
static struct {uint32_t generation=7;uint64_t preparedSerial=11;
    struct {uint64_t serial=11,referenceEpoch=3;int64_t timeNs=1000000000;} contactFrames[2];
} g_halo4FloatingPair;
static bool Halo4FloatingPairMatchesCurrent(){return pairCurrent;}
static thread_local uintptr_t caller=0;
#define _ReturnAddress() reinterpret_cast<void*>(::caller)
#define __readgsqword(offset) reinterpret_cast<uint64_t>(::slots)
// T-2 sparse shot events: recording is never active under test, so the gate
// stays closed (exactly the gate-off production path) and the shared publisher
// (defined in game.cpp) is an inert stand-in here.
bool Telemetry_WeaponEventsAccepting() noexcept {return false;}
void PublishShotDiagnostic(GameTitle,uint32_t,uint32_t,uint32_t,uint8_t,uint8_t,
    bool,bool,bool,bool,const float*,const float*) noexcept {}
#include "../src/dll/halo4_muzzle_ownership.inl"
#include "../src/dll/halo4_muzzle_publication.inl"
#include "../src/dll/halo4_muzzle_shots.inl"
#undef __readgsqword
#undef _ReturnAddress
static bool expectMuzzle=true,queryFault=false,preflightFault=false,fireFault=false,markerFault=false,nested=false,
    omitView=false,replaceStorage=false,changeGeneration=false,changeOwner=false,nativeChangesTarget=false,invalidTarget=false,nonfiniteTarget=false;
static unsigned fireCalls=0,queryCalls=0,markerCalls=0,aimCalls=0,viewCalls=0;
static int16_t markerCount=1;
static void __fastcall NativeView(uint32_t unit,uint8_t flags,uintptr_t,float* point,float* direction,float* camera)
{++viewCalls;Check(unit==owner&&flags==0,"six-argument Halo4 native view");point[0]=21;direction[0]=1;camera[0]=22;}
static void __fastcall NativeQuery(int32_t input,uint8_t flags,float* direction,int16_t zoom,float* control,void* out)
{
    ++queryCalls;Check(input==2&&zoom==3,"native input/zoom context retained");
    if(queryFault)RaiseException(0xE0421111,0,0,nullptr);
    const bool active=g_halo4BarrelShot.query;float point[3]{},view[3]{},camera[3]{};
    const auto previous=caller;caller=imageBase+0x1DB9A6;
    if(!omitView)Halo4MuzzleViewDetour(owner,0,0,point,view,camera);
    if(active&&!omitView)Check(g_halo4MuzzleOwnedQuery&&point[1]==2.5f&&camera[0]==2&&view[1]==1,"Halo4 query from clipped visible barrel");
    if(flags&2){caller=imageBase+0x1DBDB6;Halo4MuzzleViewDetour(owner,0,0,point,view,camera);}
    caller=previous;std::memset(out,0,0x28);auto* words=static_cast<uint32_t*>(out);
    words[0]=1;words[1]=words[3]=UINT32_MAX;words[2]=invalidTarget?0xABCD0005:target;
    auto* values=static_cast<float*>(out);values[4]=.25f;values[5]=.75f;if(nonfiniteTarget)values[8]=NAN;
    control[0]=.1f;control[1]=.2f;control[2]=.3f;
    if(replaceStorage)Put(entries,0x18+0x10,replacementBytes);
    if(changeGeneration)g_halo4Camera.generation=8;
    if(changeOwner)*reinterpret_cast<uint32_t*>(primaryBytes+0x624)=owner^0x10000;
}
static void __fastcall NativeAim(uint32_t unit,float* point,float* direction,float* velocity,const float* offset,
    const float* camera,uint8_t project,uint8_t use,uint32_t simulation)
{
    ++aimCalls;Check(uint32_t(unit)==owner&&velocity&&!offset&&!camera&&!project&&!use&&simulation==0xFA,
        "Halo4 native nine-argument obstruction ABI and real simulation value");
    if(g_halo4MuzzleOwnedQuery&&preflightFault)RaiseException(0xE0422222,0,0,nullptr);
    Check(point[0]==2&&point[2]==4&&direction[1]==1,"native obstruction receives authored barrel");
    const_cast<float*>(velocity)[0]=11;point[1]=2.5f;
}
static int16_t __fastcall NativeMarkers(uint32_t object,uint32_t name,void* data,int16_t capacity,uint8_t original,uint8_t firstPerson,uint8_t interpolate)
{
    ++markerCalls;Check((object==primary||object==secondary)&&name==0x101&&capacity==64&&original==0xAA&&firstPerson==0xCC&&interpolate==0xBB,"Halo4 marker ABI");
    if(markerFault)RaiseException(0xE0423333,0,0,nullptr);std::memset(data,0x5A,0x70);return markerCount;
}
static void __fastcall NativeFire(uint32_t weapon,int16_t barrel,void* data,uint8_t simulation)
{
    ++fireCalls;Check(barrel==0&&data==reinterpret_cast<void*>(0x1234)&&simulation==0xFA,"Halo4 four-argument void fire ABI");
    const auto previous=caller;alignas(float) uint8_t marker[0x70]{};caller=imageBase+0x6179B7;
    Check(Halo4MuzzleMarkersDetour(weapon,0x101,marker,64,0xAA,0xCC,0xBB)==markerCount,"native signed marker count retained");
    if(expectMuzzle)
    {
        Check(reinterpret_cast<float*>(marker+0x60)[1]==2.5f&&reinterpret_cast<float*>(marker+0x3C)[1]==1&&
            reinterpret_cast<float*>(marker+0x54)[2]==1,"actual barrel marker retains roll and clipping");
        for(size_t i=0;i<0x3C;++i)Check(marker[i]==0x5A,"local marker and world scale retained");
        for(size_t i=0x6C;i<0x70;++i)Check(marker[i]==0x5A,"native marker flags retained");
        Check(*reinterpret_cast<uint32_t*>(unitBytes+0x5B0)==target,"native direct homing sees own per-shot target");
    }
    else for(auto value:marker)Check(value==0x5A,"refused barrel leaves native marker unchanged");
    float point[3]{},direction[3]{0,1,0},velocity[3]{},camera[3]{},cameraDirection[3]{};
    if(expectMuzzle){caller=imageBase+0x617E95;
        Halo4MuzzleAimDetour(owner,point,direction,velocity,camera,cameraDirection,1,1,simulation);}
    if(expectMuzzle)Check(velocity[0]==11,"real native velocity output retained");
    caller=imageBase+0x1DC8C4;Halo4MuzzleViewDetour(owner,0,0,point,cameraDirection,camera);
    if(expectMuzzle)Check(point[0]==2&&point[1]==2.5f&&cameraDirection[1]==1&&camera[0]==2,"later native assist shares barrel ray");
    if(nested&&weapon==primary)
    {
        const auto request=g_halo4BarrelRequest;Halo4MuzzleFireDetour(secondary,0,data,simulation);
        Check(g_halo4BarrelRequest.weapon==primary&&g_halo4BarrelRequest.lease==request.lease&&g_halo4BarrelShot.active,"nested fire restores outer scope");
    }
    if(nativeChangesTarget)*reinterpret_cast<uint32_t*>(unitBytes+0x5B0)=0xABCD0006;
    caller=previous;if(fireFault)RaiseException(0xE0424444,0,0,nullptr);
}
static void Reset(bool dual=false)
{
    std::memset(unitBytes,0,sizeof(unitBytes));localUnit=owner;tlsIndex=0;
    unitBytes[0x63A]=0;unitBytes[0x63B]=dual?1:0xFF;
    Put(unitBytes,0x640,primary);Put(unitBytes,0x644,secondary);Put(unitBytes,0x694,UINT32_MAX);
    Put(unitBytes,0x5B0,uint32_t(0x44440007));std::memcpy(replacementBytes,unitBytes,sizeof(unitBytes));
    for(auto* weapon:{primaryBytes,secondaryBytes}){Put(weapon,0x624,owner);weapon[0x471]=1;Put(weapon,0x480,owner);}
    Put(tlsBytes,0x6A0,users);Put(tlsBytes,0x18,table);
    Put(table,0x20,uint64_t(0x18));table[0x31]=1;Put(table,0x44,int32_t(6));Put(table,0x50,entries);
    uint32_t handles[]{owner,primary,secondary,target};uint8_t* data[]{unitBytes,primaryBytes,secondaryBytes,targetBytes};
    for(int i=0;i<4;++i){size_t at=(handles[i]&0xffff)*0x18;Put(entries,at,uint16_t(handles[i]>>16));entries[at+4]=i==0?0:i==3?1:2;Put(entries,at+0x10,data[i]);}
    Put(users,0,uint32_t(2));Put(users,4,owner);Put(users,0x6C,primary);Put(users,0x2EC8+0x6C,secondary);
    Put(users,0x74,uint16_t(123));Put(users,0x2EC8+0x74,uint16_t(123));
    g_halo4Camera.generation=7;g_halo4Camera.armed=true;g_halo4VehicleInput.ready=true;
    seated=cinematic=false;stereo=validTracking=pairCurrent=true;title=GameTitle::Halo4;
    g_config={};g_config.gun_barrel_aim=true;tracking={};g_halo4FloatingPair={};
    g_halo4BarrelBindingsReady=true;g_enabled=g_vrAim=true;exclusive_input::active=false;
    auto& f=g_halo4Barrel;f.base=imageBase;f.generation=7;f.installedAtMs=1;f.enabled=true;f.faulted=false;f.callbacks=0;
    f.fireOriginal=NativeFire;f.queryOriginal=NativeQuery;f.viewOriginal=NativeView;f.markersOriginal=NativeMarkers;f.aimOriginal=NativeAim;
    g_halo4BarrelShot={};g_halo4BarrelRequest={};g_halo4BarrelCapture={};g_halo4BarrelQueryContext={owner,7,2,0,3,GetTickCount64()};
    g_halo4MuzzleOwnedQuery=false;fireCalls=queryCalls=markerCalls=aimCalls=viewCalls=0;markerCount=1;expectMuzzle=true;
    queryFault=preflightFault=fireFault=markerFault=nested=omitView=replaceStorage=changeGeneration=changeOwner=nativeChangesTarget=invalidTarget=nonfiniteTarget=false;
    for(uint8_t slot=0;slot<2;++slot){weapon_muzzle::Palette p{};p.barrels[0]={GameTitle::Halo4,7,owner,slot?secondary:primary,1,3,11,GetTickCount64(),1000000000,slot,0,false,{{2,3,4},{0,1,0},{0,0,1}}};
        Check(g_halo4BarrelMuzzles.Publish(GameTitle::Halo4,slot,p),"Halo4 fixture receipt");}
}
static void Shoot(){Halo4MuzzleFireDetour(primary,0,reinterpret_cast<void*>(0x1234),0xFA);}
static bool FaultingShot(){__try{Shoot();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
static void ShotTests()
{
    Reset();Shoot();Check(fireCalls==1&&queryCalls==1&&markerCalls==1&&aimCalls==2&&*reinterpret_cast<uint32_t*>(unitBytes+0x5B0)==0x44440007,"single native fire and exact target restoration");
    Reset(true);nested=true;g_halo4BarrelQueryContext.flags=2;Shoot();Check(fireCalls==2&&queryCalls==2&&!g_halo4BarrelShot.active&&*reinterpret_cast<uint32_t*>(unitBytes+0x5B0)==0x44440007,"nested native target leases restore LIFO");
    for(int reason=0;reason<26;++reason)
    {
        Reset();expectMuzzle=false;
        switch(reason){
        case 0:markerCount=0;break;case 1:markerCount=2;break;case 2:markerCount=-1;break;
        case 3:g_halo4Barrel.enabled=false;break;case 4:g_halo4Barrel.faulted=true;break;case 5:g_halo4Barrel.generation=8;break;
        case 6:g_config.gun_barrel_aim=false;break;case 7:tracking.referenceEpoch=4;break;case 8:g_config.left_handed=true;break;
        case 9:exclusive_input::active=true;break;case 10:cinematic=true;break;case 11:seated=true;break;
        case 12:g_halo4BarrelQueryContext.sampleMs=GetTickCount64()-200;break;case 13:unitBytes[0x63A]=4;break;case 14:unitBytes[0x63B]=4;break;
        case 15:*reinterpret_cast<uint32_t*>(primaryBytes+0x624)=owner^0x10000;break;case 16:g_halo4Camera.armed=false;break;
        case 17:entries[0x30]^=1;break;case 18:invalidTarget=true;break;case 19:nonfiniteTarget=true;break;
        case 20:omitView=true;break;case 21:replaceStorage=true;break;case 22:changeGeneration=true;break;case 23:changeOwner=true;break;
        case 24:g_halo4BarrelQueryContext.sampleMs=GetTickCount64()+1000;break;case 25:title=GameTitle::Halo3;break;}
        Shoot();Check(fireCalls==1&&!g_halo4BarrelShot.active&&!g_halo4Barrel.callbacks,"failed optional ownership never replays native fire");
    }
    Reset();queryFault=true;expectMuzzle=false;Shoot();Check(g_halo4Barrel.faulted&&!g_halo4MuzzleOwnedQuery,"query exception isolated with collision ownership restored");
    Reset();preflightFault=true;expectMuzzle=false;Shoot();Check(g_halo4Barrel.faulted&&!g_halo4MuzzleOwnedQuery,"native preflight failure stays feature-local");
    Reset();markerFault=true;Check(FaultingShot()&&fireCalls==1&&!g_halo4Barrel.callbacks,"native marker exception propagates without replay");
    Reset();fireFault=true;Check(FaultingShot()&&fireCalls==1&&*reinterpret_cast<uint32_t*>(unitBytes+0x5B0)==0x44440007,"native fire exception restores lease once");
    Reset();nativeChangesTarget=true;Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x5B0)==0xABCD0006,"new native target survives lease release");
    Reset();g_halo4BarrelQueryContext={};float direction[3]{0,1,0},control[3]{};uint8_t output[0x28]{};
    Halo4MuzzleQueryDetour(2,0,direction,3,control,output);Check(g_halo4BarrelQueryContext.unit==owner,"ordinary native query captures actual view owner");
    g_halo4BarrelQueryContext={};Halo4MuzzleQueryDetour(2,1,direction,3,control,output);Check(!g_halo4BarrelQueryContext.sampleMs,"fallback query cannot seed shot context");
    g_halo4BarrelQueryContext={owner,7,2,0,3,GetTickCount64()};std::thread worker([]{Check(!g_halo4BarrelQueryContext.sampleMs,"native query context stays on native thread");});worker.join();
}
static void PublicationTests()
{
    BoneMatrix destination[80];for(auto& n:destination){n.scale=2;n.translation[0]=2;n.translation[1]=3;n.translation[2]=4;}
    for(const auto& marker:weapon_muzzle::kMarkers)
    {
        if(marker.title!=GameTitle::Halo4)continue;
        for(int hand=0;hand<2;++hand)
        {
            Reset();g_config.left_handed=hand!=0;
            auto pending=Halo4PrepareMuzzlePalette(primary,123,uint32_t(marker.identity),marker.nodeCount,destination);
            Halo4CommitMuzzlePalette(pending);weapon_muzzle::Receipt sample{};weapon_muzzle::Ray expected{};
            Check(weapon_muzzle::Transform(marker,2,destination[marker.node].rotation,destination[marker.node].translation,expected),"H4 authored marker fixture");
            Check(g_halo4BarrelMuzzles.Read(GameTitle::Halo4,7,owner,primary,3,GetTickCount64(),1000000000,0,marker.barrel,hand!=0,sample)&&sample.ray.position[0]==expected.position[0],"committed H4 weapon owns correct authored barrel");
            Put(entries,0x30,uint16_t((primary>>16)^1));Halo4CommitMuzzlePalette(pending);
            Check(!g_halo4BarrelMuzzles.Read(GameTitle::Halo4,7,owner,primary,3,GetTickCount64(),1000000000,0,marker.barrel,hand!=0,sample),"weapon salt changed during native render invalidates receipt");
        }
    }
    for(int reject=0;reject<16;++reject)
    {
        Reset();uint32_t unit=0;uint8_t slot=0;
        switch(reject){
        case 0:Put(users,0x6C,primary^0x10000);break;
        case 1:Put(users,4,owner^0x10000);break;
        case 2:Put(users,0,uint32_t(0));break;
        case 3:tlsIndex=1088;break;
        case 4:g_halo4BarrelBindingsReady=false;break;
        case 5:g_config.gun_barrel_aim=false;break;
        case 6:g_halo4VehicleInput.ready=false;break;
        case 7:seated=true;break;
        case 8:Put(unitBytes,0x694,target);break;
        case 9:table[0x31]=0;break;
        case 10:Put(table,0x20,uint64_t(0x20));break;
        case 11:Put(table,0x44,int32_t(2));break;
        case 12:entries[0x30+4]=1;break;
        case 13:Put(primaryBytes,0x624,UINT32_MAX);Put(primaryBytes,0x480,owner^0x10000);break;
        case 14:unitBytes[0x63B]=0;break;
        case 15:localUnit^=0x10000;break;}
        Check(!Halo4CaptureMuzzleOwner(primary,unit,slot),"foreign/stale/unproven H4 palette ownership rejected");
    }
    for(int reject=0;reject<7;++reject)
    {
        Reset();const weapon_muzzle::Marker* marker=nullptr;
        for(const auto& candidate:weapon_muzzle::kMarkers)if(candidate.title==GameTitle::Halo4){marker=&candidate;break;}
        Check(marker!=nullptr,"H4 authored marker available");
        uint32_t checksum=uint32_t(marker->identity);int count=marker->nodeCount;const BoneMatrix* palette=destination;
        switch(reject){case 0:Put(users,0x74,uint16_t(124));break;case 1:g_halo4FloatingPair.contactFrames[1].serial++;break;
        case 2:g_halo4FloatingPair.contactFrames[1].referenceEpoch=0;break;case 3:g_halo4FloatingPair.contactFrames[1].timeNs=0;break;
        case 4:checksum=0;break;case 5:count--;break;case 6:palette=nullptr;break;}
        const auto pending=Halo4PrepareMuzzlePalette(primary,123,checksum,count,palette);Halo4CommitMuzzlePalette(pending);
        weapon_muzzle::Receipt sample{};
        Check(!g_halo4BarrelMuzzles.Read(GameTitle::Halo4,7,owner,primary,3,GetTickCount64(),1000000000,0,0,false,sample),"rejected committed palette clears previous barrel");
    }
}

static uintptr_t lifecycleBase{};
static unsigned bindingScan{},createCalls{},removeCalls{},logged{};
static int missingBinding=-1,ambiguousBinding=-1,failCreate=-1,failEnable=-1,failDisable=-1,failRemove=-1;
static bool ingress=false,quiesced=false,hookExists[5]{},hookEnabled[5]{};
static constexpr uint32_t hookRvas[]{0x6176B8,0x1DB840,0x1DC9E4,0x5D5B74,0x5F3510};
namespace sig
{
static uintptr_t Find(uintptr_t base,size_t,const char*)
{
    constexpr uint32_t bindings[]{0x6176B8,0x1DB840,0x1DC9E4,0x5D5B74,0x5F3510,0x5FA1C4,0x5F9E20,0x612238,0x5F1ABC,0x1CD454,0x1DC86C,0x3B1B4C,0x3B1F11,0x610F6C};
    const unsigned number=bindingScan++/2;Check(number<14,"bounded own Halo4 binding inventory");
    if(int(number)==missingBinding)return 0;
    return base==lifecycleBase?base+bindings[number]:int(number)==ambiguousBinding?base+0x100:0;
}
}
static int HookIndex(void* target)
{
    for(int i=0;i<5;++i)if(reinterpret_cast<uintptr_t>(target)==lifecycleBase+hookRvas[i])return i;
    Check(false,"only verified Halo4 optional targets");return -1;
}
static MH_STATUS FixtureCreate(void* target,void* detour,void** original)
{
    const int i=HookIndex(target);++createCalls;
    const void* expected[]{reinterpret_cast<void*>(&Halo4MuzzleFireDetour),
        reinterpret_cast<void*>(&Halo4MuzzleQueryDetour),reinterpret_cast<void*>(&Halo4MuzzleViewDetour),
        reinterpret_cast<void*>(&Halo4MuzzleMarkersDetour),reinterpret_cast<void*>(&Halo4MuzzleAimDetour)};
    Check(detour==expected[i]&&original,"exact Halo4 hook ABI entry");
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
    Check(!hookEnabled[i]&&quiesced&&!ingress&&!g_halo4Barrel.callbacks,"no Halo4 trampoline removed before complete drain");
    if(i==failRemove)return MH_ERROR_MEMORY_PROTECT;hookExists[i]=false;return MH_OK;
}
static bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const* originals,size_t count,const std::atomic<uint32_t>& callbacks)
{
    Check(count==5,"all five detour ingress ranges protected");
    const void* expected[]{reinterpret_cast<void*>(&Halo4MuzzleFireDetour),
        reinterpret_cast<void*>(&Halo4MuzzleQueryDetour),reinterpret_cast<void*>(&Halo4MuzzleViewDetour),
        reinterpret_cast<void*>(&Halo4MuzzleMarkersDetour),reinterpret_cast<void*>(&Halo4MuzzleAimDetour)};
    for(int i=0;i<5;++i)Check(functions[i]==expected[i]&&(!hookExists[i]||originals[i]==reinterpret_cast<void*>(uintptr_t(0x4000+i*0x100))),"exact retained detour and trampoline pairs");
    quiesced=!callbacks.load()&&!ingress;return quiesced;
}
#define MH_CreateHook FixtureCreate
#define MH_EnableHook FixtureEnable
#define MH_RemoveHook FixtureRemove
#define LOG(...) (++logged)
#include "../src/dll/halo4_muzzle_lifecycle.inl"
#undef MH_CreateHook
#undef MH_EnableHook
#undef MH_RemoveHook
#undef LOG
static void LifecycleTests()
{
    constexpr size_t size=0x700000;auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(image!=nullptr,"bounded lifecycle image");lifecycleBase=reinterpret_cast<uintptr_t>(image);
    const uint32_t edges[][2]{{0x6179B2,0x5D5B74},{0x617E90,0x5F3510},{0x618103,0x1DCB8C},{0xA364C,0x1DB840},{0xA36AE,0x1DB840},{0xA3723,0x1DB840},{0x1DB9A1,0x1DC9E4},{0x1DBDB1,0x1DC9E4},{0x1DC8BF,0x1DC9E4},{0x1DC42B,0x1DC86C}};
    for(const auto& edge:edges){image[edge[0]]=0xE8;*reinterpret_cast<int32_t*>(image+edge[0]+1)=int32_t(edge[1]-edge[0]-5);}
    auto reset=[]
    {
        g_halo4VehicleInput.ready=true;
        bindingScan=createCalls=removeCalls=0;quiesced=ingress=false;
        missingBinding=ambiguousBinding=failCreate=failEnable=failDisable=failRemove=-1;
        std::memset(hookExists,0,sizeof(hookExists));std::memset(hookEnabled,0,sizeof(hookEnabled));
        for(auto& target:g_halo4Barrel.targets)target=nullptr;
        g_halo4Barrel.fireOriginal=nullptr;g_halo4Barrel.queryOriginal=nullptr;
        g_halo4Barrel.viewOriginal=nullptr;g_halo4Barrel.markersOriginal=nullptr;g_halo4Barrel.aimOriginal=nullptr;
        g_halo4Barrel.enabled=false;g_halo4BarrelBindingsReady=false;g_halo4Barrel.callbacks=0;
    };
    reset();g_halo4VehicleInput.ready=false;
    Check(!InstallHalo4Muzzle(lifecycleBase,size,7)&&createCalls==0,"unavailable native seat reader cannot authorize muzzle");
    for(int i=0;i<14;++i)
    {
        reset();missingBinding=i;Check(!InstallHalo4Muzzle(lifecycleBase,size,7)&&createCalls==0,"missing own proof stays stock");
        reset();ambiguousBinding=i;Check(!InstallHalo4Muzzle(lifecycleBase,size,7)&&createCalls==0,"ambiguous own proof stays stock");
    }
    for(int i=0;i<5;++i)
    {
        reset();failCreate=i;Check(!InstallHalo4Muzzle(lifecycleBase,size,7)&&!g_halo4Barrel.enabled&&!g_halo4BarrelBindingsReady,"partial create remains disabled");
        Check(RemoveHalo4Muzzle(),"partial create cleanup");
        reset();failEnable=i;Check(!InstallHalo4Muzzle(lifecycleBase,size,7)&&!g_halo4Barrel.enabled&&!g_halo4BarrelBindingsReady,"partial enable remains disabled");
        Check(RemoveHalo4Muzzle(),"partial enable cleanup");
        reset();Check(InstallHalo4Muzzle(lifecycleBase,size,7),"all exact hooks installed");failDisable=i;
        Check(!RemoveHalo4Muzzle()&&removeCalls==0,"failed disable retains all trampoline dependencies");failDisable=-1;
        Check(RemoveHalo4Muzzle(),"disable retry succeeds");
        reset();Check(InstallHalo4Muzzle(lifecycleBase,size,7),"all exact hooks installed");failRemove=i;
        Check(!RemoveHalo4Muzzle()&&g_halo4Barrel.targets[i],"failed removal retains exact hook for retry");failRemove=-1;
        Check(RemoveHalo4Muzzle(),"removal retry succeeds");
    }
    for(const auto& edge:edges)
    {
        reset();image[edge[0]]^=1;Check(!InstallHalo4Muzzle(lifecycleBase,size,7)&&createCalls==0,"changed downstream edge prevents adaptation");image[edge[0]]^=1;
    }
    reset();Check(InstallHalo4Muzzle(lifecycleBase,size,7),"fixture installed");ingress=true;
    Check(!RemoveHalo4Muzzle()&&removeCalls==0,"zero counter with thread in ingress retains code");ingress=false;
    g_halo4Barrel.callbacks=1;Check(!RemoveHalo4Muzzle()&&removeCalls==0,"active callback retains code");
    g_halo4Barrel.callbacks=0;Check(RemoveHalo4Muzzle()&&!g_halo4BarrelBindingsReady,"drained cleanup clears publication authority");
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"lifecycle fixture released");
}
// T6c/F03: the raw primary role byte 0xFF is the native explicit "no weapon in
// this slot". The reader must report it through the optional absence
// out-parameter even though the pair itself is refused (the empty slot fails
// the inventory validation), and no neighbouring rejection may ever report
// absence.
static void OwnerEvidenceTests()
{
    Reset();
    uint32_t weapons[2]{};
    bool absent=true;
    unitBytes[0x63A]=0xFF;
    Check(!Halo4ReadMuzzleWeapons(localUnit,weapons,&absent)&&absent,
        "F03: an empty primary role byte reports primary-absent");
    absent=true;
    unitBytes[0x63A]=0;
    Check(Halo4ReadMuzzleWeapons(localUnit,weapons,&absent)&&!absent,
        "F03: a complete on-foot inventory reports no absence");
    absent=true;
    unitBytes[0x63A]=4;
    Check(!Halo4ReadMuzzleWeapons(localUnit,weapons,&absent)&&!absent,
        "F03: an invalid primary role is not reported as absence");
    absent=true;
    unitBytes[0x63A]=0;
    *reinterpret_cast<uint32_t*>(primaryBytes+0x624)=owner^0x10000;
    Check(!Halo4ReadMuzzleWeapons(localUnit,weapons,&absent)&&!absent,
        "F03: a foreign-owned weapon is not reported as absence");
    absent=true;
    Check(!Halo4ReadMuzzleWeapons(owner^0x10000,weapons,&absent)&&!absent,
        "F03: an unreadable unit is not reported as absence");
}
int main()
{
    ShotTests();PublicationTests();OwnerEvidenceTests();LifecycleTests();
    std::printf("PASS: %u production Halo4 muzzle checks (native services stubbed)\n",checks);
}
