// The vehicle camera file on disk (src/common/vehicle_camera_store.h): written once when missing, never
// written over, read again when it changes, and a wrong file leaving the cameras in use as they were.
// Works in a folder of its own under the temp folder and removes it afterwards.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "../src/common/vehicle_camera_store.h"

using namespace vehicle_camera_file;
using namespace vehicle_camera_store;

static unsigned g_checks = 0;
static std::filesystem::path g_folder;

static void RemoveFolder()
{
    std::error_code ignored;
    if (!g_folder.empty())
        std::filesystem::remove_all(g_folder, ignored);
}

static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        RemoveFolder();
        std::exit(1);
    }
}

static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

static bool AnyNoteHas(const PollResult& result, const char* part)
{
    for (const std::string& note : result.notes)
        if (Has(note, part))
            return true;
    return false;
}

static std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void WriteFileText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// One Halo 3 Warthog camera at the given distance behind; `padding` makes each version a different size, so a
// rewrite inside one tick of the file clock is still seen.
static std::string Halo3File(int behindCm, const char* padding)
{
    return std::string("{ \"_note\": \"") + padding +
           "\", \"games\": { \"halo3\": { \"vehicles\": { \"Warthog\": { \"cameras\": ["
           " { \"name\": \"Behind\", \"offset\": [" +
           std::to_string(behindCm) + ", 0, 180] } ] } } } } }";
}

static float Halo3Behind(const Store& store)
{
    const Game* game = store.Current().FindGame("halo3");
    if (!game || game->key != "halo3" || game->vehicles.empty() || game->vehicles[0].cameras.empty())
        return 0.0f;
    return game->vehicles[0].cameras[0].offset[0];
}

static void TestMissingFileIsWrittenOnce()
{
    const std::filesystem::path path = g_folder / kFileName;
    Store store(path.wstring());
    Check(!store.FromFile(), "before the first look the built-in table is in use");
    Check(store.Current().FindGame("halo3") != nullptr, "and it has a section every game falls back to");

    const PollResult first = store.Poll();
    Check(first.created && first.changed, "a missing file is written and read");
    Check(std::filesystem::exists(path), "the file is there afterwards");
    Check(store.FromFile(), "and the table in use is the file's");
    std::size_t entries = 0, cameras = 0;
    for (const auto& game : vehicle_camera_file::DefaultTable().games)
        for (const auto& vehicle : game.vehicles)
        {
            ++entries;
            cameras += vehicle.cameras.size();
        }
    const std::string counted =
        "read " + std::to_string(entries) + " vehicle entries and " + std::to_string(cameras) + " cameras";
    Check(AnyNoteHas(first, "built-in cameras were written") && AnyNoteHas(first, counted.c_str()),
          "the log is told of both");
    const std::string written = ReadFileText(path);
    Check(Has(written, "\"games\"") && Has(written, "Third Person"), "the file holds the built-in cameras");
    Check(Has(written, "\r\n") && !Has(written, "\r\r"), "with the line ends Windows editors expect");
    Table reread;
    Check(FromJson(written.c_str(), written.size(), reread).ok, "and it reads back as a camera file");

    const PollResult second = store.Poll();
    Check(!second.changed && !second.created && second.notes.empty(), "an unchanged file is left alone, silently");
    Check(ReadFileText(path) == written, "and is not written to");
}

static void TestEditsAreFollowed()
{
    const std::filesystem::path path = g_folder / L"edits.json";
    WriteFileText(path, Halo3File(-450, "a"));
    Store store(path.wstring());
    const PollResult first = store.Poll();
    Check(!first.created && first.changed, "a file that is already there is read, not written");
    Check(ReadFileText(path) == Halo3File(-450, "a"), "and its contents are untouched");
    Check(Halo3Behind(store) == -450.0f, "its cameras are the ones in use");

    WriteFileText(path, Halo3File(-600, "bb"));
    const PollResult edited = store.Poll();
    Check(edited.changed && Halo3Behind(store) == -600.0f, "an edit is read on the next look");
    Check(AnyNoteHas(edited, "read 1 vehicle entries and 1 cameras"), "and the log is told");

    WriteFileText(path, "{ \"games\": { \"halo3\": { \"vehicles\": { \"Warthog\": { \"cameras\": [ { \"offset\": [1, 2 } ] } } } } }");
    const PollResult broken = store.Poll();
    Check(!broken.changed, "a file with a mistake in it is not applied");
    Check(Halo3Behind(store) == -600.0f && store.FromFile(), "the cameras in use stay as they were");
    Check(AnyNoteHas(broken, "was not applied") && AnyNoteHas(broken, "line 1"), "the log is told where the mistake is");
    const PollResult quiet = store.Poll();
    Check(!quiet.changed && quiet.notes.empty(), "and told once, not on every look");

    WriteFileText(path, "\xEF\xBB\xBF" + Halo3File(-700, "ccc"));
    Check(store.Poll().changed && Halo3Behind(store) == -700.0f, "a file saved with a byte-order mark is read");

    WriteFileText(path, "{ \"colour\": 3, \"games\": { \"halo3\": { \"vehicles\": { \"Warthog\": { \"cameras\": ["
                        " { \"ofset\": [1, 2, 3] } ] } } } } }");
    const PollResult typo = store.Poll();
    Check(typo.changed, "a file with keys it does not know is still applied");
    Check(AnyNoteHas(typo, "\"colour\"") && AnyNoteHas(typo, "ofset"), "and each such key is named in the log");

    WriteFileText(path, std::string(kMaxFileBytes + 1, ' '));
    const PollResult huge = store.Poll();
    Check(!huge.changed && AnyNoteHas(huge, "larger than 512 KB"), "an oversized file is refused");

    std::filesystem::remove(path);
    const PollResult gone = store.Poll();
    Check(gone.created && gone.changed, "a file deleted while running is written afresh");
    Check(store.Current().FindGame("halo3") != nullptr && store.Current().FindGame("halo3")->key == std::string("halo3") &&
              store.Current().FindGame("reach")->key == std::string(kDefaultKey),
          "and the built-in cameras are back in use");
}

static void TestFolderThatCannotBeWritten()
{
    const std::filesystem::path path = g_folder / L"no_such_folder" / kFileName;
    Store store(path.wstring());
    const PollResult first = store.Poll();
    Check(!first.created && !first.changed, "a file that cannot be written is not reported as written");
    Check(AnyNoteHas(first, "could not be written") && AnyNoteHas(first, "built-in cameras are in use"),
          "the log is told the built-in cameras are in use");
    Check(!store.FromFile() && store.Current().FindGame("reach") != nullptr, "and they are");
    const PollResult second = store.Poll();
    Check(second.notes.empty() && !second.changed, "told once, not on every look");

    // A folder where the file should be.
    const std::filesystem::path folder = g_folder / L"a_folder.json";
    std::filesystem::create_directory(folder);
    Store blocked(folder.wstring());
    const PollResult look = blocked.Poll();
    Check(!look.created && !look.changed && !blocked.FromFile(), "a folder of the file's name is not read or replaced");
    Check(std::filesystem::is_directory(folder), "and is left as it is");
    Check(blocked.Poll().notes.empty(), "told once");
}

// A vehicle the file has no entry of its own for is added to it: the rest of the text stays exactly as it was,
// the file as the player had it is kept once beside it, and the table in use follows on the next Poll.
static void TestNewVehiclesAreAdded()
{
    const std::filesystem::path folder = g_folder / "autoadd";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path path = folder / kFileName;
    Store store(path.wstring());
    Check(store.Poll().created, "a fresh file to add to");
    const std::string original = ReadFileText(path);
    const uint8_t driver = SeatBits(true, false);

    std::string why;
    const vehicle_camera_autoadd::Addition seraph =
        vehicle_camera_autoadd::For(store.Current(), DefaultTable(), "halo3", "custom_rover", driver);
    Check(store.Add(seraph, "halo3", "custom_rover", driver, why), "a mod vehicle is added to the file");
    const std::filesystem::path backup = folder / (std::wstring(kFileName) + kBackupSuffix);
    Check(std::filesystem::exists(backup) && ReadFileText(backup) == original, "the file as it was is kept beside it");
    const std::string once = ReadFileText(path);
    Check(once.size() > original.size() && Has(once, "\"custom_rover\"") && Has(once, "\r\n") && !Has(once, "\r\r"),
          "the entry is in the file, in its line ends");
    Check(!std::filesystem::exists(folder / (std::wstring(kFileName) + L".adding")), "no half-way file is left");

    const PollResult reread = store.Poll();
    Check(reread.changed, "the table in use follows the addition");
    const Game* game = store.Current().FindGame("halo3");
    const int matched = game ? MatchVehicle(*game, "custom_rover", driver) : -1;
    Check(matched >= 0 && game->vehicles[static_cast<std::size_t>(matched)].name == "custom_rover",
          "and the vehicle now has its own entry");

    const vehicle_camera_autoadd::Addition longsword =
        vehicle_camera_autoadd::For(store.Current(), DefaultTable(), "halo3", "custom_flyer", driver);
    Check(store.Add(longsword, "halo3", "custom_flyer", driver, why), "a second vehicle is added");
    Check(ReadFileText(backup) == original, "and the kept copy is still the player's original");
    const vehicle_camera_autoadd::Addition again =
        vehicle_camera_autoadd::For(store.Current(), DefaultTable(), "halo3", "custom_rover", driver);
    Check(again.entries.empty(), "a vehicle already added is not added twice");
}

int main()
{
    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp))
    {
        std::fprintf(stderr, "FAIL: no temp folder\n");
        return 1;
    }
    g_folder = std::filesystem::path(temp) /
               (L"halomccvr_vehicle_camera_store_tests_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(g_folder, error);
    Check(!error, "the test's own folder can be made");

    TestMissingFileIsWrittenOnce();
    TestEditsAreFollowed();
    TestFolderThatCannotBeWritten();
    TestNewVehiclesAreAdded();

    RemoveFolder();
    std::printf("PASS: %u vehicle camera store checks\n", g_checks);
    return 0;
}
