#include <windows.h>
#include <MinHook.h>
#include "../src/dll/haloce_native_bindings.h"
MH_STATUS WINAPI TestCreate(LPVOID,LPVOID,LPVOID*);
MH_STATUS WINAPI TestEnable(LPVOID);
extern "C" MH_STATUS WINAPI TestDisable(LPVOID);
MH_STATUS WINAPI TestRemove(LPVOID);
namespace halo_ce { bool TestVerify(uintptr_t,size_t,uint32_t,const NativeContractSet&,const char*&) noexcept; }
#define MH_CreateHook TestCreate
#define MH_EnableHook TestEnable
#define MCCVR_DisableHookForRetirement TestDisable
#define MH_RemoveHook TestRemove
#define VerifyNativeFeatureBindings TestVerify
#include "../src/dll/haloce_orientation.cpp"
#undef MH_CreateHook
#undef MH_EnableHook
#undef MCCVR_DisableHookForRetirement
#undef MH_RemoveHook
#undef VerifyNativeFeatureBindings
#include <cstdio>
#include <limits>

namespace
{
unsigned checks{},failures{},effectCalls{},audioCalls{},unwindFailures{};
unsigned createCalls{},removeCalls{};
GameTitle title=GameTitle::HaloCE;
uint32_t currentGeneration=7;
bool hasGameplay=true,contextCurrent=true,raiseEffect{},raiseAudio{},retireDuringEffect{};
bool bindingFailure{},enableFailure{},disableFailure{},removeFailure{},quiescenceFailure{};
const AudioListenerPacket* passedPacket{};
AudioListenerPacket heardPacket{};
RenderContext gameplay{};
HaloCELocalPlayerState localPlayer{};
EffectMatrix nativeEffect{1,.98f,.2f,0,-.2f,.98f,0,0,0,1,.02f,-.04f,.01f};
void Check(bool result,const char* message)
{ ++checks;if (!result) { ++failures;std::fprintf(stderr,"CE orientation: %s\n",message); } }
bool Near(Vec3 a,Vec3 b) { return Dot(a-b,a-b)<0.000001f; }
void __fastcall NativeAudio(int32_t,const AudioListenerPacket* packet)
{
    ++audioCalls;passedPacket=packet;
    if (raiseAudio) RaiseException(0xe0424242,0,0,nullptr);
    if (packet) heardPacket=*packet;
}
AudioListenerPacket SourcePacket()
{
    AudioListenerPacket p{};
    p.position=p.matrixPosition={3,-2,1.5f};
    p.forward=p.matrixForward={1,0,0};p.up=p.matrixUp={0,0,1};
    p.matrixLeft={0,1,0};p.velocity={.1f,-.2f,.3f};p.scale=1;
    p.environment.fill(std::byte{0x5a});p.tail.fill(std::byte{0xc3});return p;
}
void __fastcall NativeEffect(int16_t,EffectMatrix* output)
{
    ++effectCalls;
    if (raiseEffect) RaiseException(0xe0424242,0,0,nullptr);
    if (output) *output=nativeEffect;
    if (retireDuringEffect) retiring=true;
}
bool NativeException()
{
    __try { EffectHook(0,nullptr); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
bool AudioException()
{
    __try { AudioHook(0,nullptr); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
void Reset()
{
    moduleBase=0x180000000;generation=currentGeneration=7;active=true;retiring=false;
    effect.ready=true;effect.original=reinterpret_cast<void*>(&NativeEffect);
    audio.ready=true;audio.original=reinterpret_cast<void*>(&NativeAudio);
    hasGameplay=contextCurrent=true;title=GameTitle::HaloCE;
    gameplay={};
    gameplay.tracking.generation=7;localPlayer.generation=7;
    gameplay.tracking.serial=20;gameplay.tracking.spaceEpoch=2;
    gameplay.reference.generation=7;gameplay.reference.spaceEpoch=2;
    gameplay.referenceRevision=1;gameplay.rendererEpoch=1;
    gameplay.camera.forward={1,0,0};gameplay.camera.up={0,0,1};
    gameplay.camera.verticalFov=1;gameplay.camera.nearPlane=.01f;gameplay.camera.farPlane=100;
    gameplay.camera.viewport=gameplay.camera.window={0,0,1000,1000};
    retireDuringEffect=raiseEffect=raiseAudio=false;
}
}
MH_STATUS WINAPI TestCreate(LPVOID,LPVOID hook,LPVOID* original)
{
    ++createCalls;
    *original=hook==reinterpret_cast<void*>(&EffectHook)?
        reinterpret_cast<void*>(&NativeEffect):reinterpret_cast<void*>(&NativeAudio);
    return MH_OK;
}
MH_STATUS WINAPI TestEnable(LPVOID target)
{ return enableFailure&&target==reinterpret_cast<void*>(moduleBase+contract::camera_effect::camera_effect_transform)?MH_ERROR_MEMORY_PROTECT:MH_OK; }
MH_STATUS WINAPI TestDisable(LPVOID) { return disableFailure?MH_ERROR_MEMORY_PROTECT:MH_OK; }
MH_STATUS WINAPI TestRemove(LPVOID) { ++removeCalls;return removeFailure?MH_ERROR_MEMORY_PROTECT:MH_OK; }
bool halo_ce::TestVerify(uintptr_t,size_t,uint32_t,const NativeContractSet&,const char*& reason) noexcept
{ reason=bindingFailure?"fixture mismatch":nullptr;return !bindingFailure; }
GameTitle TitleAdapter_GetActiveTitle() { return title; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return currentGeneration; }
bool HaloCEControls_GetLocomotionFrame(HaloCELocalPlayerState& state,RenderContext& context) noexcept
{ if (!hasGameplay) return false;state=localPlayer;context=gameplay;return true; }
bool HaloCE_RenderContextCurrent(const RenderContext&) noexcept { return contextCurrent; }
#if HALOMCCVR_DEV_DIAGNOSTICS
// The dev sound listener witness asks for the last gameplay context when the hook did not admit the packet.
bool witnessContext{};unsigned witnessLines{};
bool HaloCE_GetGameplayContext(RenderContext& context) noexcept
{ if (!witnessContext) return false;context=gameplay;return true; }
void Logf(const char* format,...) { if (format&&std::strstr(format,"SOUND LISTENER Halo CE (dev witness)")) ++witnessLines; }
#else
void Logf(const char*,...) {}
#endif
bool WaitForNativeDetourQuiescence(const void* const* entries,const void* const*,size_t count,
    const std::atomic<uint32_t>& pending)
{
    for (size_t i=0;i<count;++i)
    {
        DWORD64 image{};
        const auto* range=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entries[i]),&image,nullptr);
        if (!range||range->EndAddress<=range->BeginAddress) ++unwindFailures;
    }
    return !quiescenceFailure&&!unwindFailures&&!pending.load();
}

int main(int argc,char** argv)
{
    Reset();EffectMatrix value{};
    EffectBody(0,&value,moduleBase+0xac461d);
    Check(value==identityEffect&&effectCalls==1&&suppressed.load()==1,
        "native effect updates once before only local camera output becomes identity");
    for (unsigned reason=0;reason<10;++reason)
    {
        Reset();int16_t user=0;uintptr_t caller=moduleBase+0xac461d;
        switch (reason)
        {
        case 0:caller++;break;
        case 1:user=1;break;
        case 2:effect.ready=false;break;
        case 3:active=false;break;
        case 4:retiring=true;break;
        case 5:hasGameplay=false;break;
        case 6:contextCurrent=false;break;
        case 7:localPlayer.generation=8;break;
        case 8:currentGeneration=8;break;
        case 9:title=GameTitle::Halo3;break;
        }
        value={};const auto before=effectCalls;
        EffectBody(user,&value,caller);
        Check(value==nativeEffect&&effectCalls==before+1,
            "foreign/blocked/stale/retired call retains exact native effect");
    }
    Reset();retireDuringEffect=true;value={};
    EffectBody(0,&value,moduleBase+0xac461d);
    Check(value==nativeEffect,"retirement during native work refuses late output write");
    Reset();raiseEffect=true;
    Check(NativeException()&&!callbacks.load()&&exceptions.load()==1,
        "actual hook entry releases callback ownership while native exception propagates");
    Reset();value={};EffectBody(0,&value,moduleBase+0xac461d);
    Check(value==identityEffect,"valid camera recovers after native exception");
    const void* entries[]{reinterpret_cast<const void*>(&EffectHook),reinterpret_cast<const void*>(&AudioHook)};
    const void* originals[]{effect.original,audio.original};
    Check(WaitForNativeDetourQuiescence(entries,originals,2,callbacks)&&!unwindFailures,
        "compiled hook entry has real Windows unwind metadata for retirement");
    callbacks=1;
    Check(!WaitForNativeDetourQuiescence(entries,originals,1,callbacks),
        "outstanding callback blocks retirement");
    callbacks=0;
    Reset();AudioListenerPacket source=SourcePacket();
    const auto saved=source;
    constexpr float halfPi=.7853981633974483f;
    gameplay.tracking.headOrientation={0,std::sin(halfPi),0,std::cos(halfPi)};
    AudioBody(0,&source,moduleBase+0xb4cefe);
    Check(passedPacket!=&source&&Near(heardPacket.forward,{0,1,0})&&
        Near(heardPacket.up,{0,0,1}),"audio uses private packet with native HMD yaw");
    Check(!std::memcmp(&source,&saved,sizeof(source)),"audio leaves native packet unchanged");
    Check(Near(heardPacket.position,source.position)&&Near(heardPacket.matrixPosition,source.matrixPosition)&&
        heardPacket.environment==source.environment&&heardPacket.tail==source.tail&&heardPacket.scale==source.scale,
        "audio preserves position, environment, scale and opaque bytes");
    const Vec3 beforeVelocity=source.matrixForward*source.velocity.x+
        source.matrixLeft*source.velocity.z-source.matrixUp*source.velocity.y;
    const Vec3 afterVelocity=heardPacket.matrixForward*heardPacket.velocity.x+
        heardPacket.matrixLeft*heardPacket.velocity.z-heardPacket.matrixUp*heardPacket.velocity.y;
    Check(Near(beforeVelocity,afterVelocity),"native audio world velocity survives HMD rotation");
    if (argc==2)
    {
        FILE* file{};fopen_s(&file,argv[1],"wb");Check(file!=nullptr,"open emitted production audio packet fixture");
        if (file)
        {
            Check(fwrite(&source,sizeof(source),1,file)==1&&fwrite(&heardPacket,sizeof(heardPacket),1,file)==1,
                "write original and adapted production packet");fclose(file);
        }
    }
    // Head pitch and roll remain physical; reference pitch/roll never tilt the room.
    for (unsigned axis=0;axis<3;++axis)
    {
        Reset();const float s=std::sin(.21f),c=std::cos(.21f);
        gameplay.tracking.headOrientation=axis==0?Quat{s,0,0,c}:axis==1?Quat{0,s,0,c}:Quat{0,0,s,c};
        AudioBody(0,&source,moduleBase+0xb4cefe);
        Tracking track=gameplay.tracking;track.eyes[0].orientation=track.headOrientation;
        Camera eye{};Check(BuildEye(gameplay.camera,track,gameplay.reference,0,1,false,{1,1,1},eye)&&
            Near(heardPacket.forward,eye.forward)&&Near(heardPacket.up,eye.up),
            "audio yaw/pitch/roll match actual production eye orientation");
    }
    for (unsigned reason=0;reason<15;++reason)
    {
        Reset();source=SourcePacket();int32_t listener=0;uintptr_t caller=moduleBase+0xb4cefe;
        switch (reason)
        {
        case 0:caller++;break;
        case 1:listener=1;break;
        case 2:audio.ready=false;break;
        case 3:active=false;break;
        case 4:retiring=true;break;
        case 5:hasGameplay=false;break;
        case 6:contextCurrent=false;break;
        case 7:localPlayer.generation=8;break;
        case 8:currentGeneration=8;break;
        case 9:title=GameTitle::Halo3;break;
        case 10:gameplay.reference.spaceEpoch++;break;
        case 11:gameplay.tracking.headOrientation.w=std::numeric_limits<float>::quiet_NaN();break;
        case 12:source.matrixLeft={};break;
        case 13:source.velocity.x=std::numeric_limits<float>::infinity();break;
        case 14:gameplay.referenceRevision=0;break;
        }
        const auto before=audioCalls;AudioBody(listener,&source,caller);
        Check(passedPacket==&source&&audioCalls==before+1,
            "foreign/blocked/stale/invalid audio passes original packet exactly once");
    }
    Reset();raiseAudio=true;const auto priorExceptions=exceptions.load();
    Check(AudioException()&&!callbacks.load()&&exceptions.load()==priorExceptions+1,
        "audio hook exception propagates and releases callback ownership");
    Reset();source=SourcePacket();AudioBody(0,&source,moduleBase+0xb4cefe);
    Check(passedPacket!=&source,"audio recovers after native exception");
#if HALOMCCVR_DEV_DIAGNOSTICS
    // THE DEV SOUND LISTENER WITNESS samples inside this same detour. It must measure the game's own packet and
    // the handed one against the head, and change nothing that is handed on.
    {
        using namespace listener_witness;
        auto& w=listenerWitness;
        Reset();source=SourcePacket();const auto untouched=source;
        w.open=listener_witness::Window{};w.lastPacketMs=0;w.sampling=false;
        gameplay.unitsPerMeter=1.0f/3.048f;gameplay.positional=true;
        gameplay.tracking.headOrientation={0,std::sin(halfPi),0,std::cos(halfPi)};   // the head 90 degrees left
        AudioBody(0,&source,moduleBase+0xb4cefe);
        Check(passedPacket!=&source&&Near(heardPacket.forward,{0,1,0})&&!std::memcmp(&source,&untouched,sizeof(source)),
            "witness: the hook still hands on the head packet, and the game's packet is untouched");
        Check(w.open.taken==1&&w.open.handedHead==1&&w.open.handedStock==0&&!w.open.Refused()&&!w.sampling.load(),
            "witness: one packet taken, handed on by the hook, nothing refused, the flag released");
        Check(std::fabs(w.open[M::HeadForward].max-90.0f)<0.01f&&w.open[M::HandedHeadForward].max<0.001f&&
            w.open[M::HandedHeadUp].max<0.001f&&w.open[M::CameraForward].max<0.001f,
            "witness: the game's own listener is 90 degrees off the head and on the stock camera; the handed one is the head");
        // Not admitted (a seat, a cinematic): the game's packet goes on, and the witness finds the head itself.
        hasGameplay=false;witnessContext=true;
        AudioBody(0,&source,moduleBase+0xb4cefe);
        Check(passedPacket==&source&&w.open.taken==2&&w.open.handedStock==1&&
            std::fabs(w.open[M::HandedHeadForward].max-90.0f)<0.01f,
            "witness: a packet handed on as the game built it is measured 90 degrees off the head");
        // No gameplay context at all: counted, with no head.
        witnessContext=false;
        AudioBody(0,&source,moduleBase+0xb4cefe);
        Check(passedPacket==&source&&w.open.taken==3&&w.open.HeadMissing()==1,
            "witness: with no gameplay context the packet is counted with no head");
        // Another listener and another caller are not the producer's call for listener 0: not sampled.
        AudioBody(1,&source,moduleBase+0xb4cefe);AudioBody(0,&source,moduleBase+0xb4cefe+1);
        Check(w.open.taken==3&&!w.open.Refused(),"witness: only the producer's call for listener 0 is sampled");
        // An unreadable packet is a refusal, and the null still goes on to the native exactly as before.
        const auto calls=audioCalls;AudioBody(0,nullptr,moduleBase+0xb4cefe);
        Check(audioCalls==calls+1&&passedPacket==nullptr&&w.open.taken==3&&
            w.open.refused[static_cast<size_t>(Refusal::Unreadable)]==1,
            "witness: an unreadable packet is refused by name and forwarded unchanged");
        // A sample in progress elsewhere is never waited for.
        w.sampling=true;AudioBody(0,&source,moduleBase+0xb4cefe);
        Check(w.open.taken==3&&w.overlapped.load()==1&&passedPacket==&source,
            "witness: an overlapping call is counted and forwarded, never waited for");
        w.sampling=false;
        // A finished window crosses to the worker and is printed once.
        w.open.openedMs=GetTickCount64()-kWindowMs-1;witnessLines=0;
        AudioBody(0,&source,moduleBase+0xb4cefe);
        Check(w.open.taken==0&&w.published.serial==1&&w.published.taken==4&&!(w.version.load()&1u),
            "witness: a window two seconds old is published and a new one opened");
        ReportListenerWitness(GetTickCount64());ReportListenerWitness(GetTickCount64());
        Check(witnessLines==1&&w.reportedSerial==1,"witness: the worker prints a published window exactly once");
        witnessContext=false;
    }
#endif

    // Fault-injected platform operations exercise the actual management state
    // machine without patching any executable memory or launching the game.
    effect.ready=audio.ready=false;effect.original=audio.original=nullptr;
    const uintptr_t image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    enableFailure=true;
    Check(HaloCEOrientation_Poll(image,0x1000,7,true)&&audio.ready&&!effect.ready&&effect.target,
        "effect enable failure retains cleanup receipt and permits healthy audio");
    const auto creates=createCalls;removeFailure=true;
    Check(HaloCEOrientation_Poll(image,0x1000,7,true)&&effect.target&&audio.ready&&createCalls==creates,
        "failed optional cleanup never recreates a live trampoline or disarms sibling");
    removeFailure=false;
    Check(HaloCEOrientation_Poll(image,0x1000,7,true)&&!effect.target&&audio.ready&&createCalls==creates,
        "failed partial effect is cleaned and rejected identity is not retried");
    enableFailure=false;currentGeneration=8;
    Check(HaloCEOrientation_Poll(image,0x1000,8,true)&&active&&effect.ready&&audio.ready,
        "generation replacement restores active admission in same poll");
    disableFailure=true;
    Check(!HaloCEOrientation_Poll(0,0,0,false)&&retained&&!active&&retiring,
        "failed disable retains module/trampolines and closes late admission");
    disableFailure=false;quiescenceFailure=true;
    Check(!HaloCEOrientation_Poll(0,0,0,false)&&retained&&effect.target&&audio.target,
        "failed quiescence preserves both retained trampoline owners");
    quiescenceFailure=false;
    Check(!HaloCEOrientation_Poll(0,0,0,false)&&!retained&&!effect.target&&!audio.target,
        "next inactive poll completes retirement safely");
    bindingFailure=true;currentGeneration=9;
    Check(!HaloCEOrientation_Poll(image,0x1000,9,true)&&!effect.ready&&!audio.ready,
        "binding mismatch stays stock for both independent features");
    bindingFailure=false;const auto rejectedCreates=createCalls;
    Check(!HaloCEOrientation_Poll(image,0x1000,9,true)&&createCalls==rejectedCreates,
        "binding rejection is stable for exact module generation");
    currentGeneration=10;
    Check(HaloCEOrientation_Poll(image,0x1000,10,true)&&effect.ready&&audio.ready,
        "new generation retries both independently verified features");
    HaloCEOrientation_Poll(0,0,0,false);
    std::printf("CE orientation production checks: %u checks, %s\n",checks,failures?"FAIL":"PASS");
    return failures?1:0;
}
