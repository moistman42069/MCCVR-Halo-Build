struct CachedPhysicalCrouchBinding
{
    std::atomic<uint32_t> generation{};
    const uint8_t* const* tagTable{};
    const uintptr_t* pages{};
};
CachedPhysicalCrouchBinding g_reachCrouchCamera,g_halo4CrouchCamera;

void PrepareCachedPhysicalCrouch(GameTitle title,uintptr_t base,size_t size,uint32_t generation)
{
    auto& binding=title==GameTitle::HaloReach?g_reachCrouchCamera:g_halo4CrouchCamera;
    binding.generation.store(0,std::memory_order_release);
    uintptr_t selector=0;
    unsigned matched=0;
    for(const auto& witness:kCachedCrouchWitnesses)
    {
        if(witness.title!=title) continue;
        const auto hit=sig::Find(base,size,witness.pattern);
        if(!hit||hit-base!=witness.rva||sig::Find(hit+1,base+size-hit-1,witness.pattern))
        {
            LOG("Physical crouch camera title=%u: cached-height witness missing/ambiguous; correction stays stock",unsigned(title));
            return;
        }
        if(witness.rva==0x4a0d14||witness.rva==0x62e0a4) selector=hit;
        ++matched;
    }
    if(matched!=5||!selector) return;
    const bool reach=title==GameTitle::HaloReach;
    const uintptr_t pages=sig::RipTarget(selector+(reach?0x53:0x52),selector+(reach?0x57:0x56));
    const uintptr_t table=sig::RipTarget(selector+(reach?0x67:0x68),selector+(reach?0x6b:0x6c));
    if(pages<base||pages+16*sizeof(uintptr_t)>base+size||table<base||table+sizeof(void*)>base+size) return;
    binding.pages=reinterpret_cast<const uintptr_t*>(pages);
    binding.tagTable=reinterpret_cast<const uint8_t* const*>(table);
    binding.generation.store(generation,std::memory_order_release);
    LOG("Physical crouch camera title=%u: own-title cached height and animation-camera selector verified; read-only bounded correction",unsigned(title));
}

const uint8_t* CachedCrouchLocalUnit(GameTitle title,uint32_t& owner)
{
    if(title==GameTitle::HaloReach)
    {
        if(!g_reachCamera.playerUnitByOutputUser) return nullptr;
        owner=g_reachCamera.playerUnitByOutputUser(0);
        uint8_t kind=255;
        const auto* unit=ReachVehicleObjectData(owner,kind);
        return unit&&kind==0&&*reinterpret_cast<const uint32_t*>(unit+0x14)==UINT32_MAX?unit:nullptr;
    }
    Halo4VehicleInputState state{};
    if(!Halo4ReadVehicleInput(state)||state.seated||state.parent!=UINT32_MAX) return nullptr;
    owner=state.unit;
    const auto** slots=reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
    const auto* tls=slots?slots[*g_halo4EngineTlsIndex]:nullptr;
    return tls?Halo4VehicleObject(Halo4VehicleRead<const uint8_t*>(tls,0x18),owner,1):nullptr;
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// Why Reach's last correction was not applied, as bits (0 = applied), with the native reduction and
// the physical depth it saw, so a correction that stays 0 names its gate (dev builds only):
// 0x001 not player-controlled   0x002 physical crouch off   0x004 positional off
// 0x008 binding not proven for this generation   0x010 not the active title
// 0x020 no fresh crouch request from the input hook   0x040 cinematic not player-controlled / stale
// 0x080 unit or crouch-height reduction unreadable   0x100 lease gave no correction (not requested,
// not engaged, an identity/epoch mismatch, or no physical depth)   0x200 identity changed before the
// write   0x400 fault
std::atomic<uint32_t> g_reachCachedCrouchWhy{0};
std::atomic<float> g_reachCachedCrouchReduction{0.0f}, g_reachCachedCrouchDown{0.0f},
    g_reachCachedCrouchApplied{0.0f};
// For 0x080: the reader's failing step and the heights it read (physical_crouch_cached_read.h).
std::atomic<int> g_reachCachedCrouchStep{0}, g_reachCachedCrouchPhysics{0}, g_reachCachedCrouchAnimation{0};
std::atomic<float> g_reachCachedCrouchStanding{0.0f}, g_reachCachedCrouchCrouched{0.0f},
    g_reachCachedCrouchCurrent{0.0f}, g_reachCachedCrouchScale{0.0f};
#endif

void ApplyCachedPhysicalCrouch(GameTitle title,float* position,float physicalDown,bool playerControlled)
{
    static thread_local PhysicalCrouchCameraLease leases[2];
    const bool reach=title==GameTitle::HaloReach;
    auto& lease=leases[reach?0:1];
    auto& binding=reach?g_reachCrouchCamera:g_halo4CrouchCamera;
    const auto& layout=reach?physical_crouch_cached::reach:physical_crouch_cached::halo4;
    const auto generation=TitleAdapter_GetGeneration(title);
    PhysicalCrouchCameraRequest request{};
    // Every gate is a plain load, so evaluating them all (for the dev reason bits) changes nothing.
    uint32_t why=(!playerControlled?0x001u:0u)|(!g_config.physical_crouch?0x002u:0u)|
        (!g_positional.load()?0x004u:0u)|
        (!generation||binding.generation.load(std::memory_order_acquire)!=generation?0x008u:0u)|
        (TitleAdapter_GetActiveTitle()!=title?0x010u:0u);
    if(!why&&!PhysicalCrouchCamera_Read(request)) why|=0x020u;
    float reduction=0,correction=0;
    physical_crouch_cached::ReductionDiag diag{};
    if(why) lease.Reset();
    else
    {
        __try
        {
            const auto cine=TitleAdapter_GetCinematicControlPublication(title);
            const auto now=GetTickCount64();
            uint32_t owner=UINT32_MAX;
            const uint8_t* unit=nullptr;
            const uint8_t* table=nullptr;
            const uint8_t* definition=nullptr;
            if(cine.generation!=generation||cine.state!=CinematicControlState::PlayerControlled||
                !cine.heartbeatMs||now<cine.heartbeatMs||now-cine.heartbeatMs>100)
                why|=0x040u;
            else
            {
                unit=CachedCrouchLocalUnit(title,owner);
                table=binding.tagTable?*binding.tagTable:nullptr;
                if(!physical_crouch_cached::Reduction(layout,unit,table,binding.pages,reduction,definition,
                        &diag))
                    why|=0x080u;
                else
                    PhysicalCrouchCamera_NoteNativeDrop(title,reduction);   // dev: the game's own crouch
            }
            if(why)
                lease.Reset();
            else
            {
                const auto tag=physical_crouch_cached::Read<uint32_t>(unit,layout.tag);
                correction=lease.Update(request,title,generation,VR_PhysicalCrouchEpoch(),now,
                    owner,reinterpret_cast<uintptr_t>(unit),reduction,physicalDown);
                uint32_t currentOwner=UINT32_MAX;
                if(!(correction>0)) why|=0x100u;
                else if(CachedCrouchLocalUnit(title,currentOwner)==unit&&currentOwner==owner&&
                    physical_crouch_cached::Read<uint32_t>(unit,layout.tag)==tag&&*binding.tagTable==table&&
                    physical_crouch_cached::Decode(binding.pages,
                        physical_crouch_cached::Read<uint32_t>(table,(tag&0xffff)*8+4))==definition&&
                    TitleAdapter_GetGeneration(title)==generation)
                    position[2]+=correction;
                else why|=0x200u;
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {lease.Reset();why|=0x400u;}
    }
    // Dev: where this title's read stopped, on the shared CROUCH line (the reader step in bits 12+). A
    // correction that is not needed (standing: 0x100) is not a miss.
    if(why&~0x100u) PhysicalCrouchCamera_NoteNativeMiss(title,why|(uint32_t(diag.step)<<12));
    PhysicalCrouchCamera_NoteApplied(title,why?0.0f:correction);
#if HALOMCCVR_DEV_DIAGNOSTICS
    if(reach)
    {
        g_reachCachedCrouchWhy.store(why,std::memory_order_relaxed);
        g_reachCachedCrouchReduction.store(reduction,std::memory_order_relaxed);
        g_reachCachedCrouchDown.store(physicalDown,std::memory_order_relaxed);
        g_reachCachedCrouchApplied.store(why?0.0f:correction,std::memory_order_relaxed);
        g_reachCachedCrouchStep.store(diag.step,std::memory_order_relaxed);
        g_reachCachedCrouchPhysics.store(diag.physics,std::memory_order_relaxed);
        g_reachCachedCrouchAnimation.store(diag.animation,std::memory_order_relaxed);
        g_reachCachedCrouchStanding.store(diag.standing,std::memory_order_relaxed);
        g_reachCachedCrouchCrouched.store(diag.crouched,std::memory_order_relaxed);
        g_reachCachedCrouchCurrent.store(diag.current,std::memory_order_relaxed);
        g_reachCachedCrouchScale.store(diag.scale,std::memory_order_relaxed);
    }
#else
    (void)reduction;(void)diag;
#endif
}

// Cold worker (dev builds only): the Reach correction's gate, logged when it changes and once a second
// while a correction is being applied.
void ReportReachCachedCrouch()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    if(!g_config.physical_crouch) return;
    static uint64_t s_last=0;
    static uint32_t s_lastWhy=UINT32_MAX;
    const auto now=GetTickCount64();
    const uint32_t why=g_reachCachedCrouchWhy.load(std::memory_order_relaxed);
    const float applied=g_reachCachedCrouchApplied.load(std::memory_order_relaxed);
    if(why==s_lastWhy&&!(applied>0.0f)) return;
    if(why==s_lastWhy&&s_last&&now>=s_last&&now-s_last<1000) return;
    s_last=now;
    s_lastWhy=why;
    const auto mm=[](float wu){return double(wu/kReachWorldUnitsPerMeter*1000.0f);};
    LOG("REACHCROUCHCAM why=0x%03X native crouch drop %.0f mm, physical depth %.0f mm, applied %.0f mm; "
        "reader step %d (physics +0x%X animation +0x%X; standing %.4f crouched %.4f current %.4f scale %.4f)",
        why,mm(g_reachCachedCrouchReduction.load(std::memory_order_relaxed)),
        mm(g_reachCachedCrouchDown.load(std::memory_order_relaxed)),mm(applied),
        g_reachCachedCrouchStep.load(std::memory_order_relaxed),
        unsigned(g_reachCachedCrouchPhysics.load(std::memory_order_relaxed)),
        unsigned(g_reachCachedCrouchAnimation.load(std::memory_order_relaxed)),
        double(g_reachCachedCrouchStanding.load(std::memory_order_relaxed)),
        double(g_reachCachedCrouchCrouched.load(std::memory_order_relaxed)),
        double(g_reachCachedCrouchCurrent.load(std::memory_order_relaxed)),
        double(g_reachCachedCrouchScale.load(std::memory_order_relaxed)));
#endif
}
