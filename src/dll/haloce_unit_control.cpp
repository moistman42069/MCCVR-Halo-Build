#include "haloce_unit_control.h"
#include "haloce_controls.h"
#include "haloce_network_input.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "hook_quiescence.h"
#include "title_adapter.h"
#include "../common/haloce_controls_logic.h"
#include "../common/haloce_unit_control_logic.h"
#include "../common/minhook_lifecycle.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>

namespace
{
using namespace halo_ce;
using UnitControlFn=void(__fastcall*)(uint32_t,const UnitControlPacket*,int32_t);
using MovementFn=void(__fastcall*)(void*);
using ObjectGetFn=uintptr_t(__fastcall*)(uint32_t,uint32_t);
void* target{};
void* original{};
void* movementTarget{};
void* movementOriginal{};
HMODULE retained{};
uintptr_t moduleBase{};
bool enabled{};
bool movementEnabled{};
std::atomic<bool> active{},retiring{},ready{},vehicleReady{};
std::atomic<uint32_t> generation{},callbacks{};
uint32_t rejectedGeneration{};
uintptr_t rejectedBase{};
enum class PendingCleanup { None,Disable,Quiescence,Remove,Callbacks };
PendingCleanup pendingCleanup{};
bool pinFailureReported{};
std::atomic<uint64_t> bodies{},aims{},stock{},declined{},exceptions{};
std::atomic<uint64_t> movements{},movementStock{},movementDeclined{};
std::atomic<uint64_t> vehicles{},vehicleDeclined{};
uint64_t lastReport{};

bool Current() noexcept
{
    return active.load(std::memory_order_acquire)&&!retiring.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
bool Owner(uint32_t unit,HaloCELocalPlayerState& state,RenderContext& context) noexcept
{
    return Current()&&HaloCEControls_GetLocomotionFrame(state,context)&&
        unit!=0xffffffffu&&unit==state.unit&&state.player!=0xffffffffu&&
        state.generation==generation.load(std::memory_order_acquire)&&
        state.generation==context.tracking.generation&&
        OnFootControls({state.hasControlledUnit,state.onFoot,state.nativePerspective==0,
            state.nativeInputBlocked,state.nativeLookBlocked,state.nativePaused,
            state.nativeCinematicFlag,context.tracking.controllers.controlsPresentationBlocked})&&
        Current()&&HaloCE_RenderContextCurrent(context);
}
bool CopyPacket(const UnitControlPacket* source,UnitControlPacket& output) noexcept
{
    if (!source) return false;
    __try { output=*source;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VehicleOwner(uint32_t unit,HaloCELocalPlayerState& state,RenderContext& context,
    int16_t& seat) noexcept
{
    if (!Current()||!vehicleReady.load(std::memory_order_acquire)||
        !HaloCEControls_GetLocalPlayerState(state)||!HaloCE_GetGameplayContext(context)||
        unit==0xffffffffu||unit!=state.unit||state.player==0xffffffffu||
        !state.hasControlledUnit||state.onFoot||state.parent==0xffffffffu||!(state.parent>>16)||
        (state.nativePerspective!=0&&state.nativePerspective!=1)||state.inputUser<0||state.inputUser>=4||
        state.nativeInputBlocked||state.nativeLookBlocked||state.nativePaused||state.nativeCinematicFlag||
        !context.tracking.controllers.padValid||context.tracking.controllers.controlsPresentationBlocked||
        state.generation!=generation.load(std::memory_order_acquire)||
        state.generation!=context.tracking.generation) return false;
    // E-CE-VEHICLE-CONTROL-1: a following camera alone does not prove a vehicle.
    // Verify the seated local biped's direct parent with CE's native vehicle
    // object lookup. The seat is an identity only; no tag array is indexed.
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t occupant=get(unit,1);
        if (!occupant||*reinterpret_cast<const uint32_t*>(occupant+0xd8)!=state.parent||
            !get(state.parent,2)) return false;
        seat=*reinterpret_cast<const int16_t*>(occupant+0x2d0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    return seat>=0&&Current()&&vehicleReady.load(std::memory_order_acquire)&&
        HaloCE_RenderContextCurrent(context);
}
void UnitControlBody(uint32_t unit,const UnitControlPacket* source,int32_t clientUpdate,uintptr_t caller)
{
    const auto native=reinterpret_cast<UnitControlFn>(original);
    if (!native) return;
    // Network prediction and authority must consume the same outgoing action.
    // Never replace a consumed network packet with a fresh local XR sample.
    if (HaloCENetworkInput_UsesNativeSimulation())
    { stock.fetch_add(1,std::memory_order_relaxed);native(unit,source,clientUpdate);return; }
    UnitControlPacket packet{},candidate{};
    HaloCELocalPlayerState state{},latest{};RenderContext context{},latestContext{};
    bool aim{};
    if (caller==moduleBase+0xad0d5b&&ready.load(std::memory_order_acquire)&&
        Owner(unit,state,context)&&CopyPacket(source,packet)&&
        BuildTrackedUnitControl(context,packet,candidate,aim)&&Owner(unit,latest,latestContext)&&
        state.player==latest.player&&state.inputUser==latest.inputUser&&
        state.unit==latest.unit&&
        context.referenceRevision==latestContext.referenceRevision&&
        context.rendererEpoch==latestContext.rendererEpoch&&
        context.tracking.spaceEpoch==latestContext.tracking.spaceEpoch&&
        ready.load(std::memory_order_acquire)&&HaloCE_RenderContextCurrent(context))
    {
        // The original packet and native camera/input-angle state remain intact.
        // The engine owns validation, native interpolation and grenade release.
        native(unit,&candidate,clientUpdate);
        bodies.fetch_add(1,std::memory_order_relaxed);
        if (aim) aims.fetch_add(1,std::memory_order_relaxed);
        else declined.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    int16_t seat=-1,latestSeat=-1;
    if (caller==moduleBase+0xad0d5b&&ready.load(std::memory_order_acquire)&&
        VehicleOwner(unit,state,context,seat))
    {
        if (CopyPacket(source,packet)&&BuildTrackedVehicleControl(context,packet,candidate)&&
            VehicleOwner(unit,latest,latestContext,latestSeat)&&
            state.player==latest.player&&state.inputUser==latest.inputUser&&state.unit==latest.unit&&
            state.parent==latest.parent&&seat==latestSeat&&
            state.nativePerspective==latest.nativePerspective&&
            context.referenceRevision==latestContext.referenceRevision&&
            context.rendererEpoch==latestContext.rendererEpoch&&
            context.tracking.spaceEpoch==latestContext.tracking.spaceEpoch&&
            ready.load(std::memory_order_acquire)&&vehicleReady.load(std::memory_order_acquire)&&
            HaloCE_RenderContextCurrent(context))
        {
            // Native driver/gunner forwarding selects which occupant controls
            // the parent. Physics, seat limits, throttle and actions stay native.
            native(unit,&candidate,clientUpdate);
            vehicles.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        vehicleDeclined.fetch_add(1,std::memory_order_relaxed);
    }
    stock.fetch_add(1,std::memory_order_relaxed);
    native(unit,source,clientUpdate);
}
__declspec(noinline) void __fastcall UnitControlHook(uint32_t unit,const UnitControlPacket* packet,int32_t clientUpdate)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { UnitControlBody(unit,packet,clientUpdate,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
bool ReadMovement(void* data,uint32_t& unit,UnitMovementBasis& basis) noexcept
{
    if (!data) return false;
    __try
    {
        std::memcpy(&unit,data,sizeof(unit));
        std::memcpy(&basis,static_cast<uint8_t*>(data)+0x14,sizeof(basis));
        return Finite(basis.forward)&&Finite(basis.aim);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteMovement(void* data,const UnitMovementBasis& basis) noexcept
{
    if (!data) return false;
    __try { std::memcpy(static_cast<uint8_t*>(data)+0x14,&basis,sizeof(basis));return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void MovementBody(void* data,uintptr_t caller)
{
    const auto native=reinterpret_cast<MovementFn>(movementOriginal);
    if (!native) return;
    if (HaloCENetworkInput_UsesNativeSimulation())
    { movementStock.fetch_add(1,std::memory_order_relaxed);native(data);return; }
    uint32_t unit{};UnitMovementBasis before{},candidate{};
    HaloCELocalPlayerState state{},latest{};RenderContext context{},latestContext{};
    if ((caller!=moduleBase+contract::unit_control::movement_consumer_return&&
         caller!=moduleBase+contract::unit_control::movement_consumer_return_secondary)||
        !ready.load(std::memory_order_acquire)||!ReadMovement(data,unit,before)||
        !Owner(unit,state,context)||!BuildUnitMovementBasis(context,candidate)||
        !Owner(unit,latest,latestContext)||state.player!=latest.player||
        state.inputUser!=latest.inputUser||state.unit!=latest.unit||
        context.referenceRevision!=latestContext.referenceRevision||
        context.rendererEpoch!=latestContext.rendererEpoch||
        context.tracking.spaceEpoch!=latestContext.tracking.spaceEpoch||
        !ready.load(std::memory_order_acquire)||!HaloCE_RenderContextCurrent(context))
    { movementStock.fetch_add(1,std::memory_order_relaxed);native(data);return; }
    if (!WriteMovement(data,candidate))
    {
        (void)WriteMovement(data,before);
        movementDeclined.fetch_add(1,std::memory_order_relaxed);native(data);return;
    }
    // Only private movement-input vectors change. Native collision, slope,
    // acceleration, output velocity and the live unit's aiming remain native.
    __try { native(data);movements.fetch_add(1,std::memory_order_relaxed); }
    __finally
    {
        if (!WriteMovement(data,before)) movementDeclined.fetch_add(1,std::memory_order_relaxed);
    }
}
__declspec(noinline) void __fastcall MovementHook(void* data)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { MovementBody(data,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
bool Remove() noexcept
{
    active=false;retiring=true;ready=false;vehicleReady=false;
    const auto pending=[](PendingCleanup stage,const char* reason)
    {
        if (pendingCleanup!=stage)
            LOG("CE body/controller grenade aim cleanup pending: %s; optional feature stock, module/trampoline retained",reason);
        pendingCleanup=stage;
        return false;
    };
    if (enabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return pending(PendingCleanup::Disable,"hook disable failed");
        enabled=false;
    }
    if (movementEnabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(movementTarget);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return pending(PendingCleanup::Disable,"movement hook disable failed");
        movementEnabled=false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&UnitControlHook),reinterpret_cast<const void*>(&MovementHook)};
    const void* originals[]{original,movementOriginal};
    if ((target||movementTarget)&&!WaitForNativeDetourQuiescence(functions,originals,2,callbacks))
        return pending(PendingCleanup::Quiescence,"callback quiescence not proved");
    if (target)
    {
        const auto result=MH_RemoveHook(target);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return pending(PendingCleanup::Remove,"hook removal failed");
        target=original=nullptr;
    }
    if (movementTarget)
    {
        const auto result=MH_RemoveHook(movementTarget);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return pending(PendingCleanup::Remove,"movement hook removal failed");
        movementTarget=movementOriginal=nullptr;
    }
    if (callbacks.load(std::memory_order_acquire)) return pending(PendingCleanup::Callbacks,"callback still active");
    target=original=movementTarget=movementOriginal=nullptr;
    if (retained) { FreeLibrary(retained);retained=nullptr; }
    moduleBase=0;generation=0;retiring=false;pendingCleanup=PendingCleanup::None;return true;
}
bool Install(size_t size) noexcept
{
    const char* failure{};
    const NativeContractSet contracts{contract::unit_control::entries,contract::unit_control::witnesses,
        contract::unit_control::relatives,contract::unit_control::pointers};
    if (!VerifyNativeFeatureBindings(moduleBase,size,generation.load(),contracts,failure))
    { LOG("CE body/controller grenade aim stock fallback: %s",failure?failure:"binding failure");return false; }
    // Check the independent vehicle evidence before any detour can alter a
    // native entry signature. Failure affects only this new optional path.
    const NativeContractSet vehicleContracts{contract::vehicle::entries,contract::vehicle::witnesses,
        contract::vehicle::relatives,contract::vehicle::pointers};
    const bool vehicleVerified=VerifyNativeFeatureBindings(moduleBase,size,generation.load(),vehicleContracts,failure);
    if (!vehicleVerified)
        LOG("CE vehicle controls stock fallback: %s; on-foot controls and stereo remain active",failure?failure:"binding failure");
    void* location=reinterpret_cast<void*>(moduleBase+contract::unit_control::unit_control_set);
    auto result=MH_CreateHook(location,reinterpret_cast<void*>(&UnitControlHook),&original);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: create status %d",result);return false; }
    target=location;
    location=reinterpret_cast<void*>(moduleBase+contract::unit_control::movement_consumer);
    result=MH_CreateHook(location,reinterpret_cast<void*>(&MovementHook),&movementOriginal);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: movement create status %d",result);return false; }
    movementTarget=location;
    result=MH_EnableHook(target);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: enable status %d",result);return false; }
    enabled=true;
    result=MH_EnableHook(movementTarget);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: movement enable status %d",result);return false; }
    movementEnabled=true;ready=true;
    vehicleReady=vehicleVerified;
    if (vehicleReady.load())
        LOG("CE vehicle controls installed: tracked aiming-hand facing/aim/looking in local seated native packets; native following camera, driver/gunner forwarding and throttle retained; Original and Anniversary");
    LOG("CE unit control installed: local on-foot head facing/looking, controller aiming; private native-camera movement basis, native grenade release retained");
    return true;
}
}

bool HaloCEUnitControl_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    if (retained&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring.load()))
        if (!Remove()) return false;
    active.store(isActive,std::memory_order_release);
    if (!isActive||!base||!gen) return false;
    if (rejectedBase==base&&rejectedGeneration==gen) return false;
    if (!retained)
    {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(base),&retained))
        {
            if (!pinFailureReported)
                LOG("CE body/controller grenade aim stock fallback: module retention failed; retry pending");
            pinFailureReported=true;
            return false;
        }
        pinFailureReported=false;
        moduleBase=base;generation=gen;retiring=false;
    }
    if (!ready.load()&&!Install(size))
    {
        rejectedBase=base;rejectedGeneration=gen;
        // A partial optional hook owns its trampoline and module until removal
        // is proved. Retry cleanup on later polls without retrying this binding.
        (void)Remove();
        return false;
    }
    const uint64_t now=GetTickCount64();
    if (now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE unit control gen=%u ready=%d bodies=%llu aims=%llu stock=%llu aimDeclined=%llu movement=%llu movementStock=%llu movementDeclined=%llu exceptions=%llu vehicleReady=%d vehicles=%llu vehicleDeclined=%llu",
            gen,ready.load(),bodies.load(),aims.load(),stock.load(),declined.load(),movements.load(),movementStock.load(),movementDeclined.load(),exceptions.load(),vehicleReady.load(),vehicles.load(),vehicleDeclined.load());
    }
    return Current()&&ready.load();
}

bool HaloCEUnitControl_VehicleAimCurrent(const HaloCELocalPlayerState& player,
    const halo_ce::RenderContext& context) noexcept
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool admitted=false;
    __try
    {
        HaloCELocalPlayerState current{},latest{};RenderContext frame{},latestFrame{};
        int16_t seat=-1,latestSeat=-1;
        admitted=ready.load(std::memory_order_acquire)&&
            VehicleOwner(player.unit,current,frame,seat)&&
            VehicleOwner(player.unit,latest,latestFrame,latestSeat)&&
            player.generation==current.generation&&player.player==current.player&&
            player.inputUser==current.inputUser&&player.parent==current.parent&&
            !player.onFoot&&(player.nativePerspective==0||player.nativePerspective==1)&&
            current.player==latest.player&&current.inputUser==latest.inputUser&&
            current.parent==latest.parent&&seat==latestSeat&&
            player.nativePerspective==current.nativePerspective&&
            current.nativePerspective==latest.nativePerspective&&
            context.tracking.generation==frame.tracking.generation&&
            context.referenceRevision==frame.referenceRevision&&
            context.rendererEpoch==frame.rendererEpoch&&
            context.tracking.spaceEpoch==frame.tracking.spaceEpoch&&
            frame.referenceRevision==latestFrame.referenceRevision&&
            frame.rendererEpoch==latestFrame.rendererEpoch&&
            frame.tracking.spaceEpoch==latestFrame.tracking.spaceEpoch&&
            frame.tracking.controllers.primaryAim.valid&&
            latestFrame.tracking.controllers.primaryAim.valid&&
            ready.load(std::memory_order_acquire)&&Current()&&
            HaloCE_RenderContextCurrent(context);
    }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
    return admitted;
}
