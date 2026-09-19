#include <windows.h>
#include <MinHook.h>
#include <cstdio>
#include <limits>
#include <fstream>
namespace {
BOOL WINAPI Retain(DWORD,LPCWSTR value,HMODULE* output) { *output=(HMODULE)value;return TRUE; }
BOOL WINAPI Release(HMODULE) { return TRUE; }
}
#define GetModuleHandleExW Retain
#define FreeLibrary Release
#include "../src/dll/haloce_network_input.cpp"
#undef GetModuleHandleExW
#undef FreeLibrary

namespace {
unsigned failures{},calls{},ownerReads{},removes{},unwinds{};
bool nativeFault{},stateOkay=true,frameOkay=true,currentOkay=true,mutateOwner{},mutateEpoch{},
    mutateSeat{},proofOkay=true,quiescent=true;
MH_STATUS createStatus=MH_OK,enableStatus=MH_OK,disableStatus=MH_OK,removeStatus=MH_OK;
uint32_t testGeneration=7;
GameTitle testTitle=GameTitle::HaloCE;
HaloCELocalPlayerState player{};
RenderContext context{};
PlayerAction seed{};
uintptr_t image{},control{};
std::array<uint8_t,0x400> unit{};
constexpr uint32_t unitId=0x12340007,parentId=0x45670008;
void Check(bool okay,const char* why) { if (!okay) { ++failures;std::fprintf(stderr,"CE network: %s\n",why); } }
template<class T> void Put(uintptr_t at,T value) { std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value)); }
bool Near(float a,float b) { return std::fabs(a-b)<.0001f; }
bool Near(Vec3 a,Vec3 b) { return Dot(a-b,a-b)<.000001f; }
void __fastcall Native(const void*,PlayerAction* out)
{ ++calls;if (nativeFault) RaiseException(0xe0424242,0,0,nullptr);if(out)*out=seed; }
uintptr_t __fastcall Object(uint32_t id,uint32_t mask)
{ return (id==unitId&&mask==1)||(id==parentId&&mask==2)?reinterpret_cast<uintptr_t>(unit.data()):0; }
uint8_t __fastcall Seat(uint32_t,Vec3* axis)
{ *axis={-axis->y,axis->x,axis->z};return 1; }
void Endpoint(uint32_t rva,uintptr_t service)
{
    uint8_t code[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    std::memcpy(code+2,&service,8);DWORD old{};
    VirtualProtect(reinterpret_cast<void*>(image+rva),sizeof(code),PAGE_EXECUTE_READWRITE,&old);
    std::memcpy(reinterpret_cast<void*>(image+rva),code,sizeof(code));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image+rva),sizeof(code));
}
const void* Source() { return reinterpret_cast<void*>(control+0x14+player.inputUser*0x58); }
void Reset()
{
    moduleBase=image;generation=testGeneration=7;testTitle=GameTitle::HaloCE;
    active=ready=true;retiring=enabled=false;target=nullptr;retained=nullptr;
    original=reinterpret_cast<void*>(&Native);callbacks=0;ownerReads=0;
    stateOkay=frameOkay=currentOkay=proofOkay=quiescent=true;
    nativeFault=mutateOwner=mutateEpoch=mutateSeat=false;
    rejectedBase=0;rejectedGeneration=0;
    createStatus=enableStatus=disableStatus=removeStatus=MH_OK;
    Put(image+contract::network_input::connection_type,int16_t(1));
    Put(image+contract::network_input::control_pointer,control);
    player={};player.generation=7;player.player=0x34560002;player.unit=unitId;
    player.inputUser=2;player.nativePerspective=0;player.hasControlledUnit=player.onFoot=true;
    player.nativeInputBlocked=player.nativeLookBlocked=false;
    context={};context.camera.forward={1,0,0};context.camera.up={0,0,1};
    context.camera.viewport=context.camera.window={0,0,100,100};context.camera.verticalFov=1;
    context.camera.nearPlane=.01f;context.camera.farPlane=100;
    context.tracking.generation=context.reference.generation=7;
    context.tracking.serial=12;context.referenceRevision=3;context.rendererEpoch=4;
    context.tracking.spaceEpoch=context.reference.spaceEpoch=2;context.unitsPerMeter=1;
    auto& rig=context.tracking.controllers;rig.padValid=true;rig.primaryAim.valid=true;
    rig.controlsPresentationBlocked=false;
    rig.primaryAim.orientation={0,.70710678f,0,.70710678f};
    unit={};Put(reinterpret_cast<uintptr_t>(unit.data())+0xd8,parentId);
    Put(reinterpret_cast<uintptr_t>(unit.data())+0x2d0,int16_t(0));
    for(size_t i=0;i<seed.size();++i) seed[i]=uint8_t(i*7+3);
    WriteAction(seed,0,uint32_t(0x8040));WriteAction(seed,4,0.f);WriteAction(seed,8,0.f);
    WriteAction(seed,0xc,Vec3{.6f,.8f,.2f});
}
bool Fault()
{
    __try { ActionHook(Source(),nullptr); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
}
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return testGeneration; }
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& out) noexcept
{
    ++ownerReads;out=player;
    if(ownerReads>1&&mutateOwner) ++out.unit;
    if(ownerReads>1&&mutateSeat) Put(reinterpret_cast<uintptr_t>(unit.data())+0x2d0,int16_t(1));
    return stateOkay;
}
bool HaloCE_GetGameplayContext(RenderContext& out) noexcept
{ out=context;if(ownerReads>1&&mutateEpoch)++out.referenceRevision;return frameOkay; }
bool HaloCE_RenderContextCurrent(const RenderContext&) noexcept { return currentOkay; }
void Logf(const char*,...) {}
bool halo_ce::VerifyNativeFeatureBindings(uintptr_t,size_t,uint32_t,const NativeContractSet&,const char*& failure) noexcept
{ failure=proofOkay?nullptr:"fixture mismatch";return proofOkay; }
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* out) { *out=reinterpret_cast<void*>(&Native);return createStatus; }
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return enableStatus; }
MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID) { return disableStatus; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++removes;return removeStatus; }
bool WaitForNativeDetourQuiescence(const void* const* entries,const void* const*,size_t count,const std::atomic<uint32_t>& pending)
{
    for(size_t i=0;i<count;++i) {
        DWORD64 base{};const auto* range=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entries[i]),&base,nullptr);
        Check(range&&range->EndAddress>range->BeginAddress,"production retirement has real unwind range");++unwinds;
    }
    return quiescent&&!pending.load();
}
int main(int argc,char** argv)
{
    std::ofstream vectors;
    if(argc==2) vectors.open(argv[1],std::ios::binary|std::ios::trunc);
    image=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,contract::imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;control=image+0x10000;
    Endpoint(contract::network_input::seat_direction_transform,reinterpret_cast<uintptr_t>(&Seat));
    Endpoint(contract::player_state::state_object_try_get,reinterpret_cast<uintptr_t>(&Object));
    Reset();
    // All bearings/elevations, including both poles and wrap-around. Byte
    // preservation includes grenade/fire choices, predicted aim assist and zoom.
    for(float oldYaw:{-3.1f,-1.f,0.f,2.f,3.1f}) for(float newYaw:{-3.f,-.5f,0.f,1.57f,3.f})
        for(float pitch:{-1.570796327f,-.8f,0.f,.7f,1.570796327f}) {
            PlayerAction before=seed,out{};WriteAction(before,4,oldYaw);
            const Vec3 direction{std::cos(newYaw)*std::cos(pitch),std::sin(newYaw)*std::cos(pitch),std::sin(pitch)};
            Check(BuildNetworkAction(before,direction,true,out),"finite network action accepted");
            const float y=ReadAction<float>(out,4),p=ReadAction<float>(out,8);
            Check(Near(Vec3{std::cos(y)*std::cos(p),std::sin(y)*std::cos(p),std::sin(p)},direction),"native decoded angle reproduces tracked aim");
            Check(y>=0&&y<6.28318530718f,"outgoing yaw preserves native nonnegative angular domain");
            const Vec3 t=ReadAction<Vec3>(out,0xc),old=ReadAction<Vec3>(before,0xc);
            Check(Near(std::cos(y)*t.x-std::sin(y)*t.y,std::cos(oldYaw)*old.x-std::sin(oldYaw)*old.y)&&
                Near(std::sin(y)*t.x+std::cos(y)*t.y,std::sin(oldYaw)*old.x+std::cos(oldYaw)*old.y)&&Near(t.z,old.z),
                "world throttle and magnitude survive action bearing change");
            for(size_t i=0;i<out.size();++i)if(i<4||i>=0x18)Check(before[i]==out[i],"actions and assist bytes unchanged");
            if(vectors) {
                vectors.write(reinterpret_cast<const char*>(before.data()),before.size());
                vectors.write(reinterpret_cast<const char*>(out.data()),out.size());
                vectors.write(reinterpret_cast<const char*>(&direction),sizeof(direction));
            }
            Check(BuildNetworkAction(before,direction,false,out),"vehicle action accepted");
            Check(std::memcmp(out.data()+0xc,before.data()+0xc,12)==0,"vehicle throttle byte-identical");
        }
    PlayerAction result{};
    for(int mode:{0,1,2,3,4,-1}) {
        Reset();Put(image+contract::network_input::connection_type,int16_t(mode));
        const auto n=calls;ActionBody(Source(),&result,moduleBase+0xa9988d);
        Check(calls==n+1,"native action constructor called exactly once");
        Check(HaloCENetworkInput_UsesNativeSimulation()==(mode==1||mode==2),"client/server only");
        Check((result!=seed)==(mode==1||mode==2),"local campaign and playback unchanged");
    }
    for(unsigned reason=0;reason<16;++reason) {
        Reset();auto* source=Source();uintptr_t caller=moduleBase+0xa9988d;
        switch(reason) {
        case 0:source=reinterpret_cast<void*>(control+0x14);break;
        case 1:caller++;break;case 2:player.nativeInputBlocked=true;break;
        case 3:player.nativeLookBlocked=true;break;case 4:player.nativePaused=true;break;
        case 5:player.nativeCinematicFlag=true;break;case 6:player.hasControlledUnit=false;break;
        case 7:context.tracking.controllers.primaryAim.valid=false;break;
        case 8:context.tracking.controllers.controlsPresentationBlocked=true;break;
        case 9:++testGeneration;break;case 10:mutateOwner=true;break;case 11:mutateEpoch=true;break;
        case 12:currentOkay=false;break;case 13:player.inputUser=4;break;
        case 14:WriteAction(seed,4,std::numeric_limits<float>::quiet_NaN());break;
        case 15:WriteAction(seed,0,uint32_t(0x100));break;
        }
        ActionBody(source,&result,caller);Check(result==seed,"foreign/stale/blocked/invalid action stays native");
    }
    Reset();player.onFoot=false;player.parent=parentId;player.nativePerspective=1;
    WriteAction(seed,4,.2f);
    ActionBody(Source(),&result,moduleBase+0xa9988d);
    const float y=ReadAction<float>(result,4),p=ReadAction<float>(result,8);
    Vec3 local{std::cos(y)*std::cos(p),std::sin(y)*std::cos(p),std::sin(p)};
    Seat(unitId,&local);Check(Near(local,{0,1,0}),"native seat transform applies exactly once");
    Check(std::memcmp(result.data()+0xc,seed.data()+0xc,12)==0,"seated ABI preserves drive throttle");
    ownerReads=0;mutateSeat=true;ActionBody(Source(),&result,moduleBase+0xa9988d);
    Check(result==seed,"seat transition rejects stale basis");
    Reset();nativeFault=true;const auto before=calls;
    Check(Fault()&&calls==before+1&&!callbacks.load(),"native exception propagates once and releases ownership");
    Reset();ready=false;original=nullptr;
    Check(HaloCENetworkInput_Poll(image,contract::imageSize,7,true),"independent network hook installs");
    disableStatus=MH_ERROR_MEMORY_PROTECT;
    Check(!HaloCENetworkInput_Poll(image,contract::imageSize,7,false)&&retained&&original&&!ready.load(),"failed disable retains dependencies");
    disableStatus=MH_OK;quiescent=false;
    Check(!HaloCENetworkInput_Poll(image,contract::imageSize,7,false)&&target,"busy callback blocks removal");
    quiescent=true;
    Check(!HaloCENetworkInput_Poll(image,contract::imageSize,7,false)&&!retained&&!target,"retirement completes without rearming");
    Reset();ready=false;original=nullptr;enableStatus=MH_ERROR_MEMORY_PROTECT;
    const auto removed=removes;
    Check(!HaloCENetworkInput_Poll(image,contract::imageSize,7,true)&&removes==removed+1&&!retained,"partial install removes exact created hook");
    VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);
    std::printf("CE network production/geometry checks: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
