// Included by game.cpp and the production pause-lifetime fixture.
    bool Halo4ReadNativePaused(bool& paused)
    {
        paused = false;
        if (!g_halo4Restoration.pauseProven.load(
                std::memory_order_acquire) ||
            !g_halo4Restoration.pauseReason)
            return false;
        __try
        {
            // The proven getter immediately dereferences *(TLS+0x90). During
            // title retirement the Present thread can outlive that native
            // pause block (community f53f0bd fault halo4+A0B03). Missing thread
            // state means unknown, not resumed; do not call a native routine
            // whose own precondition is already known to be absent.
            if (!g_halo4EngineTlsIndex || *g_halo4EngineTlsIndex >= 0x200)
                return false;
            const auto* const* slots = reinterpret_cast<const uint8_t* const*>(
                __readgsqword(0x58));
            const auto* tls = slots ? slots[*g_halo4EngineTlsIndex] : nullptr;
            if (!tls || !*reinterpret_cast<const void* const*>(tls + 0x90))
                return false;
            paused = g_halo4Restoration.pauseReason(3);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

