#include "installer.h"
#include "updater.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <thread>
#include <functional>
#include <fstream>
#pragma comment(lib, "uxtheme.lib")

namespace mcc_installer {
namespace fs = std::filesystem;
namespace {
constexpr COLORREF kBackground = RGB(7, 16, 25), kPanel = RGB(15, 32, 44), kText = RGB(217, 237, 239), kMuted = RGB(143, 174, 186), kAccent = RGB(110, 229, 232);
constexpr UINT kDone = WM_APP + 20;
enum Id { Games = 100, Browse, Scan, Retain, Install, Launch, Check, Update, Folder, ReleasePage };
struct BuildIdentity { std::wstring label = L"older build (identity unavailable)"; std::string tag; bool candidate = true; };
BuildIdentity ReadIdentity(const fs::path& directory) {
    BuildIdentity identity; std::ifstream file(directory / L"BUILD-IDENTITY.txt", std::ios::binary);
    if (!file) return identity;
    std::string line, commit, kind;
    for (int n = 0; n < 20 && std::getline(file, line); ++n) {
        if (line.size() > 256) continue;
        const auto equal = line.find('='); if (equal == std::string::npos) continue;
        const auto key = Trim(line.substr(0, equal)), value = Trim(line.substr(equal + 1));
        if (!std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; })) continue;
        if (key == "source_commit") commit = value;
        else if (key == "release_tag") identity.tag = value;
        else if (key == "build_kind") kind = value;
    }
    identity.candidate = kind != "PUBLIC_RELEASE";
    if (!identity.tag.empty()) identity.label = std::wstring(identity.tag.begin(), identity.tag.end());
    else if (!commit.empty()) identity.label = L"local candidate " + std::wstring(commit.begin(), commit.begin() + std::min<size_t>(commit.size(), 12));
    return identity;
}
struct Menu {
    HINSTANCE instance{}; HWND window{}, games{}, path{}, retain{}, status{}, release{};
    HFONT body{}, smallFont{}, title{}, label{}; HBRUSH background{}, panel{};
    std::vector<GameInstall> installs;
    fs::path selfDir, payload; std::wstring launchDir;
    fs::path fontPath; bool fontLoaded = false;
    bool busy = false, updateReady = false, helperStarted = false;
    std::thread worker; std::wstring operationMessage;
    ReleaseUpdate latest;
    enum class Work { None, Install, Check, Download } work = Work::None;
    fs::path downloaded;
    GameInstall operationGame; bool operationRetain = true;
    int dpi = 96;
    int Px(int value) const { return MulDiv(value, dpi, 96); }
    GameInstall* Selected() { const auto index = SendMessageW(games, CB_GETCURSEL, 0, 0); return index >= 0 && static_cast<size_t>(index) < installs.size() ? &installs[static_cast<size_t>(index)] : nullptr; }
    void Text(HWND control, const std::wstring& text) { SetWindowTextW(control, text.c_str()); }
    void Refresh() {
        auto* game = Selected();
        const bool installed = game && fs::is_regular_file(game->root / L"Halo_MCC_VR" / L"HaloMCCVR.dll");
        bool same = game && !_wcsicmp(fs::absolute(payload).c_str(), fs::absolute(game->root / L"Halo_MCC_VR").c_str());
        const bool hasPayload = !payload.empty() && fs::is_regular_file(payload / L"INSTALL-MANIFEST.sha256");
        for (int id : {Games, Browse, Scan, Retain, ReleasePage}) EnableWindow(GetDlgItem(window, id), !busy);
        EnableWindow(GetDlgItem(window, Install), !busy && game && hasPayload && !same);
        EnableWindow(GetDlgItem(window, Launch), !busy && installed);
        EnableWindow(GetDlgItem(window, Folder), !busy && game);
        EnableWindow(GetDlgItem(window, Check), !busy);
        const auto identity = game ? ReadIdentity(game->root / L"Halo_MCC_VR") : BuildIdentity{};
        const bool sameRelease = installed && !identity.tag.empty() && identity.tag == latest.tag && !identity.candidate;
        EnableWindow(GetDlgItem(window, Update), !busy && game && updateReady && !sameRelease);
        Text(GetDlgItem(window, Update), identity.candidate && installed ? L"INSTALL PUBLIC RELEASE" : L"INSTALL UPDATE");
        if (game) Text(path, (game->store ? L"XBOX APP / MICROSOFT STORE\n" : L"STEAM\n") + game->root.wstring() +
            L"\nVR folder: " + (game->root / L"Halo_MCC_VR").wstring() + L"\nInstalled: " + (installed ? identity.label : L"not installed"));
        else Text(path, L"No MCC installation found. Choose Browse and select the folder containing MCC, or the Xbox Content folder.");
        Text(GetDlgItem(window, Install), installed ? L"INSTALL / UPDATE FROM THIS PACKAGE" : L"INSTALL VR MOD");
        InvalidateRect(window, nullptr, FALSE);
    }
    void Populate(const fs::path& preferred = {}) {
        SendMessageW(games, CB_RESETCONTENT, 0, 0); int selection = 0;
        for (size_t i = 0; i < installs.size(); ++i) {
            const auto& game = installs[i];
            const auto text = (game.store ? L"Xbox app  |  " : L"Steam  |  ") + game.root.wstring();
            SendMessageW(games, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            if (!preferred.empty() && !_wcsicmp(preferred.c_str(), game.root.c_str())) selection = static_cast<int>(i);
        }
        if (!installs.empty()) SendMessageW(games, CB_SETCURSEL, selection, 0);
        Refresh();
    }
    void Start(Work kind, std::function<void()> operation) {
        busy = true; work = kind; operationMessage.clear(); Refresh();
        worker = std::thread([this, operation = std::move(operation)] {
            try { operation(); }
            catch (const std::exception& error) {
                const std::string message = error.what(); operationMessage.assign(message.begin(), message.end());
            } catch (...) { operationMessage = L"The operation could not finish. Your game has not been launched."; }
            PostMessageW(window, kDone, 0, 0);
        });
    }
};
int CALLBACK FontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) { *reinterpret_cast<bool*>(found) = true; return 0; }
HFONT Font(Menu& menu, int size, int weight, const wchar_t* face) { return CreateFontW(-menu.Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face); }
HWND Control(Menu& menu, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
    auto control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, menu.Px(x), menu.Px(y), menu.Px(w), menu.Px(h), menu.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), menu.instance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(menu.body), TRUE); return control;
}
void Label(Menu& menu, const wchar_t* text, int x, int y, int w) { auto control = Control(menu, L"STATIC", text, 0, x, y, w, 22); SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(menu.label), TRUE); }
void BrowseFolder(Menu& menu) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD flags = 0; dialog->GetOptions(&flags); dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose the MCC install folder (or Xbox Content folder)");
    if (SUCCEEDED(dialog->Show(menu.window))) {
        IShellItem* item = nullptr; PWSTR path = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            GameInstall game;
            if (ProbeInstall(path, game)) {
                bool found = false; for (const auto& old : menu.installs) found |= !_wcsicmp(old.root.c_str(), game.root.c_str());
                if (!found) menu.installs.push_back(game);
                menu.Populate(game.root); menu.Text(menu.status, L"MCC installation selected. Install the mod, then choose Launch MCC when your headset is ready.");
            } else menu.Text(menu.status, L"This folder does not contain a Steam or Xbox MCC installation. Select its root or Content folder.");
        }
        if (path) CoTaskMemFree(path); if (item) item->Release();
    }
    dialog->Release();
}
void DrawButton(Menu& menu, const DRAWITEMSTRUCT& item) {
    const bool disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED;
    const bool primary = item.CtlID == Launch;
    const COLORREF fill = disabled ? RGB(17, 29, 37) : primary ? (pressed ? RGB(39, 112, 121) : RGB(33, 85, 99)) : (pressed ? RGB(36, 71, 85) : kPanel);
    HBRUSH brush = CreateSolidBrush(fill); FillRect(item.hDC, &item.rcItem, brush); DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, disabled ? RGB(39, 58, 67) : primary ? kAccent : RGB(59, 107, 122));
    const auto oldPen = SelectObject(item.hDC, pen); const auto oldBrush = SelectObject(item.hDC, GetStockObject(HOLLOW_BRUSH));
    Rectangle(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    SelectObject(item.hDC, oldBrush); SelectObject(item.hDC, oldPen); DeleteObject(pen);
    wchar_t text[256]{}; GetWindowTextW(item.hwndItem, text, 256);
    SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, disabled ? RGB(80, 105, 115) : kText); SelectObject(item.hDC, menu.label);
    RECT rect = item.rcItem; DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) { InflateRect(&rect, -4, -4); DrawFocusRect(item.hDC, &rect); }
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* menu = reinterpret_cast<Menu*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { menu = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); menu->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu)); }
    if (!menu) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: {
        menu->background = CreateSolidBrush(kBackground); menu->panel = CreateSolidBrush(kPanel);
        menu->body = Font(*menu, 16, FW_NORMAL, L"Segoe UI"); menu->smallFont = Font(*menu, 13, FW_NORMAL, L"Segoe UI"); menu->label = Font(*menu, 14, FW_SEMIBOLD, menu->fontLoaded ? L"Oxanium" : L"Bahnschrift");
        // Use a user's installed Halo font when available. No proprietary font
        // is redistributed. The bundled OFL-licensed Oxanium supplies an angular
        // sci-fi face, with Bahnschrift as a final Windows fallback.
        HDC dc = GetDC(window); LOGFONTW query{}; wcscpy_s(query.lfFaceName, L"Halo"); bool found = false;
        EnumFontFamiliesExW(dc, &query, FontExists, reinterpret_cast<LPARAM>(&found), 0); ReleaseDC(window, dc);
        menu->title = Font(*menu, 45, FW_NORMAL, found ? L"Halo" : menu->fontLoaded ? L"Oxanium" : L"Bahnschrift SemiCondensed");
        Label(*menu, L"01  /  GAME INSTALLATION", 38, 134, 470);
        menu->games = Control(*menu, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 38, 163, 642, 230, Games);
        SendMessageW(menu->games, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), menu->Px(25));
        SendMessageW(menu->games, CB_SETITEMHEIGHT, 0, menu->Px(29));
        Control(*menu, L"BUTTON", L"BROWSE", BS_OWNERDRAW | WS_TABSTOP, 696, 160, 122, 38, Browse);
        Control(*menu, L"BUTTON", L"DETECT", BS_OWNERDRAW | WS_TABSTOP, 832, 160, 110, 38, Scan);
        menu->path = Control(*menu, L"STATIC", L"", SS_LEFT, 40, 214, 895, 77);
        SendMessageW(menu->path, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        Label(*menu, L"02  /  YOUR VR SETUP", 38, 310, 570);
        menu->retain = Control(*menu, L"BUTTON", L"Keep my settings and add new options when updating", BS_AUTOCHECKBOX | WS_TABSTOP, 38, 340, 850, 30, Retain);
        SendMessageW(menu->retain, BM_SETCHECK, BST_CHECKED, 0); SetWindowTheme(menu->retain, L"", L"");
        Control(*menu, L"BUTTON", L"INSTALL VR MOD", BS_OWNERDRAW | WS_TABSTOP, 38, 390, 414, 48, Install);
        Control(*menu, L"BUTTON", L"LAUNCH MCC", BS_OWNERDRAW | WS_TABSTOP, 474, 390, 300, 48, Launch);
        Control(*menu, L"BUTTON", L"MOD FOLDER", BS_OWNERDRAW | WS_TABSTOP, 796, 390, 146, 48, Folder);
        Label(*menu, L"03  /  RELEASE UPDATES", 38, 472, 570);
        Control(*menu, L"BUTTON", L"CHECK FOR UPDATES", BS_OWNERDRAW | WS_TABSTOP, 38, 503, 270, 40, Check);
        Control(*menu, L"BUTTON", L"INSTALL UPDATE", BS_OWNERDRAW | WS_TABSTOP, 324, 503, 240, 40, Update);
        Control(*menu, L"BUTTON", L"RELEASE PAGE", BS_OWNERDRAW | WS_TABSTOP, 580, 503, 175, 40, ReleasePage);
        menu->release = Control(*menu, L"STATIC", L"Updates are checked only when you choose. Steam and Xbox app use the same release.", SS_LEFT, 40, 555, 895, 38);
        SendMessageW(menu->release, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        menu->status = Control(*menu, L"STATIC", L"Choose an installation to begin. The mod stays in its own Halo_MCC_VR folder.", SS_LEFT, 40, 615, 895, 74);
        SendMessageW(menu->status, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        return 0;
    }
    case WM_PRINTCLIENT: case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wParam) : BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, menu->background);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kAccent); SelectObject(dc, menu->title);
        SetTextCharacterExtra(dc, menu->Px(4)); TextOutW(dc, menu->Px(37), menu->Px(30), L"HALO / MCC VR", 13); SetTextCharacterExtra(dc, 0);
        SelectObject(dc, menu->label); SetTextColor(dc, kMuted); const wchar_t* subtitle = L"THE MASTER CHIEF COLLECTION  /  VIRTUAL REALITY";
        TextOutW(dc, menu->Px(41), menu->Px(93), subtitle, static_cast<int>(wcslen(subtitle)));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(35, 75, 91)); auto old = SelectObject(dc, pen);
        for (int y : {121, 299, 460, 603}) { MoveToEx(dc, menu->Px(38), menu->Px(y), nullptr); LineTo(dc, menu->Px(942), menu->Px(y)); }
        // Original geometric HUD accents; no extracted game art or assets.
        for (int x = 811; x < 943; x += 22) { MoveToEx(dc, menu->Px(x), menu->Px(43), nullptr); LineTo(dc, menu->Px(x + 18), menu->Px(25)); }
        SelectObject(dc, old); DeleteObject(pen); if (message == WM_PAINT) EndPaint(window, &paint); return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, kText); SetBkColor(dc, kBackground); return reinterpret_cast<LRESULT>(menu->background);
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, kText); SetBkColor(dc, kPanel); return reinterpret_cast<LRESULT>(menu->panel);
    }
    case WM_MEASUREITEM:
        if (reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlID == Games) { reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = menu->Px(29); return TRUE; }
        break;
    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item.CtlID == Games) {
            FillRect(item.hDC, &item.rcItem, menu->panel); SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, kText); SelectObject(item.hDC, menu->body);
            std::wstring text = L"Select a detected MCC installation";
            if (item.itemID != static_cast<UINT>(-1)) { const auto length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0); if (length >= 0 && length < 32768) { text.resize(static_cast<size_t>(length) + 1); SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(text.data())); text.resize(static_cast<size_t>(length)); } }
            RECT rect = item.rcItem; rect.left += menu->Px(8); rect.right -= menu->Px(4); DrawTextW(item.hDC, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            if (item.itemState & ODS_FOCUS) DrawFocusRect(item.hDC, &item.rcItem);
        } else DrawButton(*menu, item);
        return TRUE;
    }
    case WM_COMMAND: {
        if (menu->busy) return 0;
        switch (LOWORD(wParam)) {
        case Games: if (HIWORD(wParam) == CBN_SELCHANGE) menu->Refresh(); break;
        case Browse: BrowseFolder(*menu); break;
        case Scan: menu->installs = DetectInstalls(menu->selfDir); menu->Populate(); menu->Text(menu->status, L"Detection complete. Select the MCC installation you want to use."); break;
        case Folder: if (auto* game = menu->Selected()) { auto path = game->root / L"Halo_MCC_VR"; if (!fs::exists(path)) path = game->root; ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); } break;
        case ReleasePage: ShellExecuteW(window, L"open", L"https://github.com/moistman42069/MCCVR-Halo-Build/releases/latest", nullptr, nullptr, SW_SHOWNORMAL); break;
        case Launch: if (auto* game = menu->Selected()) { menu->launchDir = (game->root / L"Halo_MCC_VR").wstring(); DestroyWindow(window); } break;
        case Install: if (auto* game = menu->Selected()) {
            const auto target = *game; const auto payload = menu->payload; const bool retain = SendMessageW(menu->retain, BM_GETCHECK, 0, 0) == BST_CHECKED;
            menu->Text(menu->status, L"Verifying files and installing. MCC will not start until you choose Launch MCC.");
            menu->Start(Menu::Work::Install, [menu, target, payload, retain] { menu->operationMessage = InstallPayload(payload, target, retain).message; });
        } break;
        case Check:
            menu->updateReady = false; menu->Text(menu->status, L"Checking the project's latest public GitHub release...");
            menu->Start(Menu::Work::Check, [menu] { menu->latest = CheckForUpdate(); }); break;
        case Update: if (auto* game = menu->Selected(); game && menu->updateReady) {
            menu->operationGame = *game; menu->operationRetain = SendMessageW(menu->retain, BM_GETCHECK, 0, 0) == BST_CHECKED;
            menu->Text(menu->status, L"Downloading and verifying the release. The launcher will reopen to install it. MCC will stay closed.");
            menu->Start(Menu::Work::Download, [menu] { menu->downloaded = DownloadUpdate(menu->latest); });
        } break;
        }
        return 0;
    }
    case kDone:
        if (menu->worker.joinable()) menu->worker.join();
        menu->busy = false;
        if (menu->operationMessage.empty() && menu->work == Menu::Work::Check) {
            menu->updateReady = true;
            const std::wstring tag(menu->latest.tag.begin(), menu->latest.tag.end());
            menu->Text(menu->release, L"Latest public release: " + tag + L". Installing it replaces the current build, including any test candidate.");
            menu->operationMessage = L"Release found. Choose Install update to download it, or open the release page for its changes.";
        } else if (menu->operationMessage.empty() && menu->work == Menu::Work::Download) {
            if (StartUpdateHelper(menu->downloaded, menu->operationGame, menu->operationRetain, menu->operationMessage)) {
                menu->helperStarted = true; DestroyWindow(window); return 0;
            }
        }
        menu->Text(menu->status, menu->operationMessage); menu->Refresh(); return 0;
    case WM_CLOSE: if (!menu->busy) DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
std::wstring ShowLauncherMenu(HINSTANCE instance, const fs::path& launcherDir, int showCommand) {
    Menu menu; menu.instance = instance; menu.selfDir = launcherDir;
    menu.payload = fs::is_regular_file(launcherDir / L"ModFiles" / L"INSTALL-MANIFEST.sha256") ? launcherDir / L"ModFiles" : launcherDir;
    menu.fontPath = menu.payload / L"assets" / L"fonts" / L"Oxanium.ttf";
    menu.fontLoaded = AddFontResourceExW(menu.fontPath.c_str(), FR_PRIVATE, nullptr) != 0;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW type{sizeof(type)}; type.lpfnWndProc = WindowProc; type.hInstance = instance; type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hIcon = LoadIconW(nullptr, IDI_APPLICATION); type.lpszClassName = L"HaloMCCVRSetupMenu";
    RegisterClassExW(&type);
    RECT workArea{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int fitWidth = MulDiv(workArea.right - workArea.left - 32, 96, 980);
    const int fitHeight = MulDiv(workArea.bottom - workArea.top - 55, 96, 710);
    const int dpi = std::max(60, std::min({static_cast<int>(GetDpiForSystem()), fitWidth, fitHeight}));
    menu.dpi = dpi;
    RECT rect{0, 0, MulDiv(980, dpi, 96), MulDiv(710, dpi, 96)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    HWND window = CreateWindowExW(0, type.lpszClassName, L"Halo MCC VR | Setup & Launch", style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &menu);
    if (window) {
        menu.installs = DetectInstalls(launcherDir); menu.Populate();
        ShowWindow(window, showCommand); UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (menu.worker.joinable()) menu.worker.join();
    for (HFONT font : {menu.body, menu.smallFont, menu.title, menu.label}) if (font) DeleteObject(font);
    if (menu.background) DeleteObject(menu.background); if (menu.panel) DeleteObject(menu.panel);
    if (menu.fontLoaded) RemoveFontResourceExW(menu.fontPath.c_str(), FR_PRIVATE, nullptr);
    if (SUCCEEDED(com)) CoUninitialize();
    return menu.launchDir;
}
} // namespace mcc_installer
