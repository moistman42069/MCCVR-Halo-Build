#pragma once
bool Halo2Observer6Dof_OnFootFresh() noexcept;
#if HALOMCCVR_DEV_DIAGNOSTICS
// The simulation's clock now: the tick, and the frame interpolator's factor (NaN when unreadable).
void Halo2Observer6Dof_DevGameClock(long long& tick, float& factor) noexcept;
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "../common/seat_limit_reticle_logic.h"
void* Halo2Observer6Dof_ReloadWeapon(uint32_t weapon) noexcept;

// C-H2-8. Halo 2's per-user observer is the single camera root that BOTH of
// halo2.dll's renderers consume: the classic Blam tree reads it through the
// camera builder 0x7DF5A0 into the window cameras, and the remastered
// Anniversary (Saber GroundHog) renderer reads the same struct through the
// bridge 0x5F510, which builds its world matrix in metres. Writing a headset
// pose there therefore reaches whichever renderer is live, without forcing the
// player's graphics mode and without touching either renderer.
//
// The injection sits at the tail of the observer's LAST per-frame writer
// (0x6F0250). That ordering is load-bearing: the Saber bridge is invoked from
// inside observer_update_all itself (0x6F1C1F), so a pose applied after
// observer_update_all returns would already be too late for the remastered
// renderer.
//
// Only three non-contiguous 12-byte spans are ever written - position, forward
// and up. Field of view, aspect, cluster indices and velocity stay engine-owned.
// The engine rebuilds all three from its own state every frame (0x6F10E0), so
// an injected pose cannot accumulate and no restore is required.
bool Halo2Observer6Dof_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool activeAndRange, bool levelRunning, bool coldPassed,
    uintptr_t observerResultArray) noexcept;

bool Halo2Observer6Dof_Installed() noexcept;
// Game_SecondaryShotsOwnLine for Halo 2: its dual lane (and for barrel aim, its muzzle lane) live this generation.
bool Halo2Observer6Dof_SecondaryShotsOwnLine() noexcept;
// The core's own "teardown requested" flag, for the unload gate (host_unload_gate.h): never null.
std::atomic<bool>* Halo2Observer6Dof_TeardownRequested() noexcept;
// Management worker only. Runtime-failure latches are never cleared here.
void Halo2Observer6Dof_RequestRecovery(uint32_t generation) noexcept;
bool Halo2Observer6Dof_RecoveryPending() noexcept;
bool Halo2Observer6Dof_Armed() noexcept;
// Direct drive on foot: the observer published the view at its own heading within 100 ms
// (halo2_direct_drive.inl). While it has, the right stick turns that heading instead of reaching the game.
bool Halo2Observer6Dof_FootViewEngaged() noexcept;
// Direct drive in a seat: which axes the drive wrote within 150 ms (halo2_direct_drive.inl). The stick servo
// stands down on those.
bool Halo2Observer6Dof_SeatDriveAxes(bool& yaw, bool& pitch) noexcept;
// The seat limit reticle (src/common/seat_limit_reticle_logic.h): the stops the drive's last seat write knew (the yaw
// latch's, the record's eased pitch limits; record frame) and the hand's WORLD aim it wrote for, under 100 ms old.
bool Halo2Observer6Dof_SeatStops(seat_limit::Stops& stops, uint32_t& seatKey, float& recordYaw, float& targetYaw,
                                  float& targetPitch) noexcept;
void Halo2Observer6Dof_SnapTurnSettled(uint32_t generation, float targetYaw) noexcept;
// C-H2-43: independently installed firing-only direction hook. False keeps
// controller weapon placement dormant; it never controls camera admission.
bool Halo2Observer6Dof_DirectWeaponAimArmed() noexcept;
// Native local-unit update publishes seated state; expires after 100 ms.
bool Halo2Observer6Dof_VehicleControlActive() noexcept;
// The last seat sample said seated, however old (a pause stops the samples).
bool Halo2Observer6Dof_LastKnownSeated() noexcept;
// Whether there IS a last seat sample (observer armed and sampled). Without one LastKnownSeated() answers
// "seated" (fail closed); fail-open consumers (Game_TitleKnownSeated) must tell the two apart.
bool Halo2Observer6Dof_LastKnownSeatValid() noexcept;
// C-H2-50: optional final visible-palette transaction. The controller-owned
// shot path is admitted only while this exact visual boundary is installed.
bool Halo2Observer6Dof_FinalPaletteArmed() noexcept;
// The game's own perspective for local user 0, as an fp_view::Signal byte (0 unavailable: not bound on this build;
// 1 unread; 2 first person: the director's camera update is first_person_camera_update with no transition; 3 not:
// the following, dead or a scripted camera). Read from the director's static record: any thread, no game call
// (halo2_director_perspective.inl; src/common/director_camera_logic.h).
uint8_t Halo2Observer6Dof_FirstPersonCamera() noexcept;
// Optional H2EK-native hand/weapon contact transaction.  Physical melee may
// only synthesize input while this title-specific collision path is live.
bool Halo2Observer6Dof_WorldCollisionActive() noexcept;

// The pose the observer core applied on its most recent successful frame
// (E-H2-6 publication). Lock-free seqlock read, bounded; false when nothing
// has been published this generation or the read was torn four times.
struct Halo2ObserverPosePublication;
bool Halo2Observer6Dof_ReadPublishedPose(
    Halo2ObserverPosePublication& out) noexcept;
// E-H2-21: up to `maximum` recent publications, newest first, one per
// distinct tracked camera. Returns how many were filled.
int Halo2Observer6Dof_ReadPublishedPoses(
    Halo2ObserverPosePublication* out, int maximum) noexcept;

// E-H2-23 (C-H2-32): the ring index of the publication the most recent
// game-tick weapon placement was witnessed against, for this generation.
// False until a tick is witnessed, and false again after a tick that could
// not be witnessed (the previous witness never stands for a later tick).
// `previousIndex` is the tick before it (0 when there is none), so a renderer
// can follow the frame interpolator's blend between the two.
// Dev AS SEEN (halo2_impact_witness.inl; a no-op outside dev builds): the prepared frame a stereo core completed
// (renderer 0 Classic, 1 Anniversary) -- the centre camera its eyes were built from, the head orientation its views
// were submitted with, and the observer publication behind it (null when the core tracked a camera of its own) -- and
// the reticle the compositor drew for a serial (tracking space).
struct Halo2CameraBasis;
void Halo2Observer6Dof_DevNoteAsSeenFrame(int renderer, uint64_t serial, const Halo2CameraBasis& center,
    const float headQuat[4], const Halo2ObserverPosePublication* publication) noexcept;
void Halo2Observer6Dof_DevNoteAsSeenReticle(uint64_t serial, const float center[3], const float dir[3]) noexcept;

bool Halo2Observer6Dof_WeaponTickPublication(
    uint32_t generation, uint64_t& index, uint64_t& previousIndex) noexcept;

// E-H2-34 (C-H2-39): the stereo core that owns the frame names the cameras
// of the eye pass about to render - the centre the eyes render from, and for
// the classic pass the stale camera it draws from - so the weapon re-anchor
// moves the first-person geometry to exactly that pass. nullptr clears.
// Seqlocked copy; bounded; safe on the render thread.
struct Halo2FirstPersonPassCameras;
void Halo2Observer6Dof_SetFirstPersonPassCameras(
    const Halo2FirstPersonPassCameras* cameras) noexcept;

// E-H2-67 (C-H2-79): the camera the classic weapon pass actually holds,
// read from the engine's own first-person camera global at draw time and
// published here, so the compensation targets a MEASURED camera instead of
// a predicted one. Ignored when no core has named a pass.
struct Halo2CameraBasis;
bool Halo2Observer6Dof_BuildScopeCamera(Halo2CameraBasis& camera) noexcept;
void Halo2Observer6Dof_SetMeasuredFirstPersonViewingCamera(
    const Halo2CameraBasis& viewing, bool admitCompensation) noexcept;

// E-H2-70 (C-H2-82): the camera the ENGINE handed the packet builder for
// the named renderer's own build - the frame the final first-person nodes
// were composed against. Each stereo core compares it with the camera its
// renderer will actually draw those nodes from; a difference in
// ORIENTATION tips the weapon in that renderer alone. False until that
// renderer has owned a packet this session.
bool Halo2Observer6Dof_ReadPacketBuildCamera(
    bool anniversary, Halo2CameraBasis& out) noexcept;

// C-H2-63: draw_first_person brackets the exact Classic-only visible palette
// consumer with these calls. Begin temporarily compensates the controller-owned
// persistent packet for the current eye; End restores its controller-owned
// centre form before the other eye or the next game update can consume it.
bool Halo2Observer6Dof_BeginClassicFirstPersonEye() noexcept;
void Halo2Observer6Dof_EndClassicFirstPersonEye() noexcept;

// Atomic-only. The next valid sample becomes the new orientation/translation
// reference; safe to call from the universal recenter path.
void Halo2Observer6Dof_RequestRecenter() noexcept;
// The next recenter is the player's own (Game_RecenterManual): a seat re-aligns to the vehicle's nose.
void Halo2Observer6Dof_MarkManualRecenter() noexcept;

// Atomic-only. Removes ownership when the VR runtime is gone.
void Halo2Observer6Dof_ShutdownForVrFailure() noexcept;

// ---- Weapon-order diagnostic tranche (read-only evidence) ----
// Guarded datum/inventory primary observation for the first-B-frame ordering
// question. Requires armed/level-live/no-teardown, matching title generation
// and module, and rejects torn-teardown reads. Never mutates gameplay state.
// detailOut bit31 = native fault (ExceptionOrFault);
//           bit30 = lifecycle/generation guard rejection (GuardRejected);
//           bit1  = primary datum ownership proven (set on true return);
//           bit0  = secondary slot present (raw, not an absence claim).
// primaryAbsentOut (optional) distinguishes the native explicit raw slot
// UINT32_MAX from a validation/read failure. It is never set on false
// without a proven empty slot.
bool Halo2DiagnosticReadPrimaryWeapon(uint32_t& unitOut, uint32_t& weaponOut,
    uint32_t& detailOut, bool* primaryAbsentOut = nullptr) noexcept;
