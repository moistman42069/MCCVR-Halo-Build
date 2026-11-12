bool Halo2AnniversaryStereo_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool activeAndRange, bool levelRunning, bool coldPassed,
    bool remasteredRendererLive, uintptr_t observerResultArray) noexcept
{
    ReportPropagatedGameFaults(moduleBase, moduleSize);
    static uint32_t reportedScopeFaults=0;
    const auto scopeFaults=g_zoomFaults.load();
    if(scopeFaults!=reportedScopeFaults)
    { reportedScopeFaults=scopeFaults;LOG("Halo 2 Anniversary zoom stock fallback: optional scope faults=%u generation=%u; stereo core retained",scopeFaults,g_zoomFaultGeneration.load()); }
    uint32_t vrFailure = g_vrFailureGeneration.load(std::memory_order_acquire);
    if (vrFailure && generation && generation != vrFailure)
    {
        g_vrFailureGeneration.compare_exchange_strong(
            vrFailure, 0, std::memory_order_acq_rel, std::memory_order_acquire);
        vrFailure = g_vrFailureGeneration.load(std::memory_order_acquire);
    }

    const bool vrAvailable = !vrFailure || generation != vrFailure;
    if (PollManualVrRecovery(g_manualRecoveryGeneration, generation,
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && vrAvailable,
            moduleBase && moduleSize == kHalo2RetailImageSize && activeAndRange,
            [] { return RemoveCore("manual VR recovery"); },
            [] { g_rejectedGeneration = 0; }) == ManualVrRecoveryPoll::Waiting)
        return false;
    const bool desired = moduleBase && generation &&
        moduleSize == kHalo2RetailImageSize && activeAndRange && levelRunning &&
        coldPassed && vrAvailable && observerResultArray != 0 &&
        remasteredRendererLive;

    g_levelLive.store(levelRunning, std::memory_order_release);
    g_remasteredLive.store(remasteredRendererLive, std::memory_order_release);

    // A failed exit/partial-install cleanup must finish even if the same
    // retained module becomes eligible again before retirement completes.
    if (g_coreState == CoreState::CleanupRequired &&
        !RemoveCore("pending cleanup"))
        return false;

    const uint32_t owned = g_generation.load(std::memory_order_acquire);
    const bool foreignModule = (g_sceneTarget || g_rebuildTarget || g_hostUiTarget) &&
        (owned != generation ||
         g_moduleBase.load(std::memory_order_acquire) != moduleBase);

    if (!desired || foreignModule)
    {
        if (g_installed.load(std::memory_order_acquire) ||
            g_coreState != CoreState::StockFallback)
        {
            if (!RemoveCore(foreignModule ? "module generation changed"
                                         : "level or title no longer eligible"))
                return false;
        }
        if (TitleAdapter_GetActiveTitle() != GameTitle::Halo2 ||
            generation != g_rejectedGeneration)
            g_rejectedGeneration = 0;
        return false;
    }

    if (g_coreState != CoreState::Installed)
    {
        if (!InstallCore(moduleBase, generation, observerResultArray))
            return false;
    }

    g_armed.store(true, std::memory_order_release);
    if (g_armedLoggedGeneration != generation)
    {
        g_armedLoggedGeneration = generation;
        LOG("Halo 2 Anniversary stereo armed: the remastered scene render runs "
            "once per eye from one game frame; a frame that cannot produce a "
            "complete pair renders stock exactly once and never blacks out");
    }
    Report();
    return true;
}
