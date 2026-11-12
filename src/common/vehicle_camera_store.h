#pragma once
// THE VEHICLE CAMERA FILE ON DISK (vehicle_camera_file.h is its format): the player's own file in the mod's
// folder, written once from the built-in table and from then on read -- and ADDED TO, never rewritten.
//
//   No file      -> the built-in table is written out as one, so there is something to edit.
//   New vehicle  -> Add() puts entries for a vehicle the file has none of its own for into the text
//                   (vehicle_camera_autoadd.h); everything already in the file stays exactly as it is, and the
//                   file as it was before the first addition is kept beside it (kBackupSuffix).
//   File changed -> read again on the next Poll, so an edit takes effect while the game runs.
//   File wrong   -> the table in use stays as it was, and the reason is given once with its line and column.
//
// Poll does file I/O and allocates: it is for a slow, regular caller away from the camera hooks (every second
// or two is plenty). The table it holds is for that same thread.

#include <string>
#include <vector>

#include "vehicle_camera_autoadd.h"
#include "vehicle_camera_file.h"

namespace vehicle_camera_store
{
inline constexpr wchar_t kFileName[] = L"halomccvr_vehicle_cameras.json";
inline constexpr char kFileNameText[] = "halomccvr_vehicle_cameras.json";
// The file as the player had it before the mod first added to it: written once, never replaced.
inline constexpr wchar_t kBackupSuffix[] = L".before-autoadd";
inline constexpr char kBackupSuffixText[] = ".before-autoadd";

struct PollResult
{
    bool changed = false;           // Current() was read afresh by this call
    bool created = false;           // there was no file, and this call wrote the built-in table as one
    std::vector<std::string> notes; // lines for the log; empty when nothing happened
};

class Store
{
public:
    // `path`: the file itself, normally kFileName in the mod's folder.
    explicit Store(std::wstring path);

    PollResult Poll();

    // Adds `addition`'s entries to the file on disk for this game, vehicle and seat (vehicle_camera_autoadd::
    // Insert): the text is read, the entries spliced in, the result checked, and the file replaced in one step.
    // The table in use is read again on the next Poll. False, with the reason, when nothing was written.
    bool Add(const vehicle_camera_autoadd::Addition& addition, const char* gameKey, const std::string& vehicleName,
             uint8_t seat, std::string& why);

    const vehicle_camera_file::Table& Current() const noexcept { return table_; }
    // False while the table in use is the built-in one because no file could be read.
    bool FromFile() const noexcept { return fromFile_; }

private:
    struct Stamp
    {
        unsigned long long written = 0;
        unsigned long long size = 0;
        bool operator==(const Stamp&) const = default;
    };
    enum class Look { Found, Missing, Unreadable };

    Look Stat(Stamp& out) const;
    bool Create(std::string& why) const;
    bool ReadAll(std::string& out, std::string& why) const;

    std::wstring path_;
    vehicle_camera_file::Table table_ = vehicle_camera_file::DefaultTable();
    bool fromFile_ = false;
    bool haveSeen_ = false;
    Stamp seen_{};
    bool createFailureNoted_ = false;
    bool readFailureNoted_ = false;
};
} // namespace vehicle_camera_store
