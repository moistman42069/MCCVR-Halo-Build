#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

// THE RIGHT STICK SCROLLING THE F1 MENU. The panel under the pointer scrolls by a few pixels every frame, as far as the
// stick is pushed: smooth, and as quick as the stick. It goes straight to that panel's scroll, not through Dear ImGui's
// wheel input. Fed through the input queue, the wheel turns took frames of their own beside the pointer's moves. A
// little every frame then built a backlog that held the pointer and the scroll behind. Whole notches avoided the
// backlog but scrolled in steps. The desktop's mouse wheel still goes through ImGui as notches.
namespace menu_stick_scroll
{
inline constexpr float kDeadzone = 0.2f;       // of the stick's travel
inline constexpr float kSlowest = 150.0f;      // pixels a second just past the deadzone
inline constexpr float kFastest = 1500.0f;     // pixels a second at full push
inline constexpr float kLongestFrame = 0.05f;  // a stalled frame scrolls no further than this

// Pixels to scroll for a stick push (up positive: the content moves down) held for `seconds`. The speed grows with the
// square of the push past the deadzone, so a small push is fine control and a full one is quick.
inline float Pixels(float stick, float seconds) noexcept
{
    if (!std::isfinite(stick) || !std::isfinite(seconds) || seconds <= 0.0f) return 0.0f;
    const float push = std::fabs(stick);
    if (push <= kDeadzone) return 0.0f;
    const float t = std::min(1.0f, (push - kDeadzone) / (1.0f - kDeadzone));
    const float speed = kSlowest + (kFastest - kSlowest) * t * t;
    return std::copysign(speed * std::min(seconds, kLongestFrame), stick);
}

// The panel the wheel would scroll: the hovered window, or the nearest one holding it that can scroll.
inline ImGuiWindow* Target(ImGuiWindow* window) noexcept
{
    while (window && (window->Flags & ImGuiWindowFlags_ChildWindow) &&
           (window->ScrollMax.y <= 0.0f || (window->Flags & ImGuiWindowFlags_NoScrollWithMouse)))
        window = window->ParentWindow;
    if (!window || window->ScrollMax.y <= 0.0f || (window->Flags & ImGuiWindowFlags_NoScrollWithMouse)) return nullptr;
    return window;
}

// One frame's scroll, after ImGui::NewFrame (which finds the hovered window). The panel scrolls in whole pixels and
// keeps the fraction for the next frame, so a slow push still moves it evenly.
struct Scroller
{
    float carried = 0.0f;
    ImGuiWindow* last = nullptr;
    bool Apply(float stick, float seconds)
    {
        ImGuiWindow* window = Target(GImGui ? GImGui->HoveredWindow : nullptr);
        const float pixels = Pixels(stick, seconds);
        if (!window || pixels == 0.0f) { Reset(); return false; }
        if (window != last) carried = 0.0f;
        last = window;
        const float total = pixels + carried;
        const float whole = std::trunc(total);
        carried = total - whole;
        if (whole == 0.0f) return true;
        ImGui::SetScrollY(window, std::clamp(window->Scroll.y - whole, 0.0f, window->ScrollMax.y));
        return true;
    }
    void Reset() { carried = 0.0f; last = nullptr; }
};
}
