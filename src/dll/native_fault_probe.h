#pragma once

// Diagnostic only: never handles, retries or changes a native exception.
// Initialize on the DLL worker, then poll on the game worker (not a hot hook).
bool NativeFaultProbe_Init(const wchar_t* directory, const char* source);
void NativeFaultProbe_Poll();
