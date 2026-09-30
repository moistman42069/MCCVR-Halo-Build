// Own-title native contract: docs/ODST-WEAPON-HAPTICS-2026-09-30.md.
namespace odst_weapon_haptics {
using ScopeFn=void(__fastcall*)(uint32_t,int16_t,uint8_t);
using EffectFn=void(__fastcall*)(uint32_t,uint32_t,float);
using WorkerFn=void(__fastcall*)(uint32_t,int32_t,uint32_t,uint64_t,uint64_t,uint32_t,float,uint8_t);
using UpdateFn=void(__fastcall*)(float);
using EvaluateFn=uint64_t(__fastcall*)(const void*);
using CurveFn=float(__fastcall*)(const void*,float,float);
struct Source {uint32_t unit=UINT32_MAX,weapon=UINT32_MAX,generation=0;uint8_t slot=0;bool valid=false;};
struct Voice {Source owner{};alignas(4) uint8_t bands[0x30]{};float scale=0,age=0;uint64_t atMs=0,token=0;bool active=false;};
struct Runtime {
    uintptr_t base=0;uint32_t generation=0;void* targets[5]{};
    ScopeFn scopeOriginal=nullptr;EffectFn effectOriginal=nullptr;WorkerFn workerOriginal=nullptr;
    UpdateFn updateOriginal=nullptr;EvaluateFn evaluateOriginal=nullptr;CurveFn curve=nullptr;
    std::atomic<bool> enabled{false},faulted{false};std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> captured{0},fallback{0},faults{0};
    std::atomic_flag writer=ATOMIC_FLAG_INIT;Voice voices[8]{};
} runtime;
thread_local Source scopeSource{},effectSource{};
thread_local bool sampling=false,sampled=false;
bool Current() noexcept;
bool ReadSource(uint32_t weapon,Source& result);
bool LoadBands(uint32_t tag,uint32_t row,uint8_t (&bands)[0x30]);
bool ReadQueue(int32_t user,uint8_t*& queue);
bool Supported(const Source& source);
float ReadFloat(const void* data,size_t offset) {float out;std::memcpy(&out,static_cast<const uint8_t*>(data)+offset,4);return out;}
uint32_t ReadU32(const void* data,size_t offset) {uint32_t out;std::memcpy(&out,static_cast<const uint8_t*>(data)+offset,4);return out;}
bool SameSource(const Source& a,const Source& b) {
    return a.valid&&b.valid&&a.slot<2&&b.slot<2&&a.unit==b.unit&&a.weapon==b.weapon&&a.generation==b.generation&&a.slot==b.slot;
}
void Fault() {runtime.faulted.store(true,std::memory_order_release);++runtime.faults;}
bool ValidBands(const uint8_t (&bands)[0x30]) {
    for(int i=0;i<2;++i){const float v=ReadFloat(bands,i*0x18);if(!std::isfinite(v)||v<0||v>60)return false;}return true;
}
bool Expired(const Voice& voice) {return !voice.active||(voice.age>=ReadFloat(voice.bands,0)&&voice.age>=ReadFloat(voice.bands,0x18));}
__declspec(noinline) Source CaptureSource(uint32_t weapon,int16_t barrel) noexcept {
    Source out{};volatile bool faulted=false;
    __try {if(barrel>=0&&barrel<2&&Current())ReadSource(weapon,out);}
    __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted){Fault();out={};}return out;
}
__declspec(noinline) void __fastcall ScopeHook(uint32_t weapon,int16_t barrel,uint8_t predicted) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const Source previous=scopeSource;scopeSource=CaptureSource(weapon,barrel);
    __try {if(runtime.scopeOriginal)runtime.scopeOriginal(weapon,barrel,predicted);}
    __finally {scopeSource=previous;runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
void EffectBody(uint32_t unit,uint32_t tag,float scale,uintptr_t caller) {
    const Source previous=effectSource;effectSource={};
    if(caller==runtime.base+0x3AD8F2&&scopeSource.valid&&scopeSource.unit==unit&&Current())effectSource=scopeSource;
    __try {if(runtime.effectOriginal)runtime.effectOriginal(unit,tag,scale);}
    __finally {effectSource=previous;}
}
__declspec(noinline) void __fastcall EffectHook(uint32_t unit,uint32_t tag,float scale) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {EffectBody(unit,tag,scale,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
struct Pending {
    uint8_t* queue=nullptr;uint8_t before[0x90]{};unsigned selected=0;Voice voice{};bool valid=false,restoreSilent=true;
};
bool Prepare(uint32_t unit,int32_t user,uint32_t tag,float scale,uintptr_t caller,Pending& p) {
    if(!Current()||!effectSource.valid||effectSource.unit!=unit||user!=0||tag==UINT32_MAX||
        !std::isfinite(scale)||scale<0||(caller!=runtime.base+0x1E3CDB&&caller!=runtime.base+0x1E3D20))return false;
    Source current{};if(!ReadSource(effectSource.weapon,current)||!SameSource(current,effectSource))return false;
    if(!ReadQueue(user,p.queue)||!p.queue)return false;
    std::memcpy(p.before,p.queue,sizeof(p.before));
    for(unsigned i=0;i<8;++i){const float age=ReadFloat(p.before,0x70+i*4);if(!std::isfinite(age)||age<0)return false;
        if(age>ReadFloat(p.before,0x70+p.selected*4))p.selected=i;}
    const uint32_t oldTag=ReadU32(p.before,p.selected*12),oldRow=ReadU32(p.before,p.selected*12+4);
    if(oldTag!=UINT32_MAX&&oldRow!=UINT32_MAX) {
        uint8_t oldBands[0x30]{};const float age=ReadFloat(p.before,0x70+p.selected*4);
        if(!LoadBands(oldTag,oldRow,oldBands)||!ValidBands(oldBands))return false;
        p.restoreSilent=age>=ReadFloat(oldBands,0)&&age>=ReadFloat(oldBands,0x18);
    }
    p.voice.owner=current;p.voice.scale=scale;p.voice.atMs=GetTickCount64();
    p.voice.token=VR_WeaponHapticToken(GameTitle::Halo3ODST,current.generation,current.slot==1);
    return p.voice.token!=0;
}
__declspec(noinline) void PrepareSafe(uint32_t unit,int32_t user,uint32_t tag,float scale,uintptr_t caller,Pending& p) {
    volatile bool faulted=false;
    __try {p.valid=Prepare(unit,user,tag,scale,caller,p);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted){Fault();p.valid=false;}
}
bool Finish(Pending& p,uint32_t tag,float scale) {
    if(!p.valid||!Current())return false;
    Source current{};if(!ReadSource(p.voice.owner.weapon,current)||!SameSource(current,p.voice.owner)||
        VR_WeaponHapticToken(GameTitle::Halo3ODST,current.generation,current.slot==1)!=p.voice.token)return false;
    uint8_t* queue=nullptr;if(!ReadQueue(0,queue)||queue!=p.queue)return false;
    uint8_t after[0x90]{};std::memcpy(after,queue,sizeof(after));
    const size_t offset=p.selected*12;const uint32_t row=ReadU32(after,offset+4);
    if(ReadU32(after,offset)!=tag||std::memcmp(after+offset+8,&scale,4)||ReadFloat(after,0x70+p.selected*4)!=0)return false;
    // A matching old age-zero cue is not proof that this worker enqueued.
    // A flags/row early return must never reassign somebody else's old cue.
    if(!std::memcmp(after+offset,p.before+offset,12)&&
        !std::memcmp(after+0x70+p.selected*4,p.before+0x70+p.selected*4,4))return false;
    // No concurrent/nested native mutation may be erased, including the native
    // trigger motor timers between the queue records and their ages.
    std::memcpy(after+offset,p.before+offset,12);
    std::memcpy(after+0x70+p.selected*4,p.before+0x70+p.selected*4,4);
    if(std::memcmp(after,p.before,sizeof(after)))return false;
    if(!LoadBands(tag,row,p.voice.bands)||!ValidBands(p.voice.bands))return false;
    p.voice.active=true;if(Expired(p.voice))return false;
    unsigned free=0;for(unsigned i=0;i<8;++i){if(Expired(runtime.voices[i])){free=i;break;}
        if(runtime.voices[i].age>runtime.voices[free].age)free=i;}
    runtime.voices[free]=p.voice;
    if(p.restoreSilent){
        std::memcpy(queue+offset,p.before+offset,12);
        std::memcpy(queue+0x70+p.selected*4,p.before+0x70+p.selected*4,4);
    }else{
        // Stock already evicted this active record. Preserve that eviction;
        // remove only the exact newly copied recoil with native NONE values.
        const uint32_t empty[3]{UINT32_MAX,UINT32_MAX,0x3F800000};
        std::memcpy(queue+offset,empty,sizeof(empty));
    }
    ++runtime.captured;return true;
}
__declspec(noinline) void FinishSafe(Pending& p,uint32_t tag,float scale) {
    volatile bool faulted=false,routed=false;
    __try {routed=Finish(p,tag,scale);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted)Fault();if(!routed)++runtime.fallback;
}
void WorkerBody(uint32_t unit,int32_t user,uint32_t tag,uint64_t context,uint64_t position,uint32_t extra,float scale,uint8_t flags,uintptr_t caller) {
    const bool owns=effectSource.valid&&Current()&&!runtime.writer.test_and_set(std::memory_order_acquire);
    Pending pending{};
    __try {
        if(owns)PrepareSafe(unit,user,tag,scale,caller,pending);
        // Never swallow a native visual/player-effect exception.
        if(runtime.workerOriginal)runtime.workerOriginal(unit,user,tag,context,position,extra,scale,flags);
        if(owns)FinishSafe(pending,tag,scale);
    } __finally {if(owns)runtime.writer.clear(std::memory_order_release);}
}
__declspec(noinline) void __fastcall WorkerHook(uint32_t unit,int32_t user,uint32_t tag,uint64_t context,uint64_t position,uint32_t extra,float scale,uint8_t flags) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {WorkerBody(unit,user,tag,context,position,extra,scale,flags,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
    __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
__declspec(noinline) void ObserveEvaluation(const void* queue) {
    volatile bool faulted=false;
    __try {uint8_t* own=nullptr;if(sampling&&Current()&&ReadQueue(0,own)&&own==queue)sampled=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted)Fault();
}
__declspec(noinline) uint64_t __fastcall EvaluateHook(const void* queue) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);uint64_t result=0;
    __try {if(runtime.evaluateOriginal)result=runtime.evaluateOriginal(queue);ObserveEvaluation(queue);}
    __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}return result;
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
                VR_WeaponHapticToken(GameTitle::Halo3ODST,current.generation,current.slot==1)!=voice.token) {voice={};continue;}
            voice.atMs=now;
            bool playing=false,valid=true;float voiceMotor[2]{};
            for(int band=0;band<2;++band) {
                const float duration=ReadFloat(voice.bands,band*0x18);
                if(voice.age>=duration)continue;
                const void* curve=voice.bands+band*0x18+4;
                const float value=runtime.curve(curve,std::clamp(voice.age/duration,0.0f,1.0f),1.0f)*voice.scale;
                if(!std::isfinite(value)){Fault();valid=false;break;}
                voiceMotor[band]=std::max(0.0f,value);playing=true;
            }
            if(!playing||!valid)voice={};else {
                voice.age+=dt;
                for(int band=0;band<2;++band)motors[current.slot][band]+=voiceMotor[band];
                owners[current.slot]=current;tokens[current.slot]=voice.token;
            }
        }
        for(int slot=0;slot<2;++slot)if(owners[slot].valid&&!runtime.faulted.load()) {
            const auto quantize=[](float v){return uint16_t(std::clamp(v,0.0f,1.0f)*65535.0f+0.5f);};
            const float value=quantize(motors[slot][0])*(.65f/65535.0f)+quantize(motors[slot][1])*(.35f/65535.0f);
            if(value>0)(void)VR_PulseWeaponHaptics(GameTitle::Halo3ODST,owners[slot].generation,slot==1,slot==0&&Supported(owners[slot]),value,tokens[slot]);
        }
    } __finally {runtime.writer.clear(std::memory_order_release);}
}
__declspec(noinline) void UpdatePrivate(float dt) {
    volatile bool faulted=false;
    __try {UpdateVoices(dt);} __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
    if(faulted)Fault();
}
__declspec(noinline) void __fastcall UpdateHook(float dt) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const bool wasSampling=sampling,wasSampled=sampled;sampling=true;sampled=false;
    __try {if(runtime.updateOriginal)runtime.updateOriginal(dt);UpdatePrivate(sampled?dt:0);}
    __finally {sampling=wasSampling;sampled=wasSampled;runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
} // namespace odst_weapon_haptics
