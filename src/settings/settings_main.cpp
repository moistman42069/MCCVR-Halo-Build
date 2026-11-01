// The VR menu on the desktop: the same menu.cpp the headset shows, drawn into the same texture and clicked through the
// same pointer path, with the game's side answered by settings_stubs.cpp. It edits halomccvr.cfg and nothing else.
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include "../dll/menu.h"
#include "../common/config.h"
#include "../common/log.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kTitle[] = L"Halo MCC VR Settings";
constexpr wchar_t kTitleGameOpen[] = L"Halo MCC VR Settings - the game is open, so nothing here is being saved";

bool GameIsOpen()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool open = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !open; more = Process32NextW(snapshot, &entry))
        open = _wcsnicmp(entry.szExeFile, L"MCC-Win64-Shipping", 18) == 0 ||
            _wcsnicmp(entry.szExeFile, L"MCCWinStore", 11) == 0 ||
            _wcsnicmp(entry.szExeFile, L"project-reclaimer-client", 24) == 0;
    CloseHandle(snapshot);
    return open;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message)
    {
    case WM_SETCURSOR:
        // The menu draws its own cursor into the picture.
        if (LOWORD(lp) == HTCLIENT)
        {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

std::wstring BesideThisProgram(const wchar_t* name)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder = path;
    folder.resize(folder.find_last_of(L"\\/") + 1);
    return folder + name;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    wchar_t** argv = commandLine && *commandLine ? CommandLineToArgvW(commandLine, &argc) : nullptr;
    const std::wstring configPath = argv && argc > 0 ? argv[0] : BesideThisProgram(L"halomccvr.cfg");
    if (argv) LocalFree(argv);
    LogInit(BesideThisProgram(L"HaloMCCVRSettings.log").c_str());

    bool gameOpen = GameIsOpen();
    Config_SetReadOnly(gameOpen);
    ConfigLoad(configPath.c_str());

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"HaloMCCVRSettings";
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&windowClass);

    // The menu's own shape, as large as the screen allows: the picture is stretched, so a click still lands by fraction.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const float fit = (std::min)(1.0f, (std::min)((work.right - work.left) * 0.95f / MENU_W,
                                                  (work.bottom - work.top) * 0.9f / MENU_H));
    RECT frame{0, 0, static_cast<LONG>(MENU_W * fit), static_cast<LONG>(MENU_H * fit)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&frame, style, FALSE);
    const HWND window = CreateWindowW(windowClass.lpszClassName, gameOpen ? kTitleGameOpen : kTitle, style, CW_USEDEFAULT,
        CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferCount = 2;
    swap.BufferDesc.Width = MENU_W;
    swap.BufferDesc.Height = MENU_H;
    swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.OutputWindow = window;
    swap.SampleDesc.Count = 1;
    swap.Windowed = TRUE;
    swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &swap, &chain, &device, nullptr, &context);
    if (FAILED(result))
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &swap, &chain, &device, nullptr, &context);
    if (FAILED(result) || !Menu_Init(window, device.Get(), context.Get(), DXGI_FORMAT_R8G8B8A8_UNORM)) return 2;
    Menu_Toggle();
    ShowWindow(window, show);

    ULONGLONG checkedGame = GetTickCount64();
    bool running = true;
    while (running)
    {
        float scroll = 0.0f;
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) running = false;
            if (message.message == WM_MOUSEWHEEL)
                scroll += GET_WHEEL_DELTA_WPARAM(message.wParam) / static_cast<float>(WHEEL_DELTA);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;

        const ULONGLONG now = GetTickCount64();
        if (now - checkedGame >= 1000)
        {
            checkedGame = now;
            const bool open = GameIsOpen();
            if (open != gameOpen)
            {
                gameOpen = open;
                Config_SetReadOnly(open);
                SetWindowTextW(window, open ? kTitleGameOpen : kTitle);
                // The game saved its own changes as it closed.
                if (!open) ConfigLoad(configPath.c_str());
            }
        }

        POINT cursor{};
        RECT client{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        GetClientRect(window, &client);
        const bool hit = GetForegroundWindow() == window && client.right > 0 && client.bottom > 0 &&
            PtInRect(&client, cursor);
        if (hit)
            Menu_SetVrPointer(true, static_cast<float>(cursor.x) / client.right,
                static_cast<float>(cursor.y) / client.bottom, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, scroll);
        else
            Menu_ClearVrPointer();

        if (ID3D11Texture2D* picture = Menu_Render())
        {
            ComPtr<ID3D11Texture2D> backBuffer;
            if (SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
                context->CopyResource(backBuffer.Get(), picture);
        }
        chain->Present(1, 0);
    }
    return 0;
}
