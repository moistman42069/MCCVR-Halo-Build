// HALO 4: THE SOUND LISTENER FOLLOWS THE HEAD, the pure half (no game, engine or OpenXR includes).
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED. Nothing here has been checked against the Halo 4 editing
// kit, and docs/EDITING-KIT-EVIDENCE.md's Halo 4 rule lets halo4.dll only match and verify what the
// kit explains. Everything below is read from retail halo4.dll (SHA-256 7C53E7D5...FA84) alone, so every name is
// ours (but one: the hooked function's, which the retail image itself exports) and nothing here is a kit fact.
// As in the tracker lane (halo4_head_relative_logic.h), two things stand in for the kit:
//   * the hook proves the doubtful facts at RUN TIME before it changes anything (HandOffProof, below);
//   * docs/HALO4-HEAD-RELATIVE-2026-10-06.md lists, per binding, the kit artefact that would settle it.
// Other titles' listener lanes are a lead for where to look and are not evidence for anything here.
//
// WHAT RETAIL DOES (tools/verify-halo4-sound-listener.py re-derives every address from the patterns here).
// The listener update (+0x59B00; void(bool); four direct callers) loops over users 0..3. For a user with a
// camera it takes a forward, an up and a position -- copied from that user's camera at
// [engine TLS + 0x680] + 0x15C + user * 0x428 (position +0x00, forward +0x28, up +0x34), or blended toward the
// player's unit in a seat seen from outside, or taken from the unit under a global byte -- and then does TWO
// things with them, in this order:
//     listener = globals + user * 0x58           ; globals: the pointer the image keeps at +0x107BE08
//     listener[+0x88] = 1                        ; built
//     matrix(listener + 0x44, forward, up)       ; +0x11BFAC: scale 1 at +0, forward +4, left = up x forward +0x10,
//                                                ; up +0x1C, position zeroed +0x28
//     listener[+0x44 + 0x28] = position
//     listener[+0x78] = (v . forward, v . left, v . up)       ; +0x11D368, the velocity in the listener's frame
//     nine = { -forward.x, forward.y, forward.z,  -up.x, up.y, up.z,  -position.x, position.y, position.z }
//     listener[+0x40] = location (a 16-bit index)
//     hand_off(nine, user)                       ; +0x9B8250: its ONLY caller is this call
// The hand-off makes the front perpendicular to the top, normalises both and queues a command (id 0x10: index,
// front, top, position) for the object at [+0x1077F88]. THE IMAGE NAMES IT: halo4.dll's own export table carries
// +0x9B8250 as ?SetListenerPosition@SoundEngine@AK@@YA?AW4AKRESULT@@AEBUAkListenerPosition@@K@Z -- the audio
// middleware's public "set a listener's position" call (a listener position, a listener index), whose argument
// is front, top, position. That name is the retail image's, not ours and not the kit's. The same export table
// names the call the game gives every sound source's position to (?SetPosition@SoundEngine@AK@@..., +0x9B87C0),
// and the game's one wrapper around it (+0xADAB8) negates the x of the source's position and orientation with
// the same constant: sources and the listener reach the middleware in one world space under one mirror.
// The game's own sound code also reads the record afterwards: its matrix (+0xE5764 turns a listener-relative
// direction into the world with it) and its position (reads were found in +0x5836C, +0x5A6C8, +0xE5300,
// +0xE901C, +0x123118 and +0x4D8AA0). So there are two consumers, and neither is made from the other: the nine
// floats are built from the update's LOCALS in the same pass that fills the record.
//
// THE SEAM: ONE detour, on the hand-off (+0x9B8250). It is entered inside the update's loop, a few instructions
// after the game stored this user's record, with the nine floats in hand and the user index. For output user 0
// it (a) forwards a PRIVATE copy of the nine floats that carries the head's front and top (under the game's own
// mirror) and the listener's position moved by the head's offset from the game's camera, and (b) rewrites user
// 0's record to match: the three rows, the position and the velocity in the new frame. Every other call is
// forwarded exactly as it came. The update itself is not hooked: all four of its callers reach the mixer through
// this one call, so no caller needs telling apart. Nothing else is written: not the camera, not player control,
// not the aim, nothing replicated.
//
// ADDR-HYGIENE: resolved + guarded. No address and no offset ships. The update's head, its store-and-hand-off
// site, the game's own writer of the record's two signatures and the whole hand-off function are each matched
// uniquely; the site must lie inside the function whose head matched and its last call must land on the matched
// hand-off; the matched hand-off must be the address the image EXPORTS under the middleware's name for it; the
// record's offsets are read from the site and must sit where the signature-delimited record puts
// them; the site's two callees must carry the matrix builder's and the transform's bytes; the constant the site
// mirrors with must be the sign bit. And at run time the nine floats must AGREE, value for value, with the
// record found through the globals pointer -- two copies of one basis reached by two independent routes --
// before anything is changed, and on every call that writes the record.
#pragma once

#include "halo4_head_relative_logic.h"
#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace halo4_sound_listener
{
using halo4_head_relative::BranchRva;
using halo4_head_relative::ReadU32;
using halo4_head_relative::RipRva;

// ---- the sites -------------------------------------------------------------------------------------------------
// ADDR-HYGIENE: resolved (the comment above). The numbers beside each pattern are positions INSIDE that matched
// pattern, never image addresses.
// The update's head (retail +0x59B00): prologue (five pushes and 0x230) / security cookie / mov [rsp+0x24], cl /
// call / xor r14d, r14d / test al, al / je / call / mov r10d, [rip + TLS INDEX] / lea r15d, [r14+1] / gs:[0x58] /
// or rbx, -1 / mov r9, [rip + GLOBALS] / mov esi, r14d / movss xmm7, [rip + SIGN MASK] / mov r13d, r14d / .. /
// imul r12, r13, STRIDE
inline constexpr const char* kListenerUpdateHeadPattern =
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D A8 A8 FE FF FF 48 81 EC 30 02 00 "
    "00 0F 29 70 C8 0F 29 78 B8 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 00 01 00 00 88 4C 24 24 E8 ?? ?? ?? ?? 45 33 "
    "F6 84 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B 15 ?? ?? ?? ?? 45 8D 7E 01 65 4C 8B 1C 25 58 00 00 00 48 83 CB "
    "FF 4C 8B 0D ?? ?? ?? ?? 41 8B F6 F3 0F 10 3D ?? ?? ?? ?? 45 8B EE 89 44 24 50 44 89 74 24 28 4D 6B E5 58";
inline constexpr size_t kUpdateHeadBytes = 146;
inline constexpr size_t kUpdateFrameSub = 34, kUpdateGlobalsDisp = 115, kUpdateGlobalsNext = 119,
                        kUpdateSignMaskDisp = 126, kUpdateSignMaskNext = 130, kUpdateStride = 145;
// The five pushes the pattern's own bytes fix (push rbp, r12, r13, r14, r15: no wildcard among them).
inline constexpr uint32_t kUpdatePushedBytes = 0x28;
// The store and the hand-off (retail +0x59F6E, to the return of the call at +0x5A051):
//   mov r15d, 1 / mov [r12 + VALID], r15b / .. / lea rcx, [r12 + MATRIX] / mov [r12 + VALID + 1], al /
//   lea r8, [rsp + UP] / lea rdx, [rsp + FORWARD] / call BUILDER / movsd xmm6, [rbp + POSITION] /
//   lea r8, [r12 + VELOCITY] / mov edi, [rbp + POSITION + 8] / lea rdx, [world velocity] /
//   movsd [rcx + 0x28], xmm6 / mov [rcx + 0x30], edi / call TRANSFORM /
//   the nine floats at [rbp + NINE]: each of forward, up and position copied with its x passed through
//   xorps xmm0, xmm7 / mov edx, esi (the user) / lea rcx, [rbp + NINE] /
//   mov [r12 + LOCATION], bx / call HAND-OFF
inline constexpr const char* kListenerHandOffSitePattern =
    "41 BF 01 00 00 00 45 88 BC 24 88 00 00 00 44 39 7C 24 50 7F 1B 45 33 C9 48 8D 54 24 28 45 33 C0 48 8D 4D 98 E8 "
    "?? ?? ?? ?? 84 C0 41 8A C7 75 03 41 8A C6 49 8D 4C 24 44 41 88 84 24 89 00 00 00 4C 8D 44 24 40 48 8D 54 24 30 "
    "E8 ?? ?? ?? ?? F2 0F 10 75 98 4D 8D 44 24 78 8B 7D A0 48 8D 54 24 58 F2 0F 11 71 28 89 79 30 E8 ?? ?? ?? ?? F3 "
    "0F 10 45 98 48 8D 4D B0 F3 0F 10 4D A0 0F 57 C7 F3 0F 11 45 C8 8B D6 F3 0F 10 45 9C F3 0F 11 45 CC F3 0F 10 44 "
    "24 30 0F 57 C7 F3 0F 11 4D D0 F3 0F 10 4C 24 38 F3 0F 11 45 B0 F3 0F 10 44 24 34 F3 0F 11 45 B4 F3 0F 10 44 24 "
    "40 0F 57 C7 F3 0F 11 4D B8 F3 0F 10 4C 24 48 F3 0F 11 45 BC F3 0F 10 44 24 44 F3 0F 11 45 C0 F3 0F 11 4D C4 66 "
    "41 89 5C 24 40 E8 ?? ?? ?? ??";
inline constexpr size_t kSiteBytes = 232;
inline constexpr size_t kSiteValidDisp = 10, kSiteMatrixDisp = 55, kSiteValidNextDisp = 60, kSiteUpLocal = 68,
                        kSiteForwardLocal = 73, kSiteCallBuilder = 74, kSitePositionLocal = 83,
                        kSiteVelocityDisp = 88, kSitePositionHigh = 91, kSitePositionStore = 101,
                        kSitePositionStoreHigh = 104, kSiteCallTransform = 105, kSiteNineLocal = 118,
                        kSiteLocationDisp = 226, kSiteCallHandOff = 227, kSiteReturn = 232;
// How the site fills the nine floats: for float k, where its source operand, its store operand and (for the
// three x components) its xorps sit in the pattern. k 0..2 is the forward, 3..5 the up, 6..8 the position.
struct NineStore
{
    uint8_t source, store, mirror;  // mirror: 0 = this component is copied as it is
};
inline constexpr NineStore kSiteNine[9]{{149, 168, 150}, {174, 179, 0}, {163, 193, 0}, {185, 204, 186}, {210, 215, 0},
                                        {199, 220, 0},   {114, 131, 124}, {138, 143, 0}, {123, 157, 0}};
// The game's own writer of the two signatures, in the sound manager's initialisation (retail +0x57E06):
// imul rax, rcx, STRIDE / inc r8d / mov edx, r14d / mov dword [rax + rbx + HEADER], 'lshd' /
// mov dword [rax + rbx + FOOTER], 'lsft'
inline constexpr const char* kListenerSignatureWriterPattern =
    "48 6B C1 58 41 FF C0 41 8B D6 C7 44 18 3C 64 68 73 6C C7 84 18 84 00 00 00 74 66 73 6C";
inline constexpr size_t kWriterBytes = 29;
inline constexpr size_t kWriterStride = 3, kWriterHeaderDisp = 13, kWriterFooterDisp = 21;
inline constexpr uint32_t kListenerHeaderSignature = 0x6C736864, kListenerFooterSignature = 0x6C736674;
// The hand-off (retail +0x9B8250, THE HOOK), the whole function: prologue / mov rsi, rcx / mov edi, edx /
// just-my-code check / mov eax, 0x10 / dot(front, top) / front - dot * top / top copied / normalise both /
// position copied / the index / call / mov rcx, [rip + OBJECT] / lea rdx, [the command] / movzx r8d, ax /
// call QUEUE / epilogue. It reads [rsi + 0x00 .. 0x23] and edx, and writes nothing through rsi.
inline constexpr const char* kListenerHandOffPattern =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 8B F1 8B FA 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? B8 10 00 00 00 "
    "48 8D 56 0C 48 8B CE 66 89 44 24 22 E8 ?? ?? ?? ?? F3 0F 10 0E 48 8D 4C 24 2C 8B 46 14 0F 28 D0 F3 0F 59 56 0C "
    "0F 28 D8 89 44 24 40 F3 0F 59 46 14 F3 0F 59 5E 10 F3 0F 5C CA F3 0F 11 4C 24 2C F3 0F 10 4E 04 F3 0F 5C CB F3 "
    "0F 11 4C 24 30 F3 0F 10 4E 08 F3 0F 5C C8 F2 0F 10 46 0C F2 0F 11 44 24 38 F3 0F 11 4C 24 34 E8 ?? ?? ?? ?? 48 "
    "8D 4C 24 38 E8 ?? ?? ?? ?? F2 0F 10 46 18 8B 46 20 F2 0F 11 44 24 44 89 7C 24 28 89 44 24 4C E8 ?? ?? ?? ?? 48 "
    "8B 0D ?? ?? ?? ?? 48 8D 54 24 20 44 0F B7 C0 E8 ?? ?? ?? ?? 48 8B 5C 24 70 48 8B 74 24 78 48 83 C4 60 5F C3";
inline constexpr size_t kHandOffBytes = 221;
// The name the image's own export table gives the hand-off: AK::SoundEngine::SetListenerPosition(const
// AkListenerPosition&, unsigned long), returning AKRESULT. The install requires the export to exist and to be the
// address the pattern matched.
inline constexpr const char* kListenerHandOffExport =
    "?SetListenerPosition@SoundEngine@AK@@YA?AW4AKRESULT@@AEBUAkListenerPosition@@K@Z";
// ADDR-HYGIENE: guarded. The two callees' sizes and where the compared windows sit are a measurement of this
// build, used only to PROVE the site's callees are these two functions (the bytes must match, and the caller
// requires each to be a function of its own in the image's function table). A build where they are not binds
// nothing.
// The builder the site calls (retail +0x11BFAC, 0xA2 bytes, no relocation in it, compared whole):
// matrix(forward, up) -- scale = 1 at +0, forward at +4, left = up x forward at +0x10, up at +0x1C, position
// zeroed at +0x28. It never writes rcx, which the site relies on for its store of the position.
inline constexpr size_t kMatrixBuilderBytes = 0xA2;
inline constexpr uint8_t kMatrixBuilder[kMatrixBuilderBytes]{
    0x48, 0x83, 0xEC, 0x28, 0xC7, 0x01, 0x00, 0x00, 0x80, 0x3F, 0xF2, 0x0F, 0x10, 0x02, 0xF2, 0x0F, 0x11, 0x41, 0x04, 0x8B,
    0x42, 0x08, 0x89, 0x41, 0x0C, 0xF3, 0x41, 0x0F, 0x10, 0x58, 0x08, 0xF3, 0x0F, 0x10, 0x52, 0x08, 0x0F, 0x28, 0xC3, 0xF3,
    0x41, 0x0F, 0x10, 0x20, 0x0F, 0x28, 0xCA, 0xF3, 0x0F, 0x10, 0x2A, 0x0F, 0x29, 0x74, 0x24, 0x10, 0xF3, 0x41, 0x0F, 0x10,
    0x70, 0x04, 0x0F, 0x29, 0x3C, 0x24, 0xF3, 0x0F, 0x10, 0x7A, 0x04, 0xF3, 0x0F, 0x59, 0xC7, 0xF3, 0x0F, 0x59, 0xCE, 0xF3,
    0x0F, 0x59, 0xFC, 0xF3, 0x0F, 0x59, 0xF5, 0xF3, 0x0F, 0x5C, 0xC8, 0xF3, 0x0F, 0x59, 0xDD, 0xF3, 0x0F, 0x5C, 0xFE, 0xF3,
    0x0F, 0x59, 0xD4, 0x0F, 0x28, 0x74, 0x24, 0x10, 0xF3, 0x0F, 0x11, 0x49, 0x10, 0xF3, 0x0F, 0x5C, 0xDA, 0xF3, 0x0F, 0x11,
    0x79, 0x18, 0x0F, 0x28, 0x3C, 0x24, 0xF3, 0x0F, 0x11, 0x59, 0x14, 0xF2, 0x41, 0x0F, 0x10, 0x00, 0xF2, 0x0F, 0x11, 0x41,
    0x1C, 0x41, 0x8B, 0x40, 0x08, 0x89, 0x41, 0x24, 0x33, 0xC0, 0x48, 0x89, 0x41, 0x28, 0x89, 0x41, 0x30, 0x48, 0x83, 0xC4,
    0x28, 0xC3};
// The transform the site calls for the velocity (retail +0x11D368, 0xE4 bytes): a vector divided by the
// matrix's scale when that is not 1, then out = (v . forward, v . left, v . up). Its last 0x74 bytes are those
// three dot products against the rows at +4, +0x10 and +0x1C.
inline constexpr size_t kVelocityTransformBytes = 0xE4, kVelocityTransformDotsAt = 0x70;
inline constexpr uint8_t kVelocityTransformDots[0x74]{
    0x0F, 0x28, 0xD4, 0x0F, 0x28, 0xCE, 0xF3, 0x0F, 0x59, 0x49, 0x0C, 0x0F, 0x28, 0xDC, 0x49, 0x8B, 0xC0, 0xF3, 0x0F, 0x59,
    0x51, 0x08, 0x0F, 0x28, 0xC5, 0xF3, 0x0F, 0x59, 0x41, 0x04, 0xF3, 0x0F, 0x58, 0xD0, 0x0F, 0x28, 0xC5, 0xF3, 0x0F, 0x58,
    0xD1, 0x0F, 0x28, 0xCE, 0xF3, 0x41, 0x0F, 0x11, 0x10, 0xF3, 0x0F, 0x59, 0x59, 0x14, 0xF3, 0x0F, 0x59, 0x41, 0x10, 0xF3,
    0x0F, 0x59, 0x49, 0x18, 0xF3, 0x0F, 0x58, 0xD8, 0xF3, 0x0F, 0x58, 0xD9, 0xF3, 0x41, 0x0F, 0x11, 0x58, 0x04, 0xF3, 0x0F,
    0x59, 0x61, 0x20, 0xF3, 0x0F, 0x59, 0x71, 0x24, 0xF3, 0x0F, 0x59, 0x69, 0x1C, 0xF3, 0x0F, 0x58, 0xE5, 0xF3, 0x0F, 0x58,
    0xE6, 0x0F, 0x28, 0x34, 0x24, 0xF3, 0x41, 0x0F, 0x11, 0x60, 0x08, 0x48, 0x83, 0xC4, 0x18, 0xC3};
// What the site's xorps flips: the sign bit, so "mirrored" is exactly "negated".
inline constexpr uint32_t kMirrorSignMask = 0x80000000u;
// ADDR-HYGIENE: structural + guarded. Inside the matrix: the builder's bytes above put the rows at these places,
// the site stores the position at kMatrixPosition (DecodeListenerSites checks its operand), and every use checks
// the live record (both signatures, built, scale 1, unit perpendicular rows, left = up x forward, and the nine
// floats' agreement with it) before writing.
inline constexpr size_t kMatrixScale = 0x00, kMatrixForward = 0x04, kMatrixLeft = 0x10, kMatrixUp = 0x1C,
                        kMatrixPosition = 0x28, kMatrixBytes = 0x34;

struct ListenerSites
{
    uint32_t stride = 0;            // one listener (retail 0x58)
    uint32_t headerOffset = 0;      // listener 0's 'lshd' in the sound manager's globals (retail 0x3C)
    uint32_t locationOffset = 0;    // its 16-bit location (0x40)
    uint32_t matrixOffset = 0;      // its matrix (0x44)
    uint32_t velocityOffset = 0;    // its velocity in the matrix's space (0x78)
    uint32_t footerOffset = 0;      // its 'lsft' (0x84)
    uint32_t validOffset = 0;       // "a listener was built this update" (0x88)
    uint32_t updateFrameBytes = 0;  // from the update's rsp in its body to its return address (0x258)
    int64_t updateRva = 0;          // the listener update
    int64_t globalsRva = 0;         // where the image keeps the pointer to the sound manager's globals
    int64_t signMaskRva = 0;        // the constant the site mirrors x with
    int64_t builderRva = 0;         // matrix(forward, up)
    int64_t transformRva = 0;       // the velocity into the matrix's space
    int64_t handOffRva = 0;         // the hook
    int64_t returnRva = 0;          // where the update's call of the hand-off returns
};
enum class ListenerBind : uint8_t
{
    Bound = 0,
    UpdateHeadMissing,   // the listener update's head: no match, or more than one
    SiteMissing,         // its store-and-hand-off site: no match, or more than one
    SiteOutsideUpdate,   // the site is not inside the function whose head matched
    WriterMissing,       // the game's writer of the listener signatures: no match, or more than one
    HandOffMissing,      // the hand-off function: no match, or more than one
    CallMismatch,        // the site's last call does not land on the matched hand-off
    ExportMismatch,      // the image does not export the matched hand-off under the middleware's name for it
    StrideMismatch,      // the update and the signature writer disagree on a listener's size
    RecordMismatch,      // the stored fields do not sit where the signature-delimited record puts them
    NineMismatch,        // the nine floats are not forward, up, position with each x mirrored
    Implausible,         // an offset out of range or misaligned
    HelperMismatch,      // the site's callees are not the matrix builder and the transform
    MirrorMismatch,      // the constant the site mirrors with is not the sign bit
    Count
};
inline const char* ListenerBindName(ListenerBind why)
{
    switch (why)
    {
    case ListenerBind::Bound: return "bound";
    case ListenerBind::UpdateHeadMissing: return "the listener update's head is missing or ambiguous";
    case ListenerBind::SiteMissing: return "the listener store-and-hand-off site is missing or ambiguous";
    case ListenerBind::SiteOutsideUpdate: return "the listener site is not inside the matched update";
    case ListenerBind::WriterMissing: return "the game's listener-signature writer is missing or ambiguous";
    case ListenerBind::HandOffMissing: return "the hand-off function is missing or ambiguous";
    case ListenerBind::CallMismatch: return "the listener site does not call the matched hand-off";
    case ListenerBind::ExportMismatch:
        return "the image does not export the matched hand-off as the middleware's SetListenerPosition";
    case ListenerBind::StrideMismatch: return "the update and the signature writer disagree on a listener's size";
    case ListenerBind::RecordMismatch: return "the stored fields are not where the listener record puts them";
    case ListenerBind::NineMismatch: return "the hand-off's nine floats are not the mirrored forward, up and position";
    case ListenerBind::Implausible: return "the decoded listener offsets are implausible";
    case ListenerBind::HelperMismatch: return "the site's callees are not the matrix builder and the transform";
    case ListenerBind::MirrorMismatch: return "the site does not mirror with the sign bit";
    default: return "?";
    }
}
// `head`, `site`, `writer`, `handOff`: the unique matches (null = missing or ambiguous) with their offsets from
// the module base; `siteFunctionRva`: the beginning of the function `site` lies in, from the image's own
// function table; `exportRva`: what the image exports as kListenerHandOffExport (-1 = no such export).
inline ListenerBind DecodeListenerSites(const uint8_t* head, int64_t headRva, const uint8_t* site, int64_t siteRva,
                                        int64_t siteFunctionRva, const uint8_t* writer, const uint8_t* handOff,
                                        int64_t handOffRva, int64_t exportRva, ListenerSites& out)
{
    if (!head)
        return ListenerBind::UpdateHeadMissing;
    if (!site)
        return ListenerBind::SiteMissing;
    if (siteFunctionRva != headRva || siteRva <= headRva)
        return ListenerBind::SiteOutsideUpdate;
    if (!writer)
        return ListenerBind::WriterMissing;
    if (!handOff)
        return ListenerBind::HandOffMissing;
    if (handOffRva <= 0 || BranchRva(site, kSiteCallHandOff, siteRva) != handOffRva)
        return ListenerBind::CallMismatch;
    if (exportRva != handOffRva)
        return ListenerBind::ExportMismatch;
    const uint32_t stride = head[kUpdateStride];
    if (!stride || stride != writer[kWriterStride])
        return ListenerBind::StrideMismatch;
    const uint32_t header = writer[kWriterHeaderDisp], footer = ReadU32(writer + kWriterFooterDisp);
    const uint32_t valid = ReadU32(site + kSiteValidDisp), matrix = site[kSiteMatrixDisp];
    const uint32_t velocity = site[kSiteVelocityDisp], location = site[kSiteLocationDisp];
    if (!header || (header & 3) || stride < 0x50 || stride > 0x200 || (stride & 3) || footer <= header ||
        footer >= header + stride)
        return ListenerBind::Implausible;
    if (matrix != header + 8 || velocity != matrix + kMatrixBytes || footer != velocity + 12 || valid != footer + 4 ||
        ReadU32(site + kSiteValidNextDisp) != valid + 1 || location != header + 4 || valid + 2 > header + stride ||
        site[kSitePositionStore] != kMatrixPosition || site[kSitePositionStoreHigh] != kMatrixPosition + 8)
        return ListenerBind::RecordMismatch;
    // The nine floats: float k is stored at NINE + 4k and comes from the builder's forward (k 0..2), the
    // builder's up (3..5) or the position the site stores in the matrix (6..8); each x goes through the xorps.
    const int32_t nine = static_cast<int8_t>(site[kSiteNineLocal]);
    const int32_t position = static_cast<int8_t>(site[kSitePositionLocal]);
    const int32_t forward = site[kSiteForwardLocal], up = site[kSiteUpLocal];
    if (static_cast<int8_t>(site[kSitePositionHigh]) != position + 8)
        return ListenerBind::NineMismatch;
    for (int k = 0; k < 9; ++k)
    {
        const NineStore& store = kSiteNine[k];
        const int32_t expected = k < 3 ? forward + 4 * k : (k < 6 ? up + 4 * (k - 3) : position + 4 * (k - 6));
        const int32_t source = k < 6 ? static_cast<int32_t>(site[store.source])
                                     : static_cast<int32_t>(static_cast<int8_t>(site[store.source]));
        const bool mirrored = store.mirror && site[store.mirror] == 0x0F && site[store.mirror + 1] == 0x57 &&
                              site[store.mirror + 2] == 0xC7;
        if (source != expected || static_cast<int8_t>(site[store.store]) != nine + 4 * k ||
            mirrored != (k % 3 == 0))
            return ListenerBind::NineMismatch;
    }
    const int64_t globals = RipRva(head, kUpdateGlobalsDisp, kUpdateGlobalsNext, headRva);
    const int64_t signMask = RipRva(head, kUpdateSignMaskDisp, kUpdateSignMaskNext, headRva);
    const int64_t builder = BranchRva(site, kSiteCallBuilder, siteRva);
    const int64_t transform = BranchRva(site, kSiteCallTransform, siteRva);
    const uint32_t frame = ReadU32(head + kUpdateFrameSub);
    if (globals <= 0 || signMask <= 0 || builder <= 0 || transform <= 0 || builder == transform ||
        builder == handOffRva || transform == handOffRva || frame < 0x100 || frame > 0x1000 || (frame & 7))
        return ListenerBind::Implausible;
    out.stride = stride;
    out.headerOffset = header;
    out.locationOffset = location;
    out.matrixOffset = matrix;
    out.velocityOffset = velocity;
    out.footerOffset = footer;
    out.validOffset = valid;
    out.updateFrameBytes = kUpdatePushedBytes + frame;
    out.updateRva = headRva;
    out.globalsRva = globals;
    out.signMaskRva = signMask;
    out.builderRva = builder;
    out.transformRva = transform;
    out.handOffRva = handOffRva;
    out.returnRva = siteRva + static_cast<int64_t>(kSiteReturn);
    return ListenerBind::Bound;
}
// `builder`: kMatrixBuilderBytes at sites.builderRva; `transform`: kVelocityTransformBytes at sites.transformRva;
// `signMask`: the four bytes at sites.signMaskRva. Null = unreadable, or not a function of its own.
inline ListenerBind CheckListenerHelpers(const uint8_t* builder, const uint8_t* transform, const uint8_t* signMask)
{
    if (!builder || !transform)
        return ListenerBind::HelperMismatch;
    if (std::memcmp(builder, kMatrixBuilder, kMatrixBuilderBytes) != 0 ||
        std::memcmp(transform + kVelocityTransformDotsAt, kVelocityTransformDots, sizeof(kVelocityTransformDots)) != 0)
        return ListenerBind::HelperMismatch;
    if (!signMask || ReadU32(signMask) != kMirrorSignMask)
        return ListenerBind::MirrorMismatch;
    return ListenerBind::Bound;
}

// ---- the listener's frame --------------------------------------------------------------------------------------
struct ListenerBasis
{
    float forward[3]{}, left[3]{}, up[3]{};
};
// The game's own third row: left = up x forward (the builder at retail +0x11BFAC).
inline void LeftOf(const float forward[3], const float up[3], float left[3]) { head_relative::Cross(up, forward, left); }
// The head as a listener's rows: forward as given (unit length), up made perpendicular to it, left by the
// game's rule. False when the head's forward and up do not span a plane.
inline bool HeadBasis(const float headForward[3], const float headUp[3], ListenerBasis& out)
{
    using namespace head_relative;
    if (!Finite3(headForward) || !Finite3(headUp))
        return false;
    const float forwardLength = Length(headForward);
    if (!(forwardLength > 1.0e-6f))
        return false;
    const float forward[3]{headForward[0] / forwardLength, headForward[1] / forwardLength,
                           headForward[2] / forwardLength};
    const float along = Dot(headUp, forward);
    float up[3]{headUp[0] - forward[0] * along, headUp[1] - forward[1] * along, headUp[2] - forward[2] * along};
    const float upLength = Length(up);
    const float headUpLength = Length(headUp);
    if (!(upLength > 1.0e-3f * (headUpLength > 1.0f ? headUpLength : 1.0f)))
        return false;
    for (int i = 0; i < 3; ++i)
    {
        up[i] /= upLength;
        out.forward[i] = forward[i];
        out.up[i] = up[i];
    }
    LeftOf(out.forward, out.up, out.left);
    return true;
}
// Is this a forward and an up as the game hands them on: finite, unit length, perpendicular? The band is the one
// this mod's stereo transaction already admits for the very camera they are copied from
// (Halo4ValidateCameraBasis, halo4_render_logic.h: 0.05 on each squared length and on the dot product), so a
// camera the eyes are rendered from is never refused here for float drift. The identity of the record does not
// rest on this band: it rests on the signatures, on left = up x forward and on the nine floats' exact agreement.
inline constexpr float kCameraBasisBand = 0.05f;
inline bool PairAsBuilt(const float forward[3], const float up[3])
{
    using namespace head_relative;
    return Finite3(forward) && Finite3(up) && std::fabs(Dot(forward, forward) - 1.0f) < kCameraBasisBand &&
           std::fabs(Dot(up, up) - 1.0f) < kCameraBasisBand && std::fabs(Dot(forward, up)) < kCameraBasisBand;
}
// Is this what the game's builder writes: scale 1, a forward and an up as above, left = up x forward?
inline bool BasisAsBuilt(float scale, const ListenerBasis& basis)
{
    constexpr float tolerance = 2.0e-3f;
    if (!(scale == 1.0f) || !PairAsBuilt(basis.forward, basis.up) || !head_relative::Finite3(basis.left))
        return false;
    float left[3];
    LeftOf(basis.forward, basis.up, left);
    return std::fabs(left[0] - basis.left[0]) <= tolerance && std::fabs(left[1] - basis.left[1]) <= tolerance &&
           std::fabs(left[2] - basis.left[2]) <= tolerance;
}
// A world vector in a listener's space, as the game's transform computes it (retail +0x11D368, scale 1).
inline void ToListenerSpace(const ListenerBasis& basis, const float world[3], float out[3])
{
    const float x = head_relative::Dot(world, basis.forward);
    const float y = head_relative::Dot(world, basis.left);
    const float z = head_relative::Dot(world, basis.up);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}
// And back: the world vector whose listener-space image is `local` (the rows are orthonormal).
inline void FromListenerSpace(const ListenerBasis& basis, const float local[3], float out[3])
{
    const float x = basis.forward[0] * local[0] + basis.left[0] * local[1] + basis.up[0] * local[2];
    const float y = basis.forward[1] * local[0] + basis.left[1] * local[1] + basis.up[1] * local[2];
    const float z = basis.forward[2] * local[0] + basis.left[2] * local[1] + basis.up[2] * local[2];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// ---- the hand-off ----------------------------------------------------------------------------------------------
// The nine floats the update hands on: forward, up and position, each with its x negated (the site's xorps with
// the sign bit). The mirror is its own inverse.
inline void ToHandOff(const float forward[3], const float up[3], const float position[3], float nine[9])
{
    const float* const parts[3]{forward, up, position};
    for (int part = 0; part < 3; ++part)
    {
        nine[part * 3 + 0] = -parts[part][0];
        nine[part * 3 + 1] = parts[part][1];
        nine[part * 3 + 2] = parts[part][2];
    }
}
inline void FromHandOff(const float nine[9], float forward[3], float up[3], float position[3])
{
    float* const parts[3]{forward, up, position};
    for (int part = 0; part < 3; ++part)
    {
        parts[part][0] = -nine[part * 3 + 0];
        parts[part][1] = nine[part * 3 + 1];
        parts[part][2] = nine[part * 3 + 2];
    }
}

// Output user 0's listener as the game just wrote it (read through the globals pointer).
struct ListenerRecord
{
    bool readable = false;  // the globals pointer was not null
    uint32_t header = 0, footer = 0;
    uint8_t valid = 0;
    float scale = 0.0f;
    ListenerBasis basis;
    float position[3]{};
    float velocity[3]{};  // in the listener's space
};
// Does the record carry what this call hands on? Identity, and VALUE AGREEMENT: the nine floats and the record
// are two copies of one forward, up and position, made from the same locals a few instructions apart, so they
// must be equal value for value (the builder and the site only copy and negate). A record reached through a
// wrong pointer, stride or offset, or nine floats that mean something else, cannot agree.
enum class RecordState : uint8_t
{
    Agrees = 0,
    Unreadable,       // no sound globals
    NotAListener,     // the record does not carry the listener signatures
    NotBuilt,         // the game did not mark it built
    UnexpectedBasis,  // its matrix is not what the game's builder writes, or its velocity is not finite
    Disagrees,        // the nine floats are not this record's forward, up and position
    Count
};
inline const char* RecordStateName(RecordState state)
{
    switch (state)
    {
    case RecordState::Agrees: return "agrees";
    case RecordState::Unreadable: return "no sound globals";
    case RecordState::NotAListener: return "not a listener record";
    case RecordState::NotBuilt: return "not marked built";
    case RecordState::UnexpectedBasis: return "unexpected listener matrix";
    case RecordState::Disagrees: return "differs from the hand-off";
    default: return "?";
    }
}
inline RecordState CheckRecord(const ListenerRecord& record, const float nine[9])
{
    if (!record.readable)
        return RecordState::Unreadable;
    if (record.header != kListenerHeaderSignature || record.footer != kListenerFooterSignature)
        return RecordState::NotAListener;
    if (record.valid != 1)
        return RecordState::NotBuilt;
    if (!BasisAsBuilt(record.scale, record.basis) || !head_relative::Finite3(record.position) ||
        !head_relative::Finite3(record.velocity))
        return RecordState::UnexpectedBasis;
    float forward[3], up[3], position[3];
    FromHandOff(nine, forward, up, position);
    for (int i = 0; i < 3; ++i)
        if (!(forward[i] == record.basis.forward[i]) || !(up[i] == record.basis.up[i]) ||
            !(position[i] == record.position[i]))
            return RecordState::Disagrees;
    return RecordState::Agrees;
}

// ---- THE PROOF TAKEN AT RUN TIME -------------------------------------------------------------------------------
// What the bytes cannot say without the kit is that the nine floats really are this user's listener basis and
// that the record this lane found really is the one they were made beside. So before the first change the hook
// watches: kProofCalls calls for user 0 in a row whose nine floats agreed with the record (CheckRecord), at least
// one of them with a forward whose x is not zero, so the mirror was exercised and not just compared 0 with -0.
// A disagreement before that starts the count again. Once proven, a call whose record does not agree still
// hands the middleware the head (the nine floats alone are enough for that) but leaves the record as it is;
// kProofWithdrawRun such calls in a row withdraw the proof, and the whole proof is taken again.
// (These are thresholds of the proof, not addresses or offsets: nothing here locates anything in the game.)
inline constexpr uint32_t kProofCalls = 30;
inline constexpr uint32_t kProofWithdrawRun = 60;
struct HandOffProof
{
    uint32_t agreed = 0;      // agreeing calls since the last disagreement (until proven)
    bool mirrorSeen = false;  // one of them had a forward with a non-zero x
    uint32_t disagreed = 0;   // disagreements in a row (once proven)
    bool proven = false;
};
enum class ProofStep : uint8_t
{
    Agreed,
    Disagreed,
    Proven,     // this call completed the proof
    Withdrawn,  // this call withdrew it
    Count
};
inline ProofStep StepProof(HandOffProof& proof, RecordState state, const float nine[9])
{
    if (state == RecordState::Agrees)
    {
        proof.disagreed = 0;
        if (proof.proven)
            return ProofStep::Agreed;
        ++proof.agreed;
        if (nine[0] != 0.0f)
            proof.mirrorSeen = true;
        if (proof.agreed >= kProofCalls && proof.mirrorSeen)
        {
            proof.proven = true;
            return ProofStep::Proven;
        }
        return ProofStep::Agreed;
    }
    if (!proof.proven)
    {
        proof = HandOffProof{};
        return ProofStep::Disagreed;
    }
    if (++proof.disagreed >= kProofWithdrawRun)
    {
        proof = HandOffProof{};
        return ProofStep::Withdrawn;
    }
    return ProofStep::Disagreed;
}

// ---- the verdict -----------------------------------------------------------------------------------------------
// The head as one rendered frame published it: its orientation with a clock and a generation (HeadSample), its
// position, the game's own camera of that same frame before the head pose was applied -- the aim -- and where
// the two eye cameras of that frame are.
//
// WHY THE EYES ARE HERE. The camera the listener update copies is the per-user observer result
// (docs/HALO4-SIGNATURE-EVIDENCE.md records the same address for it), and that is the very record this mod's
// stereo transaction writes each eye's camera into and puts back afterwards. A listener build that runs on
// another thread while the transaction holds the record copies an EYE camera: already turned to the head, and
// already at the head (half the eye distance to one side). Such a build is recognised by identity -- its position
// is, bit for bit, one of the two eye positions this frame published -- and is then placed at the head itself
// instead of being moved by the head's offset a second time.
struct HeadFrame
{
    head_relative::HeadSample sample;
    float position[3]{};
    float aimForward[3]{};
    float aimPosition[3]{};
    float eyePosition[2][3]{};
};
// Was a listener at `position` copied from one of this frame's eye cameras? Identity, not nearness.
inline bool BuiltFromEye(const float position[3], const HeadFrame& frame)
{
    for (int eye = 0; eye < 2; ++eye)
    {
        const float* at = frame.eyePosition[eye];
        if (head_relative::Finite3(at) && position[0] == at[0] && position[1] == at[1] && position[2] == at[2])
            return true;
    }
    return false;
}
enum class ListenerUse : uint8_t
{
    Used = 0,           // the middleware's copy and the record both carry the head
    HandOffOnly,        // the middleware's copy carries the head; the record did not agree and was left alone
    OtherCaller,        // not the update's call: forwarded untouched
    NotLocalUser,       // the hand-off is not output user 0's
    UnexpectedHandOff,  // the nine floats are not a unit, perpendicular forward and up with a finite position
    Unproven,           // the nine floats have not (yet) agreed with the record often enough
    SwitchOff,          // the player's switch is off
    NoSample,           // no frame has published a head yet, or the head view was not live in it
    OtherGeneration,    // the sample belongs to another load of the title
    FromTheFuture,      // the sample's clock is ahead of this one's
    Stale,              // older than the age limit
    NotGameplay,        // a cinematic held the camera in that frame
    NoHeadBasis,        // the head's forward and up do not make a frame, or its position is not finite
    Count
};
inline const char* ListenerUseName(ListenerUse use)
{
    switch (use)
    {
    case ListenerUse::Used: return "used";
    case ListenerUse::HandOffOnly: return "the middleware only (the record differed)";
    case ListenerUse::OtherCaller: return "another caller";
    case ListenerUse::NotLocalUser: return "not the local user";
    case ListenerUse::UnexpectedHandOff: return "unexpected hand-off";
    case ListenerUse::Unproven: return "unproven";
    case ListenerUse::SwitchOff: return "switch off";
    case ListenerUse::NoSample: return "no head sample";
    case ListenerUse::OtherGeneration: return "another generation";
    case ListenerUse::FromTheFuture: return "sample from the future";
    case ListenerUse::Stale: return "stale head sample";
    case ListenerUse::NotGameplay: return "not gameplay";
    case ListenerUse::NoHeadBasis: return "no head frame";
    default: return "?";
    }
}
inline bool ListenerChanged(ListenerUse use) { return use == ListenerUse::Used || use == ListenerUse::HandOffOnly; }
struct ListenerRequest
{
    bool fromUpdate = false;    // this call returns into the update's loop
    int32_t user = -1;          // the hand-off's own user index
    bool proven = false;        // HandOffProof::proven after this call's step
    bool switchOn = false;      // g_config.audio_follows_head, read at this use
    bool gameplay = false;      // the frame the head sample came from was the player's, not a cinematic's
    bool movePosition = true;   // also move the listener by the head's offset from the game's camera
    uint32_t generation = 0;    // the title generation now
    uint64_t nowUs = 0;         // the sight clock now
};
struct ListenerResult
{
    float handOff[9]{};     // what the middleware is handed instead of the game's nine floats
    ListenerBasis basis;    // the record's three rows
    float position[3]{};    // the record's position (and the hand-off's, mirrored)
    float velocity[3]{};    // the record's velocity seen from the new rows
    bool recordToo = false; // the record agreed on this call: write basis, position and velocity into it
    bool fromEye = false;   // the game built this listener from one of the mod's own eye cameras (BuiltFromEye)
};
// What output user 0's listener should carry. Used / HandOffOnly = `out` is filled: the head's forward and up as
// the game's builder would store them (left = up x forward), the game's listener position moved by the head's
// offset from the game's camera of the same frame (so it sits at the head, and a listener the game placed
// somewhere else -- a seat seen from outside -- keeps the game's placement plus that offset; a listener the game
// copied from one of the mod's own eye cameras is put at the head itself), the same world velocity seen from
// the new rows, and the nine floats under the game's own mirror. Anything else names why the game's own listener
// stands, and `out` is untouched.
inline ListenerUse DecideListener(const ListenerRequest& request, const HeadFrame& frame, const float nine[9],
                                  const ListenerRecord& record, RecordState recordState, ListenerResult& out)
{
    using head_relative::Finite3;
    using head_relative::Use;
    if (!request.fromUpdate)
        return ListenerUse::OtherCaller;
    if (request.user != 0)
        return ListenerUse::NotLocalUser;
    float forward[3], up[3], position[3];
    FromHandOff(nine, forward, up, position);
    if (!PairAsBuilt(forward, up) || !Finite3(position))
        return ListenerUse::UnexpectedHandOff;
    if (!request.proven)
        return ListenerUse::Unproven;
    if (!request.switchOn)
        return ListenerUse::SwitchOff;
    switch (head_relative::SampleUse(frame.sample, request.generation, request.nowUs))
    {
    case Use::Ok: break;
    case Use::OtherGeneration: return ListenerUse::OtherGeneration;
    case Use::FromTheFuture: return ListenerUse::FromTheFuture;
    case Use::Stale: return ListenerUse::Stale;
    case Use::NoHeading: return ListenerUse::NoHeadBasis;
    default: return ListenerUse::NoSample;
    }
    if (!request.gameplay)
        return ListenerUse::NotGameplay;
    ListenerResult result;
    if (!HeadBasis(frame.sample.forward, frame.sample.up, result.basis))
        return ListenerUse::NoHeadBasis;
    for (int i = 0; i < 3; ++i)
        result.position[i] = position[i];
    result.fromEye = BuiltFromEye(position, frame);
    if (request.movePosition)
    {
        if (!Finite3(frame.position) || (!result.fromEye && !Finite3(frame.aimPosition)))
            return ListenerUse::NoHeadBasis;
        for (int i = 0; i < 3; ++i)
            result.position[i] = result.fromEye ? frame.position[i]
                                                : position[i] + (frame.position[i] - frame.aimPosition[i]);
        if (!Finite3(result.position))
            return ListenerUse::NoHeadBasis;
    }
    ToHandOff(result.basis.forward, result.basis.up, result.position, result.handOff);
    if (recordState == RecordState::Agrees)
    {
        float world[3];
        FromListenerSpace(record.basis, record.velocity, world);
        ToListenerSpace(result.basis, world, result.velocity);
        result.recordToo = Finite3(result.velocity);
    }
    out = result;
    return result.recordToo ? ListenerUse::Used : ListenerUse::HandOffOnly;
}

// ---- THE MEASUREMENT (the dev report prints it; pure so the tests can hold it) ---------------------------------
// "Does Halo 4's sound follow the aim?" in three numbers per build: the angle between the listener's forward and
// the head's, the angle between it and the aim's (the game's own camera of the frame the head was published
// from), and how far the listener sits from the head.
inline float AngleDeg(const float a[3], const float b[3])
{
    using namespace head_relative;
    const float lengths = Length(a) * Length(b);
    if (!Finite3(a) || !Finite3(b) || !(lengths > 1.0e-12f))
        return 0.0f;
    float cross[3];
    Cross(a, b, cross);
    return std::atan2(Length(cross), Dot(a, b)) * kRadToDeg;
}
inline float Distance(const float a[3], const float b[3])
{
    const float d[3]{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return head_relative::Length(d);
}
struct Measurement
{
    float fromHeadDeg = 0.0f;   // the listener's forward against the head's forward
    float fromAimDeg = 0.0f;    // the listener's forward against the aim's forward
    float fromHead = 0.0f;      // the listener's position to the head's position, in world units
};
inline Measurement Measure(const float listenerForward[3], const float listenerPosition[3], const HeadFrame& frame)
{
    Measurement measured;
    measured.fromHeadDeg = AngleDeg(listenerForward, frame.sample.forward);
    measured.fromAimDeg = AngleDeg(listenerForward, frame.aimForward);
    measured.fromHead = Distance(listenerPosition, frame.position);
    return measured;
}
// What a window of builds says, from the mean angles. Only when the head and the aim pointed apart can the two
// be told from each other.
enum class Follows : uint8_t
{
    Inconclusive,  // the head and the aim were within kFollowsApartDeg of each other
    Aim,
    Head,
    Neither,
};
inline constexpr float kFollowsApartDeg = 10.0f;
inline Follows WhatItFollows(float fromHeadDeg, float fromAimDeg, float apartDeg)
{
    if (!(apartDeg >= kFollowsApartDeg) || !std::isfinite(fromHeadDeg) || !std::isfinite(fromAimDeg))
        return Follows::Inconclusive;
    const float nearBound = apartDeg * 0.25f, farBound = apartDeg * 0.5f;
    if (fromAimDeg <= nearBound && fromHeadDeg >= farBound)
        return Follows::Aim;
    if (fromHeadDeg <= nearBound && fromAimDeg >= farBound)
        return Follows::Head;
    return Follows::Neither;
}
inline const char* FollowsName(Follows follows)
{
    switch (follows)
    {
    case Follows::Aim: return "FOLLOWS THE AIM";
    case Follows::Head: return "FOLLOWS THE HEAD";
    case Follows::Neither: return "follows neither the head nor the aim";
    default: return "cannot be told (the head and the aim pointed the same way: point them apart)";
    }
}
} // namespace halo4_sound_listener
