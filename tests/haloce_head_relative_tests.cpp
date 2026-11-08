// Halo CE: the motion tracker follows the head (src/common/haloce_head_relative_logic.h). RETAIL-DERIVED LEAD, NOT
// HCEEK-VERIFIED: these tests cover the decode of the nine sites and its refusals, the exact guard on the stored
// heading, the two-stage proof the hook takes at run time before it changes anything (provisional at rest,
// confirmed by a turn, withdrawal, no second provisional pass), the head built from the frame's own context, the
// whole decision as the detour runs it, and the player's headset check on the game's own placement arithmetic.
#include "haloce_head_relative_logic.h"

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

using namespace haloce_head_relative;
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
void PutRip(std::vector<uint8_t>& bytes, size_t disp, size_t next, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(next)));
    std::memcpy(bytes.data() + disp, &rel, 4);
}

// The image as retail halo1.dll lays it out (measured offsets from the module base).
constexpr int64_t kDrawRva = 0xBADFA0, kUpdateRva = 0xBAE530, kStoreRva = 0xBAE726, kRenderRva = 0xBAE890,
                  kDatumRva = 0xBAE94A, kUnitCallRva = 0xBAEA13, kPointCallRva = 0xBAEAC2, kPlacerRva = 0xBADD90,
                  kFacingRva = 0xA999F4, kGlobalsRva = 0x2EA8740, kControlRva = 0x2D8FE70, kHalfPiRva = 0x194F5D8,
                  kSignMaskRva = 0x18CB2B0, kCosRva = 0x1607023, kSinRva = 0x1607029, kHudGlobalsRva = 0x2EA3268;

struct Image
{
    std::vector<uint8_t> draw, update, store, render, datum, unitCall, pointCall, placer, facing;
    uint8_t halfPi[4]{}, signMask[4]{};
    int64_t storeFunctionRva = kUpdateRva, datumFunctionRva = kRenderRva, unitCallFunctionRva = kRenderRva,
            pointCallFunctionRva = kRenderRva;
    int64_t placerRva = kPlacerRva;
    bool drawMissing = false, updateMissing = false, storeMissing = false, renderMissing = false,
         datumMissing = false, unitCallMissing = false, pointCallMissing = false, placerMissing = false,
         facingMissing = false;
};
Image Retail()
{
    Image image;
    image.draw = PatternBytes(kTrackerDrawPattern);
    PutBranch(image.draw, kDrawCallUpdate, kDrawRva, kUpdateRva);
    PutBranch(image.draw, kDrawJumpRender, kDrawRva, kRenderRva);
    image.update = PatternBytes(kTrackerUpdateHeadPattern);
    PutU32(image.update, kUpdatePlayerStride, 0x568);
    image.update[kUpdateLocalSlot] = 0x28;
    PutRip(image.update, kUpdateGlobalsDisp, kUpdateGlobalsNext, kUpdateRva, kGlobalsRva);
    PutU32(image.update, kUpdateFlagDisp, 0x15A6);
    PutU32(image.update, kUpdateActiveDisp, 0x15A4);
    PutU32(image.update, kUpdateDatumStride, 0x84);
    image.store = PatternBytes(kTrackerStorePattern);
    image.store[kStoreLocalSlot] = 0x28;
    image.store[kStoreControlStride] = 0x38;
    PutRip(image.store, kStoreControlDisp, kStoreControlNext, kStoreRva, kControlRva);
    PutU32(image.store, kStoreYawDisp, 0x17C);
    PutRip(image.store, kStoreHalfPiDisp, kStoreHalfPiNext, kStoreRva, kHalfPiRva);
    image.store[kStoreHeadingDisp] = 0x7C;
    image.render = PatternBytes(kTrackerRenderHeadPattern);
    PutU32(image.render, kRenderPlayerStride, 0x568);
    PutRip(image.render, kRenderGlobalsDisp, kRenderGlobalsNext, kRenderRva, kGlobalsRva);
    image.datum = PatternBytes(kTrackerRenderDatumPattern);
    PutRip(image.datum, kDatumGlobalsDisp, kDatumGlobalsNext, kDatumRva, kGlobalsRva);
    PutU32(image.datum, kDatumActiveDisp, 0x15A4);
    PutU32(image.datum, kDatumStride, 0x84);
    image.unitCall = PatternBytes(kTrackerUnitCallPattern);
    PutBranch(image.unitCall, kUnitCall, kUnitCallRva, kPlacerRva);
    image.pointCall = PatternBytes(kTrackerPointCallPattern);
    PutBranch(image.pointCall, kPointCall, kPointCallRva, kPlacerRva);
    image.placer = PatternBytes(kTrackerPlacerPattern);
    image.placer[kPlacerHeadingDisp] = 0x7C;
    PutRip(image.placer, kPlacerMaskDisp, kPlacerMaskNext, kPlacerRva, kSignMaskRva);
    PutBranch(image.placer, kPlacerCallCos, kPlacerRva, kCosRva);
    PutBranch(image.placer, kPlacerCallSin, kPlacerRva, kSinRva);
    image.facing = PatternBytes(kFacingReaderPattern);
    PutRip(image.facing, kFacingControlDisp, kFacingControlNext, kFacingRva, kControlRva);
    PutU32(image.facing, kFacingYaw, 0x17C);
    image.facing[kFacingStride] = 0x38;
    const float halfPi = kHalfPi;
    std::memcpy(image.halfPi, &halfPi, 4);
    const uint32_t mask = 0x80000000u;
    std::memcpy(image.signMask, &mask, 4);
    return image;
}
TrackerBind Decode(const Image& image, TrackerSites& sites)
{
    TrackerImage in;
    in.draw = image.drawMissing ? nullptr : image.draw.data();
    in.drawRva = kDrawRva;
    in.update = image.updateMissing ? nullptr : image.update.data();
    in.updateRva = kUpdateRva;
    in.store = image.storeMissing ? nullptr : image.store.data();
    in.storeRva = kStoreRva;
    in.storeFunctionRva = image.storeFunctionRva;
    in.render = image.renderMissing ? nullptr : image.render.data();
    in.renderRva = kRenderRva;
    in.datum = image.datumMissing ? nullptr : image.datum.data();
    in.datumRva = kDatumRva;
    in.datumFunctionRva = image.datumFunctionRva;
    in.unitCall = image.unitCallMissing ? nullptr : image.unitCall.data();
    in.unitCallRva = kUnitCallRva;
    in.unitCallFunctionRva = image.unitCallFunctionRva;
    in.pointCall = image.pointCallMissing ? nullptr : image.pointCall.data();
    in.pointCallRva = kPointCallRva;
    in.pointCallFunctionRva = image.pointCallFunctionRva;
    in.placer = image.placerMissing ? nullptr : image.placer.data();
    in.placerRva = image.placerRva;
    in.facing = image.facingMissing ? nullptr : image.facing.data();
    in.facingRva = kFacingRva;
    return DecodeTrackerSites(in, sites);
}
// Decodes a changed copy of retail's image and expects one refusal that fills nothing.
template <typename Change>
void Refused(TrackerBind why, const char* what, Change change)
{
    Image image = Retail();
    change(image);
    TrackerSites sites;
    Check(Decode(image, sites) == why && sites == TrackerSites{}, what);
}

void TestSites()
{
    Check(PatternBytes(kTrackerDrawPattern).size() == kDrawBytes &&
              PatternBytes(kTrackerUpdateHeadPattern).size() == kUpdateHeadBytes &&
              PatternBytes(kTrackerStorePattern).size() == kStoreBytes &&
              PatternBytes(kTrackerRenderHeadPattern).size() == kRenderHeadBytes &&
              PatternBytes(kTrackerRenderDatumPattern).size() == kRenderDatumBytes &&
              PatternBytes(kTrackerUnitCallPattern).size() == kUnitCallBytes &&
              PatternBytes(kTrackerPointCallPattern).size() == kPointCallBytes &&
              PatternBytes(kTrackerPlacerPattern).size() == kPlacerBytes &&
              PatternBytes(kFacingReaderPattern).size() == kFacingBytes,
          "each pattern has the length the header says");
    Check(kStoreInstruction + 5 == kStoreBytes && kDrawJumpRender + 5 == kDrawBytes && kUnitCall + 5 == kUnitCallBytes &&
              kPointCall + 5 == kPointCallBytes && kFacingJump + 5 == kFacingBytes,
          "the store, the entry's jump, both placer calls and the facing reader's jump end their patterns");

    const Image retail = Retail();
    TrackerSites sites;
    Check(Decode(retail, sites) == TrackerBind::Bound, "retail's layout decodes");
    Check(sites.drawRva == kDrawRva && sites.updateRva == kUpdateRva && sites.renderRva == kRenderRva &&
              sites.placerRva == kPlacerRva && sites.storeRva == 0xBAE74D,
          "the draw entry, the update, the render (the hook), the placer and the store instruction are where the sites point");
    Check(sites.globalsRva == kGlobalsRva && sites.playerStride == 0x568 && sites.datumStride == 0x84 &&
              sites.activeOffset == 0x15A4 && sites.updatedOffset == 0x15A6 && sites.headingOffset == 0x7C,
          "the sensor's layout is read from the update's own operands");
    Check(sites.controlRva == kControlRva && sites.controlStride == 0x38 && sites.yawOffset == 0x17C,
          "the yaw's record, stride and offset are read from the store");
    Check(sites.halfPiRva == kHalfPiRva && sites.signMaskRva == kSignMaskRva && sites.cosThunkRva == kCosRva &&
              sites.sinThunkRva == kSinRva,
          "the two constants and the two trig calls are where the store and the placer point");
    Check(CheckTrackerConstants(retail.halfPi, retail.signMask) == TrackerBind::Bound, "pi/2 and the sign mask are accepted");
    Check(HeadingOffset(sites, 0, 0) == 0x7C && HeadingOffset(sites, 0, 9) == 9 * 0x84 + 0x7C &&
              HeadingOffset(sites, 1, 2) == 0x568 + 2 * 0x84 + 0x7C && ControlYawOffset(sites, 0) == 0x17C &&
              ControlYawOffset(sites, 2) == 0x17C + 2 * 0x38,
          "the heading of a player's datum and the yaw of a user are where the strides put them");
    Check(ActiveDatumValid(sites, 0) && ActiveDatumValid(sites, 9) && !ActiveDatumValid(sites, 10) &&
              !ActiveDatumValid(sites, -1) && !ActiveDatumValid(sites, 0x7FFF),
          "the active index must name one of the ten datums a player's record holds");

    Refused(TrackerBind::DrawMissing, "no unique draw entry: nothing binds", [](Image& i) { i.drawMissing = true; });
    Refused(TrackerBind::UpdateMissing, "no unique update head: nothing binds", [](Image& i) { i.updateMissing = true; });
    Refused(TrackerBind::StoreMissing, "no unique store: nothing binds", [](Image& i) { i.storeMissing = true; });
    Refused(TrackerBind::RenderMissing, "no unique render head: nothing binds", [](Image& i) { i.renderMissing = true; });
    Refused(TrackerBind::DatumMissing, "no unique datum choice: nothing binds", [](Image& i) { i.datumMissing = true; });
    Refused(TrackerBind::CallsMissing, "no unique unit call: nothing binds", [](Image& i) { i.unitCallMissing = true; });
    Refused(TrackerBind::CallsMissing, "no unique point call: nothing binds", [](Image& i) { i.pointCallMissing = true; });
    Refused(TrackerBind::PlacerMissing, "no unique placer: nothing binds", [](Image& i) { i.placerMissing = true; });
    Refused(TrackerBind::FacingMissing, "no unique facing reader: nothing binds", [](Image& i) { i.facingMissing = true; });

    Refused(TrackerBind::EntryMismatch, "a draw entry that calls something else than the matched update is refused",
            [](Image& i) { PutBranch(i.draw, kDrawCallUpdate, kDrawRva, kUpdateRva + 0x10); });
    Refused(TrackerBind::EntryMismatch, "a draw entry that jumps somewhere else than the matched render is refused",
            [](Image& i) { PutBranch(i.draw, kDrawJumpRender, kDrawRva, kRenderRva - 0x20); });
    Refused(TrackerBind::StoreOutsideUpdate, "a store in another function is refused",
            [](Image& i) { i.storeFunctionRva = kRenderRva; });
    Refused(TrackerBind::StoreOutsideUpdate, "a store the function table does not know is refused",
            [](Image& i) { i.storeFunctionRva = -1; });
    Refused(TrackerBind::SitesOutsideRender, "a datum choice outside the render is refused",
            [](Image& i) { i.datumFunctionRva = kUpdateRva; });
    Refused(TrackerBind::SitesOutsideRender, "a unit call outside the render is refused",
            [](Image& i) { i.unitCallFunctionRva = kUpdateRva; });
    Refused(TrackerBind::SitesOutsideRender, "a point call outside the render is refused",
            [](Image& i) { i.pointCallFunctionRva = -1; });
    Refused(TrackerBind::PlacerMismatch, "a unit call that lands elsewhere than the matched placer is refused",
            [](Image& i) { PutBranch(i.unitCall, kUnitCall, kUnitCallRva, kPlacerRva + 0x40); });
    Refused(TrackerBind::PlacerMismatch, "a point call that lands elsewhere than the matched placer is refused",
            [](Image& i) { PutBranch(i.pointCall, kPointCall, kPointCallRva, kPlacerRva - 0x40); });
    Refused(TrackerBind::PlacerMismatch, "a placer matched somewhere the calls do not go is refused",
            [](Image& i) { i.placerRva = kPlacerRva + 0x100; });

    Refused(TrackerBind::LayoutMismatch, "a render with another player stride is refused",
            [](Image& i) { PutU32(i.render, kRenderPlayerStride, 0x56C); });
    Refused(TrackerBind::LayoutMismatch, "a render with another datum stride is refused",
            [](Image& i) { PutU32(i.datum, kDatumStride, 0x88); });
    Refused(TrackerBind::LayoutMismatch, "a render with another active index is refused",
            [](Image& i) { PutU32(i.datum, kDatumActiveDisp, 0x15A0); });
    Refused(TrackerBind::LayoutMismatch, "a render head on other globals is refused",
            [](Image& i) { PutRip(i.render, kRenderGlobalsDisp, kRenderGlobalsNext, kRenderRva, kGlobalsRva + 8); });
    Refused(TrackerBind::LayoutMismatch, "a datum choice on other globals is refused",
            [](Image& i) { PutRip(i.datum, kDatumGlobalsDisp, kDatumGlobalsNext, kDatumRva, kHudGlobalsRva); });
    Refused(TrackerBind::LayoutMismatch, "a store that takes the player from another stack slot is refused",
            [](Image& i) { i.store[kStoreLocalSlot] = 0x30; });
    // The neighbouring field: the placer reading the contact count (+0x78) or the point count (+0x80).
    Refused(TrackerBind::FieldMismatch, "a placer that reads the field before the heading is refused",
            [](Image& i) { i.placer[kPlacerHeadingDisp] = 0x78; });
    Refused(TrackerBind::FieldMismatch, "a store that writes the field after the heading is refused",
            [](Image& i) { i.store[kStoreHeadingDisp] = 0x80; });
    // The yaw must be the facing reader's: its pitch (+0x180) and the control record's own yaw are not it.
    Refused(TrackerBind::FacingMismatch, "a store that reads the facing pitch is refused",
            [](Image& i) { PutU32(i.store, kStoreYawDisp, 0x180); });
    Refused(TrackerBind::FacingMismatch, "a store with another record stride is refused",
            [](Image& i) { i.store[kStoreControlStride] = 0x58; });
    Refused(TrackerBind::FacingMismatch, "a store that reads another block than player control is refused",
            [](Image& i) { PutRip(i.store, kStoreControlDisp, kStoreControlNext, kStoreRva, kHudGlobalsRva); });
    Refused(TrackerBind::FacingMismatch, "a facing reader with other operands is refused",
            [](Image& i) { PutU32(i.facing, kFacingYaw, 0x1B4); });

    const auto both = [](Image& i, size_t storeAt, size_t placerAt, uint8_t value) {
        i.store[storeAt] = value;
        i.placer[placerAt] = value;
    };
    Refused(TrackerBind::Implausible, "a heading that is not float-aligned is refused",
            [&](Image& i) { both(i, kStoreHeadingDisp, kPlacerHeadingDisp, 0x7E); });
    Refused(TrackerBind::Implausible, "a heading past the datum is refused", [](Image& i) {
        PutU32(i.update, kUpdateDatumStride, 0x7C);
        PutU32(i.datum, kDatumStride, 0x7C);
    });
    Refused(TrackerBind::Implausible, "a datum larger than a player's record is refused", [](Image& i) {
        PutU32(i.update, kUpdatePlayerStride, 0x80);
        PutU32(i.render, kRenderPlayerStride, 0x80);
    });
    Refused(TrackerBind::Implausible, "an active index inside local player 0's record is refused", [](Image& i) {
        PutU32(i.update, kUpdateActiveDisp, 0x564);
        PutU32(i.datum, kDatumActiveDisp, 0x564);
        PutU32(i.update, kUpdateFlagDisp, 0x566);
    });
    Refused(TrackerBind::Implausible, "an 'updated' byte that is not right after the active index is refused",
            [](Image& i) { PutU32(i.update, kUpdateFlagDisp, 0x15A8); });
    Refused(TrackerBind::Implausible, "sensor globals and player control behind one pointer are refused", [](Image& i) {
        PutRip(i.store, kStoreControlDisp, kStoreControlNext, kStoreRva, kGlobalsRva);
        PutRip(i.facing, kFacingControlDisp, kFacingControlNext, kFacingRva, kGlobalsRva);
    });
    Refused(TrackerBind::Implausible, "one trig thunk called twice is refused",
            [](Image& i) { PutBranch(i.placer, kPlacerCallSin, kPlacerRva, kCosRva); });

    uint8_t wrong[4];
    const float pi = kPi;
    std::memcpy(wrong, &pi, 4);
    Check(CheckTrackerConstants(wrong, retail.signMask) == TrackerBind::ConstantMismatch, "a constant that is not pi/2 is refused");
    const uint32_t absolute = 0x7FFFFFFFu;
    std::memcpy(wrong, &absolute, 4);
    Check(CheckTrackerConstants(retail.halfPi, wrong) == TrackerBind::ConstantMismatch, "a mask that is not the sign bit is refused");
    Check(CheckTrackerConstants(nullptr, retail.signMask) == TrackerBind::ConstantMismatch &&
              CheckTrackerConstants(retail.halfPi, nullptr) == TrackerBind::ConstantMismatch,
          "an unreadable constant is refused");
    bool named = true;
    for (int i = 0; i < static_cast<int>(TrackerBind::Count); ++i)
        named = named && TrackerBindName(static_cast<TrackerBind>(i))[0] != '?';
    for (int i = 0; i < static_cast<int>(TrackerUse::Count); ++i)
        named = named && TrackerUseName(static_cast<TrackerUse>(i))[0] != '?';
    for (int i = 0; i < static_cast<int>(ProofStage::Count); ++i)
        named = named && ProofStageName(static_cast<ProofStage>(i))[0] != '?';
    Check(named, "every refusal and every stage has a name for the log");
}

// ---- the exact guard ----------------------------------------------------------------------------------------------
void TestFreshStore()
{
    const float halfPi = kHalfPi;
    bool exact = true;
    for (float yawDeg = 0.0f; yawDeg < 360.0f; yawDeg += 0.37f)
    {
        const float yaw = yawDeg * kDegToRad;
        const volatile float stored = yaw + halfPi;   // the game's one single-precision add
        exact = exact && FreshStore(stored, yaw, halfPi);
    }
    Check(exact, "the game's own sum is recognised bit for bit at every yaw");
    const float yaw = 1.25f;
    const float stored = yaw + halfPi;
    Check(!FreshStore(std::nextafter(stored, 10.0f), yaw, halfPi) && !FreshStore(std::nextafter(stored, -10.0f), yaw, halfPi),
          "one unit in the last place away is not the game's store");
    Check(!FreshStore(stored, yaw + 0.001f, halfPi) && !FreshStore(stored, yaw - 0.001f, halfPi),
          "a yaw that has moved by a twentieth of a degree since the store is not this call's store");
    Check(!FreshStore(FieldFromYaw(WrapYaw(yaw + 0.5f)), yaw, halfPi),
          "a heading this lane wrote earlier (the head 29 deg off the control yaw) is not the game's store");
    Check(!FreshStore(0.0f, yaw, halfPi) && !FreshStore(yaw, yaw, halfPi), "a cleared field and a bare yaw are not the store");
    Check(!FreshStore(stored, yaw, kPi), "another constant would give another sum");
}

// ---- the proof ----------------------------------------------------------------------------------------------------
// Feeds `count` samples: the reference goes from `fromDeg` by `stepDeg` a sample, the game's yaw is the reference
// plus `missDeg`. Returns the last step.
ProofStep Feed(ViewProof& proof, int count, float fromDeg, float stepDeg, float missDeg, bool usable = true)
{
    ProofStep last = ProofStep::NoReference;
    for (int i = 0; i < count; ++i)
    {
        const float reference = (fromDeg + stepDeg * static_cast<float>(i)) * kDegToRad;
        last = StepProof(proof, WrapYaw(reference + missDeg * kDegToRad), usable, WrapPi(reference));
    }
    return last;
}
// The game's yaw is `game(reference)`: a field that is not the yaw.
template <typename Game>
ProofStep FeedField(ViewProof& proof, int count, float fromDeg, float stepDeg, Game game)
{
    ProofStep last = ProofStep::NoReference;
    for (int i = 0; i < count; ++i)
    {
        const float reference = WrapPi((fromDeg + stepDeg * static_cast<float>(i)) * kDegToRad);
        last = StepProof(proof, game(reference), true, reference);
    }
    return last;
}

void TestProof()
{
    const int samples = static_cast<int>(kProofSamples), withdraw = static_cast<int>(kProofWithdrawRun);

    // PROVISIONAL AT REST: a player who never turns with the stick still gets the tracker.
    ViewProof proof;
    Check(Feed(proof, samples - 1, 72.0f, 0.0f, 0.3f) == ProofStep::Agreed && !ProofAllows(proof),
          "29 agreeing samples at rest: not yet");
    Check(Feed(proof, 1, 72.0f, 0.0f, 0.3f) == ProofStep::Provisional && proof.stage == ProofStage::Provisional &&
              ProofAllows(proof),
          "the 30th, with the reference never having moved: provisional, and the lane may substitute");
    Check(Feed(proof, 500, 72.0f, 0.0f, 0.3f) == ProofStep::Agreed && proof.stage == ProofStage::Provisional,
          "at rest it stays provisional, however long it agrees");
    // CONFIRMATION: the first stick turn.
    Check(Feed(proof, 1, 81.0f, 0.0f, 0.0f) == ProofStep::Agreed && proof.stage == ProofStage::Provisional,
          "an agreeing sample 9 deg away is not yet the spread");
    Check(Feed(proof, 1, 102.0f, 0.0f, 0.0f) == ProofStep::Confirmed && proof.stage == ProofStage::Confirmed,
          "an agreeing sample 30 deg away (one snap turn) confirms it");
    Check(Feed(proof, 10, 102.0f, 3.0f, 0.0f) == ProofStep::Agreed && proof.stage == ProofStage::Confirmed,
          "once confirmed, an agreement is just an agreement");

    // Both conditions at once: a player turning while the first thirty samples come in.
    proof = ViewProof{};
    Check(Feed(proof, samples - 1, 100.0f, 0.5f, 0.3f) == ProofStep::Agreed && proof.stage == ProofStage::Unproven &&
              Feed(proof, 1, 115.0f, 0.0f, 0.3f) == ProofStep::Confirmed,
          "thirty agreeing samples across 15 deg confirm at once, with no provisional step");

    // The frame and the field can be one frame apart: a snap turn then gives one disagreeing sample.
    proof = ViewProof{};
    Feed(proof, 20, 10.0f, 0.0f, 0.0f);
    Check(Feed(proof, 1, 10.0f, 0.0f, 30.0f) == ProofStep::Disagreed && proof.agreed == 20,
          "one disagreement before any stage does not start the count again");
    Check(Feed(proof, samples - 21, 40.0f, 0.0f, 0.0f) == ProofStep::Agreed &&
              Feed(proof, 1, 40.0f, 0.0f, 0.0f) == ProofStep::Confirmed,
          "... and the snap turn it came from confirms the proof at the 30th agreeing sample");
    proof = ViewProof{};
    Feed(proof, 20, 10.0f, 0.0f, 0.0f);
    Check(Feed(proof, static_cast<int>(kProofRestartRun) - 1, 10.0f, 0.0f, 30.0f) == ProofStep::Disagreed && proof.agreed == 20,
          "three disagreements in a row: still counting");
    Check(Feed(proof, 1, 10.0f, 0.0f, 30.0f) == ProofStep::Restarted && proof.agreed == 0 && proof.arc == 0.0f,
          "the fourth in a row starts the count again");
    Check(Feed(proof, 50, 10.0f, 0.0f, 30.0f) == ProofStep::Disagreed && proof.agreed == 0 && !ProofAllows(proof),
          "further disagreements change nothing");
    Check(Feed(proof, 1, 10.0f, 0.0f, 0.0f) == ProofStep::Agreed && proof.disagreed == 0 && proof.agreed == 1,
          "an agreement clears the run and counts as the first");

    // Lag and a missing reference count for nothing either way.
    proof = ViewProof{};
    Feed(proof, 10, 10.0f, 0.5f, 0.0f);
    Check(Feed(proof, 200, 15.0f, 0.0f, 6.0f) == ProofStep::Lagging && proof.agreed == 10 && proof.disagreed == 0,
          "a 6 deg miss neither counts nor resets");
    Check(Feed(proof, 200, 15.0f, 1.0f, 90.0f, false) == ProofStep::NoReference && proof.agreed == 10 && proof.disagreed == 0,
          "without a reference nothing is counted for or against");
    Check(StepProof(proof, std::numeric_limits<float>::quiet_NaN(), true, 0.2f) == ProofStep::NoReference &&
              StepProof(proof, 0.2f, true, std::numeric_limits<float>::infinity()) == ProofStep::NoReference &&
              proof.agreed == 10,
          "a non-finite value is no reference");
    proof = ViewProof{};
    Check(Feed(proof, 400, 0.0f, 0.5f, 5.0f) == ProofStep::Lagging && proof.agreed == 0 && !ProofAllows(proof),
          "a yaw 5 deg off the frame never agrees");

    // Across the seam at +/-180 deg, with the game's yaw in [0, 2 pi) against a reference in (-pi, pi].
    proof = ViewProof{};
    Check(Feed(proof, samples, 170.0f, 0.5f, -0.4f) == ProofStep::Confirmed, "a turn through 180 deg confirms like any other");

    // A CONSTANT THAT AGREES AT REST, CAUGHT BY THE FIRST TURN.
    const float constant = WrapYaw(75.0f * kDegToRad);
    const auto constantField = [&](float) { return constant; };
    proof = ViewProof{};
    Check(FeedField(proof, samples, 75.4f, 0.0f, constantField) == ProofStep::Provisional,
          "a constant that happens to equal the frame's heading passes the provisional stage at rest");
    Check(FeedField(proof, withdraw - 1, 105.4f, 0.0f, constantField) == ProofStep::Disagreed && ProofAllows(proof),
          "the first snap turn: 59 disagreements, still provisional");
    Check(FeedField(proof, 1, 105.4f, 0.0f, constantField) == ProofStep::Withdrawn && !ProofAllows(proof) &&
              proof.stage == ProofStage::Unproven && proof.provisionalSpent && proof.agreed == 0,
          "the 60th in a row withdraws it");
    Check(Near(proof.lastAgreedGame, constant) && Near(proof.lastAgreedReference, 75.4f * kDegToRad) &&
              Near(proof.firstDisagreedGame, constant) && Near(proof.firstDisagreedReference, 105.4f * kDegToRad),
          "the withdrawal keeps the last agreeing and the first disagreeing values for the log");
    // THE NO-SECOND-PROVISIONAL RULE.
    Check(FeedField(proof, 5000, 75.4f, 0.0f, constantField) == ProofStep::Agreed && !ProofAllows(proof) &&
              proof.stage == ProofStage::Unproven,
          "back at rest on the same heading it agrees again for 5000 samples and is NOT let through a second time");
    bool everAllowed = false;
    for (int turn = 0; turn < 40; ++turn)
    {
        FeedField(proof, 100, 75.4f + 9.0f * static_cast<float>(turn), 0.0f, constantField);
        everAllowed = everAllowed || ProofAllows(proof);
    }
    Check(!everAllowed, "... and no amount of turning confirms a constant");
    // The real yaw, after a withdrawal: thirty at rest are not enough, the turn is.
    proof = ViewProof{};
    proof.provisionalSpent = true;
    Check(Feed(proof, 200, 30.0f, 0.0f, 0.2f) == ProofStep::Agreed && !ProofAllows(proof),
          "after a withdrawal the true yaw at rest is not let through either");
    Check(Feed(proof, 1, 60.0f, 0.0f, 0.2f) == ProofStep::Confirmed && ProofAllows(proof),
          "... until a stick turn confirms it");

    // A constant never confirms, whatever the frame does, from a fresh proof.
    proof = ViewProof{};
    bool everConfirmed = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapPi((static_cast<float>(i % 360) - 180.0f) * kDegToRad);
        everConfirmed = everConfirmed || StepProof(proof, constant, true, reference) == ProofStep::Confirmed;
    }
    Check(!everConfirmed && proof.stage != ProofStage::Confirmed, "a constant where the yaw should be is never confirmed");
    // A constant with the frame jittering around it: provisional at most.
    proof = ViewProof{};
    everConfirmed = false;
    for (int i = 0; i < 4000; ++i)
    {
        const float reference = WrapPi((75.0f + ((i % 4) ? 1.5f : 12.0f) * ((i & 8) ? 1.0f : -1.0f)) * kDegToRad);
        everConfirmed = everConfirmed || StepProof(proof, constant, true, reference) == ProofStep::Confirmed;
    }
    Check(!everConfirmed, "a constant is never confirmed by a frame that jitters around it");

    // THE MIRROR: a field that is the yaw mirrored about some axis agrees near that axis and near its opposite.
    const float axis = 40.0f * kDegToRad;
    const auto mirrored = [&](float reference) { return WrapYaw(2.0f * axis - reference); };
    proof = ViewProof{};
    Check(FeedField(proof, samples, 40.3f, 0.0f, mirrored) == ProofStep::Provisional,
          "a mirrored yaw passes the provisional stage when the player happens to face its axis at rest");
    Check(FeedField(proof, withdraw, 70.3f, 0.0f, mirrored) == ProofStep::Withdrawn && !ProofAllows(proof),
          "... and the first turn withdraws it");
    everAllowed = false;
    for (int lap = 0; lap < 6; ++lap)
    {
        // Rest on the axis, snap to the opposite heading in one sample, rest there, and back.
        FeedField(proof, 200, 40.3f, 0.0f, mirrored);
        everAllowed = everAllowed || ProofAllows(proof);
        FeedField(proof, 200, 220.3f, 0.0f, mirrored);
        everAllowed = everAllowed || ProofAllows(proof);
    }
    Check(!everAllowed && Near(proof.arc, 0.0f, 3.0f * kDegToRad),
          "a mirrored yaw is never confirmed, not even by resting on both of its agreeing headings: half a turn "
          "apart is outside the confirming band");
    proof = ViewProof{};
    everConfirmed = false;
    for (int i = 0; i < 8000; ++i)
    {
        const float reference = WrapPi((static_cast<float>(i) * 0.3f - 170.0f) * kDegToRad);
        everConfirmed = everConfirmed || StepProof(proof, mirrored(reference), true, reference) == ProofStep::Confirmed;
    }
    Check(!everConfirmed, "a mirrored yaw is never confirmed by a slow turn through every heading");
    // An unrelated angle that crosses the frame's heading now and then.
    proof = ViewProof{};
    everConfirmed = false;
    for (int i = 0; i < 8000; ++i)
    {
        const float reference = WrapPi(static_cast<float>(i) * 0.7f * kDegToRad);
        const float other = WrapYaw(static_cast<float>(i) * -0.4f * kDegToRad + 1.0f);
        const ProofStep step = StepProof(proof, other, true, reference);
        everConfirmed = everConfirmed || step == ProofStep::Confirmed || step == ProofStep::Provisional;
    }
    Check(!everConfirmed, "an unrelated angle passes neither stage");

    // WITHDRAWAL FROM EACH STAGE: only a run of disagreements, and an agreement in between starts the run again.
    proof = ViewProof{};
    Feed(proof, samples, 0.0f, 0.0f, 0.0f);
    Check(proof.stage == ProofStage::Provisional && Feed(proof, withdraw - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed &&
              ProofAllows(proof),
          "provisional: 59 disagreements in a row keep it");
    Check(Feed(proof, 1, 0.0f, 0.0f, 0.5f) == ProofStep::Agreed && proof.disagreed == 0, "an agreement clears the run");
    Check(Feed(proof, withdraw - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed && ProofAllows(proof) &&
              Feed(proof, 1, 0.0f, 0.0f, 40.0f) == ProofStep::Withdrawn && !ProofAllows(proof) && proof.provisionalSpent,
          "provisional: the 60th in a row withdraws it");
    proof = ViewProof{};
    Feed(proof, samples, 0.0f, 1.0f, 0.0f);
    Check(proof.stage == ProofStage::Confirmed && Feed(proof, withdraw - 1, 0.0f, 0.2f, 40.0f) == ProofStep::Disagreed &&
              ProofAllows(proof),
          "confirmed: 59 disagreements in a row keep it (a snap turn is one or two)");
    Check(Feed(proof, 1, 0.0f, 0.0f, 40.0f) == ProofStep::Withdrawn && !ProofAllows(proof) && proof.provisionalSpent &&
              proof.stage == ProofStage::Unproven,
          "confirmed: the 60th in a row withdraws it");
    Check(Feed(proof, samples, 0.0f, 0.0f, 0.0f) == ProofStep::Agreed && !ProofAllows(proof) &&
              Feed(proof, 1, 29.0f, 0.0f, 0.0f) == ProofStep::Confirmed && ProofAllows(proof),
          "after a withdrawal the whole proof is taken again, and only a confirmation lets the lane substitute");
    Check(Feed(proof, 500, 0.0f, 0.2f, 6.0f) == ProofStep::Lagging && ProofAllows(proof) &&
              Feed(proof, 500, 0.0f, 0.2f, 90.0f, false) == ProofStep::NoReference && ProofAllows(proof),
          "lag and a missing reference never withdraw a proof");

    // WHAT A REINSTALL KEEPS.
    proof = ViewProof{};
    Feed(proof, samples, 0.0f, 1.0f, 0.0f);
    ViewProof kept = ProofKeptAcrossReinstall(proof);
    Check(kept.stage == ProofStage::Confirmed && ProofAllows(kept) && !kept.provisionalSpent,
          "a confirmed proof is kept across a reinstall on the same image");
    proof = ViewProof{};
    Feed(proof, samples, 0.0f, 0.0f, 0.0f);
    kept = ProofKeptAcrossReinstall(proof);
    Check(proof.stage == ProofStage::Provisional && kept.stage == ProofStage::Unproven && !ProofAllows(kept) &&
              kept.agreed == 0 && !kept.provisionalSpent,
          "a provisional proof is NOT kept: it is taken again, and may pass at rest again");
    Check(Feed(kept, samples, 0.0f, 0.0f, 0.0f) == ProofStep::Provisional, "... which it does after thirty samples");
    proof = ViewProof{};
    Feed(proof, 17, 0.0f, 0.0f, 0.0f);
    kept = ProofKeptAcrossReinstall(proof);
    Check(kept.agreed == 0 && kept.stage == ProofStage::Unproven, "an unfinished count is not kept either");
    proof = ViewProof{};
    Feed(proof, samples, 0.0f, 0.0f, 0.0f);
    Feed(proof, withdraw, 0.0f, 0.0f, 50.0f);
    kept = ProofKeptAcrossReinstall(proof);
    Check(kept.provisionalSpent && !ProofAllows(kept) && Feed(kept, 300, 0.0f, 0.0f, 0.0f) == ProofStep::Agreed &&
              !ProofAllows(kept),
          "a withdrawal is remembered across a reinstall: still no second provisional pass on this image");
}

// ---- the head, and the view frame it is measured in ---------------------------------------------------------------
halo_ce::Quat YawLeft(float deg)   // OpenXR: about +Y, positive turns to the left
{
    const float half = deg * kDegToRad * 0.5f;
    return {0, std::sin(half), 0, std::cos(half)};
}
halo_ce::Quat PitchUp(float deg)   // about +X, positive looks up
{
    const float half = deg * kDegToRad * 0.5f;
    return {std::sin(half), 0, 0, std::cos(half)};
}
halo_ce::RenderContext Context(float cameraYawDeg, float cameraPitchDeg, halo_ce::Quat head, halo_ce::Quat reference = {})
{
    halo_ce::RenderContext context;
    const float yaw = cameraYawDeg * kDegToRad, pitch = cameraPitchDeg * kDegToRad;
    context.camera.position = {10, 20, 30};
    context.camera.forward = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    context.camera.up = {-std::sin(pitch) * std::cos(yaw), -std::sin(pitch) * std::sin(yaw), std::cos(pitch)};
    context.camera.verticalFov = 1.0f;
    context.camera.viewport = context.camera.window = {0, 0, 32, 32};
    context.camera.nearPlane = 0.01f;
    context.camera.farPlane = 1000.0f;
    context.tracking.serial = 9;
    context.tracking.generation = 3;
    context.tracking.spaceEpoch = 7;
    context.tracking.headOrientation = head;
    for (auto& eye : context.tracking.eyes)
    {
        eye.orientation = head;
        eye.fov[0] = -0.8f;
        eye.fov[1] = 0.8f;
        eye.fov[2] = 0.8f;
        eye.fov[3] = -0.8f;
    }
    context.reference.orientation = reference;
    context.reference.generation = 3;
    context.reference.spaceEpoch = 7;
    context.referenceRevision = 1;
    context.rendererEpoch = 1;
    context.unitsPerMeter = 1.0f / 3.048f;
    context.cameraStampUs = 5000000;
    return context;
}
float YawOf(const float v[3]) { return std::atan2(v[1], v[0]); }

void TestHeadFrame()
{
    HeadFrame frame;
    Check(BuildHeadFrame(Context(30.0f, 0.0f, {}), frame) && Near(YawOf(frame.forward), 30.0f * kDegToRad) &&
              Near(frame.forward[2], 0.0f) && Near(frame.up[2], 1.0f) && Near(YawOf(frame.heading), 30.0f * kDegToRad) &&
              !frame.seat,
          "a head along the reference looks down the native camera's heading, and that heading is the frame's");
    Check(BuildHeadFrame(Context(30.0f, 0.0f, YawLeft(90.0f)), frame) && Near(YawOf(frame.forward), 120.0f * kDegToRad) &&
              Near(YawOf(frame.heading), 30.0f * kDegToRad),
          "a head turned 90 deg to the left faces 90 deg counter-clockwise of the frame; the frame does not move");
    Check(BuildHeadFrame(Context(30.0f, 0.0f, YawLeft(-45.0f)), frame) && Near(YawOf(frame.forward), -15.0f * kDegToRad),
          "a head turned 45 deg to the right faces 45 deg clockwise of the frame");
    Check(BuildHeadFrame(Context(30.0f, 0.0f, PitchUp(40.0f)), frame) && Near(YawOf(frame.forward), 30.0f * kDegToRad) &&
              Near(frame.forward[2], std::sin(40.0f * kDegToRad)),
          "a head looking up keeps its heading and lifts its forward");
    Check(BuildHeadFrame(Context(30.0f, 35.0f, YawLeft(20.0f)), frame) && Near(YawOf(frame.forward), 50.0f * kDegToRad) &&
              Near(frame.forward[2], 0.0f) && Near(frame.heading[2], 0.0f) && Near(YawOf(frame.heading), 30.0f * kDegToRad),
          "the native camera's pitch does not tilt the frame: the room stays level");
    Check(BuildHeadFrame(Context(30.0f, 0.0f, YawLeft(70.0f), YawLeft(50.0f)), frame) &&
              Near(YawOf(frame.forward), 50.0f * kDegToRad),
          "the reference's yaw is where the room's forward was set: only the head's turn since then counts");
    // The eyes are built by the same mapping (BuildEye): an eye with the head's orientation looks where the head does.
    {
        const halo_ce::RenderContext context = Context(210.0f, -15.0f, halo_ce::Multiply(YawLeft(33.0f), PitchUp(12.0f)));
        halo_ce::Cover cover{};
        halo_ce::Camera eye{};
        Check(halo_ce::BuildCover(context.tracking, context.camera.viewport, cover) &&
                  halo_ce::BuildEye(context.camera, context.tracking, context.reference, 0, context.unitsPerMeter, true,
                                    cover, eye) &&
                  BuildHeadFrame(context, frame) && Near(eye.forward.x, frame.forward[0]) &&
                  Near(eye.forward.y, frame.forward[1]) && Near(eye.forward.z, frame.forward[2]) &&
                  Near(eye.up.x, frame.up[0]) && Near(eye.up.y, frame.up[1]) && Near(eye.up.z, frame.up[2]),
              "the head's forward and up are the ones the eyes are rendered with");
    }
    // A seat: the frame's heading is the player's control yaw, not the camera's.
    {
        halo_ce::RenderContext context = Context(30.0f, 0.0f, YawLeft(10.0f));
        context.reference.seatHeadingValid = true;
        context.reference.seatHeading = {std::cos(200.0f * kDegToRad), std::sin(200.0f * kDegToRad), 0};
        Check(BuildHeadFrame(context, frame) && frame.seat && Near(WrapPi(YawOf(frame.heading) - 200.0f * kDegToRad), 0.0f) &&
                  Near(WrapPi(YawOf(frame.forward) - 210.0f * kDegToRad), 0.0f),
              "in a seat the frame's heading is the control yaw, the head turns from it, and the frame says it is a seat's");
        context.reference.seatHeading = {};
        Check(BuildHeadFrame(context, frame) && !frame.seat && Near(YawOf(frame.heading), 30.0f * kDegToRad),
              "a seat heading with no length falls back to the camera's, and the frame does not claim a seat");
    }
    HeadFrame untouched;
    untouched.forward[0] = 5.0f;
    halo_ce::RenderContext bad = Context(30.0f, 0.0f, {});
    bad.tracking.serial = 0;
    bool refused = !BuildHeadFrame(bad, untouched);
    bad = Context(30.0f, 0.0f, {});
    bad.reference.generation = 4;
    refused = refused && !BuildHeadFrame(bad, untouched);
    bad = Context(30.0f, 0.0f, {});
    bad.reference.spaceEpoch = 8;
    refused = refused && !BuildHeadFrame(bad, untouched);
    bad = Context(30.0f, 0.0f, halo_ce::Quat{0, 0, 0, 0});
    refused = refused && !BuildHeadFrame(bad, untouched);
    bad = Context(30.0f, 0.0f, {});
    bad.referenceRevision = 0;
    refused = refused && !BuildHeadFrame(bad, untouched);
    bad = Context(30.0f, 0.0f, {});
    bad.camera.forward = {0, 0, 0};
    refused = refused && !BuildHeadFrame(bad, untouched);
    Check(refused && untouched.forward[0] == 5.0f,
          "no tracking sample, another generation or space, a bad pose, no reference or no camera: no head, and nothing is filled");

    using head_relative::Use;
    Check(ReferenceUsable(Use::Ok, true, true, true, true, false), "on foot, first person, a usable sample: a reference");
    Check(!ReferenceUsable(Use::Stale, true, true, true, true, false) && !ReferenceUsable(Use::NoSample, true, true, true, true, false) &&
              !ReferenceUsable(Use::OtherGeneration, true, true, true, true, false) &&
              !ReferenceUsable(Use::Ok, false, true, true, true, false) && !ReferenceUsable(Use::Ok, true, false, true, true, false) &&
              !ReferenceUsable(Use::Ok, true, true, false, true, false) && !ReferenceUsable(Use::Ok, true, true, true, false, false) &&
              !ReferenceUsable(Use::Ok, true, true, true, true, true),
          "an unusable sample, a cinematic, no unit, a seat, a view from outside or a seat's frame is no reference");
    float yaw = 9.0f;
    const float flat[3]{0.0f, -2.0f, 0.0f}, none[3]{0.0f, 0.0f, 1.0f};
    Check(HeadingYaw(flat, yaw) && Near(yaw, -90.0f * kDegToRad) && !HeadingYaw(none, yaw) && Near(yaw, -90.0f * kDegToRad),
          "a heading's yaw is atan2(y, x); a vector with no horizontal part has none");
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
    request.localPlayer = 0;
    request.stored = true;
    request.proven = true;
    request.switchOn = true;
    request.playerState = true;
    request.gameplay = true;
    request.generation = 3;
    request.nowUs = 5000000 + 8000;
    request.sign = sign;
    request.offsetDeg = offsetDeg;
    return request;
}
float GameYaw(float yawDeg) { return WrapYaw(yawDeg * kDegToRad); }
// Where the game draws `contact` with the control yaw at controlYawDeg and the head at headYawDeg: the bearing
// from the sensor's top, positive to its left. `substitute` = this lane rewrote the heading first.
float Drawn(const float contact[3], float controlYawDeg, float headYawDeg, bool substitute = true, int sign = 1,
            float offsetDeg = 0.0f)
{
    float field = FieldFromYaw(GameYaw(controlYawDeg));
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
    Check(Near(point[0], 0.0f) && Near(point[1], -5.0f) && Near(TrackerPointBearing(point), 0.0f),
          "facing +X, a contact ahead is placed at (0, -r): the sensor's top once the rasterizer has negated it");
    TrackerPoint(left, FieldFromYaw(0.0f), point);
    Check(Near(point[0], 5.0f) && Near(point[1], 0.0f) && Near(TrackerPointBearing(point), 90.0f * kDegToRad),
          "a contact at +Y (the left) is placed at (+r, 0): the sensor's left once negated, a positive bearing");
    TrackerPoint(right, FieldFromYaw(0.0f), point);
    Check(Near(point[0], -5.0f) && Near(TrackerPointBearing(point), -90.0f * kDegToRad),
          "a contact at -Y (the right) is placed at (-r, 0): the sensor's right once negated");
    // The rasterizer draws at (-x', -y') / 32 with +y up: ahead at the top, the right at the right.
    TrackerPoint(ahead, FieldFromYaw(0.0f), point);
    const float rasterAhead[2]{-point[0] / 32.0f, -point[1] / 32.0f};
    TrackerPoint(right, FieldFromYaw(0.0f), point);
    const float rasterRight[2]{-point[0] / 32.0f, -point[1] / 32.0f};
    Check(Near(rasterAhead[0], 0.0f) && rasterAhead[1] > 0.1f && rasterRight[0] > 0.1f && Near(rasterRight[1], 0.0f),
          "as drawn: ahead is up, the right is to the right");
    TrackerPoint(ahead, FieldFromYaw(90.0f * kDegToRad), point);
    Check(Near(TrackerPointBearing(point), -90.0f * kDegToRad),
          "turn the heading 90 deg to the left and a contact that was ahead is on the right");
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
        for (float controlYaw : {0.0f, 30.0f, -75.0f, 179.0f, 270.0f})
            ahead = ahead && Near(Drawn(contact, controlYaw, headYaw), 0.0f, 1.0e-3f);
    }
    Check(ahead, "a contact straight ahead of the head is at the sensor's top, wherever the stick last left the control yaw");

    const float contact[3]{4.0f, 7.0f, 0.0f};
    // THE HEADSET CHECK: gun still (it is no input here at all), head turned.
    Check(Near(WrapPi(Drawn(contact, 35.0f, 125.0f) - Drawn(contact, 35.0f, 35.0f)), -90.0f * kDegToRad, 1.0e-3f),
          "head +90 deg (to the left), no stick turn: the contact turns -90 deg (to the right) on the sensor");
    // The control arm: what CE does without this lane.
    Check(Near(WrapPi(Drawn(contact, 35.0f, 125.0f, false) - Drawn(contact, 35.0f, 35.0f, false)), 0.0f, 1.0e-3f),
          "control arm: on the game's own sensor the head moves nothing (the report)");
    Check(Near(WrapPi(Drawn(contact, 95.0f, 20.0f, false) - Drawn(contact, 35.0f, 20.0f, false)), -60.0f * kDegToRad, 1.0e-3f),
          "control arm: the game's own sensor turns with the stick, 60 deg for 60 deg");
    // The stick still turns the tracker: a stick turn carries the head's world heading with it.
    Check(Near(WrapPi(Drawn(contact, 95.0f, 95.0f + 40.0f) - Drawn(contact, 35.0f, 35.0f + 40.0f)), -60.0f * kDegToRad, 1.0e-3f),
          "a 60 deg stick turn with the head held 40 deg to the left of the room's forward turns the contact 60 deg, as before");
    Check(Near(WrapPi(Drawn(contact, 77.0f, 77.0f) - Drawn(contact, 77.0f, 0.0f, false)), 0.0f, 1.0e-3f),
          "head along the room's forward: exactly the game's own sensor");
    Check(Near(WrapPi(Drawn(contact, 0.0f, 90.0f, true, -1) - Drawn(contact, 0.0f, 0.0f, true, -1)), 90.0f * kDegToRad, 1.0e-3f),
          "sign -1: head +90 deg turns the contact +90 deg (mirrored)");
    Check(Near(WrapPi(Drawn(contact, 0.0f, 20.0f, true, 1, 30.0f) - Drawn(contact, 0.0f, 20.0f)), -30.0f * kDegToRad, 1.0e-3f),
          "an offset of +30 deg turns the sensor's heading 30 deg further left");
}

void TestRefusals()
{
    const float native = FieldFromYaw(GameYaw(40.0f));
    const HeadSample head = Head(130.0f);
    HeadingHold hold;
    float field = 123.0f, headYaw = 0.0f;
    Check(TrackerHeading(Request(), head, hold, native, field, &headYaw) == TrackerUse::Used &&
              Near(field, FieldFromYaw(130.0f * kDegToRad)) && Near(headYaw, 130.0f * kDegToRad),
          "used: the placer reads the head's yaw plus pi/2");

    const auto refused = [&](TrackerRequest request, HeadSample sample, TrackerUse why, const char* what) {
        HeadingHold fresh;
        float out = 123.0f;
        Check(TrackerHeading(request, sample, fresh, native, out) == why && out == 123.0f, what);
    };
    TrackerRequest request = Request();
    request.localPlayer = 1;
    refused(request, head, TrackerUse::NotLocalUser, "another local player's sensor is left alone");
    request = Request();
    request.stored = false;
    refused(request, head, TrackerUse::NoStore, "no heading stored in this call: nothing is written");
    request.proven = false;
    refused(request, head, TrackerUse::NoStore, "no store comes before the proof: the log says what is missing");
    request = Request();
    request.proven = false;
    refused(request, head, TrackerUse::Unproven, "until the heading is proven nothing is changed");
    request.switchOn = false;
    refused(request, head, TrackerUse::Unproven, "unproven comes before the switch");
    request = Request();
    request.switchOn = false;
    refused(request, head, TrackerUse::SwitchOff, "the switch off: the game's own heading");
    request = Request();
    request.playerState = false;
    refused(request, head, TrackerUse::NoPlayerState, "the player's state could not be read: the game's own heading");
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
    request.generation = 0;
    refused(request, head, TrackerUse::OtherGeneration, "a hook that belongs to another load: the game's own heading");
    request = Request();
    request.nowUs = head.stampUs - 1;
    refused(request, head, TrackerUse::FromTheFuture, "a head sample stamped after now: the game's own heading");
    refused(Request(), HeadSample{}, TrackerUse::NoSample, "nothing published yet: the game's own heading");
    HeadSample notLive = head;
    notLive.valid = false;
    refused(Request(), notLive, TrackerUse::NoSample, "the head view was not built for that frame: the game's own heading");
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
    Check(inRange, "the written heading stays in [pi/2, 5 pi/2), the range the game's own yaw gives it");
}

// ---- one call of the render, as the detour decides it -------------------------------------------------------------
// A frame of ordinary play: on foot, first person, the game stored `controlYawDeg`, the render path published the
// head `headOffsetDeg` to the left of a view frame at `frameYawDeg`.
TrackerFrame Frame(float controlYawDeg, float frameYawDeg, float headOffsetDeg, uint64_t nowUs = 5000000 + 8000)
{
    TrackerFrame frame;
    frame.localPlayer = 0;
    frame.fieldRead = true;
    frame.halfPi = kHalfPi;
    frame.controlYaw = GameYaw(controlYawDeg);
    frame.field = frame.controlYaw + frame.halfPi;
    frame.sampleRead = true;
    frame.sample = Head(frameYawDeg + headOffsetDeg, 0.0f, nowUs - 8000);
    Forward(frameYawDeg, 0.0f, frame.frameHeading);
    frame.playerState = true;
    frame.perspective = 0;
    frame.hasUnit = true;
    frame.onFoot = true;
    frame.switchOn = true;
    frame.generation = 3;
    frame.nowUs = nowUs;
    return frame;
}

void TestDecision()
{
    ViewProof proof;
    HeadingHold hold;
    const int samples = static_cast<int>(kProofSamples);

    // At rest, head turned 50 deg to the left of the room's forward: unproven, then provisional, then used.
    bool unprovenUntouched = true;
    for (int i = 0; i < samples - 1; ++i)
    {
        const TrackerFrame frame = Frame(20.0f, 20.0f, 50.0f);
        const TrackerDecision decision = DecideTracker(frame, proof, hold);
        unprovenUntouched = unprovenUntouched && decision.use == TrackerUse::Unproven && decision.field == frame.field &&
                            decision.stepped && decision.step == ProofStep::Agreed;
    }
    Check(unprovenUntouched, "the first 29 draws: the proof is stepped, nothing is changed, the reason is 'unproven'");
    TrackerFrame frame = Frame(20.0f, 20.0f, 50.0f);
    TrackerDecision decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::Provisional && decision.use == TrackerUse::Used &&
              Near(decision.field, FieldFromYaw(70.0f * kDegToRad)) && Near(decision.headYaw, 70.0f * kDegToRad) &&
              Near(decision.gameYaw, 20.0f * kDegToRad) && decision.frameYawKnown && Near(decision.frameYaw, 20.0f * kDegToRad),
          "the 30th: provisional, and that same draw places contacts by the head (70 deg) instead of the control yaw (20 deg)");

    // The switch off: the proof still runs, nothing is written.
    frame = Frame(20.0f, 20.0f, 50.0f);
    frame.switchOn = false;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.use == TrackerUse::SwitchOff && decision.field == frame.field && decision.stepped &&
              decision.step == ProofStep::Agreed,
          "the switch off: the game's own heading, and the proof goes on being checked");

    // The player has no unit: the game stores nothing, and the field still holds what this lane wrote last.
    frame = Frame(20.0f, 20.0f, 50.0f);
    frame.field = FieldFromYaw(70.0f * kDegToRad);
    const ViewProof before = proof;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.use == TrackerUse::NoStore && decision.field == frame.field && !decision.stepped &&
              proof.agreed == before.agreed && proof.disagreed == before.disagreed && proof.stage == before.stage,
          "a field the game did not store in this call is neither replaced nor held against the proof");
    frame = Frame(20.0f, 20.0f, 50.0f);
    frame.fieldRead = false;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.use == TrackerUse::NoStore && !decision.stepped, "a heading that could not be read: nothing");
    frame = Frame(20.0f, 20.0f, 50.0f);
    frame.localPlayer = 2;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.use == TrackerUse::NotLocalUser && !decision.stepped && decision.field == frame.field,
          "another local player's sensor: nothing, not even a proof step");

    // A snap turn of 30 deg, the published frame one draw behind the field: one disagreeing sample, still used.
    frame = Frame(50.0f, 20.0f, 50.0f);
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::Disagreed && decision.use == TrackerUse::Used && proof.stage == ProofStage::Provisional &&
              Near(decision.field, FieldFromYaw(70.0f * kDegToRad)),
          "the draw of a snap turn, the frame one draw behind: counted, and the head published with that frame is still used");
    frame = Frame(50.0f, 50.0f, 50.0f);
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::Confirmed && decision.use == TrackerUse::Used && proof.stage == ProofStage::Confirmed &&
              Near(decision.field, FieldFromYaw(100.0f * kDegToRad)),
          "the next draw agrees 30 deg from where the proof began: CONFIRMED, and the head's heading has turned with the stick");

    // What is no reference still lets a proven lane substitute.
    frame = Frame(50.0f, 50.0f, -30.0f);
    frame.onFoot = false;
    frame.seatFrame = true;
    Forward(140.0f, 0.0f, frame.frameHeading);   // a seat's frame, somewhere else entirely
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::Used && proof.stage == ProofStage::Confirmed &&
              Near(decision.field, FieldFromYaw(20.0f * kDegToRad)),
          "in a seat nothing is proven or disproven, and the proof taken on foot carries: the head is used");
    frame = Frame(50.0f, 50.0f, -30.0f);
    frame.perspective = 1;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::Used,
          "seen from outside (the following camera): no reference, and the head is used");
    // A cinematic: not a reference, and not gameplay.
    frame = Frame(50.0f, 110.0f, 0.0f);
    frame.cinematic = true;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::NotGameplay && decision.field == frame.field &&
              proof.disagreed == 0,
          "a cinematic camera 60 deg off the control yaw is not held against the proof, and nothing is written");
    frame = Frame(50.0f, 110.0f, 0.0f);
    frame.perspective = 3;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::NotGameplay,
          "a perspective the game draws no sensor in: the same");
    frame = Frame(50.0f, 50.0f, 10.0f);
    frame.playerState = false;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::NoPlayerState && decision.field == frame.field,
          "the player's state unreadable: no reference and no substitution");
    // A stale head sample: no reference, the game's own heading.
    frame = Frame(50.0f, 50.0f, 10.0f);
    frame.nowUs = frame.sample.stampUs + head_relative::kMaxSampleAgeUs + 1;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::Stale && decision.field == frame.field,
          "a head sample older than 100 ms: no reference and no substitution");
    frame = Frame(50.0f, 50.0f, 10.0f);
    frame.sampleRead = false;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::NoSample && !decision.frameYawKnown,
          "no coherent head publication: no reference and no substitution");
    frame = Frame(50.0f, 50.0f, 10.0f);
    frame.generation = 0;
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::NoReference && decision.use == TrackerUse::OtherGeneration,
          "a hook from another load of the title: no reference and no substitution");

    // The view frame stops being the control yaw: 60 disagreements withdraw the proof, then the game's own tracker.
    TrackerUse last = TrackerUse::Used;
    ProofStep step = ProofStep::Agreed;
    for (int i = 0; i < static_cast<int>(kProofWithdrawRun); ++i)
    {
        frame = Frame(50.0f, 140.0f, 0.0f);
        decision = DecideTracker(frame, proof, hold);
        last = decision.use;
        step = decision.step;
    }
    Check(step == ProofStep::Withdrawn && last == TrackerUse::Unproven && decision.field == frame.field && !ProofAllows(proof),
          "sixty draws with the frame 90 deg off the control yaw: withdrawn on the sixtieth, and that draw is the game's own");
    bool stillOwn = true;
    for (int i = 0; i < 300; ++i)
    {
        frame = Frame(50.0f, 50.0f, 25.0f);
        decision = DecideTracker(frame, proof, hold);
        stillOwn = stillOwn && decision.use == TrackerUse::Unproven && decision.field == frame.field;
    }
    Check(stillOwn, "after the withdrawal 300 agreeing draws at rest do not bring the substitution back");
    frame = Frame(95.0f, 95.0f, 25.0f);
    decision = DecideTracker(frame, proof, hold);
    Check(decision.step == ProofStep::Confirmed && decision.use == TrackerUse::Used &&
              Near(decision.field, FieldFromYaw(120.0f * kDegToRad)),
          "a stick turn that agrees confirms it again, and the head is used from that draw");
}

// THE HEAD IS NEVER HELD AGAINST THE GAME. The proof compares the control yaw with the VIEW FRAME; the head may
// face anywhere, and the hand is not an input at all (no field of a TrackerFrame carries it). A simulator whose
// poses are not set yet can report the head half a turn from everything else (the other titles' logs read
// "head minus aim = 180.0" there): that must cost nothing here, neither a disagreement nor a refusal, and the
// contacts are simply placed by where that head faces.
void TestHeadFarFromTheGame()
{
    for (const float headDeg : {180.0f, -180.0f, 179.5f, 90.0f, -135.0f})
    {
        ViewProof proof;
        HeadingHold hold;
        bool clean = true;
        TrackerDecision decision;
        for (uint32_t i = 0; i < kProofSamples; ++i)
        {
            decision = DecideTracker(Frame(20.0f, 20.0f, headDeg), proof, hold);
            clean = clean && decision.stepped && proof.disagreed == 0 &&
                    (decision.step == ProofStep::Agreed || decision.step == ProofStep::Provisional);
        }
        Check(clean && proof.stage == ProofStage::Provisional && decision.use == TrackerUse::Used &&
                  Near(WrapPi(YawFromField(decision.field) - (20.0f + headDeg) * kDegToRad), 0.0f, 2.0e-4f),
              "a head far from the control yaw (up to half a turn): proven at rest all the same, no disagreement, and "
              "the contacts are placed by that head");
        decision = DecideTracker(Frame(65.0f, 65.0f, headDeg), proof, hold);
        Check(decision.step == ProofStep::Confirmed && decision.use == TrackerUse::Used && proof.disagreed == 0 &&
                  Near(WrapPi(YawFromField(decision.field) - (65.0f + headDeg) * kDegToRad), 0.0f, 2.0e-4f),
              "... and a stick turn confirms it whatever the head faces");
    }
}
} // namespace

int main()
{
    TestSites();
    TestFreshStore();
    TestProof();
    TestHeadFrame();
    TestPlacement();
    TestAcceptance();
    TestRefusals();
    TestDecision();
    TestHeadFarFromTheGame();
    std::printf("halo ce head relative tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
