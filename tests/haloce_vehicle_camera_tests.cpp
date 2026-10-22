// Halo CE's seats as the vehicle camera file names them, and the game's own third-person camera worked out from its
// following camera's command (src/common/haloce_vehicle_camera_logic.h).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/common/haloce_vehicle_camera_logic.h"
#include "../src/common/haloce_vehicle_leash_logic.h"

using namespace haloce_vehicle_camera;
using vehicle_camera_file::kSeatDriver;
using vehicle_camera_file::kSeatGunner;
using vehicle_camera_file::kSeatPassenger;

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

static std::string Name(const char* path)
{
    char out[kNameCapacity];
    return NameFromTagPath(path, out) ? std::string(out) : std::string("<none>");
}

static void TestNames()
{
    Check(Name("vehicles\\warthog\\mp_warthog") == "warthog" && Name("vehicles\\warthog\\warthog") == "warthog",
          "the campaign and multiplayer Warthogs are one vehicle");
    Check(Name("vehicles\\rwarthog\\rwarthog") == "rocket_warthog", "the rocket Warthog has a name of its own");
    Check(Name("vehicles\\ghost\\ghost_mp") == "ghost" && Name("vehicles\\banshee\\banshee_mp") == "banshee" &&
              Name("vehicles\\scorpion\\scorpion_mp") == "scorpion" && Name("vehicles\\wraith\\wraith") == "wraith" &&
              Name("vehicles\\pelican\\pelican") == "pelican",
          "each stock folder names its vehicle");
    Check(Name("vehicles\\c gun turret\\c gun turret_mp") == "shade", "the Covenant gun turret is the Shade");
    Check(Name("Vehicles/Ghost/Ghost_MP") == "ghost", "either slash and any case");
    Check(Name("vehicles\\mymod\\Fancy Hog") == "fancy_hog", "a map's own vehicle by its short tag name");
    Check(Name("levels\\test\\custom ride") == "custom_ride", "outside the vehicles folder too");
    Check(Name("") == "<none>" && Name("\\\\") == "<none>" && Name("vehicles\\***\\???") == "<none>",
          "a path with nothing to name names nothing");
    Check(Name("vehicles\\warthog") == "warthog", "a two-part path goes by its last part");

    char a[kNameCapacity], b[kNameCapacity];
    CustomName(0x0123456789ABCDEFull, a);
    CustomName(0x0123456789ABCDEEull, b);
    Check(std::strcmp(a, "custom_0123456789abcdef") == 0 && std::strcmp(a, b) != 0,
          "an unnamed vehicle goes by its model fingerprint");
}

static void TestRoles()
{
    Check(SeatBits(true, kSeatDriverBit | kSeatGunnerBit, false, false) == (kSeatDriver | kSeatGunner),
          "a Ghost's seat drives and fires");
    Check(SeatBits(true, kSeatDriverBit | kSeatThirdPersonBit, false, false) == kSeatDriver, "a Warthog driver");
    Check(SeatBits(true, kSeatGunnerBit, false, false) == kSeatGunner, "a Warthog gunner");
    Check(SeatBits(true, kSeatThirdPersonBit, true, true) == kSeatPassenger,
          "a seat whose flags name neither is a passenger's, whatever the occupant slots say");
    Check(SeatBits(false, 0, true, false) == kSeatDriver && SeatBits(false, 0, false, true) == kSeatGunner &&
              SeatBits(false, 0, false, false) == kSeatPassenger,
          "with no flags, the occupant slots");
}

static bool Near(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

static void TestGameCamera()
{
    // The following camera's command for a track point (forward, left, up) = (-2, 0.5, 1) at look pitch p along
    // heading h: offset = (d cos p + -2, -0.5, d sin p + 1), distance d, forward = (cos p h, sin p).
    const float pitch = 0.2f, depth = 2.29f;
    const float h[2] = {0.6f, 0.8f};
    const float forward[3] = {std::cos(pitch) * h[0], std::cos(pitch) * h[1], std::sin(pitch)};
    const float offset[3] = {depth * std::cos(pitch) - 2.0f, -0.5f, depth * std::sin(pitch) + 1.0f};
    float at[3];
    Check(GameCameraFromPivot(forward, offset, depth, at), "a command with a heading gives a place");
    // The track point laid along the heading: forward h, left (-h.y, h.x), up.
    const float want[3] = {-2.0f * h[0] + 0.5f * -h[1], -2.0f * h[1] + 0.5f * h[0], 1.0f};
    Check(Near(at[0], want[0]) && Near(at[1], want[1]) && Near(at[2], want[2]),
          "the camera stands at the track's point from the pivot, the look's distance cancelled");
    const float up[3] = {0.0f, 0.0f, 1.0f};
    Check(!GameCameraFromPivot(up, offset, depth, at), "a look straight up has no heading");
    const float nan[3] = {std::nanf(""), 0.0f, 0.0f};
    Check(!GameCameraFromPivot(forward, nan, depth, at), "a value that is not finite gives nothing");

    Check(LevelElevationDeg(1.51f, 4.74f, 0.0f) == 19.0f, "looking level, the elevation it stands at");
    Check(LevelElevationDeg(2.30f, 6.96f, -7.6f) == 12.0f, "looking down, plus the look");
    Check(LevelElevationDeg(5.0f, 4.0f, 0.0f) == vehicle_boom::kBoomPitchUnset &&
              LevelElevationDeg(1.0f, 0.0f, 0.0f) == vehicle_boom::kBoomPitchUnset,
          "a rise past the distance, or no distance, makes no angle");
    const vehicle_boom::Range range = BoomRange();
    Check(Near(range.min, -60.0f * vehicle_boom::kRadiansPerDegree) &&
              Near(range.max, 10.0f * vehicle_boom::kRadiansPerDegree),
          "the orbit's range: 10 deg below to 60 deg above level");
}

static std::string EntryFor(const char* vehicle, uint8_t seat)
{
    static const vehicle_camera_file::Table builtIn = vehicle_camera_file::DefaultTable();
    const vehicle_camera_file::Game* ce = builtIn.FindGame("halo1");
    const int index = ce ? vehicle_camera_file::MatchVehicle(*ce, vehicle, seat) : -1;
    return index >= 0 ? ce->vehicles[static_cast<std::size_t>(index)].name : std::string();
}

static void TestBuiltInEntries()
{
    static const vehicle_camera_file::Table builtIn = vehicle_camera_file::DefaultTable();
    const vehicle_camera_file::Game* ce = builtIn.FindGame("halo1");
    Check(ce && ce->key == "halo1", "the built-in table has a halo1 section");
    Check(EntryFor("warthog", kSeatDriver) == "Warthog" && EntryFor("warthog", kSeatPassenger) == "Warthog passenger" &&
              EntryFor("warthog", kSeatGunner) == "Warthog gunner",
          "each Warthog seat its own entry");
    Check(EntryFor("rocket_warthog", kSeatGunner) == "Warthog gunner", "the rocket Warthog rides the Warthog's");
    Check(EntryFor("ghost", kSeatDriver | kSeatGunner) == "Ghost" &&
              EntryFor("banshee", kSeatDriver | kSeatGunner) == "Banshee",
          "the Ghost's and the Banshee's one seat");
    Check(EntryFor("scorpion", kSeatDriver | kSeatGunner) == "Scorpion" &&
              EntryFor("scorpion", kSeatPassenger) == "Scorpion passenger",
          "the Scorpion's driver and its riders");
    Check(EntryFor("shade", kSeatDriver | kSeatGunner) == "Shade", "the Shade");
    Check(EntryFor("wraith", kSeatDriver | kSeatGunner) == "Wraith", "the Wraith");
    Check(EntryFor("fancy_hog", kSeatDriver) == "default", "a vehicle the section does not name rides its default");
    // The Warthog's over-the-shoulder cameras stand over the outer shoulder, higher than Halo 3's (the seat's point is
    // the occupant's hips), and Halo 3's own stay as they were.
    const auto offset = [](const char* game, const char* entry, const char* camera, float x, float y, float z) {
        const vehicle_camera_file::Game* section = builtIn.FindGame(game);
        if (!section)
            return false;
        for (const vehicle_camera_file::Vehicle& vehicle : section->vehicles)
            if (vehicle.name == entry)
                for (const vehicle_camera_file::Camera& c : vehicle.cameras)
                    if (c.name == camera)
                        return c.offset[0] == x && c.offset[1] == y && c.offset[2] == z;
        return false;
    };
    Check(offset("halo1", "Warthog", "Over the Shoulder", -170.0f, -30.0f, 140.0f) &&
              offset("halo1", "Warthog passenger", "Over the Shoulder", -170.0f, 30.0f, 140.0f) &&
              offset("halo1", "Warthog gunner", "Behind the Gun", -150.0f, 0.0f, 90.0f),
          "CE's Warthog seats take the placements chosen from the first pictures in game");
    Check(offset("halo3", "Warthog", "Over the Shoulder", -200.0f, 90.0f, 70.0f) &&
              offset("halo3", "Warthog passenger", "Over the Shoulder", -200.0f, -90.0f, 70.0f),
          "and Halo 3's keep theirs");
    // Every entry starts in the seat view, and the second camera is where the game's own rests.
    bool seatFirst = true;
    for (const vehicle_camera_file::Vehicle& vehicle : ce->vehicles)
        seatFirst = seatFirst && vehicle.cameras.size() >= 2 &&
                    vehicle.cameras[0].type == vehicle_camera_file::CameraType::FirstPerson &&
                    vehicle.cameras[1].origin == vehicle_camera_file::Origin::ThirdPerson;
    Check(seatFirst, "every halo1 entry starts in the seat view, then the game's own third person");
}

// The head's displacement from the camera's point in the world, as CE's BuildEye lays it.
static bool HeadInWorld(const halo_ce::Camera& native, const halo_ce::Tracking& tracking,
                        const halo_ce::Reference& reference, float unitsPerMeter, float out[3])
{
    halo_ce::Camera frame{};
    halo_ce::Quat inverse{};
    if (!halo_ce::BuildTrackingFrame(native, reference, frame, inverse))
        return false;
    const halo_ce::Vec3 world =
        halo_ce::ToNative(frame, halo_ce::Rotate(inverse, tracking.headPosition - reference.position)) * unitsPerMeter;
    out[0] = world.x;
    out[1] = world.y;
    out[2] = world.z;
    return true;
}

// Along each of the carrier's axes, the head after the leash: where it was if it was inside the box that way, else on
// the very limit it had crossed.
static bool HeldToBox(const float was[3], const float now[3], const vehicle_camera_frame::Basis& carrier,
                      const vehicle_camera_frame::LeashBox& box)
{
    const float* const axes[3] = {carrier.forward, carrier.left, carrier.up};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float earlier = vehicle_camera_frame::Dot(was, axes[axis]);
        const float along = vehicle_camera_frame::Dot(now, axes[axis]);
        const float want = earlier < box.low[axis] ? box.low[axis] : (earlier > box.high[axis] ? box.high[axis] : earlier);
        if (!Near(along, want))
            return false;
    }
    return true;
}

static void TestLeash()
{
    using namespace halo_ce;
    Camera native{};
    native.position = {10, 20, 3};
    native.forward = {0, 1, 0}; // the camera looks along world +Y
    native.up = {0, 0, 1};
    native.verticalFov = 1.2f;
    native.viewport = {0, 0, 720, 1280};
    native.window = native.viewport;
    native.nearPlane = 0.01f;
    native.farPlane = 1000;
    Reference reference{};
    reference.orientation = {0, 0, 0, 1};
    Tracking tracking{};
    tracking.headOrientation = {0, 0, 0, 1};
    // Head moved half a metre to the room's right (+x) and 30 cm up (+y in tracking space).
    tracking.headPosition = {0.5f, 0.3f, 0.0f};
    const float upm = 0.328f; // world units per metre (1 wu = 3.048 m)
    float before[3];
    Check(HeadInWorld(native, tracking, reference, upm, before), "the head's place in the world reads");
    // Looking along +Y, the room's right is world -X... whichever: the carrier frame below is the world's own.
    const vehicle_camera_frame::Basis carrier = vehicle_camera_frame::FromAngles(0.0f, 0.0f, 0.0f);
    // A box 10 cm each way on every axis around the camera's point.
    const float leashMin[3] = {-10, -10, -10}, leashMax[3] = {10, 10, 10}, camera[3] = {0, 0, 0};
    const vehicle_camera_frame::LeashBox box = vehicle_camera_frame::LeashToBasis(leashMin, leashMax, camera);
    Reference leashed = reference;
    Check(haloce_vehicle_leash::LeashReference(native, tracking, upm, true, carrier, box, leashed),
          "a head outside the box moves the reference");
    float after[3];
    Check(HeadInWorld(native, tracking, leashed, upm, after), "and the head reads again");
    Check(HeldToBox(before, after, carrier, box), "the head then stands on the limits it crossed, and nowhere nearer");
    // An axis inside its limit keeps its value: a box wide along up keeps the head's rise.
    const float tallMin[3] = {-10, -10, -1000}, tallMax[3] = {10, 10, 1000};
    leashed = reference;
    Check(haloce_vehicle_leash::LeashReference(native, tracking, upm, true, carrier,
                                               vehicle_camera_frame::LeashToBasis(tallMin, tallMax, camera), leashed),
          "a box wide along up still stops the sideways move");
    Check(HeadInWorld(native, tracking, leashed, upm, after) &&
              HeldToBox(before, after, carrier, vehicle_camera_frame::LeashToBasis(tallMin, tallMax, camera)),
          "and holds the head on the sideways limit while leaving the rise as it was");
    // Inside the box: nothing moves.
    const float wideMin[3] = {-1000, -1000, -1000}, wideMax[3] = {1000, 1000, 1000};
    leashed = reference;
    Check(!haloce_vehicle_leash::LeashReference(native, tracking, upm, true, carrier,
                                                vehicle_camera_frame::LeashToBasis(wideMin, wideMax, camera), leashed) &&
              leashed.position.x == 0.0f && leashed.position.y == 0.0f,
          "a head inside the box moves nothing");
    Check(!haloce_vehicle_leash::LeashReference(native, tracking, upm, false, carrier, box, leashed),
          "without positional tracking there is nothing to hold");

    // A room turned 63 deg, a seat heading apart from the camera's own and a carrier turned 30 deg and pitched 10: the
    // reference is moved back through the frame's yaw, so the head still lands on the box's limits along the carrier.
    Reference turned = reference;
    turned.orientation = {0, std::sin(0.55f), 0, std::cos(0.55f)};
    turned.seatHeadingValid = true;
    turned.seatHeading = {0.6f, 0.8f, 0.0f};
    Tracking reaching = tracking;
    reaching.headPosition = {0.4f, -0.2f, 0.35f};
    const vehicle_camera_frame::Basis tilted = vehicle_camera_frame::FromAngles(0.5236f, 0.1745f, 0.0f);
    const float unevenMin[3] = {-5, -20, -8}, unevenMax[3] = {15, 5, 12};
    const vehicle_camera_frame::LeashBox uneven = vehicle_camera_frame::LeashToBasis(unevenMin, unevenMax, camera);
    const float* const axes[3] = {tilted.forward, tilted.left, tilted.up};
    float was[3];
    Check(HeadInWorld(native, reaching, turned, upm, was), "the turned room's head reads");
    bool outside = false;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float along = vehicle_camera_frame::Dot(was, axes[axis]);
        outside = outside || along < uneven.low[axis] || along > uneven.high[axis];
    }
    Check(outside, "the head starts outside the uneven box");
    Reference held = turned;
    Check(haloce_vehicle_leash::LeashReference(native, reaching, upm, true, tilted, uneven, held),
          "a turned room's head outside the box moves the reference");
    float now[3];
    Check(HeadInWorld(native, reaching, held, upm, now), "and the head reads again");
    Check(HeldToBox(was, now, tilted, uneven),
          "along the turned carrier the head then stands on each limit it crossed, and keeps each axis it was inside");
}

// A track of five points from 1 wu behind and 2 up (its start, looking straight down) to 3 behind at the pivot's height
// (its end, looking straight up), 0.25 wu to the left, its height falling as the square of the parameter: the cubic
// through any four of them is the curve itself.
static vehicle_boom::Track CurveTrack()
{
    vehicle_boom::Track track;
    track.count = 5;
    for (int i = 0; i < 5; ++i)
    {
        const float t = static_cast<float>(i) / 4.0f;
        track.points[i].position[0] = -1.0f - 2.0f * t;
        track.points[i].position[1] = 0.25f;
        track.points[i].position[2] = 2.0f - 2.0f * t * t;
        track.points[i].orientation[3] = 1.0f;
    }
    return track;
}

static void TestTrack()
{
    Check(Near(TrackParameter(-kHalfPi), 0.0f) && Near(TrackParameter(0.0f), 0.5f) && Near(TrackParameter(kHalfPi), 1.0f),
          "looking straight down, level and straight up take the track's start, middle and end");
    Check(Near(TrackParameter(-3.0f), 0.0f) && Near(TrackParameter(3.0f), 1.0f) && Near(TrackParameter(NAN), 0.5f),
          "a pitch past 90 deg holds at the end, and one that is not a number is level");

    const vehicle_boom::Track track = CurveTrack();
    float place[3];
    Check(TrackPlace(track, 0.0f, place) && Near(place[0], -2.0f) && Near(place[1], 0.25f) && Near(place[2], 1.5f),
          "looking level, the camera stands halfway along the track");
    Check(TrackPlace(track, -kHalfPi, place) && Near(place[0], -1.0f) && Near(place[2], 2.0f),
          "looking straight down, at its start");
    Check(TrackPlace(track, kHalfPi, place) && Near(place[0], -3.0f) && Near(place[2], 0.0f),
          "looking straight up, at its end");
    Check(TrackPlace(track, 0.25f * kPi, place) && Near(place[0], -2.5f) && Near(place[2], 2.0f - 2.0f * 0.5625f),
          "looking 45 deg up, three quarters along, on the curve between the points");

    vehicle_boom::Track bare = track;
    for (int i = 0; i < bare.count; ++i)
        bare.points[i].orientation[3] = 0.0f;
    Check(TrackUsable(bare) && !vehicle_boom::Usable(bare), "CE never reads the points' orientations; Halo 3 does");
    vehicle_boom::Track broken = track;
    broken.count = 3;
    Check(!TrackUsable(broken) && !TrackPlace(broken, 0.0f, place), "a track of three points is not evaluated");
    broken = track;
    broken.points[2].position[1] = NAN;
    Check(!TrackUsable(broken), "nor one with a place that is not a number");
    broken = track;
    broken.points[4].position[0] = -200.0f;
    Check(!TrackUsable(broken), "nor one with a place 200 world units out");

    vehicle_boom::Shape shape;
    Check(TrackShape(track, vehicle_boom::Range{}, shape) && shape.valid && shape.track,
          "the track gives the boom a shape across the flat look range");
    float level[3];
    vehicle_boom::OffsetAt(shape, 0.0f, level);
    Check(Near(level[0], -2.0f, 1.0e-3f) && Near(level[2], 1.5f, 1.0e-3f),
          "the shape looking level is the track looking level");
    float far[3];
    vehicle_boom::Offset(shape, 0.0f, 99.0f, far);
    Check(Near(far[0], level[0]) && Near(far[2], level[2]), "a track's offsets are its own, never scaled by a distance");
    Check(vehicle_boom::ElevationAt(shape, shape.range.min) > vehicle_boom::ElevationAt(shape, 0.0f) &&
              vehicle_boom::ElevationAt(shape, 0.0f) > vehicle_boom::ElevationAt(shape, shape.range.max),
          "looking down stands the camera higher, looking up lower");
    const float elevation = vehicle_boom::ElevationAt(shape, 0.3f);
    Check(Near(vehicle_boom::PitchForElevation(shape, elevation), 0.3f, 2.0e-3f),
          "an elevation on the track gives back the pitch it stands at");
    Check(Near(vehicle_boom::DefaultElevation(shape, vehicle_boom::kBoomPitchUnset),
               vehicle_boom::ElevationAt(shape, 0.0f) + vehicle_boom::kDefaultRaiseDeg * vehicle_boom::kRadiansPerDegree),
          "without a file's boomPitch the boom starts at the track's level view, a little higher");

    vehicle_boom::Shape kept = shape;
    Check(!TrackShape(broken, vehicle_boom::Range{}, kept) && !kept.valid, "a track that cannot be evaluated gives no shape");
    const vehicle_boom::Range backwards{0.5f, -0.5f};
    Check(!TrackShape(track, backwards, kept) && !kept.valid, "nor does a range that is not one");
}

int main()
{
    TestNames();
    TestRoles();
    TestGameCamera();
    TestBuiltInEntries();
    TestLeash();
    TestTrack();
    std::printf("haloce vehicle camera tests: %u checks passed\n", g_checks);
    return 0;
}
