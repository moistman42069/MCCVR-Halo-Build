// Exact native parent-definition/model chain and marker matrix-count witnesses.
// Own-kit derivation and pinned-retail evaluator: HALO2-VEHICLE-IDENTITY-2026-09-23.md.
struct Halo2VehicleIdentityBinding {uint32_t rva;const char* pattern;};
constexpr Halo2VehicleIdentityBinding kHalo2VehicleIdentityBindings[]={
    {0x8D121A,"48 8B 15 0F 39 D1 00 4C 8B 05 10 39 D1 00 48 89 44 24 58 0F B7 08 48 03 C9 48 63 4C CA 08 46 8B 4C 01 38 41 83 F9 FF 0F 84 25 01 00 00 48 8B 05 4A 61 FE 00 41 0F B7 C9 48 03 C9 48 63 6C CA 08 48 8B 48 48 49 03 E8"},
    {0x8D12E7,"4C 8B C0 48 0F BF 88 18 01 00 00 48 B8 C5 4E EC C4 4E EC C4 4E 48 F7 E1 48 C1 EA 04 89 94 24 B0 00 00 00 49 0F BF 88 1A 01 00 00 49 03 C8"},
    {0x8D133F,"45 8D 48 FF 48 89 4C 24 30 8B 4D 04 89 54 24 28 41 8B D5 4C 89 44 24 20 4C 8B C6 E8 F1 19 12 00"},
};
bool VerifyHalo2VehicleIdentity(uintptr_t base,size_t size) noexcept {
    for(const auto& binding:kHalo2VehicleIdentityBindings) {
        uintptr_t found=0;uint32_t count=0;
        if(!CountPatternMatches(base,size,binding.pattern,found,count)||count!=1||found!=base+binding.rva)
            return false;
    }
    return true;
}
uint64_t ReadHalo2VehicleIdentity(uint32_t parentHandle) noexcept {
    if(!g_vehicleIdentityVerified.load(std::memory_order_acquire)) return 0;
    const auto get=reinterpret_cast<Halo2GraphDefinitionGetFn>(g_graphDefinitionGet.load(std::memory_order_acquire));
    const auto slot=reinterpret_cast<const uint8_t* const*>(g_halo2TagDataBaseSlot.load(std::memory_order_acquire));
    if(!get||!slot) return 0;
    __try {
        const auto* parent=static_cast<const uint8_t*>(Halo2ObjectFromIndex(parentHandle));
        if(!parent||parent[0xAA]!=1||!*slot) return 0;
        const auto definition=*reinterpret_cast<const uint16_t*>(parent);
        const auto* object=static_cast<const uint8_t*>(get(definition));
        if(!object) return 0;
        const auto modelTag=*reinterpret_cast<const uint32_t*>(object+0x38);
        if(modelTag==UINT32_MAX) return 0;
        const auto* model=static_cast<const uint8_t*>(get(modelTag));
        if(!model) return 0;
        const auto renderTag=*reinterpret_cast<const uint32_t*>(model+4);
        if(renderTag==UINT32_MAX) return 0;
        const auto* render=static_cast<const uint8_t*>(get(renderTag));
        int32_t offset=0;
        if(!render||!Halo2ReadWeaponCompressionHeader(std::span<const uint8_t>(render,0x1C),offset)) return 0;
        // Native 8D12EA..8D1303 divides this matrix byte count by 0x34.
        const auto nodeBytes=*reinterpret_cast<const int16_t*>(parent+0x118);
        if(nodeBytes<=0||nodeBytes%0x34||nodeBytes/0x34>255) return 0;
        const auto base=reinterpret_cast<uintptr_t>(*slot);
        const auto address=static_cast<int64_t>(base)+static_cast<int64_t>(offset);
        if(address<=0||static_cast<uint64_t>(address)>UINTPTR_MAX-24) return 0;
        float bounds[6]{};std::memcpy(bounds,reinterpret_cast<const void*>(address),sizeof(bounds));
        const auto* identity=halo2_vehicle_identity::Find(bounds,nodeBytes/0x34);
        if(!identity||parent!=Halo2ObjectFromIndex(parentHandle)||
            *reinterpret_cast<const uint16_t*>(parent)!=definition||
            *reinterpret_cast<const int16_t*>(parent+0x118)!=nodeBytes||
            *reinterpret_cast<const uint32_t*>(object+0x38)!=modelTag||
            *reinterpret_cast<const uint32_t*>(model+4)!=renderTag) return 0;
        return identity->identity;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
