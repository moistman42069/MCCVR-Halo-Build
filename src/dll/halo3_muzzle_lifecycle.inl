bool RemoveHalo3Muzzle()
{
    auto& feature=g_halo3Muzzle;
    g_halo3MuzzleBindingsReady.store(false,std::memory_order_release);
    feature.enabled.store(false,std::memory_order_release);
    if(!feature.target)return true;
    const auto disabled=MCCVR_DisableHookForRetirement(feature.target);
    if(disabled!=MH_OK&&disabled!=MH_ERROR_DISABLED&&disabled!=MH_ERROR_NOT_CREATED)
    {LOG("Halo 3 muzzle CleanupRequired: disable failed");return false;}
    const void* functions[]{reinterpret_cast<const void*>(&Halo3MuzzleMarkersDetour)};
    const void* originals[]{reinterpret_cast<const void*>(feature.original)};
    if(!WaitForNativeDetourQuiescence(functions,originals,1,feature.callbacks))
    {LOG("Halo 3 muzzle CleanupRequired: callbacks or ingress busy");return false;}
    const auto removed=MH_RemoveHook(feature.target);
    if(removed!=MH_OK&&removed!=MH_ERROR_NOT_CREATED)
    {LOG("Halo 3 muzzle CleanupRequired: removal failed");return false;}
    feature.target=nullptr;feature.original=nullptr;return true;
}

// WHO HOLDS WHICH WEAPON, for the authored grip point, apart from the muzzle: a host that hooks a function of the
// dual-aim lane (Project Reclaimer, the evaluator) refuses the whole lane and so the muzzle, and the grip point would
// never be published. Only the two FP user/slot bindings this read rests on are proved, at their recorded RVAs.
void BindHalo3GripOwner(uintptr_t base,size_t size)
{
    struct Binding {uint32_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x2C24AC,"B9 68 05 00 00 4A 8B 14 D0 4D 69 D1 30 24 00 00 49 69 EC BC 11 00 00 48 8B 3C 11 49 03 FA 45 33"},
        {0x2C250F,"0F B7 44 2F 3C 48 8D 0C 40 48 8B 42 48 48 8B 4C C8 10 48 8B 05 F0 6A 78 00 0F B7 11 8B 4C D0 04"}};
    g_halo3GripOwnerReady.store(false,std::memory_order_release);
    for(const auto& binding:bindings)
    {
        const uintptr_t match=sig::Find(base,size,binding.pattern);
        if(match!=base+binding.rva||sig::Find(match+1,base+size-match-1,binding.pattern))
        {LOG("Halo 3 grip owner StockFallback: missing/ambiguous binding +%X; a weapon's grip zone is unknown",binding.rva);return;}
    }
    g_halo3GripOwnerReady.store(true,std::memory_order_release);
    LOG("Halo 3 grip owner: bound; the authored grip point is published with or without the dual-aim lane");
}

bool InstallHalo3Muzzle(uintptr_t base,size_t size)
{
    auto& feature=g_halo3Muzzle;
    if(feature.target||!g_halo3Dual.enabled.load()||size<=0x368651)
    {LOG("Halo 3 muzzle StockFallback: firing unavailable or cleanup pending");return false;}
    struct Binding {uint32_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x343D74,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 83 EC 70 45 33 F6 8B E9 49 8B"},
        {0x2C24AC,"B9 68 05 00 00 4A 8B 14 D0 4D 69 D1 30 24 00 00 49 69 EC BC 11 00 00 48 8B 3C 11 49 03 FA 45 33"},
        {0x2C250F,"0F B7 44 2F 3C 48 8D 0C 40 48 8B 42 48 48 8B 4C C8 10 48 8B 05 F0 6A 78 00 0F B7 11 8B 4C D0 04"}};
    for(const auto& binding:bindings)
    {
        const uintptr_t match=sig::Find(base,size,binding.pattern);
        if(match!=base+binding.rva||sig::Find(match+1,base+size-match-1,binding.pattern))
        {LOG("Halo 3 muzzle StockFallback: missing/ambiguous binding +%X",binding.rva);return false;}
    }
    const auto* call=reinterpret_cast<const uint8_t*>(base+0x36864C);
    if(call[0]!=0xE8||base+0x368651+*reinterpret_cast<const int32_t*>(call+1)!=base+0x343D74)
    {LOG("Halo 3 muzzle StockFallback: marker firing edge changed");return false;}
    feature.base=base;feature.generation=g_halo3Dual.generation;feature.faulted.store(false);
    void* target=reinterpret_cast<void*>(base+0x343D74);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Halo3MuzzleMarkersDetour),
        reinterpret_cast<void**>(&feature.original))!=MH_OK)
    {LOG("Halo 3 muzzle StockFallback: marker hook creation failed");return false;}
    feature.target=target;
    if(MCCVR_EnableHook(target)!=MH_OK)
    {LOG("Halo 3 muzzle StockFallback: marker hook enable failed");(void)RemoveHalo3Muzzle();return false;}
    feature.enabled.store(true,std::memory_order_release);
    g_halo3MuzzleBindingsReady.store(true,std::memory_order_release);
    LOG("Halo 3 muzzle Installed: optional committed FP marker, native targeting/obstruction; option=%d",Sight_GunBarrelAim()?1:0);
    return true;
}
