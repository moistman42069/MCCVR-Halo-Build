#include "odst_weapon_haptics_backend.inl"
namespace odst_weapon_haptics {
bool Current() noexcept {
    return runtime.enabled.load(std::memory_order_acquire)&&!runtime.faulted.load(std::memory_order_acquire)&&
        g_odstCamera.armed.load()&&!g_odstCamera.teardownRequested.load()&&
        runtime.generation==g_odstRuntimeGeneration.load()&&g_enabled.load()&&g_vrAim.load()&&
        VR_IsStereoEnabled()&&!VR_IsCutsceneTheaterActive()&&!exclusive_input::Active()&&
        TitleAdapter_GetActiveTitle()==GameTitle::Halo3ODST&&TitleAdapter_GetRuntimeMode()==RuntimeMode::Gameplay;
}
bool ReadSource(uint32_t weapon,Source& result) {
    result={};if(!Current()||weapon==UINT32_MAX||!g_odstPlayerUnitGetter)return false;
    const uint32_t unit=uint32_t(g_odstPlayerUnitGetter(0));
    int32_t scene=-1,shot=-1;
    if(!OdstMuzzleTargetStorage(unit)||ReadOdstCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled)return false;
    uint32_t weapons[2]{};if(!OdstReadMuzzleWeapons(unit,weapons))return false;
    const int role=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    if(role<0||role>1)return false;
    result={unit,weapon,runtime.generation,uint8_t(role),true};return true;
}
bool ReadQueue(int32_t user,uint8_t*& queue) {
    queue=nullptr;if(user<0||user>=4)return false;
    const auto* tls=OdstContactTls();if(!tls)return false;
    auto* base=*reinterpret_cast<uint8_t* const*>(tls+0x2B0);if(!base)return false;
    queue=base+size_t(user)*0x98;return true;
}
bool LoadBands(uint32_t tag,uint32_t row,uint8_t (&bands)[0x30]) {
    if(tag==UINT32_MAX||row>0x10000)return false;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(runtime.base+0xA9F0A8);
    const auto* base=*reinterpret_cast<const uint8_t* const*>(runtime.base+0x2022AA8);
    if(!table||!base)return false;
    const uint32_t address=ReadU32(table,4+size_t(tag&0xFFFF)*8);if(!address)return false;
    const auto* definition=base+uint64_t(address)*4;
    const uint32_t count=ReadU32(definition,0),rows=ReadU32(definition,4);
    if(!count||count>0x10000||row>=count||!rows)return false;
    std::memcpy(bands,base+uint64_t(rows)*4+size_t(row)*0xC0+0x5C,sizeof(bands));return true;
}
bool Supported(const Source& source) {
    uint32_t weapons[2]{};
    if(!OdstReadMuzzleWeapons(source.unit,weapons)||weapons[0]!=source.weapon||weapons[1]!=UINT32_MAX)return false;
    SupportGripRelationshipSnapshot support{};
    return (!g_config.persistent_support_grip&&VR_IsTwoHandAiming())||
        (VR_GetSupportGripRelationship(support)&&support.engaged&&support.title==GameTitle::Halo3ODST&&
        support.generation==source.generation&&support.unit==source.unit&&support.weapon==source.weapon);
}
}
bool RemoveOdstWeaponHaptics() {
    using namespace odst_weapon_haptics;auto& r=runtime;r.enabled.store(false,std::memory_order_release);
    for(auto target:r.targets)if(target){const auto status=MCCVR_DisableHookForRetirement(target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED){
            LOG("ODST weapon haptics CleanupRequired: disable failed");return false;}}
    const void* functions[]{reinterpret_cast<const void*>(ScopeHook),reinterpret_cast<const void*>(EffectHook),reinterpret_cast<const void*>(WorkerHook),reinterpret_cast<const void*>(UpdateHook),reinterpret_cast<const void*>(EvaluateHook)};
    const void* originals[]{reinterpret_cast<const void*>(r.scopeOriginal),reinterpret_cast<const void*>(r.effectOriginal),reinterpret_cast<const void*>(r.workerOriginal),reinterpret_cast<const void*>(r.updateOriginal),reinterpret_cast<const void*>(r.evaluateOriginal)};
    if(!WaitForNativeDetourQuiescence(functions,originals,5,r.callbacks)){
        LOG("ODST weapon haptics CleanupRequired: callbacks or ingress busy");return false;}
    for(auto& target:r.targets)if(target){const auto status=MH_RemoveHook(target);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED){LOG("ODST weapon haptics CleanupRequired: removal failed");return false;}target=nullptr;}
    r.scopeOriginal=nullptr;r.effectOriginal=nullptr;r.workerOriginal=nullptr;r.updateOriginal=nullptr;r.evaluateOriginal=nullptr;r.curve=nullptr;
    for(auto& voice:r.voices)voice={};r.base=0;r.generation=0;return true;
}
bool InstallOdstWeaponHaptics(uintptr_t base,size_t size,uint32_t generation) {
    using namespace odst_weapon_haptics;auto& r=runtime;
    for(auto target:r.targets)if(target)return false;
    if(!generation||size<=0x2022AB0)return false;
    struct Binding{uintptr_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x3ACCC8,"48 8B C4 48 89 58 18 66 89 50 10 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 10 01 00 00 4C 8B 2D B9 5D C7 01 45"},
        {0x3A1464,"48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 70 20 55 48 8D 68 D8 48 81 EC 20 01 00 00 44 8B 05 92 E6 6E 00 0F 29"},
        {0x1E3D40,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 4C 8B 3D 45 ED E3 01 33 FF 45 0F B7"},
        {0x1BE0B0,"48 89 5C 24 08 55 56 57 41 56 41 57 48 83 EC 40 45 33 FF 0F 29 74 24 30 44 38 3D 39 45 8D 00 0F 28 F0 44 89 7C 24 78 0F"},
        {0x1BE2B0,"48 8B C4 48 89 58 18 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 F3 0F 10 81 90 00 00 00 48 8D 79"},
        {0x27B458,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 56 41 57 48 81 EC 90 00 00 00 8B 71 0C 33 FF 4C 8B 35 29 76 DA 01 0F"},
        {0x1E3F6E,"4D 69 C2 98 00 00 00 B8 B0 02 00 00 45 8B D1 B9 01 00 00 00 4E 03 04 30 F3 41 0F 10 48 70 49 8D 50 74 F3 0F 10 02 0F 28"},
        {0x3AD4B0,"40 84 ED 0F 84 13 06 00 00 8B 4C 24 68 85 C9 74 1D F3 0F 10 8C 1E CC 01 00 00 66 0F 6E C1 0F 5B C0 F3 0F 5C C8 F3 0F 11"},
        {0x1BE2E5,"4C 8B 3D BC 47 E6 01 41 BD 08 00 00 00 48 8B 1D AF 0D 8E 00 45 33 C0 0F 29 70 B8 F3 0F 10 35 88 B0 6C 00 0F 29 78 A8 44"},
        {0x1BE028,"40 53 48 83 EC 20 8B 0D E8 1A 8D 00 33 D2 65 48 8B 04 25 58 00 00 00 41 B8 6C 02 00 00 BB B0 02 00 00 48 8B 04 C8 48 8B"},
        {0x1BE055,"E8 CF 23 4A 00 48 83 C3 08 BA 04 00 00 00 41 83 C8 FF 48 8B C3 B9 08 00 00 00 44 89 40 F8 44 89 40 FC C7 00 00 00 80 3F 48 8D 40 0C 48 83 E9 01 75 E8 48 81 C3 98 00 00 00 48 83 EA 01 75 D3 48 83 C4 20 5B"},
    };
    for(const auto& b:bindings){const auto hit=sig::Find(base,size,b.pattern);
        if(hit!=base+b.rva||sig::Find(hit+1,base+size-hit-1,b.pattern)){
            LOG("ODST weapon haptics StockFallback: own binding +%llX missing/ambiguous",static_cast<unsigned long long>(b.rva));return false;}}
    struct Edge{uintptr_t site,target;};
    constexpr Edge edges[]{{0x3AD8ED,0x3A1464},{0x3AD9B8,0x3A1464},{0x3A15FC,0x1E3B98},
        {0x1E3CD6,0x1E3D40},{0x1E3D1B,0x1E3D40},{0x1BE11D,0x1BE2B0},{0x1BE39F,0x27B458}};
    for(const auto& edge:edges)if(*reinterpret_cast<const uint8_t*>(base+edge.site)!=0xE8||
        base+edge.site+5+*reinterpret_cast<const int32_t*>(base+edge.site+1)!=base+edge.target){
            LOG("ODST weapon haptics StockFallback: native edge mismatch");return false;}
    r.base=base;r.generation=generation;r.faulted=false;r.writer.clear();for(auto& voice:r.voices)voice={};
    r.curve=reinterpret_cast<CurveFn>(base+0x27B458);
    const uintptr_t addresses[]{0x3ACCC8,0x3A1464,0x1E3D40,0x1BE0B0,0x1BE2B0};
    void* detours[]{reinterpret_cast<void*>(ScopeHook),reinterpret_cast<void*>(EffectHook),reinterpret_cast<void*>(WorkerHook),reinterpret_cast<void*>(UpdateHook),reinterpret_cast<void*>(EvaluateHook)};
    void** originals[]{reinterpret_cast<void**>(&r.scopeOriginal),reinterpret_cast<void**>(&r.effectOriginal),reinterpret_cast<void**>(&r.workerOriginal),reinterpret_cast<void**>(&r.updateOriginal),reinterpret_cast<void**>(&r.evaluateOriginal)};
    for(int i=0;i<5;++i){void* target=reinterpret_cast<void*>(base+addresses[i]);
        if(MH_CreateHook(target,detours[i],originals[i])!=MH_OK){RemoveOdstWeaponHaptics();LOG("ODST weapon haptics StockFallback: create failed");return false;}r.targets[i]=target;}
    for(auto target:r.targets)if(MH_EnableHook(target)!=MH_OK){RemoveOdstWeaponHaptics();LOG("ODST weapon haptics StockFallback: enable failed; retained cleanup if needed");return false;}
    r.enabled.store(true,std::memory_order_release);
    LOG("ODST hand-specific weapon haptics installed: admitted barrel response, authored private voices, generic rumble retained; headset verification pending");return true;
}
void ReportOdstWeaponHaptics() {
    const auto& r=odst_weapon_haptics::runtime;bool any=false;for(auto target:r.targets)any=any||target!=nullptr;if(!any)return;
    LOG("ODST weapon haptics: enabled=%d fault=%d captured=%llu stockFallback=%llu faults=%llu",r.enabled.load()?1:0,r.faulted.load()?1:0,r.captured.load(),r.fallback.load(),r.faults.load());
    if(r.faulted.load()||!r.enabled.load())(void)RemoveOdstWeaponHaptics();
}
