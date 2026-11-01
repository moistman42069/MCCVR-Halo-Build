// H3's actual native consumer has Reload 0x26, distinct from generic Use 3.
// All addresses/ABI/layout witnesses are H3-kit matched; no Reach offset reuse.
namespace native_h3 {
constexpr auto title=GameTitle::Halo3;
constexpr unsigned readerIndex=2;
constexpr uintptr_t converterRva=0x172434;
constexpr const char* converterPattern=
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 81 EC 70 02 00 00 "
    "49 8B D8 0F 29 70 C8 4C 8B 84 24 C8 02 00 00 45 33 E4 0F 29 78 B8 49 8B F9 8B EA 4C 63 F1";
constexpr uintptr_t controllerProofRva=0x1725EB;
constexpr const char* controllerProofPattern=
    "49 69 CE 1C 02 00 00 48 8D 05 C7 65 0B 02 BA 80 00 00 00 48 03 C1 48 8D 4C 24 30";
constexpr uintptr_t consumerBegin=0xF42D8,consumerEnd=0xF6487,consumerReloadRva=0xF6231;
constexpr const char* consumerEntryPattern=
    "48 8B C4 F3 0F 11 58 20 F3 0F 11 50 18 89 50 10 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 "
    "48 8D A8 C8 FE FF FF 48 81 EC F8 01 00 00";
constexpr const char* consumerReloadPattern=
    "BA 26 00 00 00 E8 ED 0D 09 00 48 8B C8 E8 F1 DA FF FF 8B 4F 2C 84 C0 74 06 0F BA E9 0C EB 04 "
    "0F BA F1 0C 89 4F 2C BA 26 00 00 00 49 8B CF E8 C4 0D 09 00";
// H3EK held bit 4 becomes secondary reload edge bit 6; three input aliases.
constexpr uintptr_t secondaryReloadRva=0xF614F;
constexpr const char* secondaryReloadPattern="BA 04 00 00 00 E8 CF 0E 09 00 48 8B C8 E8 D3 DB FF FF 84 C0 75 20 BA 42 00 00 00 49 8B CF E8 B6 0E 09 00 48 8B C8 E8 D6 DB FF FF 66 85 C0 75 06 83 67 2C EF EB 04 83 4F 2C 10 BA 04 00 00 00 49 8B CF E8 92 0E 09 00 48 8B C8 E8 B2 DB FF FF 0F B7 C0 3B C3 7D 22 BA 1F 00 00 00 49 8B CF E8 76 0E 09 00";
inline int SecondaryReloadRecord(unsigned action,uintptr_t caller) noexcept {
    if(action==4 && (caller==0xF6159 || caller==0xF6196))return 0;
    if(action==0x42 && caller==0xF6172)return 1;
    if(action==0x1F && caller==0xF61B2)return 2;
    return -1;
}
// Match H3EK's player-action edge resolver and output-user player mapping.
constexpr uintptr_t actionEdgesRva=0xE7C68,localPlayerRva=0xEE45C;
constexpr const char* actionEdgesPattern="48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 8B 72 40 41 BA 74 00 80 F0 44 8B 05 11 23 95 00 65 48 8B 04 25 58 00 00 00 0F B7 C9 41 B9 10 00 00 00 4A 8B 04 C0 4E 8B 04 08 4C 69 C9 F8 02 00 00";
constexpr const char* localPlayerPattern="83 C8 FF 3B C8 74 27 8B 15 33 BB 94 00 65 48 8B 04 25 58 00 00 00 41 B8 10 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 8B 84 88 B8 00 00 00 C3";
constexpr const char* edgeCallerPattern="48 8D 54 24 50 41 8B CF E8 84 2F 00 00";
constexpr const char* edgeFlagsPattern="45 84 C0 74 0D 40 84 FF 75 08 8B 42 40 83 C8 40 EB 06 8B 42 40 83 E0 BF 45 84 C0 75 0B 40 84 FF 74 06 0F BA E8 07 EB 04 0F BA F0 07 89 42 40 BF";
struct Runtime {
    HMODULE module{};
    uintptr_t base{}, state{};
    std::atomic<uint32_t> generation{};
    void* target[3]{}, *original[3]{};
    bool enabled[3]{}, attempted{};
    std::atomic<bool> secondaryEdgesReady{};
    uint32_t(__fastcall* localPlayer)(int){};
    std::atomic<bool> admitted{}, faulted{},secondaryReloadProved{};
    std::atomic<uint32_t> callbacks{};
    std::atomic<uint64_t> converted{}, routed{}, readerAt{};
    uint64_t reportAt{};
} runtime;
uint32_t blockedGeneration{}; // worker-only: a fault is not retried every poll
struct ThreadState {
    uint32_t generation{};
    uint64_t publishedAt{}, converterAt{}, cancelEpoch{};
    uint32_t actions{};
    uint32_t blocked{};
    bool direct{}, converted{};
    native_action_button::Record records[vr_mapping::Count]{};
    native_action_button::Record secondaryReleased[3]{};
    void Reset(uint32_t next) noexcept {
        generation=next; publishedAt=converterAt=cancelEpoch=0;
        actions=0;direct=converted=false;
        for(unsigned i=0;i<vr_mapping::Count;++i)
            records[i].Reset(title,native_action_button::ConsumerAction(title,static_cast<vr_mapping::Action>(i)));
    }
} thread_local local;
bool Current() noexcept {
    return runtime.admitted.load(std::memory_order_acquire) &&
        !runtime.faulted.load(std::memory_order_relaxed) && (g_config.vr_action_mapping||Config_GestureOnlyWeaponInput()||g_config.two_handed_aim) &&
        TitleAdapter_GetActiveTitle()==title &&
        TitleAdapter_GetGeneration(title)==runtime.generation;
}
using Convert = void(__fastcall*)(uintptr_t,int,const void*,void*,void*,void*);
using Read = void*(__fastcall*)(void*,unsigned);

void __fastcall ConverterHook(uintptr_t controller,int elapsedMs,const void* raw,
    void* output,void* axes,void* activity) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        reinterpret_cast<Convert>(runtime.original[0])(controller,elapsedMs,raw,output,axes,activity);
        const auto now=GetTickCount64();
        const auto cancelEpoch=publication.cancelEpoch.load(std::memory_order_acquire);
        uint32_t actions=0,blocked=0;bool direct=false;uint64_t publishedAt=0;
        // H3 preserves the real controller argument and uses it to select
        // native preferences, in both direct and copied gamepad routes.
        if(Current() && controller==0 &&
            ReadPublication(now,actions,direct,&publishedAt,title,runtime.generation.load(std::memory_order_acquire),&blocked) && elapsedMs>=0 && elapsedMs<=1000 &&
            cancelEpoch==publication.cancelEpoch.load(std::memory_order_acquire)) {
            if(local.generation!=runtime.generation) local.Reset(runtime.generation);
            local.publishedAt=publishedAt;local.actions=actions;local.direct=direct;local.blocked=blocked;
            local.cancelEpoch=cancelEpoch;
            local.converterAt=now;local.converted=true;
            for(unsigned i=0;i<vr_mapping::Count;++i)
                local.records[i].Advance((local.direct||(local.blocked&(1u<<i))) && (local.actions&(1u<<i)),unsigned(elapsedMs));
            runtime.converted.fetch_add(1,std::memory_order_relaxed);
        } else if(controller==0) local.converted=false;
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
}
__declspec(noinline) bool ReadController(uintptr_t state,unsigned& value) noexcept {
    __try {value=*reinterpret_cast<const volatile unsigned*>(state+0x514);return true;}
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
#if HALOMCCVR_DEV_DIAGNOSTICS
        // SEATEXIT witness: the proven player-control builder reading the Use (3) or Reload consumer record --
        // the game's own view of that button, whatever fed it. Read-only, SEH-guarded, dev builds only.
        if(runtime.admitted.load(std::memory_order_acquire) && caller>=runtime.base+consumerBegin &&
           caller<runtime.base+consumerEnd && reinterpret_cast<uintptr_t>(state)==runtime.state &&
           (action==3u || action==native_action_button::ConsumerAction(title,vr_mapping::Reload)))
            WitnessNativeRecord(title,action,native);
#endif
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
        // These semantic aliases drive dual pickup regardless of controller layout.
        // Suppress them only while the support hand is actually occupied.
        const int secondary=SecondaryReloadRecord(action,caller-runtime.base);
        if(secondary>=0 && runtime.secondaryReloadProved.load(std::memory_order_acquire) &&
           Game_BlockHalo3SupportPickup()) {
            // Return a released copy without modifying native input storage.
            // Manual reload alone must continue to permit dual pickup.
            auto& released=local.secondaryReleased[secondary];released.Reset(title,action);
            runtime.routed.fetch_add(1,std::memory_order_relaxed);
            return released.bytes.data();
        }
        const int semantic=native_action_button::SemanticAction(title,action);
        if(semantic<0) return native;
        auto& record=local.records[semantic];
        if(local.blocked&(1u<<semantic)) {
            runtime.routed.fetch_add(1,std::memory_order_relaxed);
            return record.bytes.data();
        }
        if(!local.direct) return native;
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
void* __fastcall ReaderHook(void* state,unsigned action,uintptr_t caller) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    void* result=nullptr;
    __try {
        result=reinterpret_cast<Read>(runtime.original[1])(state,action);
        result=ReadOwned(state,action,result,caller);
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
    return result;
}
// Preserve held/long-held aliases for dual pickup. Only the derived reload
// edge is removed, after native processing, for the exact local player.
uintptr_t __fastcall ActionEdgesHook(uint32_t player,void* packet,uintptr_t caller) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uintptr_t result=0;
    __try {
        result=reinterpret_cast<uintptr_t(__fastcall*)(uint32_t,void*)>(runtime.original[2])(player,packet);
        __try {
            uint32_t actions=0,blocked=0;bool direct=false;
            if(Current() && runtime.secondaryEdgesReady.load(std::memory_order_acquire) &&
               caller==runtime.base+0xE4CE4 && player!=UINT32_MAX && packet &&
               ReadPublication(GetTickCount64(),actions,direct,nullptr,title,runtime.generation.load(),&blocked) &&
               (blocked&(1u<<vr_mapping::Reload)) &&
               runtime.localPlayer && runtime.localPlayer(0)==player) {
                auto* flags=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(packet)+0x40);
                *flags &= ~uint32_t{0x40};
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            runtime.secondaryEdgesReady.store(false,std::memory_order_release);
        }
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_release);}
    return result;
}
// The reader is entered through a register-preserving asm thunk (native_vr_reader_thunks.asm).
void* const hooks[]{reinterpret_cast<void*>(&ConverterHook),reinterpret_cast<void*>(&NativeVrReaderThunkHalo3),reinterpret_cast<void*>(&NativeVrH3ActionEdgesThunk)};
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
    for(unsigned i=0;i<3;++i) if(runtime.enabled[i]) {
        const auto status=MCCVR_DisableHookForRetirement(runtime.target[i]);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED) return false;
        runtime.enabled[i]=false;
    }
    const void* functions[]{hooks[0],hooks[1],reinterpret_cast<void*>(&ReaderHook),reinterpret_cast<void*>(&ReadOwned),
        reinterpret_cast<void*>(&Current),reinterpret_cast<void*>(&ReadController),reinterpret_cast<void*>(&ReadFrames),
        hooks[2],reinterpret_cast<void*>(&ActionEdgesHook)};
    const void* trampolines[]{runtime.original[0],runtime.original[1],nullptr,nullptr,nullptr,nullptr,nullptr,runtime.original[2],nullptr};
    if(!WaitForNativeDetourQuiescence(functions,trampolines,9,runtime.callbacks)) return false;
    for(unsigned i=0;i<3;++i) if(runtime.target[i]) {
        if(MH_RemoveHook(runtime.target[i])!=MH_OK) return false;
        runtime.target[i]=runtime.original[i]=nullptr;
    }
    if(runtime.module) FreeLibrary(runtime.module);
    runtime.module=nullptr;runtime.base=runtime.state=0;runtime.generation=0;
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
        LOG("Native VR action bridge H3: retired faulted optional hooks; transport routing retained");
    }
    // THE REFERENCE ON halo3.dll IS HELD ONLY WHILE THE HALO 3 CORE IS INSTALLED (Game_IsHooked). The core holds none:
    // it retires its hooks at a level's end while MCC still owns the DLL, so MCC's unload and reload for its menus
    // gives the next game a fresh image. Held until the title or generation changed, this reference kept the old
    // image instead, and a second match in one session started in a module whose shutdown had already run (it never
    // loaded). The core also retires the bridge at once (NativeVrActions_Halo3CoreRetired).
    const bool coreIn=Game_IsHooked();
    if(runtime.module && (active!=title || generation!=runtime.generation || !coreIn))
        if(!Retire()) return;
    if(active!=title || !generation || !coreIn || generation==blockedGeneration || !(g_config.vr_action_mapping||Config_GestureOnlyWeaponInput()||g_config.two_handed_aim)) return;
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
            Prove(base,size,controllerProofRva,controllerProofPattern) && ProveConsumerBounds(base);
        runtime.secondaryReloadProved.store(proved && Prove(base,size,secondaryReloadRva,secondaryReloadPattern),std::memory_order_release);
        if(proved && !runtime.secondaryReloadProved.load())
            LOG("H3 secondary manual reload suppression: StockFallback; consumer alias proof failed");
        if(proved) {
            runtime.state=sig::RipTarget(base+d.stateLoadRva+10,base+d.stateLoadRva+14);
            proved=runtime.state>=base && runtime.state-base<size && size-(runtime.state-base)>=4*d.stride;

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
        bool edgeReady=proved && Prove(base,size,actionEdgesRva,actionEdgesPattern) &&
            Prove(base,size,localPlayerRva,localPlayerPattern) &&
            Prove(base,size,0xE4CD7,edgeCallerPattern) && Prove(base,size,0xE7D8B,edgeFlagsPattern);
        runtime.localPlayer=edgeReady?reinterpret_cast<uint32_t(__fastcall*)(int)>(base+localPlayerRva):nullptr;
        if(edgeReady) {
            auto* target=reinterpret_cast<void*>(base+actionEdgesRva);
            edgeReady=MH_CreateHook(target,hooks[2],&runtime.original[2])==MH_OK;
            if(edgeReady) {
                runtime.target[2]=target;
                edgeReady=MH_EnableHook(target)==MH_OK;
                runtime.enabled[2]=edgeReady;
            }
        }
        runtime.secondaryEdgesReady.store(edgeReady,std::memory_order_release);
        LOG("H3 secondary reload edge suppression: %s; dual pickup input preserved",
            edgeReady?"installed":"StockFallback (proof/hook failure)");
        runtime.readerAt.store(0,std::memory_order_release);
        runtime.admitted.store(proved,std::memory_order_release);
        LOG("Native VR action bridge H3: %s; requires observed owned input cadence; runtime validation pending",
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
        LOG("Native VR action bridge H3: converter=%llu directReads=%llu secondaryReload=%s%s",
            runtime.converted.load(),runtime.routed.load(),runtime.secondaryEdgesReady.load()?"edge-only":"stock-fallback",runtime.faulted.load()?" TRANSPORT FALLBACK (guarded access fault)":"");
    }
}

} // namespace native_h3
