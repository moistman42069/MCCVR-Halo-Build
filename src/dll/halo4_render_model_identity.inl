// Existing checksum reader shared with the production vehicle fixture.
    struct Halo4RenderModelIdentity
    {
        uint32_t runtimeImportChecksum = 0;
        int nodeCount = 0;
        uintptr_t descriptor = 0;
    };

    bool Halo4ResolveRenderModelIdentity(
        uint16_t renderModelIndex, Halo4RenderModelIdentity& identity)
    {
        identity=Halo4RenderModelIdentity{};
        if (!g_halo4RenderModelTagIndexPointerSlot ||
            !g_halo4RenderModelGroupBaseTable)
            return false;
        uintptr_t tagIndexBase=0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(
                    g_halo4RenderModelTagIndexPointerSlot),
                &tagIndexBase,sizeof(tagIndexBase)) || !tagIndexBase)
            return false;
        const uintptr_t packedAddress=tagIndexBase+
            static_cast<uintptr_t>(renderModelIndex)*8u+4u;
        if (packedAddress<tagIndexBase) return false;
        uint32_t packed=0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(packedAddress),
                &packed,sizeof(packed)))
            return false;
        const uint32_t page=packed>>28;
        uintptr_t biasedBase=0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(
                    g_halo4RenderModelGroupBaseTable+
                    static_cast<uintptr_t>(page)*8u),
                &biasedBase,sizeof(biasedBase)) || !biasedBase)
            return false;
        const uintptr_t descriptorAddress=biasedBase+
            static_cast<uintptr_t>(packed)*4u;
        if (descriptorAddress<biasedBase || descriptorAddress>
            (std::numeric_limits<uintptr_t>::max)()-0x30u)
            return false;
        uint32_t checksum=0;
        int32_t count=0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(descriptorAddress+0x08u),
                &checksum,sizeof(checksum)) ||
            !Halo4SafeRead(
                reinterpret_cast<const void*>(descriptorAddress+0x30u),
                &count,sizeof(count)) || count<=0 ||
            count>kHalo4FirstPersonBankTransforms)
            return false;
        identity.runtimeImportChecksum=checksum;
        identity.nodeCount=count;
        identity.descriptor=descriptorAddress;
        return true;
    }

