// Production CE crosshair transaction and lifetime tests. Shared D3D/XR calls
// are explicit fixtures: these do not establish native HUD pixels or framing.
#include "../src/dll/haloce_hud.cpp"
#include "../src/common/haloce_reticle_logic.h"
#include <cstdio>
#include <vector>

static GameTitle testTitle=GameTitle::HaloCE;
static uint32_t testGeneration=3;
static halo_ce::RenderContext testContext{};
static bool contextValid=true,beginAllowed=true,endAllowed=true;
static bool redirectActive{},redirectAuthored{},raiseNative{},reenterNative{},changeRevision{};
static unsigned nativeCalls{},captureBegins{},suppressionBegins{},ends{},invalidations{};
static unsigned layoutSuspensions{};
static bool nativeFramingIsolated=true,canPrepare=true,suppressionReady=true;
static AuthoredReticlePreparationResult preparationResult=AuthoredReticlePreparationResult::Ready;
static bool aimAvailable=true,playerEligible=true;
static bool seated=false,vehicleAimCurrent=true;
static bool coldTargetProof=true;
static uint64_t displayedSerial{};
static bool coveragePending{};
static ID3D11DeviceContext* pixelContext{};
static ID3D11RenderTargetView* pixelCapture{},*pixelDiscard{};
static float nativeInk=1;
bool HaloCE_HudTargetBindingsVerified(uintptr_t base,size_t size,uint32_t gen) noexcept
{ return coldTargetProof&&base==moduleBase&&size==halo_ce::contract::imageSize&&gen==testGeneration; }
bool HaloCEFirstPerson_AimArmed() noexcept { return aimAvailable; }
// Aim Frame Zoom (game.cpp): a frame's pass draws the game's crosshair as it is; the capture waits while zoomed.
static bool aimFramePass{},crosshairReadyForCapture=true;
bool Game_IsAimFramePass() { return aimFramePass; }
bool Game_AimFrameCrosshairReadyForCapture(GameTitle) { return crosshairReadyForCapture; }
bool HaloCEFirstPerson_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{
    state.hasControlledUnit=playerEligible;
    state.onFoot=state.nativePreparesFirstPerson=playerEligible&&!seated;
    state.nativeInputBlocked=state.nativeLookBlocked=false;
    return true;
}
bool HaloCEUnitControl_VehicleAimCurrent(const HaloCELocalPlayerState&,
    const halo_ce::RenderContext&) noexcept { return seated&&vehicleAimCurrent; }
bool HaloCEHudTarget_Read(uintptr_t,ID3D11DeviceContext*,CeHudTargetSnapshot&) noexcept { return false; }
void HaloCEHudLayout_Suspend() noexcept { ++layoutSuspensions; }
void HaloCEHudLayout_Resume() noexcept { --layoutSuspensions; }
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return testGeneration; }
bool HaloCE_Armed() noexcept { return testTitle==GameTitle::HaloCE; }
bool HaloCE_GetRenderContext(const halo_ce::Camera&,halo_ce::RenderContext& out) noexcept
{ out=testContext;return contextValid; }
bool HaloCE_RenderContextCurrent(const halo_ce::RenderContext& context) noexcept
{
    return contextValid&&testTitle==GameTitle::HaloCE&&context.tracking.generation==testGeneration&&
        context.referenceRevision==testContext.referenceRevision&&
        context.rendererEpoch==testContext.rendererEpoch;
}
bool VR_CeAuthoredReticleFrameMatches(ID3D11DeviceContext* context,uint64_t serial)
{ return context==reinterpret_cast<ID3D11DeviceContext*>(0x12340)&&
    halo_ce::ReticleReceiptSerialCurrent(serial,displayedSerial?displayedSerial:testContext.tracking.serial); }
bool VR_CanPrepareAuthoredReticleResources() { return canPrepare; }
AuthoredReticlePreparationResult VR_PrepareAuthoredReticleResources()
{ return preparationResult; }
bool VR_PrepareAuthoredReticleSuppressionResources() { return suppressionReady; }
bool VR_ShouldCaptureAuthoredReticleThisFrame()
{ return halo_ce::ReticleCanReplaceCapture(coveragePending); }
bool VR_BeginPreparedAuthoredReticleCapture()
{
    ++captureBegins;
    if (!beginAllowed||redirectActive) return false;
    redirectActive=redirectAuthored=true;return true;
}
bool VR_BeginPreparedAuthoredReticleSuppression()
{
    ++suppressionBegins;
    if (!beginAllowed||redirectActive) return false;
    redirectActive=true;redirectAuthored=false;return true;
}
bool VR_EndPreparedAuthoredReticleCapture()
{ ++ends;const bool match=redirectActive&&redirectAuthored;redirectActive=false;return match&&endAllowed; }
bool VR_EndPreparedAuthoredReticleSuppression()
{ ++ends;const bool match=redirectActive&&!redirectAuthored;redirectActive=false;return match&&endAllowed; }
void VR_InvalidatePreparedAuthoredReticleCapture() { ++invalidations;coveragePending=false; }
void Logf(const char*,...) { }
bool WaitForNativeDetourQuiescence(const void* const*,const void* const*,size_t,
    const std::atomic<uint32_t>& count) { return !count.load(); }
static void __fastcall NativeCrosshair(int32_t user,uint32_t weapon,uint32_t hud,const void* state)
{
    ++nativeCalls;
    nativeFramingIsolated=nativeFramingIsolated&&layoutSuspensions>0;
    if (pixelContext&&redirectActive)
    {
        const float color[4]{nativeInk,nativeInk,nativeInk,nativeInk};
        pixelContext->ClearRenderTargetView(redirectAuthored?pixelCapture:pixelDiscard,color);
    }
    if (reenterNative) { reenterNative=false;CrosshairHook(user,weapon,hud,state); }
    if (changeRevision) ++testContext.referenceRevision;
    if (raiseNative) RaiseException(0xe0424242,0,0,nullptr);
}
static bool ExceptionPassesThrough()
{
    __try { CrosshairHook(0,17,29,nullptr); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
int main()
{
    unsigned failures=0;
    const auto check=[&](bool ok,const char* text) {
        if (!ok) { ++failures;std::fprintf(stderr,"CE HUD: %s\n",text); }
    };
    std::vector<uint8_t> memory(halo_ce::contract::imageSize);
    moduleBase=reinterpret_cast<uintptr_t>(memory.data());
    const uintptr_t nativeContext=0x12340;
    std::memcpy(memory.data()+0x2ea2d30,&nativeContext,sizeof(nativeContext));
    active=installed=prepared=true;retiring=false;generation=3;original=&NativeCrosshair;
    testContext.tracking.generation=3;testContext.tracking.serial=10;
    testContext.tracking.controllers.primaryAim.valid=true;
    testContext.tracking.controllers.controlsPresentationBlocked=false;
    testContext.referenceRevision=2;testContext.rendererEpoch=4;
    prepared=false;targetBindingsVerified=false;
    coldTargetProof=false;
    PrepareCapture(moduleBase,memory.size(),3);
    check(!prepared.load()&&rejectedCaptureGeneration==3&&HaloCEHud_HasCrosshairScope(),
        "missing cold target proof isolates capture without disarming native HUD");
    coldTargetProof=true;rejectedCaptureGeneration=0;
    canPrepare=false;
    PrepareCapture(moduleBase,memory.size(),3);
    check(targetBindingsVerified&&!prepared.load()&&!rejectedCaptureGeneration,
        "current core proof admits target bindings without rescanning patched native bytes");
    canPrepare=true;
    check(HaloCEHud_HasCrosshairScope(),"native crosshair scope does not depend on private capture resources");
    CrosshairHook(0,17,29,nullptr);
    check(!captureBegins&&nativeCalls==1&&nativeFramingIsolated&&!layoutSuspensions,
        "pending capture runs native art with the gameplay HUD affine suspended");
    preparationResult=AuthoredReticlePreparationResult::Failed;
    PrepareCapture(moduleBase,memory.size(),3);
    check(HaloCEHud_HasCrosshairScope()&&!prepared.load()&&rejectedCaptureGeneration==3,
        "optional capture preparation failure retains native crosshair scope and HUD independence");
    preparationResult=AuthoredReticlePreparationResult::Ready;rejectedCaptureGeneration=0;
    suppressionReady=false;PrepareCapture(moduleBase,memory.size(),3);
    check(HaloCEHud_HasCrosshairScope()&&!prepared.load()&&rejectedCaptureGeneration==3,
        "optional discard preparation failure cannot remove the crosshair boundary");
    suppressionReady=true;rejectedCaptureGeneration=0;canPrepare=false;
    PrepareCapture(moduleBase,memory.size(),3);
    check(HaloCEHud_HasCrosshairScope()&&!prepared.load()&&!rejectedCaptureGeneration,
        "resources not ready yet retain scope and allow a later preparation retry");
    canPrepare=true;PrepareCapture(moduleBase,memory.size(),3);
    check(prepared.load(),"ready optional resources enable capture without reinstalling the native scope");
    nativeCalls=0;
    CrosshairHook(0,17,29,nullptr);
    const auto key=HaloCEHud_CrosshairKey();
    check(key&&captureBegins==1&&nativeCalls==1&&ends==1&&!redirectActive&&!callbacks.load()&&!layoutSuspensions,
        "first native phase completes capture and retires its scope");
    CrosshairHook(0,17,29,nullptr);
    check(suppressionBegins==1&&nativeCalls==2&&HaloCEHud_CrosshairKey()==key,
        "second eye runs native logic in discard phase without replacing art identity");
    ++testContext.tracking.serial;
    CrosshairHook(0,18,30,nullptr);
    check(HaloCEHud_CrosshairKey()!=key,"weapon and HUD change publish a different art identity");
    ++testContext.tracking.serial;reenterNative=true;
    const unsigned before=captureBegins;
    CrosshairHook(0,18,30,nullptr);
    check(captureBegins==before+1&&!drawing&&!redirectActive&&!callbacks.load(),
        "nested native work never opens a second redirect or leaks callback ownership");
    ++testContext.tracking.serial;beginAllowed=false;
    const unsigned fallbackBefore=nativeCalls;
    CrosshairHook(0,17,29,nullptr);
    check(nativeCalls==fallbackBefore+1&&!HaloCEHud_CapturedCrosshair()&&!redirectActive,
        "failed optional capture still runs stock native drawing and revokes authored ownership");
    beginAllowed=true;
    CrosshairHook(0,17,29,nullptr);
    check(HaloCEHud_CapturedCrosshair(),"next native phase recovers without camera or hook reinstallation");
    beginAllowed=false;
    CrosshairHook(0,17,29,nullptr);
    beginAllowed=true;
    const unsigned recaptures=captureBegins;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==recaptures+1&&HaloCEHud_CapturedCrosshair(),
        "failed second-eye suppression requires a real new capture before claiming authored art again");
    ++testContext.tracking.serial;endAllowed=false;
    CrosshairHook(0,17,29,nullptr);endAllowed=true;
    check(!HaloCEHud_CapturedCrosshair()&&invalidations&&!redirectActive,
        "failed capture completion invalidates shared art and restores redirect scope");
    ++testContext.tracking.serial;changeRevision=true;
    CrosshairHook(0,17,29,nullptr);changeRevision=false;
    check(!HaloCEHud_CapturedCrosshair(),"recenter during native callback cannot publish old-owner pixels");
    ++testContext.tracking.serial;raiseNative=true;
    check(ExceptionPassesThrough(),"native structured exception retains native propagation");raiseNative=false;
    check(!callbacks.load()&&!drawing&&!redirectActive&&!HaloCEHud_CapturedCrosshair(),
        "exception finally restores feature scope and invalidates unfinished capture");
    CrosshairHook(0,17,29,nullptr);
    check(HaloCEHud_CapturedCrosshair(),"successful phase recovers after native exception fixture");
    ++testContext.rendererEpoch;
    check(!HaloCEHud_CapturedCrosshair(),"graphics-mode epoch rejects old capture identity");
    const unsigned modeCaptures=captureBegins;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==modeCaptures+1,"new renderer cannot claim pixels from a discard-only phase");
    const unsigned eligibleCaptures=captureBegins;
    aimAvailable=false;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==eligibleCaptures&&!HaloCEHud_CapturedCrosshair(),
        "unavailable controller aim leaves the native reticle visible and revokes old art");
    aimAvailable=true;playerEligible=false;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==eligibleCaptures&&!HaloCEHud_CapturedCrosshair(),
        "unproven on-foot ownership cannot hide the native reticle");
    playerEligible=true;
    displayedSerial=testContext.tracking.serial;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==eligibleCaptures+1&&HaloCEHud_CapturedCrosshair(),
        "eligible local player must recapture after stock fallback");
    // Reproduce the headset report with the actual production draw scope:
    // a seated player has no on-foot first-person model, but still draws art.
    seated=true;
    for (uint64_t age : {0ull,1ull,8ull})
    {
        displayedSerial=++testContext.tracking.serial+age;
        const unsigned beforeVehicleCapture=captureBegins,beforeSuppression=suppressionBegins;
        const auto beforeVehicleCount=vehicleCaptures.load();
        CrosshairHook(0,71,82,nullptr);
        const auto vehicleKey=HaloCEHud_CrosshairKey();
        CrosshairHook(0,71,82,nullptr);
        check(captureBegins==beforeVehicleCapture+1&&suppressionBegins==beforeSuppression+1&&
            vehicleKey&&HaloCEHud_CrosshairKey()==vehicleKey&&
            vehicleCaptures.load()==beforeVehicleCount+2&&!redirectActive&&!callbacks.load(),
            "Original and late Anniversary seated crosshair captures once and suppresses the centered copy in both eyes");
    }
    vehicleAimCurrent=false;
    CrosshairHook(0,71,82,nullptr);
    check(!HaloCEHud_CapturedCrosshair(),"unproved seated aiming retains stock crosshair and revokes old art");
    vehicleAimCurrent=true;
    CrosshairHook(0,71,82,nullptr);
    check(HaloCEHud_CapturedCrosshair(),"seated aiming recovers without reinstalling stereo");
    seated=false;displayedSerial=++testContext.tracking.serial;
    CrosshairHook(0,17,29,nullptr);
    check(HaloCEHud_CapturedCrosshair(),"vehicle exit restores on-foot native art capture");
    // The natural Anniversary callback carries the worker's frozen receipt,
    // while the OpenXR submission is already one or more samples newer.
    for (uint64_t age=1;age<=8;++age)
    {
        ++testContext.tracking.serial;displayedSerial=testContext.tracking.serial+age;
        const unsigned beforeLateCapture=captureBegins;
        CrosshairHook(0,17,29,nullptr);
        check(captureBegins==beforeLateCapture+1&&HaloCEHud_CapturedCrosshair(),
            "owned late Anniversary HUD captures art and suppresses the native screen crosshair");
    }
    displayedSerial=testContext.tracking.serial+9;
    const unsigned staleCapture=captureBegins;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==staleCapture&&!HaloCEHud_CapturedCrosshair(),
        "expired worker receipt leaves native art stock without publishing stale ownership");
    displayedSerial=testContext.tracking.serial-1;
    CrosshairHook(0,17,29,nullptr);
    check(captureBegins==staleCapture&&!HaloCEHud_CapturedCrosshair(),
        "future receipt cannot capture into an earlier displayed frame");
    displayedSerial=testContext.tracking.serial;
    CrosshairHook(0,17,29,nullptr);
    {
        // Real GPU source A is queued for coverage. The next weapon emits B
        // (blank) before that query is consumed. Execute the production native
        // scope: B must reach discard while both source A and its key stay put.
        ID3D11Device* device{};D3D_FEATURE_LEVEL feature{};
        if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&feature,&pixelContext))) return 2;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.ArraySize=desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* source{},*discard{},*queued{},*readback{};
        if (FAILED(device->CreateTexture2D(&desc,nullptr,&source))||
            FAILED(device->CreateTexture2D(&desc,nullptr,&discard))||
            FAILED(device->CreateRenderTargetView(source,nullptr,&pixelCapture))||
            FAILED(device->CreateRenderTargetView(discard,nullptr,&pixelDiscard))) return 2;
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&desc,nullptr,&queued))||
            FAILED(device->CreateTexture2D(&desc,nullptr,&readback))) return 2;
        const auto pixel=[&](ID3D11Texture2D* texture) {
            pixelContext->CopyResource(readback,texture);D3D11_MAPPED_SUBRESOURCE mapped{};
            uint32_t value=0x12345678;
            if (SUCCEEDED(pixelContext->Map(readback,0,D3D11_MAP_READ,0,&mapped)))
            { std::memcpy(&value,mapped.pData,4);pixelContext->Unmap(readback,0); }
            return value;
        };
        displayedSerial=++testContext.tracking.serial;nativeInk=1;
        CrosshairHook(0,17,29,nullptr);
        const auto sourceKey=HaloCEHud_CrosshairKey();
        pixelContext->CopyResource(queued,source);coveragePending=true;
        const unsigned capturedBefore=captureBegins,discardedBefore=suppressionBegins;
        displayedSerial=++testContext.tracking.serial;nativeInk=0;
        CrosshairHook(0,18,30,nullptr);
        CrosshairHook(0,18,30,nullptr);
        check(coveragePending&&captureBegins==capturedBefore&&suppressionBegins==discardedBefore+2&&
            HaloCEHud_CrosshairKey()==sourceKey&&pixel(source)==0xffffffff&&pixel(discard)==0&&
            pixel(queued)==0xffffffff,
            "pending coverage keeps exact GPU source and art key while both new-weapon phases draw blank into discard");
        coveragePending=false;displayedSerial=++testContext.tracking.serial;
        CrosshairHook(0,18,30,nullptr);
        check(captureBegins==capturedBefore+1&&HaloCEHud_CrosshairKey()!=sourceKey&&pixel(source)==0,
            "completed coverage permits the next real capture to publish the changed weapon key");
        // Revoking the previous renderer/reference cancels its queued sample.
        coveragePending=true;++testContext.rendererEpoch;nativeInk=1;
        const unsigned epochCaptures=captureBegins;
        CrosshairHook(0,17,29,nullptr);
        check(!coveragePending&&captureBegins==epochCaptures+1&&pixel(source)==0xffffffff,
            "renderer change cancels old queued coverage and requires real current-owner pixels");
        coveragePending=true;displayedSerial=++testContext.tracking.serial;beginAllowed=false;
        CrosshairHook(0,18,30,nullptr);beginAllowed=true;
        check(!coveragePending&&!HaloCEHud_CapturedCrosshair(),
            "failed pending suppression cancels its sample and revokes native ownership");
        const unsigned recoveryCaptures=captureBegins;
        CrosshairHook(0,18,30,nullptr);
        check(captureBegins==recoveryCaptures+1&&HaloCEHud_CapturedCrosshair(),
            "pending suppression failure recovers through a fresh capture");
        coveragePending=true;contextValid=false;
        CrosshairHook(0,18,30,nullptr);contextValid=true;
        check(!coveragePending&&!HaloCEHud_CapturedCrosshair(),
            "expired native context cannot carry a pending measurement into the next scope");
        CrosshairHook(0,18,30,nullptr);
        pixelCapture->Release();pixelDiscard->Release();source->Release();discard->Release();
        queued->Release();readback->Release();pixelContext->Release();device->Release();
        pixelContext=nullptr;pixelCapture=pixelDiscard=nullptr;
    }
    ++testGeneration;
    check(!HaloCEHud_CapturedCrosshair(),"module generation change rejects previous crosshair ownership");
    --testGeneration;testTitle=GameTitle::Halo3;
    check(!HaloCEHud_CapturedCrosshair(),"CE does not own another title's reticle");
    testTitle=GameTitle::HaloCE;
    CaptureReceipt receipt{};lastCapture.Read(receipt);receipt.capturedAtMs=GetTickCount64()-300;
    lastCapture.Publish(receipt);
    check(!HaloCEHud_CapturedCrosshair(),"missing native phases cannot indefinitely retain captured-once ownership");
    check(nativeFramingIsolated,"every native phase including stock fallbacks excludes gameplay HUD framing");
    return failures?1:0;
}
