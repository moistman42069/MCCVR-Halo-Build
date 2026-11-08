// Halo 3 glass fade (src/common/halo3_glass_fade.h): which parameters fading touches per blend mode -- read against
// the stock glass shaders (the Sparrowhawk's, Hornet's and Longsword's additive glass, the Sparrowhawk's multiply
// glass_dark) -- and what it does to each, from all of it kept to none.
#include "halo3_glass_fade.h"

#include <cmath>
#include <cstdio>

namespace
{
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
bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-5f;
}

using namespace halo3_glass_fade;

void TestOps()
{
    // sparrowhawk_glass (additive): real[2] albedo_color, [4] diffuse_coefficient, [5] specular_coefficient,
    // [10] analytical_specular_contribution, [12] env_tint_color fade; the maps' transforms and fresnel do not.
    const char* sparrowhawk[] = {"base_map", "detail_map", "albedo_color", "bump_map", "diffuse_coefficient",
                                 "specular_coefficient", "fresnel_coefficient", "fresnel_curve_steepness",
                                 "fresnel_curve_bias", "roughness", "analytical_specular_contribution",
                                 "area_specular_contribution", "env_tint_color"};
    int scaled = 0;
    for (const char* name : sparrowhawk)
        scaled += OpFor(1, name) == Op::Scale ? 1 : 0;
    Check(scaled == 6, "additive glass: its six light terms fade");
    Check(OpFor(1, "base_map") == Op::None && OpFor(1, "fresnel_coefficient") == Op::None &&
              OpFor(1, "roughness") == Op::None,
          "additive glass: texture transforms and the fresnel shape do not");
    // longsword glass (additive) has no albedo_color: its diffuse_coefficient carries the texture's light.
    Check(OpFor(1, "diffuse_coefficient") == Op::Scale, "a glass without albedo_color still fades through diffuse");
    Check(OpFor(2, "albedo_color") == Op::TowardWhite && OpFor(2, "diffuse_coefficient") == Op::None,
          "multiply glass: only its colour, toward white");
    Check(OpFor(4, "albedo_color") == Op::TowardGray, "double multiply: toward mid grey");
    Check(OpFor(3, "albedo_color") == Op::ScaleAlpha, "alpha blend: its alpha");
    Check(OpFor(5, "albedo_color") == Op::Scale, "premultiplied alpha: colour and alpha together");
    Check(OpFor(0, "albedo_color") == Op::None && OpFor(0, "diffuse_coefficient") == Op::None,
          "opaque glass (the Banshee's) cannot fade");
    Check(OpFor(9, "albedo_color") == Op::None, "an unknown blend mode is left alone");
}

void TestApply()
{
    float tint[4] = {0.5333f, 0.3882f, 0.1137f, 1.0f};
    Apply(Op::Scale, 0.25f, tint);
    Check(Near(tint[0], 0.133325f) && Near(tint[3], 0.25f), "scale: every component times the amount kept");
    float dark[4] = {0.2f, 0.4f, 0.6f, 0.8f};
    Apply(Op::TowardWhite, 0.5f, dark);
    Check(Near(dark[0], 0.6f) && Near(dark[2], 0.8f) && Near(dark[3], 0.8f), "toward white: colour only, halfway");
    float gray[4] = {0.0f, 1.0f, 0.5f, 1.0f};
    Apply(Op::TowardGray, 0.0f, gray);
    Check(Near(gray[0], 0.5f) && Near(gray[1], 0.5f) && Near(gray[2], 0.5f), "toward grey: none kept = no change");
    float alpha[4] = {0.3f, 0.3f, 0.3f, 0.8f};
    Apply(Op::ScaleAlpha, 0.5f, alpha);
    Check(Near(alpha[0], 0.3f) && Near(alpha[3], 0.4f), "scale alpha: only w");
    float keep[4] = {0.3f, 0.4f, 0.5f, 0.6f};
    Apply(Op::Scale, 1.0f, keep);
    Check(Near(keep[0], 0.3f) && Near(keep[3], 0.6f), "all of it kept: as the game draws it");
    float clamp[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    Apply(Op::Scale, 3.0f, clamp);
    Check(Near(clamp[0], 1.0f), "an amount above 1 is 1");
    Apply(Op::Scale, -1.0f, clamp);
    Check(Near(clamp[0], 0.0f), "below 0 is 0");
    float none[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    Apply(Op::None, 0.0f, none);
    Check(Near(none[1], 0.2f), "no op leaves the parameter");
}
} // namespace

int main()
{
    TestOps();
    TestApply();
    std::printf("halo3_glass_fade_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
