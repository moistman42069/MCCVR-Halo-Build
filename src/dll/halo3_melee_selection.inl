// Optional physical-strike response selection. H3EK A5DE20 and its separately
// verified retail 35A9A4 select primary inventory +262/+268 and return early
// without a weapon. Read the striking role's own weapon; never swap inventory.
using Halo3MeleeSelectFn=void(__fastcall*)(uint32_t,int32_t,uint8_t*,uint32_t*,
    uint32_t*,uint32_t*,uint32_t*);
struct Halo3MeleeSelectionRuntime
{
    void* target=nullptr;
    Halo3MeleeSelectFn original=nullptr;
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> secondary{0},unarmed{0},fallbacks{0};
} g_halo3MeleeSelection;

const uint8_t* Halo3MeleeSelectionObject(uint32_t handle,uint8_t kind)
{
    if(!Halo3ContactObject(handle)) return nullptr;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(Halo3ContactTls()+0x38);
    const auto* entries=*reinterpret_cast<const uint8_t* const*>(table+0x48);
    const auto* entry=entries+(handle&0xffff)*0x18;
    return entry[3]==kind ? *reinterpret_cast<const uint8_t* const*>(entry+0x10) : nullptr;
}

bool Halo3ReadPhysicalMeleeResponse(uint32_t owner,int hand,Halo3MeleeResponse& result,
    bool& armed)
{
    const auto* unit=Halo3MeleeSelectionObject(owner,0);
    if(!unit || hand<0 || hand>1) return false;
    const auto* unitTag=Halo3LoadedTagDefinition(*reinterpret_cast<const uint32_t*>(unit));
    if(!unitTag) return false;
    const uint32_t unitDamage=*reinterpret_cast<const uint32_t*>(unitTag+0x1B4);
    // Both inventory-role bytes and the four full handles are independently
    // present in H3EK A5DE20/A844A0 and retail 35A9A4/3683A0/356388.
    const uint8_t index=unit[hand==1 ? 0x262 : 0x263];
    if(index!=0xff && index>=4) return false;
    const uint32_t weapon=index==0xff ? UINT32_MAX :
        *reinterpret_cast<const uint32_t*>(unit+0x268+size_t(index)*4);
    armed=weapon!=UINT32_MAX;
    if(!armed)
    {
        result=Halo3SelectPhysicalMeleeResponse(unitDamage,false,3,UINT32_MAX,
            UINT32_MAX,{},false);
        return true;
    }
    const auto* object=Halo3MeleeSelectionObject(weapon,2);
    // H3EK A844A0 and retail weapon-owner 364E18 prove these ownership fields.
    if(!object || !object[0x15D] ||
        *reinterpret_cast<const uint32_t*>(object+0x168)!=owner) return false;
    const auto* tag=Halo3LoadedTagDefinition(*reinterpret_cast<const uint32_t*>(object));
    if(!tag) return false;
    const float power=*reinterpret_cast<const float*>(object+0x18C);
    if(!std::isfinite(power)) return false;
    const bool charged=(*reinterpret_cast<const uint32_t*>(tag+0x18C)&(1u<<14)) && power>=1.0f;
    const size_t response=charged ? 0x2CC : 0x24C;
    Halo3MeleeResponse authored{};
    authored.damage=*reinterpret_cast<const uint32_t*>(tag+response+0xC);
    authored.effect=*reinterpret_cast<const uint32_t*>(tag+response+0x1C);
    if(!charged)
    {
        authored.clashDamage=*reinterpret_cast<const uint32_t*>(tag+0x2F8);
        authored.clashEffect=*reinterpret_cast<const uint32_t*>(tag+0x308);
    }
    result=Halo3SelectPhysicalMeleeResponse(unitDamage,true,tag[0x31C],
        *reinterpret_cast<const uint32_t*>(tag+0x22C),
        *reinterpret_cast<const uint32_t*>(tag+0x23C),authored,charged);
    return true;
}

__declspec(noinline) void __fastcall Halo3MeleeSelectionDetour(uint32_t unit,int32_t state,
    uint8_t* material,uint32_t* damage,uint32_t* effect,uint32_t* clashDamage,uint32_t* clashEffect)
{
    auto& feature=g_halo3MeleeSelection;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        if(!feature.original) __leave;
        feature.original(unit,state,material,damage,effect,clashDamage,clashEffect);
        const auto& scope=g_halo3ContactScope;
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        if(!scope.active || !Halo3ContactMeleeReady() || !feature.enabled.load() ||
            feature.faulted.load() || state!=0x79 || uint16_t(unit)!=uint16_t(scope.owner) ||
            (caller!=g_halo3Contact.base+0x35B751 && caller!=g_halo3Contact.base+0x35E883)) __leave;
        __try
        {
            Halo3MeleeResponse candidate{};
            bool armed=false;
            if(!material || !damage || !effect || !clashDamage || !clashEffect ||
                !Halo3ReadPhysicalMeleeResponse(scope.owner,scope.hand,candidate,armed))
            { feature.fallbacks.fetch_add(1); __leave; }
            // Preserve the native result byte-for-byte for ordinary main-weapon
            // strikes, including any engine-authored selection nuance.
            if(scope.hand==1 && armed) __leave;
            // Some custom unit/weapon tags author no usable damage response.
            // Preserve the working selection and make the local fallback visible.
            if(candidate.damage==UINT32_MAX)
            { feature.fallbacks.fetch_add(1); __leave; }
            *material=candidate.material;
            *damage=candidate.damage; *effect=candidate.effect;
            *clashDamage=candidate.clashDamage; *clashEffect=candidate.clashEffect;
            (armed ? feature.secondary : feature.unarmed).fetch_add(1);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) { feature.faulted.store(true); }
    }
    __finally { feature.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

bool RemoveHalo3MeleeSelection()
{
    auto& feature=g_halo3MeleeSelection;
    feature.enabled.store(false,std::memory_order_release);
    if(!feature.target) return true;
    const auto disabled=MCCVR_DisableHookForRetirement(feature.target);
    if(disabled!=MH_OK && disabled!=MH_ERROR_DISABLED)
    { LOG("Halo 3 melee selection CleanupRequired: disable failed"); return false; }
    const void* functions[]{reinterpret_cast<const void*>(&Halo3MeleeSelectionDetour)};
    const void* originals[]{reinterpret_cast<const void*>(feature.original)};
    if(!WaitForNativeDetourQuiescence(functions,originals,1,feature.callbacks) ||
        MH_RemoveHook(feature.target)!=MH_OK)
    { LOG("Halo 3 melee selection CleanupRequired: callbacks/ingress or removal pending"); return false; }
    feature.target=nullptr; feature.original=nullptr;
    return true;
}

bool InstallHalo3MeleeSelection(uintptr_t base,size_t size)
{
    auto& feature=g_halo3MeleeSelection;
    if(feature.target) return false;
    constexpr char pattern[]="48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 44 8B 15 D5 F5 6D 00 49 8B D9 65 48";
    const uintptr_t hit=sig::Find(base,size,pattern);
    if(hit!=base+0x35A9A4 || sig::Find(hit+1,base+size-hit-1,pattern))
    { LOG("Halo 3 melee selection StockFallback: selector binding missing/ambiguous"); return false; }
    for(uintptr_t caller : {uintptr_t(0x35B74C),uintptr_t(0x35E87E)})
    {
        const auto* call=reinterpret_cast<const uint8_t*>(base+caller);
        if(call[0]!=0xE8 || base+caller+5+*reinterpret_cast<const int32_t*>(call+1)!=hit)
        { LOG("Halo 3 melee selection StockFallback: native caller mismatch"); return false; }
    }
    if(MH_CreateHook(reinterpret_cast<void*>(hit),reinterpret_cast<void*>(&Halo3MeleeSelectionDetour),
        reinterpret_cast<void**>(&feature.original))!=MH_OK)
    { LOG("Halo 3 melee selection StockFallback: hook creation failed"); return false; }
    feature.target=reinterpret_cast<void*>(hit);
    if(MCCVR_EnableHook(feature.target)!=MH_OK)
    {
        LOG("Halo 3 melee selection StockFallback: hook enable failed");
        (void)RemoveHalo3MeleeSelection();
        return false;
    }
    feature.faulted.store(false);
    feature.enabled.store(true,std::memory_order_release);
    LOG("Halo 3 physical melee selection Installed: secondary weapon response or authored unarmed unit damage; main weapon and manual melee retain stock selection");
    return true;
}

void ReportHalo3MeleeSelection()
{
    const auto& feature=g_halo3MeleeSelection;
    if(!feature.target) return;
    LOG("Halo 3 physical melee selection: enabled=%d fault=%d secondary=%llu unarmed=%llu StockFallback=%llu",
        feature.enabled.load()?1:0,feature.faulted.load()?1:0,
        feature.secondary.load(),feature.unarmed.load(),feature.fallbacks.load());
}
