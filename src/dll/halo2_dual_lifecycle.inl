#if HALOMCCVR_DEV_DIAGNOSTICS
// The dev AS SEEN witness (halo2_impact_witness.inl) rides this lane's fire scope and owner proof: in after it,
// out before it.
bool InstallHalo2ImpactWitness(uintptr_t base,size_t size);
bool RemoveHalo2ImpactWitness();
#endif

bool RemoveHalo2DualAim()
{
    auto& feature=g_halo2Dual;
#if HALOMCCVR_DEV_DIAGNOSTICS
    if(!RemoveHalo2ImpactWitness())
    {LOG("Halo 2 dual aim CleanupRequired: the AS SEEN witness is still busy");return false;}
#endif
    feature.enabled.store(false,std::memory_order_release);
    void** targets[]{&feature.fireTarget,&feature.aimTarget,&feature.locationTarget,&feature.cameraTarget};
    bool any=false;
    for(auto target:targets)
    {
        if(!*target)continue;
        any=true;
        const auto status=MCCVR_DisableHookForRetirement(*target);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED && status!=MH_ERROR_NOT_CREATED)
        {LOG("Halo 2 dual aim CleanupRequired: disable failed");return false;}
    }
    if(!any)return RemoveHalo2Muzzle();
    const void* functions[]{reinterpret_cast<const void*>(&Halo2IndependentFireDetour),
        reinterpret_cast<const void*>(&Halo2IndependentAimDetour),
        reinterpret_cast<const void*>(&Halo2IndependentLocationDetour),
        reinterpret_cast<const void*>(&Halo2IndependentCameraDetour)};
    const void* originals[]{reinterpret_cast<const void*>(feature.fireOriginal),
        reinterpret_cast<const void*>(feature.aimOriginal),reinterpret_cast<const void*>(feature.locationOriginal),
        reinterpret_cast<const void*>(feature.cameraOriginal)};
    if(!WaitForNativeDetourQuiescence(functions,originals,4,feature.callbacks))
    {LOG("Halo 2 dual aim CleanupRequired: callbacks or ingress busy");return false;}
    if(!RemoveHalo2Muzzle())return false;
    void** mutableOriginals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.aimOriginal),reinterpret_cast<void**>(&feature.locationOriginal),
        reinterpret_cast<void**>(&feature.cameraOriginal)};
    for(size_t i=0;i<4;++i)
    {
        if(!*targets[i])continue;
        const auto status=MH_RemoveHook(*targets[i]);
        if(status!=MH_OK && status!=MH_ERROR_NOT_CREATED)
        {LOG("Halo 2 dual aim CleanupRequired: removal failed");return false;}
        *targets[i]=nullptr;
        *mutableOriginals[i]=nullptr;
    }
    return true;
}

bool InstallHalo2DualAim(uintptr_t base,size_t size)
{
    auto& feature=g_halo2Dual;
    const auto generation=g_generation.load();
    if(feature.fireTarget || feature.aimTarget || feature.locationTarget || feature.cameraTarget ||
        !generation || size<=0x8F0F98 || !g_aimAssistOriginal.load() ||
        !g_aimAssistViewDirectionOriginal.load())
    {LOG("Halo 2 dual aim StockFallback: native acquisition unavailable or cleanup pending");return false;}
    struct Binding {uint32_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x8E4940,"44 88 4C 24 20 44 89 44 24 18 66 89 54 24 10 89 4C 24 08 55 53 41 55 41 56 48 8D AC 24 A8 E1 FF FF B8 58 1F 00 00 E8 35"},
        {0x8F0F70,"48 8B C4 48 89 58 20 55 56 41 55 48 8D 68 C1 48 81 EC C0 00 00 00 48 89 78 08 4D 8B E9 4C 89 70 10 49 8B F8 4C 89 78 18"},
        {0x6F0E60,"40 53 48 83 EC 20 48 63 D9 83 FB FF 74 22 E8 5D 70 F8 FF 66 83 F8 FF 74 17 48 69 C3 68 03 00 00 48 8D 0D F5 1A F0 00 48"},
        {0x6D4730,"48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20 48 8B EA 8B D9 48 8D 54 24 30 49 8B F0 E8 FA 04 00 00 8B F8"},
        {0x7596A0,"48 89 5C 24 10 44 88 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00 48 8B 05 5D"}};
    for(const auto& binding:bindings)
    {
        uintptr_t match=0;uint32_t count=0;
        if(!CountPatternMatches(base,size,binding.pattern,match,count) || count!=1 || match!=base+binding.rva)
        {LOG("Halo 2 dual aim StockFallback: binding missing/ambiguous +%X",binding.rva);return false;}
    }
    struct Edge {uint32_t call,target;};
    constexpr Edge edges[]{{0x8E4FC8,0x8F0F70},{0x8E527C,0x7596A0},
        {0x759325,0x6F0E60},{0x75934C,0x6C0DF0},{0x7597B8,0x6D4730},{0x6C2987,0x759260}};
    for(const auto& edge:edges)
    {
        const auto* bytes=reinterpret_cast<const uint8_t*>(base+edge.call);
        if(bytes[0]!=0xE8 || base+edge.call+5+*reinterpret_cast<const int32_t*>(bytes+1)!=base+edge.target)
        {LOG("Halo 2 dual aim StockFallback: native edge changed +%X",edge.call);return false;}
    }
    const uint8_t targetField[]{0x49,0x8D,0xB7,0xD4,0x01,0x00,0x00};
    const uint8_t parentField[]{0x41,0x8B,0x87,0x60,0x02,0x00,0x00};
    if(std::memcmp(reinterpret_cast<const void*>(base+0x8E4ED0),targetField,sizeof(targetField)) ||
        std::memcmp(reinterpret_cast<const void*>(base+0x8E4EE1),parentField,sizeof(parentField)))
    {LOG("Halo 2 dual aim StockFallback: native target/parent consumer changed");return false;}
    feature.base=base;feature.generation=generation;feature.installedAtMs=GetTickCount64();
    feature.faulted.store(false);
    struct Hook {uint32_t rva;void* detour;void** original;void** target;};
    const Hook hooks[]{
        {0x8E4940,reinterpret_cast<void*>(&Halo2IndependentFireDetour),reinterpret_cast<void**>(&feature.fireOriginal),&feature.fireTarget},
        {0x8F0F70,reinterpret_cast<void*>(&Halo2IndependentAimDetour),reinterpret_cast<void**>(&feature.aimOriginal),&feature.aimTarget},
        {0x6F0E60,reinterpret_cast<void*>(&Halo2IndependentLocationDetour),reinterpret_cast<void**>(&feature.locationOriginal),&feature.locationTarget},
        {0x6D4730,reinterpret_cast<void*>(&Halo2IndependentCameraDetour),reinterpret_cast<void**>(&feature.cameraOriginal),&feature.cameraTarget}};
    for(const auto& hook:hooks)
    {
        auto* target=reinterpret_cast<void*>(base+hook.rva);
        if(MH_CreateHook(target,hook.detour,hook.original)!=MH_OK)
        {LOG("Halo 2 dual aim StockFallback: creation failed +%X",hook.rva);(void)RemoveHalo2DualAim();return false;}
        *hook.target=target;
    }
    for(const auto& hook:hooks)
        if(MH_EnableHook(*hook.target)!=MH_OK)
        {LOG("Halo 2 dual aim StockFallback: enable failed +%X",hook.rva);(void)RemoveHalo2DualAim();return false;}
    feature.enabled.store(true,std::memory_order_release);
    // Muzzle marker admission is its own optional transaction. Its failure
    // cannot remove working per-hand acquisition or any VR camera path.
    (void)InstallHalo2Muzzle(base,size);
#if HALOMCCVR_DEV_DIAGNOSTICS
    (void)InstallHalo2ImpactWitness(base,size);
#endif
    LOG("Halo 2 dual aim Installed: optional per-hand native acquisition and firing assist; option=%d",Sight_IndependentDualAim()?1:0);
    return true;
}
