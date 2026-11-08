// Stands in for the game in tests/injector_tests.cpp, copied there as MCC-Win64-Shipping.exe: it only waits, so the
// injector has a process to load the stand-in DLL into. It ends by itself after twenty seconds.
#include <windows.h>

int main()
{
    Sleep(20000);
    return 0;
}
