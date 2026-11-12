// H4 observer conversion and native per-window setup own this optical pass.
// Magnification comes from measured H4 projection gain, never an H3 field.
// `root` is the camera before the headset pose (Chief's eye, looking along his aim); `center` the rendered
// centre eye the two eye cameras are built around.
void Halo4RenderScopeBody(uintptr_t element,uintptr_t view,uint32_t window,
    const Halo4SetupArgs& args,const unsigned char* savedObserver,
    const Halo4CameraBasis& root,const Halo4CameraBasis& center,const Halo4FovCalibration& calibration)
{
    if(!VR_ScopeShouldRenderThisFrame())return;
    VR_InvalidateScopeImage();
    if(!calibration.learned||!g_halo4RigTracking.rightAimValid||
        !g_halo4OrigCuiGameplayRender||!g_halo4OrigModelSkinning||
        !std::isfinite(calibration.gain)||calibration.gain<=.001f)return;
    Halo4FloatingTargetFrame frame{};Halo4ControllerWorldPose controller{};
    if(!Halo4FreezeFloatingTargetFrame(frame))return;
    auto input=frame.common;
    memcpy(input.controllerOrientation,g_halo4RigTracking.rightAimOrientation,sizeof(input.controllerOrientation));
    memcpy(input.controllerPosition,g_halo4RigTracking.rightAimPosition,sizeof(input.controllerPosition));
    if(!Halo4BuildControllerWorldPose(input,controller))return;
    // SCOPE CONVERGENCE (F1 > Scope): from the rendered eye along the intent ray while the converged aim is
    // written -- the centre of the lens is then the reticle's point, as in every other title. Otherwise the
    // earlier lens: Chief's eye along his aim, which is the shot's own line but seen from where the character
    // stands.
    const float* scopeOrigin=root.position;
    float scopeForward[3]{root.forward[0],root.forward[1],root.forward[2]};
    float convergedForward[3]{};
    if(Sight_ScopeConvergedDirection(GameTitle::Halo4,convergedForward))
    {
        memcpy(scopeForward,convergedForward,sizeof(scopeForward));
        scopeOrigin=center.position;
    }
    ScopeCameraPose pose{};float aspect=1;
    if(!ComputeScopeCameraPose(controller.basis,scopeOrigin,scopeForward,pose)||
        !VR_GetScopeRenderAspect(aspect))return;
    const auto lens=ComputeScopeProjectionTangents(VR_GetScopeZoom(),aspect);
    const float fov=atanf(lens.vertical)/calibration.gain;
    if(!std::isfinite(fov)||fov<=.001f||fov>=3.14159f)return;
    unsigned char observer[kHalo4ObserverSnapshotBytes];memcpy(observer,savedObserver,sizeof(observer));
    memcpy(observer+kHalo4ObserverPositionOffset,pose.position,12);
    memcpy(observer+kHalo4ObserverForwardOffset,pose.forward,12);
    memcpy(observer+kHalo4ObserverUpOffset,pose.up,12);
    memcpy(observer+kHalo4ObserverVerticalFovOffset,&fov,4);
    if(!VR_BeginScopeRaster())return;
    const bool previous=g_scopeRenderActive.exchange(true);
    g_halo4ScopeRendering=true;g_halo4ScopePixelsValid=true;
    __try
    {
        if(!Halo4SafeWrite(reinterpret_cast<void*>(args.observer),observer,sizeof(observer)))return;
        g_halo4OrigSetup(args.view,args.window,args.count,args.mode,args.user,args.observer);
        float projection[16]{};
        if(!Halo4SafeRead(reinterpret_cast<void*>(element+kHalo4ElementProjectionMatrixOffset),projection,sizeof(projection)))return;
        float x=0,y=0,cx=0,cy=0;
        if(!Halo4DecodeSymmetricProjectionHalfFovs(projection,x,y,cx,cy)||
            fabsf(tanf(x)/lens.horizontal-1)>.01f||fabsf(tanf(y)/lens.vertical-1)>.01f)return;
        g_halo4OrigWrapper(element,view,window);
        if(g_halo4ScopePixelsValid)VR_CaptureScope();
    }
    __finally
    {
        g_halo4ScopeRendering=false;g_scopeRenderActive.store(previous);VR_EndScopeRaster();
        if(!Halo4RestoreMonoCamera(args,savedObserver))
        {VR_InvalidateScopeImage();g_halo4ScopeFaultGeneration.store(g_halo4Camera.generation.load());g_halo4ScopeFaults.fetch_add(1);}
    }
}
void Halo4RenderScope(uintptr_t element,uintptr_t view,uint32_t window,
    const Halo4SetupArgs& args,const unsigned char* savedObserver,
    const Halo4CameraBasis& root,const Halo4CameraBasis& center,const Halo4FovCalibration& calibration)
{
    __try {Halo4RenderScopeBody(element,view,window,args,savedObserver,root,center,calibration);}
    __except(EXCEPTION_EXECUTE_HANDLER)
    {VR_InvalidateScopeImage();g_halo4ScopeFaultGeneration.store(g_halo4Camera.generation.load());g_halo4ScopeFaults.fetch_add(1);}
}
