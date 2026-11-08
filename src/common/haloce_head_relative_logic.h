// HALO CE: THE MOTION TRACKER FOLLOWS THE HEAD, the pure half (no game, engine or OpenXR includes).
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT HCEEK-VERIFIED. docs/EDITING-KIT-EVIDENCE.md wants each title's
// bindings to come from its own editing kit; these do not. Everything below is read from retail halo1.dll alone
// (SHA-256 0A12DC56...2E6C, the image this repository pins for CE), with no editing-kit symbols, so every name
// is ours and nothing here is a kit fact. Three things stand in for the kit:
//   * nothing binds unless nine signatures each match once and agree with each other (DecodeTrackerSites);
//   * at EVERY use the float about to be replaced must be bit-identical to what the game's own store has just
//     computed (FreshStore), and before the FIRST replacement it must have agreed, at run time, with the heading
//     of the view frame this mod maps the room through (ViewProof);
//   * docs/HALOCE-HEAD-RELATIVE-2026-10-07.md lists, per binding, the kit artefact that would settle it.
// One link IS kit-derived: the yaw the sensor reads has the operands of the output user's facing reader
// (halo1 +0xA999E8), which this repository bound from HCEEK
// (contract::player_state, state_output_player; docs/HALOCE-UNIT-CONTROL-2026-09-16.md, "No camera feedback").
// That binding is taken as recorded there, not re-read against the kit here. Other titles' equivalents are a
// lead for where to look and are not evidence for anything here.
//
// WHAT RETAIL DOES (tools/verify-ce-head-relative.py re-derives every address from the patterns here).
// The HUD's unit interface (+0xC5093C) calls the sensor's draw entry (+0xBADFA0: local player, in multiplayer,
// the sensor's centre on screen) once per draw. The entry calls the UPDATE (+0xBAE530) and tail-jumps to the
// RENDER (+0xBAE890); each of the three has exactly that one direct reference.
//   sensor globals = [+0x2EA8740]: four local players x 0x568 (ten history datums x 0x84, then sixteen unit
//                    handles), then the ACTIVE datum's index (a word, +0x15A4) and an "updated" byte (+0x15A6)
//   one datum      : sixteen contacts (x, y as signed bytes, type, size), sixteen game-type points, the reference
//                    position (+0x70), the contact count (+0x78), THE HEADING (+0x7C), the point count (+0x80)
// UPDATE, on the local player's ACTIVE datum, when that player has a unit:
//   reference = the unit's camera position; every tracked unit's offset from it, in WORLD axes, as two bytes
//   heading   = [[+0x2D8FE70] + local * 0x38 + 0x17C] + pi/2            ; the player's control yaw
// RENDER walks the ten datums, newest first, and for each contact calls the PLACER (+0xBADD90; two call sites,
// nothing else calls it), which reads the datum's own heading and turns the stored offset by its negative:
//   a = -heading;  x' = cosf(a) * px - sinf(a) * py;  y' = cosf(a) * py + sinf(a) * px
// With heading = yaw + pi/2 that is x' = offset . left, y' = -(offset . forward); the rasterizer (+0xC4EF88)
// draws at (-x', -y') / 32, so a contact ahead of the yaw is at the sensor's top and one to its right at the
// sensor's right. A history datum keeps the heading it had while it was the active one (the tick, +0xBAE060,
// copies the previous datum into the new active one), so the trail is drawn in the frame of its own moment.
//
// THE YAW IS NOT THE HAND. [+0x2D8FE70] + user * 0x38 + 0x17C is the output user's facing: the pair the
// first-person camera's forward is made from (+0xA999E8 -> +0xAD0304: cos yaw cos pitch, sin yaw cos pitch,
// sin pitch), copied on every input pass (+0xA9AB00) from the control record the look update advances. In this
// mod nothing writes the hand into it for CE (the hand goes into a private copy of the unit-control packet, or
// into the outgoing network action); only the turn stick moves it. So in VR the stock sensor follows neither
// the gun nor the head: it stays aligned with the room's "forward" until a stick turn. "Head still, gun swept:
// contacts stay put" already holds. "Gun still, head turned: contacts turn" does not, and is what this changes.
//
// THE SEAM: one detour on the RENDER, the first reader. The game has just stored the heading and nothing has
// read it. For local player 0 the detour rewrites that one float of the active datum with the head's heading and
// forwards. The datum is the sensor's own: the game rewrites that float on every draw from machine-local player
// control, and nothing reads it but the placer and the tick's datum copy. Player control, the unit, the action,
// cameras and everything a shot or the network reads are never written; the control yaw is only READ.
//
// ADDR-HYGIENE: resolved + guarded. No address and no offset ships: the draw entry, the update's head, the store,
// the render's head, its datum and two call sites, the placer and the facing reader are each matched uniquely;
// the entry's call and jump must land on the matched update and render; the store must lie in the update and the
// three render sites in the render; both call sites must land on the matched placer; the update and the render
// must agree on the globals, the strides and the active index; the placer must read the displacement the store
// writes; the store's yaw must have the facing reader's operands; the constants must be pi/2 and the sign mask.
#pragma once

#include "haloce_frame_context.h"
#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace haloce_head_relative
{
inline constexpr float kHalfPi = head_relative::kPi * 0.5f;
inline constexpr float kTwoPi = head_relative::kPi * 2.0f;
inline constexpr float kWorldUp[3]{0.0f, 0.0f, 1.0f};

// ---- the sites -------------------------------------------------------------------------------------------------
// ADDR-HYGIENE: resolved (the comment above). The numbers beside each pattern are positions INSIDE that matched
// pattern, never image addresses. Every operand the lane uses at run time is a wildcard here and is decoded from
// the matched bytes.
// The draw entry (retail +0xBADFA0, the whole function): prologue / mov rbx, r8 / mov dil, dl / movzx esi, cx /
// call UPDATE / mov r8, rbx / mov dl, dil / movzx ecx, si / epilogue / jmp RENDER.
inline constexpr const char* kTrackerDrawPattern =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B D8 40 8A FA 0F B7 F1 E8 ?? ?? ?? ?? 4C 8B C3 40 8A "
    "D7 0F B7 CE 48 8B 5C 24 30 48 8B 74 24 38 48 83 C4 20 5F E9 ?? ?? ?? ??";
inline constexpr size_t kDrawBytes = 58;
inline constexpr size_t kDrawCallUpdate = 24, kDrawJumpRender = 53;
// The update's head (retail +0xBAE530): prologue / security cookie / movsx rax, cx / xor edi, edi /
//   imul r14, rax, PLAYER STRIDE / mov [rsp + SLOT], rax          ; the local player's index, kept for the store
//   mov rax, [rip + GLOBALS] / add r14, rax / cmp byte [rax + UPDATED], dil / je END
//   movzx edx, word [rax + ACTIVE] / .. / mov r13d, [players + local * 4 + UNIT] / ..
//   movsx rax, dx / imul rbx, rax, DATUM STRIDE / add rbx, r14     ; rbx = the local player's active datum
inline constexpr const char* kTrackerUpdateHeadPattern =
    "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC F0 00 00 "
    "00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 27 48 0F BF C1 33 FF 4C 69 F0 ?? ?? ?? ?? 48 89 44 24 ?? 48 "
    "8B 05 ?? ?? ?? ?? 4C 03 F0 40 38 B8 ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 0F B7 90 ?? ?? ?? ?? 83 CE FF 4C 0F "
    "BF E1 44 8B EE 44 89 65 97 89 74 24 20 44 3B E6 74 14 48 8B 05 ?? ?? ?? ?? 46 8B AC A0 ?? ?? ?? ?? 44 "
    "89 6C 24 20 48 0F BF C2 48 69 D8 ?? ?? ?? ?? 49 03 DE";
inline constexpr size_t kUpdateHeadBytes = 154;
inline constexpr size_t kUpdatePlayerStride = 58, kUpdateLocalSlot = 66, kUpdateGlobalsDisp = 70,
                        kUpdateGlobalsNext = 74, kUpdateFlagDisp = 80, kUpdateActiveDisp = 93,
                        kUpdateDatumStride = 147;
// The store (retail +0xBAE726), later in the update:
//   imul rcx, [rsp + SLOT], CONTROL STRIDE / mov rax, [rip + CONTROL] / .. /
//   movss xmm0, [rcx + rax + YAW] / addss xmm0, [rip + HALF PI] / movss [rbx + HEADING], xmm0
inline constexpr const char* kTrackerStorePattern =
    "48 6B 4C 24 ?? ?? 48 8B 05 ?? ?? ?? ?? 4C 63 65 97 44 8B 6C 24 20 F3 0F 10 84 01 ?? ?? ?? ?? F3 0F 58 "
    "05 ?? ?? ?? ?? F3 0F 11 43 ??";
inline constexpr size_t kStoreBytes = 44;
inline constexpr size_t kStoreLocalSlot = 4, kStoreControlStride = 5, kStoreControlDisp = 9, kStoreControlNext = 13,
                        kStoreYawDisp = 27, kStoreHalfPiDisp = 35, kStoreHalfPiNext = 39, kStoreInstruction = 39,
                        kStoreHeadingDisp = 43;
// The render's head (retail +0xBAE890, THE HOOK): prologue / mov r10, r8 / mov r11b, dl / movzx r9d, cx /
//   call (the user's director perspective) / sub ax, 2 / cmp ax, 1 / jbe END    ; perspectives 2 and 3: no sensor
//   movsx rax, r9w / .. / imul r14, rax, PLAYER STRIDE / mov rax, [rip + HUD GLOBALS] / .. / add r14, [rip + GLOBALS]
inline constexpr const char* kTrackerRenderHeadPattern =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 0F 29 70 "
    "C8 4D 8B D0 44 0F 29 40 B8 44 8A DA 44 0F 29 48 A8 44 0F B7 C9 E8 ?? ?? ?? ?? 66 83 E8 02 41 BC 01 00 "
    "00 00 66 41 3B C4 0F 86 ?? ?? ?? ?? 49 0F BF C1 48 8D 94 24 C8 00 00 00 4C 69 F0 ?? ?? ?? ?? 48 8B 05 "
    "?? ?? ?? ?? 45 8A C3 4C 03 35 ?? ?? ?? ??";
inline constexpr size_t kRenderHeadBytes = 116;
inline constexpr size_t kRenderPlayerStride = 95, kRenderGlobalsDisp = 112, kRenderGlobalsNext = 116;
// The render's choice of a history datum (retail +0xBAE94A):
//   mov rax, [rip + GLOBALS] / .. / movsx ecx, word [rax + ACTIVE] / (active - history + 10) mod 10 /
//   movsx rax, cx / imul rbx, rax, DATUM STRIDE / .. / add rbx, r14         ; rbx = that datum
inline constexpr const char* kTrackerRenderDatumPattern =
    "41 0F B7 F7 48 8B 05 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 44 0F BF C6 0F BF 88 ?? ?? ?? ?? B8 67 66 66 "
    "66 41 2B C8 41 03 CD F7 E9 C1 FA 02 8B C2 C1 E8 1F 03 D0 8D 04 92 03 C0 2B C8 48 0F BF C1 48 69 D8 ?? "
    "?? ?? ?? 41 8B C5 41 2B C0 49 03 DE";
inline constexpr size_t kRenderDatumBytes = 80;
inline constexpr size_t kDatumGlobalsDisp = 7, kDatumGlobalsNext = 11, kDatumActiveDisp = 26, kDatumStride = 67;
// The render's two calls of the placer, each with `mov rcx, rbx` (the datum) among its arguments: a tracked
// unit (retail +0xBAEA13) and one of the game type's own points (retail +0xBAEAC2).
inline constexpr const char* kTrackerUnitCallPattern =
    "48 8B 05 ?? ?? ?? ?? 4C 8D 8C 24 C8 00 00 00 44 8A 47 02 48 8B CB F3 44 0F 11 4C 24 30 F3 44 0F 11 44 "
    "24 28 F3 0F 10 90 D0 02 00 00 0F BE 47 FF F3 0F 11 74 24 20 66 0F 6E C0 0F BE 07 0F 5B C0 66 0F 6E C8 "
    "0F 5B C9 F3 0F 59 C2 F3 0F 59 CA F3 0F 5E 05 ?? ?? ?? ?? F3 0F 5E 0D ?? ?? ?? ?? F3 0F 11 84 24 C8 00 "
    "00 00 F3 0F 11 8C 24 CC 00 00 00 E8 ?? ?? ?? ??";
inline constexpr size_t kUnitCallBytes = 118;
inline constexpr size_t kUnitCall = 113;
inline constexpr const char* kTrackerPointCallPattern =
    "48 8B 05 ?? ?? ?? ?? 4C 8D 4C 24 40 F3 44 0F 11 4C 24 30 45 33 C0 F3 44 0F 11 44 24 28 48 8B CB F3 0F "
    "11 74 24 20 F3 0F 10 90 D0 02 00 00 0F BE 44 53 40 66 0F 6E C0 0F BE 44 53 41 B2 05 0F 5B C0 66 0F 6E "
    "C8 0F 5B C9 F3 0F 59 C2 F3 0F 59 CA F3 0F 5E 05 ?? ?? ?? ?? F3 0F 5E 0D ?? ?? ?? ?? F3 0F 11 44 24 40 "
    "F3 0F 11 4C 24 44 E8 ?? ?? ?? ??";
inline constexpr size_t kPointCallBytes = 113;
inline constexpr size_t kPointCall = 108;
// The placer (retail +0xBADD90), from its head through its turn of the offset:
//   .. / mov rax, [r9] / .. / movss xmm6, [rcx + HEADING] / xorps xmm6, [rip + SIGN MASK]     ; a = -heading
//   .. / mov [r11 + 8], rax (the offset: px at entry + 8, py at entry + 0xC) / call COS / movaps xmm8, xmm0 /
//   movaps xmm0, xmm6 / call SIN / ..
//   xmm1 = sin * px / xmm0 = sin * py / xmm9 = cos * py / xmm8 = cos * px
//   addss xmm9, xmm1 (y' = cos * py + sin * px) / subss xmm8, xmm0 (x' = cos * px - sin * py)
inline constexpr const char* kTrackerPlacerPattern =
    "4C 8B DC 49 89 5B 10 49 89 73 18 49 89 7B 20 41 56 48 81 EC 80 00 00 00 49 8B 01 41 8A D8 0F 29 74 24 "
    "70 40 8A F2 F3 0F 10 71 ?? 0F 57 35 ?? ?? ?? ?? 0F 29 7C 24 60 0F 28 C6 45 0F 29 43 C8 45 0F 29 4B B8 "
    "45 0F 29 53 A8 49 89 43 08 E8 ?? ?? ?? ?? 44 0F 28 C0 0F 28 C6 E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 0F "
    "28 C8 F3 0F 59 8C 24 90 00 00 00 45 0F 28 C8 F3 0F 59 84 24 94 00 00 00 F3 44 0F 59 8C 24 94 00 00 00 "
    "F3 44 0F 59 84 24 90 00 00 00 F3 44 0F 10 90 D0 02 00 00 F3 44 0F 58 C9 F3 44 0F 5C C0";
inline constexpr size_t kPlacerBytes = 165;
inline constexpr size_t kPlacerHeadingDisp = 42, kPlacerMaskDisp = 46, kPlacerMaskNext = 50, kPlacerCallCos = 77,
                        kPlacerCallSin = 89;
// The output user's facing reader (retail +0xA999F4, twelve bytes into the function at +0xA999E8, so this
// proof never reads that function's head):
//   mov rax, [rip + PLAYERS] / movsxd r8, ecx / mov r9d, [rax + r8 * 4 + 0xB8] /
//   mov rdx, [rip + CONTROL] / movsx rax, cx / add rdx, YAW / imul r8, rax, CONTROL STRIDE / mov ecx, r9d /
//   add rdx, r8 / mov r8, r10 / jmp (angles -> forward)
inline constexpr const char* kFacingReaderPattern =
    "48 8B 05 ?? ?? ?? ?? 4C 63 C1 46 8B 8C 80 B8 00 00 00 48 8B 15 ?? ?? ?? ?? 48 0F BF C1 48 81 C2 ?? ?? "
    "?? ?? 4C 6B C0 ?? 41 8B C9 49 03 D0 4D 8B C2 E9 ?? ?? ?? ??";
inline constexpr size_t kFacingBytes = 54;
inline constexpr size_t kFacingControlDisp = 21, kFacingControlNext = 25, kFacingYaw = 32, kFacingStride = 39,
                        kFacingJump = 49;

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

// What the install read from the image: each pattern's one match (null = missing or ambiguous), its offset from
// the module base, and what the image's own function table says about where it lies.
struct TrackerImage
{
    const uint8_t* draw = nullptr;       // kDrawBytes at the draw entry; drawRva must begin a function
    int64_t drawRva = 0;
    const uint8_t* update = nullptr;     // kUpdateHeadBytes at the update's head; must begin a function
    int64_t updateRva = 0;
    const uint8_t* store = nullptr;      // kStoreBytes
    int64_t storeRva = 0;
    int64_t storeFunctionRva = -1;       // the beginning of the function the store lies in
    const uint8_t* render = nullptr;     // kRenderHeadBytes at the render's head; must begin a function
    int64_t renderRva = 0;
    const uint8_t* datum = nullptr;      // kRenderDatumBytes
    int64_t datumRva = 0;
    int64_t datumFunctionRva = -1;
    const uint8_t* unitCall = nullptr;   // kUnitCallBytes
    int64_t unitCallRva = 0;
    int64_t unitCallFunctionRva = -1;
    const uint8_t* pointCall = nullptr;  // kPointCallBytes
    int64_t pointCallRva = 0;
    int64_t pointCallFunctionRva = -1;
    const uint8_t* placer = nullptr;     // kPlacerBytes at the placer's head; must begin a function
    int64_t placerRva = 0;
    const uint8_t* facing = nullptr;     // kFacingBytes
    int64_t facingRva = 0;
};
struct TrackerSites
{
    int64_t drawRva = 0, updateRva = 0, renderRva = 0;  // the draw entry, the update, the render (the hook)
    int64_t placerRva = 0;                              // places one contact; the only reader of the heading
    int64_t storeRva = 0;                               // the store instruction itself
    int64_t globalsRva = 0;                             // the image's pointer to the sensor globals
    uint32_t playerStride = 0, datumStride = 0;         // one local player's record; one history datum
    uint32_t activeOffset = 0, updatedOffset = 0;       // in the globals: the active datum's index; "updated"
    uint32_t headingOffset = 0;                         // in a datum
    int64_t controlRva = 0;                             // the image's pointer to player control
    uint32_t controlStride = 0, yawOffset = 0;          // the output user's facing yaw in it
    int64_t halfPiRva = 0, signMaskRva = 0;             // the constants the store adds and the placer negates with
    int64_t cosThunkRva = 0, sinThunkRva = 0;           // the placer's two trig calls, in that order
    bool operator==(const TrackerSites&) const = default;
};
enum class TrackerBind : uint8_t
{
    Bound = 0,
    DrawMissing,        // the draw entry: no match, or more than one, or not a function's beginning
    UpdateMissing,      // the update's head
    StoreMissing,       // the heading's store
    RenderMissing,      // the render's head
    DatumMissing,       // the render's choice of a history datum
    CallsMissing,       // one of the render's two calls of the placer
    PlacerMissing,      // the placer
    FacingMissing,      // the output user's facing reader
    EntryMismatch,      // the draw entry does not call the matched update and jump to the matched render
    StoreOutsideUpdate, // the store is not inside the function whose head matched
    SitesOutsideRender, // the datum or a call site is not inside the render, or they are out of order
    PlacerMismatch,     // a call site does not land on the matched placer
    LayoutMismatch,     // the update and the render disagree on the globals, a stride or the active index
    FieldMismatch,      // the placer does not read the displacement the store writes
    FacingMismatch,     // the store's yaw does not have the facing reader's operands
    Implausible,        // an operand out of range or misaligned
    ConstantMismatch,   // what the game adds is not pi/2, or what the placer negates with is not the sign mask
    Count
};
inline const char* TrackerBindName(TrackerBind why)
{
    switch (why)
    {
    case TrackerBind::Bound: return "bound";
    case TrackerBind::DrawMissing: return "the sensor's draw entry is missing, ambiguous or not a function";
    case TrackerBind::UpdateMissing: return "the sensor update's head is missing, ambiguous or not a function";
    case TrackerBind::StoreMissing: return "the heading's store is missing or ambiguous";
    case TrackerBind::RenderMissing: return "the sensor render's head is missing, ambiguous or not a function";
    case TrackerBind::DatumMissing: return "the render's choice of a history datum is missing or ambiguous";
    case TrackerBind::CallsMissing: return "a call of the contact placer is missing or ambiguous";
    case TrackerBind::PlacerMissing: return "the contact placer is missing, ambiguous or not a function";
    case TrackerBind::FacingMissing: return "the output user's facing reader is missing or ambiguous";
    case TrackerBind::EntryMismatch: return "the draw entry does not call the matched update and jump to the matched render";
    case TrackerBind::StoreOutsideUpdate: return "the heading's store is not inside the matched update";
    case TrackerBind::SitesOutsideRender: return "the datum choice and the two placer calls are not inside the matched render, in order";
    case TrackerBind::PlacerMismatch: return "a call site does not land on the matched placer";
    case TrackerBind::LayoutMismatch: return "the update and the render disagree on the sensor's layout";
    case TrackerBind::FieldMismatch: return "the placer does not read the heading the store writes";
    case TrackerBind::FacingMismatch: return "the sensor's yaw is not the output user's facing";
    case TrackerBind::Implausible: return "the decoded operands are implausible";
    case TrackerBind::ConstantMismatch: return "the constants are not pi/2 and the sign mask";
    default: return "?";
    }
}
// Decodes and cross-checks every site. Bound fills `out`, including where the two constants are; the caller then
// reads them and finishes with CheckTrackerConstants. Anything else fills nothing.
inline TrackerBind DecodeTrackerSites(const TrackerImage& image, TrackerSites& out)
{
    if (!image.draw)
        return TrackerBind::DrawMissing;
    if (!image.update)
        return TrackerBind::UpdateMissing;
    if (!image.store)
        return TrackerBind::StoreMissing;
    if (!image.render)
        return TrackerBind::RenderMissing;
    if (!image.datum)
        return TrackerBind::DatumMissing;
    if (!image.unitCall || !image.pointCall)
        return TrackerBind::CallsMissing;
    if (!image.placer)
        return TrackerBind::PlacerMissing;
    if (!image.facing)
        return TrackerBind::FacingMissing;
    if (BranchRva(image.draw, kDrawCallUpdate, image.drawRva) != image.updateRva ||
        BranchRva(image.draw, kDrawJumpRender, image.drawRva) != image.renderRva || image.updateRva == image.renderRva)
        return TrackerBind::EntryMismatch;
    if (image.storeFunctionRva != image.updateRva ||
        image.storeRva < image.updateRva + static_cast<int64_t>(kUpdateHeadBytes))
        return TrackerBind::StoreOutsideUpdate;
    if (image.datumFunctionRva != image.renderRva || image.unitCallFunctionRva != image.renderRva ||
        image.pointCallFunctionRva != image.renderRva ||
        image.datumRva < image.renderRva + static_cast<int64_t>(kRenderHeadBytes) ||
        image.unitCallRva < image.datumRva + static_cast<int64_t>(kRenderDatumBytes) ||
        image.pointCallRva < image.unitCallRva + static_cast<int64_t>(kUnitCallBytes))
        return TrackerBind::SitesOutsideRender;
    if (BranchRva(image.unitCall, kUnitCall, image.unitCallRva) != image.placerRva ||
        BranchRva(image.pointCall, kPointCall, image.pointCallRva) != image.placerRva)
        return TrackerBind::PlacerMismatch;

    TrackerSites sites;
    sites.playerStride = ReadU32(image.update + kUpdatePlayerStride);
    sites.datumStride = ReadU32(image.update + kUpdateDatumStride);
    sites.activeOffset = ReadU32(image.update + kUpdateActiveDisp);
    sites.updatedOffset = ReadU32(image.update + kUpdateFlagDisp);
    sites.globalsRva = RipRva(image.update, kUpdateGlobalsDisp, kUpdateGlobalsNext, image.updateRva);
    // Two independent functions on one layout: the update that stores and the render that reads.
    if (sites.playerStride != ReadU32(image.render + kRenderPlayerStride) ||
        sites.datumStride != ReadU32(image.datum + kDatumStride) ||
        sites.activeOffset != ReadU32(image.datum + kDatumActiveDisp) ||
        sites.globalsRva != RipRva(image.render, kRenderGlobalsDisp, kRenderGlobalsNext, image.renderRva) ||
        sites.globalsRva != RipRva(image.datum, kDatumGlobalsDisp, kDatumGlobalsNext, image.datumRva) ||
        // The slot the store takes the local player's index from is the one the head put it in.
        image.store[kStoreLocalSlot] != image.update[kUpdateLocalSlot])
        return TrackerBind::LayoutMismatch;
    sites.headingOffset = image.store[kStoreHeadingDisp];
    if (sites.headingOffset != image.placer[kPlacerHeadingDisp])
        return TrackerBind::FieldMismatch;
    sites.controlRva = RipRva(image.store, kStoreControlDisp, kStoreControlNext, image.storeRva);
    sites.controlStride = image.store[kStoreControlStride];
    sites.yawOffset = ReadU32(image.store + kStoreYawDisp);
    if (sites.controlRva != RipRva(image.facing, kFacingControlDisp, kFacingControlNext, image.facingRva) ||
        sites.controlStride != image.facing[kFacingStride] || sites.yawOffset != ReadU32(image.facing + kFacingYaw))
        return TrackerBind::FacingMismatch;
    sites.halfPiRva = RipRva(image.store, kStoreHalfPiDisp, kStoreHalfPiNext, image.storeRva);
    sites.signMaskRva = RipRva(image.placer, kPlacerMaskDisp, kPlacerMaskNext, image.placerRva);
    sites.cosThunkRva = BranchRva(image.placer, kPlacerCallCos, image.placerRva);
    sites.sinThunkRva = BranchRva(image.placer, kPlacerCallSin, image.placerRva);
    // A datum is a whole number of floats holding the heading; a player's record holds at least one datum; the
    // active index and the "updated" byte come after local player 0's record, the byte right after the index.
    if (!sites.datumStride || (sites.datumStride & 3) || sites.datumStride > 0x10000 || (sites.headingOffset & 3) ||
        sites.headingOffset + sizeof(float) > sites.datumStride || sites.playerStride < sites.datumStride ||
        (sites.playerStride & 3) || sites.playerStride > 0x100000 || sites.activeOffset < sites.playerStride ||
        (sites.activeOffset & 1) || sites.activeOffset > 0x1000000 ||
        sites.updatedOffset != sites.activeOffset + sizeof(int16_t) || !sites.controlStride ||
        (sites.controlStride & 3) || (sites.yawOffset & 3) || sites.yawOffset > 0x100000 || sites.globalsRva <= 0 ||
        sites.controlRva <= 0 || sites.controlRva == sites.globalsRva || sites.halfPiRva <= 0 ||
        sites.signMaskRva <= 0 || sites.cosThunkRva <= 0 || sites.sinThunkRva <= 0 ||
        sites.cosThunkRva == sites.sinThunkRva)
        return TrackerBind::Implausible;
    sites.drawRva = image.drawRva;
    sites.updateRva = image.updateRva;
    sites.renderRva = image.renderRva;
    sites.placerRva = image.placerRva;
    sites.storeRva = image.storeRva + static_cast<int64_t>(kStoreInstruction);
    out = sites;
    return TrackerBind::Bound;
}
// `halfPi`: the four bytes at sites.halfPiRva; `signMask`: the four at sites.signMaskRva (null = unreadable).
inline TrackerBind CheckTrackerConstants(const uint8_t* halfPi, const uint8_t* signMask)
{
    if (!halfPi || !signMask)
        return TrackerBind::ConstantMismatch;
    float value = 0.0f;
    std::memcpy(&value, halfPi, sizeof(value));
    if (!(std::fabs(value - kHalfPi) <= 1.0e-6f) || ReadU32(signMask) != 0x80000000u)
        return TrackerBind::ConstantMismatch;
    return TrackerBind::Bound;
}
// The active datum's index must name a datum inside one player's record.
inline bool ActiveDatumValid(const TrackerSites& sites, int32_t active)
{
    return active >= 0 && sites.datumStride &&
           (static_cast<uint64_t>(active) + 1) * sites.datumStride <= sites.playerStride;
}
// The heading of local player `localPlayer`'s datum `active`, as an offset into the sensor globals.
inline size_t HeadingOffset(const TrackerSites& sites, int32_t localPlayer, int32_t active)
{
    return static_cast<size_t>(localPlayer) * sites.playerStride + static_cast<size_t>(active) * sites.datumStride +
           sites.headingOffset;
}
// The output user's facing yaw, as an offset into player control.
inline size_t ControlYawOffset(const TrackerSites& sites, int32_t localPlayer)
{
    return static_cast<size_t>(localPlayer) * sites.controlStride + sites.yawOffset;
}

// ---- the heading -----------------------------------------------------------------------------------------------
// The stored field is the control yaw plus pi/2; a yaw is counter-clockwise from +X seen from above (+Z up).
inline float FieldFromYaw(float yaw) { return yaw + kHalfPi; }
inline float YawFromField(float field) { return field - kHalfPi; }
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
// The yaw of a horizontal heading. False when it has no horizontal part.
inline bool HeadingYaw(const float heading[3], float& yaw)
{
    if (!head_relative::Finite3(heading) || !(heading[0] * heading[0] + heading[1] * heading[1] > 1.0e-8f))
        return false;
    yaw = std::atan2(heading[1], heading[0]);
    return true;
}
// WHERE THE GAME PUTS A CONTACT, from the placer's own arithmetic (retail +0xBADDB6..+0xBADE35, before the range
// compression and the scale, which keep the direction):
//   a = -field;  x' = cosf(a) * px - sinf(a) * py;  y' = cosf(a) * py + sinf(a) * px
// With field = yaw + pi/2: x' = offset . left, y' = -(offset . forward). A contact straight ahead is (0, -r);
// one to the LEFT is (+r, 0). The rasterizer draws at (-x', -y') / 32, which puts ahead at the top.
inline void TrackerPoint(const float offset[3], float field, float out[2])
{
    const float a = -field;
    const float c = std::cos(a), s = std::sin(a);
    out[0] = c * offset[0] - s * offset[1];
    out[1] = c * offset[1] + s * offset[0];
}
// The angle of a placed contact from the sensor's top, positive toward its LEFT (the sense of
// head_relative::Bearing: a contact to the left of the heading is positive).
inline float TrackerPointBearing(const float point[2]) { return std::atan2(point[0], -point[1]); }

// THE GUARD AT EVERY USE (ADDR-HYGIENE: guarded, ValueAgreement with no tolerance). The game computes the
// heading as one single-precision add of the control yaw and its pi/2 constant. Repeating that add on the same
// two operands gives the same bits, so "the field is bit-identical to control yaw + pi/2" says two things at
// once: the game stored a heading in THIS call (it does not when the local player has no unit; the field then
// still holds an older value, perhaps one this lane wrote), and the field is the one the proof was taken on.
inline bool FreshStore(float field, float controlYaw, float halfPi)
{
    const float expected = controlYaw + halfPi;
    return std::memcmp(&field, &expected, sizeof(field)) == 0;
}

// ---- THE PROOF TAKEN AT RUN TIME -------------------------------------------------------------------------------
// The bytes say which float the placer turns contacts by and that it is the output user's facing yaw plus pi/2.
// What only a running game can say is that this yaw is the heading of the view frame this mod maps the room
// through, in the convention the head's yaw is measured in. So before the first substitution the field (minus
// pi/2) must agree with a reference that does not come from the same read: the heading of that view frame, as
// the render path published it with the head (on foot, in the player's own first-person view; BuildHeadFrame).
//
// CE's control yaw moves only when the player turns with the STICK, so the proof has two stages:
//   PROVISIONAL  kProofSamples agreeing samples, whatever the reference did. The lane starts substituting. At
//                rest a constant or a mirror could agree by chance; the exact guard above and the facing
//                reader's operands are what make that implausible, and the first stick turn settles it.
//   CONFIRMED    kProofSamples agreeing samples AND one of them between kProofSpreadRad and kProofAntipodeRad
//                from the first. A constant agrees only within the tolerance of one heading; a mirrored yaw
//                agrees only near two headings half a turn apart; neither has a sample in that band.
// kProofWithdrawRun disagreements in a row withdraw either stage; an agreement in between clears the run.
// After a withdrawal the proof starts from nothing and must reach CONFIRMED before the lane substitutes again:
// no second provisional pass (provisionalSpent).
// Before any stage, kProofRestartRun disagreements in a row start the count again; fewer do not, because the
// field and the published frame can be one frame apart, and a snap turn then gives one disagreeing sample.
// A miss between the tolerance and the disagreement bound counts for nothing either way.
// (These are thresholds of the proof, not addresses or offsets: nothing here locates anything in the game.)
inline constexpr uint32_t kProofSamples = 30;
inline constexpr uint32_t kProofWithdrawRun = 60;
inline constexpr uint32_t kProofRestartRun = 4;
inline constexpr float kProofToleranceRad = 2.0f * head_relative::kDegToRad;
inline constexpr float kProofDisagreeRad = 10.0f * head_relative::kDegToRad;
inline constexpr float kProofSpreadRad = 10.0f * head_relative::kDegToRad;
inline constexpr float kProofAntipodeRad = 170.0f * head_relative::kDegToRad;
static_assert(kProofSpreadRad > 2.0f * kProofToleranceRad, "a constant must not be able to agree across the spread");
static_assert(kProofAntipodeRad < head_relative::kPi - kProofToleranceRad,
              "a mirrored yaw's second agreeing heading must lie outside the confirming band");

enum class ProofStage : uint8_t
{
    Unproven = 0,
    Provisional,  // agreed at rest: substituting, to be confirmed by the first turn
    Confirmed,    // agreed across a turn
    Count
};
inline const char* ProofStageName(ProofStage stage)
{
    switch (stage)
    {
    case ProofStage::Unproven: return "pending";
    case ProofStage::Provisional: return "PROVISIONAL";
    case ProofStage::Confirmed: return "CONFIRMED";
    default: return "?";
    }
}
struct ViewProof
{
    ProofStage stage = ProofStage::Unproven;
    bool provisionalSpent = false;  // a proof was withdrawn in this image: only a CONFIRMED one substitutes again
    uint32_t agreed = 0;            // agreeing samples since the count last began
    float anchorYaw = 0.0f;         // the reference of the first of them
    float arc = 0.0f;               // the largest |reference - anchor| among them that is within the antipode bound
    uint32_t disagreed = 0;         // disagreements in a row
    // For the log: the last agreeing sample, and the first sample of the present run of disagreements.
    float lastAgreedGame = 0.0f, lastAgreedReference = 0.0f;
    float firstDisagreedGame = 0.0f, firstDisagreedReference = 0.0f;
};
// May the lane replace the heading on this proof?
inline bool ProofAllows(const ViewProof& proof) { return proof.stage != ProofStage::Unproven; }
// What a reinstall on the same loaded image keeps of a proof: a CONFIRMED one, and the fact that a provisional
// pass was already spent. A provisional or unfinished proof is taken again.
inline ViewProof ProofKeptAcrossReinstall(const ViewProof& proof)
{
    ViewProof kept;
    kept.provisionalSpent = proof.provisionalSpent;
    if (proof.stage == ProofStage::Confirmed)
    {
        kept = proof;
        kept.disagreed = 0;
    }
    return kept;
}
enum class ProofStep : uint8_t
{
    NoReference,  // nothing to compare with now: neither for nor against
    Agreed,
    Lagging,      // between the tolerance and the disagreement bound: neither for nor against
    Disagreed,    // counted; nothing changed yet
    Restarted,    // this disagreement began the unproven count again
    Provisional,  // this sample made the proof provisional
    Confirmed,    // this sample confirmed it
    Withdrawn,    // this sample withdrew it
    Count
};
inline ProofStep StepProof(ViewProof& proof, float gameYaw, bool referenceUsable, float referenceYaw)
{
    if (!referenceUsable || !std::isfinite(gameYaw) || !std::isfinite(referenceYaw))
        return ProofStep::NoReference;
    const float miss = std::fabs(WrapPi(gameYaw - referenceYaw));
    if (miss <= kProofToleranceRad)
    {
        proof.disagreed = 0;
        proof.lastAgreedGame = gameYaw;
        proof.lastAgreedReference = referenceYaw;
        if (proof.stage == ProofStage::Confirmed)
            return ProofStep::Agreed;
        if (!proof.agreed)
        {
            proof.anchorYaw = referenceYaw;
            proof.arc = 0.0f;
        }
        const float moved = std::fabs(WrapPi(referenceYaw - proof.anchorYaw));
        if (moved <= kProofAntipodeRad && moved > proof.arc)
            proof.arc = moved;
        if (proof.agreed < UINT32_MAX)
            ++proof.agreed;
        if (proof.agreed >= kProofSamples && proof.arc >= kProofSpreadRad)
        {
            proof.stage = ProofStage::Confirmed;
            return ProofStep::Confirmed;
        }
        if (proof.stage == ProofStage::Unproven && proof.agreed >= kProofSamples && !proof.provisionalSpent)
        {
            proof.stage = ProofStage::Provisional;
            return ProofStep::Provisional;
        }
        return ProofStep::Agreed;
    }
    if (miss < kProofDisagreeRad)
        return ProofStep::Lagging;
    if (!proof.disagreed)
    {
        proof.firstDisagreedGame = gameYaw;
        proof.firstDisagreedReference = referenceYaw;
    }
    if (proof.disagreed < UINT32_MAX)
        ++proof.disagreed;
    if (proof.stage == ProofStage::Unproven)
    {
        if (proof.disagreed != kProofRestartRun)
            return ProofStep::Disagreed;
        proof.agreed = 0;
        proof.arc = 0.0f;
        return ProofStep::Restarted;
    }
    if (proof.disagreed < kProofWithdrawRun)
        return ProofStep::Disagreed;
    proof.stage = ProofStage::Unproven;
    proof.provisionalSpent = true;
    proof.agreed = 0;
    proof.arc = 0.0f;
    proof.disagreed = 0;
    return ProofStep::Withdrawn;
}

// ---- THE HEAD, and the view frame it is measured in ------------------------------------------------------------
// One frame's stock centre camera, tracking sample and reference (halo_ce::RenderContext, as the render path
// publishes it for gameplay consumers) give the head's forward and up in the game's world through
// BuildTrackingFrame: the mapping the eyes and the sound listener use. `heading` is the horizontal heading the
// room is mapped into the world through: on foot the native centre camera's, in a seat the player's own control
// yaw (halo_ce::Reference::seatHeading). It is the proof's reference.
struct HeadFrame
{
    float forward[3]{}, up[3]{};
    float heading[3]{};
    bool seat = false;
};
inline bool BuildHeadFrame(const halo_ce::RenderContext& context, HeadFrame& out) noexcept
{
    using namespace halo_ce;
    if (!context.tracking.serial || !context.tracking.generation || !context.tracking.spaceEpoch ||
        !context.referenceRevision || context.tracking.generation != context.reference.generation ||
        context.tracking.spaceEpoch != context.reference.spaceEpoch || !Valid(context.tracking.headOrientation))
        return false;
    Camera frame{};
    Quat inverse{};
    if (!BuildTrackingFrame(context.camera, context.reference, frame, inverse))
        return false;
    const Quat head = Multiply(inverse, context.tracking.headOrientation);
    const Vec3 forward = ToNative(frame, Rotate(head, {0, 0, -1}));
    const Vec3 up = ToNative(frame, Rotate(head, {0, 1, 0}));
    if (!Finite(forward) || !Finite(up) || !Finite(frame.forward))
        return false;
    HeadFrame built;
    built.forward[0] = forward.x;
    built.forward[1] = forward.y;
    built.forward[2] = forward.z;
    built.up[0] = up.x;
    built.up[1] = up.y;
    built.up[2] = up.z;
    built.heading[0] = frame.forward.x;
    built.heading[1] = frame.forward.y;
    built.heading[2] = frame.forward.z;
    // BuildTrackingFrame's own test for taking a seat's heading.
    const Vec3 seat{context.reference.seatHeading.x, context.reference.seatHeading.y, 0};
    const float seatLength = std::sqrt(Dot(seat, seat));
    built.seat = context.reference.seatHeadingValid && std::isfinite(seatLength) && seatLength >= 0.0001f;
    out = built;
    return true;
}
// May a frame's heading stand as the proof's reference? Only from a head sample this consumer may use, in the
// player's own first-person view on foot. A seat is no reference: there the frame's heading is read from player
// control itself, the record the sensor's yaw is copied from, so it would prove nothing new; and a seat whose
// control yaw could not be read is mapped through a camera that follows the aim.
inline bool ReferenceUsable(head_relative::Use sampleUse, bool gameplay, bool hasUnit, bool onFoot, bool firstPerson,
                            bool seatFrame)
{
    return sampleUse == head_relative::Use::Ok && gameplay && hasUnit && onFoot && firstPerson && !seatFrame;
}

// ---- the verdict -----------------------------------------------------------------------------------------------
enum class TrackerUse : uint8_t
{
    Used = 0,
    NotLocalUser,     // the sensor is not local player 0's
    NoStore,          // the game stored no heading in this call (no unit), or the field is not control yaw + pi/2
    Unproven,         // the proof has not passed, or was withdrawn and is not confirmed again
    SwitchOff,        // the player's switch is off
    NoSample,         // no frame has published a head yet, or the head view was not live in it
    OtherGeneration,  // the sample belongs to another load of the title
    FromTheFuture,    // the sample's clock is ahead of this one's
    Stale,            // older than the age limit
    NoPlayerState,    // the local player's state could not be read
    NotGameplay,      // a cinematic, or a view the game draws no sensor in
    NoHeading,        // non-finite, or the head never had a heading
    Overlapped,       // local player 0's draw arrived while another was inside the hook (the runtime's flag)
    Count
};
inline const char* TrackerUseName(TrackerUse use)
{
    switch (use)
    {
    case TrackerUse::Used: return "used";
    case TrackerUse::NotLocalUser: return "not the local user";
    case TrackerUse::NoStore: return "the game stored no heading";
    case TrackerUse::Unproven: return "unproven";
    case TrackerUse::SwitchOff: return "switch off";
    case TrackerUse::NoSample: return "no head sample";
    case TrackerUse::OtherGeneration: return "another generation";
    case TrackerUse::FromTheFuture: return "sample from the future";
    case TrackerUse::Stale: return "stale head sample";
    case TrackerUse::NoPlayerState: return "no player state";
    case TrackerUse::NotGameplay: return "not gameplay";
    case TrackerUse::NoHeading: return "no heading";
    case TrackerUse::Overlapped: return "an overlapping draw";
    default: return "?";
    }
}
struct TrackerRequest
{
    int32_t localPlayer = -1;   // the sensor's own local player (the render's first argument)
    bool stored = false;        // FreshStore held for this call's field
    bool proven = false;        // ProofAllows after this call's step
    bool switchOn = false;      // g_config.tracker_follows_head, read at this use
    bool playerState = false;   // the local player's state could be read, for this load of the title
    bool gameplay = false;      // no cinematic, and a view the game draws the sensor in
    uint32_t generation = 0;    // the title generation now
    uint64_t nowUs = 0;         // the sight clock now
    int sign = 1;               // g_config.tracker_head_sign
    float offsetDeg = 0.0f;     // g_config.tracker_head_offset_deg
};
// The heading the placer should read. Used = `fieldOut` is the game's field with the head's yaw, in the game's
// own range [pi/2, 5 pi/2); anything else names why the game's own field stands, and `fieldOut` is untouched.
// `hold` keeps the last heading the head had, for a head looking straight up or down.
inline TrackerUse TrackerHeading(const TrackerRequest& request, const head_relative::HeadSample& sample,
                                 head_relative::HeadingHold& hold, float nativeField, float& fieldOut,
                                 float* headYawOut = nullptr)
{
    using head_relative::Use;
    if (request.localPlayer != 0)
        return TrackerUse::NotLocalUser;
    if (!request.stored)
        return TrackerUse::NoStore;
    if (!request.proven)
        return TrackerUse::Unproven;
    if (!request.switchOn)
        return TrackerUse::SwitchOff;
    switch (head_relative::SampleUse(sample, request.generation, request.nowUs))
    {
    case Use::Ok: break;
    case Use::OtherGeneration: return TrackerUse::OtherGeneration;
    case Use::FromTheFuture: return TrackerUse::FromTheFuture;
    case Use::Stale: return TrackerUse::Stale;
    case Use::NoHeading: return TrackerUse::NoHeading;
    default: return TrackerUse::NoSample;
    }
    if (!request.playerState)
        return TrackerUse::NoPlayerState;
    if (!request.gameplay)
        return TrackerUse::NotGameplay;
    if (!std::isfinite(nativeField))
        return TrackerUse::NoHeading;
    float headHeading[3], nativeForward[3];
    if (!head_relative::HeldHeading(hold, sample.forward, kWorldUp, headHeading))
        return TrackerUse::NoHeading;
    const float nativeYaw = YawFromField(nativeField);
    ForwardOfYaw(nativeYaw, nativeForward);
    float turn = 0.0f;
    if (!head_relative::YawToHead(nativeForward, headHeading, kWorldUp, request.sign,
                                  request.offsetDeg * head_relative::kDegToRad, turn))
        return TrackerUse::NoHeading;
    const float yaw = WrapYaw(nativeYaw + turn);
    if (!std::isfinite(yaw))
        return TrackerUse::NoHeading;
    fieldOut = FieldFromYaw(yaw);
    if (headYawOut)
        *headYawOut = std::atan2(headHeading[1], headHeading[0]);
    return TrackerUse::Used;
}

// ---- one call of the sensor's render, as the detour sees it -----------------------------------------------------
// Everything the detour gathered, so that what it decides is this one pure function (and is what the tests run).
struct TrackerFrame
{
    int32_t localPlayer = -1;       // the render's first argument
    bool fieldRead = false;         // the active datum's heading and the control yaw could be read
    float field = 0.0f;             // the active datum's heading, as the game left it
    float controlYaw = 0.0f;        // the output user's facing yaw now (read only)
    float halfPi = 0.0f;            // the constant the store adds, as the image holds it
    bool sampleRead = false;        // a coherent head publication was read
    head_relative::HeadSample sample;
    float frameHeading[3]{};        // that publication's view-frame heading
    bool seatFrame = false;         // ... which is a seat's control yaw
    bool playerState = false;       // the local player's state was read, for this load of the title
    bool cinematic = false;         // the game's cinematic flag
    int perspective = -1;           // the user's director perspective: 0 first person, 1 following, 2 and 3 no sensor
    bool hasUnit = false, onFoot = false;
    bool switchOn = false;          // g_config.tracker_follows_head
    uint32_t generation = 0;        // the title generation now (0 when the hook belongs to another load)
    uint64_t nowUs = 0;             // the sight clock now
    int sign = 1;                   // g_config.tracker_head_sign
    float offsetDeg = 0.0f;         // g_config.tracker_head_offset_deg
};
struct TrackerDecision
{
    TrackerUse use = TrackerUse::NoStore;
    float field = 0.0f;             // what the placer should read: the game's own unless `use` is Used
    bool stepped = false;           // the proof was stepped in this call (the game stored a heading)
    ProofStep step = ProofStep::NoReference;
    float gameYaw = 0.0f;           // the game's heading minus pi/2 (when stepped)
    bool frameYawKnown = false;
    float frameYaw = 0.0f;          // the view frame's heading
    float headYaw = 0.0f;           // the head's heading (when Used)
};
// The proof is stepped whenever the game stored a heading, whatever the switch says: it is about the game, not
// about the player's choice.
inline TrackerDecision DecideTracker(const TrackerFrame& in, ViewProof& proof, head_relative::HeadingHold& hold)
{
    TrackerDecision out;
    out.field = in.field;
    TrackerRequest request;
    request.localPlayer = in.localPlayer;
    request.stored = in.localPlayer == 0 && in.fieldRead && FreshStore(in.field, in.controlYaw, in.halfPi);
    head_relative::HeadSample sample;
    if (request.stored)
    {
        if (in.sampleRead)
            sample = in.sample;
        request.generation = in.generation;
        request.nowUs = in.nowUs;
        request.playerState = in.playerState;
        // The game draws the sensor in perspectives 0 and 1 (its own test at the render's head).
        request.gameplay = in.playerState && !in.cinematic && (in.perspective == 0 || in.perspective == 1);
        out.gameYaw = YawFromField(in.field);
        out.frameYawKnown = in.sampleRead && sample.valid && HeadingYaw(in.frameHeading, out.frameYaw);
        const bool usable =
            out.frameYawKnown && ReferenceUsable(head_relative::SampleUse(sample, in.generation, in.nowUs),
                                                 request.gameplay, in.hasUnit, in.onFoot, in.perspective == 0,
                                                 in.seatFrame);
        out.step = StepProof(proof, out.gameYaw, usable, out.frameYaw);
        out.stepped = true;
        request.proven = ProofAllows(proof);
        request.switchOn = in.switchOn;
        request.sign = in.sign;
        request.offsetDeg = in.offsetDeg;
    }
    out.use = TrackerHeading(request, sample, hold, in.field, out.field, &out.headYaw);
    return out;
}
} // namespace haloce_head_relative
