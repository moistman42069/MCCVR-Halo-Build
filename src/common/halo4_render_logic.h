#pragma once

#include <cstddef>
#include <cstdint>

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
