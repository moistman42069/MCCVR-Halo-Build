// Seats aim in world angles (src/common/seat_aim_frame_logic.h): the jne -> jmp patch builder and its inverse,
// the gate, and the dev frame check's angle maths.
#include "seat_aim_frame_logic.h"
#include <cmath>
#include <cstdio>
#include <cstring>

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

using namespace seat_aim_frame;

void TestSites()
{
    Check(kHalo3Site.jccRva == kHalo3Site.anchorRva + kAnchorToJcc, "Halo 3: the jne is 15 bytes into the anchor");
    Check(kOdstSite.jccRva == kOdstSite.anchorRva + kAnchorToJcc, "ODST: the jne is 15 bytes into the anchor");
    // The anchor must still find the site once our `E9 rel32 90` is in place (the adopt path): its last two
    // tokens, exactly the jne's opcode bytes, are wildcards.
    const size_t len = std::strlen(kAnchorPattern);
    Check(len == (kAnchorToJcc + 2) * 3 - 1 && !std::strcmp(kAnchorPattern + len - 5, "?? ??"),
          "the anchor is kAnchorToJcc bytes + two wildcards over the jne's opcode");
    Check(kHalo3Site.anchorRva > kHalo3Site.rebaseRva && kHalo3Site.skipRva > kHalo3Site.jccRva,
          "Halo 3: the test and its target sit inside the rebase, target after the test");
    Check(kOdstSite.anchorRva > kOdstSite.rebaseRva && kOdstSite.skipRva > kOdstSite.jccRva,
          "ODST: the test and its target sit inside the rebase, target after the test");
    Check(kReachSite.jccRva == kReachSite.anchorRva + kReachSite.anchorToJcc && kReachSite.anchorToJcc == 24,
          "Reach: the jne is 24 bytes into its anchor");
    const size_t reachLen = std::strlen(kReachSite.anchorPattern);
    Check(reachLen == (kReachSite.anchorToJcc + 2) * 3 - 1 &&
              !std::strcmp(kReachSite.anchorPattern + reachLen - 5, "?? ??"),
          "Reach: the anchor is anchorToJcc bytes + two wildcards over the jne's opcode");
    Check(kReachSite.anchorRva > kReachSite.rebaseRva && kReachSite.skipRva > kReachSite.jccRva,
          "Reach: the test and its target sit inside the rebase, target after the test");
    Check(kHalo3Site.anchorPattern == kAnchorPattern && kOdstSite.anchorToJcc == kAnchorToJcc,
          "Halo 3 / ODST keep the shared anchor");
}

void TestPatchBytes()
{
    // The retail bytes: Halo 3 `0F 85 DD 00 00 00` at 0x355EFA -> 0x355FDD; ODST `0F 85 FE 00 00 00` at 0x39A6C4
    // -> 0x39A7C8.
    const uint8_t h3[6]{0x0F, 0x85, 0xDD, 0x00, 0x00, 0x00};
    const uint8_t odst[6]{0x0F, 0x85, 0xFE, 0x00, 0x00, 0x00};
    uint8_t out[6]{}, back[6]{};
    Check(IsStockJcc(h3, kHalo3Site.jccRva, kHalo3Site.skipRva), "Halo 3 retail jne is recognised");
    Check(BuildSkipPatch(h3, kHalo3Site.jccRva, kHalo3Site.skipRva, out), "Halo 3 patch builds");
    const uint8_t h3Want[6]{0xE9, 0xDE, 0x00, 0x00, 0x00, 0x90};
    Check(!std::memcmp(out, h3Want, 6), "Halo 3: E9 rel32+1 90");
    Check(IsSkipPatch(out, kHalo3Site.jccRva, kHalo3Site.skipRva), "Halo 3 patch is recognised as ours");
    Check(BuildStockJcc(out, kHalo3Site.jccRva, kHalo3Site.skipRva, back) && !std::memcmp(back, h3, 6),
          "Halo 3: the inverse gives back the retail bytes");

    Check(BuildSkipPatch(odst, kOdstSite.jccRva, kOdstSite.skipRva, out), "ODST patch builds");
    const uint8_t odstWant[6]{0xE9, 0xFF, 0x00, 0x00, 0x00, 0x90};
    Check(!std::memcmp(out, odstWant, 6), "ODST: E9 rel32+1 90");
    Check(BuildStockJcc(out, kOdstSite.jccRva, kOdstSite.skipRva, back) && !std::memcmp(back, odst, 6),
          "ODST: the inverse gives back the retail bytes");

    // Reach: `0F 85 DD 00 00 00` at 0x48AE8F -> 0x48AF72.
    const uint8_t reach[6]{0x0F, 0x85, 0xDD, 0x00, 0x00, 0x00};
    Check(IsStockJcc(reach, kReachSite.jccRva, kReachSite.skipRva), "Reach retail jne is recognised");
    Check(BuildSkipPatch(reach, kReachSite.jccRva, kReachSite.skipRva, out) && !std::memcmp(out, h3Want, 6),
          "Reach: E9 DE 00 00 00 90");
    Check(BuildStockJcc(out, kReachSite.jccRva, kReachSite.skipRva, back) && !std::memcmp(back, reach, 6),
          "Reach: the inverse gives back the retail bytes");

    // rel32 carry: 0xFF -> 0x100 crosses a byte.
    const uint8_t carry[6]{0x0F, 0x85, 0xFF, 0x00, 0x00, 0x00};
    Check(BuildSkipPatch(carry, 0x1000, 0x1000 + 6 + 0xFF, out) && out[1] == 0x00 && out[2] == 0x01,
          "rel32 + 1 carries into the next byte");
    // A backward target.
    const uint8_t backward[6]{0x0F, 0x85, 0xF0, 0xFF, 0xFF, 0xFF};   // -0x10
    Check(BuildSkipPatch(backward, 0x2000, 0x2000 + 6 - 0x10, out) && out[0] == 0xE9 && out[1] == 0xF1 &&
              out[2] == 0xFF && out[3] == 0xFF && out[4] == 0xFF && out[5] == 0x90,
          "a backward jne becomes a backward jmp");

    // Refusals: anything that is not exactly the stock jne to the expected target.
    uint8_t bad[6];
    std::memcpy(bad, h3, 6);
    Check(!BuildSkipPatch(h3, kHalo3Site.jccRva, kHalo3Site.skipRva + 1, out), "refuses another target");
    Check(!BuildSkipPatch(h3, kHalo3Site.jccRva + 1, kHalo3Site.skipRva, out), "refuses another site");
    bad[1] = 0x84;   // je
    Check(!BuildSkipPatch(bad, kHalo3Site.jccRva, kHalo3Site.skipRva, out), "refuses je");
    std::memcpy(bad, h3, 6);
    bad[0] = 0x90;
    Check(!BuildSkipPatch(bad, kHalo3Site.jccRva, kHalo3Site.skipRva, out), "refuses a non-jcc");
    const uint8_t shortJne[6]{0x75, 0x10, 0x90, 0x90, 0x90, 0x90};
    Check(!BuildSkipPatch(shortJne, 0x100, 0x112, out), "refuses a short jne");
    Check(!BuildSkipPatch(h3Want, kHalo3Site.jccRva, kHalo3Site.skipRva, out),
          "refuses to patch its own patch (that is adopted, not rebuilt)");
    uint8_t notOurs[6];
    std::memcpy(notOurs, h3Want, 6);
    notOurs[5] = 0xCC;
    Check(!IsSkipPatch(notOurs, kHalo3Site.jccRva, kHalo3Site.skipRva) &&
              !BuildStockJcc(notOurs, kHalo3Site.jccRva, kHalo3Site.skipRva, back),
          "a jmp without our nop is not ours");
    std::memcpy(notOurs, h3Want, 6);
    notOurs[1] = 0xDD;
    Check(!IsSkipPatch(notOurs, kHalo3Site.jccRva, kHalo3Site.skipRva), "a jmp elsewhere is not ours");
    // Out of rel32 range.
    int32_t rel = 0;
    Check(!Rel32(0, 6, 0xFFFFFFFFu, rel), "a target beyond rel32 is refused");
}

void TestGate()
{
    Check(Wanted(true, true, true, true, false), "first-person seats on, sampler bound: skipped");
    Check(!Wanted(false, true, true, true, false), "vehicle_first_person off: the game's rebase");
    Check(!Wanted(true, false, true, true, false), "vehicle_hide_body off: the game's rebase");
    Check(!Wanted(true, true, false, true, false), "dev A/B 0: the game's rebase");
    Check(!Wanted(true, true, true, false, false),
          "vehicle sampler not bound (no C20, no seat check): the game's rebase, as stock");
    Check(!Wanted(true, true, true, true, true), "a seat natively lacking bit 4: the game's rebase, as stock");
    // Reach: R-V27's own keys (vehicle_first_person and its camera binding), not vehicle_hide_body.
    Check(ReachWanted(true, true, true, false), "Reach: R-V27 on, binding installed: skipped");
    Check(!ReachWanted(false, true, true, false), "Reach: vehicle_first_person off: the game's rebase");
    Check(!ReachWanted(true, false, true, false), "Reach: dev A/B 0: the game's rebase");
    Check(!ReachWanted(true, true, false, false), "Reach: the camera binding not installed: the game's rebase");
    Check(!ReachWanted(true, true, true, true), "Reach: a seat natively lacking bit 4: the game's rebase (case b)");
}

void TestAngles()
{
    const float pi = 3.14159265f;
    Check(std::fabs(AimSeparationDeg(0.3f, 0.2f, 0.3f, 0.2f)) < 1e-3f, "same aim: 0 deg");
    Check(std::fabs(AimSeparationDeg(0.0f, 0.0f, pi / 2, 0.0f) - 90.0f) < 1e-3f, "quarter turn of yaw: 90 deg");
    Check(std::fabs(AimSeparationDeg(0.0f, 0.0f, 0.0f, 0.1f) - 5.7295779f) < 1e-3f, "0.1 rad of pitch");
    Check(std::fabs(AimSeparationDeg(0.0f, 0.0f, 2 * pi, 0.0f)) < 1e-3f, "yaw wraps");
    // Near the pole yaw barely matters.
    Check(AimSeparationDeg(0.0f, 1.5f, 1.0f, 1.5f) < 5.0f, "near vertical, a yaw difference is small");
    Check(std::isnan(AimSeparationDeg(NAN, 0.0f, 0.0f, 0.0f)), "non-finite input: NaN");
    // Small angles stay accurate (acos would read 0 here).
    Check(std::fabs(AimSeparationDeg(0.0f, 0.0f, 1e-4f, 0.0f) - 1e-4f * 57.2957795f) < 1e-5f, "tiny angles");

    const float level[3]{0.0f, 0.0f, 1.0f};
    const float rolled[3]{std::sin(0.5f), 0.0f, std::cos(0.5f)};
    const float upside[3]{0.0f, 0.0f, -1.0f};
    const float zero[3]{0.0f, 0.0f, 0.0f};
    const float unnormalised[3]{0.0f, 2.0f, 2.0f};
    Check(std::fabs(TiltDeg(level)) < 1e-4f, "level: 0 deg tilt");
    Check(std::fabs(TiltDeg(rolled) - 28.6478898f) < 1e-3f, "0.5 rad roll");
    Check(std::fabs(TiltDeg(upside) - 180.0f) < 1e-3f, "upside down: 180 deg");
    Check(std::fabs(TiltDeg(unnormalised) - 45.0f) < 1e-3f, "length does not matter");
    Check(std::isnan(TiltDeg(zero)), "a zero vector: NaN");
}
} // namespace

int main()
{
    TestSites();
    TestPatchBytes();
    TestGate();
    TestAngles();
    if (failures)
    {
        std::printf("%u of %u checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("PASS: %u seat aim frame checks\n", checks);
    return 0;
}
