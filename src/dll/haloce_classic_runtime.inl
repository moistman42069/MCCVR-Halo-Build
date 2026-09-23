// Included inside haloce_stereo_core.cpp's private namespace, after LiveGame.
// E-CE-C1..C4, docs/HALOCE-CLASSIC-EVIDENCE-2026-09-15.md.
// Classic owns a separate hook transaction: optional installation failure must
// not remove an installed Anniversary camera or any existing title's hooks.
using ClassicGameRenderFn=void(__fastcall*)(float,float);
using ClassicWindowFn=void(__fastcall*)(Window*);
using ClassicBlitFn=void(__fastcall*)(const halo_ce::Rectangle*);
using ClassicViewFn=void(__fastcall*)(int16_t,const Camera*,const void*,const Camera*,const void*,int16_t,uint8_t);
enum ClassicHookIndex { ClassicGameRender,ClassicPlayerWindow,ClassicFinalBlit,ClassicMainView,ClassicHookCount };
std::array<Hook,ClassicHookCount> classicHooks;
std::atomic<bool> classicInstalled{};
std::atomic<int32_t> ceObservedRenderer{-1};
std::atomic<uint64_t> ceRendererEpoch{1};
std::atomic<uint64_t> classicPairs{},classicDrops{},classicStock{},classicOutputs{},classicSourceMiss{};
enum class ClassicFailure : uint32_t { None,ScopeChanged,WindowCount,PairChanged,
    PairPreparation,CameraState,Consumer,Source,SourceChanged,OutputShape,IncompletePair };
std::atomic<ClassicFailure> classicLastFailure{};
std::atomic<uint32_t> classicPairStage{},classicRasterWidth{},classicRasterHeight{},
    classicOutputWidth{},classicOutputHeight{};
std::atomic<bool> classicCacheBegan{};
uint32_t classicRejectedGeneration{};

int32_t CeObserveRendererMode() noexcept
{
    int32_t mode=-1;
    if (!Read(bindings.base+0x1b7aa84,mode)) return -1;
    int32_t previous=ceObservedRenderer.load(std::memory_order_acquire);
    if (mode!=previous&&ceObservedRenderer.compare_exchange_strong(previous,mode,
        std::memory_order_acq_rel))
    {
        ceRendererEpoch.fetch_add(1,std::memory_order_acq_rel);
        referenceRevision.fetch_add(1,std::memory_order_acq_rel);
        // Original and Anniversary render the same native player camera.
        // Retire old-mode receipts, but retain the player's tracking origin:
        // rebasing it to the current head pose makes a graphics toggle jump
        // yaw/pitch/roll and room translation. Explicit recenter, XR-space and
        // title-generation changes still reseed at the next preparation.
        completedFrame.Publish({});
    }
    return mode;
}

struct ClassicNativeSource
{
    uintptr_t owner{},wrapper{},resolved{},rtv{},resource{},context{};
    ResourceRegistry::Record record;
};
enum class ClassicSourceFailure : uint32_t { None,Owner,Wrapper,WrapperType,
    NativeView,Context,BackendContext,Selector,SelectedType,Views,RtvMismatch,Resource,Descriptor };
std::atomic<ClassicSourceFailure> classicSourceFailure{};
bool ClassicSource(ClassicNativeSource& out) noexcept
{
    ClassicNativeSource value{};
    uintptr_t vtable{},views{},nativeView{},backend{},backendContext{};
    const auto fail=[](ClassicSourceFailure reason) noexcept {
        classicSourceFailure.store(reason,std::memory_order_relaxed); return false;
    };
    // E-CE-C5: kind 0 is the host output, initialized separately at AE410.
    // The 2E3B910 wrapper array is populated only for kinds 1..8; its element
    // zero is not the native owner of the kind-0 RTV cached at 1B85E78.
    if (!Read(bindings.base+0x2e3c090,value.owner)||!value.owner)
        return fail(ClassicSourceFailure::Owner);
    if (!Read(value.owner+0x500,value.wrapper)||!value.wrapper)
        return fail(ClassicSourceFailure::Wrapper);
    if (!Read(value.wrapper,vtable)||vtable!=bindings.base+0x17fb608)
        return fail(ClassicSourceFailure::WrapperType);
    if (!Read(bindings.base+0x1b85e78,nativeView)||!nativeView)
        return fail(ClassicSourceFailure::NativeView);
    if (!Read(bindings.base+0x2ea2d30,value.context)||!value.context)
        return fail(ClassicSourceFailure::Context);
    if (!Read(bindings.base+0x2e3bde0,backend)||!backend||
        !Read(backend+0xce0,backendContext)||backendContext!=value.context)
        return fail(ClassicSourceFailure::BackendContext);
    __try
    {
        // Verified native selector is read-only. It chooses the same variants
        // used by texture virtual+D8. Never assume wrapper+E0 is selected.
        value.resolved=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(
            bindings.surfaceSelector)(value.wrapper);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return fail(ClassicSourceFailure::Selector); }
    if (!value.resolved||!Read(value.resolved,vtable)||vtable!=bindings.base+0x17fb608)
        return fail(ClassicSourceFailure::SelectedType);
    if (!Read(value.resolved+0xe8,views)||!views)
        return fail(ClassicSourceFailure::Views);
    if (!Read(views,value.rtv)||value.rtv!=nativeView)
        return fail(ClassicSourceFailure::RtvMismatch);
    if (!Read(value.resolved+0xe0,value.resource)||!value.resource)
        return fail(ClassicSourceFailure::Resource);
    if (!resources.Read(value.resource,value.resource,value.record))
        return fail(ClassicSourceFailure::Descriptor);
    out=value;
    classicSourceFailure.store(ClassicSourceFailure::None,std::memory_order_relaxed);
    return true;
}

struct ClassicFrameScope
{
    ClassicViewPair pair;
    Reference reference;
    EyeCache::Key key;
    float scale{};
    bool positional{},prepared{},failed{},capture{};
    int eye{};
    uint32_t generation{},windows[2]{},views[2]{},outputs[2]{};
    uint64_t epoch{},revision{};
    int32_t tick{};
    uintptr_t window{},clock{};
    ClassicNativeSource sources[2];
    dlss::CameraSample dlssCamera[2]{};
    halo_ce::Rectangle outputRectangle{};
    ClassicFailure failure{};
    void Fail(ClassicFailure reason) noexcept
    { failed=true; if (failure==ClassicFailure::None) failure=reason; }
};
thread_local ClassicFrameScope* classicFrameScope{};
struct ClassicPrimaryViewScope
{
    ClassicFrameScope* frame{};
    int eye{};
};
thread_local const ClassicPrimaryViewScope* classicPrimaryViewScope{};
void ClassicCaptureDlssDepth(uintptr_t caller) noexcept;

bool ClassicGetRenderContext(RenderContext& out) noexcept
{
    const auto* scope=classicFrameScope;
    if (!scope||!scope->prepared||scope->failed||scope->eye<0||scope->eye>1||
        !Current()||CeObserveRendererMode()!=0||scope->epoch!=ceRendererEpoch.load()||
        scope->revision!=referenceRevision.load()) return false;
    out={scope->pair.tracking,scope->reference,scope->pair.source.render,
        scope->scale,scope->positional,scope->revision,scope->epoch};
    return true;
}

bool ClassicScopeCurrent(const ClassicFrameScope& scope) noexcept
{
    uintptr_t clock{};
    int32_t tick{};
    return Current()&&classicInstalled.load(std::memory_order_acquire)&&
        armed.load(std::memory_order_acquire)&&trackingEnabled.load(std::memory_order_acquire)&&
        CeObserveRendererMode()==0&&scope.generation==generation.load()&&
        scope.epoch==ceRendererEpoch.load()&&scope.revision==referenceRevision.load()&&
        Read(bindings.base+0x2e9fd68,clock)&&clock==scope.clock&&
        Read(clock+0xc,tick)&&tick==scope.tick;
}

void ClassicCaptureDlssDepth(uintptr_t caller) noexcept
{
    const auto* primary=classicPrimaryViewScope;
    auto* scope=classicFrameScope;
    if(!dlssDepthRequested.load(std::memory_order_relaxed)||caller!=bindings.base+0xb3210b||
        !primary||!scope||primary->frame!=scope||primary->eye!=scope->eye||!scope->capture||
        !scope->prepared||scope->failed||!ClassicScopeCurrent(*scope)||scope->eye<0||scope->eye>1||
        !scope->dlssCamera[scope->eye].valid||!hudTargetBindingsVerified.load(std::memory_order_acquire)) return;
    uintptr_t config{},root{},context{};
    CeHudTargetSnapshot target{};
    ResourceRegistry::Record resource{};
    if(!Read(bindings.base+0x2e3bdd8,config)||!config||!Read(config+0x318,root)||!root||
        !Read(bindings.base+0x2ea2d30,context)||!context||
        !HaloCEHudTarget_Read(bindings.base,reinterpret_cast<ID3D11DeviceContext*>(context),target)||
        target.wrappers[4]!=root||!target.dsv||target.descriptor[0x44]||
        !resources.Read(target.resources[4],target.resources[4],resource)) return;
    const auto& camera=scope->pair.eyes[scope->eye].raster;
    const auto& descriptor=resource.descriptor;
    if(descriptor.Width!=UINT(camera.viewport.right-camera.viewport.left)||
        descriptor.Height!=UINT(camera.viewport.bottom-camera.viewport.top)) return;
    wantedDlssDepth.Publish({descriptor,scope->generation,context});
    (void)cache.CaptureDepth(scope->key,scope->eye,reinterpret_cast<ID3D11DeviceContext*>(context),
        reinterpret_cast<ID3D11Resource*>(resource.resource),descriptor,
        scope->dlssCamera[scope->eye],scope->revision);
}

// BBCF30 receives the actual culling and drawing cameras, after fog clipping
// and optional reflection construction. Verify the normal class-1 call owns
// both cameras in our live window; a copied or stale camera cannot count as an
// eye just because a final blit eventually occurs.
void ClassicViewBody(int16_t player,const Camera* render,const void* renderFrustum,
    const Camera* raster,const void* rasterFrustum,int16_t kind,uint8_t reflected,
    uintptr_t caller)
{
    auto* scope=classicFrameScope;
    bool primary=false;
    if (scope&&scope->prepared&&!scope->failed&&caller==bindings.base+0xbbccb2)
    {
        Camera consumedRender{},consumedRaster{},expectedRender=scope->pair.eyes[scope->eye].render;
        const auto& expectedRaster=scope->pair.eyes[scope->eye].raster;
        const bool readable=Read(reinterpret_cast<uintptr_t>(render),consumedRender)&&
            Read(reinterpret_cast<uintptr_t>(raster),consumedRaster);
        // Native fog may lower render far-plane (E-CE-1). It does not change
        // tracked origin, basis, FOV, raster or the raster camera's clip range.
        expectedRender.farPlane=consumedRender.farPlane;
        if (++scope->views[scope->eye]!=1||!ClassicScopeCurrent(*scope)||
            !readable||player!=scope->pair.source.player||kind!=1||
            reinterpret_cast<uintptr_t>(render)!=scope->window+offsetof(Window,render)||
            reinterpret_cast<uintptr_t>(raster)!=scope->window+offsetof(Window,raster)||
            !renderFrustum||!rasterFrustum||renderFrustum==rasterFrustum||
            !Valid(consumedRender)||!Valid(consumedRaster)||
            std::memcmp(&consumedRender,&expectedRender,sizeof(Camera))||
            std::memcmp(&consumedRaster,&expectedRaster,sizeof(Camera)))
            scope->Fail(ClassicFailure::Consumer);
        else {
            primary=true;
            if(dlssDepthRequested.load(std::memory_order_relaxed)) {
                uint8_t projectionValid{};float projection[16]{};
                const auto frustum=reinterpret_cast<uintptr_t>(rasterFrustum);
                if(Read(frustum+0x140,projectionValid)&&projectionValid&&Read(frustum+0x144,projection))
                    scope->dlssCamera[scope->eye]=halo_ce::DlssClassicCamera(consumedRaster,projection);
            }
        }
    }
    // The prepared frame also spans reflections and final output. Projection
    // changes require the narrower interval during which BBCF30 is actually
    // consuming the verified primary eye. Mask it for every auxiliary view,
    // including a nested reflection, and restore it even on native unwind.
    // `reflected` is not the view kind: BBCC82 sets it to 1 AFTER a reflection
    // was rendered, then passes it to the normal class-1 call at BBCCAD.
    const ClassicPrimaryViewScope primaryScope{scope,scope?scope->eye:0};
    const auto* previous=classicPrimaryViewScope;
    classicPrimaryViewScope=primary?&primaryScope:nullptr;
    __try
    {
        reinterpret_cast<ClassicViewFn>(classicHooks[ClassicMainView].original)(
            player,render,renderFrustum,raster,rasterFrustum,kind,reflected);
    }
    __finally { classicPrimaryViewScope=previous; }
}
void __fastcall ClassicViewHook(int16_t player,const Camera* render,const void* renderFrustum,
    const Camera* raster,const void* rasterFrustum,int16_t kind,uint8_t reflected)
{
    Callback callback;
    ClassicViewBody(player,render,renderFrustum,raster,rasterFrustum,kind,reflected,
        reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

bool ClassicStockFrustum(const Camera& source,std::array<float,99>& frustum) noexcept
{
    if (!Valid(source)) return false;
    float bounds[4]{};
    __try
    {
        reinterpret_cast<void(__fastcall*)(const Camera*,float*)>(bindings.base+
            contract::classic::classic_frustum_bounds)(&source,bounds);
        for (float value:bounds) if (!std::isfinite(value)) return false;
        reinterpret_cast<void(__fastcall*)(const Camera*,const float*,float*,uint8_t)>(bindings.base+
            contract::classic::classic_frustum_build)(&source,bounds,frustum.data(),1);
        for (float value:frustum) if (!std::isfinite(value)) return false;
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void ClassicWindowBody(Window* window,uintptr_t caller)
{
    const auto original=reinterpret_cast<ClassicWindowFn>(classicHooks[ClassicPlayerWindow].original);
    auto* scope=classicFrameScope;
    Window source{};
    const bool eligible=Current()&&classicInstalled.load()&&CeObserveRendererMode()==0&&
        caller==bindings.base+0xbbceeb&&Read(reinterpret_cast<uintptr_t>(window),source)&&
        ValidPrimary(source)&&LiveGame();
    if (eligible)
    {
        const uint64_t now=GetTickCount64(),last=lastCameraMs.exchange(now);
        if (!last||now<last||now-last>=500) firstCameraMs.store(now);
        uint64_t zero=0; firstCameraMs.compare_exchange_strong(zero,now);
    }
    if (!scope||!eligible||scope->failed||!ClassicScopeCurrent(*scope))
    {
        if (scope) scope->Fail(ClassicFailure::ScopeChanged);
        original(window);
        return;
    }
    if (++scope->windows[scope->eye]!=1)
    {
        scope->Fail(ClassicFailure::WindowCount);
        original(window);
        return;
    }
    if (scope->eye==0)
    {
        scope->window=reinterpret_cast<uintptr_t>(window);
        FollowRoomscale(source.render,scope->pair.tracking,scope->reference,
            scope->scale,scope->positional,scope->revision);
        ApplyPhysicalCrouchReference(scope->pair.tracking,scope->reference,scope->scale,scope->positional);
        publishedReference.Publish({scope->reference,scope->revision});
        const auto stage=StageClassicViewPair(source,scope->pair.tracking,scope->reference,
            scope->epoch,scope->scale,scope->positional,scope->pair);
        classicPairStage.store(static_cast<uint32_t>(stage),std::memory_order_relaxed);
        classicRasterWidth.store(source.raster.viewport.right,std::memory_order_relaxed);
        classicRasterHeight.store(source.raster.viewport.bottom,std::memory_order_relaxed);
        scope->prepared=stage==ClassicPairResult::Ready;
        if (scope->prepared)
            PublishGameplayContext({scope->pair.tracking,scope->reference,scope->pair.source.render,
                scope->scale,scope->positional,scope->revision,scope->epoch});
        if (scope->prepared) scope->capture=cache.Begin(scope->pair,scope->key);
        classicCacheBegan.store(scope->capture,std::memory_order_relaxed);
    }
    else if (!scope->prepared||scope->window!=reinterpret_cast<uintptr_t>(window)||
        !ClassicPairCurrent(scope->pair,source,scope->generation,
            scope->pair.tracking.spaceEpoch,scope->pair.tracking.serial,scope->epoch))
        scope->Fail(ClassicFailure::PairChanged);
    if (!scope->prepared||!scope->capture||scope->failed)
    {
        scope->Fail(ClassicFailure::PairPreparation);
        original(window);
        return;
    }
    std::array<uint8_t,sizeof(Camera)+0x18c> publishedCameraAndFrustum{};
    if (!Read(bindings.base+0x29af2c4,publishedCameraAndFrustum))
    {
        scope->Fail(ClassicFailure::CameraState);
        original(window);
        return;
    }

    // The two Camera values are plain 0x54-byte native values. Original
    // frustum construction consumes these before visibility/drawing. Restore
    // the source even if the native render raises a structured exception.
    __try
    {
        window->render=scope->pair.eyes[scope->eye].render;
        window->raster=scope->pair.eyes[scope->eye].raster;
        original(window);
    }
    __finally
    {
        window->render=source.render;
        window->raster=source.raster;
        // BBCF30 publishes a second copy for non-window consumers. Restore
        // the complete previous Camera/frustum bytes together. Rebuilding a
        // guessed stock frustum here can lose native fog/reflection state.
        std::memcpy(reinterpret_cast<void*>(bindings.base+0x29af2c4),
            publishedCameraAndFrustum.data(),publishedCameraAndFrustum.size());
    }
}
void __fastcall ClassicWindowHook(Window* window)
{
    Callback callback;
    ClassicWindowBody(window,reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

void ClassicBlitBody(const halo_ce::Rectangle* rectangle,uintptr_t caller)
{
    reinterpret_cast<ClassicBlitFn>(classicHooks[ClassicFinalBlit].original)(rectangle);
    auto* scope=classicFrameScope;
    if (caller!=bindings.base+0xae0ecb||!Current()||!classicInstalled.load()||
        CeObserveRendererMode()!=0) return;
    classicOutputs.fetch_add(1,std::memory_order_relaxed);
    ClassicNativeSource source{};
    if (!ClassicSource(source))
    {
        classicSourceMiss.fetch_add(1,std::memory_order_relaxed);
        if (scope) scope->Fail(ClassicFailure::Source);
        return;
    }
    wanted.Publish({source.record.descriptor,generation.load(),source.context});
    classicOutputWidth.store(source.record.descriptor.Width,std::memory_order_relaxed);
    classicOutputHeight.store(source.record.descriptor.Height,std::memory_order_relaxed);
    if (!scope||!scope->prepared||!scope->capture||scope->failed||
        scope->windows[scope->eye]!=1||scope->views[scope->eye]!=1||!ClassicScopeCurrent(*scope)) return;
    halo_ce::Rectangle rect{};
    const auto& descriptor=source.record.descriptor;
    if (!Read(reinterpret_cast<uintptr_t>(rectangle),rect)||!Valid(rect)||rect.top||rect.left||
        (scope->eye==1&&!Same(rect,scope->outputRectangle)))
    { scope->Fail(ClassicFailure::OutputShape); return; }
    if (scope->eye==0) scope->outputRectangle=rect;
    if (scope->eye==1)
    {
        const auto& first=scope->sources[0];
        if (source.owner!=first.owner||source.wrapper!=first.wrapper||source.resolved!=first.resolved||
            source.rtv!=first.rtv||source.resource!=first.resource||source.context!=first.context||
            source.record.revision!=first.record.revision)
        { scope->Fail(ClassicFailure::SourceChanged); return; }
    }
    scope->sources[scope->eye]=source;
    if (++scope->outputs[scope->eye]!=1||!ClassicFullRaster(scope->pair.source)||
        !cache.Capture(scope->key,scope->eye,reinterpret_cast<ID3D11DeviceContext*>(source.context),
            reinterpret_cast<ID3D11Resource*>(source.resource),descriptor))
        scope->Fail(ClassicFailure::OutputShape);
}
void __fastcall ClassicBlitHook(const halo_ce::Rectangle* rectangle)
{
    Callback callback;
    ClassicBlitBody(rectangle,reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

void ClassicGameRenderBody(float delta,float interpolation)
{
    const auto original=reinterpret_cast<ClassicGameRenderFn>(classicHooks[ClassicGameRender].original);
    Tracking tracking{};
    uintptr_t players{},clock{};
    int16_t playerCount{};
    int32_t tick{};
    if (classicFrameScope||!Current()||!classicInstalled.load()||CeObserveRendererMode()!=0||
        !armed.load()||!TrackingNow(tracking)||!LiveGame()||
        !Read(bindings.base+0x2ea2d90,players)||!Read(players+0xb4,playerCount)||playerCount!=1||
        !Read(bindings.base+0x2e9fd68,clock)||!Read(clock+0xc,tick))
    {
        original(delta,interpolation);
        classicStock.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    if (preparationBusy.test_and_set(std::memory_order_acquire))
    {
        original(delta,interpolation);
        classicStock.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    ClassicFrameScope scope{};
    // A dropped attempt retains the previous coherent pair until its existing
    // freshness/lifetime checks expire. The cache never publishes a partial eye.
    scope.generation=generation.load(); scope.epoch=ceRendererEpoch.load(); scope.tick=tick; scope.clock=clock;
    scope.revision=referenceRevision.load();
    if (recenter.exchange(false)||reference.generation!=scope.generation||
        reference.spaceEpoch!=tracking.spaceEpoch)
        reference={tracking.headPosition,tracking.headOrientation,tracking.spaceEpoch,scope.generation};
    scope.reference=reference;
    if (scope.revision==referenceRevision.load())
        publishedReference.Publish({reference,scope.revision});
    scope.scale=Game_GetWorldScale(); scope.positional=Game_IsPositionalTracking();
    scope.pair.tracking=tracking;
    classicFrameScope=&scope;
    bool returned=false;
    __try
    {
        // AC47C0 is the native render-only path. Each invocation rebuilds its
        // cameras, visibility and output; the game update AB0D10 is not called.
        // Anniversary's full-frame renderer/worker completion is not replayed.
        original(delta,interpolation);
        if (!scope.failed&&scope.prepared&&scope.capture&&scope.outputs[0]==1&&
            ClassicScopeCurrent(scope))
        {
            scope.eye=1;
            original(delta,interpolation);
        }
        returned=true;
    }
    __finally
    {
        if (returned&&!scope.failed&&scope.prepared&&scope.capture&&
            scope.outputs[0]==1&&scope.outputs[1]==1&&ClassicScopeCurrent(scope)&&
            cache.Finish(scope.key))
        {
            completedFrame.Publish({scope.key,scope.revision,GetTickCount64()});
            lastOwnedMs.store(GetTickCount64());
            captured.fetch_add(1,std::memory_order_relaxed);
            built.fetch_add(1,std::memory_order_relaxed);
            classicPairs.fetch_add(1,std::memory_order_relaxed);
            classicLastFailure.store(ClassicFailure::None,std::memory_order_relaxed);
        }
        else
        {
            cache.Drop(scope.key);
            dropped.fetch_add(1,std::memory_order_relaxed);
            classicDrops.fetch_add(1,std::memory_order_relaxed);
            classicLastFailure.store(scope.failure==ClassicFailure::None?
                ClassicFailure::IncompletePair:scope.failure,std::memory_order_relaxed);
        }
        classicFrameScope=nullptr;
        preparationBusy.clear(std::memory_order_release);
    }
}
void __fastcall ClassicGameRenderHook(float delta,float interpolation)
{
    Callback callback;
    ClassicGameRenderBody(delta,interpolation);
}

bool Classic_Remove() noexcept
{
    classicInstalled.store(false,std::memory_order_release);
    for (auto& hook:classicHooks) if (hook.enabled)
    {
        const auto status=MCCVR_DisableHookForRetirement(hook.target);
        if (status!=MH_OK&&status!=MH_ERROR_DISABLED) return false;
        hook.enabled=false;
    }
    if (callbacks.load(std::memory_order_acquire)) return false;
    const void* detours[ClassicHookCount]={reinterpret_cast<void*>(&ClassicGameRenderHook),
        reinterpret_cast<void*>(&ClassicWindowHook),reinterpret_cast<void*>(&ClassicBlitHook),
        reinterpret_cast<void*>(&ClassicViewHook)};
    const void* originals[ClassicHookCount]{};
    bool any=false;
    for (size_t i=0;i<ClassicHookCount;++i)
    { originals[i]=classicHooks[i].original; any|=classicHooks[i].target!=nullptr; }
    if (any&&!WaitForNativeDetourQuiescence(detours,originals,ClassicHookCount,callbacks)) return false;
    for (auto& hook:classicHooks) if (hook.target)
    {
        const auto status=MH_RemoveHook(hook.target);
        if (status!=MH_OK&&status!=MH_ERROR_NOT_CREATED) return false;
        hook={};
    }
    ceObservedRenderer.store(-1,std::memory_order_release);
    return true;
}

bool Classic_Install() noexcept
{
    if (classicInstalled.load()) return true;
    const auto gen=generation.load();
    if (!gen||classicRejectedGeneration==gen) return false;
    const char* failure{};
    const NativeContractSet set{contract::classic::entries,contract::classic::witnesses,
        contract::classic::relatives,contract::classic::pointers};
    if (!VerifyNativeFeatureBindings(bindings.base,bindings.size,gen,set,failure))
    {
        classicRejectedGeneration=gen;
        LOG("CE Classic stock fallback: binding verification %s",failure?failure:"failed");
        return false;
    }
    const uintptr_t addresses[ClassicHookCount]={bindings.base+contract::classic::classic_game_render,
        bindings.base+contract::classic::classic_player_window,bindings.base+contract::classic::classic_final_blit,
        bindings.base+contract::classic::classic_view_render};
    void* detours[ClassicHookCount]={reinterpret_cast<void*>(&ClassicGameRenderHook),
        reinterpret_cast<void*>(&ClassicWindowHook),reinterpret_cast<void*>(&ClassicBlitHook),
        reinterpret_cast<void*>(&ClassicViewHook)};
    for (size_t i=0;i<ClassicHookCount;++i)
    {
        auto& hook=classicHooks[i];
        const auto status=MH_CreateHook(reinterpret_cast<void*>(addresses[i]),detours[i],&hook.original);
        if (status!=MH_OK)
        {
            classicRejectedGeneration=gen;
            LOG("CE Classic stock fallback: create hook %zu status %d",i,status);
            Classic_Remove();
            return false;
        }
        hook.target=reinterpret_cast<void*>(addresses[i]);
    }
    for (auto& hook:classicHooks)
    {
        const auto status=MH_EnableHook(hook.target);
        if (status!=MH_OK)
        {
            classicRejectedGeneration=gen;
            LOG("CE Classic stock fallback: enable hook status %d",status);
            Classic_Remove();
            return false;
        }
        hook.enabled=true;
    }
    classicInstalled.store(true,std::memory_order_release);
    LOG("CE Classic render hooks installed: native render-only eyes, paired native cameras and exact final kind-0 output");
    return true;
}
