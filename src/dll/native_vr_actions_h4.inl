// H4's own kit consumer uses Reload 0x31, distinct from generic Use 2.
// Its converter ignores RCX: ownership comes from the proven native output row.
namespace native_h4 {
constexpr auto title=GameTitle::Halo4;
constexpr unsigned readerIndex=5;
constexpr uintptr_t converterRva=0x1201A8;
constexpr const char* converterPattern=
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 55 41 56 41 57 "
    "48 8B 4C 24 48 45 33 F6 8B F2 BD 01 00 00 00 66 45 39 70 04 74 03 40 88 29 "
    "41 80 78 06 1E 73 07 41 80 78 07 1E 72 03 40 88 29";
constexpr uintptr_t padProofRva=0x56E79;
constexpr const char* padProofPattern=
    "48 63 C3 48 6B C8 3C 48 8D 05 ?? ?? ?? ?? 47 89 8C 82 B0 03 00 00 48 03 C8 48 8D 57 FB";
constexpr uintptr_t controllerLoopRva=0x56FE0;
constexpr const char* controllerLoopPattern=
    "B8 01 00 00 00 48 81 C7 30 01 00 00 03 D8 48 03 F0 83 FB 04 0F 8C 4B FE FF FF";
constexpr uintptr_t consumerBegin=0xA2670,consumerEnd=0xA55EE,consumerReloadRva=0xA5264;
constexpr const char* consumerEntryPattern=
    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 FC FF FF 48 81 EC 48 04 00 00";
constexpr const char* consumerReloadPattern=
    "BA 31 00 00 00 49 8B CD E8 5B A3 09 00 48 8B C8 E8 7B C3 FF FF 44 8A C0 BA 12 00 00 00 "
    "48 8B CB E8 57 02 FF FF BA 31 00 00 00 49 8B CD E8 36 A3 09 00";
struct Runtime {
    HMODULE module{};
    uintptr_t base{}, state{}, padState{};
    std::atomic<uint32_t> generation{};
    void* target[2]{}, *original[2]{};
    bool enabled[2]{}, attempted{};
    std::atomic<bool> admitted{}, faulted{};
    std::atomic<uint32_t> callbacks{};
    std::atomic<uint64_t> converted{}, routed{}, readerAt{};
    uint64_t reportAt{};
} runtime;
uint32_t blockedGeneration{}; // worker-only: a fault is not retried every poll
struct ThreadState {
    uint32_t generation{};
    uint64_t publishedAt{}, converterAt{}, cancelEpoch{};
    uint32_t actions{};
    bool direct{}, converted{};
    native_action_button::Record records[vr_mapping::Count]{};
    void Reset(uint32_t next) noexcept {
        generation=next; publishedAt=converterAt=cancelEpoch=0;
        actions=0;direct=converted=false;
        for(unsigned i=0;i<vr_mapping::Count;++i)
            records[i].Reset(title,native_action_button::ConsumerAction(title,static_cast<vr_mapping::Action>(i)));
    }
} thread_local local;
bool Current() noexcept {
    return runtime.admitted.load(std::memory_order_acquire) &&
        !runtime.faulted.load(std::memory_order_relaxed) && g_config.vr_action_mapping &&
        TitleAdapter_GetActiveTitle()==title &&
        TitleAdapter_GetGeneration(title)==runtime.generation;
}
using Convert = void(__fastcall*)(uintptr_t,int,const void*,void*,void*,void*);
using Read = void*(__fastcall*)(void*,unsigned);

void __fastcall ConverterHook(uintptr_t unused,int elapsedMs,const void* raw,
    void* output,void* axes,void* activity) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        reinterpret_cast<Convert>(runtime.original[0])(unused,elapsedMs,raw,output,axes,activity);
        const auto now=GetTickCount64();
        const auto cancelEpoch=publication.cancelEpoch.load(std::memory_order_acquire);
        uint32_t actions=0;bool direct=false;uint64_t publishedAt=0;
        // Both direct and copied native routes write base + controller*0x3c.
        // H4 does not preserve RCX as a controller ID in this converter.
        if(Current() && reinterpret_cast<uintptr_t>(output)==runtime.padState &&
            ReadPublication(now,actions,direct,&publishedAt,title,runtime.generation.load(std::memory_order_acquire)) && elapsedMs>=0 && elapsedMs<=1000 &&
            cancelEpoch==publication.cancelEpoch.load(std::memory_order_acquire)) {
            if(local.generation!=runtime.generation) local.Reset(runtime.generation);
            local.publishedAt=publishedAt;local.actions=actions;local.direct=direct;
            local.cancelEpoch=cancelEpoch;
            local.converterAt=now;local.converted=true;
            for(unsigned i=0;i<vr_mapping::Count;++i)
                local.records[i].Advance(local.direct && (local.actions&(1u<<i)),unsigned(elapsedMs));
            runtime.converted.fetch_add(1,std::memory_order_relaxed);
        } else if(reinterpret_cast<uintptr_t>(output)==runtime.padState) local.converted=false;
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
__declspec(noinline) bool ReadController(uintptr_t state,unsigned& value) noexcept {
    __try {value=*reinterpret_cast<const volatile unsigned*>(state+0x7E4);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
__declspec(noinline) bool ReadFrames(const void* record,uint8_t& value) noexcept {
    __try {value=*(reinterpret_cast<const volatile uint8_t*>(record)+2);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void* ReadOwned(void* state,unsigned action,void* native,uintptr_t caller) noexcept {
        const auto now=GetTickCount64();
        // The abstraction updater also calls the reader to WRITE keyboard
        // counters. It must never receive a private VR record. Admit only the
        // independently matched native player-control builder's read calls.
        if(!Current() || caller<runtime.base+consumerBegin || caller>=runtime.base+consumerEnd ||
            reinterpret_cast<uintptr_t>(state)!=runtime.state)return native;
        unsigned controller=0;
        if(!ReadController(runtime.state,controller)){runtime.faulted=true;return native;}
        if(controller!=0 ||
            local.generation!=runtime.generation || !local.converted ||
            !Fresh(local.converterAt,now)||!Fresh(local.publishedAt,now)||!publication.stamp.load(std::memory_order_acquire)||
            publication.title.load(std::memory_order_acquire)!=title ||
            publication.generation.load(std::memory_order_acquire)!=local.generation ||
            local.cancelEpoch!=publication.cancelEpoch.load(std::memory_order_acquire)) return native;
        runtime.readerAt.store(now,std::memory_order_release);
        if(!local.direct) return native;
        const int semantic=native_action_button::SemanticAction(title,action);
        if(semantic<0) return native;
        auto& record=local.records[semantic];
        const auto virtualState=record.ReadCounters();
        if(!virtualState.frames) return native;
        // Native keyboard/gamepad holds retain their own counters, amount and
        // consumption record. Virtual input never edits or clears that record.
        if(native) {
            uint8_t frames=0;
            if(!ReadFrames(native,frames)){runtime.faulted=true;return native;}
            if(frames)return native;
        }
        runtime.routed.fetch_add(1,std::memory_order_relaxed);
        return record.bytes.data();
}
void* __fastcall ReaderHook(void* state,unsigned action) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    void* result=nullptr;
    __try {
        result=reinterpret_cast<Read>(runtime.original[1])(state,action);
        result=ReadOwned(state,action,result,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
    return result;
}
void* const hooks[]{reinterpret_cast<void*>(&ConverterHook),reinterpret_cast<void*>(&ReaderHook)};
bool ProveConsumerBounds(uintptr_t base) {
    DWORD64 imageBase=0;
    const auto* entry=RtlLookupFunctionEntry(base+consumerBegin,&imageBase,nullptr);
    return entry && imageBase==base && entry->BeginAddress==consumerBegin && entry->EndAddress==consumerEnd;
}
bool Retire() {
    runtime.admitted.store(false,std::memory_order_release);
    runtime.readerAt.store(0,std::memory_order_release);
    publication.stamp.store(0,std::memory_order_release);
    publication.cancelEpoch.fetch_add(1,std::memory_order_acq_rel);
    for(unsigned i=0;i<2;++i) if(runtime.enabled[i]) {
        const auto status=MCCVR_DisableHookForRetirement(runtime.target[i]);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED) return false;
        runtime.enabled[i]=false;
    }
    const void* functions[]{hooks[0],hooks[1],reinterpret_cast<void*>(&ReadOwned),reinterpret_cast<void*>(&Current),
        reinterpret_cast<void*>(&ReadController),reinterpret_cast<void*>(&ReadFrames)};
    const void* trampolines[]{runtime.original[0],runtime.original[1],nullptr,nullptr,nullptr,nullptr};
    if(!WaitForNativeDetourQuiescence(functions,trampolines,6,runtime.callbacks)) return false;
    for(unsigned i=0;i<2;++i) if(runtime.target[i]) {
        if(MH_RemoveHook(runtime.target[i])!=MH_OK) return false;
        runtime.target[i]=runtime.original[i]=nullptr;
    }
    if(runtime.module) FreeLibrary(runtime.module);
    runtime.module=nullptr;runtime.base=runtime.state=runtime.padState=0;runtime.generation=0;
    runtime.readerAt.store(0,std::memory_order_release);
    runtime.attempted=false;runtime.faulted=false;
    return true;
}
bool CanRoute(uint64_t now) noexcept {
    uint32_t actions=0;bool direct=false;
    return Current() && ReadPublication(now,actions,direct,nullptr,title,runtime.generation.load(std::memory_order_acquire)) &&
        Fresh(runtime.readerAt.load(std::memory_order_acquire),now);
}
bool GestureRouting(uint64_t now) noexcept {
    uint32_t actions=0;bool direct=false;
    return Current() && ReadPublication(now,actions,direct,nullptr,title,runtime.generation.load(std::memory_order_acquire)) && direct;
}
void Poll() {
    const auto active=TitleAdapter_GetActiveTitle();
    const auto generation=TitleAdapter_GetGeneration(title);
    if(runtime.module && runtime.faulted.load(std::memory_order_acquire)) {
        blockedGeneration=runtime.generation.load(std::memory_order_acquire);
        if(!Retire()) return;
        LOG("Native VR action bridge H4: retired faulted optional hooks; transport routing retained");
    }
    if(runtime.module && (active!=title || generation!=runtime.generation))
        if(!Retire()) return;
    if(active!=title || !generation || generation==blockedGeneration || !g_config.vr_action_mapping) return;
    if(!runtime.attempted) {
        // The preexisting read-only transport resolver must verify the reader
        // before MinHook changes its prologue. Retain that cache so optional
        // bridge failure can return to working layout-aware transport routing.
        vr_mapping::Transports fallback{};
        if(!Game_ReadVrActionBindings(fallback,GetTickCount64())) return;
        const auto* descriptor=TitleRegistry_Find(title);
        if(!descriptor || !GetModuleHandleExW(0,descriptor->moduleName,&runtime.module)) return;
        runtime.attempted=true;runtime.base=reinterpret_cast<uintptr_t>(runtime.module);
        runtime.generation=generation;
        const auto& d=kGestureBindings[readerIndex];
        uintptr_t base=0;size_t size=0;
        bool proved=sig::ModuleRange(descriptor->moduleName,base,size) && base==runtime.base &&
            Prove(base,size,converterRva,converterPattern) && Prove(base,size,d.readerRva,d.readerPattern) &&
            Prove(base,size,d.stateLoadRva,d.statePattern) &&
            Prove(base,size,consumerBegin,consumerEntryPattern) &&
            Prove(base,size,consumerReloadRva,consumerReloadPattern) &&
            Prove(base,size,padProofRva,padProofPattern) &&
            Prove(base,size,controllerLoopRva,controllerLoopPattern) && ProveConsumerBounds(base);
        if(proved) {
            runtime.state=sig::RipTarget(base+d.stateLoadRva+10,base+d.stateLoadRva+14);
            proved=runtime.state>=base && runtime.state-base<size && size-(runtime.state-base)>=4*d.stride;
            runtime.padState=sig::RipTarget(base+padProofRva+10,base+padProofRva+14);
            proved=proved && runtime.padState>=base && runtime.padState-base<size && size-(runtime.padState-base)>=4*0x3c;
        }
        const uintptr_t rvas[]{converterRva,d.readerRva};
        for(unsigned i=0;proved&&i<2;++i) {
            auto* target=reinterpret_cast<void*>(base+rvas[i]);
            proved=MH_CreateHook(target,hooks[i],&runtime.original[i])==MH_OK;
            if(proved) {
                runtime.target[i]=target;
                proved=MH_EnableHook(target)==MH_OK;
                runtime.enabled[i]=proved;
            }
        }
        runtime.readerAt.store(0,std::memory_order_release);
        runtime.admitted.store(proved,std::memory_order_release);
        LOG("Native VR action bridge H4: %s; requires observed owned input cadence; runtime validation pending",
            proved?"verified hooks installed":"transport fallback (proof/hook failure)");
        if(!proved) {
            blockedGeneration=generation;
            runtime.faulted.store(true,std::memory_order_release);
            (void)Retire();
            return;
        }
    }
    const auto now=GetTickCount64();
    if(now>=runtime.reportAt) {
        runtime.reportAt=now+5000;
        LOG("Native VR action bridge H4: converter=%llu directReads=%llu%s",
            runtime.converted.load(),runtime.routed.load(),runtime.faulted.load()?" TRANSPORT FALLBACK (guarded access fault)":"");
    }
}

} // namespace native_h4
