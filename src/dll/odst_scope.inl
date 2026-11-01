// H3 behavior: an independently magnified world view follows the gun's aim,
// while the two headset views remain wide. ODST uses its own verified compact,
// derived and nested-camera layout and native rebuild/upload functions.
std::atomic<uint32_t> g_odstScopeFaultGeneration{0};
std::atomic<uint64_t> g_odstScopeRendered{0},g_odstScopeRefused{0},g_odstScopeFaults{0};
// Where the pass stopped, for ReportOdstScope: entered (once per drawn stereo frame), prerequisites missing, lens
// wanted (open this frame), skipped after that (cinematic, pose, field of view, layout).
std::atomic<uint64_t> g_odstScopeEntered{0},g_odstScopeGated{0},g_odstScopeWanted{0},g_odstScopeSkipped{0};

void RenderOdstScopeBody(void* view)
{
    g_odstScopeEntered.fetch_add(1,std::memory_order_relaxed);
    const auto generation=g_odstRuntimeGeneration.load(std::memory_order_acquire);
    if(!view||!generation||g_odstScopeFaultGeneration.load()==generation||
        !g_odstCamera.armed.load()||g_odstCamera.teardownRequested.load()||
        !g_odstCamera.buildViewport||!g_odstCamera.buildMatrices||
        !g_odstCamera.prepareView||!g_odstCamera.originalRenderView)
    {g_odstScopeGated.fetch_add(1,std::memory_order_relaxed);return;}
    if(!VR_ScopeShouldRenderThisFrame())return;
    g_odstScopeWanted.fetch_add(1,std::memory_order_relaxed);
    int32_t scene=-1,shot=-1;
    if(ReadOdstCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled)
    {g_odstScopeSkipped.fetch_add(1,std::memory_order_relaxed);return;}
    const auto& layout=kOdstCameraProfile.layout;
    auto* bytes=static_cast<unsigned char*>(view);
    auto* camera=bytes+layout.rootCurrentCompact;
    float basis[9]{},seat[3]{},scale=1;
    float direction[3]{g_aimFwdX.load(),g_aimFwdY.load(),g_aimFwdZ.load()};
    // SCOPE CONVERGENCE (F1 > Scope): along the intent ray from this rendered eye while the converged aim is
    // written, not along the aim (Halo 3's BuildRightHandScopeCamera says why).
    float convergedForward[3]{};
    if(Sight_ScopeConvergedDirection(GameTitle::Halo3ODST,convergedForward))
        std::memcpy(direction,convergedForward,sizeof(direction));
    ScopeCameraPose pose{};float aspect=4.f/3.f;
    if(!ControllerWorldPoseEx(false,basis,seat,scale)||
        !ComputeScopeCameraPose(basis,reinterpret_cast<const float*>(camera+layout.compactPosition),direction,pose)||
        !VR_GetScopeRenderAspect(aspect))
    {g_odstScopeSkipped.fetch_add(1,std::memory_order_relaxed);return;}
    const auto tangents=ComputeScopeProjectionTangents(VR_GetScopeZoom(),aspect);
    OdstHalo3FovMatch fov{};
    if(!ComputeOdstHalo3FovMatch(atanf(tangents.horizontal),atanf(tangents.vertical),fov))
    {g_odstScopeSkipped.fetch_add(1,std::memory_order_relaxed);return;}
    constexpr size_t capacity=0xC0;
    const uintptr_t offsets[]{layout.rootCurrentCompact,layout.rootCurrentDerived,
        layout.rootSecondaryCompact,layout.rootSecondaryDerived,layout.nestedCurrentCompact,
        layout.nestedCurrentDerived,layout.nestedSecondaryCompact,layout.nestedSecondaryDerived};
    const size_t sizes[]{layout.compactSize,layout.derivedSize,layout.compactSize,layout.derivedSize,
        layout.compactSize,layout.derivedSize,layout.compactSize,layout.derivedSize};
    alignas(16) unsigned char saved[8][capacity]{};
    for(int i=0;i<8;++i){if(sizes[i]>capacity){g_odstScopeSkipped.fetch_add(1);return;}std::memcpy(saved[i],bytes+offsets[i],sizes[i]);}
    if(!VR_BeginScopeRaster()){g_odstScopeRefused.fetch_add(1);return;}
    const FpInterpolationContext contexts[]{g_fpInterpolationContexts[0],g_fpInterpolationContexts[1]};
    const bool previousScope=g_scopeRenderActive.exchange(true,std::memory_order_acq_rel);
    __try
    {
        std::memcpy(camera+layout.compactPosition,pose.position,12);
        std::memcpy(camera+layout.compactForward,pose.forward,12);
        std::memcpy(camera+layout.compactUp,pose.up,12);
        *reinterpret_cast<float*>(camera+layout.verticalFov)=fov.compactVerticalInput;
        *reinterpret_cast<float*>(camera+layout.referenceFov)=fov.compactReferenceInput;
        alignas(16) unsigned char temporary[0x40]{};
        g_odstCamera.buildViewport(camera,temporary);
        g_odstCamera.buildMatrices(camera,temporary,bytes+layout.rootCurrentDerived,0.f);
        auto* projection=reinterpret_cast<float*>(bytes+layout.rootCurrentDerived+layout.projectionMatrix);
        projection[0]=fov.projectionX;projection[5]=fov.projectionY;
        for(int i=2;i<8;i+=2)
        {
            std::memcpy(bytes+offsets[i],camera,layout.compactSize);
            std::memcpy(bytes+offsets[i+1],bytes+layout.rootCurrentDerived,layout.derivedSize);
        }
        if(g_odstCamera.fpCameraUpload)
            g_odstCamera.fpCameraUpload(bytes+layout.rootSecondaryCompact,bytes+layout.rootSecondaryDerived);
        g_odstCamera.prepareView(view,0);
        g_odstCamera.originalRenderView(view);
        VR_CaptureScope();g_odstScopeRendered.fetch_add(1,std::memory_order_relaxed);
    }
    __finally
    {
        for(int i=0;i<8;++i)std::memcpy(bytes+offsets[i],saved[i],sizes[i]);
        g_fpInterpolationContexts[0]=contexts[0];g_fpInterpolationContexts[1]=contexts[1];
        g_scopeRenderActive.store(previousScope,std::memory_order_release);
        VR_EndScopeRaster();
    }
}
void RenderOdstScope(void* view)
{
    __try {RenderOdstScopeBody(view);}
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_odstScopeFaultGeneration.store(g_odstRuntimeGeneration.load(),std::memory_order_release);
        g_odstScopeFaults.fetch_add(1,std::memory_order_relaxed);
        // An optional third-view failure never retries the native draw or
        // changes camera-core ownership. Its next request stays native.
    }
}
void ReportOdstScope()
{
    const auto faults=g_odstScopeFaults.exchange(0);
    if(faults)LOG("ODST scope StockFallback: optional third view faulted %llu times; camera core retained",faults);
    // The pass's own account since the last report, written when which of its counts are non-zero changes (an idle
    // pass, then a lens opened, drawn, refused or never asked for).
    const uint64_t entered=g_odstScopeEntered.exchange(0),gated=g_odstScopeGated.exchange(0),
        wanted=g_odstScopeWanted.exchange(0),skipped=g_odstScopeSkipped.exchange(0),
        refused=g_odstScopeRefused.exchange(0),rendered=g_odstScopeRendered.exchange(0);
    static unsigned reported=~0u;
    const unsigned pattern=(entered?1u:0u)|(gated?2u:0u)|(wanted?4u:0u)|(skipped?8u:0u)|(refused?16u:0u)|
        (rendered?32u:0u);
    if(pattern!=reported)
    {
        reported=pattern;
        LOG("ODST zoom lens pass: entered %llu, prerequisites missing %llu, lens open %llu, skipped %llu, "
            "raster refused %llu, drawn %llu",entered,gated,wanted,skipped,refused,rendered);
    }
}
