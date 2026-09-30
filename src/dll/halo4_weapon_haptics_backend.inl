// H4EK-authored recoil only. The native player-effects function runs once;
// only its exact rumble enqueue is diverted to these private voices.
namespace halo4_weapon_haptics {
using ScopeFn=void(__fastcall*)(uint32_t,int32_t,const void*);
using EffectFn=void(__fastcall*)(uint32_t,uint32_t,float);
using EnqueueFn=void(__fastcall*)(int32_t,uint32_t,float,const void*);
using UpdateFn=void(__fastcall*)(float);
using EvaluateFn=uint64_t(__fastcall*)(const void*);
using SourceUpdateFn=void(__fastcall*)(void*);
using CurveInputFn=float(__fastcall*)(const void*,float,float);
using CurveOutputFn=float(__fastcall*)(const void*,float);
struct Source {
    uint32_t unit=UINT32_MAX,weapon=UINT32_MAX,generation=0;
    uint8_t slot=0;bool valid=false;
};
struct Voice {
    Source owner{};alignas(4) uint8_t definition[0x2C]{},bands[0x30]{};
    float scale=0,age=0;uint64_t atMs=0,token=0;bool active=false;
};
struct Runtime {
    uintptr_t base=0;uint32_t generation=0;void* targets[5]{};
    ScopeFn scopeOriginal=nullptr;EffectFn effectOriginal=nullptr;
    EnqueueFn enqueueOriginal=nullptr;UpdateFn updateOriginal=nullptr;EvaluateFn evaluateOriginal=nullptr;
    SourceUpdateFn sourceUpdate=nullptr;CurveInputFn curveInput=nullptr;
    CurveOutputFn curveOutput=nullptr;
    std::atomic<bool> enabled{false},faulted{false};std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> captured{0},fallback{0},faults{0};
    std::atomic_flag writer=ATOMIC_FLAG_INIT;Voice voices[8]{};
} runtime;
thread_local Source scopeSource{},effectSource{};
thread_local bool sampling=false,sampled=false;
bool Current() noexcept;
bool ReadSource(uint32_t weapon,Source& result);
bool LoadBands(uint32_t tag,uint8_t (&bands)[0x30]);
bool Supported(const Source& source);
bool ReadQueue(const void*& queue);
float ReadFloat(const void* data,size_t offset) {float out;std::memcpy(&out,static_cast<const uint8_t*>(data)+offset,4);return out;}
bool SameSource(const Source& a,const Source& b) {
    return a.valid&&b.valid&&a.unit==b.unit&&a.weapon==b.weapon&&a.generation==b.generation&&a.slot==b.slot;
}
void Fault() {runtime.faulted.store(true,std::memory_order_release);++runtime.faults;}
__declspec(noinline) Source CaptureSource(uint32_t weapon,int32_t barrel) noexcept {
    Source out{};volatile bool faulted=false;
    __try {if(barrel>=0&&barrel<2&&Current())ReadSource(weapon,out);}
    __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted){Fault();out={};}
    return out;
}
__declspec(noinline) void __fastcall ScopeHook(uint32_t weapon,int32_t barrel,const void* effects) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const Source previous=scopeSource;scopeSource=CaptureSource(weapon,barrel);
    __try {if(runtime.scopeOriginal)runtime.scopeOriginal(weapon,barrel,effects);}
    __finally {scopeSource=previous;runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
void EffectBody(uint32_t unit,uint32_t tag,float scale,uintptr_t caller) {
    const Source previous=effectSource;effectSource={};
    if(caller==runtime.base+0x616237&&scopeSource.valid&&scopeSource.unit==unit&&Current())effectSource=scopeSource;
    __try {if(runtime.effectOriginal)runtime.effectOriginal(unit,tag,scale);}
    __finally {effectSource=previous;}
}
__declspec(noinline) void __fastcall EffectHook(uint32_t unit,uint32_t tag,float scale) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {EffectBody(unit,tag,scale,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
bool Capture(int32_t user,uint32_t tag,float scale,const void* definition,uintptr_t caller) {
    if(user!=0||tag==UINT32_MAX||!definition||!effectSource.valid||
        caller!=runtime.base+0x1E69F5||!Current()||!std::isfinite(scale)||scale<0)return false;
    Source current{};
    if(!ReadSource(effectSource.weapon,current)||!SameSource(current,effectSource))return false;
    Voice pending{};pending.owner=current;pending.scale=scale;pending.atMs=GetTickCount64();
    pending.token=VR_WeaponHapticToken(GameTitle::Halo4,current.generation,current.slot==1);
    if(!pending.token)return false;
    std::memcpy(pending.definition,definition,sizeof(pending.definition));
    // Native source definition records its output user at +4. Never steal a
    // passenger/local split-screen effect from a different output user.
    int32_t sourceUser=-1;std::memcpy(&sourceUser,pending.definition+4,4);
    if(sourceUser!=user||!LoadBands(tag,pending.bands))return false;
    for(int band=0;band<2;++band) {
        const float duration=ReadFloat(pending.bands,band*0x18);
        if(!std::isfinite(duration)||duration<0||duration>60)return false;
    }
    if(!std::isfinite(ReadFloat(pending.definition,0x28)))return false;
    pending.active=true;
    if(runtime.writer.test_and_set(std::memory_order_acquire))return false;
    __try {
        unsigned chosen=0;
        for(unsigned i=0;i<8;++i){if(!runtime.voices[i].active){chosen=i;break;}
            if(runtime.voices[i].age>runtime.voices[chosen].age)chosen=i;}
        runtime.voices[chosen]=pending;
    } __finally {runtime.writer.clear(std::memory_order_release);}
    ++runtime.captured;return true;
}
void EnqueueBody(int32_t user,uint32_t tag,float scale,const void* definition,uintptr_t caller) {
    volatile bool routed=false,faulted=false;
    __try {routed=Capture(user,tag,scale,definition,caller);}
    __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted)Fault();
    if(routed)return;
    if(effectSource.valid)++runtime.fallback;
    if(runtime.enqueueOriginal)runtime.enqueueOriginal(user,tag,scale,definition);
}
__declspec(noinline) void __fastcall EnqueueHook(int32_t user,uint32_t tag,float scale,const void* definition) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {EnqueueBody(user,tag,scale,definition,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
void UpdateVoices(float dt) {
    if(runtime.writer.test_and_set(std::memory_order_acquire))return;
    __try {
        const bool admitted=Current()&&std::isfinite(dt)&&dt>0&&dt<=.25f;
        const uint64_t now=GetTickCount64();float motors[2][2]{};Source owners[2]{};uint64_t tokens[2]{};
        for(auto& voice:runtime.voices) {
            if(!voice.active)continue;
            Source current{};
            if(!admitted||now<voice.atMs||now-voice.atMs>100||
                !ReadSource(voice.owner.weapon,current)||!SameSource(current,voice.owner)||
                VR_WeaponHapticToken(GameTitle::Halo4,current.generation,current.slot==1)!=voice.token) {voice={};continue;}
            voice.atMs=now;voice.age+=dt;
            runtime.sourceUpdate(voice.definition);
            const float attenuation=ReadFloat(voice.definition,0x28);
            if(!std::isfinite(attenuation)||attenuation<0){Fault();voice={};continue;}
            bool playing=false,valid=true;float voiceMotor[2]{};
            for(int band=0;band<2;++band) {
                const float duration=ReadFloat(voice.bands,band*0x18);
                if(voice.age>=duration)continue;
                const void* curve=voice.bands+band*0x18+4;
                const float input=runtime.curveInput(curve,std::clamp(voice.age/duration,0.0f,1.0f),1.0f);
                if(!std::isfinite(input)){Fault();valid=false;break;}
                const float value=runtime.curveOutput(curve,input)*voice.scale*attenuation;
                if(!std::isfinite(value)){Fault();valid=false;break;}
                voiceMotor[band]=std::max(0.0f,value);playing=true;
            }
            if(!playing||!valid)voice={};else {
                for(int band=0;band<2;++band)motors[current.slot][band]+=voiceMotor[band];
                owners[current.slot]=current;tokens[current.slot]=voice.token;
            }
        }
        for(int slot=0;slot<2;++slot)if(owners[slot].valid&&!runtime.faulted.load()) {
            const auto quantize=[](float v){return uint16_t(std::clamp(v,0.0f,1.0f)*65535.0f+0.5f);};
            const float value=quantize(motors[slot][0])*(.65f/65535.0f)+quantize(motors[slot][1])*(.35f/65535.0f);
            if(value>0)(void)VR_PulseWeaponHaptics(GameTitle::Halo4,owners[slot].generation,slot==1,slot==0&&Supported(owners[slot]),value,tokens[slot]);
        }
    } __finally {runtime.writer.clear(std::memory_order_release);}
}
__declspec(noinline) void UpdatePrivate(float dt) {
    volatile bool faulted=false;
    __try {UpdateVoices(dt);} __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted)Fault();
}
__declspec(noinline) void ObserveEvaluation(const void* queue) {
    volatile bool faulted=false;
    __try {const void* own=nullptr;if(sampling&&Current()&&ReadQueue(own)&&own==queue)sampled=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted)Fault();
}
__declspec(noinline) uint64_t __fastcall EvaluateHook(const void* queue) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);uint64_t result=0;
    __try {if(runtime.evaluateOriginal)result=runtime.evaluateOriginal(queue);ObserveEvaluation(queue);}
    __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}return result;
}
__declspec(noinline) void __fastcall UpdateHook(float dt) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const bool wasSampling=sampling,wasSampled=sampled;sampling=true;sampled=false;
    __try {
        if(runtime.updateOriginal)runtime.updateOriginal(dt);
        UpdatePrivate(sampled?dt:0);
    } __finally {sampling=wasSampling;sampled=wasSampled;runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
} // namespace halo4_weapon_haptics
