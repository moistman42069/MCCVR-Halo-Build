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

void ApplyCachedPhysicalCrouch(GameTitle title,float* position,float physicalDown,bool playerControlled)
{
    static thread_local PhysicalCrouchCameraLease leases[2];
    const bool reach=title==GameTitle::HaloReach;
    auto& lease=leases[reach?0:1];
    auto& binding=reach?g_reachCrouchCamera:g_halo4CrouchCamera;
    const auto& layout=reach?physical_crouch_cached::reach:physical_crouch_cached::halo4;
    const auto generation=TitleAdapter_GetGeneration(title);
    PhysicalCrouchCameraRequest request{};
    if(!playerControlled||!g_config.physical_crouch||!g_positional.load()||
        !generation||binding.generation.load(std::memory_order_acquire)!=generation||
        TitleAdapter_GetActiveTitle()!=title||!PhysicalCrouchCamera_Read(request))
    {lease.Reset();return;}
    __try
    {
        const auto cine=TitleAdapter_GetCinematicControlPublication(title);
        const auto now=GetTickCount64();
        if(cine.generation!=generation||cine.state!=CinematicControlState::PlayerControlled||
            !cine.heartbeatMs||now<cine.heartbeatMs||now-cine.heartbeatMs>100)
        {lease.Reset();return;}
        uint32_t owner=UINT32_MAX;
        const auto* unit=CachedCrouchLocalUnit(title,owner);
        const auto* table=binding.tagTable?*binding.tagTable:nullptr;
        const uint8_t* definition=nullptr;
        float reduction=0;
        if(!physical_crouch_cached::Reduction(layout,unit,table,binding.pages,reduction,definition))
        {lease.Reset();return;}
        const auto tag=physical_crouch_cached::Read<uint32_t>(unit,layout.tag);
        const float correction=lease.Update(request,title,generation,VR_PhysicalCrouchEpoch(),now,
            owner,reinterpret_cast<uintptr_t>(unit),reduction,physicalDown);
        uint32_t currentOwner=UINT32_MAX;
        if(correction>0&&CachedCrouchLocalUnit(title,currentOwner)==unit&&currentOwner==owner&&
            physical_crouch_cached::Read<uint32_t>(unit,layout.tag)==tag&&*binding.tagTable==table&&
            physical_crouch_cached::Decode(binding.pages,
                physical_crouch_cached::Read<uint32_t>(table,(tag&0xffff)*8+4))==definition&&
            TitleAdapter_GetGeneration(title)==generation)
            position[2]+=correction;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {lease.Reset();}
}
