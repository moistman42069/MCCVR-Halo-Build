#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
enum class GameTitle{Halo3ODST};
struct Pulse{uint32_t generation;bool secondary,supported;float amplitude;};
Pulse pulses[64]{};int pulseCount=0,checks=0,failures=0;
uint64_t token=10,nowMs=1000;
uint64_t VR_WeaponHapticToken(GameTitle,uint32_t,bool){return token;}
bool VR_PulseWeaponHaptics(GameTitle,uint32_t generation,bool secondary,bool supported,float amplitude,uint64_t sourceToken){
    if(sourceToken!=token)return false;if(pulseCount<64)pulses[pulseCount++]={generation,secondary,supported,amplitude};return true;}
uint64_t MockNow(){return nowMs;}
#define GetTickCount64 MockNow
#include "../src/dll/odst_weapon_haptics_backend.inl"
#undef GetTickCount64
using namespace odst_weapon_haptics;
constexpr DWORD testException=0xE0424142;
bool playable=true,sourceValid=true,supported=false,readFault=false,loadFault=false,queueFault=false;
bool nativeUpdateFault=false,nativeEffectFault=false,nativeWorkerFault=false,emit=true,passenger=false,nested=false,curveNan=false,curveFault=false;
bool mutateOther=false,paused=false,skipWrite=false,changeToken=false,changeOwner=false,mismatch=false;
uint32_t liveGeneration=7,primary=0x10001,secondary=0x10002;
int originalWorkers=0,originalUpdates=0,originalEffects=0,originalScopes=0,originalEvaluations=0,curveCalls=0;
constexpr uint32_t authoredTag=0x12340063;
uint32_t chosenRow=1,observedRow=UINT32_MAX;float duration[2]{1,1};
alignas(4) uint8_t nativeQueue[0x98]{};
namespace odst_weapon_haptics {
bool Current() noexcept{return playable&&runtime.enabled.load()&&!runtime.faulted.load()&&runtime.generation==liveGeneration;}
bool ReadSource(uint32_t weapon,Source& out){out={};if(readFault)RaiseException(testException,0,0,nullptr);
    if(!sourceValid||!Current()||(weapon!=primary&&weapon!=secondary)||weapon==UINT32_MAX)return false;
    out={0x20001,weapon,liveGeneration,uint8_t(weapon==secondary),true};return true;}
bool LoadBands(uint32_t tag,uint32_t row,uint8_t (&bands)[0x30]){
    if(loadFault)RaiseException(testException,0,0,nullptr);if(tag==UINT32_MAX||row>2)return false;
    observedRow=row;for(int i=0;i<2;++i)std::memcpy(bands+i*0x18,&duration[i],4);return true;}
bool ReadQueue(int32_t user,uint8_t*& queue){queue=nullptr;if(queueFault){queue=reinterpret_cast<uint8_t*>(uintptr_t(1));return true;}
    if(user!=0)return false;queue=nativeQueue;return true;}
bool Supported(const Source&){return supported&&secondary==UINT32_MAX;}
}
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAILED: %s\n",name);}}
void Put(size_t offset,uint32_t v){std::memcpy(nativeQueue+offset,&v,4);}
void PutFloat(size_t offset,float v){std::memcpy(nativeQueue+offset,&v,4);}
void __fastcall NativeWorker(uint32_t,int32_t,uint32_t tag,uint64_t,uint64_t,uint32_t,float scale,uint8_t){
    ++originalWorkers;if(nativeWorkerFault)RaiseException(testException,0,0,nullptr);
    if(skipWrite)return;unsigned selected=0;
    for(unsigned i=1;i<8;++i)if(ReadFloat(nativeQueue,0x70+i*4)>ReadFloat(nativeQueue,0x70+selected*4))selected=i;
    Put(selected*12,tag);Put(selected*12+4,chosenRow);PutFloat(selected*12+8,mismatch?scale*.5f:scale);PutFloat(0x70+selected*4,0);
    if(mutateOther)PutFloat(0x64,.25f);if(changeToken)++token;if(changeOwner)primary=0x10003;
}
uint64_t __fastcall NativeEvaluate(const void*){++originalEvaluations;return 0x12345678;}
void __fastcall NativeUpdate(float dt){++originalUpdates;if(nativeUpdateFault)RaiseException(testException,0,0,nullptr);
    if(!paused){const auto value=EvaluateHook(nativeQueue);Check(value==0x12345678,"native evaluation value preserved");
        for(unsigned i=0;i<8;++i)PutFloat(0x70+i*4,ReadFloat(nativeQueue,0x70+i*4)+dt);}}
float __fastcall Curve(const void*,float t,float scale){++curveCalls;Check(t>=0&&t<=1&&scale==1,"native curve normalized ABI");
    if(curveFault)RaiseException(testException,0,0,nullptr);return curveNan&&curveCalls==2?NAN:1-t;}
void __fastcall NativeEffect(uint32_t unit,uint32_t,float){++originalEffects;if(nativeEffectFault)RaiseException(testException,0,0,nullptr);
    if(nested&&unit==0x20001){nested=false;EffectBody(0x30001,1,1,runtime.base+0x3AD9BD);}
    if(emit)WorkerBody(unit,0,authoredTag,0,0,0,1,0,runtime.base+0x1E3CDB);}
void __fastcall NativeScope(uint32_t,int16_t,uint8_t){++originalScopes;
    if(emit)EffectBody(passenger?0x30001:0x20001,1,1,runtime.base+(passenger?0x3AD9BD:0x3AD8F2));}
void Reset(){runtime.enabled=true;runtime.faulted=false;runtime.callbacks=0;runtime.base=0x100000;runtime.generation=7;
    runtime.writer.clear();runtime.captured=0;runtime.fallback=0;runtime.faults=0;for(auto& v:runtime.voices)v={};scopeSource={};effectSource={};sampling=sampled=false;
    runtime.scopeOriginal=NativeScope;runtime.effectOriginal=NativeEffect;runtime.workerOriginal=NativeWorker;runtime.updateOriginal=NativeUpdate;runtime.evaluateOriginal=NativeEvaluate;runtime.curve=Curve;
    pulseCount=originalWorkers=originalUpdates=originalEffects=originalScopes=originalEvaluations=curveCalls=0;
    playable=sourceValid=emit=true;supported=readFault=loadFault=queueFault=nativeUpdateFault=nativeEffectFault=nativeWorkerFault=passenger=nested=curveNan=curveFault=false;
    mutateOther=paused=skipWrite=changeToken=changeOwner=mismatch=false;chosenRow=1;observedRow=UINT32_MAX;
    token=10;liveGeneration=7;primary=0x10001;secondary=0x10002;nowMs=1000;duration[0]=duration[1]=1;
    std::memset(nativeQueue,0,sizeof(nativeQueue));for(unsigned i=0;i<8;++i){Put(i*12,UINT32_MAX);Put(i*12+4,UINT32_MAX);PutFloat(i*12+8,1);}}
unsigned Active(){unsigned n=0;for(auto& v:runtime.voices)n+=v.active?1:0;return n;}
void Shot(bool offhand=false){ScopeHook(offhand?secondary:primary,0,0);}
void Tick(float dt=.1f){nowMs+=10;UpdateHook(dt);}
bool CatchShot(){bool caught=false;__try{Shot();}__except(GetExceptionCode()==testException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}return caught;}
bool CatchUpdate(){bool caught=false;__try{UpdateHook(.1f);}__except(GetExceptionCode()==testException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}return caught;}
int main(){
    Reset();uint8_t before[sizeof(nativeQueue)];std::memcpy(before,nativeQueue,sizeof(before));Shot();
    Check(runtime.captured==1&&originalWorkers==1&&originalEffects==1&&originalScopes==1,"owned native worker executes exactly once");
    Check(!std::memcmp(before,nativeQueue,sizeof(before))&&observedRow==chosenRow,"exact native selected row copied; prior queue bytes restored");
    Check(!scopeSource.valid&&!effectSource.valid&&runtime.callbacks==0,"source scopes/callback accounting restored");Tick();
    Check(pulseCount==1&&!pulses[0].secondary&&std::abs(pulses[0].amplitude-1)<.0001f,"native evaluation-before-aging primary pulse");
    Tick();Check(pulseCount==2&&std::abs(pulses[1].amplitude-.9f)<.0001f,"authored envelope advances by native dt");
    Reset();supported=true;secondary=UINT32_MAX;Shot();Tick();Check(pulseCount==1&&pulses[0].supported,"sole primary live support coupling");
    Reset();supported=true;Shot();Shot(true);Tick();Check(pulseCount==2&&!pulses[0].secondary&&!pulses[0].supported&&pulses[1].secondary&&!pulses[1].supported,"both equipped roles independent; no dual support coupling");
    Reset();emit=false;Shot();Tick();Check(pulseCount==0&&runtime.captured==0,"no admitted response means no fabricated dry-fire pulse");
    Reset();passenger=true;Shot();Check(originalWorkers==1&&runtime.captured==0&&ReadU32(nativeQueue,0)==authoredTag,"passenger keeps native rumble");
    Reset();EffectBody(0x20001,1,1,runtime.base+0x3AD8F2);Check(runtime.captured==0&&ReadU32(nativeQueue,0)==authoredTag,"generic effect outside fire scope stays stock");
    Reset();nested=true;Shot();Check(originalWorkers==2&&originalEffects==2&&runtime.captured==0,"nested passenger cannot consume owner scope; indistinguishable same-tuple outer write stays stock");
    for(int kind=0;kind<6;++kind){Reset();ReadSource(primary,effectSource);int user=0;uint32_t tag=authoredTag;float scale=1;uintptr_t caller=runtime.base+0x1E3CDB;uint32_t unit=0x20001;
        if(kind==0)user=1;if(kind==1)tag=UINT32_MAX;if(kind==2)scale=NAN;if(kind==3)++caller;if(kind==4)unit=0x30001;if(kind==5)token=0;
        WorkerBody(unit,user,tag,0,0,0,scale,0,caller);Check(originalWorkers==1&&runtime.captured==0,"invalid provenance preserves native worker");}
    Reset();runtime.writer.test_and_set();Shot();Check(originalWorkers==1&&runtime.captured==0,"capture contention nonblocking stock fallback");runtime.writer.clear();
    Reset();Put(0,7);Put(4,0);PutFloat(0x70,.5f);Shot();Check(runtime.captured==1&&ReadU32(nativeQueue,0)==UINT32_MAX&&ReadU32(nativeQueue,4)==UINT32_MAX&&ReadFloat(nativeQueue,8)==1&&ReadFloat(nativeQueue,0x70)==0,"native eviction preserved for overwritten active damage; new recoil becomes private");
    Reset();for(unsigned i=0;i<8;++i){Put(i*12,7);Put(i*12+4,0);PutFloat(0x70+i*4,.5f);}Shot();
    Check(runtime.captured==1&&ReadU32(nativeQueue,0)==UINT32_MAX,"full active native bank retires only newly copied recoil");
    for(unsigned i=1;i<8;++i)Check(ReadU32(nativeQueue,i*12)==7&&ReadFloat(nativeQueue,0x70+i*4)==.5f,"other active native damage voices remain exact");
    Reset();for(unsigned i=0;i<8;++i){Put(i*12,7);Put(i*12+4,0);PutFloat(0x70+i*4,2);}
    std::memcpy(before,nativeQueue,sizeof(before));Shot();Check(runtime.captured==1&&!std::memcmp(before,nativeQueue,sizeof(before)),"full expired native bank allows recoil while preserving exact expired tuple");
    Reset();PutFloat(0x70+3*4,3);Shot();Check(runtime.captured==1&&ReadU32(nativeQueue,3*12)==UINT32_MAX&&ReadFloat(nativeQueue,0x70+3*4)==3,"oldest strict comparison selects own native slot");
    for(int kind=0;kind<5;++kind){Reset();if(kind==0)mutateOther=true;if(kind==1)skipWrite=true;if(kind==2)changeToken=true;if(kind==3)changeOwner=true;if(kind==4)mismatch=true;
        Shot();Check(runtime.captured==0&&Active()==0,"post-call mismatch/cancellation cannot erase native writes");}
    Reset();Put(0,authoredTag);Put(4,chosenRow);skipWrite=true;Shot();
    Check(runtime.captured==0&&ReadU32(nativeQueue,0)==authoredTag,"unchanged matching age-zero old record cannot prove a new native enqueue");
    Reset();loadFault=true;Shot();Check(runtime.faulted&&originalWorkers==1&&ReadU32(nativeQueue,0)==authoredTag&&!runtime.writer.test_and_set(),"optional authored read fault retains native write and releases writer");runtime.writer.clear();
    Reset();readFault=true;Shot();Check(runtime.faulted&&originalWorkers==1&&runtime.callbacks==0,"optional source fault published, original retained");
    Reset();queueFault=true;Shot();Check(runtime.faulted&&originalWorkers==1&&runtime.callbacks==0,"actual invalid queue pointer contained, native worker retained");
    for(float bad:{-1.f,NAN,61.f}){Reset();duration[0]=bad;Shot();Check(runtime.captured==0&&ReadU32(nativeQueue,0)==authoredTag,"invalid authored duration leaves native queue intact");}
    Reset();Shot();curveNan=true;Tick();Check(runtime.faulted&&pulseCount==0,"invalid second band cannot leak first-band pulse");
    Reset();Shot();curveFault=true;Tick();Check(runtime.faulted&&pulseCount==0&&!runtime.writer.test_and_set(),"optional curve exception contained and writer released");runtime.writer.clear();
    Reset();nativeWorkerFault=true;Check(CatchShot()&&runtime.callbacks==0&&!scopeSource.valid&&!effectSource.valid&&!runtime.faulted&&!runtime.writer.test_and_set(),"native worker exception propagates; all scopes and writer restored");runtime.writer.clear();
    Reset();nativeEffectFault=true;Check(CatchShot()&&runtime.callbacks==0&&!scopeSource.valid&&!effectSource.valid&&!runtime.faulted,"native effect exception propagates");
    Reset();nativeUpdateFault=true;Check(CatchUpdate()&&runtime.callbacks==0&&!sampling&&!sampled&&!runtime.faulted,"native update exception propagates and sampling restored");
    for(int kind=0;kind<9;++kind){Reset();Shot();if(kind==0)playable=false;if(kind==1)sourceValid=false;if(kind==2)primary=0x10003;if(kind==3)++liveGeneration;if(kind==4)nowMs+=150;if(kind==5)nowMs=1;if(kind==6)runtime.enabled=false;if(kind==7)++token;if(kind==8)paused=true;
        Tick();Check(pulseCount==0&&Active()==0,"menu/owner/role/generation/clock/teardown/XR/pause cancels private tail");}
    for(float dt:{0.f,-1.f,NAN,.3f}){Reset();Shot();Tick(dt);Check(pulseCount==0&&Active()==0,"invalid native delta cancels private voice");}
    Reset();duration[0]=duration[1]=.1f;Shot();Tick();Tick();Check(pulseCount==1&&Active()==0,"native initial sample then exact expiry");
    Reset();duration[0]=0;Shot();Tick();Check(pulseCount==1&&std::abs(pulses[0].amplitude-.35f)<.0001f,"native high-band weight preserved");
    Reset();for(int i=0;i<9;++i)Shot();Check(runtime.captured==9&&Active()==8&&ReadU32(nativeQueue,0)==UINT32_MAX,"ninth overlapping cue replaces private oldest without bilateral native leak");
    Tick();Check(pulseCount==1&&pulses[0].amplitude<=1,"overlapping authored bands sum before clamp");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
