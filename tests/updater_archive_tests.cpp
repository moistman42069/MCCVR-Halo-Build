#include "../src/launcher/updater.h"
#include <fstream>
#include <iostream>
#include <tuple>
namespace fs = std::filesystem;
using Entry = std::tuple<std::string, std::string, uint32_t>;
static void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static void U16(std::string& out, uint16_t n) { out += static_cast<char>(n); out += static_cast<char>(n >> 8); }
static void U32(std::string& out, uint32_t n) { U16(out, static_cast<uint16_t>(n)); U16(out, static_cast<uint16_t>(n >> 16)); }
static uint32_t Crc(const std::string& bytes) { uint32_t crc = ~0u; for (unsigned char c : bytes) { crc ^= c; for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); } return ~crc; }
static void Zip(const fs::path& path, const std::vector<Entry>& entries) {
    std::string zip, central;
    for (const auto& [name, body, attributes] : entries) {
        const auto offset = static_cast<uint32_t>(zip.size()), crc = Crc(body), size = static_cast<uint32_t>(body.size());
        U32(zip, 0x04034B50); U16(zip, 20); U16(zip, 0); U16(zip, 0); U16(zip, 0); U16(zip, 0); U32(zip, crc); U32(zip, size); U32(zip, size); U16(zip, static_cast<uint16_t>(name.size())); U16(zip, 0); zip += name; zip += body;
        U32(central, 0x02014B50); U16(central, 0x0314); U16(central, 20); U16(central, 0); U16(central, 0); U16(central, 0); U16(central, 0); U32(central, crc); U32(central, size); U32(central, size); U16(central, static_cast<uint16_t>(name.size())); U16(central, 0); U16(central, 0); U16(central, 0); U16(central, 0); U32(central, attributes); U32(central, offset); central += name;
    }
    const auto offset = static_cast<uint32_t>(zip.size()); zip += central;
    U32(zip, 0x06054B50); U16(zip, 0); U16(zip, 0); U16(zip, static_cast<uint16_t>(entries.size())); U16(zip, static_cast<uint16_t>(entries.size())); U32(zip, static_cast<uint32_t>(central.size())); U32(zip, offset); U16(zip, 0);
    std::ofstream file(path, std::ios::binary); file.write(zip.data(), static_cast<std::streamsize>(zip.size())); file.close(); Require(static_cast<bool>(file), "ZIP fixture write");
}
int main() {
    fs::path root;
    try {
        root = fs::temp_directory_path() / (L"HaloMCCVR-archive-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Require(!fs::exists(root), "unique archive test folder"); fs::create_directories(root);
        Zip(root / "good.zip", {{"ModFiles/assets/example.txt", "safe payload", 0}});
        mcc_installer::ExtractUpdateArchive(root / "good.zip", root / "good");
        std::ifstream file(root / "good/ModFiles/assets/example.txt"); std::string value; std::getline(file, value); Require(value == "safe payload", "valid archive extracted"); file.close();
        const std::vector<std::vector<Entry>> malicious = {
            {{"../escaped.txt", "bad", 0}}, {{"/absolute.txt", "bad", 0}}, {{"C:\\escape.txt", "bad", 0}},
            {{"same.txt", "one", 0}, {"SAME.txt", "two", 0}}, {{"link", "../outside", 0xA0000000}},
            {{"assets/NUL.txt", "bad", 0}}, {{"assets/..\\escaped.txt", "bad", 0}}};
        for (size_t i = 0; i < malicious.size(); ++i) {
            const auto archive = root / ("bad" + std::to_string(i) + ".zip"), output = root / ("bad" + std::to_string(i));
            Zip(archive, malicious[i]); bool rejected = false;
            try { mcc_installer::ExtractUpdateArchive(archive, output); } catch (...) { rejected = true; }
            Require(rejected && !fs::exists(output), "unsafe archive rejected before extraction");
        }
        Require(!fs::exists(root / "escaped.txt"), "no traversal writes");
        Require(fs::equivalent(root.parent_path(), fs::temp_directory_path()) && root.filename().wstring().starts_with(L"HaloMCCVR-archive-test-"), "owned cleanup boundary");
        fs::remove_all(root);
        std::cout << "Updater archive extraction, traversal, absolute path, duplicate, reserved-name and symlink tests passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\nFixture: " << root << '\n'; return 1; }
}
