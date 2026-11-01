// Seat convergence for Halo CE and Halo 2 (src/common/seat_converge_logic.h): the seat's role from its flags word
// or the vehicle's occupant handles, the verdict on the engine's camera answer, the stance key, the line-versus-point
// measure and the engine's aiming-bounds clamp.
#include "seat_converge_logic.h"
#include <cmath>
#include <cstdio>

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

using namespace seat_converge;

void TestRoles()
{
    // The flags word: Halo 2's Warthog seats as its tags author them (driver 0x4, gunner 0x8, passenger neither),
    // and a Ghost-style driver that also aims the guns (0xC). Other bits never change the role.
    Role r = RoleFromSeatFlags(true, kSeatDriver | kSeatThirdPersonCamera);
    Check(r.known && r.driver && !r.gunner && !r.passenger(), "driver bit alone: a driver");
    r = RoleFromSeatFlags(true, kSeatGunner | kSeatCameraOnGun);
    Check(r.known && !r.driver && r.gunner, "gunner bit alone: a gunner");
    r = RoleFromSeatFlags(true, kSeatDriver | kSeatGunner);
    Check(r.driver && r.gunner && !r.passenger(), "both bits: driver+gunner");
    r = RoleFromSeatFlags(true, kSeatThirdPersonCamera | (1u << 5));
    Check(r.known && r.passenger(), "neither bit: a passenger");
    r = RoleFromSeatFlags(true, kSeatInvisible);
    Check(r.gunner, "an invisible seat aims its parent, as the engine's aiming-unit test says");
    r = RoleFromSeatFlags(false, kSeatDriver | kSeatGunner);
    Check(!r.known && !r.driver && !r.gunner && !r.passenger(), "flags that did not read name no role");

    // CE's live occupant handles.
    r = RoleFromOccupants(true, 0xE1740003u, 0xE1740003u, 0xFFFFFFFFu);
    Check(r.known && r.driver && !r.gunner, "the vehicle takes this unit's facing: the driver");
    r = RoleFromOccupants(true, 0xE1740003u, 0xE1740003u, 0xE1740003u);
    Check(r.driver && r.gunner, "...and its aim too: driver+gunner (Ghost, Scorpion, Banshee)");
    r = RoleFromOccupants(true, 0xE1740003u, 0xE2A00007u, 0xE1740003u);
    Check(!r.driver && r.gunner, "another unit drives, this one aims: the gunner");
    r = RoleFromOccupants(true, 0xE1740003u, 0xE2A00007u, 0xFFFFFFFFu);
    Check(r.passenger(), "neither handle is this unit: a passenger");
    r = RoleFromOccupants(false, 0xE1740003u, 0xE1740003u, 0xE1740003u);
    Check(!r.known && !r.driver, "handles that did not read name no role");
    r = RoleFromOccupants(true, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
    Check(!r.known && !r.driver && !r.gunner, "the empty handle is never an occupant");
    Check(RoleName(RoleFromSeatFlags(true, kSeatDriver | kSeatGunner))[0] == 'd' &&
          RoleName(Role{})[0] == 'u', "role names");
}

void TestSeatEye()
{
    float eye[3]{9.0f, 9.0f, 9.0f};
    EyeAnswer a{};
    Check(SeatEye(a, eye) == Why::NoEvaluator && eye[0] == 9.0f, "an unbound evaluator: refused, eye untouched");
    a.bound = true;
    Check(SeatEye(a, eye) == Why::EvaluatorFault, "bound but it faulted");
    a.called = true;
    a.perspective = 0;
    a.position[0] = 1.0f; a.position[1] = 2.0f; a.position[2] = 3.0f;
    Check(SeatEye(a, eye) == Why::Converged && eye[0] == 1.0f && eye[1] == 2.0f && eye[2] == 3.0f,
          "the first-person answer is the seat's eye");
    a.perspective = 1;
    eye[0] = 9.0f;
    Check(SeatEye(a, eye) == Why::ChaseCamera && eye[0] == 9.0f,
          "the following camera orbits the aim: refused, never used as a fixed eye");
    a.perspective = 0;
    a.position[1] = std::nanf("");
    Check(SeatEye(a, eye) == Why::EyeNonFinite, "a non-finite answer is refused");
    // No size limit: an eye a long way from anything is still the engine's answer.
    a.position[1] = 5.0e4f;
    Check(SeatEye(a, eye) == Why::Converged, "no size limit on the engine's own answer");
    for (int w = 0; w < static_cast<int>(Why::Count); ++w)
        Check(WhyName(static_cast<Why>(w))[0] != '?', "every reason has a name");
}

void TestStanceKey()
{
    Check(StanceKey(0xE1740003u, 0) != 0 && StanceKey(0, -1) != 0, "a seat's key is never the on-foot 0");
    Check(StanceKey(0xE1740003u, 0) != StanceKey(0xE1740003u, 1), "another seat of the same vehicle: another key");
    Check(StanceKey(0xE1740003u, 0) != StanceKey(0xE1750003u, 0), "the same seat of another vehicle: another key");
    Check(StanceKey(0xE1740003u, 2) == StanceKey(0xE1740003u, 2), "the same seat: the same key");
}

void TestMeasureLine()
{
    const float origin[3]{0.0f, 0.0f, 0.0f}, along[3]{2.0f, 0.0f, 0.0f};
    LineMiss m{};
    const float on[3]{10.0f, 0.0f, 0.0f};
    Check(MeasureLine(origin, along, on, m) && Near(m.offsetWu, 0.0f) && Near(m.alongWu, 10.0f) && Near(m.deg, 0.0f),
          "a point on the line: no miss (the direction need not be unit)");
    const float off[3]{10.0f, 0.0f, 1.0f};
    Check(MeasureLine(origin, along, off, m) && Near(m.offsetWu, 1.0f) && Near(m.deg, 5.7106f, 1.0e-3f),
          "one unit off at ten: 5.71 deg");
    // At range the perpendicular must not cancel: 1 km out, 1 cm off.
    const float farOff[3]{328.084f, 0.00328084f, 0.0f};
    Check(MeasureLine(origin, along, farOff, m) && Near(m.offsetWu, 0.00328084f, 1.0e-5f),
          "a centimetre at a kilometre survives the float arithmetic");
    const float behind[3]{-1.0f, 0.0f, 0.0f};
    Check(!MeasureLine(origin, along, behind, m), "a point behind the origin has no measure");
    const float zero[3]{};
    Check(!MeasureLine(origin, zero, on, m), "a degenerate line has no measure");
    const float bad[3]{std::nanf(""), 0.0f, 0.0f};
    Check(!MeasureLine(origin, along, bad, m), "a non-finite point has no measure");
    MissWindow w{};
    MeasureLine(origin, along, on, m);
    w.add(m);
    MeasureLine(origin, along, off, m);
    w.add(m);
    Check(w.n == 2 && Near(static_cast<float>(w.sumWu), 1.0f) && Near(w.maxWu, 1.0f) && w.maxDeg > 5.7f,
          "the window keeps the sum and the max");
}

void TestBounds()
{
    // A Warthog-chaingun-like rectangle: full yaw, pitch -20..+45 deg; then a passenger-like yaw cone.
    const float fwd[3]{1.0f, 0.0f, 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
    Bounds gun{-3.14159265f, 3.14159265f, -0.349066f, 0.785398f};
    float out[3]{}, over = 0.0f;
    const float level[3]{0.0f, 1.0f, 0.0f};
    Check(!ClampToBounds(gun, fwd, up, level, out, over) && over == 0.0f && out[1] == 1.0f,
          "inside the stops: untouched, and a full-turn yaw range never clamps yaw");
    const float down[3]{std::cos(-0.6f), 0.0f, std::sin(-0.6f)};
    Check(ClampToBounds(gun, fwd, up, down, out, over) && Near(out[2], std::sin(-0.349066f), 1.0e-4f) &&
          Near(over, (0.6f - 0.349066f) * 57.29578f, 0.02f), "past the pitch stop: held at -20 deg, 14.4 deg over");
    Bounds cone{-0.5f, 0.5f, -0.3f, 0.3f};
    const float side[3]{std::cos(1.0f), std::sin(1.0f), 0.0f};
    Check(ClampToBounds(cone, fwd, up, side, out, over) && Near(std::atan2(out[1], out[0]), 0.5f, 1.0e-4f) &&
          Near(over, 0.5f * 57.29578f, 0.05f), "past a yaw cone: held at its edge");
    // A tilted frame: the stops live in the unit's own frame, not the world's.
    const float tFwd[3]{std::cos(0.3f), 0.0f, std::sin(0.3f)}, tUp[3]{-std::sin(0.3f), 0.0f, std::cos(0.3f)};
    const float worldLevel[3]{1.0f, 0.0f, 0.0f};   // 0.3 rad BELOW the tilted unit's forward: at the cone's pitch edge
    Check(!ClampToBounds(Bounds{-0.5f, 0.5f, -0.31f, 0.3f}, tFwd, tUp, worldLevel, out, over),
          "a tilted unit: world-level is inside when it is within the unit's own pitch range");
    Check(ClampToBounds(Bounds{-0.5f, 0.5f, -0.1f, 0.3f}, tFwd, tUp, worldLevel, out, over) &&
          Near(over, 0.2f * 57.29578f, 0.05f), "...and past it by the frame's tilt otherwise");
    // Nothing unusable clamps.
    Check(!ClampToBounds(Bounds{0.5f, -0.5f, 0.0f, 0.0f}, fwd, up, side, out, over), "unordered bounds are no stop");
    Check(!ClampToBounds(Bounds{-0.5f, 0.5f, -3.0f, 3.0f}, fwd, up, side, out, over), "a pitch range past the pole is no stop");
    const float badUp[3]{1.0f, 0.0f, 0.0f};
    Check(!ClampToBounds(cone, fwd, badUp, side, out, over), "a frame that is not orthonormal is no stop");
    const float longDir[3]{2.0f, 0.0f, 0.0f};
    Check(!ClampToBounds(cone, fwd, up, longDir, out, over), "a direction that is not unit is no stop");
}

void TestAimReference()
{
    // A measured Halo 2 seat: the record and the unit's aiming vector at yaw -88.65, pitch -4.97 deg;
    // the observer camera 5.1 deg of pitch above it. The reference is the unit's aim, and the camera's offset is said.
    const float d2r = 0.0174532925f;
    const float yaw = -88.65f * d2r, pitch = -4.97f * d2r;
    const float aim[3]{std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};
    AimReference r = SeatAimReference(true, aim, true, yaw, pitch + 5.1f * d2r);
    Check(r.valid && r.fromUnit && Near(r.yaw, yaw, 1.0e-5f) && Near(r.pitch, pitch, 1.0e-5f),
          "a seat's reference is the unit's own aiming vector");
    Check(Near(r.cameraOffDeg, 5.1f, 0.01f), "...and the observer camera's offset from it is reported");
    // The record the game shows equals that reference, so the per-axis proof agrees where the camera's never did.
    Check(std::fabs(r.pitch - pitch) < 0.03f && std::fabs((pitch + 5.1f * d2r) - pitch) > 0.03f,
          "the camera's 5.1 deg sits outside the proof's 1.7 deg; the unit's aim inside it");
    // No camera: still the unit's aim, with no offset to report.
    r = SeatAimReference(true, aim, false, 0.0f, 0.0f);
    Check(r.valid && r.fromUnit && std::isnan(r.cameraOffDeg), "the unit's aim needs no camera");
    // A vector that is not the aiming vector (not unit length, non-finite, not read): the camera is the reference.
    const float longAim[3]{aim[0] * 1.5f, aim[1] * 1.5f, aim[2] * 1.5f};
    r = SeatAimReference(true, longAim, true, 0.4f, -0.2f);
    Check(r.valid && !r.fromUnit && Near(r.yaw, 0.4f) && Near(r.pitch, -0.2f) && std::isnan(r.cameraOffDeg),
          "a vector that is not normalised is not the aiming vector: the camera is the reference");
    const float nanAim[3]{aim[0], std::nanf(""), aim[2]};
    r = SeatAimReference(true, nanAim, true, 0.4f, -0.2f);
    Check(r.valid && !r.fromUnit, "a non-finite vector: the camera");
    r = SeatAimReference(false, aim, true, 0.4f, -0.2f);
    Check(r.valid && !r.fromUnit && Near(r.yaw, 0.4f), "a vector that did not read: the camera");
    r = SeatAimReference(false, aim, false, 0.4f, -0.2f);
    Check(!r.valid && !r.fromUnit, "neither: no reference (the stint judges nothing)");
    r = SeatAimReference(true, nanAim, true, std::nanf(""), -0.2f);
    Check(!r.valid, "a non-finite camera is no reference either");
    // Within the 2 % the game's own normalisation leaves: still the aiming vector, its angles those of the direction.
    const float nearAim[3]{aim[0] * 1.015f, aim[1] * 1.015f, aim[2] * 1.015f};
    r = SeatAimReference(true, nearAim, true, yaw, pitch);
    Check(r.fromUnit && Near(r.pitch, pitch, 1.0e-4f) && Near(r.cameraOffDeg, 0.0f, 0.01f),
          "a vector within 2 % of unit length is taken, and a camera on it reads no offset");
    // Straight up stays finite.
    const float upAim[3]{0.0f, 0.0f, 1.0f};
    r = SeatAimReference(true, upAim, false, 0.0f, 0.0f);
    Check(r.fromUnit && Near(r.pitch, 1.5707963f, 1.0e-4f) && std::isfinite(r.yaw), "the pole is finite");
}
} // namespace

int main()
{
    TestRoles();
    TestSeatEye();
    TestStanceKey();
    TestMeasureLine();
    TestBounds();
    TestAimReference();
    std::printf("seat converge tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
