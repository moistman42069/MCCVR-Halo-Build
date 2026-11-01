// Included after Halo2OwnedUnit; no new native hook or engine write.
std::atomic<bool> g_halo2CrouchCameraReady{false};
std::atomic<uintptr_t> g_halo2CrouchTagInstances{},g_halo2CrouchTagData{};
thread_local PhysicalCrouchCameraLease g_halo2CrouchLease;

void Halo2PreparePhysicalCrouchCamera(uintptr_t base,size_t size)
{
    g_halo2CrouchCameraReady.store(false,std::memory_order_release);
    for(const auto& witness:kCrouchAdditionalWitnesses)
    {
        if(witness.title!=GameTitle::Halo2)continue;
        const auto found=sig::Find(base,size,witness.pattern);
        if(found!=base+witness.rva||found>=base+size||
           sig::Find(found+1,base+size-found-1,witness.pattern))
        {LOG("Halo 2 physical crouch camera: StockFallback; witness +%X not unique",witness.rva);return;}
    }
    const auto instances=sig::RipTarget(base+0x927213,base+0x927217);
    const auto data=sig::RipTarget(base+0x927233,base+0x927237);
    if(instances<base||instances>base+size-sizeof(void*)||data<base||data>base+size-sizeof(void*))
    {LOG("Halo 2 physical crouch camera: StockFallback; tag slots out of module");return;}
    g_halo2CrouchTagInstances.store(instances,std::memory_order_relaxed);
    g_halo2CrouchTagData.store(data,std::memory_order_relaxed);
    g_halo2CrouchCameraReady.store(true,std::memory_order_release);
    LOG("Halo 2 physical crouch camera: verified own biped blend; read-only correction enabled");
}

void Halo2ApplyPhysicalCrouchCamera(float* position,float physicalDown,bool allowed) noexcept
{
    PhysicalCrouchCameraRequest request{};
    const uint32_t gateWhy=(!allowed?0x1u:0u)|(!g_config.physical_crouch?0x2u:0u)|
        (!g_halo2CrouchCameraReady.load(std::memory_order_acquire)?0x4u:0u);
    if(gateWhy||!PhysicalCrouchCamera_Read(request))
    {PhysicalCrouchCamera_NoteNativeMiss(GameTitle::Halo2,gateWhy?gateWhy:0x40u);g_halo2CrouchLease.Reset();return;}
    __try
    {
        const auto owner=Halo2OwnedUnit();
        const auto* unit=static_cast<const uint8_t*>(Halo2ObjectFromIndex(owner));
        if(!unit) {g_halo2CrouchLease.Reset();return;}
        const auto tag=physical_crouch_native::Read<uint32_t>(unit,0);
        const auto index=tag&0xFFFF;
        const auto* instances=*reinterpret_cast<const uint8_t* const*>(g_halo2CrouchTagInstances.load());
        const auto* data=*reinterpret_cast<const uint8_t* const*>(g_halo2CrouchTagData.load());
        if(index==0xFFFF||!instances||!data) {g_halo2CrouchLease.Reset();return;}
        const auto offset=physical_crouch_native::Read<int32_t>(instances,index*16+8);
        if(offset<=0) {g_halo2CrouchLease.Reset();return;}
        float reduction=0;
        if(!physical_crouch_native::Halo2(unit,data+offset,reduction)||
           Halo2OwnedUnit()!=owner||Halo2ObjectFromIndex(owner)!=unit||
           physical_crouch_native::Read<uint32_t>(unit,0)!=tag)
        {PhysicalCrouchCamera_NoteNativeMiss(GameTitle::Halo2,0x80u);g_halo2CrouchLease.Reset();return;}
        PhysicalCrouchCamera_NoteNativeDrop(GameTitle::Halo2,reduction);   // dev: the game's own crouch
        const float correction=g_halo2CrouchLease.Update(request,GameTitle::Halo2,g_generation.load(),
            VR_PhysicalCrouchEpoch(),GetTickCount64(),owner,reinterpret_cast<uintptr_t>(unit),reduction,physicalDown);
        position[2]+=correction;
        PhysicalCrouchCamera_NoteApplied(GameTitle::Halo2,correction);   // dev
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {g_halo2CrouchLease.Reset();}
}
