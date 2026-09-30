// Own-title proof: H4-WEAPON-HAPTICS-2026-09-30.md.
#include "halo4_weapon_haptics_backend.inl"
namespace halo4_weapon_haptics {
bool Current() noexcept {
    return runtime.enabled.load(std::memory_order_acquire)&&!runtime.faulted.load(std::memory_order_acquire)&&
        g_halo4Camera.armed.load()&&!g_halo4Camera.teardownRequested.load()&&
        runtime.generation==g_halo4Camera.generation.load()&&g_enabled.load()&&g_vrAim.load()&&
        VR_IsStereoEnabled()&&!VR_IsCutsceneTheaterActive()&&!exclusive_input::Active()&&
        TitleAdapter_GetActiveTitle()==GameTitle::Halo4&&TitleAdapter_GetRuntimeMode()==RuntimeMode::Gameplay;
}
bool ReadSource(uint32_t weapon,Source& result) {
    result={};if(!Current()||weapon==UINT32_MAX)return false;
    const auto* tls=Halo4MuzzleTls();
    const auto* fp=tls?Halo4VehicleRead<const uint8_t*>(tls,0x6A0):nullptr;
    if(!fp||!(Halo4VehicleRead<uint32_t>(fp,0)&2))return false;
    const uint32_t unit=Halo4VehicleRead<uint32_t>(fp,4);
    const auto* object=Halo4MuzzleObject(unit,1);
    if(!object||Halo4VehicleRead<uint32_t>(object,0x24)!=UINT32_MAX||
        Halo4VehicleRead<uint32_t>(object,0x694)!=UINT32_MAX||
        ReadHalo4CinematicControl()!=CinematicControlState::PlayerControlled)return false;
    uint32_t weapons[2]{};
    if(!Halo4ReadMuzzleWeapons(unit,weapons))return false;
    const int role=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    if(role<0||role>1||Halo4VehicleRead<uint32_t>(fp,size_t(role)*0x2EC8+0x6C)!=weapon)return false;
    result={unit,weapon,runtime.generation,uint8_t(role),true};return true;
}
bool ReadQueue(const void*& queue) {
    queue=nullptr;const auto* tls=Halo4MuzzleTls();if(!tls)return false;
    // H4EK native rumble/update, matched retail145C48/1460C8: user zero
    // begins at the native queue bank in TLS2D0 (other users stride1D8).
    queue=Halo4VehicleRead<const void*>(tls,0x2D0);return queue!=nullptr;
}
bool LoadBands(uint32_t tag,uint8_t (&bands)[0x30]) {
    // H4EK 22E4F0 / retail1460C8: native rumble tag is two 0x18 bands.
    // Native145E80 supplied this tag; this guarded copy cannot touch a live bank.
    if(tag==UINT32_MAX)return false;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(runtime.base+0x107C0B0);
    if(!table)return false;
    const uint32_t encoded=Halo4VehicleRead<uint32_t>(table,4+size_t(tag&0xFFFF)*8);
    const auto* segment=reinterpret_cast<const uint8_t* const*>(runtime.base+0x496A180)[encoded>>28];
    if(!segment)return false;
    std::memcpy(bands,segment+uint64_t(encoded)*4,sizeof(bands));return true;
}
bool Supported(const Source& source) {
    uint32_t weapons[2]{};
    if(!Halo4ReadMuzzleWeapons(source.unit,weapons)||weapons[0]!=source.weapon||weapons[1]!=UINT32_MAX)return false;
    SupportGripRelationshipSnapshot support{};
    return (!g_config.persistent_support_grip&&VR_IsTwoHandAiming())||
        (VR_GetSupportGripRelationship(support)&&support.engaged&&support.title==GameTitle::Halo4&&
        support.generation==source.generation&&support.unit==source.unit&&support.weapon==source.weapon);
}
}
bool RemoveHalo4WeaponHaptics() {
    using namespace halo4_weapon_haptics;auto& r=runtime;
    r.enabled.store(false,std::memory_order_release);
    for(auto target:r.targets)if(target){
        const auto status=MCCVR_DisableHookForRetirement(target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED){
            LOG("Halo 4 weapon haptics CleanupRequired: hook disable failed");return false;}}
    const void* functions[]{reinterpret_cast<const void*>(ScopeHook),reinterpret_cast<const void*>(EffectHook),
        reinterpret_cast<const void*>(EnqueueHook),reinterpret_cast<const void*>(UpdateHook),reinterpret_cast<const void*>(EvaluateHook)};
    const void* originals[]{reinterpret_cast<const void*>(r.scopeOriginal),reinterpret_cast<const void*>(r.effectOriginal),
        reinterpret_cast<const void*>(r.enqueueOriginal),reinterpret_cast<const void*>(r.updateOriginal),reinterpret_cast<const void*>(r.evaluateOriginal)};
    if(!WaitForNativeDetourQuiescence(functions,originals,5,r.callbacks)){
        LOG("Halo 4 weapon haptics CleanupRequired: callbacks or ingress busy");return false;}
    for(auto& target:r.targets)if(target){const auto status=MH_RemoveHook(target);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED){
            LOG("Halo 4 weapon haptics CleanupRequired: hook removal failed");return false;}
        target=nullptr;}
    r.scopeOriginal=nullptr;r.effectOriginal=nullptr;r.enqueueOriginal=nullptr;r.updateOriginal=nullptr;r.evaluateOriginal=nullptr;
    r.sourceUpdate=nullptr;r.curveInput=nullptr;r.curveOutput=nullptr;
    for(auto& voice:r.voices)voice={};r.base=0;r.generation=0;return true;
}
bool InstallHalo4WeaponHaptics(uintptr_t base,size_t size,uint32_t generation) {
    using namespace halo4_weapon_haptics;auto& r=runtime;
    for(auto target:r.targets)if(target)return false;
    struct Binding{uintptr_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x6161DC,"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 56 48 83 EC 40 49 8B D8 48 63 EA 8B F9 E8 37 C0 FF FF 44 8B D0"},
        {0x603DE0,"48 8B C4 55 53 56 57 41 56 48 8D A8 F8 FD FF FF 48 81 EC E0 02 00 00 0F 29 70 C8 48 8B 05 0E 72 83 00 48 33 C4 48 89 85"},
        {0x145E80,"48 89 7C 24 08 83 CF FF 0F 28 DA 44 8B DA 3B D7 0F 84 16 01 00 00 44 8B 05 7B 13 F1 00 65 48 8B 04 25 58 00 00 00 BA D0"},
        {0x145C48,"48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 40 33 F6 0F 29 70 C8 40 38 35 67 5E F3"},
        {0x1E7B64,"48 8B C4 48 89 58 18 55 56 57 41 55 41 56 48 8D 68 A1 48 81 EC D0 00 00 00 83 39 FF 48 8B D9 0F 29 70 C8 0F 29 78 B8 44"},
        {0x234E08,"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 57 48 81 EC 90 00 00 00 8B 59 0C 4C 8D 3D 52 53 73 04 0F 29"},
        {0x235460,"8B 51 0C 48 8D 0D 16 4D 73 04 8B C2 48 C1 E8 1C 48 8B 0C C1 80 7C 91 02 00 75 43 F6 44 91 01 04 F3 0F 10 54 91 08 74 24"},
        {0x1460C8,"48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 50 F3 0F 10 81 D0 01 00 00 48 8D 79 04"},
        {0x1E69E3,"8B 54 9F 58 4C 8D 4D B7 0F 28 D6 8B CE E8 8B F4 F5 FF 41 83 C8 FF 45 3B E8 0F 84 AA 00 00 00 48 8B 05 A7 56 E9 00 4C 8B"},
        {0x615390,"45 84 ED 74 0E 4C 8D 44 24 78 8B D5 8B CF E8 39 0E 00 00 4C 8D 44 24 78 8B D5 8B CF E8 9F 0F 00 00 48 8B 54 24 68 4C 8B"},
        {0x1460FF,"4C 8B 25 AA 5F F3 00 48 8B D9 0F 29 70 C8 41 BF 08 00 00 00 F3 0F 10 35 B5 F5 C4 00 45 33 ED 0F 29 78 B8 0F 57 FF F3 0F"},
        {0x14613E,"0F B7 47 FC 48 8D 0D 37 40 82 04 49 8B ED 41 8B 54 C4 04 8B C2 48 C1 E8 1C 48 8D 72 01 48 8B 0C C1 48 8D 34 B1 F3 0F 10"},
        {0x145CE3,"8B FE B8 D0 02 00 00 4C 8B F6 49 69 DE D8 01 00 00 8B CF 4A 03 1C 28 E8 85 FE F4 FF 83 F8 FF"},
    };
    for(const auto& b:bindings){const auto hit=sig::Find(base,size,b.pattern);
        if(hit!=base+b.rva||sig::Find(hit+1,base+size-hit-1,b.pattern)){
            LOG("Halo 4 weapon haptics unavailable: own native binding +0x%llX missing/ambiguous; generic rumble retained",static_cast<unsigned long long>(b.rva));return false;}}
    const auto call=[base](uintptr_t from,uintptr_t target){return *reinterpret_cast<const uint8_t*>(base+from)==0xE8&&
        base+from+5+*reinterpret_cast<const int32_t*>(base+from+1)==base+target;};
    if(size<=0x496A200||!call(0x61539E,0x6161DC)||!call(0x616232,0x603DE0)||!call(0x616324,0x603DE0)||
        !call(0x1E69F0,0x145E80)||!call(0x145D72,0x1460C8)||!call(0x146194,0x234E08)||!call(0x14619F,0x235460)){
        LOG("Halo 4 weapon haptics unavailable: own source/evaluation edges mismatch; generic rumble retained");return false;}
    r.base=base;r.generation=generation;r.faulted=false;r.writer.clear();for(auto& voice:r.voices)voice={};
    r.sourceUpdate=reinterpret_cast<SourceUpdateFn>(base+0x1E7B64);
    r.curveInput=reinterpret_cast<CurveInputFn>(base+0x234E08);
    r.curveOutput=reinterpret_cast<CurveOutputFn>(base+0x235460);
    const uintptr_t addresses[]{0x6161DC,0x603DE0,0x145E80,0x145C48,0x1460C8};
    void* detours[]{reinterpret_cast<void*>(ScopeHook),reinterpret_cast<void*>(EffectHook),reinterpret_cast<void*>(EnqueueHook),reinterpret_cast<void*>(UpdateHook),reinterpret_cast<void*>(EvaluateHook)};
    void** originals[]{reinterpret_cast<void**>(&r.scopeOriginal),reinterpret_cast<void**>(&r.effectOriginal),reinterpret_cast<void**>(&r.enqueueOriginal),reinterpret_cast<void**>(&r.updateOriginal),reinterpret_cast<void**>(&r.evaluateOriginal)};
    for(int i=0;i<5;++i){void* target=reinterpret_cast<void*>(base+addresses[i]);
        if(MH_CreateHook(target,detours[i],originals[i])!=MH_OK){RemoveHalo4WeaponHaptics();LOG("Halo 4 weapon haptics unavailable: hook creation failed; generic rumble retained");return false;}
        r.targets[i]=target;}
    for(auto target:r.targets)if(MH_EnableHook(target)!=MH_OK){RemoveHalo4WeaponHaptics();LOG("Halo 4 weapon haptics unavailable: hook enable failed; cleanup retained if needed");return false;}
    r.enabled.store(true,std::memory_order_release);
    LOG("Halo 4 hand-specific weapon haptics installed: native authored firing response, independent primary/secondary, generic rumble unchanged; headset verification pending");return true;
}
void ReportHalo4WeaponHaptics() {
    const auto& r=halo4_weapon_haptics::runtime;
    bool any=false;for(auto target:r.targets)any=any||target!=nullptr;
    if(!any)return;
    LOG("Halo 4 weapon haptics: enabled=%d fault=%d captured=%llu stockFallback=%llu faults=%llu",
        r.enabled.load()?1:0,r.faulted.load()?1:0,r.captured.load(),r.fallback.load(),r.faults.load());
    if(r.faulted.load()||!r.enabled.load())(void)RemoveHalo4WeaponHaptics();
}
