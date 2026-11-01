#pragma once
// DEV BUILD ONLY: the synthetic gamepad for automated tests (no physical controller). The command language and
// the reasons for it are in src/common/dev_pad_script.h; tools/Send-MccPad.ps1 is the sending side. A player build compiles
// both calls to nothing, so no player can drive the game from a file.

#include <windows.h>
#include <Xinput.h>

#if HALOMCCVR_DEV_DIAGNOSTICS
// Start the worker (dev key dev_synthetic_pad = 1) that runs scripts dropped as <modDir>halomccvr_pad.txt.
void DevPad_Start(const wchar_t* modDir);
// Merge the synthetic pad into this XInput poll the way a physical controller's state would arrive.
void DevPad_Apply(XINPUT_GAMEPAD& pad);
#else
inline void DevPad_Start(const wchar_t*) {}
inline void DevPad_Apply(XINPUT_GAMEPAD&) {}
#endif
