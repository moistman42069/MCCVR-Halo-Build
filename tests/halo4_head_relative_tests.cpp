// Halo 4: the motion tracker follows the head (src/common/halo4_head_relative_logic.h). RETAIL-DERIVED LEAD, NOT
// H4EK-VERIFIED: these tests cover the decode of the three sites and its refusals, the proof the hook takes at run
// time before it changes anything (agree, disagree, a reference that did not move, withdrawal), and the player's
// two headset checks on the game's own placement arithmetic.
#include "halo4_head_relative_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
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

using namespace halo4_head_relative;
using head_relative::HeadingHold;
using head_relative::HeadSample;
using head_relative::kDegToRad;
using head_relative::kPi;

// ---- the sites ----------------------------------------------------------------------------------------------------
// A pattern's bytes, wildcards as zero.
std::vector<uint8_t> PatternBytes(const char* pattern)
{
    std::vector<uint8_t> bytes;
    for (const char* at = pattern; *at;)
    {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        if (at[0] == '?')
        {
            bytes.push_back(0);
            while (*at == '?')
                ++at;
            continue;
        }
        char* end = nullptr;
        bytes.push_back(static_cast<uint8_t>(std::strtoul(at, &end, 16)));
        at = end;
    }
    return bytes;
}
void PutU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); }
void PutBranch(std::vector<uint8_t>& bytes, size_t opcode, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(opcode) + 5));
    std::memcpy(bytes.data() + opcode + 1, &rel, 4);
}

// The image as retail halo4.dll lays it out (the offsets from the module base, as measured).
constexpr int64_t kMethodRva = 0x423648, kPlayerBuildRva = 0x4CF7A4, kOtherBuildRva = 0x4CF97C, kSiteRva = 0x4CF841,
                  kBuilderRva = 0x4CFCB4, kHalfPiRva = 0xD957AC;

struct Image
{
    std::vector<uint8_t> method, site, builder;
    uint8_t halfPi[4]{};
};
Image Retail()
{
    Image image;
    image.method = PatternBytes(kTrackerMethodPattern);
    PutBranch(image.method, kMethodJumpOther, kMethodRva, kOtherBuildRva);
    PutBranch(image.method, kMethodCallPlayer, kMethodRva, kPlayerBuildRva);
    image.site = PatternBytes(kTrackerHeadingSitePattern);
    const int32_t halfPiRel = static_cast<int32_t>(kHalfPiRva - (kSiteRva + static_cast<int64_t>(kSiteHalfPiNext)));
    std::memcpy(image.site.data() + kSiteHalfPiDisp, &halfPiRel, 4);
    PutBranch(image.site, kSiteCallBuilder, kSiteRva, kBuilderRva);
    image.builder = PatternBytes(kTrackerBuilderHeadPattern);
    const float value = kHalfPi;
    std::memcpy(image.halfPi, &value, 4);
    return image;
}
TrackerBind Decode(const Image& image, TrackerSites& sites, int64_t siteFunctionRva = kPlayerBuildRva,
                   int64_t builderRva = kBuilderRva, bool readHalfPi = true)
{
    return DecodeTrackerSites(image.method.data(), kMethodRva, image.site.data(), kSiteRva, siteFunctionRva,
                              image.builder.data(), builderRva, readHalfPi ? image.halfPi : nullptr, sites);
}

void TestSites()
{
    Check(PatternBytes(kTrackerMethodPattern).size() == kMethodBytes &&
              PatternBytes(kTrackerHeadingSitePattern).size() == kSiteBytes &&
              PatternBytes(kTrackerBuilderHeadPattern).size() == kBuilderHeadBytes,
          "each pattern has the length the header says");
    Check(kSiteReturn == kSiteCallBuilder + 5 && kSiteReturn < kSiteBytes,
          "the return address is the instruction after the site's call of the builder");

    const Image retail = Retail();
    TrackerSites sites;
    Check(Decode(retail, sites) == TrackerBind::Bound, "retail's layout decodes");
    Check(sites.userFromState == -8, "the component's user index is 8 bytes before the state the builder is handed");
    Check(sites.tlsBlock == 0xB0 && sites.stride == 0x108 && sites.yawOffset == 0x8F8,
          "the yaw's block, stride and offset are read from the site");
    Check(sites.playerBuildRva == kPlayerBuildRva && sites.otherBuildRva == kOtherBuildRva &&
              sites.builderRva == kBuilderRva && sites.returnRva == 0x4CF933 && sites.halfPiRva == kHalfPiRva,
          "the two builds, the hook, the return address and the constant are where the sites point");

    TrackerSites untouched;
    Check(DecodeTrackerSites(nullptr, kMethodRva, retail.site.data(), kSiteRva, kPlayerBuildRva, retail.builder.data(),
                             kBuilderRva, retail.halfPi, untouched) == TrackerBind::MethodMissing,
          "no unique component method: nothing binds");
    Check(DecodeTrackerSites(retail.method.data(), kMethodRva, nullptr, kSiteRva, kPlayerBuildRva,
                             retail.builder.data(), kBuilderRva, retail.halfPi, untouched) == TrackerBind::SiteMissing,
          "no unique heading site: nothing binds");
    Check(DecodeTrackerSites(retail.method.data(), kMethodRva, retail.site.data(), kSiteRva, kPlayerBuildRva, nullptr,
                             kBuilderRva, retail.halfPi, untouched) == TrackerBind::BuilderMissing,
          "no unique builder head: nothing binds");
    Check(Decode(retail, untouched, kOtherBuildRva) == TrackerBind::SiteOutsideBuild &&
              Decode(retail, untouched, -1) == TrackerBind::SiteOutsideBuild,
          "a heading site outside the build the method calls is refused");
    Check(Decode(retail, untouched, kPlayerBuildRva, kBuilderRva + 0x10) == TrackerBind::CallMismatch,
          "a site that calls something else than the matched builder is refused");
    Check(Decode(retail, untouched, kPlayerBuildRva, kBuilderRva, false) == TrackerBind::ConstantMismatch,
          "an unreadable constant is refused");
    Check(untouched.builderRva == 0 && untouched.returnRva == 0 && untouched.userFromState == 0,
          "a refusal fills nothing");

    // The argument: the slot the site writes and the slot the builder reads must both be the eleventh.
    Image odd = retail;
    odd.site[kSiteHeadingSlot] = 0x48;  // the tenth argument's slot
    Check(Decode(odd, untouched) == TrackerBind::NotTheArgument, "a site that writes the heading to another slot is refused");
    odd = retail;
    PutU32(odd.builder, kBuilderHeadingDisp, 0xF8);  // the builder reading its tenth argument there
    Check(Decode(odd, untouched) == TrackerBind::NotTheArgument, "a builder that reads another argument is refused");
    odd = retail;
    PutU32(odd.builder, kBuilderFrameSub, 0x90);  // another frame: rsp + 0x100 is no longer the eleventh
    Check(Decode(odd, untouched) == TrackerBind::NotTheArgument, "a builder with another frame is refused");
    odd = retail;
    odd.method[kMethodStateOther] = 0x70;
    Check(Decode(odd, untouched) == TrackerBind::Implausible, "a method whose two builds get different states is refused");
    odd = retail;
    odd.method[kMethodUserDisp] = 0x71;
    Check(Decode(odd, untouched) == TrackerBind::Implausible, "a user index that is not int-aligned is refused");
    odd = retail;
    const float pi = kPi;
    std::memcpy(odd.halfPi, &pi, 4);
    Check(Decode(odd, untouched) == TrackerBind::ConstantMismatch, "a constant that is not pi/2 is refused");
    bool named = true;
    for (int i = 0; i < static_cast<int>(TrackerBind::Count); ++i)
        named = named && TrackerBindName(static_cast<TrackerBind>(i))[0] != '?';
    for (int i = 0; i < static_cast<int>(TrackerUse::Count); ++i)
        named = named && TrackerUseName(static_cast<TrackerUse>(i))[0] != '?';
    Check(named, "every refusal has a name for the log");
}

// ---- the proof ----------------------------------------------------------------------------------------------------
// Feeds `count` samples: the reference sweeps from `fromDeg` by `stepDeg` a sample, the game's yaw is the
// reference plus `missDeg`. Returns the last step.
ProofStep Feed(ArgumentProof& proof, int count, float fromDeg, float stepDeg, float missDeg, bool usable = true)
{
    ProofStep last = ProofStep::NoReference;
    for (int i = 0; i < count; ++i)
    {
        const float reference = (fromDeg + stepDeg * static_cast<float>(i)) * kDegToRad;
        last = StepProof(proof, WrapYaw(reference + missDeg * kDegToRad), usable, WrapPi(reference));
    }
    return last;
}

void TestProof()
{
    // Agreement across a reference that moved: proven on the sample that completes both conditions.
    ArgumentProof proof;
    Check(Feed(proof, static_cast<int>(kProofSamples) - 1, 100.0f, 0.5f, 0.3f) == ProofStep::Agreed && !proof.proven,
          "29 agreeing samples: not yet");
    Check(StepProof(proof, WrapYaw(115.0f * kDegToRad), true, 115.0f * kDegToRad) == ProofStep::Proven && proof.proven,
          "the 30th, with the reference 15 deg from where it began: proven");
    Check(StepProof(proof, WrapYaw(116.0f * kDegToRad), true, 116.0f * kDegToRad) == ProofStep::Agreed && proof.proven,
          "once proven, an agreement is just an agreement");

    // Thirty samples are not enough while the reference has not moved far enough.
    proof = ArgumentProof{};
    Check(Feed(proof, 200, 40.0f, 0.0f, 0.5f) == ProofStep::Agreed && !proof.proven,
          "a reference that never moved proves nothing, however long it agrees");
    Check(Feed(proof, 1, 51.0f, 0.0f, 0.5f) == ProofStep::Proven,
          "... and the first agreeing sample 11 deg away completes it");

    // A constant in the game against a moving reference: the neighbouring-field case.
    proof = ArgumentProof{};
    bool everProven = false;
    for (int i = 0; i < 2000; ++i)
    {
        const float reference = WrapPi((static_cast<float>(i % 360) - 180.0f) * kDegToRad);
        everProven = everProven || StepProof(proof, WrapYaw(75.0f * kDegToRad), true, reference) == ProofStep::Proven;
    }
    Check(!everProven && !proof.proven, "a constant where the yaw should be never passes, whatever the aim does");
    // An unrelated angle that happens to cross the reference now and then.
    proof = ArgumentProof{};
    everProven = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapPi(static_cast<float>(i) * 0.7f * kDegToRad);
        const float other = WrapYaw(static_cast<float>(i) * -0.4f * kDegToRad + 1.0f);
        everProven = everProven || StepProof(proof, other, true, reference) == ProofStep::Proven;
    }
    Check(!everProven, "an unrelated angle never passes");
    // The mirrored angle.
    proof = ArgumentProof{};
    everProven = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapPi((static_cast<float>(i) * 0.3f - 170.0f) * kDegToRad);
        everProven = everProven || StepProof(proof, WrapYaw(-reference), true, reference) == ProofStep::Proven;
    }
    Check(!everProven, "the mirrored yaw never passes");
    // A constant offset larger than the tolerance.
    proof = ArgumentProof{};
    Check(Feed(proof, 400, 0.0f, 0.5f, 5.0f) == ProofStep::Lagging && !proof.proven && proof.agreed == 0,
          "a yaw 5 deg off the reference never agrees");

    // A disagreement before the proof starts it over.
    proof = ArgumentProof{};
    Feed(proof, 20, 10.0f, 0.5f, 0.0f);
    Check(proof.agreed == 20, "20 agreements counted");
    Check(Feed(proof, 1, 20.0f, 0.0f, 25.0f) == ProofStep::Disagreed && proof.agreed == 0 && !proof.proven,
          "one disagreement before the proof resets the count");
    // A lagging sample (the reference leads the game by a tick while the hand moves) counts for nothing.
    Feed(proof, 10, 10.0f, 0.5f, 0.0f);
    Check(Feed(proof, 1, 15.0f, 0.0f, 6.0f) == ProofStep::Lagging && proof.agreed == 10,
          "a 6 deg miss neither counts nor resets");
    // No reference: nothing changes.
    Check(Feed(proof, 50, 15.0f, 1.0f, 90.0f, false) == ProofStep::NoReference && proof.agreed == 10 && !proof.proven,
          "without a reference nothing is counted for or against");
    Check(StepProof(proof, std::numeric_limits<float>::quiet_NaN(), true, 0.2f) == ProofStep::NoReference &&
              StepProof(proof, 0.2f, true, std::numeric_limits<float>::infinity()) == ProofStep::NoReference &&
              proof.agreed == 10,
          "a non-finite value is no reference");

    // Across the seam at +/-180 deg, and with the game's yaw in [0, 2 pi) against a reference in (-pi, pi].
    proof = ArgumentProof{};
    Check(Feed(proof, static_cast<int>(kProofSamples), 170.0f, 0.5f, -0.4f) == ProofStep::Proven && proof.proven,
          "a sweep through 180 deg is proven like any other");

    // Withdrawal: only a RUN of disagreements, and an agreement in between starts the run again.
    Check(Feed(proof, static_cast<int>(kProofWithdrawRun) - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed && proof.proven,
          "59 disagreements in a row: still proven (a snap turn is one or two)");
    Check(Feed(proof, 1, 0.0f, 0.0f, 0.5f) == ProofStep::Agreed && proof.disagreed == 0, "an agreement clears the run");
    Check(Feed(proof, static_cast<int>(kProofWithdrawRun) - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed && proof.proven,
          "59 more: still proven");
    Check(Feed(proof, 1, 0.0f, 0.0f, 40.0f) == ProofStep::Withdrawn && !proof.proven && proof.agreed == 0,
          "the 60th in a row withdraws it");
    Check(Feed(proof, static_cast<int>(kProofSamples) - 1, 0.0f, 1.0f, 0.0f) == ProofStep::Agreed && !proof.proven &&
              Feed(proof, 1, 29.0f, 0.0f, 0.0f) == ProofStep::Proven,
          "after a withdrawal the whole proof is taken again");
    // Lagging samples and missing references do not feed a withdrawal.
    Check(Feed(proof, 500, 0.0f, 0.2f, 6.0f) == ProofStep::Lagging && proof.proven &&
              Feed(proof, 500, 0.0f, 0.2f, 90.0f, false) == ProofStep::NoReference && proof.proven,
          "lag and a missing reference never withdraw a proof");
}

// ---- the heading --------------------------------------------------------------------------------------------------
void Forward(float yawDeg, float pitchDeg, float out[3])
{
    const float yaw = yawDeg * kDegToRad, pitch = pitchDeg * kDegToRad;
    out[0] = std::cos(pitch) * std::cos(yaw);
    out[1] = std::cos(pitch) * std::sin(yaw);
    out[2] = std::sin(pitch);
}
HeadSample Head(float yawDeg, float pitchDeg = 0.0f, uint64_t stampUs = 5000000, uint32_t generation = 3)
{
    HeadSample sample;
    Forward(yawDeg, pitchDeg, sample.forward);
    sample.up[2] = 1.0f;
    sample.stampUs = stampUs;
    sample.generation = generation;
    sample.valid = true;
    return sample;
}
TrackerRequest Request(int sign = 1, float offsetDeg = 0.0f)
{
    TrackerRequest request;
    request.playerBuild = true;
    request.user = 0;
    request.proven = true;
    request.switchOn = true;
    request.gameplay = true;
    request.generation = 3;
    request.nowUs = 5000000 + 8000;
    request.sign = sign;
    request.offsetDeg = offsetDeg;
    return request;
}
float GameYaw(float yawDeg) { return WrapYaw(yawDeg * kDegToRad); }
// Where the game draws `contact` with the aim at aimYawDeg and the head at headYawDeg: the bearing from the
// tracker's top, positive to its left. `substitute` = this lane handed the builder its heading.
float Drawn(const float contact[3], float aimYawDeg, float headYawDeg, bool substitute = true, int sign = 1,
            float offsetDeg = 0.0f)
{
    float field = FieldFromYaw(GameYaw(aimYawDeg));
    if (substitute)
    {
        HeadingHold hold;
        float replaced = field;
        if (TrackerHeading(Request(sign, offsetDeg), Head(headYawDeg), hold, field, replaced) != TrackerUse::Used)
            return 1.0e9f;
        field = replaced;
    }
    float point[2];
    TrackerPoint(contact, field, point);
    return TrackerPointBearing(point);
}

void TestPlacement()
{
    float point[2];
    const float ahead[3]{5.0f, 0.0f, 0.0f}, right[3]{0.0f, -5.0f, 0.0f}, left[3]{0.0f, 5.0f, 0.0f};
    TrackerPoint(ahead, FieldFromYaw(0.0f), point);
    Check(Near(point[0], 0.0f) && Near(point[1], -5.0f), "facing +X, a contact ahead is at the tracker's top");
    TrackerPoint(right, FieldFromYaw(0.0f), point);
    Check(Near(point[0], 5.0f) && Near(point[1], 0.0f), "facing +X, a contact at -Y (the right) is at the tracker's right");
    TrackerPoint(left, FieldFromYaw(0.0f), point);
    Check(Near(TrackerPointBearing(point), 90.0f * kDegToRad), "a contact to the left has a positive bearing");
    Check(Near(WrapYaw(-0.1f), kTwoPi - 0.1f) && Near(WrapYaw(kTwoPi + 0.25f), 0.25f) && WrapYaw(kTwoPi) < kTwoPi &&
              Near(WrapPi(kPi + 0.2f), -kPi + 0.2f) && Near(WrapPi(-kPi - 0.2f), kPi - 0.2f),
          "angles wrap into [0, 2 pi) and (-pi, pi]");
}

void TestAcceptance()
{
    bool ahead = true;
    for (float headYaw : {0.0f, 45.0f, 170.0f, -120.0f, 359.0f})
    {
        float contact[3];
        Forward(headYaw, 0.0f, contact);
        contact[0] *= 12.0f;
        contact[1] *= 12.0f;
        contact[2] = 4.0f;
        for (float aimYaw : {0.0f, 30.0f, -75.0f, 179.0f, 270.0f})
            ahead = ahead && Near(Drawn(contact, aimYaw, headYaw), 0.0f, 1.0e-3f);
    }
    Check(ahead, "a contact straight ahead of the head is at the tracker's top, whatever the aim");

    const float contact[3]{4.0f, 7.0f, 0.0f};
    const float reference = Drawn(contact, 0.0f, 20.0f);
    bool still = true;
    for (float aimYaw = -175.0f; aimYaw <= 175.0f; aimYaw += 13.0f)
        still = still && Near(WrapPi(Drawn(contact, aimYaw, 20.0f) - reference), 0.0f, 1.0e-3f);
    Check(still, "head still, gun swept through 350 deg: the contact does not move");
    Check(Near(WrapPi(Drawn(contact, 35.0f, 110.0f) - Drawn(contact, 35.0f, 20.0f)), -90.0f * kDegToRad, 1.0e-3f),
          "gun still, head +90 deg: the contact turns -90 deg on the tracker");
    Check(Near(WrapPi(Drawn(contact, 60.0f, 20.0f, false) - Drawn(contact, 0.0f, 20.0f, false)), -60.0f * kDegToRad,
               1.0e-3f) &&
              Near(WrapPi(Drawn(contact, 0.0f, 110.0f, false) - Drawn(contact, 0.0f, 20.0f, false)), 0.0f, 1.0e-3f),
          "control arm: on the game's own tracker a 60 deg gun sweep moves the contact 60 deg and the head moves nothing");
    Check(Near(WrapPi(Drawn(contact, 77.0f, 77.0f) - Drawn(contact, 77.0f, 0.0f, false)), 0.0f, 1.0e-3f),
          "head along the aim: exactly the game's own tracker");
    Check(Near(WrapPi(Drawn(contact, 0.0f, 90.0f, true, -1) - Drawn(contact, 0.0f, 0.0f, true, -1)), 90.0f * kDegToRad,
               1.0e-3f),
          "sign -1: head +90 deg turns the contact +90 deg (mirrored)");
    Check(Near(WrapPi(Drawn(contact, 0.0f, 20.0f, true, 1, 30.0f) - Drawn(contact, 0.0f, 20.0f)), -30.0f * kDegToRad,
               1.0e-3f),
          "an offset of +30 deg turns the tracker's heading 30 deg further left");
}

void TestRefusals()
{
    const float native = FieldFromYaw(GameYaw(40.0f));
    const HeadSample head = Head(130.0f);
    HeadingHold hold;
    float field = 123.0f, headYaw = 0.0f;
    Check(TrackerHeading(Request(), head, hold, native, field, &headYaw) == TrackerUse::Used &&
              Near(field, FieldFromYaw(130.0f * kDegToRad)) && Near(headYaw, 130.0f * kDegToRad),
          "used: the builder is handed the head's yaw plus pi/2");

    const auto refused = [&](TrackerRequest request, HeadSample sample, TrackerUse why, const char* what) {
        HeadingHold fresh;
        float out = 123.0f;
        Check(TrackerHeading(request, sample, fresh, native, out) == why && out == 123.0f, what);
    };
    TrackerRequest request = Request();
    request.playerBuild = false;
    refused(request, head, TrackerUse::OtherCallSite, "the other build's call is forwarded untouched");
    request = Request();
    request.user = 1;
    refused(request, head, TrackerUse::NotLocalUser, "another user's tracker is forwarded untouched");
    request = Request();
    request.proven = false;
    refused(request, head, TrackerUse::Unproven, "until the argument is proven nothing is changed");
    request.switchOn = false;
    refused(request, head, TrackerUse::Unproven, "unproven comes before the switch: the log says what is missing");
    request = Request();
    request.switchOn = false;
    refused(request, head, TrackerUse::SwitchOff, "the switch off: the game's own heading");
    request = Request();
    request.gameplay = false;
    refused(request, head, TrackerUse::NotGameplay, "a cinematic: the game's own heading");
    request = Request();
    request.nowUs = head.stampUs + head_relative::kMaxSampleAgeUs + 1;
    refused(request, head, TrackerUse::Stale, "a head sample older than 100 ms: the game's own heading");
    request = Request();
    request.generation = 4;
    refused(request, head, TrackerUse::OtherGeneration, "a head sample from another load: the game's own heading");
    request = Request();
    request.nowUs = head.stampUs - 1;
    refused(request, head, TrackerUse::FromTheFuture, "a head sample stamped after now: the game's own heading");
    refused(Request(), HeadSample{}, TrackerUse::NoSample, "nothing published yet: the game's own heading");
    HeadSample notLive = head;
    notLive.valid = false;
    refused(Request(), notLive, TrackerUse::NoSample, "the head view was not live in that frame: the game's own heading");
    HeadSample nan = head;
    nan.forward[0] = std::numeric_limits<float>::quiet_NaN();
    refused(Request(), nan, TrackerUse::NoHeading, "a non-finite head: the game's own heading");
    refused(Request(), Head(10.0f, 88.0f), TrackerUse::NoHeading, "looking straight up before any heading was known");

    HeadingHold held;
    float out = 0.0f;
    Check(TrackerHeading(Request(), Head(200.0f, -20.0f), held, native, out) == TrackerUse::Used &&
              Near(out, FieldFromYaw(200.0f * kDegToRad)) &&
              TrackerHeading(Request(), Head(75.0f, -89.0f), held, native, out) == TrackerUse::Used &&
              Near(out, FieldFromYaw(200.0f * kDegToRad)),
          "looking straight down keeps the last heading the head had");
    bool inRange = true;
    for (float headYaw = -720.0f; headYaw <= 720.0f; headYaw += 37.0f)
        for (float offset : {-180.0f, 0.0f, 180.0f})
        {
            HeadingHold each;
            float value = 0.0f;
            inRange = inRange && TrackerHeading(Request(1, offset), Head(headYaw), each, native, value) == TrackerUse::Used &&
                      value >= kHalfPi && value < kHalfPi + kTwoPi;
        }
    Check(inRange, "the handed heading stays in [pi/2, 5 pi/2), the range the game's own yaw gives it");
}
} // namespace

int main()
{
    TestSites();
    TestProof();
    TestPlacement();
    TestAcceptance();
    TestRefusals();
    std::printf("halo 4 head relative tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
