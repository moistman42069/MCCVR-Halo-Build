// Production optional scope, numeric observer, and real WARP raster-state
// transactions. No native game code or headset appearance is simulated.
#include "../src/dll/haloce_hud_layout.cpp"
#include <cstdio>
#include <vector>
#include <limits>
#include <thread>

static GameTitle testTitle=GameTitle::HaloCE;
static uint32_t testGeneration=3;
static halo_ce::RenderContext testOwner{};
static bool ownerValid=true,crosshairScopeAvailable=true;
static bool expectHidden=false;
static bool nativeResourcesReady=true;
static uintptr_t nativeResourceCheckedBase{};
static uint32_t nativeResourceCheckedGeneration{};
static ID3D11DeviceContext* testDeviceContext{};
static UINT failureCount{},nativeCalls{},scenario{};
static UINT fullResolutionEyeTop{};
static D3D11_VIEWPORT initialViewport{20,30,1000,800,0.2f,0.8f};
static D3D11_RECT initialScissor{20,30,1020,830};
static void Check(bool ok,const char* message)
{ if (!ok) { ++failureCount;std::fprintf(stderr,"CE HUD layout: %s\n",message); } }
static bool Near(float a,float b) { return std::fabs(a-b)<0.001f; }
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return testGeneration; }
bool HaloCE_Armed() noexcept { return testTitle==GameTitle::HaloCE; }
bool HaloCE_NativeHudResourcesReady(uintptr_t base,uint32_t gen) noexcept
{
    nativeResourceCheckedBase=base;nativeResourceCheckedGeneration=gen;
    return nativeResourcesReady;
}
bool HaloCE_BeginAnniversaryHudGameplay(ID3D11DeviceContext*,UINT&,UINT&) noexcept { return false; }
void HaloCE_CaptureClassicDlssDepth(uintptr_t) noexcept {}
void HaloCE_EndAnniversaryHudGameplay(bool) noexcept {}
bool HaloCEHud_HasCrosshairScope() noexcept { return crosshairScopeAvailable; }
bool HaloCE_GetRenderContext(const halo_ce::Camera&,halo_ce::RenderContext& out) noexcept
{ out=testOwner;return ownerValid; }
bool HaloCE_RenderContextCurrent(const halo_ce::RenderContext& owner) noexcept
{ return ownerValid&&owner.referenceRevision==testOwner.referenceRevision&&
    owner.rendererEpoch==testOwner.rendererEpoch&&owner.tracking.generation==testGeneration; }
void Logf(const char*,...) {}
bool WaitForNativeDetourQuiescence(const void* const*,const void* const*,size_t,
    const std::atomic<uint32_t>& count) { return !count.load(); }
static D3D11_VIEWPORT ActualViewport()
{ D3D11_VIEWPORT v{};UINT count=1;testDeviceContext->RSGetViewports(&count,&v);return v; }
static D3D11_RECT ActualScissor()
{ D3D11_RECT r{};UINT count=1;testDeviceContext->RSGetScissorRects(&count,&r);return r; }
static void NativeSetViewport(const D3D11_VIEWPORT& in)
{
    D3D11_VIEWPORT out[capacity]{};
    const bool transformed=HaloCEHudLayout_PrepareViewports(testDeviceContext,1,&in,out);
    testDeviceContext->RSSetViewports(1,transformed?out:&in);
}
static void NativeSetScissor(const D3D11_RECT& in)
{
    D3D11_RECT out[capacity]{};
    const bool transformed=HaloCEHudLayout_PrepareScissors(testDeviceContext,1,&in,out);
    testDeviceContext->RSSetScissorRects(1,transformed?out:&in);
}
static void Seed()
{ NativeSetViewport(initialViewport);NativeSetScissor(initialScissor); }
static void __fastcall FullResolutionHud()
{
    const auto verify=[] {
        const auto v=ActualViewport();
        Check(Near(v.TopLeftX,728)&&Near(v.Width,1456)&&Near(v.Height,1050)&&
            Near(v.TopLeftY,float(fullResolutionEyeTop)+525-testOwner.tracking.hud.verticalOffset),
            "2912x2100 eyes retain HUD size and exact output-pixel height controls");
        const auto r=ActualScissor();
        Check(r.top>=LONG(fullResolutionEyeTop)&&r.bottom<=LONG(fullResolutionEyeTop+2100),
            "full-resolution HUD scissors never cross the eye boundary");
    };
    verify();
    NativeSetViewport({0,0,2912,2100,0,1});NativeSetScissor({0,0,2912,2100});
    verify();
    HaloCEHudLayout_Suspend();
    Check(Near(ActualViewport().Height,2100),"full-resolution reticle suspension preserves native pixel height");
    HaloCEHudLayout_Resume();verify();
}
static void __fastcall NativeHud()
{
    ++nativeCalls;
    Check(hud_visibility::Hidden()==expectHidden,"only the admitted hidden HUD owns draw suppression");
    auto v=ActualViewport();
    if (scenario==9)
    {
        Check(Near(v.TopLeftX,250)&&Near(v.TopLeftY,84)&&Near(v.Width,500)&&Near(v.Height,200),
            "Anniversary scales authored HUD to eye height and preserves configured output-pixel height");
        auto r=ActualScissor();
        Check(r.left==250&&r.top==84&&r.right==750&&r.bottom==284,
            "Anniversary scissor and viewport receive the same single eye transform");
        HaloCEHudLayout_Suspend();
        Check(Near(ActualViewport().Height,400),"reticle suspension retains Anniversary eye mapping");
        D3D11_VIEWPORT saved[capacity]{};D3D11_RECT savedRects[capacity]{};UINT vc{},rc{};
        Check(HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRects)&&
            saved[0].Height==800&&savedRects[0].bottom==800,
            "private reticle snapshot retains authored pixels for capture and observed restore");
        HaloCEHudLayout_BeginPrivateRaster();
        NativeSetViewport({0,0,64,64,0,1});NativeSetScissor({0,0,64,64});
        Check(Near(ActualViewport().Height,64),"private reticle raster is never halved");
        HaloCEHudLayout_EndPrivateRaster();
        NativeSetViewport(saved[0]);NativeSetScissor(savedRects[0]);
        Check(Near(ActualViewport().Height,400),"native reticle restore halves authored pixels exactly once");
        HaloCEHudLayout_Resume();
        Check(Near(ActualViewport().Height,200)&&Near(ActualViewport().TopLeftY,84),
            "reticle exit restores gameplay affine within the current eye");
        return;
    }
    if (scenario==5) { Check(Near(v.Width,1000),"unowned scope remains native");return; }
    if (scenario==7) { Check(!scope&&Near(v.Width,400),"prior-pass viewport cannot be mistaken for gameplay framing");return; }
    Check(Near(v.TopLeftX,270)&&Near(v.TopLeftY,214)&&Near(v.Width,500)&&Near(v.Height,400),
        "size and height apply around native viewport center");
    auto r=ActualScissor();
    Check(r.left==270&&r.top==214&&r.right==770&&r.bottom==614,
        "scissor clipping follows the same affine");
    Check(v.MinDepth==initialViewport.MinDepth&&v.MaxDepth==initialViewport.MaxDepth,
        "native depth range survives raster transform");
    if (scenario==1)
    {
        NativeSetViewport({100,100,400,300,0,1});
        v=ActualViewport();
        Check(Near(v.TopLeftX,310)&&Near(v.TopLeftY,249)&&Near(v.Width,200),
            "native viewport changes inside scope are transformed exactly once");
    }
    if (scenario==2)
    {
        HaloCEHudLayout_Suspend();HaloCEHudLayout_Suspend();
        Check(Near(ActualViewport().Width,1000),"crosshair enters original native framing");
        D3D11_VIEWPORT saved[capacity]{};D3D11_RECT savedRects[capacity]{};UINT vc{},rc{};
        Check(HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRects)&&vc==1&&rc==1&&
            saved[0].Width==1000,"numeric capture snapshot matches suspended native state");
        HaloCEHudLayout_BeginPrivateRaster();
        NativeSetViewport({0,0,64,64,0,1});
        NativeSetScissor({0,0,64,64});
        testDeviceContext->RSSetViewports(vc,saved);testDeviceContext->RSSetScissorRects(rc,savedRects);
        HaloCEHudLayout_EndPrivateRaster();
        HaloCEHudLayout_Resume();
        Check(Near(ActualViewport().Width,1000),"nested suspension stays native until final exit");
        HaloCEHudLayout_Resume();
        Check(Near(ActualViewport().Width,500),"crosshair exit restores transformed gameplay framing");
    }
    if (scenario==3)
    {
        ++testOwner.referenceRevision;
        NativeSetViewport(initialViewport);
        Check(Near(ActualViewport().Width,1000)&&ActualScissor().left==20,
            "recenter drops layout and restores both native raster states");
    }
    if (scenario==4)
    {
        HaloCEHudLayout_InvalidateState(testDeviceContext);
        Check(Near(ActualViewport().Width,1000),"command-list invalidation first restores native framing");
        testDeviceContext->ClearState();
    }
    if (scenario==6) RaiseException(0xe0424242,0,0,nullptr);
    if (scenario==8)
    {
        std::thread other([] {
            NativeSetViewport({80,90,700,500,0,1});
            NativeSetScissor({80,90,780,590});
        });
        other.join();
    }
}
static bool NativeException()
{
    __try { MainHook(); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
int main()
{
    ID3D11Device* device{};D3D_FEATURE_LEVEL feature{};
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &device,&feature,&testDeviceContext))) return 2;
    std::vector<uint8_t> memory(halo_ce::contract::imageSize);
    moduleBase=reinterpret_cast<uintptr_t>(memory.data());
    std::memcpy(memory.data()+0x2ea2d30,&testDeviceContext,sizeof(testDeviceContext));
    testOwner.tracking.generation=3;testOwner.tracking.serial=5;
    testOwner.tracking.hud={0.5f,1.0f,0.5f,16.0f};
    testOwner.camera.viewport={30,20,830,1020};
    testOwner.referenceRevision=2;testOwner.rendererEpoch=4;
    active=installed=true;generation=3;original=&NativeHud;
    HaloCEHudLayout_SetObservationAvailable(true);
    scenario=0;Seed();testOwner.tracking.hud.hidden=expectHidden=true;
    const auto beforeHidden=nativeCalls;
    MainHook();
    Check(nativeCalls==beforeHidden+1&&!hud_visibility::Hidden()&&HaloCE_Armed(),
        "hiding HUD retains native callback and VR ownership and restores world draws");
    scenario=6;Seed();
    Check(NativeException()&&!hud_visibility::Hidden(),
        "native exception cannot leak hidden HUD state into world or menus");
    testOwner.tracking.hud.hidden=expectHidden=false;
    for (scenario=0;scenario<4;++scenario)
    {
        Seed();MainHook();
        Check(!scope&&!callbacks.load(),"HUD hook retires scope/callback ownership");
        const auto v=ActualViewport();
        Check(Near(v.Width,scenario==1?400.0f:1000.0f),"exit preserves latest native viewport");
    }
    Seed();scenario=4;MainHook();
    D3D11_VIEWPORT saved[capacity]{};D3D11_RECT savedRect[capacity]{};UINT vc{},rc{};
    Check(!HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRect),
        "unknown state cannot be reconstructed after native ClearState");
    Seed();scenario=0;MainHook();
    // A sequential handoff to another native thread changes the SAME WARP
    // context. The old thread must not retain a matching-looking TLS snapshot.
    scenario=8;MainHook();
    Check(Near(ActualViewport().Width,700)&&ActualScissor().left==80,
        "scope exit never restores stale TLS state after another thread mutates the context");
    Check(!HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRect),
        "cross-thread mutation invalidates both old-thread numeric observations");
    NativeSetViewport(initialViewport);
    Check(!HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRect),
        "one new viewport cannot revive a scissor observation from another thread");
    NativeSetScissor(initialScissor);
    Check(HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRect)&&vc==1&&rc==1,
        "same-thread viewport and scissor mutations preserve each other after fresh observation");
    scenario=0;MainHook();
    NativeSetViewport({100,100,400,300,0,1});scenario=7;MainHook();
    Seed();scenario=0;MainHook();
    active=false;
    D3D11_VIEWPORT otherViewport{0,0,42,43,0,1};
    Check(!HaloCEHudLayout_PrepareViewports(testDeviceContext,1,&otherViewport,saved)&&
        Find(testDeviceContext,false)->viewports[0].Width==1000,
        "other-title fast path does not update CE observations");
    active=true;
    Check(Near(ActualViewport().Width,1000),"fresh native observations recover on next frame");
    Seed();scenario=5;ownerValid=false;
    const auto beforeResourceFailure=nativeCalls;
    nativeResourcesReady=false;MainHook();
    Check(nativeCalls==beforeResourceFailure&&callbacks==0&&!scope&&HaloCE_Armed(),
        "disposed native effects block the ordinary fallback draw without leaking callbacks or disarming VR");
    Check(nativeResourceCheckedBase==moduleBase&&nativeResourceCheckedGeneration==generation.load(),
        "HUD readiness is requested for this layout hook's exact module and generation");
    nativeResourcesReady=true;MainHook();ownerValid=true;
    Check(nativeCalls==beforeResourceFailure+1,
        "native resource recreation restores the ordinary fallback draw on the next call");
    Check(Near(ActualViewport().Width,1000),"missing owner remains stock without core teardown");
    crosshairScopeAvailable=false;MainHook();
    Check(Near(ActualViewport().Width,1000),
        "missing native crosshair boundary cannot shift the stock reticle through HUD size or height");
    crosshairScopeAvailable=true;scenario=0;MainHook();
    Check(Near(ActualViewport().Width,1000),"HUD recovers when independent crosshair framing is available");
    scenario=5;
    HaloCEHudLayout_SetObservationAvailable(false);
    HaloCEHudLayout_SetObservationAvailable(true);
    MainHook();
    Check(!HaloCEHudLayout_CopyState(testDeviceContext,&vc,saved,&rc,savedRect),
        "observer recreation invalidates old context state");
    Seed();scenario=6;
    Check(NativeException()&&!scope&&!callbacks.load()&&Near(ActualViewport().Width,1000),
        "native exception propagates after exact numeric state restoration");
    Seed();scenario=0;MainHook();
    // Optical aspect follows the H3 symmetric-eye safe frame correction.
    halo_ce::HudLayoutAffine affine{};
    const float fov[]{-0.785398163f,0.785398163f,0.785398163f,-0.785398163f};
    Check(halo_ce::ComputeHudLayoutAffine(0.8f,1,0,2,fov,View(initialViewport),affine)&&
        Near(affine.horizontal,0.8f)&&Near(affine.vertical,0.4f),"wide native raster corrects vertical scale");
    Check(halo_ce::ComputeHudLayoutAffine(0.8f,1,0,0.5f,fov,View(initialViewport),affine)&&
        Near(affine.horizontal,0.4f)&&Near(affine.vertical,0.8f),"tall native raster corrects horizontal scale");
    const auto prior=affine;
    Check(!halo_ce::ComputeHudLayoutAffine(std::numeric_limits<float>::quiet_NaN(),1,0,1,fov,
        View(initialViewport),affine)&&std::memcmp(&prior,&affine,sizeof(prior))==0,
        "invalid configuration rejects without partial output mutation");
    halo_ce::HudLayoutRect empty{};
    Check(halo_ce::ApplyHudLayoutScissor({0.5f,0.5f,0.25f,0.25f},{1,1,1,10},empty)&&
        empty.left==empty.right,"outward rounding never opens an empty scissor");
    // Execute the production Anniversary replay around the production gameplay
    // scope, including the authored-reticle capture/restore raster sequence.
    testOwner.camera.viewport={0,0,800,1000};scenario=9;
    for (unsigned eye=0;eye<2;++eye)
    {
        Seed();
        Check(HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,400),
            "both Anniversary eyes admit the verified full-height/half-height mapping");
        Check(Near(ActualViewport().Width,1000)&&Near(ActualViewport().Height,400),
            "eye mapping seeds known HUD raster when native caching skips setters");
        Check(!HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,400),
            "nested Anniversary callback cannot overwrite replay restoration state");
        MainHook();
        Check(HaloCEHudLayout_EndEyeReplay()&&Near(ActualViewport().TopLeftY,30)&&
            Near(ActualViewport().Height,800)&&ActualScissor().top==30,
            "each replay restores exact pre-callback viewport and scissor");
    }
    Seed();
    Check(!HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,600)&&
        Near(ActualViewport().TopLeftY,30),"wrong source raster rejects without mutation");
    HaloCEHudLayout_Suspend();
    Check(!HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,400),
        "replay cannot begin inside an unrelated reticle suspension");
    HaloCEHudLayout_Resume();
    Check(HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,400),
        "replay recovers after unrelated suspension ends");
    HaloCEHudLayout_InvalidateState(testDeviceContext);testDeviceContext->ClearState();
    Check(!HaloCEHudLayout_EndEyeReplay(),"invalidated replay cannot falsely report restored raster");
    Seed();
    Check(HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,400)&&
        HaloCEHudLayout_EndEyeReplay(),"next eye transaction recovers from invalidation");
    for (UINT eye=0;eye<2;++eye) for (LONG displacement: {-600L,0L,600L})
    {
        Seed();bool cleanup=true;
        Check(HaloCEHudLayout_BeginEyeReplay(testDeviceContext,1000,800,1000,400,&cleanup,eye*400,true),
            "natural packed HUD admits independently bounded upper/lower raster");
        NativeSetScissor({-100,displacement,1200,800+displacement});
        auto clipped=ActualScissor();
        Check(clipped.left==0&&clipped.right==1000&&clipped.top>=LONG(eye*400)&&
            clipped.bottom<=LONG((eye+1)*400)&&clipped.top<=clipped.bottom,
            "large native/HUD offsets cannot expand packed scissors into the other eye");
        NativeSetScissor({3,3,3,3});clipped=ActualScissor();
        Check(clipped.left==clipped.right&&clipped.top==clipped.bottom,"packed clipping retains empty scissors");
        HaloCEHudLayout_Suspend();NativeSetScissor({0,-1000,1000,2000});HaloCEHudLayout_Resume();
        clipped=ActualScissor();
        Check(clipped.top==LONG(eye*400)&&clipped.bottom==LONG((eye+1)*400),
            "authored reticle suspension keeps its own packed eye bounds");
        Check(HaloCEHudLayout_EndEyeReplay()&&ActualScissor().top==initialScissor.top&&
            Near(ActualViewport().TopLeftY,initialViewport.TopLeftY),"packed raster restores exact native entry state");
    }
    // User's actual 2912x2100 desktop canvas remains unchanged while the
    // packed output grows to 2912x4200. Execute the production HUD hook and
    // native setter interception at that size for each eye and slider extreme.
    testOwner.camera.viewport={0,0,2100,2912};testOwner.tracking.hud.size=.5f;
    original=&FullResolutionHud;
    for (fullResolutionEyeTop=0;fullResolutionEyeTop<=2100;fullResolutionEyeTop+=2100)
        for (const float height:{-300.0f,0.0f,300.0f})
        {
            testOwner.tracking.hud.verticalOffset=height;
            NativeSetViewport({0,0,2912,2100,0,1});NativeSetScissor({0,0,2912,2100});
            bool restored{};
            Check(HaloCEHudLayout_BeginEyeReplay(testDeviceContext,2912,2100,2912,2100,
                &restored,fullResolutionEyeTop,true),"full-resolution native eye raster admitted");
            MainHook();
            Check(HaloCEHudLayout_EndEyeReplay()&&Near(ActualViewport().Height,2100)&&
                Near(ActualViewport().TopLeftY,0),"full-resolution eye restores exact native canvas");
        }
    testDeviceContext->Release();device->Release();
    return failureCount?1:0;
}
