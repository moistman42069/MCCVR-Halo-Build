#pragma once

#include <cstdint>

// WHICH PRESENT CARRIES THE GAME'S PICTURE (d3d11_hook.cpp, IsGameSwapchain).
//
// MCC presents one swapchain. A host can present others beside it: Project Reclaimer has been seen presenting a
// 1x1 swapchain from a thread of its own (its OpenGL screens going out through DXGI) for half a minute before the
// game's swapchain existed, and then both, from two threads. Everything the mod does at a present belongs to the
// game's swapchain alone: the headset's frame, the session it is made on, the host screen's tracking, a resize's
// release of the mod's views. Done for another swapchain it uses one device's immediate context from a second
// thread and copies between two devices, which gets the device removed.
//
// So a present is the game's only when its swapchain belongs to a D3D11 device; in a host that draws the game at
// its own size, when its buffers are bigger than a token surface; and, once the headset's session exists, when
// its device is the session's. Pure logic, unit-tested offline.
namespace present_owner
{
// Smaller than this across or down, a host's swapchain is a token surface, not a picture of the game.
constexpr uint32_t kTokenSurface = 64;

enum class Verdict : uint8_t
{
    Game = 0,
    NotD3D11,     // its device is not a D3D11 device
    TokenSurface, // a host's swapchain too small to be the game's picture
    OtherDevice,  // made on another device than the headset session's
};

constexpr bool IsTokenSurface(uint32_t width, uint32_t height)
{
    return width < kTokenSurface || height < kTokenSurface;
}

// `described`: the swapchain's size was read. `device` / `sessionDevice` are identities (0 = none yet).
constexpr Verdict Classify(bool d3d11, bool hostOwnsRenderSize, bool described, uint32_t width, uint32_t height,
                           uintptr_t device, uintptr_t sessionDevice)
{
    if (!d3d11 || !device)
        return Verdict::NotD3D11;
    if (hostOwnsRenderSize && described && IsTokenSurface(width, height))
        return Verdict::TokenSurface;
    if (sessionDevice && device != sessionDevice)
        return Verdict::OtherDevice;
    return Verdict::Game;
}

constexpr const char* Describe(Verdict verdict)
{
    switch (verdict)
    {
    case Verdict::Game: return "the game's picture";
    case Verdict::NotD3D11: return "not on a D3D11 device";
    case Verdict::TokenSurface: return "too small to be the game's picture (a host's own surface)";
    case Verdict::OtherDevice: return "on another device than the headset session's";
    }
    return "unknown";
}

// A HOST'S SECOND DEVICE. Project Reclaimer creates a new D3D11 device when it starts another title's engine (Halo 3's
// menu, then ODST). The headset session and everything the mod made is bound to the first, so the second engine's
// frames could never reach the headset. MCC keeps one device for every title, and the mod's title switch is built on
// that: so the host's call is answered with the session's device, and the second engine runs as a title switch does.
//
// Only the host's own call, only once its engine has stopped presenting (a call made while the game draws is some
// other part of the host, which must keep its own device), and only when the device answers what was asked.
//
// Two things keep a failure from repeating. A hand-over that never reached a game frame (the host crashed, or was
// closed at a frozen picture) stands the next launch's down, once. And one engine change hands the device to one
// thread: another thread asking in the same quiet is another part of the host, and two users of one immediate context
// would corrupt each other's state.
enum class DeviceShare : uint8_t
{
    Share = 0,
    SwitchedOff,         // reclaimer_share_device = 0
    LastAttemptFailed,   // the last launch's hand-over never reached a game frame
    AnotherThreadHasIt,  // already handed over in this quiet, to another thread
    NotAsked,            // no device wanted: a capability probe
    NotHostsCall,        // another module's call (a runtime, an overlay)
    NoSession,           // the first device: the session will be made on it
    SessionLost,         // the session's device was removed
    GameStillPresenting, // not the end of an engine
    OtherAdapter,        // another GPU was asked for
    FlagsDiffer,         // creation flags the session's device lacks
    FeatureLevelRefused, // none of the requested levels is the session device's
};

// How long the game has not presented before a host's new device can be the next engine's.
constexpr uint64_t kEngineGoneQuietMs = 250;

constexpr DeviceShare DecideDeviceShare(bool wantsDevice, bool hostsCall, bool session, bool sessionLost,
                                        uint64_t msSinceGamePresent, bool sameAdapter, uint32_t requestedFlags,
                                        uint32_t sessionFlags, bool featureLevelAccepted, bool enabled = true,
                                        bool lastAttemptFailed = false, bool anotherThreadHasIt = false)
{
    if (!wantsDevice)
        return DeviceShare::NotAsked;
    if (!enabled)
        return DeviceShare::SwitchedOff;
    if (!hostsCall)
        return DeviceShare::NotHostsCall;
    if (!session)
        return DeviceShare::NoSession;
    if (sessionLost)
        return DeviceShare::SessionLost;
    if (msSinceGamePresent < kEngineGoneQuietMs)
        return DeviceShare::GameStillPresenting;
    if (!sameAdapter)
        return DeviceShare::OtherAdapter;
    if (requestedFlags & ~sessionFlags)
        return DeviceShare::FlagsDiffer;
    if (!featureLevelAccepted)
        return DeviceShare::FeatureLevelRefused;
    if (lastAttemptFailed)
        return DeviceShare::LastAttemptFailed;
    if (anotherThreadHasIt)
        return DeviceShare::AnotherThreadHasIt;
    return DeviceShare::Share;
}

constexpr const char* Describe(DeviceShare decision)
{
    switch (decision)
    {
    case DeviceShare::Share: return "answered with the headset session's device";
    case DeviceShare::SwitchedOff: return "switched off (reclaimer_share_device = 0)";
    case DeviceShare::LastAttemptFailed:
        return "the last launch's hand-over never reached a game frame, so not this launch";
    case DeviceShare::AnotherThreadHasIt: return "already handed to another thread for this engine";
    case DeviceShare::NotAsked: return "no device wanted (a probe)";
    case DeviceShare::NotHostsCall: return "not the host's own call";
    case DeviceShare::NoSession: return "no headset session yet";
    case DeviceShare::SessionLost: return "the session's device is lost";
    case DeviceShare::GameStillPresenting: return "the game is still presenting, so this is not its next engine";
    case DeviceShare::OtherAdapter: return "another GPU was asked for";
    case DeviceShare::FlagsDiffer: return "creation flags the session's device lacks";
    case DeviceShare::FeatureLevelRefused: return "none of the requested feature levels is the session device's";
    }
    return "unknown";
}
} // namespace present_owner
