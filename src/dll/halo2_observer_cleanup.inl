#include "halo2_observer_retirement.inl"

bool RemoveParticleGate() noexcept
{
    Halo2RetirementHook hooks[]{ {&g_particleTarget,&g_particleOriginal,
        reinterpret_cast<const void*>(&Halo2ParticleRendererDetour),&g_particleActiveCallbacks} };
    return RetireHalo2ObserverHooks(hooks,std::size(hooks));
}

bool RemoveCore(const char* reason) noexcept
{
    if (g_coreState!=CoreState::CleanupRequired)
        LOG("Halo 2 observer disarming for cleanup: %s",reason?reason:"unspecified");
    g_coreState=CoreState::CleanupRequired;
    g_armed.store(false,std::memory_order_release);
    g_teardownRequested.store(true,std::memory_order_release);
    g_finalPaletteReady.store(false,std::memory_order_release);
    if constexpr(kHalo2DebugGlobalAimAssistOverrideEnabled) Game_Halo2RestoreAimAssist();
    if (!RemoveParticleGate()||!RemoveHalo2ContactMelee()||!RemoveHalo2WeaponHaptics()||!RemoveHalo2DualAim()||
        !RemoveHalo2WorldCollision()) return false;
    // The head-relative tracker's hook reads the drive's state (its written yaw, its control stamp): retire it
    // ahead of the drive, and before the module can go (halo2_head_relative.inl).
    if (!RemoveHalo2HeadRelative()) return false;
#if HALOMCCVR_DEV_DIAGNOSTICS
    RetireHalo2ListenerWitness();   // dev: its one caller (the tracker's detour) is gone; reads only, nothing to drain
#endif
    // The drive reads the unit through the accessor the unit-aim hook owns: retire it first. Left installed,
    // it outlived the module (a title switch unloads halo2.dll), and its stale target kept the next module
    // from ever being hooked.
    if (!RemoveHalo2Drive()) return false;
    g_footViewEngagedMs.store(0,std::memory_order_release);
    g_seatViewEngagedMs.store(0,std::memory_order_release);
    Halo2RetirementHook hooks[]{
        {&g_target,&g_originalAddress,reinterpret_cast<const void*>(&Halo2ObserverFinalTransformDetour),&g_activeCallbacks},
        {&g_interpFrameTarget,&g_interpFrameOriginal,reinterpret_cast<const void*>(&Halo2InterpolatedFrameDetour),&g_interpFrameActiveCallbacks},
        {&g_packetBuilderTarget,&g_packetBuilderOriginal,reinterpret_cast<const void*>(&Halo2FirstPersonPacketBuilderDetour),&g_packetBuilderActiveCallbacks},
        {&g_visibleConsumerTarget,&g_visibleConsumerOriginal,reinterpret_cast<const void*>(&Halo2VisibleFirstPersonConsumerDetour),&g_visibleConsumerActiveCallbacks},
        {&g_finalPaletteTarget,&g_finalPaletteOriginal,reinterpret_cast<const void*>(&Halo2FinalPaletteComposeDetour),&g_finalPaletteActiveCallbacks},
        {&g_weaponAimTarget,&g_weaponAimOriginal,reinterpret_cast<const void*>(&Halo2WeaponAimHelperDetour),&g_weaponAimActiveCallbacks},
        {&g_aimAssistTarget,&g_aimAssistOriginal,reinterpret_cast<const void*>(&Halo2AimAssistCalculateDetour),&g_aimAssistActiveCallbacks},
        {&g_aimAssistViewDirectionTarget,&g_aimAssistViewDirectionOriginal,reinterpret_cast<const void*>(&Halo2AimAssistViewDirectionDetour),&g_aimAssistViewDirectionActiveCallbacks},
        {&g_nativeAimTarget,&g_nativeAimOriginal,reinterpret_cast<const void*>(&Halo2NativeAimUpdateDetour),&g_nativeAimActiveCallbacks},
        {&g_reanchorTarget,&g_reanchorOriginal,reinterpret_cast<const void*>(&Halo2InterpolatorReadDetour),&g_reanchorActiveCallbacks},
        {&g_weaponsTarget,&g_weaponsOriginal,reinterpret_cast<const void*>(&Halo2FirstPersonWeaponsDetour),&g_weaponsActiveCallbacks}
    };
    if (!RetireHalo2ObserverHooks(hooks,std::size(hooks))) return false;
    g_packetBuilderLastAppliedMs.store(0,std::memory_order_release);
    g_visibleConsumerContext={};g_finalPaletteContext={};g_aimAssistControllerRayScope={};
    g_objectDatumAccessor.store(0,std::memory_order_release);
    g_vehicleSeatVerified.store(false,std::memory_order_release);
    g_vehicleFrameVerified.store(false,std::memory_order_release);
    g_vehicleSeatSample.store(0,std::memory_order_release);
    g_passCamerasSet.store(false,std::memory_order_release);
    for(auto& cache:g_slotCache) cache.valid=false;
    g_interpolatorResetAddress.store(0,std::memory_order_release);
    g_weaponTickIndex.store(0,std::memory_order_release);
    g_weaponTickPreviousIndex.store(0,std::memory_order_release);
    g_weaponTickGeneration.store(0,std::memory_order_release);
    g_observerResult.store(0,std::memory_order_release);
    g_moduleBase.store(0,std::memory_order_release);
    g_generation.store(0,std::memory_order_release);
    g_installed.store(false,std::memory_order_release);
    g_referenceValid.store(false,std::memory_order_release);
    g_recenterRequested.store(true,std::memory_order_release);
    g_coreState=CoreState::StockFallback;
    g_moduleReference=nullptr; // non-owning; never pins MCC across levels
    LOG("Halo 2 observer 6DOF removed (%s); stock camera restored",reason?reason:"cleanup");
    return true;
}
