#include "halo3_physical_crouch_reader.inl"

std::atomic<uint32_t> g_halo3CrouchCameraGeneration{};

void Halo3PreparePhysicalCrouchCamera(uintptr_t base,size_t size)
{
    g_halo3CrouchCameraGeneration.store(0,std::memory_order_release);
    // Each witness is independently unique in the pinned module; no hook,
    // engine function call, native state write, or scan on the camera thread.
    static constexpr const char* witnesses[]={
        "41 83 CB FF 44 39 5E 10 0F 85 B0 00 00 00 8B 86 10 01 00 00 41 8D 5B 02 C1 E8 02 84 C3 75 37 44 38 B6 96 00 00 00 75 2E",
        "41 8B CE E8 C5 20 00 00 45 33 C0 84 C0 75 08 F3 0F 10 BF 84 03 00 00",
        "F3 0F 5C F7 F3 41 0F 59 BC 24 EC 02 00 00 F3 41 0F 59 B4 24 E8 02 00 00 F3 0F 58 F7 F3 0F 59 B7 8C 00 00 00",
        "32 C0 80 B9 DE 04 00 00 06 75 16 8B 89 E4 04 00 00 BA 01 00 00 00 C1 E9 04 84 CA 0F B6 C0 0F 45 C2 C3",
        "48 0F BF 87 62 01 00 00 8B 0C 38 83 C1 F9 83 F9 01 77 04 48 8D 77 68"
    };
    for(const auto* pattern:witnesses)
    {
        const auto hit=sig::Find(base,size,pattern);
        if(!hit||sig::Find(hit+1,base+size-hit-1,pattern))
        {
            LOG("H3 physical crouch camera: native height witness missing/ambiguous; correction stays stock, camera core unaffected");
            return;
        }
    }
    g_halo3CrouchCameraGeneration.store(g_halo3RuntimeGeneration.load(),std::memory_order_release);
    LOG("H3 physical crouch camera: own-title height witnesses verified; read-only local-biped correction, bounded below native camera");
}

void Halo3ApplyPhysicalCrouchCamera(float* position,float physicalDown,bool playerControlled)
{
    static thread_local PhysicalCrouchCameraLease lease;
    PhysicalCrouchCameraRequest request{};
    const auto generation=g_halo3RuntimeGeneration.load(std::memory_order_acquire);
    if(!playerControlled||!g_config.physical_crouch||!g_positional.load()||
        !generation||g_halo3CrouchCameraGeneration.load(std::memory_order_acquire)!=generation||
        !g_halo3PlayerUnitGetter||!PhysicalCrouchCamera_Read(request))
    {lease.Reset();return;}
    __try
    {
        const uint32_t owner=static_cast<uint32_t>(g_halo3PlayerUnitGetter(0));
        const auto* unit=Halo3MeleeSelectionObject(owner,0);
        const auto tag=unit?*reinterpret_cast<const uint32_t*>(unit):UINT32_MAX;
        const auto* definition=Halo3LoadedTagDefinition(tag);
        float reduction=0.f;
        if(!Halo3PhysicalCrouchReduction(unit,definition,reduction))
        {lease.Reset();return;}
        const float correction=lease.Update(request,GameTitle::Halo3,generation,
            VR_PhysicalCrouchEpoch(),GetTickCount64(),owner,reinterpret_cast<uintptr_t>(unit),
            reduction,physicalDown);
        if(correction>0.f&&static_cast<uint32_t>(g_halo3PlayerUnitGetter(0))==owner&&
            Halo3MeleeSelectionObject(owner,0)==unit&&
            *reinterpret_cast<const uint32_t*>(unit)==tag&&Halo3LoadedTagDefinition(tag)==definition&&
            g_halo3RuntimeGeneration.load(std::memory_order_acquire)==generation)
            position[2]+=correction;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {lease.Reset();}
}
