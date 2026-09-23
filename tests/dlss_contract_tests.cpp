// Adapted from pancreations 3f86346: production math and frame ownership tests.
#include "../src/common/dlss_logic.h"
#include "../src/common/dlss_frame_result.h"
#include "../src/common/live_resize_logic.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <cstdlib>
static int g_failures = 0;
static void Check(bool condition, const char* reason) {
    if (!condition) { ++g_failures; std::cerr << reason << "\n"; }
}
int main() {
    // --- Optional DLSS eye resolve: pure logic behind the render path -----
    {
        // Output shape: the largest raster-shaped box inside the headset
        // slice (the user's logged 3262x2352 raster into a 3292x3524 slice),
        // never smaller than the raster, and exact when the shapes agree.
        const dlss::OutputSize fit = dlss::FitOutput(3262, 2352, 3292, 3524);
        Check(fit.width == 3292 && fit.height == 2374 && !fit.exact,
              "DLSS output keeps the raster aspect and fills the slice width");
        const dlss::OutputSize supersampled = dlss::FitOutput(3786, 2730, 3292, 3524);
        Check(supersampled.width == 3786 && supersampled.height == 2730 &&
                  !supersampled.exact,
              "DLSS output never shrinks a supersampled raster (DLAA + downscale)");
        const dlss::OutputSize exact = dlss::FitOutput(3292, 3524, 3292, 3524);
        Check(exact.width == 3292 && exact.height == 3524 && exact.exact,
              "DLSS output is the slice itself when the raster already matches it");
        const dlss::OutputSize tall = dlss::FitOutput(1000, 2000, 3000, 3000);
        Check(tall.width == 1500 && tall.height == 3000 && !tall.exact,
              "DLSS output fits a tall raster by its height");
        Check(dlss::FitOutput(0, 10, 10, 10).width == 0 &&
                  dlss::FitOutput(10, 10, 0, 10).width == 0,
              "DLSS output rejects empty inputs");

        // Quality slot by ratio, nearest first and then outward.
        Check(dlss::QualityForRatio(1.0f) == dlss::Quality::Dlaa &&
                  dlss::QualityForRatio(1.5f) == dlss::Quality::MaxQuality &&
                  dlss::QualityForRatio(1.72f) == dlss::Quality::Balanced &&
                  dlss::QualityForRatio(2.0f) == dlss::Quality::MaxPerformance &&
                  dlss::QualityForRatio(3.0f) == dlss::Quality::UltraPerformance &&
                  dlss::QualityForRatio(0.0f) == dlss::Quality::Dlaa,
              "DLSS quality slot follows the nominal scale nearest the ratio");
        dlss::Quality order[5];
        const int orderCount = dlss::QualitySearchOrder(dlss::Quality::Balanced, order);
        Check(orderCount == 5 && order[0] == dlss::Quality::Balanced &&
                  order[1] == dlss::Quality::MaxPerformance &&
                  order[2] == dlss::Quality::MaxQuality &&
                  order[3] == dlss::Quality::UltraPerformance &&
                  order[4] == dlss::Quality::Dlaa,
              "DLSS quality search tries the nearest slot first, then its neighbours");
        Check(dlss::RenderSizeAdmitted(1000, 800, 900, 700, 1200, 900) &&
                  !dlss::RenderSizeAdmitted(1300, 800, 900, 700, 1200, 900),
              "DLSS render range check is inclusive on both ends");

        // Projection decode: Halo 3's row-vector layout, standard Z.
        const float nearPlane = 0.05f, farPlane = 10240.0f;
        float standard[16] = {};
        standard[0] = 1.0f / 1.0916f;          // 1/tanX
        standard[5] = 1.0f / 1.1143f;          // 1/tanY
        standard[10] = farPlane / (farPlane - nearPlane);     // depth A
        standard[11] = 1.0f;                   // w = z
        standard[14] = -nearPlane * farPlane / (farPlane - nearPlane);
        const dlss::ProjectionTerms terms = dlss::DecodeProjection(standard, 0.0f, 0.0f);
        Check(terms.valid && std::fabs(terms.tanX - 1.0916f) < 1e-4f &&
                  std::fabs(terms.tanY - 1.1143f) < 1e-4f && !terms.depthInverted &&
                  terms.centerX == 0.0f && terms.centerY == 0.0f,
              "DLSS decodes tangents and standard depth from the row-vector projection");
        const float nearDepth = terms.depthA + terms.depthB / nearPlane;
        const float farDepth = terms.depthA + terms.depthB / farPlane;
        Check(std::fabs(nearDepth) < 1e-4f && std::fabs(farDepth - 1.0f) < 1e-4f &&
                  std::fabs(dlss::ViewDistance(terms, terms.depthA + terms.depthB / 12.0f) - 12.0f) < 1e-2f,
              "DLSS depth terms map near to 0, far to 1, and invert back to distance");
        // The same matrix stored column-vector (transposed) decodes to the
        // same terms and is flagged; the row-vector original is not.
        float transposedStandard[16] = {};
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
                transposedStandard[row * 4 + column] = standard[column * 4 + row];
        }
        const dlss::ProjectionTerms viaTranspose =
            dlss::DecodeProjection(transposedStandard, 0.0f, 0.0f);
        Check(viaTranspose.valid && viaTranspose.transposed && !terms.transposed &&
                  std::fabs(viaTranspose.tanX - terms.tanX) < 1e-6f &&
                  std::fabs(viaTranspose.tanY - terms.tanY) < 1e-6f &&
                  viaTranspose.depthA == terms.depthA &&
                  viaTranspose.depthB == terms.depthB &&
                  viaTranspose.depthInverted == terms.depthInverted,
              "DLSS decodes a column-vector (transposed) projection to the same terms");
        float notAProjection[16] = {};
        notAProjection[0] = 1.0f;
        notAProjection[5] = 1.0f;
        notAProjection[10] = 1.0f;
        notAProjection[15] = 1.0f; // identity: no w term in [11] or [14]
        Check(!dlss::DecodeProjection(notAProjection, 0.0f, 0.0f).valid,
              "DLSS rejects a matrix that is a perspective in neither layout");
        // Reversed Z, w = -z, jittered centre.
        float reversed[16] = {};
        reversed[0] = 2.0f;
        reversed[5] = 2.0f;
        // Engine centre 0 plus the hook's jitter write of jitter * sign(P11):
        // +0.001 * -1 and -0.002 * -1, which the decoder must remove.
        reversed[8] = -0.001f;
        reversed[9] = 0.002f;
        reversed[10] = -(nearPlane / (nearPlane - farPlane)); // A' such that A = A'/P11 = nearPlane/(nearPlane-farPlane)
        reversed[11] = -1.0f;
        reversed[14] = -farPlane * nearPlane / (nearPlane - farPlane);
        const dlss::ProjectionTerms rev = dlss::DecodeProjection(reversed, 0.001f, -0.002f);
        Check(rev.valid && rev.depthInverted && std::fabs(rev.tanX - 0.5f) < 1e-6f &&
                  std::fabs(rev.centerX) < 1e-7f && std::fabs(rev.centerY) < 1e-7f &&
                  std::fabs(rev.depthA + rev.depthB / nearPlane - 1.0f) < 1e-4f &&
                  std::fabs(rev.depthA + rev.depthB / farPlane) < 1e-4f,
              "DLSS decodes reversed Z through a negative w row and removes the jitter");
        Check(dlss::ProjectionCenterSign(reversed) == -1.0f &&
                  dlss::ProjectionCenterSign(standard) == 1.0f,
              "the centre-term jitter sign follows the sign of the w row");
        // An unjittered w = -z matrix with an engine centre term: the NDC
        // centre the raster sees is P8 * w.
        float centred[16] = {};
        centred[0] = 2.0f; centred[5] = 2.0f; centred[8] = 0.004f;
        centred[10] = 0.0f; centred[11] = -1.0f; centred[14] = 0.01f;
        Check(std::fabs(dlss::DecodeProjection(centred, 0.0f, 0.0f).centerX + 0.004f) < 1e-7f,
              "a w = -z projection's centre term is negated in NDC");
        float orthographic[16] = {};
        orthographic[0] = 1.0f; orthographic[5] = 1.0f; orthographic[10] = 1.0f;
        orthographic[15] = 1.0f;
        Check(!dlss::DecodeProjection(orthographic, 0.0f, 0.0f).valid &&
                  !dlss::DecodeProjection(nullptr, 0.0f, 0.0f).valid,
              "DLSS rejects non-perspective or missing projections");

        // Camera reprojection (the shader's CPU twin). Halo frame: X forward,
        // Z up, so right = forward x up = -Y.
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const float forward[3] = {1.0f, 0.0f, 0.0f};
        const float up[3] = {0.0f, 0.0f, 1.0f};
        const dlss::CameraSample still = dlss::MakeCameraSample(
            origin, forward, up, standard, 0.0f, 0.0f);
        Check(still.valid && still.right[0] == 0.0f && still.right[1] == -1.0f &&
                  still.right[2] == 0.0f,
              "DLSS camera sample derives right = forward x up in Halo's frame");
        const float depthAt20 = terms.depthA + terms.depthB / 20.0f;
        float mvX = 1.0f, mvY = 1.0f;
        Check(dlss::ReprojectPixel(still, still, 0.3f, 0.7f, depthAt20, 1000, 800, mvX, mvY) &&
                  std::fabs(mvX) < 1e-3f && std::fabs(mvY) < 1e-3f,
              "DLSS motion is zero when the camera did not move");
        // Previous camera yawed 2 degrees to the left: the point straight
        // ahead now sat to the RIGHT of centre last frame, so mv.x > 0.
        const float yaw = 2.0f * 3.14159265f / 180.0f;
        const float prevForward[3] = {std::cos(yaw), std::sin(yaw), 0.0f};
        const dlss::CameraSample yawed = dlss::MakeCameraSample(
            origin, prevForward, up, standard, 0.0f, 0.0f);
        Check(dlss::ReprojectPixel(still, yawed, 0.5f, 0.5f, depthAt20, 1000, 800, mvX, mvY) &&
                  mvX > 10.0f && std::fabs(mvY) < 1e-2f,
              "DLSS motion points to where the pixel was after a camera yaw");
        {
            auto jittered = still;
            jittered.jitterNdcX = 0.001f;
            jittered.jitterNdcY = -0.001f;
            float baseX=0,baseY=0,jitterX=0,jitterY=0;
            Check(dlss::ReprojectPixel(still,yawed,0.3f,0.7f,depthAt20,1000,800,baseX,baseY) &&
                dlss::ReprojectPixel(jittered,yawed,0.3005f,0.7005f,depthAt20,1000,800,jitterX,jitterY) &&
                std::fabs(baseX-jitterX)<1e-4f && std::fabs(baseY-jitterY)<1e-4f,
                "DLSS yaw motion follows the same surface sample when raster jitter moves the pixel");
        }
        // Sky pixels (far plane) still follow the rotation.
        const float skyDepth = terms.depthA + terms.depthB / 1.0e9f;
        Check(dlss::ReprojectPixel(still, yawed, 0.5f, 0.5f, skyDepth, 1000, 800, mvX, mvY) &&
                  mvX > 10.0f,
              "DLSS sky motion is rotation-only and never dropped");
        // Previous camera one unit behind: an off-centre point at 20 units
        // was nearer the centre last frame, and a nearer point moves more.
        const float behind[3] = {-1.0f, 0.0f, 0.0f};
        const dlss::CameraSample fromBehind = dlss::MakeCameraSample(
            behind, forward, up, standard, 0.0f, 0.0f);
        float farMvX = 0.0f, farMvY = 0.0f, nearMvX = 0.0f, nearMvY = 0.0f;
        const float depthAt2 = terms.depthA + terms.depthB / 2.0f;
        Check(dlss::ReprojectPixel(still, fromBehind, 0.75f, 0.5f, depthAt20, 1000, 800, farMvX, farMvY) &&
                  dlss::ReprojectPixel(still, fromBehind, 0.75f, 0.5f, depthAt2, 1000, 800, nearMvX, nearMvY) &&
                  farMvX < -1.0f && nearMvX < farMvX && std::fabs(farMvY) < 1e-2f,
              "DLSS motion from camera translation depends on depth");
        const float notUnit[3] = {2.0f, 0.0f, 0.0f};
        Check(!dlss::MakeCameraSample(origin, notUnit, up, standard, 0.0f, 0.0f).valid &&
                  !dlss::MakeCameraSample(origin, forward, forward, standard, 0.0f, 0.0f).valid,
              "DLSS camera sample rejects a non-unit or non-orthogonal basis");
        const float teleport[3] = {50.0f, 0.0f, 0.0f};
        const dlss::CameraSample far50 = dlss::MakeCameraSample(
            teleport, forward, up, standard, 0.0f, 0.0f);
        const dlss::CameraSample reversedCamera = dlss::MakeCameraSample(
            origin, forward, up, reversed, 0.001f, -0.002f);
        Check(!dlss::ShouldReset(still, fromBehind) && dlss::ShouldReset(still, far50) &&
                  dlss::ShouldReset(still, dlss::CameraSample{}) &&
                  dlss::ShouldReset(still, reversedCamera),
              "DLSS drops its history on teleports, missing history and depth-mapping changes");

        // Jitter: Halton(2,3) inside the pixel, phase count per the guide,
        // pixel <-> NDC round trip with +y down in pixels and up in NDC.
        Check(std::fabs(dlss::Halton(1, 2) - 0.5f) < 1e-6f &&
                  std::fabs(dlss::Halton(2, 2) - 0.25f) < 1e-6f &&
                  std::fabs(dlss::Halton(1, 3) - 1.0f / 3.0f) < 1e-6f,
              "DLSS Halton sequence matches the textbook values");
        bool jitterBounded = true;
        for (uint32_t phase = 0; phase < 64 && jitterBounded; ++phase)
        {
            float jx = 0.0f, jy = 0.0f;
            dlss::JitterPixels(phase, 32, jx, jy);
            jitterBounded = jx > -0.5f && jx < 0.5f && jy > -0.5f && jy < 0.5f;
        }
        Check(jitterBounded, "DLSS jitter stays strictly inside one render pixel");
        Check(dlss::JitterPhaseCount(1000, 1000) == 8 &&
                  dlss::JitterPhaseCount(1000, 1500) == 18 &&
                  dlss::JitterPhaseCount(1000, 2000) == 32 &&
                  dlss::JitterPhaseCount(1000, 3000) == 72 &&
                  dlss::JitterPhaseCount(0, 3000) == 8,
              "DLSS jitter phase count follows 8 x (output/render)^2");
        float ndcX = 0.0f, ndcY = 0.0f, pixelX = 0.0f, pixelY = 0.0f;
        dlss::JitterToNdc(0.25f, -0.125f, 1000, 800, ndcX, ndcY);
        dlss::NdcToJitterPixels(ndcX, ndcY, 1000, 800, pixelX, pixelY);
        Check(std::fabs(ndcX - 0.0005f) < 1e-7f && std::fabs(ndcY - 0.0003125f) < 1e-7f &&
                  std::fabs(pixelX - 0.25f) < 1e-6f && std::fabs(pixelY + 0.125f) < 1e-6f,
              "DLSS jitter converts between render pixels and projection-centre NDC exactly");
        {
            // Flow de-jitter: a static point measures j_prev - j_cur; adding
            // the delta (j_cur - j_prev) cancels it exactly.
            dlss::CameraSample cur{}, prev{};
            dlss::JitterToNdc(0.25f, -0.125f, 1000, 800, cur.jitterNdcX, cur.jitterNdcY);
            dlss::JitterToNdc(-0.5f, 0.375f, 1000, 800, prev.jitterNdcX, prev.jitterNdcY);
            float dx = 0.0f, dy = 0.0f;
            dlss::JitterDeltaPixels(cur, prev, 1000, 800, dx, dy);
            const float measuredX = -0.5f - 0.25f, measuredY = 0.375f + 0.125f;
            Check(std::fabs(dx - 0.75f) < 1e-5f && std::fabs(dy + 0.5f) < 1e-5f &&
                      std::fabs(measuredX + dx) < 1e-5f && std::fabs(measuredY + dy) < 1e-5f,
                  "DLSS flow de-jitter cancels the jitter difference between two renders");
        }
        // Mip bias per the guide: log2(render/output) - 1; none without upscaling.
        Check(std::fabs(dlss::MipBias(1456, 2912) + 2.0f) < 1e-5f &&
                  std::fabs(dlss::MipBias(971, 2912) + 2.5844f) < 1e-3f &&
                  std::fabs(dlss::MipBias(1941, 2912) + 1.5852f) < 1e-3f &&
                  std::fabs(dlss::MipBias(2912, 2912) + 1.0f) < 1e-5f &&
                  dlss::MipBias(0, 2912) == 0.0f && dlss::MipBias(3000, 2912) == 0.0f,
              "DLSS texture mip bias follows the programming guide per mode");

        // Depth copy formats: every depth family maps to its typeless twin and
        // a depth-reading view; colour formats are refused.
        uint32_t copyFormat = 0, viewFormat = 0;
        Check(dlss::DepthCopyFormats(dlss::kDxgiD24UnormS8Uint, copyFormat, viewFormat) &&
                  copyFormat == dlss::kDxgiR24G8Typeless &&
                  viewFormat == dlss::kDxgiR24UnormX8Typeless &&
                  dlss::DepthCopyFormats(dlss::kDxgiD32Float, copyFormat, viewFormat) &&
                  copyFormat == dlss::kDxgiR32Typeless && viewFormat == dlss::kDxgiR32Float &&
                  dlss::DepthCopyFormats(dlss::kDxgiR32G8X24Typeless, copyFormat, viewFormat) &&
                  viewFormat == dlss::kDxgiR32FloatX8X24Typeless &&
                  dlss::DepthCopyFormats(dlss::kDxgiD16Unorm, copyFormat, viewFormat) &&
                  copyFormat == dlss::kDxgiR16Typeless &&
                  !dlss::DepthCopyFormats(28 /* R8G8B8A8_UNORM */, copyFormat, viewFormat),
              "DLSS depth copy formats cover the depth families and refuse colour");
        bool usedSceneBound = false;
        Check(dlss::ChooseDepthSource(true, true, usedSceneBound) && usedSceneBound &&
                  dlss::ChooseDepthSource(false, true, usedSceneBound) && !usedSceneBound &&
                  !dlss::ChooseDepthSource(false, false, usedSceneBound),
              "DLSS prefers the depth bound with the scene target, then the last full-raster depth");

        // Render plan: the headset picture is native x resolution_scale (the
        // launcher's even rounding, unchanged); DLSS shrinks only the render,
        // and only when its runtime library is present.
        const dlss::RenderPlan off = dlss::PlanRender(
            2912, 2100, 1.12f, dlss::kUpscalerOff, 1, true);
        Check(off.outputW == 3262 && off.outputH == 2352 && off.renderW == 3262 &&
                  off.renderH == 2352 && !off.dlss,
              "render plan without DLSS renders the headset picture itself (the logged 3262x2352)");
        const dlss::RenderPlan quality = dlss::PlanRender(
            2912, 2100, 1.12f, dlss::kUpscalerDlss,
            static_cast<int>(dlss::Quality::MaxQuality), true);
        // The picture stays native x scale with DLSS on (the scale must
        // apply live in every mode); 1.12 plans the same 3262x2352 picture.
        Check(quality.dlss && quality.outputW == 3262 && quality.outputH == 2352 &&
                  quality.renderW == 2175 && quality.renderH == 1568 &&
                  quality.mode == dlss::Quality::MaxQuality,
              "DLSS Quality renders NGX's own optimal size for the scaled picture");
        const dlss::RenderPlan ultra = dlss::PlanRender(
            2912, 2100, 1.12f, dlss::kUpscalerDlss,
            static_cast<int>(dlss::Quality::UltraPerformance), true);
        Check(ultra.renderW == 1087 && ultra.renderH == 784 &&
                  ultra.outputW == 3262 && ultra.outputH == 2352,
              "DLSS Ultra Performance renders the exact third NGX admits (1087, not an even 1088)");
        // The measured Ultra Performance range for a 2912x2100 picture is
        // min == max == optimal == 971x700; the old even bump asked 972x700.
        const dlss::RenderPlan ultraNative = dlss::PlanRender(
            2912, 2100, 1.0f, dlss::kUpscalerDlss,
            static_cast<int>(dlss::Quality::UltraPerformance), true);
        Check(ultraNative.renderW == 971 && ultraNative.renderH == 700,
              "Ultra Performance at scale 1.00 plans the 971x700 NGX admits");
        const dlss::RenderPlan under = dlss::PlanRender(
            2912, 2100, 0.75f, dlss::kUpscalerDlss,
            static_cast<int>(dlss::Quality::Dlaa), true);
        Check(under.outputW == 2184 && under.outputH == 1576,
              "a scale below 1.00 still shrinks the DLSS picture");
        const dlss::RenderPlan dlaa = dlss::PlanRender(
            2912, 2100, 1.0f, dlss::kUpscalerDlss,
            static_cast<int>(dlss::Quality::Dlaa), true);
        Check(dlaa.dlss && dlaa.renderW == 2912 && dlaa.renderH == 2100,
              "DLAA renders the full picture");
        const dlss::RenderPlan noRuntime = dlss::PlanRender(
            2912, 2100, 1.12f, dlss::kUpscalerDlss,
            static_cast<int>(dlss::Quality::MaxPerformance), false);
        Check(!noRuntime.dlss && noRuntime.renderW == 3262 && noRuntime.renderH == 2352,
              "DLSS selected without nvngx_dlss.dll never shrinks the render (AMD/Intel installs)");
        Check(dlss::PlanRender(0, 2100, 1.0f, 0, 0, true).outputW == 0 &&
                  dlss::PlanRender(2912, 2100, 0.0f, 0, 0, true).outputW == 0,
              "render plan rejects an empty native size or scale");
        Check(dlss::ModeRatio(dlss::Quality::Dlaa) == 1.0f &&
                  dlss::ModeRatio(dlss::Quality::MaxQuality) == 1.5f &&
                  std::fabs(dlss::ModeRatio(dlss::Quality::Balanced) - 1.0f / 0.58f) < 1e-6f &&
                  dlss::ModeRatio(dlss::Quality::MaxPerformance) == 2.0f &&
                  dlss::ModeRatio(dlss::Quality::UltraPerformance) == 3.0f &&
                  dlss::ModeFromConfig(9) == dlss::Quality::MaxQuality &&
                  dlss::ModeFromConfig(-1) == dlss::Quality::MaxQuality &&
                  dlss::ModeFromConfig(4) == dlss::Quality::UltraPerformance,
              "DLSS mode ratios follow NVIDIA's published scales; an out-of-range mode is Quality");
        {
            // Replay two exit cycles, including loading beginning after the
            // render thread queues a request but before the window consumes it.
            dlss::LiveResizeDispatch dispatch;
            const auto world = dlss::PlanRender(2912, 2100, 1.3f, dlss::kUpscalerDlss, 2, true);
            const auto full = dlss::PlanPresentation(world, RuntimeMode::Shell);
            dlss::RenderPlan active = world;
            for (int cycle = 0; cycle < 2; ++cycle)
            {
                Check(!dispatch.Queue(full, RuntimeMode::Loading) && !dispatch.Pending(),
                      "level unload cannot queue a render resize");
                Check(dispatch.Queue(full, RuntimeMode::Paused),
                      "pause retains full-size live resizing");
                Check(!dispatch.Queue(world, RuntimeMode::Gameplay),
                      "queued resize cannot be overwritten before window dispatch");
                Check(!dispatch.Begin(RuntimeMode::Loading, active) &&
                          active.renderW == world.renderW && !dispatch.Pending(),
                      "loading at UI dispatch cancels without publishing new dimensions");
                Check(dispatch.Queue(full, RuntimeMode::Shell) &&
                          dispatch.Begin(RuntimeMode::Shell, active) &&
                          active.renderW == full.outputW,
                      "return to shell admits the full-resolution canvas after unloading");
                Check(dispatch.Pending() && !dispatch.Begin(RuntimeMode::Shell, active),
                      "only one window message claims a queued resize");
                dispatch.Applied();
                Check(dispatch.Phase() == 1 && dispatch.Status() == 1,
                      "resize completion timeout starts only after dimension publication");
                dispatch.Complete();
                Check(dispatch.Queue(world, RuntimeMode::Gameplay) &&
                          dispatch.Begin(RuntimeMode::Gameplay, active) && active.dlss,
                      "next level re-admits the selected DLSS input size");
                dispatch.Applied();
                dispatch.Complete();
            }
            Check(dispatch.Queue(full, RuntimeMode::Paused), "queue a window-post failure case");
            dispatch.CancelQueued();
            Check(!dispatch.Pending() && !dispatch.Begin(RuntimeMode::Paused, active) &&
                      active.renderW == world.renderW,
                  "failed PostMessage leaves the active render unchanged and permits retry");
        }
        // Menus never inherit the DLSS input ratio. Exercise all modes and
        // resolution tiers, including the two dimensions from the rejected
        // shell run, and both runtime-present/missing paths.
        Check(!dlss::kEnableDlss21StereoBatch,
              "the rejected reduced-output stereo batch remains disabled");
        Check(dlss::kEnableDlssFullOutputResolve,
              "full-output presentation optimization is independent of stereo batching");
        Check(dlss::ResolveIntermediateCount(false, false, false, false) == 0 &&
                  dlss::ResolveIntermediateCount(false, false, true, false) == 1 &&
                  dlss::ResolveIntermediateCount(false, true, false, false) == 1 &&
                  dlss::ResolveIntermediateCount(false, true, true, false) == 2 &&
                  dlss::ResolveIntermediateCount(true, false, false, false) == 3 &&
                  dlss::ResolveIntermediateCount(true, false, true, false) == 3 &&
                  dlss::ResolveIntermediateCount(true, true, false, false) == 3 &&
                  dlss::ResolveIntermediateCount(true, true, true, false) == 3 &&
                  dlss::ResolveIntermediateCount(false, false, true, true) == 0,
              "resolve graph allocates only live intermediates for every AA/sharpen combination");
        const RuntimeMode flatModes[] = { RuntimeMode::Shell, RuntimeMode::Loading,
            RuntimeMode::Paused, RuntimeMode::Unsupported };
        const RuntimeMode worldModes[] = { RuntimeMode::Gameplay, RuntimeMode::Cutscene,
            RuntimeMode::Vehicle, RuntimeMode::Turret, RuntimeMode::Dead };
        // Regression sequence: changing quality in pause, then restarting or
        // switching titles, must leave the current raster AND output intact.
        // The queued settings may take effect when world rendering returns.
        if constexpr (false) // Rejected transition-resize hold; retained dormant.
        {
        const auto running = dlss::PlanRender(2912, 2100, 1.8f,
            dlss::kUpscalerDlss, dlss::kModeMax, true);
        const auto queued = dlss::PlanRender(2912, 2100, 1.3f,
            dlss::kUpscalerDlss, 1, true);
        auto retained = running;
        for (RuntimeMode transition : {RuntimeMode::Paused, RuntimeMode::Loading,
                                      RuntimeMode::Unsupported, RuntimeMode::Loading})
        {
            if (dlss::CanChangeRenderPlan(transition))
                retained = dlss::PlanPresentation(queued, transition);
            Check(retained.renderW == running.renderW && retained.renderH == running.renderH &&
                      retained.outputW == running.outputW && retained.outputH == running.outputH &&
                      retained.dlss == running.dlss,
                  "pause/restart/title ambiguity preserve the complete live render plan");
        }
        for (RuntimeMode inWorld : worldModes)
        {
            Check(dlss::CanChangeRenderPlan(inWorld),
                  "DLSS settings can apply after every supported world presentation resumes");
        }
        Check(dlss::CanChangeRenderPlan(RuntimeMode::Shell) &&
                  dlss::PlanPresentation(queued, RuntimeMode::Shell).renderW == queued.outputW,
              "returning to the main menu retains the full-resolution shell repair");
        }
        Check(dlss::CanChangeRenderPlan(RuntimeMode::Paused) &&
                  dlss::CanChangeRenderPlan(RuntimeMode::Loading),
              "rejected resize hold must not restrict pause/loading canvas or live settings");
        const float scales[] = { 0.35f, 0.5f, 0.75f, 1.0f, 1.3f, 1.8f, 2.64f, 2.75f };
        const float modeScales[] = { 1.f, 1.f / 1.5f, 0.58f, 0.5f, 1.f / 3.f };
        for (float scale : scales)
        for (int mode = 0; mode <= dlss::kModeMax; ++mode)
        for (bool present : {false, true})
        {
            const auto world = dlss::PlanRender(2912, 2100, scale,
                dlss::kUpscalerDlss, mode, present);
            const auto stock = dlss::PlanRender(2912, 2100, scale,
                dlss::kUpscalerOff, mode, present);
            Check(world.outputW == stock.outputW && world.outputH == stock.outputH,
                  "every DLSS mode preserves the DLSS-off output dimensions");
            Check(world.renderW == (present ? dlss::ScaleRender(stock.outputW, modeScales[mode]) : stock.renderW) &&
                      world.renderH == (present ? dlss::ScaleRender(stock.outputH, modeScales[mode]) : stock.renderH),
                  "every DLSS mode retains its standard per-axis input ratio");
            for (RuntimeMode flat : flatModes)
            {
                const auto canvas = dlss::PlanPresentation(world, flat);
                const auto offCanvas = dlss::PlanPresentation(stock, flat);
                Check(!canvas.dlss && canvas.renderW == stock.outputW &&
                          canvas.renderH == stock.outputH &&
                          canvas.renderW == offCanvas.renderW && canvas.renderH == offCanvas.renderH,
                      "DLSS on/off and mode changes do not resize the flat menu canvas");
            }
            for (RuntimeMode inWorld : worldModes)
            {
                const auto canvas = dlss::PlanPresentation(world, inWorld);
                Check(canvas.dlss == world.dlss && canvas.renderW == world.renderW &&
                          canvas.renderH == world.renderH && canvas.outputW == world.outputW &&
                          canvas.outputH == world.outputH,
                      "entering or resuming a world restores its exact normal DLSS plan");
            }
        }
        Check(dlss::PlanPresentation(ultraNative, RuntimeMode::Shell).renderW == 2912 &&
                  dlss::PlanPresentation(ultraNative, RuntimeMode::Shell).renderH == 2100 &&
                  dlss::PlanPresentation(under, RuntimeMode::Paused).renderW == 2184 &&
                  dlss::PlanPresentation(under, RuntimeMode::Paused).renderH == 1576,
              "reported native and low-scale menu canvases retain full configured dimensions");
        Check(dlss::PlanPresentation({}, RuntimeMode::Shell).renderW == 0,
              "presentation selection preserves invalid-plan guards");

        // Planned output: the picture, for a raster of its shape and no larger.
        const dlss::OutputSize planned = dlss::PlannedOutput(2176, 1568, 3262, 2352, 2528, 2704);
        Check(planned.width == 3262 && planned.height == 2352 && !planned.exact,
              "planned DLSS output is the headset picture for the shrunk raster");
        Check(dlss::PlannedOutput(3262, 2352, 3262, 2352, 3262, 2352).exact,
              "planned DLSS output is exact when the picture is the slice");
        Check(dlss::PlannedOutput(3786, 2730, 3262, 2352, 2528, 2704).width == 0 &&
                  dlss::PlannedOutput(1920, 1080, 3262, 2352, 2528, 2704).width == 0 &&
                  dlss::PlannedOutput(2176, 1568, 0, 0, 2528, 2704).width == 0,
              "planned DLSS output refuses a larger raster, another shape, or no plan");
        Check(std::strcmp(dlss::PresetName(dlss::kPresetTransformerL),
                          "L (transformer, 4.5 ultra-performance model)") == 0 &&
                  std::strcmp(dlss::PresetName(dlss::kPresetTransformerM),
                              "M (transformer, 4.5 performance model)") == 0 &&
                  std::strcmp(dlss::PresetName(99), "?") == 0,
              "DLSS 4.5 presets L and M are named");

    }

    {
        struct EyeResult { const int* texture = nullptr; unsigned width = 0; };
        dlss::FrameResult<EyeResult> pending[2];
        int texture[2] = { 11, 22 };
        EyeResult result{};
        pending[0].Publish(100, {&texture[0], 1456});
        pending[1].Publish(100, {&texture[1], 1456});
        Check(pending[0].Take(100, result) && result.texture == &texture[0],
              "a DLSS result belongs to its source eye and producing frame");
        Check(!pending[0].Take(100, result) && !result.texture,
              "a completed DLSS eye is consumed only once");
        Check(!pending[1].Take(101, result) && !result.texture &&
                  !pending[1].Ready(100),
              "a skipped Present cannot reuse last frame's DLSS result");

        // Pause/resize between eye production and consumption: revoke before
        // storage is replaced, even if the address and frame number repeat.
        pending[0].Publish(102, {&texture[0], 1456});
        pending[0].Reset();
        texture[0] = 33;
        Check(!pending[0].Take(102, result) && !result.texture,
              "resource retirement revokes the old DLSS pointer before address reuse");
        pending[0].Publish(102, {&texture[0], 2912});
        Check(pending[0].Take(102, result) && result.width == 2912 &&
                  *result.texture == 33,
              "a replacement resource can publish a fresh result after resize");

        pending[0].Publish(103, {&texture[0], 2912});
        pending[1].Publish(103, {&texture[1], 2912});
        for (auto& eye : pending) eye.Reset();
        Check(!pending[0].Ready(103) && !pending[1].Ready(103),
              "pause/loading/aborted frame cleanup retires both unconsumed eyes");
        pending[0].Publish(0, {&texture[0], 2912});
        Check(!pending[0].Take(0, result) && !result.texture,
              "no prepared frame cannot publish a consumable DLSS result");
    }


    for (int mask = 0; mask < 256; ++mask) {
        const bool v[8] = {bool(mask&1),bool(mask&2),bool(mask&4),bool(mask&8),
            bool(mask&16),bool(mask&32),bool(mask&64),bool(mask&128)};
        for (int eye = -1; eye <= 2; ++eye)
            Check(dlss::ReachAlternateDepthBindEligible(v[0],v[1],eye,v[2],v[3],v[4],v[5],v[6],v[7])
                == (mask == 255 && eye >= 0 && eye < 2), "alternate depth ownership conjunction");
    }
    Check(dlss::TitleHasCameraContract(GameTitle::HaloCE), "CE owns depth and camera through its eye-pair lease");
    Check(!dlss::TitleHasCameraContract(GameTitle::None) &&
        !dlss::TitleHasCameraContract(GameTitle::Unknown), "unsupported title retains native resolve");
    for (auto title : {GameTitle::Halo2, GameTitle::Halo3, GameTitle::Halo3ODST, GameTitle::HaloReach, GameTitle::Halo4})
        Check(dlss::TitleHasCameraContract(title), "title camera publication supported");
    std::cout << "DLSS failures: " << g_failures << "\n";
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
