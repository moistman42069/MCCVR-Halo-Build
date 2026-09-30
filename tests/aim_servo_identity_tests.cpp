#include "../src/common/aim_servo_logic.h"

#include <cmath>
#include <cstdio>

namespace
{
unsigned checks = 0;
unsigned failures = 0;

void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

bool Near(float actual, float expected, float tolerance = 2.0e-5f)
{
    return std::fabs(actual - expected) <= tolerance;
}

void TestSameSampleHeadParallax()
{
    // The primary aim ray and H0 belong to one selected committed sample A0.
    // A live H1 can advance before XInput consumes the returned pose; the
    // production target helper must use the caller's H0 rather than substitute
    // that later value.
    const float aimA0[3]{0.2f, 1.4f, -0.3f};
    const float forwardA0[3]{0.0f, 0.0f, -1.0f};
    const float headH0[3]{0.0f, 1.6f, 0.0f};
    const float liveHeadH1[3]{0.6f, 1.5f, 0.1f};

    const AimServoParallaxRay coherent = AimServoParallaxRayFromHead(
        aimA0, forwardA0, headH0, 10.0f);
    Check(Near(coherent.x * coherent.distance, 0.2f) &&
            Near(coherent.y * coherent.distance, -0.2f) &&
            Near(coherent.z * coherent.distance, -10.3f),
        "production target is A0 + forward*distance - H0");

    const AimServoParallaxRay mixed = AimServoParallaxRayFromHead(
        aimA0, forwardA0, liveHeadH1, 10.0f);
    Check(Near(mixed.x * mixed.distance, -0.4f) &&
            Near(mixed.y * mixed.distance, -0.1f) &&
            Near(mixed.z * mixed.distance, -10.4f),
        "a later H1 produces a distinct target from the A0/H0 result");
    Check(std::fabs(coherent.x - mixed.x) > 0.05f ||
            std::fabs(coherent.y - mixed.y) > 0.01f,
        "substituting H1 would numerically change the applied head-origin ray");
}

void TestLegacyCoincidentOriginParity()
{
    const float aimPosition[3]{0.2f, 1.4f, -0.3f};
    const float forward[3]{0.0f, 0.0f, -1.0f};
    const float liveHead[3]{0.2f, 1.4f, -0.3f};
    const AimServoParallaxRay ray = AimServoParallaxRayFromHead(
        aimPosition, forward, liveHead, 10.0f);
    Check(ray.x == 0.0f && ray.y == 0.0f && ray.z == -1.0f &&
            ray.distance == 10.0f,
        "coincident legacy aim/head origins retain the unchanged forward ray");
}
} // namespace

int main()
{
    TestSameSampleHeadParallax();
    TestLegacyCoincidentOriginParity();
    std::printf("Aim-servo identity: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
