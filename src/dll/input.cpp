#include <windows.h>
#include <Xinput.h>
#include <cmath>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "game.h"
#include "roomscale.h"
#include "../common/roomscale_logic.h"
#include "vr.h"
#include "menu.h"
#include "title_adapter.h"
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
#include "physical_crouch_camera.h"

// M3 VR input. MCC reads gamepads through XInputGetState; hooking it lets the
// mod present the Sense controllers as a gamepad the game already understands
// (menus included), and substitute the right stick with the aim steering from
// Game_ComputeAimStick. If no physical gamepad is plugged in, a connected one
// is fabricated. The game then runs all input through its normal sensitivity
// and turn-rate paths, keeping bullets, reticle, vehicles and turrets correct.

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
    thread_local ScopeActionInput g_scopeInput;
    thread_local PhysicalCrouchInput g_physicalCrouchInput;
    thread_local vr_mapping::Mapper g_vrActionMapper;
    std::atomic<uint64_t> g_startPulseUntilMs{0};

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

    void MergeVrPad(XINPUT_STATE* state)
    {
        VrPadState pad;
        VR_GetPadState(pad);
        if (!pad.valid)
        {
            g_vrActionMapper.ready=false;
            g_scopeInput.Suspend();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false,0,0);
            return;
        }

        const bool sharedGameplayInput =
            Game_AllowsSharedGameplayFeatures();
        const MenuChordResult chord =
            g_menuChord.Update(GetTickCount64(), pad.clickL, pad.clickR);
        if (chord.toggled)
        {
            // The same one-shot chord edge owns both actions. Game_Recenter
            // resets every title's immersive camera reference and the OpenXR
            // room-fixed origin used by theatre; opening F1 on that edge then
            // places its head-locked panel from the newly centered pose.
            Game_Recenter();
            Menu_Toggle();
            LOG("L3+R3: recentered immersive/theatre space and toggled F1 menu");
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


        // The physical pad's own Y/B count for the pause chord too, so a
        // Steam Controller pauses the same way the VR controllers do.
        const WORD physicalButtons = state->Gamepad.wButtons;
        MenuChordResult pauseChord{};
        if (!menuOpen && !pad.exclusiveInput && g_config.y_b_start_chord && Game_AllowsPauseToggleInput())
        {
            pauseChord = g_pauseChord.Update(
                GetTickCount64(),
                pad.y || (physicalButtons & XINPUT_GAMEPAD_Y) != 0,
                pad.b || (physicalButtons & XINPUT_GAMEPAD_B) != 0);
            if (pauseChord.toggled)
                Input_RequestPauseToggle();
        }
        else
        {
            g_pauseChord.Reset();
        }

        const auto currentTitle=TitleAdapter_GetActiveTitle();
        const int mappingTitle=weapon_interaction::TitleIndex(currentTitle);
        const auto inputMode=TitleAdapter_GetRuntimeMode();
        const bool gameplayMode=(inputMode==RuntimeMode::Gameplay||inputMode==RuntimeMode::Vehicle||inputMode==RuntimeMode::Turret)&&
            !VR_IsPausePresentation()&&!VR_IsPausePresentationTarget()&&!VR_IsCutsceneTheaterActive();
        const uint64_t mappingEpoch=(uint64_t(TitleAdapter_GetGeneration(currentTitle))<<32)|
            (uint64_t(currentTitle)<<24)|pad.profileEpoch;
        const bool scopeFeatureAvailable=g_config.scope_enabled&&Game_IsHeadTracking()&&Game_HasScopeRenderer();
        const bool scopeAvailable=scopeFeatureAvailable&&gameplayMode;
        const int zoomSource=mappingTitle>=0&&g_config.vr_action_mapping?
            vr_mapping::Resolve(vr_mapping::Zoom,g_config.vr_bindings[mappingTitle][vr_mapping::Zoom]):vr_mapping::RightClick;
        const uint32_t collectedSources=ScopeAndActionSources(pad,dpadMode);
        // Changing D-pad gesture routes while the stick is held is cancellation,
        // not the release edge that toggles a scope. Face/trigger bindings are
        // unaffected by entering the optional head D-pad gesture.
        const unsigned scopeRoute=zoomSource>=vr_mapping::DpadUp?
            (pad.thumbrestDpad?2u:(dpadMode?1u:0u)):0u;
        const ScopeToggleUpdate scope=g_scopeInput.Update(scopeFeatureAvailable,collectedSources,
            zoomSource,mappingEpoch,scopeRoute,
            scopeAvailable&&!menuOpen&&(!pad.exclusiveInput||pad.thumbrestDpad),
            chord.consumeClicks||pauseChord.consumeClicks);
        if(scope.changed&&scopeAvailable)
        {
            VR_RequestScopeToggle();
            LOG("universal scope: mapped input release toggled body-safe scope state");
        }
        if(!scopeFeatureAvailable) VR_SetScopeActive(false);
        if(menuOpen)
        {
            g_vrActionMapper.ready=false;
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false,0,0);
            state->Gamepad={};
            return;
        }

        if(pad.exclusiveInput&&!Menu_IsOpen())
        {
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false,0,0);
            (void)Game_GestureMeleeInput(GetTickCount64()); // drains a pending gesture pulse
            g_pauseChord.Reset();
            g_startPulseUntilMs.store(0);
            const WORD confirm=pointerConfirm&&
                (pad.a||(state->Gamepad.wButtons&XINPUT_GAMEPAD_A))?XINPUT_GAMEPAD_A:0;
            state->Gamepad={};
            state->Gamepad.wButtons=confirm;
            if(pad.thumbrestDpad)
            {
                const auto title=TitleAdapter_GetActiveTitle();
                const int index=weapon_interaction::TitleIndex(title);
                if(g_config.vr_action_mapping&&index>=0&&
                    gameplayMode)
                {
                    vr_mapping::Transports native{};
                    const bool ready=Game_ReadVrActionBindings(native,GetTickCount64());
                    if(scopeAvailable) native[vr_mapping::Zoom]=0;
                    const uint32_t mapped=g_vrActionMapper.Apply(collectedSources,
                        g_config.vr_bindings[index],native,
                        (uint64_t(TitleAdapter_GetGeneration(title))<<32)|(uint64_t(title)<<24)|pad.profileEpoch,
                        ready,g_config.disable_flashlight_input||
                            (g_config.flashlight_suppress_on_two_hand&&pad.supportAimActive));
                    state->Gamepad.wButtons=static_cast<WORD>(mapped);
                    if(mapped&(1u<<16)) state->Gamepad.bLeftTrigger=255;
                    if(mapped&(1u<<17)) state->Gamepad.bRightTrigger=255;
                }
                else
                {
                    g_vrActionMapper.ready=false;
                    state->Gamepad.wButtons=DpadDirectionButtons(pad.dpadX,pad.dpadY);
                }
            }
            else g_vrActionMapper.ready=false;
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
        const bool semanticMode=g_config.vr_action_mapping&&mappingTitle>=0&&gameplayMode;
        const bool physicalCrouchEnabled=g_config.physical_crouch;
        const bool nativeReady=(semanticMode||physicalCrouchEnabled)&&gameplayMode&&
            Game_ReadVrActionBindings(nativeActions,inputNow);
        const bool nativeMapping=semanticMode&&nativeReady;
        float crouchHeadQuat[4]{},crouchHeadPos[3]{};
        const bool crouchTracking=physicalCrouchEnabled&&inputMode==RuntimeMode::Gameplay&&
            gameplayMode&&Game_IsHeadTracking()&&Game_IsPositionalTracking()&&
            VR_IsStereoEnabled()&&VR_RoomscaleTrackingFresh()&&
            VR_GetHeadPose(crouchHeadQuat,crouchHeadPos);
        const uint32_t crouchButtons=g_physicalCrouchInput.Update(currentTitle,
            TitleAdapter_GetGeneration(currentTitle),crouchHeadPos[1],VR_PhysicalCrouchEpoch(),inputNow,
            physicalCrouchEnabled,crouchTracking&&nativeReady,g_config.physical_crouch_depth_m,
            nativeActions[vr_mapping::Crouch]);
        PhysicalCrouchCamera_Publish(currentTitle,TitleAdapter_GetGeneration(currentTitle),
            VR_PhysicalCrouchEpoch(),inputNow,physicalCrouchEnabled&&crouchTracking&&nativeReady,crouchButtons!=0);
        uint32_t actionSources=collectedSources;
        if(pauseChord.consumeClicks) actionSources&=~(vr_mapping::Bit(vr_mapping::B)|vr_mapping::Bit(vr_mapping::Y));
        if(chord.consumeClicks) actionSources&=~(vr_mapping::Bit(vr_mapping::LeftClick)|vr_mapping::Bit(vr_mapping::RightClick));
        if(scopeAvailable) nativeActions[vr_mapping::Zoom]=0;
        if(wheelGesture) actionSources&=~(vr_mapping::Bit(vr_mapping::PrimaryGrip)|vr_mapping::Bit(vr_mapping::SupportGrip));
        const vr_mapping::Overrides emptyMappings{};
        const uint32_t semanticButtons=g_vrActionMapper.Apply(actionSources,
            mappingTitle>=0?g_config.vr_bindings[mappingTitle]:emptyMappings,nativeActions,
            (uint64_t(TitleAdapter_GetGeneration(currentTitle))<<32)|(uint64_t(currentTitle)<<24)|pad.profileEpoch,
            nativeMapping,g_config.disable_flashlight_input||
                (g_config.flashlight_suppress_on_two_hand&&pad.supportAimActive));
        const bool xPressed=pad.x;
        const float leftTrigger=pad.trigL;

        WORD btn = state->Gamepad.wButtons;
        if (pauseChord.consumeClicks)
            btn &= ~(XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_B);
        if (scopeAvailable)
            btn &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
        if (!semanticMode&&pad.a) btn |= XINPUT_GAMEPAD_A;
        if (!semanticMode&&pad.b && !pauseChord.consumeClicks) btn |= XINPUT_GAMEPAD_B;
        if (!semanticMode&&xPressed) btn |= XINPUT_GAMEPAD_X;
        if (!semanticMode&&pad.y && !pauseChord.consumeClicks) btn |= XINPUT_GAMEPAD_Y;
        if (!semanticMode&&pad.clickL && !chord.consumeClicks) btn |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (!semanticMode&&pad.clickR && !chord.consumeClicks && !scopeAvailable)
            btn |= XINPUT_GAMEPAD_RIGHT_THUMB;
        btn|=static_cast<WORD>(semanticButtons);
        btn|=static_cast<WORD>(crouchButtons);
        static bool previousMenu = false;
        const bool menuEdge = pad.menu && !previousMenu;
        if (menuEdge)
        {
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
            btn |= XINPUT_GAMEPAD_START;
        if (!wheelGesture&&!semanticMode)
        {
            if (pad.gripL > 0.6f && !pad.weaponConsumeSupport) btn |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (pad.gripR > 0.6f && !pad.weaponConsumePrimary) btn |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
        const bool weaponGesture=pad.weaponConsumeSupport||pad.weaponConsumePrimary||pad.weaponButtons;
        const uint32_t gestureMelee=weaponGesture?0:Game_GestureMeleeInput(inputNow);
        btn |= static_cast<WORD>(gestureMelee & 0xFFFF);
        btn |= static_cast<WORD>(pad.weaponButtons & 0xFFFF);
        state->Gamepad.wButtons = btn;
        NoteFedButtons(btn);

        const BYTE tl = semanticMode?((semanticButtons&(1u<<16))?255:0):(BYTE)(leftTrigger*255.0f);
        const BYTE tr = semanticMode?((semanticButtons&(1u<<17))?255:0):(BYTE)(pad.trigR*255.0f);
        if (tl > state->Gamepad.bLeftTrigger) state->Gamepad.bLeftTrigger = tl;
        if (tr > state->Gamepad.bRightTrigger) state->Gamepad.bRightTrigger = tr;
        if (crouchButtons & (1u<<16)) state->Gamepad.bLeftTrigger = 255;
        if (crouchButtons & (1u<<17)) state->Gamepad.bRightTrigger = 255;
        if (gestureMelee & (1u<<16)) state->Gamepad.bLeftTrigger = 255;
        if (gestureMelee & (1u<<17)) state->Gamepad.bRightTrigger = 255;
        if (pad.weaponButtons & (1u<<16)) state->Gamepad.bLeftTrigger = 255;
        if (pad.weaponButtons & (1u<<17)) state->Gamepad.bRightTrigger = 255;

        // The optional thumb-rest gesture uses the physical RIGHT stick. Its
        // axes were consumed at XR publication, including every title's turn
        // and scope consumers. Preserve head-gesture clicks and left movement.
        if (pad.thumbrestDpad&&!semanticMode)
        {
            btn |= DpadDirectionButtons(pad.dpadX, pad.dpadY);
            state->Gamepad.wButtons = btn;
            NoteFedButtons(btn);
        }

        const bool physicalMove = std::abs(int(state->Gamepad.sThumbLX)) > 7849 ||
            std::abs(int(state->Gamepad.sThumbLY)) > 7849;
        Roomscale_Input(RoomscaleGameplayEligible(TitleAdapter_GetActiveTitle(),
            TitleAdapter_GetRuntimeMode()) && !dpadMode && !physicalMove &&
            Game_IsHeadTracking() && Game_IsPositionalTracking() &&
            VR_IsStereoEnabled() && !VR_IsPausePresentation() &&
            !VR_IsPausePresentationTarget() && !VR_IsCutsceneTheaterActive(),
            pad.moveX, pad.moveY);

        if (dpadMode)
        {
            if (!semanticMode&&pad.moveY > 0.5f) btn |= XINPUT_GAMEPAD_DPAD_UP;
            if (!semanticMode&&pad.moveY < -0.5f) btn |= XINPUT_GAMEPAD_DPAD_DOWN;
            if (!semanticMode&&pad.moveX > 0.5f) btn |= XINPUT_GAMEPAD_DPAD_RIGHT;
            if (!semanticMode&&pad.moveX < -0.5f) btn |= XINPUT_GAMEPAD_DPAD_LEFT;
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
            // No walking while navigating.
            state->Gamepad.sThumbLX = 0;
            state->Gamepad.sThumbLY = 0;
            static std::atomic<bool> gestureLogged{false};
            if (!gestureLogged.exchange(true))
                LOG("M3: D-pad gesture active (right controller held at head)");
        }
        else if (Game_MoveStickIsLocomotion())
        {
            // Gameplay locomotion (UNCHANGED, headset-confirmed): the left stick
            // walks the player, rotated head-relative so forward = gaze, and
            // each axis floored past MCC's inner deadzone so small corrections
            // still move. This path runs only while the game is actually using
            // the stick to move the character.
            float mx = pad.moveX, my = pad.moveY;
            const bool roomscale = Roomscale_Move(mx, my);
            Game_MapMoveStick(mx, my);
            if ((roomscale && mx * mx + my * my > 1e-6f) ||
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
        if (Game_ComputeAimStick(rx, ry))
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

    DWORD ProcessGetState(DWORD r, DWORD user, XINPUT_STATE* state)
    {
        if (user != 0 || !state)
            return r;
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
        // Controller admission is separate from shared gameplay ownership.
        // Private ODST camera-only and Reach controller-only stages may expose
        // ordinary gamepad input while motion aim and every title-runtime
        // gameplay transform stay blocked.
        // Gate only decides whether to MERGE VR motion, not whether the pad
        // exists: hold the connection above and skip the merge when gated off.
        if (!Game_AllowsSharedControllerInput())
        {
            g_vrActionMapper.ready=false;
            g_scopeInput.Suspend();
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
            g_vrActionMapper.ready=false;
            g_scopeInput.Suspend();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            // No VR controllers: the physical pad still gets the Y+B pause
            // chord and the Start pulse it produces.
            if (!ownVirtualSlot && g_config.y_b_start_chord &&
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

void Input_RequestPauseToggle()
{
    if (!Game_AllowsPauseToggleInput())
        return;
    const bool paused = !VR_IsPausePresentationTarget();
    // Hold Start long enough to cross MCC's input polling boundary, then let
    // the normal released state provide the edge needed by a later toggle.
    g_startPulseUntilMs = GetTickCount64() + 350;
    const bool authoritative = Game_HasAuthoritativePauseState();
    if (!authoritative)
        VR_RequestPausePresentation(paused);
    LOG("pause fallback: injecting Start, presentation control=%s%s",
        authoritative ? "native engine flag" : "edge fallback, target=",
        authoritative ? "" : (paused ? "head-locked 2D" : "stereo 3D"));
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
