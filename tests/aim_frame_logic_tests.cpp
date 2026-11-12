// Aim Frame Zoom in the titles that derive their picture's aspect (src/common/aim_frame_logic.h): the width a squarer
// picture is shown at keeps the flat frame's height, a wide picture keeps the setting; what the engine built agrees
// with what was asked within the tolerance; an eye stands along the aim's basis; Halo 4's observer field of view maps
// to tangents and back; the HUD's scale in the frame is the game's own by default; and a run of refused frames gives the zoom back once, cleared by a frame that goes through.
#include "aim_frame_logic.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using namespace aim_frame;

unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

constexpr float kFlat = 16.0f / 9.0f;
const float kNaN = std::numeric_limits<float>::quiet_NaN();

void ShownWidth()
{
    // A picture of the flat aspect, or wider, is shown at the setting.
    Check(ShownWidthDeg(100.0f, kFlat, 1.0f, kFlat) == 100.0f, "the flat aspect is shown at the setting");
    Check(ShownWidthDeg(100.0f, 3.0f, 1.0f, kFlat) == 100.0f, "a wider picture is shown at the setting");

    // A square picture is as tall as the flat frame would be: tan(height / 2) = tan(width / 2) / aspect.
    const float square = ShownWidthDeg(100.0f, 0.5f, 0.5f, kFlat);
    const float flatHeight = 2.0f * std::atan(std::tan(50.0f * kDegToRad) / kFlat) * kRadToDeg;
    Check(Near(square, flatHeight, 0.01f), "a square picture keeps the flat frame's height");
    Check(square < 100.0f, "a square picture is shown narrower than the setting");

    // The height is kept for any shape between.
    const float aspect = 1.2f;
    const float shown = ShownWidthDeg(70.0f, aspect * 0.3f, 0.3f, kFlat);
    const float shownHeightTan = std::tan(shown * 0.5f * kDegToRad) / aspect;
    Check(Near(shownHeightTan, std::tan(35.0f * kDegToRad) / kFlat, 1.0e-4f), "the frame's height follows the setting");

    // Nothing usable to go on: the setting, as it is.
    Check(ShownWidthDeg(100.0f, 0.0f, 0.5f, kFlat) == 100.0f, "no horizontal tangent");
    Check(ShownWidthDeg(100.0f, 0.5f, -1.0f, kFlat) == 100.0f, "a negative vertical tangent");
    Check(ShownWidthDeg(100.0f, kNaN, 0.5f, kFlat) == 100.0f, "a NaN tangent");
    Check(ShownWidthDeg(100.0f, 0.5f, 0.5f, 0.0f) == 100.0f, "no flat aspect");
}

void Readback()
{
    Check(TangentsAgree(0.5f, 0.5f), "the same");
    Check(TangentsAgree(0.5f, 0.504f), "within one percent");
    Check(!TangentsAgree(0.5f, 0.51f), "beyond one percent");
    Check(TangentsAgree(0.5f, 0.51f, 0.05f), "the tolerance is the caller's");
    Check(!TangentsAgree(0.0f, 0.5f), "nothing asked for");
    Check(!TangentsAgree(0.5f, 0.0f), "nothing built");
    Check(!TangentsAgree(kNaN, 0.5f), "a NaN asked for");
    Check(!TangentsAgree(0.5f, kNaN), "a NaN built");
}

void EyeAlongTheAim()
{
    const float centre[3]{10.0f, 20.0f, 30.0f};
    const float right[3]{0.0f, -1.0f, 0.0f};
    const float up[3]{0.0f, 0.0f, 1.0f};
    const float forward[3]{1.0f, 0.0f, 0.0f};
    float out[3]{};
    const float none[3]{};
    EyePosition(centre, right, up, forward, none, 2.0f, out);
    Check(out[0] == 10.0f && out[1] == 20.0f && out[2] == 30.0f, "no offset is the centre");
    const float rightEye[3]{0.03f, 0.0f, 0.0f};
    EyePosition(centre, right, up, forward, rightEye, 2.0f, out);
    Check(Near(out[1], 19.94f, 1.0e-4f) && out[0] == 10.0f && out[2] == 30.0f, "+X lies along the aim's right, scaled");
    const float raised[3]{0.0f, 0.1f, 0.0f};
    EyePosition(centre, right, up, forward, raised, 1.0f, out);
    Check(Near(out[2], 30.1f, 1.0e-4f), "+Y lies along the aim's up");
    const float behind[3]{0.0f, 0.0f, 0.5f};
    EyePosition(centre, right, up, forward, behind, 1.0f, out);
    Check(Near(out[0], 9.5f, 1.0e-4f), "+Z is behind the aim");
}

void Halo4Mapping()
{
    // The proven retail gain (halo4_render_logic.h): a written 1.8295 builds a half angle of 0.71805.
    constexpr float kGain = 0.3925f;
    float tanX = 0.0f, tanY = 0.0f;
    Check(Halo4NativeTangents(1.8295f, kGain, 1.1f, tanX, tanY), "the stock field of view reads");
    Check(Near(tanY, std::tan(0.71808f), 1.0e-4f), "tanY follows the gain");
    Check(Near(tanX, tanY * 1.1f, 1.0e-5f), "tanX follows the ratio");

    float observer = 0.0f;
    Check(Halo4ObserverFovFor(tanY, kGain, observer), "and back");
    Check(Near(observer, 1.8295f, 1.0e-3f), "the write that asks for it");

    Check(!Halo4NativeTangents(0.0f, kGain, 1.1f, tanX, tanY), "no field of view");
    Check(!Halo4NativeTangents(10.0f, kGain, 1.1f, tanX, tanY), "a half angle past the proven range");
    Check(!Halo4NativeTangents(1.0f, kGain, 0.0f, tanX, tanY), "no ratio learned");
    Check(!Halo4NativeTangents(kNaN, kGain, 1.1f, tanX, tanY), "a NaN field of view");
    Check(!Halo4ObserverFovFor(0.0f, kGain, observer), "no tangent asked for");
    Check(!Halo4ObserverFovFor(0.5f, 0.0f, observer), "no gain learned");
    Check(!Halo4ObserverFovFor(kNaN, kGain, observer), "a NaN tangent");
}

void HudInTheFrame()
{
    float h = 0.0f, v = 0.0f;
    HudScale(1.0f, 1.0f, 1.0f, h, v);
    Check(h == 1.0f && v == 1.0f, "the defaults are the game's own HUD layout");
    HudScale(0.8f, 1.0f, 1.0f, h, v);
    Check(Near(h, 0.8f, 1e-6f) && Near(v, 0.8f, 1e-6f), "size scales both axes");
    HudScale(1.0f, 1.2f, 0.9f, h, v);
    Check(Near(h, 1.2f, 1e-6f) && Near(v, 0.9f, 1e-6f), "width and height scale one axis each");
    HudScale(1.1f, 1.2f, 0.9f, h, v);
    Check(Near(h, 1.32f, 1e-5f) && Near(v, 0.99f, 1e-5f), "size and stretch multiply");
    HudScale(9.0f, 9.0f, 0.0f, h, v);
    Check(Near(h, kHudSizeMax * kHudStretchMax, 1e-5f) && Near(v, kHudSizeMax * kHudStretchMin, 1e-5f),
          "settings out of range are held to it");
    HudScale(kNaN, kNaN, kNaN, h, v);
    Check(h == 1.0f && v == 1.0f, "a setting that is not a number is the game's own");
}

void Refusals()
{
    RefusalRun run;
    int given = 0;
    for (int i = 0; i < RefusalRun::kMost - 1; ++i)
        given += run.Note(true) ? 1 : 0;
    Check(given == 0, "short of the limit the frame keeps its zoom");
    Check(!run.Note(false) && run.run == 0, "a frame that goes through clears the run");
    for (int i = 0; i < RefusalRun::kMost; ++i)
        given += run.Note(true) ? 1 : 0;
    Check(given == 1, "the limit gives the zoom back");
    for (int i = 0; i < 5; ++i)
        given += run.Note(true) ? 1 : 0;
    Check(given == 1, "and says so once");
}
}

int main()
{
    ShownWidth();
    Readback();
    EyeAlongTheAim();
    Halo4Mapping();
    HudInTheFrame();
    Refusals();
    std::printf("aim_frame_logic_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
