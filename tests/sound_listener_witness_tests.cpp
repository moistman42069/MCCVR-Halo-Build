// The dev sound listener witness, the pure half: what one window measures and what it concludes
// (src/common/sound_listener_witness_logic.h), Halo 2's sites and record (src/common/halo2_sound_listener_logic.h;
// RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED) and Halo CE's packet (src/common/haloce_sound_listener_logic.h).
// The images here are built from the headers' own patterns, so these tests cannot see a constant that is wrong
// against the game: tools/verify-halo2-sound-listener.py is what holds them against the pinned module.
#include "halo2_sound_listener_logic.h"
#include "haloce_sound_listener_logic.h"

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
bool Near(float a, float b, float tol = 1.0e-3f) { return std::fabs(a - b) <= tol; }

using namespace listener_witness;
constexpr float kDegToRad = head_relative::kDegToRad;

// A forward at `yawDeg` / `pitchDeg` in Blam's angles, and the up that goes with it (no roll).
void Basis(float yawDeg, float pitchDeg, float forward[3], float up[3])
{
    ForwardOfAngles(yawDeg * kDegToRad, pitchDeg * kDegToRad, forward);
    ForwardOfAngles(yawDeg * kDegToRad, (pitchDeg + 90.0f) * kDegToRad, up);
}
Pose PoseAt(float x, float y, float z, float yawDeg, float pitchDeg)
{
    Pose pose;
    pose.position[0] = x;
    pose.position[1] = y;
    pose.position[2] = z;
    Basis(yawDeg, pitchDeg, pose.forward, pose.up);
    return pose;
}

// ---- the shared logic ---------------------------------------------------------------------------------------------
void TestAngles()
{
    const float x[3]{1, 0, 0}, y[3]{0, 1, 0}, minusX[3]{-1, 0, 0}, longX[3]{25, 0, 0}, zero[3]{0, 0, 0};
    const float bad[3]{1, std::numeric_limits<float>::quiet_NaN(), 0};
    float angle = -1.0f;
    Check(AngleDeg(x, x, angle) && angle == 0.0f, "a direction against itself is exactly 0");
    Check(AngleDeg(x, y, angle) && Near(angle, 90.0f), "perpendicular is 90");
    Check(AngleDeg(x, minusX, angle) && Near(angle, 180.0f), "opposite is 180");
    Check(AngleDeg(longX, y, angle) && Near(angle, 90.0f), "length does not matter");
    // A twentieth of a degree: where acos of a float dot product has no resolution left.
    float small[3];
    ForwardOfAngles(0.05f * kDegToRad, 0.0f, small);
    Check(AngleDeg(x, small, angle) && Near(angle, 0.05f, 2.0e-4f), "0.05 degrees is measured as 0.05 degrees");
    float before = 123.0f;
    Check(!AngleDeg(x, zero, before) && before == 123.0f, "a zero vector has no angle and leaves the output");
    Check(!AngleDeg(bad, x, before) && before == 123.0f, "a non-finite vector has no angle");
    const float a[3]{1, 2, 3}, b[3]{4, 6, 3};
    float distance = 0.0f;
    Check(Distance(a, b, distance) && Near(distance, 5.0f), "distance");
    Check(!Distance(a, bad, distance), "a non-finite point has no distance");
    float forward[3];
    Check(ForwardOfAngles(0.0f, 0.0f, forward) && Near(forward[0], 1) && Near(forward[1], 0) && Near(forward[2], 0),
          "yaw 0 pitch 0 looks along +X");
    Check(ForwardOfAngles(90.0f * kDegToRad, 0.0f, forward) && Near(forward[0], 0) && Near(forward[1], 1),
          "yaw turns from +X toward +Y");
    Check(ForwardOfAngles(0.0f, 30.0f * kDegToRad, forward) && Near(forward[2], 0.5f), "pitch is up from the horizon");
    Check(!ForwardOfAngles(std::numeric_limits<float>::infinity(), 0.0f, forward), "a non-finite angle has no forward");
}

void TestBasis()
{
    const float forward[3]{1, 0, 0}, left[3]{0, 1, 0}, up[3]{0, 0, 1};
    Check(IsBlamBasis(forward, left, up), "+X forward, +Y left, +Z up is Blam's identity (left = up x forward)");
    const float right[3]{0, -1, 0};
    Check(!IsBlamBasis(forward, right, up), "the mirrored second row is refused");
    const float skew[3]{0.2f, 0, 0.98f};
    Check(!IsBlamBasis(forward, left, skew), "an up that is not perpendicular is refused");
    const float twice[3]{2, 0, 0};
    Check(!IsBlamBasis(twice, left, up), "a row that is not unit length is refused");
    float f[3], u[3], l[3];
    Basis(37.0f, -22.0f, f, u);
    head_relative::Cross(u, f, l);
    Check(IsBlamBasis(f, l, u), "a turned and pitched basis with left = up x forward passes");
    const float nan[3]{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    Check(!IsBlamBasis(nan, left, up), "a non-finite row is refused");
}

void TestMeasure()
{
    Measure m;
    Check(m.count == 0 && m.Mean() == 0.0f, "an empty measure reads 0");
    m.Add(2.0f);
    m.Add(6.0f);
    m.Add(1.0f);
    Check(m.count == 3 && Near(m.Mean(), 3.0f) && m.max == 6.0f && m.last == 1.0f, "mean, max and last");
    m.Add(std::numeric_limits<float>::quiet_NaN());
    Check(m.count == 3, "a non-finite value is not a sample");
    Measure negative;
    negative.Add(-4.0f);
    Check(negative.max == -4.0f, "the first value is the max whatever its sign");
}

// The window a listener makes that is built from `source` while the head is `head` and the aim `aim`.
Window WindowOf(const Pose& listener, const Pose* head, const float* aim, const Pose* camera, int samples = 10)
{
    Window window;
    References refs;
    refs.head = head;
    refs.aim = aim;
    refs.camera = camera;
    for (int i = 0; i < samples; ++i)
        AddSample(window, listener, refs);
    return window;
}

void TestVerdict()
{
    // The head looks 40 degrees left of the aim and is 0.3 world units from the character's eye.
    const Pose head = PoseAt(10.3f, 5.0f, 2.0f, 40.0f, 10.0f);
    const Pose eye = PoseAt(10.0f, 5.0f, 2.0f, 0.0f, -5.0f);
    float aim[3];
    ForwardOfAngles(0.0f, -5.0f * kDegToRad, aim);

    Window byHead = WindowOf(head, &head, aim, &eye);
    Check(Orientation(byHead) == Verdict::Head, "a listener built from the head is ORIENTED BY THE HEAD");
    Check(Position(byHead) == Place::AtHead, "...and AT THE HEAD");
    Check(byHead.taken == 10 && byHead[M::HeadForward].count == 10 && byHead[M::HeadForward].max == 0.0f,
          "ten samples, each exactly 0 from the head");
    Check(Near(byHead[M::AimForward].Mean(), byHead[M::HeadAimApart].Mean(), 1.0e-3f) &&
              byHead[M::HeadAimApart].Mean() > 39.0f,
          "its angle to the aim is the head's angle to the aim");

    Window byAim = WindowOf(eye, &head, aim, &eye);
    Check(Orientation(byAim) == Verdict::Aim, "a listener built from the aim's camera is ORIENTED BY THE AIM");
    Check(Position(byAim) == Place::Away && Near(byAim[M::HeadDistance].max, 0.3f, 1.0e-4f),
          "...and 0.3 world units from the head");

    // Head and aim pointing the same way: the window cannot tell them apart.
    float sameAim[3]{head.forward[0], head.forward[1], head.forward[2]};
    Check(Orientation(WindowOf(head, &head, sameAim, &eye)) == Verdict::Together,
          "head and aim together: the verdict says so instead of choosing");

    // A third-person camera: neither the head nor the aim, but the game's own camera.
    const Pose chase = PoseAt(8.0f, 5.0f, 3.0f, 75.0f, -20.0f);
    Check(Orientation(WindowOf(chase, &head, aim, &chase)) == Verdict::Camera,
          "a listener on the game's own camera, off both the head and the aim, is named as that");
    const Pose elsewhere = PoseAt(8.0f, 5.0f, 3.0f, -100.0f, 3.0f);
    Check(Orientation(WindowOf(elsewhere, &head, aim, &eye)) == Verdict::Neither, "none of them is NEITHER");

    // The up counts: the head's forward with another roll is not the head.
    Pose rolled = head;
    float left[3];
    head_relative::Cross(head.up, head.forward, left);
    for (int i = 0; i < 3; ++i)
        rolled.up[i] = left[i];
    Check(Orientation(WindowOf(rolled, &head, aim, nullptr)) == Verdict::Neither,
          "the head's forward with the wrong up is not the head");

    // One sample off the head loses "follows" for the whole window (the means say which way it leant).
    Window mixed = WindowOf(head, &head, aim, &eye, 9);
    References refs;
    refs.head = &head;
    refs.aim = aim;
    refs.camera = &eye;
    AddSample(mixed, eye, refs);
    Check(Orientation(mixed) == Verdict::Neither && mixed[M::HeadForward].Mean() < 10.0f,
          "a window that straddles a change is NEITHER, with a small mean");

    Check(Orientation(Window{}) == Verdict::NoSamples && Position(Window{}) == Place::Unknown,
          "an empty window has no verdict");
    Window noReference = WindowOf(head, nullptr, nullptr, nullptr, 3);
    Check(Orientation(noReference) == Verdict::NoReference && noReference.aimMissing == 3,
          "samples with nothing to hold them against say so, and count the missing aim");
    // A head with no aim is still the head.
    Check(Orientation(WindowOf(head, &head, nullptr, nullptr)) == Verdict::Head, "no aim: the head alone decides");

    // What a hook handed on, against the head.
    Window handedWindow;
    References handedRefs;
    handedRefs.head = &head;
    handedRefs.handed = &head;
    AddSample(handedWindow, eye, handedRefs);
    Check(handedWindow[M::HandedHeadForward].max == 0.0f && handedWindow[M::HeadForward].max > 39.0f,
          "the handed listener and the game's own are measured apart");

    Window counts;
    counts.refused[static_cast<size_t>(Refusal::NotPlaying)] = 3;
    counts.refused[static_cast<size_t>(Refusal::Torn)] = 1;
    counts.head[static_cast<size_t>(head_relative::Use::Ok)] = 5;
    counts.head[static_cast<size_t>(head_relative::Use::Stale)] = 2;
    char text[512];
    RefusalText(counts, text, sizeof(text));
    Check(counts.Refused() == 4 && std::strstr(text, "not playing)=3") && std::strstr(text, "two reads=1"),
          "refusals are counted and named");
    HeadMissingText(counts, text, sizeof(text));
    Check(counts.HeadMissing() == 2 && std::strstr(text, "stale head sample=2") && !std::strstr(text, "used"),
          "the head's refusals are named, the usable ones are not");
    RefusalText(Window{}, text, sizeof(text));
    Check(std::strcmp(text, "none") == 0, "no refusal reads none");
    for (size_t i = 0; i < kRefusals; ++i)
        Check(std::strcmp(RefusalName(static_cast<Refusal>(i)), "?") != 0, "every refusal has a name");
}

// ---- Halo 2: the sites --------------------------------------------------------------------------------------------
using namespace halo2_sound_listener;

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
void PutRip(std::vector<uint8_t>& bytes, size_t disp, size_t next, int64_t fromRva, int64_t toRva)
{
    const int32_t rel = static_cast<int32_t>(toRva - (fromRva + static_cast<int64_t>(next)));
    std::memcpy(bytes.data() + disp, &rel, 4);
}

// The observer core's accessor arithmetic (halo2_render_logic.h, kHalo2ObserverResultAccessorPattern), spelled
// here so this test needs no engine header: imul rax, rbx, 0x368 ; lea rcx, [rip + array] ; add rax, rcx.
constexpr char kAccessorPattern[] = "48 69 C3 68 03 00 00 48 8D 0D ?? ?? ?? ?? 48 03 C1";
constexpr size_t kAccessorStrideAt = 3, kAccessorDispAt = 10, kAccessorNextAt = 14;

// A synthetic image's worth of sites at the retail offsets, each wired to the others.
struct Image
{
    int64_t loopRva = 0x6E4A10, buildRva = 0x6E4CB7, builderRva = 0x729BA0, velocityRva = 0x72A0A0;
    int64_t accessorRva = 0x6F0E60, accessorMatchRva = 0x6F0E79, playingRva = 0x6A4390;
    int64_t globalsSlotRva = 0x15F21A0, playingSlotRva = 0xE80A20, arrayRva = 0x15F297C;
    std::vector<uint8_t> loop, build, builder, playing, accessor;
    ObserverRecord observer;
    Image()
    {
        loop = PatternBytes(kListenerLoopPattern);
        build = PatternBytes(kListenerBuildPattern);
        builder = PatternBytes(kListenerMatrixBuilderPattern);
        playing = PatternBytes(kPlayingLeafPattern);
        accessor = PatternBytes(kAccessorPattern);
        Wire();
    }
    void Wire()
    {
        PutRip(loop, kLoopGlobalsDisp, kLoopGlobalsNext, loopRva, globalsSlotRva);
        PutBranch(loop, kLoopCallPlaying, loopRva, playingRva);
        PutBranch(build, kBuildCallAccessor, buildRva, accessorRva);
        PutBranch(build, kBuildCallBuilder, buildRva, builderRva);
        PutBranch(build, kBuildCallVelocity, buildRva, velocityRva);
        const int32_t back = static_cast<int32_t>(loopRva - (buildRva + static_cast<int64_t>(kBuildLoopJump) + 6));
        std::memcpy(build.data() + kBuildLoopJump + 2, &back, 4);
        PutRip(playing, kPlayingSlotDisp, kPlayingSlotNext, playingRva, playingSlotRva);
        PutRip(accessor, kAccessorDispAt, kAccessorNextAt, accessorMatchRva, arrayRva);
        observer.accessorMatchRva = accessorMatchRva;
        observer.arrayRva = arrayRva;
        observer.stride = 0x368;
        observer.position = 0x00;
        observer.forward = 0x20;
        observer.up = 0x2C;
        observer.strideAt = kAccessorStrideAt;
        observer.dispAt = kAccessorDispAt;
        observer.nextAt = kAccessorNextAt;
    }
    Bind Decode(Sites& sites) const
    {
        return DecodeSites(loop.data(), loopRva, build.data(), buildRva, builder.data(), builderRva, playing.data(),
                           accessor.data(), observer, sites);
    }
};

void TestSites()
{
    Image image;
    Check(image.loop.size() == kLoopBytes && image.build.size() == kBuildBytes &&
              image.builder.size() == kBuilderBytes && image.playing.size() == kPlayingBytes,
          "each pattern is as long as its byte count says");
    // Each named position lands on the bytes its comment names.
    Check(Has(image.loop, 0, {0x4C, 0x8B, 0x25}) && Has(image.loop, kLoopValid - 5, {0x42, 0x0F, 0xB6, 0x9C, 0x26}) &&
              Has(image.loop, kLoopCallPlaying, {0xE8}),
          "loop head: the globals load, the valid byte's load, the call");
    Check(Has(image.build, kBuildCallAccessor, {0xE8}) &&
              Has(image.build, kBuildObserverForward - 4, {0xF2, 0x0F, 0x10, 0x40, 0x20}) &&
              Has(image.build, kBuildForwardSlot - 5, {0xF2, 0x0F, 0x11, 0x44, 0x24, 0x40}) &&
              Has(image.build, kBuildObserverUp - 4, {0xF2, 0x0F, 0x10, 0x40, 0x2C}) &&
              Has(image.build, kBuildUpSlot - 5, {0xF2, 0x0F, 0x11, 0x44, 0x24, 0x30}) &&
              Has(image.build, kBuildPositionLoad, {0xF2, 0x0F, 0x10, 0x00}) &&
              Has(image.build, kBuildPositionSlot - 5, {0xF2, 0x0F, 0x11, 0x44, 0x24, 0x20}) &&
              Has(image.build, kBuildObserverVelocity - 4, {0xF2, 0x0F, 0x10, 0x40, 0x14}) &&
              Has(image.build, kBuildVelocitySlot - 5, {0xF2, 0x0F, 0x11, 0x44, 0x24, 0x50}) &&
              Has(image.build, kBuildObserverLocation - 3, {0x48, 0x8B, 0x42, 0x0C}),
          "build tail: the observer's forward, up, position, velocity and location, each to its slot");
    Check(Has(image.build, kBuildValid - 4, {0x42, 0xC6, 0x84, 0x26, 0xAE}) && image.build[kBuildValidValue] == 1 &&
              Has(image.build, kBuildMatrix - 4, {0x49, 0x8D, 0x9C, 0x24, 0xB0}) &&
              Has(image.build, kBuildByte7 - 4, {0x42, 0x88, 0x84, 0x26, 0xAF}) &&
              Has(image.build, kBuildVelocity - 4, {0x4D, 0x8D, 0x84, 0x24, 0xE4}) &&
              Has(image.build, kBuildLocation - 4, {0x42, 0x89, 0x84, 0x26, 0xA8}) &&
              Has(image.build, kBuildCluster - 5, {0x66, 0x42, 0x89, 0x84, 0x26, 0xAC}),
          "build tail: the listener's valid byte, matrix, byte 7, velocity, location and cluster");
    Check(Has(image.build, kBuildArgUp - 4, {0x4C, 0x8D, 0x4C, 0x24, 0x30}) &&
              Has(image.build, kBuildArgForward - 4, {0x4C, 0x8D, 0x44, 0x24, 0x40}) &&
              Has(image.build, kBuildArgPosition - 4, {0x48, 0x8D, 0x54, 0x24, 0x20}) &&
              Has(image.build, kBuildCallBuilder, {0xE8}) &&
              Has(image.build, kBuildArgVelocity - 4, {0x48, 0x8D, 0x54, 0x24, 0x50}) &&
              Has(image.build, kBuildCallVelocity, {0xE8}) &&
              Has(image.build, kBuildStride - 3, {0x48, 0x83, 0xC6, 0x48}) &&
              Has(image.build, kBuildUsers - 3, {0x41, 0x83, 0xFE, 0x04}) &&
              Has(image.build, kBuildLoopJump, {0x0F, 0x8C}),
          "build tail: r9 = up, r8 = forward, rdx = position, the calls, the stride, the count, the loop's jump");
    Check(Has(image.builder, kBuilderScaleStore, {0xC7, 0x01, 0x00, 0x00, 0x80, 0x3F}) &&
              Has(image.builder, kBuilderForwardLoad, {0xF2, 0x41, 0x0F, 0x10, 0x00}) &&
              Has(image.builder, kBuilderForwardXY - 4, {0xF2, 0x0F, 0x11, 0x41, 0x04}) &&
              Has(image.builder, kBuilderForwardZ - 2, {0x89, 0x41, 0x0C}) &&
              Has(image.builder, kBuilderLeftX - 4, {0xF3, 0x0F, 0x11, 0x49, 0x10}) &&
              Has(image.builder, kBuilderLeftZ - 4, {0xF3, 0x0F, 0x11, 0x79, 0x18}) &&
              Has(image.builder, kBuilderLeftY - 4, {0xF3, 0x0F, 0x11, 0x59, 0x14}) &&
              Has(image.builder, kBuilderUpLoad, {0xF2, 0x41, 0x0F, 0x10, 0x01}) &&
              Has(image.builder, kBuilderUpXY - 4, {0xF2, 0x0F, 0x11, 0x41, 0x1C}) &&
              Has(image.builder, kBuilderUpZ - 2, {0x89, 0x41, 0x24}) &&
              Has(image.builder, kBuilderPositionLoad, {0xF2, 0x0F, 0x10, 0x02}) &&
              Has(image.builder, kBuilderPositionXY - 4, {0xF2, 0x0F, 0x11, 0x41, 0x28}) &&
              Has(image.builder, kBuilderPositionZ - 2, {0x89, 0x41, 0x30}),
          "builder: scale 1.0, forward from r8, left, up from r9, position from rdx");
    Check(Has(image.playing, kPlayingSlotDisp - 3, {0x48, 0x8B, 0x05}) &&
              Has(image.playing, kPlayingOffset - 3, {0x83, 0x7C, 0x88, 0x0C, 0xFF}),
          "playing leaf: the slot's load and the per-user compare");

    Sites sites;
    Check(image.Decode(sites) == Bind::Bound, "the wired image binds");
    Check(sites.globalsSlotRva == image.globalsSlotRva && sites.listenerOffset == 0xA8 && sites.stride == 0x48 &&
              sites.users == 4 && sites.clusterOffset == 4 && sites.validOffset == 6 && sites.byte7Offset == 7 &&
              sites.matrixOffset == 8 && sites.velocityOffset == 0x3C,
          "listener 0 at globals +0xA8, stride 0x48, four of them; cluster +4, valid +6, byte 7, matrix +8, "
          "velocity +0x3C");
    Check(sites.scaleOffset == 0 && sites.forwardOffset == 4 && sites.leftOffset == 0x10 && sites.upOffset == 0x1C &&
              sites.positionOffset == 0x28 && sites.matrixBytes == 0x34,
          "the matrix: scale +0, forward +4, left +0x10, up +0x1C, position +0x28, 0x34 bytes");
    Check(sites.accessorRva == image.accessorRva && sites.observerArrayRva == image.arrayRva &&
              sites.observerStride == 0x368 && sites.observerPosition == 0 && sites.observerForward == 0x20 &&
              sites.observerUp == 0x2C && sites.observerVelocity == 0x14 && sites.observerLocation == 0x0C,
          "the source: the observer result array, stride 0x368, position +0, velocity +0x14, forward +0x20, up +0x2C");
    Check(sites.playingRva == image.playingRva && sites.playingSlotRva == image.playingSlotRva &&
              sites.playingOffset == 0x0C && sites.builderRva == image.builderRva &&
              sites.velocityRva == image.velocityRva,
          "the playing leaf, its slot and offset, the builder and the velocity transform");

    // The refusals.
    Sites untouched;
    untouched.stride = 77;
    const auto refuses = [&untouched](const Image& broken, Bind why, const char* what) {
        Sites out = untouched;
        Check(broken.Decode(out) == why && out.stride == 77, what);
    };
    {
        Sites out;
        Check(DecodeSites(nullptr, 0, image.build.data(), image.buildRva, image.builder.data(), image.builderRva,
                          image.playing.data(), image.accessor.data(), image.observer, out) == Bind::LoopMissing,
              "no loop head");
        Check(DecodeSites(image.loop.data(), image.loopRva, nullptr, 0, image.builder.data(), image.builderRva,
                          image.playing.data(), image.accessor.data(), image.observer, out) == Bind::BuildMissing,
              "no build tail");
        Check(DecodeSites(image.loop.data(), image.loopRva, image.build.data(), image.buildRva, nullptr, 0,
                          image.playing.data(), image.accessor.data(), image.observer, out) == Bind::BuilderMissing,
              "no builder");
        Check(DecodeSites(image.loop.data(), image.loopRva, image.build.data(), image.buildRva,
                          image.builder.data(), image.builderRva, nullptr, image.accessor.data(), image.observer,
                          out) == Bind::PlayingMismatch,
              "an unreadable playing leaf");
        Check(DecodeSites(image.loop.data(), image.loopRva, image.build.data(), image.buildRva,
                          image.builder.data(), image.builderRva, image.playing.data(), nullptr, image.observer,
                          out) == Bind::AccessorMismatch,
              "an unreadable accessor");
    }
    {
        Image broken;
        broken.loopRva += 0x10;   // the tail no longer jumps back to the matched head
        refuses(broken, Bind::LoopMismatch, "the tail's jump does not land on the loop head");
    }
    {
        Image broken;
        broken.loop[kLoopValid] = 0xAD;
        refuses(broken, Bind::LoopMismatch, "the two sites disagree on the valid byte");
    }
    {
        Image broken;
        broken.builderRva += 0x40;   // the tail calls something else
        refuses(broken, Bind::BuilderMismatch, "the tail does not call the matched builder");
    }
    {
        Image broken;
        broken.build[kBuildArgForward] = 0x30;   // r8 is the slot the UP was copied to
        refuses(broken, Bind::ArgumentMismatch, "the builder's forward is not the observer's forward");
    }
    {
        Image broken;
        broken.build[kBuildArgVelocity] = 0x20;
        refuses(broken, Bind::ArgumentMismatch, "the velocity transform's source is not the observer's velocity");
    }
    {
        Image broken;
        broken.builder[kBuilderUpXY] = 0x10;   // up stored where left goes
        refuses(broken, Bind::LayoutMismatch, "the builder's rows are not forward, left, up, position");
    }
    {
        Image broken;
        const float two = 2.0f;
        std::memcpy(broken.builder.data() + kBuilderScaleValue, &two, 4);
        refuses(broken, Bind::LayoutMismatch, "the builder's scale is not 1.0");
    }
    {
        Image broken;
        broken.builder[kBuilderForwardLoad + 4] = 0x01;   // forward loaded from r9
        refuses(broken, Bind::LayoutMismatch, "the builder's forward does not come from r8");
    }
    {
        Image broken;
        broken.build[kBuildStride] = 0x50;
        refuses(broken, Bind::RecordMismatch, "a stride that is not one listener");
    }
    {
        Image broken;
        broken.build[kBuildVelocity] = 0xE8;   // a gap between the matrix and the velocity
        refuses(broken, Bind::RecordMismatch, "the matrix does not end where the velocity begins");
    }
    {
        Image broken;
        broken.playing[kPlayingOffset - 3] = 0x81;
        refuses(broken, Bind::PlayingMismatch, "the loop head's call is not the playing leaf");
    }
    {
        Image broken;
        broken.accessorRva = broken.accessorMatchRva - 0x80;   // the core's arithmetic is not in the called function
        broken.Wire();
        refuses(broken, Bind::AccessorMismatch, "the source is not the function holding the core's accessor");
    }
    {
        Image broken;
        broken.observer.arrayRva += 0x368;
        refuses(broken, Bind::AccessorMismatch, "the source's array is not the one the observer core writes");
    }
    {
        Image broken;
        broken.observer.accessorMatchRva = -1;
        refuses(broken, Bind::AccessorMismatch, "the core's accessor was not matched");
    }
    {
        Image broken;
        broken.observer.forward = 0x24;
        refuses(broken, Bind::ObserverMismatch, "the fields copied are not the ones the observer core writes");
    }
    for (size_t i = 0; i < static_cast<size_t>(Bind::Count); ++i)
        Check(std::strcmp(BindName(static_cast<Bind>(i)), "?") != 0, "every bind result has a name");
    Check(MatchesPattern(image.playing.data(), kPlayingLeafPattern), "the template matches its own bytes");
    std::vector<uint8_t> other = image.playing;
    other[0] ^= 0xFF;
    Check(!MatchesPattern(other.data(), kPlayingLeafPattern) && !MatchesPattern(nullptr, kPlayingLeafPattern),
          "...and nothing else");
}

// ---- Halo 2: one listener -------------------------------------------------------------------------------------------
std::vector<uint8_t> RecordOf(const Sites& sites, const Pose& pose, uint8_t valid = 1)
{
    std::vector<uint8_t> record(sites.stride, 0);
    const int32_t location = 0x1234;
    const int16_t cluster = 7;
    std::memcpy(record.data(), &location, 4);
    std::memcpy(record.data() + sites.clusterOffset, &cluster, 2);
    record[sites.validOffset] = valid;
    record[sites.byte7Offset] = 1;
    BuildMatrixModel(pose.position, pose.forward, pose.up, sites, record.data() + sites.matrixOffset);
    return record;
}

void TestRecord()
{
    Image image;
    Sites sites;
    Check(image.Decode(sites) == Bind::Bound, "the image binds (record tests)");
    const Pose pose = PoseAt(12.5f, -3.25f, 1.75f, 133.0f, 18.0f);
    const std::vector<uint8_t> record = RecordOf(sites, pose);
    Record decoded;
    Check(ReadListener(record.data(), record.data(), sites, true, decoded) == Read::Ok, "a built listener reads");
    Check(decoded.location == 0x1234 && decoded.cluster == 7 && decoded.valid == 1 && decoded.scale == 1.0f,
          "location, cluster, valid byte and scale");
    float angle = 1.0f, distance = 1.0f;
    Check(AngleDeg(decoded.pose.forward, pose.forward, angle) && angle == 0.0f &&
              AngleDeg(decoded.pose.up, pose.up, angle) && angle == 0.0f &&
              Distance(decoded.pose.position, pose.position, distance) && distance == 0.0f,
          "the rows come back as the pose that was built: forward, up, position");
    float left[3];
    head_relative::Cross(pose.up, pose.forward, left);
    Check(Near(decoded.left[0], left[0]) && Near(decoded.left[1], left[1]) && Near(decoded.left[2], left[2]),
          "the second row is up x forward");

    Check(ReadListener(record.data(), record.data(), sites, false, decoded) == Read::NotPlaying,
          "a user who is not playing has no listener, whatever the record holds");
    const std::vector<uint8_t> neverBuilt = RecordOf(sites, pose, 0);
    Check(ReadListener(neverBuilt.data(), neverBuilt.data(), sites, true, decoded) == Read::NeverBuilt,
          "a clear valid byte is a record that was never built");
    std::vector<uint8_t> second = record;
    second[sites.matrixOffset + sites.forwardOffset] ^= 0x01;
    Check(ReadListener(record.data(), second.data(), sites, true, decoded) == Read::Torn,
          "two reads that differ are a record being rewritten");
    std::vector<uint8_t> scaled = record;
    const float two = 2.0f;
    std::memcpy(scaled.data() + sites.matrixOffset + sites.scaleOffset, &two, 4);
    Check(ReadListener(scaled.data(), scaled.data(), sites, true, decoded) == Read::NotABasis,
          "a scale that is not 1 is refused");
    std::vector<uint8_t> mirrored = record;
    for (int i = 0; i < 3; ++i)
    {
        const float flipped = -left[i];
        std::memcpy(mirrored.data() + sites.matrixOffset + sites.leftOffset + 4 * i, &flipped, 4);
    }
    Check(ReadListener(mirrored.data(), mirrored.data(), sites, true, decoded) == Read::NotABasis,
          "a second row that is not up x forward is refused: the convention is checked on every sample");
    const std::vector<uint8_t> zeros(sites.stride, 0);
    std::vector<uint8_t> blank = zeros;
    blank[sites.validOffset] = 1;
    Check(ReadListener(blank.data(), blank.data(), sites, true, decoded) == Read::NotABasis,
          "a valid byte over zeroes is not a listener");
    Check(RefusalOf(Read::NotPlaying) == Refusal::NotPlaying && RefusalOf(Read::NeverBuilt) == Refusal::NeverBuilt &&
              RefusalOf(Read::Torn) == Refusal::Torn && RefusalOf(Read::NotABasis) == Refusal::NotABasis,
          "each read verdict has its refusal");

    // THE QUESTION, on the model: the game builds listener 0 from the observer result. With the head written
    // there the window reads HEAD; with the game's own camera left there it reads AIM.
    const Pose head = PoseAt(40.25f, 12.0f, 3.5f, -65.0f, 12.0f);
    const Pose stock = PoseAt(40.0f, 12.0f, 3.4f, -20.0f, -4.0f);
    float aim[3];
    ForwardOfAngles(-20.0f * kDegToRad, -4.0f * kDegToRad, aim);
    for (int which = 0; which < 2; ++which)
    {
        const Pose& observer = which ? stock : head;
        const std::vector<uint8_t> built = RecordOf(sites, observer);
        Record listener;
        Window window;
        References refs;
        refs.head = &head;
        refs.aim = aim;
        refs.camera = &stock;
        refs.root = &observer;
        if (ReadListener(built.data(), built.data(), sites, true, listener) == Read::Ok)
            AddSample(window, listener.pose, refs);
        Check(window.taken == 1 && window[M::RootForward].max == 0.0f && window[M::RootDistance].max == 0.0f,
              "the listener is the observer result it was built from");
        Check(Orientation(window) == (which ? Verdict::Aim : Verdict::Head) &&
                  Position(window) == (which ? Place::Away : Place::AtHead),
              which ? "the game's own camera in the observer result: ORIENTED BY THE AIM, NOT AT THE HEAD"
                    : "the head in the observer result: ORIENTED BY THE HEAD, AT THE HEAD");
    }
}

// ---- Halo CE ----------------------------------------------------------------------------------------------------------
halo_ce::Quat YawAboutUp(float degrees)
{
    const float half = degrees * kDegToRad * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};   // OpenXR: +Y up, -Z forward
}
halo_ce::RenderContext CeContext()
{
    halo_ce::RenderContext context{};
    context.tracking.generation = 7;
    context.tracking.serial = 20;
    context.tracking.spaceEpoch = 2;
    context.reference.generation = 7;
    context.reference.spaceEpoch = 2;
    context.referenceRevision = 1;
    context.rendererEpoch = 1;
    context.camera.position = {30.0f, -8.0f, 2.0f};
    context.camera.forward = {1, 0, 0};
    context.camera.up = {0, 0, 1};
    context.camera.verticalFov = 1;
    context.camera.nearPlane = 0.01f;
    context.camera.farPlane = 100;
    context.camera.viewport = context.camera.window = {0, 0, 1000, 1000};
    context.unitsPerMeter = 1.0f / 3.048f;
    context.positional = true;
    return context;
}
halo_ce::AudioListenerPacket CePacket(const halo_ce::Camera& camera)
{
    halo_ce::AudioListenerPacket packet{};
    packet.position = packet.matrixPosition = camera.position;
    packet.forward = packet.matrixForward = camera.forward;
    packet.up = packet.matrixUp = camera.up;
    packet.matrixLeft = halo_ce::Cross(camera.up, camera.forward);
    packet.scale = 1;
    return packet;
}

void TestHaloCE()
{
    using namespace halo_ce;
    RenderContext context = CeContext();
    Camera centre{};
    Check(BuildHeadCamera(context, centre), "a level head at the reference builds");
    Check(Near(centre.forward.x, 1) && Near(centre.forward.y, 0) && Near(centre.up.z, 1) &&
              Near(centre.position.x, context.camera.position.x) && Near(centre.position.y, context.camera.position.y),
          "...as the stock camera itself");
    // The head turned 40 degrees to the left and leant 0.5 m forward.
    context.tracking.headOrientation = YawAboutUp(40.0f);
    context.tracking.headPosition = {0.0f, 0.0f, -0.5f};
    Check(BuildHeadCamera(context, centre), "a turned, leaning head builds");
    float headForward[3]{centre.forward.x, centre.forward.y, centre.forward.z};
    const float stockForward[3]{1, 0, 0};
    float angle = 0.0f;
    Check(AngleDeg(headForward, stockForward, angle) && Near(angle, 40.0f, 1.0e-2f) && centre.forward.y > 0,
          "the head's forward is 40 degrees to the LEFT of the stock camera's (+Y is left)");
    Check(Near(centre.position.x - context.camera.position.x, 0.5f / 3.048f, 1.0e-4f),
          "the head's position is half a metre ahead of the stock camera, in world units");
    // The listener hook's own packet must agree with the witness's head: the same mapping, written twice.
    const AudioListenerPacket native = CePacket(context.camera);
    AudioListenerPacket adapted{};
    Check(BuildHeadListener(context, native, adapted), "the hook's head listener builds for the same context");
    const float handedForward[3]{adapted.matrixForward.x, adapted.matrixForward.y, adapted.matrixForward.z};
    Check(AngleDeg(handedForward, headForward, angle) && angle < 1.0e-3f,
          "the hook's forward is the witness's head forward");

    // On foot, admitted: the game built the stock camera, the hook handed on the head.
    listener_witness::Window window;
    WitnessPacket(window, native, &adapted, &context);
    Check(window.taken == 1 && window.handedHead == 1 && window.handedStock == 0, "one packet, handed on by the hook");
    Check(Near(window[M::HeadForward].max, 40.0f, 1.0e-2f) && window[M::CameraForward].max == 0.0f,
          "the GAME'S OWN listener is the stock camera, 40 degrees off the head");
    Check(window[M::HandedHeadForward].max < 1.0e-3f && window[M::HandedHeadUp].max < 1.0e-3f,
          "what was HANDED to the backend is the head");
    Check(Near(window[M::HeadDistance].max, 0.5f / 3.048f, 1.0e-4f) && Position(window) == Place::Away,
          "the listener's position stays at the stock camera: half a metre from the head");
    Check(Orientation(window) == Verdict::Camera, "the game's own listener reads as the game's own camera");
    Check(window.aimMissing == 1, "no controller: no hand aim, counted");

    // With a hand aiming 25 degrees right of the stock camera.
    context.tracking.controllers.primaryAim.valid = true;
    context.tracking.controllers.primaryAim.orientation = YawAboutUp(-25.0f);
    context.tracking.controllers.primaryAim.position = {0.2f, -0.3f, -0.4f};
    listener_witness::Window handWindow;
    WitnessPacket(handWindow, native, &adapted, &context);
    Check(handWindow.aimMissing == 0 && Near(handWindow[M::AimForward].max, 25.0f, 1.0e-2f) &&
              Near(handWindow[M::HeadAimApart].max, 65.0f, 1.0e-2f),
          "the hand's aim is measured: 25 degrees from the game's listener, 65 from the head");

    // Not admitted (a seat, a cinematic): the game's packet goes on as it is.
    listener_witness::Window stockWindow;
    WitnessPacket(stockWindow, native, nullptr, &context);
    Check(stockWindow.handedStock == 1 && Near(stockWindow[M::HandedHeadForward].max, 40.0f, 1.0e-2f),
          "a packet handed on as the game built it is 40 degrees off the head, and the line shows it");
    // No context at all: counted, measured against nothing.
    listener_witness::Window blind;
    WitnessPacket(blind, native, nullptr, nullptr);
    Check(blind.taken == 1 && blind.HeadMissing() == 1 && Orientation(blind) == Verdict::NoReference,
          "no gameplay context: the packet is counted with no head to hold it against");
    // A packet whose matrix is not a rotation is refused.
    AudioListenerPacket broken = native;
    broken.scale = 0;
    listener_witness::Window refused;
    WitnessPacket(refused, broken, nullptr, &context);
    Check(refused.taken == 0 && refused.refused[static_cast<size_t>(Refusal::NotABasis)] == 1,
          "a packet whose matrix is not a rotation of scale 1 is refused");
    // A seat frame is counted.
    context.reference.seatHeading = {0, 1, 0};
    context.reference.seatHeadingValid = true;
    listener_witness::Window seat;
    WitnessPacket(seat, native, nullptr, &context);
    Check(seat.seated == 1, "a seat's frame is counted as one");
    // A context with no tracking has no head.
    RenderContext empty{};
    empty.camera = context.camera;
    Check(!BuildHeadCamera(empty, centre), "no tracking sample: no head");
}

// ---- the production decode on a real module (only when given one) ------------------------------------------------
// `halomccvr_sound_listener_witness_tests <path to halo2.dll>` maps the file as the loader lays it out and runs the
// bind's own steps on it: each pattern must match exactly once and DecodeSites -- the function the dev build runs
// at install -- must bind. ctest passes no argument, so no test depends on a game file. The observer core's
// constants are spelled here as halo2_render_logic.h has them (kHalo2ObserverResultArrayRva, kHalo2ObserverStride,
// the three span offsets); tools/verify-halo2-sound-listener.py reads them from that header instead.
bool MapModule(const char* path, std::vector<uint8_t>& image)
{
    std::FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file)
        return false;
    std::vector<uint8_t> raw;
    uint8_t chunk[65536];
    for (size_t got = 0; (got = std::fread(chunk, 1, sizeof(chunk), file)) != 0;)
        raw.insert(raw.end(), chunk, chunk + got);
    std::fclose(file);
    const auto u16 = [&raw](size_t at) { uint16_t v = 0; std::memcpy(&v, raw.data() + at, 2); return v; };
    const auto u32 = [&raw](size_t at) { uint32_t v = 0; std::memcpy(&v, raw.data() + at, 4); return v; };
    if (raw.size() < 0x400 || u16(0) != 0x5A4D)
        return false;
    const size_t pe = u32(0x3C);
    if (pe + 0x108 > raw.size() || u32(pe) != 0x4550)
        return false;
    const size_t sections = u16(pe + 6), optional = u16(pe + 20);
    const size_t sizeOfImage = u32(pe + 24 + 56), sizeOfHeaders = u32(pe + 24 + 60);
    if (sizeOfImage < sizeOfHeaders || sizeOfImage > 0x10000000 || sizeOfHeaders > raw.size())
        return false;
    image.assign(sizeOfImage, 0);
    std::memcpy(image.data(), raw.data(), sizeOfHeaders);
    for (size_t i = 0; i < sections; ++i)
    {
        const size_t entry = pe + 24 + optional + i * 40;
        if (entry + 40 > raw.size())
            return false;
        const size_t virtualSize = u32(entry + 8), va = u32(entry + 12), rawSize = u32(entry + 16),
                     rawPointer = u32(entry + 20);
        const size_t take = rawSize < virtualSize ? rawSize : virtualSize;
        if (va + take > image.size() || rawPointer + take > raw.size())
            return false;
        std::memcpy(image.data() + va, raw.data() + rawPointer, take);
    }
    return true;
}
// Where `pattern` matches in the image, and how often.
size_t FindPattern(const std::vector<uint8_t>& image, const char* pattern, size_t& first)
{
    const size_t length = PatternBytes(pattern).size();
    const uint8_t lead = PatternBytes(pattern)[0];
    size_t count = 0;
    for (size_t at = 0; at + length <= image.size(); ++at)
    {
        if (image[at] != lead || !MatchesPattern(image.data() + at, pattern))
            continue;
        if (!count++)
            first = at;
    }
    return count;
}
int VerifyModule(const char* path)
{
    std::vector<uint8_t> image;
    if (!MapModule(path, image))
    {
        std::printf("FAIL: %s is not a readable PE image\n", path);
        return 1;
    }
    size_t loop = 0, build = 0, builder = 0, accessor = 0;
    const size_t loops = FindPattern(image, kListenerLoopPattern, loop);
    const size_t builds = FindPattern(image, kListenerBuildPattern, build);
    const size_t builders = FindPattern(image, kListenerMatrixBuilderPattern, builder);
    const size_t accessors = FindPattern(image, kAccessorPattern, accessor);
    std::printf("matches: loop head %zu, build tail %zu, matrix builder %zu, observer accessor %zu\n", loops, builds,
                builders, accessors);
    ObserverRecord observer;
    observer.accessorMatchRva = accessors == 1 ? static_cast<int64_t>(accessor) : -1;
    observer.arrayRva = 0x15F297C;
    observer.stride = 0x368;
    observer.position = 0x00;
    observer.forward = 0x20;
    observer.up = 0x2C;
    observer.strideAt = kAccessorStrideAt;
    observer.dispAt = kAccessorDispAt;
    observer.nextAt = kAccessorNextAt;
    const uint8_t* playing = nullptr;
    if (loops == 1)
    {
        const int64_t target = BranchRva(image.data() + loop, kLoopCallPlaying, static_cast<int64_t>(loop));
        if (target > 0 && static_cast<size_t>(target) + kPlayingBytes <= image.size())
            playing = image.data() + target;
    }
    Sites sites;
    const Bind bind = DecodeSites(loops == 1 ? image.data() + loop : nullptr, static_cast<int64_t>(loop),
                                  builds == 1 ? image.data() + build : nullptr, static_cast<int64_t>(build),
                                  builders == 1 ? image.data() + builder : nullptr, static_cast<int64_t>(builder),
                                  playing, accessors == 1 ? image.data() + accessor : nullptr, observer, sites);
    if (bind != Bind::Bound)
    {
        std::printf("FAIL: the production decode does not bind on %s: %s\n", path, BindName(bind));
        return 1;
    }
    std::printf("BOUND on %s: listener 0 at *[+0x%llX] + 0x%X, stride 0x%X, %u listeners; valid +0x%X, matrix "
                "+0x%X, velocity +0x%X; matrix scale +0x%X, forward +0x%X, left +0x%X, up +0x%X, position +0x%X "
                "(0x%X bytes); playing +0x%llX through *[+0x%llX] + 0x%X; source +0x%llX = +0x%llX + user * 0x%X "
                "(position +0x%X, velocity +0x%X, forward +0x%X, up +0x%X); builder +0x%llX, velocity transform "
                "+0x%llX\n",
                path, static_cast<unsigned long long>(sites.globalsSlotRva), sites.listenerOffset, sites.stride,
                sites.users, sites.validOffset, sites.matrixOffset, sites.velocityOffset, sites.scaleOffset,
                sites.forwardOffset, sites.leftOffset, sites.upOffset, sites.positionOffset, sites.matrixBytes,
                static_cast<unsigned long long>(sites.playingRva),
                static_cast<unsigned long long>(sites.playingSlotRva), sites.playingOffset,
                static_cast<unsigned long long>(sites.accessorRva),
                static_cast<unsigned long long>(sites.observerArrayRva), sites.observerStride,
                sites.observerPosition, sites.observerVelocity, sites.observerForward, sites.observerUp,
                static_cast<unsigned long long>(sites.builderRva), static_cast<unsigned long long>(sites.velocityRva));
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2)
        return VerifyModule(argv[1]);
    TestAngles();
    TestBasis();
    TestMeasure();
    TestVerdict();
    TestSites();
    TestRecord();
    TestHaloCE();
    std::printf("sound listener witness tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
