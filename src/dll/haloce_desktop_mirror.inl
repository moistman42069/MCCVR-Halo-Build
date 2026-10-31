// Included by vr.cpp and the focused WARP production-body fixture. Present only:
// an EyeCache::Completed borrow must remain held until this draw is issued.
namespace
{
ID3D11RenderTargetView* g_ceDesktopRtv{};
ID3D11Texture2D* g_ceDesktopBuffer{}; // identity held by g_ceDesktopRtv
ID3D11PixelShader* g_ceDesktopPs[3]{};
ID3D11Texture2D* g_desktopProbe{}; // one texel, in g_desktopProbeFormat
DXGI_FORMAT g_desktopProbeFormat{};

// The shared blit backup predates shader class linkage and tessellation.
// Keep the complete shader bindings local to this optional desktop draw.
template<class Shader> struct CeDesktopShaderBinding
{
    Shader* shader{};
    ID3D11ClassInstance* classes[D3D11_SHADER_MAX_INTERFACES]{};
    UINT count{D3D11_SHADER_MAX_INTERFACES};
    ~CeDesktopShaderBinding()
    {
        if (shader) shader->Release();
        for (auto* instance:classes) if (instance) instance->Release();
    }
};

void ReleaseCeDesktopMirror()
{
    if (g_ceDesktopRtv) g_ceDesktopRtv->Release();
    g_ceDesktopRtv=nullptr;g_ceDesktopBuffer=nullptr;
    for (auto*& shader:g_ceDesktopPs)
    { if (shader) shader->Release();shader=nullptr; }
    if (g_desktopProbe) g_desktopProbe->Release();
    g_desktopProbe=nullptr;
}

// The backbuffer's middle texel as it stands, its first four bytes. It waits for the GPU: for occasional evidence only.
bool ReadDesktopMiddle(ID3D11Texture2D* backbuffer,const D3D11_TEXTURE2D_DESC& descriptor,uint32_t& texel)
{
    if (!g_device||!g_context||!backbuffer||descriptor.SampleDesc.Count!=1) return false;
    if (g_desktopProbe&&g_desktopProbeFormat!=descriptor.Format)
    { g_desktopProbe->Release();g_desktopProbe=nullptr; }
    if (!g_desktopProbe)
    {
        D3D11_TEXTURE2D_DESC probe{};
        probe.Width=probe.Height=probe.MipLevels=probe.ArraySize=1;
        probe.Format=descriptor.Format;
        probe.SampleDesc.Count=1;
        probe.Usage=D3D11_USAGE_STAGING;
        probe.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if (FAILED(g_device->CreateTexture2D(&probe,nullptr,&g_desktopProbe))) return false;
        g_desktopProbeFormat=descriptor.Format;
    }
    const D3D11_BOX middle{descriptor.Width/2,descriptor.Height/2,0,descriptor.Width/2+1,descriptor.Height/2+1,1};
    g_context->CopySubresourceRegion(g_desktopProbe,0,0,0,0,backbuffer,0,&middle);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(g_desktopProbe,0,D3D11_MAP_READ,0,&mapped))) return false;
    memcpy(&texel,mapped.pData,sizeof(texel));
    g_context->Unmap(g_desktopProbe,0);
    return true;
}

bool CeDesktopViewport(const halo_ce::EyeCache::Completed& pair,
    const D3D11_TEXTURE2D_DESC& destination,D3D11_VIEWPORT& viewport)
{
    const auto& cover=pair.covers[0];
    if (!pair.borrowId||!pair.key.generation||!pair.key.serial||
        !pair.key.spaceEpoch||!pair.key.resourceEpoch||
        !pair.eyes[0]||!pair.eyes[1]||(pair.eyes[0]==pair.eyes[1])!=(pair.monoEye>=0)||
        !destination.Width||!destination.Height||
        destination.Width>16384||destination.Height>16384||
        destination.ArraySize!=1||destination.MipLevels!=1||
        destination.SampleDesc.Count!=1||destination.SampleDesc.Quality||
        !(destination.BindFlags&D3D11_BIND_RENDER_TARGET)||
        !std::isfinite(cover.halfX)||!std::isfinite(cover.halfY)||
        cover.halfX<=0||cover.halfY<=0||cover.halfX>=1.55f||cover.halfY>=1.55f)
        return false;
    // CE's packed eye can have non-square raster pixels. Its frozen camera
    // cover, not the atlas dimensions, defines the image's physical aspect.
    // Overscan one axis to center-crop the eye into the desktop rectangle.
    const float aspect=std::tan(cover.halfX)/std::tan(cover.halfY);
    float width=static_cast<float>(destination.Width);
    float height=static_cast<float>(destination.Height);
    if (width/height<aspect) width=height*aspect;
    else height=width/aspect;
    viewport={(destination.Width-width)*0.5f,(destination.Height-height)*0.5f,
        width,height,0,1};
    return std::isfinite(width)&&std::isfinite(height)&&width<=32767&&height<=32767&&
        viewport.TopLeftX>=-32768&&viewport.TopLeftY>=-32768;
}

// One finished eye into the backbuffer, as `viewport` places it.
bool DrawEyeToDesktop(ID3D11Texture2D* eye,const D3D11_TEXTURE2D_DESC& eyeDescriptor,
    ID3D11Texture2D* backbuffer,const D3D11_TEXTURE2D_DESC& descriptor,const D3D11_VIEWPORT& viewport)
{
    if (!g_device||!g_context||!backbuffer||!eye||backbuffer==eye||!EnsureBlitPipeline()) return false;
    auto* source=AcquireSrcSrv(eye,eyeDescriptor);
    if (!source) return false;
    // Use the real typed view format, including TYPELESS -> UNORM, rather
    // than the OpenXR target format assumed by the ordinary world upload.
    D3D11_SHADER_RESOURCE_VIEW_DESC sourceView{};source->GetDesc(&sourceView);
    const bool sourceSrgb=IsSrgb(sourceView.Format),targetSrgb=IsSrgb(descriptor.Format);
    const unsigned conversion=sourceSrgb==targetSrgb?0:targetSrgb?1:2;
    if (!g_ceDesktopPs[conversion])
    {
        // No reticle alpha repair: this is a finished color eye of any size.
        static constexpr char shaderSource[]=R"(
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
struct Varying { float4 position : SV_Position; float2 uv : TEXCOORD0; };
float DecodeSrgbComponent(float x) { return x<=0.04045?x/12.92:pow((x+0.055)/1.055,2.4); }
float EncodeSrgbComponent(float x) { return x<=0.0031308?12.92*x:1.055*pow(x,1.0/2.4)-0.055; }
float4 copy(Varying i) : SV_Target { return image.Sample(imageSampler,i.uv); }
float4 decode(Varying i) : SV_Target {
    float4 c=copy(i); return float4(DecodeSrgbComponent(c.r),DecodeSrgbComponent(c.g),DecodeSrgbComponent(c.b),c.a); }
float4 encode(Varying i) : SV_Target {
    float4 c=copy(i); return float4(EncodeSrgbComponent(c.r),EncodeSrgbComponent(c.g),EncodeSrgbComponent(c.b),c.a); }
)";
        const char* entries[]={"copy","decode","encode"};
        ID3DBlob* blob{};
        const HRESULT compiled=D3DCompile(shaderSource,sizeof(shaderSource)-1,nullptr,
            nullptr,nullptr,entries[conversion],"ps_5_0",0,0,&blob,nullptr);
        if (FAILED(compiled)||!blob) { if (blob) blob->Release();return false; }
        const HRESULT created=g_device->CreatePixelShader(blob->GetBufferPointer(),
            blob->GetBufferSize(),nullptr,&g_ceDesktopPs[conversion]);
        blob->Release();if (FAILED(created)) return false;
    }
    if (g_ceDesktopBuffer!=backbuffer)
    {
        if (g_ceDesktopRtv) g_ceDesktopRtv->Release();
        g_ceDesktopRtv=nullptr;g_ceDesktopBuffer=nullptr;
        if (FAILED(g_device->CreateRenderTargetView(backbuffer,nullptr,&g_ceDesktopRtv)))
            return false;
        g_ceDesktopBuffer=backbuffer;
    }
    D3DStateBackup backup;backup.Capture(g_context);
    CeDesktopShaderBinding<ID3D11VertexShader> vertex;
    CeDesktopShaderBinding<ID3D11PixelShader> pixel;
    CeDesktopShaderBinding<ID3D11GeometryShader> geometry;
    CeDesktopShaderBinding<ID3D11HullShader> hull;
    CeDesktopShaderBinding<ID3D11DomainShader> domain;
    g_context->VSGetShader(&vertex.shader,vertex.classes,&vertex.count);
    g_context->PSGetShader(&pixel.shader,pixel.classes,&pixel.count);
    g_context->GSGetShader(&geometry.shader,geometry.classes,&geometry.count);
    g_context->HSGetShader(&hull.shader,hull.classes,&hull.count);
    g_context->DSGetShader(&domain.shader,domain.classes,&domain.count);
    g_context->OMSetRenderTargets(1,&g_ceDesktopRtv,nullptr);
    g_context->RSSetViewports(1,&viewport);
    g_context->RSSetState(g_blitRasterizer);
    g_context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_blitDepthOff,0);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blitVs,nullptr,0);
    g_context->HSSetShader(nullptr,nullptr,0);
    g_context->DSSetShader(nullptr,nullptr,0);
    g_context->GSSetShader(nullptr,nullptr,0);
    g_context->PSSetShader(g_ceDesktopPs[conversion],nullptr,0);
    g_context->PSSetShaderResources(0,1,&source);
    g_context->PSSetSamplers(0,1,&g_blitSampler);
    g_context->Draw(3,0);
    const bool hadNoViewports=backup.numViewports==0;
    // Never bind a dynamically linked native shader with zero instances,
    // even transiently: the shared backup cannot restore those instances.
    // Our complete bindings above retain each shader until restoration.
    if (backup.vs) backup.vs->Release();
    if (backup.ps) backup.ps->Release();
    if (backup.gs) backup.gs->Release();
    backup.vs=nullptr;backup.ps=nullptr;backup.gs=nullptr;
    backup.Restore(g_context);
    if (hadNoViewports) g_context->RSSetViewports(0,nullptr);
    g_context->VSSetShader(vertex.shader,vertex.classes,vertex.count);
    g_context->PSSetShader(pixel.shader,pixel.classes,pixel.count);
    g_context->GSSetShader(geometry.shader,geometry.classes,geometry.count);
    g_context->HSSetShader(hull.shader,hull.classes,hull.count);
    g_context->DSSetShader(domain.shader,domain.classes,domain.count);
    return true;
}

bool MirrorCeDesktop(const halo_ce::EyeCache::Completed& pair,
    ID3D11Texture2D* backbuffer,const D3D11_TEXTURE2D_DESC& descriptor)
{
    D3D11_VIEWPORT viewport{};
    if (!backbuffer||backbuffer==pair.eyes[1]||!CeDesktopViewport(pair,descriptor,viewport)) return false;
    return DrawEyeToDesktop(pair.eyes[0],pair.descriptor,backbuffer,descriptor,viewport);
}
}
