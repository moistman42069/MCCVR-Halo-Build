#include <windows.h>
#include <Xinput.h>
#include <cmath>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "game.h"
#include "vr.h"
#include "menu.h"
#include "title_adapter.h"
#include "../common/log.h"
#include "../common/config.h"
#include "../common/input_logic.h"
#include "../common/odst_bringup_logic.h"
#include "../common/scope_logic.h"

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
    ScopeToggleDetector g_scopeToggle;
    std::atomic<uint64_t> g_startPulseUntilMs{0};

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
            return;

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

        // The universal scope owns R3 while it is available. Passing that click
        // into Halo enters native zoom state, which hides the normal VR gun and
        // body even if we restore the eye FOV. Disabled/non-gameplay input still
        // passes through unchanged.
        const bool scopeAvailable = sharedGameplayInput &&
            g_config.scope_enabled && Game_IsHeadTracking() &&
            !Game_IsCameraOnlyBringup();
        const ScopeToggleUpdate scope = g_scopeToggle.Update(
            scopeAvailable, pad.clickR, chord.consumeClicks || Menu_IsOpen());
        if (scope.changed && scopeAvailable)
        {
            VR_RequestScopeToggle();
            LOG("universal scope: R3 release toggled body-safe scope state");
        }
        if (!scopeAvailable) VR_SetScopeActive(false);
        if (Menu_IsOpen())
        {
            state->Gamepad = {};
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

        MenuChordResult pauseChord{};
        if (g_config.y_b_start_chord && Game_AllowsPauseToggleInput())
        {
            pauseChord =
                g_pauseChord.Update(GetTickCount64(), pad.y, pad.b);
            if (pauseChord.toggled)
                Input_RequestPauseToggle();
        }
        else
        {
            g_pauseChord.Reset();
        }

        // Reach only: on foot, the left trigger and X trade places so grenade
        // lands on X and armour ability lands on the trigger. While the local
        // player is seated in ANY vehicle, restore Reach's native LT/X layout;
        // that includes aircraft such as the Banshee as well as drivers,
        // gunners, and passengers. Vehicle state is an optional title-native
        // proof. If it is unavailable or stale, preserve the existing on-foot
        // swap instead of changing controls or affecting the Reach VR core.
        const bool reachActive =
            TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
        const bool swapLeftHandActions = ReachShouldSwapLeftHandActions(
            reachActive, Game_ReachPlayerIsInVehicle());
        // The trigger is analog and X is a click. Neither Reach action is
        // pressure-sensitive, so cross them at the same 0.6 threshold the grips
        // already use, and drive the trigger fully on from the click.
        const bool xPressed = swapLeftHandActions ? pad.trigL > 0.6f : pad.x;
        const float leftTrigger =
            swapLeftHandActions ? (pad.x ? 1.0f : 0.0f) : pad.trigL;

        WORD btn = state->Gamepad.wButtons;
        if (scopeAvailable)
            btn &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
        if (pad.a) btn |= XINPUT_GAMEPAD_A;
        if (pad.b && !pauseChord.consumeClicks) btn |= XINPUT_GAMEPAD_B;
        if (xPressed) btn |= XINPUT_GAMEPAD_X;
        if (pad.y && !pauseChord.consumeClicks) btn |= XINPUT_GAMEPAD_Y;
        if (pad.clickL && !chord.consumeClicks) btn |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (pad.clickR && !chord.consumeClicks && !scopeAvailable)
            btn |= XINPUT_GAMEPAD_RIGHT_THUMB;
        static bool previousMenu = false;
        const uint64_t inputNow = GetTickCount64();
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
        if (!wheelGesture)
        {
            if (pad.gripL > 0.6f) btn |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (pad.gripR > 0.6f) btn |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
        state->Gamepad.wButtons = btn;

        const BYTE tl = (BYTE)(leftTrigger * 255.0f);
        const BYTE tr = (BYTE)(pad.trigR * 255.0f);
        if (tl > state->Gamepad.bLeftTrigger) state->Gamepad.bLeftTrigger = tl;
        if (tr > state->Gamepad.bRightTrigger) state->Gamepad.bRightTrigger = tr;

        // UEVR-style D-pad gesture: hold the configured controller (F1 menu:
        // left by default) up next to your head and the left stick becomes the
        // D-pad (menu navigation, grenade switching); lower it and the stick
        // walks again. No menu detection — the player chooses the mode with
        // the gesture, anywhere.
        bool dpadMode = false;
        {
            float hq[4], hp[3], cq[4], cp[3];
            const bool haveController = g_config.dpad_hand == 0
                ? VR_GetLeftControllerPose(cq, cp)
                : VR_GetRightControllerPose(cq, cp);
            if (haveController && VR_GetHeadPose(hq, hp))
            {
                const float dx = hp[0] - cp[0], dy = hp[1] - cp[1], dz = hp[2] - cp[2];
                dpadMode = dx * dx + dy * dy + dz * dz < 0.30f * 0.30f;
            }
        }

        if (dpadMode)
        {
            if (pad.moveY > 0.5f) btn |= XINPUT_GAMEPAD_DPAD_UP;
            if (pad.moveY < -0.5f) btn |= XINPUT_GAMEPAD_DPAD_DOWN;
            if (pad.moveX > 0.5f) btn |= XINPUT_GAMEPAD_DPAD_RIGHT;
            if (pad.moveX < -0.5f) btn |= XINPUT_GAMEPAD_DPAD_LEFT;
            // Left stick CLICK becomes the controller's left centre button
            // (Back/View) while the D-pad gesture is held. ODST puts its map
            // and objectives screen behind that button and VR players had no
            // way to reach it. This is deliberately scoped to the gesture: the
            // stick is already acting as a D-pad here, so its click has no
            // other meaning, and normal play keeps L3 untouched.
            if (pad.clickL && !chord.consumeClicks)
            {
                btn &= ~XINPUT_GAMEPAD_LEFT_THUMB;
                btn |= XINPUT_GAMEPAD_BACK;
            }
            state->Gamepad.wButtons = btn;
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
            Game_MapMoveStick(mx, my);
            if (mx * mx + my * my > 0.02f)
            {
                state->Gamepad.sThumbLX = ToRawStick(mx);
                state->Gamepad.sThumbLY = ToRawStick(my);
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
        VrPadState pad;
        VR_GetPadState(pad);
        if (!pad.valid)
        {
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

    template <int Slot>
    DWORD WINAPI GetStateHook(DWORD user, XINPUT_STATE* state)
    {
        return ProcessGetState(g_origGetState[Slot](user, state), user, state);
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
        const DWORD r = g_iatPrevGetState ? g_iatPrevGetState(user, state)
                                          : ERROR_DEVICE_NOT_CONNECTED;
        return ProcessGetState(r, user, state);
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
