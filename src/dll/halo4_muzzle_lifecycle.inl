// H4EK-first firing/targeting/ownership witnesses matched to pinned Halo4.
// Five optional hooks retire before the camera core and native seat reader.
bool RemoveHalo4Muzzle()
{
    auto& feature=g_halo4Barrel;feature.enabled.store(false,std::memory_order_release);
    g_halo4BarrelBindingsReady.store(false,std::memory_order_release);
    bool any=false;
    for(auto* target:feature.targets)
    {
        if(!target)continue;any=true;
        const auto status=MCCVR_DisableHookForRetirement(target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED)
        {LOG("Halo4 muzzle CleanupRequired: disable failed");return false;}
    }
    if(!any)return true;
    const void* functions[]{reinterpret_cast<const void*>(&Halo4MuzzleFireDetour),
        reinterpret_cast<const void*>(&Halo4MuzzleQueryDetour),reinterpret_cast<const void*>(&Halo4MuzzleViewDetour),
        reinterpret_cast<const void*>(&Halo4MuzzleMarkersDetour),reinterpret_cast<const void*>(&Halo4MuzzleAimDetour)};
    void** originals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.queryOriginal),reinterpret_cast<void**>(&feature.viewOriginal),
        reinterpret_cast<void**>(&feature.markersOriginal),reinterpret_cast<void**>(&feature.aimOriginal)};
    const void* trampolines[5]{};for(size_t i=0;i<5;++i)trampolines[i]=*originals[i];
    if(!WaitForNativeDetourQuiescence(functions,trampolines,5,feature.callbacks))
    {LOG("Halo4 muzzle CleanupRequired: callbacks or ingress busy");return false;}
    for(size_t i=0;i<5;++i)
    {
        if(!feature.targets[i])continue;
        const auto status=MH_RemoveHook(feature.targets[i]);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED)
        {LOG("Halo4 muzzle CleanupRequired: removal failed");return false;}
        feature.targets[i]=nullptr;*originals[i]=nullptr;
    }
    return true;
}
// THE LANE'S PROOF OF THE IMAGE, taken before any of this mod's Halo 4 hooks is enabled (Halo4MuzzlePreflight, from
// the camera install, ahead of InstallHalo4Vrik).
//
// The proof is a set of byte patterns at fixed places, and one of them is the HEAD of the first-person packet
// producer (+0x3B1B4C). The avatar lane hooks that producer (InstallHalo4Vrik: "exact local first-person packet
// producer hooked"), so once it has run the head reads E9 <jump> instead of the game's own bytes and the pattern
// does not match. A proof taken after that hook logs "StockFallback: missing/ambiguous binding +3B1B4C" at every
// Halo 4 level and costs Halo 4 its shots from the muzzle, its fire re-aim, its seat sight and its seated
// convergence (every shot then reads "not ours", "FIRES 0").
// A signature is a statement about the GAME's code: it has to be read before we rewrite that code.
struct Halo4MuzzleImageProof
{
    uintptr_t base=0;
    uint32_t generation=0,failedRva=0;
    bool proven=false,failedEdge=false;
};
Halo4MuzzleImageProof g_halo4MuzzleImageProof;
bool ProveHalo4MuzzleImage(uintptr_t base,size_t size,uint32_t& failedRva,bool& failedEdge)
{
    failedRva=0;failedEdge=false;
    if(size<=0x619607)return false;
    struct Binding {uint32_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x6176B8,"40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 88 DC FF FF B8 78 24 00 00 E8 39 91 44 00 48 2B E0 0F 29 B4 24 60 24"},
        {0x1DB840,"48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FE FF FF 48 81 EC 10 02 00 00 0F 29 70 B8 0F 29 78 A8"},
        {0x1DC9E4,"48 8B C4 48 89 58 08 48 89 70 18 55 57 41 56 48 8D 68 B1 48 81 EC B0 00 00 00 0F 29 70 D8 49 8B F1 0F 29 78 C8 8B F9 0F"},
        {0x5D5B74,"48 89 5C 24 10 66 44 89 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 E4 8B F1 49 8B F8 44 8B FA"},
        {0x5F3510,"48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 C9 48 81 EC D8 00 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88"},
        {0x5FA1C4,"44 8B 05 4D D0 A5 00 44 0F B7 CA 65 48 8B 04 25 58 00 00 00 BA 18 00 00 00 4A 8B 04 C0 4C 8B 04 02 83 C8 FF 66 41 3B C1"},
        {0x5F9E20,"44 8B 05 F1 D3 A5 00 65 48 8B 04 25 58 00 00 00 48 63 D2 41 BA 18 00 00 00 4A 8B 04 C0 44 0F B7 C1 4D 8B 14 02 4F 8D 0C"},
        {0x612238,"8B 15 DA 4F A4 00 65 48 8B 04 25 58 00 00 00 0F B7 C9 41 B8 18 00 00 00 48 8B 04 D0 48 8D 14 49 4D 8B 04 00 49 8B 40 50"},
        {0x5F1ABC,"65 48 8B 04 25 58 00 00 00 44 8B C1 8B 0D 4A 57 A6 00 BA 18 00 00 00 48 8B 04 C8 48 8B 0C 02 4C 8B 49 50 41 0F B7 C8 48"},
        {0x1CD454,"40 53 48 83 EC 20 48 8B D9 83 CA FF 8B 49 08 E8 98 CF 40 00 80 3B 00 4C 8B C8 74 0A 48 85 C0 74 05 41 B0 01 EB 03 45 32"},
        {0x1DC86C,"4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 49 89 7B 20 41 56 48 83 EC 60 48 8B 84 24 B8 00 00 00 44 8B D2 48 8B AC 24"},
        {0x3B1B4C,"48 8B C4 4C 89 48 20 44 89 40 18 89 50 10 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 C8 FE FF FF 48 81 EC F8"},
        {0x3B1F11,"8B 4B D0 48 89 44 24 28 89 7C 24 20 E8 42 76 00 00 4C 8B 2D 87 A1 CC 00 45 85 FF 75 32 48 85 F6 74 2D 4D 85 E4 74 28 8B"},
        {0x610F6C,"45 33 C0 4C 63 CA 83 F9 FF 0F 84 97 00 00 00 41 83 F9 01 0F 87 8D 00 00 00 44 8B 05 8C 62 A4 00 4C 8D 15 ED 91 35 04 65"},
    };
    for(const auto& binding:bindings)
    {
        const auto match=sig::Find(base,size,binding.pattern);
        if(match!=base+binding.rva||sig::Find(match+1,base+size-match-1,binding.pattern))
        {failedRva=binding.rva;return false;}
    }
    struct Edge {uint32_t call,target;};
    constexpr Edge edges[]{
        {0x6179B2,0x5D5B74},
        {0x617E90,0x5F3510},
        {0x618103,0x1DCB8C},
        {0xA364C,0x1DB840},
        {0xA36AE,0x1DB840},
        {0xA3723,0x1DB840},
        {0x1DB9A1,0x1DC9E4},
        {0x1DBDB1,0x1DC9E4},
        {0x1DC8BF,0x1DC9E4},
        {0x1DC42B,0x1DC86C},
    };
    for(const auto& edge:edges)
    {
        const auto* call=reinterpret_cast<const uint8_t*>(base+edge.call);
        if(call[0]!=0xE8||base+edge.call+5+*reinterpret_cast<const int32_t*>(call+1)!=base+edge.target)
        {failedRva=edge.call;failedEdge=true;return false;}
    }
    return true;
}
// Cold, on the title's install thread, before the first Halo 4 hook of the optional lanes is enabled.
void Halo4MuzzlePreflight(uintptr_t base,size_t size,uint32_t generation)
{
    auto& proof=g_halo4MuzzleImageProof;
    proof=Halo4MuzzleImageProof{};
    if(!base||!generation)return;
    proof.base=base;proof.generation=generation;
    proof.proven=ProveHalo4MuzzleImage(base,size,proof.failedRva,proof.failedEdge);
}
bool InstallHalo4Muzzle(uintptr_t base,size_t size,uint32_t generation)
{
    auto& feature=g_halo4Barrel;
    for(auto* target:feature.targets)if(target)return false;
    if(!generation||size<=0x619607||!g_halo4VehicleInput.ready.load(std::memory_order_acquire))return false;
    // The image as it stood before our own hooks; a caller that took no preflight for this image proves it now.
    uint32_t failedRva=0;bool failedEdge=false,proven=false;
    const auto& proof=g_halo4MuzzleImageProof;
    if(proof.base==base&&proof.generation==generation)
    {proven=proof.proven;failedRva=proof.failedRva;failedEdge=proof.failedEdge;}
    else proven=ProveHalo4MuzzleImage(base,size,failedRva,failedEdge);
    if(!proven)
    {
        if(failedEdge)LOG("Halo4 muzzle StockFallback: changed native edge +%X",failedRva);
        else LOG("Halo4 muzzle StockFallback: missing/ambiguous binding +%X",failedRva);
        return false;
    }
    feature.base=base;feature.generation=generation;feature.installedAtMs=GetTickCount64();feature.faulted.store(false);
    struct Hook {uint32_t rva;void* detour;void** original;};
    const Hook hooks[]{
        {0x6176B8,reinterpret_cast<void*>(&Halo4MuzzleFireDetour),reinterpret_cast<void**>(&feature.fireOriginal)},
        {0x1DB840,reinterpret_cast<void*>(&Halo4MuzzleQueryDetour),reinterpret_cast<void**>(&feature.queryOriginal)},
        {0x1DC9E4,reinterpret_cast<void*>(&Halo4MuzzleViewDetour),reinterpret_cast<void**>(&feature.viewOriginal)},
        {0x5D5B74,reinterpret_cast<void*>(&Halo4MuzzleMarkersDetour),reinterpret_cast<void**>(&feature.markersOriginal)},
        {0x5F3510,reinterpret_cast<void*>(&Halo4MuzzleAimDetour),reinterpret_cast<void**>(&feature.aimOriginal)}};
    for(size_t i=0;i<5;++i)
    {
        auto* target=reinterpret_cast<void*>(base+hooks[i].rva);
        if(MH_CreateHook(target,hooks[i].detour,hooks[i].original)!=MH_OK)
        {LOG("Halo4 muzzle StockFallback: hook creation failed");(void)RemoveHalo4Muzzle();return false;}
        feature.targets[i]=target;
    }
    // Each detour forwards stock until the complete set is enabled.
    for(auto* target:feature.targets)
        if(MH_EnableHook(target)!=MH_OK)
        {LOG("Halo4 muzzle StockFallback: hook enable failed");(void)RemoveHalo4Muzzle();return false;}
    feature.enabled.store(true,std::memory_order_release);g_halo4BarrelBindingsReady.store(true,std::memory_order_release);
    LOG("Halo4 muzzle Installed: optional committed FP barrel with native targeting/obstruction; option=%d",Sight_GunBarrelAim()?1:0);
    return true;
}
void ReportHalo4Muzzle()
{
    auto& feature=g_halo4Barrel;if(!feature.targets[0])return;
    LOG("Halo4 barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu queries=%llu restoreRefused=%llu option=%d",
        feature.enabled.load()?1:0,feature.faulted.load()?1:0,feature.applied.exchange(0),feature.refused.exchange(0),
        feature.queries.exchange(0),feature.restoreRefused.exchange(0),Sight_GunBarrelAim()?1:0);
}
