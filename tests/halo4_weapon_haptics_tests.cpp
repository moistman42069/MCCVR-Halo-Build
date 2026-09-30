#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
enum class GameTitle{Halo4};
struct Pulse{uint32_t generation;bool secondary,supported;float amplitude;};
Pulse pulses[64]{};int pulseCount=0,checks=0,failures=0;
uint64_t token=10;
uint64_t VR_WeaponHapticToken(GameTitle,uint32_t,bool){return token;}
bool VR_PulseWeaponHaptics(GameTitle,uint32_t generation,bool secondary,bool supported,float amplitude,uint64_t sourceToken){
    if(sourceToken!=token)return false;
    if(pulseCount<64)pulses[pulseCount++]={generation,secondary,supported,amplitude};return true;
}
uint64_t nowMs=1000;
uint64_t MockNow(){return nowMs;}
#define GetTickCount64 MockNow
#include "../src/dll/halo4_weapon_haptics_backend.inl"
#undef GetTickCount64
using namespace halo4_weapon_haptics;
bool playable=true,sourceValid=true,supported=false,readFault=false,loadFault=false,updateFault=false;
bool nativeUpdateFault=false,nativeEffectFault=false,emit=true,passenger=false,nested=false,curveNan=false,curveFault=false;
uint32_t liveGeneration=7,primary=0x10001,secondary=0x10002;
bool paused=false,foreignEvaluation=false,evaluationFault=false,queueFault=false;
int originalEvaluations=0;uint8_t nativeQueue[8]{};
int originalEnqueues=0,originalUpdates=0,originalEffects=0,originalScopes=0,sourceUpdates=0,curveInputs=0,curveOutputs=0;
float duration[2]{1,1},attenuation=1;alignas(4) uint8_t sourceDefinition[0x2C]{};
constexpr DWORD testException=0xE0424142;
namespace halo4_weapon_haptics {
bool Current() noexcept{return playable&&runtime.enabled.load()&&!runtime.faulted.load()&&runtime.generation==liveGeneration;}
bool ReadSource(uint32_t weapon,Source& out){
    out={};if(readFault)RaiseException(testException,0,0,nullptr);
    if(!sourceValid||!Current()||(weapon!=primary&&weapon!=secondary))return false;
    out={0x20001,weapon,liveGeneration,uint8_t(weapon==secondary),true};return true;
}
bool LoadBands(uint32_t,uint8_t (&bands)[0x30]){
    if(loadFault)RaiseException(testException,0,0,nullptr);
    for(int i=0;i<2;++i)std::memcpy(bands+i*0x18,&duration[i],4);return true;
}
bool Supported(const Source&){return supported&&secondary==UINT32_MAX;}
bool ReadQueue(const void*& out){if(queueFault)RaiseException(testException,0,0,nullptr);out=nativeQueue;return true;}
}
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAILED: %s\n",name);}}
void __fastcall NativeEnqueue(int32_t,uint32_t,float,const void*){++originalEnqueues;}
uint64_t __fastcall NativeEvaluate(const void*){++originalEvaluations;if(evaluationFault)RaiseException(testException,0,0,nullptr);return 0x12345678;}
void __fastcall NativeUpdate(float){++originalUpdates;if(nativeUpdateFault)RaiseException(testException,0,0,nullptr);
    if(!paused){const auto value=EvaluateHook(nativeQueue+(foreignEvaluation?1:0));Check(value==0x12345678,"native evaluation return preserved");}}
void __fastcall SourceUpdate(void* definition){++sourceUpdates;if(updateFault)RaiseException(testException,0,0,nullptr);std::memcpy(static_cast<uint8_t*>(definition)+0x28,&attenuation,4);}
float __fastcall CurveInput(const void*,float t,float scale){++curveInputs;Check(t>=0&&t<=1&&scale==1,"native normalized authored input");return t;}
float __fastcall CurveOutput(const void*,float t){++curveOutputs;if(curveFault)RaiseException(testException,0,0,nullptr);return curveNan&&curveOutputs==2?NAN:1-t;}
void __fastcall NativeEffect(uint32_t unit,uint32_t,float){
    ++originalEffects;if(nativeEffectFault)RaiseException(testException,0,0,nullptr);
    if(nested&&unit==0x20001){nested=false;EffectBody(0x30001,1,1,runtime.base+0x616329);}
    if(emit)EnqueueBody(0,99,1,sourceDefinition,runtime.base+0x1E69F5);
}
void __fastcall NativeScope(uint32_t,int32_t,const void*){
    ++originalScopes;if(emit)EffectBody(passenger?0x30001:0x20001,1,1,runtime.base+(passenger?0x616329:0x616237));
}
void Reset(){
    runtime.enabled=true;runtime.faulted=false;runtime.callbacks=0;runtime.base=0x100000;runtime.generation=7;
    runtime.writer.clear();runtime.captured=0;runtime.fallback=0;runtime.faults=0;
    for(auto& v:runtime.voices)v={};scopeSource={};effectSource={};sampling=sampled=false;
    runtime.scopeOriginal=NativeScope;runtime.effectOriginal=NativeEffect;runtime.enqueueOriginal=NativeEnqueue;runtime.updateOriginal=NativeUpdate;runtime.evaluateOriginal=NativeEvaluate;
    runtime.sourceUpdate=SourceUpdate;runtime.curveInput=CurveInput;runtime.curveOutput=CurveOutput;
    pulseCount=originalEnqueues=originalUpdates=originalEffects=originalScopes=sourceUpdates=curveInputs=curveOutputs=0;
    playable=sourceValid=emit=true;supported=readFault=loadFault=updateFault=nativeUpdateFault=nativeEffectFault=passenger=nested=curveNan=curveFault=false;
    paused=foreignEvaluation=evaluationFault=queueFault=false;originalEvaluations=0;
    token=10;liveGeneration=7;primary=0x10001;secondary=0x10002;nowMs=1000;duration[0]=duration[1]=1;attenuation=1;
    std::memset(sourceDefinition,0,sizeof(sourceDefinition));std::memcpy(sourceDefinition+0x28,&attenuation,4);
}
unsigned Active(){unsigned count=0;for(auto& v:runtime.voices)count+=v.active?1:0;return count;}
void Shot(bool offhand=false){ScopeHook(offhand?secondary:primary,0,nullptr);}
void Tick(float dt=.1f){nowMs+=10;UpdateHook(dt);}
bool CatchNativeUpdate(){bool caught=false;__try{UpdateHook(.1f);}__except(GetExceptionCode()==testException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}return caught;}
bool CatchNativeEffect(){bool caught=false;__try{Shot();}__except(GetExceptionCode()==testException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}return caught;}
int main(){
    Reset();Shot();Check(runtime.captured==1&&originalEnqueues==0&&originalScopes==1&&originalEffects==1,"owned firing response diverts only rumble, preserves effect once");
    Check(!scopeSource.valid&&!effectSource.valid&&runtime.callbacks==0,"source scope and callback count restored");Tick();
    Check(originalUpdates==1&&sourceUpdates==1&&curveInputs==2&&curveOutputs==2,"native update and both authored band evaluations");
    Check(pulseCount==1&&!pulses[0].secondary&&!pulses[0].supported&&std::abs(pulses[0].amplitude-.9f)<.0001f,"primary authored envelope");
    Reset();supported=true;secondary=UINT32_MAX;Shot();Tick();Check(pulseCount==1&&pulses[0].supported,"primary support coupling without secondary weapon");
    Reset();supported=true;Shot();Shot(true);Tick();Check(pulseCount==2&&!pulses[0].secondary&&!pulses[0].supported&&pulses[1].secondary&&!pulses[1].supported,"independent dual roles cannot add support coupling");
    Reset();emit=false;Shot();Tick();Check(pulseCount==0&&originalEffects==0&&runtime.captured==0,"no native effect means no fabricated dry-fire recoil");
    Reset();passenger=true;Shot();Check(originalEnqueues==1&&runtime.captured==0,"passenger response stays stock");
    Reset();EffectBody(0x20001,1,1,runtime.base+0x616237);Check(originalEnqueues==1,"generic effect outside firing scope stays stock");
    Reset();nested=true;Shot();Check(originalEnqueues==1&&runtime.captured==1&&originalEffects==2,"nested passenger cannot consume firing scope; outer source restored");
    Reset();Source source{};ReadSource(primary,source);scopeSource=source;EffectBody(source.unit,1,1,runtime.base+0x616329);Check(originalEnqueues==1,"exact firing-owner call site required");
    for(int kind=0;kind<6;++kind){Reset();ReadSource(primary,effectSource);
        int user=0;uint32_t tag=99;float scale=1;const void* def=sourceDefinition;uintptr_t caller=runtime.base+0x1E69F5;
        if(kind==0)user=1;if(kind==1)tag=UINT32_MAX;if(kind==2)scale=NAN;if(kind==3)def=nullptr;if(kind==4)++caller;if(kind==5){int other=1;std::memcpy(sourceDefinition+4,&other,4);}
        EnqueueBody(user,tag,scale,def,caller);Check(originalEnqueues==1&&runtime.captured==0,"invalid provenance/input preserves native enqueue");}
    Reset();runtime.writer.test_and_set();Shot();Check(originalEnqueues==1&&runtime.captured==0,"nonblocking capture contention falls back stock");runtime.writer.clear();
    Reset();loadFault=true;Shot();Check(runtime.faulted,"capture read fault published");Check(originalEnqueues==1&&runtime.callbacks==0,"capture read fault retains original and drains callback");
    Reset();readFault=true;Shot();Check(runtime.faulted,"source read fault published");Check(originalEnqueues==1&&runtime.callbacks==0,"source fault stays stock without swallowing original");
    Reset();Shot();updateFault=true;Tick();Check(runtime.faulted,"private source update fault published");Check(pulseCount==0&&originalUpdates==1&&!runtime.writer.test_and_set(),"private source update fault isolates haptics and releases writer");runtime.writer.clear();
    Reset();ReadSource(primary,effectSource);EnqueueBody(0,99,1,reinterpret_cast<const void*>(uintptr_t(1)),runtime.base+0x1E69F5);
    Check(runtime.faulted&&originalEnqueues==1&&runtime.captured==0,"actual invalid native source pointer is guarded and falls back");
    Reset();curveNan=true;Shot();Tick();Check(runtime.faulted&&pulseCount==0,"invalid second authored band cannot leak partial first-band pulse");
    Reset();curveFault=true;Shot();Tick();Check(runtime.faulted&&pulseCount==0&&originalUpdates==1&&!runtime.writer.test_and_set(),"authored curve fault isolates feature and releases writer");runtime.writer.clear();
    Reset();Shot();attenuation=NAN;Tick();Check(runtime.faulted&&pulseCount==0,"invalid native attenuation is reported, no fabricated pulse");
    for(float bad:{-1.f,NAN,61.f}){Reset();duration[0]=bad;Shot();Check(originalEnqueues==1&&runtime.captured==0,"invalid or unbounded definition retains stock native envelope");}
    Reset();nativeUpdateFault=true;Check(CatchNativeUpdate()&&runtime.callbacks==0&&!runtime.faulted,"native update exception propagates");
    Reset();nativeEffectFault=true;Check(CatchNativeEffect()&&runtime.callbacks==0&&!scopeSource.valid&&!effectSource.valid&&!runtime.faulted,"native effect exception propagates and restores nested scopes");
    for(int kind=0;kind<7;++kind){Reset();Shot();if(kind==0)playable=false;if(kind==1)sourceValid=false;if(kind==2)primary=0x10003;if(kind==3)++liveGeneration;if(kind==4)nowMs+=150;if(kind==5)nowMs=1;if(kind==6)runtime.enabled=false;
        Tick();Check(pulseCount==0&&Active()==0,"menu/owner/weapon/generation/stale/clock/teardown retires private voice");}
    for(float dt:{0.f,-1.f,NAN,.3f}){Reset();Shot();Tick(dt);Check(pulseCount==0&&Active()==0,"invalid/paused native delta cancels voice");}
    Reset();Shot();++token;Tick();Check(pulseCount==0&&Active()==0,"XR cancellation epoch retires authored tail even after rapid resume");
    Reset();Shot();paused=true;Tick();Check(pulseCount==0&&Active()==0&&originalEvaluations==0,"native skipped evaluation cancels private recoil tail");
    Reset();Shot();foreignEvaluation=true;Tick();Check(pulseCount==0&&Active()==0,"another output user evaluation cannot advance local private recoil");
    Reset();Shot();queueFault=true;Tick();Check(runtime.faulted&&pulseCount==0&&runtime.callbacks==0,"optional evaluator ownership read fault isolates feature");
    Reset();evaluationFault=true;Check(CatchNativeUpdate()&&!runtime.faulted&&runtime.callbacks==0&&!sampling&&!sampled,"native evaluation exception propagates and restores sampling scope");
    Reset();token=0;Shot();Check(runtime.captured==0&&originalEnqueues==1,"unavailable XR token preserves native fallback");
    Reset();duration[0]=duration[1]=.1f;Shot();Tick();Check(pulseCount==0&&Active()==0,"authored duration ends without an extra frame");
    Reset();attenuation=.25f;Shot();Tick();Check(pulseCount==1&&std::abs(pulses[0].amplitude-.225f)<.0001f,"native source attenuation retained");
    Reset();duration[0]=0;Shot();Tick();Check(pulseCount==1&&std::abs(pulses[0].amplitude-.315f)<.0001f,"native high-frequency motor weighting retained");
    Reset();for(int i=0;i<12;++i)Shot();Check(Active()==8&&runtime.captured==12,"bounded private voice bank");Tick();Check(pulseCount==1&&pulses[0].amplitude<=1,"overlapping authored feedback clamps");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
