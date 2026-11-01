// H4EK-owned native contact path. Included after Halo 4's physics types/state.
// Binding and layout evidence: CONTACT-RESUME-CHECKPOINT.md.
#include "../common/halo4_contact_world.h"
// UnitInstance::Update (0x5ECCB4) returns a byte, "this object did something this tick" (`mov al, r12b`):
// the biped and vehicle Update virtuals OR it into their own result, and the per-object update lets an
// object whose updates all report nothing go idle. The detour passes it back, as Halo 3's twin does.
using Halo4ContactUpdateFn=uint8_t(__fastcall*)(void*);
using Halo4ContactBuildFn=void(__fastcall*)(uint32_t,int32_t,uint32_t*);
using Halo4ContactConsumeFn=void(__fastcall*)(uint32_t,uint64_t,int16_t,float,uint8_t,uint32_t*,void*);
// Forwarded through CallNativeWithSpareSlots (native_forward.h), like every title's damage event.
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

// A live object's type: the header's kind byte at entry +4, the byte PhysicsRayCast's own object predicate tests
// (halo4 0x1BFDB2: 0 biped, 1 vehicle), read only after the native accessor's full salt check passed. For the
// sight trace's hit class. -1: a stale handle. Callers guard it (the simulation thread's __try).
int Halo4SightObjectType(uint32_t handle)
{
    if(!Halo4ContactObject(handle)) return -1;
    auto** slots=reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
    const auto* table=*reinterpret_cast<const uint8_t* const*>(slots[*g_halo4EngineTlsIndex]+0x18);
    const auto* entries=*reinterpret_cast<const uint8_t* const*>(table+0x50);
    return entries[(handle&0xFFFF)*0x18+4];
}

// The same, behind its own guard (kept free of C++ objects so __try is legal).
int Halo4SightObjectTypeGuarded(int32_t object)
{
    if(object==-1) return -1;
    __try { return Halo4SightObjectType(static_cast<uint32_t>(object)); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Halo4RedirectContactRay, Halo4ContactDamageDetour (its damage forward goes through
// CallNativeWithSpareSlots, see Halo4ContactDamageFn) and Halo4ContactBackend, shared with the native fixture.
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
            const bool admit=!exclusive_input::Active() && Sight_PhysicalMelee() && g_enabled.load() && VR_IsStereoEnabled() &&
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

// THE SHOT'S OWN QUERY. Among Halo 4's PhysicsRayCast callers on the fire tick (the dev
// sight_ray_census lists them) is a shot's aim-assist query: caller +0x1DC6FA (function
// 0x1DC2F4), profile 0x1A, 128 wu from the engine eye along the aim, flags 0x...44D409. It builds them
// the way Reach and Halo 3 build their crosshair query (global | 0x202000 there):
//      mov rax,[rip+global]; mov ecx,0x404400; mov edi,eax; ...; or edi,ecx; shr rax,32; ... call PhysicsRayCast
// Every `mov ecx, 0x404400` anchor must have that load before it, the `shr rax, 32` after it and a call to
// the hooked PhysicsRayCast within 0x200 bytes; all must name one global inside the module, and the global
// must hold the melee query's flags (ValueAgreement with the global the contact install pinned: the same
// engine default). Then the trace uses global | 0x404400, the shot's flags. Any doubt: 0, and the trace
// keeps the melee query's flags.
// ADDR-HYGIENE: resolved + guarded (anchor + load + call edge + value agreement; no address is assumed).
std::atomic<uint64_t> g_halo4SightShotFlags{0};

const char* Halo4ResolveShotFlags(uintptr_t base,size_t size,uint64_t meleeValue,uint64_t& flags,
    uint32_t& anchors,uintptr_t& global)
{
    static constexpr char kAnchor[]="B9 00 44 40 00";   // mov ecx, 0x404400
    constexpr uint64_t kShotFlagsOr=0x404400;
    flags=0; anchors=0; global=0;
    const uintptr_t rayCast=reinterpret_cast<uintptr_t>(g_halo4WorldCollision.rayCastTarget);
    uintptr_t textBegin=0,textEnd=0;
    uint32_t stamp=0;
    size_t imageSize=0;
    if(!rayCast) return "PhysicsRayCast is not hooked on this build";
    if(!meleeValue) return "the melee collision flags are zero";
    if(!ResolveOdstTextRange(base,size,textBegin,textEnd,stamp,imageSize)) return "the module has no .text section";
    __try
    {
        for(uintptr_t at=sig::Find(textBegin,textEnd-textBegin,kAnchor);at;
            at=at+1<textEnd ? sig::Find(at+1,textEnd-at-1,kAnchor) : 0)
        {
            const auto* code=reinterpret_cast<const uint8_t*>(at);
            const uint8_t* load=nullptr;
            for(uintptr_t back=7;back<=0x10 && at-back>=textBegin && !load;++back)
                if(code[-static_cast<intptr_t>(back)]==0x48 && code[1-static_cast<intptr_t>(back)]==0x8B &&
                    code[2-static_cast<intptr_t>(back)]==0x05)
                    load=code-back;
            if(!load) continue;   // another use of the constant, not a flags build
            bool high=false,calls=false;
            for(uintptr_t fwd=5;fwd<=0x20 && at+fwd+4<=textEnd && !high;++fwd)
                high=code[fwd]==0x48 && code[fwd+1]==0xC1 && code[fwd+2]==0xE8 && code[fwd+3]==0x20;
            for(uintptr_t fwd=5;fwd<=0x200 && at+fwd+5<=textEnd && !calls;++fwd)
            {
                if(code[fwd]!=0xE8) continue;
                int32_t rel=0;
                memcpy(&rel,code+fwd+1,sizeof(rel));
                calls=at+fwd+5+static_cast<intptr_t>(rel)==rayCast;
            }
            if(!high || !calls) continue;
            int32_t disp=0;
            memcpy(&disp,load+3,sizeof(disp));
            const uintptr_t named=reinterpret_cast<uintptr_t>(load)+7+static_cast<intptr_t>(disp);
            if(named<base || named+sizeof(uint64_t)>base+size || (named&7u))
                return "a shot-flags load does not address an aligned qword inside the module";
            if(global && named!=global) return "two shot queries name different flags globals";
            global=named;
            if(++anchors>4) return "more shot-query anchors than any known build has";
        }
        if(!anchors) return "no `mov ecx, 0x404400` feeding PhysicsRayCast";
        uint64_t value=0;
        memcpy(&value,reinterpret_cast<const void*>(global),sizeof(value));
        if(value!=meleeValue) return "the shot-flags global disagrees with the melee query's flags";
        flags=value|kShotFlagsOr;
        return nullptr;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return "a fault while reading the shot query's code";
    }
}

// TRACE-AND-REAIM, Halo 4 on foot (sight_layer.h; the servo side is in Game_ComputeAimStick). Halo 4
// queries collision with Havok PhysicsRayCast, which is only safe in a native simulation context: this
// runs inside the per-unit update the contact lane hooks, after its own contact tick, with profile 0x1A
// and the SHOT's own flags (above) when they resolve, else the melee collision flags the contact install
// pinned from their own call site (the query the contact lane issues itself), and ignores the unit.
// The unit is the one whose first-person body the render thread last solved (g_halo4SightUnit), and a
// seated unit (parent != -1) is not traced here: a seat's ray is traced from the seat's control update
// (halo4_seat_converge.inl), under the seat's own stance.
void Halo4SightTraceTick(uint32_t unit,void* instance)
{
    constexpr GameTitle kTitle=GameTitle::Halo4;
    if(!LegacySight(kTitle).flags.load(std::memory_order_acquire) || !g_halo4Contact.flags ||
        !g_halo4WorldCollision.originalRayCast || TitleAdapter_GetActiveTitle()!=kTitle) return;
    const uint64_t now=GetTickCount64();
    const uint64_t unitAt=g_halo4SightUnitAtMs.load(std::memory_order_acquire);
    if(!unitAt || now<unitAt || now-unitAt>250 ||
        unit!=static_cast<uint32_t>(g_halo4SightUnit.load(std::memory_order_relaxed))) return;
    Sight_NoteTitleTick(kTitle);   // the measured tick (flat parity's carry; the dev SHOT LAG line's too)
    if(Sight_TraceFaulted(kTitle) || !Sight_TraceDue(kTitle,Sight_NowMs())) return;
    const uint32_t generation=TitleAdapter_GetGeneration(kTitle);
    SightRay ray{};
    if(!Sight_ReadRay(kTitle,generation,kSightRayFreshMs,ray)) return;
    const float length=kSightTraceLengthWu;
    Halo4PhysicsRayCastInput input{};
    Halo4PhysicsRayCastResult output{};
    input.profile=0x1A;
    for(int axis=0;axis<3;++axis)
    {
        input.start[axis]=ray.head[axis]+ray.dir[axis]*kSightTraceSkipWu;
        input.end[axis]=input.start[axis]+ray.dir[axis]*length;
    }
    input.ignoredObjects[0]=static_cast<int32_t>(unit);
    input.ignoredObjectCount=1;
    input.resultOptions[0]=1;
    bool returned=false,onFoot=false;
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    // DEV ASSESSMENT (honest_reticle_assess): the game's own aim ray from the engine eye, traced in this same
    // update with the same profile, flags and ignore. Only while the key published one. Its native call has its
    // own __try: a fault in it latches only the ASSESSMENT trace off (Sight_NoteAssessTraceFault), never the
    // intent trace (the outer handler's Sight_NoteTraceFault is the aim's).
    SightAssessRay assess{};
    const bool assessDue=!Sight_AssessTraceFaulted(kTitle) &&
        Sight_ReadAssessRay(kTitle,generation,kSightRayFreshMs,assess);
    Halo4PhysicsRayCastInput assessInput=input;
    Halo4PhysicsRayCastResult assessOutput{};
    bool assessReturned=false,assessFaulted=false;
    if(assessDue)
        for(int axis=0;axis<3;++axis)
        {
            assessInput.start[axis]=assess.origin[axis]+assess.dir[axis]*kSightTraceSkipWu;
            assessInput.end[axis]=assessInput.start[axis]+assess.dir[axis]*length;
        }
    // [assess-only end]
#endif
    LARGE_INTEGER before{},after{};
    __try
    {
        const auto* biped=Halo4ContactBiped(unit);
        onFoot=biped && biped==instance && *reinterpret_cast<const int32_t*>(biped+0x24)==-1;
        if(onFoot)
        {
            const uint64_t shotFlags=g_halo4SightShotFlags.load(std::memory_order_acquire);
            if(shotFlags) memcpy(&input.collisionFlags,&shotFlags,sizeof(shotFlags));
            else memcpy(&input.collisionFlags,g_halo4Contact.flags,sizeof(uint64_t));
            QueryPerformanceCounter(&before);
            returned=g_halo4WorldCollision.originalRayCast(&input,&output);
            QueryPerformanceCounter(&after);
#if HALOMCCVR_DEV_DIAGNOSTICS
            // [assess-only begin]
            if(assessDue)
            {
                memcpy(&assessInput.collisionFlags,&input.collisionFlags,sizeof(assessInput.collisionFlags));
                __try
                {
                    assessReturned=g_halo4WorldCollision.originalRayCast(&assessInput,&assessOutput);
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    assessFaulted=true;
                }
            }
            // [assess-only end]
#endif
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Sight_NoteTraceFault(kTitle);
        return;
    }
    if(!onFoot) return;
    const bool hit=returned && std::isfinite(output.fraction) && output.fraction>=0.0f && output.fraction<=1.0f;
    // The on-foot stance: a range a seat's trace left is withdrawn before this one is published.
    Sight_SetStance(kTitle,0);
    Sight_PublishTrace(kTitle,generation,hit,hit ? kSightTraceSkipWu+output.fraction*length : 0.0f,
        static_cast<uint64_t>(after.QuadPart-before.QuadPart));
#if HALOMCCVR_DEV_DIAGNOSTICS
    // SIGHT HITS (dev): what the ray stopped on, by class (an object hit is result type 4, its handle at +0x28, as
    // the contact backend reads it) -- after the cost was taken, so the trace's time is its own.
    {
        const bool objectHit=hit && output.type==4 && output.objectIndex!=-1;
        const int type=objectHit ? Halo4SightObjectTypeGuarded(output.objectIndex) : -1;
        Sight_NoteTraceHit(kTitle,sight_hit::ClassifyHit(hit,objectHit,type),type,
            hit ? kSightTraceSkipWu+output.fraction*length : 0.0f,false);
    }
#endif
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    if(assessFaulted)
        Sight_NoteAssessTraceFault(kTitle);   // the assessment's own latch, not the aim's
    else if(assessDue)
    {
        const bool assessHit=assessReturned && std::isfinite(assessOutput.fraction) &&
            assessOutput.fraction>=0.0f && assessOutput.fraction<=1.0f;
        Sight_PublishAssessTrace(kTitle,generation,assessHit,
            assessHit ? kSightTraceSkipWu+assessOutput.fraction*length : 0.0f);
    }
    // [assess-only end]
#endif
}

// Flat parity's fire-eye proof (halo4_fire_eye.inl), fed from our unit's own update: only for the render's
// first-person unit (a cheap atomic test first: every unit comes through), on foot by the same biped test the trace uses.
void Halo4FireEyeTickFromUpdate(uint32_t unit,void* instance)
{
    if(unit!=static_cast<uint32_t>(g_halo4SightUnit.load(std::memory_order_relaxed))) return;
    bool onFoot=false;
    __try
    {
        const auto* biped=Halo4ContactBiped(unit);
        onFoot=biped && biped==instance && *reinterpret_cast<const int32_t*>(biped+0x24)==-1;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { onFoot=false; }
    Halo4NoteFireEyeTick(unit,onFoot);
}

__declspec(noinline) uint8_t __fastcall Halo4ContactUpdateDetour(void* instance)
{
    g_halo4Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint8_t result=0;
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
        if(g_halo4Contact.updateOriginal) result=g_halo4Contact.updateOriginal(instance);
        if(unit!=UINT32_MAX) Halo4ContactTick(unit,instance);
        if(unit!=UINT32_MAX) Halo4SightTraceTick(unit,instance);
        if(unit!=UINT32_MAX) Halo4FireEyeTickFromUpdate(unit,instance);
    }
    __finally { g_halo4Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
    return result;
}

bool RemoveHalo4ContactMelee()
{
    g_halo4ContactBsp.enabled.store(false,std::memory_order_release);
    g_halo4Contact.enabled.store(false,std::memory_order_release);
    LegacySight(GameTitle::Halo4).flags.store(0,std::memory_order_release);
    g_halo4SightShotFlags.store(0,std::memory_order_release);
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
    LegacySight(GameTitle::Halo4).flags.store(0,std::memory_order_release);
    g_halo4SightShotFlags.store(0,std::memory_order_release);
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
    // Trace-and-reaim (Halo4SightTraceTick) measures its range with this lane's native melee query.
    {
        uint64_t meleeFlags=0;
        memcpy(&meleeFlags,g_halo4Contact.flags,sizeof(meleeFlags));
        auto& sight=LegacySight(GameTitle::Halo4);
        Sight_ResetTitle(GameTitle::Halo4);
        uint64_t shotFlags=0;
        uint32_t shotAnchors=0;
        uintptr_t shotGlobal=0;
        const char* shotWhy=Halo4ResolveShotFlags(base,size,meleeFlags,shotFlags,shotAnchors,shotGlobal);
        g_halo4SightShotFlags.store(shotWhy ? 0 : shotFlags,std::memory_order_release);
        sight.why.store(meleeFlags ? nullptr : "the melee collision flags are zero",std::memory_order_release);
        sight.flags.store(meleeFlags,std::memory_order_release);
        if(!shotWhy)
            LOG("SIGHT Halo 4 aim trace %s: PhysicsRayCast, profile 0x1A, the SHOT's own flags 0x%llX (%u "
                "aim-assist anchor(s), global +0x%llX = the melee query's 0x%llX, | 0x404400); on foot, the unit "
                "ignored", meleeFlags ? "armed" : "NOT armed",static_cast<unsigned long long>(shotFlags),
                shotAnchors,static_cast<unsigned long long>(shotGlobal-base),
                static_cast<unsigned long long>(meleeFlags));
        else
            LOG("SIGHT Halo 4 aim trace %s: PhysicsRayCast, native melee profile 0x1A, melee collision flags "
                "0x%llX (global +0x2FFB098, pinned from its own call site; the shot's own flags did not resolve: "
                "%s); on foot, the unit ignored", meleeFlags ? "armed" : "NOT armed",
                static_cast<unsigned long long>(meleeFlags),shotWhy);
        // What those flags let the ray stop on, decoded with Halo 4's own layout (sight_hit_class_logic.h).
        if(meleeFlags)
            Sight_AnnounceTraceQuery(GameTitle::Halo4,sight_hit::Engine::Halo4,shotWhy ? meleeFlags : shotFlags,
                "PhysicsRayCast, profile 0x1A",
                shotWhy ? "the melee query's flags, a proxy: the shot's own did not resolve"
                        : "the engine's own shot aim-assist query, resolved and value-checked",
                "on foot it ignores the unit (the engine skips the held weapon with it); a seat is traced only "
                "with halo4_seat_converge = 1, from the seat's control update: the vehicle's root, the occupant and "
                "the seat's parent ignored, and the engine skips the rest of that vehicle but a boarder");
    }
    return true;
}

void Halo4PublishContactHand(int hand,const float points[][3],uint32_t count,
    int32_t unit,uint64_t shape)
{
    if(!Halo4ContactMeleeReady() || !Sight_PhysicalMelee() || hand<0 || hand>1 ||
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
