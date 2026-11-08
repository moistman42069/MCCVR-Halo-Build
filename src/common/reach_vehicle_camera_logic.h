#pragma once
// REACH'S SEATS AS THE VEHICLE CAMERA FILE NAMES THEM (vehicle_camera_file.h). The camera file finds a seat's
// entry by the vehicle's name and the seat's role; this is what a Reach seat goes by.
//
//   NAME. A vehicle the mod knows (reach_vehicle_logic.h's identities) goes by its stock tag's own word, the
//   one the per-seat trims already use: "warthog", "warthog_gauss", "space_banshee". A gun mounted on a vehicle
//   is its own vehicle object in Reach, named by its own tag, which carries its vehicle's word ("warthog_gauss",
//   "wraith_gunner", "scorpion_anti_infantry"); a walk-up gun riding a carrier (the Falcon's side guns are
//   plasma_turret's tag) goes by the CARRIER's name instead, as Halo 3 names a gunner by the vehicle carrying the
//   gun. A vehicle the mod does not know goes by "custom_" and a fingerprint of its identity tuple and physics
//   type, so each such vehicle keeps an entry, a seat memory and a note of its own.
//
//   ROLE. A known vehicle's seat 0 is its driver's (reach_vehicle_logic.h: ReachVehicleSeatIsDriver), a gun's
//   seat is its gunner's, a walk-up gun standing alone is both (one seat drives and fires it), and every other
//   seat is a passenger's. An unknown vehicle's seat is read from its own tag flags: bit 2 driver, bit 3 gunner.
//   Those two bits are an inference from the unit seat flags' shared order -- Reach's bits 4 (third-person camera)
//   and 5 (allows weapons) carry the same names there as in Halo 3, whose bits 2 and 3 are driver and gunner --
//   and every seat's line in the log prints its flags so a known seat can confirm it.
//
// Pure: no engine.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "reach_vehicle_logic.h"
#include "vehicle_boom_logic.h"
#include "vehicle_camera_file.h"

namespace reach_vehicle_camera
{
// The names, in ReachVehicleId's order from Banshee (identity 1). The same words as the per-seat trims'.
inline constexpr const char* kIdentityNames[] = {
    "banshee", "space_banshee", "ghost", "revenant", "wraith",
    "wraith_gunner", "mongoose", "warthog", "warthog_chaingun",
    "warthog_gauss", "warthog_rocket", "falcon", "sabre", "scorpion",
    "forklift", "cart", "shade_plasma", "shade_flak", "plasma_turret",
    "machinegun", "mac_cannon", "scorpion_anti_infantry", "seraph",
    "pelican", "pelican_chin_gun", "corvette_cannon",
    "space_phantom_chin_gun", "space_phantom_beam_turret", "cargo_truck",
    "military_truck", "oni_van", "pickup", "truck_cab_large",
    "squad_drop_pod"};
static_assert(sizeof(kIdentityNames) / sizeof(kIdentityNames[0]) == static_cast<std::size_t>(kReachVehicleIdentityCount),
              "one name for every Reach vehicle identity");

inline constexpr uint32_t kSeatDriverBit = 1u << 2;
inline constexpr uint32_t kSeatGunnerBit = 1u << 3;

// The name of a known identity; null for Unknown or a code out of range.
inline const char* IdentityName(ReachVehicleId id) noexcept
{
    const int code = static_cast<int>(id);
    return code >= 1 && code <= kReachVehicleIdentityCount ? kIdentityNames[code - 1] : nullptr;
}

// A gun this seat aims: one mounted on a vehicle, or a walk-up gun (on a carrier or standing alone).
inline bool IsGun(ReachVehicleId id) noexcept
{
    return ReachVehicleIsAttachedWeapon(id) || ReachVehicleIsWalkUpTurret(id);
}

// The seat's role as the camera file's SeatBits. `onCarrier`: the seated object rides another (the engine's own
// ultimate parent is not itself).
inline uint8_t SeatBits(ReachVehicleId id, int seatIndex, bool onCarrier, uint32_t seatFlags) noexcept
{
    using vehicle_camera_file::SeatBits;
    if (id == ReachVehicleId::Unknown)
        return SeatBits(!onCarrier && (seatFlags & kSeatDriverBit) != 0, (seatFlags & kSeatGunnerBit) != 0);
    if (ReachVehicleIsWalkUpTurret(id))
        return SeatBits(!onCarrier, true);
    if (ReachVehicleIsAttachedWeapon(id))
        return SeatBits(false, true);
    return SeatBits(ReachVehicleSeatIsDriver(id, seatIndex), false);
}

// A seat whose occupant may take the steering wheel: a known ground vehicle's driver (reach_vehicle_logic.h).
inline bool WheelSeat(ReachVehicleId id, int seatIndex) noexcept
{
    return ReachVehicleSeatIsDriver(id, seatIndex) && ReachVehicleUsesWheel(id);
}

// "custom_" and eight hex digits: FNV-1a over the identity tuple's four words and the physics type.
inline constexpr std::size_t kNameCapacity = 40;
inline void CustomName(const ReachVehicleFingerprint& fingerprint, int physicsType, char (&out)[kNameCapacity]) noexcept
{
    uint32_t hash = 2166136261u;
    const uint32_t words[5] = {fingerprint.radius, fingerprint.offsetX, fingerprint.offsetY, fingerprint.offsetZ,
                               static_cast<uint32_t>(physicsType)};
    for (const uint32_t word : words)
        for (int shift = 0; shift < 32; shift += 8)
        {
            hash ^= (word >> shift) & 0xFFu;
            hash *= 16777619u;
        }
    std::snprintf(out, kNameCapacity, "custom_%08x", static_cast<unsigned>(hash));
}

// THE GAME'S OWN THIRD-PERSON CAMERA ORBITS THE SEAT'S PIVOT (measured in a Mongoose: the same 7 m from the point
// unit_get_camera_info gives the seat at looks of -7.6 and +2.2 deg, its elevation falling as the look rises). Where it
// stands looking level is therefore the elevation it stood at PLUS the look it had then, in whole degrees (so a boom
// that starts from it is not moved for a fraction). `rise` is its height above the pivot, `distance` how far it
// stands, `lookDeg` its look (up is positive). Unset (vehicle_boom::kBoomPitchUnset) when the two do not make an angle.
inline float OrbitLevelElevationDeg(float rise, float distance, float lookDeg) noexcept
{
    if (!std::isfinite(rise) || !std::isfinite(distance) || !std::isfinite(lookDeg) || !(distance > 1.0e-3f) ||
        std::fabs(rise) > distance)
        return vehicle_boom::kBoomPitchUnset;
    const float degrees = std::asin(rise / distance) / vehicle_boom::kRadiansPerDegree + lookDeg;
    return std::isfinite(degrees) ? std::round(degrees) : vehicle_boom::kBoomPitchUnset;
}

// THE BOOM'S RANGE in Reach, as look pitches for vehicle_boom's orbit (the camera's elevation is minus the pitch):
// Halo 3's track bounds its boom to its own swing; with no track, the orbit over the whole flat look range stood the
// camera straight above the vehicle or under the ground (measured). Held between 10 deg below and 60 deg above level.
inline constexpr float kBoomLowestDeg = -10.0f;
inline constexpr float kBoomHighestDeg = 60.0f;
inline vehicle_boom::Range BoomRange() noexcept
{
    return vehicle_boom::Range{-kBoomHighestDeg * vehicle_boom::kRadiansPerDegree,
                               -kBoomLowestDeg * vehicle_boom::kRadiansPerDegree};
}

// The seat's vehicle as the camera file names it. `carrier` is the identity of the object the seated one rides
// (Unknown when it rides none, or the carrier is not known).
inline const char* SeatVehicleName(ReachVehicleId id, bool onCarrier, ReachVehicleId carrier,
                                   const ReachVehicleFingerprint& fingerprint, int physicsType,
                                   char (&buffer)[kNameCapacity]) noexcept
{
    if (onCarrier && ReachVehicleIsWalkUpTurret(id) && IdentityName(carrier))
        return IdentityName(carrier);
    if (const char* name = IdentityName(id))
        return name;
    CustomName(fingerprint, physicsType, buffer);
    return buffer;
}
} // namespace reach_vehicle_camera
