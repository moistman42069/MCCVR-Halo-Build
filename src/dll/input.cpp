#include "../common/manual_weapon_input.h"
#include <windows.h>
#include <Xinput.h>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <share.h>
#include <MinHook.h>
#include "game.h"
#include "roomscale.h"
#include "../common/roomscale_logic.h"
#include "vr.h"
#include "menu.h"
#include "title_adapter.h"
#include "reach_aim_provider.h"
#include "../common/log.h"
#include "../common/config.h"
#include "../common/halo2_render_logic.h"
#include "../common/input_logic.h"
#include "../common/exclusive_input.h"
#include "../common/flashlight_input.h"
#include "../common/weapon_interaction_logic.h"
#include "../common/odst_bringup_logic.h"
#include "../common/scope_action_input.h"
#include "../common/physical_crouch_input.h"
#include "../common/vr_action_passthrough.h"
#include "../common/vehicle_seat_buttons.h"
#include "physical_crouch_camera.h"
#include "native_vr_actions.h"
#include "dev_pad.h"
#include "halo2_observer_6dof.h"
#include "seat_exit_witness.h"
#include "../common/zoom_pull_audit_logic.h"

// M3 VR input. MCC reads gamepads through XInputGetState; hooking it lets the
// mod present the Sense controllers as a gamepad the game already understands
// (menus included), and substitute the right stick with the aim steering from
// Game_ComputeAimStick. If no physical gamepad is plugged in, a connected one
// is fabricated. The game then runs all input through its normal sensitivity
// and turn-rate paths, keeping bullets, reticle, vehicles and turrets correct.
// (Where direct drive writes the aim -- aim_direct_drive, default 1 -- the
// substituted right stick stands neutral on the axes written.)

namespace
{
    constexpr bool kEnableRetiredInputDiagnostics = false;

    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using XInputGetCapsFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    // 3 candidate DLLs x 2 entry points (XInputGetState by name, and the
    // undocumented XInputGetStateEx at ordinal 100 that some titles use).
    XInputGetStateFn g_origGetState[6] = {};
    // MCC only polls XInputGetState for slots that XInputGetCapabilities says
    // are connected. With no physical pad powered on, that check fails and the
    // game NEVER reads input — so the connection check is hooked too and
    // reports a standard gamepad on slot 0 whenever the VR controllers live.
    XInputGetCapsFn g_origGetCaps[3] = {};
    XInputSetStateFn g_origSetState[3] = {};
    std::atomic<bool> g_overrideLogged{false};
    MenuChordDetector g_menuChord;
    MenuChordDetector g_pauseChord;
    // The vehicle camera buttons' one state machine, for every thread that polls the pad. MCC polls from more than
    // one thread, and with a copy per thread a held button re-centred once for each of them, while a thread that
    // polled too rarely to see the hold reach its second took the release for a tap and stepped the camera.
    vehicle_seat_buttons::Seat g_seatButtons;
    std::mutex g_seatButtonsLock;
    // The zoom release's one edge, for every thread that polls the pad, for the same reason: with a copy per thread
    // each of MCC's polling threads took the same release, two scope toggles a few ms apart, and the scope stayed
    // shut whenever the render thread saw them in different frames (ODST zoom, 2026-10-30).
    ScopeActionInput g_scopeInput;
    std::mutex g_scopeInputLock;
    void SuspendScopeInput() { std::lock_guard<std::mutex> lock(g_scopeInputLock); g_scopeInput.Suspend(); }
    thread_local PhysicalCrouchInput g_physicalCrouchInput;
    std::atomic<bool> g_physicalCrouchRiseCapped{false};   // the menu's recalibrate hint
    // One mapper for every thread that polls the pad, stepped under its lock: a chord starts on a press and a toggle turns
    // on a press, and each press must be seen once.
    vr_mapping::Mapper g_vrActionMapper;
    std::mutex g_vrActionMapperLock;
    // Zoom as the mapper last pressed it, chords and all: the lens toggles on its release (a poll later).
    std::atomic<bool> g_zoomInputDown{false};
    // The Pause row presses Y with B held, or B with Y held: the physical pad without VR controllers and Project
    // Reclaimer's head keys still read that chord their own way.
    bool PauseUsesYB()
    {
        return vr_mapping::HoldsTogether(g_config.vr_mod_bindings[vr_mapping::Pause],vr_mapping::Y,vr_mapping::B);
    }
    void SuspendActionMapper()
    {
        std::lock_guard<std::mutex> lock(g_vrActionMapperLock);
        g_vrActionMapper.ready=false;
        g_zoomInputDown.store(false,std::memory_order_relaxed);
    }
    // The actions a game has none of (vr_mapping::PollOptions::absent).
    uint32_t AbsentActions(GameTitle title) noexcept
    {
        uint32_t absent=0;
        for(unsigned action=0;action<vr_mapping::Count;++action)
            if(vr_mapping::NativeAction(title,static_cast<vr_mapping::Action>(action))==vr_mapping::kNoAction)
                absent|=1u<<action;
        return absent;
    }
    std::atomic<uint64_t> g_startPulseUntilMs{0};
std::atomic<uint32_t> g_menuPressSerial{0};

#if HALOMCCVR_DEV_DIAGNOSTICS
    // MCC's crouch-lock (toggle crouch) preference as saved in GameUserSettings.ini
    // (CrouchLockEnabled[0..6] / MKCrouchLockEnabled[0..6]), bitmasks (bit n = slot n), -1 = not read.
    // LOGGED ONLY, never acted on: Reach keeps HOLD behaviour for the crouch press with slot 6, slot 5,
    // and even every slot of both arrays set (measured), so the file cannot predict how Reach
    // treats the button and a stand-down keyed on it would disable a crouch that works. If a toggle
    // ever does invert physical crouch, the CROUCH line shows it directly: active=0 pressed=0x0 with
    // unitCrouched=1 persisting after standing. Dev builds only; written by the cold worker.
    std::atomic<int> g_mccCrouchLockPad{-1}, g_mccCrouchLockMk{-1};

    // The physical-crouch diagnostic snapshot. The XInput hook only STORES it (no file I/O, no log lock
    // on the game's input thread); the cold worker prints it once a second in gameplay. Dev builds only.
    struct PhysicalCrouchStatus
    {
        std::atomic<uint64_t> atMs{0}, epoch{0};
        std::atomic<uint32_t> title{0}, gates{0}, pressed{0}, transport{0}, generation{0}, onFootWhy{0};
        std::atomic<float> head{0.0f}, base{0.0f};
        std::atomic<bool> calibrated{false};
    } g_crouchStatus;

    void PublishPhysicalCrouch(uint64_t now, GameTitle title, bool tracking, bool nativeReady, bool onFoot,
                               uint32_t onFootWhy, const VrPhysicalCrouchSample& sample, const PhysicalCrouch& g,
                               uint32_t pressed, uint32_t transport, uint32_t generation)
    {
        g_crouchStatus.title.store(static_cast<uint32_t>(title), std::memory_order_relaxed);
        g_crouchStatus.gates.store((tracking ? 1u : 0u) | (nativeReady ? 2u : 0u) | (onFoot ? 4u : 0u) |
            (g.Initialized() ? 8u : 0u) | (g.Active() ? 16u : 0u) | (g.Suspended() ? 32u : 0u),
            std::memory_order_relaxed);
        g_crouchStatus.onFootWhy.store(onFootWhy, std::memory_order_relaxed);
        g_crouchStatus.pressed.store(pressed, std::memory_order_relaxed);
        g_crouchStatus.transport.store(transport, std::memory_order_relaxed);
        g_crouchStatus.generation.store(generation, std::memory_order_relaxed);
        g_crouchStatus.epoch.store(sample.reference, std::memory_order_relaxed);
        g_crouchStatus.head.store(sample.height, std::memory_order_relaxed);
        g_crouchStatus.base.store(sample.baseline, std::memory_order_relaxed);
        g_crouchStatus.calibrated.store(sample.calibrated, std::memory_order_relaxed);
        g_crouchStatus.atMs.store(now, std::memory_order_release);
    }

    // SEATEXIT witness: this poll's record, filled along MergeVrPad and committed (after every filter,
    // including the flashlight scope) by ProcessGetState. Dev builds only; see seat_exit_witness.h.
    thread_local seat_witness::Sample g_witness;

    uint32_t WitnessTransport(const XINPUT_GAMEPAD& pad) noexcept
    {
        return pad.wButtons |
            (pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? seat_witness::kTransportLT : 0u) |
            (pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? seat_witness::kTransportRT : 0u);
    }

    void WitnessPad(const VrPadState& pad) noexcept
    {
        using namespace seat_witness;
        auto& w = g_witness;
        w.raw |= (pad.valid ? RawPadValid : 0u) | (pad.a ? RawA : 0u) | (pad.b ? RawB : 0u) |
            (pad.x ? RawX : 0u) | (pad.y ? RawY : 0u) | (pad.clickL ? RawClickL : 0u) |
            (pad.clickR ? RawClickR : 0u) | (pad.menu ? RawMenu : 0u) |
            (pad.thumbrestDpad ? RawThumbrestDpad : 0u) | (pad.exclusiveInput ? RawExclusive : 0u) |
            (pad.devTouch & 0xFu) * RawThumbrestL;   // devTouch bits 0..3 = TRL, TRR, STL, STR
        w.moveX = QuantizeAxis(pad.moveX); w.moveY = QuantizeAxis(pad.moveY);
        w.turnX = QuantizeAxis(pad.turnX); w.turnY = QuantizeAxis(pad.turnY);
        w.trigL = QuantizeUnit(pad.trigL); w.trigR = QuantizeUnit(pad.trigR);
        w.gripL = QuantizeUnit(pad.gripL); w.gripR = QuantizeUnit(pad.gripR);
        w.weapon = pad.weaponButtons & kTransportMask;
    }

    void CommitSeatWitness(const XINPUT_STATE& state) noexcept
    {
        auto& w = g_witness;
        w.fed = WitnessTransport(state.Gamepad);
        // Dev SHOT RETICLE: the RT the game reads this poll (its rising edge opens a trigger-to-fire latency).
        Game_DevNoteFireTrigger(state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        w.thread = static_cast<uint16_t>(GetCurrentThreadId());
        vr_mapping::Transports native{};
        if (Game_ReadVrActionBindings(native, GetTickCount64()))
            w.useMask = seat_witness::UseTransportMask(TitleAdapter_GetActiveTitle(), native);
        SeatWitness_Record(w);
    }
#endif

    // E-H2-13: the last button masks fed to the game, newest at g_fedHead-1.
    // One slot per CHANGE of mask (a held button is one entry), so a 600 ms
    // window around an engine event is a handful of entries. Lock-free, the
    // reporter tolerates a torn slot (it only names a mask and an age).
    constexpr unsigned kFedButtonSlots = 16;
    std::atomic<uint32_t> g_fedButtons[kFedButtonSlots]{};
    std::atomic<uint64_t> g_fedButtonsMs[kFedButtonSlots]{};
    std::atomic<unsigned> g_fedHead{0};
    std::atomic<uint32_t> g_fedLastMask{0xFFFFFFFFu};
    std::atomic<uint64_t> g_fedBackMs{0};

    void NoteFedButtons(WORD mask) noexcept
    {
        if (g_hostIsProjectReclaimer)
            NativeVrActions_NoteZoomInput(0, (mask & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
        if (g_fedLastMask.load(std::memory_order_relaxed) == mask)
            return;
        g_fedLastMask.store(mask, std::memory_order_relaxed);
        const unsigned slot =
            g_fedHead.fetch_add(1, std::memory_order_relaxed) % kFedButtonSlots;
        g_fedButtonsMs[slot].store(GetTickCount64(), std::memory_order_relaxed);
        g_fedButtons[slot].store(mask, std::memory_order_release);
        if (mask & XINPUT_GAMEPAD_BACK)
            g_fedBackMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    // E-H2-19: what the PHYSICAL pad reported, before any swallowing, so a
    // renderer switch can be tied to (or cleared of) the player's own pad.
    std::atomic<uint32_t> g_physicalLastMask{0};
    std::atomic<uint64_t> g_physicalBackMs{0};
    void NotePhysicalButtons(WORD mask) noexcept
    {
        g_physicalLastMask.store(mask, std::memory_order_relaxed);
        if (mask & XINPUT_GAMEPAD_BACK)
            g_physicalBackMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    // E-H2-19: the head-gesture stick click in Halo 2 is a deliberate
    // renderer switch only when HELD (a brief click does nothing), so the
    // moment it is honoured is unmistakable in the log and it can never
    // flip the renderer by accident.
    std::atomic<uint64_t> g_halo2GestureClickSinceMs{0};
    std::atomic<bool> g_halo2GestureClickSent{false};
    constexpr uint64_t kHalo2GestureClickHoldMs = 350;

    // Map |v| in 0..1 to a raw stick value that clears MCC's inner deadzone,
    // so small corrections still produce movement.
    SHORT ToRawStick(float v)
    {
        if (fabsf(v) < 1e-3f)
            return 0;
        const float floor = 9000.0f; // typical XInput deadzone is ~7849
        float raw = floor + fabsf(v) * (32767.0f - floor);
        if (raw > 32767.0f) raw = 32767.0f;
        return (SHORT)(v < 0 ? -raw : raw);
    }

    // Plain analog mapping for the game's OWN menus (pause/settings/shell): map
    // |v| in 0..1 straight to the full stick range with NO deadzone floor, so
    // MCC's own menu deadzone rejects a small off-axis component. Without this a
    // near-vertical push like (0.15, 0.98) had its 0.15 minor axis floored past
    // the deadzone by ToRawStick, so up/down leaked into left/right (GitHub #9).
    SHORT ToRawMenuStick(float v)
    {
        if (v > 1.0f) v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        return (SHORT)(v * 32767.0f);
    }

    // Project Reclaimer's keys at the head (HeadKeyGesture). A key only ever goes to this process's own window: anywhere
    // else it would type a letter. Returns whether the button belongs to the key right now.
    bool ControllerAtHead(int hand)
    {
        float headQ[4], headP[3], handQ[4], handP[3];
        return VR_GetPhysicalControllerPose(hand, handQ, handP) && VR_GetHeadPose(headQ, headP) &&
            DpadHeadWithinRadius(headP, handP, g_config.dpad_head_radius);
    }

    bool StepHeadKey(HeadKeyGesture& gesture, WORD& sentKey, bool enabled, int key, const char* name, bool padValid,
                     bool down, int hand = 0)
    {
        bool allowed = padValid && g_hostOwnsRenderSize && enabled && !Menu_IsOpen();
        if (allowed)
        {
            DWORD foregroundProcess = 0;
            if (const HWND foreground = GetForegroundWindow())
                GetWindowThreadProcessId(foreground, &foregroundProcess);
            allowed = foregroundProcess == GetCurrentProcessId();
        }
        bool atHead = false;
        if (allowed && down && !gesture.talking)
            atHead = ControllerAtHead(hand);
        const HeadKeyGesture::Key step = gesture.Step(atHead, down, allowed, GetTickCount64());
        if (step != HeadKeyGesture::Key::None)
        {
            const bool press = step == HeadKeyGesture::Key::Press;
            if (press)
                sentKey = static_cast<WORD>(key);
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = sentKey;
            input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(sentKey, MAPVK_VK_TO_VSC));
            input.ki.dwFlags = press ? 0 : KEYEVENTF_KEYUP;
            const bool sent = SendInput(1, &input, sizeof(input)) == 1;
            if (gesture.logged < 24 && ++gesture.logged)
                LOG("Reclaimer %s key: %s 0x%02X%s", name, press ? "pressed" : "released", unsigned(sentKey),
                    sent ? "" : " (Windows refused it)");
            if (press && sent)
                VR_PulseGameHaptics(0.35f);
        }
        return gesture.talking;
    }

    // What each action of a game is bound to right now: its Automatic rows follow the game's own layout.
    vr_mapping::Overrides EffectiveBindings(int title,const vr_mapping::Transports& layout,bool layoutKnown)
    {
        return vr_mapping::EffectiveOverrides(g_config.vr_bindings[title],layout,layoutKnown);
    }

    // Forge's Monitor answers to the controller as a gamepad: its object menus back out on B and turn their pages with
    // the bumpers, buttons no gameplay action may hold. While the game says the player is the Monitor (Halo 3, Reach,
    // Halo 4: the sandbox-editor answer two-handed aim already reads), the VR controllers are that gamepad, as with VR
    // Mappings off. Unknown is not the Monitor.
    bool ForgeMonitor(GameTitle title) noexcept
    {
        fp_view::Signal camera{},monitor{};
        int32_t mode=-1,perspective=-1;
        const bool yes=Game_FirstPersonCameraSample(title,camera,mode,perspective,monitor)&&
            monitor==fp_view::Signal::Yes;
        static std::atomic<bool> said{false};
        if(said.exchange(yes,std::memory_order_relaxed)!=yes)
            LOG(yes?"VR Mappings: Forge Monitor: the VR controllers are the gamepad (A, B, X, Y as themselves, grips as the bumpers)":
                "VR Mappings: Forge Monitor left: the rows map the controls again");
        return yes;
    }

    void MergeVrPad(XINPUT_STATE* state)
    {
        VrPadState pad;
        VR_GetPadState(pad);
        if (!pad.valid)
        {
            Game_VehicleBoomStick(0.0f);
            SuspendActionMapper();
            SuspendScopeInput();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false,0,0);
            return;
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        WitnessPad(pad);
        g_witness.route = seat_witness::RouteNormal;
#endif

        const bool sharedGameplayInput =
            Game_AllowsSharedGameplayFeatures();
        // The face buttons as the controllers gave them: the vehicle camera buttons below take left Y and right B
        // over in a seat, and the pause chord further down is still read from these.
        const bool rawPadY = pad.y, rawPadB = pad.b;
        const MenuChordResult chord =
            g_menuChord.Update(GetTickCount64(), pad.clickL, pad.clickR);
        if (chord.toggled)
        {
            // The same one-shot chord edge owns both actions. Game_Recenter
            // resets every title's immersive camera reference and the OpenXR
            // room-fixed origin used by theatre; opening F1 on that edge then
            // places its head-locked panel from the newly centered pose.
            Game_RecenterManual();
            Menu_Toggle();
            LOG("L3+R3: recentered immersive/theatre space and toggled F1 menu");
        }

        // THE VEHICLE CAMERA BUTTONS (vehicle_seat_buttons.h): in a seat the vehicle cameras run in, left Y steps
        // the camera, left X its tethering mode, either held re-centres the view, and the left stick click moves the
        // seat to its next way of being controlled; right B is the game's own B (the Banshee's bomb), or left X's job
        // (use, exit) where the action mapper binds B to nothing (vehicle_seat_buttons::RightBFor); and in a
        // passenger's seat the support trigger switches weapon. Before anything below reads a button, so every
        // route to the game -- the action mapper, the native bridge, legacy routing -- sees the seat's buttons.
        bool inSeat = false;
        {
            bool passenger = false, controlModes = true;
            const bool live =
                Game_VehicleSeatButtonsLive(passenger, controlModes) && !Menu_IsOpen() && !pad.exclusiveInput;
            inSeat = live;
            vehicle_seat_buttons::Buttons buttons;
            buttons.x = pad.x;
            buttons.y = pad.y;
            buttons.b = pad.b;
            buttons.clickL = pad.clickL;
            buttons.clickR = pad.clickR;
            buttons.supportTrigger = pad.trigL;
            // Does the action mapper bind right B to anything in this game? (By default it binds it to nothing.)
            bool bBound = false;
            const int seatTitle = weapon_interaction::TitleIndex(Game_ControlsTitle(TitleAdapter_GetActiveTitle()));
            if (seatTitle >= 0 && g_config.vr_action_mapping)
            {
                vr_mapping::Transports layout{};
                const bool layoutKnown = Game_ReadVrActionBindings(layout, GetTickCount64());
                const auto effective = EffectiveBindings(seatTitle, layout, layoutKnown);
                for (unsigned action = 0; action < vr_mapping::Count; ++action)
                    bBound = bBound || (vr_mapping::ControlBits(static_cast<vr_mapping::Action>(action), effective,
                        &g_config.vr_inputs[seatTitle]) & vr_mapping::Bit(vr_mapping::B)) != 0;
                // An extra row live in a seat on B keeps right B its own, or the seat would turn it into use and exit the
                // vehicle.
                for (unsigned action = 0; action < vr_mapping::ExtraCount; ++action)
                    bBound = bBound || (vr_mapping::kExtraContexts[action] != vr_mapping::ExtraContext::OnFoot &&
                        (vr_mapping::ExtraControlBits(g_config.vr_extra_bindings[seatTitle][action]) &
                        vr_mapping::Bit(vr_mapping::B)) && vr_mapping::NativeExtraAction(TitleAdapter_GetActiveTitle(),
                            static_cast<vr_mapping::ExtraAction>(action)) != vr_mapping::kNoAction);
            }
            const vehicle_seat_buttons::RightB rightB =
                vehicle_seat_buttons::RightBFor(g_config.vr_action_mapping, bBound);
            vehicle_seat_buttons::Requests asked;
            {
                // The clock is read under the lock, so the polls the state machine sees are in time order.
                std::lock_guard<std::mutex> lock(g_seatButtonsLock);
                asked = g_seatButtons.Step(buttons, live, passenger, GetTickCount64(),
                                           vehicle_seat_buttons::kDefaultHoldMs, rightB, controlModes);
            }
#if HALOMCCVR_DEV_DIAGNOSTICS
            static std::atomic<bool> seatBWas{false};
            const bool seatB = live && rawPadB;
            if (seatB != seatBWas.exchange(seatB, std::memory_order_relaxed) && seatB)
                LOG("Vehicle cameras (dev): right B pressed in the seat; handed to the game as %s",
                    buttons.b ? "its own B" : (buttons.x ? "use (left X's job)" : "nothing (the pause chord)"));
#endif
            if (live)
            {
                pad.x = buttons.x;
                pad.y = buttons.y;
                pad.b = buttons.b;
                pad.clickL = buttons.clickL;
                pad.trigL = buttons.supportTrigger;
            }
            if (asked.camera || asked.mode)
                Game_VehicleCameraStep(asked.camera, asked.mode);
            // A camera switched to starts with the view re-centred on the seat, as Campaign Evolved's do, so a tap of
            // left Y needs no hold after it. The boom keeps its angle: that is the seat's, kept on purpose.
            if (asked.camera && !asked.recenter)
            {
                Game_RecenterManual();
                LOG("Vehicle cameras: view re-centred with the camera switch");
            }
            if (asked.control)
                Game_VehicleControlStep();
            if (asked.recenter)
            {
                Game_RecenterManual();
                // A third-person camera's boom goes back to where its camera starts with the view.
                Game_VehicleBoomRecenter();
                LOG("Vehicle cameras: view re-centred (left X or Y held)");
            }
            if (asked.camera || asked.mode || asked.control || asked.recenter)
                VR_PulseGameHaptics(asked.recenter ? 0.6f : 0.35f);
        }

        static thread_local ExclusiveInputHolds physicalHolds;
        static thread_local ExclusiveInputHolds vrMenuHolds;
        const bool menuOpen=Menu_IsOpen();
        vrMenuHolds.ApplyVr(pad,menuOpen,false);
        const bool pointerConfirm=pad.exclusiveInput&&!pad.thumbrestDpad&&!Menu_IsOpen();
        physicalHolds.ApplyPhysical(state->Gamepad,pad.exclusiveInput||Menu_IsOpen(),pointerConfirm);
        // UEVR-style D-pad gesture: hold the configured controller (F1 menu:
        // left by default) up next to your head and the left stick becomes the
        // D-pad (menu navigation, grenade switching); lower it and the stick
        // walks again. No menu detection — the player chooses the mode with
        // the gesture, anywhere.
        bool dpadMode = false;
        {
            float hq[4], hp[3], cq[4], cp[3];
            const bool ce = TitleAdapter_GetActiveTitle() == GameTitle::HaloCE;
            const bool haveController = VR_GetPhysicalControllerPose(
                ce ? 0 : (g_config.dpad_hand == 0 ? 0 : 1), cq, cp);
            if (haveController && VR_GetHeadPose(hq, hp))
            {
                const float dx = hp[0] - cp[0], dy = hp[1] - cp[1], dz = hp[2] - cp[2];
                dpadMode = DpadHeadWithinRadius(hp, cp, g_config.dpad_head_radius);
                // CE's requested switch uses the physical left hand beside
                // the left of the head, independent of weapon handedness.
                if (ce)
                {
                    const float rightX=1.0f-2.0f*(hq[1]*hq[1]+hq[2]*hq[2]);
                    const float rightY=2.0f*(hq[0]*hq[1]+hq[3]*hq[2]);
                    const float rightZ=2.0f*(hq[0]*hq[2]-hq[3]*hq[1]);
                    dpadMode = dpadMode && dx*rightX+dy*rightY+dz*rightZ>0.03f;
                }
            }
        }
        static thread_local DpadMovementLatch dpadMovement;
        dpadMode = dpadMovement.Step(dpadMode, g_config.dpad_keep_moving && Game_MoveStickIsLocomotion() &&
            pad.moveX * pad.moveX + pad.moveY * pad.moveY > 0.04f);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (dpadMode) g_witness.raw |= seat_witness::RawDpadGesture;
#endif


        // THE MOD'S OWN ROWS (vr_mapping::ModMapper): Pause on its inputs (Y + B either way round), read from the controls
        // as the controllers gave them (before the seat's buttons) and the physical pad's Y and B, so a Steam Controller
        // pauses the same way. While one of its inputs is on, the controls it uses press nothing else (pauseConsumed).
        const WORD physicalButtons = state->Gamepad.wButtons;
        uint32_t pauseConsumed = 0;
        {
            VrPadState rawPad = pad;
            rawPad.y = rawPadY;
            rawPad.b = rawPadB;
            uint32_t modSources = ScopeAndActionSources(rawPad, dpadMode);
            if (physicalButtons & XINPUT_GAMEPAD_Y) modSources |= vr_mapping::Bit(vr_mapping::Y);
            if (physicalButtons & XINPUT_GAMEPAD_B) modSources |= vr_mapping::Bit(vr_mapping::B);
            static std::mutex modRowsLock;
            static vr_mapping::ModMapper modRows;
            std::lock_guard<std::mutex> lock(modRowsLock);
            const uint32_t started = modRows.Step(modSources,
                !menuOpen && !pad.exclusiveInput && Game_AllowsPauseToggleInput(), g_config.vr_mod_bindings);
            if (started & (1u << vr_mapping::Pause))
                Input_RequestPauseToggle();
            pauseConsumed = modRows.consumed;
        }

        const auto currentTitle=TitleAdapter_GetActiveTitle();
        // The rows are the controls title's (Project Reclaimer's ODST levels: ODST's); the engine stays currentTitle.
        const auto controlsTitle=Game_ControlsTitle(currentTitle);
        const int mappingTitle=weapon_interaction::TitleIndex(controlsTitle);
        const auto inputMode=TitleAdapter_GetRuntimeMode();
        const bool gameplayMode=(inputMode==RuntimeMode::Gameplay||inputMode==RuntimeMode::Vehicle||inputMode==RuntimeMode::Turret)&&
            !VR_IsPausePresentation()&&!VR_IsPausePresentationTarget()&&!VR_IsCutsceneTheaterActive();
        const uint64_t mappingEpoch=(uint64_t(TitleAdapter_GetGeneration(currentTitle))<<32)|
            (uint64_t(controlsTitle)<<24)|pad.profileEpoch;
        const bool scopeFeatureAvailable=g_config.scope_enabled&&Game_IsHeadTracking()&&Game_HasScopeRenderer()&&
            !Game_AimFrameZoomOwnsZoom();
        const bool scopeAvailable=scopeFeatureAvailable&&gameplayMode;
        vr_mapping::Transports gameLayout{};
        const bool gameLayoutKnown=mappingTitle>=0&&g_config.vr_action_mapping&&
            Game_ReadVrActionBindings(gameLayout,GetTickCount64());
        const vr_mapping::Overrides effectiveBindings=mappingTitle>=0?
            EffectiveBindings(mappingTitle,gameLayout,gameLayoutKnown):vr_mapping::Overrides{};
        // This game's rows' inputs: what each holds first, its mode, and its further inputs (VR Mappings).
        const vr_mapping::Inputs* rowInputs=mappingTitle>=0?&g_config.vr_inputs[mappingTitle]:nullptr;
        // Zoom's own control for the lens: its row's press, or its first further input's where the row has none.
        int zoomSource=mappingTitle>=0&&g_config.vr_action_mapping?
            vr_mapping::Resolve(vr_mapping::Zoom,effectiveBindings[vr_mapping::Zoom]):vr_mapping::RightClick;
        if(!vr_mapping::Bit(zoomSource)&&rowInputs&&g_config.vr_action_mapping)
            for(const auto& input:(*rowInputs)[vr_mapping::Zoom].more)
                if(vr_mapping::ListedSource(input.press)&&vr_mapping::Bit(input.press)) {zoomSource=input.press;break;}
        // What every row presses in this game, Automatic read from the game's own layout, and the game's own button
        // behind each action (0: none): once each time it changes, and only once the layout is read.
        if(mappingTitle>=0&&g_config.vr_action_mapping&&gameLayoutKnown)
        {
            static std::atomic<uint64_t> logged{0};
            const uint64_t shown=vr_mapping::Fingerprint(effectiveBindings,gameLayout)^(uint64_t(controlsTitle)<<56)^
                vr_mapping::Fingerprint(*rowInputs)*37u;
            if(logged.exchange(shown,std::memory_order_relaxed)!=shown)
            {
                // Each input as its held controls and its press joined by '+', a toggle marked, inputs apart by " | ".
                const auto describe=[](const vr_mapping::HeldControls& held,int press,vr_mapping::InputMode mode)
                {
                    std::string text;
                    for(const int source:held)
                        if(source!=vr_mapping::Unbound) text+=std::string(vr_mapping::kSourceNames[source])+"+";
                    text+=vr_mapping::kSourceNames[press];
                    if(mode==vr_mapping::InputMode::Toggle) text+=" (toggle)";
                    return text;
                };
                std::string rows;
                char native[16];
                for(unsigned action=0;action<vr_mapping::Count;++action)
                {
                    const auto& row=(*rowInputs)[action];
                    sprintf_s(native," (0x%X)",gameLayout[action]);
                    rows+=std::string(action?", ":"")+vr_mapping::kKeys[action]+" = "+describe(row.held,
                        vr_mapping::Resolve(static_cast<vr_mapping::Action>(action),effectiveBindings[action]),row.mode);
                    for(const auto& more:row.more)
                        if(vr_mapping::ListedSource(more.press)&&more.press!=vr_mapping::Unbound)
                            rows+=" | "+describe(more.held,more.press,more.mode);
                    rows+=native;
                }
                LOG("VR Mappings %s: %s",weapon_interaction::kTitleNames[mappingTitle],rows.c_str());
            }
        }
        // Held controls the mapper latched on a change, reported every 5 s while it grows: each one a held press that
        // reached the game as a tap at most.
        {
            static std::atomic<uint32_t> reportedRelatches{0};
            static std::atomic<uint64_t> reportedAt{0};
            uint32_t relatched=0;
            {std::lock_guard<std::mutex> lock(g_vrActionMapperLock);relatched=g_vrActionMapper.relatched;}
            const uint64_t now=GetTickCount64();
            uint64_t at=reportedAt.load(std::memory_order_relaxed);
            if(relatched!=reportedRelatches.load(std::memory_order_relaxed)&&now-at>=5000&&
                reportedAt.compare_exchange_strong(at,now))
            {
                reportedRelatches.store(relatched,std::memory_order_relaxed);
                LOG("VR Mappings: held controls latched on a change %u times so far (a held press reaches the game "
                    "as a tap at most)",relatched);
            }
        }
        uint32_t collectedSources=ScopeAndActionSources(pad,dpadMode);
        // The support hand holding the gun is the two-handed hold, not a button: what the support grip is mapped to waits
        // for a grip off the gun. Held, it pressed that action all through the hold, and a held action stops a game's
        // sprint (Halo 4's right stick up).
        if(pad.supportAimActive) collectedSources&=~vr_mapping::Bit(vr_mapping::SupportGrip);
        // THE SUPPORT HAND ON THE GUN (vr_mapping::HandOnGun): down while the support hand holds the gun, two-handed or
        // near a one-handed one. A row holds it first for the gun grip's zoom (vr_mapping::kGunZoom).
        const bool handOnGun=pad.supportAimActive||pad.supportOnOneHandedGun;
        if(handOnGun) collectedSources|=vr_mapping::Bit(vr_mapping::HandOnGun);
        const bool triggerDown=(collectedSources&vr_mapping::Bit(vr_mapping::SupportTrigger))!=0;
        // Where the support trigger has chords (the gun grip's zoom, the empty grip's equipment), a pull begun while that
        // hand's grip holds a magazine or a holster presses nothing until let go. A seat or two guns keep it plain.
        {
            const bool chorded=rowInputs&&g_config.vr_action_mapping&&!pad.dualWield&&!inSeat&&
                vr_mapping::ChordPresses(effectiveBindings,*rowInputs,vr_mapping::SupportTrigger);
            static std::mutex claimLock;
            static bool triggerWasDown=false,triggerClaimed=false;
            std::lock_guard<std::mutex> lock(claimLock);
            if(!triggerDown) triggerClaimed=false;
            else if(!triggerWasDown) triggerClaimed=chorded&&pad.weaponConsumeSupport;
            triggerWasDown=triggerDown;
            if(triggerClaimed) collectedSources&=~vr_mapping::Bit(vr_mapping::SupportTrigger);
        }
        // What may be held first: a grip on the gun is no air grip, and a seat or two guns keep the support trigger plain.
        uint32_t heldOk=~0u;
        if(handOnGun) heldOk&=~vr_mapping::Bit(vr_mapping::SupportGrip);
        if(pad.dualWield||inSeat) heldOk&=~(vr_mapping::Bit(vr_mapping::SupportGrip)|vr_mapping::Bit(vr_mapping::HandOnGun));
        // The gun grip's zoom (a Zoom input that holds the hand on the gun) ends when the hand leaves the gun. The lens has a
        // state of its own to close; the game's zoom is pressed out.
        const bool gunZoom=rowInputs&&g_config.vr_action_mapping&&
            vr_mapping::HoldsHandOnGun(vr_mapping::Zoom,effectiveBindings,*rowInputs);
        bool zoomPressedOut=false;
        {
            const bool contextualZoom=gunZoom&&gameplayMode&&!menuOpen&&!pad.exclusiveInput&&!pad.dualWield&&!inSeat;
            static std::mutex zoomExitLock;
            static vr_mapping::ZoomExit zoomExit;
            static bool lensHandWasOn=false;
            int zoomLevel=-1;
            const bool zoomLevelKnown=Game_ReadZoomLevel(zoomLevel);
            std::lock_guard<std::mutex> lock(zoomExitLock);
            // Each let-go while the game reads zoomed, and whether it is pressed out, to set beside what the player saw.
            if(lensHandWasOn&&!handOnGun&&zoomLevelKnown&&zoomLevel>=0)
                LOG("Zoom exit: the support hand left the gun at the game's zoom level %d; %s",zoomLevel,
                    scopeFeatureAvailable?"the lens closes instead":
                    !gunZoom?"not pressed out (no Zoom input holds the support hand on the gun)":
                    !gameplayMode?"not pressed out (the game is not in gameplay)":
                    menuOpen||pad.exclusiveInput?"not pressed out (a menu owns the controls)":
                    pad.dualWield||inSeat?"not pressed out (dual wielding or seated)":"pressing zoom out");
            zoomPressedOut=zoomExit.Step(contextualZoom&&!scopeFeatureAvailable,handOnGun,
                triggerDown,zoomLevelKnown,zoomLevel,GetTickCount64());
            if(contextualZoom&&scopeFeatureAvailable&&lensHandWasOn&&!handOnGun) VR_SetScopeActive(false);
            lensHandWasOn=handOnGun;
        }
        // Changing D-pad gesture routes while the stick is held is cancellation,
        // not the release edge that toggles a scope. Face/trigger bindings are
        // unaffected by entering the optional head D-pad gesture.
        const unsigned scopeRoute=zoomSource>=vr_mapping::DpadUp&&zoomSource<=vr_mapping::DpadRight?
            (pad.thumbrestDpad?2u:(dpadMode?1u:0u)):0u;
        // The lens follows Zoom as the mapper presses it, chords and their rules included (g_zoomInputDown, a poll late).
        const int scopeSource=vr_mapping::Bit(zoomSource)?zoomSource:vr_mapping::RightClick;
        const uint32_t scopeSources=rowInputs&&g_config.vr_action_mapping?
            (collectedSources&~vr_mapping::Bit(scopeSource))|
            (g_zoomInputDown.load(std::memory_order_relaxed)?vr_mapping::Bit(scopeSource):0u):collectedSources;
        ScopeToggleUpdate scope{};
        {
            std::lock_guard<std::mutex> lock(g_scopeInputLock);
            scope=g_scopeInput.Update(scopeFeatureAvailable,scopeSources,
                scopeSource,mappingEpoch,scopeRoute,
                scopeAvailable&&!menuOpen&&(!pad.exclusiveInput||pad.thumbrestDpad),
                chord.consumeClicks||pauseConsumed!=0);
        }
        if(scope.changed&&scopeAvailable)
        {
            VR_RequestScopeToggle();
            LOG("universal scope: mapped input release toggled body-safe scope state");
        }
        if(!scopeFeatureAvailable) VR_SetScopeActive(false);
        if(menuOpen)
        {
            Game_VehicleBoomStick(0.0f);
            SuspendActionMapper();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false,0,0);
            state->Gamepad={};
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.route = seat_witness::RouteF1Menu;
            g_witness.raw |= seat_witness::RawF1Menu;
            g_witness.weapon = 0;
#endif
            return;
        }

        if(pad.exclusiveInput&&!Menu_IsOpen())
        {
            Game_VehicleBoomStick(0.0f);
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.route = seat_witness::RouteExclusive;
            g_witness.weapon = 0;   // the exclusive route never forwards weapon-gesture buttons
            if (gameplayMode) g_witness.flags |= seat_witness::FlagGameplay;
#endif
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false,0,0);
            (void)Game_GestureMeleeInput(GetTickCount64()); // drains a pending gesture pulse
            g_pauseChord.Reset();
            g_startPulseUntilMs.store(0);
            // The native menu pointer's face buttons and bumpers (ExclusiveInputHolds): the menu's own A, B, X and Y,
            // and the grips as its tab bumpers.
            WORD face=0;
            if(pointerConfirm)
            {
                face=state->Gamepad.wButtons&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y|
                    XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER);
                if(pad.a) face|=XINPUT_GAMEPAD_A;
                if(pad.b) face|=XINPUT_GAMEPAD_B;
                if(pad.x) face|=XINPUT_GAMEPAD_X;
                if(pad.y) face|=XINPUT_GAMEPAD_Y;
                if(pad.gripL>0.6f) face|=XINPUT_GAMEPAD_LEFT_SHOULDER;
                if(pad.gripR>0.6f) face|=XINPUT_GAMEPAD_RIGHT_SHOULDER;
            }
            state->Gamepad={};
            state->Gamepad.wButtons=face;
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.mapper = face;   // native menu pointer's face buttons
#endif
            if(pad.thumbrestDpad)
            {
                const auto title=TitleAdapter_GetActiveTitle();
                const int index=weapon_interaction::TitleIndex(Game_ControlsTitle(title));
                if(g_config.vr_action_mapping&&index>=0&&
                    gameplayMode)
                {
                    vr_mapping::Transports native{};
                    const uint64_t now=GetTickCount64();
                    const bool direct=NativeVrActions_CanRoute(title,now);
                    const bool ready=Game_ReadVrActionBindings(native,now)||direct;
                    const vr_mapping::Transports realNative=native;
                    if(direct) for(unsigned action=0;action<vr_mapping::Count;++action)
                        native[action]=NativeVrActions_GestureBit(static_cast<vr_mapping::Action>(action));
                    if(scopeAvailable) native[vr_mapping::Zoom]=0;
                    vr_mapping::PollOptions poll;
                    poll.heldOk=heldOk;
                    poll.absent=AbsentActions(title);
                    vr_mapping::MappedActions actions;
                    {
                        std::lock_guard<std::mutex> lock(g_vrActionMapperLock);
                        actions=g_vrActionMapper.ApplyDetailed(collectedSources,
                            effectiveBindings,native,
                            (uint64_t(TitleAdapter_GetGeneration(title))<<32)|(uint64_t(title)<<24)|pad.profileEpoch,
                            ready,g_config.disable_flashlight_input||
                                (g_config.flashlight_suppress_on_two_hand&&pad.supportAimActive),nullptr,
                            &g_config.vr_inputs[index],poll);
                        g_zoomInputDown.store(actions.Has(vr_mapping::Zoom),std::memory_order_relaxed);
                    }
                    // Halo 2 context actions keep their real MCC button; see
                    // vr_action_passthrough.h. Every other action is unchanged.
                    const auto routed=vr_mapping::RouteActions(title,direct,actions,realNative,
                        inSeat||inputMode==RuntimeMode::Vehicle||inputMode==RuntimeMode::Turret,&effectiveBindings,
                        &g_config.vr_inputs[index]);
                    const uint32_t mapped=routed.buttons;
                    NativeVrActions_Publish(state,title,now,routed.published&
                        ~(scopeAvailable?(1u<<vr_mapping::Zoom):0u),direct);
                    state->Gamepad.wButtons=static_cast<WORD>(mapped);
                    if(mapped&(1u<<16)) state->Gamepad.bLeftTrigger=255;
                    if(mapped&(1u<<17)) state->Gamepad.bRightTrigger=255;
#if HALOMCCVR_DEV_DIAGNOSTICS
                    g_witness.sources = collectedSources;
                    g_witness.actions = actions.actions&~(scopeAvailable?(1u<<vr_mapping::Zoom):0u);
                    g_witness.mapper = mapped & seat_witness::kTransportMask;
                    g_witness.flags |= seat_witness::FlagSemantic | seat_witness::FlagPublished |
                        (direct ? seat_witness::FlagDirect : 0);
#endif
                }
                else
                {
                    SuspendActionMapper();
                    state->Gamepad.wButtons=DpadDirectionButtons(pad.dpadX,pad.dpadY);
#if HALOMCCVR_DEV_DIAGNOSTICS
                    g_witness.dpad = state->Gamepad.wButtons;
#endif
                }
            }
            else SuspendActionMapper();
            NoteFedButtons(state->Gamepad.wButtons);
            return;
        }

        // C9 (Halo 3 ground driver seats only; inert everywhere else): read the
        // virtual steering wheel from THIS poll's pad, before the buttons below
        // are built. Both grips down together is the wheel's own gesture and is
        // the only case where their buttons are withheld — a lone right grip is
        // never touched, so it keeps dismounting the vehicle.
        Game_Halo3UpdateVehicleWheel(pad);
        // C-H4-10: Halo 4's VR turn advances from this same shared pad sample,
        // before the right-stick block below decides who writes the axes.
        Game_Halo4UpdateVrTurn(pad);
        const bool wheelGesture = Game_Halo3VehicleSwallowsGrips();

        // VR defaults are semantic and shared. Each verified native adapter
        // reads the current MCC layout; Reach no longer trades LT and X.
        const uint64_t inputNow=GetTickCount64();
        vr_mapping::Transports nativeActions{};
        const bool forgeMonitor=gameplayMode&&ForgeMonitor(currentTitle);
        const bool semanticMode=g_config.vr_action_mapping&&mappingTitle>=0&&gameplayMode&&!forgeMonitor;
        // Never reinterpret an old transport-encoded weapon pulse as a named
        // action. A transition sample keeps the previous route until it drains.
        const bool directActions=semanticMode&&NativeVrActions_CanRoute(currentTitle,inputNow)&&
            !(pad.weaponButtons&0x3FFFFu);
        static thread_local bool previousDirectActions=false;
        if(previousDirectActions!=directActions)
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
        previousDirectActions=directActions;
        const bool physicalCrouchEnabled=g_config.physical_crouch;
        // The roomscale jump recenter also wants the game's own jump binding when the mapper is off (below).
        const bool jumpWitnessWanted=g_config.roomscale_movement&&g_config.roomscale_recenter_jump;
        // Rows with inputs made of the mod's own controls (the stick, the hand on the gun) act with remapping off too.
        const bool ownInputs=rowInputs&&!forgeMonitor&&
            vr_mapping::UsesControls(effectiveBindings,*rowInputs,vr_mapping::kOwnControls);
        const bool nativeReady=(semanticMode||physicalCrouchEnabled||ownInputs||jumpWitnessWanted)&&gameplayMode&&
            (Game_ReadVrActionBindings(nativeActions,inputNow)||directActions);
        const bool nativeMapping=semanticMode&&nativeReady;
        // The head height and the standing reference come from the OpenXR frame thread, in one frame a
        // runtime recenter does not move (VR_GetPhysicalCrouchSample): every input thread, title and level
        // shares that one reference, so none of them re-samples the player in whatever posture they are in.
        VrPhysicalCrouchSample crouchSample{};
        // Reach publishes RuntimeMode Gameplay in a seat too, so the shared mode gate alone let a
        // physical duck press crouch while seated. Require Reach's own on-foot, player-controlled proof
        // there -- the predicate direct drive and the roomscale camera already use.
        bool crouchOnFoot=true;
        uint32_t crouchOnFootWhy=0;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
        if(currentTitle==GameTitle::HaloReach)
        {crouchOnFootWhy=Game_ReachDirectDriveOnFootWhy();crouchOnFoot=crouchOnFootWhy==0;}
#endif
        // Halo 2 and Halo 4 publish Gameplay in a seat too (Halo 3, ODST and CE switch to Vehicle), so a
        // physical duck would press their crouch binding in a vehicle. Require each one's own on-foot state.
#if HALOMCCVR_HALO2_STEREO6DOF
        if(currentTitle==GameTitle::Halo2)
        {crouchOnFoot=Halo2Observer6Dof_OnFootFresh();crouchOnFootWhy=crouchOnFoot?0u:0x100u;}
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
        if(currentTitle==GameTitle::Halo4)
        {crouchOnFoot=Game_Halo4OnFoot();crouchOnFootWhy=crouchOnFoot?0u:0x200u;}
#endif
        const bool crouchTracking=physicalCrouchEnabled&&inputMode==RuntimeMode::Gameplay&&
            gameplayMode&&crouchOnFoot&&Game_IsHeadTracking()&&Game_IsPositionalTracking()&&
            VR_IsStereoEnabled()&&VR_RoomscaleTrackingFresh()&&
            VR_GetPhysicalCrouchSample(crouchSample);
        const float crouchHeadY=crouchSample.height;
        const uint64_t crouchEpoch=crouchSample.reference;
        const uint32_t crouchTransport=
            directActions?NativeVrActions_GestureBit(vr_mapping::Crouch):nativeActions[vr_mapping::Crouch];
        const uint32_t physicalCrouchButtons=g_physicalCrouchInput.Update(currentTitle,
            TitleAdapter_GetGeneration(currentTitle),crouchHeadY,crouchSample.baseline,crouchEpoch,inputNow,
            physicalCrouchEnabled,crouchTracking&&nativeReady,g_config.physical_crouch_depth_m,
            crouchTransport);
        PhysicalCrouchCamera_Publish(currentTitle,TitleAdapter_GetGeneration(currentTitle),
            crouchEpoch,inputNow,physicalCrouchEnabled&&crouchTracking&&nativeReady,
            physicalCrouchButtons!=0);
        // THE RIGHT STICK'S UP AND DOWN (vr_mapping::RightStickUp, RightStickDown, input_logic.h StickDirections): controls
        // for the rows, only on foot where the headset owns the view's pitch and never as Forge's Monitor, who has neither
        // (what they press means other things to it). The lens and the thumb-rest D-pad want the axis: there the stick
        // reads neither, and a toggled crouch stays. Leaving the feet ends every toggle (PollOptions::standUp). Every
        // thread that polls the pad shares the one state.
        const bool stickOnFoot=inputMode==RuntimeMode::Gameplay&&crouchOnFoot&&!inSeat&&!forgeMonitor&&Game_IsHeadTracking();
        uint32_t stickSources=0;
        {
            static std::mutex stickLock;
            static StickDirections stick;
            std::lock_guard<std::mutex> lock(stickLock);
            if(gameplayMode) stick.Step(pad.turnX,pad.turnY,stickOnFoot&&!VR_IsScopeActive()&&!pad.thumbrestDpad);
            stickSources=(stick.up?vr_mapping::Bit(vr_mapping::RightStickUp):0u)|
                (stick.down?vr_mapping::Bit(vr_mapping::RightStickDown):0u);
        }
        const uint32_t crouchButtons=physicalCrouchButtons;
        const uint32_t heldButtons=crouchButtons;
        if(crouchSample.reference)
            g_physicalCrouchRiseCapped.store(physicalCrouchEnabled&&crouchSample.riseCapped,
                std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if(physicalCrouchEnabled&&inputMode==RuntimeMode::Gameplay&&gameplayMode)
            PublishPhysicalCrouch(inputNow,currentTitle,crouchTracking,nativeReady,crouchOnFoot,crouchOnFootWhy,
                crouchSample,g_physicalCrouchInput.Gesture(),crouchButtons,crouchTransport,
                TitleAdapter_GetGeneration(currentTitle));
#else
        (void)crouchOnFootWhy;
#endif
        uint32_t actionSources=collectedSources|stickSources;
        actionSources&=~pauseConsumed;
        if(chord.consumeClicks) actionSources&=~(vr_mapping::Bit(vr_mapping::LeftClick)|vr_mapping::Bit(vr_mapping::RightClick));
        if(scopeAvailable) nativeActions[vr_mapping::Zoom]=0;
        if(wheelGesture) actionSources&=~(vr_mapping::Bit(vr_mapping::PrimaryGrip)|vr_mapping::Bit(vr_mapping::SupportGrip));
        const vr_mapping::Overrides emptyMappings{};
        auto mappingTransports=nativeActions;
        if(directActions) for(unsigned action=0;action<vr_mapping::Count;++action)
            mappingTransports[action]=NativeVrActions_GestureBit(static_cast<vr_mapping::Action>(action));
        // Remapping off, only the rows' inputs made of the mod's own controls act (PollOptions::ownOnly). The setting, not
        // the moment: a pause is not remapping off, and a toggled crouch stays through it.
        vr_mapping::PollOptions poll;
        poll.heldOk=heldOk;
        poll.forced=zoomPressedOut?1u<<vr_mapping::Zoom:0u;
        poll.absent=AbsentActions(currentTitle);
        poll.ownOnly=!g_config.vr_action_mapping;
        vr_mapping::MappedActions semanticActions;
        {
            std::lock_guard<std::mutex> lock(g_vrActionMapperLock);
            // Toggles end as the player leaves their feet (a seat, the Monitor, the headset losing the view), once: a
            // toggle set in a seat lasts there.
            static bool wasOnFoot=true;
            poll.standUp=gameplayMode&&wasOnFoot&&!stickOnFoot;
            if(gameplayMode) wasOnFoot=stickOnFoot;
            semanticActions=g_vrActionMapper.ApplyDetailed(actionSources,
                mappingTitle>=0?effectiveBindings:emptyMappings,mappingTransports,
                (uint64_t(TitleAdapter_GetGeneration(currentTitle))<<32)|(uint64_t(currentTitle)<<24)|pad.profileEpoch,
                nativeMapping||(ownInputs&&nativeReady),g_config.disable_flashlight_input||
                    (g_config.flashlight_suppress_on_two_hand&&pad.supportAimActive),nullptr,rowInputs,poll);
            g_zoomInputDown.store(semanticActions.Has(vr_mapping::Zoom)&&!zoomPressedOut,std::memory_order_relaxed);
        }
        // Each pull of the support trigger on foot, and what came of it (zoom_pull_audit_logic.h).
        if(g_hostIsProjectReclaimer)
        {
            NativeVrActions_NoteZoomInput(1,semanticActions.Has(vr_mapping::Zoom));
            NativeVrActions_NoteZoomInput(2,triggerDown);
        }
        {
            static std::mutex auditLock;
            static zoom_pull_audit::Audit audit;
            int level=-1;
            const bool levelKnown=Game_ReadZoomLevel(level);
            zoom_pull_audit::Report report{};
            bool due=false;
            {
                std::lock_guard<std::mutex> lock(auditLock);
                due=audit.Step(GetTickCount64(),triggerDown&&gameplayMode&&!menuOpen&&!inSeat&&!pad.dualWield,
                    handOnGun,pad.supportAimActive,pad.supportOnOneHandedGun,
                    semanticActions.Has(vr_mapping::Zoom)&&!zoomPressedOut,semanticActions.Has(vr_mapping::Grenade),
                    semanticActions.Has(vr_mapping::Equipment),levelKnown,level,report);
            }
            if(due)
                LOG("Support trigger pull (%llu ms): the hand %s the gun (two-handed %d, near a one-handed gun %d); "
                    "pressed %s%s%s%s; the game's zoom level %d -> %d%s",
                    static_cast<unsigned long long>(report.heldMs),report.handOnGun?"on":"off",
                    report.twoHanded?1:0,report.oneHandedGun?1:0,report.zoom?"zoom ":"",
                    report.grenade?"grenade ":"",report.equipment?"equipment ":"",
                    report.zoom||report.grenade||report.equipment?"":"nothing ",
                    report.levelKnownBefore?report.levelBefore:-9,report.levelKnownAfter?report.levelAfter:-9,
                    zoom_pull_audit::Audit::Unanswered(report)?" - ZOOM PRESSED, NOT ANSWERED":"");
        }
        if(scopeAvailable) semanticActions.actions&=~(1u<<vr_mapping::Zoom);
        // Direct routing withholds native transports, except Halo 2's context
        // actions, whose real button is their only complete transport (tap
        // reload / hold pick up or board). See vr_action_passthrough.h.
        // A seat: Halo 2 and Halo 4 publish Gameplay in one (crouchOnFoot is each one's own on-foot state).
        const auto routedActions=vr_mapping::RouteActions(currentTitle,directActions,
            semanticActions,nativeActions,
            inSeat||inputMode==RuntimeMode::Vehicle||inputMode==RuntimeMode::Turret||!crouchOnFoot,
            mappingTitle>=0?&effectiveBindings:nullptr,rowInputs);
        const uint32_t semanticButtons=routedActions.buttons;
        // THE EXTRA ROWS (vr_mapping::ExtraMapper): the game's actions no action row names, on controls of their own,
        // pressing the game's own button for each beside the rows, each in its context (a seat, on foot, anywhere in
        // play). One state for every thread that polls the pad.
        uint32_t seatButtons=0,extraPublished=0,extraServed=0;
        {
            const bool seated=inSeat||inputMode==RuntimeMode::Vehicle||inputMode==RuntimeMode::Turret||!crouchOnFoot;
            const unsigned contexts=nativeMapping&&mappingTitle>=0?
                (seated?vr_mapping::ContextBit(vr_mapping::ExtraContext::Seat):
                    vr_mapping::ContextBit(vr_mapping::ExtraContext::OnFoot))|
                vr_mapping::ContextBit(vr_mapping::ExtraContext::Anywhere):0u;
            vr_mapping::ExtraTransports seatTransports{};
            const bool seatLayoutKnown=mappingTitle>=0&&Game_ReadVrExtraBindings(seatTransports,inputNow);
            static std::mutex seatRowsLock;
            static vr_mapping::ExtraMapper seatRows;
            std::lock_guard<std::mutex> lock(seatRowsLock);
            // Where the bridge carries them (vr_mapping::ExtraBridgeOnly), the rows that share their game button with an
            // action on other controls press their own action alone, and no button.
            const bool extraDirect=directActions&&NativeVrActions_RoutesExtras(currentTitle)&&seatLayoutKnown&&
                gameLayoutKnown&&mappingTitle>=0;
            extraServed=extraDirect?vr_mapping::ExtraBridgeOnly(g_config.vr_extra_bindings[mappingTitle],seatTransports,
                gameLayout,effectiveBindings,rowInputs):0u;
            uint32_t extraPressed=0;
            // Automatic rows press their own button from the VR control at its place (EffectiveExtraBindings); the
            // bridge's share above is decided on the rows as set, so those rows always press the real button.
            seatButtons=seatRows.Apply(actionSources,contexts,vr_mapping::EffectiveExtraBindings(
                mappingTitle>=0?g_config.vr_extra_bindings[mappingTitle]:vr_mapping::kExtraDefaults,seatTransports,
                seatLayoutKnown),seatTransports,~0u,&extraPressed,extraServed);
            extraPublished=extraPressed&extraServed;
            // What the extra rows press in this game, and the game's own button behind each: once each time it changes.
            static uint64_t shownSeatRows=0;
            uint64_t shown=uint64_t(controlsTitle)<<56;
            char input[32];
            for(unsigned action=0;mappingTitle>=0&&action<vr_mapping::ExtraCount;++action)
            {
                shown=(shown^seatTransports[action])*1099511628211ull;
                for(const auto& seat:g_config.vr_extra_bindings[mappingTitle][action])
                    shown=(shown^(uint64_t(seat.press)|(uint64_t(seat.held[0])<<8)|(uint64_t(seat.held[1])<<16)|
                        (uint64_t(seat.mode)<<24)))*1099511628211ull;
            }
            if(nativeMapping&&seatLayoutKnown&&shown!=shownSeatRows)
            {
                shownSeatRows=shown;
                std::string rows;
                char native[16];
                for(unsigned action=0;action<vr_mapping::ExtraCount;++action)
                {
                    // Only the rows this game has.
                    if(vr_mapping::NativeExtraAction(currentTitle,static_cast<vr_mapping::ExtraAction>(action))==
                       vr_mapping::kNoAction) continue;
                    sprintf_s(native," (0x%X)",seatTransports[action]);
                    rows+=std::string(rows.empty()?"":", ")+vr_mapping::kExtraKeys[action]+" =";
                    // Each input as the file has it (vr_mapping::FormatInput), the empty ones left out.
                    for(const auto& seat:g_config.vr_extra_bindings[mappingTitle][action])
                        if(seat.press!=vr_mapping::Unbound)
                        {
                            vr_mapping::FormatInput(input,seat.held,seat.press,seat.mode);
                            rows+=std::string(" ")+input;
                        }
                    rows+=native;
                }
                LOG("VR Mappings %s, extra rows: %s",weapon_interaction::kTitleNames[mappingTitle],rows.c_str());
            }
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        {
            static uint32_t shownSources=0,shownActions=0,shownButtons=0,shownPublished=0;
            if(semanticMode&&(actionSources!=shownSources||semanticActions.actions!=shownActions||
                semanticButtons!=shownButtons||routedActions.published!=shownPublished))
            {
                shownSources=actionSources;shownActions=semanticActions.actions;
                shownButtons=semanticButtons;shownPublished=routedActions.published;
                LOG("VR Mappings (dev): sources 0x%X -> actions 0x%X -> buttons 0x%X, bridge 0x%X",
                    actionSources,semanticActions.actions,semanticButtons,routedActions.published);
            }
        }
#endif
        const bool xPressed=pad.x;
        const float leftTrigger=pad.trigL;

        WORD btn = state->Gamepad.wButtons;
        // The physical pad's Y and B, while the Pause row holds them.
        if (pauseConsumed & vr_mapping::Bit(vr_mapping::Y)) btn &= ~XINPUT_GAMEPAD_Y;
        if (pauseConsumed & vr_mapping::Bit(vr_mapping::B)) btn &= ~XINPUT_GAMEPAD_B;
        if (scopeAvailable)
            btn &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
#if HALOMCCVR_DEV_DIAGNOSTICS
        const WORD witnessBase = btn;
#endif
        if (!semanticMode&&pad.a) btn |= XINPUT_GAMEPAD_A;
        if (!semanticMode&&pad.b && !(pauseConsumed&vr_mapping::Bit(vr_mapping::B))) btn |= XINPUT_GAMEPAD_B;
        if (!semanticMode&&xPressed) btn |= XINPUT_GAMEPAD_X;
        if (!semanticMode&&pad.y && !(pauseConsumed&vr_mapping::Bit(vr_mapping::Y))) btn |= XINPUT_GAMEPAD_Y;
        if (!semanticMode&&pad.clickL && !chord.consumeClicks) btn |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (!semanticMode&&pad.clickR && !chord.consumeClicks && !scopeAvailable)
            btn |= XINPUT_GAMEPAD_RIGHT_THUMB;
        btn|=static_cast<WORD>(semanticButtons|seatButtons);
#if HALOMCCVR_DEV_DIAGNOSTICS
        g_witness.mapper = static_cast<uint32_t>(btn & ~witnessBase) |
            (semanticButtons & seat_witness::kTransportMask);
#endif
        btn|=static_cast<WORD>(heldButtons);
        static bool previousMenu = false;
        const bool menuEdge = pad.menu && !previousMenu;
        if (menuEdge)
        {
            g_menuPressSerial.fetch_add(1, std::memory_order_relaxed);
            if (Game_IsCameraOnlyBringup())
            {
                // OpenXR exposes the reserved Menu action as a short edge. ODST
                // polls XInput on a different cadence and missed that edge in
                // the headset test, so retain a normal Start press long enough
                // to cross its polling boundary. This branch is private-ODST
                // only; Halo 3 and normal OFF builds keep their existing path.
                g_startPulseUntilMs.store(inputNow + 350);
                LOG("ODST input: Menu/Start latched for native polling");
            }
            if (PausePresentationInputAllowed(sharedGameplayInput) &&
                !Game_HasAuthoritativePauseState())
                VR_RequestPausePresentation(!VR_IsPausePresentationTarget());
        }
        previousMenu = pad.menu;
        if (pad.menu || inputNow < g_startPulseUntilMs.load())
        {
            btn |= XINPUT_GAMEPAD_START;
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.start = XINPUT_GAMEPAD_START;
#endif
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        const WORD witnessBeforeGrips = btn;
#endif
        if (!wheelGesture&&!semanticMode)
        {
            if (pad.gripL > 0.6f && !pad.weaponConsumeSupport) btn |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (pad.gripR > 0.6f && !pad.weaponConsumePrimary) btn |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        g_witness.mapper |= static_cast<uint32_t>(btn & ~witnessBeforeGrips);
#endif
        const bool weaponGesture=pad.weaponConsumeSupport||pad.weaponConsumePrimary||pad.weaponButtons;
        const uint32_t gestureMelee=weaponGesture?0:Game_GestureMeleeInput(inputNow);
        const uint32_t blockedWeaponActions=manual_weapon_input::Blocked(
            gameplayMode&&inputMode==RuntimeMode::Gameplay&&g_config.gesture_only_weapon_input,
            g_config.manual_reload,g_config.weapon_holsters)&
            // A dual-wield shake is pressing a reload binding: let Reload through for its 120 ms
            // (Halo 3's left-reload edge is otherwise cleared under a blocked Reload).
            ~(pad.weaponDualReloadPulse?(1u<<vr_mapping::Reload):0u);
        const uint32_t gestureActions=((heldButtons|gestureMelee)>>18)|
            manual_weapon_input::Gestures(pad.weaponButtons,pad.weaponReloadBinding,pad.weaponSwitchBinding);
        if(semanticMode||blockedWeaponActions||(currentTitle==GameTitle::Halo3&&g_config.two_handed_aim)) NativeVrActions_Publish(state,currentTitle,inputNow,
            manual_weapon_input::Publish(semanticMode?routedActions.published:0u,gestureActions,blockedWeaponActions),
            directActions,blockedWeaponActions,extraPublished,extraServed);
        btn |= static_cast<WORD>(gestureMelee & 0xFFFF);
        btn |= static_cast<WORD>(pad.weaponButtons & 0xFFFF);
        state->Gamepad.wButtons = btn;
        NoteFedButtons(btn);
#if HALOMCCVR_DEV_DIAGNOSTICS
        {
            using namespace seat_witness;
            auto& w = g_witness;
            w.sources = actionSources;
            w.crouch = crouchButtons & kTransportMask;
            w.gesture = gestureMelee & kTransportMask;
            if (semanticMode)
            {
                w.actions = semanticActions.actions | ((heldButtons|gestureMelee|pad.weaponButtons)>>18);
                w.flags |= FlagSemantic | FlagPublished;
            }
            if (directActions) w.flags |= FlagDirect;
            if (wheelGesture) w.flags |= FlagWheel;
            if (chord.consumeClicks || pauseConsumed) w.flags |= FlagChord;
            if (gameplayMode) w.flags |= FlagGameplay;
        }
#endif

        const BYTE tl = semanticMode?((semanticButtons&(1u<<16))?255:0):(BYTE)(leftTrigger*255.0f);
        const BYTE tr = semanticMode?((semanticButtons&(1u<<17))?255:0):(BYTE)(pad.trigR*255.0f);
        if (tl > state->Gamepad.bLeftTrigger) state->Gamepad.bLeftTrigger = tl;
        if (tr > state->Gamepad.bRightTrigger) state->Gamepad.bRightTrigger = tr;
        if ((heldButtons|seatButtons|(semanticMode?0u:semanticButtons)) & (1u<<16)) state->Gamepad.bLeftTrigger = 255;
        if ((heldButtons|seatButtons|(semanticMode?0u:semanticButtons)) & (1u<<17)) state->Gamepad.bRightTrigger = 255;
        if (gestureMelee & (1u<<16)) state->Gamepad.bLeftTrigger = 255;
        if (gestureMelee & (1u<<17)) state->Gamepad.bRightTrigger = 255;
        if (pad.weaponButtons & (1u<<16)) state->Gamepad.bLeftTrigger = 255;
        if (pad.weaponButtons & (1u<<17)) state->Gamepad.bRightTrigger = 255;
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (!semanticMode)
        {
            if (tl > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) g_witness.mapper |= seat_witness::kTransportLT;
            if (tr > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) g_witness.mapper |= seat_witness::kTransportRT;
        }
#endif

        // The optional thumb-rest gesture uses the physical RIGHT stick. Its
        // axes were consumed at XR publication, including every title's turn
        // and scope consumers. Preserve head-gesture clicks and left movement.
        if (pad.thumbrestDpad&&!semanticMode)
        {
            btn |= DpadDirectionButtons(pad.dpadX, pad.dpadY);
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.dpad |= DpadDirectionButtons(pad.dpadX, pad.dpadY);
#endif
            state->Gamepad.wButtons = btn;
            NoteFedButtons(btn);
        }

        const bool physicalMove = std::abs(int(state->Gamepad.sThumbLX)) > 7849 ||
            std::abs(int(state->Gamepad.sThumbLY)) > 7849;
        float locomotionX=pad.moveX,locomotionY=pad.moveY;
        const bool running=!dpadMode&&!Menu_IsOpen()&&!VR_IsPausePresentation()&&
            !VR_IsPausePresentationTarget()&&!VR_IsCutsceneTheaterActive()&&
            Game_MoveStickIsLocomotion()&&PhysicalRunning_Move(locomotionX,locomotionY);
        Roomscale_Input(RoomscaleGameplayEligible(TitleAdapter_GetActiveTitle(),
            TitleAdapter_GetRuntimeMode()) && !dpadMode && !physicalMove &&
            Game_IsHeadTracking() && Game_IsPositionalTracking() &&
            VR_IsStereoEnabled() && !VR_IsPausePresentation() &&
            !VR_IsPausePresentationTarget() && !VR_IsCutsceneTheaterActive(),
            locomotionX, locomotionY);
        // The jump the game is being fed on this poll, for the roomscale view recenter's takeoff witness
        // (it still has to see the character's eye rise). The named action while the mapper routes; else
        // the transport MCC's own bindings give jump when they have been read; else MCC's default, A.
        {
            bool jumpHeld;
            if (nativeMapping) jumpHeld=(semanticActions.actions&(1u<<vr_mapping::Jump))!=0;
            else
            {
                const uint32_t transport=nativeReady ? nativeActions[vr_mapping::Jump] : 0u;
                const uint32_t fed=uint32_t(btn)|
                    (state->Gamepad.bLeftTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1u<<16 : 0u)|
                    (state->Gamepad.bRightTrigger>XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1u<<17 : 0u);
                jumpHeld=(fed&(transport ? transport : uint32_t(XINPUT_GAMEPAD_A)))!=0;
            }
            Roomscale_JumpInput(jumpHeld && !dpadMode);
        }

#if HALOMCCVR_DEV_DIAGNOSTICS
        const WORD witnessBeforeDpad = btn;
#endif
        if (dpadMode)
        {
            if (!semanticMode&&pad.moveY > 0.5f) btn |= XINPUT_GAMEPAD_DPAD_UP;
            if (!semanticMode&&pad.moveY < -0.5f) btn |= XINPUT_GAMEPAD_DPAD_DOWN;
            if (!semanticMode&&pad.moveX > 0.5f) btn |= XINPUT_GAMEPAD_DPAD_RIGHT;
            if (!semanticMode&&pad.moveX < -0.5f) btn |= XINPUT_GAMEPAD_DPAD_LEFT;
            // Remapping, the D-pad still reaches the game as its own for what no row can name (Halo 4 Forge's
            // monitor mode); only a direction a row gives to an action the game has elsewhere stays with that row.
            if (semanticMode)
            {
                WORD raw = 0;
                if (pad.moveY > 0.5f) raw |= XINPUT_GAMEPAD_DPAD_UP;
                if (pad.moveY < -0.5f) raw |= XINPUT_GAMEPAD_DPAD_DOWN;
                if (pad.moveX > 0.5f) raw |= XINPUT_GAMEPAD_DPAD_RIGHT;
                if (pad.moveX < -0.5f) raw |= XINPUT_GAMEPAD_DPAD_LEFT;
                for (unsigned action = 0; raw && mappingTitle >= 0 && gameLayoutKnown && action < vr_mapping::Count; ++action)
                {
                    // Every control of the row, its own and its further ones.
                    const uint32_t controls = vr_mapping::ControlBits(static_cast<vr_mapping::Action>(action),
                        effectiveBindings, rowInputs);
                    const WORD directions =
                        ((controls & vr_mapping::Bit(vr_mapping::DpadUp)) ? XINPUT_GAMEPAD_DPAD_UP : 0) |
                        ((controls & vr_mapping::Bit(vr_mapping::DpadDown)) ? XINPUT_GAMEPAD_DPAD_DOWN : 0) |
                        ((controls & vr_mapping::Bit(vr_mapping::DpadLeft)) ? XINPUT_GAMEPAD_DPAD_LEFT : 0) |
                        ((controls & vr_mapping::Bit(vr_mapping::DpadRight)) ? XINPUT_GAMEPAD_DPAD_RIGHT : 0);
                    raw &= ~(directions & ~static_cast<WORD>(gameLayout[action]));
                }
                btn |= raw;
            }
            // Left stick CLICK becomes the controller's left centre button
            // (Back/View) while the D-pad gesture is held. ODST puts its map
            // and objectives screen behind that button and VR players had no
            // way to reach it. This is deliberately scoped to the gesture: the
            // stick is already acting as a D-pad here, so its click has no
            // other meaning, and normal play keeps L3 untouched.
            const bool halo2 = TitleAdapter_GetActiveTitle() == GameTitle::Halo2;
            const bool graphicsSwitch = halo2 || TitleAdapter_GetActiveTitle() == GameTitle::HaloCE;
            if (pad.clickL && !chord.consumeClicks)
            {
                btn &= ~XINPUT_GAMEPAD_LEFT_THUMB;
                if (!graphicsSwitch)
                {
                    // ODST/Reach/Halo 3/Halo 4: the click is the Back button.
                    btn |= XINPUT_GAMEPAD_BACK;
                }
                else
                {
                    // C-H2-28: in Halo 2 the click IS the Back button, as in
                    // ODST - one Back press per click (the C-H2-27 hold was
                    // not what the player asked for). Back is the engine's
                    // Classic <-> Anniversary switch; the renderer switch
                    // guard honours it because the mod fed it.
                    const uint64_t now = GetTickCount64();
                    if (!g_halo2GestureClickSinceMs.load(std::memory_order_relaxed))
                    {
                        g_halo2GestureClickSinceMs.store(now, std::memory_order_relaxed);
                        g_halo2GestureClickSent.store(false, std::memory_order_relaxed);
                        LOG("M3: %s - head-gesture stick click: Back pressed "
                            "(the Classic/Anniversary switch)", halo2 ? "Halo 2" : "Halo CE");
                    }
                    if (now - g_halo2GestureClickSinceMs.load(std::memory_order_relaxed) <
                        kHalo2GestureClickHoldMs)
                        btn |= XINPUT_GAMEPAD_BACK;
                }
            }
            else if (graphicsSwitch)
            {
                g_halo2GestureClickSinceMs.store(0, std::memory_order_relaxed);
                g_halo2GestureClickSent.store(false, std::memory_order_relaxed);
            }
            state->Gamepad.wButtons = btn;
        NoteFedButtons(btn);
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.dpad |= static_cast<uint32_t>(btn & ~witnessBeforeDpad);
#endif
            // No walking while navigating.
            state->Gamepad.sThumbLX = 0;
            state->Gamepad.sThumbLY = 0;
            static std::atomic<bool> gestureLogged{false};
            // Name the hand that did it: dpad_hand (default 0, the LEFT controller; CE always left). This
            // line said "right" whatever the setting, which sent one diagnosis to the wrong controller.
            if (!gestureLogged.exchange(true))
                LOG("M3: D-pad gesture active (%s controller held at head)",
                    TitleAdapter_GetActiveTitle() == GameTitle::HaloCE || g_config.dpad_hand == 0 ? "left" : "right");
        }
        else if (Game_MoveStickIsLocomotion())
        {
            // Gameplay locomotion (UNCHANGED, headset-confirmed): the left stick
            // walks the player, rotated head-relative so forward = gaze, and
            // each axis floored past MCC's inner deadzone so small corrections
            // still move. This path runs only while the game is actually using
            // the stick to move the character.
            float mx = locomotionX, my = locomotionY;
            const bool roomscale = !running&&Roomscale_Move(mx, my);
            Game_MapMoveStick(mx, my);
            if (((roomscale||running) && mx * mx + my * my > 1e-6f) ||
                mx * mx + my * my > 0.02f)
            {
                RadialMoveStick(mx,my,state->Gamepad.sThumbLX,state->Gamepad.sThumbLY);
            }
        }
        else
        {
            // In-game menus (pause/settings) and any other non-locomotion state:
            // the game reads the left stick as MENU NAVIGATION, not movement.
            // Pass it through like a plain gamepad — no head-relative rotation
            // and no per-axis deadzone floor — so a near-vertical push stays
            // vertical and MCC's own menu deadzone rejects the minor axis.
            // Fixes GitHub #9 (up/down registering as left/right). Character
            // movement is untouched; that path is the locomotion branch above.
            state->Gamepad.sThumbLX = ToRawMenuStick(pad.moveX);
            state->Gamepad.sThumbLY = ToRawMenuStick(pad.moveY);
        }

        // Right stick: hand-steered aim when active; otherwise pass the raw
        // stick through (classic stick aiming, and it does nothing in menus).
        // While VR aim is active the turn stick is consumed by the snap/smooth
        // logic in game.cpp instead.
        float rx = 0, ry = 0;
        bool haveStick;
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
#if HALOMCCVR_DEV_DIAGNOSTICS
        if ((g_config.reach_direct_drive == 1 || Game_ReachDdTestOwnsAim()) &&
            TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
        {
            // Dev research modes: mode 1 (swing-and-hold) and the mode-4 persistence test (while it
            // runs) own aim via an engine-thread state write; keep the stick neutral so the two do not
            // fight. Mode 2 (live offset) deliberately does NOT suppress -- you aim normally and we
            // watch for shots landing off the reticle.
            rx = 0; ry = 0; haveStick = true;
        }
        else
#endif
        // The per-game aim provider owns Reach's aim stick when aim_provider is on (experimental), and always
        // for direct drive (mode 3, on by default through aim_direct_drive), whose WriteState rung only the
        // provider actuates (it then returns a neutral stick, so the state write is the only driver). Its
        // StickLoop rung IS Game_ComputeAimStick, so whatever it does not take over aims exactly as the
        // shipped path. With both off the call is left untouched.
        if ((g_config.aim_provider || g_config.reach_direct_drive == 3) &&
            TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
            haveStick = ReachAimProvider_ProduceStick(rx, ry);
        else
#endif
            haveStick = Game_ComputeAimStick(rx, ry);
        // THE THIRD-PERSON BOOM (vehicle_boom_logic.h): in a Halo 3 seat whose camera stands where the game's own
        // third-person camera does, the right stick's Y raises and lowers that camera -- only where it aims nothing.
        // On this path the game's Y is the aim servo's (the hand's pitch, or nothing while the wheel steers), never
        // the stick's, except in a stick-aimed seat, where the boom holds; the scope's magnification keeps the axis
        // while the scope is open. Off this path the stick's Y goes to the game as it always has, and the boom holds.
        // Halo CE aims a seat with the hand through its own control packet and its look stick turns the view's yaw
        // only, so where the VR look owns the stick (the game gets neither axis) its Y aims nothing there either.
        const bool boomAxisFree =
            haveStick || (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE && Game_VrOwnsLookStick());
        Game_VehicleBoomStick(boomAxisFree && !VR_IsScopeActive() && Game_VehicleBoomTakesStick() ? pad.turnY : 0.0f);
        if (haveStick)
        {
            state->Gamepad.sThumbRX = ToRawStick(rx);
            state->Gamepad.sThumbRY = ToRawStick(ry);
            if (!g_overrideLogged.exchange(true))
                LOG("M3: VR aim override active (right stick steered by the controller)");
        }
        else if (Game_VrOwnsLookStick())
        {
            // Match Halo 3 camera ownership in every armed camera-core title:
            // ApplyVrTurn consumes turnX directly from the OpenXR pad, while
            // the tracked HMD exclusively owns pitch. Do not also feed either
            // axis into the stock camera/aim integrator.
            state->Gamepad.sThumbRX = 0;
            state->Gamepad.sThumbRY = 0;
        }
        else if (Game_Halo2OwnsLookPitch())
        {
            // Snap mode owns RX until the native body reaches its discrete
            // target. Smooth mode retains the accepted continuous native turn.
            float snapRx=0.0f;
            if (Game_ComputeHalo2SnapStick(snapRx))
                state->Gamepad.sThumbRX=ToRawStick(snapRx);
            else if (fabsf(pad.turnX) > 0.15f)
                state->Gamepad.sThumbRX = ToRawStick(pad.turnX);
            float servoRy = 0.0f;
            state->Gamepad.sThumbRY =
                Game_ComputeHalo2PitchStick(servoRy) ? ToRawStick(servoRy) : 0;
        }
        else if (Game_Halo4OwnsLookPitch())
        {
            // Halo 4 (C-H4-9): the headset owns pitch, the engine keeps yaw.
            // The vertical axis never reaches the game again - a closed loop
            // drives the engine's own look pitch onto the head's instead, so
            // the shot line follows the view without artificial pitch tilting
            // the world away from the player's real horizon. Horizontal is
            // untouched: Halo 4 has no VR turn or aim loop yet, so the engine
            // must keep turning the body, the aim and the view together.
            if (fabsf(pad.turnX) > 0.15f)
                state->Gamepad.sThumbRX = ToRawStick(pad.turnX);
            float servoRy = 0.0f;
            state->Gamepad.sThumbRY =
                Game_ComputeHalo4PitchStick(servoRy) ? ToRawStick(servoRy) : 0;
        }
        else
        {
            const bool scopeActive = VR_IsScopeActive();
            // Scope magnification owns the vertical axis while the scope is
            // open. Horizontal snap/smooth turning remains available.
            if (scopeActive)
                state->Gamepad.sThumbRY = 0;
            if (fabsf(pad.turnX) > 0.15f ||
                (!scopeActive && fabsf(pad.turnY) > 0.15f))
            {
                state->Gamepad.sThumbRX = ToRawStick(pad.turnX);
                state->Gamepad.sThumbRY = scopeActive
                    ? 0 : ToRawStick(pad.turnY);
            }
        }
    }

    std::atomic<unsigned> g_diagReads{0}, g_diagPadValid{0}, g_diagMerged{0};
    // Slot-0 polls answered while shared input was gated OFF but the virtual pad
    // was still held connected+idle (no physical pad). A non-zero value around a
    // title transition is the fingerprint of the former dead-menu window: the
    // hook used to return NOT_CONNECTED there and MCC latched the disconnect.
    std::atomic<unsigned> g_diagGateIdle{0};

    // Heartbeat: proves whether the game is reading through our hook at all,
    // and whether controller data was valid when it did.
    void DiagTick()
    {
        static std::atomic<DWORD> lastLog{0};
        const DWORD now = GetTickCount();
        DWORD last = lastLog.load();
        if (now - last >= 10000 && lastLog.compare_exchange_strong(last, now))
            LOG("M3 DIAG: xinput reads=%u padValid=%u merged=%u gateIdle=%u (last 10s window cumulative)",
                g_diagReads.load(), g_diagPadValid.load(), g_diagMerged.load(),
                g_diagGateIdle.load());
    }

    // Project Reclaimer: how its game polls the pad, for Input_ReportGamePolls. [0] slot 0 (the pad the mod presents),
    // [1] the polls of it that carried the controllers' input, [2] any other slot.
    std::atomic<uint32_t> g_gamePolls[3]{};

    DWORD ProcessGetState(DWORD r, DWORD user, XINPUT_STATE* state)
    {
        g_gamePolls[user == 0 ? 0 : 2].fetch_add(1, std::memory_order_relaxed);
        if (user != 0 || !state)
            return r;
        struct GamePollScope {
            const XINPUT_STATE* state;
            ~GamePollScope() noexcept {
                const XINPUT_GAMEPAD& pad = state->Gamepad;
                if (pad.wButtons || pad.bLeftTrigger || pad.bRightTrigger || pad.sThumbLX || pad.sThumbLY ||
                    pad.sThumbRX || pad.sThumbRY)
                    g_gamePolls[1].fetch_add(1, std::memory_order_relaxed);
            }
        } gamePollScope{state};
        NativeVrActions_BeginPoll();
        struct NativeActionPollScope {
            ~NativeActionPollScope() noexcept {NativeVrActions_EndPoll();}
        } nativeActionPollScope;
#if HALOMCCVR_DEV_DIAGNOSTICS
        // SEATEXIT witness: declared BEFORE the flashlight filter so it is destroyed AFTER it and records
        // exactly the pad the game receives, on every return path.
        g_witness = {};
        g_witness.ms = SeatWitness_NowMs();
        g_witness.route = seat_witness::RouteGated;
        struct SeatWitnessCommitScope {
            XINPUT_STATE* state;
            ~SeatWitnessCommitScope() noexcept {CommitSeatWitness(*state);}
        } seatWitnessCommit{state};
#endif
        // Scope exit covers both tracked-controller and physical-pad-only
        // branches, after all gesture pulses and pad merging have completed.
        struct FlashlightFilterScope
        {
            XINPUT_STATE* state;
            ~FlashlightFilterScope() noexcept {
                static thread_local flashlight_input::Filter filter;
                const auto title=TitleAdapter_GetActiveTitle();
                const int index=weapon_interaction::TitleIndex(title);
                const bool gameplay=index>=0&&Game_AllowsSharedGameplayFeatures()&&
                    !Menu_IsOpen()&&!VR_IsPausePresentation()&&!VR_IsPausePresentationTarget()&&
                    !VR_IsCutsceneTheaterActive();
                vr_mapping::Transports native{};
                const bool semantic=g_config.vr_action_mapping&&Game_ReadVrActionBindings(native,GetTickCount64());
                filter.Apply(state->Gamepad,g_config.disable_flashlight_input&&!semantic,gameplay,
                    static_cast<uint32_t>(title),index>=0?g_config.flashlight_button[index]:-1);
            }
        } flashlightFilter{state};
        // Connection presence is INDEPENDENT of the shared-input gate. With no
        // physical gamepad the mod owns slot 0 and must always present it
        // connected and idle -- even while shared input is gated off (e.g. the
        // brief window mid ODST title-exit teardown). Returning NOT_CONNECTED
        // there hands MCC a disconnect edge it latches, so the menu stays dead
        // after the gate reopens (the post-Save&Quit dead controller, and the
        // cross-title input drop when switching between Halo games). A real
        // physical pad (r == ERROR_SUCCESS) passes through untouched.
        const bool ownVirtualSlot = (r != ERROR_SUCCESS);
        if (ownVirtualSlot)
        {
            *state = {};
            r = ERROR_SUCCESS;
        }
        // DEV BUILD ONLY (dev_synthetic_pad): a synthetic controller for automated tests, merged here the
        // way a physical pad's state arrives, before any gate, so it reaches menus as well as gameplay. A
        // player build compiles this to nothing.
        DevPad_Apply(state->Gamepad);
#if HALOMCCVR_DEV_DIAGNOSTICS
        g_witness.physical = WitnessTransport(state->Gamepad);
#endif
        // Controller admission is separate from shared gameplay ownership.
        // Private ODST camera-only and Reach controller-only stages may expose
        // ordinary gamepad input while motion aim and every title-runtime
        // gameplay transform stay blocked.
        // Gate only decides whether to MERGE VR motion, not whether the pad
        // exists: hold the connection above and skip the merge when gated off.
        if (!Game_AllowsSharedControllerInput())
        {
            SuspendActionMapper();
            SuspendScopeInput();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            if constexpr (kEnableRetiredInputDiagnostics)
            {
                if (ownVirtualSlot)
                    g_diagGateIdle.fetch_add(1);
            }
            return r;
        }
        if constexpr (kEnableRetiredInputDiagnostics)
        {
            g_diagReads.fetch_add(1);
            DiagTick();
        }
        // E-H2-13: in Halo 2 the pad's Back/View button is MCC's instant
        // Classic <-> Anniversary renderer switch. A physical pad (Steam
        // Controller) reaches the game through this same hook, so unless the
        // player opted in, that button never reaches Halo 2.
        if (!ownVirtualSlot)
            NotePhysicalButtons(state->Gamepad.wButtons);
        if (!ownVirtualSlot && !g_config.halo2_gamepad_graphics_switch &&
            (state->Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0 &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_BACK;
            static std::atomic<bool> loggedBackSwallow{false};
            if (!loggedBackSwallow.exchange(true))
                LOG("M3: Halo 2 - the gamepad's Back/View press was swallowed "
                    "(it is the Classic/Anniversary graphics switch); set "
                    "halo2_gamepad_graphics_switch = 1 to pass it through");
        }
        VrPadState pad;
        VR_GetPadState(pad);
        if (!pad.valid)
        {
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_witness.route = seat_witness::RouteNoPad;
            WitnessPad(pad);
#endif
            SuspendActionMapper();
            SuspendScopeInput();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            // No VR controllers: the physical pad still gets the Y+B pause
            // chord and the Start pulse it produces.
            if (!ownVirtualSlot && PauseUsesYB() &&
                Game_AllowsPauseToggleInput())
            {
                const uint64_t now = GetTickCount64();
                const WORD phys = state->Gamepad.wButtons;
                const MenuChordResult chord = g_pauseChord.Update(
                    now, (phys & XINPUT_GAMEPAD_Y) != 0,
                    (phys & XINPUT_GAMEPAD_B) != 0);
                if (chord.toggled)
                    Input_RequestPauseToggle();
                if (chord.consumeClicks)
                    state->Gamepad.wButtons &=
                        ~(XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_B);
                if (now < g_startPulseUntilMs.load())
                    state->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
            }
            if (Menu_IsOpen())
            {
                state->Gamepad = {};
                static std::atomic<DWORD> menuSeq{1};
                state->dwPacketNumber += menuSeq.fetch_add(1);
            }
            return r;
        }
        if constexpr (kEnableRetiredInputDiagnostics)
            g_diagPadValid.fetch_add(1);
        // Keep the packet number monotonically rising so MCC notices changes.
        static std::atomic<DWORD> seq{1};
        state->dwPacketNumber += seq.fetch_add(1);
        MergeVrPad(state);
        if constexpr (kEnableRetiredInputDiagnostics)
            g_diagMerged.fetch_add(1);
        return r;
    }

    thread_local unsigned g_getStateDepth = 0;

    template <int Slot>
    DWORD WINAPI GetStateHook(DWORD user, XINPUT_STATE* state)
    {
        InputPollMergeScope poll(g_getStateDepth);
        const DWORD result = g_origGetState[Slot](user, state);
        return poll.IsOutermost() ? ProcessGetState(result, user, state) : result;
    }

    DWORD(WINAPI* const g_hooks[6])(DWORD, XINPUT_STATE*) = {
        &GetStateHook<0>, &GetStateHook<1>, &GetStateHook<2>,
        &GetStateHook<3>, &GetStateHook<4>, &GetStateHook<5>};

    DWORD ProcessGetCaps(DWORD r, DWORD user, XINPUT_CAPABILITIES* caps)
    {
        if (user != 0 || !caps || r == ERROR_SUCCESS)
            return r;
        // Fabricate a standard wired gamepad UNCONDITIONALLY: MCC enumerates
        // controllers once at startup, often seconds before SteamVR activates
        // our actions — gating this on live VR data made the whole input
        // system a startup race. An idle fabricated pad is harmless. This must
        // stay independent of Game_AllowsSharedControllerInput(): a gate-closed
        // NOT_CONNECTED here during a title transition makes MCC drop the pad
        // for good (dead menu after Save & Quit, cross-title input loss).
        *caps = {};
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.bLeftTrigger = 0xFF;
        caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = 0x7FFF;
        caps->Gamepad.sThumbLY = 0x7FFF;
        caps->Gamepad.sThumbRX = 0x7FFF;
        caps->Gamepad.sThumbRY = 0x7FFF;
        caps->Vibration.wLeftMotorSpeed = 0xFFFF;
        caps->Vibration.wRightMotorSpeed = 0xFFFF;
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LOG("M3: reporting virtual gamepad as connected (no physical pad found)");
        return ERROR_SUCCESS;
    }

    template <int Slot>
    DWORD WINAPI GetCapsHook(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
    {
        return ProcessGetCaps(g_origGetCaps[Slot](user, flags, caps), user, caps);
    }

    DWORD(WINAPI* const g_capsHooks[3])(DWORD, DWORD, XINPUT_CAPABILITIES*) = {
        &GetCapsHook<0>, &GetCapsHook<1>, &GetCapsHook<2>};

    DWORD ProcessSetState(DWORD result, DWORD user, XINPUT_VIBRATION* vibration)
    {
        if (user != 0 || !vibration)
            return result;
        const DWORD connectedResult = static_cast<DWORD>(
            NormalizeVirtualXInputSetStateResult(result, user, true));
        if (!Game_HasTitleCapability(TitleCapability_Haptics))
        {
            // A title/arm transition can close the capability gate before the
            // game's zero-motor update arrives. Clear our retained request on
            // every gated call so an earlier rumble cannot resume when the
            // next title becomes armed. Capability policy suppresses only the
            // effect: slot 0 remains the same connected virtual controller
            // exposed by GetState/GetCapabilities.
            VR_SetGameHaptics(0.0f);
            return connectedResult;
        }
        VR_SetGameHaptics(BlendXInputMotors(
            vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed));
        return connectedResult;
    }

    template <int Slot>
    DWORD WINAPI SetStateHook(DWORD user, XINPUT_VIBRATION* vibration)
    {
        return ProcessSetState(g_origSetState[Slot](user, vibration), user, vibration);
    }

    DWORD(WINAPI* const g_setStateHooks[3])(DWORD, XINPUT_VIBRATION*) = {
        &SetStateHook<0>, &SetStateHook<1>, &SetStateHook<2>};

    // Steam Input redirects MCC's calls at the IMPORT TABLE, so they jump to
    // Steam's handler without ever executing the DLL export we detour — the
    // reason controls kept working or dying depending on session timing. These
    // shims are written INTO the game's import table, wrapping whatever was
    // there (Steam's handler or the real export), and the claim is re-asserted
    // periodically so a later Steam patch can't take the slot back.
    XInputGetStateFn g_iatPrevGetState = nullptr;
    XInputGetCapsFn g_iatPrevGetCaps = nullptr;
    XInputSetStateFn g_iatPrevSetState = nullptr;

    DWORD WINAPI IatGetStateShim(DWORD user, XINPUT_STATE* state)
    {
        InputPollMergeScope poll(g_getStateDepth);
        const DWORD r = g_iatPrevGetState ? g_iatPrevGetState(user, state)
                                          : ERROR_DEVICE_NOT_CONNECTED;
        return poll.IsOutermost() ? ProcessGetState(r, user, state) : r;
    }

    DWORD WINAPI IatGetCapsShim(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
    {
        const DWORD r = g_iatPrevGetCaps ? g_iatPrevGetCaps(user, flags, caps)
                                         : ERROR_DEVICE_NOT_CONNECTED;
        return ProcessGetCaps(r, user, caps);
    }

    DWORD WINAPI IatSetStateShim(DWORD user, XINPUT_VIBRATION* vibration)
    {
        const DWORD r = g_iatPrevSetState ? g_iatPrevSetState(user, vibration)
                                          : ERROR_DEVICE_NOT_CONNECTED;
        return ProcessSetState(r, user, vibration);
    }

    bool ClaimIatSlot(void** slot, void* shim, void** prevOut, const char* what)
    {
        if (!slot || *slot == shim)
            return false;
        DWORD oldProtect;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
            return false;
        *prevOut = *slot;
        *slot = shim;
        VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
        LOG("M3: claimed game import-table slot for %s (previous handler %p)", what, *prevOut);
        return true;
    }
}

void Input_DescribeRecentButtons(char* buffer, size_t bytes, uint64_t sinceMs)
{
    if (!buffer || !bytes)
        return;
    buffer[0] = 0;
    const uint64_t now = GetTickCount64();
    const unsigned head = g_fedHead.load(std::memory_order_acquire);
    size_t used = 0;
    unsigned listed = 0;
    for (unsigned back = 1; back <= kFedButtonSlots && back <= head; ++back)
    {
        const unsigned slot = (head - back) % kFedButtonSlots;
        const uint64_t at = g_fedButtonsMs[slot].load(std::memory_order_relaxed);
        const uint32_t mask = g_fedButtons[slot].load(std::memory_order_acquire);
        if (at > now || now - at > sinceMs)
            break;
        const int written = _snprintf_s(
            buffer + used, bytes - used, _TRUNCATE, "%s0x%04X@-%llums",
            listed ? " " : "", mask & 0xFFFFu,
            static_cast<unsigned long long>(now - at));
        if (written <= 0)
            break;
        used += static_cast<size_t>(written);
        ++listed;
    }
    if (!listed)
        _snprintf_s(buffer, bytes, _TRUNCATE, "none (mask unchanged for >%llu ms; last 0x%04X)",
                    static_cast<unsigned long long>(sinceMs),
                    g_fedLastMask.load(std::memory_order_relaxed) & 0xFFFFu);
}

void Input_Halo2SwitchInputEvidence(Halo2SwitchInputEvidence& evidence)
{
    evidence = {};
    const uint64_t now = GetTickCount64();
    evidence.tabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    evidence.physicalBackDown =
        (g_physicalLastMask.load(std::memory_order_relaxed) & XINPUT_GAMEPAD_BACK) != 0;
    const uint64_t physicalBackMs = g_physicalBackMs.load(std::memory_order_relaxed);
    evidence.physicalBackAgeMs =
        physicalBackMs && physicalBackMs <= now ? now - physicalBackMs : UINT64_MAX;
    evidence.physicalBackPassThrough = g_config.halo2_gamepad_graphics_switch;
    const uint64_t fedBackMs = g_fedBackMs.load(std::memory_order_relaxed);
    evidence.virtualBackAgeMs =
        fedBackMs && fedBackMs <= now ? now - fedBackMs : UINT64_MAX;
}

// Project Reclaimer's keys at the head, from the headset's own input loop: the game does not read the controller in
// every lobby or menu, and these go by where the hand is alone. They take their buttons before anything else sees them.
void Input_StepHeadKeys(VrPadState& pad)
{
    const bool rawX = pad.x, rawY = pad.y, rawA = pad.a, rawB = pad.b;
    static HeadKeyGesture talkGesture, scoreboardGesture;
    static WORD talkKey = 0, scoreboardKey = 0;
    if (StepHeadKey(talkGesture, talkKey, true, g_config.reclaimer_talk_key, "talk",
                    pad.valid, pad.valid && rawX))
        pad.x = false;
    // Y + B stays the pause.
    static HeadYChord headY;
    const bool yDown = pad.valid && rawY;
    bool yAtHead = false;
    if (yDown && g_hostOwnsRenderSize)
        yAtHead = ControllerAtHead(0);
    // B at the right ear gives way to the same chord: Y held with it is the pause, not a vote.
    static HeadYChord headB;
    const bool bDown = pad.valid && rawB;
    bool bAtHead = false;
    if (bDown && g_hostOwnsRenderSize)
        bAtHead = ControllerAtHead(1);
    const uint64_t chordNow = GetTickCount64();
    const HeadYChord::Y yRoute = headY.Step(yDown, yAtHead, rawB, PauseUsesYB(), chordNow);
    const HeadYChord::Y bRoute = headB.Step(bDown, bAtHead, rawY, PauseUsesYB(), chordNow);
    static HeadKeyGesture voteYesGesture, voteNoGesture;
    static WORD voteYesKey = 0, voteNoKey = 0;
    if (StepHeadKey(voteYesGesture, voteYesKey, true, g_config.reclaimer_vote_yes_key,
                    "vote yes", pad.valid, pad.valid && rawA, 1))
        pad.a = false;
    const bool voteOwnsB = StepHeadKey(voteNoGesture, voteNoKey,
        !headB.chord, g_config.reclaimer_vote_no_key, "vote no",
        pad.valid, bRoute == HeadYChord::Y::Key, 1);
    if (voteOwnsB || bRoute == HeadYChord::Y::Waiting)
        pad.b = false;
    const bool scoreboardOwnsY = StepHeadKey(scoreboardGesture, scoreboardKey,
        !headY.chord, g_config.reclaimer_scoreboard_key,
        "scoreboard", pad.valid, yRoute == HeadYChord::Y::Key);
    if (scoreboardOwnsY || yRoute == HeadYChord::Y::Waiting)
        pad.y = false;
}

void Input_ReportGamePolls()
{
    // Project Reclaimer reads its controllers its own way: whether it still polls the mod's pad, and whether the polls
    // carried input, to set beside a report of the controls going dead.
    static uint64_t lastMs = 0;
    const uint64_t now = GetTickCount64();
    if (!g_hostIsProjectReclaimer || now - lastMs < 10000)
        return;
    lastMs = now;
    const uint32_t polls = g_gamePolls[0].exchange(0, std::memory_order_relaxed);
    const uint32_t carried = g_gamePolls[1].exchange(0, std::memory_order_relaxed);
    const uint32_t others = g_gamePolls[2].exchange(0, std::memory_order_relaxed);
    LOG("game pad: the game polled the mod's pad %u times in the last 10 s (%u of them carried controller input); "
        "other slots %u", polls, carried, others);
}

uint32_t Input_MenuPressSerial()
{
    return g_menuPressSerial.load(std::memory_order_relaxed);
}

void Input_RequestPauseToggle()
{
    if (!Game_AllowsPauseToggleInput())
        return;
    const bool paused = !VR_IsPausePresentationTarget();
    // Hold Start long enough to cross MCC's input polling boundary, then let
    // the normal released state provide the edge needed by a later toggle.
    g_startPulseUntilMs = GetTickCount64() + 350;
    g_menuPressSerial.fetch_add(1, std::memory_order_relaxed);
    const bool authoritative = Game_HasAuthoritativePauseState();
    if (!authoritative)
        VR_RequestPausePresentation(paused);
    LOG("pause fallback: injecting Start, presentation control=%s%s",
        authoritative ? "native engine flag" : "edge fallback, target=",
        authoritative ? "" : (paused ? "head-locked 2D" : "stereo 3D"));
}

bool Input_PhysicalCrouchRiseCapped()
{
    return g_physicalCrouchRiseCapped.load(std::memory_order_relaxed);
}

void Input_PollMccCrouchLock(uint64_t now)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Cold worker only. A timestamp check every 2 s; the file is parsed only when MCC rewrites it
    // (it does so when its settings menu applies a change), so an in-session change is picked up.
    static uint64_t s_checked = 0;
    static FILETIME s_stamp{};
    if (s_checked && now >= s_checked && now - s_checked < 2000)
        return;
    s_checked = now ? now : 1;
    char path[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!length || length >= MAX_PATH - 80 ||
        strcat_s(path, "\\AppData\\LocalLow\\MCC\\Saved\\Config\\WindowsNoEditor\\GameUserSettings.ini") != 0 ||
        !GetFileAttributesExA(path, GetFileExInfoStandard, &attributes))
    {
        // Not found (another store's layout, or no settings saved yet): unknown, which never stands
        // physical crouch down -- hold-to-crouch is MCC's default.
        s_stamp = {};
        g_mccCrouchLockPad.store(-1, std::memory_order_release);
        g_mccCrouchLockMk.store(-1, std::memory_order_release);
        return;
    }
    if (CompareFileTime(&attributes.ftLastWriteTime, &s_stamp) == 0)
        return;
    int pad = -1, mk = -1;
    // Shared read (_SH_DENYNO): MCC must never find its own settings file locked because we are reading it.
    FILE* file = _fsopen(path, "rb", _SH_DENYNO);
    if (file)
    {
        s_stamp = attributes.ftLastWriteTime;
        pad = mk = 0;
        char line[256];
        while (fgets(line, sizeof line, file))
        {
            int slot = -1, value = 0;
            if (sscanf_s(line, "CrouchLockEnabled[%d]=%d", &slot, &value) == 2 && slot >= 0 && slot < 16 && value)
                pad |= 1 << slot;
            else if (sscanf_s(line, "MKCrouchLockEnabled[%d]=%d", &slot, &value) == 2 && slot >= 0 && slot < 16 && value)
                mk |= 1 << slot;
        }
        fclose(file);
    }
    const int oldPad = g_mccCrouchLockPad.exchange(pad, std::memory_order_acq_rel);
    const int oldMk = g_mccCrouchLockMk.exchange(mk, std::memory_order_acq_rel);
    if (pad != oldPad || mk != oldMk)
        LOG("MCC crouch lock settings (GameUserSettings.ini, bit n = slot n; logged only, the slot-to-game "
            "mapping is unverified): controller=0x%X keyboard=0x%X", pad, mk);
#else
    (void)now;
#endif
}

void Input_ReportPhysicalCrouch(uint64_t now)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Cold worker, 1 Hz: print the snapshot the XInput hook stored, so a crouch that does or does not happen
    // explains itself -- every gate, the tracked head height against the learned baseline, what we pressed,
    // and (Reach) what the character actually did. Only while the snapshot is fresh: the hook publishes in
    // gameplay only, so menus and a disabled gesture stay quiet.
    static uint64_t s_last = 0;
    if (s_last && now >= s_last && now - s_last < 1000)
        return;
    const uint64_t at = g_crouchStatus.atMs.load(std::memory_order_acquire);
    if (!at || now < at || now - at > 1500)
        return;
    s_last = now;
    const uint32_t title = g_crouchStatus.title.load(std::memory_order_relaxed);
    const uint32_t gates = g_crouchStatus.gates.load(std::memory_order_relaxed);
    const float head = g_crouchStatus.head.load(std::memory_order_relaxed);
    const float base = g_crouchStatus.base.load(std::memory_order_relaxed);
    const uint64_t epoch = g_crouchStatus.epoch.load(std::memory_order_relaxed);
    int unit = -1;
    // Every title: the game's own crouch camera drop, as its correction read it (world units; a Blam world
    // unit is 3.048 m). Crouched = more than 10 cm of it. Reach keeps its unit-state bit.
    float nativeDropWu = 0.0f;
    const bool nativeDropFresh =
        PhysicalCrouchCamera_NativeDrop(static_cast<GameTitle>(title), 500, nativeDropWu);
    if (nativeDropFresh)
        unit = nativeDropWu * 3.048f > 0.10f ? 1 : 0;
    // ...and what its camera added back to the view this frame (the end of the chain).
    float appliedWu = 0.0f;
    const bool appliedFresh = PhysicalCrouchCamera_Applied(static_cast<GameTitle>(title), 500, appliedWu);
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    if (static_cast<GameTitle>(title) == GameTitle::HaloReach)
        unit = Game_ReachUnitCrouchState();
#endif
    // The crouch-lock masks print as -1 when the settings file was not read, never as 0xFFFFFFFF.
    char lockPad[16], lockMk[16];
    const int pad = g_mccCrouchLockPad.load(std::memory_order_relaxed);
    const int mk = g_mccCrouchLockMk.load(std::memory_order_relaxed);
    if (pad < 0) strcpy_s(lockPad, "-1"); else sprintf_s(lockPad, "0x%X", unsigned(pad));
    if (mk < 0) strcpy_s(lockMk, "-1"); else sprintf_s(lockMk, "0x%X", unsigned(mk));
    // head/base are in the continuous frame (raw LOCAL height + carry); ref = how the standing reference
    // was set (learned = the median of the last 10 s, calibrated = the player's Calibrate) and its sample
    // count. recenters = how each LOCAL discontinuity was met: seen = an announced recenter absorbed at
    // its step, unseen-step = an unannounced step absorbed (relocalisation), given = the runtime's offset
    // applied (too small to see, or inside a gap), late = announced after its step was absorbed,
    // missed = announced but never seen, breaks = the reference had to be re-learned.
    // why = the on-foot predicate's failing bits (Reach; 0 = on foot).
    VrCrouchContinuityStats continuity{};
    VR_GetCrouchContinuityStats(continuity);
    LOG("CROUCH: title=%u gate(tracking=%d native=%d onfoot=%d why=0x%X) head=%.3f base=%.3f down=%.3f "
        "init=%d active=%d suspended=%d pressed=0x%X transport=0x%X unitCrouched=%d nativeDrop=%s%.0fmm "
        "(miss 0x%X) applied=%s%.0fmm handsLift=%.0fmm mccCrouchLock(pad=%s mk=%s) ref=%s#%llu carry=%+.3f "
        "recenters(seen=%u unseen-step=%u given=%u late=%u missed=%u breaks=%u) gen=%u",
        title, (gates & 1u) ? 1 : 0, (gates & 2u) ? 1 : 0, (gates & 4u) ? 1 : 0,
        g_crouchStatus.onFootWhy.load(std::memory_order_relaxed), head, base, base - head,
        (gates & 8u) ? 1 : 0, (gates & 16u) ? 1 : 0, (gates & 32u) ? 1 : 0,
        g_crouchStatus.pressed.load(std::memory_order_relaxed),
        g_crouchStatus.transport.load(std::memory_order_relaxed), unit, nativeDropFresh ? "" : "stale:",
        double(nativeDropWu) * 3048.0, PhysicalCrouchCamera_NativeMiss(static_cast<GameTitle>(title), 1500),
        appliedFresh ? "" : "stale:", double(appliedWu) * 3048.0,
        double(Game_ViewLiftWu(static_cast<GameTitle>(title))) * 3048.0, lockPad, lockMk,
        g_crouchStatus.calibrated.load(std::memory_order_relaxed) ? "calibrated" : "learned",
        static_cast<unsigned long long>(epoch), double(continuity.carry), continuity.announced,
        continuity.unannounced, continuity.applied, continuity.late, continuity.unseen, continuity.breaks,
        g_crouchStatus.generation.load(std::memory_order_relaxed));
#else
    (void)now;
#endif
}

int Input_ClaimXInputIat()
{
    const BYTE* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!base)
        return 0;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return 0;
    int claims = 0;
    for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         desc->Name; ++desc)
    {
        const char* dllName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dllName, "xinput1_4.dll") != 0 &&
            _stricmp(dllName, "xinput1_3.dll") != 0 &&
            _stricmp(dllName, "xinput9_1_0.dll") != 0)
            continue;
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(
            const_cast<BYTE*>(base) + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat)
        {
            void** slot = reinterpret_cast<void**>(&iat->u1.Function);
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
            {
                const WORD ordinal = static_cast<WORD>(IMAGE_ORDINAL64(names->u1.Ordinal));
                // MCC imports XInputGetState from xinput1_3 by ordinal 2. This
                // is the exact IAT slot Steam Input replaces, so claiming it
                // removes the session-timing race that made controls vanish.
                if (ordinal == 2 || ordinal == 100)
                    claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetStateShim),
                                           reinterpret_cast<void**>(&g_iatPrevGetState),
                                           "XInputGetState ordinal");
                else if (ordinal == 3)
                    claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatSetStateShim),
                                           reinterpret_cast<void**>(&g_iatPrevSetState),
                                           "XInputSetState ordinal");
                else if (ordinal == 4)
                    claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetCapsShim),
                                           reinterpret_cast<void**>(&g_iatPrevGetCaps),
                                           "XInputGetCapabilities ordinal");
                continue;
            }
            auto imp = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (!strcmp(imp->Name, "XInputGetState"))
                claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetStateShim),
                                       reinterpret_cast<void**>(&g_iatPrevGetState), "XInputGetState");
            else if (!strcmp(imp->Name, "XInputSetState"))
                claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatSetStateShim),
                                       reinterpret_cast<void**>(&g_iatPrevSetState), "XInputSetState");
            else if (!strcmp(imp->Name, "XInputGetCapabilities"))
                claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetCapsShim),
                                       reinterpret_cast<void**>(&g_iatPrevGetCaps), "XInputGetCapabilities");
        }
    }
    return claims;
}

int Input_InstallXInputHook()
{
    // MCC may load any of these depending on OS/build, and it can load them
    // LATE (well after our first attempt) — the caller keeps retrying forever.
    // Safe to call repeatedly: already-hooked slots are skipped.
    const wchar_t* candidates[3] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    int hooked = 0;
    for (int i = 0; i < 3; ++i)
    {
        HMODULE mod = GetModuleHandleW(candidates[i]);
        if (!mod)
            continue;
        for (int j = 0; j < 2; ++j)
        {
            const int slot = i * 2 + j;
            if (g_origGetState[slot])
            {
                ++hooked;
                continue;
            }
            void* target = reinterpret_cast<void*>(
                j == 0 ? GetProcAddress(mod, "XInputGetState")
                       : GetProcAddress(mod, reinterpret_cast<LPCSTR>(100))); // XInputGetStateEx
            if (!target)
                continue;
            if (MH_CreateHook(target, reinterpret_cast<void*>(g_hooks[slot]),
                              reinterpret_cast<void**>(&g_origGetState[slot])) == MH_OK &&
                MH_EnableHook(target) == MH_OK)
            {
                LOG("M3: XInputGetState%s hooked in %ls", j == 0 ? "" : "Ex", candidates[i]);
                ++hooked;
            }
            else
            {
                g_origGetState[slot] = nullptr;
            }
        }
        if (!g_origGetCaps[i])
        {
            void* capsTarget = reinterpret_cast<void*>(GetProcAddress(mod, "XInputGetCapabilities"));
            if (capsTarget &&
                MH_CreateHook(capsTarget, reinterpret_cast<void*>(g_capsHooks[i]),
                              reinterpret_cast<void**>(&g_origGetCaps[i])) == MH_OK &&
                MH_EnableHook(capsTarget) == MH_OK)
            {
                LOG("M3: XInputGetCapabilities hooked in %ls", candidates[i]);
            }
            else
            {
                g_origGetCaps[i] = nullptr;
            }
        }
        if (!g_origSetState[i])
        {
            void* setTarget = reinterpret_cast<void*>(GetProcAddress(mod, "XInputSetState"));
            if (setTarget &&
                MH_CreateHook(setTarget, reinterpret_cast<void*>(g_setStateHooks[i]),
                              reinterpret_cast<void**>(&g_origSetState[i])) == MH_OK &&
                MH_EnableHook(setTarget) == MH_OK)
            {
                LOG("M3: XInputSetState hooked in %ls", candidates[i]);
            }
            else
            {
                g_origSetState[i] = nullptr;
            }
        }
    }
    static bool warned = false;
    if (!hooked && !warned)
    {
        LOG("M3: no XInput module loaded yet — retrying until one appears");
        warned = true;
    }
    return hooked;
}
