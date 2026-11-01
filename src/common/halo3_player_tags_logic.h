// PLAYER TAGS, Halo 3 and ODST: the pure half of src/dll/halo3_player_tags.inl. No game, engine or OpenXR includes.
//
// ODST (see its signatures below): its draw is Halo 3's per entry, proven in halo3odst.dll's own code; its
// producers were not read (H3ODSTEK was not at hand), so that ODST marks objectives and carriers as Halo 3 does
// (IsPlayerTag) is the one thing taken over. The draw's census logs each navpoint kind a level shows, with how it
// was classed, so a player's game confirms or refutes it; a wrong class only sizes or fades a marker, never more.
//
// WHAT THE GAME DOES (read in the H3EK debug build halo3_tag_test.exe, chud_navpoints.cpp / chud_draw.cpp by their
// assert text, then matched in retail halo3.dll 1.3528; every name below is ours).
//
//   Each user's HUD keeps twenty navpoints, 0x74 bytes each, at the start of a struct the HUD draw hands, by
//   pointer, to ONE function: chud_draw_navpoints(navpoints, user) (retail +0x2F3D80, one caller, the user's HUD
//   draw at +0x2D2A95). It walks the twenty inline and, for each valid entry, projects its world position
//   (+0x2F2424, which reads the CURRENT VIEW's camera), then draws its arrow, its service tag (or the focused
//   player's full gamertag) and its second icon. Per entry:
//       +0x04  icon #1 sequence (2 on an objective)     +0x10  flags (0x01 off-screen ring, 0x40 radar, 0x80 goal)
//       +0x14  alpha multiplier                         +0x24  position_worldspace (world units)
//       +0x30  valid                                    +0x38  distance scale: clamp(20 / d, the skin's limits)
//       +0x70  fade
//   Every element of an entry is drawn with alpha = fade * alpha multiplier, and sized by the distance scale
//   (icons 0.5x, text 0.75x and 14x of it). Nothing else reads +0x14 or +0x38 between the update that writes them
//   and this draw (the add, the update and the radar read only +0x10, +0x24 and +0x30).
//
//   HOW MANY. The stock array holds twenty; after it come the focused player's gamertag (+0xC past the array) and
//   the focused entry's index (+0x38 past it), both addressed by the draw with a 32-bit displacement. A host may
//   enlarge the array: Project Reclaimer moves everything after it (the gamertag read becomes +0x601C, so 212
//   entries) and replaces the draw's own count check. So the count is never assumed: it is read from those two
//   displacements in the running code, which must agree with each other and with the stride.
//
//   A PLAYER navpoint (a teammate's service tag and arrow) has icon #1 0, 1 or none and none of flags 0x01 / 0x40.
//   Objectives write icon #1 = 2 and flags 0x41 / 0xC1; a flag, ball or bomb carrier sets 0x01 and keeps its
//   marker, so carriers are left as the game draws them.
//
// THE SEAM: chud_draw_navpoints. Around the original, each player entry's alpha multiplier and distance scale are
// set from the player's settings and put back afterwards (only where they still hold what was written, so an update
// that ran meanwhile keeps its own value). Distance is measured from the view the draw projects with: the camera
// the projection reads, at [view] + 8.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace halo3_player_tags
{
// ---- the signatures (halo3.dll 1.3528; each matches exactly once where it is searched) ----
// ADDR-HYGIENE: resolved. Every address is decoded from a pattern that must match exactly once: the draw by its
// entry, proven again by its one caller's call operand; the projection by the draw's call operand; the view by the
// projection's own read. The entry layout is proven by the draw's own instructions that read it (below).
// The integers beside each pattern are positions INSIDE that pattern: ADDR-HYGIENE: structural.
//
// The draw's entry: its prologue up to `mov r14, rcx` (the navpoints). Searched in the whole module.
inline constexpr char kDrawEntryPattern[] =
    "48 89 5C 24 08 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8 ?? ?? ?? "
    "?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 33 FF 0F 29 B4 24 ?? ?? ?? ?? 4C 8B F1";
// Its caller, the user's HUD draw: `add rcx, <the navpoints in the user's HUD block>; add rcx, rsi; call <draw>`.
// The HUD block offset is a wildcard: Project Reclaimer enlarges the block. The call's rel32 is at +26.
inline constexpr char kCallerPattern[] =
    "80 B8 ?? ?? ?? ?? 00 75 15 49 8B 0C 3F 8B D3 48 81 C1 ?? ?? ?? ?? 48 03 CE E8 ?? ?? ?? ??";
inline constexpr int kCallerCallRel = 26;
// The rest are searched in the draw's own body (its first kDrawBodyBytes) and must match there exactly once.
// rbx walks the entries at +8 (rbx = entry + 8):
//   the record: lea rsi,[rbx+0x34] (entry +0x3C); movsd xmm0,[rbx+0x1C] (+0x24, the position); ... [rbx+0x24].
inline constexpr char kRecordPattern[] =
    "48 8D 73 34 45 33 ED 88 06 F2 0F 10 43 1C F2 0F 11 43 38 8B 43 24 89 43 40";
//   the projection: mov rcx,rsi; call <projection>; ...; mov r12d,[r14+<focused index>]. Rel32 at +4, disp32 at +17.
inline constexpr char kProjectionCallPattern[] = "48 8B CE E8 ?? ?? ?? ?? F3 44 0F 10 43 54 45 8B A6 ?? ?? ?? ??";
inline constexpr int kProjectionCallRel = 4;
inline constexpr int kFocusIndexDisp = 17;
//   the focused gamertag: lea r9,[r14+<gamertag>]. Its disp32 at +3.
inline constexpr char kGamertagPattern[] = "4D 8D 8E ?? ?? ?? ?? 48 89";
inline constexpr int kGamertagDisp = 3;
//   the size: movss xmm7,[rbx+0x30] (entry +0x38, the distance scale).
inline constexpr char kScaleReadPattern[] = "F3 0F 10 7B 30 41 83 F8 FF";
//   the alpha: movss xmm1,[rbx+0x68] (+0x70, fade); xorps; mulss xmm1,[rbx+0x0C] (+0x14, the alpha multiplier).
//   The same three instructions draw the arrow, the text and the second icon, so this is searched for at least once.
inline constexpr char kAlphaReadPattern[] = "F3 0F 10 4B 68 0F 57 C0 F3 0F 59 4B 0C";
//   the stride: add rbx,0x74 (its count check, which follows, is a host's to replace; see HOW MANY).
inline constexpr char kStridePattern[] = "48 83 C3 74";
inline constexpr size_t kDrawBodyBytes = 0x930; // the draw's extent in .pdata is 0x92C bytes
// In the projection's first kProjectionHeadBytes: mov rax,[rip+view]; movsxd rcx,[rax+0x27F4]. Its disp32 at +3.
inline constexpr char kViewReadPattern[] = "48 8B 05 ?? ?? ?? ?? 48 63 88 F4 27 00 00";
inline constexpr int kViewReadDisp = 3;
inline constexpr size_t kProjectionHeadBytes = 0x100;

// ---- ODST (halo3odst.dll, MCC 2025.08.16; read in retail, each matching exactly once where it is searched) ----
// ODST's chud_draw_navpoints (+0x32FEE0, one caller, the user's HUD draw at +0x2FDB1C, the navpoints at the HUD
// block's +0x156C) is Halo 3's per entry: it reads the same thirteen entry fields at the same offsets the same number
// of times, with the same 0x74 stride, and walks twenty (its loop bound is `cmp r12d, 0x14`). It keeps the navpoints
// in r13, not r14, and puts the focused gamertag 0x40 past the array (Halo 3: 0xC); the focused index is 0x38 past
// it in both. The patterns below are ODST's own code; where ODST's bytes equal Halo 3's (the alpha read, the stride),
// Halo 3's pattern is used and matches in ODST's draw.
//   The entry: through `mov r13, rcx` (the navpoints). Searched in the whole module.
inline constexpr char kOdstDrawEntryPattern[] =
    "89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8 ?? ?? "
    "?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 45 33 FF 0F 29 B4 24 ?? ?? ?? ?? 44 8B F2 0F 29 BC 24 ?? ?? ?? ?? 4C 8B E9";
//   The caller: `lea rcx, [r9 + <the navpoints in the user's HUD block>]; mov edx, ebx; add rcx, rsi; call <draw>`.
//   The call's rel32 is at +27.
inline constexpr char kOdstCallerPattern[] =
    "48 8B 04 38 40 38 AC 18 ?? ?? ?? ?? 74 15 49 8D 89 ?? ?? ?? ?? 8B D3 48 03 CE E8 ?? ?? ?? ??";
inline constexpr int kOdstCallerCallRel = 27;
//   The record: lea rsi,[rbx+0x34]; mov [rsi],al; movsd xmm0,[rbx+0x1C] (+0x24, the position); ... [rbx+0x24].
inline constexpr char kOdstRecordPattern[] = "48 8D 73 34 88 06 F2 0F 10 43 1C F2 0F 11 43 38 8B 43 24 89 43 40";
//   The projection: mov rcx,rsi; call <projection>; mov eax,[r13+<focused index>]. Rel32 at +4, disp32 at +11.
inline constexpr char kOdstProjectionCallPattern[] = "48 8B CE E8 ?? ?? ?? ?? 41 8B 85 ?? ?? ?? ??";
inline constexpr int kOdstProjectionCallRel = 4;
inline constexpr int kOdstFocusIndexDisp = 11;
//   The focused gamertag: lea r9,[r13+<gamertag>]. Its disp32 at +3.
inline constexpr char kOdstGamertagPattern[] = "4D 8D 8D ?? ?? ?? ??";
inline constexpr int kOdstGamertagDisp = 3;
inline constexpr uint32_t kOdstGamertagPastEntries = 0x40;
//   The size: movss xmm8,[rbx+0x30] (entry +0x38, the distance scale).
inline constexpr char kOdstScaleReadPattern[] = "F3 44 0F 10 43 30 41 83 F8 FF";
inline constexpr size_t kOdstDrawBodyBytes = 0xC88; // the draw's extent in .pdata is 0xC87 bytes
//   In the projection's head: mov rax,[rip+view]; movss xmm1,[rcx+8]; lea rdi,[rax+8] (the camera's position, the
//   point every navpoint is projected from). Its disp32 at +3.
inline constexpr char kOdstViewReadPattern[] = "48 8B 05 ?? ?? ?? ?? F3 0F 10 49 08 48 8D 78 08";
inline constexpr int kOdstViewReadDisp = 3;

// ---- the entry (ADDR-HYGIENE: structural; proven in place by the patterns above, see the header) ----
inline constexpr int kStockEntryCount = 20;
inline constexpr int kMaxEntryCount = 256;           // more than this is refused, not adjusted
inline constexpr size_t kEntryStride = 0x74;
inline constexpr uint32_t kGamertagPastEntries = 0xC;
inline constexpr uint32_t kFocusIndexPastEntries = 0x38;
inline constexpr size_t kIconOffset = 0x04;
inline constexpr size_t kFlagsOffset = 0x10;
inline constexpr size_t kAlphaOffset = 0x14;
inline constexpr size_t kPositionOffset = 0x24;
inline constexpr size_t kValidOffset = 0x30;
inline constexpr size_t kScaleOffset = 0x38;
inline constexpr uint32_t kObjectiveFlags = 0x41; // the off-screen ring and the radar: objectives and carriers
inline constexpr int32_t kObjectiveIcon = 2;
// The view the projection reads: the camera's position, three floats at +8.
inline constexpr size_t kViewPositionOffset = 0x08;
// Halo 3's world unit: 10 feet.
inline constexpr float kMetresPerWorldUnit = 3.048f;

// The number of entries, from the two displacements the draw addresses past the array (HOW MANY): both must put the
// array's end at the same whole number of entries, from twenty to kMaxEntryCount. 0 when they do not.
// `gamertagPastEntries`: where the title keeps the focused gamertag past the array (Halo 3 0xC, ODST 0x40).
inline int EntryCountFromLayout(uint32_t gamertagDisp, uint32_t focusIndexDisp,
                                uint32_t gamertagPastEntries = kGamertagPastEntries) noexcept
{
    if (gamertagDisp < gamertagPastEntries || focusIndexDisp < kFocusIndexPastEntries)
        return 0;
    const uint32_t end = gamertagDisp - gamertagPastEntries;
    if (end != focusIndexDisp - kFocusIndexPastEntries || end % kEntryStride)
        return 0;
    const uint32_t count = end / static_cast<uint32_t>(kEntryStride);
    return (count >= static_cast<uint32_t>(kStockEntryCount) && count <= static_cast<uint32_t>(kMaxEntryCount))
               ? static_cast<int>(count)
               : 0;
}

// A teammate's service tag and arrow, not an objective or a carrier.
inline bool IsPlayerTag(int32_t icon, uint32_t flags) noexcept
{
    return icon != kObjectiveIcon && (flags & kObjectiveFlags) == 0;
}

struct Settings
{
    bool show = true;
    float scale = 1.0f;          // of the game's own size
    float opacity = 1.0f;        // of the game's own alpha
    float maxDistanceM = 0.0f;   // 0 = no limit
};

inline constexpr float kScaleMin = 0.25f, kScaleMax = 2.0f;
inline constexpr float kMaxDistanceLimitM = 300.0f;

// Settings as a player can set them, brought into range.
inline Settings Clamped(Settings s) noexcept
{
    const auto clamp = [](float v, float lo, float hi, float fallback) {
        return std::isfinite(v) ? (v < lo ? lo : (v > hi ? hi : v)) : fallback;
    };
    s.scale = clamp(s.scale, kScaleMin, kScaleMax, 1.0f);
    s.opacity = clamp(s.opacity, 0.0f, 1.0f, 1.0f);
    s.maxDistanceM = clamp(s.maxDistanceM, 0.0f, kMaxDistanceLimitM, 0.0f);
    return s;
}

// Nothing to change: the draw is left exactly as the game makes it.
inline bool IsStock(const Settings& s) noexcept
{
    return s.show && s.scale == 1.0f && s.opacity == 1.0f && s.maxDistanceM <= 0.0f;
}

// A non-zero number that changes when any setting changes (for a log line per change).
inline uint32_t Fingerprint(const Settings& s) noexcept
{
    const auto q = [](float v) { return static_cast<uint32_t>(static_cast<int>(std::lround(v * 100.0f)) & 0xFFFF); };
    uint32_t h = 2166136261u;
    for (uint32_t part : {s.show ? 1u : 0u, q(s.scale), q(s.opacity), q(s.maxDistanceM)})
        h = (h ^ part) * 16777619u;
    return h ? h : 1u;
}

// How far before the limit a tag starts to fade: a fifth of the limit, never more than 5 m.
inline float FadeBandM(float maxDistanceM) noexcept
{
    const float band = maxDistanceM * 0.2f;
    return band < 5.0f ? band : 5.0f;
}

// What one player tag gets: its alpha multiplied by `alpha`, its size by `scale`. `distanceM` < 0 = unknown, and an
// unknown distance never hides a tag.
struct Adjust
{
    float alpha = 1.0f;
    float scale = 1.0f;
};
inline Adjust AdjustFor(const Settings& s, float distanceM) noexcept
{
    Adjust a{};
    if (!s.show)
    {
        a.alpha = 0.0f;
        return a;
    }
    a.alpha = s.opacity;
    a.scale = s.scale;
    if (s.maxDistanceM > 0.0f && distanceM >= 0.0f && std::isfinite(distanceM))
    {
        const float band = FadeBandM(s.maxDistanceM);
        const float start = s.maxDistanceM - band;
        if (distanceM >= s.maxDistanceM)
            a.alpha = 0.0f;
        else if (band > 0.0f && distanceM > start)
            a.alpha *= (s.maxDistanceM - distanceM) / band;
    }
    return a;
}

// Distance in metres between two world points in world units; -1 when either is not finite.
inline float DistanceM(const float a[3], const float b[3]) noexcept
{
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::isfinite(d) ? d * kMetresPerWorldUnit : -1.0f;
}

// One title's draw: where each of the signatures above is, for halo3_player_tags.inl's install and checks. The entry
// layout (stride, field offsets) is the same in both and is proven per title by its own draw's reads.
struct DrawProfile
{
    const char* title;
    const char* drawEntryPattern;
    const char* callerPattern;
    int callerCallRel;
    const char* recordPattern;
    const char* projectionCallPattern;
    int projectionCallRel;
    int focusIndexDisp;
    const char* gamertagPattern;
    int gamertagDisp;
    uint32_t gamertagPastEntries;
    const char* scaleReadPattern;
    const char* alphaReadPattern;
    const char* stridePattern;
    size_t drawBodyBytes;
    const char* viewReadPattern;
    int viewReadDisp;
    size_t projectionHeadBytes;
};
inline constexpr DrawProfile kHalo3Draw{
    "Halo 3", kDrawEntryPattern, kCallerPattern, kCallerCallRel, kRecordPattern, kProjectionCallPattern,
    kProjectionCallRel, kFocusIndexDisp, kGamertagPattern, kGamertagDisp, kGamertagPastEntries, kScaleReadPattern,
    kAlphaReadPattern, kStridePattern, kDrawBodyBytes, kViewReadPattern, kViewReadDisp, kProjectionHeadBytes};
inline constexpr DrawProfile kOdstDraw{
    "ODST", kOdstDrawEntryPattern, kOdstCallerPattern, kOdstCallerCallRel, kOdstRecordPattern,
    kOdstProjectionCallPattern, kOdstProjectionCallRel, kOdstFocusIndexDisp, kOdstGamertagPattern, kOdstGamertagDisp,
    kOdstGamertagPastEntries, kOdstScaleReadPattern, kAlphaReadPattern, kStridePattern, kOdstDrawBodyBytes,
    kOdstViewReadPattern, kOdstViewReadDisp, kProjectionHeadBytes};

// A navpoint kind as the census records it (icon #1 and flags), for a log line per new kind and title. Never 0 (an
// empty census slot): bit 31 marks a kind; the flags use the low byte.
inline uint64_t KindKey(int32_t icon, uint32_t flags) noexcept
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(icon)) << 32) | 0x80000000u | (flags & 0x7FFFFFFFu);
}
inline int32_t KindIcon(uint64_t key) noexcept { return static_cast<int32_t>(static_cast<uint32_t>(key >> 32)); }
inline uint32_t KindFlags(uint64_t key) noexcept { return static_cast<uint32_t>(key) & 0x7FFFFFFFu; }
} // namespace halo3_player_tags
