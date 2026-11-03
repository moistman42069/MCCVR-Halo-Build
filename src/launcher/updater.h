#pragma once
#include "installer.h"
#include "../common/release_update_policy.h"
namespace mcc_installer {
std::string Sha256File(const std::filesystem::path& path);
// A repository's installable releases, newest first.
std::vector<ReleaseUpdate> ListReleases(const ReleaseChannel& channel);
// The list from whichever repository's newest release is furthest ahead by date. One that cannot be read is passed
// over and named in `passedOver`; with none readable this throws.
std::vector<ReleaseUpdate> ListLatestAcrossChannels(size_t& channel, std::wstring& passedOver);
// Download and safely extract, then return the verified package payload path.
std::filesystem::path DownloadUpdate(const ReleaseUpdate& release);
// Exposed separately so normal and malicious archive fixtures exercise the
// exact extraction path without network access or an MCC installation.
void ExtractUpdateArchive(const std::filesystem::path& archive, const std::filesystem::path& output);
// Starts an installer helper outside the installation and waits for this
// launcher to exit before replacing it. It never starts MCC automatically. `replaceThisLauncher` has it also bring this
// launcher, wherever it is, up to the installed one.
bool StartUpdateHelper(const std::filesystem::path& payload, const GameInstall& game, bool retainConfig, bool replaceThisLauncher, std::wstring& error);
}
