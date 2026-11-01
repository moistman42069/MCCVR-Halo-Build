    uint8_t LegacyCollisionSchedulerDetourBody(
        LegacyWorldCollisionFeature& feature, uint64_t flags, int32_t mode,
        const float* start, const float* desired, int32_t ignoredObjectA,
        int32_t ignoredObjectB, int32_t ignoredObjectC, void* result)
    {
        LegacyCollisionTestVectorFn original=
            reinterpret_cast<LegacyCollisionTestVectorFn>(feature.original);
        if(!original) return 0;
        const uint8_t nativeResult=original(
            flags,mode,start,desired,ignoredObjectA,ignoredObjectB,
            ignoredObjectC,result);
        if(!g_legacyCollisionOwnedQuery)
        {
            feature.engineCalls.fetch_add(1,std::memory_order_relaxed);
            // Only our extra contact work is optional. A fault in Halo's
            // original query above must reach its caller, not become a miss
            // with a partially populated collision result.
            __try { LegacyWorldCollisionTick(feature); }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                feature.failures.fetch_add(1,std::memory_order_relaxed);
                feature.installed.store(false,std::memory_order_release);
            }
        }
        return nativeResult;
    }

    __declspec(noinline) uint8_t __fastcall Halo3CollisionResolveDetour(
        uint64_t flags,int32_t mode,const float* start,const float* desired,
        int32_t ignoredObjectA,int32_t ignoredObjectB,
        int32_t ignoredObjectC,void* result)
    {
        Halo3HookCalled(Halo3Hook::WorldCollision);
        g_halo3WorldCollision.callbacks.fetch_add(
            1,std::memory_order_acq_rel);
        uint8_t nativeResult=0;
        const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        __try {
            // The crosshair / firing-assist query traces through the intent point from its own start
            // (LegacySightReaimCrosshairQuery: every build, fail closed). The engine-crosshair witness sees the
            // query as the game built it; the shot-lag witness sees what was traced.
            float reaimed[3]{},fromEyeStart[3]{},fromEyeVector[3]{};
            const float* query=desired;
            if(!g_legacyCollisionOwnedQuery)
            {
                LegacySightNoteEngineTrace(GameTitle::Halo3,flags,start,desired,caller);
                if(LegacySightReaimCrosshairQuery(GameTitle::Halo3,flags,start,desired,ignoredObjectA,
                        caller,reaimed))
                    query=reaimed;
                // A stock seat's line from the player's eye instead of the game's camera (seat_fire_from_eye).
                if(LegacySightSeatLineFromEye(GameTitle::Halo3,start,query,fromEyeStart,fromEyeVector))
                {
                    start=fromEyeStart;
                    query=fromEyeVector;
                }
                LegacySightNoteCrosshairQuery(GameTitle::Halo3,flags,start,query,ignoredObjectA,caller);
            }
            if(!Halo3RedirectContactVector(caller,flags,mode,ignoredObjectA,
                    ignoredObjectB,ignoredObjectC,result,nativeResult))
                nativeResult=LegacyCollisionSchedulerDetourBody(
                    g_halo3WorldCollision,flags,mode,start,query,ignoredObjectA,
                    ignoredObjectB,ignoredObjectC,result);
            // Where a seated crosshair query -- the line the shot follows -- stopped, against the point.
            if(!g_legacyCollisionOwnedQuery)
                LegacySightNoteCrosshairResult(GameTitle::Halo3,start,query,result,nativeResult);
#if HALOMCCVR_DEV_DIAGNOSTICS
            // Dev SHOT IMPACT (halo3_impact_witness.inl): a tracked projectile's own query records where it hit.
            if(!g_legacyCollisionOwnedQuery)
                Halo3ImpactWitnessNoteQuery(caller,start,query,result,nativeResult);
#endif
        }
        __finally
        {
            g_halo3WorldCollision.callbacks.fetch_sub(
                1,std::memory_order_acq_rel);
        }
        return nativeResult;
    }
