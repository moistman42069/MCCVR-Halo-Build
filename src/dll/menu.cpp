#include "../common/weapon_alignment.h"
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include "menu.h"
#include "../common/hotkey_logic.h"
#include "../common/flashlight_input.h"
#include "native_menu_pointer.h"
#include "host_unload_gate.h"
#include "native_vehicle_first_person.h"
#include "menu_slider.h"
#include "vr.h"
#include "game.h"
#include "title_adapter.h"
#include "d3d_state.h"
#include "d3d11_hook.h"
#include "window_resize.h"
#include "telemetry_recorder.h"
#include "two_hand_lab_runtime.h"
#include "menu_font.generated.h"
#include "menu_style.h"
#include "../common/log.h"
#include "../common/config.h"
#include "../common/input_preset.h"
#include "../common/roomscale_recenter_logic.h"
#include "../common/reclaimer_host_logic.h"
#include "../common/weapon_interaction_logic.h"
#include "../common/vr_action_passthrough.h"
#include "../common/halo2_vehicle_identity.h"
#include "../common/window_focus_policy.h"

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
    bool g_virtualStockResetArmed = false;
    // Panel drag state. The panel is otherwise completely locked; the grab
    // handle along the top edge is the only thing that moves it. vr.cpp owns the
    // drag itself because it holds the controller ray -- we only tell it whether
    // the pointer is on the handle, and it tells us when a drag is running so
    // the bar can light up.
    std::atomic<bool> g_pointerOverGrabHandle{false};
    std::atomic<bool> g_panelDragging{false};

    using namespace vr_menu_style;
    ImFont* g_headingFont = nullptr;

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
        "Expect the rest of the collection to be added over time.\n\n"
        "Pass-on/community build maintained by @MeWhenINameMyself.\n"
        "Join the Flat2VR Discord to report bugs or share feedback.";

    constexpr const char* kWelcomeCloseHint =
        "Press L3+R3 (both analog sticks) to recenter and close the menu";

    enum MenuCategory
    {
        Cat_Welcome = 0,
        Cat_Status,
        Cat_QualityOfLife,
        Cat_Comfort,
        Cat_Theatre,
        Cat_Controls,
        Cat_Reload,
        Cat_Vehicles,
        Cat_WeaponAim,
        Cat_VirtualStockLab,
        Cat_TwoHandLab,
        Cat_Crosshair,
        Cat_BodyHands,
        Cat_Picture,
        Cat_Hud,
        Cat_Subtitles,
        Cat_Desktop,
        Cat_Scope,
        Cat_Telemetry,
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
        {"Quality of Life", "VR comfort and immersion settings, collected here from their usual sections."},
        {"Comfort",       "The flat screen you see in menus, and how head motion feels."},
        {"3D Theatre",    "A room-fixed stereo screen used only when the game locks the cinematic camera."},
        {"Controls",      "Your control scheme: presets, button mappings, movement, turning and gestures."},
        {"Reload & Holsters", "Magazine handling, weapon storage, and gesture zones."},
        {"Vehicles",      "First-person driving: sit in the seat instead of floating behind the vehicle."},
        {"Weapon & Aim",  "Gun placement, per-title calibration, muzzle alignment, and two-handed aiming."},
        {"Virtual Stock Lab", "Experimental virtual shoulder aiming and proximity release."},
        {"Two-Handed Lab", "Experimental two-hand anchor, authority and damping rig (runtime-only)."},
        {"Crosshair & Trajectory",     "The floating reticle that shows where the weapon really shoots."},
        {"Body & Hands",  "Arms, shoulders, and how much of Chief you can see."},
        {"Picture",       "Render resolution, sharpening, anti-aliasing and brightness."},
        {"HUD",           "Size and position of the active game's HUD."},
        {"Subtitles",     "Localized dialogue in gameplay and 3D theatre."},
        {"Desktop",       "The window on your monitor, not the headset."},
        {"Scope",         "Experimental gun-mounted zoom screen."},
        {"Telemetry Recorder", "Records controller, headset and aim-solver data for troubleshooting."},
        {"Advanced",      "Tracking calibration, panel placement, and starting over."},
    };
    static_assert(sizeof(kCategories) / sizeof(kCategories[0]) == Cat_Count,
                  "kCategories must list exactly one row per MenuCategory");

    int g_activeCategory = Cat_Status;
    constexpr float kSidebarWidth = 300.0f;

    // Render the original controls in either location, rather than creating
    // duplicate config values or a second implementation of their callbacks.
    // Each section has its own ID scope so expanded groups cannot collide.
    struct CategorySection
    {
        bool visible=false,scoped=false;
        explicit CategorySection(MenuCategory category,MenuCategory alsoIn=Cat_Count)
        {
            if(g_activeCategory!=category&&g_activeCategory!=alsoIn&&g_activeCategory!=Cat_QualityOfLife)return;
            ImGui::PushID(static_cast<int>(category));scoped=true;
            visible=true;
        }
        ~CategorySection(){if(scoped)ImGui::PopID();}
        CategorySection(const CategorySection&)=delete;
        CategorySection& operator=(const CategorySection&)=delete;
    };

    // Choosing a preset applies it at once; the file or built-in stays as it was until the player saves over it.
    void DrawInputPresets(bool& changed)
    {
        static std::vector<std::string> saved;
        static bool listed=false;
        static char newName[64]="";
        static Config baseline;
        static std::string baselineName;
        static bool baselineFound=false;
        if(!listed){saved=input_preset::ListSaved();listed=true;}
        const std::string active=g_config.input_preset;
        if(!baselineFound||baselineName!=active)
        {
            baseline=Config{};
            baselineName=active;
            baselineFound=input_preset::Find(active,baseline);
        }
        const auto apply=[&](const std::string& name)
        {
            Config scheme{};
            if(!input_preset::Find(name,scheme)) return;
            input_preset::CopyScheme(g_config,scheme);
            strncpy_s(g_config.input_preset,name.c_str(),_TRUNCATE);
            baselineFound=false;
            changed=true;
        };
        ImGui::SetNextItemWidth(-140.0f);
        if(ImGui::BeginCombo("Input preset",active.empty()?"(not saved)":active.c_str()))
        {
            for(int i=0;i<input_preset::BuiltInCount();++i)
                if(ImGui::Selectable(input_preset::BuiltInName(i),active==input_preset::BuiltInName(i)))
                    apply(input_preset::BuiltInName(i));
            std::string chosen;
            for(const auto& name:saved)
                if(ImGui::Selectable(name.c_str(),active==name)) chosen=name;
            if(!chosen.empty()) apply(chosen);
            ImGui::EndCombo();
        }
        const bool isSaved=std::find(saved.begin(),saved.end(),active)!=saved.end();
        const bool modified=baselineFound&&!input_preset::SameScheme(g_config,baseline);
        if(modified)
        {
            ImGui::TextColored(ImVec4(1.f,.8f,.3f,1.f),"Changed since you chose this preset.");
            ImGui::SameLine();
            if(ImGui::Button("Undo changes")) apply(active);
        }
        else if(!baselineFound&&isSaved==false&&!active.empty())
            ImGui::TextDisabled("This preset's file is gone. Your settings stay as they are.");
        if(isSaved&&modified&&ImGui::Button("Save over this preset"))
        {
            input_preset::Save(active,g_config);
            baselineFound=false;
        }
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##presetname","Name (optional)",newName,sizeof(newName));
        ImGui::SameLine();
        if(ImGui::Button("Save as new preset"))
        {
            std::string name=input_preset::SafeName(newName);
            for(int n=static_cast<int>(saved.size())+1;name.empty()||std::find(saved.begin(),saved.end(),name)!=saved.end();++n)
                name="My preset "+std::to_string(n);
            if(input_preset::Save(name,g_config))
            {
                strncpy_s(g_config.input_preset,name.c_str(),_TRUNCATE);
                newName[0]=0;
                listed=false;
                baselineFound=false;
                changed=true;
            }
        }
        if(isSaved)
        {
            ImGui::SameLine();
            if(ImGui::Button("Delete this preset"))
            {
                input_preset::Delete(active);
                g_config.input_preset[0]=0;
                listed=false;
                baselineFound=false;
                changed=true;
            }
        }
        ImGui::TextDisabled("Saves your buttons, turning, movement, gestures, reload and holsters. Not your aim or\n"
            "body fit. Presets are files in Input Presets beside halomccvr.cfg, so you can share them.");
    }

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
        ImGui::PushFont(g_headingFont);
        ImGui::TextColored(Rgb(kAccent), "HALO MCC VR");
        ImGui::PopFont();
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
    constexpr UINT kLiveResizeMsg = WM_APP + 0x38;
    WindowResizeBatch g_liveResizeBatch;

    float RenderAspect()
    {
        unsigned rw=0,rh=0,ow=0,oh=0;bool dlss=false,runtime=false;
        D3D_GetRenderPlan(rw,rh,ow,oh,dlss,runtime);
        return rw&&rh?float(rw)/float(rh):float(kNativeRenderWidth)/float(kNativeRenderHeight);
    }

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
        const float aspect = RenderAspect();
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
        if(msg==WM_SIZE&&g_liveResizeBatch.SuppressesSize(hwnd)) return 0;
        if (NativeMenuPointer_Message(msg)) return 0;
        // MCC is closing: nothing may hold its shutdown back (host_unload_gate.cpp).
        if (msg == WM_CLOSE || msg == WM_DESTROY) HostUnloadGate_HostClosing(msg == WM_DESTROY);
        if(msg==kLiveResizeMsg)
        {
            unsigned width=0,height=0;
            if(!D3D_BeginLiveResize(width,height)) return 0;
            if(!D3D_CanLiveResize()||!width||!height||width>65535||height>65535||!g_origWndProc)
            { D3D_CancelQueuedLiveResize();return 0; }
            // Fit first, suppress intermediate WM_SIZE notifications, then
            // deliver one admitted render-size change on the window thread.
            g_liveResizeBatch.Apply(hwnd,[&]{if(D3D_FitActive()) FitGameWindow(hwnd);},[&]{
                struct ClientSizeScope {
                    ClientSizeScope(){D3D_SetForcedClientLie(true);}
                    ~ClientSizeScope(){D3D_SetForcedClientLie(false);}
                } clientSize;
                CallWindowProcW(g_origWndProc,hwnd,WM_SIZE,SIZE_RESTORED,
                    MAKELPARAM(static_cast<WORD>(width),static_cast<WORD>(height)));
            });
            return 0;
        }
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
        // retain the existing active-window behavior in a running title.
        // Shell/authentication keeps genuine focus so native text dialogs can
        // acquire/release keyboard input normally.
        const auto focusAction=DecideWindowFocus(TitleAdapter_GetActiveTitle(),
            TitleAdapter_GetRuntimeMode(),msg);
        switch (msg)
        {
        case WM_ACTIVATEAPP:
            if(focusAction==WindowFocusAction::KeepActive)wp = TRUE;
            break;
        case WM_ACTIVATE:
            if (focusAction==WindowFocusAction::KeepActive&&LOWORD(wp) == WA_INACTIVE)
                wp = MAKEWPARAM(WA_ACTIVE, 0);
            break;
        case WM_NCACTIVATE:
            // TRUE keeps the window drawn/treated as active.
            if(focusAction==WindowFocusAction::KeepActive)
                return CallWindowProcW(g_origWndProc, hwnd, msg, TRUE, lp);
            break;
        case WM_KILLFOCUS:
            // Don't let the game hear that it lost keyboard focus.
            if(focusAction==WindowFocusAction::SuppressLoss)return 0;
            break;
        case WM_MOUSEACTIVATE:
            if(focusAction==WindowFocusAction::ActivateMouse)return MA_ACTIVATE;
            break;
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
                            const float aspect = RenderAspect();
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
            if (D3D_CanLiveResize() && wp != SIZE_MINIMIZED)
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
        // A mod hotkey is a BARE key (hotkey_logic.h). One that arrives with Ctrl, Alt or a Windows key held is
        // another tool's chord and goes on to the game untouched: otherwise Ctrl+F9, a replay-capture hotkey, adds
        // two degrees of pitch trim per saved clip.
        hotkey::Modifiers mods;
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
        {
            mods.ctrl = GetKeyState(VK_CONTROL) < 0;
            mods.alt = GetKeyState(VK_MENU) < 0 || (msg == WM_SYSKEYDOWN && (lp & (1 << 29)) != 0);
            mods.win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
            const bool repeat = (lp & (1 << 30)) != 0;
            if (hotkey::IsForeignChord(msg == WM_KEYDOWN, msg == WM_SYSKEYDOWN, repeat,
                                       static_cast<unsigned>(wp), mods))
                LOG("hotkey ignored: key 0x%02X arrived with %s%s%sheld, so it is another tool's chord; passed on "
                    "to the game", static_cast<unsigned>(wp), mods.ctrl ? "Ctrl " : "", mods.alt ? "Alt " : "",
                    mods.win ? "Win " : "");
        }
        if (hotkey::IsHotkeyPress(msg == WM_KEYDOWN, msg == WM_SYSKEYDOWN, (lp & (1 << 30)) != 0,
                                  static_cast<unsigned>(wp), mods))
        {
            switch (wp)
            {
            case VK_F1: Menu_Toggle(); return 0;
            case VK_F2: Game_ToggleHeadTracking(); return 0;
            case VK_F3: Game_RecenterManual(); return 0;
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
        ImGui::PushFont(g_headingFont);
        for (int i = 0; i < Cat_Count; ++i)
        {
            // Virtual Stock Lab is intentionally excluded from normal navigation;
            // product controls live in Weapon & Aim.
            if (i == Cat_VirtualStockLab)
                continue;
            // Two-Handed Lab is runtime-only experimental infrastructure hidden
            // the same way: no normal navigation entry. Its page block and state
            // stay wired below; product two-hand controls live in Weapon & Aim.
            if (i == Cat_TwoHandLab)
                continue;
            const bool selected = g_activeCategory == i;
            if (ImGui::Selectable(kCategories[i].label, selected))
            {
                g_activeCategory = i;
                g_virtualStockResetArmed = false;
            }
            // A cyan bar down the left edge of the selected row, so the
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
        ImGui::PopFont();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##pane", ImVec2(0, -footerHeight), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushFont(g_headingFont);
        ImGui::TextColored(Rgb(kAccent), "%s", kCategories[g_activeCategory].label);
        ImGui::PopFont();
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
        ImGui::Spacing();
        ImGui::TextUnformatted("DLSS / NGX: This software contains source code provided by NVIDIA Corporation. "
            "NVIDIA components are covered by the included NVIDIA license.");
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
            Game_RecenterManual();
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
        if (ImGui::Button(headTrackingApplied
                ? "Turn head tracking OFF (F2)"
                : "Turn head tracking ON (F2)"))
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
                    headTrackingApplied ? "ON" : "OFF",
                    VR_IsStereoEnabled() ? "ON" : "OFF",
                    VR_IsPausePresentation() ? "head-locked 2D" : "immersive 3D");
        if (ImGui::Button("Force injection / recover VR"))
            Game_RequestManualVrRecovery();
        ImGui::TextWrapped("%s", Game_ManualVrRecoveryStatus());
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("L3+R3 recenters and closes this menu; F1 only closes it.");
        }

        if (g_activeCategory == Cat_QualityOfLife)
            ImGui::TextWrapped("Expand a section to adjust its settings. These are the same controls as their individual pages.");

        if (CategorySection section{Cat_Comfort}; section.visible)
        {
        if (ImGui::CollapsingHeader("Virtual screen##section", ImGuiTreeNodeFlags_None))
        {
        // These two used to stop at 10 m even though the config file accepts 20,
        // so a value typed into halomccvr.cfg could not be reached or restored
        // from the menu. The slider now spans the file's own range.
        changed |= vr_menu::SliderFloat("Screen width (m)", &g_config.screen_width_m, 0.5f, 20.0f, "%.1f");
        changed |= vr_menu::SliderFloat("Screen distance (m)", &g_config.screen_distance_m, 0.3f, 20.0f, "%.1f");
        ImGui::TextDisabled("The flat screen the game is shown on in menus and 2D mode.\n"
                            "This is NOT the F1 panel; that one lives under Advanced.");
        // A host that draws its own UI over the game's frame (Project Reclaimer): its menus, scoreboard and chat
        // as a panel in front of the 3D picture.
        if (g_hostOwnsRenderSize)
        {
            ImGui::Spacing();
            ImGui::Text("Project Reclaimer's menus and on-screen items");
            changed |= ImGui::Checkbox("Show them as a panel over the 3D picture", &g_config.host_ui_panel);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("On: Project Reclaimer's in-game menus, scoreboard, chat and names appear on a panel\n"
                                  "fixed in front of you, and the game stays 3D behind it.\n"
                                  "Off: the picture goes flat while one of its menus is up.");
            ImGui::BeginDisabled(!g_config.host_ui_panel);
            changed |= vr_menu::SliderFloat("Panel width (m)", &g_config.host_ui_panel_width_m, 0.5f, 20.0f, "%.1f");
            changed |= vr_menu::SliderFloat("Panel distance (m)", &g_config.host_ui_panel_distance_m, 0.3f, 20.0f,
                                            "%.1f");
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("World scale##section", ImGuiTreeNodeFlags_None))
        {
            float worldScale=Game_GetWorldScale()/.33f*100.f;
            if (vr_menu::SliderFloat("World scale", &worldScale, 50.f, 150.f, "%.0f%%"))
            {
                Game_SetWorldScale(worldScale*.0033f);
                changed=true;
            }
            if (ImGui::SmallButton("Reset world scale")) {
                Game_SetWorldScale(.33f);
                changed=true;
            }
            ImGui::TextDisabled("100% is the original scale. Saved separately for each game.");
        }
        if (ImGui::CollapsingHeader("Head motion##section", ImGuiTreeNodeFlags_None))
        {
        float headsetSmoothPercent = g_config.headset_smoothing * 100.0f;
        if (vr_menu::SliderFloat("Headset micro-smoothing", &headsetSmoothPercent,
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
        }
        if (ImGui::CollapsingHeader("Roomscale##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Roomscale body movement", &g_config.roomscale_movement);
        ImGui::TextDisabled("Physical steps move your character while on foot. Walking follows your head.\n"
            "Controller aiming is preserved. Enable head tracking and positional tracking.\n"
            "The movement stick takes priority. Physical steps are retained and the body catches up once movement settles. "
            "Recenter with F3 when needed.");
        if (g_config.roomscale_movement)
        {
            // A/B: switches live; each switch starts the follow afresh from where you stand.
            ImGui::Text("Follow:");
            ImGui::SameLine();
            changed |= ImGui::RadioButton("Hysteresis (A)", &g_config.roomscale_follow_law, 1);
            ImGui::SameLine();
            changed |= ImGui::RadioButton("Threshold (B)", &g_config.roomscale_follow_law, 0);
            ImGui::TextDisabled(g_config.roomscale_follow_law
                ? "Hysteresis: a lean under 3 cm never walks, the body brakes by letting go instead of backing up,\n"
                  "and a distance you walked away during a tracking gap is still caught up."
                : "Threshold: walks from 1.5 cm and brakes by reversing. Kept as it shipped in Alpha 0.6.0, "
                  "for comparison.");
            // The view recenter (roomscale_recenter_logic.h): three independent options, all on by default.
            ImGui::Spacing();
            ImGui::Text("Bring the view back over your character (steadier aim):");
            changed |= ImGui::Checkbox("On a jump", &g_config.roomscale_recenter_jump);
            if (g_config.roomscale_recenter_jump)
                changed |= vr_menu::SliderFloat("Jump slide time", &g_config.roomscale_recenter_jump_s,
                    roomscale_recenter::kMinSeconds, roomscale_recenter::kMaxSeconds, "%.2f s");
            changed |= ImGui::Checkbox("When you start moving with the stick", &g_config.roomscale_recenter_move_start);
            if (g_config.roomscale_recenter_move_start)
                changed |= vr_menu::SliderFloat("Start-moving slide time", &g_config.roomscale_recenter_move_start_s,
                    roomscale_recenter::kMinSeconds, roomscale_recenter::kMaxSeconds, "%.2f s");
            changed |= ImGui::Checkbox("Gradually while moving with the stick", &g_config.roomscale_recenter_moving);
            if (g_config.roomscale_recenter_moving)
                changed |= vr_menu::SliderFloat("Slide speed while moving", &g_config.roomscale_recenter_moving_speed,
                    roomscale_recenter::kMinSpeed, roomscale_recenter::kMaxSpeed, "%.2f m/s");
            ImGui::TextDisabled(
                "In the air, and while the stick moves you, your character cannot step under your head,\n"
                "so your view sits away from where the game aims and shoots from. These slide the view back\n"
                "over the character while that motion hides it. They never turn the view or change your\n"
                "height, and your hands and reticle move with it.\n"
                "On a jump / when you start moving: one quick slide, over the time you set.\n"
                "Gradually while moving: a steady slide at the speed you set, for as long as the stick is\n"
                "held. It need not finish: once you stand still, your legs walk to you as before.");
        }
        }
        if (ImGui::CollapsingHeader("Sound##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Sound follows your head", &g_config.audio_follows_head);
        ImGui::TextDisabled("ON: you hear the world from the way your head faces.\n"
                            "OFF: from the way your gun points, as the game does it.\n"
                            "Halo: Reach, Halo 3, Halo 4 and ODST; other games keep their own sound. Live.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Startup##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Auto-enter VR on level load", &g_config.auto_vr);
        ImGui::TextDisabled("Turns head tracking + stereo on when a level starts and off in the menu.");
        }
        }

        if (CategorySection section{Cat_Theatre}; section.visible)
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
        if (ImGui::CollapsingHeader("Stereo depth##section", ImGuiTreeNodeFlags_None))
        {
        float depthPercent = g_config.cutscene_theater_depth * 100.0f;
        if (vr_menu::SliderFloat(
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
        }
        if (ImGui::CollapsingHeader("Room-fixed screen##section", ImGuiTreeNodeFlags_None))
        {
        changed |= vr_menu::SliderFloat(
            "Screen width (m)##theatre",
            &g_config.cutscene_theater_width_m, 0.5f, 20.0f, "%.1f");
        changed |= vr_menu::SliderFloat(
            "Screen distance (m)##theatre",
            &g_config.cutscene_theater_distance_m, 0.3f, 20.0f, "%.1f");
        ImGui::TextDisabled(
            "Defaults: 6.0 m wide at 4.0 m away. Height follows the authored\n"
            "cinematic projection.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Black cine bars##section", ImGuiTreeNodeFlags_None))
        {
        bool matteOn = g_config.cutscene_theater_matte_aspect > 0.0f;
        if (ImGui::Checkbox("Show cine bars", &matteOn))
        {
            g_config.cutscene_theater_matte_aspect = matteOn ? 16.0f / 9.0f : 0.0f;
            changed = true;
        }
        if (matteOn)
        {
            changed |= vr_menu::SliderFloat(
                "Picture shape##theatre",
                &g_config.cutscene_theater_matte_aspect, 1.0f, 3.0f, "%.2f:1");
            changed |= vr_menu::SliderFloat(
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
        }
        if (ImGui::CollapsingHeader("Subtitles##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox(
            "Show subtitles on the theatre screen",
            &g_config.cutscene_theater_subtitles);
        ImGui::TextDisabled(
            "The game draws subtitles after it hands us the two eye images, so they\n"
            "reach your monitor but not your headset. This puts the text back on the\n"
            "screen where the game placed it. MCC's own subtitle setting must be on.");
        if (g_config.cutscene_theater_subtitles)
        {
            changed |= vr_menu::SliderFloat(
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
        }

        if (CategorySection section{Cat_Controls}; section.visible)
        {
        DrawInputPresets(changed);
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Movement##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Physical running", &g_config.physical_running);
        if(g_config.physical_running) {
            changed |= vr_menu::SliderFloat("Running speed",&g_config.physical_running_speed,.1f,1.f,"%.2f");
            changed |= vr_menu::SliderFloat("Running sensitivity",&g_config.physical_running_sensitivity,.25f,3.f,"%.2f");
            ImGui::TextWrapped("Alternate both arms to move forward while on foot. Speed is a fraction of the game's normal maximum. While running, arm motion takes over the movement stick. Menus and vehicles keep their normal controls.");
        }
        // Neither the checkbox nor the depth re-samples the standing height: it is learned all the time,
        // and only the Calibrate button takes the player's current height as their own word.
        if(ImGui::Checkbox("Physical crouch",&g_config.physical_crouch))
            changed=true;
        if(g_config.physical_crouch)
        {
            float crouchCm=g_config.physical_crouch_depth_m*100.f;
            if(vr_menu::SliderFloat("Crouch activation depth",&crouchCm,8.f,65.f,"%.0f cm"))
            {g_config.physical_crouch_depth_m=crouchCm*.01f;changed=true;}
            if(ImGui::Button("Calibrate crouch at my current height")) VR_RecalibratePhysicalCrouch();
            ImGui::TextWrapped("Lower your head by the activation depth to crouch. Your standing height is learned "
                "on its own: a few seconds standing normally sets it, and recentering, cutscenes and level loads "
                "leave it alone. Playing seated? Press Calibrate while seated: the reference then stays at that "
                "height, rising by at most a little under two thirds of the activation depth, so half-rising "
                "never leaves you crouched. Smaller depths activate sooner. On foot only; button crouch remains "
                "separate. Use MCC's hold-to-crouch setting; native toggle crouch is not overridden.");
            if(Input_PhysicalCrouchRiseCapped())
                ImGui::TextColored(ImVec4(1.f,.8f,.3f,1.f),"You stood taller than the reference learns on its own: "
                    "press Calibrate while standing tall for the full crouch depth.");
        }
        ImGui::Spacing();
        }
        if (ImGui::CollapsingHeader("VR turning (right controller stick)##section", ImGuiTreeNodeFlags_None))
        {
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
        changed |= ImGui::Checkbox("Automatically smooth turn in vehicles", &g_config.vehicle_smooth_turn);
        ImGui::TextDisabled("Uses your smooth turn speed while seated. Exiting restores your normal turn mode.");
        if (g_config.turn_smooth || g_config.vehicle_smooth_turn)
            changed |= vr_menu::SliderFloat("Turn speed (deg/s)", &g_config.turn_smooth_deg_s, 30.0f, 360.0f, "%.0f");
        if (!g_config.turn_smooth)
            changed |= vr_menu::SliderFloat("Snap increment (deg)", &g_config.turn_snap_deg, 5.0f, 90.0f, "%.0f");

        ImGui::Spacing();
        changed |= ImGui::Checkbox("Y + B sends Start (pause / resume)",
                                   &g_config.y_b_start_chord);
        ImGui::TextDisabled("On by default. Turn it off if you prefer Y and B to stay separate.");

        ImGui::Spacing();
        }
        if (ImGui::CollapsingHeader("D-pad gesture (hold controller next to head)##section", ImGuiTreeNodeFlags_None))
        {
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
        float dpadHeadRadiusCm = g_config.dpad_head_radius * 100.0f;
        if (vr_menu::SliderFloat("D-pad head radius", &dpadHeadRadiusCm,
                                 10.0f, 50.0f, "%.0f cm"))
        {
            g_config.dpad_head_radius = dpadHeadRadiusCm / 100.0f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A smaller radius requires the selected controller to be closer to your head.");
        changed |= ImGui::Checkbox("X near my head talks in Project Reclaimer", &g_config.reclaimer_talk_gesture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hold your left controller to your head and press X for voice chat.\n"
                              "It follows Reclaimer's talk mode (Audio): hold to talk, or press to start and stop.\n"
                              "Uses the key V. If you rebound Push to talk, set reclaimer_talk_key in halomccvr.cfg.");
        changed |= ImGui::Checkbox("Y near my head shows the scoreboard in Project Reclaimer",
                                   &g_config.reclaimer_scoreboard_gesture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hold your left controller to your head and press Y for the scoreboard.\n"
                              "Uses the key Tab. If you rebound it, set reclaimer_scoreboard_key in halomccvr.cfg.");
        changed |= ImGui::Checkbox("A and B near my head vote in Project Reclaimer", &g_config.reclaimer_vote_gesture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hold your right controller to your head: A votes yes, B votes no.\n"
                              "Uses the keys F5 and F6. If you rebound them, set reclaimer_vote_yes_key and\n"
                              "reclaimer_vote_no_key in halomccvr.cfg.");
        changed |= ImGui::Checkbox("Keep walking when my hand comes up", &g_config.dpad_keep_moving);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If you're walking when your hand reaches your head, you keep walking.\n"
                              "Let the stick return to centre and it becomes the D-pad.\n"
                              "Off: the stick becomes the D-pad at once and you stop.");
        changed |= ImGui::Checkbox("Quest 3 left thumb-rest D-pad",
                                   &g_config.quest_thumbrest_dpad);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Touch the physical left controller's thumb rest, then move the physical right stick.\n"
                              "These controls stay on the same physical hands in left-handed mode.");
        ImGui::Spacing();
        float hapticPercent = g_config.haptic_intensity * 100.0f;
        if (vr_menu::SliderFloat("Controller vibration", &hapticPercent,
                               0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_None))
        {
            g_config.haptic_intensity = hapticPercent / 100.0f;
            if (g_config.haptic_intensity <= 0.0f)
                VR_SetGameHaptics(0.0f);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adjust vibration strength; 0%% disables it.\n"
                              "Supported weapon feedback follows the holding hand, sharing recoil while supporting the gun.\n"
                              "Damage and vehicle feedback can use both controllers.");
        changed |= ImGui::Checkbox("Point at game menus", &g_config.game_menu_pointer);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Aim your primary controller at MCC's main or pause menu, then pull its trigger to click.\n"
                              "A white ring marks the cursor and shrinks when clicked. MCC must have desktop focus.\n"
                              "Release the trigger before your first click. Off preserves normal menu controls.");
        ImGui::TextDisabled("L3+R3 recenters and toggles this menu; the right trigger clicks the VR pointer.");
        }
        if (ImGui::CollapsingHeader("VR Mappings##section", ImGuiTreeNodeFlags_None))
        {
            static int selectedTitle=0;
            static GameTitle previousTitle=GameTitle::None;
            const auto active=TitleAdapter_GetActiveTitle();
            const int activeIndex=weapon_interaction::TitleIndex(active);
            if(active!=previousTitle&&activeIndex>=0) selectedTitle=activeIndex;
            previousTitle=active;
            ImGui::Text("Detected left: %s",VR_ControllerProfileName(true));
            ImGui::Text("Detected right: %s",VR_ControllerProfileName(false));
            changed|=ImGui::Checkbox("Remap buttons (off: every button is the game's own)",&g_config.vr_action_mapping);
            ImGui::TextWrapped("Automatic keeps an action where the game's own button layout has it. Choosing anything "
                "else for a row turns remapping on. Physical gestures remain separate.");
            ImGui::Spacing();
            ImGui::TextUnformatted("Left trigger by what your left hand is doing");
            if(ImGui::Button("Grenade with a free hand, zoom gripping the gun"))
            {
                for(auto& title:g_config.vr_bindings)
                {
                    title[vr_mapping::Grenade]=vr_mapping::SupportTriggerFree;
                    title[vr_mapping::Zoom]=vr_mapping::SupportTriggerOnGun;
                }
                g_config.vr_action_mapping=true;
                changed=true;
            }
            ImGui::TextDisabled("Sets those two rows for all six games. Reload and holster grips send nothing, and dual\n"
                "wielding and vehicle seats keep the plain trigger.");
            if(ImGui::Checkbox("Gripping air uses equipment",&g_config.trigger_air_equipment))
            {
                for(auto& title:g_config.vr_bindings)
                    if(g_config.trigger_air_equipment) title[vr_mapping::Equipment]=vr_mapping::SupportTriggerAir;
                    else if(title[vr_mapping::Equipment]==vr_mapping::SupportTriggerAir)
                        title[vr_mapping::Equipment]=vr_mapping::Automatic;
                if(g_config.trigger_air_equipment) g_config.vr_action_mapping=true;
                changed=true;
            }
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Pull the left trigger while gripping nothing to use equipment.\n"
                                  "Off, or in a game without equipment: that pull throws a grenade.");
            ImGui::Spacing();
            ImGui::Combo("Settings for",&selectedTitle,weapon_interaction::kTitleNames,6);
            ImGui::TextWrapped("All six games support separate actions while direct VR routing is available. "
                "Games using the MCC layout fallback can link actions sharing a native button. "
                "If the direct route is unavailable, Reload and Use may still share a button.");
            const auto selectedGame=static_cast<GameTitle>(selectedTitle+1);
            if(vr_mapping::DirectPassthroughActions(selectedGame))
                ImGui::TextWrapped("Halo 2: Reload and Use device always send their MCC button, so the game's "
                    "own tap and hold choose reload, pick up, board or use.");
            vr_mapping::Transports bindings{};
            const bool haveBindings=activeIndex==selectedTitle&&Game_ReadVrActionBindings(bindings,GetTickCount64());
            for(unsigned action=0;action<vr_mapping::Count;++action)
            {
                ImGui::PushID(static_cast<int>(action));
                ImGui::TextUnformatted(vr_mapping::ActionName(selectedGame,static_cast<vr_mapping::Action>(action)));
                ImGui::SetNextItemWidth(-1);
                if(ImGui::Combo("##source",&g_config.vr_bindings[selectedTitle][action],
                    vr_mapping::kSourceNames,vr_mapping::SourceCount))
                {
                    if(g_config.vr_bindings[selectedTitle][action]!=vr_mapping::Automatic)
                        g_config.vr_action_mapping=true;
                    changed=true;
                }
                if(g_config.vr_bindings[selectedTitle][action]==vr_mapping::Automatic)
                {
                    if(haveBindings)
                        ImGui::TextDisabled("The game has this on: %s",
                            vr_mapping::kSourceNames[vr_mapping::LayoutSource(bindings[action])]);
                    else
                        ImGui::TextDisabled("Follows the game's own layout.");
                }
                if(haveBindings&&!bindings[action]) ImGui::TextDisabled("No native action binding available in this title.");
                ImGui::PopID();
            }
            if(ImGui::Button("Reset this game's mappings"))
            {g_config.vr_bindings[selectedTitle]={};changed=true;}
            ImGui::Separator();
            changed|=ImGui::Checkbox("Disable flashlight button",&g_config.disable_flashlight_input);
            changed|=ImGui::Checkbox("Suppress flashlight while supporting a weapon",&g_config.flashlight_suppress_on_two_hand);
            ImGui::TextWrapped("Flashlight suppression leaves grip tracking and two-hand aiming available. "
                "MCC actions that share the same native button can still occur together.");
        }
        if (ImGui::CollapsingHeader("Two-handed grip and melee##section", ImGuiTreeNodeFlags_None))
        {
            if (ImGui::RadioButton("Toggle (click grip)", g_config.two_hand_toggle))
            { g_config.two_hand_toggle = true; changed = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Hold grip", !g_config.two_hand_toggle))
            { g_config.two_hand_toggle = false; changed = true; }
            changed |= ImGui::Checkbox("Physical melee", &g_config.physical_melee);
            changed |= ImGui::Checkbox("Gesture melee (experimental)", &g_config.gesture_melee);
            ImGui::TextDisabled("The same settings as Weapon & Aim and Body & Hands, here because a preset saves them.");
        }
        }

        if (CategorySection section{Cat_Vehicles}; section.visible)
        {
        if (ImGui::CollapsingHeader("First-person vehicle camera##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Sit in the seat (first person)",
                                   &g_config.vehicle_first_person);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("All six games. CE, Halo 2 and Halo 4 use the seated character's head position.\n"
                "Position settings have a separate base for each game. Identified vehicles also retain individual seat adjustments.");
        ImGui::TextDisabled(
            "OFF restores the stock behind-the-vehicle view instantly.");
        ImGui::Spacing();
        // Project Reclaimer only: how its flyers climb and dive (game.cpp, reclaimer_flight.inl).
        if (g_hostIsProjectReclaimer)
        {
            ImGui::Text("Project Reclaimer flying");
            changed |= ImGui::Checkbox("Climb and dive with buttons", &g_config.reclaimer_flight_buttons);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("On: Hornet-class flyers climb and dive with Ascend and Descend, as in MCC.\n"
                                  "Off: Project Reclaimer's classic Halo 3 controls: aim up or down to climb or dive.");
            ImGui::TextDisabled("%s", Game_ReclaimerFlightStatus());
            ImGui::Spacing();
        }
        static int vehicleGame=0;
        static GameTitle previousVehicleTitle=GameTitle::None;
        const auto activeVehicleTitle=TitleAdapter_GetActiveTitle();
        if(previousVehicleTitle!=activeVehicleTitle)
        {
            const int activeIndex=weapon_interaction::TitleIndex(activeVehicleTitle);
            if(activeIndex>=0)vehicleGame=activeIndex;
            previousVehicleTitle=activeVehicleTitle;
        }
        ImGui::Combo("Game##vehicletrim",&vehicleGame,"Halo 3\0ODST\0Reach\0Halo 4\0Halo CE\0Halo 2\0");
        const auto editVehicleTitle=static_cast<GameTitle>(vehicleGame+1);
        static bool editVehicleBase=false;
        ImGui::Checkbox("Edit this game's base position##vehicletrim",&editVehicleBase);
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
        static GameTitle s_seatTitle=GameTitle::None;
        if(s_seatTitle!=editVehicleTitle||editVehicleTitle!=activeVehicleTitle||editVehicleBase)
        {s_seatBind=-1;s_seatMissFrames=0;s_seatTitle=editVehicleTitle;}
        VehicleTrimBank liveBank = VehicleTrimBank::Halo3;
        const int liveSlot = editVehicleTitle==activeVehicleTitle&&!editVehicleBase?
            Game_VehicleSeatTrimSlotEx(&liveBank):-1;
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
        uint64_t vehicleIdentity=0;int modelSeat=-1;
        const bool modelTarget=!perSeat&&!editVehicleBase&&NativeVehicleFirstPerson_TrimTarget(
            editVehicleTitle,vehicleIdentity,modelSeat);
        const int modelTrim=ConfigVehicleModelTrimSlot(g_config,editVehicleTitle,vehicleIdentity,modelSeat);
        const ConfigShippedVehicleModelTrim* shippedModelTrim=modelTarget?
            ConfigShippedVehicleModelTrimFor(editVehicleTitle,vehicleIdentity,modelSeat):nullptr;
        bool modelCapacity=modelTrim>=0;
        for(const auto& trim:g_config.vehicle_model_trims)modelCapacity|=!trim.identity;
        auto setModelAxis=[&](unsigned axis,float value) {
            const int slot=ConfigEnsureVehicleModelTrim(g_config,editVehicleTitle,vehicleIdentity,modelSeat);
            if(slot>=0){g_config.vehicle_model_trims[slot].value[axis]=value;g_config.vehicle_model_trims[slot].set[axis]=true;}
        };
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
        else if(modelTarget) {
            const char* vehicleName=editVehicleTitle==GameTitle::Halo2?halo2_vehicle_identity::Name(vehicleIdentity):nullptr;
            if(!vehicleName&&shippedModelTrim)vehicleName=shippedModelTrim->name;
            ImGui::Text("Adjusting: %s, seat %d (%s)",vehicleName?vehicleName:"last identified vehicle",modelSeat,
                weapon_interaction::kTitleNames[vehicleGame]);
            ImGui::TextDisabled("Last identified seat; retained while F1 pauses the game.");
        }
        else
            ImGui::Text("Adjusting: %s base position",weapon_interaction::kTitleNames[vehicleGame]);
        float seatFwd = perSeat?ConfigSeatCamForward(g_config, seatSlot):ConfigGameVehicleCam(g_config,editVehicleTitle,0);
        if(modelTarget)seatFwd=ConfigVehicleModelCam(g_config,editVehicleTitle,vehicleIdentity,modelSeat,0);
        if (perSeat&&s_seatBank == VehicleTrimBank::Odst)
            seatFwd = ConfigOdstSeatCamForward(g_config, seatSlot);
        else if (perSeat&&s_seatBank == VehicleTrimBank::Reach)
            seatFwd = ConfigReachSeatCamForward(g_config, seatSlot);
        if(modelTarget&&!modelCapacity)ImGui::TextDisabled("Vehicle preset storage is full. Reset an unused preset before adding another.");
        ImGui::BeginDisabled(modelTarget&&!modelCapacity);
        if (vr_menu::SliderFloat("Seat forward (m)", &seatFwd,
                               forwardMin, forwardMax, "%.2f"))
        {
            if (perSeat)
            {
                if (reachSeat)
                    ConfigReachSeatBeginTrimEdit(g_config, seatSlot);
                trimForwardV[seatSlot] = seatFwd;
                trimForwardSet[seatSlot] = true;
            }
            else if(modelTarget)setModelAxis(0,seatFwd);
            else
            {g_config.vehicle_cam_game[vehicleGame][0]=seatFwd;g_config.vehicle_cam_game_set[vehicleGame][0]=true;}
            changed = true;
        }
        float seatUp = perSeat?ConfigSeatCamUp(g_config, seatSlot):ConfigGameVehicleCam(g_config,editVehicleTitle,1);
        if(modelTarget)seatUp=ConfigVehicleModelCam(g_config,editVehicleTitle,vehicleIdentity,modelSeat,1);
        if (perSeat&&s_seatBank == VehicleTrimBank::Odst)
            seatUp = ConfigOdstSeatCamUp(g_config, seatSlot);
        else if (perSeat&&s_seatBank == VehicleTrimBank::Reach)
            seatUp = ConfigReachSeatCamUp(g_config, seatSlot);
        if (vr_menu::SliderFloat("Seat height (m)", &seatUp,
                               upMin, upMax, "%.2f"))
        {
            if (perSeat)
            {
                if (reachSeat)
                    ConfigReachSeatBeginTrimEdit(g_config, seatSlot);
                trimUpV[seatSlot] = seatUp;
                trimUpSet[seatSlot] = true;
            }
            else if(modelTarget)setModelAxis(1,seatUp);
            else
            {g_config.vehicle_cam_game[vehicleGame][1]=seatUp;g_config.vehicle_cam_game_set[vehicleGame][1]=true;}
            changed = true;
        }
        float seatRight = perSeat?ConfigSeatCamRight(g_config, seatSlot):ConfigGameVehicleCam(g_config,editVehicleTitle,2);
        if(modelTarget)seatRight=ConfigVehicleModelCam(g_config,editVehicleTitle,vehicleIdentity,modelSeat,2);
        if (perSeat&&s_seatBank == VehicleTrimBank::Odst)
            seatRight = ConfigOdstSeatCamRight(g_config, seatSlot);
        else if (perSeat&&s_seatBank == VehicleTrimBank::Reach)
            seatRight = ConfigReachSeatCamRight(g_config, seatSlot);
        if (vr_menu::SliderFloat("Seat left / right (m)", &seatRight,
                               rightMin, rightMax, "%.2f"))
        {
            if (perSeat)
            {
                if (reachSeat)
                    ConfigReachSeatBeginTrimEdit(g_config, seatSlot);
                trimRightV[seatSlot] = seatRight;
                trimRightSet[seatSlot] = true;
            }
            else if(modelTarget)setModelAxis(2,seatRight);
            else
            {g_config.vehicle_cam_game[vehicleGame][2]=seatRight;g_config.vehicle_cam_game_set[vehicleGame][2]=true;}
            changed = true;
        }
        ImGui::EndDisabled();
        if(modelTarget&&modelTrim>=0&&ImGui::SmallButton(shippedModelTrim?
            "Back to this seat's built-in position##modeltrim":"Use this game's base for this seat##modeltrim"))
        {ConfigResetVehicleModelTrim(g_config,modelTrim);changed=true;}
        if (perSeat && (trimForwardSet[seatSlot] || trimUpSet[seatSlot] ||
                        trimRightSet[seatSlot]))
        {
            if (ImGui::SmallButton(reachSeat
                    ? "Back to this seat's authored point##seattrim"
                    : "Back to this game's base##seattrim"))
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
        // Reset only the selected game's fallback; other titles and explicit
        // per-seat values stay independent.
        if (!perSeat && !modelTarget &&
            (seatFwd != kVehicleCamForwardDefault ||
             seatUp != kVehicleCamUpDefault || seatRight != kVehicleCamRightDefault))
        {
            if (ImGui::SmallButton("Reset this game's base##seattrim"))
            {
                g_config.vehicle_cam_game[vehicleGame][0]=kVehicleCamForwardDefault;
                g_config.vehicle_cam_game[vehicleGame][1]=kVehicleCamUpDefault;
                g_config.vehicle_cam_game[vehicleGame][2]=kVehicleCamRightDefault;
                for(bool& axis:g_config.vehicle_cam_game_set[vehicleGame])axis=true;
                changed = true;
            }
        }
        ImGui::TextDisabled(
            "Sit in a seat and these sliders adjust that seat alone —\n"
            "a vehicle's driver, passengers and gunner each remember their\n"
            "own. On foot they set this game's base every unadjusted seat\n"
            "follows. In Reach the base is your Blender-authored point for\n"
            "that seat and the slider travels the same distance either side\n"
            "of it that Halo 3 and ODST travel; vehicle and occupant motion\n"
            "are parented around that baseline.\n"
            "Left/right: negative moves left, positive moves right.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Aim in seats##section", ImGuiTreeNodeFlags_None))
        {
        ImGui::TextDisabled("Seats follow Converged aim and Direct drive in Weapon & Aim, as on foot.\n"
                            "Reach, Halo CE and Halo 2 fire a seat's weapon at what that seat's own crosshair\n"
                            "hits, so the aim is bent to put that crosshair on your target (Halo CE and Halo 2:\n"
                            "in the first-person seat view; the gun then turns its shot onto it as far as its\n"
                            "own cone allows). A land vehicle seat's turning\n"
                            "limits still hold (a passenger's cone, a turret's or tank gun's up/down stop):\n"
                            "point past one and the crosshair stops at the edge, where the gun is held and\n"
                            "the shots go. A gun still turning toward your hand is not a limit: the\n"
                            "crosshair stays on your aim point. Aircraft have no limits.\n"
                            "A seat whose stick steers the vehicle keeps the stick.");
        {
            bool seats = g_config.seat_direct_drive != 0;
            if (ImGui::Checkbox("Direct drive in seats", &seats))
            { g_config.seat_direct_drive = seats ? 1 : 0; changed = true; }
            ImGui::TextDisabled("ON (with Direct drive): a seat's aim is written as on foot, once the seat has\n"
                                "checked its own aim (a moment after you sit down). Every title needs its\n"
                                "first-person seat view: in Halo 3 and ODST with \"Hide my character in the seat\"\n"
                                "on; in Halo 2 also with \"VR controls in Halo CE and Halo 2 vehicles\" on\n"
                                "and \"View follows the vehicle\" off. Halo 4 seats write the heading only.\n"
                                "OFF: the stick steers the aim in every seat.");
            if (g_config.reach_vehicle_aim <= 0)
            {
                // 0 is a cfg-only "seat aim off": never shown as the OFF below, never rewritten silently.
                ImGui::TextDisabled("Seat aim is off in the cfg (reach_vehicle_aim = 0): every seat keeps the stick.");
                if (ImGui::SmallButton("Turn seat aim on##reach_vehicle_aim"))
                { g_config.reach_vehicle_aim = 2; changed = true; }
            }
            else
            {
                bool drivers = g_config.reach_vehicle_aim >= 2;
                if (ImGui::Checkbox("Drivers aim with the hand", &drivers))
                { g_config.reach_vehicle_aim = drivers ? 2 : 1; changed = true; }
                ImGui::TextDisabled("ON: a driver whose vehicle turns toward the aim (Warthog, Ghost, Banshee) steers\n"
                                    "with the hand. OFF: that driver keeps the stick; gunners and passengers still\n"
                                    "aim with the hand -- except in Halo 4, which cannot yet tell a seat's role, so\n"
                                    "OFF keeps every seat there on the stick. Halo 2 reads the role off the seat\n"
                                    "itself. Halo CE's hand aims every seat either way.");
            }
            bool halo4Seats = g_config.halo4_seat_converge != 0;
            if (ImGui::Checkbox("Halo 4: converged aim in seats", &halo4Seats))
            { g_config.halo4_seat_converge = halo4Seats ? 1 : 0; changed = true; }
            ImGui::TextDisabled("ON (default): in a Halo 4 seat the game traces your aim, and the seat's aim is\n"
                                "bent so the line the game itself aims that seat along passes through the point\n"
                                "you point at; the crosshair sits on that point. With Co-op safe shots on, that\n"
                                "line starts at the game's own seat camera, as on every other player's game, and\n"
                                "only the aim you send is changed. The gun turns its shot onto the point as far as\n"
                                "the game lets it. Needs Converged aim and the first-person seat view. Halo 4\n"
                                "seats write the heading; up/down still turns at the stick's rate. OFF: Halo 4\n"
                                "seats keep the fixed-distance aim. Live.");
        }
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Turning with the vehicle##section", ImGuiTreeNodeFlags_None))
        {
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
        changed |= vr_menu::SliderFloat("Seat bounce", &g_config.vehicle_bounce,
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
        }
        if (ImGui::CollapsingHeader("VR controls in seats (Halo CE, Halo 2)##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("VR controls in Halo CE and Halo 2 vehicles",
                                   &g_config.vehicle_vr_controls);
        ImGui::TextDisabled(
            "ON (default): in Halo CE the right stick snap/smooth-turns your\n"
            "seat view; in Halo 2 your hand aims, and the seat keeps a view of\n"
            "its own that the aim does not swing. OFF: those two games' seats\n"
            "use the plain right stick, as on a flat screen. Halo 3, ODST,\n"
            "Reach and Halo 4 seats are not affected.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("How you control a seat (Halo 3)##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Combo("Seat controls", &g_config.vehicle_control,
                                "As before\0Motion: your hand\0Stick\0Wheel\0");
        ImGui::TextDisabled(
            "Apart from how your view turns: each works with a view the world\n"
            "holds still and with one that turns with the vehicle.\n"
            "Motion: your hand aims in every seat, and a Warthog, Mongoose,\n"
            "Ghost, Prowler, Chopper, Banshee or Hornet turns toward it.\n"
            "Stick: the right stick aims and steers in every seat, as on a flat\n"
            "screen; your hand aims nothing and the stick no longer turns your view.\n"
            "Wheel: in those ground vehicles the invisible wheel is in your hands\n"
            "from the moment you sit; every other seat is your hand's.\n"
            "As before: your hand aims, except that the stick or wheel steers\n"
            "those vehicles while the view turns with them.\n"
            "With vehicle cameras on, the left stick click changes this for the\n"
            "seat you are in, and each seat keeps its own choice.");
        changed |= vr_menu::SliderFloat("Ignore aim behind, within (m)",
            &g_config.vehicle_aim_reject_metres, 0.0f, 20.0f, "%.1f");
        changed |= vr_menu::SliderFloat("Behind is a dot below",
            &g_config.vehicle_aim_reject_dot, -1.0f, 1.0f, "%.2f");
        ImGui::TextDisabled(
            "While your hand steers one of those vehicles, a point you aim at\n"
            "that is behind the vehicle and this close to it does not turn it:\n"
            "it keeps its forward. With a camera outside the vehicle your hand\n"
            "easily lands on the ground just behind it. Measured along the\n"
            "vehicle's own floor; 0 m = off. Behind: -0.5 = more than 120\n"
            "degrees off the nose, 0 = more than 90, -1 = nothing.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Motion steering (Halo 3, ODST, Reach)##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Virtual steering wheel",
                                   &g_config.vehicle_motion);
        ImGui::TextDisabled(
            "Warthog, Mongoose, Ghost, Prowler and Chopper: DOUBLE-CLICK both\n"
            "grips to take an invisible wheel, then just hold your hands on it\n"
            "and tilt - no squeezing. Double-click again to let go and the\n"
            "right stick steers. A single right grip still gets you out of the\n"
            "vehicle either way. Aircraft and the Scorpion/Wraith keep their\n"
            "own controls.");
        changed |= vr_menu::SliderFloat("Full lock at (deg)",
            &g_config.vehicle_wheel_max_deg, 30.0f, 180.0f, "%.0f");
        changed |= vr_menu::SliderFloat("Wheel deadzone (deg)",
            &g_config.vehicle_wheel_deadzone_deg, 0.0f, 30.0f, "%.0f");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Vehicle cameras (Halo 3)");
        changed |= ImGui::Checkbox("Cameras from halomccvr_vehicle_cameras.json",
                                   &g_config.vehicle_cameras);
        changed |= ImGui::Combo("Note when the camera changes", &g_config.vehicle_camera_note,
                                "Off\0In the game's HUD messages\0On the mod's panel\0");
        changed |= ImGui::Checkbox("Add new vehicles to the camera file", &g_config.vehicle_camera_autoadd);
        changed |= ImGui::Checkbox("Ring where the vehicle aims",
                                   &g_config.vehicle_aim_marker);
        ImGui::TextDisabled(
            "A purple ring beside the crosshair, where the vehicle itself is\n"
            "aiming. While your view turns with the vehicle (Tethered) it is\n"
            "where the view comes to rest: aim at it to go straight without\n"
            "turning. The file's \"aimMarker\" turns it on or off per seat,\n"
            "camera or mode; the built-in cameras show it when Tethered.");
        ImGui::TextDisabled(
            "In a seat, step through the cameras that file lists for it. The\n"
            "file is in the mod's folder, written the first time this is on;\n"
            "edit it and it is read again when you save. Its first camera is\n"
            "the seat view you already have; a vehicle the mod does not know\n"
            "starts where the game's own third-person camera sits.\n"
            "In a seat: left Y = next camera, left X = next tethering mode,\n"
            "either held a second = re-centre the view, left stick click = next\n"
            "way to control the seat. Right B then uses, gets out and reloads,\n"
            "and a passenger's support trigger switches weapon. Each seat keeps\n"
            "its last camera, mode and controls. Needs first-person vehicles on.");
        if (g_config.vehicle_cameras)
        {
            if (ImGui::Button("Next camera"))
                Game_VehicleCameraStep(1, 0);
            ImGui::SameLine();
            if (ImGui::Button("Previous camera"))
                Game_VehicleCameraStep(-1, 0);
            ImGui::SameLine();
            if (ImGui::Button("Next mode"))
                Game_VehicleCameraStep(0, 1);
            char cameraLine[384];
            if (Game_VehicleCameraReadout(cameraLine, sizeof(cameraLine)))
                ImGui::TextWrapped("%s", cameraLine);
            // Lag of the camera in use, kept for that camera (halomccvr_vehicle_cameras_tuning.txt).
            float lagSeconds = 0.0f, lagDamping = 1.0f;
            bool lagTuned = false;
            if (Game_VehicleCameraLag(lagSeconds, lagDamping, lagTuned))
            {
                bool lagChanged = vr_menu::SliderFloat("Camera lag (s)", &lagSeconds, 0.0f, 2.0f, "%.2f");
                lagChanged |= vr_menu::SliderFloat("Lag damping", &lagDamping, 0.1f, 2.0f, "%.2f");
                if (lagChanged)
                    Game_VehicleCameraSetLag(lagSeconds, lagDamping);
                if (lagTuned && ImGui::Button("Back to the camera file's lag"))
                    Game_VehicleCameraResetLag();
                ImGui::TextDisabled(
                    "For the camera you are in: seconds it takes to catch up\n"
                    "with a turn or a bump (0 = rigid). Damping 1 settles\n"
                    "without overshoot; lower swings past and back. Kept for\n"
                    "this camera in halomccvr_vehicle_cameras_tuning.txt.");
            }
            else
            {
                ImGui::TextDisabled("Camera lag: sit in a vehicle with a chase camera to tune it.");
            }
            // The third-person boom of the camera in use (vehicle_boom_logic.h).
            float boomDegrees = 0.0f, boomLow = 0.0f, boomHigh = 0.0f;
            bool boomTrack = false, boomInverted = false;
            if (Game_VehicleBoom(boomDegrees, boomLow, boomHigh, boomTrack, boomInverted))
            {
                ImGui::Text("Third-person boom: %.1f deg above level (it goes from %.0f to %.0f)%s", boomDegrees,
                            boomLow, boomHigh, boomInverted ? ", stick inverted" : "");
                if (ImGui::Button("Back to the camera's start"))
                    Game_VehicleBoomRecenter();
                ImGui::TextDisabled(boomTrack
                    ? "Where the game's own third-person camera would stand at that\n"
                      "angle, from the game's camera track for this seat."
                    : "The game's camera track could not be read for this seat, so\n"
                      "the camera orbits the game's pivot at the game's distance.");
            }
            changed |= vr_menu::SliderFloat("Third-person boom speed (deg/s)",
                &g_config.vehicle_boom_rate_deg_s, 5.0f, 360.0f, "%.0f");
            int boomInvert = g_config.vehicle_boom_invert + 1;
            if (ImGui::Combo("Third-person boom up and down", &boomInvert,
                             "As the game's look inversion\0Not inverted\0Inverted\0"))
            {
                g_config.vehicle_boom_invert = boomInvert - 1;
                changed = true;
            }
            ImGui::TextDisabled(
                "A camera that stands where the game's own third-person camera\n"
                "does: push the right stick up or down to lower or raise it, the\n"
                "way the game's own stick moves its camera, in a seat where the\n"
                "stick does not aim (your hand or the wheel does). It never\n"
                "follows your aim. Each seat and camera keeps its angle; hold\n"
                "left X or Y to put it back.");
        }
        }
        }

        // Also in Controls, because a preset saves these.
        if (CategorySection section{Cat_Reload, Cat_Controls}; section.visible)
        {
        if (ImGui::CollapsingHeader("Reload and holsters##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("dual wield shake reload", &g_config.dual_wield_shake_reload);
        ImGui::TextDisabled("shake to reload a weapon when dual wielding");
        if(g_config.dual_wield_shake_reload)
        {
            changed |= vr_menu::SliderFloat("Minimum shake stroke (m)##dualreload",
                &g_config.weapon_shake_travel_m,0.06f,0.20f,"%.2f");
            ImGui::TextDisabled("Shake a hand back and forth once: it presses that hand's reload button.\n"
                "Halo 3: either hand. Halo 2: the right-hand gun only for now.\n"
                "Let the hand settle before shaking again.");
        }
        changed |= ImGui::Checkbox("Manual Reload", &g_config.manual_reload);
        if(g_config.manual_reload&&g_config.gesture_only_weapon_input)
            ImGui::TextDisabled("Gestures only: button and keyboard reload are off.");
        if (g_config.manual_reload)
        {
            ImGui::TextDisabled("Hold your support grip at your hip to take a magazine.\n"
                "Bring it below your weapon hand, then release to reload.\n"
                "Native ammo and reserve limits still apply.");
            changed |= ImGui::Checkbox("Disable automatic reload", &g_config.manual_reload_disable_auto);
            changed |= ImGui::Checkbox("Skip reload and weapon-ready animations", &g_config.manual_reload_skip_animations);
            changed |= ImGui::Checkbox("Shortened reload animation", &g_config.manual_reload_shortened_animation);
            if(g_config.manual_reload_shortened_animation)
                ImGui::TextDisabled(g_config.manual_reload_skip_animations ?
                    "Full animation skip takes precedence while both are enabled." :
                    "Skip to insertion, then play the weapon's chambering tail.\n"
                    "Weapons without a usable insertion keyframe keep their full reload.");
            changed |= vr_menu::SliderFloat("Magazine grab radius (m)",
                &g_config.weapon_body_zone_radius_m,0.08f,0.40f,"%.2f");
            changed|=ImGui::Combo("Reserve magazine location",&g_config.weapon_pouch_location,
                "Support-side front hip\0Behind support shoulder\0");
            if(ImGui::TreeNode("Reserve magazine position"))
            {
                changed|=vr_menu::SliderFloat("Reserve side offset (m)",&g_config.weapon_pouch_offset_x_m,-.40f,.40f,"%.3f");
                changed|=vr_menu::SliderFloat("Reserve height offset (m)",&g_config.weapon_pouch_offset_y_m,-.40f,.40f,"%.3f");
                changed|=vr_menu::SliderFloat("Reserve forward offset (m)",&g_config.weapon_pouch_offset_z_m,-.40f,.40f,"%.3f");
                ImGui::TextWrapped("Offsets follow your body direction. Positive side moves toward the weapon hand; left-handed mode mirrors it.");
                ImGui::TreePop();
            }
            changed |= vr_menu::SliderFloat("Magazine insertion radius (m)",
                &g_config.weapon_insert_radius_m,0.06f,0.30f,"%.2f");
            changed |= ImGui::Checkbox("Generic reload item for unknown weapons", &g_config.weapon_unknown_reload_visual);
            ImGui::TextDisabled("Detected unfamiliar weapons use a blue reload item.\n"
                "Native ammo and reload rules still apply; it is not the mod's own mesh.");
            changed |= ImGui::Checkbox("Shake to reload needle weapons", &g_config.weapon_needler_shake);
            if(g_config.weapon_needler_shake)
            {
                changed |= vr_menu::SliderFloat("Minimum shake stroke (m)",
                    &g_config.weapon_shake_travel_m,0.06f,0.20f,"%.2f");
                ImGui::TextDisabled("Recognized Needlers in all titles, plus Reach's Needle Rifle.\n"
                    "Quickly shake the gun hand back and forth once in any direction.\n"
                    "No grip needed. Let your hand settle before another shake.\n"
                    "Unrecognized weapon models keep regular controls.");
            }
        }
        changed |= ImGui::Checkbox("Weapon Holsters", &g_config.weapon_holsters);
        if(g_config.weapon_holsters&&g_config.gesture_only_weapon_input)
            ImGui::TextDisabled("Gestures only: button and keyboard weapon switching are off.");
        if(g_config.manual_reload||g_config.weapon_holsters)
        {
            changed |= ImGui::Checkbox("Gestures only (turn off the reload and switch buttons)",
                                       &g_config.gesture_only_weapon_input);
            ImGui::TextDisabled("Off (default): the gestures and the game's own buttons both work.\n"
                                "On: only the gestures reload and switch weapons while those options are on.");
        }
        if (ImGui::Button("Over-the-shoulder weapon switch (Campaign Evolved)"))
        {
            // That mod's gesture: reach over the weapon-side shoulder and squeeze the grip.
            g_config.weapon_holsters = true;
            g_config.weapon_holster_location = 2;
            g_config.weapon_holster_click = true;
            g_config.weapon_holster_slide = false;
            g_config.weapon_holster_radius_m = weapon_interaction::kOverShoulderRadius;
            changed = true;
        }
        ImGui::TextDisabled("One press sets the holster up like the Campaign Evolved mod: reach over your\n"
                            "weapon-side shoulder and squeeze the weapon grip to switch weapons. Every game.\n"
                            "Your switch-weapon button still works.");
        {
            // The holster where the player's own hand goes: arm here, then squeeze the weapon grip there.
            static uint32_t shownCount = VR_HolsterCaptureCount();
            static uint32_t shownTimedOut = VR_HolsterCaptureTimedOut();
            static int lastOutcome = 0;  // 1 set, 2 lapsed
            if (VR_HolsterCaptureCount() != shownCount)
            {
                shownCount = VR_HolsterCaptureCount();
                lastOutcome = 1;
            }
            if (VR_HolsterCaptureTimedOut() != shownTimedOut)
            {
                shownTimedOut = VR_HolsterCaptureTimedOut();
                lastOutcome = 2;
            }
            const bool armed = VR_HolsterCaptureArmed();
            // One button; its label says when it is armed (the ### keeps its identity through the change).
            if (ImGui::Button(armed ? "Calibrate Holster Location (Armed)###holstercapture"
                                    : "Calibrate Holster Location###holstercapture"))
            {
                VR_HolsterCaptureArm(!armed);
                lastOutcome = 0;
            }
            const char* hand = g_config.left_handed ? "left" : "right";
            if (armed)
                ImGui::TextDisabled("Armed. Close this menu. The next time you squeeze the grip of your %s (weapon)\n"
                                    "controller in game, the holster is set where that hand is, measured from your\n"
                                    "head. Let go of the grip first if you are holding it. Press the button again\n"
                                    "to cancel; it also lapses 30 seconds after the menu closes.", hand);
            else if (lastOutcome == 1)
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                                   "Set: %.0f cm to the weapon side, %.0f cm %s your head, %.0f cm %s it.",
                                   g_config.weapon_holster_custom_x_m * 100.0f,
                                   fabsf(g_config.weapon_holster_custom_y_m) * 100.0f,
                                   g_config.weapon_holster_custom_y_m < 0 ? "below" : "above",
                                   fabsf(g_config.weapon_holster_custom_z_m) * 100.0f,
                                   g_config.weapon_holster_custom_z_m < 0 ? "behind" : "in front of");
            else if (lastOutcome == 2)
                ImGui::TextDisabled("No grip came within 30 seconds of closing the menu: nothing was changed.");
            else
                ImGui::TextDisabled("Puts the holster where your own hand goes. Press it, close this menu, then squeeze\n"
                                    "the grip of your %s (weapon) controller where the holster should be. Turns\n"
                                    "holsters on. Every game.", hand);
        }
        if (g_config.weapon_holsters)
        {
            changed |= ImGui::Combo("Holster location", &g_config.weapon_holster_location,
                "Weapon-side shoulder\0Weapon-side hip\0Over the weapon-side shoulder\0"
                "Custom (set with your hand)\0");
            if (g_config.weapon_holster_location == 3)
            {
                changed |= vr_menu::SliderFloat("Custom spot: to the weapon side (m)",
                    &g_config.weapon_holster_custom_x_m, -0.80f, 0.80f, "%.2f");
                changed |= vr_menu::SliderFloat("Custom spot: up from the head (m)",
                    &g_config.weapon_holster_custom_y_m, -0.80f, 0.80f, "%.2f");
                changed |= vr_menu::SliderFloat("Custom spot: forward of the head (m)",
                    &g_config.weapon_holster_custom_z_m, -0.80f, 0.80f, "%.2f");
            }
            changed |= ImGui::Checkbox("Holster slide / draw gesture", &g_config.weapon_holster_slide);
            changed |= ImGui::Checkbox("Holster click gesture", &g_config.weapon_holster_click);
            changed |= vr_menu::SliderFloat("Holster grab radius (m)",
                &g_config.weapon_holster_radius_m,0.08f,0.40f,"%.2f");
            if(g_config.weapon_holster_slide&&!g_config.weapon_holster_click)
                changed |= vr_menu::SliderFloat("Minimum holster draw distance (m)",
                    &g_config.weapon_holster_draw_m,0.10f,0.50f,"%.2f");
            ImGui::TextDisabled("Click: squeeze your weapon grip inside the holster to switch.\n"
                "Slide: hold that grip and draw out of the zone to switch.\n"
                "With both enabled, click switches immediately. Release before repeating.\n"
                "Exchanges your current weapon with the other carried weapon.\n"
                "The holster is a gesture zone; no extra weapon model is shown.");
            if(!g_config.weapon_holster_slide&&!g_config.weapon_holster_click)
                ImGui::TextDisabled("Choose a gesture to enable holster switching.");
        }
        if (g_config.manual_reload || g_config.weapon_holsters)
        {
            changed |= vr_menu::SliderFloat("Pouch / hip depth below head (m)",
                &g_config.weapon_pouch_down_m,0.25f,0.85f,"%.2f");
            ImGui::TextWrapped("Reload and switch gestures follow the current game's action bindings automatically. "
                "Button choices are in Controls > VR Mappings. Release a claimed grip before starting another gesture.");
        }
        }
        }

        if (CategorySection section{Cat_WeaponAim}; section.visible)
        {
        if (ImGui::CollapsingHeader("Aim (every title)##section", ImGuiTreeNodeFlags_None))
        {
        {
            bool converged = g_config.converged_aim != 0;
            if (ImGui::Checkbox("Converged aim", &converged))
            { g_config.converged_aim = converged ? 1 : 0; changed = true; }
            ImGui::TextDisabled("ON: the shot passes through what you point at, at any distance: the game traces the\n"
                                "ray from your eye along the controller and bends the aim so its shot line crosses\n"
                                "that point, and the reticle sits on it. OFF: the earlier aim, which meets your\n"
                                "pointing line at one fixed distance. In Reach it needs Direct drive (below);\n"
                                "without it Reach keeps its own convergence.");
            bool drive = g_config.aim_direct_drive != 0;
            if (ImGui::Checkbox("Direct drive", &drive))
            { g_config.aim_direct_drive = drive ? 1 : 0; Config_ApplyAimModes(); changed = true; }
            ImGui::TextDisabled("ON (Reach, Halo 3, ODST, Halo 4): your aim writes the game's own aim, so it follows the\n"
                                "controller at once instead of at the game's turn rate. It arms itself a moment after\n"
                                "you switch it on. OFF: the aim steers the game's look like a right stick.\n"
                                "Halo 2 on foot: ON also writes the game's own aim, so your body faces where you aim\n"
                                "and the right stick turns your view; OFF keeps the earlier Halo 2 aim (the shot\n"
                                "follows the hand, the body follows the stick). Halo CE always writes its aim\n"
                                "directly. Both switches are live.");
            changed |= ImGui::Checkbox("Shots leave the visible muzzle", &g_config.shots_from_muzzle);
            ImGui::TextDisabled("ON: on foot, each shot starts at your gun's visible muzzle and flies through the\n"
                                "point your converged aim is on (the reticle), so it lands where the shot from your\n"
                                "eye would have while tracers and projectiles come out of the barrel. Needs Converged\n"
                                "aim. If the muzzle is inside a wall your eye sees past, or the aim is not converged,\n"
                                "the shot leaves your eye as before. \"Aim from the visible gun barrel\" (Crosshair\n"
                                "page), when on, wins and keeps the barrel's own direction. Live.\n"
                                "Only while Co-op safe shots (Advanced page) allows it: on Automatic, the default,\n"
                                "that is when you play alone or in Firefight or a custom game. In a co-op session,\n"
                                "or with it on Always, shots leave your eye.");
            ImGui::TextDisabled("Both cover vehicle seats too. Every seat you aim from converges (Halo 3, ODST,\n"
                                "Halo CE and Halo 2 in first-person seats; Reach and Halo 2 with Direct drive;\n"
                                "Halo 4 seats with their switch on the Vehicles page). With Direct drive, seats have\n"
                                "their aim written too (Vehicles > Aim in seats says which); a seat first checks\n"
                                "its own aim, about a second after you sit down.");
            changed |= vr_menu::SliderFloat("Aim lead (game ticks)", &g_config.aim_lead_ticks, 0.0f, 3.0f, "%.2f");
            ImGui::TextDisabled("On foot in Halo 3, ODST and Reach with Direct drive. The game fires each shot along\n"
                                "the aim it was given about one game tick earlier, so a shot fired while you turn\n"
                                "lands a little behind the reticle. This writes the aim that far ahead of your\n"
                                "hand's own motion instead. Only the game's aim is led: the reticle, the gun and\n"
                                "the scope do not move, and a snap turn or a hand at rest is never led. It works\n"
                                "the same in solo and co-op. 0 = off. Live.");
#if HALOMCCVR_DEV_DIAGNOSTICS
            ImGui::Spacing();
            bool transport = g_config.shot_point_transport != 0;
            if (ImGui::Checkbox("Flat parity: move the shot point with your eye (A/B, dev)", &transport))
            { g_config.shot_point_transport = transport ? 1 : 0; changed = true; }
            ImGui::TextDisabled("DEV BUILD ONLY (players always have it ON). On foot in Halo 3, ODST, Halo 2, CE,\n"
                                "Halo 4 and Reach (Halo 2, Halo 4: shots from the muzzle; CE: those and its aim\n"
                                "assist; Reach: only once its eye at the fire is measured). ON: a shot\n"
                                "aims through the reticle's point moved with your eye since it was sampled -- what\n"
                                "flat Halo fires with the crosshair held still on your body. OFF: through the point\n"
                                "fixed in the world (rounds trail a strafe by one game tick). The reticle never\n"
                                "moves. Strafe past a wall and compare. Live.");
            if (g_config.shot_point_transport)
            {
                ImGui::Indent();
                bool seated = g_config.shot_point_transport_seated != 0;
                if (ImGui::Checkbox("In vehicle seats too (dev)", &seated))
                { g_config.shot_point_transport_seated = seated ? 1 : 0; changed = true; }
                ImGui::TextDisabled("ON (default). OFF aims a seated shot through the point fixed in the world.");
                bool measured = g_config.halo2_measured_fire_eye != 0;
                if (ImGui::Checkbox("Halo 2: the game's own eye at the fire, once proven (dev)", &measured))
                { g_config.halo2_measured_fire_eye = measured ? 1 : 0; changed = true; }
                ImGui::TextDisabled("ON: a Halo 2 muzzle shot moves with the eye the game fires from (measured, once\n"
                                    "proven while you stand still). OFF: the view's eye carried one tick (A/B).");
                bool measured4 = g_config.halo4_measured_fire_eye != 0;
                if (ImGui::Checkbox("Halo 4: the game's own eye at the fire, once proven (dev)", &measured4))
                { g_config.halo4_measured_fire_eye = measured4 ? 1 : 0; changed = true; }
                ImGui::TextDisabled("ON: a Halo 4 muzzle shot moves with the eye the game fires from (measured, once\n"
                                    "proven while you stand still). OFF: the view's eye carried one tick (A/B).");
                ImGui::Unindent();
            }
#endif
        }
        ImGui::Spacing();
        ImGui::Separator();
        // Here, not under Comfort: it changes how the hands (and so the aim) are read, not how the view feels.
        }
        if (ImGui::CollapsingHeader("Hand smoothing##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Hand smoothing", &g_config.hand_smoothing);
        ImGui::TextDisabled("Steadies your tracked hands before anything reads them: aim and shots, the arms,\n"
                            "two-handed holds, reload and this pointer. A still hand is held steady; a fast move\n"
                            "passes through. Swing melee and arm-swing running still read your raw motion. Live.");
        if (g_config.hand_smoothing)
        {
            changed |= ImGui::Combo("Hands##handsmoothing", &g_config.hand_smoothing_hands,
                                    "Both hands\0Only the hand I aim with\0Only my other hand\0");
            ImGui::TextDisabled("\"Only my other hand\" steadies two-handed holds and the arms with no lag on your aim.");
            changed |= vr_menu::SliderFloat("Position steadiness (Hz)", &g_config.hand_smoothing_pos_min_hz,
                                          0.0f, 10.0f, "%.2f");
            changed |= vr_menu::SliderFloat("Rotation steadiness (Hz)", &g_config.hand_smoothing_rot_min_hz,
                                          0.0f, 10.0f, "%.2f");
            ImGui::TextDisabled("While the hand is still. Lower is steadier but slower to follow a slow move or a\n"
                                "slow aim. 0 turns that half off.");
            changed |= vr_menu::SliderFloat("Position follow speed", &g_config.hand_smoothing_pos_beta,
                                          0.0f, 2000.0f, "%.0f");
            changed |= vr_menu::SliderFloat("Rotation follow speed", &g_config.hand_smoothing_rot_beta,
                                          0.0f, 50.0f, "%.1f");
            ImGui::TextDisabled("How quickly smoothing lets go as the hand moves or turns faster. Raise it if slow\n"
                                "moves feel laggy; lower it if a moving hand still shakes.");
            changed |= vr_menu::SliderFloat("Speed estimate smoothing (Hz)", &g_config.hand_smoothing_speed_cutoff_hz,
                                          0.1f, 100.0f, "%.1f");
            if (ImGui::Button("Defaults##handsmoothing"))
            {
                const Config defaults{};
                g_config.hand_smoothing_hands = defaults.hand_smoothing_hands;
                g_config.hand_smoothing_pos_min_hz = defaults.hand_smoothing_pos_min_hz;
                g_config.hand_smoothing_pos_beta = defaults.hand_smoothing_pos_beta;
                g_config.hand_smoothing_rot_min_hz = defaults.hand_smoothing_rot_min_hz;
                g_config.hand_smoothing_rot_beta = defaults.hand_smoothing_rot_beta;
                g_config.hand_smoothing_speed_cutoff_hz = defaults.hand_smoothing_speed_cutoff_hz;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("1.3 Hz, 500, 1.3 Hz, 5, 10 Hz. (The Campaign Evolved mod uses 1.0 Hz for both\n"
                                "steadiness values: a little steadier, a little slower on a slow aim.)");
        }
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Two-handed aiming##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Two-handed aiming", &g_config.two_handed_aim);
        ImGui::SameLine();
        ImGui::TextDisabled(VR_IsTwoHandAiming() ? "[engaged]" : "[one-handed]");
        changed |= ImGui::Checkbox("Left-handed main weapon", &g_config.left_handed);
        if (g_config.left_handed)
        {
            ImGui::Indent();
            changed |= ImGui::Checkbox("Fix Hand Alignment (Experimental)",
                &g_config.experimental_hand_alignment);
            ImGui::TextDisabled("Off: released hand positioning. On: experimental hand/arm correction.\n"
                                "Leave off if hands become misaligned.");
            ImGui::Unindent();
        }
        ImGui::TextDisabled("Main weapon, aiming and trigger follow your left hand.\n"
                            "Your right hand supports the weapon or holds the second gun.");
        if (g_config.two_handed_aim)
        {
            ImGui::Indent();
            ImGui::TextDisabled("Put your support hand on the front of the gun, click/hold its GRIP.\n"
                                "Engages only when your hand is on the barrel line.\n"
                                "Off whenever the game leaves first person (vehicle seats, turrets,\n"
                                "third-person, death and Monitor cameras, cutscenes): your main hand\n"
                                "aims alone. A grip held through it needs a fresh grip in first person.");
            ImGui::Text("When gripping, your support hand steers the aim:");
            if (ImGui::RadioButton("Based on the weapon##twohandaimby", g_config.two_hand_grip_zone_authority))
            { g_config.two_hand_grip_zone_authority = true; changed = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Based on how far apart your hands are##twohandaimby",
                                   !g_config.two_hand_grip_zone_authority))
            { g_config.two_hand_grip_zone_authority = false; changed = true; }
            if (g_config.two_hand_grip_zone_authority)
                ImGui::TextDisabled(
                    "Long guns: your support hand steers the aim.\n"
                    "Pistol-style weapons: it holds the gun, but your main hand aims alone.\n"
                    "This doesn't change while you're gripping, so a controller tracking drop-out can't switch it.");
            else
                ImGui::TextDisabled(
                    "Hands far apart: your support hand steers the aim.\n"
                    "Hands close together: it holds the gun, but your main hand aims alone.\n"
                    "A controller tracking drop-out can make your hands read as far apart and switch this for a moment.");
            float oneHandRangeCm = g_config.two_hand_one_hand_range_m * 100.0f;
            if (vr_menu::SliderFloat("One-handed aim range", &oneHandRangeCm, 6.0f, 48.0f, "%.0f cm",
                                     ImGuiSliderFlags_None))
            {
                g_config.two_hand_one_hand_range_m = oneHandRangeCm / 100.0f;
                changed = true;
            }
            if (g_config.two_hand_grip_zone_authority)
                ImGui::TextDisabled(
                    "A weapon counts as pistol-style when its grip is within this distance of your main hand.\n"
                    "Raise it if a compact gun should aim one-handed; lower it if one should aim two-handed.");
            else
                ImGui::TextDisabled(
                    "Your main hand aims alone when your hands are within %.0f cm of each other, or your\n"
                    "support hand is within %.0f cm to the side, %.0f cm up or down and %.0f cm forward or back.",
                    oneHandRangeCm * (0.16f / 0.24f), oneHandRangeCm, oneHandRangeCm * (0.14f / 0.24f),
                    oneHandRangeCm * (0.12f / 0.24f));
            ImGui::Spacing();
            ImGui::Text("Grip mode");
            if (ImGui::RadioButton("Toggle (click grip)", g_config.two_hand_toggle))
            { g_config.two_hand_toggle = true; changed = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Hold grip", !g_config.two_hand_toggle))
            { g_config.two_hand_toggle = false; changed = true; }
            ImGui::Spacing();
            changed |= ImGui::Checkbox("Persistent support grip",
                &g_config.persistent_support_grip);
            ImGui::TextDisabled(
                "Keeps the support hand attached after a valid grab even when it leaves the original grab area.\n"
                "Hold mode: releasing the grip button ends it. Toggle mode: the toggle press ends it.\n"
                "Also ends on an explicit release, or when the weapon, title or life state changes.");
            ImGui::Indent();
            ImGui::BeginDisabled(!g_config.persistent_support_grip);
            changed |= ImGui::Checkbox("Switched weapons already two-handed",
                &g_config.two_hand_switch_inherit);
            ImGui::EndDisabled();
            ImGui::TextDisabled(
                "Needs Persistent support grip: switching weapons while you hold the grip\n"
                "gives you the new weapon two-handed immediately.\n"
                "Off: grip the new weapon normally (no need to release the grip button).");
            ImGui::Unindent();
            ImGui::Spacing();
            changed |= ImGui::Checkbox("Reduce Support-Hand Rotation",
                &g_config.two_hand_support_grip_pose);
            ImGui::TextDisabled(
                "Virtual Stock only: anchors the stock aim line to your support hand's grip position\n"
                "instead of its aim point. Free two-handed aim always uses both grip positions.");
            ImGui::Spacing();
            float offhandInfluencePercent = MenuSliderPercentFromUnit(
                g_config.two_hand_offhand_influence);
            if (vr_menu::SliderFloat("Offhand influence",
                    &offhandInfluencePercent, 0.0f, 100.0f, "%.0f%%",
                    ImGuiSliderFlags_None))
            {
                g_config.two_hand_offhand_influence =
                    MenuSliderUnitFromPercent(offhandInfluencePercent);
                changed = true;
            }
            ImGui::TextDisabled(
                "How strongly support-hand position steers free two-hand aim.\n"
                "0%% = primary-hand orientation only; 100%% = full two-hand positional steering.\n"
                "Applies when Virtual Stock is off.");
            changed |= vr_menu::SliderFloat("Two-Hand Smoothing",
                &g_config.two_hand_smoothing_strength,
                kTwoHandSmoothingStrengthMinimum,
                kTwoHandSmoothingStrengthMaximum, "%.0f");
            ImGui::TextDisabled(
                "Reduces small tracking jitter in free two-hand aim.\n"
                "0 = off; higher values apply more smoothing.\n"
                "Applies to two-handed aiming, Virtual Stock on or off.");
            ImGui::Spacing();
            ImGui::Text("Grip and grab-zone calibration");
            changed |= vr_menu::SliderFloat("Left hand forward offset (m)",
                                          &g_config.left_hand_forward_m,
                                          -0.15f, 0.30f, "%.3f");
            ImGui::TextDisabled("Additional left-hand adjustment on top of the shared gun forward offset.");
            // Store absolute metres for compatibility with existing configurations.
            constexpr float grabSideBaseline=0.080f;
            float grabSideAdjustment=g_config.two_hand_zone_right_m-grabSideBaseline;
            if(vr_menu::SliderFloat("Grab zone side offset (m)",
                                   &grabSideAdjustment,
                                   -0.10f,0.10f,"%.3f")) {
                g_config.two_hand_zone_right_m=grabSideAdjustment+grabSideBaseline;
                changed=true;
            }
            ImGui::TextDisabled("Slides the grip-click zone sideways (+ = right). Zero uses the default 0.080 m offset.");
            changed |= vr_menu::SliderFloat("Left palm depth (m)",
                                          &g_config.left_grip_forward_m,
                                          -0.05f, 0.25f, "%.3f");
            ImGui::TextDisabled("Moves the support-hand grab sample forward from the tracked controller toward the visible palm.");
            ImGui::Unindent();
        }
        }
#include "virtual_stock_menu.inl"

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Hand-held weapon##section", ImGuiTreeNodeFlags_None))
        {
        if(ImGui::Checkbox("Per-gun alignment",&g_config.per_gun_alignment))
        { Config_RefreshWeaponProfile();changed=true; }
        if(g_config.per_gun_alignment)
        {
            if(const char* weapon=Config_ActiveWeaponProfileName())
                ImGui::Text("Editing equipped weapon: %s",weapon);
            else ImGui::TextDisabled("No recognized weapon: editing this game's defaults.");
        }
        ImGui::TextDisabled("Saves weapon size, offsets and calibration for the equipped gun.\n"
                            "Switch off to use this game's shared alignment again.");
        changed |= vr_menu::SliderFloat("Weapon size", &g_config.gun_scale, 0.3f, 3.0f, "%.2fx");
        ImGui::TextDisabled("Uniform scale of RIGHT hand + weapon about your grip (Home/End in-game).");
        changed |= vr_menu::SliderFloat("Left hand size", &g_config.left_hand_scale,
                                      0.3f, 3.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("Match weapon##lhs"))
        { g_config.left_hand_scale = g_config.gun_scale; changed = true; }
        ImGui::TextDisabled("Sizes the left hand, and the second gun when dual-wielding.\n"
                            "1.00 = authored size. Separate because the left hand is\n"
                            "usually empty; use Match weapon for identical hands.\n"
                            "Its front/back position is \"Left hand forward offset\" below.");
        }
        if (ImGui::CollapsingHeader("Visual gun alignment##section", ImGuiTreeNodeFlags_None))
        {
        changed |= vr_menu::SliderFloat("Gun alignment pitch (deg)", &g_config.barrel_pitch_deg, -180.0f, 180.0f, "%.1f");
        changed |= vr_menu::SliderFloat("Gun alignment yaw (deg)", &g_config.barrel_yaw_deg, -180.0f, 180.0f, "%.1f");
        changed |= vr_menu::SliderFloat("Gun alignment roll (deg)", &g_config.barrel_roll_deg, -180.0f, 180.0f, "%.1f");
        if (ImGui::SmallButton("Reset visual rotation"))
        {
            g_config.barrel_pitch_deg = 0.0f;
            g_config.barrel_yaw_deg = 0.0f;
            g_config.barrel_roll_deg = 0.0f;
            changed = true;
        }
        ImGui::TextDisabled("Align the visible barrel with the crosshair. These rotations\n"
                            "move the gun and hands without changing the aiming ray.\n"
                            "Saved separately for each game; zero restores automatic alignment.");
        const auto gunBase=weapon_alignment::Baseline(Config_ActiveTitleProfile());
        const auto offsetSlider=[&](const char* label,float& value,float baseline) {
            float delta=value-baseline;
            if (!vr_menu::SliderFloat(label,&delta,-.30f,.30f,"%.2f")) return false;
            value=baseline+delta;
            return true;
        };
        ImGui::TextUnformatted("Right hand");
        changed |= offsetSlider("Gun forward offset (m)##right",g_config.gun_forward_m,gunBase.forward);
        changed |= offsetSlider("Gun right offset (m)##right",g_config.gun_right_m,gunBase.right);
        changed |= offsetSlider("Gun up offset (m)##right",g_config.gun_up_m,gunBase.up);
        if (ImGui::SmallButton("Reset right gun offsets")) {
            g_config.gun_forward_m=gunBase.forward;g_config.gun_right_m=gunBase.right;g_config.gun_up_m=gunBase.up;
            changed=true;
        }
        const int offsetProfile=Config_ActiveTitleProfile();
        const auto leftBase=weapon_alignment::HasProfile(offsetProfile)?weapon_alignment::kLeftOffsets[offsetProfile]:
            weapon_alignment::Offsets{-.16f,.04f,-.06f};
        ImGui::TextUnformatted("Left hand");
        changed |= offsetSlider("Gun forward offset (m)##left",g_config.left_gun_forward_m,leftBase.forward);
        changed |= offsetSlider("Gun right offset (m)##left",g_config.left_gun_right_m,leftBase.right);
        changed |= offsetSlider("Gun up offset (m)##left",g_config.left_gun_up_m,leftBase.up);
        if (ImGui::SmallButton("Reset left gun offsets")) {
            g_config.left_gun_forward_m=leftBase.forward;g_config.left_gun_right_m=leftBase.right;g_config.left_gun_up_m=leftBase.up;
            changed=true;
        }
        ImGui::TextDisabled("Zero uses this game's calibrated position. Each hand is adjusted independently.");

        ImGui::TextDisabled("Visual rotations are separate from barrel aiming in all supported VR titles.");
        ImGui::Spacing();
        ImGui::TextDisabled("Halo 2 Classic includes a fixed -9.5 pitch and -1 yaw visual correction.");
        changed |= vr_menu::SliderFloat("Muzzle height (m)", &g_config.muzzle_height_m, -0.3f, 0.3f, "%.2f");
        ImGui::TextDisabled("Reach: adjusts secondary muzzle placement; H3/ODST marker effects follow the gun. H2 Classic does not read this slider.");
        ImGui::TextDisabled("Raises supported muzzle/effect placement along the gun's own axis.");
        ImGui::TextDisabled("Where rounds LAND is unchanged. 0.11 is about four inches.");
        const int flashTitle=weapon_interaction::TitleIndex(TitleAdapter_GetActiveTitle());
        if(flashTitle==3||flashTitle==5)
        {
            ImGui::Text("Muzzle flashes: %s",weapon_interaction::kTitleNames[flashTitle]);
            changed|=ImGui::Checkbox("Hide muzzle flashes in this game",&g_config.hide_muzzle_flash[flashTitle]);
            ImGui::TextWrapped(flashTitle==5?
                "Original graphics only. Hides the local first-person particle pass; "
                "other first-person particles may also disappear. Anniversary stays unchanged.":
                "Hides the existing first-person flash effect routes. Saved separately for Halo 4.");
            ImGui::TextWrapped("Keep enabled if flashes do not align with your gun. Changing this does not change bullet aim.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Visible hand positioning##section", ImGuiTreeNodeFlags_None))
        {
        ImGui::TextDisabled("Offsets follow each hand's local axes. Guns and interactions keep their positions.");
        changed |= vr_menu::SliderFloat("Left hand X (m)", &g_config.left_hand_mesh_x_m, -.20f, .20f, "%.3f");
        changed |= vr_menu::SliderFloat("Left hand Y (m)", &g_config.left_hand_mesh_y_m, -.20f, .20f, "%.3f");
        changed |= vr_menu::SliderFloat("Left hand Z (m)", &g_config.left_hand_mesh_z_m, -.20f, .20f, "%.3f");
        changed |= vr_menu::SliderFloat("Right hand X (m)", &g_config.right_hand_mesh_x_m, -.20f, .20f, "%.3f");
        changed |= vr_menu::SliderFloat("Right hand Y (m)", &g_config.right_hand_mesh_y_m, -.20f, .20f, "%.3f");
        changed |= vr_menu::SliderFloat("Right hand Z (m)", &g_config.right_hand_mesh_z_m, -.20f, .20f, "%.3f");
        if (ImGui::SmallButton("Reset visible hand positions"))
        {
            g_config.left_hand_mesh_x_m=0.0f;
            g_config.left_hand_mesh_y_m=0.0f;
            g_config.left_hand_mesh_z_m=0.0f;
            g_config.right_hand_mesh_x_m=0.0f;
            g_config.right_hand_mesh_y_m=0.0f;
            g_config.right_hand_mesh_z_m=0.0f;
            changed=true;
        }
        }
        }

        if (CategorySection section{Cat_Crosshair}; section.visible)
        {
        if (ImGui::CollapsingHeader("Crosshair and bullet direction##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Independent dual-wield trajectories", &g_config.independent_dual_aim);
        ImGui::TextDisabled("Halo 2 and Halo 3: each gun follows its own controller when dual wielding.");
        changed |= ImGui::Checkbox("Aim from the visible gun barrel", &g_config.gun_barrel_aim);
        ImGui::TextDisabled("Uses the visible muzzle position and direction when available.\n"
                            "Gun alignment determines shot direction while this is enabled.");
        changed |= vr_menu::SliderFloat("Crosshair vertical angle (deg)",
            &g_config.gun_pitch_deg, -180.0f, 180.0f, "%.1f");
        changed |= vr_menu::SliderFloat("Crosshair horizontal angle (deg)",
            &g_config.gun_yaw_deg, -180.0f, 180.0f, "%.1f");
        changed |= vr_menu::SliderFloat("Aim roll (deg)",
            &g_config.gun_roll_deg, -180.0f, 180.0f, "%.1f");
        if (ImGui::SmallButton("Reset crosshair direction"))
        {
            g_config.gun_pitch_deg = 0.0f;
            g_config.gun_yaw_deg = 0.0f;
            g_config.gun_roll_deg = 0.0f;
            changed = true;
        }
        ImGui::TextDisabled("Adjusts the aiming ray used by shots and the reticle, even when hidden.\n"
                            "Visual gun alignment and gun-stock offsets are in Weapon & Aim.");
        ImGui::Spacing();
        }
        if (ImGui::CollapsingHeader("Authored weapon crosshair (stereo)##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Show a crosshair where the weapon shoots", &g_config.crosshair);
        if (g_config.crosshair)
        {
            float crosshairSmoothPercent = g_config.aim_stabilization * 100.0f;
            if (vr_menu::SliderFloat("Crosshair smoothing", &crosshairSmoothPercent,
                                   0.0f, 95.0f, "%.0f%%", ImGuiSliderFlags_None))
            {
                g_config.aim_stabilization = crosshairSmoothPercent / 100.0f;
                changed = true;
            }
            {
                // Size, opacity and brightness are each game's own: say whose are on the sliders.
                ImGui::Spacing();
                ImGui::Text("Adjusting crosshair size, opacity and brightness for: %s",
                            Config_TitleProfileName(Config_ShownTitleProfile()));
                if (Config_ActiveTitleProfile() < 0)
                    ImGui::TextDisabled("No game is running, so %s's are shown.\n"
                                        "Each game keeps its own: be in a game to adjust that game's crosshair.",
                                        Config_TitleProfileName(kTitleProfileReach));
                changed |= vr_menu::SliderFloat("Crosshair size (deg)", &g_config.crosshair_size_deg,
                                              0.3f, 20.0f, "%.1f");
                // The shown game's own defaults (kCrosshairStarts): what the Defaults button restores.
                const float defaultOpacity = kCrosshairStarts[Config_ShownTitleProfile()].opacity;
                const float defaultBrightness = TitleTunables{}.crosshair_brightness;
                float opacityPercent = g_config.crosshair_opacity * 100.0f;
                float brightnessPercent = g_config.crosshair_brightness * 100.0f;
                if (vr_menu::SliderFloat("Crosshair opacity", &opacityPercent, 0.0f, 200.0f, "%.0f%%",
                                       ImGuiSliderFlags_None))
                {
                    g_config.crosshair_opacity = opacityPercent / 100.0f;
                    changed = true;
                }
                if (vr_menu::SliderFloat("Crosshair brightness", &brightnessPercent, 0.0f, 200.0f, "%.0f%%",
                                       ImGuiSliderFlags_None))
                {
                    g_config.crosshair_brightness = brightnessPercent / 100.0f;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("100% / 100%##crosshairtint"))
                {
                    g_config.crosshair_opacity = g_config.crosshair_brightness = 1.0f;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Defaults##crosshairtint"))
                {
                    g_config.crosshair_opacity = defaultOpacity;
                    g_config.crosshair_brightness = defaultBrightness;
                    changed = true;
                }
                ImGui::TextDisabled("100%% is the crosshair as the game draws it. Opacity above 100%% makes a\n"
                                    "faint crosshair more solid; below, more see-through. Saved for this game.\n"
                                    "Live. (this game's defaults: %.0f%% and %.0f%%)",
                                    defaultOpacity * 100.0f, defaultBrightness * 100.0f);
                ImGui::Spacing();
            }
            changed |= vr_menu::SliderFloat("Crosshair distance (m)", &g_config.crosshair_distance_m,
                                          2.0f, 50.0f, "%.0f");
            ImGui::TextDisabled("Where the crosshair floats when it is not traced (while the aim is\n"
                                "not converged, or no trace has resolved yet).");
            ImGui::Spacing();
            ImGui::Text("Traced crosshair (every game)");
            ImGui::TextDisabled("While your aim converges, the crosshair sits on the surface you aim at,\n"
                                "on the line from your eye along the gun, as in Halo: Campaign Evolved.");
            changed |= vr_menu::SliderFloat("Max crosshair distance (m)", &g_config.reticle_max_dist_m,
                                          1.0f, 50.0f, "%.1f");
            changed |= vr_menu::SliderFloat("Gap in front of surfaces (m)", &g_config.reticle_surface_offset_m,
                                          0.0f, 2.0f, "%.2f");
            changed |= vr_menu::SliderFloat("Shrink-away distance (m)", &g_config.reticle_min_scale_dist_m,
                                          0.0f, std::max(0.1f, g_config.reticle_max_dist_m - 0.1f), "%.2f");
            changed |= vr_menu::SliderFloat("Smallest size up close", &g_config.reticle_min_scale,
                                          0.001f, 1.0f, "%.3f");
            // Keep the pair ordered live, as the loader's clamp does.
            if (g_config.reticle_min_scale_dist_m > g_config.reticle_max_dist_m - 0.1f)
                g_config.reticle_min_scale_dist_m = std::max(0.0f, g_config.reticle_max_dist_m - 0.1f);
            ImGui::TextDisabled("Beyond the max distance, and on sky, it parks there. Nearer than the\n"
                                "shrink-away distance it scales down toward the smallest size, so it\n"
                                "gets out of the way when the gun is against a wall.");
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
                changed |= vr_menu::SliderInt(
                    "Refresh every N frames", &g_config.crosshair_animation_frames,
                    6, 60, "%d");
                ImGui::TextDisabled("Lower = smoother shooting animation and faster\n"
                                    "red/green target colors. Higher = cheaper.\n"
                                    "Turn it off if you feel any frame-rate cost.");
                ImGui::Unindent();
            }
            ImGui::TextWrapped("Single weapons use their authored crosshair where available. "
                "Halo 2 and Halo 3 dual wield use two separate aim marks, one for each weapon.");
#if HALOMCCVR_DEV_DIAGNOSTICS
            ImGui::Spacing();
            {
                static const char* const kAssess[] = {"Off", "On foot", "Everywhere"};
                int assess = g_config.honest_reticle_assess < 0 ? 0 :
                    (g_config.honest_reticle_assess > 2 ? 2 : g_config.honest_reticle_assess);
                if (ImGui::Combo("Assess: marker where the game's shot lands (dev)", &assess, kAssess, 3))
                {
                    g_config.honest_reticle_assess = assess;
                    changed = true;
                }
            }
            ImGui::TextDisabled("DEV BUILD ONLY. A second marker showing where the game's shot lands;\n"
                                "your normal reticle is unchanged. The marker is a small magenta ring\n"
                                "and dot, placed along the game's own aim from its own shot origin (Reach\n"
                                "on foot: the unit's projectile aim, not the kicking camera), so you can\n"
                                "see how far the game trails your hand. Keep aiming with your reticle:\n"
                                "shots, the aim and convergence are exactly the same. Off for normal play.");
#endif
        }
        ImGui::TextDisabled("Crosshair smoothing is visual only; bullets keep the current controller ray.\n"
                            "Set it to 0%% for exact raw tracking.");
        }
        }

        if (CategorySection section{Cat_Scope}; section.visible)
        {
        if (ImGui::CollapsingHeader("Aim Frame Zoom (Halo 3 and ODST)##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Aim Frame Zoom##aimframe", &g_config.aim_frame_zoom);
        ImGui::TextDisabled("While the game is zoomed, the headset shows the game's zoomed picture as a frame\n"
                            "centred on your aim instead of the 3D view, like playing it on a flat screen. The\n"
                            "zoom button reaches the game, so the lens below is not offered there while this\n"
                            "is on. Halo 3 (MCC and Project Reclaimer) and ODST. On by default. Live.");
        if (g_config.aim_frame_zoom)
        {
            ImGui::Indent();
            changed |= ImGui::Checkbox("3D in the frame##aimframe", &g_config.aim_frame_stereo);
            ImGui::TextDisabled("ON (default): each eye sees its own picture of the zoomed view. OFF: one flat\n"
                                "picture for both eyes, drawn once.");
            if (g_config.aim_frame_stereo)
            {
                changed |= vr_menu::SliderFloat("3D strength##aimframe", &g_config.aim_frame_3d_strength,
                                              1.0f, 10.0f, "%.1fx");
                ImGui::TextDisabled("The frame's eye spacing as a multiple of yours. Far targets show little depth\n"
                                    "at 1x; what you aim at stays at the frame's depth, with the crosshair.");
            }
            changed |= vr_menu::SliderFloat("Frame size, first zoom (deg)##aimframe",
                                          &g_config.aim_frame_size_deg[0], 10.0f, 120.0f, "%.0f");
            changed |= vr_menu::SliderFloat("Frame size, second zoom (deg)##aimframe",
                                          &g_config.aim_frame_size_deg[1], 10.0f, 120.0f, "%.0f");
            changed |= vr_menu::SliderFloat("Zoom strength, first zoom##aimframe",
                                          &g_config.aim_frame_strength[0], 0.5f, 4.0f, "%.2fx");
            changed |= vr_menu::SliderFloat("Zoom strength, second zoom##aimframe",
                                          &g_config.aim_frame_strength[1], 0.5f, 4.0f, "%.2fx");
            ImGui::TextDisabled("Strength 1.00 shows the game's own zoom; higher narrows it further.\n"
                                "A frame smaller than the game's zoomed view makes the zoom feel stronger.");
            changed |= vr_menu::SliderFloat("Move time (s)##aimframe", &g_config.aim_frame_move_s,
                                          0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("How long the frame takes to grow out of the view and back. 0 = at once.");
            ImGui::Unindent();
        }
        }
        if (ImGui::CollapsingHeader("Gun-mounted circular zoom lens##section", ImGuiTreeNodeFlags_None))
        {
        ImGui::TextDisabled("Halo 2, Halo 3, ODST, Reach and Halo 4. CE uses native zoom.");
        if (ImGui::Checkbox("Enable R3 zoom lens", &g_config.scope_enabled))
        {
            changed = true;
            if (!g_config.scope_enabled)
                VR_SetScopeActive(false);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(VR_IsScopeActive() ? "[R3: visible]" : "[R3: hidden]");
        ImGui::TextDisabled("Release R3 to toggle the lens; right-stick up/down adjusts zoom.\n"
                            "The main headset view stays wide.");
        if (g_config.scope_enabled)
        {
            ImGui::Indent();
            bool converge = g_config.scope_converge != 0;
            if (ImGui::Checkbox("Converge on the reticle", &converge))
            { g_config.scope_converge = converge ? 1 : 0; changed = true; }
            ImGui::TextDisabled("ON: the lens looks from your eye along your aim line, through the point your\n"
                                "shot converges on, so the reticle's point is its centre even when you stand\n"
                                "away from the character. OFF: the earlier lens, along the game's aim.\n"
                                "Needs Converged aim, and Direct drive in Reach, Halo 3, ODST and Halo 4\n"
                                "(Weapon & Aim): with the stick aim the shot still trails your hand, so the\n"
                                "lens keeps showing the game's aim. Live.");
            changed |= vr_menu::SliderFloat("Default scope zoom", &g_config.scope_zoom,
                                          6.0f, 24.0f, "%.2fx");
            changed |= vr_menu::SliderFloat("Lens diameter (m)##scope",
                                          &g_config.scope_screen_width_m,
                                          0.04f, 0.25f, "%.3f");
            changed |= vr_menu::SliderFloat("Screen right offset (m)",
                                          &g_config.scope_screen_right_m,
                                          -0.30f, 0.30f, "%.3f");
            changed |= vr_menu::SliderFloat("Screen up offset (m)",
                                          &g_config.scope_screen_up_m,
                                          -0.20f, 0.30f, "%.3f");
            changed |= vr_menu::SliderFloat("Screen forward offset (m)",
                                          &g_config.scope_screen_forward_m,
                                          0.05f, 0.80f, "%.3f");
            ImGui::TextDisabled("Offsets are direct gun-local meters with no hidden added distance.");
            ImGui::TextDisabled("The lens refreshes every frame for a consistent render workload.");
            ImGui::Unindent();
        }
        }
        }

        if (CategorySection section{Cat_BodyHands}; section.visible)
        {
        if (ImGui::CollapsingHeader("Body (VRIK)##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Arm IK (bend arm to controller)", &g_config.arm_ik);
        ImGui::TextWrapped("Bend supported arm models toward the tracked hands. When a model cannot be solved, keep the working hands and weapons; some titles hide the unsupported arms.");
        if (g_config.arm_ik)
        {
            changed |= vr_menu::SliderFloat("Right shoulder drop", &g_config.right_shoulder_drop,
                                          0.0f, 0.3f, "%.3f");
            ImGui::TextDisabled("Lowers Chief's right arm so it doesn't clip your face.\n"
                                "Raise until the right shoulder matches your left.");
            changed |= ImGui::Checkbox("Level shoulders (don't pitch with head)",
                                       &g_config.shoulder_level);
            ImGui::TextDisabled("ON: shoulders stay level with the horizon when you look up/down.\n"
                                "OFF: shoulders ride your head pitch (old). Hand+gun unaffected.");
        }
        ImGui::Spacing();
        bool showArms=!g_config.floating_hands;
        if(ImGui::Checkbox("Show arms with hands",&showArms)) {
            g_config.floating_hands=!showArms;changed=true;
        }
        ImGui::TextWrapped("Keep the tracked hands and weapons visible, with optional articulated arms. Turn off to hide the arms. Custom rigs need a recognized arm chain.");
        if(TitleAdapter_GetActiveTitle()==GameTitle::Halo2)
            ImGui::TextWrapped("Halo 2 arm tracking is still in development; the tracked hands remain available.");
        ImGui::Spacing();
        changed |= ImGui::Checkbox("World collision (experimental)",
                                   &g_config.world_collision);
        ImGui::TextDisabled("Hands and held weapons stop on the world and pulse gently on contact.\n"
                            "Available in Halo CE, Halo 2, Halo 3, ODST, Reach, and Halo 4.");
        changed |= ImGui::Checkbox("True physical melee (experimental)",
                                   &g_config.physical_melee);
        ImGui::TextDisabled("Strike a target with either hand or its held gun. Works with world collision on or off.");
        changed |= ImGui::Checkbox("Gesture melee (experimental)", &g_config.gesture_melee);
        ImGui::TextDisabled("A fast swing activates the game's configured melee binding.");
        ImGui::TextDisabled("True physical melee takes priority when both modes are selected.");
        if (g_config.physical_melee || g_config.gesture_melee)
        {
            changed |= vr_menu::SliderFloat(
                "Required swing speed", &g_config.physical_melee_swing_speed,
                kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax, "%.2f m/s");
            ImGui::TextDisabled("Lower is more sensitive; 5.00 m/s is the default and needs a fast swing.");
        }
        ImGui::Spacing();
        changed |= ImGui::Checkbox("Show full body", &g_config.body_wip);
        ImGui::TextWrapped("Legacy body visibility can affect Halo 3's first-person hands. Tracked full-body IK takes priority over this option. Native body visibility in other games is still in development.");
        changed |= ImGui::Checkbox("Tracked full-body IK (experimental)", &g_config.experimental_body_ik);
        ImGui::TextWrapped("Experimental avatars: Halo 3 and Halo 4. Other games currently use their tracked first-person hands. Free-hand gestures use physical trigger and grip input; held weapons keep their normal grip.");
        if (g_config.experimental_body_ik)
        {
            changed |= ImGui::Checkbox("Hide lower body", &g_config.body_ik_hide_lower);
            ImGui::TextWrapped("Lower-body hiding requires separate leg geometry. Unsupported rigs retain first-person hands. Halo 3 Elite/Arbiter full avatars currently require this option off.");
        }
        }
        }

        if (CategorySection section{Cat_Hud}; section.visible)
        {
        if (ImGui::CollapsingHeader("Motion tracker##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Motion tracker follows your head", &g_config.tracker_follows_head);
        ImGui::TextDisabled("ON: contacts are placed by where your head faces, so they match what you see:\n"
                            "sweeping the gun moves nothing, turning your head turns them.\n"
                            "OFF: by where your gun points, as the game draws it.\n"
                            "Halo: Reach, Halo 3, Halo 4, Halo 2 and Halo: CE (ODST has no motion tracker). Live.\n"
                            "Halo 4 starts each level on the game's own tracker and changes over once it has\n"
                            "checked that the heading it replaces follows your aim: a few seconds on foot with\n"
                            "Direct drive on (Weapon & Aim). Not yet seen in a headset.\n"
                            "Halo 2 does the same, at your first aim movement in a level. Not yet seen in a headset.\n"
                            "Halo: CE follows your head from about a second into a level (CE's own tracker turns\n"
                            "with the turn stick only, never with the gun). Not yet seen in a headset.");
        changed |= ImGui::Checkbox("Motion tracker dots follow HUD height",
                                   &g_config.tracker_blips_follow_hud_height);
        ImGui::TextDisabled("ON: the tracker's dots are drawn on the tracker wherever HUD height puts it.\n"
                            "OFF: where the game draws them: below the tracker by the HUD height\n"
                            "(a quarter of the tracker's radius at the default height), your own dot included.\n"
                            "Halo 3 only. Live.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Waypoints##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Markers use the headset's field of view", &g_config.nav_markers_on_target);
        ImGui::TextDisabled("ON: objective markers are projected with the field of view each eye is drawn with,\n"
                            "so a marker stays on its point when you turn your head.\n"
                            "OFF: the game's flat-screen field of view, as before (about twice as far off-centre).\n"
                            "Halo: Reach only. Markers on other players are untested. Live.");
        changed |= ImGui::Checkbox("Off-screen arrows stay in view", &g_config.nav_marker_arrow_in_view);
        ImGui::TextDisabled("ON: the arrow for an objective marker or a marked player you are not looking at is\n"
                            "drawn within 30 degrees of where your head faces, in one place for both eyes.\n"
                            "OFF: the game's own ring, far enough out that only one eye sees the arrow.\n"
                            "Covers objective markers and the markers the HUD puts on objects and players.\n"
                            "Does not touch vehicle and lock-on reticles or damage arrows.\n"
                            "Halo: Reach only. Takes effect at the next level.");
        if (g_config.nav_marker_arrow_in_view)
        {
            float ringPercent = g_config.nav_marker_ring * 100.0f;
            if (vr_menu::SliderFloat("Off-screen arrow distance from centre", &ringPercent,
                                   15.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_None))
            {
                g_config.nav_marker_ring = ringPercent / 100.0f;
                changed = true;
            }
            ImGui::TextDisabled("How far from the middle of your view the arrows sit, as a share of what both eyes\n"
                                "see. Never more than 30 degrees, so the top of the slider may change nothing.\n"
                                "Lower it until you can see them. Live. (default 75%%)");
        }
        ImGui::Spacing();
        ImGui::Text("Player tags");
        changed |= ImGui::Checkbox("Show player tags", &g_config.player_tags_show);
        ImGui::TextDisabled("The service tag and arrow over each teammate. Objective markers and flag, ball and\n"
                            "bomb carriers are not changed. Halo 3 (MCC and Project Reclaimer) and ODST. Live.");
        if (g_config.player_tags_show)
        {
            float tagScalePercent = g_config.player_tags_scale * 100.0f;
            if (vr_menu::SliderFloat("Player tag size", &tagScalePercent, 25.0f, 200.0f, "%.0f%%",
                                     ImGuiSliderFlags_None))
            {
                g_config.player_tags_scale = tagScalePercent / 100.0f;
                changed = true;
            }
            float tagOpacityPercent = g_config.player_tags_opacity * 100.0f;
            if (vr_menu::SliderFloat("Player tag opacity", &tagOpacityPercent, 0.0f, 100.0f, "%.0f%%",
                                     ImGuiSliderFlags_None))
            {
                g_config.player_tags_opacity = tagOpacityPercent / 100.0f;
                changed = true;
            }
            changed |= vr_menu::SliderFloat("Player tag distance", &g_config.player_tags_max_distance_m, 0.0f, 300.0f,
                                            g_config.player_tags_max_distance_m > 0.0f ? "%.0f m" : "no limit",
                                            ImGuiSliderFlags_None);
            ImGui::TextDisabled("Tags farther away than the distance fade out over the last 5 m. 0 = no limit.\n"
                                "(defaults 100%%, 100%%, no limit)");
        }
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("HUD layout##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Hide HUD completely", &g_config.hide_hud);
        if(g_config.hide_hud) {
            changed|=ImGui::Checkbox("Reveal with hand near head",&g_config.hud_reveal_near_head);
            if(g_config.hud_reveal_near_head) {
                changed|=vr_menu::SliderFloat("HUD reveal distance (m)",&g_config.hud_reveal_radius_m,.10f,.40f,"%.2f");
                ImGui::TextWrapped("Bring your empty support hand near your headset for a moment. Move it away to hide the HUD again. Gripping, dual wielding, reloading and menus cancel the gesture.");
            }
        }
        // Size, width, curvature and height are each game's own: say whose are on the sliders.
        ImGui::Spacing();
        ImGui::Text("Adjusting HUD size, width, curvature and height for: %s",
                    Config_TitleProfileName(Config_ShownTitleProfile()));
        if (Config_ActiveTitleProfile() < 0)
            ImGui::TextDisabled("No game is running, so %s's are shown.\n"
                                "Each game keeps its own: be in a game to adjust that game's HUD.",
                                Config_TitleProfileName(kTitleProfileReach));
        changed |= vr_menu::SliderFloat("HUD size", &g_config.hud_size, 0.30f, 1.00f, "%.2f");
        changed |= vr_menu::SliderFloat("HUD width / aspect", &g_config.hud_aspect,
                                      kHudAspectMin, kHudAspectMax, "%.2f");
        const bool halo2NativeHud = Game_UsesHalo2NativeHudLayout();
        const bool reachNativeHud =
            TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
        const bool ceNativeHud = TitleAdapter_GetActiveTitle() == GameTitle::HaloCE;
        const bool noLiveHudCurvature = halo2NativeHud || reachNativeHud || ceNativeHud;
        if (noLiveHudCurvature)
            ImGui::BeginDisabled();
        changed |= vr_menu::SliderFloat("HUD curvature", &g_config.hud_curvature,
                                      kHudCurvatureMin, kHudCurvatureMax, "%.2f");
        if (noLiveHudCurvature)
            ImGui::EndDisabled();
        changed |= vr_menu::SliderFloat("HUD height", &g_config.hud_vertical_offset,
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
        if (halo2NativeHud)
            ImGui::TextDisabled("Halo 2 uses its native per-eye HUD path and has no separate curvature basis here.");
        else if (reachNativeHud)
            ImGui::TextDisabled("Reach HUD size, width and height are adjustable; curvature is baked when its tag loads.");
        else if (ceNativeHud)
            ImGui::TextDisabled("Halo CE HUD size, width and height are adjustable in both graphics modes; curvature stays flat.");
        else
            ImGui::TextDisabled("Curvature: 0.00 = flat (+0.30), 1.00 = curved (-0.30); 0.50 is authored.");
        ImGui::TextDisabled("Height: positive raises the HUD, negative lowers it; the aiming reticle stays fixed.");
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        {
            ImGui::Spacing();
            changed |= ImGui::Checkbox(
                "Show Halo 4 helmet frame", &g_config.halo4_helmet);
            ImGui::TextDisabled(
                "Keeps or hides only the authored Mjolnir helmet/visor shading; the rest of the HUD stays visible.\n"
                "In the campaign the shading is a dark band above the HUD and a larger one below it, in the\n"
                "middle of your view. OFF (default) leaves the visor's thin outline. Live.");
        }
        }
        }

        if (CategorySection section{Cat_Picture}; section.visible)
        {
        if (ImGui::CollapsingHeader("Rendering##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Mono rendering: one picture for both eyes", &g_config.mono_rendering);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The game is drawn once a frame instead of once for each eye, and both eyes see that\n"
                              "one picture. You lose 3D depth.\n"
                              "For very large maps that will not hold the headset's frame rate in 3D.\n"
                              "The crosshair and the scope move far away so they do not double.\n"
                              "Every game; Halo: CE with its Anniversary graphics is always drawn in 3D.\n"
                              "Not on headsets with angled displays, in the cutscene theatre, or with DLSS.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Headset picture resolution##section", ImGuiTreeNodeFlags_None))
        {
        // In a host that draws the game at its own size (Project Reclaimer) the scale sets that size while the
        // host's own Render resolution is Native, from its next start (d3d11_hook.cpp, HostEnumDisplaySettingsShim).
        if (g_hostOwnsRenderSize)
            ImGui::TextWrapped("In Project Reclaimer this sets the game's size while Project Reclaimer's own "
                               "Settings > Graphics > Render resolution is Native. Its other choices keep "
                               "their own size. A change needs a restart of Project Reclaimer.");
        changed |= vr_menu::SliderFloat("Resolution scale", &g_config.resolution_scale,
                                      kResolutionScaleMin, kResolutionScaleMax, "%.2fx");
        if (g_hostOwnsRenderSize && ImGui::IsItemHovered())
            ImGui::SetTooltip("Project Reclaimer reads its size only as it starts:\n"
                              "restart it for a change to take effect.");
        // Same even-rounding the launcher applies, so this is the exact render
        // size the next launch will ask MCC for.
        auto scaleEven = [](int base, float scale) {
            int value = (int)lroundf((float)base * scale);
            return (value & 1) ? value + 1 : value;
        };
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
        int nextWidth=scaleEven(kNativeRenderWidth,g_config.resolution_scale);
        int nextHeight=scaleEven(kNativeRenderHeight,g_config.resolution_scale);
        // Project Reclaimer is given the size fitted into what it accepts (d3d11_hook.cpp, HostWantRenderSize).
        if(g_hostOwnsRenderSize)
        {
            unsigned fitW=(unsigned)nextWidth,fitH=(unsigned)nextHeight;
            if(reclaimer_host::FitNativeRenderSize(fitW,fitH)) {nextWidth=(int)fitW;nextHeight=(int)fitH;}
        }
        ImGui::TextDisabled("Requested picture: %d x %d per eye",nextWidth,nextHeight);
        VrDlssRenderPlan renderPlan{};
        if(VR_GetDlssRenderPlan(renderPlan))
        {
            ImGui::TextDisabled("Current game render: %u x %u",renderPlan.liveRenderW,renderPlan.liveRenderH);
            ImGui::TextDisabled("Planned game render: %u x %u | Output: %u x %u",
                renderPlan.plannedRenderW,renderPlan.plannedRenderH,renderPlan.outputW,renderPlan.outputH);
        }
        unsigned actualW=0,actualH=0;
        const int resizeState=D3D_LiveResizeState(actualW,actualH);
        if(g_hostOwnsRenderSize)
        {
            // What its Native read is given: no larger than its display while fitting is on, and its display's own
            // size in exclusive fullscreen (d3d11_hook.cpp, HostEnumDisplaySettingsShim).
            changed |= ImGui::Checkbox("Show later games in the headset",&g_config.reclaimer_share_device);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Lets a game Project Reclaimer starts after the first (ODST, after Halo 3's menu) "
                                  "reach the headset, by giving it the headset's graphics device.\n"
                                  "Off: that game freezes in the headset. Applies the next time a game starts.");
            changed |= ImGui::Checkbox("Fit to my monitor",&g_config.reclaimer_fit_to_display);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Keeps Project Reclaimer's picture no larger than your monitor. A picture larger\n"
                                  "than the screen is suspected of stopping the graphics card on some PCs. Off gives\n"
                                  "the full size above, which is sharper in the headset when your monitor is smaller.\n"
                                  "Applies at Project Reclaimer's next start.");
            unsigned hostW=(unsigned)nextWidth,hostH=(unsigned)nextHeight;
            int clientModeValue=0;
            D3D_HostNativeRenderAnswer((unsigned)nextWidth,(unsigned)nextHeight,hostW,hostH,clientModeValue);
            const auto clientMode=static_cast<reclaimer_host::ClientDisplayMode>(clientModeValue);
            if(clientMode==reclaimer_host::ClientDisplayMode::Windowed)
                ImGui::TextWrapped("Project Reclaimer is windowed: there it renders at its Render resolution preset, "
                                   "or 1920 x 1080 on Native. For the size above, set its Display mode to Borderless "
                                   "and its Render resolution to Native, then restart it.");
            else if(clientMode==reclaimer_host::ClientDisplayMode::ExclusiveFullscreen)
                ImGui::TextWrapped("Project Reclaimer is in exclusive fullscreen: there it renders at your display's "
                                   "own size. Borderless is the display mode for VR.");
            else if(hostW!=(unsigned)nextWidth||hostH!=(unsigned)nextHeight)
                ImGui::TextWrapped("Fitted to your monitor: Project Reclaimer renders %u x %u.",hostW,hostH);
            if(!renderPlan.liveRenderW) ImGui::TextWrapped("Project Reclaimer has not drawn a frame yet.");
            else if(clientMode==reclaimer_host::ClientDisplayMode::Windowed||
                    clientMode==reclaimer_host::ClientDisplayMode::ExclusiveFullscreen)
                ImGui::TextWrapped("It renders %u x %u now.",renderPlan.liveRenderW,renderPlan.liveRenderH);
            else if(renderPlan.liveRenderW!=hostW||renderPlan.liveRenderH!=hostH)
            {
                ImGui::TextColored(Rgb(kWarning),"Restart Project Reclaimer to apply this resolution.");
                ImGui::TextWrapped("It renders %u x %u now. It takes the size above when it starts in Borderless "
                                   "with its Render resolution on Native.",renderPlan.liveRenderW,
                                   renderPlan.liveRenderH);
            }
            else ImGui::TextWrapped("Project Reclaimer renders at this size.");
        }
        else if(resizeState==1) ImGui::TextDisabled("Applying resolution after the current frame...");
        else if(resizeState==3) {
            ImGui::TextWrapped("Live resize could not complete. The current native render size was retained.");
            if(ImGui::Button("Retry resolution change")) D3D_RetryLiveResize();
        }
        else if(!D3D_CanLiveResize()) ImGui::TextWrapped("Live resolution hooks are unavailable; the current native render size is retained.");
        else ImGui::TextWrapped("Render size can update live; title loading defers the change. Lower values trade detail for speed; higher values supersample.");
        if (g_config.resolution_scale > kResolutionScaleHeavy)
            ImGui::TextColored(Rgb(kWarning),
                               "[!] Very heavy (~5K and up): can crash weaker GPUs. Test in\n"
                               "    short sessions and drop this if the game won't start.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Optional temporal upscaling##section", ImGuiTreeNodeFlags_None))
        {
        changed|=ImGui::Combo("Upscaler",&g_config.upscaler,"Off\0NVIDIA DLSS (experimental)\0");
        if(g_config.upscaler)
        {
            changed|=ImGui::Combo("DLSS quality",&g_config.dlss_mode,"DLAA\0Quality\0Balanced\0Performance\0Ultra Performance\0");
            changed|=ImGui::Combo("DLSS model",&g_config.dlss_preset,"Runtime default\0F (CNN)\0J\0K\0L\0M\0E (CNN)\0");
            changed|=ImGui::Checkbox("Temporal jitter",&g_config.dlss_jitter);
            char status[512]{};VR_GetDlssStatus(status,sizeof(status));
            ImGui::TextWrapped("%s",status);
            ImGui::TextWrapped("Requires a compatible NVIDIA GPU. Camera-based motion can leave trails on moving objects. "
                "Available across all six games, including both CE and Halo 2 graphics modes. "
                "If a game's depth or camera data is unavailable, the ordinary full-resolution view is restored.");
        }
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Image quality (applies live, every title)##section", ImGuiTreeNodeFlags_None))
        {
        const char* upscaleItems[] = {"Linear (old)", "Sharp (strong bicubic)"};
        changed |= ImGui::Combo("Upscale filter", &g_config.upscale_filter,
                                upscaleItems, 2);
        ImGui::TextDisabled("How the game image is scaled to your headset. The game usually\n"
                            "renders BELOW your per-eye headset resolution, so this upscales it.\n"
                            "Sharp keeps edges crisp; Linear is the old soft/shimmery look.");
        changed |= vr_menu::SliderFloat("Sharpening", &g_config.sharpness, 0.0f, 1.0f, "%.2f");
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
        }
        if (ImGui::CollapsingHeader("Scene##section", ImGuiTreeNodeFlags_None))
        {
        const auto pictureTitle=TitleAdapter_GetActiveTitle();
        const int bloomTitle=pictureTitle==GameTitle::Halo3?0:
            pictureTitle==GameTitle::Halo3ODST?1:pictureTitle==GameTitle::HaloReach?2:-1;
        if(bloomTitle>=0)
        {
            const bool bloomWanted=Config_BloomWanted(unsigned(bloomTitle));
            if(pictureTitle==GameTitle::Halo3) {
                bool on=bloomWanted;
                if(ImGui::Checkbox("Bloom in this game",&on)) {
                    g_config.bloom_enabled[bloomTitle]=on;g_config.bloom_chosen[bloomTitle]=true;changed=true;
                }
            } else {
                bool disabled=!bloomWanted;
                if(ImGui::Checkbox("Disable bloom",&disabled)) {
                    g_config.bloom_enabled[bloomTitle]=!disabled;g_config.bloom_chosen[bloomTitle]=true;changed=true;
                }
                if(!g_config.bloom_chosen[bloomTitle])
                    ImGui::TextWrapped("Off in VR until you choose. Your saved bloom setting is kept as it was.");
            }
            if(pictureTitle!=GameTitle::Halo3)
                ImGui::TextWrapped("Restart the game for bloom to be properly disabled.");
            ImGui::TextWrapped("Bloom is saved separately for each game.");
            if(pictureTitle==GameTitle::Halo3ODST) {
                changed|=ImGui::Checkbox("Disable autoexposure",&g_config.disable_autoexposure_odst);
                ImGui::TextWrapped("Lowering the brightness is highly recommended when autoexposure is disabled.");
            }
        }
        ImGui::Text("Adjusting brightness for: %s", Config_TitleProfileName(Config_ActiveTitleProfile()));
        changed |= vr_menu::SliderFloat("Game brightness", &g_config.game_brightness, 0.5f, 2.0f, "%.2f");
        ImGui::TextDisabled("Brightens/darkens the whole game. 1.0 = the game's own brightness.\n"
                            "Saved separately for each game.");
        changed |= ImGui::Checkbox("Motion blur", &g_config.motion_blur);
        ImGui::TextDisabled("Off is the VR standard. In stereo the game's blur is fed the wrong\n"
                            "previous frame and smears bright edges into repeating echoes.");
        changed |= ImGui::Checkbox("Disable CE Anniversary lens flares",
                                   &g_config.ce_anniversary_disable_lens_flares);
        ImGui::TextDisabled("Optional workaround for bright flare streaks. Off by default.\n"
                            "World lighting and CE Classic graphics stay unchanged.");
        changed |= vr_menu::SliderFloat("Draw distance", &g_config.draw_distance,
                                      kDrawDistanceMin, kDrawDistanceMax, "%.2f");
        ImGui::TextDisabled("1.00 = full stock draw distance. Lower brings the far plane in toward\n"
                            "you, culling distant terrain/objects (skybox goes first). Most levels\n"
                            "only start culling below ~0.25; the lowest settings clip near geometry\n"
                            "(hard pop-in) for the most frames. Live, all three games.");
        ImGui::Spacing();
        ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Weather (Halo: Reach honours these today)##section", ImGuiTreeNodeFlags_None))
        {
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
        }

        if (CategorySection section{Cat_Subtitles}; section.visible)
        {
            const auto subtitleTitle=TitleAdapter_GetActiveTitle();
            const bool gameplayFeed=subtitleTitle==GameTitle::Halo3 ||
                subtitleTitle==GameTitle::Halo3ODST || subtitleTitle==GameTitle::HaloReach ||
                subtitleTitle==GameTitle::Halo2 || subtitleTitle==GameTitle::Halo4;
            if(gameplayFeed)
            {
            changed|=ImGui::Checkbox("Gameplay subtitles",&g_config.vr_gameplay_subtitles);
            if(g_config.vr_gameplay_subtitles)
            {
                changed|=vr_menu::SliderFloat("Gameplay text scale",&g_config.vr_gameplay_subtitle_scale,.5f,2.5f,"%.2fx");
                changed|=vr_menu::SliderFloat("Gameplay horizontal position",&g_config.vr_gameplay_subtitle_x,-1.f,1.f,"%.2f");
                changed|=vr_menu::SliderFloat("Gameplay vertical position",&g_config.vr_gameplay_subtitle_y,-1.f,1.f,"%.2f");
                changed|=ImGui::Combo("Gameplay anchor",&g_config.vr_gameplay_subtitle_anchor,"Follow head\0Stay in world\0");
            }
            }
            else ImGui::TextWrapped("Gameplay subtitle placement is available in Halo 2, Halo 3, ODST, Reach and Halo 4.");
            ImGui::Separator();
            changed|=ImGui::Checkbox("Theatre subtitles",&g_config.cutscene_theater_subtitles);
            if(g_config.cutscene_theater_subtitles && gameplayFeed)
            {
                changed|=vr_menu::SliderFloat("Theatre text scale",&g_config.vr_theatre_subtitle_scale,.5f,2.5f,"%.2fx");
                changed|=vr_menu::SliderFloat("Theatre horizontal position",&g_config.vr_theatre_subtitle_x,-1.f,1.f,"%.2f");
                changed|=vr_menu::SliderFloat("Theatre vertical position",&g_config.vr_theatre_subtitle_y,-1.f,1.f,"%.2f");
                changed|=ImGui::Combo("Theatre anchor",&g_config.vr_theatre_subtitle_anchor,"Theatre screen\0Follow head\0");
            }
            else if(g_config.cutscene_theater_subtitles)
                ImGui::TextWrapped("Halo CE keeps its existing subtitle view; native text placement is not yet available.");
            ImGui::TextWrapped("Uses the game's localized dialogue. Native subtitle settings determine which lines the game supplies. "
                "Unsupported native feeds retain the existing subtitle view.");
        }

        if (CategorySection section{Cat_Desktop}; section.visible)
        {
        if (ImGui::CollapsingHeader("The window on your monitor##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::Checkbox("Show the headset's picture on my monitor",&g_config.desktop_mirror);
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Halo 3 and ODST: one eye of the VR picture on the monitor, for streaming or anyone "
                              "watching.\nOff saves one full-screen draw a frame; the monitor then stays on its last "
                              "flat frame while in VR. Live.");
        if(ImGui::Checkbox("Fit desktop window to my monitor",&g_config.fit_desktop_window)) {
            changed=true;
            if(D3D_FitActive()) PostMessageW(D3D_GameWindow(),kFitGameWindowMsg,0,0);
        }
        ImGui::TextDisabled(
            "For monitors SMALLER than your render (e.g. a big headset resolution on\n"
            "a 1080p screen), where MCC's window overflows and you can't click the\n"
            "\"Halo 3\" tile or Quit. The headset keeps the full resolution above; only\n"
            "the desktop window shrinks to fit and the GPU downscales into it (no\n"
            "extra render pass). ON by default. Enabling it fits the current window;\n"
            "disabling it stops further fitting to the monitor.\n"
            "Headset resolution changes work with either setting.");
        }
        }

        if (g_activeCategory == Cat_Telemetry &&
            ImGui::CollapsingHeader("Telemetry Recorder##section", ImGuiTreeNodeFlags_None))
        {
        const TelemetryStatusSnapshot telemetry = Telemetry_GetStatus();
        const bool active = telemetry.state == TelemetryRecorderState::Starting ||
            telemetry.state == TelemetryRecorderState::Recording;
        const bool busy = telemetry.state == TelemetryRecorderState::Finalizing;
        ImGui::Text("Status: %s", Telemetry_StateName(telemetry.state));
        if ((active || busy) && telemetry.sessionStartQpc > 0 &&
            telemetry.qpcFrequency > 0 &&
            telemetry.qpcNow >= telemetry.sessionStartQpc)
        {
            const double seconds = static_cast<double>(
                telemetry.qpcNow - telemetry.sessionStartQpc) /
                static_cast<double>(telemetry.qpcFrequency);
            ImGui::Text("Duration: %.1f s", seconds);
        }
        ImGui::Text("Producer calls: %llu",
            static_cast<unsigned long long>(telemetry.producerCalls));
        ImGui::Text("Enqueued: %llu",
            static_cast<unsigned long long>(telemetry.enqueued));
        ImGui::Text("Written: %llu",
            static_cast<unsigned long long>(telemetry.written));
        ImGui::Text("Dropped (queue full): %llu",
            static_cast<unsigned long long>(telemetry.droppedQueueFull));
        ImGui::Text("Weapon events: enq %llu / written %llu / dropped %llu",
            static_cast<unsigned long long>(telemetry.weaponEventsEnqueued),
            static_cast<unsigned long long>(telemetry.weaponEventsWritten),
            static_cast<unsigned long long>(
                telemetry.weaponEventsDroppedQueueFull));
        ImGui::Text("Duplicates suppressed: %llu",
            static_cast<unsigned long long>(
                telemetry.duplicateSerialSuppressed));
        if (telemetry.state == TelemetryRecorderState::Error ||
            telemetry.state == TelemetryRecorderState::Unavailable)
        {
            ImGui::TextColored(Rgb(kWarning), "Recorder: %s (system %u)",
                Telemetry_ErrorName(telemetry.error), telemetry.systemError);
        }

        if (active)
        {
            if (ImGui::Button("Stop & save"))
                Telemetry_RequestStop();
        }
        else
        {
            ImGui::BeginDisabled(busy ||
                telemetry.state == TelemetryRecorderState::Unavailable);
            if (ImGui::Button("Start recording"))
                Telemetry_RequestStart();
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("One sample per prepared VR frame.");
        ImGui::TextDisabled("Saved in the Telemetry Recordings folder beside HaloMCCVR.log.");
        }

        if (CategorySection section{Cat_Advanced}; section.visible)
        {
        if (ImGui::CollapsingHeader("Co-op safe shots##section", ImGuiTreeNodeFlags_None))
        {
        changed |= ImGui::RadioButton("Automatic##coopsafe", &g_config.coop_safe_shots, 2);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Always##coopsafe", &g_config.coop_safe_shots, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Off##coopsafe", &g_config.coop_safe_shots, 0);
        ImGui::TextDisabled("AUTOMATIC (default): as Always whenever anyone else is in your campaign session, and as Off\n"
                            "when the game says you are alone, or in Firefight or a custom game (the log's CO-OP SAFE\n"
                            "SHOTS automatic line says which). Not Halo CE, where it stays as Always.\n"
                            "ALWAYS: nothing the mod does changes a shot as it is fired, so every player's game\n"
                            "fires it the same way and a campaign co-op or Spartan Ops session stays together. Your\n"
                            "hand still aims; shots leave your eye. In Halo 2 your hand aims only with Direct drive\n"
                            "on, True physical melee presses the game's melee instead of striking on contact (Halo\n"
                            "CE: it does nothing), and seats in Halo CE and ODST and passenger seats in Reach keep\n"
                            "the game's own camera.\n"
                            "OFF is for playing alone: shots can leave the visible muzzle and are re-aimed as they\n"
                            "are fired. With it off, your first shot in a co-op session ends it for everyone. Live.");
        ImGui::Spacing();
        ImGui::Separator();
        changed |= ImGui::Checkbox("Bone probe (diagnostic)", &g_config.weapon_probe);
        ImGui::TextDisabled("Pushes every composed skeleton 1m left to prove writable palette boundaries.");
        changed |= ImGui::Checkbox("Render right eye first (diagnostic)",
                                   &g_config.right_eye_first);

        ImGui::Spacing();
        ImGui::Separator();
        // These flips were the F4/F5/F7 hotkeys. A phantom F4 (SteamVR sends
        // Alt+F4 on its exit path) kept inverting the controls mid-session, so
        // they are reachable only from this menu now.
        }
        if (ImGui::CollapsingHeader("Tracking calibration##section", ImGuiTreeNodeFlags_None))
        {
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
        }
        if (ImGui::CollapsingHeader("Menu panel##section", ImGuiTreeNodeFlags_None))
        {
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
        }
        if (ImGui::CollapsingHeader("Start over##section", ImGuiTreeNodeFlags_None))
        {
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
                two_hand_lab_runtime::ResetSettings();
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
                            "default, including your weapon calibration. The runtime Two-Handed\n"
                            "Lab is also reset and disabled. Render resolution updates live\n"
                            "once its slider settles; loading defers the change.");
        }
        }

        if (g_activeCategory == Cat_TwoHandLab &&
            ImGui::CollapsingHeader("Two-Handed Lab##section", ImGuiTreeNodeFlags_None))
        {
#include "two_hand_lab_menu.inl"
        }

        ImGui::PopTextWrapPos();
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

void Menu_PostLiveResize()
{
    const HWND window=D3D_GameWindow();
    if(!window||!PostMessageW(window,kLiveResizeMsg,0,0))
    {
        D3D_CancelQueuedLiveResize();
        LOG("live resize: window notification unavailable; current dimensions preserved");
    }
}

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
    ApplyTheme();                          // launcher's navy / cyan palette
    ImGui::GetStyle().ScaleAllSizes(1.5f); // legible at panel distance in the headset
    io.FontGlobalScale = 1.6f;
    io.Fonts->AddFontDefault(); // retain familiar, readable body text
    ImFontConfig headingConfig{};
    headingConfig.FontDataOwnedByAtlas = false; // immutable DLL-resident bytes
    g_headingFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(menu_assets::oxanium),
        static_cast<int>(sizeof(menu_assets::oxanium)), 13.0f, &headingConfig);

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

    NativeMenuPointer_Init(gameWindow);
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
