// Own-title native marker evaluator, not a copied H3 object layout.
// See docs/HALO4-VEHICLE-IDENTITY-2026-09-23.md.
struct Halo4VehicleIdentityBinding {uint32_t rva;const char* pattern;};
constexpr Halo4VehicleIdentityBinding kHalo4VehicleIdentityBindings[]={
    {0x5D5C08,"48 8B 15 A1 64 AA 00 48 89 44 24 68 0F B7 40 08 8B 4C C2 04 8B C1 C1 E8 1C 49 8B 04 C0 83 7C 88 70 FF 0F 84 47 01 00 00 0F B7 44 88 70"},
    {0x5D5C44,"8B 54 C2 04 8B C2 44 8B E2 C1 E8 1C 48 8D 94 24 C0 00 00 00 4D 8B 2C C0 4C 8D 44 24 70 E8 76 3A 00 00"},
    {0x5D5D1B,"41 8B D7 43 8B 4C A5 0C 8B 5B 10 48 C1 EB 08 80 E3 01 E8 7A D5 D2 FF"},
};
bool Halo4ProveVehicleIdentity(uintptr_t base,size_t size) {
    for(const auto& binding:kHalo4VehicleIdentityBindings) {
        const auto found=sig::Find(base,size,binding.pattern);
        if(!found||found-base!=binding.rva||sig::Find(found+1,base+size-found-1,binding.pattern))return false;
    }
    return g_halo4RenderModelTagIndexPointerSlot==base+kHalo4RenderModelTagIndexPointerRva&&
        g_halo4RenderModelGroupBaseTable==base+kHalo4RenderModelGroupBaseTableRva;
}
bool Halo4VehicleTagDescriptor(uint32_t datum,uintptr_t& descriptor) noexcept {
    descriptor=0;
    if(datum==UINT32_MAX||!g_halo4RenderModelTagIndexPointerSlot||!g_halo4RenderModelGroupBaseTable)return false;
    uintptr_t table{},biased{};uint32_t packed{};
    if(!Halo4SafeRead(reinterpret_cast<const void*>(g_halo4RenderModelTagIndexPointerSlot),&table,sizeof(table))||!table)return false;
    const auto offset=static_cast<uintptr_t>(datum&0xFFFFu)*8+4;
    if(table>UINTPTR_MAX-offset||!Halo4SafeRead(reinterpret_cast<const void*>(table+offset),&packed,sizeof(packed))||
        !packed||packed==UINT32_MAX||!Halo4SafeRead(reinterpret_cast<const void*>(g_halo4RenderModelGroupBaseTable+
            static_cast<uintptr_t>(packed>>28)*8),&biased,sizeof(biased))||!biased)return false;
    const auto bytes=static_cast<uintptr_t>(packed)*4;
    if(biased>UINTPTR_MAX-bytes||biased+bytes>UINTPTR_MAX-0x74)return false;
    descriptor=biased+bytes;return true;
}
uint64_t Halo4ReadVehicleIdentity(const uint8_t* parent) noexcept {
    if(!parent||!g_halo4VehicleIdentityProven.load(std::memory_order_acquire))return 0;
    uint16_t definition{};uint32_t modelTag{},renderTag{};
    uintptr_t object{},model{};
    if(!Halo4SafeRead(parent+8,&definition,sizeof(definition))||
        !Halo4VehicleTagDescriptor(definition,object)||
        !Halo4SafeRead(reinterpret_cast<const void*>(object+0x70),&modelTag,sizeof(modelTag))||
        !Halo4VehicleTagDescriptor(modelTag,model)||
        !Halo4SafeRead(reinterpret_cast<const void*>(model+0x0C),&renderTag,sizeof(renderTag))||renderTag==UINT32_MAX)return 0;
    Halo4RenderModelIdentity identity{};
    if(!Halo4ResolveRenderModelIdentity(static_cast<uint16_t>(renderTag),identity)||
        !identity.runtimeImportChecksum||identity.runtimeImportChecksum==UINT32_MAX)return 0;
    // Revoke mixed receipts if the native tag tables, references or descriptor changed.
    uint16_t currentDefinition{};uint32_t currentModel{},currentRender{};uintptr_t currentObject{},currentModelAddress{};
    Halo4RenderModelIdentity current{};
    if(!Halo4SafeRead(parent+8,&currentDefinition,sizeof(currentDefinition))||currentDefinition!=definition||
        !Halo4VehicleTagDescriptor(definition,currentObject)||currentObject!=object||
        !Halo4SafeRead(reinterpret_cast<const void*>(object+0x70),&currentModel,sizeof(currentModel))||currentModel!=modelTag||
        !Halo4VehicleTagDescriptor(modelTag,currentModelAddress)||currentModelAddress!=model||
        !Halo4SafeRead(reinterpret_cast<const void*>(model+0x0C),&currentRender,sizeof(currentRender))||currentRender!=renderTag||
        !Halo4ResolveRenderModelIdentity(static_cast<uint16_t>(renderTag),current)||
        current.descriptor!=identity.descriptor||current.nodeCount!=identity.nodeCount||
        current.runtimeImportChecksum!=identity.runtimeImportChecksum)return 0;
    return (static_cast<uint64_t>(identity.nodeCount)<<32)|identity.runtimeImportChecksum;
}
