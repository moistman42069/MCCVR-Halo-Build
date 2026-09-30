// ODST native contact adapter. Included after the title-owned native getters.
// ODST kit mappings and native ABI proof: PHYSICAL-CONTACT-MELEE-WORK.md.
using OdstContactUpdateFn=uint8_t(__fastcall*)(uint32_t);
using OdstContactMeleeFn=void(__fastcall*)(uint32_t,int32_t,int16_t,float);
using OdstContactDamageFn=void(__fastcall*)(void*,uint32_t,int16_t,int16_t,uint16_t,void*);
struct OdstContactRuntime
{
    std::atomic<bool> enabled{false},faulted{false},processing{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> queries{0},contacts{0},submitted[2]{},rejected{0},overflow{0},predicted{0};
    uintptr_t base=0;
    uint32_t generation=0;
    void* updateTarget=nullptr;
    void* damageTarget=nullptr;
    OdstContactUpdateFn updateOriginal=nullptr;
    OdstContactDamageFn damageOriginal=nullptr;
    OdstContactMeleeFn melee=nullptr;
    GameIsPlaybackFn playback=nullptr;
    const uint64_t* flags=nullptr;
    ContactMeleeQueue queue[2];
    contact_melee::Hand hands[2];
    std::atomic<uint64_t> secondaryAtMs{0};
} g_odstContact;

struct OdstContactScope
{
    bool active=false,submitted=false,worldMatched=false;
    uint32_t owner=UINT32_MAX,target=UINT32_MAX;
    unsigned rays=0;
    contact_melee::Sweep sweep{};
    contact_melee::Hit expected{};
    float direction[3]{};
};
thread_local OdstContactScope g_odstContactScope;

bool OdstContactMeleeReady()
{
    return g_odstContact.enabled.load(std::memory_order_acquire) &&
        !g_odstContact.faulted.load(std::memory_order_acquire);
}

const uint8_t* OdstContactTls()
{
    if(!g_odstEngineTlsIndex || *g_odstEngineTlsIndex>=0x200) return nullptr;
    auto** slots=reinterpret_cast<void**>(__readgsqword(0x58));
    return slots ? static_cast<const uint8_t*>(slots[*g_odstEngineTlsIndex]) : nullptr;
}

bool OdstContactObject(uint32_t handle,bool requireBiped=false)
{
    if(handle==UINT32_MAX || !(handle>>16)) return false;
    const auto* tls=OdstContactTls();
    if(!tls) return false;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(tls+kOdstTlsObjectTableOffset);
    // ODST-VEHICLE-EVIDENCE.md: title-owned table, entry stride, live
    // extent and full salt. Reject stale handles before any engine call.
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

bool OdstContactBiped(uint32_t handle) { return OdstContactObject(handle,true); }

bool OdstContactWorldHit(const uint8_t* bytes,contact_melee::Hit& hit)
{
    if(!bytes) return false;
    const uint32_t type=*reinterpret_cast<const uint32_t*>(bytes);
    if(type!=1 && type!=3) return false;
    hit={};
    hit.unit=UINT32_MAX;
    hit.kind=contact_melee::HitKind::WorldSurface;
    hit.world.nativeType=type;
    // ODST kit 6A3B30 (type 1) and 6A56B0 (type 3) share this result layout:
    // surface words +18..27, material +28, type-3 section ID +3C, and
    // collision/BSP identity +4C..58. AD4FC0 dispatches the selected
    // material/surface tuple through its target==-1 environment path.
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

bool OdstRedirectContactVector(uintptr_t caller,uint64_t flags,int32_t mode,
    int32_t ignoredA,int32_t ignoredB,int32_t ignoredC,void* result,uint8_t& returned)
{
    auto& scope=g_odstContactScope;
    if(!scope.active || caller!=g_odstContact.base+0x39FEBD ||
        static_cast<uint32_t>(ignoredA)!=scope.owner) return false;
    returned=0;
    // Native ODST melee enumerates 25 rays. Use only its zero-priority centre
    // sample, with the physical segment; never allow a head-directed ray.
    if(++scope.rays!=13 || !result || !g_odstWorldCollision.original) return true;
    const auto delta=contact_melee::Subtract(scope.sweep.end,scope.sweep.start);
    const float start[]{scope.sweep.start.x,scope.sweep.start.y,scope.sweep.start.z};
    const float vector[]{delta.x,delta.y,delta.z};
    auto original=reinterpret_cast<LegacyCollisionTestVectorFn>(g_odstWorldCollision.original);
    returned=original(flags,mode,start,vector,ignoredA,ignoredB,ignoredC,result);
    if(returned)
    {
        const auto* bytes=static_cast<const uint8_t*>(result);
        // Reject a changed/occluded target before the native builder can
        // reparent it, run aim assist, or apply an unrelated contact.
        if(scope.expected.kind==contact_melee::HitKind::WorldSurface)
        {
            contact_melee::Hit actual{};
            if(!OdstContactWorldHit(bytes,actual) ||
                !contact_melee::SameWorldSurface(scope.expected,actual)) returned=0;
            else scope.worldMatched=true;
        }
        else if(*reinterpret_cast<const uint32_t*>(bytes)!=4 ||
            *reinterpret_cast<const uint32_t*>(bytes+0x40)!=scope.target)
            returned=0;
    }
    return true;
}

__declspec(noinline) void __fastcall OdstContactDamageDetour(void* event,uint32_t target,
    int16_t region,int16_t node,uint16_t material,void* result)
{
    g_odstContact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        auto& scope=g_odstContactScope;
        auto* bytes=static_cast<uint8_t*>(event);
        const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool own=scope.active && caller==g_odstContact.base+0x3A0D04;
        if(!own || (bytes && *reinterpret_cast<const uint32_t*>(bytes+0x18)==scope.owner &&
            target==scope.target && OdstContactObject(target)))
        {
            if(own)
            {
                // ODST kit AD4FC0 -> AF2D90/AF1770, retail 3A0B00 ->
                // 3B68A8: ODST impulse vectors are +40/+4C, not H3 fields.
                // This is the constructor's own stack event, not unit state.
                memcpy(bytes+0x40,scope.direction,sizeof(scope.direction));
                memcpy(bytes+0x4C,scope.direction,sizeof(scope.direction));
            }
            if(g_odstContact.damageOriginal)
            {
                g_odstContact.damageOriginal(event,target,region,node,material,result);
                if(own) scope.submitted=true;
            }
        }
    }
    __finally { g_odstContact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

struct OdstContactBackend
{
    uint32_t owner=UINT32_MAX;
    bool Query(const contact_melee::Sweep& sweep,contact_melee::Hit& hit) noexcept
    {
        // ODST collision vector kit 6A3B30 / retail 22F80C uses
        // the native result layout documented in ODST collision evidence. Scratch is local to
        // this query; no guessed result structure is copied into the engine.
        alignas(16) uint8_t result[0x80]{};
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float start[]{sweep.start.x,sweep.start.y,sweep.start.z};
        const float vector[]{delta.x,delta.y,delta.z};
        auto original=reinterpret_cast<LegacyCollisionTestVectorFn>(g_odstWorldCollision.original);
        g_odstContact.queries.fetch_add(1,std::memory_order_relaxed);
        if(!original || !g_odstContact.flags ||
            !original(*g_odstContact.flags,1,start,vector,static_cast<int32_t>(owner),-1,-1,result) ||
            (*reinterpret_cast<const uint32_t*>(result)!=4 &&
             *reinterpret_cast<const uint32_t*>(result)!=1)) return false;
        if(*reinterpret_cast<const uint32_t*>(result)==4)
        {
            memcpy(&hit.unit,result+0x40,sizeof(hit.unit));
            if(hit.unit==owner || !OdstContactObject(hit.unit)) return false;
            memcpy(&hit.fraction,result+4,sizeof(hit.fraction));
            memcpy(&hit.position,result+8,sizeof(hit.position));
            memcpy(&hit.normal,result+0x2C,sizeof(hit.normal));
            hit.object=true;
            hit.kind=contact_melee::HitKind::Object;
        }
        else if(!OdstContactWorldHit(result,hit)) return false;
        g_odstContact.contacts.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    bool Apply(uint32_t unit,const contact_melee::Hit& hit,const contact_melee::Sweep& sweep) noexcept
    {
        const bool objectHit=hit.kind==contact_melee::HitKind::Object || hit.object;
        const bool worldHit=hit.kind==contact_melee::HitKind::WorldSurface &&
            hit.unit==UINT32_MAX && !hit.object && hit.world.valid;
        if(unit!=owner || !OdstContactBiped(owner) || (!objectHit && !worldHit) ||
            (objectHit && !OdstContactObject(hit.unit))) return false;
        const auto* tls=OdstContactTls();
        const auto* globals=tls ? *reinterpret_cast<const uint8_t* const*>(tls+0x40) : nullptr;
        if(!globals || (!globals[0] && !globals[1])) return false;
        const int16_t mode=globals[0x11]==4 ? 1 : 0;
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float length=std::sqrt(contact_melee::Dot(delta,delta));
        if(!std::isfinite(length) || length<=1e-6f) return false;
        g_odstContactScope={};
        g_odstContactScope.active=true;
        g_odstContactScope.owner=owner;
        g_odstContactScope.target=hit.unit;
        g_odstContactScope.sweep=sweep;
        g_odstContactScope.expected=hit;
        g_odstContactScope.direction[0]=delta.x/length;
        g_odstContactScope.direction[1]=delta.y/length;
        g_odstContactScope.direction[2]=delta.z/length;
        // ODST kit AD9810 / retail 39F5AC: 79 selects weapon response
        // +25C. No animation or input request is issued.
        __try { g_odstContact.melee(owner,0x79,mode,1.0f); }
        __finally { g_odstContactScope.active=false; }
        if(mode==1) g_odstContact.predicted.fetch_add(1,std::memory_order_relaxed);
        return g_odstContactScope.rays==25 && (mode==1 ||
            (objectHit ? g_odstContactScope.submitted : g_odstContactScope.worldMatched));
    }
};

void OdstContactTick(uint32_t unit)
{
    if(!OdstContactMeleeReady() || TitleAdapter_GetActiveTitle()!=GameTitle::Halo3ODST ||
        !g_odstPlayerUnitGetter || unit!=static_cast<uint32_t>(g_odstPlayerUnitGetter(0))) return;
    bool expected=false;
    if(!g_odstContact.processing.compare_exchange_strong(expected,true,std::memory_order_acquire)) return;
    __try
    {
        const uint64_t now=GetTickCount64();
        int32_t scene=-1,shot=-1;
        const bool admit=!exclusive_input::Active() && g_config.physical_melee && g_enabled.load(std::memory_order_acquire) &&
            VR_IsStereoEnabled() && g_odstContact.generation==g_odstRuntimeGeneration.load() &&
            g_odstContact.playback && !g_odstContact.playback() &&
            ReadCinematicControl(scene,shot)==CinematicControlState::PlayerControlled &&
            g_odstUnitInVehicle && !g_odstUnitInVehicle(static_cast<int32_t>(unit));
        for(int hand=0;hand<2;++hand)
        {
            ContactMeleePacket packet{};
            for(unsigned n=0;n<8 && g_odstContact.queue[hand].Pop(packet);++n)
            {
                if(!admit || packet.generation!=g_odstContact.generation || packet.frame.unit!=unit ||
                    packet.publishedAtMs>now || now-packet.publishedAtMs>100)
                { g_odstContact.hands[hand].Reset(); continue; }
                OdstContactBackend backend{};
                backend.owner=unit;
                const auto result=g_odstContact.hands[hand].Process(packet.frame,
                    std::clamp(g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax),backend);
                if(result==contact_melee::ContactResult::Applied)
                {
                    g_odstContact.submitted[hand].fetch_add(1,std::memory_order_relaxed);
                    VR_PulseContactHaptics(hand==0,0.65f);
                }
                else if(result==contact_melee::ContactResult::NativeRejected)
                    g_odstContact.rejected.fetch_add(1,std::memory_order_relaxed);
            }
            if(!admit) g_odstContact.hands[hand].Reset();
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_odstContactScope.active=false;
        g_odstContact.faulted.store(true,std::memory_order_release);
    }
    g_odstContact.processing.store(false,std::memory_order_release);
}

__declspec(noinline) uint8_t __fastcall OdstContactUpdateDetour(uint32_t unit)
{
    g_odstContact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint8_t result=0;
    __try
    {
        if(g_odstContact.updateOriginal) result=g_odstContact.updateOriginal(unit);
        OdstContactTick(unit);
    }
    __finally { g_odstContact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
    return result;
}

bool OdstContactVerifyCall(uintptr_t base,uintptr_t caller,uintptr_t target)
{
    const auto* instruction=reinterpret_cast<const uint8_t*>(base+caller);
    return instruction[0]==0xE8 && base+caller+5+
        *reinterpret_cast<const int32_t*>(instruction+1)==base+target;
}

bool InstallOdstContactMelee(uintptr_t base,size_t size,uint32_t generation)
{
    struct Binding { uintptr_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x3DEB98,"48 89 5C 24 08 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 20 8B 15 68 0F 6B 00 33 DB 65 48 8B 04 25 58 00 00 00 40 8A FB 41 B9 20 00 00 00 44"},
        {0x39FA48,"48 8B C4 F3 0F 11 58 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FD FF FF 48 81 EC C8 02 00 00"},
        {0x3B68A8,"48 8B C4 66 44 89 48 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FE FF FF 48 81 EC 28 02 00 00 44 0F B7 11 45 33 E4"},
        {0x39FE8B,"48 8B 0D 36 E5 DD 01 48 8D 85 A0 00 00 00 48 89 44 24 38 4C 8D 4D 20 44 89 5C 24 30 4C 8D 45 B8 44 89 5C 24 28 41 8A D7 44 89 6C 24 20 E8 4F F9 E8 FF"},
        {0x10B0A8,"48 83 EC 28 E8 9F FB FF FF 32 D2 84 C0 74 27 8B 0D 5F 4A 98 00 65 48 8B 04 25 58 00 00 00 41 B8 40 00 00 00 48 8B 04 C8 4A 8B 0C 00 38 91 60 01 00 00 74 02 B2 01 8A C2 48 83 C4 28 C3"}};
    g_odstContact.enabled.store(false,std::memory_order_release);
    if(!g_odstWorldCollision.original || g_odstContact.updateTarget || g_odstContact.damageTarget) return false;
    for(const auto& binding:bindings)
    {
        const uintptr_t hit=sig::Find(base,size,binding.pattern);
        if(hit!=base+binding.rva || sig::Find(hit+1,base+size-hit-1,binding.pattern))
        { LOG("ODST contact melee unavailable: native binding +0x%llX missing/ambiguous",static_cast<unsigned long long>(binding.rva)); return false; }
    }
    if(!OdstContactVerifyCall(base,0x3DEC1E,0x3C2C28) ||
        !OdstContactVerifyCall(base,0x3DEC39,0x3CFF78) ||
        !OdstContactVerifyCall(base,0x39FEB8,0x22F80C) ||
        !OdstContactVerifyCall(base,0x3A0CFF,0x3B68A8) ||
        !OdstContactVerifyCall(base,0x329502,0x10B0A8) ||
        *reinterpret_cast<const uintptr_t*>(base+0x8F6A30)!=base+0x3DEB98)
    { LOG("ODST contact melee unavailable: native caller verification failed"); return false; }
    const uintptr_t flags=base+0x39FE92+*reinterpret_cast<const int32_t*>(base+0x39FE8E);
    if(flags!=base+0x217E3C8 || flags+sizeof(uint64_t)>base+size) return false;
    g_odstContact.base=base;
    g_odstContact.generation=generation;
    g_odstContact.flags=reinterpret_cast<const uint64_t*>(flags);
    g_odstContact.melee=reinterpret_cast<OdstContactMeleeFn>(base+0x39FA48);
    g_odstContact.playback=reinterpret_cast<GameIsPlaybackFn>(base+0x10B0A8);
    g_odstContact.faulted.store(false);
    g_odstContact.processing.store(false);
    g_odstContact.secondaryAtMs.store(0);
    for(int hand=0;hand<2;++hand)
    { g_odstContact.queue[hand].Reset(); g_odstContact.hands[hand].Reset(); }
    void* const damageTarget=reinterpret_cast<void*>(base+0x3B68A8);
    if(MH_CreateHook(damageTarget,reinterpret_cast<void*>(&OdstContactDamageDetour),
            reinterpret_cast<void**>(&g_odstContact.damageOriginal))!=MH_OK)
    { LOG("ODST contact melee unavailable: damage hook creation failed"); return false; }
    g_odstContact.damageTarget=damageTarget;
    if(MH_EnableHook(damageTarget)!=MH_OK)
    { LOG("ODST contact melee unavailable: damage hook enable failed; retained for cleanup"); return false; }
    void* const updateTarget=reinterpret_cast<void*>(base+0x3DEB98);
    if(MH_CreateHook(updateTarget,reinterpret_cast<void*>(&OdstContactUpdateDetour),
            reinterpret_cast<void**>(&g_odstContact.updateOriginal))!=MH_OK)
    { LOG("ODST contact melee unavailable: simulation hook creation failed"); return false; }
    g_odstContact.updateTarget=updateTarget;
    if(MH_EnableHook(updateTarget)!=MH_OK)
    { LOG("ODST contact melee unavailable: simulation hook enable failed; retained for cleanup"); return false; }
    g_odstContact.enabled.store(true,std::memory_order_release);
    LOG("ODST contact melee installed: physical hand/weapon contact, native exact-target damage; headset verification pending");
    return true;
}

void OdstPublishContactHand(int hand,const FpInterpolationContext& context,
    const BoneMatrix& root,const BoneMatrix* solved,const float points[][3],
    int count,int32_t unit,uint32_t generation)
{
    if(!OdstContactMeleeReady() || !g_config.physical_melee || hand<0 || hand>1 ||
        unit==-1 || !points || !solved || count<=0 || count>64 ||
        generation!=g_odstContact.generation || !g_baseCamValid.load()) return;
    const uint64_t now=GetTickCount64();
    // ODST-WEAPON-IK-EVIDENCE.md proves slot 1 is its secondary palette.
    // Give that palette stable ownership of support-hand contact samples;
    // alternating primary/secondary skeletons otherwise reseed every swing.
    if(context.slot==1) g_odstContact.secondaryAtMs.store(now,std::memory_order_release);
    const uint64_t secondary=g_odstContact.secondaryAtMs.load(std::memory_order_acquire);
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
        if(LegacyBuildMappedWeaponBounds(GameTitle::Halo3ODST,context,root,solved,world+count,weaponShape))
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
    if(!g_odstContact.queue[hand].Push(packet))
        g_odstContact.overflow.fetch_add(1,std::memory_order_relaxed);
}

void ReportOdstContactMelee()
{
    if(!g_odstContact.updateTarget) return;
    LOG("ODST contact melee: enabled=%d fault=%d queries=%llu contacts=%llu submissions(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu predictedRequests=%llu",
        OdstContactMeleeReady()?1:0,g_odstContact.faulted.load()?1:0,
        g_odstContact.queries.exchange(0),g_odstContact.contacts.exchange(0),
        g_odstContact.submitted[0].exchange(0),g_odstContact.submitted[1].exchange(0),
        g_odstContact.rejected.exchange(0),g_odstContact.overflow.exchange(0),g_odstContact.predicted.exchange(0));
}
