#include "updater.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <fstream>
#include <array>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace mcc_installer {
namespace fs = std::filesystem;
namespace {
std::wstring Widen(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size && !value.empty()) throw std::runtime_error("Invalid UTF-8 release information.");
    std::wstring text(size, 0); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), text.data(), size); return text;
}
struct HttpHandle { HINTERNET value{}; ~HttpHandle() { if (value) WinHttpCloseHandle(value); } };
std::string Request(const std::wstring& url, const fs::path& destination, uint64_t limit) {
    URL_COMPONENTS components{sizeof(components)};
    components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS) throw std::runtime_error("Update URL must use HTTPS.");
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    HttpHandle session{WinHttpOpen(L"HaloMCCVRLauncher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) throw std::runtime_error("Windows could not open the update connection.");
    WinHttpSetTimeouts(session.value, 10000, 10000, 30000, 30000);
    HttpHandle connection{WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    HttpHandle request{connection.value ? WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
    if (!request.value) throw std::runtime_error("Cannot create update request.");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("The update connection failed. Check your connection and retry.");
    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error("GitHub did not return the requested release. It may be temporarily unavailable or rate limited.");
    std::wstring finalUrl(32768, 0); DWORD finalSize = static_cast<DWORD>(finalUrl.size() * sizeof(wchar_t));
    if (!WinHttpQueryOption(request.value, WINHTTP_OPTION_URL, finalUrl.data(), &finalSize)) throw std::runtime_error("Could not verify the download location.");
    finalUrl.resize(wcslen(finalUrl.c_str()));
    URL_COMPONENTS finalParts{sizeof(finalParts)}; finalParts.dwHostNameLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(finalUrl.c_str(), 0, 0, &finalParts) || finalParts.nScheme != INTERNET_SCHEME_HTTPS) throw std::runtime_error("Unsafe update redirect.");
    const std::wstring finalHost(finalParts.lpszHostName, finalParts.dwHostNameLength);
    if (finalHost != L"api.github.com" && finalHost != L"github.com" && finalHost != L"release-assets.githubusercontent.com" && finalHost != L"objects.githubusercontent.com")
        throw std::runtime_error("Update redirected outside GitHub's release storage.");
    std::ofstream file;
    if (!destination.empty()) { file.open(destination, std::ios::binary | std::ios::trunc); if (!file) throw std::runtime_error("Cannot create the update download."); }
    std::string text; uint64_t total = 0; std::array<char, 65536> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) throw std::runtime_error("The update download was interrupted.");
        if (!read) break;
        total += read;
        if (total > limit) throw std::runtime_error("Update exceeds its expected size.");
        if (destination.empty()) text.append(buffer.data(), read); else { file.write(buffer.data(), read); if (!file) throw std::runtime_error("Cannot write the update download."); }
    }
    if (!destination.empty()) { file.close(); if (!file) throw std::runtime_error("Cannot finish the update download."); }
    return text;
}
std::wstring QuotePowerShell(const fs::path& path) {
    std::wstring escaped = L"'";
    for (wchar_t c : path.wstring()) { escaped += c; if (c == L'\'') escaped += c; }
    return escaped + L"'";
}
std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring result = L"\""; unsigned slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
void ExtractArchive(const fs::path& archive, const fs::path& output) {
    // The built-in .NET ZIP reader validates every entry before extracting any
    // file. Reject traversal, duplicate Windows paths, symlinks and zip bombs.
    // Nothing from the archive is invoked or imported as code.
    const std::wstring script = LR"($ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=)" + QuotePowerShell(archive) + L"\n$root=" + QuotePowerShell(output) + LR"(
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try {
  $base=[IO.Path]::GetFullPath($root).TrimEnd('\')+'\'
  $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  [long]$total=0
  if($zip.Entries.Count -gt 4096){throw 'Too many ZIP entries'}
  foreach($entry in $zip.Entries){
    $name=$entry.FullName.Replace('/','\')
    if(!$name -or $name.Length -gt 240 -or $name -match '(^\\|:|[<>"|?*\x00-\x1f])'){throw 'Unsafe ZIP path'}
    foreach($part in $name.TrimEnd('\').Split('\')){if(!$part -or $part -eq '.' -or $part -eq '..' -or $part -match '[. ]$' -or $part -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)'){throw 'Unsafe ZIP component'}}
    $target=[IO.Path]::GetFullPath([IO.Path]::Combine($base,$name))
    if(!$target.StartsWith($base,[StringComparison]::OrdinalIgnoreCase) -or !$seen.Add($target)){throw 'Conflicting ZIP path'}
    if((($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960){throw 'ZIP symlink is not allowed'}
    $total+=$entry.Length
    if($total -gt 1073741824 -or $entry.Length -gt 536870912){throw 'ZIP expanded size exceeded'}
  }
  [IO.Directory]::CreateDirectory($base)|Out-Null
  foreach($entry in $zip.Entries){
    $target=[IO.Path]::GetFullPath([IO.Path]::Combine($base,$entry.FullName.Replace('/','\')))
    if(!$entry.Name){[IO.Directory]::CreateDirectory($target)|Out-Null;continue}
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
  }
}finally{$zip.Dispose()}
)";
    DWORD length = 0;
    if (!CryptBinaryToStringW(reinterpret_cast<const BYTE*>(script.data()), static_cast<DWORD>(script.size() * sizeof(wchar_t)), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length)) throw std::runtime_error("Could not prepare archive extraction.");
    std::wstring encoded(length, 0);
    CryptBinaryToStringW(reinterpret_cast<const BYTE*>(script.data()), static_cast<DWORD>(script.size() * sizeof(wchar_t)), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &length);
    encoded.resize(wcslen(encoded.c_str()));
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
    const fs::path powershell = fs::path(system) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    auto command = QuoteArgument(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -EncodedCommand " + encoded;
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) throw std::runtime_error("Windows archive extraction is unavailable.");
    CloseHandle(process.hThread);
    const auto waited = WaitForSingleObject(process.hProcess, 120000);
    DWORD code = 1;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
    CloseHandle(process.hProcess);
    if (code) throw std::runtime_error("The release ZIP could not be safely extracted. Keep the downloaded ZIP and install manually if needed.");
}
}

ReleaseUpdate CheckForUpdate() {
    return ParseReleaseUpdate(Request(L"https://api.github.com/repos/moistman42069/MCCVR-Halo-Build/releases/latest", {}, 2 * 1024 * 1024));
}
void ExtractUpdateArchive(const fs::path& archive, const fs::path& output) {
    if (fs::exists(output)) throw std::runtime_error("Update extraction folder must be new.");
    ExtractArchive(archive, output);
}
fs::path DownloadUpdate(const ReleaseUpdate& release) {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) throw std::runtime_error("Cannot locate the update cache.");
    fs::path cache = fs::path(local) / L"HaloMCCVR" / L"Updates"; CoTaskMemFree(local);
    cache /= std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (fs::exists(cache)) throw std::runtime_error("Update cache already exists; retry.");
    fs::create_directories(cache);
    const auto archive = cache / L"release.zip";
    Request(Widen(release.url), archive, release.bytes);
    if (fs::file_size(archive) != release.bytes || Sha256File(archive) != release.sha256) throw std::runtime_error("Downloaded ZIP did not match GitHub's SHA-256/size. It will not be installed.");
    const auto unpacked = cache / L"package";
    ExtractUpdateArchive(archive, unpacked);
    const auto payload = unpacked / L"ModFiles";
    if (!fs::is_regular_file(payload / L"INSTALL-MANIFEST.sha256"))
        throw std::runtime_error("This release predates automatic updates. The verified ZIP is in the local update cache; use its manual instructions.");
    return payload;
}
bool StartUpdateHelper(const fs::path& payload, const GameInstall& game, bool retainConfig, std::wstring& error) {
    try {
        wchar_t self[32768]{}; GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
        // Run this already-trusted installer, not an arbitrary executable from
        // the downloaded archive. It waits for the current launcher to exit.
        const auto helper = payload.parent_path().parent_path() / L"HaloMCCVRUpdateHelper.exe";
        fs::copy_file(self, helper, fs::copy_options::overwrite_existing);
        if (Sha256File(self) != Sha256File(helper)) throw std::runtime_error("Update helper verification failed.");
        auto command = QuoteArgument(helper.wstring()) + L" --apply-update " + QuoteArgument(payload.wstring()) + L" " + QuoteArgument(game.root.wstring()) +
            (retainConfig ? L" 1 " : L" 0 ") + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, helper.parent_path().c_str(), &startup, &process)) throw std::runtime_error("Could not start the update installer.");
        CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
    } catch (const std::exception& exception) { error = Widen(exception.what()); return false; }
}
} // namespace mcc_installer
