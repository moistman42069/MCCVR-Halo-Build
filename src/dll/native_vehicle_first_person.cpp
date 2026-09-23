#include "native_vehicle_first_person.h"
#include "title_adapter.h"
#include "game.h"
#include "sigscan.h"
#include "hook_quiescence.h"
#include "../common/config.h"
#include "../common/log.h"
#include "../common/minhook_lifecycle.h"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
enum Role : unsigned { Selector,Camera,Marker,MarkerProof,RoleCount };
struct Binding { uint32_t rva;const char* pattern; };
struct BindingSet { GameTitle title;uint32_t markerRva;std::array<Binding,RoleCount> entries; };
#include "native_vehicle_first_person_bindings.generated.h"
struct Runtime
{
    HMODULE module{};uintptr_t base{};uint32_t generation{};
    bool attempted{},ready{},enabled[2]{};
    void* target[2]{};void* original[2]{};void* marker{};void* markerInterpolated{};
    std::atomic<uint64_t> trimIdentity{};
    std::atomic_flag trimWriting=ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> trimVersion{},trimGeneration{};
    std::atomic<int> trimSeat{-1};
    std::atomic<bool> requested{},faulted{};
    std::atomic<uint32_t> callbacks{};
    std::atomic<uint64_t> selected{},positioned{},missingMarker{},faults{};
    uint64_t reportAt{};
};
Runtime runtime[3];
using CameraFn=void(__fastcall*)(uint32_t,const void*,void*);
bool Current(unsigned i) noexcept
{
    const auto& r=runtime[i];
    return r.requested.load(std::memory_order_acquire)&&!r.faulted.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==kBindings[i].title&&
        TitleAdapter_GetGeneration(kBindings[i].title)==r.generation;
}
bool Owner(unsigned i,uint32_t unit,NativeVehicleCameraOwner& owner) noexcept
{
    return Current(i)&&unit!=UINT32_MAX&&(unit>>16)&&
        Game_ReadVehicleCameraOwner(kBindings[i].title,owner)&&owner.unit==unit&&
        owner.generation==runtime[i].generation&&owner.parent!=UINT32_MAX&&owner.seat>=0;
}
// All three native marker records have their world transform at +56 and
// translation at +96, independently witnessed by their own camera consumers.
// Positive count is essential: the native missing-marker fallback also fills
// a transform, but returns zero. It must not put the camera at the body origin.
__declspec(noinline) bool Head(unsigned i,const NativeVehicleCameraOwner& owner,float position[3])
{
    const uint32_t unit=owner.unit;
    alignas(16) uint8_t record[128]{};
    int16_t count=0;
    if (i==0) count=reinterpret_cast<int16_t(__fastcall*)(uint32_t,const char*,void*,int16_t)>(
        runtime[i].marker)(unit,"head",record,1);
    else if (i==1) {
        if(g_config.vehicle_cam_smoothing&&runtime[i].markerInterpolated)
            count=reinterpret_cast<int16_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t,uint8_t,uint8_t)>(
                runtime[i].markerInterpolated)(unit,0x4000095,record,1,0,1);
        else count=reinterpret_cast<int16_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t)>(
            runtime[i].marker)(unit,0x4000095,record,1);
    }
    else count=reinterpret_cast<int16_t(__fastcall*)(uint32_t,uint32_t,void*,int16_t,uint8_t,uint8_t,uint8_t)>(
        runtime[i].marker)(unit,0x122,record,1,0,0,1);
    if (count!=1) return false;
    std::memcpy(position,record+96,12);
    float forward[3]{},up[3]{};
    std::memcpy(forward,record+60,12);std::memcpy(up,record+84,12);
    float forwardLength=0,upLength=0,dot=0;
    for (unsigned axis=0;axis<3;++axis) {
        if (!std::isfinite(forward[axis])||!std::isfinite(up[axis])) return false;
        forwardLength+=forward[axis]*forward[axis];upLength+=up[axis]*up[axis];dot+=forward[axis]*up[axis];
    }
    if (std::fabs(forwardLength-1)>0.05f||std::fabs(upLength-1)>0.05f||std::fabs(dot)>0.05f) return false;
    const float right[3]{forward[1]*up[2]-forward[2]*up[1],
        forward[2]*up[0]-forward[0]*up[2],forward[0]*up[1]-forward[1]*up[0]};
    const float scale=Game_GetWorldScale();
    const auto title=kBindings[i].title;
    const float f=ConfigVehicleModelCam(g_config,title,owner.vehicleIdentity,owner.seat,0),
        u=ConfigVehicleModelCam(g_config,title,owner.vehicleIdentity,owner.seat,1),
        h=ConfigVehicleModelCam(g_config,title,owner.vehicleIdentity,owner.seat,2);
    if (!std::isfinite(scale)||scale<=0||scale>100||!std::isfinite(f)||!std::isfinite(u)||
        !std::isfinite(h)||std::fabs(f)>5||std::fabs(u)>5||std::fabs(h)>5) return false;
    for (unsigned axis=0;axis<3;++axis)
        position[axis]+=scale*(forward[axis]*f+up[axis]*u+right[axis]*h);
    for (unsigned axis=0;axis<3;++axis)
        if (!std::isfinite(position[axis])||std::fabs(position[axis])>100000.0f) return false;
    return true;
}
__declspec(noinline) void Fault(unsigned i) noexcept
{
    // Keep a real handler call under MSVC /O2. Production-detour SEH fixtures
    // verify that a native marker exception persists this feature-only latch.
    runtime[i].faulted.store(true,std::memory_order_release);
    runtime[i].faults.fetch_add(1,std::memory_order_relaxed);
}
template<unsigned I> using Mode=std::conditional_t<I==2,int32_t,int16_t>;
template<unsigned I> using SelectorFn=uint32_t(__fastcall*)(uint32_t,Mode<I>*);
// Keep the guarded feature body separate from native forwarding/finally. A
// fault in the optional marker query must be handled here, never swallowed by
// or attributed to the native camera call.
template<unsigned I> bool SelectOwned(uint32_t unit,Mode<I>* mode) noexcept
{
    __try {
        NativeVehicleCameraOwner before{},after{};float point[3]{};
        if (!mode||*mode!=2||!Owner(I,unit,before)) return false;
        if (!Head(I,before,point)) {runtime[I].missingMarker.fetch_add(1,std::memory_order_relaxed);return false;}
        return Owner(I,unit,after)&&before==after;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Fault(I);return false;
    }
}
template<unsigned I> void PositionOwned(uint32_t unit,void* result) noexcept
{
    __try {
        NativeVehicleCameraOwner before{},after{};float point[3]{};Mode<I> mode{};
        if (!result||!Owner(I,unit,before)) return;
        reinterpret_cast<SelectorFn<I>>(runtime[I].original[Selector])(unit,&mode);
        if (mode!=2) return;
        if (!Head(I,before,point)) {runtime[I].missingMarker.fetch_add(1,std::memory_order_relaxed);return;}
        if (!Owner(I,unit,after)||before!=after) return;
        // After the full native evaluation, including any slave-turret marker.
        std::memcpy(static_cast<uint8_t*>(result)+4,point,12);
        auto& r=runtime[I];
        // Menu edits the last positively identified seat in this generation.
        // Opening F1 pauses native ownership, so a time expiry would otherwise
        // silently rebind a slider to the per-game base during a drag.
        if(!r.trimWriting.test_and_set(std::memory_order_acquire)) {
            r.trimVersion.fetch_add(1,std::memory_order_acq_rel);
            r.trimIdentity.store(before.vehicleIdentity,std::memory_order_relaxed);
            r.trimSeat.store(before.seat,std::memory_order_relaxed);
            r.trimGeneration.store(before.generation,std::memory_order_relaxed);
            r.trimVersion.fetch_add(1,std::memory_order_release);
            r.trimWriting.clear(std::memory_order_release);
        }
        runtime[I].positioned.fetch_add(1,std::memory_order_relaxed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Fault(I);
    }
}
template<unsigned I> uint32_t __fastcall SelectHook(uint32_t unit,Mode<I>* mode)
{
    auto& r=runtime[I];r.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint32_t result=0;
    __try {
        result=reinterpret_cast<SelectorFn<I>>(r.original[Selector])(unit,mode);
        if (SelectOwned<I>(unit,mode)) {result=0;r.selected.fetch_add(1,std::memory_order_relaxed);}
    } __finally {r.callbacks.fetch_sub(1,std::memory_order_release);}
    return result;
}
template<unsigned I> void __fastcall CameraHook(uint32_t unit,const void* facing,void* result)
{
    auto& r=runtime[I];r.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        reinterpret_cast<CameraFn>(r.original[Camera])(unit,facing,result);
        PositionOwned<I>(unit,result);
    } __finally {r.callbacks.fetch_sub(1,std::memory_order_release);}
}
void* const hooks[3][2]{
    {reinterpret_cast<void*>(&SelectHook<0>),reinterpret_cast<void*>(&CameraHook<0>)},
    {reinterpret_cast<void*>(&SelectHook<1>),reinterpret_cast<void*>(&CameraHook<1>)},
    {reinterpret_cast<void*>(&SelectHook<2>),reinterpret_cast<void*>(&CameraHook<2>)}
};
bool Prove(uintptr_t base,size_t size,const Binding& binding)
{
    if (binding.rva>=size) return false;
    const auto hit=sig::Find(base,size,binding.pattern);
    return hit==base+binding.rva&&!sig::Find(hit+1,base+size-hit-1,binding.pattern);
}
bool Retire(unsigned i)
{
    auto& r=runtime[i];r.requested.store(false,std::memory_order_release);
    for (unsigned j=0;j<2;++j) if (r.enabled[j]) {
        const auto status=MCCVR_DisableHookForRetirement(r.target[j]);
        if (status!=MH_OK&&status!=MH_ERROR_DISABLED) return false;
        r.enabled[j]=false;
    }
    // Only these two entries can enter this feature from the engine. Their
    // callback lease encloses Current/Owner/Head/Fault, including exception
    // cleanup. Requiring unwind metadata for the leaf Fault helper made every
    // retirement fail permanently in Release and retained the old title DLL.
    // The ingress ranges still cover the window before callback acquisition;
    // the count protects every nested helper and original until it returns.
    const void* functions[]{hooks[i][0],hooks[i][1]};
    const void* originals[]{r.original[0],r.original[1]};
    if (!WaitForNativeDetourQuiescence(functions,originals,2,r.callbacks)) return false;
    for (unsigned j=0;j<2;++j) if (r.target[j]) {
        if (MH_RemoveHook(r.target[j])!=MH_OK) return false;
        r.target[j]=r.original[j]=nullptr;
    }
    if (r.module) FreeLibrary(r.module);
    r.module=nullptr;r.base=0;r.generation=0;r.marker=r.markerInterpolated=nullptr;
    r.trimIdentity.store(0);r.trimGeneration.store(0);r.trimSeat.store(-1);
    r.attempted=r.ready=false;r.faulted.store(false,std::memory_order_release);return true;
}
bool Install(unsigned i,size_t size)
{
    auto& r=runtime[i];
    for (const auto& binding:kBindings[i].entries) if (!Prove(r.base,size,binding)) return false;
    r.marker=reinterpret_cast<void*>(r.base+kBindings[i].markerRva);
    // H2EK object marker argument 6 selects frame-interpolated object nodes.
    // This exact retail entry is already independently cold-proved above.
    if(i==1)r.markerInterpolated=reinterpret_cast<void*>(r.base+kBindings[i].entries[MarkerProof].rva);
    // Both detours are installed before admission. Partial installation stays
    // stock and is retired through the same quiescence path as a title change.
    for (unsigned j=0;j<2;++j) {
        void* target=reinterpret_cast<void*>(r.base+kBindings[i].entries[j].rva);
        if (MH_CreateHook(target,hooks[i][j],&r.original[j])!=MH_OK) return false;
        r.target[j]=target;
        if (MH_EnableHook(target)!=MH_OK) return false;
        r.enabled[j]=true;
    }
    return true;
}
}
bool NativeVehicleFirstPerson_TrimTarget(GameTitle title,uint64_t& identity,int& seat) noexcept
{
    identity=0;seat=-1;
    if(title!=TitleAdapter_GetActiveTitle())return false;
    for(unsigned i=0;i<3;++i)if(kBindings[i].title==title)
    {
        auto& r=runtime[i];const auto before=r.trimVersion.load(std::memory_order_acquire);
        if(before&1u)return false;
        const auto candidate=r.trimIdentity.load(std::memory_order_relaxed);
        const auto candidateSeat=r.trimSeat.load(std::memory_order_relaxed);
        const auto generation=r.trimGeneration.load(std::memory_order_relaxed);
        if(before!=r.trimVersion.load(std::memory_order_acquire)||!candidate||candidateSeat<0||candidateSeat>31||
            !generation||generation!=TitleAdapter_GetGeneration(title))return false;
        identity=candidate;seat=candidateSeat;return true;
    }
    return false;
}
void NativeVehicleFirstPerson_Poll()
{
    const auto active=TitleAdapter_GetActiveTitle();const uint64_t now=GetTickCount64();
    for (unsigned i=0;i<3;++i) {
        auto& r=runtime[i];const auto title=kBindings[i].title;
        const auto generation=TitleAdapter_GetGeneration(title);
        if (r.module&&(active!=title||generation!=r.generation)) {
            if (!Retire(i)) {
                if (now>=r.reportAt) {r.reportAt=now+2000;LOG("Vehicle first person title=%u: cleanup pending; feature stock",unsigned(title));}
                continue;
            }
        }
        if (active!=title||!generation) continue;
        if (!r.attempted&&g_config.vehicle_first_person) {
            const auto* descriptor=TitleRegistry_Find(title);
            if (!descriptor||!GetModuleHandleExW(0,descriptor->moduleName,&r.module)) continue;
            r.base=reinterpret_cast<uintptr_t>(r.module);r.generation=generation;r.attempted=true;
            const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(r.base);
            const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(r.base+dos->e_lfanew);
            r.ready=Install(i,nt->OptionalHeader.SizeOfImage);
            LOG("Vehicle first person %s: %s; existing toggle, local seated head marker, native entry/exit; headset verification pending",
                descriptor->displayName,r.ready?"installed":"STOCK FALLBACK (binding/hook)");
        }
        r.requested.store(r.ready&&g_config.vehicle_first_person,std::memory_order_release);
        if (r.module&&now>=r.reportAt) {
            r.reportAt=now+5000;
            LOG("Vehicle first person title=%u enabled=%d selected=%llu positioned=%llu missingHead=%llu faults=%llu%s",
                unsigned(title),r.requested.load()?1:0,r.selected.load(),r.positioned.load(),
                r.missingMarker.load(),r.faults.load(),r.faulted.load()?" STOCK FALLBACK (guarded access fault)":"");
        }
    }
}
