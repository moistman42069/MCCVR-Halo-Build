// Reach's authored crosshair is a translucent CHUD widget the game draws, with
// its own blend state, into a private capture cleared to transparent black.
// The VR aim quad is composited as UNPREMULTIPLIED (straight) alpha. This WARP
// fixture draws a synthetic widget both ways - over a background, as the flat
// game shows it, and into the capture, as the mod redirects it - runs the
// production Blit and its production shader, then composites the uploaded
// image exactly as OpenXR defines the quad's blend, and requires the VR result
// to equal the flat one. The artwork and blend states are synthetic; this
// verifies the alpha arithmetic of the upload, not Reach's tag content.
#include "../src/dll/vr.h"
#include "../src/dll/d3d_state.h"
#include "../src/dll/title_adapter.h"
#include "../src/common/haloce_reticle_logic.h"
#include "../src/common/authored_reticle_logic.h"
#include "../src/common/vr_blit_shader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
unsigned failures{};
ID3D11Device* g_device{};
ID3D11DeviceContext* g_context{};
ID3D11VertexShader* g_blitVs{};
ID3D11PixelShader* g_blitPsLinearize{},*g_blitPsPass{};
ID3D11SamplerState* g_blitSampler{};
ID3D11RasterizerState* g_blitRasterizer{};
ID3D11DepthStencilState* g_blitDepthOff{};
DXGI_FORMAT g_xrFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
GameTitle title=GameTitle::HaloReach;
bool failPipeline{};
std::vector<ComPtr<ID3D11ShaderResourceView>> sourceViews;
void Check(bool value,const char* why)
{ if (!value) { ++failures;std::fprintf(stderr,"Reach reticle alpha: %s\n",why); } }
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* target)
{
    ComPtr<ID3DBlob> code,error;
    const HRESULT hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,
        entry,target,0,0,&code,&error);
    Check(SUCCEEDED(hr),entry);
    if (FAILED(hr)&&error) std::fprintf(stderr,"%s\n",static_cast<const char*>(error->GetBufferPointer()));
    return code;
}
bool IsSrgb(DXGI_FORMAT f) { return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }
bool EnsureBlitPipeline()
{
    if (failPipeline) return false;
    if (g_blitVs) return true;
    auto vs=Compile(kVrBlitShader,"vs_main","vs_5_0");
    auto linear=Compile(kVrBlitShader,"ps_linearize","ps_5_0");
    auto pass=Compile(kVrBlitShader,"ps_pass","ps_5_0");
    if (!vs||!linear||!pass) return false;
    if (FAILED(g_device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&g_blitVs))||
        FAILED(g_device->CreatePixelShader(linear->GetBufferPointer(),linear->GetBufferSize(),nullptr,&g_blitPsLinearize))||
        FAILED(g_device->CreatePixelShader(pass->GetBufferPointer(),pass->GetBufferSize(),nullptr,&g_blitPsPass))) return false;
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
    raster.DepthClipEnable=TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    return SUCCEEDED(g_device->CreateSamplerState(&sampler,&g_blitSampler))&&
        SUCCEEDED(g_device->CreateRasterizerState(&raster,&g_blitRasterizer))&&
        SUCCEEDED(g_device->CreateDepthStencilState(&depth,&g_blitDepthOff));
}
ID3D11ShaderResourceView* AcquireSrcSrv(ID3D11Texture2D* source,const D3D11_TEXTURE2D_DESC&)
{
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(g_device->CreateShaderResourceView(source,nullptr,&view))) return nullptr;
    sourceViews.push_back(view);return view.Get();
}
}
GameTitle TitleAdapter_GetActiveTitle() { return title; }
#define LOG(...) ((void)0)
#include "../src/dll/vr_blit.inl"
#include "../src/dll/vr_swapchain_rtv.inl"
#undef LOG

namespace
{
constexpr float kBackground[4]{0.30f,0.25f,0.20f,1.0f};

std::vector<uint8_t> Pixels(ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    desc.MipLevels=1;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> stage;
    Check(SUCCEEDED(g_device->CreateTexture2D(&desc,nullptr,&stage)),"staging texture");
    g_context->CopySubresourceRegion(stage.Get(),0,0,0,0,texture,0,nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(SUCCEEDED(g_context->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback");
    std::vector<uint8_t> result(size_t(desc.Width)*desc.Height*4);
    for (UINT y=0;y<desc.Height;++y)
        std::memcpy(result.data()+size_t(y)*desc.Width*4,
            static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(desc.Width)*4);
    g_context->Unmap(stage.Get(),0);return result;
}

// The quad as OpenXR composites it with
// XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT, over the same background
// the flat reference used, in the same (gamma) byte domain the game blends in.
int CompositeQuadOverBackground(const std::vector<uint8_t>& quad,size_t i,unsigned channel)
{
    const float colour=quad[i+channel]/255.0f,alpha=quad[i+3]/255.0f;
    return int(std::lround(255.0f*CompositeUnpremultipliedChannel(
        colour,alpha,kBackground[channel])));
}

struct Scenario { const char* name; D3D11_BLEND srcAlpha,dstAlpha; };
}

int main()
{
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context))) return 2;
    g_device=device.Get();g_context=context.Get();
    if (!EnsureBlitPipeline()) return 2;

    // A Reach-blue ring at the DMR's idle opacity, with antialiased edges.
    const std::string widgetShader=std::string(kVrBlitShader)+R"(
float4 ps_widget(VSOut i) : SV_Target {
    float2 p=i.uv*2-1;
    float coverage=saturate((0.72-length(p))*64)*saturate((length(p)-0.62)*64);
    return float4(0.55,0.80,1.0,0.35*coverage);
})";
    auto code=Compile(widgetShader.c_str(),"ps_widget","ps_5_0");
    ComPtr<ID3D11PixelShader> widgetPs;
    Check(code&&SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&widgetPs)),
        "widget fixture pixel shader");

    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=512;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    auto makeTarget=[&](ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D11RenderTargetView>& view) {
        Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture))&&
            SUCCEEDED(device->CreateRenderTargetView(texture.Get(),nullptr,&view)),"512 target");
    };
    auto drawWidget=[&](ID3D11RenderTargetView* view,ID3D11BlendState* blend) {
        g_context->OMSetRenderTargets(1,&view,nullptr);
        D3D11_VIEWPORT viewport{0,0,512,512,0,1};g_context->RSSetViewports(1,&viewport);
        g_context->RSSetState(g_blitRasterizer);g_context->OMSetDepthStencilState(g_blitDepthOff,0);
        g_context->OMSetBlendState(blend,nullptr,0xffffffff);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs,nullptr,0);g_context->PSSetShader(widgetPs.Get(),nullptr,0);
        g_context->Draw(3,0);g_context->OMSetRenderTargets(0,nullptr,nullptr);
    };

    // Both common alpha-channel encodings of an "over" blend: the
    // premultiplied pair (alpha = coverage) and the same factors as colour
    // (alpha = coverage squared). The colour factors are the usual over pair.
    const Scenario scenarios[]{
        {"alpha ONE/INV_SRC_ALPHA",D3D11_BLEND_ONE,D3D11_BLEND_INV_SRC_ALPHA},
        {"alpha SRC_ALPHA/INV_SRC_ALPHA",D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA},
    };
    unsigned visible=0,antialias=0;
    int worstLegacyShortfall=0;
    for (const Scenario& scenario:scenarios)
    {
        D3D11_BLEND_DESC native{};
        auto& rt=native.RenderTarget[0];
        rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=scenario.srcAlpha;rt.DestBlendAlpha=scenario.dstAlpha;
        rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        ComPtr<ID3D11BlendState> blend;
        Check(SUCCEEDED(device->CreateBlendState(&native,&blend)),scenario.name);

        ComPtr<ID3D11Texture2D> flat,capture,output;ComPtr<ID3D11RenderTargetView> flatRtv,captureRtv,outputRtv;
        makeTarget(flat,flatRtv);makeTarget(capture,captureRtv);makeTarget(output,outputRtv);
        g_context->ClearRenderTargetView(flatRtv.Get(),kBackground);
        drawWidget(flatRtv.Get(),blend.Get());
        const float zero[4]{};
        g_context->ClearRenderTargetView(captureRtv.Get(),zero);
        drawWidget(captureRtv.Get(),blend.Get());
        const auto flatPixels=Pixels(flat.Get());
        const auto source=Pixels(capture.Get());

        // Before: the direct copy Reach used, composited as the quad is.
        title=GameTitle::HaloReach;
        Check(Blit(capture.Get(),desc,output.Get(),512,512,nullptr,false),"direct copy executes");
        const auto legacy=Pixels(output.Get());
        Check(legacy==source,"a non-authored Reach blit stays a byte-exact direct copy");
        // After: the production authored upload.
        Check(Blit(capture.Get(),desc,output.Get(),512,512,outputRtv.Get(),true),
            "Reach authored upload executes the production straight-alpha shader");
        const auto uploaded=Pixels(output.Get());
        for (size_t i=0;i<source.size();i+=4)
        {
            const unsigned ink=std::max({source[i],source[i+1],source[i+2],source[i+3]});
            if (!ink) { Check(uploaded[i+3]==0,"transparent texels stay transparent");continue; }
            ++visible;if (ink<32) ++antialias;
            for (unsigned c=0;c<3;++c)
            {
                const int vr=CompositeQuadOverBackground(uploaded,i,c);
                const int flatValue=flatPixels[i+c];
                Check(std::abs(vr-flatValue)<=3,
                    "the VR quad over the scene matches the flat game's own blend");
                const int before=CompositeQuadOverBackground(legacy,i,c);
                worstLegacyShortfall=std::max(worstLegacyShortfall,flatValue-before);
            }
        }
    }
    Check(visible>1000&&antialias>100,"fixture exercises a hollow ring and antialiased fragments");
    // The bug this fixes: at 35% opacity the old copy applied the fade twice,
    // falling about 0.35 x 0.65 of full blue (~58 of 255) short of flat.
    Check(worstLegacyShortfall>=40,"the old direct copy really did under-draw the widget");

    // SteamVR may hand a TYPELESS image for the negotiated SRGB format: the
    // production typed view retry plus the linearize branch must preserve the
    // straight-alpha result byte for byte (within rounding).
    {
        D3D11_BLEND_DESC native{};auto& rt=native.RenderTarget[0];
        rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.SrcBlendAlpha=D3D11_BLEND_ONE;
        rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        ComPtr<ID3D11BlendState> blend;Check(SUCCEEDED(device->CreateBlendState(&native,&blend)),"srgb blend");
        ComPtr<ID3D11Texture2D> capture,output;ComPtr<ID3D11RenderTargetView> captureRtv,outputRtv;
        makeTarget(capture,captureRtv);makeTarget(output,outputRtv);
        const float zero[4]{};g_context->ClearRenderTargetView(captureRtv.Get(),zero);
        drawWidget(captureRtv.Get(),blend.Get());
        Check(Blit(capture.Get(),desc,output.Get(),512,512,outputRtv.Get(),true),"unorm reference upload");
        const auto reference=Pixels(output.Get());
        auto xrDesc=desc;xrDesc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        ComPtr<ID3D11Texture2D> xrOutput;
        Check(SUCCEEDED(device->CreateTexture2D(&xrDesc,nullptr,&xrOutput)),"typeless XR image");
        std::vector<ID3D11Texture2D*> xrImages{xrOutput.Get()};
        std::vector<ID3D11RenderTargetView*> xrViews(1,nullptr);
        g_xrFormat=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        auto* typed=GetRtv(xrImages,xrViews,0,true);
        D3D11_RENDER_TARGET_VIEW_DESC typedDesc{};if (typed) typed->GetDesc(&typedDesc);
        Check(typed&&typedDesc.Format==g_xrFormat,"typed fallback selects the negotiated SRGB view");
        Check(Blit(capture.Get(),desc,xrOutput.Get(),512,512,typed,true),"Reach art takes the linearize/SRGB upload");
        const auto srgb=Pixels(xrOutput.Get());
        for (size_t i=0;i<srgb.size();++i)
            Check(std::abs(int(srgb[i])-int(reference[i]))<=2,
                "un-premultiplying BEFORE the sRGB curve keeps colour and coverage");
        if (typed) typed->Release();
        g_xrFormat=DXGI_FORMAT_R8G8B8A8_UNORM;

        // Halo 3 keeps its accepted direct copy, bit for bit.
        const auto source=Pixels(capture.Get());
        title=GameTitle::Halo3;
        Check(Blit(capture.Get(),desc,output.Get(),512,512,nullptr,true)&&Pixels(output.Get())==source,
            "Halo 3 retains its direct-copy route and exact pixels");
        title=GameTitle::Halo3ODST;
        Check(Blit(capture.Get(),desc,output.Get(),512,512,nullptr,true)&&Pixels(output.Get())==source,
            "ODST retains its direct-copy route and exact pixels");
        title=GameTitle::HaloReach;
        // A pipeline failure reports failure, so the caller's direct-copy
        // fallback can publish the art instead of losing the crosshair.
        failPipeline=true;
        Check(!Blit(capture.Get(),desc,output.Get(),512,512,outputRtv.Get(),true),
            "a straight-alpha failure does not report a successful upload");
        Check(Blit(capture.Get(),desc,output.Get(),512,512,nullptr,false)&&Pixels(output.Get())==source,
            "the caller's direct-copy fallback still publishes the art");
        failPipeline=false;
    }
    Check(VrBlitNeedsReachReticleStraightAlpha(true,true,512,512,512,512)&&
        !VrBlitNeedsReachReticleStraightAlpha(true,false,512,512,512,512)&&
        !VrBlitNeedsReachReticleStraightAlpha(false,true,512,512,512,512)&&
        !VrBlitNeedsReachReticleStraightAlpha(true,true,1024,512,512,512)&&
        !VrBlitNeedsReachReticleStraightAlpha(true,true,512,512,3786,2730),
        "only Reach's exact authored crosshair upload is converted");

    g_context->ClearState();sourceViews.clear();
    g_blitDepthOff->Release();g_blitRasterizer->Release();g_blitSampler->Release();
    g_blitPsPass->Release();g_blitPsLinearize->Release();g_blitVs->Release();
    if (!failures)
        std::printf("PASS_REACH_RETICLE_STRAIGHT_ALPHA: visible=%u antialias=%u legacyShortfall=%d\n",
            visible,antialias,worstLegacyShortfall);
    return failures?1:0;
}
