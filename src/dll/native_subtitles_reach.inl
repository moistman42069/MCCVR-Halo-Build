// Included after ReachThreadFreeze in game.cpp's private namespace. Optional
// final-call observers: neither their installation nor cleanup arms/disarms VR.
// HREK explains the localized line/lifetime semantics; donor signatures were
// independently rechecked against the pinned retail image (see evidence doc).
constexpr size_t kNativeReachSubtitleRelayCapacity = 512;
constexpr uint8_t kNativeReachSubtitleOriginalCall[]{0xFF,0x90,0xC8,0x02,0,0};
constexpr char kNativeReachSubtitleGameplayPattern[] =
    "48 8B 03 48 8D 4C 24 60 44 89 74 24 30 4C 8D 44 24 40 F3 0F 11 74 24 28 "
    "48 8D 54 24 50 48 89 4C 24 20 45 33 C9 48 8B CB FF 90 C8 02 00 00";
constexpr char kNativeReachSubtitleRendererPattern[] =
    "40 55 41 54 41 55 41 56 41 57 48 8D AC 24 D0 EB FF FF B8 30 15 00 00 "
    "E8 ?? ?? ?? ?? 48 2B E0 48 C7 44 24 58 FE FF FF FF";
constexpr char kNativeReachSubtitleTheatrePattern[] =
    "48 8B 01 C7 44 24 30 02 00 00 00 F3 0F 11 74 24 28 48 8D 55 90 "
    "48 89 54 24 20 4C 8D 4C 24 60 4C 8D 44 24 78 48 8D 54 24 68 FF 90 C8 02 00 00";
constexpr char kNativeReachSubtitleResolverCall[] =
    "4C 8D 85 F0 09 00 00 8B 8E 3C 06 00 00 E8 ?? ?? ?? ?? BF 00 04 00 00";
constexpr char kNativeReachSubtitleResolver[] =
    "48 89 5C 24 20 55 56 57 41 56 41 57 B8 40 10 00 00 E8 ?? ?? ?? ?? "
    "48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 30 10 00 00 49 8B F0 8B F9 8B EA";

struct NativeReachSubtitleCall {
    uint8_t* target=nullptr;
    uint8_t* relay=nullptr;
    size_t relaySize=0;
    uint8_t patch[6]{};
    bool patched=false;
};
struct NativeReachSubtitleRuntime {
    std::atomic<int32_t> activeCallbacks{0};
    std::atomic<uint32_t> generation{0};
    NativeReachSubtitleCall calls[2]{};
    uintptr_t base=0;
    HMODULE moduleReference=nullptr;
    bool cleanup=false;
    uint32_t attemptedGeneration=0;
    uint64_t retryAfterMs=0;
    unsigned cleanupAttempts=0;
} g_nativeReachSubtitles;
static_assert(std::atomic<int32_t>::is_always_lock_free);

void __fastcall NativeReachSubtitleGameplayCapture(uint32_t, uint32_t,
    const wchar_t* text,uint32_t durationBits,uint32_t) noexcept {
    const uint32_t generation=g_nativeReachSubtitles.generation.load(std::memory_order_acquire);
    if (!generation || VR_IsCutsceneTheaterActive() ||
        g_reachCinematicLocked.load(std::memory_order_relaxed) ||
        TitleAdapter_GetActiveTitle()!=GameTitle::HaloReach ||
        TitleAdapter_GetGeneration(GameTitle::HaloReach)!=generation) return;
    float seconds=0;memcpy(&seconds,&durationBits,sizeof(seconds));
    NativeSubtitles_Capture(GameTitle::HaloReach,generation,text,seconds,false);
}
void __fastcall NativeReachSubtitleTheatreCapture(uint32_t, uint32_t,
    const wchar_t* text,uint32_t durationBits,uint32_t) noexcept {
    const uint32_t generation=g_nativeReachSubtitles.generation.load(std::memory_order_acquire);
    if (!generation || !VR_IsCutsceneTheaterActive() ||
        !g_reachCinematicLocked.load(std::memory_order_relaxed) ||
        TitleAdapter_GetActiveTitle()!=GameTitle::HaloReach ||
        TitleAdapter_GetGeneration(GameTitle::HaloReach)!=generation) return;
    float seconds=0;memcpy(&seconds,&durationBits,sizeof(seconds));
    NativeSubtitles_Capture(GameTitle::HaloReach,generation,text,seconds,true);
}
void NativeReachSubtitleEmit(uint8_t*& cursor,const void* bytes,size_t size) {
    memcpy(cursor,bytes,size);cursor+=size;
}
template<typename T> void NativeReachSubtitleEmitValue(uint8_t*& cursor,const T& value) {
    NativeReachSubtitleEmit(cursor,&value,sizeof(value));
}

uintptr_t NativeReachSubtitleUnique(uintptr_t base,size_t size,const char* pattern) {
    const uintptr_t found=sig::Find(base,size,pattern);
    return found && !sig::Find(found+1,base+size-found-1,pattern)?found:0;
}
bool NativeReachSubtitleWrite(void* to,const void* from,size_t size) noexcept {
    DWORD protection=0;
    if(!VirtualProtect(to,size,PAGE_EXECUTE_READWRITE,&protection)) return false;
    memcpy(to,from,size);FlushInstructionCache(GetCurrentProcess(),to,size);
    DWORD ignored=0;
    return VirtualProtect(to,size,protection,&ignored)!=0;
}
bool NativeReachSubtitleQuiescent(ReachThreadFreeze& frozen) noexcept {
    if(g_nativeReachSubtitles.activeCallbacks.load(std::memory_order_acquire)) return false;
    for(const auto& thread:frozen.Threads()) {
        if(!thread.suspended) continue;
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(thread.handle,&context)) {
            if(WaitForSingleObject(thread.handle,0)!=WAIT_OBJECT_0) return false;
            continue;
        }
        for(const auto& call:g_nativeReachSubtitles.calls) {
            const auto target=reinterpret_cast<uintptr_t>(call.target);
            const auto relay=reinterpret_cast<uintptr_t>(call.relay);
            if((target && context.Rip>=target && context.Rip<target+6) ||
               (relay && context.Rip>=relay && context.Rip<relay+call.relaySize)) return false;
        }
    }
    return true;
}
bool NativeReachSubtitleCleanup() {
    auto& state=g_nativeReachSubtitles;
    state.generation.store(0,std::memory_order_release);
    if(!state.cleanup) return true;
    ReachThreadFreeze frozen;
    if(!frozen.Capture() || !NativeReachSubtitleQuiescent(frozen)) return false;
    for(auto& call:state.calls) {
        if(!call.patched) continue;
        if(memcmp(call.target,call.patch,sizeof(call.patch)) ||
            !NativeReachSubtitleWrite(call.target,kNativeReachSubtitleOriginalCall,6)) return false;
        call.patched=false;
    }
    if(!frozen.Release()) return false;
    for(auto& call:state.calls) {
        if(call.relay) VirtualFree(call.relay,0,MEM_RELEASE);
        call={};
    }
    if(state.moduleReference) FreeLibrary(state.moduleReference);
    state.moduleReference=nullptr;state.base=0;state.cleanup=false;
    state.retryAfterMs=0;state.cleanupAttempts=0;
    LOG("Native subtitles: Reach final-call observers retired after verified quiescence; native calls restored");
    return true;
}

    static bool NativeReachSubtitleRel32(
        uintptr_t instructionNext, uintptr_t destination,
        int32_t& displacement) noexcept
    {
        const int64_t difference = static_cast<int64_t>(destination) -
            static_cast<int64_t>(instructionNext);
        if (difference < INT32_MIN || difference > INT32_MAX)
            return false;
        displacement = static_cast<int32_t>(difference);
        return true;
    }

    static void* NativeReachSubtitleAllocateNear(
        uintptr_t target, size_t bytes) noexcept
    {
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        const uintptr_t granularity = systemInfo.dwAllocationGranularity;
        if (!granularity || (granularity & (granularity - 1)) != 0)
            return nullptr;

        void* allocation = VirtualAlloc(
            nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        int32_t displacement = 0;
        if (allocation && NativeReachSubtitleRel32(
                target + 5, reinterpret_cast<uintptr_t>(allocation),
                displacement))
            return allocation;
        if (allocation)
            VirtualFree(allocation, 0, MEM_RELEASE);

        constexpr uintptr_t reach = 0x7FFF0000u;
        const uintptr_t minimum = target > reach ? target - reach : granularity;
        const uintptr_t maximum = target <= UINTPTR_MAX - reach
            ? target + reach : UINTPTR_MAX;
        uintptr_t cursor = (minimum + granularity - 1) &
            ~(granularity - 1);
        for (uint32_t query = 0; cursor < maximum && query < 0x10000;
             ++query)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<const void*>(cursor),
                             &info, sizeof(info)) != sizeof(info))
                break;
            const uintptr_t regionBase =
                reinterpret_cast<uintptr_t>(info.BaseAddress);
            const uintptr_t regionEnd = regionBase <=
                    UINTPTR_MAX - info.RegionSize
                ? regionBase + info.RegionSize : UINTPTR_MAX;
            if (info.State == MEM_FREE)
            {
                uintptr_t candidate = regionBase < cursor ? cursor : regionBase;
                candidate = (candidate + granularity - 1) &
                    ~(granularity - 1);
                if (candidate < maximum && bytes <= maximum - candidate &&
                    bytes <= regionEnd - candidate)
                {
                    allocation = VirtualAlloc(
                        reinterpret_cast<void*>(candidate), bytes,
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                    if (allocation && NativeReachSubtitleRel32(
                            target + 5,
                            reinterpret_cast<uintptr_t>(allocation),
                            displacement))
                        return allocation;
                    if (allocation)
                        VirtualFree(allocation, 0, MEM_RELEASE);
                }
            }
            if (regionEnd <= cursor)
                break;
            cursor = regionEnd;
        }
        return nullptr;
    }

    static uint8_t* NativeReachSubtitleBuildRelay(
        uintptr_t callsite, size_t& relaySize, uintptr_t capture) noexcept
    {
        uint8_t* const code = reinterpret_cast<uint8_t*>(
            NativeReachSubtitleAllocateNear(
                callsite, kNativeReachSubtitleRelayCapacity));
        if (!code)
            return nullptr;
        uint8_t* cursor = code;

        // Discard the temporary return address from the replacement near CALL.
        // RSP is now exactly the stock pre-call RSP again; LEA preserves flags.
        const uint8_t discardRelayReturn[] = {0x48,0x8D,0x64,0x24,0x08};
        NativeReachSubtitleEmit(
            cursor, discardRelayReturn, sizeof(discardRelayReturn));

        // Sixteen pushes (flags + all GPRs) preserve the complete staged stock
        // call state and keep RSP 16-byte aligned for the C++ capture callback.
        const uint8_t saveGprs[] = {
            0x9C,
            0x50,0x51,0x52,0x53,0x55,0x56,0x57,
            0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
            0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        };
        NativeReachSubtitleEmit(cursor, saveGprs, sizeof(saveGprs));
        const uint8_t reserve[] = {
            0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,
        };
        NativeReachSubtitleEmit(cursor, reserve, sizeof(reserve));
        const uint8_t saveXmm[][6] = {
            {0xF3,0x0F,0x7F,0x44,0x24,0x30},
            {0xF3,0x0F,0x7F,0x4C,0x24,0x40},
            {0xF3,0x0F,0x7F,0x54,0x24,0x50},
            {0xF3,0x0F,0x7F,0x5C,0x24,0x60},
            {0xF3,0x0F,0x7F,0x64,0x24,0x70},
        };
        for (const auto& instruction : saveXmm)
            NativeReachSubtitleEmit(cursor, instruction, sizeof(instruction));
        const uint8_t saveXmm5[] = {
            0xF3,0x0F,0x7F,0xAC,0x24,0x80,0x00,0x00,0x00,
        };
        NativeReachSubtitleEmit(cursor, saveXmm5, sizeof(saveXmm5));

        const uint8_t movRax[] = {0x48,0xB8};
        NativeReachSubtitleEmit(cursor, movRax, sizeof(movRax));
        const uintptr_t counter = reinterpret_cast<uintptr_t>(
            &g_nativeReachSubtitles.activeCallbacks);
        NativeReachSubtitleEmitValue(cursor, counter);
        const uint8_t increment[] = {0xF0,0xFF,0x00};
        NativeReachSubtitleEmit(cursor, increment, sizeof(increment));

        // Original S is current RSP+0x120. The proven final handoff stages the
        // UTF-16 pointer at S+0x20, duration at S+0x28, and class at S+0x30.
        // EDI and ESI retain sound tag and speaker designation respectively.
        const uint8_t arguments[] = {
            0x8B,0xCF,
            0x8B,0xD6,
            0x4C,0x8B,0x84,0x24,0x40,0x01,0x00,0x00,
            0x44,0x8B,0x8C,0x24,0x48,0x01,0x00,0x00,
            0x8B,0x84,0x24,0x50,0x01,0x00,0x00,
            0x89,0x44,0x24,0x20,
        };
        NativeReachSubtitleEmit(cursor, arguments, sizeof(arguments));
        NativeReachSubtitleEmit(cursor, movRax, sizeof(movRax));
        NativeReachSubtitleEmitValue(cursor, capture);
        const uint8_t callRax[] = {0xFF,0xD0};
        NativeReachSubtitleEmit(cursor, callRax, sizeof(callRax));

        const uint8_t loadXmm[][6] = {
            {0xF3,0x0F,0x6F,0x44,0x24,0x30},
            {0xF3,0x0F,0x6F,0x4C,0x24,0x40},
            {0xF3,0x0F,0x6F,0x54,0x24,0x50},
            {0xF3,0x0F,0x6F,0x5C,0x24,0x60},
            {0xF3,0x0F,0x6F,0x64,0x24,0x70},
        };
        for (const auto& instruction : loadXmm)
            NativeReachSubtitleEmit(cursor, instruction, sizeof(instruction));
        const uint8_t loadXmm5[] = {
            0xF3,0x0F,0x6F,0xAC,0x24,0x80,0x00,0x00,0x00,
        };
        NativeReachSubtitleEmit(cursor, loadXmm5, sizeof(loadXmm5));
        const uint8_t release[] = {
            0x48,0x81,0xC4,0xA0,0x00,0x00,0x00,
        };
        NativeReachSubtitleEmit(cursor, release, sizeof(release));
        const uint8_t restoreGprs[] = {
            0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,
            0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58,
            0x5F,0x5E,0x5D,0x5B,0x5A,0x59,0x58,0x9D,
        };
        NativeReachSubtitleEmit(cursor, restoreGprs, sizeof(restoreGprs));

        // The original instruction executes exactly once, with original RSP,
        // registers, stack arguments, and vtable in RAX.
        NativeReachSubtitleEmit(
            cursor, kNativeReachSubtitleOriginalCall,
            sizeof(kNativeReachSubtitleOriginalCall));

        // Preserve both possible return registers and post-call flags while
        // retiring the callback lease, then resume at stock callsite+6.
        const uint8_t preserveResult[] = {
            0x9C,0x50,
            0x48,0x83,0xEC,0x10,
            0xF3,0x0F,0x7F,0x04,0x24,
        };
        NativeReachSubtitleEmit(cursor, preserveResult, sizeof(preserveResult));
        NativeReachSubtitleEmit(cursor, movRax, sizeof(movRax));
        NativeReachSubtitleEmitValue(cursor, counter);
        const uint8_t decrement[] = {0xF0,0xFF,0x08};
        NativeReachSubtitleEmit(cursor, decrement, sizeof(decrement));
        const uint8_t restoreResult[] = {
            0xF3,0x0F,0x6F,0x04,0x24,
            0x48,0x83,0xC4,0x10,
            0x58,0x9D,
        };
        NativeReachSubtitleEmit(cursor, restoreResult, sizeof(restoreResult));
        const uint8_t jump[] = {0xFF,0x25,0x00,0x00,0x00,0x00};
        NativeReachSubtitleEmit(cursor, jump, sizeof(jump));
        const uintptr_t resume = callsite +
            sizeof(kNativeReachSubtitleOriginalCall);
        NativeReachSubtitleEmitValue(cursor, resume);

        relaySize = static_cast<size_t>(cursor - code);
        if (relaySize > kNativeReachSubtitleRelayCapacity)
        {
            VirtualFree(code, 0, MEM_RELEASE);
            relaySize = 0;
            return nullptr;
        }
        return code;
    }


void NativeReachSubtitles_Poll(uintptr_t base,size_t size,uint32_t generation,bool levelRunning) {
    auto& state=g_nativeReachSubtitles;
    const bool eligible=levelRunning && base && generation &&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloReach &&
        TitleAdapter_GetGeneration(GameTitle::HaloReach)==generation &&
        g_reachCamera.armed.load(std::memory_order_acquire) &&
        !g_reachCamera.teardownRequested.load(std::memory_order_acquire);
    const uint32_t installedGeneration=state.generation.load(std::memory_order_acquire);
    if(state.moduleReference && (!eligible || state.base!=base || installedGeneration!=generation))
        state.cleanup=true;
    if(state.cleanup) {
        const uint64_t now=GetTickCount64();
        if(now<state.retryAfterMs) return;
        if(!NativeReachSubtitleCleanup()) {
            ++state.cleanupAttempts;
            const unsigned delay=state.cleanupAttempts<4?50u*(1u<<state.cleanupAttempts):1000u;
            state.retryAfterMs=now+delay;
            if(state.cleanupAttempts==1 || state.cleanupAttempts%30==0)
                LOG("Native subtitles: Reach observer cleanup awaiting callback/relay quiescence; optional feature only");
        }
        return;
    }
    if(!eligible) {state.attemptedGeneration=0;return;}
    if(installedGeneration==generation || state.attemptedGeneration==generation) return;
    const ReachModuleEpoch epoch{base,generation};
    if(!ReachRenderCandidate_IsPreflightCurrent(ReachRenderCandidate_GetPreflight(epoch))) return;
    state.attemptedGeneration=generation;
    // The camera's accepted preflight validates both edition hashes. Apply the
    // exact unique text/consumer proofs as well; RVAs below are comparisons,
    // never discovery or copied binding addresses.
    HMODULE reference=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(base),&reference) || reinterpret_cast<uintptr_t>(reference)!=base) {
        if(reference) FreeLibrary(reference);
        LOG("Native subtitles: Reach module retention failed; stock text retained");return;
    }
    const uintptr_t gameplay=NativeReachSubtitleUnique(base,size,kNativeReachSubtitleGameplayPattern);
    const uintptr_t renderer=NativeReachSubtitleUnique(base,size,kNativeReachSubtitleRendererPattern);
    const uintptr_t theatre=NativeReachSubtitleUnique(base,size,kNativeReachSubtitleTheatrePattern);
    const uintptr_t resolveCall=NativeReachSubtitleUnique(base,size,kNativeReachSubtitleResolverCall);
    const uintptr_t resolver=NativeReachSubtitleUnique(base,size,kNativeReachSubtitleResolver);
    const bool gameplayProof=gameplay && gameplay-base==0x711335;
    const bool theatreProof=renderer && theatre && resolveCall && resolver &&
        theatre==renderer+0x642 && resolveCall==renderer+0x36F &&
        sig::RipTarget(resolveCall+0x0E,resolveCall+0x12)==resolver;
    if(!gameplayProof && !theatreProof) {
        FreeLibrary(reference);
        LOG("Native subtitles: Reach gameplay/theatre final-call proofs unavailable; stock retained");return;
    }
    state.base=base;state.moduleReference=reference;
    const uintptr_t targets[2]{gameplayProof?gameplay+0x28:0,theatreProof?theatre+0x29:0};
    const uintptr_t captures[2]{reinterpret_cast<uintptr_t>(&NativeReachSubtitleGameplayCapture),
        reinterpret_cast<uintptr_t>(&NativeReachSubtitleTheatreCapture)};
    bool prepared=true;
    for(unsigned i=0;i<2;++i) {
        if(!targets[i]) continue;
        auto& call=state.calls[i];call.target=reinterpret_cast<uint8_t*>(targets[i]);
        DWORD64 imageBase=0;
        const auto function=RtlLookupFunctionEntry(targets[i],&imageBase,nullptr);
        if(!function || targets[i]+6>imageBase+function->EndAddress ||
            memcmp(call.target,kNativeReachSubtitleOriginalCall,6)) {prepared=false;break;}
        call.relay=NativeReachSubtitleBuildRelay(targets[i],call.relaySize,captures[i]);
        DWORD protection=0;int32_t displacement=0;
        if(!call.relay || !VirtualProtect(call.relay,call.relaySize,PAGE_EXECUTE_READ,&protection) ||
            !NativeReachSubtitleRel32(targets[i]+5,reinterpret_cast<uintptr_t>(call.relay),displacement)) {
            prepared=false;break;
        }
        FlushInstructionCache(GetCurrentProcess(),call.relay,call.relaySize);
        call.patch[0]=0xE8;memcpy(call.patch+1,&displacement,4);call.patch[5]=0x90;
    }
    if(prepared) {
        ReachThreadFreeze frozen;
        prepared=frozen.Capture() && NativeReachSubtitleQuiescent(frozen) &&
            TitleAdapter_GetActiveTitle()==GameTitle::HaloReach &&
            TitleAdapter_GetGeneration(GameTitle::HaloReach)==generation;
        if(prepared) for(auto& call:state.calls) {
            if(!call.target) continue;
            if(memcmp(call.target,kNativeReachSubtitleOriginalCall,6)) {prepared=false;break;}
            const bool written=NativeReachSubtitleWrite(call.target,call.patch,6);
            // A failed protection-restore can follow a successful byte write.
            // Retain every executable resource until guarded restoration.
            call.patched=memcmp(call.target,call.patch,6)==0;
            if(!written || !call.patched) {prepared=false;break;}
        }
        if(!frozen.Release()) prepared=false;
    }
    if(!prepared) {
        state.cleanup=true;
        LOG("Native subtitles: Reach optional observer installation incomplete; guarded cleanup queued; camera unchanged");
        return;
    }
    state.generation.store(generation,std::memory_order_release);
    LOG("Native subtitles: Reach localized final-call observers ready (generation=%u gameplay=%d theatre=%d); native calls preserved",
        generation,gameplayProof?1:0,theatreProof?1:0);
}
