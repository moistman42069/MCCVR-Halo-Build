#include "installer.h"
#include "updater.h"
#include "reclaimer_menu.h"
#include "../common/reclaimer_host_logic.h"
#include "../common/credits_file.h"
#include "menu_font.generated.h"
#include "launcher_credits.generated.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <algorithm>
#include <thread>
#include <functional>
#include <fstream>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

namespace mcc_installer {
namespace fs = std::filesystem;
namespace {
constexpr COLORREF kBackground = RGB(7, 16, 25), kPanel = RGB(15, 32, 44), kText = RGB(217, 237, 239), kMuted = RGB(143, 174, 186), kAccent = RGB(110, 229, 232);
// Project Reclaimer is another game's client, not a step of the MCC setup above it: its section is a panel of its
// own, warm where the rest of the menu is teal.
constexpr COLORREF kReclaimerPanel = RGB(31, 24, 13), kReclaimerField = RGB(46, 35, 18), kReclaimerAccent = RGB(240, 178, 84), kReclaimerMuted = RGB(186, 156, 112);
// The panel's box, at 96 dpi.
// External Mods: a section of its own below the MCC setup, each mod on a tab over its panel. Project Reclaimer is the
// first; more join it as tabs beside it.
constexpr int kExternalTop = 656, kTabHeight = 30;
constexpr int kReclaimerLeft = 26, kReclaimerTop = 754, kReclaimerRight = 954, kReclaimerBottom = 912;
constexpr UINT kDone = WM_APP + 20;
enum Id { Games = 100, Browse, Scan, Retain, Install, Launch, Check, Update, Folder, ReleasePage, Channel,
          ReclaimerPath, ReclaimerBrowse, ReclaimerDetect, ReclaimerLaunch, Settings, Latest, Version, UpdateSelf };
// The window's client size at 96 dpi.
constexpr int kMenuWidth = 1350, kMenuHeight = 988;
// The credits column, right of the divider, at 96 dpi: the title stays put and the list below it scrolls.
constexpr int kCreditsDivider = 968, kCreditsLeft = 986, kCreditsListTop = 76, kCreditsListWidth = 352;
struct BuildIdentity { std::wstring label = L"older build (identity unavailable)"; std::string tag, channel, built, release, published; bool candidate = true; };
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
        else if (key == "release_channel") identity.channel = value;
        else if (key == "built") identity.built = value;
    }
    identity.candidate = kind != "PUBLIC_RELEASE" && kind != "RELEASE_CANDIDATE";
    // The release this build was installed as, if the launcher's update installed it. Only for this very build: anything
    // installed over it another way leaves the file behind.
    std::ifstream saved(directory / L"INSTALLED-RELEASE.txt", std::ios::binary);
    std::string savedRelease, savedPublished, savedBuild;
    for (int n = 0; saved && n < 8 && std::getline(saved, line); ++n) {
        if (line.size() > 256) continue;
        const auto equal = line.find('='); if (equal == std::string::npos) continue;
        const auto key = Trim(line.substr(0, equal)), value = Trim(line.substr(equal + 1));
        if (!std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; })) continue;
        if (key == "release") savedRelease = value;
        else if (key == "published") savedPublished = value;
        else if (key == "build") savedBuild = value;
    }
    if (!savedBuild.empty() && savedBuild == identity.tag) { identity.release = savedRelease; identity.published = savedPublished; }
    if (!identity.tag.empty()) identity.label = std::wstring(identity.tag.begin(), identity.tag.end());
    else if (!commit.empty()) identity.label = L"local candidate " + std::wstring(commit.begin(), commit.begin() + std::min<size_t>(commit.size(), 12));
    return identity;
}
struct Menu {
    HINSTANCE instance{}; HWND window{}, games{}, path{}, retain{}, updateSelf{}, status{}, channel{};
    bool channelChosen = false, channelChanged = false;
    // Project Reclaimer (reclaimer_menu.h): the kept path's field, what is known of it, and the launch asked for.
    HWND reclaimerPath{}, reclaimerInfo{}; std::wstring reclaimerExe, reclaimerDir; bool reclaimerStartable = false;
    std::vector<CreditEntry> credits;
    // The link badges as last drawn, in the list's client coordinates.
    struct CreditBadge { RECT box; const wchar_t* url; };
    std::vector<CreditBadge> creditBadges;
    int creditBadgeHover = -1, creditBadgePressed = -1;
    HWND creditsList{};
    int creditsScroll = 0, creditsContent = 0, creditsDragFrom = -1, creditsDragScroll = 0;
    HFONT body{}, smallFont{}, title{}, label{}, creditsTitle{}, creditsSection{}; HBRUSH background{}, panel{}, reclaimerPanel{}, reclaimerField{};
    std::vector<GameInstall> installs;
    fs::path selfDir, payload; std::wstring launchDir;
    HWND version{}, installedLine{};
    // The download this launcher was opened from, when it holds a package that is not the install itself: the first of
    // the versions, and how a first install happens with nothing to check for.
    bool packageListed = false, packageChosen = false;
    HANDLE font{}; bool fontLoaded = false;
    bool busy = false, updateReady = false, helperStarted = false, keepSettings = true;
    // Whether an update also replaces this launcher where it is. Each version chosen resets it to the usual answer.
    bool updateThisLauncher = false;
    std::thread worker; std::wstring operationMessage;
    ReleaseUpdate latest; std::vector<ReleaseUpdate> versions;
    enum class Work { None, Install, Check, Latest, Download } work = Work::None;
    size_t latestChannel = 0; std::wstring latestPassedOver;
    fs::path downloaded;
    GameInstall operationGame; bool operationRetain = true;
    int dpi = 96;
    int Px(int value) const { return MulDiv(value, dpi, 96); }
    GameInstall* Selected() { const auto index = SendMessageW(games, CB_GETCURSEL, 0, 0); return index >= 0 && static_cast<size_t>(index) < installs.size() ? &installs[static_cast<size_t>(index)] : nullptr; }
    void Text(HWND control, const std::wstring& text) { SetWindowTextW(control, text.c_str()); }
    const ReleaseChannel& CurrentChannel() const {
        const auto index = SendMessageW(channel, CB_GETCURSEL, 0, 0);
        return kReleaseChannels[index >= 0 && static_cast<size_t>(index) < std::size(kReleaseChannels) ? index : 0];
    }
    // The chosen version's page, or until one is listed, the source's release list.
    std::wstring ReleasePageUrl() const {
        const auto& source = CurrentChannel();
        std::wstring url = L"https://github.com/" + std::wstring(source.owner, source.owner + strlen(source.owner)) + L"/MCCVR-Halo-Build/releases";
        const bool plainTag = !latest.tag.empty() && std::all_of(latest.tag.begin(), latest.tag.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
        if (plainTag) url += L"/tag/" + std::wstring(latest.tag.begin(), latest.tag.end());
        return url;
    }
    // The mod this launcher works on: the selected install's, wherever the launcher itself was started from. Its own
    // only when nothing is installed there (a package not yet installed, a Project Reclaimer deployment with no MCC).
    fs::path ModFolder() {
        const auto* game = Selected();
        const auto installed = game ? game->root / L"Halo_MCC_VR" : fs::path{};
        return game && fs::is_regular_file(installed / L"HaloMCCVR.dll") ? installed : selfDir;
    }
    // The combo's row chosen: the download, or a listed release.
    void ChooseVersion(size_t row) {
        const size_t first = packageListed ? 1 : 0;
        packageChosen = packageListed && row == 0;
        const bool release = row >= first && row - first < versions.size();
        latest = release ? versions[row - first] : ReleaseUpdate{};
        updateReady = packageChosen || release;
        updateThisLauncher = release && ReplacesOpenedLauncher(versions, latest, CurrentChannel().key, HALOMCCVR_RELEASE_CHANNEL);
        SendMessageW(version, CB_SETCURSEL, updateReady ? static_cast<WPARAM>(row) : static_cast<WPARAM>(-1), 0);
        if (updateSelf) InvalidateRect(updateSelf, nullptr, FALSE);
    }
    // This launcher is the selected install's own, which every install replaces: there is no choice to offer.
    bool RunningFromInstall() {
        const auto* game = Selected(); std::error_code ignored;
        return game && fs::equivalent(selfDir, game->root / L"Halo_MCC_VR", ignored);
    }
    // What the update-this-launcher box shows: always ticked for the install's own launcher, and never for a download
    // being installed as it is, since that download is the launcher.
    bool UpdateSelfShown() { return RunningFromInstall() || (!packageChosen && updateThisLauncher); }
    void ListVersions() {
        const auto* game = Selected();
        const auto installed = game ? ReadIdentity(game->root / L"Halo_MCC_VR") : BuildIdentity{};
        SendMessageW(version, CB_RESETCONTENT, 0, 0);
        if (packageListed) {
            const auto own = ReadIdentity(payload);
            std::wstring text = L"This download  |  " + own.label;
            if (!own.tag.empty() && own.tag == installed.tag) text += L" (Installed)";
            SendMessageW(version, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        for (size_t i = 0; i < versions.size(); ++i) {
            const auto& release = versions[i];
            std::wstring text(release.tag.begin(), release.tag.end());
            if (i == 0) text += L" (Latest)";
            if (!installed.release.empty() && release.tag == installed.release) text += L" (Installed)";
            if (release.published.size() >= 10) text += L"  |  " + std::wstring(release.published.begin(), release.published.begin() + 10);
            SendMessageW(version, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        // A check just made is what the player asked to see; otherwise the download.
        ChooseVersion(packageListed && !versions.empty() ? 1 : 0);
    }
    void Refresh() {
        auto* game = Selected();
        // Until the player picks one, the source is the one this package came from: the launcher's own, or for a
        // launcher with no package beside it, the selected install's.
        if (!channelChosen) {
            auto own = ReadIdentity(payload).channel;
            if (own.empty() && game) own = ReadIdentity(game->root / L"Halo_MCC_VR").channel;
            if (own.empty()) own = HALOMCCVR_RELEASE_CHANNEL;
            SendMessageW(channel, CB_SETCURSEL, ReleaseChannelIndex(own), 0);
        }
        const bool installed = game && fs::is_regular_file(game->root / L"Halo_MCC_VR" / L"HaloMCCVR.dll");
        bool same = game && !_wcsicmp(fs::absolute(payload).c_str(), fs::absolute(game->root / L"Halo_MCC_VR").c_str());
        const bool hasPayload = !payload.empty() && fs::is_regular_file(payload / L"INSTALL-MANIFEST.sha256");
        
        for (int id : {Games, Channel, Browse, Scan, Retain, ReleasePage, ReclaimerPath, ReclaimerBrowse, ReclaimerDetect}) EnableWindow(GetDlgItem(window, id), !busy);
        EnableWindow(GetDlgItem(window, ReclaimerLaunch), !busy && reclaimerStartable);
        if (const bool package = game && hasPayload && !same; package != packageListed) { packageListed = package; ListVersions(); }
        EnableWindow(version, !busy && (packageListed || !versions.empty()));
        EnableWindow(GetDlgItem(window, Launch), !busy && installed);
        EnableWindow(GetDlgItem(window, Folder), !busy && game);
        EnableWindow(GetDlgItem(window, Settings), !busy && game &&
            fs::is_regular_file(game->root / L"Halo_MCC_VR" / L"HaloMCCVRSettings.exe"));
        EnableWindow(GetDlgItem(window, Check), !busy);
        EnableWindow(updateSelf, !busy && !RunningFromInstall() && !packageChosen);
        EnableWindow(GetDlgItem(window, Latest), !busy);
        const auto identity = game ? ReadIdentity(game->root / L"Halo_MCC_VR") : BuildIdentity{};
        const bool current = installed && (packageChosen ? !identity.tag.empty() && identity.tag == ReadIdentity(payload).tag
                                                         : !identity.release.empty() && identity.release == latest.tag);
        EnableWindow(GetDlgItem(window, Update), !busy && game && updateReady);
        const bool older = !packageChosen && !versions.empty() && !latest.tag.empty() && latest.tag != versions.front().tag;
        Text(GetDlgItem(window, Update), !installed ? L"INSTALL VR MOD" : current ? L"REINSTALL CURRENT VERSION" : packageChosen ? L"INSTALL THIS DOWNLOAD (Replaces Current Build)"
            : older ? L"INSTALL OLDER VERSION (Replaces Current Build)" : L"INSTALL UPDATE (Replaces Current Build)");
        if (game) Text(path, std::wstring(game->store ? L"Version: Xbox app / Microsoft Store" : L"Version: Steam") +
            L"\nVR folder: " + (game->root / L"Halo_MCC_VR").wstring() );
        else Text(path, L"No MCC installation found. Choose Browse and select the folder containing MCC, or the Xbox Content folder.");
        if (installed) {
            const auto wide = [](const std::string& text) { return std::wstring(text.begin(), text.end()); };
            Text(installedLine, InstalledText(identity.channel.empty() ? L"" : kReleaseChannels[ReleaseChannelIndex(identity.channel)].label, wide(identity.release), wide(identity.published), identity.label, wide(identity.built)));
        } else Text(installedLine, game ? L"Installed: not installed" : L"");
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
std::wstring ReclaimerField(Menu& menu) {
    wchar_t text[2048]{}; GetWindowTextW(menu.reclaimerPath, text, static_cast<int>(std::size(text)));
    return reclaimer_host::TidyPath(text);
}
// Shows what is known of the path in the field; with `keep`, keeps it as the choice.
void ReclaimerShow(Menu& menu, bool keep) {
    const auto exe = ReclaimerField(menu);
    const auto info = reclaimer_launch::Describe(menu.ModFolder(), exe);
    menu.reclaimerStartable = info.startable; menu.Text(menu.reclaimerInfo, info.text);
    if (keep && !reclaimer_launch::SaveClient(menu.ModFolder(), exe))
        menu.Text(menu.status, L"The Project Reclaimer path could not be saved beside the launcher (reclaimer_client.txt). It is used for this launch only.");
    EnableWindow(GetDlgItem(menu.window, ReclaimerLaunch), !menu.busy && menu.reclaimerStartable);
}
void BrowseReclaimer(Menu& menu) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD flags = 0; dialog->GetOptions(&flags); dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    const COMDLG_FILTERSPEC filter[]{{L"Project Reclaimer game program", L"project-reclaimer*.exe"}, {L"Programs", L"*.exe"}};
    dialog->SetFileTypes(static_cast<UINT>(std::size(filter)), filter);
    dialog->SetTitle(L"Choose the Project Reclaimer game program (project-reclaimer-client-v*.exe)");
    if (SUCCEEDED(dialog->Show(menu.window))) {
        IShellItem* item = nullptr; PWSTR path = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            SetWindowTextW(menu.reclaimerPath, path); ReclaimerShow(menu, true);
            menu.Text(menu.status, menu.reclaimerStartable ? L"Project Reclaimer selected. Choose Launch Project Reclaimer when your headset is ready." : L"That file cannot be started with the VR mod: see the note under its path.");
        }
        if (path) CoTaskMemFree(path); if (item) item->Release();
    }
    dialog->Release();
}
void DrawButton(Menu& menu, const DRAWITEMSTRUCT& item) {
    const bool disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED;
    // Install joins them once a version is chosen: it is enabled then, and it is the next step.
    const bool primary = item.CtlID == Launch || item.CtlID == ReclaimerLaunch || item.CtlID == Update;
    const bool warm = item.CtlID == ReclaimerBrowse || item.CtlID == ReclaimerDetect || item.CtlID == ReclaimerLaunch;
    const COLORREF fill = warm
        ? (disabled ? RGB(36, 30, 20) : primary ? (pressed ? RGB(150, 104, 34) : RGB(116, 80, 26)) : (pressed ? RGB(84, 62, 28) : RGB(56, 43, 22)))
        : disabled ? RGB(17, 29, 37) : primary ? (pressed ? RGB(39, 112, 121) : RGB(33, 85, 99)) : (pressed ? RGB(36, 71, 85) : kPanel);
    HBRUSH brush = CreateSolidBrush(fill); FillRect(item.hDC, &item.rcItem, brush); DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, warm ? (disabled ? RGB(74, 60, 38) : primary ? kReclaimerAccent : RGB(150, 112, 56))
                                           : disabled ? RGB(39, 58, 67) : primary ? kAccent : RGB(59, 107, 122));
    const auto oldPen = SelectObject(item.hDC, pen); const auto oldBrush = SelectObject(item.hDC, GetStockObject(HOLLOW_BRUSH));
    Rectangle(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    SelectObject(item.hDC, oldBrush); SelectObject(item.hDC, oldPen); DeleteObject(pen);
    wchar_t text[256]{}; GetWindowTextW(item.hwndItem, text, 256);
    SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, disabled ? (warm ? RGB(120, 102, 76) : RGB(80, 105, 115)) : kText); SelectObject(item.hDC, menu.label);
    RECT rect = item.rcItem; DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) { InflateRect(&rect, -4, -4); DrawFocusRect(item.hDC, &rect); }
}
// The heart beside the Credits title: faceted like the menu's other geometric accents, with a glowing core.
void DrawCreditsHeart(HDC dc, int left, int top, int unit) {
    static constexpr POINT kShape[] = {{3, 0}, {5, 0}, {6, 2}, {7, 0}, {9, 0}, {12, 3}, {12, 5}, {6, 11}, {0, 5}, {0, 3}};
    POINT outer[std::size(kShape)], core[std::size(kShape)];
    for (size_t i = 0; i < std::size(kShape); ++i) {
        outer[i] = {left + kShape[i].x * unit, top + kShape[i].y * unit};
        core[i] = {left + 3 * unit + kShape[i].x * unit / 2, top + 2 * unit + kShape[i].y * unit / 2};
    }
    HPEN edge = CreatePen(PS_SOLID, std::max(1, unit / 2 + 1), kAccent); HBRUSH fill = CreateSolidBrush(kPanel), glow = CreateSolidBrush(kAccent);
    const auto oldPen = SelectObject(dc, edge); const auto oldBrush = SelectObject(dc, fill);
    Polygon(dc, outer, static_cast<int>(std::size(outer)));
    SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, glow);
    Polygon(dc, core, static_cast<int>(std::size(core)));
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(edge); DeleteObject(fill); DeleteObject(glow);
}
// The list comes from CREDITS.txt so that it can change without a new launcher; the launcher carries a copy for when it
// has none to read.
std::vector<CreditEntry> ParseCreditsUtf8(std::string_view bytes) {
    std::wstring text(bytes.size(), L'\0');
    text.resize(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), static_cast<int>(text.size()))));
    return ParseCredits(text);
}
std::vector<CreditEntry> ReadCredits(const fs::path& file) {
    constexpr std::streamsize kLargest = 256 * 1024;
    std::ifstream input(file, std::ios::binary);
    std::string bytes(static_cast<size_t>(kLargest), '\0');
    input.read(bytes.data(), kLargest);
    bytes.resize(static_cast<size_t>(input.gcount()));
    return ParseCreditsUtf8(bytes);
}
void DrawCreditsTitle(Menu& menu, HDC dc) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(35, 75, 91)); const auto old = SelectObject(dc, pen);
    MoveToEx(dc, menu.Px(kCreditsDivider), menu.Px(26), nullptr); LineTo(dc, menu.Px(kCreditsDivider), menu.Px(kMenuHeight - 26));
    SelectObject(dc, old); DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, menu.creditsTitle); SetTextColor(dc, kAccent);
    const wchar_t* title = L"Credits"; const int titleLength = static_cast<int>(wcslen(title));
    SIZE titleSize{}; GetTextExtentPoint32W(dc, title, titleLength, &titleSize);
    TextOutW(dc, menu.Px(kCreditsLeft), menu.Px(30), title, titleLength);
    const int unit = std::max(1, menu.Px(2));
    DrawCreditsHeart(dc, menu.Px(kCreditsLeft) + titleSize.cx + menu.Px(12), menu.Px(30) + (titleSize.cy - 11 * unit) / 2, unit);
}
// Writes the list from the given height down, and returns where it ended.
int DrawCreditsList(Menu& menu, HDC dc, int width, int top) {
    SetBkMode(dc, TRANSPARENT);
    int y = top;
    menu.creditBadges.clear();
    // The name, then its badges beside it, carrying on below when they run out of room.
    const auto nameAndLinks = [&](const CreditEntry& credit) {
        SelectObject(dc, menu.label); SetTextColor(dc, kAccent);
        RECT name{0, y, width, y};
        DrawTextW(dc, credit.name.c_str(), -1, &name, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        DrawTextW(dc, credit.name.c_str(), -1, &name, DT_WORDBREAK | DT_NOPREFIX);
        int bottom = name.bottom, x = name.right + menu.Px(10), row = y - menu.Px(1);
        const int height = menu.Px(20), pad = menu.Px(7);
        SelectObject(dc, menu.smallFont);
        for (const auto& link : credit.links) {
            SIZE text{}; GetTextExtentPoint32W(dc, link.label.c_str(), static_cast<int>(link.label.size()), &text);
            const int badgeWidth = text.cx + 2 * pad;
            if (x + badgeWidth > width && x > 0) { x = 0; row = bottom + menu.Px(4); }
            RECT box{x, row, x + badgeWidth, row + height};
            const bool hover = static_cast<int>(menu.creditBadges.size()) == menu.creditBadgeHover;
            HBRUSH fill = CreateSolidBrush(hover ? RGB(36, 71, 85) : kPanel), edge = CreateSolidBrush(hover ? kAccent : RGB(59, 107, 122));
            FillRect(dc, &box, fill); FrameRect(dc, &box, edge); DeleteObject(fill); DeleteObject(edge);
            SetTextColor(dc, hover ? kAccent : kText);
            DrawTextW(dc, link.label.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            menu.creditBadges.push_back({box, link.url.c_str()});
            x = box.right + menu.Px(6);
            bottom = std::max(bottom, static_cast<int>(box.bottom));
        }
        y = bottom + menu.Px(3);
    };
    const auto paragraph = [&](const wchar_t* text, HFONT font, COLORREF color, int after) {
        SelectObject(dc, font); SetTextColor(dc, color);
        RECT box{0, y, width, y};
        DrawTextW(dc, text, -1, &box, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        DrawTextW(dc, text, -1, &box, DT_WORDBREAK | DT_NOPREFIX);
        y = box.bottom + menu.Px(after);
    };
    for (const auto& credit : menu.credits) {
        if (!credit.section.empty()) { y += menu.Px(8); paragraph(credit.section.c_str(), menu.creditsSection, kMuted, 12); }
        if (!credit.name.empty()) nameAndLinks(credit);
        // Intro lines follow one another with no gap.
        const bool nextIntro = &credit != &menu.credits.back() && (&credit + 1)->name.empty();
        paragraph(credit.what.c_str(), menu.smallFont, kText, !credit.name.empty() ? 14 : nextIntro ? 0 : 18);
    }
    return y;
}
// The scroll bar's thumb, or an empty box when everything fits. Drawn here so it matches the menu.
RECT CreditsThumb(const Menu& menu, const RECT& client) {
    const int view = client.bottom, range = menu.creditsContent - view;
    if (range <= 0) return {};
    const int height = std::max(menu.Px(30), MulDiv(view, view, menu.creditsContent));
    const int top = MulDiv(menu.creditsScroll, view - height, range);
    return {client.right - menu.Px(6), top, client.right, top + height};
}
int CreditBadgeAt(const Menu& menu, LPARAM position) {
    const POINT point{static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    for (size_t i = 0; i < menu.creditBadges.size(); ++i) if (PtInRect(&menu.creditBadges[i].box, point)) return static_cast<int>(i);
    return -1;
}
void ScrollCredits(Menu& menu, int to) {
    RECT client{}; GetClientRect(menu.creditsList, &client);
    menu.creditsScroll = std::clamp(to, 0, std::max(0, menu.creditsContent - static_cast<int>(client.bottom)));
    menu.creditBadgeHover = menu.creditBadgePressed = -1;
    InvalidateRect(menu.creditsList, nullptr, FALSE);
}
LRESULT CALLBACK CreditsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* menu = reinterpret_cast<Menu*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { menu = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu)); }
    if (!menu) return DefWindowProcW(window, message, wParam, lParam);
    RECT client{}; GetClientRect(window, &client);
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC target = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wParam) : BeginPaint(window, &paint);
        // Drawn off screen first, so that scrolling does not flicker.
        HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom); const auto oldBitmap = SelectObject(dc, bitmap);
        FillRect(dc, &client, menu->background);
        const int textWidth = client.right - menu->Px(16);
        menu->creditsContent = DrawCreditsList(*menu, dc, textWidth, -menu->creditsScroll) + menu->creditsScroll;
        if (const int limit = std::max(0, menu->creditsContent - static_cast<int>(client.bottom)); menu->creditsScroll > limit) { menu->creditsScroll = limit; InvalidateRect(window, nullptr, FALSE); }
        if (const RECT thumb = CreditsThumb(*menu, client); thumb.bottom > thumb.top) {
            RECT track{thumb.left, 0, thumb.right, client.bottom};
            HBRUSH trackBrush = CreateSolidBrush(kPanel), thumbBrush = CreateSolidBrush(menu->creditsDragFrom >= 0 ? kAccent : RGB(59, 107, 122));
            FillRect(dc, &track, trackBrush); FillRect(dc, &thumb, thumbBrush); DeleteObject(trackBrush); DeleteObject(thumbBrush);
        }
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
        if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEWHEEL: ScrollCredits(*menu, menu->creditsScroll - MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), menu->Px(60), WHEEL_DELTA)); return 0;
    case WM_SETCURSOR: {
        POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(window, &cursor);
        if (LOWORD(lParam) == HTCLIENT && CreditBadgeAt(*menu, MAKELPARAM(cursor.x, cursor.y)) >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    }
    case WM_MOUSELEAVE: menu->creditBadgeHover = -1; InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        if (const int badge = CreditBadgeAt(*menu, lParam); badge >= 0) { menu->creditBadgePressed = badge; return 0; }
        const RECT thumb = CreditsThumb(*menu, client);
        const int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
        if (thumb.bottom <= thumb.top || x < thumb.left) return 0;
        // Clicking the track pages toward the click; the thumb itself drags.
        if (y < thumb.top) ScrollCredits(*menu, menu->creditsScroll - client.bottom);
        else if (y >= thumb.bottom) ScrollCredits(*menu, menu->creditsScroll + client.bottom);
        else { menu->creditsDragFrom = y; menu->creditsDragScroll = menu->creditsScroll; SetCapture(window); InvalidateRect(window, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (const int badge = CreditBadgeAt(*menu, lParam); menu->creditsDragFrom < 0 && badge != menu->creditBadgeHover) {
            menu->creditBadgeHover = badge;
            TRACKMOUSEEVENT leave{sizeof(leave), TME_LEAVE, window, 0}; TrackMouseEvent(&leave);
            InvalidateRect(window, nullptr, FALSE);
        }
        if (menu->creditsDragFrom >= 0) {
            const RECT thumb = CreditsThumb(*menu, client);
            const int travel = client.bottom - (thumb.bottom - thumb.top);
            if (travel > 0) ScrollCredits(*menu, menu->creditsDragScroll + MulDiv(static_cast<short>(HIWORD(lParam)) - menu->creditsDragFrom, menu->creditsContent - client.bottom, travel));
        }
        return 0;
    case WM_LBUTTONUP:
        if (menu->creditsDragFrom >= 0) ReleaseCapture();
        // Pressed and released on the same badge.
        if (const int badge = CreditBadgeAt(*menu, lParam); badge >= 0 && badge == menu->creditBadgePressed) ShellExecuteW(window, L"open", menu->creditBadges[static_cast<size_t>(badge)].url, nullptr, nullptr, SW_SHOWNORMAL);
        menu->creditBadgePressed = -1;
        return 0;
    case WM_CAPTURECHANGED: menu->creditsDragFrom = -1; InvalidateRect(window, nullptr, FALSE); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void DrawCheckbox(Menu& menu, const DRAWITEMSTRUCT& item, bool checked) {
    const bool disabled = item.itemState & ODS_DISABLED;
    HBRUSH clear = CreateSolidBrush(kBackground); FillRect(item.hDC, &item.rcItem, clear); DeleteObject(clear);
    const int size = menu.Px(22), top = (item.rcItem.top + item.rcItem.bottom - size) / 2;
    RECT box{item.rcItem.left, top, item.rcItem.left + size, top + size};
    HBRUSH fill = CreateSolidBrush(disabled ? RGB(17, 29, 37) : kPanel); FillRect(item.hDC, &box, fill); DeleteObject(fill);
    HPEN edge = CreatePen(PS_SOLID, 1, disabled ? RGB(39, 58, 67) : item.itemState & ODS_FOCUS ? kAccent : RGB(59, 107, 122));
    const auto oldPen = SelectObject(item.hDC, edge); const auto oldBrush = SelectObject(item.hDC, GetStockObject(HOLLOW_BRUSH));
    Rectangle(item.hDC, box.left, box.top, box.right, box.bottom);
    SelectObject(item.hDC, oldBrush); SelectObject(item.hDC, oldPen); DeleteObject(edge);
    if (checked) {
        HPEN tick = CreatePen(PS_SOLID, menu.Px(2), disabled ? RGB(80, 105, 115) : kAccent); const auto before = SelectObject(item.hDC, tick);
        MoveToEx(item.hDC, box.left + size * 5 / 22, box.top + size * 11 / 22, nullptr);
        LineTo(item.hDC, box.left + size * 9 / 22, box.top + size * 15 / 22);
        LineTo(item.hDC, box.left + size * 17 / 22, box.top + size * 6 / 22);
        SelectObject(item.hDC, before); DeleteObject(tick);
    }
    wchar_t text[256]{}; GetWindowTextW(item.hwndItem, text, 256);
    SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, disabled ? RGB(80, 105, 115) : kText); SelectObject(item.hDC, menu.body);
    RECT rect = item.rcItem; rect.left += size + menu.Px(10);
    DrawTextW(item.hDC, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}
// The closed drop-down, drawn whole so it matches the buttons beside it; the open list stays the control's own. The
// repository's reads as a sentence with its choice.
void PaintCombo(Menu& menu, HWND combo, HDC dc) {
    RECT client{}; GetClientRect(combo, &client);
    const bool disabled = !IsWindowEnabled(combo), focused = GetFocus() == combo;
    HBRUSH brush = CreateSolidBrush(disabled ? RGB(17, 29, 37) : kPanel); FillRect(dc, &client, brush); DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, disabled ? RGB(39, 58, 67) : focused ? kAccent : RGB(59, 107, 122));
    const auto oldPen = SelectObject(dc, pen); const auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, client.left, client.top, client.right, client.bottom);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
    std::wstring text = GetDlgCtrlID(combo) == Games ? L"Select a detected MCC installation" : L"";
    const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index >= 0) {
        const auto length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
        if (length >= 0 && length < 32768) { text.resize(static_cast<size_t>(length) + 1); SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data())); text.resize(static_cast<size_t>(length)); }
    }
    if (GetDlgCtrlID(combo) == Channel) text = L"Update from repository: " + text;
    if (GetDlgCtrlID(combo) == Version) text = index >= 0 ? L"Version: " + text : L"Version: choose Check for updates to list them";
    const int arrowArea = menu.Px(32);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, disabled ? RGB(80, 105, 115) : kText); SelectObject(dc, menu.body);
    RECT rect = client; rect.left += menu.Px(12); rect.right -= arrowArea;
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    HPEN arrow = CreatePen(PS_SOLID, menu.Px(2), disabled ? RGB(80, 105, 115) : kAccent); const auto before = SelectObject(dc, arrow);
    const int x = client.right - arrowArea / 2, y = (client.top + client.bottom) / 2, size = menu.Px(5);
    MoveToEx(dc, x - size, y - size / 2, nullptr); LineTo(dc, x, y + size / 2 + 1); LineTo(dc, x + size + 1, y - size / 2 - 1);
    SelectObject(dc, before); DeleteObject(arrow);
}
LRESULT CALLBACK ComboProc(HWND combo, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(combo, &paint);
        PaintCombo(*reinterpret_cast<Menu*>(data), combo, dc);
        EndPaint(combo, &paint);
        return 0;
    }
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: InvalidateRect(combo, nullptr, FALSE); break;
    case WM_NCDESTROY: RemoveWindowSubclass(combo, ComboProc, 1); break;
    }
    return DefSubclassProc(combo, message, wParam, lParam);
}
void StyleCombo(Menu& menu, HWND combo) {
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), menu.Px(32));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, menu.Px(29));
    SetWindowSubclass(combo, ComboProc, 1, reinterpret_cast<DWORD_PTR>(&menu));
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* menu = reinterpret_cast<Menu*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { menu = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); menu->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu)); }
    if (!menu) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: {
        menu->background = CreateSolidBrush(kBackground); menu->panel = CreateSolidBrush(kPanel);
        menu->reclaimerPanel = CreateSolidBrush(kReclaimerPanel); menu->reclaimerField = CreateSolidBrush(kReclaimerField);
        menu->body = Font(*menu, 16, FW_NORMAL, L"Segoe UI"); menu->smallFont = Font(*menu, 13, FW_NORMAL, L"Segoe UI"); menu->label = Font(*menu, 14, FW_SEMIBOLD, menu->fontLoaded ? L"Oxanium" : L"Bahnschrift"); menu->creditsTitle = Font(*menu, 26, FW_SEMIBOLD, menu->fontLoaded ? L"Oxanium" : L"Bahnschrift"); menu->creditsSection = Font(*menu, 19, FW_SEMIBOLD, menu->fontLoaded ? L"Oxanium" : L"Bahnschrift");
        // Use a user's installed Halo font when available. No proprietary font
        // is redistributed. The bundled OFL-licensed Oxanium supplies an angular
        // sci-fi face, with Bahnschrift as a final Windows fallback.
        HDC dc = GetDC(window); LOGFONTW query{}; wcscpy_s(query.lfFaceName, L"Halo"); bool found = false;
        EnumFontFamiliesExW(dc, &query, FontExists, reinterpret_cast<LPARAM>(&found), 0); ReleaseDC(window, dc);
        menu->title = Font(*menu, 45, FW_NORMAL, found ? L"Halo" : menu->fontLoaded ? L"Oxanium" : L"Bahnschrift SemiCondensed");
        Label(*menu, L"01  /  GAME INSTALLATION", 38, 134, 470);
        menu->games = Control(*menu, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 38, 160, 642, 230, Games);
        StyleCombo(*menu, menu->games);
        Control(*menu, L"BUTTON", L"BROWSE", BS_OWNERDRAW | WS_TABSTOP, 696, 160, 122, 38, Browse);
        Control(*menu, L"BUTTON", L"DETECT", BS_OWNERDRAW | WS_TABSTOP, 832, 160, 110, 38, Scan);
        menu->path = Control(*menu, L"STATIC", L"", SS_LEFT, 40, 216, 640, 46);
        SendMessageW(menu->path, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        Control(*menu, L"BUTTON", L"MOD FOLDER", BS_OWNERDRAW | WS_TABSTOP, 696, 219, 246, 38, Folder);
        Label(*menu, L"02  /  RELEASE UPDATES", 38, 281, 570);
        menu->installedLine = Control(*menu, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 40, 308, 895, 22);
        SendMessageW(menu->installedLine, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        // The same columns as section 01, and top to bottom in the order they are used: where from, which version, install.
        menu->channel = Control(*menu, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 38, 336, 642, 140, Channel);
        StyleCombo(*menu, menu->channel);
        for (const auto& source : kReleaseChannels) SendMessageW(menu->channel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(source.label));
        Control(*menu, L"BUTTON", L"LATEST", BS_OWNERDRAW | WS_TABSTOP, 696, 336, 246, 38, Latest);
        menu->version = Control(*menu, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 38, 386, 642, 240, Version);
        StyleCombo(*menu, menu->version);
        Control(*menu, L"BUTTON", L"CHECK FOR UPDATES", BS_OWNERDRAW | WS_TABSTOP, 696, 386, 246, 38, Check);
        Control(*menu, L"BUTTON", L"INSTALL", BS_OWNERDRAW | WS_TABSTOP, 38, 436, 642, 38, Update);
        Control(*menu, L"BUTTON", L"RELEASE PAGE", BS_OWNERDRAW | WS_TABSTOP, 696, 436, 246, 38, ReleasePage);
        // Above keep-settings, which then sits right over the settings button below it.
        menu->updateSelf = Control(*menu, L"BUTTON", L"Update this launcher from the selected release", BS_OWNERDRAW | WS_TABSTOP, 38, 482, 850, 30, UpdateSelf);
        menu->retain = Control(*menu, L"BUTTON", L"Keep my settings and add new options when updating", BS_OWNERDRAW | WS_TABSTOP, 38, 512, 850, 30, Retain);
        Label(*menu, L"03  /  YOUR VR SETUP", 38, 561, 570);
        Control(*menu, L"BUTTON", L"VR SETTINGS", BS_OWNERDRAW | WS_TABSTOP, 38, 590, 270, 48, Settings);
        Control(*menu, L"BUTTON", L"LAUNCH MCC", BS_OWNERDRAW | WS_TABSTOP, 324, 590, 618, 48, Launch);
        Label(*menu, L"04  /  EXTERNAL MODS", 38, kExternalTop + 11, 570);
        // The note under that heading, and the tab over the panel, are painted (WM_PAINT).
        menu->reclaimerPath = Control(*menu, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 46, kReclaimerTop + 18, 634, 32, ReclaimerPath);
        SendMessageW(menu->reclaimerPath, EM_SETLIMITTEXT, 2000, 0);
        Control(*menu, L"BUTTON", L"BROWSE", BS_OWNERDRAW | WS_TABSTOP, 696, kReclaimerTop + 15, 122, 38, ReclaimerBrowse);
        Control(*menu, L"BUTTON", L"DETECT", BS_OWNERDRAW | WS_TABSTOP, 832, kReclaimerTop + 15, 110, 38, ReclaimerDetect);
        menu->reclaimerInfo = Control(*menu, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 48, kReclaimerTop + 58, 890, 38);
        SendMessageW(menu->reclaimerInfo, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        Control(*menu, L"BUTTON", L"LAUNCH PROJECT RECLAIMER", BS_OWNERDRAW | WS_TABSTOP, 46, kReclaimerTop + 100, 406, 42, ReclaimerLaunch);
        menu->status = Control(*menu, L"STATIC", L"", SS_LEFT, 40, kReclaimerBottom + 16, 895, 52);
        SendMessageW(menu->status, WM_SETFONT, reinterpret_cast<WPARAM>(menu->smallFont), TRUE);
        menu->creditsList = CreateWindowExW(0, L"HaloMCCVRCredits", L"", WS_CHILD | WS_VISIBLE, menu->Px(kCreditsLeft), menu->Px(kCreditsListTop),
            menu->Px(kCreditsListWidth), menu->Px(kMenuHeight - kCreditsListTop - 26), window, nullptr, menu->instance, menu);
        return 0;
    }
    case WM_PRINTCLIENT: case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wParam) : BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, menu->background);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kAccent); SelectObject(dc, menu->title);
        SetTextCharacterExtra(dc, menu->Px(4)); TextOutW(dc, menu->Px(37), menu->Px(30), L"HALO MCC VR", 11); SetTextCharacterExtra(dc, 0);
        SelectObject(dc, menu->label); SetTextColor(dc, kMuted); const wchar_t* subtitle = L"The Master Chief Collection in Virtual Reality";
        TextOutW(dc, menu->Px(41), menu->Px(93), subtitle, static_cast<int>(wcslen(subtitle)));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(35, 75, 91)); auto old = SelectObject(dc, pen);
        for (int y : {121, 270, 550, kExternalTop}) { MoveToEx(dc, menu->Px(38), menu->Px(y), nullptr); LineTo(dc, menu->Px(942), menu->Px(y)); }
        // Original geometric HUD accents; no extracted game art or assets.
        for (int x = 811; x < 943; x += 22) { MoveToEx(dc, menu->Px(x), menu->Px(43), nullptr); LineTo(dc, menu->Px(x + 18), menu->Px(25)); }
        SelectObject(dc, old); DeleteObject(pen);
        DrawCreditsTitle(*menu, dc);
        // External Mods: the note under its heading, then Project Reclaimer's tab and panel, in its own colour. The tab
        // opens into the panel below it.
        {
            SetBkMode(dc, TRANSPARENT); SelectObject(dc, menu->smallFont); SetTextColor(dc, kMuted);
            const wchar_t* note = L"External mods share the same VR settings as MCC VR itself.";
            TextOutW(dc, menu->Px(40), menu->Px(kExternalTop + 38), note, static_cast<int>(wcslen(note)));
            RECT box{menu->Px(kReclaimerLeft), menu->Px(kReclaimerTop), menu->Px(kReclaimerRight), menu->Px(kReclaimerBottom)};
            FillRect(dc, &box, menu->reclaimerPanel);
            HBRUSH edge = CreateSolidBrush(RGB(122, 90, 44)), bar = CreateSolidBrush(kReclaimerAccent);
            FrameRect(dc, &box, edge);
            // The tab, square at the left, in line with the panel, and slanted at the right like a folder's. The letter spacing also follows the last letter, so the name
            // is placed by its own width rather than centred.
            SelectObject(dc, menu->label); SetTextCharacterExtra(dc, menu->Px(2));
            const wchar_t* name = L"PROJECT RECLAIMER";
            SIZE nameSize{}; GetTextExtentPoint32W(dc, name, static_cast<int>(wcslen(name)), &nameSize);
            const int slant = menu->Px(12), pad = menu->Px(14), accent = menu->Px(3), top = menu->Px(kReclaimerTop - kTabHeight), height = box.top - top;
            const int left = box.left, right = left + slant + 2 * pad + nameSize.cx - menu->Px(2);
            HPEN outline = CreatePen(PS_SOLID, 1, RGB(122, 90, 44));
            const auto oldPen = SelectObject(dc, outline); const auto oldBrush = SelectObject(dc, menu->reclaimerPanel);
            const POINT shape[]{{left, box.top}, {left, top}, {right - slant, top}, {right, box.top}};
            Polygon(dc, shape, static_cast<int>(std::size(shape)));
            const int inset = MulDiv(slant, accent, height);
            const POINT strip[]{{left, top}, {right - slant, top}, {right - slant + inset, top + accent}, {left, top + accent}};
            SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, bar);
            Polygon(dc, strip, static_cast<int>(std::size(strip)));
            SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(outline);
            RECT open{left + 1, box.top, right, box.top + 1}; FillRect(dc, &open, menu->reclaimerPanel);
            DeleteObject(edge); DeleteObject(bar);
            SetTextColor(dc, kReclaimerAccent);
            TextOutW(dc, left + pad, top + accent + (height - accent - nameSize.cy) / 2, name, static_cast<int>(wcslen(name)));
            SetTextCharacterExtra(dc, 0);
        }
        if (message == WM_PAINT) EndPaint(window, &paint); return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        if (reinterpret_cast<HWND>(lParam) == menu->reclaimerInfo) { SetTextColor(dc, kText); SetBkColor(dc, kReclaimerPanel); return reinterpret_cast<LRESULT>(menu->reclaimerPanel); }
        SetTextColor(dc, kText); SetBkColor(dc, kBackground); return reinterpret_cast<LRESULT>(menu->background);
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        if (reinterpret_cast<HWND>(lParam) == menu->reclaimerPath) { SetTextColor(dc, kText); SetBkColor(dc, kReclaimerField); return reinterpret_cast<LRESULT>(menu->reclaimerField); }
        SetTextColor(dc, kText); SetBkColor(dc, kPanel); return reinterpret_cast<LRESULT>(menu->panel);
    }
    case WM_MEASUREITEM:
        if (reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlID == Games || reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlID == Channel || reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlID == Version) { reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = menu->Px(29); return TRUE; }
        break;
    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item.CtlID == Games || item.CtlID == Channel || item.CtlID == Version) {
            // The closed box is painted whole (PaintCombo); a selection only asks it to paint again.
            if (item.itemState & ODS_COMBOBOXEDIT) { InvalidateRect(item.hwndItem, nullptr, FALSE); return TRUE; }
            HBRUSH row = CreateSolidBrush(item.itemState & ODS_SELECTED ? RGB(36, 71, 85) : kPanel); FillRect(item.hDC, &item.rcItem, row); DeleteObject(row); SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, kText); SelectObject(item.hDC, menu->body);
            std::wstring text = L"Select a detected MCC installation";
            if (item.itemID != static_cast<UINT>(-1)) { const auto length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0); if (length >= 0 && length < 32768) { text.resize(static_cast<size_t>(length) + 1); SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(text.data())); text.resize(static_cast<size_t>(length)); } }
            RECT rect = item.rcItem; rect.left += menu->Px(8); rect.right -= menu->Px(4); DrawTextW(item.hDC, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        } else if (item.CtlID == Retain) DrawCheckbox(*menu, item, menu->keepSettings);
        else if (item.CtlID == UpdateSelf) DrawCheckbox(*menu, item, menu->UpdateSelfShown());
        else DrawButton(*menu, item);
        return TRUE;
    }
    case WM_COMMAND: {
        if (menu->busy) return 0;
        switch (LOWORD(wParam)) {
        case Retain: menu->keepSettings = !menu->keepSettings; InvalidateRect(menu->retain, nullptr, FALSE); break;
        case UpdateSelf: menu->updateThisLauncher = !menu->updateThisLauncher; InvalidateRect(menu->updateSelf, nullptr, FALSE); break;
        case Games: if (HIWORD(wParam) == CBN_SELCHANGE) { menu->Refresh(); ReclaimerShow(*menu, false); } break;
        case Channel:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                menu->channelChosen = true; menu->channelChanged = true; menu->versions.clear(); menu->ListVersions();
                menu->Refresh();
            }
            // Choosing a repository lists its versions, once the list has closed: arrowing through an open list changes
            // the selection on the way to the one wanted.
            if ((HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_CLOSEUP) && menu->channelChanged && !SendMessageW(menu->channel, CB_GETDROPPEDSTATE, 0, 0)) {
                menu->channelChanged = false;
                PostMessageW(window, WM_COMMAND, Check, 0);
            }
            break;
        case Version: if (HIWORD(wParam) == CBN_SELCHANGE) {
            menu->ChooseVersion(static_cast<size_t>(SendMessageW(menu->version, CB_GETCURSEL, 0, 0)));
            menu->Refresh();
        } break;
        case Browse: BrowseFolder(*menu); break;
        case Scan: menu->installs = DetectInstalls(menu->selfDir); menu->Populate(); menu->Text(menu->status, L"Detection complete. Select the MCC installation you want to use."); break;
        case Folder: if (auto* game = menu->Selected()) { auto path = game->root / L"Halo_MCC_VR"; if (!fs::exists(path)) path = game->root; ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); } break;
        case Settings: if (auto* game = menu->Selected()) {
            const auto folder = game->root / L"Halo_MCC_VR";
            const std::wstring config = L"\"" + (folder / L"halomccvr.cfg").wstring() + L"\"";
            ShellExecuteW(window, L"open", (folder / L"HaloMCCVRSettings.exe").c_str(), config.c_str(), folder.c_str(), SW_SHOWNORMAL);
        } break;
        case ReleasePage: ShellExecuteW(window, L"open", menu->ReleasePageUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case Launch: if (auto* game = menu->Selected()) { menu->launchDir = (game->root / L"Halo_MCC_VR").wstring(); DestroyWindow(window); } break;
        case ReclaimerPath:
            if (HIWORD(wParam) == EN_CHANGE) ReclaimerShow(*menu, false);
            else if (HIWORD(wParam) == EN_KILLFOCUS) ReclaimerShow(*menu, true);
            break;
        case ReclaimerBrowse: BrowseReclaimer(*menu); break;
        case ReclaimerDetect: {
            std::wstring how; const auto found = reclaimer_launch::DetectClient(menu->selfDir, how);
            if (found.empty()) menu->Text(menu->status, L"No Project Reclaimer was found. Run Project Reclaimer once so that Windows knows where it is, then choose Detect again; or choose Browse and pick its program (project-reclaimer-client-v*.exe).");
            else { SetWindowTextW(menu->reclaimerPath, found.c_str()); ReclaimerShow(*menu, true); menu->Text(menu->status, how); }
        } break;
        case ReclaimerLaunch:
            ReclaimerShow(*menu, true);
            if (menu->reclaimerStartable) { menu->reclaimerExe = ReclaimerField(*menu); menu->reclaimerDir = menu->ModFolder().wstring(); DestroyWindow(window); }
            break;
        case Check:
            menu->updateReady = false; menu->Text(menu->status, std::wstring(L"Listing the releases from ") + menu->CurrentChannel().label + L"...");
            menu->Start(Menu::Work::Check, [menu, source = &menu->CurrentChannel()] { menu->versions = ListReleases(*source); }); break;
        case Latest:
            menu->updateReady = false; menu->Text(menu->status, L"Comparing the newest release from each repository...");
            menu->Start(Menu::Work::Latest, [menu] { menu->versions = ListLatestAcrossChannels(menu->latestChannel, menu->latestPassedOver); }); break;
        case Update: if (auto* game = menu->Selected(); game && menu->packageChosen) {
            const auto target = *game; const auto payload = menu->payload; const bool retain = menu->keepSettings;
            menu->Text(menu->status, L"Verifying files and installing. MCC will not start until you choose Launch MCC.");
            menu->Start(Menu::Work::Install, [menu, target, payload, retain] { menu->operationMessage = InstallPayload(payload, target, retain).message; });
        } else if (game && menu->updateReady) {
            menu->operationGame = *game; menu->operationRetain = menu->keepSettings;
            menu->Text(menu->status, L"Downloading and verifying the release. The launcher will reopen to install it. MCC will stay closed.");
            menu->Start(Menu::Work::Download, [menu] { menu->downloaded = DownloadUpdate(menu->latest); });
        } break;
        }
        return 0;
    }
    case kDone:
        if (menu->worker.joinable()) menu->worker.join();
        menu->busy = false;
        if (menu->work == Menu::Work::Install) { menu->ListVersions(); ReclaimerShow(*menu, false); }
        if (menu->operationMessage.empty() && menu->work == Menu::Work::Check) {
            menu->ListVersions();
        } else if (menu->operationMessage.empty() && menu->work == Menu::Work::Latest) {
            menu->channelChosen = true;
            SendMessageW(menu->channel, CB_SETCURSEL, menu->latestChannel, 0);
            menu->ListVersions();
            if (!menu->latestPassedOver.empty()) menu->operationMessage = L"Could not read: " + menu->latestPassedOver + L".";
        } else if (menu->operationMessage.empty() && menu->work == Menu::Work::Download) {
            // For the install to keep: the package itself does not know its release's name or date.
            {
                std::ofstream saved(menu->downloaded.parent_path() / L"INSTALLED-RELEASE.txt", std::ios::trunc);
                saved << "release=" << menu->latest.tag << "\npublished=" << menu->latest.published.substr(0, 10) << "\nbuild=" << ReadIdentity(menu->downloaded).tag << "\n";
            }
            if (StartUpdateHelper(menu->downloaded, menu->operationGame, menu->operationRetain, menu->updateThisLauncher, menu->operationMessage)) {
                menu->helperStarted = true; DestroyWindow(window); return 0;
            }
        }
        menu->Text(menu->status, menu->operationMessage); menu->Refresh(); return 0;
    case WM_CLOSE: if (!menu->busy) DestroyWindow(window); return 0;
    case WM_MOUSEWHEEL: {
        // A wheel turned over the list scrolls it, whichever control has the keyboard focus.
        const POINT cursor{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        RECT list{}; GetWindowRect(menu->creditsList, &list);
        if (PtInRect(&list, cursor)) return SendMessageW(menu->creditsList, message, wParam, lParam);
        break;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
std::wstring ShowLauncherMenu(HINSTANCE instance, const fs::path& launcherDir, int showCommand, std::wstring* reclaimerDir, std::wstring* reclaimerExe) {
    Menu menu; menu.instance = instance; menu.selfDir = launcherDir;
    menu.payload = fs::is_regular_file(launcherDir / L"ModFiles" / L"INSTALL-MANIFEST.sha256") ? launcherDir / L"ModFiles" : launcherDir;
    menu.credits = ReadCredits(menu.payload / L"CREDITS.txt");
    DWORD fontsAdded = 0;
    menu.font = AddFontMemResourceEx(const_cast<unsigned char*>(menu_assets::oxanium), sizeof(menu_assets::oxanium), nullptr, &fontsAdded);
    menu.fontLoaded = menu.font != nullptr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW type{sizeof(type)}; type.lpfnWndProc = WindowProc; type.hInstance = instance; type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hIcon = LoadIconW(nullptr, IDI_APPLICATION); type.lpszClassName = L"HaloMCCVRSetupMenu";
    RegisterClassExW(&type);
    WNDCLASSEXW creditsType{sizeof(creditsType)}; creditsType.lpfnWndProc = CreditsProc; creditsType.hInstance = instance; creditsType.hCursor = LoadCursorW(nullptr, IDC_ARROW); creditsType.lpszClassName = L"HaloMCCVRCredits";
    RegisterClassExW(&creditsType);
    RECT workArea{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int fitWidth = MulDiv(workArea.right - workArea.left - 32, 96, kMenuWidth);
    const int fitHeight = MulDiv(workArea.bottom - workArea.top - 55, 96, kMenuHeight);
    const int dpi = std::max(60, std::min({static_cast<int>(GetDpiForSystem()), fitWidth, fitHeight}));
    menu.dpi = dpi;
    RECT rect{0, 0, MulDiv(kMenuWidth, dpi, 96), MulDiv(kMenuHeight, dpi, 96)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    HWND window = CreateWindowExW(0, type.lpszClassName, L"Halo MCC VR | Setup & Launch", style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &menu);
    if (window) {
        menu.installs = DetectInstalls(launcherDir); menu.Populate();
        if (const auto* game = menu.Selected(); menu.credits.empty() && game) menu.credits = ReadCredits(game->root / L"Halo_MCC_VR" / L"CREDITS.txt");
        if (menu.credits.empty()) menu.credits = ParseCreditsUtf8({reinterpret_cast<const char*>(menu_assets::credits), sizeof(menu_assets::credits)});
        // The kept Project Reclaimer program; with none kept, one that is found is offered and kept only once the
        // player uses it.
        {
            std::wstring how; auto client = reclaimer_launch::SavedClient(menu.ModFolder());
            if (client.empty()) client = reclaimer_launch::SavedClient(launcherDir);
            if (client.empty()) client = reclaimer_launch::DetectClient(launcherDir, how);
            SetWindowTextW(menu.reclaimerPath, client.c_str()); ReclaimerShow(menu, false);
        }
        ShowWindow(window, showCommand); UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (menu.worker.joinable()) menu.worker.join();
    for (HFONT font : {menu.body, menu.smallFont, menu.title, menu.label, menu.creditsTitle, menu.creditsSection}) if (font) DeleteObject(font);
    if (menu.background) DeleteObject(menu.background); if (menu.panel) DeleteObject(menu.panel);
    if (menu.reclaimerPanel) DeleteObject(menu.reclaimerPanel); if (menu.reclaimerField) DeleteObject(menu.reclaimerField);
    if (menu.font) RemoveFontMemResourceEx(menu.font);
    if (SUCCEEDED(com)) CoUninitialize();
    if (reclaimerExe) *reclaimerExe = menu.reclaimerExe;
    if (reclaimerDir) *reclaimerDir = menu.reclaimerDir;
    return menu.launchDir;
}
} // namespace mcc_installer
