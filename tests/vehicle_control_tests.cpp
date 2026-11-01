// How a vehicle seat is controlled (src/common/vehicle_control_logic.h): the choice of hand, stick or wheel
// apart from how the view turns, which seats the stick aims, and the wheel handed over when it is the chosen way.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/common/vehicle_control_logic.h"

using namespace vehicle_control;

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

static void TestWhoSteers()
{
    // Automatic is the rule the mod had before the choice: it must agree with it seat for seat.
    for (const bool follows : {false, true})
    {
        Check(Authored(Mode::Automatic, true, follows) ==
                  Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, false, follows),
              "automatic steers a look-steered driver as before: the stick or wheel when the view follows");
        Check(Authored(Mode::Automatic, false, follows) ==
                  Halo3SeatAuthorsSteering(Halo3VehicleId::Scorpion, 0, false, follows),
              "and leaves a tank driver's turret to the hand as before");
    }
    Check(!Authored(Mode::Automatic, true, false) && Authored(Mode::Automatic, true, true),
          "automatic: the hand in a still view, the stick or wheel in a following one");

    for (const bool follows : {false, true})
    {
        Check(!Authored(Mode::Motion, true, follows), "motion: the hand steers, however the view turns");
        Check(Authored(Mode::Stick, true, follows), "stick: the stick steers, however the view turns");
        Check(Authored(Mode::Wheel, true, follows), "wheel: the wheel or stick steers, however the view turns");
        for (const Mode mode : {Mode::Automatic, Mode::Motion, Mode::Stick, Mode::Wheel})
            Check(!Authored(mode, false, follows), "no mode steers a seat that is not a look-steered driver's");
    }

    Check(FromConfig(0) == Mode::Automatic && FromConfig(1) == Mode::Motion && FromConfig(2) == Mode::Stick &&
              FromConfig(3) == Mode::Wheel,
          "the config's numbers");
    Check(FromConfig(-1) == Mode::Automatic && FromConfig(4) == Mode::Automatic && FromConfig(999) == Mode::Automatic,
          "a number that names no mode is automatic");
}

static void TestStickAims()
{
    Check(StickAims(Mode::Stick), "stick: the right stick aims the seat, whichever seat it is");
    Check(!StickAims(Mode::Motion) && !StickAims(Mode::Wheel) && !StickAims(Mode::Automatic),
          "in every other mode the hand aims: a wheel steers a heading, it aims no gun");

    // Who has the turn stick: the seat, or the view.
    Check(!StickBusy(false, false, false), "a hand-aimed seat leaves the stick to turn the view");
    Check(StickBusy(false, true, false), "a stick-aimed gunner's stick is aiming");
    Check(StickBusy(true, false, false), "a stick-steered driver's stick is steering");
    Check(StickBusy(true, true, false), "and so is a stick-aimed driver's");
    Check(!StickBusy(true, false, true) && !StickBusy(true, true, true),
          "with the wheel in hand and steering, the stick is free to turn the view");
    Check(StickBusy(false, true, true), "a wheel that is not this seat's frees nothing");
}

static void TestNext()
{
    Check(Next(Mode::Motion, true) == Mode::Stick && Next(Mode::Stick, true) == Mode::Wheel &&
              Next(Mode::Wheel, true) == Mode::Motion,
          "a seat with a wheel goes round Motion, Stick, Wheel");
    Check(Next(Mode::Motion, false) == Mode::Stick && Next(Mode::Stick, false) == Mode::Motion,
          "a seat without one goes between Motion and Stick");
    Check(Next(Mode::Wheel, false) == Mode::Motion, "a wheel chosen elsewhere is left for the hand");
    Check(Next(Mode::Automatic, true) == Mode::Stick && Next(Mode::Automatic, false) == Mode::Stick,
          "from as before, the click moves on as from Motion");
    Check(Name(Mode::Stick)[0] == 'S' && Name(Mode::Automatic)[0] == 'A', "each mode has a name for the readout");
}

static void TestWheelOnOffer()
{
    Halo3Wheel wheel;
    WheelOffer offer;
    Check(!TakeWheelOnOffer(wheel, offer, false, true) && !wheel.engaged, "no wheel is taken in a seat that offers none");
    Check(!TakeWheelOnOffer(wheel, offer, true, false) && !wheel.engaged, "nor when the wheel is not the chosen way");

    Check(TakeWheelOnOffer(wheel, offer, true, true) && wheel.engaged, "the chosen wheel is taken when the seat offers it");
    Check(!TakeWheelOnOffer(wheel, offer, true, true) && wheel.engaged, "once: the next poll takes nothing more");

    // The player lets go with the double-click; the offer is spent, so the wheel stays let go.
    auto click = [&wheel](uint64_t at) {
        Halo3UpdateWheelToggle(wheel, true, 0.9f, 0.9f, at);
        return Halo3UpdateWheelToggle(wheel, true, 0.0f, 0.0f, at + 5);
    };
    click(1000);
    Check(click(1100) && !wheel.engaged, "a double-click lets go of it");
    Check(!TakeWheelOnOffer(wheel, offer, true, true) && !wheel.engaged, "and it is not handed straight back");
    click(2000);
    Check(click(2100) && wheel.engaged, "a double-click takes it again, as before");

    // Leaving the seat ends the offer; the next seat makes a new one.
    Halo3UpdateWheelToggle(wheel, false, 0.0f, 0.0f, 3000);
    Check(!wheel.engaged && !TakeWheelOnOffer(wheel, offer, false, true), "leaving the seat lets go");
    Check(TakeWheelOnOffer(wheel, offer, true, true) && wheel.engaged, "and the next seat hands the wheel over again");

    // Choosing another way to steer ends the offer too.
    Check(!TakeWheelOnOffer(wheel, offer, true, false), "another way chosen takes nothing");
    wheel.engaged = false;
    Check(TakeWheelOnOffer(wheel, offer, true, true) && wheel.engaged, "and choosing the wheel again hands it over");

    // A wheel already in hand is left as it is, and the offer is spent all the same.
    Halo3Wheel held;
    held.engaged = true;
    WheelOffer fresh;
    Check(!TakeWheelOnOffer(held, fresh, true, true) && held.engaged && fresh.made, "a wheel already held is not taken twice");
}

// Following the hull and authoring the steering for every seat with a view (halo3_vehicle_logic.h): a tuned seat
// answers exactly as before; a seat with no tuned point (a mod map's vehicle, the Elephant) by its role.
static void TestFollowForEverySeat()
{
    using Id = Halo3VehicleId;
    const Id named[] = {Id::Scorpion, Id::Warthog, Id::Mongoose, Id::Ghost,   Id::Wraith,
                        Id::Mauler,   Id::Banshee, Id::Hornet,   Id::Chopper, Id::StationaryTurret};
    unsigned tuned = 0;
    for (Id id : named)
        for (int seatIndex = -1; seatIndex < 12; ++seatIndex)
            for (bool mounted : {false, true})
            {
                if (!Halo3FindSeatPoint(id, seatIndex, mounted) && id != Id::StationaryTurret)
                    continue;
                ++tuned;
                for (bool driver : {false, true})
                    Check(Halo3SeatViewFollowsHull(id, Id::Unknown, driver, seatIndex, mounted, true) ==
                                  Halo3SeatFollowsHull(id, seatIndex, mounted) &&
                              Halo3SeatViewFollowsPitch(id, Id::Unknown, driver, seatIndex, mounted, true) ==
                                  Halo3SeatFollowsPitch(id, seatIndex, mounted),
                          "a tuned seat follows its hull exactly as before");
                const Halo3SeatRole role = Halo3SeatRoleOf(id, Id::Unknown, seatIndex, mounted, 0);
                Check(Halo3SeatRoleAuthorsSteering(role, true) == Halo3SeatAuthorsSteering(id, seatIndex, mounted, true) &&
                          !Halo3SeatRoleAuthorsSteering(role, false),
                      "and has its steering authored exactly as before");
                Check(Halo3SeatRoleUsesWheel(role) == Halo3VehicleUsesWheel(id), "and takes the wheel exactly as before");
            }
    Check(tuned > 10, "the tuned seats were all asked");

    const auto Follows = [](Id cls, bool driver, bool eye) {
        return Halo3SeatViewFollowsHull(Id::Unknown, cls, driver, 0, false, eye);
    };
    const auto Pitch = [](Id cls, bool driver) { return Halo3SeatViewFollowsPitch(Id::Unknown, cls, driver, 0, false, true); };
    Check(Follows(Id::Warthog, true, true) && Pitch(Id::Warthog, true), "a mod jeep's driver follows in yaw and pitch");
    Check(Follows(Id::Hornet, true, true) && !Pitch(Id::Hornet, true) && Follows(Id::Banshee, true, true) &&
              !Pitch(Id::Banshee, true),
          "a mod flyer's pilot follows in yaw only");
    Check(Follows(Id::Scorpion, true, true) && Pitch(Id::Scorpion, true), "a mod tank's driver follows");
    Check(!Follows(Id::Unknown, true, true) && Follows(Id::Unknown, false, true),
          "the driver of a vehicle of no known class keeps the view; its other seats follow");
    Check(!Follows(Id::StationaryTurret, false, true) && !Follows(Id::StationaryTurret, true, true),
          "never on a turret's own frame");
    Check(!Follows(Id::Warthog, false, false), "never a seat with no view");

    const Halo3SeatRole pilot = Halo3SeatRoleOf(Id::Unknown, Id::Hornet, 0, false, kHalo3SeatDriverBit);
    Check(Halo3SeatRoleAuthorsSteering(pilot, true) && !Halo3SeatRoleUsesWheel(pilot),
          "a mod flyer's pilot is steered by the stick, never a wheel");
    const Halo3SeatRole jeep = Halo3SeatRoleOf(Id::Unknown, Id::Warthog, 0, false, kHalo3SeatDriverBit);
    Check(Halo3SeatRoleAuthorsSteering(jeep, true) && Halo3SeatRoleUsesWheel(jeep), "a mod jeep's driver may take the wheel");
    const Halo3SeatRole rider = Halo3SeatRoleOf(Id::Unknown, Id::Warthog, 1, false, 0);
    Check(!rider.driver && !Halo3SeatRoleAuthorsSteering(rider, true) && Follows(Id::Warthog, rider.driver, true),
          "its passenger follows the hull and steers nothing");
    const Halo3SeatRole tank = Halo3SeatRoleOf(Id::Unknown, Id::Scorpion, 0, false, kHalo3SeatDriverBit);
    Check(tank.driver && !Halo3SeatRoleAuthorsSteering(tank, true), "a mod tank's look aims its gun: nothing to author");

    Check(!Halo3FindSeatPoint(Id::Elephant, 0, false) &&
              Halo3SeatViewFollowsHull(Id::Elephant, Id::Scorpion, true, 0, false, true) &&
              Halo3SeatViewFollowsPitch(Id::Elephant, Id::Scorpion, true, 0, false, true),
          "the Elephant, named with no tuned point, follows its hull from the occupant's head");
}

int main()
{
    TestWhoSteers();
    TestStickAims();
    TestNext();
    TestWheelOnOffer();
    TestFollowForEverySeat();
    std::printf("PASS: %u vehicle control checks\n", g_checks);
    return 0;
}
