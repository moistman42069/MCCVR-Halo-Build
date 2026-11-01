// Execute the production desktop draw with a real D3D11 WARP device. Only the
// shared blit/SRV services are fixtures; crop, shaders, state and lifetime are
// the exact implementation included by vr.cpp. No game or OpenXR session runs.
#include "../src/dll/haloce_eye_cache.h"
#include "../src/dll/d3d_state.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
unsigned failures{};
ID3D11Device* g_device{};
ID3D11DeviceContext* g_context{};
ID3D11VertexShader* g_blitVs{};
ID3D11SamplerState* g_blitSampler{};
ID3D11RasterizerState* g_blitRasterizer{};
ID3D11DepthStencilState* g_blitDepthOff{};
struct SourceView { ID3D11Texture2D* source; ComPtr<ID3D11ShaderResourceView> view; };
std::vector<SourceView> sourceViews;
bool failPipeline{},failSource{};

void Check(bool value,const char* why)
{ if (!value) { ++failures;std::fprintf(stderr,"CE desktop mirror: %s\n",why); } }
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* target)
{
    ComPtr<ID3DBlob> code,error;
    const HRESULT hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,
        entry,target,0,0,&code,&error);
    if (FAILED(hr))
        std::fprintf(stderr,"fixture shader %s: %s\n",entry,
            error?static_cast<const char*>(error->GetBufferPointer()):"compile failed");
    return code;
}
bool IsSrgb(DXGI_FORMAT format)
{
    return format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
        format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}
bool EnsureBlitPipeline()
{
    if (failPipeline) return false;
    if (g_blitVs&&g_blitSampler&&g_blitRasterizer&&g_blitDepthOff) return true;
    static constexpr char vs[]=R"(
struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varying main(uint id : SV_VertexID) {
    Varying o;float2 uv=float2((id<<1)&2,id&2);
    o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;
})";
    const auto code=Compile(vs,"main","vs_5_0");
    if (!code||FAILED(g_device->CreateVertexShader(code->GetBufferPointer(),
        code->GetBufferSize(),nullptr,&g_blitVs))) return false;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(g_device->CreateSamplerState(&sampler,&g_blitSampler))) return false;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    if (FAILED(g_device->CreateRasterizerState(&raster,&g_blitRasterizer))) return false;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;
    return SUCCEEDED(g_device->CreateDepthStencilState(&depth,&g_blitDepthOff));
}
ID3D11ShaderResourceView* AcquireSrcSrv(ID3D11Texture2D* source,
    const D3D11_TEXTURE2D_DESC& descriptor)
{
    if (failSource||!source) return nullptr;
    for (const auto& entry:sourceViews) if (entry.source==source) return entry.view.Get();
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format=descriptor.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS
        ?DXGI_FORMAT_R8G8B8A8_UNORM:descriptor.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS
        ?DXGI_FORMAT_B8G8R8A8_UNORM:descriptor.Format;
    view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
    SourceView entry{source,{}};
    if (FAILED(g_device->CreateShaderResourceView(source,&view,&entry.view))) return nullptr;
    sourceViews.push_back(std::move(entry));return sourceViews.back().view.Get();
}
}

#include "../src/dll/haloce_desktop_mirror.inl"

namespace
{
D3D11_TEXTURE2D_DESC Descriptor(UINT width,UINT height,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM)
{
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.ArraySize=1;d.MipLevels=1;
    d.Format=format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
    d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;return d;
}
ComPtr<ID3D11Texture2D> Texture(const D3D11_TEXTURE2D_DESC& d)
{
    ComPtr<ID3D11Texture2D> texture;
    Check(SUCCEEDED(g_device->CreateTexture2D(&d,nullptr,&texture)),"fixture texture creation");
    return texture;
}
void Paint(ID3D11Texture2D* texture,const D3D11_TEXTURE2D_DESC& d,uint32_t color)
{
    const std::vector<uint32_t> pixels(size_t(d.Width)*d.Height,color);
    g_context->UpdateSubresource(texture,0,nullptr,pixels.data(),d.Width*4,0);
}
std::vector<uint32_t> Pixels(ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(g_device->CreateTexture2D(&d,nullptr,&staging))) return {};
    g_context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return {};
    std::vector<uint32_t> result(size_t(d.Width)*d.Height);
    for (UINT y=0;y<d.Height;++y)
        std::memcpy(result.data()+size_t(y)*d.Width,
            static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,d.Width*4);
    g_context->Unmap(staging.Get(),0);return result;
}
bool Solid(ID3D11Texture2D* texture,uint32_t color,int tolerance=0)
{
    const auto pixels=Pixels(texture);
    if (pixels.empty()) return false;
    for (uint32_t pixel:pixels) for (unsigned shift=0;shift<32;shift+=8)
        if (std::abs(int((pixel>>shift)&255)-int((color>>shift)&255))>tolerance) return false;
    return true;
}
halo_ce::EyeCache::Completed Pair(ID3D11Texture2D* left,ID3D11Texture2D* right,
    const D3D11_TEXTURE2D_DESC& d,float aspect=1.0f)
{
    halo_ce::EyeCache::Completed pair{};pair.key={3,7,100,9};pair.borrowId=2;
    pair.eyes[0]=left;pair.eyes[1]=right;pair.descriptor=d;
    pair.covers[0]=pair.covers[1]={2*std::atan(0.5f),std::atan(aspect*0.5f),std::atan(0.5f)};
    return pair;
}
void TestCrop(float physicalAspect,UINT destinationWidth,UINT destinationHeight)
{
    const auto sourceDesc=Descriptor(64,16),targetDesc=Descriptor(destinationWidth,destinationHeight);
    auto left=Texture(sourceDesc),right=Texture(sourceDesc),target=Texture(targetDesc);
    std::vector<uint32_t> gradient(64*16);
    for (UINT y=0;y<16;++y) for (UINT x=0;x<64;++x)
        gradient[size_t(y)*64+x]=0x71370000u|
            (uint32_t(std::lround(255*(y+0.5f)/16))<<8)|uint32_t(std::lround(255*(x+0.5f)/64));
    g_context->UpdateSubresource(left.Get(),0,nullptr,gradient.data(),64*4,0);
    Paint(right.Get(),sourceDesc,0xff00ffff);
    const auto leftBefore=Pixels(left.Get()),rightBefore=Pixels(right.Get());
    const auto pair=Pair(left.Get(),right.Get(),sourceDesc,physicalAspect);
    Check(MirrorCeDesktop(pair,target.Get(),targetDesc),"production mirror draws the non-square eye");
    const auto output=Pixels(target.Get());
    const float drawWidth=std::max(float(destinationWidth),destinationHeight*physicalAspect);
    const float drawHeight=std::max(float(destinationHeight),destinationWidth/physicalAspect);
    bool crop=output.size()==size_t(destinationWidth)*destinationHeight;
    for (UINT y=0;crop&&y<destinationHeight;++y) for (UINT x=0;x<destinationWidth;++x)
    {
        const float u=(x+0.5f+(drawWidth-destinationWidth)*0.5f)/drawWidth;
        const float v=(y+0.5f+(drawHeight-destinationHeight)*0.5f)/drawHeight;
        const uint32_t pixel=output[size_t(y)*destinationWidth+x];
        crop=std::abs(int(pixel&255)-int(std::lround(u*255)))<=2&&
            std::abs(int((pixel>>8)&255)-int(std::lround(v*255)))<=2&&
            (pixel&0xffff0000)==0x71370000u;
        if (!crop) break;
    }
    Check(crop,"camera aspect center-crops the correct axis, preserves orientation and fills every desktop pixel");
    Check(Pixels(left.Get())==leftBefore&&Pixels(right.Get())==rightBefore,
        "desktop crop leaves both borrowed headset textures byte-identical");
}
void TestFormats()
{
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
    for (DXGI_FORMAT sourceFormat:formats) for (DXGI_FORMAT targetFormat:formats)
    {
        if (targetFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS) continue;
        // The byte layout changes for RGBA -> BGRA; test each native family.
        const bool sourceBgra=sourceFormat==DXGI_FORMAT_B8G8R8A8_UNORM||sourceFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool targetBgra=targetFormat==DXGI_FORMAT_B8G8R8A8_UNORM||targetFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (sourceBgra!=targetBgra) continue;
        const auto sourceDesc=Descriptor(16,8,sourceFormat),targetDesc=Descriptor(16,8,targetFormat);
        auto left=Texture(sourceDesc),right=Texture(sourceDesc),target=Texture(targetDesc);
        constexpr uint32_t color=0x35c14008;
        Paint(left.Get(),sourceDesc,color);Paint(right.Get(),sourceDesc,0xffeebb99);
        const auto pair=Pair(left.Get(),right.Get(),sourceDesc,2);
        Check(MirrorCeDesktop(pair,target.Get(),targetDesc),"UNORM/sRGB/typeless production shader compiles and draws");
        Check(Solid(target.Get(),color,1),"encoded and linear source/target combinations preserve displayed color and alpha");
        Check(Solid(left.Get(),color)&&Solid(right.Get(),0xffeebb99),"gamma conversion never modifies either eye");
    }
    const auto square=Descriptor(512,512),smallDesc=Descriptor(32,32);
    auto left=Texture(square),right=Texture(square),target=Texture(smallDesc);
    Paint(left.Get(),square,0x40806020);Paint(right.Get(),square,0xffffffff);
    Check(MirrorCeDesktop(Pair(left.Get(),right.Get(),square),target.Get(),smallDesc)&&Solid(target.Get(),0x40806020),
        "512-square finished eye bypasses the shared reticle alpha repair");
    // MONO: one picture handed out for both eyes mirrors as a pair does. One texture as both eyes of a pair, or a
    // mono frame whose eyes differ, is still refused.
    auto mono=Pair(left.Get(),left.Get(),square);mono.monoEye=0;
    Check(MirrorCeDesktop(mono,target.Get(),smallDesc)&&Solid(target.Get(),0x40806020),"a mono picture mirrors");
    Check(!MirrorCeDesktop(Pair(left.Get(),left.Get(),square),target.Get(),smallDesc),
        "one texture as both eyes of a pair is refused");
    auto mismatched=Pair(left.Get(),right.Get(),square);mismatched.monoEye=0;
    Check(!MirrorCeDesktop(mismatched,target.Get(),smallDesc),"a mono frame whose eyes differ is refused");
}
bool SameState(const D3DStateBackup& a,const D3DStateBackup& b)
{
    return !std::memcmp(a.rtvs,b.rtvs,sizeof(a.rtvs))&&a.dsv==b.dsv&&
        a.numViewports==b.numViewports&&!std::memcmp(a.viewports,b.viewports,a.numViewports*sizeof(D3D11_VIEWPORT))&&
        a.rasterizer==b.rasterizer&&a.blend==b.blend&&!std::memcmp(a.blendFactor,b.blendFactor,sizeof(a.blendFactor))&&
        a.sampleMask==b.sampleMask&&a.depthStencil==b.depthStencil&&a.stencilRef==b.stencilRef&&
        a.inputLayout==b.inputLayout&&a.topology==b.topology&&a.vs==b.vs&&a.ps==b.ps&&a.gs==b.gs&&
        a.psSrv0==b.psSrv0&&a.psSampler0==b.psSampler0;
}
void TestState()
{
    const auto d=Descriptor(32,16);
    auto left=Texture(d),right=Texture(d),target=Texture(d),nativeTarget=Texture(d);
    Paint(left.Get(),d,0xff204080);Paint(right.Get(),d,0xffabcdef);
    const auto pair=Pair(left.Get(),right.Get(),d,2);
    ComPtr<ID3D11RenderTargetView> rtv;
    Check(SUCCEEDED(g_device->CreateRenderTargetView(nativeTarget.Get(),nullptr,&rtv)),"native target view fixture");
    auto depthDesc=d;depthDesc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    auto depthTexture=Texture(depthDesc);ComPtr<ID3D11DepthStencilView> dsv;
    Check(SUCCEEDED(g_device->CreateDepthStencilView(depthTexture.Get(),nullptr,&dsv)),"native depth fixture");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_WIREFRAME;raster.CullMode=D3D11_CULL_BACK;
    raster.ScissorEnable=TRUE;raster.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs;g_device->CreateRasterizerState(&raster,&rs);
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=0;
    ComPtr<ID3D11BlendState> bs;g_device->CreateBlendState(&blend,&bs);
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc=D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11DepthStencilState> ds;g_device->CreateDepthStencilState(&depth,&ds);
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    ComPtr<ID3D11SamplerState> ss;g_device->CreateSamplerState(&sampler,&ss);
    static constexpr char shader[]=R"(
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V vs(float3 position:POSITION) { V o;o.position=float4(position,1);o.uv=0;return o; }
float4 ps(V i):SV_Target { return float4(i.uv,0,1); }
[maxvertexcount(3)] void gs(triangle V v[3],inout TriangleStream<V> stream) {
    stream.Append(v[0]);stream.Append(v[1]);stream.Append(v[2]); }
struct Patch { float edge[3]:SV_TessFactor;float inside:SV_InsideTessFactor; };
Patch constants(InputPatch<V,3> p) { Patch o;o.edge[0]=o.edge[1]=o.edge[2]=o.inside=1;return o; }
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("constants")]
V hs(InputPatch<V,3> p,uint id:SV_OutputControlPointID) { return p[id]; }
[domain("tri")] V ds(Patch p,float3 bary:SV_DomainLocation,const OutputPatch<V,3> v) {
    V o;o.position=v[0].position*bary.x+v[1].position*bary.y+v[2].position*bary.z;o.uv=0;return o; }
)";
    const auto vc=Compile(shader,"vs","vs_5_0"),pc=Compile(shader,"ps","ps_5_0"),
        gc=Compile(shader,"gs","gs_5_0"),hc=Compile(shader,"hs","hs_5_0"),dc=Compile(shader,"ds","ds_5_0");
    if (!vc||!pc||!gc||!hc||!dc) { Check(false,"state shader fixture compilation");return; }
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> domain;ComPtr<ID3D11InputLayout> layout;
    g_device->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,&vs);
    g_device->CreatePixelShader(pc->GetBufferPointer(),pc->GetBufferSize(),nullptr,&ps);
    g_device->CreateGeometryShader(gc->GetBufferPointer(),gc->GetBufferSize(),nullptr,&gs);
    g_device->CreateHullShader(hc->GetBufferPointer(),hc->GetBufferSize(),nullptr,&hs);
    g_device->CreateDomainShader(dc->GetBufferPointer(),dc->GetBufferSize(),nullptr,&domain);
    const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    g_device->CreateInputLayout(&element,1,vc->GetBufferPointer(),vc->GetBufferSize(),&layout);
    Check(vs&&ps&&gs&&hs&&domain&&layout&&rs&&bs&&ds&&ss,"all native pipeline fixture objects exist");
    const D3D11_VIEWPORT viewports[]={{1,2,8,7,0.1f,0.8f},{9,1,5,6,0.2f,0.9f}};
    const D3D11_RECT scissors[]={{2,3,4,5},{6,7,8,9}};
    const FLOAT factors[]={0.1f,0.2f,0.3f,0.4f};
    auto* view=rtv.Get();auto* srv=AcquireSrcSrv(right.Get(),d);auto* samplerPtr=ss.Get();
    g_context->OMSetRenderTargets(1,&view,dsv.Get());g_context->RSSetViewports(2,viewports);
    g_context->RSSetScissorRects(2,scissors);g_context->RSSetState(rs.Get());
    g_context->OMSetBlendState(bs.Get(),factors,0x10203040);g_context->OMSetDepthStencilState(ds.Get(),13);
    g_context->IASetInputLayout(layout.Get());g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    g_context->VSSetShader(vs.Get(),nullptr,0);g_context->PSSetShader(ps.Get(),nullptr,0);
    g_context->GSSetShader(gs.Get(),nullptr,0);g_context->HSSetShader(hs.Get(),nullptr,0);g_context->DSSetShader(domain.Get(),nullptr,0);
    g_context->PSSetShaderResources(0,1,&srv);g_context->PSSetSamplers(0,1,&samplerPtr);
    D3DStateBackup before,after;before.Capture(g_context);
    Check(MirrorCeDesktop(pair,target.Get(),d)&&Solid(target.Get(),0xff204080),
        "mirror overrides native tessellation, geometry, blend, depth and restrictive scissor state for its own draw");
    after.Capture(g_context);Check(SameState(before,after),"all touched shared pipeline states restored exactly");
    before.Release();after.Release();
    ComPtr<ID3D11HullShader> actualHs;ComPtr<ID3D11DomainShader> actualDs;
    g_context->HSGetShader(&actualHs,nullptr,nullptr);g_context->DSGetShader(&actualDs,nullptr,nullptr);
    Check(actualHs.Get()==hs.Get()&&actualDs.Get()==domain.Get(),"hull and domain shader identities restored");
    D3D11_RECT actualScissors[2]{};UINT count=2;g_context->RSGetScissorRects(&count,actualScissors);
    Check(count==2&&!std::memcmp(scissors,actualScissors,sizeof(scissors)),"native scissor rectangles untouched");
    g_context->ClearState();
    Check(MirrorCeDesktop(pair,target.Get(),d),"mirror runs with no preexisting viewport");
    count=0;g_context->RSGetViewports(&count,nullptr);Check(count==0,"zero original viewports remain zero");
}
void TestShaderClasses()
{
    static constexpr char linkedSource[]=R"(
interface IColor { float4 Evaluate(); };
class ColorChoice : IColor { float4 value; float4 Evaluate() { return value; } };
ColorChoice nativeColor;
IColor chosenColor;
float4 main():SV_Target { return chosenColor.Evaluate(); }
)";
    const auto code=Compile(linkedSource,"main","ps_5_0");
    ComPtr<ID3D11ClassLinkage> linkage;ComPtr<ID3D11ClassInstance> instance;
    ComPtr<ID3D11PixelShader> pixel;
    const bool ready=code&&SUCCEEDED(g_device->CreateClassLinkage(&linkage))&&
        SUCCEEDED(g_device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),linkage.Get(),&pixel))&&
        SUCCEEDED(linkage->CreateClassInstance("ColorChoice",0,0,0,0,&instance));
    Check(ready,"dynamic shader linkage fixture creates its shader and class");
    if (!ready) return;
    auto* selected=instance.Get();g_context->PSSetShader(pixel.Get(),&selected,1);
    const auto d=Descriptor(16,8);auto left=Texture(d),right=Texture(d),target=Texture(d);
    Paint(left.Get(),d,0xff123456);Paint(right.Get(),d,0xffabcdef);
    Check(MirrorCeDesktop(Pair(left.Get(),right.Get(),d,2),target.Get(),d)&&Solid(target.Get(),0xff123456),
        "desktop draw replaces the dynamic linked native shader for its own copy");
    ComPtr<ID3D11PixelShader> actualShader;ID3D11ClassInstance* actualClass{};UINT count=1;
    g_context->PSGetShader(&actualShader,&actualClass,&count);
    Check(actualShader.Get()==pixel.Get()&&count==1&&actualClass==instance.Get(),
        "dynamic linked shader class instance and slot survive the production mirror");
    if (actualClass) actualClass->Release();g_context->ClearState();
}
class LifetimeMarker final : public IUnknown
{
    ULONG refs_{1};std::shared_ptr<bool> released_;
public:
    explicit LifetimeMarker(std::shared_ptr<bool> released):released_(std::move(released)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override
    {
        if (!out) return E_POINTER;
        *out=nullptr;if (iid!=__uuidof(IUnknown)) return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto remaining=--refs_;
        if (!remaining) { *released_=true;delete this; }
        return remaining;
    }
};
void TestFailuresAndLifetime()
{
    const auto d=Descriptor(32,16);auto left=Texture(d),right=Texture(d),target=Texture(d);
    Paint(left.Get(),d,0xff123456);Paint(right.Get(),d,0xffabcdef);
    const auto pair=Pair(left.Get(),right.Get(),d,2);
    const auto rejected=[&](halo_ce::EyeCache::Completed candidate,D3D11_TEXTURE2D_DESC destination) {
        Paint(target.Get(),d,0x77112233);
        Check(!MirrorCeDesktop(candidate,target.Get(),destination)&&Solid(target.Get(),0x77112233),
            "invalid/retired pair or unavailable mirror resource leaves native desktop intact");
    };
    auto bad=pair;bad.borrowId=0;rejected(bad,d);
    bad=pair;bad.key.serial=0;rejected(bad,d);
    bad=pair;bad.key.generation=0;rejected(bad,d);
    bad=pair;bad.key.spaceEpoch=0;rejected(bad,d);
    bad=pair;bad.key.resourceEpoch=0;rejected(bad,d);
    bad=pair;bad.eyes[1]=nullptr;rejected(bad,d);
    bad=pair;bad.eyes[1]=bad.eyes[0];rejected(bad,d);
    bad=pair;bad.covers[0].halfY=0;rejected(bad,d);
    bad=pair;bad.covers[0].halfX=std::numeric_limits<float>::quiet_NaN();rejected(bad,d);
    auto invalid=d;invalid.SampleDesc.Count=2;rejected(pair,invalid);
    invalid=d;invalid.Width=0;rejected(pair,invalid);
    invalid=d;invalid.BindFlags=D3D11_BIND_SHADER_RESOURCE;rejected(pair,invalid);
    failPipeline=true;rejected(pair,d);failPipeline=false;
    failSource=true;rejected(pair,d);failSource=false;
    Check(!MirrorCeDesktop(pair,left.Get(),d)&&!MirrorCeDesktop(pair,right.Get(),d)&&
        Solid(left.Get(),0xff123456)&&Solid(right.Get(),0xffabcdef),"aliasing destination cannot overwrite either headset eye");
    Check(MirrorCeDesktop(pair,target.Get(),d)&&Solid(target.Get(),0xff123456),"mirror recovers on the next valid frame");
    auto* cached=g_ceDesktopRtv;
    Check(MirrorCeDesktop(pair,target.Get(),d)&&g_ceDesktopRtv==cached,"settled desktop target reuses the cached RTV");
    const auto larger=Descriptor(64,24);auto resized=Texture(larger);
    Check(MirrorCeDesktop(pair,resized.Get(),larger)&&g_ceDesktopBuffer==resized.Get()&&Solid(resized.Get(),0xff123456),
        "changed desktop dimensions replace the target view and remain one view");
    g_context->ClearState();
    // D3D11 internal view references need not change the public texture COM
    // count. A private-data IUnknown observes the actual resource destruction.
    const GUID lifetimeGuid={0x644f6bdc,0x269c,0x42f8,{0xa5,0xf1,0x55,0xa2,0x6b,0x52,0x37,0x92}};
    const auto destroyed=std::make_shared<bool>(false);
    auto* marker=new LifetimeMarker(destroyed);
    Check(SUCCEEDED(resized->SetPrivateDataInterface(lifetimeGuid,marker)),"resource lifetime observer attaches");
    marker->Release();resized.Reset();g_context->Flush();
    Check(!*destroyed,"cached desktop RTV keeps its backbuffer alive until release");
    ReleaseCeDesktopMirror();
    g_context->Flush();
    Check(*destroyed&&!g_ceDesktopRtv&&!g_ceDesktopBuffer&&
        !g_ceDesktopPs[0]&&!g_ceDesktopPs[1]&&!g_ceDesktopPs[2],"resize/detach release drops the retained backbuffer and all mirror shaders");
    ReleaseCeDesktopMirror();
    resized=Texture(larger);
    Check(MirrorCeDesktop(pair,resized.Get(),larger)&&Solid(resized.Get(),0xff123456),
        "repeated release and a later valid frame recreate resources successfully");
}
}
int main()
{
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,
        D3D11_SDK_VERSION,&device,nullptr,&context))) return 2;
    g_device=device.Get();g_context=context.Get();
    TestCrop(1,32,16);TestCrop(4,16,16);TestFormats();TestState();TestShaderClasses();TestFailuresAndLifetime();
    context->ClearState();ReleaseCeDesktopMirror();sourceViews.clear();
    if (g_blitVs) g_blitVs->Release();if (g_blitSampler) g_blitSampler->Release();
    if (g_blitRasterizer) g_blitRasterizer->Release();if (g_blitDepthOff) g_blitDepthOff->Release();
    g_device=nullptr;g_context=nullptr;
    if (!failures) std::puts("CE desktop mirror production WARP checks passed");
    return failures?1:0;
}
