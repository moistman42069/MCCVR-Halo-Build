// Mono rendering's pure decisions (src/common/mono_view_logic.h): the cover of both eyes, when a headset's
// eyes are parallel enough, when mono is refused, and where a world-registered layer goes so that it has no
// disparity over a picture that has none.
#include "../src/common/mono_view_logic.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace
{
int failures = 0;
void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
constexpr float kDeg = 3.14159265f / 180.0f;
// Degrees between two directions, from the cross product as well as the dot: an arc cosine alone cannot tell a
// hundredth of a degree from none.
float Angle(const float a[3], const float b[3])
{
    const double cross[3]{double(a[1]) * b[2] - double(a[2]) * b[1], double(a[2]) * b[0] - double(a[0]) * b[2],
                          double(a[0]) * b[1] - double(a[1]) * b[0]};
    const double sine = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
    const double cosine = double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2];
    return static_cast<float>(std::atan2(sine, cosine) / kDeg);
}
} // namespace

int main()
{
    using namespace mono_view;

    // The cover: a headset whose eyes see 54 degrees outward and 40 inward, 44 up and 55 down.
    {
        const Fov left{-54.0f * kDeg, 40.0f * kDeg, 44.0f * kDeg, -55.0f * kDeg};
        const Fov right{-40.0f * kDeg, 54.0f * kDeg, 44.0f * kDeg, -55.0f * kDeg};
        float tanX = 0.0f, tanY = 0.0f;
        Check(MonoCover(left, right, tanX, tanY), "a usable pair of eyes has a cover");
        Check(std::fabs(tanX - std::tan(54.0f * kDeg)) < 1.0e-5f, "across, the cover is the outward angle");
        Check(std::fabs(tanY - std::tan(55.0f * kDeg)) < 1.0e-5f, "up and down, the larger of the two");
        // Each eye's own frustum lies inside it: that is what lets the submit path crop it out.
        for (const Fov& eye : {left, right})
            Check(std::tan(-eye.left) <= tanX + 1.0e-6f && std::tan(eye.right) <= tanX + 1.0e-6f &&
                      std::tan(eye.up) <= tanY + 1.0e-6f && std::tan(-eye.down) <= tanY + 1.0e-6f,
                  "an eye's frustum is inside the cover");
        // Eyes that differ up and down too.
        const Fov odd{-50.0f * kDeg, 45.0f * kDeg, 60.0f * kDeg, -40.0f * kDeg};
        Check(MonoCover(left, odd, tanX, tanY) && std::fabs(tanY - std::tan(60.0f * kDeg)) < 1.0e-5f,
              "the cover takes the larger angle from either eye");
        Fov bad = left;
        bad.right = -0.1f;
        Check(!MonoCover(bad, right, tanX, tanY), "an eye that sees nothing to its right is refused");
        bad = left;
        bad.up = std::nanf("");
        Check(!MonoCover(left, bad, tanX, tanY), "a non-finite angle is refused");
        bad = left;
        bad.left = -1.6f;
        Check(!MonoCover(bad, right, tanX, tanY), "an angle past a right angle is refused");
    }

    // Canted displays.
    {
        const float straight[4]{0.0f, 0.0f, 0.0f, 1.0f};
        const float negated[4]{0.0f, 0.0f, 0.0f, -1.0f};
        Check(EyeCantDeg(straight, straight) < 1.0e-3f, "the same orientation is no cant");
        Check(EyeCantDeg(straight, negated) < 1.0e-3f, "q and -q are the same orientation");
        const float half = 2.5f * kDeg;   // 5 degrees about the up axis
        const float turned[4]{0.0f, std::sin(half), 0.0f, std::cos(half)};
        Check(std::fabs(EyeCantDeg(straight, turned) - 5.0f) < 0.01f, "5 degrees outward reads 5 degrees");
        const float nothing[4]{};
        Check(EyeCantDeg(straight, nothing) > 90.0f, "no orientation is not parallel");
    }

    // When the frame is drawn mono.
    {
        MonoInput in;
        Check(DecideMono(in) == MonoRefusal::Off, "not asked for: off");
        in.asked = true;
        Check(DecideMono(in) == MonoRefusal::Title, "asked for in a title without it");
        in.titleSupported = true;
        Check(DecideMono(in) == MonoRefusal::NoViews, "before the eyes are located");
        in.viewsLocated = true;
        Check(DecideMono(in) == MonoRefusal::Canted, "an unknown cant is a cant");
        in.eyeCantDeg = 4.0f;
        Check(DecideMono(in) == MonoRefusal::Canted, "canted displays keep stereo");
        in.eyeCantDeg = 0.1f;
        Check(DecideMono(in) == MonoRefusal::None, "parallel eyes: mono");
        in.theatre = true;
        Check(DecideMono(in) == MonoRefusal::Theatre, "the cutscene theatre is stereo");
        in.theatre = false;
        in.upscaler = true;
        Check(DecideMono(in) == MonoRefusal::Upscaler, "an upscaler per eye keeps stereo");
        in.upscaler = false;
        in.rejected = true;
        Check(DecideMono(in) == MonoRefusal::Rejected, "a title that turned single passes down keeps stereo");
        in.rejected = false;
        in.eyeCantDeg = std::nanf("");
        Check(DecideMono(in) == MonoRefusal::Canted, "a non-finite cant is refused");
        for (int r = 0; r < static_cast<int>(MonoRefusal::Count); ++r)
            Check(MonoRefusalName(static_cast<MonoRefusal>(r))[0] != '?', "every refusal has a name");
    }

    // A title whose renderer turns single passes down.
    {
        using Frame = MonoRejections::Frame;
        // One frame of the title: what it is asked to draw, and whether the renderer drew it.
        const auto frame = [](MonoRejections& r, bool monoDraws, bool stereoDraws, bool& refusedNow) {
            const Frame asked = r.Next();
            if (asked == Frame::Mono)
            {
                if (monoDraws) r.MonoRendered(); else r.MonoRejected();
            }
            else if (stereoDraws)
            {
                refusedNow = r.StereoRendered() || refusedNow;
            }
            return asked;
        };

        // A title that draws every pair and no single pass.
        MonoRejections broken;
        bool refusedNow = false;
        unsigned monoAsked = 0, probes = 0, frames = 0;
        while (!broken.refused && frames < 1000)
        {
            const Frame asked = frame(broken, false, true, refusedNow);
            monoAsked += asked == Frame::Mono;
            probes += asked == Frame::Probe;
            ++frames;
        }
        Check(broken.refused && refusedNow, "a title that draws pairs and no single pass is refused");
        Check(monoAsked == kMonoDropsBeforeProbe * kMonoBlamesToRefuse && probes == kMonoBlamesToRefuse,
              "and it costs that many dropped frames, with a pair drawn after each run of them");
        Check(broken.Next() == Frame::Refused, "once refused it stays refused");
        broken.MonoRendered();
        Check(broken.refused, "and nothing but a reset lifts it");
        broken.Reset();
        Check(!broken.refused && broken.Next() == Frame::Mono, "the setting switched off and on gives a new try");

        // A title that is dropping every frame for a while, pairs too: mono is not to blame.
        MonoRejections stalled;
        refusedNow = false;
        for (int i = 0; i < 400; ++i)
            frame(stalled, false, false, refusedNow);
        Check(!stalled.refused && !refusedNow && stalled.blames == 0, "frames dropped as pairs too are not blamed on mono");
        frame(stalled, false, true, refusedNow);   // the stall ends on a probe frame
        Check(!stalled.refused && stalled.blames == 0, "a probe that went missing first blames nothing when one is drawn");
        Check(frame(stalled, true, true, refusedNow) == Frame::Mono && stalled.dropRun == 0, "and mono is tried again");

        // Mono that draws most of the time: a few runs of drops do not add up to "cannot draw mono" ...
        MonoRejections flaky;
        refusedNow = false;
        unsigned rounds = 0;
        const auto round = [&] {
            for (unsigned i = 0; i < kMonoDropsBeforeProbe; ++i)
                frame(flaky, false, true, refusedNow);
            frame(flaky, false, true, refusedNow);   // the probe: drawn, one blame
            frame(flaky, true, true, refusedNow);    // then a mono frame is drawn: the run of blames is over
            ++rounds;
        };
        while (rounds + 1 < kMonoBlamesInAllToRefuse)
            round();
        Check(!flaky.refused && !refusedNow && flaky.blames == 0,
              "a mono frame drawn between the blames ends their run");
        // ... but a title that goes on dropping single passes in runs is left in stereo in the end.
        round();
        Check(flaky.refused && refusedNow, "runs of dropped single passes that keep coming are refused in all");

        // A pair that is not a probe (the setting off, the theatre) changes nothing.
        MonoRejections idle;
        Check(!idle.StereoRendered() && !idle.probing && idle.blames == 0, "an ordinary pair is not a verdict");
        idle.MonoRejected();
        Check(!idle.StereoRendered() && idle.dropRun == 1, "nor is a pair before any probe was asked for");
    }

    // A crosshair 3 m away, a little right and up of the midpoint of the eyes; eyes 64 mm apart.
    {
        const float centre[3]{0.2f, 1.6f, -0.1f};
        const float mark[3]{centre[0] + 0.3f, centre[1] + 0.2f, centre[2] - 3.0f};
        float position[3]{mark[0], mark[1], mark[2]};
        float size[2]{0.21f, 0.21f};
        const float toMark[3]{mark[0] - centre[0], mark[1] - centre[1], mark[2] - centre[2]};
        const float before = std::sqrt(toMark[0] * toMark[0] + toMark[1] * toMark[1] + toMark[2] * toMark[2]);
        Check(FlattenQuad(centre, position, size), "a near crosshair is moved");
        const float moved[3]{position[0] - centre[0], position[1] - centre[1], position[2] - centre[2]};
        const float after = std::sqrt(moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]);
        Check(std::fabs(after - kMonoFlatDistanceM) < 1.0e-2f, "to the far distance");
        Check(Angle(toMark, moved) < 1.0e-3f, "along its own ray: the same direction from the midpoint");
        Check(std::fabs(size[0] / after - 0.21f / before) < 1.0e-6f, "its angle is kept");
        // The point of it: what each eye sees. Before, the two eyes disagree by over a degree about where the
        // crosshair is; after, by under a twentieth.
        const float leftEye[3]{centre[0] - 0.032f, centre[1], centre[2]};
        const float rightEye[3]{centre[0] + 0.032f, centre[1], centre[2]};
        const auto disparity = [&](const float p[3]) {
            const float l[3]{p[0] - leftEye[0], p[1] - leftEye[1], p[2] - leftEye[2]};
            const float r[3]{p[0] - rightEye[0], p[1] - rightEye[1], p[2] - rightEye[2]};
            return Angle(l, r);
        };
        Check(disparity(mark) > 1.0f, "at 3 m the eyes disagree by more than a degree");
        Check(disparity(position) < 0.05f, "at the far distance by under a twentieth of one");
        // Already far, on the midpoint, or not finite: left alone.
        float farAway[3]{centre[0], centre[1], centre[2] - 150.0f};
        float farSize[2]{1.0f, 1.0f};
        Check(!FlattenQuad(centre, farAway, farSize) && farAway[2] == centre[2] - 150.0f && farSize[0] == 1.0f,
              "a layer already past the far distance is left where it is");
        float onCentre[3]{centre[0], centre[1], centre[2]};
        Check(!FlattenQuad(centre, onCentre, farSize), "a layer on the midpoint has no ray to slide along");
        float broken[3]{std::nanf(""), 0.0f, 0.0f};
        Check(!FlattenQuad(centre, broken, farSize), "a non-finite position is left alone");
    }

    if (failures)
        std::printf("%d check(s) failed\n", failures);
    else
        std::printf("mono view: all checks passed\n");
    return failures ? 1 : 0;
}
