#pragma once

// FIRST-PERSON VIEW -- the exit condition for two-handed aim, in all six titles.
//
// Two-handed aim builds the aim from the line between the two hands on the gun. That only means something while
// the game shows the player's own first-person view: in a third-person camera (a vehicle seat, a stationary or
// vehicle turret, a carried turret, a chase or orbit camera, the death / spectator camera, Forge's Monitor, a
// cinematic) the hands are not on the gun the player sees, and the resulting aim is confusing. So
// two-handed aim holds only while every signal a title has says "first person", and leaves at the first one that
// says otherwise. One-hand aim (the primary controller, the configured aim hand) is always safe; two-hand aim is
// a convenience. Every doubt therefore resolves toward one hand.
//
// Each title supplies whatever of these it can read, strongest first:
//   1. CAMERA     the game's own camera mode / perspective (Halo 3 and ODST: the director's camera record, mode 3
//                 = the first-person camera from perspective 0; CE: the user's native perspective, 0 = first).
//   2. SEATED     the unit's own parent link: a seat word or parent object (a seat or a mounted turret).
//   3. VIEW MODEL the title's first-person witness the mod already uses for roomscale: the first-person view model
//                 was drawn within its window (Halo 3 / ODST / Reach / Halo 4), or Halo 2's on-foot unit sample.
// plus two vetoes that need no ladder: a CINEMATIC owning the view, and NO UNIT (dead, respawning, spectating).
//
// Reach, Halo 4 and Halo 2 have a camera rung too (director_camera_logic.h): Reach's and Halo 4's
// director record (mode 3 = c_first_person_camera, perspective 0), Halo 2's director_get_perspective inputs.
//
// FORGE'S MONITOR shares the first-person camera while it holds nothing (Halo 3, Reach, Halo 4: the game director
// picks its first-person camera for any unit whose camera path is first person, and the editor's own camera only
// while the editor holds an object). The camera rung alone therefore reads an idle Monitor as first person, so the
// title's own "this player is a sandbox editor" answer is a veto here exactly as it is for roomscale
// (roomscale_admission_logic.h). Where that answer is unknown (not bound, unread, faulted) only the view model can
// vouch for a first-person camera: the Monitor draws none. A title with no Forge answers No by construction.
//
// A signal is Unavailable when the title has no such source on this build (never bound, a hook that never
// installed): the next rung decides. It is Unread when the source exists but this sample could not be read or is
// stale: that is never evidence of first person, and the gate below leaves two-hand after a short grace.
//
// Pure functions and plain structs only: unit-tested in tests/support_grip_logic_tests.cpp.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "shot_lag_logic.h"

namespace fp_view
{
enum class Signal : uint8_t
{
    Unavailable = 0,  // no such source for this title on this build: the next rung decides
    Unread,           // the source exists but this sample could not be read, or is stale
    Yes,
    No,
};

enum class Why : uint8_t
{
    FirstPerson = 0,
    Seated,             // a vehicle seat or a turret (the unit's own parent link)
    Camera,             // the game's camera is not its first-person camera from the first-person perspective
    NoFirstPersonView,  // the title's first-person witness says the first-person view is not being drawn
    Cinematic,          // a cinematic / authored camera owns the view
    NoUnit,             // no controlled unit: dead, respawning, spectating
    Unread,             // a signal this title relies on could not be read
    Monitor,            // Forge's Monitor (the game's own sandbox-editor answer)
    Count
};

inline const char* WhyName(Why why)
{
    switch (why)
    {
    case Why::FirstPerson: return "first person";
    case Why::Seated: return "vehicle seat or turret";
    case Why::Camera: return "third-person camera";
    case Why::NoFirstPersonView: return "first-person view not drawn";
    case Why::Cinematic: return "cinematic";
    case Why::NoUnit: return "no player unit";
    case Why::Unread: return "view unreadable";
    case Why::Monitor: return "Forge Monitor";
    default: return "?";
    }
}

// Positive evidence that the view is not first person: leave at once. The two soft reasons (a witness that went
// quiet, an unreadable signal) are also what a single long frame looks like, so they leave after kSoftGraceMs.
inline constexpr bool HardReason(Why why)
{
    return why == Why::Seated || why == Why::Camera || why == Why::Cinematic || why == Why::NoUnit ||
           why == Why::Monitor;
}

struct Evidence
{
    Signal cinematic = Signal::Unavailable;  // Yes = a cinematic owns the view (only Yes is ever used)
    Signal unit = Signal::Unavailable;       // Yes = the local player has a controlled unit (only No is used)
    Signal seated = Signal::Unavailable;     // Yes = seated or mounted
    Signal camera = Signal::Unavailable;     // Yes = the game's first-person camera from the first-person perspective
    Signal viewModel = Signal::Unavailable;  // Yes = the first-person view was drawn within the witness window
    // Yes = Forge's Monitor (the game's own answer); No = known not, or the title has no Forge. Unavailable /
    // Unread = unknown: a first-person camera then needs the view model to vouch (the Monitor draws none).
    Signal monitor = Signal::Unavailable;
};

// The verdict. Vetoes first; then the strongest rung the title has. The camera rung outranks the view-model
// witness (a native zoom or an unarmed player may stop the view model while the camera is still first person),
// but not a seat: the mod itself tells Halo 3's seats to be first person (C20), so the director can say "first
// person" from a seat -- which is why an UNREAD seat beside a first-person camera is no answer either. And not
// the Monitor: an idle Monitor has the first-person camera.
inline Why Classify(const Evidence& e)
{
    if (e.cinematic == Signal::Yes)
        return Why::Cinematic;
    if (e.unit == Signal::No)
        return Why::NoUnit;
    if (e.seated == Signal::Yes)
        return Why::Seated;
    if (e.monitor == Signal::Yes)
        return Why::Monitor;
    if (e.camera == Signal::No)
        return Why::Camera;
    if (e.camera == Signal::Yes)
    {
        if (e.seated == Signal::Unread)
            return Why::Unread;
        if (e.monitor == Signal::No)
            return Why::FirstPerson;   // first person, and not the Monitor: no view model needed (unarmed, zoom)
        // The Monitor check is unknown: only the view model can vouch.
        if (e.viewModel == Signal::No)
            return Why::NoFirstPersonView;
        if (e.viewModel == Signal::Unread)
            return Why::Unread;
        return Why::FirstPerson;       // drawn, or no witness on this build
    }
    if (e.camera == Signal::Unread)
        return Why::Unread;
    // No camera rung: the title's first-person witness.
    if (e.viewModel == Signal::No)
        return Why::NoFirstPersonView;
    if (e.viewModel == Signal::Unread || e.seated == Signal::Unread)
        return Why::Unread;
    if (e.viewModel == Signal::Yes)
        return Why::FirstPerson;
    // Nothing but the parent link: on foot is first person only when the title said so itself.
    return e.seated == Signal::No ? Why::FirstPerson : Why::Unread;
}

// The two-hand side (VR frame thread). `blocksSupport` closes the support-grip admission exactly as dual wield
// does (SupportGripAdmission: an engaged hold drops, and a Grip held through the exit needs a release and a fresh
// squeeze once first person returns); `entered` is the edge that drops the presentation history so the aim
// lands on the primary controller with no release blend (SeatedSupportGate::DropPresentationHistory).
//
// `hold` (a pause, the mod menu, a loading screen, a title's own pause flag) freezes the gate where it is: a pause
// is neither an exit nor a way back in, and the samples it stops are no evidence.
//
// Once blocked, only a FirstPerson verdict reopens it; the reason that closed it is kept for the log.
struct Gate
{
    static constexpr uint64_t kSoftGraceMs = 250;

    bool blocked = false;
    Why reason = Why::FirstPerson;
    bool softPending = false;
    uint64_t softSinceMs = 0;

    struct Step
    {
        bool blocksSupport = false;
        bool entered = false;
        Why reason = Why::FirstPerson;
    };

    Step Observe(Why why, bool hold, uint64_t nowMs) noexcept
    {
        if (hold)
        {
            softPending = false;
            return {blocked, false, reason};
        }
        if (why == Why::FirstPerson)
        {
            blocked = false;
            softPending = false;
            reason = Why::FirstPerson;
            return {};
        }
        if (blocked)
            return {true, false, reason};
        if (!HardReason(why))
        {
            if (!softPending)
            {
                softPending = true;
                softSinceMs = nowMs;
            }
            if (nowMs < softSinceMs || nowMs - softSinceMs < kSoftGraceMs)
                return {false, false, why};
        }
        blocked = true;
        softPending = false;
        reason = why;
        return {true, true, why};
    }
};

// Halo 3's director camera record as the two-hand gate reads it: the first-person camera (mode 3) from the
// first-person perspective (0) is first person; ANY other value is not -- including a mode outside the record's
// plausible range, which ODST's and Halo 3's set_camera_mode both build (their default case, a camera whose
// get_type is 8). The record identity (shot_lag 4d) treats such a value as unread; here it is simply "not the
// first-person camera", which is the safe direction.
inline Signal DirectorCameraSignal(bool bound, bool read, int32_t mode, int32_t perspective)
{
    if (!bound)
        return Signal::Unavailable;
    if (!read)
        return Signal::Unread;
    return mode == shot_lag::kHalo3CameraModeFirstPerson && perspective == shot_lag::kHalo3PerspectiveFirstPerson
               ? Signal::Yes
               : Signal::No;
}

// ---- ODST's director record (the same engine as Halo 3, a different second site) ----
// set_camera_mode's store matches shot_lag::kHalo3DirectorSetModeSitePattern uniquely in halo3odst.dll too
// (+0x152A90, in 0x15298C). The first-person model cache site Halo 3 pairs it with does not exist in ODST, so the
// second, independent site is the per-user FIRST-PERSON GATE both titles share (ODST +0x2A7150 in 0x2A714C, Halo 3
// +0x279D48 in 0x279D44): `movsxd r9, edx / cmp r9d, -1 / je / mov ecx, [rip + TLS index] / gs:[0x58] /
// mov edx, BLOCK / slots[index] / lea rcx, [user*3] / [tls + BLOCK] / cmp dword [rax + rcx*4 + PERSPECTIVE], 0 /
// jne / mov ecx, r9d / call player_unit` -- it asks for the user's unit only while the user's perspective is
// first person. Both sites must agree (block, perspective field at all three uses, mode right after it), and the
// gate's TLS index must be the engine TLS index ODST's vehicle binding decoded independently (unit_in_vehicle).
// tools/verify-odst-director-camera.py checks both sites, the decode, and that ODST's set_camera_mode builds the
// same camera types per mode as Halo 3 (case 3: get_type 3, get_perspective a constant 0).
// ADDR-HYGIENE: resolved -- no address or offset ships: the TLS index, the block and both fields are read from the
// two matched sites at bind time (each unique in the whole module, refused otherwise), cross-checked against each
// other and the independently decoded engine TLS index; a build where they do not agree binds nothing and the
// two-hand gate falls to the next rung (the view-model witness).
inline constexpr const char* kDirectorFirstPersonGateSitePattern =
    "4C 63 CA 41 83 F9 FF 74 ?? 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BA ?? ?? ?? ?? 4C 8B 04 C8 4B 8D 0C 49 "
    "49 8B 04 10 83 BC 88 ?? ?? ?? ?? 00 75 ?? 41 8B C9 E8";
inline constexpr size_t kDirectorGateSiteBytes = 55;
inline constexpr size_t kDirectorGateTlsDisp = 11, kDirectorGateTlsNext = 15, kDirectorGateBlock = 25,
                        kDirectorGatePerspective = 44;

// True (and `out` filled) only when the gate site and set_camera_mode's site agree, under the same plausibility
// bounds as shot_lag::DecodeDirectorCameraSites.
inline bool DecodeDirectorGateSites(const uint8_t* gate, const uint8_t* setMode, shot_lag::DirectorCameraSites& out)
{
    if (!gate || !setMode)
        return false;
    const auto u32 = [](const uint8_t* at) {
        uint32_t v = 0;
        std::memcpy(&v, at, sizeof(v));
        return v;
    };
    const uint32_t block = u32(gate + kDirectorGateBlock), persp = u32(gate + kDirectorGatePerspective);
    const uint32_t block2 = u32(setMode + shot_lag::kDirectorSetModeBlock);
    const uint32_t persp2 = u32(setMode + shot_lag::kDirectorSetModePerspective);
    const uint32_t mode = u32(setMode + shot_lag::kDirectorSetModeMode);
    const uint32_t perspStore = u32(setMode + shot_lag::kDirectorSetModePerspectiveStore);
    if (block != block2 || persp != persp2 || persp != perspStore || mode != persp + 4)
        return false;
    if (!block || block >= 0x1000 || (block & 7) || !persp || persp >= 0x10000 || (persp & 3) ||
        mode + 4 - persp > shot_lag::kDirectorUserStride)
        return false;
    out.blockOffset = block;
    out.perspectiveOffset = persp;
    out.modeOffset = mode;
    out.tlsIndexDisp = static_cast<int32_t>(u32(gate + kDirectorGateTlsDisp));
    return true;
}
} // namespace fp_view
