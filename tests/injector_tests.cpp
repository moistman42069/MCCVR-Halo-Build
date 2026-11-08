// HaloMCCVRInject.exe: its rules (injector_policy.h), then the real program loading a stand-in DLL into a stand-in game,
// and refusing everything else.
#include "../src/common/injector_policy.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace
{
int failures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}

void Rules()
{
    using namespace injector_policy;
    Check(IsGameProgram(L"G:\\Steam\\MCC\\Binaries\\Win64\\MCC-Win64-Shipping.exe") &&
              IsGameProgram(L"C:\\XboxGames\\MCC\\Binaries\\Win64\\mccwinstore-win64-shipping.EXE") &&
              IsGameProgram(L"D:\\PR\\project-reclaimer-client-v0.10.0.exe") && IsGameProgram(L"project-reclaimer-v0.9.8.exe"),
          "MCC on both stores and Project Reclaimer's client are games");
    Check(!IsGameProgram(L"C:\\Windows\\explorer.exe") && !IsGameProgram(L"project-reclaimer-dedicated-v0.10.0.exe") &&
              !IsGameProgram(L"MCC-Win64-Shipping.exe.bak") && !IsGameProgram(L""),
          "nothing else is: not Explorer, nor Reclaimer's dedicated server");
    Check(IsModDll(L"C:\\x\\HaloMCCVR.dll", false) && IsModDll(L"C:\\x\\haloMCCVRReclaimerRelay.DLL", false) &&
              !IsModDll(L"C:\\x\\HaloMCCFlatProbe.dll", false) && IsModDll(L"C:\\x\\HaloMCCFlatProbe.dll", true) &&
              !IsModDll(L"C:\\x\\evil.dll", true),
          "only the mod's DLLs; the flat probe only in dev builds");
    Check(InOwnFolder(L"C:\\Mod\\HaloMCCVR.dll", L"C:\\mod\\HaloMCCVRInject.exe") &&
              !InOwnFolder(L"C:\\Other\\HaloMCCVR.dll", L"C:\\Mod\\HaloMCCVRInject.exe") &&
              !InOwnFolder(L"C:\\Mod\\Sub\\HaloMCCVR.dll", L"C:\\Mod\\HaloMCCVRInject.exe") &&
              !InOwnFolder(L"HaloMCCVR.dll", L"C:\\Mod\\HaloMCCVRInject.exe"),
          "the DLL must be in the injector's own folder");
}

struct Run { DWORD exitCode = 99; std::string said; };
Run Inject(const fs::path& injector, DWORD pid, const fs::path& dll)
{
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) throw std::runtime_error("CreatePipe");
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = writeEnd; si.hStdError = writeEnd;
    std::wstring command = L"\"" + injector.wstring() + L"\" " + std::to_wstring(pid) + L" \"" + dll.wstring() + L"\"";
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(injector.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        throw std::runtime_error("cannot start the injector");
    CloseHandle(writeEnd);
    WaitForSingleObject(pi.hProcess, 30000);
    Run run;
    GetExitCodeProcess(pi.hProcess, &run.exitCode);
    char buffer[512]; DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read) run.said.append(buffer, read);
    CloseHandle(readEnd); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return run;
}

PROCESS_INFORMATION StartSuspended(const fs::path& program)
{
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring command = L"\"" + program.wstring() + L"\"";
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, program.parent_path().c_str(), &si, &pi))
        throw std::runtime_error("cannot start the stand-in game");
    return pi;
}

void End(PROCESS_INFORMATION& pi)
{
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

void RealInjection()
{
    const fs::path sandbox = fs::temp_directory_path() /
        (L"HaloMCCVR-injector-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const fs::path mod = sandbox / L"Halo_MCC_VR", game = sandbox / L"MCC";
    fs::create_directories(mod); fs::create_directories(game); fs::create_directories(sandbox / L"elsewhere");
    fs::copy_file(HALOMCCVR_TEST_INJECTOR, mod / L"HaloMCCVRInject.exe");
    fs::copy_file(HALOMCCVR_TEST_DLL, mod / L"HaloMCCVR.dll");
    fs::copy_file(HALOMCCVR_TEST_DLL, mod / L"Other.dll");
    fs::copy_file(HALOMCCVR_TEST_DLL, sandbox / L"elsewhere" / L"HaloMCCVR.dll");
    fs::copy_file(HALOMCCVR_TEST_HOST, game / L"MCC-Win64-Shipping.exe");
    fs::copy_file(HALOMCCVR_TEST_HOST, game / L"SomethingElse.exe");
    const fs::path injector = mod / L"HaloMCCVRInject.exe";

    // Another program is never touched, however it is asked.
    auto other = StartSuspended(game / L"SomethingElse.exe");
    const Run refusedProgram = Inject(injector, other.dwProcessId, mod / L"HaloMCCVR.dll");
    End(other);
    Check(refusedProgram.exitCode == 1 && refusedProgram.said.find("not MCC or Project Reclaimer") != std::string::npos &&
              !fs::exists(mod / L"loaded.txt"),
          "a program that is not a game is refused, and nothing is loaded into it");

    auto gameProcess = StartSuspended(game / L"MCC-Win64-Shipping.exe");
    const Run refusedName = Inject(injector, gameProcess.dwProcessId, mod / L"Other.dll");
    const Run refusedFolder = Inject(injector, gameProcess.dwProcessId, sandbox / L"elsewhere" / L"HaloMCCVR.dll");
    Check(refusedName.exitCode == 1 && refusedFolder.exitCode == 1 && !fs::exists(mod / L"loaded.txt") &&
              !fs::exists(sandbox / L"elsewhere" / L"loaded.txt"),
          "a DLL that is not the mod's, or not in the injector's folder, is refused");

    const Run loaded = Inject(injector, gameProcess.dwProcessId, mod / L"HaloMCCVR.dll");
    Check(loaded.exitCode == 0 && loaded.said.empty(), "the mod's DLL loads into the game");
    Check(fs::exists(mod / L"loaded.txt"), "and it really ran inside the game");
    End(gameProcess);

    const Run gone = Inject(injector, gameProcess.dwProcessId, mod / L"HaloMCCVR.dll");
    Check(gone.exitCode == 1 && !gone.said.empty(), "a process that has ended is a failure with its reason");

    std::error_code ignored;
    if (fs::equivalent(sandbox.parent_path(), fs::temp_directory_path(), ignored)) fs::remove_all(sandbox, ignored);
}
}

int main()
{
    try
    {
        Rules();
        RealInjection();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
    std::cout << "injector tests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
