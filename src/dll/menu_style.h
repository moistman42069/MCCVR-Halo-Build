#pragma once
#include <imgui.h>

namespace vr_menu_style {
    // Launcher-matched navy panels with cyan highlights. Applied once at init,
    // immediately after StyleColorsDark seeds every slot, so anything not named
    // here still has a sane value.
    constexpr ImVec4 Rgb(unsigned hex, float alpha = 1.0f)
    {
        return ImVec4(((hex >> 16) & 0xFF) / 255.0f,
                      ((hex >> 8) & 0xFF) / 255.0f,
                      (hex & 0xFF) / 255.0f,
                      alpha);
    }

    // Match the launcher's navy panels, pale text and cyan rules.
    constexpr unsigned kPanelBg     = 0x071019;
    constexpr unsigned kSurfaceBg   = 0x0F202C;
    constexpr unsigned kTroughBg    = 0x111D25;
    constexpr unsigned kRaised      = 0x183442;
    constexpr unsigned kRaisedHover = 0x244755;
    constexpr unsigned kSelected    = 0x215563;
    constexpr unsigned kTextMain    = 0xD9EDEF;
    constexpr unsigned kTextDim     = 0x8FAEBA;
    constexpr unsigned kAccent      = 0x6EE5E8;
    constexpr unsigned kAccentHot   = 0xA5F6F7;
    constexpr unsigned kAccentDown  = 0x277079;
    constexpr unsigned kBorder      = 0x233A43;
    constexpr unsigned kSeparator   = 0x234B5B;
    // Keep warnings distinct from cyan interaction highlights.
    constexpr unsigned kWarning     = 0xFF6B3D;

    inline void ApplyTheme()
    {
        ImGuiStyle& s = ImGui::GetStyle();
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]            = Rgb(kPanelBg, 0.96f);
        c[ImGuiCol_ChildBg]             = Rgb(kSurfaceBg, 1.00f);
        c[ImGuiCol_PopupBg]             = Rgb(kSurfaceBg, 0.98f);
        c[ImGuiCol_Text]                = Rgb(kTextMain);
        c[ImGuiCol_TextDisabled]        = Rgb(kTextDim);
        c[ImGuiCol_Border]              = Rgb(kBorder);
        c[ImGuiCol_BorderShadow]        = Rgb(0x000000, 0.0f);
        c[ImGuiCol_Separator]           = Rgb(kSeparator);
        c[ImGuiCol_SeparatorHovered]    = Rgb(kAccent, 0.60f);
        c[ImGuiCol_SeparatorActive]     = Rgb(kAccent);
        c[ImGuiCol_FrameBg]             = Rgb(kTroughBg);
        c[ImGuiCol_FrameBgHovered]      = Rgb(kRaisedHover);
        c[ImGuiCol_FrameBgActive]       = Rgb(kSelected);
        c[ImGuiCol_TitleBg]             = Rgb(kSurfaceBg);
        c[ImGuiCol_TitleBgActive]       = Rgb(kSelected);
        c[ImGuiCol_TitleBgCollapsed]    = Rgb(kSurfaceBg);
        c[ImGuiCol_MenuBarBg]           = Rgb(kSurfaceBg);
        c[ImGuiCol_Button]              = Rgb(kRaised);
        c[ImGuiCol_ButtonHovered]       = Rgb(kRaisedHover);
        c[ImGuiCol_ButtonActive]        = Rgb(kAccentDown);
        c[ImGuiCol_Header]              = Rgb(kSelected);
        c[ImGuiCol_HeaderHovered]       = Rgb(kRaisedHover);
        c[ImGuiCol_HeaderActive]        = Rgb(kAccentDown, 0.85f);
        c[ImGuiCol_CheckMark]           = Rgb(kAccent);
        c[ImGuiCol_SliderGrab]          = Rgb(kAccent);
        c[ImGuiCol_SliderGrabActive]    = Rgb(kAccentHot);
        c[ImGuiCol_ScrollbarBg]         = Rgb(kPanelBg, 0.0f);
        c[ImGuiCol_ScrollbarGrab]       = Rgb(kSeparator);
        c[ImGuiCol_ScrollbarGrabHovered]= Rgb(kRaisedHover);
        c[ImGuiCol_ScrollbarGrabActive] = Rgb(kAccent);
        c[ImGuiCol_Tab]                 = Rgb(kRaised);
        c[ImGuiCol_TabHovered]          = Rgb(kRaisedHover);
        c[ImGuiCol_TabSelected]         = Rgb(kSelected);
        c[ImGuiCol_ResizeGrip]          = Rgb(kPanelBg, 0.0f); // panel is locked
        c[ImGuiCol_ResizeGripHovered]   = Rgb(kPanelBg, 0.0f);
        c[ImGuiCol_ResizeGripActive]    = Rgb(kPanelBg, 0.0f);
        c[ImGuiCol_NavCursor]           = Rgb(kAccent);

        // Square, industrial edges suit the armour look and stay crisp when the
        // panel is sampled at an angle in the headset.
        s.WindowRounding = 0.0f;
        s.ChildRounding = 0.0f;
        s.FrameRounding = 2.0f;
        s.GrabRounding = 2.0f;
        s.TabRounding = 0.0f;
        s.ScrollbarRounding = 2.0f;
        s.WindowBorderSize = 0.0f;
        s.FrameBorderSize = 1.0f;
    }

}
