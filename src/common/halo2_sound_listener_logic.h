// HALO 2 (halo2.dll): THE SOUND LISTENER WITNESS, the pure half (no game, engine or OpenXR includes). DEV BUILDS
// ONLY: src/dll/halo2_sound_listener_witness.inl includes this inside #if HALOMCCVR_DEV_DIAGNOSTICS. It reads; it
// hooks nothing and writes nothing a game reads.
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED. docs/EDITING-KIT-EVIDENCE.md's Halo 2 rule begins every
// new binding with the official kit; this one does not (no editing-kit symbols were available). Everything below
// is read from retail halo2.dll (SHA-256 DE65B4F4...CF171946) alone, so every name is ours and nothing here is a
// kit fact. A witness is the right thing to build on a lead: it changes nothing, and what it prints is a
// measurement of the running game, which is the evidence the lead lacks.
//
// WHAT RETAIL DOES (tools/verify-halo2-sound-listener.py re-derives every address from the patterns here).
// The listener update (+0x6E49B0, one bool argument) walks the four local users. For each one that is playing
// (+0x6A4390: *[+0xE80A20] + 0xC + user * 4 is not -1) it fills listener `user` in the sound globals
// (*[+0x15F21A0] + 0xA8 + user * 0x48):
//     +0x00  location (dword)      +0x04  cluster (word)
//     +0x06  "valid" byte: set to 1 here and never cleared by this function
//     +0x07  a byte returned by +0x6783D0(location, position)
//     +0x08  a matrix of 0x34 bytes, built by +0x729BA0(matrix, position, forward, up)
//     +0x3C  the velocity in the listener's frame, +0x72A0A0(matrix, velocity, out)
// A user who is not playing keeps the valid byte it had and gets location -1 and cluster -1: its matrix is left
// as it was. Position, forward, up and velocity are copied from the OBSERVER RESULT (+0x6F0E60(user) ->
// +0x15F297C + user * 0x368: position +0x00, velocity +0x14, forward +0x20, up +0x2C), the record this mod's
// observer core writes the head camera into -- EXCEPT in one branch: the director's perspective is 1 (the
// following camera), the user's unit has a parent and a seat index. There the position is moved from the
// observer's toward the unit's head marker by the seat definition's float at +0x40, and forward and up are
// turned by the same fraction of the rotation that takes the observer's forward onto the unit's desired looking
// vector (unit +0x18C, which player control's desired angles -- the aim -- drive). A seat seen through the
// following camera is therefore the one state in which, statically, the listener is neither the head nor the aim.
//
// THE MATRIX, FROM THE BUILDER'S OWN STORES. +0x729BA0 is a leaf of 0xB6 bytes with no relocated operand, so the
// pattern below is every byte of it and its displacement bytes ARE the layout:
//     [rcx + 0x00] = 1.0f                    scale
//     [rcx + 0x04 .. 0x0C] <- [r8]           forward
//     [rcx + 0x10 .. 0x18] = up x forward    left
//     [rcx + 0x1C .. 0x24] <- [r9]           up
//     [rcx + 0x28 .. 0x30] <- [rdx]          position
// and the update's call site says what r8, r9 and rdx are: three stack slots it has just filled from the
// observer result's +0x20, +0x2C and +0x00. DecodeSites reads all of it from the matched bytes; nothing about
// the convention is assumed.
//
// ADDR-HYGIENE: resolved + guarded. No address and no offset ships. Three patterns are each matched exactly once
// (the update's loop head, its copy-and-build tail, the matrix builder); the tail's jump must land on the head,
// its call on the builder, its first call on the function that holds the observer core's own uniquely matched
// accessor arithmetic, whose array must be the one the observer core writes into; the two sites must agree on
// the valid byte; the matrix must be 0x34 bytes ending where the velocity begins; the stride must be exactly
// one listener. At run time every read is guarded, read twice, and refused unless the rows are a rotation of
// scale 1 whose second row is up x forward.
#pragma once

#include "sound_listener_witness_logic.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace halo2_sound_listener
{
// ---- the sites -------------------------------------------------------------------------------------------------
// The numbers beside each pattern are positions INSIDE that matched pattern, never image addresses (structural;
// the unit tests check that each lands on the bytes its comment names).

// The update's per-user loop head:
//     mov r12, [rip + sound globals] ; mov ecx, r14d ; movzx ebx, byte [rsi + r12 + valid] ; call playing ;
//     test al, al ; je not-playing
inline constexpr const char* kListenerLoopPattern =
    "4C 8B 25 ?? ?? ?? ?? 41 8B CE 42 0F B6 9C 26 AE 00 00 00 E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ??";
inline constexpr size_t kLoopBytes = 32;
inline constexpr size_t kLoopGlobalsDisp = 3, kLoopGlobalsNext = 7, kLoopValid = 15, kLoopCallPlaying = 19;

// The update's plain copy of the observer result and the build of one listener, through the loop's end:
//     mov ecx, r14d ; call observer accessor ; copy forward, up, position, velocity and the location to the
//     stack ; valid = 1 ; byte7 = f(location, position) ; builder(matrix, position, forward, up) ;
//     velocity transform ; location ; cluster ; (not playing: valid as it was, location -1, cluster -1) ;
//     inc user ; add rsi, stride ; cmp user, count ; jl loop head
inline constexpr const char* kListenerBuildPattern =
    "41 8B CE E8 ?? ?? ?? ?? 48 8B D0 F2 0F 10 40 20 F2 0F 11 44 24 40 8B 48 28 89 4C 24 48 F2 0F 10 "
    "40 2C F2 0F 11 44 24 30 8B 48 34 89 4C 24 38 F2 0F 10 00 F2 0F 11 44 24 20 8B 48 08 89 4C 24 28 "
    "F2 0F 10 40 14 F2 0F 11 44 24 50 8B 40 1C 89 44 24 58 48 8B 42 0C 48 89 45 48 45 33 C0 42 C6 84 "
    "26 AE 00 00 00 01 48 8D 54 24 20 48 8D 4D 48 E8 ?? ?? ?? ?? 49 8D 9C 24 B0 00 00 00 42 88 84 26 "
    "AF 00 00 00 48 03 DE 4C 8D 4C 24 30 48 8B CB 4C 8D 44 24 40 48 8D 54 24 20 E8 ?? ?? ?? ?? 4D 8D "
    "84 24 E4 00 00 00 48 8B CB 4C 03 C6 48 8D 54 24 50 E8 ?? ?? ?? ?? 8B 45 48 BF FF FF FF FF 42 89 "
    "84 26 A8 00 00 00 0F B7 45 4C 66 42 89 84 26 AC 00 00 00 EB 19 42 88 9C 26 AE 00 00 00 42 89 BC "
    "26 A8 00 00 00 66 42 89 BC 26 AC 00 00 00 41 FF C6 48 83 C6 48 41 83 FE 04 0F 8C ?? ?? ?? ??";
inline constexpr size_t kBuildBytes = 255;
inline constexpr size_t kBuildCallAccessor = 3;
// The observer result's fields (one displacement byte each; the position's load has none: it is at +0), and the
// stack slot each is copied to.
inline constexpr size_t kBuildObserverForward = 15, kBuildForwardSlot = 21, kBuildObserverUp = 33, kBuildUpSlot = 39,
                        kBuildPositionLoad = 47, kBuildPositionSlot = 56, kBuildObserverVelocity = 68,
                        kBuildVelocitySlot = 74, kBuildObserverLocation = 85;
// The listener's fields, as offsets from the sound globals (four bytes each).
inline constexpr size_t kBuildValid = 97, kBuildValidValue = 101, kBuildMatrix = 120, kBuildByte7 = 128,
                        kBuildVelocity = 162, kBuildLocation = 194, kBuildCluster = 207;
// The builder's three pointer arguments (the stack slot each lea takes) and the velocity transform's.
inline constexpr size_t kBuildArgUp = 139, kBuildArgForward = 147, kBuildArgPosition = 152, kBuildCallBuilder = 153,
                        kBuildArgVelocity = 176, kBuildCallVelocity = 177;
inline constexpr size_t kBuildStride = 244, kBuildUsers = 248, kBuildLoopJump = 249;

// The matrix builder, every byte (a leaf with no relocated operand).
inline constexpr const char* kListenerMatrixBuilderPattern =
    "48 83 EC 28 C7 01 00 00 80 3F F2 41 0F 10 00 F2 0F 11 41 04 41 8B 40 08 89 41 0C F3 41 0F 10 59 "
    "08 F3 41 0F 10 50 08 0F 28 C3 F3 41 0F 10 21 0F 28 CA F3 41 0F 10 28 0F 29 74 24 10 F3 41 0F 10 "
    "71 04 0F 29 3C 24 F3 41 0F 10 78 04 F3 0F 59 C7 F3 0F 59 CE F3 0F 59 FC F3 0F 5C C8 F3 0F 59 F5 "
    "F3 0F 59 DD F3 0F 5C FE F3 0F 59 D4 0F 28 74 24 10 F3 0F 11 49 10 F3 0F 5C DA F3 0F 11 79 18 0F "
    "28 3C 24 F3 0F 11 59 14 F2 41 0F 10 01 F2 0F 11 41 1C 41 8B 41 08 89 41 24 33 C0 48 89 41 28 89 "
    "41 30 F2 0F 10 02 F2 0F 11 41 28 8B 42 08 89 41 30 48 83 C4 28 C3";
inline constexpr size_t kBuilderBytes = 182;
// `mov dword [rcx], imm32` (scale at +0: the store has no displacement) and its four immediate bytes; then the
// one displacement byte of each store. X and Y of a row are one 8-byte store, Z a 4-byte one.
inline constexpr size_t kBuilderScaleStore = 4, kBuilderScaleValue = 6;
inline constexpr size_t kBuilderForwardLoad = 10, kBuilderForwardXY = 19, kBuilderForwardZ = 26;
inline constexpr size_t kBuilderLeftX = 117, kBuilderLeftZ = 126, kBuilderLeftY = 135;
inline constexpr size_t kBuilderUpLoad = 136, kBuilderUpXY = 145, kBuilderUpZ = 152;
inline constexpr size_t kBuilderPositionLoad = 162, kBuilderPositionXY = 170, kBuilderPositionZ = 176;

// "This local user is playing", the leaf the loop head calls:
//     cmp ecx, -1 ; je no ; mov rax, [rip + slot] ; movsxd rcx, ecx ; cmp dword [rax + rcx*4 + 0xC], -1 ;
//     setne al ; ret ; no: xor al, al ; ret
// Not scanned for: it is read at the loop head's call target and must be exactly this.
inline constexpr const char* kPlayingLeafPattern =
    "83 F9 FF 74 13 48 8B 05 ?? ?? ?? ?? 48 63 C9 83 7C 88 0C FF 0F 95 C0 C3 32 C0 C3";
inline constexpr size_t kPlayingBytes = 27;
inline constexpr size_t kPlayingSlotDisp = 8, kPlayingSlotNext = 12, kPlayingOffset = 18;
inline constexpr uint32_t kPlayingStride = 4;   // `rcx*4` in the leaf's own addressing byte (0x88 at 17)

// The observer core's own accessor arithmetic (src/common/halo2_render_logic.h,
// kHalo2ObserverResultAccessorPattern: imul rax, rbx, stride ; lea rcx, [rip + array] ; add rax, rcx) lies this
// close behind the entry of the function the listener update calls for its source. (The accessor is 0x38 bytes.)
inline constexpr int64_t kAccessorSpan = 0x40;

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
// Where the E8 at `site + opcode` lands, as an offset from the module base (`siteRva` is the site's own).
inline int64_t BranchRva(const uint8_t* site, size_t opcode, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(opcode) + 5 + ReadI32(site + opcode + 1);
}
inline int64_t RipRva(const uint8_t* site, size_t disp, size_t next, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(next) + ReadI32(site + disp);
}
// A pattern against bytes, wildcards skipped. `bytes` must hold as many bytes as the pattern has tokens.
inline bool MatchesPattern(const uint8_t* bytes, const char* pattern)
{
    if (!bytes || !pattern)
        return false;
    size_t i = 0;
    for (const char* at = pattern; *at;)
    {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        if (at[0] == '?')
        {
            while (*at == '?')
                ++at;
            ++i;
            continue;
        }
        unsigned value = 0;
        for (int digit = 0; digit < 2; ++digit, ++at)
        {
            const char c = *at;
            value = value * 16 + static_cast<unsigned>(c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        if (bytes[i++] != static_cast<uint8_t>(value))
            return false;
    }
    return true;
}

struct Sites
{
    int64_t loopRva = 0;            // the update's per-user loop head
    int64_t buildRva = 0;           // its copy-and-build tail
    int64_t builderRva = 0;         // the matrix builder
    int64_t velocityRva = 0;        // the world-to-listener vector transform the velocity goes through
    int64_t accessorRva = 0;        // the function the update asks for its source (the observer result)
    int64_t playingRva = 0;         // "this local user is playing"
    int64_t globalsSlotRva = 0;     // the pointer to the sound globals
    int64_t playingSlotRva = 0;     // the pointer the playing leaf reads through
    uint32_t playingOffset = 0;     // ...and where the per-user dword is behind it (0xC)
    uint32_t listenerOffset = 0;    // listener 0 in the sound globals (0xA8)
    uint32_t stride = 0;            // one listener (0x48)
    uint32_t users = 0;             // listeners (4)
    // Inside one listener.
    uint32_t clusterOffset = 0, validOffset = 0, byte7Offset = 0, matrixOffset = 0, velocityOffset = 0;
    // Inside the matrix, from the builder's stores.
    uint32_t scaleOffset = 0, forwardOffset = 0, leftOffset = 0, upOffset = 0, positionOffset = 0, matrixBytes = 0;
    // The source record's fields, from the update's copy.
    int64_t observerArrayRva = 0;
    uint32_t observerStride = 0;
    uint32_t observerPosition = 0, observerVelocity = 0, observerForward = 0, observerUp = 0, observerLocation = 0;
};
enum class Bind : uint8_t
{
    Bound = 0,
    LoopMissing,          // the loop head: no match, or more than one
    BuildMissing,         // the copy-and-build tail: no match, or more than one
    BuilderMissing,       // the matrix builder: no match, or more than one
    LoopMismatch,         // the tail does not jump back to the matched head, or the two disagree on the valid byte
    BuilderMismatch,      // the tail does not call the matched builder
    ArgumentMismatch,     // the builder's arguments are not the slots the observer's fields were copied to
    LayoutMismatch,       // the builder's stores are not scale, forward, left, up, position in 0x34 bytes
    RecordMismatch,       // the listener's fields do not make one record of the stride
    PlayingMismatch,      // the loop head does not call the playing leaf
    AccessorMismatch,     // the source is not the observer core's accessor, array or stride
    ObserverMismatch,     // the fields copied are not the three spans the observer core writes
    Implausible,          // an operand out of range
    Count
};
inline const char* BindName(Bind why)
{
    switch (why)
    {
    case Bind::Bound: return "bound";
    case Bind::LoopMissing: return "the listener update's loop head is missing or ambiguous";
    case Bind::BuildMissing: return "the listener update's copy-and-build tail is missing or ambiguous";
    case Bind::BuilderMissing: return "the matrix builder is missing or ambiguous";
    case Bind::LoopMismatch: return "the build tail and the loop head do not agree (jump target or valid byte)";
    case Bind::BuilderMismatch: return "the build tail does not call the matched matrix builder";
    case Bind::ArgumentMismatch: return "the builder's arguments are not the observer result's position, forward and up";
    case Bind::LayoutMismatch: return "the builder does not store scale, forward, left, up, position";
    case Bind::RecordMismatch: return "the listener's fields do not make one record of the stride";
    case Bind::PlayingMismatch: return "the loop head does not call the playing leaf";
    case Bind::AccessorMismatch: return "the listener's source is not the observer result this mod writes";
    case Bind::ObserverMismatch: return "the fields copied are not the ones the observer core writes";
    case Bind::Implausible: return "the decoded operands are implausible";
    default: return "?";
    }
}

// What the observer core holds on its own (src/common/halo2_render_logic.h and the array address its cold
// observation resolved): the witness's source must be exactly that.
struct ObserverRecord
{
    int64_t accessorMatchRva = -1;   // the unique match of the core's accessor arithmetic (-1: missing/ambiguous)
    int64_t arrayRva = 0;            // the result array the core writes into, as an offset from the module base
    uint32_t stride = 0;             // one user's observer (0x368)
    uint32_t position = 0, forward = 0, up = 0;   // the three spans the core writes
    // Positions inside the core's accessor pattern.
    size_t strideAt = 0, dispAt = 0, nextAt = 0;
};

// `loop`, `build`, `builder`: the unique matches (null = missing or ambiguous) with their offsets from the module
// base; `playing`: kPlayingBytes read at the loop head's call target (null when unreadable); `accessor`: the bytes
// at the observer core's accessor match (null when unreadable).
inline Bind DecodeSites(const uint8_t* loop, int64_t loopRva, const uint8_t* build, int64_t buildRva,
                        const uint8_t* builder, int64_t builderRva, const uint8_t* playing, const uint8_t* accessor,
                        const ObserverRecord& observer, Sites& out)
{
    if (!loop)
        return Bind::LoopMissing;
    if (!build)
        return Bind::BuildMissing;
    if (!builder)
        return Bind::BuilderMissing;
    Sites sites;
    sites.loopRva = loopRva;
    sites.buildRva = buildRva;
    sites.builderRva = builderRva;
    // The tail's last jump is the loop's own; both name the valid byte.
    const int64_t jumpTarget =
        buildRva + static_cast<int64_t>(kBuildLoopJump) + 6 + ReadI32(build + kBuildLoopJump + 2);
    const uint32_t valid = ReadU32(build + kBuildValid);
    if (jumpTarget != loopRva || ReadU32(loop + kLoopValid) != valid || build[kBuildValidValue] != 1)
        return Bind::LoopMismatch;
    if (BranchRva(build, kBuildCallBuilder, buildRva) != builderRva)
        return Bind::BuilderMismatch;
    // The builder: scale 1.0 at +0, then forward <- r8, left, up <- r9, position <- rdx.
    static constexpr uint8_t kScaleStore[2]{0xC7, 0x01};              // mov dword [rcx], imm32
    static constexpr uint8_t kLoadR8[5]{0xF2, 0x41, 0x0F, 0x10, 0x00};  // movsd xmm0, [r8]
    static constexpr uint8_t kLoadR9[5]{0xF2, 0x41, 0x0F, 0x10, 0x01};  // movsd xmm0, [r9]
    static constexpr uint8_t kLoadRdx[4]{0xF2, 0x0F, 0x10, 0x02};       // movsd xmm0, [rdx]
    const float one = 1.0f;
    if (std::memcmp(builder + kBuilderScaleStore, kScaleStore, sizeof(kScaleStore)) != 0 ||
        std::memcmp(builder + kBuilderScaleValue, &one, sizeof(one)) != 0 ||
        std::memcmp(builder + kBuilderForwardLoad, kLoadR8, sizeof(kLoadR8)) != 0 ||
        std::memcmp(builder + kBuilderUpLoad, kLoadR9, sizeof(kLoadR9)) != 0 ||
        std::memcmp(builder + kBuilderPositionLoad, kLoadRdx, sizeof(kLoadRdx)) != 0)
        return Bind::LayoutMismatch;
    sites.scaleOffset = 0;
    sites.forwardOffset = builder[kBuilderForwardXY];
    sites.leftOffset = builder[kBuilderLeftX];
    sites.upOffset = builder[kBuilderUpXY];
    sites.positionOffset = builder[kBuilderPositionXY];
    sites.matrixBytes = sites.positionOffset + 12;
    if (builder[kBuilderForwardZ] != sites.forwardOffset + 8 || builder[kBuilderLeftY] != sites.leftOffset + 4 ||
        builder[kBuilderLeftZ] != sites.leftOffset + 8 || builder[kBuilderUpZ] != sites.upOffset + 8 ||
        builder[kBuilderPositionZ] != sites.positionOffset + 8 || sites.forwardOffset != sizeof(float) ||
        sites.leftOffset != sites.forwardOffset + 12 || sites.upOffset != sites.leftOffset + 12 ||
        sites.positionOffset != sites.upOffset + 12)
        return Bind::LayoutMismatch;
    // The call site: r8, r9 and rdx are the slots the observer's forward, up and position were copied to, and
    // the velocity transform's source is the slot its velocity was copied to.
    if (build[kBuildArgForward] != build[kBuildForwardSlot] || build[kBuildArgUp] != build[kBuildUpSlot] ||
        build[kBuildArgPosition] != build[kBuildPositionSlot] ||
        build[kBuildArgVelocity] != build[kBuildVelocitySlot] ||
        std::memcmp(build + kBuildPositionLoad, "\xF2\x0F\x10\x00", 4) != 0)   // movsd xmm0, [rax]: position at +0
        return Bind::ArgumentMismatch;
    sites.velocityRva = BranchRva(build, kBuildCallVelocity, buildRva);
    // One listener: location, cluster, valid, byte 7, the matrix, the velocity, and nothing after it.
    const uint32_t location = ReadU32(build + kBuildLocation);
    const uint32_t cluster = ReadU32(build + kBuildCluster);
    const uint32_t byte7 = ReadU32(build + kBuildByte7);
    const uint32_t matrix = ReadU32(build + kBuildMatrix);
    const uint32_t velocity = ReadU32(build + kBuildVelocity);
    sites.stride = build[kBuildStride];
    sites.users = build[kBuildUsers];
    if (!(location < cluster && cluster < valid && valid < byte7 && byte7 < matrix && matrix < velocity) ||
        velocity - matrix != sites.matrixBytes || velocity + 12 - location != sites.stride ||
        cluster - location != sizeof(uint32_t) || !sites.users || sites.users > 8)
        return Bind::RecordMismatch;
    sites.listenerOffset = location;
    sites.clusterOffset = cluster - location;
    sites.validOffset = valid - location;
    sites.byte7Offset = byte7 - location;
    sites.matrixOffset = matrix - location;
    sites.velocityOffset = velocity - location;
    sites.globalsSlotRva = RipRva(loop, kLoopGlobalsDisp, kLoopGlobalsNext, loopRva);
    // The playing leaf.
    sites.playingRva = BranchRva(loop, kLoopCallPlaying, loopRva);
    if (!playing || !MatchesPattern(playing, kPlayingLeafPattern))
        return Bind::PlayingMismatch;
    sites.playingSlotRva = RipRva(playing, kPlayingSlotDisp, kPlayingSlotNext, sites.playingRva);
    sites.playingOffset = playing[kPlayingOffset];
    // The source: the observer core's own accessor, array and stride.
    sites.accessorRva = BranchRva(build, kBuildCallAccessor, buildRva);
    if (!accessor || observer.accessorMatchRva < 0 || observer.accessorMatchRva < sites.accessorRva ||
        observer.accessorMatchRva - sites.accessorRva >= kAccessorSpan)
        return Bind::AccessorMismatch;
    sites.observerStride = ReadU32(accessor + observer.strideAt);
    sites.observerArrayRva = RipRva(accessor, observer.dispAt, observer.nextAt, observer.accessorMatchRva);
    if (sites.observerStride != observer.stride || sites.observerArrayRva != observer.arrayRva)
        return Bind::AccessorMismatch;
    sites.observerPosition = 0;
    sites.observerForward = build[kBuildObserverForward];
    sites.observerUp = build[kBuildObserverUp];
    sites.observerVelocity = build[kBuildObserverVelocity];
    sites.observerLocation = build[kBuildObserverLocation];
    if (sites.observerPosition != observer.position || sites.observerForward != observer.forward ||
        sites.observerUp != observer.up)
        return Bind::ObserverMismatch;
    if (sites.globalsSlotRva <= 0 || sites.playingSlotRva <= 0 || sites.observerArrayRva <= 0 ||
        sites.accessorRva <= 0 || sites.playingRva <= 0 || sites.velocityRva <= 0 || sites.stride < sites.matrixBytes)
        return Bind::Implausible;
    out = sites;
    return Bind::Bound;
}

// ---- one listener ------------------------------------------------------------------------------------------------
struct Record
{
    int32_t location = -1;
    int16_t cluster = -1;
    uint8_t valid = 0, byte7 = 0;
    float scale = 0.0f;
    listener_witness::Pose pose;      // position, forward, up: the matrix's rows
    float left[3]{};
    float velocity[3]{};              // in the listener's frame
};
// One listener's bytes (`sites.stride` of them) into a Record. Reads only; decides nothing.
inline void DecodeRecord(const uint8_t* bytes, const Sites& sites, Record& out)
{
    std::memcpy(&out.location, bytes, sizeof(out.location));
    std::memcpy(&out.cluster, bytes + sites.clusterOffset, sizeof(out.cluster));
    out.valid = bytes[sites.validOffset];
    out.byte7 = bytes[sites.byte7Offset];
    const uint8_t* matrix = bytes + sites.matrixOffset;
    std::memcpy(&out.scale, matrix + sites.scaleOffset, sizeof(float));
    std::memcpy(out.pose.forward, matrix + sites.forwardOffset, sizeof(out.pose.forward));
    std::memcpy(out.left, matrix + sites.leftOffset, sizeof(out.left));
    std::memcpy(out.pose.up, matrix + sites.upOffset, sizeof(out.pose.up));
    std::memcpy(out.pose.position, matrix + sites.positionOffset, sizeof(out.pose.position));
    std::memcpy(out.velocity, bytes + sites.velocityOffset, sizeof(out.velocity));
}
// The verdict on one listener that was read twice (`first`, `second`: sites.stride bytes each) while the game's
// own test said whether user 0 was playing. Ok = `out` is a listener to measure.
enum class Read : uint8_t { Ok, NotPlaying, NeverBuilt, Torn, NotABasis };
inline Read ReadListener(const uint8_t* first, const uint8_t* second, const Sites& sites, bool playing, Record& out)
{
    if (!playing)
        return Read::NotPlaying;
    if (std::memcmp(first, second, sites.stride) != 0)
        return Read::Torn;
    DecodeRecord(first, sites, out);
    if (!out.valid)
        return Read::NeverBuilt;
    if (!(out.scale == 1.0f) || !listener_witness::Finite3(out.pose.position) ||
        !listener_witness::IsBlamBasis(out.pose.forward, out.left, out.pose.up))
        return Read::NotABasis;
    return Read::Ok;
}
inline listener_witness::Refusal RefusalOf(Read read)
{
    using listener_witness::Refusal;
    switch (read)
    {
    case Read::NotPlaying: return Refusal::NotPlaying;
    case Read::NeverBuilt: return Refusal::NeverBuilt;
    case Read::Torn: return Refusal::Torn;
    default: return Refusal::NotABasis;
    }
}

// THE MODEL of the game's builder, for the unit tests and the offline verifier's cross-check only (ADDR-HYGIENE:
// not used at run time): what +0x729BA0 stores for a position, a forward and an up.
inline void BuildMatrixModel(const float position[3], const float forward[3], const float up[3], const Sites& sites,
                             uint8_t* matrix)
{
    const float one = 1.0f;
    float left[3];
    head_relative::Cross(up, forward, left);
    std::memcpy(matrix + sites.scaleOffset, &one, sizeof(one));
    std::memcpy(matrix + sites.forwardOffset, forward, 12);
    std::memcpy(matrix + sites.leftOffset, left, 12);
    std::memcpy(matrix + sites.upOffset, up, 12);
    std::memcpy(matrix + sites.positionOffset, position, 12);
}
} // namespace halo2_sound_listener
