// H3 experience, Reach ownership: one outer visibility/frame-once call, followed
// by a mono scope and the unchanged two eye passes within that same native scope.
// The scope and head share an origin so their angular union is conservative.
bool ReachPrepareScopeBody(ReachOwnerScope& frame,unsigned char* headDerived,
                           ReachVrRenderAccess& access)
{
    if(!VR_ScopeShouldRenderThisFrame())return false;
    VR_InvalidateScopeImage();
    const auto generation=g_reachCamera.generation.load();
    if(frame.cutsceneTheater||g_reachCinematicLocked.load()||!generation||
        g_reachScopeFaultGeneration.load()==generation||
        !g_reachFirstPersonRenderGateReturn.load()||!g_reachCamera.hudDrawWidgetTarget||
        !g_reachCamera.decoratorWindState||g_reachWindReplayFaults.load()||
        !VR_ReachScopeReady(access))return false;
    float basis[9]{},seat[3]{},scale=1,aspect=1;
    float direction[]{g_aimFwdX.load(),g_aimFwdY.load(),g_aimFwdZ.load()};
    // SCOPE CONVERGENCE (F1 > Scope): along the intent ray from the head while the converged aim is
    // written, not along the aim (Halo 3's BuildRightHandScopeCamera says why).
    float convergedForward[3]{};
    if(Game_ReachScopeConvergedDirection(convergedForward))
        memcpy(direction,convergedForward,sizeof(direction));
    ScopeCameraPose pose{};
    if(!ControllerWorldPoseEx(false,basis,seat,scale)||!VR_GetScopeRenderAspect(aspect)||
        !ComputeScopeCameraPose(basis,reinterpret_cast<const float*>(frame.headCenter),direction,pose))return false;
    const auto lens=ComputeScopeProjectionTangents(VR_GetScopeZoom(),aspect);
    ReachScopeFrame scope{};
    memcpy(scope.compact,frame.headCenter,sizeof(scope.compact));
    memcpy(scope.compact,pose.position,12);memcpy(scope.compact+0xC,pose.forward,12);
    memcpy(scope.compact+0x18,pose.up,12);
    *reinterpret_cast<float*>(scope.compact+0x28)=2*atanf(lens.vertical);
    float bounds[4]{};
    if(!g_reachHelpers.frustum(scope.compact,bounds))return false;
    for(float v:bounds)if(!std::isfinite(v))return false;
    g_reachHelpers.projection(scope.compact,bounds,scope.derived,0.f);
    const auto* scopeProjection=reinterpret_cast<const float*>(scope.derived+kReachDerivedProjectionOffset);
    // The native fixed-aspect projection must agree with the actual capture.
    if(!std::isfinite(scopeProjection[0])||!std::isfinite(scopeProjection[5])||
        fabsf(fabsf(scopeProjection[0])*lens.horizontal-1)>.005f||
        fabsf(fabsf(scopeProjection[5])*lens.vertical-1)>.005f)return false;
    const auto* oldProjection=reinterpret_cast<const float*>(headDerived+kReachDerivedProjectionOffset);
    ScopeProjectionTangents cover{1/fabsf(oldProjection[0]),1/fabsf(oldProjection[5])};
    const float headAspect=cover.horizontal/cover.vertical;
    if(!ExpandScopeCullTangents(reinterpret_cast<const float*>(frame.headCenter+0xC),
        reinterpret_cast<const float*>(frame.headCenter+0x18),pose,lens,cover))return false;
    const float vertical=2*atanf(fmaxf(cover.vertical,cover.horizontal/headAspect));
    if(!std::isfinite(vertical)||vertical>=3.0f)return false;
    alignas(16) unsigned char expanded[kReachCompactCameraBytes],derived[kReachDerivedBlockSize];
    memcpy(expanded,frame.headCenter,sizeof(expanded));
    *reinterpret_cast<float*>(expanded+0x28)=vertical;
    if(!g_reachHelpers.frustum(expanded,bounds))return false;
    for(float v:bounds)if(!std::isfinite(v))return false;
    g_reachHelpers.projection(expanded,bounds,derived,0.f);
    const auto* p=reinterpret_cast<const float*>(derived+kReachDerivedProjectionOffset);
    if(!std::isfinite(p[0])||!std::isfinite(p[5])||fabsf(p[0])<=0||fabsf(p[5])<=0||
        1/fabsf(p[0])+1.e-4f<cover.horizontal||1/fabsf(p[5])+1.e-4f<cover.vertical)return false;
    // Commit local data only after the complete optional projection is valid.
    memcpy(frame.headCenter,expanded,sizeof(expanded));memcpy(headDerived,derived,sizeof(derived));
    scope.ready=true;frame.scope=scope;return true;
}
void ReachPrepareScope(ReachOwnerScope& frame,unsigned char* derived,ReachVrRenderAccess& access)
{
    __try {(void)ReachPrepareScopeBody(frame,derived,access);}
    __except(EXCEPTION_EXECUTE_HANDLER)
    {g_reachScopeFaultGeneration.store(g_reachCamera.generation.load());g_reachScopeFaults.fetch_add(1);}
}

void ReachRenderScopeBody(uintptr_t playerView,ReachVrRenderAccess& access)
{
    const auto& frame=g_reachOwnerScope;
    if(!frame.scope.ready||!VR_ReachScopeReady(access))return;
    const uintptr_t workspace=frame.workspace;
    const uintptr_t ownerAddress=g_reachCamera.base+kReachRenderCameraOwnerRva;
    const uintptr_t owner=*reinterpret_cast<const uintptr_t*>(ownerAddress);
    if(owner!=playerView+kReachPlayerViewCameraStateOffset)return;
    alignas(16) unsigned char savedWorkspace[kReachRenderScopeSnapshotSize],savedPv[kReachPvSnapshotBytes];
    unsigned char wind[ReachWindReplay::kBytes]{};
    void* const windState=g_reachCamera.decoratorWindState;
    if(!windState||!ReachWindCopy(wind,windState,sizeof(wind)))return;
    memcpy(savedWorkspace,reinterpret_cast<void*>(workspace),sizeof(savedWorkspace));
    memcpy(savedPv,reinterpret_cast<void*>(playerView+kReachPvSnapshotBegin),sizeof(savedPv));
    const auto previousFp=g_reachFpCameraEyeScope;
    const bool previousScope=g_scopeRenderActive.exchange(true);
    g_reachScopeRendering=true;
    __try
    {
        memcpy(reinterpret_cast<void*>(workspace),frame.scope.compact,kReachCompactCameraBytes);
        memcpy(reinterpret_cast<void*>(workspace+kReachPrimaryDerivedOffset),frame.scope.derived,kReachDerivedBlockSize);
        memcpy(reinterpret_cast<void*>(workspace+kReachSecondaryCompactOffset),frame.scope.compact,kReachCompactCameraBytes);
        memcpy(reinterpret_cast<void*>(workspace+kReachSecondaryDerivedOffset),frame.scope.derived,kReachDerivedBlockSize);
        auto* compact=reinterpret_cast<unsigned char*>(workspace);
        auto* derived=reinterpret_cast<unsigned char*>(workspace+kReachPrimaryDerivedOffset);
        g_reachHelpers.cameraState(reinterpret_cast<void*>(owner),compact);
        g_reachHelpers.matrix(reinterpret_cast<void*>(playerView+kReachPlayerViewCurrentMatricesOffset),
            derived,derived+kReachDerivedProjectionOffset,compact+kReachCompactRenderBoundsOffset,
            reinterpret_cast<void*>(playerView+kReachPlayerViewProjectionOffsetPairOffset));
        g_reachHelpers.commitOuterCamera();
        *reinterpret_cast<uintptr_t*>(ownerAddress)=owner;
        *reinterpret_cast<uint8_t*>(playerView+kReachLastWindowFlagOffset)=0;
        g_reachFpCameraEyeScope.active=false;
        if(ReachCallPlayerViewWithEyeScopedSuppressions(playerView))
            (void)VR_ReachCopyScope(access);
        else
        {g_reachScopeFaultGeneration.store(g_reachCamera.generation.load());g_reachScopeFaults.fetch_add(1);}
    }
    __finally
    {
        memcpy(reinterpret_cast<void*>(workspace),savedWorkspace,sizeof(savedWorkspace));
        memcpy(reinterpret_cast<void*>(playerView+kReachPvSnapshotBegin),savedPv,sizeof(savedPv));
        *reinterpret_cast<uintptr_t*>(ownerAddress)=owner;
        (void)ReachWindCopy(windState,wind,sizeof(wind));
        g_reachFpCameraEyeScope=previousFp;
        g_reachScopeRendering=false;g_scopeRenderActive.store(previousScope);
    }
}
void ReachRenderScope(uintptr_t playerView,ReachVrRenderAccess& access)
{
    __try {ReachRenderScopeBody(playerView,access);}
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        VR_InvalidateScopeImage();g_reachScopeFaultGeneration.store(g_reachCamera.generation.load());
        g_reachScopeFaults.fetch_add(1);
    }
}
void ReportReachScope()
{
    const auto faults=g_reachScopeFaults.exchange(0);
    if(faults)LOG("Reach scope StockFallback: optional lens faulted %u times; camera core retained",faults);
}
