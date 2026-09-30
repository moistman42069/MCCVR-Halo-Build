#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../src/common/runtime_types.h"
#include "../src/common/weapon_hand_logic.h"
static constexpr uint32_t owner=0x12340001,primary=0x23450002,secondary=0x34560003,effectTag=0x45670004;
static std::atomic<bool> g_armed{true},g_levelLive{true},g_teardownRequested{false};
static std::atomic<uint32_t> g_generation{7};
static struct {std::atomic<bool> enabled{true};uint32_t generation=7;} g_halo2Dual;
static struct {bool persistent_support_grip=true;} g_config;
struct SupportGripRelationshipSnapshot {bool engaged{};GameTitle title{};uint32_t generation{},unit{},weapon{};};
static bool dual{},support{},paused{},curveFault{},nativeFault{},sourceFault{},owned=true;
static uint32_t Halo2OwnedUnit(){return owner;}
static void* Halo2IndependentTargetStorage(uint32_t u){
    if(sourceFault)RaiseException(0xe0424747,0,0,nullptr);
    return owned&&u==owner?reinterpret_cast<void*>(1):nullptr;
}
static bool Halo2ReadIndependentWeapons(uint32_t u,uint32_t (&weapons)[2],bool){weapons[0]=primary;weapons[1]=dual?secondary:UINT32_MAX;return u==owner;}
static bool VR_IsTwoHandAiming(){return support;}
static bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot& out){out={support,GameTitle::Halo2,7,owner,primary};return support;}
static unsigned pulseCount[2]{},stockCount{},nativeUpdates{},nativeEvaluations{},checks{},failures{};
static float amplitude[2]{};static bool lastSupported{};
static uint64_t testToken=3;
static uint64_t VR_WeaponHapticToken(GameTitle,uint32_t,bool) noexcept{return testToken;}
static void Check(bool pass,const char* name){++checks;if(!pass){++failures;std::fprintf(stderr,"H2 haptic: %s\n",name);}}
static bool VR_PulseWeaponHaptics(GameTitle title,uint32_t generation,bool second,bool supported,float value,uint64_t token) noexcept {
    Check(token==testToken&&token!=0,"private voice carries its original live hand token");
    Check(title==GameTitle::Halo2&&generation==7&&std::isfinite(value)&&value>0&&value<=1,"bounded title-specific pulse");
    ++pulseCount[second?1:0];amplitude[second?1:0]=value;lastSupported=supported;return true;
}
#include "../src/dll/halo2_weapon_haptics.inl"
using namespace h2_recoil;
template<class T>static void Put(uintptr_t address,T value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}
static float __fastcall NativeCurve(const void* mapping,float fraction,float range) {
    if(curveFault)RaiseException(0xe0424545,0,0,nullptr);
    Check(mapping&&Read<uint32_t>(reinterpret_cast<uintptr_t>(mapping))==0x12345678&&range==1,
        "native curve receives copied mapping and own range ABI");return 1-fraction;
}
static void __fastcall NativeEnqueue(int32_t,uint32_t,int32_t,float){++stockCount;}
static void __fastcall NativeEffect(uint32_t,uint32_t tag){EnqueueBody(0,tag,0,.8f,base+proof::enqueueReturn);}
static void __fastcall NativeTrigger(uint32_t,int16_t,uint8_t){
    if(nativeFault)RaiseException(0xe0424646,0,0,nullptr);
    EffectBody(owner,effectTag,base+proof::effectReturn);
}
static void* __fastcall NativeEvaluate(void* out,const void*){++nativeEvaluations;std::memset(out,0x5a,12);return out;}
static void __fastcall NativeUpdate(float delta){
    ++nativeUpdates;const float period=Read<float>(base+proof::interval);
    if(period>0){const float total=Read<float>(base+proof::accumulator)+delta;Put(base+proof::accumulator,total);
        if(total<=period)return;delta=float(int(total/period))*period;Put(base+proof::accumulator,total-delta);}
    if(paused)return;
    uint8_t output[12]{};EvaluateBody(output,Read<void*>(base+proof::vibration),base+proof::evaluateReturn);
    Check(output[0]==0x5a&&output[11]==0x5a,"general native rumble output remains unchanged");
}
static bool CatchTrigger(){__try{Trigger(primary,0,0);}__except(GetExceptionCode()==0xe0424646?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
int main(){
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1700000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;base=reinterpret_cast<uintptr_t>(image);generation=7;enabled=true;
    const uintptr_t table=base+0x1000,tags=base+0x10000,block=base+0x20000;
    Put(base+proof::instances,table);Put(base+proof::tagData,tags);Put(table+4*16+8,int32_t(0x100));
    Put(tags+0x100+0x6c,int32_t(1));Put(tags+0x100+0x70,uint32_t(0x100));Put(base+proof::blockBase,block);
    Band bands[2]{{.1f,{0x12345678,0}},{.1f,{0x12345678,0}}};
    std::memcpy(reinterpret_cast<void*>(block+0x124),bands,sizeof(bands));
    Put(base+proof::durationScale,1.0f);Put(base+proof::amplitudeScale,1.0f);Put(base+proof::vibration,base+0x30000);
    uint8_t jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};const auto curve=reinterpret_cast<uintptr_t>(&NativeCurve);
    std::memcpy(jump+2,&curve,8);std::memcpy(image+proof::curve,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),image+proof::curve,sizeof(jump));
    triggerHook.original=reinterpret_cast<void*>(&NativeTrigger);effectHook.original=reinterpret_cast<void*>(&NativeEffect);
    enqueueHook.original=reinterpret_cast<void*>(&NativeEnqueue);updateHook.original=reinterpret_cast<void*>(&NativeUpdate);evaluateHook.original=reinterpret_cast<void*>(&NativeEvaluate);
    Trigger(primary,0,0);Update(.02f);
    Check(captured==1&&stockCount==0&&pulseCount[0]==1&&pulseCount[1]==0&&!lastSupported,"one-handed shot only primary; only authored enqueue diverted");
    Check(!source.valid&&firingWeapon==UINT32_MAX&&callbacks==0,"nested source and callbacks restored");
    support=true;Update(.02f);Check(lastSupported&&amplitude[0]<.8f,"engaged primary support and native elapsed decay");support=false;
    ClearVoices();dual=true;support=true;Trigger(primary,0,0);Update(.02f);
    Check(!lastSupported,"holding a secondary weapon prevents primary recoil coupling even without secondary firing");
    ClearVoices();support=false;Trigger(secondary,0,0);const auto previousPrimary=pulseCount[0];Update(.02f);
    Check(pulseCount[1]==1&&pulseCount[0]==previousPrimary&&!lastSupported,"secondary shot belongs only to secondary weapon");
    Trigger(primary,0,0);Update(.02f);Check(pulseCount[0]==previousPrimary+1&&pulseCount[1]==2,"independent concurrent dual envelopes");
    const auto stockBefore=stockCount;Trigger(0x99990002,0,0);
    EnqueueBody(0,effectTag,0,.8f,base+proof::enqueueReturn);
    Check(stockCount==stockBefore+2,"foreign full handle and unscoped effect preserve stock rumble");
    ClearVoices();dual=false;Trigger(primary,0,0);for(auto& v:voices)if(v.active)v.at-=101;
    const auto pulsesBefore=pulseCount[0];Update(.01f);Check(pulseCount[0]==pulsesBefore,"stale native voice retired");
    Trigger(primary,0,0);paused=true;Update(.01f);paused=false;
    Update(.01f);Check(pulseCount[0]==pulsesBefore,"paused native evaluator clears pending recoil");
    Put(base+proof::interval,.05f);Put(base+proof::accumulator,0.0f);Trigger(primary,0,0);
    Update(.02f);Check(pulseCount[0]==pulsesBefore,"throttled update does not fabricate evaluation");
    Update(.04f);Check(pulseCount[0]==pulsesBefore+1&&std::fabs(voices[0].age-.05f)<1e-6f,"native accumulator quantizes elapsed time once");
    Put(base+proof::interval,0.0f);
    const auto beforeCancel=pulseCount[0];testToken+=2;Update(.01f);
    Check(pulseCount[0]==beforeCancel,"XR hand token change retires saved native envelope");
    testToken=0;const auto beforeUnavailable=stockCount;Trigger(primary,0,0);
    Check(stockCount==beforeUnavailable+1,"unavailable XR hand leaves native enqueue stock");
    testToken=7;Trigger(primary,0,0);curveFault=true;Update(.01f);curveFault=false;
    Check(fault&&g_armed&&callbacks==0,"optional curve exception preserves camera and callback balance");
    const bool wasHeld=writer.test_and_set();writer.clear();Check(!wasHeld,"fault releases private writer");
    const auto stockFault=stockCount;Trigger(primary,0,0);Check(stockCount==stockFault+1,"faulted feature returns later shots to stock");
    nativeFault=true;Check(CatchTrigger()&&callbacks==0&&firingWeapon==UINT32_MAX,"original native exception propagates with TLS cleanup");
    nativeFault=false;fault=false;ClearVoices();const auto beforeSourceFault=stockCount;sourceFault=true;
    Trigger(primary,0,0);sourceFault=false;
    Check(fault&&stockCount==beforeSourceFault+1&&!source.valid&&callbacks==0,
        "optional owner read fault preserves original effect and restores scope");
    fault=false;Put(base+proof::instances,uintptr_t(1));const auto beforeReadFault=stockCount;
    Trigger(primary,0,0);
    Check(fault&&stockCount==beforeReadFault+1&&g_armed&&callbacks==0,
        "invalid native definition pointer falls back before suppressing enqueue");
    enabled=false;base=0;VirtualFree(image,0,MEM_RELEASE);
    std::printf("H2 production haptics: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
