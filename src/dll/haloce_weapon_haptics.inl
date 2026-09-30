// Included in CE contact's independently verified damage-hook owner. Only the
// exact native firing-damage caller may divert an authored vibration envelope.
Hook hapticEnqueueHook,hapticUpdateHook,hapticTriggerHook;
std::atomic<bool> hapticReady{},hapticFault{};
std::atomic_flag hapticWriter=ATOMIC_FLAG_INIT;
std::atomic<uint64_t> hapticCaptured{},hapticFallback{},hapticFaults{};
using HapticEnqueueFn=void(__fastcall*)(int16_t,const void*,float,float);
using HapticUpdateFn=void(__fastcall*)();
using HapticCurveFn=float(__fastcall*)(uint16_t,float);
using HapticTriggerFn=void(__fastcall*)(uint32_t,int16_t);
thread_local uint32_t recoilTriggerWeapon=UINT32_MAX;
struct RecoilSource { uint32_t owner{},weapon{},generation{};int16_t user{-1};bool valid{}; };
thread_local RecoilSource recoilSource;
struct RecoilVoice
{
    RecoilSource source{};
    float amplitude[2]{},duration[2]{},age{};
    uint16_t curve[2]{};
    uint64_t atMs{},token{};
    bool active{};
};
RecoilVoice recoilVoices[8]{};

bool RecoilPlayer(HaloCELocalPlayerState& state) noexcept
{
    return Current()&&hapticReady.load()&&!hapticFault.load()&&
        HaloCEControls_GetLocalPlayerState(state)&&state.generation==generation.load()&&
        state.inputUser==0&&state.hasControlledUnit&&state.onFoot&&
        state.weapon!=UINT32_MAX&&state.nativePreparesFirstPerson&&
        !state.nativeInputBlocked&&!state.nativePaused&&!state.nativeCinematicFlag;
}
RecoilSource CaptureRecoilSource(uint32_t target,uintptr_t caller) noexcept
{
    RecoilSource result{};
    __try {
        HaloCELocalPlayerState state{};
        if(caller==moduleBase+haptic_contract::firingDamageReturn&&
            RecoilPlayer(state)&&target==state.unit&&state.weapon==recoilTriggerWeapon)
            result={state.unit,state.weapon,state.generation,state.inputUser,true};
    } __except(EXCEPTION_EXECUTE_HANDLER) { hapticFault=true;++hapticFaults; }
    return result;
}
__declspec(noinline) void __fastcall HapticTriggerHook(uint32_t weapon,int16_t trigger)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    const uint32_t previous=recoilTriggerWeapon;
    recoilTriggerWeapon=trigger>=0&&trigger<2?weapon:UINT32_MAX;
    __try { if(hapticTriggerHook.original)reinterpret_cast<HapticTriggerFn>(hapticTriggerHook.original)(weapon,trigger); }
    __finally { recoilTriggerWeapon=previous;callbacks.fetch_sub(1,std::memory_order_release); }
}
bool CaptureRecoilEnvelope(int16_t user,const void* definition,float scale,float duration,
    uintptr_t caller)
{
    if(!recoilSource.valid||user!=recoilSource.user||
        caller!=moduleBase+haptic_contract::enqueueReturn||!definition||
        !Current()||!hapticReady.load()||hapticFault.load())return false;
    float data[15]{};std::memcpy(data,definition,sizeof(data));
    if(!std::isfinite(scale)||!std::isfinite(duration)||duration<0||
        !std::isfinite(data[10]))return false;
    const float factor=(1-data[10])*scale+data[10];
    RecoilVoice pending{};pending.source=recoilSource;pending.active=true;pending.atMs=GetTickCount64();
    pending.token=VR_WeaponHapticToken(GameTitle::HaloCE,recoilSource.generation,false);
    if(!pending.token)return false;
    for(int band=0;band<2;++band)
    {
        pending.amplitude[band]=data[band*5]*factor;
        pending.duration[band]=data[band*5+1]*duration;
        std::memcpy(&pending.curve[band],data+band*5+2,2);
        if(!std::isfinite(pending.amplitude[band])||pending.amplitude[band]<0||
            !std::isfinite(pending.duration[band])||pending.duration[band]<0||
            pending.curve[band]>5)return false;
    }
    if(hapticWriter.test_and_set(std::memory_order_acquire))return false;
    __try {
        unsigned oldest=0;
        for(unsigned i=0;i<8;++i) {
            if(!recoilVoices[i].active){oldest=i;break;}
            if(recoilVoices[i].age>recoilVoices[oldest].age)oldest=i;
        }
        recoilVoices[oldest]=pending;
    } __finally { hapticWriter.clear(std::memory_order_release); }
    ++hapticCaptured;return true;
}
void HapticEnqueueBody(int16_t user,const void* definition,float scale,float duration,uintptr_t caller)
{
    bool routed=false;
    __try { routed=CaptureRecoilEnvelope(user,definition,scale,duration,caller); }
    __except(EXCEPTION_EXECUTE_HANDLER) { hapticFault=true;++hapticFaults; }
    if(routed)return;
    if(recoilSource.valid)++hapticFallback;
    if(hapticEnqueueHook.original)
        reinterpret_cast<HapticEnqueueFn>(hapticEnqueueHook.original)(user,definition,scale,duration);
}
__declspec(noinline) void __fastcall HapticEnqueueHook(int16_t user,const void* definition,float scale,float duration)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { HapticEnqueueBody(user,definition,scale,duration,reinterpret_cast<uintptr_t>(_ReturnAddress())); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
void UpdateRecoilEnvelopes()
{
    if(hapticWriter.test_and_set(std::memory_order_acquire))return;
    __try {
        HaloCELocalPlayerState state{};const uint64_t now=GetTickCount64();
        const bool admitted=RecoilPlayer(state);
        const uint64_t token=admitted?VR_WeaponHapticToken(GameTitle::HaloCE,state.generation,false):0;
        float motor[2]{};
        for(auto& voice:recoilVoices)
        {
            if(!voice.active)continue;
            if(!admitted||now-voice.atMs>100||voice.source.owner!=state.unit||voice.source.weapon!=state.weapon||
                voice.source.generation!=state.generation||
                !token||voice.token!=token){voice={};continue;}
            voice.atMs=now;
            bool playing=false;
            for(int band=0;band<2;++band)
                if(voice.age<voice.duration[band]) {
                    const float fraction=std::clamp(1-voice.age/voice.duration[band],0.0f,1.0f);
                    const float value=reinterpret_cast<HapticCurveFn>(moduleBase+haptic_contract::curve)(voice.curve[band],fraction);
                    if(!std::isfinite(value)){hapticFault=true;++hapticFaults;voice={};break;}
                    motor[band]+=value*voice.amplitude[band];playing=true;
                }
            voice.age+=1.0f/30.0f; // Own native B9B400 tick, cold-verified constant.
            if(!playing)voice={};
        }
        if(admitted&&!hapticFault.load()) {
            SupportGripRelationshipSnapshot support{};
            const bool supported=(!g_config.persistent_support_grip&&VR_IsTwoHandAiming())||
                (VR_GetSupportGripRelationship(support)&&support.engaged&&
                support.title==GameTitle::HaloCE&&support.generation==state.generation&&
                support.unit==state.unit&&support.weapon==state.weapon);
            const auto quantize=[](float v){return uint16_t(std::clamp(v,0.0f,1.0f)*65535.0f+0.5f);};
            const float value=quantize(motor[0])*(.65f/65535.0f)+quantize(motor[1])*(.35f/65535.0f);
            if(value>0) (void)VR_PulseWeaponHaptics(GameTitle::HaloCE,state.generation,false,supported,value,
                token);
        }
    } __finally { hapticWriter.clear(std::memory_order_release); }
}
__declspec(noinline) void __fastcall HapticUpdateHook()
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        if(hapticUpdateHook.original)reinterpret_cast<HapticUpdateFn>(hapticUpdateHook.original)();
        __try { UpdateRecoilEnvelopes(); }
        __except(EXCEPTION_EXECUTE_HANDLER) { hapticFault=true;++hapticFaults; }
    } __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
