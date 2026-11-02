// Halo 2's seats as the vehicle camera file names them (src/common/halo2_vehicle_camera_logic.h): each model's name,
// a gun's, a vehicle the mod does not know, a seat's role, the key's identity, a model's size, the orbit's pivot and
// range, and the built-in entries each finds.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/common/halo2_vehicle_camera_logic.h"
#include "../src/common/halo2_vehicle_identity.h"

using namespace halo2_vehicle_camera;
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
    const vehicle_camera_file::Game* halo2 = builtIn.FindGame("halo2");
    const int index = halo2 && halo2->key == "halo2" ? vehicle_camera_file::MatchVehicle(*halo2, vehicle, seat) : -1;
    return index >= 0 ? halo2->vehicles[static_cast<std::size_t>(index)].name : std::string();
}

static void TestFileNames()
{
    char name[kNameCapacity];
    Check(FileName("Warthog", name) && std::strcmp(name, "warthog") == 0, "a model's name in lower case");
    Check(FileName("Warthog / Chaingun", name) && std::strcmp(name, "warthog_chaingun") == 0,
          "a slash and its spaces are one underscore");
    Check(FileName("C Turret Ap", name) && std::strcmp(name, "c_turret_ap") == 0, "every space an underscore");
    Check(FileName("  Pelican /  Pelican Rear Gun ", name) && std::strcmp(name, "pelican_pelican_rear_gun") == 0,
          "no underscore at either end, one for a run");
    Check(!FileName(nullptr, name) && name[0] == '\0' && !FileName(" / ", name) && name[0] == '\0',
          "no name, or only separators, is no name");
    std::string longName(kNameCapacity, 'a');
    Check(!FileName(longName.c_str(), name) && name[0] == '\0', "a name that does not fit is no name");
    // Every vehicle model the mod knows has a file name that fits.
    bool all = true;
    for (const auto& model : halo2_vehicle_identity::kModels)
        all = all && FileName(model.name, name);
    Check(all, "every known model's name fits");
}

static void TestSeatNames()
{
    char buffer[kNameCapacity];
    const float bounds[6] = {-1.0f, 1.0f, -0.5f, 0.5f, 0.0f, 0.8f};
    Check(std::strcmp(SeatVehicleName("Warthog", nullptr, false, bounds, 15, buffer), "warthog") == 0,
          "a known vehicle goes by its model's name");
    Check(std::strcmp(SeatVehicleName("Warthog / Gauss", "Warthog", true, bounds, 6, buffer), "warthog_gauss") == 0,
          "a known gun by its own model's name, which carries its vehicle's");
    Check(std::strcmp(SeatVehicleName(nullptr, "Wraith", true, bounds, 3, buffer), "wraith_gun") == 0,
          "an unknown gun riding a known vehicle by that vehicle's name and _gun");
    const char* custom = SeatVehicleName(nullptr, nullptr, false, bounds, 3, buffer);
    Check(std::strncmp(custom, "custom_", 7) == 0 && std::strlen(custom) == 15, "anything else is custom_ and a hash");
    char again[kNameCapacity];
    Check(std::strcmp(SeatVehicleName(nullptr, nullptr, false, bounds, 3, again), custom) == 0,
          "the same model hashes the same");
    const float other[6] = {-1.0f, 1.0f, -0.5f, 0.5f, 0.0f, 0.9f};
    Check(std::strcmp(SeatVehicleName(nullptr, nullptr, false, other, 3, again), custom) != 0 &&
              std::strcmp(SeatVehicleName(nullptr, nullptr, false, bounds, 4, again), custom) != 0,
          "another size or node count hashes otherwise");
    Check(std::strncmp(SeatVehicleName(nullptr, "Wraith", false, bounds, 3, again), "custom_", 7) == 0,
          "an unknown vehicle not on a carrier is not named by one");
}

static void TestRoles()
{
    using vehicle_camera_file::SeatBits;
    Check(halo2_vehicle_camera::SeatBits(true, kSeatDriverBit, false, 3) == SeatBits(true, false),
          "the driver's flag makes a driver's seat");
    Check(halo2_vehicle_camera::SeatBits(true, kSeatGunnerBit, true, 0) == SeatBits(false, true),
          "the gunner's flag a gunner's");
    Check(halo2_vehicle_camera::SeatBits(true, kSeatDriverBit | kSeatGunnerBit, false, 0) == SeatBits(true, true),
          "both, a turret's one seat");
    Check(halo2_vehicle_camera::SeatBits(true, 0x60u, false, 0) == kSeatPassenger, "neither, a passenger's");
    Check(halo2_vehicle_camera::SeatBits(false, 0, true, 0) == kSeatGunner &&
              halo2_vehicle_camera::SeatBits(false, 0, false, 0) == kSeatDriver &&
              halo2_vehicle_camera::SeatBits(false, 0, false, 2) == kSeatPassenger,
          "unread flags: a gun on a carrier is its gunner's, seat 0 a driver's, any other a passenger's");
}

static void TestKeyAndSize()
{
    Check(KeyIdentity(0) == 0 && KeyIdentity(0x0F2F9DDD9BC289A3ull) == (0x0F2F9DDDu ^ 0x9BC289A3u),
          "the key folds the identity's halves");
    const float warthog[6] = {-0.996299f, 0.984343f, -0.51156f, 0.503887f, -0.018016f, 0.760923f};
    const float radius = BoundsRadius(warthog);
    Check(radius > 1.1f && radius < 1.25f, "the Warthog's size is half its box's diagonal");
    const float flat[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};
    const float bad[6] = {0.0f, NAN, 0.0f, 1.0f, 0.0f, 1.0f};
    Check(BoundsRadius(flat) == 0.0f && BoundsRadius(bad) == 0.0f && BoundsRadius(nullptr) == 0.0f,
          "bounds that are not a box have no size");
}

static void TestRing()
{
    Check(RingFollowsAim(true, false, 0u), "a gun riding a carrier shows its own aim, whatever its flags say");
    Check(RingFollowsAim(false, true, 0x00001098u), "a seat flagged gunner and not driver shows its gun's aim");
    Check(!RingFollowsAim(false, true, 0x0000801Cu), "a driver that also guns (the Ghost's seat) keeps the hull");
    Check(!RingFollowsAim(false, true, 0x00048014u) && !RingFollowsAim(false, true, 0x00001070u),
          "a driver's or a passenger's seat keeps the hull");
    Check(!RingFollowsAim(false, false, kSeatGunnerBit), "flags that were not read say nothing");
}

static void TestOrbit()
{
    const float warthog[6] = {-0.996299f, 0.984343f, -0.51156f, 0.503887f, -0.018016f, 0.760923f};
    Check(std::fabs(OrbitPivotUpWu(true, warthog) - 0.3714535f) < 1e-5f,
          "the orbit turns about the middle of the vehicle's height");
    const float sunk[6] = {-1.0f, 1.0f, -1.0f, 1.0f, -2.0f, 0.5f};
    const float bad[6] = {0.0f, 1.0f, 0.0f, 1.0f, 1.0f, NAN};
    Check(OrbitPivotUpWu(true, sunk) == 0.0f && OrbitPivotUpWu(false, warthog) == 0.0f &&
              OrbitPivotUpWu(true, bad) == 0.0f && OrbitPivotUpWu(true, nullptr) == 0.0f,
          "never below the origin; unread or broken bounds turn about the origin");
    const vehicle_boom::Range range = BoomRange();
    Check(std::fabs(range.min + 60.0f * vehicle_boom::kRadiansPerDegree) < 1e-6f && range.max == 0.0f &&
              !std::signbit(range.max),
          "the orbit runs from level with its pivot to 60 deg above (as look pitches, the other way round)");
}

static void TestEntries()
{
    char buffer[kNameCapacity];
    const auto seatName = [&buffer](const char* model, const char* carrier) {
        return std::string(SeatVehicleName(model, carrier, carrier != nullptr, nullptr, 0, buffer));
    };
    Check(EntryFor(seatName("Warthog", nullptr).c_str(), kSeatDriver) == "Warthog" &&
              EntryFor(seatName("Warthog", nullptr).c_str(), kSeatPassenger) == "Warthog passenger" &&
              EntryFor(seatName("Warthog / Chaingun", "Warthog").c_str(), kSeatGunner) == "Warthog gunner",
          "the Warthog's three seats find their entries by the names given them");
    Check(EntryFor(seatName("Wraith / Minigun", "Wraith").c_str(), kSeatGunner) == "Wraith gunner" &&
              EntryFor(seatName("Spectre / Plasma", "Spectre").c_str(), kSeatGunner) == "Spectre gunner",
          "the Wraith's and the Spectre's guns their gunners'");
    Check(EntryFor(seatName("C Turret Ap", nullptr).c_str(), vehicle_camera_file::SeatBits(true, true)) == "Turret" &&
              EntryFor(seatName("Cov Guntower", nullptr).c_str(), vehicle_camera_file::SeatBits(true, true)) == "Turret",
          "the walk-up guns the turret's");
    Check(EntryFor(seatName("Phantom", nullptr).c_str(), kSeatPassenger) == "default",
          "a dropship's passenger rides Halo 2's default");
}

int main()
{
    TestFileNames();
    TestSeatNames();
    TestRoles();
    TestKeyAndSize();
    TestOrbit();
    TestRing();
    TestEntries();
    std::printf("halo2_vehicle_camera_tests: %u checks passed\n", g_checks);
    return 0;
}
