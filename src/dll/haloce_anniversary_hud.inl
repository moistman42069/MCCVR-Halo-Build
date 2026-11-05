// Native HUD output routing is an independent optional feature. The forced
// two-view list uses bit0, while the native in-frame HUD branch requires bit1.
// E-CE-AHUD-1..4, HALOCE-ANNIVERSARY-HUD-EVIDENCE-2026-09-15.md.
using AnniversaryHudFn=void(__fastcall*)();
Hook anniversaryHudHook;
// 884de13 reached this replay once, failed native cleanup and stopped
// presenting after the graphics switch. Keep the complete adapter dormant;
// its native callback owns more state than our target/raster wrapper can
// restore. See HALOCE-ANNIVERSARY-REPLAY-ROLLBACK-2026-09-15.md.
constexpr bool kCeAnniversaryManualHudReplayEnabled=false;
// 22cb813 counted completed late HUD copies while the headset showed no HUD.
// Retain the adapter, but disable its unprepared raster/target transaction
// before implementing its independently verified replacement.
constexpr bool kCeAnniversaryUnpreparedHudReplayEnabled=false;
// 2cf002b stopped presenting on the first Original -> Anniversary switch.
// Disable this candidate's new native binder transaction separately before
// replacing it; preserve Original reticles and the shared contact refinements.
constexpr bool kCeAnniversaryPreparedHudTargetsEnabled=false;
// The dump exonerated target preparation: native gameplay dereferenced a
// disposed shader in the ordinary fallback first. The corrected lifecycle,
// native-resource guard and partial-bind cleanup enable this separate path.
constexpr bool kCeAnniversaryNativeReadyHudTargetsEnabled=true;
std::atomic<bool> anniversaryHudInstalled{};
std::atomic<bool> anniversaryHudNaturalInstalled{};
std::atomic<uint64_t> anniversaryHudDraws{},anniversaryHudFallbacks{};
std::atomic<uint64_t> anniversaryHudTargetPreparations{},anniversaryHudDepthDetachments{};
std::atomic<uint32_t> anniversaryHudFailure{};
const char* AnniversaryHudFailureName(uint32_t value) noexcept
{
    switch (value)
    {
    case 0:return "none";
    case 1:return "legacy-admission";
    case 2:return "raster-or-target-stack";
    case 3:return "callback-or-restoration";
    case 4:return "restored-native-exception";
    case 5:return "unverified-cleanup";
    case 10:return "native-HUD-frame-bit";
    case 11:return "primary-eye-ownership";
    case 12:return "native-callback-owner";
    case 13:return "native-player-count";
    case 14:return "native-HUD-initialized";
    case 15:return "native-HUD-disabled";
    case 16:return "native-rendering-disabled";
    case 17:return "native-HUD-unavailable";
    case 18:return "native-display-mode";
    case 19:return "native-display-size";
    case 20:return "stock-camera";
    case 21:return "tracking-reference";
    case 22:return "output-source";
    case 30:return "eye-frame-scope";
    case 31:return "eye-core-ownership";
    case 32:return "eye-scene-index";
    case 33:return "eye-camera-identity";
    case 34:return "eye-consumer-stages";
    case 35:return "eye-reference-revision";
    case 36:return "eye-generation";
    case 37:return "eye-tracking-age";
    case 38:return "eye-camera-changed";
    case 39:return "eye-selected-camera";
    case 40:return "eye-ownership-changed";
    case 50:return "natural-HUD-frame";
    case 51:return "natural-HUD-packed-target";
    case 52:return "natural-HUD-gameplay-or-raster";
    case 53:return "natural-HUD-source-changed";
    case 54:return "natural-HUD-target-preparation";
    case 55:return "natural-HUD-target-restoration";
    default:return "unknown";
    }
}
struct AnniversaryHudReplay
{
    RenderContext owner;
    ID3D11DeviceContext* context{};
    UINT width{},height{},eyeWidth{},eyeHeight{};
    bool entered{},returned{},rasterRestored{true};
};
thread_local AnniversaryHudReplay* anniversaryHudReplay{};

// The normal native callback runs once, late in the frame, after the native
// copies have packed the two worlds. Only its gameplay HUD draw is framed
// twice. No callback, preamble, target push/pop or lock is invoked manually.
struct AnniversaryHudRaster
{
    static constexpr UINT capacity=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT views[capacity]{};D3D11_RECT scissors[capacity]{};
    UINT viewCount{},scissorCount{};
};
struct AnniversaryNaturalHud
{
    FrameScope* frame{};
    RenderContext owner;
    CeHudTargetSnapshot target;
    CeHudTargetSnapshot nativeTarget;
    AnniversaryHudRaster nativeRaster;
    std::array<uint8_t,0x48> preparedDescriptor{};
    uint64_t nativeDepthRevision{};
    ID3D11DeviceContext* context{};
    // The frame pass's deferred zoom art (AnniversaryHud_ScreenEffect): 0xB31D88's own parameter block, live while
    // its gameplay HUD call runs.
    const void* screenEffect{};
    bool entered{},complete{},targetPrepared{};
};
thread_local AnniversaryNaturalHud* anniversaryNaturalHud{};

bool AnniversaryHud_NaturalCurrent(const AnniversaryNaturalHud& hud) noexcept
{
    const auto* f=hud.frame;
    return !anniversaryMaterialMasked&&f&&f==frameScope&&f->capture&&f->synthetic&&f->prepared.valid&&
        f->diagnostic.eyeMask==3&&f->packedEyeMask==3&&f->packedResource&&
        f->prepared.referenceRevision==referenceRevision.load(std::memory_order_acquire)&&
        Current()&&Anniversary()&&HaloCE_RenderContextCurrent(hud.owner);
}
bool AnniversaryHud_ReplaceTarget(const CeHudTargetSnapshot& expected,
    const std::array<uint8_t,0x48>& descriptor,const AnniversaryHudRaster& raster,
    CeHudTargetSnapshot& result,bool restoring=false) noexcept
{
    bool replaced=false;
    HaloCEHudLayout_BeginPrivateRaster();
    __try
    {
        replaced=restoring?HaloCEHudTarget_RestorePrepared(expected,descriptor,result):
            HaloCEHudTarget_Replace(expected,descriptor,result);
        auto prepared=expected;prepared.descriptor=descriptor;
        // A failed binder/readback can still own this exact transaction, but
        // a foreign native target must retain its own raster as well as target.
        const bool rasterOwned=replaced||HaloCEHudTarget_CaptureSourceCurrent(expected)||
            HaloCEHudTarget_CaptureSourceCurrent(prepared);
        if (rasterOwned)
        {
            expected.context->RSSetViewports(raster.viewCount,raster.views);
            expected.context->RSSetScissorRects(raster.scissorCount,raster.scissors);
        }
        else HaloCEHudLayout_InvalidateState(expected.context);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { replaced=false; }
    HaloCEHudLayout_EndPrivateRaster();
    return replaced;
}
bool AnniversaryHud_RestoreTarget(AnniversaryNaturalHud& hud) noexcept
{
    if (!hud.targetPrepared) return true;
    if (hud.nativeTarget.dsv)
    {
        ResourceRegistry::Record depth{};const auto identity=hud.nativeTarget.resources[4];
        if (!resources.Read(identity,identity,depth)||depth.revision!=hud.nativeDepthRevision)
        { hud.complete=false;anniversaryHudFailure=55;return false; }
    }
    CeHudTargetSnapshot restored{};
    if (!AnniversaryHud_ReplaceTarget(hud.nativeTarget,hud.preparedDescriptor,hud.nativeRaster,restored,true))
    { hud.complete=false;anniversaryHudFailure=55;return false; }
    hud.target=restored;hud.targetPrepared=false;return true;
}
bool AnniversaryHud_BeginGameplay(ID3D11DeviceContext* context,UINT& width,UINT& height) noexcept
{
    auto* hud=anniversaryNaturalHud;
    if (!hud) return false;
    if (hud->entered) { hud->complete=false;return false; }
    if (!context||!AnniversaryHud_NaturalCurrent(*hud)) return false;
    auto& f=*hud->frame;
    anniversaryHudFailure=51;
    ResourceRegistry::Record resource{};
    if (!hudTargetBindingsVerified.load()||
        reinterpret_cast<uintptr_t>(context)!=f.diagnostic.copyContext[0]||
        f.diagnostic.copyContext[0]!=f.diagnostic.copyContext[1]||
        !resources.Read(f.packedResource,f.packedResource,resource)||resource.revision!=f.packedRevision||
        !HaloCEHudTarget_Read(bindings.base,context,hud->target)||hud->target.count!=1||
        hud->target.resources[0]!=f.packedResource||
        std::memcmp(&resource.descriptor,&f.packedDescriptor,sizeof(resource.descriptor))) return false;
    const auto& d=f.packedDescriptor;
    if (!d.Width||!d.Height||d.Height%2||d.Width!=f.diagnostic.sourceWidth||
        d.Height!=2*f.diagnostic.sourceHeight) return false;
    hud->context=context;
    hud->nativeTarget=hud->target;
    auto descriptor=hud->target.descriptor;
    bool detachDepth=false;
    anniversaryHudFailure=54;
    auto& raster=hud->nativeRaster;
    if (!HaloCEHudLayout_CopyState(context,&raster.viewCount,raster.views,&raster.scissorCount,raster.scissors)) return false;
    if (hud->target.dsv)
    {
        ResourceRegistry::Record depth{};
        const auto identity=hud->target.resources[4];
        if (!identity||!resources.Read(identity,identity,depth)) return false;
        const auto& z=depth.descriptor;
        hud->nativeDepthRevision=depth.revision;
        detachDepth=z.Width!=d.Width||z.Height!=d.Height||z.ArraySize!=d.ArraySize||
            z.SampleDesc.Count!=d.SampleDesc.Count||z.SampleDesc.Quality!=d.SampleDesc.Quality;
        if (detachDepth) std::memset(descriptor.data()+0x30,0,8);
    }
    // The native cache describes intended attachments, not whether D3D
    // accepted the last bind. A packed color can be twice the native depth's
    // height. Bind a coherent native descriptor before actual HUD drawing;
    // bitmap/text rendering disables depth, so only incompatible depth detaches.
    hud->preparedDescriptor=descriptor;
    hud->targetPrepared=true; // Record possible mutation BEFORE the native call.
    if (!AnniversaryHud_ReplaceTarget(hud->nativeTarget,descriptor,hud->nativeRaster,hud->target))
    { (void)AnniversaryHud_RestoreTarget(*hud);return false; }
    hud->entered=true;
    anniversaryHudTargetPreparations.fetch_add(1,std::memory_order_relaxed);
    if (detachDepth) anniversaryHudDepthDetachments.fetch_add(1,std::memory_order_relaxed);
    width=d.Width;height=d.Height;
    anniversaryHudFailure=52;
    return true;
}
void AnniversaryHud_EndGameplay(bool complete) noexcept
{
    auto* hud=anniversaryNaturalHud;
    if (!hud||!hud->entered) return;
    CeHudTargetSnapshot after{};
    hud->complete=complete&&AnniversaryHud_NaturalCurrent(*hud)&&
        HaloCEHudTarget_Read(bindings.base,hud->context,after)&&
        after.backend==hud->target.backend&&after.descriptor==hud->target.descriptor&&
        after.count==hud->target.count&&after.resources[0]==hud->target.resources[0]&&
        after.rtvs[0]==hud->target.rtvs[0]&&after.dsv==hud->target.dsv;
    // A native binder can publish its descriptor before its view cache.
    // One bounded repair attempt finishes an owned partial restoration before
    // the callback performs its ordinary ClearState cleanup.
    if (!AnniversaryHud_RestoreTarget(*hud)) (void)AnniversaryHud_RestoreTarget(*hud);
}
void AnniversaryHud_NaturalBody(AnniversaryHudFn original,uintptr_t caller)
{
    auto* f=frameScope;
    if (!anniversaryHudNaturalInstalled.load()||anniversaryNaturalHud||anniversaryMaterialMasked||
        caller!=bindings.base+0x4572f5||!f||
        !f->capture||!(f->renderFlags&0x10)||f->diagnostic.eyeMask!=3)
    { original();return; }
    AnniversaryNaturalHud local{};local.frame=f;
    ReferenceSample sample{};
    uintptr_t players{};int16_t playerCount{};
    if (!Read(bindings.base+0x2ea2d90,players)||!Read(players+0xb4,playerCount)||playerCount!=1||
        !Read(bindings.base+0x2d9cb34,local.owner.camera)||!Valid(local.owner.camera)||
        !publishedReference.Read(sample)||sample.revision!=f->prepared.referenceRevision)
    { anniversaryHudFailure=50;anniversaryHudFallbacks.fetch_add(1);original();return; }
    local.owner.tracking=f->prepared.receipt.tracking;
    local.owner.reference=sample.value;local.owner.referenceRevision=sample.revision;
    local.owner.rendererEpoch=ceRendererEpoch.load();
    local.owner.unitsPerMeter=Game_GetWorldScale();local.owner.positional=Game_IsPositionalTracking();
    if (!AnniversaryHud_NaturalCurrent(local))
    { anniversaryHudFailure=50;anniversaryHudFallbacks.fetch_add(1);original();return; }
    anniversaryHudFailure=52;
    anniversaryNaturalHud=&local;
    bool returned=false;
    __try { original();returned=true; }
    __finally
    {
        (void)AnniversaryHud_RestoreTarget(local);
        anniversaryNaturalHud=nullptr;
        ResourceRegistry::Record after{};
        const bool committed=returned&&local.complete&&AnniversaryHud_NaturalCurrent(local)&&
            HaloCEHudTarget_CaptureSourceCurrent(local.target)&&
            resources.Read(f->packedResource,f->packedResource,after)&&after.revision==f->packedRevision&&
            !std::memcmp(&after.descriptor,&f->packedDescriptor,sizeof(after.descriptor))&&
            cache.CapturePacked(f->key,local.context,reinterpret_cast<ID3D11Resource*>(f->packedResource),after.descriptor);
        if (committed) { anniversaryHudFailure=0;anniversaryHudDraws.fetch_add(2,std::memory_order_relaxed); }
        else { if (local.complete) anniversaryHudFailure=53;anniversaryHudFallbacks.fetch_add(1,std::memory_order_relaxed); }
    }
}

void AnniversaryHudCallbackBody(uintptr_t caller=0)
{
    const auto original=reinterpret_cast<AnniversaryHudFn>(anniversaryHudHook.original);
    auto* replay=anniversaryHudReplay;
    if (!replay) { AnniversaryHud_NaturalBody(original,caller); return; }
    if (replay->entered||!HaloCE_RenderContextCurrent(replay->owner)) return;
    if (!HaloCEHudLayout_BeginEyeReplay(replay->context,replay->width,replay->height,
        replay->eyeWidth,replay->eyeHeight,&replay->rasterRestored)) return;
    replay->entered=true;
    __try { original(); replay->returned=true; }
    __finally { replay->rasterRestored=HaloCEHudLayout_EndEyeReplay(); }
}
void __fastcall AnniversaryHudCallbackHook()
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { AnniversaryHudCallbackBody(reinterpret_cast<uintptr_t>(_ReturnAddress())); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}

// AIM FRAME ZOOM, ANNIVERSARY: THE ZOOM ART. A weapon's zoom art (its scope mask and blur) is the screen effect
// 0xAC7EF0, which 0xB31D88 draws just before the gameplay HUD. Classic draws it in the frame's own pass. Here the late
// callback runs once over the packed eyes, two eyes high, while the effect frames the native rectangle (one eye, at the
// top) and scales each sampled picture by that rectangle, not by the texture: over the packed target it would sample
// both eyes as one. So a frame pass's call waits for the gameplay HUD's prepared target (AnniversaryHud_ScreenEffect)
// and runs once per eye, each run sampling that eye's own picture through the kind-1 entry of the native view table.
// It composites into the top eye; eye 1's run borrows the top and hands it back. Every other call is the game's own.
using ScreenEffectFn=void(__fastcall*)(const void*);
Hook screenEffectHook;
std::atomic<bool> screenEffectInstalled{},screenEffectFaulted{};
std::atomic<uint64_t> screenEffectRuns{},screenEffectRefusals{};
std::atomic<uint32_t> screenEffectFailure{};
std::atomic<int32_t> screenEffectRefusedRect[4]{};
std::atomic<uint32_t> screenEffectRefusedEye[2]{};
const char* ScreenEffectFailureName(uint32_t value) noexcept
{
    switch (value)
    {
    case 0:return "none";
    case 1:return "scratch-pending";
    case 2:return "native-bound-kind";
    case 3:return "native-rectangle";
    case 4:return "native-scene-view";
    case 5:return "native-secondary-size";
    case 6:return "native-view-table-changed";
    case 7:return "native-target-or-stack-changed";
    case 8:return "native-fault";
    default:return "unknown";
    }
}
// One eye in the packed target's format; the views match the native kind-1 view.
struct ScreenEffectShape
{
    UINT width{},height{};
    DXGI_FORMAT format{},viewFormat{};
    uint32_t generation{};
    bool operator==(const ScreenEffectShape&) const=default;
};
Snapshot<ScreenEffectShape> screenEffectWanted;
struct ScreenEffectScratch
{
    ID3D11Texture2D* eyes[2]{};
    ID3D11ShaderResourceView* views[2]{};
    ScreenEffectShape shape,refused;
};
// Allocated at present time; the render path and present share it under the busy flag, never both at once.
ScreenEffectScratch screenEffectScratch;
std::atomic<bool> screenEffectScratchBusy{};
void ScreenEffect_ReleaseScratch(ScreenEffectScratch& scratch) noexcept
{
    for (auto*& view:scratch.views) { if (view) view->Release(); view=nullptr; }
    for (auto*& eye:scratch.eyes) { if (eye) eye->Release(); eye=nullptr; }
    scratch.shape={};
}
void AnniversaryHud_PresentResources(ID3D11Device* device) noexcept
{
    ScreenEffectShape want{};
    if (!device||!screenEffectWanted.Read(want)||!want.width||want.generation!=generation.load()) return;
    if (screenEffectScratchBusy.exchange(true,std::memory_order_acq_rel)) return;
    auto& scratch=screenEffectScratch;
    if (!(scratch.shape==want)&&!(scratch.refused==want))
    {
        ScreenEffect_ReleaseScratch(scratch);
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width=want.width;texture.Height=want.height;texture.MipLevels=1;texture.ArraySize=1;
        texture.Format=want.format;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
        texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format=want.viewFormat;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
        bool made=true;
        for (int eye=0;eye<2&&made;++eye)
            made=SUCCEEDED(device->CreateTexture2D(&texture,nullptr,&scratch.eyes[eye]))&&
                SUCCEEDED(device->CreateShaderResourceView(scratch.eyes[eye],&view,&scratch.views[eye]));
        if (made) scratch.shape=want;
        else { ScreenEffect_ReleaseScratch(scratch);scratch.refused=want; }
        LOG("CE Anniversary zoom art: per-eye pictures %s (%ux%u format=%u view=%u)",
            made?"prepared":"unavailable",want.width,want.height,want.format,want.viewFormat);
    }
    screenEffectScratchBusy.store(false,std::memory_order_release);
}
bool ScreenEffect_SceneView(ID3D11ShaderResourceView* view,ID3D11Resource* expected,
    D3D11_SHADER_RESOURCE_VIEW_DESC& desc) noexcept
{
    __try
    {
        ID3D11Resource* resource{};
        view->GetResource(&resource);
        if (resource) resource->Release();
        if (resource!=expected) return false;
        view->GetDesc(&desc);
        return desc.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&desc.Texture2D.MostDetailedMip==0;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ScreenEffect_TargetSize(ID3D11RenderTargetView* view,UINT width,UINT height) noexcept
{
    __try
    {
        ID3D11Resource* resource{};
        view->GetResource(&resource);
        if (!resource) return false;
        D3D11_RESOURCE_DIMENSION dimension{};
        D3D11_TEXTURE2D_DESC desc{};
        resource->GetType(&dimension);
        if (dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D) static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
        resource->Release();
        return dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D&&desc.Width==width&&desc.Height==height;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// One native run with `eyeView` as the kind-1 picture; the table entry is the native view again afterwards. The table
// owns a reference to its entry, so the lent view carries one: a native refresh during the run releases that one.
bool ScreenEffect_Run(const void* parameters,ID3D11ShaderResourceView** slot,ID3D11ShaderResourceView* nativeView,
    ID3D11ShaderResourceView* eyeView,bool& tableIntact) noexcept
{
    const auto original=reinterpret_cast<ScreenEffectFn>(screenEffectHook.original);
    bool returned=false;
    __try
    {
        eyeView->AddRef();
        __try { *slot=eyeView;original(parameters);returned=true; }
        __finally
        {
            tableIntact=*slot==eyeView;
            if (tableIntact) { *slot=nativeView;eyeView->Release(); }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { returned=false; }
    return returned;
}
void ScreenEffect_Refuse(uint32_t failure) noexcept
{
    screenEffectFailure.store(failure,std::memory_order_relaxed);
    screenEffectRefusals.fetch_add(1,std::memory_order_relaxed);
}
// The gameplay HUD's prepared target is bound (AnniversaryHud_BeginGameplay). False only when the effect did not leave
// that target and the native target stack as it found them: the HUD must not draw into the packed eyes then.
bool AnniversaryHud_ScreenEffect(ID3D11DeviceContext* context) noexcept
{
    auto* hud=anniversaryNaturalHud;
    if (!hud||!hud->entered||!hud->screenEffect) return true;
    const void* parameters=hud->screenEffect;
    hud->screenEffect=nullptr;
    if (!screenEffectInstalled.load(std::memory_order_acquire)||screenEffectFaulted.load()||
        context!=hud->context||!AnniversaryHud_NaturalCurrent(*hud)) return true;
    const auto& packedDescriptor=hud->frame->packedDescriptor;
    auto* packed=reinterpret_cast<ID3D11Resource*>(hud->frame->packedResource);
    const UINT width=packedDescriptor.Width,height=packedDescriptor.Height/2;
    namespace effect=contract::anniversary_screen_effect;
    // The view table holds each target kind's render target view, then its shader resource view, 0x30 apart.
    const uintptr_t views=bindings.base+effect::native_target_shader_views;
    auto** sceneSlot=reinterpret_cast<ID3D11ShaderResourceView**>(views+0x30);
    int16_t kind{},rect[4]{};
    ID3D11ShaderResourceView* sceneView{};
    ID3D11RenderTargetView* secondary{};
    D3D11_SHADER_RESOURCE_VIEW_DESC sceneDesc{};
    uintptr_t backend{};
    int32_t stackBefore{},stackAfter{};
    if (!Read(bindings.base+effect::native_bound_target_kind,kind)||kind!=1)
    { ScreenEffect_Refuse(2);return true; }
    // rectangle2d: top, left, bottom, right
    if (!Read(bindings.base+effect::native_screen_rectangle,rect)||rect[0]||rect[1]||
        rect[2]!=static_cast<int32_t>(height)||rect[3]!=static_cast<int32_t>(width))
    {
        for (int i=0;i<4;++i) screenEffectRefusedRect[i].store(rect[i],std::memory_order_relaxed);
        screenEffectRefusedEye[0].store(width,std::memory_order_relaxed);
        screenEffectRefusedEye[1].store(height,std::memory_order_relaxed);
        ScreenEffect_Refuse(3);return true;
    }
    if (packedDescriptor.SampleDesc.Count!=1||!Read(reinterpret_cast<uintptr_t>(sceneSlot),sceneView)||!sceneView||
        !ScreenEffect_SceneView(sceneView,packed,sceneDesc))
    { ScreenEffect_Refuse(4);return true; }
    // The effect binds its secondary target with the one-eye native depth and fills it whole.
    if (!Read(views+0x58,secondary)||!secondary||!ScreenEffect_TargetSize(secondary,width,height))
    { ScreenEffect_Refuse(5);return true; }
    if (!Read(bindings.base+0x2e3bde0,backend)||!backend||!Read(backend+0x10,stackBefore))
    { ScreenEffect_Refuse(7);return true; }
    const ScreenEffectShape shape{width,height,packedDescriptor.Format,sceneDesc.Format,generation.load()};
    if (screenEffectScratchBusy.exchange(true,std::memory_order_acq_rel)) { ScreenEffect_Refuse(1);return true; }
    auto& scratch=screenEffectScratch;
    if (!(scratch.shape==shape))
    {
        screenEffectScratchBusy.store(false,std::memory_order_release);
        screenEffectWanted.Publish(shape);
        ScreenEffect_Refuse(1);return true;
    }
    // The zoom art belongs to the picture, not the HUD: a hidden HUD still shows it, as Classic does.
    const unsigned hidden=hud_visibility::depth;
    hud_visibility::depth=0;
    HaloCEHudLayout_BeginPrivateRaster();
    const D3D11_BOX top{0,0,0,width,height,1},bottom{0,height,0,width,2*height,1};
    context->CopySubresourceRegion(scratch.eyes[0],0,0,0,0,packed,0,&top);
    context->CopySubresourceRegion(scratch.eyes[1],0,0,0,0,packed,0,&bottom);
    context->CopySubresourceRegion(packed,0,0,0,0,scratch.eyes[1],0,nullptr);
    bool intact=true;
    bool ran=ScreenEffect_Run(parameters,sceneSlot,sceneView,scratch.views[1],intact);
    if (ran&&intact)
    {
        context->CopySubresourceRegion(scratch.eyes[1],0,0,0,0,packed,0,&top);
        context->CopySubresourceRegion(packed,0,0,height,0,scratch.eyes[1],0,nullptr);
        context->CopySubresourceRegion(packed,0,0,0,0,scratch.eyes[0],0,nullptr);
        ran=ScreenEffect_Run(parameters,sceneSlot,sceneView,scratch.views[0],intact);
    }
    if (!ran||!intact) context->CopySubresourceRegion(packed,0,0,0,0,scratch.eyes[0],0,nullptr);
    // The effect's own viewports were never the HUD's: the observed native raster is the bound one again.
    context->RSSetViewports(hud->nativeRaster.viewCount,hud->nativeRaster.views);
    context->RSSetScissorRects(hud->nativeRaster.scissorCount,hud->nativeRaster.scissors);
    HaloCEHudLayout_EndPrivateRaster();
    hud_visibility::depth=hidden;
    screenEffectScratchBusy.store(false,std::memory_order_release);
    CeHudTargetSnapshot after{};
    const bool kept=HaloCEHudTarget_Read(bindings.base,context,after)&&
        after.backend==hud->target.backend&&after.descriptor==hud->target.descriptor&&
        after.count==hud->target.count&&after.resources[0]==hud->target.resources[0]&&
        after.rtvs[0]==hud->target.rtvs[0]&&after.dsv==hud->target.dsv&&
        Read(backend+0x10,stackAfter)&&stackAfter==stackBefore;
    if (!ran) { screenEffectFaulted=true;ScreenEffect_Refuse(8); }
    else if (!intact) { screenEffectFaulted=true;ScreenEffect_Refuse(6); }
    else if (kept) { screenEffectFailure.store(0,std::memory_order_relaxed);screenEffectRuns.fetch_add(1,std::memory_order_relaxed); }
    if (!kept) { ScreenEffect_Refuse(7);hud->complete=false; }
    return kept;
}
void ScreenEffectBody(const void* parameters,uintptr_t caller)
{
    namespace effect=contract::anniversary_screen_effect;
    auto* hud=anniversaryNaturalHud;
    // The call at 0xB32101, inside an owned late callback of a frame pass: it waits for the gameplay HUD it precedes.
    static_assert(effect::relatives[0].target==effect::native_screen_effect_draw);
    constexpr uint32_t afterCall=effect::relatives[0].rva+effect::relatives[0].size;
    if (hud&&!hud->entered&&!hud->screenEffect&&parameters&&caller==bindings.base+afterCall&&
        screenEffectInstalled.load(std::memory_order_acquire)&&!screenEffectFaulted.load()&&
        Game_IsAimFramePass()&&AnniversaryHud_NaturalCurrent(*hud))
    { hud->screenEffect=parameters;return; }
    reinterpret_cast<ScreenEffectFn>(screenEffectHook.original)(parameters);
}
void __fastcall ScreenEffectHook(const void* parameters)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { ScreenEffectBody(parameters,reinterpret_cast<uintptr_t>(_ReturnAddress())); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
bool AnniversaryHud_RemoveScreenEffect() noexcept
{
    screenEffectInstalled=false;
    if (screenEffectHook.enabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(screenEffectHook.target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        screenEffectHook.enabled=false;
    }
    if (screenEffectHook.target)
    {
        const void* detours[]{reinterpret_cast<void*>(&ScreenEffectHook)};
        const void* originals[]{screenEffectHook.original};
        if (!WaitForNativeDetourQuiescence(detours,originals,1,callbacks)) return false;
        const auto result=MH_RemoveHook(screenEffectHook.target);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return false;
        screenEffectHook={};
    }
    if (screenEffectScratchBusy.exchange(true,std::memory_order_acq_rel)) return false;
    ScreenEffect_ReleaseScratch(screenEffectScratch);
    screenEffectScratch.refused={};
    screenEffectWanted.Publish({});
    screenEffectFaulted=false;
    screenEffectScratchBusy.store(false,std::memory_order_release);
    return true;
}
void AnniversaryHud_InstallScreenEffect() noexcept
{
    namespace effect=contract::anniversary_screen_effect;
    const NativeContractSet set{effect::entries,effect::witnesses,effect::relatives,effect::pointers};
    const char* failure{};
    if (!VerifyNativeFeatureBindings(bindings.base,bindings.size,generation.load(),set,failure))
    { LOG("CE Anniversary zoom art stock fallback: %s",failure?failure:"binding verification");return; }
    auto* target=reinterpret_cast<void*>(bindings.base+effect::native_screen_effect_draw);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&ScreenEffectHook),&screenEffectHook.original)!=MH_OK)
    { LOG("CE Anniversary zoom art stock fallback: hook creation failed");return; }
    screenEffectHook.target=target;
    if (MH_EnableHook(target)!=MH_OK)
    { (void)AnniversaryHud_RemoveScreenEffect();LOG("CE Anniversary zoom art stock fallback: hook enable failed");return; }
    screenEffectHook.enabled=true;screenEffectInstalled=true;
    LOG("CE Anniversary zoom art installed: a zoomed frame's scope mask and blur run once per eye before its HUD");
}

bool AnniversaryHud_Remove() noexcept
{
    if (!AnniversaryHud_RemoveScreenEffect()) return false;
    anniversaryHudInstalled=false;
    anniversaryHudNaturalInstalled=false;
    if (anniversaryHudHook.enabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(anniversaryHudHook.target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        anniversaryHudHook.enabled=false;
    }
    if (!anniversaryHudHook.target) return true;
    const void* detours[]{reinterpret_cast<void*>(&AnniversaryHudCallbackHook)};
    const void* originals[]{anniversaryHudHook.original};
    if (!WaitForNativeDetourQuiescence(detours,originals,1,callbacks)) return false;
    const auto result=MH_RemoveHook(anniversaryHudHook.target);
    if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return false;
    anniversaryHudHook={};
    return true;
}
bool AnniversaryHud_Install() noexcept
{
    if (!kCeAnniversaryManualHudReplayEnabled)
    {
        LOG("CE Anniversary HUD stock fallback: manual callback replay disabled after native cleanup failure; camera retained");
        return false;
    }
    const NativeContractSet set{contract::anniversary_hud::entries,contract::anniversary_hud::witnesses,
        contract::anniversary_hud::relatives,contract::anniversary_hud::pointers};
    const char* failure{};
    if (!VerifyNativeFeatureBindings(bindings.base,bindings.size,generation.load(),set,failure))
    { LOG("CE Anniversary HUD stock fallback: %s; camera retained",failure?failure:"binding verification"); return false; }
    auto* target=reinterpret_cast<void*>(bindings.base+contract::anniversary_hud::native_hud_callback);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&AnniversaryHudCallbackHook),
        &anniversaryHudHook.original)!=MH_OK) return false;
    anniversaryHudHook.target=target;
    if (MH_EnableHook(target)!=MH_OK) { (void)AnniversaryHud_Remove(); return false; }
    anniversaryHudHook.enabled=true; anniversaryHudInstalled=true;
    LOG("CE Anniversary HUD installed: per-eye native callback before exact source copy; independent target/raster restoration, visible result unverified");
    return true;
}

bool AnniversaryHud_InstallNatural() noexcept
{
    if (!kCeAnniversaryUnpreparedHudReplayEnabled&&!kCeAnniversaryPreparedHudTargetsEnabled&&
        !kCeAnniversaryNativeReadyHudTargetsEnabled)
    { LOG("CE Anniversary unprepared HUD replay disabled after 22cb813 headset failure; camera retained");return false; }
    const NativeContractSet set{contract::anniversary_hud::entries,contract::anniversary_hud::witnesses,
        contract::anniversary_hud::relatives,contract::anniversary_hud::pointers};
    const char* failure{};
    if (!hudTargetBindingsVerified.load()||
        !VerifyNativeFeatureBindings(bindings.base,bindings.size,generation.load(),set,failure))
    { LOG("CE Anniversary HUD stock fallback: %s; camera retained",failure?failure:"cold target proof");return false; }
    auto* target=reinterpret_cast<void*>(bindings.base+contract::anniversary_hud::native_hud_callback);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&AnniversaryHudCallbackHook),&anniversaryHudHook.original)!=MH_OK) return false;
    anniversaryHudHook.target=target;
    if (MH_EnableHook(target)!=MH_OK) { (void)AnniversaryHud_Remove();return false; }
    anniversaryHudHook.enabled=true;anniversaryHudNaturalInstalled=true;
    LOG("CE Anniversary HUD installed: native target preparation and restoration, compatible packed-eye attachments; manual callback replay remains disabled");
    AnniversaryHud_InstallScreenEffect();
    return true;
}

struct AnniversaryHudSource
{
    uintptr_t root{},surface{},resource{},context{},backend{};
    ResourceRegistry::Record record;
    int32_t index{};
};
bool AnniversaryHud_ReadSource(AnniversaryHudSource& out) noexcept
{
    AnniversaryHudSource next{}; uintptr_t vtable{},context{};
    if (!Read(bindings.base+0x1b7b11c,next.index)||next.index<0||next.index>1||
        !Read(bindings.base+0x2d62890+next.index*8,next.root)||!next.root||
        !Read(next.root,vtable)||vtable!=bindings.base+0x17fb608||
        !Read(bindings.base+0x2e3bde0,next.backend)||!next.backend||
        !Read(next.backend,vtable)||vtable!=bindings.base+0x17f9d10||
        !Read(next.backend+0xce0,next.context)||!next.context||
        !Read(bindings.base+0x2ea2d30,context)||context!=next.context) return false;
    __try { next.surface=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(bindings.surfaceSelector)(next.root); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!next.surface||!Read(next.surface,vtable)||vtable!=bindings.base+0x17fb608||
        !Read(next.surface+0xe0,next.resource)||!next.resource||
        !resources.Read(next.resource,next.resource,next.record)) return false;
    out=next; return true;
}
void AnniversaryHud_ReplayEyeBody(FrameScope& scope,int eye,bool& cleanupVerified)
{
    if (!anniversaryHudInstalled.load()||!scope.synthetic||!scope.capture||
        eye<0||eye>1||anniversaryHudReplay) return;
    const uint32_t mask=1u<<eye;
    if (scope.hudAttempted&mask) return;
    scope.hudAttempted|=mask;
    uint32_t failure=1;
    AnniversaryHudReplay replay{};
    AnniversaryHudSource source{};
    ReferenceSample sample{};
    uintptr_t callback{},players{},config{},display{},enabledPointer{},stackStorage{};
    uintptr_t previousOutput{};
    uint8_t nativeInitialized{},nativeDisabled{},nativeRendering{},hudAvailable{};
    int16_t playerCount{}; int32_t stereo{},nativeStackDepth{},nativeStackCapacity{};
    std::array<uint8_t,0x48> targetDescriptor{};
    constexpr UINT rasterCapacity=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT previousViewports[rasterCapacity]{};
    D3D11_RECT previousScissors[rasterCapacity]{};
    UINT previousViewportCount{},previousScissorCount{};
    bool targetRestored=false;
    const auto camera=reinterpret_cast<const SaberCamera*>(scope.renderer+0xf0+eye*sizeof(SaberView));
    // Keep each optional admission refusal attributable in the cold log.
    // Earlier builds collapsed all of these into failure 1, hiding which
    // native state actually prevented HUD replay on the user's machine.
    failure=10;
    if (!(scope.renderFlags&0x10)) goto failed;
    failure=11;
    if (scope.diagnostic.nativeFlags!=1) goto failed;
    failure=AnniversaryEyeTracking(camera,replay.owner.tracking);
    if (failure) goto failed;
    failure=12;
    if (!Read(bindings.base+0x1c33fe0,callback)||callback!=bindings.base+contract::anniversary_hud::native_hud_callback) goto failed;
    failure=13;
    if (!Read(bindings.base+0x2ea2d90,players)||!Read(players+0xb4,playerCount)||playerCount!=1) goto failed;
    failure=14;
    if (!Read(bindings.base+0x2d9bdd1,nativeInitialized)||!nativeInitialized) goto failed;
    failure=15;
    if (!Read(bindings.base+0x2b23700,nativeDisabled)||nativeDisabled) goto failed;
    failure=16;
    if (!Read(bindings.base+0x2d91330,enabledPointer)||!Read(enabledPointer+2,nativeRendering)||!nativeRendering) goto failed;
    failure=17;
    if (!Read(bindings.base+0x2e3b829,hudAvailable)||!hudAvailable) goto failed;
    failure=18;
    if (!Read(bindings.base+0x2e3bdd8,config)||!Read(config+0x238,stereo)||stereo) goto failed;
    failure=19;
    if (!Read(config+0x118,display)||!Read(display+0x10,replay.width)||!Read(display+0x14,replay.height)) goto failed;
    failure=20;
    if (!Read(bindings.base+0x2d9cb34,replay.owner.camera)||!Valid(replay.owner.camera)) goto failed;
    failure=21;
    if (!publishedReference.Read(sample)||sample.revision!=scope.prepared.referenceRevision) goto failed;
    failure=22;
    if (!AnniversaryHud_ReadSource(source)) goto failed;
    failure=2;
    replay.owner.reference=sample.value; replay.owner.referenceRevision=sample.revision;
    replay.owner.rendererEpoch=ceRendererEpoch.load();
    replay.owner.unitsPerMeter=Game_GetWorldScale(); replay.owner.positional=Game_IsPositionalTracking();
    replay.context=reinterpret_cast<ID3D11DeviceContext*>(source.context);
    replay.eyeWidth=source.record.descriptor.Width; replay.eyeHeight=source.record.descriptor.Height;
    if (!HaloCE_RenderContextCurrent(replay.owner)||
        !std::isfinite(replay.owner.unitsPerMeter)||replay.owner.unitsPerMeter<=0||replay.owner.unitsPerMeter>10||
        replay.width!=replay.eyeWidth||replay.height!=2*replay.eyeHeight||
        replay.eyeWidth!=scope.prepared.receipt.pair.cameras[eye].viewportWidth||
        replay.eyeHeight!=scope.prepared.receipt.pair.cameras[eye].viewportHeight||
        replay.owner.camera.viewport.left||replay.owner.camera.viewport.top||
        replay.owner.camera.viewport.right!=replay.width||replay.owner.camera.viewport.bottom!=replay.height||
        !Read(bindings.base+0x2e3d0d0,previousOutput)||
        !Read(source.backend+0x10,nativeStackDepth)||nativeStackDepth<0||
        !Read(source.backend+0x14,nativeStackCapacity)||nativeStackCapacity<2||
        nativeStackCapacity>4096||nativeStackDepth>nativeStackCapacity-2||
        !Read(source.backend+8,stackStorage)||!stackStorage||
        !Read(source.backend+0x18,targetDescriptor)||
        !HaloCEHudLayout_CopyState(replay.context,&previousViewportCount,previousViewports,
            &previousScissorCount,previousScissors)) goto failed;
    {
        // Both native pushes fit the existing stack: this adapter never grows
        // native storage. The callback borrows kind1/2 and restores them itself.
        // Our outer push/pop restores the pre-HUD backend target and viewport.
        bool pushed=false,borrowed=false;
        cleanupVerified=false;
        __try
        {
            reinterpret_cast<AnniversaryHudFn>(bindings.base+contract::anniversary_hud::native_target_push)();
            pushed=true;
            *reinterpret_cast<uintptr_t*>(bindings.base+0x2e3d0d0)=source.root; borrowed=true;
            anniversaryHudReplay=&replay;
            reinterpret_cast<AnniversaryHudFn>(bindings.base+contract::anniversary_hud::native_hud_preamble)();
        }
        __finally
        {
            anniversaryHudReplay=nullptr;
            uintptr_t outputAfter{};
            const bool outputOwned=borrowed&&Read(bindings.base+0x2e3d0d0,outputAfter)&&outputAfter==source.root;
            if (outputOwned) *reinterpret_cast<uintptr_t*>(bindings.base+0x2e3d0d0)=previousOutput;
            if (pushed)
            {
                uintptr_t backendAfter{},contextAfter{},storageAfter{};
                int32_t depthAfter{};
                std::array<uint8_t,0x48> descriptorAfter{};
                const bool popOwned=Read(bindings.base+0x2e3bde0,backendAfter)&&
                    backendAfter==source.backend&&Read(source.backend+0xce0,contextAfter)&&
                    contextAfter==source.context&&Read(source.backend+8,storageAfter)&&
                    storageAfter==stackStorage&&Read(source.backend+0x10,depthAfter)&&
                    depthAfter==nativeStackDepth+1;
                const auto popped=popOwned?reinterpret_cast<uint8_t(__fastcall*)()>(
                    bindings.base+contract::anniversary_hud::native_target_pop)():0;
                targetRestored=outputOwned&&popped&&Read(source.backend+0x10,depthAfter)&&
                    depthAfter==nativeStackDepth&&Read(source.backend+0x18,descriptorAfter)&&
                    descriptorAfter==targetDescriptor;
                if (targetRestored)
                {
                    // The native pop restores target dimensions, not an earlier
                    // custom viewport. Restore its separately observed numeric
                    // raster only after the descriptor and stack match again.
                    replay.context->RSSetViewports(previousViewportCount,previousViewports);
                    replay.context->RSSetScissorRects(previousScissorCount,previousScissors);
                }
            }
            cleanupVerified=targetRestored&&replay.rasterRestored;
        }
    }
    if (!cleanupVerified)
    {
        // Unknown target/raster ownership invalidates only this frame. Keep
        // the native hooks and XR lifecycle available for the next frame.
        scope.capture=false;
        scope.diagnostic.failure=FrameFailure::IncompletePair;
        failure=5;
        goto failed;
    }
    failure=3;
    {
        AnniversaryHudSource after{};
        if (!replay.entered||!replay.returned||!replay.rasterRestored||!targetRestored||
            !HaloCE_RenderContextCurrent(replay.owner)||!AnniversaryHud_ReadSource(after)||
            after.root!=source.root||after.surface!=source.surface||after.resource!=source.resource||
            after.context!=source.context||after.record.revision!=source.record.revision) goto failed;
    }
    anniversaryHudFailure.store(0,std::memory_order_relaxed);
    anniversaryHudDraws.fetch_add(1,std::memory_order_relaxed); return;
failed:
    anniversaryHudFailure.store(failure,std::memory_order_relaxed);
    anniversaryHudFallbacks.fetch_add(1,std::memory_order_relaxed);
}
void AnniversaryHud_ReplayEye(FrameScope& scope,int eye)
{
    bool cleanupVerified=true;
    __try { AnniversaryHud_ReplayEyeBody(scope,eye,cleanupVerified); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        // Native optional HUD callbacks can fault after taking ownership.
        // Their finally blocks run before this handler; retain the world pair
        // only when those blocks proved target, stack and raster restoration.
        if (!cleanupVerified)
        {
            scope.capture=false;
            scope.diagnostic.failure=FrameFailure::IncompletePair;
        }
        anniversaryHudFailure.store(cleanupVerified?4u:5u,std::memory_order_relaxed);
        anniversaryHudFallbacks.fetch_add(1,std::memory_order_relaxed);
    }
}
