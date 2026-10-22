// Halo 4's seats as the vehicle camera file names them, and the point the game's own third-person camera turns about
// (src/common/halo4_vehicle_camera_logic.h): names by render model identity, a mounted gun by its carrier, a vehicle
// the mod has not measured, a seat's role from its flags, the built-in entry each finds, and the pivot finder.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/common/halo4_vehicle_camera_logic.h"

using namespace halo4_vehicle_camera;
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

static std::string EntryFor(const char* vehicle, uint8_t seat)
{
    static const vehicle_camera_file::Table builtIn = vehicle_camera_file::DefaultTable();
    const vehicle_camera_file::Game* halo4 = builtIn.FindGame("halo4");
    const int index = halo4 ? vehicle_camera_file::MatchVehicle(*halo4, vehicle, seat) : -1;
    return index >= 0 ? halo4->vehicles[static_cast<std::size_t>(index)].name : std::string();
}

static void TestNames()
{
    Check(IdentityName(0) == nullptr, "an identity that could not be read has no name");
    Check(IdentityName(0x0000001d1a19101eull) != nullptr &&
              std::strcmp(IdentityName(0x0000001d1a19101eull), "broadsword") == 0,
          "the Broadsword's model is named");
    Check(IdentityName(0x0000003511090710ull) != nullptr &&
              std::strcmp(IdentityName(0x0000003511090710ull), "warthog") == 0,
          "and the Warthog's, measured in Forge");
    bool distinct = true, named = true;
    const std::size_t count = sizeof(kKnownVehicles) / sizeof(kKnownVehicles[0]);
    for (std::size_t a = 0; a < count; ++a)
    {
        named = named && kKnownVehicles[a].identity != 0 && kKnownVehicles[a].name && kKnownVehicles[a].name[0];
        for (std::size_t b = a + 1; b < count; ++b)
            distinct = distinct && kKnownVehicles[a].identity != kKnownVehicles[b].identity;
    }
    Check(named && distinct, "every measured model has a name, and no model is listed twice");

    char buffer[kNameCapacity];
    const char* custom = SeatVehicleName(0x0000002a12345678ull, false, 0, buffer);
    Check(std::strcmp(custom, "custom_0000002a12345678") == 0, "a model the mod has not measured is custom_ and its hex");
    char unread[kNameCapacity];
    Check(std::strcmp(SeatVehicleName(0, false, 0, unread), "custom_unread") == 0,
          "and one that could not be read says so");
    Check(std::strcmp(SeatVehicleName(0x0000002a12345678ull, true, 0x0000001d1a19101eull, buffer), "broadsword") == 0,
          "a gun riding a measured carrier goes by the carrier");
    Check(std::strcmp(SeatVehicleName(0x0000001d1a19101eull, true, 0x0000002a12345678ull, buffer), "broadsword") == 0,
          "a measured gun on an unmeasured carrier keeps its own name");
    Check(std::strcmp(SeatVehicleName(0x0000002a12345678ull, true, 0x0000003b00000001ull, buffer),
                      "custom_0000002a12345678") == 0,
          "and an unmeasured gun on an unmeasured carrier goes by its own model");
}

static void TestRoles()
{
    Check(SeatBits(true, kSeatDriverBit, false, 0) == kSeatDriver, "flag bit 2 drives");
    Check(SeatBits(true, kSeatDriverBit | kSeatGunnerBit, false, 0) == (kSeatDriver | kSeatGunner),
          "bits 2 and 3 drive and fire (a Ghost)");
    Check(SeatBits(true, kSeatGunnerBit, false, 2) == kSeatGunner, "bit 3 fires");
    Check(SeatBits(true, 0x30u, false, 1) == kSeatPassenger, "neither is a passenger (the Warthog's seat 1, measured)");
    Check(SeatBits(true, 0x30u, false, 0) == kSeatPassenger, "read flags win over the seat's number");
    Check(SeatBits(true, kSeatDriverBit, true, 0) == kSeatGunner, "a gun's seat on a carrier fires and never drives it");
    Check(SeatBits(false, 0u, true, 0) == kSeatGunner, "even when its flags could not be read");
    Check(SeatBits(false, 0u, false, 0) == kSeatDriver, "flags not read: seat 0 drives");
    Check(SeatBits(false, kSeatDriverBit, false, 1) == kSeatPassenger, "and any other seat rides");
}

static void TestEntries()
{
    Check(EntryFor("warthog", kSeatDriver) == "Warthog" && EntryFor("warthog", kSeatPassenger) == "Warthog passenger" &&
              EntryFor("warthog", kSeatGunner) == "Warthog gunner",
          "the Warthog's three seats find their entries");
    Check(EntryFor("broadsword", kSeatDriver) == "Broadsword", "the Broadsword's pilot his");
    char buffer[kNameCapacity];
    const char* custom = SeatVehicleName(0x0000002a12345678ull, false, 0, buffer);
    Check(EntryFor(custom, kSeatDriver) == "default", "an unmeasured vehicle rides Halo 4's default entry");
}

static void TestLevelAndRange()
{
    // Looking level, a camera 1 unit above its pivot 3 units away stands asin(1/3) = 19.47 deg up: 19 deg in whole
    // degrees. Looking 10 deg up from 2 deg above, it would stand at 12 deg looking level.
    Check(OrbitLevelElevationDeg(1.0f, 3.0f, 0.0f) == 19.0f, "level elevation from the rise looking level");
    Check(OrbitLevelElevationDeg(std::sin(2.0f * 0.0174532925f) * 4.0f, 4.0f, 10.0f) == 12.0f,
          "and the look the camera had then is added back");
    Check(OrbitLevelElevationDeg(5.0f, 3.0f, 0.0f) == vehicle_boom::kBoomPitchUnset &&
              OrbitLevelElevationDeg(0.0f, 0.0f, 0.0f) == vehicle_boom::kBoomPitchUnset,
          "a rise past the distance, or no distance, gives no angle");
    const vehicle_boom::Range range = BoomRange();
    Check(range.min < range.max && std::fabs(range.min + 60.0f * vehicle_boom::kRadiansPerDegree) < 1.0e-5f &&
              std::fabs(range.max - 10.0f * vehicle_boom::kRadiansPerDegree) < 1.0e-5f,
          "the boom orbits from 10 deg below level to 60 above");
}

// A following camera: pivot - distance * aim, looking along the aim, in the vehicle's own frame.
static void Following(const float pivot[3], float distance, float yawDeg, float pitchDeg, float o[3], float d[3])
{
    const float yaw = yawDeg * 0.0174532925f, pitch = pitchDeg * 0.0174532925f;
    d[0] = std::cos(pitch) * std::cos(yaw);
    d[1] = std::cos(pitch) * std::sin(yaw);
    d[2] = std::sin(pitch);
    for (int axis = 0; axis < 3; ++axis)
        o[axis] = pivot[axis] - distance * d[axis];
}

static void TestPivotFinder()
{
    const float pivot[3] = {0.2f, -0.1f, 0.6f};
    PivotFinder finder;
    float o[3], d[3], found[3];
    // One look, many times: the lines are one line, and nothing is found.
    for (int i = 0; i < 50; ++i)
    {
        Following(pivot, 2.3f, 0.0f, -5.0f, o, d);
        Check(finder.Add(o, d), "a sample is taken");
    }
    Check(!finder.Solve(found) && finder.Spread() < 1.0e-6, "lines all one way do not cross anywhere");
    // A few degrees of turn are not enough; the aim swinging 30 degrees is.
    for (int i = 0; i < 20; ++i)
    {
        Following(pivot, 2.3f, 2.0f, -5.0f, o, d);
        finder.Add(o, d);
    }
    Check(!finder.Solve(found), "a two-degree turn is too little to cross the lines");
    for (int i = 0; i < 40; ++i)
    {
        Following(pivot, 2.3f, -30.0f + 1.5f * static_cast<float>(i), -5.0f + 0.2f * static_cast<float>(i % 7), o, d);
        finder.Add(o, d);
    }
    float residual = -1.0f;
    Check(finder.Solve(found, &residual), "a 60-degree swing crosses them");
    Check(std::fabs(found[0] - pivot[0]) < 1.0e-3f && std::fabs(found[1] - pivot[1]) < 1.0e-3f &&
              std::fabs(found[2] - pivot[2]) < 1.0e-3f && residual >= 0.0f && residual < 1.0e-3f,
          "at the pivot, with the lines through it");

    // The camera pulled in from a wall still looks through the pivot: the distance does not matter.
    PivotFinder pulled;
    for (int i = 0; i < 30; ++i)
    {
        Following(pivot, 0.8f + 0.1f * static_cast<float>(i), -20.0f + 2.0f * static_cast<float>(i), 0.0f, o, d);
        pulled.Add(o, d);
    }
    Check(pulled.Solve(found) && std::fabs(found[2] - pivot[2]) < 1.0e-3f, "a camera pulled in finds the same point");

    // A track that lifts the camera off the line by a few centimetres still finds the point near the pivot.
    PivotFinder tracked;
    for (int i = 0; i < 40; ++i)
    {
        Following(pivot, 2.3f, -40.0f + 2.0f * static_cast<float>(i), -10.0f + static_cast<float>(i % 5), o, d);
        o[2] += 0.05f;
        tracked.Add(o, d);
    }
    Check(tracked.Solve(found, &residual) && std::fabs(found[2] - pivot[2]) < 0.06f && residual < 0.06f,
          "a small track offset moves the point no further than the offset");

    // Lines that do not meet (cameras scattered about) are not taken.
    PivotFinder scattered;
    for (int i = 0; i < 40; ++i)
    {
        const float place[3] = {static_cast<float>((i * 7) % 11) - 5.0f, static_cast<float>((i * 3) % 13) - 6.0f,
                                static_cast<float>(i % 3)};
        Following(place, 2.0f, static_cast<float>(i * 37 % 360), 0.0f, o, d);
        scattered.Add(o, d);
    }
    Check(!scattered.Solve(found), "lines that pass far from any one point find nothing");

    // Not a direction, not finite: refused.
    const float bad[3] = {1.0f, 1.0f, 0.0f}, nan[3] = {std::nanf(""), 0.0f, 0.0f}, unit[3] = {1.0f, 0.0f, 0.0f};
    Check(!finder.Add(o, bad) && !finder.Add(nan, unit), "a sample that is not a line is refused");
    finder.Reset();
    Check(finder.samples == 0 && !finder.Solve(found), "and a reset forgets every line");

    float near[3];
    const float lineO[3] = {-3.0f, 0.0f, 1.0f}, lineD[3] = {1.0f, 0.0f, 0.0f}, origin[3] = {0.0f, 0.0f, 0.0f};
    NearestOnLine(lineO, lineD, origin, near);
    Check(near[0] == 0.0f && near[1] == 0.0f && near[2] == 1.0f, "the nearest point of a line to the origin");
}

int main()
{
    TestNames();
    TestRoles();
    TestEntries();
    TestLevelAndRange();
    TestPivotFinder();
    std::printf("halo4_vehicle_camera_tests: %u checks passed\n", g_checks);
    return 0;
}
