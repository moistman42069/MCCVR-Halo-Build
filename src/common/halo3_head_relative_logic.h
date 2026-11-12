// HEAD-RELATIVE MOTION TRACKER, Halo 3 (and ODST, which carries the same code): the pure half of
// src/dll/halo3_head_relative.inl. No game, engine or OpenXR includes.
//
// WHAT THE GAME DOES (retail halo3.dll 1.3528, read from the retail binary only, no editing-kit symbols, so every
// name below is ours; tools/verify-halo3-motion-tracker.py re-derives the addresses from the patterns here).
//
//   The HUD update (0x2CF7F8) calls the motion tracker's per-user update (0x32DA98) with the user's tracker state
//   (inside the per-user HUD block, engine TLS +0xF0, stride 0x28C8; the output user index at state +0xCB0). That
//   update takes ONE heading: the player-control facing yaw of its user,
//       yaw = [engine TLS +0xC0] + user * 0x80 + 0x514          (ODST: [TLS +0xD8] + user * 0x230 + 0xBD4)
//   -- the angle direct drive steers with the hand, on foot and in every seat alike (no vehicle branch, no camera
//   read) -- and builds
//       a = -(yaw + pi/2);   S = sinf(a);   C = cosf(a)
//   which it hands to the two builders that place a contact:
//       0x32D9A8  a world point (objective marks):  arguments 5 and 6 = S, C
//       0x32D680  a tracked object:                 arguments 6 and 7 = S, C
//   Each computes, for the horizontal offset d = contact - unit (clamped to the range radially first),
//       blip.x = (d.y * S - d.x * C) / range        blip.y = (d.x * S + d.y * C) / range
//   With S = -cos(yaw) and C = -sin(yaw) that is blip.x = d . right, blip.y = -(d . forward), where
//   forward = (cos yaw, sin yaw) and right = (sin yaw, -cos yaw): the tracker's "up" is the control yaw's heading.
//   The draw (0x2F3904) only paints what the update stored.
//
// THE SEAM: the builders' (S, C) arguments, and nothing else. The detours on the two builders (their only caller
// is the update) replace the pair with the pair of the HEAD's heading. Nothing is read from or written to player
// control, the aim record, the camera or the observer; the builders' own outputs are the tracker's blips.
//
// THE SIGN is not a guess here: the substitution hands the game the same quantity it built (a world heading in
// its own convention), so a contact straight ahead of the head lands where a contact straight ahead of the aim
// landed before. tracker_head_sign / tracker_head_offset_deg stay as safety values a DEV build reads from the
// config at start-up (a player build always uses +1 and 0; its switch is the F1 checkbox).
#pragma once

#include "head_relative_logic.h"

#include <cmath>
#include <cstdint>

namespace halo3_head_relative
{
// Blam's world up. ADDR-HYGIENE: structural (the engine's axis convention, not a measurement of a build).
inline constexpr float kWorldUp[3]{0.0f, 0.0f, 1.0f};

// ---- the signatures (halo3.dll and halo3odst.dll: each matches exactly once in both; the verifier checks) ----
// ADDR-HYGIENE: resolved. No address or offset below is a constant of ours: every one is decoded from a pattern
// that must match exactly once, and each hook target is proven twice (the update's call operand AND the builder's
// own entry pattern at that address), with ValueAgreement on the rest (the install lists them).
// The integers beside each pattern (kUpdateUserDisp, kTrigCallA, ...) are positions INSIDE that matched pattern,
// not image offsets: ADDR-HYGIENE: structural (the unit tests check that each lands on the bytes its comment names).
//
// The tracker update's entry: ... mov ecx,[rcx+USER]; mov rsi,rdx; ...; call <unit of output user>; cmp eax,-1.
// USER (the output user index in the tracker state) is the disp32 at +41; the call's rel32 at +53.
inline constexpr char kUpdateEntryPattern[] =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 81 EC 80 00 00 00 48 8B F9 "
    "0F 29 70 D8 8B 89 ?? ?? 00 00 48 8B F2 0F 29 78 C8 E8 ?? ?? ?? ?? 83 F8 FF";
inline constexpr int kUpdateUserDisp = 41, kUpdateUnitCall = 52;
// The heading's trig, inside the update: addss xmm6,[pi/2]; xorps xmm6,[sign mask]; movaps xmm0,xmm6; call A;
// movaps xmm7,xmm0; movaps xmm0,xmm6; call B; ...; movaps xmm6,xmm0. So xmm7 = A(a), xmm6 = B(a), a = -(yaw+pi/2).
// pi/2 is the rel32 at +4, the mask at +11, call A at +18, call B at +29.
inline constexpr char kTrigPattern[] =
    "F3 0F 58 35 ?? ?? ?? ?? 0F 57 35 ?? ?? ?? ?? 0F 28 C6 E8 ?? ?? ?? ?? 0F 28 F8 0F 28 C6 E8 ?? ?? ?? ?? "
    "49 63 C4 48 8D 9F ?? ?? 00 00 48 69 C8 ?? ?? 00 00 0F 28 F0";
inline constexpr int kTrigHalfPi = 4, kTrigMask = 11, kTrigCallA = 18, kTrigCallB = 29;
// The update's call of the point builder: [rsp+0x28] = xmm6 (argument 6), [rsp+0x20] = xmm7 (argument 5).
inline constexpr char kPointCallPattern[] =
    "F3 0F 11 74 24 28 48 8D 56 ?? F3 0F 11 7C 24 20 4C 8D 4C 24 ?? 48 8B CF E8 ?? ?? ?? ?? 84 C0";
inline constexpr int kPointCall = 24;
// Its two calls of the object builder: [rsp+0x30] = xmm6 (argument 7), [rsp+0x28] = xmm7 (argument 6); argument 5
// is null (a tracked object) or a world point (an object the HUD globals list).
inline constexpr char kObjectCallAPattern[] =
    "F3 0F 11 74 24 30 4C 03 C3 F3 0F 11 7C 24 28 48 83 64 24 20 00 48 8B CF E8 ?? ?? ?? ?? 84 C0";
inline constexpr int kObjectCallA = 24;
inline constexpr char kObjectCallBPattern[] =
    "F3 0F 11 74 24 30 4C 03 C3 F3 0F 11 7C 24 28 83 CA FF 48 89 44 24 20 48 8B CF E8 ?? ?? ?? ?? 84 C0";
inline constexpr int kObjectCallB = 26;
// The point builder's entry, and its placement (argument 5 at [rsp+0x60], argument 6 at [rsp+0x68]).
inline constexpr char kPointEntryPattern[] =
    "48 8B C4 48 89 58 08 48 89 70 10 57 48 83 EC 30 F3 0F 10 12 49 8B F8 F3 41 0F 5C 11 F3 0F 10 5A 04 48 8B F1 "
    "F3 41 0F 5C 59 04";
inline constexpr char kPointPlacePattern[] =
    "0F 28 C2 0F 28 CB F3 0F 59 4C 24 60 B0 01 32 D8 F3 0F 59 54 24 60 F3 0F 59 44 24 68 8D 4B 05 02 D8 "
    "F3 0F 59 5C 24 68 F3 0F 5C C8 F3 0F 58 D3";
// The object builder's entry, and its placement (argument 6 at [rsp+0xA8], argument 7 at [rsp+0xB0]).
inline constexpr char kObjectEntryPattern[] =
    "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 4C 63 E2 33 DB "
    "41 83 FC FF 0F 29 70 C8 0F 29 78 B8 4D 8B F8 4C 8B F1";
inline constexpr char kObjectPlacePattern[] =
    "0F 28 C6 F3 0F 59 B4 24 A8 00 00 00 F3 0F 59 84 24 B0 00 00 00 0F 28 CF F3 0F 59 8C 24 A8 00 00 00 "
    "F3 0F 59 BC 24 B0 00 00 00 F3 0F 5C C8 F3 0F 58 F7";
// How far past its entry a site of the same function may lie (the update is 0x2DB bytes, the builders 0xEF and
// 0x327; a site further away is in another function). ADDR-HYGIENE: guarded -- a measurement of this build used
// only as an upper bound on top of the unique matches and their order; too small on another build fails closed.
inline constexpr uint32_t kUpdateExtent = 0x400, kPointExtent = 0x100, kObjectExtent = 0x340;

// ---- the model of the game's arithmetic ----
// The pair the update builds for a heading yaw (0x32DB31..0x32DB57).
inline void NativePair(float yaw, float& sinA, float& cosA)
{
    const float a = -(yaw + head_relative::kPi * 0.5f);
    sinA = std::sin(a);
    cosA = std::cos(a);
}
// The heading a pair stands for: forward = (-S, -C, 0). False (out untouched) when the pair is not finite or is
// not a unit pair (it is sin and cos of one angle, or it is not the tracker's pair).
inline bool PairForward(float sinA, float cosA, float out[3])
{
    if (!out || !std::isfinite(sinA) || !std::isfinite(cosA))
        return false;
    const float length = std::sqrt(sinA * sinA + cosA * cosA);
    if (!(std::fabs(length - 1.0f) <= 1.0e-3f))
        return false;
    out[0] = -sinA / length;
    out[1] = -cosA / length;
    out[2] = 0.0f;
    return true;
}
// The pair of a horizontal heading: S = -x, C = -y, normalised. False when it has no horizontal part.
inline bool ForwardPair(const float heading[3], float& sinA, float& cosA)
{
    if (!head_relative::Finite3(heading))
        return false;
    const float length = std::sqrt(heading[0] * heading[0] + heading[1] * heading[1]);
    if (!(length > 1.0e-6f))
        return false;
    sinA = -heading[0] / length;
    cosA = -heading[1] / length;
    return true;
}
// Where the builders put a contact at horizontal offset (dx, dy) from the unit (0x32D8FA, 0x32DA35): x to the
// right on the tracker, y DOWN (a contact straight ahead has y = -distance / range).
inline void Place(float dx, float dy, float sinA, float cosA, float invRange, float out[2])
{
    out[0] = (dy * sinA - dx * cosA) * invRange;
    out[1] = (dx * sinA + dy * cosA) * invRange;
}

// ---- the player's own blip ----
// The tracker update measures every contact from the unit's HEAD MARKER (it calls +0x355460, which asks for
// marker 0x9F), and its first object contact, slot 0 handed over with no point, is the player's own unit,
// measured from the object's own position (0x32D722: object +0x1C / +0x20 minus the origin). So the dot that
// stands for the player is drawn where the body is relative to the animated head, not at the centre.
// MEASURED (Halo 3, a 25 m tracker, walking): 0.40 m behind the centre with the gun level,
// 0.53 m with it pitched 50 degrees down, and the offset turns with whatever heading the tracker is given
// (0.21 m right / 0.33 m below with the head 60 degrees right). On screen the player's own dot sits below
// the centre.
//
// The builder takes its contact's position from `point` when one is given (0x32D74B), so handing it the
// ORIGIN as the point makes that one contact's offset exactly zero. Whether the blip is shown at all, and its
// type, still come from the unit (the slot keeps its tracked entry). No other contact is touched.
// Applies when the head-relative tracker is on for the local player; otherwise the game's own placement.
//
// WHICH CONTACT IS THE PLAYER'S OWN. NOT "slot 0". The update's loop (0x32DBED .. 0x32DCAF) hands the object
// builder each of the tracker state's TWENTY tracked entries in turn, slot 0 to 19, every one with no point;
// an entry is whatever unit began to move while that slot was free. A player alone in a game is the only
// mover and sits in slot 0, so a "slot == 0" test looks right there and is wrong.
// In a game with other players the player's own entry lapses whenever they stand still, and the
// next unit to move takes slot 0: a "slot == 0" rule draws THAT unit exactly at the centre (an enemy marker
// on the player's own position where there is no enemy).
// The own contact is the entry whose object handle IS the output user's unit: the handle
// is the entry's first dword (the builder: lea rsi, [rcx + slot * 12]; cmp dword [rsi], -1), and the unit is
// what the update itself asks the output-user unit getter for (0x32DACC). With no proven getter, or an entry
// layout that does not prove, no contact is centred: the game's own placement, the dot 0.4 m off.
inline constexpr int kTrackedEntries = 20;
inline constexpr size_t kTrackedEntryBytes = 12;
// The object builder's instructions right after its entry pattern, which fix that layout: lea r10, [r12 +
// r12 * 2]; lea rsi, [rcx + r10 * 4]; cmove rsi, rbx; je +9; cmp dword [rsi], -1. Identical in halo3.dll and
// halo3odst.dll. ADDR-HYGIENE: guarded -- compared at install; a mismatch only switches the own-blip rule off.
inline constexpr uint8_t kObjectSlotEntryBytes[17]{0x4F, 0x8D, 0x14, 0x64, 0x4A, 0x8D, 0x34, 0x91, 0x48,
                                                  0x0F, 0x44, 0xF3, 0x74, 0x09, 0x83, 0x3E, 0xFF};
inline bool SlotIsOwnUnit(int slot, int32_t entryHandle, int32_t ownUnit) noexcept
{
    return slot >= 0 && slot < kTrackedEntries && entryHandle != -1 && entryHandle == ownUnit;
}
inline const float* OwnBlipPoint(bool localSwitchedOn, bool slotIsOwnUnit, const float* point,
                                 const float* origin) noexcept
{
    return localSwitchedOn && slotIsOwnUnit && !point && origin ? origin : point;
}

// ---- the substitution ----
// The last heading the head had (looking straight up or down leaves none), per title generation.
struct Hold
{
    head_relative::HeadingHold heading;
    uint32_t generation = 0;
};

struct Request
{
    bool switchOn = false;        // g_config.tracker_follows_head, read live
    int32_t user = -1;            // the output user the tracker state belongs to
    bool sampleRead = false;      // a coherent head sample was read
    bool gameplay = false;        // the frame that published it was player-controlled (no cinematic)
    head_relative::HeadSample sample{};
    uint32_t generation = 0;      // the title generation now
    uint64_t nowUs = 0;           // the sight clock now
    int sign = 1;                 // g_config.tracker_head_sign
    float offsetRad = 0.0f;       // g_config.tracker_head_offset_deg, in radians
};

// The verdict on one contact placement and, when it is Use::Ok, the pair to hand the builder in sinA / cosA:
// the game's own pair turned about world up so its heading is the head's. Any other verdict leaves sinA / cosA
// exactly as the game made them. `nativeForward` / `usedForward` (optional, written only on Ok) are the two
// horizontal headings, for the log.
inline head_relative::Use Substitute(const Request& in, Hold& hold, float& sinA, float& cosA,
                                     float nativeForward[3] = nullptr, float usedForward[3] = nullptr)
{
    using head_relative::Use;
    if (!in.switchOn)
        return Use::SwitchOff;
    if (in.user != 0)
        return Use::NotLocalUser;
    if (!in.sampleRead)
        return Use::NoSample;
    const Use sampleUse = head_relative::SampleUse(in.sample, in.generation, in.nowUs);
    if (sampleUse != Use::Ok)
        return sampleUse;
    if (!in.gameplay)
        return Use::NotGameplay;
    float native[3];
    if (!PairForward(sinA, cosA, native))
        return Use::NoHeading;
    if (hold.generation != in.generation)
    {
        hold.heading = head_relative::HeadingHold{};
        hold.generation = in.generation;
    }
    float head[3], used[3];
    if (!head_relative::HeldHeading(hold.heading, in.sample.forward, kWorldUp, head) ||
        !head_relative::WithHeadYaw(native, head, kWorldUp, in.sign, in.offsetRad, used))
        return Use::NoHeading;
    float s = 0.0f, c = 0.0f;
    if (!ForwardPair(used, s, c))
        return Use::NoHeading;
    sinA = s;
    cosA = c;
    if (nativeForward)
    {
        nativeForward[0] = native[0];
        nativeForward[1] = native[1];
        nativeForward[2] = 0.0f;
    }
    if (usedForward)
    {
        usedForward[0] = used[0];
        usedForward[1] = used[1];
        usedForward[2] = 0.0f;
    }
    return Use::Ok;
}

// ====================================================================================================
// THE SOUND LISTENER BY THE HEAD'S ORIENTATION, Halo 3 (and ODST, which carries the same code).
//
// WHAT THE GAME DOES (retail halo3.dll 1.3528, read from the retail binary only; names ours;
// tools/verify-halo3-sound-listener.py re-derives it).
//
//   The sound manager keeps four listeners, one per output user, in its globals (a static pointer, halo3
//   +0xA49150): listener i at globals + 0xAC + i * 0x50 -- a location word at +4 (0xFFFF: no listener), a
//   4x3 matrix at +0xC (scale 1, forward +4, left +0x10, up +0x1C, position +0x28) and the listener-relative
//   velocity at +0x40. The listener update (0xAB650, one bool argument) rebuilds them from each user's
//   OBSERVER result (engine TLS +0x578, stride 0x3D0: position +0x11C, velocity +0x12C, forward +0x144, up
//   +0x150): the matrix helper 0x11F958 writes scale = 1, forward, left = up x forward, up; the caller stores
//   the position; 0x120C9C stores the velocity in that basis (v . forward, v . left, v . up). The game's own
//   spatialisation reads those records (the mixer is handed sources already in listener space).
//
//   ORDER, on the main-loop thread, inside the loop body 0xB2538: the listener update runs at +0xB2AAE and
//   +0xB2AC6 (before observer_update 0x17E608 at +0xB2AF2) and again at +0xB2B63 (inside the sound update
//   0xA9508, after it). The render -- where this mod writes the head into the observer result -- is called
//   after the whole body (+0xB2F58). So the pass's last listener update, the one the sound update then mixes
//   with, reads the camera observer_update has just rebuilt from the game's own aim: THE LISTENER TURNS WITH
//   THE GUN, not the head.
//
// THE SEAM: one detour on the listener update. After the game's own update it rewrites listener 0's basis
// (forward, left, up) with the head's and re-expresses the listener-relative velocity in the new basis, so the
// world velocity (Doppler) is unchanged. Position, location, scale and every other listener are left alone.
// Nothing but the sound manager's own listener record is written: no camera, no aim, nothing replicated.
// There is no sign to get wrong: the head's forward and up go into the slots the game filled from the
// observer's forward and up, through the game's own left = up x forward.

// ADDR-HYGIENE: resolved + guarded. No address and no record offset ships: the update's entry (the hook), the sound
// globals pointer and listener 0's location / built / matrix / velocity offsets are decoded from patterns that
// must each match exactly once, inside the update and in order; both helpers are proven twice (the build's call
// operand AND the helper's own whole body there); the record's layout must agree with the matrix's (the install
// lists it). The integers beside each pattern are positions INSIDE that matched pattern (structural; unit-tested).
//
// The listener update's entry (the hook target).
inline constexpr char kListenerEntryPattern[] =
    "88 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F8 FE FF FF 48 81 EC 08 02 00 00 "
    "E8 ?? ?? ?? ?? 45 33 DB 84 C0 0F 84 ?? ?? ?? ??";
// Inside it, the loop's start (r11d is zero since the entry's xor): mov rax,gs:[0x58]; lea r14d,[r11+1];
// mov edx,[rip+..]; mov r12d,r11d; mov esi,r11d; ...; mov edi,r11d; ...; mov r15d,r11d; mov r10d,r11d. The user
// index (r12d) and the listener's byte offset (r15) both start at zero.
inline constexpr char kListenerInitPattern[] =
    "65 48 8B 04 25 58 00 00 00 45 8D 73 01 8B 15 ?? ?? ?? ?? 45 8B E3 41 8B F3 4C 89 5C 24 ?? 41 8B FB "
    "4C 89 5C 24 ?? 45 8B FB 45 8B D3";
// Inside it, the top of the per-user loop: ...; or ebx,-1; mov r13,[rip+GLOBALS]; cmp r12d,ebx. The rel32 of
// the sound manager's globals pointer is at +20 (the instruction ends at +24).
inline constexpr char kListenerGlobalsPattern[] =
    "0F B7 98 ?? ?? 00 00 66 89 9D ?? ?? 00 00 83 CB FF 4C 8B 2D ?? ?? ?? ?? 44 3B E3 0F 84 ?? ?? ?? ??";
inline constexpr int kListenerGlobalsRel = 20;
// Inside it, where a listener that got a camera is marked built: mov r14d,1; movzx eax,r10w;
// mov [r15+r13+BUILT],r14b; cmp [rbp+..],r14w. BUILT at +14. The sound's own readers require that byte and a
// set location before they use a listener (0x15AD6C / 0x15AD7A), and so does this lane.
inline constexpr char kListenerBuiltPattern[] =
    "41 BE 01 00 00 00 41 0F B7 C2 47 88 B4 2F ?? ?? 00 00 66 44 39 B5 ?? ?? 00 00";
inline constexpr int kListenerBuiltDisp = 14;
// Inside it, one listener's build: lea r8,[up]; lea rcx,[r13+MATRIX]; mov [r15+r13+AUX],ax; add rcx,r15;
// lea rdx,[forward]; call <matrix from forward and up>; ...; lea r8,[r13+VELOCITY]; ...; movsd [rcx+0x28],pos;
// add r8,r15; mov [rcx+0x30],pos.z; call <vector into the matrix's basis>; ...; mov [r15+r13+LOCATION],bx.
// (r13 = the globals, r15 = user * stride.) MATRIX at +8, the matrix call at +29, VELOCITY at +43, the velocity
// call at +67, LOCATION at +86.
inline constexpr char kListenerBuildPattern[] =
    "4C 8D 44 24 30 49 8D 8D ?? ?? 00 00 66 43 89 84 2F ?? ?? 00 00 49 03 CF 48 8D 54 24 40 E8 ?? ?? ?? ?? "
    "F2 0F 10 44 24 20 4D 8D 85 ?? ?? 00 00 8B 44 24 28 48 8D 54 24 58 F2 0F 11 41 28 4D 03 C7 89 41 30 "
    "E8 ?? ?? ?? ?? 4C 8B 4D ?? 4C 8B 54 24 ?? 66 43 89 9C 2F ?? ?? 00 00";
inline constexpr int kListenerMatrixDisp = 8, kListenerMatrixCall = 29, kListenerVelocityDisp = 43,
                     kListenerVelocityCall = 67, kListenerLocationDisp = 86;
// Inside it, the loop's foot: add r10,OBSERVER; add rdi,..; add r12d,r14d; ...; add r15,STRIDE; ...;
// add rsi,OBSERVER; cmp r12d,4; jl. The observer stride at +3 and +31, the listener stride (one byte) at +22,
// the user count at +38: listener i belongs to output user i, and listener 0 starts at the array's base.
inline constexpr char kListenerLoopPattern[] =
    "49 81 C2 ?? ?? 00 00 48 83 C7 ?? 45 03 E6 4C 89 54 24 ?? 49 83 C7 ?? 48 89 7C 24 ?? 48 81 C6 ?? ?? 00 00 "
    "41 83 FC 04 0F 8C ?? ?? ?? ??";
inline constexpr int kListenerLoopObserverA = 3, kListenerLoopStride = 22, kListenerLoopObserverB = 31;
// The matrix helper, whole (0x11F958): [rcx] = 1.0f; [rcx+4] = *rdx (forward); [rcx+0x10] = *r8 x *rdx
// (left = up x forward); [rcx+0x1C] = *r8 (up); [rcx+0x28] = 0 (position).
inline constexpr char kListenerMatrixHelperPattern[] =
    "48 83 EC 28 C7 01 00 00 80 3F F2 0F 10 02 F2 0F 11 41 04 8B 42 08 89 41 0C F3 41 0F 10 58 08 F3 0F 10 52 08 "
    "0F 28 C3 F3 41 0F 10 20 0F 28 CA F3 0F 10 2A 0F 29 74 24 10 F3 41 0F 10 70 04 0F 29 3C 24 F3 0F 10 7A 04 "
    "F3 0F 59 C7 F3 0F 59 CE F3 0F 59 FC F3 0F 59 F5 F3 0F 5C C8 F3 0F 59 DD F3 0F 5C FE F3 0F 59 D4 "
    "0F 28 74 24 10 F3 0F 11 49 10 F3 0F 5C DA F3 0F 11 79 18 0F 28 3C 24 F3 0F 11 59 14 F2 41 0F 10 00 "
    "F2 0F 11 41 1C 41 8B 40 08 89 41 24 33 C0 48 89 41 28 89 41 30 48 83 C4 28 C3";
// The velocity helper, whole (0x120C9C): v = *rdx (divided by the matrix's scale unless it is 1);
// *r8 = (v . [rcx+4], v . [rcx+0x10], v . [rcx+0x1C]).
inline constexpr char kListenerVelocityHelperPattern[] =
    "48 83 EC 18 F3 0F 10 09 F3 0F 10 15 ?? ?? ?? ?? 0F 2E CA F3 0F 10 2A F3 0F 10 62 04 0F 29 34 24 "
    "F3 0F 10 72 08 7A 02 74 32 0F 2F 0D ?? ?? ?? ?? 72 0A F3 0F 5F 0D ?? ?? ?? ?? EB 0F F3 0F 10 05 ?? ?? ?? ?? "
    "F3 0F 5D C1 0F 28 C8 F3 0F 5E D1 F3 0F 59 EA F3 0F 59 E2 F3 0F 59 F2 0F 28 D4 0F 28 CE F3 0F 59 49 0C "
    "0F 28 DC 49 8B C0 F3 0F 59 51 08 0F 28 C5 F3 0F 59 41 04 F3 0F 58 D0 0F 28 C5 F3 0F 58 D1 0F 28 CE "
    "F3 41 0F 11 10 F3 0F 59 59 14 F3 0F 59 41 10 F3 0F 59 49 18 F3 0F 58 D8 F3 0F 58 D9 F3 41 0F 11 58 04 "
    "F3 0F 59 61 20 F3 0F 59 71 24 F3 0F 59 69 1C F3 0F 58 E5 F3 0F 58 E6 0F 28 34 24 F3 41 0F 11 60 08 "
    "48 83 C4 18 C3";
// The matrix's layout (bytes from the matrix's start). ADDR-HYGIENE: structural + guarded. These ARE fixed
// offsets, of the engine's 4x3 matrix: the matrix helper's whole-body pattern above writes exactly these fields
// (a build that lays the matrix out differently does not match it: StockFallback), and every rewrite first
// requires the live record to be such a matrix (scale exactly 1, rows orthonormal, left = up x forward).
inline constexpr int kMatrixScale = 0x00, kMatrixForward = 0x04, kMatrixLeft = 0x10, kMatrixUp = 0x1C,
                     kMatrixPosition = 0x28;
// ADDR-HYGIENE: guarded -- the update is 0x699 bytes here; an upper bound on top of the unique matches.
inline constexpr uint32_t kListenerUpdateExtent = 0x700;

// A listener's basis as the game's matrix helper builds it: forward, left = up x forward, up.
struct ListenerBasis
{
    float forward[3]{}, left[3]{}, up[3]{};
};
// The model of the matrix helper (0x11F958): forward and up as given, left = up x forward.
inline void BasisFromForwardUp(const float forward[3], const float up[3], ListenerBasis& out)
{
    for (int i = 0; i < 3; ++i)
    {
        out.forward[i] = forward[i];
        out.up[i] = up[i];
    }
    head_relative::Cross(up, forward, out.left);
}
// Unit, mutually perpendicular, and left = up x forward: what the game's helper makes of a camera's forward
// and up. Anything else is not a listener matrix (or not one this lane may rewrite).
inline bool BasisValid(const ListenerBasis& b, float tolerance = 2.0e-3f)
{
    if (!head_relative::Finite3(b.forward) || !head_relative::Finite3(b.left) || !head_relative::Finite3(b.up))
        return false;
    float cross[3];
    head_relative::Cross(b.up, b.forward, cross);
    return std::fabs(head_relative::Length(b.forward) - 1.0f) <= tolerance &&
           std::fabs(head_relative::Length(b.up) - 1.0f) <= tolerance &&
           std::fabs(head_relative::Dot(b.forward, b.up)) <= tolerance &&
           std::fabs(cross[0] - b.left[0]) <= tolerance && std::fabs(cross[1] - b.left[1]) <= tolerance &&
           std::fabs(cross[2] - b.left[2]) <= tolerance;
}
// The head's basis: forward normalised, up made perpendicular to it (the view's up is the head's own when the
// view rolls with the head, the game's level up when it does not), left = up x forward. False when either
// vector is not finite, forward is zero or up is along forward.
inline bool HeadBasis(const float headForward[3], const float headUp[3], ListenerBasis& out)
{
    if (!head_relative::Finite3(headForward) || !head_relative::Finite3(headUp))
        return false;
    const float length = head_relative::Length(headForward);
    if (!(length > 1.0e-6f))
        return false;
    const float forward[3]{headForward[0] / length, headForward[1] / length, headForward[2] / length};
    const float along = head_relative::Dot(headUp, forward);
    float up[3]{headUp[0] - forward[0] * along, headUp[1] - forward[1] * along, headUp[2] - forward[2] * along};
    const float upLength = head_relative::Length(up);
    if (!(upLength > 1.0e-3f))
        return false;
    for (float& component : up)
        component /= upLength;
    ListenerBasis basis;
    BasisFromForwardUp(forward, up, basis);
    if (!BasisValid(basis))
        return false;
    out = basis;
    return true;
}
// The model of the velocity helper (0x120C9C) at scale 1: a world vector in the listener's basis.
inline void ToListener(const ListenerBasis& b, const float world[3], float local[3])
{
    const float x = head_relative::Dot(world, b.forward), y = head_relative::Dot(world, b.left),
                z = head_relative::Dot(world, b.up);
    local[0] = x;
    local[1] = y;
    local[2] = z;
}
// Its inverse (the basis is orthonormal): a listener-relative vector back in the world.
inline void ToWorld(const ListenerBasis& b, const float local[3], float world[3])
{
    const float x = b.forward[0] * local[0] + b.left[0] * local[1] + b.up[0] * local[2];
    const float y = b.forward[1] * local[0] + b.left[1] * local[1] + b.up[1] * local[2];
    const float z = b.forward[2] * local[0] + b.left[2] * local[1] + b.up[2] * local[2];
    world[0] = x;
    world[1] = y;
    world[2] = z;
}

struct ListenerRequest
{
    bool switchOn = false;        // g_config.audio_follows_head, read live
    bool listener = false;        // the game has a listener for output user 0 (built, and its location is set)
    float scale = 0.0f;           // the game's matrix scale (1 as its helper writes it)
    bool sampleRead = false;      // a coherent head sample was read
    bool gameplay = false;        // the frame that published it was player-controlled (no cinematic)
    head_relative::HeadSample sample{};
    uint32_t generation = 0;      // the title generation now
    uint64_t nowUs = 0;           // the sight clock now
};

// The verdict on one listener update and, when it is Use::Ok, the basis and the listener-relative velocity to
// store: the head's basis, and the SAME world velocity expressed in it. `basis` / `velocity` come in as the
// game built them; any other verdict leaves both exactly so. NoHeading also covers a record that does not look
// like the game's listener matrix (scale not 1, basis not orthonormal with left = up x forward): the guard on
// the decoded layout.
inline head_relative::Use SubstituteListener(const ListenerRequest& in, ListenerBasis& basis, float velocity[3])
{
    using head_relative::Use;
    if (!in.switchOn)
        return Use::SwitchOff;
    if (!in.listener)
        return Use::NotLocalUser;
    if (!in.sampleRead)
        return Use::NoSample;
    const Use sampleUse = head_relative::SampleUse(in.sample, in.generation, in.nowUs);
    if (sampleUse != Use::Ok)
        return sampleUse;
    if (!in.gameplay)
        return Use::NotGameplay;
    ListenerBasis head;
    if (in.scale != 1.0f || !BasisValid(basis) || !head_relative::Finite3(velocity) ||
        !HeadBasis(in.sample.forward, in.sample.up, head))
        return Use::NoHeading;
    float world[3], local[3];
    ToWorld(basis, velocity, world);
    ToListener(head, world, local);
    if (!head_relative::Finite3(local))
        return Use::NoHeading;
    basis = head;
    velocity[0] = local[0];
    velocity[1] = local[1];
    velocity[2] = local[2];
    return Use::Ok;
}
} // namespace halo3_head_relative
