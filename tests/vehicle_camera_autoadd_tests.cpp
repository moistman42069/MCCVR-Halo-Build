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

int main()
{
    TestWhatIsAdded();
    TestTheTextIsAddedTo();
    std::printf("PASS: %u vehicle camera auto-add checks\n", g_checks);
    return 0;
}
