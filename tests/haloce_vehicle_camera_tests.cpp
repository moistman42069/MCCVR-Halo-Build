// Halo CE's seats as the vehicle camera file names them, and the game's own third-person camera worked out from its
// following camera's command (src/common/haloce_vehicle_camera_logic.h).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/common/haloce_vehicle_camera_logic.h"

using namespace haloce_vehicle_camera;
using vehicle_camera_file::kSeatDriver;
using vehicle_camera_file::kSeatGunner;
using vehicle_camera_file::kSeatPassenger;

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

static std::string Name(const char* path)
{
    char out[kNameCapacity];
    return NameFromTagPath(path, out) ? std::string(out) : std::string("<none>");
}

static void TestNames()
{
    Check(Name("vehicles\\warthog\\mp_warthog") == "warthog" && Name("vehicles\\warthog\\warthog") == "warthog",
          "the campaign and multiplayer Warthogs are one vehicle");
    Check(Name("vehicles\\rwarthog\\rwarthog") == "rocket_warthog", "the rocket Warthog has a name of its own");
    Check(Name("vehicles\\ghost\\ghost_mp") == "ghost" && Name("vehicles\\banshee\\banshee_mp") == "banshee" &&
              Name("vehicles\\scorpion\\scorpion_mp") == "scorpion" && Name("vehicles\\wraith\\wraith") == "wraith" &&
              Name("vehicles\\pelican\\pelican") == "pelican",
          "each stock folder names its vehicle");
    Check(Name("vehicles\\c gun turret\\c gun turret_mp") == "shade", "the Covenant gun turret is the Shade");
    Check(Name("Vehicles/Ghost/Ghost_MP") == "ghost", "either slash and any case");
    Check(Name("vehicles\\mymod\\Fancy Hog") == "fancy_hog", "a map's own vehicle by its short tag name");
    Check(Name("levels\\test\\custom ride") == "custom_ride", "outside the vehicles folder too");
    Check(Name("") == "<none>" && Name("\\\\") == "<none>" && Name("vehicles\\***\\???") == "<none>",
          "a path with nothing to name names nothing");
    Check(Name("vehicles\\warthog") == "warthog", "a two-part path goes by its last part");

    char a[kNameCapacity], b[kNameCapacity];
    CustomName(0x0123456789ABCDEFull, a);
    CustomName(0x0123456789ABCDEEull, b);
    Check(std::strcmp(a, "custom_0123456789abcdef") == 0 && std::strcmp(a, b) != 0,
          "an unnamed vehicle goes by its model fingerprint");
}

static void TestRoles()
{
    Check(SeatBits(true, kSeatDriverBit | kSeatGunnerBit, false, false) == (kSeatDriver | kSeatGunner),
          "a Ghost's seat drives and fires");
    Check(SeatBits(true, kSeatDriverBit | kSeatThirdPersonBit, false, false) == kSeatDriver, "a Warthog driver");
    Check(SeatBits(true, kSeatGunnerBit, false, false) == kSeatGunner, "a Warthog gunner");
    Check(SeatBits(true, kSeatThirdPersonBit, true, true) == kSeatPassenger,
          "a seat whose flags name neither is a passenger's, whatever the occupant slots say");
    Check(SeatBits(false, 0, true, false) == kSeatDriver && SeatBits(false, 0, false, true) == kSeatGunner &&
              SeatBits(false, 0, false, false) == kSeatPassenger,
          "with no flags, the occupant slots");
}

static bool Near(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

static void TestGameCamera()
{
    // The following camera's command for a track point (forward, left, up) = (-2, 0.5, 1) at look pitch p along
    // heading h: offset = (d cos p + -2, -0.5, d sin p + 1), distance d, forward = (cos p h, sin p).
    const float pitch = 0.2f, depth = 2.29f;
    const float h[2] = {0.6f, 0.8f};
    const float forward[3] = {std::cos(pitch) * h[0], std::cos(pitch) * h[1], std::sin(pitch)};
    const float offset[3] = {depth * std::cos(pitch) - 2.0f, -0.5f, depth * std::sin(pitch) + 1.0f};
    float at[3];
    Check(GameCameraFromPivot(forward, offset, depth, at), "a command with a heading gives a place");
    // The track point laid along the heading: forward h, left (-h.y, h.x), up.
    const float want[3] = {-2.0f * h[0] + 0.5f * -h[1], -2.0f * h[1] + 0.5f * h[0], 1.0f};
    Check(Near(at[0], want[0]) && Near(at[1], want[1]) && Near(at[2], want[2]),
          "the camera stands at the track's point from the pivot, the look's distance cancelled");
    const float up[3] = {0.0f, 0.0f, 1.0f};
    Check(!GameCameraFromPivot(up, offset, depth, at), "a look straight up has no heading");
    const float nan[3] = {std::nanf(""), 0.0f, 0.0f};
    Check(!GameCameraFromPivot(forward, nan, depth, at), "a value that is not finite gives nothing");

    Check(LevelElevationDeg(1.51f, 4.74f, 0.0f) == 19.0f, "looking level, the elevation it stands at");
    Check(LevelElevationDeg(2.30f, 6.96f, -7.6f) == 12.0f, "looking down, plus the look");
    Check(LevelElevationDeg(5.0f, 4.0f, 0.0f) == vehicle_boom::kBoomPitchUnset &&
              LevelElevationDeg(1.0f, 0.0f, 0.0f) == vehicle_boom::kBoomPitchUnset,
          "a rise past the distance, or no distance, makes no angle");
    const vehicle_boom::Range range = BoomRange();
    Check(Near(range.min, -60.0f * vehicle_boom::kRadiansPerDegree) &&
              Near(range.max, 10.0f * vehicle_boom::kRadiansPerDegree),
          "the orbit's range: 10 deg below to 60 deg above level");
}

static std::string EntryFor(const char* vehicle, uint8_t seat)
{
    static const vehicle_camera_file::Table builtIn = vehicle_camera_file::DefaultTable();
    const vehicle_camera_file::Game* ce = builtIn.FindGame("halo1");
    const int index = ce ? vehicle_camera_file::MatchVehicle(*ce, vehicle, seat) : -1;
    return index >= 0 ? ce->vehicles[static_cast<std::size_t>(index)].name : std::string();
}

static void TestBuiltInEntries()
{
    static const vehicle_camera_file::Table builtIn = vehicle_camera_file::DefaultTable();
    const vehicle_camera_file::Game* ce = builtIn.FindGame("halo1");
    Check(ce && ce->key == "halo1", "the built-in table has a halo1 section");
    Check(EntryFor("warthog", kSeatDriver) == "Warthog" && EntryFor("warthog", kSeatPassenger) == "Warthog passenger" &&
              EntryFor("warthog", kSeatGunner) == "Warthog gunner",
          "each Warthog seat its own entry");
    Check(EntryFor("rocket_warthog", kSeatGunner) == "Warthog gunner", "the rocket Warthog rides the Warthog's");
    Check(EntryFor("ghost", kSeatDriver | kSeatGunner) == "Ghost" &&
              EntryFor("banshee", kSeatDriver | kSeatGunner) == "Banshee",
          "the Ghost's and the Banshee's one seat");
    Check(EntryFor("scorpion", kSeatDriver | kSeatGunner) == "Scorpion" &&
              EntryFor("scorpion", kSeatPassenger) == "Scorpion passenger",
          "the Scorpion's driver and its riders");
    Check(EntryFor("shade", kSeatDriver | kSeatGunner) == "Shade", "the Shade");
    Check(EntryFor("wraith", kSeatDriver | kSeatGunner) == "Wraith", "the Wraith");
    Check(EntryFor("fancy_hog", kSeatDriver) == "default", "a vehicle the section does not name rides its default");
    // Every entry starts in the seat view, and the second camera is where the game's own rests.
    bool seatFirst = true;
    for (const vehicle_camera_file::Vehicle& vehicle : ce->vehicles)
        seatFirst = seatFirst && vehicle.cameras.size() >= 2 &&
                    vehicle.cameras[0].type == vehicle_camera_file::CameraType::FirstPerson &&
                    vehicle.cameras[1].origin == vehicle_camera_file::Origin::ThirdPerson;
    Check(seatFirst, "every halo1 entry starts in the seat view, then the game's own third person");
}

int main()
{
    TestNames();
    TestRoles();
    TestGameCamera();
    TestBuiltInEntries();
    std::printf("haloce vehicle camera tests: %u checks passed\n", g_checks);
    return 0;
}
