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
    Swing swing{};
    // Moves the hand at a constant velocity, head-relative unless the head moves too.
    void Move(float hand[3], const float velocity[3], float seconds, bool grip, const Settings& settings,
              bool offHand = false, bool standDown = false, const float headVelocity[3] = nullptr)
    {
        float head[3]{kHead[0], kHead[1], kHead[2]};
        for (float t = 0.0f; t < seconds; t += kDt)
        {
            for (int i = 0; i < 3; ++i)
            {
                hand[i] += velocity[i] * kDt;
                if (headVelocity) head[i] += headVelocity[i] * kDt;
            }
            if (detector.Step(true, hand, head, grip, offHand, standDown, kDt, settings, &swing)) ++fired;
        }
    }
};

int Chop(bool offHand, bool grip, const Settings& settings)
{
    // Fast and far, across the body, at nearly constant distance from the head.
    Detector detector;
    const float radius = 0.5f;
    float angle = -0.8f;
    int fired = 0;
    for (int i = 0; i < 40; ++i)
    {
        angle += 8.0f * kDt;
        const float hand[3]{0.0f, kHead[1] + radius * std::sin(angle), -radius * std::cos(angle)};
        if (detector.Step(true, hand, kHead, grip, offHand, false, kDt, settings)) ++fired;
    }
    return fired;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }
}

int main()
{
    const Settings settings{};
    const float rest[3]{0.0f, 0.0f, 0.0f};
    const float thrust[3]{0.0f, 0.0f, -4.0f};
    {
        Run run;
        float hand[3]{0.0f, 1.3f, -0.15f};
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, thrust, 0.15f, false, settings);
        Check(run.fired == 1, "a thrust strikes, once");
        Check(Near(run.swing.direction[2], -1.0f) && Near(run.swing.direction[0], 0.0f), "and says where it was going");
        run.Move(hand, thrust, 0.05f, false, settings);
        Check(run.fired == 1, "still moving is the same swing");
        run.Move(hand, rest, 0.3f, false, settings);
        hand[2] = -0.15f;
        run.detector.Step(false, hand, kHead, false, false, false, kDt, settings);
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, thrust, 0.15f, false, settings);
        Check(run.fired == 2, "after the hand settles, another swing strikes");
    }
    {
        // An aim turn: the hand orbits the head at constant radius, fast.
        Detector detector;
        const float radius = 0.5f;
        float angle = 0.0f;
        int fired = 0;
        for (int i = 0; i < 60; ++i)
        {
            angle += 10.0f * kDt; // 5 m/s along the arc
            const float hand[3]{radius * std::sin(angle), kHead[1], -radius * std::cos(angle)};
            if (detector.Step(true, hand, kHead, false, false, false, kDt, settings)) ++fired;
        }
        Check(fired == 0, "a fast aim turn does not strike");
    }
    {
        // Walking or a vehicle moves head and hand together.
        Run run;
        float hand[3]{0.0f, 1.3f, -0.4f};
        const float walk[3]{0.0f, 0.0f, -6.0f};
        run.Move(hand, walk, 0.4f, false, settings, false, false, walk);
        Check(run.fired == 0, "body motion is not a swing");
    }
    {
        // A twitch close to the chest.
        Run run;
        float hand[3]{0.0f, 1.55f, -0.05f};
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, thrust, 0.04f, false, settings);
        Check(run.fired == 0, "a hand that never got out does not strike");
    }
    {
        Check(Chop(true, false, settings) == 1, "an off-hand chop strikes on its travel");
        Check(Chop(false, false, settings) == 0, "the same motion in the gun hand is an aim sweep");
        Check(Chop(false, true, settings) == 0, "whatever its grip");
        Settings noTravel = settings;
        noTravel.travel = 0.0f;
        Check(Chop(true, false, noTravel) == 0, "travel 0 turns the chop off");
    }
    {
        Settings fist = settings;
        fist.requireGrip = true;
        Run open, closed;
        float a[3]{0.0f, 1.3f, -0.15f}, b[3]{0.0f, 1.3f, -0.15f};
        open.Move(a, rest, 0.1f, false, fist);
        open.Move(a, thrust, 0.15f, false, fist);
        closed.Move(b, rest, 0.1f, true, fist);
        closed.Move(b, thrust, 0.15f, true, fist);
        Check(open.fired == 0 && closed.fired == 1, "grip required: only a fist strikes");
    }
    {
        // A hand with a job of its own.
        Run run;
        float hand[3]{0.0f, 1.3f, -0.15f};
        run.Move(hand, rest, 0.1f, false, settings);
        run.Move(hand, thrust, 0.15f, false, settings, true, true);
        Check(run.fired == 0, "a hand standing down does not strike");
    }
    {
        // Tracking jumps.
        Run run;
        float hand[3]{0.0f, 1.3f, -0.3f};
        run.Move(hand, rest, 0.1f, false, settings);
        const float jumped[3]{0.0f, 1.3f, -0.9f};
        Detector& d = run.detector;
        Check(!d.Step(true, jumped, kHead, false, false, false, kDt, settings), "a teleport is not a swing");
        Check(!d.Step(true, jumped, kHead, false, false, false, kDt, settings), "nor is the sample after it");
        const float tooFar[3]{0.0f, 1.3f, -2.5f};
        Check(!d.Step(true, tooFar, kHead, false, false, false, kDt, settings), "an impossible reach is refused");
        const float nan[3]{std::nanf(""), 1.3f, -0.3f};
        Check(!d.Step(true, nan, kHead, false, false, false, kDt, settings), "a bad pose is refused");
        Check(!d.Step(true, hand, kHead, false, false, false, 1.0f, settings), "a stall is not differentiated across");
        Check(!d.Step(false, hand, kHead, false, false, false, kDt, settings), "a lost hand is refused");
    }
    {
        Settings fast = settings;
        fast.speed = 8.0f;
        Run slow, quick;
        float a[3]{0.0f, 1.3f, -0.1f}, b[3]{0.0f, 1.3f, -0.1f};
        const float nine[3]{0.0f, 0.0f, -9.0f};
        slow.Move(a, rest, 0.1f, false, fast);
        slow.Move(a, thrust, 0.2f, false, fast);
        quick.Move(b, rest, 0.1f, false, fast);
        quick.Move(b, nine, 0.1f, false, fast);
        Check(slow.fired == 0 && quick.fired == 1, "a high required speed still works");
    }
    {
        const float swing[3]{1.0f, 0.0f, 0.0f}; // to the right
        float aim[3]{0.0f, 0.0f, -1.0f};        // straight ahead
        Check(!ApplyAimHold(aim, swing, 1000, 0, 150) && Near(aim[2], -1.0f), "no hold leaves the aim alone");
        Check(ApplyAimHold(aim, swing, 1000, 1250, 150) && Near(aim[0], 1.0f) && Near(aim[2], 0.0f), "held along the swing");
        float half[3]{0.0f, 0.0f, -1.0f};
        Check(ApplyAimHold(half, swing, 1325, 1250, 150) && Near(half[0], 0.7071f) && Near(half[2], -0.7071f),
              "then blends back");
        float done[3]{0.0f, 0.0f, -1.0f};
        Check(!ApplyAimHold(done, swing, 1400, 1250, 150) && Near(done[2], -1.0f), "and lets go");
        const float behind[3]{-0.1736f, 0.0f, 0.9848f}; // yaw -170
        float wrap[3]{0.1736f, 0.0f, 0.9848f};          // yaw +170
        ApplyAimHold(wrap, behind, 1325, 1250, 150);
        Check(Near(wrap[0], 0.0f) && Near(wrap[2], 1.0f), "yaw takes the short way round");
        const float up[3]{0.0f, 1.0f, 0.0f};
        float pitched[3]{0.0f, 0.0f, -1.0f};
        ApplyAimHold(pitched, up, 1000, 1250, 150);
        Check(Near(pitched[1], 1.0f), "a straight-up swing stays finite");
    }
    if (!g_failed) std::printf("gesture melee tests passed\n");
    return g_failed ? 1 : 0;
}
