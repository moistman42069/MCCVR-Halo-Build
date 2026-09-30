// H4EK-owned native contact path. Included after Halo 4's physics types/state.
// Binding and layout evidence: CONTACT-RESUME-CHECKPOINT.md.
#include "../common/halo4_contact_world.h"
using Halo4ContactUpdateFn=void(__fastcall*)(void*);
using Halo4ContactBuildFn=void(__fastcall*)(uint32_t,int32_t,uint32_t*);
using Halo4ContactConsumeFn=void(__fastcall*)(uint32_t,uint64_t,int16_t,float,uint8_t,uint32_t*,void*);
using Halo4ContactDamageFn=void(__fastcall*)(uint32_t,int32_t,const void*,const void*,const float*);
using Halo4ContactObjectFn=const uint8_t*(__fastcall*)(uint32_t,uint32_t);
struct Halo4ContactRuntime
{
    std::atomic<bool> enabled{false},faulted{false},processing{false};
    std::atomic<uint32_t> callbacks{0},owner{UINT32_MAX};
    std::atomic<uint64_t> queries{0},contacts{0},submitted[2]{},rejected{0},overflow{0},predicted{0};
    uintptr_t base=0;
    uint32_t generation=0;
    void* updateTarget=nullptr;
    void* damageTarget=nullptr;
    Halo4ContactUpdateFn updateOriginal=nullptr;
    Halo4ContactDamageFn damageOriginal=nullptr;
    Halo4ContactBuildFn build=nullptr;
    Halo4ContactConsumeFn consume=nullptr;
    Halo4ContactObjectFn object=nullptr;
    uint32_t(__fastcall* playbackMode)()=nullptr;
    uint32_t(__fastcall* simulationMode)()=nullptr;
    const uint64_t* flags=nullptr;
    ContactMeleeQueue queue[2];
    contact_melee::Hand hands[2];
} g_halo4Contact;

CinematicControlState ReadHalo4CinematicControl() noexcept;

bool Halo4ContactMeleeReady()
{
    return g_halo4Contact.enabled.load(std::memory_order_acquire) &&
        !g_halo4Contact.faulted.load(std::memory_order_acquire);
}

const uint8_t* Halo4ContactObject(uint32_t handle,bool requireBiped=false)
{
    if(handle==UINT32_MAX || !(handle>>16) || !g_halo4Contact.object ||
        !g_halo4EngineTlsIndex || *g_halo4EngineTlsIndex>=256) return nullptr;
    auto** slots=reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
    const auto* tls=slots ? slots[*g_halo4EngineTlsIndex] : nullptr;
    const auto* table=tls ? *reinterpret_cast<const uint8_t* const*>(tls+0x18) : nullptr;
    // H4EK datum_get: stride +20, valid +31, extent +44, entries +50.
    // Retail 43AD8 checks the full signed salt before 5DA400 checks kind +4.
    if(!table || !table[0x31] || *reinterpret_cast<const uint64_t*>(table+0x20)!=0x18 ||
        !*reinterpret_cast<const void* const*>(table+0x50)) return nullptr;
    const int32_t count=*reinterpret_cast<const int32_t*>(table+0x44);
    if(count<=0 || count>0x10000 || (handle&0xFFFF)>=static_cast<uint32_t>(count)) return nullptr;
    // The verified native accessor applies this type mask after its own full
    // salt check. Targets may be any object; only the attacker must be biped.
    return g_halo4Contact.object(handle,requireBiped ? 1u : UINT32_MAX);
}

const uint8_t* Halo4ContactBiped(uint32_t handle) { return Halo4ContactObject(handle,true); }

#include "halo4_contact_melee_backend.inl"

void Halo4ContactTick(uint32_t unit,void* instance)
{
    if(!Halo4ContactMeleeReady() || !instance ||
        TitleAdapter_GetActiveTitle()!=GameTitle::Halo4) return;
    bool expected=false;
    if(!g_halo4Contact.processing.compare_exchange_strong(expected,true,std::memory_order_acquire)) return;
    __try
    {
        const auto* biped=Halo4ContactBiped(unit);
        if(unit==g_halo4Contact.owner.load(std::memory_order_acquire) && biped==instance)
        {
            const uint64_t now=GetTickCount64();
            const bool admit=!exclusive_input::Active() && g_config.physical_melee && g_enabled.load() && VR_IsStereoEnabled() &&
                g_halo4Camera.armed.load() && !g_halo4Camera.teardownRequested.load() &&
                g_halo4Contact.generation==g_halo4Camera.generation.load() &&
                g_halo4Contact.playbackMode && g_halo4Contact.playbackMode()==0 &&
                ReadHalo4CinematicControl()==CinematicControlState::PlayerControlled &&
                *reinterpret_cast<const int32_t*>(biped+0x24)==-1;
            for(int hand=0;hand<2;++hand)
            {
                ContactMeleePacket packet{};
                for(unsigned n=0;n<8 && g_halo4Contact.queue[hand].Pop(packet);++n)
                {
                    if(!admit || packet.frame.unit!=unit || packet.generation!=g_halo4Contact.generation ||
                        packet.publishedAtMs>now || now-packet.publishedAtMs>100)
                    { g_halo4Contact.hands[hand].Reset(); continue; }
                    Halo4ContactBackend backend{};
                    backend.owner=unit;
                    const auto result=g_halo4Contact.hands[hand].Process(packet.frame,
                        std::clamp(g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax),backend);
                    if(result==contact_melee::ContactResult::Applied)
                    { g_halo4Contact.submitted[hand].fetch_add(1,std::memory_order_relaxed); VR_PulseContactHaptics(hand==0,0.65f); }
                    else if(result==contact_melee::ContactResult::NativeRejected)
                        g_halo4Contact.rejected.fetch_add(1,std::memory_order_relaxed);
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_halo4ContactScope.active=false;
        g_halo4Contact.faulted.store(true,std::memory_order_release);
    }
    g_halo4Contact.processing.store(false,std::memory_order_release);
}

__declspec(noinline) void __fastcall Halo4ContactUpdateDetour(void* instance)
{
    g_halo4Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        uint32_t unit=UINT32_MAX;
        // The native update can destroy/reparent its instance. Capture its
        // handle while it is live; afterwards revalidate by full handle.
        __try
        {
            if(instance && Halo4ContactMeleeReady())
                unit=*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(instance)-0xC);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) { unit=UINT32_MAX; }
        if(g_halo4Contact.updateOriginal) g_halo4Contact.updateOriginal(instance);
        if(unit!=UINT32_MAX) Halo4ContactTick(unit,instance);
    }
    __finally { g_halo4Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

bool RemoveHalo4ContactMelee()
{
    g_halo4ContactBsp.enabled.store(false,std::memory_order_release);
    g_halo4Contact.enabled.store(false,std::memory_order_release);
    if(!g_halo4Contact.updateTarget && !g_halo4Contact.damageTarget) return true;
    void** targets[]{&g_halo4Contact.updateTarget,&g_halo4Contact.damageTarget};
    for(auto target:targets) if(*target)
    {
        const auto status=MH_DisableHook(*target);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED && status!=MH_ERROR_NOT_CREATED) return false;
    }
    const void* functions[]{reinterpret_cast<void*>(&Halo4ContactUpdateDetour),
        reinterpret_cast<void*>(&Halo4ContactDamageDetour),reinterpret_cast<void*>(&Halo4ContactTick)};
    const void* trampolines[]{reinterpret_cast<void*>(g_halo4Contact.updateOriginal),
        reinterpret_cast<void*>(g_halo4Contact.damageOriginal),nullptr};
    if(!WaitForNativeDetourQuiescence(functions,trampolines,3,g_halo4Contact.callbacks)) return false;
    for(auto target:targets) if(*target)
    {
        const auto status=MH_RemoveHook(*target);
        if(status!=MH_OK && status!=MH_ERROR_NOT_CREATED) return false;
        *target=nullptr;
    }
    g_halo4Contact.updateOriginal=nullptr;
    g_halo4Contact.damageOriginal=nullptr;
    g_halo4Contact.owner.store(UINT32_MAX);
    return true;
}

bool Halo4ContactVerifyCall(uintptr_t base,uintptr_t caller,uintptr_t target)
{
    const auto* instruction=reinterpret_cast<const uint8_t*>(base+caller);
    return instruction[0]==0xE8 && base+caller+5+
        *reinterpret_cast<const int32_t*>(instruction+1)==base+target;
}

bool InstallHalo4ContactBsp(uintptr_t base,size_t size)
{
    g_halo4ContactBsp.enabled.store(false,std::memory_order_release);
    g_halo4ContactBsp.faulted.store(false,std::memory_order_release);
    struct Binding {uintptr_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x21F7B4,"40 53 48 83 EC 20 48 8B 05 6F 82 74 04 4C 8D 15 B8 A9 74 04 48 8B D9 44 8B 80 A8 00 00 00 48 63 C2 48 6B D0 54 49 03 D0 49 C1 E8 1C 4B 8B 04 C2"},
        {0x2DE9FC,"40 55 48 8B EC 48 83 EC 30 F3 0F 10 6D 50 0F 57 E4 0F 29 74 24 20 4C 8B D1 0F 28 F5 0F 29 7C 24 10 F3 0F 5C F4 0F 2F F4 72 05 0F 28 FD EB 03 0F"},
        {0x2DEF10,"48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 0F 29 70 C8 4C 8D 15 42 B2 68 04 8B C2"},
        {0x2DF698,"48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 33 FF 0F 29 70 C8 4C 8D 15 B8 AA 68 04"},
        {0x2DEC78,"48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC A0 00 00 00 4C 8B 7D 7F 41 8B F1 0F 29 70 C8 8B FA 4C 8B F1 41 BA 20 00 00 00 49 8B 86 E8 00 00 00 4C 8D 0D C1 B4 68 04 48 63 CF 48 C1 E1 05 8B 50"},
        {0x2DF400,"48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC A0 00 00 00 4C 8B 7D 7F 41 8B F1 0F 29 70 C8 8B FA 4C 8B F1 41 BA 20 00 00 00 49 8B 86 E8 00 00 00 4C 8D 0D 39 AD 68 04 48 63 CF 48 C1 E1 05 8B 50"},
        {0x21DFF8,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B F9 8B D9 49 63 F0 E8 80 FC FF FF 45 33 D2 4C 8B D8 48 85 C0 74 31 4C 6B C6 38 4D 03 D8 74 28"},
        {0xDEA60,"48 83 EC 28 4C 8B D9 48 8D 8A 88 05 00 00 33 D2 39 51 1C 74 06 83 79 0C FF EB 02 39 11 0F 95 C0 84 C0 74 12 E8 9B 59 F6 FF 48 8B D0 49 8B CB E8"},
    };
    for(const auto& binding:bindings)
    {
        const uintptr_t found=sig::Find(base,size,binding.pattern);
        if(found!=base+binding.rva || sig::Find(found+1,base+size-found-1,binding.pattern))
        { LOG("Halo 4 BSP glass supplement unavailable: own binding +0x%llX missing/ambiguous; material contact retained",static_cast<unsigned long long>(binding.rva));return false; }
    }
    if(!Halo4ContactVerifyCall(base,0x21F814,0xDEA60) ||
        !Halo4ContactVerifyCall(base,0x24F631,0x2DE9FC) ||
        !Halo4ContactVerifyCall(base,0x24F675,0x2DE9FC) ||
        !Halo4ContactVerifyCall(base,0x24F657,0x2DEC78) ||
        !Halo4ContactVerifyCall(base,0x24F663,0x2DEF10) ||
        !Halo4ContactVerifyCall(base,0x24F69E,0x2DF400) ||
        !Halo4ContactVerifyCall(base,0x24F6AA,0x2DF698) ||
        !Halo4ContactVerifyCall(base,0x21E95A,0x21DFF8))
    {LOG("Halo 4 BSP glass supplement unavailable: own call-edge proof failed; material contact retained");return false;}
    g_halo4ContactBsp.view=reinterpret_cast<decltype(g_halo4ContactBsp.view)>(base+0x21F7B4);
    g_halo4ContactBsp.initialize=reinterpret_cast<decltype(g_halo4ContactBsp.initialize)>(base+0x2DE9FC);
    g_halo4ContactBsp.standard[0]=reinterpret_cast<uint8_t(__fastcall*)(void*,uint32_t,const void*)>(base+0x2DEF10);
    g_halo4ContactBsp.standard[1]=reinterpret_cast<uint8_t(__fastcall*)(void*,uint32_t,const void*)>(base+0x2DF698);
    g_halo4ContactBsp.supernode[0]=reinterpret_cast<uint8_t(__fastcall*)(void*,uint32_t,int32_t,int32_t,int32_t,const void*)>(base+0x2DEC78);
    g_halo4ContactBsp.supernode[1]=reinterpret_cast<uint8_t(__fastcall*)(void*,uint32_t,int32_t,int32_t,int32_t,const void*)>(base+0x2DF400);
    g_halo4ContactBsp.valid=reinterpret_cast<decltype(g_halo4ContactBsp.valid)>(base+0x21DFF8);
    g_halo4ContactBsp.enabled.store(true,std::memory_order_release);
    LOG("Halo 4 BSP glass supplement installed: exact native surface/physical segment, authored validity and damage; headset verification pending");
    return true;
}

bool InstallHalo4ContactMelee(uintptr_t base,size_t size,uint32_t generation)
{
    struct Binding { uintptr_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x5ECCB4,"48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 48 8B 05 32 E3 84 00 48 33 C4 48 89 45 27 48 8B F1 48 89 4D D7 E8 57 02 04 00 8B 5E F4 44 8A E0"},
        {0x601B1C,"48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 38 FD FF FF 48 81 EC 90 03 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF"},
        {0x60296C,"48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F1 48 81 EC D0 00 00 00 48 8B 05 81 86 83 00 48 33 C4 48 89 45 07 48 8B 45 7F 45 33 FF 48 8B 5D 77"},
        {0x603330,"48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 FD FF FF 48 81 EC F0 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 48 8B 05"},
        {0x5DA400,"48 83 EC 28 44 8B 05 0D CE A7 00 44 8B DA 65 48 8B 04 25 58 00 00 00 8B D1 41 B9 18 00 00 00 45 33 D2 4A 8B 04 C0 49 8B 0C 01 E8 A9 96 A6 FF 48 8B C8 48 85 C0 74 0E 0F B6 40 04 41 0F A3 C3 73 04 4C 8B 51 10 49 8B C2"},
        {0x43AD8,"45 33 C0 83 FA FF 74 25 0F B7 C2 3B 41 44 7D 1D 4C 8B 49 20 4C 0F AF C8 4C 03 49 50 66 45 39 01 74 0B C1 FA 10 66 41 39 11 4D 0F 44 C1 49 8B C0 C3"},
        {0x9C62C,"48 83 EC 28 E8 CF F7 FF FF 33 C9 84 C0 74 22 8B 0D D7 AB FB 00 65 48 8B 04 25 58 00 00 00 BA 40 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 89 00 CE 01 00 8B C1 48 83 C4 28 C3"},
        {0x9C5A0,"48 83 EC 28 E8 5B F8 FF FF 33 C9 84 C0 74 1F 8B 0D 63 AC FB 00 65 48 8B 04 25 58 00 00 00 BA 40 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 49 1C 8B C1 48 83 C4 28 C3"},
        {0x601EE4,"48 8B 05 AD 91 9F 02 41 BE FE FF FF FF F3 0F 10 25 63 35 79 00 F3 44 0F"},
        {0x602580,"F2 0F 10 45 E8 8B 45 00 89 43 14 8B 45 F0 F2 41 0F 11 07 F2 0F 10 45 F4 41 89 47 08 8B 45 FC F2 0F 11 43 40 89 43 48 0F B7 45 18 66 89 43 20"},
        {0x603740,"66 44 3B 43 20 74 26 8B 43 28 44 0F BF 4B 22 44 0F BF 43 20 8B 53 24 8B 4B 18 89 44 24 28 48 8D 44 24 70 48 89 44 24 20"},
    };
    g_halo4Contact.enabled.store(false,std::memory_order_release);
    if(!g_halo4WorldCollision.originalRayCast || !g_halo4EngineTlsIndex ||
        g_halo4Contact.updateTarget || g_halo4Contact.damageTarget) return false;
    for(const auto& binding:bindings)
    {
        const uintptr_t hit=sig::Find(base,size,binding.pattern);
        if(hit!=base+binding.rva || sig::Find(hit+1,base+size-hit-1,binding.pattern))
        { LOG("Halo 4 contact melee unavailable: native binding +0x%llX missing/ambiguous",static_cast<unsigned long long>(binding.rva)); return false; }
    }
    if(!Halo4ContactVerifyCall(base,0x6020E5,0x1C1D4C) ||
        !Halo4ContactVerifyCall(base,0x603768,0x21E908) ||
        !Halo4ContactVerifyCall(base,0x6026D4,0x1C1D4C) ||
        !Halo4ContactVerifyCall(base,0x602DD3,0x601120) ||
        !Halo4ContactVerifyCall(base,0x602E10,0x601120) ||
        !Halo4ContactVerifyCall(base,0x6012ED,0x603330) ||
        !Halo4ContactVerifyCall(base,0x601310,0x603330) ||
        !Halo4ContactVerifyCall(base,0x6013C4,0x603330) ||
        !Halo4ContactVerifyCall(base,0x6013F8,0x603330) ||
        !Halo4ContactVerifyCall(base,0x601930,0x603330) ||
        !Halo4ContactVerifyCall(base,0x5DA42A,0x43AD8) ||
        !Halo4ContactVerifyCall(base,0x9C630,0x9BE04) ||
        !Halo4ContactVerifyCall(base,0x9C5A4,0x9BE04) ||
        !Halo4ContactVerifyCall(base,0x5ECCEC,0x62CF48))
    { LOG("Halo 4 contact melee unavailable: native call-edge verification failed"); return false; }
    const uintptr_t flags=base+0x601EEB+*reinterpret_cast<const int32_t*>(base+0x601EE7);
    if(flags!=base+0x2FFB098 || flags+sizeof(uint64_t)>base+size) return false;
    g_halo4Contact.base=base;
    g_halo4Contact.generation=generation;
    g_halo4Contact.flags=reinterpret_cast<const uint64_t*>(flags);
    g_halo4Contact.build=reinterpret_cast<Halo4ContactBuildFn>(base+0x601B1C);
    g_halo4Contact.consume=reinterpret_cast<Halo4ContactConsumeFn>(base+0x60296C);
    g_halo4Contact.object=reinterpret_cast<Halo4ContactObjectFn>(base+0x5DA400);
    g_halo4Contact.playbackMode=reinterpret_cast<uint32_t(__fastcall*)()>(base+0x9C62C);
    g_halo4Contact.simulationMode=reinterpret_cast<uint32_t(__fastcall*)()>(base+0x9C5A0);
    g_halo4Contact.faulted.store(false);
    g_halo4Contact.processing.store(false);
    g_halo4Contact.owner.store(UINT32_MAX);
    for(int hand=0;hand<2;++hand)
    { g_halo4Contact.queue[hand].Reset(); g_halo4Contact.hands[hand].Reset(); }
    void* const damageTarget=reinterpret_cast<void*>(base+0x603330);
    if(MH_CreateHook(damageTarget,reinterpret_cast<void*>(&Halo4ContactDamageDetour),
        reinterpret_cast<void**>(&g_halo4Contact.damageOriginal))!=MH_OK)
    { LOG("Halo 4 contact melee unavailable: damage hook creation failed"); return false; }
    g_halo4Contact.damageTarget=damageTarget;
    if(MH_EnableHook(damageTarget)!=MH_OK)
    { LOG("Halo 4 contact melee unavailable: damage hook enable failed; cleanup retained"); return false; }
    void* const updateTarget=reinterpret_cast<void*>(base+0x5ECCB4);
    if(MH_CreateHook(updateTarget,reinterpret_cast<void*>(&Halo4ContactUpdateDetour),
        reinterpret_cast<void**>(&g_halo4Contact.updateOriginal))!=MH_OK)
    { LOG("Halo 4 contact melee unavailable: simulation hook creation failed"); return false; }
    g_halo4Contact.updateTarget=updateTarget;
    if(MH_EnableHook(updateTarget)!=MH_OK)
    { LOG("Halo 4 contact melee unavailable: simulation hook enable failed; cleanup retained"); return false; }
    InstallHalo4ContactBsp(base,size);
    g_halo4Contact.enabled.store(true,std::memory_order_release);
    LOG("Halo 4 contact melee installed: independent physical hand/weapon sweeps, exact-target native damage; headset verification pending");
    return true;
}

void Halo4PublishContactHand(int hand,const float points[][3],uint32_t count,
    int32_t unit,uint64_t shape)
{
    if(!Halo4ContactMeleeReady() || !g_config.physical_melee || hand<0 || hand>1 ||
        !points || !count || count>contact_melee::kMaxPoints || unit==-1 ||
        g_halo4FloatingPair.generation!=g_halo4Contact.generation) return;
    ContactMeleePacket packet{};
    packet.publishedAtMs=GetTickCount64();
    packet.generation=g_halo4Contact.generation;
    packet.frame=g_halo4FloatingPair.contactFrames[hand];
    auto& frame=packet.frame;
    frame.unit=static_cast<uint32_t>(unit);
    frame.shape=shape ? shape : 1;
    frame.count=count;
    // These extrema have already been rebased onto the unblocked wrist/root
    // by Halo 4's accepted authored-volume publisher.
    for(uint32_t i=0;i<count;++i)
        frame.points[i]=frame.transform.Tracking({points[i][0],points[i][1],points[i][2]});
    g_halo4Contact.owner.store(frame.unit,std::memory_order_release);
    const int published=g_halo4Contact.queue[hand].Push(packet);
    if(hand==1 && published) g_halo4FloatingPair.rightContactPublished=true;
    if(!published)
        g_halo4Contact.overflow.fetch_add(1,std::memory_order_relaxed);
}

void ReportHalo4ContactMelee()
{
    if(!g_halo4Contact.updateTarget && !g_halo4Contact.damageTarget) return;
    LOG("Halo 4 BSP glass supplement: enabled=%d fault=%d",g_halo4ContactBsp.enabled.load()?1:0,g_halo4ContactBsp.faulted.load()?1:0);
    LOG("Halo 4 contact melee: enabled=%d fault=%d queries=%llu contacts=%llu submissions(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu predictedAttempts=%llu",
        Halo4ContactMeleeReady()?1:0,g_halo4Contact.faulted.load()?1:0,
        g_halo4Contact.queries.exchange(0),g_halo4Contact.contacts.exchange(0),
        g_halo4Contact.submitted[0].exchange(0),g_halo4Contact.submitted[1].exchange(0),
        g_halo4Contact.rejected.exchange(0),g_halo4Contact.overflow.exchange(0),g_halo4Contact.predicted.exchange(0));
}
