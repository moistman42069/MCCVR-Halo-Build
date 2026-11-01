// The F8 / F9 pitch trim (src/common/pitch_trim_logic.h): it applies with stereo off and is zero in stereo VR,
// and a key press in stereo changes nothing.
#include "pitch_trim_logic.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(float a, float b) { return std::fabs(a - b) <= 1.0e-6f; }
} // namespace

int main()
{
    using namespace pitch_trim;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // The incident: two F9 presses banked 4 degrees. In stereo the cameras must still see none of it.
    {
        bool ignored = true;
        float stored = Step(0.0f, +1, false, ignored);
        Check(!ignored && Near(stored, kStepRad), "a press with stereo off moves the trim one step");
        stored = Step(stored, +1, false, ignored);
        Check(Near(stored, 2.0f * kStepRad), "a second press adds a second step");
        Check(Near(Effective(stored, false), 2.0f * kStepRad), "with stereo off the stored trim applies");
        Check(Effective(stored, true) == 0.0f, "in stereo the trim is zero whatever is stored");
    }

    // A press in stereo is ignored and banks nothing for later.
    {
        bool ignored = false;
        const float stored = Step(0.0f, +1, true, ignored);
        Check(ignored, "a press in stereo reports that it was ignored");
        Check(stored == 0.0f, "a press in stereo leaves a zero trim at zero");
        Check(Effective(stored, false) == 0.0f, "so nothing appears when stereo later goes off");
        bool ignoredAgain = false;
        const float kept = Step(3.0f * kStepRad, -1, true, ignoredAgain);
        Check(ignoredAgain && Near(kept, 3.0f * kStepRad), "a press in stereo leaves a deliberate flat-screen trim alone");
        Check(Effective(kept, true) == 0.0f, "and that trim is still not applied in stereo");
        Check(Near(Effective(kept, false), 3.0f * kStepRad), "but it is back when stereo goes off");
    }

    // Both directions, no direction, and the limit.
    {
        bool ignored = true;
        Check(Near(Step(0.0f, -1, false, ignored), -kStepRad) && !ignored, "F8 steps down");
        Check(Near(Step(0.0f, +5, false, ignored), kStepRad), "any positive direction is one step up");
        Check(Near(Step(0.0f, -5, false, ignored), -kStepRad), "any negative direction is one step down");
        Check(Step(kStepRad, 0, false, ignored) == kStepRad, "no direction changes nothing");
        float stored = 0.0f;
        for (int i = 0; i < 100; ++i)
            stored = Step(stored, +1, false, ignored);
        Check(Near(stored, kLimitRad), "the trim stops at the upper limit");
        for (int i = 0; i < 200; ++i)
            stored = Step(stored, -1, false, ignored);
        Check(Near(stored, -kLimitRad), "and at the lower limit");
        Check(Near(Step(5.0f, -1, false, ignored), kLimitRad), "a stored value beyond the limit comes back inside it");
    }

    // A stored value that is not a number never reaches a camera, and a press recovers from it.
    {
        bool ignored = true;
        Check(Effective(nan, false) == 0.0f, "a NaN trim applies as zero");
        Check(Effective(inf, false) == 0.0f, "an infinite trim applies as zero");
        Check(Effective(nan, true) == 0.0f, "a NaN trim is zero in stereo too");
        Check(Near(Step(nan, +1, false, ignored), kStepRad), "a press on a NaN trim starts again from zero");
        Check(Near(Step(-inf, -1, false, ignored), -kStepRad), "a press on an infinite trim starts again from zero");
    }

    // The step is the documented size and the limit is a whole number of steps short of a quarter turn.
    Check(kStepRad * 57.29578f > 1.9f && kStepRad * 57.29578f < 2.1f, "one press is about two degrees");
    Check(kLimitRad < 1.5f, "the limit stays inside every camera path's own pitch clamp");

    std::printf("pitch trim: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
