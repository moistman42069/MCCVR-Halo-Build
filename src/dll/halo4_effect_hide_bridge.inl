// Production local first-person effect bridge, shared with the fixture.
extern "C" void __fastcall Halo4EffectHideBridge(
    void* descriptor, void* matrix)
{
    if (!g_halo4EffectsEnabled || !descriptor || !matrix)
        return;
    __try
    {
        const uint8_t flags =
            *(static_cast<const uint8_t*>(descriptor) + 0x4A);
        if (!Halo4EffectDescriptorIsLocalFirstPerson(flags))
            return;
        constexpr uint32_t kFiniteFar = 0x461C4000u;
        auto* const destination = static_cast<uint8_t*>(matrix);
        std::memcpy(destination + 0x28, &kFiniteFar, sizeof(kFiniteFar));
        std::memcpy(destination + 0x2C, &kFiniteFar, sizeof(kFiniteFar));
        std::memcpy(destination + 0x30, &kFiniteFar, sizeof(kFiniteFar));
        InterlockedIncrement64(&g_halo4EffectsHidden);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // A torn optional effect record leaves that effect stock. The camera
        // and OpenXR session are owned by a separate transaction.
    }
}
