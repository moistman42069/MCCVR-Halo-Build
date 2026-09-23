#include "../common/hud_visibility.h"
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <climits>
#include <cmath>
#include <intrin.h>
#include <MinHook.h>

#pragma intrinsic(_ReturnAddress)
#include "d3d11_hook.h"
#include "game.h"
#include "vr.h"
#include "../common/dlss_logic.h"
#include "../common/dlss_sampler_logic.h"
#include "../common/live_resize_logic.h"
#include "haloce_stereo_core.h"
#include "haloce_hud_layout.h"
#include "title_reentry_probe.h"
#include "title_adapter.h"
#include "bloom_override.h"
#if HALOMCCVR_HALO2_STEREO6DOF
#include "halo2_stereo_core.h"
#include "../common/halo2_hud_logic.h"
#include "../common/halo2_hud_shader_logic.h"
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
#include "../common/halo4_helmet_shader_logic.h"
#include "../common/halo4_screen_effect_shader_logic.h"
#endif
#include "../common/config.h"
#include "../common/coop_probe_logic.h"
#include "../common/log.h"
#include "../common/runtime_types.h"

// We can't hook "the game's swapchain" directly because it doesn't exist yet
// when we're injected. Instead we create a throwaway D3D11 device + swapchain
// of our own, read the addresses of Present/ResizeBuffers out of its vtable
// (all swapchains in the process share the same implementation), hook those,
// and throw the dummy away.

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,
    const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
static CreateTexture2DFn g_origCreateTexture2D = nullptr;
using CreateShaderResourceViewFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11Resource*,
    const D3D11_SHADER_RESOURCE_VIEW_DESC*,ID3D11ShaderResourceView**);
static CreateShaderResourceViewFn g_origCreateShaderResourceView{};
static BloomSetResourcesFn g_origBloomSetResources{};
static HRESULT STDMETHODCALLTYPE CreateShaderResourceViewHook(ID3D11Device* device,ID3D11Resource* resource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,ID3D11ShaderResourceView** view)
{
    const HRESULT hr=g_origCreateShaderResourceView(device,resource,desc,view);
    if(SUCCEEDED(hr)&&view&&*view)Bloom_ObserveSrvCreated(device,*view);
    return hr;
}
static void STDMETHODCALLTYPE BloomSetResourcesHook(ID3D11DeviceContext* context,UINT start,UINT count,
    ID3D11ShaderResourceView* const* views)
{
    g_origBloomSetResources(context,start,count,views);
    Bloom_ObserveResources(context,start,count,views);
}
static HRESULT STDMETHODCALLTYPE CreateTexture2DHook(ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC* descriptor,const D3D11_SUBRESOURCE_DATA* data,
    ID3D11Texture2D** texture)
{
    const HRESULT result=g_origCreateTexture2D(device,descriptor,data,texture);
    if (SUCCEEDED(result)&&texture&&*texture)
    {
        // Creation owns this pointer; observe runtime-normalized fields here.
        D3D11_TEXTURE2D_DESC actual{};
        (*texture)->GetDesc(&actual);
        HaloCE_RecordTextureCreated(*texture,actual);
    }
    return result;
}
typedef void(STDMETHODCALLTYPE* OMSetRenderTargetsFn)(ID3D11DeviceContext*, UINT,
    ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef void(STDMETHODCALLTYPE* PSSetSamplersFn)(ID3D11DeviceContext*, UINT, UINT,
    ID3D11SamplerState* const*);
static PSSetSamplersFn g_origPSSetSamplers = nullptr;
using CreateSamplerStateFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_SAMPLER_DESC*,ID3D11SamplerState**);
static CreateSamplerStateFn g_origCreateSamplerState=nullptr;
using RSSetViewportsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);
using RSSetScissorRectsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_RECT*);
using ExecuteCommandListFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
using ClearStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using SwapDeviceContextStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, ID3DDeviceContextState*, ID3DDeviceContextState**);
static RSSetViewportsFn g_origRSSetViewports = nullptr;
static RSSetScissorRectsFn g_origRSSetScissorRects = nullptr;
static ExecuteCommandListFn g_origExecuteCommandList = nullptr;
static ClearStateFn g_origClearState = nullptr;
static SwapDeviceContextStateFn g_origSwapDeviceContextState = nullptr;
typedef void(STDMETHODCALLTYPE* OMSetRenderTargetsAndUnorderedAccessViewsFn)(
    ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
    ID3D11DepthStencilView*, UINT, UINT,
    ID3D11UnorderedAccessView* const*, const UINT*);
static OMSetRenderTargetsAndUnorderedAccessViewsFn
    g_origOMSetRenderTargetsAndUnorderedAccessViews = nullptr;
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
static std::atomic<bool> g_halo4AuthoredReticleDrawPathAvailable{false};
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
typedef void(STDMETHODCALLTYPE* DrawIndexedFn)(ID3D11DeviceContext*, UINT, UINT, INT);
#endif
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
typedef void(STDMETHODCALLTYPE* CopyResourceFn)(ID3D11DeviceContext*,
    ID3D11Resource*, ID3D11Resource*);
#endif
#if HALOMCCVR_HALO2_STEREO6DOF || HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
typedef HRESULT(STDMETHODCALLTYPE* CreatePixelShaderFn)(
    ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
    ID3D11PixelShader**);
static CreatePixelShaderFn g_origCreatePixelShader = nullptr;
#endif

#if HALOMCCVR_HALO2_STEREO6DOF
typedef void(STDMETHODCALLTYPE* Halo2DrawIndexedFn)(
    ID3D11DeviceContext*, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE* Halo2DrawFn)(ID3D11DeviceContext*, UINT, UINT);
static Halo2DrawIndexedFn g_origHalo2DrawIndexed = nullptr;
static Halo2DrawFn g_origHalo2Draw = nullptr;
static std::atomic<bool> g_halo2ShaderHooksAvailable{false};
static std::atomic<uint32_t> g_halo2NativeCrosshairGeneration{0};

struct Halo2RegisteredPixelShader
{
    std::atomic<uintptr_t> pointer{0};
    std::atomic<uint8_t> role{
        static_cast<uint8_t>(halo2_hud_shader::Role::Other)};
};

// The working external HUD mod identifies sixteen shaders. Leave spare slots
// for duplicate objects (device rebuilds can instantiate the same bytecode
// again) without putting a lock or allocation in Draw/DrawIndexed.
static std::array<Halo2RegisteredPixelShader, 64>
    g_halo2RegisteredPixelShaders{};
static std::atomic<uint64_t> g_halo2GameplayShadersRegistered{0};
static std::atomic<uint64_t> g_halo2CrosshairShadersRegistered{0};
static std::atomic<uint64_t> g_halo2HudRasterDraws{0};
static std::atomic<uint64_t> g_halo2NativeCrosshairDraws{0};
static std::atomic<uint64_t> g_halo2HudStateFailures{0};

static bool RegisterHalo2PixelShader(
    ID3D11PixelShader* shader, halo2_hud_shader::Role role) noexcept
{
    if (!shader)
        return false;
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(shader);
    for (auto& registered : g_halo2RegisteredPixelShaders)
    {
        if (registered.pointer.load(std::memory_order_acquire) == pointer)
        {
            const halo2_hud_shader::Role oldRole =
                static_cast<halo2_hud_shader::Role>(registered.role.exchange(
                    static_cast<uint8_t>(role), std::memory_order_acq_rel));
            if (role == halo2_hud_shader::Role::Other)
                registered.pointer.store(0, std::memory_order_release);
            return role != halo2_hud_shader::Role::Other && oldRole != role;
        }
    }
    if (role == halo2_hud_shader::Role::Other)
        return false;
    for (auto& registered : g_halo2RegisteredPixelShaders)
    {
        uintptr_t empty = 0;
        if (registered.pointer.compare_exchange_strong(
                empty, 1, std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            registered.role.store(
                static_cast<uint8_t>(role), std::memory_order_relaxed);
            registered.pointer.store(pointer, std::memory_order_release);
            return true;
        }
    }
    return false;
}

static halo2_hud_shader::Role LookupHalo2PixelShader(
    ID3D11PixelShader* shader) noexcept
{
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(shader);
    if (!pointer)
        return halo2_hud_shader::Role::Other;
    for (const auto& registered : g_halo2RegisteredPixelShaders)
    {
        if (registered.pointer.load(std::memory_order_acquire) == pointer)
        {
            return static_cast<halo2_hud_shader::Role>(
                registered.role.load(std::memory_order_acquire));
        }
    }
    return halo2_hud_shader::Role::Other;
}

struct Halo2HudDrawMutation
{
    enum class Kind : uint8_t { None, Raster, NativeCrosshair, Hidden } kind = Kind::None;
    UINT viewportCount = 0;
    D3D11_VIEWPORT viewports[
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT scissorCount = 0;
    D3D11_RECT scissors[
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
};

static Halo2HudDrawMutation BeginHalo2HudDraw(
    ID3D11DeviceContext* context) noexcept
{
    Halo2HudDrawMutation mutation{};
    if (!context ||
        !g_halo2ShaderHooksAvailable.load(std::memory_order_acquire) ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo2)
    {
        return mutation;
    }

    // Stage 3F ownership proof: Stage 3E proved that the native CHUD CPU
    // routine and the known HUD pixel shaders are both live, but that their
    // execution scopes never overlap. Classify the shader on the *actual*
    // D3D11 context issuing Draw/DrawIndexed instead of requiring the rejected
    // Halo2NativeHud_GetRasterLayout TLS gate.
    ID3D11PixelShader* shader = nullptr;
    UINT classInstanceCount = 0;
    context->PSGetShader(&shader, nullptr, &classInstanceCount);
    const halo2_hud_shader::Role role = LookupHalo2PixelShader(shader);
    if (shader)
        shader->Release();

    // Stage 3G native-crosshair handoff: the exact Halo 2 crosshair shader is
    // already proven independently by the working Toggle HUD/3DMigoto mod.
    // Stage 3F removed the rejected CHUD-TLS conjunction, so reaching this
    // branch means the *actual Draw/DrawIndexed context* has that authored
    // crosshair shader bound. Redirect that draw into the existing authored
    // reticle capture path. EndHalo2HudDraw publishes ownership only after the
    // draw completes, and Game_TitleCapturesAuthoredCrosshair then makes the
    // old procedural VR marker transparent. Until this first real draw occurs,
    // the procedural controller marker remains visible as the fail-open
    // fallback.
    if((Game_IsScopeRendering()||(g_config.hide_hud&&Game_IsHeadTracking()&&Game_MoveStickIsLocomotion()))&&
        (role==halo2_hud_shader::Role::Crosshair||role==halo2_hud_shader::Role::GameplayHud))
    { mutation.kind=Halo2HudDrawMutation::Kind::Hidden;return mutation; }
    if (role == halo2_hud_shader::Role::Crosshair)
    {
        if (VR_BeginAuthoredReticleCapture())
            mutation.kind = Halo2HudDrawMutation::Kind::NativeCrosshair;
        return mutation;
    }
    if (role != halo2_hud_shader::Role::GameplayHud)
        return mutation;

    // Stage 3H converts Stage 3F/3G's deliberately hard-coded 50% ownership
    // proof into the shipped HUD controls at the same headset-proven shader
    // draw boundary. Halo 2 has no native curvature basis, so the planar
    // viewport transaction consumes only size, aspect and vertical offset.
    // The exact old viewport is still restored immediately after this draw.
    mutation.viewportCount =
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context->RSGetViewports(&mutation.viewportCount, mutation.viewports);
    if (!mutation.viewportCount ||
        mutation.viewportCount >
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
    {
        g_halo2HudStateFailures.fetch_add(1, std::memory_order_relaxed);
        return mutation;
    }

    std::array<D3D11_VIEWPORT,
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> adjusted{};
    const float scaleY = std::clamp(
        g_config.hud_size,
        halo2_hud::kMinimumLayoutFraction,
        halo2_hud::kMaximumLayoutFraction);
    const float scaleX = std::clamp(
        g_config.hud_size * g_config.hud_aspect,
        halo2_hud::kMinimumLayoutFraction,
        halo2_hud::kMaximumLayoutFraction);
    const float verticalOffset = std::clamp(
        g_config.hud_vertical_offset,
        -halo2_hud::kMaximumVerticalOffset,
        halo2_hud::kMaximumVerticalOffset);
    for (UINT index = 0; index < mutation.viewportCount; ++index)
    {
        adjusted[index] = mutation.viewports[index];
        adjusted[index].TopLeftX +=
            mutation.viewports[index].Width * (1.0f - scaleX) * 0.5f;
        adjusted[index].TopLeftY +=
            mutation.viewports[index].Height * (1.0f - scaleY) * 0.5f -
            verticalOffset;
        adjusted[index].Width = mutation.viewports[index].Width * scaleX;
        adjusted[index].Height = mutation.viewports[index].Height * scaleY;
    }
    context->RSSetViewports(mutation.viewportCount, adjusted.data());
    mutation.scissorCount = 0;
    mutation.kind = Halo2HudDrawMutation::Kind::Raster;
    return mutation;
}

static void EndHalo2HudDraw(
    ID3D11DeviceContext* context, Halo2HudDrawMutation& mutation) noexcept
{
    if (mutation.kind == Halo2HudDrawMutation::Kind::NativeCrosshair)
    {
        VR_EndAuthoredReticleCapture();
        const uint32_t generation =
            TitleAdapter_GetGeneration(GameTitle::Halo2);
        if (generation)
            g_halo2NativeCrosshairGeneration.store(
                generation, std::memory_order_release);
        g_halo2NativeCrosshairDraws.fetch_add(
            1, std::memory_order_relaxed);
    }
    else if (mutation.kind == Halo2HudDrawMutation::Kind::Raster)
    {
        context->RSSetViewports(mutation.viewportCount, mutation.viewports);
        if (mutation.scissorCount)
            context->RSSetScissorRects(
                mutation.scissorCount, mutation.scissors);
        g_halo2HudRasterDraws.fetch_add(1, std::memory_order_relaxed);
    }
}

// Exact shader identities keep this hook out of world/weapon/menu draws.
// Stage 3F removes Stage 3E's disproven native-CHUD TLS conjunction and
// mutates the actual context that reaches Draw/DrawIndexed. Every changed
// viewport is restored immediately after the draw.
static void STDMETHODCALLTYPE Halo2DrawIndexedCensusHook(
    ID3D11DeviceContext* context, UINT indexCount, UINT startIndex,
    INT baseVertex)
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        VR_Halo2NoteDraw();
    if(hud_visibility::Hidden()) return;
    Halo2HudDrawMutation mutation = BeginHalo2HudDraw(context);
    if(mutation.kind==Halo2HudDrawMutation::Kind::Hidden) return;
    VR_Halo4PrepareAuthoredReticleDraw(context);
    {
        BloomDrawScope bloom(context);
        g_origHalo2DrawIndexed(context, indexCount, startIndex, baseVertex);
    }
    EndHalo2HudDraw(context, mutation);
}

static void STDMETHODCALLTYPE Halo2DrawCensusHook(
    ID3D11DeviceContext* context, UINT vertexCount, UINT startVertex)
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        VR_Halo2NoteDraw();
    if(hud_visibility::Hidden()) return;
    Halo2HudDrawMutation mutation = BeginHalo2HudDraw(context);
    if(mutation.kind==Halo2HudDrawMutation::Kind::Hidden) return;
    VR_Halo4PrepareAuthoredReticleDraw(context);
    {
        BloomDrawScope bloom(context);
        g_origHalo2Draw(context, vertexCount, startVertex);
    }
    EndHalo2HudDraw(context, mutation);
}
#endif

static bool ShouldHideExtraHudDraw(ID3D11DeviceContext* context)
{
    if(hud_visibility::Hidden()) return true;
#if HALOMCCVR_HALO2_STEREO6DOF
    if(context&&g_halo2ShaderHooksAvailable.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==GameTitle::Halo2&&
        (Game_IsScopeRendering()||(g_config.hide_hud&&Game_IsHeadTracking()&&Game_MoveStickIsLocomotion())))
    {
        ID3D11PixelShader* shader=nullptr;UINT count=0;
        context->PSGetShader(&shader,nullptr,&count);
        const auto role=LookupHalo2PixelShader(shader);
        if(shader)shader->Release();
        return role==halo2_hud_shader::Role::Crosshair||role==halo2_hud_shader::Role::GameplayHud;
    }
#endif
    return false;
}
#include "hud_extra_draws.inl"

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
typedef void(STDMETHODCALLTYPE* PixelShaderSetFn)(
    ID3D11DeviceContext*, ID3D11PixelShader*,
    ID3D11ClassInstance* const*, UINT);
static PixelShaderSetFn g_origPixelShaderSet = nullptr;
static std::atomic<bool> g_halo4HelmetShaderPathAvailable{false};
// The working V6 donor owns one exact live visor shader pointer. Overwrite it
// when MCC recreates the shader on a later device/level instead of retaining a
// bounded collection of stale COM identities that could eventually fill up.
static std::atomic<uintptr_t> g_halo4HelmetShader{0};
static std::atomic<uint64_t> g_halo4HelmetShadersRegistered{0};
static std::atomic<uint64_t> g_halo4HelmetShaderSuppressions{0};
static std::atomic<bool> g_halo4ScreenEffectShaderPathAvailable{false};
static std::atomic<uintptr_t> g_halo4MotionSuckShader{0};
static std::atomic<uint64_t> g_halo4MotionSuckShadersRegistered{0};
static std::atomic<uint64_t> g_halo4MotionSuckShaderSuppressions{0};

static bool RegisterHalo4HelmetShader(
    ID3D11PixelShader* shader, bool visorFraming) noexcept
{
    if (!shader)
        return false;
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(shader);
    if (!visorFraming)
    {
        uintptr_t registered = pointer;
        (void)g_halo4HelmetShader.compare_exchange_strong(
            registered, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        return false;
    }
    return g_halo4HelmetShader.exchange(
               pointer, std::memory_order_acq_rel) != pointer;
}

static bool IsHalo4HelmetShader(ID3D11PixelShader* shader) noexcept
{
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(shader);
    return pointer && g_halo4HelmetShader.load(
        std::memory_order_acquire) == pointer;
}

static bool RegisterHalo4MotionSuckShader(
    ID3D11PixelShader* shader, bool motionSuck) noexcept
{
    if (!shader)
        return false;
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(shader);
    if (!motionSuck)
    {
        uintptr_t registered = pointer;
        (void)g_halo4MotionSuckShader.compare_exchange_strong(
            registered, 0, std::memory_order_acq_rel,
            std::memory_order_acquire);
        return false;
    }
    return g_halo4MotionSuckShader.exchange(
               pointer, std::memory_order_acq_rel) != pointer;
}

static bool IsHalo4MotionSuckShader(ID3D11PixelShader* shader) noexcept
{
    const uintptr_t pointer = reinterpret_cast<uintptr_t>(shader);
    return pointer && g_halo4MotionSuckShader.load(
        std::memory_order_acquire) == pointer;
}

static void STDMETHODCALLTYPE PixelShaderSetHook(
    ID3D11DeviceContext* context, ID3D11PixelShader* shader,
    ID3D11ClassInstance* const* classInstances, UINT classInstanceCount)
{
    Bloom_ObserveShader(context,shader,classInstanceCount);
    const bool suppressMotionSuck =
        halo4_screen_effect_shader::ShouldSuppress(
            g_halo4ScreenEffectShaderPathAvailable.load(
                std::memory_order_acquire),
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4,
            VR_IsStereoEnabled(), IsHalo4MotionSuckShader(shader));
    if (suppressMotionSuck)
    {
        // A null pixel shader makes this one full-screen draw write nothing,
        // preserving the already-rendered eye. Class instances remain exactly
        // as authored, matching the independently accepted visor bridge.
        g_origPixelShaderSet(
            context, nullptr, classInstances, classInstanceCount);
        g_halo4MotionSuckShaderSuppressions.fetch_add(
            1, std::memory_order_relaxed);
        return;
    }

    const bool suppress = halo4_helmet_shader::ShouldSuppress(
        g_halo4HelmetShaderPathAvailable.load(std::memory_order_acquire),
        TitleAdapter_GetActiveTitle() == GameTitle::Halo4,
        g_config.halo4_helmet && !g_config.hide_hud, IsHalo4HelmetShader(shader));
    if (suppress)
    {
        // Match the working V6 bridge exactly: replace only the shader pointer
        // and preserve the caller's class-instance pointer/count unchanged.
        g_origPixelShaderSet(
            context, nullptr, classInstances, classInstanceCount);
        g_halo4HelmetShaderSuppressions.fetch_add(
            1, std::memory_order_relaxed);
        return;
    }
    g_origPixelShaderSet(
        context, shader, classInstances, classInstanceCount);
}
#endif

#if HALOMCCVR_HALO2_STEREO6DOF || HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
static HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(
    ID3D11Device* device, const void* bytecode, SIZE_T bytecodeLength,
    ID3D11ClassLinkage* linkage, ID3D11PixelShader** shader)
{
    const HRESULT result = g_origCreatePixelShader(
        device, bytecode, bytecodeLength, linkage, shader);
    if (FAILED(result) || !shader || !*shader || !bytecode || !bytecodeLength)
        return result;
    Bloom_ObserveShaderCreated(*shader,bytecode,bytecodeLength);

#if HALOMCCVR_HALO2_STEREO6DOF
    const uint64_t hash =
        halo2_hud_shader::MigotoFnv1(bytecode, bytecodeLength);
    const halo2_hud_shader::Role role =
        halo2_hud_shader::Classify(hash);
    const bool newlyRegistered = RegisterHalo2PixelShader(*shader, role);
    if (role != halo2_hud_shader::Role::Other && newlyRegistered)
    {
        (role == halo2_hud_shader::Role::Crosshair
             ? g_halo2CrosshairShadersRegistered
             : g_halo2GameplayShadersRegistered)
            .fetch_add(1, std::memory_order_relaxed);
    }
#else
    const uint64_t hash =
        halo4_helmet_shader::MigotoFnv1(bytecode, bytecodeLength);
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    if (RegisterHalo4HelmetShader(
            *shader, halo4_helmet_shader::IsVisorFramingShader(hash)))
    {
        g_halo4HelmetShadersRegistered.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (RegisterHalo4MotionSuckShader(
            *shader,
            halo4_screen_effect_shader::IsMotionSuckShader(hash)))
    {
        g_halo4MotionSuckShadersRegistered.fetch_add(
            1, std::memory_order_relaxed);
    }
#endif
    return result;
}
#endif

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA && !HALOMCCVR_HALO2_STEREO6DOF
typedef void(STDMETHODCALLTYPE* Halo4DrawIndexedFn)(
    ID3D11DeviceContext*, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE* Halo4DrawFn)(ID3D11DeviceContext*, UINT, UINT);
static Halo4DrawIndexedFn g_origHalo4DrawIndexed = nullptr;
static Halo4DrawFn g_origHalo4Draw = nullptr;

static void STDMETHODCALLTYPE Halo4DrawIndexedFramingHook(
    ID3D11DeviceContext* context, UINT indexCount, UINT startIndex,
    INT baseVertex)
{
    VR_Halo4PrepareAuthoredReticleDraw(context);
    BloomDrawScope bloom(context);
    g_origHalo4DrawIndexed(context, indexCount, startIndex, baseVertex);
}

static void STDMETHODCALLTYPE Halo4DrawFramingHook(
    ID3D11DeviceContext* context, UINT vertexCount, UINT startVertex)
{
    VR_Halo4PrepareAuthoredReticleDraw(context);
    BloomDrawScope bloom(context);
    g_origHalo4Draw(context, vertexCount, startVertex);
}
#endif

bool D3D_Halo2NativeCrosshairCaptured()
{
#if HALOMCCVR_HALO2_STEREO6DOF
    const uint32_t generation =
        TitleAdapter_GetGeneration(GameTitle::Halo2);
    return generation != 0 &&
        TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
        g_halo2ShaderHooksAvailable.load(std::memory_order_acquire) &&
        g_halo2NativeCrosshairGeneration.load(std::memory_order_acquire) ==
            generation;
#else
    return false;
#endif
}

bool D3D_Halo2HudShaderPathAvailable()
{
#if HALOMCCVR_HALO2_STEREO6DOF
    return g_halo2ShaderHooksAvailable.load(std::memory_order_acquire);
#else
    return false;
#endif
}

bool D3D_Halo4AuthoredReticleDrawPathAvailable()
{
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    return g_halo4AuthoredReticleDrawPathAvailable.load(
        std::memory_order_acquire);
#else
    return false;
#endif
}

bool D3D_Halo4HelmetShaderPathAvailable()
{
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    return g_halo4HelmetShaderPathAvailable.load(std::memory_order_acquire);
#else
    return false;
#endif
}

bool D3D_Halo4ScreenEffectShaderPathAvailable()
{
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    return g_halo4ScreenEffectShaderPathAvailable.load(
        std::memory_order_acquire);
#else
    return false;
#endif
}

void D3D_GetHalo4HelmetTelemetry(
    uint64_t& shadersRegistered, uint64_t& suppressions)
{
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    shadersRegistered = g_halo4HelmetShadersRegistered.load(
        std::memory_order_relaxed);
    suppressions = g_halo4HelmetShaderSuppressions.exchange(
        0, std::memory_order_relaxed);
#else
    shadersRegistered = 0;
    suppressions = 0;
#endif
}

void D3D_GetHalo4ScreenEffectTelemetry(
    uint64_t& shadersRegistered, uint64_t& suppressions)
{
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    shadersRegistered = g_halo4MotionSuckShadersRegistered.load(
        std::memory_order_relaxed);
    suppressions = g_halo4MotionSuckShaderSuppressions.exchange(
        0, std::memory_order_relaxed);
#else
    shadersRegistered = 0;
    suppressions = 0;
#endif
}

void D3D_GetHalo2HudTelemetry(
    uint64_t& gameplayShadersRegistered,
    uint64_t& crosshairShadersRegistered,
    uint64_t& rasterDraws,
    uint64_t& nativeCrosshairDraws,
    uint64_t& stateFailures)
{
#if HALOMCCVR_HALO2_STEREO6DOF
    gameplayShadersRegistered =
        g_halo2GameplayShadersRegistered.load(std::memory_order_relaxed);
    crosshairShadersRegistered =
        g_halo2CrosshairShadersRegistered.load(std::memory_order_relaxed);
    rasterDraws = g_halo2HudRasterDraws.load(std::memory_order_relaxed);
    nativeCrosshairDraws =
        g_halo2NativeCrosshairDraws.load(std::memory_order_relaxed);
    stateFailures = g_halo2HudStateFailures.load(std::memory_order_relaxed);
#else
    gameplayShadersRegistered = 0;
    crosshairShadersRegistered = 0;
    rasterDraws = 0;
    nativeCrosshairDraws = 0;
    stateFailures = 0;
#endif
}

static PresentFn g_origPresent = nullptr;
static Present1Fn g_origPresent1 = nullptr;
static ResizeBuffersFn g_origResizeBuffers = nullptr;
static OMSetRenderTargetsFn g_origOMSetRenderTargets = nullptr;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
static DrawIndexedFn g_origDrawIndexed = nullptr;
// The July 26 HUD-discovery detour performed synchronous GPU readback and
// logging before DrawIndexed. It produced no HUD/world evidence and remained
// active in every later Reach build. Candidate a50da4a proved removing it does
// not change the black-world defect. It remains disabled because restoring
// blocking GPU readback and file I/O to a render hot hook would be unsafe.
constexpr bool kEnableReachDrawIndexedDiagnostic = false;
#endif
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
static CopyResourceFn g_origCopyResource = nullptr;
#endif

// --- Desktop-window fit (config.fit_desktop_window) -----------------------
// resolution_scale sizes the render the headset captures. On a monitor smaller
// than that render, MCC's window overflows the screen and its menu buttons fall
// off the edge. With the fit on we keep MCC drawing the FULL render (so the
// headset picture and the gun alignment never change) while shrinking only the
// visible window to fit the monitor (menu.cpp); the GPU downscales the full
// backbuffer into the small window on present (flip-model + DXGI_SCALING_STRETCH)
// for free -- no second pass. The hazard is the reverse of the earlier attempt:
// if MCC learns the window shrank it draws a small frame into the corner of the
// big backbuffer (the black-border crop). So we force the backbuffer full at
// creation and keep MCC believing its client is still full-size through its own
// resize handling -- WITHOUT lying to DXGI's present-time client query, which
// must still see the true small window to downscale correctly. Everything below
// is gated on g_config.fit_desktop_window; with it off, none of it is installed.
typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChainForHwndFn)(IDXGIFactory2*, IUnknown*, HWND,
    const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
    IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChainFn)(IDXGIFactory*, IUnknown*,
    DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef BOOL(WINAPI* GetClientRectFn)(HWND, LPRECT);
typedef BOOL(WINAPI* GetWindowRectFn)(HWND, LPRECT);
static CreateSwapChainForHwndFn g_origCreateSwapChainForHwnd = nullptr;
static CreateSwapChainFn g_origCreateSwapChain = nullptr;
static GetClientRectFn g_origGetClientRect = nullptr;
static GetWindowRectFn g_origGetWindowRect = nullptr;
static std::atomic<UINT> g_forcedRenderW{0};
static std::atomic<UINT> g_forcedRenderH{0};
static bool g_forcedMainSwapchain = false; // only force the game's own (first) swapchain
static HWND g_gameHwnd = nullptr;          // captured at swapchain creation
static bool g_fitActive = false;           // set once at startup: fit on AND its hooks installed
// Set on the game's UI thread ONLY while it synchronously processes a WM_SIZE we
// rewrote to the full render size, so GetClientRectHook feeds MCC's own resize
// code the full size on exactly that call stack -- never on the render thread's
// present-time query, which must keep getting the true small client size.
static thread_local bool g_lieClientToGame = false;

void D3D_GetForcedRenderSize(unsigned& width, unsigned& height)
{
    width = g_forcedRenderW;
    height = g_forcedRenderH;
}

bool D3D_FitActive()
{
    return g_fitActive;
}

void D3D_SetForcedClientLie(bool on)
{
    g_lieClientToGame = on;
}

// --- Render plan (one game start's sizes) ----------------------------------
// The game's render size and the headset picture size, decided once at startup
// exactly as the launcher decided them (same config, same dlss::PlanRender):
// the launcher's numbers arrive in the environment on Steam; the Store edition
// starts without our environment and recomputes the identical values. With
// DLSS selected and its library beside the mod, the render is smaller than the
// picture and DLSS makes the picture at run time; otherwise both are the
// resolution_scale size, byte-for-byte the pre-DLSS behaviour.
static std::atomic<UINT> g_planRenderW{0};
static std::atomic<UINT> g_planRenderH{0};
static std::atomic<UINT> g_planOutputW{0};
static std::atomic<UINT> g_planOutputH{0};
static std::atomic<bool> g_planDlss{false};
static bool g_planDlssRuntimePresent = false;

static bool DlssRuntimeBesideMod()
{
    const wchar_t* dir = LogDirectory();
    if (!dir || !*dir)
        return false;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s", dir);
    size_t length = wcslen(path);
    while (length > 1 && (path[length - 1] == L'\\' || path[length - 1] == L'/'))
        path[--length] = L'\0';
    wcscat_s(path, L"\\nvngx_dlss.dll");
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void InitRenderPlan()
{
    g_planDlssRuntimePresent = DlssRuntimeBesideMod();
    int nativeW = 0, nativeH = 0;
    nativeW = kNativeRenderWidth; nativeH = kNativeRenderHeight;
    const dlss::RenderPlan worldPlan = dlss::PlanRender(
        nativeW, nativeH, g_config.resolution_scale,
        g_config.upscaler, g_config.dlss_mode, g_planDlssRuntimePresent);
    const dlss::RenderPlan plan = dlss::PlanPresentation(worldPlan, RuntimeMode::Shell);
    g_planRenderW = (UINT)plan.renderW;
    g_planRenderH = (UINT)plan.renderH;
    g_planOutputW = (UINT)plan.outputW;
    g_planOutputH = (UINT)plan.outputH;
    g_planDlss = plan.dlss;
    // The launcher's exact numbers win when present (they were computed from
    // the same config by the same function; this only guards against a config
    // edited between launch and injection).
    wchar_t buf[32];
    UINT envRenderW = 0, envRenderH = 0, envOutputW = 0, envOutputH = 0;
    if (GetEnvironmentVariableW(L"HALO3XR_RENDER_W", buf, 32) > 0)
        envRenderW = (UINT)_wtoi(buf);
    if (GetEnvironmentVariableW(L"HALO3XR_RENDER_H", buf, 32) > 0)
        envRenderH = (UINT)_wtoi(buf);
    if (GetEnvironmentVariableW(L"HALO3XR_OUTPUT_W", buf, 32) > 0)
        envOutputW = (UINT)_wtoi(buf);
    if (GetEnvironmentVariableW(L"HALO3XR_OUTPUT_H", buf, 32) > 0)
        envOutputH = (UINT)_wtoi(buf);
    if (envRenderW && envRenderH)
    {
        g_planRenderW = envRenderW;
        g_planRenderH = envRenderH;
    }
    if (envOutputW && envOutputH)
    {
        g_planOutputW = envOutputW;
        g_planOutputH = envOutputH;
    }
    // Even an old launcher's reduced input is not a valid shell canvas.
    // Preserve its configured output, but start render/metrics at that output.
    g_planRenderW = g_planOutputW.load();
    g_planRenderH = g_planOutputH.load();
    g_planDlss = false;
    LOG("render plan: game renders %ux%u, headset picture %ux%u (%s%s)",
        g_planRenderW.load(), g_planRenderH.load(), g_planOutputW.load(), g_planOutputH.load(),
        worldPlan.dlss ? "shell full-size; world DLSS " : "",
        worldPlan.dlss ? dlss::QualityName(worldPlan.mode)
        : (g_config.upscaler == dlss::kUpscalerDlss
               ? "DLSS selected but nvngx_dlss.dll is not beside the mod; full-size render"
               : "DLSS off"));
}

void D3D_GetRenderPlan(unsigned& renderW, unsigned& renderH,
                       unsigned& outputW, unsigned& outputH,
                       bool& dlss, bool& dlssRuntimePresent)
{
    renderW = g_planRenderW;
    renderH = g_planRenderH;
    outputW = g_planOutputW;
    outputH = g_planOutputH;
    dlss = g_planDlss;
    dlssRuntimePresent = g_planDlssRuntimePresent;
}

// --- Live render resize -----------------------------------------------------
// The user changes Resolution scale or the DLSS mode in F1; the menu re-plans
// and calls D3D_RequestRenderPlan. With the fit active a plan is queued;
// loading admission is checked both here and by the window consumer before
// the forced size moves. The menu then sends the game window WM_SIZE on
// the UI thread, which WndProcHook rewrites to the forced size exactly as it
// does for a real window resize; MCC's resize code then re-sizes its
// swapchain (ResizeBuffersHook pins the size) and its internal targets, and
// the mod re-learns the backbuffer, eye caches and DLSS resources as it
// already does on every resize. Fail-open: if no ResizeBuffers to the
// requested size lands within kLiveResizeTimeoutMs, the forced size reverts
// to the real backbuffer so every client/metrics lie stays consistent, and
// the log and F1 say so.
static UINT g_liveResizeRequestedW = 0;
static UINT g_liveResizeRequestedH = 0;
static UINT g_liveResizeFailedW = 0;
static UINT g_liveResizeFailedH = 0;
static ULONGLONG g_liveResizeRequestMs = 0;
static dlss::LiveResizeDispatch g_liveResizeDispatch;
static dlss::RenderPlan g_liveResizeCommittedBeforeDispatch;
static UINT g_liveResizeActualW = 0;
static UINT g_liveResizeActualH = 0;
constexpr ULONGLONG kLiveResizeTimeoutMs = 4000;

D3DRenderPlanResult D3D_RequestRenderPlan(
    unsigned renderW, unsigned renderH, unsigned outputW, unsigned outputH,
    bool dlss)
{
    if (!renderW || !renderH || !outputW || !outputH)
        return D3DRenderPlanResult::Unchanged;
    static bool loggedLoadingDeferral = false;
    if (!dlss::CanDispatchLiveResize(TitleAdapter_GetRuntimeMode()))
    {
        if (!loggedLoadingDeferral)
        {
            LOG("live resize: deferred while the title is loading; current render dimensions remain in force");
            loggedLoadingDeferral = true;
        }
        return D3DRenderPlanResult::Deferred;
    }
    loggedLoadingDeferral = false;
    if (g_liveResizeDispatch.Pending())
        return D3DRenderPlanResult::Unchanged;
    // Do not publish a queued plan through window metrics. The UI consumer
    // checks lifecycle admission again before making the new size visible.
    if (g_fitActive && (renderW != g_forcedRenderW || renderH != g_forcedRenderH))
    {
        if (renderW == g_liveResizeFailedW && renderH == g_liveResizeFailedH)
            return D3DRenderPlanResult::Unchanged;
        dlss::RenderPlan request{};
        request.renderW = static_cast<int>(renderW);
        request.renderH = static_cast<int>(renderH);
        request.outputW = static_cast<int>(outputW);
        request.outputH = static_cast<int>(outputH);
        request.dlss = dlss;
        return g_liveResizeDispatch.Queue(request, TitleAdapter_GetRuntimeMode())
            ? D3DRenderPlanResult::Requested : D3DRenderPlanResult::Deferred;
    }
    g_planOutputW = outputW;
    g_planOutputH = outputH;
    g_planDlss = dlss;
    if (!g_fitActive)
    {
        // No forced size exists without the fit; the plan's render stays what
        // the launcher started, and the output still follows the config.
        static bool logged = false;
        if (!logged && dlss && (renderW != g_planRenderW || renderH != g_planRenderH))
        {
            logged = true;
            LOG("DLSS: cannot apply world input size without desktop-fit resize hooks; "
                "enable Fit desktop window and restart. Keeping the full-size render.");
        }
        return D3DRenderPlanResult::RestartNeeded;
    }
    if (renderW == g_forcedRenderW && renderH == g_forcedRenderH)
    {
        g_planRenderW = renderW;
        g_planRenderH = renderH;
        return D3DRenderPlanResult::Unchanged;
    }
    return D3DRenderPlanResult::Unchanged;
}

bool D3D_BeginLiveResize(unsigned& renderW, unsigned& renderH)
{
    dlss::RenderPlan request{};
    const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
    if (!g_liveResizeDispatch.Begin(mode, request))
    {
        if (mode == RuntimeMode::Loading)
            LOG("live resize: queued request cancelled before dimension publication because the title entered loading");
        return false;
    }
    renderW = static_cast<unsigned>(request.renderW);
    renderH = static_cast<unsigned>(request.renderH);
    const unsigned outputW = static_cast<unsigned>(request.outputW);
    const unsigned outputH = static_cast<unsigned>(request.outputH);
    const bool dlss = request.dlss;
    g_liveResizeCommittedBeforeDispatch.renderW = static_cast<int>(g_forcedRenderW.load());
    g_liveResizeCommittedBeforeDispatch.renderH = static_cast<int>(g_forcedRenderH.load());
    g_liveResizeCommittedBeforeDispatch.outputW = static_cast<int>(g_planOutputW.load());
    g_liveResizeCommittedBeforeDispatch.outputH = static_cast<int>(g_planOutputH.load());
    g_liveResizeCommittedBeforeDispatch.dlss = g_planDlss.load();
    g_planOutputW = outputW;
    g_planOutputH = outputH;
    g_planDlss = dlss;
    LOG("live resize: asking MCC to render %ux%u instead of %ux%u "
        "(headset picture %ux%u%s); waiting for its swapchain resize",
        renderW, renderH, g_forcedRenderW.load(), g_forcedRenderH.load(), outputW, outputH,
        dlss ? ", DLSS" : "");
    g_liveResizeRequestedW = renderW;
    g_liveResizeRequestedH = renderH;
    g_liveResizeRequestMs = GetTickCount64();
    g_forcedRenderW = renderW;
    g_forcedRenderH = renderH;
    g_planRenderW = renderW;
    g_planRenderH = renderH;
    g_liveResizeDispatch.Applied();
    return true;
}

void D3D_CancelQueuedLiveResize()
{
    // The UI may reject the admitted request before sending WM_SIZE (missing
    // window procedure, removed fit, or WORD-overflow dimensions). Begin has
    // already published the requested plan, so restore the committed one now;
    // waiting four seconds would expose dimensions the engine never received.
    if (g_liveResizeDispatch.Phase() == 1)
    {
        g_liveResizeFailedW = g_liveResizeRequestedW;
        g_liveResizeFailedH = g_liveResizeRequestedH;
        const auto& previous = g_liveResizeCommittedBeforeDispatch;
        g_forcedRenderW = g_planRenderW = previous.renderW;
        g_forcedRenderH = g_planRenderH = previous.renderH;
        g_planOutputW = previous.outputW;
        g_planOutputH = previous.outputH;
        g_planDlss = previous.dlss;
        g_liveResizeDispatch.Failed();
        LOG("live resize: window dispatch rejected; restored committed render %dx%d",
            previous.renderW, previous.renderH);
        return;
    }
    g_liveResizeDispatch.CancelQueued();
}

// Present-time check, only while a request is pending (one GetDesc a frame
// for at most the timeout).
static void CheckLiveResize(IDXGISwapChain* sc)
{
    if (g_liveResizeDispatch.Phase() != 1 || !sc)
        return;
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d)))
        return;
    g_liveResizeActualW = d.BufferDesc.Width;
    g_liveResizeActualH = d.BufferDesc.Height;
    if (d.BufferDesc.Width == g_liveResizeRequestedW &&
        d.BufferDesc.Height == g_liveResizeRequestedH)
    {
        LOG("live resize: MCC now renders %ux%u", d.BufferDesc.Width,
            d.BufferDesc.Height);
        g_liveResizeDispatch.Complete();
        return;
    }
    if (GetTickCount64() - g_liveResizeRequestMs < kLiveResizeTimeoutMs)
        return;
    LOG("live resize: MCC did not resize its swapchain to %ux%u within %llu ms "
        "(still %ux%u); keeping the real size, a restart applies the new one",
        g_liveResizeRequestedW, g_liveResizeRequestedH,
        static_cast<unsigned long long>(kLiveResizeTimeoutMs),
        d.BufferDesc.Width, d.BufferDesc.Height);
    g_liveResizeFailedW = g_liveResizeRequestedW;
    g_liveResizeFailedH = g_liveResizeRequestedH;
    g_forcedRenderW = d.BufferDesc.Width;
    g_forcedRenderH = d.BufferDesc.Height;
    g_planRenderW = d.BufferDesc.Width;
    g_planRenderH = d.BufferDesc.Height;
    g_liveResizeDispatch.Failed();
}

int D3D_LiveResizeState(unsigned& actualW, unsigned& actualH)
{
    actualW = g_liveResizeActualW;
    actualH = g_liveResizeActualH;
    return g_liveResizeDispatch.Status();
}

HWND D3D_GameWindow()
{
    return g_gameHwnd;
}

static void InitForcedRenderSize()
{
    // The plan already folded in the launcher's exact numbers when present
    // and otherwise replicated the launcher's arithmetic, so the two can never
    // disagree. Config is loaded before this (dllmain InitThread order).
    g_forcedRenderW = g_planRenderW.load();
    g_forcedRenderH = g_planRenderH.load();
    LOG("fit_desktop_window ON: forcing MCC backbuffer to %ux%u (full headset "
        "render); the desktop window is shrunk to fit the monitor separately",
        g_forcedRenderW.load(), g_forcedRenderH.load());
}

// Game-executable image range, used to scope the client-rect lie and cursor
// remap to game-side callers (never DXGI/DWM or our own overlay). Populated by
// InitExeRange() at hook install; the accessors are defined with the cursor
// hooks below.
static const BYTE* g_exeBase = nullptr;
static const BYTE* g_exeEnd = nullptr;
static inline bool CallerInExe(const void* ret); // defined with the cursor hooks

// Feed the game the full render size for the game window whenever the caller is
// game-side: MCC's resize code (so it keeps drawing full) AND its menu hit-test
// (which clamps the cursor against the client size -- if that returns the true
// small window while our cursor is scaled up to render space, only client/render
// of the menu is reachable, i.e. the top-left ~31% dead-zone). DXGI/DWM query
// the client at present time from system DLLs (not the game EXE), so they still
// get the real small size and keep downscaling the full frame into the window.
static BOOL WINAPI GetClientRectHook(HWND hwnd, LPRECT rc)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetClientRect(hwnd, rc);
    if (ok && rc && hwnd == g_gameHwnd && g_forcedRenderW && g_forcedRenderH &&
        (g_lieClientToGame || CallerInExe(caller)))
    {
        rc->left = 0;
        rc->top = 0;
        rc->right = (LONG)g_forcedRenderW;
        rc->bottom = (LONG)g_forcedRenderH;
        static int s_log = 0;
        static const void* s_lastCaller = nullptr;
        if (!g_lieClientToGame && s_log < 16 && caller != s_lastCaller &&
            g_exeBase)
        {
            ++s_log;
            s_lastCaller = caller;
            LOG("fit: GetClientRect game-side +0x%llX -> forced %ux%u",
                (unsigned long long)((const BYTE*)caller - g_exeBase),
                g_forcedRenderW.load(), g_forcedRenderH.load());
        }
    }
    return ok;
}

// UI input/focus layers commonly bound their hittable/navigable region to the
// window rect. Ours is the true small window (e.g. 141,0..1139,720), so MCC
// culls every widget below/right of it -> the menu is fully DRAWN but the lower
// items are dead to mouse, keyboard AND d-pad (the reported symptom). For
// game-side callers, report a window rect at the real top-left but sized to the
// full render, so MCC's input region matches its full-size layout and cursor.
// DXGI/DWM/system callers are untouched, so the physical window and the
// present-time downscale are unchanged.
static BOOL WINAPI GetWindowRectHook(HWND hwnd, LPRECT rc)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetWindowRect(hwnd, rc);
    if (ok && rc && hwnd == g_gameHwnd && g_forcedRenderW && g_forcedRenderH &&
        CallerInExe(caller))
    {
        // Keep the true top-left; extend to the full render extent.
        rc->right = rc->left + (LONG)g_forcedRenderW;
        rc->bottom = rc->top + (LONG)g_forcedRenderH;
        static int s_log = 0;
        static const void* s_lastCaller = nullptr;
        if (s_log < 16 && caller != s_lastCaller && g_exeBase)
        {
            ++s_log;
            s_lastCaller = caller;
            LOG("fit: GetWindowRect game-side +0x%llX -> (%ld,%ld,%ld,%ld)",
                (unsigned long long)((const BYTE*)caller - g_exeBase),
                rc->left, rc->top, rc->right, rc->bottom);
        }
    }
    return ok;
}

// --- Fitted-menu cursor coordinate remap -----------------------------------
// With the fit on, MCC DRAWS the full render (e.g. 3204x2310) but the visible
// window is shrunk to the monitor and the GPU downscales it on present. MCC
// lays out and hit-tests its native shell / pause menu in that full render
// space, yet the OS cursor that drives EVERY selection -- the mouse, and the
// gamepad/keyboard "virtual cursor" MCC's console-style shell moves for you --
// is confined to the small physical window. So only the top-left window-sized
// slice of the menu is reachable: the pointer only responds top-left, and
// keyboard/controller focus "moves but stops short" at the window edge. That is
// the exact reported symptom.
//
// Fix: make MCC's OS-cursor coordinate space match the fitted window in BOTH
// directions, and touch only calls that come from the game executable.
//   * GetCursorPos (MCC reads the cursor): scale the physical, window-confined
//     point UP into full render space, so the hit-test lands on the widget
//     directly under the visibly-downscaled cursor.
//   * SetCursorPos (MCC moves the cursor for gamepad/keyboard nav): scale its
//     render-space target back DOWN so the OS cursor stays inside the window
//     and the round-trip through GetCursorPos is exact.
//   * WindowFromPoint MUST keep seeing the TRUE physical point. MCC calls it
//     right after GetCursorPos to confirm the cursor is over its window; if it
//     saw our scaled-up point (which lands outside the small window) it decides
//     the cursor left and drops the input. That silent detail is what broke the
//     earlier broad GetCursorPos rewrite. We undo the remap for exactly the
//     value we last handed out, so no game address needs to be hardcoded.
typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
typedef BOOL(WINAPI* SetCursorPosFn)(int, int);
typedef HWND(WINAPI* WindowFromPointFn)(POINT);
typedef BOOL(WINAPI* ClipCursorFn)(const RECT*);
typedef int(WINAPI* GetSystemMetricsFn)(int);
typedef BOOL(WINAPI* GetMonitorInfoWFn)(HMONITOR, LPMONITORINFO);
static GetCursorPosFn g_origGetCursorPos = nullptr;
static SetCursorPosFn g_origSetCursorPos = nullptr;
static WindowFromPointFn g_origWindowFromPoint = nullptr;
static ClipCursorFn g_origClipCursor = nullptr;
static GetSystemMetricsFn g_origGetSystemMetrics = nullptr;
static GetMonitorInfoWFn g_origGetMonitorInfoW = nullptr;

// The shell/pause cursor consumers live in the game executable (RE-notes /
// RESOLUTION-FSR-INVESTIGATION static analysis). We only remap calls whose
// return address is inside that image, so our own ImGui overlay and any system
// DLL are never touched. (g_exeBase/g_exeEnd are declared above GetClientRectHook.)
static void InitExeRange()
{
    HMODULE h = GetModuleHandleW(nullptr);
    if (!h)
        return;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const BYTE*>(h) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    g_exeBase = reinterpret_cast<const BYTE*>(h);
    g_exeEnd = g_exeBase + nt->OptionalHeader.SizeOfImage;
}
static inline bool CallerInExe(const void* ret)
{
    return g_exeBase && ret >= static_cast<const void*>(g_exeBase) &&
           ret < static_cast<const void*>(g_exeEnd);
}

// True (unlied) fitted-window client origin (in screen space) and size. Uses
// the ORIGINAL GetClientRect so the WM_SIZE full-size lie can never leak in.
static bool FitClientMetrics(POINT& originScreen, LONG& clientW, LONG& clientH)
{
    if (!g_gameHwnd || !g_origGetClientRect)
        return false;
    RECT rc{};
    if (!g_origGetClientRect(g_gameHwnd, &rc))
        return false;
    clientW = rc.right - rc.left;
    clientH = rc.bottom - rc.top;
    if (clientW <= 0 || clientH <= 0)
        return false;
    originScreen.x = 0;
    originScreen.y = 0;
    return ClientToScreen(g_gameHwnd, &originScreen) != FALSE;
}

static BOOL WINAPI GetCursorPosHook(LPPOINT p)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetCursorPos(p);
    if (!ok || !p || !g_fitActive || !g_forcedRenderW || !g_forcedRenderH ||
        !CallerInExe(caller))
        return ok;
    POINT origin{};
    LONG cw = 0, ch = 0;
    if (!FitClientMetrics(origin, cw, ch))
        return ok;
    // Only remap while the cursor is actually over the fitted window.
    if (p->x < origin.x || p->y < origin.y ||
        p->x >= origin.x + cw || p->y >= origin.y + ch)
        return ok;
    const POINT phys = *p;
    POINT mapped;
    mapped.x = origin.x +
               (LONG)llround((double)(phys.x - origin.x) * g_forcedRenderW / cw);
    mapped.y = origin.y +
               (LONG)llround((double)(phys.y - origin.y) * g_forcedRenderH / ch);
    *p = mapped;
    // Log only when the physical cursor actually MOVES (and only once the
    // window is shrunk), so the budget records real navigation instead of 40
    // copies of one idle frame.
    static int s_log = 0;
    static POINT s_lastLogged{LONG_MIN, LONG_MIN};
    if (cw < (LONG)g_forcedRenderW && s_log < 60 &&
        (phys.x != s_lastLogged.x || phys.y != s_lastLogged.y))
    {
        ++s_log;
        s_lastLogged = phys;
        LOG("fit: menu cursor read +0x%llX phys(%ld,%ld) -> render(%ld,%ld) "
            "client %ldx%ld",
            (unsigned long long)((const BYTE*)caller - g_exeBase),
            phys.x, phys.y, mapped.x, mapped.y, cw, ch);
    }
    return ok;
}

static BOOL WINAPI SetCursorPosHook(int X, int Y)
{
    const void* caller = _ReturnAddress();
    if (!g_fitActive || !g_forcedRenderW || !g_forcedRenderH ||
        !CallerInExe(caller))
        return g_origSetCursorPos(X, Y);
    POINT origin{};
    LONG cw = 0, ch = 0;
    if (!FitClientMetrics(origin, cw, ch))
        return g_origSetCursorPos(X, Y);
    // MCC targets the cursor in its own (full render) client space. Convert
    // points that fall inside that render rectangle back into the small window
    // so the OS cursor lands where MCC intends; leave anything else untouched.
    const LONG relX = X - origin.x;
    const LONG relY = Y - origin.y;
    if (relX < 0 || relY < 0 ||
        relX > (LONG)g_forcedRenderW || relY > (LONG)g_forcedRenderH)
        return g_origSetCursorPos(X, Y);
    const int px = origin.x + (int)llround((double)relX * cw / g_forcedRenderW);
    const int py = origin.y + (int)llround((double)relY * ch / g_forcedRenderH);
    static int s_log = 0;
    static POINT s_lastLogged{LONG_MIN, LONG_MIN};
    if (cw < (LONG)g_forcedRenderW && s_log < 60 &&
        (X != s_lastLogged.x || Y != s_lastLogged.y))
    {
        ++s_log;
        s_lastLogged.x = X;
        s_lastLogged.y = Y;
        LOG("fit: menu cursor move +0x%llX render(%d,%d) -> phys(%d,%d)",
            (unsigned long long)((const BYTE*)caller - g_exeBase), X, Y, px, py);
    }
    return g_origSetCursorPos(px, py);
}

// If MCC confines the cursor (ClipCursor) to a rectangle expressed in its
// believed full-render space, the OS clips the physical cursor to that (often
// off-screen) rectangle and you can't move the pointer to the lower menu items
// at all. Fold any render-space clip rect back into the real window client so
// the cursor stays free across the whole fitted menu. Fail-open: on anything
// unexpected we pass the request through untouched.
static BOOL WINAPI ClipCursorHook(const RECT* rc)
{
    if (!g_fitActive || !g_forcedRenderW || !g_forcedRenderH || !rc)
        return g_origClipCursor(rc);
    POINT origin{};
    LONG cw = 0, ch = 0;
    if (!FitClientMetrics(origin, cw, ch))
        return g_origClipCursor(rc);
    RECT mapped = *rc;
    auto foldX = [&](LONG v) {
        LONG r = v - origin.x;
        if (r < 0) r = 0;
        if (r > (LONG)g_forcedRenderW) r = (LONG)g_forcedRenderW;
        return origin.x + (LONG)llround((double)r * cw / g_forcedRenderW);
    };
    auto foldY = [&](LONG v) {
        LONG r = v - origin.y;
        if (r < 0) r = 0;
        if (r > (LONG)g_forcedRenderH) r = (LONG)g_forcedRenderH;
        return origin.y + (LONG)llround((double)r * ch / g_forcedRenderH);
    };
    mapped.left = foldX(rc->left);
    mapped.right = foldX(rc->right);
    mapped.top = foldY(rc->top);
    mapped.bottom = foldY(rc->bottom);
    static int s_log = 0;
    if (cw < (LONG)g_forcedRenderW && s_log < 12)
    {
        ++s_log;
        LOG("fit: menu cursor clip (%ld,%ld,%ld,%ld) -> (%ld,%ld,%ld,%ld)",
            rc->left, rc->top, rc->right, rc->bottom,
            mapped.left, mapped.top, mapped.right, mapped.bottom);
    }
    return g_origClipCursor(&mapped);
}

static HWND WINAPI WindowFromPointHook(POINT pt)
{
    // MCC hit-tests its menu in full render space, so it asks WindowFromPoint
    // about points spread across the *believed* 3204x2310 client -- most of
    // which fall OFF the real, shrunk window (and often off the monitor). Those
    // resolve to "not my window" and the menu item dies; only points that happen
    // to still land on the small top-left slice work. Map ANY point inside the
    // render rectangle back down into the real window client before the OS
    // answers, so every menu item resolves to the game window like it should.
    if (g_fitActive && g_forcedRenderW && g_forcedRenderH)
    {
        POINT origin{};
        LONG cw = 0, ch = 0;
        if (FitClientMetrics(origin, cw, ch))
        {
            const LONG rx = pt.x - origin.x;
            const LONG ry = pt.y - origin.y;
            if (rx >= 0 && ry >= 0 && rx <= (LONG)g_forcedRenderW &&
                ry <= (LONG)g_forcedRenderH)
            {
                pt.x = origin.x +
                       (LONG)llround((double)rx * cw / g_forcedRenderW);
                pt.y = origin.y +
                       (LONG)llround((double)ry * ch / g_forcedRenderH);
            }
        }
    }
    return g_origWindowFromPoint(pt);
}

// THE root cause of the "menu items are drawn but dead below a certain row"
// symptom (which happens with the fit ON *or* OFF, because it is driven by the
// desktop resolution, not our window): MCC lays out and clips its native shell /
// pause menu against the size it believes the SCREEN is. It asks Windows via
// GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN) and GetMonitorInfo, and Windows
// truthfully answers the real desktop size (e.g. 1280x720). But MCC renders the
// menu across the full headset canvas (e.g. 3204x2310), so every widget below
// the real screen height is treated as off-screen and refuses focus -- for the
// mouse, the keyboard, AND the d-pad alike (they all obey this one clip).
//
// Fix: for game-side callers only, report the full render size as the screen /
// monitor size so MCC's layout-vs-viewport clip matches what it actually draws.
// The compositor, DXGI and our own fit math call these from system DLLs / this
// DLL (not the game EXE), so they keep the true desktop size and the display
// downscale is unchanged. Works at ANY desktop resolution or aspect ratio
// because we always hand back the current forced render extent, never a constant.
static int WINAPI GetSystemMetricsHook(int index)
{
    const void* caller = _ReturnAddress();
    const int real = g_origGetSystemMetrics(index);
    if (!g_forcedRenderW || !g_forcedRenderH || !CallerInExe(caller))
        return real;
    // Only the primary-screen extent metrics; every other index passes through.
    int lied = real;
    if (index == SM_CXSCREEN || index == SM_CXFULLSCREEN || index == SM_CXVIRTUALSCREEN)
        lied = (int)g_forcedRenderW;
    else if (index == SM_CYSCREEN || index == SM_CYFULLSCREEN || index == SM_CYVIRTUALSCREEN)
        lied = (int)g_forcedRenderH;
    else
        return real;
    static int s_log = 0;
    static const void* s_lastCaller = nullptr;
    if (s_log < 16 && caller != s_lastCaller && g_exeBase)
    {
        ++s_log;
        s_lastCaller = caller;
        LOG("fit: GetSystemMetrics(%d) game-side +0x%llX -> %d (real %d)",
            index, (unsigned long long)((const BYTE*)caller - g_exeBase), lied, real);
    }
    return lied;
}

static BOOL WINAPI GetMonitorInfoWHook(HMONITOR mon, LPMONITORINFO mi)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetMonitorInfoW(mon, mi);
    if (!ok || !mi || !g_forcedRenderW || !g_forcedRenderH || !CallerInExe(caller))
        return ok;
    // Only rewrite the monitor that hosts the game window; leave others intact so
    // multi-monitor geometry MCC may query for other reasons stays truthful.
    if (g_gameHwnd &&
        MonitorFromWindow(g_gameHwnd, MONITOR_DEFAULTTONEAREST) != mon)
        return ok;
    // Extend both the monitor and work rects from their real top-left to the full
    // render extent, so MCC's fullscreen/menu layout treats the screen as large as
    // what it draws. Keeps the true origin (multi-monitor offsets stay correct).
    mi->rcMonitor.right = mi->rcMonitor.left + (LONG)g_forcedRenderW;
    mi->rcMonitor.bottom = mi->rcMonitor.top + (LONG)g_forcedRenderH;
    mi->rcWork.right = mi->rcWork.left + (LONG)g_forcedRenderW;
    mi->rcWork.bottom = mi->rcWork.top + (LONG)g_forcedRenderH;
    static int s_log = 0;
    static const void* s_lastCaller = nullptr;
    if (s_log < 16 && caller != s_lastCaller && g_exeBase)
    {
        ++s_log;
        s_lastCaller = caller;
        LOG("fit: GetMonitorInfo game-side +0x%llX -> monitor %ldx%ld work %ldx%ld",
            (unsigned long long)((const BYTE*)caller - g_exeBase),
            mi->rcMonitor.right - mi->rcMonitor.left,
            mi->rcMonitor.bottom - mi->rcMonitor.top,
            mi->rcWork.right - mi->rcWork.left,
            mi->rcWork.bottom - mi->rcWork.top);
    }
    return ok;
}

static HRESULT STDMETHODCALLTYPE CreateSwapChainForHwndHook(IDXGIFactory2* self, IUnknown* device,
    HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreen, IDXGIOutput* restrictOut,
    IDXGISwapChain1** ppSwapChain)
{
    if (pDesc && g_forcedRenderW && g_forcedRenderH && !g_forcedMainSwapchain)
    {
        g_forcedMainSwapchain = true;
        g_gameHwnd = hwnd;
        DXGI_SWAP_CHAIN_DESC1 desc = *pDesc;
        LOG("fit: CreateSwapChainForHwnd MCC requested %ux%u scaling=%d hwnd=%p "
            "-> forcing backbuffer %ux%u STRETCH",
            pDesc->Width, pDesc->Height, (int)pDesc->Scaling, (void*)hwnd,
            g_forcedRenderW.load(), g_forcedRenderH.load());
        desc.Width = g_forcedRenderW;
        desc.Height = g_forcedRenderH;
        desc.Scaling = DXGI_SCALING_STRETCH;
        const HRESULT hr = g_origCreateSwapChainForHwnd(self, device, hwnd, &desc,
                                                        pFullscreen, restrictOut, ppSwapChain);
        if (FAILED(hr))
            LOG("fit: forced CreateSwapChainForHwnd FAILED (hr=0x%08X); the fit did "
                "NOT apply on this machine", (unsigned)hr);
        return hr;
    }
    return g_origCreateSwapChainForHwnd(self, device, hwnd, pDesc, pFullscreen,
                                        restrictOut, ppSwapChain);
}

static HRESULT STDMETHODCALLTYPE CreateSwapChainHook(IDXGIFactory* self, IUnknown* device,
    DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
    if (pDesc && g_forcedRenderW && g_forcedRenderH && !g_forcedMainSwapchain)
    {
        g_forcedMainSwapchain = true;
        g_gameHwnd = pDesc->OutputWindow;
        DXGI_SWAP_CHAIN_DESC desc = *pDesc;
        LOG("fit: CreateSwapChain(legacy) MCC requested %ux%u -> forcing %ux%u",
            pDesc->BufferDesc.Width, pDesc->BufferDesc.Height,
            g_forcedRenderW.load(), g_forcedRenderH.load());
        desc.BufferDesc.Width = g_forcedRenderW;
        desc.BufferDesc.Height = g_forcedRenderH;
        const HRESULT hr = g_origCreateSwapChain(self, device, &desc, ppSwapChain);
        if (FAILED(hr))
            LOG("fit: forced CreateSwapChain FAILED (hr=0x%08X); the fit did NOT "
                "apply on this machine", (unsigned)hr);
        return hr;
    }
    return g_origCreateSwapChain(self, device, pDesc, ppSwapChain);
}

// Broad probe paths deliberately NOT installed here; each was retired after
// theory. Re-adding any of them costs frame time for information we already
// have:
//   UpdateSubresource/Map/Unmap - constant census sagged fps ~25%; the
//     exact-match matrix matcher scored zero hits in a full session.
//   CopySubresourceRegion and frame-wide CopyResource learning - learned the
//     scene snapshot pairs; per-eye substitution of both sides left the ghost
//     unchanged. The learning also allocated a full-resolution shadow texture
//     per eye per pair (~25 MB each) and re-copied them every eye pass.
// CopyResource itself is now intercepted only for ODST's exact native-CHUD
// phase scope. It performs one retained-pointer comparison and may substitute
// only the source with the already-owned eye cache; it does no discovery.
//   PSSetShaderResources - cross-pass history discovery promoted 0 targets in
//     two sessions.
//   OMSetRenderTargetsAndUnorderedAccessViews frame-level RTV *discovery*
//     promoted 0 targets. The current H4 reticle path hooks that setter only
//     to mirror its already-known slot-0 pointer after a real state change; it
//     performs no discovery, COM query, logging, or allocation.
//   PSSetShader/VSSetShader/Draw* - the CHUD steal-and-requad classifier
//     (2026-07-18): removed the native HUD from both eyes, never displayed
//     its hand quad, and its calibration retry loop cost ~30 fps.
// OMSetRenderTargets makes the two eye renders land in separate textures.

// --- DLSS texture mip bias (Programming Guide 3.5) ---------------------------
// With DLSS the game rasterises at a fraction of the headset picture, so its
// samplers pick texture mips for that small render and every surface arrives
// blurred; DLSS then has only blur to reconstruct from. The guide requires a
// negative mip bias of log2(render/output) - 1 while DLSS is on. The game
// creates its sampler states once, so they cannot be re-created with the
// bias; instead, while an eye is rasterised for DLSS, every pixel-shader
// sampler the game binds is swapped for a twin with the bias added. Twins are
// made once per distinct sampler (the game has a few dozen) and looked up in
// a fixed direct-mapped table: the hook is one atomic load and a branch when
// the bias is off, and a few pointer compares per bound sampler when it is on.
// The game's sampler objects are held (AddRef) so a pointer can never be
// recycled for a different sampler while its twin exists. Render thread only:
// the bias is armed with the arming thread's id and other threads pass
// through untouched.
#include "dlss_sampler_cache.inl"

void D3D_ReportSamplerCache()
{
    if constexpr (!dlss_sampler::kEnabled) return;
    // Called by the existing game worker, never an eye/draw hook.
    static uint64_t last=0,priorFailures=0;
    const uint64_t now=GetTickCount64();
    if(now>=last&&now-last<5000) return;
    last=now;
    const auto misses=g_samplerMisses.exchange(0,std::memory_order_relaxed);
    const auto failures=g_samplerFailures.load(std::memory_order_relaxed);
    if((misses||failures!=priorFailures)&&g_config.upscaler==dlss::kUpscalerDlss)
        LOG("DLSS sampler cache: %llu prewarmed, %llu uncached binds, %llu factory/capacity failures; "
            "uncached samplers retain native texture mips; eye rendering remains available",
            static_cast<unsigned long long>(g_samplerTwinsMade.load()),
            static_cast<unsigned long long>(misses),static_cast<unsigned long long>(failures));
    priorFailures=failures;
}

static void STDMETHODCALLTYPE OMSetRenderTargetsHook(ID3D11DeviceContext* context, UINT count,
    ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
    Bloom_Invalidate(context,false);
    ID3D11RenderTargetView* redirected[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    UINT ceCount{};
    ID3D11DepthStencilView* ceDepth{};
    if (VR_CeRedirectAuthoredReticleTargets(context,count,rtvs,dsv,ceCount,redirected,ceDepth))
    {
        g_origOMSetRenderTargets(context,ceCount,redirected,ceDepth);
        VR_Halo4NoteBoundRenderTargets(context,ceCount,redirected);
        return;
    }
    if (count <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT &&
        VR_RedirectRenderTargets(context, count, rtvs, dsv, redirected))
    {
        g_origOMSetRenderTargets(context, count, redirected, dsv);
        // Publish only after the void D3D state mutation has completed, so the
        // cached slot exactly describes the state a later Draw will consume.
        VR_Halo4NoteBoundRenderTargets(context, count, redirected);
        return;
    }
    g_origOMSetRenderTargets(context, count, rtvs, dsv);
    VR_Halo4NoteBoundRenderTargets(context, count, rtvs);
}

// CE's optional native gameplay-HUD scope supplies the only transform. These
// observers copy bounded numeric state, with no queries, allocation or logging.
static void STDMETHODCALLTYPE RSSetViewportsHook(ID3D11DeviceContext* context,
    UINT count, const D3D11_VIEWPORT* viewports)
{
    D3D11_VIEWPORT capture;
    if (VR_CeRedirectAuthoredReticleViewports(context,count,viewports,capture))
    { g_origRSSetViewports(context,1,&capture);return; }
    D3D11_VIEWPORT adjusted[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    const bool replace = HaloCEHudLayout_PrepareViewports(context,count,viewports,adjusted);
    g_origRSSetViewports(context,count,replace?adjusted:viewports);
}

static void STDMETHODCALLTYPE RSSetScissorRectsHook(ID3D11DeviceContext* context,
    UINT count, const D3D11_RECT* scissors)
{
    D3D11_RECT capture;
    if (VR_CeRedirectAuthoredReticleScissors(context,count,scissors,capture))
    { g_origRSSetScissorRects(context,1,&capture);return; }
    D3D11_RECT adjusted[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    const bool replace = HaloCEHudLayout_PrepareScissors(context,count,scissors,adjusted);
    g_origRSSetScissorRects(context,count,replace?adjusted:scissors);
}

static void STDMETHODCALLTYPE ExecuteCommandListHook(ID3D11DeviceContext* context,
    ID3D11CommandList* commands, BOOL restoreState)
{
    Bloom_Invalidate(context);
    VR_CeInvalidateAuthoredReticleState(context);
    HaloCEHudLayout_InvalidateState(context);
    g_origExecuteCommandList(context,commands,restoreState);
}

static void STDMETHODCALLTYPE ClearStateHook(ID3D11DeviceContext* context)
{
    Bloom_Invalidate(context);
    VR_CeInvalidateAuthoredReticleState(context);
    HaloCEHudLayout_InvalidateState(context);
    g_origClearState(context);
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    VR_Halo4NoteBoundRenderTargets(context, 0, nullptr);
#endif
}

static void STDMETHODCALLTYPE SwapDeviceContextStateHook(ID3D11DeviceContext1* context,
    ID3DDeviceContextState* state, ID3DDeviceContextState** previous)
{
    Bloom_Invalidate(context);
    VR_CeInvalidateAuthoredReticleState(context);
    HaloCEHudLayout_InvalidateState(context);
    g_origSwapDeviceContextState(context,state,previous);
}

static void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViewsHook(
    ID3D11DeviceContext* context, UINT renderTargetCount,
    ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv,
    UINT uavStartSlot, UINT uavCount,
    ID3D11UnorderedAccessView* const* uavs, const UINT* initialCounts)
{
    Bloom_Invalidate(context,false);
    // CE's verified native binder uses OMSetRenderTargets. An unexpected
    // UAV/target mutation invalidates just its current crosshair capture.
    VR_CeInvalidateAuthoredReticleState(context);
    g_origOMSetRenderTargetsAndUnorderedAccessViews(
        context, renderTargetCount, rtvs, dsv, uavStartSlot, uavCount, uavs,
        initialCounts);
    // The KEEP sentinel leaves OM render targets untouched. Every real RTV
    // mutation replaces slot 0 (or clears it), so update the allocation-free
    // Stage 3BH-equivalent tracker after the actual call.
    if (renderTargetCount != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        VR_Halo4NoteBoundRenderTargets(context, renderTargetCount, rtvs);
    VR_ObserveReachAlternateDepthBind(context, renderTargetCount, dsv);
}

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
// Diagnostic only, and deliberately far more conservative than the retired
// RSSetViewports hook: this file's own history (see the OMSetRenderTargetsHook
// comment above) records that a prior PS/VS/Draw*-hooking attempt cost ~30 fps.
// This hook does the absolute minimum on every call - one relaxed atomic
// load and compare, nothing else - unless it is one of a HARD-CAPPED total
// number of samples for the whole process lifetime (not per-second: DrawIndexed
// can be called thousands of times a frame, so any per-frame or per-second
// throttle can still mean thousands of wasted checks). Once the budget is
// spent the hook is a single branch forever, so it cannot degrade a long
// session even if this candidate stays enabled by accident.
//
// Purpose: every static and memory-inspection method available tonight
// converged on the same result - the chud_globals curvature-info safe frame
// this mod writes is correct, stable, and read by nothing in the process
// (docs/RE-notes.md, the Reach HUD layout entry). If Reach's HUD is drawn as
// screen-space quads (proven: RSSetViewports never shows a HUD-sized rect),
// the quad's actual screen extent has to come from SOMEWHERE - vertex data,
// a constant buffer, or a shader immediate. Sampling the bound viewport,
// vertex buffer stride, and index/vertex counts at a real HUD-sized draw call
// is the most direct way left to find that path without disassembling more
// of a stripped optimized binary blind.
static std::atomic<int> g_reachDrawSamplesTaken{0};
constexpr int kReachDrawSampleBudget = 200;

// The first 64-sample pass (2026-07-26) found two distinct full-viewport
// index-count-6 shapes: stride 40 and stride 24, plus an unrelated stride-32
// mip/blur chain that shrinks through power-of-two sizes (not sampled here).
// The stride-40 shape was sampled next and its vertices proved to be an exact
// (0,0)-(1,1) UV quad spanning the FULL screen corner to corner in every
// sample - a full-screen post-process/compositor pass, not the HUD. This pass
// switches to the stride-24 shape, the other untried candidate from the same
// original sweep, and reads its real vertex bytes the same way.
static bool IsProvenHudQuadShape(
    UINT indexCount, UINT stride, const D3D11_VIEWPORT& vp, float backbufferW)
{
    return indexCount == 6 && stride == 24 &&
        vp.Width >= backbufferW - 1.0f;
}

static void DumpVertexBufferSample(
    ID3D11DeviceContext* context, ID3D11Buffer* vb, UINT stride, UINT sampleNo)
{
    ID3D11Device* device = nullptr;
    context->GetDevice(&device);
    if (!device)
        return;
    D3D11_BUFFER_DESC srcDesc{};
    vb->GetDesc(&srcDesc);
    D3D11_BUFFER_DESC stagingDesc = srcDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ID3D11Buffer* staging = nullptr;
    const HRESULT hr = device->CreateBuffer(&stagingDesc, nullptr, &staging);
    device->Release();
    if (FAILED(hr) || !staging)
        return;
    context->CopyResource(staging, vb);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(
            staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(mapped.pData);
        // Log up to 4 vertices worth (our proven shape uses 4 unique
        // vertices per quad, 3 quads); stride 40 = 10 floats/vertex.
        const UINT vertsToShow = std::min(
            4u, srcDesc.ByteWidth / std::max(stride, 1u));
        for (UINT v = 0; v < vertsToShow; ++v)
        {
            const float* f = reinterpret_cast<const float*>(
                bytes + static_cast<size_t>(v) * stride);
            const UINT floatsToShow = std::min(stride / 4u, 10u);
            char line[256];
            int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                "REACHVTX[sample %u]: vertex %u:", sampleNo, v);
            for (UINT fi = 0; fi < floatsToShow && n > 0 &&
                 static_cast<size_t>(n) < sizeof(line); ++fi)
            {
                char piece[24];
                _snprintf_s(piece, sizeof(piece), _TRUNCATE,
                    " %.4f", f[fi]);
                strcat_s(line, sizeof(line), piece);
            }
            LOG("%s", line);
        }
        context->Unmap(staging, 0);
    }
    staging->Release();
}

static void LogReachHudDrawIfSmall(ID3D11DeviceContext* context,
    UINT indexCount, UINT startIndex, INT baseVertex)
{
    if (g_reachDrawSamplesTaken.load(std::memory_order_relaxed) >=
        kReachDrawSampleBudget)
        return;
    if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach)
        return;
    if (indexCount == 0 || indexCount > 64)
        return;

    UINT vpCount = 1;
    D3D11_VIEWPORT vp{};
    context->RSGetViewports(&vpCount, &vp);

    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    context->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    const bool hadVb = vb != nullptr;

    unsigned forcedBackbufferW = 0, forcedBackbufferH = 0;
    D3D_GetForcedRenderSize(forcedBackbufferW, forcedBackbufferH);
    const float backbufferW = forcedBackbufferW
        ? static_cast<float>(forcedBackbufferW) : 3786.0f;
    const bool provenShape =
        hadVb && IsProvenHudQuadShape(indexCount, stride, vp, backbufferW);

    if (!provenShape)
    {
        if (vb)
            vb->Release();
        return;
    }

    const int sampleNo =
        g_reachDrawSamplesTaken.fetch_add(1, std::memory_order_relaxed) + 1;
    if (sampleNo > kReachDrawSampleBudget)
    {
        vb->Release();
        return;
    }
    LOG("REACHDRAW: DrawIndexed count=%u startIndex=%u baseVertex=%d "
        "vp=(%.1f,%.1f %.1fx%.1f) vbStride=%u [sample %d/%d]",
        indexCount, startIndex, baseVertex, vp.TopLeftX, vp.TopLeftY,
        vp.Width, vp.Height, stride, sampleNo, kReachDrawSampleBudget);
    DumpVertexBufferSample(
        context, vb, stride, static_cast<UINT>(sampleNo));
    vb->Release();
}

static void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* context,
    UINT indexCount, UINT startIndex, INT baseVertex)
{
    LogReachHudDrawIfSmall(context, indexCount, startIndex, baseVertex);
    g_origDrawIndexed(context, indexCount, startIndex, baseVertex);
}
#endif

#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
static void STDMETHODCALLTYPE CopyResourceHook(ID3D11DeviceContext* context,
    ID3D11Resource* destination, ID3D11Resource* source)
{
    g_origCopyResource(context, destination,
                       VR_RedirectNativeHudCopySource(source));
}
#endif

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
// RETIRED DIAGNOSTIC (2026-07-26): a live session proved every RSSetViewports
// call in a Reach frame is a world/shadow-cascade/mip-chain size (observed
// 3786x2730 down through 2x1 in one frame, never a small HUD-shaped rect).
// Reach's CHUD does not resize a distinct D3D11 viewport at all - it draws
// screen-space geometry inside the single full-frame viewport, positioned by
// vertex coordinates. This hook answered its question and is retired rather
// than left installed: with dozens of genuinely distinct viewports per frame,
// its "distinct shape" dedup could not throttle a hot path (proven: ~500k
// lines in one short session) and volume like that risks burying real
// signal. See docs/RE-notes.md, the Reach HUD layout entry, for what this
// ruled out and what is tried next.
#endif

// Log-only: record what MCC's swapchain actually is, plus how its backbuffer
// compares to the visible window and the monitor. On a monitor smaller than the
// requested render this shows whether MCC drew the full frame (backbuffer size)
// or a small one, the swap effect + scaling (whether an oversized backbuffer
// STRETCHes or CLIPs on present), and the real window client vs the monitor.
// That is exactly what tells us whether the fit is working. Fires once, then
// only when the backbuffer size changes, so it never spams the hot path.
static void LogSwapchainConfigOnce(IDXGISwapChain* sc)
{
    static UINT s_lastW = 0, s_lastH = 0;
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d)))
        return;
    if (d.BufferDesc.Width == s_lastW && d.BufferDesc.Height == s_lastH)
        return;
    s_lastW = d.BufferDesc.Width;
    s_lastH = d.BufferDesc.Height;

    const char* swapEffect = "UNKNOWN";
    switch (d.SwapEffect)
    {
        case DXGI_SWAP_EFFECT_DISCARD: swapEffect = "DISCARD"; break;
        case DXGI_SWAP_EFFECT_SEQUENTIAL: swapEffect = "SEQUENTIAL"; break;
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: swapEffect = "FLIP_SEQUENTIAL"; break;
        case DXGI_SWAP_EFFECT_FLIP_DISCARD: swapEffect = "FLIP_DISCARD"; break;
        default: break;
    }

    const char* scaling = "n/a";
    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)))
    {
        DXGI_SWAP_CHAIN_DESC1 d1{};
        if (SUCCEEDED(sc1->GetDesc1(&d1)))
        {
            switch (d1.Scaling)
            {
                case DXGI_SCALING_STRETCH: scaling = "STRETCH"; break;
                case DXGI_SCALING_NONE: scaling = "NONE"; break;
                case DXGI_SCALING_ASPECT_RATIO_STRETCH: scaling = "ASPECT_RATIO_STRETCH"; break;
                default: break;
            }
        }
        sc1->Release();
    }

    int clientW = 0, clientH = 0;
    HWND hwnd = d.OutputWindow;
    RECT client{};
    if (hwnd && GetClientRect(hwnd, &client))
    {
        clientW = client.right - client.left;
        clientH = client.bottom - client.top;
    }
    int monW = 0, monH = 0, workW = 0, workH = 0;
    if (hwnd)
    {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfo(mon, &mi))
        {
            monW = mi.rcMonitor.right - mi.rcMonitor.left;
            monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            workW = mi.rcWork.right - mi.rcWork.left;
            workH = mi.rcWork.bottom - mi.rcWork.top;
        }
    }

    LOG("swapchain: backbuffer %ux%u fmt=%d swapEffect=%s scaling=%s bufferCount=%u "
        "windowed=%d | window client %dx%d | monitor %dx%d work %dx%d | fit=%d",
        d.BufferDesc.Width, d.BufferDesc.Height, (int)d.BufferDesc.Format,
        swapEffect, scaling, d.BufferCount, d.Windowed ? 1 : 0,
        clientW, clientH, monW, monH, workW, workH,
        g_config.fit_desktop_window ? 1 : 0);
}

// Present1 can forward to Present internally; this depth counter makes sure
// we only run the VR frame once per game frame.
static thread_local int g_presentDepth = 0;

// --- Co-op drop probe -------------------------------------------------------
// Records, per frame, how long THIS MOD holds MCC's render thread inside
// Present. See coop_probe_logic.h for why that thread matters to co-op. The
// ring is fixed and written only from the game's render thread (Present has one
// caller), so the hot path stays allocation-free and lock-free: fill the slot,
// then publish the count with a release store.
// The investigation is complete; preserve the probe for a targeted future run
// while compiling it out of the normal Present path.
constexpr bool kEnableCoopPresentProbe = false;
namespace
{
    // ~34 s at 120 Hz, ~57 s at 72 Hz -- long enough to hold the whole run-up to
    // a kick at any headset rate we support.
    constexpr size_t kCoopProbeSamples = 4096;
    constexpr size_t kCoopProbeSeconds = 64;

    CoopProbeSample g_coopSamples[kCoopProbeSamples]{};
    std::atomic<uint64_t> g_coopWritten{ 0 };
    int64_t g_coopPrevHookStartQpc = 0;

    double CoopProbeQpcToMs()
    {
        static double s_toMs = [] {
            LARGE_INTEGER f{};
            QueryPerformanceFrequency(&f);
            return f.QuadPart ? 1000.0 / static_cast<double>(f.QuadPart) : 0.0;
        }();
        return s_toMs;
    }

    void CoopProbeNoteFrame(int64_t hookStartQpc, int64_t presentStartQpc,
                            int64_t presentEndQpc)
    {
        if constexpr (!kEnableCoopPresentProbe)
            return;
        if (!g_config.coop_probe)
            return;
        const double toMs = CoopProbeQpcToMs();
        if (toMs == 0.0)
            return;

        LARGE_INTEGER hookEnd{};
        QueryPerformanceCounter(&hookEnd);

        const double hookMs =
            static_cast<double>(hookEnd.QuadPart - hookStartQpc) * toMs;
        const double dxgiMs =
            static_cast<double>(presentEndQpc - presentStartQpc) * toMs;
        // What we ADD is the hook minus the game's own present. Clamp at zero:
        // the two timestamps are taken at slightly different points and a
        // negative "hold" would only ever be measurement noise.
        const double holdMs = hookMs > dxgiMs ? hookMs - dxgiMs : 0.0;
        const double intervalMs = g_coopPrevHookStartQpc
            ? static_cast<double>(hookStartQpc - g_coopPrevHookStartQpc) * toMs
            : 0.0;
        g_coopPrevHookStartQpc = hookStartQpc;

        const uint64_t written = g_coopWritten.load(std::memory_order_relaxed);
        CoopProbeSample& slot = g_coopSamples[written % kCoopProbeSamples];
        slot.tickMs = static_cast<uint32_t>(GetTickCount64());
        slot.holdMs = static_cast<float>(holdMs);
        slot.dxgiMs = static_cast<float>(dxgiMs);
        slot.intervalMs = static_cast<float>(intervalMs);
        g_coopWritten.store(written + 1, std::memory_order_release);
    }
}

void CoopProbe_DumpRunUp(const char* reason)
{
    if constexpr (!kEnableCoopPresentProbe)
        return;
    if (!g_config.coop_probe)
        return;
    const uint64_t written = g_coopWritten.load(std::memory_order_acquire);
    if (!written)
        return;

    // Dump path only -- never the hot path -- so these statics cost nothing per
    // frame and keep the probe allocation-free here too.
    static CoopProbeSample s_ordered[kCoopProbeSamples];
    static CoopProbeBucket s_buckets[kCoopProbeSeconds];

    const size_t count = static_cast<size_t>(
        written < kCoopProbeSamples ? written : kCoopProbeSamples);
    const size_t first =
        static_cast<size_t>((written - count) % kCoopProbeSamples);
    for (size_t i = 0; i < count; ++i)
        s_ordered[i] = g_coopSamples[(first + i) % kCoopProbeSamples];

    const uint32_t nowTick = static_cast<uint32_t>(GetTickCount64());
    const size_t bucketCount = CoopProbeSummarise(
        s_ordered, count, nowTick, s_buckets, kCoopProbeSeconds);

    LOG("COOPPROBE run-up to teardown from %s: %zu frames across %zu s. "
        "'hold' is the time THIS MOD adds inside MCC's Present, on MCC's own "
        "render thread -- the thread its co-op simulation is paced from. "
        "Rising hold or interval before a kick implicates the mod; flat means "
        "look elsewhere.",
        reason ? reason : "?", count, bucketCount);
    for (size_t age = bucketCount; age-- > 0;)
    {
        const CoopProbeBucket& b = s_buckets[age];
        if (!b.frames)
            continue;
        LOG("COOPPROBE t-%02zus: frames=%u hold avg=%.3f max=%.3f over4ms=%u "
            "over8ms=%u | dxgi avg=%.3f max=%.3f | interval avg=%.3f max=%.3f",
            age, b.frames, b.holdAvgMs, b.holdMaxMs, b.holdOver4Ms,
            b.holdOver8Ms, b.dxgiAvgMs, b.dxgiMaxMs, b.intervalAvgMs,
            b.intervalMaxMs);
    }
}

// The VR frame is submitted INSIDE the game's desktop present (below), so
// whatever throttles that present throttles the headset with it. MCC's V-Sync
// paces on the DESKTOP monitor's refresh, which has nothing to do with the
// headset's -- that is how a 60 Hz desktop ends up capping a 120 Hz headset.
// Once xrWaitFrame is driving our cadence, present the desktop mirror unlocked
// and let the runtime's reported display period be the only clock. No rate is
// assumed here: 72, 90, 120 and 144 Hz headsets all pace themselves.
static UINT PacedSyncInterval(UINT requested)
{
    if (!g_config.desktop_present_unlocked || !VR_IsFramePacingOwned())
        return requested;
    static UINT s_loggedRequested = UINT_MAX;
    if (requested != s_loggedRequested)
    {
        s_loggedRequested = requested;
        if (requested == 0)
            LOG("pacing: MCC already presents unlocked (syncInterval=0); the "
                "runtime app cadence is %.1fHz", VR_HeadsetRefreshHz());
        else
            LOG("pacing: MCC asked for syncInterval=%u (its V-Sync would cap the "
                "headset at the DESKTOP refresh); presenting unlocked so the "
                "runtime app cadence remains %.1fHz",
                requested, VR_HeadsetRefreshHz());
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* sc, UINT syncInterval, UINT flags)
{
    const bool topLevel = (g_presentDepth++ == 0);
    const bool runVrFrame = topLevel && !(flags & DXGI_PRESENT_TEST);
    LARGE_INTEGER hookStart{};
    if (runVrFrame)
    {
        TitleReentryProbe_PublishPresentCaller(_ReturnAddress(), GetTickCount64());
        if constexpr (kEnableCoopPresentProbe)
            QueryPerformanceCounter(&hookStart);
        LogSwapchainConfigOnce(sc);
        CheckLiveResize(sc);
        VR_BeforePresent(sc);
    }
    const UINT pacedSyncInterval = runVrFrame
        ? PacedSyncInterval(syncInterval) : syncInterval;
    LARGE_INTEGER presentStart{}, presentEnd{};
    if (runVrFrame)
        QueryPerformanceCounter(&presentStart);
    HRESULT hr = g_origPresent(sc, pacedSyncInterval, flags);
    if (runVrFrame)
    {
        QueryPerformanceCounter(&presentEnd);
        VR_AfterPresent(sc, presentStart.QuadPart, presentEnd.QuadPart, hr);
        if constexpr (kEnableCoopPresentProbe)
            CoopProbeNoteFrame(hookStart.QuadPart, presentStart.QuadPart,
                               presentEnd.QuadPart);
    }
    g_presentDepth--;
    return hr;
}

static HRESULT STDMETHODCALLTYPE Present1Hook(IDXGISwapChain1* sc, UINT syncInterval, UINT flags,
                                              const DXGI_PRESENT_PARAMETERS* params)
{
    const bool topLevel = (g_presentDepth++ == 0);
    const bool runVrFrame = topLevel && !(flags & DXGI_PRESENT_TEST);
    LARGE_INTEGER hookStart{};
    if (runVrFrame)
    {
        TitleReentryProbe_PublishPresentCaller(_ReturnAddress(), GetTickCount64());
        if constexpr (kEnableCoopPresentProbe)
            QueryPerformanceCounter(&hookStart);
        LogSwapchainConfigOnce(sc);
        CheckLiveResize(sc);
        VR_BeforePresent(sc);
    }
    const UINT pacedSyncInterval = runVrFrame
        ? PacedSyncInterval(syncInterval) : syncInterval;
    LARGE_INTEGER presentStart{}, presentEnd{};
    if (runVrFrame)
        QueryPerformanceCounter(&presentStart);
    HRESULT hr = g_origPresent1(sc, pacedSyncInterval, flags, params);
    if (runVrFrame)
    {
        QueryPerformanceCounter(&presentEnd);
        VR_AfterPresent(sc, presentStart.QuadPart, presentEnd.QuadPart, hr);
        if constexpr (kEnableCoopPresentProbe)
            CoopProbeNoteFrame(hookStart.QuadPart, presentStart.QuadPart,
                               presentEnd.QuadPart);
    }
    g_presentDepth--;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ResizeBuffersHook(IDXGISwapChain* sc, UINT bufferCount, UINT width,
                                                   UINT height, DXGI_FORMAT format, UINT flags)
{
    Bloom_BeforeResize();
    // With the fit on, keep the backbuffer pinned to the full launched render
    // size so a later resize (e.g. triggered when we shrink the visible window to
    // fit the monitor) can't clamp the surface the headset captures back down.
    // With the fit off, g_forcedRenderW/H are 0 and this passes the size through
    // unchanged -- exactly the previous behavior.
    UINT fw = width, fh = height;
    if (g_forcedRenderW && g_forcedRenderH)
    {
        fw = g_forcedRenderW;
        fh = g_forcedRenderH;
    }
    LOG("game resized its swapchain to %ux%u (using %ux%u)", width, height, fw, fh);
    const bool released = VR_OnResizeBuffers(sc);
    const HRESULT result = released
        ? g_origResizeBuffers(sc, bufferCount, fw, fh, format, flags)
        : DXGI_ERROR_INVALID_CALL;
    if (FAILED(result))
    {
        DXGI_SWAP_CHAIN_DESC actual{};
        if (sc && SUCCEEDED(sc->GetDesc(&actual)))
        {
            // Revert the metrics at this edge, before another WM_SIZE can
            // observe dimensions that DXGI rejected.
            if (g_fitActive)
            {
                g_forcedRenderW = actual.BufferDesc.Width;
                g_forcedRenderH = actual.BufferDesc.Height;
            }
            g_planRenderW = actual.BufferDesc.Width;
            g_planRenderH = actual.BufferDesc.Height;
            g_liveResizeActualW = actual.BufferDesc.Width;
            g_liveResizeActualH = actual.BufferDesc.Height;
            g_liveResizeFailedW = fw;
            g_liveResizeFailedH = fh;
            g_liveResizeDispatch.Failed();
        }
        LOG("swapchain resize rejected hr=0x%08X resources-released=%u; retaining actual size",
            static_cast<unsigned>(result), released ? 1u : 0u);
    }
    VR_AfterResizeBuffers(sc);
    return result;
}

bool InstallD3D11Hooks()
{
    // Config is loaded before this runs. Only when the desktop-window fit is on
    // do we compute the forced render size and install the swapchain-creation /
    // GetClientRect hooks below; with it off, none of that exists and the render
    // path is byte-for-byte the previous behavior.
    const bool fit = g_config.fit_desktop_window;
    InitRenderPlan();
    if (fit)
        InitForcedRenderSize();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"halo3xr_dummy_window";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
                                0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        LOG("dummy window creation failed (%lu)", GetLastError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr))
    {
        LOG("dummy D3D11 device creation failed (hr=0x%08X)", (unsigned)hr);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    void** vtbl = *(void***)sc;
    void** deviceVtbl = *(void***)dev;
    void** contextVtbl = *(void***)ctx;
#if HALOMCCVR_HALO2_STEREO6DOF
    bool halo2ShaderPathCreated = false;
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    bool halo4ReticleDrawPathCreated = false;
    bool halo4ReticleBindingStatePathCreated = false;
    bool halo4HelmetShaderPathCreated = false;
#endif
    bool ok = MH_CreateHook(vtbl[8], (void*)&PresentHook, (void**)&g_origPresent) == MH_OK &&
              MH_CreateHook(vtbl[13], (void*)&ResizeBuffersHook, (void**)&g_origResizeBuffers) == MH_OK &&
              MH_CreateHook(contextVtbl[33], (void*)&OMSetRenderTargetsHook,
                            (void**)&g_origOMSetRenderTargets) == MH_OK;
    // Optional CE metadata observation must never block an existing title.
    const MH_STATUS ceTextureStatus=MH_CreateHook(deviceVtbl[5],
        (void*)&CreateTexture2DHook,(void**)&g_origCreateTexture2D);
    if (ceTextureStatus!=MH_OK)
        LOG("CE early texture metadata unavailable (%d); native creation/import observation remains",ceTextureStatus);
    const MH_STATUS bloomSrv=MH_CreateHook(deviceVtbl[7],
        (void*)&CreateShaderResourceViewHook,(void**)&g_origCreateShaderResourceView);
    const MH_STATUS bloomResources=MH_CreateHook(contextVtbl[8],
        (void*)&BloomSetResourcesHook,(void**)&g_origBloomSetResources);
    bool bloomShaderStateCreated=false;
    // The SDK ID3D11DeviceContext slots are 44/45 (raster setters), 58
    // (command-list execution), and 110 (ClearState). Every observation path
    // is required only by optional CE layout; a partial set cannot gate VR.
    // ID3D11Device slot 23: build immutable variants during resource creation.
    // Optional failure keeps native samplers and the ordinary DLSS resolve.
    if(dlss_sampler::kEnabled && MH_CreateHook(deviceVtbl[23],(void*)&CreateSamplerStateHook,
        (void**)&g_origCreateSamplerState)!=MH_OK)
    {g_origCreateSamplerState=nullptr;LOG("DLSS: sampler prewarm unavailable; native texture mips retained");}
    // Slot 10 is PSSetSamplers: the DLSS texture mip bias. Optional: without
    // it DLSS still runs, on textures sampled for the smaller render.
    if (dlss_sampler::kEnabled && MH_CreateHook(contextVtbl[10], (void*)&PSSetSamplersHook,
                      (void**)&g_origPSSetSamplers) != MH_OK)
    {
        g_origPSSetSamplers = nullptr;
        LOG("DLSS: PSSetSamplers could not be hooked; the texture mip bias is unavailable");
    }
    const MH_STATUS ceViewports=MH_CreateHook(contextVtbl[44],
        (void*)&RSSetViewportsHook,(void**)&g_origRSSetViewports);
    const MH_STATUS ceScissors=MH_CreateHook(contextVtbl[45],
        (void*)&RSSetScissorRectsHook,(void**)&g_origRSSetScissorRects);
    const MH_STATUS ceCommands=MH_CreateHook(contextVtbl[58],
        (void*)&ExecuteCommandListHook,(void**)&g_origExecuteCommandList);
    const MH_STATUS sharedClearState=MH_CreateHook(contextVtbl[110],
        (void*)&ClearStateHook,(void**)&g_origClearState);
    const MH_STATUS sharedOmRtUav=MH_CreateHook(contextVtbl[34],
        (void*)&OMSetRenderTargetsAndUnorderedAccessViewsHook,
        (void**)&g_origOMSetRenderTargetsAndUnorderedAccessViews);
    // D3D11.1 can replace numeric state without the individual setters. Its
    // interface discovery belongs here at installation, never in a HUD draw.
    ID3D11DeviceContext1* context1=nullptr;
    bool ceContextStateObserved=true;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1),
        reinterpret_cast<void**>(&context1))))
    {
        void** context1Vtbl=*reinterpret_cast<void***>(context1);
        ceContextStateObserved=MH_CreateHook(context1Vtbl[131],
            (void*)&SwapDeviceContextStateHook,(void**)&g_origSwapDeviceContextState)==MH_OK;
        context1->Release();
    }
    const bool ceRasterObservationCreated=ceViewports==MH_OK&&ceScissors==MH_OK&&
        ceCommands==MH_OK&&sharedClearState==MH_OK&&sharedOmRtUav==MH_OK&&ceContextStateObserved;
    if (!ceRasterObservationCreated)
        LOG("CE HUD raster observation unavailable: viewports=%d scissors=%d commands=%d clear=%d RTV/UAV=%d contextState=%d; layout stays stock",
            ceViewports,ceScissors,ceCommands,sharedClearState,sharedOmRtUav,ceContextStateObserved?1:0);
#if HALOMCCVR_HALO2_STEREO6DOF || HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    const MH_STATUS createPixelShader = MH_CreateHook(
        deviceVtbl[15], (void*)&CreatePixelShaderHook,
        (void**)&g_origCreatePixelShader);
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    // The known-good V6 helmet toggle intercepts only PSSetShader and only the
    // exact 3DMigoto visor hash. Both hooks are optional; a partial install
    // leaves the authored helmet stock and never gates Present or camera VR.
    const MH_STATUS halo4PixelShaderSet = MH_CreateHook(
        contextVtbl[9], (void*)&PixelShaderSetHook,
        (void**)&g_origPixelShaderSet);
    halo4HelmetShaderPathCreated =
        createPixelShader == MH_OK && halo4PixelShaderSet == MH_OK;
    bloomShaderStateCreated=halo4HelmetShaderPathCreated;
    if (!halo4HelmetShaderPathCreated)
    {
        LOG("Halo 4 helmet shader path unavailable: CreatePS=%d PSSetShader=%d; "
            "authored visor stays stock",
            static_cast<int>(createPixelShader),
            static_cast<int>(halo4PixelShaderSet));
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    ok = ok && MH_CreateHook(contextVtbl[47], (void*)&CopyResourceHook,
                             (void**)&g_origCopyResource) == MH_OK;
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    // Slot 34 is the alternate OM RTV/UAV setter and slot 110 is ClearState.
    // Stage 3BH admitted framing only when its selected private RTV was truly
    // bound. Tracking all MCC-used OM mutation paths preserves that evidence
    // without an OMGetRenderTargets AddRef/Release in every admitted Draw.
    const MH_STATUS halo4OmRtUav = sharedOmRtUav;
    const MH_STATUS halo4ClearState = sharedClearState;
    halo4ReticleBindingStatePathCreated =
        halo4OmRtUav == MH_OK && halo4ClearState == MH_OK;
    if (!halo4ReticleBindingStatePathCreated)
    {
        LOG("Halo 4 authored-reticle OM state tracking unavailable: "
            "OMSetRTV/UAV=%d ClearState=%d; only the reticle feature stays stock",
            static_cast<int>(halo4OmRtUav),
            static_cast<int>(halo4ClearState));
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // ID3D11DeviceContext::DrawIndexed is vtable slot 12. The old HUD probe is
    // preserved above as evidence, but it must not intercept production draws.
    // This optional hook was never part of `ok`, so disabling it cannot gate
    // Present, render-target redirection, or any title camera core.
    if constexpr (kEnableReachDrawIndexedDiagnostic)
    {
        if (MH_CreateHook(contextVtbl[12], (void*)&DrawIndexedHook,
                          (void**)&g_origDrawIndexed) != MH_OK)
            LOG("REACHDRAW: DrawIndexed hook failed; draw sampling disabled");
    }
    else
    {
        LOG("REACHDRAW: DrawIndexed diagnostic hook intentionally disabled; "
            "no draw calls are intercepted");
    }
#endif
#if HALOMCCVR_HALO2_STEREO6DOF
    // CreatePixelShader is ID3D11Device slot 15; DrawIndexed/Draw are context
    // slots 12/13. These remain optional and fail open: the core Present path
    // and every other title continue even if this exact HUD path is unavailable.
    const MH_STATUS halo2DrawIndexed = MH_CreateHook(
        contextVtbl[12], (void*)&Halo2DrawIndexedCensusHook,
        (void**)&g_origHalo2DrawIndexed);
    const MH_STATUS halo2Draw = MH_CreateHook(
        contextVtbl[13], (void*)&Halo2DrawCensusHook,
        (void**)&g_origHalo2Draw);
    halo2ShaderPathCreated =
        createPixelShader == MH_OK &&
        halo2DrawIndexed == MH_OK && halo2Draw == MH_OK;
    if (halo2DrawIndexed != MH_OK || halo2Draw != MH_OK)
        LOG("Gameplay HUD hiding: GPU draw interception unavailable; native HUD may remain visible; camera ownership retained");
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    halo4ReticleDrawPathCreated =
        halo2DrawIndexed == MH_OK && halo2Draw == MH_OK &&
        halo4ReticleBindingStatePathCreated;
#endif
    if (!halo2ShaderPathCreated)
    {
        LOG("Halo 2 proven HUD shader path unavailable: CreatePS=%d "
            "DrawIndexed=%d Draw=%d; HUD stays stock and the procedural "
            "crosshair fallback remains",
            static_cast<int>(createPixelShader),
            static_cast<int>(halo2DrawIndexed),
            static_cast<int>(halo2Draw));
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA && !HALOMCCVR_HALO2_STEREO6DOF
    // Stage 3BH/3BR pin Halo 4's proven capture viewport immediately before
    // vertices bake. This optional path is independent of Present/camera
    // ownership and fails open to the stock CUI reticle if either draw hook is
    // unavailable.
    const MH_STATUS halo4DrawIndexed = MH_CreateHook(
        contextVtbl[12], (void*)&Halo4DrawIndexedFramingHook,
        (void**)&g_origHalo4DrawIndexed);
    const MH_STATUS halo4Draw = MH_CreateHook(
        contextVtbl[13], (void*)&Halo4DrawFramingHook,
        (void**)&g_origHalo4Draw);
    halo4ReticleDrawPathCreated =
        halo4DrawIndexed == MH_OK && halo4Draw == MH_OK &&
        halo4ReticleBindingStatePathCreated;
    if (!halo4ReticleDrawPathCreated)
    {
        LOG("Halo 4 authored-reticle draw framing unavailable: "
            "DrawIndexed=%d Draw=%d; only the reticle feature stays stock",
            static_cast<int>(halo4DrawIndexed),
            static_cast<int>(halo4Draw));
    }
#endif

    const bool bloomExtraDrawsCreated=InstallExtraHudDrawHooks(contextVtbl);

    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)))
    {
        void** vtbl1 = *(void***)sc1;
        if (MH_CreateHook(vtbl1[22], (void*)&Present1Hook, (void**)&g_origPresent1) != MH_OK)
            LOG("warning: Present1 hook failed; flip-model swapchains may not be captured");
        sc1->Release();
    }

    if (fit)
    {
        bool forceHookOk = false;
        bool clientRectHookOk = false;

        // Hook the DXGI factory's swapchain-creation entry points so we can force
        // MCC's backbuffer to the full launched render size. All factories in the
        // process share the same vtable implementation, so hooking the dummy
        // device's factory hooks MCC's real creation call -- exactly how the
        // Present/ResizeBuffers vtables above are taken.
        // CreateSwapChain=vtbl[10], CreateSwapChainForHwnd=vtbl[15] (IDXGIFactory2).
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory2 = nullptr;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) &&
            SUCCEEDED(dxgiDev->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory2)))
        {
            void** facVtbl = *(void***)factory2;
            if (MH_CreateHook(facVtbl[15], (void*)&CreateSwapChainForHwndHook,
                              (void**)&g_origCreateSwapChainForHwnd) == MH_OK)
                forceHookOk = true;
            else
                LOG("warning: CreateSwapChainForHwnd hook failed; desktop fit inactive");
            if (MH_CreateHook(facVtbl[10], (void*)&CreateSwapChainHook,
                              (void**)&g_origCreateSwapChain) != MH_OK)
                LOG("warning: CreateSwapChain hook failed; legacy desktop fit inactive");
        }
        else
        {
            LOG("warning: could not reach the DXGI factory; desktop fit inactive");
        }
        if (factory2) factory2->Release();
        if (adapter) adapter->Release();
        if (dxgiDev) dxgiDev->Release();

        // Hook user32!GetClientRect so MCC's own resize code, while it handles the
        // WM_SIZE we rewrite, sees the full render size and keeps drawing full.
        // The hook only lies on that thread-local call stack (see the header note),
        // so DXGI's present-time query still downscales the full frame into the
        // real, smaller window.
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            // Cache the game-EXE image range first: GetClientRect/GetWindowRect
            // and the cursor hooks all scope their game-side behavior on it.
            InitExeRange();

            if (void* pGetClientRect = (void*)GetProcAddress(user32, "GetClientRect"))
            {
                if (MH_CreateHook(pGetClientRect, (void*)&GetClientRectHook,
                                  (void**)&g_origGetClientRect) == MH_OK)
                    clientRectHookOk = true;
                else
                    LOG("warning: GetClientRect hook failed; the fit may crop on "
                        "resize-polling titles");
            }

            // GetWindowRect lie (game-side only): make MCC's input/focus region
            // match its full-size layout so the fitted menu's lower items stay
            // navigable. Independent of the render force; warn-only on failure.
            if (void* pGetWindowRect = (void*)GetProcAddress(user32, "GetWindowRect"))
            {
                if (MH_CreateHook(pGetWindowRect, (void*)&GetWindowRectHook,
                                  (void**)&g_origGetWindowRect) != MH_OK)
                    LOG("warning: GetWindowRect hook failed; fitted-menu lower "
                        "items may stay unreachable if MCC bounds input to the "
                        "window rect");
            }

            // Cursor coordinate remap so MCC's native shell / pause menu is
            // navigable in the fitted window (see the block above the hooks).
            // These are supplementary: if any fails, the display fit still works,
            // the menu is just no more navigable than before -- so they do NOT
            // gate g_fitActive. (g_exeBase is already cached above.)
            void* pGetCursorPos = (void*)GetProcAddress(user32, "GetCursorPos");
            void* pSetCursorPos = (void*)GetProcAddress(user32, "SetCursorPos");
            void* pWindowFromPoint = (void*)GetProcAddress(user32, "WindowFromPoint");
            const bool cursorHooksOk =
                g_exeBase &&
                pGetCursorPos &&
                MH_CreateHook(pGetCursorPos, (void*)&GetCursorPosHook,
                              (void**)&g_origGetCursorPos) == MH_OK &&
                pSetCursorPos &&
                MH_CreateHook(pSetCursorPos, (void*)&SetCursorPosHook,
                              (void**)&g_origSetCursorPos) == MH_OK &&
                pWindowFromPoint &&
                MH_CreateHook(pWindowFromPoint, (void*)&WindowFromPointHook,
                              (void**)&g_origWindowFromPoint) == MH_OK;
            if (!cursorHooksOk)
                LOG("warning: fitted-menu cursor remap hooks failed; the native "
                    "shell/pause menu may not be fully navigable in the fitted window");

            // ClipCursor correction is independent of the remap trio; warn-only.
            if (void* pClipCursor = (void*)GetProcAddress(user32, "ClipCursor"))
            {
                if (MH_CreateHook(pClipCursor, (void*)&ClipCursorHook,
                                  (void**)&g_origClipCursor) != MH_OK)
                    LOG("warning: ClipCursor hook failed; a fitted-menu cursor "
                        "clip could still confine the pointer to the wrong rect");
            }

            // Screen-size lie (game-side only): THE fix for the menu row-clip that
            // happens whether the fit is on or off, because MCC clips its menu
            // layout against the real desktop size (SM_CYSCREEN / GetMonitorInfo)
            // while drawing at full render. Report the full render extent to
            // MCC's own code so every drawn item stays navigable. System/DXGI/our
            // own callers keep the true desktop size. Warn-only; the display fit
            // does not depend on these.
            if (void* pGetSysMetrics = (void*)GetProcAddress(user32, "GetSystemMetrics"))
            {
                if (MH_CreateHook(pGetSysMetrics, (void*)&GetSystemMetricsHook,
                                  (void**)&g_origGetSystemMetrics) != MH_OK)
                    LOG("warning: GetSystemMetrics hook failed; the native menu's "
                        "lower rows may stay unreachable (MCC clips layout to the "
                        "real screen height)");
            }
            if (void* pGetMonInfo = (void*)GetProcAddress(user32, "GetMonitorInfoW"))
            {
                if (MH_CreateHook(pGetMonInfo, (void*)&GetMonitorInfoWHook,
                                  (void**)&g_origGetMonitorInfoW) != MH_OK)
                    LOG("warning: GetMonitorInfoW hook failed; the native menu may "
                        "still clip layout to the real monitor size");
            }
        }

        // The desktop fit only engages if BOTH levers that keep MCC drawing full
        // are in place. If either failed, we leave the window at full size (the
        // previous overflow behavior) rather than risk shrinking it into a crop.
        g_fitActive = forceHookOk && clientRectHookOk;
        if (!g_fitActive)
            LOG("fit_desktop_window ON but a required hook is missing; leaving the "
                "desktop window full-size (no shrink) to avoid a cropped render");
    }

    sc->Release();
    ctx->Release();
    dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (!ok)
    {
        LOG("MinHook could not hook the required D3D render path");
        return false;
    }
    const bool enabled = MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
    bool bloomMainDrawsCreated=false;
#if HALOMCCVR_HALO2_STEREO6DOF
    bloomMainDrawsCreated=halo2ShaderPathCreated;
#elif HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    bloomMainDrawsCreated=halo4ReticleDrawPathCreated;
#endif
    const bool bloomAvailable=enabled&&bloomMainDrawsCreated&&bloomExtraDrawsCreated&&
        bloomSrv==MH_OK&&bloomResources==MH_OK&&
        bloomShaderStateCreated&&ceCommands==MH_OK&&sharedClearState==MH_OK&&
        sharedOmRtUav==MH_OK&&ceContextStateObserved;
    Bloom_SetAvailable(bloomAvailable,g_origBloomSetResources);
    LOG("Bloom consumer path %s: optional exact Reach/H3/ODST shaders, 64 MiB source+zero cache; default native bloom",
        bloomAvailable?"available":"StockFallback (hook admission incomplete)");
    HaloCEHudLayout_SetObservationAvailable(enabled&&ceRasterObservationCreated);
#if HALOMCCVR_HALO2_STEREO6DOF
    g_halo2ShaderHooksAvailable.store(
        enabled && halo2ShaderPathCreated, std::memory_order_release);
    if (enabled && halo2ShaderPathCreated)
    {
        LOG("Halo 2 native HUD shader path ON: exact gameplay HUD shaders "
            "receive slider raster transforms; native crosshair shader is "
            "captured for the controller-aim quad");
    }
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    g_halo4AuthoredReticleDrawPathAvailable.store(
        enabled && halo4ReticleDrawPathCreated, std::memory_order_release);
    g_halo4HelmetShaderPathAvailable.store(
        enabled && halo4HelmetShaderPathCreated, std::memory_order_release);
    g_halo4ScreenEffectShaderPathAvailable.store(
        enabled && halo4HelmetShaderPathCreated, std::memory_order_release);
    if (enabled && halo4HelmetShaderPathCreated)
    {
        LOG("Halo 4 helmet shader bridge ACTIVE: tracking exact 3Dmigoto "
            "pixel shader %016llX; no CUI/radar property is modified",
            static_cast<unsigned long long>(
                halo4_helmet_shader::kVisorFramingHash));
        LOG("Halo 4 screen-effect comfort bridge ACTIVE: exact H4EK/retail "
            "motion-suck pixel shader %016llX is suppressed only while Halo 4 "
            "stereo owns the view; other speed-line/tint effects stay native",
            static_cast<unsigned long long>(
                halo4_screen_effect_shader::kMotionSuckHash));
    }
#endif
    return enabled;
}
