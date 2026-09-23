#pragma once

// Pure, allocation-free logic behind the optional NVIDIA DLSS eye resolve.
// Nothing here touches D3D or NGX, so the core tests exercise every rule the
// render thread relies on: how the DLSS output is shaped inside the headset
// slice, which DLSS quality slot a render/output ratio belongs to, how a title's
// row-vector projection matrix is decoded into the terms the camera-motion
// shader needs, and the reprojection itself (a CPU reference of the HLSL).
//
// Coordinate conventions, fixed here and mirrored in the shader:
// - World: whatever the title uses. Position, forward, up arrive from the eye
//   render hook exactly as the engine rasterized them; right = forward x up,
//   which is the same cross product the eye hooks use for the IPD offset.
// - Projection: Halo 3 / ODST row-vector layout, clip = [x y z 1] * P with
//   rows [P0 0 0 0] [0 P5 0 0] [P8 P9 P10 P11] [0 0 P14 0]. Index 0/5 are the
//   inverse half-frustum tangents the mod already writes, 8/9 the horizontal /
//   vertical center terms (the jitter goes there), 10/14 the depth terms.
// - Screen: uv (0,0) is the top-left pixel, +x right, +y down. Motion vectors
//   are `previous - current` in render-resolution pixels, which is the DLSS
//   contract ("current + mv = where that pixel was last frame").

#include <cmath>
#include <cstdint>
#include "runtime_types.h"

namespace dlss
{
    // Every title publishes its own validated eye camera. CE lends depth and
    // camera through its complete-pair lease; it does not use OM guessing.
    inline constexpr bool TitleHasCameraContract(GameTitle title) noexcept
    {
        return title == GameTitle::Halo3 || title == GameTitle::Halo3ODST ||
            title == GameTitle::HaloReach || title == GameTitle::Halo4 ||
            title == GameTitle::Halo2 || title == GameTitle::HaloCE;
    }

    // OMSetRenderTargetsAndUnorderedAccessViews can also bind Reach's scene
    // depth. KEEP does not mutate the depth state. A borrowed DSV is admitted
    // only while its exact context/device owns a freshly published eye camera.
    inline constexpr bool ReachAlternateDepthBindEligible(
        bool reach, bool requested, int eye, bool cameraPublished,
        bool exactContext, bool exactDevice, bool depthMutated,
        bool depthPresent, bool capacity) noexcept
    {
        return reach && requested && eye >= 0 && eye < 2 && cameraPublished &&
            exactContext && exactDevice && depthMutated && depthPresent && capacity;
    }

    // DLSS-19 was headset-rejected: expensive image matching and long
    // synchronous shader initialization. Preserve the experiments dormant.
    inline constexpr bool kEnableDlss19ImageMotion = false;
    inline constexpr bool kEnableDlss19FusedOutput = false;
    // DLSS-21 rejected: altered standard ratios/output and broke shell menus.
    // Keep its implementation dormant while restoring the normal contract.
    inline constexpr bool kEnableDlss21StereoBatch = false;
    // Independently measured full-output presentation optimization. Unlike
    // the rejected batch, this changes neither NGX dimensions nor its model.
    inline constexpr bool kEnableDlssFullOutputResolve = true;

    inline int ResolveIntermediateCount(bool smaa, bool fxaa, bool rcas,
                                        bool combined) noexcept
    {
        if (combined || (!smaa && !fxaa && !rcas)) return 0;
        if (smaa) return 3;
        return fxaa && rcas ? 2 : 1;
    }
    inline constexpr int kUpscalerOff = 0;
    inline constexpr int kUpscalerDlss = 1;
    inline constexpr int kUpscalerMax = 1;

    // dlss_preset config values. 0 lets the driver choose per mode (on a
    // DLSS 4.5 runtime: K for DLAA/Quality/Balanced, M for Performance, L for
    // Ultra Performance); the named presets exist so one F1 change can compare
    // models in the same "IQ GPU" log line. These are stable config numbers,
    // not NGX enum values (dlss.cpp maps them).
    inline constexpr int kPresetDefault = 0;
    inline constexpr int kPresetFastCnn = 1;       // NGX preset F (CNN)
    inline constexpr int kPresetTransformerJ = 2;  // NGX preset J
    inline constexpr int kPresetTransformerK = 3;  // NGX preset K
    inline constexpr int kPresetTransformerL = 4;  // NGX preset L (DLSS 4.5 Ultra Performance model)
    inline constexpr int kPresetTransformerM = 5;  // NGX preset M (DLSS 4.5 Performance model)
    inline constexpr int kPresetCnn = 6;           // NGX preset E (CNN), the cheapest model
    inline constexpr int kPresetMax = 6;

    // DLSS quality slots, in the order the render/output ratio grows.
    enum class Quality : uint8_t
    {
        Dlaa = 0,          // 1.0x
        MaxQuality,        // 1.5x
        Balanced,          // ~1.72x
        MaxPerformance,    // 2.0x
        UltraPerformance,  // 3.0x
        Count
    };

    inline const char* QualityName(Quality q) noexcept
    {
        switch (q)
        {
        case Quality::Dlaa: return "DLAA";
        case Quality::MaxQuality: return "Quality";
        case Quality::Balanced: return "Balanced";
        case Quality::MaxPerformance: return "Performance";
        case Quality::UltraPerformance: return "Ultra Performance";
        default: return "?";
        }
    }

    inline const char* PresetName(int preset) noexcept
    {
        switch (preset)
        {
        case kPresetDefault: return "driver default";
        case kPresetFastCnn: return "F (CNN)";
        case kPresetTransformerJ: return "J (transformer)";
        case kPresetTransformerK: return "K (transformer)";
        case kPresetTransformerL: return "L (transformer, 4.5 ultra-performance model)";
        case kPresetTransformerM: return "M (transformer, 4.5 performance model)";
        case kPresetCnn: return "E (CNN)";
        default: return "?";
        }
    }

    // --- Render plan ---------------------------------------------------------

    // dlss_mode config values are the Quality enum above (0 DLAA .. 4 Ultra
    // Performance): what fraction of the headset picture the game rasterizes.
    inline constexpr int kModeMax = static_cast<int>(Quality::UltraPerformance);

    // NVIDIA's published per-axis render scale for each mode (Programming
    // Guide 3.2, quality modes): DLAA 1.0, Quality 1.5, Balanced 1/0.58,
    // Performance 2.0, Ultra Performance 3.0. NGX's optimal-settings query
    // returns these same values; they are fixed here so the launcher can size
    // the game's render before any GPU device exists.
    inline float ModeRatio(Quality q) noexcept
    {
        switch (q)
        {
        case Quality::MaxQuality: return 1.5f;
        case Quality::Balanced: return 1.0f / 0.58f;
        case Quality::MaxPerformance: return 2.0f;
        case Quality::UltraPerformance: return 3.0f;
        default: return 1.0f;
        }
    }

    inline Quality ModeFromConfig(int mode) noexcept
    {
        if (mode < 0 || mode > kModeMax)
            return Quality::MaxQuality;
        return static_cast<Quality>(mode);
    }

    // The launcher's even rounding, unchanged since the first release: the
    // nearest integer in float arithmetic, bumped up to even.
    inline int ScaleEven(int base, float scale) noexcept
    {
        int value = static_cast<int>(std::lround(static_cast<float>(base) * scale));
        if (value & 1)
            ++value;
        return value;
    }

    // The DLSS render size is NOT rounded up to even: NGX derives its own
    // admissible range from round(output / ratio), and the Ultra Performance
    // slot admits ONLY that exact size (its min, max and optimal are all
    // 971x700 for a 2912x2100 picture on the player's 310.8.0 runtime). The
    // even bump asked for 972x700, which no slot admitted, so Ultra
    // Performance silently fell back to the mod's own resolve.
    inline int ScaleRender(int base, float scale) noexcept
    {
        return static_cast<int>(std::lround(static_cast<float>(base) * scale));
    }

    // One game start's sizes, shared by the launcher (-ResX/-ResY), the DLL's
    // forced backbuffer, the F1 menu and the DLSS output at run time:
    //   output = native * resolution_scale, the headset picture the mod has
    //            always produced (with DLSS on, DLSS makes it; the final
    //            stretch into the headset slice is unchanged);
    //   render = output / ModeRatio(mode) when DLSS and its runtime are present;
    //            otherwise the output itself, which is the pre-DLSS behaviour
    //            and the AMD / Intel / no-DLL case (never shrunk).
    struct RenderPlan
    {
        int outputW = 0, outputH = 0;
        int renderW = 0, renderH = 0;
        Quality mode = Quality::Dlaa;
        bool dlss = false; // the render was shrunk for DLSS
    };

    // Performance-first stereo layout. Both fresh eyes are packed side by
    // side into one DLSS feature. Each reconstructed eye is half the requested
    // linear picture size, then the existing display resolve expands it into
    // the unchanged XR slice. This quarters both the pair's neural output
    // pixels and its game-raster pixels versus two full per-eye features.
    // A multiple of six keeps DLAA, Quality (3:2), Performance (2:1), and
    // Ultra Performance (3:1) dimensions integral; Balanced uses its normal
    // admitted range query.
    struct StereoBatchPlan
    {
        int eyeRenderW = 0, eyeRenderH = 0;
        int atlasRenderW = 0, atlasRenderH = 0;
        int atlasOutputW = 0, atlasOutputH = 0;
        int eyeOutputW = 0, eyeOutputH = 0;
    };

    inline int FloorMultiple(int value, int multiple) noexcept
    {
        return value > 0 && multiple > 0 ? value - value % multiple : 0;
    }

    inline StereoBatchPlan PlanStereoBatch(int outputW, int outputH,
                                            Quality mode) noexcept
    {
        StereoBatchPlan batch{};
        if (outputW < 192 || outputH < 192)
            return batch;
        batch.atlasOutputW = FloorMultiple(outputW, 6);
        batch.atlasOutputH = FloorMultiple(outputH / 2, 6);
        if (batch.atlasOutputW < 96 || batch.atlasOutputH < 96)
            return {};
        batch.eyeOutputW = batch.atlasOutputW / 2;
        batch.eyeOutputH = batch.atlasOutputH;
        const float inverse = 1.0f / ModeRatio(mode);
        batch.atlasRenderW = ScaleRender(batch.atlasOutputW, inverse);
        // Equal-width eye sources require an even atlas input. Nearest even is
        // within the non-UP quality ranges; the /6 output makes UP exact.
        if (batch.atlasRenderW & 1)
            ++batch.atlasRenderW;
        batch.atlasRenderH = ScaleRender(batch.atlasOutputH, inverse);
        batch.eyeRenderW = batch.atlasRenderW / 2;
        batch.eyeRenderH = batch.atlasRenderH;
        if (batch.eyeRenderW < 32 || batch.eyeRenderH < 32)
            return {};
        return batch;
    }

    inline RenderPlan PlanRender(int nativeW, int nativeH, float scale,
                                 int upscaler, int mode,
                                 bool runtimePresent) noexcept
    {
        RenderPlan plan{};
        if (nativeW <= 0 || nativeH <= 0 || !(scale > 0.0f) || !std::isfinite(scale))
            return plan;
        plan.mode = ModeFromConfig(mode);
        plan.dlss = upscaler == kUpscalerDlss && runtimePresent;
        // The headset picture is native x resolution_scale whether or not
        // DLSS is on: the scale is the user's picture size and must apply
        // live in every mode (DLSS-9 headset result). DLSS only decides how
        // much of that picture the game rasterises.
        plan.outputW = ScaleEven(nativeW, scale);
        plan.outputH = ScaleEven(nativeH, scale);
        plan.renderW = plan.outputW;
        plan.renderH = plan.outputH;
        if (plan.dlss && !kEnableDlss21StereoBatch)
        {
            const float inverse = 1.0f / ModeRatio(plan.mode);
            plan.renderW = ScaleRender(plan.outputW, inverse);
            plan.renderH = ScaleRender(plan.outputH, inverse);
            if (plan.renderW < 32) plan.renderW = 32;
            if (plan.renderH < 32) plan.renderH = 32;
        }
        else if (plan.dlss)
        {
            const StereoBatchPlan batch = PlanStereoBatch(
                plan.outputW, plan.outputH, plan.mode);
            if (batch.eyeRenderW && batch.eyeRenderH)
            {
                plan.renderW = batch.eyeRenderW;
                plan.renderH = batch.eyeRenderH;
            }
            else
            {
                plan.dlss = false;
            }
        }
        return plan;
    }

    // A flat menu/loading canvas is not a DLSS input. Its draw coordinates,
    // window metrics and hit testing must all use the full configured picture.
    // Only an in-world presentation uses the normal DLSS input ratio. Keeping
    // this shared avoids different launch sizes in Steam and Store.
    inline bool UsesWorldRender(RuntimeMode mode) noexcept
    {
        switch (mode)
        {
        case RuntimeMode::Gameplay:
        case RuntimeMode::Cutscene:
        case RuntimeMode::Vehicle:
        case RuntimeMode::Turret:
        case RuntimeMode::Dead:
            return true;
        default:
            return false;
        }
    }

    // Historical transition-size hold, disabled after user rejection. The
    // normal full-resolution flat-canvas policy below remains active.
    inline bool CanChangeRenderPlan(RuntimeMode mode) noexcept
    {
        // Rejected workaround: preserve its implementation, but restore the
        // full-resolution pause/loading behavior while fixing the lifecycle.
        constexpr bool enableTransitionResizeHold = false;
        return !enableTransitionResizeHold ||
            mode == RuntimeMode::Shell || UsesWorldRender(mode);
    }

    inline RenderPlan PlanPresentation(RenderPlan world, RuntimeMode mode) noexcept
    {
        if (!UsesWorldRender(mode))
        {
            world.renderW = world.outputW;
            world.renderH = world.outputH;
            world.dlss = false;
        }
        return world;
    }

    struct OutputSize
    {
        uint32_t width = 0;
        uint32_t height = 0;
        // True when the output IS the headset slice, so the finished DLSS
        // image is copied straight into the swapchain with no second pass.
        bool exact = false;
    };

    // The planned headset picture as the DLSS output for one raster, or an
    // empty size when the raster does not belong to that plan: a different
    // shape (more than 1% aspect difference: the fit is off and MCC chose its
    // own resolution, or an old launcher) or larger than the picture (DLSS
    // never downscales). The caller then uses FitOutput below.
    inline OutputSize PlannedOutput(
        uint32_t renderW, uint32_t renderH,
        uint32_t planW, uint32_t planH,
        uint32_t sliceW, uint32_t sliceH) noexcept
    {
        OutputSize out{};
        if (!renderW || !renderH || !planW || !planH)
            return out;
        if (renderW > planW || renderH > planH)
            return out;
        const double renderAspect = static_cast<double>(renderW) / renderH;
        const double planAspect = static_cast<double>(planW) / planH;
        if (std::fabs(renderAspect - planAspect) > 0.01 * planAspect)
            return out;
        out.width = planW;
        out.height = planH;
        out.exact = planW == sliceW && planH == sliceH;
        return out;
    }

    // DLSS reconstructs correctly only when the render and output rectangles
    // share one aspect ratio (Programming Guide 3.2.2). The title's raster and
    // the headset slice do not: the raster is the symmetric cover mapped onto
    // the slice with an anisotropic stretch. So the DLSS output is the largest
    // raster-shaped rectangle that fits the slice (never smaller than the
    // raster itself, so a supersampled raster runs DLAA and is downscaled
    // afterwards), and the existing final expansion stretches that into the
    // slice. When the raster already has the slice's shape the output is exact.
    inline OutputSize FitOutput(
        uint32_t renderW, uint32_t renderH,
        uint32_t sliceW, uint32_t sliceH) noexcept
    {
        OutputSize out{};
        if (!renderW || !renderH || !sliceW || !sliceH)
            return out;
        const double aspect = static_cast<double>(renderW) / renderH;
        double w = static_cast<double>(sliceW);
        double h = w / aspect;
        if (h > static_cast<double>(sliceH))
        {
            h = static_cast<double>(sliceH);
            w = h * aspect;
        }
        uint32_t width = static_cast<uint32_t>(std::lround(w));
        uint32_t height = static_cast<uint32_t>(std::lround(h));
        // Keep even dimensions: the anisotropic final stretch and the eye
        // caches are even-sized everywhere else in this mod.
        width &= ~1u;
        height &= ~1u;
        if (width < renderW || height < renderH)
        {
            width = renderW;
            height = renderH;
        }
        if (width < 32) width = 32;
        if (height < 32) height = 32;
        out.width = width;
        out.height = height;
        out.exact = width == sliceW && height == sliceH;
        return out;
    }

    // The DLSS quality slot whose nominal scale is nearest the actual ratio.
    // The feature is created with the exact raster size (dynamic-resolution
    // contract), so the slot only tells the model what scale to expect.
    inline Quality QualityForRatio(float ratio) noexcept
    {
        if (!(ratio > 0.0f) || !std::isfinite(ratio))
            return Quality::Dlaa;
        if (ratio < 1.25f) return Quality::Dlaa;
        if (ratio < 1.61f) return Quality::MaxQuality;
        if (ratio < 1.86f) return Quality::Balanced;
        if (ratio < 2.5f) return Quality::MaxPerformance;
        return Quality::UltraPerformance;
    }

    // Order in which quality slots are tried for a given ratio: nearest first,
    // then outward, so a raster outside one slot's admissible render range can
    // still be served by its neighbour instead of failing open.
    inline int QualitySearchOrder(Quality nearest, Quality out[5]) noexcept
    {
        const int base = static_cast<int>(nearest);
        int count = 0;
        out[count++] = nearest;
        for (int distance = 1; distance < 5; ++distance)
        {
            if (base + distance < static_cast<int>(Quality::Count))
                out[count++] = static_cast<Quality>(base + distance);
            if (base - distance >= 0)
                out[count++] = static_cast<Quality>(base - distance);
        }
        return count;
    }

    inline bool RenderSizeAdmitted(
        uint32_t renderW, uint32_t renderH,
        uint32_t minW, uint32_t minH, uint32_t maxW, uint32_t maxH) noexcept
    {
        return renderW >= minW && renderH >= minH &&
            renderW <= maxW && renderH <= maxH;
    }

    // Decoded terms of one eye's projection. tanX/tanY are the symmetric
    // half-frustum tangents, centerX/Y the NDC center offsets (unjittered),
    // and depth follows d = depthA + depthB / t for a point t units along
    // forward, so t = depthB / (d - depthA).
    struct ProjectionTerms
    {
        float tanX = 0.0f;
        float tanY = 0.0f;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float depthA = 0.0f;
        float depthB = 0.0f;
        bool depthInverted = false; // far plane at 0 (reversed Z)
        // The matrix arrived in the column-vector (transposed) layout and was
        // transposed before decoding; the terms above are the same either way.
        bool transposed = false;
        bool valid = false;
    };

    inline bool Finite3(const float v[3]) noexcept
    {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    }

    // The factor a hook multiplies a wanted NDC centre offset by before adding
    // it to P8/P9 of a row-vector projection, so the rasterized image really
    // moves by that offset: +1 for w = +z, -1 for w = -z (Halo).
    inline float ProjectionCenterSign(const float p[16]) noexcept
    {
        return (p && p[11] < 0.0f) ? -1.0f : 1.0f;
    }

    inline ProjectionTerms DecodeRowVectorProjection(
        const float p[16], float jitterNdcX, float jitterNdcY) noexcept
    {
        ProjectionTerms terms{};
        if (!p)
            return terms;
        for (int i = 0; i < 16; ++i)
        {
            if (!std::isfinite(p[i]))
                return terms;
        }
        const float p0 = p[0], p5 = p[5], p8 = p[8], p9 = p[9];
        const float p10 = p[10], p11 = p[11], p14 = p[14], p15 = p[15];
        // A perspective row-vector projection has w = +-z and no constant w.
        if (std::fabs(std::fabs(p11) - 1.0f) > 1e-3f || std::fabs(p15) > 1e-4f)
            return terms;
        if (!(p0 > 1e-4f) || !(p5 > 1e-4f) || p14 == 0.0f)
            return terms;
        // The center terms row must be the only off-axis contribution.
        if (std::fabs(p[1]) > 1e-4f || std::fabs(p[2]) > 1e-4f ||
            std::fabs(p[3]) > 1e-4f || std::fabs(p[4]) > 1e-4f ||
            std::fabs(p[6]) > 1e-4f || std::fabs(p[7]) > 1e-4f ||
            std::fabs(p[12]) > 1e-4f || std::fabs(p[13]) > 1e-4f)
            return terms;
        // NDC x = (x*P0 + z*P8) / (z*P11): the centre offset the raster sees
        // is P8*w, so a hook that wants the image shifted by +jitter must add
        // jitter*w to P8 (ProjectionCenterSign). Halo 3's live matrix has
        // P11 = -1 (read through Cheat Engine 2026-09-05); DLSS-1..5 added
        // +jitter and told DLSS +jitter, i.e. the wrong sign on both axes.
        const float w = p11 > 0.0f ? 1.0f : -1.0f;
        terms.tanX = 1.0f / p0;
        terms.tanY = 1.0f / p5;
        terms.centerX = p8 * w - jitterNdcX;
        terms.centerY = p9 * w - jitterNdcY;
        terms.depthA = p10 * w;
        terms.depthB = p14;
        // Standard Z reaches 1 at the far plane (depthA -> 1); reversed Z
        // reaches 0 (depthA -> 0).
        terms.depthInverted = terms.depthA < 0.5f;
        terms.valid = true;
        return terms;
    }

    // Decodes the Halo row-vector layout first; a matrix stored the other
    // way round (column-vector: w in [14], centre terms in [2]/[6]) is
    // transposed and decoded with the same rules, flagged `transposed`. A
    // genuine perspective matrix can satisfy only one of the two (|w| = 1
    // sits in exactly one of [11]/[14]), so the order cannot mis-decode.
    inline ProjectionTerms DecodeProjection(
        const float p[16], float jitterNdcX, float jitterNdcY) noexcept
    {
        ProjectionTerms terms = DecodeRowVectorProjection(p, jitterNdcX, jitterNdcY);
        if (terms.valid || !p)
            return terms;
        float t[16];
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
                t[row * 4 + column] = p[column * 4 + row];
        }
        terms = DecodeRowVectorProjection(t, jitterNdcX, jitterNdcY);
        terms.transposed = terms.valid;
        return terms;
    }

    struct CameraSample
    {
        float position[3]{};
        float forward[3]{};
        float up[3]{};
        float right[3]{};
        ProjectionTerms projection{};
        // The jitter that was added to the projection center for this render,
        // in NDC units; DLSS is told the same offset in render pixels.
        float jitterNdcX = 0.0f;
        float jitterNdcY = 0.0f;
        bool valid = false;
    };

    inline void Cross(const float a[3], const float b[3], float out[3]) noexcept
    {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    }

    inline float Dot(const float a[3], const float b[3]) noexcept
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    // Builds a sample from what an eye render hook has in hand. Rejects
    // non-finite or degenerate bases so the shader never sees garbage.
    inline CameraSample MakeCameraSample(
        const float position[3], const float forward[3], const float up[3],
        const float projection[16], float jitterNdcX, float jitterNdcY) noexcept
    {
        CameraSample s{};
        if (!position || !forward || !up || !projection ||
            !Finite3(position) || !Finite3(forward) || !Finite3(up) ||
            !std::isfinite(jitterNdcX) || !std::isfinite(jitterNdcY))
            return s;
        const float fl = Dot(forward, forward);
        const float ul = Dot(up, up);
        if (fl < 0.9f || fl > 1.1f || ul < 0.9f || ul > 1.1f)
            return s;
        if (std::fabs(Dot(forward, up)) > 0.1f)
            return s;
        s.projection = DecodeProjection(projection, jitterNdcX, jitterNdcY);
        if (!s.projection.valid)
            return s;
        for (int i = 0; i < 3; ++i)
        {
            s.position[i] = position[i];
            s.forward[i] = forward[i];
            s.up[i] = up[i];
        }
        Cross(forward, up, s.right);
        s.jitterNdcX = jitterNdcX;
        s.jitterNdcY = jitterNdcY;
        s.valid = true;
        return s;
    }

    // Distance along forward for a raw depth value, or a negative number when
    // the depth does not describe a finite point in front of the camera.
    inline float ViewDistance(const ProjectionTerms& t, float depth) noexcept
    {
        const float denominator = depth - t.depthA;
        if (!std::isfinite(depth) || std::fabs(denominator) < 1e-9f)
            return -1.0f;
        const float distance = t.depthB / denominator;
        if (!std::isfinite(distance) || distance <= 0.0f)
            return -1.0f;
        return distance;
    }

    // CPU reference of the motion-vector shader: for pixel (u,v) with raw
    // depth d rendered by `current`, the offset (in render pixels, +y down)
    // to where that world point sat in `previous`. Returns false when the
    // point was behind the previous camera; the caller then writes zero.
    inline bool ReprojectPixel(
        const CameraSample& current, const CameraSample& previous,
        float u, float v, float depth, uint32_t renderW, uint32_t renderH,
        float& outMvX, float& outMvY) noexcept
    {
        outMvX = 0.0f;
        outMvY = 0.0f;
        if (!current.valid || !previous.valid || !renderW || !renderH)
            return false;
        const ProjectionTerms& cp = current.projection;
        const ProjectionTerms& pp = previous.projection;
        // Depth belongs to the jittered raster sample, while the decoded
        // projection and DLSS motion are unjittered. Recover that sample's
        // actual ray, then measure motion relative to its unjittered UV.
        const float sampleU = u - current.jitterNdcX * 0.5f;
        const float sampleV = v + current.jitterNdcY * 0.5f;
        const float ndcX = 2.0f * sampleU - 1.0f;
        const float ndcY = 1.0f - 2.0f * sampleV;
        const float sx = (ndcX - cp.centerX) * cp.tanX;
        const float sy = (ndcY - cp.centerY) * cp.tanY;
        float direction[3];
        for (int i = 0; i < 3; ++i)
        {
            direction[i] = current.forward[i] + current.right[i] * sx +
                current.up[i] * sy;
        }
        const float t = ViewDistance(cp, depth);
        float relative[3];
        if (t > 0.0f && t < 1.0e7f)
        {
            for (int i = 0; i < 3; ++i)
            {
                relative[i] = current.position[i] + direction[i] * t -
                    previous.position[i];
            }
        }
        else
        {
            // Sky / far plane: treat the point as infinitely far, so only
            // the rotation between the two cameras moves it.
            for (int i = 0; i < 3; ++i)
                relative[i] = direction[i];
        }
        const float tp = Dot(relative, previous.forward);
        if (tp <= 1e-6f)
            return false;
        const float px = Dot(relative, previous.right) / (tp * pp.tanX) +
            pp.centerX;
        const float py = Dot(relative, previous.up) / (tp * pp.tanY) +
            pp.centerY;
        const float prevU = (px + 1.0f) * 0.5f;
        const float prevV = (1.0f - py) * 0.5f;
        outMvX = (prevU - sampleU) * static_cast<float>(renderW);
        outMvY = (prevV - sampleV) * static_cast<float>(renderH);
        return std::isfinite(outMvX) && std::isfinite(outMvY);
    }

    // A jump this large between consecutive renders is a level load, a
    // respawn, or a teleport; DLSS must drop its history rather than smear it.
    // One Halo world unit is about three metres.
    inline constexpr float kResetDistanceWorldUnits = 1.0f;

    inline bool ShouldReset(
        const CameraSample& current, const CameraSample& previous) noexcept
    {
        if (!current.valid || !previous.valid)
            return true;
        float delta[3];
        for (int i = 0; i < 3; ++i)
            delta[i] = current.position[i] - previous.position[i];
        if (Dot(delta, delta) >
            kResetDistanceWorldUnits * kResetDistanceWorldUnits)
            return true;
        // A different depth mapping (near/far change, theatre projection)
        // makes the previous history geometrically wrong.
        return current.projection.depthInverted !=
            previous.projection.depthInverted;
    }

    // --- Sub-pixel jitter -------------------------------------------------

    inline float Halton(uint32_t index, uint32_t base) noexcept
    {
        float result = 0.0f;
        float fraction = 1.0f / static_cast<float>(base);
        while (index > 0)
        {
            result += fraction * static_cast<float>(index % base);
            index /= base;
            fraction /= static_cast<float>(base);
        }
        return result;
    }

    // Programming Guide 3.7.1.1: 8 phases scaled by the pixel-area ratio.
    inline uint32_t JitterPhaseCount(uint32_t renderW, uint32_t outputW) noexcept
    {
        if (!renderW || !outputW)
            return 8;
        const double ratio = static_cast<double>(outputW) / renderW;
        double phases = 8.0 * ratio * ratio;
        if (phases < 8.0) phases = 8.0;
        if (phases > 128.0) phases = 128.0;
        return static_cast<uint32_t>(std::ceil(phases));
    }

    // Jitter in render pixels for a phase index, each axis inside (-0.5, 0.5).
    inline void JitterPixels(
        uint32_t phase, uint32_t phaseCount, float& outX, float& outY) noexcept
    {
        if (!phaseCount)
            phaseCount = 8;
        const uint32_t index = (phase % phaseCount) + 1; // Halton starts at 1
        outX = Halton(index, 2) - 0.5f;
        outY = Halton(index, 3) - 0.5f;
    }

    // A pixel offset (+x right, +y down) becomes a projection-center offset
    // in NDC (+x right, +y up): the whole image shifts by that many pixels.
    inline void JitterToNdc(
        float pixelX, float pixelY, uint32_t renderW, uint32_t renderH,
        float& outNdcX, float& outNdcY) noexcept
    {
        outNdcX = renderW ? 2.0f * pixelX / static_cast<float>(renderW) : 0.0f;
        outNdcY = renderH ? -2.0f * pixelY / static_cast<float>(renderH) : 0.0f;
    }

    inline void NdcToJitterPixels(
        float ndcX, float ndcY, uint32_t renderW, uint32_t renderH,
        float& outPixelX, float& outPixelY) noexcept
    {
        outPixelX = ndcX * static_cast<float>(renderW) * 0.5f;
        outPixelY = -ndcY * static_cast<float>(renderH) * 0.5f;
    }

    // This render's jitter minus the previous render's, in render pixels
    // (+x right, +y down). Motion measured between the two rasterised images
    // (optical flow, previous - current) contains the OPPOSITE of this on top
    // of the real motion, so adding it recovers the unjittered vector DLSS
    // expects: a static point rendered at p + j_prev and then at p + j_cur
    // measures j_prev - j_cur; plus (j_cur - j_prev) is zero.
    inline void JitterDeltaPixels(
        const CameraSample& current, const CameraSample& previous,
        uint32_t renderW, uint32_t renderH, float& outDx, float& outDy) noexcept
    {
        float curX = 0.0f, curY = 0.0f, prevX = 0.0f, prevY = 0.0f;
        NdcToJitterPixels(current.jitterNdcX, current.jitterNdcY, renderW, renderH, curX, curY);
        NdcToJitterPixels(previous.jitterNdcX, previous.jitterNdcY, renderW, renderH, prevX, prevY);
        outDx = curX - prevX;
        outDy = curY - prevY;
    }

    // --- Texture mip bias ---------------------------------------------------

    // Programming Guide 3.5: with DLSS on, textures must be sampled for the
    // OUTPUT resolution, not the render resolution the rasterizer picks
    // mips for, or every surface arrives blurred and the upscale can only
    // sharpen blur. bias = log2(render / output) - 1 (+ epsilon, 0 here):
    // -2.0 at Performance, -2.58 at Ultra Performance, -1.58 at Quality,
    // -1.0 at DLAA. Zero when nothing is being upscaled.
    inline float MipBias(uint32_t renderW, uint32_t outputW) noexcept
    {
        if (!renderW || !outputW || renderW > outputW)
            return 0.0f;
        const float bias = std::log2(static_cast<float>(renderW) / static_cast<float>(outputW)) - 1.0f;
        return bias < -8.0f ? -8.0f : bias;
    }

    // --- Depth copy formats ------------------------------------------------

    // DXGI_FORMAT values, spelled out so this header stays free of d3d11.h.
    inline constexpr uint32_t kDxgiR32G8X24Typeless = 19;
    inline constexpr uint32_t kDxgiD32FloatS8X24Uint = 20;
    inline constexpr uint32_t kDxgiR32FloatX8X24Typeless = 21;
    inline constexpr uint32_t kDxgiR32Typeless = 39;
    inline constexpr uint32_t kDxgiD32Float = 40;
    inline constexpr uint32_t kDxgiR32Float = 41;
    inline constexpr uint32_t kDxgiR24G8Typeless = 44;
    inline constexpr uint32_t kDxgiD24UnormS8Uint = 45;
    inline constexpr uint32_t kDxgiR24UnormX8Typeless = 46;
    inline constexpr uint32_t kDxgiR16Typeless = 53;
    inline constexpr uint32_t kDxgiD16Unorm = 55;
    inline constexpr uint32_t kDxgiR16Unorm = 56;

    // The texture format a CopyResource-compatible, shader-readable copy of an
    // engine depth buffer must use, and the view format that reads its depth
    // channel. False for anything that is not a depth format.
    inline bool DepthCopyFormats(
        uint32_t engineFormat, uint32_t& outTexture, uint32_t& outView) noexcept
    {
        switch (engineFormat)
        {
        case kDxgiR32G8X24Typeless:
        case kDxgiD32FloatS8X24Uint:
        case kDxgiR32FloatX8X24Typeless:
            outTexture = kDxgiR32G8X24Typeless;
            outView = kDxgiR32FloatX8X24Typeless;
            return true;
        case kDxgiR32Typeless:
        case kDxgiD32Float:
        case kDxgiR32Float:
            outTexture = kDxgiR32Typeless;
            outView = kDxgiR32Float;
            return true;
        case kDxgiR24G8Typeless:
        case kDxgiD24UnormS8Uint:
        case kDxgiR24UnormX8Typeless:
            outTexture = kDxgiR24G8Typeless;
            outView = kDxgiR24UnormX8Typeless;
            return true;
        case kDxgiR16Typeless:
        case kDxgiD16Unorm:
        case kDxgiR16Unorm:
            outTexture = kDxgiR16Typeless;
            outView = kDxgiR16Unorm;
            return true;
        default:
            return false;
        }
    }

    // Which depth buffer represents the eye. A depth view bound together with
    // the learned scene-colour target is the strongest evidence; otherwise the
    // last full-raster depth view bound inside the eye is used.
    inline bool ChooseDepthSource(
        bool boundWithSceneColor, bool fullRasterSeen, bool& outUsedSceneBound) noexcept
    {
        outUsedSceneBound = boundWithSceneColor;
        return boundWithSceneColor || fullRasterSeen;
    }
}
