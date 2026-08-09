#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "aim_servo_logic.h"

// Halo 4-only render evidence and (eventually) allocation-free policy. This
// file contains no Windows, COM, MinHook, logging, or engine writes, so its
// contents can be exhaustively tested offline before any detour is
// authorized. It currently holds pinned identity constants ONLY: no RVA,
// AOB, stride, or layout may be added here without its proof first recorded
// in docs/HALO4-SIGNATURE-EVIDENCE.md (H4EK-first; the Reach script-table
// retail-derivation chain is UNPROVEN for Halo 4).

inline constexpr size_t kHalo4RetailFileSize = 17829336;
inline constexpr size_t kHalo4RetailImageSize = 0x04A3F000;
inline constexpr uint32_t kHalo4RetailPeTimestamp = 0x68A0E7BF;

// One retail Halo 4 build, signed once per storefront. As documented for the
// other titles (docs/MCC-EDITIONS-EVIDENCE.md), the Steam and Microsoft
// Store images are byte-identical apart from the Authenticode certificate,
// so the whole-file digest differs per edition while every RVA, the PE
// timestamp, and the image size stay shared. Both digests describe the one
// code image this file pins; an MCC update invalidates the whole table
// loudly through the identity preflight.
inline constexpr const char* kHalo4RetailModuleSha256[] = {
    // Steam
    "7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84",
    // Microsoft Store / Xbox app (Game Pass)
    "5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496",
};

// Kit-vs-retail build drift, same shape Reach had: kit facts transfer as
// semantics and layouts only, never as addresses.
inline constexpr char kHalo4KitBuildTag[] = "2023.06.27.176405.1-Release";
inline constexpr char kHalo4RetailBuildTag[] = "2025.08.16.178512.1";

// Data anchors the code anchors below must decode to (E-H4-4 table).
inline constexpr uint32_t kHalo4PlayerViewArrayRva = 0x30AD1C0;
inline constexpr uint32_t kHalo4PlayerViewStride = 0xAD0;
inline constexpr uint32_t kHalo4ViewStackTopRva = 0xE84634;

// E-H4-4 retail anchors (docs/HALO4-SIGNATURE-EVIDENCE.md, PROVEN 2026-08-07;
// the prologue-inclusive constructor variant is recorded under C-H4-2). Every
// pattern below was measured to match EXACTLY ONCE over the whole pinned
// module, at exactly the recorded RVA. C-H4-2's cold observation re-runs that
// measurement against the LOADED image; it admits no hook either way.
//
// ripDispOffset, when non-zero, is the byte offset of a rip-relative disp32
// INSIDE the matched bytes (the referencing instruction ends at
// ripDispOffset + 4), and the decode must land on ripTargetRva. Keeping the
// offset beside the pattern it indexes is load-bearing: a pattern whose
// prefix is lengthened without moving its offset decodes garbage.
struct Halo4RetailAnchor
{
    const char* name;
    const char* pattern;
    uint32_t rva;
    uint8_t ripDispOffset;
    uint32_t ripTargetRva;
};

// Table order is part of the contract - the named indices below and the
// level-load gate's pattern reuse in game.cpp bind to it.
inline constexpr size_t kHalo4AnchorCtor = 0;
inline constexpr size_t kHalo4AnchorPush = 1;
inline constexpr size_t kHalo4AnchorPop = 2;
inline constexpr size_t kHalo4AnchorClamp = 3;

inline constexpr Halo4RetailAnchor kHalo4RetailAnchors[] = {
    // Constructor loop of the 4 x 0xAD0 player-view array; the same shape
    // (and the same pattern string) the Halo 4 level-load gate resolves the
    // array from. The lea displacement at +0x0D decodes to the array base.
    { "player-view-array-ctor",
      "48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? BF 04 00 00 00 "
      "48 8B CB E8 ?? ?? ?? ?? 48 81 C3 D0 0A 00 00 48 83 EF 01 75 ?? "
      "48 8B 5C 24 30",
      0x22A50, 0x0D, kHalo4PlayerViewArrayRva },
    // Render-view stack push: refuses at top >= 3, stores the re-entry
    // callback at view+0x298. The mov r8d displacement decodes to
    // g_view_stack_top.
    { "render-view-stack-push",
      "48 83 EC 28 44 8B 05 ?? ?? ?? ?? 41 83 F8 03 7D ?? 41 FF C0 "
      "48 89 91 98 02 00 00",
      0x341760, 0x07, kHalo4ViewStackTopRva },
    // Render-view stack pop; its mov eax displacement decodes to the SAME
    // g_view_stack_top the push uses, which the preflight cross-checks by
    // pinning both to one ripTargetRva.
    { "render-view-stack-pop",
      "48 83 EC 28 8B 05 ?? ?? ?? ?? 83 E8 01 89 05 ?? ?? ?? ?? 78 ?? "
      "48 8D 0D",
      0x3417A8, 0x06, kHalo4ViewStackTopRva },
    // The window count clamp(n,1,4) inside the main_render_game homolog.
    { "player-window-count-clamp",
      "B9 01 00 00 00 3B C1 0F 4F C8 B8 04 00 00 00 3B C8 0F 4C C1",
      0x1221CE, 0, 0 },
};

inline constexpr size_t kHalo4RetailAnchorCount =
    sizeof(kHalo4RetailAnchors) / sizeof(kHalo4RetailAnchors[0]);

constexpr uint32_t Halo4RetailAnchorRipTargetCount()
{
    uint32_t count = 0;
    for (const Halo4RetailAnchor& anchor : kHalo4RetailAnchors)
        if (anchor.ripDispOffset != 0)
            ++count;
    return count;
}

inline constexpr uint32_t kHalo4RetailAnchorRipTargets =
    Halo4RetailAnchorRipTargetCount();

// Everything C-H4-2's cold observation measures against the loaded image.
// Pure data so the verdict is offline-testable; the DLL side only fills it.
struct Halo4ColdObservationResult
{
    bool moduleRangeValid = false;   // base present, size == pinned image size
    bool peIdentity = false;         // machine/timestamp/SizeOfImage as pinned
    uint32_t anchorsMatchedOnce = 0; // anchors matching exactly once
    uint32_t anchorsAtPinnedRva = 0; // of those, matches at the pinned RVA
    uint32_t ripTargetsAtPinnedRva = 0; // decodes landing on ripTargetRva
    bool mappingStable = false;      // module pin still current after the scan
};

constexpr bool Halo4ColdObservationPass(const Halo4ColdObservationResult& r)
{
    return r.moduleRangeValid && r.peIdentity &&
        r.anchorsMatchedOnce == kHalo4RetailAnchorCount &&
        r.anchorsAtPinnedRva == kHalo4RetailAnchorCount &&
        r.ripTargetsAtPinnedRva == kHalo4RetailAnchorRipTargets &&
        r.mappingStable;
}

// ===========================================================================
// E-H4-6: the per-window camera transaction, retail-anchored 2026-08-07.
// Proof: docs/HALO4-SIGNATURE-EVIDENCE.md, section E-H4-6. Every RVA below was
// derived three independent ways that agree - the per-window loop's own rel32
// decodes, each function's entry signature, and the converter's copy map - and
// every pattern was measured to match EXACTLY ONCE over the pinned .text.
// ===========================================================================

// The per-window loop body inside main_render_game (fn 0x12259C-0x123115). It
// marshals all six setup arguments, calls setup, writes the first-window flag,
// then calls the inner wrapper. Anchoring the loop rather than the callees is
// what proves the ABI: its three displacements decode to the two functions we
// hook and to the element they publish.
inline constexpr uint32_t kHalo4PerWindowLoopRva = 0x122CA6;
// Byte offsets of the three displacements inside that pattern.
inline constexpr uint32_t kHalo4LoopSetupRel32Offset = 0x1E;
inline constexpr uint32_t kHalo4LoopElementRipOffset = 0x38;
inline constexpr uint32_t kHalo4LoopWrapperRel32Offset = 0x3D;

// The producer. setup(view, window, count, mode, user, observer*) writes the
// rasterizer camera, the projection, the render pair and the constant bank in
// ONE straight-line call - so per-eye state must be substituted before it, not
// after it (E-H4-5's beta-1 boundary).
inline constexpr uint32_t kHalo4SetupRva = 0x374C84;
// The render transaction: set-current, push, render, pop, clear.
// wrapper(element, view, window).
inline constexpr uint32_t kHalo4WrapperRva = 0x1222F4;
// The observer -> camera converter's copy map. Not hooked; its bytes are the
// layout proof for the observer offsets below.
inline constexpr uint32_t kHalo4ConverterCopyRva = 0x38F074;
// g_player_view_stack_element - the single camera-bearing object the wrapper
// pushes, and the destination setup writes every camera artifact into.
inline constexpr uint32_t kHalo4StackElementRva = 0x10DAFE0;
// The active c_player_view* the wrapper sets and clears.
inline constexpr uint32_t kHalo4ActiveViewRva = 0x4969AA0;

// s_observer_result layout, proven by H4EK symbols/source strings and the
// retail converter copy map at 0x38F074:
//   [rdx+0x00] -> element+0x00 (position)   [rdx+0x28] -> +0x0C (forward)
//   [rdx+0x34] -> element+0x18 (up)         [rdx+0x78] -> +0x28 (vertical FOV)
//   [rdx+0x7C] -> element+0x2C (FOV ratio)
// The snapshot covers every converter input, but C-H4-7 substitutes only the
// three pose vectors; every FOV/focus/aspect byte remains stock.
inline constexpr uint32_t kHalo4ObserverPositionOffset = 0x00;
inline constexpr uint32_t kHalo4ObserverForwardOffset = 0x28;
inline constexpr uint32_t kHalo4ObserverUpOffset = 0x34;
inline constexpr uint32_t kHalo4ObserverVerticalFovOffset = 0x78;
inline constexpr uint32_t kHalo4ObserverFovRatioOffset = 0x7C;
// Saved and restored around the whole stereo transaction. 0x80 covers every
// field the converter reads plus the +0x44..+0x5C block setup copies onto the
// view element right after it (0x374D65-0x374D7A).
inline constexpr uint32_t kHalo4ObserverSnapshotBytes = 0x80;

// setup writes the converted camera vectors directly into the stack element,
// then builds its raster projection at element+0x88. H4EK proves the final
// row-vector 4x4 begins at projection+0x78, hence element+0x100. The camera
// transaction reads these engine-held outputs after each stock setup call; it
// never guesses a projection from the observer's FOV-ratio field.
inline constexpr uint32_t kHalo4ElementPositionOffset = 0x00;
inline constexpr uint32_t kHalo4ElementForwardOffset = 0x0C;
inline constexpr uint32_t kHalo4ElementUpOffset = 0x18;
// Where the converter lands the two FOV fields (0x38F13E/0x38F143). Reading
// them back beside the observer values we wrote measures retail's converter
// scale K = element[+0x28] / observer[+0x78] on the running engine, so the
// solve never depends on a constant read out of the image.
inline constexpr uint32_t kHalo4ElementVerticalFovOffset = 0x28;
inline constexpr uint32_t kHalo4ElementFovRatioOffset = 0x2C;
inline constexpr uint32_t kHalo4RasterProjectionOffset = 0x88;
inline constexpr uint32_t kHalo4ProjectionMatrixOffset = 0x78;
inline constexpr uint32_t kHalo4ElementProjectionMatrixOffset =
    kHalo4RasterProjectionOffset + kHalo4ProjectionMatrixOffset;

// c_player_view fields setup writes (0x374E7A-0x374E99), which let the wrapper
// detour recover setup's own arguments without re-deriving the TLS chain.
inline constexpr uint32_t kHalo4ViewWindowIndexOffset = 0x38C;
inline constexpr uint32_t kHalo4ViewWindowCountOffset = 0x390;
inline constexpr uint32_t kHalo4ViewModeOffset = 0x394;
inline constexpr uint32_t kHalo4ViewOutputUserOffset = 0x39C;
inline constexpr uint32_t kHalo4ViewFirstWindowFlagOffset = 0x389;

// ===========================================================================
// E-H4-15/E-H4-16: the first-person weapons and orientations globals.
//
// Kit-explained (H4EK halo4_tag_test.exe: the "fp weapons"/"fp orientations"
// named allocations at 0x931A90 and the bounds-checked accessor at 0x928290)
// and retail-verified (halo4.dll 0x3C647C allocates the same 0x17D20/0xF000
// for the same 4 users; 0x3B5360 indexes them). Full derivation in
// docs/HALO4-SIGNATURE-EVIDENCE.md.
//
// This is also the block the E-H4-11 level-re-entry crash dereferences while
// it is NULL, so every constant here is consumed behind a proof, never
// assumed to be reachable.
// ===========================================================================

// The engine's TLS slot index lives in this global; the per-thread block is
// gs:[0x58][index]. Two independent retail derivations agree on the RVA (the
// allocator at 0x3C64D9 and the accessor at 0x3B536A), and the anchor below
// re-proves it from a signature rather than shipping the bare address.
inline constexpr uint32_t kHalo4EngineTlsIndexRva = 0x1057218;
// Offsets of the two block pointers inside that per-thread block.
inline constexpr uint32_t kHalo4TlsFirstPersonWeaponsOffset = 0x6A0;
inline constexpr uint32_t kHalo4TlsFirstPersonOrientationsOffset = 0x678;
// fp weapons: 0x17D20 total / 4 users.
inline constexpr uint32_t kHalo4FirstPersonWeaponsUserStride = 0x5F48;
// Two weapon sub-records per user (k_first_person_max_weapons = 2, proven by
// the kit accessor's own `cmp ebx,1 / jbe` bound assert).
inline constexpr uint32_t kHalo4FirstPersonWeaponStride = 0x2EC8;
inline constexpr uint32_t kHalo4FirstPersonMaxWeapons = 2;
inline constexpr uint32_t kHalo4FirstPersonMaxUsers = 4;
// Per-weapon fields, all relative to the weapon sub-record.
//
// E-H4-17, corrected by the C-H4-11 headset probe. This field was first read
// as a node INDEX; the live run proved it is the node COUNT. Retail
// 0x3B53B3-0x3B53D6 loads it, shifts it left 5 (a BYTE LENGTH, not an element
// offset) and passes it as the third argument of a CRT copy whose destination
// is orientation+0xF00 and whose source is orientation+0x00:
//
//     memcpy(record + 0xF00, record + 0x00, node_count * 0x20)
//
// The probe read 85 here and found zeros at record+0xF00+85*0x20, which is
// exactly one byte past the end of 85 copied nodes - the arithmetic that
// confirms both the meaning of this field and which bank is live.
inline constexpr uint32_t kHalo4FirstPersonWeaponNodeCountOffset = 0x15D4;
// Per-user record fields.
inline constexpr uint32_t kHalo4FirstPersonRecordFlagsOffset = 0x00;
inline constexpr uint32_t kHalo4FirstPersonRecordUnitOffset = 0x04;
// Retail gates the whole path on `shr eax,1 / test al,1`, i.e. bit 1.
inline constexpr uint32_t kHalo4FirstPersonRecordActiveFlag = 0x2;
// fp orientations: 0xF000 total = 0x1E00 x 2 weapons x 4 users, indexed
// (weapon_slot + user * 2).
inline constexpr uint32_t kHalo4FirstPersonOrientationStride = 0x1E00;
// The record is TWO equal banks of 0xF00. The LIVE nodes are the first bank;
// +0xF00 is the destination of the per-frame copy above, i.e. the previous
// frame's pose used for interpolation (the kit's
// `node_count_interpolated == node_count` assert). C-H4-11 read the copy and
// found zeros; that is what identified the pair.
inline constexpr uint32_t kHalo4FirstPersonNodeArrayOffset = 0x000;
inline constexpr uint32_t kHalo4FirstPersonPreviousNodeArrayOffset = 0xF00;
inline constexpr uint32_t kHalo4FirstPersonNodeStride = 0x20;
// 0xF00 / 0x20 = 120, which also bounds MAXIMUM_NODES_PER_FIRST_PERSON_MODEL.
inline constexpr uint32_t kHalo4FirstPersonMaxNodes =
    kHalo4FirstPersonPreviousNodeArrayOffset / kHalo4FirstPersonNodeStride;
// E-H4-21b: H4EK model_skinning.cpp builds the final 3x4 GPU palette from
// immutable 0x34-byte absolute object-node matrices.  Retail's structural
// homolog is unique at this RVA; its third (and only first-person) caller
// returns to kHalo4FirstPersonSkinningReturnRva.
inline constexpr uint32_t kHalo4ModelSkinningRva = 0x33D8B8;
inline constexpr uint32_t kHalo4FirstPersonSkinningReturnRva = 0x36F3C9;
inline constexpr char kHalo4ModelSkinningPattern[] =
    "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 "
    "B8 30 31 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8D AC 24 A0 00 00 00 "
    "48 83 E5 80";

// storm_fp.render_model, H4EK 1.890.0.0.  The body owns nodes 0..79; the
// composed first-person record observed in retail has five appended weapon
// nodes.  These indices and parent relationships are tag facts, not copied
// from another Halo title.
inline constexpr int kHalo4StormFpBodyNodeCount = 80;
inline constexpr int kHalo4StormFpComposedNodeCount = 85;
inline constexpr int kHalo4RightShoulderNode = 4;
inline constexpr int kHalo4RightElbowNode = 16;
inline constexpr int kHalo4RightHandNode = 29;
inline constexpr int kHalo4LeftShoulderNode = 5;
inline constexpr int kHalo4LeftElbowNode = 8;
inline constexpr int kHalo4LeftHandNode = 37;

// Authored v4 pole directions and controller-local attachment positions.
// Positions are metres and are intentionally independent of Blender empty
// scale (runtime_uses_scale=false in the exported evidence JSON).
inline constexpr float kHalo4RightPoleDirection[3] =
    {-0.665396988f, -0.692102790f, -0.279715300f};
inline constexpr float kHalo4LeftPoleDirection[3] =
    {-0.417066097f, 0.881134331f, -0.222840950f};
inline constexpr float kHalo4RightAttachmentMetres[3] =
    {-0.000000045f, 0.059896708f, -0.000000089f};
inline constexpr float kHalo4LeftAttachmentMetres[3] =
    {-0.000000007f, 0.059896648f, -0.000000045f};

// H4EK's visible Storm meshes are genuinely blended across both arm joints:
// the official tag has 82 upper/forearm cross-weighted vertices per side and
// 222 right / 235 left forearm/hand cross-weighted vertices.  When a tracked
// hand is outside the animated chain's natural reach, assigning the whole
// excess to the hand transform makes those authored blends look like broken
// weight painting.  Split extension between the two links in their authored
// length ratio.  The runtime then moves the complete elbow and hand subtrees
// to the two planned endpoints; no vertex weights or inverse binds are changed.
struct Halo4ArmReachPlan
{
    float stretch = 1.0f;
    float upperLength = 0.0f;
    float lowerLength = 0.0f;
    float upperExtension = 0.0f;
    float lowerExtension = 0.0f;
};

inline bool Halo4PlanArmReach(float upperLength, float lowerLength,
    float shoulderToTarget, Halo4ArmReachPlan& out) noexcept
{
    if (!std::isfinite(upperLength) || !std::isfinite(lowerLength) ||
        !std::isfinite(shoulderToTarget) || upperLength <= 1.0e-5f ||
        lowerLength <= 1.0e-5f || shoulderToTarget < 0.0f)
        return false;
    const float naturalReach = upperLength + lowerLength;
    if (!std::isfinite(naturalReach) || naturalReach <= 1.0e-4f)
        return false;
    const float requested = shoulderToTarget > naturalReach
        ? shoulderToTarget / naturalReach : 1.0f;
    // Retain the existing 1.8 safety bound. Typical tracked poses are below
    // it; the bound prevents a corrupt stage-space position from producing an
    // unbounded skin transform in the final-palette hook.
    const float stretch = requested < 1.8f ? requested : 1.8f;
    out.stretch = stretch;
    out.upperLength = upperLength * stretch;
    out.lowerLength = lowerLength * stretch;
    out.upperExtension = out.upperLength - upperLength;
    out.lowerExtension = out.lowerLength - lowerLength;
    return std::isfinite(out.upperLength) && std::isfinite(out.lowerLength) &&
        std::isfinite(out.upperExtension) &&
        std::isfinite(out.lowerExtension);
}
// A Blam skeleton's root is node 0; the assembly hangs off it, so writing it
// moves the whole gun-and-arms rig rather than one part of it.
inline constexpr uint32_t kHalo4FirstPersonRootNode = 0;

// --- C-H4-14: one counter per refusal stage ---------------------------------
//
// C-H4-13 shipped a single combined refusal bucket. Its headset log therefore
// said only "every palette was refused", which cannot separate a wrong
// admission gate from a wrong node map, and the wrong one turned out to be the
// gate. Every stage below is counted on its own so a single sitting names the
// exact predicate that refused.
enum class Halo4VrikStage : uint8_t
{
    // Nothing refused. Also returned by the classifier to mean "this record is
    // the Storm first-person body".
    Solved = 0,
    CountRefused,
    CopyFailed,
    // C-H4-14 lumped "the 3x3 is not orthonormal" together with "the
    // translation is outside an assumed range", and its headset log could
    // therefore only say `basis`. They are different faults with different
    // fixes - a wrong element layout versus a wrong assumption about which
    // space the matrices live in - so they are counted apart from here on.
    BasisFailed,
    RangeFailed,
    // E-H4-21d: the bank is world-absolute, so the model's own frame has to be
    // recovered from its root node before any authored, model-space quantity
    // means anything.
    AnchorFailed,
    LinkFailed,
    SideFailed,
    HeadPoseFailed,
    RightPoseFailed,
    LeftPoseFailed,
    RightIkFailed,
    LeftIkFailed,
    Count,
};

// E-H4-21d, measured in retail and confirmed against the H4EK tag.
//
// ARGUMENT 7 IS NOT A NODE COUNT, AND NOTHING MAY EVER GATE ON IT AGAIN.
// `halo4.dll+0x33D6F0` returns the SKINNING PALETTE SIZE - the number of
// 0x30-byte output matrices the caller must allocate, which is exactly why the
// caller sizes its buffer `count * 0x30 + 0xA8`. For a node-mapped render model
// (`tag+0x04` flags bit 2, which storm_fp has) it is
// `1 + sum over drawn regions of the selected mesh's node-map length`.
// Master Chief's live permutation set gives `1 + 60 + 28 + 4 + 3 = 96`, which
// is why the headset log showed records of 96, 5 and 33 and no record of 80.
// storm_fp's node count really is 80; 80 is simply not a reachable palette
// size for it, so C-H4-14's `[80,120]` window admitted the arms by luck and
// would have rejected them outright whenever a region was masked off.
//
// The consumer's own loop bound is the render model's node count
// (`+0x33D99C mov r10d,[rdx+r13*4+0x30]`), NOT argument 7, and the record's
// bank is a fixed 120 transforms inside a 0x1910-byte global record
// (`(0x1910 - 0xB0) / 0x34 == 120`). Copying the bank bound is therefore
// bounded by the structure itself and needs no count predicate at all.
inline constexpr int32_t kHalo4FirstPersonBankTransforms =
    static_cast<int32_t>(kHalo4FirstPersonMaxNodes);

// The 0x1910-byte record's own header. `+0x08` is written by the bank filler
// (`halo4.dll+0x3B95B8 mov [rbx+8],r8d`) from its third argument: the two
// weapon fills at `+0x3B1E15` / `+0x3B1F1D` pass 1, and the body fill at
// `+0x3B23AD` passes 0. It is a free discriminator, so it is LOGGED - the
// authored bind geometry is what actually identifies the arms.
inline constexpr uint32_t kHalo4FirstPersonRecordBankOffset = 0xB0;
inline constexpr uint32_t kHalo4FirstPersonRecordFillFlagOffset = 0x08;
inline constexpr int32_t kHalo4FirstPersonBodyFillFlag = 0;

// H4EK storm_fp.render_model bind lengths in world units: upper arms 0.0915251,
// forearms 0.116662. Animation rotates a link but cannot change its length, so
// these four distances identify the Storm arms inside a palette whose node
// meaning is otherwise unproven. The envelopes are C-H4-13's own, unchanged:
// they have never been measured against a live palette, so widening them now
// would trade one guess for another. The split counters exist to measure them.
inline constexpr float kHalo4StormUpperArmBind = 0.0915251f;
inline constexpr float kHalo4StormForearmBind = 0.116662f;

// CALIBRATED FROM THE LIVE ENGINE, NOT FROM THE TAG BIND POSE.
//
// C-H4-14 derived this window from the H4EK storm_fp bind (0.0915 upper /
// 0.1167 forearm) and admitted +-20% around it. The 2026-08-08 17:56 headset
// log measured what the engine actually holds for the body-fill record:
//
//   window A: R 0.2113 / 0.2341   L 0.2135 / 0.3144   (1942 records)
//   window B: R 0.2100 / 0.2209   L 0.2027 / 0.3192   ( 512 records)
//
// Every one of those is outside the bind-derived window, so the gate refused
// 100% of real body records - 1942 link refusals against 1942 body fills, an
// exact match - while a weapon record slipped through it. The live bank is
// simply not in the tag's units: the ratio to bind is not even constant
// (2.01x to 2.69x across the four links), so it is not a pure scale and must
// not be "corrected" by a guessed factor.
//
// What the measurements DO establish, and what this gate now uses:
//   - the two UPPER arms agree to within 3.5% across every sample, which is
//     the mirror symmetry only a real pair of arms produces;
//   - the FOREARM distance is pose-dependent (0.22 to 0.32) because nodes
//     16->29 and 8->37 are not direct parent-child links, so it can only ever
//     be a loose sanity bound.
//
// The absolute range deliberately spans BOTH regimes - the tag bind and the
// measured live geometry - because the record's identity is now established
// by the engine's own fill flag before this is ever called (see the body-fill
// gate in Halo4ModelSkinningDetour). This is a sanity check on the geometry,
// not the thing that decides which record the arms are.
inline constexpr bool Halo4StormLinkLengthsMatch(
    float rightUpper, float rightLower,
    float leftUpper, float leftLower) noexcept
{
    // NaN fails every comparison, so no separate finite test is needed.
    const bool inRange =
        rightUpper > 0.050f && rightUpper < 0.350f &&
        rightLower > 0.050f && rightLower < 0.500f &&
        leftUpper > 0.050f && leftUpper < 0.350f &&
        leftLower > 0.050f && leftLower < 0.500f;
    if (!inRange) return false;
    // Mirror symmetry on the upper arm: pose-invariant (it is a true
    // parent-child bone length), measured at <=3.5% on real Storm arms, and
    // the discriminator a weapon record cannot fake.
    const float larger =
        leftUpper > rightUpper ? leftUpper : rightUpper;
    const float difference =
        leftUpper > rightUpper ? leftUpper - rightUpper
                               : rightUpper - leftUpper;
    return difference <= larger * 0.25f;
}

// Blam's second basis axis is "left", so the right shoulder sits at the lower
// value. This separates the Storm arms from a mirrored or transposed node map
// that happens to carry the same four link lengths.
//
// This is only true in the MODEL's own frame. The live bank is world-absolute,
// where the shoulder separation is the model's left axis rotated by the
// player's heading, so its second component changes sign as the player turns -
// the predicate would pass for about half of all facings and fail for the
// rest. Call it only after lifting the nodes into model space.
inline constexpr bool Halo4StormSideOrderMatches(
    float rightShoulderLeftAxis, float leftShoulderLeftAxis) noexcept
{
    return rightShoulderLeftAxis < leftShoulderLeftAxis;
}

static_assert(kHalo4FirstPersonOrientationStride *
                  kHalo4FirstPersonMaxWeapons * kHalo4FirstPersonMaxUsers ==
              0xF000,
    "orientation dimensions must reproduce the kit's 0xF000 allocation");
static_assert(kHalo4FirstPersonWeaponStride * kHalo4FirstPersonMaxWeapons <
                  kHalo4FirstPersonWeaponsUserStride,
    "two weapon sub-records must fit inside one per-user record");
static_assert(kHalo4FirstPersonMaxNodes == 120,
    "the node bank is 120 transforms of 0x20 bytes");

inline constexpr size_t kHalo4CameraAnchorLoop = 0;
inline constexpr size_t kHalo4CameraAnchorSetup = 1;
inline constexpr size_t kHalo4CameraAnchorWrapper = 2;
inline constexpr size_t kHalo4CameraAnchorConverter = 3;
inline constexpr size_t kHalo4CameraAnchorFirstPerson = 4;

// Reuses Halo4RetailAnchor so the cold observation's proven matcher validates
// this table with no new scanning code.
inline constexpr Halo4RetailAnchor kHalo4CameraAnchors[] = {
    // The loop body. Its lea displacement decodes to the stack element; the two
    // rel32s are checked separately by Halo4CameraLoopTargetsAgree below,
    // because a Halo4RetailAnchor carries only one displacement.
    { "per-window-camera-loop",
      "48 8B 47 08 48 89 44 24 28 8B 07 89 44 24 20 44 8B 4C 24 50 "
      "45 8B C7 8B 57 10 49 8B CD E8 ?? ?? ?? ?? 85 F6 0F 94 C0 "
      "41 88 85 89 03 00 00 44 8B 47 10 49 8B D5 48 8D 0D ?? ?? ?? ?? "
      "E8 ?? ?? ?? ??",
      kHalo4PerWindowLoopRva, kHalo4LoopElementRipOffset,
      kHalo4StackElementRva },
    // Setup's entry. Its lea r13 displacement decodes to the same stack
    // element the loop's lea does - an independent second derivation.
    { "player-view-setup-entry",
      "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
      "48 8B D9 0F 29 74 24 40 4C 8D 2D ?? ?? ?? ?? 49 63 E8",
      kHalo4SetupRva, 0x1F, kHalo4StackElementRva },
    // The wrapper's entry. Its displacement decodes to the active-view global.
    { "player-view-wrapper-entry",
      "48 89 5C 24 08 48 89 7C 24 10 41 56 48 83 EC 20 48 8B FA "
      "48 89 15 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 41 8B D8 E8",
      kHalo4WrapperRva, 0x16, kHalo4ActiveViewRva },
    // The converter's copy map: the literal instructions that read the observer
    // offsets this file pins. If the layout ever moves, this stops matching.
    { "observer-camera-copy-map",
      "F2 0F 10 42 28 F2 0F 11 43 0C 8B 42 30 89 43 14 "
      "F2 0F 10 42 34 F2 0F 11 43 18 8B 42 3C 89 43 20 "
      "F3 0F 10 42 78 F3 0F 11 43 28 F3 0F 10 72 7C F3 0F 11 73 2C",
      kHalo4ConverterCopyRva, 0, 0 },
    // E-H4-16's first-person node accessor. It is anchored ONLY to prove the
    // engine TLS index global: its rip displacement must decode to
    // kHalo4EngineTlsIndexRva, which is the one address the hands path cannot
    // reach the blocks without. Nothing here is hooked. The pattern runs to the
    // `imul rbx, r8, 0x5F48` so the fp-weapons per-user stride this file pins
    // is part of the matched bytes rather than a separate belief, and the whole
    // 48-byte string was measured to match EXACTLY ONCE over the pinned image.
    { "first-person-node-accessor",
      "48 89 5C 24 08 57 48 83 EC 40 44 8B 05 ?? ?? ?? ?? "
      "65 48 8B 04 25 58 00 00 00 0F 29 74 24 30 0F 28 F2 "
      "4E 8B 0C C0 4C 63 C1 49 69 D8 48 5F 00 00",
      0x3B5360, 0x0D, kHalo4EngineTlsIndexRva },
};

inline constexpr size_t kHalo4CameraAnchorCount =
    sizeof(kHalo4CameraAnchors) / sizeof(kHalo4CameraAnchors[0]);

constexpr uint32_t Halo4CameraAnchorRipTargetCount()
{
    uint32_t count = 0;
    for (const Halo4RetailAnchor& anchor : kHalo4CameraAnchors)
        if (anchor.ripDispOffset != 0)
            ++count;
    return count;
}

inline constexpr uint32_t kHalo4CameraAnchorRipTargets =
    Halo4CameraAnchorRipTargetCount();

// The loop's own two rel32 call targets must be the functions we are about to
// hook. This is the edge that makes the hook a proven caller relationship
// rather than two addresses that merely matched a pattern.
constexpr bool Halo4CameraLoopTargetsAgree(
    uint32_t setupTargetRva, uint32_t wrapperTargetRva)
{
    return setupTargetRva == kHalo4SetupRva &&
        wrapperTargetRva == kHalo4WrapperRva;
}

// Everything the camera core proves before it creates a single hook. Pure data
// so core_tests can prove each field fails closed on its own.
struct Halo4CameraInstallProof
{
    bool coldObservationPassed = false; // C-H4-2's identity+anchor preflight
    uint32_t anchorsMatchedOnce = 0;
    uint32_t anchorsAtPinnedRva = 0;
    uint32_t ripTargetsAtPinnedRva = 0;
    bool loopCallTargetsAgree = false;  // the loop calls setup and the wrapper
    bool executableRange = false;       // both hook sites inside .text
    bool mappingStable = false;
};

constexpr bool Halo4CameraInstallComplete(const Halo4CameraInstallProof& p)
{
    return p.coldObservationPassed &&
        p.anchorsMatchedOnce == kHalo4CameraAnchorCount &&
        p.anchorsAtPinnedRva == kHalo4CameraAnchorCount &&
        p.ripTargetsAtPinnedRva == kHalo4CameraAnchorRipTargets &&
        p.loopCallTargetsAgree && p.executableRange && p.mappingStable;
}

// ---------------------------------------------------------------------------
// Per-eye camera math. Allocation-free, engine-free and exhaustively testable:
// the hot detour does nothing here that core_tests cannot reproduce offline.
// ---------------------------------------------------------------------------

struct Halo4CameraBasis
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
    float verticalFov = 0.0f;
    float fovRatio = 0.0f;
};

// Rejects anything the engine could not have produced, so a torn or
// mid-transition observer read can never become a rendered eye.
inline bool Halo4ValidateCameraBasis(const Halo4CameraBasis& basis) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(basis.position[axis]) ||
            !std::isfinite(basis.forward[axis]) ||
            !std::isfinite(basis.up[axis]))
        {
            return false;
        }
    }
    const float forwardLengthSquared =
        basis.forward[0] * basis.forward[0] +
        basis.forward[1] * basis.forward[1] +
        basis.forward[2] * basis.forward[2];
    const float upLengthSquared =
        basis.up[0] * basis.up[0] + basis.up[1] * basis.up[1] +
        basis.up[2] * basis.up[2];
    const float forwardUpDot =
        basis.forward[0] * basis.up[0] +
        basis.forward[1] * basis.up[1] +
        basis.forward[2] * basis.up[2];
    // Eye displacement uses forward x up as the right axis, so admitting a
    // merely finite but skewed basis would visibly distort IPD. Halo's camera
    // producer supplies an orthonormal basis; leave a small float-drift band
    // while refusing geometry that no longer has that shape.
    return std::fabs(forwardLengthSquared - 1.0f) < 0.05f &&
        std::fabs(upLengthSquared - 1.0f) < 0.05f &&
        std::fabs(forwardUpDot) < 0.05f &&
        std::isfinite(basis.verticalFov) && basis.verticalFov > 1.0e-4f &&
        basis.verticalFov < 3.14149284f && std::isfinite(basis.fovRatio) &&
        basis.fovRatio > 0.0f;
}

constexpr bool Halo4PreparedPairMatches(
    uint64_t expectedSerial, uint64_t leftSerial, uint64_t rightSerial) noexcept
{
    return expectedSerial != 0 && leftSerial == expectedSerial &&
        rightSerial == expectedSerial;
}

constexpr bool Halo4EyeCaptureIsCurrent(
    int requestedEye, int activeRasterEye, bool redirected,
    bool cacheAvailable) noexcept
{
    return requestedEye >= 0 && requestedEye < 2 &&
        activeRasterEye == requestedEye && redirected && cacheAvailable;
}

constexpr bool Halo4XrPairUploadComplete(
    bool acquired, bool waited, bool bothEyesUploaded,
    bool released) noexcept
{
    return acquired && waited && bothEyesUploaded && released;
}

constexpr bool Halo4XrPairSubmissionAccepted(
    bool projectionQueued, bool exactEndFrameSuccess) noexcept
{
    return projectionQueued && exactEndFrameSuccess;
}

// Generic OpenXR cover math retained for later projection work. These tangents
// are NOT the values stored at observer +0x78/+0x7C; those fields are a full
// vertical FOV in radians and an engine-defined FOV ratio. C-H4-7 deliberately
// leaves both observer fields byte-identical and reads the projection Halo 4
// actually built instead. Angles here are left/right/up/down.
inline bool Halo4SymmetricCoverFromFov(
    const float fov[4], float& tangentX, float& tangentY) noexcept
{
    if (!fov)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(fov[i]))
            return false;
    const float halfHorizontal = fov[1] > -fov[0] ? fov[1] : -fov[0];
    const float halfVertical = fov[2] > -fov[3] ? fov[2] : -fov[3];
    constexpr float kMaximumHalfAngle = 1.5533f; // ~89 degrees
    if (halfHorizontal <= 0.0f || halfHorizontal >= kMaximumHalfAngle ||
        halfVertical <= 0.0f || halfVertical >= kMaximumHalfAngle)
    {
        return false;
    }
    tangentX = std::tan(halfHorizontal);
    tangentY = std::tan(halfVertical);
    return std::isfinite(tangentX) && std::isfinite(tangentY) &&
        tangentX > 0.0f && tangentY > 0.0f;
}

inline void Halo4RotateAboutAxis(
    float vector[3], const float axis[3], float cosAngle,
    float sinAngle) noexcept
{
    const float dot =
        axis[0] * vector[0] + axis[1] * vector[1] + axis[2] * vector[2];
    const float cross[3] = {
        axis[1] * vector[2] - axis[2] * vector[1],
        axis[2] * vector[0] - axis[0] * vector[2],
        axis[0] * vector[1] - axis[1] * vector[0]};
    for (int i = 0; i < 3; ++i)
    {
        vector[i] = vector[i] * cosAngle + cross[i] * sinAngle +
            axis[i] * dot * (1.0f - cosAngle);
    }
}

inline bool Halo4NormalizeQuaternion(
    const float input[4], float output[4]) noexcept
{
    if (!input || !output)
        return false;
    float lengthSquared = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (!std::isfinite(input[i]))
            return false;
        lengthSquared += input[i] * input[i];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f)
        return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    for (int i = 0; i < 4; ++i)
        output[i] = input[i] * inverseLength;
    return true;
}

inline bool Halo4RotateCameraByLocalOrientation(
    Halo4CameraBasis& camera, const float orientation[4]) noexcept
{
    float q[4];
    if (!Halo4ValidateCameraBasis(camera) ||
        !Halo4NormalizeQuaternion(orientation, q))
    {
        return false;
    }
    const float sinHalf = std::sqrt(
        q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    if (sinHalf <= 1.0e-5f)
        return true;

    float angle = 2.0f * std::atan2(sinHalf, q[3]);
    if (angle > 3.14159265f)
        angle -= 6.2831853f;
    const float right[3] = {
        camera.forward[1] * camera.up[2] -
            camera.forward[2] * camera.up[1],
        camera.forward[2] * camera.up[0] -
            camera.forward[0] * camera.up[2],
        camera.forward[0] * camera.up[1] -
            camera.forward[1] * camera.up[0]};
    const float worldAxis[3] = {
        (q[0] / sinHalf) * right[0] + (q[1] / sinHalf) * camera.up[0] -
            (q[2] / sinHalf) * camera.forward[0],
        (q[0] / sinHalf) * right[1] + (q[1] / sinHalf) * camera.up[1] -
            (q[2] / sinHalf) * camera.forward[1],
        (q[0] / sinHalf) * right[2] + (q[1] / sinHalf) * camera.up[2] -
            (q[2] / sinHalf) * camera.forward[2]};
    const float cosAngle = std::cos(angle);
    const float sinAngle = std::sin(angle);
    Halo4RotateAboutAxis(camera.forward, worldAxis, cosAngle, sinAngle);
    Halo4RotateAboutAxis(camera.up, worldAxis, cosAngle, sinAngle);
    return Halo4ValidateCameraBasis(camera);
}

// Displaces and cants the mono camera into one eye. eyePosition/eyeOrientation
// are this eye's offset from the stereo midpoint in OpenXR view axes
// (+X right, +Y up, -Z forward); worldScale converts meters to world units.
// The cant is applied because a headset whose panels are angled outward reports
// its FOV around that canted axis - rendering both eyes straight ahead leaves
// the outer lens edge uncovered.
inline bool Halo4BuildEyeCamera(
    const Halo4CameraBasis& mono, const float eyePosition[3],
    const float* eyeOrientation, float worldScale,
    Halo4CameraBasis& out) noexcept
{
    if (!Halo4ValidateCameraBasis(mono) || !eyePosition ||
        !std::isfinite(worldScale) || worldScale <= 0.0f)
    {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(eyePosition[axis]))
            return false;

    const float right[3] = {
        mono.forward[1] * mono.up[2] - mono.forward[2] * mono.up[1],
        mono.forward[2] * mono.up[0] - mono.forward[0] * mono.up[2],
        mono.forward[0] * mono.up[1] - mono.forward[1] * mono.up[0]};

    out = mono;
    for (int axis = 0; axis < 3; ++axis)
    {
        out.position[axis] = mono.position[axis] +
            (right[axis] * eyePosition[0] + mono.up[axis] * eyePosition[1] -
             mono.forward[axis] * eyePosition[2]) * worldScale;
    }

    return eyeOrientation
        ? Halo4RotateCameraByLocalOrientation(out, eyeOrientation)
        : Halo4ValidateCameraBasis(out);
}

inline bool Halo4CameraOutputMatches(
    const Halo4CameraBasis& requested, const float position[3],
    const float forward[3], const float up[3]) noexcept
{
    if (!Halo4ValidateCameraBasis(requested) || !position || !forward || !up)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(position[axis]) || !std::isfinite(forward[axis]) ||
            !std::isfinite(up[axis]))
        {
            return false;
        }
    }
    // Retail converter instructions copy these nine floats directly. Exact
    // bytes are therefore the proof that setup consumed our observer write;
    // a tolerance could mislabel a small ignored transform as TAKING.
    return std::memcmp(
               position, requested.position, sizeof(requested.position)) == 0 &&
        std::memcmp(
               forward, requested.forward, sizeof(requested.forward)) == 0 &&
        std::memcmp(up, requested.up, sizeof(requested.up)) == 0;
}

// Decode only the proven normal H4 row-vector projection. The retail setup
// passes an exact zero center to this builder and produces positive X/Y scales.
// H4 also has a custom-window path whose p[8]/p[9] center terms cannot be
// represented by the compositor's current symmetric-half-FOV API. Reject that
// distinct path instead of publishing a geometrically false projection.
inline bool Halo4DecodeSymmetricProjectionHalfFovs(
    const float matrix[16], float& halfX, float& halfY,
    float& centerX, float& centerY) noexcept
{
    if (!matrix)
        return false;
    const float scaleX = matrix[0];
    const float scaleY = matrix[5];
    centerX = matrix[8];
    centerY = matrix[9];
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) ||
        !std::isfinite(centerX) || !std::isfinite(centerY) ||
        !std::isfinite(matrix[11]) || scaleX <= 0.0f || scaleY <= 0.0f ||
        matrix[11] != -1.0f ||
        centerX != 0.0f || centerY != 0.0f)
    {
        return false;
    }
    halfX = std::atan(1.0f / scaleX);
    halfY = std::atan(1.0f / scaleY);
    return std::isfinite(halfX) && std::isfinite(halfY) && halfX > 0.0f &&
        halfX < 1.5707f && halfY > 0.0f && halfY < 1.5707f;
}

// ===========================================================================
// C-H4-8: head pose, 6DOF, and native headset-FOV coverage.
//
// Everything below is pure math over plain floats: no engine, no Windows, no
// allocation. core_tests exercises each function directly, so the hot detour
// contains nothing that cannot be reproduced offline.
// ===========================================================================

inline float Halo4WrapPi(float angle) noexcept
{
    while (angle > 3.14159265f)
        angle -= 6.28318531f;
    while (angle < -3.14159265f)
        angle += 6.28318531f;
    return angle;
}

// --- Native headset FOV coverage -------------------------------------------
//
// The compositor crops the submitted image to the headset's own per-eye
// frustum (vr.cpp's native-FOV path), but it can only do that when the raster
// the game produced CONTAINS that frustum. Halo 4's stock cover is narrower
// than several headsets - measured on PSVR2, stock 50.46/41.14 deg against a
// native 61.5/53.0 deg - so the containment test fails and the whole slice is
// submitted at the cover FOV instead, which is geometrically wrong.
//
// Nothing here is headset-specific: the required cover is derived from
// whatever XrFovf the runtime reports for this eye, exactly as Halo 3 does at
// game.cpp's immersiveTangentX/Y.

// The symmetric cover that contains one asymmetric native frustum.
// fov is left/right/up/down in radians, OpenXR sign convention.
inline bool Halo4RequiredCoverTangents(
    const float fov[4], float& tangentX, float& tangentY) noexcept
{
    if (!fov)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(fov[i]))
            return false;
    if (!(fov[0] < 0.0f && fov[1] > 0.0f && fov[2] > 0.0f && fov[3] < 0.0f))
        return false;
    const float halfX = -fov[0] > fov[1] ? -fov[0] : fov[1];
    const float halfY = fov[2] > -fov[3] ? fov[2] : -fov[3];
    constexpr float kMaximumHalfAngle = 1.5533f; // ~89 degrees
    if (halfX <= 0.0f || halfX >= kMaximumHalfAngle || halfY <= 0.0f ||
        halfY >= kMaximumHalfAngle)
    {
        return false;
    }
    tangentX = std::tan(halfX);
    tangentY = std::tan(halfY);
    return std::isfinite(tangentX) && std::isfinite(tangentY) &&
        tangentX > 0.0f && tangentY > 0.0f;
}

// Does a symmetric cover contain the native frustum on all four edges? This is
// the same test vr.cpp applies before it crops, reproduced here so the camera
// core can refuse to publish a cover it already knows will be rejected.
inline bool Halo4CoverContainsFov(
    float coverHalfX, float coverHalfY, const float fov[4]) noexcept
{
    float requiredX = 0.0f;
    float requiredY = 0.0f;
    if (!Halo4RequiredCoverTangents(fov, requiredX, requiredY))
        return false;
    if (!std::isfinite(coverHalfX) || !std::isfinite(coverHalfY) ||
        coverHalfX <= 0.0f || coverHalfY <= 0.0f || coverHalfX >= 1.5707f ||
        coverHalfY >= 1.5707f)
    {
        return false;
    }
    return std::tan(coverHalfX) >= requiredX && std::tan(coverHalfY) >= requiredY;
}

// What the camera core has learned about how Halo 4 answers an observer FOV
// write. Both quantities are MEASURED from the engine's own finished
// projection rather than assumed:
//
//   gain  = builtHalfY / writtenVerticalFov
//           Retail converter 0x38F094-0x38F0AC copies observer +0x78 to
//           element +0x28 multiplied by a scale, and the projection builder
//           treats element +0x28 as a FULL vertical FOV, so
//           builtHalfY = observer[+0x78] * scale / 2.
//           The scale is the literal float 0.785 (halo4.dll RVA 0xD9560C),
//           confirmed live to five figures on two independent values:
//           1.8295 * 0.785 = 1.43616 against a logged element 1.4361, and
//           1.5385 * 0.785 = 1.20772 against 1.2077. That makes the expected
//           gain 0.785/2 = 0.3925, which the C-H4-7 readback corroborates
//           (0.71805 / 1.8295 = 0.39249).
//
//           It is LEARNED rather than hardcoded because 0x38F01A-0x38F05E
//           selects that scale through a branch: a global at RVA 0x4969640
//           (converted degrees->radians, fallback 78.000 deg) compared against
//           zero picks either 0.785 or 0.168214291. If that branch ever flips,
//           a hardcoded constant would silently distort the world by 4.7x,
//           while a learned gain corrects itself on the next frame.
//   ratio = tan(builtHalfX) / tan(builtHalfY)
//           Halo 4 derives the horizontal extent itself, so the vertical write
//           is the only handle we have on both axes.
struct Halo4FovCalibration
{
    // 0.785/2, the proven retail mapping. Never actually consumed: solving
    // requires `learned`, which only a real readback can set. It exists so the
    // struct has a meaningful value rather than a misleading zero.
    float gain = 0.3925f;
    float ratio = 0.0f;  // 0 until the first projection has been read back
    bool learned = false;
};

// Fold one measured projection into the calibration.
inline bool Halo4LearnFovCalibration(
    float writtenVerticalFov, float builtHalfX, float builtHalfY,
    Halo4FovCalibration& calibration) noexcept
{
    if (!std::isfinite(writtenVerticalFov) || !std::isfinite(builtHalfX) ||
        !std::isfinite(builtHalfY) || writtenVerticalFov <= 1.0e-4f ||
        builtHalfX <= 1.0e-4f || builtHalfY <= 1.0e-4f ||
        builtHalfX >= 1.5707f || builtHalfY >= 1.5707f)
    {
        return false;
    }
    const float gain = builtHalfY / writtenVerticalFov;
    const float tangentX = std::tan(builtHalfX);
    const float tangentY = std::tan(builtHalfY);
    if (!std::isfinite(gain) || gain <= 1.0e-3f || gain >= 10.0f ||
        !std::isfinite(tangentX) || !std::isfinite(tangentY) ||
        tangentY <= 1.0e-4f)
    {
        return false;
    }
    const float ratio = tangentX / tangentY;
    if (!std::isfinite(ratio) || ratio <= 1.0e-3f || ratio >= 100.0f)
        return false;
    calibration.gain = gain;
    calibration.ratio = ratio;
    calibration.learned = true;
    return true;
}

// Solve the observer +0x78 write that makes the built cover contain this eye's
// native frustum. Because Halo 4 owns the horizontal extent, the vertical write
// must also absorb any horizontal shortfall - hence the max() against
// requiredTangentX / ratio. The margin keeps float rounding from leaving the
// cover a hair short, which would trip vr.cpp's containment fallback.
inline bool Halo4SolveCoverVerticalFov(
    const float fov[4], const Halo4FovCalibration& calibration, float margin,
    float& verticalFovWrite, float& expectedHalfY) noexcept
{
    float requiredX = 0.0f;
    float requiredY = 0.0f;
    if (!Halo4RequiredCoverTangents(fov, requiredX, requiredY))
        return false;
    if (!calibration.learned || !std::isfinite(calibration.gain) ||
        !std::isfinite(calibration.ratio) || calibration.gain <= 1.0e-3f ||
        calibration.ratio <= 1.0e-3f || !std::isfinite(margin) ||
        margin < 1.0f || margin > 1.5f)
    {
        return false;
    }
    float targetTangentY = requiredY;
    const float tangentYForX = requiredX / calibration.ratio;
    if (tangentYForX > targetTangentY)
        targetTangentY = tangentYForX;
    targetTangentY *= margin;
    if (!std::isfinite(targetTangentY) || targetTangentY <= 0.0f)
        return false;
    expectedHalfY = std::atan(targetTangentY);
    if (!std::isfinite(expectedHalfY) || expectedHalfY <= 0.0f ||
        expectedHalfY >= 1.5533f)
    {
        return false;
    }
    verticalFovWrite = expectedHalfY / calibration.gain;
    // Refuse a write the engine could not plausibly have produced. A full
    // vertical FOV at or past 180 degrees is not a camera.
    return std::isfinite(verticalFovWrite) && verticalFovWrite > 1.0e-3f &&
        verticalFovWrite < 3.14159265f;
}

// --- Head pose and 6DOF -----------------------------------------------------
//
// Halo 4 keeps its own look direction here, unlike Halo 3's ApplyHeadLook which
// REPLACES forward/up outright. Halo 3 can do that because it also owns the
// turn stick (ApplyVrTurn feeds g_gameYawRef); Halo 4's turn/look ownership is
// a separate later milestone, so replacing the basis would leave the player
// unable to turn at all. Applying the headset as a DELTA on top of the engine's
// camera keeps the accepted C-H4-1 gamepad behaviour working and still gives
// real head tracking: the world stays fixed while you look around it.
//
// The result is the same player experience by AGENTS.md's definition of parity,
// reached a different way, which that document explicitly permits.

// The room-space head quantities Halo 3's ApplyHeadLook derives, extracted so
// both the yaw/pitch/roll delta and the 6DOF decomposition can share them.
struct Halo4HeadOrientation
{
    float forward[3]{};  // room-space head forward (OpenXR axes)
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

// Identical decomposition to Halo 3's ApplyHeadLook (game.cpp), including the
// horizon-referenced roll that keeps the world fixed when you tilt your head.
inline bool Halo4DecodeHeadOrientation(
    const float quaternion[4], Halo4HeadOrientation& out) noexcept
{
    float q[4];
    if (!Halo4NormalizeQuaternion(quaternion, q))
        return false;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float hfx = -2.0f * (w * y + x * z);
    const float hfy = 2.0f * (w * x - y * z);
    const float hfz = -(1.0f - 2.0f * (x * x + y * y));
    out.forward[0] = hfx;
    out.forward[1] = hfy;
    out.forward[2] = hfz;
    out.yaw = std::atan2(hfx, -hfz);
    const float clampedPitch = hfy < -1.0f ? -1.0f : (hfy > 1.0f ? 1.0f : hfy);
    out.pitch = std::asin(clampedPitch);

    const float hux = 2.0f * (x * y - w * z);
    const float huy = 1.0f - 2.0f * (x * x + z * z);
    const float huz = 2.0f * (y * z + w * x);
    float hrx = -hfz;
    float hrz = hfx;
    float hrLength = std::sqrt(hrx * hrx + hrz * hrz);
    if (hrLength < 1.0e-4f)
        hrLength = 1.0e-4f;
    hrx /= hrLength;
    hrz /= hrLength;
    const float hnux = -hfy * hrz;
    const float hnuy = hrLength;
    const float hnuz = hfy * hrx;
    out.roll = std::atan2(
        hux * hrx + huz * hrz, hux * hnux + huy * hnuy + huz * hnuz);
    return std::isfinite(out.yaw) && std::isfinite(out.pitch) &&
        std::isfinite(out.roll);
}

// Everything the head transform consumes, so the detour passes state instead of
// reading globals inside the math and core_tests can drive it directly.
struct Halo4HeadPoseInput
{
    float quaternion[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float position[3]{};        // room space, meters
    float headYawReference = 0.0f;
    float headPositionReference[3]{};
    float yawSign = -1.0f;
    float pitchSign = 1.0f;
    float pitchTrim = 0.0f;     // radians
    float worldScale = 0.33f;   // game units per meter
    bool positional = true;     // 6DOF on
    // C-H4-9. False reproduces C-H4-8 exactly: the head is a delta ON TOP of
    // whatever pitch and roll the engine's camera already carries. True keeps
    // only the engine's HEADING and gives the headset pitch and roll outright,
    // which is what every other title does.
    bool headOwnsPitch = false;
    // C-H4-10. False composes yaw around the engine's LIVE heading, which is
    // right while the engine's own stick still turns the player. True composes
    // it around `gameYawReference` instead, which is required the moment a
    // closed aim loop is steering that heading toward the same reference -
    // otherwise the head's yaw is applied twice. Halo 3 has only ever done the
    // latter (g_gameYawRef).
    bool headOwnsYaw = false;
    float gameYawReference = 0.0f;
};

// Halo 3's ApplyHeadLook composition (game.cpp, the accepted first-person
// camera), reused here term for term so the two titles build the same basis
// from the same head. Halo 4's basis was independently PROVEN to be the same
// shape - right-handed, Z-up, yaw = atan2(fwd.y, fwd.x) - by the retail
// projection builder at 0x38F658 and the live C-H4-6 camera dump.
//
//   forward = (cos p cos y, cos p sin y, sin p)
//   up      = horizon-level up * cos(roll) + right * sin(roll),
//             where right = (sin y, -cos y, 0)
//
// Orthonormal by construction, so it can never fail Halo4ValidateCameraBasis
// the way C-H4-6's partial rewrite did.
inline void Halo4ComposeHeadOwnedBasis(
    float gameYaw, float pitch, float roll, float outForward[3],
    float outUp[3]) noexcept
{
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(gameYaw), sy = std::sin(gameYaw);
    const float cr = std::cos(roll), sr = std::sin(roll);
    outForward[0] = cp * cy;
    outForward[1] = cp * sy;
    outForward[2] = sp;
    outUp[0] = (-sp * cy) * cr + sy * sr;
    outUp[1] = (-sp * sy) * cr - cy * sr;
    outUp[2] = cp * cr;
}

inline bool Halo4ApplyHeadLean(
    Halo4CameraBasis& camera, const Halo4HeadPoseInput& input,
    const Halo4HeadOrientation& head) noexcept;

// Rotate the engine's mono camera by the headset's orientation and displace it
// by the headset's room-space movement.
//
// Basis, PROVEN for Halo 4 rather than inherited from Halo 3: the retail
// projection builder at 0x38F658 computes right = forward x up from element
// +0x0C/+0x18, normalises it, and writes (right, up, -forward) as an
// orthonormal view basis, so Halo 4 is right-handed with right = forward x up.
// The live C-H4-6 camera dump reads up(-0.000 0.000 1.000), i.e. Z-up, and its
// recentre logged -77.2 deg against fwd(0.222 -0.975 0.000), which is exactly
// atan2(fwd.y, fwd.x) - the Blam yaw convention this function assumes.
//
// Both forward AND up are rotated at every step, deliberately. C-H4-6 honoured
// Halo 3's g_writeUp (F7) toggle and left `up` at the engine's value while
// replacing `forward`; because Halo4ValidateCameraBasis rejects a basis whose
// |forward . up| reaches 0.05, that made every frame past ~2.87 degrees of head
// pitch fail validation. Halo 3 has no such validator and so never showed the
// fault. Rotating the pair together keeps the basis orthonormal by
// construction, so g_writeUp is intentionally not consulted here.
inline bool Halo4ApplyHeadPose(
    Halo4CameraBasis& camera, const Halo4HeadPoseInput& input) noexcept
{
    if (!Halo4ValidateCameraBasis(camera))
        return false;
    Halo4HeadOrientation head{};
    if (!Halo4DecodeHeadOrientation(input.quaternion, head))
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(input.position[axis]) ||
            !std::isfinite(input.headPositionReference[axis]))
        {
            return false;
        }
    }
    if (!std::isfinite(input.headYawReference) ||
        !std::isfinite(input.yawSign) || !std::isfinite(input.pitchSign) ||
        !std::isfinite(input.pitchTrim) || !std::isfinite(input.worldScale) ||
        input.worldScale <= 0.0f)
    {
        return false;
    }

    // Yaw is measured from the recenter reference so your physical facing maps
    // to the engine's own heading. Pitch and roll need no reference: a level
    // head is zero, which leaves the engine's own pitch untouched.
    const float deltaYaw =
        input.yawSign * Halo4WrapPi(head.yaw - input.headYawReference);
    const float deltaPitch = input.pitchSign * head.pitch + input.pitchTrim;
    const float deltaRoll = head.roll;
    if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch) ||
        !std::isfinite(deltaRoll))
    {
        return false;
    }

    // C-H4-9: the headset owns pitch and roll. The engine keeps its HEADING
    // and nothing else, so a level head is a level horizon no matter what the
    // engine's own camera pitch is doing.
    //
    // C-H4-8 added head pitch on top of the engine's pitch, which is correct
    // only while the engine's pitch is zero. It is not: the look stick's
    // vertical axis drives it, and so does weapon kick. Every degree of engine
    // pitch tilted the whole world away from the player's real horizon - the
    // reported "the up and down stick is breaking my orientation on my head".
    // No amount of stick suppression fixes that on its own, because engine
    // pitch that is already non-zero simply stays there with nothing to
    // return it to level.
    if (input.headOwnsPitch)
    {
        // C-H4-10: the reference heading once the aim loop owns the engine's,
        // the engine's live heading until then. Both are the same quantity -
        // which way the player's body faces - read from the only source that
        // is authoritative at that stage.
        const float engineYaw = input.headOwnsYaw
            ? input.gameYawReference
            : std::atan2(camera.forward[1], camera.forward[0]);
        if (!std::isfinite(engineYaw))
            return false;
        // Same +-1.5 rad clamp Halo 3 applies, so the basis can never
        // degenerate at the poles.
        const float pitch = deltaPitch < -1.5f
            ? -1.5f : (deltaPitch > 1.5f ? 1.5f : deltaPitch);
        Halo4ComposeHeadOwnedBasis(
            engineYaw + deltaYaw, pitch, deltaRoll, camera.forward, camera.up);
        if (!Halo4ValidateCameraBasis(camera))
            return false;
        return input.positional
            ? Halo4ApplyHeadLean(camera, input, head)
            : true;
    }

    // Yaw about world up, then pitch about the resulting right, then roll about
    // the resulting forward - the standard intrinsic head composition, applied
    // on top of whatever the engine is already looking at.
    constexpr float kWorldUp[3] = {0.0f, 0.0f, 1.0f};
    Halo4RotateAboutAxis(
        camera.forward, kWorldUp, std::cos(deltaYaw), std::sin(deltaYaw));
    Halo4RotateAboutAxis(
        camera.up, kWorldUp, std::cos(deltaYaw), std::sin(deltaYaw));

    float right[3] = {
        camera.forward[1] * camera.up[2] - camera.forward[2] * camera.up[1],
        camera.forward[2] * camera.up[0] - camera.forward[0] * camera.up[2],
        camera.forward[0] * camera.up[1] - camera.forward[1] * camera.up[0]};
    float rightLength = std::sqrt(
        right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (!std::isfinite(rightLength) || rightLength < 1.0e-4f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        right[axis] /= rightLength;
    Halo4RotateAboutAxis(
        camera.forward, right, std::cos(deltaPitch), std::sin(deltaPitch));
    Halo4RotateAboutAxis(
        camera.up, right, std::cos(deltaPitch), std::sin(deltaPitch));

    float forwardAxis[3] = {
        camera.forward[0], camera.forward[1], camera.forward[2]};
    float forwardLength = std::sqrt(
        forwardAxis[0] * forwardAxis[0] + forwardAxis[1] * forwardAxis[1] +
        forwardAxis[2] * forwardAxis[2]);
    if (!std::isfinite(forwardLength) || forwardLength < 1.0e-4f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        forwardAxis[axis] /= forwardLength;
    Halo4RotateAboutAxis(
        camera.up, forwardAxis, std::cos(deltaRoll), std::sin(deltaRoll));

    if (!Halo4ValidateCameraBasis(camera))
        return false;

    if (!input.positional)
        return true;
    return Halo4ApplyHeadLean(camera, input, head);
}

// Leaning. Decompose the room-space move in the head's own horizontal frame
// and re-apply it in the game's frame, so it stays correct as you turn.
// Same construction and the same +-1.5 world-unit clamp Halo 3 ships. Shared
// by both composition paths so 6DOF is bit-identical whoever owns pitch.
inline bool Halo4ApplyHeadLean(
    Halo4CameraBasis& camera, const Halo4HeadPoseInput& input,
    const Halo4HeadOrientation& head) noexcept
{
    const float dx = input.position[0] - input.headPositionReference[0];
    const float dy = input.position[1] - input.headPositionReference[1];
    const float dz = input.position[2] - input.headPositionReference[2];
    float horizontalLength = std::sqrt(
        head.forward[0] * head.forward[0] + head.forward[2] * head.forward[2]);
    if (horizontalLength < 1.0e-4f)
        horizontalLength = 1.0e-4f;
    const float headForwardX = head.forward[0] / horizontalLength;
    const float headForwardZ = head.forward[2] / horizontalLength;
    const float forwardComponent = dx * headForwardX + dz * headForwardZ;
    const float rightComponent = dx * (-headForwardZ) + dz * headForwardX;

    // The final camera heading after the rotation above. Halo's horizontal
    // right is (sin yaw, -cos yaw, 0), which is why the lateral term is
    // subtracted on Y - the exact mapping Halo 3's accepted 6DOF uses.
    const float gameYaw = std::atan2(camera.forward[1], camera.forward[0]);
    const float cosYaw = std::cos(gameYaw);
    const float sinYaw = std::sin(gameYaw);
    const float scale = input.worldScale;
    float offset[3] = {
        (cosYaw * forwardComponent + sinYaw * rightComponent) * scale,
        (sinYaw * forwardComponent - cosYaw * rightComponent) * scale,
        dy * scale};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(offset[axis]))
            return false;
        if (offset[axis] > 1.5f)
            offset[axis] = 1.5f;
        if (offset[axis] < -1.5f)
            offset[axis] = -1.5f;
        camera.position[axis] += offset[axis];
    }
    return Halo4ValidateCameraBasis(camera);
}

// ===========================================================================
// C-H4-11: first-person hands and weapon placement.
//
// A first-person node is 0x20 bytes (retail `shl r8, 5` at 0x3B53C7). That is
// the size of Blam's usual rotation-quaternion + translation + scale node, but
// SIZE IS NOT PROOF OF LAYOUT - so nothing is written until the engine's own
// live values have been read back and shown to satisfy that shape. This is the
// same discipline C-H4-8's FOV calibration used: learn the mapping from the
// engine, never assume it.
// ===========================================================================

struct Halo4FirstPersonNode
{
    float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
    float translation[3]{};
    float scale = 1.0f;
};

static_assert(sizeof(Halo4FirstPersonNode) == 0x20,
    "a first-person node must be exactly the engine's 0x20-byte stride");

// Does a block of engine bytes actually look like {quaternion, translation,
// scale}? Every field finite, the quaternion unit-length, the scale positive
// and sane, and the translation inside a plausible first-person envelope
// (these are camera-relative world units, so a weapon node sits within a
// metre or two of the eye, never hundreds).
inline bool Halo4FirstPersonNodeLooksValid(
    const Halo4FirstPersonNode& node) noexcept
{
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(node.rotation[i]))
            return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(node.translation[i]))
            return false;
    if (!std::isfinite(node.scale))
        return false;
    const float quaternionLengthSquared =
        node.rotation[0] * node.rotation[0] +
        node.rotation[1] * node.rotation[1] +
        node.rotation[2] * node.rotation[2] +
        node.rotation[3] * node.rotation[3];
    if (std::fabs(quaternionLengthSquared - 1.0f) > 0.05f)
        return false;
    if (node.scale <= 1.0e-3f || node.scale > 100.0f)
        return false;
    const float translationLengthSquared =
        node.translation[0] * node.translation[0] +
        node.translation[1] * node.translation[1] +
        node.translation[2] * node.translation[2];
    return std::isfinite(translationLengthSquared) &&
        translationLengthSquared < 100.0f * 100.0f;
}

// Everything the placement consumes, so the detour passes state instead of
// reading globals inside the math and core_tests can drive it directly.
struct Halo4HandPlacementInput
{
    // Controller pose relative to the headset, in OpenXR room axes
    // (+X right, +Y up, -Z forward), metres.
    float controllerOffset[3]{};
    float controllerOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float worldScale = 0.33f;    // game units per metre
    float forwardTrim = 0.0f;    // config, metres
    float verticalTrim = 0.0f;
    float lateralTrim = 0.0f;
    bool mirrored = false;       // left-handed
};

// Hamilton product, (x, y, z, w).
inline void Halo4MultiplyQuaternion(
    const float a[4], const float b[4], float out[4]) noexcept
{
    const float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    const float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

inline void Halo4RotateVectorByQuaternion(
    const float q[4], const float v[3], float out[3]) noexcept
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]);
    const float ty = 2.0f * (z * v[0] - x * v[2]);
    const float tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

// Apply one rigid transform to an ENTIRE composed first-person assembly.
//
// These are absolute per-node transforms (that is why the engine keeps a
// whole second bank to interpolate against), so transforming every node by the
// same rotation and translation moves the gun and arms as one rigid body -
// with no dependence on which node is the root or how the hierarchy is
// arranged. That independence is the point: it is what makes the placement
// work without betting on a hierarchy fact that is not proven.
inline bool Halo4TransformAssemblyNode(
    const Halo4FirstPersonNode& node, const float rotation[4],
    const float translation[3], Halo4FirstPersonNode& out) noexcept
{
    if (!Halo4FirstPersonNodeLooksValid(node))
        return false;
    float unitRotation[4];
    if (!Halo4NormalizeQuaternion(rotation, unitRotation))
        return false;
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(translation[axis]))
            return false;

    out = node;
    float rotated[3];
    Halo4RotateVectorByQuaternion(unitRotation, node.translation, rotated);
    for (int axis = 0; axis < 3; ++axis)
        out.translation[axis] = rotated[axis] + translation[axis];
    float composed[4];
    Halo4MultiplyQuaternion(unitRotation, node.rotation, composed);
    float normalised[4];
    if (!Halo4NormalizeQuaternion(composed, normalised))
        return false;
    for (int i = 0; i < 4; ++i)
        out.rotation[i] = normalised[i];
    return Halo4FirstPersonNodeLooksValid(out);
}

// Map the controller's offset-from-head into the engine's first-person node
// space and build the node the weapon root should carry.
//
// The node space is CAMERA-RELATIVE (docs/RE-notes.md records the same for
// Halo 3: "first-person bones are camera-space positions in world units"), so
// the controller's room-space offset from the head converts directly once the
// axes are mapped and the metres scaled. Blam is right-handed Z-up with
// forward = +X, left = +Y, up = +Z; OpenXR is right-handed Y-up with
// forward = -Z, right = +X, up = +Y. Hence:
//
//     blam.x (forward) = -openxr.z
//     blam.y (left)    = -openxr.x
//     blam.z (up)      =  openxr.y
//
// The same permutation carries the quaternion's vector part, with the scalar
// untouched; a handedness-preserving axis permutation is an ordinary basis
// change, not a conjugation.
inline bool Halo4BuildHandNode(
    const Halo4HandPlacementInput& input, const Halo4FirstPersonNode& stock,
    Halo4FirstPersonNode& out) noexcept
{
    if (!Halo4FirstPersonNodeLooksValid(stock))
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(input.controllerOffset[i]))
            return false;
    float q[4];
    if (!Halo4NormalizeQuaternion(input.controllerOrientation, q))
        return false;
    if (!std::isfinite(input.worldScale) || input.worldScale <= 0.0f ||
        !std::isfinite(input.forwardTrim) ||
        !std::isfinite(input.verticalTrim) ||
        !std::isfinite(input.lateralTrim))
    {
        return false;
    }

    const float lateralSign = input.mirrored ? -1.0f : 1.0f;
    const float forward = -input.controllerOffset[2] + input.forwardTrim;
    const float left =
        (-input.controllerOffset[0] + input.lateralTrim) * lateralSign;
    const float up = input.controllerOffset[1] + input.verticalTrim;

    out = stock; // keep the engine's own scale
    out.translation[0] = forward * input.worldScale;
    out.translation[1] = left * input.worldScale;
    out.translation[2] = up * input.worldScale;
    // The controller's own orientation, permuted into Blam axes. This is the
    // ROTATION half of the rigid transform; the translation above is the other.
    out.rotation[0] = -q[2];
    out.rotation[1] = -q[0] * lateralSign;
    out.rotation[2] = q[1];
    out.rotation[3] = q[3];

    // Renormalise: the permutation is exact, but the source quaternion came
    // from a runtime and float drift must never reach an engine bone.
    float normalised[4];
    if (!Halo4NormalizeQuaternion(out.rotation, normalised))
        return false;
    for (int i = 0; i < 4; ++i)
        out.rotation[i] = normalised[i];
    return Halo4FirstPersonNodeLooksValid(out);
}

// Controller ownership for Halo 4's final, world-absolute skinning bank.
//
// This deliberately does not accept the live engine camera or the current HMD
// pose. Halo 4's motion-aim loop already steers the engine camera toward the
// controller, while the render camera separately receives the HMD pose. Using
// either one as a parent here applies tracked rotation twice. The only camera
// state this conversion needs is the stable gameplay origin captured before
// HMD composition; orientation comes from the recenter pair and controller.
struct Halo4ControllerWorldPoseInput
{
    float controllerOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float controllerPosition[3]{};       // OpenXR stage space, metres
    float trackingOriginPosition[3]{};   // HMD position captured at recenter
    float bodyOrigin[3]{};               // Halo 4 world space, pre-HMD
    float headYawReference = 0.0f;
    float gameYawReference = 0.0f;
    float yawSign = -1.0f;
    float pitchSign = 1.0f;
    float worldScale = 0.33f;
    float forwardTrim = 0.0f;            // controller-local metres
    float verticalTrim = 0.0f;
    float lateralTrim = 0.0f;            // positive = controller right
    bool mirrored = false;
};

struct Halo4ControllerWorldPose
{
    float basis[9]{};       // Blam columns: forward, left, up
    float position[3]{};    // Halo 4 world space
};

inline bool Halo4BuildControllerWorldPose(
    const Halo4ControllerWorldPoseInput& input,
    Halo4ControllerWorldPose& out) noexcept
{
    Halo4HeadOrientation controller{};
    if (!Halo4DecodeHeadOrientation(
            input.controllerOrientation, controller))
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(input.controllerPosition[axis]) ||
            !std::isfinite(input.trackingOriginPosition[axis]) ||
            !std::isfinite(input.bodyOrigin[axis]))
            return false;
    }
    if (!std::isfinite(input.headYawReference) ||
        !std::isfinite(input.gameYawReference) ||
        !std::isfinite(input.yawSign) ||
        !std::isfinite(input.pitchSign) ||
        !std::isfinite(input.worldScale) || input.worldScale <= 0.0f ||
        !std::isfinite(input.forwardTrim) ||
        !std::isfinite(input.verticalTrim) ||
        !std::isfinite(input.lateralTrim))
        return false;

    float trackedYaw = input.yawSign * Halo4WrapPi(
        controller.yaw - input.headYawReference);
    float trackedPitch = input.pitchSign * controller.pitch;
    float trackedRoll = controller.roll;
    if (input.mirrored)
    {
        // Reflection about the controller's forward/up plane expressed as a
        // proper rotation: lateral position, yaw and roll change sign while
        // pitch remains unchanged.
        trackedYaw = -trackedYaw;
        trackedRoll = -trackedRoll;
    }
    trackedPitch = trackedPitch < -1.5f
        ? -1.5f : (trackedPitch > 1.5f ? 1.5f : trackedPitch);
    const float worldYaw = input.gameYawReference + trackedYaw;
    if (!std::isfinite(trackedYaw) || !std::isfinite(trackedPitch) ||
        !std::isfinite(trackedRoll) || !std::isfinite(worldYaw))
        return false;
    float forward[3], up[3];
    Halo4ComposeHeadOwnedBasis(
        worldYaw, trackedPitch, trackedRoll, forward, up);
    const float left[3] = {
        up[1] * forward[2] - up[2] * forward[1],
        up[2] * forward[0] - up[0] * forward[2],
        up[0] * forward[1] - up[1] * forward[0]};
    for (int axis = 0; axis < 3; ++axis)
    {
        out.basis[axis] = forward[axis];
        out.basis[3 + axis] = left[axis];
        out.basis[6 + axis] = up[axis];
    }

    const float dx =
        input.controllerPosition[0] - input.trackingOriginPosition[0];
    const float dy =
        input.controllerPosition[1] - input.trackingOriginPosition[1];
    const float dz =
        input.controllerPosition[2] - input.trackingOriginPosition[2];
    const float sh = std::sin(input.headYawReference);
    const float ch = std::cos(input.headYawReference);
    float roomForward = dx * sh - dz * ch;
    float roomRight = dx * ch + dz * sh;
    if (input.mirrored)
        roomRight = -roomRight;

    // Physical displacement is rotated only by the stable recenter/body yaw.
    // Trims are controller-local as promised by halomccvr.cfg, so they use the
    // controller's finished world basis rather than the body basis.
    const float cg = std::cos(input.gameYawReference);
    const float sg = std::sin(input.gameYawReference);
    const float scale = input.worldScale;
    const float bodyOffset[3] = {
        (cg * roomForward + sg * roomRight) * scale,
        (sg * roomForward - cg * roomRight) * scale,
        dy * scale};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float trim =
            forward[axis] * input.forwardTrim -
            left[axis] * input.lateralTrim +
            up[axis] * input.verticalTrim;
        out.position[axis] =
            input.bodyOrigin[axis] + bodyOffset[axis] + trim * scale;
        if (!std::isfinite(out.position[axis]))
            return false;
    }
    return true;
}

// --- C-H4-9: keeping Halo 4's OWN look pitch under the headset ---------------
//
// Taking pitch off the stick fixes the view, and on its own would break the
// game: Halo spawns first-person shots along the ENGINE's camera ray, so a
// frozen engine pitch means every shot leaves level however far up or down the
// player is looking.
//
// Halo 3 solves the same problem with a closed loop that emits a right-stick
// deflection proportional to the angular error and lets the engine integrate it
// through its own turn-rate path (Game_ComputeAimStick). Halo 4 has exactly one
// proven aim anchor at this stage - the observer camera the C-H4-7 transaction
// already reads every frame, which IS the ray shots leave along - and exactly
// one proven actuator, the virtual right stick C-H4-1 accepted. That is enough
// to close the same loop on the pitch axis alone.
//
// Two things are deliberately MEASURED rather than assumed:
//
//  * The direction of the engine's own stick->pitch mapping. `direction`
//    estimates its sign from what the engine's pitch actually did after our
//    last command, so a player with inverted look is followed instead of
//    fought, and a wrong initial guess costs a handful of frames rather than a
//    session. Nothing here encodes a belief about which way Halo 4 pitches.
//  * The actuator's own resolution, through the shared AimServoAxis. ToRawStick
//    floors every non-zero command at 27.5% deflection to clear MCC's inner
//    deadzone, so the engine only ever hears "stop" or "at least 27.5%" - the
//    quantised actuator that produced the Halo 3/ODST turret wiggle. The rest
//    hysteresis parks the loop inside a band widened by the measured step,
//    which is the only thing that stops a limit cycle on an axis this coarse.
struct Halo4PitchServo
{
    AimServoAxis axis{};
    float lastCommand = 0.0f;       // signed stick value we issued last frame
    float lastEnginePitch = 0.0f;
    bool haveLastEnginePitch = false;
    float directionEvidence = 0.0f; // running estimate of the mapping's sign
    float direction = 1.0f;
    uint64_t commandedFrames = 0;
    uint64_t parkedFrames = 0;
};

inline void Halo4ResetPitchServo(Halo4PitchServo& servo) noexcept
{
    servo = Halo4PitchServo{};
}

// Full deflection at ~4.8 degrees of error, the same constant Halo 3's accepted
// aim loop uses. The real ceiling is the engine's own look rate.
inline constexpr float kHalo4PitchServoGain = 12.0f;
// Bounded so the estimate can still flip within a few frames if the mapping
// changes under it (a sensitivity or invert-look change mid-session).
inline constexpr float kHalo4PitchServoEvidenceCap = 6.0f;
// Below this the engine did not measurably move, so the frame says nothing
// about the mapping's direction and must not be counted as evidence.
inline constexpr float kHalo4PitchServoMotionEpsilon = 1.0e-4f;

inline float Halo4PitchServoStep(
    Halo4PitchServo& servo, float enginePitch, float desiredPitch,
    float gain) noexcept
{
    if (!std::isfinite(enginePitch) || !std::isfinite(desiredPitch) ||
        !std::isfinite(gain))
    {
        servo.lastCommand = 0.0f;
        servo.haveLastEnginePitch = false;
        return 0.0f;
    }

    if (servo.haveLastEnginePitch && std::fabs(servo.lastCommand) > 1.0e-3f)
    {
        const float observed = enginePitch - servo.lastEnginePitch;
        if (std::isfinite(observed) &&
            std::fabs(observed) > kHalo4PitchServoMotionEpsilon)
        {
            // sign(observed * issued) IS the sign of the engine's mapping, not
            // "was our guess right" - so the estimate is stable once correct
            // instead of oscillating with the value it is estimating.
            servo.directionEvidence +=
                observed * servo.lastCommand > 0.0f ? 1.0f : -1.0f;
            if (servo.directionEvidence > kHalo4PitchServoEvidenceCap)
                servo.directionEvidence = kHalo4PitchServoEvidenceCap;
            if (servo.directionEvidence < -kHalo4PitchServoEvidenceCap)
                servo.directionEvidence = -kHalo4PitchServoEvidenceCap;
            servo.direction = servo.directionEvidence < 0.0f ? -1.0f : 1.0f;
        }
    }

    AimServoObserve(servo.axis, enginePitch, std::fabs(servo.lastCommand));
    const float command = AimServoCommand(
        servo.axis, desiredPitch - enginePitch, gain,
        kAimServoRestEnterRadians, kAimServoRestExitRadians);
    const float output = command * servo.direction;

    servo.lastCommand = output;
    servo.lastEnginePitch = enginePitch;
    servo.haveLastEnginePitch = true;
    if (output == 0.0f)
        ++servo.parkedFrames;
    else
        ++servo.commandedFrames;
    return output;
}
