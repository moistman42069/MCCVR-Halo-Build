#pragma once
// HALO 2'S SEATS AS THE VEHICLE CAMERA FILE NAMES THEM (vehicle_camera_file.h). The camera file finds a seat's entry
// by the vehicle's name and the seat's role; this is what a Halo 2 seat goes by.
//
//   NAME. A vehicle the mod knows (halo2_vehicle_identity.h: its render model, matched by node count and bounds) goes
//   by that model's name in lower case, each run of spaces and slashes an underscore: "warthog", "ghost",
//   "banshee". A gun mounted on a vehicle is its own object in Halo 2, and its model's name already carries its
//   vehicle's ("Warthog / Chaingun" is "warthog_chaingun", "Wraith / Minigun" "wraith_minigun"), so its gunner finds
//   the vehicle's entries. A gun the mod does not know riding a vehicle it does goes by that vehicle's name and
//   "_gun". Anything else goes by "custom_" and a fingerprint of its render model's bounds and node count, so each
//   such vehicle keeps an entry, a seat memory and a note of its own.
//
//   ROLE. The seat's own flags where they were read: bit 2 driver, bit 3 gunner (seat_converge_logic.h reads Halo 2's
//   flags the same way, from the engine's own driver and gunner predicates). Where they were not, a gun riding a
//   carrier is its gunner's, seat 0 of anything else its driver's, and every other seat a passenger's.
//
// Pure: no engine.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "vehicle_boom_logic.h"
#include "vehicle_camera_file.h"

namespace halo2_vehicle_camera
{
inline constexpr std::size_t kNameCapacity = 48;

inline constexpr uint32_t kSeatDriverBit = 1u << 2;
inline constexpr uint32_t kSeatGunnerBit = 1u << 3;

// The vehicle camera key's 32 bits of a 64-bit model identity (0 stays 0: no identity).
inline constexpr uint32_t KeyIdentity(uint64_t identity) noexcept
{
    return static_cast<uint32_t>(identity ^ (identity >> 32));
}

// A model's name as the camera file uses it: lower case, every run of spaces and slashes one underscore, no
// underscore at either end. False (and `out` empty) for no name, or one that does not fit.
inline bool FileName(const char* modelName, char (&out)[kNameCapacity]) noexcept
{
    out[0] = '\0';
    if (!modelName)
        return false;
    std::size_t length = 0;
    bool gap = false;
    for (const char* c = modelName; *c; ++c)
    {
        const char ch = *c;
        if (ch == ' ' || ch == '/' || ch == '\t')
        {
            gap = length != 0;
            continue;
        }
        if (length + (gap ? 2u : 1u) >= kNameCapacity)
        {
            out[0] = '\0';
            return false;
        }
        if (gap)
        {
            out[length++] = '_';
            gap = false;
        }
        out[length++] = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    out[length] = '\0';
    return length != 0;
}

// The seat's role as the camera file's SeatBits. `onCarrier`: the seated object rides another.
inline uint8_t SeatBits(bool flagsKnown, uint32_t flags, bool onCarrier, int seatIndex) noexcept
{
    using vehicle_camera_file::SeatBits;
    if (flagsKnown)
        return SeatBits((flags & kSeatDriverBit) != 0, (flags & kSeatGunnerBit) != 0);
    if (onCarrier)
        return SeatBits(false, true);
    return SeatBits(seatIndex == 0, false);
}

// "custom_" and eight hex digits: FNV-1a over the render model's six bounds and its node count.
inline void CustomName(const float bounds[6], unsigned nodes, char (&out)[kNameCapacity]) noexcept
{
    uint32_t hash = 2166136261u;
    const auto mix = [&hash](uint32_t word) {
        for (int shift = 0; shift < 32; shift += 8)
        {
            hash ^= (word >> shift) & 0xFFu;
            hash *= 16777619u;
        }
    };
    for (int i = 0; i < 6; ++i)
    {
        uint32_t word = 0;
        const float value = bounds ? bounds[i] : 0.0f;
        static_assert(sizeof(word) == sizeof(value), "a float is one word");
        std::memcpy(&word, &value, sizeof(word));
        mix(word);
    }
    mix(nodes);
    std::snprintf(out, kNameCapacity, "custom_%08x", static_cast<unsigned>(hash));
}

// The seat's vehicle as the camera file names it. `modelName` is the seated object's model name (null: not one the
// mod knows), `carrierModelName` the name of the vehicle it rides (null: it rides none, or one the mod does not
// know); `bounds` and `nodes` are the seated object's render model's, for a name of its own.
inline const char* SeatVehicleName(const char* modelName, const char* carrierModelName, bool onCarrier,
                                   const float bounds[6], unsigned nodes, char (&buffer)[kNameCapacity]) noexcept
{
    if (FileName(modelName, buffer))
        return buffer;
    if (onCarrier && FileName(carrierModelName, buffer))
    {
        std::size_t length = 0;
        while (buffer[length])
            ++length;
        if (length + 4 < kNameCapacity)
        {
            buffer[length] = '_';
            buffer[length + 1] = 'g';
            buffer[length + 2] = 'u';
            buffer[length + 3] = 'n';
            buffer[length + 4] = '\0';
            return buffer;
        }
    }
    CustomName(bounds, nodes, buffer);
    return buffer;
}

// A render model's size: half the diagonal of its bounding box (min x, max x, min y, max y, min z, max z), world
// units. 0 for bounds that are not a box.
inline float BoundsRadius(const float bounds[6]) noexcept
{
    if (!bounds)
        return 0.0f;
    float sum = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float low = bounds[axis * 2], high = bounds[axis * 2 + 1];
        if (!std::isfinite(low) || !std::isfinite(high) || high < low)
            return 0.0f;
        sum += (high - low) * (high - low);
    }
    const float radius = 0.5f * std::sqrt(sum);
    return std::isfinite(radius) ? radius : 0.0f;
}

// THE GAME'S OWN THIRD-PERSON CAMERA TURNS ABOUT THE SEAT'S PIVOT, as Halo 1's following camera does and as Reach's
// was measured to: where it stands looking level is the elevation it stood at PLUS the look it had then, in whole
// degrees (so a boom that starts from it is not moved for a fraction). `rise` is its height above the pivot,
// `distance` how far it stands, `lookDeg` its look (up is positive). Unset (vehicle_boom::kBoomPitchUnset) when the
// two do not make an angle.
inline float OrbitLevelElevationDeg(float rise, float distance, float lookDeg) noexcept
{
    if (!std::isfinite(rise) || !std::isfinite(distance) || !std::isfinite(lookDeg) || !(distance > 1.0e-3f) ||
        std::fabs(rise) > distance)
        return vehicle_boom::kBoomPitchUnset;
    const float degrees = std::asin(rise / distance) / vehicle_boom::kRadiansPerDegree + lookDeg;
    return std::isfinite(degrees) ? std::round(degrees) : vehicle_boom::kBoomPitchUnset;
}

// THE BOOM'S RANGE in Halo 2, as look pitches for vehicle_boom's orbit (the camera's elevation is minus the pitch):
// Halo 2's camera track is not read, so the orbit is held between 10 deg below and 60 deg above level, as Reach's
// (the whole flat look range stood the camera straight above the vehicle or under the ground there).
inline constexpr float kBoomLowestDeg = -10.0f;
inline constexpr float kBoomHighestDeg = 60.0f;
inline vehicle_boom::Range BoomRange() noexcept
{
    return vehicle_boom::Range{-kBoomHighestDeg * vehicle_boom::kRadiansPerDegree,
                               -kBoomLowestDeg * vehicle_boom::kRadiansPerDegree};
}
} // namespace halo2_vehicle_camera
