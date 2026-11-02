#pragma once

#include <array>
#include <cstdint>

// Player-facing HUD layout behavior is shared across titles. Each title only
// supplies the immutable bytes that prove its native curvature-info layout in
// loaded tag data, plus where that layout keeps the fields the shared sliders
// drive. The record shape is NOT shared: Halo 3 and ODST use the same
// s_chud_curvature_info field order, while Reach's own record inserts a
// "vehicle 3d sensor radius" and four minimap points before its safe frame and
// has no depth field at all.
enum class HudLayoutProfile : uint32_t
{
    None = 0,
    Halo3,
    Halo3ODST,
    HaloReach,
};

inline constexpr int kHudLayoutMaxAnchor = 80;

// Mirrors kMaxSafeFrameHits in the writer.
inline constexpr int kMaxHudLayoutBlocks = 16;

// A title whose curvature record has no runtime depth field. Reach bakes its
// curvature into a derived basis when the tag block is postprocessed at load,
// so there is nothing for the shared depth write to move.
inline constexpr int kHudLayoutNoDepthField = INT32_MIN;

// A title may author more than one curvature record for the same skin, one per
// resolution class (Reach: fullscreen wide / fullscreen standard / halfscreen /
// quarterscreen...). The engine picks one by the render's aspect, so writing
// only the widescreen record is not enough when the render is not widescreen -
// which is exactly the VR case: Reach's per-eye target is 3752x3828 (aspect
// 0.98), nowhere near 16:9. Alternates are matched and written identically to
// the primary anchor; every one still has to pass its own full identity check.
inline constexpr int kHudLayoutMaxAltAnchors = 3;

struct HudLayoutAltAnchor
{
    std::array<uint8_t, kHudLayoutMaxAnchor> anchor;
    std::array<uint8_t, kHudLayoutMaxAnchor> mask;
    int anchorLength;
    int safeFrameOffset;
};

struct HudLayoutAdapter
{
    HudLayoutProfile profile;
    const char* name;
    // Compared at the record's anchor field. mask 0xFF compares, 0x00 ignores.
    // The first eight bytes must be fully compared: they are the scan prefix.
    std::array<uint8_t, kHudLayoutMaxAnchor> anchor;
    std::array<uint8_t, kHudLayoutMaxAnchor> mask;
    int anchorLength;
    // Anchor start to "global safe frame horiz."; vert. follows four bytes on.
    int safeFrameOffset;
    // Safe-frame slot to the depth field, or kHudLayoutNoDepthField.
    int depthFromSlot;
    int expectedBlocks;
    // Halo 3 and ODST keep their proven exact cardinality. Reach accepts any
    // count in this range so a second identity-verified copy of the record can
    // also be written; every target still has to pass the full anchor check.
    int maxBlocks;
    // Halo 3's tag data lives in private read-write memory. Reach's map data
    // does not have to, so its adapter may also inspect mapped regions.
    bool scanMappedRegions;
    // Additional resolution-class records for the same skins (see above).
    std::array<HudLayoutAltAnchor, kHudLayoutMaxAltAnchors> altAnchors;
    int altAnchorCount;
    // Halo 3 and ODST's record is authored-static once loaded: the writer can
    // skip a slot whose bytes already match what we want, because nothing else
    // touches it between our passes. Reach's record is NOT static: read-back
    // proved Reach itself recomputes and overwrites this record live (a fresh
    // authored value appeared with no config change and no title reload), so a
    // "matches already" skip loses the race and our value gets stomped between
    // verify passes. Reach must write every verify pass, unconditionally.
    bool forceWriteEveryPass;
};

inline constexpr std::array<uint8_t, kHudLayoutMaxAnchor> HudLayoutBytes(
    std::initializer_list<uint8_t> bytes)
{
    std::array<uint8_t, kHudLayoutMaxAnchor> out{};
    int i = 0;
    for (uint8_t b : bytes)
    {
        if (i >= kHudLayoutMaxAnchor)
            break;
        out[i++] = b;
    }
    return out;
}

// Halo 3 / ODST: s_chud_curvature_info places dest offset z immediately before
// virtual width, and the safe-frame pair immediately after blip radius.
//   -4  dest offset z
//    0  virtual width, virtual height
//    8  sensor origin, sensor radius, blip radius
//   24  global safe frame horiz., global safe frame vert.
// Headset-rejected: matching alternate records did not affect the visible HUD.
inline constexpr HudLayoutAdapter kHalo3AlternateHudLayoutCandidate = {
    HudLayoutProfile::Halo3,
    "Halo 3",
    HudLayoutBytes({
        0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00, // 1280, 720
        0x00, 0x00, 0x5C, 0x42, 0x00, 0x40, 0x25, 0x44, // 55.0, 661.0
        0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x80, 0x40, // 58.0, 4.0
    }),
    HudLayoutBytes({
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    }),
    24,
    24,
    -28,
    2,
    3,
    false,
    // Ultimate Firefight authors two live resolution classes; its stock
    // 1280x720 record has zero safe-frame scale and remains rejected.
    {{
        {HudLayoutBytes({0x40,0x06,0x00,0x00,0x44,0x02,0x00,0x00,0x00,0x00,0x5C,0x42,0x00,0x00,0x02,0x44,0x00,0x00,0x68,0x42,0x00,0x00,0x80,0x40}),
         HudLayoutBytes({0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}),24,24},
        {HudLayoutBytes({0xC0,0x03,0x00,0x00,0x44,0x02,0x00,0x00,0x00,0x00,0x5C,0x42,0x00,0x00,0x02,0x44,0x00,0x00,0x68,0x42,0x00,0x00,0x80,0x40}),
         HudLayoutBytes({0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}),24,24}
    }},
    2,
    false,
};

// Restore the accepted stock locator while retaining the failed candidate.
inline constexpr HudLayoutAdapter kHalo3HudLayoutAdapter = [] {
    auto adapter=kHalo3AlternateHudLayoutCandidate;
    adapter.expectedBlocks=3;
    adapter.maxBlocks=3;
    adapter.altAnchorCount=0;
    return adapter;
}();

inline constexpr HudLayoutAdapter kOdstHudLayoutAdapter = {
    HudLayoutProfile::Halo3ODST,
    "Halo 3: ODST",
    HudLayoutBytes({
        0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00, // 1280, 720
        0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44, // 2000.0, 2000.0
        0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x80, 0x40, // 58.0, 4.0
    }),
    HudLayoutBytes({
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    }),
    24,
    24,
    -28,
    1,
    1,
    false,
    {},
    0,
    false,
};

// Halo: Reach's own record, from official HREK evidence only. The block's
// load-time postprocess function (reach_tag_test.exe 0x8E7170, the code behind
// "Curvature points are invalid.  Defaulting to no curvature") writes the
// identity curvature grid to +0x04..+0x4B, which pins the record start; the
// official chud_globals_definition export gives the field order after it:
//   +0x00 res flags        +0x04 nine curvature points
//   +0x4C screen transform basis (derived at postprocess from those points)
//   +0x94 virtual width    +0x98 virtual height
//   +0x9C sensor origin    +0xA4 sensor radius
//   +0xA8 vehicle 3d sensor radius   +0xAC blip radius
//   +0xB0 minimap world UL/LR, minimap hud UL/LR
//   +0xD0 global safe frame horiz.   +0xD4 global safe frame vert.
//   +0xD8 safe frame horizontal ding +0xDC safe frame vertical ding
// The anchor therefore starts at virtual width (+0x94) and the safe-frame pair
// sits 60 bytes later, not 24. Three skins (default, dervish, monitor) each
// author five curvature records; only the "fullscreen wide{720p fullscreen}"
// one applies to a fullscreen VR render, which is the same one-per-skin rule
// Halo 3 already uses. Those three differ in sensor origin Y (656/650/656) and
// sensor radius (72/68/72), and dervish alone authors nonzero minimap points,
// so those bytes are wildcards; the two safe-frame floats are wildcards
// because they are what this feature writes. The safe-frame dings are 0.0 in
// all three, and Reach's extra vehicle radius is a field Halo 3 does not have
// at all, so the compared bytes stay stronger than Halo 3's anchor.
//
// Reach has no dest offset z. Its curvature is a nine-point grid that the
// engine folds into the derived basis once, when the tag block loads, so a
// runtime write to it cannot move anything. The depth field is declared absent
// rather than pointed at something inert.
inline constexpr HudLayoutAdapter kReachHudLayoutAdapter = {
    HudLayoutProfile::HaloReach,
    "Halo: Reach",
    HudLayoutBytes({
        0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00, // 1280, 720
        0x00, 0x00, 0xB0, 0x42, 0x00, 0x00, 0x00, 0x00, // 88.0, sensor origin Y
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x42, // sensor radius, 80.0
        0x00, 0x00, 0xC0, 0x40,                         // blip radius 6.0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // minimap world UL
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // minimap world LR
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // minimap hud UL
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // minimap hud LR
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // safe frame horiz/vert
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // both dings 0.0
    }),
    HudLayoutBytes({
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    }),
    76,
    60,
    kHudLayoutNoDepthField,
    3,
    kMaxHudLayoutBlocks,
    true,
    // Alternate resolution class: "fullscreen standard{480i fullscreen}".
    // Reach authors this record identically in all three skins (920x690
    // virtual canvas, sensor origin 88,624, sensor radius 58, vehicle 3d
    // sensor radius 78, blip radius 6), so no byte here needs a wildcard.
    // These are the game's own authored virtual-canvas constants, NOT the
    // player's render resolution - covering both aspect classes is precisely
    // what makes this independent of whatever resolution or window mode is in
    // use, because the engine's own choice between them is then irrelevant.
    {{
        {
            HudLayoutBytes({
                0x98, 0x03, 0x00, 0x00, 0xB2, 0x02, 0x00, 0x00, // 920, 690
                0x00, 0x00, 0xB0, 0x42, 0x00, 0x00, 0x1C, 0x44, // 88.0, 624.0
                0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x9C, 0x42, // 58.0, 78.0
                0x00, 0x00, 0xC0, 0x40,                         // blip 6.0
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // minimap
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // safe frame
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // dings 0/0
            }),
            HudLayoutBytes({
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            }),
            76,
            60,
        },
    }},
    1,
    true,
};

inline constexpr const HudLayoutAdapter* HudLayoutAdapterFor(
    HudLayoutProfile profile)
{
    switch (profile)
    {
    case HudLayoutProfile::Halo3:
        return &kHalo3HudLayoutAdapter;
    case HudLayoutProfile::Halo3ODST:
        return &kOdstHudLayoutAdapter;
    case HudLayoutProfile::HaloReach:
        return &kReachHudLayoutAdapter;
    default:
        return nullptr;
    }
}

// One resolution-class anchor of an adapter: primary (index 0) or alternate.
struct HudLayoutAnchorView
{
    const uint8_t* anchor;
    const uint8_t* mask;
    int anchorLength;
    int safeFrameOffset;
};

inline constexpr int HudLayoutAnchorCount(const HudLayoutAdapter& adapter)
{
    return 1 + adapter.altAnchorCount;
}

inline constexpr HudLayoutAnchorView HudLayoutAnchorAt(
    const HudLayoutAdapter& adapter, int index)
{
    if (index <= 0)
        return HudLayoutAnchorView{
            adapter.anchor.data(), adapter.mask.data(),
            adapter.anchorLength, adapter.safeFrameOffset};
    const HudLayoutAltAnchor& alt = adapter.altAnchors[index - 1];
    return HudLayoutAnchorView{
        alt.anchor.data(), alt.mask.data(),
        alt.anchorLength, alt.safeFrameOffset};
}

inline constexpr bool HudLayoutHasDepthField(const HudLayoutAdapter& adapter)
{
    return adapter.depthFromSlot != kHudLayoutNoDepthField;
}

// Every adapter must keep the scan prefix fully compared and stay inside the
// fixed anchor buffer, or the shared scanner would silently widen its match.
inline constexpr bool HudLayoutAnchorWellFormed(
    const HudLayoutAnchorView& view)
{
    if (view.anchorLength < 8 || view.anchorLength > kHudLayoutMaxAnchor)
        return false;
    if (view.safeFrameOffset < 8 ||
        view.safeFrameOffset + 8 > kHudLayoutMaxAnchor)
        return false;
    for (int i = 0; i < 8; ++i)
    {
        if (view.mask[i] != 0xFF)
            return false;
    }
    for (int i = view.safeFrameOffset;
         i < view.safeFrameOffset + 8 && i < view.anchorLength; ++i)
    {
        if (view.mask[i] != 0x00)
            return false;
    }
    return true;
}

inline constexpr bool HudLayoutAdapterWellFormed(
    const HudLayoutAdapter& adapter)
{
    if (adapter.altAnchorCount < 0 ||
        adapter.altAnchorCount > kHudLayoutMaxAltAnchors)
        return false;
    for (int i = 1; i < HudLayoutAnchorCount(adapter); ++i)
    {
        if (!HudLayoutAnchorWellFormed(HudLayoutAnchorAt(adapter, i)))
            return false;
    }
    if (adapter.anchorLength < 8 || adapter.anchorLength > kHudLayoutMaxAnchor)
        return false;
    if (adapter.safeFrameOffset < 8 ||
        adapter.safeFrameOffset + 8 > kHudLayoutMaxAnchor)
        return false;
    if (adapter.expectedBlocks <= 0)
        return false;
    for (int i = 0; i < 8; ++i)
    {
        if (adapter.mask[i] != 0xFF)
            return false;
    }
    // The safe-frame pair is written by this feature, so it can never be part
    // of the compared identity. Halo 3 and ODST stop their anchor right where
    // the pair begins; Reach's anchor reaches past it to the dings, so those
    // eight bytes must be wildcards there.
    for (int i = adapter.safeFrameOffset;
         i < adapter.safeFrameOffset + 8 && i < adapter.anchorLength; ++i)
    {
        if (adapter.mask[i] != 0x00)
            return false;
    }
    return true;
}

// The scan must be able to read the safe-frame pair as well as the anchor.
inline constexpr int HudLayoutScanSpan(const HudLayoutAnchorView& view)
{
    const int throughSafeFrame = view.safeFrameOffset + 8;
    return view.anchorLength > throughSafeFrame
        ? view.anchorLength : throughSafeFrame;
}

static_assert(HudLayoutAdapterWellFormed(kHalo3HudLayoutAdapter));
static_assert(HudLayoutAdapterWellFormed(kOdstHudLayoutAdapter));
static_assert(HudLayoutAdapterWellFormed(kReachHudLayoutAdapter));

inline constexpr bool HudLayoutPublicationMatches(
    HudLayoutProfile currentProfile, uint32_t currentGeneration,
    HudLayoutProfile publishedProfile, uint32_t publishedGeneration)
{
    return currentProfile != HudLayoutProfile::None &&
        currentProfile == publishedProfile &&
        currentGeneration == publishedGeneration;
}

// Halo 3 and ODST prove an exact cardinality. Reach only proves a floor: a
// second identity-verified copy of the same record is a target, not a reason to
// reject every candidate.
inline constexpr bool HudLayoutAcceptedCountOk(
    const HudLayoutAdapter& adapter, int accepted)
{
    return accepted >= adapter.expectedBlocks &&
        accepted <= adapter.maxBlocks;
}

// The safe-frame slots point into the running level's tag data, which the
// title frees while it unloads. Its camera beacon is the proof the level is
// still live: no slot is read, verified, rescanned or written unless the title
// copied a camera within this window. Build bd2aac6 read Halo 3's cached slot
// 0 a second after the last camera copy, as the level was unloading, and
// faulted (caught by SafeFrameVerifySlot's own guard).
inline constexpr uint64_t kHudLayoutLiveCameraWindowMs = 1000;

inline constexpr bool HudLayoutTitleFrameLive(
    uint64_t lastCameraMs, uint64_t nowMs)
{
    return lastCameraMs != 0 && nowMs >= lastCameraMs &&
        nowMs - lastCameraMs <= kHudLayoutLiveCameraWindowMs;
}

inline constexpr bool HudLayoutCanReacquireFromRemembered(
    int rememberedCount, int expectedBlocks)
{
    return expectedBlocks > 0 && rememberedCount == expectedBlocks;
}

inline constexpr bool HudLayoutCanReacquireFromRemembered(
    const HudLayoutAdapter& adapter, int rememberedCount)
{
    return HudLayoutAcceptedCountOk(adapter, rememberedCount);
}
