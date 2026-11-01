#pragma once

// SEAT CONVERGENCE, Halo CE and Halo 2 -- the pure half.
//
// HOW THESE TWO TITLES AIM A SEAT'S WEAPON (read from the retail modules; docs/H2-CE-SEAT-CONVERGENCE-2026-10-06.md).
// The fire routine (CE trigger 0xB7A374, Halo 2 0x8E4940) takes the shot's origin and direction from the weapon's own
// barrel marker when the weapon's owner is a vehicle with a gunner (the unit-aim flag is cleared for it), else from
// the firing unit's aiming vector. Then, for a player, it runs the title's player_aim_projectile (CE 0xB67B00, Halo 2
// 0x7596A0), which
//   1. asks the director for a DETERMINISTIC camera of the player's own biped (CE 0xB14F14, Halo 2 0x6D4730): the
//      seat's camera position along the biped's AIMING VECTOR -- first person when the camera selector says so (the
//      mod's first-person seat view makes it say so), else the following camera, also built from that aiming vector;
//   2. starts a collision test on that line, as far out as the aiming unit's origin is from the camera, with the
//      title's projectile flags, ignoring the aiming unit's ultimate parent (and so, in these two engines, everything
//      attached to it);
//   3. turns the shot from its own origin at the point that test hit, and -- ONLY IN A SEAT -- pins the result inside
//      the weapon's deviation cone about the direction it came in with (the barrel, or the unit's aim).
// So a seat converges when the biped's aim is bent to run from that camera position through the point the hand's
// ray hits: the engine's own crosshair test then finds that point and turns the barrel's shot onto it, held to the
// barrel's cone -- which is the seat's natural limit, kept by the engine itself. The biped's aim is what the title
// replicates (CE: the unit-control packet / the network action; Halo 2: player control's desired angles), so the
// bend is upstream of replication; the director call at the fire is re-aimed as the local backstop, counted.
//
// This header holds what needs no engine: the seat's role from its flags word or the vehicle's occupant handles, the
// verdict on the engine's camera answer, why a seated poll did not converge (our reasons only -- never a natural
// limit), and the line-versus-point measure the dev witness prints.
//
// Zero engine dependencies; unit-tested in tests/seat_converge_tests.cpp.

#include <cmath>
#include <cstdint>

namespace seat_converge
{

// THE SEAT FLAGS WORD (the dword at +0 of a unit definition's seat; CE stride 0x11C, Halo 2 stride 0xB0). The low
// bits are the same in both engines, each read off the code that tests it:
//   bit 0 invisible, bit 3 gunner -- CE 0xB05790 `test byte [seat], 9` and Halo 2 0x8F8C00..0x8F8C0C: either makes the
//           seat's AIMING UNIT its parent (unit_get_aiming_unit_index);
//   bit 2 driver -- Halo 2's sibling predicates 0x8FC99C (`shr eax, 2`) and 0x8FCA5C (`shr eax, 3`), the Halo 1 seat
//           enum (_unit_seat_driver_bit = 2, _unit_seat_gunner_bit = 3);
//   bit 4 third-person camera -- the seat-frame rebase (seat_frame_sites.h, kSeatFlagThirdPersonBit);
//   bit 7 first-person camera on the gun -- CE 0xC52359 `test byte [seat], 0x80`, Halo 2 0x77DB86 `shr eax, 7`:
//           the deterministic first-person camera is the parent's primary-trigger marker (position AND forward).
inline constexpr uint32_t kSeatInvisible = 1u << 0;
inline constexpr uint32_t kSeatDriver = 1u << 2;
inline constexpr uint32_t kSeatGunner = 1u << 3;
inline constexpr uint32_t kSeatThirdPersonCamera = 1u << 4;
inline constexpr uint32_t kSeatCameraOnGun = 1u << 7;

struct Role
{
    bool known = false;    // the seat's role was read (flags or occupant handles)
    bool driver = false;   // this seat drives the vehicle
    bool gunner = false;   // this seat aims the vehicle's own weapon (its aiming unit is the vehicle)
    bool passenger() const { return known && !driver && !gunner; }
};

inline Role RoleFromSeatFlags(bool flagsKnown, uint32_t flags)
{
    Role r;
    r.known = flagsKnown;
    r.driver = flagsKnown && (flags & kSeatDriver) != 0;
    r.gunner = flagsKnown && (flags & (kSeatGunner | kSeatInvisible)) != 0;
    return r;
}

// CE keeps the answer live in the vehicle itself: the occupant whose facing and throttle it takes (+0x304) and the
// occupant whose aim it takes (+0x308; the trigger reads the same handle for the firing player). `none` is the
// engine's empty handle.
inline Role RoleFromOccupants(bool read, uint32_t unit, uint32_t drivingOccupant, uint32_t aimingOccupant,
                              uint32_t none = 0xFFFFFFFFu)
{
    Role r;
    r.known = read && unit != none;
    r.driver = r.known && drivingOccupant == unit;
    r.gunner = r.known && aimingOccupant == unit;
    return r;
}

inline const char* RoleName(const Role& r)
{
    if (!r.known)
        return "unidentified";
    if (r.driver && r.gunner)
        return "driver+gunner";
    if (r.driver)
        return "driver";
    if (r.gunner)
        return "gunner";
    return "passenger";
}

// WHY A SEATED POLL DID NOT CONVERGE. Every reason is OUR code's or a state convergence does not apply to; a natural
// limit (a seat's cone, a gun's pitch stop, a barrel still turning) is never one: the engine holds the shot to it.
enum class Why : uint8_t
{
    Converged = 0,
    AimOff,            // converged_aim = 0
    NoHand,            // the aiming hand's ray could not be built (not tracked, presentation blocked)
    NotIdentified,     // the seat's role did not read
    PolicyOff,         // the seat is not the hand's by the player's keys (the stick keeps it)
    NoEvaluator,       // the engine's camera evaluator is not bound (its hook did not install)
    EvaluatorFault,    // ...or faulted when asked
    EyeNonFinite,      // ...or answered a non-finite position
    ChaseCamera,       // the engine aims this seat from its following camera (the first-person seat view is off or
                       // in an entry / exit transition): not solved here, the seat keeps its unconverged aim
    StanceSettling,    // the newest trace is another stance's (just sat down / stood up / changed seat)
    NoTrace,           // no fresh trace result (the simulation stalled, the trace is off or faulted)
    NoRange,           // the traces have no range yet, or the geometry cannot resolve
    NotWritten,        // the converged aim was built but the write did not go through (Halo 2: a check still pending)
    Count
};

inline const char* WhyName(Why w)
{
    switch (w)
    {
    case Why::Converged: return "converged";
    case Why::AimOff: return "converged_aim off";
    case Why::NoHand: return "no hand ray";
    case Why::NotIdentified: return "seat not identified";
    case Why::PolicyOff: return "the stick keeps this seat";
    case Why::NoEvaluator: return "camera evaluator not bound";
    case Why::EvaluatorFault: return "camera evaluator faulted";
    case Why::EyeNonFinite: return "non-finite seat eye";
    case Why::ChaseCamera: return "engine aims from the chase camera";
    case Why::StanceSettling: return "trace from another stance";
    case Why::NoTrace: return "no fresh trace";
    case Why::NoRange: return "no range / geometry";
    case Why::NotWritten: return "not written yet";
    default: return "?";
    }
}

// THE ENGINE'S OWN CAMERA FOR THE SEAT, as its deterministic evaluator answered for the player's biped. The eye the
// aim is bent from is the answer's position when the evaluator took its FIRST-PERSON path (perspective 0): that
// position does not depend on the aim (the seat's camera marker, or the gun's own trigger marker), so one solve
// settles. On the following path the camera orbits the aim it is built from; that is refused here (ChaseCamera).
struct EyeAnswer
{
    bool bound = false;     // the evaluator could be asked at all
    bool called = false;    // ...and returned (false: it faulted)
    int perspective = -1;   // its return: 0 first person, else following
    float position[3]{};
};

inline Why SeatEye(const EyeAnswer& a, float eye[3])
{
    if (!a.bound)
        return Why::NoEvaluator;
    if (!a.called)
        return Why::EvaluatorFault;
    if (!std::isfinite(a.position[0]) || !std::isfinite(a.position[1]) || !std::isfinite(a.position[2]))
        return Why::EyeNonFinite;
    if (a.perspective != 0)
        return Why::ChaseCamera;
    eye[0] = a.position[0];
    eye[1] = a.position[1];
    eye[2] = a.position[2];
    return Why::Converged;
}

// THE GAME'S AIM IN A HALO 2 SEAT, for the seat drive's per-axis proof (seat_drive::StintCheck) and its arm gate.
// The record the drive writes is player control's desired angles; the proof compares it with "the aim the game shows"
// before an axis is written. The OBSERVER camera's forward (the rendered camera), which on foot is the unit's aim,
// is the wrong reference in a seat. MEASURED (Coagulation, retail Steam
// halo2.dll, read out of process beside the mod's own log): in a seat the observer camera's forward sits a constant
// 5.1-5.2 deg of pitch ABOVE the unit's aiming vector -- Ghost seat 0 (flags 0x801C) and Warthog passenger seat 1
// (flags 0x1070), at four headings and at record pitches from -31 to +13 deg -- while the unit's aiming vector
// (unit+0x174) equals the record to 0.02 deg on both axes. So against the camera the pitch can never prove (it
// fails and retries every 5 s), the pitch stays on the stick servo, and no Halo 2 seat writes both axes: none
// converges. The unit's aiming vector is the line the engine itself fires along (0x77DAA0 copies it into the
// deterministic first-person camera the crosshair test runs on; the fire routine takes it as the shot's line where no
// barrel is), so it is the reference: the proof then says what it means -- the record's angles ARE the unit's world
// aim, with no frame or offset between them.
//
// `unitAim` is used when it read and is a unit vector (the game keeps it normalised: within 2 %); otherwise the camera
// (fail closed to the camera reference, never to no reference). `cameraOffDeg` is the angle between the
// two when both are there (the dev line prints its maximum), NaN otherwise.
struct AimReference
{
    bool valid = false;      // a reference to judge the record against
    bool fromUnit = false;   // ...and it is the unit's own aiming vector
    float yaw = 0.0f, pitch = 0.0f;
    float cameraOffDeg = NAN;
};

inline AimReference SeatAimReference(bool unitRead, const float unitAim[3], bool cameraValid, float cameraYaw,
                                     float cameraPitch)
{
    AimReference r;
    float length2 = 0.0f;
    bool finite = unitRead && unitAim != nullptr;
    for (int i = 0; finite && i < 3; ++i)
    {
        finite = std::isfinite(unitAim[i]);
        length2 += unitAim[i] * unitAim[i];
    }
    const float length = finite ? std::sqrt(length2) : 0.0f;
    const bool cameraOk = cameraValid && std::isfinite(cameraYaw) && std::isfinite(cameraPitch);
    if (finite && std::fabs(length - 1.0f) <= 0.02f)
    {
        r.valid = r.fromUnit = true;
        r.yaw = std::atan2(unitAim[1], unitAim[0]);
        const float s = unitAim[2] / length;
        r.pitch = std::asin(s < -1.0f ? -1.0f : s > 1.0f ? 1.0f : s);
        if (cameraOk)
        {
            const float c[3]{std::cos(cameraYaw) * std::cos(cameraPitch), std::sin(cameraYaw) * std::cos(cameraPitch),
                             std::sin(cameraPitch)};
            float dot = (c[0] * unitAim[0] + c[1] * unitAim[1] + c[2] * unitAim[2]) / length;
            dot = dot < -1.0f ? -1.0f : dot > 1.0f ? 1.0f : dot;
            // atan2 of the cross product's length: acos loses the small angles this exists to show.
            const float x[3]{c[1] * unitAim[2] - c[2] * unitAim[1], c[2] * unitAim[0] - c[0] * unitAim[2],
                             c[0] * unitAim[1] - c[1] * unitAim[0]};
            const float cross = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) / length;
            r.cameraOffDeg = std::atan2(cross, dot) * 57.2957795f;
        }
        return r;
    }
    if (cameraOk)
    {
        r.valid = true;
        r.yaw = cameraYaw;
        r.pitch = cameraPitch;
    }
    return r;
}

// One seat stint's identity for the trace and its consumers: never 0 (0 is "on foot"), and different for another
// vehicle or another seat of the same one.
inline uint32_t StanceKey(uint32_t parentHandle, int seatIndex)
{
    const uint32_t key = parentHandle * 2654435761u ^ (static_cast<uint32_t>(seatIndex + 1) * 40503u);
    return key ? key : 1u;
}

// THE WITNESS'S MEASURE: how far `point` sits off the line (origin, direction), in world units and degrees, and how
// far along the line it is. False for a degenerate line, a non-finite input or a point behind the origin.
struct LineMiss
{
    float offsetWu = 0.0f;
    float deg = 0.0f;
    float alongWu = 0.0f;
};

inline bool MeasureLine(const float origin[3], const float direction[3], const float point[3], LineMiss& out)
{
    float length2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(origin[i]) || !std::isfinite(direction[i]) || !std::isfinite(point[i]))
            return false;
        length2 += direction[i] * direction[i];
    }
    const float length = std::sqrt(length2);
    if (!std::isfinite(length) || !(length > 1.0e-6f))
        return false;
    float along = 0.0f, to[3]{};
    for (int i = 0; i < 3; ++i)
    {
        to[i] = point[i] - origin[i];
        along += to[i] * direction[i] / length;
    }
    if (!std::isfinite(along) || !(along > 0.0f))
        return false;
    // The perpendicular as a vector (never distance^2 - along^2: it cancels at range).
    float off2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float p = to[i] - direction[i] / length * along;
        off2 += p * p;
    }
    out.offsetWu = std::sqrt(off2);
    out.alongWu = along;
    out.deg = std::atan2(out.offsetWu, along) * 57.2957795f;
    return std::isfinite(out.offsetWu) && std::isfinite(out.deg);
}

// A window of those measures (the dev line's mean / max). Plain sums: the caller owns the thread or the lock.
struct MissWindow
{
    uint32_t n = 0;
    double sumWu = 0.0, sumDeg = 0.0, sumAlongWu = 0.0;
    float maxWu = 0.0f, maxDeg = 0.0f;

    void add(const LineMiss& m)
    {
        ++n;
        sumWu += m.offsetWu;
        sumDeg += m.deg;
        sumAlongWu += m.alongWu;
        if (m.offsetWu > maxWu)
            maxWu = m.offsetWu;
        if (m.deg > maxDeg)
            maxDeg = m.deg;
    }
};

// THE SEAT'S REAL ROTATION LIMITS, Halo CE (unit_clip_to_aiming_bounds, Halo 1's units.c): while a unit aims with its
// euler screen, the engine holds its aiming vector inside a yaw / pitch rectangle in the unit's OWN frame
// (forward, up; left = up x forward). `Bounds` is that rectangle (radians: yaw x0..x1, pitch y0..y1). ClampToBounds
// is the engine's clamp of a world direction, for the reticle and the dev witness: true when `dir` was past a stop
// (`out` then is the held direction and `overshootDeg` the angle between them). A yaw range of a full turn or more
// never clamps yaw (the engine's own wrap test).
struct Bounds
{
    float yawMin = 0.0f, yawMax = 0.0f, pitchMin = 0.0f, pitchMax = 0.0f;
    bool usable() const
    {
        return std::isfinite(yawMin) && std::isfinite(yawMax) && std::isfinite(pitchMin) && std::isfinite(pitchMax) &&
               yawMin <= yawMax && pitchMin <= pitchMax && pitchMin >= -1.6f && pitchMax <= 1.6f &&
               yawMin >= -6.4f && yawMax <= 6.4f;
    }
};

inline bool ClampToBounds(const Bounds& b, const float forward[3], const float up[3], const float dir[3],
                          float out[3], float& overshootDeg)
{
    overshootDeg = 0.0f;
    for (int i = 0; i < 3; ++i)
        out[i] = dir[i];
    if (!b.usable())
        return false;
    float f2 = 0.0f, u2 = 0.0f, fu = 0.0f, d2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(forward[i]) || !std::isfinite(up[i]) || !std::isfinite(dir[i]))
            return false;
        f2 += forward[i] * forward[i];
        u2 += up[i] * up[i];
        fu += forward[i] * up[i];
        d2 += dir[i] * dir[i];
    }
    if (std::fabs(f2 - 1.0f) > 0.05f || std::fabs(u2 - 1.0f) > 0.05f || std::fabs(fu) > 0.05f ||
        std::fabs(d2 - 1.0f) > 0.05f)
        return false;
    const float left[3]{up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
                        up[0] * forward[1] - up[1] * forward[0]};
    float lx = 0.0f, ly = 0.0f, lz = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        lx += dir[i] * forward[i];
        ly += dir[i] * left[i];
        lz += dir[i] * up[i];
    }
    float yaw = std::atan2(ly, lx);
    float pitch = std::asin(lz < -1.0f ? -1.0f : lz > 1.0f ? 1.0f : lz);
    const bool yawWraps = (b.yawMax - b.yawMin) - 6.28318531f > -1.0e-4f;
    bool clamped = false;
    if (!yawWraps)
    {
        if (yaw < b.yawMin)
        {
            yaw = b.yawMin;
            clamped = true;
        }
        else if (yaw > b.yawMax)
        {
            yaw = b.yawMax;
            clamped = true;
        }
    }
    if (pitch < b.pitchMin)
    {
        pitch = b.pitchMin;
        clamped = true;
    }
    else if (pitch > b.pitchMax)
    {
        pitch = b.pitchMax;
        clamped = true;
    }
    if (!clamped)
        return false;
    const float cp = std::cos(pitch);
    const float local[3]{std::cos(yaw) * cp, std::sin(yaw) * cp, std::sin(pitch)};
    float dot = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        out[i] = forward[i] * local[0] + left[i] * local[1] + up[i] * local[2];
        dot += out[i] * dir[i];
    }
    overshootDeg = std::acos(dot < -1.0f ? -1.0f : dot > 1.0f ? 1.0f : dot) * 57.2957795f;
    return std::isfinite(overshootDeg);
}

} // namespace seat_converge
