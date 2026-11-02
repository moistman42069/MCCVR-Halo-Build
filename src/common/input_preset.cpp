#include "input_preset.h"
#include "log.h"
#include "weapon_interaction_logic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace input_preset
{
namespace
{
struct BoolField { const char* key; bool Config::*member; };
struct IntField { const char* key; int Config::*member; };
struct FloatField { const char* key; float Config::*member; };

constexpr BoolField kBools[] = {
    {"vr_action_mapping", &Config::vr_action_mapping},
    {"trigger_air_equipment", &Config::trigger_air_equipment},
    {"disable_flashlight_input", &Config::disable_flashlight_input},
    {"flashlight_suppress_on_two_hand", &Config::flashlight_suppress_on_two_hand},
    {"y_b_start_chord", &Config::y_b_start_chord},
    {"turn_smooth", &Config::turn_smooth},
    {"vehicle_smooth_turn", &Config::vehicle_smooth_turn},
    {"dpad_keep_moving", &Config::dpad_keep_moving},
    {"quest_thumbrest_dpad", &Config::quest_thumbrest_dpad},
    {"reclaimer_talk_gesture", &Config::reclaimer_talk_gesture},
    {"reclaimer_scoreboard_gesture", &Config::reclaimer_scoreboard_gesture},
    {"reclaimer_vote_gesture", &Config::reclaimer_vote_gesture},
    {"physical_running", &Config::physical_running},
    {"physical_crouch", &Config::physical_crouch},
    {"right_stick_crouch", &Config::right_stick_crouch},
    {"physical_melee", &Config::physical_melee},
    {"gesture_melee", &Config::gesture_melee},
    {"gesture_melee_require_grip", &Config::gesture_melee_require_grip},
    {"gesture_melee_off_hand", &Config::gesture_melee_off_hand},
    {"gesture_melee_directional", &Config::gesture_melee_directional},
    {"two_hand_toggle", &Config::two_hand_toggle},
    {"manual_reload", &Config::manual_reload},
    {"weapon_holsters", &Config::weapon_holsters},
    {"weapon_holster_slide", &Config::weapon_holster_slide},
    {"weapon_holster_click", &Config::weapon_holster_click},
    {"gesture_only_weapon_input", &Config::gesture_only_weapon_input},
    {"dual_wield_shake_reload", &Config::dual_wield_shake_reload},
    {"weapon_needler_shake", &Config::weapon_needler_shake},
};
constexpr IntField kInts[] = {
    {"dpad_hand", &Config::dpad_hand},
    {"weapon_holster_location", &Config::weapon_holster_location},
};
constexpr FloatField kFloats[] = {
    {"gesture_melee_speed", &Config::gesture_melee_speed},
    {"gesture_melee_extension", &Config::gesture_melee_extension},
    {"gesture_melee_travel", &Config::gesture_melee_travel},
    {"gesture_melee_reach", &Config::gesture_melee_reach},
    {"turn_snap_deg", &Config::turn_snap_deg},
    {"turn_smooth_deg_s", &Config::turn_smooth_deg_s},
    {"dpad_head_radius", &Config::dpad_head_radius},
    {"physical_running_speed", &Config::physical_running_speed},
    {"physical_running_sensitivity", &Config::physical_running_sensitivity},
};

std::filesystem::path Folder()
{
    return std::filesystem::path(Config_Path()).parent_path() / L"Input Presets";
}
std::filesystem::path FileFor(const std::string& name)
{
    return Folder() / std::filesystem::path(name + ".cfg");
}
}

void CopyScheme(Config& to, const Config& from) noexcept
{
    for (const auto& f : kBools) to.*f.member = from.*f.member;
    for (const auto& f : kInts) to.*f.member = from.*f.member;
    for (const auto& f : kFloats) to.*f.member = from.*f.member;
    for (unsigned title = 0; title < weapon_interaction::kTitleCount; ++title)
        to.vr_bindings[title] = from.vr_bindings[title];
}

bool SameScheme(const Config& a, const Config& b) noexcept
{
    for (const auto& f : kBools) if (a.*f.member != b.*f.member) return false;
    for (const auto& f : kInts) if (a.*f.member != b.*f.member) return false;
    // A file keeps three decimals, so a loaded preset can differ from what was saved by rounding alone.
    for (const auto& f : kFloats) if (std::fabs(a.*f.member - b.*f.member) > 0.002f) return false;
    for (unsigned title = 0; title < weapon_interaction::kTitleCount; ++title)
        if (a.vr_bindings[title] != b.vr_bindings[title]) return false;
    return true;
}

std::string Serialize(const Config& scheme)
{
    std::string text = "# Halo MCC VR input preset\n";
    char line[128];
    for (const auto& f : kBools)
    {
        sprintf_s(line, "%s = %d\n", f.key, scheme.*f.member ? 1 : 0);
        text += line;
    }
    for (const auto& f : kInts)
    {
        sprintf_s(line, "%s = %d\n", f.key, scheme.*f.member);
        text += line;
    }
    for (const auto& f : kFloats)
    {
        sprintf_s(line, "%s = %.3f\n", f.key, scheme.*f.member);
        text += line;
    }
    for (unsigned title = 0; title < weapon_interaction::kTitleCount; ++title)
        for (unsigned action = 0; action < vr_mapping::Count; ++action)
        {
            sprintf_s(line, "vr_bind_%s_%s = %d\n", weapon_interaction::kTitleKeys[title], vr_mapping::kKeys[action],
                      scheme.vr_bindings[title][action]);
            text += line;
        }
    return text;
}

void Parse(const char* text, Config& out)
{
    std::istringstream lines(text ? text : "");
    std::string line;
    while (std::getline(lines, line))
    {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c) != 0; }), key.end());
        const char* value = line.c_str() + eq + 1;
        bool done = false;
        for (const auto& f : kBools)
            if (key == f.key) { out.*f.member = atoi(value) != 0; done = true; break; }
        if (done) continue;
        for (const auto& f : kInts)
            if (key == f.key) { out.*f.member = atoi(value); done = true; break; }
        if (done) continue;
        for (const auto& f : kFloats)
            if (key == f.key)
            {
                const float parsed = static_cast<float>(atof(value));
                if (std::isfinite(parsed)) out.*f.member = parsed;
                done = true;
                break;
            }
        if (done) continue;
        char expected[96];
        for (unsigned title = 0; title < weapon_interaction::kTitleCount && !done; ++title)
            for (unsigned action = 0; action < vr_mapping::Count; ++action)
            {
                sprintf_s(expected, "vr_bind_%s_%s", weapon_interaction::kTitleKeys[title], vr_mapping::kKeys[action]);
                if (key != expected) continue;
                const int source = atoi(value);
                if (source >= 0 && source < vr_mapping::SourceCount)
                    out.vr_bindings[title][action] = source;
                done = true;
                break;
            }
    }
}

int BuiltInCount() noexcept { return 2; }

const char* BuiltInName(int index) noexcept
{
    return index == 0 ? kDefaultName : index == 1 ? "VR action layout" : "";
}

void BuiltIn(int index, Config& out)
{
    // Default is the struct's own values: the game's button layout, with nothing remapped.
    if (index != 1) return;
    // The layout the VR Mappings page used to call Automatic.
    out.vr_action_mapping = true;
    for (unsigned title = 0; title < weapon_interaction::kTitleCount; ++title)
        for (unsigned action = 0; action < vr_mapping::Count; ++action)
            out.vr_bindings[title][action] = vr_mapping::kDefaults[action];
}

std::string SafeName(const char* name)
{
    std::string safe;
    for (const char* c = name ? name : ""; *c && safe.size() < 48; ++c)
        if (std::isalnum(static_cast<unsigned char>(*c)) || *c == ' ' || *c == '-' || *c == '_')
            safe += *c;
    while (!safe.empty() && safe.back() == ' ') safe.pop_back();
    while (!safe.empty() && safe.front() == ' ') safe.erase(safe.begin());
    for (int i = 0; i < BuiltInCount(); ++i)
        if (_stricmp(safe.c_str(), BuiltInName(i)) == 0) return {};
    return safe;
}

std::vector<std::string> ListSaved()
{
    std::vector<std::string> names;
    std::error_code error;
    for (std::filesystem::directory_iterator it(Folder(), error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file(error) || it->path().extension() != L".cfg") continue;
        const std::string name = it->path().stem().string();
        if (SafeName(name.c_str()) == name) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Find(const std::string& name, Config& out)
{
    for (int i = 0; i < BuiltInCount(); ++i)
        if (name == BuiltInName(i))
        {
            BuiltIn(i, out);
            return true;
        }
    if (SafeName(name.c_str()) != name || name.empty()) return false;
    std::ifstream file(FileFor(name));
    if (!file) return false;
    std::stringstream text;
    text << file.rdbuf();
    Parse(text.str().c_str(), out);
    return true;
}

bool Save(const std::string& name, const Config& scheme)
{
    if (SafeName(name.c_str()) != name || name.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(Folder(), error);
    std::ofstream file(FileFor(name), std::ios::trunc);
    if (!file)
    {
        LOG("input preset: could not write '%s'", name.c_str());
        return false;
    }
    file << Serialize(scheme);
    LOG("input preset: saved '%s'", name.c_str());
    return static_cast<bool>(file);
}

bool Delete(const std::string& name)
{
    if (SafeName(name.c_str()) != name || name.empty()) return false;
    std::error_code error;
    return std::filesystem::remove(FileFor(name), error);
}
}
