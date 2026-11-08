// HaloMCCVRInject.exe <game process id> <full path of the mod DLL>
//
// Loads one of the mod's own DLLs into a game the mod is for, and nothing else (injector_policy.h): the launcher starts
// the game, suspended where it can, and runs this to load the mod before resuming it. Keeping this apart from the
// launcher means the program that downloads and installs updates contains no code that writes into another process,
// and this one, small and seldom changed, stays the same file from one release to the next.
//
// Method: classic CreateRemoteThread + LoadLibraryW. Exit code 0 when the DLL loaded; otherwise 1, with the reason as
// one line of UTF-8 on standard error for the launcher to show.
#include <windows.h>
#include <cstdio>
#include <string>
#include "injector_policy.h"

namespace
{
#ifndef HALOMCCVR_DEV_DIAGNOSTICS
#define HALOMCCVR_DEV_DIAGNOSTICS 0
#endif

std::wstring WinErr(DWORD code)
{
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring message = text ? text : L"";
    if (text) LocalFree(text);
    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' '))
        message.pop_back();
    return message + L" (" + std::to_wstring(code) + L")";
}

int Fail(const std::wstring& why)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, why.c_str(), static_cast<int>(why.size()), nullptr, 0, nullptr, nullptr);
    std::string text(length > 0 ? length : 0, '\0');
    if (length > 0) WideCharToMultiByte(CP_UTF8, 0, why.c_str(), static_cast<int>(why.size()), text.data(), length, nullptr, nullptr);
    fprintf(stderr, "%s\n", text.c_str());
    return 1;
}

std::wstring FullPath(const wchar_t* path)
{
    wchar_t full[MAX_PATH * 4]{};
    const DWORD length = GetFullPathNameW(path, static_cast<DWORD>(std::size(full)), full, nullptr);
    return length && length < std::size(full) ? std::wstring(full) : std::wstring();
}

// Write the DLL path into the game process and make it call LoadLibraryW on it.
// LoadLibraryW lives at the same address in every 64-bit process.
bool InjectDll(HANDLE process, const std::wstring& dllPath, std::wstring& failWhy)
{
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remoteMem, 0, nullptr);
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
                    failWhy = L"LoadLibraryW returned NULL inside the game (missing dependency or blocked DLL?)";
            }
            else
                failWhy = L"the injection thread timed out";
            CloseHandle(thread);
        }
    }
    VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
    return injected;
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
        return Fail(L"Usage: HaloMCCVRInject.exe <game process id> <full path of the mod DLL>");
    wchar_t* end = nullptr;
    const unsigned long pid = wcstoul(argv[1], &end, 10);
    if (!pid || !end || *end)
        return Fail(L"Not a process id: " + std::wstring(argv[1]));

    wchar_t self[MAX_PATH * 4]{};
    const DWORD selfLength = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    const std::wstring dllPath = FullPath(argv[2]);
    if (!selfLength || selfLength >= std::size(self) || dllPath.empty())
        return Fail(L"Cannot resolve the injector's or the DLL's folder.");
    if (!injector_policy::IsModDll(dllPath, HALOMCCVR_DEV_DIAGNOSTICS != 0) || !injector_policy::InOwnFolder(dllPath, self))
        return Fail(L"Only the mod's own DLL, from the injector's folder, is loaded: " + dllPath);
    const DWORD attributes = GetFileAttributesW(dllPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return Fail(dllPath + L" is missing.");

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                                 FALSE, pid);
    if (!process)
        return Fail(L"OpenProcess: " + WinErr(GetLastError()));
    wchar_t image[MAX_PATH * 4]{};
    DWORD imageLength = static_cast<DWORD>(std::size(image));
    if (!QueryFullProcessImageNameW(process, 0, image, &imageLength) || !injector_policy::IsGameProgram(image))
    {
        CloseHandle(process);
        return Fail(L"Process " + std::to_wstring(pid) + L" is not MCC or Project Reclaimer; nothing was loaded.");
    }
    std::wstring failWhy;
    const bool injected = InjectDll(process, dllPath, failWhy);
    CloseHandle(process);
    return injected ? 0 : Fail(failWhy);
}
