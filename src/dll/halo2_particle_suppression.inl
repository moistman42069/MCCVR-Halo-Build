// Production Classic-only particle gate; included by the H2 adapter and fixture.
    __declspec(noinline) void __fastcall Halo2ParticleRendererDetour(
        uint32_t arg0, uint32_t currentUserFirstPerson, uint32_t arg2,
        uint32_t arg3)
    {
        g_particleActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            const auto original = reinterpret_cast<Halo2ParticleRendererFn>(
                g_particleOriginal.load(std::memory_order_acquire));
            bool suppress = false;
            if (original && g_config.hide_muzzle_flash[5] && g_armed.load(std::memory_order_acquire) &&
                g_levelLive.load(std::memory_order_acquire) &&
                !g_teardownRequested.load(std::memory_order_acquire))
            {
                const uintptr_t base =
                    g_moduleBase.load(std::memory_order_acquire);
                if (base)
                {
                    uint8_t classicDisabled = 1;
                    bool readable = false;
                    __try
                    {
                        classicDisabled = *reinterpret_cast<
                            const volatile uint8_t*>(
                                base + kHalo2ClassicRenderDisabledByteRva);
                        readable = true;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        g_particleReadFaults.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                    suppress = readable &&
                        Halo2ShouldSuppressClassicFirstPersonParticle(
                            classicDisabled,
                            static_cast<uint8_t>(currentUserFirstPerson));
                }
            }

            if (suppress)
            {
                g_particleSuppressed.fetch_add(1, std::memory_order_relaxed);
                g_particleHitPending.store(true, std::memory_order_release);
            }
            else if (original)
            {
                original(arg0, currentUserFirstPerson, arg2, arg3);
            }
        }
        __finally
        {
            g_particleActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

