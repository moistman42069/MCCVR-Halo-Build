// Which vehicle camera is in use (src/common/vehicle_camera_select.h): the entry for a seat, the camera it starts
// in, stepping through cameras and tethering modes, what a seat remembers, and a reloaded file.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../src/common/vehicle_camera_select.h"

using namespace vehicle_camera_file;
using namespace vehicle_camera_select;

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

static Table Read(const std::string& text)
{
    Table table;
    const ParseResult result = FromJson(text.c_str(), text.size(), table);
    if (!result.ok)
        std::fprintf(stderr, "test table: %s\n", result.error.c_str());
    Check(result.ok, "the test's own table parses");
    return table;
}

// A Halo 3 section: a Warthog entry for the driver and one for the gunner, a Ghost switched off, and a default.
static std::string Halo3Table(const char* warthogCameras)
{
    return std::string("{ \"games\": { \"halo3\": { \"vehicles\": {"
                       "  \"Warthog\": { \"defaultCamera\": 1, \"defaultMode\": 1, \"cameras\": ") +
           warthogCameras +
           " },"
           "  \"Warthog gunner\": { \"match\": [\"warthog\"], \"seat\": [\"gunner\"], \"cameras\": ["
           "     { \"name\": \"Turret\", \"origin\": \"turret_marker\", \"offset\": [-200, 0, 90] } ] },"
           "  \"Ghost\": { \"enabled\": false },"
           "  \"default\": { \"cameras\": [ { \"name\": \"Seat\", \"type\": \"firstperson\" } ] }"
           " } } } }";
}

static const char* const kWarthogCameras =
    "[ { \"name\": \"Seat\", \"type\": \"firstperson\" },"
    "  { \"name\": \"Behind\", \"offset\": [-450, 0, 180], \"leashMax\": [null, null, 360],"
    "    \"tethering\": [ { \"name\": \"Untethered\" },"
    "                     { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"] } ] },"
    "  { \"name\": \"High\", \"offset\": [-700, 0, 420], \"collide\": false,"
    "    \"tethering\": [ { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"] },"
    "                     { \"name\": \"Untethered\" } ] } ]";

static void TestBuiltInTable()
{
    Selector selector;
    Check(!selector.Active().valid, "nothing is selected before the player sits");
    Check(selector.StandDownReason() == "on foot", "the reason before the player sits is that he is on foot");
    Check(selector.Describe().empty(), "nothing to describe on foot");
    Check(!selector.Update("halo3", false, "", 0), "staying on foot changes nothing");
    Check(!selector.StepCamera(1) && !selector.StepMode(1), "there is nothing to step on foot");

    // A vehicle the built-in table has no entry for: its "default" entry.
    Check(selector.Update("halo3", true, "unidentified", kSeatDriver), "sitting down selects a camera");
    const ActiveCamera seat = selector.Active();
    Check(seat.valid && !seat.chase, "the built-in table starts in the seat view");
    Check(seat.camera == 0 && seat.cameraCount == 2 && seat.modeCount == 2 && seat.mode == 0 && !seat.rotYaw &&
              seat.rotationSet,
          "the seat view is camera 1 of 2, in its first mode, Untethered");
    Check(seat.changes == 1, "sitting down counts as a change");
    Check(selector.StandDownReason().empty(), "no stand-down reason while a camera is selected");
    Check(selector.Describe() == "default (Driver): camera 1 of 2 \"Seat\", the seat view, your body shown without its head",
          "the seat view's log line");
    Check(!selector.Update("halo3", true, "unidentified", kSeatDriver), "the same seat again changes nothing");
    Check(selector.Active() == seat, "and publishes the same record");
    Check(selector.StepMode(1) && selector.Active().rotYaw && selector.Active().mode == 1,
          "the seat view steps to Tethered, which turns the view with the vehicle");
    Check(selector.StepMode(1) && !selector.Active().rotYaw && selector.Active().mode == 0, "and back to Untethered");
    Check(!selector.StepCamera(0), "a step of zero is no step");

    Check(selector.StepCamera(1), "stepping to the next camera");
    const ActiveCamera behind = selector.Active();
    Check(behind.valid && behind.chase && behind.camera == 1, "the second camera is a chase camera");
    Check(behind.mode == 0 && behind.modeCount == 2, "it starts in its first mode of two");
    Check(behind.offsetCm[0] == 0.0f && behind.offsetCm[1] == 0.0f && behind.offsetCm[2] == 0.0f,
          "its offset is the file's");
    Check(behind.locYaw && !behind.locPitch && !behind.locRoll && !behind.locView,
          "its position turns with the vehicle and stays level");
    Check(!behind.rotYaw && !behind.rotPitch && !behind.rotRoll, "its first mode does not turn the view");
    Check(behind.origin == static_cast<uint8_t>(Origin::ThirdPerson) && behind.marker[0] == '\0',
          "it is measured from where the game's own third-person camera rests and names no marker");
    Check(behind.collide && behind.collideMarginCm == 30.0f, "it is pulled in from obstacles by the default margin");
    Check(behind.changes == 4, "a step counts as a change (sitting down, the seat view's two mode steps, this one)");
    Check(selector.Describe() ==
              "default (Driver): camera 2 of 2 \"Third Person\", mode 1 of 2 \"Untethered\", chase from thirdperson, "
              "offset 0 0 0 cm, location Yaw, rotation None, leash None, lag 0.5 s",
          "the chase camera's log line");
    Check(behind.lagSeconds == 0.5f && behind.lagDamping == 1.5f, "it follows on a spring, without overshoot");

    Check(!behind.aimMarker, "no aim ring while the world holds still");
    Check(selector.StepMode(1), "stepping to the next mode");
    Check(selector.Active().mode == 1 && selector.Active().rotYaw, "the second mode turns the view with the vehicle");
    Check(selector.Active().aimMarker && Has(selector.Describe(), "lag 0.5 s, aim ring"),
          "and shows the ring where the vehicle aims, and its line says so");
    Check(selector.Active().changes == 5, "a mode step counts as a change");
    Check(selector.StepMode(1) && selector.Active().mode == 0, "the modes wrap round forwards");
    Check(selector.StepMode(-1) && selector.Active().mode == 1, "and backwards");

    Check(selector.StepCamera(1) && selector.Active().camera == 0, "the cameras wrap round");
    Check(selector.StepCamera(-1) && selector.Active().camera == 1, "and backwards");
    Check(selector.Active().mode == 1,
          "the tethering choice survives a pass through the seat view, which has no modes of its own");
}

static void TestSeatMemory()
{
    Selector selector;
    selector.Update("halo3", true, "warthog", kSeatDriver);
    selector.StepCamera(1);
    selector.StepMode(1);
    const uint32_t seatedChanges = selector.Active().changes;

    Check(selector.Update("halo3", false, "", 0), "getting out changes the published camera");
    Check(!selector.Active().valid, "nothing is selected on foot");
    Check(selector.Active().changes == seatedChanges + 1, "getting out counts as a change");
    Check(selector.StandDownReason() == "on foot", "the reason on foot");
    Check(!selector.Update("halo3", false, "", 0), "staying out changes nothing");
    Check(selector.Active().changes == seatedChanges + 1, "and does not count again");

    Check(selector.Update("halo3", true, "warthog", kSeatDriver), "getting back in");
    Check(selector.Active().camera == 1 && selector.Active().mode == 1,
          "a seat starts in the camera and mode the player last chose there");
    Check(selector.Active().changes == seatedChanges + 2, "getting in counts as a change");

    selector.Update("reach", false, "", 0);
    Check(selector.Update("reach", true, "warthog", kSeatDriver), "the same seat in another game");
    Check(selector.Active().camera == 0, "a choice made in one game is not another game's");

    // The seat view was never chosen by a step in this entry, so a seat with no memory takes the defaults.
    Selector fresh;
    fresh.SetTable(Read(Halo3Table(kWarthogCameras)));
    Check(fresh.Update("halo3", true, "Warthog", kSeatDriver), "a seat with defaults in the file");
    Check(fresh.Active().camera == 1 && fresh.Active().mode == 1, "starts in defaultCamera and defaultMode");
    fresh.StepCamera(-1);
    fresh.Update("halo3", false, "", 0);
    fresh.Update("halo3", true, "Warthog", kSeatDriver);
    Check(fresh.Active().camera == 0, "and after a step, in the camera the player left it in instead");
}

static void TestEntriesAndSeats()
{
    Selector selector;
    selector.SetTable(Read(Halo3Table(kWarthogCameras)));

    Check(selector.Update("halo3", true, "Warthog", 0), "a Warthog seat the game has not named yet");
    Check(Has(selector.Describe(), "Warthog: camera 2 of 3 \"Behind\""),
          "takes the entry without a seat list, and says nothing about the seat");
    const ActiveCamera unnamed = selector.Active();
    Check(unnamed.leashMaxCm[2] == 360.0f && unnamed.leashMaxCm[0] >= kUnleashed,
          "the camera's leash is published, an unlimited side left unlimited");
    Check(Has(selector.Describe(), "leash up at most 360 cm"), "and described");

    Check(!selector.Update("halo3", true, "Warthog", kSeatDriver), "the game naming the seat a driver's");
    Check(selector.Active() == unnamed, "keeps the same entry's camera where it is");
    Check(Has(selector.Describe(), "Warthog (Driver): camera 2 of 3"), "and the line now names the seat");

    Check(selector.Update("halo3", true, "Warthog", kSeatGunner), "moving to the gunner's seat");
    const ActiveCamera gunner = selector.Active();
    Check(Has(selector.Describe(), "Warthog gunner (Gunner): camera 1 of 1 \"Turret\", chase from turret_marker"),
          "takes the gunner's entry");
    Check(gunner.origin == static_cast<uint8_t>(Origin::Marker) && std::strcmp(gunner.marker, "turret_marker") == 0,
          "a marker origin publishes the marker's name");
    Check(gunner.changes == unnamed.changes + 1, "changing seat counts as a change");
    Check(!selector.StepCamera(1), "an entry with one camera has nothing to step to");
    Check(!selector.StepMode(1), "a camera with no tethering list has one mode");
    Check(!Has(selector.Describe(), "mode "), "and its line names no mode");

    Check(selector.Update("halo3", true, "Ghost", kSeatDriver), "a vehicle whose entry is switched off");
    Check(!selector.Active().valid, "selects nothing");
    Check(Has(selector.StandDownReason(), "\"Ghost\"") && Has(selector.StandDownReason(), "\"enabled\": false"),
          "and says which entry switched it off");
    Check(selector.Describe().empty(), "with nothing to describe");
    Check(!selector.StepCamera(1), "and nothing to step");

    Check(selector.Update("halo3", true, "Mongoose", kSeatPassenger), "a vehicle with no entry of its own");
    Check(Has(selector.Describe(), "default (Passenger): camera 1 of 1 \"Seat\", the seat view"),
          "takes the default entry");

    Check(selector.Update("odst", true, "Warthog", kSeatDriver), "a game with no section of its own and no default section");
    Check(!selector.Active().valid && Has(selector.StandDownReason(), "no section for this game"),
          "selects nothing and says why");

    Selector empty;
    empty.SetTable(Read("{ \"games\": { \"halo3\": { \"vehicles\": {"
                        " \"Ghost\": { \"cameras\": [ { \"name\": \"A\" } ] } } } } }"));
    empty.Update("halo3", true, "Warthog", kSeatDriver);
    Check(!empty.Active().valid && Has(empty.StandDownReason(), "no entry for this vehicle"),
          "a section with no matching entry and no default selects nothing and says why");
    Check(!empty.Update(nullptr, true, nullptr, 0) || !empty.Active().valid,
          "a null game key and vehicle name are taken as empty text");
}

static void TestLongMarkerName()
{
    const std::string longName(80, 'm');
    Selector selector;
    selector.SetTable(Read("{ \"games\": { \"default\": { \"vehicles\": { \"default\": { \"cameras\": ["
                           " { \"origin\": \"" + longName + "\" } ] } } } } }"));
    selector.Update("halo3", true, "anything", kSeatDriver);
    const ActiveCamera& active = selector.Active();
    Check(active.valid && active.origin == static_cast<uint8_t>(Origin::Marker), "a long marker name still selects");
    Check(std::strlen(active.marker) == static_cast<std::size_t>(kMarkerCapacity - 1),
          "and is cut to fit the record, with its terminator");
    Check(Has(selector.Describe(), "default (Driver): camera 1 of 1, chase from mmm"),
          "an unnamed camera is described by its number alone");
}

static void TestReload()
{
    Selector selector;
    selector.SetTable(Read(Halo3Table(kWarthogCameras)));
    selector.Update("halo3", true, "Warthog", kSeatDriver);
    const ActiveCamera before = selector.Active(); // Behind, Tethered
    Check(before.camera == 1 && before.mode == 1 && before.rotYaw, "the starting point: Behind, Tethered");

    // Only a number edited.
    selector.SetTable(Read(Halo3Table(
        "[ { \"name\": \"Seat\", \"type\": \"firstperson\" },"
        "  { \"name\": \"Behind\", \"offset\": [-500, 20, 200],"
        "    \"tethering\": [ { \"name\": \"Untethered\" },"
        "                     { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"] } ] } ]")));
    Check(selector.Active().camera == 1 && selector.Active().mode == 1, "an edited number keeps the camera and mode");
    Check(selector.Active().offsetCm[0] == -500.0f && selector.Active().offsetCm[1] == 20.0f,
          "and publishes the new number");
    Check(selector.Active().cameraCount == 2, "and the new camera count");
    Check(selector.Active().changes == before.changes, "without counting as a change");

    // Cameras and modes moved round in the file.
    selector.SetTable(Read(Halo3Table(
        "[ { \"name\": \"Behind\", \"offset\": [-500, 20, 200],"
        "    \"tethering\": [ { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"] },"
        "                     { \"name\": \"Untethered\" } ] },"
        "  { \"name\": \"Seat\", \"type\": \"firstperson\" } ]")));
    Check(selector.Active().camera == 0 && selector.Active().mode == 0 && selector.Active().rotYaw,
          "a camera and mode that moved in the file are followed by name");
    Check(selector.Active().changes == before.changes, "without counting as a change");

    // Renamed where they stand.
    selector.SetTable(Read(Halo3Table(
        "[ { \"name\": \"Chase\", \"offset\": [-500, 20, 200],"
        "    \"tethering\": [ { \"name\": \"Follow\", \"rotationTracking\": [\"yaw\"] },"
        "                     { \"name\": \"Untethered\" } ] },"
        "  { \"name\": \"Seat\", \"type\": \"firstperson\" } ]")));
    Check(selector.Active().camera == 0 && selector.Active().mode == 0,
          "a renamed camera and mode are kept by their position");
    Check(selector.Active().changes == before.changes, "without counting as a change");
    Check(Has(selector.Describe(), "\"Chase\", mode 1 of 2 \"Follow\""), "and described by their new names");

    // The mode in use removed: the nearest the camera still has.
    selector.SetTable(Read(Halo3Table(
        "[ { \"name\": \"Chase\", \"offset\": [-500, 20, 200],"
        "    \"tethering\": [ { \"name\": \"Loose\" },"
        "                     { \"name\": \"Tight\", \"rotationTracking\": [\"yaw\"] } ] },"
        "  { \"name\": \"Seat\", \"type\": \"firstperson\" } ]")));
    Check(selector.Active().mode == 0, "a mode renamed where it stands is still kept by position");
    selector.StepMode(1); // Tight
    const uint32_t tight = selector.Active().changes;
    selector.SetTable(Read(Halo3Table(
        "[ { \"name\": \"Chase\", \"offset\": [-500, 20, 200] },"
        "  { \"name\": \"Seat\", \"type\": \"firstperson\" } ]")));
    Check(selector.Active().camera == 0 && selector.Active().mode == 0 && selector.Active().modeCount == 1,
          "a mode that is gone leaves the player in the camera");
    Check(selector.Active().changes == tight + 1, "and that counts as a change");

    // The camera in use became another kind.
    const uint32_t chase = selector.Active().changes;
    selector.SetTable(Read(Halo3Table(
        "[ { \"name\": \"Chase\", \"type\": \"firstperson\" },"
        "  { \"name\": \"Seat\", \"type\": \"firstperson\" } ]")));
    Check(!selector.Active().chase && selector.Active().changes == chase + 1,
          "a camera that became the seat view counts as a change");

    // The camera in use removed, and nothing left at its position.
    selector.StepCamera(1); // Seat, camera 2 of 2
    const uint32_t second = selector.Active().changes;
    selector.SetTable(Read(Halo3Table("[ { \"name\": \"Only\", \"offset\": [-300, 0, 100] } ]")));
    Check(selector.Active().valid && selector.Active().camera == 0 && selector.Active().cameraCount == 1,
          "a camera that is gone puts the player in the entry's starting camera");
    Check(selector.Active().changes == second + 1, "and that counts as a change");

    // The entry switched off, then on again.
    const uint32_t on = selector.Active().changes;
    selector.SetTable(Read("{ \"games\": { \"halo3\": { \"vehicles\": { \"Warthog\": { \"enabled\": false } } } } }"));
    Check(!selector.Active().valid && selector.Active().changes == on + 1,
          "an entry switched off in the file stands the camera down");
    selector.SetTable(Read(Halo3Table(kWarthogCameras)));
    Check(selector.Active().valid && selector.Active().changes == on + 2,
          "and switched on again selects one, each counting as a change");

    // A reload on foot waits for the next seat.
    selector.Update("halo3", false, "", 0);
    const ActiveCamera onFoot = selector.Active();
    selector.SetTable(DefaultTable());
    Check(selector.Active() == onFoot && selector.StandDownReason() == "on foot", "a reload on foot selects nothing");
    selector.Update("halo3", true, "Warthog", kSeatDriver);
    Check(Has(selector.Describe(), "Warthog (Driver)"), "and the next seat uses the new table");
}

// The built-in table's own cameras for the vehicles the mod knows: each seat of a vehicle has its entry, every
// one starting in the seat view with the game's third-person camera second, and a vehicle it does not know gets
// the default entry's two.
static void TestBuiltInPresets()
{
    Selector selector;
    selector.SetTable(DefaultTable());
    selector.Update("halo3", true, "warthog", kSeatDriver);
    Check(Has(selector.Describe(), "Warthog (Driver): camera 1 of 4 \"Seat\""), "a Warthog driver has four cameras");
    Check(selector.Note() == L"Warthog \u00B7 Seat, Untethered",
          "the headset note names the vehicle, the camera and its mode");
    Check(selector.StepCamera(1) && Has(selector.Describe(), "\"Third Person\""), "the second the game's own");
    Check(selector.Note() == L"Warthog \u00B7 Third Person, Untethered", "and the mode where there is more than one");
    Check(selector.TuningKey("halo3") == "halo3\tWarthog\tThird Person", "lag tuning keys the camera by game, entry, name");
    Check(selector.StepCamera(1) && Has(selector.Describe(), "\"Over the Shoulder\"") &&
              selector.Active().lagSeconds > 0.0f && selector.Active().origin == static_cast<uint8_t>(Origin::Seat),
          "then one over the shoulder, from the seat, on a spring");
    selector.Update("halo3", true, "warthog", kSeatPassenger);
    Check(Has(selector.Describe(), "Warthog passenger (Passenger)"), "the passenger has an entry of its own");
    selector.Update("halo3", true, "warthog", kSeatGunner);
    Check(Has(selector.Describe(), "Warthog gunner (Gunner): camera 1 of 3"), "and so has the gunner");
    selector.Update("halo3", true, "banshee", kSeatDriver);
    Check(selector.StepCamera(1) && selector.StepCamera(1) && selector.Active().locPitch && selector.Active().locRoll,
          "a flyer's own cameras ride its pitch and roll");
    selector.Update("halo3", true, "unidentified", kSeatDriver);
    Check(Has(selector.Describe(), "default (Driver): camera 1 of 2"), "a vehicle it does not know gets two");
    Check(selector.Note() == L"Unidentified \u00B7 Seat, Untethered",
          "and its note names the vehicle, not the default entry");
    selector.Update("reach", true, "warthog", kSeatDriver);
    Check(Has(selector.Describe(), "default (Driver): camera 1 of 2"), "and another game the default section's");
}

static void TestTwoCamerasOfOneName()
{
    Selector selector;
    const char* const twins = "[ { \"name\": \"Cam\", \"offset\": [-100, 0, 0] },"
                              "  { \"name\": \"Cam\", \"offset\": [-200, 0, 0] } ]";
    selector.SetTable(Read(Halo3Table(twins))); // defaultCamera 1
    selector.Update("halo3", true, "Warthog", kSeatDriver);
    Check(selector.Active().camera == 1 && selector.Active().offsetCm[0] == -200.0f, "the second of two same-named cameras");
    selector.SetTable(Read(Halo3Table(twins)));
    Check(selector.Active().camera == 1, "is still the second after a reload");
}

static void TestCarryAcrossCameras()
{
    Selector selector;
    selector.SetTable(Read(Halo3Table(kWarthogCameras)));
    selector.Update("halo3", true, "Warthog", kSeatDriver); // Behind, Tethered (mode 1)
    Check(selector.StepCamera(1), "stepping from Behind to High");
    Check(selector.Active().camera == 2 && selector.Active().mode == 0 && selector.Active().rotYaw,
          "lands in High's mode of the same name, wherever it is in its list");
    Check(!selector.Active().collide, "a camera's own collide setting is published");
    selector.StepMode(1); // High, Untethered
    selector.StepCamera(1); // Seat
    selector.StepCamera(1); // Behind
    Check(selector.Active().camera == 1 && selector.Active().mode == 0 && !selector.Active().rotYaw,
          "and the choice last made carries through the seat view to the next camera");
}

static void TestNoSeatView()
{
    Selector selector;
    Check(selector.Update("halo3", true, "unidentified", kSeatDriver, false), "a vehicle the game has no seat view for");
    Check(selector.Active().chase && selector.Active().camera == 1,
          "starts in the first chase camera when the entry's default is the seat view");
    Check(selector.StepCamera(1) && !selector.Active().chase, "and can still step to the seat view");
    selector.Update("halo3", false, "", 0);
    selector.Update("halo3", true, "unidentified", kSeatDriver, false);
    Check(!selector.Active().chase, "where the player left it, the next time");

    Selector known;
    known.Update("halo3", true, "warthog", kSeatDriver, true);
    Check(!known.Active().chase, "a vehicle with a seat view starts in it");
}

// "showHeadlessBody": a firstperson camera draws the seated body without its head -- by default in a driver's or a
// gunner's seat, never by default in a passenger's (who may hold a weapon, whose arms would then be drawn twice).
// Read, published, described, written back; and the table says whether any camera can ask for it (what installs the
// body drawing at all).
// "hideRegions": published joined by commas for the cold worker to resolve against the vehicle's regions, and
// described; a camera without it hides nothing.
static void TestHideRegions()
{
    const std::string text =
        "{ \"games\": { \"halo3\": { \"vehicles\": { \"sparrowhawk\": { \"cameras\": ["
        " { \"name\": \"Seat\", \"type\": \"firstperson\", \"hideRegions\": [\"canopy\", 3],"
        " \"hideParts\": [\"glass_dark\", \"sparrowhawk_glass\"], \"fadeParts\": { \"sparrowhawk_glass\": 0.25 } },"
        " { \"name\": \"Behind\", \"type\": \"chase\" } ] } } } } }";
    Selector selector;
    selector.SetTable(Read(text));
    selector.Update("halo3", true, "sparrowhawk", kSeatDriver, true);
    Check(selector.Active().valid && std::string(selector.Active().hideRegions) == "canopy,3",
          "a camera's hideRegions is published joined by commas");
    Check(Has(selector.Describe(), "hiding canopy,3"), "and described");
    Check(std::string(selector.Active().hideParts) == "glass_dark,sparrowhawk_glass" &&
              Has(selector.Describe(), "hiding parts glass_dark,sparrowhawk_glass"),
          "its hideParts likewise");
    Check(std::string(selector.Active().fadeParts) == "sparrowhawk_glass" && selector.Active().fadeKept[0] == 0.25f &&
              Has(selector.Describe(), "fading sparrowhawk_glass"),
          "its fadeParts with how much is kept");
    selector.StepCamera(1);
    Check(selector.Active().chase && selector.Active().hideRegions[0] == '\0' && selector.Active().hideParts[0] == '\0',
          "a camera without them hides nothing");
}

// "eyeAdjust": false holds the exposure for that camera, of either type; published, described; a camera without
// it leaves the game's adaptation alone.
static void TestEyeAdjust()
{
    const std::string text =
        "{ \"games\": { \"halo3\": { \"vehicles\": { \"longsword\": { \"cameras\": ["
        " { \"name\": \"Seat\", \"type\": \"firstperson\", \"eyeAdjust\": false },"
        " { \"name\": \"Behind\", \"type\": \"chase\" },"
        " { \"name\": \"Far\", \"type\": \"chase\", \"eyeAdjust\": false } ] } } } } }";
    Selector selector;
    selector.SetTable(Read(text));
    selector.Update("halo3", true, "longsword", kSeatDriver, true);
    Check(selector.Active().valid && !selector.Active().chase && selector.Active().holdExposure &&
              Has(selector.Describe(), "exposure held"),
          "a firstperson camera with eyeAdjust false holds the exposure, and the log says so");
    selector.StepCamera(1);
    Check(selector.Active().chase && !selector.Active().holdExposure && !Has(selector.Describe(), "exposure"),
          "a camera without it leaves the game's adaptation alone");
    selector.StepCamera(1);
    Check(selector.Active().chase && selector.Active().holdExposure && Has(selector.Describe(), "exposure held"),
          "and a chase camera can hold it too");
}

static void TestShowBody()
{
    const std::string text =
        "{ \"games\": { \"halo3\": { \"vehicles\": { \"mongoose\": { \"cameras\": ["
        " { \"name\": \"Seat\", \"type\": \"firstperson\" },"
        " { \"name\": \"Seat as it is\", \"type\": \"firstperson\", \"showHeadlessBody\": false },"
        " { \"name\": \"Behind\", \"type\": \"chase\", \"showHeadlessBody\": true },"
        " { \"name\": \"Seat with body\", \"type\": \"firstperson\", \"showHeadlessBody\": true } ] } } } } }";
    Selector selector;
    selector.SetTable(Read(text));
    selector.Update("halo3", true, "mongoose", kSeatDriver, true);
    Check(selector.Active().valid && !selector.Active().chase && selector.Active().showHeadlessBody,
          "a driver's firstperson camera shows the headless body by default");
    Check(Has(selector.Describe(), "the seat view, your body shown without its head"), "and the log says so");
    selector.StepCamera(1);
    Check(!selector.Active().chase && !selector.Active().showHeadlessBody && !Has(selector.Describe(), "body"),
          "one that says false is the game's own seat view");
    selector.StepCamera(1);
    Check(selector.Active().chase && !selector.Active().showHeadlessBody, "a chase camera is not changed by it");

    Selector gunner;
    gunner.SetTable(Read(text));
    gunner.Update("halo3", true, "mongoose", kSeatGunner, true);
    Check(gunner.Active().showHeadlessBody, "a gunner's too");

    Selector passenger;
    passenger.SetTable(Read(text));
    passenger.Update("halo3", true, "mongoose", kSeatPassenger, true);
    Check(!passenger.Active().chase && !passenger.Active().showHeadlessBody,
          "a passenger's firstperson camera does not by default");
    passenger.StepCamera(1);
    passenger.StepCamera(1);
    passenger.StepCamera(1);
    Check(!passenger.Active().chase && passenger.Active().showHeadlessBody, "unless it says true");

    const Table table = Read(text);
    Check(AnyShowsHeadlessBody(table, "halo3"), "the table asks for the body drawing");
    Check(!AnyShowsHeadlessBody(table, "halo2"), "for its own game only");
    const std::string written = ToJson(table, false);
    Check(Has(written, "\"name\": \"Seat\", \"type\": \"firstperson\" }") &&
              Has(written, "\"name\": \"Seat as it is\", \"type\": \"firstperson\", \"showHeadlessBody\": false }") &&
              Has(written, "\"name\": \"Seat with body\", \"type\": \"firstperson\", \"showHeadlessBody\": true }"),
          "written back as it was said, and left out where it was left out");
    Table again;
    Check(FromJson(written.c_str(), written.size(), again).ok &&
              again.games[0].vehicles[0].cameras[0].showHeadlessBody < 0 &&
              again.games[0].vehicles[0].cameras[1].showHeadlessBody == 0 &&
              again.games[0].vehicles[0].cameras[3].showHeadlessBody == 1,
          "and read back the same");

    const Table passengersOnly = Read(
        "{ \"games\": { \"halo3\": { \"vehicles\": { \"mongoose\": { \"seat\": [\"passenger\"], \"cameras\": ["
        " { \"type\": \"firstperson\" }, { \"type\": \"chase\", \"showHeadlessBody\": true } ] } } } } }");
    Check(!AnyShowsHeadlessBody(passengersOnly, "halo3"),
          "an entry for passengers only, and a chase camera's key, ask for nothing");
    const Table allFalse = Read(
        "{ \"games\": { \"halo3\": { \"vehicles\": { \"mongoose\": { \"cameras\": ["
        " { \"type\": \"firstperson\", \"showHeadlessBody\": false } ] } } } } }");
    Check(!AnyShowsHeadlessBody(allFalse, "halo3"), "nor does a camera that says false");
}

// A FIRSTPERSON CAMERA'S OFFSET: the seat view, with its eye moved; nothing else of the placement fields.
static void TestFirstPersonOffset()
{
    const std::string text =
        "{ \"games\": { \"halo3\": { \"vehicles\": { \"seraph_reach\": { \"cameras\": ["
        " { \"name\": \"Seat\", \"type\": \"firstperson\", \"offset\": [12, -3, 8] },"
        " { \"name\": \"Seat as it is\", \"type\": \"firstperson\" } ] } } } } }";
    Selector selector;
    selector.SetTable(Read(text));
    selector.Update("halo3", true, "seraph_reach", kSeatDriver, true);
    const ActiveCamera& active = selector.Active();
    Check(active.valid && !active.chase, "a firstperson camera with an offset is still the seat view");
    Check(active.offsetCm[0] == 12.0f && active.offsetCm[1] == -3.0f && active.offsetCm[2] == 8.0f,
          "and carries its offset to the camera thread");
    Check(Has(selector.Describe(), "the seat view, its eye moved 12 -3 8 cm (forward, right, up)"),
          "the log says how far its eye is moved");
    selector.StepCamera(1);
    Check(!selector.Active().chase && selector.Active().offsetCm[0] == 0.0f && selector.Active().offsetCm[2] == 0.0f &&
              !Has(selector.Describe(), "moved"),
          "one without an offset is the seat's eye itself");

    // Written back as it was read: the offset kept, the fields it does not use left out.
    const Table table = Read(text);
    const std::string written = ToJson(table, false);
    Check(Has(written, "\"type\": \"firstperson\", \"offset\": [12, -3, 8]") && !Has(written, "\"lag\": 0,"),
          "the file is written back with the offset and nothing it does not use");
    Table again;
    Check(FromJson(written.c_str(), written.size(), again).ok &&
              again.games[0].vehicles[0].cameras[0].offset[1] == -3.0f,
          "and reads back the same");
}

static void TestMemoryPerVehicleAndSeat()
{
    Selector selector;
    selector.Update("halo3", true, "warthog", kSeatDriver);
    selector.StepCamera(1);
    selector.Update("halo3", true, "ghost", kSeatDriver);
    Check(selector.Active().camera == 0, "one default entry still keeps each vehicle's own choice");
    selector.Update("halo3", true, "warthog", kSeatPassenger);
    Check(selector.Active().camera == 0, "and each seat's");
    selector.Update("halo3", true, "warthog", kSeatDriver);
    Check(selector.Active().camera == 1, "the Warthog driver's choice is kept");
    Check(selector.MemoryDirty(), "a choice marks the memory to be saved");
}

static void TestControlChoice()
{
    Selector selector;
    Check(selector.ControlChoice(1) == -1, "on foot there is no control choice");
    selector.Update("halo3", true, "warthog", kSeatDriver);
    Check(selector.ControlChoice(1) == -1, "a seat starts with none");
    selector.MemorySaved();
    selector.SetControlChoice(2, 1);
    Check(selector.ControlChoice(1) == 2 && selector.MemoryDirty(), "a choice is kept against its base");
    selector.Update("halo3", true, "ghost", kSeatDriver);
    Check(selector.ControlChoice(1) == -1, "per vehicle");
    selector.Update("halo3", true, "warthog", kSeatDriver);
    Check(selector.ControlChoice(1) == 2, "and back in the Warthog it is there");
    Check(selector.ControlChoice(3) == -1, "a base changed since forgets it");
    Check(selector.ControlChoice(1) == -1, "for good");
    selector.SetControlChoice(1, 1);
    Check(selector.ControlChoice(1) == -1, "a choice equal to its base is no choice");
}

static void TestMemoryFile()
{
    Selector selector;
    selector.SetTable(Read(Halo3Table(kWarthogCameras)));
    selector.Update("halo3", true, "Warthog", kSeatDriver);
    selector.StepCamera(1); // High, Tethered (carried)
    selector.SetControlChoice(2, 1);
    selector.Update("halo3", true, "Warthog", kSeatGunner);
    selector.SetControlChoice(3, 1);
    const std::string text = selector.MemoryText();
    Check(Has(text, "halo3\tWarthog\tDriver\tWarthog\t3\tHigh\t1\tTethered\tstick\tmotion"), "the driver's line");
    Check(Has(text, "halo3\tWarthog\tGunner\t\t0\t\t0\t\twheel\tmotion"),
          "the gunner's line: controls chosen, no camera stepped to");
    Check(text.rfind("# ", 0) == 0, "the file starts with what it is");

    Selector fresh;
    fresh.SetTable(Read(Halo3Table(kWarthogCameras)));
    Check(fresh.LoadMemory(text + "not a line of ours\r\nhalo3\tGhost\r\n") == 2, "two seats read back, the rest skipped");
    Check(!fresh.MemoryDirty(), "reading the file is not a change to save");
    fresh.Update("halo3", true, "Warthog", kSeatDriver);
    Check(fresh.Active().camera == 2 && fresh.Active().mode == 0 && fresh.Active().rotYaw,
          "the next session starts the seat where it was left");
    Check(fresh.ControlChoice(1) == 2, "with its controls");
    fresh.Update("halo3", true, "Warthog", kSeatGunner);
    Check(fresh.ControlChoice(1) == 3, "each seat with its own");

    // A seat whose remembered entry is not the one it is matched to now starts in that entry's defaults.
    Selector moved;
    moved.SetTable(Read(Halo3Table(kWarthogCameras)));
    moved.LoadMemory("halo3\tWarthog\tDriver\tsomething else\t3\tHigh\t1\tTethered\tstick\tmotion\r\n");
    moved.Update("halo3", true, "Warthog", kSeatDriver);
    Check(moved.Active().camera == 1, "another entry's memory is not this entry's camera");
    Check(moved.ControlChoice(1) == 2, "though the controls are the seat's own");

    // A choice made while the vehicle rode on "default" carries into an entry added for it since, which starts
    // with the default entry's cameras.
    Selector added;
    added.LoadMemory("halo3\tseraph_reach\tDriver\tdefault\t2\tThird Person\t1\tUntethered\t\t\r\n");
    Table withOwn = DefaultTable();
    for (Game& game : withOwn.games)
    {
        if (game.key != "halo3")
            continue;
        for (const Vehicle& vehicle : game.vehicles)
            if (vehicle.isDefault)
            {
                Vehicle own = vehicle;
                own.isDefault = false;
                own.name = "seraph_reach";
                own.match = {"seraph_reach"};
                game.vehicles.push_back(own);
                break;
            }
    }
    added.SetTable(withOwn);
    added.Update("halo3", true, "seraph_reach", kSeatDriver);
    Check(added.Active().camera == 1, "a choice made on default carries into the vehicle's added entry");

    // Seated when the entry is added (the file is read again under the player): he stays in his camera, and it
    // is not a new ride.
    Selector seated;
    seated.Update("halo3", true, "seraph_reach", kSeatDriver);
    seated.StepCamera(1);
    const uint32_t changes = seated.Active().changes;
    const int camera = seated.Active().camera;
    seated.SetTable(withOwn);
    seated.Update("halo3", true, "seraph_reach", kSeatDriver);
    Check(seated.Active().camera == camera && seated.Active().changes == changes,
          "an entry added under a seated player keeps his camera, with no change counted");
}

// THE BOOM'S MEMORY: per game, vehicle, seat and camera, on lines of their own that an older build skips.
static void TestBoomMemory()
{
    Selector selector;
    selector.Update("halo3", true, "Warthog", kSeatDriver);
    float degrees = 0.0f;
    Check(selector.Active().camera == 0 && !selector.BoomPitch(degrees), "the seat view has no boom");
    selector.SetBoomPitch(30.0f, false);
    Check(!selector.BoomPitch(degrees), "and keeps none");
    selector.StepCamera(1);
    Check(selector.Active().origin == static_cast<uint8_t>(Origin::ThirdPerson) && !selector.BoomPitch(degrees),
          "the third-person camera starts with none kept");
    Check(!BoomPitchSet(selector.Active().boomPitchDeg), "and the built-in camera names no angle of its own");
    selector.MemorySaved();
    selector.SetBoomPitch(25.0f, false);
    Check(selector.MemoryDirty() && selector.BoomPitch(degrees) && degrees == 25.0f, "the player's angle is kept");
    selector.MemorySaved();
    selector.SetBoomPitch(25.02f, false);
    Check(!selector.MemoryDirty(), "a still boom does not keep the file dirty");
    selector.StepCamera(1);
    Check(!selector.BoomPitch(degrees), "each camera keeps its own");
    selector.StepCamera(-1);
    Check(selector.BoomPitch(degrees) && degrees == 25.0f, "and has it back");
    selector.Update("halo3", true, "Warthog", kSeatPassenger);
    selector.StepCamera(1);
    Check(!selector.BoomPitch(degrees), "each seat too");
    selector.Update("halo3", true, "Warthog", kSeatDriver);
    Check(selector.BoomPitch(degrees) && degrees == 25.0f, "getting back in the seat finds it");

    const std::string text = selector.MemoryText();
    Check(Has(text, "boom\thalo3\tWarthog\tDriver\tWarthog\tThird Person\t25.0\r\n"), "the boom's line");
    Check(Has(text, "# boom\t"), "the file says what the line is");
    Selector fresh;
    Check(fresh.LoadMemory(text) == 2 && fresh.BoomKept() == 1, "both seats and the one boom read back");
    fresh.Update("halo3", true, "Warthog", kSeatDriver);
    Check(fresh.Active().camera == 1 && fresh.BoomPitch(degrees) && degrees == 25.0f,
          "the next session starts the seat's camera at the player's angle");
    fresh.SetBoomPitch(26.0f, true);
    Check(!fresh.BoomPitch(degrees) && fresh.BoomKept() == 0, "back at the camera's own start, nothing is kept");
    Check(!Has(fresh.MemoryText(), "boom\thalo3"), "and the line goes");

    Selector bad;
    Check(bad.LoadMemory("boom\thalo3\tWarthog\tDriver\tWarthog\tThird Person\t95\r\n"
                         "boom\thalo3\tWarthog\tDriver\tWarthog\tThird Person\thigh\r\n"
                         "boom\thalo3\tWarthog\tDriver\r\n") == 0 &&
              bad.BoomKept() == 0,
          "a boom line past straight up, not a number, or cut short is skipped");

    // A camera file's own angle is the camera's.
    Selector own;
    own.SetTable(Read("{ \"games\": { \"default\": { \"vehicles\": { \"default\": { \"cameras\": ["
                      "  { \"name\": \"Low\", \"origin\": \"thirdperson\", \"boomPitch\": 12 } ] } } } } }"));
    own.Update("halo3", true, "Mongoose", kSeatDriver);
    Check(own.Active().boomPitchDeg == 12.0f && Has(own.Describe(), "boom from 12 deg"), "the file's angle, said in the log");
}

// "aimMarker" in a file: in the camera published, and an edit that only switches it is applied in place, not as a
// change of camera.
static void TestAimMarker()
{
    Selector selector;
    selector.SetTable(Read(Halo3Table(kWarthogCameras)));
    selector.Update("halo3", true, "warthog", kSeatDriver);
    Check(selector.Active().camera == 1 && selector.Active().mode == 1 && selector.Active().aimMarker,
          "a file that says nothing has the ring in a mode that turns the view with the vehicle");
    Check(selector.StepMode(1) && selector.Active().mode == 0 && !selector.Active().aimMarker,
          "and not in one that holds the world still");
    Check(selector.StepMode(1) && selector.Active().mode == 1, "back to the turning mode");
    const uint32_t changes = selector.Active().changes;
    const std::string off = Halo3Table(
        "[ { \"name\": \"Seat\", \"type\": \"firstperson\" },"
        "  { \"name\": \"Behind\", \"offset\": [-450, 0, 180], \"leashMax\": [null, null, 360],"
        "    \"tethering\": [ { \"name\": \"Untethered\" },"
        "                     { \"name\": \"Tethered\", \"rotationTracking\": [\"yaw\"], \"aimMarker\": false } ] } ]");
    selector.SetTable(Read(off));
    Check(!selector.Active().aimMarker && selector.Active().camera == 1 && selector.Active().mode == 1,
          "a mode's own aimMarker turns it off, and the player stays where he is");
    Check(selector.Active().changes == changes, "which is not a change of camera");
    Check(!Has(selector.Describe(), "aim ring"), "and the line no longer names the ring");
}

int main()
{
    TestBuiltInTable();
    TestBuiltInPresets();
    TestAimMarker();
    TestSeatMemory();
    TestEntriesAndSeats();
    TestLongMarkerName();
    TestReload();
    TestTwoCamerasOfOneName();
    TestCarryAcrossCameras();
    TestNoSeatView();
    TestFirstPersonOffset();
    TestShowBody();
    TestHideRegions();
    TestEyeAdjust();
    TestMemoryPerVehicleAndSeat();
    TestControlChoice();
    TestMemoryFile();
    TestBoomMemory();
    std::printf("PASS: %u vehicle camera selection checks\n", g_checks);
    return 0;
}
