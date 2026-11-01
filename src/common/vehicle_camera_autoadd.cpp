#include "vehicle_camera_autoadd.h"

#include <cctype>

#include "json_lite.h"

namespace vehicle_camera_autoadd
{
namespace
{
using vehicle_camera_file::Game;
using vehicle_camera_file::Table;
using vehicle_camera_file::Vehicle;

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool HasKey(const Game& game, const std::string& key)
{
    const std::string wanted = Lower(key);
    for (const Vehicle& vehicle : game.vehicles)
        if (Lower(vehicle.name) == wanted)
            return true;
    return false;
}

bool SharesMatch(const Vehicle& a, const Vehicle& b)
{
    for (const std::string& x : a.match)
        for (const std::string& y : b.match)
            if (!x.empty() && x == y)
                return true;
    return false;
}

// The byte just past the '}' closing the object that opens at `open`, in strict JSON (json_lite has read it), or
// 0 when the text ends first.
std::size_t ObjectEnd(const std::string& text, std::size_t open)
{
    int depth = 0;
    bool inString = false;
    for (std::size_t i = open; i < text.size(); ++i)
    {
        const char c = text[i];
        if (inString)
        {
            if (c == '\\')
                ++i;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        else if (c == '{' || c == '[')
            ++depth;
        else if ((c == '}' || c == ']') && --depth == 0)
            return i + 1;
    }
    return 0;
}
} // namespace

Addition For(const Table& file, const Table& builtIn, const char* gameKey, const std::string& vehicleName,
             uint8_t seat)
{
    Addition addition;
    const Game* game = file.FindGame(gameKey);
    if (!game || vehicleName.empty())
        return addition;
    const int matched = vehicle_camera_file::MatchVehicle(*game, vehicleName, seat);
    if (matched >= 0 && !game->vehicles[static_cast<std::size_t>(matched)].isDefault)
        return addition; // it has one of its own
    addition.sectionKey = game->key;

    // The built-in table's entries for this vehicle: the one that would serve this seat, and every other with a
    // match text of its own in common (the vehicle's other seats), unless the file has an entry by that name.
    const Game* builtInGame = builtIn.FindGame(gameKey);
    if (builtInGame)
    {
        const int preset = vehicle_camera_file::MatchVehicle(*builtInGame, vehicleName, seat);
        if (preset >= 0 && !builtInGame->vehicles[static_cast<std::size_t>(preset)].isDefault)
        {
            const Vehicle& serving = builtInGame->vehicles[static_cast<std::size_t>(preset)];
            for (const Vehicle& candidate : builtInGame->vehicles)
                if (!candidate.isDefault && SharesMatch(candidate, serving) && !HasKey(*game, candidate.name))
                    addition.entries.push_back(candidate);
            if (!addition.entries.empty())
            {
                addition.fromBuiltIn = true;
                return addition;
            }
        }
    }

    // One of its own, named and matched by the vehicle's name, with the cameras it was riding on.
    Vehicle own;
    if (matched >= 0)
    {
        own = game->vehicles[static_cast<std::size_t>(matched)];
    }
    else if (const Game* fallbackGame = builtIn.FindGame(gameKey))
    {
        const int fallback = vehicle_camera_file::MatchVehicle(*fallbackGame, std::string(), 0);
        if (fallback < 0)
            return addition;
        own = fallbackGame->vehicles[static_cast<std::size_t>(fallback)];
    }
    else
    {
        return addition;
    }
    own.isDefault = false;
    own.name = vehicleName;
    own.match = {Lower(vehicleName)};
    own.seats = 0;
    if (HasKey(*game, own.name))
    {
        // An entry by that name that does not serve this seat: one for this seat beside it.
        if (seat == 0)
            return addition;
        own.name = vehicleName + " " + Lower(vehicle_camera_file::SeatText(seat));
        own.seats = seat;
        if (HasKey(*game, own.name))
            return addition;
    }
    addition.entries.push_back(own);
    return addition;
}

bool Insert(std::string& text, const Addition& addition, const char* gameKey, const std::string& vehicleName,
            uint8_t seat, std::string& why)
{
    if (addition.entries.empty())
    {
        why = "nothing to add";
        return false;
    }
    json_lite::Value root;
    std::string error;
    if (!json_lite::Parse(text.data(), text.size(), root, error))
    {
        why = "the file does not read as JSON (" + error + ")";
        return false;
    }
    const json_lite::Value* games = root.Get("games");
    const json_lite::Value* section = games ? games->Get(addition.sectionKey.c_str()) : nullptr;
    const json_lite::Value* vehicles = section ? section->Get("vehicles") : nullptr;
    if (!vehicles || !vehicles->IsObject() || vehicles->at >= text.size() || text[vehicles->at] != '{')
    {
        why = "the file has no \"games\" > \"" + addition.sectionKey + "\" > \"vehicles\" object";
        return false;
    }
    const std::size_t end = ObjectEnd(text, vehicles->at);
    if (end == 0)
    {
        why = "the \"vehicles\" object does not close";
        return false;
    }
    const std::size_t close = end - 1;
    // Where the last member ends (or the '{' when there is none), and the closing brace's own indentation.
    std::size_t last = close;
    while (last > vehicles->at + 1 && std::isspace(static_cast<unsigned char>(text[last - 1])))
        --last;
    std::size_t lineStart = close;
    while (lineStart > last && text[lineStart - 1] != '\n')
        --lineStart;
    std::string indent;
    for (std::size_t i = lineStart; i < close; ++i)
        if (text[i] == ' ' || text[i] == '\t')
            indent += text[i];

    std::string added = vehicles->keys.empty() ? "\n" : ",\n";
    for (std::size_t i = 0; i < addition.entries.size(); ++i)
    {
        std::string entry = vehicle_camera_file::VehicleJson(addition.entries[i]);
        if (i + 1 < addition.entries.size())
            entry.insert(entry.size() - 1, ","); // before the entry's own last newline
        added += entry;
    }
    added += indent;
    if (text.find("\r\n") != std::string::npos)
    {
        std::string crlf;
        for (const char c : added)
        {
            if (c == '\n')
                crlf += '\r';
            crlf += c;
        }
        added = crlf;
    }

    std::string result = text.substr(0, last) + added + text.substr(close);
    Table check;
    const vehicle_camera_file::ParseResult parsed = vehicle_camera_file::FromJson(result.data(), result.size(), check);
    if (!parsed.ok)
    {
        why = "the file would not read back (" + parsed.error + ")";
        return false;
    }
    const Game* game = check.FindGame(gameKey);
    const int matched = game ? vehicle_camera_file::MatchVehicle(*game, vehicleName, seat) : -1;
    if (matched < 0 || game->vehicles[static_cast<std::size_t>(matched)].isDefault)
    {
        why = "the added entries would not be the ones this seat reads";
        return false;
    }
    text = std::move(result);
    return true;
}
} // namespace vehicle_camera_autoadd
