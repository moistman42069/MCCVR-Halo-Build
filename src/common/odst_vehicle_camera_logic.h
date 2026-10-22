#pragma once
// ODST'S SEATS AS THE VEHICLE CAMERA FILE NAMES THEM (vehicle_camera_file.h). The camera file finds a seat's entry
// by the vehicle's name and the seat's role; this is what an ODST seat goes by.
//
//   NAME. A vehicle the mod knows (odst_vehicle_logic.h's identities) goes by OdstVehicleIdName's word, the one
//   Halo 3's seats go by ("warthog", "mongoose", "prowler", "turret"), and the Shade by "shade". A gun mounted on a
//   vehicle (the Warthog's, the Scorpion's, the Wraith's) is its own object in ODST, with turret physics, and the mod
//   knows it as a turret; seated on a carrier it goes by the CARRIER's name, as Halo 3 names a gunner by the vehicle
//   carrying the gun.
//
//   ROLE. A known vehicle's seat 0 is its driver's (OdstSeatIsDriver), a gun riding a carrier is its gunner's, a
//   walk-up gun or the Shade standing alone is both (one seat drives and fires it), and every other seat is a
//   passenger's: the Warthog's and the Mongoose's second seats, and the four riders ODST seats on the Scorpion.
//
// Pure: no engine.

#include <cstdint>

#include "odst_vehicle_logic.h"
#include "vehicle_camera_file.h"

namespace odst_vehicle_camera
{
// A seat on a gun: the walk-up and mounted turrets the mod knows as turrets, and the Shade.
inline constexpr bool IsGun(OdstVehicleId id) noexcept
{
    return id == OdstVehicleId::StationaryTurret || id == OdstVehicleId::Shade;
}

// A gunner on a carrier's gun: the seated object is a gun (or one the mod does not know) and rides a vehicle.
inline constexpr bool GunOnCarrier(OdstVehicleId id, bool onCarrier) noexcept
{
    return onCarrier && (IsGun(id) || id == OdstVehicleId::Unknown);
}

// The seat's vehicle as the camera file names it. `carrier`: the identity of the vehicle the seated object rides
// (Unknown when it rides none, or the carrier is not one the mod knows).
inline const char* SeatVehicleName(OdstVehicleId id, bool onCarrier, OdstVehicleId carrier) noexcept
{
    if (GunOnCarrier(id, onCarrier) && carrier != OdstVehicleId::Unknown)
        return OdstVehicleIdName(carrier);
    return OdstVehicleIdName(id);
}

// The seat's role as the camera file's SeatBits.
inline constexpr uint8_t SeatBits(OdstVehicleId id, int seatIndex, bool onCarrier) noexcept
{
    using vehicle_camera_file::SeatBits;
    if (GunOnCarrier(id, onCarrier))
        return SeatBits(false, true);
    if (IsGun(id))
        return SeatBits(true, true);
    return SeatBits(!onCarrier && OdstSeatIsDriver(id, seatIndex, false), false);
}

// A seat whose occupant may take the steering wheel: a look-steered ground vehicle's driver (odst_vehicle_logic.h).
inline constexpr bool WheelSeat(OdstVehicleId id, int seatIndex, bool onCarrier) noexcept
{
    return !onCarrier && OdstSeatIsDriver(id, seatIndex, false) && OdstVehicleUsesWheel(id);
}

// The seat key's title mark (vehicle_cameras.inl's VehicleCameraSeatKey::title): ODST's seats are keyed apart from
// Halo 3's, whose generations count from the same start.
inline constexpr uint8_t kSeatKeyTitle = 1;
} // namespace odst_vehicle_camera
