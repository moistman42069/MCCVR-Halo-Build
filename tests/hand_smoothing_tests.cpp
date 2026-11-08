#include "../src/common/hand_smoothing.h"
#include "../src/common/config.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace hand_smoothing;

// config.cpp logs through this; the test has no log.
void Logf(const char*, ...) {}

static unsigned checks{};
static void Check(bool value, const char* label)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

namespace
{
constexpr int64_t kFrameNs = 11'111'111;   // 90 Hz
const float kIdentity[4]{0.0f, 0.0f, 0.0f, 1.0f};

void YawQuat(float radians, float* q)
{
    q[0] = 0.0f; q[1] = std::sin(radians * 0.5f); q[2] = 0.0f; q[3] = std::cos(radians * 0.5f);
}
float AngleBetween(const float* a, const float* b)
{
    float inverse[4], delta[4], turned[3];
    QuatConj(a, inverse);
    QuatMul(b, inverse, delta);
    QuatLog(delta, turned);
    return Length3(turned);
}
} // namespace

int main()
{
    // The defaults: Campaign Evolved's, with both steadiness cutoffs at 1.3 Hz (tuned in a headset); the same
    // in the header and in the config.
    {
        const Params params;
        Check(params.posMinHz == 1.3f && params.posBeta == 500.0f && params.rotMinHz == 1.3f &&
                  params.rotBeta == 5.0f && params.speedCutoffHz == 10.0f,
              "the filter's defaults");
        Check(kCampaignEvolvedPosMinHz == 1.0f && kCampaignEvolvedRotMinHz == 1.0f,
              "Campaign Evolved's own steadiness cutoffs are kept for the record");
        const Config config{};
        Check(config.hand_smoothing && config.hand_smoothing_hands == 0 &&
                  config.hand_smoothing_pos_min_hz == kDefaultPosMinHz &&
                  config.hand_smoothing_pos_beta == kDefaultPosBeta &&
                  config.hand_smoothing_rot_min_hz == kDefaultRotMinHz &&
                  config.hand_smoothing_rot_beta == kDefaultRotBeta &&
                  config.hand_smoothing_speed_cutoff_hz == kDefaultSpeedCutoffHz,
              "the config ships the same numbers, on, both hands");
    }
    // The first sample is the hand as tracked; so is one after a gap.
    {
        State s;
        const float p[3]{0.1f, 1.2f, -0.3f};
        float q[4];
        YawQuat(0.4f, q);
        Check(!Step(s, p, q, 1000, Params{}) && s.have && s.pos[0] == p[0] && s.pos[1] == p[1] && s.pos[2] == p[2] &&
                  s.rot[1] == q[1] && s.rot[3] == q[3],
              "the first sample starts the filter on the raw pose");
        Check(!Step(s, p, q, 1000 + kFrameNs, Params{}), "the same sample again is not a step");
        Check(Step(s, p, q, 1000 + kRepeatHoldNs + kFrameNs, Params{}) && s.pos[0] == p[0],
              "a pose repeated for longer is a still hand: the filter steps on it (and stays on it)");
        const float far[3]{0.6f, 1.0f, -0.1f};
        Check(!Step(s, far, kIdentity, 1000 + kRepeatHoldNs + kStaleNs + 3 * kFrameNs, Params{}) && s.pos[0] == far[0] &&
                  s.rot[3] == 1.0f,
              "after a gap the filter restarts on the raw pose instead of easing across it");
        Check(!Step(s, p, q, 500, Params{}) && s.pos[0] == p[0], "a sample time that runs backwards restarts it too");
    }
    // A still hand with tracking jitter is held steadier than the raw signal.
    {
        State s;
        int64_t t = 0;
        float worstRaw = 0.0f, worstFiltered = 0.0f;
        for (int i = 0; i < 400; ++i)
        {
            t += kFrameNs;
            const float jitter = 0.0001f * static_cast<float>((i * 7919) % 11 - 5) / 5.0f;   // +-0.1 mm
            const float p[3]{0.2f + jitter, 1.1f - jitter, -0.4f + 0.5f * jitter};
            (void)Step(s, p, kIdentity, t, Params{});
            if (i > 100)
            {
                worstRaw = std::fmax(worstRaw, std::fabs(p[0] - 0.2f));
                worstFiltered = std::fmax(worstFiltered, std::fabs(s.pos[0] - 0.2f));
            }
        }
        Check(worstRaw > 0.00008f && worstFiltered < 0.7f * worstRaw, "a still hand's jitter is reduced");
    }
    // A fast move passes through with little lag, and the filter never overshoots a steady move.
    {
        State s;
        int64_t t = 0;
        float x = 0.0f, worstLag = 0.0f;
        const float p0[3]{0.0f, 1.0f, 0.0f};
        (void)Step(s, p0, kIdentity, t, Params{});
        for (int i = 0; i < 90; ++i)   // one second at 1 m/s
        {
            t += kFrameNs;
            x += 1.0f * static_cast<float>(kFrameNs) * 1e-9f;
            const float p[3]{x, 1.0f, 0.0f};
            Check(Step(s, p, kIdentity, t, Params{}), "a new sample steps the filter");
            Check(s.pos[0] <= x + 1e-6f, "a steady move is never overshot");
            if (i > 10)
                worstLag = std::fmax(worstLag, x - s.pos[0]);
        }
        Check(worstLag < 0.004f, "at 1 m/s the steadied hand is within 4 mm of the tracked one");
    }
    // A hand that jumps and is then reported exactly still is settled onto, not left short.
    {
        State s;
        int64_t t = 0;
        float from[4], to[4];
        YawQuat(0.0f, from);
        YawQuat(0.35f, to);
        const float a[3]{0.0f, 1.0f, 0.0f}, b[3]{0.10f, 1.0f, 0.0f};
        (void)Step(s, a, from, t, Params{});
        t += kFrameNs;
        (void)Step(s, b, to, t, Params{});
        const float shortBy = AngleBetween(s.rot, to);
        Check(shortBy > 0.001f, "one step does not reach a pose the hand jumped to");
        for (int i = 0; i < 180; ++i)   // two seconds of the identical pose
        {
            t += kFrameNs;
            (void)Step(s, b, to, t, Params{});
        }
        Check(AngleBetween(s.rot, to) < 0.0005f && std::fabs(s.pos[0] - b[0]) < 0.0001f,
              "two seconds of the same pose: the steadied hand is on it");
    }
    // A slow drift is followed (it is smoothing, not a dead zone).
    {
        State s;
        int64_t t = 0;
        float x = 0.0f;
        const float p0[3]{0.0f, 1.0f, 0.0f};
        (void)Step(s, p0, kIdentity, t, Params{});
        for (int i = 0; i < 270; ++i)   // three seconds at 1 cm/s
        {
            t += kFrameNs;
            x += 0.01f * static_cast<float>(kFrameNs) * 1e-9f;
            const float p[3]{x, 1.0f, 0.0f};
            (void)Step(s, p, kIdentity, t, Params{});
        }
        Check(x - s.pos[0] < 0.002f && x - s.pos[0] >= 0.0f, "a slow drift is followed to within 2 mm");
    }
    // Rotation: a fast turn passes through, the result stays a unit quaternion, and the short way is taken.
    {
        State s;
        int64_t t = 0;
        float yaw = 3.0f, q[4];
        const float p[3]{0.0f, 1.0f, 0.0f};
        YawQuat(yaw, q);
        (void)Step(s, p, q, t, Params{});
        float worst = 0.0f;
        for (int i = 0; i < 90; ++i)   // 3 rad/s through the quaternion's sign flip at pi
        {
            t += kFrameNs;
            yaw += 3.0f * static_cast<float>(kFrameNs) * 1e-9f;
            YawQuat(yaw, q);
            if (i % 2)
                for (float& c : q)
                    c = -c;   // the same rotation with the other sign: must not be read as a turn
            (void)Step(s, p, q, t, Params{});
            Check(FiniteUnit(s.rot), "the steadied rotation stays a unit quaternion");
            if (i > 10)
                worst = std::fmax(worst, AngleBetween(s.rot, q));
        }
        Check(worst < 0.045f, "at 3 rad/s the steadied hand is within 2.6 degrees of the tracked one");
    }
    // Each half can be switched off by its own cutoff.
    {
        State s;
        Params rotOnly;
        rotOnly.posMinHz = 0.0f;
        const float a[3]{0.0f, 1.0f, 0.0f}, b[3]{0.0003f, 1.0f, 0.0f};
        float q[4];
        YawQuat(0.001f, q);
        (void)Step(s, a, kIdentity, 0, rotOnly);
        (void)Step(s, b, q, kFrameNs, rotOnly);
        Check(s.pos[0] == b[0] && AngleBetween(s.rot, q) > 1e-5f, "position off: the position is raw, the rotation steadied");
        State r;
        Params posOnly;
        posOnly.rotMinHz = 0.0f;
        (void)Step(r, a, kIdentity, 0, posOnly);
        (void)Step(r, b, q, kFrameNs, posOnly);
        Check(r.rot[1] == q[1] && r.rot[3] == q[3] && r.pos[0] < b[0], "rotation off: the rotation is raw, the position steadied");
    }
    // Which hands, and what counts as a usable sample.
    {
        Check(HandWanted(0, true) && HandWanted(0, false), "0: both hands");
        Check(HandWanted(1, true) && !HandWanted(1, false), "1: only the hand that aims");
        Check(!HandWanted(2, true) && HandWanted(2, false), "2: only the other hand");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float bad[3]{0.0f, nan, 0.0f}, zero[4]{}, good[3]{0.0f, 1.0f, 0.0f};
        Check(!Finite3(bad) && Finite3(good), "a position that is not a number is not a sample");
        Check(!FiniteUnit(zero) && FiniteUnit(kIdentity), "a quaternion that is not unit length is not a sample");
        Check(Alpha(0.0f, 0.011f) == 0.0f && Alpha(1.0f, 0.0f) == 0.0f, "no cutoff or no interval moves nothing");
    }
    std::printf("Hand smoothing: %u checks passed\n", checks);
    return 0;
}
