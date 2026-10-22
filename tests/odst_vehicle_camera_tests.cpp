// ODST's seats as the vehicle camera file names them (src/common/odst_vehicle_camera_logic.h): each identity's name,
// a seat's role, a gun riding a carrier, and the built-in entry each one finds in ODST's own section.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/common/odst_vehicle_camera_logic.h"

using namespace odst_vehicle_camera;
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

static const vehicle_camera_file::Table& BuiltIn()
{
    static const vehicle_camera_file::Table table = vehicle_camera_file::DefaultTable();
    return table;
}

static std::string EntryFor(const char* vehicle, uint8_t seat)
{
    const vehicle_camera_file::Game* odst = BuiltIn().FindGame("odst");
    const int index = odst ? vehicle_camera_file::MatchVehicle(*odst, vehicle, seat) : -1;
    return index >= 0 ? odst->vehicles[static_cast<std::size_t>(index)].name : std::string();
}

static void TestNames()
{
    Check(std::strcmp(SeatVehicleName(OdstVehicleId::Warthog, false, OdstVehicleId::Unknown), "warthog") == 0 &&
              std::strcmp(SeatVehicleName(OdstVehicleId::Mauler, false, OdstVehicleId::Unknown), "prowler") == 0 &&
              std::strcmp(SeatVehicleName(OdstVehicleId::Shade, false, OdstVehicleId::Unknown), "shade") == 0,
          "a known vehicle goes by Halo 3's word for it, the Shade by its own");
    Check(std::strcmp(SeatVehicleName(OdstVehicleId::StationaryTurret, true, OdstVehicleId::Warthog), "warthog") == 0 &&
              std::strcmp(SeatVehicleName(OdstVehicleId::StationaryTurret, true, OdstVehicleId::Scorpion),
                          "scorpion") == 0,
          "a gun riding a vehicle goes by the vehicle's name");
    Check(std::strcmp(SeatVehicleName(OdstVehicleId::StationaryTurret, false, OdstVehicleId::Unknown), "turret") == 0 &&
              std::strcmp(SeatVehicleName(OdstVehicleId::StationaryTurret, true, OdstVehicleId::Unknown), "turret") == 0,
          "and by its own standing alone, or on a carrier the mod does not know");
    Check(std::strcmp(SeatVehicleName(OdstVehicleId::Warthog, true, OdstVehicleId::Scorpion), "warthog") == 0,
          "a vehicle that is not a gun keeps its own name whatever it rides");
    Check(std::strcmp(SeatVehicleName(OdstVehicleId::Unknown, false, OdstVehicleId::Unknown), "unknown") == 0,
          "a vehicle the mod does not know is unknown");
}

static void TestRoles()
{
    Check(SeatBits(OdstVehicleId::Warthog, 0, false) == kSeatDriver &&
              SeatBits(OdstVehicleId::Warthog, 1, false) == kSeatPassenger,
          "a Warthog's seat 0 drives, its seat 1 rides");
    Check(SeatBits(OdstVehicleId::StationaryTurret, 0, true) == kSeatGunner,
          "a gun riding a vehicle is its gunner's");
    Check(SeatBits(OdstVehicleId::StationaryTurret, 0, false) == (kSeatDriver | kSeatGunner) &&
              SeatBits(OdstVehicleId::Shade, 0, false) == (kSeatDriver | kSeatGunner),
          "a walk-up gun and the Shade are driven and fired from their one seat");
    Check(SeatBits(OdstVehicleId::Scorpion, 0, false) == kSeatDriver &&
              SeatBits(OdstVehicleId::Scorpion, 1, false) == kSeatPassenger &&
              SeatBits(OdstVehicleId::Scorpion, 4, false) == kSeatPassenger,
          "the Scorpion's driver drives, its four riders ride");
    Check(SeatBits(OdstVehicleId::Unknown, 0, false) == kSeatPassenger, "an unknown vehicle's seat rides");
    Check(WheelSeat(OdstVehicleId::Warthog, 0, false) && !WheelSeat(OdstVehicleId::Warthog, 1, false) &&
              WheelSeat(OdstVehicleId::Mongoose, 0, false) && !WheelSeat(OdstVehicleId::Banshee, 0, false) &&
              !WheelSeat(OdstVehicleId::Scorpion, 0, false) && !WheelSeat(OdstVehicleId::Shade, 0, false) &&
              !WheelSeat(OdstVehicleId::Unknown, 0, false),
          "only a look-steered ground vehicle's driver may take the wheel");
    Check(kSeatKeyTitle != 0, "ODST's seat keys are marked apart from Halo 3's");
}

// Every seat an ODST player can sit in finds the built-in entry meant for it.
static void TestBuiltInEntries()
{
    const auto entry = [](OdstVehicleId id, int seat, bool onCarrier, OdstVehicleId carrier) {
        return EntryFor(SeatVehicleName(id, onCarrier, carrier), SeatBits(id, seat, onCarrier));
    };
    const OdstVehicleId none = OdstVehicleId::Unknown;
    Check(entry(OdstVehicleId::Warthog, 0, false, none) == "Warthog" &&
              entry(OdstVehicleId::Warthog, 1, false, none) == "Warthog passenger" &&
              entry(OdstVehicleId::StationaryTurret, 0, true, OdstVehicleId::Warthog) == "Warthog gunner",
          "the Warthog's three seats");
    Check(entry(OdstVehicleId::Mongoose, 0, false, none) == "Mongoose" &&
              entry(OdstVehicleId::Mongoose, 1, false, none) == "Mongoose passenger",
          "the Mongoose's two");
    Check(entry(OdstVehicleId::Scorpion, 0, false, none) == "Scorpion" &&
              entry(OdstVehicleId::StationaryTurret, 0, true, OdstVehicleId::Scorpion) == "Scorpion gunner" &&
              entry(OdstVehicleId::Scorpion, 3, false, none) == "Scorpion rider",
          "the Scorpion's driver, gunner and riders");
    Check(entry(OdstVehicleId::Wraith, 0, false, none) == "Wraith" &&
              entry(OdstVehicleId::StationaryTurret, 0, true, OdstVehicleId::Wraith) == "Wraith gunner",
          "the Wraith's driver and gunner");
    Check(entry(OdstVehicleId::Ghost, 0, false, none) == "Ghost" && entry(OdstVehicleId::Banshee, 0, false, none) == "Banshee" &&
              entry(OdstVehicleId::Chopper, 0, false, none) == "Chopper" &&
              entry(OdstVehicleId::Mauler, 0, false, none) == "Prowler" &&
              entry(OdstVehicleId::Hornet, 0, false, none) == "Hornet",
          "the single seats");
    Check(entry(OdstVehicleId::Shade, 0, false, none) == "Shade" &&
              entry(OdstVehicleId::StationaryTurret, 0, false, none) == "Turret" &&
              entry(OdstVehicleId::StationaryTurret, 0, true, none) == "Turret",
          "the Shade and a walk-up gun, also one on a carrier the mod does not know");
    const vehicle_camera_file::Game* odst = BuiltIn().FindGame("odst");
    Check(odst && odst->key == "odst" && odst->vehicles.back().isDefault, "ODST's section ends on its default entry");
    Check(entry(OdstVehicleId::Unknown, 0, false, none) == "default", "and a vehicle it does not know rides it");

    // ODST's vehicles are Halo 3's tags, so their cameras are Halo 3's.
    const vehicle_camera_file::Game* halo3 = BuiltIn().FindGame("halo3");
    bool same = halo3 != nullptr;
    for (std::size_t i = 0; same && i < odst->vehicles.size(); ++i)
    {
        const vehicle_camera_file::Vehicle& own = odst->vehicles[i];
        for (const vehicle_camera_file::Vehicle& theirs : halo3->vehicles)
        {
            if (theirs.name != own.name)
                continue;
            same = same && theirs.cameras.size() == own.cameras.size();
            for (std::size_t c = 0; same && c < own.cameras.size(); ++c)
                for (int axis = 0; axis < 3; ++axis)
                    same = same && theirs.cameras[c].offset[axis] == own.cameras[c].offset[axis] &&
                           theirs.cameras[c].origin == own.cameras[c].origin;
        }
    }
    Check(same, "every entry ODST shares with Halo 3 has Halo 3's cameras");
}

int main()
{
    TestNames();
    TestRoles();
    TestBuiltInEntries();
    std::printf("odst vehicle camera tests: %u checks passed\n", g_checks);
    return 0;
}
