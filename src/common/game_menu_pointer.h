#pragma once
#include "runtime_types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game_menu_pointer {
// The right stick as a mouse wheel over what the pointer is on: whole notches, faster the further it is pushed, from
// the time it was held. Positive is up, away from the player.
struct StickWheel
{
    float pending = 0.0f;
    uint32_t lastMs = 0;
    int Step(float stick, uint32_t now) noexcept
    {
        constexpr float kDeadzone = 0.25f, kNotchesPerSecond = 10.0f;
        const uint32_t elapsed = lastMs ? std::min<uint32_t>(now - lastMs, 100u) : 0u;
        lastMs = now;
        if (!std::isfinite(stick) || std::fabs(stick) < kDeadzone)
        {
            pending = 0.0f;
            return 0;
        }
        // The first touch moves one notch at once, so a flick always scrolls.
        if (pending == 0.0f)
            pending = stick > 0.0f ? 1.0f : -1.0f;
        else
            pending += stick * kNotchesPerSecond * float(elapsed) * 0.001f;
        const int notches = int(pending);
        pending -= float(notches);
        if (pending == 0.0f)
            pending = stick > 0.0f ? 1e-4f : -1e-4f;
        return notches;
    }
    void Reset() noexcept { pending = 0.0f; lastMs = 0; }
};

inline bool MenuMode(RuntimeMode mode, bool pauseScreen) noexcept
{
    // MCC's shell preloads all six modules. The camera-owner resolver calls
    // that Unsupported; it is not evidence that the displayed menu is absent.
    // Callers additionally require an actual stock-screen submission.
    return mode == RuntimeMode::Shell || mode == RuntimeMode::Unsupported ||
        mode == RuntimeMode::Paused || pauseScreen;
}

// One atomic word carries coordinates, button and freshness together. A missed
// ray is inactive; it cannot press at the last valid cursor location.
inline uint64_t Pack(uint32_t now, bool active, float u, float v, bool pressed) noexcept
{
    active = active && std::isfinite(u) && std::isfinite(v) &&
        u >= 0 && u <= 1 && v >= 0 && v <= 1;
    const uint32_t x = active ? uint32_t(u * 32767.0f) : 0;
    const uint32_t y = active ? uint32_t(v * 32767.0f) : 0;
    return (uint64_t(now) << 32) | x | (y << 15) |
        (uint32_t(pressed) << 30) | (uint32_t(active) << 31);
}
inline bool FreshActive(uint64_t packet, uint32_t now) noexcept
{
    return (packet & (1ull << 31)) && uint32_t(now - uint32_t(packet >> 32)) <= 200;
}

// A trigger pull moves the hand: measured in play, a click's press and release landed up to 35 pixels apart on a
// 3840-wide window, and buttons took several clicks. From the first of the pull the point holds where it was aimed,
// until the trigger is let go. A hand that goes on moving means a drag (a slider, a scrollbar), and then it follows.
class ClickLock {
    bool locked = false, dragging = false;
    float u = 0, v = 0;
public:
    static constexpr float kPullStarted = 0.15f, kDragDistance = 0.03f;
    void Reset() noexcept { locked = dragging = false; }
    void Apply(float trigger, float& pointU, float& pointV) noexcept
    {
        if (trigger < kPullStarted) { Reset(); return; }
        if (!locked) { locked = true; u = pointU; v = pointV; }
        const float du = pointU - u, dv = pointV - v;
        dragging = dragging || du*du + dv*dv > kDragDistance*kDragDistance;
        if (!dragging) { pointU = u; pointV = v; }
    }
};

struct Result { bool move = false, pressed = false; int x = 0, y = 0; };
class Pointer {
    bool armed = false, hadHit = false;
    float u = 0, v = 0;
public:
    void Reset() noexcept { armed = hadHit = false; }
    Result Update(uint64_t packet, uint32_t now, bool allowed, int width, int height) noexcept
    {
        if (!allowed || !FreshActive(packet, now) || width <= 0 || height <= 0) {
            Reset(); return {};
        }
        const float nextU = float(packet & 32767) / 32767.0f;
        const float nextV = float((packet >> 15) & 32767) / 32767.0f;
        u = hadHit ? u + (nextU-u)*0.35f : nextU;
        v = hadHit ? v + (nextV-v)*0.35f : nextV;
        hadHit = true;
        const bool trigger = (packet & (1ull << 30)) != 0;
        if (!trigger) armed = true;
        return {true, armed && trigger,
            std::clamp(int(u*width), 0, width-1),
            std::clamp(int(v*height), 0, height-1)};
    }
};
}
