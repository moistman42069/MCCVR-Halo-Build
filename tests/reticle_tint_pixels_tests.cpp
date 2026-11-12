// The crosshair's opacity and brightness on a runtime that cannot scale a layer's colour: the mod redraws the
// crosshair's image from an untouched master copy through kReticleTintShader. This WARP fixture runs that
// production shader, with the production vertex shader, the way vr.cpp's DrawReticleTint binds them, and reads
// the pixels back: colour times brightness, alpha times opacity, each clamped; texel for texel; and, on an sRGB
// image, in linear light (what a compositor's layer colour scale does). The art is synthetic.
#include "../src/common/reticle_tint_shader.h"
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
constexpr UINT kSize = 512;
void Check(bool value, const char* why)
{
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "reticle tint: %s\n", why);
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
// Four known texels, repeated: each texel's own position decides which, so a shifted or filtered read shows.
const uint8_t kArt[4][4]{
    {40, 100, 200, 90},    // a faint blue: the DMR at rest
    {255, 255, 255, 255},  // solid white
    {0, 0, 0, 0},          // nothing
    {128, 64, 32, 128},
};
std::vector<uint8_t> Art()
{
    std::vector<uint8_t> px(size_t(kSize) * kSize * 4);
    for (UINT y = 0; y < kSize; ++y)
        for (UINT x = 0; x < kSize; ++x)
            std::memcpy(px.data() + (size_t(y) * kSize + x) * 4, kArt[(x + 2 * (y & 1)) & 3], 4);
    return px;
}
float Decode(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float Encode(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
int Expected(uint8_t byte, float scale, bool srgbColour)
{
    float v = byte / 255.0f;
    if (srgbColour)
        v = Decode(v);
    v *= scale;
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    if (srgbColour)
        v = Encode(v);
    return static_cast<int>(std::lround(v * 255.0f));
}
} // namespace

int main()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                 nullptr, &context)))
        return 2;
    const auto vsCode = Compile(kVrBlitShader, "vs_main", "vs_5_0");
    const auto psCode = Compile(kReticleTintShader, "ps_tint", "ps_5_0");
    if (!vsCode || !psCode)
        return 2;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    Check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs)) &&
              SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps)),
          "the production vertex shader and the tint shader link");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterState;
    D3D11_DEPTH_STENCIL_DESC depth{};
    ComPtr<ID3D11DepthStencilState> depthOff;
    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = 16;
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    if (FAILED(device->CreateRasterizerState(&raster, &rasterState)) ||
        FAILED(device->CreateDepthStencilState(&depth, &depthOff)) ||
        FAILED(device->CreateBuffer(&constantsDesc, nullptr, &constants)))
        return 2;

    const std::vector<uint8_t> art = Art();
    struct Case
    {
        const char* name;
        float opacity, brightness;
    };
    const Case cases[]{
        {"as the game draws it", 1.0f, 1.0f},   {"twice as solid", 2.0f, 1.0f},
        {"half as solid", 0.5f, 1.0f},          {"twice as bright", 1.0f, 2.0f},
        {"half as bright", 1.0f, 0.5f},         {"both at their top", 2.0f, 2.0f},
        {"invisible", 0.0f, 1.0f},              {"black", 1.0f, 0.0f},
    };
    // The image's stored format, and the format its views name: a typed image, an sRGB one, and the TYPELESS
    // image SteamVR can hand out (whose views must name the negotiated format).
    struct Format
    {
        const char* name;
        DXGI_FORMAT texture, view;
        bool srgb;
    };
    const Format formats[]{
        {"RGBA8", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, false},
        {"RGBA8 sRGB", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true},
        {"typeless viewed as sRGB", DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true},
    };
    for (const Format& format : formats)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = kSize;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = format.texture;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{art.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> master, image;
        ComPtr<ID3D11ShaderResourceView> masterSrv;
        ComPtr<ID3D11RenderTargetView> imageRtv;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format.view;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = format.view;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        Check(SUCCEEDED(device->CreateTexture2D(&desc, &data, &master)) &&
                  SUCCEEDED(device->CreateShaderResourceView(master.Get(), &srvDesc, &masterSrv)),
              "the master copy and its view");
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &image)) &&
                  SUCCEEDED(device->CreateRenderTargetView(image.Get(), &rtvDesc, &imageRtv)),
              "the crosshair's image and its view");
        if (!masterSrv || !imageRtv)
            return 2;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stage;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &stage)))
            return 2;
        for (const Case& test : cases)
        {
            const float scale[4]{test.brightness, test.brightness, test.brightness, test.opacity};
            context->UpdateSubresource(constants.Get(), 0, nullptr, scale, 0, 0);
            const float junk[4]{0.5f, 0.5f, 0.5f, 0.5f};
            context->ClearRenderTargetView(imageRtv.Get(), junk);
            ID3D11RenderTargetView* rtv = imageRtv.Get();
            context->OMSetRenderTargets(1, &rtv, nullptr);
            D3D11_VIEWPORT vp{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1};
            context->RSSetViewports(1, &vp);
            context->RSSetState(rasterState.Get());
            context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
            context->OMSetDepthStencilState(depthOff.Get(), 0);
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vs.Get(), nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->PSSetShader(ps.Get(), nullptr, 0);
            ID3D11ShaderResourceView* srv = masterSrv.Get();
            context->PSSetShaderResources(0, 1, &srv);
            ID3D11Buffer* cb = constants.Get();
            context->PSSetConstantBuffers(0, 1, &cb);
            context->Draw(3, 0);
            ID3D11RenderTargetView* none = nullptr;
            context->OMSetRenderTargets(1, &none, nullptr);
            context->CopyResource(stage.Get(), image.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
                return 2;
            int worst = 0;
            unsigned wrong = 0;
            for (UINT y = 0; y < kSize; ++y)
                for (UINT x = 0; x < kSize; ++x)
                {
                    const uint8_t* got = static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch +
                                         size_t(x) * 4;
                    const uint8_t* in = kArt[(x + 2 * (y & 1)) & 3];
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const bool colour = channel < 3;
                        const int want = Expected(in[channel], colour ? test.brightness : test.opacity,
                                                  colour && format.srgb);
                        const int off = std::abs(static_cast<int>(got[channel]) - want);
                        worst = off > worst ? off : worst;
                        wrong += off > 1 ? 1u : 0u;
                    }
                }
            context->Unmap(stage.Get(), 0);
            if (wrong)
                std::fprintf(stderr, "reticle tint: %s, %s: %u channels off by more than 1 (worst %d)\n", format.name,
                             test.name, wrong, worst);
            Check(wrong == 0, "every texel is its own texel, colour times brightness and alpha times opacity");
            // 100% and 100% through the shader would be the art itself (the mod does not even draw then).
            if (test.opacity == 1.0f && test.brightness == 1.0f)
                Check(worst == 0, "at 100% and 100% the image is the art, byte for byte");
        }
    }
    if (failures)
        return 1;
    std::printf("reticle tint pixels: ok\n");
    return 0;
}
