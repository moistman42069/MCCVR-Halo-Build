        if(ok&&haveLatch&&g_zoomFaultGeneration.load()!=generation&&VR_ScopeShouldRenderThisFrame()&&ViewSlotsIntact(base))
        {
            VR_InvalidateScopeImage();
            Halo2CameraBasis lensCamera{};float aspect=1;
            if(Halo2Observer6Dof_BuildScopeCamera(lensCamera)&&VR_GetScopeRenderAspect(aspect)&&
                VR_BeginHalo2Scope(generation,serial))
            {
                const auto tangents=ComputeScopeProjectionTangents(VR_GetScopeZoom(),aspect);
                Halo2SaberEyeCover lens=cover;
                lens.halfHorizontalRadians=atanf(tangents.horizontal);lens.halfVerticalRadians=atanf(tangents.vertical);
                lens.horizontalDegrees=2*lens.halfHorizontalRadians*57.2957795f;
                lens.verticalDegrees=2*lens.halfVerticalRadians*57.2957795f;
                bool rendered=false,contextSaved=false;
                unsigned char postEyeContext[kHalo2SaberSceneContextResetBytes];
                const bool previousScope=Game_SetScopeRendering(true);
                __try
                {
                    __try
                    {
                        memcpy(postEyeContext,reinterpret_cast<uint8_t*>(ctx)+kHalo2SaberSceneContextResetOffset,sizeof(postEyeContext));
                        contextSaved=true;
                        memcpy(reinterpret_cast<uint8_t*>(ctx)+kHalo2SaberSceneContextResetOffset,savedContext,kHalo2SaberSceneContextResetBytes);
                        *reinterpret_cast<int32_t*>(base+kHalo2SaberSceneOnceLatchRva)=savedLatch;
                        if(ApplyEyeCamera(record,lensCamera,constants,lens))
                        {
                            original(ctx,rdx,viewIndex);
                            float sx=0,sy=0;Halo2SymmetricHalfFovs actual{};
                            rendered=ReadGuarded(record+kHalo2SaberProjectionScaleXOffset,sx)&&
                                ReadGuarded(record+kHalo2SaberProjectionScaleYOffset,sy)&&
                                Halo2HalfFovsFromProjectionScales(sx,sy,actual)&&
                                fabsf(tanf(actual.horizontal)/tangents.horizontal-1)<.01f&&
                                fabsf(tanf(actual.vertical)/tangents.vertical-1)<.01f;
                        }
                    }
                    __finally
                    {
                        // Restore the state left by the real right eye, not
                        // the pre-frame reset template used to admit a view.
                        __try
                        {
                            if(contextSaved)memcpy(reinterpret_cast<uint8_t*>(ctx)+kHalo2SaberSceneContextResetOffset,
                                postEyeContext,sizeof(postEyeContext));
                        }
                        __finally {Game_SetScopeRendering(previousScope);VR_EndHalo2Scope(rendered&&!AbnormalTermination());}
                    }
                }
                __except(GameFault(faultAnchor)) // the scene render is the game's code
                {g_zoomFaultGeneration.store(generation);g_zoomFaults.fetch_add(1);VR_InvalidateScopeImage();}
            }
        }
