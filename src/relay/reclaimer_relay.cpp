// HaloMCCVRReclaimerRelay.dll: carries the VR mod to Project Reclaimer's engine worker.
//
// Project Reclaimer (a standalone client for MCC's halo3.dll) does not run the engine in the process the player
// starts. That process starts itself again as a supervisor, and the supervisor starts the engine worker
// (src/common/reclaimer_host_logic.h has the measured tree). The worker creates its D3D11 device within a second
// of starting and may be started again by the supervisor, so a launcher that waits for it and then reaches in
// would race it every time.
//
// This DLL makes the route the Steam route's own: every Project Reclaimer process creates its child SUSPENDED,
// loads this DLL into it, loads HaloMCCVR.dll too when the child is the engine worker with the player's window,
// and only then lets it run. The launcher loads this DLL into the first process; each process hands it on.
//
//   * It hooks ONE function, kernel32!CreateProcessW, in Project Reclaimer client processes only. Loaded into
//     anything else it does nothing.
//   * It follows only children whose image is a Project Reclaimer client (never the dedicated server, never
//     another program the client starts).
//   * A failure never costs the player the game: a child that could not be given the DLLs is resumed as
//     Project Reclaimer started it, flat, and the log says why.
//
// Log: HaloMCCVRReclaimerRelay.log beside this DLL, one line per event with the writing pid (several processes
// append to it; the launcher empties it at each launch).

#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <iterator>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "../common/reclaimer_host_logic.h"

namespace
{
HMODULE g_self = nullptr;
wchar_t g_dir[1024]{};      // this DLL's folder, with the trailing backslash
wchar_t g_selfPath[1024]{}; // this DLL
wchar_t g_modPath[1024]{};  // HaloMCCVR.dll beside it

using CreateProcessWFn = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
                                       LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
CreateProcessWFn g_origCreateProcessW = nullptr;

void RelayLog(const char* format, ...)
{
    if (!g_dir[0])
        return;
    wchar_t path[1100]{};
    swprintf_s(path, L"%lsHaloMCCVRReclaimerRelay.log", g_dir);
    char line[1600];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int used = sprintf_s(line, "[%02u:%02u:%02u.%03u] pid %lu: ", now.wHour, now.wMinute, now.wSecond,
                         now.wMilliseconds, GetCurrentProcessId());
    va_list arguments;
    va_start(arguments, format);
    const int body = vsnprintf(line + used, sizeof(line) - used - 3, format, arguments);
    va_end(arguments);
    used = body < 0 ? static_cast<int>(sizeof(line)) - 3 : (std::min)(used + body, static_cast<int>(sizeof(line)) - 3);
    line[used++] = '\r';
    line[used++] = '\n';
    const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(used), &written, nullptr);
    CloseHandle(file);
}

// Make `process` (created suspended by us) call LoadLibraryW(path). kernel32 sits at one address in every
// 64-bit process of a session, which is what makes the local LoadLibraryW the remote one.
bool LoadInto(HANDLE process, const wchar_t* path, DWORD& error, const char*& step)
{
    const SIZE_T bytes = (wcslen(path) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
    {
        error = GetLastError();
        step = "VirtualAllocEx";
        return false;
    }
    bool loaded = false;
    if (!WriteProcessMemory(process, remote, path, bytes, nullptr))
    {
        error = GetLastError();
        step = "WriteProcessMemory";
    }
    else
    {
        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        const HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!thread)
        {
            error = GetLastError();
            step = "CreateRemoteThread";
        }
        else
        {
            if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0)
            {
                error = WAIT_TIMEOUT;
                step = "the load did not return in 15 s";
            }
            else
            {
                DWORD exitCode = 0;
                GetExitCodeThread(thread, &exitCode);
                loaded = exitCode != 0; // the low half of the module handle; 0 = LoadLibraryW failed
                if (!loaded)
                {
                    error = 0;
                    step = "LoadLibraryW returned NULL in the child";
                }
            }
            CloseHandle(thread);
        }
    }
    // A child whose load timed out may still be reading the path: leave the page to it.
    if (loaded || error != WAIT_TIMEOUT)
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return loaded;
}

bool FileExists(const wchar_t* path)
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

struct ChildPlan
{
    bool follow = false;
    reclaimer_host::Role role = reclaimer_host::Role::Supervisor;
};

// Which child this is, from what the parent asked for. Never throws: a child that cannot be classified is not
// followed.
ChildPlan PlanFor(LPCWSTR application, LPCWSTR commandLine) noexcept
{
    ChildPlan plan;
    try
    {
        const std::wstring_view command = commandLine ? std::wstring_view(commandLine) : std::wstring_view();
        std::wstring image;
        if (application && *application)
            image = application;
        else
        {
            const auto arguments = reclaimer_host::Arguments(command);
            if (!arguments.empty())
                image = arguments[0];
        }
        if (!reclaimer_host::IsClientExeName(reclaimer_host::Leaf(image)))
            return plan;
        plan.follow = true;
        plan.role = reclaimer_host::RoleOf(command);
    }
    catch (...)
    {
        plan = ChildPlan{};
    }
    return plan;
}

BOOL WINAPI CreateProcessWHook(LPCWSTR application, LPWSTR commandLine, LPSECURITY_ATTRIBUTES processAttributes,
                               LPSECURITY_ATTRIBUTES threadAttributes, BOOL inheritHandles, DWORD flags,
                               LPVOID environment, LPCWSTR currentDirectory, LPSTARTUPINFOW startup,
                               LPPROCESS_INFORMATION information)
{
    const ChildPlan plan = PlanFor(application, commandLine);
    if (!plan.follow || !information)
        return g_origCreateProcessW(application, commandLine, processAttributes, threadAttributes, inheritHandles,
                                    flags, environment, currentDirectory, startup, information);

    // The command line is copied first: CreateProcessW may write into the caller's buffer.
    wchar_t shown[700]{};
    if (commandLine)
        wcsncpy_s(shown, commandLine, _TRUNCATE);

    const BOOL created = g_origCreateProcessW(application, commandLine, processAttributes, threadAttributes,
                                              inheritHandles, flags | CREATE_SUSPENDED, environment,
                                              currentDirectory, startup, information);
    if (!created)
        return FALSE;

    RelayLog("child pid %lu is a Project Reclaimer %s: %ls", information->dwProcessId,
             reclaimer_host::RoleName(plan.role), shown);

    DWORD error = 0;
    const char* step = "";
    if (!LoadInto(information->hProcess, g_selfPath, error, step))
        RelayLog("child pid %lu: the relay was NOT loaded (%s, error %lu); what it starts will not be followed",
                 information->dwProcessId, step, error);

    if (plan.role == reclaimer_host::Role::ClientWorker)
    {
        if (!FileExists(g_modPath))
            RelayLog("child pid %lu: HaloMCCVR.dll is missing beside the relay; the game runs flat",
                     information->dwProcessId);
        else
        {
            // The mod sets its hooks on a thread of its own, and the worker reads its display size and creates
            // its swapchain within its first few hundred milliseconds; resumed at once it runs ahead of the mod's
            // render-size and swapchain hooks by a margin nobody chose (about 100 ms where it was measured). So
            // the worker stays suspended until the mod says those hooks are in (dllmain.cpp sets this event), or
            // eight seconds pass; the wait measures about a third of a second.
            // This orders the hooks before the swapchain and nothing more: the size the game is drawn at is the
            // host's own (g_hostOwnsRenderSize in config.h).
            wchar_t eventName[64]{};
            reclaimer_host::EarlyHooksEventName(eventName, std::size(eventName), information->dwProcessId);
            const HANDLE hooksReady = CreateEventW(nullptr, TRUE, FALSE, eventName);
            if (LoadInto(information->hProcess, g_modPath, error, step))
            {
                const ULONGLONG waitStart = GetTickCount64();
                const DWORD waited = hooksReady ? WaitForSingleObject(hooksReady, 8000) : WAIT_FAILED;
                RelayLog("child pid %lu: HaloMCCVR.dll loaded before the engine worker's first instruction; its "
                         "early hooks %s after %llu ms", information->dwProcessId,
                         waited == WAIT_OBJECT_0 ? "were in" : "were NOT reported in (the worker runs anyway)",
                         static_cast<unsigned long long>(GetTickCount64() - waitStart));
            }
            else
                RelayLog("child pid %lu: HaloMCCVR.dll was NOT loaded (%s, error %lu); the game runs flat",
                         information->dwProcessId, step, error);
            if (hooksReady)
                CloseHandle(hooksReady);
        }
    }

    if (!(flags & CREATE_SUSPENDED))
        ResumeThread(information->hThread);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

void Attach(HMODULE module)
{
    g_self = module;
    const DWORD length = GetModuleFileNameW(module, g_selfPath, static_cast<DWORD>(std::size(g_selfPath)));
    if (!length || length >= std::size(g_selfPath) - 1)
    {
        g_selfPath[0] = 0;
        return; // a path this long is not followed; nothing is hooked
    }
    wcscpy_s(g_dir, g_selfPath);
    wchar_t* slash = wcsrchr(g_dir, L'\\');
    if (!slash)
    {
        g_dir[0] = 0;
        return;
    }
    slash[1] = 0;
    swprintf_s(g_modPath, L"%lsHaloMCCVR.dll", g_dir);

    wchar_t host[1024]{};
    GetModuleFileNameW(nullptr, host, static_cast<DWORD>(std::size(host)));
    const wchar_t* hostLeaf = wcsrchr(host, L'\\');
    hostLeaf = hostLeaf ? hostLeaf + 1 : host;
    bool client = false;
    reclaimer_host::Role role = reclaimer_host::Role::Supervisor;
    try
    {
        client = reclaimer_host::IsClientExeName(hostLeaf);
        role = reclaimer_host::RoleOf(GetCommandLineW());
    }
    catch (...)
    {
        client = false;
    }
    if (!client)
    {
        RelayLog("loaded into %ls, which is not a Project Reclaimer client: nothing is hooked", hostLeaf);
        return;
    }
    if (MH_Initialize() != MH_OK ||
        MH_CreateHookApi(L"kernel32", "CreateProcessW", &CreateProcessWHook,
                         reinterpret_cast<void**>(&g_origCreateProcessW)) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        RelayLog("in %ls (%s): the process-start hook could NOT be installed; what this process starts will "
                 "not be followed", hostLeaf, reclaimer_host::RoleName(role));
        return;
    }
    RelayLog("in %ls (%s): following the Project Reclaimer processes it starts", hostLeaf,
             reclaimer_host::RoleName(role));
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    // The hook goes in HERE, not on a thread: the parent resumes this process as soon as this load returns, and
    // the first thing a supervisor does is start its child.
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        Attach(module);
    }
    return TRUE;
}
