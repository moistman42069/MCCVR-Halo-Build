// HREK-first witnesses matched to pinned Reach. Four optional hooks share
// the installed core unit-adjust trampoline; retire these before that core.
bool RemoveReachMuzzle()
{
    auto& feature=g_reachBarrel;feature.enabled.store(false,std::memory_order_release);
    g_reachBarrelBindingsReady.store(false,std::memory_order_release);
    bool any=false;
    for(auto* target:feature.targets)
    {
        if(!target)continue;any=true;
        const auto status=MCCVR_DisableHookForRetirement(target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED)
        {LOG("Reach muzzle CleanupRequired: disable failed");return false;}
    }
    if(!any)return true;
    const void* functions[]{reinterpret_cast<const void*>(&ReachMuzzleFireDetour),
        reinterpret_cast<const void*>(&ReachMuzzleQueryDetour),reinterpret_cast<const void*>(&ReachMuzzleViewDetour),
        reinterpret_cast<const void*>(&ReachMuzzleMarkersDetour)};
    void** originals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.queryOriginal),reinterpret_cast<void**>(&feature.viewOriginal),
        reinterpret_cast<void**>(&feature.markersOriginal)};
    const void* trampolines[4]{};for(size_t i=0;i<4;++i)trampolines[i]=*originals[i];
    if(!WaitForNativeDetourQuiescence(functions,trampolines,4,feature.callbacks))
    {LOG("Reach muzzle CleanupRequired: callbacks or ingress busy");return false;}
    for(size_t i=0;i<4;++i)
    {
        if(!feature.targets[i])continue;
        const auto status=MH_RemoveHook(feature.targets[i]);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED)
        {LOG("Reach muzzle CleanupRequired: removal failed");return false;}
        feature.targets[i]=nullptr;*originals[i]=nullptr;
    }
    return true;
}
// THE LANE'S PROOF OF THE IMAGE, taken before the core hooks the unit-adjust (ReachMuzzlePreflight, from the camera
// install, ahead of the "Reach vehicle shot line" hook).
//
// One of the fourteen patterns is the HEAD of the unit-adjust (+0x484F24). This lane runs on that function's
// trampoline, so it installs only AFTER the core has hooked it -- and by then the head reads `E9 <jump>`. A
// proof taken that late does not match, and the lane logs "StockFallback: missing/ambiguous binding +484F24" at
// every Reach level: no committed first-person barrel, no "shots leave the visible muzzle", and no fire scope
// (the fire detour is where a fire's shooter mark and fire eye begin). The pattern is
// unique at +0x484F24 on the image as shipped. Same rule as Halo 4's (halo4_muzzle_lifecycle.inl):
// a signature is a statement about the GAME's code, so it is read before we rewrite that code. sig::Find also
// reads our own enabled hooks as the bytes they replaced (sigscan.h), which is the net under this order.
struct ReachMuzzleImageProof
{
    uintptr_t base=0;
    uint32_t generation=0,failedRva=0;
    bool proven=false,failedEdge=false;
};
ReachMuzzleImageProof g_reachMuzzleImageProof;
bool ProveReachMuzzleImage(uintptr_t base,size_t size,uint32_t& failedRva,bool& failedEdge)
{
    failedRva=0;failedEdge=false;
    if(size<=0x4C4924)return false;
    struct Binding {uint32_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x4C2710,"48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 D9 FF FF B8 40 27 00 00 E8 9E 9B 3A 00 48 2B E0 0F 29 B4"},
        {0x10E970,"48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FE FF FF 48 81 EC E0 01 00 00 0F 29 70 B8 0F 29 78 A8"},
        {0x10FA74,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30 49 8B F1 0F B6 DA 49 8B E8 8B F9 E8 D7 B1 37"},
        {0x47044C,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 D2 8B"},
        {0x484F24,"48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 C9 48 81 EC D8 00 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88"},
        {0x2B1234,"BA A0 06 00 00 4E 8B 0C C0 48 63 C1 48 69 C8 A8 53 00 00 33 C0 49 03 0C 11 8B D0 48 83 C1 3C 44 39 19 74 14 FF C0 48 FF C2 48 81 C1 78 29 00 00 48 83 FA 02 7C E9"},
        {0x48B3D0,"83 C8 FF 44 0F B7 CA 66 3B C2 74 38 44 8B 05 35 C7 78 00 65 48 8B 04 25 58 00 00 00 BA 10 00 00 00 0F B7 C9 4A 8B 04 C0 4C 8B 04 10 48 8D 14 49 49 0F BF C9 49 8B 40 50 48 8B 44 D0 10 8B 84 88"},
        {0x4C2F57,"48 8D 82 BB 02 00 00 48 83 E0 FC 48 89 85 A8 00 00 00 F6 40 24 02"},
        {0x48379E,"44 8B 81 90 03 00 00 41 83 F8 FF 75 E3 41 8B C1 C3"},
        {0x4C2A4F,"0F BE 90 4B 03 00 00 E8 75 89 FC FF"},
        {0x4C31B0,"0F BE 91 4A 03 00 00 8B CB E8 12 82 FC FF"},
        {0x4BE0E8,"8B 91 2C 03 00 00 3B D0 74 03 8B C2 C3 80 B9 A9 01 00 00 00 74 06 8B 81 B4 01 00 00 C3"},
        {0x106F04,"40 53 48 83 EC 20 33 DB 38 19 74 12 8B 49 08 83 CA FF E8 B1 CA 36 00 48 85 C0 74 02 B3 01 8A C3 48 83 C4 20 5B"},
        {0x10F8F4,"4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 49 89 7B 20 41 56 48 83 EC 60 48 8B 84 24 B8 00 00 00 41 8A F0 48 8B AC 24"},
    };
    for(const auto& binding:bindings)
    {
        const auto match=sig::Find(base,size,binding.pattern);
        if(match!=base+binding.rva||sig::Find(match+1,base+size-match-1,binding.pattern))
        {failedRva=binding.rva;return false;}
    }
    struct Edge {uint32_t call,target;};
    constexpr Edge edges[]{
        {0x4C2A9B,0x47044C},
        {0x4C303A,0x484F24},
        {0x4C3293,0x10FB94},
        {0x5EF44,0x10E970},
        {0x10EAC8,0x10FA74},
        {0x10EEEC,0x10FA74},
        {0x10F94F,0x10FA74},
        {0x10F583,0x10F8F4},
    };
    for(const auto& edge:edges)
    {
        const auto* call=reinterpret_cast<const uint8_t*>(base+edge.call);
        if(call[0]!=0xE8||base+edge.call+5+*reinterpret_cast<const int32_t*>(call+1)!=base+edge.target)
        {failedRva=edge.call;failedEdge=true;return false;}
    }
    return true;
}
// Cold, on the title's install thread, before the unit-adjust hook is enabled.
void ReachMuzzlePreflight(uintptr_t base,size_t size,uint32_t generation)
{
    auto& proof=g_reachMuzzleImageProof;
    proof=ReachMuzzleImageProof{};
    if(!base||!generation)return;
    proof.base=base;proof.generation=generation;
    proof.proven=ProveReachMuzzleImage(base,size,proof.failedRva,proof.failedEdge);
}
bool InstallReachMuzzle(uintptr_t base,size_t size,uint32_t generation)
{
    auto& feature=g_reachBarrel;
    for(auto* target:feature.targets)if(target)return false;
    if(!generation||size<=0x4C4924||!g_origReachUnitAdjust||!g_reachUnitAdjustTarget||
        !g_reachVehicleShotRedirectEnabled.load(std::memory_order_acquire))return false;
    // The image as it stood before the core's hooks; a caller that took no preflight for this image proves it now
    // (sig::Find reads through our own hooks, and says so in the log).
    uint32_t failedRva=0;bool failedEdge=false,proven=false;
    const auto& proof=g_reachMuzzleImageProof;
    if(proof.base==base&&proof.generation==generation)
    {proven=proof.proven;failedRva=proof.failedRva;failedEdge=proof.failedEdge;}
    else proven=ProveReachMuzzleImage(base,size,failedRva,failedEdge);
    if(!proven)
    {
        if(failedEdge)LOG("Reach muzzle StockFallback: changed native edge +%X",failedRva);
        else LOG("Reach muzzle StockFallback: missing/ambiguous binding +%X",failedRva);
        return false;
    }
    feature.base=base;feature.generation=generation;feature.installedAtMs=GetTickCount64();feature.faulted.store(false);
    struct Hook {uint32_t rva;void* detour;void** original;};
    const Hook hooks[]{
        {0x4C2710,reinterpret_cast<void*>(&ReachMuzzleFireDetour),reinterpret_cast<void**>(&feature.fireOriginal)},
        {0x10E970,reinterpret_cast<void*>(&ReachMuzzleQueryDetour),reinterpret_cast<void**>(&feature.queryOriginal)},
        {0x10FA74,reinterpret_cast<void*>(&ReachMuzzleViewDetour),reinterpret_cast<void**>(&feature.viewOriginal)},
        {0x47044C,reinterpret_cast<void*>(&ReachMuzzleMarkersDetour),reinterpret_cast<void**>(&feature.markersOriginal)}};
    for(size_t i=0;i<4;++i)
    {
        auto* target=reinterpret_cast<void*>(base+hooks[i].rva);
        if(MH_CreateHook(target,hooks[i].detour,hooks[i].original)!=MH_OK)
        {LOG("Reach muzzle StockFallback: hook creation failed");(void)RemoveReachMuzzle();return false;}
        feature.targets[i]=target;
    }
    // Each detour forwards stock until the complete set is enabled.
    for(auto* target:feature.targets)
        if(MH_EnableHook(target)!=MH_OK)
        {LOG("Reach muzzle StockFallback: hook enable failed");(void)RemoveReachMuzzle();return false;}
    feature.enabled.store(true,std::memory_order_release);g_reachBarrelBindingsReady.store(true,std::memory_order_release);
    LOG("Reach muzzle Installed: optional committed FP barrel with native targeting/obstruction; option=%d",Sight_GunBarrelAim()?1:0);
    return true;
}
void ReportReachMuzzle()
{
    auto& feature=g_reachBarrel;if(!feature.targets[0])return;
    LOG("Reach barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu queries=%llu restoreRefused=%llu option=%d",
        feature.enabled.load()?1:0,feature.faulted.load()?1:0,feature.applied.exchange(0),feature.refused.exchange(0),
        feature.queries.exchange(0),feature.restoreRefused.exchange(0),Sight_GunBarrelAim()?1:0);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev: why the window's barrel shots were refused, and whether the lane's query context can reach the fire.
    auto& why=g_reachBarrelWhy;
    static constexpr const char* names[kReachWhyCount]{"lane disabled or faulted","option off","exclusive input",
        "VR aim off","not Reach / another generation","no fire lease","barrel index","marker query for another object",
        "marker count","no unit-adjust","camera not armed or cinematic","no query context on the fire thread",
        "query context stale (over 100 ms, or another generation)","no target storage","weapons unreadable",
        "no tracking snapshot","weapon slot unresolved","aim hand not valid","no muzzle publication",
        "muzzle not finite after the obstruction check","no converged point","native targeting refused",
        "muzzle published for another unit or weapon","muzzle published in another tracking space",
        "muzzle published over 100 ms ago","muzzle from a later or far older frame than the tracking snapshot",
        "muzzle published for another generation, slot, barrel or hand"};
    char text[640];int used=0;uint32_t total=0;
    for(int i=0;i<kReachWhyCount;++i)
    {
        const uint32_t n=why.why[i].exchange(0,std::memory_order_relaxed);
        if(!n)continue;
        total+=n;
        if(used<int(sizeof(text))-80)
        {
            const int wrote=_snprintf_s(text+used,sizeof(text)-used,_TRUNCATE,"%s%s=%u",used?", ":"",names[i],n);
            if(wrote>0)used+=wrote;
        }
    }
    const uint32_t fires=why.fires.exchange(0),queries=why.queries.exchange(0),captures=why.captures.exchange(0);
    const uint32_t views0=why.captureViews0.exchange(0),views1=why.captureViews1.exchange(0),viewsMany=why.captureViewsMany.exchange(0);
    if(total||fires)
        LOG("Reach barrel refusals (dev): %s; fires %u on thread %u; targeting queries %u (sampled %u: the view asked "
            "0 times %u, once %u, more %u) on thread %u",total?text:"none",fires,why.fireThread.load(),queries,captures,
            views0,views1,viewsMany,why.queryThread.load());
    // And why the first-person palette published no muzzle for the lane to read.
    auto& publish=g_reachMuzzlePublishWhy;
    static constexpr const char* publishNames[kReachPublishCount]{"option off or bindings not ready","no slot-0 view-0 palette",
        "pair scope not armed or another frame","render model unread","model not in the marker table","no owner",
        "another owner than the interpolation captured","no tracked aim hand in the frame's targets","marker node count differs",
        "marker transform refused","fault"};
    char publishText[512];int publishUsed=0;uint32_t publishTotal=0;
    for(int i=0;i<kReachPublishCount;++i)
    {
        const uint32_t n=publish.why[i].exchange(0,std::memory_order_relaxed);
        if(!n)continue;
        publishTotal+=n;
        if(publishUsed<int(sizeof(publishText))-80)
        {
            const int wrote=_snprintf_s(publishText+publishUsed,sizeof(publishText)-publishUsed,_TRUNCATE,"%s%s=%u",
                publishUsed?", ":"",publishNames[i],n);
            if(wrote>0)publishUsed+=wrote;
        }
    }
    const uint32_t publishCalls=publish.calls.exchange(0),publishBarrels=publish.barrels.exchange(0);
    if(total||fires)
        LOG("Reach muzzle publication (dev): %u first-person palettes, %u barrel(s) published; not published: %s; last "
            "model not in the table: checksum 0x%08X with %u nodes",publishCalls,publishBarrels,
            publishTotal?publishText:"none",publish.unknownChecksum.load(),publish.unknownNodes.load());
#endif
}
