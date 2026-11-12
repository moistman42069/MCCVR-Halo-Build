#pragma once

// THE GAME'S OWN FIRST-PERSON ANSWER -- Reach, Halo 4 and Halo 2.
//
// Roomscale's admission (roomscale_admission_logic.h) and two-handed aim's exit (first_person_view_logic.h) both
// ask "is the game showing the local player his own first-person view?". Halo 3, ODST and CE answer from the
// game's camera. The other evidence Reach, Halo 4 and Halo 2 have is the first-person WEAPON VIEW MODEL, which an
// unarmed character does not have -- judged by it alone, an unarmed player on foot is refused ("no first-person
// view model") and a zoom depends on whether the view model still draws. This file is the camera rung for those
// three titles.
//
// REACH and HALO 4 are Halo 3's engine: c_director::set_camera_mode stores, per output user, the perspective
// c_director::get_perspective() computed and the camera mode, in a 0xC record after the four directors of the
// director globals (engine TLS). Evidence:
//   * HREK (reach_tag_test.exe, the official kit; RTTI names the classes): c_first_person_camera::get_type() == 3
//     and get_perspective() == 0; c_following_camera 0 / 1; c_orbiting_camera 1 / 1; c_flying_camera 2 / 3;
//     c_dead_camera 4 / 1; c_static_camera 5 / 3; c_scripted_camera 6; c_authored_camera 7; c_respawning_camera
//     8 / 1; c_animated_trick_camera 9 / 1; c_tracked_target_camera 10 / 1; c_sandbox_camera 11 / 0;
//     c_space_fighter_camera 12 / 1. The kit's set_camera_mode (director.cpp, by its asserts) stores the mode at
//     [globals + user*0xC + 0x6B8], the offsets retail has.
//   * retail haloreach.dll: set_camera_mode 0xC5834 builds, per case, a camera whose vtable reports exactly the
//     kit's type / perspective pairs, and stores perspective +0x6B4 / mode +0x6B8 in TLS block +0x188.
//   * retail halo4.dll (read from the retail binary only, no editing-kit symbols; names by analogy with Reach's kit):
//     set_camera_mode 0x10BD98, the same table plus a 14th camera (type 13, perspective 3); perspective +0x744 /
//     mode +0x748 in TLS block +0x150.
// c_game_director::update (Reach 0x134260 = HREK 0x4E66B0, Halo 4 0x2056D8) picks the mode from the unit's camera
// path (Reach 0xC6948, Halo 4 0x10D268): first person -> mode 3; third person -> the following camera 0 (or 9 /
// 12). The path is third person for a seat whose tag says so, for an animation-driven camera, and while the unit's
// active equipment's definition asks for a third-person camera (the flag is read from the equipment tag; the tags
// themselves are not inspected -- by the games' behaviour Reach's Armor Lock, Halo 4's Hardlight Shield and Thruster
// Pack). No unit -> the dead (4), respawning (8) or static (5) camera; cinematics are 6 / 7. get_perspective()
// itself answers 1 while the first-person camera is still blending in, and 3 when the game is not in progress.
// So "mode 3, perspective 0" is first person, and nothing else is.
//
// FORGE'S MONITOR. In Reach the game director gives an editing player the sandbox camera (11) only while the
// game-engine globals carry an object index for him (by its use the object the editor holds; not confirmed in
// game); an idle Monitor gets mode 3. Halo 4's game director asks the same question and drops the answer: with
// a first-person camera path its Monitor gets mode 3 (where Halo 4 picks its sandbox camera is not known). So, as
// in Halo 3, the camera does not exclude the Monitor and the game's own "this player is a sandbox editor"
// predicate is asked (Reach 0x715AC = HREK 0x465800, Halo 4 0xBA400: a game engine, the multiplayer game mode, a
// living player, the sandbox engine, and the per-player editor test).
// Both are readers: no store but one through the optional out-pointer, which is passed null, and no indirect call
// (tools/verify-reach-director-camera.py and tools/verify-halo4-director-camera.py walk them).
//
// HALO 2 is the older engine: no camera classes. The director's per-user record (a static array in the module,
// not TLS) holds the camera's UPDATE FUNCTION, and director_get_perspective(user) (retail 0x6D4EC0, called from
// 29 functions: first-person weapons, the HUD, player control) derives the perspective from it: 0 when the update
// is first_person_camera_update and the record's transition timer is zero, 1 for the following camera, 2 for the
// scripted ones, else 3. Halo 2 has no Forge. Read from the retail binary only (no editing-kit symbols).
//
// Nothing here ships an address or an offset: every one is decoded from two code sites that are each matched
// uniquely and must agree (and, where the title already bound them, with the engine TLS index and the unit
// getter). A build where any link fails binds nothing and the title keeps the view-model rung, said once.
// Pure: no clock, no engine. Unit-tested in tests/roomscale_admission_tests.cpp against the pinned modules' bytes.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "first_person_view_logic.h"

namespace director_camera
{
using fp_view::Signal;

inline uint32_t ReadU32(const uint8_t* at)
{
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
// rel32 call at `site + at`: its target as an offset from the module base (`siteRva` is the site's own offset).
inline int64_t CallRva(const uint8_t* site, size_t at, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(at) + 5 + static_cast<int32_t>(ReadU32(site + at + 1));
}
// rip-relative operand whose disp32 sits at `dispAt` in an instruction that ends at `nextAt`.
inline int64_t RipRva(const uint8_t* site, size_t dispAt, size_t nextAt, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(nextAt) + static_cast<int32_t>(ReadU32(site + dispAt));
}

// ---- Reach / Halo 4: the director's per-user camera record -------------------------------------------------
// Site 1, the tail of c_director::set_camera_mode (identical code in both titles but for the constants):
//   call get_perspective / mov edx, [rip + TLS index] / gs:[0x58] / movsxd r8, [director + USER] /
//   mov r9d, BLOCK / slots[index] / [tls + BLOCK] / lea r9, [user*3] / cmp [rdx + r9*4 + PERSPECTIVE], eax / jne /
//   lea rcx, [user*3] / cmp [rdx + rcx*4 + MODE], ebp / je / mov [rdx + r9*4 + PERSPECTIVE], eax / mov ebx, edi /
//   movsxd rax, [director + USER] / lea rcx, [user*3] / mov [rdx + rcx*4 + MODE], ebp / mov esi, [director + USER] /
//   mov edx, ebx / mov ecx, esi / call NOTIFY
// Site 2, NOTIFY itself -- the first-person weapons' "is this user's weapon slot shown" (it is what the store
// calls for both weapon slots right after writing the record), which reads the perspective and wants 0:
//   mov r8d, [rip + TLS index] / xor ebx, ebx / gs:[0x58] / movsxd rdi, ecx / slots[index] / imul r8, rdi, .. /
//   mov eax, .. / add r8, [tls + ..] / movsxd rax, edx / imul r9, rax, .. / test byte [r9 + r8 + ..], 1 / je /
//   mov eax, BLOCK / lea rcx, [user*3] / [tls + BLOCK] / cmp [rax + rcx*4 + PERSPECTIVE], ebx / jne
// They must agree on the TLS index, the block and the perspective field at all three uses; the mode must be the
// field right after the perspective; the store's call must land on site 2; and the TLS index must be the engine
// TLS index the title already bound.
// ADDR-HYGIENE: resolved -- no address or offset ships; each site is matched uniquely in the whole module and the
// two are cross-checked (DecodeRecordSites). Unbound = no camera rung = the view-model rung.
inline constexpr const char* kSetModeStoreSitePattern =
    "E8 ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 65 48 8B 0C 25 58 00 00 00 4C 63 86 ?? ?? ?? ?? 41 B9 ?? ?? ?? ?? "
    "48 8B 0C D1 49 8B 14 09 4F 8D 0C 40 42 39 84 8A ?? ?? ?? ?? 75 0D 4B 8D 0C 40 39 AC 8A ?? ?? ?? ?? 74 32 "
    "42 89 84 8A ?? ?? ?? ?? 8B DF 48 63 86 ?? ?? ?? ?? 48 8D 0C 40 89 AC 8A ?? ?? ?? ?? 8B B6 ?? ?? ?? ?? "
    "8B D3 8B CE E8 ?? ?? ?? ??";
inline constexpr size_t kSetModeStoreSiteBytes = 111;
inline constexpr size_t kSetModeTlsDisp = 7, kSetModeTlsNext = 11, kSetModeUser = 23, kSetModeBlock = 29,
                        kSetModePerspective = 49, kSetModeMode = 62, kSetModePerspectiveStore = 72,
                        kSetModeUserSecond = 81, kSetModeModeStore = 92, kSetModeUserThird = 98,
                        kSetModeCallNotify = 106;
inline constexpr const char* kFirstPersonWeaponGatePattern =
    "48 89 5C 24 08 57 48 83 EC 20 44 8B 05 ?? ?? ?? ?? 33 DB 65 48 8B 04 25 58 00 00 00 48 63 F9 4E 8B 1C C0 "
    "4C 69 C7 ?? ?? ?? ?? B8 ?? ?? ?? ?? 4E 03 04 18 48 63 C2 4C 69 C8 ?? ?? ?? ?? 43 F6 44 01 ?? 01 74 ?? "
    "B8 ?? ?? ?? ?? 48 8D 0C 7F 4A 8B 04 18 39 9C 88 ?? ?? ?? ?? 75 ??";
inline constexpr size_t kFirstPersonWeaponGateBytes = 91;
inline constexpr size_t kGateTlsDisp = 13, kGateTlsNext = 17, kGateBlock = 70, kGatePerspective = 85;
inline constexpr uint32_t kRecordUserStride = 0xC;

// The camera modes the record holds, as HREK's RTTI names them (the same numbers in Halo 3's and Halo 4's
// set_camera_mode). Only the first-person camera from the first-person perspective is "first person".
inline constexpr int32_t kModeFollowing = 0, kModeFirstPerson = 3, kModeSandbox = 11;
inline constexpr int32_t kPerspectiveFirstPerson = 0;

struct RecordSites
{
    uint32_t blockOffset = 0;        // the director globals' pointer in engine TLS (Reach 0x188, Halo 4 0x150)
    uint32_t perspectiveOffset = 0;  // output user 0's perspective (Reach 0x6B4, Halo 4 0x744)
    uint32_t modeOffset = 0;         // and camera mode, the field after it
    int64_t tlsIndexRva = 0;         // the engine TLS index the sites load
};
enum class RecordBind : uint8_t
{
    Bound = 0,
    StoreSiteMissing,   // set_camera_mode's store: no match, or more than one
    GateMissing,        // the first-person weapons' gate: no match, or more than one
    CallMismatch,       // the store does not call the matched gate
    FieldMismatch,      // the sites disagree on the block or a field, or the mode is not the field after
    Implausible,        // an offset out of range or misaligned
    TlsMismatch,        // the sites' TLS index operands differ, or are not the engine TLS index
    Count
};
inline const char* RecordBindName(RecordBind why)
{
    switch (why)
    {
    case RecordBind::Bound: return "bound";
    case RecordBind::StoreSiteMissing: return "set_camera_mode's store site is missing or ambiguous";
    case RecordBind::GateMissing: return "the first-person weapon gate is missing or ambiguous";
    case RecordBind::CallMismatch: return "set_camera_mode does not call the matched first-person weapon gate";
    case RecordBind::FieldMismatch: return "the two sites disagree on the record";
    case RecordBind::Implausible: return "the decoded offsets are implausible";
    case RecordBind::TlsMismatch: return "a TLS index operand is not the engine TLS index";
    default: return "?";
    }
}
// `store` / `gate`: the unique matches (null = missing or ambiguous) with their offsets from the module base.
// `engineTlsIndexRva`: what the title bound independently.
inline RecordBind DecodeRecordSites(const uint8_t* store, int64_t storeRva, const uint8_t* gate, int64_t gateRva,
                                    int64_t engineTlsIndexRva, RecordSites& out)
{
    if (!store)
        return RecordBind::StoreSiteMissing;
    if (!gate)
        return RecordBind::GateMissing;
    if (CallRva(store, kSetModeCallNotify, storeRva) != gateRva)
        return RecordBind::CallMismatch;
    const uint32_t block = ReadU32(store + kSetModeBlock), persp = ReadU32(store + kSetModePerspective);
    const uint32_t mode = ReadU32(store + kSetModeMode), user = ReadU32(store + kSetModeUser);
    if (block != ReadU32(gate + kGateBlock) || persp != ReadU32(gate + kGatePerspective) ||
        persp != ReadU32(store + kSetModePerspectiveStore) || mode != ReadU32(store + kSetModeModeStore) ||
        mode != persp + 4 || user != ReadU32(store + kSetModeUserSecond) ||
        user != ReadU32(store + kSetModeUserThird))
        return RecordBind::FieldMismatch;
    if (!block || block >= 0x1000 || (block & 7) || !persp || persp >= 0x10000 || (persp & 3) ||
        mode + 4 - persp > kRecordUserStride)
        return RecordBind::Implausible;
    const int64_t tls = RipRva(store, kSetModeTlsDisp, kSetModeTlsNext, storeRva);
    if (tls != RipRva(gate, kGateTlsDisp, kGateTlsNext, gateRva) || tls != engineTlsIndexRva)
        return RecordBind::TlsMismatch;
    out.blockOffset = block;
    out.perspectiveOffset = persp;
    out.modeOffset = mode;
    out.tlsIndexRva = tls;
    return RecordBind::Bound;
}

// ---- Reach / Halo 4: the game's own "this player is Forge's Monitor" predicate ----------------------------------
// The predicate's head (the same in both titles): bool is_sandbox_editor(int player, int* heldObjectOut) --
//   prologue / xor r9b, r9b / mov r10d, ecx / mov rbx, rdx / cmp ecx, -1 / je / gs:[0x58] /
//   mov r8d, [rip + TLS index] / mov esi, GAME-ENGINE GLOBALS
// and c_game_director::update's call site, which resolves its output user's player and unit and then asks it with
// a null out-pointer (Reach keeps the answer and uses it to choose the sandbox camera; Halo 4 drops it):
//   Reach:  call PLAYER / mov ecx, ebx / mov esi, eax / call UNIT / ... / cmp esi, -1 / je / xor edx, edx /
//           mov ecx, esi / call PREDICATE / mov [rbp - ..], al
//   Halo 4: call PLAYER / mov ecx, r9d / mov esi, eax / call UNIT / ... / cmp esi, -1 / je / xor edx, edx /
//           mov ecx, esi / call PREDICATE
// PLAYER must have the output-user getter's body (a leaf: slots[TLS index] -> [tls + block] -> [.. + user*4 +
// field]); UNIT must be the unit getter the title already calls, where it has bound one.
// ADDR-HYGIENE: resolved -- the predicate's head and the director's call site are each matched uniquely; the call
// must land on that head; the getter must have the getter's body; every TLS operand must be the engine TLS index
// (DecodeEditorSites). Unbound or faulted = the Monitor answer is "unknown" and a first-person camera also needs
// the view model.
inline constexpr const char* kSandboxEditorPredicateHeadPattern =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 45 32 C9 44 8B D1 48 8B DA 83 F9 FF 0F 84 ?? ?? ?? ?? "
    "65 48 8B 04 25 58 00 00 00 44 8B 05 ?? ?? ?? ?? BE ?? ?? ?? ??";
inline constexpr size_t kSandboxEditorPredicateHeadBytes = 54;
inline constexpr size_t kEditorPredicateTlsDisp = 45, kEditorPredicateTlsNext = 49;
inline constexpr const char* kOutputUserPlayerGetterPattern =
    "83 C8 FF 83 F9 03 77 27 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 ?? ?? ?? ?? 48 63 C9 "
    "48 8B 04 D0 4A 8B 04 00 8B 84 88 ?? ?? ?? ?? C3";
inline constexpr size_t kOutputUserPlayerGetterBytes = 48;
inline constexpr size_t kPlayerGetterTlsDisp = 10, kPlayerGetterTlsNext = 14;
struct EditorCallSite
{
    const char* pattern;
    size_t bytes, callPlayer, callUnit, callPredicate;
};
inline constexpr EditorCallSite kReachEditorCallSite{
    "E8 ?? ?? ?? ?? 8B CB 8B F0 E8 ?? ?? ?? ?? 45 32 E4 C6 45 ?? 00 44 88 65 ?? 44 8B F8 83 FE FF 74 0C "
    "33 D2 8B CE E8 ?? ?? ?? ?? 88 45 ??",
    45, 0, 9, 37};
inline constexpr EditorCallSite kHalo4EditorCallSite{
    "E8 ?? ?? ?? ?? 41 8B C9 8B F0 E8 ?? ?? ?? ?? 45 33 ED 44 8B F0 44 88 6D ?? 45 8A E5 44 88 6D ?? "
    "41 BF 08 00 00 00 41 8D 5D 01 83 FE FF 74 ?? 33 D2 8B CE E8 ?? ?? ?? ??",
    56, 0, 10, 51};

struct EditorSites
{
    int64_t predicateRva = 0;     // is_sandbox_editor(player, out)
    int64_t playerGetterRva = 0;  // player index by output user
    int64_t unitGetterRva = 0;    // unit index by output user (the call site's second call)
};
enum class EditorBind : uint8_t
{
    Bound = 0,
    PredicateMissing,   // the predicate's head: no match, or more than one
    CallSiteMissing,    // the director's call site: no match, or more than one
    TargetMismatch,     // the call site does not call the matched predicate
    UnitGetterMismatch, // the call site's unit getter is not the one the title bound
    PlayerGetterBody,   // the call site's player getter does not have the getter's body
    PlayerGetterSame,   // the player getter is the unit getter
    TlsMismatch,        // a TLS index operand is not the engine TLS index
    Count
};
inline const char* EditorBindName(EditorBind why)
{
    switch (why)
    {
    case EditorBind::Bound: return "bound";
    case EditorBind::PredicateMissing: return "the predicate's head is missing or ambiguous";
    case EditorBind::CallSiteMissing: return "the director's call site is missing or ambiguous";
    case EditorBind::TargetMismatch: return "the director does not call the matched predicate";
    case EditorBind::UnitGetterMismatch: return "the call site's unit getter is not the bound one";
    case EditorBind::PlayerGetterBody: return "the player getter's body does not match";
    case EditorBind::PlayerGetterSame: return "the player getter is the unit getter";
    case EditorBind::TlsMismatch: return "a TLS index operand is not the engine TLS index";
    default: return "?";
    }
}
// `predicate` / `callSite`: the unique matches (null = missing or ambiguous). `playerGetter`: the bytes at the call
// site's first target (null when it is outside the module); `playerGetterBodyMatches`: the caller ran
// kOutputUserPlayerGetterPattern there. `boundUnitGetterRva`: the unit getter the title bound independently, or a
// negative value where it has none to compare.
inline EditorBind DecodeEditorSites(const EditorCallSite& shape, const uint8_t* predicate, int64_t predicateRva,
                                    const uint8_t* callSite, int64_t callSiteRva, const uint8_t* playerGetter,
                                    bool playerGetterBodyMatches, int64_t boundUnitGetterRva,
                                    int64_t engineTlsIndexRva, EditorSites& out)
{
    if (!predicate)
        return EditorBind::PredicateMissing;
    if (!callSite)
        return EditorBind::CallSiteMissing;
    if (CallRva(callSite, shape.callPredicate, callSiteRva) != predicateRva)
        return EditorBind::TargetMismatch;
    const int64_t unitGetterRva = CallRva(callSite, shape.callUnit, callSiteRva);
    if (boundUnitGetterRva >= 0 && unitGetterRva != boundUnitGetterRva)
        return EditorBind::UnitGetterMismatch;
    const int64_t playerGetterRva = CallRva(callSite, shape.callPlayer, callSiteRva);
    if (!playerGetter || !playerGetterBodyMatches)
        return EditorBind::PlayerGetterBody;
    if (playerGetterRva == unitGetterRva)
        return EditorBind::PlayerGetterSame;
    if (RipRva(predicate, kEditorPredicateTlsDisp, kEditorPredicateTlsNext, predicateRva) != engineTlsIndexRva ||
        RipRva(playerGetter, kPlayerGetterTlsDisp, kPlayerGetterTlsNext, playerGetterRva) != engineTlsIndexRva)
        return EditorBind::TlsMismatch;
    out.predicateRva = predicateRva;
    out.playerGetterRva = playerGetterRva;
    out.unitGetterRva = unitGetterRva;
    return EditorBind::Bound;
}

// ---- Halo 2: director_get_perspective's inputs ---------------------------------------------------------------
// Site 1, director_get_perspective(user) whole (retail 0x6D4EC0):
//   sub rsp, 0x28 / movsxd rax, ecx / lea r8, [rip + DIRECTOR GLOBALS] / imul rcx, rax, STRIDE /
//   lea rdx, [rip + first_person_camera_update] / mov rax, [rcx + r8 + UPDATE] / cmp rax, rdx / jne /
//   movss xmm1, [rcx + r8 + TRANSITION] / xorps xmm0, xmm0 / ucomiss xmm1, xmm0 / jp / jne / xor eax, eax / ret /
//   lea rcx, [rip + following_camera_update] / cmp rax, rcx / jne / mov eax, 1 / ret ...
// Site 2, an independent WRITER of the same record -- the director switching a user to the first-person camera
// (retail 0x6D55B5): imul rbx, rax, STRIDE / ... / lea rdi, [rip + DIRECTOR GLOBALS] / ... /
//   call first_person_camera_initialize / mov dword [rbx + rdi + ..], 1.0f /
//   lea rax, [rip + first_person_camera_update] / mov [rbx + rdi + UPDATE], rax / mov byte [rbx + rdi + ..], 0
// They must agree on the globals, the stride, the update field and the first-person update function.
// ADDR-HYGIENE: resolved -- no address or offset ships; both sites are matched uniquely and cross-checked
// (DecodeHalo2DirectorSites). Unbound = no camera rung = Halo 2's first-person palette packet rung.
inline constexpr const char* kHalo2DirectorPerspectivePattern =
    "48 83 EC 28 48 63 C1 4C 8D 05 ?? ?? ?? ?? 48 69 C8 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 4A 8B 44 01 ?? "
    "48 3B C2 75 18 F3 42 0F 10 4C 01 ?? 0F 57 C0 0F 2E C8 7A ?? 75 ?? 33 C0 48 83 C4 28 C3 "
    "48 8D 0D ?? ?? ?? ?? 48 3B C1 75 0A B8 01 00 00 00 48 83 C4 28 C3";
inline constexpr size_t kHalo2DirectorPerspectiveBytes = 84;
inline constexpr size_t kH2PerspGlobalsDisp = 10, kH2PerspGlobalsNext = 14, kH2PerspStride = 17,
                        kH2PerspFirstPersonDisp = 24, kH2PerspFirstPersonNext = 28, kH2PerspUpdate = 32,
                        kH2PerspTransition = 44, kH2PerspFollowingDisp = 65, kH2PerspFollowingNext = 69;
inline constexpr const char* kHalo2DirectorFirstPersonStorePattern =
    "48 69 D8 ?? ?? ?? ?? 33 C0 48 89 7C 24 20 48 8D 3D ?? ?? ?? ?? 48 8D 4F ?? 48 03 CB 66 89 44 3B ?? "
    "E8 ?? ?? ?? ?? C7 84 3B ?? ?? ?? ?? 00 00 80 3F 48 8D 05 ?? ?? ?? ?? 48 89 44 3B ?? C6 84 3B ?? ?? ?? ?? 00";
inline constexpr size_t kHalo2DirectorFirstPersonStoreBytes = 69;
inline constexpr size_t kH2StoreStride = 3, kH2StoreGlobalsDisp = 17, kH2StoreGlobalsNext = 21,
                        kH2StoreFirstPersonDisp = 52, kH2StoreFirstPersonNext = 56, kH2StoreUpdate = 60;

struct Halo2DirectorSites
{
    int64_t perspectiveFnRva = 0;        // director_get_perspective
    int64_t globalsRva = 0;              // the per-user director records (static, in the module's data)
    int64_t firstPersonUpdateRva = 0;    // first_person_camera_update
    int64_t followingUpdateRva = 0;      // following_camera_update (perspective 1)
    uint32_t stride = 0;                 // one user's record
    uint32_t updateOffset = 0;           // the camera's update function
    uint32_t transitionOffset = 0;       // the transition timer (non-zero while the camera blends)
};
enum class Halo2DirectorBind : uint8_t
{
    Bound = 0,
    PerspectiveMissing,  // director_get_perspective: no match, or more than one
    StoreSiteMissing,    // the first-person store: no match, or more than one
    GlobalsMismatch,     // the two sites address different records
    FieldMismatch,       // the stride or the update field differ
    UpdateMismatch,      // the first-person update functions differ, or it is the following one
    Implausible,         // a stride or offset out of range or misaligned
    Count
};
inline const char* Halo2DirectorBindName(Halo2DirectorBind why)
{
    switch (why)
    {
    case Halo2DirectorBind::Bound: return "bound";
    case Halo2DirectorBind::PerspectiveMissing: return "director_get_perspective is missing or ambiguous";
    case Halo2DirectorBind::StoreSiteMissing: return "the first-person camera store site is missing or ambiguous";
    case Halo2DirectorBind::GlobalsMismatch: return "the two sites address different director records";
    case Halo2DirectorBind::FieldMismatch: return "the two sites disagree on the record's layout";
    case Halo2DirectorBind::UpdateMismatch: return "the two sites disagree on the first-person update function";
    case Halo2DirectorBind::Implausible: return "the decoded layout is implausible";
    default: return "?";
    }
}
inline Halo2DirectorBind DecodeHalo2DirectorSites(const uint8_t* perspective, int64_t perspectiveRva,
                                                 const uint8_t* store, int64_t storeRva, Halo2DirectorSites& out)
{
    if (!perspective)
        return Halo2DirectorBind::PerspectiveMissing;
    if (!store)
        return Halo2DirectorBind::StoreSiteMissing;
    const int64_t globals = RipRva(perspective, kH2PerspGlobalsDisp, kH2PerspGlobalsNext, perspectiveRva);
    if (globals != RipRva(store, kH2StoreGlobalsDisp, kH2StoreGlobalsNext, storeRva))
        return Halo2DirectorBind::GlobalsMismatch;
    const uint32_t stride = ReadU32(perspective + kH2PerspStride);
    const uint32_t update = perspective[kH2PerspUpdate], transition = perspective[kH2PerspTransition];
    if (stride != ReadU32(store + kH2StoreStride) || update != store[kH2StoreUpdate])
        return Halo2DirectorBind::FieldMismatch;
    const int64_t firstPerson =
        RipRva(perspective, kH2PerspFirstPersonDisp, kH2PerspFirstPersonNext, perspectiveRva);
    const int64_t following = RipRva(perspective, kH2PerspFollowingDisp, kH2PerspFollowingNext, perspectiveRva);
    if (firstPerson != RipRva(store, kH2StoreFirstPersonDisp, kH2StoreFirstPersonNext, storeRva) ||
        firstPerson == following)
        return Halo2DirectorBind::UpdateMismatch;
    // The offsets are signed bytes in the instructions: both sit in the record's first 0x80 bytes.
    if (!stride || stride > 0x1000 || (stride & 7) || update >= 0x80 || (update & 7) || transition >= 0x80 ||
        (transition & 3) || update + 8 > stride || transition + 4 > stride || globals <= 0 || firstPerson <= 0)
        return Halo2DirectorBind::Implausible;
    out.perspectiveFnRva = perspectiveRva;
    out.globalsRva = globals;
    out.firstPersonUpdateRva = firstPerson;
    out.followingUpdateRva = following;
    out.stride = stride;
    out.updateOffset = update;
    out.transitionOffset = transition;
    return Halo2DirectorBind::Bound;
}
// director_get_perspective's own test for 0: the user's camera update IS first_person_camera_update and the
// transition timer is exactly zero. A record with no update function yet (between levels) is no answer.
inline Signal Halo2PerspectiveSignal(bool bound, bool read, uint64_t update, uint64_t firstPersonUpdate,
                                     float transition)
{
    if (!bound)
        return Signal::Unavailable;
    if (!read || !update || !firstPersonUpdate)
        return Signal::Unread;
    return update == firstPersonUpdate && transition == 0.0f ? Signal::Yes : Signal::No;
}

// ---- Value agreement: the camera rung against the view model it replaces ------------------------------------
// The record is decoded from the game's own instructions, but "mode 3 = the first-person camera" is a meaning,
// and a meaning can move in a title update while every signature still matches (an enum gains a member; the
// field next door is read). The view-model witness is the independent reference: whenever the first-person view
// model IS being drawn for a player on foot and in control, the camera must say first person. So:
//   * the two agreeing kProven times proves the rung for the title generation (it reads the right field, with
//     the right meaning), and nothing later condemns it;
//   * a rung that has never agreed and says "not first person" kCondemn times in a row while the view model is
//     drawn is condemned for the generation: the title falls back to the view-model rung,
//     said once. Unverified is not known-bad: until either count is reached the rung is used.
// Only frames where the view model is drawn tell; an unarmed player says nothing either way.
struct RungTrust
{
    static constexpr uint32_t kProven = 30, kCondemn = 600;
    enum class Step : uint8_t { None, Proven, Condemned };

    uint32_t agreed = 0, disagreedRun = 0;
    bool condemned = false;

    void Reset() noexcept { *this = RungTrust{}; }
    bool Proven() const noexcept { return agreed >= kProven; }
    Step Observe(Signal camera, Signal viewModel) noexcept
    {
        if (condemned || viewModel != Signal::Yes)
            return Step::None;
        if (camera == Signal::Yes)
        {
            disagreedRun = 0;
            if (agreed < kProven)
                return ++agreed == kProven ? Step::Proven : Step::None;
            return Step::None;
        }
        if (camera != Signal::No || agreed >= kProven)
            return Step::None;
        if (++disagreedRun < kCondemn)
            return Step::None;
        condemned = true;
        return Step::Condemned;
    }
};
} // namespace director_camera
