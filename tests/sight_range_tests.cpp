// The range the sight layer's aim converges on (SightSmoothInverseRange in src/dll/sight_layer.h): the inverse
// range averaged over 120 ms, and a miss counted as the trace's far end, as Reach's aim does.
#include "../src/dll/sight_layer.h"
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
float RangeM(float invPerWu) { return invPerWu > 0.0f ? 1.0f / invPerWu / kSightWorldUnitsPerMetre : 0.0f; }
constexpr float kStep = 0.03f;   // one trace every ~30 ms
} // namespace

int main()
{
    const float wall = 1.6f * kSightWorldUnitsPerMetre;
    const float far = 40.0f * kSightWorldUnitsPerMetre;

    float inv = SightSmoothInverseRange(0.0f, false, true, wall, kStep);
    Check(std::fabs(RangeM(inv) - 1.6f) < 0.001f, "the first hit is taken as it is");

    // A sustained miss (sky): the aim must head for the far end, not stay on the wall.
    for (int i = 0; i < 100; ++i)   // 3 s of misses
        inv = SightSmoothInverseRange(inv, true, false, 0.0f, kStep);
    Check(RangeM(inv) > 500.0f, "three seconds of misses converge far, not on the last wall");

    // One miss after a near hit does not snap the aim away from it.
    inv = SightSmoothInverseRange(0.0f, false, true, wall, kStep);
    inv = SightSmoothInverseRange(inv, true, false, 0.0f, kStep);
    Check(RangeM(inv) > 1.6f && RangeM(inv) < 3.0f, "a single miss moves the range only part of the way");

    // A miss with nothing before it converges on the far end at once (no range = cannot converge is gone).
    inv = SightSmoothInverseRange(0.0f, false, false, 0.0f, kStep);
    Check(std::fabs(RangeM(inv) - 1000.0f) < 1.0f, "a first miss converges on the 1 km far end");

    // Far to near is fast in the inverse (half-way within ~one time constant), near to far is smooth.
    inv = SightSmoothInverseRange(0.0f, false, true, far, kStep);
    for (int i = 0; i < 4; ++i)   // 120 ms of a 1.6 m target
        inv = SightSmoothInverseRange(inv, true, true, wall, kStep);
    Check(RangeM(inv) < 4.0f, "far to near: within ~one time constant the aim is most of the way in");

    // A gap or a new generation starts afresh from the next sample.
    inv = SightSmoothInverseRange(1.0f / wall, false, true, far, kStep);
    Check(std::fabs(RangeM(inv) - 40.0f) < 0.01f, "a discontinuity starts from the new sample");

    // Garbage in a hit is treated as the far end, never as a range.
    inv = SightSmoothInverseRange(0.0f, false, true, std::nanf(""), kStep);
    Check(std::fabs(RangeM(inv) - 1000.0f) < 1.0f, "a non-finite hit range counts as the far end");
    inv = SightSmoothInverseRange(0.0f, false, true, -2.0f, kStep);
    Check(std::fabs(RangeM(inv) - 1000.0f) < 1.0f, "a negative hit range counts as the far end");

    std::printf("Sight range: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
