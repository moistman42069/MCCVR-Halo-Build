// The vehicle camera file (src/common/vehicle_camera_file.h): reading, writing, the built-in table, per-game
// sections, tethering modes, the leash cascade, vehicle matching, and every bound a hand-edited file can reach.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../src/common/vehicle_camera_file.h"

using namespace vehicle_camera_file;

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

static ParseResult Read(const std::string& text, Table& out)
{
    return FromJson(text.c_str(), text.size(), out);
}

static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

// A mistake is reported either as why the file was not read at all, or as a part left out of a file whose rest is
// read (ParseResult::leftOut): either way with its path.
static bool Flagged(const ParseResult& result, const char* part)
{
    if (!result.ok && Has(result.error, part))
        return true;
    for (const std::string& note : result.leftOut)
        if (Has(note, part))
            return true;
    return false;
}

// One game section holding one vehicle entry with the given body.
static std::string OneVehicle(const char* body)
{
    return std::string("{ \"games\": { \"halo3\": { \"vehicles\": { \"Warthog\": ") + body + " } } } }";
}

static void TestBuiltInTable()
{
    const Table built = DefaultTable();
    Check(built.games.size() == 2 && built.games[0].key == std::string("halo3") && built.games[1].key == kDefaultKey,
          "the built-in table has Halo 3's own section and the default one");
    Check(built.FindGame("halo3") == &built.games[0] && built.FindGame("reach") == &built.games[1],
          "Halo 3 finds its own, any other game the default");
    const Game* game = built.FindGame("reach");
    Check(game->vehicles.size() == 1 && game->vehicles[0].isDefault, "the default section has one default entry");
    const Vehicle& any = game->vehicles[0];

    // Halo 3's section: an entry for each vehicle the mod knows by name and the seats it gives cameras of their
    // own, then a default entry with the same two cameras as every other game's; every entry starts in the seat
    // view with the game's own third-person camera second, so the file changes nothing until a camera is chosen.
    const Game& halo3 = built.games[0];
    Check(halo3.vehicles.size() > 10 && halo3.vehicles.back().isDefault &&
              halo3.vehicles.back().cameras.size() == 2,
          "Halo 3 has its vehicles' entries and a default entry of two cameras");
    bool startsInSeat = true, thirdPersonSecond = true, othersBeyond = true, namedOnce = true;
    for (const Vehicle& vehicle : halo3.vehicles)
    {
        // The stock vehicles' entries (named as the mod names them) and default; a mod map's vehicle, named by its
        // tag, keeps the cameras tuned for it.
        if (!vehicle.isDefault && !std::isupper(static_cast<unsigned char>(vehicle.name[0])))
            continue;
        startsInSeat = startsInSeat && vehicle.cameras.size() >= 2 && vehicle.defaultCamera == 0 &&
                       vehicle.cameras[0].type == CameraType::FirstPerson;
        thirdPersonSecond = thirdPersonSecond && vehicle.cameras[1].origin == Origin::ThirdPerson &&
                            vehicle.cameras[1].name == "Third Person";
        // One match text, the vehicle's name; the Elephant's and the Prowler's also their stock tags' words.
        const bool tagWord = vehicle.name.rfind("Elephant", 0) == 0 || vehicle.name.rfind("Prowler", 0) == 0;
        if (!vehicle.isDefault)
            othersBeyond = othersBeyond && vehicle.cameras.size() >= 3 && vehicle.match.size() == (tagWord ? 2u : 1u);
        for (const Vehicle& other : halo3.vehicles)
            namedOnce = namedOnce && (&other == &vehicle || other.name != vehicle.name);
    }
    Check(startsInSeat && thirdPersonSecond, "every Halo 3 entry starts in the seat view, then the game's own");
    {
        int modEntries = 0;
        bool longswordTuned = false;
        for (const Vehicle& vehicle : halo3.vehicles)
        {
            if (vehicle.isDefault || std::isupper(static_cast<unsigned char>(vehicle.name[0])))
                continue;
            ++modEntries;
            if (vehicle.name == "longswordh2a" && !vehicle.cameras.empty())
                longswordTuned = vehicle.cameras[0].fadeParts.size() == 4;
        }
        Check(modEntries == 17 && longswordTuned, "the mod maps' vehicles are built in, with their tuned cameras");
    }
    Check(othersBeyond && namedOnce, "and a known vehicle's entry adds cameras of its own under its own name");
    Check(any.cameras.size() == 2 && any.defaultCamera == 0, "two cameras, starting in the first");
    Check(any.cameras[0].type == CameraType::FirstPerson, "the first is the seat view the player already has");
    Check(any.cameras[0].ModeCount() == 2 && any.cameras[0].rotationSet && !any.cameras[0].Mode(0).rotYaw &&
              any.cameras[0].Mode(1).rotYaw,
          "the seat view too: Untethered first, then Tethered to yaw, and says so");
    Check(any.cameras[1].type == CameraType::Chase && any.cameras[1].ModeCount() == 2, "the second has two modes");
    Check(!any.cameras[1].Mode(0).rotYaw && any.cameras[1].Mode(1).rotYaw, "untethered first, then tethered to yaw");
    Check(any.cameras[1].origin == Origin::ThirdPerson && any.cameras[1].offset[0] == 0.0f &&
              any.cameras[1].offset[1] == 0.0f && any.cameras[1].offset[2] == 0.0f,
          "and sits where the game's own third-person camera rests, so it suits a vehicle of any size");

    // Written and read back it is the same table, and writing that again gives the same text.
    const std::string text = ToJson(built);
    Table back;
    const ParseResult result = Read(text, back);
    Check(result.ok && result.ignored.empty(), "the built-in table's own text reads with nothing ignored");
    std::size_t entries = 0, cameras = 0;
    for (const Game& each : built.games)
        for (const Vehicle& vehicle : each.vehicles)
        {
            ++entries;
            cameras += vehicle.cameras.size();
        }
    Check(result.vehicles == static_cast<int>(entries) && result.cameras == static_cast<int>(cameras),
          "and counts every entry and camera");
    Check(ToJson(back) == text, "writing what was read gives the same text");
    Check(Has(text, "\"_readme\"") && !Has(ToJson(built, false), "\"_readme\""), "the guide is optional");
    Check(Has(text, "{ \"name\": \"Seat\", \"type\": \"firstperson\",\n") &&
              !Has(text, "{ \"name\": \"Seat\", \"type\": \"firstperson\", \"origin\""),
          "a firstperson camera names only what was set: its modes, no placement");
    Check(Has(text, "\"origin\": \"thirdperson\"") && back.games[1].vehicles[0].cameras[1].origin == Origin::ThirdPerson,
          "the game's own third-person place is written and read by name");
    Check(OriginText(Origin::ThirdPerson, "x") == "thirdperson", "and named so in a readout");
    Check(any.cameras[1].lag == kThirdPersonLagSeconds && Has(text, "\"lag\": 0.5, \"lagDamping\": 1.5") &&
              back.games[1].vehicles[0].cameras[1].lag == kThirdPersonLagSeconds,
          "the built-in third-person camera follows on a spring, and the file says so");
    Check(!BoomPitchSet(any.cameras[1].boomPitch) && Has(text, "\"boomPitch\": null") &&
              !BoomPitchSet(back.games[1].vehicles[0].cameras[1].boomPitch),
          "the built-in third-person camera names its boom, unset: where the game's camera looks level, a little higher");
    Check(!Has(ToJson(built, false), "\"origin\": \"seat\", \"offset\": [-200, 90, 70], \"locationTracking\": [\"yaw\"], "
                                     "\"rotationTracking\": [], \"collide\": true, \"collideMargin\": 30, \"lag\": 0.15, "
                                     "\"lagDamping\": 1, \"boomPitch\""),
          "a camera measured from anything else has no boom");

    // A camera that does not say: one at the game's own third-person place lags, any other does not.
    Camera unset;
    Check(LagSecondsOf(unset) == 0.0f, "a camera that says nothing about lag has none");
    unset.origin = Origin::ThirdPerson;
    Check(LagSecondsOf(unset) == kThirdPersonLagSeconds, "unless it rests where the game's camera does");
    Check(LagDampingOf(unset) == kThirdPersonLagDamping && kThirdPersonLagDamping > 1.0f,
          "and settles there without swinging past");
    unset.lagDamping = 0.8f;
    Check(LagDampingOf(unset) == 0.8f, "its own damping when it says");
    unset.lag = 0.0f;
    Check(LagSecondsOf(unset) == 0.0f, "and \"lag\": 0 turns that off");
}

static void TestGames()
{
    Table table;
    const std::string text =
        "{ \"version\": 1, \"_note\": \"mine\", \"games\": {"
        "  \"Halo3\": { \"vehicles\": { \"default\": { \"cameras\": [ { \"name\": \"A\" } ] } } },"
        "  \"_skipped\": 5,"
        "  \"default\": { \"vehicles\": { \"default\": { \"cameras\": [ { \"name\": \"B\" } ] } } } } }";
    Check(Read(text, table).ok, "two sections read");
    Check(table.games.size() == 2 && table.games[0].key == "halo3", "a section's key is matched in lower case");
    Check(table.FindGame("halo3")->vehicles[0].cameras[0].name == "A", "a game gets its own section");
    Check(table.FindGame("reach")->vehicles[0].cameras[0].name == "B", "another game gets the default section");
    Check(table.FindGame(nullptr)->vehicles[0].cameras[0].name == "B", "no game gets the default section");

    Table only;
    Check(Read("{ \"games\": { \"odst\": { \"vehicles\": { } } } }", only).ok, "a section may list no vehicles");
    Check(only.FindGame("odst") != nullptr && only.FindGame("halo4") == nullptr,
          "with no default section another game has none");

    Table untouched = DefaultTable();
    ParseResult bad = Read("{ \"games\": { \"halo5\": { \"vehicles\": { } } } }", untouched);
    Check(Flagged(bad, "games.halo5") && Flagged(bad, "line 1"), "an unknown game key is refused");
    Check(untouched.games.size() == 2 && untouched.games[1].key == kDefaultKey, "and the table is left as it was");
    bad = Read("{ \"games\": { \"reach\": { \"vehicles\": { } }, \"REACH\": { \"vehicles\": { } } } }", untouched);
    Check(Flagged(bad, "listed once"), "a game listed twice is refused");
    bad = Read("{ \"version\": 1 }", untouched);
    Check(Flagged(bad, "\"games\""), "a file without games is refused");
    bad = Read("{ \"games\": { \"halo3\": { } } }", untouched);
    Check(Flagged(bad, "\"vehicles\""), "a section without vehicles is refused");
    bad = Read("[]", untouched);
    Check(Flagged(bad, "the file"), "a file that is not an object is refused");

    const ParseResult extra = Read("{ \"gamez\": 1, \"games\": { \"halo3\": { \"vehicles\": { }, \"vehicle\": 2 } } }", only);
    Check(extra.ok && extra.ignored.size() == 2 && extra.ignored[0] == "gamez" &&
              extra.ignored[1] == "games.halo3.vehicle",
          "keys that match nothing are reported by their path");
}

static void TestCamerasAndTethering()
{
    Table table;
    const std::string text = OneVehicle(
        "{ \"defaultCamera\": 1, \"defaultMode\": 1, \"cameras\": ["
        "  { \"name\": \"Seat\", \"type\": \"FirstPerson\" },"
        "  { \"name\": \"Behind\", \"origin\": \"vehicle\", \"offset\": [-450, 10.5, 180],"
        "    \"locationTracking\": [\"yaw\"], \"rotationTracking\": [], \"collide\": false, \"collideMargin\": 12,"
        "    \"tethering\": ["
        "      { \"name\": \"Untethered\" },"
        "      { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\", \"roll\"], \"offset\": [-300, 0, 150],"
        "        \"origin\": \"turret_base\", \"locationTracking\": \"view\" } ] },"
        "  { \"origin\": \"PlayerHead\", \"locationTracking\": \"VIEW\" } ] }");
    const ParseResult result = Read(text, table);
    Check(result.ok && result.vehicles == 1 && result.cameras == 3 && result.ignored.empty(), "three cameras read");
    const Vehicle& warthog = table.games[0].vehicles[0];
    Check(warthog.name == "Warthog" && warthog.match.size() == 1 && warthog.match[0] == "warthog",
          "an entry without match is matched by its own key");
    Check(warthog.defaultCamera == 1 && warthog.defaultMode == 1, "where the seat starts");
    Check(warthog.cameras[0].type == CameraType::FirstPerson && warthog.cameras[0].ModeCount() == 1,
          "the type is read in any case");

    const Camera& behind = warthog.cameras[1];
    Check(behind.offset[0] == -450.0f && behind.offset[1] == 10.5f && behind.offset[2] == 180.0f, "the offset");
    Check(behind.locYaw && !behind.locPitch && !behind.locRoll && !behind.locView, "location tracking by axis");
    Check(!behind.collide && behind.collideMargin == 12.0f, "collision settings");
    Check(behind.lag < 0.0f && LagSecondsOf(behind) == 0.0f, "a camera that does not set lag has none");
    Check(behind.lagDamping < 0.0f && LagDampingOf(behind) == 1.0f, "and an unset damping away from the game's place is 1");
    Check(behind.ModeCount() == 2, "two modes");
    const Tether still = behind.Mode(0);
    Check(still.name == "Untethered" && still.offset[0] == -450.0f && still.locYaw && !still.locPitch &&
              !still.rotYaw && still.origin == -1,
          "a mode takes the camera's own values for what it leaves out");
    Check(behind.OriginOf(still) == Origin::Vehicle && behind.MarkerOf(still).empty(), "and the camera's origin");
    const Tether tethered = behind.Mode(1);
    Check(tethered.rotYaw && !tethered.rotPitch && tethered.rotRoll, "a mode's own rotation tracking");
    Check(behind.rotationSet && behind.rotationOwn && !warthog.cameras[0].rotationSet,
          "a camera says how its view turns when it or a mode gives rotationTracking, and one that never does does not");
    Check(tethered.offset[0] == -300.0f && tethered.offset[2] == 150.0f, "a mode's own offset");
    Check(tethered.locView && !tethered.locYaw, "a mode's own location tracking");
    Check(behind.OriginOf(tethered) == Origin::Marker && behind.MarkerOf(tethered) == "turret_base",
          "a mode's own origin: any other name is a marker");
    Check(behind.Mode(2).name == "Untethered" && behind.Mode(-1).name == "Tethered", "a mode index wraps");

    const Camera& head = warthog.cameras[2];
    Check(head.origin == Origin::Head && head.locView && !head.locYaw && head.name.empty(),
          "playerhead and view, in any case");
    const Tether own = head.Mode(5);
    Check(own.locView && own.name.empty() && head.ModeCount() == 1, "a camera without tethering is its one mode");

    // The file as written reads back to the same text.
    const std::string written = ToJson(table, false);
    Table back;
    Check(Read(written, back).ok && ToJson(back, false) == written, "the table round-trips");
    Check(Has(written, "{ \"name\": \"Untethered\" }"), "a mode names only what differs from its camera");
    Check(Has(written, "\"origin\": \"turret_base\"") && Has(written, "\"locationTracking\": \"view\""),
          "and writes what does");

    // "head" is read as playerhead; a marker on the camera itself is kept by name.
    Check(Read(OneVehicle("{ \"cameras\": [ { \"origin\": \"head\" }, { \"origin\": \"driver_seat\" } ] }"), table).ok,
          "origins by name");
    Check(table.games[0].vehicles[0].cameras[0].origin == Origin::Head, "head is the player's head");
    Check(table.games[0].vehicles[0].cameras[1].origin == Origin::Marker &&
              table.games[0].vehicles[0].cameras[1].originMarker == "driver_seat",
          "a marker keeps its name as written");
    Check(OriginText(Origin::Marker, "driver_seat") == "driver_seat" && OriginText(Origin::Head, "") == "playerhead" &&
              OriginText(Origin::Seat, "") == "seat" && OriginText(Origin::Vehicle, "x") == "vehicle",
          "origins as the file writes them");
}

// "aimMarker": the ring where the vehicle aims -- the seat's, a camera's over it, a mode's over that.
static void TestAimMarker()
{
    // The built-in cameras have it as Halo Campaign Evolved's do: on for the seat, off in each Untethered mode, so
    // it shows only where the view turns with the vehicle.
    const Table built = DefaultTable();
    bool seatOn = true, untetheredOff = true, tetheredOn = true;
    int chaseModes = 0;
    for (const Game& game : built.games)
        for (const Vehicle& vehicle : game.vehicles)
        {
            // A mod map's built-in vehicle is written as a player's file has it: the ring follows each mode's turn.
            if (!vehicle.isDefault && !std::isupper(static_cast<unsigned char>(vehicle.name[0])))
                continue;
            seatOn = seatOn && vehicle.aimMarker == 1;
            for (std::size_t c = 0; c < vehicle.cameras.size(); ++c)
            {
                const Camera& camera = vehicle.cameras[c];
                if (camera.type != CameraType::Chase)
                    continue;
                for (int m = 0; m < camera.ModeCount(); ++m)
                {
                    ++chaseModes;
                    const bool shown = EffectiveAimMarker(vehicle, static_cast<int>(c), m);
                    if (camera.Mode(m).rotYaw)
                        tetheredOn = tetheredOn && shown && camera.Mode(m).aimMarker == -1;
                    else
                        untetheredOff = untetheredOff && !shown && camera.Mode(m).aimMarker == 0;
                }
            }
        }
    Check(chaseModes > 20 && seatOn && untetheredOff && tetheredOn,
          "built in: the ring in every Tethered mode and in no Untethered one");
    const std::string builtText = ToJson(built, false);
    Check(Has(builtText, "\"aimMarker\": true,") && Has(builtText, "{ \"name\": \"Untethered\", \"aimMarker\": false }") &&
              Has(builtText, "{ \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"] }"),
          "and the file says so: on for each seat, off in each Untethered mode");

    // The cascade, read from a file: mode over camera over seat, null and left out passing it up.
    Table table;
    const std::string text = OneVehicle(
        "{ \"aimMarker\": false, \"cameras\": ["
        "  { \"name\": \"A\", \"tethering\": [ { \"name\": \"one\" }, { \"name\": \"two\", \"aimMarker\": true },"
        "                                   { \"name\": \"three\", \"aimMarker\": null } ] },"
        "  { \"name\": \"B\", \"aimMarker\": true, \"tethering\": [ { \"name\": \"one\", \"aimMarker\": false },"
        "                                                          { \"name\": \"two\" } ] },"
        "  { \"name\": \"C\", \"aimMarker\": null },"
        "  { \"name\": \"D\", \"aimMarker\": true } ] }");
    const ParseResult result = Read(text, table);
    Check(result.ok && result.ignored.empty(), "aimMarker is read at every level");
    const Vehicle& seat = table.games[0].vehicles[0];
    Check(seat.aimMarker == 0, "the seat's own");
    Check(!EffectiveAimMarker(seat, 0, 0) && EffectiveAimMarker(seat, 0, 1) && !EffectiveAimMarker(seat, 0, 2),
          "a mode that says nothing, or null, is the seat's through its camera; one that says, its own");
    Check(seat.cameras[0].Mode(2).aimMarker == -1, "null is unset");
    Check(!EffectiveAimMarker(seat, 1, 0) && EffectiveAimMarker(seat, 1, 1), "a camera's own over the seat's");
    Check(!EffectiveAimMarker(seat, 2, 0) && seat.cameras[2].aimMarker == -1, "a camera's null is the seat's");
    Check(EffectiveAimMarker(seat, 3, 0) && EffectiveAimMarker(seat, 3, 7), "a camera without modes, any mode");
    Check(!EffectiveAimMarker(seat, 9, 0) && !EffectiveAimMarker(seat, -1, 0), "a camera out of range: the seat's");
    Check(EffectiveAimMarker(seat, 1, 3), "a mode index wraps, as Mode's does");
    const Vehicle unset{};
    Check(unset.aimMarker == -1 && !EffectiveAimMarker(unset, 0, 0), "a seat that says nothing, with no camera: none");

    // Left out at every level, as in a file written before the key: shown exactly where the mode turns the view with
    // the vehicle's heading -- what the built-in values amount to.
    Table older;
    Check(Read(OneVehicle("{ \"cameras\": [ { \"name\": \"Seat\", \"type\": \"firstperson\" },"
                          "  { \"tethering\": [ { \"name\": \"Untethered\" },"
                          "                     { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"] },"
                          "                     { \"name\": \"Pitch\", \"rotationTracking\": [\"pitch\"] } ] },"
                          "  { \"rotationTracking\": [\"yaw\", \"pitch\"] }, { } ] }"),
               older)
              .ok,
          "a file without aimMarker reads");
    const Vehicle& plain = older.games[0].vehicles[0];
    Check(plain.aimMarker == -1 && !EffectiveAimMarker(plain, 1, 0) && EffectiveAimMarker(plain, 1, 1) &&
              !EffectiveAimMarker(plain, 1, 2),
          "on in the mode that turns the view with the vehicle's yaw, off where it does not");
    Check(EffectiveAimMarker(plain, 2, 0) && !EffectiveAimMarker(plain, 3, 0), "and so for a camera without modes");
    Check(!Has(ToJson(older, false), "aimMarker"), "nothing is written that the file did not say");
    Check(Read(OneVehicle("{ \"aimMarker\": null, \"cameras\": [ { \"rotationTracking\": [\"yaw\"] } ] }"), older)
                  .ok &&
              older.games[0].vehicles[0].aimMarker == -1 && EffectiveAimMarker(older.games[0].vehicles[0], 0, 0),
          "the seat's null is the same as leaving it out");

    const std::string written = ToJson(table, false);
    Table back;
    Check(Read(written, back).ok && ToJson(back, false) == written, "it round-trips");
    Check(Has(written, "\"aimMarker\": false,") && Has(written, "{ \"name\": \"two\", \"aimMarker\": true }") &&
              Has(written, "{ \"name\": \"three\" }") && Has(written, "{ \"name\": \"one\", \"aimMarker\": false }"),
          "written where a mode sets it, left out where it does not");
    const char* const cameraC = "{ \"name\": \"C\", \"type\": \"chase\", \"origin\": \"vehicle\", \"offset\": [0, 0, 0], "
                                "\"locationTracking\": [\"yaw\", \"pitch\", \"roll\"], \"rotationTracking\": [], "
                                "\"collide\": true, \"collideMargin\": 30 }";
    const char* const cameraD = "\"name\": \"D\", \"type\": \"chase\", \"origin\": \"vehicle\", \"offset\": [0, 0, 0], "
                                "\"locationTracking\": [\"yaw\", \"pitch\", \"roll\"], \"rotationTracking\": [], "
                                "\"collide\": true, \"collideMargin\": 30, \"aimMarker\": true }";
    Check(Has(written, cameraC) && Has(written, cameraD), "a camera writes it only where it sets it");

    // Wrong types are refused, naming where.
    ParseResult bad = Read(OneVehicle("{ \"aimMarker\": 0, \"cameras\": [ {} ] }"), table);
    Check(Flagged(bad, "Warthog.aimMarker"), "the seat's is true, false or null");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"aimMarker\": \"yes\" } ] }"), table);
    Check(Flagged(bad, "cameras[0].aimMarker"), "a camera's is true, false or null");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"tethering\": [ { \"aimMarker\": 1 } ] } ] }"), table);
    Check(Flagged(bad, "tethering[0].aimMarker"), "and so is a mode's");
}

static void TestLeash()
{
    Table table;
    const std::string text = OneVehicle(
        "{ \"cameras\": ["
        "    { \"name\": \"Inherits\" },"
        "    { \"name\": \"Own\", \"leashMax\": [\"inherit\", null, 70], \"leashMin\": null,"
        "      \"tethering\": [ { \"name\": \"A\" }, { \"name\": \"B\", \"leashMax\": [5] },"
        "                       { \"name\": \"C\", \"leashMin\": [-1, \"inherit\"], \"leashMax\": \"inherit\" } ] } ],"
        "  \"leashMin\": [-10, -15, 40], \"leashMax\": [8, 12, 64] }");
    Check(Read(text, table).ok, "a leash at every level reads");
    const Vehicle& seat = table.games[0].vehicles[0];
    Check(seat.leashMin[0] == -10.0f && seat.leashMax[2] == 64.0f, "the seat's leash, wherever the file puts it");
    const Camera& inherits = seat.cameras[0];
    Check(inherits.leashMin[1] == -15.0f && inherits.leashMax[1] == 12.0f, "a camera starts from its seat's leash");
    const Camera& own = seat.cameras[1];
    Check(own.leashMax[0] == 8.0f && own.leashMax[1] == kUnleashed && own.leashMax[2] == 70.0f,
          "per axis: inherit, no limit, a limit of its own");
    Check(!LeashSet(own.leashMin), "null drops the seat's leash on that side");
    Check(own.Mode(0).leashMax[2] == 70.0f && !LeashSet(own.Mode(0).leashMin), "a mode starts from its camera's");
    Check(own.Mode(1).leashMax[0] == 5.0f && own.Mode(1).leashMax[1] == kUnleashed &&
              own.Mode(1).leashMax[2] == kUnleashed,
          "a shorter list leaves the other axes unlimited");
    Check(own.Mode(2).leashMin[0] == -1.0f && own.Mode(2).leashMin[1] == -kUnleashed &&
              own.Mode(2).leashMax[2] == 70.0f,
          "inherit in a mode is the camera's value");

    const std::string written = ToJson(table, false);
    Table back;
    Check(Read(written, back).ok && ToJson(back, false) == written, "leashes round-trip");
    Check(LeashText(seat.leashMin, seat.leashMax) == "forward -10 to 8, right -15 to 12, up 40 to 64 cm",
          "the leash as places from the origin");
    Check(LeashText(own.leashMin, own.Mode(1).leashMax) == "forward at most 5 cm", "one limit");
    const float noFloor[3] = {-kUnleashed, -kUnleashed, -kUnleashed};
    const float noCeiling[3] = {kUnleashed, kUnleashed, kUnleashed};
    const float floorOnly[3] = {-kUnleashed, -kUnleashed, 20.0f};
    Check(LeashText(floorOnly, own.Mode(1).leashMax) == "forward at most 5, up at least 20 cm", "a floor");
    Check(LeashText(noFloor, noCeiling) == "None" && !LeashSet(noFloor) && !LeashSet(noCeiling) && LeashSet(floorOnly),
          "no limit at all");

    ParseResult bad = Read(OneVehicle("{ \"leashMin\": \"tight\", \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "vehicles.Warthog.leashMin"), "a leash that is neither a list nor inherit");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"leashMax\": [1, 2, 3, 4] } ] }"), table);
    Check(Flagged(bad, "cameras[0].leashMax"), "a leash of four axes");
}

static void TestMatching()
{
    Table table;
    const std::string text =
        "{ \"games\": { \"halo3\": { \"vehicles\": {"
        "  \"Warthog\": { \"match\": [\"warthog\"], \"cameras\": [ { \"name\": \"driver\" } ] },"
        "  \"Warthog passenger\": { \"match\": \"warthog\", \"seat\": \"passenger\", \"cameras\": [ { } ] },"
        "  \"Warthog gunner\": { \"match\": [\"warthog\"], \"seat\": [\"gunner\"], \"cameras\": [ { } ] },"
        "  \"Wraith\": { \"cameras\": [ { } ] },"
        "  \"Wraith turret\": { \"match\": [\"wraith_turret\", \"anti_infantry\"], \"cameras\": [ { } ] },"
        "  \"Pelican\": { \"enabled\": false },"
        "  \"default\": { \"cameras\": [ { } ] },"
        "  \"Unseated twin\": { \"match\": [\"WARTHOG\"], \"cameras\": [ { } ] } } } } }";
    Check(Read(text, table).ok, "a section of entries reads");
    const Game& game = table.games[0];
    const auto name = [&game](const char* vehicle, uint8_t seat) -> std::string {
        const int index = MatchVehicle(game, vehicle, seat);
        return index < 0 ? std::string("(none)") : game.vehicles[static_cast<std::size_t>(index)].name;
    };
    const char* hog = "objects\\vehicles\\warthog\\warthog";
    Check(name(hog, 0) == "Warthog", "an unknown seat never picks an entry that names a seat");
    Check(name(hog, kSeatDriver) == "Warthog", "the driver uses the entry without a seat list");
    Check(name(hog, kSeatPassenger) == "Warthog passenger", "a passenger uses the entry naming that seat");
    Check(name(hog, kSeatGunner) == "Warthog gunner", "and a gunner the gunner's");
    Check(name(hog, SeatBits(true, true)) == "Warthog gunner", "a seat with two flags passes a list naming either");
    Check(name("Objects\\Vehicles\\WARTHOG\\Warthog", kSeatDriver) == "Warthog", "names match in any case");
    Check(name("objects\\vehicles\\wraith\\wraith", kSeatDriver) == "Wraith", "an entry's own key is its match");
    Check(name("objects\\vehicles\\wraith\\turrets\\wraith_turret", kSeatGunner) == "Wraith turret",
          "the longest match wins");
    Check(name("objects\\weapons\\turret\\anti_infantry", 0) == "Wraith turret", "any of an entry's match texts");
    Check(name("objects\\vehicles\\ghost\\ghost", kSeatDriver) == "default", "an unlisted vehicle uses default");
    Check(name("objects\\vehicles\\pelican\\pelican", 0) == "Pelican", "a disabled entry still matches");
    const Vehicle& pelican = game.vehicles[static_cast<std::size_t>(MatchVehicle(game, "pelican", 0))];
    Check(!pelican.enabled && pelican.cameras.empty(), "and may list no cameras");
    Check(game.vehicles[1].seats == kSeatPassenger && game.vehicles[2].seats == kSeatGunner, "seat lists as bits");

    // The built-in Halo 3 entries: a mod's variant named by its tag finds the stock vehicle's cameras, the Elephant's
    // and the Prowler's by their stock tags' words too.
    const Table built = DefaultTable();
    const Game& h3 = built.games[0];
    const auto builtName = [&h3](const char* vehicle, uint8_t seat) -> std::string {
        const int index = MatchVehicle(h3, vehicle, seat);
        return index < 0 ? std::string("(none)") : h3.vehicles[static_cast<std::size_t>(index)].name;
    };
    Check(builtName("behemoth_fast", kSeatDriver) == "Elephant" &&
              builtName("behemoth_fast", kSeatGunner) == "Elephant gunner" &&
              builtName("elephant", kSeatDriver) == "Elephant",
          "built in: behemoth_fast takes the Elephant's cameras, driver and gunner");
    Check(builtName("mauler_fast", kSeatDriver) == "Prowler" && builtName("mauler_fast", kSeatGunner) == "Prowler gunner" &&
              builtName("prowler", kSeatDriver) == "Prowler",
          "built in: mauler_fast takes the Prowler's");
    Check(builtName("warthog_fast", kSeatGunner) == "Warthog gunner" && builtName("ghost_aa", kSeatDriver) == "Ghost" &&
              builtName("custom_rover", kSeatDriver) == "default" && builtName("sparrowhawk", kSeatDriver) == "sparrowhawk",
          "built in: other variants by their own words; a vehicle like no stock one, the default entry");

    Table bare;
    Check(Read("{ \"games\": { \"reach\": { \"vehicles\": { \"Ghost\": { \"cameras\": [ { } ] } } } } }", bare).ok,
          "a section without a default entry");
    Check(MatchVehicle(bare.games[0], "banshee", 0) == -1, "gives an unlisted vehicle nothing");

    Check(SeatBits(true, false) == kSeatDriver && SeatBits(false, true) == kSeatGunner &&
              SeatBits(false, false) == kSeatPassenger && SeatBits(true, true) == (kSeatDriver | kSeatGunner),
          "a seat's flags as bits");
    Check(SeatText(kSeatDriver | kSeatGunner) == "Driver, Gunner" && SeatText(kSeatPassenger) == "Passenger" &&
              SeatText(0).empty(),
          "a seat as text");

    const std::string written = ToJson(table, false);
    Table back;
    Check(Read(written, back).ok && ToJson(back, false) == written, "entries round-trip");
    Check(Has(written, "\"seat\": [\"passenger\"]") && Has(written, "\"enabled\": false"), "seat and enabled written");

    ParseResult bad = Read(OneVehicle("{ \"seat\": \"pilot\", \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "Warthog.seat"), "an unknown seat is refused");
    bad = Read(OneVehicle("{ \"seat\": [\"driver\", 3], \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "seat[1]"), "and one in a list, by its place");
    Check(Read(OneVehicle("{ \"seat\": [], \"cameras\": [ { } ] }"), table).ok && table.games[0].vehicles[0].seats == 0,
          "an empty seat list is any seat");
}

static void TestCarryMode()
{
    Table table;
    const std::string text = OneVehicle(
        "{ \"cameras\": ["
        "  { \"name\": \"Cockpit\", \"tethering\": [ { \"name\": \"Untethered\" },"
        "      { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\", \"pitch\", \"roll\"] } ] },"
        "  { \"name\": \"Behind\", \"tethering\": [ { \"name\": \"Free\" },"
        "      { \"name\": \"Level\", \"rotationTracking\": [\"yaw\"] },"
        "      { \"name\": \"tethered\", \"rotationTracking\": [\"yaw\", \"roll\"] } ] },"
        "  { \"name\": \"Plain\" } ] }");
    Check(Read(text, table).ok, "three cameras with modes read");
    const Vehicle& seat = table.games[0].vehicles[0];
    Check(CarryMode(seat.cameras[1], seat.cameras[0].Mode(1)) == 2, "a mode of the same name is kept, in any case");
    Check(CarryMode(seat.cameras[1], seat.cameras[0].Mode(0)) == 0, "else one turning the view on the same axes");
    Check(CarryMode(seat.cameras[0], seat.cameras[1].Mode(1)) == 0, "else the first");
    Check(CarryMode(seat.cameras[2], seat.cameras[0].Mode(1)) == 0, "a camera with one mode has only that");
    Check(RotationTrackingText(seat.cameras[0].Mode(1)) == "Yaw, Pitch, Roll" &&
              RotationTrackingText(seat.cameras[0].Mode(0)) == "None" &&
              RotationTrackingText(seat.cameras[1].Mode(2)) == "Yaw, Roll",
          "rotation tracking as text");
    Check(LocationTrackingText(seat.cameras[0].Mode(0)) == "Yaw, Pitch, Roll", "location tracking as text");
}

static void TestBoundsAndErrors()
{
    Table table = DefaultTable();
    const std::string before = ToJson(table, false);

    ParseResult bad = Read("{ \"games\": { \"halo3\": { \"vehicles\": {\n  \"Ghost\": { \"cameras\": [ { \"offset\": \"far\" } ] } } } } }", table);
    Check(Flagged(bad, "games.halo3.vehicles.Ghost.cameras[0].offset") && Flagged(bad, "line 2"),
          "a type error names the key and the line");
    Check(bad.ok && table.games.size() == 1 && table.games[0].vehicles[0].name == "Ghost" &&
              table.games[0].vehicles[0].cameras[0].offset[0] == 0.0f && ToJson(table, false) != before,
          "and the rest of the file is in use, that offset at its default");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"offset\": [0, 0, 1e39] } ] }"), table);
    Check(Flagged(bad, "offset[2]"), "a number past the bound is refused");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"offset\": [1, 2, 3, 4] } ] }"), table);
    Check(Flagged(bad, ""), "an offset of four numbers");
    Check(Read(OneVehicle("{ \"cameras\": [ { \"offset\": [5] } ] }"), table).ok &&
              table.games[0].vehicles[0].cameras[0].offset[0] == 5.0f &&
              table.games[0].vehicles[0].cameras[0].offset[1] == 0.0f,
          "a shorter offset leaves the rest at zero");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"type\": \"orbit\" } ] }"), table);
    Check(Flagged(bad, "chase"), "an unknown camera type");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"rotationTracking\": [\"yaw\", \"spin\"] } ] }"), table);
    Check(Flagged(bad, "rotationTracking[1]"), "an unknown axis, by its place");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"locationTracking\": \"orbit\" } ] }"), table);
    Check(Flagged(bad, "view"), "a location tracking word that is not view");
    Check(Read(OneVehicle("{ \"cameras\": [ { \"lag\": 0.5, \"lagDamping\": 0.7 } ] }"), table).ok &&
              table.games[0].vehicles[0].cameras[0].lag == 0.5f &&
              table.games[0].vehicles[0].cameras[0].lagDamping == 0.7f,
          "lag and its damping are read");
    Check(Has(ToJson(table, false), "\"lag\": 0.5, \"lagDamping\": 0.7"), "and written back");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"lag\": -1 } ] }"), table);
    Check(Flagged(bad, "lag"), "a negative lag");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"lag\": 6 } ] }"), table);
    Check(Flagged(bad, "seconds"), "a lag past five seconds");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"lagDamping\": 0.05 } ] }"), table);
    Check(Flagged(bad, "lagDamping"), "a damping too small to settle");
    Check(Read(OneVehicle("{ \"cameras\": [ { \"origin\": \"thirdperson\", \"boomPitch\": 25.5 } ] }"), table).ok &&
              table.games[0].vehicles[0].cameras[0].boomPitch == 25.5f,
          "a boom angle is read");
    Check(Has(ToJson(table, false), "\"boomPitch\": 25.5"), "and written back");
    Check(Read(OneVehicle("{ \"cameras\": [ { \"origin\": \"thirdperson\", \"boomPitch\": null } ] }"), table).ok &&
              !BoomPitchSet(table.games[0].vehicles[0].cameras[0].boomPitch),
          "null is no boom angle of its own");
    Check(Read(OneVehicle("{ \"cameras\": [ { \"boomPitch\": -89 } ] }"), table).ok &&
              table.games[0].vehicles[0].cameras[0].boomPitch == -89.0f && Has(ToJson(table, false), "\"boomPitch\": -89"),
          "straight down is the end of it, and any camera may name one");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"boomPitch\": 95 } ] }"), table);
    Check(Flagged(bad, "boomPitch") && Flagged(bad, "-89 to 89"), "a boom angle past straight up");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"boomPitch\": \"high\" } ] }"), table);
    Check(Flagged(bad, "boomPitch"), "a boom angle that is not a number");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"collide\": 1 } ] }"), table);
    Check(Flagged(bad, "true or false"), "a number where true or false belongs");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"origin\": \"\" } ] }"), table);
    Check(Flagged(bad, "origin"), "an empty origin");
    bad = Read(OneVehicle("{ \"cameras\": [ ] }"), table);
    Check(Flagged(bad, "at least one camera"), "an empty camera list");
    bad = Read(OneVehicle("{ }"), table);
    Check(Flagged(bad, "\"cameras\""), "an enabled entry without cameras");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"tethering\": [ ] } ] }"), table);
    Check(Flagged(bad, "at least one tethering mode"), "an empty tethering list");
    bad = Read(OneVehicle("{ \"cameras\": [ { \"tethering\": [ 3 ] } ] }"), table);
    Check(Flagged(bad, "tethering[0]"), "a tethering mode that is not an object");
    bad = Read(OneVehicle("{ \"defaultCamera\": 3000000000, \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "defaultCamera"), "a camera index past any list is refused");
    bad = Read(OneVehicle("{ \"defaultCamera\": -1, \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, ""), "and a negative one");
    bad = Read(OneVehicle("{ \"defaultMode\": 1.5, \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "defaultMode"), "and a fraction");
    Check(Read(OneVehicle("{ \"defaultCamera\": 7, \"defaultMode\": 7, \"cameras\": [ { } ] }"), table).ok &&
              table.games[0].vehicles[0].defaultCamera == 0 && table.games[0].vehicles[0].defaultMode == 0,
          "an index past this entry's list starts at the first");
    bad = Read(OneVehicle("{ \"enabled\": \"no\", \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "enabled"), "enabled that is not true or false");
    bad = Read(OneVehicle("{ \"match\": 5, \"cameras\": [ { } ] }"), table);
    Check(Flagged(bad, "match"), "match that is not text");

    // The counts a file may reach, and one more.
    const auto cameras = [](int count) {
        std::string list;
        for (int i = 0; i < count; ++i)
            list += std::string(i ? ", " : "") + "{ }";
        return OneVehicle(("{ \"cameras\": [ " + list + " ] }").c_str());
    };
    Check(Read(cameras(kMaxCameras), table).ok, "sixteen cameras read");
    bad = Read(cameras(kMaxCameras + 1), table);
    Check(Flagged(bad, "at most 16 cameras"), "seventeen do not");
    const auto modes = [](int count) {
        std::string list;
        for (int i = 0; i < count; ++i)
            list += std::string(i ? ", " : "") + "{ }";
        return OneVehicle(("{ \"cameras\": [ { \"tethering\": [ " + list + " ] } ] }").c_str());
    };
    Check(Read(modes(kMaxTethers), table).ok, "eight tethering modes read");
    bad = Read(modes(kMaxTethers + 1), table);
    Check(Flagged(bad, "at most 8"), "nine do not");
    const auto entries = [](int count) {
        std::string list;
        for (int i = 0; i < count; ++i)
            list += std::string(i ? ", " : "") + "\"v" + std::to_string(i) + "\": { \"cameras\": [ { } ] }";
        return "{ \"games\": { \"halo3\": { \"vehicles\": { \"_note\": 1, " + list + " } } } }";
    };
    Check(Read(entries(kMaxVehicles), table).ok && table.games[0].vehicles.size() == kMaxVehicles,
          "sixty-four entries read, notes aside");
    bad = Read(entries(kMaxVehicles + 1), table);
    Check(Flagged(bad, "at most 64"), "sixty-five do not");

    // A file past the size bound is refused before it is parsed.
    std::string huge = "{ \"_pad\": \"";
    huge.append(kMaxFileBytes, 'x');
    huge += "\", \"games\": { } }";
    bad = Read(huge, table);
    Check(Flagged(bad, "512 KB"), "a file past the size bound");
    bad = FromJson(nullptr, 0, table);
    Check(Flagged(bad, ""), "no text at all");

    // Notes anywhere, and keys that match nothing.
    const ParseResult noted = Read(
        OneVehicle("{ \"_why\": \"mine\", \"cameras\": [ { \"_note\": 1, \"ofset\": [1], \"name\": null,"
                   " \"tethering\": [ { \"_n\": 2, \"speed\": 3 } ] } ], \"colour\": \"red\" }"),
        table);
    Check(noted.ok && noted.ignored.size() == 3, "notes are skipped and unknown keys reported");
    Check(noted.ignored[0] == "games.halo3.vehicles.Warthog.cameras[0].ofset" &&
              noted.ignored[1] == "games.halo3.vehicles.Warthog.cameras[0].tethering[0].speed" &&
              noted.ignored[2] == "games.halo3.vehicles.Warthog.colour",
          "each by its path");

    // Text that needs escaping survives the round trip.
    Check(Read(OneVehicle("{ \"cameras\": [ { \"name\": \"Say \\\"hi\\\" \\\\ there\" } ] }"), table).ok,
          "a name with quotes and a backslash reads");
    const std::string written = ToJson(table, false);
    Table back;
    Check(Read(written, back).ok && back.games[0].vehicles[0].cameras[0].name == "Say \"hi\" \\ there",
          "and is written so that it reads back");
    Check(std::string(TypeName(CameraType::Chase)) == "chase" &&
              std::string(TypeName(CameraType::FirstPerson)) == "firstperson",
          "camera types as the file writes them");
}

// "eyeAdjust": read, left out when true, written back when false, and a wrong value flagged.
static void TestEyeAdjustKey()
{
    Table table;
    Check(Read(OneVehicle("{ \"cameras\": [ { \"type\": \"firstperson\", \"eyeAdjust\": false }, {} ] }"), table).ok,
          "a camera's eyeAdjust is read");
    Check(!table.games[0].vehicles[0].cameras[0].eyeAdjust && table.games[0].vehicles[0].cameras[1].eyeAdjust,
          "false where it says so, true where it is left out");
    const std::string text = ToJson(table, false);
    Check(Has(text, "\"eyeAdjust\": false") && text.find("\"eyeAdjust\": true") == std::string::npos,
          "and written back only where it is false");
    const ParseResult bad = Read(OneVehicle("{ \"cameras\": [ { \"eyeAdjust\": 3 } ] }"), table);
    Check(Flagged(bad, "eyeAdjust"), "a value that is not true or false is flagged");
}

// A SLIP COSTS ONLY ITS OWN PART: a quoted true / false or number is read as meant; a wrong value leaves its field at
// its default; a camera, tethering mode, entry or section that cannot be read is left out and the rest applies; only
// a file of which nothing can be read leaves the table in use as it was.
static void TestResilience()
{
    Table table;
    ParseResult result = Read(OneVehicle("{ \"cameras\": [ { \"type\": \"firstperson\", \"showHeadlessBody\": \"true\" },"
                                         " { \"collide\": \"False\", \"lag\": \"0.5\" } ] }"),
                              table);
    Check(result.ok && result.leftOut.empty(), "quoted true, false and numbers are read as meant");
    const Vehicle& quoted = table.games[0].vehicles[0];
    Check(quoted.cameras[0].showHeadlessBody == 1 && !quoted.cameras[1].collide && quoted.cameras[1].lag == 0.5f,
          "with the values they say");

    result = Read(OneVehicle("{ \"cameras\": [ { \"name\": \"A\", \"lag\": \"fast\", \"offset\": [1, 2, 3] } ] }"), table);
    const Camera& kept = table.games[0].vehicles[0].cameras[0];
    Check(result.ok && result.leftOut.size() == 1 && Flagged(result, "cameras[0].lag") && Flagged(result, "default"),
          "a wrong value is left out and said");
    Check(kept.name == "A" && kept.lag < 0.0f && kept.offset[0] == 1.0f && kept.offset[2] == 3.0f,
          "and only that field: it keeps its default, the camera keeps the rest");

    result = Read(OneVehicle("{ \"cameras\": [ { \"name\": \"A\" }, 5, { \"name\": \"C\" } ] }"), table);
    Check(result.ok && table.games[0].vehicles[0].cameras.size() == 2 &&
              table.games[0].vehicles[0].cameras[1].name == "C" && Flagged(result, "this camera is left out"),
          "a camera that is not one is left out, the others read");

    result = Read(OneVehicle("{ \"cameras\": [ { \"name\": \"A\", \"tethering\": [ { \"name\": \"one\" }, \"two\","
                             " { \"name\": \"three\", \"rotationTracking\": [\"spin\"] } ] } ] }"),
                  table);
    const Camera& modes = table.games[0].vehicles[0].cameras[0];
    Check(result.ok && modes.tethering.size() == 2 && modes.tethering[1].name == "three" &&
              !modes.tethering[1].rotYaw && Flagged(result, "this tethering mode is left out") &&
              Flagged(result, "tethering[2].rotationTracking"),
          "a mode that is not one is left out, and a mode's wrong axis leaves that mode at its camera's");

    result = Read("{ \"games\": { \"halo3\": { \"vehicles\": {"
                  " \"Warthog\": { \"cameras\": [ 1, 2 ] },"
                  " \"Ghost\": { \"cameras\": [ { \"name\": \"G\" } ] } } },"
                  " \"halo5\": { \"vehicles\": { } } } }",
                  table);
    Check(result.ok && table.games.size() == 1 && table.games[0].vehicles.size() == 1 &&
              table.games[0].vehicles[0].name == "Ghost" && Flagged(result, "this entry is left out") &&
              Flagged(result, "games.halo5"),
          "an entry none of whose cameras can be read, and a section for a game this does not know, are left out");

    result = Read(OneVehicle("{ \"cameras\": [ { \"name\": \"A\" } ], \"surprise\": 1, \"seat\": [\"pilot\"] }"), table);
    Check(result.ok && result.ignored.size() == 1 && Flagged(result, "Warthog.seat") &&
              table.games[0].vehicles[0].seats == 0,
          "an unknown key is said once even though the entry was read again, and a wrong seat list means any seat");

    Table before;
    Check(Read(OneVehicle("{ \"cameras\": [ { \"name\": \"Kept\" } ] }"), before).ok, "a table in use");
    Table attempt = before;
    result = Read(OneVehicle("{ \"cameras\": [ 1, 2 ] }"), attempt);
    Check(!result.ok && Has(result.error, "nothing in it could be read") && attempt.games[0].vehicles[0].cameras[0].name == "Kept",
          "a file of which nothing can be read leaves the table in use as it was");
    attempt = before;
    result = Read("{ \"games\": { \"halo3\": { \"vehicles\": { \"Warthog\": { \"cameras\": [ { } ] } } }", attempt);
    Check(!result.ok && attempt.games[0].vehicles[0].cameras[0].name == "Kept", "and so does a syntax error");
}

// "hideRegions": the regions of the player's own vehicle a camera does not draw, by name or number; one name alone
// is a list of one; a wrong value costs only that field; the file writes them back.
static void TestHideRegions()
{
    Table table;
    ParseResult result = Read(OneVehicle("{ \"cameras\": [ { \"type\": \"firstperson\", \"hideRegions\": [\"canopy\", 2] },"
                                         " { \"hideRegions\": \"glass\" }, { \"hideRegions\": null } ] }"),
                              table);
    const Vehicle& v = table.games[0].vehicles[0];
    Check(result.ok && result.leftOut.empty() && v.cameras[0].hideRegions.size() == 2 &&
              v.cameras[0].hideRegions[0] == "canopy" && v.cameras[0].hideRegions[1] == "2",
          "names and numbers are read");
    Check(v.cameras[1].hideRegions.size() == 1 && v.cameras[1].hideRegions[0] == "glass" &&
              v.cameras[2].hideRegions.empty(),
          "one name alone is a list of one; null is none");
    Check(AnyHidesRegions(table, "halo3") && !AnyHidesRegions(table, "reach"), "the section that hides is known");

    const std::string written = ToJson(table);
    Check(Has(written, "\"hideRegions\": [\"canopy\", 2]") && Has(written, "\"hideRegions\": [\"glass\"]"),
          "they are written back, numbers as numbers");
    Table again;
    Check(Read(written, again).ok && again.games[0].vehicles[0].cameras[0].hideRegions == v.cameras[0].hideRegions,
          "and read back the same");

    const char* wrong[] = {"[1.5]", "[\"\"]", "[\"a,b\"]", "[true]", "[-1]",
                           "[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\"]",
                           "\"abcdefghijklmnopqrstuvwxyzabcdefg\""};
    for (const char* value : wrong)
    {
        const std::string body = std::string("{ \"cameras\": [ { \"name\": \"A\", \"hideRegions\": ") + value + " } ] }";
        result = Read(OneVehicle(body.c_str()), table);
        const Camera& kept = table.games[0].vehicles[0].cameras[0];
        Check(result.ok && Flagged(result, "hideRegions") && kept.name == "A" && kept.hideRegions.empty(),
              "a wrong hideRegions is left out, the camera kept");
    }
    Check(!AnyHidesRegions(DefaultTable(), "halo3"), "the built-in cameras hide nothing");
}

// "hideParts": parts by their material's name (the last part of its path, or the whole path); one name alone is a
// list of one; a wrong value costs only that field; the file writes them back.
static void TestHideParts()
{
    Table table;
    ParseResult result = Read(OneVehicle("{ \"cameras\": [ { \"type\": \"firstperson\", \"hideParts\": [\"hornet_glass\","
                                         " \"objects\\\\vehicles\\\\hornet\\\\shaders\\\\hornet_glass_damage\"] },"
                                         " { \"hideParts\": \"longsword glass\" } ] }"),
                              table);
    const Vehicle& v = table.games[0].vehicles[0];
    Check(result.ok && result.leftOut.empty() && v.cameras[0].hideParts.size() == 2 &&
              v.cameras[0].hideParts[0] == "hornet_glass" &&
              v.cameras[0].hideParts[1] == "objects\\vehicles\\hornet\\shaders\\hornet_glass_damage" &&
              v.cameras[1].hideParts.size() == 1 && v.cameras[1].hideParts[0] == "longsword glass",
          "material names and paths are read; one name alone is a list of one");
    Check(AnyHidesParts(table, "halo3") && !AnyHidesRegions(table, "halo3") && !AnyHidesParts(DefaultTable(), "halo3"),
          "the section that hides parts is known, apart from regions");
    Table again;
    Check(Read(ToJson(table), again).ok && again.games[0].vehicles[0].cameras[0].hideParts == v.cameras[0].hideParts &&
              again.games[0].vehicles[0].cameras[1].hideParts == v.cameras[1].hideParts,
          "they are written back and read back the same");
    const char* wrong[] = {"[3]", "[\"\"]", "[\"a,b\"]", "[\"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuv\"]",
                           "[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\"]"};
    for (const char* value : wrong)
    {
        const std::string body = std::string("{ \"cameras\": [ { \"name\": \"A\", \"hideParts\": ") + value + " } ] }";
        result = Read(OneVehicle(body.c_str()), table);
        const Camera& kept = table.games[0].vehicles[0].cameras[0];
        Check(result.ok && Flagged(result, "hideParts") && kept.name == "A" && kept.hideParts.empty(),
              "a wrong hideParts is left out, the camera kept");
    }
}

// "fadeParts": materials by name with how much of each is kept; a wrong value costs only that field; written back.
static void TestFadeParts()
{
    Table table;
    ParseResult result = Read(OneVehicle("{ \"cameras\": [ { \"type\": \"firstperson\", \"fadeParts\":"
                                         " { \"sparrowhawk_glass\": 0.3, \"glass_dark\": \"0\" } } ] }"),
                              table);
    const Camera& c = table.games[0].vehicles[0].cameras[0];
    Check(result.ok && result.leftOut.empty() && c.fadeParts.size() == 2 && c.fadeParts[0].first == "sparrowhawk_glass" &&
              c.fadeParts[0].second == 0.3f && c.fadeParts[1].first == "glass_dark" && c.fadeParts[1].second == 0.0f,
          "names with how much is kept are read (a quoted number too)");
    Check(AnyFadesParts(table, "halo3") && !AnyHidesParts(table, "halo3"),
          "the section that fades is known, apart from hiding");
    // The built-in mod vehicles fade their cockpit glass (the Sparrowhawk's, the Longsword's); none hides a part.
    Check(AnyFadesParts(DefaultTable(), "halo3") && !AnyHidesParts(DefaultTable(), "halo3"),
          "the built-in cameras fade glass and hide nothing");
    Table again;
    Check(Read(ToJson(table), again).ok && again.games[0].vehicles[0].cameras[0].fadeParts == c.fadeParts,
          "they are written back and read back the same");
    const char* wrong[] = {"[\"glass\"]", "{ \"glass\": 1.5 }", "{ \"glass\": -0.1 }", "{ \"glass\": \"half\" }",
                           "{ \"a,b\": 0.5 }", "{ \"glass\": null }"};
    for (const char* value : wrong)
    {
        const std::string body = std::string("{ \"cameras\": [ { \"name\": \"A\", \"fadeParts\": ") + value + " } ] }";
        result = Read(OneVehicle(body.c_str()), table);
        const Camera& kept = table.games[0].vehicles[0].cameras[0];
        Check(result.ok && Flagged(result, "fadeParts") && kept.name == "A" && kept.fadeParts.empty(),
              "a wrong fadeParts is left out, the camera kept");
    }
}

int main()
{
    TestBuiltInTable();
    TestGames();
    TestCamerasAndTethering();
    TestAimMarker();
    TestLeash();
    TestMatching();
    TestCarryMode();
    TestBoundsAndErrors();
    TestEyeAdjustKey();
    TestResilience();
    TestHideRegions();
    TestHideParts();
    TestFadeParts();
    std::printf("PASS: %u vehicle camera file checks\n", g_checks);
    return 0;
}
