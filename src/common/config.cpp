#include "weapon_alignment.h"
#include "default_controls.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <charconv>
#include "config.h"
#include "weapon_interaction_logic.h"
#include "flashlight_input.h"
#include "dlss_logic.h"
#include "roomscale_recenter_logic.h"
#include "aim_lead_logic.h"
#include "log.h"

Config g_config;
bool g_hostOwnsRenderSize = false;
bool g_hostIsProjectReclaimer = false;

// Shipped per-seat placements become built-in defaults, so a config file that
// predates a key is still placed rather than bare. Halo 3/ODST rows are
// headset-tuned; Reach rows are the user's Blender-authored, headset-unverified
// lineup. An entry the slot table cannot key is skipped rather than written
// somewhere else, which keeps a future enum change from silently landing a
// scorpion's trim on a mongoose.
Config::Config()
{
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
    {
        title_profiles[profile].crosshair_size_deg = kCrosshairStarts[profile].size_deg;
        title_profiles[profile].crosshair_opacity = kCrosshairStarts[profile].opacity;
        title_profiles[profile].hud_size = kHudStarts[profile].size;
        title_profiles[profile].hud_aspect = kHudStarts[profile].aspect;
        title_profiles[profile].hud_curvature = kHudStarts[profile].curvature;
        title_profiles[profile].hud_vertical_offset = kHudStarts[profile].vertical_offset;
    }
    for (const ConfigShippedSeatTrim& trim : kConfigShippedSeatTrims)
    {
        const int slot = ConfigSeatTrimSlot(
            trim.vehicleId, trim.seatIndex, trim.mountedTurret);
        if (slot < 0 || slot >= kVehicleTrimSlots)
            continue;
        if (trim.hasForward)
        {
            vehicle_cam_forward_v[slot] = trim.forward;
            vehicle_cam_forward_set[slot] = true;
        }
        if (trim.hasUp)
        {
            vehicle_cam_up_v[slot] = trim.up;
            vehicle_cam_up_set[slot] = true;
        }
        if (trim.hasRight)
        {
            vehicle_cam_right_v[slot] = trim.right;
            vehicle_cam_right_set[slot] = true;
        }
    }
    for (const ConfigShippedSeatTrim& trim : kConfigOdstShippedSeatTrims)
    {
        const int slot = ConfigOdstSeatTrimSlot(
            trim.vehicleId, trim.seatIndex, trim.mountedTurret);
        if (slot < 0 || slot >= kOdstVehicleTrimSlots)
            continue;
        if (trim.hasForward)
        {
            odst_vehicle_cam_forward_v[slot] = trim.forward;
            odst_vehicle_cam_forward_set[slot] = true;
        }
        if (trim.hasUp)
        {
            odst_vehicle_cam_up_v[slot] = trim.up;
            odst_vehicle_cam_up_set[slot] = true;
        }
        if (trim.hasRight)
        {
            odst_vehicle_cam_right_v[slot] = trim.right;
            odst_vehicle_cam_right_set[slot] = true;
        }
    }
    for (const ConfigReachShippedSeatTrim& trim :
         kConfigReachShippedSeatTrims)
    {
        const int slot =
            ConfigReachSeatTrimSlot(trim.vehicleId, trim.seatIndex);
        if (slot < 0 || slot >= kReachVehicleTrimSlots)
            continue;
        reach_vehicle_cam_forward_v[slot] = trim.forward;
        reach_vehicle_cam_up_v[slot] = trim.up;
        reach_vehicle_cam_right_v[slot] = trim.right;
        reach_vehicle_cam_forward_set[slot] = true;
        reach_vehicle_cam_up_set[slot] = true;
        reach_vehicle_cam_right_set[slot] = true;
    }
    for (const ConfigShippedVehicleModelTrim& trim : kConfigShippedVehicleModelTrims)
    {
        const int slot = ConfigEnsureVehicleModelTrim(*this, trim.title, trim.identity, trim.seat);
        if (slot >= 0)
            ConfigResetVehicleModelTrim(*this, slot);
    }
}
static std::wstring g_path;

static bool ParseFloatSetting(const char* key, const char* text, float& destination)
{
    char* end = nullptr;
    errno = 0;
    const float parsed = strtof(text, &end);
    while (end && isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end == text || !end || *end != 0 || errno == ERANGE || !std::isfinite(parsed))
    {
        LOG("config: malformed value for '%s' ignored; keeping %.3f", key, destination);
        return false;
    }
    destination = parsed;
    return true;
}

static bool ParseIntSetting(
    const char* key, const char* text, int minimum, int maximum,
    int& destination)
{
    char* end = nullptr;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    while (end && isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end == text || !end || *end != 0 || errno == ERANGE ||
        parsed < minimum || parsed > maximum)
    {
        LOG("config: malformed value for '%s' ignored; keeping %d", key,
            destination);
        return false;
    }
    destination = static_cast<int>(parsed);
    return true;
}

static bool ParseBoolSetting(
    const char* key, const char* text, bool& destination)
{
    int parsed = destination ? 1 : 0;
    if (!ParseIntSetting(key, text, 0, 1, parsed))
        return false;
    destination = parsed != 0;
    return true;
}

static bool ParseVehicleModelTrim(const char* key,const char* val)
{
    constexpr char prefix[]="vehicle_model_";
    if(strncmp(key,prefix,sizeof(prefix)-1))return false;
    const char* rest=key+sizeof(prefix)-1;
    for(unsigned title=0;title<6;++title)
    {
        const auto length=strlen(weapon_interaction::kTitleKeys[title]);
        if(strncmp(rest,weapon_interaction::kTitleKeys[title],length)||rest[length]!='_')continue;
        const char* identityText=rest+length+1;
        if(strlen(identityText)<23)break;
        uint64_t identity=0;
        for(unsigned n=0;n<16;++n)
        {
            const char digit=identityText[n];
            const int value=digit>='0'&&digit<='9'?digit-'0':digit>='a'&&digit<='f'?digit-'a'+10:
                digit>='A'&&digit<='F'?digit-'A'+10:-1;
            if(value<0){LOG("config: malformed vehicle identity in '%s' ignored",key);return true;}
            identity=(identity<<4)|static_cast<unsigned>(value);
        }
        if(!identity||strncmp(identityText+16,"_seat",5))break;
        const char* seatText=identityText+21;
        if(*seatText<'0'||*seatText>'9')break;
        char* end{};const long seat=strtol(seatText,&end,10);
        if(seat<0||seat>31||!end||*end!='_')break;
        const char* axes[]{"forward_m","up_m","right_m"};
        for(unsigned axis=0;axis<3;++axis)if(!strcmp(end+1,axes[axis]))
        {
            float value=0;if(!ParseFloatSetting(key,val,value))return true;
            const int slot=ConfigEnsureVehicleModelTrim(g_config,static_cast<GameTitle>(title+1),identity,static_cast<int>(seat));
            if(slot<0){LOG("config: vehicle preset capacity reached; '%s' ignored",key);return true;}
            g_config.vehicle_model_trims[slot].value[axis]=value;
            g_config.vehicle_model_trims[slot].set[axis]=true;return true;
        }
        break;
    }
    LOG("config: malformed vehicle preset '%s' ignored",key);return true;
}

// C19 per-seat trim keys. `suffix` is what follows vehicle_cam_forward_m_ /
// vehicle_cam_up_m_ / vehicle_cam_right_m_ and is either "<vehicle>_<seat>" or
// the C12-era
// "<vehicle>", which meant the whole vehicle and so migrates onto every seat
// slot of it. An unknown vehicle or seat name is reported and dropped rather
// than misfiled, and a malformed value never creates an override.
static void ParseSeatTrim(const char* key, const char* suffix,
                          const char* val, float* values, bool* setFlags)
{
    for (int v = 0; v < kVehicleTrimCount; ++v)
    {
        const size_t nameLen = strlen(kVehicleTrimNames[v]);
        if (strncmp(suffix, kVehicleTrimNames[v], nameLen) != 0)
            continue;
        const char* rest = suffix + nameLen;
        if (*rest == 0)
        {
            // Legacy whole-vehicle key: apply to every seat of it.
            float parsed = values[v * kVehicleSeatSlots];
            if (!ParseFloatSetting(key, val, parsed))
                return;
            for (int s = 0; s < kVehicleSeatSlots; ++s)
            {
                values[v * kVehicleSeatSlots + s] = parsed;
                setFlags[v * kVehicleSeatSlots + s] = true;
            }
            LOG("config: '%s' applied to every seat of the %s (per-seat trim "
                "replaced the per-vehicle key)", key, kVehicleTrimNames[v]);
            return;
        }
        if (*rest != '_')
            continue;               // a longer vehicle name may still match
        ++rest;
        for (int s = 0; s < kVehicleSeatSlots; ++s)
            if (!strcmp(rest, kVehicleSeatNames[s]))
            {
                const int slot = v * kVehicleSeatSlots + s;
                if (ParseFloatSetting(key, val, values[slot]))
                    setFlags[slot] = true;
                return;
            }
        LOG("config: unknown seat in '%s' ignored", key);
        return;
    }
    LOG("config: unknown vehicle in '%s' ignored", key);
}

// The ODST bank, keyed "odst_<vehicle>_<seat>". Deliberately a separate parser
// from the Halo 3 one above rather than a parameterised version of it: ODST has
// its own vehicle list (it adds the shade) and its own seat list (its scorpion
// riders are player-enterable), so sharing the loop would need both tables
// threaded through and could mis-file a seat that exists in only one title.
// There is no legacy whole-vehicle form here — these keys are new.
static void ParseOdstSeatTrim(const char* key, const char* suffix,
                              const char* val, float* values, bool* setFlags)
{
    for (int v = 0; v < kOdstVehicleTrimCount; ++v)
    {
        const size_t nameLen = strlen(kOdstVehicleTrimNames[v]);
        if (strncmp(suffix, kOdstVehicleTrimNames[v], nameLen) != 0)
            continue;
        const char* rest = suffix + nameLen;
        if (*rest != '_')
            continue;               // a longer vehicle name may still match
        ++rest;
        for (int s = 0; s < kOdstVehicleSeatSlots; ++s)
            if (!strcmp(rest, kOdstVehicleSeatNames[s]))
            {
                const int slot = v * kOdstVehicleSeatSlots + s;
                if (ParseFloatSetting(key, val, values[slot]))
                    setFlags[slot] = true;
                return;
            }
        LOG("config: unknown ODST seat in '%s' ignored", key);
        return;
    }
    LOG("config: unknown ODST vehicle in '%s' ignored", key);
}

// Reach uses role-neutral seat0..seat15 keys because raw indices do not imply
// the same driver/passenger/gunner role across its official HREK tags.
static constexpr char kReachSeatUniversalPrefix[] =
    "vehicle_cam_use_universal_reach_";
static constexpr size_t kReachSeatUniversalPrefixLength =
    sizeof(kReachSeatUniversalPrefix) - 1;

static int FindReachSeatTrimSlot(const char* key, const char* suffix)
{
    for (int v = 0; v < kReachVehicleTrimCount; ++v)
    {
        const size_t nameLen = strlen(kReachVehicleTrimNames[v]);
        if (strncmp(suffix, kReachVehicleTrimNames[v], nameLen) != 0)
            continue;
        const char* rest = suffix + nameLen;
        if (*rest != '_')
            continue;
        ++rest;
        for (int s = 0; s < kReachVehicleSeatSlots; ++s)
            if (!strcmp(rest, kReachVehicleSeatNames[s]))
                return v * kReachVehicleSeatSlots + s;
    }
    LOG("config: unknown Reach vehicle or seat in '%s' ignored", key);
    return -1;
}

static int ParseReachSeatTrim(const char* key, const char* suffix,
                              const char* val, float* values, bool* setFlags)
{
    const int slot = FindReachSeatTrimSlot(key, suffix);
    if (slot < 0 || !ParseFloatSetting(key, val, values[slot]))
        return -1;
    setFlags[slot] = true;
    return slot;
}

static int ParseReachSeatUniversal(
    const char* key, const char* suffix, const char* val, bool* tombstones)
{
    const int slot = FindReachSeatTrimSlot(key, suffix);
    if (slot < 0)
        return -1;
    float parsed = tombstones[slot] ? 1.0f : 0.0f;
    if (!ParseFloatSetting(key, val, parsed))
        return -1;
    tombstones[slot] = parsed != 0.0f;
    return slot;
}

static bool FileExists(const wchar_t* path)
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void Clamp()
{
    g_config.config_version = 7;
    g_config.haptic_intensity = std::clamp(g_config.haptic_intensity, 0.0f, 1.0f);
    g_config.dpad_head_radius = std::clamp(g_config.dpad_head_radius, 0.10f, 0.50f);
    g_config.headset_smoothing = std::clamp(g_config.headset_smoothing, 0.0f, 0.10f);
    g_config.aim_stabilization = std::clamp(g_config.aim_stabilization, 0.0f, 0.95f);
    g_config.hand_smoothing_hands = std::clamp(g_config.hand_smoothing_hands, 0, 2);
    g_config.hand_smoothing_pos_min_hz = std::clamp(g_config.hand_smoothing_pos_min_hz, 0.0f, 30.0f);
    g_config.hand_smoothing_pos_beta = std::clamp(g_config.hand_smoothing_pos_beta, 0.0f, 100000.0f);
    g_config.hand_smoothing_rot_min_hz = std::clamp(g_config.hand_smoothing_rot_min_hz, 0.0f, 30.0f);
    g_config.hand_smoothing_rot_beta = std::clamp(g_config.hand_smoothing_rot_beta, 0.0f, 1000.0f);
    g_config.hand_smoothing_speed_cutoff_hz = std::clamp(g_config.hand_smoothing_speed_cutoff_hz, 0.1f, 100.0f);
    g_config.screen_width_m = std::clamp(g_config.screen_width_m, 0.5f, 20.0f);
    g_config.screen_distance_m = std::clamp(g_config.screen_distance_m, 0.3f, 20.0f);
    g_config.host_ui_panel_width_m = std::clamp(g_config.host_ui_panel_width_m, 0.5f, 20.0f);
    g_config.host_ui_panel_distance_m = std::clamp(g_config.host_ui_panel_distance_m, 0.3f, 20.0f);
    g_config.cutscene_theater_depth = std::clamp(
        g_config.cutscene_theater_depth, 0.0f, 2.0f);
    g_config.cutscene_theater_width_m = std::clamp(
        g_config.cutscene_theater_width_m, 0.5f, 20.0f);
    g_config.cutscene_theater_distance_m = std::clamp(
        g_config.cutscene_theater_distance_m, 0.3f, 20.0f);
    // 0 is the documented "no cine bars" value, so it must survive the clamp.
    if (g_config.cutscene_theater_matte_aspect > 0.0f)
    {
        g_config.cutscene_theater_matte_aspect = std::clamp(
            g_config.cutscene_theater_matte_aspect, 1.0f, 3.0f);
    }
    else
    {
        g_config.cutscene_theater_matte_aspect = 0.0f;
    }
    g_config.cutscene_theater_matte_offset = std::clamp(
        g_config.cutscene_theater_matte_offset, -0.25f, 0.25f);
    g_config.cutscene_theater_subtitle_band = std::clamp(
        g_config.cutscene_theater_subtitle_band, 0.05f, 1.0f);
    g_config.menu_distance_m = std::clamp(g_config.menu_distance_m,
                                          kMenuDistanceMin, kMenuDistanceMax);
    g_config.menu_width_m = std::clamp(g_config.menu_width_m,
                                       kMenuWidthMin, kMenuWidthMax);
    g_config.menu_height_m = std::clamp(g_config.menu_height_m,
                                        -kMenuOffsetLimit, kMenuOffsetLimit);
    g_config.menu_side_m = std::clamp(g_config.menu_side_m,
                                      -kMenuOffsetLimit, kMenuOffsetLimit);
    g_config.turn_snap_deg = std::clamp(g_config.turn_snap_deg, 5.0f, 90.0f);
    g_config.turn_smooth_deg_s = std::clamp(g_config.turn_smooth_deg_s, 30.0f, 360.0f);
    g_config.vehicle_cam_forward_m =
        std::clamp(g_config.vehicle_cam_forward_m,
                   kVehicleCamForwardMin, kVehicleCamForwardMax);
    g_config.vehicle_cam_up_m =
        std::clamp(g_config.vehicle_cam_up_m,
                   kVehicleCamUpMin, kVehicleCamUpMax);
    g_config.vehicle_cam_right_m =
        std::clamp(g_config.vehicle_cam_right_m,
                   kVehicleCamRightMin, kVehicleCamRightMax);
    const float vehicleMinimum[]{kVehicleCamForwardMin,kVehicleCamUpMin,kVehicleCamRightMin};
    const float vehicleMaximum[]{kVehicleCamForwardMax,kVehicleCamUpMax,kVehicleCamRightMax};
    for(unsigned title=0;title<6;++title)for(unsigned axis=0;axis<3;++axis)
    {
        float& value=g_config.vehicle_cam_game[title][axis];
        if(!std::isfinite(value)){value=0;g_config.vehicle_cam_game_set[title][axis]=false;}
        else value=std::clamp(value,vehicleMinimum[axis],vehicleMaximum[axis]);
    }
    for(auto& trim:g_config.vehicle_model_trims)for(unsigned axis=0;axis<3;++axis)
    {
        if(!std::isfinite(trim.value[axis])){trim.value[axis]=0;trim.set[axis]=false;}
        else trim.value[axis]=std::clamp(trim.value[axis],vehicleMinimum[axis],vehicleMaximum[axis]);
    }
    // This one triplet is the base for EVERY seat in all three titles that has
    // no line of its own. On 2026-08-06 it was walked from the accepted
    // 0.10/0.05/0.00 to -0.76/+0.89 by a Reach vehicle the mod could not
    // identify, which silently moved Halo 3 and ODST seats for days. R-V25
    // removed the mechanism; this makes the state itself impossible to miss,
    // because nothing else in the log distinguishes "the user tuned this" from
    // "something else wrote it".
    if (g_config.vehicle_cam_forward_m != kVehicleCamForwardDefault ||
        g_config.vehicle_cam_up_m != kVehicleCamUpDefault ||
        g_config.vehicle_cam_right_m != kVehicleCamRightDefault)
    {
        LOG("config: the SHARED universal seat trim is %.2f/%.2f/%.2f, not the "
            "shipped %.2f/%.2f/%.2f. Every seat in Halo 3, ODST and Reach "
            "without its own line follows this. If a seat feels wrong in a "
            "title you were not tuning, reset it in F1 > Vehicles.",
            g_config.vehicle_cam_forward_m, g_config.vehicle_cam_up_m,
            g_config.vehicle_cam_right_m, kVehicleCamForwardDefault,
            kVehicleCamUpDefault, kVehicleCamRightDefault);
    }
    for (int i = 0; i < kVehicleTrimSlots; ++i)
    {
        g_config.vehicle_cam_forward_v[i] =
            std::clamp(g_config.vehicle_cam_forward_v[i],
                       kVehicleCamForwardMin, kVehicleCamForwardMax);
        g_config.vehicle_cam_up_v[i] =
            std::clamp(g_config.vehicle_cam_up_v[i],
                       kVehicleCamUpMin, kVehicleCamUpMax);
        g_config.vehicle_cam_right_v[i] =
            std::clamp(g_config.vehicle_cam_right_v[i],
                       kVehicleCamRightMin, kVehicleCamRightMax);
    }
    for (int i = 0; i < kOdstVehicleTrimSlots; ++i)
    {
        g_config.odst_vehicle_cam_forward_v[i] =
            std::clamp(g_config.odst_vehicle_cam_forward_v[i],
                       kVehicleCamForwardMin, kVehicleCamForwardMax);
        g_config.odst_vehicle_cam_up_v[i] =
            std::clamp(g_config.odst_vehicle_cam_up_v[i],
                       kVehicleCamUpMin, kVehicleCamUpMax);
        g_config.odst_vehicle_cam_right_v[i] =
            std::clamp(g_config.odst_vehicle_cam_right_v[i],
                       kVehicleCamRightMin, kVehicleCamRightMax);
    }
    for (int i = 0; i < kReachVehicleTrimSlots; ++i)
    {
        g_config.reach_vehicle_cam_forward_v[i] = std::clamp(
            g_config.reach_vehicle_cam_forward_v[i],
            kReachVehicleCamForwardMin, kReachVehicleCamForwardMax);
        g_config.reach_vehicle_cam_up_v[i] = std::clamp(
            g_config.reach_vehicle_cam_up_v[i],
            kReachVehicleCamUpMin, kReachVehicleCamUpMax);
        g_config.reach_vehicle_cam_right_v[i] = std::clamp(
            g_config.reach_vehicle_cam_right_v[i],
            kReachVehicleCamRightMin, kReachVehicleCamRightMax);
    }
    g_config.vehicle_wheel_max_deg =
        std::clamp(g_config.vehicle_wheel_max_deg, 30.0f, 180.0f);
    g_config.vehicle_wheel_deadzone_deg =
        std::clamp(g_config.vehicle_wheel_deadzone_deg, 0.0f, 30.0f);
    // vehicle_aim_reject_logic.h holds the same bounds (kMaxMetres) for a value set between loads.
    g_config.vehicle_aim_reject_metres = std::isfinite(g_config.vehicle_aim_reject_metres)
        ? std::clamp(g_config.vehicle_aim_reject_metres, 0.0f, 20.0f) : Config{}.vehicle_aim_reject_metres;
    g_config.vehicle_aim_reject_dot = std::isfinite(g_config.vehicle_aim_reject_dot)
        ? std::clamp(g_config.vehicle_aim_reject_dot, -1.0f, 1.0f) : Config{}.vehicle_aim_reject_dot;
    // vehicle_boom_logic.h holds the same bounds (kMinRateDegPerSecond, kMaxRateDegPerSecond) for a value set
    // between loads.
    g_config.vehicle_boom_rate_deg_s = std::isfinite(g_config.vehicle_boom_rate_deg_s)
        ? std::clamp(g_config.vehicle_boom_rate_deg_s, 5.0f, 360.0f) : Config{}.vehicle_boom_rate_deg_s;
    g_config.vehicle_boom_invert = std::clamp(g_config.vehicle_boom_invert, -1, 1);
    // Hand trims are deliberately small: they nudge an authored placement, not
    // relocate the weapon across the room. A wild value here would put the gun
    // outside the first-person node envelope the engine validates against.
    g_config.halo4_hand_forward_m =
        std::clamp(g_config.halo4_hand_forward_m, -0.50f, 0.50f);
    g_config.halo4_hand_vertical_m =
        std::clamp(g_config.halo4_hand_vertical_m, -0.50f, 0.50f);
    g_config.halo4_hand_lateral_m =
        std::clamp(g_config.halo4_hand_lateral_m, -0.50f, 0.50f);
    g_config.crosshair_distance_m = std::clamp(g_config.crosshair_distance_m, 2.0f, 50.0f);
    g_config.crosshair_size_deg = std::clamp(g_config.crosshair_size_deg, 0.3f, 20.0f);
    g_config.crosshair_opacity = std::isfinite(g_config.crosshair_opacity)
        ? std::clamp(g_config.crosshair_opacity, 0.0f, 2.0f) : 1.0f;
    g_config.crosshair_brightness = std::isfinite(g_config.crosshair_brightness)
        ? std::clamp(g_config.crosshair_brightness, 0.0f, 2.0f) : 1.0f;
    g_config.reticle_max_dist_m = std::clamp(g_config.reticle_max_dist_m, 1.0f, 50.0f);
    g_config.reticle_surface_offset_m = std::clamp(g_config.reticle_surface_offset_m, 0.0f, 2.0f);
    g_config.reticle_min_scale_dist_m =
        std::clamp(g_config.reticle_min_scale_dist_m, 0.0f, g_config.reticle_max_dist_m - 0.1f);
    g_config.reticle_min_scale = std::clamp(g_config.reticle_min_scale, 0.001f, 1.0f);
    // 0 stays 0 (hold one image). Below 6 is honoured only where a publish is
    // measured cheap (authored_reticle_logic.h, AuthoredReticleUploadGapForCost).
    if (g_config.crosshair_animation_frames != 0)
        g_config.crosshair_animation_frames =
            std::clamp(g_config.crosshair_animation_frames, 1, 60);
    g_config.reticle_r = std::clamp(g_config.reticle_r, 0.0f, 1.0f);
    g_config.reticle_g = std::clamp(g_config.reticle_g, 0.0f, 1.0f);
    g_config.reticle_b = std::clamp(g_config.reticle_b, 0.0f, 1.0f);
    g_config.gun_scale = std::clamp(g_config.gun_scale, 0.3f, 3.0f);
    g_config.left_hand_scale = std::clamp(g_config.left_hand_scale, 0.3f, 3.0f);
    g_config.left_hand_mesh_x_m = std::isfinite(g_config.left_hand_mesh_x_m) ? std::clamp(g_config.left_hand_mesh_x_m, -.20f, .20f) : 0.0f;
    g_config.left_hand_mesh_y_m = std::isfinite(g_config.left_hand_mesh_y_m) ? std::clamp(g_config.left_hand_mesh_y_m, -.20f, .20f) : 0.0f;
    g_config.left_hand_mesh_z_m = std::isfinite(g_config.left_hand_mesh_z_m) ? std::clamp(g_config.left_hand_mesh_z_m, -.20f, .20f) : 0.0f;
    g_config.right_hand_mesh_x_m = std::isfinite(g_config.right_hand_mesh_x_m) ? std::clamp(g_config.right_hand_mesh_x_m, -.20f, .20f) : 0.0f;
    g_config.right_hand_mesh_y_m = std::isfinite(g_config.right_hand_mesh_y_m) ? std::clamp(g_config.right_hand_mesh_y_m, -.20f, .20f) : 0.0f;
    g_config.right_hand_mesh_z_m = std::isfinite(g_config.right_hand_mesh_z_m) ? std::clamp(g_config.right_hand_mesh_z_m, -.20f, .20f) : 0.0f;

    g_config.game_brightness = std::isfinite(g_config.game_brightness)
        ? std::clamp(g_config.game_brightness, 0.5f, 2.0f) : TitleTunables{}.game_brightness;
    // Free-form: a hand-typed 0.90 stays 0.90. (Until 2026-07-20 this snapped
    // to the six installer tiers, so any custom value was silently rounded.)
    g_config.resolution_scale = std::clamp(g_config.resolution_scale,
                                           kResolutionScaleMin, kResolutionScaleMax);
    g_config.draw_distance = std::clamp(g_config.draw_distance,
                                        kDrawDistanceMin, kDrawDistanceMax);
    g_config.upscale_filter = std::clamp(g_config.upscale_filter, 0, 1);
    g_config.sharpness = std::clamp(g_config.sharpness, 0.0f, 1.0f);
    g_config.aa_mode = std::clamp(g_config.aa_mode, 0, 4);
    g_config.hud_size = std::clamp(g_config.hud_size, 0.30f, 1.00f);
    g_config.hud_aspect = std::clamp(g_config.hud_aspect, kHudAspectMin, kHudAspectMax);
    g_config.hud_curvature = std::clamp(g_config.hud_curvature,
                                        kHudCurvatureMin, kHudCurvatureMax);
    g_config.hud_vertical_offset = std::clamp(g_config.hud_vertical_offset,
                                              kHudHeightMin, kHudHeightMax);
    g_config.left_hand_forward_m = std::clamp(g_config.left_hand_forward_m, -0.15f, 0.30f);
    g_config.two_hand_zone_right_m = std::clamp(g_config.two_hand_zone_right_m, -0.02f, 0.18f);
    g_config.left_grip_forward_m = std::clamp(g_config.left_grip_forward_m, -0.05f, 0.25f);
    g_config.virtual_stock_standard_strength =
        std::isfinite(g_config.virtual_stock_standard_strength)
            ? std::clamp(g_config.virtual_stock_standard_strength,
                         kVirtualStockStrengthMinimum,
                         kVirtualStockStrengthMaximum)
            : kVirtualStockStandardStrengthDefault;
    g_config.virtual_stock_plus_strength =
        std::isfinite(g_config.virtual_stock_plus_strength)
            ? std::clamp(g_config.virtual_stock_plus_strength,
                         kVirtualStockStrengthMinimum,
                         kVirtualStockStrengthMaximum)
            : kVirtualStockPlusStrengthDefault;
    g_config.two_hand_smoothing_strength =
        std::isfinite(g_config.two_hand_smoothing_strength)
            ? std::clamp(g_config.two_hand_smoothing_strength,
                         kTwoHandSmoothingStrengthMinimum,
                         kTwoHandSmoothingStrengthMaximum)
            : kTwoHandSmoothingStrengthDefault;
    g_config.two_hand_offhand_influence =
        std::isfinite(g_config.two_hand_offhand_influence)
            ? std::clamp(g_config.two_hand_offhand_influence,
                         kTwoHandOffhandInfluenceMinimum,
                         kTwoHandOffhandInfluenceMaximum)
            : kTwoHandOffhandInfluenceDefault;
    g_config.virtual_stock_rear_height_m = std::isfinite(g_config.virtual_stock_rear_height_m)
        ? std::clamp(g_config.virtual_stock_rear_height_m,
                     kVirtualStockRearHeightMinimumM,
                     kVirtualStockRearHeightMaximumM)
        : kVirtualStockRearHeightDefaultM;
    g_config.virtual_stock_rear_reference =
        g_config.virtual_stock_rear_reference >= kVirtualStockRearReferenceMinimum &&
                g_config.virtual_stock_rear_reference <= kVirtualStockRearReferenceMaximum
            ? g_config.virtual_stock_rear_reference
            : kVirtualStockProductRearReferenceDefault;
    // Chest was an experimental mode and is no longer a product selection.
    if (g_config.virtual_stock_rear_reference == 2)
        g_config.virtual_stock_rear_reference = 0;
    g_config.virtual_stock_shoulder_back_m =
        std::isfinite(g_config.virtual_stock_shoulder_back_m)
            ? std::clamp(g_config.virtual_stock_shoulder_back_m,
                         kVirtualStockShoulderBackMinimumM,
                         kVirtualStockShoulderBackMaximumM)
            : kVirtualStockShoulderBackDefaultM;
    g_config.virtual_stock_shoulder_side_m =
        std::isfinite(g_config.virtual_stock_shoulder_side_m)
            ? std::clamp(g_config.virtual_stock_shoulder_side_m,
                         kVirtualStockShoulderSideMinimumM,
                         kVirtualStockShoulderSideMaximumM)
            : kVirtualStockShoulderSideDefaultM;
    g_config.virtual_stock_chest_height_m =
        std::isfinite(g_config.virtual_stock_chest_height_m)
            ? std::clamp(g_config.virtual_stock_chest_height_m,
                         kVirtualStockChestHeightMinimumM,
                         kVirtualStockChestHeightMaximumM)
            : kVirtualStockChestHeightDefaultM;
    g_config.virtual_stock_chest_back_m =
        std::isfinite(g_config.virtual_stock_chest_back_m)
            ? std::clamp(g_config.virtual_stock_chest_back_m,
                         kVirtualStockChestBackMinimumM,
                         kVirtualStockChestBackMaximumM)
            : kVirtualStockChestBackDefaultM;
    g_config.virtual_stock_chest_side_m =
        std::isfinite(g_config.virtual_stock_chest_side_m)
            ? std::clamp(g_config.virtual_stock_chest_side_m,
                         kVirtualStockChestSideMinimumM,
                         kVirtualStockChestSideMaximumM)
            : kVirtualStockChestSideDefaultM;
    g_config.virtual_stock_adaptive_top_height_m =
        std::isfinite(g_config.virtual_stock_adaptive_top_height_m)
            ? std::clamp(g_config.virtual_stock_adaptive_top_height_m,
                         kVirtualStockAdaptiveTopHeightMinimumM,
                         kVirtualStockAdaptiveTopHeightMaximumM)
            : kVirtualStockAdaptiveTopHeightDefaultM;
    g_config.virtual_stock_adaptive_bottom_height_m =
        std::isfinite(g_config.virtual_stock_adaptive_bottom_height_m)
            ? std::clamp(g_config.virtual_stock_adaptive_bottom_height_m,
                         kVirtualStockAdaptiveBottomHeightMinimumM,
                         kVirtualStockAdaptiveBottomHeightMaximumM)
            : kVirtualStockAdaptiveBottomHeightDefaultM;
    g_config.virtual_stock_adaptive_top_half_width_m =
        std::isfinite(g_config.virtual_stock_adaptive_top_half_width_m)
            ? std::clamp(g_config.virtual_stock_adaptive_top_half_width_m,
                         kVirtualStockAdaptiveTopHalfWidthMinimumM,
                         kVirtualStockAdaptiveTopHalfWidthMaximumM)
            : kVirtualStockAdaptiveTopHalfWidthDefaultM;
    g_config.virtual_stock_adaptive_bottom_half_width_m =
        std::isfinite(g_config.virtual_stock_adaptive_bottom_half_width_m)
            ? std::clamp(g_config.virtual_stock_adaptive_bottom_half_width_m,
                         kVirtualStockAdaptiveBottomHalfWidthMinimumM,
                         kVirtualStockAdaptiveBottomHalfWidthMaximumM)
            : kVirtualStockAdaptiveBottomHalfWidthDefaultM;
    g_config.virtual_stock_hybrid_offhand_influence =
        std::isfinite(g_config.virtual_stock_hybrid_offhand_influence)
            ? std::clamp(g_config.virtual_stock_hybrid_offhand_influence,
                         kVirtualStockHybridOffhandInfluenceMinimum,
                         kVirtualStockHybridOffhandInfluenceMaximum)
            : kVirtualStockHybridOffhandInfluenceDefault;
    g_config.virtual_stock_hybrid_ads_reference =
        g_config.virtual_stock_hybrid_ads_reference >=
                kVirtualStockHybridAdsReferenceMinimum &&
                g_config.virtual_stock_hybrid_ads_reference <=
                    kVirtualStockHybridAdsReferenceMaximum
            ? g_config.virtual_stock_hybrid_ads_reference
            : kVirtualStockHybridAdsReferenceDefault;
    g_config.virtual_stock_hybrid_seat_full_m =
        std::isfinite(g_config.virtual_stock_hybrid_seat_full_m)
            ? std::clamp(g_config.virtual_stock_hybrid_seat_full_m,
                         kVirtualStockHybridSeatFullMinimumM,
                         kVirtualStockHybridSeatFullMaximumM)
            : kVirtualStockProductHybridSeatFullDefaultM;
    g_config.virtual_stock_hybrid_seat_release_m =
        std::isfinite(g_config.virtual_stock_hybrid_seat_release_m)
            ? std::clamp(g_config.virtual_stock_hybrid_seat_release_m,
                         kVirtualStockHybridSeatReleaseMinimumM,
                         kVirtualStockHybridSeatReleaseMaximumM)
            : kVirtualStockProductHybridSeatReleaseDefaultM;
    g_config.virtual_stock_hybrid_horizontal_full_m =
        std::isfinite(g_config.virtual_stock_hybrid_horizontal_full_m)
            ? std::clamp(g_config.virtual_stock_hybrid_horizontal_full_m,
                         kVirtualStockProximityFullMinimumM,
                         kVirtualStockProximityFullMaximumM)
            : kVirtualStockProductHybridHorizontalFullDefaultM;
    g_config.virtual_stock_hybrid_horizontal_release_m =
        std::isfinite(g_config.virtual_stock_hybrid_horizontal_release_m)
            ? std::clamp(g_config.virtual_stock_hybrid_horizontal_release_m,
                         kVirtualStockProximityReleaseMinimumM,
                         kVirtualStockProximityReleaseMaximumM)
            : kVirtualStockProductHybridHorizontalReleaseDefaultM;
    g_config.virtual_stock_proximity_full_m =
        std::isfinite(g_config.virtual_stock_proximity_full_m)
            ? std::clamp(g_config.virtual_stock_proximity_full_m,
                         kVirtualStockProximityFullMinimumM,
                         kVirtualStockProximityFullMaximumM)
            : kVirtualStockProximityFullDefaultM;
    g_config.virtual_stock_proximity_release_m =
        std::isfinite(g_config.virtual_stock_proximity_release_m)
            ? std::clamp(g_config.virtual_stock_proximity_release_m,
                         kVirtualStockProximityReleaseMinimumM,
                         kVirtualStockProximityReleaseMaximumM)
            : kVirtualStockProximityReleaseDefaultM;
    g_config.physical_melee_swing_speed = std::clamp(
        g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax);
    g_config.gesture_melee_speed = std::clamp(g_config.gesture_melee_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax);
    g_config.gesture_melee_extension = std::clamp(g_config.gesture_melee_extension, 0.0f, 6.0f);
    g_config.gesture_melee_travel = std::clamp(g_config.gesture_melee_travel, 0.0f, 1.2f);
    g_config.gesture_melee_reach = std::clamp(g_config.gesture_melee_reach, 0.0f, 1.0f);
    g_config.gesture_melee_aim_hold_ms = std::clamp(g_config.gesture_melee_aim_hold_ms, 0, 1000);
    g_config.gesture_melee_aim_ramp_ms = std::clamp(g_config.gesture_melee_aim_ramp_ms, 0, 1000);
    g_config.gesture_melee_press_ms = std::clamp(g_config.gesture_melee_press_ms, 20, 500);
    g_config.gesture_melee_cooldown_ms = std::clamp(g_config.gesture_melee_cooldown_ms, 100, 3000);
    g_config.gesture_melee_holster_veto_ms = std::clamp(g_config.gesture_melee_holster_veto_ms, 0, 3000);
    g_config.gesture_melee_smoothing_ms = std::clamp(g_config.gesture_melee_smoothing_ms, 0.0f, 200.0f);
    g_config.gesture_melee_max_speed = std::clamp(g_config.gesture_melee_max_speed, 10.0f, 100.0f);
    g_config.gesture_melee_max_reach = std::clamp(g_config.gesture_melee_max_reach, 1.0f, 3.0f);
    g_config.right_shoulder_drop = std::clamp(g_config.right_shoulder_drop, 0.0f, 0.3f);
    g_config.shoulder_back_m = std::clamp(g_config.shoulder_back_m, -0.3f, 0.3f);
    g_config.gun_pitch_deg = std::clamp(g_config.gun_pitch_deg, -180.0f, 180.0f);
    g_config.gun_yaw_deg = std::clamp(g_config.gun_yaw_deg, -180.0f, 180.0f);
    g_config.gun_roll_deg = std::clamp(g_config.gun_roll_deg, -180.0f, 180.0f);
    g_config.gun_forward_m = std::clamp(g_config.gun_forward_m, weapon_alignment::kForwardMinimum, weapon_alignment::kForwardMaximum);
    g_config.gun_right_m = std::clamp(g_config.gun_right_m, weapon_alignment::kRightMinimum, weapon_alignment::kRightMaximum);
    g_config.gun_up_m = std::clamp(g_config.gun_up_m, weapon_alignment::kUpMinimum, weapon_alignment::kUpMaximum);
    g_config.halo2_classic_gun_pitch_deg =
        std::clamp(g_config.halo2_classic_gun_pitch_deg, -180.0f, 180.0f);
    g_config.halo2_classic_gun_yaw_deg =
        std::clamp(g_config.halo2_classic_gun_yaw_deg, -180.0f, 180.0f);
    g_config.halo2_classic_gun_roll_deg =
        std::clamp(g_config.halo2_classic_gun_roll_deg, -180.0f, 180.0f);
    g_config.halo2_classic_gun_forward_m =
        std::clamp(g_config.halo2_classic_gun_forward_m, -0.5f, 0.5f);
    g_config.halo2_classic_gun_right_m =
        std::clamp(g_config.halo2_classic_gun_right_m, -0.3f, 0.3f);
    g_config.halo2_classic_gun_up_m =
        std::clamp(g_config.halo2_classic_gun_up_m, -0.3f, 0.3f);
    g_config.muzzle_height_m = std::clamp(g_config.muzzle_height_m, -0.3f, 0.3f);
    g_config.scope_zoom = std::clamp(g_config.scope_zoom, 6.0f, 24.0f);
    for (int level = 0; level < 2; ++level)
    {
        const float size = g_config.aim_frame_size_deg[level];
        g_config.aim_frame_size_deg[level] = std::isfinite(size) ? std::clamp(size, 10.0f, 120.0f) : 100.0f;
        const float strength = g_config.aim_frame_strength[level];
        g_config.aim_frame_strength[level] = std::isfinite(strength) ? std::clamp(strength, 0.5f, 4.0f) : 1.0f;
    }
    g_config.aim_frame_3d_strength = std::isfinite(g_config.aim_frame_3d_strength)
        ? std::clamp(g_config.aim_frame_3d_strength, 1.0f, 10.0f) : 6.0f;
    g_config.two_hand_one_hand_range_m = std::isfinite(g_config.two_hand_one_hand_range_m)
        ? std::clamp(g_config.two_hand_one_hand_range_m, 0.06f, 0.48f) : 0.24f;
    g_config.aim_frame_move_s = std::isfinite(g_config.aim_frame_move_s)
        ? std::clamp(g_config.aim_frame_move_s, 0.0f, 1.0f) : 0.12f;
    g_config.aim_frame_hud_size = std::isfinite(g_config.aim_frame_hud_size)
        ? std::clamp(g_config.aim_frame_hud_size, 0.5f, 1.25f) : 1.0f;
    g_config.aim_frame_hud_width_scale = std::isfinite(g_config.aim_frame_hud_width_scale)
        ? std::clamp(g_config.aim_frame_hud_width_scale, 0.5f, 1.5f) : 1.0f;
    g_config.aim_frame_hud_height_scale = std::isfinite(g_config.aim_frame_hud_height_scale)
        ? std::clamp(g_config.aim_frame_hud_height_scale, 0.5f, 1.5f) : 1.0f;
    g_config.scope_screen_width_m = std::clamp(g_config.scope_screen_width_m, 0.04f, 0.25f);
    g_config.scope_screen_right_m = std::clamp(g_config.scope_screen_right_m, -0.30f, 0.30f);
    g_config.scope_screen_up_m = std::clamp(g_config.scope_screen_up_m, -0.20f, 0.30f);
    g_config.scope_screen_forward_m = std::clamp(g_config.scope_screen_forward_m, 0.05f, 0.80f);
    g_config.scope_refresh_divisor = std::clamp(g_config.scope_refresh_divisor, 1, 4);
}

// Pair validation belongs after the complete file has been parsed. Key order
// must not change the result when a hand-edited config contains both values.
static void NormalizeVirtualStockProximityPair()
{
    if (!(g_config.virtual_stock_proximity_release_m >
          g_config.virtual_stock_proximity_full_m))
    {
        g_config.virtual_stock_proximity_full_m =
            kVirtualStockProximityFullDefaultM;
        g_config.virtual_stock_proximity_release_m =
            kVirtualStockProximityReleaseDefaultM;
    }
}

// Hybrid seating uses its own authority fade rather than the legacy proximity
// strength attenuation. Keep the pair deterministic after the complete config
// file has been parsed, with a 10 mm minimum interval matching the UI policy.
static void NormalizeVirtualStockHybridSeatPair()
{
    if (g_config.virtual_stock_hybrid_seat_release_m +
            kVirtualStockHybridSeatComparisonEpsilon <
        g_config.virtual_stock_hybrid_seat_full_m +
            kVirtualStockHybridSeatMinimumSeparationM)
    {
        g_config.virtual_stock_hybrid_seat_full_m =
            kVirtualStockProductHybridSeatFullDefaultM;
        g_config.virtual_stock_hybrid_seat_release_m =
            kVirtualStockProductHybridSeatReleaseDefaultM;
    }
}

static void NormalizeVirtualStockHybridHorizontalPair()
{
    if (!(g_config.virtual_stock_hybrid_horizontal_release_m >=
          g_config.virtual_stock_hybrid_horizontal_full_m +
              kVirtualStockHybridHorizontalRearReleaseMinimumSeparationM))
    {
        g_config.virtual_stock_hybrid_horizontal_full_m =
            kVirtualStockProductHybridHorizontalFullDefaultM;
        g_config.virtual_stock_hybrid_horizontal_release_m =
            kVirtualStockProductHybridHorizontalReleaseDefaultM;
    }
}


// C-TITLE-1: per-title weapon/hand/HUD profiles. One descriptor per
// tunable: its cfg suffix, its slot in a TitleTunables, its live field in
// Config, and its load clamp (the same ranges the shared field gets).
namespace
{
struct TunableField
{
    const char* suffix;
    float TitleTunables::*member;
    float Config::*live;
    float minimum;
    float maximum;
    // The profile the live field follows, and an edit lands in, while no game is running; -1 = the shared
    // defaults.
    int noGameProfile = -1;
};

const TunableField kTunableFields[] = {
    {"game_brightness", &TitleTunables::game_brightness, &Config::game_brightness, 0.5f, 2.0f},
    {"left_hand_mesh_x_m", &TitleTunables::left_hand_mesh_x_m, &Config::left_hand_mesh_x_m, -.20f, .20f},
    {"left_hand_mesh_y_m", &TitleTunables::left_hand_mesh_y_m, &Config::left_hand_mesh_y_m, -.20f, .20f},
    {"left_hand_mesh_z_m", &TitleTunables::left_hand_mesh_z_m, &Config::left_hand_mesh_z_m, -.20f, .20f},
    {"right_hand_mesh_x_m", &TitleTunables::right_hand_mesh_x_m, &Config::right_hand_mesh_x_m, -.20f, .20f},
    {"right_hand_mesh_y_m", &TitleTunables::right_hand_mesh_y_m, &Config::right_hand_mesh_y_m, -.20f, .20f},
    {"right_hand_mesh_z_m", &TitleTunables::right_hand_mesh_z_m, &Config::right_hand_mesh_z_m, -.20f, .20f},

    {"gun_scale", &TitleTunables::gun_scale, &Config::gun_scale, 0.30f, 3.00f},
    {"left_hand_scale", &TitleTunables::left_hand_scale,
     &Config::left_hand_scale, 0.30f, 3.00f},
    {"gun_pitch_deg", &TitleTunables::gun_pitch_deg, &Config::gun_pitch_deg,
     -180.0f, 180.0f},
    {"gun_yaw_deg", &TitleTunables::gun_yaw_deg, &Config::gun_yaw_deg,
     -180.0f, 180.0f},
    {"gun_roll_deg", &TitleTunables::gun_roll_deg, &Config::gun_roll_deg,
     -180.0f, 180.0f},
    {"world_scale", &TitleTunables::world_scale, &Config::world_scale, .165f, .495f},
    {"gun_forward_m", &TitleTunables::gun_forward_m, &Config::gun_forward_m, weapon_alignment::kForwardMinimum, weapon_alignment::kForwardMaximum},
    {"gun_right_m", &TitleTunables::gun_right_m, &Config::gun_right_m, weapon_alignment::kRightMinimum, weapon_alignment::kRightMaximum},
    {"gun_up_m", &TitleTunables::gun_up_m, &Config::gun_up_m, weapon_alignment::kUpMinimum, weapon_alignment::kUpMaximum},
    {"left_gun_forward_m", &TitleTunables::left_gun_forward_m, &Config::left_gun_forward_m, weapon_alignment::kForwardMinimum, weapon_alignment::kForwardMaximum},
    {"left_gun_right_m", &TitleTunables::left_gun_right_m, &Config::left_gun_right_m, weapon_alignment::kRightMinimum, weapon_alignment::kRightMaximum},
    {"left_gun_up_m", &TitleTunables::left_gun_up_m, &Config::left_gun_up_m, weapon_alignment::kUpMinimum, weapon_alignment::kUpMaximum},
    {"left_hand_forward_m", &TitleTunables::left_hand_forward_m,
     &Config::left_hand_forward_m, -0.15f, 0.30f},
    {"hud_size", &TitleTunables::hud_size, &Config::hud_size, 0.30f, 1.00f, kTitleProfileReach},
    {"hud_aspect", &TitleTunables::hud_aspect, &Config::hud_aspect,
     kHudAspectMin, kHudAspectMax, kTitleProfileReach},
    {"hud_curvature", &TitleTunables::hud_curvature, &Config::hud_curvature,
     kHudCurvatureMin, kHudCurvatureMax, kTitleProfileReach},
    {"hud_vertical_offset", &TitleTunables::hud_vertical_offset,
     &Config::hud_vertical_offset, kHudHeightMin, kHudHeightMax, kTitleProfileReach},
    {"barrel_pitch_deg", &TitleTunables::barrel_pitch_deg,
     &Config::barrel_pitch_deg, -180.0f, 180.0f},
    {"barrel_yaw_deg", &TitleTunables::barrel_yaw_deg,
     &Config::barrel_yaw_deg, -180.0f, 180.0f},
    {"barrel_roll_deg", &TitleTunables::barrel_roll_deg,
     &Config::barrel_roll_deg, -180.0f, 180.0f},
    {"crosshair_size_deg", &TitleTunables::crosshair_size_deg,
     &Config::crosshair_size_deg, 0.3f, 20.0f, kTitleProfileReach},
    {"crosshair_opacity", &TitleTunables::crosshair_opacity,
     &Config::crosshair_opacity, 0.0f, 2.0f, kTitleProfileReach},
    {"crosshair_brightness", &TitleTunables::crosshair_brightness,
     &Config::crosshair_brightness, 0.0f, 2.0f, kTitleProfileReach},
};
constexpr int kTunableFieldCount =
    static_cast<int>(sizeof(kTunableFields) / sizeof(kTunableFields[0]));

const char* const kTitleProfilePrefixes[kTitleProfileCount] = {
    "halo3_", "odst_", "reach_", "halo4_", "halo2a_", "halo2c_", "halo1_"};
const char* const kTitleProfileTitles[kTitleProfileCount] = {
    "Halo 3", "Halo 3: ODST", "Halo: Reach", "Halo 4",
    "Halo 2 Anniversary", "Halo 2 Classic",
    "Halo: Combat Evolved"};

bool g_profileFieldSet[kTitleProfileCount][kTunableFieldCount]{};
std::atomic<int> g_activeTitleProfile{-1};

// What a profile field the cfg did not name starts from: the shared value, but for a crosshair size or opacity,
// or a HUD layout value, of a game whose own start is not the shared default (kCrosshairStarts, kHudStarts),
// which starts there whatever the shared key holds.
float ProfileSeed(int profile, const TunableField& descriptor, float shared)
{
    // Each game's calibrated gun position. A shared value the player moved away from the old single
    // default (-0.14 / 0 / 0 m) keeps that adjustment on top of every game's baseline.
    const auto gun=weapon_alignment::Baseline(profile);
    const TitleTunables legacy{};
    if (descriptor.member == &TitleTunables::gun_forward_m) return gun.forward+(shared-legacy.gun_forward_m);
    if (descriptor.member == &TitleTunables::gun_right_m) return gun.right+(shared-legacy.gun_right_m);
    if (descriptor.member == &TitleTunables::gun_up_m) return gun.up+(shared-legacy.gun_up_m);
    const auto left=weapon_alignment::HasProfile(profile)?weapon_alignment::kLeftOffsets[profile]:weapon_alignment::Offsets{-.16f,.04f,-.06f};
    if (descriptor.member == &TitleTunables::left_gun_forward_m) return left.forward;
    if (descriptor.member == &TitleTunables::left_gun_right_m) return left.right;
    if (descriptor.member == &TitleTunables::left_gun_up_m) return left.up;
    const TitleTunables defaults{};
    const CrosshairStart& own = kCrosshairStarts[profile];
    if (descriptor.member == &TitleTunables::crosshair_size_deg && own.size_deg != defaults.crosshair_size_deg)
        return own.size_deg;
    if (descriptor.member == &TitleTunables::crosshair_opacity && own.opacity != defaults.crosshair_opacity)
        return own.opacity;
    const HudStart& hud = kHudStarts[profile];
    if (descriptor.member == &TitleTunables::hud_size && hud.size != defaults.hud_size)
        return hud.size;
    if (descriptor.member == &TitleTunables::hud_aspect && hud.aspect != defaults.hud_aspect)
        return hud.aspect;
    if (descriptor.member == &TitleTunables::hud_curvature && hud.curvature != defaults.hud_curvature)
        return hud.curvature;
    if (descriptor.member == &TitleTunables::hud_vertical_offset && hud.vertical_offset != defaults.hud_vertical_offset)
        return hud.vertical_offset;
    return shared;
}

// Where a field's live value comes from, and where an edit of it goes, for the active profile (-1 = no game).
TitleTunables& ProfileFor(int active, const TunableField& descriptor)
{
    if (active >= 0 && active < kTitleProfileCount)
        return g_config.title_profiles[active];
    if (descriptor.noGameProfile >= 0 && descriptor.noGameProfile < kTitleProfileCount)
        return g_config.title_profiles[descriptor.noGameProfile];
    return g_config.base_tunables;
}
#include "weapon_alignment_config.inl"

void ResetTitleProfileTracking()
{
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
        for (int field = 0; field < kTunableFieldCount; ++field)
            g_profileFieldSet[profile][field] = false;
}

// A halo3_gun_scale-style key. Returns true when consumed.
bool ParseTitleProfileKey(const char* key, const char* val)
{
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
    {
        const char* prefix = kTitleProfilePrefixes[profile];
        const size_t length = strlen(prefix);
        if (strncmp(key, prefix, length) != 0)
            continue;
        const char* suffix = key + length;
        for (int field = 0; field < kTunableFieldCount; ++field)
        {
            if (strcmp(suffix, kTunableFields[field].suffix) != 0)
                continue;
            ParseFloatSetting(
                key, val,
                g_config.title_profiles[profile].*kTunableFields[field].member);
            g_profileFieldSet[profile][field] = true;
            return true;
        }
    }
    return false;
}

// After the shared fields are parsed and clamped: snapshot them as the
// shared defaults, fill every profile field the cfg did not name from those
// defaults, clamp every profile field with the same range its shared field
// gets, and re-assert the active profile if one is live (a reload during
// gameplay must not leave stale values in the live fields).
void ResolveTitleProfiles()
{
    for (int field = 0; field < kTunableFieldCount; ++field)
        g_config.base_tunables.*kTunableFields[field].member =
            g_config.*kTunableFields[field].live;
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
    {
        for (int field = 0; field < kTunableFieldCount; ++field)
        {
            const TunableField& descriptor = kTunableFields[field];
            float& value = g_config.title_profiles[profile].*descriptor.member;
            if (!g_profileFieldSet[profile][field] || !std::isfinite(value))
                value = ProfileSeed(profile, descriptor, g_config.base_tunables.*descriptor.member);
            value = std::clamp(value, descriptor.minimum, descriptor.maximum);
        }
    }
    // The live fields follow the active profile; with no game running, a field with a profile of its own for
    // that case follows it, and the rest keep the shared defaults just parsed into them.
    const int active = g_activeTitleProfile.load(std::memory_order_acquire);
    for (int field = 0; field < kTunableFieldCount; ++field)
    {
        const TunableField& descriptor = kTunableFields[field];
        const TitleTunables& source = ProfileFor(active, descriptor);
        if (&source != &g_config.base_tunables)
            g_config.*descriptor.live = source.*descriptor.member;
    }
}
} // namespace

const char* Config_TitleProfileName(int profile)
{
    if (profile >= 0 && profile < kTitleProfileCount)
        return kTitleProfileTitles[profile];
    return "shared defaults (no game active)";
}

int Config_ActiveTitleProfile()
{
    return g_activeTitleProfile.load(std::memory_order_acquire);
}

int Config_ShownTitleProfile()
{
    const int active = g_activeTitleProfile.load(std::memory_order_acquire);
    return active >= 0 && active < kTitleProfileCount ? active : kTitleProfileReach;
}

void Config_ApplyTitleProfile(int profile)
{
    const std::lock_guard lock(g_profileMutex);
    if (profile < -1 || profile >= kTitleProfileCount)
        profile = -1;
    if(g_activeTitleProfile.load(std::memory_order_acquire)!=profile)
    {
        RestoreWeaponFallback();
        g_observedWeaponIdentity=0;
    }
    const int previous =
        g_activeTitleProfile.exchange(profile, std::memory_order_acq_rel);
    if (previous == profile)
        return;
    for (int field = 0; field < kTunableFieldCount; ++field)
        g_config.*kTunableFields[field].live =
            ProfileFor(profile, kTunableFields[field]).*kTunableFields[field].member;
    LOG("config: weapon and hand tunables now follow %s; the crosshair and the HUD layout follow %s",
        Config_TitleProfileName(profile), Config_TitleProfileName(Config_ShownTitleProfile()));
}

void Config_StoreLiveTunables()
{
    const std::lock_guard lock(g_profileMutex);
    StoreWeaponAlignment();
    const int active = g_activeTitleProfile.load(std::memory_order_acquire);
    for (int field = 0; field < kTunableFieldCount; ++field)
    {
        const TunableField& descriptor = kTunableFields[field];
        if(g_activeWeaponProfile>=0&&WeaponAlignmentFieldIndex(descriptor.live)>=0) continue;
        ProfileFor(active, descriptor).*descriptor.member = std::clamp(
            g_config.*descriptor.live, descriptor.minimum, descriptor.maximum);
    }
}

void Config_RefreshLiveTunables()
{
    const std::lock_guard lock(g_profileMutex);
    const int active = g_activeTitleProfile.load(std::memory_order_acquire);
    for (int field = 0; field < kTunableFieldCount; ++field)
    {
        const TunableField& descriptor = kTunableFields[field];
        if(g_activeWeaponProfile>=0&&WeaponAlignmentFieldIndex(descriptor.live)>=0) continue;
        g_config.*descriptor.live = ProfileFor(active, descriptor).*descriptor.member;
    }
}

const char* Config_ActiveWeaponProfileName()
{ return g_activeWeaponName.load(std::memory_order_acquire); }

void Config_RefreshWeaponProfile()
{
    const std::lock_guard lock(g_profileMutex);
    Config_ApplyWeaponProfile(g_observedWeaponIdentity);
}

void Config_ApplyWeaponProfile(uint64_t identity)
{
    const std::lock_guard lock(g_profileMutex);
    g_observedWeaponIdentity=identity;
    const int title=Config_ActiveTitleProfile();
    const auto* model=g_config.per_gun_alignment?AlignmentModel(title,identity):nullptr;
    if(g_activeWeaponProfile>=0)
    {
        const auto& active=g_weaponProfiles[g_activeWeaponProfile];
        if(model&&active.title==title&&active.identity==identity) return;
    }
    RestoreWeaponFallback();
    if(!model) return;
    // Preserve pending edits to the title fallback before the first gun overlay.
    Config_StoreLiveTunables();
    for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
        g_weaponFallback[i]=g_config.*kWeaponAlignmentFields[i].live;
    auto found=std::find_if(g_weaponProfiles.begin(),g_weaponProfiles.end(),
        [&](const auto& p){return p.title==title&&p.identity==identity;});
    if(found==g_weaponProfiles.end())
    {
        g_weaponProfiles.push_back({title,identity});
        found=std::prev(g_weaponProfiles.end());
    }
    g_activeWeaponProfile=static_cast<int>(found-g_weaponProfiles.begin());
    for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
        if(found->set[i]) g_config.*kWeaponAlignmentFields[i].live=found->values[i];
    g_activeWeaponName.store(model->name,std::memory_order_release);
}

void ConfigLoad(const wchar_t* path)
{
    const std::lock_guard lock(g_profileMutex);
    g_weaponProfiles.clear();g_activeWeaponProfile=-1;g_observedWeaponIdentity=0;
    g_activeWeaponName.store(nullptr,std::memory_order_release);
    ResetTitleProfileTracking();
    g_config = Config{};
    g_path = path;
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"rt");
    if (!f)
    {
        LOG("config: no file yet, writing defaults");
        // A first run takes the same derived aim modes a loaded file does (each lane reads its own field),
        // and the same profiles: every game's starts from the shared defaults, and the live fields follow the
        // profile they belong to before the save stores them back.
        Config_ApplyAimModes();
        ResolveTitleProfiles();
        // A new player starts on the Default controls, not the struct's Legacy Default.
        default_controls::Apply(g_config);
        strcpy_s(g_config.input_preset, "Default");
        ConfigSave();
        return;
    }
    bool reachNumericKeyLoaded[kReachVehicleTrimSlots] = {};
    char line[512];
    int loadedConfigVersion = 1;
    bool loadedLegacyCurvature = false;
    bool loadedScopeZoom = false;
    bool loadedHolsterRadius = false;
    bool flashlightMappingV2 = false;
    bool loadedInputPreset = false;
    int inputsVersion = 0; // vr_inputs_version: 0, a file from before the rows held inputs
    bool leftHandForwardResetV1 = false;
    bool halo4HudHeightV2 = false;   // halo4_hud_height_version >= 2: Halo 4's HUD height is already up = positive
    bool loadedSharedLeftHandForward = false;
    bool twoHandZoneBaselineV1 = false;
    bool loadedTwoHandZoneRight = false;
    bool aimDirectDriveSeen = false;
    bool coopSafeShotsV2 = false;   // coop_safe_shots_version >= 2: a 1 in this file is a chosen "always"
    bool loadedLegacyVirtualStockStrength = false;
    bool loadedStandardVirtualStockStrength = false;
    bool loadedPlusVirtualStockStrength = false;
    float legacyVirtualStockStrength = kVirtualStockPlusStrengthDefault;
    bool loadedTwoHandSmoothingStrength = false;
    bool loadedLegacyTwoHandSmoothing = false;
    float legacyTwoHandSmoothingStrength = kTwoHandSmoothingStrengthDefault;
    while (fgets(line, sizeof(line), f))
    {
        if (char* hash = strchr(line, '#'))
            *hash = 0;
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        auto trim = [](char* s) -> char* {
            while (isspace((unsigned char)*s)) s++;
            char* e = s + strlen(s);
            while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
            return s;
        };
        const char* key = trim(line);
        const char* val = trim(eq + 1);
        // C-TITLE-1: per-title profile keys, handled by table.
        if(ParseVehicleModelTrim(key,val))continue;
        bool vehicleGameKey=false;
        const char* vehicleAxes[]{"forward","up","right"};
        for(unsigned title=0;title<6&&!vehicleGameKey;++title)for(unsigned axis=0;axis<3;++axis)
        {
            char expected[80]{};
            sprintf_s(expected,"vehicle_game_%s_%s_m",weapon_interaction::kTitleKeys[title],vehicleAxes[axis]);
            if(strcmp(key,expected))continue;
            if(ParseFloatSetting(key,val,g_config.vehicle_cam_game[title][axis]))
                g_config.vehicle_cam_game_set[title][axis]=true;
            vehicleGameKey=true;break;
        }
        if(vehicleGameKey)continue;
        if (!strcmp(key,"world_scale")) {ParseFloatSetting(key,val,g_config.world_scale);continue;}
        // Legacy key, no longer read: bloom_chosen_<game> records the player's own bloom choice.
        if (!strcmp(key,"bloom_defaults_version")) continue;
        if (!strcmp(key,"left_hand_forward_reset_version")) {leftHandForwardResetV1=atoi(val)>=1;continue;}
        if (!strcmp(key,"halo4_hud_height_version")) {halo4HudHeightV2=atoi(val)>=2;continue;}
        if (!strcmp(key,"left_hand_forward_m")) loadedSharedLeftHandForward=true;
        if (!strcmp(key,"two_hand_zone_baseline_version")) {twoHandZoneBaselineV1=atoi(val)>=1;continue;}
        if (!strcmp(key,"two_hand_zone_right_m")) loadedTwoHandZoneRight=true;
        if (ParseWeaponAlignmentKey(key,val)||ParseTitleProfileKey(key, val))
            continue;
        if(!strcmp(key,"vr_inputs_version"))
        {
            inputsVersion=atoi(val);
            // From version 3 a file names only the rows a player changed: the rest are the default controls' rows
            // (default_controls::Rows), so a later change to those reaches them. The version comes before the rows.
            if(inputsVersion>=3)
                for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
                    default_controls::Rows(static_cast<GameTitle>(title+1),g_config.vr_bindings[title],
                        g_config.vr_inputs[title],g_config.vr_extra_bindings[title]);
            continue;
        }
        // A row's inputs (vr_mapping::ParseInput): its own (the key alone, which may be Automatic) and further ones (_2
        // on), and the extra rows' (their own may be Automatic too).
        bool vrBindingKey=false;
        for(unsigned title=0;title<weapon_interaction::kTitleCount&&!vrBindingKey;++title)
            for(unsigned action=0;action<vr_mapping::Count+vr_mapping::ExtraCount&&!vrBindingKey;++action)
            {
                const bool seat=action>=vr_mapping::Count;
                const char* row=seat?vr_mapping::kExtraKeys[action-vr_mapping::Count]:vr_mapping::kKeys[action];
                for(unsigned slot=0;slot<vr_mapping::kRowInputs&&!vrBindingKey;++slot)
                {
                    char bindingKey[96]{};
                    sprintf_s(bindingKey,"vr_bind_%s_%s%s",weapon_interaction::kTitleKeys[title],row,
                        vr_mapping::kControlSuffix[slot]);
                    if(strcmp(key,bindingKey)) continue;
                    vrBindingKey=true;
                    bool read=false;
                    if(seat)
                    {
                        auto& input=g_config.vr_extra_bindings[title][action-vr_mapping::Count][slot];
                        read=vr_mapping::ParseExtraInput(val,input,slot==0);
                    }
                    else if(slot==0)
                    {
                        auto& inputs=g_config.vr_inputs[title][action];
                        read=vr_mapping::ParseInput(val,inputs.held,g_config.vr_bindings[title][action],inputs.mode,true);
                    }
                    else
                    {
                        auto& input=g_config.vr_inputs[title][action].more[slot-1];
                        read=vr_mapping::ParseInput(val,input.held,input.press,input.mode,false);
                    }
                    if(!read) LOG("config: invalid VR binding '%s' ignored",key);
                }
            }
        // The mod's own rows, every game's: vr_bind_pause and _2 on.
        for(unsigned action=0;action<vr_mapping::ModCount&&!vrBindingKey;++action)
            for(unsigned slot=0;slot<vr_mapping::kRowInputs&&!vrBindingKey;++slot)
            {
                char bindingKey[96]{};
                sprintf_s(bindingKey,"vr_bind_%s%s",vr_mapping::kModKeys[action],vr_mapping::kControlSuffix[slot]);
                if(strcmp(key,bindingKey)) continue;
                vrBindingKey=true;
                auto& input=g_config.vr_mod_bindings[action][slot];
                if(!vr_mapping::ParseInput(val,input.held,input.press,input.mode,false))
                    LOG("config: invalid VR binding '%s' ignored",key);
            }
        if(vrBindingKey) continue;
        if(!strcmp(key,"experimental_body_ik")) {g_config.experimental_body_ik=atoi(val)!=0;continue;}
        if(!strcmp(key,"body_ik_hide_lower")) {g_config.body_ik_hide_lower=atoi(val)!=0;continue;}
        if(!strcmp(key,"physical_running")) {g_config.physical_running=atoi(val)!=0;continue;}
        if(!strcmp(key,"physical_running_speed")) {
            ParseFloatSetting(key,val,g_config.physical_running_speed);
            g_config.physical_running_speed=std::clamp(g_config.physical_running_speed,.1f,1.f);continue;
        }
        if(!strcmp(key,"physical_running_sensitivity")) {
            ParseFloatSetting(key,val,g_config.physical_running_sensitivity);
            g_config.physical_running_sensitivity=std::clamp(g_config.physical_running_sensitivity,.25f,3.f);continue;
        }
        if(!strcmp(key,"physical_crouch")) {g_config.physical_crouch=atoi(val)!=0;continue;}
#if HALOMCCVR_DEV_DIAGNOSTICS   // test hook for a simulated runtime: a player build never reads it
        if(!strcmp(key,"test_accept_untracked_head")) {g_config.test_accept_untracked_head=atoi(val)!=0;continue;}
#endif
        if(!strcmp(key,"head_leash_below_eye_m")) {g_config.head_leash_below_eye_m=static_cast<float>(atof(val));continue;}
        if(!strcmp(key,"view_follows_standing_height")) {g_config.view_follows_standing_height=atoi(val)!=0;continue;}
        if(!strcmp(key,"physical_crouch_depth_m"))
        {
            ParseFloatSetting(key,val,g_config.physical_crouch_depth_m);
            g_config.physical_crouch_depth_m=std::clamp(g_config.physical_crouch_depth_m,.08f,.65f);
            continue;
        }
        bool muzzleFlashKey=false;
        for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        {
            char settingKey[80]{};
            sprintf_s(settingKey,"hide_muzzle_flash_%s",weapon_interaction::kTitleKeys[title]);
            if(strcmp(key,settingKey)) continue;
            char* end=nullptr;const long value=strtol(val,&end,10);
            if(end!=val&&end&&*end==0&&(value==0||value==1)) g_config.hide_muzzle_flash[title]=value!=0;
            else LOG("config: invalid muzzle-flash setting '%s' ignored",key);
            muzzleFlashKey=true;break;
        }
        if(muzzleFlashKey) continue;
        if(!strcmp(key,"disable_autoexposure_odst")) {
            char* end=nullptr;const long value=strtol(val,&end,10);
            if(end!=val&&end&&*end==0&&(value==0||value==1))g_config.disable_autoexposure_odst=value!=0;
            else LOG("config: invalid autoexposure setting '%s' ignored",key);
            continue;
        }
        bool bloomKey=false;
        for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        {
            char settingKey[80]{};
            sprintf_s(settingKey,"bloom_enabled_%s",weapon_interaction::kTitleKeys[title]);
            if(strcmp(key,settingKey)) continue;
            char* end=nullptr;const long value=strtol(val,&end,10);
            if(end!=val&&end&&*end==0&&(value==0||value==1)) g_config.bloom_enabled[title]=value!=0;
            else LOG("config: invalid bloom setting '%s' ignored",key);
            bloomKey=true;break;
        }
        if(bloomKey) continue;
        bool bloomChoiceKey=false;
        for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        {
            char settingKey[80]{};
            sprintf_s(settingKey,"bloom_chosen_%s",weapon_interaction::kTitleKeys[title]);
            if(strcmp(key,settingKey)) continue;
            g_config.bloom_chosen[title]=atoi(val)!=0;
            bloomChoiceKey=true;break;
        }
        if(bloomChoiceKey) continue;
        if(!strcmp(key,"vr_action_mapping")) {g_config.vr_action_mapping=atoi(val)!=0;continue;}
        if(!strcmp(key,"gesture_melee_speed")) {ParseFloatSetting(key,val,g_config.gesture_melee_speed);continue;}
        if(!strcmp(key,"gesture_melee_extension")) {ParseFloatSetting(key,val,g_config.gesture_melee_extension);continue;}
        if(!strcmp(key,"gesture_melee_travel")) {ParseFloatSetting(key,val,g_config.gesture_melee_travel);continue;}
        if(!strcmp(key,"gesture_melee_reach")) {ParseFloatSetting(key,val,g_config.gesture_melee_reach);continue;}
        if(!strcmp(key,"gesture_melee_require_grip")) {ParseBoolSetting(key,val,g_config.gesture_melee_require_grip);continue;}
        if(!strcmp(key,"gesture_melee_off_hand")) {ParseBoolSetting(key,val,g_config.gesture_melee_off_hand);continue;}
        if(!strcmp(key,"gesture_melee_directional")) {ParseBoolSetting(key,val,g_config.gesture_melee_directional);continue;}
        if(!strcmp(key,"gesture_melee_smoothed_hands")) {ParseBoolSetting(key,val,g_config.gesture_melee_smoothed_hands);continue;}
        if(!strcmp(key,"gesture_melee_log")) {ParseBoolSetting(key,val,g_config.gesture_melee_log);continue;}
        if(!strcmp(key,"gesture_melee_aim_hold_ms")) {g_config.gesture_melee_aim_hold_ms=atoi(val);continue;}
        if(!strcmp(key,"gesture_melee_aim_ramp_ms")) {g_config.gesture_melee_aim_ramp_ms=atoi(val);continue;}
        if(!strcmp(key,"gesture_melee_press_ms")) {g_config.gesture_melee_press_ms=atoi(val);continue;}
        if(!strcmp(key,"gesture_melee_cooldown_ms")) {g_config.gesture_melee_cooldown_ms=atoi(val);continue;}
        if(!strcmp(key,"gesture_melee_holster_veto_ms")) {g_config.gesture_melee_holster_veto_ms=atoi(val);continue;}
        if(!strcmp(key,"gesture_melee_smoothing_ms")) {ParseFloatSetting(key,val,g_config.gesture_melee_smoothing_ms);continue;}
        if(!strcmp(key,"gesture_melee_max_speed")) {ParseFloatSetting(key,val,g_config.gesture_melee_max_speed);continue;}
        if(!strcmp(key,"gesture_melee_max_reach")) {ParseFloatSetting(key,val,g_config.gesture_melee_max_reach);continue;}
        if(!strcmp(key,"input_preset")) {strncpy_s(g_config.input_preset,val,_TRUNCATE);loadedInputPreset=true;continue;}
        if(!strcmp(key,"trigger_air_equipment")) {g_config.trigger_air_equipment=atoi(val)!=0;continue;}
        if(!strcmp(key,"grip_contextual_trigger")) {g_config.grip_contextual_trigger=atoi(val)!=0;continue;}
        if(!strcmp(key,"right_stick_crouch")) {g_config.right_stick_crouch=atoi(val)!=0;continue;}
        if(!strcmp(key,"flashlight_suppress_on_two_hand")) {g_config.flashlight_suppress_on_two_hand=atoi(val)!=0;continue;}
        if(!strcmp(key,"upscaler")) {g_config.upscaler=atoi(val)==1?1:0;continue;}
        if(!strcmp(key,"dlss_mode")) {g_config.dlss_mode=std::clamp(atoi(val),0,4);continue;}
        if(!strcmp(key,"dlss_jitter")) {g_config.dlss_jitter=atoi(val)!=0;continue;}
        if(!strcmp(key,"dlss_preset")) {g_config.dlss_preset=std::clamp(atoi(val),0,dlss::kPresetMax);continue;}
        if(!strcmp(key,"dlss_debug_view")) {g_config.dlss_debug_view=atoi(val)!=0;continue;}
        if(!strcmp(key,"vr_gameplay_subtitles")) {g_config.vr_gameplay_subtitles=atoi(val)!=0;continue;}
        if(!strcmp(key,"vr_gameplay_subtitle_anchor")) {g_config.vr_gameplay_subtitle_anchor=atoi(val)==1?1:0;continue;}
        if(!strcmp(key,"vr_theatre_subtitle_anchor")) {g_config.vr_theatre_subtitle_anchor=atoi(val)==1?1:0;continue;}
        struct SubtitleField {const char* key;float* value;float min,max;};
        const SubtitleField subtitleFields[]{
            {"vr_gameplay_subtitle_scale",&g_config.vr_gameplay_subtitle_scale,.5f,2.5f},
            {"vr_gameplay_subtitle_x",&g_config.vr_gameplay_subtitle_x,-1.f,1.f},
            {"vr_gameplay_subtitle_y",&g_config.vr_gameplay_subtitle_y,-1.f,1.f},
            {"vr_theatre_subtitle_scale",&g_config.vr_theatre_subtitle_scale,.5f,2.5f},
            {"vr_theatre_subtitle_x",&g_config.vr_theatre_subtitle_x,-1.f,1.f},
            {"vr_theatre_subtitle_y",&g_config.vr_theatre_subtitle_y,-1.f,1.f}};
        bool subtitleKey=false;
        for(const auto& field:subtitleFields) if(!strcmp(key,field.key))
        {
            float value=*field.value;
            if(ParseFloatSetting(key,val,value)) *field.value=std::clamp(value,field.min,field.max);
            subtitleKey=true;break;
        }
        if(subtitleKey) continue;
        if(!strcmp(key,"virtual_stock")) {g_config.virtual_stock=atoi(val)!=0;continue;}
        if(!strcmp(key,"virtual_stock_standard_strength"))
        {
            loadedStandardVirtualStockStrength = ParseFloatSetting(
                key, val, g_config.virtual_stock_standard_strength);
            continue;
        }
        if(!strcmp(key,"virtual_stock_plus_strength"))
        {
            loadedPlusVirtualStockStrength = ParseFloatSetting(
                key, val, g_config.virtual_stock_plus_strength);
            continue;
        }
        if(!strcmp(key,"virtual_stock_strength"))
        {
            loadedLegacyVirtualStockStrength = ParseFloatSetting(
                key, val, legacyVirtualStockStrength);
            continue;
        }
        if(!strcmp(key,"virtual_stock_rear_reference"))
        {
            ParseIntSetting(key, val, kVirtualStockRearReferenceMinimum,
                kVirtualStockRearReferenceMaximum,
                g_config.virtual_stock_rear_reference);
            continue;
        }
        if(!strcmp(key,"virtual_stock_hybrid_ads_reference"))
        {
            ParseIntSetting(key, val,
                kVirtualStockHybridAdsReferenceMinimum,
                kVirtualStockHybridAdsReferenceMaximum,
                g_config.virtual_stock_hybrid_ads_reference);
            continue;
        }
        if(!strcmp(key,"virtual_stock_proximity_release")) {g_config.virtual_stock_proximity_release=atoi(val)!=0;continue;}
        if(!strcmp(key,"virtual_stock_hybrid_horizontal_release"))
        {
            ParseBoolSetting(key, val,
                g_config.virtual_stock_hybrid_horizontal_release);
            continue;
        }
        if(!strcmp(key,"two_hand_support_grip_pose"))
        {
            ParseBoolSetting(key, val, g_config.two_hand_support_grip_pose);
            continue;
        }
        if(!strcmp(key,"persistent_support_grip"))
        {
            ParseBoolSetting(key, val, g_config.persistent_support_grip);
            continue;
        }
        if(!strcmp(key,"two_hand_smoothing_strength"))
        {
            loadedTwoHandSmoothingStrength = ParseFloatSetting(
                key, val, g_config.two_hand_smoothing_strength);
            continue;
        }
        if(!strcmp(key,"two_hand_offhand_influence"))
        {
            ParseFloatSetting(key, val, g_config.two_hand_offhand_influence);
            continue;
        }
        if(!strcmp(key,"two_hand_smoothing"))
        {
            // Previous-candidate boolean key. It is only a migration source and
            // is not re-saved; true means the old full-strength speed-25
            // filter, never slider value 1.
            bool enabled = false;
            if(ParseBoolSetting(key, val, enabled))
            {
                loadedLegacyTwoHandSmoothing = true;
                legacyTwoHandSmoothingStrength = enabled
                    ? kTwoHandSmoothingStrengthMaximum
                    : kTwoHandSmoothingStrengthMinimum;
            }
            continue;
        }
        if(!strcmp(key,"two_hand_transition_smoothing"))
        {
            // Retired 2026-09-29: the 200 ms VS-OFF acquire/release continuity
            // is fixed-on product behaviour (TwoHandTransitionContinuityEnabled()
            // in virtual_stock_settings.h). This legacy key is still parsed so
            // historical files load quietly, but the value is dormant and is
            // never resolved by any product or telemetry path.
            ParseBoolSetting(key, val, g_config.two_hand_transition_smoothing);
            continue;
        }
        if(!strcmp(key,"two_hand_one_hand_range_m"))
        {
            ParseFloatSetting(key, val, g_config.two_hand_one_hand_range_m);
            continue;
        }
        if(!strcmp(key,"two_hand_grip_zone_authority"))
        {
            ParseBoolSetting(key, val, g_config.two_hand_grip_zone_authority);
            continue;
        }
        if(!strcmp(key,"two_hand_switch_inherit"))
        {
            ParseBoolSetting(key, val, g_config.two_hand_switch_inherit);
            continue;
        }
        // Keep new keys outside the already-at-limit legacy else-if chain.
        bool weaponButtonKey = false;
        for (unsigned i=0;i<weapon_interaction::kTitleCount;++i)
        {
            char reloadKey[64]{},switchKey[64]{},flashlightKey[64]{};
            sprintf_s(reloadKey,"weapon_reload_button_%s",weapon_interaction::kTitleKeys[i]);
            sprintf_s(switchKey,"weapon_switch_button_%s",weapon_interaction::kTitleKeys[i]);
            sprintf_s(flashlightKey,"flashlight_button_%s",weapon_interaction::kTitleKeys[i]);
            const bool flashlightKeyMatch=!strcmp(key,flashlightKey);
            int* setting=!strcmp(key,reloadKey)?&g_config.weapon_reload_button[i]:
                !strcmp(key,switchKey)?&g_config.weapon_switch_button[i]:
                flashlightKeyMatch?&g_config.flashlight_button[i]:nullptr;
            if (!setting) continue;
            char* end=nullptr;
            const long value=strtol(val,&end,10);
            if (end!=val&&end&&*end==0&&value>=0&&value<(flashlightKeyMatch?flashlight_input::kCount:8)) *setting=static_cast<int>(value);
            else LOG("config: invalid mapped controller button '%s' ignored",key);
            weaponButtonKey=true;
            break;
        }
        if (weaponButtonKey) continue;
        if (!strcmp(key,"vehicle_smooth_turn")) {g_config.vehicle_smooth_turn=atoi(val)!=0;continue;}
        if (!strcmp(key,"ce_anniversary_disable_lens_flares")) {g_config.ce_anniversary_disable_lens_flares=atoi(val)!=0;continue;}
        if (!strcmp(key,"flashlight_mapping_version")) {flashlightMappingV2=atoi(val)>=2;continue;}
        if (!strcmp(key,"disable_flashlight_input")) {g_config.disable_flashlight_input=atoi(val)!=0;continue;}
        if (!strcmp(key,"per_gun_alignment")) { g_config.per_gun_alignment=atoi(val)!=0;continue; }
        if (!strcmp(key,"manual_reload")) { g_config.manual_reload=atoi(val)!=0;continue; }
        if (!strcmp(key,"manual_reload_disable_auto")) { g_config.manual_reload_disable_auto=atoi(val)!=0;continue; }
        if (!strcmp(key,"manual_reload_skip_animations")) { g_config.manual_reload_skip_animations=atoi(val)!=0;continue; }
        if (!strcmp(key,"manual_reload_shortened_animation")) { g_config.manual_reload_shortened_animation=atoi(val)!=0;continue; }
        if (!strcmp(key,"weapon_holsters")) { g_config.weapon_holsters=atoi(val)!=0;continue; }
        if (!strcmp(key,"weapon_holster_slide")) { g_config.weapon_holster_slide=atoi(val)!=0;continue; }
        if (!strcmp(key,"weapon_holster_click")) { g_config.weapon_holster_click=atoi(val)!=0;continue; }
        if (!strcmp(key,"gesture_only_weapon_input")) { g_config.gesture_only_weapon_input=atoi(val)!=0;continue; }
        if (!strcmp(key,"dual_wield_shake_reload")) { g_config.dual_wield_shake_reload=atoi(val)!=0;continue; }
        if (!strcmp(key,"weapon_needler_shake")) { g_config.weapon_needler_shake=atoi(val)!=0;continue; }
        if (!strcmp(key,"weapon_unknown_reload_visual")) { g_config.weapon_unknown_reload_visual=atoi(val)!=0;continue; }
        if (!strcmp(key,"weapon_shake_travel_m"))
        {
            ParseFloatSetting(key,val,g_config.weapon_shake_travel_m);
            g_config.weapon_shake_travel_m=std::clamp(g_config.weapon_shake_travel_m,0.06f,0.20f);
            continue;
        }
        if (!strcmp(key,"weapon_holster_radius_m"))
        {
            ParseFloatSetting(key,val,g_config.weapon_holster_radius_m);
            g_config.weapon_holster_radius_m=std::clamp(g_config.weapon_holster_radius_m,0.08f,0.40f);
            loadedHolsterRadius=true;continue;
        }
        if (!strcmp(key,"weapon_insert_radius_m"))
        {
            ParseFloatSetting(key,val,g_config.weapon_insert_radius_m);
            g_config.weapon_insert_radius_m=std::clamp(g_config.weapon_insert_radius_m,0.06f,0.30f);
            continue;
        }
        if (!strcmp(key,"weapon_holster_draw_m"))
        {
            ParseFloatSetting(key,val,g_config.weapon_holster_draw_m);
            g_config.weapon_holster_draw_m=std::clamp(g_config.weapon_holster_draw_m,0.10f,0.50f);
            continue;
        }
        if (!strcmp(key,"weapon_pouch_down_m"))
        {
            ParseFloatSetting(key,val,g_config.weapon_pouch_down_m);
            g_config.weapon_pouch_down_m=std::clamp(g_config.weapon_pouch_down_m,0.25f,0.85f);
            continue;
        }
        if(!strcmp(key,"weapon_pouch_location"))
        {g_config.weapon_pouch_location=atoi(val)==1?1:0;continue;}
        float* pouchOffset=!strcmp(key,"weapon_pouch_offset_x_m")?&g_config.weapon_pouch_offset_x_m:
            !strcmp(key,"weapon_pouch_offset_y_m")?&g_config.weapon_pouch_offset_y_m:
            !strcmp(key,"weapon_pouch_offset_z_m")?&g_config.weapon_pouch_offset_z_m:nullptr;
        if(pouchOffset)
        {ParseFloatSetting(key,val,*pouchOffset);*pouchOffset=std::clamp(*pouchOffset,-.40f,.40f);continue;}
        if (!strcmp(key,"weapon_body_zone_radius_m"))
        {
            ParseFloatSetting(key,val,g_config.weapon_body_zone_radius_m);
            g_config.weapon_body_zone_radius_m=std::clamp(g_config.weapon_body_zone_radius_m,0.08f,0.40f);
            continue;
        }
        if (!strcmp(key,"weapon_holster_location"))
        { const int location=atoi(val);g_config.weapon_holster_location=location>=1&&location<=3?location:0;continue; }
        float* holsterCustom=!strcmp(key,"weapon_holster_custom_x_m")?&g_config.weapon_holster_custom_x_m:
            !strcmp(key,"weapon_holster_custom_y_m")?&g_config.weapon_holster_custom_y_m:
            !strcmp(key,"weapon_holster_custom_z_m")?&g_config.weapon_holster_custom_z_m:nullptr;
        if(holsterCustom)
        {
            const float fallback=*holsterCustom;
            ParseFloatSetting(key,val,*holsterCustom);
            *holsterCustom=std::isfinite(*holsterCustom)?std::clamp(*holsterCustom,-.80f,.80f):fallback;
            continue;
        }
        // Project Reclaimer's keys at the head were switches once; they are always on now.
        if (!strcmp(key, "reclaimer_talk_gesture") || !strcmp(key, "reclaimer_vote_gesture") ||
            !strcmp(key, "reclaimer_scoreboard_gesture"))
            continue;
        if (!strcmp(key, "reclaimer_vote_yes_key"))
        {
            const int code = atoi(val);
            g_config.reclaimer_vote_yes_key = code > 0 && code < 255 ? code : Config{}.reclaimer_vote_yes_key;
            continue;
        }
        if (!strcmp(key, "reclaimer_vote_no_key"))
        {
            const int code = atoi(val);
            g_config.reclaimer_vote_no_key = code > 0 && code < 255 ? code : Config{}.reclaimer_vote_no_key;
            continue;
        }
        if (!strcmp(key, "reclaimer_scoreboard_key"))
        {
            const int code = atoi(val);
            g_config.reclaimer_scoreboard_key = code > 0 && code < 255 ? code : Config{}.reclaimer_scoreboard_key;
            continue;
        }
        if (!strcmp(key, "reclaimer_talk_key"))
        {
            const int code = atoi(val);
            g_config.reclaimer_talk_key = code > 0 && code < 255 ? code : Config{}.reclaimer_talk_key;
            continue;
        }
        if (!strcmp(key, "dpad_keep_moving"))
        {
            ParseBoolSetting(key, val, g_config.dpad_keep_moving);
            continue;
        }
        if (!strcmp(key, "dpad_head_radius"))
        {
            ParseFloatSetting(key, val, g_config.dpad_head_radius);
            continue;
        }
        if (!strcmp(key, "quest_thumbrest_dpad"))
        {
            g_config.quest_thumbrest_dpad = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "hide_hud")) { g_config.hide_hud=atoi(val)!=0; continue; }
        if (!strcmp(key, "hud_reveal_near_head")) { ParseBoolSetting(key,val,g_config.hud_reveal_near_head); continue; }
        if (!strcmp(key, "hud_reveal_radius_m")) {
            if(ParseFloatSetting(key,val,g_config.hud_reveal_radius_m))
                g_config.hud_reveal_radius_m=std::clamp(g_config.hud_reveal_radius_m,.10f,.40f);
            continue;
        }
        if (!strcmp(key, "independent_dual_aim")) { g_config.independent_dual_aim=atoi(val)!=0; continue; }
        if (!strcmp(key, "two_hand_coherent_aim")) { g_config.two_hand_coherent_aim=atoi(val)!=0; continue; }
        if (!strcmp(key, "gun_barrel_aim")) { g_config.gun_barrel_aim=atoi(val)!=0; continue; }
        if (!strcmp(key, "shots_from_muzzle")) { g_config.shots_from_muzzle=atoi(val)!=0; continue; }
        if (!strcmp(key, "coop_safe_shots"))
        { const int v=atoi(val); g_config.coop_safe_shots=v<0?0:(v>2?2:v); continue; }
        if (!strcmp(key, "coop_safe_shots_version")) { coopSafeShotsV2 = atoi(val) >= 2; continue; }
        if (!strcmp(key, "aim_lead_ticks"))
        {
            // Held inside 0 to 3 ticks; a value that is not a number keeps the one in effect.
            float ticks = g_config.aim_lead_ticks;
            if (ParseFloatSetting(key, val, ticks))
                g_config.aim_lead_ticks = aim_lead::ClampTicks(ticks, g_config.aim_lead_ticks);
            continue;
        }
        if (!strcmp(key, "aim_provider")) { g_config.aim_provider=atoi(val); continue; }
        if (!strcmp(key, "legacy_direct_drive")) { g_config.legacy_direct_drive = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "converged_aim")) { g_config.converged_aim = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "scope_converge")) { g_config.scope_converge = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "seat_direct_drive")) { g_config.seat_direct_drive = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "seat_aim_through_barrel")) { g_config.seat_aim_through_barrel = atoi(val) != 0; continue; }
        if (!strcmp(key, "seat_fire_from_eye")) { g_config.seat_fire_from_eye = atoi(val) != 0; continue; }
        if (!strcmp(key, "halo4_seat_converge")) { g_config.halo4_seat_converge = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "install_search_memory")) { g_config.install_search_memory = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "install_search_cache")) { g_config.install_search_cache = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "install_hooks_together")) { g_config.install_hooks_together = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "aim_direct_drive"))
        { g_config.aim_direct_drive = atoi(val) != 0 ? 1 : 0; aimDirectDriveSeen = true; continue; }
        if (!strcmp(key, "drive_address_cache")) { g_config.drive_address_cache = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "reach_direct_drive"))
        {
            g_config.reach_direct_drive=atoi(val);
#if !HALOMCCVR_DEV_DIAGNOSTICS
            // A player build takes 0 (off) or 3 (direct drive) only: modes 1, 2 and 4 are research
            // tools that write aim state blind, and they are compiled into dev builds only.
            if (g_config.reach_direct_drive != 3) g_config.reach_direct_drive = 0;
#endif
            continue;
        }
        // A player's setting stands ABOVE the development-only part below: a key read only in there is saved by a
        // player build and then called unknown at its next start (tests/config_player_roundtrip_tests.cpp).
        if (!strcmp(key, "mono_rendering")) { g_config.mono_rendering = atoi(val) != 0; continue; }
        if (!strcmp(key, "aim_frame_zoom")) { g_config.aim_frame_zoom = atoi(val) != 0; continue; }
        if (!strcmp(key, "aim_frame_stereo")) { g_config.aim_frame_stereo = atoi(val) != 0; continue; }
        if (!strcmp(key, "halo2_camera_shake")) { g_config.halo2_camera_shake = atoi(val) != 0; continue; }
        if (!strcmp(key, "aim_frame_3d_strength")) { g_config.aim_frame_3d_strength = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_size_deg_1")) { g_config.aim_frame_size_deg[0] = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_size_deg_2")) { g_config.aim_frame_size_deg[1] = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_strength_1")) { g_config.aim_frame_strength[0] = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_strength_2")) { g_config.aim_frame_strength[1] = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_move_s")) { g_config.aim_frame_move_s = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_hud_size")) { g_config.aim_frame_hud_size = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_hud_width_scale")) { g_config.aim_frame_hud_width_scale = (float)atof(val); continue; }
        if (!strcmp(key, "aim_frame_hud_height_scale")) { g_config.aim_frame_hud_height_scale = (float)atof(val); continue; }
        if (!strcmp(key, "hook_cost_frames"))
        {
            const int frames = atoi(val);
            g_config.hook_cost_frames = frames <= 0 ? 0 : (frames < 8 ? 8 : (frames > 100000 ? 100000 : frames));
            continue;
        }
        if (!strcmp(key, "host_ui_panel")) { g_config.host_ui_panel = atoi(val) != 0; continue; }
        if (!strcmp(key, "host_ui_panel_width_m")) { g_config.host_ui_panel_width_m = (float)atof(val); continue; }
        if (!strcmp(key, "host_ui_panel_distance_m")) { g_config.host_ui_panel_distance_m = (float)atof(val); continue; }
        if (!strcmp(key, "reclaimer_flight_buttons")) { g_config.reclaimer_flight_buttons = atoi(val) != 0; continue; }
#if HALOMCCVR_DEV_DIAGNOSTICS   // research tools and dev overrides: a player build never reads them
        if (!strcmp(key, "reach_aim_probe")) { g_config.reach_aim_probe=atoi(val); continue; }
        if (!strcmp(key, "dev_install_search_check")) { g_config.dev_install_search_check = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "reach_desired_aim_offset")) { g_config.reach_desired_aim_offset=(int)strtol(val,nullptr,0); continue; }
        if (!strcmp(key, "reach_dd_test_deg")) { g_config.reach_dd_test_deg=atoi(val); continue; }
        if (!strcmp(key, "reach_dd_watchpoint")) { g_config.reach_dd_watchpoint=atoi(val); continue; }
        if (!strcmp(key, "reach_sight_trace_flags")) { g_config.reach_sight_trace_flags=strtoull(val,nullptr,0); continue; }
        if (!strcmp(key, "reach_sight_trace_mode")) { g_config.reach_sight_trace_mode=(int)strtol(val,nullptr,0); continue; }
        if (!strcmp(key, "reach_collision_census")) { g_config.reach_collision_census=atoi(val); continue; }
        if (!strcmp(key, "sight_ray_census")) { g_config.sight_ray_census=atoi(val); continue; }
        if (!strcmp(key, "seated_inherit_compensation")) { g_config.seated_inherit_compensation=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "shot_point_transport")) { g_config.shot_point_transport=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "shot_point_transport_seated")) { g_config.shot_point_transport_seated=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "halo2_measured_fire_eye")) { g_config.halo2_measured_fire_eye=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "halo4_measured_fire_eye")) { g_config.halo4_measured_fire_eye=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "seat_upstream_aim"))
        {
            const int v = atoi(val);
            g_config.seat_upstream_aim = v < 0 ? 0 : (v > 2 ? 2 : v);
            continue;
        }
        if (!strcmp(key, "honest_reticle_assess"))
        {
            const int v = atoi(val);
            g_config.honest_reticle_assess = v < 0 ? 0 : (v > 2 ? 2 : v);
            continue;
        }
        if (!strcmp(key, "type13_log")) { g_config.type13_log=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "seat_aim_world_frame")) { g_config.seat_aim_world_frame=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "seat_view_tag_write")) { g_config.seat_view_tag_write=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "dev_synthetic_pad")) { g_config.dev_synthetic_pad=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "dev_vehicle_unnamed")) { g_config.dev_vehicle_unnamed=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "dev_seat_body_probe")) { g_config.dev_seat_body_probe=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "dev_sim_trace")) { g_config.dev_sim_trace=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "dev_sim_probe")) { g_config.dev_sim_probe=(int)strtol(val,nullptr,0)&0xFF; continue; }
        if (!strcmp(key, "dev_sim_minimal")) { g_config.dev_sim_minimal=atoi(val)!=0?1:0; continue; }
        if (!strcmp(key, "dev_sim_bisect")) { g_config.dev_sim_bisect=(int)strtol(val,nullptr,0)&0xFFFF; continue; }
        if (!strcmp(key, "dev_ps_census")) { g_config.dev_ps_census = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "reach_sight_fault")) { g_config.reach_sight_fault=(int)strtol(val,nullptr,0); continue; }
        if (!strcmp(key, "first_person_rung_fault")) { g_config.first_person_rung_fault=(int)strtol(val,nullptr,0)&0xF; continue; }
        if (!strcmp(key, "drive_fault")) { g_config.drive_fault=(int)strtol(val,nullptr,0)&0xFF; continue; }
        if (!strcmp(key, "reach_sight_trace_length_m")) { g_config.reach_sight_trace_length_m=(float)atof(val); continue; }
        if (!strcmp(key, "reach_sight_trace_site"))
        {
            const int v = atoi(val);
            g_config.reach_sight_trace_site = v < 0 ? -1 : (v > 1 ? 1 : v);
            continue;
        }
        if (!strcmp(key, "reach_effect_move_rule"))
        {
            const int v = atoi(val);
            g_config.reach_effect_move_rule = v < 0 ? -1 : (v > 3 ? 3 : v);
            continue;
        }
        if (!strcmp(key, "tracker_head_sign")) { g_config.tracker_head_sign = atoi(val) < 0 ? -1 : 1; continue; }
        if (!strcmp(key, "audio_head_halo4_parts")) { g_config.audio_head_halo4_parts = atoi(val) & 7; continue; }
        if (!strcmp(key, "nav_marker_witness")) { g_config.nav_marker_witness = atoi(val) != 0 ? 1 : 0; continue; }
        if (!strcmp(key, "tracker_head_offset_deg"))
        {
            // A NaN fails both comparisons and reads as no offset.
            const float v = static_cast<float>(atof(val));
            g_config.tracker_head_offset_deg = (v >= -180.0f && v <= 180.0f) ? v : 0.0f;
            continue;
        }
#endif
        if (!strcmp(key, "reach_vehicle_aim"))
        {
            const int v = atoi(val);
            g_config.reach_vehicle_aim = v < 0 ? 0 : (v > 2 ? 2 : v);
            continue;
        }
        if (!strcmp(key, "honest_reticle_onfoot"))
        {
            // A retired key, read and ignored: there is no player-facing honest reticle (the crosshair stays on the
            // converged point; the game's own tracers show where a held or slewing gun really fires). Dev builds have
            // the assessment key honest_reticle_assess instead. Logged once a session when a cfg still sets it.
            static bool s_loggedHonestOnFoot = false;
            if (atoi(val) != 0 && !s_loggedHonestOnFoot)
            {
                s_loggedHonestOnFoot = true;
                LOG("config: honest_reticle_onfoot is retired and ignored (the crosshair stays on the aim point)");
            }
            continue;
        }
        if (!strcmp(key, "reticle_max_dist_m")) { g_config.reticle_max_dist_m=(float)atof(val); continue; }
        if (!strcmp(key, "reticle_surface_offset_m")) { g_config.reticle_surface_offset_m=(float)atof(val); continue; }
        if (!strcmp(key, "reticle_min_scale_dist_m")) { g_config.reticle_min_scale_dist_m=(float)atof(val); continue; }
        if (!strcmp(key, "reticle_min_scale")) { g_config.reticle_min_scale=(float)atof(val); continue; }
        if (!strcmp(key, "halo4_helmet"))
        {
            g_config.halo4_helmet = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "roomscale_movement"))
        {
            g_config.roomscale_movement = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "roomscale_follow_law"))
        {
            g_config.roomscale_follow_law = atoi(val) != 0 ? 1 : 0;
            continue;
        }
        if (!strcmp(key, "roomscale_recenter_jump")) { g_config.roomscale_recenter_jump = atoi(val) != 0; continue; }
        if (!strcmp(key, "roomscale_recenter_jump_s"))
        {
            g_config.roomscale_recenter_jump_s = roomscale_recenter::ClampSeconds(
                static_cast<float>(atof(val)), Config{}.roomscale_recenter_jump_s);
            continue;
        }
        if (!strcmp(key, "roomscale_recenter_move_start")) { g_config.roomscale_recenter_move_start = atoi(val) != 0; continue; }
        if (!strcmp(key, "roomscale_recenter_move_start_s"))
        {
            g_config.roomscale_recenter_move_start_s = roomscale_recenter::ClampSeconds(
                static_cast<float>(atof(val)), Config{}.roomscale_recenter_move_start_s);
            continue;
        }
        if (!strcmp(key, "roomscale_recenter_moving")) { g_config.roomscale_recenter_moving = atoi(val) != 0; continue; }
        if (!strcmp(key, "roomscale_recenter_moving_speed"))
        {
            g_config.roomscale_recenter_moving_speed = roomscale_recenter::ClampSpeed(
                static_cast<float>(atof(val)), Config{}.roomscale_recenter_moving_speed);
            continue;
        }
        if (!strcmp(key, "tracker_follows_head")) { g_config.tracker_follows_head = atoi(val) != 0; continue; }
        if (!strcmp(key, "audio_follows_head")) { g_config.audio_follows_head = atoi(val) != 0; continue; }
        if (!strcmp(key, "title_unload_wait")) { g_config.title_unload_wait = atoi(val) != 0; continue; }
        if (!strcmp(key, "crosshair_opacity")) { ParseFloatSetting(key, val, g_config.crosshair_opacity); continue; }
        if (!strcmp(key, "crosshair_brightness")) { ParseFloatSetting(key, val, g_config.crosshair_brightness); continue; }
        // Hand smoothing (hand_smoothing.h). Outside the chain below: it is at the compiler's nesting limit.
        if (!strncmp(key, "hand_smoothing", 14))
        {
            if (!strcmp(key, "hand_smoothing")) { ParseBoolSetting(key, val, g_config.hand_smoothing); continue; }
            if (!strcmp(key, "hand_smoothing_hands")) { ParseIntSetting(key, val, 0, 2, g_config.hand_smoothing_hands); continue; }
            if (!strcmp(key, "hand_smoothing_pos_min_hz")) { ParseFloatSetting(key, val, g_config.hand_smoothing_pos_min_hz); continue; }
            if (!strcmp(key, "hand_smoothing_pos_beta")) { ParseFloatSetting(key, val, g_config.hand_smoothing_pos_beta); continue; }
            if (!strcmp(key, "hand_smoothing_rot_min_hz")) { ParseFloatSetting(key, val, g_config.hand_smoothing_rot_min_hz); continue; }
            if (!strcmp(key, "hand_smoothing_rot_beta")) { ParseFloatSetting(key, val, g_config.hand_smoothing_rot_beta); continue; }
            if (!strcmp(key, "hand_smoothing_speed_cutoff_hz")) { ParseFloatSetting(key, val, g_config.hand_smoothing_speed_cutoff_hz); continue; }
        }
        if (!strcmp(key, "tracker_blips_follow_hud_height"))
        {
            g_config.tracker_blips_follow_hud_height = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "nav_markers_on_target")) { g_config.nav_markers_on_target = atoi(val) != 0; continue; }
        if (!strcmp(key, "nav_markers_follow_head")) { g_config.nav_markers_follow_head = atoi(val) != 0; continue; }
        if (!strcmp(key, "nav_marker_arrow_in_view")) { g_config.nav_marker_arrow_in_view = atoi(val) != 0; continue; }
        if (!strcmp(key, "nav_marker_ring"))
        {
            // A NaN fails both comparisons and reads as the default.
            const float v = static_cast<float>(atof(val));
            g_config.nav_marker_ring = (v >= 0.0f && v <= 1.0f) ? v : Config{}.nav_marker_ring;
            continue;
        }
        if (!strcmp(key, "player_tags_show")) { g_config.player_tags_show = atoi(val) != 0; continue; }
        if (!strcmp(key, "player_tags_scale") || !strcmp(key, "player_tags_opacity") ||
            !strcmp(key, "player_tags_max_distance_m"))
        {
            // Out of range or NaN reads as the default.
            const float v = static_cast<float>(atof(val));
            const Config d{};
            if (!strcmp(key, "player_tags_scale"))
                g_config.player_tags_scale = (v >= 0.25f && v <= 2.0f) ? v : d.player_tags_scale;
            else if (!strcmp(key, "player_tags_opacity"))
                g_config.player_tags_opacity = (v >= 0.0f && v <= 1.0f) ? v : d.player_tags_opacity;
            else
                g_config.player_tags_max_distance_m = (v >= 0.0f && v <= 300.0f) ? v : d.player_tags_max_distance_m;
            continue;
        }
        if (!strcmp(key, "experimental_hand_alignment"))
        {
            g_config.experimental_hand_alignment = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "left_handed"))
        {
            g_config.left_handed = atoi(val) != 0;
            continue;
        }
        // Keep the new independent left-hand offset keys outside the legacy
        // else-if chain: those extra arms exceed MSVC's block-nesting limit (C1061).
        if (!strcmp(key, "left_gun_forward_m"))
        { g_config.left_gun_forward_m = (float)atof(val); continue; }
        if (!strcmp(key, "left_gun_right_m"))
        { g_config.left_gun_right_m = (float)atof(val); continue; }
        if (!strcmp(key, "left_gun_up_m"))
        { g_config.left_gun_up_m = (float)atof(val); continue; }

        // Here, not in the else-if chain below: that chain is at MSVC's block nesting limit (C1061).
        if (!strcmp(key, "vehicle_vr_controls"))
        {
            g_config.vehicle_vr_controls = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "vehicle_cameras"))
        {
            g_config.vehicle_cameras = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "vehicle_camera_autoadd"))
        {
            g_config.vehicle_camera_autoadd = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "vehicle_camera_note"))
        {
            const int note = atoi(val);
            g_config.vehicle_camera_note = note < 0 || note > 2 ? 1 : note;
            continue;
        }
        if (!strcmp(key, "vehicle_aim_marker"))
        {
            g_config.vehicle_aim_marker = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "vehicle_control"))
        {
            const int mode = atoi(val);
            g_config.vehicle_control = mode >= 0 && mode <= 3 ? mode : 0;
            continue;
        }
        if (!strcmp(key, "vehicle_aim_reject_metres"))
        {
            g_config.vehicle_aim_reject_metres = static_cast<float>(atof(val));
            continue;
        }
        if (!strcmp(key, "vehicle_aim_reject_dot"))
        {
            g_config.vehicle_aim_reject_dot = static_cast<float>(atof(val));
            continue;
        }
        if (!strcmp(key, "vehicle_boom_rate_deg_s"))
        {
            g_config.vehicle_boom_rate_deg_s = static_cast<float>(atof(val));
            continue;
        }
        if (!strcmp(key, "vehicle_boom_invert"))
        {
            const int invert = atoi(val);
            g_config.vehicle_boom_invert = invert < -1 || invert > 1 ? -1 : invert;
            continue;
        }
        // C-H2-85: handled by table, not by the else-if chain below - that
        // chain is at MSVC's block nesting limit and cannot take more arms.
        {
            struct FloatKey { const char* name; float* destination; };
            const FloatKey kFloatKeys[] = {
                {"virtual_stock_rear_height_m", &g_config.virtual_stock_rear_height_m},
                {"virtual_stock_shoulder_back_m", &g_config.virtual_stock_shoulder_back_m},
                {"virtual_stock_shoulder_side_m", &g_config.virtual_stock_shoulder_side_m},
                {"virtual_stock_chest_height_m", &g_config.virtual_stock_chest_height_m},
                {"virtual_stock_chest_back_m", &g_config.virtual_stock_chest_back_m},
                {"virtual_stock_chest_side_m", &g_config.virtual_stock_chest_side_m},
                {"virtual_stock_adaptive_top_height_m",
                 &g_config.virtual_stock_adaptive_top_height_m},
                {"virtual_stock_adaptive_bottom_height_m",
                 &g_config.virtual_stock_adaptive_bottom_height_m},
                {"virtual_stock_adaptive_top_half_width_m",
                 &g_config.virtual_stock_adaptive_top_half_width_m},
                 {"virtual_stock_adaptive_bottom_half_width_m",
                  &g_config.virtual_stock_adaptive_bottom_half_width_m},
                 {"virtual_stock_hybrid_offhand_influence",
                  &g_config.virtual_stock_hybrid_offhand_influence},
                 {"virtual_stock_hybrid_seat_full_m",
                  &g_config.virtual_stock_hybrid_seat_full_m},
                 {"virtual_stock_hybrid_seat_release_m",
                   &g_config.virtual_stock_hybrid_seat_release_m},
                 {"virtual_stock_hybrid_horizontal_full_m",
                  &g_config.virtual_stock_hybrid_horizontal_full_m},
                 {"virtual_stock_hybrid_horizontal_release_m",
                  &g_config.virtual_stock_hybrid_horizontal_release_m},
                 {"virtual_stock_proximity_full_m", &g_config.virtual_stock_proximity_full_m},
                {"virtual_stock_proximity_release_m", &g_config.virtual_stock_proximity_release_m},
                {"left_hand_mesh_x_m", &g_config.left_hand_mesh_x_m},
                {"left_hand_mesh_y_m", &g_config.left_hand_mesh_y_m},
                {"left_hand_mesh_z_m", &g_config.left_hand_mesh_z_m},
                {"right_hand_mesh_x_m", &g_config.right_hand_mesh_x_m},
                {"right_hand_mesh_y_m", &g_config.right_hand_mesh_y_m},
                {"right_hand_mesh_z_m", &g_config.right_hand_mesh_z_m},

                // Stage 3N / V5 compatibility. The current source writes the
                // expanded `halo2_...` names below, but existing V5 configs
                // used these two shorter keys for the same Classic-only
                // visual carrier trim.
                {"h2_classic_gun_pitch_deg",
                 &g_config.halo2_classic_gun_pitch_deg},
                {"h2_classic_gun_yaw_deg",
                 &g_config.halo2_classic_gun_yaw_deg},
                {"halo2_classic_gun_pitch_deg",
                 &g_config.halo2_classic_gun_pitch_deg},
                {"halo2_classic_gun_yaw_deg",
                 &g_config.halo2_classic_gun_yaw_deg},
                {"halo2_classic_gun_roll_deg",
                 &g_config.halo2_classic_gun_roll_deg},
                {"halo2_classic_gun_forward_m",
                 &g_config.halo2_classic_gun_forward_m},
                {"halo2_classic_gun_right_m",
                 &g_config.halo2_classic_gun_right_m},
                {"halo2_classic_gun_up_m",
                 &g_config.halo2_classic_gun_up_m},
                {"barrel_pitch_deg", &g_config.barrel_pitch_deg},
                {"barrel_yaw_deg", &g_config.barrel_yaw_deg},
                {"barrel_roll_deg", &g_config.barrel_roll_deg},
            };
            bool handled = false;
            for (const FloatKey& entry : kFloatKeys)
            {
                if (strcmp(key, entry.name) != 0)
                    continue;
                ParseFloatSetting(key, val, *entry.destination);
                handled = true;
                break;
            }
            if (handled)
                continue;
        }
        if (!strcmp(key, "game_menu_pointer"))
        {
            g_config.game_menu_pointer = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "allow_ofxr_bridge"))
        {
            g_config.allow_ofxr_bridge = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "desktop_mirror"))
        {
            g_config.desktop_mirror = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "reclaimer_share_device"))
        {
            g_config.reclaimer_share_device = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "reclaimer_fit_to_display"))
        {
            g_config.reclaimer_fit_to_display = atoi(val) != 0;
            continue;
        }
        if (!strcmp(key, "config_version"))
        {
            char* end = nullptr;
            const long parsed = strtol(val, &end, 10);
            while (end && isspace(static_cast<unsigned char>(*end)))
                ++end;
            if (end == val || !end || *end != 0 || parsed < 1)
                LOG("config: malformed value for 'config_version' ignored; using version 1");
            else
            {
                loadedConfigVersion = static_cast<int>(parsed);
                if (parsed > 7)
                    LOG("config: version %ld is newer than supported version 7; known keys will be loaded", parsed);
            }
        }
        else if (!strcmp(key, "haptic_intensity"))
            ParseFloatSetting(key, val, g_config.haptic_intensity);
        else if (!strcmp(key, "headset_smoothing"))
            ParseFloatSetting(key, val, g_config.headset_smoothing);
        else if (!strcmp(key, "aim_stabilization"))
            ParseFloatSetting(key, val, g_config.aim_stabilization);
        else if (!strcmp(key, "screen_width_m"))
            g_config.screen_width_m = (float)atof(val);
        else if (!strcmp(key, "screen_distance_m"))
            g_config.screen_distance_m = (float)atof(val);
        else if (!strcmp(key, "cutscene_theater_enabled"))
            g_config.cutscene_theater_enabled = atoi(val) != 0;
        else if (!strcmp(key, "cutscene_theater_depth"))
            ParseFloatSetting(key, val, g_config.cutscene_theater_depth);
        else if (!strcmp(key, "cutscene_theater_flip_depth"))
            g_config.cutscene_theater_flip_depth = atoi(val) != 0;
        else if (!strcmp(key, "cutscene_theater_width_m"))
            ParseFloatSetting(key, val, g_config.cutscene_theater_width_m);
        else if (!strcmp(key, "cutscene_theater_distance_m"))
            ParseFloatSetting(key, val, g_config.cutscene_theater_distance_m);
        else if (!strcmp(key, "cutscene_theater_matte_aspect"))
            ParseFloatSetting(key, val, g_config.cutscene_theater_matte_aspect);
        else if (!strcmp(key, "cutscene_theater_matte_offset"))
            ParseFloatSetting(key, val, g_config.cutscene_theater_matte_offset);
        else if (!strcmp(key, "cutscene_theater_subtitles"))
            g_config.cutscene_theater_subtitles = atoi(val) != 0;
        else if (!strcmp(key, "cutscene_theater_subtitle_band"))
            ParseFloatSetting(key, val, g_config.cutscene_theater_subtitle_band);
        else if (!strcmp(key, "cutscene_theater_subtitle_debug"))
            g_config.cutscene_theater_subtitle_debug = atoi(val) != 0;
        else if (!strcmp(key, "menu_distance_m"))
            ParseFloatSetting(key, val, g_config.menu_distance_m);
        else if (!strcmp(key, "menu_width_m"))
            ParseFloatSetting(key, val, g_config.menu_width_m);
        else if (!strcmp(key, "menu_height_m"))
            ParseFloatSetting(key, val, g_config.menu_height_m);
        else if (!strcmp(key, "menu_side_m"))
            ParseFloatSetting(key, val, g_config.menu_side_m);
        else if (!strcmp(key, "show_welcome"))
            g_config.show_welcome = atoi(val) != 0;
        else if (!strcmp(key, "turn_smooth"))
            g_config.turn_smooth = atoi(val) != 0;
        else if (!strcmp(key, "turn_snap_deg"))
            g_config.turn_snap_deg = (float)atof(val);
        else if (!strcmp(key, "turn_smooth_deg_s"))
            g_config.turn_smooth_deg_s = (float)atof(val);
        else if (!strcmp(key, "y_b_start_chord"))
            g_config.y_b_start_chord = atoi(val) != 0;
        else if (!strcmp(key, "halo2_gamepad_graphics_switch"))
            g_config.halo2_gamepad_graphics_switch = atoi(val) != 0;
        else if (!strcmp(key, "ghost_fix") || !strcmp(key, "stereo_alternate_order") ||
                 !strcmp(key, "stereo_warmup_pass") || !strcmp(key, "per_eye_history") ||
                 !strcmp(key, "stereo_sun_shafts") || !strcmp(key, "gun_length_scale") ||
                 !strcmp(key, "vehicle_cam_lead"))
            continue; // retired switches; accept old config files quietly
        else if (!strcmp(key, "dpad_hand"))
            g_config.dpad_hand = atoi(val) != 0 ? 1 : 0;
        else if (!strcmp(key, "vehicle_first_person"))
            g_config.vehicle_first_person = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_cam_forward_m"))
            ParseFloatSetting(key,val,g_config.vehicle_cam_forward_m);
        else if (!strcmp(key, "vehicle_cam_up_m"))
            ParseFloatSetting(key,val,g_config.vehicle_cam_up_m);
        else if (!strcmp(key, "vehicle_cam_right_m"))
            ParseFloatSetting(key,val,g_config.vehicle_cam_right_m);
        // Per-seat trim overrides: vehicle_cam_forward_m_warthog_passenger.
        // The exact matches above cannot fire for these (different length).
        // A malformed value must NOT invent an override (the seat keeps
        // following the universal trim), and an unknown suffix is reported,
        // not silently swallowed.
        // Title banks must be tested before the generic Halo 3 prefix.
        else if (!strncmp(key, kReachSeatUniversalPrefix,
                          kReachSeatUniversalPrefixLength))
            ParseReachSeatUniversal(
                key, key + kReachSeatUniversalPrefixLength, val,
                g_config.reach_vehicle_cam_use_universal);
        else if (!strncmp(key, "vehicle_cam_forward_m_reach_", 28))
        {
            const int slot = ParseReachSeatTrim(
                key, key + 28, val, g_config.reach_vehicle_cam_forward_v,
                g_config.reach_vehicle_cam_forward_set);
            if (slot >= 0)
                reachNumericKeyLoaded[slot] = true;
        }
        else if (!strncmp(key, "vehicle_cam_forward_m_odst_", 27))
            ParseOdstSeatTrim(key, key + 27, val,
                              g_config.odst_vehicle_cam_forward_v,
                              g_config.odst_vehicle_cam_forward_set);
        else if (!strncmp(key, "vehicle_cam_up_m_reach_", 23))
        {
            const int slot = ParseReachSeatTrim(
                key, key + 23, val, g_config.reach_vehicle_cam_up_v,
                g_config.reach_vehicle_cam_up_set);
            if (slot >= 0)
                reachNumericKeyLoaded[slot] = true;
        }
        else if (!strncmp(key, "vehicle_cam_up_m_odst_", 22))
            ParseOdstSeatTrim(key, key + 22, val,
                              g_config.odst_vehicle_cam_up_v,
                              g_config.odst_vehicle_cam_up_set);
        else if (!strncmp(key, "vehicle_cam_right_m_reach_", 26))
        {
            const int slot = ParseReachSeatTrim(
                key, key + 26, val, g_config.reach_vehicle_cam_right_v,
                g_config.reach_vehicle_cam_right_set);
            if (slot >= 0)
                reachNumericKeyLoaded[slot] = true;
        }
        else if (!strncmp(key, "vehicle_cam_right_m_odst_", 25))
            ParseOdstSeatTrim(key, key + 25, val,
                              g_config.odst_vehicle_cam_right_v,
                              g_config.odst_vehicle_cam_right_set);
        else if (!strncmp(key, "vehicle_cam_forward_m_", 22))
            ParseSeatTrim(key, key + 22, val, g_config.vehicle_cam_forward_v,
                          g_config.vehicle_cam_forward_set);
        else if (!strncmp(key, "vehicle_cam_up_m_", 17))
            ParseSeatTrim(key, key + 17, val, g_config.vehicle_cam_up_v,
                          g_config.vehicle_cam_up_set);
        else if (!strncmp(key, "vehicle_cam_right_m_", 20))
            ParseSeatTrim(key, key + 20, val, g_config.vehicle_cam_right_v,
                          g_config.vehicle_cam_right_set);
        else if (!strcmp(key, "vehicle_view_follow"))
        {
            // C19 changed the fractional control to a toggle. Preserve every
            // old non-zero setting (including the user's 0.82) as ON.
            float follow = g_config.vehicle_view_follow ? 1.0f : 0.0f;
            if (ParseFloatSetting(key, val, follow))
                g_config.vehicle_view_follow = follow != 0.0f;
        }
        else if (!strcmp(key, "vehicle_cam_smoothing"))
            g_config.vehicle_cam_smoothing = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_hide_body"))
            g_config.vehicle_hide_body = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_hands_follow_body"))
            g_config.vehicle_hands_follow_body = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_bounce"))
            g_config.vehicle_bounce = (float)atof(val);
        else if (!strcmp(key, "vehicle_recenter_on_seat"))
            g_config.vehicle_recenter_on_seat = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_steady_exposure"))
            g_config.vehicle_steady_exposure = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_motion"))
            g_config.vehicle_motion = atoi(val) != 0;
        else if (!strcmp(key, "vehicle_wheel_max_deg"))
            g_config.vehicle_wheel_max_deg = (float)atof(val);
        else if (!strcmp(key, "vehicle_wheel_deadzone_deg"))
            g_config.vehicle_wheel_deadzone_deg = (float)atof(val);
        else if (!strcmp(key, "halo4_hands"))
            g_config.halo4_hands = atoi(val) != 0;
        else if (!strcmp(key, "halo4_hand_forward_m"))
            g_config.halo4_hand_forward_m = (float)atof(val);
        else if (!strcmp(key, "halo4_hand_vertical_m"))
            g_config.halo4_hand_vertical_m = (float)atof(val);
        else if (!strcmp(key, "halo4_hand_lateral_m"))
            g_config.halo4_hand_lateral_m = (float)atof(val);
        else if (!strcmp(key, "halo4_hands_mirrored"))
            g_config.halo4_hands_mirrored = atoi(val) != 0;
        else if (!strcmp(key, "crosshair"))
            g_config.crosshair = atoi(val) != 0;
        else if (!strcmp(key, "crosshair_distance_m"))
            g_config.crosshair_distance_m = (float)atof(val);
        else if (!strcmp(key, "crosshair_size_deg"))
            g_config.crosshair_size_deg = (float)atof(val);
        else if (!strcmp(key, "crosshair_animation_frames"))
            g_config.crosshair_animation_frames = atoi(val);
        else if (!strcmp(key, "reticle_r"))
            g_config.reticle_r = (float)atof(val);
        else if (!strcmp(key, "reticle_g"))
            g_config.reticle_g = (float)atof(val);
        else if (!strcmp(key, "reticle_b"))
            g_config.reticle_b = (float)atof(val);
        else if (!strcmp(key, "gun_scale"))
            g_config.gun_scale = (float)atof(val);
        else if (!strcmp(key, "left_hand_scale"))
            g_config.left_hand_scale = (float)atof(val);
        else if (!strcmp(key, "bullet_snap"))
            continue; // retired: the composed-wrist snap was reverted (hand spin); accept old cfgs quietly
        else if (!strcmp(key, "hud_probe"))
            g_config.hud_probe = atoi(val) != 0;
        else if (!strcmp(key, "fsr_probe"))
            g_config.fsr_probe = atoi(val) != 0;
        else if (!strcmp(key, "bullet_probe"))
            g_config.bullet_probe = atoi(val) != 0;
        else if (!strcmp(key, "weapon_probe"))
            g_config.weapon_probe = atoi(val) != 0;
        else if (!strcmp(key, "gun_pitch_deg"))
            g_config.gun_pitch_deg = (float)atof(val);
        else if (!strcmp(key, "gun_yaw_deg"))
            g_config.gun_yaw_deg = (float)atof(val);
        else if (!strcmp(key, "gun_roll_deg"))
            g_config.gun_roll_deg = (float)atof(val);
        else if (!strcmp(key, "gun_forward_m"))
            g_config.gun_forward_m = (float)atof(val);
        else if (!strcmp(key, "gun_right_m"))
            g_config.gun_right_m = (float)atof(val);
        else if (!strcmp(key, "gun_up_m"))
            g_config.gun_up_m = (float)atof(val);
        else if (!strcmp(key, "muzzle_height_m"))
            g_config.muzzle_height_m = (float)atof(val);
        else if (!strcmp(key, "scope_enabled"))
            g_config.scope_enabled = atoi(val) != 0;
        else if (!strcmp(key, "scope_zoom"))
        {
            g_config.scope_zoom = (float)atof(val);
            loadedScopeZoom = true;
        }
        else if (!strcmp(key, "scope_screen_width_m"))
            g_config.scope_screen_width_m = (float)atof(val);
        else if (!strcmp(key, "scope_screen_right_m"))
            g_config.scope_screen_right_m = (float)atof(val);
        else if (!strcmp(key, "scope_screen_up_m"))
            g_config.scope_screen_up_m = (float)atof(val);
        else if (!strcmp(key, "scope_screen_forward_m"))
            g_config.scope_screen_forward_m = (float)atof(val);
        else if (!strcmp(key, "scope_refresh_divisor"))
            g_config.scope_refresh_divisor = atoi(val);
        else if (!strcmp(key, "scope_magnification") ||
                 !strcmp(key, "scope_size_deg") ||
                 !strcmp(key, "scope_up_m") ||
                 !strcmp(key, "scope_forward_m"))
            continue; // retire the failed diagnostic-scope coordinates quietly
        else if (!strcmp(key, "show_hud") || !strcmp(key, "hud_ammo") ||
                 !strcmp(key, "hud_health") || !strcmp(key, "hud_motion") ||
                 !strcmp(key, "hud_grenades"))
            continue; // retired: chud byte writes used a disproven offset map; accept old cfgs quietly
        else if (!strcmp(key, "kill_reticle"))
            g_config.kill_reticle = atoi(val) != 0;
        else if (!strcmp(key, "reticle_element_id"))
            continue; // retired runtime tag-index picker; accept old configs quietly
        else if (!strcmp(key, "game_brightness"))
            ParseFloatSetting(key, val, g_config.game_brightness);
        else if (!strcmp(key, "resolution_scale"))
            g_config.resolution_scale = (float)atof(val);
        else if (!strcmp(key, "fit_desktop_window"))
            g_config.fit_desktop_window = atoi(val) != 0;
        else if (!strcmp(key, "desktop_present_unlocked"))
            g_config.desktop_present_unlocked = atoi(val) != 0;
        else if (!strcmp(key, "coop_probe"))
            g_config.coop_probe = atoi(val) != 0;
        else if (!strcmp(key, "draw_distance"))
            g_config.draw_distance = (float)atof(val);
        else if (!strcmp(key, "rain"))
            g_config.rain = atoi(val) != 0;
        else if (!strcmp(key, "atmospheric_fog"))
            g_config.atmospheric_fog = atoi(val) != 0;
        else if (!strcmp(key, "upscale_filter"))
            g_config.upscale_filter = atoi(val);
        else if (!strcmp(key, "sharpness"))
            ParseFloatSetting(key, val, g_config.sharpness);
        else if (!strcmp(key, "aa_mode"))
            g_config.aa_mode = atoi(val);
        else if (!strcmp(key, "hud_size"))
            g_config.hud_size = (float)atof(val);
        else if (!strcmp(key, "hud_aspect"))
            ParseFloatSetting(key, val, g_config.hud_aspect);
        else if (!strcmp(key, "hud_curvature") || !strcmp(key, "hud_height"))
        {
            ParseFloatSetting(key, val, g_config.hud_curvature);
            loadedLegacyCurvature = loadedConfigVersion < 2 || !strcmp(key, "hud_height");
        }
        else if (!strcmp(key, "hud_vertical_offset"))
            ParseFloatSetting(key, val, g_config.hud_vertical_offset);
        else if (!strcmp(key, "hud_offset_x") || !strcmp(key, "hud_offset_y") ||
                 !strcmp(key, "hud_elem_scale"))
            continue; // retired placement-experiment keys; accept old cfgs quietly
        else if (!strcmp(key, "hud_scale") || !strcmp(key, "hud_forward") ||
                 !strcmp(key, "hud_fov_scale") || !strcmp(key, "hud_zoom") ||
                 !strcmp(key, "hud_panel") || !strcmp(key, "hud_panel_size_m") ||
                 !strcmp(key, "hud_panel_dist_m"))
            continue; // retired levers (hud_scale was brightness; hud_zoom + the capture-diff panel both disproven); accept old cfgs quietly
        else if (!strcmp(key, "auto_vr"))
            g_config.auto_vr = atoi(val) != 0;
        else if (!strcmp(key, "two_handed_aim"))
            g_config.two_handed_aim = atoi(val) != 0;
        else if (!strcmp(key, "two_hand_toggle"))
            g_config.two_hand_toggle = atoi(val) != 0;
        else if (!strcmp(key, "left_hand_forward_m"))
            g_config.left_hand_forward_m = (float)atof(val);
        else if (!strcmp(key, "two_hand_zone_right_m"))
            g_config.two_hand_zone_right_m = (float)atof(val);
        else if (!strcmp(key, "left_grip_forward_m"))
            g_config.left_grip_forward_m = (float)atof(val);
        else if (!strcmp(key, "crouch_by_height") || !strcmp(key, "crouch_threshold_m"))
            continue; // removed feature; accept old config files quietly
        else if (!strcmp(key, "body_wip"))
            g_config.body_wip = atoi(val) != 0;
        else if (!strcmp(key, "arm_ik"))
            g_config.arm_ik = atoi(val) != 0;
        else if (!strcmp(key, "floating_hands") ||
                 !strcmp(key, "world_collision") ||
                 !strcmp(key, "gesture_melee") ||
                 !strcmp(key, "physical_melee"))
        {
            bool* destination = key[0] == 'f' ? &g_config.floating_hands :
                (key[0] == 'w' ? &g_config.world_collision :
                 key[0] == 'g' ? &g_config.gesture_melee :
                    &g_config.physical_melee);
            *destination = atoi(val) != 0;
        }
        else if (!strcmp(key, "physical_melee_swing_speed") ||
                 !strcmp(key, "right_shoulder_drop"))
            (key[0] == 'p' ? g_config.physical_melee_swing_speed :
                g_config.right_shoulder_drop) = (float)atof(val);
        else if (!strcmp(key, "shoulder_back_m"))
            g_config.shoulder_back_m = (float)atof(val);
        else if (!strcmp(key, "shoulder_level"))
            g_config.shoulder_level = atoi(val) != 0;
        else if (!strcmp(key, "motion_blur"))
            g_config.motion_blur = atoi(val) != 0;
        else if (!strcmp(key, "right_eye_first"))
            g_config.right_eye_first = atoi(val) != 0;
        else if (*key)
            LOG("config: unknown key '%s' ignored", key);
    }
    fclose(f);
    // A file from before input presets. A remapping player's Automatic rows meant the fixed VR layout, and Automatic
    // now follows the game's own: write that layout out so nothing moves under them.
    if (!loadedInputPreset && g_config.vr_action_mapping)
    {
        g_config.input_preset[0] = 0;
        for (auto& title : g_config.vr_bindings)
            for (unsigned action = 0; action < vr_mapping::Count; ++action)
                if (title[action] == vr_mapping::Automatic)
                    title[action] = vr_mapping::kDefaults[action];
    }
    // Before version 7, "Default" was the struct's scheme, now Legacy Default: the player keeps it under its new name.
    if (loadedConfigVersion < 7 && !strcmp(g_config.input_preset, "Default"))
        strcpy_s(g_config.input_preset, "Legacy Default");
    if (loadedLegacyVirtualStockStrength)
    {
        if (VirtualStockUsesPlusMode(g_config))
        {
            if (!loadedPlusVirtualStockStrength)
                g_config.virtual_stock_plus_strength = legacyVirtualStockStrength;
        }
        else if (!loadedStandardVirtualStockStrength)
        {
            g_config.virtual_stock_standard_strength = legacyVirtualStockStrength;
        }
    }
    // Previous-candidate boolean two_hand_smoothing: false/absent keeps the
    // 0.0 default, true migrates to the full 25.0 strength. An explicit
    // numeric key always wins, and the numeric key is what gets saved.
    if (loadedLegacyTwoHandSmoothing && !loadedTwoHandSmoothingStrength)
        g_config.two_hand_smoothing_strength = legacyTwoHandSmoothingStrength;
    if(!flashlightMappingV2) {
        for(auto& button:g_config.flashlight_button)
            if(button==0) button=flashlight_input::kGripDefault;
    }
    if (!loadedHolsterRadius) g_config.weapon_holster_radius_m=g_config.weapon_body_zone_radius_m;
    for (int i = 0; i < kReachVehicleTrimSlots; ++i)
    {
        // A valid numeric key is an explicit seat override even if a stale or
        // hand-edited tombstone appears later in the file.
        if (reachNumericKeyLoaded[i])
            g_config.reach_vehicle_cam_use_universal[i] = false;
        if (!g_config.reach_vehicle_cam_use_universal[i])
            continue;
        g_config.reach_vehicle_cam_forward_set[i] = false;
        g_config.reach_vehicle_cam_up_set[i] = false;
        g_config.reach_vehicle_cam_right_set[i] = false;
    }
    if (loadedConfigVersion < 3 && loadedScopeZoom)
    {
        // Version 3 moved the scope to the gameplay/bullet origin. The former
        // crosshair-origin calibration is too wide there, so preserve the
        // user's relative setting while doubling the available lens strength.
        g_config.scope_zoom *= 2.0f;
        LOG("config: migrated scope zoom to the stronger gameplay-origin lens");
    }
    if (loadedConfigVersion < 4 && loadedScopeZoom)
    {
        // The first gameplay-origin headset pass was still much too wide. Keep
        // the prior relative setting but move it into the tighter 6x..24x lens.
        g_config.scope_zoom *= 1.75f;
        LOG("config: migrated scope zoom to the tighter world-only lens");
    }
    if (loadedConfigVersion < 6 && !g_config.two_hand_coherent_aim)
    {
        g_config.two_hand_coherent_aim = true;
        LOG("config: migrated two_hand_coherent_aim to enabled for v6; set two_hand_coherent_aim = 0 to restore the legacy path");
    }
    if (loadedLegacyCurvature)
    {
        // Version 1 stored a signed value whose physical delta was value*0.1.
        // Preserve that exact curve when migrating to normalized 0(flat)..1(curved).
        const float legacyDelta = g_config.hud_curvature * 0.1f;
        g_config.hud_curvature = (0.30f - legacyDelta) / 0.60f;
    }
    // One direct-drive switch for every title (aim_direct_drive, default 1). A file from before it set each lane
    // on its own (reach_direct_drive = 3, legacy_direct_drive = 1). Those old keys can only turn it on: a 0 in
    // them was the old default, never a choice, and only dev builds ever wrote them.
    if (!aimDirectDriveSeen && (g_config.legacy_direct_drive || g_config.reach_direct_drive == 3))
        g_config.aim_direct_drive = 1;
    // Co-op safe shots gained a third value, 2 = automatic, which is the default. A file written before that knew
    // only off and on, and its 1 was the default rather than a choice between always and automatic, so it becomes
    // automatic. Off stays off; a 1 written since (coop_safe_shots_version = 2) is a chosen "always" and stays.
    if (!coopSafeShotsV2 && g_config.coop_safe_shots == 1)
    {
        g_config.coop_safe_shots = 2;
        LOG("config: co-op safe shots moved from on to automatic (on whenever anyone else is in your session); "
            "choose Always on the Advanced page to keep it on everywhere");
    }
    Config_ApplyAimModes();
    if (!twoHandZoneBaselineV1 && loadedTwoHandZoneRight) {
        // The grab zone's default side offset moved from 0.030 m to 0.080 m, and every saved file holds the
        // old default. Keep each saved adjustment relative to it (a file at the old default reads 0.080).
        // Before Clamp, which applies the new range. In memory only: the next save writes it.
        constexpr float kLegacyTwoHandZoneRight=0.03f,kTwoHandZoneRight=0.08f;
        g_config.two_hand_zone_right_m+=kTwoHandZoneRight-kLegacyTwoHandZoneRight;
    }
    Clamp();
    NormalizeVirtualStockProximityPair();
    NormalizeVirtualStockHybridSeatPair();
    NormalizeVirtualStockHybridHorizontalPair();
    if (!leftHandForwardResetV1) {
        // left_hand_forward_m is now an extra on top of each game's left-gun position, no longer the
        // whole standoff measured from the old -0.063 m default. Keep each saved adjustment relative to
        // that default (a file at the old default reads 0). In memory only: the next save writes it.
        constexpr float kLegacyLeftHandForward=-0.063f;
        if (loadedSharedLeftHandForward)
            g_config.left_hand_forward_m=std::clamp(
                g_config.left_hand_forward_m-kLegacyLeftHandForward,-0.15f,0.30f);
        for (int field=0;field<kTunableFieldCount;++field)
            if (kTunableFields[field].member==&TitleTunables::left_hand_forward_m)
                for (int profile=0;profile<kTitleProfileCount;++profile)
                    if (g_profileFieldSet[profile][field])
                        g_config.title_profiles[profile].left_hand_forward_m-=kLegacyLeftHandForward;
    }
    // Rows saved while the support trigger's contexts were rows' choices become the Input combinations they stood for.
    {
        bool converted=false;
        for(auto& row:g_config.vr_bindings)
            converted|=vr_mapping::ConvertContextRows(row,g_config.grip_contextual_trigger,g_config.trigger_air_equipment);
        if(converted)
            LOG("config: VR Mappings rows on the support trigger by hand became Input combinations (gun zooms %d, "
                "air uses equipment %d)",g_config.grip_contextual_trigger?1:0,g_config.trigger_air_equipment?1:0);
    }
    // A file from before the rows held inputs: the right stick's crouch and sprint and the two Input combinations become
    // the inputs they stood for, in every game's rows (vr_mapping::ConvertSettingsToInputs). The settings are not read again.
    if(inputsVersion<1)
    {
        for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
            vr_mapping::ConvertSettingsToInputs(g_config.vr_inputs[title],static_cast<GameTitle>(title+1),
                g_config.right_stick_crouch,g_config.grip_contextual_trigger,g_config.trigger_air_equipment);
        LOG("config: VR Mappings rows now hold the right stick (crouch and sprint %d), the gun grip's zoom (%d) and the "
            "empty grip's equipment (%d) as inputs",g_config.right_stick_crouch?1:0,
            g_config.grip_contextual_trigger?1:0,g_config.trigger_air_equipment?1:0);
    }
    // A file from before the Pause row: Y + B sending Start turned off leaves the row empty.
    if(inputsVersion<2&&!g_config.y_b_start_chord)
    {
        g_config.vr_mod_bindings[vr_mapping::Pause]={};
        LOG("config: Y + B sending Start was off, so the Pause row has no inputs");
    }
    // A file from when the default controls left rows on Automatic: those rows take the default's own control, so the
    // VR controls no longer follow the player's flat MCC layout. VR Mappings off keeps its rows: the plain route reads
    // none of them.
    if(inputsVersion<3&&g_config.vr_action_mapping)
        if(const unsigned moved=default_controls::FillAutomatic(g_config))
            LOG("config: %u VR Mappings rows left on Automatic took the default controls' own",moved);
    if (!halo4HudHeightV2) {
        // Halo 4's HUD height ran the other way until it was corrected (a positive value lowered the HUD), so a
        // saved Halo 4 height is turned over once: the HUD stays exactly where the player put it. In memory only:
        // the next save writes it with halo4_hud_height_version = 2.
        for (int field=0;field<kTunableFieldCount;++field)
            if (kTunableFields[field].member==&TitleTunables::hud_vertical_offset&&
                g_profileFieldSet[kTitleProfileHalo4][field])
                g_config.title_profiles[kTitleProfileHalo4].hud_vertical_offset=
                    -g_config.title_profiles[kTitleProfileHalo4].hud_vertical_offset;
    }
    ResolveTitleProfiles();
    LOG("config: loaded (screen %.2fm wide at %.2fm)", g_config.screen_width_m, g_config.screen_distance_m);
}

void Config_ApplyAimModes()
{
    // Each lane reads its own field; the player's switch is aim_direct_drive. Both lanes' hooks are installed
    // whenever the title loads and re-read these every tick, so a change here takes effect live.
    g_config.legacy_direct_drive = g_config.aim_direct_drive ? 1 : 0;
    // A dev build's Reach research modes (1, 2, 4) stay as set.
    if (g_config.reach_direct_drive == 0 || g_config.reach_direct_drive == 3)
        g_config.reach_direct_drive = g_config.aim_direct_drive ? 3 : 0;
}

void ConfigLoadMigrating(const wchar_t* primaryPath, const wchar_t* legacyPath)
{
    if (FileExists(primaryPath))
    {
        ConfigLoad(primaryPath);
        return;
    }
    if (legacyPath && FileExists(legacyPath))
    {
        ConfigLoad(legacyPath);
        g_path = primaryPath;
        ConfigSave();
        LOG("config: imported legacy %ls into %ls (legacy file retained)",
            legacyPath, primaryPath);
        return;
    }
    ConfigLoad(primaryPath);
}

static std::atomic<bool> g_configReadOnly{false};
void Config_SetReadOnly(bool readOnly)
{
    g_configReadOnly.store(readOnly);
}

const wchar_t* Config_Path()
{
    return g_path.c_str();
}

void ConfigSave()
{
    const std::lock_guard lock(g_profileMutex);
    if (g_path.empty() || g_configReadOnly.load())
        return;
    Config_ApplyWeaponProfile(g_observedWeaponIdentity);
    Clamp();
    NormalizeVirtualStockProximityPair();
    NormalizeVirtualStockHybridSeatPair();
    NormalizeVirtualStockHybridHorizontalPair();
    // C-TITLE-1: whatever the player just tuned belongs to the active
    // title's profile (or the shared defaults when no game is active).
    Config_StoreLiveTunables();
    FILE* f = nullptr;
    _wfopen_s(&f, g_path.c_str(), L"wt");
    if (!f)
    {
        LOG("config: FAILED to write %ls", g_path.c_str());
        return;
    }
    const Config d{}; // struct defaults, printed beside each setting

    fprintf(f, "# ===================================================================\n");
    fprintf(f, "#  Halo MCC VR settings\n");
    fprintf(f, "# ===================================================================\n");
    fprintf(f, "#  Edit this file in Notepad with MCC CLOSED, or press F1 in game.\n");
    fprintf(f, "#  Every setting below lists what it does and its default value, so\n");
    fprintf(f, "#  you can always put one back the way it was.\n");
    fprintf(f, "#  This ONE file is shared by every supported MCC game. Your comfort,\n");
    fprintf(f, "#  control, aiming, and presentation preferences follow you between\n");
    fprintf(f, "#  titles; each game keeps its own internal engine calibration.\n");
    fprintf(f, "#\n");
    fprintf(f, "#  * Lost? Close MCC, DELETE this file, and start the game. A fresh\n");
    fprintf(f, "#    one with all the defaults is written for you. Nothing else in\n");
    fprintf(f, "#    the mod folder is affected.\n");
    fprintf(f, "#  * The F1 menu rewrites this whole file when it saves, so notes you\n");
    fprintf(f, "#    type in yourself will disappear. Your VALUES are always kept.\n");
    fprintf(f, "#  * resolution_scale needs a full game restart. Everything else\n");
    fprintf(f, "#    takes effect the next time you launch, or live in the F1 menu.\n");
    fprintf(f, "#  * A line the mod does not recognize, or a value that is not a\n");
    fprintf(f, "#    number, is ignored and noted in HaloMCCVR.log. It cannot break\n");
    fprintf(f, "#    the mod, and out-of-range numbers are pulled back into range.\n");
    fprintf(f, "# ===================================================================\n\n");
    fprintf(f, "config_version = %d\n\n", g_config.config_version);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  OPENXR & COMFORT\n");
    fprintf(f, "#  Headset, controller feedback, and the shared 2D menu screen.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# OpenXR controller vibration strength, 0 = off and 1 = full.\n");
    fprintf(f, "# (default %.2f, range 0 to 1)\n", d.haptic_intensity);
    fprintf(f, "haptic_intensity = %.2f\n\n", g_config.haptic_intensity);
    fprintf(f, "# Headset micro-smoothing, 0 = raw. Try 0.05 only for micro-jitter.\n");
    fprintf(f, "# (default %.2f, range 0 to 0.10)\n", d.headset_smoothing);
    fprintf(f, "headset_smoothing = %.2f\n\n", g_config.headset_smoothing);
    fprintf(f, "# Width of the virtual screen in meters (menus / 2D mode).\n");
    fprintf(f, "# (default %.2f, range 0.5 to 20)\n", d.screen_width_m);
    fprintf(f, "screen_width_m = %.2f\n\n", g_config.screen_width_m);
    fprintf(f, "# Distance from your head to that screen, in meters.\n");
    fprintf(f, "# (default %.2f, range 0.3 to 20)\n", d.screen_distance_m);
    fprintf(f, "screen_distance_m = %.2f\n\n", g_config.screen_distance_m);
    fprintf(f, "# Mono rendering. 1 = the game is drawn once a frame and both eyes see that one picture: no 3D\n");
    fprintf(f, "# depth. For very large maps that are too slow in 3D. Halo: CE with its Anniversary graphics is\n");
    fprintf(f, "# always drawn in 3D. Not on headsets with angled (canted) displays, in the cutscene theatre, or\n");
    fprintf(f, "# with DLSS. (default %d)\n",
            d.mono_rendering ? 1 : 0);
    fprintf(f, "mono_rendering = %d\n\n", g_config.mono_rendering ? 1 : 0);
    fprintf(f, "# For performance reports. N = the mod times its own work in one frame out of every N and writes\n");
    fprintf(f, "# it to the log every 10 seconds (lines starting 'timing: the mod's own work'). The timed frame is\n");
    fprintf(f, "# a little slower, so leave this at 0 for normal play. Try 251. (default %d, 0 = off, least 8)\n",
            d.hook_cost_frames);
    fprintf(f, "hook_cost_frames = %d\n\n", g_config.hook_cost_frames);
    fprintf(f, "# Project Reclaimer only. Its menus, scoreboard, chat and other on-screen items are drawn over the\n");
    fprintf(f, "# game's picture after the headset's pictures are taken. 1 = they are shown as a panel in front of\n");
    fprintf(f, "# the 3D picture, which stays 3D. 0 = the picture goes flat while a menu is up. (default %d)\n",
            d.host_ui_panel ? 1 : 0);
    fprintf(f, "host_ui_panel = %d\n", g_config.host_ui_panel ? 1 : 0);
    fprintf(f, "# That panel's width and its distance from your head, in meters.\n");
    fprintf(f, "# (defaults %.2f and %.2f, ranges 0.5 to 20 and 0.3 to 20)\n", d.host_ui_panel_width_m,
            d.host_ui_panel_distance_m);
    fprintf(f, "host_ui_panel_width_m = %.2f\n", g_config.host_ui_panel_width_m);
    fprintf(f, "host_ui_panel_distance_m = %.2f\n\n", g_config.host_ui_panel_distance_m);
    fprintf(f, "# Project Reclaimer only. How its Hornet-class flyers climb and dive. 1 = with the buttons, as in\n");
    fprintf(f, "# MCC (right A climbs, the left grip dives). 0 = Project Reclaimer's classic Halo 3 controls: aim\n");
    fprintf(f, "# up or down to climb or dive. (default %d)\n", d.reclaimer_flight_buttons ? 1 : 0);
    fprintf(f, "reclaimer_flight_buttons = %d\n\n", g_config.reclaimer_flight_buttons ? 1 : 0);
    fprintf(f, "# Stereo 3D theatre for engine-confirmed cutscenes where the game\n");
    fprintf(f, "# has taken camera control away from the player. Shared by every\n");
    fprintf(f, "# supported title, including future title adapters. (default %d)\n",
             d.cutscene_theater_enabled ? 1 : 0);
    fprintf(f, "cutscene_theater_enabled = %d\n\n",
             g_config.cutscene_theater_enabled ? 1 : 0);
    fprintf(f, "# Theatre stereo separation: 0 = flat, 1 = natural IPD, 2 = double.\n");
    fprintf(f, "# (default %.2f, range 0 to 2)\n", d.cutscene_theater_depth);
    fprintf(f, "cutscene_theater_depth = %.2f\n\n",
             g_config.cutscene_theater_depth);
    fprintf(f, "# Swap the theatre's left/right images to reverse depth. (default %d)\n",
             d.cutscene_theater_flip_depth ? 1 : 0);
    fprintf(f, "cutscene_theater_flip_depth = %d\n\n",
             g_config.cutscene_theater_flip_depth ? 1 : 0);
    fprintf(f, "# Room-fixed theatre screen width and distance in meters. Its height\n");
    fprintf(f, "# follows the authored cinematic projection.\n");
    fprintf(f, "# (defaults %.2f wide at %.2f away)\n",
             d.cutscene_theater_width_m, d.cutscene_theater_distance_m);
    fprintf(f, "cutscene_theater_width_m = %.2f\n",
             g_config.cutscene_theater_width_m);
    fprintf(f, "cutscene_theater_distance_m = %.2f\n\n",
             g_config.cutscene_theater_distance_m);
    fprintf(f, "# Black cine bars on the theatre screen. The cutscene is drawn into the\n");
    fprintf(f, "# headset's render shape, which is taller than a TV picture, so without\n");
    fprintf(f, "# bars you see scene above and below the shot the game intends. This is\n");
    fprintf(f, "# the shape left showing between the bars: 1.78 = 16:9 (a TV), 2.39 =\n");
    fprintf(f, "# a wide cinema crop, 0 = no bars. The picture is never resized, only\n");
    fprintf(f, "# covered. (default %.2f, range 1.0 to 3.0, or 0)\n",
             d.cutscene_theater_matte_aspect);
    fprintf(f, "cutscene_theater_matte_aspect = %.2f\n",
             g_config.cutscene_theater_matte_aspect);
    fprintf(f, "# Slides that window up (positive) or down inside the frame, as a\n");
    fprintf(f, "# fraction of the picture height. (default %.2f, range -0.25 to 0.25)\n",
             d.cutscene_theater_matte_offset);
    fprintf(f, "cutscene_theater_matte_offset = %.2f\n\n",
             g_config.cutscene_theater_matte_offset);
    fprintf(f, "# Show the game's own subtitle text on the theatre screen. Subtitles\n");
    fprintf(f, "# are interface text the game draws after the two eye images are\n");
    fprintf(f, "# taken, which is why they appear on the monitor but not in the\n");
    fprintf(f, "# headset. Turn MCC's own subtitle setting on as well. (default %d)\n",
             d.cutscene_theater_subtitles ? 1 : 0);
    fprintf(f, "cutscene_theater_subtitles = %d\n\n",
             g_config.cutscene_theater_subtitles ? 1 : 0);
    fprintf(f, "# How much of the bottom of the game's frame is searched for that\n");
    fprintf(f, "# text, as a fraction of its height. Text drawn higher than this is\n");
    fprintf(f, "# never found, so raise it if subtitles do not appear.\n");
    fprintf(f, "# (default %.2f, range 0.05 to 1.0)\n",
             d.cutscene_theater_subtitle_band);
    fprintf(f, "cutscene_theater_subtitle_band = %.2f\n\n",
             g_config.cutscene_theater_subtitle_band);
    fprintf(f, "# Diagnostic. Paints that strip onto the theatre screen exactly as it\n");
    fprintf(f, "# was captured, with no text selection at all, so you can see what the\n");
    fprintf(f, "# mod is reading. Leave off for normal play. (default %d)\n",
             d.cutscene_theater_subtitle_debug ? 1 : 0);
    fprintf(f, "cutscene_theater_subtitle_debug = %d\n\n",
             g_config.cutscene_theater_subtitle_debug ? 1 : 0);
    fprintf(f, "# Where the F1 menu panel floats. You do not need to edit these by\n");
    fprintf(f, "# hand: grab the bar along the top of the panel with your right\n");
    fprintf(f, "# trigger to move it, and push the right stick up/down while holding\n");
    fprintf(f, "# to change how far away it is. \"Reset panel position\" in the menu's\n");
    fprintf(f, "# Advanced page puts all four back to the defaults below.\n");
    fprintf(f, "# (defaults %.2f / %.2f / %.2f / %.2f)\n",
             d.menu_distance_m, d.menu_width_m, d.menu_height_m, d.menu_side_m);
    fprintf(f, "# Distance from your head to the panel, in meters. (range %.2f to %.2f)\n",
             kMenuDistanceMin, kMenuDistanceMax);
    fprintf(f, "menu_distance_m = %.2f\n", g_config.menu_distance_m);
    fprintf(f, "# Panel width in meters; its height follows automatically. (range %.2f to %.2f)\n",
             kMenuWidthMin, kMenuWidthMax);
    fprintf(f, "menu_width_m = %.2f\n", g_config.menu_width_m);
    fprintf(f, "# Height offset in meters, negative = below your eye line. (range -%.2f to %.2f)\n",
             kMenuOffsetLimit, kMenuOffsetLimit);
    fprintf(f, "menu_height_m = %.2f\n", g_config.menu_height_m);
    fprintf(f, "# Sideways offset in meters, positive = to your right. (range -%.2f to %.2f)\n",
             kMenuOffsetLimit, kMenuOffsetLimit);
    fprintf(f, "menu_side_m = %.2f\n\n", g_config.menu_side_m);
    fprintf(f, "# Aim the primary controller at MCC / pause menus; trigger clicks.\n");
    fprintf(f, "# On by default; requires MCC desktop focus. Does not affect gameplay or F1.\n");
    fprintf(f, "game_menu_pointer = %d\n\n", g_config.game_menu_pointer ? 1 : 0);
    fprintf(f, "# Let OFXR Bridge (an OpenXR frame-generation layer) run in this game.\n");
    fprintf(f, "# Off by default: OFXR has crashed the mod a few seconds in. Off keeps\n");
    fprintf(f, "# OFXR out of this game only; other games still get it. Applies at launch.\n");
    fprintf(f, "allow_ofxr_bridge = %d\n\n", g_config.allow_ofxr_bridge ? 1 : 0);
    fprintf(f, "# Project Reclaimer: keep its picture no larger than your monitor (Display\n");
    fprintf(f, "# mode Borderless, Render resolution Native). A picture larger than the\n");
    fprintf(f, "# screen is suspected of stopping the graphics card on some PCs; 0 gives it\n");
    fprintf(f, "# the Picture panel's full size. From 0.10 the picture keeps the full size\n");
    fprintf(f, "# and only the window is fitted inside your monitor (0 leaves the window\n");
    fprintf(f, "# larger than the monitor). Applies at Project Reclaimer's next start.\n");
    fprintf(f, "reclaimer_fit_to_display = %d\n\n", g_config.reclaimer_fit_to_display ? 1 : 0);
    fprintf(f, "# Project Reclaimer: show a game it starts after the first (ODST, after Halo 3's menu) in the\n");
    fprintf(f, "# headset, by giving it the headset's graphics device. 0 = that game freezes in the headset.\n");
    fprintf(f, "reclaimer_share_device = %d\n\n", g_config.reclaimer_share_device ? 1 : 0);
    fprintf(f, "# Show the welcome page by itself once at the start of each launch.\n");
    fprintf(f, "# Tick \"Don't show this again\" on that page to clear this. The page\n");
    fprintf(f, "# itself always stays in the F1 menu, so you can read it again later.\n");
    fprintf(f, "# (default %d)\n", d.show_welcome ? 1 : 0);
    fprintf(f, "show_welcome = %d\n\n", g_config.show_welcome ? 1 : 0);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  CONTROLS & TURNING\n");
    fprintf(f, "#  Universal controller choices used in every supported title.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# VR turning with the right controller stick: 0 = snap, 1 = smooth.\n");
    fprintf(f, "# (default %d)\n", d.turn_smooth ? 1 : 0);
    fprintf(f, "turn_smooth = %d\n\n", g_config.turn_smooth ? 1 : 0);
    fprintf(f, "# Temporarily use smooth VR turning in vehicles; keep the saved on-foot mode.\n");
    fprintf(f, "# Default off. Native vehicle steering retains its own controls and limits.\n");
    fprintf(f, "vehicle_smooth_turn = %d\n\n", g_config.vehicle_smooth_turn ? 1 : 0);
    fprintf(f, "# Physical horizontal movement drives native walking while on foot.\n");
    fprintf(f, "# Default on. Controller aiming is preserved; walking stays head-relative.\n");
    fprintf(f, "roomscale_movement = %d\n\n", g_config.roomscale_movement ? 1 : 0);
    fprintf(f, "# Roomscale A/B: 1 = hysteresis (a lean under 3 cm never walks, it brakes by letting\n");
    fprintf(f, "# go of the stick, and a distance walked away during a tracking gap is still caught up),\n");
    fprintf(f, "# 0 = threshold (walks from 1.5 cm, brakes by reversing), exactly as it shipped in\n");
    fprintf(f, "# Alpha 0.6.0. (default %d)\n", d.roomscale_follow_law);
    fprintf(f, "roomscale_follow_law = %d\n\n", g_config.roomscale_follow_law);
    fprintf(f, "# Roomscale view recenter. While you are in the air or moving with the stick, your\n");
    fprintf(f, "# character cannot step under your head, so your view sits away from where the game\n");
    fprintf(f, "# aims and shoots from. These slide the view back over the character while that motion\n");
    fprintf(f, "# hides it: position only, never a turn, never your height. Whatever is left when you\n");
    fprintf(f, "# stop, your legs walk to you as before. They need roomscale_movement = 1.\n");
    fprintf(f, "# On a jump: a quick slide when the jump leaves the ground. (default %d)\n",
        d.roomscale_recenter_jump ? 1 : 0);
    fprintf(f, "roomscale_recenter_jump = %d\n", g_config.roomscale_recenter_jump ? 1 : 0);
    fprintf(f, "# How long that slide takes, in seconds. (default %.2f, range 0.05 to 1.00)\n",
        d.roomscale_recenter_jump_s);
    fprintf(f, "roomscale_recenter_jump_s = %.2f\n", g_config.roomscale_recenter_jump_s);
    fprintf(f, "# When you start moving: the same quick slide as the stick is first pushed.\n");
    fprintf(f, "# (default %d)\n", d.roomscale_recenter_move_start ? 1 : 0);
    fprintf(f, "roomscale_recenter_move_start = %d\n", g_config.roomscale_recenter_move_start ? 1 : 0);
    fprintf(f, "# How long that slide takes, in seconds. (default %.2f, range 0.05 to 1.00)\n",
        d.roomscale_recenter_move_start_s);
    fprintf(f, "roomscale_recenter_move_start_s = %.2f\n", g_config.roomscale_recenter_move_start_s);
    fprintf(f, "# While moving: a steady slide for as long as the stick is held. It need not finish.\n");
    fprintf(f, "# (default %d)\n", d.roomscale_recenter_moving ? 1 : 0);
    fprintf(f, "roomscale_recenter_moving = %d\n", g_config.roomscale_recenter_moving ? 1 : 0);
    fprintf(f, "# Its speed, in metres of offset closed per second. (default %.2f, range 0.10 to 3.00)\n",
        d.roomscale_recenter_moving_speed);
    fprintf(f, "roomscale_recenter_moving_speed = %.2f\n\n", g_config.roomscale_recenter_moving_speed);
    fprintf(f, "# Motion tracker follows your head. You aim with your hand and look with your head, and the\n");
    fprintf(f, "# game turns its motion tracker with the aim. 1 = contacts are placed by where your head faces,\n");
    fprintf(f, "# so sweeping the gun moves nothing and turning your head turns them. 0 = the game's own\n");
    fprintf(f, "# tracker, turned by the gun. A game that has no head-relative tracker yet keeps its own\n");
    fprintf(f, "# (log: TRACKER HEAD <game>). Halo 4 starts each level on the game's own tracker and changes\n");
    fprintf(f, "# over once it has checked that the heading it replaces follows your aim: a few seconds on\n");
    fprintf(f, "# foot with aim_direct_drive = 1. Halo 2 does the same, at your first aim movement in a\n");
    fprintf(f, "# level. Halo: CE follows your head from about a second into a level (CE's own tracker\n");
    fprintf(f, "# turns with the turn stick only, never with the gun). Live in F1 > HUD. (default %d)\n",
        d.tracker_follows_head ? 1 : 0);
    fprintf(f, "tracker_follows_head = %d\n", g_config.tracker_follows_head ? 1 : 0);
    fprintf(f, "# Motion tracker dots follow HUD height. Halo 3's HUD height moves the tracker but not the\n");
    fprintf(f, "# dots on it, so every dot, your own included, is drawn below the tracker (a quarter of its radius\n");
    fprintf(f, "# at the default height). 1 = the dots are drawn on the tracker wherever HUD height puts it.\n");
    fprintf(f, "# 0 = where the game draws them. Halo 3 only (log: TRACKER HUD HEIGHT <game>).\n");
    fprintf(f, "# Live in F1 > HUD. (default %d)\n", d.tracker_blips_follow_hud_height ? 1 : 0);
    fprintf(f, "tracker_blips_follow_hud_height = %d\n", g_config.tracker_blips_follow_hud_height ? 1 : 0);
    fprintf(f, "# A second game in one session starts at the first try. When a game ends its DLL unloads and\n");
    fprintf(f, "# MCC loads a fresh one; this mod's own hold on Halo: Reach's DLL used to defeat that, and the\n");
    fprintf(f, "# next Reach game bounced back to the lobby once. 1 = MCC's reload waits (6 s at most) for\n");
    fprintf(f, "# the mod to let go. 0 = as before. No menu control (log: UNLOAD GATE). (default %d)\n",
        d.title_unload_wait ? 1 : 0);
    fprintf(f, "title_unload_wait = %d\n", g_config.title_unload_wait ? 1 : 0);
    fprintf(f, "# Sound follows your head. 1 = you hear the world from the way your head faces. 0 = from the\n");
    fprintf(f, "# way the gun points, as the game does it. Halo: Reach, Halo 3, Halo 4 and ODST; a game with no head-relative\n");
    fprintf(f, "# sound yet keeps its own (log: AUDIO HEAD <game>). Live in F1 > Comfort. (default %d)\n",
        d.audio_follows_head ? 1 : 0);
    fprintf(f, "audio_follows_head = %d\n", g_config.audio_follows_head ? 1 : 0);
    fprintf(f, "# Markers use your headset's field of view. 1 = Halo: Reach's objective markers are projected\n");
    fprintf(f, "# with the field of view each eye is drawn with, so a marker stays on its point when you turn\n");
    fprintf(f, "# your head. 0 = with the game's flat-screen field of view, as before: a marker is then right\n");
    fprintf(f, "# only where you look straight at it and is drawn about twice as far off-centre everywhere\n");
    fprintf(f, "# else. Halo: Reach only. Tested with an objective marker; markers on other players are\n");
    fprintf(f, "# untested (log: NAV MARKERS <game>). Live in F1 > HUD. (default %d)\n",
        d.nav_markers_on_target ? 1 : 0);
    fprintf(f, "nav_markers_on_target = %d\n", g_config.nav_markers_on_target ? 1 : 0);
    fprintf(f, "# Markers follow your head, not your gun. 1 = whether an objective marker shows, or its\n");
    fprintf(f, "# off-screen arrow does, depends on where you look. 0 = on where the gun points, as before.\n");
    fprintf(f, "# Halo: Reach and Halo 4 (log: NAV MARKERS <game>). Live in F1 > HUD > Waypoints. (default %d)\n",
        d.nav_markers_follow_head ? 1 : 0);
    fprintf(f, "nav_markers_follow_head = %d\n", g_config.nav_markers_follow_head ? 1 : 0);
    fprintf(f, "# Off-screen arrows stay in view. 1 = when an objective marker or a marked player is out of\n");
    fprintf(f, "# view, its arrow is drawn on a ring around where your head faces, at most 30 degrees out, in\n");
    fprintf(f, "# one place for both eyes. 0 = the game's own ring, which is far enough out that only one eye\n");
    fprintf(f, "# sees the arrow. Covers objective markers and the markers the HUD anchors on objects and\n");
    fprintf(f, "# players. Does NOT touch: vehicle and lock-on reticles, damage arrows. Needs\n");
    fprintf(f, "# nav_markers_on_target. Halo: Reach only. Takes effect at the next level.\n");
    fprintf(f, "# (default %d)\n", d.nav_marker_arrow_in_view ? 1 : 0);
    fprintf(f, "nav_marker_arrow_in_view = %d\n", g_config.nav_marker_arrow_in_view ? 1 : 0);
    fprintf(f, "# How far from the centre of your view those arrows sit: a fraction of the field both eyes see\n");
    fprintf(f, "# (never more than 30 degrees, so the top of the range may change nothing on a wide headset).\n");
    fprintf(f, "# Lower brings them closer to the centre. 0 = the game's own ring.\n");
    fprintf(f, "# Live in F1 > HUD > Waypoints. (default %.2f, range 0 to 1)\n", d.nav_marker_ring);
    fprintf(f, "nav_marker_ring = %.2f\n\n", g_config.nav_marker_ring);
    fprintf(f, "# Player tags: the service tag and arrow over each teammate. Halo 3 (MCC and Project\n");
    fprintf(f, "# Reclaimer) and ODST. Objective markers are not changed; a flag, ball or bomb carrier's\n");
    fprintf(f, "# tag is a player's tag and is.\n");
    fprintf(f, "# Live in F1 > HUD > Waypoints.\n");
    fprintf(f, "# Show them at all. (default %d)\n", d.player_tags_show ? 1 : 0);
    fprintf(f, "player_tags_show = %d\n", g_config.player_tags_show ? 1 : 0);
    fprintf(f, "# Size, times the game's own. (default %.2f, range 0.25 to 2)\n", d.player_tags_scale);
    fprintf(f, "player_tags_scale = %.2f\n", g_config.player_tags_scale);
    fprintf(f, "# Opacity, times the game's own. (default %.2f, range 0 to 1)\n", d.player_tags_opacity);
    fprintf(f, "player_tags_opacity = %.2f\n", g_config.player_tags_opacity);
    fprintf(f, "# Hide tags farther than this many metres from you; they fade out over the last 5 m.\n");
    fprintf(f, "# 0 = no limit. (default %.0f, range 0 to 300)\n", d.player_tags_max_distance_m);
    fprintf(f, "player_tags_max_distance_m = %.0f\n\n", g_config.player_tags_max_distance_m);
    fprintf(f, "# Degrees per snap turn.\n");
    fprintf(f, "# (default %.0f, range 5 to 90)\n", d.turn_snap_deg);
    fprintf(f, "turn_snap_deg = %.0f\n\n", g_config.turn_snap_deg);
    fprintf(f, "# Smooth turn speed in degrees per second.\n");
    fprintf(f, "# (default %.0f, range 30 to 360)\n", d.turn_smooth_deg_s);
    fprintf(f, "turn_smooth_deg_s = %.0f\n\n", g_config.turn_smooth_deg_s);
    fprintf(f, "# Halo 2 only: the gamepad's Back/View button is MCC's instant\n");
    fprintf(f, "# Classic <-> Anniversary graphics switch. 0 = the mod swallows that\n");
    fprintf(f, "# button in Halo 2 so a physical pad (Steam Controller) cannot flip the\n");
    fprintf(f, "# renderer mid-game; 1 = pass it through. Holding the left stick\n");
    fprintf(f, "# click with the controller at your head switches renderers on\n");
    fprintf(f, "# purpose in either setting. Y+B still pauses on the pad.\n");
    fprintf(f, "# (default %d)\n", d.halo2_gamepad_graphics_switch ? 1 : 0);
    fprintf(f, "halo2_gamepad_graphics_switch = %d\n\n",
            g_config.halo2_gamepad_graphics_switch ? 1 : 0);
    fprintf(f, "# Halo 2: the game's camera shake from firing, damage and explosions in the headset view.\n");
    fprintf(f, "# (default %d)\n", d.halo2_camera_shake ? 1 : 0);
    fprintf(f, "halo2_camera_shake = %d\n\n", g_config.halo2_camera_shake ? 1 : 0);
    fprintf(f, "# Hold this controller next to your head to use the left stick as D-pad:\n");
    fprintf(f, "# 0 = left controller, 1 = right controller.\n");
    fprintf(f, "# While held, clicking the left stick presses the controller's\n");
    fprintf(f, "# left centre button (Back/View) - ODST's map/objectives screen.\n");
    fprintf(f, "# (default %d)\n", d.dpad_hand);
    fprintf(f, "dpad_hand = %d\n\n", g_config.dpad_hand);
    fprintf(f, "# Head-gesture activation radius in metres. A smaller radius requires\n");
    fprintf(f, "# the selected controller to be closer to your head.\n");
    fprintf(f, "# (default %.2f, range 0.10 to 0.50)\n", d.dpad_head_radius);
    fprintf(f, "dpad_head_radius = %.3f\n\n", g_config.dpad_head_radius);
    fprintf(f, "# Raising the hand while walking leaves the stick walking until it returns to centre; then it is the\n");
    fprintf(f, "# D-pad. 0: the stick is the D-pad at once and walking stops. (default %d)\n", d.dpad_keep_moving ? 1 : 0);
    fprintf(f, "dpad_keep_moving = %d\n\n", g_config.dpad_keep_moving ? 1 : 0);
    fprintf(f, "# Project Reclaimer: X with the left controller at the head (within dpad_head_radius) presses the talk\n");
    fprintf(f, "# key, so voice chat follows Reclaimer's Hold or Toggle talk mode; Y shows the scoreboard; A and B with\n");
    fprintf(f, "# the right controller vote yes and no.\n");
    fprintf(f, "# The talk key's Windows virtual-key code, if you rebound Push to talk in Reclaimer: 86 is V, 66 is B\n");
    fprintf(f, "# (team), 78 is N (proximity). (default %d)\n", d.reclaimer_talk_key);
    fprintf(f, "reclaimer_talk_key = %d\n\n", g_config.reclaimer_talk_key);
    fprintf(f, "# The scoreboard key's Windows virtual-key code, if you rebound it: 9 is Tab. (default %d)\n",
            d.reclaimer_scoreboard_key);
    fprintf(f, "reclaimer_scoreboard_key = %d\n\n", g_config.reclaimer_scoreboard_key);
    fprintf(f, "# The vote keys' Windows virtual-key codes, if you rebound them: 116 is F5, 117 is F6. (defaults %d, %d)\n",
            d.reclaimer_vote_yes_key, d.reclaimer_vote_no_key);
    fprintf(f, "reclaimer_vote_yes_key = %d\n", g_config.reclaimer_vote_yes_key);
    fprintf(f, "reclaimer_vote_no_key = %d\n\n", g_config.reclaimer_vote_no_key);
    fprintf(f, "# Quest 3 alternate D-pad: touch the left controller's thumb rest,\n");
    fprintf(f, "# then move the right stick. Always uses the physical left thumb rest\n");
    fprintf(f, "# and physical right stick, including in left-handed mode.\n");
    fprintf(f, "# (default %d)\n", d.quest_thumbrest_dpad ? 1 : 0);
    fprintf(f, "quest_thumbrest_dpad = %d\n\n", g_config.quest_thumbrest_dpad ? 1 : 0);
    fprintf(f,"# Block the configured flashlight gamepad button during gameplay; XR grip/two-hand aim and menus stay available.\n");
    fprintf(f,"flashlight_mapping_version = 2\n");
    fprintf(f,"disable_flashlight_input = %d\n",g_config.disable_flashlight_input?1:0);
    fprintf(f,"# Match MCC's layout per title: 0=RB,1=LB,2=DUp,3=DDown,4=DLeft,5=DRight,6=X,7=Y,8=A,9=B,10=L3,11=R3,12=Back,13=LT,14=RT.\n");
    for(unsigned i=0;i<weapon_interaction::kTitleCount;++i)
        fprintf(f,"flashlight_button_%s = %d\n",weapon_interaction::kTitleKeys[i],g_config.flashlight_button[i]);
    fprintf(f,"\n");
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  RETICLE & AIMING\n");
    fprintf(f, "#  Portable aiming preferences; title adapters supply engine offsets.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Halo 4 first-person hands: put the engine's own gun and arms on your\n");
    fprintf(f, "# controller instead of on your head. Off returns Halo 4 to its stock\n");
    fprintf(f, "# head-mounted weapon. (default %d)\n", d.halo4_hands);
    fprintf(f, "halo4_hands = %d\n\n", g_config.halo4_hands);
    fprintf(f, "# Nudge where the gun sits, in metres, in your controller's own frame.\n");
    fprintf(f, "# forward pushes it away from you, vertical raises it, lateral moves it\n");
    fprintf(f, "# toward your right hand. (defaults %.2f/%.2f/%.2f, range -0.50 to 0.50)\n",
            d.halo4_hand_forward_m, d.halo4_hand_vertical_m,
            d.halo4_hand_lateral_m);
    fprintf(f, "halo4_hand_forward_m = %.2f\n", g_config.halo4_hand_forward_m);
    fprintf(f, "halo4_hand_vertical_m = %.2f\n", g_config.halo4_hand_vertical_m);
    fprintf(f, "halo4_hand_lateral_m = %.2f\n\n", g_config.halo4_hand_lateral_m);
    fprintf(f, "# Left-handed: mirror the Halo 4 hand placement. (default %d)\n",
            d.halo4_hands_mirrored);
    fprintf(f, "halo4_hands_mirrored = %d\n\n", g_config.halo4_hands_mirrored);
    fprintf(f, "# Halo 4 authored Mjolnir helmet/visor frame. This does not hide\n");
    fprintf(f, "# shields, radar, ammo, objectives, or the aiming reticle. In the campaign the\n");
    fprintf(f, "# frame is a dark band above the HUD and a larger one below it. (default %d)\n",
            d.halo4_helmet ? 1 : 0);
    fprintf(f, "halo4_helmet = %d\n\n", g_config.halo4_helmet ? 1 : 0);
    fprintf(f, "# Hide gameplay HUD and VR reticle; menus remain available.\nhide_hud = %d\n\n", g_config.hide_hud ? 1 : 0);
    fprintf(f, "# Temporarily reveal a hidden HUD with an empty support hand near your head.\n"
        "hud_reveal_near_head = %d\nhud_reveal_radius_m = %.3f\n\n",
        g_config.hud_reveal_near_head?1:0,g_config.hud_reveal_radius_m);
    fprintf(f, "# Optional CE Anniversary lens-flare suppression. Does not alter world lighting.\nce_anniversary_disable_lens_flares = %d\n\n", g_config.ce_anniversary_disable_lens_flares ? 1 : 0);
    fprintf(f, "# Each owned dual-wielded gun follows its own controller.\nindependent_dual_aim = %d\n\n", g_config.independent_dual_aim ? 1 : 0);
    fprintf(f, "# Use verified visible weapon muzzle origin and orientation.\ngun_barrel_aim = %d\n\n", g_config.gun_barrel_aim ? 1 : 0);
    fprintf(f, "# Shots leave the visible muzzle but fly through the converged aim's point (the reticle):\n"
               "# they land where the shot from the eye would have. Needs converged_aim; gun_barrel_aim wins.\n"
               "# (default %d)\n"
               "shots_from_muzzle = %d\n\n", d.shots_from_muzzle ? 1 : 0, g_config.shots_from_muzzle ? 1 : 0);
    fprintf(f, "# Co-op safe shots. 2 = AUTOMATIC (the default): on whenever anyone else is in your campaign or\n"
               "# Spartan Ops session, and off when the game says you are alone, or in Firefight or a custom game.\n"
               "# Halo CE's session is not read, so there it is always on.\n"
               "# 1 = ALWAYS: nothing changes a shot as it is fired (no muzzle or barrel origin, no fire-time re-aim, no\n"
               "# separate dual-wield aim, no seat redirect), so every player's game fires it the same way.\n"
               "# Your hand still aims; shots leave the eye. True physical melee presses the game's melee\n"
               "# instead of striking on contact, in Halo 2 your hand aims only with\n"
               "# direct drive on, and seats in Halo CE and ODST and passenger seats in Reach keep the game's\n"
               "# own camera.\n"
               "# 0 = OFF, for playing alone only: with it off, your first shot in a campaign co-op or Spartan Ops\n"
               "# session ends the session for everyone. (default %d)\n"
               "coop_safe_shots = %d\n"
               "# Which values the line above can hold (2: off, always and automatic). Leave it as it is.\n"
               "coop_safe_shots_version = 2\n\n", d.coop_safe_shots, g_config.coop_safe_shots);
    fprintf(f, "# Aim lead, on foot in Halo 3, ODST and Reach: the aim written for each shot is carried ahead by\n"
               "# this many simulation ticks of the aim's own motion, so a shot fired while you turn lands where\n"
               "# the reticle is and not where it was a tick ago. Only the game's aim is led: the reticle, the\n"
               "# gun and the scope do not move. The same in solo and co-op. 0 = off.\n"
               "# (default %.2f, range 0 to %.0f)\n"
               "aim_lead_ticks = %.2f\n\n", d.aim_lead_ticks, aim_lead::kMaxTicks, g_config.aim_lead_ticks);
    fprintf(f, "# Floating VR-crosshair smoothing only; bullets stay raw.\n");
    fprintf(f, "# (default %.2f, range 0 to 0.95)\n", d.aim_stabilization);
    fprintf(f, "aim_stabilization = %.2f\n\n", g_config.aim_stabilization);
    fprintf(f, "# Hand smoothing. Steadies your tracked hands before anything reads them, so aim, the arms,\n");
    fprintf(f, "# two-handed holds and reload all see the same steadied hands. A still hand is held steady; a\n");
    fprintf(f, "# fast move passes through with little lag. Swing melee and arm-swing running read your raw\n");
    fprintf(f, "# motion. Your headset is not smoothed. Live in F1 > Weapon & Aim > Hand smoothing.\n");
    fprintf(f, "# 1 = on, 0 = your hands exactly as the headset tracks them. (default %d)\n", d.hand_smoothing ? 1 : 0);
    fprintf(f, "hand_smoothing = %d\n", g_config.hand_smoothing ? 1 : 0);
    fprintf(f, "# Which hands: 0 both, 1 only the hand you aim with, 2 only your other hand. (default %d)\n",
        d.hand_smoothing_hands);
    fprintf(f, "hand_smoothing_hands = %d\n", g_config.hand_smoothing_hands);
    fprintf(f, "# Steadiness while still, in Hz, for position and for rotation: lower is steadier but slower\n");
    fprintf(f, "# to follow a slow move. 0 turns that half off. (defaults %.1f and %.1f, range 0 to 30)\n",
        d.hand_smoothing_pos_min_hz, d.hand_smoothing_rot_min_hz);
    fprintf(f, "hand_smoothing_pos_min_hz = %.2f\n", g_config.hand_smoothing_pos_min_hz);
    fprintf(f, "hand_smoothing_rot_min_hz = %.2f\n", g_config.hand_smoothing_rot_min_hz);
    fprintf(f, "# Follow speed: how quickly smoothing lets go as the hand moves faster (per metre a second)\n");
    fprintf(f, "# or the wrist turns faster (per radian a second). Raise it if slow moves feel laggy, lower\n");
    fprintf(f, "# it if a moving hand still shakes. (defaults %.0f and %.1f)\n",
        d.hand_smoothing_pos_beta, d.hand_smoothing_rot_beta);
    fprintf(f, "hand_smoothing_pos_beta = %.1f\n", g_config.hand_smoothing_pos_beta);
    fprintf(f, "hand_smoothing_rot_beta = %.2f\n", g_config.hand_smoothing_rot_beta);
    fprintf(f, "# Speed estimate smoothing, in Hz. Rarely needs changing. (default %.0f, range 0.1 to 100)\n",
        d.hand_smoothing_speed_cutoff_hz);
    fprintf(f, "hand_smoothing_speed_cutoff_hz = %.1f\n\n", g_config.hand_smoothing_speed_cutoff_hz);
    fprintf(f, "# Aim crosshair in stereo: a floating reticle where the weapon actually\n");
    fprintf(f, "# shoots (the game's own reticle follows your head, not your hand).\n");
    fprintf(f, "# (default %d)\n", d.crosshair ? 1 : 0);
    fprintf(f, "crosshair = %d\n\n", g_config.crosshair ? 1 : 0);
    fprintf(f, "# How far away that crosshair floats, in meters.\n");
    fprintf(f, "# (default %.1f, range 2 to 50)\n", d.crosshair_distance_m);
    fprintf(f, "crosshair_distance_m = %.1f\n\n", g_config.crosshair_distance_m);
    fprintf(f, "# Apparent size of the crosshair, in degrees. Each game keeps its own (halo3_crosshair_size_deg\n");
    fprintf(f, "# and so on, at the end of this file; F1 > Crosshair edits the running game's). This is the\n");
    fprintf(f, "# size a game's profile starts from when it names none. The games below start at their own\n");
    fprintf(f, "# size instead, whatever this is: their crosshair art fills a different share of the crosshair.\n");
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
        if (d.title_profiles[profile].crosshair_size_deg != d.crosshair_size_deg)
            fprintf(f, "#   %s starts at %.2f\n", Config_TitleProfileName(profile),
                d.title_profiles[profile].crosshair_size_deg);
    fprintf(f, "# (default %.2f, range 0.3 to 20)\n", d.crosshair_size_deg);
    fprintf(f, "crosshair_size_deg = %.2f\n\n", g_config.base_tunables.crosshair_size_deg);
    fprintf(f, "# Crosshair opacity and brightness. 1 = as the game draws it. Opacity below 1 is more\n");
    fprintf(f, "# see-through, above 1 makes a faint crosshair more solid; brightness below 1 is dimmer,\n");
    fprintf(f, "# above 1 brighter. Each game keeps its own, like the size: these are the values a game's\n");
    fprintf(f, "# profile starts from. Live in F1 > Crosshair.\n");
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
        if (d.title_profiles[profile].crosshair_opacity != d.crosshair_opacity)
            fprintf(f, "# %s's opacity starts at %.2f instead, whatever this is.\n",
                Config_TitleProfileName(profile), d.title_profiles[profile].crosshair_opacity);
    fprintf(f, "# (defaults %.2f and %.2f, range 0 to 2)\n",
        d.crosshair_opacity, d.crosshair_brightness);
    fprintf(f, "crosshair_opacity = %.2f\n", g_config.base_tunables.crosshair_opacity);
    fprintf(f, "crosshair_brightness = %.2f\n\n", g_config.base_tunables.crosshair_brightness);
    fprintf(f, "# Every game, while the aim converges: the crosshair sits on the surface you aim at, pulled\n");
    fprintf(f, "# back reticle_surface_offset_m, and parks at reticle_max_dist_m beyond that.\n");
    fprintf(f, "# It shrinks away as a surface comes closer than reticle_min_scale_dist_m\n");
    fprintf(f, "# (to reticle_min_scale). Distances in meters. (defaults %.1f, %.2f, %.2f, %.3f)\n",
            d.reticle_max_dist_m, d.reticle_surface_offset_m, d.reticle_min_scale_dist_m,
            d.reticle_min_scale);
    fprintf(f, "reticle_max_dist_m = %.1f\n", g_config.reticle_max_dist_m);
    fprintf(f, "reticle_surface_offset_m = %.2f\n", g_config.reticle_surface_offset_m);
    fprintf(f, "reticle_min_scale_dist_m = %.2f\n", g_config.reticle_min_scale_dist_m);
    fprintf(f, "reticle_min_scale = %.3f\n\n", g_config.reticle_min_scale);
    fprintf(f, "# How often the VR crosshair re-reads the game's own crosshair art,\n");
    fprintf(f, "# in frames. This makes Halo 3 and Halo 4 authored crosshairs animate:\n");
    fprintf(f, "# when you shoot, and the red/green target colors. Lower is more\n");
    fprintf(f, "# responsive, higher is cheaper. 0 holds animation. Below 6 only where\n");
    fprintf(f, "# the headset runtime makes an update cheap; otherwise 6 is used.\n");
    fprintf(f, "# Halo 3 and Halo 4. (default %d, 0 or 1 to 60)\n",
             d.crosshair_animation_frames);
    fprintf(f, "crosshair_animation_frames = %d\n\n",
             g_config.crosshair_animation_frames);
    fprintf(f, "# Crosshair color, 0-1 per channel. Not in the F1 menu; this file only.\n");
    fprintf(f, "# (defaults %.3f / %.3f / %.3f = light blue, range 0 to 1)\n",
             d.reticle_r, d.reticle_g, d.reticle_b);
    fprintf(f, "reticle_r = %.3f\n", g_config.reticle_r);
    fprintf(f, "reticle_g = %.3f\n", g_config.reticle_g);
    fprintf(f, "reticle_b = %.3f\n\n", g_config.reticle_b);
    fprintf(f, "# Hide the native head-centered crosshair after the floating\n");
    fprintf(f, "# motion-control reticle is ready. Set 0 for an emergency native fallback.\n");
    fprintf(f, "# (default %d)\n", d.kill_reticle ? 1 : 0);
    fprintf(f, "kill_reticle = %d\n\n", g_config.kill_reticle ? 1 : 0);
    fprintf(f, "# EXPERIMENTAL / diagnostic, Halo Reach only. Routes aiming through the\n");
    fprintf(f, "# game-agnostic aim-solve core. On its own it aims exactly like the built-in\n");
    fprintf(f, "# path (that path is its fallback) and logs its decisions; direct drive through\n");
    fprintf(f, "# it is aim_direct_drive = 1. Leave it at 0. (default %d)\n",
            d.aim_provider);
    fprintf(f, "aim_provider = %d\n\n", g_config.aim_provider);
#if !HALOMCCVR_DEV_DIAGNOSTICS
    // reach_direct_drive is derived from aim_direct_drive in a player build (written below).
#else
    fprintf(f, "# DEV BUILD ONLY (the research keys below). EXPERIMENTAL / diagnostic, Halo Reach only, READ-ONLY. Confirms the desired-aim\n");
    fprintf(f, "# struct offset for a future direct-drive aim: it logs the candidate desired-aim\n");
    fprintf(f, "# vector against the known current aim (they match at rest, the desired leads when\n");
    fprintf(f, "# you turn). It does NOT change your aim. Leave at 0. (default %d)\n", d.reach_aim_probe);
    fprintf(f, "reach_aim_probe = %d\n\n", g_config.reach_aim_probe);
    fprintf(f, "# Candidate byte offset of Reach's unit desired-aim vector (hex ok). 0x208 is the\n");
    fprintf(f, "# reverse-engineered value; only used when reach_aim_probe is on. (default 0x%X)\n",
            d.reach_desired_aim_offset);
    fprintf(f, "reach_desired_aim_offset = 0x%X\n\n", g_config.reach_desired_aim_offset);
    fprintf(f, "# EXPERIMENTAL / diagnostic, Halo Reach only. Direct-drive MECHANISM TEST: writes the\n");
    fprintf(f, "# desired-aim field to learn whether it STEERS aim (the read-only probe cannot tell on\n");
    fprintf(f, "# foot). 1 = swing your aim reach_dd_test_deg degrees and hold it there (suppresses the\n");
    fprintf(f, "# stick). 2 = LIVE OFFSET (no suppression): aim normally and it offsets the desired-aim\n");
    fprintf(f, "# field by reach_dd_test_deg each tick; if that field drives the SHOT, bullets land that\n");
    fprintf(f, "# many degrees off your reticle. 3 = ACTIVE DIRECT DRIVE: your aim writes the game's own\n");
    fprintf(f, "# control angles directly (crisp; co-op replication not yet verified); needs a relaunch\n");
    fprintf(f, "# to install, then arms itself once it has validated the address (log: REACHDRIVE ARMED).\n");
    fprintf(f, "# On foot, and in vehicles as reach_vehicle_aim says; cutscenes and menus keep the stick.\n");
    fprintf(f, "# 4 = PERSISTENCE TEST: one write to the sim's own control angles, checks that it\n");
    fprintf(f, "# sticks and turns your view, then puts your aim back (your view jumps briefly; the\n");
    fprintf(f, "# stick is ignored while it runs). Leave at 0 unless testing. (default %d)\n", d.reach_direct_drive);
    fprintf(f, "reach_direct_drive = %d\n\n", g_config.reach_direct_drive);
    fprintf(f, "# Swing angle in degrees for the reach_direct_drive=1 and =4 tests. (default %d)\n", d.reach_dd_test_deg);
    fprintf(f, "reach_dd_test_deg = %d\n\n", g_config.reach_dd_test_deg);
    fprintf(f, "# DEV locator: hardware write-watchpoints (logs the on-foot writer's address + call\n");
    fprintf(f, "# chain so it can be hooked for direct drive). 3 = value-scan: finds desired_angles in\n");
    fprintf(f, "# the sim's master game state, narrows it over 2-3 still aims >=3 deg apart, then logs\n");
    fprintf(f, "# its writer. 1/2 = older single-address modes (1 sees only a snapshot copy; 2 is\n");
    fprintf(f, "# refuted). Read-only (never changes aim). Leave at 0. (default %d)\n",
            d.reach_dd_watchpoint);
    fprintf(f, "reach_dd_watchpoint = %d\n\n", g_config.reach_dd_watchpoint);
    fprintf(f, "# DEV BUILD ONLY. Halo Reach on foot, direct drive: replaces the resolved crosshair-trace\n");
    fprintf(f, "# flags of the trace-and-reaim aim for an experiment (0 = use the resolved ones). (default 0x%llX)\n",
            d.reach_sight_trace_flags);
    fprintf(f, "reach_sight_trace_flags = 0x%llX\n", g_config.reach_sight_trace_flags);
    fprintf(f, "reach_sight_trace_mode = %d\n", g_config.reach_sight_trace_mode);
    fprintf(f, "# DEV BUILD ONLY. Log Reach's own collision-test calls (caller, flags, mode). Leave at 0.\n");
    fprintf(f, "reach_collision_census = %d\n\n", g_config.reach_collision_census);
    fprintf(f, "# DEV BUILD ONLY. Log the game's own queries that start at the eye along the aim (Halo 4,\n");
    fprintf(f, "# Halo 2). Leave at 0.\n");
    fprintf(f, "sight_ray_census = %d\n\n", g_config.sight_ray_census);
    fprintf(f, "# DEV BUILD ONLY. Halo 3 seated shots: 1 = aim so the round, which inherits its vehicle's sideways\n");
    fprintf(f, "# velocity, still flies at the reticle's point. 0 = aim straight at it (rounds lead along your travel).\n");
    fprintf(f, "seated_inherit_compensation = %d\n\n", g_config.seated_inherit_compensation);
    fprintf(f, "# DEV BUILD ONLY (players always get 1). Flat parity for on-foot shots while strafing: 1 = Halo 3,\n");
    fprintf(f, "# ODST, Halo 2, CE, Halo 4 and Reach shots (Reach only with its measured eye at the fire) aim through\n");
    fprintf(f, "# the reticle's point moved with your eye since it was sampled -- what flat Halo fires with the\n");
    fprintf(f, "# crosshair held still on your body. 0 = through the world point (rounds trail a strafe by one game\n");
    fprintf(f, "# tick; the A/B). The reticle never moves. Live.\n");
    fprintf(f, "# (default %d)\n", d.shot_point_transport);
    fprintf(f, "shot_point_transport = %d\n", g_config.shot_point_transport);
    fprintf(f, "# DEV BUILD ONLY (players always get 1). With shot_point_transport = 1: 1 = seated shots too.\n");
    fprintf(f, "# 0 = seated shots through the point fixed in the world.\n");
    fprintf(f, "shot_point_transport_seated = %d\n", g_config.shot_point_transport_seated);
    fprintf(f, "# DEV BUILD ONLY (players always get 1). Halo 2 on foot, with shot_point_transport = 1: 1 = once proven,\n");
    fprintf(f, "# a muzzle shot's eye at the fire is the game's own camera evaluator asked in the fire (the eye after\n");
    fprintf(f, "# that tick's move). 0 = the newest view's eye carried one tick (the A/B; short by Halo 2's camera\n");
    fprintf(f, "# interpolation). Live. (default %d)\n", d.halo2_measured_fire_eye);
    fprintf(f, "halo2_measured_fire_eye = %d\n\n", g_config.halo2_measured_fire_eye);
    fprintf(f, "# DEV BUILD ONLY (players always get 1). Halo 4 on foot, with shot_point_transport = 1: 1 = once proven,\n");
    fprintf(f, "# a muzzle shot's eye at the fire is the game's own unit camera position asked in the fire (the eye after\n");
    fprintf(f, "# that tick's move). 0 = the view's eye carried one tick (the A/B; short by Halo 4's camera\n");
    fprintf(f, "# interpolation). Live. (default %d)\n", d.halo4_measured_fire_eye);
    fprintf(f, "halo4_measured_fire_eye = %d\n\n", g_config.halo4_measured_fire_eye);
    fprintf(f, "# DEV BUILD ONLY. Halo 3 seats: 1 = aim the seat's record so the game's own crosshair line at the\n");
    fprintf(f, "# fire passes through the reticle's point (what a multiplayer host fires). 2 = the same for the camera an\n");
    fprintf(f, "# unmodded host uses (a third-person seat's chase camera). 0 = as before (A/B). (default 1)\n");
    fprintf(f, "seat_upstream_aim = %d\n\n", g_config.seat_upstream_aim);
    fprintf(f, "# DEV BUILD ONLY. Assessment: a second marker showing where the game's shot lands; your normal reticle\n");
    fprintf(f, "# is unchanged. A small magenta ring and dot, along the game's own aim from its own shot origin (Reach on\n");
    fprintf(f, "# foot: the unit's projectile aim, not the camera), to see how far the game trails the hand. Draws only;\n");
    fprintf(f, "# shots, aim and convergence are unchanged.\n");
    fprintf(f, "# 0 = off, 1 = on foot, 2 = everywhere. (default 0)\n");
    fprintf(f, "honest_reticle_assess = %d\n\n", g_config.honest_reticle_assess);
    fprintf(f, "# DEV BUILD ONLY. 1 = log the type-13 pilot controllers (Reach Sabre, Halo 4 Broadsword): REACH SF and\n");
    fprintf(f, "# H4 T13 lines. Writes nothing to the game. Read at startup. Leave at 0.\n");
    fprintf(f, "type13_log = %d\n\n", g_config.type13_log);
    fprintf(f, "# DEV BUILD ONLY. Halo 3 / ODST / Reach seats: 1 = skip the game's vehicle-tilt rebase of the aim while the\n");
    fprintf(f, "# first-person seat patch is on, so seats aim in world angles as in stock. 0 = leave the rebase (A/B for\n");
    fprintf(f, "# the DRIVE seats frame check line). (default %d)\n", d.seat_aim_world_frame);
    fprintf(f, "seat_aim_world_frame = %d\n\n", g_config.seat_aim_world_frame);
    fprintf(f, "# DEV BUILD ONLY. Halo 3 seats: 0 = the game's camera director alone is told the occupied seat is first\n");
    fprintf(f, "# person; the vehicle's loaded data stays stock, so shots, AI and every other player's game see a stock\n");
    fprintf(f, "# seat. 1 = the earlier way (A/B): the seat's third-person flag is cleared in the loaded vehicle data\n");
    fprintf(f, "# while seated, which the simulation reads too (a co-op campaign client goes out of step). (default %d)\n",
            d.seat_view_tag_write);
    fprintf(f, "seat_view_tag_write = %d\n\n", g_config.seat_view_tag_write);
    fprintf(f, "# DEV BUILD ONLY. Headless tests: 1 = a synthetic gamepad driven by halomccvr_pad.txt in the\n");
    fprintf(f, "# mod folder (tools/Send-MccPad.ps1). Read at startup. Leave at 0.\n");
    fprintf(f, "dev_synthetic_pad = %d\n\n", g_config.dev_synthetic_pad);
    fprintf(f, "# DEV BUILD ONLY (players always get 1 and 0). The motion tracker's head correction, with\n");
    fprintf(f, "# tracker_follows_head = 1: tracker_head_sign = -1 turns it the other way, if a game's\n");
    fprintf(f, "# tracker turns AGAINST your head; tracker_head_offset_deg adds a fixed angle. Live.\n");
    fprintf(f, "tracker_head_sign = %d\n", g_config.tracker_head_sign < 0 ? -1 : 1);
    fprintf(f, "tracker_head_offset_deg = %.1f\n\n", g_config.tracker_head_offset_deg);
    fprintf(f, "# DEV BUILD ONLY (players always get 3: turned to the head, not moved, as in Reach and Halo 3).\n");
    fprintf(f, "# Halo 4's head-relative sound, with audio_follows_head = 1:\n");
    fprintf(f, "# which of its writes are made. Add up: 1 = the copy handed to the audio middleware, 2 = the game's\n");
    fprintf(f, "# own listener record, 4 = the listener is also moved to the head. 0 = measure only. Live.\n");
    fprintf(f, "audio_head_halo4_parts = %d\n\n", g_config.audio_head_halo4_parts & 7);
    fprintf(f, "# DEV BUILD ONLY. Halo: Reach navigation markers. nav_marker_witness = 1 logs the pixel the game\n");
    fprintf(f, "# puts each marker on against the pixel the eye's own picture shows its target at (log: NAV\n");
    fprintf(f, "# MARKERS Reach (dev witness)). Live.\n");
    fprintf(f, "nav_marker_witness = %d\n\n", g_config.nav_marker_witness);
#endif
    fprintf(f, "# Vehicle seats: which seats your hand aims. With aim_direct_drive = 1 the seat's aim is written\n");
    fprintf(f, "# as on foot (Reach, Halo 3, ODST, Halo 4, Halo 2); Halo 3 and ODST seats also converge whenever\n");
    fprintf(f, "# converged_aim is on (0 here keeps their old seat aim), and Halo 2 seats converge wherever this\n");
    fprintf(f, "# key lets the hand write them. Halo CE's hand aims every seat whatever this key says (its seat\n");
    fprintf(f, "# aim converges with converged_aim alone). A land vehicle seat's turning limits\n");
    fprintf(f, "# still hold (a passenger's cone, a turret's or tank gun's pitch stop): past one the crosshair\n");
    fprintf(f, "# stops at the edge, where the gun is held and the shots go; a gun still turning toward your\n");
    fprintf(f, "# hand keeps the crosshair on your aim point; aircraft have no limits. A driver whose stick or\n");
    fprintf(f, "# wheel steers keeps it for steering. 0 = off (vehicles\n");
    fprintf(f, "# keep the old stick aim), 1 = gunners, passengers, and drivers whose gun turns apart from the\n");
    fprintf(f, "# hull (a driver whose look steers the vehicle keeps the stick; Halo 4 cannot tell a seat's\n");
    fprintf(f, "# role yet, so 1 keeps every seat there on the stick; Halo 2 reads the role off the seat\n");
    fprintf(f, "# itself, and a seat it cannot read counts as a driver), 2 = every seat you aim,\n");
    fprintf(f, "# drivers included. F1 > Vehicles: \"Drivers aim with the hand\" switches 2 / 1. (default %d)\n",
            d.reach_vehicle_aim);
    fprintf(f, "reach_vehicle_aim = %d\n\n", g_config.reach_vehicle_aim);
    fprintf(f, "# 1 = with aim_direct_drive = 1, direct drive writes the aim in vehicle seats too (the seats\n");
    fprintf(f, "# reach_vehicle_aim names; each seat checks itself first), instead of the stick steering it\n");
    fprintf(f, "# at the game's turn rate. Every title needs its first-person seat view (Halo 3 / ODST also\n");
    fprintf(f, "# vehicle_hide_body; Halo 2 also vehicle_vr_controls, and vehicle_view_follow off); Halo 4\n");
    fprintf(f, "# seats write the heading only. 0 keeps every seat on\n");
    fprintf(f, "# the stick. F1 > Vehicles: \"Direct drive in seats\". (default %d)\n",
            d.seat_direct_drive);
    fprintf(f, "seat_direct_drive = %d\n\n", g_config.seat_direct_drive);
    fprintf(f, "# Halo 2. 1 = a vehicle gun you aim (a tank's cannon, a turret) is aimed so that its BARREL points\n");
    fprintf(f, "# at the crosshair's point, so the shot lands on it at any range. A Scorpion's barrel is known; any\n");
    fprintf(f, "# other gun's place is learned from your own shots and remembered for that kind of vehicle while the\n");
    fprintf(f, "# game runs, so only the first shot from a gun new to it can still land beside a near point.\n");
    fprintf(f, "# 0 = the seat is aimed from its camera only: shots at near targets land beside the crosshair, by\n");
    fprintf(f, "# about the distance from your view to the barrel. Needs seat_direct_drive and converged_aim.\n");
    fprintf(f, "# Live. (default %d)\n", d.seat_aim_through_barrel ? 1 : 0);
    fprintf(f, "seat_aim_through_barrel = %d\n\n", g_config.seat_aim_through_barrel ? 1 : 0);
    fprintf(f, "# Halo 2 and Halo 3. 1 = when you fire a vehicle's gun the game checks what is under the crosshair\n");
    fprintf(f, "# along YOUR line of sight. 0 = along the line from the seat's own camera, as the game does: a shot\n");
    fprintf(f, "# aimed over something low and near can then be pulled down onto it, and in Halo 3 something behind\n");
    fprintf(f, "# your vehicle (that camera stands behind it) can take the shot. Not in co-op. Needs converged_aim.\n");
    fprintf(f, "# Live.\n");
    fprintf(f, "# (default %d)\n", d.seat_fire_from_eye ? 1 : 0);
    fprintf(f, "seat_fire_from_eye = %d\n\n", g_config.seat_fire_from_eye ? 1 : 0);
    fprintf(f, "# Halo 4. 1 = CONVERGED AIM IN HALO 4 SEATS: the game traces the ray from your eye\n");
    fprintf(f, "# along the controller, and the seat's aim is bent so the line the game itself aims the seat\n");
    fprintf(f, "# along passes through the point it hits; the reticle sits on that point. With coop_safe_shots\n");
    fprintf(f, "# on that line starts at the game's own seat camera, the same on every player's game, and\n");
    fprintf(f, "# nothing but the aim you send is changed. The gun then turns its shot onto the point as far as\n");
    fprintf(f, "# the game lets it (its own 21 degree cap, a seat's stops). Halo 4 seats write the heading; the\n");
    fprintf(f, "# up/down still follows the stick's turn rate. Needs converged_aim, reach_vehicle_aim above 0\n");
    fprintf(f, "# and vehicle_first_person. 0 = Halo 4 seats keep the fixed-distance aim. Live. The log line\n");
    fprintf(f, "# \"SIGHT Halo 4 seat converge\" says every 2 s whether it runs and, if not, why. (default %d)\n",
            d.halo4_seat_converge);
    fprintf(f, "halo4_seat_converge = %d\n\n", g_config.halo4_seat_converge);
    fprintf(f, "# Reach, Halo 3, ODST and Halo 4 (switch it live in F1 > Weapon & aim). 1 = DIRECT DRIVE: your\n");
    fprintf(f, "# aim writes the game's own aim instead of steering the right stick (which can only turn at the\n");
    fprintf(f, "# game's turn rate). It arms itself a moment after it is switched on, once the aim it found\n");
    fprintf(f, "# agrees with the game's (log: DRIVE ... armed=1, REACHDRIVE ARMED). Cutscenes and menus keep the\n");
    fprintf(f, "# stick. Halo 2 on foot: 1 also writes the game's own aim, so the body faces where you aim and\n");
    fprintf(f, "# the right stick turns your view (log: Halo 2 DRIVE: armed=1); 0 keeps Halo 2's earlier aim.\n");
    fprintf(f, "# Replaces reach_direct_drive = 3 and legacy_direct_drive = 1. 0 = off. (default %d)\n",
            d.aim_direct_drive);
    fprintf(f, "aim_direct_drive = %d\n\n", g_config.aim_direct_drive);
    fprintf(f, "# Direct drive finds the game's own aim code each time a game loads. 1 = when it had to search a\n");
    fprintf(f, "# game DLL for that code (a game build newer than this mod knows), it remembers where it was in\n");
    fprintf(f, "# HaloMCCVR-drive-addresses.txt beside this file, and later launches check those addresses\n");
    fprintf(f, "# instead of searching again. That file is a cache, not a setting: every address in it is\n");
    fprintf(f, "# checked before it is used, and it is safe to delete. 0 = never read or write that file.\n");
    fprintf(f, "# (default %d)\n", d.drive_address_cache);
    fprintf(f, "drive_address_cache = %d\n\n", g_config.drive_address_cache);
    fprintf(f, "# Every title (Reach also needs aim_direct_drive = 1), on foot and in vehicle seats: Halo 3,\n");
    fprintf(f, "# ODST and Reach; Halo CE and Halo 2 in their first-person seat view (vehicle_first_person = 1;\n");
    fprintf(f, "# Halo 2 where direct drive writes the seat, see seat_direct_drive); Halo 4 seats with\n");
    fprintf(f, "# halo4_seat_converge = 1.\n");
    fprintf(f, "# 1 = CONVERGED AIM: the game traces the ray from your eye\n");
    fprintf(f, "# along the controller and bends the aim so the shot passes through the point it hits, at\n");
    fprintf(f, "# any distance; the reticle sits on that point. In a Halo CE or Halo 2 seat the game's own\n");
    fprintf(f, "# crosshair test then finds that point and turns the seat's gun onto it, as far as the gun's\n");
    fprintf(f, "# own cone and stops allow. 0 = the earlier aim, which converges on one\n");
    fprintf(f, "# fixed distance (crosshair_distance_m) and runs no trace. (default %d)\n", d.converged_aim);
    fprintf(f, "converged_aim = %d\n\n", g_config.converged_aim);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  WEAPON CALIBRATION\n");
    fprintf(f, "#  Personal trims applied over the active title's verified base pose.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Size of the right hand and the weapon it holds. Home/End adjust it\n");
    fprintf(f, "# in-game. 1.00 = the size the active game authored the model at.\n");
    fprintf(f, "# (default %.2f, range 0.3 to 3)\n", d.gun_scale);
    fprintf(f, "# Tracked-motion world scale in game units per metre (0.33 = 100%%, range 0.165-0.495).\n");
    fprintf(f, "world_scale = %.4f\n\n", g_config.base_tunables.world_scale);
    fprintf(f, "gun_scale = %.2f\n\n", g_config.base_tunables.gun_scale);
    fprintf(f, "# Size of the LEFT hand, and of the second gun when dual-wielding.\n");
    fprintf(f, "# Separate from gun_scale because the left hand is usually empty.\n");
    fprintf(f, "# Set it to the same number as gun_scale for matching hands.\n");
    fprintf(f, "# (default %.2f, range 0.3 to 3)\n", d.left_hand_scale);
    fprintf(f, "left_hand_scale = %.2f\n\n", g_config.base_tunables.left_hand_scale);
    fprintf(f, "# Visible-hand mesh offsets only (metres, -0.20 to 0.20; zero preserves placement).\n");
    fprintf(f, "left_hand_mesh_x_m = %.3f\n", g_config.base_tunables.left_hand_mesh_x_m);
    fprintf(f, "left_hand_mesh_y_m = %.3f\n", g_config.base_tunables.left_hand_mesh_y_m);
    fprintf(f, "left_hand_mesh_z_m = %.3f\n", g_config.base_tunables.left_hand_mesh_z_m);
    fprintf(f, "right_hand_mesh_x_m = %.3f\n", g_config.base_tunables.right_hand_mesh_x_m);
    fprintf(f, "right_hand_mesh_y_m = %.3f\n", g_config.base_tunables.right_hand_mesh_y_m);
    fprintf(f, "right_hand_mesh_z_m = %.3f\n", g_config.base_tunables.right_hand_mesh_z_m);

    fprintf(f, "# Controller aim calibration in degrees: rotates the aiming ray,\n");
    fprintf(f, "# crosshair and gun together. Use barrel_* for visual-only alignment.\n");
    fprintf(f, "# (defaults %.0f / %.0f / %.0f, range -180 to 180)\n",
            d.gun_pitch_deg, d.gun_yaw_deg, d.gun_roll_deg);
    fprintf(f, "gun_pitch_deg = %.0f\n", g_config.base_tunables.gun_pitch_deg);
    fprintf(f, "gun_yaw_deg = %.0f\n", g_config.base_tunables.gun_yaw_deg);
    fprintf(f, "gun_roll_deg = %.0f\n\n", g_config.base_tunables.gun_roll_deg);
    fprintf(f, "# Mesh-only barrel trim: rotates the visible gun + hands about the\n");
    fprintf(f, "# controller without moving the crosshair or the shot. Lay the drawn\n");
    fprintf(f, "# barrel on the crosshair line. Saved per game below.\n");
    fprintf(f, "barrel_pitch_deg = %.1f\n", g_config.base_tunables.barrel_pitch_deg);
    fprintf(f, "barrel_yaw_deg = %.1f\n", g_config.base_tunables.barrel_yaw_deg);
    fprintf(f, "barrel_roll_deg = %.1f\n\n", g_config.base_tunables.barrel_roll_deg);
    fprintf(f, "# Push the gun forward of the controller, in meters.\n");
    fprintf(f, "# Negative seats the gun back into your fist.\n");
    fprintf(f, "# (default %.2f, range -0.3 to 0.5)\n", d.gun_forward_m);
    fprintf(f, "gun_forward_m = %.2f\n\n", g_config.base_tunables.gun_forward_m);
    fprintf(f, "# Sideways and vertical gun-stock offsets in controller-local meters.\n");
    fprintf(f, "# Applied after weapon mount rotation; visual only, aim is unchanged.\n");
    fprintf(f, "# (defaults 0.00 / 0.00, range -0.3 to 0.3)\n");
    fprintf(f, "gun_right_m = %.2f\n", g_config.base_tunables.gun_right_m);
    fprintf(f, "gun_up_m = %.2f\n\n", g_config.base_tunables.gun_up_m);
    fprintf(f, "# Halo 2 CLASSIC / original-graphics visual alignment only.\n");
    fprintf(f, "# Pitch/yaw are the two live F1 controls; Anniversary and the\n");
    fprintf(f, "# native reticle/shot ray never read any of these values.\n");
    fprintf(f, "# (pitch/yaw slider range -30 to +30 degrees; defaults 0)\n");
    fprintf(f, "halo2_classic_gun_pitch_deg = %.2f\n",
            SharedAlignmentValue(&Config::halo2_classic_gun_pitch_deg));
    fprintf(f, "halo2_classic_gun_yaw_deg = %.2f\n",
            SharedAlignmentValue(&Config::halo2_classic_gun_yaw_deg));
    fprintf(f, "halo2_classic_gun_roll_deg = %.2f\n",
            SharedAlignmentValue(&Config::halo2_classic_gun_roll_deg));
    fprintf(f, "halo2_classic_gun_forward_m = %.3f\n",
            SharedAlignmentValue(&Config::halo2_classic_gun_forward_m));
    fprintf(f, "halo2_classic_gun_right_m = %.3f\n",
            SharedAlignmentValue(&Config::halo2_classic_gun_right_m));
    fprintf(f, "halo2_classic_gun_up_m = %.3f\n\n",
            SharedAlignmentValue(&Config::halo2_classic_gun_up_m));
    fprintf(f, "# Raise the muzzle flash / bullet spawn point along the gun's\n");
    fprintf(f, "# own up axis, in meters. Reach only. Does NOT change where\n");
    fprintf(f, "# rounds land - only where they appear to come from.\n");
    fprintf(f, "# (default %.2f, range -0.3 to 0.3; ~0.11 is about 4 inches)\n",
            d.muzzle_height_m);
    fprintf(f, "muzzle_height_m = %.2f\n\n", SharedAlignmentValue(&Config::muzzle_height_m));
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  FIRST-PERSON VEHICLES\n");
    fprintf(f, "#  Sit in the seat instead of floating behind the vehicle.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# First-person vehicle camera. The position is your title-specific\n");
    fprintf(f, "# Blender-authored point for each seat; head look and leaning stay\n");
    fprintf(f, "# live. 0 = the stock behind-the-vehicle view.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_first_person ? 1 : 0);
    fprintf(f, "vehicle_first_person = %d\n\n",
            g_config.vehicle_first_person ? 1 : 0);
    fprintf(f, "# Trim applied to every seat, in meters: forward toward the\n");
    fprintf(f, "# windshield, up out of the seat, and right across the seat.\n");
    fprintf(f, "# (defaults %.2f / %.2f / %.2f; forward+up range -1 to 1.5,\n",
            d.vehicle_cam_forward_m, d.vehicle_cam_up_m,
            d.vehicle_cam_right_m);
    fprintf(f, "# right range -1 to 1; negative right moves left)\n");
    fprintf(f, "vehicle_cam_forward_m = %.2f\n", g_config.vehicle_cam_forward_m);
    fprintf(f, "vehicle_cam_up_m = %.2f\n", g_config.vehicle_cam_up_m);
    fprintf(f, "vehicle_cam_right_m = %.2f\n\n", g_config.vehicle_cam_right_m);
    fprintf(f,"# Per-game seat fallback. Missing axes inherit the legacy values above.\n");
    const char* vehicleAxes[]{"forward","up","right"};
    for(unsigned title=0;title<6;++title)for(unsigned axis=0;axis<3;++axis)
        if(g_config.vehicle_cam_game_set[title][axis])
            fprintf(f,"vehicle_game_%s_%s_m = %.3f\n",weapon_interaction::kTitleKeys[title],
                vehicleAxes[axis],g_config.vehicle_cam_game[title][axis]);
    fprintf(f,"\n");
    fprintf(f,"# Identified vehicle/seat overrides; stable authored model identity, never object handles.\n");
    fprintf(f,"# Built in: Halo 4's Mantis pilot (halo4_000000551b05181e_seat0) sits above its closed cockpit,\n");
    fprintf(f,"# forward 0.30, up 0.80. At 0 the eye is inside the cockpit and sees only its walls.\n");
    for(const auto& trim:g_config.vehicle_model_trims)
    {
        const int title=weapon_interaction::TitleIndex(trim.title);
        if(title<0||!trim.identity||trim.seat<0||trim.seat>31)continue;
        for(unsigned axis=0;axis<3;++axis)if(trim.set[axis])
            fprintf(f,"vehicle_model_%s_%016llx_seat%d_%s_m = %.3f\n",weapon_interaction::kTitleKeys[title],
                static_cast<unsigned long long>(trim.identity),trim.seat,vehicleAxes[axis],trim.value[axis]);
    }
    fprintf(f,"\n");
    fprintf(f, "# Per-SEAT trim. A line appears here when you adjust the three\n");
    fprintf(f, "# seat sliders while SITTING IN that seat; every seat without\n");
    fprintf(f, "# a line keeps using the universal trim above. Delete a line\n");
    fprintf(f, "# (or use the F1 button) to hand that seat back to it. Names:\n");
    fprintf(f, "#   vehicle_cam_forward_m_<vehicle>_<seat>\n");
    fprintf(f, "#   vehicle_cam_up_m_<vehicle>_<seat>\n");
    fprintf(f, "#   vehicle_cam_right_m_<vehicle>_<seat>\n");
    fprintf(f, "#   vehicles: scorpion warthog mongoose ghost wraith prowler\n");
    fprintf(f, "#             banshee hornet chopper turret\n");
    fprintf(f, "#   seats:    driver passenger passenger2 gunner\n");
    fprintf(f, "# 'gunner' is a turret mounted ON that vehicle (the Warthog's\n");
    fprintf(f, "# chaingun, the Scorpion's coax); each vehicle's own driver,\n");
    fprintf(f, "# passengers and gunner are independent.\n");
    for (int v = 0; v < kVehicleTrimCount; ++v)
        for (int s = 0; s < kVehicleSeatSlots; ++s)
        {
            const int i = v * kVehicleSeatSlots + s;
            if (g_config.vehicle_cam_forward_set[i])
                fprintf(f, "vehicle_cam_forward_m_%s_%s = %.2f\n",
                        kVehicleTrimNames[v], kVehicleSeatNames[s],
                        g_config.vehicle_cam_forward_v[i]);
            if (g_config.vehicle_cam_up_set[i])
                fprintf(f, "vehicle_cam_up_m_%s_%s = %.2f\n",
                        kVehicleTrimNames[v], kVehicleSeatNames[s],
                        g_config.vehicle_cam_up_v[i]);
            if (g_config.vehicle_cam_right_set[i])
                fprintf(f, "vehicle_cam_right_m_%s_%s = %.2f\n",
                        kVehicleTrimNames[v], kVehicleSeatNames[s],
                        g_config.vehicle_cam_right_v[i]);
        }
    fprintf(f, "# ODST keeps its own seat trims under the same names with an\n");
    fprintf(f, "# 'odst_' in front of the vehicle, so tuning a Warthog in ODST\n");
    fprintf(f, "# never moves the Halo 3 one:\n");
    fprintf(f, "#   vehicle_cam_forward_m_odst_<vehicle>_<seat>\n");
    fprintf(f, "#   ODST adds the shade, and its Scorpion riders are seats\n");
    fprintf(f, "#   passenger .. passenger4.\n");
    for (int v = 0; v < kOdstVehicleTrimCount; ++v)
        for (int s = 0; s < kOdstVehicleSeatSlots; ++s)
        {
            const int i = v * kOdstVehicleSeatSlots + s;
            if (g_config.odst_vehicle_cam_forward_set[i])
                fprintf(f, "vehicle_cam_forward_m_odst_%s_%s = %.2f\n",
                        kOdstVehicleTrimNames[v], kOdstVehicleSeatNames[s],
                        g_config.odst_vehicle_cam_forward_v[i]);
            if (g_config.odst_vehicle_cam_up_set[i])
                fprintf(f, "vehicle_cam_up_m_odst_%s_%s = %.2f\n",
                        kOdstVehicleTrimNames[v], kOdstVehicleSeatNames[s],
                        g_config.odst_vehicle_cam_up_v[i]);
            if (g_config.odst_vehicle_cam_right_set[i])
                fprintf(f, "vehicle_cam_right_m_odst_%s_%s = %.2f\n",
                        kOdstVehicleTrimNames[v], kOdstVehicleSeatNames[s],
                        g_config.odst_vehicle_cam_right_v[i]);
        }
    fprintf(f, "# Reach uses role-neutral raw seat indices:\n");
    fprintf(f, "#   vehicle_cam_forward_m_reach_<vehicle>_seat<number>\n");
    fprintf(f, "#   vehicles: banshee space_banshee ghost revenant wraith wraith_gunner\n");
    fprintf(f, "#             mongoose warthog warthog_chaingun warthog_gauss\n");
    fprintf(f, "#             warthog_rocket falcon sabre scorpion forklift cart\n");
    fprintf(f, "#             shade_plasma shade_flak plasma_turret machinegun\n");
    fprintf(f, "#             plus the full census rows and 'unmatched',\n");
    fprintf(f, "#             which every vehicle this build cannot identify\n");
    fprintf(f, "#             shares so a seated F1 edit never rewrites the\n");
    fprintf(f, "#             universal trim above.\n");
    fprintf(f, "#   seats: seat0 through seat15 (only authored seats are used)\n");
    fprintf(f, "# F1's Back to this seat's authored point persists as:\n");
    fprintf(f, "#   vehicle_cam_use_universal_reach_<vehicle>_<seat> = 1\n");
    fprintf(f, "#   (the seat returns to its built-in Blender row; only a seat\n");
    fprintf(f, "#    that never had one follows the universal trim)\n");
    for (int v = 0; v < kReachVehicleTrimCount; ++v)
        for (int s = 0; s < kReachVehicleSeatSlots; ++s)
        {
            const int i = v * kReachVehicleSeatSlots + s;
            if (g_config.reach_vehicle_cam_use_universal[i])
            {
                fprintf(f,
                    "vehicle_cam_use_universal_reach_%s_%s = 1\n",
                    kReachVehicleTrimNames[v], kReachVehicleSeatNames[s]);
                continue;
            }
            if (g_config.reach_vehicle_cam_forward_set[i])
                fprintf(f, "vehicle_cam_forward_m_reach_%s_%s = %.2f\n",
                    kReachVehicleTrimNames[v], kReachVehicleSeatNames[s],
                    g_config.reach_vehicle_cam_forward_v[i]);
            if (g_config.reach_vehicle_cam_up_set[i])
                fprintf(f, "vehicle_cam_up_m_reach_%s_%s = %.2f\n",
                    kReachVehicleTrimNames[v], kReachVehicleSeatNames[s],
                    g_config.reach_vehicle_cam_up_v[i]);
            if (g_config.reach_vehicle_cam_right_set[i])
                fprintf(f, "vehicle_cam_right_m_reach_%s_%s = %.2f\n",
                    kReachVehicleTrimNames[v], kReachVehicleSeatNames[s],
                    g_config.reach_vehicle_cam_right_v[i]);
        }
    fprintf(f, "\n");
    fprintf(f, "# ON follows ground-vehicle yaw/pitch; aircraft stay yaw-only.\n");
    fprintf(f, "# Vehicle roll stays horizon-stable.\n");
    fprintf(f, "# OFF keeps the established world-locked view.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_view_follow ? 1 : 0);
    fprintf(f, "vehicle_view_follow = %d\n\n",
            g_config.vehicle_view_follow ? 1 : 0);
    fprintf(f, "# ON (default) parents that point to the exact interpolated seat or\n");
    fprintf(f, "# attachment node Halo renders and adds only occupant-head motion\n");
    fprintf(f, "# relative to the settled seat pose. No filter or placement shift.\n");
    fprintf(f, "# OFF uses the raw node matrices as a diagnostic A/B.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_cam_smoothing ? 1 : 0);
    fprintf(f, "vehicle_cam_smoothing = %d\n\n", g_config.vehicle_cam_smoothing ? 1 : 0);
    fprintf(f, "# ON (default) hides only your own seated WORLD character from\n");
    fprintf(f, "# your first-person camera. Other cameras still see it, and the\n");
    fprintf(f, "# first-person arms and weapon remain. No game file is modified.\n");
    fprintf(f, "# Works with vehicle_view_follow both OFF and ON; has no effect\n");
    fprintf(f, "# when vehicle_first_person = 0.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_hide_body ? 1 : 0);
    fprintf(f, "vehicle_hide_body = %d\n\n", g_config.vehicle_hide_body ? 1 : 0);
    fprintf(f, "# ON (default) hangs your arms and gun off the SEAT while you\n");
    fprintf(f, "# are riding in first person, so they stay with the vehicle and\n");
    fprintf(f, "# your hands while your head turns freely. OFF anchors them to\n");
    fprintf(f, "# Halo's seated camera, which is your character's head, so\n");
    fprintf(f, "# turning to look around drags the gun with your face.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_hands_follow_body ? 1 : 0);
    fprintf(f, "vehicle_hands_follow_body = %d\n\n",
            g_config.vehicle_hands_follow_body ? 1 : 0);
    fprintf(f, "# How much of the seat's bounce reaches your view, 0 to 1.\n");
    fprintf(f, "# 1 is the engine's full travel (about 24 cm) and reads as far\n");
    fprintf(f, "# too much; 0 bolts your view to the seat. This is a strength\n");
    fprintf(f, "# control, NOT smoothing - nothing is filtered at any setting.\n");
    fprintf(f, "# (default %.2f, range 0-1)\n", d.vehicle_bounce);
    fprintf(f, "vehicle_bounce = %.2f\n\n", g_config.vehicle_bounce);
    fprintf(f, "# ON (default) re-centres your play space when you settle into\n");
    fprintf(f, "# a seat and again when you get out, so a step you took on\n");
    fprintf(f, "# foot is not carried into the vehicle (or back out) as a\n");
    fprintf(f, "# standing lean. Position only - it never turns your view.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_recenter_on_seat ? 1 : 0);
    fprintf(f, "vehicle_recenter_on_seat = %d\n\n",
            g_config.vehicle_recenter_on_seat ? 1 : 0);
    fprintf(f, "# Hold the game's automatic exposure steady while you are in a\n");
    fprintf(f, "# first-person seat. Looking down at a vehicle's dashboard\n");
    fprintf(f, "# fills the game's brightness measurement with a big dark\n");
    fprintf(f, "# surface, so its auto-exposure ramps the whole scene - flat\n");
    fprintf(f, "# play never shows this because nobody looks at the dash. Uses\n");
    fprintf(f, "# the game's own exposure lock and only inside the seat, so\n");
    fprintf(f, "# normal play still adapts and cutscenes still light\n");
    fprintf(f, "# themselves. ODST for now. (default %d)\n",
            d.vehicle_steady_exposure ? 1 : 0);
    fprintf(f, "vehicle_steady_exposure = %d\n\n",
            g_config.vehicle_steady_exposure ? 1 : 0);
    fprintf(f, "# VR controls in Halo CE and Halo 2 vehicle seats. ON (default):\n");
    fprintf(f, "# in Halo CE the right stick snap- or smooth-turns your seat view\n");
    fprintf(f, "# (pitch is your head's), and the view follows the vehicle when\n");
    fprintf(f, "# vehicle_view_follow is on; in Halo 2 your hand aims the seat,\n");
    fprintf(f, "# and the seat keeps a view of its own that the aim does not\n");
    fprintf(f, "# swing. OFF: those two games' seats use the plain right stick, as\n");
    fprintf(f, "# on a flat screen. Halo 3, ODST, Reach and Halo 4 seats are not\n");
    fprintf(f, "# affected.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_vr_controls ? 1 : 0);
    fprintf(f, "vehicle_vr_controls = %d\n\n", g_config.vehicle_vr_controls ? 1 : 0);
    fprintf(f, "# Vehicle cameras (Halo 3 for now; needs vehicle_first_person). In a\n");
    fprintf(f, "# vehicle seat, step through the cameras listed for that seat in\n");
    fprintf(f, "# halomccvr_vehicle_cameras.json, in this folder. The file is written\n");
    fprintf(f, "# the first time this is on; it is yours to edit and is read again\n");
    fprintf(f, "# when you save it. Its first camera is the seat view you already\n");
    fprintf(f, "# have; a vehicle the mod does not know starts where the game's own\n");
    fprintf(f, "# third-person camera sits. In a seat: left Y = next camera, left X =\n");
    fprintf(f, "# next tethering mode, either held a second = re-centre the view, left\n");
    fprintf(f, "# stick click = next way to control the seat (vehicle_control). Right B\n");
    fprintf(f, "# then uses, gets out and reloads, and a passenger's support trigger\n");
    fprintf(f, "# switches weapon. Each seat's last camera, mode and controls are kept\n");
    fprintf(f, "# in halomccvr_vehicle_cameras_last.txt (delete it to start afresh).\n");
    fprintf(f, "# A camera placed outside the vehicle shows your character in the seat.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_cameras ? 1 : 0);
    fprintf(f, "vehicle_cameras = %d\n\n", g_config.vehicle_cameras ? 1 : 0);
    fprintf(f, "# With vehicle cameras on: a short note when you get in and when the\n");
    fprintf(f, "# camera, its tethering mode or the seat's controls change.\n");
    fprintf(f, "# 0 = no note; 1 = in the game's own HUD messages; 2 = on the mod's\n");
    fprintf(f, "# own panel in the headset. 1 uses the panel when the game's HUD\n");
    fprintf(f, "# cannot take the note, and in co-op.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_camera_note);
    fprintf(f, "vehicle_camera_note = %d\n\n", g_config.vehicle_camera_note);
    fprintf(f, "# With vehicle cameras on: the first time you sit in a vehicle the\n");
    fprintf(f, "# camera file has no entry of its own for, entries for it are added\n");
    fprintf(f, "# to halomccvr_vehicle_cameras.json (the built-in ones where there are\n");
    fprintf(f, "# any, else one by the vehicle's name with the default cameras), so\n");
    fprintf(f, "# every vehicle you have ridden is there to edit. Only added to; the\n");
    fprintf(f, "# file as it was is kept once as ...json.before-autoadd.\n");
    fprintf(f, "# 0 = the file is never written to. (default %d)\n", d.vehicle_camera_autoadd ? 1 : 0);
    fprintf(f, "vehicle_camera_autoadd = %d\n\n", g_config.vehicle_camera_autoadd ? 1 : 0);
    fprintf(f, "# With vehicle cameras on: a purple ring where the VEHICLE is aiming,\n");
    fprintf(f, "# beside the crosshair that shows where your hand points. In a mode\n");
    fprintf(f, "# where your view turns with the vehicle it is where the view comes to\n");
    fprintf(f, "# rest: aim at it to keep going straight without turning. Each seat,\n");
    fprintf(f, "# camera and tethering mode can turn it on or off with \"aimMarker\" in\n");
    fprintf(f, "# halomccvr_vehicle_cameras.json (the built-in cameras show it only in\n");
    fprintf(f, "# their Tethered modes). 0 = never shown, whatever the file says.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_aim_marker ? 1 : 0);
    fprintf(f, "vehicle_aim_marker = %d\n\n", g_config.vehicle_aim_marker ? 1 : 0);
    fprintf(f, "# How you control a vehicle seat (Halo 3; ODST and Reach with vehicle\n");
    fprintf(f, "# cameras on, Reach without Stick). This is apart from how your view\n");
    fprintf(f, "# turns: each works with a view the world holds still and with one\n");
    fprintf(f, "# that turns with the vehicle.\n");
    fprintf(f, "#   1 = Motion: your hand aims in every seat, and a Warthog, Mongoose,\n");
    fprintf(f, "#       Ghost, Prowler, Chopper, Banshee or Hornet turns toward it.\n");
    fprintf(f, "#       With a view that turns with the vehicle, pointing aside keeps\n");
    fprintf(f, "#       it turning until your hand points ahead again.\n");
    fprintf(f, "#   2 = Stick: the right stick aims and steers in every seat, as on a\n");
    fprintf(f, "#       flat screen. Your hand aims nothing, and the crosshair is\n");
    fprintf(f, "#       where the game's own is. The stick no longer turns your view\n");
    fprintf(f, "#       in a seat.\n");
    fprintf(f, "#   3 = Wheel: in those ground vehicles the invisible steering wheel\n");
    fprintf(f, "#       is in your hands from the moment you sit; double-click both\n");
    fprintf(f, "#       grips to let go (the stick then steers) and to take it again.\n");
    fprintf(f, "#       Aircraft take the stick. Every other seat is your hand's.\n");
    fprintf(f, "#   0 = as before: your hand aims every seat, except that the stick or\n");
    fprintf(f, "#       wheel steers those vehicles while the view turns with them.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_control);
    fprintf(f, "vehicle_control = %d\n\n", g_config.vehicle_control);
    fprintf(f, "# While your hand steers one of those vehicles (Halo 3 for now), a\n");
    fprintf(f, "# point you aim at that is BEHIND the vehicle and this close to it\n");
    fprintf(f, "# does not turn it: the vehicle keeps its forward. With a camera\n");
    fprintf(f, "# outside the vehicle your hand easily lands on the ground just\n");
    fprintf(f, "# behind it, which would swing the vehicle round. Measured along the\n");
    fprintf(f, "# vehicle's own floor, in metres; 0 = off (at most 20). Every camera,\n");
    fprintf(f, "# the seat view too.\n");
    fprintf(f, "# (default %.1f)\n", d.vehicle_aim_reject_metres);
    fprintf(f, "vehicle_aim_reject_metres = %.2f\n", g_config.vehicle_aim_reject_metres);
    fprintf(f, "# How far round counts as behind: the point's direction from the\n");
    fprintf(f, "# vehicle against its forward, as a dot product from -1 to 1. Below\n");
    fprintf(f, "# this is behind: -0.5 = more than 120 degrees off the nose, 0 = more\n");
    fprintf(f, "# than 90, -1 = nothing.\n");
    fprintf(f, "# (default %.2f)\n", d.vehicle_aim_reject_dot);
    fprintf(f, "vehicle_aim_reject_dot = %.2f\n\n", g_config.vehicle_aim_reject_dot);
    fprintf(f, "# Vehicle cameras (Halo 3 for now): a camera that stands where the\n");
    fprintf(f, "# game's own third-person camera does is raised and lowered by the\n");
    fprintf(f, "# right stick pushed up or down, in a seat where the stick does not\n");
    fprintf(f, "# aim (your hand or the wheel does). It moves the way the game's own\n");
    fprintf(f, "# stick moves its camera -- push up to look up and the camera drops --\n");
    fprintf(f, "# within the range the game gives your look there, and it never\n");
    fprintf(f, "# follows your aim. Each seat and camera keeps its angle; holding left\n");
    fprintf(f, "# X or Y puts it back. Speed in degrees of look a second at full push\n");
    fprintf(f, "# (5-360).\n");
    fprintf(f, "# (default %.0f)\n", d.vehicle_boom_rate_deg_s);
    fprintf(f, "vehicle_boom_rate_deg_s = %.1f\n", g_config.vehicle_boom_rate_deg_s);
    fprintf(f, "# Its up and down: -1 = as the game's own look inversion (its flight\n");
    fprintf(f, "# inversion when you pilot an aircraft; not inverted if the game's\n");
    fprintf(f, "# setting cannot be read), 0 = not inverted, 1 = inverted.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_boom_invert);
    fprintf(f, "vehicle_boom_invert = %d\n\n", g_config.vehicle_boom_invert);
    fprintf(f, "# Motion steering (Halo 3, ODST and Reach). In a Warthog,\n");
    fprintf(f, "# Mongoose, Ghost, Prowler or Chopper, DOUBLE-CLICK both grips to\n");
    fprintf(f, "# take hold of an invisible steering wheel: hold your hands as if\n");
    fprintf(f, "# on a wheel and tilt it, nothing needs squeezing. Double-click\n");
    fprintf(f, "# again to let go and the right stick steers. A single right grip\n");
    fprintf(f, "# still gets you out of the vehicle either way. Aircraft and the\n");
    fprintf(f, "# Scorpion and Wraith are unaffected. This switches the wheel only.\n");
    fprintf(f, "# (default %d)\n", d.vehicle_motion ? 1 : 0);
    fprintf(f, "vehicle_motion = %d\n", g_config.vehicle_motion ? 1 : 0);
    fprintf(f, "# Wheel angle for full lock, and the slack around centre.\n");
    fprintf(f, "# (defaults %.0f / %.0f, ranges 30-180 and 0-30)\n",
            d.vehicle_wheel_max_deg, d.vehicle_wheel_deadzone_deg);
    fprintf(f, "vehicle_wheel_max_deg = %.0f\n", g_config.vehicle_wheel_max_deg);
    fprintf(f, "vehicle_wheel_deadzone_deg = %.0f\n\n",
            g_config.vehicle_wheel_deadzone_deg);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  EXPERIMENTAL SCOPE\n");
    fprintf(f, "#  Universal physical placement and performance preferences.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Gun-mounted zoom screen. R3 toggles it without hiding the VR body.\n");
    fprintf(f, "# The main VR view stays wide while a fixed-magnification image appears. 1 = on.\n");
    fprintf(f, "# (default %d)\n", d.scope_enabled ? 1 : 0);
    fprintf(f, "scope_enabled = %d\n\n", g_config.scope_enabled ? 1 : 0);
    fprintf(f, "# The zoom looks AT the point your converged aim sends the shot to, from your eye, so the\n");
    fprintf(f, "# reticle's point is its centre however far you stand from the character. 0 = the earlier\n");
    fprintf(f, "# zoom, along the game's aim: parallel to your sight line, off by that distance. Needs\n");
    fprintf(f, "# converged_aim and the aim written (direct drive). Live in F1 > Scope. (default %d)\n",
            d.scope_converge);
    fprintf(f, "scope_converge = %d\n\n", g_config.scope_converge);
    fprintf(f, "# Aim Frame Zoom (Halo CE, Halo 2, Halo 3 in MCC and Project Reclaimer, ODST, Reach and Halo 4):\n");
    fprintf(f, "# while the game is zoomed, the\n");
    fprintf(f, "# headset shows the game's zoomed picture as a frame centred on your aim instead of the\n");
    fprintf(f, "# immersive view. The zoom button then reaches the game, so the lens above is not offered in\n");
    fprintf(f, "# those games while this is on. 1 = on. (default %d)\n", d.aim_frame_zoom ? 1 : 0);
    fprintf(f, "aim_frame_zoom = %d\n\n", g_config.aim_frame_zoom ? 1 : 0);
    fprintf(f, "# The frame in 3D: each eye sees its own picture of the zoomed view. 0 = one flat picture.\n");
    fprintf(f, "# (default %d)\n", d.aim_frame_stereo ? 1 : 0);
    fprintf(f, "aim_frame_stereo = %d\n\n", g_config.aim_frame_stereo ? 1 : 0);
    fprintf(f, "# The frame's 3D strength: its eye spacing as a multiple of yours. (default %.1f, range 1 to 10)\n",
            d.aim_frame_3d_strength);
    fprintf(f, "aim_frame_3d_strength = %.2f\n\n", g_config.aim_frame_3d_strength);
    fprintf(f, "# The frame's width in degrees at the first zoom level (_1) and the second and higher (_2).\n");
    fprintf(f, "# (default %.0f / %.0f, range 10 to 120)\n", d.aim_frame_size_deg[0], d.aim_frame_size_deg[1]);
    fprintf(f, "aim_frame_size_deg_1 = %.1f\n", g_config.aim_frame_size_deg[0]);
    fprintf(f, "aim_frame_size_deg_2 = %.1f\n\n", g_config.aim_frame_size_deg[1]);
    fprintf(f, "# How much further the frame narrows the game's own zoomed view, per level (1 = the game's).\n");
    fprintf(f, "# (default %.2f / %.2f, range 0.5 to 4)\n", d.aim_frame_strength[0], d.aim_frame_strength[1]);
    fprintf(f, "aim_frame_strength_1 = %.2f\n", g_config.aim_frame_strength[0]);
    fprintf(f, "aim_frame_strength_2 = %.2f\n\n", g_config.aim_frame_strength[1]);
    fprintf(f, "# Seconds the frame takes to move between the normal view and the frame. 0 = at once.\n");
    fprintf(f, "# (default %.2f, range 0 to 1)\n", d.aim_frame_move_s);
    fprintf(f, "aim_frame_move_s = %.2f\n\n", g_config.aim_frame_move_s);
    fprintf(f, "# The HUD inside the frame only, apart from the HUD layout and the Halo 4 helmet frame. 1 = the\n");
    fprintf(f, "# game's own layout, as on a flat screen, its edge art at the frame's edges. Size scales it whole;\n");
    fprintf(f, "# width and height stretch it side to side and top to bottom. Live in F1 > Scope.\n");
    fprintf(f, "# (default %.2f / %.2f / %.2f, size 0.5 to 1.25, width and height 0.5 to 1.5)\n",
            d.aim_frame_hud_size, d.aim_frame_hud_width_scale, d.aim_frame_hud_height_scale);
    fprintf(f, "aim_frame_hud_size = %.2f\n", g_config.aim_frame_hud_size);
    fprintf(f, "aim_frame_hud_width_scale = %.2f\n", g_config.aim_frame_hud_width_scale);
    fprintf(f, "aim_frame_hud_height_scale = %.2f\n\n", g_config.aim_frame_hud_height_scale);
    fprintf(f, "# Default experimental magnification used for every weapon.\n");
    fprintf(f, "# Initial zoom restored whenever R3 opens the scope; right-stick Y adjusts it.\n");
    fprintf(f, "# (default %.2f, range 6.0 to 24.0)\n", d.scope_zoom);
    fprintf(f, "scope_zoom = %.2f\n\n", g_config.scope_zoom);
    fprintf(f, "# Fixed physical screen width in meters; height is always 3/4 of width.\n");
    fprintf(f, "# (default %.3f, range 0.04 to 0.25)\n", d.scope_screen_width_m);
    fprintf(f, "scope_screen_width_m = %.3f\n\n", g_config.scope_screen_width_m);
    fprintf(f, "# Direct controller-local screen offsets in meters: right, up, forward.\n");
    fprintf(f, "# (defaults %.3f / %.3f / %.3f)\n",
            d.scope_screen_right_m, d.scope_screen_up_m, d.scope_screen_forward_m);
    fprintf(f, "scope_screen_right_m = %.3f\n", g_config.scope_screen_right_m);
    fprintf(f, "scope_screen_up_m = %.3f\n", g_config.scope_screen_up_m);
    fprintf(f, "scope_screen_forward_m = %.3f\n\n", g_config.scope_screen_forward_m);
    fprintf(f, "# Refresh the final zoom picture every Nth frame. 1 = full rate.\n");
    fprintf(f, "# (default %d, range 1 to 4)\n", d.scope_refresh_divisor);
    fprintf(f, "scope_refresh_divisor = %d\n\n", g_config.scope_refresh_divisor);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  HUD, PRESENTATION & PERFORMANCE\n");
    fprintf(f, "#  Shared intent; each title adapter maps it to that game's renderer.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Game brightness / gamma. 1.0 = the game's own; higher = brighter.\n");
    fprintf(f, "# Shared starting value; each game saves its own game_brightness profile below.\n");
    fprintf(f, "# (default %.2f, range 0.5 to 2)\n", d.game_brightness);
    fprintf(f, "game_brightness = %.2f\n\n", g_config.base_tunables.game_brightness);
    fprintf(f, "# How sharp the game renders inside the headset. ANY value in range\n");
    fprintf(f, "# works, so pick your own: 0.90 really means 90%%, not \"rounded to 80\".\n");
    fprintf(f, "# The named tiers are only shortcuts in the F1 menu:\n");
    fprintf(f, "#   0.50 potato   0.75 low     1.00 medium\n");
    fprintf(f, "#   1.30 high     1.80 ultra   2.64 keith david (8K-class)\n");
    fprintf(f, "# %d x %d is 1.00; your value scales both numbers together, so the\n",
            kNativeRenderWidth, kNativeRenderHeight);
    fprintf(f, "# picture keeps its shape at every setting.\n");
    fprintf(f, "#\n");
    fprintf(f, "# YES, YOU CAN GO WAY OVER 100%%. Above 1.00 is supersampling: the game\n");
    fprintf(f, "# renders bigger than the headset needs and the extra detail is\n");
    fprintf(f, "# squeezed down, which is the cleanest image available. Keith David\n");
    fprintf(f, "# (2.64) is true 8K width. The ceiling is %.2f (%d x %d), and the top\n",
            kResolutionScaleMax,
            (int)(kNativeRenderWidth * kResolutionScaleMax),
            (int)(kNativeRenderHeight * kResolutionScaleMax));
    fprintf(f, "# end needs a monster GPU. A bigger number is pulled back to %.2f\n",
            kResolutionScaleMax);
    fprintf(f, "# instead of accepted, so a typo (20 instead of 2.0) cannot leave you\n");
    fprintf(f, "# unable to start.\n");
    fprintf(f, "#\n");
    fprintf(f, "# WARNING: anything above about %.2f (~5K wide) is very heavy and can\n",
            kResolutionScaleHeavy);
    fprintf(f, "# crash weaker GPUs or refuse to start. Try it in short sessions, and\n");
    fprintf(f, "# drop back down if the game won't launch.\n");
    fprintf(f, "#\n");
    fprintf(f, "# CLOSE MCC COMPLETELY and relaunch after changing this one.\n");
    fprintf(f, "# The headset projection stays full-size; the complete eye is upscaled.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
            d.resolution_scale, kResolutionScaleMin, kResolutionScaleMax);
    fprintf(f, "resolution_scale = %.2f\n\n", g_config.resolution_scale);
    fprintf(f, "# Fit the desktop window to your monitor while the headset keeps the\n");
    fprintf(f, "# full resolution_scale render above. Turn this on when your render is\n");
    fprintf(f, "# larger than your monitor and MCC's menu (the \"Halo 3\" tile, Quit)\n");
    fprintf(f, "# falls off the screen edge where you can't click it. MCC still draws\n");
    fprintf(f, "# the full frame -- the headset picture and gun alignment do NOT change --\n");
    fprintf(f, "# the window is just shrunk to fit and the GPU downscales into it (no\n");
    fprintf(f, "# extra render pass, no measurable cost). 0 = off (default; the window\n");
    fprintf(f, "# behaves exactly as before). CLOSE MCC COMPLETELY and relaunch after\n");
    fprintf(f, "# changing this, the same as resolution_scale.\n");
    fprintf(f, "# (V5 default %d)\n", d.fit_desktop_window ? 1 : 0);
    fprintf(f, "fit_desktop_window = %d\n\n", g_config.fit_desktop_window ? 1 : 0);
    fprintf(f, "# Halo 3 and ODST: show one eye of the headset's picture on the monitor. 0 = the monitor stays on\n");
    fprintf(f, "# its last flat frame while in VR. (default %d)\n", d.desktop_mirror ? 1 : 0);
    fprintf(f, "desktop_mirror = %d\n\n", g_config.desktop_mirror ? 1 : 0);
    fprintf(f, "# Let the HEADSET set the frame rate, not your monitor. The VR frame\n");
    fprintf(f, "# is submitted inside MCC's desktop present, so MCC's V-Sync would\n");
    fprintf(f, "# pace your headset at your DESKTOP's refresh -- a 60 Hz desktop\n");
    fprintf(f, "# capping a 120 Hz headset. With this on, the desktop mirror presents\n");
    fprintf(f, "# unlocked and the VR runtime's own refresh (72, 90, 120, 144 Hz --\n");
    fprintf(f, "# whatever your headset reports) is the only clock. The desktop window\n");
    fprintf(f, "# may tear; the headset never does. 1 = on, 0 = stock passthrough.\n");
    fprintf(f, "# (default %d)\n", d.desktop_present_unlocked ? 1 : 0);
    fprintf(f, "desktop_present_unlocked = %d\n\n",
            g_config.desktop_present_unlocked ? 1 : 0);
    fprintf(f, "# Dormant co-op kick probe. This production build does not collect\n");
    fprintf(f, "# Present samples or write COOPPROBE rows; both are compile-time\n");
    fprintf(f, "# disabled. The key remains for a targeted diagnostic rebuild only.\n");
    fprintf(f, "# Changing it in this build has no effect. 1 = request collection\n");
    fprintf(f, "# when such a diagnostic rebuild is used, 0 = off.\n");
    fprintf(f, "# (default %d)\n", d.coop_probe ? 1 : 0);
    fprintf(f, "coop_probe = %d\n\n", g_config.coop_probe ? 1 : 0);
    fprintf(f, "# Draw distance: how far the game renders the whole scene, as a\n");
    fprintf(f, "# fraction of stock. Applies live to all three games (Halo 3, ODST,\n");
    fprintf(f, "# Reach). 1.00 = full stock draw distance. Lower brings the far plane\n");
    fprintf(f, "# in toward you, culling distant terrain and objects (the skybox goes\n");
    fprintf(f, "# first). Most levels only start visibly culling below ~0.25, since\n");
    fprintf(f, "# nearby geometry is closer than that; the lowest settings clip near\n");
    fprintf(f, "# geometry (hard pop-in) in exchange for the most frames. Helps weaker\n");
    fprintf(f, "# machines; VR is usually CPU-limited, not GPU. No restart needed.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
            d.draw_distance, kDrawDistanceMin, kDrawDistanceMax);
    fprintf(f, "draw_distance = %.2f\n\n", g_config.draw_distance);
    fprintf(f, "# Weather. Both are OFF by default because they read as a soft,\n");
    fprintf(f, "# washed-out picture in a headset. They drive the game's own switch\n");
    fprintf(f, "# for each effect and apply live, no restart. Halo: Reach is the\n");
    fprintf(f, "# game that honours these today; a game with no proven switch for an\n");
    fprintf(f, "# effect just leaves it alone and says so in HaloMCCVR.log.\n");
    fprintf(f, "#\n");
    fprintf(f, "# Rain: the streaks the rain renderer draws across your view.\n");
    fprintf(f, "# 1 = the game's normal rain, 0 = no rain.\n");
    fprintf(f, "# (default %d)\n", d.rain ? 1 : 0);
    fprintf(f, "rain = %d\n\n", g_config.rain ? 1 : 0);
    fprintf(f, "# Atmospheric fog: the distance haze that greys out far terrain and\n");
    fprintf(f, "# flattens contrast. Turning it off is the bigger clarity win of the\n");
    fprintf(f, "# two. 1 = the game's normal fog, 0 = no fog.\n");
    fprintf(f, "# (default %d)\n", d.atmospheric_fog ? 1 : 0);
    fprintf(f, "atmospheric_fog = %d\n\n", g_config.atmospheric_fog ? 1 : 0);
    fprintf(f, "# Upscale/resolve filter for the headset image: 0 = linear (old),\n");
    fprintf(f, "# 1 = sharp (strong bicubic). The game usually renders below your\n");
    fprintf(f, "# headset's per-eye resolution, so the mod upscales the difference;\n");
    fprintf(f, "# sharp keeps edges crisp instead of the linear shimmer. Live, no restart.\n");
    fprintf(f, "# (default %d)\n", d.upscale_filter);
    fprintf(f, "upscale_filter = %d\n\n", g_config.upscale_filter);
    fprintf(f, "# Sharpening strength (RCAS-based 2x overdrive). 0 = off, 1 = max.\n");
    fprintf(f, "# The top is intentionally aggressive and can ring/clip; adjust down.\n");
    fprintf(f, "# Same five taps and one pass as before. Live, no restart.\n");
    fprintf(f, "# (default %.2f, range 0 to 1)\n", d.sharpness);
    fprintf(f, "sharpness = %.2f\n\n", g_config.sharpness);
    fprintf(f, "# Anti-aliasing on the finished image: 0 = off, 1 = FXAA (balanced),\n");
    fprintf(f, "# 2 = FXAA Strong, 3 = SMAA 1x, 4 = SMAA 1x + FXAA Strong.\n");
    fprintf(f, "# Mode 4 is the most aggressive. SMAA modes cost more GPU when selected.\n");
    fprintf(f, "# Smooths jagged edges without a huge render resolution. Live.\n");
    fprintf(f, "# (default %d)\n", d.aa_mode);
    fprintf(f, "aa_mode = %d\n\n", g_config.aa_mode);
    fprintf(f, "# HUD size: fraction of the view the HUD lays out into. Smaller pulls\n");
    fprintf(f, "# shields/radar/ammo toward the center so both VR eyes see them.\n");
    fprintf(f, "# Halo 3, ODST, and Reach write this into their own HUD layout data.\n");
    fprintf(f, "# (default %.2f = calibrated stock layout, range 0.30 to 1.00)\n", d.hud_size);
    fprintf(f, "hud_size = %.2f\n\n", g_config.base_tunables.hud_size);
    fprintf(f, "# HUD width/aspect trim after automatic headset correction.\n");
    fprintf(f, "# 1 = automatic, lower = narrower, higher = wider.\n");
    fprintf(f, "# Applies to Halo 3, ODST, and Reach.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
            d.hud_aspect, kHudAspectMin, kHudAspectMax);
    fprintf(f, "hud_aspect = %.2f\n\n", g_config.base_tunables.hud_aspect);
    fprintf(f, "# HUD curvature: 0 = flat, 1 = fully curved.\n");
    fprintf(f, "# 0.50 keeps the active game's authored curvature.\n");
    fprintf(f, "# Applies to Halo 3 and ODST; not Halo: Reach or Halo 4 yet.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
            d.hud_curvature, kHudCurvatureMin, kHudCurvatureMax);
    fprintf(f, "hud_curvature = %.2f\n\n", g_config.base_tunables.hud_curvature);
    fprintf(f, "# HUD height in virtual-screen pixels. Positive = higher, negative = lower.\n");
    fprintf(f, "# Halo 3 and ODST; Reach and Halo 4 are not active here yet.\n");
    fprintf(f, "# (default %+.0f, range %+.0f to %+.0f)\n",
             d.hud_vertical_offset, kHudHeightMin, kHudHeightMax);
    fprintf(f, "hud_vertical_offset = %+.0f\n\n", g_config.base_tunables.hud_vertical_offset);
    fprintf(f, "# Game camera motion blur: 0 = off (VR default; also removes\n");
    fprintf(f, "# repeating stereo echo artifacts), 1 = the game's normal blur.\n");
    fprintf(f, "# (default %d)\n", d.motion_blur ? 1 : 0);
    fprintf(f, "motion_blur = %d\n\n", g_config.motion_blur ? 1 : 0);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  GAMEPLAY, HANDS & IK\n");
    fprintf(f, "#  Shared behavior with title-specific skeleton calibration underneath.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Automatically enter VR when a level loads (no F2/F11 needed).\n");
    fprintf(f, "# (default %d)\n", d.auto_vr ? 1 : 0);
    fprintf(f, "auto_vr = %d\n\n", g_config.auto_vr ? 1 : 0);
    fprintf(f, "# Optional pouch-to-gun reload (off by default) and the holster's reserve-weapon draw (on by\n");
    fprintf(f, "# default); native ammo, reload animation and inventory rules remain.\n");
    fprintf(f, "manual_reload = %d\nweapon_holsters = %d\n",g_config.manual_reload?1:0,g_config.weapon_holsters?1:0);
    fprintf(f, "# 1 = while manual reload or holsters are on, the game's reload and weapon-switch buttons do nothing; gestures only.\n");
    fprintf(f, "gesture_only_weapon_input = %d\n", g_config.gesture_only_weapon_input?1:0);
    fprintf(f, "manual_reload_disable_auto = %d\nmanual_reload_skip_animations = %d\n",
        g_config.manual_reload_disable_auto?1:0,g_config.manual_reload_skip_animations?1:0);
    fprintf(f, "# Shortened reload retains the native chambering tail; full animation skip takes precedence.\n");
    fprintf(f, "manual_reload_shortened_animation = %d\n",g_config.manual_reload_shortened_animation?1:0);
    fprintf(f, "weapon_pouch_down_m = %.3f\nweapon_body_zone_radius_m = %.3f\n",
        g_config.weapon_pouch_down_m,g_config.weapon_body_zone_radius_m);
    fprintf(f, "# Holster location: 0 = weapon-side shoulder, 1 = weapon-side hip, 2 = over the weapon-side\n");
    fprintf(f, "# shoulder (higher and further back: the Campaign Evolved mod's weapon-switch spot).\n");
    fprintf(f, "# 3 = your own spot, the three values below: metres from your head to the weapon side, up, and\n");
    fprintf(f, "# forward. Set it by hand: F1 > Controls > Reload and holsters > Calibrate Holster Location, close the menu,\n");
    fprintf(f, "# then squeeze the weapon grip where the holster should be. (defaults 0.198, 0.085, -0.284)\n");
    fprintf(f, "# (default location %d)\n", d.weapon_holster_location);
    fprintf(f, "weapon_holster_location = %d\n",g_config.weapon_holster_location);
    fprintf(f, "weapon_holster_custom_x_m = %.3f\nweapon_holster_custom_y_m = %.3f\nweapon_holster_custom_z_m = %.3f\n",
        g_config.weapon_holster_custom_x_m,g_config.weapon_holster_custom_y_m,g_config.weapon_holster_custom_z_m);
    fprintf(f,"weapon_pouch_location = %d\nweapon_pouch_offset_x_m = %.3f\nweapon_pouch_offset_y_m = %.3f\nweapon_pouch_offset_z_m = %.3f\n",
        g_config.weapon_pouch_location,g_config.weapon_pouch_offset_x_m,g_config.weapon_pouch_offset_y_m,g_config.weapon_pouch_offset_z_m);
    fprintf(f, "# Independent grab/insert radii. Click uses a fresh weapon grip in the holster.\n");
    fprintf(f, "# Slide preserves the draw gesture. With both on, click completes first.\n");
    fprintf(f, "# (defaults: holster radius %.2f, slide %d, click %d)\n", d.weapon_holster_radius_m,
        d.weapon_holster_slide ? 1 : 0, d.weapon_holster_click ? 1 : 0);
    fprintf(f, "weapon_holster_radius_m = %.3f\nweapon_insert_radius_m = %.3f\nweapon_holster_draw_m = %.3f\n",
        g_config.weapon_holster_radius_m,g_config.weapon_insert_radius_m,g_config.weapon_holster_draw_m);
    fprintf(f, "weapon_holster_slide = %d\nweapon_holster_click = %d\n",
        g_config.weapon_holster_slide?1:0,g_config.weapon_holster_click?1:0);
    fprintf(f, "# Dual wield: a shake presses that hand's reload button (Halo 3 both hands, Halo 2 right). Independent of manual_reload.\n");
    fprintf(f, "dual_wield_shake_reload = %d\n", g_config.dual_wield_shake_reload?1:0);
    fprintf(f, "# Recognized Needlers across all titles and Reach Needle Rifle; requires manual_reload.\n");
    fprintf(f, "# One rapid gun-hand out-and-back shake in any direction; no grip; settle to rearm.\n");
    fprintf(f, "weapon_needler_shake = %d\nweapon_shake_travel_m = %.3f\n",
        g_config.weapon_needler_shake?1:0,g_config.weapon_shake_travel_m);
    fprintf(f, "# Legacy transport fallback for titles without verified native action lookup.\n");
    fprintf(f, "# Show a generic blue reload item for detected unfamiliar/modded held models.\n");
    fprintf(f, "weapon_unknown_reload_visual = %d\n",g_config.weapon_unknown_reload_visual?1:0);
    fprintf(f, "# 0=X, 1=RB, 2=LB, 3=B, 4=Y, 5=A, 6=LT, 7=RT. CE/H2 share both graphics modes.\n");
    fprintf(f, "# Project Reclaimer: Halo 3 reload 0 (X, MCC's default) presses RB, Reclaimer's reload.\n");
    for(unsigned i=0;i<weapon_interaction::kTitleCount;++i)
    {
        fprintf(f,"weapon_reload_button_%s = %d\n",weapon_interaction::kTitleKeys[i],g_config.weapon_reload_button[i]);
        fprintf(f,"weapon_switch_button_%s = %d\n",weapon_interaction::kTitleKeys[i],g_config.weapon_switch_button[i]);
    }
    fprintf(f,"\n# VR action bindings: 0=the game's own layout, 1=unbound. Edited in F1 > Controls > VR Mappings.\n");
    fprintf(f,"# Physical crouch: lower your head by physical_crouch_depth_m (metres, 0.08 to 0.65) to crouch,\n"
              "# on foot. It needs MCC's hold-to-crouch setting: toggle crouch is not overridden.\n"
              "# 0 = crouch with the button only. (default %d, depth %.2f)\n",
        d.physical_crouch?1:0,d.physical_crouch_depth_m);
    fprintf(f,"physical_crouch = %d\nphysical_crouch_depth_m = %.3f\n",
        g_config.physical_crouch?1:0,g_config.physical_crouch_depth_m);
#if HALOMCCVR_DEV_DIAGNOSTICS
    fprintf(f,"# DEV BUILD ONLY, automated testing: accept an untracked (valid) head for roomscale and\n"
              "# physical crouch. Leave at 0 in a headset. (default 0)\n");
    fprintf(f,"test_accept_untracked_head = %d\n",g_config.test_accept_untracked_head?1:0);
#endif
    fprintf(f,"# On foot in Reach, Halo 3, ODST, Halo 4 and Halo 2: the most the view may drop below the\n"
              "# character's own eye (game metres), so a deep physical duck cannot put it inside his body.\n"
              "# Rides his crouch. -1 = off. (default 0.10)\n");
    fprintf(f,"head_leash_below_eye_m = %.3f\n",g_config.head_leash_below_eye_m);
    fprintf(f,"# With the physical crouch on: if the view was centred while you were crouched or bent over before\n");
    fprintf(f,"# the mod knew your standing height (the first minute of a session), you stood too tall in the game\n");
    fprintf(f,"# until the next recenter. 1 = once your standing height is known the view settles to it by itself\n");
    fprintf(f,"# (Halo 3, ODST, Reach, Halo 4; on foot). 0 = as before. No menu control (log: VIEW HEIGHT).\n");
    fprintf(f,"# (default %d)\n",d.view_follows_standing_height?1:0);
    fprintf(f,"view_follows_standing_height = %d\n",g_config.view_follows_standing_height?1:0);
    fprintf(f,"# Arm-swing locomotion: speed is a fraction of native maximum movement.\n");
    fprintf(f,"physical_running = %d\nphysical_running_speed = %.3f\nphysical_running_sensitivity = %.3f\n",
        g_config.physical_running?1:0,g_config.physical_running_speed,g_config.physical_running_sensitivity);
    fprintf(f,"vr_action_mapping = %d\nflashlight_suppress_on_two_hand = %d\n",
        g_config.vr_action_mapping?1:0,g_config.flashlight_suppress_on_two_hand?1:0);
    fprintf(f,"# The controls preset last chosen in F1 > Controls. (default %s)\ninput_preset = %s\n",
        d.input_preset,g_config.input_preset);
    fprintf(f,"# VR Mappings: each row's inputs. An input is its held controls and its press joined by '+', the press\n"
              "# last, then \" toggle\" for a toggle. A row's own input is its key alone (0 = Automatic); its further\n"
              "# ones are _2 to _4 (%d = none). Only what differs from the default controls is written.\n"
              "vr_inputs_version = 3\n",vr_mapping::Unbound);
    char input[32];
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
    {
        vr_mapping::Overrides startRows{};
        vr_mapping::Inputs start{};
        vr_mapping::ExtraBindings startExtra{};
        default_controls::Rows(static_cast<GameTitle>(title+1),startRows,start,startExtra);
        for(unsigned action=0;action<vr_mapping::Count;++action)
        {
            const auto& row=g_config.vr_inputs[title][action];
            if(g_config.vr_bindings[title][action]!=startRows[action]||row.held!=start[action].held||
               row.mode!=start[action].mode)
            {
                vr_mapping::FormatInput(input,row.held,g_config.vr_bindings[title][action],row.mode);
                fprintf(f,"vr_bind_%s_%s = %s\n",weapon_interaction::kTitleKeys[title],vr_mapping::kKeys[action],input);
            }
            if(row.more==start[action].more) continue;
            for(unsigned slot=1;slot<vr_mapping::kRowInputs;++slot)
            {
                const auto& more=row.more[slot-1];
                vr_mapping::FormatInput(input,more.held,more.press,more.mode);
                fprintf(f,"vr_bind_%s_%s%s = %s\n",weapon_interaction::kTitleKeys[title],vr_mapping::kKeys[action],
                    vr_mapping::kControlSuffix[slot],input);
            }
        }
    }
    fprintf(f,"# The extra rows: each game's actions no row above names, their inputs (_2 to _4 further ones), each in its\n"
        "# context; only those that differ from the default controls.\n");
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
    {
        vr_mapping::Overrides startRows{};
        vr_mapping::Inputs start{};
        vr_mapping::ExtraBindings startExtra{};
        default_controls::Rows(static_cast<GameTitle>(title+1),startRows,start,startExtra);
        for(unsigned action=0;action<vr_mapping::ExtraCount;++action)
            for(unsigned slot=0;slot<vr_mapping::kRowInputs&&g_config.vr_extra_bindings[title][action]!=startExtra[action];
                ++slot)
            {
                const auto& seat=g_config.vr_extra_bindings[title][action][slot];
                vr_mapping::FormatInput(input,seat.held,seat.press,seat.mode);
                fprintf(f,"vr_bind_%s_%s%s = %s\n",weapon_interaction::kTitleKeys[title],vr_mapping::kExtraKeys[action],
                    vr_mapping::kControlSuffix[slot],input);
            }
    }
    fprintf(f,"# The mod's own rows, every game's: Pause (the pause view and Start), its inputs (_2 to _4 further ones).\n");
    for(unsigned action=0;action<vr_mapping::ModCount;++action)
        for(unsigned slot=0;slot<vr_mapping::kRowInputs;++slot)
        {
            const auto& mod=g_config.vr_mod_bindings[action][slot];
            vr_mapping::FormatInput(input,mod.held,mod.press,mod.mode);
            fprintf(f,"vr_bind_%s%s = %s\n",vr_mapping::kModKeys[action],vr_mapping::kControlSuffix[slot],input);
        }
    fprintf(f,"\n# Native localized subtitles: independent gameplay and theatre placement.\n");
    fprintf(f,"# Per-game muzzle-flash hiding. Supported paths: H2 Original and H4.\n");
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        fprintf(f,"hide_muzzle_flash_%s = %d\n",weapon_interaction::kTitleKeys[title],g_config.hide_muzzle_flash[title]?1:0);
    fprintf(f,"disable_autoexposure_odst = %d\n",g_config.disable_autoexposure_odst?1:0);
    fprintf(f,"# Stock bloom per game. Optional suppression supports H3, ODST and Reach.\n");
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        fprintf(f,"bloom_enabled_%s = %d\n",weapon_interaction::kTitleKeys[title],g_config.bloom_enabled[title]?1:0);
    fprintf(f,"# 1 = the bloom setting above is your own choice. 0: ODST and Reach play without bloom in VR\n"
              "# and the value above is left as it was.\n");
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        fprintf(f,"bloom_chosen_%s = %d\n",weapon_interaction::kTitleKeys[title],g_config.bloom_chosen[title]?1:0);
    fprintf(f,"vr_gameplay_subtitles = %d\nvr_gameplay_subtitle_scale = %.3f\nvr_gameplay_subtitle_x = %.3f\nvr_gameplay_subtitle_y = %.3f\nvr_gameplay_subtitle_anchor = %d\n",
        g_config.vr_gameplay_subtitles?1:0,g_config.vr_gameplay_subtitle_scale,g_config.vr_gameplay_subtitle_x,g_config.vr_gameplay_subtitle_y,g_config.vr_gameplay_subtitle_anchor);
    fprintf(f,"vr_theatre_subtitle_scale = %.3f\nvr_theatre_subtitle_x = %.3f\nvr_theatre_subtitle_y = %.3f\nvr_theatre_subtitle_anchor = %d\n",
        g_config.vr_theatre_subtitle_scale,g_config.vr_theatre_subtitle_x,g_config.vr_theatre_subtitle_y,g_config.vr_theatre_subtitle_anchor);
    fprintf(f,"\n# Optional DLSS (0=off, 1=on); modes: DLAA/Quality/Balanced/Performance/Ultra Performance.\n");
    fprintf(f,"# Model preset: 0=runtime default, 1=F, 2=J, 3=K, 4=L, 5=M, 6=E.\n");
    fprintf(f,"upscaler = %d\ndlss_mode = %d\ndlss_jitter = %d\ndlss_preset = %d\ndlss_debug_view = %d\n",
        g_config.upscaler,g_config.dlss_mode,g_config.dlss_jitter?1:0,g_config.dlss_preset,g_config.dlss_debug_view?1:0);
    fprintf(f,"\n");
    fprintf(f, "# Two-handed aiming: support hand on the gun front acquires in the barrel grab zone;\n");
    fprintf(f, "# aim follows the two-hand line. 1 = on. Engage style is Toggle (grip click) or Hold (held grip).\n");
    fprintf(f, "# First person only: whenever the game leaves first person (any vehicle seat or turret, a third-person,\n");
    fprintf(f, "# death or Monitor camera, a cutscene) your main (aim) hand aims alone, in every game.\n");
    fprintf(f, "# (default %d)\n", d.two_handed_aim ? 1 : 0);
    fprintf(f, "two_handed_aim = %d\n\n", g_config.two_handed_aim ? 1 : 0);
    fprintf(f, "# Experimental: consume the latest committed coherent Aim/Grip/head sample in XInput aim.\n");
    fprintf(f, "# (default %d)\n", d.two_hand_coherent_aim ? 1 : 0);
    fprintf(f, "two_hand_coherent_aim = %d\n\n",
        g_config.two_hand_coherent_aim ? 1 : 0);
    fprintf(f, "# Virtual stock changes only the base engaged two-hand orientation using the configured\n");
    fprintf(f, "# rear-reference blend. Base position/roll stay primary-controller owned;\n");
    fprintf(f, "# verified barrel-origin substitution (gun_barrel_aim) remains separate.\n");
    fprintf(f, "# (default %d)\n", d.virtual_stock ? 1 : 0);
    fprintf(f, "virtual_stock = %d\n", g_config.virtual_stock ? 1 : 0);
    fprintf(f, "# Standard and Plus remember independent rear-reference influence values.\n");
    fprintf(f, "# 0 = exact controller-to-controller solver; 1 = fully anchored stock.\n");
    fprintf(f, "# (Standard default %.2f, Plus default %.2f, range %.2f to %.2f)\n",
        d.virtual_stock_standard_strength,
        d.virtual_stock_plus_strength,
        kVirtualStockStrengthMinimum,
        kVirtualStockStrengthMaximum);
    fprintf(f, "virtual_stock_standard_strength = %.2f\n",
        g_config.virtual_stock_standard_strength);
    fprintf(f, "virtual_stock_plus_strength = %.2f\n",
        g_config.virtual_stock_plus_strength);
    fprintf(f, "# Tracking-space vertical offset for the virtual rear reference, in meters.\n");
    fprintf(f, "# 0.000 = headset height. Negative values lower the virtual rear reference.\n");
    fprintf(f, "# (default %.3f, range %.2f to %.2f)\n",
        d.virtual_stock_rear_height_m,
        kVirtualStockRearHeightMinimumM,
        kVirtualStockRearHeightMaximumM);
    fprintf(f, "virtual_stock_rear_height_m = %.3f\n", g_config.virtual_stock_rear_height_m);
    fprintf(f, "# Virtual Stock mode/reference: 3 = Plus; 0 = Standard Centre; 1 = Standard Shoulder.\n");
    fprintf(f, "# Legacy Chest value 2 loads as Standard Centre.\n");
    fprintf(f, "# (default %d)\n", d.virtual_stock_rear_reference);
    fprintf(f, "virtual_stock_rear_reference = %d\n", g_config.virtual_stock_rear_reference);
    fprintf(f, "# Shoulder offsets in meters: back from the head rear target and toward\n");
    fprintf(f, "# the semantic firing-hand side.\n");
    fprintf(f, "# (defaults %.3f / %.3f, ranges %.3f..%.3f / %.3f..%.3f)\n",
        d.virtual_stock_shoulder_back_m, d.virtual_stock_shoulder_side_m,
        kVirtualStockShoulderBackMinimumM, kVirtualStockShoulderBackMaximumM,
        kVirtualStockShoulderSideMinimumM, kVirtualStockShoulderSideMaximumM);
    fprintf(f, "virtual_stock_shoulder_back_m = %.3f\n", g_config.virtual_stock_shoulder_back_m);
    fprintf(f, "virtual_stock_shoulder_side_m = %.3f\n", g_config.virtual_stock_shoulder_side_m);
    fprintf(f, "# Legacy Chest compatibility values; not exposed by the product UI.\n");
    fprintf(f, "# (defaults %.3f / %.3f / %.3f, ranges %.3f..%.3f / %.3f..%.3f / %.3f..%.3f)\n",
        d.virtual_stock_chest_height_m, d.virtual_stock_chest_back_m,
        d.virtual_stock_chest_side_m,
        kVirtualStockChestHeightMinimumM, kVirtualStockChestHeightMaximumM,
        kVirtualStockChestBackMinimumM, kVirtualStockChestBackMaximumM,
        kVirtualStockChestSideMinimumM, kVirtualStockChestSideMaximumM);
    fprintf(f, "virtual_stock_chest_height_m = %.3f\n", g_config.virtual_stock_chest_height_m);
    fprintf(f, "virtual_stock_chest_back_m = %.3f\n", g_config.virtual_stock_chest_back_m);
    fprintf(f, "virtual_stock_chest_side_m = %.3f\n", g_config.virtual_stock_chest_side_m);
    fprintf(f, "# Legacy adaptive-sheet compatibility values; not exposed by the product UI.\n");
    fprintf(f, "# (defaults %.3f / %.3f / %.3f / %.3f, ranges %.3f..%.3f / %.3f..%.3f / %.3f..%.3f / %.3f..%.3f)\n",
        d.virtual_stock_adaptive_top_height_m,
        d.virtual_stock_adaptive_bottom_height_m,
        d.virtual_stock_adaptive_top_half_width_m,
        d.virtual_stock_adaptive_bottom_half_width_m,
        kVirtualStockAdaptiveTopHeightMinimumM,
        kVirtualStockAdaptiveTopHeightMaximumM,
        kVirtualStockAdaptiveBottomHeightMinimumM,
        kVirtualStockAdaptiveBottomHeightMaximumM,
        kVirtualStockAdaptiveTopHalfWidthMinimumM,
        kVirtualStockAdaptiveTopHalfWidthMaximumM,
        kVirtualStockAdaptiveBottomHalfWidthMinimumM,
        kVirtualStockAdaptiveBottomHalfWidthMaximumM);
    fprintf(f, "virtual_stock_adaptive_top_height_m = %.3f\n",
        g_config.virtual_stock_adaptive_top_height_m);
    fprintf(f, "virtual_stock_adaptive_bottom_height_m = %.3f\n",
        g_config.virtual_stock_adaptive_bottom_height_m);
    fprintf(f, "virtual_stock_adaptive_top_half_width_m = %.3f\n",
        g_config.virtual_stock_adaptive_top_half_width_m);
    fprintf(f, "virtual_stock_adaptive_bottom_half_width_m = %.3f\n",
        g_config.virtual_stock_adaptive_bottom_half_width_m);
    fprintf(f, "# Plus internal support authority, retained for config compatibility.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
        d.virtual_stock_hybrid_offhand_influence,
        kVirtualStockHybridOffhandInfluenceMinimum,
        kVirtualStockHybridOffhandInfluenceMaximum);
    fprintf(f, "virtual_stock_hybrid_offhand_influence = %.2f\n",
        g_config.virtual_stock_hybrid_offhand_influence);
    fprintf(f, "# Plus rear reference: 0 = Centre (internal Head), 1 = Shoulder.\n");
    fprintf(f, "# (default %d)\n", d.virtual_stock_hybrid_ads_reference);
    fprintf(f, "virtual_stock_hybrid_ads_reference = %d\n",
        g_config.virtual_stock_hybrid_ads_reference);
    fprintf(f, "# Plus broad safety distances in meters. The 0.010 m minimum\n");
    fprintf(f, "# threshold interval keeps a finite transition and accepts normal VR seating.\n");
    fprintf(f, "# (defaults %.3f / %.3f, ranges %.3f..%.3f / %.3f..%.3f m)\n",
        d.virtual_stock_hybrid_seat_full_m,
        d.virtual_stock_hybrid_seat_release_m,
        kVirtualStockHybridSeatFullMinimumM,
        kVirtualStockHybridSeatFullMaximumM,
        kVirtualStockHybridSeatReleaseMinimumM,
        kVirtualStockHybridSeatReleaseMaximumM);
    fprintf(f, "virtual_stock_hybrid_seat_full_m = %.3f\n",
        g_config.virtual_stock_hybrid_seat_full_m);
    fprintf(f, "virtual_stock_hybrid_seat_release_m = %.3f\n",
        g_config.virtual_stock_hybrid_seat_release_m);
    fprintf(f, "# Plus horizontal unloading. These are product defaults and are not normal UI tuning.\n");
    fprintf(f, "virtual_stock_hybrid_horizontal_release = %d\n",
        g_config.virtual_stock_hybrid_horizontal_release ? 1 : 0);
    fprintf(f, "virtual_stock_hybrid_horizontal_full_m = %.3f\n",
        g_config.virtual_stock_hybrid_horizontal_full_m);
    fprintf(f, "virtual_stock_hybrid_horizontal_release_m = %.3f\n",
        g_config.virtual_stock_hybrid_horizontal_release_m);
    fprintf(f, "# Stateless proximity release: 1 fades virtual stock as the raw primary hand\n");
    fprintf(f, "# moves away from the full rear target. 0 preserves the current solver.\n");
    fprintf(f, "# (default %d)\n", d.virtual_stock_proximity_release ? 1 : 0);
    fprintf(f, "virtual_stock_proximity_release = %d\n", g_config.virtual_stock_proximity_release ? 1 : 0);
    fprintf(f, "# Full stock through this distance; release reaches zero at the second value.\n");
    fprintf(f, "# (defaults %.3f / %.3f, ranges %.2f..%.2f / %.2f..%.2f m)\n",
        d.virtual_stock_proximity_full_m, d.virtual_stock_proximity_release_m,
        kVirtualStockProximityFullMinimumM,
        kVirtualStockProximityFullMaximumM,
        kVirtualStockProximityReleaseMinimumM,
        kVirtualStockProximityReleaseMaximumM);
    fprintf(f, "virtual_stock_proximity_full_m = %.3f\n", g_config.virtual_stock_proximity_full_m);
    fprintf(f, "virtual_stock_proximity_release_m = %.3f\n\n",
        g_config.virtual_stock_proximity_release_m);
    fprintf(f, "# Reduce support-hand rotation while preserving support-position steering.\n");
    fprintf(f, "# (default %d)\n", d.two_hand_support_grip_pose ? 1 : 0);
    fprintf(f, "two_hand_support_grip_pose = %d\n\n", g_config.two_hand_support_grip_pose ? 1 : 0);
    fprintf(f, "# Durable owner-bound support grip; off restores spatial-only support behaviour.\n");
    fprintf(f, "# (default %d)\n", d.persistent_support_grip ? 1 : 0);
    fprintf(f, "persistent_support_grip = %d\n\n", g_config.persistent_support_grip ? 1 : 0);
    fprintf(f, "# Pavlov-inspired controller-input smoothing strength: 0 = off/raw controller\n");
    fprintf(f, "# input; 25 = the full fixed speed-25 quaternion-native filter; values between\n");
    fprintf(f, "# wet/dry mix the filtered directional input copies over raw. Two-handed aim\n");
    fprintf(f, "# with Virtual Stock on or off; raw poses and the weapon/base position are\n");
    fprintf(f, "# never smoothed.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
        d.two_hand_smoothing_strength,
        kTwoHandSmoothingStrengthMinimum,
        kTwoHandSmoothingStrengthMaximum);
    fprintf(f, "two_hand_smoothing_strength = %.2f\n\n",
        g_config.two_hand_smoothing_strength);
    fprintf(f, "# Free two-hand (VS-OFF) support-steering authority: 0 = the primary\n");
    fprintf(f, "# controller's own aim is authoritative; 1 = the support controller's aim\n");
    fprintf(f, "# carries equal authority. VS-OFF free two-hand aiming only; Virtual Stock\n");
    fprintf(f, "# is unaffected.\n");
    fprintf(f, "# (default %.2f, range %.2f to %.2f)\n",
        d.two_hand_offhand_influence,
        kTwoHandOffhandInfluenceMinimum,
        kTwoHandOffhandInfluenceMaximum);
    fprintf(f, "two_hand_offhand_influence = %.2f\n\n",
        g_config.two_hand_offhand_influence);
    // two_hand_transition_smoothing is retired: the fixed 200 ms VS-OFF
    // acquire/release continuity is internal product behaviour and the key is
    // no longer written. Historical files may still contain it; ConfigLoad
    // parses it and ignores it.
    fprintf(f, "# Persistent-grip switch option: 1 = a weapon switch while the grip is held\n");
    fprintf(f, "# keeps the two-hand hold on the new weapon immediately (no re-orientation).\n");
    fprintf(f, "# 0 = grip the new weapon normally; the grip button need not be released.\n");
    fprintf(f, "# (default %d)\n", d.two_hand_switch_inherit ? 1 : 0);
    fprintf(f, "two_hand_switch_inherit = %d\n\n", g_config.two_hand_switch_inherit ? 1 : 0);
    fprintf(f, "# Two-handed aim follows the weapon's grip zone: a pistol-style hold never steers, a long gun always\n");
    fprintf(f, "# does, so a tracking drop-out cannot switch it. 0 = decided by how far apart the hands read each\n");
    fprintf(f, "# frame. (default %d)\n", d.two_hand_grip_zone_authority ? 1 : 0);
    fprintf(f, "two_hand_grip_zone_authority = %d\n\n", g_config.two_hand_grip_zone_authority ? 1 : 0);
    fprintf(f, "# One-handed aim range in metres. With that 1: a weapon whose grip is within this of the main hand\n");
    fprintf(f, "# aims one-handed. With that 0: how close the hands must be for the main hand to aim alone. (default\n");
    fprintf(f, "# %.2f, range 0.06 to 0.48)\n", d.two_hand_one_hand_range_m);
    fprintf(f, "two_hand_one_hand_range_m = %.2f\n\n", g_config.two_hand_one_hand_range_m);
    fprintf(f, "# Main weapon, aim and trigger on the physical left controller.\n");
    fprintf(f, "# Movement, turning and face buttons keep their physical bindings.\n");
    fprintf(f, "left_handed = %d\n\n", g_config.left_handed ? 1 : 0);
    fprintf(f, "# Experimental anatomical hand alignment; only active with left_handed.\n");
    fprintf(f, "# Default off preserves the released hand positioning.\n");
    fprintf(f, "experimental_hand_alignment = %d\n\n", g_config.experimental_hand_alignment ? 1 : 0);
    fprintf(f, "# Engage style: 1 = toggle (click grip on/off), 0 = hold.\n");
    fprintf(f, "# (default %d)\n", d.two_hand_toggle ? 1 : 0);
    fprintf(f, "two_hand_toggle = %d\n\n", g_config.two_hand_toggle ? 1 : 0);
    fprintf(f, "# Left controller wrist-to-palm correction for support-hand seating/IK, in meters.\n");
    fprintf(f, "# Excluded from the two-hand aim line, which uses raw tracked points.\n");
    fprintf(f, "# (default %.3f, range -0.15 to 0.30)\n", d.left_hand_forward_m);
    fprintf(f, "left_gun_forward_m = %.3f\n", g_config.base_tunables.left_gun_forward_m);
    fprintf(f, "left_gun_right_m = %.3f\n", g_config.base_tunables.left_gun_right_m);
    fprintf(f, "left_gun_up_m = %.3f\n", g_config.base_tunables.left_gun_up_m);
    fprintf(f, "# 1 = left_hand_forward_m below is the extra on top of the left-gun offsets.\n");
    fprintf(f, "left_hand_forward_reset_version = 1\n");
    // Halo 4's HUD height runs the same way as every game's (positive raises it); see ConfigLoad.
    fprintf(f, "halo4_hud_height_version = 2\n");
    fprintf(f, "left_hand_forward_m = %.3f\n\n", g_config.base_tunables.left_hand_forward_m);
    fprintf(f, "# Sideways nudge of the two-hand grab zone (+ = player's right), so the\n");
    fprintf(f, "# grab line sits on the visible barrel, in meters (absolute; menu zero = 0.080 m).\n");
    fprintf(f, "# (default %.3f, absolute range -0.02 to 0.18; menu -0.10 to 0.10)\n", d.two_hand_zone_right_m);
    fprintf(f, "# 1 = two_hand_zone_right_m below is measured with the 0.080 m default.\n");
    fprintf(f, "two_hand_zone_baseline_version = 1\n");
    fprintf(f, "two_hand_zone_right_m = %.3f\n\n", g_config.two_hand_zone_right_m);
    fprintf(f, "# Support-controller-to-palm depth for two-hand grab acquisition only, in meters.\n");
    fprintf(f, "# Does not change aim geometry, hand seating, or shots.\n");
    fprintf(f, "# (default %.3f, range -0.05 to 0.25)\n", d.left_grip_forward_m);
    fprintf(f, "left_grip_forward_m = %.3f\n\n", g_config.left_grip_forward_m);
    fprintf(f, "# VRIK arm IK: 1 = bend the arm to your controller (shoulder planted,\n");
    fprintf(f, "# elbow solved); 0 = rigid-parent the whole arm assembly. Required on\n");
    fprintf(f, "# Halo 3 / ODST, so leave it 1.\n");
    fprintf(f, "# (default %d)\n", d.arm_ik ? 1 : 0);
    fprintf(f, "arm_ik = %d\n\n", g_config.arm_ik ? 1 : 0);
    fprintf(f, "# Floating hands: 1 = show only the hands and the guns they hold\n");
    fprintf(f, "# (arms hidden); 0 = show arms where a verified tracked chain is available.\n");
    fprintf(f, "# (default %d)\n", d.floating_hands ? 1 : 0);
    fprintf(f, "floating_hands = %d\n\n", g_config.floating_hands ? 1 : 0);
    fprintf(f, "# World collision (experimental): hands and held weapons stop on\n");
    fprintf(f, "# world geometry and provide gentle contact haptics. Implemented for\n");
    fprintf(f, "# Halo 2, Halo 3, ODST, Reach, and Halo 4.\n");
    fprintf(f, "# (default %d)\n", d.world_collision ? 1 : 0);
    fprintf(f, "world_collision = %d\n\n", g_config.world_collision ? 1 : 0);
    fprintf(f, "# True physical melee (experimental): a fast tracked hand/weapon\n");
    fprintf(f, "# contact damages the struck target. Independent of world_collision.\n");
    fprintf(f, "# Implemented for Halo 2, Halo 3, ODST, Reach, and Halo 4.\n");
    fprintf(f, "# (default %d)\n", d.physical_melee ? 1 : 0);
    fprintf(f, "physical_melee = %d\n", g_config.physical_melee ? 1 : 0);
    fprintf(f, "# Gesture melee: connect a fast swing to the active game's melee binding.\n");
    fprintf(f, "# (default %d)\n", d.gesture_melee ? 1 : 0);
    fprintf(f, "gesture_melee = %d\n", g_config.gesture_melee ? 1 : 0);
    fprintf(f, "# What counts as a swing, measured relative to your head. Speed in m/s (default %.2f), how fast the\n"
               "# arm must be extending in m/s (default %.2f), how far from your head the hand must be in metres\n"
               "# (default %.2f), and how far an off-hand chop must travel instead of extending in metres,\n"
               "# 0 = off (default %.2f). require_grip: only with that hand's grip held (default %d).\n",
            d.gesture_melee_speed, d.gesture_melee_extension, d.gesture_melee_reach, d.gesture_melee_travel,
            d.gesture_melee_require_grip ? 1 : 0);
    fprintf(f, "gesture_melee_speed = %.2f\ngesture_melee_extension = %.2f\ngesture_melee_reach = %.2f\n"
               "gesture_melee_travel = %.2f\ngesture_melee_require_grip = %d\n",
            g_config.gesture_melee_speed, g_config.gesture_melee_extension, g_config.gesture_melee_reach,
            g_config.gesture_melee_travel, g_config.gesture_melee_require_grip ? 1 : 0);
    fprintf(f, "# off_hand: the hand without the gun punches too (default %d). directional: the aim follows the swing\n"
               "# for aim_hold_ms, then blends back over aim_ramp_ms (default %d, %d, %d).\n",
            d.gesture_melee_off_hand ? 1 : 0, d.gesture_melee_directional ? 1 : 0, d.gesture_melee_aim_hold_ms,
            d.gesture_melee_aim_ramp_ms);
    fprintf(f, "gesture_melee_off_hand = %d\ngesture_melee_directional = %d\ngesture_melee_aim_hold_ms = %d\n"
               "gesture_melee_aim_ramp_ms = %d\n",
            g_config.gesture_melee_off_hand ? 1 : 0, g_config.gesture_melee_directional ? 1 : 0,
            g_config.gesture_melee_aim_hold_ms, g_config.gesture_melee_aim_ramp_ms);
    fprintf(f, "# File only. How long the press is held and the time between swings, in ms (default %d, %d); no swing this\n"
               "# soon after a reload or holster gesture, ms (default %d); velocity smoothing, ms (default %.0f); faster\n"
               "# than this m/s or further than this m is tracking jumping (default %.1f, %.2f); read the hands after\n"
               "# Hand smoothing instead of as they moved (default %d); log every swing (default %d).\n",
            d.gesture_melee_press_ms, d.gesture_melee_cooldown_ms, d.gesture_melee_holster_veto_ms,
            d.gesture_melee_smoothing_ms, d.gesture_melee_max_speed, d.gesture_melee_max_reach,
            d.gesture_melee_smoothed_hands ? 1 : 0, d.gesture_melee_log ? 1 : 0);
    fprintf(f, "gesture_melee_press_ms = %d\ngesture_melee_cooldown_ms = %d\ngesture_melee_holster_veto_ms = %d\n"
               "gesture_melee_smoothing_ms = %.1f\ngesture_melee_max_speed = %.1f\ngesture_melee_max_reach = %.2f\n"
               "gesture_melee_smoothed_hands = %d\ngesture_melee_log = %d\n",
            g_config.gesture_melee_press_ms, g_config.gesture_melee_cooldown_ms, g_config.gesture_melee_holster_veto_ms,
            g_config.gesture_melee_smoothing_ms, g_config.gesture_melee_max_speed, g_config.gesture_melee_max_reach,
            g_config.gesture_melee_smoothed_hands ? 1 : 0, g_config.gesture_melee_log ? 1 : 0);
    fprintf(f, "# Required controller/weapon sample speed in metres per second.\n");
    fprintf(f, "# (default %.2f, range 0.3 to 10.0)\n",
            d.physical_melee_swing_speed);
    fprintf(f, "physical_melee_swing_speed = %.2f\n\n",
            g_config.physical_melee_swing_speed);
    fprintf(f, "# Lower the right (weapon) shoulder so the arm doesn't clip your face\n");
    fprintf(f, "# (0 = authored, higher = lower; ~world units).\n");
    fprintf(f, "# (default %.3f, range 0 to 0.3)\n", d.right_shoulder_drop);
    fprintf(f, "right_shoulder_drop = %.3f\n\n", g_config.right_shoulder_drop);
    fprintf(f, "# Push BOTH shoulders back toward your torso, along your heading.\n");
    fprintf(f, "# Some titles (e.g. ODST) plant the shoulders in front of you;\n");
    fprintf(f, "# raise this until they sit at your body. Negative = forward.\n");
    fprintf(f, "# (default %.3f, range -0.3 to 0.3; ~world units)\n", d.shoulder_back_m);
    fprintf(f, "shoulder_back_m = %.3f\n\n", g_config.shoulder_back_m);
    fprintf(f, "# Keep the IK shoulders level with the horizon instead of pitching\n");
    fprintf(f, "# with your head. 1 = level torso (shoulders stay put); 0 = old.\n");
    fprintf(f, "# (default %d)\n", d.shoulder_level ? 1 : 0);
    fprintf(f, "shoulder_level = %d\n\n", g_config.shoulder_level ? 1 : 0);
    fprintf(f, "# VRIK stage A1: show the player's game-animated body (experimental).\n");
    fprintf(f, "# (default %d)\n", d.body_wip ? 1 : 0);
    fprintf(f, "body_wip = %d\n\n", g_config.body_wip ? 1 : 0);
    fprintf(f, "# Experimental H3/H4 tracked avatars and supported free-hand finger poses.\n");
    fprintf(f, "# Overrides the legacy H3 body switch; unsupported rigs retain first-person hands.\n");
    fprintf(f, "experimental_body_ik = %d\n", g_config.experimental_body_ik ? 1 : 0);
    fprintf(f, "# Hide separate lower-body regions; H3 Elite/Arbiter require this off.\n");
    fprintf(f, "body_ik_hide_lower = %d\n\n", g_config.body_ik_hide_lower ? 1 : 0);
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  DEVELOPMENT DIAGNOSTICS\n");
    fprintf(f, "#  Leave these off unless a developer asks you to enable one.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Diagnostic: 1 ignores the controller and pushes the weapon a fixed\n");
    fprintf(f, "# distance left, to test whether the gun mesh reads our matrices.\n");
    fprintf(f, "# (default %d)\n", d.weapon_probe ? 1 : 0);
    fprintf(f, "weapon_probe = %d\n\n", g_config.weapon_probe ? 1 : 0);
    fprintf(f, "# Diagnostic: 1 logs the CHUD state-byte window on change (finds the\n");
    fprintf(f, "# enemy-red reticle state and per-element HUD flags). Log-only.\n");
    fprintf(f, "# Not in the F1 menu; this file only. (default %d)\n", d.hud_probe ? 1 : 0);
    fprintf(f, "hud_probe = %d\n\n", g_config.hud_probe ? 1 : 0);
    fprintf(f, "# Diagnostic: 1 logs each distinct scene render target MCC binds\n");
    fprintf(f, "# (size/format/flags + viewport) so toggling MCC's built-in FSR shows\n");
    fprintf(f, "# what it changes. Not in the F1 menu; this file only. (default %d)\n",
            d.fsr_probe ? 1 : 0);
    fprintf(f, "fsr_probe = %d\n\n", g_config.fsr_probe ? 1 : 0);
    fprintf(f, "# Diagnostic: log camera-vs-muzzle offset on each shot (bullet origin).\n");
    fprintf(f, "# Not in the F1 menu; this file only. (default %d)\n", d.bullet_probe ? 1 : 0);
    fprintf(f, "bullet_probe = %d\n\n", g_config.bullet_probe ? 1 : 0);
    fprintf(f, "# Ghosting diagnostic: 1 renders the right eye first.\n");
    fprintf(f, "# (default %d)\n", d.right_eye_first ? 1 : 0);
    fprintf(f, "right_eye_first = %d\n", g_config.right_eye_first ? 1 : 0);
    fprintf(f, "# Halo 3: the mod's hooks are installed again at each new level. 1 = an install re-uses\n");
    fprintf(f, "# what an earlier one found in the same load of the game's DLL and searches it only for what is new.\n");
    fprintf(f, "# 0 = every install searches all of it again. (default %d)\n", d.install_search_memory);
    fprintf(f, "install_search_memory = %d\n", g_config.install_search_memory);
    fprintf(f, "# Halo 3: 1 = what those searches found is saved for each build of the game's DLL, and the next\n");
    fprintf(f, "# launch checks each saved place before using it (HaloMCCVR-install-searches.bin, safe to delete).\n");
    fprintf(f, "# 0 = nothing is saved or read. (default %d)\n", d.install_search_cache);
    fprintf(f, "install_search_cache = %d\n", g_config.install_search_cache);
    fprintf(f, "# Halo 3: 1 = the hooks of the optional features go live together with the core's, in one pass\n");
    fprintf(f, "# that pauses the game's threads once. 0 = each feature pauses them again for its own. (default %d)\n",
            d.install_hooks_together);
    fprintf(f, "install_hooks_together = %d\n", g_config.install_hooks_together);
    fprintf(f, "# --- Per-title weapon/hand/HUD/crosshair profiles (C-TITLE-1) ---\n");
    fprintf(f, "# Every supported game - and Halo 2 Anniversary vs Classic\n");
    fprintf(f, "# separately - keeps its OWN copy of the tunables below.\n");
    fprintf(f, "# The F1 sliders edit the profile of whichever game\n");
    fprintf(f, "# is running; the shared keys above are only the defaults a\n");
    fprintf(f, "# fresh profile starts from. Edit these by hand freely.\n");
    fprintf(f, "# With no game running, the crosshair's size, opacity and\n");
    fprintf(f, "# brightness sliders and the HUD layout's four edit Halo: Reach's.\n\n");
    for (int profile = 0; profile < kTitleProfileCount; ++profile)
    {
        fprintf(f, "# %s\n", kTitleProfileTitles[profile]);
        for (int field = 0; field < kTunableFieldCount; ++field)
        {
            fprintf(f, "%s%s = %.4f\n",
                    kTitleProfilePrefixes[profile],
                    kTunableFields[field].suffix,
                    static_cast<double>(
                        g_config.title_profiles[profile].*
                        kTunableFields[field].member));
        }
        fprintf(f, "\n");
    }
    fprintf(f,"# Optional per-gun alignment; unknown models retain title settings.\n");
    fprintf(f,"per_gun_alignment = %d\n",g_config.per_gun_alignment?1:0);
    for(const auto& profile:g_weaponProfiles)
    {
        const auto* model=AlignmentModel(profile.title,profile.identity);
        if(!model) continue;
        fprintf(f,"# %s: %s\n",Config_TitleProfileName(profile.title),model->name);
        for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
            if(profile.set[i]) fprintf(f,"weapon_alignment_%d_%016llX_%s = %.6f\n",
                profile.title,static_cast<unsigned long long>(profile.identity),
                kWeaponAlignmentFields[i].suffix,static_cast<double>(profile.values[i]));
    }
    fclose(f);
}
