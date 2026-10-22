#include "haloce_vehicle_cameras.h"
#include "haloce_controls.h"
#include "haloce_native_bindings.h"
#include "hook_quiescence.h"
#include "native_forward.h"
#include "title_adapter.h"
#include "../common/config.h"
#include "../common/haloce_vehicle_camera_contract.h"
#include "../common/haloce_vehicle_camera_logic.h"
#include "../common/log.h"
#include "../common/minhook_lifecycle.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
using namespace halo_ce;
using FollowingFn=void(__fastcall*)(void*,const void*,void*);
using MarkerFn=int16_t(__fastcall*)(uint32_t,const char*,void*,int16_t);
HMODULE retained{};
uintptr_t moduleBase{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<bool> active{},ready{},retiring{};
void* target{};
void* original{};
bool enabled{};
uint32_t rejectedGeneration{};
uintptr_t rejectedBase{};
bool pendingReported{};
std::atomic<uint64_t> placed{},faults{};
uint64_t lastReport{};

bool Current() noexcept
{
    return ready.load(std::memory_order_acquire)&&active.load(std::memory_order_acquire)&&
        !retiring.load(std::memory_order_acquire)&&TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}

// A marker record (object_get_marker_by_name): the marker's world frame at +0x38 -- forward +0x3C, left +0x48,
// up +0x54, position +0x60.
bool MarkerFrame(uint32_t object,const char* name,float position[3],float forward[3],float up[3]) noexcept
{
    alignas(16) uint8_t record[128]{};
    const auto marker=reinterpret_cast<MarkerFn>(moduleBase+vehicle_camera_contract::marker);
    if (marker(object,name,record,1)!=1) return false;
    std::memcpy(position,record+0x60,12);std::memcpy(forward,record+0x3C,12);std::memcpy(up,record+0x54,12);
    float f=0,u=0,d=0;
    for (int axis=0;axis<3;++axis)
    {
        if (!std::isfinite(position[axis])||!std::isfinite(forward[axis])||!std::isfinite(up[axis])) return false;
        f+=forward[axis]*forward[axis];u+=up[axis]*up[axis];d+=forward[axis]*up[axis];
    }
    return std::fabs(f-1)<=0.05f&&std::fabs(u-1)<=0.05f&&std::fabs(d)<=0.05f;
}

// The command the following camera built (an observer command): flags +0 (bit 0 = valid), focus position +4,
// focus offset +0x10 (along the look's heading: forward, right, up), focus distance +0x1C, forward +0x24.
void Place(uint8_t* command) noexcept
{
    if (!Game_HaloCEChaseAny()||!Current()) return;
    if (!(*reinterpret_cast<const uint32_t*>(command)&1u)) return;
    HaloCEVehicleSeatNow seat{};
    if (!HaloCEControls_ReadVehicleSeatNow(seat)||seat.generation!=generation.load(std::memory_order_acquire)) return;
    HaloCEChaseFrame frame{};
    frame.generation=seat.generation;frame.unit=seat.unit;frame.parent=seat.parent;frame.root=seat.root;
    frame.definition=seat.definition;frame.seat=seat.seat;frame.boundsRadius=seat.boundsRadius;
    __try
    {
        if (!MarkerFrame(seat.root,"",frame.vehiclePos,frame.vehicleForward,frame.vehicleUp)) return;
        frame.seatValid=MarkerFrame(seat.unit,"",frame.seatPos,frame.seatForward,frame.seatUp);
        frame.headValid=Game_HaloCEChaseHead()&&MarkerFrame(seat.unit,"head",frame.headPos,frame.headForward,frame.headUp);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { faults.fetch_add(1,std::memory_order_relaxed);return; }
    std::memcpy(frame.pivot,command+4,12);
    float offset[3]{},distance{};
    std::memcpy(offset,command+0x10,12);std::memcpy(&distance,command+0x1C,4);
    std::memcpy(frame.looks,command+0x24,12);
    frame.gameValid=std::isfinite(frame.pivot[0])&&std::isfinite(frame.pivot[1])&&std::isfinite(frame.pivot[2])&&
        haloce_vehicle_camera::GameCameraFromPivot(frame.looks,offset,distance,frame.gameFromPivot);
    float point[3]{};
    if (!Game_HaloCEChasePlace(frame,point)) return;
    // The observer stands its camera at focus + offset - forward * distance: the place, with nothing added.
    std::memcpy(command+4,point,12);
    std::memset(command+0x10,0,12);
    std::memset(command+0x1C,0,4);
    placed.fetch_add(1,std::memory_order_relaxed);
}

__declspec(noinline) void __fastcall FollowingHook(void* camera,const void* controls,void* result)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        CallNativeWithSpareSlots(reinterpret_cast<FollowingFn>(original),camera,controls,result);
        if (result) Place(static_cast<uint8_t*>(result));
    }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}

bool Remove() noexcept
{
    active=false;retiring=true;ready=false;
    if (enabled)
    {
        const auto status=MCCVR_DisableHookForRetirement(target);
        if (status!=MH_OK&&status!=MH_ERROR_DISABLED)
        {
            if (!pendingReported) LOG("CE vehicle cameras: cleanup pending (hook disable %d); the game's camera is stock",status);
            pendingReported=true;return false;
        }
        enabled=false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&FollowingHook),
        reinterpret_cast<const void*>(&HaloCEVehicleCameras_RootFrame)};
    const void* originals[]{original,nullptr};
    if (target&&!WaitForNativeDetourQuiescence(functions,originals,2,callbacks))
    {
        if (!pendingReported) LOG("CE vehicle cameras: cleanup pending (callbacks still running); the game's camera is stock");
        pendingReported=true;return false;
    }
    if (target)
    {
        const auto status=MH_RemoveHook(target);
        if (status!=MH_OK&&status!=MH_ERROR_NOT_CREATED)
        {
            if (!pendingReported) LOG("CE vehicle cameras: cleanup pending (hook removal %d)",status);
            pendingReported=true;return false;
        }
    }
    target=original=nullptr;
    if (retained) { FreeLibrary(retained);retained=nullptr; }
    moduleBase=0;generation=0;retiring=false;pendingReported=false;
    return true;
}

bool Install(size_t size) noexcept
{
    const char* failure{};
    const NativeContractSet proof{vehicle_camera_contract::entries,vehicle_camera_contract::witnesses,
        vehicle_camera_contract::relatives,vehicle_camera_contract::pointers};
    if (!VerifyNativeFeatureBindings(moduleBase,size,generation.load(),proof,failure))
    {
        LOG("CE vehicle cameras: chase cameras stock (%s); the seat view is unchanged",failure?failure:"binding failure");
        return false;
    }
    void* location=reinterpret_cast<void*>(moduleBase+vehicle_camera_contract::following);
    auto status=MH_CreateHook(location,reinterpret_cast<void*>(&FollowingHook),&original);
    if (status!=MH_OK)
    { LOG("CE vehicle cameras: chase cameras stock (create status %d)",status);return false; }
    target=location;
    status=MH_EnableHook(target);
    if (status!=MH_OK)
    { LOG("CE vehicle cameras: chase cameras stock (enable status %d)",status);return false; }
    enabled=true;ready=true;
    LOG("CE vehicle cameras: chase cameras installed in the game's following camera (halo1+0x%X); the simulation's "
        "own camera is never placed",vehicle_camera_contract::following);
    return true;
}
}

bool HaloCEVehicleCameras_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    // Installed only while the cameras are wanted in CE: the vehicle cameras and the seat view they run in.
    const bool wanted=isActive&&g_config.vehicle_cameras&&g_config.vehicle_first_person;
    if (retained&&(!wanted||base!=moduleBase||gen!=generation.load()||retiring.load()))
        if (!Remove()) return false;
    active.store(wanted,std::memory_order_release);
    if (!wanted||!base||!gen) return false;
    if (rejectedBase==base&&rejectedGeneration==gen) return false;
    if (!retained)
    {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(base),&retained))
            return false;
        moduleBase=base;generation=gen;retiring=false;
    }
    if (!ready.load()&&!Install(size))
    {
        rejectedBase=base;rejectedGeneration=gen;
        (void)Remove();
        return false;
    }
    const uint64_t now=GetTickCount64();
    if (now-lastReport>=5000)
    {
        lastReport=now;
        const uint64_t count=placed.exchange(0,std::memory_order_relaxed);
        const uint64_t faulted=faults.exchange(0,std::memory_order_relaxed);
        if (count||faulted)
            LOG("CE vehicle cameras: chase camera placed on %llu frames in the last 5 s (%llu guarded faults)",
                static_cast<unsigned long long>(count),static_cast<unsigned long long>(faulted));
    }
    return Current();
}

bool HaloCEVehicleCameras_FollowingLive() noexcept
{
    return Current();
}

// Counted before anything is checked, as the hook is: retirement waits for the count, so the module cannot go between
// the check and the query.
bool HaloCEVehicleCameras_RootFrame(uint32_t object,float position[3],float forward[3],float up[3]) noexcept
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool ok=false;
    if (object!=UINT32_MAX&&Current())
    {
        __try { ok=MarkerFrame(object,"",position,forward,up); }
        __except(EXCEPTION_EXECUTE_HANDLER) { faults.fetch_add(1,std::memory_order_relaxed);ok=false; }
    }
    callbacks.fetch_sub(1,std::memory_order_release);
    return ok;
}
