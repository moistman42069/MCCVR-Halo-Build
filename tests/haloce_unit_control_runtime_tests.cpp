#include <windows.h>
#include <MinHook.h>
#include "../src/dll/haloce_native_bindings.h"
#include <cstdio>
#include <limits>
namespace
{
bool retainOkay=true;unsigned retains{},releases{};
bool networkSession{};
BOOL WINAPI RetainModule(DWORD,LPCWSTR address,HMODULE* module)
{ if (!retainOkay) return FALSE;*module=reinterpret_cast<HMODULE>(const_cast<wchar_t*>(address));++retains;return TRUE; }
BOOL WINAPI ReleaseModule(HMODULE) { ++releases;return TRUE; }
}
#define GetModuleHandleExW RetainModule
#define FreeLibrary ReleaseModule
#include "../src/dll/haloce_unit_control.cpp"
#undef GetModuleHandleExW
#undef FreeLibrary

namespace
{
unsigned failures{},nativeCalls{},reads{},unwindFailures{},creates{},removes{},logs{},movementCalls{};
unsigned createFailureAt{},enableFailureAt{},enableCalls{};
GameTitle title=GameTitle::HaloCE;
uint32_t currentGeneration=7;
bool hasGameplay=true,contextCurrent=true,raiseNative{},mutateOwner{},mutateReference{},
    bindingOkay=true,quiescent=true,vehicleBindingOkay=true,vehicleParentOkay=true,
    vehicleUnitOkay=true,mutateVehicleParent{},mutateVehicleSeat{},mutateVehicleMode{},
    mutateVehicleRenderer{},mutateVehicleSpace{};
unsigned vehicleReads{},vehicleContextReads{};
uintptr_t fixtureBase{};
std::array<uint8_t,0x400> fixtureUnit{};
constexpr uint32_t vehicleId=0x34560009;
MH_STATUS createResult=MH_OK,enableResult=MH_OK,disableResult=MH_OK,removeResult=MH_OK;
RenderContext gameplay{};
HaloCELocalPlayerState localPlayer{};
UnitControlPacket consumed{};
UnitMovementBasis consumedMovement{};
const UnitControlPacket* consumedPointer{};
uint32_t consumedUnit{};int32_t consumedUpdate{};
void Check(bool result,const char* message)
{ if (!result) { ++failures;std::fprintf(stderr,"CE unit control: %s\n",message); } }
bool Near(Vec3 a,Vec3 b) { return Dot(a-b,a-b)<.000001f; }
template<class T> void PutUnit(size_t offset,T value)
{ std::memcpy(fixtureUnit.data()+offset,&value,sizeof(value)); }
uintptr_t __fastcall NativeObjectGet(uint32_t object,uint32_t mask)
{
    if (mask==1&&object==0x12340007&&vehicleUnitOkay)
        return reinterpret_cast<uintptr_t>(fixtureUnit.data());
    if (mask==2&&object==vehicleId&&vehicleParentOkay)
        return reinterpret_cast<uintptr_t>(fixtureUnit.data());
    return 0;
}
void __fastcall NativeControl(uint32_t unit,const UnitControlPacket* packet,int32_t update)
{
    ++nativeCalls;consumedPointer=packet;consumedUnit=unit;consumedUpdate=update;
    if (raiseNative) RaiseException(0xe0424242,0,0,nullptr);
    if (packet) consumed=*packet;
}
void __fastcall NativeMovement(void* data)
{
    ++movementCalls;
    std::memcpy(&consumedMovement,static_cast<uint8_t*>(data)+0x14,sizeof(consumedMovement));
    if (raiseNative) RaiseException(0xe0424242,0,0,nullptr);
    // Native motion writes its output beyond the two overridden input vectors.
    static_cast<uint8_t*>(data)[0xb8]=0x77;
}
bool NativeException()
{
    __try { UnitControlHook(0,nullptr,0); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
bool MovementException(void* data,bool hook)
{
    __try
    {
        if (hook) MovementHook(data);
        else MovementBody(data,moduleBase+contract::unit_control::movement_consumer_return);
    }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
void Reset()
{
    moduleBase=fixtureBase;generation=currentGeneration=7;active=true;retiring=false;
    networkSession=false;
    ready=true;original=reinterpret_cast<void*>(&NativeControl);target=nullptr;retained=nullptr;enabled=false;
    movementOriginal=reinterpret_cast<void*>(&NativeMovement);movementTarget=nullptr;movementEnabled=false;
    pendingCleanup=PendingCleanup::None;pinFailureReported=false;
    rejectedBase=0;rejectedGeneration=0;title=GameTitle::HaloCE;
    hasGameplay=contextCurrent=bindingOkay=quiescent=retainOkay=true;
    vehicleReady=vehicleBindingOkay=vehicleParentOkay=vehicleUnitOkay=true;
    mutateVehicleParent=mutateVehicleSeat=mutateVehicleMode=false;
    mutateVehicleRenderer=mutateVehicleSpace=false;
    vehicleReads=vehicleContextReads=0;fixtureUnit={};
    PutUnit(0xd8,uint32_t(0xffffffffu));PutUnit(0x2d0,int16_t(-1));
    raiseNative=mutateOwner=mutateReference=false;reads=0;
    createResult=enableResult=disableResult=removeResult=MH_OK;
    createFailureAt=enableFailureAt=0;
    gameplay={};gameplay.tracking.generation=gameplay.reference.generation=7;
    gameplay.tracking.serial=10;gameplay.tracking.spaceEpoch=gameplay.reference.spaceEpoch=2;
    gameplay.referenceRevision=3;gameplay.rendererEpoch=4;gameplay.unitsPerMeter=1;
    gameplay.camera.forward={1,0,0};gameplay.camera.up={0,0,1};
    gameplay.camera.viewport=gameplay.camera.window={0,0,100,100};
    gameplay.camera.verticalFov=1;gameplay.camera.nearPlane=.01f;gameplay.camera.farPlane=100;
    gameplay.tracking.controllers.controlsPresentationBlocked=false;
    gameplay.tracking.controllers.padValid=true;
    gameplay.tracking.controllers.primaryAim.valid=true;
    // In CE, XR yaw +90 points toward world +Y from a +X native camera.
    gameplay.tracking.headOrientation={0,.7071067812f,0,.7071067812f};
    localPlayer={};localPlayer.generation=7;localPlayer.unit=0x12340007;
    localPlayer.player=0x23450003;localPlayer.inputUser=0;localPlayer.nativePerspective=0;
    localPlayer.hasControlledUnit=localPlayer.onFoot=true;
    localPlayer.nativeInputBlocked=localPlayer.nativeLookBlocked=false;
}
void Seated()
{
    localPlayer.onFoot=false;localPlayer.parent=vehicleId;localPlayer.nativePerspective=1;
    PutUnit(0xd8,vehicleId);PutUnit(0x2d0,int16_t(0));
}
UnitControlPacket Packet()
{
    UnitControlPacket value{};for (size_t i=0;i<value.size();++i) value[i]=uint8_t(i+20);
    WriteUnitControl<uint16_t>(value,2,0x2000);
    WriteUnitControl<Vec3>(value,0xc,{.6f,.8f,.2f});
    WriteUnitControl<Vec3>(value,0x1c,{1,0,0});
    WriteUnitControl<Vec3>(value,0x28,{1,0,0});
    WriteUnitControl<Vec3>(value,0x34,{1,0,0});return value;
}
}
GameTitle TitleAdapter_GetActiveTitle() { return title; }
bool HaloCENetworkInput_UsesNativeSimulation() noexcept { return networkSession; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return currentGeneration; }
bool HaloCEControls_GetLocomotionFrame(HaloCELocalPlayerState& state,RenderContext& context) noexcept
{
    ++reads;if (!hasGameplay) return false;state=localPlayer;context=gameplay;
    if (reads>1&&mutateOwner) ++state.player;
    if (reads>1&&mutateReference) ++context.referenceRevision;
    return OnFootControls({state.hasControlledUnit,state.onFoot,state.nativePerspective==0,
        state.nativeInputBlocked,state.nativeLookBlocked,state.nativePaused,state.nativeCinematicFlag,
        context.tracking.controllers.controlsPresentationBlocked});
}
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{
    ++vehicleReads;if (!hasGameplay) return false;state=localPlayer;
    if (vehicleReads>1&&mutateOwner) ++state.player;
    if (vehicleReads>1&&mutateVehicleParent) ++state.parent;
    if (vehicleReads>1&&mutateVehicleSeat) PutUnit(0x2d0,int16_t(1));
    if (vehicleReads>1&&mutateVehicleMode) state.nativePerspective=0;
    return true;
}
bool HaloCE_GetGameplayContext(RenderContext& context) noexcept
{
    ++vehicleContextReads;if (!hasGameplay) return false;context=gameplay;
    if (vehicleContextReads>1&&mutateReference) ++context.referenceRevision;
    if (vehicleContextReads>1&&mutateVehicleRenderer) ++context.rendererEpoch;
    if (vehicleContextReads>1&&mutateVehicleSpace) ++context.tracking.spaceEpoch;
    return true;
}
bool HaloCE_RenderContextCurrent(const RenderContext&) noexcept { return contextCurrent; }
void Logf(const char*,...) { ++logs; }
bool halo_ce::VerifyNativeFeatureBindings(uintptr_t,size_t,uint32_t,const NativeContractSet& contracts,const char*& failure) noexcept
{ const bool okay=bindingOkay&&(contracts.entries.data()!=contract::vehicle::entries.data()||vehicleBindingOkay);
  failure=okay?nullptr:"fixture mismatch";return okay; }
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID hook,LPVOID* native)
{ ++creates;if (creates==createFailureAt) return MH_ERROR_MEMORY_ALLOC;
  if (createResult==MH_OK) *native=hook==reinterpret_cast<void*>(&UnitControlHook)?
    reinterpret_cast<void*>(&NativeControl):reinterpret_cast<void*>(&NativeMovement);return createResult; }
MH_STATUS WINAPI MH_EnableHook(LPVOID)
{ ++enableCalls;return enableCalls==enableFailureAt?MH_ERROR_MEMORY_PROTECT:enableResult; }
MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID) { return disableResult; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++removes;return removeResult; }
bool WaitForNativeDetourQuiescence(const void* const* entries,const void* const*,size_t count,
    const std::atomic<uint32_t>& pending)
{
    for (size_t i=0;i<count;++i)
    {
        DWORD64 image{};
        const auto* range=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entries[i]),&image,nullptr);
        if (!range||range->EndAddress<=range->BeginAddress) ++unwindFailures;
    }
    return quiescent&&!unwindFailures&&!pending.load();
}

int main()
{
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,contract::imageSize,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!image) return 2;
    fixtureBase=reinterpret_cast<uintptr_t>(image);
    // Exercise the production native object lookup through the same x64 ABI.
    uint8_t endpoint[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    const uintptr_t lookup=reinterpret_cast<uintptr_t>(&NativeObjectGet);
    std::memcpy(endpoint+2,&lookup,sizeof(lookup));
    auto* entry=image+contract::player_state::state_object_try_get;DWORD protection{};
    if (!VirtualProtect(entry,sizeof(endpoint),PAGE_EXECUTE_READWRITE,&protection)) return 2;
    std::memcpy(entry,endpoint,sizeof(endpoint));FlushInstructionCache(GetCurrentProcess(),entry,sizeof(endpoint));
    Reset();const auto packet=Packet();UnitControlPacket result{};bool aim{};
    networkSession=true;
    UnitControlBody(localPlayer.unit,&packet,17,moduleBase+0xad0d5b);
    Check(consumedPointer==&packet&&consumed==packet&&consumedUpdate==17,
        "network authority consumes original action packet without a fresh local pose override");
    UnitControlBody(localPlayer.unit,&packet,-1,moduleBase+0xad14d3);
    Check(consumedPointer==&packet&&consumed==packet,
        "network client prediction consumes the same packet unchanged");
    std::array<uint8_t,0xc0> networkMotion{};
    const UnitMovementBasis networkBasis{{0,1,0},{0,0,1}};
    std::memcpy(networkMotion.data(),&localPlayer.unit,sizeof(localPlayer.unit));
    std::memcpy(networkMotion.data()+0x14,&networkBasis,sizeof(networkBasis));
    MovementBody(networkMotion.data(),moduleBase+contract::unit_control::movement_consumer_return);
    Check(Near(consumedMovement.forward,networkBasis.forward)&&Near(consumedMovement.aim,networkBasis.aim),
        "network movement uses native replicated simulation basis, not a local camera replacement");
    Reset();
    Check(BuildTrackedUnitControl(gameplay,packet,result,aim)&&aim,"valid body and controller packet");
    Check(Near(ReadUnitControl<Vec3>(result,0x1c),{0,1,0})&&
        Near(ReadUnitControl<Vec3>(result,0x34),{0,1,0})&&
        Near(ReadUnitControl<Vec3>(result,0x28),{1,0,0}),"head/body and primary aim are independent");
    Check(Near(ReadUnitControl<Vec3>(result,0xc),{.6f,.8f,.2f}),"native throttle remains byte-identical");
    for (size_t i=0;i<packet.size();++i)
        if (!(i>=0x1c&&i<0x40))
            Check(result[i]==packet[i],"native actions, weapon/grenade/zoom, speed and aim assist preserved");
    // A vehicle consumes one controller direction, independently of the head.
    // Exercise world yaw and pitch (including aircraft pointing straight up),
    // with the same inverse recenter transform used by CE's rendered hands.
    for (float cameraYaw:{-2.7f,0.0f,1.4f})
        for (float referenceYaw:{-.6f,0.0f,.8f})
            for (float handYaw:{-1.1f,0.0f,1.3f})
                for (float pitch:{-.7f,0.0f,.6f,1.570796327f})
                {
                    Reset();auto& rig=gameplay.tracking.controllers;rig.padValid=true;
                    gameplay.camera.forward={std::cos(cameraYaw),std::sin(cameraYaw),0};
                    gameplay.reference.orientation={0,std::sin(referenceYaw/2),0,std::cos(referenceYaw/2)};
                    const Quat yaw{0,std::sin(handYaw/2),0,std::cos(handYaw/2)};
                    const Quat tilt{std::sin(pitch/2),0,0,std::cos(pitch/2)};
                    rig.primaryAim.orientation=Multiply(yaw,tilt);
                    const float worldYaw=cameraYaw+handYaw-referenceYaw;
                    const Vec3 expected{std::cos(worldYaw)*std::cos(pitch),
                        std::sin(worldYaw)*std::cos(pitch),std::sin(pitch)};
                    Check(BuildTrackedVehicleControl(gameplay,packet,result),"tracked vehicle packet admitted");
                    for (size_t offset:{size_t(0x1c),size_t(0x28),size_t(0x34)})
                        Check(Near(ReadUnitControl<Vec3>(result,offset),expected),
                            "vehicle direction follows world controller yaw/pitch, not head look");
                    for (size_t i=0;i<packet.size();++i)
                        if (!(i>=0x1c&&i<0x40))
                            Check(result[i]==packet[i],"vehicle preserves every nondirection byte");
                    const auto beforeHeadTurn=result;
                    gameplay.tracking.headOrientation={.5f,.5f,.5f,.5f};
                    Check(BuildTrackedVehicleControl(gameplay,packet,result)&&result==beforeHeadTurn,
                        "head look alone cannot steer the vehicle");
                }
    for (unsigned reason=0;reason<10;++reason)
    {
        Reset();gameplay.tracking.controllers.padValid=true;auto source=packet;
        result.fill(0xab);const auto before=result;
        switch (reason)
        {
        case 0:gameplay.tracking.generation++;break;
        case 1:gameplay.tracking.spaceEpoch++;break;
        case 2:gameplay.referenceRevision=0;break;
        case 3:gameplay.rendererEpoch=0;break;
        case 4:gameplay.tracking.controllers.controlsPresentationBlocked=true;break;
        case 5:gameplay.tracking.controllers.primaryAim.valid=false;break;
        case 6:gameplay.tracking.controllers.padValid=false;break;
        case 7:gameplay.tracking.controllers.primaryAim.orientation={0,0,0,0};break;
        case 8:WriteUnitControl<uint16_t>(source,2,0x100);break;
        case 9:gameplay.camera.forward.x=std::numeric_limits<float>::quiet_NaN();break;
        }
        Check(!BuildTrackedVehicleControl(gameplay,source,result)&&result==before,
            "invalid or untracked vehicle direction leaves packet untouched");
    }
    Reset();
    for (float yaw:{-3.0f,-1.5f,-.78f,0.0f,.78f,1.5f,3.0f})
    {
        gameplay.tracking.headOrientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
        auto diagonal=packet;WriteUnitControl<Vec3>(diagonal,0xc,{1,1,.2f});
        Check(BuildTrackedUnitControl(gameplay,diagonal,result,aim),"rotated diagonal admitted");
        Check(Near(ReadUnitControl<Vec3>(result,0xc),{1,1,.2f}),"diagonal throttle is never rotated or clipped");
    }
    // Exercise composition with the shipped XInput map, including CE's left
    // throttle sign. It must apply HMD yaw once across both adapters.
    for (float yaw:{-1.570796327f,-.785398164f,0.0f,.785398164f,1.570796327f})
        for (Vec3 stick:{Vec3{0,1,0},Vec3{1,0,0},Vec3{.6f,.8f,0}})
        {
            Reset();gameplay.tracking.headOrientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
            float x{},y{};Check(HeadRelativeMovement(gameplay,stick.x,stick.y,x,y),"XInput head map");
            auto moved=packet;WriteUnitControl<Vec3>(moved,0xc,{y,-x,0});
            Check(BuildTrackedUnitControl(gameplay,moved,result,aim),"body adapter after XInput map");
            UnitMovementBasis basis{};Check(BuildUnitMovementBasis(gameplay,basis),"private native movement basis");
            const auto throttle=ReadUnitControl<Vec3>(result,0xc);
            const Vec3 actual=basis.forward*throttle.x+Cross({0,0,1},basis.forward)*throttle.y;
            const Vec3 heading=ReadUnitControl<Vec3>(result,0x1c);
            Check(Near(actual,heading*stick.y+Cross(heading,{0,0,1})*stick.x),
                "combined XInput/body/movement adapters apply physical head yaw exactly once");
        }
    Reset();gameplay.tracking.controllers.primaryAim.valid=false;
    Check(BuildTrackedUnitControl(gameplay,packet,result,aim)&&!aim&&
        Near(ReadUnitControl<Vec3>(result,0x28),ReadUnitControl<Vec3>(packet,0x28)),
        "lost controller preserves native aim while head/body remains independent");
    for (unsigned reason=0;reason<8;++reason)
    {
        Reset();auto source=packet;result.fill(0xab);const auto before=result;
        switch (reason)
        {
        case 0:gameplay.tracking.generation++;break;
        case 1:gameplay.tracking.spaceEpoch++;break;
        case 2:gameplay.referenceRevision=0;break;
        case 3:gameplay.tracking.headOrientation={0,0,0,0};break;
        case 4:gameplay.tracking.controllers.controlsPresentationBlocked=true;break;
        case 5:WriteUnitControl<uint16_t>(source,2,0x100);break;
        case 6:gameplay.camera.forward.x=std::numeric_limits<float>::quiet_NaN();break;
        case 7:gameplay.tracking.spaceEpoch=0;break;
        }
        Check(!BuildTrackedUnitControl(gameplay,source,result,aim)&&result==before&&!aim,
            "stale, blocked and malformed packets decline without output mutation");
    }
    Reset();const auto before=packet;const auto count=nativeCalls;
    UnitControlBody(localPlayer.unit,&packet,42,moduleBase+0xad0d5b);
    Check(nativeCalls==count+1&&consumedPointer!=&packet&&packet==before&&
        consumedUnit==localPlayer.unit&&consumedUpdate==42&&Near(ReadUnitControl<Vec3>(consumed,0x1c),{0,1,0}),
        "production hook passes private packet exactly once, preserving unit and update ABI");
    for (unsigned reason=0;reason<18;++reason)
    {
        Reset();uint32_t unit=localPlayer.unit;uintptr_t caller=moduleBase+0xad0d5b;
        switch (reason)
        {
        case 0:caller++;break;case 1:unit++;break;case 2:ready=false;break;
        case 3:active=false;break;case 4:retiring=true;break;case 5:hasGameplay=false;break;
        case 6:contextCurrent=false;break;case 7:localPlayer.generation++;break;
        case 8:currentGeneration++;break;case 9:title=GameTitle::Halo3;break;
        case 10:localPlayer.onFoot=false;break;case 11:localPlayer.nativePaused=true;break;
        case 12:localPlayer.nativeCinematicFlag=true;break;case 13:localPlayer.nativeInputBlocked=true;break;
        case 14:localPlayer.nativeLookBlocked=true;break;case 15:localPlayer.nativePerspective=1;break;
        case 16:mutateOwner=true;break;case 17:mutateReference=true;break;
        }
        const auto calls=nativeCalls;UnitControlBody(unit,&packet,11,caller);
        Check(consumedPointer==&packet&&consumed==packet&&nativeCalls==calls+1,
            "foreign/stale/vehicle/cinematic/paused/changed owner retains original native call");
    }
    Reset();Seated();const auto vehicleCalls=nativeCalls;const auto vehicleBefore=vehicles.load();
    UnitControlBody(localPlayer.unit,&packet,47,moduleBase+0xad0d5b);
    Check(nativeCalls==vehicleCalls+1&&consumedPointer!=&packet&&packet==before&&
        consumedUnit==localPlayer.unit&&consumedUpdate==47&&vehicles.load()==vehicleBefore+1&&
        Near(ReadUnitControl<Vec3>(consumed,0x1c),{1,0,0})&&
        Near(ReadUnitControl<Vec3>(consumed,0x28),{1,0,0})&&
        Near(ReadUnitControl<Vec3>(consumed,0x34),{1,0,0}),
        "seated native hook is reachable independently of the on-foot locomotion getter");
    for (size_t i=0;i<packet.size();++i)
        if (!(i>=0x1c&&i<0x40))
            Check(consumed[i]==packet[i],"native seated packet preserves actions and throttle");
    Reset();Seated();
    const auto reticleNativeCalls=nativeCalls;
    Check(HaloCEUnitControl_VehicleAimCurrent(localPlayer,gameplay)&&!callbacks.load()&&
        nativeCalls==reticleNativeCalls,"vehicle crosshair reads the steering owner without submitting another control packet");
    Reset();Seated();localPlayer.nativePerspective=0;
    Check(HaloCEUnitControl_VehicleAimCurrent(localPlayer,gameplay),"first-person seated camera retains vehicle reticle ownership");
    UnitControlBody(localPlayer.unit,&packet,47,moduleBase+0xad0d5b);
    Check(consumedPointer!=&packet&&Near(ReadUnitControl<Vec3>(consumed,0x28),{1,0,0}),
        "first-person seated camera retains vehicle steering");
    for (unsigned reason=0;reason<32;++reason)
    {
        Reset();Seated();uint32_t unit=localPlayer.unit;uintptr_t caller=moduleBase+0xad0d5b;
        switch (reason)
        {
        case 0:caller++;break;case 1:unit++;break;case 2:ready=false;break;
        case 3:vehicleReady=false;break;case 4:active=false;break;case 5:retiring=true;break;
        case 6:hasGameplay=false;break;case 7:contextCurrent=false;break;
        case 8:localPlayer.generation++;break;case 9:currentGeneration++;break;
        case 10:title=GameTitle::Halo3;break;case 11:localPlayer.onFoot=true;break;
        case 12:localPlayer.nativePaused=true;break;case 13:localPlayer.nativeCinematicFlag=true;break;
        case 14:localPlayer.nativeInputBlocked=true;break;case 15:localPlayer.nativeLookBlocked=true;break;
        case 16:localPlayer.nativePerspective=2;break;case 17:mutateOwner=true;break;
        case 18:mutateReference=true;break;case 19:mutateVehicleParent=true;break;
        case 20:mutateVehicleSeat=true;break;case 21:mutateVehicleMode=true;break;
        case 22:vehicleParentOkay=false;break;case 23:vehicleUnitOkay=false;break;
        case 24:localPlayer.parent=0xffffffffu;break;case 25:localPlayer.parent=9;break;
        case 26:PutUnit(0x2d0,int16_t(-1));break;
        case 27:gameplay.tracking.controllers.primaryAim.valid=false;break;
        case 28:gameplay.tracking.controllers.padValid=false;break;
        case 29:gameplay.tracking.controllers.controlsPresentationBlocked=true;break;
        case 30:mutateVehicleRenderer=true;break;
        case 31:mutateVehicleSpace=true;break;
        }
        if (reason>=2)
        {
            Check(!HaloCEUnitControl_VehicleAimCurrent(localPlayer,gameplay)&&!callbacks.load(),
                "vehicle crosshair rejects invalid, changing or untracked native owner independently of stereo");
            vehicleReads=vehicleContextReads=0;PutUnit(0x2d0,int16_t(reason==26?-1:0));
        }
        const auto calls=nativeCalls;UnitControlBody(unit,&packet,11,caller);
        Check(consumedPointer==&packet&&consumed==packet&&nativeCalls==calls+1,
            "invalid, changing or untracked vehicle ownership invokes exact original once");
    }
    for (unsigned mismatch=0;mismatch<8;++mismatch)
    {
        Reset();Seated();auto hudPlayer=localPlayer;auto hudContext=gameplay;
        switch(mismatch)
        {
        case 0:++hudPlayer.player;break;case 1:++hudPlayer.parent;break;
        case 2:++hudPlayer.inputUser;break;case 3:++hudContext.tracking.generation;break;
        case 4:++hudContext.referenceRevision;break;case 5:++hudContext.rendererEpoch;break;
        case 6:++hudContext.tracking.spaceEpoch;break;case 7:hudPlayer.onFoot=true;break;
        }
        Check(!HaloCEUnitControl_VehicleAimCurrent(hudPlayer,hudContext)&&!callbacks.load(),
            "crosshair renderer/reference/player must match the current vehicle steering owner");
    }
    Reset();Seated();UnitControlBody(localPlayer.unit,&packet,47,moduleBase+0xad0d5b);
    localPlayer.onFoot=true;localPlayer.parent=0xffffffffu;localPlayer.nativePerspective=0;
    PutUnit(0xd8,uint32_t(0xffffffffu));PutUnit(0x2d0,int16_t(-1));
    UnitControlBody(localPlayer.unit,&packet,47,moduleBase+0xad0d5b);
    Check(Near(ReadUnitControl<Vec3>(consumed,0x1c),{0,1,0})&&
        Near(ReadUnitControl<Vec3>(consumed,0x28),{1,0,0}),
        "vehicle exit immediately restores existing separate head/body and controller aim");
    Reset();raiseNative=true;
    Check(NativeException()&&!callbacks.load()&&exceptions.load()==1,
        "real hook unwinds callback ownership while preserving native exception");
    Reset();std::array<uint8_t,0xc4> motion{};
    std::memcpy(motion.data(),&localPlayer.unit,sizeof(localPlayer.unit));
    const UnitMovementBasis nativeBasis{{0,1,0},{0,-1,0}};
    std::memcpy(motion.data()+0x14,&nativeBasis,sizeof(nativeBasis));const auto motionBefore=motion;
    MovementBody(motion.data(),moduleBase+contract::unit_control::movement_consumer_return);
    Check(Near(consumedMovement.forward,{1,0,0})&&Near(consumedMovement.aim,{1,0,0}),
        "native movement consumes original camera basis independently of current body/controller aim");
    Check(std::memcmp(motion.data()+0x14,&nativeBasis,sizeof(nativeBasis))==0&&motion[0xb8]==0x77,
        "temporary movement inputs restore while native collision/velocity outputs survive");
    Reset();MovementBody(motion.data(),moduleBase+contract::unit_control::movement_consumer_return_secondary);
    Check(Near(consumedMovement.forward,{1,0,0})&&
        std::memcmp(motion.data()+0x14,&nativeBasis,sizeof(nativeBasis))==0,
        "second independently verified biped producer uses the same temporary basis");
    Reset();raiseNative=true;
    Check(MovementException(motion.data(),false)&&
        std::memcmp(motion.data()+0x14,&nativeBasis,sizeof(nativeBasis))==0,
        "native motion exception restores the temporary input basis");
    const auto exceptionBefore=exceptions.load();
    Check(MovementException(motion.data(),true)&&!callbacks.load()&&exceptions.load()==exceptionBefore+1,
        "real movement hook releases callback ownership during native exception unwind");
    Reset();movementCalls=0;MovementBody(motion.data(),moduleBase+contract::unit_control::movement_consumer_return+1);
    Check(movementCalls==1&&Near(consumedMovement.forward,nativeBasis.forward),"foreign motion caller stays stock");
    Reset();Seated();
    for (uintptr_t caller:{uintptr_t(contract::unit_control::movement_consumer_return),
        uintptr_t(contract::unit_control::movement_consumer_return_secondary)})
    {
        const auto calls=movementCalls;MovementBody(motion.data(),moduleBase+caller);
        Check(movementCalls==calls+1&&Near(consumedMovement.forward,nativeBasis.forward)&&
            std::memcmp(motion.data()+0x14,&nativeBasis,sizeof(nativeBasis))==0,
            "both biped movement producers remain entirely native while seated");
    }
    Reset();target=reinterpret_cast<void*>(0x123);retained=reinterpret_cast<HMODULE>(moduleBase);enabled=true;
    disableResult=MH_ERROR_MEMORY_PROTECT;
    const auto logsBefore=logs;
    Check(!Remove()&&target&&original&&retained&&retiring.load()&&!ready.load(),
        "failed disable retains module and trampoline with optional feature revoked");
    Check(!Remove()&&logs==logsBefore+1,"pending cleanup logs once per unchanged failure episode");
    disableResult=MH_OK;quiescent=false;
    Check(!Remove()&&target&&original&&retained,"unproven quiescence retains resources");
    quiescent=true;removeResult=MH_ERROR_MEMORY_PROTECT;
    Check(!Remove()&&target&&original&&retained,"failed removal retains resources");
    removeResult=MH_OK;Check(Remove()&&!target&&!retained&&!original&&!unwindFailures,
        "retirement recovers with compiled unwind metadata");
    Reset();ready=false;original=nullptr;const uintptr_t base=moduleBase;
    Check(HaloCEUnitControl_Poll(base,0x4000000,7,true),"initial install is immediately current");
    currentGeneration=8;
    Check(HaloCEUnitControl_Poll(base,0x4000000,8,true)&&generation.load()==8&&active.load(),
        "generation replacement restores active admission in the same poll");
    Check(!HaloCEUnitControl_Poll(base,0x4000000,8,false)&&!retained,"inactive poll retires optional feature");
    Reset();ready=false;original=nullptr;vehicleBindingOkay=false;
    Check(HaloCEUnitControl_Poll(base,0x4000000,7,true)&&ready.load()&&!vehicleReady.load(),
        "vehicle evidence mismatch preserves installed on-foot unit and movement hooks");
    UnitControlBody(localPlayer.unit,&packet,47,moduleBase+0xad0d5b);
    Check(consumedPointer!=&packet&&Near(ReadUnitControl<Vec3>(consumed,0x1c),{0,1,0}),
        "vehicle binding fallback cannot disable working on-foot controller/body behavior");
    Seated();UnitControlBody(localPlayer.unit,&packet,47,moduleBase+0xad0d5b);
    Check(consumedPointer==&packet&&consumed==packet,"unproved vehicle bindings preserve exact stock packet");
    (void)Remove();
    Reset();ready=false;original=nullptr;enableResult=MH_ERROR_MEMORY_PROTECT;
    const auto removeBefore=removes;
    Check(!HaloCEUnitControl_Poll(base,0x4000000,7,true)&&!retained&&!target&&removes==removeBefore+2,
        "partial enable failure cleans up both exact created hooks");
    enableResult=MH_OK;const auto createBefore=creates;
    Check(!HaloCEUnitControl_Poll(base,0x4000000,7,true)&&creates==createBefore,
        "failed binding is not retried every worker tick");
    Check(HaloCEUnitControl_Poll(base+0x1000,0x4000000,7,true),
        "rejected generation does not poison another loaded module base");
    (void)Remove();
    Reset();ready=false;original=nullptr;createFailureAt=creates+2;const auto beforeCreateCleanup=removes;
    Check(!HaloCEUnitControl_Poll(base,0x4000000,7,true)&&!retained&&!target&&!movementTarget&&
        removes==beforeCreateCleanup+1,"second hook create failure retires the first hook");
    Reset();ready=false;original=nullptr;enableFailureAt=enableCalls+2;const auto beforeEnableCleanup=removes;
    Check(!HaloCEUnitControl_Poll(base,0x4000000,7,true)&&!retained&&!target&&!movementTarget&&
        removes==beforeEnableCleanup+2&&!enabled&&!movementEnabled,
        "second hook enable failure disables and retires the already-enabled first hook");
    Reset();ready=false;retainOkay=false;const auto pinLogs=logs;
    Check(!HaloCEUnitControl_Poll(base,0x4000000,7,true)&&
        !HaloCEUnitControl_Poll(base,0x4000000,7,true)&&logs==pinLogs+1,
        "module retention fallback is visible once while retries remain possible");
    std::printf("CE unit-control production checks: %s\n",failures?"FAIL":"PASS");
    VirtualFree(image,0,MEM_RELEASE);
    return failures?1:0;
}
