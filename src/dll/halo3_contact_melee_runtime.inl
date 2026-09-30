// Halo 3 native contact adapter. Included after the title-owned native getters.
// H3EK mappings and native ABI proof: PHYSICAL-CONTACT-MELEE-WORK.md.
using Halo3ContactUpdateFn=uint8_t(__fastcall*)(uint32_t);
using Halo3ContactMeleeFn=void(__fastcall*)(uint32_t,int32_t,int16_t,float);
using Halo3ContactDamageFn=void(__fastcall*)(void*,uint32_t,int16_t,int16_t,uint16_t,void*);
struct Halo3ContactRuntime
{
    std::atomic<bool> enabled{false},faulted{false},processing{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> queries{0},contacts{0},submitted[2]{},rejected{0},overflow{0},predicted{0};
    uintptr_t base=0;
    uint32_t generation=0;
    void* updateTarget=nullptr;
    void* damageTarget=nullptr;
    Halo3ContactUpdateFn updateOriginal=nullptr;
    Halo3ContactDamageFn damageOriginal=nullptr;
    Halo3ContactMeleeFn melee=nullptr;
    GameIsPlaybackFn playback=nullptr;
    const uint64_t* flags=nullptr;
    ContactMeleeQueue queue[2];
    contact_melee::Hand hands[2];
    std::atomic<uint64_t> secondaryAtMs{0};
} g_halo3Contact;

struct Halo3ContactScope
{
    bool active=false,submitted=false;
    bool worldMatched=false;
    uint32_t owner=UINT32_MAX,target=UINT32_MAX;
    unsigned rays=0;
    contact_melee::Sweep sweep{};
    contact_melee::Hit expected{};
    float direction[3]{};
    int hand=1; // Controller role: support/secondary=0, main=1.
};
thread_local Halo3ContactScope g_halo3ContactScope;

bool Halo3ContactMeleeReady()
{
    return g_halo3Contact.enabled.load(std::memory_order_acquire) &&
        !g_halo3Contact.faulted.load(std::memory_order_acquire);
}

const uint8_t* Halo3ContactTls()
{
    if(!g_engineTlsIndex || *g_engineTlsIndex>=0x200) return nullptr;
    auto** slots=reinterpret_cast<void**>(__readgsqword(0x58));
    return slots ? static_cast<const uint8_t*>(slots[*g_engineTlsIndex]) : nullptr;
}

bool Halo3ContactObject(uint32_t handle,bool requireBiped=false)
{
    if(handle==UINT32_MAX || !(handle>>16)) return false;
    const auto* tls=Halo3ContactTls();
    if(!tls) return false;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(tls+0x38);
    // H3EK datum_get 364640: stride +24, live extent +3C, entries +48,
    // signed salt at entry +0. Retail 830F0..83107 retains these table fields;
    // H3's native unit getter independently establishes the object entry shape.
    if(!table || *reinterpret_cast<const int32_t*>(table+0x24)!=0x18) return false;
    const int32_t count=*reinterpret_cast<const int32_t*>(table+0x3C);
    if(count<=0 || count>0x10000 || (handle&0xFFFF)>=static_cast<uint32_t>(count)) return false;
    const auto* entries=*reinterpret_cast<const uint8_t* const*>(table+0x48);
    if(!entries) return false;
    const auto* entry=entries+(handle&0xFFFF)*0x18;
    return *reinterpret_cast<const uint16_t*>(entry)==static_cast<uint16_t>(handle>>16) &&
        entry[3]<32 && (!requireBiped || entry[3]==0) &&
        *reinterpret_cast<const void* const*>(entry+0x10);
}

bool Halo3ContactBiped(uint32_t handle) { return Halo3ContactObject(handle,true); }

bool Halo3ContactWorldHit(const uint8_t* bytes,contact_melee::Hit& hit)
{
    if(!bytes) return false;
    const uint32_t type=*reinterpret_cast<const uint32_t*>(bytes);
    if(type!=1 && type!=3) return false;
    hit={};
    hit.unit=UINT32_MAX;
    hit.kind=contact_melee::HitKind::WorldSurface;
    hit.world.nativeType=1;
    // H3EK collision 652A10 (type 1) and helper 654590 (type 3) share this
    // result layout: surface words +18..27, material +28, type-3 section ID
    // +3C, and structure/object identity words +4C..58. The melee consumer's
    // target==-1 branch dispatches the selected material/surface natively.
    hit.world.nativeType=type;
    memcpy(hit.world.nativeData,bytes+0x18,4*sizeof(uint32_t));
    memcpy(hit.world.nativeData+4,bytes+0x3C,sizeof(uint32_t));
    memcpy(hit.world.nativeData+5,bytes+0x4C,4*sizeof(uint32_t));
    memcpy(&hit.world.material,bytes+0x28,sizeof(hit.world.material));
    memcpy(&hit.fraction,bytes+4,sizeof(hit.fraction));
    memcpy(&hit.position,bytes+8,sizeof(hit.position));
    memcpy(&hit.normal,bytes+0x2C,sizeof(hit.normal));
    hit.world.valid=hit.world.nativeData[5]!=UINT32_MAX && hit.world.material!=-1 &&
        std::isfinite(hit.fraction) && hit.fraction>=0 && hit.fraction<=1 &&
        contact_melee::Finite(hit.position) && contact_melee::Finite(hit.normal);
    return hit.world.valid;
}

#include "halo3_melee_selection.inl"

bool Halo3RedirectContactVector(uintptr_t caller,uint64_t flags,int32_t mode,
    int32_t ignoredA,int32_t ignoredB,int32_t ignoredC,void* result,uint8_t& returned)
{
    auto& scope=g_halo3ContactScope;
    if(!scope.active || caller!=g_halo3Contact.base+0x35B297 ||
        static_cast<uint32_t>(ignoredA)!=scope.owner) return false;
    returned=0;
    // Native H3 melee enumerates 25 rays. Use only its zero-priority centre
    // sample, with the physical segment; never allow a head-directed ray.
    if(++scope.rays!=13 || !result || !g_halo3WorldCollision.original) return true;
    const auto delta=contact_melee::Subtract(scope.sweep.end,scope.sweep.start);
    const float start[]{scope.sweep.start.x,scope.sweep.start.y,scope.sweep.start.z};
    const float vector[]{delta.x,delta.y,delta.z};
    auto original=reinterpret_cast<LegacyCollisionTestVectorFn>(g_halo3WorldCollision.original);
    returned=original(flags,mode,start,vector,ignoredA,ignoredB,ignoredC,result);
    if(returned)
    {
        const auto* bytes=static_cast<const uint8_t*>(result);
        // Reject a changed/occluded target before the native builder can
        // reparent it, run aim assist, or apply an unrelated contact.
        if(scope.expected.kind==contact_melee::HitKind::WorldSurface)
        {
            contact_melee::Hit actual{};
            if(!Halo3ContactWorldHit(bytes,actual) ||
                !contact_melee::SameWorldSurface(scope.expected,actual)) returned=0;
            else scope.worldMatched=true;
        }
        else if(*reinterpret_cast<const uint32_t*>(bytes)!=4 ||
            *reinterpret_cast<const uint32_t*>(bytes+0x40)!=scope.target)
            returned=0;
    }
    return true;
}

__declspec(noinline) void __fastcall Halo3ContactDamageDetour(void* event,uint32_t target,
    int16_t region,int16_t node,uint16_t material,void* result)
{
    g_halo3Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        auto& scope=g_halo3ContactScope;
        auto* bytes=static_cast<uint8_t*>(event);
        const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool own=scope.active && caller==g_halo3Contact.base+0x35C102;
        if(!own || (bytes && *reinterpret_cast<const uint32_t*>(bytes+0x18)==scope.owner &&
            target==scope.target && Halo3ContactObject(target)))
        {
            if(own)
            {
                // H3EK A59860 -> A745B0 -> A73390 and retail 35BEFC ->
                // 36FE00 agree: +3C/+48 are the two native impulse vectors.
                // This is the constructor's own stack event, not unit state.
                memcpy(bytes+0x3C,scope.direction,sizeof(scope.direction));
                memcpy(bytes+0x48,scope.direction,sizeof(scope.direction));
            }
            if(g_halo3Contact.damageOriginal)
            {
                g_halo3Contact.damageOriginal(event,target,region,node,material,result);
                if(own) scope.submitted=true;
            }
        }
    }
    __finally { g_halo3Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

struct Halo3ContactBackend
{
    uint32_t owner=UINT32_MAX;
    int hand=1;
    bool Query(const contact_melee::Sweep& sweep,contact_melee::Hit& hit) noexcept
    {
        // Native collision initializes/uses fields through +63 (H3EK
        // 652A10 and retail 1FD748). Oversized aligned scratch is local to
        // this query; no guessed result structure is copied into the engine.
        alignas(16) uint8_t result[0x80]{};
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float start[]{sweep.start.x,sweep.start.y,sweep.start.z};
        const float vector[]{delta.x,delta.y,delta.z};
        auto original=reinterpret_cast<LegacyCollisionTestVectorFn>(g_halo3WorldCollision.original);
        g_halo3Contact.queries.fetch_add(1,std::memory_order_relaxed);
        if(!original || !g_halo3Contact.flags ||
            !original(*g_halo3Contact.flags,1,start,vector,static_cast<int32_t>(owner),-1,-1,result) ||
            (*reinterpret_cast<const uint32_t*>(result)!=4 &&
             *reinterpret_cast<const uint32_t*>(result)!=1)) return false;
        if(*reinterpret_cast<const uint32_t*>(result)==4)
        {
            memcpy(&hit.unit,result+0x40,sizeof(hit.unit));
            if(hit.unit==owner || !Halo3ContactObject(hit.unit)) return false;
            memcpy(&hit.fraction,result+4,sizeof(hit.fraction));
            memcpy(&hit.position,result+8,sizeof(hit.position));
            memcpy(&hit.normal,result+0x2C,sizeof(hit.normal));
            hit.object=true;
            hit.kind=contact_melee::HitKind::Object;
        }
        else if(!Halo3ContactWorldHit(result,hit)) return false;
        g_halo3Contact.contacts.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    bool Apply(uint32_t unit,const contact_melee::Hit& hit,const contact_melee::Sweep& sweep) noexcept
    {
        const bool objectHit=hit.kind==contact_melee::HitKind::Object || hit.object;
        const bool worldHit=hit.kind==contact_melee::HitKind::WorldSurface &&
            hit.unit==UINT32_MAX && !hit.object && hit.world.valid;
        if(unit!=owner || !Halo3ContactBiped(owner) || (!objectHit && !worldHit) ||
            (objectHit && !Halo3ContactObject(hit.unit))) return false;
        const auto* tls=Halo3ContactTls();
        const auto* globals=tls ? *reinterpret_cast<const uint8_t* const*>(tls+0x48) : nullptr;
        if(!globals || (!globals[0] && !globals[1])) return false;
        const int16_t mode=globals[0x11]==4 ? 1 : 0;
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float length=std::sqrt(contact_melee::Dot(delta,delta));
        if(!std::isfinite(length) || length<=1e-6f) return false;
        g_halo3ContactScope={};
        g_halo3ContactScope.active=true;
        g_halo3ContactScope.owner=owner;
        g_halo3ContactScope.target=hit.unit;
        g_halo3ContactScope.sweep=sweep;
        g_halo3ContactScope.expected=hit;
        g_halo3ContactScope.direction[0]=delta.x/length;
        g_halo3ContactScope.direction[1]=delta.y/length;
        g_halo3ContactScope.direction[2]=delta.z/length;
        g_halo3ContactScope.hand=hand;
        // 79 selects the first authored melee damage response in H3EK A5DE20
        // and retail 35A9A4. It does not request an animation or button action.
        __try { g_halo3Contact.melee(owner,0x79,mode,1.0f); }
        __finally { g_halo3ContactScope.active=false; }
        if(mode==1) g_halo3Contact.predicted.fetch_add(1,std::memory_order_relaxed);
        return g_halo3ContactScope.rays==25 && (mode==1 ||
            (objectHit ? g_halo3ContactScope.submitted : g_halo3ContactScope.worldMatched));
    }
};

void Halo3ContactTick(uint32_t unit)
{
    if(!Halo3ContactMeleeReady() || TitleAdapter_GetActiveTitle()!=GameTitle::Halo3 ||
        !g_halo3PlayerUnitGetter || unit!=static_cast<uint32_t>(g_halo3PlayerUnitGetter(0))) return;
    bool expected=false;
    if(!g_halo3Contact.processing.compare_exchange_strong(expected,true,std::memory_order_acquire)) return;
    __try
    {
        const uint64_t now=GetTickCount64();
        int32_t scene=-1,shot=-1;
        const bool admit=!exclusive_input::Active() && g_config.physical_melee && g_enabled.load(std::memory_order_acquire) &&
            VR_IsStereoEnabled() && g_halo3Contact.generation==g_halo3RuntimeGeneration.load() &&
            g_halo3Contact.playback && !g_halo3Contact.playback() &&
            ReadCinematicControl(scene,shot)==CinematicControlState::PlayerControlled &&
            g_halo3UnitInVehicle && !g_halo3UnitInVehicle(static_cast<int32_t>(unit));
        for(int hand=0;hand<2;++hand)
        {
            ContactMeleePacket packet{};
            for(unsigned n=0;n<8 && g_halo3Contact.queue[hand].Pop(packet);++n)
            {
                if(!admit || packet.generation!=g_halo3Contact.generation || packet.frame.unit!=unit ||
                    packet.publishedAtMs>now || now-packet.publishedAtMs>100)
                { g_halo3Contact.hands[hand].Reset(); continue; }
                Halo3ContactBackend backend{};
                backend.owner=unit;
                backend.hand=hand;
                const auto result=g_halo3Contact.hands[hand].Process(packet.frame,
                    std::clamp(g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax),backend);
                if(result==contact_melee::ContactResult::Applied)
                {
                    g_halo3Contact.submitted[hand].fetch_add(1,std::memory_order_relaxed);
                    VR_PulseContactHaptics(hand==0,0.65f);
                }
                else if(result==contact_melee::ContactResult::NativeRejected)
                    g_halo3Contact.rejected.fetch_add(1,std::memory_order_relaxed);
            }
            if(!admit) g_halo3Contact.hands[hand].Reset();
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_halo3ContactScope.active=false;
        g_halo3Contact.faulted.store(true,std::memory_order_release);
    }
    g_halo3Contact.processing.store(false,std::memory_order_release);
}

__declspec(noinline) uint8_t __fastcall Halo3ContactUpdateDetour(uint32_t unit)
{
    g_halo3Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint8_t result=0;
    __try
    {
        if(g_halo3Contact.updateOriginal) result=g_halo3Contact.updateOriginal(unit);
        Halo3ContactTick(unit);
    }
    __finally { g_halo3Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
    return result;
}

bool Halo3ContactVerifyCall(uintptr_t base,uintptr_t caller,uintptr_t target)
{
    const auto* instruction=reinterpret_cast<const uint8_t*>(base+caller);
    return instruction[0]==0xE8 && base+caller+5+
        *reinterpret_cast<const int32_t*>(instruction+1)==base+target;
}

bool InstallHalo3ContactMelee(uintptr_t base,size_t size,uint32_t generation)
{
    struct Binding { uintptr_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x379B30,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 8B 15 ?? ?? ?? ?? 40 32 FF 65 48 8B 04 25 58 00 00 00 33 F6 44 8B F1 B9 38 00 00 00"},
        {0x35AE34,"48 8B C4 F3 0F 11 58 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FD FF FF 48 81 EC D8 02 00 00"},
        {0x36FE00,"48 8B C4 66 44 89 48 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FE FF FF 48 81 EC 28 02 00 00 44 0F B7 11 45 33 E4"},
        {0x35B265,"48 8B 0D ?? ?? ?? ?? 48 8D 85 B0 00 00 00 48 89 44 24 38 4C 8D 4D 30 44 89 5C 24 30 4C 8D 45 E0 44 89 5C 24 28 41 8A D4 44 89 7C 24 20 E8 ?? ?? ?? ??"},
        {0xF01A0,"48 83 EC 28 E8 ?? ?? ?? ?? 32 D2 84 C0 74 27 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 48 00 00 00 48 8B 04 C8 4A 8B 0C 00 38 91 60 01 00 00 74 02 B2 01 8A C2 48 83 C4 28 C3"}};
    g_halo3Contact.enabled.store(false,std::memory_order_release);
    if(!g_halo3WorldCollision.original || g_halo3Contact.updateTarget || g_halo3Contact.damageTarget) return false;
    for(const auto& binding:bindings)
    {
        const uintptr_t hit=sig::Find(base,size,binding.pattern);
        if(hit!=base+binding.rva || sig::Find(hit+1,base+size-hit-1,binding.pattern))
        { LOG("Halo 3 contact melee unavailable: native binding +0x%llX missing/ambiguous",static_cast<unsigned long long>(binding.rva)); return false; }
    }
    if(!Halo3ContactVerifyCall(base,0x39A5AD,0x379B30) ||
        !Halo3ContactVerifyCall(base,0x35B292,0x1FD748) ||
        !Halo3ContactVerifyCall(base,0x35C0FD,0x36FE00) ||
        !Halo3ContactVerifyCall(base,0x2EDFA1,0xF01A0))
    { LOG("Halo 3 contact melee unavailable: native caller verification failed"); return false; }
    const uintptr_t flags=base+0x35B26C+*reinterpret_cast<const int32_t*>(base+0x35B268);
    if(flags!=base+0x2127108 || flags+sizeof(uint64_t)>base+size) return false;
    g_halo3Contact.base=base;
    g_halo3Contact.generation=generation;
    g_halo3Contact.flags=reinterpret_cast<const uint64_t*>(flags);
    g_halo3Contact.melee=reinterpret_cast<Halo3ContactMeleeFn>(base+0x35AE34);
    g_halo3Contact.playback=reinterpret_cast<GameIsPlaybackFn>(base+0xF01A0);
    g_halo3Contact.faulted.store(false);
    g_halo3Contact.processing.store(false);
    g_halo3Contact.secondaryAtMs.store(0);
    for(int hand=0;hand<2;++hand)
    { g_halo3Contact.queue[hand].Reset(); g_halo3Contact.hands[hand].Reset(); }
    void* const damageTarget=reinterpret_cast<void*>(base+0x36FE00);
    if(MH_CreateHook(damageTarget,reinterpret_cast<void*>(&Halo3ContactDamageDetour),
            reinterpret_cast<void**>(&g_halo3Contact.damageOriginal))!=MH_OK)
    { LOG("Halo 3 contact melee unavailable: damage hook creation failed"); return false; }
    g_halo3Contact.damageTarget=damageTarget;
    if(MH_EnableHook(damageTarget)!=MH_OK)
    { LOG("Halo 3 contact melee unavailable: damage hook enable failed; retained for cleanup"); return false; }
    void* const updateTarget=reinterpret_cast<void*>(base+0x379B30);
    if(MH_CreateHook(updateTarget,reinterpret_cast<void*>(&Halo3ContactUpdateDetour),
            reinterpret_cast<void**>(&g_halo3Contact.updateOriginal))!=MH_OK)
    { LOG("Halo 3 contact melee unavailable: simulation hook creation failed"); return false; }
    g_halo3Contact.updateTarget=updateTarget;
    if(MH_EnableHook(updateTarget)!=MH_OK)
    { LOG("Halo 3 contact melee unavailable: simulation hook enable failed; retained for cleanup"); return false; }
    g_halo3Contact.enabled.store(true,std::memory_order_release);
    (void)InstallHalo3MeleeSelection(base,size);
    LOG("Halo 3 contact melee installed: physical hand/weapon contact, native exact-target damage; headset verification pending");
    return true;
}

void Halo3PublishContactHand(int hand,const FpInterpolationContext& context,
    const BoneMatrix& root,const BoneMatrix* solved,const float points[][3],
    int count,int32_t unit,uint32_t generation)
{
    if(!Halo3ContactMeleeReady() || !g_config.physical_melee || hand<0 || hand>1 ||
        unit==-1 || !points || !solved || count<=0 || count>64 ||
        generation!=g_halo3Contact.generation || !g_baseCamValid.load()) return;
    const uint64_t now=GetTickCount64();
    if(context.slot==1) g_halo3Contact.secondaryAtMs.store(now,std::memory_order_release);
    const uint64_t secondary=g_halo3Contact.secondaryAtMs.load(std::memory_order_acquire);
    // Once a secondary palette is present it owns left-hand publication. This
    // also works when World collision is disabled (its telemetry is unrelated).
    if(hand==0 && context.slot==0 && secondary && now>=secondary && now-secondary<=100) return;
    VrContactTrackingSnapshot tracking{};
    if(!VR_GetContactTrackingSnapshot(tracking)) return;
    ContactMeleePacket packet{};
    packet.publishedAtMs=now;
    packet.generation=generation;
    auto& frame=packet.frame;
    frame.serial=tracking.serial;
    frame.timeNs=tracking.timeNs;
    frame.unit=static_cast<uint32_t>(unit);
    frame.rigidMotion=true;
    const auto& controller=tracking.hands[hand];
    const bool poseValid=controller.valid &&
        frame.controllerPose.SetPose(controller.orientation,controller.position);
    // Exact on-foot position mapping used by ControllerWorldPoseEx. Express
    // both samples in this current actor frame, excluding walking/turning.
    const float sh=sinf(g_headYawRef),ch=cosf(g_headYawRef);
    const float cg=cosf(g_gameYawRef),sg=sinf(g_gameYawRef);
    frame.transform.unitsPerMetre=g_worldScale.load();
    frame.transform.axis[0]={cg*sh+sg*ch,sg*sh-cg*ch,0};
    frame.transform.axis[1]={0,0,1};
    frame.transform.axis[2]={-cg*ch+sg*sh,-sg*ch-cg*sh,0};
    const auto reference=frame.transform.World({g_headPosRef[0],g_headPosRef[1],g_headPosRef[2]});
    frame.transform.origin={g_baseCamX.load()-reference.x,g_baseCamY.load()-reference.y,
                            g_baseCamZ.load()-reference.z};
    if(poseValid && tracking.referenceEpoch)
    {
        uint64_t epoch=(14695981039346656037ull^tracking.referenceEpoch)*1099511628211ull;
        epoch=(epoch^generation)*1099511628211ull;
        epoch=(epoch^uint64_t(tracking.twoHandAimActive))*1099511628211ull;
        const float settings[]{g_headPosRef[0],g_headPosRef[1],g_headPosRef[2],g_headYawRef,
            g_config.gun_scale,g_config.left_hand_scale,g_config.gun_forward_m,
            g_config.gun_right_m,g_config.gun_up_m,g_config.left_hand_forward_m,
            g_config.gun_pitch_deg,g_config.gun_yaw_deg,g_config.gun_roll_deg,
            g_config.barrel_pitch_deg,g_config.barrel_yaw_deg,g_config.barrel_roll_deg};
        for(float value:settings)
        { uint32_t bits=0; memcpy(&bits,&value,sizeof(bits)); epoch=(epoch^bits)*1099511628211ull; }
        frame.referenceEpoch=epoch ? epoch : 1;
    }
    const uint64_t mask=hand ? context.wristDescendants : context.lWristDescendants;
    frame.shape=(mask^(uint64_t(context.count)<<48)^uint64_t(context.slot))*1099511628211ull;
    float world[contact_melee::kMaxPoints][3]{};
    memcpy(world,points,sizeof(float)*3*count);
    if((hand==1 && context.slot==0) || (hand==0 && context.slot==1))
    {
        uint64_t weaponShape=0;
        if(LegacyBuildMappedWeaponBounds(GameTitle::Halo3,context,root,solved,world+count,weaponShape))
        {
            count+=kLegacyCollisionWeaponSamples;
            frame.shape=(frame.shape^weaponShape)*1099511628211ull;
        }
    }
    frame.shape=frame.shape ? frame.shape : 1;
    frame.count=static_cast<unsigned>(count);
    for(int i=0;i<count;++i)
        frame.points[i]=frame.transform.Tracking({
            world[i][0]-g_fpPaletteCollisionCorrection[hand][0],
            world[i][1]-g_fpPaletteCollisionCorrection[hand][1],
            world[i][2]-g_fpPaletteCollisionCorrection[hand][2]});
    if(!g_halo3Contact.queue[hand].Push(packet))
        g_halo3Contact.overflow.fetch_add(1,std::memory_order_relaxed);
}

void ReportHalo3ContactMelee()
{
    ReportHalo3DualAim();
    if(!g_halo3Contact.updateTarget) return;
    ReportHalo3MeleeSelection();
    LOG("Halo 3 contact melee: enabled=%d fault=%d queries=%llu contacts=%llu submissions(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu predictedRequests=%llu",
        Halo3ContactMeleeReady()?1:0,g_halo3Contact.faulted.load()?1:0,
        g_halo3Contact.queries.exchange(0),g_halo3Contact.contacts.exchange(0),
        g_halo3Contact.submitted[0].exchange(0),g_halo3Contact.submitted[1].exchange(0),
        g_halo3Contact.rejected.exchange(0),g_halo3Contact.overflow.exchange(0),g_halo3Contact.predicted.exchange(0));
}
