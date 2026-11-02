#include <windows.h>
#include <string>
#include <cwctype>
#include <MinHook.h>
#include "../common/minhook_lifecycle.h"
#include "sigscan.h"
#include "host_unload_gate.h"
#include "../common/log.h"
#include "../common/config.h"
#include "../common/reclaimer_host_logic.h"
#include "d3d11_hook.h"
#include "vr.h"
#include "game.h"
#include "native_fault_probe.h"
#include "dev_pad.h"
#include "drive_cascade.h"
#include "install_search_cache.h"
#include "title_adapter.h"
#include "telemetry_recorder.h"

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

#ifndef HALOMCCVR_BUILD_HALO4
#define HALOMCCVR_BUILD_HALO4 "unknown"
#endif

#ifndef HALOMCCVR_BUILD_HALO2
#define HALOMCCVR_BUILD_HALO2 "unknown"
#endif

// Entry point of the injected DLL. DllMain itself must do almost nothing
// (Windows holds a global "loader lock" while it runs), so we immediately
// spawn a thread that does the real setup.

static HMODULE g_hModule = nullptr;

// Byte-pattern proofs read the GAME's code, not our jumps (sigscan.h). MinHook's own records say which bytes
// each enabled hook replaced; sig::Find matches against those.
static uint32_t EnabledHookPatches(uintptr_t base, size_t size, sig::HookPatch* out, uint32_t capacity)
{
    MCCVR_HOOK_PATCH local[64];
    const uint32_t wanted = capacity < 64 ? capacity : 64;
    const UINT total = MCCVR_CopyEnabledHookPatches(reinterpret_cast<LPCVOID>(base), size, local, wanted);
    if (total > wanted && capacity > wanted)
    {
        // More hooks in this image than the local buffer: copy straight into a buffer of the caller's size.
        auto* wide = static_cast<MCCVR_HOOK_PATCH*>(
            HeapAlloc(GetProcessHeap(), 0, sizeof(MCCVR_HOOK_PATCH) * capacity));
        if (!wide)
            return 0;
        const UINT again = MCCVR_CopyEnabledHookPatches(reinterpret_cast<LPCVOID>(base), size, wide, capacity);
        const uint32_t copied = again < capacity ? again : capacity;
        for (uint32_t i = 0; i < copied; ++i)
        {
            out[i].address = reinterpret_cast<uintptr_t>(wide[i].address);
            out[i].size = wide[i].size;
            memcpy(out[i].original, wide[i].original, sizeof(out[i].original));
        }
        HeapFree(GetProcessHeap(), 0, wide);
        return again;
    }
    const uint32_t copied = total < wanted ? total : wanted;
    for (uint32_t i = 0; i < copied; ++i)
    {
        out[i].address = reinterpret_cast<uintptr_t>(local[i].address);
        out[i].size = local[i].size;
        memcpy(out[i].original, local[i].original, sizeof(out[i].original));
    }
    return total;
}

// A proof that only held because our own hook was read as the bytes it replaced. It is not an error any more,
// but it is an install order nobody chose: say so once per place, in every build, so the proof gets moved ahead
// of the hook (tools/sweep_proof_hook_overlap.py lists the candidates offline).
static void ProofReadThroughOurHook(uintptr_t match, size_t length, uintptr_t patch)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    static uintptr_t seen[64];
    static size_t seenCount = 0;
    bool first = true;
    AcquireSRWLockExclusive(&lock);
    for (size_t i = 0; i < seenCount; ++i)
        if (seen[i] == match)
            first = false;
    if (first && seenCount < 64)
        seen[seenCount++] = match;
    else
        first = false;
    ReleaseSRWLockExclusive(&lock);
    if (!first)
        return;
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    const wchar_t* name = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(match), &module) &&
        module && GetModuleFileNameW(module, path, MAX_PATH))
    {
        name = path;
        for (const wchar_t* p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/')
                name = p + 1;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    LOG("PROOF AFTER HOOK: a %zu-byte pattern at %ls+0x%llX reads this mod's own enabled hook at +0x%llX. It was "
        "matched against the bytes the hook replaced, so the lane that asked is not refused. "
        "Take the proof before that hook is enabled.",
        length, name, static_cast<unsigned long long>(match - base),
        static_cast<unsigned long long>(patch - base));
}

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
    // Not MCC at all: Project Reclaimer runs MCC's halo3.dll without MCC's shell (src/relay/reclaimer_relay.cpp).
    // Named so a log from it is never read as a broken MCC session.
    if (path.find(L"\\project-reclaimer") != std::wstring::npos)
        return "NOT MCC: Project Reclaimer (standalone Halo 3 client, experimental)";
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
        "ReachRender %s, Halo4 %s, Halo2 %s, compiled "
        __DATE__ " " __TIME__ ")", GetCurrentProcessId(),
        HALOMCCVR_BUILD_COMMIT, HALOMCCVR_BUILD_ODST,
        HALOMCCVR_BUILD_REACH, HALOMCCVR_BUILD_REACH_RENDER,
        HALOMCCVR_BUILD_HALO4, HALOMCCVR_BUILD_HALO2);
    wchar_t hostExe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, hostExe, MAX_PATH);
    LOG("MCC edition: %s (%ls)", DetectMccEdition(hostExe), hostExe);
    LOG("Native fault evidence: %s (HaloMCCVR-native-faults.log; observe only, exceptions retain native handling)",
        NativeFaultProbe_Init(dir.c_str(),HALOMCCVR_BUILD_COMMIT)?"enabled":"unavailable");
    // The direct drives' address cache lives beside the log and the settings (drive_cascade.h).
    drive_cascade::SetCacheFolder(dir.c_str());
    // A title install's searches, saved per build of its DLL, live there too (install_search_cache.h).
    install_search_cache::SetFolder(dir.c_str());
    // What a drive resolved in a module is remembered for that image of it only: the title adapter's module
    // generation changes when MCC unloads a game DLL and loads it again, even at the same base.
    drive_cascade::SetModuleGenerationSource(&TitleAdapter_GetGeneration);
    const std::wstring primaryConfig = dir + L"halomccvr.cfg";
    const std::wstring legacyConfig = dir + L"halo3xr.cfg";
    ConfigLoadMigrating(primaryConfig.c_str(), legacyConfig.c_str());
    // Project Reclaimer draws the game at its own render resolution (config.h, g_hostOwnsRenderSize).
    if (reclaimer_host::IsClientExeName(reclaimer_host::Leaf(hostExe)))
        g_hostIsProjectReclaimer = g_hostOwnsRenderSize = true;
    if (!Telemetry_Init())
        LOG("telemetry: recorder unavailable; VR startup continues");
#if HALOMCCVR_DEV_DIAGNOSTICS
    // A dev build carries test hooks a player build does not; say so, and say loudly which are live.
    LOG("DEV BUILD: development diagnostics and headless-test hooks are compiled in (never packaged)");
    if (g_config.test_accept_untracked_head)
        LOG("DEV TEST OVERRIDE ACTIVE: test_accept_untracked_head = 1 -- roomscale and physical crouch accept "
            "an UNTRACKED head. For the headless simulator only; set it to 0 before playing in a headset.");
    if (g_config.reach_direct_drive != 0 && g_config.reach_direct_drive != 3)
        LOG("DEV OVERRIDE: reach_direct_drive = %d is a research write test (player builds accept 0 or 3)",
            g_config.reach_direct_drive);
    if (g_config.reach_aim_probe || g_config.reach_dd_watchpoint)
        LOG("DEV OVERRIDE: research tools on (reach_aim_probe = %d, reach_dd_watchpoint = %d)",
            g_config.reach_aim_probe, g_config.reach_dd_watchpoint);
    if (g_config.reach_sight_trace_flags)
        LOG("DEV OVERRIDE: reach_sight_trace_flags = 0x%llX mode %d replaces the resolved crosshair-trace "
            "flags (Reach aim trace)",
            g_config.reach_sight_trace_flags, g_config.reach_sight_trace_mode);
    if (g_config.reach_sight_trace_site >= 0)
        LOG("DEV OVERRIDE: reach_sight_trace_site = %d: Reach's aim trace is always made %s, whatever Co-op safe says",
            g_config.reach_sight_trace_site,
            g_config.reach_sight_trace_site ? "at the control pass" : "inside the unit's update");
    if (g_config.reach_effect_move_rule >= 0)
        LOG("DEV OVERRIDE: reach_effect_move_rule = %d: Reach moves an effect's place onto the gun %s, whatever "
            "Co-op safe says",
            g_config.reach_effect_move_rule,
            g_config.reach_effect_move_rule == 0 ? "always"
            : g_config.reach_effect_move_rule == 1 ? "only outside a simulation tick"
            : g_config.reach_effect_move_rule == 2 ? "never" : "only for the player's own weapon effects");
    if (g_config.dev_sim_trace || g_config.dev_sim_probe)
        LOG("DEV OVERRIDE: dev_sim_trace = %d, dev_sim_probe = %d (Reach: every object's state per tick to a file; "
            "the probe makes the mod's own calls for the local unit while a film replays)",
            g_config.dev_sim_trace, g_config.dev_sim_probe);
    if (g_config.dev_synthetic_pad)
        DevPad_Start(dir.c_str());
    if (g_config.dev_vehicle_unnamed)
        LOG("DEV OVERRIDE: dev_vehicle_unnamed = 1 (every Halo 3 vehicle is taken as one the mod cannot name, as a "
            "mod map's own: no identity, no tuned eye, no built-in camera preset)");
    if (g_config.dev_seat_body_probe)
        LOG("DEV OVERRIDE: dev_seat_body_probe = 1 (a seat's headless body of an unknown model hides one region at a "
            "time instead)");
#endif

    if (MH_Initialize() != MH_OK)
    {
        LOG("FATAL: MinHook failed to initialize; mod inactive");
        return 0;
    }
    sig::SetHookPatchSource(&EnabledHookPatches);
    sig::SetProofThroughHookObserver(&ProofReadThroughOurHook);
    // MCC's reload of a game DLL after a game waits for a title core that still holds it (host_unload_gate.cpp).
    (void)HostUnloadGate_Install();
    if (!InstallD3D11Hooks())
    {
        LOG("FATAL: D3D11 hooks failed to install; mod inactive");
        return 0;
    }
    LOG("D3D11 hooks installed; waiting for the game to render its first frame");
    // Project Reclaimer only: its relay holds the engine worker suspended until the render-size and D3D11 hooks
    // above exist (src/relay/reclaimer_relay.cpp says why). Nobody waits for this in MCC, and nothing is created
    // there.
    if (reclaimer_host::IsClientExeName(reclaimer_host::Leaf(hostExe)))
    {
        wchar_t eventName[64]{};
        reclaimer_host::EarlyHooksEventName(eventName, std::size(eventName), GetCurrentProcessId());
        if (const HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName))
        {
            SetEvent(ready);
            CloseHandle(ready);
            LOG("Project Reclaimer: early hooks reported to the relay; the engine worker starts now");
        }
        else
            LOG("Project Reclaimer: no relay is waiting for the early hooks (the worker was already running)");
    }

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
