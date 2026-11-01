            // H2EK render_view owns visibility as well as rasterization. Run
            // the optional mono view only after both real eye images are safe
            // (in a mono frame, its one image).
            if(scope->completedEyeMask==Halo2ExpectedEyeMask(scope->mono,firstEye)&&!scope->invalidated&&
                g_zoomFaultGeneration.load()!=scope->generation&&VR_ScopeShouldRenderThisFrame())
            {
                VR_InvalidateScopeImage();
                Halo2CameraBasis camera{};float aspect=1;
                if(Halo2Observer6Dof_BuildScopeCamera(camera)&&VR_GetScopeRenderAspect(aspect)&&
                    VR_BeginHalo2Scope(scope->generation,scope->serial))
                {
                    StereoScope lens=*scope;
                    const auto tangents=ComputeScopeProjectionTangents(VR_GetScopeZoom(),aspect);
                    lens.eyes[0].render=lens.eyes[0].raster=camera;
                    lens.renderCoverVerticalFov=lens.rasterCoverVerticalFov=2*atanf(tangents.vertical);
                    bool rendered=false;
                    uint8_t previousLatch=0;
                    const bool latchSaved=ReadByteGuarded(moduleBase+kHalo2ClassicSceneTargetLatchRva,previousLatch);
                    const bool previousScope=Game_SetScopeRendering(true);
                    __try
                    {
                        __try
                        {
                            if(latchSaved&&scope->sceneTargetLatchValid&&
                                WriteByteGuarded(moduleBase+kHalo2ClassicSceneTargetLatchRva,scope->sceneTargetLatch)&&
                                WriteEyeSpans(lens,0))
                            {
                                original(argument01,argument02,argument03,argument04,argument05,
                                    argument06,argument07,argument08,argument09,argument10,argument11,
                                    argument12,argument13,argument14,argument15,argument16,argument17,argument18,argument19);
                                rendered=ReadEngineProjection(lens,0)&&lens.engineHalfFovsValid[0]&&
                                    fabsf(tanf(lens.engineHalfFovs[0].horizontal)/tangents.horizontal-1)<.01f&&
                                    fabsf(tanf(lens.engineHalfFovs[0].vertical)/tangents.vertical-1)<.01f;
                            }
                        }
                        __finally
                        {
                            if(!RestoreOwnedSpans(lens))rendered=false;
                            if(latchSaved&&!WriteByteGuarded(moduleBase+kHalo2ClassicSceneTargetLatchRva,previousLatch))rendered=false;
                            Game_SetScopeRendering(previousScope);VR_EndHalo2Scope(rendered);
                        }
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {g_zoomFaultGeneration.store(scope->generation);g_zoomFaults.fetch_add(1);VR_InvalidateScopeImage();}
                }
            }
