// The VR menu on the desktop: the same menu.cpp the headset shows, drawn into the same texture and clicked through the
// same pointer path, with the game's side answered by settings_stubs.cpp. It edits halomccvr.cfg and nothing else.
// While a game is running it shows that game's own menu instead (menu_mirror.h).
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
#include "../common/menu_mirror.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kTitle[] = L"Halo MCC VR Settings";
constexpr wchar_t kTitleGameOpen[] = L"Halo MCC VR Settings - the game is open, so nothing here is being saved";
constexpr wchar_t kTitleLive[] = L"Halo MCC VR Settings - live with the running game";

// The game's menu, while connected: its picture on this device, and the block its pointer and typing go through.
struct LiveMenu
{
    HANDLE mapping = nullptr;
    MenuMirrorShared* block = nullptr;
    ComPtr<ID3D11Texture2D> picture;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool connected() const { return block != nullptr; }
    void Disconnect()
    {
        picture.Reset();
        if (block) UnmapViewOfFile(block);
        if (mapping) CloseHandle(mapping);
        block = nullptr;
        mapping = nullptr;
    }
    bool Connect(ID3D11Device* device)
    {
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMenuMirrorMapping);
        if (mapping)
            block = static_cast<MenuMirrorShared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MenuMirrorShared)));
        const uint64_t now = GetTickCount64();
        if (block && block->version.load(std::memory_order_acquire) == kMenuMirrorVersion &&
            now - block->gameBeatMs.load() < kMenuMirrorGoneMs &&
            SUCCEEDED(device->OpenSharedResource(reinterpret_cast<HANDLE>(block->texture.load()), IID_PPV_ARGS(&picture))))
        {
            format = static_cast<DXGI_FORMAT>(block->format.load());
            return true;
        }
        Disconnect();
        return false;
    }
};

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

    // The menu's own shape with its side panel beside it, as large as the screen allows: the picture is stretched, so a
    // click still lands by fraction.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const float fit = (std::min)(1.0f, (std::min)((work.right - work.left) * 0.95f / MENU_TEX_W,
                                                  (work.bottom - work.top) * 0.9f / MENU_H));
    RECT frame{0, 0, static_cast<LONG>(MENU_TEX_W * fit), static_cast<LONG>(MENU_H * fit)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&frame, style, FALSE);
    const HWND window = CreateWindowW(windowClass.lpszClassName, gameOpen ? kTitleGameOpen : kTitle, style, CW_USEDEFAULT,
        CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferCount = 2;
    swap.BufferDesc.Width = MENU_TEX_W;
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
    LiveMenu live;
    const auto bufferFormat = [&](DXGI_FORMAT format)
    {
        return SUCCEEDED(chain->ResizeBuffers(2, MENU_TEX_W, MENU_H, format, 0));
    };
    while (running)
    {
        float scroll = 0.0f;
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) running = false;
            if (message.message == WM_MOUSEWHEEL)
                scroll += GET_WHEEL_DELTA_WPARAM(message.wParam) / static_cast<float>(WHEEL_DELTA);
            if (message.message == WM_CHAR && live.connected())
            {
                const uint32_t slot = live.block->charsWritten.load(std::memory_order_relaxed);
                live.block->chars[slot % kMenuMirrorChars].store(static_cast<uint16_t>(message.wParam), std::memory_order_relaxed);
                live.block->charsWritten.store(slot + 1, std::memory_order_release);
            }
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
            const bool gone = live.connected() &&
                (!open || now - live.block->gameBeatMs.load(std::memory_order_relaxed) >= kMenuMirrorGoneMs);
            if (gone)
            {
                live.Disconnect();
                bufferFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
                SetWindowTextW(window, open ? kTitleGameOpen : kTitle);
            }
            else if (open && !live.connected() && live.Connect(device.Get()))
            {
                if (bufferFormat(live.format))
                    SetWindowTextW(window, kTitleLive);
                else
                    live.Disconnect();
            }
        }

        POINT cursor{};
        RECT client{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        GetClientRect(window, &client);
        const bool hit = GetForegroundWindow() == window && client.right > 0 && client.bottom > 0 &&
            PtInRect(&client, cursor);
        if (live.connected())
        {
            MenuMirrorShared& block = *live.block;
            block.desktopBeatMs.store(now, std::memory_order_relaxed);
            block.pointerU.store(hit ? static_cast<float>(cursor.x) / client.right : 0.0f, std::memory_order_relaxed);
            block.pointerV.store(hit ? static_cast<float>(cursor.y) / client.bottom : 0.0f, std::memory_order_relaxed);
            block.pointerPressed.store(hit && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, std::memory_order_relaxed);
            block.pointerHit.store(hit, std::memory_order_relaxed);
            if (hit && scroll != 0.0f)
                block.scrollMilli.fetch_add(static_cast<int32_t>(scroll * 1000.0f), std::memory_order_relaxed);
            ComPtr<ID3D11Texture2D> backBuffer;
            if (SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
                context->CopyResource(backBuffer.Get(), live.picture.Get());
            chain->Present(1, 0);
            continue;
        }
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
