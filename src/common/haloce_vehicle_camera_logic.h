#pragma once
// HALO CE'S SEATS AS THE VEHICLE CAMERA FILE NAMES THEM (vehicle_camera_file.h, the "halo1" section), and the parts of
// a CE chase camera's placement that need no engine.
//
//   NAME. A vehicle goes by its definition's tag path. CE's own vehicles live under "vehicles\<word>\" (warthog,
//   rwarthog, ghost, banshee, scorpion, c gun turret, wraith, pelican), and the folder's word is the name, whichever
//   tag of that folder it is: the campaign's "warthog" and multiplayer's "mp_warthog" are one vehicle with one set of
//   seats. A tag outside those folders (a map's own vehicle) goes by its own short name. Names are lower case with
//   spaces as underscores ("c_gun_turret"). Where no path can be read, "custom_" and the hex of the model's node-name
//   fingerprint (haloce_vehicle_identity.inl), so each such vehicle keeps an entry, a seat memory and a note of its own.
//
//   ROLE. From the seat's own tag flags, as the Halo 1 engine names them: bit 2 driver, bit 3 gunner (a Ghost's or a
//   Banshee's one seat is both). Where the flags cannot be read, from the vehicle's two occupant slots: the unit
//   whose facing drives it is its driver, the one whose aim it takes its gunner.
//
//   THE GAME'S OWN THIRD-PERSON CAMERA. CE's following camera hands its observer a pivot (the seat camera's marker,
//   as drawn), an offset laid along the look's heading (forward, RIGHT, up) and a distance back along the look. The
//   observer stands the camera at pivot + offset - forward * distance (collision aside), so where the game's camera
//   stands from its pivot is worked out from the command alone (GameCameraFromPivot).
//
// Pure: no engine.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "vehicle_boom_logic.h"
#include "vehicle_camera_file.h"

namespace haloce_vehicle_camera
{
inline constexpr std::size_t kNameCapacity = 64;

// The seat flags' bits the role is read from (unit_seat flags).
inline constexpr uint32_t kSeatDriverBit = 1u << 2;
inline constexpr uint32_t kSeatGunnerBit = 1u << 3;
inline constexpr uint32_t kSeatThirdPersonBit = 1u << 4;

// The stock folders under "vehicles\", and the name each gives.
struct StockFolder
{
    const char* folder;
    const char* name;
};
inline constexpr StockFolder kStockFolders[] = {
    {"warthog", "warthog"},       {"rwarthog", "rocket_warthog"}, {"ghost", "ghost"},     {"banshee", "banshee"},
    {"scorpion", "scorpion"},     {"c gun turret", "shade"},      {"wraith", "wraith"},   {"pelican", "pelican"},
    {"c_dropship", "dropship"},
};

inline char LowerAscii(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// `text` lowered, spaces as underscores, anything not a letter, digit, '_' or '-' left out; at most out's capacity.
// False when nothing usable is left.
inline bool Sanitize(const char* text, std::size_t length, char (&out)[kNameCapacity]) noexcept
{
    std::size_t written = 0;
    for (std::size_t i = 0; i < length && text[i] && written + 1 < kNameCapacity; ++i)
    {
        const char c = LowerAscii(text[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            out[written++] = c;
        else if (c == ' ')
            out[written++] = '_';
    }
    out[written] = '\0';
    return written != 0;
}

// The vehicle's name from its tag path ("vehicles\warthog\mp_warthog" -> "warthog"). False (and `out` empty) for a
// path with nothing to name.
inline bool NameFromTagPath(const char* path, char (&out)[kNameCapacity]) noexcept
{
    out[0] = '\0';
    if (!path || !path[0])
        return false;
    // Its components, split at either slash.
    const char* parts[8]{};
    std::size_t lengths[8]{};
    std::size_t count = 0;
    const char* start = path;
    for (const char* c = path;; ++c)
    {
        if (*c == '\\' || *c == '/' || *c == '\0')
        {
            if (c > start && count < 8)
            {
                parts[count] = start;
                lengths[count] = static_cast<std::size_t>(c - start);
                ++count;
            }
            if (*c == '\0')
                break;
            start = c + 1;
        }
    }
    if (!count)
        return false;
    // "vehicles\<folder>\...": a stock folder names it.
    if (count >= 3 && lengths[0] == 8)
    {
        bool vehicles = true;
        for (std::size_t i = 0; i < 8; ++i)
            vehicles = vehicles && LowerAscii(parts[0][i]) == "vehicles"[i];
        if (vehicles)
            for (const StockFolder& stock : kStockFolders)
            {
                std::size_t want = 0;
                while (stock.folder[want])
                    ++want;
                if (want != lengths[1])
                    continue;
                bool same = true;
                for (std::size_t i = 0; i < want; ++i)
                    same = same && LowerAscii(parts[1][i]) == stock.folder[i];
                if (same)
                {
                    std::snprintf(out, kNameCapacity, "%s", stock.name);
                    return true;
                }
            }
    }
    // Else its own short name.
    return Sanitize(parts[count - 1], lengths[count - 1], out);
}

// "custom_" and the fingerprint in hex, for a vehicle whose path could not be read.
inline void CustomName(uint64_t modelIdentity, char (&out)[kNameCapacity]) noexcept
{
    std::snprintf(out, kNameCapacity, "custom_%016llx", static_cast<unsigned long long>(modelIdentity));
}

// The seat's role as the camera file's SeatBits: from its tag flags where they were read, else from the occupant
// slots the player's unit fills.
inline uint8_t SeatBits(bool flagsRead, uint32_t seatFlags, bool drivesIt, bool aimsIt) noexcept
{
    if (flagsRead)
        return vehicle_camera_file::SeatBits((seatFlags & kSeatDriverBit) != 0, (seatFlags & kSeatGunnerBit) != 0);
    return vehicle_camera_file::SeatBits(drivesIt, aimsIt);
}

// WHERE THE GAME'S OWN THIRD-PERSON CAMERA STANDS FROM ITS PIVOT, from its following camera's command: `forward` the
// way it looks (unit), `offset` [forward, right, up] along the look's heading, `distance` back along the look. Out:
// world units. False when the look has no heading (straight up or down) or a value is not finite.
inline bool GameCameraFromPivot(const float forward[3], const float offset[3], float distance, float out[3]) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(forward[axis]) || !std::isfinite(offset[axis]))
            return false;
    if (!std::isfinite(distance))
        return false;
    const float level = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    if (!(level > 1.0e-4f))
        return false;
    const float heading[2] = {forward[0] / level, forward[1] / level};
    // The offset's second axis is the heading turned a quarter to the right: (h.y, -h.x).
    out[0] = offset[0] * heading[0] + offset[1] * heading[1] - forward[0] * distance;
    out[1] = offset[0] * heading[1] - offset[1] * heading[0] - forward[1] * distance;
    out[2] = offset[2] - forward[2] * distance;
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

// Where the game's camera stands looking level, as an elevation above level from its pivot in whole degrees: the
// elevation it stood at plus the look it had then (the boom is an orbit of the pivot in CE, as in Reach: its camera
// track is not read). `rise` its height above the pivot, `distance` how far it stands, `lookDeg` its look (up is
// positive). Unset (vehicle_boom::kBoomPitchUnset) when the two do not make an angle.
inline float LevelElevationDeg(float rise, float distance, float lookDeg) noexcept
{
    if (!std::isfinite(rise) || !std::isfinite(distance) || !std::isfinite(lookDeg) || !(distance > 1.0e-3f) ||
        std::fabs(rise) > distance)
        return vehicle_boom::kBoomPitchUnset;
    const float degrees = std::asin(rise / distance) / vehicle_boom::kRadiansPerDegree + lookDeg;
    return std::isfinite(degrees) ? std::round(degrees) : vehicle_boom::kBoomPitchUnset;
}

// THE BOOM'S RANGE in CE, as look pitches for vehicle_boom's orbit (the elevation is minus the pitch): held between
// 10 deg below and 60 deg above level, as Reach's orbit is -- over the whole flat look range an orbit stands the
// camera straight above the vehicle or under the ground.
inline constexpr float kBoomLowestDeg = -10.0f;
inline constexpr float kBoomHighestDeg = 60.0f;
inline vehicle_boom::Range BoomRange() noexcept
{
    return vehicle_boom::Range{-kBoomHighestDeg * vehicle_boom::kRadiansPerDegree,
                               -kBoomLowestDeg * vehicle_boom::kRadiansPerDegree};
}

// A director seat state no game writes: the selector reports it for a seat the mod shows from a chase camera, so the
// director, which re-chooses its camera only when the reported state changes, takes the game's following camera when
// a chase camera is chosen and the first-person one again when it is not.
inline constexpr int16_t kChaseSeatState = 0x0102;
} // namespace haloce_vehicle_camera
