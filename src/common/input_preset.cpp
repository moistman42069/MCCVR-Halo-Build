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
    {"physical_running", &Config::physical_running},
    {"physical_crouch", &Config::physical_crouch},
    {"right_stick_crouch", &Config::right_stick_crouch},
    {"physical_melee", &Config::physical_melee},
    {"gesture_melee", &Config::gesture_melee},
    {"two_handed_aim", &Config::two_handed_aim},
    {"gesture_melee_require_grip", &Config::gesture_melee_require_grip},
    {"gesture_melee_off_hand", &Config::gesture_melee_off_hand},
    {"gesture_melee_directional", &Config::gesture_melee_directional},
    {"two_hand_toggle", &Config::two_hand_toggle},
    {"two_hand_grip_zone_authority", &Config::two_hand_grip_zone_authority},
    {"persistent_support_grip", &Config::persistent_support_grip},
    {"two_hand_switch_inherit", &Config::two_hand_switch_inherit},
    {"two_hand_support_grip_pose", &Config::two_hand_support_grip_pose},
    {"manual_reload", &Config::manual_reload},
    {"manual_reload_disable_auto", &Config::manual_reload_disable_auto},
    {"manual_reload_skip_animations", &Config::manual_reload_skip_animations},
    {"manual_reload_shortened_animation", &Config::manual_reload_shortened_animation},
    {"weapon_unknown_reload_visual", &Config::weapon_unknown_reload_visual},
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
    {"weapon_pouch_location", &Config::weapon_pouch_location},
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
    {"physical_melee_swing_speed", &Config::physical_melee_swing_speed},
    // Two-handed aiming. Where the support hand grabs the gun is body fit, and stays out.
    {"two_hand_one_hand_range_m", &Config::two_hand_one_hand_range_m},
    {"two_hand_offhand_influence", &Config::two_hand_offhand_influence},
    {"two_hand_smoothing_strength", &Config::two_hand_smoothing_strength},
    // How far the reload and holster gestures reach. Where the custom holster and the reserve magazine sit on the
    // body is body fit, and stays out.
    {"weapon_holster_radius_m", &Config::weapon_holster_radius_m},
    {"weapon_holster_draw_m", &Config::weapon_holster_draw_m},
    {"weapon_shake_travel_m", &Config::weapon_shake_travel_m},
    {"weapon_body_zone_radius_m", &Config::weapon_body_zone_radius_m},
    {"weapon_insert_radius_m", &Config::weapon_insert_radius_m},
};

std::filesystem::path Folder()
{
    const auto beside = std::filesystem::path(Config_Path()).parent_path();
    const auto folder = beside / L"Controls Presets";
    // Builds before the rename kept them in "Input Presets": moved the first time presets are looked for.
    std::error_code error;
    if (!std::filesystem::exists(folder, error) && std::filesystem::exists(beside / L"Input Presets", error))
        std::filesystem::rename(beside / L"Input Presets", folder, error);
    return folder;
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
    std::string text = "# Halo MCC VR controls preset\n";
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

std::string DescriptionLine(const std::string& description)
{
    std::string text;
    for (const char c : description)
        if (text.size() < kDescriptionMost)
            text += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    return text.empty() ? std::string{} : "description = " + text + "\n";
}

std::string ParseDescription(const char* text)
{
    constexpr char kKey[] = "description = ";
    std::istringstream lines(text ? text : "");
    std::string line;
    while (std::getline(lines, line))
        if (line.rfind(kKey, 0) == 0)
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line.substr(sizeof(kKey) - 1);
        }
    return {};
}

int BuiltInCount() noexcept { return 2; }

const char* BuiltInDescription(int index) noexcept
{
    return index == 0 ? "The game's own button layout, with nothing remapped. A good place to start."
         : index == 1 ? "Arranged for VR controllers: right grip melees, left grip uses equipment, X reloads and uses, "
                        "Y switches weapon, and the stick clicks crouch and zoom."
                      : "";
}

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

std::string DescriptionOf(const std::string& name)
{
    for (int i = 0; i < BuiltInCount(); ++i)
        if (name == BuiltInName(i)) return BuiltInDescription(i);
    if (SafeName(name.c_str()) != name || name.empty()) return {};
    std::ifstream file(FileFor(name));
    if (!file) return {};
    std::stringstream text;
    text << file.rdbuf();
    return ParseDescription(text.str().c_str());
}

bool Save(const std::string& name, const Config& scheme, const std::string& description)
{
    if (SafeName(name.c_str()) != name || name.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(Folder(), error);
    std::ofstream file(FileFor(name), std::ios::trunc);
    if (!file)
    {
        LOG("controls preset: could not write '%s'", name.c_str());
        return false;
    }
    file << DescriptionLine(description) << Serialize(scheme);
    LOG("controls preset: saved '%s'", name.c_str());
    return static_cast<bool>(file);
}

bool Delete(const std::string& name)
{
    if (SafeName(name.c_str()) != name || name.empty()) return false;
    std::error_code error;
    return std::filesystem::remove(FileFor(name), error);
}
}
