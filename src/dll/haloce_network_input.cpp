#include "haloce_network_input.h"
#include "haloce_controls.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "title_adapter.h"
#include "hook_quiescence.h"
#include "../common/haloce_network_input_logic.h"
#include "../common/minhook_lifecycle.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>

namespace
{
using namespace halo_ce;
using ActionFn=void(__fastcall*)(const void*,PlayerAction*);
using SeatFn=uint8_t(__fastcall*)(uint32_t,Vec3*);
using ObjectFn=uintptr_t(__fastcall*)(uint32_t,uint32_t);
uintptr_t moduleBase{},rejectedBase{};
uint32_t rejectedGeneration{};
HMODULE retained{};
void* target{};void* original{};
bool enabled{},retiring{},cleanupReported{};
std::atomic<bool> active{},ready{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<uint64_t> applied{},stock{},faults{},seated{};
uint64_t lastReport{};
bool Current() noexcept
{
    return ready.load(std::memory_order_acquire)&&active.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
bool Network() noexcept
{
    if (!Current()) return false;
    __try
    {
        const int16_t connection=*reinterpret_cast<const int16_t*>(moduleBase+contract::network_input::connection_type);
        return (connection==1||connection==2)&&Current();
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Owner(const void* source,HaloCELocalPlayerState& player,RenderContext& frame,int16_t& seat) noexcept
{
    seat=-1;
    if (!Network()||!HaloCEControls_GetLocalPlayerState(player)||!HaloCE_GetGameplayContext(frame)||
        !player.hasControlledUnit||player.unit==UINT32_MAX||player.player==UINT32_MAX||
        player.inputUser<0||player.inputUser>=4||player.nativeInputBlocked||player.nativeLookBlocked||
        player.nativePaused||player.nativeCinematicFlag||
        (player.nativePerspective!=0&&(player.onFoot||player.nativePerspective!=1))||
        !frame.tracking.controllers.padValid||frame.tracking.controllers.controlsPresentationBlocked||
        player.generation!=generation.load()||player.generation!=frame.tracking.generation) return false;
    __try
    {
        const auto control=*reinterpret_cast<const uintptr_t*>(moduleBase+contract::network_input::control_pointer);
        if (!control||reinterpret_cast<uintptr_t>(source)!=control+0x14+size_t(player.inputUser)*0x58) return false;
        if (!player.onFoot)
        {
            const auto get=reinterpret_cast<ObjectFn>(moduleBase+contract::player_state::state_object_try_get);
            const uintptr_t unit=get(player.unit,1);
            if (!unit||player.parent==UINT32_MAX||!(player.parent>>16)||
                *reinterpret_cast<const uint32_t*>(unit+0xd8)!=player.parent||!get(player.parent,2)) return false;
            seat=*reinterpret_cast<const int16_t*>(unit+0x2d0);
            if (seat<0) return false;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return Current()&&HaloCE_RenderContextCurrent(frame);
}
bool Adapt(const void* source,PlayerAction* output) noexcept
{
    HaloCELocalPlayerState player{},latest{};RenderContext frame{},latestFrame{};
    int16_t seat=-1,latestSeat=-1;
    if (!output||!Owner(source,player,frame,seat)) return false;
    NodeMatrix aim{};
    if (!BuildControllerMatrix(frame.camera,frame.tracking,frame.reference,
        frame.tracking.controllers.primaryAim,frame.unitsPerMeter,frame.positional,aim)) return false;
    PlayerAction before{},candidate{};Vec3 direction=aim.forward;
    __try
    {
        before=*output;
        if (!player.onFoot)
        {
            // AD0304 applies this native seat transform AFTER decoding yaw/pitch.
            // Evaluate its pure direction operation on a private orthogonal basis
            // and invert it, so native prediction/host do not rotate our aim twice.
            Vec3 basis[3]{{1,0,0},{0,1,0},{0,0,1}};
            const auto transform=reinterpret_cast<SeatFn>(moduleBase+contract::network_input::seat_direction_transform);
            for (auto& axis:basis) (void)transform(player.unit,&axis);
            if (!InverseSeatDirection(direction,basis,direction)) return false;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { faults.fetch_add(1);return false; }
    if (!BuildNetworkAction(before,direction,player.onFoot,candidate)||!Owner(source,latest,latestFrame,latestSeat)||
        latest.player!=player.player||latest.unit!=player.unit||latest.inputUser!=player.inputUser||
        latest.parent!=player.parent||latest.onFoot!=player.onFoot||latestSeat!=seat||
        latest.nativePerspective!=player.nativePerspective||frame.referenceRevision!=latestFrame.referenceRevision||
        frame.rendererEpoch!=latestFrame.rendererEpoch||frame.tracking.spaceEpoch!=latestFrame.tracking.spaceEpoch||
        !Network()||!HaloCE_RenderContextCurrent(frame)) return false;
    __try { *output=candidate; }
    __except(EXCEPTION_EXECUTE_HANDLER) { faults.fetch_add(1);return false; }
    if (!player.onFoot) seated.fetch_add(1,std::memory_order_relaxed);
    return true;
}
void ActionBody(const void* source,PlayerAction* output,uintptr_t caller)
{
    const auto native=reinterpret_cast<ActionFn>(original);
    if (!native) return;
    native(source,output); // Exactly once; original exceptions always propagate.
    if (caller==moduleBase+0xa9988d&&Adapt(source,output)) applied.fetch_add(1,std::memory_order_relaxed);
    else stock.fetch_add(1,std::memory_order_relaxed);
}
__declspec(noinline) void __fastcall ActionHook(const void* source,PlayerAction* output)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { ActionBody(source,output,caller); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
bool Remove() noexcept
{
    active=false;ready=false;retiring=true;
    if (enabled)
    {
        const auto status=MCCVR_DisableHookForRetirement(target);
        if (status!=MH_OK&&status!=MH_ERROR_DISABLED) return false;
        enabled=false;
    }
    const void* functions[]{reinterpret_cast<void*>(&ActionHook),
        reinterpret_cast<void*>(&HaloCENetworkInput_UsesNativeSimulation)};
    const void* trampolines[]{original,nullptr};
    if (target&&!WaitForNativeDetourQuiescence(functions,trampolines,2,callbacks)) return false;
    if (target)
    {
        const auto status=MH_RemoveHook(target);
        if (status!=MH_OK&&status!=MH_ERROR_NOT_CREATED) return false;
    }
    if (callbacks.load()) return false;
    target=original=nullptr;
    if (retained) FreeLibrary(retained);
    retained=nullptr;moduleBase=0;generation=0;retiring=false;cleanupReported=false;return true;
}
}

bool HaloCENetworkInput_UsesNativeSimulation() noexcept
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool result{};
    __try { result=Network(); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
    return result;
}
bool HaloCENetworkInput_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    if (retained&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring))
        if (!Remove())
        {
            if (!cleanupReported) LOG("CE network input cleanup pending: callback/hook retirement not proved; module and trampoline retained, optional input adapter stopped");
            cleanupReported=true;return false;
        }
    if (!isActive||!base||!gen||(rejectedBase==base&&rejectedGeneration==gen)) return false;
    if (!retained)
    {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(base),&retained)) return false;
        moduleBase=base;generation=gen;
        const NativeContractSet proof{contract::network_input::entries,contract::network_input::witnesses,
            contract::network_input::relatives,contract::network_input::pointers};
        const char* failure{};
        bool okay=VerifyNativeFeatureBindings(base,size,gen,proof,failure);
        if (okay)
        {
            void* location=reinterpret_cast<void*>(base+contract::network_input::action_build);
            okay=MH_CreateHook(location,reinterpret_cast<void*>(&ActionHook),&original)==MH_OK;
            if (okay) { target=location;okay=MH_EnableHook(target)==MH_OK;enabled=okay; }
        }
        if (!okay)
        {
            LOG("CE network input stock fallback: %s; camera and campaign adapters retained",failure?failure:"hook installation failed");
            rejectedBase=base;rejectedGeneration=gen;(void)Remove();return false;
        }
        active=true;ready=true;
        LOG("CE network input installed: local outgoing controller direction before client prediction/host submission; native actions and seat transform retained; network simulation overrides disabled");
    }
    const auto now=GetTickCount64();
    if (now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE network input gen=%u network=%d applied=%llu seated=%llu stock=%llu faults=%llu",
            gen,HaloCENetworkInput_UsesNativeSimulation(),applied.load(),seated.load(),stock.load(),faults.load());
    }
    return Current();
}
