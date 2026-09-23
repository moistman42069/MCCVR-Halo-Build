#include "../src/common/installer_policy.h"
#include "../src/common/release_update_policy.h"
#include "../src/launcher/installer.h"
#include "../src/launcher/updater.h"
#include <fstream>
#include <iostream>

using namespace mcc_installer;
namespace fs = std::filesystem;
static void Require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
static void Write(const fs::path& path, const std::string& value) { fs::create_directories(path.parent_path()); std::ofstream file(path, std::ios::binary); file << value; file.close(); Require(static_cast<bool>(file), "fixture write"); }
static std::string Read(const fs::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}; }
static void Manifest(const fs::path& source) {
    std::string text;
    for (const auto name : {"HaloMCCVR.dll", "HaloMCCVRLauncher.exe", "halomccvr.cfg", "LICENSE", "assets/fixture.txt"}) text += Sha256File(source / name) + "  " + name + "\n";
    Write(source / "INSTALL-MANIFEST.sha256", text);
}
static void Policies() {
    auto merged = MergeConfig("# player's notes\r\nconfig_version = 5\r\nleft_handed = 1\r\ncustom_unknown = abc", "config_version = 6\nleft_handed = 0\nnew_option = 1\nnew_option = 0\n");
    Require(merged.addedKeys == 1 && merged.text.starts_with("# player's notes\r\nconfig_version = 5\r\nleft_handed = 1\r\ncustom_unknown = abc"), "preserve settings/notes/unknown keys");
    Require(merged.text.find("new_option = 1") != std::string::npos && merged.text.find("new_option = 0") == std::string::npos, "append missing defaults once");
    Require(MergeConfig(merged.text, "new_option = 0\nleft_handed=0\n").text == merged.text, "idempotent merge");
    auto legacy = MergeConfig("hud_height=0.4\nweapon_body_zone_radius_m=0.25\n", "config_version=5\nflashlight_mapping_version=2\nhud_curvature=0.5\nscope_zoom=12\nweapon_holster_radius_m=0.1\nnew=1\n");
    Require(legacy.addedKeys == 1 && legacy.text.find("config_version") == std::string::npos && legacy.text.find("scope_zoom") == std::string::npos, "legacy migrations must still run");
    auto explicitValues = MergeConfig("config_version=5\nbinding_jump_halo3=-1 # unbound\n", "binding_jump_halo3=3\nbinding_jump_halo2=3\n");
    Require(explicitValues.text.find("binding_jump_halo3=-1") != std::string::npos && explicitValues.addedKeys == 1, "per-game explicit Unbound preserved");
    for (const auto unsafe : {"../HaloMCCVR.dll", "C:\\file", "\\server\\share", "assets/../../bad", "a//b", "assets/NUL.txt", "a/COM1", "a. /file", "a ", "a:stream", "a\\..\\b"}) Require(!SafeRelativePath(unsafe), "reject unsafe relative path");
    Require(SafeRelativePath("assets/weapons/mesh.bin"), "normal asset relative path");
    const std::string hash(64, 'a'); std::vector<PayloadFile> files; std::string error;
    const std::string valid = hash + "  HaloMCCVR.dll\n" + hash + "  HaloMCCVRLauncher.exe\n" + hash + "  halomccvr.cfg\n";
    Require(ParseManifest(valid, files, error) && files.size() == 3, "valid payload manifest");
    Require(!ParseManifest(valid + hash + "  HALOMCCVR.DLL\n", files, error), "case-insensitive duplicate rejected");
    Require(!ParseManifest(valid + hash + "  ../escape\n", files, error), "manifest traversal rejected");
    Require(!ParseManifest(hash + "  HaloMCCVR.dll\n", files, error), "partial payload rejected");
    const auto libraries = VdfValues("\"libraryfolders\" { \"0\" { \"path\" \"C:\\\\Steam\" } \"1\" { \"path\" \"D:\\\\Steam Library\" } }", "path");
    Require(libraries.size() == 2 && libraries[1] == "D:\\Steam Library", "Valve library escaping");
    const auto json = R"({"tag_name":"MCC_VR_ALPHA_0.6.0","draft":false,"prerelease":false,"assets":[{"name":"HaloMCCVR-abcdef0-Build.zip","browser_download_url":"https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/0.6/build.zip","digest":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":100},{"name":"source.zip","size":99}]})";
    const auto release = ParseReleaseUpdate(json);
    Require(release.bytes == 100 && release.tag == "MCC_VR_ALPHA_0.6.0", "release parser chooses player ZIP");
    for (const auto input : {R"({"tag_name":"x","draft":false,"prerelease":false,"assets":[]})", R"({"a":1,"a":2})", R"([1,])", R"({"a":"\uD800"})", R"({"a":01})"}) {
        bool threw = false; try { ParseReleaseUpdate(input); } catch (...) { threw = true; } Require(threw, "invalid release JSON rejected");
    }
    auto badDigest = std::string(json); badDigest.replace(badDigest.find("sha256:"), 7, "sha512:");
    bool threw = false; try { ParseReleaseUpdate(badDigest); } catch (...) { threw = true; } Require(threw, "unknown digest rejected");
    auto foreign = std::string(json); foreign.replace(foreign.find("github.com/moistman42069"), 22, "github.com/another-user");
    threw = false; try { ParseReleaseUpdate(foreign); } catch (...) { threw = true; } Require(threw, "foreign release URL rejected");
}
static void VerifyPackagedPayload(const fs::path& source,const fs::path& sandbox) {
    std::vector<PayloadFile> files;std::string error;
    Require(ParseManifest(Read(source / "INSTALL-MANIFEST.sha256"),files,error),"packaged manifest parses");
    const auto identity=Read(source / "BUILD-IDENTITY.txt");
    Require(identity.find("source_commit=")!=std::string::npos&&identity.find("build_kind=")!=std::string::npos&&
        identity.find("release_tag=")!=std::string::npos,"packaged source identity is present");
    for(const auto name:{"BUILD-IDENTITY.txt","assets/fonts/Oxanium.ttf","assets/fonts/OFL-Oxanium.txt","assets/fonts/SOURCE.txt"})
        Require(fs::is_regular_file(source/name),"packaged identity and licensed font contract");
    if(fs::is_regular_file(source.parent_path()/"HaloMCCVRLauncher.exe"))
        Require(Sha256File(source.parent_path()/"HaloMCCVRLauncher.exe")==Sha256File(source/"HaloMCCVRLauncher.exe"),
            "root and manual payload launcher bytes agree");
    size_t count=0;
    for(const auto& item:fs::recursive_directory_iterator(source))
        if(item.is_regular_file()&&item.path().filename()!=L"INSTALL-MANIFEST.sha256") ++count;
    Require(count==files.size(),"every distributed payload file is listed in its manifest");
    for(bool store:{false,true}) {
        const auto gameRoot=sandbox/(store?"Store Content":"Steam MCC");
        Write(gameRoot/(store?"MCC/Binaries/Win64/MCCWinStore-Win64-Shipping.exe":"MCC/Binaries/Win64/MCC-Win64-Shipping.exe"),
            "Synthetic test executable: never run.");
        GameInstall game;Require(ProbeInstall(gameRoot,game)&&game.store==store,"staged payload synthetic edition detection");
        auto installed=InstallPayload(source,game,true);
        Require(installed.success,"staged payload installs into synthetic game folder");
        const auto target=gameRoot/"Halo_MCC_VR";
        for(const auto& item:files) Require(Sha256File(target/fs::path(item.relative))==Sha256File(source/fs::path(item.relative)),
            "all installed payload bytes match complete package");
        Write(target/"halomccvr.cfg","config_version=5\n# preserved player note\nvr_bind_halo3_crouch=1\n");
        auto updated=InstallPayload(source,game,true);
        Require(updated.success&&Read(target/"halomccvr.cfg").find("vr_bind_halo3_crouch=1")!=std::string::npos&&
            Read(updated.backup/"halomccvr.cfg").find("# preserved player note")!=std::string::npos,
            "complete packaged update preserves explicit Unbound and backup");
    }
}
int wmain(int argc,wchar_t** argv) {
    fs::path sandbox;
    try {
        Policies();
        sandbox = fs::temp_directory_path() / (L"HaloMCCVR-installer-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Require(!fs::exists(sandbox), "unique synthetic fixture directory");
        if(argc==3&&std::wstring(argv[1])==L"--verify-payload") {
            VerifyPackagedPayload(fs::path(argv[2]),sandbox);
            Require(fs::equivalent(sandbox.parent_path(),fs::temp_directory_path())&&sandbox.filename().wstring().starts_with(L"HaloMCCVR-installer-test-"),"owned packaged fixture cleanup");
            fs::remove_all(sandbox);
            std::cout<<"Complete packaged payload verified in synthetic Steam and Store installs, including cfg retention and licensed font layout.\n";
            return 0;
        }
        const auto source = sandbox / "package" / "ModFiles", steam = sandbox / "Steam MCC", store = sandbox / "Xbox MCC" / "Content";
        Write(steam / "MCC/Binaries/Win64/MCC-Win64-Shipping.exe", "fake game, never executed");
        Write(store / "MCC/Binaries/Win64/MCCWinStore-Win64-Shipping.exe", "fake store game, never executed");
        Write(store / "MicrosoftGame.config", "fixture");
        GameInstall game, xbox;
        Require(ProbeInstall(steam, game) && !game.store, "Steam layout detection");
        Require(ProbeInstall(store.parent_path(), xbox) && xbox.store && xbox.root == store, "Xbox Content layout detection");
        GameInstall nested;
        Require(ProbeInstall(steam / "MCC/Binaries/Win64", nested) && nested.root == steam, "browse a child folder");
        for(const auto& gameRoot:{steam,store}) {
            for(const auto& browse:{gameRoot,gameRoot/"MCC",gameRoot/"MCC/Binaries",gameRoot/"MCC/Binaries/Win64",
                gameRoot/"Halo_MCC_VR",gameRoot/"Halo_MCC_VR/ModFiles"}) {
                GameInstall resolved;
                Require(ProbeInstall(browse,resolved)&&resolved.root==gameRoot,
                    "root/bin/already-installed-folder browsing always resolves to actual game root");
                Require((resolved.root/"Halo_MCC_VR").parent_path()==gameRoot,
                    "VR destination is a direct child of game root for either edition");
            }
        }
        Write(source / "HaloMCCVR.dll", "new DLL fixture"); Write(source / "HaloMCCVRLauncher.exe", "new launcher fixture");
        Write(source / "halomccvr.cfg", "config_version=5\nleft_handed=0\nnew_feature=1\n"); Write(source / "LICENSE", "license fixture"); Write(source / "assets/fixture.txt", "asset fixture"); Manifest(source);
        auto first = InstallPayload(source, game, true);
        Require(first.success, "first synthetic install");
        const auto target = steam / "Halo_MCC_VR";
        Require(Read(target / "HaloMCCVR.dll") == "new DLL fixture" && Read(target / "assets/fixture.txt") == "asset fixture", "binaries and nested assets installed");
        Require(!fs::exists(steam/"MCC/Binaries/Win64/Halo_MCC_VR")&&!fs::exists(target/"Halo_MCC_VR")&&
            !fs::exists(steam.parent_path()/"Halo_MCC_VR"),"installer never creates binaries, double-nested, or sibling mod folders");
        GameInstall mistaken{steam/"MCC/Binaries/Win64",false};
        Require(!InstallPayload(source,mistaken,true).success&&!fs::exists(mistaken.root/"Halo_MCC_VR"),
            "installer refuses an unnormalized game root even if directly supplied by a caller");
        GameInstall selectedInstalled;
        Require(ProbeInstall(target,selectedInstalled)&&InstallPayload(source,selectedInstalled,true).success&&
            !fs::exists(target/"Halo_MCC_VR"),"updating after browsing existing mod folder cannot double nest");
        Write(target / "halomccvr.cfg", "config_version=5\nleft_handed=1\n# keep my note\n"); Write(target / "my-custom.txt", "keep custom file");
        Write(source / "HaloMCCVR.dll", "updated DLL fixture"); Manifest(source);
        auto update = InstallPayload(source, game, true);
        Require(update.success && update.addedKeys == 1 && Read(target / "halomccvr.cfg").find("left_handed=1") != std::string::npos, "retain and merge existing configuration");
        Require(Read(update.backup / "HaloMCCVR.dll") == "new DLL fixture" && Read(target / "my-custom.txt") == "keep custom file", "backup and unrelated files preserved");
        Write(source / "HaloMCCVR.dll", "corrupted after manifest");
        auto corrupt = InstallPayload(source, game, true);
        Require(!corrupt.success && Read(target / "HaloMCCVR.dll") == "updated DLL fixture", "hash mismatch leaves installed files unchanged");
        Manifest(source);
        HANDLE locked = CreateFileW((target / "HaloMCCVRLauncher.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "lock second destination for rollback fixture");
        auto failed = InstallPayload(source, game, true); CloseHandle(locked);
        Require(!failed.success && Read(target / "HaloMCCVR.dll") == "updated DLL fixture", "later replacement failure rolls back earlier DLL write");
        auto defaults = InstallPayload(source, game, false);
        Require(defaults.success && Read(target / "halomccvr.cfg") == Read(source / "halomccvr.cfg"), "explicit reset to defaults");
        auto storeInstall = InstallPayload(source, xbox, true);
        Require(storeInstall.success && fs::is_regular_file(store / "Halo_MCC_VR/HaloMCCVR.dll"), "Store synthetic install");
        Require(!fs::exists(store/"MCC/Binaries/Win64/Halo_MCC_VR")&&!fs::exists(store/"Halo_MCC_VR/Halo_MCC_VR")&&
            !fs::exists(store.parent_path()/"Halo_MCC_VR"),"Store Content root remains the sole direct-parent destination");
        // This test owns only its unique directory directly under temp.
        Require(fs::equivalent(sandbox.parent_path(), fs::temp_directory_path()) && sandbox.filename().wstring().starts_with(L"HaloMCCVR-installer-test-"), "safe fixture cleanup boundary");
        fs::remove_all(sandbox);
        std::cout << "Installer policy, release parsing, detection, SHA-256, cfg merge, backup and rollback tests passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\nFixture retained at: " << sandbox << '\n'; return 1; }
}
