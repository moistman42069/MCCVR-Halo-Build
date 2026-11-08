// Reach: the motion tracker and the sound listener follow the head (src/common/reach_head_relative_logic.h).
// For each: the decode of the game's own sites (offsets read from its instructions, the agreements between
// independent sites, every refusal), and what a player checks in a headset, as geometry -- on the game's OWN arithmetic
// (the tracker's placement, the listener's matrix rows and its transform), not on a model of it.
#include "reach_head_relative_logic.h"

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
float Wrap(float a)
{
    while (a > head_relative::kPi)
        a -= 2.0f * head_relative::kPi;
    while (a <= -head_relative::kPi)
        a += 2.0f * head_relative::kPi;
    return a;
}

using namespace reach_head_relative;
using head_relative::HeadingHold;
using head_relative::HeadSample;
using head_relative::kDegToRad;
using head_relative::kPi;
using head_relative::Use;

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
void PutRel(std::vector<uint8_t>& bytes, size_t opcode, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(opcode) + 5));
    std::memcpy(bytes.data() + opcode + 1, &rel, 4);
}

// The image as retail haloreach.dll lays it out (the offsets from the module base).
constexpr int64_t kUpdateRva = 0x2ED0C8, kSiteRva = 0x2ED187, kFirstRva = 0x2EC164, kSecondRva = 0x2EC404,
                  kThirdRva = 0x2ECEB0, kHalfPiRva = 0xA8B004;
constexpr uint32_t kHeading = 0x13D4, kUser = 0x13B8;

struct Image
{
    std::vector<uint8_t> site, update, first, third;
    uint8_t halfPi[4]{};
};
Image Retail()
{
    Image image;
    image.site = PatternBytes(kTrackerHeadingSitePattern);
    image.site.resize(kSiteBytes, 0);
    const int32_t halfPiRel = static_cast<int32_t>(kHalfPiRva - (kSiteRva + static_cast<int64_t>(kSiteHalfPiNext)));
    std::memcpy(image.site.data() + kSiteHalfPiDisp, &halfPiRel, 4);
    PutU32(image.site, kSiteHeadingDisp, kHeading);
    PutRel(image.site, kSiteCallFirst, kSiteRva, kFirstRva);
    PutRel(image.site, kSiteCallSecond, kSiteRva, kSecondRva);
    PutRel(image.site, kSiteCallThird, kSiteRva, kThirdRva);
    image.update = PatternBytes(kTrackerUpdateHeadPattern);
    PutU32(image.update, kUpdateUserDisp, kUser);
    image.first = PatternBytes(kTrackerFirstReaderHeadPattern);
    image.first.resize(kFirstReaderBytes, 0x90);
    std::memcpy(image.first.data() + kFirstReaderRead, kFirstReaderReadOpcode, sizeof(kFirstReaderReadOpcode));
    PutU32(image.first, kFirstReaderReadDisp, kHeading);
    image.third.assign(kThirdReaderBytes, 0x90);
    std::memcpy(image.third.data() + kThirdReaderRead, kThirdReaderReadOpcode, sizeof(kThirdReaderReadOpcode));
    PutU32(image.third, kThirdReaderReadDisp, kHeading);
    const float value = kHalfPi;
    std::memcpy(image.halfPi, &value, 4);
    return image;
}
TrackerBind Decode(const Image& image, TrackerSites& sites, int64_t siteFunctionRva = kUpdateRva,
                   int64_t firstRva = kFirstRva)
{
    return DecodeTrackerSites(image.site.data(), kSiteRva, image.update.data(), kUpdateRva, siteFunctionRva,
                              image.first.data(), firstRva, sites);
}

void TestSites()
{
    Check(PatternBytes(kTrackerHeadingSitePattern).size() + 4 == kSiteBytes, "the store site's length is its pattern plus the last rel32");
    Check(PatternBytes(kTrackerUpdateHeadPattern).size() == kUpdateHeadBytes, "the update head's length is its pattern");
    Check(PatternBytes(kTrackerFirstReaderHeadPattern).size() == kFirstReaderHeadBytes &&
              kFirstReaderHeadBytes <= kFirstReaderRead,
          "the first reader's head pattern ends before its read of the heading");

    const Image retail = Retail();
    TrackerSites sites;
    Check(Decode(retail, sites) == TrackerBind::Bound, "retail's layout decodes");
    Check(sites.headingOffset == kHeading && sites.userOffset == kUser, "the heading and the user index are read from the instructions");
    Check(sites.firstReaderRva == kFirstRva && sites.thirdReaderRva == kThirdRva && sites.halfPiRva == kHalfPiRva,
          "the hook, the other reader and the constant are where the site points");
    Check(CheckTrackerReaders(retail.first.data(), retail.third.data(), retail.halfPi, sites) == TrackerBind::Bound,
          "both readers read the stored field and the constant is pi/2");

    TrackerSites untouched;
    Check(DecodeTrackerSites(nullptr, kSiteRva, retail.update.data(), kUpdateRva, kUpdateRva, retail.first.data(),
                             kFirstRva, untouched) == TrackerBind::SiteMissing,
          "no unique store site: nothing binds");
    Check(DecodeTrackerSites(retail.site.data(), kSiteRva, nullptr, kUpdateRva, kUpdateRva, retail.first.data(),
                             kFirstRva, untouched) == TrackerBind::UpdateHeadMissing,
          "no unique update head: nothing binds");
    Check(DecodeTrackerSites(retail.site.data(), kSiteRva, retail.update.data(), kUpdateRva, kUpdateRva, nullptr,
                             kFirstRva, untouched) == TrackerBind::FirstHeadMissing,
          "no unique first reader: nothing binds");
    Check(Decode(retail, untouched, kUpdateRva + 0x40) == TrackerBind::SiteOutsideUpdate,
          "a store inside another function than the matched update is refused");
    Check(Decode(retail, untouched, -1) == TrackerBind::SiteOutsideUpdate, "a store in no function is refused");
    Check(Decode(retail, untouched, kUpdateRva, kFirstRva + 0x10) == TrackerBind::CallMismatch,
          "a call that lands elsewhere than the matched head is refused");
    Check(untouched.headingOffset == 0 && untouched.firstReaderRva == 0, "a refusal fills nothing");

    Image odd = retail;
    PutU32(odd.site, kSiteHeadingDisp, 0x13D5);
    Check(Decode(odd, untouched) == TrackerBind::Implausible, "a heading that is not float-aligned is refused");
    odd = retail;
    PutU32(odd.update, kUpdateUserDisp, kHeading + 8);
    Check(Decode(odd, untouched) == TrackerBind::Implausible, "a user index past the heading is refused");

    // The neighbouring field: the likeliest way a patch goes wrong is a reader that moved and a store that did not.
    Image moved = retail;
    PutU32(moved.first, kFirstReaderReadDisp, kHeading + 4);
    Check(CheckTrackerReaders(moved.first.data(), moved.third.data(), moved.halfPi, sites) == TrackerBind::FieldMismatch,
          "the first reader reading the neighbouring field is refused");
    moved = retail;
    PutU32(moved.third, kThirdReaderReadDisp, kHeading - 4);
    Check(CheckTrackerReaders(moved.first.data(), moved.third.data(), moved.halfPi, sites) == TrackerBind::FieldMismatch,
          "the third reader reading the neighbouring field is refused");
    moved = retail;
    moved.first[kFirstReaderRead + 4] = 0x96;  // another base register
    Check(CheckTrackerReaders(moved.first.data(), moved.third.data(), moved.halfPi, sites) == TrackerBind::ReaderMismatch,
          "a first reader that does not read the heading there is refused");
    Check(CheckTrackerReaders(retail.first.data(), nullptr, retail.halfPi, sites) == TrackerBind::ReaderMismatch,
          "an unreadable third reader is refused");
    const float pi = kPi;
    uint8_t wrong[4];
    std::memcpy(wrong, &pi, 4);
    Check(CheckTrackerReaders(retail.first.data(), retail.third.data(), wrong, sites) == TrackerBind::ConstantMismatch,
          "a constant that is not pi/2 is refused");
    bool named = true;
    for (int i = 0; i < static_cast<int>(TrackerBind::Count); ++i)
        named = named && TrackerBindName(static_cast<TrackerBind>(i))[0] != '?';
    Check(named, "every refusal has a name for the log");
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
    request.switchOn = true;
    request.outputUser = 0;
    request.gameplay = true;
    request.generation = 3;
    request.nowUs = 5000000 + 8000;  // the sample is 8 ms old
    request.sign = sign;
    request.offsetDeg = offsetDeg;
    return request;
}
// The game's yaw as its accessor returns it: [0, 2 pi).
float GameYaw(float yawDeg) { return WrapYaw(yawDeg * kDegToRad); }
// Where the game draws `contact` (a world offset from the player) with the aim at aimYawDeg and the head at
// headYawDeg: the bearing from the tracker's top, positive to its left. `substitute` = this lane ran.
float Drawn(const float contact[3], float aimYawDeg, float headYawDeg, bool substitute = true, int sign = 1,
            float offsetDeg = 0.0f)
{
    float field = FieldFromYaw(GameYaw(aimYawDeg));
    if (substitute)
    {
        HeadingHold hold;
        float replaced = field;
        if (TrackerField(Request(sign, offsetDeg), Head(headYawDeg), hold, field, replaced) != Use::Ok)
            return 1.0e9f;
        field = replaced;
    }
    float point[2];
    TrackerPoint(contact, field, point);
    return TrackerPointBearing(point);
}

void TestPlacement()
{
    // The game's own arithmetic: ahead is the tracker's top (0, -r), right is (+r, 0).
    float point[2];
    const float ahead[3]{5.0f, 0.0f, 0.0f}, right[3]{0.0f, -5.0f, 0.0f}, left[3]{0.0f, 5.0f, 0.0f};
    TrackerPoint(ahead, FieldFromYaw(0.0f), point);
    Check(Near(point[0], 0.0f) && Near(point[1], -5.0f), "facing +X, a contact ahead is at the tracker's top");
    TrackerPoint(right, FieldFromYaw(0.0f), point);
    Check(Near(point[0], 5.0f) && Near(point[1], 0.0f), "facing +X, a contact at -Y (the right) is at the tracker's right");
    TrackerPoint(left, FieldFromYaw(0.0f), point);
    Check(Near(TrackerPointBearing(point), 90.0f * kDegToRad), "a contact to the left has a positive bearing");
    float north[3]{0.0f, 7.0f, 0.0f};
    TrackerPoint(north, FieldFromYaw(90.0f * kDegToRad), point);
    Check(Near(point[0], 0.0f) && Near(point[1], -7.0f), "facing +Y, a contact at +Y is at the top");
    Check(Near(WrapYaw(-0.1f), kTwoPi - 0.1f) && Near(WrapYaw(kTwoPi + 0.25f), 0.25f) && WrapYaw(kTwoPi) < kTwoPi &&
              WrapYaw(-kTwoPi) >= 0.0f,
          "a yaw is wrapped into [0, 2 pi)");
}

void TestAcceptance()
{
    // (0) A contact straight ahead of the HEAD is at the tracker's top, whatever the aim is.
    bool ahead = true;
    for (float headYaw : {0.0f, 45.0f, 170.0f, -120.0f, 359.0f})
    {
        float contact[3];
        Forward(headYaw, 0.0f, contact);
        contact[0] *= 12.0f;
        contact[1] *= 12.0f;
        contact[2] = 4.0f;  // above the player: the tracker ignores height for the bearing
        for (float aimYaw : {0.0f, 30.0f, -75.0f, 179.0f, 270.0f})
            ahead = ahead && Near(Drawn(contact, aimYaw, headYaw), 0.0f, 1.0e-3f);
    }
    Check(ahead, "a contact straight ahead of the head is at the tracker's top, whatever the aim");

    // (1) Head still, sweep the gun left and right: the contact stays put.
    const float contact[3]{4.0f, 7.0f, 0.0f};
    const float reference = Drawn(contact, 0.0f, 20.0f);
    bool still = true;
    for (float aimYaw = -175.0f; aimYaw <= 175.0f; aimYaw += 13.0f)
        still = still && Near(Wrap(Drawn(contact, aimYaw, 20.0f) - reference), 0.0f, 1.0e-3f);
    Check(still, "head still, gun swept through 350 deg: the contact does not move");

    // (2) Gun still, turn the head by +90 deg (to the left): a fixed contact turns by -90 deg on the tracker.
    Check(Near(Wrap(Drawn(contact, 35.0f, 110.0f) - Drawn(contact, 35.0f, 20.0f)), -90.0f * kDegToRad, 1.0e-3f),
          "gun still, head +90 deg: the contact turns -90 deg on the tracker");

    // The control arm: without this lane the GUN moves the contact and the head does not.
    Check(Near(Wrap(Drawn(contact, 60.0f, 20.0f, false) - Drawn(contact, 0.0f, 20.0f, false)), -60.0f * kDegToRad,
               1.0e-3f) &&
              Near(Wrap(Drawn(contact, 0.0f, 110.0f, false) - Drawn(contact, 0.0f, 20.0f, false)), 0.0f, 1.0e-3f),
          "control arm: on the game's own tracker a 60 deg gun sweep moves the contact 60 deg and the head moves nothing");

    // Head and gun together: the lane changes nothing (what a flat player sees).
    Check(Near(Wrap(Drawn(contact, 77.0f, 77.0f) - Drawn(contact, 77.0f, 0.0f, false)), 0.0f, 1.0e-3f),
          "head along the aim: exactly the game's own tracker");

    // sign -1 mirrors the head's turn (the live safety value), and the offset is added after it.
    Check(Near(Wrap(Drawn(contact, 0.0f, 90.0f, true, -1) - Drawn(contact, 0.0f, 0.0f, true, -1)), 90.0f * kDegToRad,
               1.0e-3f),
          "sign -1: head +90 deg turns the contact +90 deg (mirrored)");
    Check(Near(Wrap(Drawn(contact, 0.0f, 20.0f, true, 1, 30.0f) - Drawn(contact, 0.0f, 20.0f)), -30.0f * kDegToRad,
               1.0e-3f),
          "an offset of +30 deg turns the tracker's heading 30 deg further left");
}

void TestRefusals()
{
    const float native = FieldFromYaw(GameYaw(40.0f));
    const HeadSample head = Head(130.0f);
    HeadingHold hold;
    float field = 123.0f, headYaw = 0.0f;
    Check(TrackerField(Request(), head, hold, native, field, &headYaw) == Use::Ok &&
              Near(field, FieldFromYaw(130.0f * kDegToRad)) && Near(headYaw, 130.0f * kDegToRad),
          "used: the field is the head's yaw plus pi/2");

    const auto refused = [&](TrackerRequest request, HeadSample sample, Use why, const char* what) {
        HeadingHold fresh;
        float out = 123.0f;
        Check(TrackerField(request, sample, fresh, native, out) == why && out == 123.0f, what);
    };
    TrackerRequest request = Request();
    request.switchOn = false;
    refused(request, head, Use::SwitchOff, "the switch off: the game's own heading");
    request = Request();
    request.outputUser = 1;
    refused(request, head, Use::NotLocalUser, "another output user: the game's own heading");
    request.outputUser = -1;
    refused(request, head, Use::NotLocalUser, "no output user: the game's own heading");
    request = Request();
    request.gameplay = false;
    refused(request, head, Use::NotGameplay, "a cinematic: the game's own heading");
    request = Request();
    request.nowUs = head.stampUs + head_relative::kMaxSampleAgeUs + 1;
    refused(request, head, Use::Stale, "a head sample older than 100 ms: the game's own heading");
    request = Request();
    request.generation = 4;
    refused(request, head, Use::OtherGeneration, "a head sample from another load: the game's own heading");
    request.generation = 0;
    refused(request, head, Use::OtherGeneration, "a hook from another load (generation 0): the game's own heading");
    request = Request();
    request.nowUs = head.stampUs - 1;
    refused(request, head, Use::FromTheFuture, "a head sample stamped after now: the game's own heading");
    refused(Request(), HeadSample{}, Use::NoSample, "nothing published yet: the game's own heading");
    HeadSample notLive = head;
    notLive.valid = false;
    refused(Request(), notLive, Use::NoSample, "the head view was not live in that frame: the game's own heading");
    HeadSample nan = head;
    nan.forward[0] = std::numeric_limits<float>::quiet_NaN();
    refused(Request(), nan, Use::NoHeading, "a non-finite head: the game's own heading");
    refused(Request(), Head(10.0f, 88.0f), Use::NoHeading, "looking straight up before any heading was known: the game's own");
    {
        HeadingHold fresh;
        float out = 123.0f;
        Check(TrackerField(Request(), head, fresh, std::numeric_limits<float>::infinity(), out) == Use::NoHeading &&
                  out == 123.0f,
              "a non-finite native field is left alone");
    }

    // Looking straight up or down keeps the last heading the head had.
    HeadingHold held;
    float out = 0.0f;
    Check(TrackerField(Request(), Head(200.0f, -20.0f), held, native, out) == Use::Ok &&
              Near(out, FieldFromYaw(200.0f * kDegToRad)),
          "a head pitched 20 deg down: its heading");
    Check(TrackerField(Request(), Head(75.0f, -89.0f), held, native, out) == Use::Ok &&
              Near(out, FieldFromYaw(200.0f * kDegToRad)),
          "then straight down: the last heading is held, not the noise of a vertical forward");
    // Head pitch and roll never reach the tracker: the same heading at any pitch below the limit.
    bool flat = true;
    for (float pitch : {-70.0f, -30.0f, 0.0f, 45.0f, 79.0f})
    {
        HeadingHold each;
        float value = 0.0f;
        flat = flat && TrackerField(Request(), Head(300.0f, pitch), each, native, value) == Use::Ok &&
               Near(value, FieldFromYaw(300.0f * kDegToRad), 1.0e-3f);
    }
    Check(flat, "head pitch does not change the tracker's heading");

    // What is written is always in the game's own range for the field, whatever the head and the offset are.
    bool inRange = true;
    for (float headYaw = -720.0f; headYaw <= 720.0f; headYaw += 37.0f)
        for (float offset : {-180.0f, 0.0f, 180.0f})
        {
            HeadingHold each;
            float value = 0.0f;
            inRange = inRange && TrackerField(Request(1, offset), Head(headYaw), each, native, value) == Use::Ok &&
                      value >= kHalfPi && value < kHalfPi + kTwoPi;
        }
    Check(inRange, "the written field stays in [pi/2, 5 pi/2), the range the game's own yaw gives it");
}

// ==== the sound listener ============================================================================================
// The image as retail haloreach.dll lays it out (offsets from the module base).
constexpr int64_t kListenerUpdateRva = 0x28820, kListenerStoreRva = 0x28C79, kSoundGlobalsRva = 0xC1A3E8,
                  kBuilderRva = 0xA7BEC, kTransformRva = 0xA8F7C;
constexpr uint32_t kListenerStride = 0x54, kListenerHeader = 0xBC, kListenerMatrix = 0xC4, kListenerVelocity = 0xF8,
                   kListenerFooter = 0x104, kListenerValid = 0x108, kListenerLocation = 0xC0;

struct ListenerImage
{
    std::vector<uint8_t> head, store, writer, builder, transform;
};
void PutRip(std::vector<uint8_t>& bytes, size_t disp, size_t next, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(next)));
    std::memcpy(bytes.data() + disp, &rel, 4);
}
ListenerImage RetailListener()
{
    ListenerImage image;
    image.head = PatternBytes(kListenerUpdateHeadPattern);
    PutRip(image.head, kListenerHeadGlobalsDisp, kListenerHeadGlobalsNext, kListenerUpdateRva, kSoundGlobalsRva);
    image.head[kListenerHeadStride] = static_cast<uint8_t>(kListenerStride);
    image.store = PatternBytes(kListenerStoreSitePattern);
    PutU32(image.store, kStoreValidDisp, kListenerValid);
    PutU32(image.store, kStoreMatrixDisp, kListenerMatrix);
    PutU32(image.store, kStoreValidNextDisp, kListenerValid + 1);
    PutRel(image.store, kStoreCallBuilder, kListenerStoreRva, kBuilderRva);
    PutU32(image.store, kStoreVelocityDisp, kListenerVelocity);
    PutRel(image.store, kStoreCallTransform, kListenerStoreRva, kTransformRva);
    PutU32(image.store, kStoreLocationDisp, kListenerLocation);
    PutRip(image.store, kStoreGlobalsDisp, kStoreGlobalsNext, kListenerStoreRva, kSoundGlobalsRva);
    image.writer = PatternBytes(kListenerSignatureWriterPattern);
    image.writer[kWriterStride] = static_cast<uint8_t>(kListenerStride);
    PutU32(image.writer, kWriterHeaderDisp, kListenerHeader);
    PutU32(image.writer, kWriterFooterDisp, kListenerFooter);
    image.builder.assign(kMatrixBuilderBytes, 0x90);
    std::memcpy(image.builder.data(), kMatrixBuilderHead, sizeof(kMatrixBuilderHead));
    std::memcpy(image.builder.data() + kMatrixBuilderTailAt, kMatrixBuilderTail, sizeof(kMatrixBuilderTail));
    image.transform.assign(kVelocityTransformBytes, 0x90);
    std::memcpy(image.transform.data() + kVelocityTransformDotsAt, kVelocityTransformDots,
                sizeof(kVelocityTransformDots));
    return image;
}
ListenerBind DecodeListener(const ListenerImage& image, ListenerSites& sites,
                            int64_t storeFunctionRva = kListenerUpdateRva)
{
    return DecodeListenerSites(image.head.data(), kListenerUpdateRva, image.store.data(), kListenerStoreRva,
                               storeFunctionRva, image.writer.data(), sites);
}

void TestListenerSites()
{
    Check(PatternBytes(kListenerUpdateHeadPattern).size() == kListenerHeadBytes &&
              PatternBytes(kListenerStoreSitePattern).size() == kListenerStoreBytes &&
              PatternBytes(kListenerSignatureWriterPattern).size() == kListenerWriterBytes,
          "each listener pattern has the length the header says");
    Check(kMatrixBuilderTailAt + sizeof(kMatrixBuilderTail) == kMatrixBuilderBytes &&
              kVelocityTransformDotsAt + sizeof(kVelocityTransformDots) == kVelocityTransformBytes,
          "the builder's tail and the transform's dot products end where their functions end");
    uint32_t headerText = 0, footerText = 0;
    std::memcpy(&headerText, "dhsl", 4);
    std::memcpy(&footerText, "tfsl", 4);
    Check(headerText == kListenerHeaderSignature && footerText == kListenerFooterSignature,
          "the signatures are 'lshd' and 'lsft' as the game stores them");

    const ListenerImage retail = RetailListener();
    ListenerSites sites;
    Check(DecodeListener(retail, sites) == ListenerBind::Bound, "retail's listener layout decodes");
    Check(sites.stride == kListenerStride && sites.headerOffset == kListenerHeader &&
              sites.matrixOffset == kListenerMatrix && sites.velocityOffset == kListenerVelocity &&
              sites.footerOffset == kListenerFooter && sites.validOffset == kListenerValid,
          "the record's offsets are read from the instructions");
    Check(sites.globalsRva == kSoundGlobalsRva && sites.builderRva == kBuilderRva && sites.transformRva == kTransformRva,
          "the globals pointer and both callees are where the sites point");
    Check(CheckListenerHelpers(retail.builder.data(), retail.transform.data()) == ListenerBind::Bound,
          "the callees are the matrix builder and the transform");

    ListenerSites untouched;
    Check(DecodeListenerSites(nullptr, kListenerUpdateRva, retail.store.data(), kListenerStoreRva, kListenerUpdateRva,
                              retail.writer.data(), untouched) == ListenerBind::UpdateHeadMissing,
          "no unique update head: nothing binds");
    Check(DecodeListenerSites(retail.head.data(), kListenerUpdateRva, nullptr, kListenerStoreRva, kListenerUpdateRva,
                              retail.writer.data(), untouched) == ListenerBind::StoreSiteMissing,
          "no unique store: nothing binds");
    Check(DecodeListenerSites(retail.head.data(), kListenerUpdateRva, retail.store.data(), kListenerStoreRva,
                              kListenerUpdateRva, nullptr, untouched) == ListenerBind::WriterMissing,
          "no unique signature writer: nothing binds");
    Check(DecodeListener(retail, untouched, kListenerUpdateRva + 0x100) == ListenerBind::StoreOutsideUpdate &&
              DecodeListener(retail, untouched, -1) == ListenerBind::StoreOutsideUpdate,
          "a store outside the matched update is refused");
    Check(untouched.stride == 0 && untouched.globalsRva == 0, "a refusal fills nothing");

    ListenerImage odd = retail;
    PutRip(odd.store, kStoreGlobalsDisp, kStoreGlobalsNext, kListenerStoreRva, kSoundGlobalsRva + 8);
    Check(DecodeListener(odd, untouched) == ListenerBind::GlobalsMismatch,
          "the head and the store loading the globals from different places is refused");
    odd = retail;
    odd.writer[kWriterStride] = 0x58;
    Check(DecodeListener(odd, untouched) == ListenerBind::StrideMismatch,
          "the update and the signature writer disagreeing on a listener's size is refused");
    // The neighbouring field, one at a time: each stored field must sit where the signature-delimited record puts it.
    const struct
    {
        size_t at;
        uint32_t value;
        const char* what;
    } moved[]{
        {kStoreMatrixDisp, kListenerMatrix + 4, "a matrix that does not start 8 after the header is refused"},
        {kStoreVelocityDisp, kListenerVelocity - 4, "a velocity that does not follow the matrix is refused"},
        {kStoreValidDisp, kListenerValid + 4, "a valid byte that does not follow the footer is refused"},
        {kStoreValidNextDisp, kListenerValid + 2, "a second flag that is not the byte after valid is refused"},
        {kStoreLocationDisp, kListenerLocation + 2, "a location that is not right after the header is refused"},
    };
    for (const auto& move : moved)
    {
        odd = retail;
        PutU32(odd.store, move.at, move.value);
        Check(DecodeListener(odd, untouched) == ListenerBind::RecordMismatch, move.what);
    }
    odd = retail;
    PutU32(odd.writer, kWriterFooterDisp, kListenerFooter + 4);
    Check(DecodeListener(odd, untouched) == ListenerBind::RecordMismatch,
          "a footer that does not follow the velocity is refused");
    odd = retail;
    PutU32(odd.writer, kWriterHeaderDisp, 0);
    Check(DecodeListener(odd, untouched) == ListenerBind::Implausible, "a header at offset 0 is refused");
    odd = retail;
    PutU32(odd.writer, kWriterFooterDisp, kListenerHeader + kListenerStride);
    Check(DecodeListener(odd, untouched) == ListenerBind::Implausible, "a footer outside its own listener is refused");
    odd = retail;
    odd.head[kListenerHeadStride] = 0x30;
    odd.writer[kWriterStride] = 0x30;
    Check(DecodeListener(odd, untouched) == ListenerBind::Implausible,
          "a listener too small to hold a matrix, a velocity and two signatures is refused");

    ListenerImage helper = retail;
    helper.builder[4] = 0x00;  // not `mov dword [rcx], 1.0f`
    Check(CheckListenerHelpers(helper.builder.data(), helper.transform.data()) == ListenerBind::HelperMismatch,
          "a first callee that is not the matrix builder is refused");
    helper = retail;
    helper.builder[kMatrixBuilderTailAt + 9] = 0x20;  // up stored somewhere else than +0x1C
    Check(CheckListenerHelpers(helper.builder.data(), helper.transform.data()) == ListenerBind::HelperMismatch,
          "a builder that puts up elsewhere is refused");
    helper = retail;
    helper.transform[kVelocityTransformDotsAt + 10] = 0x10;  // the first dot against another row
    Check(CheckListenerHelpers(helper.builder.data(), helper.transform.data()) == ListenerBind::HelperMismatch,
          "a second callee that is not the three dot products is refused");
    Check(CheckListenerHelpers(nullptr, retail.transform.data()) == ListenerBind::HelperMismatch &&
              CheckListenerHelpers(retail.builder.data(), nullptr) == ListenerBind::HelperMismatch,
          "an unreadable callee is refused");
    bool named = true;
    for (int i = 0; i < static_cast<int>(ListenerBind::Count); ++i)
        named = named && ListenerBindName(static_cast<ListenerBind>(i))[0] != '?';
    for (int i = 0; i < static_cast<int>(ListenerUse::Count); ++i)
        named = named && ListenerUseName(static_cast<ListenerUse>(i))[0] != '?';
    Check(named, "every listener refusal has a name for the log");
}

bool Near3(const float a[3], float x, float y, float z, float tol = 1.0e-4f)
{
    return Near(a[0], x, tol) && Near(a[1], y, tol) && Near(a[2], z, tol);
}
bool Same3(const float a[3], const float b[3], float tol = 1.0e-4f) { return Near3(a, b[0], b[1], b[2], tol); }
// A frame as the game's builder makes one: forward at yaw / pitch, then rolled about that forward (positive roll
// leans the top of the head to the right), left = up x forward.
ListenerBasis Frame(float yawDeg, float pitchDeg = 0.0f, float rollDeg = 0.0f)
{
    ListenerBasis basis;
    Forward(yawDeg, pitchDeg, basis.forward);
    float upright[3];
    Forward(yawDeg, pitchDeg + 90.0f, upright);  // perpendicular to forward, in its vertical plane
    head_relative::RotateAbout(upright, basis.forward, rollDeg * kDegToRad, basis.up);
    LeftOf(basis.forward, basis.up, basis.left);
    return basis;
}
// Output user 0's listener as the game writes it for an aim at aimYaw / aimPitch, moving at `worldVelocity`.
ListenerRecord Listener(float aimYawDeg, float aimPitchDeg, const float worldVelocity[3])
{
    ListenerRecord record;
    record.header = kListenerHeaderSignature;
    record.footer = kListenerFooterSignature;
    record.valid = 1;
    record.scale = 1.0f;
    record.basis = Frame(aimYawDeg, aimPitchDeg);
    ToListenerSpace(record.basis, worldVelocity, record.velocity);
    return record;
}
HeadSample HeadFrame(float yawDeg, float pitchDeg = 0.0f, float rollDeg = 0.0f)
{
    HeadSample sample = Head(yawDeg, pitchDeg);
    const ListenerBasis frame = Frame(yawDeg, pitchDeg, rollDeg);
    std::memcpy(sample.up, frame.up, sizeof(sample.up));
    return sample;
}
ListenerRequest Hearing()
{
    ListenerRequest request;
    request.switchOn = true;
    request.gameplay = true;
    request.generation = 3;
    request.nowUs = 5000000 + 8000;
    return request;
}
const float kStill[3]{0.0f, 0.0f, 0.0f};
// Where the game hears a source at `offset` from the listener (its own transform against the listener's rows),
// with the aim and the head as given. `follow` = this lane ran.
bool Heard(const float offset[3], float aimYawDeg, const HeadSample& head, float out[3], bool follow = true)
{
    ListenerRecord record = Listener(aimYawDeg, 0.0f, kStill);
    if (follow)
    {
        ListenerBasis basis;
        float velocity[3];
        if (ListenerOrientation(Hearing(), head, record, basis, velocity) != ListenerUse::Used)
            return false;
        record.basis = basis;
    }
    ToListenerSpace(record.basis, offset, out);
    return true;
}

void TestListenerBasis()
{
    ListenerBasis basis;
    const float east[3]{1, 0, 0}, top[3]{0, 0, 1};
    Check(HeadBasis(east, top, basis) && Near3(basis.forward, 1, 0, 0) && Near3(basis.up, 0, 0, 1) &&
              Near3(basis.left, 0, 1, 0),
          "a level head facing +X: forward +X, up +Z, left +Y (the game's up x forward)");
    const ListenerBasis leaning = Frame(40.0f, 25.0f, 30.0f);
    Check(HeadBasis(leaning.forward, leaning.up, basis) && Same3(basis.forward, leaning.forward) &&
              Same3(basis.up, leaning.up) && Same3(basis.left, leaning.left) && BasisAsBuilt(1.0f, basis),
          "a pitched and rolled head keeps its pitch and its roll, and is a frame the game's builder would make");
    // An up that is not quite perpendicular (two samples of a moving head) is squared against the forward.
    const float skewUp[3]{0.2f, 0.0f, 1.0f}, longForward[3]{3.0f, 0.0f, 0.0f};
    Check(HeadBasis(longForward, skewUp, basis) && Near3(basis.forward, 1, 0, 0) && Near3(basis.up, 0, 0, 1) &&
              BasisAsBuilt(1.0f, basis),
          "forward is normalised and up made perpendicular to it");
    ListenerBasis untouched = Frame(10.0f);
    const ListenerBasis before = untouched;
    const float zero[3]{0, 0, 0}, nan3[3]{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    Check(!HeadBasis(east, east, untouched) && !HeadBasis(zero, top, untouched) && !HeadBasis(east, zero, untouched) &&
              !HeadBasis(nan3, top, untouched) && !HeadBasis(east, nan3, untouched) &&
              Same3(untouched.forward, before.forward) && Same3(untouched.up, before.up),
          "an up along the forward, a zero or a non-finite vector makes no frame, and the output is untouched");

    ListenerBasis built = Frame(123.0f, -35.0f, 12.0f);
    Check(BasisAsBuilt(1.0f, built), "the builder's own output is recognised");
    Check(!BasisAsBuilt(2.0f, built) && !BasisAsBuilt(0.0f, built), "another scale is not the builder's output");
    ListenerBasis broken = built;
    broken.left[0] = -broken.left[0];
    broken.left[1] = -broken.left[1];
    broken.left[2] = -broken.left[2];
    Check(!BasisAsBuilt(1.0f, broken), "a mirrored left (forward x up) is not the builder's output");
    broken = built;
    broken.forward[0] *= 1.5f;
    Check(!BasisAsBuilt(1.0f, broken), "a forward that is not unit length is refused");
    broken = built;
    std::memcpy(broken.up, broken.forward, sizeof(broken.up));
    Check(!BasisAsBuilt(1.0f, broken), "an up along the forward is refused");
    broken = built;
    broken.up[2] = std::numeric_limits<float>::infinity();
    Check(!BasisAsBuilt(1.0f, broken), "a non-finite row is refused");

    const float world[3]{2.0f, -3.0f, 5.0f};
    float local[3], back[3];
    ToListenerSpace(built, world, local);
    FromListenerSpace(built, local, back);
    Check(Same3(back, world, 1.0e-3f), "into a listener's space and back is the same world vector");
    const ListenerBasis level = Frame(0.0f);
    const float ahead[3]{4, 0, 0}, toLeft[3]{0, 4, 0}, above[3]{0, 0, 4};
    ToListenerSpace(level, ahead, local);
    bool axes = Near3(local, 4, 0, 0);
    ToListenerSpace(level, toLeft, local);
    axes = axes && Near3(local, 0, 4, 0);
    ToListenerSpace(level, above, local);
    Check(axes && Near3(local, 0, 0, 4), "listener space is (ahead, left, above), as the game's transform computes it");
}

void TestListenerAcceptance()
{
    float heard[3];
    // (0) A sound straight ahead of the HEAD is heard straight ahead, whatever the gun points at.
    bool ahead = true;
    for (float headYaw : {0.0f, 60.0f, 175.0f, -110.0f})
    {
        float source[3];
        Forward(headYaw, 0.0f, source);
        for (float& component : source)
            component *= 9.0f;
        for (float aimYaw : {0.0f, 45.0f, -90.0f, 180.0f})
            ahead = ahead && Heard(source, aimYaw, HeadFrame(headYaw), heard) && Near3(heard, 9, 0, 0, 1.0e-3f);
    }
    Check(ahead, "a sound straight ahead of the head is heard straight ahead, whatever the aim");

    // (1) Head still, sweep the gun: a fixed sound does not move around the listener.
    const float source[3]{3.0f, 6.0f, 1.0f};
    float reference[3];
    bool still = Heard(source, 0.0f, HeadFrame(25.0f), reference);
    for (float aimYaw = -170.0f; aimYaw <= 170.0f; aimYaw += 20.0f)
        still = still && Heard(source, aimYaw, HeadFrame(25.0f), heard) && Same3(heard, reference, 1.0e-3f);
    Check(still, "head still, gun swept through 340 deg: a fixed sound stays where it was heard");

    // (2) Gun still, turn the head +90 deg (to the left): a sound that was ahead is now on the right.
    const float east[3]{5.0f, 0.0f, 0.0f};
    Check(Heard(east, 30.0f, HeadFrame(0.0f), heard) && Near3(heard, 5, 0, 0, 1.0e-3f) &&
              Heard(east, 30.0f, HeadFrame(90.0f), heard) && Near3(heard, 0, -5, 0, 1.0e-3f),
          "gun still, head +90 deg: a sound that was ahead is heard on the right");

    // The control arm: without this lane the GUN moves the sound and the head does not.
    float control[3];
    Check(Heard(east, 0.0f, HeadFrame(0.0f), control, false) && Near3(control, 5, 0, 0, 1.0e-3f) &&
              Heard(east, 90.0f, HeadFrame(0.0f), control, false) && Near3(control, 0, -5, 0, 1.0e-3f) &&
              Heard(east, 0.0f, HeadFrame(90.0f), control, false) && Near3(control, 5, 0, 0, 1.0e-3f),
          "control arm: the game's own listener turns with the gun and ignores the head");

    // Pitch and roll count for sound. Head tipped back 90 deg (looking straight up): what was ahead is below.
    Check(Heard(east, 0.0f, HeadFrame(0.0f, 89.9f), heard) && Near(heard[2], -5.0f, 2.0e-2f) && Near(heard[1], 0.0f, 1.0e-3f),
          "looking straight up: a sound on the horizon ahead is heard below (pitch counts)");
    // Head leaned 90 deg to the right: a sound overhead is heard on the left.
    const float overhead[3]{0.0f, 0.0f, 5.0f};
    Check(Heard(overhead, 0.0f, HeadFrame(0.0f, 0.0f, 90.0f), heard) && Near3(heard, 0, 5, 0, 1.0e-3f),
          "head leaned 90 deg to the right: a sound overhead is heard on the left (roll counts)");
    // Head along the gun, level: exactly the game's own listener.
    Check(Heard(source, 77.0f, HeadFrame(77.0f), heard) && Heard(source, 77.0f, HeadFrame(0.0f), control, false) &&
              Same3(heard, control, 1.0e-3f),
          "head along the aim: exactly the game's own listener");

    // The world velocity the game stored is the same world velocity afterwards (Doppler does not turn with the head).
    const float moving[3]{4.0f, -2.0f, 0.5f};
    const ListenerRecord native = Listener(140.0f, -10.0f, moving);
    ListenerBasis basis;
    float velocity[3], world[3];
    Check(ListenerOrientation(Hearing(), HeadFrame(-35.0f, 20.0f, 15.0f), native, basis, velocity) == ListenerUse::Used,
          "a moving listener is turned to the head");
    FromListenerSpace(basis, velocity, world);
    Check(Same3(world, moving, 1.0e-3f) && BasisAsBuilt(1.0f, basis),
          "its velocity is the same world velocity, re-expressed in the head's frame");
}

void TestListenerRefusals()
{
    const float moving[3]{1.0f, 2.0f, 3.0f};
    const ListenerRecord native = Listener(40.0f, 5.0f, moving);
    const HeadSample head = HeadFrame(130.0f, -15.0f, 8.0f);
    const auto refused = [&](ListenerRequest request, const HeadSample& sample, const ListenerRecord& record,
                             ListenerUse why, const char* what) {
        ListenerBasis basis = Frame(1.0f);
        const ListenerBasis before = basis;
        float velocity[3]{7.0f, 7.0f, 7.0f};
        Check(ListenerOrientation(request, sample, record, basis, velocity) == why && Same3(basis.forward, before.forward) &&
                  Same3(basis.left, before.left) && Same3(basis.up, before.up) && Near3(velocity, 7, 7, 7),
              what);
    };
    ListenerRequest request = Hearing();
    request.switchOn = false;
    refused(request, head, native, ListenerUse::SwitchOff, "the switch off: the game's own listener");
    request = Hearing();
    request.gameplay = false;
    refused(request, head, native, ListenerUse::NotGameplay, "a cinematic: the game's own listener");
    request = Hearing();
    request.nowUs = head.stampUs + head_relative::kMaxSampleAgeUs + 1;
    refused(request, head, native, ListenerUse::Stale, "a head sample older than 100 ms: the game's own listener");
    request = Hearing();
    request.generation = 4;
    refused(request, head, native, ListenerUse::OtherGeneration, "a head sample from another load: the game's own listener");
    request = Hearing();
    request.nowUs = head.stampUs - 1;
    refused(request, head, native, ListenerUse::FromTheFuture, "a head sample stamped after now: the game's own listener");
    refused(Hearing(), HeadSample{}, native, ListenerUse::NoSample, "nothing published yet: the game's own listener");
    HeadSample notLive = head;
    notLive.valid = false;
    refused(Hearing(), notLive, native, ListenerUse::NoSample, "the head view was not live in that frame: the game's own listener");
    HeadSample flat = head;
    std::memcpy(flat.up, flat.forward, sizeof(flat.up));
    refused(Hearing(), flat, native, ListenerUse::NoHeadBasis, "a head whose up lies along its forward: the game's own listener");
    HeadSample nan = head;
    nan.up[1] = std::numeric_limits<float>::quiet_NaN();
    refused(Hearing(), nan, native, ListenerUse::NoHeadBasis, "a non-finite head: the game's own listener");

    ListenerRecord record = native;
    record.valid = 0;
    refused(Hearing(), head, record, ListenerUse::NoListener, "the game built no listener this update: nothing is written");
    record = native;
    record.header = 0;
    refused(Hearing(), head, record, ListenerUse::NotAListener, "a record without the header signature: nothing is written");
    record = native;
    record.footer = kListenerHeaderSignature;
    refused(Hearing(), head, record, ListenerUse::NotAListener, "a record without the footer signature: nothing is written");
    refused(Hearing(), head, ListenerRecord{}, ListenerUse::NotAListener, "no sound globals yet: nothing is written");
    record = native;
    record.scale = 2.0f;
    refused(Hearing(), head, record, ListenerUse::UnexpectedBasis, "a matrix with another scale: nothing is written");
    record = native;
    record.basis.left[0] += 0.5f;
    refused(Hearing(), head, record, ListenerUse::UnexpectedBasis, "a matrix whose left is not up x forward: nothing is written");
    record = native;
    record.velocity[2] = std::numeric_limits<float>::infinity();
    refused(Hearing(), head, record, ListenerUse::UnexpectedBasis, "a non-finite stored velocity: nothing is written");
}
} // namespace

int main()
{
    TestSites();
    TestPlacement();
    TestAcceptance();
    TestRefusals();
    TestListenerSites();
    TestListenerBasis();
    TestListenerAcceptance();
    TestListenerRefusals();
    std::printf("reach head relative tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
