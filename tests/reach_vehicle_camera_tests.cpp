// Reach's seats as the vehicle camera file names them (src/common/reach_vehicle_camera_logic.h): each identity's
// name, a seat's role, a vehicle the mod does not know, and the built-in entries each one finds.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/common/reach_vehicle_camera_logic.h"

using namespace reach_vehicle_camera;
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
    const vehicle_camera_file::Game* reach = builtIn.FindGame("reach");
    const int index = reach ? vehicle_camera_file::MatchVehicle(*reach, vehicle, seat) : -1;
    return index >= 0 ? reach->vehicles[static_cast<std::size_t>(index)].name : std::string();
}

static void TestNames()
{
    Check(IdentityName(ReachVehicleId::Unknown) == nullptr, "an unknown vehicle has no identity name");
    Check(std::strcmp(IdentityName(ReachVehicleId::Banshee), "banshee") == 0 &&
              std::strcmp(IdentityName(ReachVehicleId::Warthog), "warthog") == 0 &&
              std::strcmp(IdentityName(ReachVehicleId::WarthogGauss), "warthog_gauss") == 0 &&
              std::strcmp(IdentityName(ReachVehicleId::SquadDropPod), "squad_drop_pod") == 0,
          "each identity goes by its stock tag's word, first to last");
    Check(IdentityName(static_cast<ReachVehicleId>(kReachVehicleIdentityCount + 1)) == nullptr,
          "a code past the last identity has no name");
    bool distinct = true;
    for (int a = 1; a <= kReachVehicleIdentityCount; ++a)
        for (int b = a + 1; b <= kReachVehicleIdentityCount; ++b)
            distinct = distinct && std::strcmp(IdentityName(static_cast<ReachVehicleId>(a)),
                                               IdentityName(static_cast<ReachVehicleId>(b))) != 0;
    Check(distinct, "and no two identities share a name");

    ReachVehicleFingerprint one{0x3F978072, 0xBD406A82, 0xBAD03632, 0x3EC25783};
    ReachVehicleFingerprint two = one;
    two.offsetZ ^= 1u;
    char a[kNameCapacity], b[kNameCapacity], c[kNameCapacity];
    CustomName(one, 1, a);
    CustomName(one, 1, b);
    CustomName(two, 1, c);
    Check(std::strncmp(a, "custom_", 7) == 0 && std::strlen(a) == 15, "an unknown vehicle is custom_ and 8 hex digits");
    Check(std::strcmp(a, b) == 0 && std::strcmp(a, c) != 0, "the same tuple the same name, another tuple another");
    CustomName(one, 4, c);
    Check(std::strcmp(a, c) != 0, "and the physics type is part of it");

    char buffer[kNameCapacity];
    Check(std::strcmp(SeatVehicleName(ReachVehicleId::PlasmaTurret, true, ReachVehicleId::Falcon, one, 6, buffer),
                      "falcon") == 0,
          "a walk-up gun riding the Falcon goes by the Falcon's name");
    Check(std::strcmp(SeatVehicleName(ReachVehicleId::PlasmaTurret, false, ReachVehicleId::Unknown, one, 6, buffer),
                      "plasma_turret") == 0 &&
              std::strcmp(SeatVehicleName(ReachVehicleId::PlasmaTurret, true, ReachVehicleId::Unknown, one, 6, buffer),
                          "plasma_turret") == 0,
          "and by its own standing alone, or on a carrier the mod does not know");
    Check(std::strcmp(SeatVehicleName(ReachVehicleId::WarthogChaingun, true, ReachVehicleId::Warthog, one, 6, buffer),
                      "warthog_chaingun") == 0,
          "a mounted gun goes by its own tag, which carries its vehicle's word");
    const char* custom = SeatVehicleName(ReachVehicleId::Unknown, false, ReachVehicleId::Unknown, one, 1, buffer);
    Check(custom == buffer && std::strcmp(custom, a) == 0, "an unknown vehicle by its fingerprint");
}

static void TestRoles()
{
    Check(SeatBits(ReachVehicleId::Warthog, 0, false, 0) == kSeatDriver &&
              SeatBits(ReachVehicleId::Warthog, 1, false, 0) == kSeatPassenger,
          "a Warthog's seat 0 drives, its seat 1 rides");
    Check(SeatBits(ReachVehicleId::WarthogGauss, 0, true, 0) == kSeatGunner &&
              SeatBits(ReachVehicleId::WraithGunner, 0, true, 0) == kSeatGunner,
          "a mounted gun's seat is its gunner's");
    Check(SeatBits(ReachVehicleId::ShadeFlak, 0, false, 0) == (kSeatDriver | kSeatGunner),
          "a walk-up gun standing alone is driven and fired from its one seat");
    Check(SeatBits(ReachVehicleId::PlasmaTurret, 0, true, 0) == kSeatGunner,
          "and riding a carrier only fired");
    Check(SeatBits(ReachVehicleId::Falcon, 0, false, 0) == kSeatDriver &&
              SeatBits(ReachVehicleId::Falcon, 3, false, 0) == kSeatPassenger,
          "the Falcon's pilot drives, its bench seats ride");
    Check(SeatBits(ReachVehicleId::Unknown, 0, false, kSeatDriverBit) == kSeatDriver &&
              SeatBits(ReachVehicleId::Unknown, 2, false, kSeatGunnerBit) == kSeatGunner &&
              SeatBits(ReachVehicleId::Unknown, 1, false, 1u << 5) == kSeatPassenger,
          "an unknown vehicle's seat by its own tag flags");
    Check(SeatBits(ReachVehicleId::Unknown, 0, true, kSeatDriverBit | kSeatGunnerBit) == kSeatGunner,
          "an unknown gun on a carrier does not drive it");
    Check(WheelSeat(ReachVehicleId::Warthog, 0) && !WheelSeat(ReachVehicleId::Warthog, 1) &&
              !WheelSeat(ReachVehicleId::Banshee, 0) && !WheelSeat(ReachVehicleId::Unknown, 0),
          "only a known ground vehicle's driver may take the wheel");
}

// Every seat a Reach player can sit in finds the built-in entry meant for it.
static void TestBuiltInEntries()
{
    char buffer[kNameCapacity];
    const ReachVehicleFingerprint none{};
    const auto entry = [&](ReachVehicleId id, int seat, bool onCarrier, ReachVehicleId carrier) {
        return EntryFor(SeatVehicleName(id, onCarrier, carrier, none, 0, buffer), SeatBits(id, seat, onCarrier, 0));
    };
    Check(entry(ReachVehicleId::Warthog, 0, false, ReachVehicleId::Unknown) == "Warthog" &&
              entry(ReachVehicleId::Warthog, 1, false, ReachVehicleId::Unknown) == "Warthog passenger",
          "the Warthog's driver and passenger");
    Check(entry(ReachVehicleId::WarthogChaingun, 0, true, ReachVehicleId::Warthog) == "Warthog gunner" &&
              entry(ReachVehicleId::WarthogGauss, 0, true, ReachVehicleId::Warthog) == "Warthog gunner" &&
              entry(ReachVehicleId::WarthogRocket, 0, true, ReachVehicleId::Warthog) == "Warthog gunner",
          "and each of its guns");
    Check(entry(ReachVehicleId::Mongoose, 1, false, ReachVehicleId::Unknown) == "Mongoose passenger" &&
              entry(ReachVehicleId::Revenant, 1, false, ReachVehicleId::Unknown) == "Revenant passenger",
          "the Mongoose's and the Revenant's passengers");
    Check(entry(ReachVehicleId::Ghost, 0, false, ReachVehicleId::Unknown) == "Ghost" &&
              entry(ReachVehicleId::SpaceBanshee, 0, false, ReachVehicleId::Unknown) == "Banshee" &&
              entry(ReachVehicleId::Sabre, 0, false, ReachVehicleId::Unknown) == "Sabre",
          "the Ghost, the space Banshee and the Sabre");
    Check(entry(ReachVehicleId::Falcon, 0, false, ReachVehicleId::Unknown) == "Falcon" &&
              entry(ReachVehicleId::Falcon, 4, false, ReachVehicleId::Unknown) == "Falcon passenger" &&
              entry(ReachVehicleId::PlasmaTurret, 0, true, ReachVehicleId::Falcon) == "Falcon gunner",
          "the Falcon's pilot, its passengers and its side guns");
    Check(entry(ReachVehicleId::Scorpion, 0, false, ReachVehicleId::Unknown) == "Scorpion" &&
              entry(ReachVehicleId::ScorpionAntiInfantry, 0, true, ReachVehicleId::Scorpion) == "Scorpion gunner" &&
              entry(ReachVehicleId::Wraith, 0, false, ReachVehicleId::Unknown) == "Wraith" &&
              entry(ReachVehicleId::WraithGunner, 0, true, ReachVehicleId::Wraith) == "Wraith gunner",
          "both tanks and their guns");
    Check(entry(ReachVehicleId::ShadePlasma, 0, false, ReachVehicleId::Unknown) == "Turret" &&
              entry(ReachVehicleId::Machinegun, 0, false, ReachVehicleId::Unknown) == "Turret" &&
              entry(ReachVehicleId::PlasmaTurret, 0, false, ReachVehicleId::Unknown) == "Turret",
          "every walk-up gun standing alone");
    Check(entry(ReachVehicleId::Forklift, 0, false, ReachVehicleId::Unknown) == "default" &&
              entry(ReachVehicleId::Unknown, 0, false, ReachVehicleId::Unknown) == "default",
          "and a vehicle with no entry of its own Reach's default");
}

int main()
{
    TestNames();
    TestRoles();
    TestBuiltInEntries();
    std::printf("PASS: %u Reach vehicle camera checks\n", g_checks);
    return 0;
}
