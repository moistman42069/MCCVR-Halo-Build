#pragma once

// Shared production upload shader. Kept verbatim when exposed to the focused
// pixel fixture: native RGB-only art needs both visible-ink measurement and
// this straight-alpha conversion before OpenXR composites the aim quad.
// Reach's authored capture (premultiplied colour, see
// AuthoredReticleStraightAlpha) takes the same fix() so its fade applies once.
inline constexpr char kVrBlitShader[]=R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}
float lin(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float4 fix(float4 c)
{
    uint w, h;
    srcTex.GetDimensions(w, h);
    if (w != 512 || h != 512) return c;
    float a = max(c.a, max(c.r, max(c.g, c.b)));
    return float4(a > 0 ? c.rgb / a : c.rgb, a);
}
float4 ps_linearize(VSOut i) : SV_Target
{
    float4 c = fix(srcTex.Sample(smp, i.uv));
    return float4(lin(c.r), lin(c.g), lin(c.b), c.a);
}
float4 ps_pass(VSOut i) : SV_Target
{
    return fix(srcTex.Sample(smp, i.uv));
}
)";
