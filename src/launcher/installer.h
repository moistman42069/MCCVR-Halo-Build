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
// Empty on cancellation. Returning a directory only requests the existing
// native launch path; installing never starts the game.
std::wstring ShowLauncherMenu(HINSTANCE instance, const std::filesystem::path& launcherDir, int showCommand);
} // namespace mcc_installer
