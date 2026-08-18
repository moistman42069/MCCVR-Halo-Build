#include <windows.h>
#include <string>
#include <cwctype>
#include <MinHook.h>
#include "../common/log.h"
#include "../common/config.h"
#include "d3d11_hook.h"
#include "vr.h"
#include "game.h"

#ifndef HALOMCCVR_BUILD_COMMIT
#define HALOMCCVR_BUILD_COMMIT "unknown"
#endif

#ifndef HALOMCCVR_BUILD_ODST
#define HALOMCCVR_BUILD_ODST "unknown"
#endif

#ifndef HALOMCCVR_BUILD_REACH
#define HALOMCCVR_BUILD_REACH "unknown"
#endif

#ifndef HALOMCCVR_BUILD_REACH_RENDER
#define HALOMCCVR_BUILD_REACH_RENDER "unknown"
#endif

// Entry point of the injected DLL. DllMain itself must do almost nothing
// (Windows holds a global "loader lock" while it runs), so we immediately
// spawn a thread that does the real setup.

static HMODULE g_hModule = nullptr;

// Record WHICH edition of MCC this session ran on, for the same reason the
// headset name is recorded: a log that cannot tell its own sessions apart
// cannot settle an argument. The two editions ship byte-identical game modules
// (docs/MCC-EDITIONS-EVIDENCE.md) but launch completely differently, so a
// report that does not name the edition is not reproducible.
//
// Derived from this process, never from the launcher, so it is still right when
// the DLL is injected some other way. The WindowsApps path test comes first
// because it survives someone renaming the executable to the Steam name - the
// old community workaround - which the name test alone would misreport.
static const char* DetectMccEdition(const wchar_t* exePath)
{
    std::wstring path(exePath);
    for (wchar_t& c : path)
        c = towlower(c);
    if (path.find(L"\\windowsapps\\") != std::wstring::npos ||
        path.find(L"mccwinstore-win64-shipping.exe") != std::wstring::npos)
        return "Microsoft Store / Xbox app (Game Pass)";
    if (path.find(L"mcc-win64-shipping.exe") != std::wstring::npos)
        return "Steam";
    return "unrecognised host executable";
}

static DWORD WINAPI InitThread(LPVOID)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);
    std::wstring dir(path);
    dir.resize(dir.find_last_of(L'\\') + 1);

    LogInit((dir + L"HaloMCCVR.log").c_str());
    // Commit + compile option identify source/configuration. The timestamp is
    // context only; verify the installed DLL's SHA-256 separately.
    LOG("HaloMCCVR loaded into pid %lu (source %s, ODST %s, Reach %s, "
        "ReachRender %s, compiled "
        __DATE__ " " __TIME__ ")", GetCurrentProcessId(),
        HALOMCCVR_BUILD_COMMIT, HALOMCCVR_BUILD_ODST,
        HALOMCCVR_BUILD_REACH, HALOMCCVR_BUILD_REACH_RENDER);
    wchar_t hostExe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, hostExe, MAX_PATH);
    LOG("MCC edition: %s (%ls)", DetectMccEdition(hostExe), hostExe);
    const std::wstring primaryConfig = dir + L"halomccvr.cfg";
    const std::wstring legacyConfig = dir + L"halo3xr.cfg";
    ConfigLoadMigrating(primaryConfig.c_str(), legacyConfig.c_str());

    if (MH_Initialize() != MH_OK)
    {
        LOG("FATAL: MinHook failed to initialize; mod inactive");
        return 0;
    }
    if (!InstallD3D11Hooks())
    {
        LOG("FATAL: D3D11 hooks failed to install; mod inactive");
        return 0;
    }
    LOG("D3D11 hooks installed; waiting for the game to render its first frame");

    // Install input interception immediately. OpenXR instance creation below
    // blocks for roughly 20 seconds while SteamVR starts; waiting until after
    // that made MCC finish controller enumeration before our hooks existed.
    // This same worker also waits for halo3.dll and installs camera hooks.
    Game_Init();

    // Start OpenXR on this background thread so the slow runtime startup does
    // not stall the render thread. Input interception is already active above.
    VR_InitInstance();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t)
            CloseHandle(t);
    }
    return TRUE;
}
