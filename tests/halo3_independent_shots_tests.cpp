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
#include "../src/common/seat_view_logic.h"

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
static struct {float crosshair_distance_m=10;bool independent_dual_aim=true,gun_barrel_aim=false,left_handed=false,
    shots_from_muzzle=false;} g_config;
// The three shot switches as they are in effect (sight_layer.h); here, the test's own config.
[[maybe_unused]] static bool Sight_ShotsFromMuzzle(){return g_config.shots_from_muzzle;}
[[maybe_unused]] static bool Sight_GunBarrelAim(){return g_config.gun_barrel_aim;}
[[maybe_unused]] static bool Sight_IndependentDualAim(){return g_config.independent_dual_aim;}
struct VrContactTrackingSnapshot
{uint64_t referenceEpoch=3;int64_t timeNs=1000000000;struct {bool valid=true;} hands[2];};
static VrContactTrackingSnapshot tracking;
static weapon_muzzle::Store g_halo3Muzzles;
static thread_local bool g_legacyCollisionOwnedQuery=false;
// Aim Frame Zoom's record of the game's zoom level (game.cpp); the lane only reports it.
static int g_notedZoomLevel=-2;
[[maybe_unused]] static void NoteGameZoomLevel(GameTitle,int16_t level,uint32_t){g_notedZoomLevel=level;}
static alignas(8) unsigned char unitBytes[0x400]{},replacementBytes[0x400]{};
static int32_t Player(int32_t){return int32_t(localUnit);}
static bool Seated(int32_t){return seated;}
static auto g_halo3PlayerUnitGetter=&Player;
static auto g_halo3UnitInVehicle=&Seated;
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
// The evaluator's chase path (seat_upstream_logic.h StockCamera): bound only from a matching evaluator branch.
using Halo3ChaseCameraFn=void(__fastcall*)(uint32_t,float*,float*);
static struct
{
    std::atomic<Halo3ChaseCameraFn> chaseCamera{nullptr};
    std::atomic<uint32_t> chaseGeneration{0};
    std::atomic<bool> chaseFaulted{false};
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
// T-2 sparse shot events: recording is never active under test, so the gate
// stays closed (exactly the gate-off production path) and the shared publisher
// (defined in game.cpp) is an inert stand-in here.
bool Telemetry_WeaponEventsAccepting() noexcept {return false;}
void PublishShotDiagnostic(GameTitle,uint32_t,uint32_t,uint32_t,uint8_t,uint8_t,
    bool,bool,bool,bool,const float*,const float*) noexcept {}
// The dev turret-shot witness (legacy_sight_trace.inl, defined in game.cpp): it only reads, so an inert stand-in.
// The firing call's weapon (halo3_dual_wield_runtime.inl declares it before including the lane).
static thread_local uint32_t g_halo3FiringWeapon=UINT32_MAX;
// A look-steered driver's gun band (halo3_vehicle_gun_band.inl): counted here, learned there.
static unsigned gunBandShots=0;
static void Halo3GunBandNoteShot(uint32_t,uint32_t,uint32_t,int16_t,const float*,const float*,bool){++gunBandShots;}
void LegacySightNoteTurretShot(GameTitle,uint32_t,const float*,const float*) {}
// The shot-lag fix (sight_layer.h / game.cpp): the fire's `predicted` flag rides a thread-local for the witness, and
// the acquisition's camera evaluator publishes the game's own eye. Both only read the native results.
enum class SightShotPath : uint8_t {Native=0,CrosshairAssist,Muzzle,UnitAdjust,Count};
static thread_local int t_legacyFirePredicted=-1;
static unsigned shotLagNotes{},tickEyes{};
static int lastNotedPredicted=-2;
static void Sight_NoteShotLagFromIntent(GameTitle,SightShotPath,const float*,const float*,int predicted,bool,int32_t)
{++shotLagNotes;lastNotedPredicted=predicted;}
static void PublishHalo3TickEye(uint32_t,int32_t,const float*){++tickEyes;}
// The acquisition's whole camera (seat_upstream_logic.h): position and direction, whichever path it took.
static unsigned acqCameras{};
static int32_t lastAcqPath=-9;
static void PublishHalo3AcquisitionCamera(uint32_t,int32_t,int32_t path,const float*,const float*)
{++acqCameras;lastAcqPath=path;}
static uint64_t Sight_NowUs(){return 1000000;}
// The local player's unit as the crosshair re-aims see it (-1: none, the default for every other check).
static int32_t localPlayerUnit=-1;
static int32_t LegacyCollisionIgnoredObject(GameTitle){return localPlayerUnit;}
// THE LOCAL PLAYER'S FIRE (shot_lag_logic.h FireMark; game.cpp t_halo3Fire): opened by the fire detour, marked by
// its shot line, restored at its exit.
#include "../src/common/shot_lag_logic.h"
static thread_local int8_t t_halo3Fire=shot_lag::kFireNone;
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
// The chase path's body scan (BindHalo3ChaseCamera): the first scan's hit and the second's (0 = unique).
static bool chaseScan=false;
static unsigned chaseScans{};
static uintptr_t chaseFirst{},chaseSecond{};
namespace sig
{
uintptr_t Find(uintptr_t base,size_t,const char*)
{
    if(chaseScan)return chaseScans++==0?chaseFirst:chaseSecond;
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
#define MCCVR_EnableHook FixtureEnable
#define MH_RemoveHook FixtureRemove
#define LOG(...) (++logged)
static bool RemoveHalo3Muzzle(){return true;}
static bool InstallHalo3Muzzle(uintptr_t,size_t){return false;}
#include "../src/dll/halo3_dual_lifecycle.inl"
#undef LOG
#undef MH_CreateHook
#undef MH_EnableHook
#undef MCCVR_EnableHook
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
    image[0x13BC08]^=1;

    // THE CHASE PATH: bound from the evaluator's body, with no hook of this lane installed.
    reset();
    const uint32_t chaseRva=0x2144C0;
    const unsigned char branch[]{0x85,0xC0,0x75,0x07,0xE8,0,0,0,0,0xEB,0x05,0xE8};
    std::memcpy(image+0x2121C7,branch,sizeof(branch));
    *reinterpret_cast<int32_t*>(image+0x2121C7+12)=int32_t(chaseRva-0x2121D7);
    edge(seat_view::kHalo3EvaluatorCallRva,seat_view::kHalo3ThirdPersonRva);
    const auto bind=[&](uintptr_t first,uintptr_t second,uint32_t generation)
    {
        chaseScan=true;chaseScans=0;chaseFirst=first;chaseSecond=second;
        BindHalo3ChaseCamera(lifecycleBase,size,generation);
        chaseScan=false;
        return g_halo3Dual.chaseCamera.load()!=nullptr;
    };
    const uintptr_t chaseAt=lifecycleBase+chaseRva;
    Check(bind(chaseAt,0,7)&&g_halo3Dual.chaseGeneration==7&&
        reinterpret_cast<uintptr_t>(g_halo3Dual.chaseCamera.load())==chaseAt&&createCalls==0,
        "the chase path binds from the evaluator's body with no hook installed");
    Check(!bind(chaseAt,0,0)&&g_halo3Dual.chaseGeneration==0,"no generation: unbound");
    Check(!bind(chaseAt,chaseAt+0x40,7),"an ambiguous body: unbound");
    Check(!bind(chaseAt+0x10,0,7),"a body found elsewhere: unbound");
    Check(!bind(0,0,7),"no body: unbound");
    image[0x2121C7+2]^=1;
    Check(!bind(chaseAt,0,7),"a changed branch: unbound");
    image[0x2121C7+2]^=1;
    edge(seat_view::kHalo3EvaluatorCallRva,0x1326F0);
    Check(!bind(chaseAt,0,7),"an evaluator that no longer calls the seat's third-person test: unbound");
    edge(seat_view::kHalo3EvaluatorCallRva,seat_view::kHalo3ThirdPersonRva);
    Check(bind(chaseAt,0,7),"bound again");
    // Its retirement: unbound at once; with no hook to wait on, an ask still inside holds it.
    g_halo3Dual.callbacks=1;
    Check(!RemoveHalo3DualAim()&&!g_halo3Dual.chaseCamera.load()&&g_halo3Dual.chaseGeneration==0,
        "retirement unbinds the chase path, and waits for an ask in flight though no hook is installed");
    g_halo3Dual.callbacks=0;
    Check(RemoveHalo3DualAim(),"...and completes once it has left");
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"private lifecycle image released");
}

// The chase path asked (Halo3AskChaseCamera): its own generation and fault latch, none of the lane's hooks.
static unsigned chaseCalls{};
static bool chaseFaults=false;
static void __fastcall NativeChase(uint32_t unit,float* position,float* direction)
{
    ++chaseCalls;
    if(chaseFaults)RaiseException(0xE0123458,0,0,nullptr);
    position[0]=float(unit);position[1]=2;position[2]=3;
    direction[0]=0;direction[1]=1;direction[2]=0;
}
static void ChaseAskTests()
{
    float position[3]{},direction[3]{};
    g_halo3Dual.callbacks=0;g_halo3Dual.enabled=false;g_halo3Dual.faulted=true;
    g_halo3Dual.chaseCamera=&NativeChase;g_halo3Dual.chaseGeneration=7;g_halo3Dual.chaseFaulted=false;
    g_halo3RuntimeGeneration=7;chaseCalls=0;chaseFaults=false;
    Check(Halo3AskChaseCamera(5,position,direction)&&chaseCalls==1&&position[0]==5.0f&&direction[1]==1.0f&&
        !g_halo3Dual.callbacks.load(),"the chase path answers with the lane's hooks out and the lane off");
    Check(!Halo3AskChaseCamera(-1,position,direction)&&chaseCalls==1,"no unit: not asked");
    g_halo3RuntimeGeneration=8;
    Check(!Halo3AskChaseCamera(5,position,direction)&&chaseCalls==1,"another generation: not asked");
    g_halo3RuntimeGeneration=7;
    g_halo3Dual.chaseGeneration=0;
    Check(!Halo3AskChaseCamera(5,position,direction)&&chaseCalls==1,"unbound generation: not asked");
    g_halo3Dual.chaseGeneration=7;
    chaseFaults=true;position[0]=-1;
    Check(!Halo3AskChaseCamera(5,position,direction)&&chaseCalls==2&&position[0]==-1.0f&&
        g_halo3Dual.chaseFaulted.load()&&!g_halo3Dual.callbacks.load(),"a fault answers nothing and is latched");
    chaseFaults=false;
    Check(!Halo3AskChaseCamera(5,position,direction)&&chaseCalls==2,"...and the path is not asked again");
    g_halo3Dual.chaseCamera=nullptr;g_halo3Dual.chaseGeneration=0;g_halo3Dual.chaseFaulted=false;
    g_halo3Dual.faulted=false;
}

static unsigned queryCalls{},fireCalls{},cameraCalls{},aimCalls{};
static bool queryFault=false,fireFault=false,omitCamera=false,changeGeneration=false,
    replaceStorage=false,changeNativeTarget=false,changeWeapons=false,nestedFire=false;
static uint32_t observedTargets[2]{};
static float observedDirections[2][3]{};
// The fire tag the firing assist sees (after the shot line), and whether another gun's line owned that assist.
static int8_t observedFire[2]{-9,-9};
static bool observedOwnLine[2]{};
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
    // The assist's crosshair query comes after the shot line: what the re-aim gate reads there.
    observedFire[slot]=t_halo3Fire;observedOwnLine[slot]=g_halo3IndependentShot.active;
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
    // THE LOCAL PLAYER'S FIRE and the own shot line, as the crosshair re-aim sees them at the assist.
    Reset();localPlayerUnit=static_cast<int32_t>(owner);(void)Shoot();
    Check(observedFire[0]==shot_lag::kFireLocal,"the local player's shot line tags his fire local before the assist");
    Check(observedOwnLine[0],"a dual-aim shot's assist runs under its own shot line (the re-aim leaves it alone)");
    Check(t_halo3Fire==shot_lag::kFireNone,"the fire's exit restores the tag");
    Reset();localPlayerUnit=static_cast<int32_t>(owner)+1;(void)Shoot();
    Check(observedFire[0]==shot_lag::kFireOther,"another unit's shot line leaves the fire another's (co-op, AI)");
    Reset();localPlayerUnit=static_cast<int32_t>(owner);g_config.independent_dual_aim=false;(void)Shoot();
    Check(observedFire[0]==shot_lag::kFireLocal&&!observedOwnLine[0],
          "a single-gun local shot is the one the re-aim may take");
    Reset();localPlayerUnit=static_cast<int32_t>(owner);nestedFire=true;(void)Shoot();
    Check(observedFire[1]==shot_lag::kFireLocal&&t_halo3Fire==shot_lag::kFireNone,
          "a nested fire tags its own shot and the outer exit restores none");
    localPlayerUnit=-1;
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
    ChaseAskTests();
    std::printf("PASS: %u production H3 independent shot checks (native services stubbed)\n",checks);
}
