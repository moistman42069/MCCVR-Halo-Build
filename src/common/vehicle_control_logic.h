#pragma once
// HOW A VEHICLE SEAT IS CONTROLLED (vehicle_control): the hand, the stick or the wheel -- its own choice, apart
// from how the view turns.
//
//   Motion  the hand aims. A gunner's or passenger's gun points where the hand does, and a look-steered vehicle
//           (Warthog, Mongoose, Ghost, Prowler, Chopper, Banshee, Hornet) turns toward where it points.
//   Stick   the right stick aims and steers, as on a flat screen, in every seat. The hand aims nothing.
//   Wheel   two hands on an invisible wheel steer a look-steered ground vehicle (an aircraft takes the stick
//           for its heading). Every other seat is aimed by the hand, as in Motion.
// Whether the VIEW turns with the vehicle is a separate matter -- the view-follow setting, or the tethering mode
// of a camera from the vehicle camera file -- and any of the three may be had with either.
//
// Automatic keeps the two tied as they were before the choice existed: the hand aims every seat, except that
// the stick or wheel steers a look-steered driver whose view turns with the vehicle.
//
// With the hand steering a view that turns with the vehicle, the hand's angle off straight ahead is a turn RATE,
// not a heading: the vehicle keeps turning for as long as the hand points aside, and straightens when the hand
// does.
//
// Pure: no engine.

#include "halo3_vehicle_logic.h"

namespace vehicle_control
{
enum class Mode : int { Automatic = 0, Motion = 1, Stick = 2, Wheel = 3 };
inline constexpr int kModeCount = 4;

inline constexpr Mode FromConfig(int value) noexcept
{
    return value >= 0 && value < kModeCount ? static_cast<Mode>(value) : Mode::Automatic;
}

// Does the stick or wheel steer this seat's HEADING, in place of the hand's aim? Only ever a look-steered
// driver's: `lookSteeredDriver` is that seat. `viewFollows`: the view turns with the vehicle in it right now.
// This is the seat the wheel may be taken in, and the one whose turn stick is steering and not turning the view.
inline constexpr bool Authored(Mode mode, bool lookSteeredDriver, bool viewFollows) noexcept
{
    if (!lookSteeredDriver)
        return false;
    switch (mode)
    {
    case Mode::Motion:
        return false;
    case Mode::Stick:
    case Mode::Wheel:
        return true;
    default:
        return viewFollows;
    }
}

// Does the right stick aim this seat on BOTH axes, the hand aiming nothing in it? Stick, in any seat: the
// hand's aim is then neither written nor steered toward, and the reticle belongs to the game's own aim.
inline constexpr bool StickAims(Mode mode) noexcept
{
    return mode == Mode::Stick;
}

// THE LEFT STICK CLICK'S NEXT CHOICE for a seat now controlled by `current`: Motion, then Stick, then -- in a
// seat that can take the wheel (`wheelSeat`: a look-steered ground vehicle's driver) -- Wheel, and round again.
// Automatic is where the seat was before any choice; the click moves it on as from Motion.
inline constexpr Mode Next(Mode current, bool wheelSeat) noexcept
{
    switch (current)
    {
    case Mode::Stick:
        return wheelSeat ? Mode::Wheel : Mode::Motion;
    case Mode::Wheel:
        return Mode::Motion;
    default:
        return Mode::Stick;
    }
}

inline constexpr const char* Name(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::Motion:
        return "Motion (your hand)";
    case Mode::Stick:
        return "Stick";
    case Mode::Wheel:
        return "Wheel";
    default:
        return "As before";
    }
}

// Is the turn stick busy with the seat, so that it must not turn the view as well? A stick-aimed seat's always
// is. A seat whose heading is authored has it busy until the wheel is in hand and steering.
inline constexpr bool StickBusy(bool authored, bool stickAims, bool wheelSteering) noexcept
{
    if (authored && wheelSteering)
        return false;
    return authored || stickAims;
}

// WHEEL AS THE CHOSEN WAY TO STEER. The wheel is put in the player's hands when a seat first offers one, once
// per offer: the double-click that lets go of it (Halo3UpdateWheelToggle) is not undone a moment later, and
// takes it again as before. `available`: the seat can use a wheel this poll. Call after the toggle. True when
// this call took the wheel.
struct WheelOffer
{
    bool made = false;
};
inline bool TakeWheelOnOffer(Halo3Wheel& wheel, WheelOffer& offer, bool available, bool wheelChosen) noexcept
{
    if (!available || !wheelChosen)
    {
        offer.made = false;
        return false;
    }
    if (offer.made)
        return false;
    offer.made = true;
    if (wheel.engaged)
        return false;
    wheel.engaged = true;
    wheel.clicks = 0;
    return true;
}
} // namespace vehicle_control
