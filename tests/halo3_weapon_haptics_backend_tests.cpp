#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

enum class GameTitle { Halo3 };
struct TestConfig { bool persistent_support_grip{}; };
TestConfig g_config{};
struct SupportGripRelationshipSnapshot { bool engaged{}; GameTitle title{}; uint32_t generation{}, unit{}, weapon{}; };
std::atomic<bool> g_vrAim{true};
std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
uint32_t engineTlsIndex = 1; uint32_t* g_engineTlsIndex = &engineTlsIndex;
int64_t (__fastcall* g_halo3PlayerUnitGetter)(int) = nullptr;
void* tagDataBase = nullptr; void** g_halo3TagDataBase = &tagDataBase;
void* g_halo3TagInstanceTable = nullptr;
uint8_t* Halo3LoadedTagDefinition(uint32_t);
bool Halo3ReadOwnedWeapons(uint32_t, uint32_t[2], bool);
int ResolveEquippedWeaponSlot(uint32_t, uint32_t, uint32_t, bool, bool);
uint64_t VR_WeaponHapticToken(GameTitle, uint32_t, bool) noexcept;
bool VR_PulseWeaponHaptics(GameTitle, uint32_t, bool, bool, float, uint64_t) noexcept;
bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot&) noexcept;
bool VR_IsTwoHandAiming() noexcept;
constexpr int MH_OK=0, MH_ERROR_DISABLED=1, MH_ERROR_NOT_CREATED=2;
bool disableHookFails = false;
int MCCVR_DisableHookForRetirement(void*) { return disableHookFails ? 99 : MH_OK; }
int MH_RemoveHook(void*) { return MH_OK; }
int MH_CreateHook(void*, void*, void**) { return MH_OK; }
int MH_EnableHook(void*) { return MH_OK; }
bool WaitForNativeDetourQuiescence(const void* const*,const void* const*,size_t,std::atomic<uint32_t>&){return true;}
#define LOG(...) ((void)0)
namespace sig { uintptr_t Find(uintptr_t,size_t,const char*) { return 0; } }

uint64_t tlsSlots[8]{};
uint64_t MockReadGs(unsigned long) { return reinterpret_cast<uint64_t>(tlsSlots); }
uintptr_t mockReturnAddress{};
uintptr_t MockReturnAddress() { return mockReturnAddress; }
#define __readgsqword MockReadGs
#define _ReturnAddress() reinterpret_cast<void*>(MockReturnAddress())
uint64_t clockMs = 1000;
uint64_t MockNow() { return clockMs; }
#define GetTickCount64 MockNow

#include "../src/dll/halo3_weapon_haptics.inl"

#undef __readgsqword
#undef GetTickCount64

uint8_t definition[8]{};
uint8_t tagBytes[0xC0 * 2 + 4]{};
uint8_t tlsBytes[0x2B8]{};
uint8_t queueBytes[0x98]{};
uint64_t currentToken = 77;
uint32_t livePrimary = 0x100, liveSecondary = UINT32_MAX;
bool readWeaponsFault = false, relationshipEnabled = false, legacyTwoHand = false, mutateOtherQueue = false;
bool shotEmitsOwnerEnvelope = false;
bool triggerEmitsChargeEffect = false, chargeEffectOwnerMatches = true;
uintptr_t triggerEffectReturn = 0;
int triggerOriginalCalls = 0, readWeaponsCalls=0;
int checkCount=0;
constexpr DWORD injectedException = 0xE0423301;
constexpr uint32_t firingDatum = 0xAABB1234u;
uint8_t* Halo3LoadedTagDefinition(uint32_t) { return definition; }
bool Halo3ReadOwnedWeapons(uint32_t, uint32_t out[2], bool) {
    ++readWeaponsCalls;
    if(readWeaponsFault) RaiseException(injectedException,0,0,nullptr);
    out[0]=livePrimary; out[1]=liveSecondary; return true;
}
int ResolveEquippedWeaponSlot(uint32_t weapon,uint32_t primary,uint32_t secondary,bool,bool) {
    return weapon==primary?0:(weapon==secondary?1:-1);
}
uint64_t VR_WeaponHapticToken(GameTitle,uint32_t,bool) noexcept { return currentToken; }
struct Pulse { bool secondary{},supported{};float amplitude{};uint64_t token{}; } pulses[8]{};int pulseCount=0;
bool VR_PulseWeaponHaptics(GameTitle,uint32_t,bool secondary,bool supported,float amplitude,uint64_t token) noexcept {
    if(pulseCount<8)pulses[pulseCount++]={secondary,supported,amplitude,token};return token==currentToken;
}
bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot& out) noexcept {
    out={relationshipEnabled,GameTitle::Halo3,7,0x200,livePrimary};return relationshipEnabled;
}
bool VR_IsTwoHandAiming() noexcept { return legacyTwoHand; }
void __fastcall NativeWorker(uint32_t,int32_t,uint32_t datum,uint64_t,uint64_t,uint32_t,float scale,uint8_t) {
    auto* queue=queueBytes;
    size_t selected=0;
    float maxAge=*reinterpret_cast<float*>(queue+0x70);
    for(size_t i=1;i<8;++i){float age=*reinterpret_cast<float*>(queue+0x70+i*4);if(age>maxAge){selected=i;maxAge=age;}}
    auto* record=queue+selected*0x0C;
    *reinterpret_cast<uint32_t*>(record)=datum;
    *reinterpret_cast<uint32_t*>(record+4)=1;
    *reinterpret_cast<float*>(record+8)=scale;
    *reinterpret_cast<float*>(queue+0x70+selected*4)=0.0f;
    if(mutateOtherQueue)*reinterpret_cast<uint32_t*>(queue+0x18)=0x654321u;
}
void __fastcall NativeShot(uint32_t weapon,int16_t,uint8_t) {
    ++triggerOriginalCalls;
    if (shotEmitsOwnerEnvelope) {
        const uintptr_t previous=mockReturnAddress;
        mockReturnAddress=g_halo3WeaponHaptics.base+
            halo3_haptic_contract::owner_damage_wrapper_return;
        Halo3WeaponHapticDamageWrapperDetour(0x200,firingDatum,0.75f);
        mockReturnAddress=previous;
    }
}
uint8_t __fastcall NativeTriggerUpdate(uint32_t weapon) {
    ++triggerOriginalCalls;
    if (triggerEmitsChargeEffect) {
        const uintptr_t previous=mockReturnAddress;
        mockReturnAddress=g_halo3WeaponHaptics.base+(triggerEffectReturn ? triggerEffectReturn :
            halo3_haptic_contract::charge_effect_return_initial);
        Halo3WeaponHapticEffectDetour(chargeEffectOwnerMatches?0x200:0x201,
            0,firingDatum,0,0,0,0,0);
        mockReturnAddress=previous;
    }
    return 1;
}
int64_t __fastcall PlayerUnit(int) { return 0x200; }
float __fastcall MockCurve(const float*,float,float endpoint) { return endpoint; }
void __fastcall NativePlayerEffect(uint32_t owner,int32_t user,uint32_t datum,
    uint64_t,uint64_t,uint32_t,uint32_t,uint8_t) {
    Halo3WeaponHapticWorkerBody(owner,user,datum,0,0,0,0.75f,0,
        g_halo3WeaponHaptics.base+halo3_haptic_contract::worker_call_local_list+5);
}
void __fastcall NativeDamageWrapper(uint32_t owner,uint32_t datum,float scale) {
    const uintptr_t previous=mockReturnAddress;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::wrapper_player_effect_return;
    Halo3WeaponHapticEffectDetour(owner,0,datum,0,0,0,0,0);
    mockReturnAddress=previous;
}
void __fastcall FaultingWorker(uint32_t,int32_t,uint32_t,uint64_t,uint64_t,uint32_t,float,uint8_t) {
    RaiseException(injectedException,0,0,nullptr);
}
bool CatchWorkerException(){bool caught=false;__try{
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,g_halo3WeaponHaptics.base+0x1B384F);
}__except(GetExceptionCode()==injectedException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}return caught;}

void Check(bool condition,const char* label){++checkCount;if(!condition){std::printf("FAILED: %s\n",label);std::fflush(stdout);std::abort();}}
void ResetFixture(){
    std::memset(queueBytes,0,sizeof(queueBytes));
    for(size_t i=0;i<8;++i)*reinterpret_cast<float*>(queueBytes+0x70+i*4)=static_cast<float>(i);
    *reinterpret_cast<uint32_t*>(queueBytes)=UINT32_MAX;
    *reinterpret_cast<float*>(queueBytes+0x70)=8.0f;
    std::memset(tlsBytes,0,sizeof(tlsBytes));
    *reinterpret_cast<uint8_t**>(tlsBytes+0x2B0)=queueBytes;
    tlsSlots[engineTlsIndex]=reinterpret_cast<uint64_t>(tlsBytes);
    std::memset(tagBytes,0,sizeof(tagBytes));
    *reinterpret_cast<int32_t*>(definition)=2;
    *reinterpret_cast<uint32_t*>(definition+4)=1;
    auto* row=tagBytes+4+0xC0;
    *reinterpret_cast<float*>(row+0x5C)=1.0f;
    *reinterpret_cast<float*>(row+0x60)=1.0f;
    *reinterpret_cast<uint32_t*>(row+0x64)=0xFFFFFFFFu; // Opaque native mapping sentinel.
    *reinterpret_cast<float*>(row+0x74)=0.5f;
    *reinterpret_cast<float*>(row+0x78)=1.0f;
    tagDataBase=tagBytes;
    auto& feature=g_halo3WeaponHaptics;
    feature.base=reinterpret_cast<uintptr_t>(&MockCurve)-halo3_haptic_contract::haptic_curve;
    feature.generation=7;feature.enabled=true;feature.faulted=false;
    feature.writer.clear();feature.workerOriginal=reinterpret_cast<void*>(&NativeWorker);
    feature.weaponOriginal=reinterpret_cast<void*>(&NativeShot);
    feature.triggerUpdateOriginal=reinterpret_cast<void*>(&NativeTriggerUpdate);
    feature.wrapperOriginal=reinterpret_cast<void*>(&NativeDamageWrapper);
    feature.playerEffectOriginal=reinterpret_cast<void*>(&NativePlayerEffect);
    feature.fallback=0;feature.captured=0;feature.retired=0;feature.faults=0;
    for(auto& voice:feature.voices)voice={};
    g_halo3RuntimeGeneration=7;g_halo3WeaponHapticSource={0x200,0x100,7,false,true};
    g_halo3WeaponHapticFireEffectScope=true;
    g_halo3WeaponHapticChargeEffectScope=false;
    g_halo3PlayerUnitGetter=PlayerUnit;currentToken=77;livePrimary=0x100;liveSecondary=UINT32_MAX;
    readWeaponsFault=relationshipEnabled=legacyTwoHand=mutateOtherQueue=shotEmitsOwnerEnvelope=false;
    triggerEmitsChargeEffect=false;chargeEffectOwnerMatches=true;g_config.persistent_support_grip=false;
    triggerEffectReturn=0;disableHookFails=false;
    feature.weaponTarget=feature.triggerUpdateTarget=feature.wrapperTarget=feature.playerEffectTarget=
        feature.workerTarget=feature.evaluatorTarget=feature.updateTarget=nullptr;
    triggerOriginalCalls=pulseCount=readWeaponsCalls=0;clockMs=1000;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::admitted_event_return;
    for(auto& pulse:pulses)pulse={};
}
int main(){
    ResetFixture();
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,
        g_halo3WeaponHaptics.base+0x1B384F);
    Check(g_halo3WeaponHaptics.captured==1&&g_halo3WeaponHaptics.retired==1,
        "production worker captures firing envelope and retires matching native slot");
    Check(*reinterpret_cast<uint32_t*>(queueBytes)==UINT32_MAX&&
        *reinterpret_cast<float*>(queueBytes+0x70)==0.0f,
        "retirement writes exact native NONE sentinel and preserves age");
    uint32_t mappingBits=0;
    std::memcpy(&mappingBits,&g_halo3WeaponHaptics.voices[0].authored[2],sizeof(mappingBits));
    Check(g_halo3WeaponHaptics.voices[0].active&&
        g_halo3WeaponHaptics.voices[0].weapon==0x100&&
        g_halo3WeaponHaptics.voices[0].sourceToken==77&&
        mappingBits==0xFFFFFFFFu,
        "private voice retains weapon role and source token");
    Halo3WeaponHapticsTick(0.1f);
    Check(pulseCount==1&&!pulses[0].secondary&&!pulses[0].supported&&
        std::fabs(pulses[0].amplitude-0.75f)<0.001f,"tick evaluates captured source and emits primary pulse");
    clockMs += 101;
    Halo3WeaponHapticsTick(0.1f);
    Check(pulseCount==1&&!g_halo3WeaponHaptics.voices[0].active,
        "voice missing native-update freshness window is retired");
    ResetFixture();legacyTwoHand=true;
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,g_halo3WeaponHaptics.base+0x1B384F);
    Halo3WeaponHapticsTick(0.1f);
    Check(pulseCount==1&&pulses[0].supported,"legacy two-hand aiming still routes coupled support pulse");
    ResetFixture();
    *reinterpret_cast<uint32_t*>(queueBytes)=0x1234;
    *reinterpret_cast<uint32_t*>(queueBytes+4)=1;
    *reinterpret_cast<float*>(tagBytes+4+0xC0+0x5C)=10.0f;
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,
        g_halo3WeaponHaptics.base+0x1B384F);
    Check(g_halo3WeaponHaptics.captured==1&&*reinterpret_cast<uint32_t*>(queueBytes)==UINT32_MAX,
        "native worker eviction is preserved while fresh recoil is retired to NONE");
    ResetFixture();
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,
        g_halo3WeaponHaptics.base+0x1B384F+1);
    Check(g_halo3WeaponHaptics.captured==0&&*reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "unverified worker return site falls back stock");
    ResetFixture();mutateOtherQueue=true;
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,
        g_halo3WeaponHaptics.base+0x1B384F);
    Check(g_halo3WeaponHaptics.captured==0&&*reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "unexpected concurrent/nonselected queue write falls back native");
    ResetFixture();
    for(size_t i=0;i<8;++i){auto& voice=g_halo3WeaponHaptics.voices[i];voice.active=true;
        voice.owner=0x200;voice.weapon=0x100;voice.generation=7;voice.slot=0;
        voice.sourceToken=77;voice.scale=1;voice.ageSeconds=static_cast<float>(i);
        voice.lastUpdateMs=1000;voice.authored[0]=20;voice.authored[6]=20;}
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,
        g_halo3WeaponHaptics.base+0x1B384F);
    Check(g_halo3WeaponHaptics.captured==1&&g_halo3WeaponHaptics.voices[7].ageSeconds==0,
        "full private bank replaces its greatest-age voice like native queue");
    ResetFixture();liveSecondary=0x101;relationshipEnabled=true;
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,g_halo3WeaponHaptics.base+0x1B384F);
    Halo3WeaponHapticsTick(0.1f);
    Check(pulseCount==1&&!pulses[0].supported,"secondary weapon prevents support-hand coupled pulse");
    ResetFixture();legacyTwoHand=true;g_config.persistent_support_grip=true;relationshipEnabled=false;
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,g_halo3WeaponHaptics.base+0x1B384F);
    Halo3WeaponHapticsTick(0.1f);
    Check(pulseCount==1&&!pulses[0].supported,
        "stale relationship cannot fall back to legacy support while persistent grip mode is enabled");
    ResetFixture();
    Halo3WeaponHapticWorkerBody(0x200,0,firingDatum,0,0,0,0.75f,0,g_halo3WeaponHaptics.base+0x1B384F);
    currentToken++;
    Halo3WeaponHapticsTick(0.1f);
    Check(pulseCount==0&&!g_halo3WeaponHaptics.voices[0].active,"source token loss retires stale voice");
    ResetFixture();
    shotEmitsOwnerEnvelope=true;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::admitted_event_return;
    Halo3WeaponHapticShotDetour(0x100,2,0);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==1&&
        *reinterpret_cast<uint32_t*>(queueBytes)==UINT32_MAX,
        "admitted network shot routes owner-authored recoil and retires only its native queue record");
    ResetFixture();shotEmitsOwnerEnvelope=true;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::admitted_trigger_return;
    Halo3WeaponHapticShotDetour(0x100,2,0);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==1,
        "admitted ordinary trigger shot routes the same owner-authored recoil");
    ResetFixture();shotEmitsOwnerEnvelope=true;
    mockReturnAddress=g_halo3WeaponHaptics.base+0x123456;
    Halo3WeaponHapticShotDetour(0x100,2,0);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==0&&
        *reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "unadmitted or dry-fire caller leaves authored effects on native path");
    ResetFixture();shotEmitsOwnerEnvelope=true;livePrimary=0x101;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::admitted_event_return;
    Halo3WeaponHapticShotDetour(0x100,2,0);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==0&&
        *reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "non-owned shot weapon retains native bilateral fallback");
    ResetFixture();triggerEmitsChargeEffect=true;liveSecondary=0x101;
    mockReturnAddress=g_halo3WeaponHaptics.base+0x123456;
    Halo3WeaponHapticTriggerUpdateDetour(0x101);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==1&&
        g_halo3WeaponHaptics.voices[0].slot==1,
        "local secondary-weapon charge effect routes through its own weapon role");
    ResetFixture();triggerEmitsChargeEffect=true;liveSecondary=0x101;
    triggerEffectReturn=halo3_haptic_contract::charge_effect_return_repeat;
    Halo3WeaponHapticTriggerUpdateDetour(0x101);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==1&&
        g_halo3WeaponHaptics.voices[0].slot==1,
        "repeated local charge response uses the same secondary weapon role");
    ResetFixture();triggerEmitsChargeEffect=true;chargeEffectOwnerMatches=false;
    Halo3WeaponHapticTriggerUpdateDetour(0x100);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==0&&
        *reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "charge effect for a different owner remains on native feedback path");
    ResetFixture();triggerEmitsChargeEffect=true;livePrimary=0x101;
    Halo3WeaponHapticTriggerUpdateDetour(0x100);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==0,
        "non-equipped charge source cannot be routed to a local controller");
    ResetFixture();triggerEmitsChargeEffect=true;
    triggerEffectReturn=0x123456;
    Halo3WeaponHapticTriggerUpdateDetour(0x100);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.captured==0&&
        *reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "unrelated player effect during trigger update retains native feedback");
    ResetFixture();
    g_halo3WeaponHapticFireEffectScope=false;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::owner_damage_wrapper_return;
    Halo3WeaponHapticDamageWrapperDetour(0x200,firingDatum,0.75f);
    Check(g_halo3WeaponHaptics.captured==1&&
        !g_halo3WeaponHapticFireEffectScope,
        "verified owner shot wrapper stages the authored haptic then restores scope");
    ResetFixture();
    g_halo3WeaponHapticFireEffectScope=false;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::passenger_damage_wrapper_return;
    Halo3WeaponHapticDamageWrapperDetour(0x200,firingDatum,0.75f);
    Check(g_halo3WeaponHaptics.captured==0&&
        *reinterpret_cast<uint32_t*>(queueBytes)==firingDatum,
        "passenger shot wrapper preserves native envelope instead of assigning it to owner hand");
    ResetFixture();
    g_halo3WeaponHapticFireEffectScope=false;
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::owner_damage_wrapper_return;
    Halo3WeaponHapticDamageWrapperDetour(0x201,firingDatum,0.75f);
    Check(g_halo3WeaponHaptics.captured==0,
        "mismatched effect owner cannot be captured as local weapon recoil");
    ResetFixture();
    mockReturnAddress=g_halo3WeaponHaptics.base+
        halo3_haptic_contract::admitted_trigger_return;
    Halo3WeaponHapticShotDetour(0x100,2,0);
    Check(triggerOriginalCalls==1,"ordinary trigger shot admission reaches stock shot routine");
    ResetFixture();
    g_halo3WeaponHapticSource={};
    mockReturnAddress=g_halo3WeaponHaptics.base+0x123456;
    Halo3WeaponHapticShotDetour(0x100,2,0);
    Check(triggerOriginalCalls==1&&!g_halo3WeaponHapticSource.valid,
        "unverified or dryfire caller does not open an authored recoil capture scope");
    ResetFixture();readWeaponsFault=true;
    Halo3WeaponHapticShotDetour(0x100,0,0);
    Check(triggerOriginalCalls==1&&g_halo3WeaponHaptics.faulted&&
        g_halo3WeaponHaptics.callbacks==0,"optional owner read fault still invokes stock shot routine");
    ResetFixture();g_halo3WeaponHaptics.workerOriginal=reinterpret_cast<void*>(&FaultingWorker);
    Check(CatchWorkerException()&&!g_halo3WeaponHaptics.faulted&&
        !g_halo3WeaponHaptics.writer.test_and_set(),"native worker exception propagates and optional writer is released");
    g_halo3WeaponHaptics.writer.clear();
    ResetFixture();g_halo3WeaponHaptics.weaponTarget=reinterpret_cast<void*>(1);
    g_halo3WeaponHaptics.faulted=true;
    disableHookFails=true;
    ReportHalo3WeaponHaptics();
    Check(g_halo3WeaponHaptics.weaponTarget&&!g_halo3WeaponHaptics.enabled.load(),
        "cleanup failure retains optional hook ownership for retry");
    disableHookFails=false;
    ReportHalo3WeaponHaptics();
    Check(!g_halo3WeaponHaptics.weaponTarget&&!g_halo3WeaponHaptics.enabled.load(),
        "faulted optional haptics hooks retire through the report retry path");
    std::printf("Halo 3 production haptics backend: %d checks passed\n",checkCount);
}
