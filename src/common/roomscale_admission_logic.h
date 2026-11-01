#pragma once

// ROOMSCALE ADMISSION -- is the local player on foot, in control, and looking through his own eyes?
//
// The body follow, the view recenter, physical running and the head leash all act on one assumption: the
// engine camera this frame is the character's own first-person eye, standing on the ground under the player.
// Each title proves that with whatever it can read. This file is the one rule that turns those readings
// into a verdict, and names the reading that failed when it refuses.
//
// WHY THE VIEW MODEL ALONE IS NOT ENOUGH. The first-person VIEW-MODEL witness is the hook that runs while the
// engine draws the weapon in the player's hands. An unarmed character draws none (Halo 3, the start of Sierra
// 117: the Chief has no weapon), so a rule built on that witness alone stands roomscale down for as long as he
// is unarmed, while the game's own camera director says "first-person camera, first-person perspective"
// throughout. Hence the camera rung comes first.
//
// The ladder, strongest rung first:
//   1. CAMERA      the game's own camera mode / perspective (Halo 3, ODST, Reach and Halo 4: the director's
//                  camera record, mode 3 = the first-person camera, perspective 0; Halo 2: the director's
//                  first-person camera update with no transition; CE: the user's native perspective 0). A
//                  camera that is NOT first person refuses whatever else is true. A camera that IS first
//                  person admits without a view model -- provided the unit is not Forge's Monitor (below).
//                  Reach's, Halo 4's and Halo 2's rungs are in director_camera_logic.h; without one an
//                  unarmed player is refused for want of a view model.
//   2. VIEW MODEL  the first-person view-model witness, where the title's camera rung did not bind on this
//                  build or was condemned by the witness itself (director_camera::RungTrust). Fresh = first
//                  person. Not drawn = refused: there an unarmed player, a native zoom and a third-person
//                  camera all look alike.
//   3. NEITHER     a build where the witness hook did not install and no camera rung is bound admits on the
//                  on-foot proof alone (the optional hook must not cost the whole session its roomscale).
//
// FORGE'S MONITOR. The Monitor is an unarmed unit, on foot (no seat), player-controlled, and Halo 3's
// director gives it the FIRST-PERSON camera: c_game_director::update (0x211E80) picks mode 3 for any living
// unit whose camera path is first person, and its special mode 8 only while the editor's record carries an
// object index (0x2120AF..0x2120D2; by its use the object the Monitor holds -- read from the retail binary, not
// confirmed in game). The view-model rung excludes the Monitor only by accident -- it draws no view model. So
// the camera rung admits an unarmed player only when the game itself says the unit is NOT the Monitor: the
// engine's own
// "this player is a sandbox editor" predicate (Halo 3 0x111E44: multiplayer, engine type 5 = sandbox, the
// player's bit in the editor mask and his unit the editor biped; Reach 0x715AC and Halo 4 0xBA400 are the same
// question, and their game directors give an idle Monitor mode 3 too). A title with no Forge (CE, Halo 2, ODST)
// answers No by construction. A title whose predicate is not bound or could not be read answers "unknown",
// and then only the view model can vouch: fail closed.
//
// Every real stand-down stays: a seat or a turret, a cinematic, death (no unit), a third-person / orbit /
// spectator / scripted camera, the Monitor, a menu or a pause, an untracked head (the caller's own gate).
//
// Pure: no clock, no engine, no config. Unit-tested in tests/roomscale_admission_tests.cpp.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "first_person_view_logic.h"
#include "runtime_types.h"

namespace roomscale_admission
{
using fp_view::Signal;

enum class Refusal : uint8_t
{
    None = 0,            // admitted
    Unspecified,         // a caller that passes a bare "not allowed" (tests, a self-tracked frame)
    WrongTitle,          // another title's callback, or the active title changed under it
    Disabled,            // roomscale and physical running are off, or VR / 6DOF / VR aim / stereo is off
    Menu,                // the mod menu, a pause presentation, the title's own pause flag
    NotGameplay,         // the published runtime mode is not gameplay (shell, loading, paused, unsupported)
    Cinematic,           // a cinematic or the cutscene theatre owns the view
    ControlBlocked,      // the title has taken the player's input or look away (CE's native flags)
    NoUnit,              // no controlled unit: dead, respawning, spectating
    Seated,              // a vehicle seat or a turret
    StaleSample,         // the title's on-foot / seat / cinematic sample is missing, faulted or too old
    ThirdPersonCamera,   // the game's camera is not its first-person camera from the first-person perspective
    Monitor,             // Forge's Monitor (the game's own editor answer)
    CameraUnread,        // the camera rung is bound but this sample could not be read, and no view model vouches
    MonitorUnknown,      // first-person camera, no view model, and the Monitor check is unbound or unread
    NoFirstPersonModel,  // no camera rung: the first-person view model is not drawn (unarmed, zoom, third person)
    Count
};

inline const char* RefusalName(Refusal why)
{
    switch (why)
    {
    case Refusal::None: return "admitted";
    case Refusal::Unspecified: return "refused by the caller";
    case Refusal::WrongTitle: return "another title";
    case Refusal::Disabled: return "switched off / VR not presenting";
    case Refusal::Menu: return "menu or pause";
    case Refusal::NotGameplay: return "not in gameplay";
    case Refusal::Cinematic: return "cinematic";
    case Refusal::ControlBlocked: return "player control blocked";
    case Refusal::NoUnit: return "no player unit";
    case Refusal::Seated: return "seat or turret";
    case Refusal::StaleSample: return "on-foot sample stale";
    case Refusal::ThirdPersonCamera: return "camera not first person";
    case Refusal::Monitor: return "Forge Monitor";
    case Refusal::CameraUnread: return "camera mode unread";
    case Refusal::MonitorUnknown: return "no view model and Monitor check unknown";
    case Refusal::NoFirstPersonModel: return "no first-person view model";
    default: return "?";
    }
}

// What the title read this frame. Every field defaults to "this title has no such source".
struct TitleEvidence
{
    Signal cinematic = Signal::Unavailable;      // Yes = authored camera; No = player-controlled; Unread = stale
    Signal controlBlocked = Signal::Unavailable; // Yes = the title blocks the player's input or look
    Signal unit = Signal::Unavailable;           // Yes = a controlled unit; No = none
    Signal seated = Signal::Unavailable;         // Yes = seat / turret; No = on foot; Unread = no fresh sample
    Signal camera = Signal::Unavailable;         // Yes = the game's first-person camera, first-person perspective
    Signal monitor = Signal::Unavailable;        // Yes = Forge's Monitor; No = known not (or the title has no Forge)
    Signal viewModel = Signal::Unavailable;      // Yes = the first-person view model was drawn inside its window
};

// The title half: on foot, in control, first person.
inline Refusal ClassifyTitle(const TitleEvidence& e)
{
    if (e.cinematic == Signal::Yes)
        return Refusal::Cinematic;
    if (e.cinematic == Signal::Unread)
        return Refusal::StaleSample;
    if (e.controlBlocked == Signal::Yes)
        return Refusal::ControlBlocked;
    if (e.unit == Signal::No)
        return Refusal::NoUnit;
    if (e.seated == Signal::Yes)
        return Refusal::Seated;
    // The on-foot proof itself: a sampler that is not installed, faulted or stale is never "on foot".
    if (e.unit == Signal::Unread || e.seated == Signal::Unread || e.controlBlocked == Signal::Unread)
        return Refusal::StaleSample;
    // The Monitor is refused on the game's word alone, whatever the other rungs say.
    if (e.monitor == Signal::Yes)
        return Refusal::Monitor;

    // 1. The game's own camera.
    if (e.camera == Signal::No)
        return Refusal::ThirdPersonCamera;
    if (e.camera == Signal::Yes)
    {
        if (e.monitor == Signal::No)
            return Refusal::None;   // first person, and not the Monitor: no weapon needed
        // The Monitor check is unbound or unread: only the view model can vouch (the Monitor draws none).
        if (e.viewModel == Signal::No)
            return Refusal::MonitorUnknown;
        return Refusal::None;       // drawn, or no witness on this build (as before the witnesses)
    }
    // 2. No camera answer: the view-model witness.
    if (e.viewModel == Signal::Yes)
        return Refusal::None;
    if (e.viewModel == Signal::No)
        return Refusal::NoFirstPersonModel;
    if (e.viewModel == Signal::Unread || e.camera == Signal::Unread)
        return Refusal::CameraUnread;
    // 3. Neither rung exists on this build: the on-foot proof alone, as before the witnesses.
    return Refusal::None;
}

// The presentation half, the same for every title, checked first.
struct Presentation
{
    bool titleActive = false;   // the asking title is the active title
    bool enabled = false;       // roomscale or physical running on, VR enabled, positional, VR aim, stereo
    bool menu = false;          // the mod menu or a pause presentation
    bool theatre = false;       // the cutscene theatre
    RuntimeMode mode = RuntimeMode::Shell;   // the published runtime mode: only Gameplay admits
};

inline Refusal ClassifyPresentation(const Presentation& p)
{
    if (!p.titleActive)
        return Refusal::WrongTitle;
    if (!p.enabled)
        return Refusal::Disabled;
    if (p.menu)
        return Refusal::Menu;
    if (p.theatre)
        return Refusal::Cinematic;
    // A mode the title has already classified names its own reason (CE, Halo 3 and ODST publish these).
    switch (p.mode)
    {
    case RuntimeMode::Gameplay: return Refusal::None;
    case RuntimeMode::Paused: return Refusal::Menu;
    case RuntimeMode::Cutscene: return Refusal::Cinematic;
    case RuntimeMode::Vehicle:
    case RuntimeMode::Turret: return Refusal::Seated;
    case RuntimeMode::Dead: return Refusal::NoUnit;
    default: return Refusal::NotGameplay;   // the shell, a load, an unsupported state
    }
}

// The camera hook hands this to roomscale: the verdict, and the three first-person rungs as they stood, for
// the dev line (2 bits each: Signal).
struct Verdict
{
    Refusal refusal = Refusal::Unspecified;
    uint8_t rungs = 0;
    bool monitorCheckFaulted = false;   // the title's Monitor predicate faulted this generation (said once)
    // Reach / Halo 4 / Halo 2 (director_camera::RungTrust; each said once per title generation): the camera rung
    // agreed with the drawn view model often enough to be proven / never did and contradicted it for long enough
    // to be withdrawn (the view model then decides, as before).
    bool cameraRungProven = false;
    bool cameraRungCondemned = false;
    bool Admitted() const noexcept { return refusal == Refusal::None; }
};
// What each rung IS in a title, for the dev line: the game's camera, the Monitor answer, the view-model witness.
struct RungSources
{
    const char* camera = nullptr;
    const char* monitor = nullptr;
    const char* viewModel = nullptr;
};
inline RungSources TitleRungSources(GameTitle title)
{
    switch (title)
    {
    case GameTitle::HaloCE:
        return {"native user perspective 0", "no Forge", "none"};
    case GameTitle::Halo2:
        return {"director first-person update, no transition", "no Forge", "first-person palette packet, 250 ms"};
    case GameTitle::Halo3:
        return {"director mode 3 / perspective 0", "sandbox-editor predicate", "first-person interpolation, 100 ms"};
    case GameTitle::Halo3ODST:
        return {"director mode 3 / perspective 0", "no Forge", "first-person interpolation, 100 ms"};
    case GameTitle::HaloReach:
        return {"director mode 3 / perspective 0", "sandbox-editor predicate",
                "first-person interpolation twice in 150 ms"};
    case GameTitle::Halo4:
        return {"director mode 3 / perspective 0", "sandbox-editor predicate", "first-person skinning, 100 ms"};
    default:
        return {};
    }
}
inline constexpr uint8_t PackRungs(Signal camera, Signal monitor, Signal viewModel)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(camera) | (static_cast<uint8_t>(monitor) << 2) |
                                (static_cast<uint8_t>(viewModel) << 4));
}
inline constexpr Signal RungCamera(uint8_t rungs) { return static_cast<Signal>(rungs & 3u); }
inline constexpr Signal RungMonitor(uint8_t rungs) { return static_cast<Signal>((rungs >> 2) & 3u); }
inline constexpr Signal RungViewModel(uint8_t rungs) { return static_cast<Signal>((rungs >> 4) & 3u); }
inline const char* SignalName(Signal signal)
{
    switch (signal)
    {
    case Signal::Unavailable: return "none";
    case Signal::Unread: return "unread";
    case Signal::Yes: return "yes";
    case Signal::No: return "no";
    default: return "?";
    }
}
// The report's tail: every reason that refused at least one camera frame with its count, then the last refusal's
// reason and the three first-person rungs as they stood -- with `sources`, each rung named for the title it was
// read in ("game camera first person [director mode 3 / perspective 0]=yes"). Always terminated; returns the
// characters written.
inline int FormatRefusals(char* line, size_t size, const uint64_t counts[static_cast<size_t>(Refusal::Count)],
                          Refusal last, uint8_t lastRungs, const RungSources* sources = nullptr)
{
    if (!line || !size)
        return 0;
    line[0] = 0;
    size_t used = 0;
    const auto append = [&](const char* format, const char* text, unsigned long long number, bool withNumber) {
        if (used + 1 >= size)
            return;
        const int wrote = withNumber ? snprintf(line + used, size - used, format, text, number)
                                     : snprintf(line + used, size - used, format, text);
        if (wrote > 0)
            used = used + static_cast<size_t>(wrote) < size ? used + static_cast<size_t>(wrote) : size - 1;
    };
    for (size_t i = 1; i < static_cast<size_t>(Refusal::Count); ++i)
        if (counts[i])
            append(" %s=%llu;", RefusalName(static_cast<Refusal>(i)), static_cast<unsigned long long>(counts[i]), true);
    append(" at the last one (%s):", RefusalName(last < Refusal::Count ? last : Refusal::Unspecified), 0, false);
    const auto rung = [&](const char* name, const char* source, Signal value, const char* tail) {
        append(" %s", name, 0, false);
        if (source && source[0])
            append(" [%s]", source, 0, false);
        append("=%s", SignalName(value), 0, false);
        append("%s", tail, 0, false);
    };
    rung("game camera first person", sources ? sources->camera : nullptr, RungCamera(lastRungs), ",");
    rung("Forge Monitor", sources ? sources->monitor : nullptr, RungMonitor(lastRungs), ",");
    rung("first-person view model", sources ? sources->viewModel : nullptr, RungViewModel(lastRungs), "");
    return static_cast<int>(used);
}

inline Verdict Decide(const Presentation& p, const TitleEvidence& e)
{
    Verdict v{};
    v.rungs = PackRungs(e.camera, e.monitor, e.viewModel);
    v.refusal = ClassifyPresentation(p);
    if (v.refusal == Refusal::None)
        v.refusal = ClassifyTitle(e);
    return v;
}

// ---- Halo 3: the game's own "this player is Forge's Monitor" predicate ----------------------------------------
// c_game_director::update (0x211E80) asks it for the output user's player right after resolving the player and
// his unit, and uses the answer to choose the Monitor's held-object camera (mode 8). Read offline from the pinned
// halo3.dll (B209D845...EFB63):
//
//   0x211EB1  call 0xEE45C            ; player index of the director's output user   (kept in r15d)
//   0x211EB6  mov  ecx, r9d
//   0x211EBC  call 0xEE48C            ; that user's unit  (the getter the vehicle sampler already binds)
//   0x211EC1  xor  r13b, r13b
//   0x211EC7  cmp  r15d, -1 / je
//   0x211ECD  xor  r8d, r8d / xor edx, edx / mov ecx, r15d
//   0x211ED5  call 0x111E44           ; is_sandbox_editor(player, nullptr, nullptr)
//   0x211EDA  mov  r13b, al
//
//   0x111E44: false when the player is -1 or there is no game engine (campaign: 0x14B02C answers null); needs the
//   game mode byte ([TLS +0x48] +0x10) to be 2 (multiplayer), the player alive (0xFB72C), the engine type
//   (0x101144) to be 5 = sandbox, and then returns 0x1115FC(player): the player's bit in the editor mask (the
//   int16 at game-engine globals [TLS +0x20] +0xE1A4) with his unit's definition the editor biped (0x14AF78).
//   Every callee is a reader: no store outside the two optional out-pointers, which are passed null.
//
// Nothing is assumed at run time: the predicate's body and the director's call site are each matched uniquely in
// the whole module, the call site's third target must BE the matched body, its second target must be the unit
// getter the vehicle sampler bound from its own signature, its first target must have the player getter's body,
// and both TLS index operands must be the engine TLS index already bound. tools/verify-halo3-sandbox-editor.py
// checks all of that against the pinned module, offline.
// ADDR-HYGIENE: resolved -- no address or offset ships; a build where any link fails binds nothing, the Monitor
// check answers "unknown", and an unarmed player is then admitted only with a view model (the behaviour before).
inline constexpr const char* kHalo3SandboxEditorPredicatePattern =
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 45 32 D2 8B D9 4D 8B F0 48 8B FA 83 F9 FF "
    "0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 44 8B 05 ?? ?? ?? ?? "
    "4A 8B 34 C0 B8 ?? ?? ?? ?? 48 8B 04 30 80 78 ?? 02 75 ?? 8B CB E8 ?? ?? ?? ?? 84 C0 75 ?? E8 ?? ?? ?? ?? "
    "66 83 F8 05 75 ?? 8B CB E8 ?? ?? ?? ??";
inline constexpr size_t kSandboxEditorPredicateBytes = 119;
inline constexpr size_t kSandboxEditorPredicateTlsDisp = 67, kSandboxEditorPredicateTlsNext = 71;
inline constexpr const char* kHalo3DirectorEditorCallSitePattern =
    "E8 ?? ?? ?? ?? 41 8B C9 44 8B F8 E8 ?? ?? ?? ?? 45 32 ED 44 8B F0 41 83 FF FF 74 10 45 33 C0 33 D2 41 8B CF "
    "E8 ?? ?? ?? ?? 44 8A E8";
inline constexpr size_t kDirectorEditorCallSiteBytes = 44;
inline constexpr size_t kDirectorEditorCallPlayer = 0, kDirectorEditorCallUnit = 11, kDirectorEditorCallPredicate = 36;
// The player getter's body (player_mapping_get_player_by_output_user): the unit getter's twin, one field over.
inline constexpr const char* kHalo3OutputUserPlayerGetterPattern =
    "83 C8 FF 3B C8 74 27 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 ?? ?? ?? ?? 48 63 C9 48 8B 04 D0 "
    "4A 8B 04 00 8B 84 88 ?? ?? ?? ?? C3";
inline constexpr size_t kOutputUserPlayerGetterBytes = 47;
inline constexpr size_t kOutputUserGetterTlsDisp = 9, kOutputUserGetterTlsNext = 13, kOutputUserGetterBlock = 24,
                        kOutputUserGetterField = 42;

// rel32 call at `site + at`: its target as an offset from the module base (`siteRva` is the site's own offset).
inline int64_t CallTargetRva(const uint8_t* site, size_t at, int64_t siteRva)
{
    int32_t rel = 0;
    std::memcpy(&rel, site + at + 1, sizeof(rel));
    return siteRva + static_cast<int64_t>(at) + 5 + rel;
}
inline int64_t RipTargetRva(const uint8_t* site, size_t dispAt, size_t nextAt, int64_t siteRva)
{
    int32_t disp = 0;
    std::memcpy(&disp, site + dispAt, sizeof(disp));
    return siteRva + static_cast<int64_t>(nextAt) + disp;
}

struct SandboxEditorSites
{
    int64_t predicateRva = 0;     // is_sandbox_editor(player, out, out)
    int64_t playerGetterRva = 0;  // player index by output user
};
enum class SandboxEditorBind : uint8_t
{
    Bound = 0,
    PredicateMissing,   // the predicate's body: no match, or more than one
    CallSiteMissing,    // the director's call site: no match, or more than one
    TargetMismatch,     // the call site does not call the matched predicate
    UnitGetterMismatch, // the call site's unit getter is not the one the vehicle sampler bound
    PlayerGetterBody,   // the call site's player getter does not have the getter's body
    PlayerGetterTwin,   // the player getter is the unit getter, or reads another block than it
    TlsMismatch,        // a TLS index operand is not the engine TLS index
    Count
};
inline const char* SandboxEditorBindName(SandboxEditorBind why)
{
    switch (why)
    {
    case SandboxEditorBind::Bound: return "bound";
    case SandboxEditorBind::PredicateMissing: return "the predicate's body is missing or ambiguous";
    case SandboxEditorBind::CallSiteMissing: return "the director's call site is missing or ambiguous";
    case SandboxEditorBind::TargetMismatch: return "the director does not call the matched predicate";
    case SandboxEditorBind::UnitGetterMismatch: return "the call site's unit getter is not the bound one";
    case SandboxEditorBind::PlayerGetterBody: return "the player getter's body does not match";
    case SandboxEditorBind::PlayerGetterTwin: return "the player getter is not the unit getter's twin";
    case SandboxEditorBind::TlsMismatch: return "a TLS index operand is not the engine TLS index";
    default: return "?";
    }
}
// `predicate` / `callSite`: the unique matches (null = missing or ambiguous), with their offsets from the module
// base. `playerGetterBodyMatches`: the caller ran kHalo3OutputUserPlayerGetterPattern AT the call's first target.
// `unitGetterRva` / `engineTlsIndexRva`: what the vehicle sampler bound independently.
inline SandboxEditorBind DecodeSandboxEditorSites(const uint8_t* predicate, int64_t predicateRva,
                                                 const uint8_t* callSite, int64_t callSiteRva,
                                                 const uint8_t* playerGetter, bool playerGetterBodyMatches,
                                                 const uint8_t* unitGetter, int64_t unitGetterRva,
                                                 int64_t engineTlsIndexRva, SandboxEditorSites& out)
{
    if (!predicate)
        return SandboxEditorBind::PredicateMissing;
    if (!callSite)
        return SandboxEditorBind::CallSiteMissing;
    if (CallTargetRva(callSite, kDirectorEditorCallPredicate, callSiteRva) != predicateRva)
        return SandboxEditorBind::TargetMismatch;
    if (!unitGetter || CallTargetRva(callSite, kDirectorEditorCallUnit, callSiteRva) != unitGetterRva)
        return SandboxEditorBind::UnitGetterMismatch;
    const int64_t playerGetterRva = CallTargetRva(callSite, kDirectorEditorCallPlayer, callSiteRva);
    if (!playerGetter || !playerGetterBodyMatches)
        return SandboxEditorBind::PlayerGetterBody;
    // The twin: the same block of the same TLS slot, a different field.
    uint32_t playerBlock = 0, unitBlock = 0, playerField = 0, unitField = 0;
    std::memcpy(&playerBlock, playerGetter + kOutputUserGetterBlock, 4);
    std::memcpy(&unitBlock, unitGetter + kOutputUserGetterBlock, 4);
    std::memcpy(&playerField, playerGetter + kOutputUserGetterField, 4);
    std::memcpy(&unitField, unitGetter + kOutputUserGetterField, 4);
    if (playerGetterRva == unitGetterRva || playerBlock != unitBlock || playerField == unitField)
        return SandboxEditorBind::PlayerGetterTwin;
    if (RipTargetRva(predicate, kSandboxEditorPredicateTlsDisp, kSandboxEditorPredicateTlsNext, predicateRva) !=
            engineTlsIndexRva ||
        RipTargetRva(playerGetter, kOutputUserGetterTlsDisp, kOutputUserGetterTlsNext, playerGetterRva) !=
            engineTlsIndexRva)
        return SandboxEditorBind::TlsMismatch;
    out.predicateRva = predicateRva;
    out.playerGetterRva = playerGetterRva;
    return SandboxEditorBind::Bound;
}
} // namespace roomscale_admission
