// H2EK-specific adapter, shared by H2 Classic and Anniversary renderers.
// See PHYSICAL-CONTACT-MELEE-WORK.md for ABI, layouts and native call edges.
using Halo2ContactMeleeFn=void(__fastcall*)(uint32_t,int32_t,uint8_t,float);
using Halo2ContactDamageFn=void(__fastcall*)(void*,uint32_t);
using Halo2ContactEventFn=void(__fastcall*)(uint32_t,int32_t,const uint32_t*,uint32_t,
    uint32_t,const void*,uint32_t,uint32_t);
struct Halo2ContactRuntime
{
    std::atomic<bool> enabled{false},faulted{false},processing{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> queries{0},contacts{0},applied[2]{},requests[2]{},rejected{0},drops{0};
    uintptr_t base=0;
    uint32_t generation=0;
    void* damageTarget=nullptr;
    void* eventTarget=nullptr;
    Halo2ContactMeleeFn melee=nullptr;
    Halo2ContactDamageFn damageOriginal=nullptr;
    Halo2ContactEventFn eventOriginal=nullptr;
    ContactMeleeQueue queue[2];
    contact_melee::Hand hands[2];
} g_halo2Contact;

struct Halo2ContactScope
{
    bool active=false,applied=false,requested=false,worldMatched=false;
    uint32_t owner=UINT32_MAX,target=UINT32_MAX;
    unsigned rays=0;
    contact_melee::Sweep sweep{};
    contact_melee::Hit expected{};
    contact_melee::Point position{},direction{};
};
thread_local Halo2ContactScope g_halo2ContactScope;

bool Halo2ContactMeleeReady()
{
    return g_halo2Contact.enabled.load(std::memory_order_acquire) &&
        !g_halo2Contact.faulted.load(std::memory_order_acquire);
}

const uint8_t* Halo2ContactObject(uint32_t handle,bool requireBiped=false)
{
    const auto* object=static_cast<const uint8_t*>(Halo2ObjectFromIndex(handle));
    return object && (!requireBiped || object[0xAA]==0) ? object : nullptr;
}

const uint8_t* Halo2ContactBiped(uint32_t handle) { return Halo2ContactObject(handle,true); }

bool Halo2ContactWorldHit(const uint8_t* bytes,contact_melee::Hit& hit) noexcept
{
    Halo2BreakableSurfaceIdentity identity{};
    if(!Halo2ReadBreakableSurfaceIdentity(bytes,0x60,identity))return false;
    hit={};
    hit.unit=UINT32_MAX;
    hit.kind=contact_melee::HitKind::WorldSurface;
    hit.world.nativeType=identity.type;
    hit.world.material=static_cast<int16_t>(identity.material);
    hit.world.nativeData[0]=static_cast<uint32_t>(identity.structureIndex);
    hit.world.nativeData[1]=static_cast<uint32_t>(identity.feature);
    hit.world.nativeData[2]=identity.breakableSurfaceIndex;
    hit.world.nativeData[3]=8; // H2EK collision_result breakable flag bit.
    std::memcpy(&hit.fraction,bytes+0x04,sizeof(hit.fraction));
    std::memcpy(&hit.position,bytes+0x08,sizeof(hit.position));
    std::memcpy(&hit.normal,bytes+0x2C,sizeof(hit.normal));
    hit.world.valid=std::isfinite(hit.fraction)&&hit.fraction>=0&&hit.fraction<=1&&
        contact_melee::Finite(hit.position)&&contact_melee::Finite(hit.normal);
    return hit.world.valid;
}

bool Halo2RedirectContactVector(uintptr_t caller,uint32_t flags,
    int32_t ignoredA,int32_t ignoredB,Halo2CollisionResult* result,uint8_t& returned)
{
    auto& scope=g_halo2ContactScope;
    if(!scope.active || caller!=g_halo2Contact.base+0x8F4024 || uint32_t(ignoredA)!=scope.owner)
        return false;
    returned=0;
    if(++scope.rays!=13 || !result || !g_halo2WorldCollision.original) return true;
    const auto delta=contact_melee::Subtract(scope.sweep.end,scope.sweep.start);
    const float start[]{scope.sweep.start.x,scope.sweep.start.y,scope.sweep.start.z};
    const float vector[]{delta.x,delta.y,delta.z};
    returned=g_halo2WorldCollision.original(flags,start,vector,ignoredA,ignoredB,result);
    if(returned)
    {
        if(scope.expected.kind==contact_melee::HitKind::WorldSurface)
        {
            contact_melee::Hit actual{};
            if(!Halo2ContactWorldHit(reinterpret_cast<const uint8_t*>(result),actual)||
                !contact_melee::SameWorldSurface(scope.expected,actual)) returned=0;
            else scope.worldMatched=true;
        }
        else if(result->type!=4 || uint32_t(result->objectIndex)!=scope.target)
            returned=0;
    }
    return true;
}

__declspec(noinline) void __fastcall Halo2ContactDamageDetour(void* event,uint32_t target)
{
    g_halo2Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        auto& scope=g_halo2ContactScope;
        const bool own=scope.active && reinterpret_cast<uintptr_t>(_ReturnAddress())==
            g_halo2Contact.base+0x8F482A;
        auto* bytes=static_cast<uint8_t*>(event);
        if(!own || (bytes && target==scope.target && Halo2ContactObject(target) &&
            *reinterpret_cast<const uint32_t*>(bytes+0x18)==scope.owner))
        {
            if(own)
            {
                // H2EK 481850 and retail 8F4690 construct their own stack
                // event. Replace the marker position and unit-forward impulses
                // for our exact hit only. Native recoil/self-damage stays stock.
                std::memcpy(bytes+0x24,&scope.position,sizeof(scope.position));
                std::memcpy(bytes+0x3C,&scope.direction,sizeof(scope.direction));
                std::memcpy(bytes+0x48,&scope.direction,sizeof(scope.direction));
            }
            if(g_halo2Contact.damageOriginal)
            {
                g_halo2Contact.damageOriginal(event,target);
                if(own) scope.applied=true;
            }
        }
    }
    __finally { g_halo2Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

__declspec(noinline) void __fastcall Halo2ContactEventDetour(uint32_t type,int32_t count,
    const uint32_t* objects,uint32_t player,uint32_t size,const void* payload,
    uint32_t flags,uint32_t extra)
{
    g_halo2Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        auto& scope=g_halo2ContactScope;
        const bool own=scope.active && reinterpret_cast<uintptr_t>(_ReturnAddress())==
            g_halo2Contact.base+0x8858B5;
        if(!own || (type==0x17 && count==2 && objects && objects[0]==scope.owner &&
            objects[1]==scope.target && size==0x1C && payload && Halo2ContactObject(scope.target)))
        {
            if(g_halo2Contact.eventOriginal)
            {
                g_halo2Contact.eventOriginal(type,count,objects,player,size,payload,flags,extra);
                // This is an issued native request, not proof of remote-host
                // acceptance. The native event has no physical direction field.
                if(own) scope.requested=true;
            }
        }
    }
    __finally { g_halo2Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

struct Halo2ContactBackend
{
    uint32_t owner=UINT32_MAX;
    bool Query(const contact_melee::Sweep& sweep,contact_melee::Hit& hit) noexcept
    {
        alignas(16) Halo2CollisionResult result{};
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float start[]{sweep.start.x,sweep.start.y,sweep.start.z};
        const float vector[]{delta.x,delta.y,delta.z};
        g_halo2Contact.queries.fetch_add(1,std::memory_order_relaxed);
        if(!g_halo2WorldCollision.original || !g_halo2WorldCollision.original(
            kHalo2CollisionFlags,start,vector,int32_t(owner),-1,&result))
            return false;
        if(result.type==4)
        {
            hit.unit=uint32_t(result.objectIndex);
            if(hit.unit==owner || !Halo2ContactObject(hit.unit)) return false;
            hit.fraction=result.fraction;
            std::memcpy(&hit.position,result.position,sizeof(hit.position));
            // H2 builder reads its collision plane normal at result +2C.
            std::memcpy(&hit.normal,reinterpret_cast<const uint8_t*>(&result)+0x2C,sizeof(hit.normal));
            hit.object=true;
            hit.kind=contact_melee::HitKind::Object;
            if(!std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 ||
                !contact_melee::Finite(hit.position) || !contact_melee::Finite(hit.normal)) return false;
        }
        else if(!Halo2ContactWorldHit(reinterpret_cast<const uint8_t*>(&result),hit))
            return false;
        g_halo2Contact.contacts.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    bool Apply(uint32_t unit,const contact_melee::Hit& hit,const contact_melee::Sweep& sweep) noexcept
    {
        const bool worldHit=hit.kind==contact_melee::HitKind::WorldSurface &&
            hit.unit==UINT32_MAX && !hit.object && hit.world.valid;
        const bool objectHit=(hit.kind==contact_melee::HitKind::Object || hit.object) &&
            hit.unit!=UINT32_MAX && Halo2ContactObject(hit.unit);
        if(unit!=owner || !Halo2ContactBiped(owner) || (!worldHit&&!objectHit)) return false;
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float length=std::sqrt(contact_melee::Dot(delta,delta));
        if(!std::isfinite(length) || length<=1e-6f) return false;
        g_halo2ContactScope={};
        g_halo2ContactScope.active=true;
        g_halo2ContactScope.owner=owner;
        g_halo2ContactScope.target=hit.unit;
        g_halo2ContactScope.sweep=sweep;
        g_halo2ContactScope.expected=hit;
        g_halo2ContactScope.position=hit.position;
        g_halo2ContactScope.direction={delta.x/length,delta.y/length,delta.z/length};
        // H2EK 480D40 and retail 8F3C80: state C000073 selects the first
        // authored weapon melee response; zero means apply, scalar 1 is full
        // authored damage. The routine retains native prediction/authority.
        __try { g_halo2Contact.melee(owner,0xC000073,0,1.0f); }
        __finally { g_halo2ContactScope.active=false; }
        return g_halo2ContactScope.rays==25 && (worldHit ?
            g_halo2ContactScope.worldMatched :
            (g_halo2ContactScope.applied || g_halo2ContactScope.requested));
    }
};

__declspec(noinline) void Halo2ContactTick(uint32_t unit)
{
    g_halo2Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!Halo2ContactMeleeReady()) __leave;
        bool expected=false;
        if(!g_halo2Contact.processing.compare_exchange_strong(expected,true,std::memory_order_acquire))
            __leave;
        __try
        {
            const uint64_t now=GetTickCount64();
            const auto* biped=Halo2ContactBiped(unit);
            const bool admit=!exclusive_input::Active() && g_config.physical_melee && Game_Halo2ControllerAimActive() &&
                Halo2Observer6Dof_DirectWeaponAimArmed() && biped &&
                g_vehicleSeatVerified.load(std::memory_order_acquire) &&
                *reinterpret_cast<const int16_t*>(biped+kHalo2UnitParentSeatOffset)==-1 &&
                g_generation.load(std::memory_order_acquire)==g_halo2Contact.generation;
            for(int hand=0;hand<2;++hand)
            {
                ContactMeleePacket packet{};
                for(unsigned n=0;n<8 && g_halo2Contact.queue[hand].Pop(packet);++n)
                {
                    if(!admit || packet.generation!=g_halo2Contact.generation || packet.frame.unit!=unit ||
                        packet.publishedAtMs>now || now-packet.publishedAtMs>100)
                    { g_halo2Contact.hands[hand].Reset(); continue; }
                    Halo2ContactBackend backend{};
                    backend.owner=unit;
                    const auto result=g_halo2Contact.hands[hand].Process(packet.frame,
                        std::clamp(g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax),backend);
                    if(result==contact_melee::ContactResult::Applied)
                    {
                        if(g_halo2ContactScope.applied)
                            g_halo2Contact.applied[hand].fetch_add(1,std::memory_order_relaxed);
                        if(g_halo2ContactScope.requested)
                            g_halo2Contact.requests[hand].fetch_add(1,std::memory_order_relaxed);
                        VR_PulseContactHaptics(hand==0,0.65f);
                    }
                    else if(result==contact_melee::ContactResult::NativeRejected)
                        g_halo2Contact.rejected.fetch_add(1,std::memory_order_relaxed);
                }
                if(!admit) g_halo2Contact.hands[hand].Reset();
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            g_halo2ContactScope.active=false;
            g_halo2Contact.faulted.store(true,std::memory_order_release);
        }
        g_halo2Contact.processing.store(false,std::memory_order_release);
    }
    __finally { g_halo2Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

void Halo2PrepareContactFrames(const Halo2ObserverPosePublication& publication,
    bool independentPrimary,Halo2VisibleConsumerContext& context)
{
    uint64_t epoch=(14695981039346656037ull^uint64_t(context.twoHandAimActive))*1099511628211ull;
    epoch=(epoch^uint64_t(independentPrimary))*1099511628211ull;
    const float settings[]{context.rightScale,context.leftScale,g_config.gun_forward_m,
        g_config.gun_right_m,g_config.gun_up_m,g_config.left_hand_forward_m,
        g_config.gun_pitch_deg,g_config.gun_yaw_deg,g_config.gun_roll_deg,
        g_config.barrel_pitch_deg,g_config.barrel_yaw_deg,g_config.barrel_roll_deg,
        g_config.halo2_classic_gun_yaw_deg,g_config.halo2_classic_gun_pitch_deg};
    for(float value:settings)
    { uint32_t bits=0; std::memcpy(&bits,&value,sizeof(bits)); epoch=(epoch^bits)*1099511628211ull; }
    for(unsigned hand=0;hand<2;++hand)
    {
        uint64_t shape=(14695981039346656037ull^context.weaponObject)*1099511628211ull;
        shape=(shape^context.secondaryWeaponObject)*1099511628211ull;
        shape=(shape^hand)*1099511628211ull;
        (void)Halo2PreparePhysicalContactFrame(publication,hand,context.unitObject,
            shape ? shape : 1,context.worldScale,epoch ? epoch : 1,context.contactFrames[hand]);
    }
}

void Halo2PublishContactSamples(int hand,const Halo2VisibleConsumerContext& context,
    const float points[][3],uint32_t count)
{
    if(!Halo2ContactMeleeReady() || !g_config.physical_melee || hand<0 || hand>1 ||
        !points || !count || count>contact_melee::kMaxPoints) return;
    ContactMeleePacket packet{};
    packet.generation=g_generation.load(std::memory_order_acquire);
    packet.publishedAtMs=GetTickCount64();
    packet.frame=context.contactFrames[hand];
    packet.frame.count=count;
    for(unsigned i=0;i<count;++i)
        packet.frame.points[i]=packet.frame.transform.Tracking({points[i][0],points[i][1],points[i][2]});
    if(!g_halo2Contact.queue[hand].Push(packet))
        g_halo2Contact.drops.fetch_add(1,std::memory_order_relaxed);
}

bool Halo2ContactVerifyCall(uintptr_t base,uintptr_t caller,uintptr_t target)
{
    const auto* instruction=reinterpret_cast<const uint8_t*>(base+caller);
    return instruction[0]==0xE8 && base+caller+5+
        *reinterpret_cast<const int32_t*>(instruction+1)==base+target;
}

bool InstallHalo2ContactMelee(uintptr_t base,size_t size,uint32_t generation)
{
    g_halo2Contact.enabled.store(false,std::memory_order_release);
    if(!g_halo2WorldCollision.original || !g_nativeAimOriginal.load() || !g_vehicleSeatVerified.load() ||
        g_halo2Contact.damageTarget || g_halo2Contact.eventTarget)
    { LOG("Halo 2 contact melee StockFallback: native dependencies unavailable or cleanup pending"); return false; }
    struct Binding { uintptr_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x8F3C80,"48 8B C4 44 88 40 18 89 50 10 89 48 08 55 53 41 57 48 8D A8 B8 FE FF FF 48 81 EC 30 02 00 00 48 89 70 20 45 33 FF"},
        {0x90F4B0,"48 83 EC 38 B8 FF FF FF FF 48 C7 44 24 28 00 00 00 00 44 8B C8 66 89 44 24 20 44 8B C0 E8 ?? ?? ?? ?? 48 83 C4 38 C3"},
        {0x8BD810,"40 55 57 41 54 41 55 41 56 48 83 EC 60 45 8B E1 4D 8B F0 48 63 EA 44 8B E9 E8 ?? ?? ?? ?? 48 8B F8 44 8B 50 10 41 83 EA 05"},
        {0x67AFEC,"89 7D 20 48 89 45 50 48 8B 44 24 58 89 75 24 C7 45 2C 40 74 40 64 48 C7 45 48 00 00 00 00 C6 45 29 00"},
        {0x8DD512,"E8 ?? ?? ?? ?? BA 00 00 10 00 48 89 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 32 DB E8 ?? ?? ?? ??"}};
    for(const auto& binding:bindings)
    {
        uintptr_t hit=0;
        uint32_t count=0;
        if(!CountPatternMatches(base,size,binding.pattern,hit,count) || count!=1 || hit!=base+binding.rva)
        { LOG("Halo 2 contact melee StockFallback: binding +0x%llX missing/ambiguous",(unsigned long long)binding.rva); return false; }
    }
    if(!Halo2ContactVerifyCall(base,0x936995,0x8F3C80) ||
        !Halo2ContactVerifyCall(base,0x8F401F,0x75B850) ||
        !Halo2ContactVerifyCall(base,0x8F44CC,0x8F4690) ||
        !Halo2ContactVerifyCall(base,0x8F4825,0x90F4B0) ||
        !Halo2ContactVerifyCall(base,0x90F4CD,0x90EB80) ||
        !Halo2ContactVerifyCall(base,0x8858B0,0x8BD810) ||
        !Halo2ContactVerifyCall(base,0x8DD512,0x67B1D0) ||
        base+0x8DD523+*reinterpret_cast<const int32_t*>(base+0x8DD51F)!=base+kHalo2ObjectsDataArrayPointerRva)
    { LOG("Halo 2 contact melee StockFallback: native call/data edge mismatch"); return false; }
    g_halo2Contact.base=base;
    g_halo2Contact.generation=generation;
    g_halo2Contact.melee=reinterpret_cast<Halo2ContactMeleeFn>(base+0x8F3C80);
    g_halo2Contact.faulted.store(false);
    g_halo2Contact.processing.store(false);
    for(unsigned hand=0;hand<2;++hand)
    { g_halo2Contact.queue[hand].Reset(); g_halo2Contact.hands[hand].Reset(); }
    void* const damage=reinterpret_cast<void*>(base+0x90F4B0);
    if(MH_CreateHook(damage,reinterpret_cast<void*>(&Halo2ContactDamageDetour),
        reinterpret_cast<void**>(&g_halo2Contact.damageOriginal))!=MH_OK)
    { LOG("Halo 2 contact melee StockFallback: damage hook creation failed"); return false; }
    g_halo2Contact.damageTarget=damage;
    if(MH_EnableHook(damage)!=MH_OK)
    { LOG("Halo 2 contact melee StockFallback: damage hook enable failed; cleanup retained"); return false; }
    void* const event=reinterpret_cast<void*>(base+0x8BD810);
    if(MH_CreateHook(event,reinterpret_cast<void*>(&Halo2ContactEventDetour),
        reinterpret_cast<void**>(&g_halo2Contact.eventOriginal))!=MH_OK)
    { LOG("Halo 2 contact melee StockFallback: native event hook creation failed"); return false; }
    g_halo2Contact.eventTarget=event;
    if(MH_EnableHook(event)!=MH_OK)
    { LOG("Halo 2 contact melee StockFallback: native event hook enable failed; cleanup retained"); return false; }
    g_halo2Contact.enabled.store(true,std::memory_order_release);
    LOG("Halo 2 contact melee installed for Classic/Anniversary: physical segments, independent hands, native exact-target damage; headset verification pending");
    return true;
}

bool RemoveHalo2ContactMelee()
{
    g_halo2Contact.enabled.store(false,std::memory_order_release);
    void* const targets[]{g_halo2Contact.damageTarget,g_halo2Contact.eventTarget};
    const void* functions[]{reinterpret_cast<const void*>(&Halo2ContactDamageDetour),
        reinterpret_cast<const void*>(&Halo2ContactEventDetour),reinterpret_cast<const void*>(&Halo2ContactTick)};
    const void* trampolines[]{reinterpret_cast<const void*>(g_halo2Contact.damageOriginal),
        reinterpret_cast<const void*>(g_halo2Contact.eventOriginal),nullptr};
    bool present=false;
    for(unsigned i=0;i<2;++i)
    {
        if(!targets[i]) continue;
        present=true;
        const auto status=MCCVR_DisableHookForRetirement(targets[i]);
        if((status!=MH_OK && status!=MH_ERROR_DISABLED) || !trampolines[i])
        { LOG("Halo 2 contact cleanup: disable/range verification failed; retained hooks"); return false; }
    }
    if(!present) return true;
    if(!WaitForNativeDetourQuiescence(functions,trampolines,3,g_halo2Contact.callbacks))
    { LOG("Halo 2 contact cleanup: callbacks/ingress busy; retained hooks"); return false; }
    bool removed=true;
    if(g_halo2Contact.damageTarget)
    {
        if(MH_RemoveHook(g_halo2Contact.damageTarget)==MH_OK)
        { g_halo2Contact.damageTarget=nullptr; g_halo2Contact.damageOriginal=nullptr; }
        else removed=false;
    }
    if(g_halo2Contact.eventTarget)
    {
        if(MH_RemoveHook(g_halo2Contact.eventTarget)==MH_OK)
        { g_halo2Contact.eventTarget=nullptr; g_halo2Contact.eventOriginal=nullptr; }
        else removed=false;
    }
    if(!removed) LOG("Halo 2 contact cleanup: removal failed; retained bookkeeping");
    return removed;
}

void ReportHalo2ContactMelee()
{
    LOG("Halo 2 contact melee: enabled=%d fault=%d queries=%llu contacts=%llu nativeDamage(L/R)=%llu/%llu nativeRequests(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu",
        Halo2ContactMeleeReady()?1:0,g_halo2Contact.faulted.load()?1:0,
        g_halo2Contact.queries.exchange(0),g_halo2Contact.contacts.exchange(0),
        g_halo2Contact.applied[0].exchange(0),g_halo2Contact.applied[1].exchange(0),
        g_halo2Contact.requests[0].exchange(0),g_halo2Contact.requests[1].exchange(0),
        g_halo2Contact.rejected.exchange(0),g_halo2Contact.drops.exchange(0));
}
