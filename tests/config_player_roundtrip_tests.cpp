// A PLAYER build reads every key a player build writes.
//
// The settings file is written from the whole of the settings, and read by a chain of key tests of which a part
// is compiled into development builds only. A key that is written always and read inside that part is saved,
// then called unknown at the next start: the setting works for the session it is changed in and is forgotten.
// This target is compiled as a player build (no HALOMCCVR_DEV_DIAGNOSTICS), so that part is not in it.
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <cmath>
#include <string>
#include <vector>
#include "../src/common/config.h"
#include "../src/common/weapon_alignment.h"

namespace
{
std::vector<std::string> g_unknownKeys;
int failures = 0;
void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
} // namespace

void Logf(const char* format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (std::strstr(line, "unknown key"))
        g_unknownKeys.emplace_back(line);
}

int main()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    std::printf("FAIL: this target must be compiled as a player build\n");
    return 1;
#else
    wchar_t folder[MAX_PATH]{}, path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, folder);
    swprintf_s(path, L"%shalomccvr-player-roundtrip-%lu.cfg", folder, GetCurrentProcessId());
    DeleteFileW(path);

    // The first load of a file that is not there leaves the built-in settings; the save writes all of them.
    g_config = Config{};
    ConfigLoad(path);
    for (int profile=0;profile<kTitleProfileCount;++profile) {
        const auto baseline=weapon_alignment::Baseline(profile);
        const auto& saved=g_config.title_profiles[profile];
        Check(saved.gun_forward_m==baseline.forward && saved.gun_right_m==baseline.right &&
            saved.gun_up_m==baseline.up,"new config uses per-title gun baselines");
        const auto left=weapon_alignment::kLeftOffsets[profile];
        Check(saved.left_gun_forward_m==left.forward && saved.left_gun_right_m==left.right &&
            saved.left_gun_up_m==left.up,"new config uses independent left baselines");
    }
    g_config.left_gun_forward_m=.11f;g_config.left_gun_right_m=-.07f;g_config.left_gun_up_m=.03f;
    g_config.gun_forward_m=-.22f;g_config.gun_right_m=.08f;g_config.gun_up_m=-.09f;
    g_config.mono_rendering = true;
    g_config.hook_cost_frames = 503;
    g_config.host_ui_panel = false;
    g_config.host_ui_panel_width_m = 3.3f;
    g_config.host_ui_panel_distance_m = 1.7f;
    g_config.aim_frame_zoom = false;
    g_config.aim_frame_stereo = false;
    g_config.aim_frame_3d_strength = 3.5f;
    g_config.aim_frame_size_deg[1] = 70.0f;
    g_config.aim_frame_strength[0] = 2.0f;
    g_config.aim_frame_move_s = 0.3f;
    g_config.reclaimer_share_device = false;
    g_config.desktop_mirror = false;
    g_config.two_hand_one_hand_range_m = 0.30f;
    ConfigSave();

    g_unknownKeys.clear();
    g_config = Config{};
    ConfigLoad(path);
    DeleteFileW(path);

    for (const std::string& line : g_unknownKeys)
        std::printf("FAIL: written by a player build and not read by one: %s\n", line.c_str());
    failures += static_cast<int>(g_unknownKeys.size());
    Check(g_config.mono_rendering, "mono_rendering is remembered");
    Check(g_config.hook_cost_frames == 503, "hook_cost_frames is remembered");
    Check(!g_config.host_ui_panel, "host_ui_panel is remembered");
    Check(g_config.host_ui_panel_width_m > 3.29f && g_config.host_ui_panel_width_m < 3.31f,
          "host_ui_panel_width_m is remembered");
    Check(g_config.host_ui_panel_distance_m > 1.69f && g_config.host_ui_panel_distance_m < 1.71f,
          "host_ui_panel_distance_m is remembered");
    Check(g_config.left_gun_forward_m==.11f && g_config.left_gun_right_m==-.07f && g_config.left_gun_up_m==.03f,
        "left offsets survive save/reload");
    Check(g_config.gun_forward_m==-.22f && g_config.gun_right_m==.08f && g_config.gun_up_m==-.09f,
        "right offsets remain independent after save/reload");
    Check(!g_config.aim_frame_zoom, "aim_frame_zoom is remembered");
    Check(!g_config.aim_frame_stereo, "aim_frame_stereo is remembered");
    Check(g_config.aim_frame_3d_strength > 3.49f && g_config.aim_frame_3d_strength < 3.51f,
          "aim_frame_3d_strength is remembered");
    Check(g_config.aim_frame_size_deg[1] > 69.9f && g_config.aim_frame_size_deg[1] < 70.1f,
          "aim_frame_size_deg_2 is remembered");
    Check(g_config.aim_frame_strength[0] > 1.99f && g_config.aim_frame_strength[0] < 2.01f,
          "aim_frame_strength_1 is remembered");
    Check(g_config.aim_frame_move_s > 0.29f && g_config.aim_frame_move_s < 0.31f,
          "aim_frame_move_s is remembered");
    Check(!g_config.reclaimer_share_device, "reclaimer_share_device is remembered");
    Check(!g_config.desktop_mirror, "desktop_mirror is remembered");
    Check(g_config.two_hand_one_hand_range_m > 0.29f && g_config.two_hand_one_hand_range_m < 0.31f,
          "two_hand_one_hand_range_m is remembered");

    // A pre-version-1 file keeps each saved adjustment relative to the old -0.063 m default,
    // including explicit title overrides; nothing is written on load; later edits survive.
    FILE* legacy=nullptr;
    _wfopen_s(&legacy,path,L"wt");
    Check(legacy!=nullptr,"legacy fixture opens");
    if (legacy) {
        std::fputs("left_hand_forward_m = 0.12\nhalo3_left_hand_forward_m = 0.2\n",legacy);
        std::fclose(legacy);
        ConfigLoad(path);
        const auto closeTo=[](float a,float b){return std::fabs(a-b)<.0001f;};
        Check(closeTo(g_config.left_hand_forward_m,.12f+.063f),"legacy shared offset keeps its adjustment");
        int titleOverride=0,seeded=0;
        for (const auto& profile:g_config.title_profiles) {
            if (closeTo(profile.left_hand_forward_m,.2f+.063f)) ++titleOverride;
            else if (closeTo(profile.left_hand_forward_m,.12f+.063f)) ++seeded;
        }
        Check(titleOverride==1&&seeded==int(std::size(g_config.title_profiles))-1,
            "the explicit Halo 3 override keeps its adjustment; other games follow the shared value");
        FILE* unchanged=nullptr;_wfopen_s(&unchanged,path,L"rt");char text[128]{};
        if (unchanged) {std::fread(text,1,sizeof(text)-1,unchanged);std::fclose(unchanged);}
        Check(!std::strcmp(text,"left_hand_forward_m = 0.12\nhalo3_left_hand_forward_m = 0.2\n"),
            "loading a legacy file does not rewrite it");
        g_config.left_hand_forward_m=0.075f;
        ConfigSave();
        ConfigLoad(path);
        Check(g_config.left_hand_forward_m==0.075f,"post-upgrade edit survives reload");
        DeleteFileW(path);
    }

    // A file from before the 0.080 m grab-zone default keeps its side adjustment relative to the old 0.030 m
    // default, even one outside the new range; nothing is written on load; a saved file is not shifted again.
    const auto closeTo=[](float a,float b){return std::fabs(a-b)<.0001f;};
    const auto loadZone=[&](const char* text) {
        FILE* zone=nullptr;
        _wfopen_s(&zone,path,L"wt");
        Check(zone!=nullptr,"grab zone fixture opens");
        if (zone) {std::fputs(text,zone);std::fclose(zone);}
        ConfigLoad(path);
    };
    loadZone("two_hand_zone_right_m = 0.030\n");
    Check(closeTo(g_config.two_hand_zone_right_m,.08f),"a file at the old grab zone default gets the new default");
    FILE* zoneFile=nullptr;_wfopen_s(&zoneFile,path,L"rt");char zoneText[64]{};
    if (zoneFile) {std::fread(zoneText,1,sizeof(zoneText)-1,zoneFile);std::fclose(zoneFile);}
    Check(!std::strcmp(zoneText,"two_hand_zone_right_m = 0.030\n"),"loading an old grab zone file does not rewrite it");
    loadZone("two_hand_zone_right_m = -0.050\n");
    Check(closeTo(g_config.two_hand_zone_right_m,0.f),"an old grab zone adjustment is kept relative to the old default");
    g_config.two_hand_zone_right_m=.1f;
    ConfigSave();ConfigLoad(path);
    Check(closeTo(g_config.two_hand_zone_right_m,.1f),"a saved grab zone is not shifted again");
    loadZone("two_hand_zone_baseline_version = 1\ntwo_hand_zone_right_m = 0.030\n");
    Check(closeTo(g_config.two_hand_zone_right_m,.03f),"a version 1 grab zone is read as written");
    DeleteFileW(path);

    // Visible hand offsets (shared and per title) survive load, a title switch and reload.
    FILE* retired=nullptr;
    _wfopen_s(&retired,path,L"wt");
    Check(retired!=nullptr,"visible hand fixture opens");
    if (retired) {
        std::fputs("left_hand_forward_reset_version = 1\nleft_hand_mesh_x_m = .1\n"
            "right_hand_mesh_z_m = -.1\nhalo4_left_hand_mesh_y_m = .15\n"
            "halo4_right_hand_mesh_x_m = -.12\nhalo4_left_gun_right_m = .07\n",retired);
        std::fclose(retired);
        for (int pass=0;pass<2;++pass) {
            ConfigLoad(path);
            Config_ApplyTitleProfile(3);
            Check(g_config.left_hand_mesh_x_m==.1f && g_config.left_hand_mesh_y_m==.15f &&
                g_config.left_hand_mesh_z_m==0 && g_config.right_hand_mesh_x_m==-.12f &&
                g_config.right_hand_mesh_y_m==0 && g_config.right_hand_mesh_z_m==-.1f,
                "visible hand offsets survive a title switch and reload");
            Check(g_config.left_gun_right_m==.07f,"independent gun offset sits beside the visible hand offsets");
            Config_ApplyTitleProfile(-1);
        }
        DeleteFileW(path);
    }

    // Legacy brightness seeds every title; gameplay edits never replace that seed.
    FILE* brightness=nullptr;
    _wfopen_s(&brightness,path,L"wt");
    Check(brightness!=nullptr,"brightness migration fixture opens");
    if(brightness) {
        std::fputs("game_brightness = 1.25\n",brightness);std::fclose(brightness);
        ConfigLoad(path);
        for(int profile=0;profile<kTitleProfileCount;++profile) {
            Config_ApplyTitleProfile(profile);
            Check(g_config.game_brightness==1.25f,"legacy brightness migrates without resetting appearance");
            g_config.game_brightness=.6f+float(profile)*.1f;
            Config_StoreLiveTunables();
        }
        ConfigSave();
        ConfigLoad(path);
        for(int profile=0;profile<kTitleProfileCount;++profile) {
            Config_ApplyTitleProfile(profile);
            Check(std::fabs(g_config.game_brightness-(.6f+float(profile)*.1f))<.0001f,
                "brightness remains independent after title switches and save/reload");
        }
        Config_ApplyTitleProfile(-1);
        Check(g_config.game_brightness==1.25f,"saving in a game preserves the shared brightness seed");
        DeleteFileW(path);
    }

    // Relative slider endpoints survive save/reload for every title and hand.
    for(float delta:{-.30f,.30f}) {
        for(int profile=0;profile<kTitleProfileCount;++profile) {
            Config_ApplyTitleProfile(profile);
            const auto right=weapon_alignment::kRightOffsets[profile];
            const auto left=weapon_alignment::kLeftOffsets[profile];
            g_config.gun_forward_m=right.forward+delta;
            g_config.gun_right_m=right.right+delta;
            g_config.gun_up_m=right.up+delta;
            g_config.left_gun_forward_m=left.forward+delta;
            g_config.left_gun_right_m=left.right+delta;
            g_config.left_gun_up_m=left.up+delta;
            Config_StoreLiveTunables();
        }
        g_config.two_hand_zone_right_m=delta<0?-.02f:.18f;
        ConfigSave();ConfigLoad(path);
        Check(std::fabs(g_config.two_hand_zone_right_m-(delta<0?-.02f:.18f))<.0001f,
            "grab side slider endpoints survive reload");
        for(int profile=0;profile<kTitleProfileCount;++profile) {
            Config_ApplyTitleProfile(profile);
            const auto right=weapon_alignment::kRightOffsets[profile];
            const auto left=weapon_alignment::kLeftOffsets[profile];
            Check(std::fabs(g_config.gun_forward_m-right.forward-delta)<.0001f&&
                std::fabs(g_config.gun_right_m-right.right-delta)<.0001f&&
                std::fabs(g_config.gun_up_m-right.up-delta)<.0001f&&
                std::fabs(g_config.left_gun_forward_m-left.forward-delta)<.0001f&&
                std::fabs(g_config.left_gun_right_m-left.right-delta)<.0001f&&
                std::fabs(g_config.left_gun_up_m-left.up-delta)<.0001f,
                "all six gun offsets preserve the full relative slider range");
        }
    }
    Config_ApplyTitleProfile(-1);DeleteFileW(path);

    if (failures)
        std::printf("%d check(s) failed\n", failures);
    else
        std::printf("config player round trip: all checks passed\n");
    return failures ? 1 : 0;
#endif
}
