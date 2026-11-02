#pragma once

#include <atomic>
#include <cstdint>

#include "../common/converge_policy.h"
#include "../common/first_person_view_logic.h"
#include "../common/halo3_vehicle_logic.h"
#include "../common/roomscale_admission_logic.h"
#include "../common/runtime_types.h"
#include "../common/support_grip_logic.h"
#include "../common/vr_action_mapping.h"

// Native 13-float matrix records (scale, column-major basis, translation).
// Call only with owner-verified authored wrists and the solved primary target.
void Game_PublishAuthoredGrip(GameTitle title, uint32_t generation,
    uint32_t unit, uint32_t weapon, const void* right, const void* left,
    const void* desired, const float* controllerBasis, float units, bool attached);

// True only for a current, occupied support hand in Halo 3.
bool Game_BlockHalo3SupportPickup() noexcept;
// Whether a dual-wielded second gun's shots leave on a line of their own (Halo 2, Halo 3): independent dual aim,
// or barrel aim through the muzzle lane, with the dual lane's hooks live. Otherwise both guns fire along the game's
// one aim: a proof failure (Project Reclaimer's Halo 3), a fault, the co-op guard, or both options off.
bool Game_SecondaryShotsOwnLine(GameTitle title) noexcept;

// Read-only, generation-tagged controller bindings. No game memory is touched
// on the caller's thread. False means there is no verified current snapshot.
bool Game_ReadVrActionBindings(vr_mapping::Transports& out, uint64_t nowMs,
    unsigned controller = 0);
uint32_t Game_VrActionTransport(vr_mapping::Action action, uint64_t nowMs);

// Camera-thread admission (roomscale_admission_logic.h): on foot, in control, first person -- by the game's own
// camera where the title has it bound (every title; Reach, Halo 4 and Halo 2 since director_camera_logic.h), else
// by the first-person view-model witness -- behind the roomscale setting and the presentation gates. The verdict
// names the reading that refused.
// A TITLE'S CAMERA CORE, for a lane that keeps its own loader reference on the game DLL (the first-person seat
// view, native_vehicle_first_person.cpp). Every game is meant to start in a fresh image of its DLL: when a game
// ends MCC lets go of the DLL, it unloads, and MCC loads it again for its menus (host_unload_gate.cpp). A reference
// that outlives the game makes that load take the old image, and the next game of the same title starts in a
// module whose own shutdown has already run. So such a lane holds its reference only while `installed` (the
// core's hooks are in: they need the image mapped until they are out), and registers it with the unload gate
// with `tearingDown` (the core's own "teardown requested") and `lastFrameMs` (GetTickCount64 of the title's last
// frame; null: the gate only waits for a teardown the core began itself). False: this title's core does not say,
// and the lane keeps its reference until another title starts, as before.
struct GameTitleCoreState
{
    bool installed = false;
    std::atomic<bool>* tearingDown = nullptr;
    const std::atomic<uint64_t>* lastFrameMs = nullptr;
};
bool Game_TitleCoreState(GameTitle title, GameTitleCoreState& out) noexcept;
roomscale_admission::Verdict Game_RoomscaleCameraVerdict(GameTitle title);
bool Game_RoomscaleCameraAllowed(GameTitle title);
// That on-foot, player-controlled, first-person evidence alone (no roomscale setting or presentation gates): the
// head leash below the character's eye holds the view only while this is true.
roomscale_admission::Verdict Game_TitleOnFootVerdict(GameTitle title);
bool Game_TitleOnFootPlayerControlled(GameTitle title);
// The local player is in a vehicle seat, as last known (a pause, which stops the samples, keeps the answer).
bool Game_TitleSeated(GameTitle title);
// Two-handed aim's exit condition: whether the game is showing the local player's own first-person view, as the
// title's strongest available evidence says (first_person_view_logic.h; the per-title table is at the definition).
// Any thread (VR's frame thread reads it): atomics and the camera threads' publications only. `why` is FirstPerson
// only on positive evidence; anything unreadable is Unread. `hold`: the shell, a load or a pause (the published
// runtime mode or the title's own flag), or not the active title: the caller should neither leave nor re-enter on
// this sample.
struct TitleFirstPersonView
{
    fp_view::Why why = fp_view::Why::Unread;
    bool hold = false;
};
TitleFirstPersonView Game_TitleFirstPersonView(GameTitle title);
// The title's camera rung as last published, raw (the dev line that says what each state's camera reads): the
// director's camera mode and perspective for output user 0 (Halo 3, ODST, Reach, Halo 4: first person is mode 3 /
// perspective 0; -1 / -1 for Halo 2, whose rung is the director's first-person camera update) and the Monitor
// answer. False: the title has no camera rung bound. Any thread: atomics and Halo 2's static read only.
bool Game_FirstPersonCameraSample(GameTitle title, fp_view::Signal& camera, int32_t& mode, int32_t& perspective,
                                  fp_view::Signal& monitor);

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
void Game_RequestManualVrRecovery();
const char* Game_ManualVrRecoveryStatus();

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
// True when the active title consumes the headset pose in its own verified
// camera transaction. Shared presentation must not run Halo 3's positional
// engine writes for such a title.
bool Game_UsesTitleOwnedHeadTracking();
bool Game_IsCameraOnlyBringup(); // private ODST camera core; no gameplay features
bool Game_HasScopeRenderer(); // optional title-local zoom view is available
// Aim Frame Zoom has the zoom button in the active title (Halo 3 with aim_frame_zoom on): it reaches the game,
// and the gun-mounted lens is not offered.
bool Game_AimFrameZoomOwnsZoom();
// The game's own zoom level for the local player (-1 not zoomed), Halo 3 and ODST. False: not read lately.
bool Game_ReadZoomLevel(int& level);
// Halo 3's HUD layout as it draws now: the one written (Game_GetAppliedHudSafeFrame), or, before it is written
// for a customised layout, the title's own (MCC 0.87, Project Reclaimer 0.97) with the player's size. False: the
// title's own layout.
bool Game_GetHalo3HudSafeFrameInEffect(float& horizontal, float& vertical, float* size);
bool Game_IsScopeRendering();
bool Game_SetScopeRendering(bool active);
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
// True only when a title places its native crosshair in 3D itself. Halo 4's
// native flat CUI copy is hidden while its captured animated/coloured pixels
// are presented by the shared controller-ray OpenXR quad, so it returns false.
bool Game_TitlePositionsNativeCrosshair();
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
// True only while Halo 2's armed native-CHUD rectangle adapter owns the title.
// Its UI exposes size/aspect/height but disables the unsupported curvature knob.
bool Game_UsesHalo2NativeHudLayout();
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
// The safe-frame pair the active title's HUD is drawn with, while the mod's layout is in every slot; false while
// it is not (the title's own safe frame then scales across and down alike). `size`, when given, receives the HUD
// size that pair was computed from. Lock-free: the crosshair capture reads it on the thread that draws the HUD.
bool Game_GetAppliedHudSafeFrame(float& horizontal, float& vertical, float* size = nullptr);
// Project Reclaimer: what reclaimer_flight_buttons does there right now, for the Vehicles panel.
const char* Game_ReclaimerFlightStatus();

// Head-tracking controls, driven by hotkeys so we can tune it live in-headset.
void Game_ToggleHeadTracking(); // F2
void Game_AutoVrTick();         // per-frame: auto-enter/exit VR on level load/exit
void Game_Recenter();           // F3; Halo camera + OpenXR screen origin
void Game_RecenterManual();     // the player's own recenter (L3+R3, F3, F1): Halo 2 seats re-align to the nose
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
// The free-camera frame (src/common/aim_no_unit_logic.h): the local player has had no unit long enough to be
// dead / waiting to respawn / spectating, as the last Game_ComputeAimStick decided it. The hand still steers the
// camera through the stick servo; nothing writes aim state. False when unknown. Any thread.
bool Game_AimFreeCamera();
// Shots from the muzzle, seated (defensive; converge::SeatMuzzleRefuses): whether the local player's seat
// points the gun away from the converged point by a natural cause right now -- the direct drive held at the
// seat's stop (Halo 3, ODST, Halo 4), or a rate-capped turret (Halo 3, ODST, where the muzzle owner check
// cannot see a stationary turret). Refuses the redirect only; nothing else reads it. Any thread.
converge::SeatMuzzleRefusal Game_SeatMuzzleRefusal(GameTitle title);
// The crosshair-assist re-aim, seated (Halo 3, ODST; converge::SeatAssistReaimRefuses): only a seat held at its natural
// stop refuses -- never a turret for being one (the game's own assist cap bounds a catch-up shot). Any thread.
converge::SeatMuzzleRefusal Game_SeatAssistReaimRefusal(GameTitle title);
// DEV (reach_direct_drive=4): true while the MASTER desired_angles persistence test owns Reach's aim;
// the XInput hook then holds the aim stick neutral. Defined only in experimental Reach builds.
bool Game_ReachDdTestOwnsAim();
// DEV (reach_direct_drive=3): Reach direct drive, the aim provider's WriteState rung
// (reach_aim_provider.cpp is the only caller). Defined only in experimental Reach builds.
//   TakeDesiredAim -- the game-basis aim (radians) Game_ComputeAimStick recorded during THIS input
//                     poll; consumed once, so a stopped aim path can never be chased.
//   Ready          -- the hook is installed, the arm-gate has proven the player_control address, and
//                     the hook has run on it within 150 ms (installed is not running).
//   OnFoot         -- fresh on-foot, player-controlled Reach gameplay.
//   AimWritable    -- where the write may drive the aim: on foot, or in a first-person seat the hand
//                     aims (not one whose stick is steering; as reach_vehicle_aim allows). AimWhy gives
//                     the failing conditions as bits, Seated whether the fresh sample is a seat.
//   Publish        -- set the setpoint (radians) the sim-thread hook writes on its next tick.
//   Release        -- invalidate it at once, so the hook stops writing and the stick servo aims alone.
bool Game_ReachTakeDesiredAim(float& yaw, float& pitch);
bool Game_ReachDirectDriveReady();
bool Game_ReachDirectDriveOnFoot();
uint32_t Game_ReachDirectDriveOnFootWhy();   // failing on-foot conditions as bits (0 = on foot)
bool Game_ReachDirectDriveAimWritable();
uint32_t Game_ReachDirectDriveAimWhy();      // failing aim-write conditions as bits (0 = writable)
bool Game_ReachDirectDriveSeated();
int Game_ReachUnitCrouchState();             // native crouch state 0/1, -1 unknown/stale (diagnostics)
bool Game_Halo4OnFoot();                     // the local Halo 4 unit is unseated and unparented (input thread)
// Diagnostics (the CROUCH line): what the view was last lifted by over the engine's eye -- the physical
// crouch give-back plus the leash -- which the title's hands share (world units; 0 when none or unknown).
// Halo 3 / ODST / Halo 4 / Halo 2; Reach keeps its own per-frame lift on the render thread (0 here).
float Game_ViewLiftWu(GameTitle title);
// `converged` = the core bent this setpoint through a freshly traced point. The sim-thread hook records
// it with every write, and the firing site stands the hand shot ray down only while the angles
// actually in desired_angles are converged ones. `seated` = the aim is a seat's: the hook then keeps its
// write inside the limits the game clamps that seat's record to.
void Game_ReachDirectDrivePublish(float gameYaw, float gamePitch, bool converged, bool seated);
void Game_ReachDirectDriveRelease();
void Game_ReachDirectDriveReport();   // cold worker: the hook's once-a-second status line
// Reach trace-and-reaim, on foot and seated (reach_sight_trace.inl). Defined only in experimental Reach
// builds.
//   SightSample -- the latest rendered intent (reticle) ray and the engine eye the shot is aimed from,
//                  all in world units, plus the latest range the sim-thread trace measured along that
//                  ray: the hit, or on a miss (hit=false) the trace's far end, since the target is at
//                  least that far. Only direct drive (reach_direct_drive=3) asks; asking is what keeps
//                  the sim-thread trace running. False while the ray or the trace result is stale,
//                  the crosshair-trace flags did not resolve on this build, or it faulted.
//   NoteConvergedSample -- DEV builds: the exact sample and smoothed range the provider actuated, so
//                  the shot check measures against what was aimed rather than a later sample.
struct ReachSightSample
{
    float origin[3]{};   // where the intent ray starts: the player's rendered EYE (centre eye), as in
                         // Halo: Campaign Evolved -- so a marker anywhere on the ray reads true
    float dir[3]{};      // its unit direction: the controller's aim direction
    float eye[3]{};      // the engine eye Reach aims from: on foot the character's eye; seated, the point
                         // of Reach's own camera line nearest the seat (its crosshair trace starts on it)
    float head[3]{};     // the rendered head (centre eye) of the same frame (== origin)
    float rangeWu = 0.0f;
    bool hit = false;
    uint64_t raySampleMs = 0, traceSampleMs = 0;
    uint64_t eyeStampUs = 0;   // when `eye` was published, on the sight clock (Sight_NowUs); 0 unknown
};
bool Game_ReachSightSample(ReachSightSample& out);
// For the dev converge-off witness only: whether the crosshair trace's flags resolved on this build, and
// whether the trace latched off on a native fault (the reasons Game_ReachSightSample can fail for good).
void Game_ReachSightHealth(bool& flagsResolved, bool& faulted);
void Game_ReachNoteConvergedSample(const ReachSightSample& sample, float smoothedRangeWu);
// The traced reticle's range along that eye-anchored intent ray, for the compositor (any thread):
// the latest fresh trace result -- the hit range in METRES, or hit=false on a miss -- and only while
// the aim in effect is the converged one, so the marker and the shot come from one piece of geometry.
bool Game_ReachSightReticleRange(float& outRangeM, bool& outHit);
// The direction Reach's zoom camera looks along from the head (scope convergence, F1 > Scope): the
// eye-anchored intent ray's, under the traced reticle's own gates plus scope_converge.
bool Game_ReachScopeConvergedDirection(float outDir[3]);
// C-H2-41. True only while Halo 2's controller-owned aim/weapon feature is
// requested; tracking loss still makes each call fail open independently.
bool Game_Halo2ControllerAimActive();
bool Game_Halo2SeatAimActive();   // a Halo 2 seat owns its aim (no on-foot palette witness in a seat)
bool Game_CeControllerFeaturesRequested();
bool Game_ComputeHalo2SnapStick(float& outRx);
// C-H2-89 optional feature transaction. The official H2EK boolean is resolved
// by name and set only while the Halo 2 VR core owns gameplay. Failure leaves
// aim assist stock without affecting camera/stereo; teardown restores the
// captured value.
bool Game_Halo2TryDisableAimAssist(uintptr_t moduleBase, size_t moduleSize);
void Game_Halo2MaintainDisabledAimAssist();
void Game_Halo2RestoreAimAssist();
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
// Vehicle cameras (vehicle_cameras.inl), for the F1 menu. Step asks for the seat's next (+1) or previous (-1)
// camera and tethering mode; the cold worker takes it up within a poll. Readout copies one line saying which
// camera is in use, or why none is; false when there is nothing to say (the feature is off).
void Game_VehicleCameraStep(int cameraDirection, int modeDirection);
bool Game_VehicleCameraReadout(char* out, size_t capacity);
// The lag of the vehicle camera in use (F1 > Vehicles): false when no chase camera is in use. `tuned`: the player
// has set it from the menu, in place of the camera file's.
bool Game_VehicleCameraLag(float& seconds, float& damping, bool& tuned);
// Sets the lag of the chase camera in use, kept for that camera in halomccvr_vehicle_cameras_tuning.txt.
void Game_VehicleCameraSetLag(float seconds, float damping);
// Back to the camera file's lag for the camera in use.
void Game_VehicleCameraResetLag();
// THE VEHICLE AIM RING (vehicle_cameras.inl; src/common/vehicle_aim_marker_logic.h): true while a Halo 3 chase
// camera whose "aimMarker" asks for it is up and its ring was placed within the last tenth of a second. `outDir`: the
// direction from the rendered centre eye to the point the vehicle aims at, a VR-space unit vector in the reticle's
// convention; `outDistanceM`: how far along it the ring is drawn, VR metres.
bool Game_GetVehicleAimMarker(float outDir[3], float& outDistanceM);
// The vehicle camera buttons (vehicle_seat_buttons.h), for the input hook. Live: the seat the player is in is one
// the vehicle cameras run in, so left X and left Y are theirs, and the left stick click too where `controlModes`
// says the game's seats can be controlled more than one way; `passenger` says it is a passenger's. ControlStep asks
// for the seat's next way to be controlled (the left stick click).
bool Game_VehicleSeatButtonsLive(bool& passenger, bool& controlModes);
void Game_VehicleControlStep();
// THE THIRD-PERSON BOOM (vehicle_boom_logic.h), for the input hook and the F1 menu. TakesStick: a Halo 3 seat's
// camera stands where the game's own third-person camera does, and the right stick's Y aims nothing in it (not a
// stick-aimed seat), so the boom raises and lowers it. Stick hands that Y over (0 when it is not the boom's); the
// caller sends the game none of it. Recenter puts the boom back at its camera's start. Game_VehicleBoom: false while
// no boom is in use; else its angle and the angles it reaches, degrees above level.
bool Game_VehicleBoomTakesStick();
void Game_VehicleBoomStick(float y);
void Game_VehicleBoomRecenter();
bool Game_VehicleBoom(float& degrees, float& lowDegrees, float& highDegrees, bool& fromTrack, bool& inverted);
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
#if HALOMCCVR_DEV_DIAGNOSTICS
// [assess-only begin]
// DEV ASSESSMENT ONLY (honest_reticle_assess): true while this input poll
// published where the engine's shot goes, for the compositor to draw as a
// SECOND marker beside the player's reticle (which it never moves). `outDir`
// carries it as a VR-space unit vector. A player build does not contain it.
// `outRangeM` > 0: the poll PLACED the mark -- `outDir` is then the
// ray from the rendered centre eye through the point the game's shot reaches
// (from the game's shot origin along its aim, to the first surface), and this
// is that point's distance in VR metres; `outSurface` says the range is a
// traced hit. 0: draw the direction from the eye at the crosshair distance.
// Only the VR drawing code may call it (tools/check_assess_isolation.py).
bool Game_GetClampedAimDirection(float outDir[3], float* outRangeM = nullptr,
                                 bool* outSurface = nullptr);
// [assess-only end]
#endif
#if HALOMCCVR_DEV_DIAGNOSTICS
// THE SHOT IMPACT WITNESS's two outside feeds (dev builds only; game.cpp halo3_impact_witness.inl). Neither decides
// anything. The compositor, each frame it draws the traced reticle (Halo 3 / ODST / Halo 4 lane, never Reach): its
// prepared frame's serial, the eye it drew from and the ray's direction (tracking space), and the range it was drawn
// for (VR metres, `hit` = a traced surface). The XInput hook, every poll: whether the RT fed to the game is pressed.
void Game_DevNoteTracedReticle(uint64_t serial, const float center[3], const float dir[3], float rangeM, bool hit);
void Game_DevNoteFireTrigger(bool pressed);
// A gun-slaved seat's pitch check (Halo 3 / ODST): false lets the stick servo run while the check collects its
// evidence, as before the hold existed. For comparing the two in one sitting; a player build always holds.
void Game_DevSetSeatPitchCheckHold(bool hold);
// Halo 4's Broadsword view: true stands it on the seat camera's point, false leaves the observer as the game makes
// it (the line still measures how far it trails). A player build always rides.
void Game_DevSetBroadswordRide(bool ride);
#endif
// THE SEAT LIMIT RETICLE (all builds; src/common/seat_limit_reticle_logic.h):
// true while this input poll clamped the seated reticle at a natural angular
// stop of a land vehicle's seat (a passenger's cone, a turret's or tank gun's
// pitch stop), so it sits where the gun is held. `outDir`: VR-space unit
// vector, the reticle's convention. `outRangeM` > 0: the poll PLACED it -- the
// ray from the rendered centre eye through the point the clamped shot reaches
// (from the seat's shot origin along the clamped aim, to the first surface),
// this far in VR metres, `outSurface` a traced hit. `outFromController`: a
// title with no traced seat reticle; `outDir` is the reticle's own direction
// turned by the clamp, drawn from wherever the reticle already is.
bool Game_GetSeatLimitReticle(float outDir[3], float& outRangeM, bool& outSurface,
                              bool& outFromController);
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
// Advances Halo 4's own VR turn (snap or smooth, per config) from one shared
// pad sample. Inert unless Halo 4's motion aim is active.
void Game_Halo4UpdateVrTurn(const VrPadState& pad);
// Stage 9 physical melee: samples OpenXR tracking-space hand velocity and is
// true during a short threshold-qualified native melee-input pulse. The XInput
// hook merges this with the same right-shoulder route as the Quest right grip,
// so Halo 4 retains proximity, target selection and the complete melee action.
bool Game_Halo4PhysicalMeleePulseActive(uint64_t nowMs);
// Dormant compatibility entry: true contact never injects a melee button.
bool Game_PhysicalMeleePulseActive(uint64_t nowMs);
// Configured gesture input: bits0..15 XInput buttons, bit16 LT, bit17 RT.
// Returns zero outside gameplay, without a fresh verified binding, or when
// true contact takes priority because both melee modes are selected.
uint32_t Game_GestureMeleeInput(uint64_t nowMs);
// 3CR/3CX fold-in: the live Halo 4 CUI canvas values the visible-pass hide
// last recorded - the positive base half-height (baseY) and the hide shift
// 4*|baseX|. Returns false until a hide has run (callers then keep the
// calibrated 3BR constants, exactly like the 3CX payload skipping its
// refresh).
bool Game_Halo4LiveCuiCanvas(float& baseY, float& hideShift);
// The closed-loop right-stick Y that keeps Halo 4's own look pitch - and so its
// shot line - under the headset. Returns false when the loop has nothing to
// command; the caller must still hold the axis at zero.
bool Game_ComputeHalo4PitchStick(float& outRy);
// C-H2-23: Halo 2's headset-owned pitch. The observer core flattens the
// engine's pitch out of the camera and composes the head's; this loop feeds
// the right stick's vertical axis so the engine's own aim pitch follows the
// view (the shot line follows where you look), exactly as Halo 4 does.
bool Game_Halo2OwnsLookPitch();
bool Game_ComputeHalo2PitchStick(float& outRy);
// Hooks XInputGetState in every loaded xinput DLL; returns how many are
// hooked. Safe to call repeatedly until it succeeds.
int Input_InstallXInputHook();
// Writes our shims into MCC's import table for xinput1_3 (Steam Input patches
// the same slots, bypassing DLL-level hooks). Call repeatedly to re-assert.
int Input_ClaimXInputIat();
// Injects a real XINPUT_GAMEPAD_START pulse and requests the matching
// stereo/flat pause presentation. Used when a runtime reserves its system key.
void Input_RequestPauseToggle();
// How many times the player has asked for the game's menu from the controller (wraps; compare for change only).
uint32_t Input_MenuPressSerial();
struct VrPadState;
void Input_StepHeadKeys(VrPadState& pad);
// MCC's crouch-lock (toggle crouch) settings, read from its GameUserSettings.ini by the cold worker
// (a timestamp check every 2 s, a parse only when MCC rewrites the file) for the CROUCH log line.
// Diagnostic only: the file did not predict Reach's crouch behaviour in FA (see input.cpp).
// Dev builds only (HALOMCCVR_DEV_DIAGNOSTICS); an empty stub otherwise.
void Input_PollMccCrouchLock(uint64_t nowMs);
// True when physical crouch learned the player's standing height only up to its cap (see
// PhysicalCrouch::RiseCapped): the menu then suggests calibrating again while standing tall.
bool Input_PhysicalCrouchRiseCapped();
// Cold worker, 1 Hz: the dev CROUCH line from the snapshot the XInput hook stores (gameplay only).
// Dev builds only; an empty stub otherwise, so the hook never formats or logs on the input thread.
void Input_ReportPhysicalCrouch(uint64_t nowMs);
// E-H2-13: every button mask the virtual gamepad fed the game in the last
// `sinceMs` milliseconds, newest first, as "0x1234@-37ms ..." (buffer-bounded).
// The game sees no other pad, so this is the whole input story behind an
// engine event such as the Halo 2 renderer switch.
void Input_DescribeRecentButtons(char* buffer, size_t bytes, uint64_t sinceMs);
// E-H2-19: the switch inputs visible right now (keyboard Tab, raw physical
// pad Back, the mod's own fed Back), for the renderer switch guard.
struct Halo2SwitchInputEvidence;
void Input_Halo2SwitchInputEvidence(Halo2SwitchInputEvidence& evidence);
// True when Halo's signature-resolved native pause flag is available. Once
// true, presentation follows the engine rather than controller-edge guesses.
bool Game_HasAuthoritativePauseState();


// M2 alternate-eye proof. -1 removes the stereo eye offset; 0/1 selects the
// left/right render camera for the next game frame.
void Game_SetStereoEye(int eye);
float Game_GetWorldScale();
// The gamepad transport the player's layout binds to Halo 3's left-weapon (secondary) reload,
// controller 0; 0 when unknown, unbound, stale or another title (gesture_melee_bindings.inl).
uint32_t Game_SecondaryReloadTransport(GameTitle title, uint64_t now);
void Game_SetWorldScale(float value);
// The F6 positional (leaning) toggle, shared by every title's camera core.
bool Game_IsPositionalTracking();
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

// ---- Shots-vs-reticle frame tranche (read-only evidence) ----
// Bounded lock-free reads of engine-side aim/camera publications for the
// per-prepared-frame telemetry recorder. Hot-path safe: atomic/seqlock reads
// only, finite-checked, no allocation/log/lock/IO, no gameplay mutation.
// Active-title qualification happens at the vr.cpp capture site; these
// readers enforce publication-local validity (generation/armed/freshness)
// only. Out-params are zeroed on failure; validity is authoritative.
// Shared legacy aim (H3/ODST/Reach-compact writers, latest-only, no serial).
bool Game_TelemetryReadSharedAim(float forwardOut[3]) noexcept;
// Shared camera truth: base = pre-lean origin, eye = rendered camera.
bool Game_TelemetryReadSharedCamera(bool& baseValidOut, float baseOut[3],
    bool& eyeValidOut, float eyeOut[3]) noexcept;
// Live g_worldScale, finite and positive (H3/H4 authority).
bool Game_TelemetryReadWorldScale(float& scaleOut) noexcept;
// Reach seated aim with provenance. Same admission as ReachReadAimFeedback
// (live generation, 500 ms freshness, seated sources only, finite) plus the
// publication source ordinal (ReachAimFeedbackSource), generation, sampleMs.
bool Game_TelemetryReadReachSeatedAim(float forwardOut[3], uint8_t& sourceOut,
    uint32_t& generationOut, uint64_t& sampleMsOut) noexcept;
// Reach completed-frame eye qualified to the live camera generation.
bool Game_TelemetryReadReachCompletedEye(float eyeOut[3],
    uint64_t& preparedSerialOut) noexcept;
// Halo 4 engine aim + pitch. Pitch is radians (capture converts to degrees);
// the serial is enginePitchSerial and may differ from its payload by one
// writer iteration (treat serial +/-1); there is no sample clock (sampleMs
// = 0).
bool Game_TelemetryReadHalo4EngineAim(float forwardOut[3],
    bool& pitchValidOut, float& pitchRadiansOut,
    uint64_t& pitchSerialOut) noexcept;
// Halo 4 pristine observer (stock) camera position from the stereo
// transaction, with its monotonic publication serial. Version-bracketed: the
// position/valid/serial triple is one whole publication or the read fails
// open. False keeps the caller's source at the explicit absent label; the
// position is never interpreted without true.
bool Game_TelemetryReadHalo4Camera(float eyeOut[3],
    uint64_t& serialOut) noexcept;
// T-2 sparse shot evidence: the one shared `shot` publisher, for a title
// firing detour that lives outside game.cpp (Halo 2's observer translation
// unit). It applies the disabled gate, suppresses a non-finite final ray,
// snapshots the presentation flags, fills the title-qualified engine aim
// source and publishes one event; the caller owns only the final ray and the
// firing context it actually carries. Callers pass
// Telemetry_WeaponEventsAccepting() FIRST and then publish nothing else when
// recording is off.
void Game_PublishShotDiagnostic(GameTitle title, uint32_t titleGeneration,
    uint32_t unit, uint32_t weapon, uint8_t slot, uint8_t barrel,
    bool predicted, bool substituted, bool firesFromCamera, bool unitAim,
    const float origin[3], const float direction[3]) noexcept;

// ---- Weapon-order diagnostic tranche (read-only evidence) ----
// Diagnostic-only helpers for the first-B-frame ordering question. They
// perform bounded read-only native observations under each title's existing
// guards and never mutate gameplay, tracking, rendering, or lifecycle state.
// Shared Game_DiagnosticReadPrimaryWeapon detailOut bits:
//   bit31 native fault (analyser ExceptionOrFault)
//   bit30 lifecycle/generation guard rejection (analyser GuardRejected)
//   bit2  FP producer/slot agreement (H3/ODST/Reach/H4; required by commits)
//   bit1  ownership proven (H2/H4 mirror validation)
//   bit0  secondary slot present (where the reader naturally knows it)
// Records the calling thread as this title's proven-safe FP thread for the
// current title generation and recording session. Diagnostic metadata only;
// no gameplay meaning.
void Game_DiagnosticNoteFpThread(GameTitle title) noexcept;
// Conservative §17 gate for TLS-backed titles (H3/ODST/Reach/H4). Returns 0
// when a Capture-time probe may run (stored FP thread for this
// title/generation equals the calling thread); otherwise 1 = no safe FP
// thread observed yet, 2 = thread mismatch, 3 = title/generation guard
// rejection. CE/H2 use their own cross-thread-proven readers and do not
// need this gate.
uint8_t Game_DiagnosticCaptureProbePermission(GameTitle title) noexcept;
// Ungated read-only semantic-primary observation. Returns false without
// touching unitOut/weaponOut when the title cannot prove the identity
// (lifecycle mismatch, seated/parent rejection, ownership failure, fault).
bool Game_DiagnosticReadPrimaryWeapon(GameTitle title, uint32_t& unitOut,
    uint32_t& weaponOut, uint32_t& detailOut) noexcept;

// ---- Persistent support grip: production owner evidence ----
// Tri-state semantic-primary ownership evidence. `state` is a
// support_grip::OwnerEvidence value; Unknown is never treated as absence.
// Returns false only when the title/generation cannot be observed at all.
// Halo 3 and Halo 2 are wired; every other title reports Unknown (state 0,
// generation carried) until its own producer lands.
struct PrimaryWeaponEvidence
{
    uint32_t generation = 0;
    uint32_t unit = 0xFFFFFFFFu;
    uint32_t weapon = 0xFFFFFFFFu;
    uint8_t state = 0;   // support_grip::OwnerEvidence
    uint32_t detail = 0; // Game_DiagnosticReadPrimaryWeapon detail bits
};
bool Game_ReadPrimaryWeaponEvidence(GameTitle title,
    PrimaryWeaponEvidence& out) noexcept;

// Persistent support grip: one observation of the title's current semantic
// primary, published to the durable relationship input. An unprovable read
// publishes an explicit Unknown record (generation 0 when the lifecycle is
// unavailable), so a stale KnownPresent cannot survive an observation that
// proves nothing. Titles call this from a lifecycle seam that also runs while
// no weapon packet/invocation exists (Halo 2's level-live poll).
void Game_PublishSupportGripOwnerEvidence(GameTitle title) noexcept;

// Persistent support grip: resolve one FP invocation's owner trust and publish
// its (possibly Unknown) owner evidence to the durable relationship input.
// Stability requires both reads to agree on an unchanged
// generation/unit/weapon; when `requireProducerAgreement` is set the FP
// producer/slot agreement bit (bit2) must also be present on both reads for a
// PRESENT owner. An explicit raw primary-slot absence is probe-provable
// without that bit (F03: agreement is a present-owner proof only). Read-only
// with respect to the durable relationship: the shared OpenXR input path is
// the only writer.
struct SupportInvocationResolution
{
    bool trusted = false;
    bool evidencePresent = false;
    bool evidenceAbsent = false;
    // F06/F07 material for a title that publishes a title-native invocation
    // receipt: the stable owner proof this invocation resolved (Unknown and
    // proven absence included) plus the durable-relationship reading taken at
    // the same instant. Only a title whose presented-ray consumer is wired
    // copies this into VR_PublishSupportInvocationReceipt.
    uint32_t generation = 0;
    support_grip::OwnerEvidence evidence = support_grip::OwnerEvidence::Unknown;
    bool producerAgreement = false;
    support_grip::OwnerTuple owner{};
    bool relationshipReadable = false;
    bool relationshipEngaged = false;
    uint64_t relationshipEpoch = 0;
};
SupportInvocationResolution Game_ResolveSupportInvocation(GameTitle title,
    const PrimaryWeaponEvidence& before, bool beforeOk,
    const PrimaryWeaponEvidence& after, bool afterOk,
    bool requireProducerAgreement) noexcept;
