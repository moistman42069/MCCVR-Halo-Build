// Stands in for HaloMCCVR.dll in tests/injector_tests.cpp: once loaded, it writes "loaded" beside itself.
#include <windows.h>
#include <fstream>
#include <string>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        wchar_t path[MAX_PATH * 4]{};
        GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        std::wstring marker = path;
        marker = marker.substr(0, marker.find_last_of(L'\\') + 1) + L"loaded.txt";
        std::ofstream(marker) << "loaded";
    }
    return TRUE;
}
