bool RemoveHalo2WeaponHaptics() {
    using namespace h2_recoil;
    enabled=false;
    Hook* hooks[]{&triggerHook,&effectHook,&enqueueHook,&updateHook,&evaluateHook};
    for(auto* hook:hooks)if(hook->target) {
        const auto status=MCCVR_DisableHookForRetirement(hook->target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED)return false;
    }
    const void* functions[]{reinterpret_cast<void*>(&Trigger),reinterpret_cast<void*>(&Effect),
        reinterpret_cast<void*>(&Enqueue),reinterpret_cast<void*>(&Update),reinterpret_cast<void*>(&Evaluate),
        reinterpret_cast<void*>(&EffectBody),reinterpret_cast<void*>(&EnqueueBody),reinterpret_cast<void*>(&EvaluateBody)};
    const void* originals[]{triggerHook.original,effectHook.original,enqueueHook.original,
        updateHook.original,evaluateHook.original,nullptr,nullptr,nullptr};
    if(!WaitForNativeDetourQuiescence(functions,originals,8,callbacks))return false;
    for(auto* hook:hooks)if(hook->target) {
        const auto status=MH_RemoveHook(hook->target);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED)return false;
        *hook={};
    }
    ClearVoices();base=0;generation=0;return true;
}
bool InstallHalo2WeaponHaptics(uintptr_t module,size_t size) {
    using namespace h2_recoil;
    if(enabled.load())return true;
    if(triggerHook.target||effectHook.target||enqueueHook.target||updateHook.target||evaluateHook.target)
    {LOG("Halo 2 weapon haptics CleanupRequired: prior hooks retained");return false;}
    if(!g_halo2Dual.enabled.load())
    {LOG("Halo 2 weapon haptics StockFallback: owned weapon reader unavailable");return false;}
    for(const auto& binding:proof::entries) {
        uintptr_t found{};uint32_t count{};
        if(!CountPatternMatches(module,size,binding.pattern,found,count)||count!=1||found!=module+binding.rva)
        {LOG("Halo 2 weapon haptics StockFallback: binding +%X missing/ambiguous",binding.rva);return false;}
    }
    for(const auto& edge:proof::edges) {
        const auto* call=reinterpret_cast<const uint8_t*>(module+edge.call);
        if(edge.call+5>size||call[0]!=0xE8||module+edge.call+5+Read<int32_t>(module+edge.call+1)!=module+edge.target)
        {LOG("Halo 2 weapon haptics StockFallback: native call +%X changed",edge.call);return false;}
    }
    for(const auto& binding:proof::witnesses) {
        uintptr_t found{};uint32_t count{};
        if(!CountPatternMatches(module,size,binding.pattern,found,count)||count!=1||found!=module+binding.rva)
        {LOG("Halo 2 weapon haptics StockFallback: layout/ABI +%X changed",binding.rva);return false;}
    }
    for(const auto& ref:proof::references)
        if(ref.instruction+ref.length>size||ref.target+8>size||
            module+ref.instruction+ref.length+Read<int32_t>(module+ref.instruction+ref.displacement)!=module+ref.target)
        {LOG("Halo 2 weapon haptics StockFallback: data witness +%X changed",ref.instruction);return false;}
    base=module;generation=g_generation.load();fault=false;ClearVoices();
    struct Entry {Hook* hook;uint32_t rva;void* detour;};
    const Entry entries[]{
        {&triggerHook,proof::trigger,reinterpret_cast<void*>(&Trigger)},
        {&effectHook,proof::effect,reinterpret_cast<void*>(&Effect)},
        {&enqueueHook,proof::enqueue,reinterpret_cast<void*>(&Enqueue)},
        {&updateHook,proof::update,reinterpret_cast<void*>(&Update)},
        {&evaluateHook,proof::evaluate,reinterpret_cast<void*>(&Evaluate)}};
    for(const auto& entry:entries) {
        void* target=reinterpret_cast<void*>(module+entry.rva);
        if(MH_CreateHook(target,entry.detour,&entry.hook->original)!=MH_OK) {
            LOG("Halo 2 weapon haptics StockFallback: create +%X failed",entry.rva);
            (void)RemoveHalo2WeaponHaptics();return false;
        }
        entry.hook->target=target;
    }
    for(const auto& entry:entries)if(MH_EnableHook(entry.hook->target)!=MH_OK) {
        LOG("Halo 2 weapon haptics StockFallback: enable +%X failed",entry.rva);
        (void)RemoveHalo2WeaponHaptics();return false;
    }
    enabled=true;
    LOG("Halo 2 weapon haptics Installed: native authored recoil per owned weapon/hand; general rumble preserved");
    return true;
}
void ReportHalo2WeaponHaptics() {
    using namespace h2_recoil;
    LOG("Halo 2 weapon haptics: enabled=%d fault=%d captured=%llu stockFallback=%llu faults=%llu",
        enabled.load()?1:0,fault.load()?1:0,captured.exchange(0),fallback.exchange(0),faults.exchange(0));
    const bool retained=triggerHook.target||effectHook.target||enqueueHook.target||
        updateHook.target||evaluateHook.target;
    if(retained&&(fault.load()||!enabled.load())) {
        const bool removed=RemoveHalo2WeaponHaptics();
        LOG("Halo 2 weapon haptics %s: optional hooks %s; camera unchanged",
            removed?"StockFallback":"CleanupRequired",removed?"retired":"retained for retry");
    }
}
