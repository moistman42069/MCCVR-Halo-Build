// Seat frame conversion (src/common/seat_frame_logic.h): the game's rebase frame R(u) built with its own
// convention and fallback, its inverse, the record <-> world angle conversions, the frame check, the value check
// on a read object frame, and the case decision.
//
// MUTATION-CHECKED, each of these applied alone: f = u x Y instead of Y x u; the fallback Z x u dropped
// (or replaced by X x u); Apply using the rows (R^T) instead of the columns; Invert using R instead of R^-1; the
// [0, 2pi) wrap removed; the up-pole fallback yaw ignored; FrameReadAgrees without the perpendicular test. Every
// one of them fails at least one check below.
#include "seat_frame_logic.h"
#include <cmath>
#include <cstdio>
#include <string_view>

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

using namespace seat_frame;
constexpr float kDeg = 0.017453292519943295f;
constexpr float kPi = 3.14159265358979f;

bool Near(float a, float b, float tolerance = 1e-5f)
{
    return std::fabs(a - b) <= tolerance;
}

bool Near3(const float a[3], const float b[3], float tolerance = 1e-5f)
{
    return Near(a[0], b[0], tolerance) && Near(a[1], b[1], tolerance) && Near(a[2], b[2], tolerance);
}

float AngleDiff(float a, float b)
{
    return std::fabs(std::remainder(a - b, 2.0f * kPi));
}

// u for a vehicle pitched nose-up by `pitch` about its own left (world +Y) axis, then rolled by `roll` about
// world +X, then turned to `heading` about +Z. Unit length.
void UpFor(float pitch, float roll, float heading, float out[3])
{
    // Start at +Z; pitch about Y: (sin p, 0, cos p) for nose-down positive -- the sign is irrelevant to the tests,
    // which only need a family of unit ups covering pitch, roll and heading.
    float v[3]{std::sin(pitch), 0.0f, std::cos(pitch)};
    // roll about X
    const float y = v[1] * std::cos(roll) - v[2] * std::sin(roll);
    const float z = v[1] * std::sin(roll) + v[2] * std::cos(roll);
    v[1] = y;
    v[2] = z;
    // heading about Z
    const float x = v[0] * std::cos(heading) - v[1] * std::sin(heading);
    const float yy = v[0] * std::sin(heading) + v[1] * std::cos(heading);
    out[0] = x;
    out[1] = yy;
    out[2] = v[2];
}

void TestLevelIsIdentity()
{
    const float up[3]{0.0f, 0.0f, 1.0f};
    Frame r{};
    Check(BuildFrame(up, r), "level: a frame");
    const float f[3]{1, 0, 0}, l[3]{0, 1, 0}, u[3]{0, 0, 1};
    Check(Near3(r.f, f) && Near3(r.l, l) && Near3(r.u, u), "level: R is the identity (f = +X, l = +Y, u = +Z)");
    Angles a{};
    Check(RecordFromWorld(r, 1.0f, 0.3f, 0.0f, a) && Near(a.yaw, 1.0f) && Near(a.pitch, 0.3f),
          "level: the record holds the world angles unchanged");
    Check(RecordFromWorld(r, -1.0f, -0.2f, 0.0f, a) && Near(a.yaw, 2.0f * kPi - 1.0f, 1e-4f) && Near(a.pitch, -0.2f),
          "level: a negative world yaw comes back in [0, 2pi)");
    Check(Near(FrameCheckDeg(r, 1.0f, 0.3f, 1.0f, 0.3f), 0.0f, 1e-3f), "level: the frame check of equal angles is 0");
}

// The game's convention, by hand: the vehicle pitched by 30 deg about world Y. u = (sin 30, 0, cos 30).
// f = normalize(Y x u) = (cos 30, 0, -sin 30); l = u x f = (0, 1, 0). A record pointing straight ahead (yaw 0,
// pitch 0) therefore aims along f: 30 deg BELOW the horizon.
void TestThirtyDegreePitchByHand()
{
    const float s = std::sin(30.0f * kDeg), c = std::cos(30.0f * kDeg);
    const float up[3]{s, 0.0f, c};
    Frame r{};
    Check(BuildFrame(up, r), "30 deg: a frame");
    const float f[3]{c, 0.0f, -s}, l[3]{0.0f, 1.0f, 0.0f};
    Check(Near3(r.f, f), "30 deg: f = normalize(Y x u) = (cos 30, 0, -sin 30) (the game's order, not u x Y)");
    Check(Near3(r.l, l), "30 deg: l = u x f = +Y");
    Angles w{};
    Check(WorldFromRecord(r, 0.0f, 0.0f, w) && Near(w.yaw, 0.0f, 1e-5f) && Near(w.pitch, -30.0f * kDeg, 1e-5f),
          "30 deg: record (0, 0) aims 30 deg below the horizon in the world");
    // ...so to aim level and straight ahead the record must hold +30 deg of pitch.
    Angles rec{};
    Check(RecordFromWorld(r, 0.0f, 0.0f, 0.0f, rec) && Near(rec.yaw, 0.0f, 1e-5f) && Near(rec.pitch, 30.0f * kDeg, 1e-5f),
          "30 deg: a level world aim needs record pitch +30 deg");
    // A world aim 90 deg to the left (yaw pi/2, level) lies along l: record yaw pi/2, pitch 0.
    Check(RecordFromWorld(r, kPi / 2.0f, 0.0f, 0.0f, rec) && Near(rec.yaw, kPi / 2.0f, 1e-5f) && Near(rec.pitch, 0.0f, 1e-5f),
          "30 deg: a world aim along the pitch axis keeps its angles");
    // The witness: the record that the inverse produced reads 0 deg against the world aim; the raw world angles
    // written into the record read the tilt.
    Check(Near(FrameCheckDeg(r, 0.0f, 30.0f * kDeg, 0.0f, 0.0f), 0.0f, 1e-3f), "30 deg: frame check of the inverse is 0");
    Check(Near(FrameCheckDeg(r, 0.0f, 0.0f, 0.0f, 0.0f), 30.0f, 1e-3f),
          "30 deg: world angles written raw are off by the tilt (the bug this lane fixes)");
    Check(Near(TiltDeg(up), 30.0f, 1e-3f), "30 deg: the tilt reads 30 deg");
}

// Roll: the vehicle rolled 20 deg about world X. u = (0, -sin 20, cos 20). f = normalize(Y x u) = (cos 20, 0, 0)
// normalised = +X; l = u x f.
void TestRollByHand()
{
    const float s = std::sin(20.0f * kDeg), c = std::cos(20.0f * kDeg);
    const float up[3]{0.0f, -s, c};
    Frame r{};
    Check(BuildFrame(up, r), "roll: a frame");
    const float f[3]{1.0f, 0.0f, 0.0f}, l[3]{0.0f, c, s};
    Check(Near3(r.f, f), "roll: f stays +X (the roll axis)");
    Check(Near3(r.l, l), "roll: l = u x f = (0, cos 20, sin 20)");
    Angles w{};
    // Record yaw pi/2 (left) lifts with the roll: world pitch +20 deg.
    Check(WorldFromRecord(r, kPi / 2.0f, 0.0f, w) && Near(w.yaw, kPi / 2.0f, 1e-4f) && Near(w.pitch, 20.0f * kDeg, 1e-4f),
          "roll: a record aiming left aims 20 deg up in the world");
}

// The fallback: u parallel to world Y (a vehicle on its side) makes Y x u zero; the game takes Z x u.
void TestFallback()
{
    const float up[3]{0.0f, 1.0f, 0.0f};
    Frame r{};
    Check(BuildFrame(up, r), "fallback: a frame for u = +Y");
    const float f[3]{-1.0f, 0.0f, 0.0f};   // Z x (0,1,0) = (-1, 0, 0)
    Check(Near3(r.f, f), "fallback: f = normalize(Z x u) = -X");
    // l = u x f = (0,1,0) x (-1,0,0) = (0, 0, 1)
    const float l[3]{0.0f, 0.0f, 1.0f};
    Check(Near3(r.l, l), "fallback: l = u x f = +Z");
    // Just inside the game's epsilon: |Y x u| = |(uz, 0, -ux)| < 1e-4 still falls back.
    const float nearUp[3]{5e-5f, -1.0f, 5e-5f};
    Check(BuildFrame(nearUp, r) && Near(r.f[2], 0.0f, 1e-6f) && Near(std::fabs(r.f[0]), 1.0f, 1e-4f),
          "fallback: |Y x u| below 1e-4 takes Z x u, as the game's normalize returns 0 there");
    // Just outside it: the game normalises Y x u even though it is tiny.
    const float edgeUp[3]{2e-4f, -1.0f, 0.0f};
    Check(BuildFrame(edgeUp, r) && Near(r.f[1], 0.0f, 1e-6f) && Near(r.f[2], -1.0f, 1e-4f),
          "fallback: |Y x u| of 2e-4 is normalised (not the fallback)");
    const float zero[3]{0.0f, 0.0f, 0.0f};
    Check(!BuildFrame(zero, r), "a zero up has no frame");
    const float bad[3]{std::nanf(""), 0.0f, 1.0f};
    Check(!BuildFrame(bad, r), "a NaN up has no frame");
}

// Round trip: for many tilts and world aims, R * e(RecordFromWorld(w)) == e(w).
void TestRoundTrip()
{
    unsigned bad = 0, total = 0;
    for (float pitch = -80.0f; pitch <= 80.0f; pitch += 20.0f)
        for (float roll = -60.0f; roll <= 60.0f; roll += 30.0f)
            for (float heading = 0.0f; heading < 360.0f; heading += 72.0f)
            {
                float up[3];
                UpFor(pitch * kDeg, roll * kDeg, heading * kDeg, up);
                Frame r{};
                if (!BuildFrame(up, r))
                {
                    ++bad;
                    continue;
                }
                for (float yaw = 0.1f; yaw < 6.2f; yaw += 0.9f)
                    for (float p = -1.3f; p <= 1.3f; p += 0.65f)
                    {
                        ++total;
                        Angles rec{}, back{};
                        if (!RecordFromWorld(r, yaw, p, 0.0f, rec) || !(rec.yaw >= 0.0f && rec.yaw < 2.0f * kPi) ||
                            !WorldFromRecord(r, rec.yaw, rec.pitch, back) || AngleDiff(back.yaw, yaw) > 1e-3f ||
                            std::fabs(back.pitch - p) > 1e-3f ||
                            !(FrameCheckDeg(r, rec.yaw, rec.pitch, yaw, p) < 0.05f))
                            ++bad;
                    }
            }
    Check(total > 1000 && bad == 0, "round trip: R e(R^-1 w) == w over pitch, roll and heading (yaw in [0, 2pi))");
}

// An unnormalised u (the game never normalises it): R is then not a rotation (l and u carry |u|), and the inverse
// must still be exact for THAT R -- R e(record) points along the world aim -- rather than for a normalised one.
void TestUnnormalisedUp()
{
    float up[3];
    UpFor(25.0f * kDeg, 10.0f * kDeg, 1.0f, up);
    float big[3]{up[0] * 1.5f, up[1] * 1.5f, up[2] * 1.5f};
    Frame a{}, b{};
    Check(BuildFrame(up, a) && BuildFrame(big, b), "scaled up: frames");
    Angles ra{}, rb{}, back{};
    Check(RecordFromWorld(a, 2.0f, 0.4f, 0.0f, ra) && RecordFromWorld(b, 2.0f, 0.4f, 0.0f, rb),
          "scaled up: record angles");
    Check(WorldFromRecord(b, rb.yaw, rb.pitch, back) && Near(back.yaw, 2.0f, 1e-4f) && Near(back.pitch, 0.4f, 1e-4f),
          "scaled up: the game's own (unnormalised) R takes the record back onto the world aim");
    Check(!(Near(ra.yaw, rb.yaw, 1e-3f) && Near(ra.pitch, rb.pitch, 1e-3f)),
          "scaled up: a scaled u is a different R (the inverse is not computed from a normalised copy)");
}

// The record's own up pole: no yaw there, the fallback is kept rather than a jump.
void TestPoleKeepsYaw()
{
    float up[3];
    UpFor(30.0f * kDeg, 0.0f, 0.0f, up);
    Frame r{};
    Check(BuildFrame(up, r), "pole: a frame");
    // The world aim along u itself maps to record pitch +90 deg, where yaw is meaningless.
    Angles world{};
    Check(AnglesOf(up, 0.0f, world), "pole: u's angles");
    Angles rec{};
    Check(RecordFromWorld(r, world.yaw, world.pitch, 1.234f, rec) && Near(rec.yaw, 1.234f, 1e-5f) &&
              Near(rec.pitch, kPi / 2.0f, 1e-3f),
          "pole: aiming along the record's up keeps the fallback yaw and reads pitch +90");
}

void TestFrameReadAgrees()
{
    const float fwd[3]{1, 0, 0}, up[3]{0, 0, 1};
    Check(FrameReadAgrees(fwd, up), "agrees: an orthonormal pair");
    const float longUp[3]{0, 0, 1.05f};
    Check(!FrameReadAgrees(fwd, longUp), "agrees: refuses an up 5 % long");
    const float slightlyLong[3]{0, 0, 1.01f};
    Check(FrameReadAgrees(fwd, slightlyLong), "agrees: accepts 1 % (float drift in a node chain)");
    const float tilted[3]{std::sin(5.0f * kDeg), 0, std::cos(5.0f * kDeg)};
    Check(!FrameReadAgrees(fwd, tilted), "agrees: refuses a forward 5 deg off perpendicular (a neighbouring field)");
    const float zero[3]{0, 0, 0};
    Check(!FrameReadAgrees(fwd, zero), "agrees: refuses a zero up");
    const float nan[3]{0, std::nanf(""), 1};
    Check(!FrameReadAgrees(fwd, nan), "agrees: refuses a NaN");
}

void TestClassify()
{
    Check(Classify(false, true, true, false, false, true) == Case::Unknown, "classify: unreadable is unknown");
    Check(Classify(true, false, false, false, false, false) == Case::World,
          "classify: the rebase's own gate exits first (no parent or seat) is world");
    Check(Classify(true, true, false, false, false, true) == Case::Unknown, "classify: flags unread is unknown");
    Check(Classify(true, true, true, true, false, false) == Case::World, "classify: bit 4 is world (no u needed)");
    Check(Classify(true, true, true, true, true, false) == Case::World, "classify: bit 4 with the skip is world");
    Check(Classify(true, true, true, false, true, false) == Case::SkipWorld,
          "classify: no bit 4 with our skip is case (a), world angles, u not needed");
    Check(Classify(true, true, true, false, false, true) == Case::Rebased, "classify: no bit 4, no skip: case (b)");
    Check(Classify(true, true, true, false, false, false) == Case::Unknown,
          "classify: case (b) with no usable frame makes no write");
    Check(std::string_view(CaseName(Case::SkipWorld)) == "a" && std::string_view(CaseName(Case::Rebased)) == "b" &&
              std::string_view(CaseName(Case::World)) == "world",
          "classify: the log names");
}

// The yaw-only write: the world heading R e(written yaw, record pitch) lands on, against the hand's.
float WorldYawErrorDeg(const Frame& r, float yaw, float recordPitch, float handYaw)
{
    Angles w{};
    if (!WorldFromRecord(r, yaw, recordPitch, w))
        return 1e9f;
    return std::fabs(std::remainder(w.yaw - handYaw, 2.0f * kPi)) / kDeg;
}

void TestYawOnlyWrite()
{
    // The worked example: 30 deg of roll, the hand 20 deg up at heading 0, the record still level in pitch.
    const float up[3]{0.0f, -std::sin(30.0f * kDeg), std::cos(30.0f * kDeg)};
    Frame r{};
    Check(BuildFrame(up, r), "yaw-only: a rolled frame");
    const float handYaw = 0.0f, handPitch = 20.0f * kDeg, recordPitch = 0.0f;
    Angles old{};
    Check(RecordFromWorld(r, handYaw, handPitch, 0.0f, old), "yaw-only: the old conversion resolves");
    const float oldErr = WorldYawErrorDeg(r, old.yaw, recordPitch, handYaw);
    float y = 0.0f;
    Check(RecordYawForWorldYaw(r, handYaw, old.yaw, recordPitch, y), "yaw-only: the new conversion resolves");
    const float newErr = WorldYawErrorDeg(r, y, recordPitch, handYaw);
    Check(oldErr > 5.0f, "yaw-only: the old conversion (the hand's pitch) misses the heading by several degrees");
    Check(newErr < 0.5f * oldErr, "yaw-only: the current world pitch at least halves it in one update");
    // Each update the record yaw is the last write: the residual shrinks to nothing.
    for (int i = 0; i < 6; ++i)
        Check(RecordYawForWorldYaw(r, handYaw, y, recordPitch, y), "yaw-only: resolves every update");
    Check(WorldYawErrorDeg(r, y, recordPitch, handYaw) < 0.05f, "yaw-only: converges on the hand's heading");
    // Over a sweep of headings, record pitches and starting yaws, every update brings it closer.
    bool closer = true;
    for (int h = -170; h < 180; h += 20)
        for (int p0 = -20; p0 <= 20; p0 += 20)
        {
            float start = 0.0f, next = 0.0f;
            Angles o{};
            if (!RecordFromWorld(r, h * kDeg, 25.0f * kDeg, 0.0f, o) ||
                !RecordYawForWorldYaw(r, h * kDeg, o.yaw, p0 * kDeg, next))
            {
                closer = false;
                continue;
            }
            start = o.yaw;
            const float before = WorldYawErrorDeg(r, start, p0 * kDeg, h * kDeg);
            const float after = WorldYawErrorDeg(r, next, p0 * kDeg, h * kDeg);
            if (after > before + 1e-3f && after > 0.05f)
                closer = false;
        }
    Check(closer, "yaw-only: never further from the hand's heading than the old conversion, over a sweep");
    // Level: exactly the hand's yaw, whatever the record's pitch.
    Frame level{};
    const float z[3]{0.0f, 0.0f, 1.0f};
    Check(BuildFrame(z, level) && RecordYawForWorldYaw(level, 1.0f, 0.2f, -0.3f, y) && Near(y, 1.0f, 1e-5f),
          "yaw-only: level is the hand's yaw");
}

// Reach's hand-back to the stick servo (Game_ReachDirectDriveAimWhy 0x400): never a frozen seat.
void TestHandBack()
{
    Check(SeatFrameHandsBack(false, 0, 1000), "hand-back: an unbound reader always hands the seat back");
    Check(SeatFrameHandsBack(false, 900, 1000), "hand-back: ...whatever the stamp says");
    Check(!SeatFrameHandsBack(true, 0, 1000), "hand-back: bound and never Unknown: the state write may drive");
    Check(SeatFrameHandsBack(true, 1000, 1000), "hand-back: Unknown this instant: the servo");
    Check(SeatFrameHandsBack(true, 1000, 1000 + kUnreadHandBackMs), "hand-back: ...through the hold window");
    Check(!SeatFrameHandsBack(true, 1000, 1001 + kUnreadHandBackMs), "hand-back: ...and the state write after it");
    Check(SeatFrameHandsBack(true, 1000, 999),
          "hand-back: a reader clock just behind the sim thread's stamp counts as fresh, not as 2^64 ms old");
    Check(SeatFrameHandsBack(true, 5000, 5100, 200) && !SeatFrameHandsBack(true, 5000, 5201, 200),
          "hand-back: the hold is a parameter");
}
} // namespace

int main()
{
    TestLevelIsIdentity();
    TestThirtyDegreePitchByHand();
    TestRollByHand();
    TestFallback();
    TestRoundTrip();
    TestUnnormalisedUp();
    TestPoleKeepsYaw();
    TestFrameReadAgrees();
    TestClassify();
    TestHandBack();
    TestYawOnlyWrite();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
