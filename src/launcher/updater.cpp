#include "updater.h"
#include <winhttp.h>
#include <shlobj.h>
#include <fstream>
#include <array>
#include <algorithm>
#include <set>
#include <miniz.h>
#pragma comment(lib, "winhttp.lib")
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
std::string Request(const std::wstring& url, const fs::path& destination, uint64_t limit, int seconds = 30) {
    URL_COMPONENTS components{sizeof(components)};
    components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS) throw std::runtime_error("Update URL must use HTTPS.");
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    HttpHandle session{WinHttpOpen(L"HaloMCCVRLauncher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) throw std::runtime_error("Windows could not open the update connection.");
    WinHttpSetTimeouts(session.value, std::min(seconds, 10) * 1000, std::min(seconds, 10) * 1000, seconds * 1000, seconds * 1000);
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
struct ZipReader {
    mz_zip_archive zip{}; FILE* file = nullptr; bool open = false;
    ~ZipReader() { if (open) mz_zip_reader_end(&zip); if (file) fclose(file); }
};
// The release ZIP is read here, in this process, with miniz: no other program is started to do it. Every entry is
// checked before the first file is written -- paths that leave the folder or name a drive, names Windows cannot keep
// apart or reserves, symbolic links, encryption and sizes past the limits -- and each file's CRC is checked as it is
// written. Nothing from the archive is run or loaded.
void ExtractArchive(const fs::path& archive, const fs::path& output) {
    ZipReader reader;
    if (_wfopen_s(&reader.file, archive.c_str(), L"rb") || !reader.file) throw std::runtime_error("Cannot open the release ZIP.");
    std::error_code sizeError; const auto archiveSize = fs::file_size(archive, sizeError);
    if (sizeError || !mz_zip_reader_init_cfile(&reader.zip, reader.file, archiveSize, 0)) throw std::runtime_error("The release ZIP is not a readable ZIP file.");
    reader.open = true;
    const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
    if (count > 4096) throw std::runtime_error("Too many ZIP entries.");
    wchar_t baseBuffer[MAX_PATH * 4]{};
    if (!GetFullPathNameW(output.c_str(), static_cast<DWORD>(std::size(baseBuffer)), baseBuffer, nullptr)) throw std::runtime_error("Unsafe ZIP destination.");
    std::wstring base = baseBuffer;
    while (!base.empty() && base.back() == L'\\') base.pop_back();
    base += L'\\';
    const auto folded = [](std::wstring text) { CharLowerBuffW(text.data(), static_cast<DWORD>(text.size())); return text; };
    const std::wstring baseFolded = folded(base);
    struct Planned { mz_uint index; std::wstring target; bool directory; };
    std::vector<Planned> plan; std::set<std::wstring> seen; uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&reader.zip, i, &stat) || !mz_zip_reader_is_file_supported(&reader.zip, i) || stat.m_is_encrypted)
            throw std::runtime_error("Unsupported ZIP entry.");
        const std::string utf8 = stat.m_filename;
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring name(length > 0 ? length : 0, 0);
        if (length <= 0 || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), name.data(), length))
            throw std::runtime_error("Unsafe ZIP path.");
        std::replace(name.begin(), name.end(), L'/', L'\\');
        if (name.empty() || name.size() > 240 || name.front() == L'\\' ||
            std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 0x20 || wcschr(L":<>\"|?*", c); }))
            throw std::runtime_error("Unsafe ZIP path.");
        std::wstring trimmed = name;
        while (!trimmed.empty() && trimmed.back() == L'\\') trimmed.pop_back();
        for (size_t at = 0; at <= trimmed.size();) {
            const size_t next = std::min(trimmed.find(L'\\', at), trimmed.size());
            const std::wstring part = trimmed.substr(at, next - at), upper = [&] { std::wstring u = part; CharUpperBuffW(u.data(), static_cast<DWORD>(u.size())); return u; }();
            const std::wstring stem = upper.substr(0, upper.find(L'.'));
            const bool reserved = stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
                (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && iswdigit(stem[3]));
            if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ' || reserved)
                throw std::runtime_error("Unsafe ZIP component.");
            at = next + 1;
        }
        wchar_t full[MAX_PATH * 4]{};
        if (!GetFullPathNameW((base + name).c_str(), static_cast<DWORD>(std::size(full)), full, nullptr)) throw std::runtime_error("Unsafe ZIP path.");
        const std::wstring target = full, targetFolded = folded(target);
        if (!targetFolded.starts_with(baseFolded) || !seen.insert(targetFolded).second) throw std::runtime_error("Conflicting ZIP path.");
        if (((stat.m_external_attr >> 16) & 0xF000) == 0xA000) throw std::runtime_error("ZIP symlink is not allowed.");
        total += stat.m_uncomp_size;
        if (total > 1073741824ull || stat.m_uncomp_size > 536870912ull) throw std::runtime_error("ZIP expanded size exceeded.");
        plan.push_back({i, target, stat.m_is_directory != 0 || name.back() == L'\\'});
    }
    fs::create_directories(base);
    for (const auto& entry : plan) {
        if (entry.directory) { fs::create_directories(entry.target); continue; }
        fs::create_directories(fs::path(entry.target).parent_path());
        if (fs::exists(entry.target)) throw std::runtime_error("Conflicting ZIP path.");
        std::ofstream file(entry.target, std::ios::binary);
        if (!file) { ThrowIfAntivirus(entry.target, true); throw std::runtime_error("Cannot write an extracted file."); }
        const auto write = [](void* opaque, mz_uint64, const void* data, size_t size) -> size_t {
            auto& out = *static_cast<std::ofstream*>(opaque);
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            return out ? size : 0;
        };
        if (!mz_zip_reader_extract_to_callback(&reader.zip, entry.index, write, &file, 0)) throw std::runtime_error("A ZIP entry is damaged.");
        file.close();
        if (!file) { ThrowIfAntivirus(entry.target); throw std::runtime_error("Cannot finish an extracted file."); }
    }
}
#if HALOMCCVR_DEV_DIAGNOSTICS
// DEV BUILDS ONLY, never packaged. HALOMCCVR_DEV_RELEASES names a folder that stands in for GitHub, so an update can be
// rehearsed end to end without a public release: its releases.json is read as every repository's release list, and an
// asset is copied from the file of its name there instead of downloaded. Everything after is the real update.
fs::path DevReleases() {
    wchar_t value[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"HALOMCCVR_DEV_RELEASES", value, MAX_PATH);
    return length && length < MAX_PATH ? fs::path(value) : fs::path{};
}
#endif
}

std::vector<ReleaseUpdate> ListReleases(const ReleaseChannel& channel) {
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (const auto dev = DevReleases(); !dev.empty()) {
        std::ifstream file(dev / L"releases.json", std::ios::binary);
        return ParseReleaseList({std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}, channel);
    }
#endif
    const std::wstring repository = L"https://api.github.com/repos/" + Widen(channel.owner) + L"/MCCVR-Halo-Build/releases";
    return ParseReleaseList(Request(repository + L"?per_page=30", {}, 2 * 1024 * 1024), channel);
}
bool HasTroubleshootingPage(const ReleaseChannel& channel) {
    try { Request(L"https://api.github.com/repos/" + Widen(channel.owner) + L"/MCCVR-Halo-Build/contents/TROUBLESHOOTING.md", {}, 2 * 1024 * 1024, 5); return true; }
    catch (const std::exception&) { return false; }
}
std::vector<ReleaseUpdate> ListLatestAcrossChannels(size_t& channel, std::wstring& passedOver) {
    std::vector<std::vector<ReleaseUpdate>> lists(std::size(kReleaseChannels));
    std::vector<std::string> dates(lists.size());
    passedOver.clear();
    for (size_t i = 0; i < lists.size(); ++i) {
        try { lists[i] = ListReleases(kReleaseChannels[i]); dates[i] = lists[i].front().published; }
        catch (const std::exception&) { passedOver += (passedOver.empty() ? L"" : L", "); passedOver += kReleaseChannels[i].label; }
    }
    channel = NewestPublished(dates);
    if (channel == std::string::npos) throw std::runtime_error("No repository could be read. Check your connection, or open a release page for manual installation.");
    return lists[channel];
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
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (const auto dev = DevReleases(); !dev.empty()) fs::copy_file(dev / Widen(release.assetName), archive);
    else
#endif
    Request(Widen(release.url), archive, release.bytes);
    if (fs::file_size(archive) != release.bytes || Sha256File(archive) != release.sha256) throw std::runtime_error("Downloaded ZIP did not match GitHub's SHA-256/size. It will not be installed.");
    const auto unpacked = cache / L"package";
    ExtractUpdateArchive(archive, unpacked);
    const auto payload = unpacked / L"ModFiles";
    if (!fs::is_regular_file(payload / L"INSTALL-MANIFEST.sha256"))
        throw std::runtime_error("This release predates automatic updates. The verified ZIP is in the local update cache; use its manual instructions.");
    return payload;
}
} // namespace mcc_installer
