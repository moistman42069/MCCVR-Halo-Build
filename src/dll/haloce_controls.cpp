#include "haloce_controls.h"
#include "physical_crouch_camera.h"
#include "../common/physical_crouch_native_read.h"
#include "../common/haloce_crouch_contract.h"
#include "../common/haloce_vehicle_identity_contract.h"
#include "../common/haloce_vehicle_camera_contract.h"
#include "native_vehicle_first_person.h"
#include "haloce_vehicle_cameras.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "hook_quiescence.h"
#include "title_adapter.h"
#include "../common/haloce_controls_logic.h"
#include "../common/minhook_lifecycle.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>

namespace
{
using namespace halo_ce;
using ObjectGetFn=uintptr_t(__fastcall*)(uint32_t,uint32_t);
using WeaponOwnerFn=uint32_t(__fastcall*)(uint32_t);
using PerspectiveFn=int16_t(__fastcall*)(int16_t);
using DatumGetFn=uintptr_t(__fastcall*)(uintptr_t,uint32_t);
using TurnFn=void(__fastcall*)(int32_t,float,float);
HMODULE retainedModule{};
uintptr_t moduleBase{};
void* turnTarget{};
void* turnOriginal{};
bool turnEnabled{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<bool> active{},retiring{},stateReady{},turnReady{};
std::atomic<bool> vehicleViewReady{};
std::atomic<uint64_t> lastOwned{},observed{},applied{},refused{},exceptions{};
uint32_t stateFailedGeneration{},turnFailedGeneration{};
double qpcSeconds{};
uint64_t lastReport{};
thread_local ControlTurnState turnState;
thread_local VehicleTurnFollow vehicleFollow;
struct Callback
{
    Callback() { callbacks.fetch_add(1,std::memory_order_acq_rel); }
    ~Callback() { callbacks.fetch_sub(1,std::memory_order_release); }
};
bool StateCurrent() noexcept
{
    return stateReady.load(std::memory_order_acquire)&&active.load(std::memory_order_acquire)&&
        !retiring.load(std::memory_order_acquire)&&TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
bool TurnCurrent() noexcept
{ return turnReady.load(std::memory_order_acquire)&&StateCurrent(); }
#include "haloce_vehicle_identity.inl"
// The vehicle cameras' seat facts (HaloCEControls_ReadVehicleSeatFacts): the tag path and seat-flag reads proven by
// their own contract (haloce_vehicle_camera_contract.h) at admission.
std::atomic<bool> vehicleCameraFactsReady{};
void PrepareVehicleCameraFacts(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const char* failure{};
    const NativeContractSet proof{vehicle_camera_contract::entries,vehicle_camera_contract::witnesses,
        vehicle_camera_contract::relatives,vehicle_camera_contract::pointers};
    const bool ready=VerifyNativeFeatureBindings(base,size,gen,proof,failure);
    vehicleCameraFactsReady.store(ready,std::memory_order_release);
    LOG("CE vehicle cameras' seat facts: %s%s%s",ready?"verified (tag path, seat flags, occupant slots)":"stock",
        failure?"; ":"",failure?failure:"");
}

static bool ReadNativePaused(bool& paused) noexcept
{
    if (!StateCurrent()) return false;
    const auto before=generation.load(std::memory_order_acquire);
    uint8_t value{};
    __try
    {
        const uintptr_t clock=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2e9fd68);
        if (!clock||*reinterpret_cast<const uint8_t*>(clock)!=1) return false;
        value=*reinterpret_cast<const uint8_t*>(clock+2);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (value>1||!StateCurrent()||before!=generation.load(std::memory_order_acquire)) return false;
    paused=value!=0;return true;
}

static bool ReadInputSuppressed(bool& suppressed) noexcept
{
    if (!StateCurrent()) return false;
    const auto before=generation.load(std::memory_order_acquire);
    bool candidate{};
    __try
    {
        const uintptr_t mapping=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2ea2d90);
        if (!mapping) return false;
        const uint32_t player=*reinterpret_cast<const uint32_t*>(mapping+0xb8);
        if (player==UINT32_MAX||!(player>>16)) return false;
        int input=-1;
        for (int i=0;i<4;++i)
            if (*reinterpret_cast<const uint32_t*>(mapping+4+i*4)==player)
            { if (input!=-1) return false;input=i; }
        if (input<0) return false;
        // Same E-CE-FP3 predicates consumed by A9915C; no unit lookup and no
        // stronger claim about why the native engine suppresses input.
        candidate=*reinterpret_cast<const uint8_t*>(moduleBase+0x2d9b960+input*0xf8+0x59)!=0||
            (*reinterpret_cast<const int32_t*>(moduleBase+0x1c34fc8)==0&&
             *reinterpret_cast<const int32_t*>(moduleBase+0x1b85760)!=-1);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (!StateCurrent()||before!=generation.load(std::memory_order_acquire)) return false;
    suppressed=candidate;return true;
}

static bool ReadLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{
    if (!StateCurrent()) return false;
    HaloCELocalPlayerState candidate{};
    __try
    {
        candidate.generation=generation.load(std::memory_order_acquire);
        const uintptr_t mapping=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2ea2d90);
        const uintptr_t control=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2d8fe70);
        const uintptr_t players=*reinterpret_cast<const uintptr_t*>(moduleBase+0x1c40480);
        const uintptr_t clock=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2e9fd68);
        const uintptr_t cinematic=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2ea0208);
        if (!mapping||!control||!players||!clock||!cinematic||
            *reinterpret_cast<const uint8_t*>(clock)!=1) return false;
        candidate.nativePaused=*reinterpret_cast<const uint8_t*>(clock+2)!=0;
        candidate.nativeCinematicFlag=*reinterpret_cast<const uint8_t*>(cinematic+0xa)!=0;
        candidate.player=*reinterpret_cast<const uint32_t*>(mapping+0xb8);
        // The native table API accepts salt zero as an index lookup. Our
        // published ownership requires a full salted datum instead.
        if (candidate.player==0xffffffffu||!(candidate.player>>16)||
            *reinterpret_cast<const uint16_t*>(players+0x22)!=0xc20) return false;
        const uintptr_t player=reinterpret_cast<DatumGetFn>(
            moduleBase+contract::player_state::state_datum_get)(players,candidate.player);
        if (!player) return false;
        candidate.unit=*reinterpret_cast<const uint32_t*>(player+0x64);
        // Match the native input->output routing by complete player identity.
        for (int16_t input=0;input<4;++input)
            if (*reinterpret_cast<const uint32_t*>(mapping+4+input*4)==candidate.player)
            {
                if (candidate.inputUser!=-1) return false;
                candidate.inputUser=input;
            }
        if (candidate.inputUser<0) return false;
        const uintptr_t director=moduleBase+0x2d9b960+size_t(candidate.inputUser)*0xf8;
        candidate.nativeLookBlocked=*reinterpret_cast<const uint8_t*>(director+0x58)!=0;
        candidate.nativeInputBlocked=*reinterpret_cast<const uint8_t*>(director+0x59)!=0||
            (*reinterpret_cast<const int32_t*>(moduleBase+0x1c34fc8)==0&&
             *reinterpret_cast<const int32_t*>(moduleBase+0x1b85760)!=-1);
        candidate.nativePerspective=reinterpret_cast<PerspectiveFn>(
            moduleBase+contract::player_state::state_user_perspective)(0);
        if (candidate.nativePerspective<0||candidate.nativePerspective>3) return false;
        candidate.nativePreparesFirstPerson=candidate.nativePerspective==0;
        if (*reinterpret_cast<const uint32_t*>(control+0x170)!=candidate.unit||
            *reinterpret_cast<const uint32_t*>(control+0x10+size_t(candidate.inputUser)*0x58)!=candidate.unit)
            return false;
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        if (candidate.unit!=0xffffffffu)
        {
            if (!(candidate.unit>>16)) return false;
            const uintptr_t unit=get(candidate.unit,3);
            if (!unit||*reinterpret_cast<const uint32_t*>(unit+0x1f8)!=candidate.player) return false;
            candidate.parent=*reinterpret_cast<const uint32_t*>(unit+0xd8);
            candidate.onFoot=candidate.parent==0xffffffffu;
            candidate.hasControlledUnit=true;
            const uintptr_t users=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2d9cd90);
            if (users)
            {
                const uint32_t weapon=*reinterpret_cast<const uint32_t*>(users+8);
                candidate.hasFirstPersonUserRecord=true;
                candidate.weaponSlotPresent=weapon!=0xffffffffu;
                candidate.firstPersonVisible=(*reinterpret_cast<const uint8_t*>(users)&1)!=0;
                if (weapon!=0xffffffffu&&get(weapon,4)&&
                    reinterpret_cast<WeaponOwnerFn>(moduleBase+contract::player_state::state_weapon_owner)(weapon)==candidate.unit)
                    candidate.weapon=weapon;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (!StateCurrent()||candidate.generation!=generation.load(std::memory_order_acquire)) return false;
    state=candidate;return true;
}

bool AdmittedContext(HaloCELocalPlayerState& state,RenderContext& context) noexcept
{
    if (!ReadLocalPlayerState(state)||!HaloCE_GetGameplayContext(context)||
        state.generation!=context.tracking.generation||!context.tracking.controllers.padValid) return false;
    return OnFootControls({state.hasControlledUnit,state.onFoot,state.nativePreparesFirstPerson,
        state.nativeInputBlocked,state.nativeLookBlocked,state.nativePaused,state.nativeCinematicFlag,
        context.tracking.controllers.controlsPresentationBlocked});
}

bool VehicleTurnFrame(const HaloCELocalPlayerState& state,const RenderContext& context,
    int16_t& seat,float& hullYaw) noexcept
{
    if (!vehicleViewReady.load(std::memory_order_acquire)||!StateCurrent()||
        !context.tracking.controllers.vehicleControls||!context.tracking.controllers.padValid||
        context.tracking.controllers.controlsPresentationBlocked||
        state.generation!=context.tracking.generation||!state.hasControlledUnit||state.onFoot||
        (state.nativePerspective!=0&&state.nativePerspective!=1)||state.parent==UINT32_MAX||!(state.parent>>16)||
        state.nativeInputBlocked||state.nativeLookBlocked||state.nativePaused||state.nativeCinematicFlag)
        return false;
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t occupant=get(state.unit,1),parent=get(state.parent,2);
        if (!occupant||!parent||*reinterpret_cast<const uint32_t*>(occupant+0xd8)!=state.parent||
            *reinterpret_cast<const uint32_t*>(parent+0xd8)!=UINT32_MAX) return false;
        // HCEEK vehicle physics 8E1B60 and retail B45178 pass these actual
        // orientation vectors to their matrix builders. No desired aim read.
        const Vec3 forward=*reinterpret_cast<const Vec3*>(parent+0x30);
        const Vec3 up=*reinterpret_cast<const Vec3*>(parent+0x3c);
        seat=*reinterpret_cast<const int16_t*>(occupant+0x2d0);
        if (seat<0||!Finite(forward)||!Finite(up)||
            std::fabs(Dot(forward,forward)-1)>0.05f||
            std::fabs(Dot(up,up)-1)>0.05f||std::fabs(Dot(forward,up))>0.05f||
            forward.x*forward.x+forward.y*forward.y<0.0001f) return false;
        hullYaw=std::atan2(forward.y,forward.x);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    return StateCurrent()&&state.generation==generation.load(std::memory_order_acquire)&&
        HaloCE_RenderContextCurrent(context);
}

bool AdmittedTurnContext(HaloCELocalPlayerState& state,RenderContext& context,
    bool& seated,int16_t& seat,float& hullYaw) noexcept
{
    seated=false;
    if (AdmittedContext(state,context)) return true;
    seated=VehicleTurnFrame(state,context,seat,hullYaw);
    return seated;
}

void TurnBody(int32_t inputUser,float yawDelta,float pitchDelta,uintptr_t caller)
{
    const auto original=reinterpret_cast<TurnFn>(turnOriginal);
    if (!original) return;
    if (caller!=moduleBase+0xa99660||!TurnCurrent())
    { original(inputUser,yawDelta,pitchDelta);return; }
    observed.fetch_add(1,std::memory_order_relaxed);
    HaloCELocalPlayerState state{};RenderContext context{};
    LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);
    const double nowSeconds=static_cast<double>(counter.QuadPart)*qpcSeconds;
    bool seated=false;int16_t seat=-1;float hullYaw{};
    const bool admitted=AdmittedTurnContext(state,context,seated,seat,hullYaw);
    if (state.inputUser>=0&&inputUser!=state.inputUser)
    { original(inputUser,yawDelta,pitchDelta);return; }
    if (!admitted)
    {
        (void)turnState.Step(context,false,nowSeconds);
        vehicleFollow={};
        lastOwned=0;refused.fetch_add(1,std::memory_order_relaxed);
        original(inputUser,yawDelta,pitchDelta);return;
    }
    float turn=turnState.Step(context,true,nowSeconds,seated);
    if (seated)
    {
        // A chase camera of the vehicle camera file turns the view by its own mode's rotationTracking yaw in its
        // seat (haloce_vehicle_cameras.h); every other seat view by the setting, vehicle_view_follow.
        RenderContext follow=context;bool yaw=false;
        if (Game_HaloCEVehicleCameraFollow(state.generation,state.parent,seat,yaw))
            follow.tracking.controllers.vehicleViewFollow=yaw;
        turn+=vehicleFollow.Step(follow,state.unit,state.parent,seat,hullYaw,nowSeconds);
    }
    else vehicleFollow={};
    if (!TurnCurrent()||!HaloCE_RenderContextCurrent(context))
    { lastOwned=0;original(inputUser,yawDelta,pitchDelta);return; }
    // Let the native updater normalize yaw and apply its own angular clamps.
    // HMD pitch is already part of the render pose; raw native pitch
    // would create a second independently accumulated look rotation.
    original(inputUser,turn,0.0f);
    lastOwned.store(GetTickCount64(),std::memory_order_release);
    applied.fetch_add(1,std::memory_order_relaxed);
}

void TurnDispatch(int32_t inputUser,float yawDelta,float pitchDelta,uintptr_t caller)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { TurnBody(inputUser,yawDelta,pitchDelta,caller); }
    __finally
    {
        if (AbnormalTermination())
        { lastOwned=0;exceptions.fetch_add(1,std::memory_order_relaxed); }
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
__declspec(noinline) void __fastcall TurnHook(int32_t inputUser,float yawDelta,float pitchDelta)
{
    // Own the cleanup boundary at the actual hook entry. A wrapper tail-call
    // to TurnDispatch compiles as a leaf without x64 unwind metadata; the
    // retirement range resolver then rejects it forever after one camera gap.
    // Retain the older dispatch below the feature boundary for its existing
    // direct fixture coverage, but keep native caller identity at this entry.
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { TurnBody(inputUser,yawDelta,pitchDelta,caller); }
    __finally
    {
        if (AbnormalTermination())
        { lastOwned=0;exceptions.fetch_add(1,std::memory_order_relaxed); }
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}

bool Remove() noexcept
{
    active=false;retiring=true;stateReady=false;turnReady=false;vehicleViewReady=false;vehicleIdentityReady=false;
    vehicleCameraFactsReady=false;lastOwned=0;
    if (turnTarget&&turnEnabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(turnTarget);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        turnEnabled=false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&TurnHook),
        reinterpret_cast<const void*>(&TurnDispatch),
        reinterpret_cast<const void*>(&TurnBody),
        reinterpret_cast<const void*>(&HaloCEControls_GetLocalPlayerState),
        reinterpret_cast<const void*>(&HaloCEControls_OwnsLookStick),
        reinterpret_cast<const void*>(&HaloCEControls_MapMoveStick),
        reinterpret_cast<const void*>(&HaloCEControls_GetLocomotionFrame),
        reinterpret_cast<const void*>(&HaloCEControls_GetNativePaused),
        reinterpret_cast<const void*>(&HaloCEControls_PhysicalCrouchCorrection),
        reinterpret_cast<const void*>(&HaloCEControls_ReadVehicleCameraOwner),
        reinterpret_cast<const void*>(&HaloCEControls_SeatHeading),
        reinterpret_cast<const void*>(&HaloCEControls_ReadVehicleSeatFacts),
        reinterpret_cast<const void*>(&HaloCEControls_ReadVehicleSeatNow),
        reinterpret_cast<const void*>(&HaloCEControls_ReadVehicleSeatTrack)};
    const void* trampolines[]{turnOriginal,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
        nullptr,nullptr,nullptr};
    if (!WaitForNativeDetourQuiescence(functions,trampolines,std::size(functions),callbacks)) return false;
    if (turnTarget&&MH_RemoveHook(turnTarget)!=MH_OK) return false;
    turnTarget=turnOriginal=nullptr;
    if (retainedModule) { FreeLibrary(retainedModule);retainedModule=nullptr; }
    moduleBase=0;generation=0;retiring=false;return true;
}
#include "haloce_physical_crouch.inl"

bool InstallState(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const char* failure{};
    const NativeContractSet contracts{contract::player_state::entries,
        contract::player_state::witnesses,contract::player_state::relatives,contract::player_state::pointers};
    if (!VerifyNativeFeatureBindings(base,size,gen,contracts,failure))
    { LOG("CE native control state unavailable: %s",failure?failure:"binding failure");return false; }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(base),&retainedModule)) return false;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart<=0) { FreeLibrary(retainedModule);retainedModule=nullptr;return false; }
    qpcSeconds=1.0/static_cast<double>(frequency.QuadPart);
    PrepareCrouchCamera(base,size,gen);
    PrepareVehicleIdentity(base,size,gen);
    PrepareVehicleCameraFacts(base,size,gen);
    moduleBase=base;generation=gen;active=true;retiring=false;stateReady=true;
    LOG("CE native control state verified independently: player/input/output ownership and native admission flags");
    return true;
}
bool InstallTurn(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const char* failure{};
    const NativeContractSet contracts{contract::controls::entries,
        contract::controls::witnesses,contract::controls::relatives,contract::controls::pointers};
    if (!VerifyNativeFeatureBindings(base,size,gen,contracts,failure))
    { LOG("CE VR turn stock fallback: %s",failure?failure:"binding failure");return false; }
    const NativeContractSet vehicleContracts{contract::vehicle::entries,contract::vehicle::witnesses,
        contract::vehicle::relatives,contract::vehicle::pointers};
    const NativeContractSet viewContracts{contract::vehicle_view::entries,contract::vehicle_view::witnesses,
        contract::vehicle_view::relatives,contract::vehicle_view::pointers};
    vehicleViewReady=VerifyNativeFeatureBindings(base,size,gen,vehicleContracts,failure)&&
        VerifyNativeFeatureBindings(base,size,gen,viewContracts,failure);
    if (!vehicleViewReady.load())
        LOG("CE vehicle view stock fallback: %s; on-foot turn and accepted steering retained",failure?failure:"binding failure");
    void* target=reinterpret_cast<void*>(base+contract::controls::input_angle_delta);
    auto result=MH_CreateHook(target,reinterpret_cast<void*>(&TurnHook),&turnOriginal);
    if (result!=MH_OK)
    { LOG("CE VR turn stock fallback: create status %d",result);return false; }
    turnTarget=target;
    result=MH_EnableHook(target);
    if (result!=MH_OK)
    {
        LOG("CE VR turn stock fallback: enable status %d",result);
        // Keep an unenabled failed entry retained until ordinary retirement;
        // state readers remain independently available.
        return false;
    }
    turnEnabled=true;turnReady=true;
    LOG("CE VR turn installed: exact native input delta phase, shared snap latch/smooth rate; vehicle view=%d (optional actual hull yaw follow)",vehicleViewReady.load()?1:0);
    return true;
}
}

bool HaloCEControls_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    active.store(isActive,std::memory_order_release);
    if (retainedModule&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring.load()))
        if (!Remove()) return false;
    if (!isActive||!base||!gen) return false;
    if (!stateReady.load()&&gen!=stateFailedGeneration)
        if (!InstallState(base,size,gen)) stateFailedGeneration=gen;
    if (stateReady.load()&&!turnReady.load()&&gen!=turnFailedGeneration)
        if (!InstallTurn(base,size,gen)) turnFailedGeneration=gen;
    const uint64_t now=GetTickCount64();
    if (now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE controls gen=%u state=%d turn=%d observed=%llu applied=%llu stock=%llu exceptions=%llu",
            gen,stateReady.load(),turnReady.load(),observed.load(),applied.load(),refused.load(),exceptions.load());
    }
    return TurnCurrent();
}
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{ Callback callback;return ReadLocalPlayerState(state); }
static bool ReadVehicleCameraOwnerBody(NativeVehicleCameraOwner& owner) noexcept
{
    HaloCELocalPlayerState state{};
    if (!vehicleViewReady.load(std::memory_order_acquire)||!ReadLocalPlayerState(state)||
        !state.hasControlledUnit||state.onFoot||state.nativeInputBlocked||state.nativeLookBlocked||
        state.nativePaused||state.nativeCinematicFlag||
        (state.nativePerspective!=0&&state.nativePerspective!=1)) return false;
    __try {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t unit=get(state.unit,1),parent=get(state.parent,2);
        if (!unit||!parent||*reinterpret_cast<const uint32_t*>(unit+0xd8)!=state.parent) return false;
        const auto seat=*reinterpret_cast<const int16_t*>(unit+0x2d0);
        if (seat<0||!StateCurrent()||state.generation!=generation.load(std::memory_order_acquire)) return false;
        const uint64_t identity=ReadVehicleModelIdentity(parent);
        if(get(state.unit,1)!=unit||get(state.parent,2)!=parent||
            *reinterpret_cast<const uint32_t*>(unit+0xd8)!=state.parent||
            *reinterpret_cast<const int16_t*>(unit+0x2d0)!=seat||!StateCurrent()||
            state.generation!=generation.load(std::memory_order_acquire))return false;
        owner={state.generation,state.unit,state.parent,seat,unit,identity};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool HaloCEControls_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept
{ Callback callback;return ReadVehicleCameraOwnerBody(owner); }
// THE SEAT'S FACTS for the vehicle cameras (haloce_controls.h). Every address is one vehicle_camera_contract proved at
// admission (vehicleCameraFactsReady); every pointer is the engine's own accessor's answer, read guarded, and the
// owner is checked again after the reads.
static HaloCEVehicleSeatRead ReadVehicleSeatFactsBody(HaloCEVehicleSeatFacts& facts) noexcept
{
    HaloCELocalPlayerState state{};
    if (!ReadLocalPlayerState(state)) return HaloCEVehicleSeatRead::Unknown;
    if (!state.hasControlledUnit||state.onFoot) return HaloCEVehicleSeatRead::OnFoot;
    if (!vehicleCameraFactsReady.load(std::memory_order_acquire)||state.parent==UINT32_MAX||!(state.parent>>16))
        return HaloCEVehicleSeatRead::Unknown;
    HaloCEVehicleSeatFacts out{};
    out.generation=state.generation;out.unit=state.unit;out.parent=state.parent;
    uintptr_t parentObject{};
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t unit=get(state.unit,1),parent=get(state.parent,2);
        if (!unit||!parent||*reinterpret_cast<const uint32_t*>(unit+0xd8)!=state.parent)
            return HaloCEVehicleSeatRead::Unknown;
        parentObject=parent;
        out.seat=*reinterpret_cast<const int16_t*>(unit+0x2d0);
        if (out.seat<0) return HaloCEVehicleSeatRead::Unknown;
        out.definition=*reinterpret_cast<const uint32_t*>(parent);
        // E-CE-VEHICLE-CONTROL-1: the vehicle takes facing and throttle from the occupant at +0x304, its aim from +0x308.
        out.drivesIt=*reinterpret_cast<const uint32_t*>(parent+0x304)==state.unit;
        out.aimsIt=*reinterpret_cast<const uint32_t*>(parent+0x308)==state.unit;
        // The hierarchy's top: the vehicle itself unless it rides another object. Bounded.
        uint32_t at=state.parent;
        for (int depth=0;depth<8;++depth)
        {
            const uintptr_t object=get(at,UINT32_MAX);
            if (!object) break;
            const uint32_t up=*reinterpret_cast<const uint32_t*>(object+0xd8);
            if (up==UINT32_MAX) { out.root=at;break; }
            at=up;
        }
        const intptr_t virtualBase=*reinterpret_cast<const intptr_t*>(moduleBase+vehicle_camera_contract::cacheVirtualBase);
        const intptr_t mappedBase=*reinterpret_cast<const intptr_t*>(moduleBase+vehicle_camera_contract::cacheMappedBase);
        if (out.definition!=UINT32_MAX&&(out.definition&0xffffu)<0x8000u&&mappedBase>0&&virtualBase>=0)
        {
            using TagGet=uintptr_t(__fastcall*)(uint32_t);
            const uintptr_t definition=reinterpret_cast<TagGet>(moduleBase+vehicle_camera_contract::tag)(out.definition);
            if (definition)
            {
                // object_definition: bounding radius +4 (world units).
                const float radius=*reinterpret_cast<const float*>(definition+4);
                if (std::isfinite(radius)&&radius>0&&radius<1000) out.boundsRadius=radius;
                // unit_definition seats: a block at +0x2E4 (count) / +0x2E8 (cache pointer), 0x11C a seat, flags at +0.
                const int32_t count=*reinterpret_cast<const int32_t*>(definition+0x2e4);
                const int32_t address=*reinterpret_cast<const int32_t*>(definition+0x2e8);
                if (count>0&&count<=64&&out.seat<count&&address)
                {
                    const uintptr_t seats=static_cast<uintptr_t>(static_cast<intptr_t>(address)-virtualBase+mappedBase);
                    out.seatFlags=*reinterpret_cast<const uint32_t*>(seats+size_t(out.seat)*0x11c);
                    out.seatFlagsRead=true;
                }
            }
            // The tag instance's path: entry +0x10 of the 0x20-byte instance, a cache pointer like its data's.
            const uintptr_t table=*reinterpret_cast<const uintptr_t*>(moduleBase+vehicle_camera_contract::tagInstances);
            if (table)
            {
                const int32_t path=*reinterpret_cast<const int32_t*>(table+size_t(out.definition&0xffffu)*0x20+0x10);
                if (path)
                {
                    const auto* text=reinterpret_cast<const char*>(static_cast<intptr_t>(path)-virtualBase+mappedBase);
                    size_t n=0;
                    for (;n+1<sizeof(out.tagPath)&&text[n];++n)
                    {
                        const unsigned char c=static_cast<unsigned char>(text[n]);
                        if (c<32||c>126) { n=0;break; }
                        out.tagPath[n]=text[n];
                    }
                    out.tagPath[n]='\0';
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return HaloCEVehicleSeatRead::Unknown; }
    out.modelIdentity=ReadVehicleModelIdentity(parentObject);
    if (!StateCurrent()||state.generation!=generation.load(std::memory_order_acquire))
        return HaloCEVehicleSeatRead::Unknown;
    facts=out;
    return HaloCEVehicleSeatRead::Seated;
}
HaloCEVehicleSeatRead HaloCEControls_ReadVehicleSeatFacts(HaloCEVehicleSeatFacts& facts) noexcept
{ Callback callback;return ReadVehicleSeatFactsBody(facts); }
static bool ReadVehicleSeatNowBody(HaloCEVehicleSeatNow& seat) noexcept
{
    HaloCELocalPlayerState state{};
    if (!vehicleCameraFactsReady.load(std::memory_order_acquire)||!ReadLocalPlayerState(state)||
        !state.hasControlledUnit||state.onFoot||state.nativeCinematicFlag||state.parent==UINT32_MAX||
        !(state.parent>>16))
        return false;
    HaloCEVehicleSeatNow out{};
    out.generation=state.generation;out.unit=state.unit;out.parent=state.parent;
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t unit=get(state.unit,1),parent=get(state.parent,2);
        if (!unit||!parent||*reinterpret_cast<const uint32_t*>(unit+0xd8)!=state.parent) return false;
        out.seat=*reinterpret_cast<const int16_t*>(unit+0x2d0);
        if (out.seat<0) return false;
        out.definition=*reinterpret_cast<const uint32_t*>(parent);
        uint32_t at=state.parent;
        for (int depth=0;depth<8;++depth)
        {
            const uintptr_t object=get(at,UINT32_MAX);
            if (!object) break;
            const uint32_t up=*reinterpret_cast<const uint32_t*>(object+0xd8);
            if (up==UINT32_MAX) { out.root=at;break; }
            at=up;
        }
        if (out.definition!=UINT32_MAX&&(out.definition&0xffffu)<0x8000u)
        {
            using TagGet=uintptr_t(__fastcall*)(uint32_t);
            const uintptr_t definition=reinterpret_cast<TagGet>(moduleBase+vehicle_camera_contract::tag)(out.definition);
            const float radius=definition?*reinterpret_cast<const float*>(definition+4):0.0f;
            if (std::isfinite(radius)&&radius>0&&radius<1000) out.boundsRadius=radius;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (out.root==UINT32_MAX||!StateCurrent()||state.generation!=generation.load(std::memory_order_acquire))
        return false;
    seat=out;
    return true;
}
bool HaloCEControls_ReadVehicleSeatNow(HaloCEVehicleSeatNow& seat) noexcept
{ Callback callback;return ReadVehicleSeatNowBody(seat); }
// THE SEAT CAMERA'S TRACK (haloce_controls.h). Every address and field is one vehicle_camera_contract proved at
// admission: the seat camera (unit_get_camera_info), its tracks and the default (camera_track_splut). Guarded.
static bool ReadVehicleSeatTrackBody(uint32_t gen,uint32_t parentDatum,int16_t seat,HaloCEVehicleSeatTrack& out) noexcept
{
    if (!vehicleCameraFactsReady.load(std::memory_order_acquire)||!StateCurrent()||
        gen!=generation.load(std::memory_order_acquire)||seat<0||parentDatum==UINT32_MAX||!(parentDatum>>16))
        return false;
    HaloCEVehicleSeatTrack read{};
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t parent=get(parentDatum,2);
        if (!parent) return false;
        const uint32_t definitionDatum=*reinterpret_cast<const uint32_t*>(parent);
        const intptr_t virtualBase=*reinterpret_cast<const intptr_t*>(moduleBase+vehicle_camera_contract::cacheVirtualBase);
        const intptr_t mappedBase=*reinterpret_cast<const intptr_t*>(moduleBase+vehicle_camera_contract::cacheMappedBase);
        if (definitionDatum==UINT32_MAX||(definitionDatum&0xffffu)>=0x8000u||mappedBase<=0||virtualBase<0) return false;
        const auto relocate=[virtualBase,mappedBase](int32_t address) noexcept {
            return address?static_cast<uintptr_t>(static_cast<intptr_t>(address)-virtualBase+mappedBase):uintptr_t{0}; };
        using TagGet=uintptr_t(__fastcall*)(uint32_t);
        const auto tag=reinterpret_cast<TagGet>(moduleBase+vehicle_camera_contract::tag);
        const uintptr_t definition=tag(definitionDatum);
        if (!definition) return false;
        // The seat's camera: the vehicle definition's seat block (+0x2E4 count, +0x2E8 address, 0x11C a seat) at +0x84.
        const int32_t seats=*reinterpret_cast<const int32_t*>(definition+0x2e4);
        const uintptr_t seatBlock=relocate(*reinterpret_cast<const int32_t*>(definition+0x2e8));
        if (seats<=0||seats>64||seat>=seats||!seatBlock) return false;
        const uintptr_t camera=seatBlock+size_t(seat)*0x11c+0x84;
        // Its look limits (+0x44 lowest, +0x48 highest), as the player's look update holds the pitch to them.
        read.pitchMin=*reinterpret_cast<const float*>(camera+0x44);
        read.pitchMax=*reinterpret_cast<const float*>(camera+0x48);
        // Its first track's reference (+0x4C count, +0x50 address, the datum at +0xC), else the globals' default.
        read.tracks=*reinterpret_cast<const int32_t*>(camera+0x4c);
        uint32_t trackDatum=UINT32_MAX;
        if (read.tracks>0)
        {
            const uintptr_t first=relocate(*reinterpret_cast<const int32_t*>(camera+0x50));
            if (first) trackDatum=*reinterpret_cast<const uint32_t*>(first+0xc);
        }
        if (trackDatum==UINT32_MAX)
        {
            const uintptr_t globals=*reinterpret_cast<const uintptr_t*>(moduleBase+vehicle_camera_contract::globals);
            const uintptr_t reference=globals?relocate(*reinterpret_cast<const int32_t*>(globals+0x108)):0;
            if (!reference) return false;
            trackDatum=*reinterpret_cast<const uint32_t*>(reference+0xc);
            read.defaultTrack=true;
        }
        if (trackDatum==UINT32_MAX||(trackDatum&0xffffu)>=0x8000u) return false;
        const uintptr_t track=tag(trackDatum);
        if (!track) return false;
        // The track's control points: +4 count, +8 address, 0x3C a point (position, then orientation i j k w).
        read.points=*reinterpret_cast<const int32_t*>(track+4);
        const uintptr_t points=relocate(*reinterpret_cast<const int32_t*>(track+8));
        if (points&&read.points>=vehicle_boom::kMinTrackPoints&&read.points<=vehicle_boom::kMaxTrackPoints)
        {
            for (int32_t i=0;i<read.points;++i)
            {
                const auto* point=reinterpret_cast<const float*>(points+size_t(i)*0x3c);
                for (int k=0;k<3;++k) read.track.points[i].position[k]=point[k];
                for (int k=0;k<4;++k) read.track.points[i].orientation[k]=point[3+k];
            }
            read.track.count=read.points;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (!StateCurrent()||gen!=generation.load(std::memory_order_acquire)) return false;
    out=read;
    return true;
}
bool HaloCEControls_ReadVehicleSeatTrack(uint32_t gen,uint32_t parent,int16_t seat,HaloCEVehicleSeatTrack& track) noexcept
{ Callback callback;return ReadVehicleSeatTrackBody(gen,parent,seat,track); }
// THE SEAT'S VIEW HEADING (halo_ce::Reference::seatHeading; haloce_render_logic.h has the why and the measurement).
// The player's control record for input user N sits at control + 0x14 + N * 0x58, with its unit handle 4 bytes
// before it; its yaw and pitch are the two floats at +8 / +0xC -- the pair CE's own action builder copies into the
// outgoing action's +4 / +8 (contract::network_input::action_build: `mov rax,[rcx+8] ... mov [rdx+4],rax`), and the
// pair the look update this file hooks (TurnBody) advances. World angles: the seat transform CE applies afterwards
// (0xB0584C) only tilts a direction by the vehicle's up, and not at all for a seat whose tag has bit 4.
// ADDR-HYGIENE: guarded -- the record's unit handle must be the local player's unit (ReadLocalPlayerState already
// requires control + 0x10 + N * 0x58 == unit and control + 0x170 == unit; re-read here), the yaw must be finite
// and inside [-2pi, 4pi], and the pitch finite; anything else answers "no heading" and the native camera's is used.
static bool SeatHeadingBody(uint32_t expectedGeneration,halo_ce::Vec3& heading) noexcept
{
    HaloCELocalPlayerState state{};
    if (!ReadLocalPlayerState(state)||state.generation!=expectedGeneration||!state.hasControlledUnit||
        state.onFoot||state.parent==UINT32_MAX||!(state.parent>>16)||state.inputUser<0||state.inputUser>=4)
        return false;
    float yaw{},pitch{};
    __try
    {
        const uintptr_t control=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2d8fe70);
        if (!control) return false;
        const uintptr_t record=control+0x14+size_t(state.inputUser)*0x58;
        if (*reinterpret_cast<const uint32_t*>(record-4)!=state.unit) return false;
        yaw=*reinterpret_cast<const float*>(record+8);
        pitch=*reinterpret_cast<const float*>(record+0xc);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (!std::isfinite(yaw)||!std::isfinite(pitch)||yaw<-6.2832f||yaw>12.5664f||
        !StateCurrent()||state.generation!=generation.load(std::memory_order_acquire)) return false;
    heading={std::cos(yaw),std::sin(yaw),0};
    return true;
}
bool HaloCEControls_SeatHeading(uint32_t expectedGeneration,halo_ce::Vec3& heading) noexcept
{ Callback callback;return SeatHeadingBody(expectedGeneration,heading); }
// AIM FRAME ZOOM (game.cpp): the local player's zoom level from the same control record, the int16 at
// control + 0x34 + N * 0x58. The control update (+0xA9915C) owns it: -1 on a weapon change and whenever the player
// cannot act (paused, a cinematic, no unit), the weapon's next level when the zoom button asks; the aim-assist query
// (+0xA97CF4) reads the game's zoom from it. ADDR-HYGIENE: guarded as the seat heading is (the record's unit handle
// must be the local player's unit). The frame is drawn on foot only: in a seat this answers -1.
static bool ZoomLevelBody(uint32_t expectedGeneration,int16_t& level) noexcept
{
    HaloCELocalPlayerState state{};
    if (!ReadLocalPlayerState(state)||state.generation!=expectedGeneration||!state.hasControlledUnit||
        state.inputUser<0||state.inputUser>=4)
        return false;
    int16_t value=-1;
    __try
    {
        const uintptr_t control=*reinterpret_cast<const uintptr_t*>(moduleBase+0x2d8fe70);
        if (!control) return false;
        const uintptr_t record=control+size_t(state.inputUser)*0x58;
        if (*reinterpret_cast<const uint32_t*>(record+0x10)!=state.unit) return false;
        value=*reinterpret_cast<const int16_t*>(record+0x34);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (!StateCurrent()||state.generation!=generation.load(std::memory_order_acquire)) return false;
    level=state.onFoot&&value>=0?value:-1;
    return true;
}
bool HaloCEControls_ZoomLevel(uint32_t expectedGeneration,int16_t& level) noexcept
{ Callback callback;return ZoomLevelBody(expectedGeneration,level); }
float HaloCEControls_PhysicalCrouchCorrection(uint32_t expectedGeneration,uint64_t epoch,
    float physicalDown,bool allowed) noexcept
{ Callback callback;return CrouchCameraBody(expectedGeneration,epoch,physicalDown,allowed); }

bool HaloCEControls_GetNativePaused(bool& paused,bool* suppressed,bool* inputKnown) noexcept
{
    Callback callback;
    if (inputKnown) *inputKnown=suppressed&&ReadInputSuppressed(*suppressed);
    return ReadNativePaused(paused);
}
bool HaloCEControls_OwnsLookStick() noexcept
{
    Callback callback;
    const uint64_t stamp=lastOwned.load(std::memory_order_acquire),now=GetTickCount64();
    HaloCELocalPlayerState state{};RenderContext context{};
    bool seated=false;int16_t seat=-1;float hullYaw{};
    return TurnCurrent()&&stamp&&now>=stamp&&now-stamp<150&&AdmittedTurnContext(state,context,seated,seat,hullYaw)&&
        HaloCE_RenderContextCurrent(context);
}
bool HaloCEControls_MapMoveStick(float x,float y,float& outputX,float& outputY) noexcept
{
    Callback callback;HaloCELocalPlayerState state{};RenderContext context{};
    float candidateX{},candidateY{};
    if (!AdmittedContext(state,context)||!HeadRelativeMovement(context,x,y,candidateX,candidateY)||
        !StateCurrent()||!HaloCE_RenderContextCurrent(context)) return false;
    outputX=candidateX;outputY=candidateY;return true;
}
bool HaloCEControls_GetLocomotionFrame(HaloCELocalPlayerState& state,RenderContext& context) noexcept
{
    Callback callback;HaloCELocalPlayerState candidateState{};RenderContext candidateContext{};
    if (!AdmittedContext(candidateState,candidateContext)||!StateCurrent()||
        !HaloCE_RenderContextCurrent(candidateContext)) return false;
    state=candidateState;context=candidateContext;return true;
}
