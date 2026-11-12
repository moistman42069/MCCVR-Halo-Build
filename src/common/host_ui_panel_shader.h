#pragma once

// The host's UI picture, drawn into the panel's image (vr_host_ui_panel.inl) with the blit pipeline's vertex
// shader (kVrBlitShader, vs_main). The picture holds colour already multiplied by alpha, as the host's draws left
// it (host_ui_layer_blend.h); the image the headset's compositor blends wants the same.
//   mode.x = 1: the picture's colour is display-encoded and the image stores linear light (an sRGB image). The
//               multiplication by alpha was done on encoded colour, so it is undone first, the colour made linear,
//               and multiplied again: an edge of white text stays white at half coverage instead of going grey.
//   mode.y = 1: the picture is opaque wherever it is (PanelAlpha::Opaque): its alpha is not read.
// Kept verbatim for the pixel fixture (tests/host_ui_panel_pixels_tests.cpp), which runs it on a software device.
inline constexpr char kHostUiPanelShader[] = R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
cbuffer Panel : register(b0) { float4 mode; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float lin(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float4 ps_panel(VSOut i) : SV_Target
{
    float4 c = srcTex.Sample(smp, i.uv);
    float a = mode.y > 0.5 ? 1.0 : saturate(c.a);
    float3 rgb = saturate(c.rgb);
    if (mode.x > 0.5)
    {
        float3 straight = a > 1.0 / 510.0 ? saturate(rgb / a) : float3(0, 0, 0);
        rgb = float3(lin(straight.r), lin(straight.g), lin(straight.b)) * a;
    }
    return float4(rgb, a);
}
)";
