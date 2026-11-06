// The right stick scrolling the in-headset settings menu, fed to Dear ImGui the way Menu_Render feeds it: the
// pointer's position, a trigger change, then the wheel. The hand never holds perfectly still, so the pointer moves a
// pixel or two every frame.
#include "game_menu_pointer.h"
#include <imgui.h>
#include <imgui_internal.h>
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
    void Frame(float x, float y, bool pressed, float wheel)
    {
        auto& io = ImGui::GetIO();
        static bool previousPressed = false;
        io.AddMousePosEvent(x, y);
        if (pressed != previousPressed) { io.AddMouseButtonEvent(0, pressed); previousPressed = pressed; }
        if (wheel != 0.0f) io.AddMouseWheelEvent(0.0f, wheel);
        ImGui::NewFrame();
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
        scrollY = ImGui::GetScrollY();
        ImGui::End();
        ImGui::Render();
    }
    static int Queued() { return ImGui::GetCurrentContext()->InputEventsQueue.Size; }
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
    uint32_t now = 1000;
    auto tick = [&] { now += 11; };

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
        // The stick moves the menu in whole notches, as on the game's own menus.
        game_menu_pointer::StickWheel wheel;
        const float before = menu.scrollY;
        menu.Frame(300, 200, false, float(wheel.Step(-1.0f, now)));
        menu.Frame(300, 200, false, 0);
        Check(menu.scrollY > before, "a push scrolls the menu at once");
        int worst = 0;
        float scrolled = menu.scrollY;
        for (int i = 0; i < twoSeconds; ++i)
        {
            tick();
            menu.Frame(300.0f + float(i % 3), 200, false, float(wheel.Step(-1.0f, now)));
            if (Menu::Queued() > worst) worst = Menu::Queued();
        }
        Check(worst <= 1, "held, nothing builds up behind the pointer");
        Check(menu.scrollY > scrolled, "held, the menu keeps scrolling");

        // Let go and point at a setting: the pointer is there on the next frame, and a pull clicks it.
        tick();
        menu.Frame(100, 50, false, float(wheel.Step(0.0f, now)));
        Check(io.MousePos.x == 100 && io.MousePos.y == 50, "after scrolling the pointer follows the hand at once");
        Check(menu.rowHovered, "and the setting under it is hovered");
        const float stopped = menu.scrollY;
        menu.Frame(100, 50, true, 0);
        Check(io.MouseDown[0], "a trigger pull right after scrolling clicks");
        menu.Frame(100, 50, false, 0);
        Check(menu.scrollY == stopped, "letting go of the stick stops the scroll");
    }
    {
        // Pushed a little, it scrolls slower than pushed fully.
        game_menu_pointer::StickWheel full, half;
        int fullNotches = 0, halfNotches = 0;
        uint32_t t = 5000;
        for (int i = 0; i < twoSeconds; ++i)
        {
            t += 11;
            fullNotches += full.Step(1.0f, t);
            halfNotches += half.Step(0.5f, t);
        }
        Check(halfNotches > 0 && halfNotches < fullNotches, "the further the stick is pushed, the faster it scrolls");
    }
    ImGui::DestroyContext();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
