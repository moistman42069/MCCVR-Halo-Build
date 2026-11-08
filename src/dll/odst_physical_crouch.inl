std::atomic<uint32_t> g_odstCrouchCameraGeneration{};

void OdstPreparePhysicalCrouchCamera(uintptr_t base,size_t size)
{
    g_odstCrouchCameraGeneration.store(0,std::memory_order_release);
    unsigned admitted=0;
    for(const auto& witness:kCrouchAdditionalWitnesses)
    {
        if(witness.title!=GameTitle::Halo3ODST) continue;
        const auto hit=sig::Find(base,size,witness.pattern);
        if(!hit||hit-base!=witness.rva||sig::Find(hit+1,base+size-hit-1,witness.pattern))
        {
            LOG("ODST physical crouch camera: own-title height witness missing/ambiguous; correction stays stock, camera core unaffected");
            return;
        }
        ++admitted;
    }
    if(admitted!=5) return;
    g_odstCrouchCameraGeneration.store(g_odstRuntimeGeneration.load(),std::memory_order_release);
    LOG("ODST physical crouch camera: own-title height witnesses verified; read-only local-biped correction, bounded below native camera");
}

const uint8_t* OdstPhysicalCrouchUnit(uint32_t owner)
{
    if(!OdstContactObject(owner,true)) return nullptr;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(OdstContactTls()+kOdstTlsObjectTableOffset);
    const auto* entries=*reinterpret_cast<const uint8_t* const*>(table+0x48);
    return *reinterpret_cast<const uint8_t* const*>(entries+(owner&0xffff)*0x18+0x10);
}

void OdstApplyPhysicalCrouchCamera(float* position,float physicalDown,bool playerControlled)
{
    static thread_local PhysicalCrouchCameraLease lease;
    PhysicalCrouchCameraRequest request{};
    const auto generation=g_odstRuntimeGeneration.load(std::memory_order_acquire);
    const uint32_t gateWhy=(!playerControlled?0x1u:0u)|(!g_config.physical_crouch?0x2u:0u)|
        (!g_positional.load()?0x4u:0u)|(!generation?0x8u:0u)|
        (g_odstCrouchCameraGeneration.load(std::memory_order_acquire)!=generation?0x10u:0u)|
        (!g_odstPlayerUnitGetter?0x20u:0u);
    if(gateWhy||!PhysicalCrouchCamera_Read(request))
    {PhysicalCrouchCamera_NoteNativeMiss(GameTitle::Halo3ODST,gateWhy?gateWhy:0x40u);lease.Reset();return;}
    __try
    {
        const uint32_t owner=static_cast<uint32_t>(g_odstPlayerUnitGetter(0));
        const auto* unit=OdstPhysicalCrouchUnit(owner);
        const auto tag=unit?*reinterpret_cast<const uint32_t*>(unit):UINT32_MAX;
        const auto* definition=OdstLoadedTagDefinition(tag);
        float reduction=0.f;
        if(!physical_crouch_native::Odst(unit,definition,reduction))
        {PhysicalCrouchCamera_NoteNativeMiss(GameTitle::Halo3ODST,0x80u);lease.Reset();return;}
        PhysicalCrouchCamera_NoteNativeDrop(GameTitle::Halo3ODST,reduction);   // dev: the game's own crouch
        const float correction=lease.Update(request,GameTitle::Halo3ODST,generation,
            VR_PhysicalCrouchEpoch(),GetTickCount64(),owner,reinterpret_cast<uintptr_t>(unit),
            reduction,physicalDown);
        const bool apply=correction>0.f&&static_cast<uint32_t>(g_odstPlayerUnitGetter(0))==owner&&
            OdstPhysicalCrouchUnit(owner)==unit&&*reinterpret_cast<const uint32_t*>(unit)==tag&&
            OdstLoadedTagDefinition(tag)==definition&&
            g_odstRuntimeGeneration.load(std::memory_order_acquire)==generation;
        if(apply) position[2]+=correction;
        PhysicalCrouchCamera_NoteApplied(GameTitle::Halo3ODST,apply?correction:0.f);   // dev
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {lease.Reset();}
}
