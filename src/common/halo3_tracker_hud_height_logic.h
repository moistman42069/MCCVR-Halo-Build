// THE MOTION TRACKER'S BLIPS MOVE WITH THE HUD HEIGHT, Halo 3: the pure half of
// src/dll/halo3_tracker_hud_height.inl. No game, engine or OpenXR includes.
//
// EVIDENCE CLASS: RETAIL-DERIVED AND RUN. Read from retail halo3.dll by disassembly (no editing-kit symbols,
// so every name below is ours; tools/verify-halo3-tracker-hud-height.py re-derives every address from the
// patterns here), then measured in the game's own picture (docs/HALO3-TRACKER-HUD-HEIGHT-2026-10-07.md has the
// heights and the numbers).
//
// THE PROBLEM. With a HUD height set, the tracker's dots sit below the tracker, the player's own dot
// included. The cause is the HUD HEIGHT setting, not the tracker:
//
//   * The HUD height hook (game.cpp, HudAnchorBasisHook) lowers the y of the basis the HUD's anchor function
//     (0x2EE234) returns, by hud_vertical_offset.
//   * That function resolves anchor 12, "my parent", by calling ITSELF for the parent HUD widget's own anchor
//     (through 0x2EDFF4, which has no other caller and resolves exactly one level). A hook that acts at both
//     levels moves an element of a HUD widget by TWICE the setting and something anchored directly (the
//     message text) by once: in the desktop mirror, 2771 pixels wide, +16 then moves the tracker's disc
//     34.5 pixels and the message text half of that. So the hook
//     moves the outermost call's finished basis only: every element moves by the setting, ONCE.
//   * The tracker's BLIPS are not HUD widgets. The blip draw (0x2F3904; one caller, the per-user HUD draw
//     0x2D289C, after its widget loop) places each quad at  blip * scale + origin,  and reads origin and scale
//     from four floats the HUD frame setup (0x2ED0D4) copies out of the current HUD-resolution record (+0x18 ..
//     +0x24) before any widget is drawn. Same units as an anchor basis: the screen corners the anchors use are the
//     same record's width and height. Nothing in the game moves that origin, so the blips stay where the tracker
//     is authored while its art goes up: every dot 16 HUD units low at the default height, a quarter of the
//     tracker's radius. (Measured with the height applied twice, 2 x 16 = 32 units, half the radius: the player's
//     own dot 32.5 pixels below the disc's centre at +16 and within 2 pixels of it at 0, the disc having moved
//     34.5 and the dot not at all.)
//
// THE SEAM: for the duration of the blip draw the origin's y is lowered by what the hook lowered the tracker's art
// in this HUD pass, and put back. The frame setup rewrites all four floats at the start of every pass.
//
//   * HOW MUCH: the height the anchor hook applied in this pass on this thread, which that hook itself
//     publishes (so a hook that is not installed, a height of 0 or a flat session all mean: nothing). The hook
//     moves every element once, however it is anchored, so this does not rest on HOW the tracker's art is
//     anchored.
//   * WHAT IT CANNOT TOUCH: one float of the HUD's own frame globals, for the length of one draw call. Not the
//     tracker's contacts (what is drawn is what the update stored), nothing the simulation, the aim or the
//     network reads.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace halo3_tracker_hud_height
{
// ---- the signatures (halo3.dll: each matches exactly once; the verifier checks) ----
// ADDR-HYGIENE: resolved. No address or offset below is a constant of ours: the hook target is proven twice (its
// own entry pattern AND the operand of its one call), and the origin's address is decoded from three independent
// sites that must agree (the draw's own read, the contact builder's read and the frame setup's write), each of
// which must also place the scale pair right behind it. The integers beside each pattern are positions INSIDE
// that matched pattern, not image offsets: ADDR-HYGIENE: structural (the unit test checks that each lands on the
// bytes its comment names).
//
// The blip draw's entry: mov rax,rsp; three home-slot saves; push rbp, r14, r15; lea rbp,[rax-0x2508];
// mov eax,0x25F0; call __chkstk; sub rsp,rax; movaps [rsp+0x25E0],xmm6; mov rdi,rcx.
inline constexpr const char* kDrawEntryPattern =
    "48 8B C4 48 89 58 08 48 89 70 18 48 89 78 20 55 41 56 41 57 48 8D A8 F8 DA FF FF B8 F0 25 00 00 "
    "E8 ?? ?? ?? ?? 48 2B E0 0F 29 B4 24 E0 25 00 00 48 8B F9";
// Its one call, in the per-user HUD draw after the widget loop: test al,al; je +0x13; mov rcx,[r15+rdi];
// add rcx,0x244; add rcx,rsi; call. The E8 is at +18.
inline constexpr const char* kDrawCallPattern =
    "84 C0 74 13 49 8B 0C 3F 48 81 C1 44 02 00 00 48 03 CE E8 ?? ?? ?? ??";
inline constexpr size_t kDrawCall = 18;
// The draw's read of its origin and scale: cmp [rdi+0xCAC],ebx; jle; mov rax,[ORIGIN]; lea r14,[rdi+0xC58];
// movss xmm6,[SCALE A]; mov esi,ebx; movss xmm7,[SCALE B]. Each disp32 ends its instruction.
inline constexpr const char* kDrawOriginPattern =
    "39 9F AC 0C 00 00 0F 8E ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 4C 8D B7 58 0C 00 00 F3 0F 10 35 ?? ?? ?? ?? "
    "8B F3 F3 0F 10 3D ?? ?? ?? ??";
inline constexpr size_t kDrawOrigin = 15, kDrawScaleA = 30, kDrawScaleB = 40;
// The contact builder's read of the same three (0x2F36B8, called only by the draw): movss xmm10,[SCALE B];
// movsxd rax,edx; movaps [r11-0x78],xmm11; movss xmm11,[SCALE A]; imul r14,rax,0x104; movaps [rsp+0x40],xmm12;
// mov rax,[ORIGIN]; mov [r11+8],rax.
inline constexpr const char* kObjectOriginPattern =
    "F3 44 0F 10 15 ?? ?? ?? ?? 48 63 C2 45 0F 29 5B 88 F3 44 0F 10 1D ?? ?? ?? ?? 4C 69 F0 04 01 00 00 "
    "44 0F 29 64 24 40 48 8B 05 ?? ?? ?? ?? 49 89 43 08";
inline constexpr size_t kObjectScaleB = 5, kObjectScaleA = 22, kObjectOrigin = 42;
// The HUD frame setup's write of all of it (rax = the current HUD-resolution record): mov [RECORD],rax;
// mov rcx,[rax+0x18]; mov [ORIGIN],rcx; movss xmm0,[rax+0x20]; movss [SCALE A],xmm0; movss xmm1,[rax+0x24];
// movss [SCALE B],xmm1. So the origin is that record's +0x18 pair, rewritten every HUD pass.
inline constexpr const char* kSetupWritePattern =
    "48 89 05 ?? ?? ?? ?? 48 8B 48 18 48 89 0D ?? ?? ?? ?? F3 0F 10 40 20 F3 0F 11 05 ?? ?? ?? ?? "
    "F3 0F 10 48 24 F3 0F 11 0D ?? ?? ?? ??";
inline constexpr size_t kSetupRecord = 3, kSetupOrigin = 14, kSetupScaleA = 27, kSetupScaleB = 40;
// How far past its entry the draw's origin read may lie (the draw is 0x270 bytes; the read is at +0x16C).
// ADDR-HYGIENE: guarded -- a measurement of this build used only as an upper bound on top of the unique matches;
// too small on another build fails closed.
inline constexpr int64_t kDrawExtent = 0x300;

inline int32_t ReadI32(const uint8_t* at)
{
    int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
// Where the E8 at `site + opcode` lands, as an offset from the module base (`siteRva` is the site's own).
inline int64_t BranchRva(const uint8_t* site, size_t opcode, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(opcode) + 5 + ReadI32(site + opcode + 1);
}
// What the rip-relative disp32 at `site + disp` names; the disp32 is its instruction's last four bytes.
inline int64_t RipRva(const uint8_t* site, size_t disp, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(disp) + 4 + ReadI32(site + disp);
}

// What the install decodes (offsets from the module base).
struct Sites
{
    int64_t drawRva = 0;     // the blip draw's entry: the hook
    int64_t originRva = 0;   // two floats: the tracker's centre on the HUD, x then y (y grows downward)
    int64_t recordRva = 0;   // the pointer to the HUD-resolution record the centre was copied from (dev witness)
};
enum class Bind : uint8_t
{
    Bound = 0,
    EntryMissing,       // the blip draw's entry: no match, or more than one
    CallMissing,        // the per-user HUD draw's call of it
    DrawReadMissing,    // the draw's read of its origin and scale
    ObjectReadMissing,  // the contact builder's read of them
    SetupWriteMissing,  // the frame setup's write of them
    CallMismatch,       // the HUD draw's call does not land on the matched entry
    ReadOutside,        // the origin read is not inside the blip draw
    ScaleNotBeside,     // a site's scale pair is not right behind its origin
    OriginDisagrees,    // the three sites do not name one origin
    RecordNotBeside,    // the setup's record pointer is not right in front of the origin
    Implausible,        // the origin is not a float's address inside the module
    Count
};
inline const char* BindName(Bind why)
{
    switch (why)
    {
    case Bind::Bound: return "bound";
    case Bind::EntryMissing: return "the blip draw's entry is missing or ambiguous";
    case Bind::CallMissing: return "the HUD draw's call of the blip draw is missing or ambiguous";
    case Bind::DrawReadMissing: return "the blip draw's read of its origin is missing or ambiguous";
    case Bind::ObjectReadMissing: return "the contact builder's read of the origin is missing or ambiguous";
    case Bind::SetupWriteMissing: return "the HUD frame setup's write of the origin is missing or ambiguous";
    case Bind::CallMismatch: return "the HUD draw's call does not land on the blip draw's entry";
    case Bind::ReadOutside: return "the origin read is not inside the blip draw";
    case Bind::ScaleNotBeside: return "a site's scale pair is not right behind its origin";
    case Bind::OriginDisagrees: return "the draw, the contact builder and the frame setup do not name one origin";
    case Bind::RecordNotBeside: return "the frame setup's record pointer is not right in front of the origin";
    case Bind::Implausible: return "the decoded origin is not a float's address inside the module";
    default: return "?";
    }
}
// Each site as the matched pattern's bytes and its offset from the module base; null bytes = no unique match.
// `imageSize` bounds the decoded origin.
inline Bind DecodeSites(const uint8_t* entry, int64_t entryRva, const uint8_t* call, int64_t callRva,
                        const uint8_t* drawRead, int64_t drawReadRva, const uint8_t* objectRead,
                        int64_t objectReadRva, const uint8_t* setupWrite, int64_t setupWriteRva, int64_t imageSize,
                        Sites& out)
{
    if (!entry)
        return Bind::EntryMissing;
    if (!call)
        return Bind::CallMissing;
    if (!drawRead)
        return Bind::DrawReadMissing;
    if (!objectRead)
        return Bind::ObjectReadMissing;
    if (!setupWrite)
        return Bind::SetupWriteMissing;
    if (BranchRva(call, kDrawCall, callRva) != entryRva)
        return Bind::CallMismatch;
    if (!(drawReadRva > entryRva && drawReadRva - entryRva < kDrawExtent))
        return Bind::ReadOutside;
    const int64_t origin = RipRva(drawRead, kDrawOrigin, drawReadRva);
    if (RipRva(drawRead, kDrawScaleA, drawReadRva) != origin + 8 ||
        RipRva(drawRead, kDrawScaleB, drawReadRva) != origin + 12 ||
        RipRva(objectRead, kObjectScaleA, objectReadRva) != RipRva(objectRead, kObjectOrigin, objectReadRva) + 8 ||
        RipRva(objectRead, kObjectScaleB, objectReadRva) != RipRva(objectRead, kObjectOrigin, objectReadRva) + 12 ||
        RipRva(setupWrite, kSetupScaleA, setupWriteRva) != RipRva(setupWrite, kSetupOrigin, setupWriteRva) + 8 ||
        RipRva(setupWrite, kSetupScaleB, setupWriteRva) != RipRva(setupWrite, kSetupOrigin, setupWriteRva) + 12)
        return Bind::ScaleNotBeside;
    if (RipRva(objectRead, kObjectOrigin, objectReadRva) != origin ||
        RipRva(setupWrite, kSetupOrigin, setupWriteRva) != origin)
        return Bind::OriginDisagrees;
    if (RipRva(setupWrite, kSetupRecord, setupWriteRva) != origin - 8)
        return Bind::RecordNotBeside;
    // Record pointer (8), origin (8), scale pair (8): all inside the image, and floats sit on four bytes.
    if (origin < 8 || origin > imageSize - 16 || (origin & 3))
        return Bind::Implausible;
    out.drawRva = entryRva;
    out.originRva = origin;
    out.recordRva = origin - 8;
    return Bind::Bound;
}

// ---- the arithmetic ----
enum class Use : uint8_t
{
    Lowered = 0,
    SwitchOff,
    NothingLowered,   // the anchor hook lowered nothing in this HUD pass: HUD height 0, a flat session, or no hook
    NotFinite,        // the origin, or the y it would get, is not a number
    Unreadable,       // the origin could not be read or written (counted as a fault as well)
    Count
};
inline const char* UseName(Use use)
{
    switch (use)
    {
    case Use::Lowered: return "moved with the HUD height";
    case Use::SwitchOff: return "the switch is off";
    case Use::NothingLowered: return "the HUD height moved nothing in this HUD pass";
    case Use::NotFinite: return "the tracker's centre is not a number";
    case Use::Unreadable: return "the tracker's centre could not be read";
    default: return "?";
    }
}
// By how much the tracker's art was lowered on the screen (y grows downward, so a positive HUD height gives a
// negative number: up) when the anchor hook lowered each basis by `height`.
inline float ArtShiftY(float height) { return -height; }

// The origin's y for the duration of the blip draw. `height` is what the anchor hook lowered a basis by in this
// HUD pass on this thread (0 = it lowered none). Anything but Lowered leaves `lowered` at the game's own y.
// Refused only for what the value IS, never for its size.
inline Use LoweredOriginY(bool switchOn, float height, float originY, float& lowered)
{
    lowered = originY;
    if (!switchOn)
        return Use::SwitchOff;
    if (height == 0.0f)
        return Use::NothingLowered;
    const float wanted = originY + ArtShiftY(height);
    if (!std::isfinite(height) || !std::isfinite(originY) || !std::isfinite(wanted))
        return Use::NotFinite;
    lowered = wanted;
    return Use::Lowered;
}

// After the draw: the game's own y goes back only where OUR value still stands (bit for bit). Anything else was
// written by the game meanwhile and is left alone.
inline bool RestoreOurs(float current, float ours)
{
    return std::memcmp(&current, &ours, sizeof(float)) == 0;
}
} // namespace halo3_tracker_hud_height
