#include <Windows.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "../src/common/dual_weapon_aim_logic.h"
#include "../src/common/native_shot_target_lease.h"
#include "../src/common/exclusive_input.h"
#include "../src/common/weapon_muzzle.h"

static unsigned checks{};
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
static constexpr uint32_t owner=0x12340001,primary=0x56780002,secondary=0x789A0003;
static constexpr uintptr_t imageBase=0x180000000;
static GameTitle title=GameTitle::Halo3;
static bool cinematic=false,seated=false,stereo=true,owned=true,validTracking=true,storageReplaced=false;
static uint32_t localUnit=owner,liveWeapons[2]{primary,secondary};
static std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
static std::atomic<bool> g_vrAim{true},g_enabled{true};
static struct {float crosshair_distance_m=10;bool independent_dual_aim=true,gun_barrel_aim=false,left_handed=false;} g_config;
struct VrContactTrackingSnapshot
{uint64_t referenceEpoch=3;int64_t timeNs=1000000000;struct {bool valid=true;} hands[2];};
static VrContactTrackingSnapshot tracking;
static weapon_muzzle::Store g_halo3Muzzles;
static thread_local bool g_legacyCollisionOwnedQuery=false;
static alignas(8) unsigned char unitBytes[0x400]{},replacementBytes[0x400]{};
static int32_t Player(int32_t){return int32_t(localUnit);}
static bool Seated(int32_t){return seated;}
static auto g_halo3PlayerUnitGetter=&Player;
static auto g_halo3UnitInVehicle=&Seated;
static GameTitle TitleAdapter_GetActiveTitle(){return title;}
static bool VR_IsStereoEnabled(){return stereo;}
static float Game_GetWorldScale(){return 1;}
static CinematicControlState ReadCinematicControl(int32_t&,int32_t&)
{return cinematic?CinematicControlState::AuthoredLocked:CinematicControlState::PlayerControlled;}
static const uint8_t* Halo3MeleeSelectionObject(uint32_t unit,uint8_t kind)
{return unit==localUnit&&kind==0?(storageReplaced?replacementBytes:unitBytes):nullptr;}
static bool Halo3ReadOwnedDualWeapons(uint32_t unit,uint32_t* weapons)
{std::memcpy(weapons,liveWeapons,sizeof(liveWeapons));return owned&&unit==localUnit;}
static bool Halo3ReadOwnedWeapons(uint32_t unit,uint32_t* weapons,bool requireDual)
{return Halo3ReadOwnedDualWeapons(unit,weapons)&&(!requireDual||weapons[1]!=UINT32_MAX);}
static bool Halo3ContactObject(uint32_t target){return target==0x11110004||target==0x22220005;}
static bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out){out=tracking;return validTracking;}
static int ResolveEquippedWeaponSlot(uint32_t weapon,uint32_t a,uint32_t b,bool,bool)
{return weapon==a?0:weapon==b?1:-1;}
using Fire=uint64_t(__fastcall*)(uint32_t,int16_t,void*,int32_t,uint8_t);
using Aim=void(__fastcall*)(uint32_t,float*,float*,uint64_t,float*,uint8_t,uint8_t);
using Query=void(__fastcall*)(int32_t,uint8_t,float*,int16_t,float*,void*);
using Camera=int32_t(__fastcall*)(uint32_t,float*,float*);
static struct
{
    uintptr_t base=imageBase;uint32_t generation=7;uint64_t installedAtMs=1;
    Fire fireOriginal{};Aim aimOriginal{};Query queryOriginal{};Camera cameraOriginal{};
    void* fireTarget{},*aimTarget{},*queryTarget{},*cameraTarget{};
    std::atomic<bool> enabled{true},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> rays[2]{},refused{0},nativeQueries{0},targetRestoresRefused{0};
    std::atomic<uint64_t> fireEntries{0},fireReturns{0},fireUnwinds{0},fireWithData{0},firePredicted{0};
    DualWeaponAimPublication aim;
} g_halo3Dual;
static uintptr_t caller{};
#define _ReturnAddress() reinterpret_cast<void*>(::caller)
#include "../src/dll/halo3_independent_shots.inl"
#undef _ReturnAddress

// Exercise the actual optional installer/retirement state machine, including
// zero-counter threads paused before entry accounting. Native byte identity is
// checked independently by tools/verify-halo3-dual-bindings.py.
static uintptr_t lifecycleBase{};
static unsigned bindingScan{},createCalls{},enableCalls{},removeCalls{},logged{};
static int missingBinding=-1,ambiguousBinding=-1,failCreate=-1,failEnable=-1,
    failDisable=-1,failRemove=-1;
static bool ingress=false,quiesced=false;
static bool hookExists[4]{},hookEnabled[4]{};
static constexpr uint32_t hookRvas[]{0x3683A0,0x3524B0,0x13B08C,0x212198};
namespace sig
{
uintptr_t Find(uintptr_t base,size_t,const char*)
{
    constexpr uint32_t bindings[]{0x3683A0,0x3524B0,0x13B08C,0x212198,0x13BAD0};
    const unsigned number=bindingScan++/2;
    Check(number<5,"bounded cold binding inventory");
    if(int(number)==missingBinding)return 0;
    return base==lifecycleBase?base+bindings[number]:
        int(number)==ambiguousBinding?base+0x100:0;
}
}
static int HookIndex(void* target)
{
    for(int i=0;i<4;++i)if(reinterpret_cast<uintptr_t>(target)==lifecycleBase+hookRvas[i])return i;
    Check(false,"only known optional hooks");return -1;
}
static void PublishHalo3DualAim(){}
static MH_STATUS FixtureCreate(void* target,void* detour,void** original)
{
    const int i=HookIndex(target);++createCalls;
    const void* expected[]{reinterpret_cast<void*>(&Halo3IndependentFireDetour),reinterpret_cast<void*>(&Halo3IndependentAimDetour),
        reinterpret_cast<void*>(&Halo3IndependentQueryDetour),reinterpret_cast<void*>(&Halo3IndependentCameraDetour)};
    Check(detour==expected[i]&&original,"correct production hook entry");
    if(i==failCreate)return MH_ERROR_MEMORY_ALLOC;
    hookExists[i]=true;*original=reinterpret_cast<void*>(uintptr_t(0x4000+i*0x100));return MH_OK;
}
static MH_STATUS FixtureEnable(void* target)
{
    const int i=HookIndex(target);++enableCalls;Check(hookExists[i],"enable created entry only");
    if(i==failEnable)return MH_ERROR_MEMORY_PROTECT;
    hookEnabled[i]=true;return MH_OK;
}
static MH_STATUS MCCVR_DisableHookForRetirement(void* target)
{
    const int i=HookIndex(target);
    if(i==failDisable)return MH_ERROR_MEMORY_PROTECT;
    if(!hookExists[i])return MH_ERROR_NOT_CREATED;
    const auto status=hookEnabled[i]?MH_OK:MH_ERROR_DISABLED;hookEnabled[i]=false;return status;
}
static MH_STATUS FixtureRemove(void* target)
{
    const int i=HookIndex(target);++removeCalls;
    Check(quiesced&&!hookEnabled[i]&&!g_halo3Dual.callbacks.load(),"never free busy native entry");
    if(i==failRemove)return MH_ERROR_MEMORY_PROTECT;
    const auto status=hookExists[i]?MH_OK:MH_ERROR_NOT_CREATED;hookExists[i]=false;return status;
}
static bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const* original,
    size_t count,const std::atomic<uint32_t>& callbacks)
{
    const void* expected[]{reinterpret_cast<void*>(&Halo3IndependentFireDetour),reinterpret_cast<void*>(&Halo3IndependentAimDetour),
        reinterpret_cast<void*>(&Halo3IndependentQueryDetour),reinterpret_cast<void*>(&Halo3IndependentCameraDetour),
        reinterpret_cast<void*>(&PublishHalo3DualAim)};
    Check(count==5,"all four native entries and publisher included");
    for(size_t i=0;i<count;++i)
    {
        Check(functions[i]==expected[i],"exact detour range");
        if(i<4)
        {
            Check(!hookEnabled[i],"all hooks disabled before quiescence");
            Check(original[i]==(hookExists[i]?reinterpret_cast<void*>(0x4000+i*0x100):nullptr),"no retired trampoline retained on retry");
        }
    }
    quiesced=!callbacks.load()&&!ingress;return quiesced;
}
#define MH_CreateHook FixtureCreate
#define MH_EnableHook FixtureEnable
#define MH_RemoveHook FixtureRemove
#define LOG(...) (++logged)
static bool RemoveHalo3Muzzle(){return true;}
static bool InstallHalo3Muzzle(uintptr_t,size_t){return false;}
#include "../src/dll/halo3_dual_lifecycle.inl"
#undef LOG
#undef MH_CreateHook
#undef MH_EnableHook
#undef MH_RemoveHook

static void LifecycleTests()
{
    constexpr size_t size=0x36A000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(image!=nullptr,"private lifecycle image");lifecycleBase=reinterpret_cast<uintptr_t>(image);
    auto edge=[&](uint32_t call,uint32_t target){image[call]=0xE8;*reinterpret_cast<int32_t*>(image+call+1)=int32_t(target-call-5);};
    edge(0x368B92,0x3524B0);edge(0x13B1D7,0x212198);edge(0x13BC08,0x212198);edge(0xF4CC8,0x13B08C);edge(0x368DFE,0x13BAD0);
    const unsigned char field[]{0x4C,0x8D,0xAB,0x18,0x02,0,0},parent[]{0x44,0x8B,0x83,0xA4,0x02,0,0};
    std::memcpy(image+0x368A6A,field,sizeof(field));std::memcpy(image+0x368AE5,parent,sizeof(parent));
    auto reset=[&]{
        bindingScan=createCalls=enableCalls=removeCalls=0;quiesced=ingress=false;
        missingBinding=ambiguousBinding=failCreate=failEnable=failDisable=failRemove=-1;
        std::memset(hookExists,0,sizeof(hookExists));std::memset(hookEnabled,0,sizeof(hookEnabled));
        g_halo3Dual.fireTarget=g_halo3Dual.aimTarget=g_halo3Dual.queryTarget=g_halo3Dual.cameraTarget=nullptr;
        g_halo3Dual.fireOriginal=nullptr;g_halo3Dual.aimOriginal=nullptr;g_halo3Dual.queryOriginal=nullptr;g_halo3Dual.cameraOriginal=nullptr;
        g_halo3Dual.enabled=false;g_halo3Dual.callbacks=0;
    };
    for(int i=0;i<5;++i)
    {
        reset();missingBinding=i;Check(!InstallHalo3DualAim(lifecycleBase,size,7)&&createCalls==0,"missing native binding stays stock");
        reset();ambiguousBinding=i;Check(!InstallHalo3DualAim(lifecycleBase,size,7)&&createCalls==0,"ambiguous native binding stays stock");
    }
    for(int i=0;i<4;++i)
    {
        reset();failCreate=i;Check(!InstallHalo3DualAim(lifecycleBase,size,7)&&!g_halo3Dual.enabled,"partial creation isolated");
        Check(RemoveHalo3DualAim(),"partial creation fully retired");
        reset();failEnable=i;Check(!InstallHalo3DualAim(lifecycleBase,size,7)&&!g_halo3Dual.enabled,"partial enable isolated");
        Check(RemoveHalo3DualAim(),"partial enable fully retired");
        reset();Check(InstallHalo3DualAim(lifecycleBase,size,7),"fixture installed");failDisable=i;
        Check(!RemoveHalo3DualAim()&&removeCalls==0,"failed disable retains all trampolines");
        failDisable=-1;Check(RemoveHalo3DualAim(),"disable cleanup retry succeeds");
        reset();Check(InstallHalo3DualAim(lifecycleBase,size,7),"fixture installed");failRemove=i;
        Check(!RemoveHalo3DualAim(),"failed removal reported");failRemove=-1;
        Check(RemoveHalo3DualAim(),"partial removal retry has no stale originals");
    }
    reset();Check(InstallHalo3DualAim(lifecycleBase,size,7),"fixture installed");ingress=true;
    Check(!RemoveHalo3DualAim()&&removeCalls==0,"zero counter with live ingress retains code");
    ingress=false;g_halo3Dual.callbacks=1;Check(!RemoveHalo3DualAim()&&removeCalls==0,"live callback retains code");
    g_halo3Dual.callbacks=0;Check(RemoveHalo3DualAim(),"drained cleanup succeeds");
    reset();image[0x368AE5]^=1;Check(!InstallHalo3DualAim(lifecycleBase,size,7)&&createCalls==0,"changed controlling-parent field blocks only feature");
    image[0x368AE5]^=1;reset();image[0x13BC08]^=1;
    Check(!InstallHalo3DualAim(lifecycleBase,size,7)&&createCalls==0,"changed downstream consumer blocks only feature");
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"private lifecycle image released");
}

static unsigned queryCalls{},fireCalls{},cameraCalls{},aimCalls{};
static bool queryFault=false,fireFault=false,omitCamera=false,changeGeneration=false,
    replaceStorage=false,changeNativeTarget=false,changeWeapons=false,nestedFire=false;
static uint32_t observedTargets[2]{};
static float observedDirections[2][3]{};
static int32_t __fastcall NativeCamera(uint32_t,float* position,float* direction)
{++cameraCalls;position[0]=position[1]=position[2]=0;direction[0]=0;direction[1]=1;direction[2]=0;return 17;}
static void __fastcall NativeQuery(int32_t user,uint8_t flags,float* direction,int16_t zoom,float* control,void* out)
{
    ++queryCalls;
    if(g_halo3IndependentShot.active)
        Check(g_legacyCollisionOwnedQuery,"auxiliary H3 targeting cannot schedule another collision/melee tick");
    Check(user==2&&zoom==3,"original native input-user/zoom retained, no output-user assumption");
    Check(flags==0||flags==1||flags==2,"native flags forwarded");
    if(queryFault)RaiseException(0xE0123456,0,0,nullptr);
    float position[3]{},view[3]{};
    const auto previous=caller;caller=imageBase+0x13B1DC;
    if(!omitCamera)Check(Halo3IndependentCameraDetour(localUnit,position,view)==17,"native camera return preserved");
    caller=previous;
    std::memset(out,0,0x24);
    auto* words=static_cast<uint32_t*>(out);
    words[0]=UINT32_MAX;words[1]=direction[0]>.5f?0x11110004:0x22220005;words[2]=UINT32_MAX;
    auto* values=static_cast<float*>(out);values[3]=.25f;values[4]=.75f;
    control[0]=.1f;control[1]=.2f;control[2]=.3f;
    if(changeWeapons)liveWeapons[1]=0x98760008;
}
static void __fastcall NativeAim(uint32_t,float*,float* direction,uint64_t,float*,uint8_t,uint8_t)
{++aimCalls;direction[0]=0;direction[1]=1;direction[2]=0;}
static uint64_t __fastcall NativeFire(uint32_t weapon,int16_t barrel,void* data,int32_t index,uint8_t predicted)
{
    ++fireCalls;
    Check(barrel==-2&&data==reinterpret_cast<void*>(0x1234)&&index==-7&&predicted==1,"complete native firing ABI");
    const int slot=weapon==primary?0:1;
    observedTargets[slot]=*reinterpret_cast<uint32_t*>(unitBytes+0x21C);
    float origin[3]{},direction[3]{},offset[3]{};
    const auto previous=caller;caller=imageBase+0x368B97;
    Halo3IndependentAimDetour(owner,origin,direction,0xABCDEF,offset,1,1);
    // Simulate the downstream assist replacing direction from its camera ray.
    caller=imageBase+0x13BC0D;
    Check(Halo3IndependentCameraDetour(owner,origin,direction)==17,"downstream return preserved");
    std::memcpy(observedDirections[slot],direction,sizeof(direction));
    caller=previous;
    if(nestedFire&&slot==0)
    {
        const auto before=*reinterpret_cast<uint32_t*>(unitBytes+0x21C);
        (void)Halo3IndependentFireDetour(secondary,-2,reinterpret_cast<void*>(0x1234),-7,1);
        Check(*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==before,"nested second shot restores outer targeting");
    }
    if(changeNativeTarget)*reinterpret_cast<uint32_t*>(unitBytes+0x21C)=0x33330006;
    if(changeGeneration)g_halo3RuntimeGeneration=8;
    if(replaceStorage)storageReplaced=true;
    if(fireFault)RaiseException(0xE0123457,0,0,nullptr);
    return 0x123456789ABCDEF0;
}
static void Reset()
{
    title=GameTitle::Halo3;cinematic=seated=storageReplaced=false;
    stereo=owned=validTracking=true;localUnit=owner;liveWeapons[0]=primary;liveWeapons[1]=secondary;
    g_config.independent_dual_aim=true;
    g_halo3RuntimeGeneration=7;g_vrAim=g_enabled=true;g_halo3Dual.enabled=true;g_halo3Dual.faulted=false;
    queryFault=fireFault=omitCamera=changeGeneration=replaceStorage=changeNativeTarget=changeWeapons=nestedFire=false;
    queryCalls=fireCalls=cameraCalls=aimCalls=0;caller=0;
    g_halo3Dual.fireEntries=0;g_halo3Dual.fireReturns=0;g_halo3Dual.fireUnwinds=0;
    g_halo3Dual.fireWithData=0;g_halo3Dual.firePredicted=0;
    g_halo3NativeQueryContext={};g_halo3QueryCapture={};g_halo3IndependentShot={};
    std::memset(unitBytes,0,sizeof(unitBytes));std::memset(replacementBytes,0,sizeof(replacementBytes));
    *reinterpret_cast<uint32_t*>(unitBytes+0x2A4)=UINT32_MAX;
    *reinterpret_cast<uint32_t*>(replacementBytes+0x2A4)=UINT32_MAX;
    *reinterpret_cast<uint32_t*>(unitBytes+0x21C)=0x44440007;
    tracking={};
    DualWeaponAimSnapshot sample{};
    sample.unit=owner;sample.weapons[0]=primary;sample.weapons[1]=secondary;
    sample.generation=7;sample.trackingEpoch=3;sample.timeNs=tracking.timeNs;sample.sampleMs=GetTickCount64();
    sample.directions[0][0]=1;sample.directions[1][2]=1;
    sample.positions[0][0]=.2f;sample.positions[1][0]=-.2f;
    Check(g_halo3Dual.aim.Publish(sample),"coherent controller publication");
    float direction[3]{1,0,0},control[3]{};unsigned char result[0x24]{};
    Halo3IndependentQueryDetour(2,2,direction,3,control,result);
    Check(g_halo3NativeQueryContext.unit==owner,"native camera establishes actual query owner");
}
static uint64_t Shoot(uint32_t weapon=primary)
{return Halo3IndependentFireDetour(weapon,-2,reinterpret_cast<void*>(0x1234),-7,1);}
static bool FaultingShot()
{__try{(void)Shoot();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
#include "halo3_muzzle_shots_fixture.inl"
#include "halo3_muzzle_lifecycle_fixture.inl"
int main()
{
    g_halo3Dual.fireOriginal=NativeFire;g_halo3Dual.aimOriginal=NativeAim;
    g_halo3Dual.queryOriginal=NativeQuery;g_halo3Dual.cameraOriginal=NativeCamera;
    Reset();
    Check(Shoot()==0x123456789ABCDEF0,"full native firing return");
    Check(observedTargets[0]==0x11110004&&observedDirections[0][0]==1,"primary query and final ray belong to primary");
    Check(*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x44440007,"normal shot restores target");
    Check(Shoot(secondary)==0x123456789ABCDEF0,"secondary fires");
    Check(observedTargets[1]==0x22220005&&observedDirections[1][2]==1,"secondary query and final ray independent");
    Check(g_halo3Dual.callbacks==0&&!g_halo3IndependentShot.active,"callback scope balanced");
    Reset();nestedFire=true;(void)Shoot();Check(fireCalls==2,"simultaneous/nested roles each fire once");
    Reset();fireFault=true;Check(FaultingShot()&&fireCalls==1,"native firing failure never replayed");
    Check(g_halo3Dual.fireEntries==1&&g_halo3Dual.fireReturns==0&&g_halo3Dual.fireUnwinds==1&&
        g_halo3Dual.fireWithData==1&&g_halo3Dual.firePredicted==1,"native fault distinguished from completed shot without altering arguments");
    Check(g_halo3Dual.callbacks==0&&!g_halo3IndependentShot.active&&*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x44440007,"exception restores own target and scopes");
    Reset();g_config.independent_dual_aim=false;g_config.gun_barrel_aim=false;fireFault=true;queryCalls=0;
    Check(FaultingShot()&&fireCalls==1&&queryCalls==0&&g_halo3Dual.fireUnwinds==1&&
        g_halo3Dual.callbacks==0,"settings-off native failure remains visible with no extra targeting query");
    Reset();queryFault=true;(void)Shoot();
    Check(fireCalls==1,"optional query failure forwards stock firing once");
    Check(g_halo3Dual.faulted,"optional query failure isolates the feature");
    Check(*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x44440007,"optional query failure leaves stock target intact");
    Reset();omitCamera=true;(void)Shoot();Check(observedTargets[0]==0x44440007,"unconsumed query scope retains stock");
    Reset();changeWeapons=true;(void)Shoot();Check(observedTargets[0]==0x44440007,"inventory change during query refuses mutation");
    Reset();changeGeneration=true;(void)Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x11110004,"foreign generation never restored");
    Reset();replaceStorage=true;(void)Shoot();Check(*reinterpret_cast<uint32_t*>(replacementBytes+0x21C)==0,"replaced storage untouched");
    Reset();changeNativeTarget=true;(void)Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x21C)==0x33330006,"native target change wins");
    for(int refusal=0;refusal<16;++refusal)
    {
        Reset();
        switch(refusal)
        {
        case 0:g_halo3NativeQueryContext.sampleMs=0;break;
        case 1:g_halo3NativeQueryContext.generation=6;break;
        case 2:g_halo3NativeQueryContext.unit=0x12350001;break;
        case 3:g_halo3NativeQueryContext.sampleMs=GetTickCount64()+1000;break;
        case 4:cinematic=true;break;case 5:seated=true;break;case 6:owned=false;break;
        case 7:stereo=false;break;case 8:validTracking=false;break;case 9:tracking.referenceEpoch=4;break;
        case 10:tracking.hands[1].valid=false;break;case 11:title=GameTitle::Unknown;break;
        case 12:*reinterpret_cast<uint32_t*>(unitBytes+0x2A4)=0xFFFF0002;break;
        case 13:g_vrAim=false;break;
        case 14:g_config.independent_dual_aim=false;break;
        case 15:exclusive_input::active=true;break;
        }
        const auto before=queryCalls;(void)Shoot();
        Check(queryCalls==before&&observedTargets[0]==0x44440007&&fireCalls==1,"unsafe scope retains original single fire");
        exclusive_input::active=false;
    }
    Reset();
    std::thread other([]{Check(g_halo3NativeQueryContext.sampleMs==0&&!g_halo3IndependentShot.active,"native query context cannot cross threads");});other.join();
    Check(g_halo3Dual.callbacks==0,"all callbacks drained");
    MuzzleTests();
    MuzzleLifecycleTests();
    LifecycleTests();
    std::printf("PASS: %u production H3 independent shot checks (native services stubbed)\n",checks);
}
