#pragma once

// PROJECT RECLAIMER IN THE LAUNCHER'S MENU (experimental). The menu's last section names the Project Reclaimer
// game program, keeps that choice, and starts it with the VR mod. What the section needs to know about a client
// is here; launcher.cpp implements it beside the launch itself.

#include <filesystem>
#include <string>

namespace reclaimer_launch
{
// The folder whose HaloMCCVR.dll, relay and settings a Project Reclaimer launch from this launcher uses: the
// launcher's own folder, or its ModFiles folder in a package that has not been installed.
std::filesystem::path ModDir(const std::filesystem::path& launcherDir);

// The kept choice: the one line of reclaimer_client.txt beside the launcher. Empty when there is none.
std::wstring SavedClient(const std::filesystem::path& launcherDir);
// Keeps `exe` as the choice; an empty `exe` forgets it. A path that is not a Project Reclaimer game program on
// this disk is not kept. False: the file could not be written.
bool SaveClient(const std::filesystem::path& launcherDir, const std::wstring& exe);

// Looks for an installed client: where the game's own join links point (it registers a `reclaimer:` link the
// first time it runs), a client that is running now, then the folders around the launcher. Empty when none is
// found; `how` is the sentence the menu shows for where it was found.
std::wstring DetectClient(const std::filesystem::path& launcherDir, std::wstring& how);

struct ClientInfo
{
    std::wstring text;        // what the menu shows under the path
    bool startable = false;   // the path is a client on this disk and the VR files to start it with are here
};
ClientInfo Describe(const std::filesystem::path& launcherDir, const std::wstring& exe);
} // namespace reclaimer_launch
