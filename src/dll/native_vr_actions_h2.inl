// H2 native abstraction is a packed C0-byte snapshot, not a later-title
// abstract button pointer. Only the player builder's private copy is merged.
namespace native_h2 {
constexpr auto title=GameTitle::Halo2;
constexpr uintptr_t converterRva=0x6D8AB0,copierRva=0x6D9C40,elapsedRva=0x6D0C60;
constexpr const char* converterPattern=
    "48 8B C4 4C 89 48 20 4C 89 40 18 89 48 08 53 55 56 57 41 54 41 55 41 56 41 57 "
    "48 81 EC F8 00 00 00 48 C7 44 24 70 FE FF FF FF";
constexpr const char* copierPattern=
    "48 63 C1 48 8D 0C 40 48 C1 E1 06 48 8D 05 ?? ?? ?? ?? 48 03 C1 0F 10 00 0F 11 02 "
    "0F 10 48 10 0F 11 4A 10 0F 10 40 20 0F 11 42 20 0F 10 48 30 0F 11 4A 30";
constexpr const char* elapsedPattern="8B 05 22 9A F1 00 C3 CC CC CC CC CC CC CC CC CC";
constexpr uintptr_t cadenceProofRva=0x6D9128;
constexpr const char* cadenceProofPattern=
    "E8 33 7B FF FF 48 8D 53 02 89 44 24 20 44 0F B6 CD 45 33 C0 48 8B CB E8 EC 8C FF FF";
constexpr uintptr_t consumerBegin=0x6C0E30,consumerEnd=0x6C10F0,callerProofRva=0x6C0F3B,callerReturn=0x6C0F47;
constexpr const char* consumerEntryPattern=
    "F3 0F 11 5C 24 20 F3 0F 11 54 24 18 89 54 24 10 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 "
    "48 8D AC 24 48 D3 FF FF B8 B8 2D 00 00";
constexpr const char* callerPattern="48 8D 55 30 41 8B CC E8 F9 8C 01 00 8B 8D CC 2B 00 00 41 B5 01";
constexpr uintptr_t consumerReloadRva=0x6C36BF;
constexpr const char* consumerReloadPattern=
    "8B C1 0F BA F1 15 0F BA E8 15 44 38 75 B8 0F 47 C8 8B C1 0F BA F1 16 0F BA E8 16";
struct Runtime {
    HMODULE module{};uintptr_t base{},state{},elapsed{};
    std::atomic<uint32_t> generation{};
    void* target[2]{},*original[2]{};bool enabled[2]{},attempted{};
    std::atomic<bool> admitted{},faulted{};std::atomic<uint32_t> callbacks{};
    std::atomic<uint64_t> converted{},routed{},readerAt{};uint64_t reportAt{};
} runtime;
uint32_t blockedGeneration{};
struct ThreadState {
    uint32_t generation{};uint64_t publishedAt{},converterAt{},cancelEpoch{};
    bool direct{},converted{};
    native_action_button::Counters counters[vr_mapping::Count]{};
    void Reset(uint32_t next) noexcept {
        generation=next;publishedAt=converterAt=cancelEpoch=0;direct=converted=false;
        for(auto& c:counters)c={};
    }
} thread_local local;
bool Current() noexcept {
    return runtime.admitted.load(std::memory_order_acquire) && !runtime.faulted.load(std::memory_order_relaxed) &&
        g_config.vr_action_mapping && TitleAdapter_GetActiveTitle()==title &&
        TitleAdapter_GetGeneration(title)==runtime.generation;
}
__declspec(noinline) bool ReadElapsed(unsigned& value) noexcept {
    __try {value=*reinterpret_cast<const volatile unsigned*>(runtime.elapsed);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
using Convert=void(__fastcall*)(unsigned,void*,void*,void*,void*,void*);
using Copy=void(__fastcall*)(unsigned,void*);
void __fastcall ConverterHook(unsigned controller,void* prefs,void* raw,void* axes,void* look,void* output) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        reinterpret_cast<Convert>(runtime.original[0])(controller,prefs,raw,axes,look,output);
        const auto now=GetTickCount64();const auto epoch=publication.cancelEpoch.load(std::memory_order_acquire);
        uint32_t actions=0;bool direct=false;uint64_t publishedAt=0;unsigned elapsed=0;
        if(Current() && controller==0 && reinterpret_cast<uintptr_t>(output)==runtime.state &&
            ReadPublication(now,actions,direct,&publishedAt,title,runtime.generation.load(std::memory_order_acquire)) &&
            epoch==publication.cancelEpoch.load(std::memory_order_acquire)) {
            if(!ReadElapsed(elapsed)){runtime.faulted=true;local.converted=false;}
            else if(elapsed<=1000){
                if(local.generation!=runtime.generation)local.Reset(runtime.generation);
                local.publishedAt=publishedAt;local.converterAt=now;local.cancelEpoch=epoch;
                local.direct=direct;local.converted=true;
                for(unsigned i=0;i<vr_mapping::Count;++i)
                    native_action_button::Advance(local.counters[i],direct&&(actions&(1u<<i)),elapsed);
                runtime.converted.fetch_add(1,std::memory_order_relaxed);
            } else local.converted=false;
        } else if(controller==0)local.converted=false;
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
// The native copier has already filled this caller-owned snapshot. Never touch
// the global abstraction source, bindings, axes or consumption masks.
__declspec(noinline) bool MergeCopy(void* output) noexcept {
    __try {
        auto* bytes=reinterpret_cast<volatile uint8_t*>(output);
        for(unsigned semantic=0;semantic<vr_mapping::Count;++semantic){
            const auto& counter=local.counters[semantic];if(!counter.frames)continue;
            const unsigned action=vr_mapping::NativeAction(title,static_cast<vr_mapping::Action>(semantic));
            if(action>=0x30)continue;
            unsigned ids[3]{action,0xff,0xff};
            // H2's own weapon-context selectors consume these distinct IDs:
            // primary 0A/16/18; secondary 17/19. Grenade is independently 7.
            if(semantic==vr_mapping::Fire){ids[1]=0x16;ids[2]=0x18;}
            if(semantic==vr_mapping::Grenade){ids[1]=0x17;ids[2]=0x19;}
            for(const auto id:ids)if(id<0x30 && bytes[id]==0){
                bytes[id]=counter.frames;
                *reinterpret_cast<volatile uint16_t*>(bytes+0x30+id*2)=counter.milliseconds;
            }
            if(semantic==vr_mapping::Fire || semantic==vr_mapping::Grenade){
                const unsigned offset=semantic==vr_mapping::Fire?0x90:0x94;
                auto* amount=reinterpret_cast<volatile float*>(bytes+offset);
                if(*amount==0.0f)*amount=1.0f;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __fastcall CopierHook(unsigned controller,void* output) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        reinterpret_cast<Copy>(runtime.original[1])(controller,output);
        const auto now=GetTickCount64();
        if(Current() && controller==0 && reinterpret_cast<uintptr_t>(_ReturnAddress())==runtime.base+callerReturn &&
            local.generation==runtime.generation && local.converted && Fresh(local.converterAt,now) &&
            Fresh(local.publishedAt,now) && publication.stamp.load(std::memory_order_acquire) &&
            publication.title.load(std::memory_order_acquire)==title &&
            publication.generation.load(std::memory_order_acquire)==local.generation &&
            publication.cancelEpoch.load(std::memory_order_acquire)==local.cancelEpoch){
            runtime.readerAt.store(now,std::memory_order_release);
            if(local.direct){
                if(MergeCopy(output))runtime.routed.fetch_add(1,std::memory_order_relaxed);
                else runtime.faulted=true;
            }
        }
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
void* const hooks[]{reinterpret_cast<void*>(&ConverterHook),reinterpret_cast<void*>(&CopierHook)};
bool ProveConsumerBounds(uintptr_t base){
    DWORD64 imageBase=0;const auto* entry=RtlLookupFunctionEntry(base+consumerBegin,&imageBase,nullptr);
    return entry&&imageBase==base&&entry->BeginAddress==consumerBegin&&entry->EndAddress==consumerEnd;
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
    const void* functions[]{hooks[0],hooks[1],reinterpret_cast<void*>(&MergeCopy),reinterpret_cast<void*>(&Current),
        reinterpret_cast<void*>(&ReadElapsed)};
    const void* trampolines[]{runtime.original[0],runtime.original[1],nullptr,nullptr,nullptr};
    if(!WaitForNativeDetourQuiescence(functions,trampolines,5,runtime.callbacks)) return false;
    for(unsigned i=0;i<2;++i) if(runtime.target[i]) {
        if(MH_RemoveHook(runtime.target[i])!=MH_OK) return false;
        runtime.target[i]=runtime.original[i]=nullptr;
    }
    if(runtime.module) FreeLibrary(runtime.module);
    runtime.module=nullptr;runtime.base=runtime.state=runtime.elapsed=0;runtime.generation=0;
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
        LOG("Native VR action bridge H2: retired faulted optional hooks; transport routing retained");
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
        uintptr_t base=0;size_t size=0;
        bool proved=sig::ModuleRange(descriptor->moduleName,base,size) && base==runtime.base &&
            Prove(base,size,converterRva,converterPattern) && Prove(base,size,copierRva,copierPattern) &&
            Prove(base,size,elapsedRva,elapsedPattern) && Prove(base,size,cadenceProofRva,cadenceProofPattern) &&
            Prove(base,size,consumerBegin,consumerEntryPattern) &&
            Prove(base,size,callerProofRva,callerPattern) &&
            Prove(base,size,consumerReloadRva,consumerReloadPattern) && ProveConsumerBounds(base);
        if(proved) {
            runtime.state=sig::RipTarget(base+copierRva+14,base+copierRva+18);
            runtime.elapsed=sig::RipTarget(base+elapsedRva+2,base+elapsedRva+6);
            proved=runtime.state>=base && runtime.state-base<size && size-(runtime.state-base)>=4*0xC0 &&
                runtime.elapsed>=base && runtime.elapsed-base<size && size-(runtime.elapsed-base)>=4;
        }
        const uintptr_t rvas[]{converterRva,copierRva};
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
        LOG("Native VR action bridge H2: %s; requires observed owned input cadence; runtime validation pending",
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
        LOG("Native VR action bridge H2: converter=%llu directReads=%llu%s",
            runtime.converted.load(),runtime.routed.load(),runtime.faulted.load()?" TRANSPORT FALLBACK (guarded access fault)":"");
    }
}

} // namespace native_h2
