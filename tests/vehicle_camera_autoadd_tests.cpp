// A vehicle of its own in the player's camera file (src/common/vehicle_camera_autoadd.h): what is added for a
// vehicle the file meets only with "default", and the text it is spliced into.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/common/vehicle_camera_autoadd.h"

using namespace vehicle_camera_file;
using namespace vehicle_camera_autoadd;

static unsigned g_checks = 0;

static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

static std::string Crlf(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

// The built-in table without the entries whose key starts with `prefix`: a file written before they existed.
static Table Without(const char* prefix)
{
    Table table = DefaultTable();
    for (Game& game : table.games)
    {
        std::vector<Vehicle> kept;
        for (const Vehicle& vehicle : game.vehicles)
            if (vehicle.name.rfind(prefix, 0) != 0)
                kept.push_back(vehicle);
        game.vehicles = kept;
    }
    return table;
}

static const Vehicle* Matched(const Table& table, const char* gameKey, const char* vehicle, uint8_t seat)
{
    const Game* game = table.FindGame(gameKey);
    const int index = game ? MatchVehicle(*game, vehicle, seat) : -1;
    return index >= 0 ? &game->vehicles[static_cast<std::size_t>(index)] : nullptr;
}

static void TestWhatIsAdded()
{
    const Table builtIn = DefaultTable();
    const uint8_t driver = SeatBits(true, false), gunner = SeatBits(false, true), passenger = SeatBits(false, false);

    // A vehicle with an entry of its own: nothing.
    Check(For(builtIn, builtIn, "halo3", "warthog", driver).entries.empty(), "a vehicle the file has gets nothing");
    Check(For(builtIn, builtIn, "halo3", "", driver).entries.empty(), "nor does a seat with no vehicle name");

    // A file written before the Elephant's presets: they are added, the driver's and the gunner's together.
    const Table old = Without("Elephant");
    Check(Matched(old, "halo3", "elephant", driver)->isDefault, "the old file rides the Elephant on default");
    const Addition elephant = For(old, builtIn, "halo3", "elephant", driver);
    Check(elephant.fromBuiltIn && elephant.sectionKey == "halo3" && elephant.entries.size() == 2 &&
              elephant.entries[0].name == "Elephant" && elephant.entries[1].name == "Elephant gunner",
          "the built-in Elephant entries are added, every seat's");

    // A mod map's own vehicle: one entry by its name, with the cameras of the default it was riding on.
    const Vehicle* riding = Matched(builtIn, "halo3", "custom_rover", driver);
    Check(riding && riding->isDefault, "a mod vehicle rides on default");
    const Addition seraph = For(builtIn, builtIn, "halo3", "custom_rover", driver);
    Check(!seraph.fromBuiltIn && seraph.entries.size() == 1, "a mod vehicle gets one entry of its own");
    const Vehicle& own = seraph.entries[0];
    Check(own.name == "custom_rover" && own.match.size() == 1 && own.match[0] == "custom_rover" && !own.isDefault &&
              own.seats == 0,
          "named and matched by its own name, for every seat");
    Check(own.cameras.size() == riding->cameras.size() && own.defaultCamera == riding->defaultCamera &&
              own.cameras[1].name == riding->cameras[1].name,
          "with the cameras it was riding on, so the view does not change");

    // A name the file has already, in an entry that does not serve this seat: one for the seat beside it.
    Table clash = builtIn;
    Vehicle gunnerOnly = own;
    gunnerOnly.seats = gunner;
    for (Game& game : clash.games)
        if (game.key == "halo3")
            game.vehicles.insert(game.vehicles.begin(), gunnerOnly);
    const Addition beside = For(clash, builtIn, "halo3", "custom_rover", passenger);
    Check(beside.entries.size() == 1 && beside.entries[0].name == "custom_rover passenger" &&
              beside.entries[0].seats == passenger,
          "a seat whose vehicle name is taken gets an entry for that seat");
}

static void TestTheTextIsAddedTo()
{
    const Table builtIn = DefaultTable();
    const uint8_t driver = SeatBits(true, false);
    for (const bool windows : {false, true})
    {
        const std::string original = windows ? Crlf(ToJson(Without("Elephant"))) : ToJson(Without("Elephant"));
        Table file;
        Check(FromJson(original.data(), original.size(), file).ok, "the starting file reads");
        const Addition addition = For(file, builtIn, "halo3", "custom_rover", driver);
        std::string text = original;
        std::string why;
        Check(Insert(text, addition, "halo3", "custom_rover", driver, why), "the entry is spliced in");
        Table after;
        Check(FromJson(text.data(), text.size(), after).ok, "and the file still reads");
        const Vehicle* now = Matched(after, "halo3", "custom_rover", driver);
        Check(now && now->name == "custom_rover", "and gives the vehicle its own entry");
        Check(Has(text, "_readme") && Has(text, "\"Warthog\""), "everything that was there is still there");
        // Only added to: the original is the new text with the inserted part taken out.
        std::size_t same = 0;
        while (same < original.size() && original[same] == text[same])
            ++same;
        const std::size_t added = text.size() - original.size();
        Check(text.compare(same + added, std::string::npos, original, same, std::string::npos) == 0,
              "and nothing else in the text changed");
        Check(windows ? !Has(text.substr(same, added), "\n\n") && Has(text.substr(same, added), "\r\n") &&
                            text.find('\n') == text.find("\r\n") + 1
                      : !Has(text, "\r"),
              "in the file's own line ends");
        Check(now->cameras.size() == 2, "with the default entry's two cameras");
    }

    // A section with no vehicles yet, and the player's own notes kept.
    const std::string sparse =
        "{\n  \"_mine\": \"keep me\",\n  \"games\": {\n    \"halo3\": {\n      \"vehicles\": {}\n    },\n"
        "    \"default\": { \"vehicles\": { \"default\": { \"cameras\": [ { \"name\": \"Seat\", \"type\": "
        "\"firstperson\" } ] } } }\n  }\n}\n";
    Table file;
    Check(FromJson(sparse.data(), sparse.size(), file).ok, "the sparse file reads");
    const Addition addition = For(file, builtIn, "halo3", "custom_rover", driver);
    Check(addition.sectionKey == "halo3" && addition.entries.size() == 1 && !addition.fromBuiltIn,
          "an empty section takes the vehicle");
    std::string text = sparse;
    std::string why;
    Check(Insert(text, addition, "halo3", "custom_rover", driver, why), "into an empty vehicles object");
    Table after;
    Check(FromJson(text.data(), text.size(), after).ok && Has(text, "keep me"), "it reads, with the note kept");
    const Vehicle* now = Matched(after, "halo3", "custom_rover", driver);
    Check(now && now->name == "custom_rover", "and the vehicle has its entry");

    // A name the built-in table already claims ("space_banshee" holds "banshee", as the file's own matching reads
    // it): the built-in preset is what is added, as the built-in table would give it.
    const Addition banshee = For(file, builtIn, "halo3", "space_banshee", driver);
    Check(banshee.fromBuiltIn && !banshee.entries.empty() && banshee.entries[0].name == "Banshee",
          "a vehicle a preset matches gets the preset");

    // Text that does not read is left alone.
    std::string broken = "{ \"games\": { \"halo3\": { \"vehicles\": { ";
    const std::string before = broken;
    Check(!Insert(broken, addition, "halo3", "custom_rover", driver, why) && broken == before && !why.empty(),
          "a file that does not read is not touched");
    // So is one whose section is not where the addition says.
    Addition elsewhere = addition;
    elsewhere.sectionKey = "halo4";
    std::string other = sparse;
    Check(!Insert(other, elsewhere, "halo3", "custom_rover", driver, why) && other == sparse,
          "nor one without the section");
}

// A file written before the mod had cameras for a game: the game's vehicles ride on the "default" section, which
// is every other game's too, so the game's own section is added -- with the "default" entry as the file has it.
static void TestANewSection()
{
    const Table builtIn = DefaultTable();
    const uint8_t driver = SeatBits(true, false), gunner = SeatBits(false, true);
    Table old = builtIn;
    for (std::size_t g = 0; g < old.games.size(); ++g)
        if (old.games[g].key == "reach")
        {
            old.games.erase(old.games.begin() + static_cast<std::ptrdiff_t>(g));
            break;
        }
    // The player's own default entry, which the new section must keep: a third camera of his.
    for (Game& game : old.games)
        if (game.key == kDefaultKey)
        {
            Camera mine;
            mine.name = "Mine";
            game.vehicles[0].cameras.push_back(mine);
        }
    Check(old.FindGame("reach")->key == kDefaultKey, "the old file has Reach ride on the default section");

    const Addition warthog = For(old, builtIn, "reach", "warthog", driver);
    Check(warthog.newSection && warthog.sectionKey == "reach" && warthog.fromBuiltIn,
          "a Reach Warthog adds Reach's section, from the built-in presets");
    Check(warthog.entries.size() == 4 && warthog.entries[0].name == "Warthog" &&
              warthog.entries[2].name == "Warthog gunner" && warthog.entries.back().isDefault &&
              warthog.entries.back().cameras.size() == 3 && warthog.entries.back().cameras[2].name == "Mine",
          "every Warthog seat's entry, then the default entry as the file had it");

    const Addition custom = For(old, builtIn, "reach", "custom_1234abcd", gunner);
    Check(custom.newSection && !custom.fromBuiltIn && custom.entries.size() == 2 &&
              custom.entries[0].name == "custom_1234abcd" && !custom.entries[0].isDefault &&
              custom.entries[0].cameras.size() == 3 && custom.entries[1].isDefault,
          "a vehicle with no preset gets its own entry with the default's cameras, and the default entry");

    // A file whose default section has no "default" entry gave the game's vehicles no camera: the new section adds
    // the vehicle's presets alone, and a vehicle with none gets nothing (it had no camera before either).
    Table bare = old;
    for (Game& game : bare.games)
        if (game.key == kDefaultKey)
            game.vehicles.clear();
    const Addition presetsOnly = For(bare, builtIn, "reach", "warthog", driver);
    Check(presetsOnly.newSection && presetsOnly.entries.size() == 3 && !presetsOnly.entries.back().isDefault,
          "with no default entry to copy, the new section holds the presets alone");
    Check(For(bare, builtIn, "reach", "custom_1234abcd", driver).entries.empty(),
          "and a vehicle with no preset and nothing to ride on gets nothing");

    for (const bool windows : {false, true})
    {
        const std::string original = windows ? Crlf(ToJson(old)) : ToJson(old);
        Table file;
        Check(FromJson(original.data(), original.size(), file).ok, "the old file reads");
        const Addition addition = For(file, builtIn, "reach", "warthog_gauss", gunner);
        std::string text = original;
        std::string why;
        Check(Insert(text, addition, "reach", "warthog_gauss", gunner, why), "the section is spliced in");
        Table after;
        Check(FromJson(text.data(), text.size(), after).ok, "and the file still reads");
        Check(after.FindGame("reach") && after.FindGame("reach")->key == "reach", "with a Reach section");
        const Vehicle* now = Matched(after, "reach", "warthog_gauss", gunner);
        Check(now && now->name == "Warthog gunner", "that gives the gun its own entry");
        const Vehicle* other = Matched(after, "reach", "forklift", driver);
        Check(other && other->isDefault && other->cameras.size() == 3,
              "and every other Reach vehicle the default entry it had");
        const Vehicle* odst = Matched(after, "odst", "warthog", driver);
        Check(odst && odst->isDefault, "other games still ride on the default section");
        std::size_t same = 0;
        while (same < original.size() && original[same] == text[same])
            ++same;
        const std::size_t added = text.size() - original.size();
        Check(text.compare(same + added, std::string::npos, original, same, std::string::npos) == 0,
              "and nothing else in the text changed");
        Check(windows ? !Has(text.substr(same, added), "\n\n") && Has(text.substr(same, added), "\r\n")
                      : !Has(text, "\r"),
              "in the file's own line ends");
        // Once there, a second vehicle goes into it the ordinary way.
        const Addition next = For(after, builtIn, "reach", "custom_77", driver);
        Check(!next.newSection && next.sectionKey == "reach" && next.entries.size() == 1,
              "the next new vehicle is added to the section now there");
        // The section cannot be added twice.
        std::string again = text;
        Check(!Insert(again, addition, "reach", "warthog_gauss", gunner, why) && again == text,
              "a section the file has is not added again");
    }
}

// The same rule in Halo 3: a file without a "halo3" section (its vehicles riding on "default", every game's) gets
// Halo 3's own section, not Halo 3 entries written into "default" where ODST and the others would find them.
static void TestAHalo3NewSection()
{
    const Table builtIn = DefaultTable();
    const uint8_t driver = SeatBits(true, false);
    Table old = builtIn;
    for (std::size_t g = 0; g < old.games.size(); ++g)
        if (old.games[g].key == "halo3")
        {
            old.games.erase(old.games.begin() + static_cast<std::ptrdiff_t>(g));
            break;
        }
    Check(old.FindGame("halo3")->key == kDefaultKey, "the old file has Halo 3 ride on the default section");
    const Addition warthog = For(old, builtIn, "halo3", "warthog", driver);
    Check(warthog.newSection && warthog.sectionKey == "halo3" && warthog.fromBuiltIn,
          "a Halo 3 Warthog adds Halo 3's section, from the built-in presets");
    Check(warthog.entries.size() == 4 && warthog.entries[0].name == "Warthog" &&
              warthog.entries[1].name == "Warthog passenger" && warthog.entries[2].name == "Warthog gunner" &&
              warthog.entries.back().isDefault,
          "every Halo 3 Warthog seat's entry, then the default entry as the file had it");

    const std::string original = ToJson(old);
    Table file;
    Check(FromJson(original.data(), original.size(), file).ok, "the old Halo 3 file reads");
    const Addition addition = For(file, builtIn, "halo3", "warthog", driver);
    std::string text = original;
    std::string why;
    Check(Insert(text, addition, "halo3", "warthog", driver, why), "Halo 3's section is spliced in");
    Table after;
    Check(FromJson(text.data(), text.size(), after).ok && after.FindGame("halo3")->key == "halo3",
          "and the file reads with a Halo 3 section");
    const Vehicle* odst = Matched(after, "odst", "warthog", driver);
    Check(odst && odst->isDefault, "while ODST's Warthog still rides on the default section");
}

int main()
{
    TestWhatIsAdded();
    TestTheTextIsAddedTo();
    TestANewSection();
    TestAHalo3NewSection();
    std::printf("PASS: %u vehicle camera auto-add checks\n", g_checks);
    return 0;
}
