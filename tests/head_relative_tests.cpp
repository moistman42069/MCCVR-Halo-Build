// Head-relative HUD and sound, the shared pure half (src/common/head_relative_logic.h): the heading of a forward
// vector, the yaw between two headings, the game's forward with the head's yaw, the head sample's verdict, and the
// two acceptance cases the player checks in the headset as geometry.
#include "head_relative_logic.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

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
bool Near3(const float a[3], float x, float y, float z, float tol = 1.0e-4f)
{
    return Near(a[0], x, tol) && Near(a[1], y, tol) && Near(a[2], z, tol);
}
// The difference of two angles, wrapped into (-pi, pi].
float Wrap(float a)
{
    while (a > head_relative::kPi)
        a -= 2.0f * head_relative::kPi;
    while (a <= -head_relative::kPi)
        a += 2.0f * head_relative::kPi;
    return a;
}

using namespace head_relative;

const float kUp[3]{0.0f, 0.0f, 1.0f};  // Blam's world up

// A forward vector at `yawDeg` (counter-clockwise from +X, seen from above) and `pitchDeg` above the horizon.
void Forward(float yawDeg, float pitchDeg, float out[3])
{
    const float yaw = yawDeg * kDegToRad, pitch = pitchDeg * kDegToRad;
    out[0] = std::cos(pitch) * std::cos(yaw);
    out[1] = std::cos(pitch) * std::sin(yaw);
    out[2] = std::sin(pitch);
}

void TestHeading()
{
    float out[3]{9, 9, 9};
    const float pitched[3]{1.0f, 0.0f, 0.5f};
    Check(Heading(pitched, kUp, out) && Near3(out, 1, 0, 0), "a pitched forward: its horizontal part, unit length");
    float f[3];
    Forward(37.0f, -60.0f, f);
    Check(Heading(f, kUp, out) && Near(out[0], std::cos(37.0f * kDegToRad)) &&
              Near(out[1], std::sin(37.0f * kDegToRad)) && Near(out[2], 0.0f),
          "looking 60 deg down still has a heading");
    float steep[3];
    Forward(37.0f, 85.0f, steep);
    float untouched[3]{7, 7, 7};
    Check(!Heading(steep, kUp, untouched) && Near3(untouched, 7, 7, 7), "steeper than 80 deg: no heading, out untouched");
    Forward(37.0f, -89.0f, steep);
    Check(!Heading(steep, kUp, untouched), "nearly straight down: no heading");
    const float zero[3]{0, 0, 0};
    Check(!Heading(zero, kUp, untouched), "a zero forward has no heading");
    const float nan3[3]{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    Check(!Heading(nan3, kUp, untouched) && !Heading(pitched, nan3, untouched), "non-finite inputs are refused");
    const float longUp[3]{0, 0, 2};
    Check(!Heading(pitched, longUp, untouched), "a world up that is not unit length is refused");
    // A scaled forward (a camera matrix row with scale) gives the same heading.
    const float scaled[3]{3.0f, 0.0f, 1.5f};
    Check(Heading(scaled, kUp, out) && Near3(out, 1, 0, 0), "the forward's length does not matter");
}

void TestYaw()
{
    const float x[3]{1, 0, 0}, y[3]{0, 1, 0}, ny[3]{0, -1, 0}, nx[3]{-1, 0, 0};
    Check(Near(SignedYaw(x, y, kUp), 90.0f * kDegToRad), "+X onto +Y about +Z is +90 deg");
    Check(Near(SignedYaw(x, ny, kUp), -90.0f * kDegToRad), "+X onto -Y is -90 deg");
    Check(Near(std::fabs(SignedYaw(x, nx, kUp)), kPi), "opposite headings are 180 deg apart");
    Check(Near(SignedYaw(x, x, kUp), 0.0f), "a heading onto itself is 0");
    float out[3];
    RotateAbout(x, kUp, 90.0f * kDegToRad, out);
    Check(Near3(out, 0, 1, 0), "RotateAbout: +X about +Z by +90 deg is +Y");
    float v[3]{1.0f, 0.0f, 0.7f};
    RotateAbout(v, kUp, 180.0f * kDegToRad, v);
    Check(Near3(v, -1.0f, 0.0f, 0.7f), "RotateAbout keeps the part along the axis, and may alias");
}

void TestWithHeadYaw()
{
    float aim[3], head[3], out[3];
    Forward(0.0f, 17.0f, aim);     // the aim: east, pitched up
    Forward(90.0f, -12.0f, head);  // the head: north, looking a little down
    float applied = 0.0f;
    Check(WithHeadYaw(aim, head, kUp, 1, 0.0f, out, &applied), "aim east, head north: substituted");
    float heading[3];
    Check(Heading(out, kUp, heading) && Near3(heading, 0, 1, 0), "the result's heading is the head's");
    Check(Near(out[2], aim[2]) && Near(Length(out), Length(aim)), "the native pitch and length are kept");
    Check(Near(applied, 90.0f * kDegToRad), "the applied yaw is native -> head");

    Check(WithHeadYaw(aim, head, kUp, -1, 0.0f, out, &applied) && Heading(out, kUp, heading) &&
              Near3(heading, 0, -1, 0) && Near(applied, -90.0f * kDegToRad),
          "sign -1 mirrors the correction");
    Check(WithHeadYaw(aim, head, kUp, 1, 10.0f * kDegToRad, out, &applied) && Near(applied, 100.0f * kDegToRad),
          "the offset is added after the sign");
    Check(WithHeadYaw(aim, head, kUp, 1, std::numeric_limits<float>::quiet_NaN(), out, &applied) &&
              Near(applied, 90.0f * kDegToRad),
          "a non-finite offset counts as none");

    float steep[3], untouched[3]{5, 5, 5};
    Forward(0.0f, 88.0f, steep);
    Check(!WithHeadYaw(aim, steep, kUp, 1, 0.0f, untouched) && Near3(untouched, 5, 5, 5),
          "a head with no heading: refused, out untouched");
    Check(!WithHeadYaw(steep, head, kUp, 1, 0.0f, untouched), "a native forward with no heading: refused");
    // Aliasing: out may be the native vector itself.
    float inPlace[3];
    Forward(200.0f, 5.0f, inPlace);
    const float z = inPlace[2];
    Check(WithHeadYaw(inPlace, head, kUp, 1, 0.0f, inPlace) && Heading(inPlace, kUp, heading) &&
              Near3(heading, 0, 1, 0) && Near(inPlace[2], z),
          "out may alias the native forward");
}

// What the player checks in the headset, as geometry. The tracker draws a contact at its bearing from the heading
// the game is handed; the seam hands it WithHeadYaw(native = the aim, head).
float TrackerBearing(const float contact[3], float aimYawDeg, float headYawDeg, int sign = 1)
{
    float aim[3], head[3], handed[3], heading[3];
    Forward(aimYawDeg, 0.0f, aim);
    Forward(headYawDeg, 0.0f, head);
    if (!WithHeadYaw(aim, head, kUp, sign, 0.0f, handed) || !Heading(handed, kUp, heading))
        return 1.0e9f;
    float bearing = 1.0e9f;
    Bearing(contact, heading, kUp, bearing);
    return bearing;
}

void TestAcceptance()
{
    // (0) A contact straight ahead of the HEAD is at tracker "up" (bearing 0), whatever the aim is.
    bool ahead = true;
    for (float headYaw : {0.0f, 45.0f, 170.0f, -120.0f})
    {
        float contact[3];
        Forward(headYaw, 0.0f, contact);
        contact[0] *= 10.0f;
        contact[1] *= 10.0f;
        contact[2] = 3.0f;  // a contact above the player: height never changes the bearing
        for (float aimYaw : {0.0f, 30.0f, -75.0f, 179.0f})
            ahead = ahead && Near(TrackerBearing(contact, aimYaw, headYaw), 0.0f, 1.0e-3f);
    }
    Check(ahead, "a contact straight ahead of the head is at tracker up, whatever the aim");

    // (1) Head still, sweep the gun: the contact stays put.
    const float fixedContact[3]{4.0f, 7.0f, 0.0f};
    const float reference = TrackerBearing(fixedContact, 0.0f, 20.0f);
    bool still = true;
    for (float aimYaw = -170.0f; aimYaw <= 170.0f; aimYaw += 17.0f)
        still = still && Near(Wrap(TrackerBearing(fixedContact, aimYaw, 20.0f) - reference), 0.0f, 1.0e-3f);
    Check(still, "head still, gun swept through 340 deg: the contact does not move");

    // (2) Gun still, turn the head by +90 deg: a fixed contact turns by -90 deg on the tracker.
    const float before = TrackerBearing(fixedContact, 35.0f, 20.0f);
    const float after = TrackerBearing(fixedContact, 35.0f, 110.0f);
    Check(Near(Wrap(after - before), -90.0f * kDegToRad, 1.0e-3f),
          "gun still, head +90 deg: the contact turns -90 deg on the tracker");

    // The game's own tracker (no substitution) is the control arm: there the GUN moves contacts and the head does not.
    float aimA[3], aimB[3], headingA[3], headingB[3];
    Forward(0.0f, 0.0f, aimA);
    Forward(60.0f, 0.0f, aimB);
    Heading(aimA, kUp, headingA);
    Heading(aimB, kUp, headingB);
    float nativeA = 0.0f, nativeB = 0.0f;
    Bearing(fixedContact, headingA, kUp, nativeA);
    Bearing(fixedContact, headingB, kUp, nativeB);
    Check(Near(Wrap(nativeB - nativeA), -60.0f * kDegToRad, 1.0e-3f),
          "control arm: on the game's own tracker a 60 deg gun sweep moves the contact 60 deg");

    // sign -1: the head's turn is applied the other way (the mirrored seam the live key exists for).
    const float mirroredBefore = TrackerBearing(fixedContact, 0.0f, 0.0f, -1);
    const float mirroredAfter = TrackerBearing(fixedContact, 0.0f, 90.0f, -1);
    Check(Near(Wrap(mirroredAfter - mirroredBefore), 90.0f * kDegToRad, 1.0e-3f),
          "sign -1: head +90 deg turns the contact +90 deg (mirrored)");

    float bearing = 0.0f;
    const float overhead[3]{0.0f, 0.0f, 5.0f};
    Check(!Bearing(overhead, headingA, kUp, bearing), "a contact directly overhead has no bearing");
}

void TestHold()
{
    HeadingHold hold;
    float out[3]{8, 8, 8};
    float steep[3];
    Forward(10.0f, 89.0f, steep);
    Check(!HeldHeading(hold, steep, kUp, out) && Near3(out, 8, 8, 8), "no heading yet and looking up: nothing to hold");
    float level[3];
    Forward(90.0f, 0.0f, level);
    Check(HeldHeading(hold, level, kUp, out) && Near3(out, 0, 1, 0), "a level head: its heading");
    Check(HeldHeading(hold, steep, kUp, out) && Near3(out, 0, 1, 0), "then straight up: the last heading is held");
    Forward(180.0f, 30.0f, level);
    Check(HeldHeading(hold, level, kUp, out) && Near3(out, -1, 0, 0), "a new heading replaces the held one");
}

void TestSampleUse()
{
    HeadSample sample;
    Check(SampleUse(sample, 7, 1000) == Use::NoSample, "nothing published yet");
    Forward(0.0f, 0.0f, sample.forward);
    sample.up[2] = 1.0f;
    sample.stampUs = 1000000;
    sample.generation = 7;
    sample.valid = true;
    Check(SampleUse(sample, 7, 1000000) == Use::Ok, "this frame's sample");
    Check(SampleUse(sample, 7, 1000000 + kMaxSampleAgeUs) == Use::Ok, "exactly at the age limit");
    Check(SampleUse(sample, 7, 1000001 + kMaxSampleAgeUs) == Use::Stale, "one microsecond past it: stale");
    Check(SampleUse(sample, 8, 1000000) == Use::OtherGeneration, "another load of the title");
    Check(SampleUse(sample, 0, 1000000) == Use::OtherGeneration, "no generation to compare with");
    Check(SampleUse(sample, 7, 999999) == Use::FromTheFuture, "a sample stamped after now");
    HeadSample invalid = sample;
    invalid.valid = false;
    Check(SampleUse(invalid, 7, 1000000) == Use::NoSample, "the head view was not live in that frame");
    HeadSample nan = sample;
    nan.forward[1] = std::numeric_limits<float>::quiet_NaN();
    Check(SampleUse(nan, 7, 1000000) == Use::NoHeading, "a non-finite forward");
    bool named = true;
    for (int i = 0; i < static_cast<int>(Use::Count); ++i)
        named = named && UseName(static_cast<Use>(i))[0] != '?';
    Check(named, "every verdict has a name for the log");
}
} // namespace

int main()
{
    TestHeading();
    TestYaw();
    TestWithHeadYaw();
    TestAcceptance();
    TestHold();
    TestSampleUse();
    std::printf("head relative tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
