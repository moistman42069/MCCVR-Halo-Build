// HALO 2 (halo2.dll, the engine both of its renderers draw from): THE MOTION TRACKER FOLLOWS THE HEAD, the pure
// half (no game, engine or OpenXR includes).
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED. docs/EDITING-KIT-EVIDENCE.md's Halo 2 rule begins every
// new binding with the official kit; this one was made without it (no editing-kit symbols were available).
// Everything below was read from retail halo2.dll (SHA-256 DE65B4F4...CF171946) alone, so every
// name is ours and nothing here is a kit fact. No other title's finding is used as evidence: where the shape
// resembles another title's that is a lead for where to look, nothing more. Two things stand in for the kit:
//   * the hook proves the one doubtful fact at RUN TIME before it changes anything (ArgumentProof, below);
//   * docs/HALO2-HEAD-RELATIVE-2026-10-07.md lists, per binding, the kit artefact that would settle it.
//
// WHAT RETAIL DOES (tools/verify-halo2-head-relative.py re-derives every address from the patterns here).
// The motion sensor's state is one game-state allocation ("motion sensor (radar)", 0xBC8 bytes): four users of
// 0x2F0 bytes -- ten history slots of 0x44 (sixteen 4-byte blips and a count), sixteen tracked object indices --
// then the ring's cursor. Once per game TICK, not per rendered frame (game_update -> the HUD update -> the tracker
// update +0x830A60), the update steps the cursor and calls the SAMPLER (+0x82FE80) for each playing local user.
// The sampler asks
//     +0x6C3940(user)  ->  *[player control globals] + user * 0xB8 + 0x38       ; a 25-byte leaf
// for a POINTER to that user's desired angles -- the record this mod's direct drive writes the hand into
// (E-H2-81) -- reads ONE float through it (the yaw) and builds, in registers only,
//     a = -(yaw + pi/2);   S = (float)sin(a);   C = (float)cos(a)               ; the double imports
// then for every tracked object, d = the object's centre (+0x30, +0x34) minus the unit's camera position:
//     x = (S * dy - C * dx) / range * 64 + 64        y = (C * dy + S * dx) / range * 64 + 64
// each clamped to [0, 128] and stored as a BYTE in the cursor's history slot. So x = d . right and
// y = -(d . forward): the tracker's "up" is the control yaw. The draw (+0x831470) paints the stored bytes and
// reads no heading; there is no vehicle branch and no camera read.
//
// THE SEAM. The pair (S, C) never leaves the sampler's registers: there is no heading argument and no stored
// heading to replace. The tracker's copy of the heading is what it reads through the accessor's pointer. So the
// detour sits on the accessor and, for the ONE call that returns into the sampler and for local user 0, hands
// back a pointer to a private pair {the head's yaw in the record's convention, the record's own pitch}. Every
// other call (the accessor has three other callers) gets the game's own pointer. No game memory is written by
// this lane at all: not player control, not the aim, not a camera, not the tracker's state.
//
// ADDR-HYGIENE: resolved + guarded. No address and no offset ships: the accessor (the hook), the sampler from its
// entry through its sine and cosine, the placement arithmetic and the update's per-user loop are each matched
// uniquely; the sampler's call must land on the matched accessor, the loop's call on the sampler's entry, the
// placement must lie inside the sampler, the constants must be pi/2 and the sign mask, and the accessor's record
// (slot, stride, offset) must be the one the direct drive bound on its own from the accumulator. And at run time
// the float behind the pointer must AGREE with the yaw this mod's own direct drive is writing, over a reference
// that moved, before one value is replaced.
#pragma once

#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace halo2_head_relative
{
inline constexpr float kHalfPi = head_relative::kPi * 0.5f;
inline constexpr float kTwoPi = head_relative::kPi * 2.0f;
// Blam's world up. ADDR-HYGIENE: structural (the engine's axis convention, not a measurement of a build).
inline constexpr float kWorldUp[3]{0.0f, 0.0f, 1.0f};

// ---- the sites -------------------------------------------------------------------------------------------------
// ADDR-HYGIENE: resolved (the comment above). The numbers beside each pattern are positions INSIDE that matched
// pattern, never image addresses (structural; the unit tests check that each lands on the bytes its comment names).
//
// The accessor, whole (retail +0x6C3940; the hook): movsxd rax, ecx / imul rcx, rax, STRIDE /
// mov rax, [rip + PLAYER CONTROL GLOBALS] / add rax, YAW / add rax, rcx / ret.
// ADDR-HYGIENE: resolved + guarded. The stride (0xB8) and the yaw offset (0x38) are bytes of the pattern itself, so
// an image it matches has them; the globals slot is read from the match. All three must then EQUAL the record the
// direct drive bound on its own (DecodeTrackerSites: RecordMismatch), or nothing binds.
inline constexpr const char* kAnglesAccessorPattern =
    "48 63 C1 48 69 C8 B8 00 00 00 48 8B 05 ?? ?? ?? ?? 48 83 C0 38 48 03 C1 C3";
inline constexpr size_t kAccessorBytes = 25;
inline constexpr size_t kAccessorStride = 6, kAccessorSlotDisp = 13, kAccessorSlotNext = 17, kAccessorYaw = 20;

// The sampler, from its entry through its sine, its cosine and its call for the unit's camera position (retail
// +0x82FE80):
//   prologue / mov rax, [rip + TRACKER DATA] / movsxd rbp, ecx (the user) / imul r14, rbp, USER BYTES /
//   .. movsxd rax, [rax + CURSOR] / imul r15, rax, SLOT BYTES / call USER IS PLAYING / .. call PLAYER OF USER /
//   .. (no unit: je EXIT) .. / mov ecx, ebp / .. / call ACCESSOR / xorps xmm6, xmm6 / movss xmm0, [rax] /
//   addss xmm0, [rip + HALF PI] / xorps xmm0, [rip + SIGN MASK] / cvtss2sd xmm6, xmm0 / movaps xmm0, xmm6 /
//   call SIN / .. cvtsd2ss xmm10, xmm0 / movaps xmm0, xmm6 / call COS / mov rax, [rip + HUD GLOBALS] / ..
//   cvtsd2ss xmm11, xmm0 / divss xmm9, [rax + RANGE] / call UNIT CAMERA POSITION
// So xmm10 = S, xmm11 = C, xmm9 = 1 / range, and the pointer in rax is read exactly once: SIN, an import, follows.
// ADDR-HYGIENE: resolved + guarded. Positions inside the matched pattern. What they read is held against the other
// sites before anything binds: the call at kSamplerCallAngles must land on the matched accessor (CallMismatch), the
// two rip operands must hold pi/2 and the sign mask (ConstantMismatch), the cursor and the "user is playing" call
// must be the update loop's (LoopMismatch), the range must be the placement's (RangeMismatch), and the two trig
// calls must be the C runtime's sin then cos (halo2_head_relative.inl). kSamplerReturn is the instruction after
// that call (the static_assert below): the one return address the detour acts on.
inline constexpr const char* kTrackerSamplerPattern =
    "48 89 5C 24 18 48 89 6C 24 20 57 41 56 41 57 48 81 EC 90 00 00 00 48 8B 05 ?? ?? ?? ?? 48 63 E9 4C 69 F5 F0 02 "
    "00 00 8B CD 4C 03 F0 48 63 80 C0 0B 00 00 4C 6B F8 44 4D 03 FE E8 ?? ?? ?? ?? 33 DB 84 C0 74 2D 48 8B 05 ?? ?? "
    "?? ?? 48 8B 48 48 48 8D 3C 01 48 85 C9 75 02 8B FB 8B CD E8 ?? ?? ?? ?? 0F B7 C8 48 69 C1 24 02 00 00 8B 7C 38 "
    "2C EB 05 BF FF FF FF FF 41 89 5F 40 83 FF FF 0F 84 22 02 00 00 4C 89 A4 24 B8 00 00 00 8B CD 0F 29 B4 24 80 00 "
    "00 00 44 0F 29 4C 24 50 44 0F 29 54 24 40 44 0F 29 5C 24 30 E8 ?? ?? ?? ?? 0F 57 F6 F3 0F 10 00 F3 0F 58 05 ?? "
    "?? ?? ?? 0F 57 05 ?? ?? ?? ?? F3 0F 5A F0 0F 28 C6 E8 ?? ?? ?? ?? 45 0F 57 D2 F2 44 0F 5A D0 0F 28 C6 E8 ?? ?? "
    "?? ?? 48 8B 05 ?? ?? ?? ?? 48 8D 54 24 20 F3 44 0F 10 0D ?? ?? ?? ?? 45 0F 57 DB 8B CF F2 44 0F 5A D8 F3 44 0F "
    "5E 88 90 02 00 00 E8 ?? ?? ?? ??";
inline constexpr size_t kSamplerBytes = 270;
inline constexpr size_t kSamplerDataDisp = 25, kSamplerDataNext = 29, kSamplerUserBytes = 35, kSamplerCursor = 47,
                        kSamplerSlotBytes = 54, kSamplerCallPlaying = 58, kSamplerCallPlayer = 93,
                        kSamplerExitJump = 126, kSamplerCallAngles = 168, kSamplerReturn = 173,
                        kSamplerHalfPiDisp = 184, kSamplerHalfPiNext = 188, kSamplerMaskDisp = 191,
                        kSamplerMaskNext = 195, kSamplerCallSin = 202, kSamplerCallCos = 219, kSamplerHudDisp = 227,
                        kSamplerHudNext = 231, kSamplerRange = 261, kSamplerCallOrigin = 265;
static_assert(kSamplerReturn == kSamplerCallAngles + 5, "the return address is the instruction after the E8 rel32");

// The placement, inside the sampler (retail +0x83000A): rsi = the object, [rsp + 0x20] = the unit's camera
// position, xmm10 = S, xmm11 = C, xmm9 = 1 / range, xmm8 = 64, xmm7 = 128, xmm6 = 0, r15 = the history slot:
//   dx = [rsi + 0x30] - origin.x / dy = [rsi + 0x34] - origin.y / .. range * range <= dx*dx + dy*dy: skip ..
//   x = (S * dy - C * dx) * (1 / range) * 64 + 64 / y = (C * dy + S * dx) * (1 / range) * 64 + 64 /
//   each clamped to [0, 128], truncated and stored as a byte at blip + 0 and blip + 1 / call CLASSIFY
// ADDR-HYGIENE: resolved + guarded. Positions inside the matched pattern; the arithmetic itself is the pattern's
// own bytes (its only wildcards are the HUD globals' displacement and the last call's target). The match must lie
// INSIDE the sampler, after its trig and before the exit its own
// "no unit" jump names (PlacementOutside), and must clip by the range the pair is divided by (RangeMismatch). It is
// matched to prove that the sampler's pair is what places contacts; the lane does not hook or patch it.
inline constexpr const char* kTrackerPlacementPattern =
    "F3 0F 10 66 30 F3 0F 10 6E 34 F3 0F 5C 64 24 20 F3 0F 5C 6C 24 24 48 8B 05 ?? ?? ?? ?? 0F 28 C4 F3 0F 10 90 90 "
    "02 00 00 0F 28 CD F3 0F 59 CD F3 0F 59 C4 F3 0F 59 D2 F3 0F 58 C8 F3 0F 58 CE 0F 2F D1 0F 86 8F 00 00 00 49 63 "
    "4F 40 41 0F 28 C3 F3 0F 59 C4 41 0F 28 DA F3 0F 59 DD 41 0F 28 D3 8D 41 01 F3 0F 59 D5 4D 8D 04 8F 41 89 47 40 "
    "F3 0F 5C D8 41 0F 28 C2 F3 0F 59 C4 F3 0F 58 D0 F3 41 0F 59 D9 F3 41 0F 59 D8 F3 41 0F 59 D1 F3 41 0F 58 D8 F3 "
    "41 0F 59 D0 0F 2F F3 F3 41 0F 58 D0 76 05 0F 28 CE EB 07 0F 28 CF F3 0F 5D CB 0F 2F F2 76 05 0F 28 C6 EB 07 0F "
    "28 C7 F3 0F 5D C2 F3 0F 2C C1 8B D7 8B CD 41 88 00 F3 0F 2C C0 41 88 40 01 E8 ?? ?? ?? ??";
inline constexpr size_t kPlacementBytes = 215;
inline constexpr size_t kPlacementHudDisp = 25, kPlacementHudNext = 29, kPlacementRange = 36,
                        kPlacementCallClassify = 210;

// The tracker update's ring step and per-user loop (retail +0x830C61): mov eax, [rcx + CURSOR] / test eax, eax /
// jne / mov eax, LAST SLOT / jmp / dec eax / mov [rcx + CURSOR], eax / .. / mov ecx, edi / call USER IS PLAYING /
// test al, al / je / mov ecx, edi / call SAMPLER / inc edi / cmp edi, USERS / jl
// ADDR-HYGIENE: resolved + guarded. Positions inside the matched pattern. Its call must land on the sampler's entry
// and its cursor and "user is playing" test must be the sampler's (LoopMismatch). It is matched to prove the
// sampler is called once per user from the update; the lane does not hook or patch it.
inline constexpr const char* kTrackerUpdateLoopPattern =
    "8B 81 C0 0B 00 00 85 C0 75 07 B8 09 00 00 00 EB 02 FF C8 89 81 C0 0B 00 00 66 0F 1F 44 00 00 8B CF E8 ?? ?? ?? "
    "?? 84 C0 74 07 8B CF E8 ?? ?? ?? ?? FF C7 83 FF 04 7C E7";
inline constexpr size_t kLoopBytes = 56;
inline constexpr size_t kLoopCursorRead = 2, kLoopLastSlot = 11, kLoopCursorWrite = 21, kLoopCallPlaying = 33,
                        kLoopCallSampler = 44, kLoopUsers = 53;

inline uint32_t ReadU32(const uint8_t* at)
{
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
inline int32_t ReadI32(const uint8_t* at)
{
    int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
// Where the E8 / E9 at `site + opcode` lands, as an offset from the module base (`siteRva` is the site's own).
inline int64_t BranchRva(const uint8_t* site, size_t opcode, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(opcode) + 5 + ReadI32(site + opcode + 1);
}
inline int64_t RipRva(const uint8_t* site, size_t disp, size_t next, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(next) + ReadI32(site + disp);
}

struct TrackerSites
{
    int64_t accessorRva = 0;      // the hook
    int64_t returnRva = 0;        // where the sampler's call of the accessor returns: the one call that is changed
    int64_t recordSlotRva = 0;    // the player control globals' pointer, as the accessor loads it
    uint32_t recordStride = 0;    // per user (0xB8)
    uint32_t yawOffset = 0;       // desired yaw in a user's record (0x38); the pitch is the float after it
    int64_t samplerRva = 0;       // the sampler's entry
    int64_t samplerExitRva = 0;   // its epilogue, from its own "no unit" jump
    int64_t placementRva = 0;     // the placement arithmetic inside it
    int64_t updateLoopRva = 0;    // the update's ring step and per-user loop
    int64_t dataSlotRva = 0;      // the tracker's game-state pointer
    uint32_t userBytes = 0;       // one user's state (0x2F0)
    uint32_t cursorOffset = 0;    // the ring's cursor in the data (0xBC0)
    uint32_t slotBytes = 0;       // one history slot (0x44)
    uint32_t ringSlots = 0;       // history slots in the ring (10)
    uint32_t users = 0;           // local users the update walks (4)
    int64_t hudGlobalsSlotRva = 0;  // the HUD globals' pointer (the range is read through it)
    uint32_t rangeOffset = 0;     // the tracker's range in the HUD globals (0x290)
    int64_t playingRva = 0;       // "this local user is playing"
    int64_t halfPiRva = 0, signMaskRva = 0;
    int64_t sinRva = 0, cosRva = 0;  // what the sampler calls for S and for C (import thunks in retail)
    int64_t originRva = 0;        // the unit's camera position (the tracker's origin; this lane leaves it alone)
};
enum class TrackerBind : uint8_t
{
    Bound = 0,
    AccessorMissing,      // the accessor: no match, or more than one
    SamplerMissing,       // the sampler's entry-through-trig: no match, or more than one
    PlacementMissing,     // the placement arithmetic: no match, or more than one
    LoopMissing,          // the update's per-user loop: no match, or more than one
    CallMismatch,         // the sampler's call does not land on the matched accessor
    PlacementOutside,     // the placement is not inside the sampler, after its trig
    LoopMismatch,         // the loop does not call the matched sampler, or the two disagree on the cursor / the user test
    RangeMismatch,        // the pair and the clip do not read the same range
    ConstantMismatch,     // the angle is not -(yaw + pi/2)
    RecordMismatch,       // the accessor does not hand out the record the direct drive bound
    Implausible,          // an operand out of range
    Count
};
inline const char* TrackerBindName(TrackerBind why)
{
    switch (why)
    {
    case TrackerBind::Bound: return "bound";
    case TrackerBind::AccessorMissing: return "the desired-angles accessor is missing or ambiguous";
    case TrackerBind::SamplerMissing: return "the tracker's sampler (entry through sine and cosine) is missing or ambiguous";
    case TrackerBind::PlacementMissing: return "the placement arithmetic is missing or ambiguous";
    case TrackerBind::LoopMissing: return "the tracker update's per-user loop is missing or ambiguous";
    case TrackerBind::CallMismatch: return "the sampler does not call the matched accessor";
    case TrackerBind::PlacementOutside: return "the placement arithmetic is not inside the sampler";
    case TrackerBind::LoopMismatch: return "the update's loop and the sampler do not agree (call target, cursor or user test)";
    case TrackerBind::RangeMismatch: return "the pair and the clip do not read the same range";
    case TrackerBind::ConstantMismatch: return "the angle is not -(yaw + pi/2)";
    case TrackerBind::RecordMismatch: return "the accessor does not hand out the record the direct drive writes";
    case TrackerBind::Implausible: return "the decoded operands are implausible";
    default: return "?";
    }
}
// The record the direct drive bound on its own (halo2_direct_drive.inl: the slot from the accumulator's prologue,
// the stride and the yaw offset it writes with). The accessor must hand out exactly that.
struct DriveRecord
{
    int64_t slotRva = 0;
    uint32_t stride = 0;
    uint32_t yawOffset = 0;
};
// `accessor`, `sampler`, `placement`, `loop`: the unique matches (null = missing or ambiguous) with their offsets
// from the module base; `halfPi` / `signMask`: the four bytes behind the sampler's two rip operands (null when
// unreadable); `drive`: the direct drive's own binding of the record.
inline TrackerBind DecodeTrackerSites(const uint8_t* accessor, int64_t accessorRva, const uint8_t* sampler,
                                      int64_t samplerRva, const uint8_t* placement, int64_t placementRva,
                                      const uint8_t* loop, int64_t loopRva, const uint8_t* halfPi,
                                      const uint8_t* signMask, const DriveRecord& drive, TrackerSites& out)
{
    if (!accessor)
        return TrackerBind::AccessorMissing;
    if (!sampler)
        return TrackerBind::SamplerMissing;
    if (!placement)
        return TrackerBind::PlacementMissing;
    if (!loop)
        return TrackerBind::LoopMissing;
    if (BranchRva(sampler, kSamplerCallAngles, samplerRva) != accessorRva)
        return TrackerBind::CallMismatch;
    // The sampler's own "no unit" jump names its epilogue: the placement lies between the trig and that.
    const int64_t exitRva =
        samplerRva + static_cast<int64_t>(kSamplerExitJump) + 6 + ReadI32(sampler + kSamplerExitJump + 2);
    if (placementRva < samplerRva + static_cast<int64_t>(kSamplerBytes) ||
        placementRva + static_cast<int64_t>(kPlacementBytes) > exitRva)
        return TrackerBind::PlacementOutside;
    const uint32_t cursor = ReadU32(sampler + kSamplerCursor);
    const int64_t playing = BranchRva(sampler, kSamplerCallPlaying, samplerRva);
    if (BranchRva(loop, kLoopCallSampler, loopRva) != samplerRva ||
        BranchRva(loop, kLoopCallPlaying, loopRva) != playing || ReadU32(loop + kLoopCursorRead) != cursor ||
        ReadU32(loop + kLoopCursorWrite) != cursor)
        return TrackerBind::LoopMismatch;
    const int64_t hudSlot = RipRva(sampler, kSamplerHudDisp, kSamplerHudNext, samplerRva);
    const uint32_t range = ReadU32(sampler + kSamplerRange);
    if (RipRva(placement, kPlacementHudDisp, kPlacementHudNext, placementRva) != hudSlot ||
        ReadU32(placement + kPlacementRange) != range)
        return TrackerBind::RangeMismatch;
    float halfPiValue = 0.0f;
    uint32_t maskValue = 0;
    if (!halfPi || !signMask)
        return TrackerBind::ConstantMismatch;
    std::memcpy(&halfPiValue, halfPi, sizeof(halfPiValue));
    std::memcpy(&maskValue, signMask, sizeof(maskValue));
    if (!(std::fabs(halfPiValue - kHalfPi) <= 1.0e-6f) || maskValue != 0x80000000u)
        return TrackerBind::ConstantMismatch;
    const int64_t slot = RipRva(accessor, kAccessorSlotDisp, kAccessorSlotNext, accessorRva);
    const uint32_t stride = ReadU32(accessor + kAccessorStride);
    const uint32_t yaw = accessor[kAccessorYaw];
    if (slot != drive.slotRva || stride != drive.stride || yaw != drive.yawOffset)
        return TrackerBind::RecordMismatch;
    const uint32_t userBytes = ReadU32(sampler + kSamplerUserBytes);
    const uint32_t slotBytes = sampler[kSamplerSlotBytes];
    const uint32_t ringSlots = ReadU32(loop + kLoopLastSlot) + 1;
    const uint32_t users = loop[kLoopUsers];
    // One float pair inside one user's record; the ring's slots inside one user's state; the cursor after the users.
    if (slot <= 0 || (yaw & 3) || stride < yaw + 8 || (stride & 3) || !slotBytes || !ringSlots || !users ||
        static_cast<uint64_t>(slotBytes) * ringSlots > userBytes || cursor < static_cast<uint64_t>(userBytes) * users ||
        (range & 3))
        return TrackerBind::Implausible;
    out.accessorRva = accessorRva;
    out.returnRva = samplerRva + static_cast<int64_t>(kSamplerReturn);
    out.recordSlotRva = slot;
    out.recordStride = stride;
    out.yawOffset = yaw;
    out.samplerRva = samplerRva;
    out.samplerExitRva = exitRva;
    out.placementRva = placementRva;
    out.updateLoopRva = loopRva;
    out.dataSlotRva = RipRva(sampler, kSamplerDataDisp, kSamplerDataNext, samplerRva);
    out.userBytes = userBytes;
    out.cursorOffset = cursor;
    out.slotBytes = slotBytes;
    out.ringSlots = ringSlots;
    out.users = users;
    out.hudGlobalsSlotRva = hudSlot;
    out.rangeOffset = range;
    out.playingRva = playing;
    out.halfPiRva = RipRva(sampler, kSamplerHalfPiDisp, kSamplerHalfPiNext, samplerRva);
    out.signMaskRva = RipRva(sampler, kSamplerMaskDisp, kSamplerMaskNext, samplerRva);
    out.sinRva = BranchRva(sampler, kSamplerCallSin, samplerRva);
    out.cosRva = BranchRva(sampler, kSamplerCallCos, samplerRva);
    out.originRva = BranchRva(sampler, kSamplerCallOrigin, samplerRva);
    return TrackerBind::Bound;
}

// ---- the heading -----------------------------------------------------------------------------------------------
// The record's yaw is counter-clockwise from +X seen from above (+Z up), in [0, 2 pi) as the game's own
// accumulator leaves it.
inline void ForwardOfYaw(float yaw, float out[3])
{
    out[0] = std::cos(yaw);
    out[1] = std::sin(yaw);
    out[2] = 0.0f;
}
inline float WrapPi(float angle)
{
    angle = std::fmod(angle, kTwoPi);
    if (angle > head_relative::kPi)
        angle -= kTwoPi;
    else if (angle <= -head_relative::kPi)
        angle += kTwoPi;
    return angle;
}
// A yaw in [0, 2 pi).
inline float WrapYaw(float yaw)
{
    yaw = std::fmod(yaw, kTwoPi);
    if (yaw < 0.0f)
        yaw += kTwoPi;
    return yaw < kTwoPi ? yaw : 0.0f;
}
// The tracker's blip space: 128 units across, its centre at 64; a contact at the edge of the range is 64 from it.
// ADDR-HYGIENE: not used at run time. These two, NativePair, TrackerPoint and TrackerBlip are the unit tests' MODEL
// of the game's arithmetic (the two acceptance checks are run on it); the hook replaces a yaw and never places a
// contact. tools/verify-halo2-head-relative.py reads both numbers from this header and holds them against the two
// constants the retail sampler loads (xmm8 = 64, xmm7 = 128) and the arithmetic against its bytes.
inline constexpr float kBlipCentre = 64.0f, kBlipSpan = 128.0f;
// The pair the sampler builds for a yaw (retail +0x82FF30..+0x82FF7B): through double sine and cosine, as the game.
inline void NativePair(float yaw, float& sinA, float& cosA)
{
    const double a = static_cast<double>(-(yaw + kHalfPi));
    sinA = static_cast<float>(std::sin(a));
    cosA = static_cast<float>(std::cos(a));
}
// WHERE THE GAME PUTS A CONTACT, before its clamp and its truncation (retail +0x830056..+0x8300A5): x to the
// tracker's right, y DOWN (a contact straight ahead has y below 64).
inline void TrackerPoint(const float offset[3], float yaw, float range, float out[2])
{
    float s = 0.0f, c = 0.0f;
    NativePair(yaw, s, c);
    const float inverse = 1.0f / range;
    out[0] = (s * offset[1] - c * offset[0]) * inverse * kBlipCentre + kBlipCentre;
    out[1] = (c * offset[1] + s * offset[0]) * inverse * kBlipCentre + kBlipCentre;
}
// ...and what it stores: false when the contact is outside the range (the game skips it), else the two bytes.
inline bool TrackerBlip(const float offset[3], float yaw, float range, uint8_t out[2])
{
    if (!(range * range > offset[1] * offset[1] + offset[0] * offset[0]))
        return false;
    float point[2];
    TrackerPoint(offset, yaw, range, point);
    for (int i = 0; i < 2; ++i)
    {
        const float clamped = 0.0f > point[i] ? 0.0f : (kBlipSpan < point[i] ? kBlipSpan : point[i]);
        out[i] = static_cast<uint8_t>(static_cast<int32_t>(clamped));
    }
    return true;
}
// The angle of a tracker point from the tracker's top, positive toward its LEFT.
inline float TrackerPointBearing(const float point[2])
{
    return std::atan2(-(point[0] - kBlipCentre), -(point[1] - kBlipCentre));
}

// ---- THE PROOF TAKEN AT RUN TIME -------------------------------------------------------------------------------
// The bytes say a float is read through the accessor's pointer and turned into the pair that places contacts.
// They cannot say, without the kit, what that float IS. So before the first substitution it must agree with a
// reference that does not come from the same read: the yaw this mod's own direct drive last WROTE on foot
// (halo2_head_relative.inl, Halo2TrackerAimReference). kProofSamples agreeing samples, and the reference must have
// MOVED among them: one agreeing sample whose reference lies between kProofSpreadRad and half a turn less
// kProofSpreadRad from the first one's. A constant agrees only within the tolerance of ONE heading, so it has no
// such sample. A MIRRORED yaw (2c - yaw) agrees within the tolerance of TWO headings, exactly half a turn apart,
// so it has none either: that is why a half turn does not count as movement. It matters when the reference makes
// exactly that half turn in one jump (a simulated runtime whose controller starts half a turn from the head does so
// on its first pose step): no sample lies between the two headings to disagree, as one would in a hand's sweep. A
// miss between the tolerance and the disagreement bound counts for nothing either way. Once proven, kProofWithdrawRun
// disagreements in a row withdraw it. A pass also settles the SIGN: the float is then the aim's yaw in the
// convention the drive writes, and the head's yaw is measured the same way (atan2(y, x) of a world forward).
// The sampler runs once a game tick: at the 60 ticks a second measured for Halo 2 (E-H2-28)
// thirty samples are half a second of play, and the withdrawal run a whole one.
// (These five are thresholds of the proof, not addresses or offsets: nothing here locates anything in the game.)
inline constexpr uint32_t kProofSamples = 30;
inline constexpr uint32_t kProofWithdrawRun = 60;
inline constexpr float kProofToleranceRad = 2.0f * head_relative::kDegToRad;
inline constexpr float kProofSpreadRad = 10.0f * head_relative::kDegToRad;
inline constexpr float kProofDisagreeRad = 10.0f * head_relative::kDegToRad;
static_assert(kProofSpreadRad > 2.0f * kProofToleranceRad,
              "neither a constant nor a mirrored yaw may have an agreeing sample inside the band");
static_assert(kProofSpreadRad < head_relative::kPi * 0.25f, "the band of movement must not be empty or narrow");

struct ArgumentProof
{
    uint32_t agreed = 0;       // agreeing samples since the last disagreement (until proven)
    float anchorYaw = 0.0f;    // the reference of the first of them
    bool moved = false;        // one of them had its reference inside the band of movement, measured from the anchor
    uint32_t disagreed = 0;    // disagreements in a row (once proven)
    bool proven = false;
};
enum class ProofStep : uint8_t
{
    NoReference,  // nothing to compare with now: neither for nor against
    Agreed,
    Lagging,      // between the tolerance and the disagreement bound: neither for nor against
    Disagreed,
    Proven,       // this sample completed the proof
    Withdrawn,    // this sample withdrew it
    Count
};
inline ProofStep StepProof(ArgumentProof& proof, float gameYaw, bool referenceUsable, float referenceYaw)
{
    if (!referenceUsable || !std::isfinite(gameYaw) || !std::isfinite(referenceYaw))
        return ProofStep::NoReference;
    const float miss = std::fabs(WrapPi(gameYaw - referenceYaw));
    if (miss <= kProofToleranceRad)
    {
        proof.disagreed = 0;
        if (proof.proven)
            return ProofStep::Agreed;
        if (!proof.agreed)
        {
            proof.anchorYaw = referenceYaw;
            proof.moved = false;
        }
        // Movement: this far from the first agreeing reference, and not within the same margin of half a turn.
        const float away = std::fabs(WrapPi(referenceYaw - proof.anchorYaw));
        if (away >= kProofSpreadRad && away <= head_relative::kPi - kProofSpreadRad)
            proof.moved = true;
        ++proof.agreed;
        if (proof.agreed >= kProofSamples && proof.moved)
        {
            proof.proven = true;
            return ProofStep::Proven;
        }
        return ProofStep::Agreed;
    }
    if (miss < kProofDisagreeRad)
        return ProofStep::Lagging;
    if (!proof.proven)
    {
        proof = ArgumentProof{};
        return ProofStep::Disagreed;
    }
    if (++proof.disagreed >= kProofWithdrawRun)
    {
        proof = ArgumentProof{};
        return ProofStep::Withdrawn;
    }
    return ProofStep::Disagreed;
}

// ---- the verdict -----------------------------------------------------------------------------------------------
// One per call of the sampler's. Identity and freshness only; every reason has a name for the log. (A call of the
// accessor by anything but the sampler is not the tracker's and is counted apart: it is never changed.)
enum class TrackerUse : uint8_t
{
    Used = 0,
    NotLocalUser,       // the sampler's user is not local user 0
    Unproven,           // the float has not (yet) agreed with the aim this mod presented
    SwitchOff,          // the player's switch is off
    ControlNotRunning,  // the game has not run local user 0's control update lately: a cinematic holds the player
    NoSample,           // the observer has published no head camera yet, or the head pose was not live in it
    OtherGeneration,    // the publication belongs to another load of the title
    FromTheFuture,      // the publication's clock is ahead of this one's
    Stale,              // older than the age limit
    NoHeading,          // non-finite, or the head never had a heading
    Overlapped,         // user 0's call arrived while another call for user 0 was inside (the runtime's flag)
    WrittenThrough,     // the game once WROTE through the private copy it was handed: never handed again
    Count
};
inline const char* TrackerUseName(TrackerUse use)
{
    switch (use)
    {
    case TrackerUse::Used: return "used";
    case TrackerUse::NotLocalUser: return "not the local user";
    case TrackerUse::Unproven: return "unproven";
    case TrackerUse::SwitchOff: return "switch off";
    case TrackerUse::ControlNotRunning: return "control update not running";
    case TrackerUse::NoSample: return "no head sample";
    case TrackerUse::OtherGeneration: return "another generation";
    case TrackerUse::FromTheFuture: return "sample from the future";
    case TrackerUse::Stale: return "stale head sample";
    case TrackerUse::NoHeading: return "no heading";
    case TrackerUse::Overlapped: return "an overlapping call";
    case TrackerUse::WrittenThrough: return "the game wrote through the private copy (withdrawn)";
    default: return "?";
    }
}
// THE PRIVATE COPY the sampler is handed in place of the pointer into the player's own record. The static read
// says the sampler reads the yaw and the pitch through it, at once, and nothing else. Nothing at run time
// checks that, and a bare pair of floats would have the mod's own memory behind it. So the copy is the record's
// pair AND what follows it in the record (as much as lies inside the user's record, at most
// kShadowBytes), copied at each call, with only the yaw replaced: a read past the pair finds what the record
// holds. And the copy is compared with what was handed when the next call arrives on that thread: if it
// differs the game WROTE through the pointer, a write the record never got, and the substitution ends for
// this install (TrackerUse::WrittenThrough; the game's own pointer from then on).
inline constexpr size_t kShadowBytes = 32;
// How much of the record lies behind the yaw, up to kShadowBytes; never less than the pair itself.
inline size_t ShadowBytes(uint32_t recordStride, uint32_t yawOffset) noexcept
{
    const size_t behind = recordStride > yawOffset ? recordStride - yawOffset : 0;
    const size_t bytes = behind < kShadowBytes ? behind & ~size_t{3} : kShadowBytes;
    return bytes < 8 ? 8 : bytes;
}
// Fill the copy from the record (`bytes` of it) with the yaw replaced, and keep what was handed.
inline void FillShadow(float* shadow, float* handed, const float* record, size_t bytes, float yaw) noexcept
{
    std::memcpy(shadow, record, bytes);
    shadow[0] = yaw;
    std::memcpy(handed, shadow, bytes);
}
// The copy no longer holds what was handed: something wrote through the pointer. (Bytes, not floats: a NaN
// that was copied in compares equal to itself here.)
inline bool ShadowWritten(const float* shadow, const float* handed, size_t bytes) noexcept
{
    return bytes != 0 && std::memcmp(shadow, handed, bytes) != 0;
}
// The game runs the users' control update once per pass of its main-loop body, ahead of that pass's ticks (the
// sampler runs inside a tick), and skips the call this mod stamps while the director holds the player (a
// cinematic). So at the sampler a live stamp is a few milliseconds old; older than this, the player is not in
// control. (A threshold of ours: it locates nothing in the game.)
inline constexpr uint64_t kControlFreshMs = 120;
inline bool ControlFresh(uint64_t stampMs, uint64_t nowMs)
{
    return stampMs && nowMs >= stampMs && nowMs - stampMs <= kControlFreshMs;
}
struct TrackerRequest
{
    int32_t user = -1;         // the local user the sampler asked for
    bool proven = false;       // ArgumentProof::proven after this call's step
    bool switchOn = false;     // g_config.tracker_follows_head, read at this use
    bool controlFresh = false; // the game ran local user 0's control update within kControlFreshMs
    uint32_t generation = 0;   // the title generation now (0 when this hook is not that generation's)
    uint64_t nowUs = 0;        // the sight clock now
    int sign = 1;              // g_config.tracker_head_sign
    float offsetDeg = 0.0f;    // g_config.tracker_head_offset_deg
};
// The yaw the sampler should read. Used = `yawOut` is the record's yaw with the head's heading, in the record's
// own range [0, 2 pi); anything else names why the game's own yaw stands, and `yawOut` is untouched. `hold` keeps
// the last heading the head had, for a head looking straight up or down.
inline TrackerUse TrackerYaw(const TrackerRequest& request, const head_relative::HeadSample& sample,
                             head_relative::HeadingHold& hold, float nativeYaw, float& yawOut,
                             float* headYawOut = nullptr)
{
    using head_relative::Use;
    if (request.user != 0)
        return TrackerUse::NotLocalUser;
    if (!request.proven)
        return TrackerUse::Unproven;
    if (!request.switchOn)
        return TrackerUse::SwitchOff;
    if (!request.controlFresh)
        return TrackerUse::ControlNotRunning;
    switch (head_relative::SampleUse(sample, request.generation, request.nowUs))
    {
    case Use::Ok: break;
    case Use::OtherGeneration: return TrackerUse::OtherGeneration;
    case Use::FromTheFuture: return TrackerUse::FromTheFuture;
    case Use::Stale: return TrackerUse::Stale;
    case Use::NoHeading: return TrackerUse::NoHeading;
    default: return TrackerUse::NoSample;
    }
    if (!std::isfinite(nativeYaw))
        return TrackerUse::NoHeading;
    float headHeading[3], nativeForward[3];
    if (!head_relative::HeldHeading(hold, sample.forward, kWorldUp, headHeading))
        return TrackerUse::NoHeading;
    ForwardOfYaw(nativeYaw, nativeForward);
    float turn = 0.0f;
    if (!head_relative::YawToHead(nativeForward, headHeading, kWorldUp, request.sign,
                                  request.offsetDeg * head_relative::kDegToRad, turn))
        return TrackerUse::NoHeading;
    const float yaw = WrapYaw(nativeYaw + turn);
    if (!std::isfinite(yaw))
        return TrackerUse::NoHeading;
    yawOut = yaw;
    if (headYawOut)
        *headYawOut = std::atan2(headHeading[1], headHeading[0]);
    return TrackerUse::Used;
}
} // namespace halo2_head_relative
