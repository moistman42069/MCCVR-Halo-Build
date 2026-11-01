// DEV BUILD ONLY: the synthetic gamepad (src/common/dev_pad_script.h has the command language).
//
// Files in the mod folder, all written whole (the harness writes a temp file and renames it):
//   halomccvr_pad.txt         a script to run; the worker deletes it as it takes it
//   halomccvr_pad_abort.txt   stop the running script now and leave the pad neutral
//   halomccvr_pad_status.txt  written by the worker: "ready", "seq <n>", "aborted" or "error <why>"
// Scripts run one at a time, in the order they arrive. MCC reads the pad only while it is the foreground
// window, which is why the harness holds the foreground (through the polite gate) while a script runs.

#include "dev_pad.h"
#include "d3d11_hook.h"

#if HALOMCCVR_DEV_DIAGNOSTICS
#include "../common/dev_pad_script.h"
#include "../common/aim_lead_logic.h"
#include "../common/config.h"
#include "../common/roomscale_recenter_logic.h"
#include "../common/log.h"
#include "vr.h"
#include "game.h"
#include "native_vehicle_first_person.h"
#include "title_adapter.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
std::atomic<uint16_t> g_buttons{0};
std::atomic<int16_t> g_lx{0}, g_ly{0}, g_rx{0}, g_ry{0};
std::atomic<uint8_t> g_lt{0}, g_rt{0};
std::wstring g_dir;   // set once before the worker starts

void Neutral()
{
    g_buttons.store(0, std::memory_order_relaxed);
    g_lx.store(0, std::memory_order_relaxed);
    g_ly.store(0, std::memory_order_relaxed);
    g_rx.store(0, std::memory_order_relaxed);
    g_ry.store(0, std::memory_order_relaxed);
    g_lt.store(0, std::memory_order_relaxed);
    g_rt.store(0, std::memory_order_relaxed);
}

bool ReadScript(const std::wstring& path, std::string& out)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return false;
    char buffer[4096];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0 && out.size() < 65536)
        out.append(buffer, n);
    fclose(f);
    return true;
}

void WriteStatus(const std::string& text)
{
    const std::wstring tmp = g_dir + L"halomccvr_pad_status.tmp";
    const std::wstring dst = g_dir + L"halomccvr_pad_status.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f)
        return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Sleep in slices an abort file can cut short. True = aborted.
bool Pause(uint32_t ms)
{
    const std::wstring abortPath = g_dir + L"halomccvr_pad_abort.txt";
    const uint64_t end = GetTickCount64() + ms;
    for (;;)
    {
        if (GetFileAttributesW(abortPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileW(abortPath.c_str());
            return true;
        }
        const uint64_t now = GetTickCount64();
        if (now >= end)
            return false;
        Sleep(static_cast<DWORD>(std::min<uint64_t>(20, end - now)));
    }
}

// The live switches a script may flip, written exactly as F1 writes them.
bool SetLive(const std::string& name, int value)
{
    if (name == "converged_aim") g_config.converged_aim = value ? 1 : 0;
    else if (name == "aim_direct_drive") { g_config.aim_direct_drive = value ? 1 : 0; Config_ApplyAimModes(); }
    else if (name == "roomscale_follow_law") g_config.roomscale_follow_law = value ? 1 : 0;
    else if (name == "roomscale_movement") g_config.roomscale_movement = value != 0;
    // The view recenter: the three switches, and its tunables as whole numbers (ms, cm/s).
    else if (name == "roomscale_recenter_jump") g_config.roomscale_recenter_jump = value != 0;
    else if (name == "roomscale_recenter_move_start") g_config.roomscale_recenter_move_start = value != 0;
    else if (name == "roomscale_recenter_moving") g_config.roomscale_recenter_moving = value != 0;
    else if (name == "roomscale_recenter_jump_ms")
        g_config.roomscale_recenter_jump_s = roomscale_recenter::ClampSeconds(float(value) * 0.001f, 0.2f);
    else if (name == "roomscale_recenter_move_start_ms")
        g_config.roomscale_recenter_move_start_s = roomscale_recenter::ClampSeconds(float(value) * 0.001f, 0.2f);
    else if (name == "roomscale_recenter_moving_speed_cm_s")
        g_config.roomscale_recenter_moving_speed =
            roomscale_recenter::ClampSpeed(float(value) * 0.01f, Config{}.roomscale_recenter_moving_speed);
    else if (name == "physical_crouch") g_config.physical_crouch = value != 0;
    else if (name == "scope_converge") g_config.scope_converge = value ? 1 : 0;
    else if (name == "seat_direct_drive") g_config.seat_direct_drive = value ? 1 : 0;
    else if (name == "seat_aim_through_barrel") g_config.seat_aim_through_barrel = value != 0;
    else if (name == "seat_fire_from_eye") g_config.seat_fire_from_eye = value != 0;
    else if (name == "halo4_seat_converge") g_config.halo4_seat_converge = value ? 1 : 0;
    else if (name == "reach_vehicle_aim") g_config.reach_vehicle_aim = value < 0 ? 0 : (value > 2 ? 2 : value);
    else if (name == "shots_from_muzzle") g_config.shots_from_muzzle = value != 0;
    else if (name == "coop_safe_shots") g_config.coop_safe_shots = value < 0 ? 0 : (value > 2 ? 2 : value);
    // The aim lead as a whole number of hundredths of a tick (100 = one tick, 0 = off).
    else if (name == "aim_lead_ticks_x100")
        g_config.aim_lead_ticks = aim_lead::ClampTicks(float(value) * 0.01f, Config{}.aim_lead_ticks);
    else if (name == "gun_barrel_aim") g_config.gun_barrel_aim = value != 0;
    else if (name == "vehicle_cam_smoothing") g_config.vehicle_cam_smoothing = value != 0;
    // A gun-slaved seat's pitch check: 1 the pitch stick is held while it collects (what a player build does),
    // 0 the stick servo runs through it.
    else if (name == "seat_pitch_check_hold") Game_DevSetSeatPitchCheckHold(value != 0);
    // Halo 4's Broadsword view: 1 on the seat camera's point (what a player build does), 0 the observer.
    else if (name == "broadsword_ride") Game_DevSetBroadswordRide(value != 0);
    // The first-person ladder's fault bits (config.h): the ladders read the value at every evaluation, so
    // bits 0-2 take effect on the next camera frame and one launch can walk 0 -> 1 -> 3 -> 5 -> 0. Bit 3
    // needs a level the camera rung has not been proven in (the value agreement never withdraws a proven
    // rung), so it still wants the key set before the level loads.
    else if (name == "first_person_rung_fault") g_config.first_person_rung_fault = value & 0xF;
    else if (name == "dev_sim_trace") g_config.dev_sim_trace = value ? 1 : 0;
    // 0 inside the unit's update, 1 the control pass, anything else by the rule (-1).
    else if (name == "reach_sight_trace_site") g_config.reach_sight_trace_site = value == 0 ? 0 : (value == 1 ? 1 : -1);
    // 0 always, 1 only outside a tick, 2 never, 3 the player's own weapon effects only, anything else by the rule.
    else if (name == "reach_effect_move_rule")
        g_config.reach_effect_move_rule = value >= 0 && value <= 3 ? static_cast<int>(value) : -1;
    else if (name == "dev_sim_probe") g_config.dev_sim_probe = static_cast<int>(value & 0xFF);
    // Halo 4's reticle capture (vr.h, VrHalo4ReticleDrawScope): 0 as the game draws it, 1 alpha only from draws
    // that write coverage (what a player build does), 2 no draw writes alpha. Takes effect at the next capture.
    else if (name == "reticle_capture_alpha") VR_DevSetHalo4ReticleAlphaMode(value);
    // The seat camera's offsets for the vehicle model and seat the camera is in now, in centimetres plus 500
    // (a script's numbers are unsigned): 500 is zero, 650 is +1.5 m. What the Vehicles page's sliders write.
    else if (name == "seat_cam_forward" || name == "seat_cam_up" || name == "seat_cam_right")
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        uint64_t identity = 0;
        int seat = -1;
        if (!NativeVehicleFirstPerson_TrimTarget(title, identity, seat)) return false;
        const int slot = ConfigEnsureVehicleModelTrim(g_config, title, identity, seat);
        if (slot < 0) return false;
        const unsigned axis = name == "seat_cam_forward" ? 0u : (name == "seat_cam_up" ? 1u : 2u);
        g_config.vehicle_model_trims[slot].value[axis] = static_cast<float>(value - 500) / 100.0f;
        g_config.vehicle_model_trims[slot].set[axis] = true;
    }
    // The pixel shader census (dev_ps_census.inl): pause or resume it, and bind no pixel shader in place of
    // the numbered one (0 = none).
    else if (name == "dev_ps_census") g_config.dev_ps_census = value ? 1 : 0;
    // Halo 4's copies of its finished picture (halo4_scene_copy_logic.h): 1 taken from the eye being drawn
    // (what every build does), 0 left as the game makes them.
    else if (name == "halo_four_scene_copy") VR_DevSetHalo4SceneCopy(value != 0);
    else if (name == "dev_ps_blank") D3D_DevSetBlankPixelShader(value);
    else return false;
    return true;
}

bool Run(const std::vector<dev_pad::Step>& steps)
{
    using dev_pad::Op;
    for (const dev_pad::Step& s : steps)
    {
        bool aborted = false;
        switch (s.op)
        {
        case Op::Hold: g_buttons.fetch_or(s.buttons, std::memory_order_relaxed); break;
        case Op::Release: g_buttons.fetch_and(static_cast<uint16_t>(~s.buttons), std::memory_order_relaxed); break;
        case Op::ReleaseAll: g_buttons.store(0, std::memory_order_relaxed); break;
        case Op::Press:
            g_buttons.fetch_or(s.buttons, std::memory_order_relaxed);
            aborted = Pause(s.ms);
            g_buttons.fetch_and(static_cast<uint16_t>(~s.buttons), std::memory_order_relaxed);
            break;
        case Op::Stick:
            (s.side ? g_rx : g_lx).store(dev_pad::StickRaw(s.x), std::memory_order_relaxed);
            (s.side ? g_ry : g_ly).store(dev_pad::StickRaw(s.y), std::memory_order_relaxed);
            break;
        case Op::Trigger: (s.side ? g_rt : g_lt).store(dev_pad::TriggerRaw(s.x), std::memory_order_relaxed); break;
        case Op::Wait: aborted = Pause(s.ms); break;
        case Op::Clear: Neutral(); break;
        case Op::Seq: WriteStatus("seq " + std::to_string(s.seq) + "\n"); break;
        case Op::Set:
            if (SetLive(s.name, s.value)) LOG("DEVPAD: set %s = %d (live, as F1 does)", s.name.c_str(), s.value);
            else LOG("DEVPAD: set %s refused: not a live switch this tool may change", s.name.c_str());
            break;
        }
        if (aborted)
            return false;
    }
    return true;
}

DWORD WINAPI Worker(LPVOID)
{
    const std::wstring scriptPath = g_dir + L"halomccvr_pad.txt";
    for (;;)
    {
        Sleep(50);
        if (GetFileAttributesW(scriptPath.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        std::string text;
        const bool read = ReadScript(scriptPath, text);
        DeleteFileW(scriptPath.c_str());
        if (!read)
            continue;
        std::vector<dev_pad::Step> steps;
        std::string error;
        if (!dev_pad::Parse(text, steps, error))
        {
            LOG("DEVPAD: refused a script (%s); the pad is unchanged", error.c_str());
            WriteStatus("error " + error + "\n");
            continue;
        }
        LOG("DEVPAD: running a %zu-step script", steps.size());
        // An abort is for the script running when it was written: one left from an earlier script (it
        // finished before the abort landed) must not stop this one at its first pause.
        DeleteFileW((g_dir + L"halomccvr_pad_abort.txt").c_str());
        if (!Run(steps))
        {
            Neutral();
            LOG("DEVPAD: aborted; the pad is neutral");
            WriteStatus("aborted\n");
        }
    }
}
} // namespace

void DevPad_Start(const wchar_t* modDir)
{
    static std::atomic<bool> started{false};
    if (!modDir || started.exchange(true))
        return;
    g_dir = modDir;
    // Nothing left over from an earlier session replays.
    DeleteFileW((g_dir + L"halomccvr_pad.txt").c_str());
    DeleteFileW((g_dir + L"halomccvr_pad_abort.txt").c_str());
    WriteStatus("ready\n");
    HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!thread)
    {
        LOG("DEVPAD: the worker thread could not start (%lu); the synthetic pad is off", GetLastError());
        return;
    }
    CloseHandle(thread);
    LOG("DEV TEST HOOK ACTIVE: dev_synthetic_pad = 1 -- scripts dropped as halomccvr_pad.txt in the mod folder "
        "press this pad's buttons (tools/Send-MccPad.ps1). Headless tests only; set it to 0 to play.");
}

void DevPad_Apply(XINPUT_GAMEPAD& pad)
{
    pad.wButtons = static_cast<WORD>(pad.wButtons | g_buttons.load(std::memory_order_relaxed));
    const uint8_t lt = g_lt.load(std::memory_order_relaxed), rt = g_rt.load(std::memory_order_relaxed);
    if (lt > pad.bLeftTrigger) pad.bLeftTrigger = lt;
    if (rt > pad.bRightTrigger) pad.bRightTrigger = rt;
    const int16_t lx = g_lx.load(std::memory_order_relaxed), ly = g_ly.load(std::memory_order_relaxed);
    if (lx || ly) { pad.sThumbLX = lx; pad.sThumbLY = ly; }
    const int16_t rx = g_rx.load(std::memory_order_relaxed), ry = g_ry.load(std::memory_order_relaxed);
    if (rx || ry) { pad.sThumbRX = rx; pad.sThumbRY = ry; }
}
#endif
