// Halo 3: the motion tracker's blips move with the HUD height (src/common/halo3_tracker_hud_height_logic.h).
// The decode of the five sites on an image BUILT FROM THE HEADER'S OWN PATTERNS, laid out as retail halo3.dll lays
// it out, with every way it must refuse; the positions inside each pattern against the bytes their comments name;
// and the arithmetic of the move. This cannot see a pattern that is wrong against the game: that is
// tools/verify-halo3-tracker-hud-height.py, and the picture is docs/HALO3-TRACKER-HUD-HEIGHT-2026-10-07.md.
#include "halo3_tracker_hud_height_logic.h"

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

using namespace halo3_tracker_hud_height;

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
void PutBranch(std::vector<uint8_t>& bytes, size_t opcode, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(opcode) + 5));
    std::memcpy(bytes.data() + opcode + 1, &rel, 4);
}
// The disp32 at `disp` ends its instruction.
void PutRip(std::vector<uint8_t>& bytes, size_t disp, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(disp) + 4));
    std::memcpy(bytes.data() + disp, &rel, 4);
}

// The image as retail halo3.dll lays it out (measured offsets from the module base).
constexpr int64_t kEntryRva = 0x2F3904, kCallRva = 0x2D2A5C, kDrawReadRva = 0x2F3A70, kObjectReadRva = 0x2F3708,
                  kSetupWriteRva = 0x2ED1FC, kOriginRva = 0xAD3160, kImageSize = 0x4A00000;

struct Image
{
    std::vector<uint8_t> entry, call, drawRead, objectRead, setupWrite;
};
// `origin` per site, so a test can make one site disagree.
Image Retail(int64_t drawOrigin = kOriginRva, int64_t objectOrigin = kOriginRva, int64_t setupOrigin = kOriginRva)
{
    Image image;
    image.entry = PatternBytes(kDrawEntryPattern);
    image.call = PatternBytes(kDrawCallPattern);
    PutBranch(image.call, kDrawCall, kCallRva, kEntryRva);
    image.drawRead = PatternBytes(kDrawOriginPattern);
    PutRip(image.drawRead, kDrawOrigin, kDrawReadRva, drawOrigin);
    PutRip(image.drawRead, kDrawScaleA, kDrawReadRva, drawOrigin + 8);
    PutRip(image.drawRead, kDrawScaleB, kDrawReadRva, drawOrigin + 12);
    image.objectRead = PatternBytes(kObjectOriginPattern);
    PutRip(image.objectRead, kObjectOrigin, kObjectReadRva, objectOrigin);
    PutRip(image.objectRead, kObjectScaleA, kObjectReadRva, objectOrigin + 8);
    PutRip(image.objectRead, kObjectScaleB, kObjectReadRva, objectOrigin + 12);
    image.setupWrite = PatternBytes(kSetupWritePattern);
    PutRip(image.setupWrite, kSetupRecord, kSetupWriteRva, setupOrigin - 8);
    PutRip(image.setupWrite, kSetupOrigin, kSetupWriteRva, setupOrigin);
    PutRip(image.setupWrite, kSetupScaleA, kSetupWriteRva, setupOrigin + 8);
    PutRip(image.setupWrite, kSetupScaleB, kSetupWriteRva, setupOrigin + 12);
    return image;
}
Bind Decode(const Image& image, Sites& sites, int64_t drawReadRva = kDrawReadRva, int64_t imageSize = kImageSize)
{
    return DecodeSites(image.entry.data(), kEntryRva, image.call.data(), kCallRva, image.drawRead.data(),
                       drawReadRva, image.objectRead.data(), kObjectReadRva, image.setupWrite.data(),
                       kSetupWriteRva, imageSize, sites);
}

void TestPositions()
{
    const std::vector<uint8_t> entry = PatternBytes(kDrawEntryPattern), call = PatternBytes(kDrawCallPattern),
                               draw = PatternBytes(kDrawOriginPattern), object = PatternBytes(kObjectOriginPattern),
                               setup = PatternBytes(kSetupWritePattern);
    // The entry: mov rax,rsp ... mov eax,0x25F0; call; sub rsp,rax; ...; mov rdi,rcx.
    Check(Has(entry, 0, {0x48, 0x8B, 0xC4}) && Has(entry, 27, {0xB8, 0xF0, 0x25, 0x00, 0x00, 0xE8}) &&
              Has(entry, entry.size() - 3, {0x48, 0x8B, 0xF9}),
          "the entry pattern begins mov rax,rsp, probes a 0x25F0 frame and ends mov rdi,rcx");
    // The call: ... add rcx,0x244; add rcx,rsi; E8 at kDrawCall, and the pattern ends with its rel32.
    Check(Has(call, 8, {0x48, 0x81, 0xC1, 0x44, 0x02, 0x00, 0x00}) && Has(call, kDrawCall - 3, {0x48, 0x03, 0xCE, 0xE8}) &&
              call.size() == kDrawCall + 5,
          "kDrawCall is the E8 behind add rcx,0x244 / add rcx,rsi, and the pattern ends with its operand");
    // The draw's read: mov rax,[rip] / movss xmm6,[rip] / movss xmm7,[rip]; each position is the disp32.
    Check(Has(draw, kDrawOrigin - 3, {0x48, 0x8B, 0x05}), "kDrawOrigin is the disp32 of mov rax,[rip+..]");
    Check(Has(draw, kDrawScaleA - 4, {0xF3, 0x0F, 0x10, 0x35}), "kDrawScaleA is the disp32 of movss xmm6,[rip+..]");
    Check(Has(draw, kDrawScaleB - 4, {0xF3, 0x0F, 0x10, 0x3D}) && draw.size() == kDrawScaleB + 4,
          "kDrawScaleB is the disp32 of movss xmm7,[rip+..], the pattern's last four bytes");
    // The instruction after each disp32 begins right behind it.
    Check(Has(draw, kDrawOrigin + 4, {0x4C, 0x8D, 0xB7}) && Has(draw, kDrawScaleA + 4, {0x8B, 0xF3}),
          "each of the draw's disp32 ends its instruction");
    // The contact builder's read: movss xmm10 / movss xmm11 / mov rax.
    Check(Has(object, kObjectScaleB - 5, {0xF3, 0x44, 0x0F, 0x10, 0x15}),
          "kObjectScaleB is the disp32 of movss xmm10,[rip+..]");
    Check(Has(object, kObjectScaleA - 5, {0xF3, 0x44, 0x0F, 0x10, 0x1D}),
          "kObjectScaleA is the disp32 of movss xmm11,[rip+..]");
    Check(Has(object, kObjectOrigin - 3, {0x48, 0x8B, 0x05}) && Has(object, kObjectOrigin + 4, {0x49, 0x89, 0x43, 0x08}),
          "kObjectOrigin is the disp32 of mov rax,[rip+..], stored to [r11+8] next");
    Check(Has(object, kObjectScaleB + 4, {0x48, 0x63, 0xC2}) && Has(object, kObjectScaleA + 4, {0x4C, 0x69, 0xF0}),
          "each of the contact builder's disp32 ends its instruction");
    // The frame setup's write: mov [rip],rax; mov rcx,[rax+0x18]; mov [rip],rcx; movss [rip],xmm0; movss [rip],xmm1.
    Check(Has(setup, kSetupRecord - 3, {0x48, 0x89, 0x05}) && Has(setup, kSetupRecord + 4, {0x48, 0x8B, 0x48, 0x18}),
          "kSetupRecord is the disp32 of mov [rip+..],rax, and the record's +0x18 is read next");
    Check(Has(setup, kSetupOrigin - 3, {0x48, 0x89, 0x0D}) && Has(setup, kSetupOrigin + 4, {0xF3, 0x0F, 0x10, 0x40, 0x20}),
          "kSetupOrigin is the disp32 of mov [rip+..],rcx, and the record's +0x20 is read next");
    Check(Has(setup, kSetupScaleA - 4, {0xF3, 0x0F, 0x11, 0x05}) &&
              Has(setup, kSetupScaleA + 4, {0xF3, 0x0F, 0x10, 0x48, 0x24}),
          "kSetupScaleA is the disp32 of movss [rip+..],xmm0, and the record's +0x24 is read next");
    Check(Has(setup, kSetupScaleB - 4, {0xF3, 0x0F, 0x11, 0x0D}) && setup.size() == kSetupScaleB + 4,
          "kSetupScaleB is the disp32 of movss [rip+..],xmm1, the pattern's last four bytes");
}

void TestDecode()
{
    Sites sites{};
    Check(Decode(Retail(), sites) == Bind::Bound, "the retail layout binds");
    Check(sites.drawRva == kEntryRva && sites.originRva == kOriginRva && sites.recordRva == kOriginRva - 8,
          "the draw is the matched entry, the origin 0xAD3160 and the record pointer the eight bytes before it");

    // Each site missing: its own reason, and nothing decoded.
    const Image retail = Retail();
    const uint8_t* const parts[5]{retail.entry.data(), retail.call.data(), retail.drawRead.data(),
                                  retail.objectRead.data(), retail.setupWrite.data()};
    const Bind missing[5]{Bind::EntryMissing, Bind::CallMissing, Bind::DrawReadMissing, Bind::ObjectReadMissing,
                          Bind::SetupWriteMissing};
    for (int gone = 0; gone < 5; ++gone)
    {
        const uint8_t* p[5];
        for (int i = 0; i < 5; ++i)
            p[i] = i == gone ? nullptr : parts[i];
        Sites none{};
        const Bind bind = DecodeSites(p[0], kEntryRva, p[1], kCallRva, p[2], kDrawReadRva, p[3], kObjectReadRva, p[4],
                                      kSetupWriteRva, kImageSize, none);
        Check(bind == missing[gone] && none.drawRva == 0 && none.originRva == 0,
              "a site with no unique match refuses with its own reason and decodes nothing");
    }

    // The call lands somewhere else: the entry pattern matched a function the HUD draw does not call.
    {
        Image image = Retail();
        PutBranch(image.call, kDrawCall, kCallRva, kEntryRva + 0x10);
        Sites none{};
        Check(Decode(image, none) == Bind::CallMismatch, "a call that does not land on the matched entry refuses");
    }
    // The origin read lies before the entry, at it, or beyond the draw.
    {
        Sites none{};
        Check(Decode(Retail(), none, kEntryRva - 0x40) == Bind::ReadOutside, "an origin read before the entry refuses");
        Check(Decode(Retail(), none, kEntryRva) == Bind::ReadOutside, "an origin read AT the entry refuses");
        Check(Decode(Retail(), none, kEntryRva + kDrawExtent) == Bind::ReadOutside,
              "an origin read past the draw's extent refuses");
    }
    // A scale pair that is not right behind its origin, at each site.
    for (int site = 0; site < 3; ++site)
        for (int which = 0; which < 2; ++which)
        {
            Image image = Retail();
            std::vector<uint8_t>& bytes = site == 0 ? image.drawRead : site == 1 ? image.objectRead : image.setupWrite;
            const int64_t rva = site == 0 ? kDrawReadRva : site == 1 ? kObjectReadRva : kSetupWriteRva;
            const size_t at = site == 0 ? (which ? kDrawScaleB : kDrawScaleA)
                                        : site == 1 ? (which ? kObjectScaleB : kObjectScaleA)
                                                    : (which ? kSetupScaleB : kSetupScaleA);
            PutRip(bytes, at, rva, kOriginRva + (which ? 16 : 4));
            Sites none{};
            Check(Decode(image, none) == Bind::ScaleNotBeside, "a scale that is not right behind its origin refuses");
        }
    // One site names another origin (its own scale pair moved with it, so only the agreement can catch it).
    {
        Sites none{};
        Check(Decode(Retail(kOriginRva, kOriginRva + 0x10), none) == Bind::OriginDisagrees,
              "the contact builder reading a neighbouring pair refuses");
        Check(Decode(Retail(kOriginRva, kOriginRva, kOriginRva - 0x10), none) == Bind::OriginDisagrees,
              "the frame setup writing a neighbouring pair refuses");
        Check(Decode(Retail(kOriginRva + 4), none) == Bind::OriginDisagrees,
              "the draw reading four bytes further on refuses (the likeliest patch failure: the neighbouring field)");
    }
    // The record pointer is not the eight bytes in front.
    {
        Image image = Retail();
        PutRip(image.setupWrite, kSetupRecord, kSetupWriteRva, kOriginRva - 16);
        Sites none{};
        Check(Decode(image, none) == Bind::RecordNotBeside, "a record pointer that is not right in front refuses");
    }
    // The origin itself: outside the image, or not on a float's boundary (all three sites agreeing on it).
    {
        Sites none{};
        Check(Decode(Retail(), none, kDrawReadRva, kOriginRva + 15) == Bind::Implausible,
              "an origin whose scale pair ends past the image refuses");
        Check(Decode(Retail(), none, kDrawReadRva, kOriginRva + 16) == Bind::Bound,
              "an origin whose sixteen bytes just fit binds");
        Check(Decode(Retail(kOriginRva + 2, kOriginRva + 2, kOriginRva + 2), none) == Bind::Implausible,
              "an origin that is not on a float's boundary refuses");
        Check(Decode(Retail(4, 4, 4), none) == Bind::Implausible,
              "an origin with no room for the record pointer in front refuses");
    }
    for (int i = 0; i < static_cast<int>(Bind::Count); ++i)
        Check(std::strcmp(BindName(static_cast<Bind>(i)), "?") != 0, "every bind result has a name");
}

void TestMove()
{
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    // The HUD height hook moves every element's finished basis exactly once (an element of a HUD widget
    // included, never twice); the blips must make the same move.
    Check(ArtShiftY(16.0f) == -16.0f && ArtShiftY(-24.0f) == 24.0f && ArtShiftY(0.0f) == 0.0f,
          "the art's move is the height, up for a positive height (y grows downward)");

    float y = 0.0f;
    Check(LoweredOriginY(true, 16.0f, 600.0f, y) == Use::Lowered && y == 584.0f,
          "the default height puts the centre 16 units higher");
    Check(LoweredOriginY(true, -24.0f, 600.0f, y) == Use::Lowered && y == 624.0f,
          "a negative height puts it lower");
    Check(LoweredOriginY(true, 300.0f, 600.0f, y) == Use::Lowered && y == 300.0f &&
              LoweredOriginY(true, -300.0f, 600.0f, y) == Use::Lowered && y == 900.0f,
          "the slider's whole range is followed: nothing is refused for its size");
    Check(LoweredOriginY(true, 300.0f, 100.0f, y) == Use::Lowered && y == -200.0f,
          "a centre the HUD height takes off the screen goes there with the art");

    // Every refusal leaves the game's own y.
    y = 1.0f;
    Check(LoweredOriginY(false, 16.0f, 600.0f, y) == Use::SwitchOff && y == 600.0f, "the switch off is the game's own");
    y = 1.0f;
    Check(LoweredOriginY(true, 0.0f, 600.0f, y) == Use::NothingLowered && y == 600.0f,
          "the anchor hook lowering nothing in this pass is the game's own");
    y = 1.0f;
    Check(LoweredOriginY(true, -0.0f, 600.0f, y) == Use::NothingLowered && y == 600.0f, "minus zero is nothing too");
    y = 1.0f;
    Check(LoweredOriginY(true, 16.0f, nan, y) == Use::NotFinite && std::isnan(y), "a centre that is not a number stays");
    y = 1.0f;
    Check(LoweredOriginY(true, 16.0f, inf, y) == Use::NotFinite && y == inf, "an infinite centre stays");
    y = 1.0f;
    Check(LoweredOriginY(true, nan, 600.0f, y) == Use::NotFinite && y == 600.0f, "a height that is not a number moves nothing");
    y = 1.0f;
    Check(LoweredOriginY(true, inf, 600.0f, y) == Use::NotFinite && y == 600.0f, "an infinite height moves nothing");
    for (int i = 0; i < static_cast<int>(Use::Count); ++i)
        Check(std::strcmp(UseName(static_cast<Use>(i)), "?") != 0, "every verdict has a name");

    // The game's y goes back only over our own value, bit for bit.
    Check(RestoreOurs(568.0f, 568.0f), "our value still standing is replaced");
    Check(!RestoreOurs(600.0f, 568.0f) && !RestoreOurs(568.00006f, 568.0f), "any other value is the game's and stays");
    Check(!RestoreOurs(-0.0f, 0.0f), "minus zero is not our zero");
    Check(RestoreOurs(nan, nan), "the comparison is of bits, so it cannot be fooled by a NaN (never written anyway)");
}
} // namespace

int main()
{
    TestPositions();
    TestDecode();
    TestMove();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
