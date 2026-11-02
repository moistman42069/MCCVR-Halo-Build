#include <windows.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <shobjidl_core.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "installer.h"
#include "../common/manual_vr_recovery.h"
#include "../common/reclaimer_host_logic.h"
#include "reclaimer_menu.h"
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
// Constants only (native raster size and the resolution_scale limits) so the
// launcher and the DLL's config clamp can never disagree. The launcher does
// not link config.cpp; it reads the one line it needs itself.
#include "../common/config.h"

// Starts the MCC shipping executable directly — which is exactly what Steam's
// official "Play without anti-cheat" option runs, and what the Microsoft Store
// package's own "Halo: MCC Anti-Cheat Disabled (Mods and Limited Services)"
// entry runs, so EAC is never started — and loads HaloMCCVR.dll into it before
// the game's first instruction runs.
//
// Injection method: classic CreateRemoteThread + LoadLibraryW. The process is
// created suspended, a remote thread loads our DLL, then the game resumes.
//
// Two editions ship the same game with different shipping executables:
//
//   Steam                MCC\Binaries\Win64\MCC-Win64-Shipping.exe
//   Microsoft Store      MCC\Binaries\Win64\MCCWinStore-Win64-Shipping.exe
//   (Xbox app/Game Pass)
//
// The per-game modules the DLL actually hooks (halo3.dll, halo3odst.dll,
// haloreach.dll) are byte-identical between the two editions apart from their
// Authenticode signature and PE checksum, so one build serves both and no
// signature, offset or struct layout changes. Only the launch differs:
//
//   * Only the Steam edition needs Steam. The Store edition is licensed by the
//     Xbox app, so requiring steam.exe there would block a valid install.
//   * The SteamAppId environment variables exist purely so Steamworks does not
//     bounce the process ("relaunch me through Steam"), which would kill the
//     process we just injected into and start a fresh, unmodded one. The Store
//     build does not use Steamworks, so it is not given them.
//
// Edition is decided by the install layout, not just the executable name: the
// Store package roots the game at ...\Content\ next to MicrosoftGame.config,
// which no Steam install has. That way a Store install whose executable was
// renamed to the Steam name — the old community workaround this replaces — is
// still recognised as a Store install and is not asked to start Steam.

static const wchar_t* kSteamAppId = L"976730"; // Halo: The Master Chief Collection

static const wchar_t* kSteamExeName = L"MCC-Win64-Shipping.exe";
static const wchar_t* kStoreExeName = L"MCCWinStore-Win64-Shipping.exe";

enum class Edition
{
    Steam,
    Store,
};

static const wchar_t* EditionName(Edition edition)
{
    return edition == Edition::Store ? L"Microsoft Store / Xbox app (Game Pass)"
                                     : L"Steam";
}

static std::wstring g_logPath;

// --headless (for unattended test scripts such as tools/Enter-Reach-FA.ps1): launch the mod in the launcher's own
// folder at once, without the setup menu, and never show a dialog. Every message then reaches only
// HaloMCCVRLauncher.log, in full, and the exit code says what happened -- so an unattended run can never
// leave a modal box on the desktop of whoever is using the PC (a "Steam is not running" box would
// otherwise sit there after the script has given up). A test hook, so it is compiled into
// dev builds only (HALOMCCVR_DEV_DIAGNOSTICS): a player's launcher never parses it, and no player-facing
// string names it (tools/verify-qol-package.py checks the packaged launcher for the switch).
#if HALOMCCVR_DEV_DIAGNOSTICS
static bool g_headless = false;
#else
static constexpr bool g_headless = false;
#endif

// --flat-probe (dev builds only): the AS SEEN control arm. Starts MCC exactly as the VR launch does
// (anti-cheat never started: Steam's direct shipping executable / the Store's NoEAC entry) but FLAT -- no VR render
// surface arguments, no render-size environment -- and loads ONLY HaloMCCFlatProbe.dll from this folder, an
// observe-only measurement DLL (src/probe/flat_probe.cpp). HaloMCCVR.dll is never loaded in this mode. Refused from
// a folder named Halo_MCC_VR (the player's live install). tools/Install-FlatProbe.ps1 stages the folder.
#if HALOMCCVR_DEV_DIAGNOSTICS
static bool g_flatProbe = false;
#else
static constexpr bool g_flatProbe = false;
#endif

// Exit codes. A launch reports success only after the game has run for 12 s.
constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitSteamNotRunning = 2; // the Steam edition, with no steam.exe process

static void LauncherLog(const char* fmt, ...)
{
    if (g_logPath.empty())
        return;
    FILE* f = nullptr;
    _wfopen_s(&f, g_logPath.c_str(), L"at");
    if (!f)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// A message as ONE log line: its lines joined by " | " (blank lines dropped), in UTF-8, so the log alone
// says what the player was told.
static std::string LogLine(const std::wstring& text)
{
    std::wstring flat;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
            end = text.size();
        size_t first = start, last = end;
        while (first < last && (text[first] == L' ' || text[first] == L'\t'))
            ++first;
        while (last > first && (text[last - 1] == L'\r' || text[last - 1] == L' ' || text[last - 1] == L'\t'))
            --last;
        if (last > first)
        {
            if (!flat.empty())
                flat += L" | ";
            flat.append(text, first, last - first);
        }
        start = end + 1;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, flat.c_str(), static_cast<int>(flat.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? static_cast<size_t>(bytes) : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, flat.c_str(), static_cast<int>(flat.size()), out.data(), bytes,
                            nullptr, nullptr);
    return out;
}

static void ErrorBox(const std::wstring& text)
{
    LauncherLog("ERROR %s: %s", g_headless ? "(unattended, no dialog)" : "shown to user", LogLine(text).c_str());
    if (!g_headless)
        MessageBoxW(nullptr, text.c_str(), L"Halo MCC VR launcher", MB_OK | MB_ICONERROR | MB_TOPMOST);
}

static void InfoBox(const std::wstring& text)
{
    LauncherLog("NOTICE shown to user: %s", LogLine(text).c_str());
    MessageBoxW(nullptr, text.c_str(), L"Halo MCC VR launcher", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

static std::wstring WinErr(DWORD code)
{
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring s = buf ? buf : L"(unknown error)";
    if (buf)
        LocalFree(buf);
    wchar_t num[32];
    swprintf_s(num, L" (code %lu)", code);
    return s + num;
}

static bool FileExists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static DWORD FindProcessId(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe{sizeof(pe)};
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, exeName) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool ProcessRunning(const wchar_t* exeName)
{
    return FindProcessId(exeName) != 0;
}

static int OfferManualVrRecovery(DWORD pid, const std::wstring& expectedExe)
{
    if (g_headless)
    {
        // Recovery is a person's choice in a dialog; an unattended launch neither offers it nor starts a
        // second copy of the game.
        ErrorBox(L"MCC is already running (pid " + std::to_wstring(pid) +
                 L"). An unattended launch neither offers VR recovery nor starts a second copy.");
        return kExitFailed;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t imagePath[32768]{};
    DWORD imageLength = static_cast<DWORD>(std::size(imagePath));
    const bool sameInstall = process &&
        QueryFullProcessImageNameW(process, 0, imagePath, &imageLength) &&
        _wcsicmp(imagePath, expectedExe.c_str()) == 0;
    if (process) CloseHandle(process);
    if (!sameInstall)
    {
        ErrorBox(L"MCC is running from another installation. Use that installation's VR launcher to recover it.");
        return 1;
    }
    constexpr int recoverButton = 100;
    const TASKDIALOG_BUTTON buttons[]{
        {recoverButton, L"Force injection / recover VR"}};
    TASKDIALOGCONFIG dialog{sizeof(dialog)};
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    dialog.pszWindowTitle = L"Halo MCC VR launcher";
    dialog.pszMainInstruction = L"MCC is already running";
    dialog.pszContent = L"Retry VR for the active game: CE, Halo 2, Halo 3, ODST, Reach or Halo 4.\n"
        L"Both Original/Classic and Anniversary graphics are supported.\n"
        L"The level must finish loading before stereo can resume.\n"
        L"Check F1 > Status in the headset for the result.";
    dialog.cButtons = static_cast<UINT>(std::size(buttons));
    dialog.pButtons = buttons;
    dialog.nDefaultButton = IDCANCEL;
    int selected = IDCANCEL;
    const HRESULT result = TaskDialogIndirect(&dialog, &selected, nullptr, nullptr);
    if (FAILED(result))
    {
        ErrorBox(L"Could not display the recovery button. Use F1 > Status > Force injection / recover VR.");
        return 1;
    }
    if (selected != recoverButton) return 0;
    wchar_t eventName[96]{};
    ManualVrRecoveryEventName(eventName, pid);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    if (!event)
    {
        ErrorBox(L"This running game does not expose VR recovery.\n\n"
            L"Close MCC, update both the VR DLL and launcher, then start MCC through the VR launcher.\n"
            L"An older or unloaded mod cannot receive this request.");
        return 1;
    }
    const bool signaled = SetEvent(event) != FALSE;
    CloseHandle(event);
    LauncherLog("manual VR recovery for pid %lu: %s", pid, signaled ? "requested" : "signal failed");
    if (!signaled)
    {
        ErrorBox(L"Could not request VR recovery. Use the F1 Status button in the game.");
        return 1;
    }
    return 0;
}

// Poll for a process by name. Used only on the Store path, where the game is
// started by Windows rather than by us, so we cannot create it suspended and
// must instead claim it as soon as it exists. The poll is deliberately tight:
// the DLL has to be in before the game creates its D3D11 device, which is
// several seconds of module loading away.
static DWORD WaitForProcess(const wchar_t* exeName, DWORD timeoutMs)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        const DWORD pid = FindProcessId(exeName);
        if (pid)
            return pid;
        if (GetTickCount64() >= deadline)
            return 0;
        Sleep(5);
    }
}

// A Store game the launcher did not start itself may have been started WITH anti-cheat: the Xbox app's default
// "Halo: The Master Chief Collection" entry runs mcclauncher.exe and EasyAntiCheat, and its game process has
// the same name as the anti-cheat-disabled entry's. This mod never touches a protected session. The name of
// the first anti-cheat process found, or null. (A process list, read only; nothing is opened.)
static const wchar_t* AntiCheatProcessRunning()
{
    static const wchar_t* const kNames[]{L"mcclauncher.exe", L"EasyAntiCheat_EOS.exe", L"EasyAntiCheat.exe",
                                         L"start_protected_game.exe"};
    for (const wchar_t* name : kNames)
        if (FindProcessId(name))
            return name;
    return nullptr;
}

static bool ModuleLoadedIn(DWORD pid, const wchar_t* moduleName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W me{sizeof(me)};
    bool found = false;
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, moduleName) == 0)
            {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Write the DLL path into the game process and make it call LoadLibraryW on it.
// LoadLibraryW lives at the same address in every 64-bit process.
static bool InjectDll(HANDLE process, const std::wstring& dllPath, std::wstring& failWhy)
{
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(process, nullptr, bytes,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem)
    {
        failWhy = L"VirtualAllocEx: " + WinErr(GetLastError());
        return false;
    }
    bool injected = false;
    if (!WriteProcessMemory(process, remoteMem, dllPath.c_str(), bytes, nullptr))
        failWhy = L"WriteProcessMemory: " + WinErr(GetLastError());
    else
    {
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary,
                                           remoteMem, 0, nullptr);
        if (!thread)
            failWhy = L"CreateRemoteThread: " + WinErr(GetLastError());
        else
        {
            if (WaitForSingleObject(thread, 15000) == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeThread(thread, &exitCode);
                if (exitCode != 0) // LoadLibrary returns the module handle; 0 means it failed
                    injected = true;
                else
                    failWhy = L"LoadLibraryW returned NULL inside the game "
                              L"(missing dependency or blocked DLL?)";
            }
            else
                failWhy = L"the injection thread timed out";
            CloseHandle(thread);
        }
    }
    VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
    return injected;
}

// A packaged app cannot be started with CreateProcess: the process would have
// no package identity and therefore no licence. Measured on this build,
// 2026-07-28: running MCCWinStore-Win64-Shipping.exe directly exits with code 0
// after about 210 ms, with and without the mod and with and without the render
// arguments, writing no log of its own. The Windows SDK has no CreateProcess
// attribute that grants package identity - there is no
// PROC_THREAD_ATTRIBUTE_PACKAGE_* of any kind - so packaged activation is the
// only supported route.
//
// The application id used is the package's own "Anti-Cheat Disabled (Mods and
// Limited Services)" entry, declared in its MicrosoftGame.config. Activating it
// is the Store equivalent of running MCC-Win64-Shipping.exe directly on Steam,
// so EasyAntiCheat is still never started.
static const wchar_t* kStoreAumid =
    L"Microsoft.Chelan_8wekyb3d8bbwe!HaloMCCShippingNoEAC";

static HRESULT ActivateStoreApp(const wchar_t* aumid, const wchar_t* arguments,
                                DWORD* pidOut)
{
    *pidOut = 0;
    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                  CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(hr))
        return hr;
    hr = manager->ActivateApplication(aumid, arguments, AO_NOERRORUI, pidOut);
    manager->Release();
    return hr;
}

static float ReadResolutionScale(const std::wstring& path)
{
    float scale = 1.0f;
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rt");
    if (f)
    {
        char line[512];
        while (fgets(line, sizeof(line), f))
        {
            float parsed = 1.0f;
            if (sscanf_s(line, " resolution_scale = %f", &parsed) == 1 &&
                std::isfinite(parsed))
                scale = parsed;
        }
        fclose(f);
    }
    // Any value in range is honored exactly; the named tiers are only F1
    // shortcuts. (Before 2026-07-20 this snapped to those six tiers,
    // so a hand-typed 0.90 silently became 0.80.)
    return std::clamp(scale, kResolutionScaleMin, kResolutionScaleMax);
}

static int ScaleEven(int base, float scale)
{
    int value = static_cast<int>(std::lround(static_cast<float>(base) * scale));
    if (value & 1)
        ++value;
    return value;
}

// Whether this process runs with an administrator's elevated token.
static bool ProcessIsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool read = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != 0;
    CloseHandle(token);
    return read && elevation.TokenIsElevated != 0;
}

static int LaunchInstalledMod(const std::wstring& dir)
{
    g_logPath = dir + L"\\HaloMCCVRLauncher.log";
    // fresh log each launch
    {
        FILE* f = nullptr;
        _wfopen_s(&f, g_logPath.c_str(), L"wt");
        if (f) fclose(f);
    }
    LauncherLog("launcher started; self dir = %ls", dir.c_str());
    // The game is started with this process's token. In an elevated game the OpenXR loader ignores
    // XR_RUNTIME_JSON, so a headset runtime chosen with that variable is replaced by the machine's registered one.
    if (ProcessIsElevated())
        LauncherLog("NOTE: the launcher is running as administrator, so the game will too. If VR does not start, "
                    "open the launcher normally: an elevated game ignores an OpenXR runtime chosen with "
                    "XR_RUNTIME_JSON");

    const std::wstring dllPath = dir + (g_flatProbe ? L"\\HaloMCCFlatProbe.dll" : L"\\HaloMCCVR.dll");
    if (g_flatProbe)
    {
        const size_t leaf = dir.find_last_of(L"\\/");
        const std::wstring folder = leaf == std::wstring::npos ? dir : dir.substr(leaf + 1);
        LauncherLog("FLAT PROBE launch: only %ls is loaded; MCC starts flat (no VR render surface)", dllPath.c_str());
        if (!_wcsicmp(folder.c_str(), L"Halo_MCC_VR"))
        {
            ErrorBox(L"The flat probe is never run from Halo_MCC_VR (the live VR install).\n\n"
                     L"Stage it in its own folder with tools\\Install-FlatProbe.ps1.");
            return 1;
        }
        if (!FileExists(dllPath))
        {
            ErrorBox(L"HaloMCCFlatProbe.dll is missing from:\n" + dir);
            return 1;
        }
    }
    else if (!FileExists(dllPath))
    {
        ErrorBox(L"HaloMCCVR.dll is missing from:\n" + dir +
                 L"\n\nCopy HaloMCCVR.dll from the package into this same folder,\n"
                 L"next to HaloMCCVRLauncher.exe.");
        return 1;
    }

    // The launcher lives in <game>\Halo_MCC_VR\, so the game exe is found by
    // walking up from here. A few levels are tried so a dev copy placed
    // elsewhere inside the game folder also works. Both editions' executable
    // names are tried at every level, so one launcher serves either install.
    std::wstring gameExe, gameDir, gameRoot;
    std::wstring probe = dir;
    bool foundStoreExeName = false;
    for (int i = 0; i < 6 && !probe.empty(); i++)
    {
        const std::wstring binDir = probe + L"\\MCC\\Binaries\\Win64";
        // The Store name is tried first: a genuine Steam install never ships
        // it, so finding it is unambiguous.
        const std::wstring storeCand = binDir + L"\\" + kStoreExeName;
        const std::wstring steamCand = binDir + L"\\" + kSteamExeName;
        if (FileExists(storeCand))
        {
            gameExe = storeCand;
            foundStoreExeName = true;
        }
        else if (FileExists(steamCand))
        {
            gameExe = steamCand;
        }
        if (!gameExe.empty())
        {
            gameDir = binDir;
            gameRoot = probe;
            break;
        }
        const size_t slash = probe.find_last_of(L'\\');
        if (slash == std::wstring::npos)
            break;
        probe.resize(slash);
    }
    if (gameExe.empty())
    {
        ErrorBox(L"Could not find the Halo: MCC program near:\n" + dir +
                 L"\n\nLooked for both editions' executables:\n"
                 L"  MCC\\Binaries\\Win64\\" + kSteamExeName + L"   (Steam)\n"
                 L"  MCC\\Binaries\\Win64\\" + kStoreExeName + L"   (Microsoft Store)\n\n"
                 L"The Halo_MCC_VR folder must sit inside the game's install\n"
                 L"folder - the one that contains the MCC folder. For the\n"
                 L"Microsoft Store / Xbox app edition that is the Content folder.");
        return 1;
    }

    // MicrosoftGame.config sits beside the MCC folder in the Store package and
    // in no Steam install, so it identifies the edition even when the
    // executable was renamed to the Steam name by hand.
    const bool storeLayout = FileExists(gameRoot + L"\\MicrosoftGame.config");
    const Edition edition =
        (foundStoreExeName || storeLayout) ? Edition::Store : Edition::Steam;
    LauncherLog("game exe = %ls", gameExe.c_str());
    LauncherLog("detected edition: %ls (store exe name=%d, MicrosoftGame.config=%d)",
                EditionName(edition), foundStoreExeName ? 1 : 0, storeLayout ? 1 : 0);

    // Steam only owns the Steam edition's licensing. The Store edition is
    // licensed by the Xbox app, so demanding Steam there would reject a
    // perfectly valid install.
    if (edition == Edition::Steam && !ProcessRunning(L"steam.exe"))
    {
        ErrorBox(L"Steam is not running.\n\nStart Steam first, then run this launcher again.\n"
                 L"(This Steam copy of the game needs Steam to verify ownership.)");
        return kExitSteamNotRunning;
    }
    // The running mod handles recovery on its title worker. Never inject a
    // second copy or start a competing MCC process for this action.
    const DWORD runningSteam = FindProcessId(kSteamExeName);
    const DWORD runningStore = FindProcessId(kStoreExeName);
    if (runningSteam && runningStore)
    {
        ErrorBox(L"Both MCC editions are running. Close one before requesting VR recovery.");
        return 1;
    }
    if ((runningSteam || runningStore) && g_flatProbe)
    {
        ErrorBox(L"Halo: MCC is already running. Close it, then start the flat probe again.");
        return 1;
    }
    if (runningSteam || runningStore)
        return OfferManualVrRecovery(runningSteam ? runningSteam : runningStore, gameExe);

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    // M2 stereo: render the game into a wide surface matching Halo's VR
    // projection rather than the normal 16:9 desktop frame. Windowed mode
    // avoids asking the monitor for a non-standard display mode.
    // PSVR2's symmetric coverage has a tangent-space aspect near 1.386:1.
    // 2912x2100 keeps approximately the same pixel cost as 2448x2496 while
    // allowing Halo to generate the correct wide raster/culling frustum.
    // resolution_scale reduces both dimensions together, preserving this
    // aspect and Halo's culling/projection. The DLL then upscales the complete
    // eye into the unchanged full-size OpenXR projection. This can help
    // GPU-limited systems; it cannot remove CPU/per-render engine cost.
    // (kNativeRenderWidth/Height live in ../common/config.h so the F1 menu can
    // show the same pixel count this line will produce.)
    const std::wstring primaryConfig = dir + L"/halomccvr.cfg";
    const std::wstring legacyConfig = dir + L"/halo3xr.cfg";
    const DWORD primaryAttributes = GetFileAttributesW(primaryConfig.c_str());
    const bool primaryExists = primaryAttributes != INVALID_FILE_ATTRIBUTES &&
        (primaryAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    const float resolutionScale =
        ReadResolutionScale(primaryExists ? primaryConfig : legacyConfig);
    const int renderWidth = ScaleEven(kNativeRenderWidth, resolutionScale);
    const int renderHeight = ScaleEven(kNativeRenderHeight, resolutionScale);
    // Hand the DLL the EXACT render surface size we are launching with, so that
    // when fit_desktop_window is on it can force MCC's swapchain backbuffer to
    // this exact size (matching this ScaleEven, so the two can never disagree)
    // and preserve it through window resize. The child inherits these because
    // CreateProcessW is given a NULL environment block. Harmless when the fit is
    // off -- the DLL only reads them in that case.
    wchar_t envBuf[16];
    wchar_t renderArgs[96]{};
    if (!g_flatProbe)
    {
        swprintf_s(envBuf, L"%d", renderWidth);
        SetEnvironmentVariableW(L"HALO3XR_RENDER_W", envBuf);
        swprintf_s(envBuf, L"%d", renderHeight);
        SetEnvironmentVariableW(L"HALO3XR_RENDER_H", envBuf);
        swprintf_s(renderArgs, L"-WINDOWED -ResX=%d -ResY=%d",
                   renderWidth, renderHeight);
    }
    else
    {
        // Flat: the player's own display settings, untouched.
        SetEnvironmentVariableW(L"HALO3XR_RENDER_W", nullptr);
        SetEnvironmentVariableW(L"HALO3XR_RENDER_H", nullptr);
    }
    const wchar_t quote = 34;
    std::wstring cmdline(1, quote);
    cmdline += gameExe;
    cmdline += quote;
    cmdline += L' ';
    cmdline += renderArgs;
    if (g_flatProbe)
        LauncherLog("flat launch: no render-surface arguments (the game's own display settings)");
    else
        LauncherLog("VR render surface: %dx%d (resolution_scale %.2f; native %dx%d)",
                    renderWidth, renderHeight, resolutionScale,
                    kNativeRenderWidth, kNativeRenderHeight);

    // An unattended test may start this launcher through __COMPAT_LAYER=RunAsInvoker, which keeps
    // a UAC prompt off the desktop whatever the launcher's manifest asks for. That override is for
    // the launcher alone; the game inherits this environment, so it starts without it.
    if (g_headless)
    {
        wchar_t layer[64]{};
        const DWORD n = GetEnvironmentVariableW(L"__COMPAT_LAYER", layer, static_cast<DWORD>(std::size(layer)));
        if (n && n < std::size(layer) && !_wcsicmp(layer, L"RunAsInvoker"))
        {
            SetEnvironmentVariableW(L"__COMPAT_LAYER", nullptr);
            LauncherLog("unattended launch: dropped __COMPAT_LAYER=RunAsInvoker before starting the game");
        }
    }

    bool injected = false;
    std::wstring failWhy;

    if (edition == Edition::Steam)
    {
        // Make Steamworks believe the game was launched correctly, so it does
        // not ask Steam to relaunch it (which would drop the process we inject
        // into). The child process inherits these environment variables.
        SetEnvironmentVariableW(L"SteamAppId", kSteamAppId);
        SetEnvironmentVariableW(L"SteamGameId", kSteamAppId);
        SetEnvironmentVariableW(L"SteamOverlayGameId", kSteamAppId);
        LauncherLog("set SteamAppId=%ls; creating game process (suspended) with"
                    " -WINDOWED -ResX=%d -ResY=%d",
                    kSteamAppId, renderWidth, renderHeight);

        if (!CreateProcessW(gameExe.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, gameDir.c_str(), &si, &pi))
        {
            const DWORD e = GetLastError();
            LauncherLog("CreateProcessW failed: %lu", e);
            ErrorBox(L"Could not start the game:\n" + WinErr(e));
            return 1;
        }
        LauncherLog("game process created, pid %lu", pi.dwProcessId);

        injected = InjectDll(pi.hProcess, dllPath, failWhy);
        if (!injected)
        {
            LauncherLog("injection FAILED: %ls", failWhy.c_str());
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            ErrorBox(L"Could not load the VR mod into the game, so the game was closed.\n\n"
                     L"Reason: " + failWhy);
            return 1;
        }
        LauncherLog("DLL injected OK; resuming game");
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
    }
    else
    {
        // Store edition: ask Windows to activate the package, then claim the
        // game process the moment it is ready. We cannot create it suspended, so
        // the poll is tight; MCC spends seconds loading modules before it
        // creates its D3D11 device, which is the deadline that actually matters.
        //
        // Measured 2026-07-28: activation returns the pid of GameLaunchHelper,
        // the game process itself appears about 4 s later, and the activation
        // arguments ARE forwarded to it verbatim - so the Store edition gets the
        // same VR render surface as Steam. Windows starts the game, so it does
        // not inherit our environment; that costs nothing, because the DLL
        // derives the identical render size from resolution_scale in
        // halomccvr.cfg when HALO3XR_RENDER_W/H are absent.
        LauncherLog("Store edition: activating %ls with %ls",
                    kStoreAumid, renderArgs);
        HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool coOwned = SUCCEEDED(coHr);
        DWORD activatedPid = 0;
        const HRESULT hr = ActivateStoreApp(kStoreAumid, renderArgs, &activatedPid);
        if (SUCCEEDED(hr))
        {
            LauncherLog("activation OK (helper pid %lu); waiting for %ls",
                        activatedPid, kStoreExeName);
        }
        else if (g_headless)
        {
            // The fallback below asks a person to start the game from the Xbox
            // app; an unattended launch has nobody to ask, so it fails here.
            LauncherLog("activation FAILED: hr=0x%08X", static_cast<unsigned>(hr));
            if (coOwned)
                CoUninitialize();
            ErrorBox(L"Windows would not start the Microsoft Store copy of the game. "
                     L"An unattended launch does not wait for a manual start from the Xbox app.");
            return kExitFailed;
        }
        else
        {
            // Loud, never silent: say exactly what failed and hand the user a
            // route that still works, rather than pretending the launch is fine.
            LauncherLog("activation FAILED: hr=0x%08X; falling back to attaching "
                        "to a manually started game", static_cast<unsigned>(hr));
            InfoBox(L"Windows would not start the Microsoft Store copy of the game "
                    L"for this launcher.\n\n"
                    L"Start Halo: MCC yourself from the Xbox app now - use the\n"
                    L"\"Halo: MCC Anti-Cheat Disabled (Mods and Limited Services)\"\n"
                    L"entry - and leave this window alone. The mod will attach to the\n"
                    L"game automatically as soon as it appears.\n\n"
                    L"Details are in HaloMCCVRLauncher.log.");
        }

        // Generous: a first Store launch can sit on a licence or EAC-choice
        // prompt, and the fallback above needs time for the user to click Play.
        const DWORD waitMs = SUCCEEDED(hr) ? 120000 : 300000;
        const DWORD gamePid = WaitForProcess(kStoreExeName, waitMs);
        if (!gamePid)
        {
            if (coOwned)
                CoUninitialize();
            LauncherLog("the game process never appeared within %lu ms", waitMs);
            ErrorBox(L"The game never started, so the VR mod was not loaded.\n\n"
                     L"Start Halo: MCC once from the Xbox app to check it runs on its\n"
                     L"own, then try this launcher again.\n\n"
                     L"Details are in HaloMCCVRLauncher.log.");
            return 1;
        }
        LauncherLog("game process appeared, pid %lu", gamePid);
        if (const wchar_t* const antiCheat = AntiCheatProcessRunning())
        {
            // Never into a protected session: say so and leave the game alone.
            if (coOwned)
                CoUninitialize();
            LauncherLog("REFUSED: %ls is running, so this game was started WITH anti-cheat; the mod is not loaded "
                        "into it", antiCheat);
            ErrorBox(L"This Halo: MCC session was started with anti-cheat, so the VR mod was not loaded.\n\n"
                     L"Close the game, then start it with this launcher, or from the Xbox app with the\n"
                     L"\"Halo: MCC Anti-Cheat Disabled (Mods and Limited Services)\" entry.");
            return 1;
        }

        // The process exists but its loader may not have populated the module
        // list yet. Injecting into that window is what makes CreateRemoteThread
        // injection flaky, so wait for proof the loader is up before touching
        // it. kernel32 is present as soon as it is.
        const ULONGLONG readyDeadline = GetTickCount64() + 15000;
        while (!ModuleLoadedIn(gamePid, L"kernel32.dll") &&
               GetTickCount64() < readyDeadline)
            Sleep(5);
        LauncherLog("game loader ready; injecting");

        if (ModuleLoadedIn(gamePid, L"HaloMCCVR.dll") ||
            ModuleLoadedIn(gamePid, L"halo3xr.dll") ||
            (g_flatProbe && ModuleLoadedIn(gamePid, L"HaloMCCFlatProbe.dll")))
        {
            if (coOwned)
                CoUninitialize();
            LauncherLog("an MCCVR DLL is already loaded in pid %lu; refusing to inject twice",
                        gamePid);
            ErrorBox(L"The VR mod is already loaded into the running game.\n\n"
                     L"Close Halo: MCC completely, then run this launcher again.");
            return 1;
        }

        pi.hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                      PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                      PROCESS_VM_READ | SYNCHRONIZE,
                                  FALSE, gamePid);
        if (!pi.hProcess)
        {
            const DWORD e = GetLastError();
            if (coOwned)
                CoUninitialize();
            LauncherLog("OpenProcess(%lu) failed: %lu", gamePid, e);
            ErrorBox(L"Could not attach to the running game:\n" + WinErr(e));
            return 1;
        }
        pi.dwProcessId = gamePid;

        injected = InjectDll(pi.hProcess, dllPath, failWhy);
        if (coOwned)
            CoUninitialize();
        if (!injected)
        {
            // The game is the user's own licensed session here - we did not
            // create it, so we do not kill it.
            LauncherLog("injection FAILED: %ls", failWhy.c_str());
            CloseHandle(pi.hProcess);
            ErrorBox(L"The game started, but the VR mod could not be loaded into it.\n\n"
                     L"The game was left running.\n\nReason: " + failWhy);
            return 1;
        }
        LauncherLog("DLL injected OK into the running game");
    }

    // Watch the game for a short while. If it exits almost immediately, it
    // bounced through Steam, failed a licence check, or crashed on startup —
    // capture the exit code so we can see what happened instead of the process
    // silently vanishing.
    const DWORD watchMs = 12000;
    const DWORD waitRes = WaitForSingleObject(pi.hProcess, watchMs);
    if (waitRes == WAIT_OBJECT_0)
    {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        LauncherLog("game process EXITED within %lus, exit code %lu (0x%08X)", watchMs / 1000, code, code);
        CloseHandle(pi.hProcess);
        const std::wstring why =
            edition == Edition::Store
                ? L"The game was started through the Xbox app but quit on its own. Start\n"
                  L"Halo: MCC once from the Xbox app without this launcher to check it\n"
                  L"runs, and that you are signed in, then try again."
                : L"This usually means it relaunched through Steam or the anti-cheat-off\n"
                  L"mode refused this launch.";
        ErrorBox(L"The game closed itself right after starting (exit code " +
                 std::to_wstring(code) + L").\n\n" + why +
                 L"\n\nDetails are in HaloMCCVRLauncher.log.");
        return 1;
    }
    LauncherLog("game still running after %lus - launch looks good", watchMs / 1000);
    CloseHandle(pi.hProcess);
    return kExitOk;
}

// ---------------------------------------------------------------------------------------------------------
// PROJECT RECLAIMER (experimental). Project Reclaimer is a standalone client that runs MCC's own
// halo3.dll without MCC. A folder holding this launcher, HaloMCCVR.dll AND HaloMCCVRReclaimerRelay.dll, with a
// Project Reclaimer client beside it and no MCC install around it, is a Project Reclaimer deployment: the
// launcher starts that client and the relay carries the mod to its engine worker
// (src/common/reclaimer_host_logic.h, src/relay/reclaimer_relay.cpp). The relay DLL is the switch: it is not
// in a player package, so no MCC player's launcher ever takes this route.
// ---------------------------------------------------------------------------------------------------------
static const wchar_t* kReclaimerRelayName = L"HaloMCCVRReclaimerRelay.dll";
#ifndef HALOMCCVR_RECLAIMER_LAUNCHER
#define HALOMCCVR_RECLAIMER_LAUNCHER 0
#endif

static bool MccInstallNear(std::wstring probe)
{
    for (int i = 0; i < 6 && !probe.empty(); i++)
    {
        const std::wstring binDir = probe + L"\\MCC\\Binaries\\Win64";
        if (FileExists(binDir + L"\\" + kStoreExeName) || FileExists(binDir + L"\\" + kSteamExeName))
            return true;
        const size_t slash = probe.find_last_of(L'\\');
        if (slash == std::wstring::npos)
            break;
        probe.resize(slash);
    }
    return false;
}

// The Project Reclaimer client this folder is deployed for; `exe` is empty when this is not such a folder.
// Looked for in: the path named in reclaimer_client.txt (one line: an install somewhere else, or the player's
// choice among several), this folder, its parent, and the parent's other folders
// (..\0.9.8\project-reclaimer-v0.9.8.exe). Of several, reclaimer_host::ChooseClient decides: a version the mod
// has been run with before any other, then the highest version, then the newest file.
struct ReclaimerClient
{
    std::wstring exe;
    reclaimer_host::Version version;
    bool versionFromName = false;   // the file carries no version of its own: taken from its name
    bool named = false;          // from reclaimer_client.txt
    std::wstring passedOver;     // a newer client found beside the chosen one and not started
};
static std::string ReclaimerVersionText(const reclaimer_host::Version& version)
{
    if (!version.known)
        return "no version in the file or its name";
    char text[48]{};
    sprintf_s(text, "%d.%d.%d", version.part[0], version.part[1], version.part[2]);
    return text;
}
// A client's version. The client updates itself in place and keeps its file name, so the name says which version
// was first installed, not which one runs: the file's own version resource is read, and the name is the fallback
// for a file without one. `fromName` (optional) says which it was.
static reclaimer_host::Version ReclaimerVersionOfFile(const std::wstring& path, bool* fromName = nullptr)
{
    reclaimer_host::Version version;
    if (fromName)
        *fromName = false;
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size)
    {
        std::vector<BYTE> block(size);
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT fixedSize = 0;
        if (GetFileVersionInfoW(path.c_str(), 0, size, block.data()) &&
            VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) && fixed &&
            fixedSize >= sizeof(VS_FIXEDFILEINFO) && fixed->dwSignature == VS_FFI_SIGNATURE &&
            (fixed->dwFileVersionMS || fixed->dwFileVersionLS))
        {
            version.part[0] = HIWORD(fixed->dwFileVersionMS);
            version.part[1] = LOWORD(fixed->dwFileVersionMS);
            version.part[2] = HIWORD(fixed->dwFileVersionLS);
            version.known = true;
            return version;
        }
    }
    version = reclaimer_host::VersionOf(reclaimer_host::Leaf(path));
    if (fromName)
        *fromName = version.known;
    return version;
}
static std::string ReclaimerProvenText()
{
    std::string text;
    for (const reclaimer_host::Version& version : reclaimer_host::kProvenVersions)
        text += (text.empty() ? "" : ", ") + ReclaimerVersionText(version);
    return text;
}
// The one line of reclaimer_client.txt in `dir`, tidied. Empty when there is no such file or line.
static std::wstring ReclaimerNamedPath(const std::filesystem::path& dir)
{
    FILE* f = nullptr;
    _wfopen_s(&f, (dir / L"reclaimer_client.txt").c_str(), L"rt, ccs=UTF-8");
    if (!f)
        return {};
    wchar_t line[2048]{};
    const bool read = fgetws(line, static_cast<int>(std::size(line)), f) != nullptr;
    fclose(f);
    return read ? reclaimer_host::TidyPath(line) : std::wstring();
}
static bool ReclaimerIsClientFile(const std::wstring& path)
{
    return !path.empty() && FileExists(path) && reclaimer_host::IsClientExeName(reclaimer_host::Leaf(path));
}
static ReclaimerClient ReclaimerScanAround(const std::filesystem::path& dir);

static ReclaimerClient FindReclaimerClient(const std::filesystem::path& dir)
{
    ReclaimerClient client;
    if (!FileExists((dir / kReclaimerRelayName).wstring()) || MccInstallNear(dir.wstring()))
        return client;
    const std::wstring named = ReclaimerNamedPath(dir);
    if (ReclaimerIsClientFile(named))
    {
        client.exe = named;
        client.version = ReclaimerVersionOfFile(named, &client.versionFromName);
        client.named = true;
        return client;
    }
    return ReclaimerScanAround(dir);
}

// The client among the game programs in `dir`, its parent and the parent's other folders.
static ReclaimerClient ReclaimerScanAround(const std::filesystem::path& dir)
{
    namespace fs = std::filesystem;
    ReclaimerClient client;
    std::vector<reclaimer_host::ClientCandidate> candidates;
    const auto consider = [&](const fs::path& folder)
    {
        std::error_code ec;
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            std::error_code entryError;
            if (!it->is_regular_file(entryError) || entryError)
                continue;
            const std::wstring leaf = it->path().filename().wstring();
            if (!reclaimer_host::IsClientExeName(leaf))
                continue;
            const fs::file_time_type time = it->last_write_time(entryError);
            candidates.push_back({it->path().wstring(), ReclaimerVersionOfFile(it->path().wstring()),
                                  static_cast<int64_t>(time.time_since_epoch().count())});
        }
    };
    consider(dir);
    const fs::path parent = dir.parent_path();
    if (!parent.empty() && parent != dir)
    {
        consider(parent);
        std::error_code ec;
        for (fs::directory_iterator it(parent, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            std::error_code entryError;
            if (it->is_directory(entryError) && !entryError && it->path() != dir)
                consider(it->path());
        }
    }
    const reclaimer_host::ClientChoice choice = reclaimer_host::ChooseClient(candidates);
    if (choice.chosen >= 0)
    {
        client.exe = candidates[static_cast<size_t>(choice.chosen)].path;
        client.version = ReclaimerVersionOfFile(client.exe, &client.versionFromName);
        if (choice.passedOver >= 0)
            client.passedOver = candidates[static_cast<size_t>(choice.passedOver)].path;
    }
    return client;
}

// The pid of a running Project Reclaimer client process; with `withModule`, only one that has that module.
static DWORD FindReclaimerProcess(const wchar_t* withModule)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe{sizeof(pe)};
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (reclaimer_host::IsClientExeName(pe.szExeFile) &&
                (!withModule || ModuleLoadedIn(pe.th32ProcessID, withModule)))
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// ---- the menu's Project Reclaimer section (reclaimer_menu.h) ----------------------------------------------------
static std::wstring WideOf(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}
// The program the `reclaimer:` link is registered to open, for this user or the machine.
static std::wstring ReclaimerLinkProgram()
{
    for (const HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    {
        wchar_t command[2048]{};
        DWORD bytes = sizeof(command) - sizeof(wchar_t);
        if (RegGetValueW(hive, L"Software\\Classes\\reclaimer\\shell\\open\\command", nullptr, RRF_RT_REG_SZ,
                         nullptr, command, &bytes) != ERROR_SUCCESS)
            continue;
        const std::wstring program = reclaimer_host::ClientOfOpenCommand(command);
        if (ReclaimerIsClientFile(program))
            return program;
    }
    return {};
}
static std::wstring ReclaimerRunningProgram()
{
    const DWORD pid = FindReclaimerProcess(nullptr);
    if (!pid)
        return {};
    std::wstring program;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t path[2048]{};
        DWORD size = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(process, 0, path, &size))
            program.assign(path, size);
        CloseHandle(process);
    }
    return ReclaimerIsClientFile(program) ? program : std::wstring();
}

namespace reclaimer_launch
{
std::filesystem::path ModDir(const std::filesystem::path& launcherDir)
{
    if (!FileExists((launcherDir / L"HaloMCCVR.dll").wstring()) &&
        FileExists((launcherDir / L"ModFiles" / L"HaloMCCVR.dll").wstring()))
        return launcherDir / L"ModFiles";
    return launcherDir;
}

std::wstring SavedClient(const std::filesystem::path& launcherDir)
{
    return ReclaimerNamedPath(launcherDir);
}

bool SaveClient(const std::filesystem::path& launcherDir, const std::wstring& exe)
{
    const std::filesystem::path file = launcherDir / L"reclaimer_client.txt";
    if (exe.empty())
    {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return !ec;
    }
    if (!ReclaimerIsClientFile(exe))
        return true;
    if (ReclaimerNamedPath(launcherDir) == exe)
        return true;
    FILE* f = nullptr;
    _wfopen_s(&f, file.c_str(), L"wt, ccs=UTF-8");
    if (!f)
        return false;
    const bool written = fputws(exe.c_str(), f) >= 0 && fputws(L"\n", f) >= 0;
    return fclose(f) == 0 && written;
}

std::wstring DetectClient(const std::filesystem::path& launcherDir, std::wstring& how)
{
    std::wstring program = ReclaimerLinkProgram();
    if (!program.empty())
    {
        how = L"Found the Project Reclaimer that Windows has on record for this PC.";
        return program;
    }
    program = ReclaimerRunningProgram();
    if (!program.empty())
    {
        how = L"Found the Project Reclaimer that is running now.";
        return program;
    }
    program = ReclaimerScanAround(launcherDir).exe;
    if (!program.empty())
        how = L"Found Project Reclaimer in a folder beside this launcher.";
    return program;
}

ClientInfo Describe(const std::filesystem::path& launcherDir, const std::wstring& exe)
{
    ClientInfo info;
    if (exe.empty())
    {
        info.text = L"Not set. Choose Detect, or Browse and pick the game program (project-reclaimer-client-v*.exe).";
        return info;
    }
    if (!FileExists(exe))
    {
        info.text = L"There is no file at this path.";
        return info;
    }
    if (!reclaimer_host::IsClientExeName(reclaimer_host::Leaf(exe)))
    {
        info.text = L"This is not the Project Reclaimer game program. Its name is "
                    L"project-reclaimer-client-v<version>.exe (project-reclaimer-v<version>.exe before 0.9.23; not "
                    L"the dedicated server or the remote console).";
        return info;
    }
    const reclaimer_host::Version version = ReclaimerVersionOfFile(exe);
    info.text = L"Project Reclaimer " + WideOf(ReclaimerVersionText(version)) +
        (reclaimer_host::Proven(version)
             ? L": the VR mod has been run with this version."
             : L": the VR mod has NOT been run with this version (it has with " + WideOf(ReclaimerProvenText()) +
                   L").");
    const std::filesystem::path modDir = ModDir(launcherDir);
    for (const wchar_t* needed : {L"HaloMCCVR.dll", kReclaimerRelayName})
    {
        if (FileExists((modDir / needed).wstring()))
            continue;
        info.text += std::wstring(L"\n") + needed + L" is not in " + modDir.wstring() +
            L", so this launcher cannot start it.";
        return info;
    }
    info.text += L"\nVR files and settings: " + modDir.wstring();
    info.startable = true;
    return info;
}
} // namespace reclaimer_launch

// Prepare the client's settings.json, under Documents\My Games\Project Reclaimer, before it starts and reads it:
// Centered crosshair on (reclaimer_host::WithCenteredCrosshair) and exclusive fullscreen made borderless
// (reclaimer_host::WithoutExclusiveFullscreen). Written through a temporary file, so the player's settings are
// never left half written.
static void ReclaimerPrepareSettings()
{
    PWSTR documents = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) || !documents)
    {
        LauncherLog("Project Reclaimer's settings: the Documents folder is unknown; left as they are");
        return;
    }
    const std::filesystem::path folder = std::filesystem::path(documents) / L"My Games" / L"Project Reclaimer";
    CoTaskMemFree(documents);
    const std::filesystem::path file = folder / L"settings.json";
    std::string text;
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, file.c_str(), L"rb") == 0 && f)
        {
            char buffer[4096];
            size_t n = 0;
            while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
                text.append(buffer, n);
            fclose(f);
        }
    }
    bool changed = false;
    std::string crosshairOut;
    const reclaimer_host::CenteredCrosshairEdit crosshair = reclaimer_host::WithCenteredCrosshair(text, crosshairOut);
    if (crosshair == reclaimer_host::CenteredCrosshairEdit::AlreadyOn)
        LauncherLog("Project Reclaimer's centered crosshair: already on");
    else if (crosshair == reclaimer_host::CenteredCrosshairEdit::Unreadable)
        LauncherLog("Project Reclaimer's centered crosshair: %ls could not be read as its settings; left as it is. "
                    "Turn on Settings > HUD > Centered crosshair in the game, or the hand will not take over its aim",
                    file.c_str());
    else
    {
        text = crosshairOut;
        changed = true;
    }
    std::string displayOut;
    const reclaimer_host::DisplayModeEdit display = reclaimer_host::WithoutExclusiveFullscreen(text, displayOut);
    if (display == reclaimer_host::DisplayModeEdit::MadeBorderless)
    {
        text = displayOut;
        changed = true;
    }
    else if (display == reclaimer_host::DisplayModeEdit::Unreadable)
        LauncherLog("Project Reclaimer's display mode: could not be read in %ls; left as it is. If the game starts "
                    "in exclusive fullscreen, choose Borderless or Windowed in its display settings",
                    file.c_str());
    if (!changed)
        return;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path temp = folder / L"settings.json.vr-launcher-tmp";
    FILE* f = nullptr;
    bool written = false;
    if (_wfopen_s(&f, temp.c_str(), L"wb") == 0 && f)
    {
        written = fwrite(text.data(), 1, text.size(), f) == text.size();
        written = (fclose(f) == 0) && written;
    }
    if (!written || !MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
        LauncherLog("Project Reclaimer's settings: could not write %ls; left as they are", file.c_str());
        return;
    }
    if (crosshair == reclaimer_host::CenteredCrosshairEdit::TurnedOn ||
        crosshair == reclaimer_host::CenteredCrosshairEdit::Created)
        LauncherLog("Project Reclaimer's centered crosshair: turned on in %ls (%s)", file.c_str(),
                    crosshair == reclaimer_host::CenteredCrosshairEdit::Created ? "a new settings file"
                                                                                : "nothing else in it changed");
    if (display == reclaimer_host::DisplayModeEdit::MadeBorderless)
        LauncherLog("Project Reclaimer's display mode: exclusive fullscreen changed to borderless in %ls. In "
                    "exclusive fullscreen the game slows to about 10 fps beside the headset and the graphics card is "
                    "reset; the desktop window is only a mirror of the headset's picture", file.c_str());
}

static int LaunchReclaimer(const std::wstring& dir, const ReclaimerClient& client)
{
    const std::wstring& clientExe = client.exe;
    g_logPath = dir + L"\\HaloMCCVRLauncher.log";
    {
        FILE* f = nullptr;
        _wfopen_s(&f, g_logPath.c_str(), L"wt");
        if (f) fclose(f);
    }
    LauncherLog("launcher started; self dir = %ls", dir.c_str());
    LauncherLog("PROJECT RECLAIMER deployment (experimental): client = %ls (%s%s%s)", clientExe.c_str(),
                ReclaimerVersionText(client.version).c_str(),
                client.versionFromName ? ", from its file name: the file carries no version of its own" : "",
                client.named ? ", named in reclaimer_client.txt" : "");
    {
        // The client updates itself in place: its name then still says the version first installed.
        const reclaimer_host::Version inName = reclaimer_host::VersionOf(reclaimer_host::Leaf(clientExe));
        if (client.version.known && !client.versionFromName && inName.known &&
            (inName.part[0] != client.version.part[0] || inName.part[1] != client.version.part[1] ||
             inName.part[2] != client.version.part[2]))
            LauncherLog("the client's file is named for %s and is %s: it has updated itself in place",
                        ReclaimerVersionText(inName).c_str(), ReclaimerVersionText(client.version).c_str());
    }
    if (!reclaimer_host::Proven(client.version))
        LauncherLog("WARNING: the VR mod has not been run with this Project Reclaimer version (it has with: %s). "
                    "What it reads of the client may not hold there; if the game misbehaves, that is the first "
                    "thing to rule out", ReclaimerProvenText().c_str());
    if (!client.passedOver.empty())
        LauncherLog("a newer Project Reclaimer is here too and was NOT started: %ls. The VR mod has not been run "
                    "with it. To start it all the same, put its full path as the one line of reclaimer_client.txt "
                    "beside this launcher", client.passedOver.c_str());
    // A game started by an elevated launcher is elevated, and the OpenXR loader ignores XR_RUNTIME_JSON in an
    // elevated process: a player who picks their headset's runtime with that variable gets the machine's
    // registered runtime instead (the mod's log then names that runtime and waits for a headset while the
    // player's own runtime is running). HaloMCCVRReclaimerLauncher.exe is this launcher built without the
    // administrator manifest; it is the one a Project Reclaimer folder gets.
    {
        HANDLE token = nullptr;
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                elevation.TokenIsElevated)
                LauncherLog("WARNING: this launcher is running as administrator, so Project Reclaimer will too and "
                            "OpenXR will ignore XR_RUNTIME_JSON there (the machine's registered runtime is used)");
            CloseHandle(token);
        }
    }

    const std::wstring dllPath = dir + L"\\HaloMCCVR.dll";
    const std::wstring relayPath = dir + L"\\" + kReclaimerRelayName;
    if (!FileExists(dllPath))
    {
        ErrorBox(L"HaloMCCVR.dll is missing from:\n" + dir +
                 L"\n\nIt belongs in this folder, next to HaloMCCVRLauncher.exe.");
        return kExitFailed;
    }
    if (const DWORD running = FindReclaimerProcess(nullptr))
    {
        ErrorBox(L"Project Reclaimer is already running (pid " + std::to_wstring(running) +
                 L").\n\nClose it, then run this launcher again: the VR mod has to be there before the game "
                 L"starts.");
        return kExitFailed;
    }
    ReclaimerPrepareSettings();
    // The relay's log is this launch's: several processes append to it.
    {
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"\\HaloMCCVRReclaimerRelay.log").c_str(), L"wt");
        if (f) fclose(f);
    }
    if (g_headless)
    {
        wchar_t layer[64]{};
        const DWORD n = GetEnvironmentVariableW(L"__COMPAT_LAYER", layer, static_cast<DWORD>(std::size(layer)));
        if (n && n < std::size(layer) && !_wcsicmp(layer, L"RunAsInvoker"))
        {
            SetEnvironmentVariableW(L"__COMPAT_LAYER", nullptr);
            LauncherLog("unattended launch: dropped __COMPAT_LAYER=RunAsInvoker before starting the game");
        }
    }

    // Started as a double-click starts it: no arguments (its "game-client"), in its own folder.
    const std::wstring clientDir = std::filesystem::path(clientExe).parent_path().wstring();
    std::wstring cmdline = L"\"" + clientExe + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(clientExe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        clientDir.c_str(), &si, &pi))
    {
        const DWORD e = GetLastError();
        LauncherLog("CreateProcessW failed: %lu", e);
        ErrorBox(L"Could not start Project Reclaimer:\n" + WinErr(e));
        return kExitFailed;
    }
    LauncherLog("Project Reclaimer created (suspended), pid %lu", pi.dwProcessId);
    std::wstring failWhy;
    if (!InjectDll(pi.hProcess, relayPath, failWhy))
    {
        LauncherLog("the relay was NOT loaded: %ls", failWhy.c_str());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        ErrorBox(L"Could not load the VR relay into Project Reclaimer, so it was closed.\n\nReason: " + failWhy);
        return kExitFailed;
    }
    LauncherLog("relay loaded; resuming Project Reclaimer");
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // The process just started is not the game: it starts a supervisor, which starts the engine worker. Success
    // is an engine worker that has the mod and is still there twelve seconds later.
    DWORD worker = 0;
    for (const ULONGLONG deadline = GetTickCount64() + 45000; !worker && GetTickCount64() < deadline;)
    {
        worker = FindReclaimerProcess(L"HaloMCCVR.dll");
        if (!worker)
            Sleep(100);
    }
    if (!worker)
    {
        LauncherLog("no Project Reclaimer process holds HaloMCCVR.dll after 45 s (see HaloMCCVRReclaimerRelay.log)");
        ErrorBox(L"Project Reclaimer started, but the VR mod did not reach its game process.\n\n"
                 L"The game, if it is open, is running without VR.\n"
                 L"Details are in HaloMCCVRReclaimerRelay.log and HaloMCCVRLauncher.log.");
        return kExitFailed;
    }
    LauncherLog("engine worker pid %lu holds HaloMCCVR.dll", worker);
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, worker);
    if (process)
    {
        const DWORD waited = WaitForSingleObject(process, 12000);
        DWORD code = 0;
        if (waited == WAIT_OBJECT_0)
            GetExitCodeProcess(process, &code);
        CloseHandle(process);
        if (waited == WAIT_OBJECT_0)
        {
            LauncherLog("the engine worker EXITED within 12 s, exit code %lu (0x%08X)", code, code);
            ErrorBox(L"Project Reclaimer's game process closed right after starting with the VR mod (exit code " +
                     std::to_wstring(code) + L").\n\nDetails are in HaloMCCVR.log and HaloMCCVRLauncher.log.");
            return kExitFailed;
        }
    }
    LauncherLog("engine worker still running after 12 s - launch looks good");
    return kExitOk;
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand)
{
    wchar_t self[32768]{};
    const auto length = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    if (!length || length >= std::size(self)) return 1;
    std::filesystem::path launcherDir = std::filesystem::path(self).parent_path();
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args && count > 1 && !wcscmp(args[1], L"--apply-update"))
    {
        if (count != 6 || (wcscmp(args[4], L"0") && wcscmp(args[4], L"1")))
        {
            LocalFree(args);
            ErrorBox(L"Invalid update request. Open the launcher and choose Check for updates.");
            return 1;
        }
        const std::filesystem::path payload(args[2]), gameRoot(args[3]);
        const bool retainConfig = args[4][0] == L'1';
        wchar_t* end = nullptr;
        const DWORD parentPid = wcstoul(args[5], &end, 10);
        const bool validPid = parentPid && parentPid != GetCurrentProcessId() && end && !*end;
        LocalFree(args); args = nullptr;
        if (!validPid) { ErrorBox(L"Invalid update launcher process."); return 1; }
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (parent)
        {
            const auto result = WaitForSingleObject(parent, 60000);
            CloseHandle(parent);
            if (result != WAIT_OBJECT_0) { ErrorBox(L"Close the previous VR launcher, then try the update again."); return 1; }
        }
        else if (GetLastError() != ERROR_INVALID_PARAMETER)
        {
            ErrorBox(L"Could not confirm that the previous launcher closed. Reopen the extracted package to update.");
            return 1;
        }
        mcc_installer::GameInstall game;
        if (!mcc_installer::ProbeInstall(gameRoot, game)) { ErrorBox(L"The MCC install folder could not be verified."); return 1; }
        const auto installed = mcc_installer::InstallPayload(payload, game, retainConfig);
        MessageBoxW(nullptr, installed.message.c_str(), L"Halo MCC VR update", MB_OK | (installed.success ? MB_ICONINFORMATION : MB_ICONERROR));
        if (!installed.success) return 1;
        // Reopen the newly installed launcher's menu. The separate Launch MCC
        // button is still required; installing an update never starts MCC.
        const auto updated = game.root / L"Halo_MCC_VR" / L"HaloMCCVRLauncher.exe";
        std::wstring command = L"\"" + updated.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(updated.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, updated.parent_path().c_str(), &startup, &process))
        {
            ErrorBox(L"The update is installed. Open HaloMCCVRLauncher.exe in Halo_MCC_VR to launch MCC.");
            return 1;
        }
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return 0;
    }
    // --headless (dev builds): launch THIS folder's mod. The menu's Launch always targets <game>\Halo_MCC_VR,
    // so a side-by-side install in another folder is only ever launched from its own folder this way.
#if HALOMCCVR_DEV_DIAGNOSTICS
    const bool headless = args && count == 2 && !wcscmp(args[1], L"--headless");
    g_headless = headless;
    // --flat-probe (dev): this folder's HaloMCCFlatProbe.dll alone, MCC flat (see g_flatProbe).
    const bool flatProbe = args && count == 2 && !wcscmp(args[1], L"--flat-probe");
    g_flatProbe = flatProbe;
#else
    const bool headless = false;
    const bool flatProbe = false;
#endif
    if (args) LocalFree(args);
    try
    {
        // The one-click route: HaloMCCVRReclaimerLauncher.exe (and an unattended launch) in a Project Reclaimer
        // deployment starts the client it finds, with no menu. HaloMCCVRLauncher.exe opens its menu, whose last
        // section starts Project Reclaimer from the path kept there.
        if (!flatProbe && (HALOMCCVR_RECLAIMER_LAUNCHER || headless))
        {
            const ReclaimerClient reclaimerClient = FindReclaimerClient(launcherDir);
            if (!reclaimerClient.exe.empty())
                return LaunchReclaimer(launcherDir.wstring(), reclaimerClient);
        }
        if (headless || flatProbe)
            return LaunchInstalledMod(launcherDir.wstring());
        std::wstring reclaimerExe;
        const auto selected = mcc_installer::ShowLauncherMenu(instance, launcherDir, showCommand, &reclaimerExe);
        if (!reclaimerExe.empty())
        {
            ReclaimerClient chosen;
            chosen.exe = reclaimerExe;
            chosen.version = ReclaimerVersionOfFile(reclaimerExe, &chosen.versionFromName);
            chosen.named = true;
            return LaunchReclaimer(reclaimer_launch::ModDir(launcherDir).wstring(), chosen);
        }
        return selected.empty() ? 0 : LaunchInstalledMod(selected);
    }
    catch (const std::exception& error)
    {
        const std::string detail(error.what());
        ErrorBox(L"The launcher could not complete this operation.\n\n" + std::wstring(detail.begin(), detail.end()));
        return 1;
    }
}
