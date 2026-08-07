#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include "menu.h"
#include "vr.h"
#include "game.h"
#include "title_adapter.h"
#include "d3d_state.h"
#include "d3d11_hook.h"
#include "../common/log.h"
#include "../common/config.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    bool g_ready = false;
    std::atomic<bool> g_open{false};
    HWND g_hwnd = nullptr;
    WNDPROC g_origWndProc = nullptr;
    ID3D11DeviceContext* g_ctx = nullptr;
    ID3D11Texture2D* g_tex = nullptr;
    ID3D11RenderTargetView* g_rtv = nullptr;
    struct VrPointerInput
    {
        bool hit = false;
        bool pressed = false;
        float u = 0.0f;
        float v = 0.0f;
        float scrollY = 0.0f;
    };
    VrPointerInput g_vrPointer;
    bool g_resetArmed = false; // "reset all settings" needs a second click
    // Panel drag state. The panel is otherwise completely locked; the grab
    // handle along the top edge is the only thing that moves it. vr.cpp owns the
    // drag itself because it holds the controller ray -- we only tell it whether
    // the pointer is on the handle, and it tells us when a drag is running so
    // the bar can light up.
    std::atomic<bool> g_pointerOverGrabHandle{false};
    std::atomic<bool> g_panelDragging{false};

    // Master Chief green with orange visor highlights. Applied once at init,
    // immediately after StyleColorsDark seeds every slot, so anything not named
    // here still has a sane value.
    constexpr ImVec4 Rgb(unsigned hex, float alpha = 1.0f)
    {
        return ImVec4(((hex >> 16) & 0xFF) / 255.0f,
                      ((hex >> 8) & 0xFF) / 255.0f,
                      (hex & 0xFF) / 255.0f,
                      alpha);
    }

    constexpr unsigned kPanelBg     = 0x10160F; // near-black armour green
    constexpr unsigned kSurfaceBg   = 0x161D14; // sidebar / child panels
    constexpr unsigned kTroughBg    = 0x1E2A1B; // slider + input backgrounds
    constexpr unsigned kRaised      = 0x26331F; // buttons at rest
    constexpr unsigned kRaisedHover = 0x3A4E30;
    constexpr unsigned kSelected    = 0x2A3A22; // selected row
    constexpr unsigned kTextMain    = 0xE4EBDF;
    constexpr unsigned kTextDim     = 0x7E8C76;
    constexpr unsigned kAccent      = 0xFFA22B; // visor orange
    constexpr unsigned kAccentHot   = 0xFFBC5E;
    constexpr unsigned kAccentDown  = 0xE08312;
    constexpr unsigned kBorder      = 0x2C3A26;
    constexpr unsigned kSeparator   = 0x35472E;
    // Deliberately a different orange from the accent so a warning can never be
    // mistaken for a highlight.
    constexpr unsigned kWarning     = 0xFF6B3D;

    void ApplyTheme()
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

    // Height of the grab bar in menu-texture pixels. The hover test is ImGui's
    // own item rectangle, so the bar you can see is exactly the bar you can grab.
    constexpr float kGrabHandleHeight = 46.0f;

    // Sidebar categories, in the order they are listed. Replaces the seven flat
    // tabs: the tab strip had already run out of room, and the two big tabs were
    // long enough to need scrolling. A category is a plain row in a list, so
    // adding a settings area later costs one enum entry and one row.
    // ===================================================================
    //  THE WELCOME MESSAGE -- the author's note to players. This is the
    //  whole message; edit the lines below and nothing else.
    //
    //  One line here is one line on screen. Lines still soft-wrap if the
    //  panel is made narrow with the grab handle, so a long sentence stays
    //  readable rather than being cut off.
    // ===================================================================
    constexpr const char* kWelcomeMessage =
        "Hi everyone, pancreations here!\n"
        "Thank you for taking the time to get this up and running.\n"
        "If you have any issues please reach out and let me know.\n"
        "Expect the rest of the collection to be added over time";

    constexpr const char* kWelcomeCloseHint =
        "Press L3+R3 (both analog sticks) to recenter and close the menu";

    enum MenuCategory
    {
        Cat_Welcome = 0,
        Cat_Status,
        Cat_Comfort,
        Cat_Theatre,
        Cat_Controls,
        Cat_Vehicles,
        Cat_WeaponAim,
        Cat_Crosshair,
        Cat_BodyHands,
        Cat_Picture,
        Cat_Hud,
        Cat_Desktop,
        Cat_Scope,
        Cat_Advanced,
        Cat_Count
    };

    struct CategoryRow
    {
        const char* label;
        const char* blurb; // one line, shown at the top of the settings pane
    };

    // Kept in the same order as MenuCategory; a static assert below stops the
    // two from drifting apart.
    constexpr CategoryRow kCategories[Cat_Count] = {
        {"Welcome",       "Read me first."},
        {"Status",        "What the mod is doing right now, and the switches you reach for mid-game."},
        {"Comfort",       "The flat screen you see in menus, and how head motion feels."},
        {"3D Theatre",    "A room-fixed stereo screen used only when the game locks the cinematic camera."},
        {"Controls",      "Turning, gestures, and controller vibration."},
        {"Vehicles",      "First-person driving: sit in the seat instead of floating behind the vehicle."},
        {"Weapon & Aim",  "Where the gun sits in your hand, and two-handed aiming."},
        {"Crosshair",     "The floating reticle that shows where the weapon really shoots."},
        {"Body & Hands",  "Arms, shoulders, and how much of Chief you can see."},
        {"Picture",       "Render resolution, sharpening, anti-aliasing and brightness."},
        {"HUD",           "Size, shape and position of the game's own HUD."},
        {"Desktop",       "The window on your monitor, not the headset."},
        {"Scope",         "Experimental gun-mounted zoom screen."},
        {"Advanced",      "Tracking calibration, panel placement, and starting over."},
    };
    static_assert(sizeof(kCategories) / sizeof(kCategories[0]) == Cat_Count,
                  "kCategories must list exactly one row per MenuCategory");

    int g_activeCategory = Cat_Status;
    constexpr float kSidebarWidth = 300.0f;

    // The one bar that moves the panel. Everything else is inert, which is the
    // whole point: before this, the settings window itself was a movable ImGui
    // window floating inside a transparent quad, so grabbing near its title bar
    // slid it around and it read as "two windows in one".
    void DrawGrabHandle()
    {
        const bool dragging = g_panelDragging.load(std::memory_order_acquire);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 topLeft = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 bottomRight(topLeft.x + width, topLeft.y + kGrabHandleHeight);

        ImGui::InvisibleButton("##panelgrab", ImVec2(width, kGrabHandleHeight));
        const bool hovered = ImGui::IsItemHovered();
        g_pointerOverGrabHandle.store(hovered || dragging, std::memory_order_release);

        draw->AddRectFilled(topLeft, bottomRight,
                            ImGui::GetColorU32(dragging  ? Rgb(kAccentDown, 0.55f)
                                               : hovered ? Rgb(kRaisedHover)
                                                         : Rgb(kSurfaceBg)));
        // A bright rule along the bottom edge reads as a seam you can pick up.
        draw->AddRectFilled(ImVec2(topLeft.x, bottomRight.y - 2.0f), bottomRight,
                            ImGui::GetColorU32(Rgb(kAccent, dragging ? 1.0f : 0.75f)));

        // Grip dots, centered, so the bar looks draggable without any text.
        const float midY = topLeft.y + kGrabHandleHeight * 0.5f;
        const float midX = topLeft.x + width * 0.5f;
        const ImU32 gripColor = ImGui::GetColorU32(Rgb(kAccent, dragging ? 1.0f : 0.8f));
        for (int i = -3; i <= 3; ++i)
            draw->AddCircleFilled(ImVec2(midX + i * 14.0f, midY), 2.5f, gripColor);

        ImGui::SetCursorScreenPos(ImVec2(topLeft.x + 12.0f, topLeft.y + 10.0f));
        ImGui::TextColored(Rgb(kTextMain), "HALO MCC VR");
        ImGui::SetCursorScreenPos(ImVec2(topLeft.x, bottomRight.y + 6.0f));
        ImGui::TextDisabled("%s", dragging
            ? "Moving the panel. Right stick up/down changes the distance; release to place it."
            : "Hold the right trigger on this bar to move the whole panel.");
        ImGui::Separator();
    }
    // ImGui gets input on the game's window thread but draws on its render
    // thread; this lock keeps the two from touching ImGui at the same time.
    CRITICAL_SECTION g_cs;

    // Private message we post to the game window so the fit runs on the window's
    // own (UI) thread, where touching window size/position is safe.
    constexpr UINT kFitGameWindowMsg = WM_APP + 0x37;

    // Fit the game window inside the primary monitor's work area, preserving the
    // render aspect (kNativeRenderWidth:kNativeRenderHeight, constant because
    // resolution_scale is uniform) so the downscaled desktop picture isn't
    // distorted. This shrinks only the VISIBLE window; MCC keeps drawing the
    // full-size frame into the forced full-size backbuffer (d3d11_hook.cpp), so
    // the headset picture and the gun alignment are unchanged. Runs on the UI
    // thread.
    void FitGameWindow(HWND hwnd)
    {
        // MCC's decorated window path keeps Slate in a different geometry from
        // the DXGI-stretched client when the render is larger than the monitor.
        // That is the path where native shell/pause hit-testing and controller
        // focus become unusable. Use the game's borderless-window geometry for
        // the fitted client so MCC has one client rectangle for both display and
        // native menu input.
        const LONG_PTR oldStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR borderlessStyle =
            (oldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                          WS_MAXIMIZEBOX | WS_SYSMENU)) |
            WS_POPUP;
        const LONG_PTR oldExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const LONG_PTR borderlessExStyle =
            oldExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE |
                           WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, borderlessStyle);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, borderlessExStyle);

        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfo(mon, &mi))
            return;
        const int workW = mi.rcWork.right - mi.rcWork.left;
        const int workH = mi.rcWork.bottom - mi.rcWork.top;
        if (workW <= 0 || workH <= 0)
            return;
        const float aspect = (float)kNativeRenderWidth / (float)kNativeRenderHeight;
        int w = workW;
        int h = (int)((float)w / aspect + 0.5f);
        if (h > workH)
        {
            h = workH;
            w = (int)((float)h * aspect + 0.5f);
        }
        const int x = mi.rcWork.left + (workW - w) / 2;
        const int y = mi.rcWork.top + (workH - h) / 2;
        if (!SetWindowPos(hwnd, nullptr, x, y, w, h,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        {
            LOG("fit: borderless SetWindowPos failed (%lu)",
                static_cast<unsigned long>(GetLastError()));
            return;
        }
        RECT client{};
        if (GetClientRect(hwnd, &client))
        {
            LOG("fit: native MCC window is borderless; client %ldx%ld at (%d,%d)",
                client.right - client.left, client.bottom - client.top, x, y);
        }
    }

    LRESULT CALLBACK WndProcHook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        // Fit request (posted from Menu_Init) -- run it here on the UI thread.
        if (msg == kFitGameWindowMsg)
        {
            FitGameWindow(hwnd);
            return 0;
        }
        // Keep the game rendering and processing input while the user is in the
        // headset. Looking through the headset hands desktop focus to SteamVR,
        // and MCC (like most games) stops drawing and ignores input when it
        // isn't the focused window — which showed up as a frozen VR screen. We
        // tell the game it is always the active, foreground window.
        switch (msg)
        {
        case WM_ACTIVATEAPP:
            wp = TRUE;
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
                wp = MAKEWPARAM(WA_ACTIVE, 0);
            break;
        case WM_NCACTIVATE:
            // TRUE keeps the window drawn/treated as active.
            return CallWindowProcW(g_origWndProc, hwnd, msg, TRUE, lp);
        case WM_KILLFOCUS:
            // Don't let the game hear that it lost keyboard focus.
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;
        case WM_WINDOWPOSCHANGING:
            if (D3D_FitActive())
            {
                // MCC believes its client is full-size (we tell it so), so it may
                // try to size the WINDOW to match and grow it back off-screen.
                // Clamp any oversize request to a monitor fit. Genuine size
                // changes only (user drags with SWP_NOSIZE are left alone).
                WINDOWPOS* pos = (WINDOWPOS*)lp;
                if (pos && !(pos->flags & SWP_NOSIZE))
                {
                    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                    MONITORINFO mi{sizeof(mi)};
                    if (GetMonitorInfo(mon, &mi))
                    {
                        const int workW = mi.rcWork.right - mi.rcWork.left;
                        const int workH = mi.rcWork.bottom - mi.rcWork.top;
                        if (workW > 0 && workH > 0 && (pos->cx > workW || pos->cy > workH))
                        {
                            const float aspect =
                                (float)kNativeRenderWidth / (float)kNativeRenderHeight;
                            int w = workW;
                            int h = (int)((float)w / aspect + 0.5f);
                            if (h > workH) { h = workH; w = (int)((float)h * aspect + 0.5f); }
                            pos->cx = w;
                            pos->cy = h;
                            pos->x = mi.rcWork.left + (workW - w) / 2;
                            pos->y = mi.rcWork.top + (workH - h) / 2;
                            pos->flags &= ~SWP_NOMOVE;
                        }
                    }
                }
            }
            break;
        case WM_SIZE:
            if (D3D_FitActive())
            {
                // The window is smaller than the render. Tell MCC its client is
                // still the full render size so it keeps drawing the full frame
                // into the (forced full-size) backbuffer instead of shrinking to
                // the corner -- the black-border crop. The GPU downscales the
                // full frame into the small window on present. Bracket the call
                // so our GetClientRect hook returns full ONLY to MCC's resize
                // code on this stack (never to DXGI's present-time query).
                unsigned fw = 0, fh = 0;
                D3D_GetForcedRenderSize(fw, fh);
                if (fw && fh)
                {
                    const LPARAM full = MAKELPARAM((WORD)fw, (WORD)fh);
                    D3D_SetForcedClientLie(true);
                    const LRESULT r = CallWindowProcW(g_origWndProc, hwnd, msg, wp, full);
                    D3D_SetForcedClientLie(false);
                    return r;
                }
            }
            break;
        }

        // Hotkeys act on plain WM_KEYDOWN only. F10 is the one exception:
        // Windows delivers it as WM_SYSKEYDOWN even without Alt held. All
        // other Alt combos must reach the game untouched — SteamVR's
        // exit/dashboard flow sends Alt+F4 (WM_SYSKEYDOWN + VK_F4), and when
        // F4 was a hotkey that phantom press flipped the yaw sign, inverting
        // head-turn and hand-aim ("controls completely broken", two sessions
        // in a row ~50 ms after the session lost focus). The F4/F5/F7
        // calibration flips now live in the F1 menu instead of on keys.
        const bool keyDown = msg == WM_KEYDOWN && !(lp & (1 << 30)); // no auto-repeat
        const bool sysKeyDown = msg == WM_SYSKEYDOWN && !(lp & (1 << 30));
        if (sysKeyDown && wp == VK_F4)
            LOG("Alt+F4 received; passing it to the game (close request)");
        if (keyDown || (sysKeyDown && wp == VK_F10))
        {
            switch (wp)
            {
            case VK_F1: Menu_Toggle(); return 0;
            case VK_F2: Game_ToggleHeadTracking(); return 0;
            case VK_F3: Game_Recenter(); return 0;
            case VK_F6: Game_TogglePositional(); return 0;
            case VK_F8: Game_PitchTrim(-1); return 0;
            case VK_F9: Game_PitchTrim(+1); return 0;
            case VK_F10: VR_ToggleScreenFollow(); return 0;
            case VK_F11:
                if (Game_CanToggleImmersiveView()) VR_ToggleStereo();
                return 0;
            case VK_PRIOR: Game_LeanScale(+1); return 0; // Page Up
            case VK_NEXT:  Game_LeanScale(-1); return 0; // Page Down
            case VK_HOME: Game_GunScale(+1); return 0; // bigger hand-held weapon
            case VK_END:  Game_GunScale(-1); return 0; // smaller hand-held weapon
            case VK_INSERT: Game_ToggleVrAim(); return 0; // controller steers aim
            }
        }
        if (g_ready && g_open)
        {
            EnterCriticalSection(&g_cs);
            if (msg == WM_MOUSEMOVE)
            {
                // The menu texture is a fixed size; map the window-space mouse
                // position onto it.
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const float cw = (float)(rc.right > 0 ? rc.right : 1);
                const float ch = (float)(rc.bottom > 0 ? rc.bottom : 1);
                ImGui::GetIO().AddMousePosEvent(GET_X_LPARAM(lp) * MENU_W / cw,
                                                GET_Y_LPARAM(lp) * MENU_H / ch);
            }
            else
            {
                ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
            }
            LeaveCriticalSection(&g_cs);
            // Swallow mouse/keyboard so clicking the menu doesn't also fire
            // the player's weapon. (Raw input still reaches the game in M0.)
            // Never swallow Alt+F4: closing the game must always work.
            if (((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                 (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) &&
                !(msg == WM_SYSKEYDOWN && wp == VK_F4))
                return 0;
        }
        // Keep native mouse messages in MCC's stock physical client domain. The
        // prior fit scaled only this path while its separately polled cursor stayed
        // physical. This candidate tests whether restoring one coordinate domain
        // stops mouse hover from displacing keyboard/controller menu focus.
        return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
    }

    void DrawUI()
    {
        // The window fills the whole panel texture and is completely locked.
        // Cond_Always (not FirstUseEver) means ImGui cannot keep a position of
        // its own, and NoMove/NoResize/NoTitleBar remove every drag target. The
        // panel background now covers the entire quad, so there is no
        // transparent surround for a smaller window to slide around inside.
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(MENU_W, MENU_H), ImGuiCond_Always);
        ImGui::Begin("HaloMCCVR Settings", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoSavedSettings);

        DrawGrabHandle();

        bool changed = false;

        // Leave room for the footer line below both columns.
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() +
            ImGui::GetStyle().ItemSpacing.y * 2.0f + 8.0f;

        ImGui::BeginChild("##sidebar", ImVec2(kSidebarWidth, -footerHeight), ImGuiChildFlags_Borders);
        for (int i = 0; i < Cat_Count; ++i)
        {
            const bool selected = g_activeCategory == i;
            if (ImGui::Selectable(kCategories[i].label, selected))
                g_activeCategory = i;
            // An orange bar down the left edge of the selected row, so the
            // current category reads at a glance from across the panel.
            if (selected)
            {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    min, ImVec2(min.x + 4.0f, max.y),
                    ImGui::GetColorU32(Rgb(kAccent)));
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##pane", ImVec2(0, -footerHeight), ImGuiChildFlags_Borders);
        ImGui::TextColored(Rgb(kAccent), "%s", kCategories[g_activeCategory].label);
        ImGui::TextDisabled("%s", kCategories[g_activeCategory].blurb);
        ImGui::Separator();
        ImGui::Spacing();

        if (g_activeCategory == Cat_Welcome)
        {
        bool dontShow = !g_config.show_welcome;
        if (ImGui::Checkbox("Never show again", &dontShow))
        {
            g_config.show_welcome = !dontShow;
            changed = true;
        }
        changed |= ImGui::Checkbox(
            "Stereo 3D Theatre for cutscenes",
            &g_config.cutscene_theater_enabled);
        ImGui::TextDisabled(
            "On by default. Player-controlled cameras always stay immersive.\n"
            "Open 3D Theatre in the left column for depth and screen controls.");
        ImGui::Spacing();
        ImGui::TextUnformatted(kWelcomeCloseHint);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(kWelcomeMessage);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        }

        if (g_activeCategory == Cat_Status)
        {
        VrStatus st{};
        VR_GetStatus(st);
        ImGui::Text("Runtime: %s", st.runtime);
        ImGui::Text("Session: %s   |   Game: %ux%u @ %.0f fps", st.sessionState, st.gameWidth,
                    st.gameHeight, st.fps);
        const TitleDescriptor* title = TitleAdapter_GetActive();
        ImGui::Text("Title: %s   |   Mode: %s",
                    title ? title->displayName : "MCC shell",
                    RuntimeModeName(TitleAdapter_GetRuntimeMode()));
        if (ImGui::Button("Re-center headset and screen"))
        {
            Game_Recenter();
        }
        ImGui::SameLine();
        if (ImGui::Button(VR_IsPausePresentation()
                ? "Resume game (Start)"
                : "Pause game in 2D (Start)"))
        {
            Input_RequestPauseToggle();
            Menu_Toggle();
        }
        ImGui::TextDisabled("PSVR2 fallback: press Y+B together to Pause/Resume.");
        ImGui::Separator();
        const bool geometryOnly = Game_IsStereoGeometryOnlyBringup();
        const bool headTrackingApplied = Game_IsHeadTrackingApplied();
        ImGui::BeginDisabled(
            !Game_CanToggleImmersiveView() || geometryOnly);
        if (ImGui::Button(geometryOnly
                ? "Head tracking pending (C-H4-8)"
                : (headTrackingApplied
                    ? "Turn head tracking OFF (F2)"
                    : "Turn head tracking ON (F2)")))
        {
            Game_ToggleHeadTracking();
        }
        ImGui::SameLine();
        if (ImGui::Button(VR_IsStereoEnabled()
                ? "Turn stereo 3D OFF (F11)"
                : "Turn stereo 3D ON (F11)"))
        {
            VR_ToggleStereo();
        }
        ImGui::EndDisabled();
        ImGui::Text("Head tracking: %s   |   Stereo rendering: %s   |   View: %s",
                    geometryOnly ? "NOT IN C-H4-7"
                                 : (headTrackingApplied ? "ON" : "OFF"),
                    VR_IsStereoEnabled() ? "ON" : "OFF",
                    VR_IsPausePresentation() ? "head-locked 2D" : "immersive 3D");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("L3+R3 recenters and closes this menu; F1 only closes it.");
        }

        if (g_activeCategory == Cat_Comfort)
        {
        ImGui::Text("Virtual screen");
        // These two used to stop at 10 m even though the config file accepts 20,
        // so a value typed into halomccvr.cfg could not be reached or restored
        // from the menu. The slider now spans the file's own range.
        changed |= ImGui::SliderFloat("Screen width (m)", &g_config.screen_width_m, 0.5f, 20.0f, "%.1f");
        changed |= ImGui::SliderFloat("Screen distance (m)", &g_config.screen_distance_m, 0.3f, 20.0f, "%.1f");
        ImGui::TextDisabled("The flat screen the game is shown on in menus and 2D mode.\n"
                            "This is NOT the F1 panel; that one lives under Advanced.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Head motion");
        float headsetSmoothPercent = g_config.headset_smoothing * 100.0f;
        if (ImGui::SliderFloat("Headset micro-smoothing", &headsetSmoothPercent,
                               0.0f, 10.0f, "%.0f%%", ImGuiSliderFlags_None))
        {
            g_config.headset_smoothing = headsetSmoothPercent / 100.0f;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Raw (0%)##headset"))
        {
            g_config.headset_smoothing = 0.0f;
            changed = true;
        }
        ImGui::TextDisabled("Raw by default. Try 5%% only for micro-jitter; capped at 10%% for comfort.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Startup");
        changed |= ImGui::Checkbox("Auto-enter VR on level load", &g_config.auto_vr);
        ImGui::TextDisabled("Turns head tracking + stereo on when a level starts and off in the menu.");
        }

        if (g_activeCategory == Cat_Theatre)
        {
        changed |= ImGui::Checkbox(
            "Enable Stereo 3D Theatre for cutscenes",
            &g_config.cutscene_theater_enabled);
        ImGui::TextDisabled(
            "Applies to every supported game, including future additions.\n"
            "Only an engine-confirmed cinematic with no player camera control qualifies.\n"
            "Scripted gameplay, vehicles, death cameras, loading and pause stay immersive.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Stereo depth");
        float depthPercent = g_config.cutscene_theater_depth * 100.0f;
        if (ImGui::SliderFloat(
                "Depth", &depthPercent, 0.0f, 200.0f, "%.0f%%"))
        {
            g_config.cutscene_theater_depth = depthPercent / 100.0f;
            changed = true;
        }
        changed |= ImGui::Checkbox(
            "Flip Depth (swap left and right eyes)",
            &g_config.cutscene_theater_flip_depth);
        ImGui::TextDisabled(
            "0%% is flat, 100%% uses your headset's natural eye spacing,\n"
            "and 200%% doubles the separation. Flip only if depth looks reversed.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Room-fixed screen");
        changed |= ImGui::SliderFloat(
            "Screen width (m)##theatre",
            &g_config.cutscene_theater_width_m, 0.5f, 20.0f, "%.1f");
        changed |= ImGui::SliderFloat(
            "Screen distance (m)##theatre",
            &g_config.cutscene_theater_distance_m, 0.3f, 20.0f, "%.1f");
        ImGui::TextDisabled(
            "Defaults: 6.0 m wide at 4.0 m away. Height follows the authored\n"
            "cinematic projection.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Black cine bars");
        bool matteOn = g_config.cutscene_theater_matte_aspect > 0.0f;
        if (ImGui::Checkbox("Show cine bars", &matteOn))
        {
            g_config.cutscene_theater_matte_aspect = matteOn ? 16.0f / 9.0f : 0.0f;
            changed = true;
        }
        if (matteOn)
        {
            changed |= ImGui::SliderFloat(
                "Picture shape##theatre",
                &g_config.cutscene_theater_matte_aspect, 1.0f, 3.0f, "%.2f:1");
            changed |= ImGui::SliderFloat(
                "Slide picture##theatre",
                &g_config.cutscene_theater_matte_offset, -0.25f, 0.25f, "%.2f");
        }
        ImGui::TextDisabled(
            "The cutscene is drawn into the headset's shape, which is taller than a TV,\n"
            "so without bars you see scene above and below the intended shot.\n"
            "1.78 is a TV picture, 2.39 a wide cinema crop. The picture is never\n"
            "resized, only covered. Slide moves what stays showing up or down.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Subtitles");
        changed |= ImGui::Checkbox(
            "Show subtitles on the theatre screen",
            &g_config.cutscene_theater_subtitles);
        ImGui::TextDisabled(
            "The game draws subtitles after it hands us the two eye images, so they\n"
            "reach your monitor but not your headset. This puts the text back on the\n"
            "screen where the game placed it. MCC's own subtitle setting must be on.");
        if (g_config.cutscene_theater_subtitles)
        {
            changed |= ImGui::SliderFloat(
                "Search height##theatre",
                &g_config.cutscene_theater_subtitle_band, 0.05f, 1.0f, "%.2f");
            ImGui::TextDisabled(
                "How much of the bottom of the game's frame is searched for the text.\n"
                "Raise it if no subtitles appear: text drawn higher is never found.");
            changed |= ImGui::Checkbox(
                "Diagnostic: show the strip unfiltered",
                &g_config.cutscene_theater_subtitle_debug);
            ImGui::TextDisabled(
                "Paints the captured strip straight onto the screen with no text\n"
                "picking at all, so you can see exactly what the mod is reading.\n"
                "Turn it off for normal play.");
        }
        }

        if (g_activeCategory == Cat_Controls)
        {
        ImGui::Text("VR turning (right controller stick)");
        if (ImGui::RadioButton("Snap turn", !g_config.turn_smooth))
        {
            g_config.turn_smooth = false;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Smooth turn", g_config.turn_smooth))
        {
            g_config.turn_smooth = true;
            changed = true;
        }
        if (g_config.turn_smooth)
            changed |= ImGui::SliderFloat("Turn speed (deg/s)", &g_config.turn_smooth_deg_s, 30.0f, 360.0f, "%.0f");
        else
            changed |= ImGui::SliderFloat("Snap increment (deg)", &g_config.turn_snap_deg, 5.0f, 90.0f, "%.0f");

        ImGui::Spacing();
        changed |= ImGui::Checkbox("Y + B sends Start (pause / resume)",
                                   &g_config.y_b_start_chord);
        ImGui::TextDisabled("On by default. Turn it off if you prefer Y and B to stay separate.");

        ImGui::Spacing();
        ImGui::Text("D-pad gesture (hold controller next to head)");
        if (ImGui::RadioButton("Left controller", g_config.dpad_hand == 0))
        {
            g_config.dpad_hand = 0;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Right controller", g_config.dpad_hand == 1))
        {
            g_config.dpad_hand = 1;
            changed = true;
        }
        float hapticPercent = g_config.haptic_intensity * 100.0f;
        if (ImGui::SliderFloat("Controller vibration", &hapticPercent,
                               0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_None))
        {
            g_config.haptic_intensity = hapticPercent / 100.0f;
            if (g_config.haptic_intensity <= 0.0f)
                VR_SetGameHaptics(0.0f);
            changed = true;
        }
        ImGui::TextDisabled("L3+R3 recenters and toggles this menu; the right trigger clicks the VR pointer.");
        }

        if (g_activeCategory == Cat_Vehicles)
        {
        ImGui::Text("First-person vehicle camera");
        changed |= ImGui::Checkbox("Sit in the seat (first person)",
                                   &g_config.vehicle_first_person);
        ImGui::TextDisabled(
            "OFF restores the stock behind-the-vehicle view instantly.");
        ImGui::Spacing();
        // C19: the same three sliders always, bound to whichever SEAT is under
        // you — driver, passenger and gunner of the same vehicle are all
        // independent. Seated, they edit THAT seat's own trim (created on
        // first touch, saved to the config, reloaded whenever you sit there
        // again); on foot they edit the universal trim every unadjusted seat
        // follows.
        // The binding is STICKY: the slot read is a fail-closed snapshot that
        // can blip to -1 for a frame (stale/torn sampler read), and rebinding
        // on that frame would silently rewrite the universal trim mid-drag.
        // Only a sustained -1 (a real exit) rebinds to the universal pair.
        static int s_seatBind = -1;
        static VehicleTrimBank s_seatBank = VehicleTrimBank::Halo3;
        static int s_seatMissFrames = 0;
        VehicleTrimBank liveBank = VehicleTrimBank::Halo3;
        const int liveSlot = Game_VehicleSeatTrimSlotEx(&liveBank);
        if (liveSlot >= 0)
        {
            s_seatBind = liveSlot;
            s_seatBank = liveBank;
            s_seatMissFrames = 0;
        }
        else if (++s_seatMissFrames > 30)
            s_seatBind = -1;
        const int seatSlot = s_seatBind;
        int seatSlotLimit = kVehicleTrimSlots;
        float* trimForwardV = g_config.vehicle_cam_forward_v;
        float* trimUpV = g_config.vehicle_cam_up_v;
        float* trimRightV = g_config.vehicle_cam_right_v;
        bool* trimForwardSet = g_config.vehicle_cam_forward_set;
        bool* trimUpSet = g_config.vehicle_cam_up_set;
        bool* trimRightSet = g_config.vehicle_cam_right_set;
        if (s_seatBank == VehicleTrimBank::Odst)
        {
            seatSlotLimit = kOdstVehicleTrimSlots;
            trimForwardV = g_config.odst_vehicle_cam_forward_v;
            trimUpV = g_config.odst_vehicle_cam_up_v;
            trimRightV = g_config.odst_vehicle_cam_right_v;
            trimForwardSet = g_config.odst_vehicle_cam_forward_set;
            trimUpSet = g_config.odst_vehicle_cam_up_set;
            trimRightSet = g_config.odst_vehicle_cam_right_set;
        }
        else if (s_seatBank == VehicleTrimBank::Reach)
        {
            seatSlotLimit = kReachVehicleTrimSlots;
            trimForwardV = g_config.reach_vehicle_cam_forward_v;
            trimUpV = g_config.reach_vehicle_cam_up_v;
            trimRightV = g_config.reach_vehicle_cam_right_v;
            trimForwardSet = g_config.reach_vehicle_cam_forward_set;
            trimUpSet = g_config.reach_vehicle_cam_up_set;
            trimRightSet = g_config.reach_vehicle_cam_right_set;
        }
        const bool perSeat = seatSlot >= 0 && seatSlot < seatSlotLimit;
        const bool reachSeat = perSeat && s_seatBank == VehicleTrimBank::Reach;
        // R-V25: every slider is Halo 3's slider. A Reach seat may legitimately
        // sit tens of metres from its marker (the authored Sabre eye is 42 m
        // ahead of it), and exposing that whole span made one pixel of travel
        // worth about a third of a metre - "ridiculously strong". The stored
        // Reach clamps stay wide so no authored row is truncated; the slider
        // hangs Halo 3's own travel off the seat's authored Blender base
        // instead, so dragging feels identical in all three titles.
        float reachBaseForward = 0.0f;
        float reachBaseUp = 0.0f;
        float reachBaseRight = 0.0f;
        if (reachSeat)
        {
            ConfigReachSeatAuthoredBase(seatSlot, &reachBaseForward,
                                        &reachBaseUp, &reachBaseRight);
        }
        const float forwardMin = reachBaseForward + kVehicleCamForwardMin;
        const float forwardMax = reachBaseForward + kVehicleCamForwardMax;
        const float upMin = reachBaseUp + kVehicleCamUpMin;
        const float upMax = reachBaseUp + kVehicleCamUpMax;
        const float rightMin = reachBaseRight + kVehicleCamRightMin;
        const float rightMax = reachBaseRight + kVehicleCamRightMax;
        if (perSeat)
            ImGui::Text("Adjusting: %s (this seat only)",
                        Game_VehicleSeatTrimName(seatSlot, s_seatBank));
        else
            ImGui::Text("Adjusting: every seat (universal trim)");
        float seatFwd = ConfigSeatCamForward(g_config, seatSlot);
        if (s_seatBank == VehicleTrimBank::Odst)
            seatFwd = ConfigOdstSeatCamForward(g_config, seatSlot);
        else if (s_seatBank == VehicleTrimBank::Reach)
            seatFwd = ConfigReachSeatCamForward(g_config, seatSlot);
        if (ImGui::SliderFloat("Seat forward (m)", &seatFwd,
                               forwardMin, forwardMax, "%.2f"))
        {
            if (perSeat)
            {
                if (reachSeat)
                    ConfigReachSeatBeginTrimEdit(g_config, seatSlot);
                trimForwardV[seatSlot] = seatFwd;
                trimForwardSet[seatSlot] = true;
            }
            else
                g_config.vehicle_cam_forward_m = seatFwd;
            changed = true;
        }
        float seatUp = ConfigSeatCamUp(g_config, seatSlot);
        if (s_seatBank == VehicleTrimBank::Odst)
            seatUp = ConfigOdstSeatCamUp(g_config, seatSlot);
        else if (s_seatBank == VehicleTrimBank::Reach)
            seatUp = ConfigReachSeatCamUp(g_config, seatSlot);
        if (ImGui::SliderFloat("Seat height (m)", &seatUp,
                               upMin, upMax, "%.2f"))
        {
            if (perSeat)
            {
                if (reachSeat)
                    ConfigReachSeatBeginTrimEdit(g_config, seatSlot);
                trimUpV[seatSlot] = seatUp;
                trimUpSet[seatSlot] = true;
            }
            else
                g_config.vehicle_cam_up_m = seatUp;
            changed = true;
        }
        float seatRight = ConfigSeatCamRight(g_config, seatSlot);
        if (s_seatBank == VehicleTrimBank::Odst)
            seatRight = ConfigOdstSeatCamRight(g_config, seatSlot);
        else if (s_seatBank == VehicleTrimBank::Reach)
            seatRight = ConfigReachSeatCamRight(g_config, seatSlot);
        if (ImGui::SliderFloat("Seat left / right (m)", &seatRight,
                               rightMin, rightMax, "%.2f"))
        {
            if (perSeat)
            {
                if (reachSeat)
                    ConfigReachSeatBeginTrimEdit(g_config, seatSlot);
                trimRightV[seatSlot] = seatRight;
                trimRightSet[seatSlot] = true;
            }
            else
                g_config.vehicle_cam_right_m = seatRight;
            changed = true;
        }
        if (perSeat && (trimForwardSet[seatSlot] || trimUpSet[seatSlot] ||
                        trimRightSet[seatSlot]))
        {
            if (ImGui::SmallButton(reachSeat
                    ? "Back to this seat's authored point##seattrim"
                    : "Back to the universal trim##seattrim"))
            {
                if (reachSeat)
                    ConfigReachSeatUseUniversalTrim(g_config, seatSlot);
                else
                {
                    trimForwardSet[seatSlot] = false;
                    trimUpSet[seatSlot] = false;
                    trimRightSet[seatSlot] = false;
                }
                changed = true;
            }
        }
        // The universal trim is shared by all three titles, so a bad value in
        // it moves every seat that has no line of its own. One click puts it
        // back to the shipped default instead of hunting for it on a slider.
        if (!perSeat &&
            (g_config.vehicle_cam_forward_m != kVehicleCamForwardDefault ||
             g_config.vehicle_cam_up_m != kVehicleCamUpDefault ||
             g_config.vehicle_cam_right_m != kVehicleCamRightDefault))
        {
            if (ImGui::SmallButton("Reset the universal trim##seattrim"))
            {
                g_config.vehicle_cam_forward_m = kVehicleCamForwardDefault;
                g_config.vehicle_cam_up_m = kVehicleCamUpDefault;
                g_config.vehicle_cam_right_m = kVehicleCamRightDefault;
                changed = true;
            }
        }
        ImGui::TextDisabled(
            "Sit in a seat and these sliders adjust that seat alone —\n"
            "a vehicle's driver, passengers and gunner each remember their\n"
            "own. On foot they set the shared base every unadjusted seat\n"
            "follows. In Reach the base is your Blender-authored point for\n"
            "that seat and the slider travels the same distance either side\n"
            "of it that Halo 3 and ODST travel; vehicle and occupant motion\n"
            "are parented around that baseline.\n"
            "Left/right: negative moves left, positive moves right.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Turning with the vehicle");
        changed |= ImGui::Checkbox("View follows the vehicle",
                                   &g_config.vehicle_view_follow);
        ImGui::TextDisabled(
            "ON follows ground-vehicle yaw/pitch; aircraft stay yaw-only.\n"
            "Roll stays stable. OFF is world-locked (default).");
        changed |= ImGui::Checkbox("Match the rendered vehicle motion",
                                   &g_config.vehicle_cam_smoothing);
        ImGui::TextDisabled(
            "ON (default) uses the exact seat/attachment node Halo renders\n"
            "and adds occupant bounce relative to the settled seat pose. It\n"
            "does not filter or shift your authored point. OFF is raw-node A/B.");
        changed |= ImGui::Checkbox("Hide my character in the seat",
                                   &g_config.vehicle_hide_body);
        ImGui::TextDisabled(
            "ON (default) hides only your own seated world character from\n"
            "your first-person camera. Other cameras still see it, and your\n"
            "first-person arms and weapon remain. Game files are untouched.\n"
            "It applies with View follows the vehicle both OFF and ON.");
        changed |= ImGui::Checkbox("Arms ride the seat, not my head",
                                   &g_config.vehicle_hands_follow_body);
        ImGui::TextDisabled(
            "ON (default) hangs your arms and gun off the seat, so they stay\n"
            "with the vehicle and your hands while your head turns freely.\n"
            "OFF anchors them to Halo's seated camera - your character's own\n"
            "head - so looking around drags the gun with your face.");
        changed |= ImGui::SliderFloat("Seat bounce", &g_config.vehicle_bounce,
                                      0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled(
            "How much of the seat's bounce reaches your view. 1 is the\n"
            "engine's full travel (about 24 cm) and is a lot; 0 bolts your\n"
            "view to the seat. Strength only - nothing is smoothed or\n"
            "delayed at any setting.");
        changed |= ImGui::Checkbox("Re-centre when I get in and out",
                                   &g_config.vehicle_recenter_on_seat);
        changed |= ImGui::Checkbox("Steady brightness in the seat (ODST)",
                                   &g_config.vehicle_steady_exposure);
        ImGui::TextDisabled(
            "Looking down at the dashboard fills the game's brightness\n"
            "meter with a big dark surface, so its auto-exposure ramps the\n"
            "whole scene. This holds it steady while you are seated.");
        ImGui::TextDisabled(
            "ON (default) re-centres your play space once you have settled\n"
            "into a seat and again when you get out, so a step you took on\n"
            "foot is not carried in (or back out) as a standing lean.\n"
            "Position only - it never turns your view.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Motion steering");
        changed |= ImGui::Checkbox("Virtual steering wheel",
                                   &g_config.vehicle_motion);
        ImGui::TextDisabled(
            "Warthog, Mongoose, Ghost, Prowler and Chopper: DOUBLE-CLICK both\n"
            "grips to take an invisible wheel, then just hold your hands on it\n"
            "and tilt - no squeezing. Double-click again to let go and the\n"
            "right stick steers. A single right grip still gets you out of the\n"
            "vehicle either way. Aircraft and the Scorpion/Wraith keep their\n"
            "own controls.");
        changed |= ImGui::SliderFloat("Full lock at (deg)",
            &g_config.vehicle_wheel_max_deg, 30.0f, 180.0f, "%.0f");
        changed |= ImGui::SliderFloat("Wheel deadzone (deg)",
            &g_config.vehicle_wheel_deadzone_deg, 0.0f, 30.0f, "%.0f");
        }

        if (g_activeCategory == Cat_WeaponAim)
        {
        ImGui::Text("Hand-held weapon");
        changed |= ImGui::SliderFloat("Weapon size", &g_config.gun_scale, 0.3f, 3.0f, "%.2fx");
        ImGui::TextDisabled("Uniform scale of RIGHT hand + weapon about your grip (Home/End in-game).");
        changed |= ImGui::SliderFloat("Left hand size", &g_config.left_hand_scale,
                                      0.3f, 3.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("Match weapon##lhs"))
        { g_config.left_hand_scale = g_config.gun_scale; changed = true; }
        ImGui::TextDisabled("Sizes the left hand, and the second gun when dual-wielding.\n"
                            "1.00 = authored size. Separate because the left hand is\n"
                            "usually empty; use Match weapon for identical hands.\n"
                            "Its front/back position is \"Left hand forward offset\" below.");
        changed |= ImGui::SliderFloat("Weapon pitch (deg)", &g_config.gun_pitch_deg, -180.0f, 180.0f, "%.0f");
        changed |= ImGui::SliderFloat("Weapon yaw (deg)", &g_config.gun_yaw_deg, -180.0f, 180.0f, "%.0f");
        changed |= ImGui::SliderFloat("Weapon roll (deg)", &g_config.gun_roll_deg, -180.0f, 180.0f, "%.0f");
        ImGui::TextDisabled("Pitch, yaw, and roll rotate on their matching local gun axes.");
        ImGui::TextDisabled("0/0/0 keeps the current automatic barrel alignment.");
        changed |= ImGui::SliderFloat("Gun forward offset (m)", &g_config.gun_forward_m, -0.3f, 0.5f, "%.2f");
        ImGui::TextDisabled("Slides gun/arms along your aim. Negative seats the gun back in your fist.");
        changed |= ImGui::SliderFloat("Muzzle height (m)", &g_config.muzzle_height_m, -0.3f, 0.3f, "%.2f");
        ImGui::TextDisabled("HALO REACH ONLY for now - Halo 3 and ODST support is coming soon.");
        ImGui::TextDisabled("Raises the muzzle flash / bullet spawn up the gun's own axis.");
        ImGui::TextDisabled("Where rounds LAND is unchanged. 0.11 is about four inches.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Two-handed aiming");
        changed |= ImGui::Checkbox("Two-handed aiming", &g_config.two_handed_aim);
        ImGui::SameLine();
        ImGui::TextDisabled(VR_IsTwoHandAiming() ? "[engaged]" : "[one-handed]");
        if (g_config.two_handed_aim)
        {
            ImGui::Indent();
            if (ImGui::RadioButton("Toggle (click grip)", g_config.two_hand_toggle))
            { g_config.two_hand_toggle = true; changed = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Hold grip", !g_config.two_hand_toggle))
            { g_config.two_hand_toggle = false; changed = true; }
            changed |= ImGui::SliderFloat("Left hand forward offset (m)",
                                          &g_config.left_hand_forward_m,
                                          -0.15f, 0.30f, "%.3f");
            ImGui::TextDisabled("Moves the visible support hand and the two-hand aim point together.");
            changed |= ImGui::SliderFloat("Grab zone side offset (m)",
                                          &g_config.two_hand_zone_right_m,
                                          -0.10f, 0.10f, "%.3f");
            ImGui::TextDisabled("Slides the grip-click zone sideways (+ = right) onto the visible barrel.");
            changed |= ImGui::SliderFloat("Left palm depth (m)",
                                          &g_config.left_grip_forward_m,
                                          -0.05f, 0.25f, "%.3f");
            ImGui::TextDisabled("Extends the two-hand grab line and grip-click zone to your visible palm.");
            ImGui::Unindent();
        }
        ImGui::TextDisabled("Put your left hand on the front of the gun, click/hold the LEFT GRIP.\n"
                            "Engages only when your hand is on the barrel line.");
        }

        if (g_activeCategory == Cat_Crosshair)
        {
        ImGui::Text("Authored weapon crosshair (stereo)");
        changed |= ImGui::Checkbox("Show a crosshair where the weapon shoots", &g_config.crosshair);
        if (g_config.crosshair)
        {
            float crosshairSmoothPercent = g_config.aim_stabilization * 100.0f;
            if (ImGui::SliderFloat("Crosshair smoothing", &crosshairSmoothPercent,
                                   0.0f, 95.0f, "%.0f%%", ImGuiSliderFlags_None))
            {
                g_config.aim_stabilization = crosshairSmoothPercent / 100.0f;
                changed = true;
            }
            changed |= ImGui::SliderFloat("Crosshair size (deg)", &g_config.crosshair_size_deg,
                                          0.3f, 20.0f, "%.1f");
            changed |= ImGui::SliderFloat("Crosshair distance (m)", &g_config.crosshair_distance_m,
                                          2.0f, 50.0f, "%.0f");
            // Halo 3's crosshair kicks on fire and turns red/green on a target.
            // Reading it back costs render time, so the rate is the player's
            // call: drag it live and keep whatever still feels smooth.
            bool crosshairAnimates = g_config.crosshair_animation_frames != 0;
            if (ImGui::Checkbox("Animate the crosshair (Halo 3)", &crosshairAnimates))
            {
                g_config.crosshair_animation_frames = crosshairAnimates ? 6 : 0;
                changed = true;
            }
            if (crosshairAnimates)
            {
                ImGui::Indent();
                changed |= ImGui::SliderInt(
                    "Refresh every N frames", &g_config.crosshair_animation_frames,
                    6, 60, "%d");
                ImGui::TextDisabled("Lower = smoother shooting animation and faster\n"
                                    "red/green target colors. Higher = cheaper.\n"
                                    "Turn it off if you feel any frame-rate cost.");
                ImGui::Unindent();
            }
            ImGui::TextDisabled("Uses the equipped weapon's authored crosshair and target colors.");
        }
        ImGui::TextDisabled("Crosshair smoothing is visual only; bullets keep the current controller ray.\n"
                            "Set it to 0%% for exact raw tracking.");
        }

        if (g_activeCategory == Cat_Scope)
        {
        ImGui::Text("Experimental gun-mounted zoom screen");
        if (ImGui::Checkbox("Enable experimental R3 zoom screen", &g_config.scope_enabled))
        {
            changed = true;
            if (!g_config.scope_enabled)
                VR_SetScopeActive(false);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(VR_IsScopeActive() ? "[R3: visible]" : "[R3: hidden]");
        ImGui::TextDisabled("R3 toggles a fixed-magnification view while the main headset view\n"
                            "stays wide. Placement and zoom are experimental per-user tuning.");
        if (g_config.scope_enabled)
        {
            ImGui::Indent();
            changed |= ImGui::SliderFloat("Default scope zoom", &g_config.scope_zoom,
                                          6.0f, 24.0f, "%.2fx");
            changed |= ImGui::SliderFloat("Screen width (m)##scope",
                                          &g_config.scope_screen_width_m,
                                          0.04f, 0.25f, "%.3f");
            changed |= ImGui::SliderFloat("Screen right offset (m)",
                                          &g_config.scope_screen_right_m,
                                          -0.30f, 0.30f, "%.3f");
            changed |= ImGui::SliderFloat("Screen up offset (m)",
                                          &g_config.scope_screen_up_m,
                                          -0.20f, 0.30f, "%.3f");
            changed |= ImGui::SliderFloat("Screen forward offset (m)",
                                          &g_config.scope_screen_forward_m,
                                          0.05f, 0.80f, "%.3f");
            changed |= ImGui::SliderInt("Image refresh divisor",
                                        &g_config.scope_refresh_divisor, 1, 4);
            ImGui::TextDisabled("Offsets are direct gun-local meters with no hidden added distance.");
            ImGui::TextDisabled("Higher refresh divisors render the zoom image less often; the screen\n"
                                "still follows the gun every frame. Use 4 for the lowest GPU cost.");
            ImGui::Unindent();
        }
        }

        if (g_activeCategory == Cat_BodyHands)
        {
        ImGui::Text("Body (VRIK)");
        changed |= ImGui::Checkbox("Arm IK (bend arm to controller)", &g_config.arm_ik);
        ImGui::TextDisabled("ON: shoulder stays, elbow bends, hand+gun follow your controller.\n"
                            "OFF: the whole arm rigid-parents to the controller (old behavior).");
        if (g_config.arm_ik)
        {
            changed |= ImGui::SliderFloat("Right shoulder drop", &g_config.right_shoulder_drop,
                                          0.0f, 0.3f, "%.3f");
            ImGui::TextDisabled("Lowers Chief's right arm so it doesn't clip your face.\n"
                                "Raise until the right shoulder matches your left.");
            changed |= ImGui::Checkbox("Level shoulders (don't pitch with head)",
                                       &g_config.shoulder_level);
            ImGui::TextDisabled("ON: shoulders stay level with the horizon when you look up/down.\n"
                                "OFF: shoulders ride your head pitch (old). Hand+gun unaffected.");
        }
        ImGui::Spacing();
        changed |= ImGui::Checkbox("Floating hands (hide arms)", &g_config.floating_hands);
        ImGui::TextDisabled("Shows only your hands and the guns they hold; the arms are hidden.\n"
                            "Hands still track your controllers exactly as with full arms.");
        ImGui::Spacing();
        changed |= ImGui::Checkbox("Show body (VRIK stage A1)", &g_config.body_wip);
        ImGui::TextDisabled("Shows Chief's game-animated body via the engine's own director switches.");
        ImGui::TextDisabled("Room-scale unit movement is gated until the player-biped boundary is headset-proven.");
        }

        if (g_activeCategory == Cat_Hud)
        {
        ImGui::Text("HUD layout");
        changed |= ImGui::SliderFloat("HUD size", &g_config.hud_size, 0.30f, 1.00f, "%.2f");
        changed |= ImGui::SliderFloat("HUD width / aspect", &g_config.hud_aspect,
                                      kHudAspectMin, kHudAspectMax, "%.2f");
        changed |= ImGui::SliderFloat("HUD curvature", &g_config.hud_curvature,
                                      kHudCurvatureMin, kHudCurvatureMax, "%.2f");
        changed |= ImGui::SliderFloat("HUD height", &g_config.hud_vertical_offset,
                                      kHudHeightMin, kHudHeightMax, "%+.0f px");
        if (ImGui::SmallButton("Pull HUD in (0.45)##sf"))
        { g_config.hud_size = 0.45f; changed = true; }
        ImGui::SameLine();
        // This used to write 0.87 / 1.0 / 0.5 / 0, none of which are the
        // defaults, so "reset" left the HUD somewhere the config file could not
        // describe and disagreed with the global "Reset ALL settings".
        if (ImGui::SmallButton("Reset HUD layout##sf"))
        {
            const Config hudDefaults{};
            g_config.hud_size = hudDefaults.hud_size;
            g_config.hud_aspect = hudDefaults.hud_aspect;
            g_config.hud_curvature = hudDefaults.hud_curvature;
            g_config.hud_vertical_offset = hudDefaults.hud_vertical_offset;
            changed = true;
        }
        ImGui::TextDisabled("Reset returns the four values above to the shipped defaults\n"
                            "(%.2f / %.2f / %.2f / %+.0f). Lower size pulls HUD elements inward.",
                            Config{}.hud_size, Config{}.hud_aspect,
                            Config{}.hud_curvature, Config{}.hud_vertical_offset);
        ImGui::TextDisabled("Width corrects squeeze separately from size; 1.00 uses automatic correction.");
        ImGui::TextDisabled("Curvature: 0.00 = flat (+0.30), 1.00 = curved (-0.30); 0.50 is authored.");
        ImGui::TextDisabled("Height: positive raises the HUD, negative lowers it; the aiming reticle stays fixed.");
        }

        if (g_activeCategory == Cat_Picture)
        {
        ImGui::Text("Render resolution");
        changed |= ImGui::SliderFloat("Resolution scale", &g_config.resolution_scale,
                                      kResolutionScaleMin, kResolutionScaleMax, "%.2fx");
        // Same even-rounding the launcher applies, so this is the exact render
        // size the next launch will ask MCC for.
        auto scaleEven = [](int base, float scale) {
            int value = (int)lroundf((float)base * scale);
            return (value & 1) ? value + 1 : value;
        };
        ImGui::TextDisabled("Renders %d x %d.", scaleEven(kNativeRenderWidth, g_config.resolution_scale),
                            scaleEven(kNativeRenderHeight, g_config.resolution_scale));
        struct ResolutionPreset { const char* name; float scale; };
        // Tiers span the full 0.35..2.75 range. "Keith David" is true 8K width
        // (7680, scale ~2.64); "Ultra" sits at ~5k, the heavy threshold below.
        // All uniform, so Halo's 2912:2100 VR aspect is preserved at every tier.
        static const ResolutionPreset kPresets[] = {
            {"Potato", 0.50f}, {"Low", 0.75f}, {"Medium", 1.00f},
            {"High", 1.30f}, {"Ultra", 1.80f}, {"Keith David", 2.64f}
        };
        // Sized from the table, not a literal 6: the old hardcoded count would
        // have silently dropped any tier added below.
        for (int i = 0; i < (int)std::size(kPresets); ++i)
        {
            if (i)
                ImGui::SameLine();
            if (ImGui::SmallButton(kPresets[i].name))
            {
                g_config.resolution_scale = kPresets[i].scale;
                changed = true;
            }
        }
        ImGui::TextDisabled("The buttons are shortcuts; the slider takes any value in between,\n"
                            "as does resolution_scale in halomccvr.cfg. Below 1.00x trades\n"
                            "sharpness for frame rate; above it supersamples. Keith David is\n"
                            "8K-class. Changing this requires a full game restart.");
        if (g_config.resolution_scale > kResolutionScaleHeavy)
            ImGui::TextColored(Rgb(kWarning),
                               "[!] Very heavy (~5K and up): can crash weaker GPUs. Test in\n"
                               "    short sessions and drop this if the game won't start.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Image quality (applies live, every title)");
        const char* upscaleItems[] = {"Linear (old)", "Sharp (strong bicubic)"};
        changed |= ImGui::Combo("Upscale filter", &g_config.upscale_filter,
                                upscaleItems, 2);
        ImGui::TextDisabled("How the game image is scaled to your headset. The game usually\n"
                            "renders BELOW your per-eye headset resolution, so this upscales it.\n"
                            "Sharp keeps edges crisp; Linear is the old soft/shimmery look.");
        changed |= ImGui::SliderFloat("Sharpening", &g_config.sharpness, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("RCAS-based 2x overdrive. 0 = off; 1 = twice the prior maximum.\n"
                            "It uses the same five taps/pass; lower it if the top rings or clips.");
        const char* aaItems[] = {
            "Off", "FXAA", "FXAA Strong", "SMAA 1x", "SMAA 1x + FXAA Strong"};
        changed |= ImGui::Combo("Anti-aliasing", &g_config.aa_mode, aaItems, 5);
        ImGui::TextDisabled("Smooths jagged edges on the finished image, so a mid/low rig doesn't\n"
                            "need a huge render resolution. SMAA 1x is the real 3-stage filter;\n"
                            "the final option adds FXAA Strong for the most aggressive cleanup.\n"
                            "SMAA costs more GPU only when one of its modes is selected.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Scene");
        changed |= ImGui::SliderFloat("Game brightness", &g_config.game_brightness, 0.5f, 2.0f, "%.2f");
        ImGui::TextDisabled("Brightens/darkens the whole game. 1.0 = the game's own brightness.\n"
                            "One setting for Halo 3, ODST and Reach - all three move together.");
        changed |= ImGui::Checkbox("Motion blur", &g_config.motion_blur);
        ImGui::TextDisabled("Off is the VR standard. In stereo the game's blur is fed the wrong\n"
                            "previous frame and smears bright edges into repeating echoes.");
        changed |= ImGui::SliderFloat("Draw distance", &g_config.draw_distance,
                                      kDrawDistanceMin, kDrawDistanceMax, "%.2f");
        ImGui::TextDisabled("1.00 = full stock draw distance. Lower brings the far plane in toward\n"
                            "you, culling distant terrain/objects (skybox goes first). Most levels\n"
                            "only start culling below ~0.25; the lowest settings clip near geometry\n"
                            "(hard pop-in) for the most frames. Live, all three games.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Weather (Halo: Reach honours these today)");
        changed |= ImGui::Checkbox("Rain", &g_config.rain);
        ImGui::TextDisabled("The rain streaks the game draws across your view. Off by default:\n"
                            "in a headset they read as noise over the whole image. Live -- flip\n"
                            "this while it's raining and you'll see it change immediately.");
        changed |= ImGui::Checkbox("Atmospheric fog", &g_config.atmospheric_fog);
        ImGui::TextDisabled("The distance haze that greys out far terrain and flattens contrast.\n"
                            "Off by default and the bigger clarity win of the two. This is the\n"
                            "game's own fog switch, so hazy levels see much further. Live.\n"
                            "A game with no proven switch is left alone and says so in the log.");
        }

        if (g_activeCategory == Cat_Desktop)
        {
        ImGui::Text("The window on your monitor");
        changed |= ImGui::Checkbox("Fit desktop window to my monitor",
                                   &g_config.fit_desktop_window);
        ImGui::TextDisabled(
            "For monitors SMALLER than your render (e.g. a big headset resolution on\n"
            "a 1080p screen), where MCC's window overflows and you can't click the\n"
            "\"Halo 3\" tile or Quit. The headset keeps the full resolution above; only\n"
            "the desktop window shrinks to fit and the GPU downscales into it (no\n"
            "extra render pass, no measurable cost). OFF by default. Takes effect on\n"
            "the next launch -- close MCC and relaunch.");
        }

        if (g_activeCategory == Cat_Advanced)
        {
        changed |= ImGui::Checkbox("Bone probe (diagnostic)", &g_config.weapon_probe);
        ImGui::TextDisabled("Pushes every composed skeleton 1m left to prove writable palette boundaries.");
        changed |= ImGui::Checkbox("Render right eye first (diagnostic)",
                                   &g_config.right_eye_first);

        ImGui::Spacing();
        ImGui::Separator();
        // These flips were the F4/F5/F7 hotkeys. A phantom F4 (SteamVR sends
        // Alt+F4 on its exit path) kept inverting the controls mid-session, so
        // they are reachable only from this menu now.
        ImGui::Text("Tracking calibration  (yaw %+.0f, pitch %+.0f, up-vector %s)",
                    Game_GetYawSign(), Game_GetPitchSign(), Game_GetWriteUp() ? "on" : "off");
        if (ImGui::Button("Flip yaw"))
            Game_FlipYaw();
        ImGui::SameLine();
        if (ImGui::Button("Flip pitch"))
            Game_FlipPitch();
        ImGui::SameLine();
        if (ImGui::Button("Toggle up-vector"))
            Game_ToggleUp();
        ImGui::TextDisabled("If turning your head left turns the view right, click Flip yaw.");

        ImGui::Spacing();
        ImGui::TextDisabled("Insert toggles hand aim. Sense sticks move/turn; grips = bumpers,\ntriggers = fire/grenade, stick-click = zoom/crouch, left menu = Start.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Menu panel");
        ImGui::TextDisabled("Currently %.2f m away, %.2f m wide, %+.2f m high, %+.2f m across.",
                            g_config.menu_distance_m, g_config.menu_width_m,
                            g_config.menu_height_m, g_config.menu_side_m);
        if (ImGui::Button("Reset panel position"))
        {
            const Config defaults{};
            g_config.menu_distance_m = defaults.menu_distance_m;
            g_config.menu_width_m = defaults.menu_width_m;
            g_config.menu_height_m = defaults.menu_height_m;
            g_config.menu_side_m = defaults.menu_side_m;
            changed = true;
        }
        ImGui::SameLine();
        // Clamped here as well as in ConfigSave: the render thread reads these
        // for the composition quad before the save runs.
        if (ImGui::SmallButton("Smaller##panel"))
        {
            g_config.menu_width_m = std::clamp(g_config.menu_width_m - 0.10f,
                                               kMenuWidthMin, kMenuWidthMax);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Bigger##panel"))
        {
            g_config.menu_width_m = std::clamp(g_config.menu_width_m + 0.10f,
                                               kMenuWidthMin, kMenuWidthMax);
            changed = true;
        }
        ImGui::TextDisabled("Grab the bar at the top of this panel with the right trigger to\n"
                            "move it; push the right stick up/down while holding to change\n"
                            "how far away it sits. Your placement is saved automatically.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Start over");
        // Two clicks: a stray VR pointer click here would otherwise wipe a
        // headset-tuned weapon calibration with no undo. Closing the menu
        // disarms it, so a forgotten arm can never fire on the next session.
        if (ImGui::Button(g_resetArmed ? "Click again to confirm reset"
                                       : "Reset ALL settings to defaults"))
        {
            if (g_resetArmed)
            {
                g_config = Config{};
                ConfigSave();
                LOG("config: reset to defaults from the menu");
                g_resetArmed = false;
            }
            else
            {
                g_resetArmed = true;
            }
        }
        if (g_resetArmed)
        {
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                g_resetArmed = false;
        }
        ImGui::TextDisabled("Puts every setting back to the value halomccvr.cfg lists as its\n"
                            "default, including your weapon calibration. Resolution needs a\n"
                            "game restart; everything else applies immediately.");
        }

        ImGui::EndChild(); // ##pane

        static bool dirty = false;
        if (changed)
            dirty = true;
        if (dirty && !ImGui::IsAnyItemActive())
        {
            ConfigSave(); // save once the slider is let go, not every frame
            dirty = false;
        }

        ImGui::Separator();
        ImGui::TextDisabled("L3+R3 recenters and closes this menu; F1 only closes it. Settings save automatically.");
        ImGui::End();
    }
} // namespace

bool Menu_Init(HWND gameWindow, ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT rtFormat)
{
    if (g_ready)
        return true;
    g_hwnd = gameWindow;
    g_ctx = context;
    InitializeCriticalSection(&g_cs);

    D3D11_TEXTURE2D_DESC td{};
    td.Width = MENU_W;
    td.Height = MENU_H;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = rtFormat;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &g_tex)) ||
        FAILED(device->CreateRenderTargetView(g_tex, nullptr, &g_rtv)))
    {
        LOG("menu: render target creation failed");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;              // don't scatter imgui.ini into the game folder
    io.MouseDrawCursor = true;             // draw the cursor into our texture
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ApplyTheme();                          // Master Chief green / visor orange
    ImGui::GetStyle().ScaleAllSizes(1.5f); // legible at panel distance in the headset
    io.FontGlobalScale = 1.5f;

    if (!ImGui_ImplWin32_Init(gameWindow) || !ImGui_ImplDX11_Init(device, context))
    {
        LOG("menu: ImGui backend init failed");
        return false;
    }

    g_origWndProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
    if (!g_origWndProc)
    {
        LOG("menu: could not hook the game window procedure (%lu)", GetLastError());
        return false;
    }

    // With the fit on, shrink the desktop window to the monitor now that we can
    // catch its resizes. MCC created it at the full render size (which overflows
    // small monitors and put the menu off-screen); the shrink keeps MCC drawing
    // the full-size backbuffer while only the visible window fits. Posted so it
    // runs on the UI thread.
    if (D3D_FitActive())
        PostMessageW(gameWindow, kFitGameWindowMsg, 0, 0);

    g_ready = true;
    LOG("menu ready (F1 to toggle)");
    return true;
}


bool Menu_IsOpen()
{
    return g_ready && g_open;
}

bool Menu_Toggle()
{
    if (!g_ready)
        return false;
    const bool open = !g_open.load(std::memory_order_acquire);
    g_open.store(open, std::memory_order_release);
    g_resetArmed = false; // never leave a reset half-armed across sessions
    // A closed menu can never be mid-drag, and a stale hover would otherwise let
    // the next trigger press grab the panel without touching the handle.
    g_pointerOverGrabHandle.store(false, std::memory_order_release);
    g_panelDragging.store(false, std::memory_order_release);
    LOG("menu %s", open ? "opened" : "closed");
    return open;
}

// Force the panel open on the welcome page. vr.cpp calls this exactly once per
// process, on the first focused frame, when show_welcome is set. Unconditional
// rather than "only if closed": if the player somehow already has the menu up
// this early, showing them the page is still the intended startup behavior, and
// a special case here would mean the message can silently never appear.
bool Menu_OpenWelcome()
{
    if (!g_ready)
        return false;
    EnterCriticalSection(&g_cs);
    g_activeCategory = Cat_Welcome;
    LeaveCriticalSection(&g_cs);
    g_resetArmed = false;
    g_pointerOverGrabHandle.store(false, std::memory_order_release);
    g_panelDragging.store(false, std::memory_order_release);
    g_open.store(true, std::memory_order_release);
    LOG("menu: opened on the welcome page for this launch");
    return true;
}

bool Menu_PointerOverGrabHandle()
{
    return g_ready && g_pointerOverGrabHandle.load(std::memory_order_acquire);
}

void Menu_SetPanelDragging(bool dragging)
{
    g_panelDragging.store(dragging, std::memory_order_release);
}

void Menu_SetVrPointer(bool hit, float u, float v, bool pressed, float scrollY)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_cs);
    g_vrPointer.hit = hit;
    g_vrPointer.pressed = pressed;
    g_vrPointer.u = u;
    g_vrPointer.v = v;
    g_vrPointer.scrollY += scrollY;
    LeaveCriticalSection(&g_cs);
}

void Menu_ClearVrPointer()
{
    Menu_SetVrPointer(false, 0.0f, 0.0f, false, 0.0f);
}

ID3D11Texture2D* Menu_Render()
{
    if (!g_ready)
        return nullptr;
    EnterCriticalSection(&g_cs);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2((float)MENU_W, (float)MENU_H); // our texture, not the window
    // The Win32 backend updates the desktop mouse during NewFrame. Apply the
    // VR ray afterward so it is the final pointer sample ImGui consumes.
    ImGuiIO& io = ImGui::GetIO();
    if (g_vrPointer.hit)
        io.AddMousePosEvent(g_vrPointer.u * MENU_W, g_vrPointer.v * MENU_H);
    else
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    static bool previousVrPressed = false;
    if (g_vrPointer.pressed != previousVrPressed)
    {
        io.AddMouseButtonEvent(0, g_vrPointer.pressed);
        previousVrPressed = g_vrPointer.pressed;
    }
    if (g_vrPointer.hit && g_vrPointer.scrollY != 0.0f)
        io.AddMouseWheelEvent(0.0f, g_vrPointer.scrollY);
    g_vrPointer.scrollY = 0.0f;
    ImGui::NewFrame();
    DrawUI();
    ImGui::Render();

    D3DStateBackup backup;
    backup.Capture(g_ctx);
    const float clear[4] = {0, 0, 0, 0}; // transparent: only the window itself shows on the panel
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    D3D11_VIEWPORT vp{0, 0, (float)MENU_W, (float)MENU_H, 0, 1};
    g_ctx->RSSetViewports(1, &vp);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    backup.Restore(g_ctx);

    LeaveCriticalSection(&g_cs);
    return g_tex;
}
