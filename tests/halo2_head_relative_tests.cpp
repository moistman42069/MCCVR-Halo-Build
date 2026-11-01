// Halo 2: the motion tracker follows the head (src/common/halo2_head_relative_logic.h). RETAIL-DERIVED LEAD, NOT
// H2EK-VERIFIED: these tests cover the decode of the four sites and its refusals, the proof the hook takes at run
// time before it changes anything (agree, disagree, a reference that did not move, a half turn, withdrawal), the game's own
// placement arithmetic as read from retail halo2.dll, and the acceptance checks on it (TestAcceptance).
#include "halo2_head_relative_logic.h"

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

using namespace halo2_head_relative;
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
bool Has(const std::vector<uint8_t>& bytes, size_t at, std::initializer_list<uint8_t> expected)
{
    size_t i = at;
    for (uint8_t value : expected)
        if (i >= bytes.size() || bytes[i++] != value)
            return false;
    return true;
}
void PutU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); }
void PutBranch(std::vector<uint8_t>& bytes, size_t opcode, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(opcode) + 5));
    std::memcpy(bytes.data() + opcode + 1, &rel, 4);
}
void PutRip(std::vector<uint8_t>& bytes, size_t disp, size_t next, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(next)));
    std::memcpy(bytes.data() + disp, &rel, 4);
}

// The image as retail halo2.dll lays it out (offsets from the module base).
constexpr int64_t kAccessorRva = 0x6C3940, kRecordSlotRva = 0x15E40D0, kSamplerRva = 0x82FE80,
                  kPlacementRva = 0x83000A, kLoopRva = 0x830C61, kHalfPiRva = 0xC328A4, kMaskRva = 0xC33400,
                  kDataSlotRva = 0x187E3F8, kHudSlotRva = 0x187C5D8, kSinRva = 0x9ED76F, kCosRva = 0x9ED769,
                  kPlayingRva = 0x6A4390, kPlayerRva = 0x69D040, kOriginRva = 0x8F9190, kClassifyRva = 0x830140;
// What the direct drive bound on its own (halo2_direct_drive.inl; halo2_observer_6dof.cpp kHalo2PcStride /
// kHalo2PcDesiredYaw).
constexpr DriveRecord kDrive{kRecordSlotRva, 0xB8, 0x38};

struct Image
{
    std::vector<uint8_t> accessor, sampler, placement, loop;
    uint8_t halfPi[4]{}, mask[4]{};
};
Image Retail()
{
    Image image;
    image.accessor = PatternBytes(kAnglesAccessorPattern);
    PutRip(image.accessor, kAccessorSlotDisp, kAccessorSlotNext, kAccessorRva, kRecordSlotRva);
    image.sampler = PatternBytes(kTrackerSamplerPattern);
    PutRip(image.sampler, kSamplerDataDisp, kSamplerDataNext, kSamplerRva, kDataSlotRva);
    PutBranch(image.sampler, kSamplerCallPlaying, kSamplerRva, kPlayingRva);
    PutBranch(image.sampler, kSamplerCallPlayer, kSamplerRva, kPlayerRva);
    PutBranch(image.sampler, kSamplerCallAngles, kSamplerRva, kAccessorRva);
    PutRip(image.sampler, kSamplerHalfPiDisp, kSamplerHalfPiNext, kSamplerRva, kHalfPiRva);
    PutRip(image.sampler, kSamplerMaskDisp, kSamplerMaskNext, kSamplerRva, kMaskRva);
    PutBranch(image.sampler, kSamplerCallSin, kSamplerRva, kSinRva);
    PutBranch(image.sampler, kSamplerCallCos, kSamplerRva, kCosRva);
    PutRip(image.sampler, kSamplerHudDisp, kSamplerHudNext, kSamplerRva, kHudSlotRva);
    PutBranch(image.sampler, kSamplerCallOrigin, kSamplerRva, kOriginRva);
    image.placement = PatternBytes(kTrackerPlacementPattern);
    PutRip(image.placement, kPlacementHudDisp, kPlacementHudNext, kPlacementRva, kHudSlotRva);
    PutBranch(image.placement, kPlacementCallClassify, kPlacementRva, kClassifyRva);
    image.loop = PatternBytes(kTrackerUpdateLoopPattern);
    PutBranch(image.loop, kLoopCallPlaying, kLoopRva, kPlayingRva);
    PutBranch(image.loop, kLoopCallSampler, kLoopRva, kSamplerRva);
    const float halfPi = kHalfPi;
    const uint32_t mask = 0x80000000u;
    std::memcpy(image.halfPi, &halfPi, 4);
    std::memcpy(image.mask, &mask, 4);
    return image;
}
TrackerBind Decode(const Image& image, TrackerSites& sites, const DriveRecord& drive = kDrive,
                   int64_t placementRva = kPlacementRva, bool readConstants = true)
{
    return DecodeTrackerSites(image.accessor.data(), kAccessorRva, image.sampler.data(), kSamplerRva,
                              image.placement.data(), placementRva, image.loop.data(), kLoopRva,
                              readConstants ? image.halfPi : nullptr, readConstants ? image.mask : nullptr, drive,
                              sites);
}

void TestSites()
{
    const std::vector<uint8_t> accessor = PatternBytes(kAnglesAccessorPattern),
                               sampler = PatternBytes(kTrackerSamplerPattern),
                               placement = PatternBytes(kTrackerPlacementPattern),
                               loop = PatternBytes(kTrackerUpdateLoopPattern);
    Check(accessor.size() == kAccessorBytes && sampler.size() == kSamplerBytes &&
              placement.size() == kPlacementBytes && loop.size() == kLoopBytes,
          "each pattern has the length the header says");
    // The positions beside each pattern land on the bytes their comments name.
    Check(Has(accessor, 3, {0x48, 0x69, 0xC8}) && kAccessorStride == 6 && Has(accessor, 10, {0x48, 0x8B, 0x05}) &&
              kAccessorSlotDisp == 13 && kAccessorSlotNext == kAccessorSlotDisp + 4 &&
              Has(accessor, 17, {0x48, 0x83, 0xC0}) && kAccessorYaw == 20 && Has(accessor, 21, {0x48, 0x03, 0xC1, 0xC3}),
          "the accessor's stride, slot and yaw offset are where the header says");
    Check(Has(sampler, 22, {0x48, 0x8B, 0x05}) && kSamplerDataDisp == 25 && kSamplerDataNext == 29 &&
              Has(sampler, 29, {0x48, 0x63, 0xE9}) && Has(sampler, 32, {0x4C, 0x69, 0xF5}) && kSamplerUserBytes == 35 &&
              Has(sampler, 44, {0x48, 0x63, 0x80}) && kSamplerCursor == 47 && Has(sampler, 51, {0x4C, 0x6B, 0xF8}) &&
              kSamplerSlotBytes == 54,
          "the sampler's data pointer, user stride, cursor and slot size are where the header says");
    Check(sampler[kSamplerCallPlaying] == 0xE8 && sampler[kSamplerCallPlayer] == 0xE8 &&
              sampler[kSamplerCallAngles] == 0xE8 && sampler[kSamplerCallSin] == 0xE8 &&
              sampler[kSamplerCallCos] == 0xE8 && sampler[kSamplerCallOrigin] == 0xE8 &&
              Has(sampler, kSamplerExitJump, {0x0F, 0x84}),
          "the sampler's six calls and its exit jump are where the header says");
    Check(kSamplerReturn == kSamplerCallAngles + 5 && Has(sampler, kSamplerCallAngles - 28, {0x8B, 0xCD}) &&
              Has(sampler, kSamplerReturn, {0x0F, 0x57, 0xF6, 0xF3, 0x0F, 0x10, 0x00, 0xF3, 0x0F, 0x58, 0x05}) &&
              kSamplerHalfPiDisp == kSamplerReturn + 11 && kSamplerHalfPiNext == kSamplerHalfPiDisp + 4 &&
              Has(sampler, kSamplerHalfPiNext, {0x0F, 0x57, 0x05}) && kSamplerMaskDisp == kSamplerHalfPiNext + 3 &&
              kSamplerMaskNext == kSamplerMaskDisp + 4,
          "the user goes to the accessor in ecx; its pointer is read once (movss xmm0, [rax]), pi/2 added, negated");
    Check(Has(sampler, kSamplerMaskNext, {0xF3, 0x0F, 0x5A, 0xF0, 0x0F, 0x28, 0xC6, 0xE8}) &&
              Has(sampler, kSamplerCallSin + 5, {0x45, 0x0F, 0x57, 0xD2, 0xF2, 0x44, 0x0F, 0x5A, 0xD0, 0x0F, 0x28, 0xC6, 0xE8}) &&
              kSamplerCallCos == kSamplerCallSin + 17 && Has(sampler, 251, {0xF2, 0x44, 0x0F, 0x5A, 0xD8}),
          "the first call's answer goes to xmm10 (S) and the second's to xmm11 (C), both of the same angle");
    Check(Has(sampler, 224, {0x48, 0x8B, 0x05}) && kSamplerHudDisp == 227 && kSamplerHudNext == 231 &&
              Has(sampler, 256, {0xF3, 0x44, 0x0F, 0x5E, 0x88}) && kSamplerRange == 261,
          "the sampler divides one by the range it reads through the HUD globals pointer");
    Check(Has(placement, 0, {0xF3, 0x0F, 0x10, 0x66, 0x30, 0xF3, 0x0F, 0x10, 0x6E, 0x34}) &&
              Has(placement, 22, {0x48, 0x8B, 0x05}) && kPlacementHudDisp == 25 && kPlacementHudNext == 29 &&
              Has(placement, 32, {0xF3, 0x0F, 0x10, 0x90}) && kPlacementRange == 36 &&
              placement[kPlacementCallClassify] == 0xE8 && kPlacementCallClassify + 5 == kPlacementBytes,
          "the placement reads the contact's centre, the range, and ends at its classify call");
    Check(Has(placement, 199, {0x41, 0x88, 0x00}) && Has(placement, 206, {0x41, 0x88, 0x40, 0x01}),
          "the placement stores x and y as two bytes of the blip");
    Check(Has(loop, 0, {0x8B, 0x81}) && kLoopCursorRead == 2 && Has(loop, 10, {0xB8}) && kLoopLastSlot == 11 &&
              Has(loop, 19, {0x89, 0x81}) && kLoopCursorWrite == 21 && loop[kLoopCallPlaying] == 0xE8 &&
              loop[kLoopCallSampler] == 0xE8 && Has(loop, 51, {0x83, 0xFF}) && kLoopUsers == 53,
          "the loop's cursor, ring size, two calls and user count are where the header says");

    const Image retail = Retail();
    TrackerSites sites;
    Check(Decode(retail, sites) == TrackerBind::Bound, "retail's layout decodes");
    Check(sites.accessorRva == kAccessorRva && sites.returnRva == 0x82FF2D && sites.samplerRva == kSamplerRva &&
              sites.placementRva == kPlacementRva && sites.updateLoopRva == kLoopRva,
          "the hook, the one return address that is changed, the sampler, the placement and the loop");
    Check(sites.recordSlotRva == kRecordSlotRva && sites.recordStride == 0xB8 && sites.yawOffset == 0x38,
          "the record is read from the accessor: slot, stride, yaw offset");
    Check(sites.dataSlotRva == kDataSlotRva && sites.userBytes == 0x2F0 && sites.cursorOffset == 0xBC0 &&
              sites.slotBytes == 0x44 && sites.ringSlots == 10 && sites.users == 4,
          "the tracker's state: four users of 0x2F0, a ring of ten 0x44-byte slots, the cursor after the users");
    Check(sites.hudGlobalsSlotRva == kHudSlotRva && sites.rangeOffset == 0x290 && sites.playingRva == kPlayingRva &&
              sites.halfPiRva == kHalfPiRva && sites.signMaskRva == kMaskRva && sites.sinRva == kSinRva &&
              sites.cosRva == kCosRva && sites.originRva == kOriginRva && sites.samplerExitRva == 0x830126,
          "the range, the user test, the constants, the two trig calls, the origin call and the sampler's exit");

    TrackerSites untouched;
    Check(DecodeTrackerSites(nullptr, kAccessorRva, retail.sampler.data(), kSamplerRva, retail.placement.data(),
                             kPlacementRva, retail.loop.data(), kLoopRva, retail.halfPi, retail.mask, kDrive,
                             untouched) == TrackerBind::AccessorMissing,
          "no unique accessor: nothing binds");
    Check(DecodeTrackerSites(retail.accessor.data(), kAccessorRva, nullptr, kSamplerRva, retail.placement.data(),
                             kPlacementRva, retail.loop.data(), kLoopRva, retail.halfPi, retail.mask, kDrive,
                             untouched) == TrackerBind::SamplerMissing,
          "no unique sampler: nothing binds");
    Check(DecodeTrackerSites(retail.accessor.data(), kAccessorRva, retail.sampler.data(), kSamplerRva, nullptr,
                             kPlacementRva, retail.loop.data(), kLoopRva, retail.halfPi, retail.mask, kDrive,
                             untouched) == TrackerBind::PlacementMissing,
          "no unique placement: nothing binds");
    Check(DecodeTrackerSites(retail.accessor.data(), kAccessorRva, retail.sampler.data(), kSamplerRva,
                             retail.placement.data(), kPlacementRva, nullptr, kLoopRva, retail.halfPi, retail.mask,
                             kDrive, untouched) == TrackerBind::LoopMissing,
          "no unique update loop: nothing binds");
    Image odd = retail;
    PutBranch(odd.sampler, kSamplerCallAngles, kSamplerRva, kAccessorRva + 0x20);
    Check(Decode(odd, untouched) == TrackerBind::CallMismatch,
          "a sampler that asks another function for the angles is refused");
    Check(Decode(retail, untouched, kDrive, kSamplerRva + static_cast<int64_t>(kSamplerBytes) - 1) ==
                  TrackerBind::PlacementOutside &&
              Decode(retail, untouched, kDrive, 0x830126 - static_cast<int64_t>(kPlacementBytes) + 1) ==
                  TrackerBind::PlacementOutside &&
              Decode(retail, untouched, kDrive, kSamplerRva - 0x400) == TrackerBind::PlacementOutside,
          "placement arithmetic before the trig, past the sampler's exit or in another function is refused");
    odd = retail;
    PutBranch(odd.loop, kLoopCallSampler, kLoopRva, kSamplerRva + 0x10);
    Check(Decode(odd, untouched) == TrackerBind::LoopMismatch, "a loop that calls something else is refused");
    odd = retail;
    PutBranch(odd.loop, kLoopCallPlaying, kLoopRva, kPlayerRva);
    Check(Decode(odd, untouched) == TrackerBind::LoopMismatch,
          "a loop that tests its user with another function than the sampler does is refused");
    odd = retail;
    PutU32(odd.loop, kLoopCursorWrite, 0xBC4);
    Check(Decode(odd, untouched) == TrackerBind::LoopMismatch,
          "a loop that steps another field than the sampler's cursor is refused");
    odd = retail;
    PutRip(odd.placement, kPlacementHudDisp, kPlacementHudNext, kPlacementRva, kHudSlotRva + 8);
    Check(Decode(odd, untouched) == TrackerBind::RangeMismatch,
          "a clip that reads its range through another pointer than the pair's is refused");
    odd = retail;
    PutU32(odd.placement, kPlacementRange, 0x294);
    Check(Decode(odd, untouched) == TrackerBind::RangeMismatch,
          "a clip on the neighbouring field (the velocity threshold) is refused");
    Check(Decode(retail, untouched, kDrive, kPlacementRva, false) == TrackerBind::ConstantMismatch,
          "unreadable constants are refused");
    odd = retail;
    const float pi = kPi;
    std::memcpy(odd.halfPi, &pi, 4);
    Check(Decode(odd, untouched) == TrackerBind::ConstantMismatch, "a constant that is not pi/2 is refused");
    odd = retail;
    const uint32_t absMask = 0x7FFFFFFFu;
    std::memcpy(odd.mask, &absMask, 4);
    Check(Decode(odd, untouched) == TrackerBind::ConstantMismatch, "a mask that does not negate is refused");
    // The record: every way the accessor could hand out something else than what the direct drive writes.
    Check(Decode(retail, untouched, DriveRecord{kRecordSlotRva + 8, 0xB8, 0x38}) == TrackerBind::RecordMismatch,
          "another globals pointer than the direct drive's is refused");
    Check(Decode(retail, untouched, DriveRecord{kRecordSlotRva, 0xC0, 0x38}) == TrackerBind::RecordMismatch,
          "another stride than the direct drive's is refused");
    Check(Decode(retail, untouched, DriveRecord{kRecordSlotRva, 0xB8, 0x3C}) == TrackerBind::RecordMismatch,
          "the neighbouring field (the pitch) is refused");
    Check(Decode(retail, untouched, DriveRecord{}) == TrackerBind::RecordMismatch,
          "no direct-drive binding to hold the accessor against: refused");
    odd = retail;
    odd.accessor[kAccessorYaw] = 0x3A;
    Check(Decode(odd, untouched, DriveRecord{kRecordSlotRva, 0xB8, 0x3A}) == TrackerBind::Implausible,
          "a yaw offset that is not float-aligned is refused");
    odd = retail;
    PutU32(odd.loop, kLoopLastSlot, 99);
    Check(Decode(odd, untouched) == TrackerBind::Implausible, "a ring that does not fit in one user's state is refused");
    Check(untouched.accessorRva == 0 && untouched.returnRva == 0 && untouched.recordSlotRva == 0,
          "a refusal fills nothing");
    bool named = true;
    for (int i = 0; i < static_cast<int>(TrackerBind::Count); ++i)
        named = named && TrackerBindName(static_cast<TrackerBind>(i))[0] != '?';
    for (int i = 0; i < static_cast<int>(TrackerUse::Count); ++i)
        named = named && TrackerUseName(static_cast<TrackerUse>(i))[0] != '?';
    Check(named, "every refusal has a name for the log");
    // THE PRIVATE COPY the sampler is handed.
    Check(ShadowBytes(0xB8, 0x38) == kShadowBytes, "retail's record has 0x80 bytes behind the yaw: the whole copy");
    Check(ShadowBytes(0x40, 0x38) == 8 && ShadowBytes(0x38, 0x38) == 8 && ShadowBytes(0, 0x38) == 8,
          "never less than the pair itself");
    Check(ShadowBytes(0x4E, 0x38) == 20, "otherwise what lies inside the record, in whole floats");
    {
        float record[10]{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
        float shadow[8]{}, handed[8]{};
        FillShadow(shadow, handed, record, kShadowBytes, 0.25f);
        bool same = shadow[0] == 0.25f;
        for (int i = 1; i < 8; ++i)
            same = same && shadow[i] == record[i];
        Check(same, "the copy is the record's bytes with only the yaw replaced: a read past the pair is true");
        Check(record[0] == 1.0f, "the record itself is not written");
        Check(!ShadowWritten(shadow, handed, kShadowBytes), "untouched, it is what was handed");
        shadow[5] = 6.5f;
        Check(ShadowWritten(shadow, handed, kShadowBytes), "a write anywhere in it is noticed");
        Check(!ShadowWritten(shadow, handed, 8), "only as far as the copy that was handed reaches");
        shadow[5] = 6.0f;
        shadow[0] = 0.5f;
        Check(ShadowWritten(shadow, handed, 8), "a write to the yaw itself is noticed");
        float nan[8]{}, nanHanded[8]{};
        record[1] = std::numeric_limits<float>::quiet_NaN();
        FillShadow(nan, nanHanded, record, kShadowBytes, 0.25f);
        Check(!ShadowWritten(nan, nanHanded, kShadowBytes), "a NaN in the record is not mistaken for a write");
        Check(!ShadowWritten(shadow, handed, 0), "nothing handed: nothing to compare");
    }
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
        last = StepProof(proof, WrapYaw(reference + missDeg * kDegToRad), usable, WrapYaw(reference));
    }
    return last;
}

void TestProof()
{
    // Agreement across a reference that moved: proven on the sample that completes both conditions.
    ArgumentProof proof;
    Check(Feed(proof, static_cast<int>(kProofSamples) - 1, 100.0f, 0.5f, 0.0f) == ProofStep::Agreed && !proof.proven,
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

    // A HALF TURN IS NOT MOVEMENT. A reference can jump exactly half a turn in one step (a simulated runtime's
    // untouched controller starts half a turn from the head, and its first pose is that jump): no sample lies
    // in between.
    proof = ArgumentProof{};
    Check(Feed(proof, 200, 30.0f, 0.0f, 0.0f) == ProofStep::Agreed && Feed(proof, 200, 210.0f, 0.0f, 0.0f) == ProofStep::Agreed &&
              !proof.proven && proof.agreed == 400 && !proof.moved,
          "an exact half turn of the reference proves nothing, however long it agrees on both sides");
    Check(Feed(proof, 1, 230.0f, 0.0f, 0.0f) == ProofStep::Proven,
          "... and the next agreeing sample 20 deg on (160 from the first) completes it");
    // The band's two edges, each from a fresh count: 10 deg from the first reference, and 10 deg short of half a turn.
    for (const float awayDeg : {9.0f, -9.0f, 171.0f, -171.0f, 180.0f})
    {
        proof = ArgumentProof{};
        Feed(proof, 40, 50.0f, 0.0f, 0.0f);
        Check(Feed(proof, 1, 50.0f + awayDeg, 0.0f, 0.0f) == ProofStep::Agreed && !proof.proven,
              "a reference under 10 deg from the first, or within 10 deg of half a turn from it, is not movement");
    }
    for (const float awayDeg : {11.0f, -11.0f, 90.0f, -90.0f, 169.0f, -169.0f})
    {
        proof = ArgumentProof{};
        Feed(proof, 40, 50.0f, 0.0f, 0.0f);
        Check(Feed(proof, 1, 50.0f + awayDeg, 0.0f, 0.0f) == ProofStep::Proven,
              "a reference between 10 and 170 deg from the first, either way round, is movement");
    }
    // A mirrored yaw (2c - yaw, c = 30 deg) agrees at exactly two headings, c and c + 180. Jumping between them,
    // with no sample in between, would span any spread; it has no sample inside the band.
    proof = ArgumentProof{};
    bool mirrorProven = false;
    for (int round = 0; round < 50; ++round)
        for (const float referenceDeg : {30.0f, 210.0f})
            for (int i = 0; i < 40; ++i)
            {
                const float reference = WrapYaw(referenceDeg * kDegToRad);
                mirrorProven = mirrorProven ||
                    StepProof(proof, WrapYaw(60.0f * kDegToRad - reference), true, reference) == ProofStep::Proven;
            }
    Check(!mirrorProven && !proof.proven && proof.agreed == 4000,
          "a mirrored yaw resting on its two agreeing headings, half a turn apart, never passes");
    Check(StepProof(proof, WrapYaw((60.0f - 230.0f) * kDegToRad), true, WrapYaw(230.0f * kDegToRad)) == ProofStep::Disagreed &&
              proof.agreed == 0,
          "... and the first reference off those two headings is a disagreement that starts the count again");

    // A constant in the game against a moving reference: the neighbouring-field case.
    proof = ArgumentProof{};
    bool everProven = false;
    for (int i = 0; i < 2000; ++i)
    {
        const float reference = WrapYaw(static_cast<float>(i % 360) * kDegToRad);
        everProven = everProven || StepProof(proof, WrapYaw(75.0f * kDegToRad), true, reference) == ProofStep::Proven;
    }
    Check(!everProven && !proof.proven, "a constant where the yaw should be never passes, whatever the aim does");
    // The pitch next door: small, around zero, while the yaw sweeps.
    proof = ArgumentProof{};
    everProven = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapYaw(static_cast<float>(i) * 0.7f * kDegToRad);
        const float pitch = 0.2f * std::sin(static_cast<float>(i) * 0.05f);
        everProven = everProven || StepProof(proof, pitch, true, reference) == ProofStep::Proven;
    }
    Check(!everProven, "the pitch (the float after the yaw) never passes");
    // Another user's yaw: an unrelated angle that happens to cross the reference now and then.
    proof = ArgumentProof{};
    everProven = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapYaw(static_cast<float>(i) * 0.7f * kDegToRad);
        const float other = WrapYaw(static_cast<float>(i) * -0.4f * kDegToRad + 1.0f);
        everProven = everProven || StepProof(proof, other, true, reference) == ProofStep::Proven;
    }
    Check(!everProven, "an unrelated angle never passes");
    // The mirrored angle.
    proof = ArgumentProof{};
    everProven = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapYaw((static_cast<float>(i) * 0.3f + 10.0f) * kDegToRad);
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
    Feed(proof, 10, 10.0f, 0.5f, 0.0f);
    Check(Feed(proof, 1, 15.0f, 0.0f, 6.0f) == ProofStep::Lagging && proof.agreed == 10,
          "a 6 deg miss neither counts nor resets");
    Check(Feed(proof, 50, 15.0f, 1.0f, 90.0f, false) == ProofStep::NoReference && proof.agreed == 10 && !proof.proven,
          "without a reference nothing is counted for or against");
    Check(StepProof(proof, std::numeric_limits<float>::quiet_NaN(), true, 0.2f) == ProofStep::NoReference &&
              StepProof(proof, 0.2f, true, std::numeric_limits<float>::infinity()) == ProofStep::NoReference &&
              proof.agreed == 10,
          "a non-finite value is no reference");

    // Across the record's own seam: the yaw wraps from just under 2 pi to 0.
    proof = ArgumentProof{};
    Check(Feed(proof, static_cast<int>(kProofSamples), 350.0f, 0.5f, 0.0f) == ProofStep::Proven && proof.proven,
          "a sweep through the record's wrap at 2 pi is proven like any other");

    // Withdrawal: only a RUN of disagreements, and an agreement in between starts the run again.
    Check(Feed(proof, static_cast<int>(kProofWithdrawRun) - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed && proof.proven,
          "59 disagreements in a row: still proven");
    Check(Feed(proof, 1, 0.0f, 0.0f, 0.5f) == ProofStep::Agreed && proof.disagreed == 0, "an agreement clears the run");
    Check(Feed(proof, static_cast<int>(kProofWithdrawRun) - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed && proof.proven,
          "59 more: still proven");
    Check(Feed(proof, 1, 0.0f, 0.0f, 40.0f) == ProofStep::Withdrawn && !proof.proven && proof.agreed == 0,
          "the 60th in a row withdraws it");
    Check(Feed(proof, static_cast<int>(kProofSamples) - 1, 0.0f, 1.0f, 0.0f) == ProofStep::Agreed && !proof.proven &&
              Feed(proof, 1, 29.0f, 0.0f, 0.0f) == ProofStep::Proven,
          "after a withdrawal the whole proof is taken again");
    Check(Feed(proof, 500, 0.0f, 0.2f, 6.0f) == ProofStep::Lagging && proof.proven &&
              Feed(proof, 500, 0.0f, 0.2f, 90.0f, false) == ProofStep::NoReference && proof.proven,
          "lag and a missing reference never withdraw a proof");

    Check(ControlFresh(1000, 1000) && ControlFresh(1000, 1000 + kControlFreshMs) &&
              !ControlFresh(1000, 1001 + kControlFreshMs) && !ControlFresh(0, 50) && !ControlFresh(1000, 999),
          "the control update counts as running for 120 ms after its stamp, never without one or before it");
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
    request.user = 0;
    request.proven = true;
    request.switchOn = true;
    request.controlFresh = true;
    request.generation = 3;
    request.nowUs = 5000000 + 8000;
    request.sign = sign;
    request.offsetDeg = offsetDeg;
    return request;
}
float GameYaw(float yawDeg) { return WrapYaw(yawDeg * kDegToRad); }
// The retail maps' hud_globals carry this range (world units; tag data, read from the maps, not a constant of the
// lane: the sampler reads it through the HUD globals pointer).
constexpr float kRange = 8.0f;
// Where the game draws `contact` with the aim at aimYawDeg and the head at headYawDeg: the bearing from the
// tracker's top, positive to its left. `substitute` = this lane handed the sampler its yaw.
float Drawn(const float contact[3], float aimYawDeg, float headYawDeg, bool substitute = true, int sign = 1,
            float offsetDeg = 0.0f)
{
    float yaw = GameYaw(aimYawDeg);
    if (substitute)
    {
        HeadingHold hold;
        float replaced = yaw;
        if (TrackerYaw(Request(sign, offsetDeg), Head(headYawDeg), hold, yaw, replaced) != TrackerUse::Used)
            return 1.0e9f;
        yaw = replaced;
    }
    float point[2];
    TrackerPoint(contact, yaw, kRange, point);
    return TrackerPointBearing(point);
}

void TestPlacement()
{
    float s = 0.0f, c = 0.0f;
    NativePair(0.0f, s, c);
    Check(Near(s, -1.0f) && Near(c, 0.0f), "facing +X the pair is S = -cos(yaw) = -1, C = -sin(yaw) = 0");
    NativePair(kHalfPi, s, c);
    Check(Near(s, 0.0f) && Near(c, -1.0f), "facing +Y the pair is S = 0, C = -1");

    float point[2];
    const float ahead[3]{4.0f, 0.0f, 0.0f}, right[3]{0.0f, -4.0f, 0.0f}, left[3]{0.0f, 4.0f, 0.0f};
    TrackerPoint(ahead, 0.0f, kRange, point);
    Check(Near(point[0], 64.0f) && Near(point[1], 32.0f),
          "facing +X, a contact half the range ahead is half way from the centre to the tracker's top");
    TrackerPoint(right, 0.0f, kRange, point);
    Check(Near(point[0], 96.0f) && Near(point[1], 64.0f), "facing +X, a contact at -Y (the right) is at the tracker's right");
    TrackerPoint(left, 0.0f, kRange, point);
    Check(Near(TrackerPointBearing(point), 90.0f * kDegToRad), "a contact to the left has a positive bearing");

    uint8_t blip[2]{};
    Check(TrackerBlip(ahead, 0.0f, kRange, blip) && blip[0] == 64 && blip[1] == 32,
          "the stored bytes are the point, truncated");
    const float outside[3]{8.0f, 0.0f, 0.0f}, inside[3]{7.99f, 0.0f, 0.0f};
    Check(!TrackerBlip(outside, 0.0f, kRange, blip) && TrackerBlip(inside, 0.0f, kRange, blip) && blip[1] == 0,
          "a contact at the range or beyond is skipped; just inside it sits at the rim");
    const float corner[3]{-5.6f, 5.6f, 0.0f};
    Check(TrackerBlip(corner, 0.0f, kRange, blip) && blip[0] >= 19 && blip[0] <= 20 && blip[1] >= 108 && blip[1] <= 109,
          "behind and to the left: left of the centre and below it");
    // On Halo 2's numbers (the Halo 3 family has the same question): how far a 0.5 m offset between the point the
    // tracker measures from and the body shows. 0.5 m is 0.164 world units; at the retail range of 8 that is 1.3
    // of the 64 units from the centre to the rim.
    const float half[3]{-0.5f / 3.048f, 0.0f, 0.0f};
    TrackerPoint(half, 0.0f, kRange, point);
    Check(Near(point[1] - 64.0f, 1.3123f, 2.0e-3f) && TrackerBlip(half, 0.0f, kRange, blip) && blip[0] == 64 && blip[1] == 65,
          "a body 0.5 m behind the origin is drawn 1.3 units below the centre: one step of the stored byte");

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
        contact[0] *= 5.0f;
        contact[1] *= 5.0f;
        contact[2] = 4.0f;
        for (float aimYaw : {0.0f, 30.0f, -75.0f, 179.0f, 270.0f})
            ahead = ahead && Near(Drawn(contact, aimYaw, headYaw), 0.0f, 1.0e-3f);
    }
    Check(ahead, "a contact straight ahead of the head is at the tracker's top, whatever the aim");

    const float contact[3]{2.0f, 3.5f, 0.0f};
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
    // A simulated runtime's untouched controller points exactly half a turn from the head. That is a size, not a fault:
    // the head's heading is handed whichever way round the half turn is taken, and just either side of it.
    bool opposite = true;
    for (float headYaw : {-69.7f, 0.0f, 135.0f})
        for (float apart : {180.0f, 179.99f, -179.99f})
        {
            HeadingHold hold;
            float yaw = GameYaw(headYaw + apart);
            opposite = opposite && TrackerYaw(Request(), Head(headYaw), hold, yaw, yaw) == TrackerUse::Used &&
                       yaw >= 0.0f && yaw < kTwoPi && Near(WrapPi(yaw - headYaw * kDegToRad), 0.0f, 1.0e-3f);
        }
    Check(opposite, "the aim exactly half a turn from the head (the simulator before its poses are set): the head's "
                    "heading is handed, with no refusal");
    // The stored bytes move the same way: a contact 4 units ahead of the head sits above the centre whatever the aim.
    bool bytes = true;
    for (float aimYaw : {0.0f, 90.0f, 200.0f})
    {
        HeadingHold hold;
        float yaw = GameYaw(aimYaw);
        const float four[3]{0.0f, 4.0f, 0.0f};
        uint8_t blip[2]{};
        bytes = bytes && TrackerYaw(Request(), Head(90.0f), hold, yaw, yaw) == TrackerUse::Used &&
                TrackerBlip(four, yaw, kRange, blip) && blip[0] >= 63 && blip[0] <= 64 && blip[1] >= 31 && blip[1] <= 32;
    }
    Check(bytes, "in the stored bytes too: a contact 4 units ahead of the head is half way up, whatever the aim");
}

void TestRefusals()
{
    const float native = GameYaw(40.0f);
    const HeadSample head = Head(130.0f);
    HeadingHold hold;
    float yaw = 123.0f, headYaw = 0.0f;
    Check(TrackerYaw(Request(), head, hold, native, yaw, &headYaw) == TrackerUse::Used &&
              Near(yaw, 130.0f * kDegToRad) && Near(headYaw, 130.0f * kDegToRad),
          "used: the sampler reads the head's yaw, in the record's convention");

    const auto refused = [&](TrackerRequest request, HeadSample sample, TrackerUse why, const char* what) {
        HeadingHold fresh;
        float out = 123.0f;
        Check(TrackerYaw(request, sample, fresh, native, out) == why && out == 123.0f, what);
    };
    TrackerRequest request = Request();
    request.user = 1;
    refused(request, head, TrackerUse::NotLocalUser, "another local user's tracker reads the game's own record");
    request = Request();
    request.user = -1;
    refused(request, head, TrackerUse::NotLocalUser, "no user: the game's own record");
    request = Request();
    request.proven = false;
    refused(request, head, TrackerUse::Unproven, "until the float is proven nothing is changed");
    request.switchOn = false;
    refused(request, head, TrackerUse::Unproven, "unproven comes before the switch: the log says what is missing");
    request = Request();
    request.switchOn = false;
    refused(request, head, TrackerUse::SwitchOff, "the switch off: the game's own heading");
    request = Request();
    request.controlFresh = false;
    refused(request, head, TrackerUse::ControlNotRunning,
            "the control update not running (a cinematic holds the player): the game's own heading");
    request = Request();
    request.nowUs = head.stampUs + head_relative::kMaxSampleAgeUs + 1;
    refused(request, head, TrackerUse::Stale, "a head sample older than 100 ms: the game's own heading");
    request = Request();
    request.generation = 4;
    refused(request, head, TrackerUse::OtherGeneration, "a head sample from another load: the game's own heading");
    request = Request();
    request.generation = 0;
    refused(request, head, TrackerUse::OtherGeneration, "a hook that is not this generation's: the game's own heading");
    request = Request();
    request.nowUs = head.stampUs - 1;
    refused(request, head, TrackerUse::FromTheFuture, "a head sample stamped after now: the game's own heading");
    refused(Request(), HeadSample{}, TrackerUse::NoSample, "nothing published yet: the game's own heading");
    HeadSample notLive = head;
    notLive.valid = false;
    refused(Request(), notLive, TrackerUse::NoSample, "the head pose was not live in that frame: the game's own heading");
    HeadSample nan = head;
    nan.forward[0] = std::numeric_limits<float>::quiet_NaN();
    refused(Request(), nan, TrackerUse::NoHeading, "a non-finite head: the game's own heading");
    refused(Request(), Head(10.0f, 88.0f), TrackerUse::NoHeading, "looking straight up before any heading was known");
    {
        HeadingHold fresh;
        float out = 123.0f;
        Check(TrackerYaw(Request(), head, fresh, std::numeric_limits<float>::quiet_NaN(), out) == TrackerUse::NoHeading &&
                  out == 123.0f,
              "a non-finite record yaw: nothing is handed");
    }

    HeadingHold held;
    float out = 0.0f;
    Check(TrackerYaw(Request(), Head(200.0f, -20.0f), held, native, out) == TrackerUse::Used &&
              Near(out, 200.0f * kDegToRad) &&
              TrackerYaw(Request(), Head(75.0f, -89.0f), held, native, out) == TrackerUse::Used &&
              Near(out, 200.0f * kDegToRad),
          "looking straight down keeps the last heading the head had");
    bool inRange = true;
    for (float headYawDeg = -720.0f; headYawDeg <= 720.0f; headYawDeg += 37.0f)
        for (float offset : {-180.0f, 0.0f, 180.0f})
        {
            HeadingHold each;
            float value = -1.0f;
            inRange = inRange && TrackerYaw(Request(1, offset), Head(headYawDeg), each, native, value) == TrackerUse::Used &&
                      value >= 0.0f && value < kTwoPi;
        }
    Check(inRange, "the handed yaw stays in [0, 2 pi), the range the game's own accumulator keeps the record in");
    // Pitch and roll of the head never reach the tracker: only its heading.
    HeadingHold tilted;
    HeadSample rolled = Head(60.0f, 35.0f);
    rolled.up[0] = 0.5f;
    rolled.up[1] = -0.3f;
    rolled.up[2] = 0.8f;
    Check(TrackerYaw(Request(), rolled, tilted, native, out) == TrackerUse::Used && Near(out, 60.0f * kDegToRad),
          "head pitch and roll are ignored: the heading alone is handed");
}
} // namespace

int main()
{
    TestSites();
    TestProof();
    TestPlacement();
    TestAcceptance();
    TestRefusals();
    std::printf("halo 2 head relative tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
