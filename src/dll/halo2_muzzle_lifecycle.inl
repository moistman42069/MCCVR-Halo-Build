bool RemoveHalo2Muzzle()
{
    auto& feature=g_halo2Muzzle;
    feature.enabled.store(false,std::memory_order_release);
    if(!feature.target)return true;
    const auto disabled=MCCVR_DisableHookForRetirement(feature.target);
    if(disabled!=MH_OK&&disabled!=MH_ERROR_DISABLED&&disabled!=MH_ERROR_NOT_CREATED)
    {LOG("Halo 2 muzzle CleanupRequired: disable failed");return false;}
    const void* functions[]{reinterpret_cast<const void*>(&Halo2MuzzleMarkersDetour)};
    const void* originals[]{reinterpret_cast<const void*>(feature.original)};
    if(!WaitForNativeDetourQuiescence(functions,originals,1,feature.callbacks))
    {LOG("Halo 2 muzzle CleanupRequired: callbacks or ingress busy");return false;}
    const auto removed=MH_RemoveHook(feature.target);
    if(removed!=MH_OK&&removed!=MH_ERROR_NOT_CREATED)
    {LOG("Halo 2 muzzle CleanupRequired: removal failed");return false;}
    feature.target=nullptr;feature.original=nullptr;return true;
}

bool InstallHalo2Muzzle(uintptr_t base,size_t size)
{
    auto& feature=g_halo2Muzzle;
    if(feature.target||!g_halo2Dual.enabled.load()||size<=0x8E8A10)
    {LOG("Halo 2 muzzle StockFallback: firing transaction unavailable or cleanup pending");return false;}
    struct Binding {uint32_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x8D6570,"48 83 EC 38 C6 44 24 28 00 C6 44 24 20 00 E8 1D AC FF FF 48 83 C4 38 C3"},
        {0x8D11A0,"48 89 5C 24 10 66 44 89 4C 24 20 55 56 57 41 55 41 56 48 83 EC 60 33 F6 44 8B F1 49 8B D8 44 8B EA 8B F9 0F B7 EE"},
        {0x8E8990,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 05 F2 E9 FC 00 8B D9 48 8B 48 48 48 85 C9 74 06 48 8D 14 01 EB 02 33 D2"},
        {0x8E89E7,"E8 64 E7 FE FF 84 C0 75 06 83 7F 38 FF 75 08 83 7E 14 FF 0F 45 5E 14 48 8B 74 24 38 8B C3"}};
    for(const auto& binding:bindings)
    {
        uintptr_t match=0;uint32_t count=0;
        if(!CountPatternMatches(base,size,binding.pattern,match,count)||count!=1||match!=base+binding.rva)
        {LOG("Halo 2 muzzle StockFallback: missing/ambiguous marker binding +%X",binding.rva);return false;}
    }
    struct Edge {uint32_t call,target;};
    constexpr Edge edges[]{{0x8E4B94,0x8E8990},{0x8E4BAA,0x8D6570},{0x8D657E,0x8D11A0}};
    for(const auto& edge:edges)
    {
        const auto* bytes=reinterpret_cast<const uint8_t*>(base+edge.call);
        if(bytes[0]!=0xE8||base+edge.call+5+*reinterpret_cast<const int32_t*>(bytes+1)!=base+edge.target)
        {LOG("Halo 2 muzzle StockFallback: native marker edge changed +%X",edge.call);return false;}
    }
    feature.base=base;feature.generation=g_halo2Dual.generation;feature.faulted.store(false);
    void* target=reinterpret_cast<void*>(base+0x8D6570);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Halo2MuzzleMarkersDetour),
        reinterpret_cast<void**>(&feature.original))!=MH_OK)
    {LOG("Halo 2 muzzle StockFallback: marker hook creation failed");return false;}
    feature.target=target;
    if(MH_EnableHook(target)!=MH_OK)
    {LOG("Halo 2 muzzle StockFallback: marker hook enable failed");(void)RemoveHalo2Muzzle();return false;}
    feature.enabled.store(true,std::memory_order_release);
    LOG("Halo 2 muzzle Installed: optional committed FP marker, native targeting/obstruction; option=%d",g_config.gun_barrel_aim?1:0);
    return true;
}
