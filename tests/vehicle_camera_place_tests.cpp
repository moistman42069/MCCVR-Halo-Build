// Where the camera in use is (src/common/vehicle_camera_place_logic.h): the origin a camera is measured from,
// which of the vehicle's rotations carry its offset, an offset that keeps a world heading, and an orbiting one.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "../src/common/vehicle_camera_place_logic.h"

using namespace vehicle_camera_place;
using vehicle_camera_file::Origin;
using vehicle_camera_frame::FromAngles;
using vehicle_camera_frame::kWorldUnitsPerCm;
using vehicle_camera_select::ActiveCamera;

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

static bool Near(float a, float b)
{
    return std::fabs(a - b) <= 1.0e-4f;
}

static bool Near3(const float v[3], float x, float y, float z)
{
    return Near(v[0], x) && Near(v[1], y) && Near(v[2], z);
}

constexpr float kHalfTurn = 3.14159265358979323846f;
constexpr float kQuarterTurn = kHalfTurn / 2.0f;

// A chase camera 300 cm behind, 60 cm to the right and 150 cm up, rigid to the vehicle.
static ActiveCamera Behind()
{
    ActiveCamera camera;
    camera.valid = true;
    camera.chase = true;
    camera.origin = static_cast<uint8_t>(Origin::Vehicle);
    camera.offsetCm[0] = -300.0f;
    camera.offsetCm[1] = 60.0f;
    camera.offsetCm[2] = 150.0f;
    camera.locYaw = camera.locPitch = camera.locRoll = true;
    for (int axis = 0; axis < 3; ++axis)
    {
        camera.leashMinCm[axis] = -vehicle_camera_file::kUnleashed;
        camera.leashMaxCm[axis] = vehicle_camera_file::kUnleashed;
    }
    camera.changes = 1;
    return camera;
}

static Inputs At(float x, float y, float z, float yaw, float pitch, float roll)
{
    Inputs in;
    in.vehicleOrigin[0] = x;
    in.vehicleOrigin[1] = y;
    in.vehicleOrigin[2] = z;
    in.vehicle = FromAngles(yaw, pitch, roll);
    return in;
}

static void TestRigid()
{
    const float back = 300.0f * kWorldUnitsPerCm, right = 60.0f * kWorldUnitsPerCm, up = 150.0f * kWorldUnitsPerCm;
    State state;
    float point[3]{};

    Check(Place(Behind(), At(10.0f, 20.0f, 3.0f, 0.0f, 0.0f, 0.0f), state, point), "a chase camera on a level vehicle");
    Check(Near3(point, 10.0f - back, 20.0f - right, 3.0f + up),
          "sits behind, to the right (minus left) and above the vehicle's origin, in world units");

    // The vehicle faces +Y: behind is -Y, right is +X.
    Check(Place(Behind(), At(10.0f, 20.0f, 3.0f, kQuarterTurn, 0.0f, 0.0f), state, point), "the vehicle turned left");
    Check(Near3(point, 10.0f + right, 20.0f - back, 3.0f + up), "carries a rigid offset round with it");

    // Nose straight up: behind is below, up is back along -X.
    Check(Place(Behind(), At(0.0f, 0.0f, 0.0f, 0.0f, kQuarterTurn, 0.0f), state, point), "the vehicle nose up");
    Check(Near3(point, -up, -right, -back), "pitches a rigid offset with it");
}

static void TestYawOnlyStaysLevel()
{
    ActiveCamera camera = Behind();
    camera.locPitch = camera.locRoll = false;
    State state;
    float point[3]{};
    Check(Place(camera, At(0.0f, 0.0f, 0.0f, kQuarterTurn, 0.5f, -0.4f), state, point), "a yaw-only offset on a tilted vehicle");
    Check(Near3(point, 60.0f * kWorldUnitsPerCm, -300.0f * kWorldUnitsPerCm, 150.0f * kWorldUnitsPerCm),
          "turns with the vehicle and stays level");
}

static void TestWorldHeadingIsKept()
{
    ActiveCamera camera = Behind();
    camera.offsetCm[1] = 0.0f;
    camera.offsetCm[2] = 0.0f;
    camera.locYaw = camera.locPitch = camera.locRoll = false;
    const float back = 300.0f * kWorldUnitsPerCm;
    State state;
    float point[3]{};

    Check(Place(camera, At(0.0f, 0.0f, 0.0f, kQuarterTurn, 0.0f, 0.0f), state, point), "an offset that follows no rotation");
    Check(Near3(point, 0.0f, -back, 0.0f), "starts behind the vehicle as it faces when the camera is chosen");
    Check(Place(camera, At(5.0f, 0.0f, 0.0f, kHalfTurn, 0.3f, 0.0f), state, point), "the vehicle turning and moving on");
    Check(Near3(point, 5.0f, -back, 0.0f), "moves the camera with the vehicle but keeps the offset's world heading");

    camera.changes = 2;
    Check(Place(camera, At(5.0f, 0.0f, 0.0f, kHalfTurn, 0.0f, 0.0f), state, point), "choosing a camera again");
    Check(Near3(point, 5.0f + back, 0.0f, 0.0f), "takes the vehicle's heading of that moment");
}

static void TestOrbit()
{
    ActiveCamera camera = Behind();
    camera.offsetCm[1] = 0.0f;
    camera.offsetCm[2] = 0.0f;
    camera.locView = true;
    const float back = 300.0f * kWorldUnitsPerCm;
    State state;
    float point[3]{};
    Inputs in = At(0.0f, 0.0f, 1.0f, 0.3f, 0.6f, 0.2f);
    in.viewYaw = kQuarterTurn;
    Check(Place(camera, in, state, point), "an orbiting camera");
    Check(Near3(point, 0.0f, -back, 1.0f), "sits behind the way the player faces, level, whatever the vehicle does");
    in.viewYaw = kHalfTurn;
    Check(Place(camera, in, state, point) && Near3(point, back, 0.0f, 1.0f), "and swings round as he turns");
}

static void TestOrigins()
{
    ActiveCamera camera = Behind();
    camera.offsetCm[0] = camera.offsetCm[1] = camera.offsetCm[2] = 0.0f;
    State state;
    float point[3]{};
    Inputs in = At(1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f);
    in.seat[0] = 4.0f;
    in.seat[1] = 5.0f;
    in.seat[2] = 6.0f;
    in.head[0] = 7.0f;
    in.head[1] = 8.0f;
    in.head[2] = 9.0f;

    camera.origin = static_cast<uint8_t>(Origin::Seat);
    Check(Place(camera, in, state, point) && Near3(point, 1.0f, 2.0f, 3.0f), "a seat origin the game has no point for falls back to the vehicle");
    in.seatValid = true;
    Check(Place(camera, in, state, point) && Near3(point, 4.0f, 5.0f, 6.0f), "a seat origin is the seat's point");

    camera.origin = static_cast<uint8_t>(Origin::Head);
    Check(Place(camera, in, state, point) && Near3(point, 4.0f, 5.0f, 6.0f), "a head origin with no head falls back to the seat");
    in.headValid = true;
    Check(Place(camera, in, state, point) && Near3(point, 7.0f, 8.0f, 9.0f), "a head origin is the head");

    camera.origin = static_cast<uint8_t>(Origin::Marker);
    Check(Place(camera, in, state, point) && Near3(point, 1.0f, 2.0f, 3.0f), "a marker the game has not resolved is the vehicle's origin");
}

static void TestThirdPersonRest()
{
    ActiveCamera camera = Behind();
    camera.origin = static_cast<uint8_t>(Origin::ThirdPerson);
    camera.offsetCm[0] = camera.offsetCm[1] = camera.offsetCm[2] = 0.0f;
    State state;
    float point[3]{};

    // The vehicle faces +Y; the game's camera turns about a point above it and stands 2 world units behind.
    Inputs in = At(10.0f, 20.0f, 3.0f, kQuarterTurn, 0.0f, 0.0f);
    in.restValid = true;
    in.rest[0] = 10.0f;
    in.rest[1] = 20.5f;
    in.rest[2] = 4.0f;
    in.restOffset[0] = -2.0f;
    Check(Place(camera, in, state, point), "a camera at the game's own third-person place");
    Check(Near3(point, 10.0f, 18.5f, 4.0f), "stands where the game's own stands from its own turning point");
    Check(!state.standIn, "and that is not the stand-in");

    // The boom's place is behind, to the side and above the turning point, along the carrier: forward, LEFT, up.
    // The vehicle faces +Y, so its left is -X.
    in.restOffset[0] = -1.6f;
    in.restOffset[1] = 0.25f;
    in.restOffset[2] = 1.2f;
    Check(Place(camera, in, state, point) && Near3(point, 9.75f, 18.9f, 5.2f),
          "behind, to the left and above its turning point, as the boom says");
    in.restOffset[0] = in.restOffset[1] = in.restOffset[2] = 0.0f;
    Check(Place(camera, in, state, point) && state.standIn, "no place from the turning point is no resting place");
    in.restOffset[0] = -2.0f;

    camera.offsetCm[0] = 100.0f;
    camera.offsetCm[2] = 50.0f;
    Check(Place(camera, in, state, point) &&
              Near3(point, 10.0f, 18.5f + 100.0f * kWorldUnitsPerCm, 4.0f + 50.0f * kWorldUnitsPerCm),
          "an offset is measured from that resting place");

    // A level, yaw-only carrier keeps the place level on a tilted vehicle; nothing of the vehicle's aim is in it.
    camera.offsetCm[0] = camera.offsetCm[2] = 0.0f;
    camera.locPitch = camera.locRoll = false;
    in.vehicle = FromAngles(kQuarterTurn, 0.4f, 0.3f);
    Check(Place(camera, in, state, point) && Near3(point, 10.0f, 18.5f, 4.0f), "and it rides only the rotations its mode lists");

    in.restValid = false;
    Check(Place(camera, in, state, point), "a game that cannot say where its camera rests");
    Check(Near3(point, 10.0f, 20.0f - kStandInBehindCm * kWorldUnitsPerCm, 3.0f + kStandInAboveCm * kWorldUnitsPerCm),
          "gets the mod's stand-in behind and above the vehicle's origin");
    Check(state.standIn, "and the caller is told");
    // A vehicle larger than a few metres: the stand-in stands off by its size, out of its hull (a Longsword's
    // sphere is some 20 m, a Lich's 342 m).
    in.vehicleRadiusCm = 2000.0f;
    Check(Place(camera, in, state, point) &&
              Near3(point, 10.0f, 20.0f - 3000.0f * kWorldUnitsPerCm, 3.0f + 1000.0f * kWorldUnitsPerCm),
          "a large vehicle's stand-in is behind and above it by its own size");
    float behind = 0.0f, above = 0.0f;
    StandInOffsetCm(100.0f, behind, above);
    Check(behind == kStandInBehindCm && above == kStandInAboveCm, "a small one keeps the mod's own stand-in");
    StandInOffsetCm(std::numeric_limits<float>::quiet_NaN(), behind, above);
    Check(behind == kStandInBehindCm && above == kStandInAboveCm, "and so does one whose size is not a number");
    in.vehicleRadiusCm = 0.0f;
    in.restValid = true;
    in.restOffset[0] = std::numeric_limits<float>::quiet_NaN();
    Check(Place(camera, in, state, point) && state.standIn, "a place that is not a number is no resting place either");
    in.restOffset[0] = -2.0f;
    Check(Place(camera, in, state, point) && !state.standIn, "and a place given again is used again");
}

static void TestCollide()
{
    Check(CollideReach(false, 0.0f, 2.0f, 30.0f) == 1.0f, "nothing met: all the way out");
    Check(CollideReach(true, 3.0f, 2.0f, 30.0f) == 1.0f, "something past the camera: all the way out");
    const float margin = 30.0f * kWorldUnitsPerCm;
    Check(Near(CollideReach(true, 1.0f, 2.0f, 30.0f), (1.0f - margin) / 2.0f), "a wall halfway: stopped the margin short");
    Check(CollideReach(true, 0.05f, 2.0f, 30.0f) == 0.0f, "a wall nearer than the margin: at the pivot");
    Check(CollideReach(true, 1.0f, 0.0f, 30.0f) == 1.0f, "no way out to speak of: nothing to hold in");

    float held = 1.0f;
    Check(EaseReach(held, 0.25f, 0.01f, 1.5f) == 0.25f, "drawn in at once");
    Check(Near(EaseReach(held, 1.0f, 0.1f, 1.5f), 0.4f), "let out at its rate");
    Check(Near(EaseReach(held, 1.0f, 1.0f, 1.5f), 1.0f), "and no further than the way out");
    Check(EaseReach(held, 7.0f, 0.1f, 1.5f) == 1.0f && EaseReach(held, -3.0f, 0.1f, 1.5f) == 0.0f,
          "kept between none and all of the way");
}

static void TestLeash()
{
    ActiveCamera camera = Behind(); // 300 behind, 60 right, 150 up; rigid
    State state;
    float point[3]{};
    Placed placed;
    Check(Place(camera, At(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f), state, point, &placed), "a placement to leash against");
    Check(placed.offsetCm[0] == -300.0f && placed.offsetCm[2] == 150.0f && vehicle_camera_frame::Usable(placed.carrier),
          "the placement says where and in what frame the offset was laid out");
    float head[3]{0.1f, 0.0f, 0.5f};
    Check(!LeashHead(camera, placed, 1.0f, head) && head[2] == 0.5f, "no leash: the head goes where it goes");

    // Up at most 160 cm from the origin: 10 cm above the camera's point.
    for (int axis = 0; axis < 3; ++axis)
    {
        camera.leashMinCm[axis] = vehicle_camera_file::kUnleashed * -1.0f;
        camera.leashMaxCm[axis] = vehicle_camera_file::kUnleashed;
    }
    camera.leashMaxCm[2] = 160.0f;
    Check(LeashHead(camera, placed, 1.0f, head), "a leash on the way up");
    Check(Near(head[2], 10.0f * kWorldUnitsPerCm) && Near(head[0], 0.1f), "stops the head 10 cm up and leaves the rest");
    float down[3]{0.0f, 0.0f, -0.5f};
    Check(LeashHead(camera, placed, 1.0f, down) && Near(down[2], -0.5f), "coming down is free");

    // A camera held in by a wall to half its offset: the ceiling is a place, so there is more room above.
    float lean[3]{0.0f, 0.0f, 0.5f};
    Check(LeashHead(camera, placed, 0.5f, lean), "a leash with the camera held in");
    Check(Near(lean[2], (160.0f - 75.0f) * kWorldUnitsPerCm), "the limit stays where it is in the vehicle");
}

static void TestRefusals()
{
    State state;
    float point[3]{42.0f, 42.0f, 42.0f};
    const Inputs good = At(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    ActiveCamera none;
    Check(!Place(none, good, state, point), "no camera selected places nothing");
    ActiveCamera seatView = Behind();
    seatView.chase = false;
    Check(!Place(seatView, good, state, point), "the seat view places nothing: it is the game's and the mod's own");

    Inputs bad = good;
    bad.vehicle.up[2] = 0.0f;
    Check(!Place(Behind(), bad, state, point), "a frame that is not a frame places nothing");
    bad = good;
    bad.vehicleOrigin[1] = std::numeric_limits<float>::quiet_NaN();
    Check(!Place(Behind(), bad, state, point), "an origin that is not a number places nothing");
    bad = good;
    bad.viewYaw = std::numeric_limits<float>::infinity();
    Check(!Place(Behind(), bad, state, point), "a heading that is not a number places nothing");
    Check(point[0] == 42.0f && point[1] == 42.0f && point[2] == 42.0f, "and a refusal leaves the caller's point alone");
    Check(!state.latched, "and takes no heading");
}

// LAG: the camera follows its place on a spring that also pulls on the place's velocity.
static void TestLag()
{
    ActiveCamera camera = Behind();
    LagState state;
    float out[3]{};
    const float dt = 1.0f / 90.0f;

    // Without lag the point is its place, every frame.
    float place[3] = {1.0f, 2.0f, 3.0f};
    FollowLag(camera, place, dt, state, out);
    Check(Near3(out, 1.0f, 2.0f, 3.0f) && !state.valid, "no lag: the point is its place");

    camera.lagSeconds = 0.3f;
    camera.lagDamping = 1.0f;
    FollowLag(camera, place, -1.0f, state, out);
    Check(Near3(out, 1.0f, 2.0f, 3.0f) && state.valid, "the first frame starts on the place");

    // A steady speed does not lag: 10 world units a second (about 30 m/s), straight on.
    for (int i = 1; i <= 180; ++i)
    {
        place[0] = 1.0f + 10.0f * dt * static_cast<float>(i);
        FollowLag(camera, place, dt, state, out);
    }
    Check(std::fabs(out[0] - place[0]) < 0.01f, "at a steady speed the camera sits on its place");

    // A sudden move of one world unit: about four fifths of it made up after the lag's seconds, never overshot.
    camera.changes = 2;
    place[0] = 0.0f;
    for (int i = 0; i < 5; ++i)
        FollowLag(camera, place, dt, state, out);
    place[0] = 1.0f;
    float most = 0.0f, atLag = 0.0f;
    for (int i = 1; i <= 270; ++i)
    {
        FollowLag(camera, place, dt, state, out);
        most = out[0] > most ? out[0] : most;
        if (i == 27)
            atLag = out[0];
    }
    Check(atLag > 0.72f && atLag < 0.92f, "about four fifths of a sudden move is made up after the lag");
    Check(most < 1.05f && std::fabs(out[0] - 1.0f) < 0.01f,
          "with damping 1 it settles barely past its place: a jump is not taken for a speed");

    // A softer spring swings past its place and back.
    camera.changes = 3;
    camera.lagDamping = 0.3f;
    place[0] = 0.0f;
    for (int i = 0; i < 5; ++i)
        FollowLag(camera, place, dt, state, out);
    place[0] = 1.0f;
    most = 0.0f;
    for (int i = 1; i <= 270; ++i)
    {
        FollowLag(camera, place, dt, state, out);
        most = out[0] > most ? out[0] : most;
    }
    Check(most > 1.1f, "a damping below 1 overshoots");

    // A bump: the place shaken up and down five times a second reaches the camera at a fraction of its size.
    camera.changes = 5;
    camera.lagDamping = 1.0f;
    place[0] = place[1] = place[2] = 0.0f;
    FollowLag(camera, place, dt, state, out);
    float shaken = 0.0f;
    for (int i = 1; i <= 270; ++i)
    {
        place[2] = 0.06f * std::sin(2.0f * 3.14159265f * 5.0f * dt * static_cast<float>(i));
        FollowLag(camera, place, dt, state, out);
        if (i > 135)
            shaken = std::fabs(out[2]) > shaken ? std::fabs(out[2]) : shaken;
    }
    Check(shaken < 0.06f * 0.3f, "a five-times-a-second bump reaches the camera at under a third of its size");

    // The same frame again moves nothing; another camera, a far jump or a long gap starts over on the place.
    camera.lagDamping = 1.0f;
    place[0] = 1.5f;
    FollowLag(camera, place, dt, state, out);
    const float held = out[0];
    FollowLag(camera, place, 0.0f, state, out);
    Check(out[0] == held, "a second call in the same frame moves nothing");
    camera.changes = 4;
    FollowLag(camera, place, dt, state, out);
    Check(out[0] == 1.5f, "another camera starts on its place");
    for (int i = 0; i < 5; ++i)
        FollowLag(camera, place, dt, state, out);
    place[0] = 10.0f;
    FollowLag(camera, place, dt, state, out);
    Check(out[0] == 10.0f && state.recentCount == 0, "so does a place that leaps metres in one frame");
    place[0] = 10.5f;
    FollowLag(camera, place, 1.0f, state, out);
    Check(out[0] == 10.5f, "and a gap of a second");
}

// LAG AT SPEED: no speed is too fast to follow, and trailing is never a reason to start over. The follower used to
// hold the place's velocity to 20 world units a second and start over 3 world units behind, so anything faster
// fell behind, snapped onto its place, and fell behind again: a fast flyer's camera jumped over and over.
static void TestLagAtSpeed()
{
    ActiveCamera camera = Behind();
    camera.lagSeconds = 0.5f;
    camera.lagDamping = 1.5f;
    camera.changes = 7;
    LagState state;
    float out[3]{};
    const float dt = 1.0f / 90.0f;
    float place[3] = {0.0f, 0.0f, 0.0f};
    float x = 0.0f, speed = 0.0f;

    // From rest to 80 world units a second (about 240 m/s) in four seconds, then four seconds at that speed.
    bool restarted = false;
    float worst = 0.0f;
    for (int i = 0; i < 720; ++i)
    {
        if (i < 360)
            speed += 20.0f * dt;
        x += speed * dt;
        place[0] = x;
        FollowLag(camera, place, dt, state, out);
        if (i > 3 && state.recentCount < 3)
            restarted = true;
        const float gap = std::fabs(out[0] - place[0]);
        worst = gap > worst ? gap : worst;
    }
    Check(!restarted && state.jumps == 0 && state.runaways == 0,
          "a flyer speeding up to 240 m/s never sends its camera back to its place");
    // Speeding up is a change of motion, so it lags: about 0.28 s of the acceleration, here 5.6 world units at six g
    // -- far inside the runaway bound, so the camera falls back smoothly and comes up again, never snapping.
    Check(worst < 7.0f, "and its camera falls back from a speeding flyer smoothly, inside the runaway bound");
    Check(std::fabs(out[0] - place[0]) < 0.02f, "and sits on its place at a steady 240 m/s");

    // Turns at speed. A lagged camera trails in a turn by design (the swing comes through slowly); what it must
    // never do is start over. A hard turn at 120 m/s (40 degrees a second), and a 300 m/s flyer turning at 60.
    const float turns[2][2] = {{40.0f, 40.0f}, {100.0f, 60.0f}};
    const float bounds[2] = {10.0f, 30.0f};
    for (int k = 0; k < 2; ++k)
    {
        camera.changes = 8 + 10 * k;
        const float rate = turns[k][1] * 3.14159265f / 180.0f, radius = turns[k][0] / rate;
        restarted = false;
        worst = 0.0f;
        for (int i = 0; i < 360; ++i)
        {
            const float a = rate * dt * static_cast<float>(i);
            place[0] = radius * std::cos(a);
            place[1] = radius * std::sin(a);
            FollowLag(camera, place, dt, state, out);
            if (i > 3 && state.recentCount < 3)
                restarted = true;
            const float gap = std::sqrt((out[0] - place[0]) * (out[0] - place[0]) +
                                        (out[1] - place[1]) * (out[1] - place[1]));
            worst = gap > worst ? gap : worst;
        }
        Check(!restarted && worst < bounds[k], k == 0 ? "a hard turn at 120 m/s is followed, never restarted"
                                                       : "and so is a 300 m/s flyer's turn");
    }

    // A camera chosen in flight moves with its place from the start, instead of dropping back and catching up.
    camera.changes = 9;
    place[1] = 0.0f;
    x = 0.0f;
    worst = 0.0f;
    for (int i = 0; i < 90; ++i)
    {
        x += 40.0f * dt;
        place[0] = x;
        FollowLag(camera, place, dt, state, out);
        const float gap = std::fabs(out[0] - place[0]);
        worst = gap > worst ? gap : worst;
    }
    Check(worst < 0.05f, "a camera chosen at 120 m/s keeps up from its first frame");

    // In flight, a one-frame step of a world unit (a bump) is followed on the spring, not taken for a speed...
    x += 40.0f * dt + 1.0f;
    place[0] = x;
    FollowLag(camera, place, dt, state, out);
    Check(state.recentCount == 3 && out[0] < place[0], "a bump in flight is followed, not jumped to");
    float most = 0.0f;
    for (int i = 0; i < 180; ++i)
    {
        x += 40.0f * dt;
        place[0] = x;
        FollowLag(camera, place, dt, state, out);
        most = out[0] - place[0] > most ? out[0] - place[0] : most;
    }
    Check(most < 0.1f && std::fabs(out[0] - place[0]) < 0.02f,
          "and the camera does not fly past its place afterwards: the step's speed is never the median");

    // ...and a leap of seven world units in one frame (a respawn, a seat change) starts over on the place.
    x += 40.0f * dt + 7.0f;
    place[0] = x;
    FollowLag(camera, place, dt, state, out);
    Check(out[0] == place[0] && state.recentCount == 0 && state.jumps == 1 && state.runaways == 0,
          "a leap in flight starts over on the place, counted as a jump");
}

// THE LAG'S LIMIT: a camera never stands further than its limit from its place, and at the limit it rides it -- a
// fixed distance behind -- instead of swinging out and in.
static void TestLagLimit()
{
    ActiveCamera camera = Behind();
    camera.lagSeconds = 0.5f;
    camera.lagDamping = 1.5f;
    camera.lagLimitCm = 300.0f;
    const float limit = 300.0f * kWorldUnitsPerCm;
    camera.changes = 31;
    LagState state;
    float out[3]{};
    const float dt = 1.0f / 90.0f;
    float place[3] = {0.0f, 0.0f, 0.0f};
    float x = 0.0f, speed = 0.0f, worst = 0.0f;
    // Speeding up to 240 m/s: without the limit it falls back about 5.6 world units (TestLagAtSpeed).
    for (int i = 0; i < 720; ++i)
    {
        if (i < 360)
            speed += 20.0f * dt;
        x += speed * dt;
        place[0] = x;
        FollowLag(camera, place, dt, state, out);
        const float gap = std::fabs(out[0] - place[0]);
        worst = gap > worst ? gap : worst;
    }
    Check(worst <= limit * 1.001f && state.limited > 0 && state.jumps == 0 && state.runaways == 0,
          "a speeding flyer's camera falls back no further than its limit, and is never started over");
    Check(std::fabs(out[0] - place[0]) < 0.02f, "and sits on its place again at a steady speed");

    // A 300 m/s flyer turning at 60 degrees a second: held on the limit, steadily, once it gets there.
    camera.changes = 32;
    const float rate = 60.0f * 3.14159265f / 180.0f, radius = 100.0f / rate;
    worst = 0.0f;
    float least = 1.0e9f;
    for (int i = 0; i < 360; ++i)
    {
        const float a = rate * dt * static_cast<float>(i);
        place[0] = radius * std::cos(a);
        place[1] = radius * std::sin(a);
        place[2] = 0.0f;
        FollowLag(camera, place, dt, state, out);
        const float gap = std::sqrt((out[0] - place[0]) * (out[0] - place[0]) +
                                    (out[1] - place[1]) * (out[1] - place[1]));
        worst = gap > worst ? gap : worst;
        if (i > 180)
            least = gap < least ? gap : least;
    }
    Check(worst <= limit * 1.001f, "a fast turn trails no further than the limit");
    Check(least > limit * 0.98f, "and through the turn it rides the limit instead of springing in and out");

    // Stopping hard from 240 m/s: the camera runs past its place no further than the limit either.
    camera.changes = 33;
    x = 0.0f;
    speed = 80.0f;
    worst = 0.0f;
    for (int i = 0; i < 900; ++i)
    {
        if (i >= 90)
            speed = speed > 160.0f * dt ? speed - 160.0f * dt : 0.0f;
        x += speed * dt;
        place[0] = x;
        place[1] = 0.0f;
        FollowLag(camera, place, dt, state, out);
        const float past = out[0] - place[0];
        worst = past > worst ? past : worst;
    }
    Check(worst <= limit * 1.001f && std::fabs(out[0] - place[0]) < 0.02f,
          "a hard stop runs past the place no further than the limit, then settles on it");

    // No limit: as before.
    camera.lagLimitCm = 0.0f;
    camera.changes = 34;
    x = 0.0f;
    speed = 0.0f;
    worst = 0.0f;
    const uint32_t before = state.limited;
    for (int i = 0; i < 360; ++i)
    {
        speed += 20.0f * dt;
        x += speed * dt;
        place[0] = x;
        FollowLag(camera, place, dt, state, out);
        const float gap = std::fabs(out[0] - place[0]);
        worst = gap > worst ? gap : worst;
    }
    Check(worst > limit * 2.0f && state.limited == before, "a camera with no limit trails as far as its lag takes it");
}

// LAG ON THE BOOM: the vehicle's travel never lags, at any speed or change of speed; only the boom's swing does, and
// the camera keeps the boom's length from its pivot.
static void TestLagOnBoom()
{
    ActiveCamera camera = Behind();
    camera.lagSeconds = 0.5f;
    camera.lagDamping = 1.5f;
    camera.changes = 41;
    LagState state;
    float out[3]{};
    const float dt = 1.0f / 90.0f;
    const float boom[3]{-1.5f, 0.0f, 0.5f};
    const float boomLength = std::sqrt(boom[0] * boom[0] + boom[2] * boom[2]);
    float x = 0.0f, speed = 0.0f, worst = 0.0f;
    // Speeding up to 240 m/s, a steady run, then a hard stop: the camera stays exactly on its place throughout.
    for (int i = 0; i < 900; ++i)
    {
        if (i < 360)
            speed += 20.0f * dt;
        else if (i >= 450)
            speed = speed > 160.0f * dt ? speed - 160.0f * dt : 0.0f;
        x += speed * dt;
        const float pivot[3]{x, 0.0f, 0.0f};
        const float place[3]{x + boom[0], boom[1], boom[2]};
        FollowLagOnBoom(camera, pivot, place, dt, state, out);
        const float gap = std::sqrt((out[0] - place[0]) * (out[0] - place[0]) + (out[2] - place[2]) * (out[2] - place[2]));
        worst = gap > worst ? gap : worst;
    }
    Check(worst < 1.0e-3f && state.jumps == 0 && state.runaways == 0,
          "on the boom, speeding up to 240 m/s and stopping hard leave the camera on its place");

    // A turn: the boom swings round a vehicle turning at 90 degrees a second while it flies at 120 m/s. The camera
    // trails the swing, never further from its pivot than the boom, and catches up once the turn ends.
    camera.changes = 42;
    const float rate = 90.0f * 3.14159265f / 180.0f;
    float heading = 0.0f, px = 0.0f, py = 0.0f, lagged = 0.0f, worstLength = 0.0f;
    for (int i = 0; i < 450; ++i)
    {
        if (i < 180)
            heading += rate * dt;
        px += 40.0f * std::cos(heading) * dt;
        py += 40.0f * std::sin(heading) * dt;
        const float pivot[3]{px, py, 0.0f};
        const float place[3]{px + boom[0] * std::cos(heading), py + boom[0] * std::sin(heading), boom[2]};
        FollowLagOnBoom(camera, pivot, place, dt, state, out);
        const float fromPivot[3]{out[0] - px, out[1] - py, out[2]};
        const float length = std::sqrt(fromPivot[0] * fromPivot[0] + fromPivot[1] * fromPivot[1] +
                                       fromPivot[2] * fromPivot[2]);
        worstLength = std::fabs(length - boomLength) > worstLength ? std::fabs(length - boomLength) : worstLength;
        if (i == 179)
            lagged = std::sqrt((out[0] - place[0]) * (out[0] - place[0]) + (out[1] - place[1]) * (out[1] - place[1]));
        if (i == 449)
            Check(std::fabs(out[0] - place[0]) < 0.02f && std::fabs(out[1] - place[1]) < 0.02f,
                  "once the turn ends the camera comes round behind the vehicle again");
    }
    Check(lagged > 0.1f && lagged < 2.0f * boomLength, "through the turn it trails the boom's swing");
    Check(worstLength < 1.0e-3f, "and always stands the boom's length from its pivot");
}

// A camera measured from the seat (or the head) lays its offset out in the SEAT'S frame -- the part the seat sits on,
// a walker's torso that turns on its legs -- and a camera measured from the vehicle keeps the vehicle's.
static void TestSeatFrame()
{
    ActiveCamera camera = Behind();
    camera.origin = static_cast<uint8_t>(Origin::Seat);
    camera.offsetCm[0] = 900.0f;
    camera.offsetCm[1] = 0.0f;
    camera.offsetCm[2] = 0.0f;
    float turned[3]{}, riding[3]{}, kept[3]{};
    // The same seat point on a vehicle turned a quarter turn, with no seat frame given: the offset turns with it.
    {
        State state;
        Inputs in = At(0.0f, 0.0f, 0.0f, kQuarterTurn, 0.0f, 0.0f);
        in.seatValid = true;
        in.seat[0] = 2.0f;
        Check(Place(camera, in, state, turned), "a seat camera on a turned vehicle is placed");
    }
    // The vehicle facing ahead and the seat's part turned the quarter turn: the offset rides the seat.
    {
        State state;
        Inputs in = At(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        in.seatValid = true;
        in.seat[0] = 2.0f;
        in.seatFrameValid = true;
        in.seatFrame = FromAngles(kQuarterTurn, 0.0f, 0.0f);
        Check(Place(camera, in, state, riding), "a seat camera on a turned seat is placed");
        Check(Near3(riding, turned[0], turned[1], turned[2]),
              "its offset turns with the seat's part, not with the vehicle under it");
        camera.origin = static_cast<uint8_t>(Origin::Head);
        float head[3]{};
        Check(Place(camera, in, state, head) && Near3(head, riding[0], riding[1], riding[2]),
              "a head camera with no head rides the seat's part too");
        // A head turned a quarter turn the other way on that seat: a head camera follows the head, a seat camera
        // keeps to the seat.
        in.headValid = true;
        in.head[0] = 2.0f;
        in.headFrameValid = true;
        in.headFrame = FromAngles(0.0f, 0.0f, 0.0f);
        float looked[3]{};
        Check(Place(camera, in, state, looked) &&
                  Near3(looked, 2.0f + 900.0f * kWorldUnitsPerCm, 0.0f, 0.0f),
              "a head camera lays its offset out along the head");
        in.headFrameValid = false;
        Check(Place(camera, in, state, looked) && Near3(looked, riding[0], riding[1], riding[2]),
              "and along the seat's part where the head's turn is not known");
        in.headFrameValid = true;
        camera.origin = static_cast<uint8_t>(Origin::Seat);
        Check(Place(camera, in, state, looked) && Near3(looked, riding[0], riding[1], riding[2]),
              "a seat camera ignores the head");
        in.headValid = false;
        in.headFrameValid = false;
        camera.origin = static_cast<uint8_t>(Origin::Vehicle);
        Check(Place(camera, in, state, kept) &&
                  Near3(kept, 900.0f * kWorldUnitsPerCm, 0.0f, 0.0f),
              "a vehicle camera keeps the vehicle's frame");
    }
    // An unusable seat frame is the vehicle's.
    {
        State state;
        camera.origin = static_cast<uint8_t>(Origin::Seat);
        Inputs in = At(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        in.seatValid = true;
        in.seat[0] = 2.0f;
        in.seatFrameValid = true; // all zero: not a frame
        float point[3]{};
        Check(Place(camera, in, state, point) && Near3(point, 2.0f + 900.0f * kWorldUnitsPerCm, 0.0f, 0.0f),
              "a seat frame that is not one leaves the vehicle's");
    }
}

int main()
{
    TestRigid();
    TestYawOnlyStaysLevel();
    TestWorldHeadingIsKept();
    TestOrbit();
    TestOrigins();
    TestSeatFrame();
    TestLagLimit();
    TestLagOnBoom();
    TestThirdPersonRest();
    TestCollide();
    TestLeash();
    TestRefusals();
    TestLag();
    TestLagAtSpeed();
    std::printf("PASS: %u vehicle camera placement checks\n", g_checks);
    return 0;
}
