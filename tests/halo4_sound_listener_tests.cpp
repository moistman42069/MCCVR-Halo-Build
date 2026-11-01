// Halo 4: the sound listener follows the head (src/common/halo4_sound_listener_logic.h). RETAIL-DERIVED LEAD, NOT
// H4EK-VERIFIED: these tests cover the decode of the four sites and its refusals, the value agreement and the
// proof the hook takes at run time before it changes anything, what the listener is given (on the game's own
// builder, transform and mirror, modelled here as they were disassembled), every refusal, and the measurement
// the dev report prints. They build their images from the header's own constants, so they cannot see a constant
// that is wrong against the game: tools/verify-halo4-sound-listener.py does that on the pinned module.
#include "halo4_sound_listener_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
bool Near3(const float a[3], const float b[3], float tol = 1.0e-4f)
{
    return Near(a[0], b[0], tol) && Near(a[1], b[1], tol) && Near(a[2], b[2], tol);
}

using namespace halo4_sound_listener;
using head_relative::Cross;
using head_relative::Dot;
using head_relative::kDegToRad;

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

// The image as retail halo4.dll lays it out (offsets from the module base).
constexpr int64_t kUpdateRva = 0x59B00, kSiteRva = 0x59F6E, kHandOffRva = 0x9B8250, kBuilderRva = 0x11BFAC,
                  kTransformRva = 0x11D368, kGlobalsRva = 0x107BE08, kSignMaskRva = 0xD29740;

struct Image
{
    std::vector<uint8_t> head, site, writer, handOff, builder, transform;
    uint8_t signMask[4]{};
};
Image Retail()
{
    Image image;
    image.head = PatternBytes(kListenerUpdateHeadPattern);
    PutRip(image.head, kUpdateGlobalsDisp, kUpdateGlobalsNext, kUpdateRva, kGlobalsRva);
    PutRip(image.head, kUpdateSignMaskDisp, kUpdateSignMaskNext, kUpdateRva, kSignMaskRva);
    image.site = PatternBytes(kListenerHandOffSitePattern);
    PutBranch(image.site, kSiteCallBuilder, kSiteRva, kBuilderRva);
    PutBranch(image.site, kSiteCallTransform, kSiteRva, kTransformRva);
    PutBranch(image.site, kSiteCallHandOff, kSiteRva, kHandOffRva);
    image.writer = PatternBytes(kListenerSignatureWriterPattern);
    image.handOff = PatternBytes(kListenerHandOffPattern);
    image.builder.assign(kMatrixBuilder, kMatrixBuilder + kMatrixBuilderBytes);
    image.transform.assign(kVelocityTransformBytes, 0x90);
    std::memcpy(image.transform.data() + kVelocityTransformDotsAt, kVelocityTransformDots,
                sizeof(kVelocityTransformDots));
    std::memcpy(image.signMask, &kMirrorSignMask, 4);
    return image;
}
ListenerBind Decode(const Image& image, ListenerSites& sites, int64_t siteFunctionRva = kUpdateRva,
                    int64_t handOffRva = kHandOffRva, int64_t exportRva = kHandOffRva)
{
    return DecodeListenerSites(image.head.data(), kUpdateRva, image.site.data(), kSiteRva, siteFunctionRva,
                               image.writer.data(), image.handOff.data(), handOffRva, exportRva, sites);
}

void TestSites()
{
    Check(PatternBytes(kListenerUpdateHeadPattern).size() == kUpdateHeadBytes &&
              PatternBytes(kListenerHandOffSitePattern).size() == kSiteBytes &&
              PatternBytes(kListenerSignatureWriterPattern).size() == kWriterBytes &&
              PatternBytes(kListenerHandOffPattern).size() == kHandOffBytes,
          "each pattern has the length the header says");
    Check(kSiteReturn == kSiteCallHandOff + 5 && kSiteReturn == kSiteBytes,
          "the return address is the instruction after the site's call of the hand-off");
    Check(kVelocityTransformDotsAt + sizeof(kVelocityTransformDots) == kVelocityTransformBytes,
          "the transform's dot products are its last bytes");

    const Image retail = Retail();
    ListenerSites sites;
    Check(Decode(retail, sites) == ListenerBind::Bound, "retail's layout decodes");
    Check(sites.stride == 0x58 && sites.headerOffset == 0x3C && sites.locationOffset == 0x40 &&
              sites.matrixOffset == 0x44 && sites.velocityOffset == 0x78 && sites.footerOffset == 0x84 &&
              sites.validOffset == 0x88,
          "the record: 0x58 long, 'lshd' +0x3C, location +0x40, matrix +0x44, velocity +0x78, 'lsft' +0x84, built +0x88");
    Check(sites.updateRva == kUpdateRva && sites.globalsRva == kGlobalsRva && sites.signMaskRva == kSignMaskRva &&
              sites.builderRva == kBuilderRva && sites.transformRva == kTransformRva &&
              sites.handOffRva == kHandOffRva && sites.returnRva == 0x5A056,
          "the update, the globals pointer, the sign mask, the two helpers, the hook and the return address");
    Check(sites.updateFrameBytes == 0x258, "the update's return address is 0x258 above its body's rsp");
    Check(CheckListenerHelpers(retail.builder.data(), retail.transform.data(), retail.signMask) == ListenerBind::Bound,
          "retail's builder, transform and sign mask are accepted");

    ListenerSites untouched;
    Check(DecodeListenerSites(nullptr, kUpdateRva, retail.site.data(), kSiteRva, kUpdateRva, retail.writer.data(),
                              retail.handOff.data(), kHandOffRva, kHandOffRva, untouched) == ListenerBind::UpdateHeadMissing,
          "no unique update head: nothing binds");
    Check(DecodeListenerSites(retail.head.data(), kUpdateRva, nullptr, kSiteRva, kUpdateRva, retail.writer.data(),
                              retail.handOff.data(), kHandOffRva, kHandOffRva, untouched) == ListenerBind::SiteMissing,
          "no unique site: nothing binds");
    Check(DecodeListenerSites(retail.head.data(), kUpdateRva, retail.site.data(), kSiteRva, kUpdateRva, nullptr,
                              retail.handOff.data(), kHandOffRva, kHandOffRva, untouched) == ListenerBind::WriterMissing,
          "no unique signature writer: nothing binds");
    Check(DecodeListenerSites(retail.head.data(), kUpdateRva, retail.site.data(), kSiteRva, kUpdateRva,
                              retail.writer.data(), nullptr, kHandOffRva, kHandOffRva, untouched) == ListenerBind::HandOffMissing,
          "no unique hand-off: nothing binds");
    Check(Decode(retail, untouched, kUpdateRva + 0x10) == ListenerBind::SiteOutsideUpdate &&
              Decode(retail, untouched, -1) == ListenerBind::SiteOutsideUpdate,
          "a site outside the function whose head matched is refused");
    Check(Decode(retail, untouched, kUpdateRva, kHandOffRva + 0x10) == ListenerBind::CallMismatch,
          "a site that calls something else than the matched hand-off is refused");
    Check(Decode(retail, untouched, kUpdateRva, kHandOffRva, -1) == ListenerBind::ExportMismatch,
          "an image that does not export the middleware's listener-position call is refused");
    Check(Decode(retail, untouched, kUpdateRva, kHandOffRva, 0x9B87C0) == ListenerBind::ExportMismatch,
          "an image that exports another function under that name is refused");
    Check(std::strstr(kListenerHandOffExport, "SetListenerPosition@SoundEngine@AK") != nullptr,
          "the export is the middleware's listener-position call");

    Image odd = retail;
    odd.writer[kWriterStride] = 0x54;
    Check(Decode(odd, untouched) == ListenerBind::StrideMismatch,
          "an update and a signature writer that disagree on a listener's size are refused");
    odd = retail;
    odd.head[kUpdateStride] = 0;
    Check(Decode(odd, untouched) == ListenerBind::StrideMismatch, "a zero stride is refused");
    odd = retail;
    odd.writer[kWriterHeaderDisp] = 0x3D;
    Check(Decode(odd, untouched) == ListenerBind::Implausible, "a misaligned header is refused");
    odd = retail;
    PutU32(odd.writer, kWriterFooterDisp, 0x3C + 0x58);
    Check(Decode(odd, untouched) == ListenerBind::Implausible, "a footer outside the record is refused");
    // Each stored field moved to its neighbour.
    const size_t byteFields[]{kSiteMatrixDisp, kSiteVelocityDisp, kSiteLocationDisp, kSitePositionStore,
                              kSitePositionStoreHigh};
    bool everyFieldRefused = true;
    for (const size_t field : byteFields)
    {
        odd = retail;
        odd.site[field] = static_cast<uint8_t>(odd.site[field] + 4);
        everyFieldRefused = everyFieldRefused && Decode(odd, untouched) == ListenerBind::RecordMismatch;
    }
    odd = retail;
    PutU32(odd.site, kSiteValidDisp, 0x8C);
    everyFieldRefused = everyFieldRefused && Decode(odd, untouched) == ListenerBind::RecordMismatch;
    odd = retail;
    PutU32(odd.site, kSiteValidNextDisp, 0x8A);
    everyFieldRefused = everyFieldRefused && Decode(odd, untouched) == ListenerBind::RecordMismatch;
    odd = retail;
    PutU32(odd.writer, kWriterFooterDisp, 0x88);
    everyFieldRefused = everyFieldRefused && Decode(odd, untouched) == ListenerBind::RecordMismatch;
    Check(everyFieldRefused, "a stored field that is not where the signature-delimited record puts it is refused");

    // The nine floats: every store, every source and every mirror.
    bool everyNineRefused = true;
    for (int k = 0; k < 9; ++k)
    {
        odd = retail;
        odd.site[kSiteNine[k].store] = static_cast<uint8_t>(odd.site[kSiteNine[k].store] + 4);
        everyNineRefused = everyNineRefused && Decode(odd, untouched) == ListenerBind::NineMismatch;
        odd = retail;
        odd.site[kSiteNine[k].source] = static_cast<uint8_t>(odd.site[kSiteNine[k].source] + 4);
        everyNineRefused = everyNineRefused && Decode(odd, untouched) == ListenerBind::NineMismatch;
        if (kSiteNine[k].mirror)
        {
            odd = retail;
            odd.site[kSiteNine[k].mirror + 1] = 0x28;  // movaps in place of xorps: this x is not mirrored
            everyNineRefused = everyNineRefused && Decode(odd, untouched) == ListenerBind::NineMismatch;
        }
    }
    odd = retail;
    odd.site[kSitePositionHigh] = static_cast<uint8_t>(odd.site[kSitePositionHigh] + 4);
    everyNineRefused = everyNineRefused && Decode(odd, untouched) == ListenerBind::NineMismatch;
    Check(everyNineRefused, "nine floats that are not forward, up, position with each x mirrored are refused");
    Check(kSiteNine[0].mirror && !kSiteNine[1].mirror && !kSiteNine[2].mirror && kSiteNine[3].mirror &&
              !kSiteNine[4].mirror && !kSiteNine[5].mirror && kSiteNine[6].mirror && !kSiteNine[7].mirror &&
              !kSiteNine[8].mirror,
          "the x of the forward, of the up and of the position is mirrored, and nothing else");

    odd = retail;
    PutBranch(odd.site, kSiteCallTransform, kSiteRva, kBuilderRva);
    Check(Decode(odd, untouched) == ListenerBind::Implausible, "one callee for both helpers is refused");
    odd = retail;
    PutU32(odd.head, kUpdateFrameSub, 0x234);
    Check(Decode(odd, untouched) == ListenerBind::Implausible, "a misaligned update frame is refused");
    Check(untouched.handOffRva == 0 && untouched.returnRva == 0 && untouched.stride == 0 && untouched.globalsRva == 0,
          "a refusal fills nothing");

    // The helpers.
    odd = retail;
    odd.builder[0x70] ^= 1;
    Check(CheckListenerHelpers(odd.builder.data(), retail.transform.data(), retail.signMask) ==
              ListenerBind::HelperMismatch,
          "a builder with another byte is refused");
    odd = retail;
    odd.transform[kVelocityTransformDotsAt + 9] ^= 1;
    Check(CheckListenerHelpers(retail.builder.data(), odd.transform.data(), retail.signMask) ==
              ListenerBind::HelperMismatch,
          "a transform with other dot products is refused");
    Check(CheckListenerHelpers(nullptr, retail.transform.data(), retail.signMask) == ListenerBind::HelperMismatch &&
              CheckListenerHelpers(retail.builder.data(), nullptr, retail.signMask) == ListenerBind::HelperMismatch,
          "a callee that is not a function of its own is refused");
    const uint8_t notTheSignBit[4]{0xFF, 0xFF, 0xFF, 0x7F};
    Check(CheckListenerHelpers(retail.builder.data(), retail.transform.data(), notTheSignBit) ==
                  ListenerBind::MirrorMismatch &&
              CheckListenerHelpers(retail.builder.data(), retail.transform.data(), nullptr) ==
                  ListenerBind::MirrorMismatch,
          "a mirror constant that is not the sign bit is refused");
    bool named = true;
    for (int why = 0; why < static_cast<int>(ListenerBind::Count); ++why)
        named = named && std::strcmp(ListenerBindName(static_cast<ListenerBind>(why)), "?") != 0;
    Check(named, "every bind refusal has a name");
}

// ---- the game, modelled as it was disassembled ----------------------------------------------------------------------
// The builder (retail +0x11BFAC): scale, forward, left, up, position.
struct GameMatrix
{
    float scale = 0.0f, forward[3]{}, left[3]{}, up[3]{}, position[3]{};
};
GameMatrix GameBuilder(const float forward[3], const float up[3])
{
    GameMatrix m;
    m.scale = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        m.forward[i] = forward[i];
        m.up[i] = up[i];
    }
    m.left[0] = forward[2] * up[1] - up[2] * forward[1];  // [rcx + 0x10]
    m.left[2] = forward[1] * up[0] - up[1] * forward[0];  // [rcx + 0x18]
    m.left[1] = up[2] * forward[0] - forward[2] * up[0];  // [rcx + 0x14]
    return m;
}
// The transform (retail +0x11D368), scale 1: three dot products against the rows.
void GameTransform(const GameMatrix& m, const float v[3], float out[3])
{
    out[0] = v[1] * m.forward[1] + v[0] * m.forward[0] + v[2] * m.forward[2];
    out[1] = v[1] * m.left[1] + v[0] * m.left[0] + v[2] * m.left[2];
    out[2] = v[1] * m.up[1] + v[0] * m.up[0] + v[2] * m.up[2];
}
// The site's nine floats (retail +0x59FDC .. +0x5A046).
void GameHandOff(const float forward[3], const float up[3], const float position[3], float nine[9])
{
    nine[0] = -forward[0];
    nine[1] = forward[1];
    nine[2] = forward[2];
    nine[3] = -up[0];
    nine[4] = up[1];
    nine[5] = up[2];
    nine[6] = -position[0];
    nine[7] = position[1];
    nine[8] = position[2];
}
// What the hand-off makes of the nine floats before it queues them (retail +0x9B8281 .. +0x9B82E8): the front made
// perpendicular to the top, both normalised.
void MiddlewareFrontTop(const float nine[9], float front[3], float top[3])
{
    const float dot = nine[0] * nine[3] + nine[1] * nine[4] + nine[2] * nine[5];
    for (int i = 0; i < 3; ++i)
    {
        front[i] = nine[i] - dot * nine[3 + i];
        top[i] = nine[3 + i];
    }
    const float frontLength = head_relative::Length(front), topLength = head_relative::Length(top);
    for (int i = 0; i < 3; ++i)
    {
        front[i] /= frontLength;
        top[i] /= topLength;
    }
}

// A camera's forward and up from a yaw (counter-clockwise from +X seen from above), a pitch (up positive) and a
// lean (toward its left positive), in degrees.
void Camera(float yawDeg, float pitchDeg, float leanDeg, float forward[3], float up[3])
{
    const float y = yawDeg * kDegToRad, p = pitchDeg * kDegToRad, r = leanDeg * kDegToRad;
    forward[0] = std::cos(p) * std::cos(y);
    forward[1] = std::cos(p) * std::sin(y);
    forward[2] = std::sin(p);
    const float level[3]{-std::sin(p) * std::cos(y), -std::sin(p) * std::sin(y), std::cos(p)};
    float left[3];
    Cross(level, forward, left);
    for (int i = 0; i < 3; ++i)
        up[i] = level[i] * std::cos(r) + left[i] * std::sin(r);
}
// One build of user 0's listener by the game, from a camera: the record and the nine floats.
struct Build
{
    ListenerRecord record;
    float nine[9]{};
};
Build GameBuild(const float forward[3], const float up[3], const float position[3], const float worldVelocity[3])
{
    Build build;
    const GameMatrix matrix = GameBuilder(forward, up);
    build.record.readable = true;
    build.record.header = kListenerHeaderSignature;
    build.record.footer = kListenerFooterSignature;
    build.record.valid = 1;
    build.record.scale = matrix.scale;
    for (int i = 0; i < 3; ++i)
    {
        build.record.basis.forward[i] = matrix.forward[i];
        build.record.basis.left[i] = matrix.left[i];
        build.record.basis.up[i] = matrix.up[i];
        build.record.position[i] = position[i];
    }
    GameTransform(matrix, worldVelocity, build.record.velocity);
    GameHandOff(forward, up, position, build.nine);
    return build;
}
constexpr uint32_t kGeneration = 7;
constexpr uint64_t kNowUs = 5000000;
// The head as a frame published it: its orientation, where it is, and the game's camera (the aim) of that frame.
HeadFrame Frame(float yawDeg, float pitchDeg, float leanDeg, const float position[3], const float aimForward[3],
                const float aimPosition[3])
{
    HeadFrame frame;
    Camera(yawDeg, pitchDeg, leanDeg, frame.sample.forward, frame.sample.up);
    frame.sample.stampUs = kNowUs - 8000;
    frame.sample.generation = kGeneration;
    frame.sample.valid = true;
    // The two eye cameras: half an eye distance to each side of the head, along its left.
    float left[3];
    Cross(frame.sample.up, frame.sample.forward, left);
    for (int i = 0; i < 3; ++i)
    {
        frame.position[i] = position[i];
        frame.aimForward[i] = aimForward[i];
        frame.aimPosition[i] = aimPosition[i];
        frame.eyePosition[0][i] = position[i] + left[i] * 0.0105f;
        frame.eyePosition[1][i] = position[i] - left[i] * 0.0105f;
    }
    return frame;
}
ListenerRequest Request()
{
    ListenerRequest request;
    request.fromUpdate = true;
    request.user = 0;
    request.proven = true;
    request.switchOn = true;
    request.gameplay = true;
    request.movePosition = true;
    request.generation = kGeneration;
    request.nowUs = kNowUs;
    return request;
}
// Where a world point is heard from a listener: in its own frame, by the game's own transform.
void Heard(const ListenerBasis& basis, const float listenerPosition[3], const float point[3], float out[3])
{
    GameMatrix matrix;
    matrix.scale = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        matrix.forward[i] = basis.forward[i];
        matrix.left[i] = basis.left[i];
        matrix.up[i] = basis.up[i];
    }
    const float offset[3]{point[0] - listenerPosition[0], point[1] - listenerPosition[1],
                          point[2] - listenerPosition[2]};
    GameTransform(matrix, offset, out);
}

void TestGameModel()
{
    float forward[3], up[3];
    Camera(37.0f, 12.0f, 8.0f, forward, up);
    const GameMatrix matrix = GameBuilder(forward, up);
    float left[3];
    LeftOf(forward, up, left);
    Check(Near3(left, matrix.left, 1.0e-6f), "LeftOf is the builder's third row: up x forward");
    ListenerBasis basis;
    for (int i = 0; i < 3; ++i)
    {
        basis.forward[i] = matrix.forward[i];
        basis.left[i] = matrix.left[i];
        basis.up[i] = matrix.up[i];
    }
    Check(BasisAsBuilt(matrix.scale, basis), "what the builder writes is recognised as built");
    const float v[3]{1.5f, -2.25f, 0.75f};
    float game[3], ours[3], back[3];
    GameTransform(matrix, v, game);
    ToListenerSpace(basis, v, ours);
    FromListenerSpace(basis, ours, back);
    Check(Near3(game, ours, 1.0e-6f) && Near3(back, v, 1.0e-5f),
          "ToListenerSpace is the game's transform, and FromListenerSpace undoes it");
    const float position[3]{10.0f, -20.0f, 3.0f};
    float gameNine[9], ourNine[9], f[3], u[3], p[3];
    GameHandOff(forward, up, position, gameNine);
    ToHandOff(forward, up, position, ourNine);
    FromHandOff(gameNine, f, u, p);
    Check(std::memcmp(gameNine, ourNine, sizeof(gameNine)) == 0 && Near3(f, forward, 0.0f) && Near3(u, up, 0.0f) &&
              Near3(p, position, 0.0f),
          "ToHandOff is the site's mirror bit for bit, and FromHandOff undoes it");
}

void TestAgreement()
{
    float forward[3], up[3];
    Camera(123.0f, -9.0f, 0.0f, forward, up);
    const float position[3]{45.5f, 12.25f, -3.0f}, velocity[3]{0.5f, 1.0f, -0.25f};
    const Build build = GameBuild(forward, up, position, velocity);
    Check(CheckRecord(build.record, build.nine) == RecordState::Agrees,
          "the record the game built agrees with the nine floats it hands on");

    ListenerRecord record = build.record;
    record.readable = false;
    Check(CheckRecord(record, build.nine) == RecordState::Unreadable, "no sound globals: unreadable");
    record = build.record;
    record.header = 0;
    Check(CheckRecord(record, build.nine) == RecordState::NotAListener, "no 'lshd': not a listener");
    record = build.record;
    record.footer = kListenerHeaderSignature;
    Check(CheckRecord(record, build.nine) == RecordState::NotAListener, "no 'lsft': not a listener");
    record = build.record;
    record.valid = 0;
    Check(CheckRecord(record, build.nine) == RecordState::NotBuilt, "a listener the game did not build this pass");
    record = build.record;
    record.scale = 2.0f;
    Check(CheckRecord(record, build.nine) == RecordState::UnexpectedBasis, "a scale that is not 1");
    record = build.record;
    record.basis.left[0] = -record.basis.left[0];
    record.basis.left[1] = -record.basis.left[1];
    record.basis.left[2] = -record.basis.left[2];
    Check(CheckRecord(record, build.nine) == RecordState::UnexpectedBasis, "a left that is not up x forward");
    record = build.record;
    record.velocity[1] = std::numeric_limits<float>::quiet_NaN();
    Check(CheckRecord(record, build.nine) == RecordState::UnexpectedBasis, "a velocity that is not finite");

    float nine[9];
    std::memcpy(nine, build.nine, sizeof(nine));
    nine[0] = -nine[0];
    Check(CheckRecord(build.record, nine) == RecordState::Disagrees, "a forward whose x is not mirrored disagrees");
    std::memcpy(nine, build.nine, sizeof(nine));
    nine[7] = std::nextafter(nine[7], 1000.0f);
    Check(CheckRecord(build.record, nine) == RecordState::Disagrees, "a position one step away disagrees");
    // Another camera's nine floats (two builds crossing on two threads would look like this).
    float otherForward[3], otherUp[3];
    Camera(10.0f, 0.0f, 0.0f, otherForward, otherUp);
    const Build other = GameBuild(otherForward, otherUp, position, velocity);
    Check(CheckRecord(build.record, other.nine) == RecordState::Disagrees, "another build's nine floats disagree");
    // +0 and -0 are one value: a forward along +Y has x = 0 in the record and -0 in the hand-off.
    Camera(90.0f, 0.0f, 0.0f, forward, up);
    forward[0] = 0.0f;
    up[0] = 0.0f;
    const Build alongY = GameBuild(forward, up, position, velocity);
    Check(CheckRecord(alongY.record, alongY.nine) == RecordState::Agrees, "a zero x agrees with its mirror");
    // The camera's own float drift: the band the stereo transaction admits for this record, and no more.
    Camera(123.0f, -9.0f, 0.0f, forward, up);
    float drifted[3]{forward[0] * 1.01f, forward[1] * 1.01f, forward[2] * 1.01f};
    const Build drift = GameBuild(drifted, up, position, velocity);
    ListenerResult out;
    const float head[3]{45.6f, 12.3f, -2.8f};
    Check(CheckRecord(drift.record, drift.nine) == RecordState::Agrees &&
              DecideListener(Request(), Frame(10.0f, 0.0f, 0.0f, head, forward, position), drift.nine, drift.record,
                             RecordState::Agrees, out) == ListenerUse::Used,
          "a camera basis one percent off unit length is still the game's");
    drifted[0] = forward[0] * 1.03f;
    drifted[1] = forward[1] * 1.03f;
    drifted[2] = forward[2] * 1.03f;
    const Build skewed = GameBuild(drifted, up, position, velocity);
    Check(CheckRecord(skewed.record, skewed.nine) == RecordState::UnexpectedBasis &&
              DecideListener(Request(), Frame(10.0f, 0.0f, 0.0f, head, forward, position), skewed.nine,
                             skewed.record, RecordState::Agrees, out) == ListenerUse::UnexpectedHandOff,
          "a basis outside the band the stereo transaction admits is not");
    bool named = true;
    for (int state = 0; state < static_cast<int>(RecordState::Count); ++state)
        named = named && std::strcmp(RecordStateName(static_cast<RecordState>(state)), "?") != 0;
    Check(named, "every record state has a name");
}

void TestProof()
{
    float forward[3], up[3];
    Camera(33.0f, 5.0f, 0.0f, forward, up);
    const float position[3]{1.0f, 2.0f, 3.0f}, velocity[3]{};
    const Build build = GameBuild(forward, up, position, velocity);

    HandOffProof proof;
    bool early = false;
    for (uint32_t i = 0; i + 1 < kProofCalls; ++i)
        early = early || StepProof(proof, RecordState::Agrees, build.nine) != ProofStep::Agreed || proof.proven;
    Check(!early, "one call short of the count proves nothing");
    Check(StepProof(proof, RecordState::Agrees, build.nine) == ProofStep::Proven && proof.proven,
          "the count of agreeing calls in a row proves it");
    Check(StepProof(proof, RecordState::Agrees, build.nine) == ProofStep::Agreed && proof.proven,
          "a proven hand-off that agrees stays proven");

    // A disagreement before the proof starts the count again.
    proof = HandOffProof{};
    for (uint32_t i = 0; i + 1 < kProofCalls; ++i)
        (void)StepProof(proof, RecordState::Agrees, build.nine);
    Check(StepProof(proof, RecordState::Disagrees, build.nine) == ProofStep::Disagreed && !proof.proven &&
              proof.agreed == 0,
          "a disagreement before the proof restarts it");
    Check(StepProof(proof, RecordState::Agrees, build.nine) == ProofStep::Agreed && !proof.proven,
          "and the next agreement is the first again");
    // Every state that is not agreement counts against.
    bool everyStateAgainst = true;
    for (int state = 1; state < static_cast<int>(RecordState::Count); ++state)
    {
        proof = HandOffProof{};
        (void)StepProof(proof, RecordState::Agrees, build.nine);
        everyStateAgainst = everyStateAgainst &&
                            StepProof(proof, static_cast<RecordState>(state), build.nine) == ProofStep::Disagreed &&
                            proof.agreed == 0;
    }
    Check(everyStateAgainst, "an unreadable, unsigned, unbuilt or unexpected record counts against like a disagreement");

    // The mirror must have been exercised: a forward with x = 0 compares 0 with -0 and shows nothing.
    float alongForward[3]{0.0f, 1.0f, 0.0f}, alongUp[3]{0.0f, 0.0f, 1.0f};
    const Build alongY = GameBuild(alongForward, alongUp, position, velocity);
    proof = HandOffProof{};
    bool provenBlind = false;
    for (uint32_t i = 0; i < kProofCalls * 3; ++i)
        provenBlind = provenBlind || StepProof(proof, RecordState::Agrees, alongY.nine) == ProofStep::Proven;
    Check(!provenBlind && !proof.proven, "agreement that never showed a mirrored x proves nothing, however long");
    Check(StepProof(proof, RecordState::Agrees, build.nine) == ProofStep::Proven,
          "and the first call with a mirrored x completes it");

    // Withdrawal: kProofWithdrawRun disagreements in a row, and only in a row.
    bool keptThrough = true;
    for (uint32_t i = 0; i + 1 < kProofWithdrawRun; ++i)
        keptThrough = keptThrough && StepProof(proof, RecordState::Disagrees, build.nine) == ProofStep::Disagreed &&
                      proof.proven;
    Check(keptThrough, "one disagreement short of the run keeps the proof");
    Check(StepProof(proof, RecordState::Agrees, build.nine) == ProofStep::Agreed && proof.disagreed == 0,
          "an agreement in between clears the run");
    for (uint32_t i = 0; i + 1 < kProofWithdrawRun; ++i)
        (void)StepProof(proof, RecordState::Disagrees, build.nine);
    Check(StepProof(proof, RecordState::Disagrees, build.nine) == ProofStep::Withdrawn && !proof.proven,
          "the run of disagreements withdraws it");
    bool again = false;
    for (uint32_t i = 0; i < kProofCalls; ++i)
        again = StepProof(proof, RecordState::Agrees, build.nine) == ProofStep::Proven;
    Check(again && proof.proven, "after a withdrawal the proof is taken again from nothing");
}

void TestListener()
{
    // The player stands at the origin of this picture; the game's camera (the aim) points along +X, level.
    const float cameraPosition[3]{100.0f, 50.0f, 10.0f};
    const float headPosition[3]{100.25f, 49.9f, 10.4f};  // the head: leaned and raised off the game's camera
    const float worldVelocity[3]{0.3f, -0.2f, 0.1f};
    float aimForward[3], aimUp[3];
    Camera(0.0f, 0.0f, 0.0f, aimForward, aimUp);
    const Build stock = GameBuild(aimForward, aimUp, cameraPosition, worldVelocity);
    const RecordState agrees = CheckRecord(stock.record, stock.nine);

    // The control arm: the game's own listener hears a source ahead of the AIM as ahead.
    const float ahead[3]{cameraPosition[0] + 5.0f, cameraPosition[1], cameraPosition[2]};
    float heard[3];
    Heard(stock.record.basis, stock.record.position, ahead, heard);
    Check(Near(heard[0], 5.0f) && Near(heard[1], 0.0f) && Near(heard[2], 0.0f),
          "control: the game's listener hears a source ahead of the aim straight ahead");

    // Head 90 degrees to the left of the aim (yaw +90), level.
    HeadFrame frame = Frame(90.0f, 0.0f, 0.0f, headPosition, aimForward, cameraPosition);
    ListenerResult result;
    Check(DecideListener(Request(), frame, stock.nine, stock.record, agrees, result) == ListenerUse::Used &&
              result.recordToo,
          "a proven, agreeing build in gameplay is given the head");
    Check(BasisAsBuilt(1.0f, result.basis), "the rows handed back are what the game's builder would write");
    Check(Near3(result.basis.forward, frame.sample.forward) && Near3(result.basis.up, frame.sample.up),
          "the rows are the head's forward and up");
    Check(Near3(result.position, headPosition), "the listener that sat at the game's camera now sits at the head");
    // Heard through the game's own transform with the rows the lane writes:
    Heard(result.basis, result.position, ahead, heard);
    Check(heard[1] < -4.0f && std::fabs(heard[0]) < 1.0f,
          "head turned 90 degrees left: the source ahead of the aim is heard on the right");
    const float aheadOfHead[3]{headPosition[0], headPosition[1] + 5.0f, headPosition[2]};
    Heard(result.basis, result.position, aheadOfHead, heard);
    Check(Near(heard[0], 5.0f) && Near(heard[1], 0.0f) && Near(heard[2], 0.0f),
          "a source straight ahead of the head is heard straight ahead");

    // Head still, the gun swept all the way round: nothing the listener carries moves.
    bool still = true;
    for (int aimYaw = -170; aimYaw <= 180; aimYaw += 35)
    {
        float sweptForward[3], sweptUp[3];
        Camera(static_cast<float>(aimYaw), 15.0f, 0.0f, sweptForward, sweptUp);
        const Build swept = GameBuild(sweptForward, sweptUp, cameraPosition, worldVelocity);
        const HeadFrame held = Frame(90.0f, 0.0f, 0.0f, headPosition, sweptForward, cameraPosition);
        ListenerResult now;
        still = still && DecideListener(Request(), held, swept.nine, swept.record,
                                        CheckRecord(swept.record, swept.nine), now) == ListenerUse::Used &&
                Near3(now.basis.forward, result.basis.forward) && Near3(now.basis.left, result.basis.left) &&
                Near3(now.basis.up, result.basis.up) && Near3(now.position, result.position);
    }
    Check(still, "head still and the gun swept all the way round: the listener does not move");

    // Pitch and lean count for sound.
    frame = Frame(0.0f, 60.0f, 0.0f, headPosition, aimForward, cameraPosition);
    DecideListener(Request(), frame, stock.nine, stock.record, agrees, result);
    const float horizon[3]{headPosition[0] + 5.0f, headPosition[1], headPosition[2]};
    Heard(result.basis, result.position, horizon, heard);
    Check(heard[2] < -4.0f, "looking up 60 degrees puts the horizon below");
    frame = Frame(0.0f, 0.0f, 90.0f, headPosition, aimForward, cameraPosition);
    DecideListener(Request(), frame, stock.nine, stock.record, agrees, result);
    const float overhead[3]{headPosition[0], headPosition[1], headPosition[2] + 5.0f};
    Heard(result.basis, result.position, overhead, heard);
    Check(std::fabs(heard[1]) > 4.0f && std::fabs(heard[2]) < 1.0f, "leaning the head puts overhead to the side");

    // The middleware's copy: the same forward, up and position under the game's own mirror.
    frame = Frame(63.0f, -20.0f, 12.0f, headPosition, aimForward, cameraPosition);
    DecideListener(Request(), frame, stock.nine, stock.record, agrees, result);
    float expected[9];
    GameHandOff(result.basis.forward, result.basis.up, result.position, expected);
    Check(std::memcmp(expected, result.handOff, sizeof(expected)) == 0,
          "the middleware is handed the record's forward, up and position, mirrored as the game mirrors its own");
    float front[3], top[3], gameFront[3], gameTop[3];
    MiddlewareFrontTop(result.handOff, front, top);
    MiddlewareFrontTop(expected, gameFront, gameTop);
    Check(Near3(front, gameFront, 1.0e-6f) && Near3(top, gameTop, 1.0e-6f) && Near(Dot(front, top), 0.0f, 1.0e-5f),
          "what the hand-off makes of it is a unit, perpendicular front and top");
    // Handedness under the mirror: a reflection flips a cross product, so the listener's RIGHT (forward x up in
    // the game) is top x front in the mirrored space, and a source keeps its side when it is mirrored the same way.
    {
        const float source[3]{headPosition[0] + 3.0f, headPosition[1] - 4.0f, headPosition[2] + 1.0f};
        float right[3], offset[3], mirroredRight[3];
        Cross(result.basis.forward, result.basis.up, right);
        for (int i = 0; i < 3; ++i)
            offset[i] = source[i] - result.position[i];
        const float side = Dot(offset, right);
        Cross(top, front, mirroredRight);
        const float mirroredOffset[3]{-offset[0], offset[1], offset[2]};
        Check(Near(Dot(mirroredOffset, mirroredRight), side, 1.0e-3f),
              "a source mirrored like the listener is on the same side of it (top x front is its right)");
    }

    // The velocity: the same world velocity, seen from the new rows.
    float world[3];
    FromListenerSpace(result.basis, result.velocity, world);
    Check(Near3(world, worldVelocity, 1.0e-5f), "the world velocity the game computed is unchanged");

    // The position: the game's own, moved by the head's offset from the game's camera of the same frame.
    const float seatListener[3]{90.0f, 60.0f, 12.0f};  // the game placed the listener somewhere else (a seat blend)
    const Build seat = GameBuild(aimForward, aimUp, seatListener, worldVelocity);
    frame = Frame(20.0f, 0.0f, 0.0f, headPosition, aimForward, cameraPosition);
    DecideListener(Request(), frame, seat.nine, seat.record, CheckRecord(seat.record, seat.nine), result);
    const float seatExpected[3]{seatListener[0] + 0.25f, seatListener[1] - 0.1f, seatListener[2] + 0.4f};
    Check(Near3(result.position, seatExpected),
          "a listener the game placed elsewhere keeps the game's placement plus the head's offset");
    ListenerRequest request = Request();
    request.movePosition = false;
    Check(DecideListener(request, frame, stock.nine, stock.record, agrees, result) == ListenerUse::Used &&
              Near3(result.position, cameraPosition, 0.0f) && Near(result.handOff[6], -cameraPosition[0], 0.0f),
          "with the position part off the listener stays where the game put it");

    // A build that ran while the mod's render held the camera copied an EYE camera: recognised by identity and
    // put at the head itself, not moved by the head's offset a second time.
    frame = Frame(20.0f, 5.0f, 0.0f, headPosition, aimForward, cameraPosition);
    for (int eye = 0; eye < 2; ++eye)
    {
        const Build fromEye = GameBuild(frame.sample.forward, frame.sample.up, frame.eyePosition[eye], worldVelocity);
        ListenerResult eyeResult;
        Check(BuiltFromEye(frame.eyePosition[eye], frame) &&
                  DecideListener(Request(), frame, fromEye.nine, fromEye.record,
                                 CheckRecord(fromEye.record, fromEye.nine), eyeResult) == ListenerUse::Used &&
                  eyeResult.fromEye && Near3(eyeResult.position, headPosition, 0.0f),
              eye ? "a listener copied from the right eye's camera is put at the head"
                  : "a listener copied from the left eye's camera is put at the head");
    }
    {
        float nearEye[3]{frame.eyePosition[0][0], frame.eyePosition[0][1], frame.eyePosition[0][2]};
        nearEye[2] = std::nextafter(nearEye[2], 1000.0f);
        Check(!BuiltFromEye(nearEye, frame) && !BuiltFromEye(cameraPosition, frame) && !BuiltFromEye(headPosition, frame),
              "only a position that IS an eye's counts: one step away, the camera's and the head's do not");
        HeadFrame noEyes = frame;
        for (int i = 0; i < 3; ++i)
            noEyes.eyePosition[0][i] = noEyes.eyePosition[1][i] = std::numeric_limits<float>::quiet_NaN();
        Check(!BuiltFromEye(frame.eyePosition[0], noEyes), "a frame that published no eye cameras matches nothing");
        DecideListener(Request(), frame, stock.nine, stock.record, agrees, result);
        Check(!result.fromEye, "a listener at the game's camera is not taken for an eye's");
        // An eye-built listener needs no camera position to measure an offset from.
        const Build fromEye = GameBuild(frame.sample.forward, frame.sample.up, frame.eyePosition[0], worldVelocity);
        HeadFrame noAim = frame;
        noAim.aimPosition[0] = std::numeric_limits<float>::quiet_NaN();
        Check(DecideListener(Request(), noAim, fromEye.nine, fromEye.record,
                             CheckRecord(fromEye.record, fromEye.nine), result) == ListenerUse::Used &&
                  Near3(result.position, headPosition, 0.0f),
              "an eye-built listener is placed without the camera's position");
        ListenerRequest still = Request();
        still.movePosition = false;
        Check(DecideListener(still, frame, fromEye.nine, fromEye.record, CheckRecord(fromEye.record, fromEye.nine),
                             result) == ListenerUse::Used &&
                  Near3(result.position, frame.eyePosition[0], 0.0f),
              "with the position part off even an eye-built listener stays where it was built");
    }

    // The record did not agree on this call (another build crossed it): the middleware still gets the head, from
    // the hand-off's own position, and the record is left alone.
    ListenerRecord crossed = stock.record;
    crossed.position[0] += 7.0f;
    frame = Frame(90.0f, 0.0f, 0.0f, headPosition, aimForward, cameraPosition);
    ListenerResult only;
    Check(DecideListener(Request(), frame, stock.nine, crossed, CheckRecord(crossed, stock.nine), only) ==
                  ListenerUse::HandOffOnly &&
              !only.recordToo && Near3(only.position, headPosition) && Near3(only.basis.forward, frame.sample.forward),
          "a record that differs on this call: the middleware's copy only, placed from the hand-off's own position");
    Check(ListenerChanged(ListenerUse::Used) && ListenerChanged(ListenerUse::HandOffOnly) &&
              !ListenerChanged(ListenerUse::Unproven) && !ListenerChanged(ListenerUse::SwitchOff),
          "only the two uses change anything");

    // Head on the aim, at the camera: the listener is the game's own.
    frame = Frame(0.0f, 0.0f, 0.0f, cameraPosition, aimForward, cameraPosition);
    DecideListener(Request(), frame, stock.nine, stock.record, agrees, result);
    Check(Near3(result.basis.forward, stock.record.basis.forward) && Near3(result.basis.left, stock.record.basis.left) &&
              Near3(result.basis.up, stock.record.basis.up) && Near3(result.position, stock.record.position) &&
              Near3(result.velocity, stock.record.velocity),
          "head on the aim and at the camera: the listener is the game's own");
}

void TestRefusals()
{
    const float cameraPosition[3]{1.0f, 2.0f, 3.0f}, headPosition[3]{1.1f, 2.1f, 3.3f}, velocity[3]{};
    float aimForward[3], aimUp[3];
    Camera(15.0f, 0.0f, 0.0f, aimForward, aimUp);
    const Build stock = GameBuild(aimForward, aimUp, cameraPosition, velocity);
    const RecordState agrees = CheckRecord(stock.record, stock.nine);
    const HeadFrame good = Frame(80.0f, 10.0f, 0.0f, headPosition, aimForward, cameraPosition);

    ListenerResult sentinel;
    sentinel.handOff[0] = 123.0f;
    sentinel.position[2] = -77.0f;
    sentinel.recordToo = true;
    const auto untouched = [&](const ListenerResult& out) {
        return std::memcmp(&out, &sentinel, sizeof(out)) == 0;
    };
    const auto refused = [&](const ListenerRequest& request, const HeadFrame& frame, const float nine[9],
                             ListenerUse expected) {
        ListenerResult out = sentinel;
        return DecideListener(request, frame, nine, stock.record, agrees, out) == expected && untouched(out);
    };

    ListenerRequest request = Request();
    request.fromUpdate = false;
    Check(refused(request, good, stock.nine, ListenerUse::OtherCaller), "a call that is not the update's is left alone");
    request = Request();
    request.user = 1;
    Check(refused(request, good, stock.nine, ListenerUse::NotLocalUser), "another user's listener is left alone");
    request.user = -1;
    Check(refused(request, good, stock.nine, ListenerUse::NotLocalUser), "no user is left alone");

    float nine[9];
    std::memcpy(nine, stock.nine, sizeof(nine));
    nine[1] *= 2.0f;
    Check(refused(Request(), good, nine, ListenerUse::UnexpectedHandOff), "a forward that is not unit length is refused");
    std::memcpy(nine, stock.nine, sizeof(nine));
    nine[3] = nine[0];
    nine[4] = nine[1];
    nine[5] = nine[2];
    Check(refused(Request(), good, nine, ListenerUse::UnexpectedHandOff), "an up along the forward is refused");
    std::memcpy(nine, stock.nine, sizeof(nine));
    nine[8] = std::numeric_limits<float>::infinity();
    Check(refused(Request(), good, nine, ListenerUse::UnexpectedHandOff), "a position that is not finite is refused");

    request = Request();
    request.proven = false;
    Check(refused(request, good, stock.nine, ListenerUse::Unproven), "nothing is changed before the proof");
    request = Request();
    request.switchOn = false;
    Check(refused(request, good, stock.nine, ListenerUse::SwitchOff), "the switch off is the game's own listener");

    HeadFrame frame = good;
    frame.sample.valid = false;
    Check(refused(Request(), frame, stock.nine, ListenerUse::NoSample), "a head that was not live is refused");
    frame = good;
    frame.sample.stampUs = 0;
    Check(refused(Request(), frame, stock.nine, ListenerUse::NoSample), "nothing published yet is refused");
    frame = good;
    frame.sample.generation = kGeneration + 1;
    Check(refused(Request(), frame, stock.nine, ListenerUse::OtherGeneration), "another load's sample is refused");
    request = Request();
    request.generation = 0;
    Check(refused(request, good, stock.nine, ListenerUse::OtherGeneration), "no generation is refused");
    frame = good;
    frame.sample.stampUs = kNowUs + 1;
    Check(refused(Request(), frame, stock.nine, ListenerUse::FromTheFuture), "a sample from the future is refused");
    frame = good;
    frame.sample.stampUs = kNowUs - head_relative::kMaxSampleAgeUs - 1;
    Check(refused(Request(), frame, stock.nine, ListenerUse::Stale), "a sample older than the limit is refused");
    frame.sample.stampUs = kNowUs - head_relative::kMaxSampleAgeUs;
    ListenerResult out;
    Check(DecideListener(Request(), frame, stock.nine, stock.record, agrees, out) == ListenerUse::Used,
          "a sample exactly at the limit is used");
    request = Request();
    request.gameplay = false;
    Check(refused(request, good, stock.nine, ListenerUse::NotGameplay), "a cinematic's frame is refused");

    frame = good;
    frame.sample.up[0] = frame.sample.forward[0];
    frame.sample.up[1] = frame.sample.forward[1];
    frame.sample.up[2] = frame.sample.forward[2];
    Check(refused(Request(), frame, stock.nine, ListenerUse::NoHeadBasis), "a head whose up lies along its forward is refused");
    frame = good;
    frame.sample.forward[2] = std::numeric_limits<float>::quiet_NaN();
    Check(refused(Request(), frame, stock.nine, ListenerUse::NoHeadBasis), "a head that is not finite is refused");
    frame = good;
    frame.position[1] = std::numeric_limits<float>::quiet_NaN();
    Check(refused(Request(), frame, stock.nine, ListenerUse::NoHeadBasis), "a head with no position is refused");
    frame = good;
    frame.aimPosition[0] = std::numeric_limits<float>::quiet_NaN();
    Check(refused(Request(), frame, stock.nine, ListenerUse::NoHeadBasis),
          "a frame with no camera position to measure the head's offset from is refused");
    request = Request();
    request.movePosition = false;
    Check(DecideListener(request, frame, stock.nine, stock.record, agrees, out) == ListenerUse::Used,
          "with the position part off, the orientation needs no position");

    bool named = true;
    for (int use = 0; use < static_cast<int>(ListenerUse::Count); ++use)
        named = named && std::strcmp(ListenerUseName(static_cast<ListenerUse>(use)), "?") != 0;
    Check(named, "every use has a name");
}

void TestMeasurement()
{
    const float x[3]{1.0f, 0.0f, 0.0f}, y[3]{0.0f, 1.0f, 0.0f}, minusX[3]{-2.0f, 0.0f, 0.0f}, zero[3]{};
    Check(Near(AngleDeg(x, x), 0.0f) && Near(AngleDeg(x, y), 90.0f) && Near(AngleDeg(x, minusX), 180.0f, 1.0e-3f) &&
              AngleDeg(x, zero) == 0.0f,
          "the angle between two directions, whatever their lengths");
    const float a[3]{1.0f, 2.0f, 3.0f}, b[3]{4.0f, 6.0f, 3.0f};
    Check(Near(Distance(a, b), 5.0f), "the distance between two points");

    // One build with the head 40 degrees left of the aim and off the game's camera, measured both ways.
    const float cameraPosition[3]{10.0f, 20.0f, 5.0f}, headPosition[3]{10.3f, 20.0f, 5.4f}, velocity[3]{};
    float aimForward[3], aimUp[3];
    Camera(-30.0f, 0.0f, 0.0f, aimForward, aimUp);
    const Build stock = GameBuild(aimForward, aimUp, cameraPosition, velocity);
    const HeadFrame frame = Frame(10.0f, 0.0f, 0.0f, headPosition, aimForward, cameraPosition);
    float forward[3], up[3], position[3];
    FromHandOff(stock.nine, forward, up, position);
    const Measurement built = Measure(forward, position, frame);
    Check(Near(built.fromAimDeg, 0.0f, 1.0e-2f) && Near(built.fromHeadDeg, 40.0f, 1.0e-2f) &&
              Near(built.fromHead, 0.5f, 1.0e-4f),
          "as the game built it: on the aim, 40 degrees from the head, the head's offset away");
    ListenerResult result;
    DecideListener(Request(), frame, stock.nine, stock.record, CheckRecord(stock.record, stock.nine), result);
    FromHandOff(result.handOff, forward, up, position);
    const Measurement left = Measure(forward, position, frame);
    Check(Near(left.fromHeadDeg, 0.0f, 1.0e-2f) && Near(left.fromAimDeg, 40.0f, 1.0e-2f) &&
              Near(left.fromHead, 0.0f, 1.0e-4f),
          "as the lane left it: on the head, 40 degrees from the aim, at the head");
    const float apart = AngleDeg(frame.sample.forward, frame.aimForward);
    Check(WhatItFollows(built.fromHeadDeg, built.fromAimDeg, apart) == Follows::Aim &&
              WhatItFollows(left.fromHeadDeg, left.fromAimDeg, apart) == Follows::Head,
          "which reads: the game's listener follows the aim, the lane's follows the head");
    Check(WhatItFollows(1.0f, 1.0f, 5.0f) == Follows::Inconclusive &&
              WhatItFollows(0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()) == Follows::Inconclusive,
          "with the head and the aim together nothing can be told");
    Check(WhatItFollows(20.0f, 20.0f, 40.0f) == Follows::Neither && WhatItFollows(35.0f, 30.0f, 40.0f) == Follows::Neither,
          "a listener between or away from both follows neither");
    // A hand that moves between the head's frame and the build blurs the aim's angle a little: still the aim.
    Check(WhatItFollows(38.0f, 6.0f, 40.0f) == Follows::Aim && WhatItFollows(4.0f, 37.0f, 40.0f) == Follows::Head,
          "a few degrees of lag do not change the reading");
    Check(std::strcmp(FollowsName(Follows::Aim), FollowsName(Follows::Head)) != 0 &&
              std::strcmp(FollowsName(Follows::Inconclusive), FollowsName(Follows::Neither)) != 0,
          "each reading has its own words");
}
} // namespace

int main()
{
    TestSites();
    TestGameModel();
    TestAgreement();
    TestProof();
    TestListener();
    TestRefusals();
    TestMeasurement();
    std::printf("halo4 sound listener: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
