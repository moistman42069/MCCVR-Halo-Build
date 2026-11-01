// A host's UI as a panel over the stereo picture, proved on a software device (WARP), with synthetic art:
//   1. A host's draw over the frame, repeated into a transparent picture with reclaimer_host::LayerBlend, gives a
//      picture that laid over the scene as premultiplied colour is the frame the host's own draw produced. For a
//      straight-alpha host that masks alpha writes off, for a premultiplied host, and for a draw with blending off.
//   2. The production panel shader (kHostUiPanelShader, with the production vertex shader) carries that picture
//      into the headset's image: byte for byte into an image of the same encoding, as linear light times alpha
//      into an sRGB image, and opaque when the picture is.
#include "../src/common/host_ui_layer_blend.h"
#include "../src/common/host_ui_panel_shader.h"
#include "../src/common/vr_blit_shader.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
unsigned failures = 0;
constexpr UINT kFrameW = 64, kFrameH = 48;
constexpr UINT kWidgetW = 16, kWidgetH = 12;
// Where the host puts its widget on the frame: each widget texel covers 2 x 2 pixels.
constexpr UINT kAtX = 10, kAtY = 8, kDrawnW = kWidgetW * 2, kDrawnH = kWidgetH * 2;

void Check(bool value, const char* why)
{
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "host UI panel: %s\n", why);
    }
}
ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* target)
{
    ComPtr<ID3DBlob> code, error;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target, 0, 0, &code,
                                  &error);
    Check(SUCCEEDED(hr), entry);
    if (FAILED(hr) && error)
        std::fprintf(stderr, "%s\n", static_cast<const char*>(error->GetBufferPointer()));
    return code;
}
float Decode(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float Encode(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

// The scene the host draws over: every pixel its own colour, opaque.
void Scene(UINT x, UINT y, uint8_t out[4])
{
    out[0] = static_cast<uint8_t>(20 + x * 3);
    out[1] = static_cast<uint8_t>(200 - y * 4);
    out[2] = static_cast<uint8_t>(90 + ((x + y) & 7) * 12);
    out[3] = 255;
}
// The widget: four coverages, a colour that varies along the other axis. `premultiplied`: colour already times
// alpha, as a premultiplied host keeps its art. `opaque`: every texel solid.
void Widget(UINT x, UINT y, bool premultiplied, bool opaque, uint8_t out[4])
{
    static const uint8_t alphas[4]{0, 64, 128, 255};
    const uint8_t a = opaque ? 255 : alphas[x & 3];
    const uint8_t colour[3]{static_cast<uint8_t>(255 - y * 12), static_cast<uint8_t>(40 + y * 15),
                            static_cast<uint8_t>((x >> 2) * 60)};
    for (int c = 0; c < 3; ++c)
        out[c] = premultiplied ? static_cast<uint8_t>((colour[c] * a + 127) / 255) : colour[c];
    out[3] = a;
}

struct Device
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> hostPs, panelPs;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthOff;
    ComPtr<ID3D11SamplerState> point, linear;
    ComPtr<ID3D11Buffer> constants;
};

struct Target
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
};
bool MakeTarget(Device& d, UINT w, UINT h, DXGI_FORMAT format, const uint8_t* pixels, Target& out)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels, w * 4, 0};
    return SUCCEEDED(d.device->CreateTexture2D(&desc, pixels ? &data : nullptr, &out.texture)) &&
        SUCCEEDED(d.device->CreateRenderTargetView(out.texture.Get(), nullptr, &out.rtv)) &&
        SUCCEEDED(d.device->CreateShaderResourceView(out.texture.Get(), nullptr, &out.srv));
}
bool Read(Device& d, ID3D11Texture2D* texture, std::vector<uint8_t>& out)
{
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> stage;
    if (FAILED(d.device->CreateTexture2D(&desc, nullptr, &stage)))
        return false;
    d.context->CopyResource(stage.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(d.context->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    out.resize(size_t(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(out.data() + size_t(y) * desc.Width * 4,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(desc.Width) * 4);
    d.context->Unmap(stage.Get(), 0);
    return true;
}
// One full-viewport triangle with the production vertex shader: the host's draw, and the panel's.
void Draw(Device& d, ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, ID3D11PixelShader* ps,
          ID3D11ShaderResourceView* source, ID3D11SamplerState* sampler, ID3D11BlendState* blend)
{
    d.context->OMSetRenderTargets(1, &target, nullptr);
    d.context->RSSetViewports(1, &viewport);
    d.context->RSSetState(d.raster.Get());
    d.context->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
    d.context->OMSetDepthStencilState(d.depthOff.Get(), 0);
    d.context->IASetInputLayout(nullptr);
    d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d.context->VSSetShader(d.vs.Get(), nullptr, 0);
    d.context->GSSetShader(nullptr, nullptr, 0);
    d.context->PSSetShader(ps, nullptr, 0);
    d.context->PSSetShaderResources(0, 1, &source);
    d.context->PSSetSamplers(0, 1, &sampler);
    ID3D11Buffer* constants = d.constants.Get();
    d.context->PSSetConstantBuffers(0, 1, &constants);
    d.context->Draw(3, 0);
    ID3D11RenderTargetView* none = nullptr;
    ID3D11ShaderResourceView* noSource = nullptr;
    d.context->OMSetRenderTargets(1, &none, nullptr);
    d.context->PSSetShaderResources(0, 1, &noSource);
}

struct Host
{
    const char* name;
    bool blended, premultiplied, opaqueArt;
    D3D11_BLEND srcColour, destColour, srcAlpha, destAlpha;
    UINT8 writeMask;
};
} // namespace

int main()
{
    Device d;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d.device,
                                 nullptr, &d.context)))
        return 2;
    const auto vsCode = Compile(kVrBlitShader, "vs_main", "vs_5_0");
    const auto hostCode = Compile(kVrBlitShader, "ps_pass", "ps_5_0");
    const auto panelCode = Compile(kHostUiPanelShader, "ps_panel", "ps_5_0");
    if (!vsCode || !hostCode || !panelCode)
        return 2;
    Check(SUCCEEDED(d.device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr,
                                                 &d.vs)) &&
              SUCCEEDED(d.device->CreatePixelShader(hostCode->GetBufferPointer(), hostCode->GetBufferSize(), nullptr,
                                                    &d.hostPs)) &&
              SUCCEEDED(d.device->CreatePixelShader(panelCode->GetBufferPointer(), panelCode->GetBufferSize(),
                                                    nullptr, &d.panelPs)),
          "the production vertex shader and the panel shader link");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    D3D11_SAMPLER_DESC sampler{};
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = 16;
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(d.device->CreateRasterizerState(&raster, &d.raster)) ||
        FAILED(d.device->CreateDepthStencilState(&depth, &d.depthOff)) ||
        FAILED(d.device->CreateSamplerState(&sampler, &d.point)) ||
        FAILED(d.device->CreateBuffer(&constantsDesc, nullptr, &d.constants)))
        return 2;
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;   // the blit pipeline's sampler
    if (FAILED(d.device->CreateSamplerState(&sampler, &d.linear)))
        return 2;

    std::vector<uint8_t> scene(size_t(kFrameW) * kFrameH * 4);
    for (UINT y = 0; y < kFrameH; ++y)
        for (UINT x = 0; x < kFrameW; ++x)
            Scene(x, y, scene.data() + (size_t(y) * kFrameW + x) * 4);

    const Host hosts[]{
        {"straight alpha, alpha writes masked off", true, false, false, D3D11_BLEND_SRC_ALPHA,
         D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ZERO, D3D11_BLEND_ONE,
         D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE},
        {"premultiplied alpha", true, true, false, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE,
         D3D11_BLEND_INV_SRC_ALPHA, D3D11_COLOR_WRITE_ENABLE_ALL},
        {"blending off, solid art", false, false, true, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE,
         D3D11_BLEND_ZERO, D3D11_COLOR_WRITE_ENABLE_ALL},
    };
    const D3D11_VIEWPORT widgetAt{static_cast<float>(kAtX), static_cast<float>(kAtY), static_cast<float>(kDrawnW),
                                  static_cast<float>(kDrawnH), 0, 1};
    const D3D11_VIEWPORT whole{0, 0, static_cast<float>(kFrameW), static_cast<float>(kFrameH), 0, 1};
    const float passMode[4]{0, 0, 0, 0};
    for (const Host& host : hosts)
    {
        std::vector<uint8_t> art(size_t(kWidgetW) * kWidgetH * 4);
        for (UINT y = 0; y < kWidgetH; ++y)
            for (UINT x = 0; x < kWidgetW; ++x)
                Widget(x, y, host.premultiplied, host.opaqueArt, art.data() + (size_t(y) * kWidgetW + x) * 4);
        Target widget, frame, picture;
        if (!MakeTarget(d, kWidgetW, kWidgetH, DXGI_FORMAT_R8G8B8A8_UNORM, art.data(), widget) ||
            !MakeTarget(d, kFrameW, kFrameH, DXGI_FORMAT_R8G8B8A8_UNORM, scene.data(), frame) ||
            !MakeTarget(d, kFrameW, kFrameH, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, picture))
            return 2;
        D3D11_BLEND_DESC hostDesc{};
        hostDesc.RenderTarget[0].BlendEnable = host.blended ? TRUE : FALSE;
        hostDesc.RenderTarget[0].SrcBlend = host.srcColour;
        hostDesc.RenderTarget[0].DestBlend = host.destColour;
        hostDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        hostDesc.RenderTarget[0].SrcBlendAlpha = host.srcAlpha;
        hostDesc.RenderTarget[0].DestBlendAlpha = host.destAlpha;
        hostDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        hostDesc.RenderTarget[0].RenderTargetWriteMask = host.writeMask;
        // A host with blending off may leave the default state bound: that is the null case.
        const D3D11_BLEND_DESC layerDesc = reclaimer_host::LayerBlend(host.blended ? &hostDesc : nullptr);
        Check(reclaimer_host::LayerDrawBlended(host.blended ? &hostDesc : nullptr) == host.blended,
              "a draw is told blended from not");
        ComPtr<ID3D11BlendState> hostBlend, layerBlend;
        if (FAILED(d.device->CreateBlendState(&hostDesc, &hostBlend)) ||
            FAILED(d.device->CreateBlendState(&layerDesc, &layerBlend)))
        {
            Check(false, "the blend states are valid");
            continue;
        }
        const float clear[4]{0, 0, 0, 0};
        d.context->ClearRenderTargetView(picture.rtv.Get(), clear);
        d.context->UpdateSubresource(d.constants.Get(), 0, nullptr, passMode, 0, 0);
        // The draw repeated into the picture, then the host's own draw into the frame.
        Draw(d, picture.rtv.Get(), widgetAt, d.hostPs.Get(), widget.srv.Get(), d.point.Get(), layerBlend.Get());
        Draw(d, frame.rtv.Get(), widgetAt, d.hostPs.Get(), widget.srv.Get(), d.point.Get(),
             host.blended ? hostBlend.Get() : nullptr);
        std::vector<uint8_t> framePixels, picturePixels;
        if (!Read(d, frame.texture.Get(), framePixels) || !Read(d, picture.texture.Get(), picturePixels))
            return 2;
        unsigned wrongOver = 0, wrongAlpha = 0;
        int worst = 0;
        for (UINT y = 0; y < kFrameH; ++y)
            for (UINT x = 0; x < kFrameW; ++x)
            {
                const uint8_t* got = picturePixels.data() + (size_t(y) * kFrameW + x) * 4;
                const uint8_t* want = framePixels.data() + (size_t(y) * kFrameW + x) * 4;
                const uint8_t* under = scene.data() + (size_t(y) * kFrameW + x) * 4;
                const bool inside = x >= kAtX && x < kAtX + kDrawnW && y >= kAtY && y < kAtY + kDrawnH;
                uint8_t texel[4]{};
                if (inside)
                    Widget((x - kAtX) / 2, (y - kAtY) / 2, host.premultiplied, host.opaqueArt, texel);
                if (std::abs(static_cast<int>(got[3]) - static_cast<int>(inside ? texel[3] : 0)) > 1)
                    ++wrongAlpha;
                for (int c = 0; c < 3; ++c)
                {
                    // Premultiplied over: the picture's colour plus what shows through it.
                    const int over = got[c] + ((255 - got[3]) * under[c] + 127) / 255;
                    const int off = std::abs(over - static_cast<int>(want[c]));
                    worst = off > worst ? off : worst;
                    wrongOver += off > 2 ? 1u : 0u;
                }
            }
        if (wrongOver || wrongAlpha)
            std::fprintf(stderr, "host UI panel: %s: %u channels over the scene differ from the frame (worst %d), %u "
                                 "alphas are not the art's coverage\n", host.name, wrongOver, worst, wrongAlpha);
        Check(wrongOver == 0, "the picture over the scene is the frame the host drew");
        Check(wrongAlpha == 0, "the picture's alpha is the art's coverage where the host drew, nothing elsewhere");
        // The control: repeated with the host's own blend instead, a host that keeps no alpha leaves the picture
        // without any, and the check above must be able to see that.
        if (host.blended && host.writeMask != D3D11_COLOR_WRITE_ENABLE_ALL)
        {
            Target naive;
            if (!MakeTarget(d, kFrameW, kFrameH, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, naive))
                return 2;
            d.context->ClearRenderTargetView(naive.rtv.Get(), clear);
            Draw(d, naive.rtv.Get(), widgetAt, d.hostPs.Get(), widget.srv.Get(), d.point.Get(), hostBlend.Get());
            std::vector<uint8_t> naivePixels;
            if (!Read(d, naive.texture.Get(), naivePixels))
                return 2;
            unsigned covered = 0;
            for (size_t i = 0; i < naivePixels.size(); i += 4)
                covered += naivePixels[i + 3] ? 1u : 0u;
            Check(covered == 0, "control: with the host's own alpha blend the picture has no coverage at all");
        }

        // 2. The picture into the headset's image.
        Target same, linear, solid;
        if (!MakeTarget(d, kFrameW, kFrameH, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, same) ||
            !MakeTarget(d, kFrameW, kFrameH, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, nullptr, linear) ||
            !MakeTarget(d, kFrameW, kFrameH, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, solid))
            return 2;
        const float junk[4]{0.5f, 0.5f, 0.5f, 0.5f};
        const float linearMode[4]{1, 0, 0, 0}, opaqueMode[4]{0, 1, 0, 0};
        struct Pass
        {
            Target* target;
            const float* mode;
        };
        const Pass passes[]{{&same, passMode}, {&linear, linearMode}, {&solid, opaqueMode}};
        for (const Pass& pass : passes)
        {
            d.context->ClearRenderTargetView(pass.target->rtv.Get(), junk);
            d.context->UpdateSubresource(d.constants.Get(), 0, nullptr, pass.mode, 0, 0);
            Draw(d, pass.target->rtv.Get(), whole, d.panelPs.Get(), picture.srv.Get(), d.linear.Get(), nullptr);
        }
        std::vector<uint8_t> samePixels, linearPixels, solidPixels;
        if (!Read(d, same.texture.Get(), samePixels) || !Read(d, linear.texture.Get(), linearPixels) ||
            !Read(d, solid.texture.Get(), solidPixels))
            return 2;
        unsigned wrongSame = 0, wrongLinear = 0, wrongSolid = 0;
        for (size_t i = 0; i < picturePixels.size(); i += 4)
        {
            const uint8_t* in = picturePixels.data() + i;
            const float a = in[3] / 255.0f;
            for (int c = 0; c < 4; ++c)
            {
                wrongSame += std::abs(static_cast<int>(samePixels[i + c]) - static_cast<int>(in[c])) > 1 ? 1u : 0u;
                const int solidWant = c == 3 ? 255 : in[c];
                wrongSolid += std::abs(static_cast<int>(solidPixels[i + c]) - solidWant) > 1 ? 1u : 0u;
                int linearWant = in[3];
                if (c < 3)
                {
                    float straight = a > 0.0f ? (in[c] / 255.0f) / a : 0.0f;
                    straight = straight > 1.0f ? 1.0f : straight;
                    // The sRGB image stores the encoding of what the shader wrote: linear light times alpha.
                    linearWant = static_cast<int>(std::lround(Encode(Decode(straight) * a) * 255.0f));
                }
                // A coverage of 64/255 leaves the colour 6 bits: the undone multiplication is that coarse.
                const int tolerance = c < 3 ? (in[3] && in[3] < 100 ? 6 : 3) : 1;
                wrongLinear += std::abs(static_cast<int>(linearPixels[i + c]) - linearWant) > tolerance ? 1u : 0u;
            }
        }
        if (wrongSame || wrongLinear || wrongSolid)
            std::fprintf(stderr, "host UI panel: %s: into the image: %u same-encoding, %u linear, %u opaque channels "
                                 "wrong\n", host.name, wrongSame, wrongLinear, wrongSolid);
        Check(wrongSame == 0, "into an image of the picture's own encoding the picture is unchanged");
        Check(wrongLinear == 0, "into an sRGB image it is linear light times alpha");
        Check(wrongSolid == 0, "an opaque picture is solid wherever it is");
    }
    // White text at half coverage keeps a white edge in linear light: not the grey a plain decode of the
    // multiplied colour would give.
    {
        const float edge = Encode(Decode(1.0f) * 0.5f), grey = Encode(Decode(0.5f));
        Check(edge > grey + 0.2f, "undoing the multiplication first matters (the reason the shader does it)");
    }
    if (failures)
        return 1;
    std::printf("host UI panel pixels: ok\n");
    return 0;
}
