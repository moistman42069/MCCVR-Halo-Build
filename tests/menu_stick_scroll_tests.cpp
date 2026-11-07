// The right stick scrolling the in-headset settings menu, fed to Dear ImGui the way Menu_Render feeds it: the
// pointer's position, a trigger change, then the stick's scroll straight after NewFrame. The hand never holds
// perfectly still, so the pointer moves a pixel or two every frame.
#include "menu_stick_scroll.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

static int checks = 0, failures = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

struct Menu
{
    float scrollY = 0;
    bool rowHovered = false;
    menu_stick_scroll::Scroller scroller;
    float lastStep = 0;
    void Frame(float x, float y, bool pressed, float wheel, float stick = 0.0f)
    {
        auto& io = ImGui::GetIO();
        static bool previousPressed = false;
        io.AddMousePosEvent(x, y);
        if (pressed != previousPressed) { io.AddMouseButtonEvent(0, pressed); previousPressed = pressed; }
        if (wheel != 0.0f) io.AddMouseWheelEvent(0.0f, wheel);
        ImGui::NewFrame();
        if (stick != 0.0f) scroller.Apply(stick, io.DeltaTime); else scroller.Reset();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(600, 400));
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings);
        rowHovered = false;
        for (int i = 0; i < 400; ++i)
        {
            ImGui::PushID(i);
            ImGui::Selectable("A setting");
            rowHovered = rowHovered || ImGui::IsItemHovered();
            ImGui::PopID();
        }
        lastStep = ImGui::GetScrollY() - scrollY;
        scrollY = ImGui::GetScrollY();
        ImGui::End();
        ImGui::Render();
    }
    static int Queued() { return ImGui::GetCurrentContext()->InputEventsQueue.Size; }
    float Max() const { return ImGui::FindWindowByName("Settings")->ScrollMax.y; }
};

int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 900);
    io.DeltaTime = 1.0f / 90;
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Menu menu;
    const int twoSeconds = 180;

    {
        // What went wrong: a fraction of a notch every frame. ImGui takes the move and the wheel turn on separate
        // frames, so a backlog grows for as long as the stick is held, and the pointer lags behind the hand.
        menu.Frame(300, 200, false, 0);
        for (int i = 0; i < twoSeconds; ++i)
            menu.Frame(300.0f + float(i % 3), 200, false, -0.12f);
        Check(Menu::Queued() > 60, "ImGui still splits a move and a wheel turn queued in the same frame");
        menu.Frame(100, 50, false, 0);
        Check(io.MousePos.x != 100 || io.MousePos.y != 50, "so the per-frame fraction left the pointer behind");
        for (int i = 0; i < 400 && Menu::Queued() > 0; ++i)
            menu.Frame(100, 50, false, 0);
    }
    {
        // The stick scrolls the panel under the pointer a little every frame, straight to its scroll.
        const float before = menu.scrollY;
        menu.Frame(300, 200, false, 0, -1.0f);
        menu.Frame(300, 200, false, 0, -1.0f);
        Check(menu.scrollY > before, "a push scrolls the menu at once");
        int worst = 0;
        float smallest = 1e9f, largest = 0;
        for (int i = 0; i < twoSeconds; ++i)
        {
            menu.Frame(300.0f + float(i % 3), 200, false, 0, -1.0f);
            if (Menu::Queued() > worst) worst = Menu::Queued();
            if (menu.scrollY < menu.Max()) { smallest = std::min(smallest, menu.lastStep); largest = std::max(largest, menu.lastStep); }
        }
        Check(worst <= 1, "held, nothing builds up behind the pointer");
        Check(smallest > 0 && largest - smallest <= 1.0f, "held, the menu moves every frame by an even few pixels: smooth");

        // Let go and point at a setting: the pointer is there on the next frame, and a pull clicks it.
        menu.Frame(100, 50, false, 0, 0.0f);
        Check(io.MousePos.x == 100 && io.MousePos.y == 50, "after scrolling the pointer follows the hand at once");
        Check(menu.rowHovered, "and the setting under it is hovered");
        const float stopped = menu.scrollY;
        menu.Frame(100, 50, true, 0);
        Check(io.MouseDown[0], "a trigger pull right after scrolling clicks");
        menu.Frame(100, 50, false, 0);
        Check(menu.scrollY == stopped, "letting go of the stick stops the scroll");

        // Up scrolls back up.
        menu.Frame(300, 200, false, 0, 1.0f);
        menu.Frame(300, 200, false, 0, 1.0f);
        Check(menu.scrollY < stopped, "pushed up, the menu scrolls back up");
    }
    {
        // Pushed a little, it scrolls slower than pushed fully; inside the deadzone not at all.
        const float second = 1.0f;
        Check(menu_stick_scroll::Pixels(0.1f, second) == 0.0f, "a stick resting off centre scrolls nothing");
        const float half = std::fabs(menu_stick_scroll::Pixels(-0.5f, second));
        const float full = std::fabs(menu_stick_scroll::Pixels(-1.0f, second));
        Check(half > 0 && half < full && full <= menu_stick_scroll::kFastest * menu_stick_scroll::kLongestFrame + 1,
              "the further the stick is pushed, the faster it scrolls, and a stalled frame is held to a short step");
        Check(menu_stick_scroll::Pixels(0.6f, 0.01f) > 0 && menu_stick_scroll::Pixels(-0.6f, 0.01f) < 0,
              "up scrolls up, down scrolls down");
    }
    ImGui::DestroyContext();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
