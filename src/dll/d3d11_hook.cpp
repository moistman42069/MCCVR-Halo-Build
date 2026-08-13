#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <climits>
#include <cmath>
#include <intrin.h>
#include <MinHook.h>

#pragma intrinsic(_ReturnAddress)
#include "d3d11_hook.h"
#include "game.h"
#include "vr.h"
#include "title_adapter.h"
#include "../common/config.h"
#include "../common/coop_probe_logic.h"
#include "../common/log.h"
#include "../common/runtime_types.h"

// We can't hook "the game's swapchain" directly because it doesn't exist yet
// when we're injected. Instead we create a throwaway D3D11 device + swapchain
// of our own, read the addresses of Present/ResizeBuffers out of its vtable
// (all swapchains in the process share the same implementation), hook those,
// and throw the dummy away.

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef void(STDMETHODCALLTYPE* OMSetRenderTargetsFn)(ID3D11DeviceContext*, UINT,
    ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef void(STDMETHODCALLTYPE* RSSetViewportsFn)(ID3D11DeviceContext*, UINT,
    const D3D11_VIEWPORT*);
typedef void(STDMETHODCALLTYPE* RSSetScissorRectsFn)(ID3D11DeviceContext*, UINT,
    const D3D11_RECT*);
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
typedef void(STDMETHODCALLTYPE* DrawIndexedFn)(ID3D11DeviceContext*, UINT, UINT, INT);
#endif
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
typedef void(STDMETHODCALLTYPE* CopyResourceFn)(ID3D11DeviceContext*,
    ID3D11Resource*, ID3D11Resource*);
#endif

static PresentFn g_origPresent = nullptr;
static Present1Fn g_origPresent1 = nullptr;
static ResizeBuffersFn g_origResizeBuffers = nullptr;
static OMSetRenderTargetsFn g_origOMSetRenderTargets = nullptr;
static RSSetViewportsFn g_origRSSetViewports = nullptr;
static RSSetScissorRectsFn g_origRSSetScissorRects = nullptr;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
static DrawIndexedFn g_origDrawIndexed = nullptr;
// The July 26 HUD-discovery detour performed synchronous GPU readback and
// logging before DrawIndexed. It produced no HUD/world evidence and remained
// active in every later Reach build. Candidate a50da4a proved removing it does
// not change the black-world defect. It remains disabled because restoring
// blocking GPU readback and file I/O to a render hot hook would be unsafe.
constexpr bool kEnableReachDrawIndexedDiagnostic = false;
#endif
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
static CopyResourceFn g_origCopyResource = nullptr;
#endif

// --- Desktop-window fit (config.fit_desktop_window) -----------------------
// resolution_scale sizes the render the headset captures. On a monitor smaller
// than that render, MCC's window overflows the screen and its menu buttons fall
// off the edge. With the fit on we keep MCC drawing the FULL render (so the
// headset picture and the gun alignment never change) while shrinking only the
// visible window to fit the monitor (menu.cpp); the GPU downscales the full
// backbuffer into the small window on present (flip-model + DXGI_SCALING_STRETCH)
// for free -- no second pass. The hazard is the reverse of the earlier attempt:
// if MCC learns the window shrank it draws a small frame into the corner of the
// big backbuffer (the black-border crop). So we force the backbuffer full at
// creation and keep MCC believing its client is still full-size through its own
// resize handling -- WITHOUT lying to DXGI's present-time client query, which
// must still see the true small window to downscale correctly. Everything below
// is gated on g_config.fit_desktop_window; with it off, none of it is installed.
typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChainForHwndFn)(IDXGIFactory2*, IUnknown*, HWND,
    const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
    IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChainFn)(IDXGIFactory*, IUnknown*,
    DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef BOOL(WINAPI* GetClientRectFn)(HWND, LPRECT);
typedef BOOL(WINAPI* GetWindowRectFn)(HWND, LPRECT);
static CreateSwapChainForHwndFn g_origCreateSwapChainForHwnd = nullptr;
static CreateSwapChainFn g_origCreateSwapChain = nullptr;
static GetClientRectFn g_origGetClientRect = nullptr;
static GetWindowRectFn g_origGetWindowRect = nullptr;
static UINT g_forcedRenderW = 0;
static UINT g_forcedRenderH = 0;
static bool g_forcedMainSwapchain = false; // only force the game's own (first) swapchain
static HWND g_gameHwnd = nullptr;          // captured at swapchain creation
static bool g_fitActive = false;           // set once at startup: fit on AND its hooks installed
// Set on the game's UI thread ONLY while it synchronously processes a WM_SIZE we
// rewrote to the full render size, so GetClientRectHook feeds MCC's own resize
// code the full size on exactly that call stack -- never on the render thread's
// present-time query, which must keep getting the true small client size.
static thread_local bool g_lieClientToGame = false;

void D3D_GetForcedRenderSize(unsigned& width, unsigned& height)
{
    width = g_forcedRenderW;
    height = g_forcedRenderH;
}

bool D3D_FitActive()
{
    return g_fitActive;
}

void D3D_SetForcedClientLie(bool on)
{
    g_lieClientToGame = on;
}

static void InitForcedRenderSize()
{
    wchar_t buf[32];
    if (GetEnvironmentVariableW(L"HALO3XR_RENDER_W", buf, 32) > 0)
        g_forcedRenderW = (UINT)_wtoi(buf);
    if (GetEnvironmentVariableW(L"HALO3XR_RENDER_H", buf, 32) > 0)
        g_forcedRenderH = (UINT)_wtoi(buf);
    if (g_forcedRenderW == 0 || g_forcedRenderH == 0)
    {
        // DLL run without the current launcher: replicate the launcher's
        // ScaleEven so the two can never disagree. Config is loaded before this
        // (dllmain InitThread order).
        const float s = g_config.resolution_scale;
        auto even = [](int base, float sc) -> UINT {
            int v = (int)((float)base * sc + 0.5f);
            if (v & 1) ++v;
            return (UINT)v;
        };
        g_forcedRenderW = even(kNativeRenderWidth, s);
        g_forcedRenderH = even(kNativeRenderHeight, s);
    }
    LOG("fit_desktop_window ON: forcing MCC backbuffer to %ux%u (full headset "
        "render); the desktop window is shrunk to fit the monitor separately",
        g_forcedRenderW, g_forcedRenderH);
}

// Game-executable image range, used to scope the client-rect lie and cursor
// remap to game-side callers (never DXGI/DWM or our own overlay). Populated by
// InitExeRange() at hook install; the accessors are defined with the cursor
// hooks below.
static const BYTE* g_exeBase = nullptr;
static const BYTE* g_exeEnd = nullptr;
static inline bool CallerInExe(const void* ret); // defined with the cursor hooks

// Feed the game the full render size for the game window whenever the caller is
// game-side: MCC's resize code (so it keeps drawing full) AND its menu hit-test
// (which clamps the cursor against the client size -- if that returns the true
// small window while our cursor is scaled up to render space, only client/render
// of the menu is reachable, i.e. the top-left ~31% dead-zone). DXGI/DWM query
// the client at present time from system DLLs (not the game EXE), so they still
// get the real small size and keep downscaling the full frame into the window.
static BOOL WINAPI GetClientRectHook(HWND hwnd, LPRECT rc)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetClientRect(hwnd, rc);
    if (ok && rc && hwnd == g_gameHwnd && g_forcedRenderW && g_forcedRenderH &&
        (g_lieClientToGame || CallerInExe(caller)))
    {
        rc->left = 0;
        rc->top = 0;
        rc->right = (LONG)g_forcedRenderW;
        rc->bottom = (LONG)g_forcedRenderH;
        static int s_log = 0;
        static const void* s_lastCaller = nullptr;
        if (!g_lieClientToGame && s_log < 16 && caller != s_lastCaller &&
            g_exeBase)
        {
            ++s_log;
            s_lastCaller = caller;
            LOG("fit: GetClientRect game-side +0x%llX -> forced %ux%u",
                (unsigned long long)((const BYTE*)caller - g_exeBase),
                g_forcedRenderW, g_forcedRenderH);
        }
    }
    return ok;
}

// UI input/focus layers commonly bound their hittable/navigable region to the
// window rect. Ours is the true small window (e.g. 141,0..1139,720), so MCC
// culls every widget below/right of it -> the menu is fully DRAWN but the lower
// items are dead to mouse, keyboard AND d-pad (the reported symptom). For
// game-side callers, report a window rect at the real top-left but sized to the
// full render, so MCC's input region matches its full-size layout and cursor.
// DXGI/DWM/system callers are untouched, so the physical window and the
// present-time downscale are unchanged.
static BOOL WINAPI GetWindowRectHook(HWND hwnd, LPRECT rc)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetWindowRect(hwnd, rc);
    if (ok && rc && hwnd == g_gameHwnd && g_forcedRenderW && g_forcedRenderH &&
        CallerInExe(caller))
    {
        // Keep the true top-left; extend to the full render extent.
        rc->right = rc->left + (LONG)g_forcedRenderW;
        rc->bottom = rc->top + (LONG)g_forcedRenderH;
        static int s_log = 0;
        static const void* s_lastCaller = nullptr;
        if (s_log < 16 && caller != s_lastCaller && g_exeBase)
        {
            ++s_log;
            s_lastCaller = caller;
            LOG("fit: GetWindowRect game-side +0x%llX -> (%ld,%ld,%ld,%ld)",
                (unsigned long long)((const BYTE*)caller - g_exeBase),
                rc->left, rc->top, rc->right, rc->bottom);
        }
    }
    return ok;
}

// --- Fitted-menu cursor coordinate remap -----------------------------------
// With the fit on, MCC DRAWS the full render (e.g. 3204x2310) but the visible
// window is shrunk to the monitor and the GPU downscales it on present. MCC
// lays out and hit-tests its native shell / pause menu in that full render
// space, yet the OS cursor that drives EVERY selection -- the mouse, and the
// gamepad/keyboard "virtual cursor" MCC's console-style shell moves for you --
// is confined to the small physical window. So only the top-left window-sized
// slice of the menu is reachable: the pointer only responds top-left, and
// keyboard/controller focus "moves but stops short" at the window edge. That is
// the exact reported symptom.
//
// Fix: make MCC's OS-cursor coordinate space match the fitted window in BOTH
// directions, and touch only calls that come from the game executable.
//   * GetCursorPos (MCC reads the cursor): scale the physical, window-confined
//     point UP into full render space, so the hit-test lands on the widget
//     directly under the visibly-downscaled cursor.
//   * SetCursorPos (MCC moves the cursor for gamepad/keyboard nav): scale its
//     render-space target back DOWN so the OS cursor stays inside the window
//     and the round-trip through GetCursorPos is exact.
//   * WindowFromPoint MUST keep seeing the TRUE physical point. MCC calls it
//     right after GetCursorPos to confirm the cursor is over its window; if it
//     saw our scaled-up point (which lands outside the small window) it decides
//     the cursor left and drops the input. That silent detail is what broke the
//     earlier broad GetCursorPos rewrite. We undo the remap for exactly the
//     value we last handed out, so no game address needs to be hardcoded.
typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
typedef BOOL(WINAPI* SetCursorPosFn)(int, int);
typedef HWND(WINAPI* WindowFromPointFn)(POINT);
typedef BOOL(WINAPI* ClipCursorFn)(const RECT*);
typedef int(WINAPI* GetSystemMetricsFn)(int);
typedef BOOL(WINAPI* GetMonitorInfoWFn)(HMONITOR, LPMONITORINFO);
static GetCursorPosFn g_origGetCursorPos = nullptr;
static SetCursorPosFn g_origSetCursorPos = nullptr;
static WindowFromPointFn g_origWindowFromPoint = nullptr;
static ClipCursorFn g_origClipCursor = nullptr;
static GetSystemMetricsFn g_origGetSystemMetrics = nullptr;
static GetMonitorInfoWFn g_origGetMonitorInfoW = nullptr;

// The shell/pause cursor consumers live in the game executable (RE-notes /
// RESOLUTION-FSR-INVESTIGATION static analysis). We only remap calls whose
// return address is inside that image, so our own ImGui overlay and any system
// DLL are never touched. (g_exeBase/g_exeEnd are declared above GetClientRectHook.)
static void InitExeRange()
{
    HMODULE h = GetModuleHandleW(nullptr);
    if (!h)
        return;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const BYTE*>(h) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return;
    g_exeBase = reinterpret_cast<const BYTE*>(h);
    g_exeEnd = g_exeBase + nt->OptionalHeader.SizeOfImage;
}
static inline bool CallerInExe(const void* ret)
{
    return g_exeBase && ret >= static_cast<const void*>(g_exeBase) &&
           ret < static_cast<const void*>(g_exeEnd);
}

// True (unlied) fitted-window client origin (in screen space) and size. Uses
// the ORIGINAL GetClientRect so the WM_SIZE full-size lie can never leak in.
static bool FitClientMetrics(POINT& originScreen, LONG& clientW, LONG& clientH)
{
    if (!g_gameHwnd || !g_origGetClientRect)
        return false;
    RECT rc{};
    if (!g_origGetClientRect(g_gameHwnd, &rc))
        return false;
    clientW = rc.right - rc.left;
    clientH = rc.bottom - rc.top;
    if (clientW <= 0 || clientH <= 0)
        return false;
    originScreen.x = 0;
    originScreen.y = 0;
    return ClientToScreen(g_gameHwnd, &originScreen) != FALSE;
}

static BOOL WINAPI GetCursorPosHook(LPPOINT p)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetCursorPos(p);
    if (!ok || !p || !g_fitActive || !g_forcedRenderW || !g_forcedRenderH ||
        !CallerInExe(caller))
        return ok;
    POINT origin{};
    LONG cw = 0, ch = 0;
    if (!FitClientMetrics(origin, cw, ch))
        return ok;
    // Only remap while the cursor is actually over the fitted window.
    if (p->x < origin.x || p->y < origin.y ||
        p->x >= origin.x + cw || p->y >= origin.y + ch)
        return ok;
    const POINT phys = *p;
    POINT mapped;
    mapped.x = origin.x +
               (LONG)llround((double)(phys.x - origin.x) * g_forcedRenderW / cw);
    mapped.y = origin.y +
               (LONG)llround((double)(phys.y - origin.y) * g_forcedRenderH / ch);
    *p = mapped;
    // Log only when the physical cursor actually MOVES (and only once the
    // window is shrunk), so the budget records real navigation instead of 40
    // copies of one idle frame.
    static int s_log = 0;
    static POINT s_lastLogged{LONG_MIN, LONG_MIN};
    if (cw < (LONG)g_forcedRenderW && s_log < 60 &&
        (phys.x != s_lastLogged.x || phys.y != s_lastLogged.y))
    {
        ++s_log;
        s_lastLogged = phys;
        LOG("fit: menu cursor read +0x%llX phys(%ld,%ld) -> render(%ld,%ld) "
            "client %ldx%ld",
            (unsigned long long)((const BYTE*)caller - g_exeBase),
            phys.x, phys.y, mapped.x, mapped.y, cw, ch);
    }
    return ok;
}

static BOOL WINAPI SetCursorPosHook(int X, int Y)
{
    const void* caller = _ReturnAddress();
    if (!g_fitActive || !g_forcedRenderW || !g_forcedRenderH ||
        !CallerInExe(caller))
        return g_origSetCursorPos(X, Y);
    POINT origin{};
    LONG cw = 0, ch = 0;
    if (!FitClientMetrics(origin, cw, ch))
        return g_origSetCursorPos(X, Y);
    // MCC targets the cursor in its own (full render) client space. Convert
    // points that fall inside that render rectangle back into the small window
    // so the OS cursor lands where MCC intends; leave anything else untouched.
    const LONG relX = X - origin.x;
    const LONG relY = Y - origin.y;
    if (relX < 0 || relY < 0 ||
        relX > (LONG)g_forcedRenderW || relY > (LONG)g_forcedRenderH)
        return g_origSetCursorPos(X, Y);
    const int px = origin.x + (int)llround((double)relX * cw / g_forcedRenderW);
    const int py = origin.y + (int)llround((double)relY * ch / g_forcedRenderH);
    static int s_log = 0;
    static POINT s_lastLogged{LONG_MIN, LONG_MIN};
    if (cw < (LONG)g_forcedRenderW && s_log < 60 &&
        (X != s_lastLogged.x || Y != s_lastLogged.y))
    {
        ++s_log;
        s_lastLogged.x = X;
        s_lastLogged.y = Y;
        LOG("fit: menu cursor move +0x%llX render(%d,%d) -> phys(%d,%d)",
            (unsigned long long)((const BYTE*)caller - g_exeBase), X, Y, px, py);
    }
    return g_origSetCursorPos(px, py);
}

// If MCC confines the cursor (ClipCursor) to a rectangle expressed in its
// believed full-render space, the OS clips the physical cursor to that (often
// off-screen) rectangle and you can't move the pointer to the lower menu items
// at all. Fold any render-space clip rect back into the real window client so
// the cursor stays free across the whole fitted menu. Fail-open: on anything
// unexpected we pass the request through untouched.
static BOOL WINAPI ClipCursorHook(const RECT* rc)
{
    if (!g_fitActive || !g_forcedRenderW || !g_forcedRenderH || !rc)
        return g_origClipCursor(rc);
    POINT origin{};
    LONG cw = 0, ch = 0;
    if (!FitClientMetrics(origin, cw, ch))
        return g_origClipCursor(rc);
    RECT mapped = *rc;
    auto foldX = [&](LONG v) {
        LONG r = v - origin.x;
        if (r < 0) r = 0;
        if (r > (LONG)g_forcedRenderW) r = (LONG)g_forcedRenderW;
        return origin.x + (LONG)llround((double)r * cw / g_forcedRenderW);
    };
    auto foldY = [&](LONG v) {
        LONG r = v - origin.y;
        if (r < 0) r = 0;
        if (r > (LONG)g_forcedRenderH) r = (LONG)g_forcedRenderH;
        return origin.y + (LONG)llround((double)r * ch / g_forcedRenderH);
    };
    mapped.left = foldX(rc->left);
    mapped.right = foldX(rc->right);
    mapped.top = foldY(rc->top);
    mapped.bottom = foldY(rc->bottom);
    static int s_log = 0;
    if (cw < (LONG)g_forcedRenderW && s_log < 12)
    {
        ++s_log;
        LOG("fit: menu cursor clip (%ld,%ld,%ld,%ld) -> (%ld,%ld,%ld,%ld)",
            rc->left, rc->top, rc->right, rc->bottom,
            mapped.left, mapped.top, mapped.right, mapped.bottom);
    }
    return g_origClipCursor(&mapped);
}

static HWND WINAPI WindowFromPointHook(POINT pt)
{
    // MCC hit-tests its menu in full render space, so it asks WindowFromPoint
    // about points spread across the *believed* 3204x2310 client -- most of
    // which fall OFF the real, shrunk window (and often off the monitor). Those
    // resolve to "not my window" and the menu item dies; only points that happen
    // to still land on the small top-left slice work. Map ANY point inside the
    // render rectangle back down into the real window client before the OS
    // answers, so every menu item resolves to the game window like it should.
    if (g_fitActive && g_forcedRenderW && g_forcedRenderH)
    {
        POINT origin{};
        LONG cw = 0, ch = 0;
        if (FitClientMetrics(origin, cw, ch))
        {
            const LONG rx = pt.x - origin.x;
            const LONG ry = pt.y - origin.y;
            if (rx >= 0 && ry >= 0 && rx <= (LONG)g_forcedRenderW &&
                ry <= (LONG)g_forcedRenderH)
            {
                pt.x = origin.x +
                       (LONG)llround((double)rx * cw / g_forcedRenderW);
                pt.y = origin.y +
                       (LONG)llround((double)ry * ch / g_forcedRenderH);
            }
        }
    }
    return g_origWindowFromPoint(pt);
}

// THE root cause of the "menu items are drawn but dead below a certain row"
// symptom (which happens with the fit ON *or* OFF, because it is driven by the
// desktop resolution, not our window): MCC lays out and clips its native shell /
// pause menu against the size it believes the SCREEN is. It asks Windows via
// GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN) and GetMonitorInfo, and Windows
// truthfully answers the real desktop size (e.g. 1280x720). But MCC renders the
// menu across the full headset canvas (e.g. 3204x2310), so every widget below
// the real screen height is treated as off-screen and refuses focus -- for the
// mouse, the keyboard, AND the d-pad alike (they all obey this one clip).
//
// Fix: for game-side callers only, report the full render size as the screen /
// monitor size so MCC's layout-vs-viewport clip matches what it actually draws.
// The compositor, DXGI and our own fit math call these from system DLLs / this
// DLL (not the game EXE), so they keep the true desktop size and the display
// downscale is unchanged. Works at ANY desktop resolution or aspect ratio
// because we always hand back the current forced render extent, never a constant.
static int WINAPI GetSystemMetricsHook(int index)
{
    const void* caller = _ReturnAddress();
    const int real = g_origGetSystemMetrics(index);
    if (!g_forcedRenderW || !g_forcedRenderH || !CallerInExe(caller))
        return real;
    // Only the primary-screen extent metrics; every other index passes through.
    int lied = real;
    if (index == SM_CXSCREEN || index == SM_CXFULLSCREEN || index == SM_CXVIRTUALSCREEN)
        lied = (int)g_forcedRenderW;
    else if (index == SM_CYSCREEN || index == SM_CYFULLSCREEN || index == SM_CYVIRTUALSCREEN)
        lied = (int)g_forcedRenderH;
    else
        return real;
    static int s_log = 0;
    static const void* s_lastCaller = nullptr;
    if (s_log < 16 && caller != s_lastCaller && g_exeBase)
    {
        ++s_log;
        s_lastCaller = caller;
        LOG("fit: GetSystemMetrics(%d) game-side +0x%llX -> %d (real %d)",
            index, (unsigned long long)((const BYTE*)caller - g_exeBase), lied, real);
    }
    return lied;
}

static BOOL WINAPI GetMonitorInfoWHook(HMONITOR mon, LPMONITORINFO mi)
{
    const void* caller = _ReturnAddress();
    const BOOL ok = g_origGetMonitorInfoW(mon, mi);
    if (!ok || !mi || !g_forcedRenderW || !g_forcedRenderH || !CallerInExe(caller))
        return ok;
    // Only rewrite the monitor that hosts the game window; leave others intact so
    // multi-monitor geometry MCC may query for other reasons stays truthful.
    if (g_gameHwnd &&
        MonitorFromWindow(g_gameHwnd, MONITOR_DEFAULTTONEAREST) != mon)
        return ok;
    // Extend both the monitor and work rects from their real top-left to the full
    // render extent, so MCC's fullscreen/menu layout treats the screen as large as
    // what it draws. Keeps the true origin (multi-monitor offsets stay correct).
    mi->rcMonitor.right = mi->rcMonitor.left + (LONG)g_forcedRenderW;
    mi->rcMonitor.bottom = mi->rcMonitor.top + (LONG)g_forcedRenderH;
    mi->rcWork.right = mi->rcWork.left + (LONG)g_forcedRenderW;
    mi->rcWork.bottom = mi->rcWork.top + (LONG)g_forcedRenderH;
    static int s_log = 0;
    static const void* s_lastCaller = nullptr;
    if (s_log < 16 && caller != s_lastCaller && g_exeBase)
    {
        ++s_log;
        s_lastCaller = caller;
        LOG("fit: GetMonitorInfo game-side +0x%llX -> monitor %ldx%ld work %ldx%ld",
            (unsigned long long)((const BYTE*)caller - g_exeBase),
            mi->rcMonitor.right - mi->rcMonitor.left,
            mi->rcMonitor.bottom - mi->rcMonitor.top,
            mi->rcWork.right - mi->rcWork.left,
            mi->rcWork.bottom - mi->rcWork.top);
    }
    return ok;
}

static HRESULT STDMETHODCALLTYPE CreateSwapChainForHwndHook(IDXGIFactory2* self, IUnknown* device,
    HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreen, IDXGIOutput* restrictOut,
    IDXGISwapChain1** ppSwapChain)
{
    if (pDesc && g_forcedRenderW && g_forcedRenderH && !g_forcedMainSwapchain)
    {
        g_forcedMainSwapchain = true;
        g_gameHwnd = hwnd;
        DXGI_SWAP_CHAIN_DESC1 desc = *pDesc;
        LOG("fit: CreateSwapChainForHwnd MCC requested %ux%u scaling=%d hwnd=%p "
            "-> forcing backbuffer %ux%u STRETCH",
            pDesc->Width, pDesc->Height, (int)pDesc->Scaling, (void*)hwnd,
            g_forcedRenderW, g_forcedRenderH);
        desc.Width = g_forcedRenderW;
        desc.Height = g_forcedRenderH;
        desc.Scaling = DXGI_SCALING_STRETCH;
        const HRESULT hr = g_origCreateSwapChainForHwnd(self, device, hwnd, &desc,
                                                        pFullscreen, restrictOut, ppSwapChain);
        if (FAILED(hr))
            LOG("fit: forced CreateSwapChainForHwnd FAILED (hr=0x%08X); the fit did "
                "NOT apply on this machine", (unsigned)hr);
        return hr;
    }
    return g_origCreateSwapChainForHwnd(self, device, hwnd, pDesc, pFullscreen,
                                        restrictOut, ppSwapChain);
}

static HRESULT STDMETHODCALLTYPE CreateSwapChainHook(IDXGIFactory* self, IUnknown* device,
    DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
    if (pDesc && g_forcedRenderW && g_forcedRenderH && !g_forcedMainSwapchain)
    {
        g_forcedMainSwapchain = true;
        g_gameHwnd = pDesc->OutputWindow;
        DXGI_SWAP_CHAIN_DESC desc = *pDesc;
        LOG("fit: CreateSwapChain(legacy) MCC requested %ux%u -> forcing %ux%u",
            pDesc->BufferDesc.Width, pDesc->BufferDesc.Height,
            g_forcedRenderW, g_forcedRenderH);
        desc.BufferDesc.Width = g_forcedRenderW;
        desc.BufferDesc.Height = g_forcedRenderH;
        const HRESULT hr = g_origCreateSwapChain(self, device, &desc, ppSwapChain);
        if (FAILED(hr))
            LOG("fit: forced CreateSwapChain FAILED (hr=0x%08X); the fit did NOT "
                "apply on this machine", (unsigned)hr);
        return hr;
    }
    return g_origCreateSwapChain(self, device, pDesc, ppSwapChain);
}

// Broad probe paths deliberately NOT installed here; each was retired after
// theory. Re-adding any of them costs frame time for information we already
// have:
//   UpdateSubresource/Map/Unmap - constant census sagged fps ~25%; the
//     exact-match matrix matcher scored zero hits in a full session.
//   CopySubresourceRegion and frame-wide CopyResource learning - learned the
//     scene snapshot pairs; per-eye substitution of both sides left the ghost
//     unchanged. The learning also allocated a full-resolution shadow texture
//     per eye per pair (~25 MB each) and re-copied them every eye pass.
// CopyResource itself is now intercepted only for ODST's exact native-CHUD
// phase scope. It performs one retained-pointer comparison and may substitute
// only the source with the already-owned eye cache; it does no discovery.
//   PSSetShaderResources - cross-pass history discovery promoted 0 targets in
//     two sessions.
//   OMSetRenderTargetsAndUnorderedAccessViews - frame-level RTV discovery
//     promoted 0 targets.
//   PSSetShader/VSSetShader/Draw* - the CHUD steal-and-requad classifier
//     (2026-07-18): removed the native HUD from both eyes, never displayed
//     its hand quad, and its calibration retry loop cost ~30 fps.
// OMSetRenderTargets makes the two eye renders land in separate textures.

static void STDMETHODCALLTYPE OMSetRenderTargetsHook(ID3D11DeviceContext* context, UINT count,
    ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
    ID3D11RenderTargetView* redirected[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    bool dropDepthStencil = false;
    if (count <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT &&
        VR_RedirectRenderTargets(context, count, rtvs, redirected,
                                 &dropDepthStencil))
    {
        // A substituted private authored-crosshair target is far smaller than
        // the scene depth buffer the engine paired with this bind, and D3D11
        // requires one size across the whole OM stage. Bind no depth for that
        // call rather than an invalid pair.
        g_origOMSetRenderTargets(context, count, redirected,
                                 dropDepthStencil ? nullptr : dsv);
        return;
    }
    g_origOMSetRenderTargets(context, count, rtvs, dsv);
}

// While a title's private authored-crosshair capture is open, that capture owns
// the rasterizer framing. The capture texture is a small square and the title
// draws its reticle at full raster scale, so the viewport is the ONLY thing
// deciding which pixels land in it: a full-raster viewport puts a corner of the
// HUD in the texture instead of the centred reticle. Halo 4 rebinds its scene
// target inside the captured CUI replay and sets its own viewport around that
// bind, so re-applying the framing at the bind alone cannot win a race with a
// viewport set afterwards. These two detours remove the race entirely.
//
// Cost when no capture is open - which is every frame of every other title, and
// all but a few Halo 4 frames a second - is one relaxed atomic load and a
// branch. That is deliberately far cheaper than the retired diagnostic detours
// documented above, which read back GPU data and logged per call.
static void STDMETHODCALLTYPE RSSetViewportsHook(ID3D11DeviceContext* context,
    UINT count, const D3D11_VIEWPORT* viewports)
{
    D3D11_VIEWPORT owned{};
    if (VR_AuthoredCaptureOwnsRasterFraming(context, &owned, nullptr))
    {
        g_origRSSetViewports(context, 1, &owned);
        return;
    }
    g_origRSSetViewports(context, count, viewports);
}

static void STDMETHODCALLTYPE RSSetScissorRectsHook(ID3D11DeviceContext* context,
    UINT count, const D3D11_RECT* rects)
{
    D3D11_RECT owned{};
    if (VR_AuthoredCaptureOwnsRasterFraming(context, nullptr, &owned))
    {
        g_origRSSetScissorRects(context, 1, &owned);
        return;
    }
    g_origRSSetScissorRects(context, count, rects);
}

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
// Diagnostic only, and deliberately far more conservative than the retired
// RSSetViewports hook: this file's own history (see the OMSetRenderTargetsHook
// comment above) records that a prior PS/VS/Draw*-hooking attempt cost ~30 fps.
// This hook does the absolute minimum on every call - one relaxed atomic
// load and compare, nothing else - unless it is one of a HARD-CAPPED total
// number of samples for the whole process lifetime (not per-second: DrawIndexed
// can be called thousands of times a frame, so any per-frame or per-second
// throttle can still mean thousands of wasted checks). Once the budget is
// spent the hook is a single branch forever, so it cannot degrade a long
// session even if this candidate stays enabled by accident.
//
// Purpose: every static and memory-inspection method available tonight
// converged on the same result - the chud_globals curvature-info safe frame
// this mod writes is correct, stable, and read by nothing in the process
// (docs/RE-notes.md, the Reach HUD layout entry). If Reach's HUD is drawn as
// screen-space quads (proven: RSSetViewports never shows a HUD-sized rect),
// the quad's actual screen extent has to come from SOMEWHERE - vertex data,
// a constant buffer, or a shader immediate. Sampling the bound viewport,
// vertex buffer stride, and index/vertex counts at a real HUD-sized draw call
// is the most direct way left to find that path without disassembling more
// of a stripped optimized binary blind.
static std::atomic<int> g_reachDrawSamplesTaken{0};
constexpr int kReachDrawSampleBudget = 200;

// The first 64-sample pass (2026-07-26) found two distinct full-viewport
// index-count-6 shapes: stride 40 and stride 24, plus an unrelated stride-32
// mip/blur chain that shrinks through power-of-two sizes (not sampled here).
// The stride-40 shape was sampled next and its vertices proved to be an exact
// (0,0)-(1,1) UV quad spanning the FULL screen corner to corner in every
// sample - a full-screen post-process/compositor pass, not the HUD. This pass
// switches to the stride-24 shape, the other untried candidate from the same
// original sweep, and reads its real vertex bytes the same way.
static bool IsProvenHudQuadShape(
    UINT indexCount, UINT stride, const D3D11_VIEWPORT& vp, float backbufferW)
{
    return indexCount == 6 && stride == 24 &&
        vp.Width >= backbufferW - 1.0f;
}

static void DumpVertexBufferSample(
    ID3D11DeviceContext* context, ID3D11Buffer* vb, UINT stride, UINT sampleNo)
{
    ID3D11Device* device = nullptr;
    context->GetDevice(&device);
    if (!device)
        return;
    D3D11_BUFFER_DESC srcDesc{};
    vb->GetDesc(&srcDesc);
    D3D11_BUFFER_DESC stagingDesc = srcDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ID3D11Buffer* staging = nullptr;
    const HRESULT hr = device->CreateBuffer(&stagingDesc, nullptr, &staging);
    device->Release();
    if (FAILED(hr) || !staging)
        return;
    context->CopyResource(staging, vb);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(
            staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(mapped.pData);
        // Log up to 4 vertices worth (our proven shape uses 4 unique
        // vertices per quad, 3 quads); stride 40 = 10 floats/vertex.
        const UINT vertsToShow = std::min(
            4u, srcDesc.ByteWidth / std::max(stride, 1u));
        for (UINT v = 0; v < vertsToShow; ++v)
        {
            const float* f = reinterpret_cast<const float*>(
                bytes + static_cast<size_t>(v) * stride);
            const UINT floatsToShow = std::min(stride / 4u, 10u);
            char line[256];
            int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                "REACHVTX[sample %u]: vertex %u:", sampleNo, v);
            for (UINT fi = 0; fi < floatsToShow && n > 0 &&
                 static_cast<size_t>(n) < sizeof(line); ++fi)
            {
                char piece[24];
                _snprintf_s(piece, sizeof(piece), _TRUNCATE,
                    " %.4f", f[fi]);
                strcat_s(line, sizeof(line), piece);
            }
            LOG("%s", line);
        }
        context->Unmap(staging, 0);
    }
    staging->Release();
}

static void LogReachHudDrawIfSmall(ID3D11DeviceContext* context,
    UINT indexCount, UINT startIndex, INT baseVertex)
{
    if (g_reachDrawSamplesTaken.load(std::memory_order_relaxed) >=
        kReachDrawSampleBudget)
        return;
    if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach)
        return;
    if (indexCount == 0 || indexCount > 64)
        return;

    UINT vpCount = 1;
    D3D11_VIEWPORT vp{};
    context->RSGetViewports(&vpCount, &vp);

    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    context->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    const bool hadVb = vb != nullptr;

    unsigned forcedBackbufferW = 0, forcedBackbufferH = 0;
    D3D_GetForcedRenderSize(forcedBackbufferW, forcedBackbufferH);
    const float backbufferW = forcedBackbufferW
        ? static_cast<float>(forcedBackbufferW) : 3786.0f;
    const bool provenShape =
        hadVb && IsProvenHudQuadShape(indexCount, stride, vp, backbufferW);

    if (!provenShape)
    {
        if (vb)
            vb->Release();
        return;
    }

    const int sampleNo =
        g_reachDrawSamplesTaken.fetch_add(1, std::memory_order_relaxed) + 1;
    if (sampleNo > kReachDrawSampleBudget)
    {
        vb->Release();
        return;
    }
    LOG("REACHDRAW: DrawIndexed count=%u startIndex=%u baseVertex=%d "
        "vp=(%.1f,%.1f %.1fx%.1f) vbStride=%u [sample %d/%d]",
        indexCount, startIndex, baseVertex, vp.TopLeftX, vp.TopLeftY,
        vp.Width, vp.Height, stride, sampleNo, kReachDrawSampleBudget);
    DumpVertexBufferSample(
        context, vb, stride, static_cast<UINT>(sampleNo));
    vb->Release();
}

static void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* context,
    UINT indexCount, UINT startIndex, INT baseVertex)
{
    LogReachHudDrawIfSmall(context, indexCount, startIndex, baseVertex);
    g_origDrawIndexed(context, indexCount, startIndex, baseVertex);
}
#endif

#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
static void STDMETHODCALLTYPE CopyResourceHook(ID3D11DeviceContext* context,
    ID3D11Resource* destination, ID3D11Resource* source)
{
    g_origCopyResource(context, destination,
                       VR_RedirectNativeHudCopySource(source));
}
#endif

#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
// RETIRED DIAGNOSTIC (2026-07-26): a live session proved every RSSetViewports
// call in a Reach frame is a world/shadow-cascade/mip-chain size (observed
// 3786x2730 down through 2x1 in one frame, never a small HUD-shaped rect).
// Reach's CHUD does not resize a distinct D3D11 viewport at all - it draws
// screen-space geometry inside the single full-frame viewport, positioned by
// vertex coordinates. This hook answered its question and is retired rather
// than left installed: with dozens of genuinely distinct viewports per frame,
// its "distinct shape" dedup could not throttle a hot path (proven: ~500k
// lines in one short session) and volume like that risks burying real
// signal. See docs/RE-notes.md, the Reach HUD layout entry, for what this
// ruled out and what is tried next.
#endif

// Log-only: record what MCC's swapchain actually is, plus how its backbuffer
// compares to the visible window and the monitor. On a monitor smaller than the
// requested render this shows whether MCC drew the full frame (backbuffer size)
// or a small one, the swap effect + scaling (whether an oversized backbuffer
// STRETCHes or CLIPs on present), and the real window client vs the monitor.
// That is exactly what tells us whether the fit is working. Fires once, then
// only when the backbuffer size changes, so it never spams the hot path.
static void LogSwapchainConfigOnce(IDXGISwapChain* sc)
{
    static UINT s_lastW = 0, s_lastH = 0;
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d)))
        return;
    if (d.BufferDesc.Width == s_lastW && d.BufferDesc.Height == s_lastH)
        return;
    s_lastW = d.BufferDesc.Width;
    s_lastH = d.BufferDesc.Height;

    const char* swapEffect = "UNKNOWN";
    switch (d.SwapEffect)
    {
        case DXGI_SWAP_EFFECT_DISCARD: swapEffect = "DISCARD"; break;
        case DXGI_SWAP_EFFECT_SEQUENTIAL: swapEffect = "SEQUENTIAL"; break;
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: swapEffect = "FLIP_SEQUENTIAL"; break;
        case DXGI_SWAP_EFFECT_FLIP_DISCARD: swapEffect = "FLIP_DISCARD"; break;
        default: break;
    }

    const char* scaling = "n/a";
    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)))
    {
        DXGI_SWAP_CHAIN_DESC1 d1{};
        if (SUCCEEDED(sc1->GetDesc1(&d1)))
        {
            switch (d1.Scaling)
            {
                case DXGI_SCALING_STRETCH: scaling = "STRETCH"; break;
                case DXGI_SCALING_NONE: scaling = "NONE"; break;
                case DXGI_SCALING_ASPECT_RATIO_STRETCH: scaling = "ASPECT_RATIO_STRETCH"; break;
                default: break;
            }
        }
        sc1->Release();
    }

    int clientW = 0, clientH = 0;
    HWND hwnd = d.OutputWindow;
    RECT client{};
    if (hwnd && GetClientRect(hwnd, &client))
    {
        clientW = client.right - client.left;
        clientH = client.bottom - client.top;
    }
    int monW = 0, monH = 0, workW = 0, workH = 0;
    if (hwnd)
    {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfo(mon, &mi))
        {
            monW = mi.rcMonitor.right - mi.rcMonitor.left;
            monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            workW = mi.rcWork.right - mi.rcWork.left;
            workH = mi.rcWork.bottom - mi.rcWork.top;
        }
    }

    LOG("swapchain: backbuffer %ux%u fmt=%d swapEffect=%s scaling=%s bufferCount=%u "
        "windowed=%d | window client %dx%d | monitor %dx%d work %dx%d | fit=%d",
        d.BufferDesc.Width, d.BufferDesc.Height, (int)d.BufferDesc.Format,
        swapEffect, scaling, d.BufferCount, d.Windowed ? 1 : 0,
        clientW, clientH, monW, monH, workW, workH,
        g_config.fit_desktop_window ? 1 : 0);
}

// Present1 can forward to Present internally; this depth counter makes sure
// we only run the VR frame once per game frame.
static thread_local int g_presentDepth = 0;

// --- Co-op drop probe -------------------------------------------------------
// Records, per frame, how long THIS MOD holds MCC's render thread inside
// Present. See coop_probe_logic.h for why that thread matters to co-op. The
// ring is fixed and written only from the game's render thread (Present has one
// caller), so the hot path stays allocation-free and lock-free: fill the slot,
// then publish the count with a release store.
// The investigation is complete; preserve the probe for a targeted future run
// while compiling it out of the normal Present path.
constexpr bool kEnableCoopPresentProbe = false;
namespace
{
    // ~34 s at 120 Hz, ~57 s at 72 Hz -- long enough to hold the whole run-up to
    // a kick at any headset rate we support.
    constexpr size_t kCoopProbeSamples = 4096;
    constexpr size_t kCoopProbeSeconds = 64;

    CoopProbeSample g_coopSamples[kCoopProbeSamples]{};
    std::atomic<uint64_t> g_coopWritten{ 0 };
    int64_t g_coopPrevHookStartQpc = 0;

    double CoopProbeQpcToMs()
    {
        static double s_toMs = [] {
            LARGE_INTEGER f{};
            QueryPerformanceFrequency(&f);
            return f.QuadPart ? 1000.0 / static_cast<double>(f.QuadPart) : 0.0;
        }();
        return s_toMs;
    }

    void CoopProbeNoteFrame(int64_t hookStartQpc, int64_t presentStartQpc,
                            int64_t presentEndQpc)
    {
        if constexpr (!kEnableCoopPresentProbe)
            return;
        if (!g_config.coop_probe)
            return;
        const double toMs = CoopProbeQpcToMs();
        if (toMs == 0.0)
            return;

        LARGE_INTEGER hookEnd{};
        QueryPerformanceCounter(&hookEnd);

        const double hookMs =
            static_cast<double>(hookEnd.QuadPart - hookStartQpc) * toMs;
        const double dxgiMs =
            static_cast<double>(presentEndQpc - presentStartQpc) * toMs;
        // What we ADD is the hook minus the game's own present. Clamp at zero:
        // the two timestamps are taken at slightly different points and a
        // negative "hold" would only ever be measurement noise.
        const double holdMs = hookMs > dxgiMs ? hookMs - dxgiMs : 0.0;
        const double intervalMs = g_coopPrevHookStartQpc
            ? static_cast<double>(hookStartQpc - g_coopPrevHookStartQpc) * toMs
            : 0.0;
        g_coopPrevHookStartQpc = hookStartQpc;

        const uint64_t written = g_coopWritten.load(std::memory_order_relaxed);
        CoopProbeSample& slot = g_coopSamples[written % kCoopProbeSamples];
        slot.tickMs = static_cast<uint32_t>(GetTickCount64());
        slot.holdMs = static_cast<float>(holdMs);
        slot.dxgiMs = static_cast<float>(dxgiMs);
        slot.intervalMs = static_cast<float>(intervalMs);
        g_coopWritten.store(written + 1, std::memory_order_release);
    }
}

void CoopProbe_DumpRunUp(const char* reason)
{
    if constexpr (!kEnableCoopPresentProbe)
        return;
    if (!g_config.coop_probe)
        return;
    const uint64_t written = g_coopWritten.load(std::memory_order_acquire);
    if (!written)
        return;

    // Dump path only -- never the hot path -- so these statics cost nothing per
    // frame and keep the probe allocation-free here too.
    static CoopProbeSample s_ordered[kCoopProbeSamples];
    static CoopProbeBucket s_buckets[kCoopProbeSeconds];

    const size_t count = static_cast<size_t>(
        written < kCoopProbeSamples ? written : kCoopProbeSamples);
    const size_t first =
        static_cast<size_t>((written - count) % kCoopProbeSamples);
    for (size_t i = 0; i < count; ++i)
        s_ordered[i] = g_coopSamples[(first + i) % kCoopProbeSamples];

    const uint32_t nowTick = static_cast<uint32_t>(GetTickCount64());
    const size_t bucketCount = CoopProbeSummarise(
        s_ordered, count, nowTick, s_buckets, kCoopProbeSeconds);

    LOG("COOPPROBE run-up to teardown from %s: %zu frames across %zu s. "
        "'hold' is the time THIS MOD adds inside MCC's Present, on MCC's own "
        "render thread -- the thread its co-op simulation is paced from. "
        "Rising hold or interval before a kick implicates the mod; flat means "
        "look elsewhere.",
        reason ? reason : "?", count, bucketCount);
    for (size_t age = bucketCount; age-- > 0;)
    {
        const CoopProbeBucket& b = s_buckets[age];
        if (!b.frames)
            continue;
        LOG("COOPPROBE t-%02zus: frames=%u hold avg=%.3f max=%.3f over4ms=%u "
            "over8ms=%u | dxgi avg=%.3f max=%.3f | interval avg=%.3f max=%.3f",
            age, b.frames, b.holdAvgMs, b.holdMaxMs, b.holdOver4Ms,
            b.holdOver8Ms, b.dxgiAvgMs, b.dxgiMaxMs, b.intervalAvgMs,
            b.intervalMaxMs);
    }
}

// The VR frame is submitted INSIDE the game's desktop present (below), so
// whatever throttles that present throttles the headset with it. MCC's V-Sync
// paces on the DESKTOP monitor's refresh, which has nothing to do with the
// headset's -- that is how a 60 Hz desktop ends up capping a 120 Hz headset.
// Once xrWaitFrame is driving our cadence, present the desktop mirror unlocked
// and let the runtime's reported display period be the only clock. No rate is
// assumed here: 72, 90, 120 and 144 Hz headsets all pace themselves.
static UINT PacedSyncInterval(UINT requested)
{
    if (!g_config.desktop_present_unlocked || !VR_IsFramePacingOwned())
        return requested;
    static UINT s_loggedRequested = UINT_MAX;
    if (requested != s_loggedRequested)
    {
        s_loggedRequested = requested;
        if (requested == 0)
            LOG("pacing: MCC already presents unlocked (syncInterval=0); the "
                "runtime app cadence is %.1fHz", VR_HeadsetRefreshHz());
        else
            LOG("pacing: MCC asked for syncInterval=%u (its V-Sync would cap the "
                "headset at the DESKTOP refresh); presenting unlocked so the "
                "runtime app cadence remains %.1fHz",
                requested, VR_HeadsetRefreshHz());
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* sc, UINT syncInterval, UINT flags)
{
    const bool topLevel = (g_presentDepth++ == 0);
    const bool runVrFrame = topLevel && !(flags & DXGI_PRESENT_TEST);
    LARGE_INTEGER hookStart{};
    if (runVrFrame)
    {
        if constexpr (kEnableCoopPresentProbe)
            QueryPerformanceCounter(&hookStart);
        LogSwapchainConfigOnce(sc);
        VR_BeforePresent(sc);
    }
    const UINT pacedSyncInterval = runVrFrame
        ? PacedSyncInterval(syncInterval) : syncInterval;
    LARGE_INTEGER presentStart{}, presentEnd{};
    if (runVrFrame)
        QueryPerformanceCounter(&presentStart);
    HRESULT hr = g_origPresent(sc, pacedSyncInterval, flags);
    if (runVrFrame)
    {
        QueryPerformanceCounter(&presentEnd);
        VR_AfterPresent(sc, presentStart.QuadPart, presentEnd.QuadPart, hr);
        if constexpr (kEnableCoopPresentProbe)
            CoopProbeNoteFrame(hookStart.QuadPart, presentStart.QuadPart,
                               presentEnd.QuadPart);
    }
    g_presentDepth--;
    return hr;
}

static HRESULT STDMETHODCALLTYPE Present1Hook(IDXGISwapChain1* sc, UINT syncInterval, UINT flags,
                                              const DXGI_PRESENT_PARAMETERS* params)
{
    const bool topLevel = (g_presentDepth++ == 0);
    const bool runVrFrame = topLevel && !(flags & DXGI_PRESENT_TEST);
    LARGE_INTEGER hookStart{};
    if (runVrFrame)
    {
        if constexpr (kEnableCoopPresentProbe)
            QueryPerformanceCounter(&hookStart);
        LogSwapchainConfigOnce(sc);
        VR_BeforePresent(sc);
    }
    const UINT pacedSyncInterval = runVrFrame
        ? PacedSyncInterval(syncInterval) : syncInterval;
    LARGE_INTEGER presentStart{}, presentEnd{};
    if (runVrFrame)
        QueryPerformanceCounter(&presentStart);
    HRESULT hr = g_origPresent1(sc, pacedSyncInterval, flags, params);
    if (runVrFrame)
    {
        QueryPerformanceCounter(&presentEnd);
        VR_AfterPresent(sc, presentStart.QuadPart, presentEnd.QuadPart, hr);
        if constexpr (kEnableCoopPresentProbe)
            CoopProbeNoteFrame(hookStart.QuadPart, presentStart.QuadPart,
                               presentEnd.QuadPart);
    }
    g_presentDepth--;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ResizeBuffersHook(IDXGISwapChain* sc, UINT bufferCount, UINT width,
                                                   UINT height, DXGI_FORMAT format, UINT flags)
{
    // With the fit on, keep the backbuffer pinned to the full launched render
    // size so a later resize (e.g. triggered when we shrink the visible window to
    // fit the monitor) can't clamp the surface the headset captures back down.
    // With the fit off, g_forcedRenderW/H are 0 and this passes the size through
    // unchanged -- exactly the previous behavior.
    UINT fw = width, fh = height;
    if (g_forcedRenderW && g_forcedRenderH)
    {
        fw = g_forcedRenderW;
        fh = g_forcedRenderH;
    }
    LOG("game resized its swapchain to %ux%u (using %ux%u)", width, height, fw, fh);
    VR_OnResizeBuffers(sc); // we must drop any references to the old backbuffer first
    const HRESULT result =
        g_origResizeBuffers(sc, bufferCount, fw, fh, format, flags);
    VR_AfterResizeBuffers(sc);
    return result;
}

bool InstallD3D11Hooks()
{
    // Config is loaded before this runs. Only when the desktop-window fit is on
    // do we compute the forced render size and install the swapchain-creation /
    // GetClientRect hooks below; with it off, none of that exists and the render
    // path is byte-for-byte the previous behavior.
    const bool fit = g_config.fit_desktop_window;
    if (fit)
        InitForcedRenderSize();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"halo3xr_dummy_window";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
                                0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        LOG("dummy window creation failed (%lu)", GetLastError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr))
    {
        LOG("dummy D3D11 device creation failed (hr=0x%08X)", (unsigned)hr);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    void** vtbl = *(void***)sc;
    void** contextVtbl = *(void***)ctx;
    bool ok = MH_CreateHook(vtbl[8], (void*)&PresentHook, (void**)&g_origPresent) == MH_OK &&
              MH_CreateHook(vtbl[13], (void*)&ResizeBuffersHook, (void**)&g_origResizeBuffers) == MH_OK &&
              MH_CreateHook(contextVtbl[33], (void*)&OMSetRenderTargetsHook,
                            (void**)&g_origOMSetRenderTargets) == MH_OK;

    // ID3D11DeviceContext::RSSetViewports is vtable slot 44 and
    // RSSetScissorRects is 45, immediately after the slot-33/47 entries this
    // file already takes. These are NOT part of `ok`: they guarantee the
    // authored-crosshair capture framing only, so failing to take them must
    // leave that one optional feature degraded rather than gate Present, the
    // per-eye redirect, or any title camera core. The failure is loud because a
    // silently unowned framing captures the wrong pixels and still looks
    // nominal in every counter.
    const bool rasterFramingHooksOk =
        MH_CreateHook(contextVtbl[44], (void*)&RSSetViewportsHook,
                      (void**)&g_origRSSetViewports) == MH_OK &&
        MH_CreateHook(contextVtbl[45], (void*)&RSSetScissorRectsHook,
                      (void**)&g_origRSSetScissorRects) == MH_OK;
    VR_SetAuthoredCaptureFramingHooksInstalled(rasterFramingHooksOk);
    if (!rasterFramingHooksOk)
    {
        LOG("warning: rasterizer framing hooks failed; an authored-crosshair "
            "capture cannot own its viewport and the captured art may not be "
            "the title's reticle. Stereo, the eye redirect and every camera "
            "core are unaffected");
    }
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    ok = ok && MH_CreateHook(contextVtbl[47], (void*)&CopyResourceHook,
                             (void**)&g_origCopyResource) == MH_OK;
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    // ID3D11DeviceContext::DrawIndexed is vtable slot 12. The old HUD probe is
    // preserved above as evidence, but it must not intercept production draws.
    // This optional hook was never part of `ok`, so disabling it cannot gate
    // Present, render-target redirection, or any title camera core.
    if constexpr (kEnableReachDrawIndexedDiagnostic)
    {
        if (MH_CreateHook(contextVtbl[12], (void*)&DrawIndexedHook,
                          (void**)&g_origDrawIndexed) != MH_OK)
            LOG("REACHDRAW: DrawIndexed hook failed; draw sampling disabled");
    }
    else
    {
        LOG("REACHDRAW: DrawIndexed diagnostic hook intentionally disabled; "
            "no draw calls are intercepted");
    }
#endif

    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)))
    {
        void** vtbl1 = *(void***)sc1;
        if (MH_CreateHook(vtbl1[22], (void*)&Present1Hook, (void**)&g_origPresent1) != MH_OK)
            LOG("warning: Present1 hook failed; flip-model swapchains may not be captured");
        sc1->Release();
    }

    if (fit)
    {
        bool forceHookOk = false;
        bool clientRectHookOk = false;

        // Hook the DXGI factory's swapchain-creation entry points so we can force
        // MCC's backbuffer to the full launched render size. All factories in the
        // process share the same vtable implementation, so hooking the dummy
        // device's factory hooks MCC's real creation call -- exactly how the
        // Present/ResizeBuffers vtables above are taken.
        // CreateSwapChain=vtbl[10], CreateSwapChainForHwnd=vtbl[15] (IDXGIFactory2).
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory2 = nullptr;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) &&
            SUCCEEDED(dxgiDev->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory2)))
        {
            void** facVtbl = *(void***)factory2;
            if (MH_CreateHook(facVtbl[15], (void*)&CreateSwapChainForHwndHook,
                              (void**)&g_origCreateSwapChainForHwnd) == MH_OK)
                forceHookOk = true;
            else
                LOG("warning: CreateSwapChainForHwnd hook failed; desktop fit inactive");
            if (MH_CreateHook(facVtbl[10], (void*)&CreateSwapChainHook,
                              (void**)&g_origCreateSwapChain) != MH_OK)
                LOG("warning: CreateSwapChain hook failed; legacy desktop fit inactive");
        }
        else
        {
            LOG("warning: could not reach the DXGI factory; desktop fit inactive");
        }
        if (factory2) factory2->Release();
        if (adapter) adapter->Release();
        if (dxgiDev) dxgiDev->Release();

        // Hook user32!GetClientRect so MCC's own resize code, while it handles the
        // WM_SIZE we rewrite, sees the full render size and keeps drawing full.
        // The hook only lies on that thread-local call stack (see the header note),
        // so DXGI's present-time query still downscales the full frame into the
        // real, smaller window.
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            // Cache the game-EXE image range first: GetClientRect/GetWindowRect
            // and the cursor hooks all scope their game-side behavior on it.
            InitExeRange();

            if (void* pGetClientRect = (void*)GetProcAddress(user32, "GetClientRect"))
            {
                if (MH_CreateHook(pGetClientRect, (void*)&GetClientRectHook,
                                  (void**)&g_origGetClientRect) == MH_OK)
                    clientRectHookOk = true;
                else
                    LOG("warning: GetClientRect hook failed; the fit may crop on "
                        "resize-polling titles");
            }

            // GetWindowRect lie (game-side only): make MCC's input/focus region
            // match its full-size layout so the fitted menu's lower items stay
            // navigable. Independent of the render force; warn-only on failure.
            if (void* pGetWindowRect = (void*)GetProcAddress(user32, "GetWindowRect"))
            {
                if (MH_CreateHook(pGetWindowRect, (void*)&GetWindowRectHook,
                                  (void**)&g_origGetWindowRect) != MH_OK)
                    LOG("warning: GetWindowRect hook failed; fitted-menu lower "
                        "items may stay unreachable if MCC bounds input to the "
                        "window rect");
            }

            // Cursor coordinate remap so MCC's native shell / pause menu is
            // navigable in the fitted window (see the block above the hooks).
            // These are supplementary: if any fails, the display fit still works,
            // the menu is just no more navigable than before -- so they do NOT
            // gate g_fitActive. (g_exeBase is already cached above.)
            void* pGetCursorPos = (void*)GetProcAddress(user32, "GetCursorPos");
            void* pSetCursorPos = (void*)GetProcAddress(user32, "SetCursorPos");
            void* pWindowFromPoint = (void*)GetProcAddress(user32, "WindowFromPoint");
            const bool cursorHooksOk =
                g_exeBase &&
                pGetCursorPos &&
                MH_CreateHook(pGetCursorPos, (void*)&GetCursorPosHook,
                              (void**)&g_origGetCursorPos) == MH_OK &&
                pSetCursorPos &&
                MH_CreateHook(pSetCursorPos, (void*)&SetCursorPosHook,
                              (void**)&g_origSetCursorPos) == MH_OK &&
                pWindowFromPoint &&
                MH_CreateHook(pWindowFromPoint, (void*)&WindowFromPointHook,
                              (void**)&g_origWindowFromPoint) == MH_OK;
            if (!cursorHooksOk)
                LOG("warning: fitted-menu cursor remap hooks failed; the native "
                    "shell/pause menu may not be fully navigable in the fitted window");

            // ClipCursor correction is independent of the remap trio; warn-only.
            if (void* pClipCursor = (void*)GetProcAddress(user32, "ClipCursor"))
            {
                if (MH_CreateHook(pClipCursor, (void*)&ClipCursorHook,
                                  (void**)&g_origClipCursor) != MH_OK)
                    LOG("warning: ClipCursor hook failed; a fitted-menu cursor "
                        "clip could still confine the pointer to the wrong rect");
            }

            // Screen-size lie (game-side only): THE fix for the menu row-clip that
            // happens whether the fit is on or off, because MCC clips its menu
            // layout against the real desktop size (SM_CYSCREEN / GetMonitorInfo)
            // while drawing at full render. Report the full render extent to
            // MCC's own code so every drawn item stays navigable. System/DXGI/our
            // own callers keep the true desktop size. Warn-only; the display fit
            // does not depend on these.
            if (void* pGetSysMetrics = (void*)GetProcAddress(user32, "GetSystemMetrics"))
            {
                if (MH_CreateHook(pGetSysMetrics, (void*)&GetSystemMetricsHook,
                                  (void**)&g_origGetSystemMetrics) != MH_OK)
                    LOG("warning: GetSystemMetrics hook failed; the native menu's "
                        "lower rows may stay unreachable (MCC clips layout to the "
                        "real screen height)");
            }
            if (void* pGetMonInfo = (void*)GetProcAddress(user32, "GetMonitorInfoW"))
            {
                if (MH_CreateHook(pGetMonInfo, (void*)&GetMonitorInfoWHook,
                                  (void**)&g_origGetMonitorInfoW) != MH_OK)
                    LOG("warning: GetMonitorInfoW hook failed; the native menu may "
                        "still clip layout to the real monitor size");
            }
        }

        // The desktop fit only engages if BOTH levers that keep MCC drawing full
        // are in place. If either failed, we leave the window at full size (the
        // previous overflow behavior) rather than risk shrinking it into a crop.
        g_fitActive = forceHookOk && clientRectHookOk;
        if (!g_fitActive)
            LOG("fit_desktop_window ON but a required hook is missing; leaving the "
                "desktop window full-size (no shrink) to avoid a cropped render");
    }

    sc->Release();
    ctx->Release();
    dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (!ok)
    {
        LOG("MinHook could not hook the required D3D render path");
        return false;
    }
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}
