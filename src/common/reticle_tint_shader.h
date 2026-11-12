#pragma once

// The crosshair's opacity and brightness, applied by the mod to the crosshair's own image on an OpenXR runtime
// that cannot scale a layer's colour (vr.cpp, FinishReticleImage / RetintReticleIfChanged). Drawn with the blit
// pipeline's vertex shader (kVrBlitShader, vs_main) over the whole 512-square image, texel for texel:
//   colour times scale.rgb (brightness), alpha times scale.a (opacity), each clamped to 0..1.
// The crosshair's layer is straight alpha, so the two are independent. Kept verbatim for the pixel fixture
// (tests/reticle_tint_pixels_tests.cpp), which runs it on a software device.
inline constexpr char kReticleTintShader[] = R"(
Texture2D srcTex : register(t0);
cbuffer Tint : register(b0) { float4 scale; };
float4 ps_tint(float4 pos : SV_Position) : SV_Target
{
    return saturate(srcTex.Load(int3(pos.xy, 0)) * scale);
}
)";
