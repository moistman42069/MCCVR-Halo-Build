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
#include "log.h"

Config g_config;

// Shipped per-seat placements become built-in defaults, so a config file that
// predates a key is still placed rather than bare. Halo 3/ODST rows are
// headset-tuned; Reach rows are the user's Blender-authored, headset-unverified
// lineup. An entry the slot table cannot key is skipped rather than written
// somewhere else, which keeps a future enum change from silently landing a
// scorpion's trim on a mongoose.
Config::Config()
{
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

struct VirtualStockField {const char* key;float Config::*value;float minimum,maximum;};
static constexpr VirtualStockField kVirtualStockFields[]{
    {"virtual_stock_strength",&Config::virtual_stock_strength,0.f,1.f},
    {"virtual_stock_rear_height_m",&Config::virtual_stock_rear_height_m,-.30f,.10f},
    {"virtual_stock_shoulder_back_m",&Config::virtual_stock_shoulder_back_m,0.f,.25f},
    {"virtual_stock_shoulder_side_m",&Config::virtual_stock_shoulder_side_m,0.f,.20f},
    {"virtual_stock_chest_height_m",&Config::virtual_stock_chest_height_m,-.5f,-.22f},
    {"virtual_stock_chest_back_m",&Config::virtual_stock_chest_back_m,0.f,.25f},
    {"virtual_stock_chest_side_m",&Config::virtual_stock_chest_side_m,0.f,.20f},
    {"virtual_stock_adaptive_top_height_m",&Config::virtual_stock_adaptive_top_height_m,-.35f,0.f},
    {"virtual_stock_adaptive_bottom_height_m",&Config::virtual_stock_adaptive_bottom_height_m,-.65f,-.20f},
    {"virtual_stock_adaptive_top_half_width_m",&Config::virtual_stock_adaptive_top_half_width_m,.02f,.25f},
    {"virtual_stock_adaptive_bottom_half_width_m",&Config::virtual_stock_adaptive_bottom_half_width_m,.02f,.30f},
    {"virtual_stock_proximity_full_m",&Config::virtual_stock_proximity_full_m,.10f,.60f},
    {"virtual_stock_proximity_release_m",&Config::virtual_stock_proximity_release_m,.15f,.80f},
};

static void Clamp()
{
    const Config stockDefaults{};
    for(const auto& field:kVirtualStockFields)
    {
        float& value=g_config.*field.value;
        value=std::isfinite(value)?std::clamp(value,field.minimum,field.maximum):stockDefaults.*field.value;
    }
    g_config.virtual_stock_rear_reference=std::clamp(g_config.virtual_stock_rear_reference,0,3);
    if(g_config.virtual_stock_proximity_release_m<=g_config.virtual_stock_proximity_full_m)
    {g_config.virtual_stock_proximity_full_m=.270f;g_config.virtual_stock_proximity_release_m=.425f;}
    if(g_config.virtual_stock_adaptive_top_height_m<=g_config.virtual_stock_adaptive_bottom_height_m)
    {g_config.virtual_stock_adaptive_top_height_m=-.180f;g_config.virtual_stock_adaptive_bottom_height_m=-.450f;}
    g_config.config_version = 5;
    g_config.haptic_intensity = std::clamp(g_config.haptic_intensity, 0.0f, 1.0f);
    g_config.dpad_head_radius = std::clamp(g_config.dpad_head_radius, 0.10f, 0.50f);
    g_config.headset_smoothing = std::clamp(g_config.headset_smoothing, 0.0f, 0.10f);
    g_config.aim_stabilization = std::clamp(g_config.aim_stabilization, 0.0f, 0.95f);
    g_config.screen_width_m = std::clamp(g_config.screen_width_m, 0.5f, 20.0f);
    g_config.screen_distance_m = std::clamp(g_config.screen_distance_m, 0.3f, 20.0f);
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
    // 0 stays 0 (hold one image); every other value is clamped into the range
    // the publish floor can actually honour.
    if (g_config.crosshair_animation_frames != 0)
        g_config.crosshair_animation_frames =
            std::clamp(g_config.crosshair_animation_frames, 6, 60);
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

    g_config.game_brightness = std::clamp(g_config.game_brightness, 0.5f, 2.0f);
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
    g_config.two_hand_zone_right_m = std::clamp(g_config.two_hand_zone_right_m, -0.10f, 0.10f);
    g_config.left_grip_forward_m = std::clamp(g_config.left_grip_forward_m, -0.05f, 0.25f);
    g_config.physical_melee_swing_speed = std::clamp(
        g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax);
    g_config.right_shoulder_drop = std::clamp(g_config.right_shoulder_drop, 0.0f, 0.3f);
    g_config.shoulder_back_m = std::clamp(g_config.shoulder_back_m, -0.3f, 0.3f);
    g_config.gun_pitch_deg = std::clamp(g_config.gun_pitch_deg, -180.0f, 180.0f);
    g_config.gun_yaw_deg = std::clamp(g_config.gun_yaw_deg, -180.0f, 180.0f);
    g_config.gun_roll_deg = std::clamp(g_config.gun_roll_deg, -180.0f, 180.0f);
    g_config.gun_forward_m = std::clamp(g_config.gun_forward_m, -0.3f, 0.5f);
    g_config.gun_right_m = std::clamp(g_config.gun_right_m, -0.3f, 0.3f);
    g_config.gun_up_m = std::clamp(g_config.gun_up_m, -0.3f, 0.3f);
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
    g_config.scope_screen_width_m = std::clamp(g_config.scope_screen_width_m, 0.04f, 0.25f);
    g_config.scope_screen_right_m = std::clamp(g_config.scope_screen_right_m, -0.30f, 0.30f);
    g_config.scope_screen_up_m = std::clamp(g_config.scope_screen_up_m, -0.20f, 0.30f);
    g_config.scope_screen_forward_m = std::clamp(g_config.scope_screen_forward_m, 0.05f, 0.80f);
    g_config.scope_refresh_divisor = std::clamp(g_config.scope_refresh_divisor, 1, 4);
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
};

const TunableField kTunableFields[] = {
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
    {"gun_forward_m", &TitleTunables::gun_forward_m, &Config::gun_forward_m,
     -0.30f, 0.50f},
    {"gun_right_m", &TitleTunables::gun_right_m, &Config::gun_right_m,
     -0.30f, 0.30f},
    {"gun_up_m", &TitleTunables::gun_up_m, &Config::gun_up_m, -0.30f, 0.30f},
    {"left_hand_forward_m", &TitleTunables::left_hand_forward_m,
     &Config::left_hand_forward_m, -0.15f, 0.30f},
    {"hud_size", &TitleTunables::hud_size, &Config::hud_size, 0.30f, 1.00f},
    {"hud_aspect", &TitleTunables::hud_aspect, &Config::hud_aspect,
     kHudAspectMin, kHudAspectMax},
    {"hud_curvature", &TitleTunables::hud_curvature, &Config::hud_curvature,
     kHudCurvatureMin, kHudCurvatureMax},
    {"hud_vertical_offset", &TitleTunables::hud_vertical_offset,
     &Config::hud_vertical_offset, kHudHeightMin, kHudHeightMax},
    {"barrel_pitch_deg", &TitleTunables::barrel_pitch_deg,
     &Config::barrel_pitch_deg, -180.0f, 180.0f},
    {"barrel_yaw_deg", &TitleTunables::barrel_yaw_deg,
     &Config::barrel_yaw_deg, -180.0f, 180.0f},
    {"barrel_roll_deg", &TitleTunables::barrel_roll_deg,
     &Config::barrel_roll_deg, -180.0f, 180.0f},
};
constexpr int kTunableFieldCount =
    static_cast<int>(sizeof(kTunableFields) / sizeof(kTunableFields[0]));

const char* const kTitleProfilePrefixes[kTitleProfileCount] = {
    "halo3_", "odst_", "reach_", "halo4_", "halo2a_", "halo2c_", "halo1_"};
const char* const kTitleProfileTitles[kTitleProfileCount] = {
    "Halo 3", "Halo 3: ODST", "Halo: Reach", "Halo 4",
    "Halo 2 Anniversary", "Halo 2 Classic",
    "Halo: Combat Evolved (future bring-up)"};

bool g_profileFieldSet[kTitleProfileCount][kTunableFieldCount]{};
std::atomic<int> g_activeTitleProfile{-1};
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
            if (!g_profileFieldSet[profile][field])
                value = g_config.base_tunables.*descriptor.member;
            value = std::clamp(value, descriptor.minimum, descriptor.maximum);
        }
    }
    const int active = g_activeTitleProfile.load(std::memory_order_acquire);
    if (active >= 0 && active < kTitleProfileCount)
    {
        for (int field = 0; field < kTunableFieldCount; ++field)
            g_config.*kTunableFields[field].live =
                g_config.title_profiles[active].*kTunableFields[field].member;
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
    const TitleTunables& source = profile >= 0
        ? g_config.title_profiles[profile]
        : g_config.base_tunables;
    for (int field = 0; field < kTunableFieldCount; ++field)
        g_config.*kTunableFields[field].live =
            source.*kTunableFields[field].member;
    LOG("config: weapon/hand/HUD tunables now follow %s",
        Config_TitleProfileName(profile));
}

void Config_StoreLiveTunables()
{
    const std::lock_guard lock(g_profileMutex);
    StoreWeaponAlignment();
    const int active = g_activeTitleProfile.load(std::memory_order_acquire);
    TitleTunables& destination =
        active >= 0 && active < kTitleProfileCount
            ? g_config.title_profiles[active]
            : g_config.base_tunables;
    for (int field = 0; field < kTunableFieldCount; ++field)
    {
        const TunableField& descriptor = kTunableFields[field];
        if(g_activeWeaponProfile>=0&&WeaponAlignmentFieldIndex(descriptor.live)>=0) continue;
        destination.*descriptor.member = std::clamp(
            g_config.*descriptor.live, descriptor.minimum, descriptor.maximum);
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
        if (ParseWeaponAlignmentKey(key,val)||ParseTitleProfileKey(key, val))
            continue;
        bool vrBindingKey=false;
        for(unsigned title=0;title<weapon_interaction::kTitleCount&&!vrBindingKey;++title)
            for(unsigned action=0;action<vr_mapping::Count;++action)
            {
                char bindingKey[96]{};
                sprintf_s(bindingKey,"vr_bind_%s_%s",weapon_interaction::kTitleKeys[title],vr_mapping::kKeys[action]);
                if(strcmp(key,bindingKey)) continue;
                char* end=nullptr;
                const long value=strtol(val,&end,10);
                if(end!=val&&end&&*end==0&&value>=0&&value<vr_mapping::SourceCount)
                    g_config.vr_bindings[title][action]=static_cast<int>(value);
                else LOG("config: invalid VR binding '%s' ignored",key);
                vrBindingKey=true;break;
            }
        if(vrBindingKey) continue;
        if(!strcmp(key,"physical_crouch")) {g_config.physical_crouch=atoi(val)!=0;continue;}
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
        if(!strcmp(key,"vr_action_mapping")) {g_config.vr_action_mapping=atoi(val)!=0;continue;}
        if(!strcmp(key,"flashlight_suppress_on_two_hand")) {g_config.flashlight_suppress_on_two_hand=atoi(val)!=0;continue;}
        if(!strcmp(key,"upscaler")) {g_config.upscaler=atoi(val)==1?1:0;continue;}
        if(!strcmp(key,"dlss_mode")) {g_config.dlss_mode=std::clamp(atoi(val),0,4);continue;}
        if(!strcmp(key,"dlss_jitter")) {g_config.dlss_jitter=atoi(val)!=0;continue;}
        if(!strcmp(key,"dlss_preset")) {g_config.dlss_preset=std::clamp(atoi(val),0,5);continue;}
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
        if(!strcmp(key,"virtual_stock_proximity_release")) {g_config.virtual_stock_proximity_release=atoi(val)!=0;continue;}
        if(!strcmp(key,"virtual_stock_rear_reference")) {g_config.virtual_stock_rear_reference=std::clamp(atoi(val),0,3);continue;}
        bool stockKey=false;
        for(const auto& field:kVirtualStockFields) if(!strcmp(key,field.key))
        {ParseFloatSetting(key,val,g_config.*field.value);stockKey=true;break;}
        if(stockKey) continue;
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
        { g_config.weapon_holster_location=atoi(val)==1?1:0;continue; }
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
        if (!strcmp(key, "independent_dual_aim")) { g_config.independent_dual_aim=atoi(val)!=0; continue; }
        if (!strcmp(key, "gun_barrel_aim")) { g_config.gun_barrel_aim=atoi(val)!=0; continue; }
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
        // C-H2-85: handled by table, not by the else-if chain below - that
        // chain is at MSVC's block nesting limit and cannot take more arms.
        {
            struct FloatKey { const char* name; float* destination; };
            const FloatKey kFloatKeys[] = {
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
                if (parsed > 5)
                    LOG("config: version %ld is newer than supported version 5; known keys will be loaded", parsed);
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
            g_config.game_brightness = (float)atof(val);
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
    if (loadedLegacyCurvature)
    {
        // Version 1 stored a signed value whose physical delta was value*0.1.
        // Preserve that exact curve when migrating to normalized 0(flat)..1(curved).
        const float legacyDelta = g_config.hud_curvature * 0.1f;
        g_config.hud_curvature = (0.30f - legacyDelta) / 0.60f;
    }
    Clamp();
    ResolveTitleProfiles();
    LOG("config: loaded (screen %.2fm wide at %.2fm)", g_config.screen_width_m, g_config.screen_distance_m);
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

void ConfigSave()
{
    const std::lock_guard lock(g_profileMutex);
    if (g_path.empty())
        return;
    Config_ApplyWeaponProfile(g_observedWeaponIdentity);
    Clamp();
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
    fprintf(f, "# Optional; requires MCC desktop focus. Does not affect gameplay or F1.\n");
    fprintf(f, "game_menu_pointer = %d\n\n", g_config.game_menu_pointer ? 1 : 0);
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
    fprintf(f, "# Default off. Controller aiming is preserved; walking stays head-relative.\n");
    fprintf(f, "roomscale_movement = %d\n\n", g_config.roomscale_movement ? 1 : 0);
    fprintf(f, "# Degrees per snap turn.\n");
    fprintf(f, "# (default %.0f, range 5 to 90)\n", d.turn_snap_deg);
    fprintf(f, "turn_snap_deg = %.0f\n\n", g_config.turn_snap_deg);
    fprintf(f, "# Smooth turn speed in degrees per second.\n");
    fprintf(f, "# (default %.0f, range 30 to 360)\n", d.turn_smooth_deg_s);
    fprintf(f, "turn_smooth_deg_s = %.0f\n\n", g_config.turn_smooth_deg_s);
    fprintf(f, "# Press Y+B together to send Start (pause/resume) in supported titles.\n");
    fprintf(f, "# (default %d)\n", d.y_b_start_chord ? 1 : 0);
    fprintf(f, "y_b_start_chord = %d\n\n", g_config.y_b_start_chord ? 1 : 0);
    fprintf(f, "# Halo 2 only: the gamepad's Back/View button is MCC's instant\n");
    fprintf(f, "# Classic <-> Anniversary graphics switch. 0 = the mod swallows that\n");
    fprintf(f, "# button in Halo 2 so a physical pad (Steam Controller) cannot flip the\n");
    fprintf(f, "# renderer mid-game; 1 = pass it through. Holding the left stick\n");
    fprintf(f, "# click with the controller at your head switches renderers on\n");
    fprintf(f, "# purpose in either setting. Y+B still pauses on the pad.\n");
    fprintf(f, "# (default %d)\n", d.halo2_gamepad_graphics_switch ? 1 : 0);
    fprintf(f, "halo2_gamepad_graphics_switch = %d\n\n",
            g_config.halo2_gamepad_graphics_switch ? 1 : 0);
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
    fprintf(f, "# shields, radar, ammo, objectives, or the aiming reticle. (default %d)\n",
            d.halo4_helmet ? 1 : 0);
    fprintf(f, "halo4_helmet = %d\n\n", g_config.halo4_helmet ? 1 : 0);
    fprintf(f, "# Hide gameplay HUD and VR reticle; menus remain available.\nhide_hud = %d\n\n", g_config.hide_hud ? 1 : 0);
    fprintf(f, "# Optional CE Anniversary lens-flare suppression. Does not alter world lighting.\nce_anniversary_disable_lens_flares = %d\n\n", g_config.ce_anniversary_disable_lens_flares ? 1 : 0);
    fprintf(f, "# Each owned dual-wielded gun follows its own controller.\nindependent_dual_aim = %d\n\n", g_config.independent_dual_aim ? 1 : 0);
    fprintf(f, "# Use verified visible weapon muzzle origin and orientation.\ngun_barrel_aim = %d\n\n", g_config.gun_barrel_aim ? 1 : 0);
    fprintf(f, "# Floating VR-crosshair smoothing only; bullets stay raw.\n");
    fprintf(f, "# (default %.2f, range 0 to 0.95)\n", d.aim_stabilization);
    fprintf(f, "aim_stabilization = %.2f\n\n", g_config.aim_stabilization);
    fprintf(f, "# Aim crosshair in stereo: a floating reticle where the weapon actually\n");
    fprintf(f, "# shoots (the game's own reticle follows your head, not your hand).\n");
    fprintf(f, "# (default %d)\n", d.crosshair ? 1 : 0);
    fprintf(f, "crosshair = %d\n\n", g_config.crosshair ? 1 : 0);
    fprintf(f, "# How far away that crosshair floats, in meters.\n");
    fprintf(f, "# (default %.1f, range 2 to 50)\n", d.crosshair_distance_m);
    fprintf(f, "crosshair_distance_m = %.1f\n\n", g_config.crosshair_distance_m);
    fprintf(f, "# Apparent size of the crosshair, in degrees.\n");
    fprintf(f, "# (default %.2f, range 0.3 to 20)\n", d.crosshair_size_deg);
    fprintf(f, "crosshair_size_deg = %.2f\n\n", g_config.crosshair_size_deg);
    fprintf(f, "# How often the VR crosshair re-reads the game's own crosshair art,\n");
    fprintf(f, "# in frames. This makes Halo 3 and Halo 4 authored crosshairs animate:\n");
    fprintf(f, "# when you shoot, and the red/green target colors. Lower is more\n");
    fprintf(f, "# responsive, higher is cheaper. 0 holds animation.\n");
    fprintf(f, "# Halo 3 and Halo 4. (default %d, 0 or 6 to 60)\n",
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
    fprintf(f, "# -------------------------------------------------------------------\n");
    fprintf(f, "#  WEAPON CALIBRATION\n");
    fprintf(f, "#  Personal trims applied over the active title's verified base pose.\n");
    fprintf(f, "# -------------------------------------------------------------------\n\n");
    fprintf(f, "# Size of the right hand and the weapon it holds. Home/End adjust it\n");
    fprintf(f, "# in-game. 1.00 = the size the active game authored the model at.\n");
    fprintf(f, "# (default %.2f, range 0.3 to 3)\n", d.gun_scale);
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
    fprintf(f, "# Motion steering. In a Warthog, Mongoose, Ghost, Prowler or\n");
    fprintf(f, "# Chopper, DOUBLE-CLICK both grips to take hold of an invisible\n");
    fprintf(f, "# steering wheel: hold your hands as if on a wheel and tilt it,\n");
    fprintf(f, "# nothing needs squeezing. Double-click again to let go and the\n");
    fprintf(f, "# right stick steers. A single right grip still gets you out of\n");
    fprintf(f, "# the vehicle either way. Aircraft and the Scorpion and Wraith\n");
    fprintf(f, "# are unaffected.\n");
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
    fprintf(f, "# One value for Halo 3, ODST and Reach, so a change is felt in all three.\n");
    fprintf(f, "# (default %.2f, range 0.5 to 2)\n", d.game_brightness);
    fprintf(f, "game_brightness = %.2f\n\n", g_config.game_brightness);
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
    fprintf(f, "# Optional pouch-to-gun reload and shoulder/hip reserve-weapon draw.\n");
    fprintf(f, "# Off by default; native ammo, reload animation and inventory rules remain.\n");
    fprintf(f, "manual_reload = %d\nweapon_holsters = %d\n",g_config.manual_reload?1:0,g_config.weapon_holsters?1:0);
    fprintf(f, "manual_reload_disable_auto = %d\nmanual_reload_skip_animations = %d\n",
        g_config.manual_reload_disable_auto?1:0,g_config.manual_reload_skip_animations?1:0);
    fprintf(f, "# Shortened reload retains the native chambering tail; full animation skip takes precedence.\n");
    fprintf(f, "manual_reload_shortened_animation = %d\n",g_config.manual_reload_shortened_animation?1:0);
    fprintf(f, "weapon_pouch_down_m = %.3f\nweapon_body_zone_radius_m = %.3f\n",
        g_config.weapon_pouch_down_m,g_config.weapon_body_zone_radius_m);
    fprintf(f, "# Holster location: 0 = weapon-side shoulder, 1 = weapon-side hip.\n");
    fprintf(f, "weapon_holster_location = %d\n",g_config.weapon_holster_location);
    fprintf(f,"weapon_pouch_location = %d\nweapon_pouch_offset_x_m = %.3f\nweapon_pouch_offset_y_m = %.3f\nweapon_pouch_offset_z_m = %.3f\n",
        g_config.weapon_pouch_location,g_config.weapon_pouch_offset_x_m,g_config.weapon_pouch_offset_y_m,g_config.weapon_pouch_offset_z_m);
    fprintf(f, "# Independent grab/insert radii. Click uses a fresh weapon grip in the holster.\n");
    fprintf(f, "# Slide preserves the draw gesture. With both on, click completes first.\n");
    fprintf(f, "weapon_holster_radius_m = %.3f\nweapon_insert_radius_m = %.3f\nweapon_holster_draw_m = %.3f\n",
        g_config.weapon_holster_radius_m,g_config.weapon_insert_radius_m,g_config.weapon_holster_draw_m);
    fprintf(f, "weapon_holster_slide = %d\nweapon_holster_click = %d\n",
        g_config.weapon_holster_slide?1:0,g_config.weapon_holster_click?1:0);
    fprintf(f, "# Recognized Needlers across all titles and Reach Needle Rifle; requires manual_reload.\n");
    fprintf(f, "# One rapid gun-hand out-and-back shake in any direction; no grip; settle to rearm.\n");
    fprintf(f, "weapon_needler_shake = %d\nweapon_shake_travel_m = %.3f\n",
        g_config.weapon_needler_shake?1:0,g_config.weapon_shake_travel_m);
    fprintf(f, "# Legacy transport fallback for titles without verified native action lookup.\n");
    fprintf(f, "# Show a generic blue reload item for detected unfamiliar/modded held models.\n");
    fprintf(f, "weapon_unknown_reload_visual = %d\n",g_config.weapon_unknown_reload_visual?1:0);
    fprintf(f, "# 0=X, 1=RB, 2=LB, 3=B, 4=Y, 5=A, 6=LT, 7=RT. CE/H2 share both graphics modes.\n");
    for(unsigned i=0;i<weapon_interaction::kTitleCount;++i)
    {
        fprintf(f,"weapon_reload_button_%s = %d\n",weapon_interaction::kTitleKeys[i],g_config.weapon_reload_button[i]);
        fprintf(f,"weapon_switch_button_%s = %d\n",weapon_interaction::kTitleKeys[i],g_config.weapon_switch_button[i]);
    }
    fprintf(f,"\n# VR action bindings: 0=automatic, 1=unbound. Edited in VR Mappings.\n");
    fprintf(f,"physical_crouch = %d\nphysical_crouch_depth_m = %.3f\n",
        g_config.physical_crouch?1:0,g_config.physical_crouch_depth_m);
    fprintf(f,"vr_action_mapping = %d\nflashlight_suppress_on_two_hand = %d\n",
        g_config.vr_action_mapping?1:0,g_config.flashlight_suppress_on_two_hand?1:0);
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        for(unsigned action=0;action<vr_mapping::Count;++action)
            fprintf(f,"vr_bind_%s_%s = %d\n",weapon_interaction::kTitleKeys[title],
                vr_mapping::kKeys[action],g_config.vr_bindings[title][action]);
    fprintf(f,"\n# Native localized subtitles: independent gameplay and theatre placement.\n");
    fprintf(f,"# Per-game muzzle-flash hiding. Supported paths: H2 Original and H4.\n");
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        fprintf(f,"hide_muzzle_flash_%s = %d\n",weapon_interaction::kTitleKeys[title],g_config.hide_muzzle_flash[title]?1:0);
    fprintf(f,"# Stock bloom per game. Optional suppression supports H3, ODST and Reach.\n");
    for(unsigned title=0;title<weapon_interaction::kTitleCount;++title)
        fprintf(f,"bloom_enabled_%s = %d\n",weapon_interaction::kTitleKeys[title],g_config.bloom_enabled[title]?1:0);
    fprintf(f,"vr_gameplay_subtitles = %d\nvr_gameplay_subtitle_scale = %.3f\nvr_gameplay_subtitle_x = %.3f\nvr_gameplay_subtitle_y = %.3f\nvr_gameplay_subtitle_anchor = %d\n",
        g_config.vr_gameplay_subtitles?1:0,g_config.vr_gameplay_subtitle_scale,g_config.vr_gameplay_subtitle_x,g_config.vr_gameplay_subtitle_y,g_config.vr_gameplay_subtitle_anchor);
    fprintf(f,"vr_theatre_subtitle_scale = %.3f\nvr_theatre_subtitle_x = %.3f\nvr_theatre_subtitle_y = %.3f\nvr_theatre_subtitle_anchor = %d\n",
        g_config.vr_theatre_subtitle_scale,g_config.vr_theatre_subtitle_x,g_config.vr_theatre_subtitle_y,g_config.vr_theatre_subtitle_anchor);
    fprintf(f,"\n# Optional DLSS (0=off, 1=on); modes: DLAA/Quality/Balanced/Performance/Ultra Performance.\n");
    fprintf(f,"upscaler = %d\ndlss_mode = %d\ndlss_jitter = %d\ndlss_preset = %d\ndlss_debug_view = %d\n",
        g_config.upscaler,g_config.dlss_mode,g_config.dlss_jitter?1:0,g_config.dlss_preset,g_config.dlss_debug_view?1:0);
    fprintf(f,"\n");
    fprintf(f, "# Two-handed aiming: put your left hand on the gun front and use the\n");
    fprintf(f, "# left grip to steady aim along the two-hand line. 1 = on.\n");
    fprintf(f, "# (default %d)\n", d.two_handed_aim ? 1 : 0);
    fprintf(f, "two_handed_aim = %d\n\n", g_config.two_handed_aim ? 1 : 0);
    fprintf(f,"# Optional virtual stock: rear reference 0=head,1=shoulder,2=chest,3=adaptive.\n");
    fprintf(f,"virtual_stock = %d\nvirtual_stock_rear_reference = %d\nvirtual_stock_proximity_release = %d\n",
        g_config.virtual_stock?1:0,g_config.virtual_stock_rear_reference,g_config.virtual_stock_proximity_release?1:0);
    for(const auto& field:kVirtualStockFields) fprintf(f,"%s = %.4f\n",field.key,g_config.*field.value);
    fprintf(f, "# Main weapon, aim and trigger on the physical left controller.\n");
    fprintf(f, "# Movement, turning and face buttons keep their physical bindings.\n");
    fprintf(f, "left_handed = %d\n\n", g_config.left_handed ? 1 : 0);
    fprintf(f, "# Experimental anatomical hand alignment; only active with left_handed.\n");
    fprintf(f, "# Default off preserves the released hand positioning.\n");
    fprintf(f, "experimental_hand_alignment = %d\n\n", g_config.experimental_hand_alignment ? 1 : 0);
    fprintf(f, "# Engage style: 1 = toggle (click grip on/off), 0 = hold.\n");
    fprintf(f, "# (default %d)\n", d.two_hand_toggle ? 1 : 0);
    fprintf(f, "two_hand_toggle = %d\n\n", g_config.two_hand_toggle ? 1 : 0);
    fprintf(f, "# Left controller wrist-to-palm correction, shared by support-hand IK\n");
    fprintf(f, "# and the two-hand aim point, in meters.\n");
    fprintf(f, "# (default %.3f, range -0.15 to 0.30)\n", d.left_hand_forward_m);
    fprintf(f, "left_hand_forward_m = %.3f\n\n", g_config.base_tunables.left_hand_forward_m);
    fprintf(f, "# Sideways nudge of the two-hand grab zone (+ = player's right), so the\n");
    fprintf(f, "# grab line sits on the visible barrel, in meters.\n");
    fprintf(f, "# (default %.3f, range -0.10 to 0.10)\n", d.two_hand_zone_right_m);
    fprintf(f, "two_hand_zone_right_m = %.3f\n\n", g_config.two_hand_zone_right_m);
    fprintf(f, "# Rendered left hand wrist-to-palm distance, in meters. Seats the\n");
    fprintf(f, "# dual-wield gun in the palm and the grab line through it.\n");
    fprintf(f, "# (default %.3f, range -0.05 to 0.25)\n", d.left_grip_forward_m);
    fprintf(f, "left_grip_forward_m = %.3f\n\n", g_config.left_grip_forward_m);
    fprintf(f, "# VRIK arm IK: 1 = bend the arm to your controller (shoulder planted,\n");
    fprintf(f, "# elbow solved); 0 = rigid-parent the whole arm assembly. Required on\n");
    fprintf(f, "# Halo 3 / ODST, so leave it 1.\n");
    fprintf(f, "# (default %d)\n", d.arm_ik ? 1 : 0);
    fprintf(f, "arm_ik = %d\n\n", g_config.arm_ik ? 1 : 0);
    fprintf(f, "# Floating hands: 1 = show only the hands and the guns they hold\n");
    fprintf(f, "# (arms hidden); 0 = full arms. Pure render filter over VRIK.\n");
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
    fprintf(f, "# --- Per-title weapon/hand/HUD profiles (C-TITLE-1) ---\n");
    fprintf(f, "# Every supported game - and Halo 2 Anniversary vs Classic\n");
    fprintf(f, "# separately - keeps its OWN copy of the thirteen tunables\n");
    fprintf(f, "# below. The F1 sliders edit the profile of whichever game\n");
    fprintf(f, "# is running; the shared keys above are only the defaults a\n");
    fprintf(f, "# fresh profile starts from. Edit these by hand freely.\n\n");
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
