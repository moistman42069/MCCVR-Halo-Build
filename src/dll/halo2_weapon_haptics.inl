// Optional, native-authored weapon recoil. General rumble stays engine-owned.
#include "../common/halo2_haptic_contract.h"
namespace h2_recoil {
namespace proof=halo2_haptic_contract;
using TriggerFn=void(__fastcall*)(uint32_t,int16_t,uint8_t);
using EffectFn=void(__fastcall*)(uint32_t,uint32_t);
using EnqueueFn=void(__fastcall*)(int32_t,uint32_t,int32_t,float);
using UpdateFn=void(__fastcall*)(float);
using EvaluateFn=void*(__fastcall*)(void*,const void*);
using CurveFn=float(__fastcall*)(const void*,float,float);
struct Hook {void* target{};void* original{};};
struct Band {float duration{};uint32_t mapping[2]{};};
static_assert(sizeof(Band)==12);
struct Voice {
    Band bands[2]{};float scale{},age{};uint64_t at{},token{};
    uint32_t unit=UINT32_MAX,weapon=UINT32_MAX,generation{};
    bool active{};int slot=-1;
};
struct Source {uint32_t unit=UINT32_MAX,weapon=UINT32_MAX,effect=UINT32_MAX;bool valid{};};
struct Tick {float delta{};bool valid{},evaluated{};};
uintptr_t base{};uint32_t generation{};
Hook triggerHook,effectHook,enqueueHook,updateHook,evaluateHook;
std::atomic<bool> enabled{},fault{};
std::atomic<uint32_t> callbacks{};
std::atomic<uint64_t> captured{},fallback{},faults{};
std::atomic_flag writer=ATOMIC_FLAG_INIT;
Voice voices[16]{}; // eight authored voices per weapon role
thread_local uint32_t firingWeapon=UINT32_MAX;
thread_local Source source;
thread_local Tick tick;
template<class T>T Read(uintptr_t address) {T value{};std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return value;}
bool Current() noexcept {
    return enabled.load()&&!fault.load()&&g_armed.load()&&g_levelLive.load()&&
        !g_teardownRequested.load()&&generation==g_generation.load()&&
        g_halo2Dual.enabled.load()&&g_halo2Dual.generation==generation;
}
bool Owned(uint32_t weapon,uint32_t& unit,int& slot) {
    if(!Current())return false;
    unit=Halo2OwnedUnit();uint32_t weapons[2]{};
    if(unit==UINT32_MAX||!Halo2IndependentTargetStorage(unit)||
        !Halo2ReadIndependentWeapons(unit,weapons,false))return false;
    slot=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    return slot>=0&&slot<2;
}
void ClearVoices() noexcept {
    if(writer.test_and_set(std::memory_order_acquire))return;
    for(auto& voice:voices)voice={};
    writer.clear(std::memory_order_release);
}
bool Capture(int32_t user,uint32_t effect,int32_t row,float scale,uintptr_t caller) {
    uint32_t unit{};int slot=-1;
    if(caller!=base+proof::enqueueReturn||user!=0||!source.valid||effect!=source.effect||
        !std::isfinite(scale)||scale<0||row<0||row>=256||
        !Owned(source.weapon,unit,slot)||unit!=source.unit)return false;
    const uint32_t index=effect&0xffff;
    const auto instances=Read<uintptr_t>(base+proof::instances);
    const auto tagBase=Read<uintptr_t>(base+proof::tagData);
    if(index==0xffff||!instances||!tagBase)return false;
    const int32_t offset=Read<int32_t>(instances+index*16+8);
    if(offset<=0)return false;
    const uintptr_t definition=tagBase+uint32_t(offset);
    const int32_t count=Read<int32_t>(definition+0x6c);
    const uint32_t address=Read<uint32_t>(definition+0x70);
    if(count<=0||count>256||row>=count||address==UINT32_MAX)return false;
    const auto storage=Read<uintptr_t>(base+(address&0x80000000u?proof::alternateBlockBase:proof::blockBase));
    if(!storage)return false;
    const auto data=storage+(address&0x7fffffffu)+size_t(row)*0x4c+0x24;
    Voice pending{};pending.scale=scale;pending.unit=unit;pending.weapon=source.weapon;
    pending.token=VR_WeaponHapticToken(GameTitle::Halo2,generation,slot==1);
    if(!pending.token)return false;
    pending.slot=slot;pending.generation=generation;pending.at=GetTickCount64();pending.active=true;
    std::memcpy(pending.bands,reinterpret_cast<const void*>(data),sizeof(pending.bands));
    for(const auto& band:pending.bands)
        if(!std::isfinite(band.duration)||band.duration<0)return false;
    if(writer.test_and_set(std::memory_order_acquire))return false;
    unsigned selected=unsigned(slot)*8;
    for(unsigned i=unsigned(slot)*8;i<unsigned(slot+1)*8;++i) {
        if(!voices[i].active){selected=i;break;}
        if(voices[i].age>voices[selected].age)selected=i;
    }
    voices[selected]=pending;
    writer.clear(std::memory_order_release);++captured;return true;
}
void EvaluateVoices() {
    if(writer.test_and_set(std::memory_order_acquire))return;
    __try {
        float motors[2][2]{};const auto now=GetTickCount64();
        const float durationScale=Read<float>(base+proof::durationScale);
        const float amplitudeScale=Read<float>(base+proof::amplitudeScale);
        if(!std::isfinite(durationScale)||durationScale<0||!std::isfinite(amplitudeScale)||amplitudeScale<0)
        {fault=true;++faults;__leave;}
        uint32_t roleWeapons[2]{UINT32_MAX,UINT32_MAX},roleUnit=UINT32_MAX;
        uint64_t roleTokens[2]{};
        for(auto& voice:voices) {
            if(!voice.active)continue;
            uint32_t unit{};int slot=-1;
            if(!tick.valid||!Owned(voice.weapon,unit,slot)||unit!=voice.unit||slot!=voice.slot||
                voice.generation!=generation||now<voice.at||now-voice.at>100||
                voice.token!=VR_WeaponHapticToken(GameTitle::Halo2,generation,slot==1)){voice={};continue;}
            voice.at=now;bool playing=false;
            for(unsigned band=0;band<2;++band) {
                const float duration=voice.bands[band].duration*durationScale;
                if(!std::isfinite(duration)){fault=true;++faults;__leave;}
                if(voice.age<duration) {
                    const float fraction=std::clamp(voice.age/duration,0.0f,1.0f);
                    const float value=reinterpret_cast<CurveFn>(base+proof::curve)(voice.bands[band].mapping,fraction,1.0f);
                    if(!std::isfinite(value)){fault=true;++faults;__leave;}
                    motors[slot][band]+=value*voice.scale;playing=true;
                }
            }
            roleWeapons[slot]=voice.weapon;roleUnit=unit;roleTokens[slot]=voice.token;
            voice.age+=tick.delta;if(!playing)voice={};
        }
        if(!Current())__leave;
        for(int slot=0;slot<2;++slot) {
            if(!std::isfinite(motors[slot][0])||!std::isfinite(motors[slot][1]))
            {fault=true;++faults;__leave;}
            const auto quantize=[&](float v){return uint16_t(std::clamp(v*amplitudeScale,0.0f,1.0f)*65535.0f+.5f);};
            const float value=quantize(motors[slot][0])*(.65f/65535)+quantize(motors[slot][1])*(.35f/65535);
            SupportGripRelationshipSnapshot support{};
            uint32_t equipped[2]{};
            const bool singlePrimary=slot==0&&roleUnit!=UINT32_MAX&&
                Halo2ReadIndependentWeapons(roleUnit,equipped,false)&&
                equipped[0]==roleWeapons[0]&&equipped[1]==UINT32_MAX;
            const bool supported=singlePrimary&&
                ((!g_config.persistent_support_grip&&VR_IsTwoHandAiming())||
                (VR_GetSupportGripRelationship(support)&&support.engaged&&support.title==GameTitle::Halo2&&
                 support.generation==generation&&support.unit==roleUnit&&support.weapon==roleWeapons[0]));
            if(value>0)(void)VR_PulseWeaponHaptics(GameTitle::Halo2,generation,slot==1,supported,value,roleTokens[slot]);
        }
    } __finally {writer.clear(std::memory_order_release);}
}
__declspec(noinline) void __fastcall Trigger(uint32_t weapon,int16_t barrel,uint8_t flag) {
    ++callbacks;const auto previous=firingWeapon;firingWeapon=weapon;
    __try {if(triggerHook.original)reinterpret_cast<TriggerFn>(triggerHook.original)(weapon,barrel,flag);}
    __finally {firingWeapon=previous;--callbacks;}
}
void EffectBody(uint32_t unit,uint32_t effect,uintptr_t caller) {
    const auto previous=source;source={};
    __try {
        __try {
            uint32_t owner{};int slot=-1;
            if(caller==base+proof::effectReturn&&Owned(firingWeapon,owner,slot)&&owner==unit)
                source={unit,firingWeapon,effect,true};
        } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;++faults;}
        if(effectHook.original)reinterpret_cast<EffectFn>(effectHook.original)(unit,effect);
    } __finally {source=previous;}
}
__declspec(noinline) void __fastcall Effect(uint32_t unit,uint32_t effect) {
    ++callbacks;__try {EffectBody(unit,effect,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {--callbacks;}
}
void EnqueueBody(int32_t user,uint32_t effect,int32_t row,float scale,uintptr_t caller) {
    bool routed=false;
    __try {routed=Capture(user,effect,row,scale,caller);}
    __except(EXCEPTION_EXECUTE_HANDLER){fault=true;++faults;}
    if(routed)return;
    if(source.valid)++fallback;
    if(enqueueHook.original)reinterpret_cast<EnqueueFn>(enqueueHook.original)(user,effect,row,scale);
}
__declspec(noinline) void __fastcall Enqueue(int32_t user,uint32_t effect,int32_t row,float scale) {
    ++callbacks;__try {EnqueueBody(user,effect,row,scale,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {--callbacks;}
}
void* EvaluateBody(void* output,const void* native,uintptr_t caller) {
    void* result=evaluateHook.original?reinterpret_cast<EvaluateFn>(evaluateHook.original)(output,native):output;
    __try {
        if(tick.valid&&caller==base+proof::evaluateReturn&&native==Read<const void*>(base+proof::vibration)) {
            tick.evaluated=true;EvaluateVoices();
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;++faults;}
    return result;
}
__declspec(noinline) void* __fastcall Evaluate(void* output,const void* native) {
    ++callbacks;void* result{};
    __try {result=EvaluateBody(output,native,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {--callbacks;}return result;
}
Tick PrepareTick(float delta) {
    Tick result{};
    if(!Current()||!std::isfinite(delta)||delta<0||delta>1)return result;
    const float period=Read<float>(base+proof::interval);
    if(!std::isfinite(period))return result;
    if(period>0) {
        const float total=Read<float>(base+proof::accumulator)+delta;
        if(!std::isfinite(total)||total<=period||double(total)/period>=double(INT32_MAX))return result;
        delta=float(int32_t(total/period))*period;
    }
    if(!std::isfinite(delta)||delta<0||delta>1)return result;
    result.delta=delta;result.valid=true;return result;
}
__declspec(noinline) void __fastcall Update(float delta) {
    ++callbacks;const auto previous=tick;tick={};
    __try {
        __try {tick=PrepareTick(delta);} __except(EXCEPTION_EXECUTE_HANDLER){fault=true;++faults;}
        if(updateHook.original)reinterpret_cast<UpdateFn>(updateHook.original)(delta);
        // A throttled update retains its voices; pause/title loss retires them.
        if(!Current()||(tick.valid&&!tick.evaluated))ClearVoices();
    } __finally {tick=previous;--callbacks;}
}
} // namespace h2_recoil
