// Which present carries the game's picture (src/common/present_owner_logic.h): a D3D11 swapchain, not a host's
// token surface, and on the headset session's device once there is one.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../src/common/present_owner_logic.h"
#include "../src/common/vr_desktop_mirror_logic.h"

using namespace present_owner;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    constexpr uintptr_t kGameDevice = 0x1000, kOtherDevice = 0x2000;

    // MCC: one swapchain, before and after the session.
    Check(Classify(true, false, true, 2912, 2100, kGameDevice, 0) == Verdict::Game, "MCC's swapchain before the session");
    Check(Classify(true, false, true, 2912, 2100, kGameDevice, kGameDevice) == Verdict::Game,
          "MCC's swapchain on the session's device");
    // MCC never has a host's token rule: a small swapchain there stays the game's.
    Check(Classify(true, false, true, 1, 1, kGameDevice, 0) == Verdict::Game, "no token rule outside a host");

    // Project Reclaimer: the 1x1 surface that presents first is never the game's, before or after the session.
    Check(Classify(true, true, true, 1, 1, kOtherDevice, 0) == Verdict::TokenSurface, "a host's 1x1 surface");
    Check(Classify(true, true, true, 1, 1, kGameDevice, kGameDevice) == Verdict::TokenSurface,
          "a host's 1x1 surface on the session's own device");
    Check(Classify(true, true, true, 63, 2100, kGameDevice, 0) == Verdict::TokenSurface, "63 across is a token");
    Check(Classify(true, true, true, 2912, 63, kGameDevice, 0) == Verdict::TokenSurface, "63 down is a token");
    Check(Classify(true, true, true, 64, 64, kGameDevice, 0) == Verdict::Game, "64 by 64 is a picture");
    Check(Classify(true, true, true, 2912, 2100, kGameDevice, 0) == Verdict::Game, "the game's swapchain, first");
    // An unread size is not taken for a token: the device rule still applies.
    Check(Classify(true, true, false, 0, 0, kGameDevice, 0) == Verdict::Game, "an unread size is not a token");

    // Another device than the session's is never the game's, in any host.
    Check(Classify(true, true, true, 2912, 2100, kOtherDevice, kGameDevice) == Verdict::OtherDevice,
          "a host's picture on another device");
    Check(Classify(true, false, true, 2912, 2100, kOtherDevice, kGameDevice) == Verdict::OtherDevice,
          "MCC's picture on another device");

    // Not D3D11.
    Check(Classify(false, false, true, 2912, 2100, kGameDevice, 0) == Verdict::NotD3D11, "not a D3D11 swapchain");
    Check(Classify(true, false, true, 2912, 2100, 0, 0) == Verdict::NotD3D11, "no device at all");

    Check(IsTokenSurface(1, 1) && !IsTokenSurface(kTokenSurface, kTokenSurface), "the token bound");
    for (Verdict verdict : {Verdict::Game, Verdict::NotD3D11, Verdict::TokenSurface, Verdict::OtherDevice})
        Check(Describe(verdict) && std::strcmp(Describe(verdict), "unknown") != 0, "every verdict is described");

    // A host's second device: the session's device answers its next engine's call, and nobody else's.
    const auto share = [](bool wants, bool host, bool session, bool lost, uint64_t quiet, bool adapter, uint32_t flags,
                          bool level) {
        return DecideDeviceShare(wants, host, session, lost, quiet, adapter, flags, 0x20u, level);
    };
    Check(share(true, true, true, false, 1000, true, 0x20u, true) == DeviceShare::Share, "the next engine's device");
    Check(share(true, true, true, false, kEngineGoneQuietMs, true, 0u, true) == DeviceShare::Share,
          "fewer flags than the session's device has");
    Check(share(false, true, true, false, 1000, true, 0u, true) == DeviceShare::NotAsked, "a capability probe");
    Check(share(true, false, true, false, 1000, true, 0u, true) == DeviceShare::NotHostsCall, "a runtime's own call");
    Check(share(true, true, false, false, 1000, true, 0u, true) == DeviceShare::NoSession, "the first device");
    Check(share(true, true, true, true, 1000, true, 0u, true) == DeviceShare::SessionLost, "a lost device");
    Check(share(true, true, true, false, kEngineGoneQuietMs - 1, true, 0u, true) == DeviceShare::GameStillPresenting,
          "the host's other screens while the game draws");
    Check(share(true, true, true, false, 1000, false, 0u, true) == DeviceShare::OtherAdapter, "another GPU");
    Check(share(true, true, true, false, 1000, true, 0x2u, true) == DeviceShare::FlagsDiffer, "a flag it lacks");
    Check(share(true, true, true, false, 1000, true, 0u, false) == DeviceShare::FeatureLevelRefused, "a level refused");
    Check(DecideDeviceShare(true, true, true, false, 1000, true, 0u, 0x20u, true, false) == DeviceShare::SwitchedOff,
          "switched off");
    Check(DecideDeviceShare(false, true, true, false, 1000, true, 0u, 0x20u, true, false) == DeviceShare::NotAsked,
          "a probe is no decision of the switch's");
    Check(DecideDeviceShare(true, true, true, false, 1000, true, 0u, 0x20u, true, true, true) ==
              DeviceShare::LastAttemptFailed,
          "the last launch's hand-over never reached a frame");
    Check(DecideDeviceShare(true, true, true, false, 1000, true, 0u, 0x20u, true, true, false, true) ==
              DeviceShare::AnotherThreadHasIt,
          "one engine change, one thread");
    Check(DecideDeviceShare(true, true, true, false, 0, true, 0u, 0x20u, true, true, true, true) ==
              DeviceShare::GameStillPresenting,
          "while the game draws no hand-over is even considered");
    for (DeviceShare decision : {DeviceShare::SwitchedOff, DeviceShare::LastAttemptFailed,
                                 DeviceShare::AnotherThreadHasIt, DeviceShare::Share, DeviceShare::NotAsked,
                                 DeviceShare::NotHostsCall,
                                 DeviceShare::NoSession, DeviceShare::SessionLost, DeviceShare::GameStillPresenting,
                                 DeviceShare::OtherAdapter, DeviceShare::FlagsDiffer,
                                 DeviceShare::FeatureLevelRefused})
        Check(Describe(decision) && std::strcmp(Describe(decision), "unknown") != 0, "every decision is described");

    // One eye on the window: true proportions, centred, cropped on one axis.
    float left = 0, top = 0, width = 0, height = 0;
    const auto near = [](float a, float b) { return a - b < 0.01f && b - a < 0.01f; };
    Check(vr_desktop_mirror::CropViewport(1.0f, 1.0f, 3840, 2160, 2912, 2100, left, top, width, height) &&
              near(width, 2912.0f) && near(height, 2100.0f * 16.0f / 9.0f) && near(left, 0.0f) &&
              near(top, (2100.0f - height) * 0.5f),
          "a square eye on a wide window fills its width and crops top and bottom");
    Check(vr_desktop_mirror::CropViewport(2.0f, 1.0f, 1000, 1000, 1000, 1000, left, top, width, height) &&
              near(width, 2000.0f) && near(height, 1000.0f) && near(left, -500.0f) && near(top, 0.0f),
          "a wide eye on a square window crops the sides");
    Check(!vr_desktop_mirror::CropViewport(0.0f, 1.0f, 3840, 2160, 2912, 2100, left, top, width, height) &&
              !vr_desktop_mirror::CropViewport(1.0f, 1.0f, 0, 2160, 2912, 2100, left, top, width, height) &&
              !vr_desktop_mirror::CropViewport(1.0f, 1.0f, 3840, 2160, 0, 2100, left, top, width, height),
          "nothing to place refuses");

    std::printf("present_owner: %u checks passed\n", g_checks);
    return 0;
}
