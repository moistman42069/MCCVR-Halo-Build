#pragma once
// A VEHICLE OF ITS OWN IN THE PLAYER'S CAMERA FILE (vehicle_camera_file.h is the format). The first time the
// player sits in a vehicle the file meets only with its "default" entry, entries for that vehicle are added to
// the file, so every vehicle the player has ridden -- a mod map's own included -- is there to edit, without
// anyone tracking them by hand:
//   * the built-in table's entries for it, where the built-in table has some the file does not (a file written
//     before those presets existed: its Elephant rides on "default" until then);
//   * else one entry named and matched by the vehicle's own name, with the cameras of the "default" entry it was
//     riding on -- so nothing about the view changes when it is added; only the file now has a place for it.
// The entries go at the end of the section the reader took the vehicle from, written in the file's own layout;
// the rest of the text is left exactly as it was (the player's own notes and order included).
//
// Pure: no engine and no file I/O. vehicle_camera_store does the reading and writing.

#include <cstdint>
#include <string>
#include <vector>

#include "vehicle_camera_file.h"

namespace vehicle_camera_autoadd
{
struct Addition
{
    std::string sectionKey;                        // the file's section the entries go into ("halo3", "default")
    std::vector<vehicle_camera_file::Vehicle> entries; // empty: nothing to add
    bool fromBuiltIn = false;                      // the built-in table's own entries for the vehicle
};

// What to add for this vehicle and seat (`seat` is SeatBits; 0 = not known). Nothing when the file already has
// an entry of its own for it, when it has no section the game could use, or when the vehicle has no name.
Addition For(const vehicle_camera_file::Table& file, const vehicle_camera_file::Table& builtIn, const char* gameKey,
             const std::string& vehicleName, uint8_t seat);

// `text` (the file as it is on disk) with `addition`'s entries added at the end of its section's "vehicles",
// in the text's own line endings. The result is read back: it must parse, and must now give the vehicle and seat
// an entry of its own. False -- `text` untouched and the reason in `why` -- when the text does not parse, has no
// such section, or the result would not read back so.
bool Insert(std::string& text, const Addition& addition, const char* gameKey, const std::string& vehicleName,
            uint8_t seat, std::string& why);
} // namespace vehicle_camera_autoadd
