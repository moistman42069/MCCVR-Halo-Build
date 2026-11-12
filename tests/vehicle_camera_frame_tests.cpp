// Where a vehicle camera sits (src/common/vehicle_camera_frame_logic.h): frames and their angles, the frame that
// follows only some of a vehicle's rotations, file offsets as world vectors, and the leash.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/common/vehicle_camera_frame_logic.h"

using namespace vehicle_camera_frame;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static constexpr float kPi = 3.14159265358979323846f;
static float Radians(float degrees)
{
    return degrees * kPi / 180.0f;
}
static bool Near(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}
static bool Near3(const float a[3], float x, float y, float z, float tolerance = 1.0e-4f)
{
    return Near(a[0], x, tolerance) && Near(a[1], y, tolerance) && Near(a[2], z, tolerance);
}
static bool SameBasis(const Basis& a, const Basis& b, float tolerance = 1.0e-4f)
{
    return Near3(a.forward, b.forward[0], b.forward[1], b.forward[2], tolerance) &&
           Near3(a.left, b.left[0], b.left[1], b.left[2], tolerance) &&
           Near3(a.up, b.up[0], b.up[1], b.up[2], tolerance);
}
// The compass bearing of a direction, seen from above.
static float Bearing(const float v[3])
{
    return std::atan2(v[1], v[0]);
}

static void TestFrames()
{
    const Basis level = FromAngles(0.0f, 0.0f, 0.0f);
    Check(Near3(level.forward, 1, 0, 0) && Near3(level.left, 0, 1, 0) && Near3(level.up, 0, 0, 1),
          "no angles: forward +X, left +Y, up +Z");
    const Basis turned = FromAngles(Radians(90.0f), 0.0f, 0.0f);
    Check(Near3(turned.forward, 0, 1, 0) && Near3(turned.left, -1, 0, 0), "a positive yaw turns to the left");
    const Basis nosedUp = FromAngles(0.0f, Radians(30.0f), 0.0f);
    Check(nosedUp.forward[2] > 0.49f && nosedUp.up[0] < -0.49f && Near3(nosedUp.left, 0, 1, 0),
          "a positive pitch lifts the nose");
    const Basis banked = FromAngles(0.0f, 0.0f, Radians(30.0f));
    Check(banked.left[2] > 0.49f && banked.up[1] < -0.49f && Near3(banked.forward, 1, 0, 0),
          "a positive roll lifts the left side");

    // Every frame is right-handed and orthonormal, and its angles build it again.
    for (const float yaw : {-170.0f, -45.0f, 0.0f, 60.0f, 179.0f})
        for (const float pitch : {-80.0f, -20.0f, 0.0f, 35.0f, 85.0f})
            for (const float roll : {-175.0f, -30.0f, 0.0f, 45.0f, 120.0f})
            {
                const Basis basis = FromAngles(Radians(yaw), Radians(pitch), Radians(roll));
                Check(Usable(basis), "a built frame is usable");
                float y = 0.0f, p = 0.0f, r = 0.0f;
                ToAngles(basis, y, p, r);
                Check(SameBasis(FromAngles(y, p, r), basis, 2.0e-4f), "a frame's angles build the same frame");
                Check(Near(Heading(basis), Radians(yaw), 2.0e-4f), "the heading is the yaw it was built with");
            }

    // Straight up: the heading is gone, and the frame is still rebuilt from its angles.
    const Basis vertical = FromAngles(Radians(40.0f), Radians(90.0f), 0.0f);
    float y = 0.0f, p = 0.0f, r = 0.0f;
    ToAngles(vertical, y, p, r);
    Check(SameBasis(FromAngles(y, p, r), vertical, 1.0e-3f), "a vertical frame survives its angles");

    Basis built{};
    const float forward[3]{0.0f, 3.0f, 0.0f};
    const float roughUp[3]{0.0f, 0.2f, 1.0f};
    Check(FromForwardUp(forward, roughUp, built) && Near3(built.forward, 0, 1, 0) && Near3(built.left, -1, 0, 0) &&
              Near3(built.up, 0, 0, 1),
          "forward and a rough up make a frame");
    const float zero[3]{0.0f, 0.0f, 0.0f};
    const float parallel[3]{0.0f, -1.0f, 0.0f};
    const float notANumber[3]{std::nanf(""), 0.0f, 0.0f};
    const Basis before = built;
    Check(!FromForwardUp(zero, roughUp, built) && !FromForwardUp(forward, parallel, built) &&
              !FromForwardUp(notANumber, roughUp, built) && SameBasis(built, before, 0.0f),
          "no direction, a parallel up or a non-number makes none, and the output is untouched");

    Basis skewed = level;
    skewed.left[0] = 0.5f;
    Basis mirrored = level;
    mirrored.left[1] = -1.0f;
    Basis broken = level;
    broken.up[2] = std::nanf("");
    Check(!Usable(skewed) && !Usable(mirrored) && !Usable(broken), "skewed, left-handed and non-finite frames are refused");
}

static void TestTracked()
{
    // A vehicle heading 50 degrees left, nose up 20, banked 15.
    const Basis vehicle = FromAngles(Radians(50.0f), Radians(20.0f), Radians(15.0f));
    const float freeYaw = Radians(-110.0f);

    Check(SameBasis(Tracked(vehicle, true, true, true, freeYaw), vehicle), "all three tracked is the vehicle's frame");

    const Basis none = Tracked(vehicle, false, false, false, freeYaw);
    Check(SameBasis(none, FromAngles(freeYaw, 0.0f, 0.0f)), "none tracked is level at the free heading");

    const Basis yawOnly = Tracked(vehicle, true, false, false, freeYaw);
    Check(SameBasis(yawOnly, FromAngles(Radians(50.0f), 0.0f, 0.0f)), "yaw only is level at the vehicle's heading");

    const Basis yawPitch = Tracked(vehicle, true, true, false, freeYaw);
    Check(SameBasis(yawPitch, FromAngles(Radians(50.0f), Radians(20.0f), 0.0f)), "yaw and pitch leaves the bank out");

    // Tilt without yaw: the frame stands on the vehicle's deck and keeps its own compass bearing, whatever the
    // deck does.
    for (const float heading : {-170.0f, -110.0f, -20.0f, 0.0f, 50.0f, 95.0f, 180.0f})
    {
        const Basis tilt = Tracked(vehicle, false, true, true, Radians(heading));
        Check(Usable(tilt), "a tilted frame is usable");
        Check(Near3(tilt.up, vehicle.up[0], vehicle.up[1], vehicle.up[2]), "its up is the vehicle's");
        Check(Near(YawDelta(Bearing(tilt.forward), Radians(heading)), 0.0f, 2.0e-4f),
              "its bearing is exactly the free heading");
    }
    // Facing along the vehicle it reads the vehicle's pitch; facing across it, the vehicle's pitch is a bank.
    const Basis nose = FromAngles(Radians(50.0f), Radians(20.0f), 0.0f);
    const Basis along = Tracked(nose, false, true, true, Radians(50.0f));
    Check(SameBasis(along, nose), "facing the vehicle's way, the tilt is the vehicle's pitch");
    const Basis across = Tracked(nose, false, true, true, Radians(140.0f));
    Check(Near(across.forward[2], 0.0f) && across.left[2] < -0.3f, "facing across it, the same tilt is a bank");

    // Pitch alone and roll alone, without yaw.
    const Basis pitchOnly = Tracked(vehicle, false, true, false, Radians(50.0f));
    Check(SameBasis(pitchOnly, FromAngles(Radians(50.0f), Radians(20.0f), 0.0f)), "pitch alone at the vehicle's heading");
    const Basis rollOnly = Tracked(vehicle, false, false, true, Radians(50.0f));
    Check(SameBasis(rollOnly, FromAngles(Radians(50.0f), 0.0f, Radians(15.0f))), "roll alone at the vehicle's heading");

    // A deck standing on its edge cannot carry a free heading: the frame stays usable and keeps the vehicle's.
    const Basis onEdge = FromAngles(Radians(10.0f), 0.0f, Radians(90.0f));
    const Basis kept = Tracked(onEdge, false, true, true, Radians(120.0f));
    Check(Usable(kept) && SameBasis(kept, onEdge), "a deck on edge keeps the vehicle's own heading");

    // The vehicle turning under an untethered frame does not turn it.
    const Basis later = FromAngles(Radians(170.0f), Radians(-10.0f), Radians(40.0f));
    Check(SameBasis(Tracked(later, false, false, false, freeYaw), none), "an untracked vehicle moves nothing");
}

static void TestOffsets()
{
    const Basis level = FromAngles(0.0f, 0.0f, 0.0f);
    float world[3];
    const float behind[3]{-304.8f, 0.0f, 0.0f};
    OffsetToWorld(level, behind, 1.0f, world);
    Check(Near3(world, -1, 0, 0), "304.8 cm behind is one world unit back");
    const float right[3]{0.0f, 152.4f, 0.0f};
    OffsetToWorld(level, right, 1.0f, world);
    Check(Near3(world, 0, -0.5f, 0), "the file's second number is to the RIGHT: against the frame's left");
    const float mixed[3]{304.8f, -304.8f, 609.6f};
    OffsetToWorld(level, mixed, 1.0f, world);
    Check(Near3(world, 1, 1, 2), "forward, left of centre, and up");
    OffsetToWorld(level, mixed, 0.25f, world);
    Check(Near3(world, 0.25f, 0.25f, 0.5f), "a shorter reach scales the whole offset");

    // Along a turned and tilted frame the offset keeps its length and its place on the frame's axes.
    const Basis turned = FromAngles(Radians(90.0f), Radians(30.0f), Radians(-20.0f));
    OffsetToWorld(turned, mixed, 1.0f, world);
    Check(Near(Dot(world, turned.forward), 1.0f) && Near(Dot(world, turned.left), 1.0f) && Near(Dot(world, turned.up), 2.0f),
          "an offset lies along its frame's axes");
    Check(Near(std::sqrt(Dot(world, world)), std::sqrt(6.0f)), "and keeps its length");
    Check(Near(kWorldUnitsPerCm * 304.8f, 1.0f, 1.0e-6f), "a world unit is 304.8 cm");
}

static void TestLeash()
{
    const float unleashed = 1.0e9f;
    const Basis level = FromAngles(0.0f, 0.0f, 0.0f);

    // An offset [0, 0, 60] with limits back 10, left 15 (right -15), down to 40; forward 15, right 20, up 65.
    const float low[3]{-10.0f, -15.0f, 40.0f};
    const float high[3]{15.0f, 20.0f, 65.0f};
    const float camera[3]{0.0f, 0.0f, 60.0f};
    const LeashBox box = LeashToBasis(low, high, camera);
    Check(Near(box.low[0], -10.0f * kWorldUnitsPerCm) && Near(box.high[0], 15.0f * kWorldUnitsPerCm), "forward limits");
    Check(Near(box.low[1], -20.0f * kWorldUnitsPerCm) && Near(box.high[1], 15.0f * kWorldUnitsPerCm),
          "the right limit is the lowest left, the left limit the highest");
    Check(Near(box.low[2], -20.0f * kWorldUnitsPerCm) && Near(box.high[2], 5.0f * kWorldUnitsPerCm),
          "up limits are measured from the camera's point");

    float shift[3];
    const float inside[3]{0.02f, -0.03f, 0.01f};
    LeashShift(inside, level, box, shift);
    Check(Near3(shift, 0, 0, 0, 1.0e-7f), "inside the box nothing moves");
    const float tooHigh[3]{0.0f, 0.0f, 0.10f};
    LeashShift(tooHigh, level, box, shift);
    Check(Near3(shift, 0, 0, 0.10f - 5.0f * kWorldUnitsPerCm), "past the ceiling the overshoot comes back");
    const float tooFarRight[3]{0.03f, -0.20f, 0.0f};
    LeashShift(tooFarRight, level, box, shift);
    Check(Near3(shift, 0, -0.20f + 20.0f * kWorldUnitsPerCm, 0), "past the right limit only that axis is held");
    const float corner[3]{-1.0f, 1.0f, -1.0f};
    LeashShift(corner, level, box, shift);
    Check(Near3(shift, -1.0f + 10.0f * kWorldUnitsPerCm, 1.0f - 15.0f * kWorldUnitsPerCm, -1.0f + 20.0f * kWorldUnitsPerCm),
          "past three limits each axis is held at its own");

    // A camera outside its own box: the limit on the far side of it counts as the camera's point, so the leash
    // never moves the view, it only stops it.
    const float outside[3]{0.0f, 0.0f, 80.0f};
    const LeashBox clipped = LeashToBasis(low, high, outside);
    Check(Near(clipped.high[2], 0.0f) && Near(clipped.low[2], -40.0f * kWorldUnitsPerCm), "the camera's point stays inside");
    const float still[3]{0.0f, 0.0f, 0.0f};
    LeashShift(still, level, clipped, shift);
    Check(Near3(shift, 0, 0, 0, 1.0e-7f), "a head at the camera's point is never moved");

    // No limit at all, and a limit on one side only.
    const float noLow[3]{-unleashed, -unleashed, -unleashed};
    const float noHigh[3]{unleashed, unleashed, unleashed};
    const LeashBox open = LeashToBasis(noLow, noHigh, camera);
    const float far[3]{50.0f, -50.0f, 50.0f};
    LeashShift(far, level, open, shift);
    Check(Near3(shift, 0, 0, 0, 1.0e-7f), "without a leash the head goes anywhere");
    const float ceilingOnly[3]{unleashed, unleashed, 60.0f};
    const LeashBox duck = LeashToBasis(noLow, ceilingOnly, camera);
    const float risen[3]{2.0f, 2.0f, 0.5f};
    LeashShift(risen, level, duck, shift);
    Check(Near3(shift, 0, 0, 0.5f), "a ceiling at the camera's own height: duck, never rise");

    // Along a turned frame the box turns with it.
    const Basis turned = FromAngles(Radians(90.0f), 0.0f, 0.0f);
    const float ahead[3]{0.0f, 1.0f, 0.0f}; // one unit along the turned frame's forward
    LeashShift(ahead, turned, box, shift);
    Check(Near3(shift, 0, 1.0f - 15.0f * kWorldUnitsPerCm, 0), "the box lies along the frame it is given");
}

static void TestYawDelta()
{
    Check(Near(YawDelta(Radians(10.0f), Radians(-10.0f)), Radians(20.0f)), "a small turn");
    Check(Near(YawDelta(Radians(-170.0f), Radians(170.0f)), Radians(20.0f)), "across the seam the short way");
    Check(Near(YawDelta(Radians(170.0f), Radians(-170.0f)), Radians(-20.0f)), "and back");
    Check(Near(YawDelta(Radians(720.0f + 5.0f), 0.0f), Radians(5.0f), 1.0e-3f), "whole turns drop out");
    Check(Near(YawDelta(Radians(180.0f), 0.0f), kPi, 1.0e-5f) || Near(YawDelta(Radians(180.0f), 0.0f), -kPi, 1.0e-5f),
          "half a turn is half a turn");
}

// A frame seen from another, and a parent turned as far as its child has turned since a rest pose.
static void TestTurnedSince()
{
    const Basis parent = FromAngles(0.3f, 0.1f, -0.2f);
    // A child whose own axes point nowhere useful (a head marker's up its neck), in the parent's coordinates.
    const Basis rest = FromAngles(1.2f, -0.7f, 0.4f);
    Basis world{};
    FromBasis(parent, rest.forward, world.forward);
    FromBasis(parent, rest.left, world.left);
    FromBasis(parent, rest.up, world.up);
    Check(SameBasis(InFrame(parent, world), rest), "a frame seen from its parent is what it was laid out as");
    Check(SameBasis(TurnedSince(parent, rest, rest), parent), "a child at rest leaves the parent as it is");
    // The child turned 30 degrees left about the parent's up, from rest.
    const Basis turn = FromAngles(Radians(30.0f), 0.0f, 0.0f);
    Basis turned{};
    FromBasis(turn, rest.forward, turned.forward);
    FromBasis(turn, rest.left, turned.left);
    FromBasis(turn, rest.up, turned.up);
    Basis expected{};
    FromBasis(parent, turn.forward, expected.forward);
    FromBasis(parent, turn.left, expected.left);
    FromBasis(parent, turn.up, expected.up);
    const Basis got = TurnedSince(parent, turned, rest);
    Check(SameBasis(got, expected), "the parent turns 30 degrees left about its own up, as the child did");
    Check(Usable(got), "and stays a frame");
    // A nod: 20 degrees nose down about the parent's left.
    const Basis nod = FromAngles(0.0f, Radians(-20.0f), 0.0f);
    Basis nodded{};
    FromBasis(nod, rest.forward, nodded.forward);
    FromBasis(nod, rest.left, nodded.left);
    FromBasis(nod, rest.up, nodded.up);
    Basis nodExpected{};
    FromBasis(parent, nod.forward, nodExpected.forward);
    FromBasis(parent, nod.left, nodExpected.left);
    FromBasis(parent, nod.up, nodExpected.up);
    Check(SameBasis(TurnedSince(parent, nodded, rest), nodExpected), "a nod tilts the parent the same way");
}

int main()
{
    TestFrames();
    TestTracked();
    TestOffsets();
    TestLeash();
    TestYawDelta();
    TestTurnedSince();
    std::printf("PASS: %u vehicle camera frame checks\n", g_checks);
    return 0;
}
