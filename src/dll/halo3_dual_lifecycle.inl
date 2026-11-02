bool RemoveHalo3DualAim()
{
    auto& feature = g_halo3Dual;
    feature.enabled.store(false, std::memory_order_release);
    feature.chaseCamera.store(nullptr, std::memory_order_release);   // callers hold the callback count: the wait covers them
    feature.chaseGeneration.store(0, std::memory_order_release);
    void** targets[]{&feature.fireTarget, &feature.aimTarget,&feature.queryTarget,&feature.cameraTarget};
    bool any = false;
    for (auto target : targets)
    {
        if (!*target) continue;
        any = true;
        const auto status = MCCVR_DisableHookForRetirement(*target);
        if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
        { LOG("Halo 3 dual aim CleanupRequired: disable failed"); return false; }
    }
    if (!any)
    {
        // No hook to retire, but the chase path is asked without this lane's hooks and may be mid-ask on a
        // simulation thread. It is unbound above, so no new ask reaches the game; one already inside holds the count.
        if (feature.callbacks.load(std::memory_order_acquire) != 0)
        { LOG("Halo 3 dual aim CleanupRequired: a chase camera ask is still running"); return false; }
        return RemoveHalo3Muzzle();
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo3IndependentFireDetour),
        reinterpret_cast<const void*>(&Halo3IndependentAimDetour),
        reinterpret_cast<const void*>(&Halo3IndependentQueryDetour),
        reinterpret_cast<const void*>(&Halo3IndependentCameraDetour),
        reinterpret_cast<const void*>(&PublishHalo3DualAim)};
    const void* originals[]{reinterpret_cast<const void*>(feature.fireOriginal),
        reinterpret_cast<const void*>(feature.aimOriginal),
        reinterpret_cast<const void*>(feature.queryOriginal),
        reinterpret_cast<const void*>(feature.cameraOriginal),nullptr};
    if (!WaitForNativeDetourQuiescence(functions, originals, 5, feature.callbacks))
    { LOG("Halo 3 dual aim CleanupRequired: callbacks or ingress busy"); return false; }
    if(!RemoveHalo3Muzzle())return false;
    void** mutableOriginals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.aimOriginal),reinterpret_cast<void**>(&feature.queryOriginal),
        reinterpret_cast<void**>(&feature.cameraOriginal)};
    for (size_t index=0;index<4;++index)
    {
        auto target=targets[index];
        if (!*target) continue;
        const auto status=MH_RemoveHook(*target);
        if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
        { LOG("Halo 3 dual aim CleanupRequired: removal failed"); return false; }
        *target = nullptr;
        *mutableOriginals[index]=nullptr;
    }
    feature.fireOriginal = nullptr;
    feature.aimOriginal = nullptr;
    feature.queryOriginal=nullptr;
    feature.cameraOriginal=nullptr;
    return true;
}

// THE EVALUATOR'S CHASE PATH (seat_upstream_logic.h 4b and StockCamera): never hooked, only ever called under the
// lane's callback count (Halo3AskChaseCamera). It is the start of a stock seat's crosshair line, so it is bound
// whether or not this lane's hooks went in -- also in a host that hooks the evaluator's entry itself, where the
// evaluator's own signature no longer reads. Resolved from the evaluator's body, never assumed:
//   * the evaluator is the one expected: its call at 0x2121B8 goes to the seat's third-person test 0x1326E8 (a link
//     a hook on either function's entry leaves in place);
//   * its branch `test eax, eax; jne; call <first person>; jmp; call <chase>` at 0x2121C7 matches;
//   * the chase call lands on a function whose body matches uniquely in the module.
// Unbound: a stock seat's record is solved from the last crosshair query's start, and the log says so.
// ADDR-HYGIENE: resolved (decoded from the evaluator's body) + guarded (the evaluator's own call target, and a
// unique body signature at the decoded target).
void BindHalo3ChaseCamera(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_halo3Dual;
    feature.chaseCamera.store(nullptr, std::memory_order_release);
    feature.chaseGeneration.store(0, std::memory_order_release);
    feature.chaseFaulted.store(false, std::memory_order_release);
    static constexpr char kChaseBody[] =
        "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 50 44 8B 0D ?? ?? ?? ?? 33 DB 65 48 8B "
        "04 25 58 00 00 00 4C 8B FA 48 8B 15 ?? ?? ?? ?? 49 8B E8 44 8B F1";
    const auto* branch = reinterpret_cast<const uint8_t*>(base + 0x2121C7);
    const bool shape = size > 0x2121D7 && branch[0] == 0x85 && branch[1] == 0xC0 && branch[2] == 0x75 &&
        branch[3] == 0x07 && branch[4] == 0xE8 && branch[9] == 0xEB && branch[10] == 0x05 && branch[11] == 0xE8;
    uintptr_t test = 0;
    const uintptr_t testCall = base + seat_view::kHalo3EvaluatorCallRva;
    const bool evaluator = generation && shape &&
        seat_view::ReadCall(reinterpret_cast<const uint8_t*>(testCall), testCall, test) &&
        test == base + seat_view::kHalo3ThirdPersonRva;
    const uintptr_t chase = evaluator ? base + 0x2121D7 + *reinterpret_cast<const int32_t*>(branch + 12) : 0;
    const uintptr_t hit = chase ? sig::Find(base, size, kChaseBody) : 0;
    if (!chase || hit != chase || sig::Find(hit + 1, base + size - hit - 1, kChaseBody))
    {
        LOG("Halo 3 seat stock camera: the evaluator's chase path did not bind (branch %s, the evaluator's call to "
            "the third-person test %s, body %s); a seat's record is solved from the last crosshair query's start, so "
            "only while the gun fires",
            shape ? "matched" : "changed", evaluator ? "matched" : "changed or not read",
            hit ? (hit == chase ? "ambiguous" : "elsewhere") : "missing");
        return;
    }
    feature.chaseGeneration.store(generation, std::memory_order_release);
    feature.chaseCamera.store(reinterpret_cast<Halo3ChaseCameraFn>(chase), std::memory_order_release);
    LOG("Halo 3 seat stock camera: the evaluator's chase path bound at +0x%llX (from the evaluator's call +0x2121D2); "
        "a seat's record is solved from it at every control pass",
        static_cast<unsigned long long>(chase - base));
}

bool InstallHalo3DualAim(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_halo3Dual;
    if (feature.fireTarget || feature.aimTarget || feature.queryTarget || feature.cameraTarget ||
        !generation || size <= 0x369FB9) return false;
    struct Binding { uint32_t rva; const char* pattern; };
    constexpr Binding bindings[]{
        {0x3683A0, "48 8B C4 48 89 58 20 4C 89 40 18 66 89 50 10 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 DD FF FF B8 F0 22 00"},
        {0x3524B0, "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 56 41 57 48 8D 68 C1 48 81 EC C0 00 00 00 44 8B 15 C6 7A 6E 00 48 8B"},
        {0x13B08C, "48 8B C4 48 89 58 08 66 44 89 48 20 4C 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 18 FF FF FF 48 81 EC B0 01 00"},
        {0x212198, "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 83 EC 20 48 8B EA 49 8B F0 48 8D 50 20 8B D9 E8 2B 05 F2 FF 8B F8 4C"},
        {0x13BAD0, "48 8B C4 44 88 48 20 44 89 40 18 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 FE FF FF 48 81 EC E8 01 00 00 44 8B 15"}};
    for (const auto& binding : bindings)
    {
        const uintptr_t hit = sig::Find(base, size, binding.pattern);
        if (hit != base + binding.rva ||
            sig::Find(hit + 1, base + size - hit - 1, binding.pattern))
        { LOG("Halo 3 dual aim StockFallback: missing/ambiguous binding +0x%X", binding.rva); return false; }
    }
    const auto* call = reinterpret_cast<const uint8_t*>(base + 0x368B92);
    if (call[0] != 0xE8 || base + 0x368B97 + *reinterpret_cast<const int32_t*>(call + 1) != base + 0x3524B0)
    { LOG("Halo 3 dual aim StockFallback: native firing call mismatch"); return false; }
    const uint8_t targetField[]{0x4C,0x8D,0xAB,0x18,0x02,0x00,0x00};
    const uint8_t parentField[]{0x44,0x8B,0x83,0xA4,0x02,0x00,0x00};
    if(std::memcmp(reinterpret_cast<const void*>(base+0x368A6A),targetField,sizeof(targetField)) ||
        std::memcmp(reinterpret_cast<const void*>(base+0x368AE5),parentField,sizeof(parentField)))
    {LOG("Halo 3 dual aim StockFallback: native target/controlling-parent consumer changed");return false;}
    struct Edge {uint32_t call,target;};
    constexpr Edge edges[]{{0x13B1D7,0x212198},{0x13BC08,0x212198},
        {0xF4CC8,0x13B08C},{0x368DFE,0x13BAD0}};
    for(const auto& edge:edges)
    {
        const auto* bytes=reinterpret_cast<const uint8_t*>(base+edge.call);
        if(bytes[0]!=0xE8 || base+edge.call+5+*reinterpret_cast<const int32_t*>(bytes+1)!=base+edge.target)
        {LOG("Halo 3 dual aim StockFallback: native query/assist edge mismatch +%X",edge.call);return false;}
    }
    feature.base = base;
    feature.generation = generation;
    feature.installedAtMs = GetTickCount64();
    feature.fireEntries=0;feature.fireReturns=0;feature.fireUnwinds=0;
    feature.fireWithData=0;feature.firePredicted=0;
    feature.faulted.store(false);
    if (MH_CreateHook(reinterpret_cast<void*>(base + 0x3524B0),
            reinterpret_cast<void*>(&Halo3IndependentAimDetour),
            reinterpret_cast<void**>(&feature.aimOriginal)) != MH_OK)
    { LOG("Halo 3 dual aim StockFallback: helper creation failed"); return false; }
    feature.aimTarget = reinterpret_cast<void*>(base + 0x3524B0);
    if (MH_CreateHook(reinterpret_cast<void*>(base + 0x3683A0),
            reinterpret_cast<void*>(&Halo3IndependentFireDetour),
            reinterpret_cast<void**>(&feature.fireOriginal)) != MH_OK)
    {
        LOG("Halo 3 dual aim StockFallback: firing scope creation failed");
        (void)RemoveHalo3DualAim();
        return false;
    }
    feature.fireTarget = reinterpret_cast<void*>(base + 0x3683A0);
    struct Hook {uint32_t rva;void* detour;void** original;void** target;};
    const Hook added[]{
        {0x13B08C,reinterpret_cast<void*>(&Halo3IndependentQueryDetour),reinterpret_cast<void**>(&feature.queryOriginal),&feature.queryTarget},
        {0x212198,reinterpret_cast<void*>(&Halo3IndependentCameraDetour),reinterpret_cast<void**>(&feature.cameraOriginal),&feature.cameraTarget}};
    for(const auto& hook:added)
    {
        auto* target=reinterpret_cast<void*>(base+hook.rva);
        if(MH_CreateHook(target,hook.detour,hook.original)!=MH_OK)
        {LOG("Halo 3 dual aim StockFallback: native query/camera hook creation failed");(void)RemoveHalo3DualAim();return false;}
        *hook.target=target;
    }
    if (MCCVR_EnableHook(feature.cameraTarget)!=MH_OK || MCCVR_EnableHook(feature.queryTarget)!=MH_OK ||
        MCCVR_EnableHook(feature.aimTarget) != MH_OK || MCCVR_EnableHook(feature.fireTarget) != MH_OK)
    {
        LOG("Halo 3 dual aim StockFallback: hook enable failed");
        (void)RemoveHalo3DualAim();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    (void)InstallHalo3Muzzle(base,size);
    LOG("Halo 3 dual aim Installed: optional independent controller rays with native per-shot acquisition; option=%d; single weapon, vehicles and remote units retain native aim",Sight_IndependentDualAim()?1:0);
    return true;
}

// AIM FRAME ZOOM'S READ OF THE GAME'S ZOOM LEVEL when the lane above did not install -- a host that hooks one of
// its other functions itself, as Project Reclaimer hooks the evaluator (+0x212198), refuses the whole lane. The
// aim query's hook alone goes into the lane's own slot, so RemoveHalo3DualAim retires it like the lane's. With
// the lane not enabled its detour captures nothing, stamps nothing and asks nothing of the game: it notes the
// zoom level the game hands its own query and calls through.
// ADDR-HYGIENE: guarded (the query's unique body signature at its recorded RVA, and the native call to it).
bool InstallHalo3ZoomReader(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_halo3Dual;
    if (feature.fireTarget || feature.aimTarget || feature.queryTarget || feature.cameraTarget || !generation ||
        size <= 0x13B100)
        return false;
    static constexpr char kQuery[] =
        "48 8B C4 48 89 58 08 66 44 89 48 20 4C 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 18 FF FF FF 48 81 "
        "EC B0 01 00";
    const uintptr_t hit = sig::Find(base, size, kQuery);
    const auto* call = reinterpret_cast<const uint8_t*>(base + 0xF4CC8);
    if (hit != base + 0x13B08C || sig::Find(hit + 1, base + size - hit - 1, kQuery) || call[0] != 0xE8 ||
        base + 0xF4CC8 + 5 + *reinterpret_cast<const int32_t*>(call + 1) != hit)
    {
        LOG("Halo 3 zoom reader: the aim query did not bind (missing, ambiguous or not called as expected); Aim "
            "Frame Zoom has no zoom level here");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.faulted.store(false);
    if (MH_CreateHook(reinterpret_cast<void*>(hit), reinterpret_cast<void*>(&Halo3IndependentQueryDetour),
                      reinterpret_cast<void**>(&feature.queryOriginal)) != MH_OK)
    {
        LOG("Halo 3 zoom reader: hook creation failed; Aim Frame Zoom has no zoom level here");
        feature.queryOriginal = nullptr;
        return false;
    }
    feature.queryTarget = reinterpret_cast<void*>(hit);
    if (MH_EnableHook(feature.queryTarget) != MH_OK)
    {
        LOG("Halo 3 zoom reader: hook enable failed; Aim Frame Zoom has no zoom level here");
        (void)RemoveHalo3DualAim();
        return false;
    }
    LOG("Halo 3 zoom reader: installed on the aim query alone (the dual aim lane did not install); Aim Frame Zoom "
        "reads the game's zoom level from it");
    return true;
}

void ReportHalo3DualAim()
{
    const auto& feature = g_halo3Dual;
    if (!feature.fireTarget && !feature.aimTarget && !feature.queryTarget && !feature.cameraTarget) return;
    LOG("Halo 3 native firing (all actors, cumulative): entered=%llu returned=%llu unwound=%llu withData=%llu predicted=%llu; counters are independent of optional aim settings",
        feature.fireEntries.load(),feature.fireReturns.load(),feature.fireUnwinds.load(),
        feature.fireWithData.load(),feature.firePredicted.load());
    LOG("Halo 3 dual aim: enabled=%d isolatedFault=%d primary/secondary=%llu/%llu refused=%llu",
        feature.enabled.load() ? 1 : 0, feature.faulted.load() ? 1 : 0,
        g_halo3Dual.rays[0].exchange(0), g_halo3Dual.rays[1].exchange(0),
        g_halo3Dual.refused.exchange(0));
    LOG("Halo 3 dual native targeting: queries=%llu restoreRefused=%llu option=%d",
        g_halo3Dual.nativeQueries.exchange(0),g_halo3Dual.targetRestoresRefused.exchange(0),Sight_IndependentDualAim()?1:0);
    LOG("Halo 3 barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu option=%d",
        g_halo3Muzzle.enabled.load()?1:0,g_halo3Muzzle.faulted.load()?1:0,
        g_halo3Muzzle.applied.exchange(0),g_halo3Muzzle.refused.exchange(0),Sight_GunBarrelAim()?1:0);
}
