// Legacy Halo 3 body switches. Boolean-width/lifetime hardening only;
// these controls do not prove full-body visibility or preserve FP hands.
    // These are type-5 engine booleans, not dwords. The official H3 kit places
    // debug_first_person_models directly beside other one-byte flags.
    struct EngineVarSlot { uint8_t* slot=nullptr; uint8_t original=0; uint8_t onValue=0; };
    EngineVarSlot g_bodyVars[3];
    std::atomic<int> g_bodyVarCount{0};
    std::atomic<bool> g_bodyApplied{false};
    std::atomic<uint32_t> g_bodyVarGeneration{0};
    std::atomic<unsigned> g_bodyNotice{0};

    void RefuseBodySwitch(bool restore)
    {
        const int n=g_bodyVarCount.exchange(0,std::memory_order_acq_rel);
        if(restore) for(int i=0;i<n;++i)
            (void)SafeWriteByte(g_bodyVars[i].slot,g_bodyVars[i].original);
        g_bodyApplied.store(false,std::memory_order_release);
        g_bodyNotice.store(3,std::memory_order_release);
    }

    void ResolveBodyVars(uintptr_t base, size_t size)
    {
        g_bodyVarCount.store(0,std::memory_order_release);
        g_bodyApplied.store(false,std::memory_order_release);
        struct { const char* name; uint8_t on; } wanted[3] = {
            {"director_disable_first_person", 1}, // stop treating view as FP
            {"render_first_person", 0},           // hide the viewmodel layer
            {"debug_first_person_models", 1},     // require a live typed slot
        };
        int n=0;
        for (auto& w : wanted)
        {
            auto* slot=static_cast<uint8_t*>(FindDebugVarSlot(base,size,w.name,5));
            uint8_t original=0;
            if (slot && (!SafeReadByte(slot,&original) || original>1)) slot=nullptr;
            if (slot) { g_bodyVars[n++]={slot,original,w.on}; }
            LOG("VRIK: body switch '%s' -> %p%s", w.name, slot,
                slot?"":" (null at install; may init at runtime)");
        }
        g_bodyVarGeneration.store(g_halo3RuntimeGeneration.load(std::memory_order_acquire),
                                  std::memory_order_release);
        // A partial combination can hide working FP hands without enabling
        // the body path. Missing any required switch keeps this feature stock.
        g_bodyVarCount.store(n==3?n:0,std::memory_order_release);
        if(n!=3)g_bodyNotice.store(3,std::memory_order_release);
        LOG("VRIK: %d/3 legacy body booleans resolved; full-body rendering remains unverified",n);
    }

    void ApplyBodySetting()
    {
        const int n=g_bodyVarCount.load(std::memory_order_acquire);
        const auto generation=g_bodyVarGeneration.load(std::memory_order_acquire);
        if (!n || !generation ||
            TitleAdapter_GetActiveTitle()!=GameTitle::Halo3 ||
            TitleAdapter_GetGeneration(GameTitle::Halo3)!=generation ||
            g_halo3RuntimeGeneration.load(std::memory_order_acquire)!=generation) return;
        // The avatar consumes the committed first-person wrist palette.
        // Its opt-in takes precedence over the legacy switch that suppresses
        // that producer; retain body_wip so disabling IK restores preference.
        if (g_config.body_wip && !g_config.experimental_body_ik)
        {
            if (!g_bodyApplied.exchange(true))
            {
                g_bodyNotice.store(1,std::memory_order_release);
                for (int i=0;i<n;++i)
                    if (!SafeReadByte(g_bodyVars[i].slot,&g_bodyVars[i].original)) {
                        RefuseBodySwitch(false);return;
                    }
            }
            for (int i=0;i<n;++i)
                if (!SafeWriteByte(g_bodyVars[i].slot,g_bodyVars[i].onValue)) {
                    RefuseBodySwitch(true);return;
                }
        }
        else if (g_bodyApplied.exchange(false))
        {
            g_bodyNotice.store(2,std::memory_order_release);
            for (int i=0;i<n;++i)
                if (!SafeWriteByte(g_bodyVars[i].slot,g_bodyVars[i].original)) {
                    RefuseBodySwitch(true);return;
                }
        }
    }
