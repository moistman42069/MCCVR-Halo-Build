// Runs the production CE adapter against native-call fixtures and real WARP
// textures. It never maps executable game code, installs hooks or opens MCC.
#include "../src/dll/haloce_stereo_core.cpp"
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <thread>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;
void ConfigureCeHudLayoutRuntimeFixture(uint32_t generation,bool enabled,bool installed=true);
void RunCeHudGameplayRuntimeFixture(uintptr_t base,void(__fastcall* draw)());
static bool naturalHudFrame{};
static GameTitle testTitle=GameTitle::HaloCE;
static uint32_t testGeneration=3;
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return testGeneration; }
static TitleRuntimeLifecycle publishedLifecycle{};
bool TitleAdapter_PublishLifecycle(GameTitle,uint32_t,const TitleRuntimeLifecycle& value)
{ publishedLifecycle=value; return true; }
bool TitleAdapter_PublishHeartbeat(GameTitle,uint32_t,uint64_t) { return true; }
float Game_GetWorldScale() { return 1.0f/3.048f; }
bool Game_IsPositionalTracking() { return true; }
bool Game_RoomscaleCameraAllowed(GameTitle) { return false; }
uint64_t VR_PhysicalCrouchEpoch() noexcept { return 1; }
float HaloCEControls_PhysicalCrouchCorrection(uint32_t,uint64_t,float,bool) noexcept { return 0; }
bool HaloCEHud_HasCrosshairScope() noexcept { return naturalHudFrame; }
void Roomscale_Camera(GameTitle,bool,const float*,const float*,const float*,const float*,float*,float) noexcept {}
void Logf(const char*,...) { }
bool WaitForNativeDetourQuiescence(const void* const*,const void* const*,size_t count,
    const std::atomic<uint32_t>& value) { return count<=8&&!value.load(); }

namespace
{
extern D3D11_TEXTURE2D_DESC testDesc;
uintptr_t resolutionPool{};
std::array<std::array<uint8_t,0x118>,4> resolutionRoots{};
std::array<std::array<int32_t,2>,4> resolutionSizes{};
std::array<uint8_t,0x40> resolutionConfig{};
std::vector<ComPtr<ID3D11Texture2D>> resolutionGpuChildren;
ID3D11Device* resolutionDevice{};
unsigned resolutionReleases{},resolutionReconfigures{},resolutionManagements{};
bool resolutionFault{};
bool resolutionEntryFault{};
bool resolutionReconfigureFail{},resolutionNested{};
bool resolutionReleaseFault{},resolutionReconfigureFault{},resolutionChangeGeneration{};
uintptr_t resolutionReplacementBackend{};
bool resolutionOmitHudEffect{},resolutionReconfigureSawInitialized{};
unsigned resolutionLegacyReloads{};
void ResolutionLegacyEffects(bool ready)
{
    using namespace contract::anniversary_resolution;
    for (uint32_t rva=resolution_legacy_effects_begin;rva<resolution_legacy_effects_end;rva+=0x20)
    {
        const uintptr_t effect=ready?bindings.base+rva+8:0;
        std::memcpy(reinterpret_cast<void*>(bindings.base+rva),&effect,sizeof(effect));
    }
}
void ResolutionLegacyInitialized(uint32_t initialized)
{
    std::memcpy(reinterpret_cast<void*>(bindings.base+
        contract::anniversary_resolution::resolution_legacy_initialized),&initialized,sizeof(initialized));
}
uint32_t ResolutionLegacyInitialized()
{
    uint32_t initialized{};
    std::memcpy(&initialized,reinterpret_cast<const void*>(bindings.base+
        contract::anniversary_resolution::resolution_legacy_initialized),sizeof(initialized));
    return initialized;
}
std::array<uint64_t,2> resolutionChildOpaque{};
std::array<std::array<uint32_t,2>,2> resolutionInitialChildHeights{};
uintptr_t __fastcall ResolutionEntryNative(uintptr_t pool,uintptr_t,uint64_t,
    int32_t width,int32_t height,uint32_t,uint32_t usage,uintptr_t)
{
    auto& count=*reinterpret_cast<int32_t*>(pool+0xc);
    const unsigned index=static_cast<unsigned>(count);
    if (index>=resolutionRoots.size()) return 0;
    if (!width) std::memcpy(&width,resolutionConfig.data()+0x20,4);
    if (!height) std::memcpy(&height,resolutionConfig.data()+0x24,4);
    resolutionSizes[index]={width,height};
    auto& bytes=resolutionRoots[index];bytes={};
    const auto root=reinterpret_cast<uintptr_t>(bytes.data());
    const int16_t w=static_cast<int16_t>(width),h=static_cast<int16_t>(height);
    std::memcpy(bytes.data()+0x10,&w,2);std::memcpy(bytes.data()+0x12,&h,2);
    const uint32_t flags=usage&0x4000000u?0x160u:0x170u;
    std::memcpy(bytes.data()+0x88,&flags,4);
    std::memcpy(reinterpret_cast<void*>(pool+0x10+size_t(count)*0x38),&root,8);
    if (resolutionEntryFault) RaiseException(0xece01001u,0,0,nullptr);
    if (index<2) for (unsigned eye=0;eye<2;++eye)
    {
        // +20a9b0 calls native +a8 while this slot's usage/count remain
        // unpublished. +1f3e90 creates both children inside that call.
        const auto child=ce_resolution::ChildHook(root,0,width,height/2,0,eye+1);
        D3D11_TEXTURE2D_DESC descriptor{};
        if (child) reinterpret_cast<ID3D11Texture2D*>(child)->GetDesc(&descriptor);
        resolutionInitialChildHeights[index][eye]=descriptor.Height;
    }
    std::memcpy(reinterpret_cast<void*>(pool+0x18+size_t(count)*0x38),&usage,4);
    ++count;
    return root;
}
uintptr_t __fastcall ResolutionInitializeNative(uintptr_t pool)
{
    if (resolutionFault) RaiseException(0xece01001u,0,0,nullptr);
    *reinterpret_cast<int32_t*>(pool+0xc)=0;
    ce_resolution::EntryHook(pool,0,0x800171,0,0,0,1,0);
    ce_resolution::EntryHook(pool,0,0x1000271,0,0,0x35,0x8800001,0);
    ce_resolution::EntryHook(pool,0,0x800161,0,0,0,0x4000001,0);
    ce_resolution::EntryHook(pool,0,0x800161,0,0,0x1b,0x24000001,0);
    return 1;
}
uintptr_t __fastcall ResolutionChildNative(uintptr_t parent,uintptr_t,int32_t width,
    int32_t height,uint64_t opaque,uint64_t selector)
{
    if (resolutionFault) RaiseException(0xece01001u,0,0,nullptr);
    resolutionChildOpaque={opaque,selector};
    auto descriptor=testDesc;descriptor.Width=width;descriptor.Height=height;
    const bool depth=parent==reinterpret_cast<uintptr_t>(resolutionRoots[1].data());
    if (depth) { descriptor.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;descriptor.BindFlags=D3D11_BIND_DEPTH_STENCIL; }
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resolutionDevice->CreateTexture2D(&descriptor,nullptr,&texture))) return 0;
    const auto result=reinterpret_cast<uintptr_t>(texture.Get());
    resolutionGpuChildren.push_back(std::move(texture));
    return result;
}
void __fastcall ResolutionManageNative(uint64_t,uint64_t,uint64_t,uint64_t) { ++resolutionManagements; }
void __fastcall ResolutionReleaseNative()
{
    ++resolutionReleases;ce_resolution::completed.Publish({});
    // Actual pinned +4f1ad0 -> backend+70/+82170 destroys the complete
    // effect table and clears the lifetime gate consumed by backend+60.
    // test_ce_resolution_lifecycle_native.py independently executes it.
    if (ResolutionLegacyInitialized())
    { ResolutionLegacyInitialized(0);ResolutionLegacyEffects(false); }
    if (resolutionNested) ce_resolution::ManageHook(1,2,3,4);
    if (resolutionChangeGeneration) ++testGeneration;
    if (resolutionReplacementBackend)
        std::memcpy(reinterpret_cast<void*>(bindings.base+0x2e3bdd8),&resolutionReplacementBackend,8);
    if (resolutionReleaseFault) RaiseException(0xece01001u,0,0,nullptr);
}
uintptr_t __fastcall ResolutionReconfigureNative(int32_t mode,uint8_t value)
{
    ++resolutionReconfigures;
    resolutionReconfigureSawInitialized=ResolutionLegacyInitialized()==1;
    if (resolutionReconfigureFault) RaiseException(0xece01001u,0,0,nullptr);
    if (mode||value||resolutionReconfigureFail)
    { ResolutionLegacyInitialized(0);ResolutionLegacyEffects(false);return 0; }
    if (resolutionReconfigureSawInitialized)
    {
        ++resolutionLegacyReloads;ResolutionLegacyEffects(true);
        if (resolutionOmitHudEffect)
        {
            const uintptr_t absent{};
            std::memcpy(reinterpret_cast<void*>(bindings.base+
                contract::anniversary_resolution::resolution_legacy_hud_meter),&absent,8);
        }
    }
    return ce_resolution::InitializeHook(resolutionPool);
}
bool ResolutionCaughtManagementFault()
{
    __try { ce_resolution::ManageHook(1,2,3,4); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode()==0xece01001u; }
    return false;
}
bool ResolutionCaughtFault(bool initialize)
{
    __try
    {
        if (initialize) ce_resolution::InitializeHook(resolutionPool);
        else ce_resolution::ChildHook(reinterpret_cast<uintptr_t>(resolutionRoots[0].data()),0,32,8,0,0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode()==0xece01001u; }
    return false;
}
int32_t sceneRequestedRefresh{};
const float* scenePrimary{};
const float* sceneSecondary{};
void __fastcall NativeSceneCamera(uintptr_t scene,int32_t refresh,
    const float* primary,const float* secondary)
{
    sceneRequestedRefresh=refresh; scenePrimary=primary; sceneSecondary=secondary;
    // Model just the documented native previous/current camera-count flag.
    // Actual native static-cache reconstruction is executed by the pinned
    // test_ce_scene_refresh_native.py verifier.
    auto* flags=reinterpret_cast<uint32_t*>(scene+0x110);
    *flags=(*flags&~0x1000u)|(primary&&secondary?0x1000u:0u);
}
ID3D11DeviceContext* testContext{};
ID3D11Texture2D* testSource{};
ID3D11Texture2D* testDestination{};
D3D11_TEXTURE2D_DESC testDesc{};
bool omitRight{};
bool invalidBox{};
bool collideOutputMetadata{};
bool omitDepth{},omitShading{},foreignUploadCamera{},changedUploadCamera{},changedUploadPlayer{};
bool aliasDepthResource{},aliasDepthView{},wrongBoundDepth{},changeDepthAtScene{},omitDepthDraw{};
bool recreateDepthAtScene{},auxiliaryDepthOverwrite{};
bool recyclePreparedSource{};
bool inspectPrimaryDraw{},primaryDrawCorrect{true};
unsigned primaryDrawStages{},primaryDrawOutputs{};
bool nestedMaterialProbe{},nestedMaterialFault{},nestedMaterialRejected{true};
int depthEye{};
uintptr_t depthRoot{},depthBackend{};
std::array<std::array<uint8_t,0x118>,2> depthSurfaces{};
ID3D11DepthStencilView* depthViews[2]{};
ID3D11Resource* depthTextures[2]{};
uintptr_t hudRoot{};
uintptr_t packedHudRoot{};
uintptr_t __fastcall NativeDepthSelect(uintptr_t root)
{ return root==hudRoot||root==packedHudRoot?root:root==depthRoot?reinterpret_cast<uintptr_t>(depthSurfaces[depthEye].data()):0; }
void SelectDepth(int eye)
{
    depthEye=eye;
    const auto resource=reinterpret_cast<uintptr_t>(depthTextures[aliasDepthResource?0:eye]);
    const auto view=reinterpret_cast<uintptr_t>(depthViews[aliasDepthView?0:eye]);
    std::memcpy(depthSurfaces[eye].data()+0xe0,&resource,8);
    std::memcpy(depthSurfaces[eye].data()+0x108,&view,8);
}
void __fastcall NativeDepthDraw(uintptr_t,uintptr_t,uintptr_t,int32_t eye)
{
    if (inspectPrimaryDraw)
    {
        Tracking owner{};
        primaryDrawCorrect&=HaloCE_GetAnniversaryPrimaryEyeTracking(owner)==(eye>=0&&eye<2);
    }
    if (eye==2) eye=0;
    SelectDepth(eye);
    const auto view=reinterpret_cast<uintptr_t>(depthViews[wrongBoundDepth?1-eye:aliasDepthView?0:eye]);
    std::memcpy(reinterpret_cast<void*>(depthBackend+0xd20),&view,8);
    testContext->OMSetRenderTargets(0,nullptr,reinterpret_cast<ID3D11DepthStencilView*>(view));
    testContext->ClearDepthStencilView(reinterpret_cast<ID3D11DepthStencilView*>(view),
        D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,eye?.8f:.2f,static_cast<UINT8>(eye+1));
}
unsigned nativeCopies{};
uintptr_t rendererAddress{};
constexpr uint32_t leftColor=0xff123456,rightColor=0xffabcdef;
void Paint(ID3D11Texture2D* texture,uint32_t color);
constexpr uint32_t hudColor=0xff34ab78;
unsigned hudCallbacks{};
bool hudRasterCorrect{true};
enum class HudFault { None,CallbackException,PreambleException,UnownedStack };
HudFault hudFault{};
void ObserveRaster(const D3D11_VIEWPORT& viewport,const D3D11_RECT& scissor)
{
    D3D11_VIEWPORT transformed{}; D3D11_RECT clipped{};
    const bool mappedView=HaloCEHudLayout_PrepareViewports(testContext,1,&viewport,&transformed);
    const bool mappedScissor=HaloCEHudLayout_PrepareScissors(testContext,1,&scissor,&clipped);
    testContext->RSSetViewports(1,mappedView?&transformed:&viewport);
    testContext->RSSetScissorRects(1,mappedScissor?&clipped:&scissor);
}
void __fastcall NativeHudPush()
{
    auto* depth=reinterpret_cast<int32_t*>(depthBackend+0x10);
    uintptr_t storage{};Read(depthBackend+8,storage);
    std::memcpy(reinterpret_cast<void*>(storage+0x48*(*depth)++),reinterpret_cast<void*>(depthBackend+0x18),0x48);
}
uint8_t __fastcall NativeHudPop()
{
    auto* depth=reinterpret_cast<int32_t*>(depthBackend+0x10);
    uintptr_t storage{};Read(depthBackend+8,storage);
    if (*depth<=0) return 0;
    std::memcpy(reinterpret_cast<void*>(depthBackend+0x18),reinterpret_cast<void*>(storage+0x48*--(*depth)),0x48);
    return 1;
}
void __fastcall NativeHudCallback()
{
    ++hudCallbacks;
    D3D11_VIEWPORT view{};D3D11_RECT rect{};UINT views=1,rects=1;
    testContext->RSGetViewports(&views,&view);testContext->RSGetScissorRects(&rects,&rect);
    hudRasterCorrect&=views==1&&rects==1&&view.Width==testDesc.Width&&view.Height==testDesc.Height&&
        rect.right==LONG(testDesc.Width)&&rect.bottom==LONG(testDesc.Height);
    if (hudFault==HudFault::UnownedStack)
        ++*reinterpret_cast<int32_t*>(depthBackend+0x10);
    if (hudFault==HudFault::CallbackException||hudFault==HudFault::UnownedStack)
        RaiseException(0xe042ce01,0,0,nullptr);
    // Native callback/D3D drawing are explicit fixtures. The actual adapter
    // admission, full/half raster mapping, copy and restoration are exercised.
    Paint(testSource,hudColor);
    ObserveRaster({0,0,float(testDesc.Width),float(testDesc.Height*2),0,1},
        {0,0,LONG(testDesc.Width),LONG(testDesc.Height*2)});
}
void __fastcall NativeHudPreamble()
{
    NativeHudPush();NativeHudPop();
    if (hudFault==HudFault::PreambleException) RaiseException(0xe042ce02,0,0,nullptr);
    AnniversaryHudCallbackBody();
}
unsigned naturalCallbacks{},naturalGameplayCalls{};
bool naturalRevokeSource{},naturalInvalidateRaster{},naturalOmitGameplay{},naturalWrongTarget{};
bool naturalForeignCaller{},naturalUnobservedRaster{},naturalRepeatGameplay{};
bool naturalSwapSelectedSource{},naturalRetireContext{};
bool naturalIncompatibleDepth{};
bool naturalTargetRestoreCorrect{true};
enum class NaturalBindFault { None,BeforeMutation,ZeroDimensions,AfterMutation,Readback,ForeignTarget };
NaturalBindFault naturalBindFault{};
unsigned naturalBindFaultAt=1,naturalBindCalls{};
bool naturalForeignRasterPreserved{true},naturalRevokeDepth{},naturalChangeDepthView{};
bool naturalGameplayFault{};
const D3D11_VIEWPORT foreignHudViewport{5,6,7,8,0,1};
const D3D11_RECT foreignHudScissor{5,6,12,14};
bool naturalFrozen{},naturalNativeReset{},naturalRasterRestored{true};
UINT naturalAuthoredHeight{};
UINT NaturalAuthoredHeight() { return naturalAuthoredHeight?naturalAuthoredHeight:2*testDesc.Height; }
uint64_t expectedNaturalSerial{};
ID3D11RenderTargetView* packedHudView{};
ID3D11VertexShader* naturalHudVs{};
ID3D11PixelShader* naturalHudPs{};
ID3D11RasterizerState* naturalHudRaster{};
ID3D11DepthStencilState* naturalHudDepth{};
D3D11_BOX hudRegions[2]{};
bool __fastcall NaturalHudTargetBind(uintptr_t backend,const void* descriptor)
{
    const bool fault=++naturalBindCalls==naturalBindFaultAt;
    if (fault&&naturalBindFault==NaturalBindFault::BeforeMutation)
        RaiseException(0xe042ce05,0,0,nullptr);
    std::memmove(reinterpret_cast<void*>(backend+0x18),descriptor,0x48);
    // Actual 205E40 publishes the descriptor, then 1DC120 clears dimensions
    // before attachment validation. A fault here retains the old view cache.
    std::memset(reinterpret_cast<void*>(backend+0x50),0,8);
    if (fault&&naturalBindFault==NaturalBindFault::ZeroDimensions)
        RaiseException(0xe042ce05,0,0,nullptr);
    const auto* bytes=reinterpret_cast<const uint8_t*>(backend+0x18);
    uintptr_t color{},depth{};std::memcpy(&color,bytes+0x10,8);std::memcpy(&depth,bytes+0x30,8);
    if (color!=packedHudRoot) return false;
    ID3D11DepthStencilView* dsv{};
    if (depth)
    {
        const auto selected=NativeDepthSelect(depth);
        if (!selected) return false;
        std::memcpy(&dsv,reinterpret_cast<const void*>(selected+0x108),8);
    }
    const uint32_t count=1;std::memcpy(reinterpret_cast<void*>(backend+0xcf8),&count,4);
    std::memcpy(reinterpret_cast<void*>(backend+0xd00),&packedHudView,8);
    std::memcpy(reinterpret_cast<void*>(backend+0xd20),&dsv,8);
    const UINT dimensions[]{testDesc.Width,testDesc.Height*2};
    std::memcpy(reinterpret_cast<void*>(backend+0x50),dimensions,8);
    testContext->OMSetRenderTargets(1,&packedHudView,dsv);
    ObserveRaster({0,0,float(testDesc.Width),float(testDesc.Height*2),0,1},
        {0,0,LONG(testDesc.Width),LONG(testDesc.Height*2)});
    if (fault&&naturalBindFault==NaturalBindFault::AfterMutation)
        RaiseException(0xe042ce05,0,0,nullptr);
    if (fault&&naturalBindFault==NaturalBindFault::Readback)
        std::memset(reinterpret_cast<void*>(backend+0xcf8),0,4);
    if (fault&&naturalBindFault==NaturalBindFault::ForeignTarget)
    {
        *reinterpret_cast<uint8_t*>(backend+0x18)^=0x80;
        ObserveRaster(foreignHudViewport,foreignHudScissor);
    }
    return true;
}
void __fastcall NaturalGameplayHud()
{
    RenderContext owner{};Camera camera{};Read(bindings.base+0x29af2c4,camera);
    naturalFrozen&=HaloCE_GetRenderContext(camera,owner)&&owner.tracking.serial==expectedNaturalSerial;
    if (naturalNativeReset)
        ObserveRaster({0,0,float(testDesc.Width),float(NaturalAuthoredHeight()),0,1},
            {0,0,LONG(testDesc.Width),LONG(NaturalAuthoredHeight())});
    D3D11_VIEWPORT v{};UINT count=1;testContext->RSGetViewports(&count,&v);
    const auto edge=[](float value){return UINT(std::ceil(value-.5f));};
    D3D11_BOX box{edge(v.TopLeftX),edge(v.TopLeftY),0,
        edge(v.TopLeftX+v.Width/4),edge(v.TopLeftY+v.Height/2),1};
    if (naturalGameplayCalls<2) hudRegions[naturalGameplayCalls]=box;
    ++naturalGameplayCalls;
    if (box.right>box.left&&box.bottom>box.top&&box.bottom<=2*testDesc.Height)
    {
        // A real draw must consume the actual bound output, viewport and
        // scissor. UpdateSubresource bypassed every native raster/target bug.
        testContext->VSSetShader(naturalHudVs,nullptr,0);
        testContext->PSSetShader(naturalHudPs,nullptr,0);
        testContext->GSSetShader(nullptr,nullptr,0);
        testContext->IASetInputLayout(nullptr);
        testContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        testContext->RSSetState(naturalHudRaster);
        testContext->OMSetDepthStencilState(naturalHudDepth,0);
        testContext->OMSetBlendState(nullptr,nullptr,~0u);
        testContext->Draw(4,0);
    }
    if (naturalInvalidateRaster) HaloCEHudLayout_InvalidateState(testContext);
    if (naturalRevokeDepth) resources.Forget(reinterpret_cast<uintptr_t>(depthTextures[0]));
    if (naturalChangeDepthView) std::memcpy(depthSurfaces[0].data()+0x108,&depthViews[1],8);
    if (naturalGameplayFault) RaiseException(0xe042ce06,0,0,nullptr);
}
void __fastcall NaturalHudCallback()
{
    ++naturalCallbacks;
    std::memcpy(reinterpret_cast<void*>(packedHudRoot+0xe0),&testDestination,8);
    auto* backend=reinterpret_cast<uint8_t*>(depthBackend);
    std::memset(backend+0x18,0,0x48);
    std::memcpy(backend+0x28,&packedHudRoot,8);
    const UINT dimensions[]{testDesc.Width,testDesc.Height*2};
    std::memcpy(backend+0x50,dimensions,8);
    const uint32_t count=1;std::memcpy(backend+0xcf8,&count,4);
    std::memcpy(backend+0xd00,&packedHudView,8);
    std::memset(backend+0xd08,0,0x20);
    if (naturalIncompatibleDepth)
    {
        SelectDepth(0);
        std::memcpy(backend+0x48,&depthRoot,8);
        std::memcpy(backend+0xd20,&depthViews[0],8);
    }
    testContext->OMSetRenderTargets(1,&packedHudView,naturalIncompatibleDepth?depthViews[0]:nullptr);
    const D3D11_VIEWPORT full{0,0,float(testDesc.Width),float(NaturalAuthoredHeight()),0,1};
    const D3D11_RECT rect{0,0,LONG(testDesc.Width),LONG(NaturalAuthoredHeight())};
    ObserveRaster(full,rect);
    if (naturalWrongTarget) std::memset(backend+0xd00,0,8);
    if (naturalUnobservedRaster) HaloCEHudLayout_InvalidateState(testContext);
    if (!naturalOmitGameplay) RunCeHudGameplayRuntimeFixture(bindings.base,&NaturalGameplayHud);
    if (naturalRepeatGameplay) RunCeHudGameplayRuntimeFixture(bindings.base,&NaturalGameplayHud);
    uintptr_t restoredDepth{};std::memcpy(&restoredDepth,backend+0x48,8);
    ID3D11DepthStencilView* restoredView{};std::memcpy(&restoredView,backend+0xd20,8);
    naturalTargetRestoreCorrect&=restoredDepth==(naturalIncompatibleDepth?depthRoot:0)&&
        restoredView==(naturalIncompatibleDepth?depthViews[0]:nullptr);
    D3D11_VIEWPORT after{};UINT n=1;testContext->RSGetViewports(&n,&after);
    if (naturalBindFault==NaturalBindFault::ForeignTarget)
    {
        D3D11_RECT scissor{};UINT rectangles=1;testContext->RSGetScissorRects(&rectangles,&scissor);
        naturalForeignRasterPreserved&=n==1&&rectangles==1&&
            !std::memcmp(&after,&foreignHudViewport,sizeof(after))&&
            !std::memcmp(&scissor,&foreignHudScissor,sizeof(scissor));
    }
    naturalRasterRestored&=n==1&&!std::memcmp(&after,&full,sizeof(full));
    ObserveRaster(full,rect); // Native callback's own final full-height viewport.
    if (naturalRevokeSource)
    {
        auto packed=testDesc;packed.Height*=2;
        HaloCE_RecordTextureCreated(testDestination,packed);
    }
    // The real native callback's backend+0x28 epilogue calls D3D ClearState
    // and clears the cached output count. It retains the native descriptor.
    testContext->ClearState();HaloCEHudLayout_InvalidateState(testContext);
    std::memset(backend+0xcf8,0,4);std::memset(backend+0xd20,0,8);
    if (naturalSwapSelectedSource)
        std::memcpy(reinterpret_cast<void*>(packedHudRoot+0xe0),&testSource,8);
    if (naturalRetireContext) std::memset(reinterpret_cast<void*>(bindings.base+0x2ea2d30),0,8);
}
void __fastcall NativeHudHookFault() { RaiseException(0xe042ce04,0,0,nullptr); }
bool InvokeActualHudHookFault()
{
    __try { AnniversaryHudCallbackHook(); }
    __except(GetExceptionCode()==0xe042ce04?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
bool InvokeNaturalHudFrameFault()
{
    __try { FrameBody(0,0x10); }
    __except(GetExceptionCode()==0xe042ce06?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
bool InstallFixtureJump(uintptr_t at,uintptr_t destination)
{
    DWORD previous{};
    if (!VirtualProtect(reinterpret_cast<void*>(at),12,PAGE_EXECUTE_READWRITE,&previous)) return false;
    // mov rax, imm64; jmp rax. This is private fixture memory, never a game module.
    const uint8_t entry[]{0x48,0xb8}; const uint8_t tail[]{0xff,0xe0};
    std::memcpy(reinterpret_cast<void*>(at),entry,2);
    std::memcpy(reinterpret_cast<void*>(at+2),&destination,8);
    std::memcpy(reinterpret_cast<void*>(at+10),tail,2);
    return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),12)!=0;
}
using MixedAppendFn=uintptr_t(__fastcall*)(uintptr_t,const SaberCamera*,uint32_t,int32_t,
    float,float,uint32_t,float,uint16_t,uint16_t,uint64_t,uint8_t,float,float,uint8_t,uint8_t);
bool appendArgumentsIntact{};
uintptr_t __fastcall MixedAppend(uintptr_t list,const SaberCamera* source,uint32_t flags,int32_t index,
    float a5,float a6,uint32_t a7,float a8,uint16_t a9,uint16_t a10,
    uint64_t a11,uint8_t a12,float a13,float a14,uint8_t a15,uint8_t a16)
{
    appendArgumentsIntact=list==0x1122334455667788ull&&source==reinterpret_cast<const SaberCamera*>(0x12340)&&
        flags==0x110b&&index==-1&&a5==1.25f&&a6==-2.5f&&a7==0xdeadbeef&&a8==.125f&&
        a9==0x1234&&a10==0xabcd&&a11==0xfedcba9876543210ull&&a12==0xff&&
        a13==-12.5f&&a14==14.75f&&a15==2&&a16==3;
    return 0xabcdfedc12345678ull;
}
void Paint(ID3D11Texture2D* texture,uint32_t color)
{
    std::vector<uint32_t> pixels(testDesc.Width*testDesc.Height,color);
    testContext->UpdateSubresource(texture,0,nullptr,pixels.data(),testDesc.Width*4,0);
}
void STDMETHODCALLTYPE NativeCopy(ID3D11DeviceContext* context,ID3D11Resource* destination,
    UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* source,UINT sourceSub,const D3D11_BOX* box)
{
    ++nativeCopies;
    context->CopySubresourceRegion(destination,sub,x,y,z,source,sourceSub,box);
}
uintptr_t __fastcall NativeTransfer(uintptr_t,SurfaceTransfer* request)
{
    if (collideOutputMetadata&&request->destinationY>0)
    {
        // Both keys had exactly the same 13-bit hash in the previous registry.
        // Native allocation order could therefore evict the right-eye descriptor
        // permanently even though its GPU resource was still alive.
        const auto collision=reinterpret_cast<uintptr_t>(testSource)^uintptr_t{0x20040};
        HaloCE_RecordTextureCreated(reinterpret_cast<ID3D11Texture2D*>(collision),testDesc);
    }
    D3D11_BOX box{0,0,0,testDesc.Width,testDesc.Height,1};
    if (invalidBox) ++box.right;
    CopyBody(testContext,testDestination,0,0,request->destinationY,0,testSource,0,&box,bindings.base+0x204da0);
    return 0x123401;
}
void __fastcall NativeOutput(int eye)
{
    if (inspectPrimaryDraw)
    {
        Tracking owner{};
        primaryDrawCorrect&=!HaloCE_GetAnniversaryPrimaryEyeTracking(owner);
        ++primaryDrawOutputs;
    }
    SurfaceTransfer request{0x111,0x222,0,0,0,0,0,static_cast<int>(eye*testDesc.Height),0,0,
        static_cast<int>(testDesc.Width),static_cast<int>(testDesc.Height)};
    TransferBody(0,&request,bindings.base+0x45e376);
}
void __fastcall NativeCameraUpload(uintptr_t,uintptr_t,const SaberCamera* camera)
{
    if (inspectPrimaryDraw)
    {
        Tracking owner{};
        primaryDrawCorrect&=!HaloCE_GetAnniversaryPrimaryEyeTracking(owner);
    }
    const uintptr_t selected=reinterpret_cast<uintptr_t>(camera);
    std::memcpy(reinterpret_cast<void*>(rendererAddress+0xbe98),&selected,sizeof(selected));
}
void ConsumeCamera(int eye,uintptr_t caller)
{
    SelectDepth(changeDepthAtScene&&caller==0x456a86?1-eye:eye);
    auto* camera=reinterpret_cast<SaberCamera*>(rendererAddress+0xf0+eye*sizeof(SaberView));
    const SaberCamera saved=*camera;
    SaberCamera foreign=*camera;
    if (changedUploadCamera&&eye==1) camera->pose.matrix[12]+=100;
    if (changedUploadPlayer&&eye==1)
    {
        const int32_t otherPlayer=1;
        std::memcpy(reinterpret_cast<uint8_t*>(camera)+0x220,&otherPlayer,4);
    }
    CameraUploadBody(0,0,foreignUploadCamera?&foreign:camera,bindings.base+caller);
    if (inspectPrimaryDraw)
    {
        Tracking owner{};
        primaryDrawCorrect&=HaloCE_GetAnniversaryPrimaryEyeTracking(owner)&&
            owner.serial==frameScope->prepared.receipt.tracking.serial;
        primaryDrawStages|=caller==0x4562bf?1u:caller==0x456a86?2u:4u;
    }
    *camera=saved;
}
Prepared MakePrepared(uint64_t serial,uintptr_t list,PreparationOrigin origin);
void __fastcall NativeFrame(uintptr_t,uint32_t flags)
{
    if (naturalHudFrame) std::memcpy(reinterpret_cast<void*>(depthBackend+0x48),&depthRoot,8);
    if (nestedMaterialProbe)
    {
        Tracking owner{};
        nestedMaterialRejected&=!HaloCE_GetAnniversaryPrimaryEyeTracking(owner);
        if (nestedMaterialFault) RaiseException(0xe042ce03,0,0,nullptr);
        return;
    }
    if (recyclePreparedSource&&frameScope)
    {
        // The native copied-list worker can start preparing the next frame
        // after this render frame has frozen its already-copied receipt.
        // Recycle only its private source, leaving active renderer bytes alone.
        const auto& receipt=frameScope->prepared.receipt;
        MakePrepared(receipt.tracking.serial+1,receipt.ticket.sourceList,receipt.ticket.origin);
    }
    if (!omitDepth) for (int eye=0;eye<2;++eye)
    {
        ConsumeCamera(eye,0x4562bf);
        if (!omitDepthDraw) DepthMeshBody(0,0,0,eye,bindings.base+0x456329);
    }
    if (recreateDepthAtScene)
    {
        D3D11_TEXTURE2D_DESC d{};static_cast<ID3D11Texture2D*>(depthTextures[0])->GetDesc(&d);
        HaloCE_RecordTextureCreated(static_cast<ID3D11Texture2D*>(depthTextures[0]),d);
    }
    if (auxiliaryDepthOverwrite) DepthMeshBody(0,0,0,2,bindings.base+0x456329);
    for (int eye=0;eye<(omitRight?1:2);++eye)
    {
        ConsumeCamera(eye,0x456a86);
        if (!omitShading) ConsumeCamera(eye,0x457c07);
        Paint(testSource,eye?rightColor:leftColor); OutputBody(eye);
    }
    if (naturalHudFrame&&(flags&0x10)) AnniversaryHudCallbackBody(bindings.base+0x4572f5+(naturalForeignCaller?1:0));
}
bool InvokeNestedMaterialFault()
{
    __try { FrameBody(0,0); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
uintptr_t __fastcall NativeReset(uintptr_t list)
{ *reinterpret_cast<SaberViewPair*>(list)={}; return 0xfedcba9876543210ull; }
uint8_t observedBuilderSecondary{};
bool observedBuilderTracking{};
uintptr_t __fastcall NativeBuilder(uintptr_t,SaberViewPair* list,uint8_t secondary,float*)
{
    observedBuilderSecondary=secondary;
    observedBuilderTracking=buildScope!=nullptr;
    *list={};
    return 0xceba1234;
}
bool workerSubmissionProbe{},workerBeforeUnowned{},workerAfterOwned{};
uint64_t workerObservedSerial{};
void __fastcall NativePrepare(uintptr_t job)
{
    Tracking owner{};
    if (workerSubmissionProbe)
        workerBeforeUnowned=!HaloCE_GetAnniversaryPreparedListTracking(rendererAddress+0xb0,owner);
    std::memcpy(reinterpret_cast<void*>(rendererAddress+0xb0),reinterpret_cast<void*>(job+0x70),sizeof(SaberViewPair));
    if (workerSubmissionProbe)
    {
        HaloCE_RecordAnniversaryVisibilitySubmission(rendererAddress+0xb0,1,bindings.base+0x45550b);
        workerAfterOwned=HaloCE_GetAnniversaryPreparedListTracking(rendererAddress+0xb0,owner);
        workerObservedSerial=owner.serial;
    }
}
bool Pixels(ID3D11Device* device,ID3D11Texture2D* texture,uint32_t expected)
{
    auto d=testDesc; d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&d,nullptr,&staging))) return false;
    testContext->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(testContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    bool good=true;
    for (UINT y=0;y<d.Height;++y)
    {
        const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
        for (UINT x=0;x<d.Width;++x) good&=row[x]==expected;
    }
    testContext->Unmap(staging.Get(),0); return good;
}
Prepared MakePrepared(uint64_t serial,uintptr_t list,PreparationOrigin origin)
{
    auto& native=*reinterpret_cast<SaberViewPair*>(list);
    native={}; native.flags=1; native.count=2;
    Tracking tracking{}; tracking.serial=serial; tracking.generation=3; tracking.spaceEpoch=7;
    // This fixture represents active gameplay. The zero-initialized policy
    // deliberately defaults to blocked until a real caller establishes it.
    tracking.controllers.controlsPresentationBlocked=false;
    tracking.headPosition={.1f,1.6f,.2f};
    tracking.eyes[0].offset={-.032f,0,0}; tracking.eyes[1].offset={.032f,0,0};
    Camera stockCamera{};
    stockCamera.position={10,20,30}; stockCamera.forward={1,0,0}; stockCamera.up={0,0,1};
    stockCamera.verticalFov=1.2f; stockCamera.nearPlane=.01f; stockCamera.farPlane=1000;
    stockCamera.viewport=stockCamera.window={0,0,32,32};
    StagedViewPair pair{};
    for (int eye=0;eye<2;++eye)
    {
        native.views[eye].flags=eye?0x20b:0x10b; native.views[eye].viewIndex=eye;
        auto& camera=native.views[eye].camera;
        // Reproduce the test log's full-desktop camera / half-height eye source.
        camera.viewportWidth=static_cast<float>(testDesc.Width); camera.viewportHeight=static_cast<float>(2*testDesc.Height);
        BuildSaberPose(stockCamera,{},0,camera.pose);
        camera.verticalFovDegrees=70; camera.horizontalFovDegrees=100;
        camera.nearPlane=.03f; camera.farPlane=3000;
        tracking.eyes[eye].fov[0]=-.9f; tracking.eyes[eye].fov[1]=.8f;
        tracking.eyes[eye].fov[2]=.85f; tracking.eyes[eye].fov[3]=-.95f;
    }
    SaberViewPair raster{};
    const Reference originReference{tracking.headPosition,tracking.headOrientation,7,3};
    const bool staged=SelectNativeEyeRaster(native,testDesc.Width,testDesc.Height,raster)&&
        StageNativeViewPair(raster,tracking,originReference,Game_GetWorldScale(),true,
            [](SaberCamera&) { return true; },pair)==PairStageResult::Staged;
    if (staged) for (int eye=0;eye<2;++eye) native.views[eye].camera=pair.cameras[eye];
    const auto ticket=handoff.Begin(origin,list,3);
    Prepared result{list,3,true,false,{}};
    result.referenceRevision=referenceRevision.load();
    result.valid=staged&&handoff.Publish(ticket,tracking,pair,native)&&handoff.Read(origin,list,native,3,7,result.receipt);
    return result;
}
}
int main()
{
    int failures=0;
    const auto check=[&](bool value,const char* why) { if (!value) { ++failures; std::fprintf(stderr,"CE runtime: %s\n",why); } };
    hooks[Append].original=reinterpret_cast<void*>(&MixedAppend);
    const auto forwarded=reinterpret_cast<MixedAppendFn>(&AppendHook)(0x1122334455667788ull,
        reinterpret_cast<const SaberCamera*>(0x12340),0x110b,-1,1.25f,-2.5f,0xdeadbeef,.125f,
        0x1234,0xabcd,0xfedcba9876543210ull,0xff,-12.5f,14.75f,2,3);
    check(appendArgumentsIntact&&forwarded==0xabcdfedc12345678ull,
        "production append detour preserves register arguments, mixed-width stack slots and return value");
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL feature=D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&feature,1,D3D11_SDK_VERSION,&device,nullptr,&context))) return 1;
    testContext=context.Get();
    testDesc.Width=32; testDesc.Height=16; testDesc.MipLevels=1; testDesc.ArraySize=1;
    testDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; testDesc.SampleDesc.Count=1;
    testDesc.Usage=D3D11_USAGE_DEFAULT; testDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> source,destination;
    if (FAILED(device->CreateTexture2D(&testDesc,nullptr,&source))||FAILED(device->CreateTexture2D(&testDesc,nullptr,&destination))) return 1;
    testSource=source.Get(); testDestination=destination.Get();
    std::array<uint8_t,0x100> sourceWrapper{},destinationWrapper{};
    std::memcpy(sourceWrapper.data()+0xe0,&testSource,sizeof(testSource));
    std::memcpy(destinationWrapper.data()+0xe0,&testDestination,sizeof(testDestination));
    RecordResource(reinterpret_cast<uintptr_t>(sourceWrapper.data()));
    RecordResource(reinterpret_cast<uintptr_t>(destinationWrapper.data()));
    std::vector<uint8_t> mapped(contract::imageSize),renderer(0xc000),job(0xc000),nativeBackend(0xd28),nativeConfig(0x330);
    bindings.base=reinterpret_cast<uintptr_t>(mapped.data()); bindings.generation=3;
    bindings.surfaceSelector=reinterpret_cast<uintptr_t>(&NativeDepthSelect);
    depthRoot=reinterpret_cast<uintptr_t>(depthSurfaces[0].data());
    depthBackend=reinterpret_cast<uintptr_t>(nativeBackend.data());
    const uintptr_t configAddress=reinterpret_cast<uintptr_t>(nativeConfig.data());
    const uintptr_t backendVtable=bindings.base+0x17f9d10,textureVtable=bindings.base+0x17fb608;
    std::memcpy(mapped.data()+0x2e3bdd8,&configAddress,8);
    std::memcpy(mapped.data()+0x2e3bde0,&depthBackend,8);
    std::memcpy(mapped.data()+0x2ea2d30,&testContext,8);
    std::memcpy(nativeConfig.data()+0x318,&depthRoot,8);
    std::memcpy(nativeBackend.data(),&backendVtable,8);
    std::memcpy(nativeBackend.data()+0xce0,&testContext,8);
    std::memcpy(nativeBackend.data()+0x48,&depthRoot,8);
    ComPtr<ID3D11Texture2D> depthTexture[2];ComPtr<ID3D11DepthStencilView> depthView[2];
    auto depthDesc=testDesc;depthDesc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    for (int eye=0;eye<2;++eye)
    {
        if (FAILED(device->CreateTexture2D(&depthDesc,nullptr,&depthTexture[eye]))||
            FAILED(device->CreateDepthStencilView(depthTexture[eye].Get(),nullptr,&depthView[eye]))) return 1;
        depthTextures[eye]=depthTexture[eye].Get();depthViews[eye]=depthView[eye].Get();
        std::memcpy(depthSurfaces[eye].data(),&textureVtable,8);
        const uint32_t flags=1u<<9;std::memcpy(depthSurfaces[eye].data()+0x88,&flags,4);
        SelectDepth(eye);HaloCE_RecordTextureCreated(depthTexture[eye].Get(),depthDesc);
    }
    *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=1;
    CeObserveRendererMode();
    rendererAddress=reinterpret_cast<uintptr_t>(renderer.data());
    std::memcpy(mapped.data()+0x1bea9e0,&rendererAddress,sizeof(rendererAddress));
    installed=true; active=true; retiring=false; armed=true; generation=3; recenter=false; trackingEnabled=true;
    {
        // Run the production worker publication in both native graphics modes.
        // Haptics must follow the existing armed-camera boundary, with no new
        // grant before readiness or after the camera heartbeat expires.
        for (int32_t mode : {0,1})
        {
            *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=mode;
            firstCameraMs=0;lastCameraMs=0;armed=false;
            lastReport=GetTickCount64();
            (void)HaloCE_Poll(bindings.base,mapped.size(),3,true,true);
            check(!publishedLifecycle.armed&&!(publishedLifecycle.enabledCapabilities&TitleCapability_Haptics),
                "CE haptics stay closed before a fresh camera in either graphics mode");
            const uint64_t now=GetTickCount64();
            firstCameraMs=now-1100;lastCameraMs=now;
            check(HaloCE_Poll(bindings.base,mapped.size(),3,true,true)&&publishedLifecycle.armed&&
                    (publishedLifecycle.enabledCapabilities&TitleCapability_Haptics),
                "armed CE publishes haptics in Original and Anniversary");
            lastCameraMs=now-600;
            (void)HaloCE_Poll(bindings.base,mapped.size(),3,true,true);
            check(!publishedLifecycle.armed&&!(publishedLifecycle.enabledCapabilities&TitleCapability_Haptics),
                "expired CE camera withdraws haptics in either graphics mode");
        }
        firstCameraMs=0;lastCameraMs=0;lastReport=0;
        armed=true;recenter=false;
    }
    {
        std::array<uint8_t,0x3000> pool{};
        resolutionPool=reinterpret_cast<uintptr_t>(pool.data());resolutionDevice=device.Get();
        const uintptr_t config=reinterpret_cast<uintptr_t>(resolutionConfig.data());
        std::memcpy(mapped.data()+0x1bea8a0,&resolutionPool,8);
        std::memcpy(nativeConfig.data()+0x118,&config,8);
        const uintptr_t lifecycleVtable=bindings.base+0x17f0c98;
        const uintptr_t lifecycleReload=bindings.base+contract::anniversary_resolution::resolution_legacy_reload;
        const uintptr_t lifecycleDispose=bindings.base+contract::anniversary_resolution::resolution_legacy_dispose;
        std::memcpy(nativeConfig.data(),&lifecycleVtable,8);
        std::memcpy(reinterpret_cast<void*>(lifecycleVtable+0x60),&lifecycleReload,8);
        std::memcpy(reinterpret_cast<void*>(lifecycleVtable+0x70),&lifecycleDispose,8);
        ResolutionLegacyInitialized(1);ResolutionLegacyEffects(true);
        std::memcpy(resolutionConfig.data()+0x20,&testDesc.Width,4);
        std::memcpy(resolutionConfig.data()+0x24,&testDesc.Height,4);
        ce_resolution::hooks[ce_resolution::Initialize].original=reinterpret_cast<void*>(&ResolutionInitializeNative);
        ce_resolution::hooks[ce_resolution::Entry].original=reinterpret_cast<void*>(&ResolutionEntryNative);
        ce_resolution::hooks[ce_resolution::Child].original=reinterpret_cast<void*>(&ResolutionChildNative);
        ce_resolution::hooks[ce_resolution::Manage].original=reinterpret_cast<void*>(&ResolutionManageNative);
        ce_resolution::release=&ResolutionReleaseNative;ce_resolution::reconfigure=&ResolutionReconfigureNative;
        ce_resolution::enabled=true;ce_resolution::completed.Publish({});
        ce_resolution::legacyLifecycleVerified=true;ce_resolution::legacyReloadPending.Publish({});
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=0;
        check(ce_resolution::InitializeHook(resolutionPool)==1&&resolutionSizes[0][1]==16&&
            resolutionSizes[1][1]==16&&resolutionSizes[2][1]==16&&resolutionSizes[3][1]==16&&
            resolutionInitialChildHeights[0][0]==8&&resolutionInitialChildHeights[1][1]==8,
            "Original allocation leaves native color, depth and packed targets unchanged");
        const auto tracking=MakePrepared(97,rendererAddress+0xb0,PreparationOrigin::ActiveList).receipt.tracking;
        HaloCE_PublishTracking(tracking,true);
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=1;
        resolutionNested=true;ce_resolution::ManageHook(1,2,3,4);resolutionNested=false;
        check(resolutionReleases==1&&resolutionReconfigures==1&&resolutionManagements==2&&
            resolutionSizes[0][1]==16&&resolutionSizes[1][1]==16&&
            resolutionSizes[2][1]==32&&resolutionSizes[3][1]==32&&
            resolutionInitialChildHeights[0][0]==16&&resolutionInitialChildHeights[0][1]==16&&
            resolutionInitialChildHeights[1][0]==16&&resolutionInitialChildHeights[1][1]==16,
            "managed native release/reconfigure expands only Anniversary packed outputs once, including nested callback");
        check(resolutionReconfigureSawInitialized&&resolutionLegacyReloads==1&&
            ResolutionLegacyInitialized()==1&&ce_resolution::LegacyEffectsReady(true),
            "managed reset restores the prior active native lifetime before conditional effect reconstruction");
        ce_resolution::ManageHook(1,2,3,4);
        check(resolutionReleases==1&&resolutionReconfigures==1,
            "steady Anniversary full-resolution pool never reallocates per frame");
        for (unsigned root=0;root<2;++root) for (unsigned eye=0;eye<2;++eye)
        {
            const auto parent=reinterpret_cast<uintptr_t>(resolutionRoots[root].data());
            const auto child=ce_resolution::ChildHook(parent,0,32,8,0x123456789abcdef0ull,eye+1);
            D3D11_TEXTURE2D_DESC desc{};
            if (child) reinterpret_cast<ID3D11Texture2D*>(child)->GetDesc(&desc);
            check(child&&desc.Width==32&&desc.Height==16&&
                desc.BindFlags==(root?D3D11_BIND_DEPTH_STENCIL:D3D11_BIND_RENDER_TARGET)&&
                resolutionChildOpaque[0]==0x123456789abcdef0ull&&resolutionChildOpaque[1]==eye+1,
                "production child detour allocates full native eye color/depth and preserves opaque ABI arguments");
        }
        check(resolutionGpuChildren[0]!=resolutionGpuChildren[1]&&resolutionGpuChildren[2]!=resolutionGpuChildren[3],
            "left/right native full-resolution color and depth allocations remain independent D3D textures");
        const uint32_t oddHeight=17;std::memcpy(resolutionConfig.data()+0x24,&oddHeight,4);
        ce_resolution::ManageHook(1,2,3,4);
        const auto parent=reinterpret_cast<uintptr_t>(resolutionRoots[0].data());
        auto child=ce_resolution::ChildHook(parent,0,32,8,0,0);
        D3D11_TEXTURE2D_DESC desc{};reinterpret_cast<ID3D11Texture2D*>(child)->GetDesc(&desc);
        check(desc.Height==17&&resolutionSizes[2][1]==34&&resolutionReleases==2,
            "native resolution changes rebuild safely and odd eye height keeps its final row");
        std::array<uint8_t,0x118> foreign=resolutionRoots[0];
        child=ce_resolution::ChildHook(reinterpret_cast<uintptr_t>(foreign.data()),0,32,8,0,0);
        reinterpret_cast<ID3D11Texture2D*>(child)->GetDesc(&desc);
        check(desc.Height==8,"unregistered shadow/imported split surfaces retain native dimensions");
        resolutionFault=true;
        check(ResolutionCaughtFault(true)&&ResolutionCaughtFault(false)&&ce_resolution::callbacks.load()==0&&
            !ce_resolution::initializing.address,"native allocation exceptions restore TLS and every callback pin");
        resolutionFault=false;resolutionEntryFault=true;
        check(ResolutionCaughtFault(true)&&ce_resolution::callbacks.load()==0&&
            !ce_resolution::initializing.address&&!ce_resolution::allocating.pool,
            "entry allocation exception revokes the pending native slot and restores callback ownership");
        resolutionEntryFault=false;resolutionReconfigureFail=true;ce_resolution::completed.Publish({});
        const auto beforeFailure=resolutionReleases;
        ce_resolution::ManageHook(1,2,3,4);ce_resolution::ManageHook(1,2,3,4);
        check(resolutionReleases==beforeFailure+1&&ce_resolution::lastFailure.load()==2&&HaloCE_Armed(),
            "failed optional full-resolution rebuild backs off and keeps the camera core armed");
        resolutionReconfigureFail=false;ce_resolution::retryAtMs=0;
        ce_resolution::ManageHook(1,2,3,4);
        ce_resolution::LegacyOwner pending{};
        check(ce_resolution::lastFailure.load()==0&&!ce_resolution::managing&&
            ce_resolution::LegacyEffectsReady(true)&&ce_resolution::legacyReloadPending.Read(pending)&&!pending.base,
            "a later management callback restores the retained native lifetime after failed reconfigure");

        // A driver-success/pool-success result cannot conceal a missing
        // native effect. Retained lifetime intent must override that completed
        // pool receipt on the next management callback.
        resolutionOmitHudEffect=true;ce_resolution::completed.Publish({});
        ce_resolution::retryAtMs=0;HaloCE_PublishTracking(tracking,true);
        ce_resolution::ManageHook(1,2,3,4);
        ce_resolution::Pool completedPool{};
        check(ce_resolution::lastFailure.load()==2&&ResolutionLegacyInitialized()==0&&
            ce_resolution::completed.Read(completedPool)&&completedPool.full&&
            ce_resolution::legacyReloadPending.Read(pending)&&pending.base==bindings.base,
            "missing native effect rejects successful pool rebuild and retains only its original active lifetime");
        const auto beforeEffectRetry=resolutionReleases;
        resolutionOmitHudEffect=false;ce_resolution::retryAtMs=0;
        ce_resolution::ManageHook(1,2,3,4);
        check(resolutionReleases==beforeEffectRetry+1&&ce_resolution::lastFailure.load()==0&&
            ce_resolution::LegacyEffectsReady(true)&&ce_resolution::legacyReloadPending.Read(pending)&&!pending.base,
            "pending effect reconstruction bypasses an otherwise complete pool receipt and recovers");

        // This guard protects the ordinary native HUD fallback even when VR
        // heartbeat admission is absent; it must not use camera arming.
        active=false;armed=false;trackingEnabled=false;
        check(HaloCE_NativeHudResourcesReady(bindings.base,3),"usable native HUD effects remain available while camera is unarmed");
        const uint32_t hudSlot=contract::anniversary_resolution::resolution_legacy_hud_meter;
        const uintptr_t absent{};std::memcpy(mapped.data()+hudSlot,&absent,8);
        check(!HaloCE_NativeHudResourcesReady(bindings.base,3),"missing native HUD effect refuses ordinary fallback without a VR heartbeat");
        ce_resolution::legacyLifecycleVerified=false;
        check(HaloCE_NativeHudResourcesReady(bindings.base,3)&&
            !HaloCE_NativeHudResourcesReady(bindings.base+8,3)&&
            !HaloCE_NativeHudResourcesReady(bindings.base,2),
            "optional proof fallback is allowed only for the exact HUD caller module and generation");
        ce_resolution::legacyLifecycleVerified=true;
        ResolutionLegacyEffects(true);active=true;armed=true;HaloCE_PublishTracking(tracking,true);

        // A previously inactive native owner must never be fabricated by this
        // optional target resize. No retained intent exists in this case.
        ResolutionLegacyInitialized(0);ResolutionLegacyEffects(false);
        ce_resolution::completed.Publish({});ce_resolution::legacyReloadPending.Publish({});
        ce_resolution::retryAtMs=0;
        const auto beforeInactiveReload=resolutionLegacyReloads;
        ce_resolution::ManageHook(1,2,3,4);
        check(!resolutionReconfigureSawInitialized&&ResolutionLegacyInitialized()==0&&
            resolutionLegacyReloads==beforeInactiveReload&&ce_resolution::lastFailure.load()==0&&
            ce_resolution::legacyReloadPending.Read(pending)&&!pending.base,
            "an unrelated prior-zero native renderer remains inactive through target reconfiguration");
        ResolutionLegacyInitialized(1);ResolutionLegacyEffects(true);

        for (int phase=0;phase<2;++phase)
        {
            ce_resolution::completed.Publish({});ce_resolution::retryAtMs=0;
            resolutionReleaseFault=phase==0;resolutionReconfigureFault=phase==1;
            check(ResolutionCaughtManagementFault()&&ce_resolution::callbacks.load()==0&&
                !ce_resolution::managing&&ResolutionLegacyInitialized()==0&&
                ce_resolution::legacyReloadPending.Read(pending)&&pending.base==bindings.base,
                "release and reconfigure SEH unwind ownership while retaining the originally active retry intent");
            resolutionReleaseFault=false;resolutionReconfigureFault=false;ce_resolution::retryAtMs=0;
            ce_resolution::ManageHook(1,2,3,4);
            check(ce_resolution::lastFailure.load()==0&&ce_resolution::LegacyEffectsReady(true),
                "the next management callback reconstructs native effects after isolated SEH fixture failure");
        }

        ce_resolution::completed.Publish({});ce_resolution::retryAtMs=0;
        resolutionChangeGeneration=true;
        const auto beforeStaleGeneration=resolutionReconfigures;
        ce_resolution::ManageHook(1,2,3,4);resolutionChangeGeneration=false;
        check(resolutionReconfigures==beforeStaleGeneration&&ResolutionLegacyInitialized()==0&&
            ce_resolution::lastFailure.load()==4&&!ce_resolution::managing,
            "a title generation change during native release forbids the old lifetime write and reconfigure");
        --testGeneration;ce_resolution::retryAtMs=0;
        ce_resolution::ManageHook(1,2,3,4);
        check(ce_resolution::LegacyEffectsReady(true),"restored fixture generation can recover its own retained lifetime");

        std::array<uint8_t,0x330> replacementBackend{};
        std::memcpy(replacementBackend.data(),&lifecycleVtable,8);
        std::memcpy(replacementBackend.data()+0x118,&config,8);
        resolutionReplacementBackend=reinterpret_cast<uintptr_t>(replacementBackend.data());
        ce_resolution::completed.Publish({});ce_resolution::retryAtMs=0;
        const auto beforeStaleBackend=resolutionReconfigures;
        ce_resolution::ManageHook(1,2,3,4);resolutionReplacementBackend=0;
        check(resolutionReconfigures==beforeStaleBackend&&ResolutionLegacyInitialized()==0&&
            ce_resolution::lastFailure.load()==4,
            "backend replacement during release cannot receive the departed owner's initialization intent");
        ce_resolution::retryAtMs=0;ce_resolution::ManageHook(1,2,3,4);
        check(!resolutionReconfigureSawInitialized&&ResolutionLegacyInitialized()==0&&
            ce_resolution::legacyReloadPending.Read(pending)&&!pending.base,
            "a new prior-zero backend discards the stale owner's retry token instead of inheriting it");
        std::memcpy(mapped.data()+0x2e3bdd8,&configAddress,8);
        ResolutionLegacyInitialized(1);ResolutionLegacyEffects(true);
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=0;
        ce_resolution::InitializeHook(resolutionPool);
        child=ce_resolution::ChildHook(parent,0,32,8,0,0);
        reinterpret_cast<ID3D11Texture2D*>(child)->GetDesc(&desc);
        check(desc.Height==8&&resolutionSizes[2][1]==17,"Original native reinitialization restores stock split/output dimensions");
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=1;
        ce_resolution::enabled=false;ce_resolution::completed.Publish({});ce_resolution::hooks={};
        ce_resolution::legacyLifecycleVerified=false;ce_resolution::legacyReloadPending.Publish({});
        ce_resolution::release=nullptr;ce_resolution::reconfigure=nullptr;
        resolutionGpuChildren.clear();resolutionDevice=nullptr;
        std::memset(mapped.data()+0x1bea8a0,0,8);std::memset(nativeConfig.data()+0x118,0,8);
    }
    hudTargetBindingsVerified=true;
    check(HaloCE_HudTargetBindingsVerified(bindings.base,bindings.size,3),
        "cold HUD target proof is available only through the current retained core");
    check(!HaloCE_HudTargetBindingsVerified(bindings.base+1,bindings.size,3)&&
        !HaloCE_HudTargetBindingsVerified(bindings.base,bindings.size-1,3)&&
        !HaloCE_HudTargetBindingsVerified(bindings.base,bindings.size,4),
        "HUD target proof cannot cross module, size or generation boundaries");
    retiring=true;
    check(!HaloCE_HudTargetBindingsVerified(bindings.base,bindings.size,3),
        "core retirement immediately revokes the borrowed HUD target proof");
    retiring=false;hudTargetBindingsVerified=false;
    check(!HaloCE_HudTargetBindingsVerified(bindings.base,bindings.size,3)&&HaloCE_Armed(),
        "failed optional target proof leaves the working camera core armed");
    {
        std::array<uint8_t,0x118> nativeScene{};
        const uintptr_t scene=reinterpret_cast<uintptr_t>(nativeScene.data());
        const uintptr_t sceneVtable=bindings.base+0x1819658;
        std::memcpy(nativeScene.data(),&sceneVtable,8);
        auto* sceneFlags=reinterpret_cast<uint32_t*>(nativeScene.data()+0x110);
        *sceneFlags=0xa8000042u;
        const auto savedScope=jobScope;
        jobScope={reinterpret_cast<uintptr_t>(job.data()),rendererAddress+0xb0,true};
        hooks[SceneCamera].original=reinterpret_cast<void*>(&NativeSceneCamera);
        const auto primary=reinterpret_cast<const float*>(jobScope.activeList+0x70);
        const auto secondary=reinterpret_cast<const float*>(jobScope.activeList+0x438);
        sceneVisibilityRefreshes=0;
        SceneCameraHook(scene,0,primary,secondary);
        check(sceneRequestedRefresh==1&&sceneVisibilityRefreshes==1&&
            scenePrimary==primary&&sceneSecondary==secondary&&*sceneFlags==0xa8001042u,
            "two-eye entry requests native static-scene refresh and preserves camera pointers/other flags");
        SceneCameraHook(scene,0,primary,secondary);
        check(sceneRequestedRefresh==0&&sceneVisibilityRefreshes==1,
            "steady stereo does not rebuild the static scene every frame");
        SceneCameraHook(scene,7,primary,secondary);
        check(sceneRequestedRefresh==7&&sceneVisibilityRefreshes==1,
            "authored native refresh requests remain unchanged");
        armed=false; retiring=true;
        SceneCameraHook(scene,0,primary,nullptr);
        check(sceneRequestedRefresh==1&&sceneVisibilityRefreshes==2&&*sceneFlags==0xa8000042u,
            "stock return during retirement refreshes mono masks without depending on XR arming");
        armed=true; retiring=false;
        const auto copiedPrimary=reinterpret_cast<const float*>(jobScope.job+0xe0);
        const auto copiedSecondary=reinterpret_cast<const float*>(jobScope.job+0x4a8);
        SceneCameraHook(scene,0,copiedPrimary,copiedSecondary);
        check(sceneRequestedRefresh==1&&sceneVisibilityRefreshes==3,
            "copied worker preparation requests the same native transition refresh");
        jobScope.owned=false;
        SceneCameraHook(scene,0,copiedPrimary,nullptr);
        check(sceneRequestedRefresh==0&&sceneVisibilityRefreshes==3,
            "unowned callbacks preserve stock refresh choice");
        jobScope.owned=true;
        SceneCameraHook(scene,0,primary+1,secondary);
        check(sceneRequestedRefresh==0&&sceneVisibilityRefreshes==3,
            "foreign primary-position pointers cannot grant transition ownership");
        *sceneFlags&=~0x1000u;
        SceneCameraHook(scene,0,primary,secondary+1);
        check(sceneRequestedRefresh==0&&sceneVisibilityRefreshes==3,
            "foreign secondary-position pointers cannot grant transition ownership");
        *sceneFlags&=~0x1000u;
        nativeScene[0]^=0x08;
        SceneCameraHook(scene,0,primary,secondary);
        check(sceneRequestedRefresh==0&&sceneVisibilityRefreshes==3,
            "another native scene class cannot inherit the verified layout");
        nativeScene[0]^=0x08;
        *sceneFlags&=~0x1000u;
        SceneCameraHook(scene,0,primary,secondary);
        check(sceneRequestedRefresh==1&&sceneVisibilityRefreshes==4,
            "the next owned transition recovers without stale adapter bookkeeping");
        jobScope=savedScope;
    }
    preparedLists[0].Publish({}); preparedLists[1].Publish({}); renderReady.Publish({});
    hooks[Copy].original=reinterpret_cast<void*>(&NativeCopy);
    hooks[Transfer].original=reinterpret_cast<void*>(&NativeTransfer);
    hooks[Output].original=reinterpret_cast<void*>(&NativeOutput);
    hooks[Frame].original=reinterpret_cast<void*>(&NativeFrame);
    hooks[CameraUpload].original=reinterpret_cast<void*>(&NativeCameraUpload);
    hooks[DepthMesh].original=reinterpret_cast<void*>(&NativeDepthDraw);
    hooks[Prepare].original=reinterpret_cast<void*>(&NativePrepare);
    hooks[ResetList].original=reinterpret_cast<void*>(&NativeReset);
    auto bootstrap=MakePrepared(99,rendererAddress+0xb0,PreparationOrigin::ActiveList);
    bootstrap.valid=false; // native two-view bootstrap has no compatible cache yet
    preparedLists[0].Publish(bootstrap); renderReady.Publish(bootstrap);
    FrameBody(0,0);
    HaloCE_PresentResources(device.Get(),context.Get());
    Wanted selectedRaster{};
    check(allocated.Read(selectedRaster)&&selectedRaster.generation==3&&
        selectedRaster.descriptor.Width==testDesc.Width&&selectedRaster.descriptor.Height==testDesc.Height,
        "native bootstrap copies discover the actual raster and cold Present prepares compatible eye caches");
    nativeCopies=0; previewFolded=0;
    const auto publish=[&](uint64_t serial) {
        const auto p=MakePrepared(serial,rendererAddress+0xb0,PreparationOrigin::ActiveList);
        check(p.valid,"fixture uses the production receipt ledger"); preparedLists[0].Publish(p); renderReady.Publish(p);
    };
    publish(100); FrameBody(0,0);
    FrameDiagnostic initialDepth{};frameDiagnostic.Read(initialDepth);
    if (initialDepth.failure!=FrameFailure::None)
        std::fprintf(stderr,"initial frame failure=%u depth=%u mask=%u\n",unsigned(initialDepth.failure),initialDepth.depthFailure,initialDepth.depthMask);
    EyeCache::Completed pair{};
    const auto noCurrentPair=[&](uint64_t serial) {
        EyeCache::Completed retained{};
        if (!HaloCE_AcquirePair(context.Get(),serial,7,retained)) return true;
        const bool valid=retained.key.serial<serial&&retained.tracking.serial==retained.key.serial;
        HaloCE_ReleasePair(retained.borrowId);
        return valid;
    };
    check(HaloCE_AcquirePair(context.Get(),101,7,pair),"native frame/output/transfer scopes produce a submitted pair with its older prepared pose");
    if (pair.borrowId)
    {
        Paint(testSource,0xff000000);
        check(Pixels(device.Get(),pair.eyes[0],leftColor)&&Pixels(device.Get(),pair.eyes[1],rightColor),"production capture preserves both eyes after source recycling");
        check(Pixels(device.Get(),testDestination,rightColor),"undersized packed destination gets a bounded last-eye desktop preview");
        check(pair.tracking.serial==100&&pair.tracking.headPosition.y==1.6f,"exact preparation pose survives native copy handoff");
        HaloCE_ReleasePair(pair.borrowId); pair={};
    }
    check(previewFolded.load()==1&&nativeCopies==2,"second-eye packing guard ran exactly once");
    collideOutputMetadata=true;publish(101);FrameBody(0,0);collideOutputMetadata=false;
    check(HaloCE_AcquirePair(context.Get(),101,7,pair),
        "unrelated native allocation sharing an eye-resource hash cannot black out the right eye");
    if (pair.borrowId)
    {
        check(Pixels(device.Get(),pair.eyes[0],leftColor)&&Pixels(device.Get(),pair.eyes[1],rightColor),
            "colliding metadata retains distinct complete eye pixels");
        HaloCE_ReleasePair(pair.borrowId);pair={};
    }
    resources.Forget(reinterpret_cast<uintptr_t>(testSource)^uintptr_t{0x20040});
    omitRight=true; publish(102); FrameBody(0,0);
    check(noCurrentPair(102),"partial native frame never submits its incomplete pixels");
    check(HaloCE_AcquirePair(context.Get(),102,7,pair)&&pair.tracking.serial==101&&
        Pixels(device.Get(),pair.eyes[0],leftColor)&&Pixels(device.Get(),pair.eyes[1],rightColor),
        "rejected successor keeps both last-good eye images paired with their original tracking");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId);pair={}; }
    check(!HaloCE_AcquirePair(context.Get(),110,7,pair),
        "retained pair expires after eight tracking frames and cannot conceal a persistent render failure");
    omitRight=false; publish(103); FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),103,7,pair),"frame after a partial failure recovers");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    publish(104); invalidBox=true; const auto before=nativeCopies; FrameBody(0,0); invalidBox=false;
    check(nativeCopies==before&&noCurrentPair(104),"invalid native rectangle performs no unsafe GPU operation and never publishes that pair");
    publish(105); RevokeWrappedResource(reinterpret_cast<uintptr_t>(sourceWrapper.data())); FrameBody(0,0);
    check(noCurrentPair(105),"released resource identity cannot lend a stale descriptor");
    RecordResource(reinterpret_cast<uintptr_t>(sourceWrapper.data()));
    publish(106); FrameBody(0,0);
    testTitle=GameTitle::Halo3;
    check(!HaloCE_AcquirePair(context.Get(),106,7,pair),"foreign title cannot borrow CE images");
    testTitle=GameTitle::HaloCE;
    check(!HaloCE_AcquirePair(context.Get(),106,8,pair),"new tracking space cannot borrow old images");
    const auto jobAddress=reinterpret_cast<uintptr_t>(job.data());
    *reinterpret_cast<int*>(job.data()+0xbe58)=1; *reinterpret_cast<int*>(job.data()+0xbe5c)=1;
    const auto copied=MakePrepared(107,jobAddress+0x70,PreparationOrigin::CopiedList);
    reinterpret_cast<SaberViewPair*>(jobAddress+0x70)->count=4;
    preparedLists[1].Publish(copied); PrepareBody(jobAddress);
    Prepared frozen{};
    check(renderReady.Read(frozen)&&frozen.valid&&frozen.receipt.tracking.serial==107&&
        frozen.sourceList==jobAddress+0x70,"copied-list receipt survives native auxiliary culling views");
    FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),107,7,pair),"copied native preparation reaches the real GPU pair");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    publish(108); FrameBody(0,0);
    HaloCE_Recenter();
    recenter=false; // the next builder has consumed the request
    check(!HaloCE_AcquirePair(context.Get(),108,7,pair),"recenter revokes a completed pair even in the same XR space");
    FrameBody(0,0);
    check(!HaloCE_AcquirePair(context.Get(),109,7,pair),"queued preparation from before recenter cannot regain admission");
    publish(110); FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),110,7,pair),"new reference recovers on the next prepared frame");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    publish(111);
    Prepared resized{}; renderReady.Read(resized);
    resized.receipt.pair.cameras[0].viewportWidth+=1;
    reinterpret_cast<SaberViewPair*>(rendererAddress+0xb0)->views[0].camera.viewportWidth+=1;
    renderReady.Publish(resized); FrameBody(0,0);
    check(noCurrentPair(111),"source dimensions must match the raster whose FOV will be submitted");
    publish(112); FrameBody(0,0);
    CompletedFrame aged{}; completedFrame.Read(aged); aged.capturedAtMs=GetTickCount64()-300; completedFrame.Publish(aged);
    check(!HaloCE_AcquirePair(context.Get(),112,7,pair),"elapsed time rejects old pixels even when the XR serial stops");
    publish(113);
    reinterpret_cast<SaberViewPair*>(rendererAddress+0xb0)->count=3;
    FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),113,7,pair),
        "native auxiliary culling views must not black out both primary eye images");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    FrameDiagnostic diagnostic{};
    check(frameDiagnostic.Read(diagnostic)&&diagnostic.failure==FrameFailure::None&&
        diagnostic.nativeCount==3&&diagnostic.eyeMask==3&&diagnostic.cameraHeight==testDesc.Height&&
        diagnostic.sourceHeight==testDesc.Height,"worker diagnostics report matched rasters and both actual GPU copies");
    publish(114); omitRight=true; FrameBody(0,0); omitRight=false;
    check(frameDiagnostic.Read(diagnostic)&&diagnostic.failure==FrameFailure::IncompletePair&&diagnostic.eyeMask==1,
        "missing eye is distinguished from camera receipt and raster rejection");
    publish(115); reinterpret_cast<SaberViewPair*>(rendererAddress+0xb0)->views[1].camera.pose.matrix[12]+=100;
    FrameBody(0,0);
    check(frameDiagnostic.Read(diagnostic)&&diagnostic.failure==FrameFailure::CameraChanged&&
        diagnostic.cameraDifference>=sizeof(SaberCamera)&&diagnostic.eyeMask==0,
        "a displaced right camera is rejected and identified before any GPU capture");
    publish(116); omitDepth=true; FrameBody(0,0); omitDepth=false;
    check(noCurrentPair(116)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.failure==FrameFailure::DepthResource&&diagnostic.depthFailure==5,
        "prepared cameras and two GPU colors cannot prove a missing native depth-camera consumption");
    publish(117); omitShading=true; FrameBody(0,0); omitShading=false;
    check(noCurrentPair(117)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.consumerFailure==5,"missing native shading upload rejects the current pair only");
    publish(118); foreignUploadCamera=true; FrameBody(0,0); foreignUploadCamera=false;
    check(noCurrentPair(118)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.consumerFailure==1,"identical bytes at an unrelated camera address cannot claim a native primary eye");
    publish(119); changedUploadCamera=true; FrameBody(0,0); changedUploadCamera=false;
    check(noCurrentPair(119)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.consumerFailure==2,"camera changes after frame entry are rejected at the actual render consumer");
    publish(120); changedUploadPlayer=true; FrameBody(0,0); changedUploadPlayer=false;
    check(noCurrentPair(120)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.consumerFailure==3,"camera source-player changes outside the pose prefix cannot claim the right eye");
    publish(121); FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),121,7,pair)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.consumedDepth==3&&diagnostic.consumedScene==3&&diagnostic.consumedShading==3&&
        diagnostic.consumedCamera[0]==rendererAddress+0xf0&&
        diagnostic.consumedCamera[1]==rendererAddress+0xf0+sizeof(SaberView)&&
        diagnostic.sourceResource[0]==reinterpret_cast<uintptr_t>(testSource)&&
        diagnostic.sourceResource[1]==reinterpret_cast<uintptr_t>(testSource)&&
        diagnostic.copyContext[0]==reinterpret_cast<uintptr_t>(testContext),
        "a new frame recovers and reports actual consumed cameras and recycled per-eye source identity");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    aliasDepthResource=true;publish(122);FrameBody(0,0);aliasDepthResource=false;
    check(noCurrentPair(122)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==4&&diagnostic.depthResource[0]==diagnostic.depthResource[1]&&
        diagnostic.depthView[0]!=diagnostic.depthView[1],
        "different DSV identities cannot admit two eyes using the same depth texture");
    aliasDepthView=true;publish(123);FrameBody(0,0);aliasDepthView=false;
    check(noCurrentPair(123)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==4,"aliased depth views reject a manufactured pair");
    wrongBoundDepth=true;publish(124);FrameBody(0,0);wrongBoundDepth=false;
    check(noCurrentPair(124)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==1,"native depth draw must bind the selected native depth view");
    changeDepthAtScene=true;publish(125);FrameBody(0,0);changeDepthAtScene=false;
    check(noCurrentPair(125)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==5,"scene cannot silently select the opposite eye depth");
    omitDepthDraw=true;publish(126);FrameBody(0,0);omitDepthDraw=false;
    check(noCurrentPair(126)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==5,"camera uploads alone do not establish completed native depth draws");
    publish(127);FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),127,7,pair)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthMask==3&&diagnostic.depthFailure==0,
        "independent current depth restores frame submission after each rejected pair");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    recreateDepthAtScene=true;publish(128);FrameBody(0,0);recreateDepthAtScene=false;
    check(noCurrentPair(128)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==5,"reused texture pointers cannot borrow depth from an earlier resource lifetime");
    auxiliaryDepthOverwrite=true;publish(129);FrameBody(0,0);auxiliaryDepthOverwrite=false;
    check(noCurrentPair(129)&&frameDiagnostic.Read(diagnostic)&&
        diagnostic.depthFailure==6,"an auxiliary depth draw cannot overwrite a completed primary depth");
    publish(130);FrameBody(0,0);
    check(HaloCE_AcquirePair(context.Get(),130,7,pair),"new depth lifetime and clean native frame recover without disarming");
    if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId); pair={}; }
    {
        FrameScope post{};
        post.synthetic=post.capture=true;post.renderer=rendererAddress;
        post.prepared=MakePrepared(131,rendererAddress+0xb0,PreparationOrigin::ActiveList);
        post.diagnostic.consumedDepth=post.diagnostic.consumedScene=post.diagnostic.consumedShading=3;
        auto current=post.prepared.receipt.tracking;current.serial=133;current.motionBlur=true;
        trackingSnapshot.Publish(current);trackingAtMs.store(GetTickCount64());
        frameScope=&post;
        Tracking borrowed{};
        for (int eye=0;eye<2;++eye)
        {
            auto* camera=reinterpret_cast<SaberCamera*>(rendererAddress+0xf0+eye*sizeof(SaberView));
            post.lastSceneEye=eye;
            post.primaryDrawEye=eye;post.primaryDrawStage=3;
            post.diagnostic.consumedCamera[eye]=reinterpret_cast<uintptr_t>(camera);
            NativeCameraUpload(0,0,camera);
            check(HaloCE_GetAnniversaryEyeTracking(camera,borrowed)&&borrowed.serial==131&&!borrowed.motionBlur,
                "post effects borrow each current primary's frozen preparation settings rather than newer XR input");
            check(HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed)&&borrowed.serial==131,
                "material lens uses the same frozen primary camera settings");
            auto foreign=*camera;
            check(!HaloCE_GetAnniversaryEyeTracking(&foreign,borrowed)&&!borrowed.serial,
                "identical copied camera bytes cannot lend post-effect ownership");
            const auto saved=*camera;camera->pose.matrix[12]+=1;
            check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post effects reject camera mutation after shading");
            check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects changed native camera bytes");
            *camera=saved;
        }
        auto* camera=reinterpret_cast<SaberCamera*>(rendererAddress+0xf0+sizeof(SaberView));
        const auto selected=reinterpret_cast<SaberCamera*>(rendererAddress+0xf0);
        NativeCameraUpload(0,0,selected);
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"a later selected camera revokes current-scene post ownership");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects a changed native selected camera");
        NativeCameraUpload(0,0,camera);
        post.diagnostic.consumedShading=1;
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post effects require completed shading consumption");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"shading material scope requires its own consumed camera receipt");
        post.diagnostic.consumedShading=3;
        post.capture=false;
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"dropped pairs cannot lend post-effect ownership");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"dropped pairs cannot lend material lens ownership");
        post.capture=true;current.serial=140;trackingSnapshot.Publish(current);
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post effects reject an old preparation beyond the XR serial window");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects stale tracking serials");
        current.serial=133;trackingSnapshot.Publish(current);trackingAtMs.store(GetTickCount64()-300);
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post effects reject expired XR publication");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects expired tracking publication");
        trackingAtMs.store(GetTickCount64());current.spaceEpoch=8;trackingSnapshot.Publish(current);
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post effects reject a different XR reference space");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects changed XR space");
        current.spaceEpoch=7;trackingSnapshot.Publish(current);
        ++post.prepared.referenceRevision;
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post effects reject a changed tracking reference");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects changed reference revision");
        --post.prepared.referenceRevision;
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=0;
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"Original renderer cannot borrow an Anniversary post scope");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"Original renderer cannot borrow Anniversary material ownership");
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=1;
        check(HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"current post-effect ownership recovers after rejected observations");
        nestedMaterialProbe=true;
        FrameBody(0,0);
        check(nestedMaterialRejected&&!anniversaryMaterialMasked&&HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),
            "nested frames cannot borrow an outer material eye before their first camera upload");
        nestedMaterialFault=true;
        check(InvokeNestedMaterialFault()&&nestedMaterialRejected&&!anniversaryMaterialMasked&&
            HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),
            "native nested-frame unwind restores outer material scope without lending it to the nested frame");
        nestedMaterialProbe=nestedMaterialFault=false;
        ++testGeneration;
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material lens rejects a retired title generation");
        --testGeneration;
        post.diagnostic.nativeCount=3;
        CameraUploadBody(0,0,camera,bindings.base+0x1234);
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),
            "unknown native camera uploads revoke the preceding primary material scope");
        CameraUploadBody(0,0,camera,bindings.base+0x4562bf);
        check(HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),
            "a verified depth upload admits material projection before scene or shading");
        auto* auxiliary=reinterpret_cast<SaberCamera*>(rendererAddress+0xf0+2*sizeof(SaberView));
        CameraUploadBody(0,0,auxiliary,bindings.base+0x4562bf);
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),
            "auxiliary and reflection cameras cannot lend material projection ownership");
        frameScope=nullptr;
        check(!HaloCE_GetAnniversaryEyeTracking(camera,borrowed),"post-effect ownership ends with the native frame scope");
        check(!HaloCE_GetAnniversaryPrimaryEyeTracking(borrowed),"material ownership ends with the native frame scope");
    }
    {
        publish(132);Prepared prepared{};renderReady.Read(prepared);
        HaloCE_PublishTracking(prepared.receipt.tracking,true);recenter=false;
        inspectPrimaryDraw=true;FrameBody(0,0);inspectPrimaryDraw=false;
        Tracking ended{};
        check(primaryDrawCorrect&&primaryDrawStages==7&&primaryDrawOutputs==2&&
            !HaloCE_GetAnniversaryPrimaryEyeTracking(ended),
            "both eyes own material projection at depth/scene/shading, never during upload/output or after frame");
        check(HaloCE_AcquirePair(context.Get(),132,7,pair),"material stage observations preserve both world captures");
        if (pair.borrowId) { HaloCE_ReleasePair(pair.borrowId);pair={}; }
    }
    {
        publish(133);Prepared p{};renderReady.Read(p);HaloCE_PublishTracking(p.receipt.tracking,true);
        recenter=false;Tracking owner{};const uintptr_t list=rendererAddress+0xb0;
        check(!frameScope&&HaloCE_GetAnniversaryPreparedListTracking(list,owner)&&owner.serial==133,
            "native material worker borrows exact pre-culling list before any drawing-eye scope");
        SaberViewPair foreign{};Read(list,foreign);
        check(!HaloCE_GetAnniversaryPreparedListTracking(reinterpret_cast<uintptr_t>(&foreign),owner)&&!owner.serial,
            "identical camera bytes at a foreign list never grant worker ownership");
        auto* player=reinterpret_cast<int32_t*>(list+0x10+sizeof(SaberView)+0x30+0x220);
        *player=1;
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"true second native player is excluded from VR source-player remapping");
        *player=0;referenceRevision.fetch_add(1);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"recenter revokes pending worker list receipts");
        referenceRevision.fetch_sub(1);
        check(HaloCE_GetAnniversaryPreparedListTracking(list,owner),"restored matching list/reference recovers before culling");
        handoff.Invalidate(PreparationOrigin::ActiveList);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"recycled native list ticket cannot inherit previous worker ownership");
        const auto copied=MakePrepared(133,jobAddress+0x70,PreparationOrigin::CopiedList);
        preparedLists[1].Publish(copied);copiedWorkerList.Publish({});
        check(HaloCE_GetAnniversaryPreparedListTracking(jobAddress+0x70,owner),"copied job source has its own exact worker receipt");
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),
            "stationary old renderer cannot borrow a matching new copied-source receipt before the native handoff");
        const auto previousJob=jobScope;jobScope={jobAddress,list,true,true};
        activeListAddress=list;
        HaloCE_RecordAnniversaryVisibilitySubmission(list,0,bindings.base+0x45550b);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"phase zero cannot publish a copied destination");
        HaloCE_RecordAnniversaryVisibilitySubmission(list,1,bindings.base+0x455378);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"other native submission caller cannot publish a copy");
        HaloCE_RecordAnniversaryVisibilitySubmission(list,1,bindings.base+0x45550b);
        check(HaloCE_GetAnniversaryPreparedListTracking(list,owner)&&owner.serial==133,
            "exact post-copy phase-one submission publishes the source ticket before native worker execution");
        *reinterpret_cast<int32_t*>(jobAddress+0xbe5c)=0;
        HaloCE_RecordAnniversaryVisibilitySubmission(list,1,bindings.base+0x45550b);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),
            "failed new copy publication revokes an otherwise matching previous destination");
        *reinterpret_cast<int32_t*>(jobAddress+0xbe5c)=1;
        HaloCE_RecordAnniversaryVisibilitySubmission(list,1,bindings.base+0x45550b);
        check(HaloCE_GetAnniversaryPreparedListTracking(list,owner),"fresh valid submission recovers after failed copy publication");
        RevokeCopiedWorkerList();
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),
            "atomic revocation defeats a retained matching snapshot without depending on snapshot publication");
        HaloCE_RecordAnniversaryVisibilitySubmission(list,1,bindings.base+0x45550b);
        auto blocked=copied.receipt.tracking;blocked.controllers.controlsPresentationBlocked=true;
        HaloCE_PublishTracking(blocked,true);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"newly blocked presentation revokes older unblocked worker tracking");
        HaloCE_PublishTracking(copied.receipt.tracking,true);
        check(HaloCE_GetAnniversaryPreparedListTracking(list,owner),"resumed fresh presentation recovers current worker list");
        SaberViewPair beforeReset{};Read(list,beforeReset);ResetListHook(list);
        std::memcpy(reinterpret_cast<void*>(list),&beforeReset,sizeof(beforeReset));
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),
            "destination reset revokes copied ownership even when identical stationary camera bytes return");
        HaloCE_RecordAnniversaryVisibilitySubmission(list,1,bindings.base+0x45550b);
        check(HaloCE_GetAnniversaryPreparedListTracking(list,owner),"fresh native copy publication recovers after destination reset");
        handoff.Invalidate(PreparationOrigin::CopiedList);
        check(!HaloCE_GetAnniversaryPreparedListTracking(list,owner),"reused copied source revokes its worker destination marker");
        jobScope=previousJob;copiedWorkerList.Publish({});
        const auto oldActive=MakePrepared(133,list,PreparationOrigin::ActiveList);
        preparedLists[0].Publish(oldActive);
        const auto nextCopied=MakePrepared(134,jobAddress+0x70,PreparationOrigin::CopiedList);
        preparedLists[1].Publish(nextCopied);HaloCE_PublishTracking(nextCopied.receipt.tracking,true);
        check(HaloCE_GetAnniversaryPreparedListTracking(list,owner)&&owner.serial==133,
            "fixture retains a valid old active-list receipt with identical stationary cameras");
        workerSubmissionProbe=true;PrepareBody(jobAddress);workerSubmissionProbe=false;
        check(workerBeforeUnowned&&workerAfterOwned&&workerObservedSerial==134,
            "real job scope retires overwritten active-list receipt before copy and admits only its fresh copied ticket before workers");
        check(!jobScope.owned&&HaloCE_Armed(),"worker handoff leaves camera armed and clears job scope");
    }
    {
        // Cover the complete frame -> per-eye replay -> native callback ->
        // actual GPU capture chain. be2140f lost the render flags before this
        // boundary, which all earlier standalone layout/target tests missed.
        std::array<uint8_t,0xb8> players{};
        std::array<uint8_t,3> rendering{{0,0,1}};
        std::array<uint8_t,0x18> display{};
        std::array<uint8_t,4*0x48> targetStack{};
        const uintptr_t playersAddress=reinterpret_cast<uintptr_t>(players.data());
        const uintptr_t renderingAddress=reinterpret_cast<uintptr_t>(rendering.data());
        const uintptr_t displayAddress=reinterpret_cast<uintptr_t>(display.data());
        const uintptr_t storageAddress=reinterpret_cast<uintptr_t>(targetStack.data());
        const uintptr_t callbackAddress=bindings.base+contract::anniversary_hud::native_hud_callback;
        const int16_t playerCount=1;const int32_t depth=1,capacity=4;
        const UINT height=testDesc.Height*2;
        hudRoot=reinterpret_cast<uintptr_t>(sourceWrapper.data());
        std::memcpy(sourceWrapper.data(),&textureVtable,8);
        std::memcpy(mapped.data()+0x2d62890,&hudRoot,8);
        std::memcpy(mapped.data()+0x1c33fe0,&callbackAddress,8);
        std::memcpy(mapped.data()+0x2ea2d90,&playersAddress,8);
        std::memcpy(players.data()+0xb4,&playerCount,2);
        mapped[0x2d9bdd1]=mapped[0x2e3b829]=1;
        std::memcpy(mapped.data()+0x2d91330,&renderingAddress,8);
        std::memcpy(nativeConfig.data()+0x118,&displayAddress,8);
        std::memcpy(display.data()+0x10,&testDesc.Width,4);
        std::memcpy(display.data()+0x14,&height,4);
        std::memcpy(nativeBackend.data()+8,&storageAddress,8);
        std::memcpy(nativeBackend.data()+0x10,&depth,4);
        std::memcpy(nativeBackend.data()+0x14,&capacity,4);
        Camera camera{};camera.position={10,20,30};camera.forward={1,0,0};camera.up={0,0,1};
        camera.verticalFov=1;camera.nearPlane=.01f;camera.farPlane=1000;
        camera.window=camera.viewport={0,0,static_cast<int16_t>(height),static_cast<int16_t>(testDesc.Width)};
        std::memcpy(mapped.data()+0x2d9cb34,&camera,sizeof(camera));
        check(InstallFixtureJump(bindings.base+contract::anniversary_hud::native_target_push,
                reinterpret_cast<uintptr_t>(&NativeHudPush))&&
            InstallFixtureJump(bindings.base+contract::anniversary_hud::native_target_pop,
                reinterpret_cast<uintptr_t>(&NativeHudPop))&&
            InstallFixtureJump(bindings.base+contract::anniversary_hud::native_hud_preamble,
                reinterpret_cast<uintptr_t>(&NativeHudPreamble)),"private native HUD fixture entry points");
        anniversaryHudHook.original=reinterpret_cast<void*>(&NativeHudCallback);
        anniversaryHudInstalled=true;ConfigureCeHudLayoutRuntimeFixture(3,true);
        const D3D11_VIEWPORT priorViewport{2,3,20,10,.2f,.8f};
        const D3D11_RECT priorScissor{2,3,22,13};
        auto hudFrame=[&](uint64_t serial,uint32_t flags,bool expectedHud,
            bool expectedPair=true,unsigned expectedCallbacks=UINT_MAX,bool expectedClean=true) {
            if (recyclePreparedSource)
            {
                const auto next=MakePrepared(serial,jobAddress+0x70,PreparationOrigin::CopiedList);
                preparedLists[1].Publish(next);PrepareBody(jobAddress);
            }
            else publish(serial);
            Prepared prepared{};renderReady.Read(prepared);
            HaloCE_PublishTracking(prepared.receipt.tracking,true);recenter=false;
            publishedReference.Publish({{prepared.receipt.tracking.headPosition,{},7,3},referenceRevision.load()});
            ObserveRaster(priorViewport,priorScissor);
            const auto beforeHud=hudCallbacks;
            FrameBody(0,flags);
            check(hudCallbacks-beforeHud==(expectedCallbacks==UINT_MAX?(expectedHud?2u:0u):expectedCallbacks),
                "native HUD-enable bit and guards reach the expected output callbacks");
            const bool acquired=HaloCE_AcquirePair(context.Get(),serial,7,pair);
            check((acquired&&pair.tracking.serial==serial)==expectedPair,
                "HUD cleanup ownership determines whether the current world pair can be submitted");
            if (pair.borrowId)
            {
                check(!expectedPair||(Pixels(device.Get(),pair.eyes[0],expectedHud?hudColor:leftColor)&&
                    Pixels(device.Get(),pair.eyes[1],expectedHud?hudColor:rightColor)),
                    "HUD callback pixels reach the two production eye captures only when admitted");
                HaloCE_ReleasePair(pair.borrowId);pair={};
            }
            if (expectedClean)
            {
                D3D11_VIEWPORT after{};D3D11_RECT rect{};UINT n=1,m=1;
                testContext->RSGetViewports(&n,&after);testContext->RSGetScissorRects(&m,&rect);
                check(n==1&&m==1&&!std::memcmp(&after,&priorViewport,sizeof(after))&&
                    !std::memcmp(&rect,&priorScissor,sizeof(rect)),"complete replay restores exact pre-HUD GPU raster");
            }
            int32_t afterDepth{};uintptr_t afterOutput{};
            Read(depthBackend+0x10,afterDepth);Read(bindings.base+0x2e3d0d0,afterOutput);
            check((!expectedClean||afterDepth==1)&&afterOutput==0,
                "HUD restores borrowed output and every still-owned native target stack");
            check(!anniversaryHudReplay&&HaloCE_Armed(),
                "optional HUD work clears its thread scope and preserves the CE core");
        };
        hudFrame(134,0,false);
        check(anniversaryHudFailure.load()==10,
            "native HUD frame-bit refusal is distinguished from camera and source failures");
        hudFrame(135,0x10,true);
        check(anniversaryHudFailure.load()==0,
            "recovered native HUD clears its old rejection reason");
        const int32_t noCapacity=2;std::memcpy(nativeBackend.data()+0x14,&noCapacity,4);
        hudFrame(136,0x10,false);
        std::memcpy(nativeBackend.data()+0x14,&capacity,4);
        hudFrame(137,0x10,true);
        hudFault=HudFault::CallbackException;
        hudFrame(138,0x10,false,true,2);
        check(anniversaryHudFailure.load()==4,"a restored callback exception is a feature-only fallback");
        hudFault=HudFault::None;hudFrame(139,0x10,true);
        hudFault=HudFault::PreambleException;
        hudFrame(140,0x10,false);
        check(anniversaryHudFailure.load()==4,"a restored preamble exception cannot escape the optional feature");
        hudFault=HudFault::None;hudFrame(141,0x10,true);
        hudFault=HudFault::UnownedStack;
        hudFrame(142,0x10,false,false,1,false);
        check(anniversaryHudFailure.load()==5,"unverifiable cleanup drops only the affected frame");
        // Model the native owner restoring its own fresh stack before the
        // next frame; the adapter must not pop an unowned stack entry.
        int32_t preservedDepth{};Read(depthBackend+0x10,preservedDepth);
        check(preservedDepth==3,"unowned stack entries are preserved instead of guessed away");
        std::memcpy(nativeBackend.data()+0x10,&depth,4);
        hudFault=HudFault::None;hudFrame(143,0x10,true);
        // Numeric observation is available before the optional layout hook
        // installs. Rejected replay has not mutated its raster, so it must
        // preserve both world captures and refrain from calling native HUD.
        ConfigureCeHudLayoutRuntimeFixture(3,true,false);
        hudFrame(144,0x10,false);
        check(anniversaryHudFailure.load()==3,
            "uninstalled optional layout is a feature-only admission fallback");
        ConfigureCeHudLayoutRuntimeFixture(3,true);
        hudFrame(145,0x10,true);
        Camera invalidCamera{};
        std::memcpy(mapped.data()+0x2d9cb34,&invalidCamera,sizeof(invalidCamera));
        hudFrame(146,0x10,false);
        check(anniversaryHudFailure.load()==20,
            "missing stock HUD camera leaves world eyes intact with a precise reason");
        std::memcpy(mapped.data()+0x2d9cb34,&camera,sizeof(camera));
        hudFrame(147,0x10,true);
        check(hudRasterCorrect&&anniversaryHudDraws.load()==14,
            "both-eye HUD raster and capture recover after rejected and faulted optional callbacks");
        recyclePreparedSource=true;
        hudFrame(148,0x10,true);
        Prepared renderedReceipt{};renderReady.Read(renderedReceipt);
        check(!handoff.Current(renderedReceipt.receipt.ticket)&&anniversaryHudFailure.load()==0,
            "recycled preparation source cannot revoke the frozen in-flight HUD eye receipt");
        recyclePreparedSource=false;
        // The live 884de13 replay reached native work and failed cleanup once
        // before rendering stopped. Above, the preserved dormant adapter
        // reproduces that unsafe native-stack case. The shipping rollback
        // must refuse entry while the same fault remains armed, yet produce
        // fresh, distinct world images on successive Anniversary frames.
        anniversaryHudInstalled=false;
        check(!AnniversaryHud_Install(),"failed manual HUD replay remains disabled at native installation");
        hudFault=HudFault::UnownedStack;
        const auto fallbackBefore=anniversaryHudFallbacks.load();
        const auto drawsBefore=anniversaryHudDraws.load();
        const auto capturedBefore=captured.load();
        hudFrame(149,0x10,false);
        hudFrame(150,0x10,false);
        check(captured.load()==capturedBefore+2&&anniversaryHudFallbacks.load()==fallbackBefore&&
            anniversaryHudDraws.load()==drawsBefore&&HaloCE_Armed(),
            "disabled unsafe callback leaves consecutive Anniversary pairs and camera ownership alive");
        hudFault=HudFault::None;
        anniversaryHudInstalled=false;anniversaryHudHook={};hudRoot=0;
        ConfigureCeHudLayoutRuntimeFixture(3,false);
    }
    {
        // Native order regression: both worlds copy first; the single normal
        // callback then reaches the production gameplay/layout adapter. GPU
        // pixels must reach both eye caches before the native frame returns.
        auto packedDesc=testDesc;packedDesc.Height*=2;
        const char* hudShader=R"(
            float4 VS(uint id:SV_VertexID):SV_Position {
                float2 p[4]={float2(-1,1),float2(-.5,1),float2(-1,0),float2(-.5,0)};
                return float4(p[id],0,1);
            }
            float4 PS():SV_Target { return float4(120,171,52,255)/255; }
        )";
        ComPtr<ID3DBlob> vsCode,psCode;
        ComPtr<ID3D11VertexShader> hudVs;ComPtr<ID3D11PixelShader> hudPs;
        ComPtr<ID3D11RasterizerState> hudRaster;ComPtr<ID3D11DepthStencilState> hudDepth;
        D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;
        rasterDesc.CullMode=D3D11_CULL_NONE;rasterDesc.DepthClipEnable=true;rasterDesc.ScissorEnable=true;
        D3D11_DEPTH_STENCIL_DESC hudDepthDesc{};hudDepthDesc.DepthEnable=false;
        hudDepthDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;hudDepthDesc.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(SUCCEEDED(D3DCompile(hudShader,std::strlen(hudShader),nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,&vsCode,nullptr))&&
            SUCCEEDED(D3DCompile(hudShader,std::strlen(hudShader),nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&psCode,nullptr))&&
            SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&hudVs))&&
            SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&hudPs))&&
            SUCCEEDED(device->CreateRasterizerState(&rasterDesc,&hudRaster))&&
            SUCCEEDED(device->CreateDepthStencilState(&hudDepthDesc,&hudDepth)),"native HUD draw fixture shaders created");
        naturalHudVs=hudVs.Get();naturalHudPs=hudPs.Get();naturalHudRaster=hudRaster.Get();naturalHudDepth=hudDepth.Get();
        ComPtr<ID3D11Texture2D> packedTexture;ComPtr<ID3D11RenderTargetView> packedView;
        check(SUCCEEDED(device->CreateTexture2D(&packedDesc,nullptr,&packedTexture))&&
            SUCCEEDED(device->CreateRenderTargetView(packedTexture.Get(),nullptr,&packedView)),"packed native HUD target created");
        testDestination=packedTexture.Get();packedHudView=packedView.Get();
        HaloCE_RecordTextureCreated(testDestination,packedDesc);
        std::array<uint8_t,0x118> wrapper{};uintptr_t table=reinterpret_cast<uintptr_t>(&packedHudView);
        std::array<uint8_t,0xb8> players{};
        const uintptr_t playersAddress=reinterpret_cast<uintptr_t>(players.data());
        const int16_t playerCount=1;
        std::memcpy(mapped.data()+0x2ea2d90,&playersAddress,8);std::memcpy(players.data()+0xb4,&playerCount,2);
        packedHudRoot=reinterpret_cast<uintptr_t>(wrapper.data());
        std::memcpy(wrapper.data(),&textureVtable,8);
        const uint32_t colorFlag=1u<<8;const int32_t viewCount=1;const int8_t mipCount=1;
        std::memcpy(wrapper.data()+0x88,&colorFlag,4);std::memcpy(wrapper.data()+0x1a,&mipCount,1);
        std::memcpy(wrapper.data()+0xe0,&testDestination,8);std::memcpy(wrapper.data()+0xe8,&table,8);
        std::memcpy(wrapper.data()+0xf0,&viewCount,4);
        check(InstallFixtureJump(bindings.base+contract::hud_target::hud_target_surface_select,
            reinterpret_cast<uintptr_t>(&NativeDepthSelect)),"verified native surface selector fixture");
        check(InstallFixtureJump(bindings.base+contract::hud_target::hud_target_bind,
            reinterpret_cast<uintptr_t>(&NaturalHudTargetBind)),"verified native target binder fixture");
        Camera camera{};camera.position={10,20,30};camera.forward={1,0,0};camera.up={0,0,1};
        camera.verticalFov=1;camera.nearPlane=.01f;camera.farPlane=1000;
        camera.window=camera.viewport={0,0,static_cast<int16_t>(packedDesc.Height),static_cast<int16_t>(packedDesc.Width)};
        std::memcpy(mapped.data()+0x2d9cb34,&camera,sizeof(camera));
        std::memcpy(mapped.data()+0x29af2c4,&camera,sizeof(camera));
        const int16_t player=0;std::memcpy(mapped.data()+0x29af2b8,&player,2);
        anniversaryHudHook.original=reinterpret_cast<void*>(&NaturalHudCallback);
        anniversaryHudInstalled=false;naturalHudFrame=true;hudTargetBindingsVerified=true;
        ConfigureCeHudLayoutRuntimeFixture(3,true);
        auto pixelsMatch=[&](ID3D11Texture2D* texture,int eye,bool hud) {
            auto desc=testDesc;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
            context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE read{};
            if (FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&read))) return false;
            bool same=true;const auto& region=hudRegions[eye];
            for (UINT y=0;y<desc.Height;++y) for (UINT x=0;x<desc.Width;++x)
            {
                const UINT packedY=y+eye*desc.Height;
                const bool inside=hud&&x>=region.left&&x<region.right&&packedY>=region.top&&packedY<region.bottom;
                const uint32_t expected=inside?hudColor:(eye?rightColor:leftColor);
                const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(read.pData)+y*read.RowPitch);
                same&=row[x]==expected;
            }
            context->Unmap(staging.Get(),0);return same;
        };
        auto naturalFrame=[&](uint64_t serial,bool expectedHud,unsigned expectedDraws,float size=1.0f,uint32_t flags=0x10) {
            publish(serial);Prepared p{};renderReady.Read(p);p.receipt.tracking.hud.size=size;
            p.receipt.tracking.hud.aspect=1;p.receipt.tracking.hud.verticalOffset=0;
            renderReady.Publish(p);auto newer=p.receipt.tracking;++newer.serial;
            HaloCE_PublishTracking(newer,true);recenter=false;
            publishedReference.Publish({{p.receipt.tracking.headPosition,{},7,3},referenceRevision.load()});
            expectedNaturalSerial=serial;naturalFrozen=true;naturalRasterRestored=true;naturalTargetRestoreCorrect=true;
            naturalBindCalls=0;naturalForeignRasterPreserved=true;
            const auto before=naturalCallbacks;naturalGameplayCalls=0;hudRegions[0]={};hudRegions[1]={};
            FrameBody(0,flags);
            std::memcpy(mapped.data()+0x2ea2d30,&testContext,8);
            FrameDiagnostic diag{};frameDiagnostic.Read(diag);
            if (diag.failure!=FrameFailure::None||naturalGameplayCalls!=expectedDraws)
                std::fprintf(stderr,"natural serial=%llu frame=%u consumer=%u depth=%u eyes=%u HUD=%u calls=%u expected=%u\n",
                    serial,unsigned(diag.failure),diag.consumerFailure,diag.depthFailure,diag.eyeMask,
                    anniversaryHudFailure.load(),naturalGameplayCalls,expectedDraws);
            check(naturalCallbacks-before==((flags&0x10)?1u:0u),"native callback runs only once and only from ordinary frame tail");
            check(naturalGameplayCalls==expectedDraws,"only admitted native gameplay HUD is framed into both eyes");
            check(HaloCE_AcquirePair(context.Get(),serial+1,7,pair),"late HUD keeps complete world pair available");
            if (pair.borrowId)
            {
                const bool drawnPixels=pixelsMatch(pair.eyes[0],0,expectedHud)&&pixelsMatch(pair.eyes[1],1,expectedHud);
                if (!drawnPixels) std::fprintf(stderr,"HUD GPU mismatch serial=%llu HUD=%u\n",serial,unsigned(expectedHud));
                check(drawnPixels,
                    "late native HUD pixels reach both eyes while distinct world pixels survive around them");
                HaloCE_ReleasePair(pair.borrowId);pair={};
            }
            if (expectedHud) check(naturalFrozen&&naturalRasterRestored&&naturalTargetRestoreCorrect&&anniversaryHudFailure==0,
                "both HUD draws use frozen frame tracking and restore full native viewport");
            check(!anniversaryNaturalHud&&HaloCE_Armed(),"natural HUD scope clears and keeps camera armed");
        };
        anniversaryHudNaturalInstalled=false;naturalFrame(151,false,1); // Reproduce prior early-capture omission.
        anniversaryHudNaturalInstalled=true;naturalFrame(152,true,2);
        naturalFrame(153,true,2,.5f);
        naturalNativeReset=true;naturalFrame(154,true,2,.5f);naturalNativeReset=false;
        naturalRevokeSource=true;naturalFrame(155,false,2);naturalRevokeSource=false;
        naturalFrame(156,true,2);
        naturalWrongTarget=true;naturalFrame(157,false,1);naturalWrongTarget=false;
        naturalFrame(158,true,2);
        naturalOmitGameplay=true;naturalFrame(159,false,0);naturalOmitGameplay=false;
        naturalFrame(160,false,0,1,0);
        naturalInvalidateRaster=true;naturalFrame(161,false,1);naturalInvalidateRaster=false;
        naturalFrame(162,true,2);
        naturalForeignCaller=true;naturalFrame(163,false,1);naturalForeignCaller=false;
        naturalUnobservedRaster=true;naturalFrame(164,false,1);naturalUnobservedRaster=false;
        naturalRepeatGameplay=true;naturalFrame(165,false,3);naturalRepeatGameplay=false;
        const int16_t twoPlayers=2;std::memcpy(players.data()+0xb4,&twoPlayers,2);
        naturalFrame(166,false,1);std::memcpy(players.data()+0xb4,&playerCount,2);
        naturalFrame(167,true,2);
        naturalSwapSelectedSource=true;naturalFrame(168,false,2);naturalSwapSelectedSource=false;
        naturalRetireContext=true;naturalFrame(169,false,2);naturalRetireContext=false;
        naturalFrame(170,true,2);
        // Full-resolution eyes keep the native HUD canvas at desktop height;
        // only the packed world target is double height. Its two HUD regions
        // must keep size/height controls and native resets without halving art.
        naturalAuthoredHeight=testDesc.Height;
        camera.window=camera.viewport={0,0,static_cast<int16_t>(testDesc.Height),static_cast<int16_t>(testDesc.Width)};
        std::memcpy(mapped.data()+0x2d9cb34,&camera,sizeof(camera));
        std::memcpy(mapped.data()+0x29af2c4,&camera,sizeof(camera));
        naturalNativeReset=true;naturalFrame(171,true,2,.5f);naturalNativeReset=false;
        check(hudRegions[0].bottom>hudRegions[0].top&&hudRegions[0].bottom<=testDesc.Height&&
            hudRegions[1].top>=testDesc.Height&&hudRegions[1].bottom<=2*testDesc.Height,
            "full-resolution authored HUD maps into both independently bounded eye regions");
        check(hudRegions[0].right-hudRegions[0].left==testDesc.Width/8&&
            hudRegions[1].right-hudRegions[1].left==testDesc.Width/8,
            "full-resolution packed HUD still applies the configured half-size slider");
        naturalAuthoredHeight=0;
        naturalIncompatibleDepth=true;naturalFrame(172,true,2);naturalIncompatibleDepth=false;
        const auto detachments=anniversaryHudDepthDetachments.load();
        check(detachments>0,"native one-eye depth is detached for packed HUD drawing");
        naturalIncompatibleDepth=true;
        naturalBindFault=NaturalBindFault::BeforeMutation;naturalFrame(173,false,1);
        naturalBindFault=NaturalBindFault::AfterMutation;naturalFrame(174,false,1);
        check(naturalTargetRestoreCorrect,"native target restores after a bind mutates then throws");
        naturalBindFault=NaturalBindFault::Readback;naturalFrame(175,false,1);
        check(naturalTargetRestoreCorrect,"native target restores after a successful bind has incomplete cache readback");
        naturalBindFault=NaturalBindFault::ForeignTarget;naturalFrame(176,false,1);
        check(naturalForeignRasterPreserved&&naturalBindCalls==1,
            "foreign target takeover keeps both its native descriptor and distinct raster untouched by cleanup");
        naturalBindFault=NaturalBindFault::Readback;naturalBindFaultAt=2;naturalFrame(177,false,2);
        check(naturalBindCalls==3,"partial restore with original descriptor can retry after failed cache publication");
        naturalBindFault=NaturalBindFault::None;naturalBindFaultAt=1;
        naturalFrame(178,true,2);
        naturalRevokeDepth=true;naturalFrame(179,false,2);naturalRevokeDepth=false;
        check(naturalBindCalls==1,"released detached depth is refused before the restoration binder");
        HaloCE_RecordTextureCreated(static_cast<ID3D11Texture2D*>(depthTextures[0]),depthDesc);
        naturalChangeDepthView=true;naturalFrame(180,false,2);naturalChangeDepthView=false;SelectDepth(0);
        check(naturalBindCalls==1,"changed detached depth view is refused before the restoration binder");
        naturalFrame(181,true,2);
        publish(182);Prepared faultPrepared{};renderReady.Read(faultPrepared);
        auto latest=faultPrepared.receipt.tracking;++latest.serial;HaloCE_PublishTracking(latest,true);recenter=false;
        publishedReference.Publish({{faultPrepared.receipt.tracking.headPosition,{},7,3},referenceRevision.load()});
        naturalGameplayFault=true;naturalBindCalls=0;naturalGameplayCalls=0;
        check(InvokeNaturalHudFrameFault(),"native gameplay HUD exception propagates after scoped cleanup");
        naturalGameplayFault=false;
        uintptr_t depthAfterFault{};std::memcpy(&depthAfterFault,reinterpret_cast<void*>(depthBackend+0x48),8);
        check(!anniversaryNaturalHud&&depthAfterFault==depthRoot&&naturalBindCalls==2&&HaloCE_Armed(),
            "gameplay SEH restores the original native depth descriptor and leaves the camera armed");
        naturalFrame(183,true,2);naturalIncompatibleDepth=false;
        naturalIncompatibleDepth=true;
        naturalBindFault=NaturalBindFault::ZeroDimensions;naturalFrame(184,false,1);
        check(naturalTargetRestoreCorrect&&naturalBindCalls==2,
            "native zero-dimension partial preparation restores exact original target");
        naturalBindFaultAt=2;naturalFrame(185,false,2);
        check(naturalTargetRestoreCorrect&&naturalBindCalls==3,
            "native zero-dimension partial restoration repairs once without replaying gameplay");
        naturalBindFault=NaturalBindFault::None;naturalBindFaultAt=1;
        naturalFrame(186,true,2);naturalIncompatibleDepth=false;
        const auto callbacksBefore=callbacks.load();
        anniversaryHudHook.original=reinterpret_cast<void*>(&NativeHudHookFault);
        check(InvokeActualHudHookFault()&&callbacks.load()==callbacksBefore&&!anniversaryNaturalHud,
            "actual HUD detour preserves native exception and retires its callback count under SEH");
        anniversaryHudNaturalInstalled=false;naturalHudFrame=false;anniversaryHudHook={};
        packedHudRoot=0;packedHudView=nullptr;testDestination=destination.Get();
        context->OMSetRenderTargets(0,nullptr,nullptr);ConfigureCeHudLayoutRuntimeFixture(3,false);
    }
    {
        // The Anniversary native builder must publish its untouched center
        // for controls/shot consumers outside render callbacks. Earlier WIP
        // only published this receipt from Classic, leaving Anniversary inert.
        Tracking tracking{}; tracking.generation=3; tracking.spaceEpoch=7; tracking.serial=140;
        tracking.headPosition={.4f,1.7f,-.3f};
        HaloCE_PublishTracking(tracking,true); recenter=false;
        const Reference frozen{{0,1.6f,0},{},7,3};
        Camera native{}; native.position={10,20,30}; native.forward={1,0,0}; native.up={0,0,1};
        native.verticalFov=1; native.viewport=native.window={0,0,32,32};
        native.nearPlane=.01f; native.farPlane=1000;
        const Vec3 offset{17,-9,23}; const float bias=2.5f;
        std::array<uint8_t,0x138> nativeScene{};
        const uintptr_t sceneAddress=reinterpret_cast<uintptr_t>(nativeScene.data());
        std::memcpy(mapped.data()+0x2e3c418,&sceneAddress,sizeof(sceneAddress));
        std::memcpy(mapped.data()+0x2b05118,&offset,sizeof(offset));
        std::memcpy(mapped.data()+0x2e3b838,&bias,sizeof(bias));
        SaberCamera saber{}; BuildSaberPose(native,offset,bias,saber.pose);
        saber.viewportWidth=32; saber.viewportHeight=32; saber.verticalFovDegrees=57.2957795f;
        saber.nearPlane=.03f; saber.farPlane=3000;
        const auto revision=referenceRevision.load();
        gameplayCamera.Publish({}); RenderContext received{};
        check(!HaloCE_GetGameplayContext(received),"Anniversary controls require an actual stock-camera publication");
        gameplayBridgeVerified=false;
        PublishAnniversaryGameplayContext(saber,tracking,frozen,revision,Game_GetWorldScale(),true);
        check(!HaloCE_GetGameplayContext(received),"unverified native bridge stays stock for controls only");
        gameplayBridgeVerified=true;
        PublishAnniversaryGameplayContext(saber,tracking,frozen,revision,Game_GetWorldScale(),true);
        check(HaloCE_GetGameplayContext(received)&&std::fabs(received.camera.position.x-10)<.0001f&&
            std::fabs(received.camera.position.y-20)<.0001f&&std::fabs(received.camera.position.z-30)<.0001f&&
            received.tracking.serial==140&&received.referenceRevision==revision,
            "Anniversary stock-camera receipt reaches nonrender controls without tracked-eye offsets");
        tracking.serial=141; tracking.headPosition.x=.7f; HaloCE_PublishTracking(tracking,true);
        check(HaloCE_GetGameplayContext(received)&&received.tracking.serial==141&&
            received.tracking.headPosition.x==.7f&&std::fabs(received.camera.position.x-10)<.0001f,
            "controls refresh XR input while preserving the native center and matching reference");
        mapped[0x2e3b826]=1;
        PublishAnniversaryGameplayContext(saber,tracking,frozen,revision,Game_GetWorldScale(),true);
        check(!HaloCE_GetGameplayContext(received),"native free camera immediately revokes prior gameplay publication");
        mapped[0x2e3b826]=0; nativeScene[0x130]=1;
        PublishAnniversaryGameplayContext(saber,tracking,frozen,revision,Game_GetWorldScale(),true);
        check(!HaloCE_GetGameplayContext(received),"external native scene camera cannot be inverted as a gameplay center");
        nativeScene[0x130]=0;
        PublishAnniversaryGameplayContext(saber,tracking,frozen,revision,Game_GetWorldScale(),true);
        check(HaloCE_GetGameplayContext(received),"ordinary native camera publication recovers after external mode");
        HaloCE_Recenter();
        check(!HaloCE_GetGameplayContext(received),"recenter revokes the nonrender Anniversary control receipt");
        recenter=false;
        PublishAnniversaryGameplayContext(saber,tracking,frozen,revision,Game_GetWorldScale(),true);
        check(!HaloCE_GetGameplayContext(received),"old builder revision cannot republish after recenter");
        PublishAnniversaryGameplayContext(saber,tracking,frozen,referenceRevision.load(),Game_GetWorldScale(),true);
        check(HaloCE_GetGameplayContext(received),"new native center recovers Anniversary controls");
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=0;
        check(!HaloCE_GetGameplayContext(received),"graphics-mode switch revokes Anniversary controls before reuse");
        *reinterpret_cast<int32_t*>(mapped.data()+0x1b7aa84)=1; CeObserveRendererMode();
    }
    {
        // An Original-mode preparation may observe the graphics toggle only
        // after it started. Its bookkeeping scope owns no tracking reference.
        // Require the camera guard before even forcing native secondary=1.
        std::array<uint8_t,0x10> clock{};clock[0]=1;
        const int32_t tick=20,count=1;
        std::memcpy(clock.data()+0xc,&tick,4);
        const uintptr_t clockAddress=reinterpret_cast<uintptr_t>(clock.data());
        std::memcpy(mapped.data()+0x2e9fd68,&clockAddress,8);
        Camera center{};center.forward={1,0,0};center.up={0,0,1};
        center.verticalFov=1;center.nearPlane=.01f;center.farPlane=1000;
        center.viewport=center.window={0,0,32,32};
        SaberCamera source{};BuildSaberPose(center,{},0,source.pose);
        source.viewportWidth=source.viewportHeight=32;
        source.verticalFovDegrees=57.2957795f;source.nearPlane=.03f;source.farPlane=3000;
        const uintptr_t sourceAddress=reinterpret_cast<uintptr_t>(&source);
        const uintptr_t arrayAddress=reinterpret_cast<uintptr_t>(&sourceAddress);
        std::memcpy(mapped.data()+0x2b17b98,&count,4);
        std::memcpy(mapped.data()+0x2b17b90,&arrayAddress,8);
        auto tracking=MakePrepared(160,jobAddress+0x70,PreparationOrigin::CopiedList).receipt.tracking;
        HaloCE_PublishTracking(tracking,true);
        const auto previousScope=jobScope;
        const auto previousReference=reference;
        hooks[Builder].original=reinterpret_cast<void*>(&NativeBuilder);
        jobScope={jobAddress,rendererAddress+0xb0,true,false};
        recenter=true;
        check(LiveGame()&&SingleCamera(),"builder camera-ownership fixture reaches native eligibility");
        check(BuilderHook(0,reinterpret_cast<SaberViewPair*>(jobAddress+0x70),0,nullptr)==0xceba1234&&
            observedBuilderSecondary==0&&!observedBuilderTracking&&recenter.load()&&
            !std::memcmp(&reference,&previousReference,sizeof(reference)),
            "Original stock job cannot force stereo or consume recenter after a graphics toggle");
        jobScope.cameraOwned=true;
        check(BuilderHook(0,reinterpret_cast<SaberViewPair*>(jobAddress+0x70),0,nullptr)==0xceba1234&&
            observedBuilderSecondary==1&&observedBuilderTracking&&!recenter.load()&&
            reference.generation==tracking.generation&&reference.spaceEpoch==tracking.spaceEpoch,
            "camera-owned Anniversary job still reaches tracked native construction");
        jobScope=previousScope;
    }
    {
        // Publishing the next valid XR sample is not a disable operation.
        // Material workers can inspect admission during this exact production
        // function, even while the copied sample itself is being refreshed.
        const auto tracking=MakePrepared(170,jobAddress+0x70,PreparationOrigin::CopiedList).receipt.tracking;
        HaloCE_PublishTracking(tracking,true);
        std::atomic<bool> started{},done{};
        std::atomic<unsigned> transientDisables{};
        std::thread reader([&] {
            started.store(true,std::memory_order_release);
            while (!done.load(std::memory_order_acquire))
                if (!trackingEnabled.load(std::memory_order_acquire))
                    transientDisables.fetch_add(1,std::memory_order_relaxed);
        });
        while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned iteration=0;iteration<100000;++iteration) HaloCE_PublishTracking(tracking,true);
        done.store(true,std::memory_order_release);reader.join();
        std::printf("CE tracking replacement: %u transient disables\n",transientDisables.load());
        check(transientDisables.load()==0,"enabled tracking replacement never transiently revokes worker material policy");
        HaloCE_PublishTracking(tracking,false);
        Tracking received{};
        check(!TrackingNow(received),"explicit tracking disable still revokes workers immediately");
        HaloCE_PublishTracking(tracking,true);
        check(TrackingNow(received)&&received.serial==tracking.serial,"valid tracking publication recovers after explicit disable");
    }
    ResourceRegistry::Record recorded{};
    const uintptr_t pendingIdentity=0x70012340;
    const auto pendingRevision=resources.Revoke(pendingIdentity);
    resources.Forget(pendingIdentity);
    check(!resources.Publish(pendingIdentity,pendingIdentity,pendingRevision,testDesc)&&
        !resources.Read(pendingIdentity,pendingIdentity,recorded),
        "native release invalidates reserved metadata even before its payload was published");
    resources.Forget(reinterpret_cast<uintptr_t>(testSource));
    HaloCE_RecordTextureCreated(testSource,testDesc);
    check(resources.Read(reinterpret_cast<uintptr_t>(testSource),reinterpret_cast<uintptr_t>(testSource),recorded),
        "early creation metadata bootstraps a texture created before native hooks");
    check(!Remove()&&installed.load(),"retirement keeps protective hooks while native synthetic lists remain");
    check(ResetListHook(jobAddress+0x70)==0xfedcba9876543210ull,"native list reset preserves its return register");
    ResetListHook(rendererAddress+0xb0);
    check(!HasSyntheticLists(),"native reset retires synthetic source and active lists");
    check(Remove(),"drained adapter resources retire without touching another title");
    check(!resources.Read(reinterpret_cast<uintptr_t>(testSource),reinterpret_cast<uintptr_t>(testSource),recorded),
        "module retirement invalidates previous pointer identities");
    return failures?1:0;
}
