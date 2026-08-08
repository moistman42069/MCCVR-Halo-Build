#pragma once

#include <cstdint>

#include "../common/halo3_vehicle_logic.h"
#include "../common/runtime_types.h"

// Monotonic counters written by render/palette hooks and sampled at OpenXR
// frame boundaries. Hooks only touch relaxed atomics; formatting and file I/O
// stay on the existing 50 ms title worker.
struct GameFramePerfCounters
{
    uint64_t viewRenders = 0;
    uint64_t fpPaletteRequests[3]{};
    uint64_t fpPaletteFullSolves[3]{}; // eye 0, eye 1, outside an eye pass
    uint64_t fpPaletteCacheHits[3]{};
    uint64_t fpPaletteCacheStores[3]{};
    uint64_t fpPaletteCacheFull[3]{};
    uint64_t zoomLogWrites = 0;
    uint64_t viewRateLogWrites = 0;
    uint64_t paletteRateLogWrites = 0;
    uint64_t cameraRateLogWrites = 0;
    uint64_t fpDriverRateLogWrites = 0;
};

void Game_ReadFramePerfCounters(GameFramePerfCounters& out);

// The Halo 3 engine (halo3.dll) loads only once you enter a level. This module
// waits for it, then (M1) drives the in-game camera from the headset. Runs on
// its own threads and never blocks rendering.

void Game_Init();
bool Game_IsHooked();
// Historical immersive-presentation gate. H4 C-H4-7 uses it to admit stereo
// even though that geometry-only candidate does not yet consume HMD head pose.
bool Game_IsHeadTracking();
// User-visible truth: the active camera actually consumes HMD rotation/position.
bool Game_IsHeadTrackingApplied();
bool Game_IsStereoGeometryOnlyBringup();
bool Game_IsCameraOnlyBringup(); // private ODST camera core; no gameplay features
// True only while the exact Reach stereo + mandatory authored-crosshair
// transaction owns the active title. Used to admit its frame-bound authored
// quad without granting unrelated shared gameplay capabilities.
bool Game_OwnsReachAuthoredReticle();
// Identity of the crosshair art Reach last captured. Unchanged value means the
// authored reticle already in the swapchain is still correct and does not need
// re-uploading this frame.
uint64_t Game_GetReachAuthoredCrosshairKey();
// Identity of the crosshair art captured this frame, for any title. 0 means
// nothing was captured. The compositor re-uploads the authored reticle only
// when this changes, so a static crosshair costs no swapchain work.
// True when the active title's authored-crosshair capture hooks are installed,
// so the captured widget is the crosshair and the procedural reticle must stay
// invisible. False means the procedural reticle IS the crosshair.
bool Game_TitleCapturesAuthoredCrosshair();
uint64_t Game_GetAuthoredCrosshairKey();
// How many class-2 widget pieces reached the capture surface this frame.
// 0 means the active title supplies no count, which leaves the capture
// completeness guard inert.
uint32_t Game_GetAuthoredCrosshairDrawCount();
// Halo 3's discrete authored colour output for the current capture. The value
// is an opaque state identity (not a requested replacement colour); 0 means the
// exact output was unavailable. Other titles retain their existing cadence.
uint32_t Game_GetAuthoredCrosshairColorState();
// Called once per displayed frame, before any capture, so the per-frame
// accumulation starts clean and a static crosshair yields a stable key.
void Game_ResetAuthoredCrosshairKey();
// Atomically rejects the active Reach authored-reticle transaction after a
// frame-bound upload failure. The title worker performs verified teardown;
// there is no procedural, transparent, or flat-crosshair substitute.
void Game_RejectReachAuthoredReticle(uint32_t expectedGeneration,
                                     const char* reason);
bool Game_AllowsSharedGameplayFeatures();
bool Game_AllowsSharedControllerInput();
// True only for a supported title-owned input context that may turn the Y+B
// fallback chord into a native Start pulse.
bool Game_AllowsPauseToggleInput();
bool Game_HasTitleCapability(uint32_t requiredCapabilities);
CinematicControlState Game_GetCinematicControlState();
bool Game_GetCutsceneTheaterPresentation(float& authoredAspect);
void Game_OnCutsceneTheaterPresentationChanged();
bool Game_CanToggleImmersiveView();
bool Game_ProcessPresentationDetachRequest();
// Render-thread emergency transaction used only after OpenXR session failure.
// Disarms title ownership before VR releases retained presentation resources.
void Game_DetachForVrRuntimeFailure();

// HUD layout: hud_size/hud_aspect drive Halo's safe-frame floats, while
// hud_curvature offsets the adjacent authored destination_offset_z in the
// loaded chud_globals tag data (auto-located from the same verified anchor).
void Game_LocateHudSafeFrames();  // manual rescan (menu button; normally automatic)
void Game_GetHudSafeFrameStatus(int& matches, bool& scanning);

// Head-tracking controls, driven by hotkeys so we can tune it live in-headset.
void Game_ToggleHeadTracking(); // F2
void Game_AutoVrTick();         // per-frame: auto-enter/exit VR on level load/exit
void Game_Recenter();           // F3; Halo camera + OpenXR screen origin
void Game_FlipYaw();            // F1 menu only (was F4: SteamVR's Alt+F4 kept triggering it)
void Game_FlipPitch();          // F1 menu only (was F5)
void Game_TogglePositional();   // F6: leaning / positional tracking on/off
// Forces positional tracking for established title cores; H4 C-H4-7 keeps
// only the shared immersive-stereo flag until its separate 6DOF milestone.
void Game_ForcePositional();
void Game_ToggleUp();           // F1 menu only (was F7)
float Game_GetYawSign();        // current calibration state, shown in the menu
float Game_GetPitchSign();
bool Game_GetWriteUp();
void Game_PitchTrim(int dir);   // F8 (down) / F9 (up): nudge pitch offset
void Game_LeanScale(int dir);   // PageDown / PageUp: leaning strength
void Game_GunScale(int dir); // Home (bigger) / End (smaller): hand-anchored weapon mesh size
void Game_ToggleVrAim();        // Insert: right controller steers the weapon aim

// M3 VR aim: called by the XInput hook on the game's input thread. Returns
// the right-stick deflection (-1..1) that turns the game's aim toward the
// right controller ray; false = leave the player's real stick alone.
bool Game_ComputeAimStick(float& outRx, float& outRy);
// Rotates a move-stick vector so pushing forward walks toward the gaze
// instead of the hand-steered aim heading. No-op when VR aim is inactive.
void Game_MapMoveStick(float& mx, float& my);
// True only while the game is actually consuming the left stick to move the
// player (gameplay/vehicle/turret). False in menus, pause, loading, cutscene,
// death and the shell, where the same stick navigates the game's own menus and
// must pass through as a plain analog stick (see input.cpp / GitHub #9).
bool Game_MoveStickIsLocomotion();
// Reach-only optional input refinement. True only when the exact current title
// generation has proven that output user 0's unit is seated on a parent unit.
// False preserves the established on-foot LT/X swap.
bool Game_ReachPlayerIsInVehicle();
// Halo 3-only vehicle refinement (docs/HALO3-VEHICLE-EVIDENCE.md). Lock-free
// view of the render-thread sampler. state is Unknown whenever the probe
// binding is absent, the title generation is stale, or the sampler has not
// run within the last half second — consumers must treat Unknown exactly like
// today's on-foot behavior, so every dependent feature stays stock by default.
struct Halo3VehicleStateSnapshot
{
    Halo3VehicleState state = Halo3VehicleState::Unknown;
    bool typeValid = false; // true only for an authored physics type 0..9
    int vehicleType = -1;   // 5 = turret (mounted or stationary, C1-measured)
    int seatIndex = -1;     // seat block index on the DIRECT parent tag
};
Halo3VehicleStateSnapshot Game_Halo3VehicleState();
// C9 virtual steering wheel (Halo 3 look-steered ground driver seats only; a
// no-op in every other seat, title and camera mode). Call once per XInput poll
// with THAT poll's pad, before the buttons are built — passing the same sample
// is what keeps a grip from being read as pressed by one and not the other.
// Taken and released by double-clicking both grips.
struct VrPadState;
void Game_Halo3UpdateVehicleWheel(const VrPadState& pad);
// True only while BOTH grips are down, which is the wheel gesture itself; the
// input hook withholds the grip buttons for exactly that window, so a lone
// right grip keeps performing the dismount it already does.
bool Game_Halo3VehicleSwallowsGrips();
// True while the wheel, rather than the turn stick, authors the steering.
bool Game_Halo3VehicleWheelActive();
// C13 per-SEAT trim: the config storage slot for the seat the player occupies
// right now (see ConfigSeatTrimSlot; a mounted gunner reports the carrier
// vehicle's gunner slot), -1 when on foot or unidentified, plus its display
// name ("Warthog gunner"). The F1 seat sliders bind to whichever seat this
// reports, so the same two widgets edit every seat's own trim without a
// per-seat menu. Game_Halo3SeatTrimName is menu-thread only.
int Game_Halo3CurrentSeatTrimSlot();
const char* Game_Halo3SeatTrimName(int slot);
// The F1 seat sliders need the title bank as well as the integer slot; slot 3
// in each title is unrelated storage. Keep this explicit as title coverage
// grows rather than encoding it in another boolean.
enum class VehicleTrimBank : uint8_t
{
    Halo3 = 0,
    Odst,
    Reach,
};
int Game_VehicleSeatTrimSlotEx(VehicleTrimBank* outBank);
const char* Game_VehicleSeatTrimName(int slot, VehicleTrimBank bank);
// True while a vehicle seat's own angular limit is holding the weapon short of
// where the hand points (a warthog rider seat authors yaw +/-90, pitch +/-45
// relative to the hull). `outDir` then carries the engine's ACTUAL aim as a
// VR-space unit vector so the floating reticle can sit on the gun instead of
// promising a shot the engine will never take.
bool Game_GetClampedAimDirection(float outDir[3]);
// Reach-only exact-frame reticle truth: returns the completed stereo pair's
// native seated unit aim in that pair's final center-camera local axes.
// The compositor rotates it by the submitted stereo-center orientation.
// False leaves the existing controller/clamp reticle path untouched.
bool Game_GetReachVehicleReticleAimDirection(
    uint64_t expectedPreparedSerial, float outCameraLocal[3]);
// True while an armed tracked camera consumes the OpenXR turn action. The
// XInput hook must then suppress stock RX/RY so the game cannot create a second
// camera motion underneath the HMD-owned view.
bool Game_VrOwnsLookStick();
// True while Halo 4's armed camera core owns the look stick's VERTICAL axis.
// Halo 4 keeps the horizontal axis on the engine (it has no VR turn or aim loop
// yet), so this is deliberately narrower than Game_VrOwnsLookStick.
bool Game_Halo4OwnsLookPitch();
// The closed-loop right-stick Y that keeps Halo 4's own look pitch - and so its
// shot line - under the headset. Returns false when the loop has nothing to
// command; the caller must still hold the axis at zero.
bool Game_ComputeHalo4PitchStick(float& outRy);
// Hooks XInputGetState in every loaded xinput DLL; returns how many are
// hooked. Safe to call repeatedly until it succeeds.
int Input_InstallXInputHook();
// Writes our shims into MCC's import table for xinput1_3 (Steam Input patches
// the same slots, bypassing DLL-level hooks). Call repeatedly to re-assert.
int Input_ClaimXInputIat();
// Injects a real XINPUT_GAMEPAD_START pulse and requests the matching
// stereo/flat pause presentation. Used when a runtime reserves its system key.
void Input_RequestPauseToggle();
// True when Halo's signature-resolved native pause flag is available. Once
// true, presentation follows the engine rather than controller-edge guesses.
bool Game_HasAuthoritativePauseState();


// M2 alternate-eye proof. -1 removes the stereo eye offset; 0/1 selects the
// left/right render camera for the next game frame.
void Game_SetStereoEye(int eye);
float Game_GetWorldScale();
// >1 while the player is zoomed (weapon scope); 1.0 at hip. Drives the scope.
float Game_GetZoomFactor();
// The mount-trimmed controller-local aim direction (unit, OpenXR local axes).
// Shared by bullet steering (game.cpp) and the reticle (vr.cpp) so barrel,
// flash, reticle and bullets stay on one ray as the user trims the mount.
// Symmetric half-frustum tangents from Halo's active world camera.
void Game_GetProjectionTangents(float& tanX, float& tanY);
// Returns the exact symmetric raster FOV for the frame being submitted. Reach
// fails closed when its two eye projections are absent or belong to another
// prepared frame, so OpenXR never receives stale Halo 3 projection metadata.
bool Game_GetRenderHalfFovs(
    uint64_t preparedFrameSerial, float halfX[2], float halfY[2]);
