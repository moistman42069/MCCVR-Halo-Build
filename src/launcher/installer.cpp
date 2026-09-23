#include "installer.h"
#include "../common/installer_policy.h"
#include <appmodel.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <fstream>
#include <sstream>
#include <array>
#include <system_error>
#include <cwctype>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace mcc_installer {
namespace fs = std::filesystem;
namespace {
std::string Read(const fs::path& path, size_t limit = 16 * 1024 * 1024) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read file.");
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0 || static_cast<uint64_t>(length) > limit) throw std::runtime_error("File is too large.");
    std::string text(static_cast<size_t>(length), '\0');
    stream.seekg(0);
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) throw std::runtime_error("Cannot finish reading file.");
    return text;
}
std::string TryRead(const fs::path& path) { try { return Read(path); } catch (...) { return {}; } }
std::wstring Wide(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count && !text.empty()) throw std::runtime_error("Invalid UTF-8 path.");
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
bool IsFile(const fs::path& path) { std::error_code ec; return fs::is_regular_file(path, ec); }
bool Exists(const fs::path& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring RegistryString(HKEY hive, const wchar_t* key, const wchar_t* name) {
    wchar_t value[32768]{}; DWORD bytes = sizeof(value);
    return RegGetValueW(hive, key, name, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS ? value : L"";
}
bool NoReparse(const fs::path& path) {
    fs::path current;
    for (const auto& component : fs::absolute(path)) {
        current /= component;
        const auto attr = GetFileAttributesW(current.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}
bool GameRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true; // Can't prove files are idle.
    PROCESSENTRY32W entry{sizeof(entry)};
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (!_wcsicmp(entry.szExeFile, L"MCC-Win64-Shipping.exe") || !_wcsicmp(entry.szExeFile, L"MCCWinStore-Win64-Shipping.exe")) { running = true; break; }
    } while (Process32NextW(snapshot, &entry));
    else running = true;
    CloseHandle(snapshot);
    return running;
}
struct HashProvider {
    BCRYPT_ALG_HANDLE algorithm{};
    HashProvider() { if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA-256 is unavailable."); }
    ~HashProvider() { if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
};
std::wstring UniqueSuffix() {
    SYSTEMTIME time{}; GetSystemTime(&time);
    wchar_t suffix[96]{};
    swprintf_s(suffix, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu-%llu", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId(), GetTickCount64());
    return suffix;
}
struct InstallMutex {
    HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\HaloMCCVRInstaller");
    bool owned = false;
    InstallMutex() { if (handle) { auto result = WaitForSingleObject(handle, 0); owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED; } }
    ~InstallMutex() { if (owned) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
};
}

// Shared with updater: digest check is against GitHub's asset digest as well
// as the per-file manifest, never just the archive filename.
std::string Sha256File(const fs::path& path) {
    HashProvider provider;
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptCreateHash(provider.algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) throw std::runtime_error("Cannot create SHA-256 hash.");
    std::array<unsigned char, 32> digest{};
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read file for SHA-256.");
        std::array<char, 65536> buffer{};
        while (file) {
            file.read(buffer.data(), buffer.size());
            if (file.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(file.gcount()), 0) < 0)
                throw std::runtime_error("SHA-256 read failed.");
        }
        if (!file.eof() || BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("SHA-256 failed.");
    } catch (...) { BCryptDestroyHash(hash); throw; }
    BCryptDestroyHash(hash);
    const char* hex = "0123456789abcdef";
    std::string value;
    for (auto byte : digest) { value += hex[byte >> 4]; value += hex[byte & 15]; }
    return value;
}

bool ProbeInstall(const fs::path& candidate, GameInstall& result) {
    std::error_code ec;
    auto root = fs::absolute(candidate, ec).lexically_normal();
    if (ec) return false;
    if (IsFile(root)) root = root.parent_path();
    for (int level = 0; level < 6 && !root.empty(); ++level) {
        for (const auto& base : {root, root / L"Content"}) {
            const auto bin = base / L"MCC" / L"Binaries" / L"Win64";
            const bool store = IsFile(bin / L"MCCWinStore-Win64-Shipping.exe");
            if (store || IsFile(bin / L"MCC-Win64-Shipping.exe")) {
                result = {base.lexically_normal(), store || IsFile(base / L"MicrosoftGame.config")};
                return true;
            }
        }
        const auto parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    return false;
}

std::vector<GameInstall> DetectInstalls(const fs::path& launcherDir) {
    std::vector<GameInstall> results;
    auto add = [&](const fs::path& candidate) {
        if (candidate.empty()) return;
        GameInstall game;
        if (!ProbeInstall(candidate, game)) return;
        for (const auto& old : results) if (!_wcsicmp(old.root.c_str(), game.root.c_str())) return;
        results.push_back(game);
    };
    add(launcherDir);
    std::vector<fs::path> libraries;
    for (const auto& steam : {RegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
                              RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
                              RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath")}) {
        if (steam.empty()) continue;
        libraries.emplace_back(steam);
        const auto vdf = TryRead(fs::path(steam) / L"steamapps" / L"libraryfolders.vdf");
        for (const auto& library : VdfValues(vdf, "path")) libraries.emplace_back(Wide(library));
    }
    for (const auto& library : libraries) {
        auto names = VdfValues(TryRead(library / L"steamapps" / L"appmanifest_976730.acf"), "installdir");
        if (names.empty()) names.push_back("Halo The Master Chief Collection");
        for (const auto& name : names) if (SafeRelativePath(name)) add(library / L"steamapps" / L"common" / Wide(name));
    }
    // Package registration is the authoritative Store location, including a
    // user-selected Xbox library on any drive. This does not activate it.
    UINT32 count = 0, length = 0;
    if (GetPackagesByPackageFamily(L"Microsoft.Chelan_8wekyb3d8bbwe", &count, nullptr, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER && count < 128 && length < 1024 * 1024) {
        std::vector<PWSTR> names(count); std::vector<wchar_t> buffer(length);
        if (GetPackagesByPackageFamily(L"Microsoft.Chelan_8wekyb3d8bbwe", &count, names.data(), &length, buffer.data()) == ERROR_SUCCESS) {
            for (UINT32 i = 0; i < count; ++i) {
                UINT32 pathLength = 0;
                if (GetPackagePathByFullName(names[i], &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER || pathLength > 32768) continue;
                std::wstring path(pathLength, 0);
                if (GetPackagePathByFullName(names[i], &pathLength, path.data()) == ERROR_SUCCESS) { path.resize(wcslen(path.c_str())); add(path); }
            }
        }
    }
    // Bounded fallback for Xbox installations that have not registered for the
    // current Windows account. Never recursively crawl drives or network shares.
    wchar_t drives[512]{}; GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    for (const wchar_t* drive = drives; *drive; drive += wcslen(drive) + 1) {
        if (GetDriveTypeW(drive) != DRIVE_FIXED) continue;
        for (const auto* name : {L"XboxGames", L"XBOX"}) {
            std::error_code ec;
            fs::directory_iterator iterator(fs::path(drive) / name, fs::directory_options::skip_permission_denied, ec), end;
            for (size_t n = 0; !ec && iterator != end && n < 512; iterator.increment(ec), ++n) {
                const auto path = iterator->path();
                auto label = path.filename().wstring();
                std::transform(label.begin(), label.end(), label.begin(), towlower);
                if (label.find(L"halo") != std::wstring::npos || label.find(L"chelan") != std::wstring::npos) add(path);
            }
        }
        add(fs::path(drive) / L"SteamLibrary" / L"steamapps" / L"common" / L"Halo The Master Chief Collection");
    }
    return results;
}

InstallResult InstallPayload(const fs::path& payload, const GameInstall& game, bool retainConfig) {
    InstallResult result;
    fs::path stage, target, backup;
    struct Written { fs::path relative; bool existed; };
    std::vector<Written> written;
    InstallMutex mutex;
    if (!mutex.owned) { result.message = L"Another Halo MCC VR installation is already running."; return result; }
    try {
        GameInstall verified;
        if (!ProbeInstall(game.root, verified) || _wcsicmp(verified.root.c_str(), game.root.c_str())) throw std::runtime_error("Choose the MCC root containing MCC/Binaries/Win64.");
        if (GameRunning()) throw std::runtime_error("Close both editions of MCC before installing or updating.");
        target = game.root / L"Halo_MCC_VR";
        if (!NoReparse(target) || !NoReparse(payload)) throw std::runtime_error("Installation through a junction or symbolic link is not supported. Choose the actual folder.");
        wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
        if (!_wcsicmp(fs::absolute(self).parent_path().c_str(), fs::absolute(target).c_str()))
            throw std::runtime_error("For an update, run the launcher from the extracted release package, outside Halo_MCC_VR.");
        std::vector<PayloadFile> files; std::string error;
        if (!ParseManifest(Read(payload / L"INSTALL-MANIFEST.sha256"), files, error)) throw std::runtime_error(error);
        // The manifest itself joins the same backup/rollback transaction. It
        // cannot list its own digest, so derive this one from the validated file.
        files.push_back({"INSTALL-MANIFEST.sha256", Sha256File(payload / L"INSTALL-MANIFEST.sha256")});
        const auto suffix = UniqueSuffix();
        stage = target / (L".install-stage-" + suffix);
        backup = target / L"backups" / suffix;
        // Validate the full source and destination sets before the first write.
        for (const auto& file : files) {
            const fs::path relative = Wide(file.relative);
            if (!NoReparse(payload / relative) || !NoReparse(target / relative) || !IsFile(payload / relative)) throw std::runtime_error("Payload file is missing or a path uses a symbolic link.");
            auto expected = file.sha256;
            std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (Sha256File(payload / relative) != expected) throw std::runtime_error("Payload SHA-256 mismatch. Download and extract the release again.");
        }
        if (!NoReparse(backup) || Exists(stage) || Exists(backup)) throw std::runtime_error("Unsafe or conflicting staging/backup folder.");
        fs::create_directories(stage);
        for (const auto& file : files) {
            const fs::path relative = Wide(file.relative);
            fs::create_directories((stage / relative).parent_path());
            fs::copy_file(payload / relative, stage / relative);
            if (Sha256File(payload / relative) != Sha256File(stage / relative)) throw std::runtime_error("Copied payload did not verify.");
        }
        const auto cfg = target / L"halomccvr.cfg";
        const auto oldCfg = IsFile(cfg) ? cfg : target / L"halo3xr.cfg";
        if (retainConfig && IsFile(oldCfg)) {
            if (!NoReparse(oldCfg)) throw std::runtime_error("Existing configuration is a symbolic link.");
            const auto merged = MergeConfig(Read(oldCfg), Read(stage / L"halomccvr.cfg"));
            std::ofstream output(stage / L"halomccvr.cfg", std::ios::binary | std::ios::trunc);
            output.write(merged.text.data(), static_cast<std::streamsize>(merged.text.size()));
            output.close();
            if (!output) throw std::runtime_error("Cannot save the merged configuration.");
            result.addedKeys = merged.addedKeys;
        }
        if (GameRunning()) throw std::runtime_error("MCC started during installation. Close MCC and retry.");
        fs::create_directories(backup);
        for (const auto& file : files) {
            const fs::path relative = Wide(file.relative), destination = target / relative;
            if (!NoReparse(destination)) throw std::runtime_error("Destination changed during installation.");
            const bool existed = Exists(destination);
            if (existed) {
                if (!IsFile(destination)) throw std::runtime_error("A folder conflicts with a payload filename.");
                fs::create_directories((backup / relative).parent_path());
                fs::copy_file(destination, backup / relative);
                if (Sha256File(destination) != Sha256File(backup / relative)) throw std::runtime_error("Backup verification failed; installation stopped.");
            }
            fs::create_directories(destination.parent_path());
            if (!MoveFileExW((stage / relative).c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace a mod file. Close other launchers and check folder write permissions.");
            written.push_back({relative, existed});
        }
        // User logs and custom files stay intact. The manifest describes the
        // distributed defaults; a retained configuration is deliberately custom.
        std::error_code cleanup;
        fs::remove_all(stage, cleanup);
        result.success = true; result.backup = backup;
        result.message = L"Installation complete. " + std::to_wstring(result.addedKeys) + L" new settings added.\nPrevious files: " + backup.wstring();
    } catch (const std::exception& error) {
        bool restored = true;
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            if (it->existed) {
                if (!CopyFileW((backup / it->relative).c_str(), (target / it->relative).c_str(), FALSE)) restored = false;
            } else if (!DeleteFileW((target / it->relative).c_str())) restored = false;
        }
        result.message = Wide(error.what());
        if (!written.empty()) result.message += restored ? L"\nPrevious mod files were restored." : L"\nSome files could not be restored. Keep the backup and restore it before launching.";
        if (!backup.empty() && Exists(backup)) { result.backup = backup; result.message += L"\nBackup: " + backup.wstring(); }
        // A failed stage is intentionally retained for diagnosis; no unchecked
        // recursive cleanup touches any existing user folder.
    }
    return result;
}
} // namespace mcc_installer
