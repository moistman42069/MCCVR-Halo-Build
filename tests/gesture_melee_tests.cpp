#include "../src/common/gesture_melee_logic.h"
#include <cstdio>

using namespace gesture_melee;

namespace
{
int g_failed = 0;
void Check(bool ok, const char* why)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", why);
        ++g_failed;
    }
}

constexpr float kDt = 1.0f / 90.0f;
constexpr float kHead[3]{0.0f, 1.6f, 0.0f};

struct Run
{
    Detector detector;
    int fired = 0;
    // Moves the hand at a constant velocity, head-relative unless the head moves too.
    void Move(float hand[3], const float velocity[3], float seconds, bool grip, const Settings& settings,
              const float headVelocity[3] = nullptr)
    {
        float head[3]{kHead[0], kHead[1], kHead[2]};
        for (float t = 0.0f; t < seconds; t += kDt)
        {
            for (int i = 0; i < 3; ++i)
            {
                hand[i] += velocity[i] * kDt;
                if (headVelocity) head[i] += headVelocity[i] * kDt;
            }
            if (detector.Step(true, hand, head, grip, kDt, settings)) ++fired;
        }
    }
};
}

int main()
{
    const Settings settings{};
    const float rest[3]{0.0f, 0.0f, 0.0f};
    {
        // A thrust away from the head.
        Run run;
        float hand[3]{0.0f, 1.3f, -0.15f};
        const float thrust[3]{0.0f, 0.0f, -4.0f};
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, thrust, 0.15f, false, settings);
        Check(run.fired == 1, "a thrust strikes, once");
        run.Move(hand, thrust, 0.05f, false, settings);
        Check(run.fired == 1, "still moving is the same swing");
        run.Move(hand, rest, 0.3f, false, settings);
        hand[2] = -0.15f;
        run.detector.Step(false, hand, kHead, false, kDt, settings);
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, thrust, 0.15f, false, settings);
        Check(run.fired == 2, "after the hand settles, another swing strikes");
    }
    {
        // An aim turn: the hand orbits the head at constant radius, fast.
        Run run;
        const float radius = 0.5f;
        const float rate = 10.0f; // 5 m/s along the arc
        float angle = 0.0f;
        for (int i = 0; i < 60; ++i)
        {
            angle += rate * kDt;
            const float hand[3]{radius * std::sin(angle), kHead[1], -radius * std::cos(angle)};
            if (run.detector.Step(true, hand, kHead, false, kDt, settings)) ++run.fired;
        }
        Check(run.fired == 0, "a fast aim turn does not strike");
    }
    {
        // Walking or a vehicle moves head and hand together.
        Run run;
        float hand[3]{0.0f, 1.3f, -0.4f};
        const float walk[3]{0.0f, 0.0f, -6.0f};
        run.Move(hand, walk, 0.4f, false, settings, walk);
        Check(run.fired == 0, "body motion is not a swing");
    }
    {
        // A twitch close to the chest.
        Run run;
        float hand[3]{0.0f, 1.55f, -0.05f};
        const float twitch[3]{0.0f, 0.0f, -4.0f};
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, twitch, 0.04f, false, settings);
        Check(run.fired == 0, "a hand that never got out does not strike");
    }
    {
        // A chop: fast and far, across the body, at nearly constant distance from the head.
        const float radius = 0.5f;
        const auto chop = [&](bool grip) {
            Run run;
            float angle = -0.8f;
            for (int i = 0; i < 40; ++i)
            {
                angle += 8.0f * kDt;
                const float hand[3]{0.0f, kHead[1] + radius * std::sin(angle), -radius * std::cos(angle)};
                if (run.detector.Step(true, hand, kHead, grip, kDt, settings)) ++run.fired;
            }
            return run.fired;
        };
        Check(chop(true) == 1, "with the grip held, a chop strikes on its travel");
        Check(chop(false) == 0, "with the grip open, the same motion is an aim sweep");
    }
    {
        Settings fist = settings;
        fist.requireGrip = true;
        Run open, closed;
        float a[3]{0.0f, 1.3f, -0.15f}, b[3]{0.0f, 1.3f, -0.15f};
        const float thrust[3]{0.0f, 0.0f, -4.0f};
        open.Move(a, rest, 0.1f, false, fist);
        open.Move(a, thrust, 0.15f, false, fist);
        closed.Move(b, rest, 0.1f, true, fist);
        closed.Move(b, thrust, 0.15f, true, fist);
        Check(open.fired == 0 && closed.fired == 1, "grip required: only a fist strikes");
    }
    {
        // Tracking jumps.
        Run run;
        float hand[3]{0.0f, 1.3f, -0.3f};
        run.Move(hand, rest, 0.1f, false, settings);
        const float jumped[3]{0.0f, 1.3f, -0.9f};
        if (run.detector.Step(true, jumped, kHead, false, kDt, settings)) ++run.fired;
        Check(run.fired == 0, "a teleport is not a swing");
        if (run.detector.Step(true, jumped, kHead, false, kDt, settings)) ++run.fired;
        Check(run.fired == 0, "nor is the sample after it");
        const float tooFar[3]{0.0f, 1.3f, -2.5f};
        if (run.detector.Step(true, tooFar, kHead, false, kDt, settings)) ++run.fired;
        Check(run.fired == 0, "an impossible reach is refused");
        const float nan[3]{std::nanf(""), 1.3f, -0.3f};
        Check(!run.detector.Step(true, nan, kHead, false, kDt, settings), "a bad pose is refused");
        Check(!run.detector.Step(true, hand, kHead, false, 1.0f, settings), "a stall is not differentiated across");
        Check(!run.detector.Step(false, hand, kHead, false, kDt, settings), "a lost hand is refused");
    }
    {
        // Above 5 m/s is a usable setting: this used to turn the gesture off without saying so.
        Settings fast = settings;
        fast.speed = 8.0f;
        Run slow, quick;
        float a[3]{0.0f, 1.3f, -0.1f}, b[3]{0.0f, 1.3f, -0.1f};
        const float four[3]{0.0f, 0.0f, -4.0f}, nine[3]{0.0f, 0.0f, -9.0f};
        slow.Move(a, rest, 0.1f, false, fast);
        slow.Move(a, four, 0.2f, false, fast);
        quick.Move(b, rest, 0.1f, false, fast);
        quick.Move(b, nine, 0.1f, false, fast);
        Check(slow.fired == 0 && quick.fired == 1, "a high required speed still works");
    }
    if (!g_failed) std::printf("gesture melee tests passed\n");
    return g_failed ? 1 : 0;
}
