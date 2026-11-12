// Scope convergence gates (src/dll/sight_layer.h): the zoom lens looks along the intent ray only while the
// converged aim is in effect AND written, and it takes the ray's direction, never a point on it.
#include "../src/dll/sight_layer.h"
#include "../src/dll/title_adapter.h"
#include "../src/common/config.h"

#include <windows.h>
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
uint32_t testGeneration = 7;

void PublishRay(GameTitle title, const float head[3], const float dir[3])
{
    SightRay ray{};
    ray.generation = testGeneration;
    ray.sampleMs = GetTickCount64();
    for (int axis = 0; axis < 3; ++axis)
    {
        ray.head[axis] = head[axis];
        ray.dir[axis] = dir[axis];
        ray.eye[axis] = head[axis];
    }
    Sight_PublishRay(title, ray);
}

bool Near(const float a[3], const float b[3])
{
    return std::fabs(a[0] - b[0]) < 1e-5f && std::fabs(a[1] - b[1]) < 1e-5f && std::fabs(a[2] - b[2]) < 1e-5f;
}
} // namespace

Config::Config() {}
Config g_config;
GameTitle TitleAdapter_GetActiveTitle() { return GameTitle::Halo3; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return testGeneration; }
void Logf(const char*, ...) {}

int main()
{
    g_config.converged_aim = 1;
    g_config.scope_converge = 1;
    const float head[3]{2.0f, 3.0f, 4.0f};
    const float dir[3]{0.0f, 0.6f, 0.8f};
    float out[3]{9.0f, 9.0f, 9.0f};

    PublishRay(GameTitle::Halo3, head, dir);
    Check(!Sight_ScopeConvergedDirection(GameTitle::Halo3, out) && out[0] == 9.0f,
          "nothing converged: the lens keeps its own direction, untouched");

    // The stick servo: the target is converged but the game's aim is still turning toward it.
    Sight_MarkConverged(GameTitle::Halo3, 1.0f);
    Check(Sight_ConvergedLive(GameTitle::Halo3) && !Sight_ScopeConvergedLive(GameTitle::Halo3) &&
              !Sight_ScopeConvergedDirection(GameTitle::Halo3, out),
          "a converged target the servo is still steering toward never converges the lens");

    // Direct drive: the converged aim is the game's aim.
    Sight_MarkAimWritten(GameTitle::Halo3);
    Check(Sight_ScopeConvergedLive(GameTitle::Halo3) && Sight_ScopeConvergedDirection(GameTitle::Halo3, out) &&
              Near(out, dir),
          "a converged, written aim: the lens looks along the intent ray");

    // The head moved since the ray was sampled: the lens direction does not move with it (a look-at point
    // built from the old head would swing the lens by the offset over the range).
    const float movedHead[3]{2.3f, 2.8f, 4.1f};
    PublishRay(GameTitle::Halo3, movedHead, dir);
    Check(Sight_ScopeConvergedDirection(GameTitle::Halo3, out) && Near(out, dir),
          "the lens direction does not depend on where the ray's head was sampled");

    // A ray published unnormalised comes back as a unit direction.
    const float longDir[3]{0.0f, 3.0f, 4.0f};
    PublishRay(GameTitle::Halo3, head, longDir);
    Check(Sight_ScopeConvergedDirection(GameTitle::Halo3, out) && Near(out, dir), "the direction is unit length");

    // The player keys.
    g_config.scope_converge = 0;
    Check(!Sight_ScopeConvergedLive(GameTitle::Halo3) && !Sight_ScopeConvergedDirection(GameTitle::Halo3, out),
          "scope_converge off keeps the earlier lens");
    g_config.scope_converge = 1;
    g_config.converged_aim = 0;
    Check(!Sight_ScopeConvergedLive(GameTitle::Halo3), "converged_aim off keeps the earlier lens");
    g_config.converged_aim = 1;

    // Per title: another title's written stamp says nothing about this one.
    Sight_MarkConverged(GameTitle::Halo2, 1.0f);
    Check(!Sight_ScopeConvergedLive(GameTitle::Halo2), "Halo 2 converged but not written: no lens convergence");
    Sight_MarkAimWritten(GameTitle::Halo2);
    Check(Sight_ScopeConvergedLive(GameTitle::Halo2), "Halo 2 converged and written: the lens converges");

    // A new title generation: both stamps are stale.
    testGeneration = 8;
    Check(!Sight_ScopeConvergedLive(GameTitle::Halo3) && !Sight_AimWrittenLive(GameTitle::Halo3),
          "a new generation never inherits the old one's written aim");
    testGeneration = 7;
    Check(Sight_ScopeConvergedLive(GameTitle::Halo3), "fixture: the stamps are live again for their generation");

    // The written stamp goes stale on its own: a drive that stopped writing leaves the lens within 100 ms.
    Sight_MarkConverged(GameTitle::Halo3, 1.0f);
    Sleep(130);
    Sight_MarkConverged(GameTitle::Halo3, 1.0f);   // the target keeps converging under the servo
    Check(Sight_ConvergedLive(GameTitle::Halo3) && !Sight_AimWrittenLive(GameTitle::Halo3) &&
              !Sight_ScopeConvergedLive(GameTitle::Halo3),
          "an aim no longer written stops converging the lens");

    // A reset forgets both.
    Sight_MarkAimWritten(GameTitle::Halo3);
    Sight_ResetTitle(GameTitle::Halo3);
    Check(!Sight_ConvergedLive(GameTitle::Halo3) && !Sight_AimWrittenLive(GameTitle::Halo3),
          "a title reset forgets the converged and written stamps");

    std::printf("%s: %u sight scope checks, %u failed\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
