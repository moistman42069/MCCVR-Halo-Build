#pragma once
#include "installer.h"
#include "../common/release_update_policy.h"
namespace mcc_installer {
std::string Sha256File(const std::filesystem::path& path);
ReleaseUpdate CheckForUpdate();
// Download and safely extract, then return the verified package payload path.
std::filesystem::path DownloadUpdate(const ReleaseUpdate& release);
// Exposed separately so normal and malicious archive fixtures exercise the
// exact extraction path without network access or an MCC installation.
void ExtractUpdateArchive(const std::filesystem::path& archive, const std::filesystem::path& output);
// Starts an installer helper outside the installation and waits for this
// launcher to exit before replacing it. It never starts MCC automatically.
bool StartUpdateHelper(const std::filesystem::path& payload, const GameInstall& game, bool retainConfig, std::wstring& error);
}
