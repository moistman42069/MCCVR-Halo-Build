// CE's own array-based native input contract. This is included in the optional
// bridge translation unit; it never treats CE memory as a Reach button record.
namespace native_ce {
constexpr auto title=GameTitle::HaloCE;
constexpr uintptr_t converterRva=0xC0E330, consumerRva=0xA97CF4, consumerEnd=0xA99159;
constexpr uintptr_t callerReturn=0xA991F4, getterRva=0xADC640, stateProofRva=0xA97E04;
constexpr const char* converterPattern=
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 81 EC E0 08 00 00 "
    "33 ED 0F 29 70 C8 4D 8B D0 0F 29 78 B8 8B FA 8B F1";
constexpr const char* consumerPattern=
    "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 30 EE FF FF B8 D0 12 00 00";
constexpr const char* getterPattern=
    "48 63 C1 48 8D 0D ?? ?? ?? ?? 48 8D 04 40 48 C1 E0 05 48 03 C1 C3";
constexpr const char* stateProofPattern=
    "41 83 FE 03 0F 87 E1 12 00 00 4C 89 74 24 28 48 8D 05 E6 81 56 FF "
    "48 8D 80 90 28 EA 02 41 8B CF 4F 8D 34 76 49 C1 E6 05 49 03 C6 48 89 44 24 78";
constexpr uintptr_t callerProofRva=0xA991D8;
constexpr const char* callerPattern=
    "4C 8D 8D 00 08 00 00 66 44 89 BD 1C 08 00 00 0F 28 D3 41 8B D0 8B CB E8 00 EB FF FF";
struct Runtime {
    HMODULE module{};
    uintptr_t base{},state{};
    std::atomic<uint32_t> generation{},callbacks{};
    std::atomic<bool> admitted{},faulted{};
    std::atomic<uint64_t> consumerAt{},converted{},routed{};
    void* target[2]{},*original[2]{};
    bool enabled[2]{};
    uint64_t reportAt{};
} runtime;
uint32_t blockedGeneration{};
struct Held {uint16_t milliseconds{};uint8_t frames{};};
struct Local {
    uint32_t generation{};
    uint64_t publishedAt{},convertedAt{},cancelEpoch{};
    bool converted{},direct{};
    Held held[vr_mapping::Count]{};
} thread_local local;
bool Current() noexcept {
    return runtime.admitted.load(std::memory_order_acquire) &&
        !runtime.faulted.load(std::memory_order_relaxed) && g_config.vr_action_mapping &&
        TitleAdapter_GetActiveTitle()==title &&
        TitleAdapter_GetGeneration(title)==runtime.generation.load(std::memory_order_acquire);
}
using Convert=void(__fastcall*)(unsigned,int,const void*,void*);
using Consumer=void(__fastcall*)(int,unsigned,float,void*);
void __fastcall ConverterHook(unsigned controller,int deltaMs,const void* raw,void* output) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        reinterpret_cast<Convert>(runtime.original[0])(controller,deltaMs,raw,output);
        if(controller!=0)return;
        const auto now=GetTickCount64(),epoch=publication.cancelEpoch.load(std::memory_order_acquire);
        const auto generation=runtime.generation.load(std::memory_order_acquire);
        uint32_t actions=0;bool direct=false;uint64_t publishedAt=0;
        if(Current() && deltaMs>=0 && deltaMs<=1000 &&
            ReadPublication(now,actions,direct,&publishedAt,title,generation) &&
            epoch==publication.cancelEpoch.load(std::memory_order_acquire)) {
            if(local.generation!=generation){local={};local.generation=generation;}
            local.converted=true;local.direct=direct;local.publishedAt=publishedAt;
            local.convertedAt=now;local.cancelEpoch=epoch;
            for(unsigned i=0;i<vr_mapping::Count;++i) {
                auto& value=local.held[i];
                if(!direct || !(actions&(1u<<i)))value={};
                else {
                    value.frames=static_cast<uint8_t>(std::min(unsigned(value.frames)+1u,255u));
                    value.milliseconds=static_cast<uint16_t>(std::min(unsigned(value.milliseconds)+unsigned(deltaMs),65535u));
                }
            }
            runtime.converted.fetch_add(1,std::memory_order_relaxed);
        } else local.converted=false;
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
struct Overlay {
    unsigned char* state{};
    uint32_t changed{};
    Held native[vr_mapping::Count]{};
    Held borrowed[vr_mapping::Count]{};
};
__declspec(noinline) bool ReadDigital(const unsigned char* state,unsigned action,Held& value) noexcept {
    __try {
        value.frames=*reinterpret_cast<const volatile unsigned char*>(state+1+action);
        value.milliseconds=*reinterpret_cast<const volatile uint16_t*>(state+0x1A+2*action);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
__declspec(noinline) bool WriteDigital(unsigned char* state,unsigned action,const Held& value) noexcept {
    __try {
        *reinterpret_cast<volatile unsigned char*>(state+1+action)=value.frames;
        *reinterpret_cast<volatile uint16_t*>(state+0x1A+2*action)=value.milliseconds;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void Apply(int user,unsigned controller,uintptr_t caller,Overlay& overlay) noexcept {
        const auto now=GetTickCount64();
        if(!Current() || controller!=0 || user<0 || user>=4 || caller!=runtime.base+callerReturn ||
            local.generation!=runtime.generation.load(std::memory_order_acquire) || !local.converted ||
            !Fresh(local.convertedAt,now) || !Fresh(local.publishedAt,now) ||
            !publication.stamp.load(std::memory_order_acquire) ||
            publication.title.load(std::memory_order_acquire)!=title ||
            publication.generation.load(std::memory_order_acquire)!=local.generation ||
            local.cancelEpoch!=publication.cancelEpoch.load(std::memory_order_acquire))return;
        runtime.consumerAt.store(now,std::memory_order_release);
        if(!local.direct)return;
        overlay.state=reinterpret_cast<unsigned char*>(runtime.state);
        for(unsigned i=0;i<vr_mapping::Count;++i) {
            const auto action=vr_mapping::NativeAction(title,static_cast<vr_mapping::Action>(i));
            if(action>=25 || !local.held[i].frames)continue;
            Held native{};
            if(!ReadDigital(overlay.state,action,native)){runtime.faulted=true;return;}
            // A physical keyboard/pad action remains fully authoritative.
            if(native.frames)continue;
            overlay.native[i]=native;
            overlay.borrowed[i]=local.held[i];
            overlay.changed|=1u<<i; // restore even if the second write faults
            if(!WriteDigital(overlay.state,action,local.held[i])){runtime.faulted=true;return;}
        }
        if(overlay.changed)runtime.routed.fetch_add(1,std::memory_order_relaxed);
}
void Restore(Overlay& overlay) noexcept {
    // Restore only the digital fields this transaction borrowed. Native movement,
    // look and the consumer's +5C write survive; virtual history never reaches
    // the abstraction updater's next previous-state copy.
    for(unsigned i=0;i<vr_mapping::Count;++i)if(overlay.changed&(1u<<i)) {
        const auto action=vr_mapping::NativeAction(title,static_cast<vr_mapping::Action>(i));
        if(!WriteDigital(overlay.state,action,overlay.native[i]))runtime.faulted=true;
    }
}
void Resume(Overlay& overlay) noexcept {
    if(!Current()){overlay.changed=0;return;}
    for(unsigned i=0;i<vr_mapping::Count;++i)if(overlay.changed&(1u<<i)) {
            const auto action=vr_mapping::NativeAction(title,static_cast<vr_mapping::Action>(i));
            Held native{};
            if(!ReadDigital(overlay.state,action,native)){runtime.faulted=true;return;}
            // If a nested native call changed physical counters, retain them.
            overlay.native[i]=native;
            if(native.frames){overlay.changed&=~(1u<<i);continue;}
            if(!WriteDigital(overlay.state,action,overlay.borrowed[i])){runtime.faulted=true;return;}
    }
}
thread_local Overlay* activeOverlay{};
thread_local unsigned consumerDepth{};
void __fastcall ConsumerHook(int user,unsigned controller,float delta,void* output) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        Overlay overlay{};
        const bool outer=consumerDepth==0;
        const bool suspend=consumerDepth==1&&activeOverlay;
        if(suspend)Restore(*activeOverlay);
        if(outer){
            Apply(user,controller,reinterpret_cast<uintptr_t>(_ReturnAddress()),overlay);
            if(runtime.faulted.load(std::memory_order_acquire)){Restore(overlay);overlay.changed=0;}
            activeOverlay=&overlay;
        }
        ++consumerDepth;
        __try {reinterpret_cast<Consumer>(runtime.original[1])(user,controller,delta,output);}
        __finally {
            --consumerDepth;
            if(outer){Restore(overlay);activeOverlay=nullptr;}
            else if(suspend)Resume(*activeOverlay);
        }
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
void* const hooks[]{reinterpret_cast<void*>(&ConverterHook),reinterpret_cast<void*>(&ConsumerHook)};
bool Retire() {
    runtime.admitted.store(false,std::memory_order_release);runtime.consumerAt.store(0,std::memory_order_release);
    publication.stamp.store(0,std::memory_order_release);publication.cancelEpoch.fetch_add(1,std::memory_order_acq_rel);
    for(unsigned i=0;i<2;++i)if(runtime.enabled[i]) {
        const auto status=MCCVR_DisableHookForRetirement(runtime.target[i]);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED)return false;
        runtime.enabled[i]=false;
    }
    const void* functions[]{hooks[0],hooks[1],reinterpret_cast<void*>(&Apply),reinterpret_cast<void*>(&Restore),
        reinterpret_cast<void*>(&Resume),reinterpret_cast<void*>(&Current),reinterpret_cast<void*>(&ReadDigital),reinterpret_cast<void*>(&WriteDigital)};
    const void* originals[]{runtime.original[0],runtime.original[1],nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    if(!WaitForNativeDetourQuiescence(functions,originals,8,runtime.callbacks))return false;
    for(unsigned i=0;i<2;++i)if(runtime.target[i]) {
        if(MH_RemoveHook(runtime.target[i])!=MH_OK)return false;
        runtime.target[i]=runtime.original[i]=nullptr;
    }
    if(runtime.module)FreeLibrary(runtime.module);
    runtime.module=nullptr;runtime.base=runtime.state=0;runtime.generation=0;runtime.consumerAt=0;runtime.faulted=false;
    return true;
}
bool CanRoute(uint64_t now) noexcept {
    uint32_t actions=0;bool direct=false;
    return Current() && ReadPublication(now,actions,direct,nullptr,title,runtime.generation.load(std::memory_order_acquire)) &&
        Fresh(runtime.consumerAt.load(std::memory_order_acquire),now);
}
bool GestureRouting(uint64_t now) noexcept {
    uint32_t actions=0;bool direct=false;
    return Current() && ReadPublication(now,actions,direct,nullptr,title,runtime.generation.load(std::memory_order_acquire)) && direct;
}
void Poll() {
    const auto active=TitleAdapter_GetActiveTitle();
    const auto generation=TitleAdapter_GetGeneration(title);
    if(runtime.module&&runtime.faulted.load(std::memory_order_acquire)) {
        blockedGeneration=runtime.generation.load(std::memory_order_acquire);
        if(!Retire())return;
        LOG("Native VR action bridge CE: retired faulted optional hooks; transport routing retained");
    }
    if(runtime.module&&(active!=title||generation!=runtime.generation.load(std::memory_order_acquire)))
        if(!Retire())return;
    if(active!=title||!generation||generation==blockedGeneration||!g_config.vr_action_mapping)return;
    if(!runtime.module) {
        vr_mapping::Transports fallback{};
        if(!Game_ReadVrActionBindings(fallback,GetTickCount64()))return;
        const auto* descriptor=TitleRegistry_Find(title);
        if(!descriptor||!GetModuleHandleExW(0,descriptor->moduleName,&runtime.module))return;
        runtime.base=reinterpret_cast<uintptr_t>(runtime.module);runtime.generation=generation;
        uintptr_t base=0;size_t size=0;DWORD64 imageBase=0;
        bool proved=sig::ModuleRange(descriptor->moduleName,base,size)&&base==runtime.base&&
            Prove(base,size,converterRva,converterPattern)&&Prove(base,size,consumerRva,consumerPattern)&&
            Prove(base,size,getterRva,getterPattern)&&Prove(base,size,stateProofRva,stateProofPattern)&&
            Prove(base,size,callerProofRva,callerPattern);
        if(proved) {
            const auto* entry=RtlLookupFunctionEntry(base+consumerRva,&imageBase,nullptr);
            runtime.state=sig::RipTarget(base+getterRva+6,base+getterRva+10);
            proved=entry&&imageBase==base&&entry->BeginAddress==consumerRva&&entry->EndAddress==consumerEnd&&
                runtime.state==base+0x2EA2890&&runtime.state-base<size&&size-(runtime.state-base)>=4*0x60;
        }
        const uintptr_t rvas[]{converterRva,consumerRva};
        for(unsigned i=0;proved&&i<2;++i) {
            auto* target=reinterpret_cast<void*>(base+rvas[i]);
            proved=MH_CreateHook(target,hooks[i],&runtime.original[i])==MH_OK;
            if(proved){runtime.target[i]=target;proved=MH_EnableHook(target)==MH_OK;runtime.enabled[i]=proved;}
        }
        runtime.consumerAt=0;runtime.admitted.store(proved,std::memory_order_release);
        LOG("Native VR action bridge CE: %s; requires observed owned input cadence; runtime validation pending",
            proved?"verified hooks installed":"transport fallback (proof/hook failure)");
        if(!proved){blockedGeneration=generation;runtime.faulted=true;(void)Retire();return;}
    }
    const auto now=GetTickCount64();
    if(now>=runtime.reportAt) {
        runtime.reportAt=now+5000;
        LOG("Native VR action bridge CE: converter=%llu scopedReads=%llu%s",runtime.converted.load(),runtime.routed.load(),
            runtime.faulted.load()?" TRANSPORT FALLBACK (guarded access fault)":"");
    }
}
}
