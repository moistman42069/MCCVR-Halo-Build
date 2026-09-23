// Mirrors dlss::ReprojectPixel; embedded at build time.

Texture2D<float> depthTex : register(t0);
Texture2D<float2> motionTex : register(t1);
Texture2D<float4> colorTex : register(t3);
SamplerState smp : register(s0);
cbuffer MotionParams : register(b0)
{
    float4 curPos, curRight, curUp, curFwd, curProj, curDepth;
    float4 prevPos, prevRight, prevUp, prevFwd, prevProj, prevDepth;
    float4 size;
    float4 debugParams;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct MotionOut { float2 motion : SV_Target0; float depth : SV_Target1; };
MotionOut ps_motion(VSOut i)
{
    MotionOut o;
    // SV_Position includes the viewport origin. Stereo batching draws each
    // eye into a different atlas viewport while sampling an eye-local depth
    // texture, so remove that origin before the load. debugParams.w is zero
    // for the ordinary per-eye/debug path.
    int2 px = int2((i.pos.xy - float2(debugParams.w, 0.0)) * size.zw);
    float d = depthTex.Load(int3(px, 0));
    o.depth = d;
    o.motion = float2(0.0, 0.0);
    float2 sampleUv = i.uv - float2(debugParams.y, -debugParams.z) * 0.5;
    float2 ndc = float2(2.0 * sampleUv.x - 1.0, 1.0 - 2.0 * sampleUv.y);
    float3 dir = curFwd.xyz
        + curRight.xyz * ((ndc.x - curProj.z) * curProj.x)
        + curUp.xyz * ((ndc.y - curProj.w) * curProj.y);
    float denom = d - curDepth.x;
    float t = (abs(denom) > 1e-9) ? curDepth.y / denom : -1.0;
    float3 rel = (t > 0.0 && t < 1.0e7)
        ? (curPos.xyz + dir * t - prevPos.xyz) : dir;
    float tp = dot(rel, prevFwd.xyz);
    if (tp <= 1e-6) return o;
    float ppx = dot(rel, prevRight.xyz) / (tp * prevProj.x) + prevProj.z;
    float ppy = dot(rel, prevUp.xyz) / (tp * prevProj.y) + prevProj.w;
    float2 prevUv = float2((ppx + 1.0) * 0.5, (1.0 - ppy) * 0.5);
    o.motion = (prevUv - sampleUv) * size.xy;
    return o;
}
float4 ps_copy(VSOut i) : SV_Target
{
    return colorTex.Load(int3(int2(i.pos.xy), 0));
}
float4 ps_debug(VSOut i) : SV_Target
{
    float2 mv = motionTex.SampleLevel(smp, i.uv, 0);
    float s = debugParams.x;
    return float4(saturate(mv.x * s + 0.5), saturate(mv.y * s + 0.5), 0.5, 1.0);
}
