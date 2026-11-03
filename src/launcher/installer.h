#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace mcc_installer {
struct GameInstall { std::filesystem::path root; bool store = false; };
struct InstallResult { bool success = false; std::wstring message; size_t addedKeys = 0; std::filesystem::path backup; };
bool ProbeInstall(const std::filesystem::path& candidate, GameInstall& result);
std::vector<GameInstall> DetectInstalls(const std::filesystem::path& launcherDir);
InstallResult InstallPayload(const std::filesystem::path& payload, const GameInstall& game, bool retainConfig);
// After an update installs: brings the launcher the player opened up to the installed one, wherever it is. A package
// folder's ModFiles comes along, so that the folder stays one version, and the launcher it replaces is kept beside it
// as <name>.previous.exe, a way back from a launcher that will not start. The update has already succeeded by this point,
// so a failure here only leaves that copy as it was, and is the caller's to report.
bool RefreshLauncherCopy(const std::filesystem::path& installedLauncher, const std::filesystem::path& payload, const std::filesystem::path& copy, std::wstring& error);
// Empty on cancellation. Returning a directory only requests the existing
// native launch path; installing never starts the game. `reclaimerExe`, when given, receives the Project
// Reclaimer game program the player chose to start instead (the menu's last section); empty otherwise.
std::wstring ShowLauncherMenu(HINSTANCE instance, const std::filesystem::path& launcherDir, int showCommand, std::wstring* reclaimerDir,
                              std::wstring* reclaimerExe = nullptr);
} // namespace mcc_installer
