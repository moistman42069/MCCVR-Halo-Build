    bool RemoveCore(const char* reason) noexcept
    {
        g_armed.store(false, std::memory_order_release);
        g_teardown.store(true, std::memory_order_release);
        g_coreState = CoreState::CleanupRequired;
        if (g_sceneTarget || g_rebuildTarget || g_hostUiTarget || g_viewTargetsTarget)
        {
            for (void* target : {g_sceneTarget, g_rebuildTarget, g_hostUiTarget, g_viewTargetsTarget})
            {
                if (!target) continue;
                const auto status = MCCVR_DisableHookForRetirement(target);
                if (status != MH_OK && status != MH_ERROR_DISABLED &&
                    status != MH_ERROR_NOT_CREATED) return false;
            }
            for (int i = 0; i < 200 &&
                 g_activeCallbacks.load(std::memory_order_acquire); ++i)
            {
                Sleep(10);
            }
            if (g_activeCallbacks.load(std::memory_order_acquire))
                return false;
            const void* functions[] = {
                reinterpret_cast<const void*>(&SaberSceneDetour),
                reinterpret_cast<const void*>(&RebuildDetour),
                reinterpret_cast<const void*>(&HostUiDetour),
                reinterpret_cast<const void*>(&ViewTargetsDetour)};
            const void* originals[] = {
                g_sceneTarget ? reinterpret_cast<const void*>(g_originalScene.load()) : nullptr,
                g_rebuildTarget ? reinterpret_cast<const void*>(g_originalRebuild.load()) : nullptr,
                g_hostUiTarget ? reinterpret_cast<const void*>(g_originalHostUi.load()) : nullptr,
                g_viewTargetsTarget ? reinterpret_cast<const void*>(g_originalViewTargets.load()) : nullptr};
            // A callback can be suspended before incrementing the counter.
            // Retain every remaining trampoline until its detour entry and
            // trampoline instruction ranges are also quiescent.
            if (!WaitForNativeDetourQuiescence(functions, originals, 4, g_activeCallbacks))
                return false;
            MH_STATUS status = MH_OK;
            if (g_sceneTarget)
            {
                status = MH_RemoveHook(g_sceneTarget);
                if (status != MH_OK && status != MH_ERROR_NOT_CREATED) return false;
                g_sceneTarget = nullptr;
            }
            if (g_rebuildTarget)
            {
                status = MH_RemoveHook(g_rebuildTarget);
                if (status != MH_OK && status != MH_ERROR_NOT_CREATED) return false;
                g_rebuildTarget = nullptr;
            }
            if (g_hostUiTarget)
            {
                status = MH_RemoveHook(g_hostUiTarget);
                if (status != MH_OK && status != MH_ERROR_NOT_CREATED) return false;
                g_hostUiTarget = nullptr;
            }
            if (g_viewTargetsTarget)
            {
                status = MH_RemoveHook(g_viewTargetsTarget);
                if (status != MH_OK && status != MH_ERROR_NOT_CREATED) return false;
                g_viewTargetsTarget = nullptr;
            }
        }
        g_originalScene.store(0, std::memory_order_release);
        g_originalRebuild.store(0, std::memory_order_release);
        g_originalHostUi.store(0, std::memory_order_release);
        g_originalViewTargets.store(0, std::memory_order_release);
        g_fpPatchRecord.store(0, std::memory_order_release);
        g_rebuildMatrices.store(0, std::memory_order_release);
        g_cameraCommit.store(0, std::memory_order_release);
        g_cameraRefreshRect.store(0, std::memory_order_release);
        g_moduleBase.store(0, std::memory_order_release);
        g_observerResult.store(0, std::memory_order_release);
        g_generation.store(0, std::memory_order_release);
        g_installed.store(false, std::memory_order_release);
        g_referenceValid.store(false, std::memory_order_release);
        g_recenterRequested.store(true, std::memory_order_release);
        g_lastCompletedSerial.store(0, std::memory_order_release);
        g_coreState = CoreState::StockFallback;
        // Non-owning identity; MCC alone owns the title DLL lifetime.
        g_moduleReference = nullptr;
        if (reason)
            LOG("Halo 2 Anniversary stereo removed (%s); stock rendering restored",
                reason);
        return true;
    }
