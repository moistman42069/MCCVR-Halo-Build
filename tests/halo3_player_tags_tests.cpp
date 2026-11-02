// Halo 3 player tags (src/common/halo3_player_tags_logic.h): which navpoints are teammates' tags, what each setting
// does to one, and that every position named beside a signature lands on the bytes its comment names.
#include "halo3_player_tags_logic.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(float a, float b, float tol = 1.0e-4f) { return std::fabs(a - b) <= tol; }

// A pattern's tokens: a byte, or -1 for "??".
std::vector<int> Tokens(const char* pattern)
{
    std::vector<int> out;
    std::string token;
    for (const char* p = pattern;; ++p)
    {
        if (*p == ' ' || *p == '\0')
        {
            if (!token.empty())
                out.push_back(token == "??" ? -1 : static_cast<int>(std::stoul(token, nullptr, 16)));
            token.clear();
            if (*p == '\0')
                break;
        }
        else
            token += *p;
    }
    return out;
}
bool Wild(const std::vector<int>& t, int at, int count)
{
    if (at < 0 || at + count > static_cast<int>(t.size()))
        return false;
    for (int i = at; i < at + count; ++i)
        if (t[i] != -1)
            return false;
    return true;
}
} // namespace

int main()
{
    using namespace halo3_player_tags;

    // Teammates' tags against objectives and carriers, as the producers write them.
    Check(IsPlayerTag(0, 0x00), "a teammate, no state");
    Check(IsPlayerTag(1, 0x04), "a teammate firing");
    Check(IsPlayerTag(-1, 0x10), "a teammate speaking, no icon");
    Check(IsPlayerTag(0, 0x3C), "every player state flag");
    Check(!IsPlayerTag(2, 0x41), "a CTF objective");
    Check(!IsPlayerTag(2, 0xC1), "an objective with its distance text");
    Check(!IsPlayerTag(0, 0x01), "a carrier on the off-screen ring");
    Check(!IsPlayerTag(1, 0x40), "a radar entry");

    // Settings into range.
    Settings s{};
    Check(IsStock(Clamped(s)), "the defaults change nothing");
    s.scale = 9.0f;
    s.opacity = -1.0f;
    s.maxDistanceM = 5000.0f;
    Settings c = Clamped(s);
    Check(c.scale == kScaleMax && c.opacity == 0.0f && c.maxDistanceM == kMaxDistanceLimitM, "out of range clamps");
    s.scale = NAN;
    s.opacity = NAN;
    s.maxDistanceM = NAN;
    c = Clamped(s);
    Check(c.scale == 1.0f && c.opacity == 1.0f && c.maxDistanceM == 0.0f, "NaN reads as the default");

    // What a tag gets.
    Settings hide{};
    hide.show = false;
    Check(!IsStock(hide) && AdjustFor(hide, 3.0f).alpha == 0.0f, "hidden");
    Settings look{};
    look.scale = 0.5f;
    look.opacity = 0.4f;
    Adjust a = AdjustFor(look, 50.0f);
    Check(Near(a.scale, 0.5f) && Near(a.alpha, 0.4f), "size and opacity, no distance limit");
    Settings far{};
    far.maxDistanceM = 40.0f;
    Check(Near(FadeBandM(40.0f), 5.0f), "the fade band is at most 5 m");
    Check(Near(FadeBandM(10.0f), 2.0f), "the fade band is a fifth of a short limit");
    Check(Near(AdjustFor(far, 20.0f).alpha, 1.0f), "inside the limit");
    Check(Near(AdjustFor(far, 37.5f).alpha, 0.5f), "half way through the fade");
    Check(AdjustFor(far, 40.0f).alpha == 0.0f && AdjustFor(far, 90.0f).alpha == 0.0f, "past the limit");
    Check(Near(AdjustFor(far, -1.0f).alpha, 1.0f), "an unknown distance never hides");
    Check(Near(AdjustFor(far, 90.0f).scale, 1.0f), "the limit does not change size");
    Settings both = far;
    both.opacity = 0.5f;
    Check(Near(AdjustFor(both, 37.5f).alpha, 0.25f), "the fade multiplies the opacity");

    // Distance in metres from world units (10 ft each).
    const float a0[3]{0.0f, 0.0f, 0.0f}, a1[3]{3.0f, 4.0f, 0.0f};
    Check(Near(DistanceM(a0, a1), 5.0f * kMetresPerWorldUnit), "5 world units");
    const float bad[3]{NAN, 0.0f, 0.0f};
    Check(DistanceM(a0, bad) < 0.0f, "a NaN position is unknown");

    // A log line per change.
    Settings f1{}, f2{};
    f2.opacity = 0.5f;
    Check(Fingerprint(f1) != Fingerprint(f2) && Fingerprint(f1) != 0u, "fingerprints differ per setting");

    // Every position beside a signature lands on its wildcards, and the layout patterns spell the offsets they claim.
    const auto caller = Tokens(kCallerPattern);
    Check(caller[kCallerCallRel - 1] == 0xE8 && Wild(caller, kCallerCallRel, 4), "the caller's call rel32");
    const auto call = Tokens(kProjectionCallPattern);
    Check(call[kProjectionCallRel - 1] == 0xE8 && Wild(call, kProjectionCallRel, 4), "the projection call's rel32");
    const auto view = Tokens(kViewReadPattern);
    Check(view[0] == 0x48 && view[1] == 0x8B && view[2] == 0x05 && Wild(view, kViewReadDisp, 4), "the view's disp32");
    const auto record = Tokens(kRecordPattern);
    Check(record[3] + 8 == 0x3C && record[13] + 8 == static_cast<int>(kPositionOffset), "record at +0x3C, position");
    const auto scale = Tokens(kScaleReadPattern);
    Check(scale[4] + 8 == static_cast<int>(kScaleOffset), "the size read is the distance scale");
    const auto alpha = Tokens(kAlphaReadPattern);
    Check(alpha[4] + 8 == 0x70 && alpha[12] + 8 == static_cast<int>(kAlphaOffset), "fade times the alpha multiplier");
    const auto stride = Tokens(kStridePattern);
    Check(stride[3] == static_cast<int>(kEntryStride), "the stride");
    const auto gamertag = Tokens(kGamertagPattern);
    Check(gamertag[0] == 0x4D && gamertag[1] == 0x8D && Wild(gamertag, kGamertagDisp, 4), "the gamertag's disp32");
    Check(call[kFocusIndexDisp - 3] == 0x45 && call[kFocusIndexDisp - 2] == 0x8B && Wild(call, kFocusIndexDisp, 4),
          "the focused index's disp32");

    // The number of entries from the two fields past them.
    Check(EntryCountFromLayout(0x91C, 0x948) == 20, "MCC's twenty (retail halo3.dll)");
    Check(EntryCountFromLayout(0x601C, 0x6048) == 212, "an enlarged array (Project Reclaimer's 212)");
    Check(EntryCountFromLayout(0x91C, 0x6048) == 0, "the two fields disagree");
    Check(EntryCountFromLayout(0x920, 0x94C) == 0, "not a whole number of entries");
    Check(EntryCountFromLayout(0x800, 0x82C) == 0, "fewer than twenty");
    const uint32_t tooMany = static_cast<uint32_t>((kMaxEntryCount + 1) * kEntryStride);
    Check(EntryCountFromLayout(tooMany + kGamertagPastEntries, tooMany + kFocusIndexPastEntries) == 0,
          "more than the most allowed");
    Check(EntryCountFromLayout(0, 0) == 0 && EntryCountFromLayout(4, 0x948) == 0, "below the fields' own offsets");

    // ODST: its own patterns' positions, and its count from its own two fields (the gamertag 0x40 past the array).
    const auto odstCaller = Tokens(kOdstCallerPattern);
    Check(odstCaller[kOdstCallerCallRel - 1] == 0xE8 && Wild(odstCaller, kOdstCallerCallRel, 4),
          "ODST: the caller's call rel32");
    const auto odstCall = Tokens(kOdstProjectionCallPattern);
    Check(odstCall[kOdstProjectionCallRel - 1] == 0xE8 && Wild(odstCall, kOdstProjectionCallRel, 4),
          "ODST: the projection call's rel32");
    Check(odstCall[kOdstFocusIndexDisp - 3] == 0x41 && odstCall[kOdstFocusIndexDisp - 2] == 0x8B &&
              odstCall[kOdstFocusIndexDisp - 1] == 0x85 && Wild(odstCall, kOdstFocusIndexDisp, 4),
          "ODST: the focused index's disp32 (mov eax, [r13 + disp32])");
    const auto odstView = Tokens(kOdstViewReadPattern);
    Check(odstView[0] == 0x48 && odstView[1] == 0x8B && odstView[2] == 0x05 && Wild(odstView, kOdstViewReadDisp, 4) &&
              odstView[15] == static_cast<int>(kViewPositionOffset),
          "ODST: the view's disp32, and the camera at [view] + 8");
    const auto odstRecord = Tokens(kOdstRecordPattern);
    Check(odstRecord[3] + 8 == 0x3C && odstRecord[10] + 8 == static_cast<int>(kPositionOffset),
          "ODST: record at +0x3C, position");
    const auto odstScale = Tokens(kOdstScaleReadPattern);
    Check(odstScale[5] + 8 == static_cast<int>(kScaleOffset), "ODST: the size read is the distance scale");
    const auto odstGamertag = Tokens(kOdstGamertagPattern);
    Check(odstGamertag[0] == 0x4D && odstGamertag[1] == 0x8D && odstGamertag[2] == 0x8D &&
              Wild(odstGamertag, kOdstGamertagDisp, 4),
          "ODST: the gamertag's disp32 (lea r9, [r13 + disp32])");
    Check(EntryCountFromLayout(0x950, 0x948, kOdstGamertagPastEntries) == 20, "ODST's twenty (retail halo3odst.dll)");
    Check(EntryCountFromLayout(0x950, 0x948) == 0, "ODST's gamertag field is not where Halo 3 keeps it");
    Check(kOdstDraw.alphaReadPattern == kAlphaReadPattern && kOdstDraw.stridePattern == kStridePattern &&
              kOdstDraw.gamertagPastEntries == kOdstGamertagPastEntries && kHalo3Draw.gamertagPastEntries == 0xC,
          "each title's profile names its own fields");

    // The census key round-trips, and a teammate with no flags is never an empty slot.
    Check(KindKey(0, 0) != 0, "a kind is never an empty census slot");
    Check(KindIcon(KindKey(-1, 0x10)) == -1 && KindFlags(KindKey(-1, 0x10)) == 0x10, "no icon, speaking");
    Check(KindIcon(KindKey(2, 0xC1)) == 2 && KindFlags(KindKey(2, 0xC1)) == 0xC1, "an objective");

    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
