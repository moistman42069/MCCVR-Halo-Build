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
#include "../src/common/halo2_render_logic.h"
#include "../src/common/weapon_hand_logic.h"
#include "../src/common/native_shot_target_lease.h"
#include "../src/common/exclusive_input.h"
#include "../src/common/weapon_muzzle.h"

static unsigned checks{};
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
static constexpr uint32_t owner=0x12340001,primary=0x56780002,secondary=0x789A0003;
static constexpr uint32_t kOwnedUser=0;
static std::atomic<uint32_t> g_generation{7};
static struct {bool independent_dual_aim=true;float crosshair_distance_m=10;bool gun_barrel_aim=false,left_handed=false;} g_config;
static bool controller=true,palette=true,direct=true,owned=true,publicationValid=true,trackingValid=true,carrierValid=true;
static bool storageReplaced=false,queryFault=false,fireFault=false,omitLocation=false,omitView=false,
    changeWeapons=false,changeGeneration=false,replaceStorage=false,changeNativeTarget=false,nested=false;
static uint32_t localUnit=owner;
static alignas(8) uint8_t unitBytes[0x400]{},replacementBytes[0x400]{};
static uint8_t users[kHalo2FirstPersonUserStride]{};
static const uint8_t* userPointer=users;
static Halo2ObserverPosePublication publication;
struct VrContactTrackingSnapshot
{struct {bool valid=true;} hands[2];uint64_t referenceEpoch=3;int64_t timeNs=1000000000;};
static VrContactTrackingSnapshot tracking;
static Halo2CameraBasis carriers[2];
static weapon_muzzle::Store g_halo2Muzzles;
static thread_local bool g_halo2CollisionOwnQuery=false;
static bool Game_Halo2ControllerAimActive(){return controller;}
static bool Halo2Observer6Dof_FinalPaletteArmed(){return palette;}
static bool Halo2Observer6Dof_DirectWeaponAimArmed(){return direct;}
static uint32_t Halo2OwnedUnit(){return localUnit;}
static float Game_GetWorldScale(){return 1;}
static void* Halo2ObjectFromIndex(uint32_t unit)
{
    if(unit==localUnit)return storageReplaced?replacementBytes:unitBytes;
    return unit==0x11110004||unit==0x22220005?unitBytes:nullptr;
}
static bool Halo2DualWeaponOwned(uint32_t weapon,uint32_t unit)
{return owned&&unit==owner&&(weapon==primary||weapon==secondary);}
static bool Halo2Observer6Dof_ReadPublishedPose(Halo2ObserverPosePublication& out)
{out=publication;return publicationValid;}
static bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out)
{out=tracking;return trackingValid;}
static bool BuildStableFirstPersonCarriers(const Halo2ObserverPosePublication&,uint32_t,
    Halo2CameraBasis& a,Halo2CameraBasis& b,bool independent)
{Check(independent,"unblended per-hand carriers requested");a=carriers[0];b=carriers[1];return carrierValid;}
using Halo2AimAssistCalculateFn=void(__fastcall*)(uint32_t,float*,Halo2AimAssistTargetingResult*);
static std::atomic<uintptr_t> g_aimAssistOriginal{},g_aimAssistViewDirectionOriginal{1};
struct Halo2AimAssistControllerRayScope {bool active=false,applied=false;float direction[3]{};};
static thread_local Halo2AimAssistControllerRayScope g_aimAssistControllerRayScope;
#include "../src/dll/halo2_independent_query_context.inl"
using Fire=void(__fastcall*)(uint32_t,int16_t,int32_t,uint8_t);
using Aim=void(__fastcall*)(uint32_t,float*,float*,uint64_t,float*,uint8_t,uint8_t,uint8_t);
using Camera=int32_t(__fastcall*)(uint32_t,float*,float*);
using Location=const void*(__fastcall*)(uint32_t);
static struct
{
    uintptr_t base{};uint32_t generation=7;uint64_t installedAtMs=1;
    Fire fireOriginal{};Aim aimOriginal{};Camera cameraOriginal{};Location locationOriginal{};
    void* fireTarget{},*aimTarget{},*locationTarget{},*cameraTarget{};
    std::atomic<bool> enabled{true},faulted{false};std::atomic<uint32_t> callbacks{};
    std::atomic<uint64_t> primaryRays{},secondaryRays{},refused{},nativeQueries{},targetRestoresRefused{};
} g_halo2Dual;
static uintptr_t caller{};
#define _ReturnAddress() reinterpret_cast<void*>(::caller)
// T-2 sparse shot events. The recorder gate is stubbed and the one shared
// publisher (defined in game.cpp, reached here through the game.h door) is
// captured instead of published, so this suite proves the producer's count,
// firing-context and substitution flags directly. The gate stub is mutable so
// the recording-off path can be checked to publish nothing.
struct CapturedShot
{
    GameTitle title=GameTitle::None;uint32_t generation=0,unit=0,weapon=0;
    uint8_t slot=0,barrel=0;bool predicted=false,substituted=false;
    bool firesFromCamera=false,unitAim=false;float origin[3]{},direction[3]{};
};
static bool telemetryAccepting=true;
static unsigned shotEvents{};
static CapturedShot lastShot{};
bool Telemetry_WeaponEventsAccepting() noexcept {return telemetryAccepting;}
void Game_PublishShotDiagnostic(GameTitle title,uint32_t titleGeneration,
    uint32_t unit,uint32_t weapon,uint8_t slot,uint8_t barrel,bool predicted,
    bool substituted,bool firesFromCamera,bool unitAim,const float* origin,
    const float* direction) noexcept
{
    ++shotEvents;
    lastShot.title=title;lastShot.generation=titleGeneration;lastShot.unit=unit;
    lastShot.weapon=weapon;lastShot.slot=slot;lastShot.barrel=barrel;
    lastShot.predicted=predicted;lastShot.substituted=substituted;
    lastShot.firesFromCamera=firesFromCamera;lastShot.unitAim=unitAim;
    if(origin)std::memcpy(lastShot.origin,origin,sizeof(lastShot.origin));
    if(direction)std::memcpy(lastShot.direction,direction,sizeof(lastShot.direction));
}
#include "../src/dll/halo2_independent_shots.inl"
#undef _ReturnAddress

#include "halo2_independent_lifecycle_fixture.inl"
static unsigned fireCalls{},queryCalls{},cameraCalls{},locationCalls{},aimCalls{};
static uint32_t observedTarget[2]{};
static float observedRay[2][3]{};
static struct {float position[3]{0,0,1};uint32_t bsp=0xABCDEF12,cluster=0x456789AB;} location;
static const void* NativeLocation(uint32_t user)
{++locationCalls;Check(user==0,"native local input user");return &location;}
static int32_t NativeCamera(uint32_t,float* p,float* d)
{++cameraCalls;float position[]{0,0,1},direction[]{1,0,0};std::memcpy(p,position,12);std::memcpy(d,direction,12);return 0x12345678;}
static void NativeAim(uint32_t,float* p,float* d,uint64_t marker,float* offset,uint8_t project,uint8_t use,uint8_t collision)
{
    ++aimCalls;Check(marker==0x123456789ABCDEFull&&offset&&project==3&&use==4&&collision==5,"all eight aim arguments forwarded");
    float position[]{0,0,1},direction[]{1,0,0};std::memcpy(p,position,12);std::memcpy(d,direction,12);
}
static void NativeQuery(uint32_t user,float* control,Halo2AimAssistTargetingResult* target)
{
    ++queryCalls;
    Check(g_halo2CollisionOwnQuery,"auxiliary H2 targeting cannot schedule another collision/melee tick");
    if(queryFault)RaiseException(0xE0422222,0,0,nullptr);
    if(!omitLocation)
    {
        caller=g_halo2Dual.base+0x75932A;
        const void* result=Halo2IndependentLocationDetour(user);
        Check(result==&location&&location.bsp==0xABCDEF12&&location.cluster==0x456789AB,"native location pointer and BSP identity preserved");
    }
    if(!omitView)g_aimAssistControllerRayScope.applied=g_aimAssistControllerRayScope.active;
    const auto* ray=g_aimAssistControllerRayScope.direction;
    *target={};target->identifiers[0]=ray[0]>ray[1]?0x11110004:0x22220005;
    target->identifiers[1]=5;target->identifiers[2]=UINT32_MAX;
    target->magnetismHorizontal=.7f;target->magnetismVertical=.8f;
    control[0]=.1f;
    if(changeWeapons)*reinterpret_cast<uint32_t*>(users+kHalo2FirstPersonWeaponDataOffset+kHalo2FirstPersonWeaponSlotStride+kHalo2FirstPersonWeaponObjectOffset)=UINT32_MAX;
}
static void NativeFire(uint32_t weapon,int16_t barrel,int32_t projectile,uint8_t predicted)
{
    ++fireCalls;Check(barrel==-2&&projectile==-7&&predicted==0xFA,"full native firing arguments");
    const int slot=weapon==secondary?1:0;
    observedTarget[slot]=*reinterpret_cast<uint32_t*>(unitBytes+0x1D4);
    float origin[3]{},direction[3]{},offset[3]{};
    caller=g_halo2Dual.base+0x8E4FCD;
    Halo2IndependentAimDetour(owner,origin,direction,0x123456789ABCDEFull,offset,3,4,5);
    float cameraPosition[3]{},cameraDirection[3]{};
    caller=g_halo2Dual.base+0x7597BD;
    Check(Halo2IndependentCameraDetour(owner,cameraPosition,cameraDirection)==0x12345678,"native perspective return preserved");
    Check(cameraPosition[0]==0&&cameraPosition[1]==0&&cameraPosition[2]==1,"native assist origin retained");
    std::memcpy(observedRay[slot],cameraDirection,12);
    if(nested&&slot==0)
    {
        const auto outer=g_halo2IndependentShot;
        Halo2IndependentFireDetour(secondary,-2,-7,0xFA);
        Check(g_halo2IndependentShot.slot==outer.slot&&*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==observedTarget[0],"nested shot restores outer hand and target");
    }
    if(changeGeneration)g_generation=8;
    if(replaceStorage)storageReplaced=true;
    if(changeNativeTarget)*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)=0x33330006;
    if(fireFault)RaiseException(0xE0423333,0,0,nullptr);
}
static void Reset()
{
    controller=palette=direct=owned=publicationValid=trackingValid=carrierValid=true;
    storageReplaced=queryFault=fireFault=omitLocation=omitView=changeWeapons=changeGeneration=replaceStorage=changeNativeTarget=nested=false;
    localUnit=owner;g_generation=7;exclusive_input::active=false;
    g_halo2Dual.base=reinterpret_cast<uintptr_t>(&userPointer)-kHalo2FirstPersonUserDataPointerRva;
    g_halo2Dual.generation=7;g_halo2Dual.installedAtMs=1;g_halo2Dual.enabled=true;g_halo2Dual.faulted=false;
    g_halo2Dual.fireOriginal=NativeFire;g_halo2Dual.aimOriginal=NativeAim;
    g_halo2Dual.cameraOriginal=NativeCamera;g_halo2Dual.locationOriginal=NativeLocation;
    g_aimAssistOriginal=reinterpret_cast<uintptr_t>(&NativeQuery);g_aimAssistViewDirectionOriginal=1;
    g_config.independent_dual_aim=true;
    std::memset(users,0,sizeof(users));std::memset(unitBytes,0,sizeof(unitBytes));std::memset(replacementBytes,0,sizeof(replacementBytes));
    *reinterpret_cast<uint32_t*>(users+kHalo2FirstPersonWeaponDataOffset+kHalo2FirstPersonWeaponObjectOffset)=primary;
    *reinterpret_cast<uint32_t*>(users+kHalo2FirstPersonWeaponDataOffset+kHalo2FirstPersonWeaponSlotStride+kHalo2FirstPersonWeaponObjectOffset)=secondary;
    users[kHalo2FirstPersonWeaponDataOffset+kHalo2FirstPersonWeaponSlotStride]=1;
    *reinterpret_cast<uint32_t*>(unitBytes+0x260)=UINT32_MAX;
    *reinterpret_cast<uint32_t*>(unitBytes+0x14)=UINT32_MAX;
    *reinterpret_cast<int16_t*>(unitBytes+0x210)=-1;
    *reinterpret_cast<uint32_t*>(unitBytes+0x1D4)=0x44440007;
    publication={};publication.generation=7;publication.serial=11;
    publication.snapshot.valid=publication.snapshot.rightAimValid=publication.snapshot.leftControllerValid=true;
    publication.snapshot.trackingSpaceEpoch=3;publication.snapshot.predictedDisplayTimeNs=1000000000;
    tracking={};carriers[0]={{0,0,1},{1,0,0},{0,0,1}};carriers[1]={{0,0,1},{0,1,0},{0,0,1}};
    g_halo2IndependentShot={};g_aimAssistControllerRayScope={};
    RecordHalo2IndependentQuery();
    fireCalls=queryCalls=cameraCalls=locationCalls=aimCalls=0;
    std::memset(observedTarget,0,sizeof(observedTarget));
    telemetryAccepting=true;shotEvents=0;lastShot={};
}
static void Shoot(uint32_t weapon=primary){Halo2IndependentFireDetour(weapon,-2,-7,0xFA);}
static bool FaultingShot(){__try{Shoot();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
#include "halo2_muzzle_shots_fixture.inl"
#include "halo2_muzzle_lifecycle_fixture.inl"
int main()
{
    Reset();Shoot();Shoot(secondary);
    Check(observedTarget[0]==0x11110004&&observedTarget[1]==0x22220005,"each native target acquired from its own hand");
    Check(observedRay[0][0]==1&&observedRay[1][1]==1,"later native assist uses independent rays");
    Check(*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x44440007,"normal target restoration");
    Check(g_halo2Dual.callbacks==0&&!g_halo2IndependentShot.active,"scopes balanced");
    // T-2 shot evidence: exactly one event per firing invocation this detour
    // serves, published after the final ray is decided. The second shot above
    // is the secondary weapon's, so it carries that slot and the substituted
    // hand ray; the firing context is the engine's own fire arguments (barrel
    // -2 is not a barrel index, so it stays the unknown sentinel).
    Check(shotEvents==2&&lastShot.title==GameTitle::Halo2&&
        lastShot.generation==7&&lastShot.unit==owner&&
        lastShot.weapon==secondary&&lastShot.slot==1&&
        lastShot.barrel==kTelemetryShotIndexUnknown&&!lastShot.predicted&&
        lastShot.substituted&&lastShot.firesFromCamera&&lastShot.unitAim,
        "each firing invocation publishes one substituted shot with its firing context");
    Check(lastShot.origin[0]==0&&lastShot.origin[1]==0&&lastShot.origin[2]==1&&
        lastShot.direction[0]==0&&lastShot.direction[1]>0.99f,
        "the published shot carries the final ray the engine consumed");
    Reset();telemetryAccepting=false;Shoot();
    Check(shotEvents==0&&fireCalls==1&&queryCalls==1,
        "recording-off gate publishes nothing and changes no firing work");
    Reset();Shoot();
    Check(shotEvents==1&&lastShot.weapon==primary&&lastShot.slot==0&&
        lastShot.substituted,
        "an owned primary shot reports its own slot and substitution");
    Reset();std::swap(carriers[0],carriers[1]);Shoot();Shoot(secondary);
    Check(observedTarget[0]==0x22220005&&observedTarget[1]==0x11110004&&
        observedRay[0][1]==1&&observedRay[1][0]==1,"role-swapped handedness follows the new primary/support controllers");
    Reset();
    g_halo2IndependentShot={true,true,false,owner,0,carriers[1],10};
    caller=g_halo2Dual.base+0x75932B;
    Check(Halo2IndependentLocationDetour(0)==&location&&!g_halo2IndependentShot.locationApplied,"unrelated location caller remains native");
    g_halo2IndependentShot.query=false;
    float position[3]{},direction[3]{};
    caller=g_halo2Dual.base+0x7597BE;
    Check(Halo2IndependentCameraDetour(owner,position,direction)==0x12345678&&direction[0]==1,"unrelated camera caller remains native");
    caller=g_halo2Dual.base+0x7597BD;
    Check(Halo2IndependentCameraDetour(owner+0x10000,position,direction)==0x12345678&&direction[0]==1,"remote salted unit retains native camera ray");
    Reset();nested=true;Shoot();Check(fireCalls==2,"nested simultaneous roles fire once each");
    Reset();queryFault=true;Shoot();Check(fireCalls==1&&g_halo2Dual.faulted&&observedTarget[0]==0x44440007,"optional query failure is isolated");
    Reset();fireFault=true;Check(FaultingShot()&&fireCalls==1,"native firing exception never replayed");
    Check(g_halo2Dual.callbacks==0&&*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x44440007,"native exception cleans scopes and target");
    Reset();omitLocation=true;Shoot();Check(observedTarget[0]==0x44440007,"missing native location consumer refuses ownership");
    Reset();omitView=true;Shoot();Check(observedTarget[0]==0x44440007,"missing view consumer refuses ownership");
    Reset();changeWeapons=true;Shoot();Check(observedTarget[0]==0x44440007,"inventory replacement during acquisition refuses mutation");
    Reset();changeGeneration=true;Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x11110004,"generation change forbids stale restore");
    Reset();replaceStorage=true;Shoot();Check(*reinterpret_cast<uint32_t*>(replacementBytes+0x1D4)==0,"replacement storage never written");
    Reset();changeNativeTarget=true;Shoot();Check(*reinterpret_cast<uint32_t*>(unitBytes+0x1D4)==0x33330006,"native target changes win");
    for(int refusal=0;refusal<18;++refusal)
    {
        Reset();
        switch(refusal)
        {
        case 0:g_config.independent_dual_aim=false;break;
        case 1:controller=false;break;case 2:palette=false;break;case 3:direct=false;break;
        case 4:owned=false;break;case 5:trackingValid=false;break;case 6:publicationValid=false;break;
        case 7:carrierValid=false;break;case 8:tracking.referenceEpoch=4;break;
        case 9:tracking.hands[1].valid=false;break;case 10:g_halo2IndependentQueryContext.sampleMs=0;break;
        case 11:g_halo2IndependentQueryContext.sampleMs=GetTickCount64()+1000;break;
        case 12:g_halo2IndependentQueryContext.generation=6;break;
        case 13:g_halo2IndependentQueryContext.unit=0x12350001;break;
        case 14:*reinterpret_cast<uint32_t*>(unitBytes+0x260)=0xABCD0009;break;
        case 15:*reinterpret_cast<uint32_t*>(unitBytes+0x14)=0xABCD0009;*reinterpret_cast<int16_t*>(unitBytes+0x210)=1;break;
        case 16:exclusive_input::active=true;break;case 17:g_aimAssistViewDirectionOriginal=0;break;
        }
        Shoot();Check(queryCalls==0&&fireCalls==1&&observedTarget[0]==0x44440007,"unsafe scope forwards stock exactly once");
        Check(shotEvents==1&&!lastShot.substituted&&lastShot.weapon==primary&&
            lastShot.slot==kTelemetryShotIndexUnknown,
            "a refused acquisition still publishes the native firing invocation with unknown slot");
    }
    Reset();std::thread other([]{Check(g_halo2IndependentQueryContext.sampleMs==0&&!g_halo2IndependentShot.active,"query context cannot cross threads");});other.join();
    MuzzleTests();
    MuzzleLifecycleTests();
    LifecycleTests();
    std::printf("PASS: %u production H2 independent shot checks (native services stubbed)\n",checks);
}
