#pragma once
#include "reclaimer_host_logic.h"
#include <string>
#include <string_view>

// What HaloMCCVRInject.exe (src/injector/injector.cpp) will do: load one of the mod's own DLLs, from the injector's
// own folder, into one of the games the mod is for. Anything else it refuses, so it is never a general-purpose
// injector. It is the only program that loads code into another process; the launcher, which downloads and installs
// updates, has no such code (antivirus scores a program that does both as a trojan). No Windows calls here; the unit
// test is tests/injector_policy_tests.cpp.
namespace injector_policy
{
inline bool SameName(std::wstring_view a, std::wstring_view b)
{
    return reclaimer_host::Lower(a) == reclaimer_host::Lower(b);
}

// MCC on Steam and on the Microsoft Store, and Project Reclaimer's game client.
inline bool IsGameProgram(std::wstring_view path)
{
    const std::wstring_view leaf = reclaimer_host::Leaf(path);
    return SameName(leaf, L"MCC-Win64-Shipping.exe") || SameName(leaf, L"MCCWinStore-Win64-Shipping.exe") ||
           reclaimer_host::IsClientExeName(leaf);
}

// The mod's DLLs. The dev builds' flat probe (HaloMCCFlatProbe.dll) only where those builds ask for it.
inline bool IsModDll(std::wstring_view path, bool devBuild)
{
    const std::wstring_view leaf = reclaimer_host::Leaf(path);
    return SameName(leaf, L"HaloMCCVR.dll") || SameName(leaf, L"HaloMCCVRReclaimerRelay.dll") ||
           (devBuild && SameName(leaf, L"HaloMCCFlatProbe.dll"));
}

// The folder a path is in, without its last separator ("" for a bare name).
inline std::wstring_view Folder(std::wstring_view path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? std::wstring_view{} : path.substr(0, slash);
}

// The DLL must sit in the injector's own folder: a full path, compared without case, both already made absolute.
inline bool InOwnFolder(std::wstring_view dllPath, std::wstring_view injectorPath)
{
    const std::wstring_view dllFolder = Folder(dllPath), ownFolder = Folder(injectorPath);
    return !dllFolder.empty() && SameName(dllFolder, ownFolder);
}
}
