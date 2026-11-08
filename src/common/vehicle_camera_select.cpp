#include "vehicle_camera_select.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace vehicle_camera_select
{
namespace
{
using vehicle_camera_file::Camera;
using vehicle_camera_file::CameraType;
using vehicle_camera_file::Origin;
using vehicle_camera_file::Tether;
using vehicle_camera_file::Vehicle;

bool SameName(const std::string& a, const std::string& b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

int Wrap(int index, int count) noexcept
{
    return count > 0 ? ((index % count) + count) % count : 0;
}

// The camera an earlier choice points at in `entry` as it is now: the one still at that position under that
// name, else the first of that name, else whatever is at that position, else -1. Position is tried first so
// that two cameras of one name are not confused, and last so that a renamed camera keeps its player.
int FindCamera(const Vehicle& entry, const std::string& name, int index) noexcept
{
    const int count = static_cast<int>(entry.cameras.size());
    const bool inRange = index >= 0 && index < count;
    if (inRange && SameName(entry.cameras[static_cast<std::size_t>(index)].name, name))
        return index;
    if (!name.empty())
        for (int i = 0; i < count; ++i)
            if (SameName(entry.cameras[static_cast<std::size_t>(i)].name, name))
                return i;
    return inRange ? index : -1;
}

int FindMode(const Camera& camera, const std::string& name, int index) noexcept
{
    const int count = camera.ModeCount();
    const bool inRange = index >= 0 && index < count;
    if (camera.tethering.empty())
        return inRange ? index : -1;
    if (inRange && SameName(camera.tethering[static_cast<std::size_t>(index)].name, name))
        return index;
    if (!name.empty())
        for (int i = 0; i < count; ++i)
            if (SameName(camera.tethering[static_cast<std::size_t>(i)].name, name))
                return i;
    return inRange ? index : -1;
}

std::string Number(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    return text;
}

// The memory file's words for vehicle_control's modes, in its order (0 = as before, 1 motion, 2 stick, 3 wheel).
constexpr const char* kControlWords[] = {"asbefore", "motion", "stick", "wheel"};
constexpr int kControlCount = static_cast<int>(sizeof(kControlWords) / sizeof(kControlWords[0]));

const char* ControlWord(int control) noexcept
{
    return control >= 0 && control < kControlCount ? kControlWords[control] : "-";
}

int ControlFromWord(const std::string& word) noexcept
{
    for (int i = 0; i < kControlCount; ++i)
        if (word == kControlWords[i])
            return i;
    return -1;
}

// A name is kept on one line between tabs: a tab, a line end or a control character in it becomes a space.
std::string FieldText(const std::string& value)
{
    std::string out = value;
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20)
            c = ' ';
    return out;
}
} // namespace

const Vehicle* Selector::Entry() const noexcept
{
    if (!game_ || entry_ < 0 || entry_ >= static_cast<int>(game_->vehicles.size()))
        return nullptr;
    return &game_->vehicles[static_cast<std::size_t>(entry_)];
}

std::string Selector::MemoryKey() const
{
    const std::string seat = vehicle_camera_file::SeatText(seat_);
    return FieldText(gameKey_) + '\t' + FieldText(vehicleName_) + '\t' + (seat.empty() ? std::string("any seat") : seat);
}

std::string Selector::BoomKey() const
{
    return MemoryKey() + '\t' + FieldText(entryName_) + '\t' +
           (cameraName_.empty() ? "camera " + std::to_string(active_.camera + 1) : FieldText(cameraName_));
}

bool Selector::BoomPitch(float& degrees) const
{
    if (!seated_ || !active_.valid || !active_.chase)
        return false;
    const auto found = boom_.find(BoomKey());
    if (found == boom_.end())
        return false;
    degrees = found->second;
    return true;
}

void Selector::SetBoomPitch(float degrees, bool isDefault)
{
    if (!seated_ || !active_.valid || !active_.chase)
        return;
    const std::string key = BoomKey();
    const auto found = boom_.find(key);
    if (isDefault || !vehicle_camera_file::BoomPitchSet(degrees))
    {
        if (found != boom_.end())
        {
            boom_.erase(found);
            memoryDirty_ = true;
        }
        return;
    }
    // A twentieth of a degree is no change: the camera thread's angle is taken every poll, and a still boom must not
    // keep the file dirty.
    if (found != boom_.end() && std::fabs(found->second - degrees) < 0.05f)
        return;
    boom_[key] = degrees;
    memoryDirty_ = true;
}

void Selector::Clear(std::string reason)
{
    const uint32_t changes = active_.changes + (active_.valid ? 1u : 0u);
    active_ = ActiveCamera{};
    active_.changes = changes;
    entry_ = -1;
    entryName_.clear();
    rideGame_.clear();
    rideVehicle_.clear();
    rideSeat_ = 0;
    cameraName_.clear();
    modeName_.clear();
    haveChoice_ = false;
    standDown_ = std::move(reason);
}

void Selector::Select(int camera, int mode, bool countsAsChange)
{
    const Vehicle* entry = Entry();
    if (!entry || entry->cameras.empty())
        return;
    const int count = static_cast<int>(entry->cameras.size());
    if (camera < 0 || camera >= count)
        camera = 0;
    const Camera& chosen = entry->cameras[static_cast<std::size_t>(camera)];
    const int modes = chosen.ModeCount();
    mode = Wrap(mode, modes);
    const Tether tether = chosen.Mode(mode);

    ActiveCamera next{};
    next.valid = true;
    next.chase = chosen.type == CameraType::Chase;
    const Origin origin = chosen.OriginOf(tether);
    next.origin = static_cast<uint8_t>(origin);
    if (origin == Origin::Marker)
    {
        const std::string& marker = chosen.MarkerOf(tether);
        const std::size_t room = static_cast<std::size_t>(kMarkerCapacity - 1);
        std::memcpy(next.marker, marker.data(), marker.size() < room ? marker.size() : room);
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        next.offsetCm[axis] = tether.offset[axis];
        next.leashMinCm[axis] = tether.leashMin[axis];
        next.leashMaxCm[axis] = tether.leashMax[axis];
    }
    next.locYaw = tether.locYaw;
    next.locPitch = tether.locPitch;
    next.locRoll = tether.locRoll;
    next.locView = tether.locView;
    next.rotYaw = tether.rotYaw;
    next.rotPitch = tether.rotPitch;
    next.rotRoll = tether.rotRoll;
    next.rotationSet = chosen.rotationSet;
    next.collide = chosen.collide;
    next.collideMarginCm = chosen.collideMargin;
    next.lagSeconds = vehicle_camera_file::LagSecondsOf(chosen);
    next.lagDamping = vehicle_camera_file::LagDampingOf(chosen);
    next.lagLimitCm = vehicle_camera_file::LagLimitOf(chosen);
    next.boomPitchDeg = vehicle_camera_file::BoomPitchSet(chosen.boomPitch) ? chosen.boomPitch
                                                                           : vehicle_camera_file::kBoomPitchUnset;
    next.aimMarker = vehicle_camera_file::EffectiveAimMarker(*entry, camera, mode);
    next.showHeadlessBody = vehicle_camera_file::EffectiveShowHeadlessBody(chosen, seat_);
    next.holdExposure = !chosen.eyeAdjust;
    {
        std::string joined;
        for (const std::string& name : chosen.hideRegions)
            joined += (joined.empty() ? "" : ",") + name;
        const size_t room = sizeof(next.hideRegions) - 1;
        std::memcpy(next.hideRegions, joined.data(), joined.size() < room ? joined.size() : room);
        joined.clear();
        for (const std::string& name : chosen.hideParts)
            joined += (joined.empty() ? "" : ",") + name;
        const size_t partsRoom = sizeof(next.hideParts) - 1;
        std::memcpy(next.hideParts, joined.data(), joined.size() < partsRoom ? joined.size() : partsRoom);
        joined.clear();
        int fades = 0;
        for (const auto& fade : chosen.fadeParts)
        {
            const std::string piece = (joined.empty() ? "" : ",") + fade.first;
            if (fades == vehicle_camera_file::kMaxFadeParts || joined.size() + piece.size() >= sizeof(next.fadeParts))
                break;
            joined += piece;
            next.fadeKept[fades++] = fade.second;
        }
        std::memcpy(next.fadeParts, joined.data(), joined.size());
    }
    next.camera = camera;
    next.cameraCount = count;
    next.mode = mode;
    next.modeCount = modes;
    next.changes = active_.changes + (countsAsChange ? 1u : 0u);

    active_ = next;
    entryName_ = entry->name;
    cameraName_ = chosen.name;
    modeName_ = tether.name;
    if (modes > 1)
    {
        choice_ = tether;
        haveChoice_ = true;
    }
    standDown_.clear();
}

void Selector::Remember()
{
    const Vehicle* entry = Entry();
    if (!entry || !active_.valid)
        return;
    Remembered& kept = memory_[MemoryKey()];
    if (kept.entry == entry->name && kept.camera == active_.camera && kept.cameraName == cameraName_ &&
        kept.mode == active_.mode && kept.modeName == modeName_)
        return;
    kept.entry = entry->name;
    kept.camera = active_.camera;
    kept.cameraName = cameraName_;
    kept.mode = active_.mode;
    kept.modeName = modeName_;
    memoryDirty_ = true;
}

int Selector::ControlChoice(int base)
{
    if (!seated_)
        return -1;
    const auto found = memory_.find(MemoryKey());
    if (found == memory_.end() || found->second.control < 0)
        return -1;
    if (found->second.controlBase != base)
    {
        found->second.control = -1;
        found->second.controlBase = -1;
        memoryDirty_ = true;
        return -1;
    }
    return found->second.control;
}

void Selector::SetControlChoice(int choice, int base)
{
    if (!seated_)
        return;
    Remembered& kept = memory_[MemoryKey()];
    const int control = choice == base ? -1 : choice;
    const int controlBase = control < 0 ? -1 : base;
    if (kept.control == control && kept.controlBase == controlBase)
        return;
    kept.control = control;
    kept.controlBase = controlBase;
    memoryDirty_ = true;
}

std::string Selector::MemoryText() const
{
    std::string out =
        "# Your last camera, tethering mode and controls in each vehicle seat -- written by the mod, read when it\r\n"
        "# starts. Delete this file to start every seat in its default camera, mode and controls again.\r\n"
        "# game\tvehicle\tseat\tentry\tcamera number\tcamera name\tmode number\tmode name\tcontrols you chose\t"
        "the default you chose them against\r\n"
        "# boom\tgame\tvehicle\tseat\tentry\tcamera\tyour third-person boom, degrees above level\r\n";
    for (const auto& [key, kept] : memory_)
    {
        if (kept.camera < 0 && kept.control < 0)
            continue;
        out += key + '\t' + FieldText(kept.entry) + '\t' + std::to_string(kept.camera + 1) + '\t' +
               FieldText(kept.cameraName) + '\t' + std::to_string(kept.mode + 1) + '\t' + FieldText(kept.modeName) +
               '\t' + ControlWord(kept.control) + '\t' + ControlWord(kept.controlBase) + "\r\n";
    }
    for (const auto& [key, degrees] : boom_)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(degrees));
        out += "boom\t" + key + '\t' + text + "\r\n";
    }
    return out;
}

int Selector::LoadMemory(const std::string& text)
{
    int read = 0;
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields;
        std::size_t from = 0;
        for (;;)
        {
            const std::size_t tab = line.find('\t', from);
            fields.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
            if (tab == std::string::npos)
                break;
            from = tab + 1;
        }
        if (fields.size() == 7 && fields[0] == "boom")
        {
            // boom, game, vehicle, seat, entry, camera, degrees.
            char* stop = nullptr;
            const double degrees = std::strtod(fields[6].c_str(), &stop);
            if (stop != fields[6].c_str() && !fields[1].empty() && !fields[2].empty() && !fields[3].empty() &&
                vehicle_camera_file::BoomPitchSet(static_cast<float>(degrees)))
                boom_[fields[1] + '\t' + fields[2] + '\t' + fields[3] + '\t' + fields[4] + '\t' + fields[5]] =
                    static_cast<float>(degrees);
            continue;
        }
        if (fields.size() != 10 || fields[0].empty() || fields[1].empty() || fields[2].empty())
            continue;
        Remembered kept;
        kept.entry = fields[3];
        const int camera = std::atoi(fields[4].c_str());
        kept.camera = camera > 0 ? camera - 1 : -1;
        kept.cameraName = fields[5];
        const int mode = std::atoi(fields[6].c_str());
        kept.mode = mode > 0 ? mode - 1 : -1;
        kept.modeName = fields[7];
        kept.control = ControlFromWord(fields[8]);
        kept.controlBase = ControlFromWord(fields[9]);
        if (kept.control < 0 || kept.controlBase < 0)
            kept.control = kept.controlBase = -1;
        if (kept.camera < 0 && kept.control < 0)
            continue;
        memory_[fields[0] + '\t' + fields[1] + '\t' + fields[2]] = kept;
        ++read;
    }
    return read;
}

bool Selector::Match()
{
    const ActiveCamera before = active_;
    const std::string previousEntry = entryName_;
    const std::string previousCamera = cameraName_;
    const std::string previousMode = modeName_;

    game_ = table_.FindGame(gameKey_.c_str());
    entry_ = game_ ? vehicle_camera_file::MatchVehicle(*game_, vehicleName_, seat_) : -1;
    const Vehicle* entry = Entry();
    if (!entry)
    {
        Clear(game_ ? "the camera file has no entry for this vehicle and no \"default\" entry"
                    : "the camera file has no section for this game and no \"default\" section");
        return !(before == active_);
    }
    if (!entry->enabled)
    {
        Clear("the camera file's entry \"" + entry->name + "\" is set \"enabled\": false");
        return !(before == active_);
    }
    if (entry->cameras.empty())
    {
        Clear("the camera file's entry \"" + entry->name + "\" lists no cameras");
        return !(before == active_);
    }

    // The same entry -- or "default" giving way to an entry since added for this vehicle (vehicle_camera_autoadd),
    // which starts with the default entry's cameras: the player stays where he is.
    const bool sameEntry = SameName(previousEntry, entry->name) ||
        (!entry->isDefault && SameName(previousEntry, vehicle_camera_file::kDefaultKey));
    const bool sameRide = before.valid && sameEntry && rideGame_ == gameKey_ && rideVehicle_ == vehicleName_ &&
                          (rideSeat_ == seat_ || rideSeat_ == 0);
    rideGame_ = gameKey_;
    rideVehicle_ = vehicleName_;
    rideSeat_ = seat_;
    if (sameRide)
    {
        // The same seat as before (the file was reloaded, or the game has only now named the seat): the
        // player stays in the camera and mode he is in.
        const int camera = FindCamera(*entry, previousCamera, before.camera);
        if (camera >= 0)
        {
            const Camera& kept = entry->cameras[static_cast<std::size_t>(camera)];
            const int mode = FindMode(kept, previousMode, before.mode);
            const bool otherKind = (kept.type == CameraType::Chase) != before.chase;
            if (mode >= 0)
            {
                Select(camera, mode, otherKind);
            }
            else
            {
                // His mode is gone from the file: the nearest to it the camera still has.
                Tether was;
                was.name = previousMode;
                was.rotYaw = before.rotYaw;
                was.rotPitch = before.rotPitch;
                was.rotRoll = before.rotRoll;
                Select(camera, vehicle_camera_file::CarryMode(kept, was), true);
            }
            return !(before == active_);
        }
    }

    // A seat the player was not in a moment ago: his last choice there, else the entry's defaults -- where the
    // default is the seat view and the game has none to give this seat, its first chase camera.
    int camera = entry->defaultCamera;
    int mode = entry->defaultMode;
    if (!seatView_ && camera >= 0 && camera < static_cast<int>(entry->cameras.size()) &&
        entry->cameras[static_cast<std::size_t>(camera)].type == CameraType::FirstPerson)
    {
        for (std::size_t i = 0; i < entry->cameras.size(); ++i)
            if (entry->cameras[i].type == CameraType::Chase)
            {
                camera = static_cast<int>(i);
                mode = 0;
                break;
            }
    }
    // The seat's last choice where it was made in this entry -- or in "default", for an entry since added for the
    // vehicle (vehicle_camera_autoadd: it starts with the default entry's cameras, so the choice still names one).
    const auto found = memory_.find(MemoryKey());
    const bool rememberedHere = found != memory_.end() &&
        (SameName(found->second.entry, entry->name) ||
         (!entry->isDefault && SameName(found->second.entry, vehicle_camera_file::kDefaultKey)));
    if (rememberedHere && found->second.camera >= 0)
    {
        const int remembered = FindCamera(*entry, found->second.cameraName, found->second.camera);
        if (remembered >= 0)
        {
            camera = remembered;
            const int rememberedMode =
                FindMode(entry->cameras[static_cast<std::size_t>(remembered)], found->second.modeName, found->second.mode);
            mode = rememberedMode >= 0 ? rememberedMode : 0;
        }
    }
    Select(camera, mode, true);
    return !(before == active_);
}

void Selector::SetTable(vehicle_camera_file::Table table)
{
    table_ = std::move(table);
    game_ = nullptr;
    if (seated_)
        Match();
}

bool Selector::Update(const char* gameKey, bool seated, const char* vehicleName, uint8_t seat, bool seatView)
{
    if (!seated)
    {
        if (!seated_)
            return false;
        seated_ = false;
        const bool was = active_.valid;
        Clear(kOnFoot);
        return was;
    }
    const char* key = gameKey ? gameKey : "";
    const char* name = vehicleName ? vehicleName : "";
    if (seated_ && seat_ == seat && seatView_ == seatView && gameKey_ == key && vehicleName_ == name)
        return false;
    seated_ = true;
    seat_ = seat;
    seatView_ = seatView;
    gameKey_ = key;
    vehicleName_ = name;
    return Match();
}

bool Selector::StepCamera(int direction)
{
    const Vehicle* entry = Entry();
    if (!entry || !active_.valid || active_.cameraCount < 2 || direction == 0)
        return false;
    const int count = static_cast<int>(entry->cameras.size());
    const int to = Wrap(active_.camera + (direction > 0 ? 1 : -1), count);
    if (to == active_.camera)
        return false;
    const Tether from = active_.modeCount > 1 || !haveChoice_
                            ? entry->cameras[static_cast<std::size_t>(active_.camera)].Mode(active_.mode)
                            : choice_;
    Select(to, vehicle_camera_file::CarryMode(entry->cameras[static_cast<std::size_t>(to)], from), true);
    Remember();
    return true;
}

bool Selector::StepMode(int direction)
{
    const Vehicle* entry = Entry();
    if (!entry || !active_.valid || active_.modeCount < 2 || direction == 0)
        return false;
    Select(active_.camera, Wrap(active_.mode + (direction > 0 ? 1 : -1), active_.modeCount), true);
    Remember();
    return true;
}

std::wstring Selector::Note() const
{
    const Vehicle* entry = Entry();
    if (!entry || !active_.valid)
        return {};
    const auto wide = [](const std::string& text) {
        std::wstring out;
        out.reserve(text.size());
        for (const char c : text)
            out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        return out;
    };
    std::wstring line = wide(entry->isDefault && !vehicleName_.empty() ? vehicleName_ : entry->name);
    if (!line.empty())
        line[0] = static_cast<wchar_t>(std::towupper(line[0]));
    line += L" \u00B7 ";
    line += cameraName_.empty() ? L"Camera " + std::to_wstring(active_.camera + 1) : wide(cameraName_);
    if (active_.modeCount > 1)
        line += L", " + (modeName_.empty() ? L"Mode " + std::to_wstring(active_.mode + 1) : wide(modeName_));
    return line;
}

std::string Selector::TuningKey(const char* gameKey) const
{
    const Vehicle* entry = Entry();
    if (!entry || !active_.valid || !gameKey)
        return {};
    return std::string(gameKey) + "\t" + entry->name + "\t" +
           (cameraName_.empty() ? "camera " + std::to_string(active_.camera + 1) : cameraName_);
}

std::string Selector::Describe() const
{
    const Vehicle* entry = Entry();
    if (!entry || !active_.valid)
        return {};
    std::string line = entry->name;
    const std::string seat = vehicle_camera_file::SeatText(seat_);
    if (!seat.empty())
        line += " (" + seat + ")";
    line += ": camera " + std::to_string(active_.camera + 1) + " of " + std::to_string(active_.cameraCount);
    if (!cameraName_.empty())
        line += " \"" + cameraName_ + "\"";
    if (!active_.chase)
    {
        line += ", the seat view";
        if (active_.offsetCm[0] != 0.0f || active_.offsetCm[1] != 0.0f || active_.offsetCm[2] != 0.0f)
            line += ", its eye moved " + Number(active_.offsetCm[0]) + " " + Number(active_.offsetCm[1]) + " " +
                    Number(active_.offsetCm[2]) + " cm (forward, right, up)";
        if (active_.showHeadlessBody)
            line += ", your body shown without its head";
        if (active_.hideRegions[0])
            line += std::string(", hiding ") + active_.hideRegions;
        if (active_.hideParts[0])
            line += std::string(", hiding parts ") + active_.hideParts;
        if (active_.fadeParts[0])
            line += std::string(", fading ") + active_.fadeParts;
        if (active_.holdExposure)
            line += ", exposure held";
        return line;
    }
    if (active_.modeCount > 1)
    {
        line += ", mode " + std::to_string(active_.mode + 1) + " of " + std::to_string(active_.modeCount);
        if (!modeName_.empty())
            line += " \"" + modeName_ + "\"";
    }
    Tether mode;
    mode.locYaw = active_.locYaw;
    mode.locPitch = active_.locPitch;
    mode.locRoll = active_.locRoll;
    mode.locView = active_.locView;
    mode.rotYaw = active_.rotYaw;
    mode.rotPitch = active_.rotPitch;
    mode.rotRoll = active_.rotRoll;
    line += ", chase from " + vehicle_camera_file::OriginText(static_cast<Origin>(active_.origin), active_.marker);
    line += ", offset " + Number(active_.offsetCm[0]) + " " + Number(active_.offsetCm[1]) + " " +
            Number(active_.offsetCm[2]) + " cm";
    line += ", location " + vehicle_camera_file::LocationTrackingText(mode);
    line += ", rotation " + vehicle_camera_file::RotationTrackingText(mode);
    line += ", leash " + vehicle_camera_file::LeashText(active_.leashMinCm, active_.leashMaxCm);
    if (active_.lagSeconds > 0.0f)
        line += ", lag " + Number(active_.lagSeconds) + " s" +
                (active_.lagLimitCm > 0.0f ? " up to " + Number(active_.lagLimitCm) + " cm" : std::string());
    if (vehicle_camera_file::BoomPitchSet(active_.boomPitchDeg))
        line += ", boom from " + Number(active_.boomPitchDeg) + " deg";
    if (active_.aimMarker)
        line += ", aim ring";
    if (active_.hideRegions[0])
        line += std::string(", hiding ") + active_.hideRegions;
    if (active_.hideParts[0])
        line += std::string(", hiding parts ") + active_.hideParts;
    if (active_.fadeParts[0])
        line += std::string(", fading ") + active_.fadeParts;
    if (active_.holdExposure)
        line += ", exposure held";
    return line;
}
} // namespace vehicle_camera_select
