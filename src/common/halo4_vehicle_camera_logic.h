#pragma once
// HALO 4'S SEATS AS THE VEHICLE CAMERA FILE NAMES THEM (vehicle_camera_file.h), and where the game's own
// third-person camera turns about in a seat.
//
//   NAME. Halo 4 gives the mod no tag path to read: a vehicle is known by its render model's identity
//   (halo4_vehicle_identity.inl: the model's node count above its runtime import checksum, one number on every map and
//   in every session). The models measured in game go by the stock vehicle's word ("warthog", "mongoose"), the
//   words Halo 3's and Reach's sections use. A gun mounted on a vehicle (the Warthog's turret) is a seated object of
//   its own riding the vehicle: it goes by the CARRIER's name where the carrier is known, as Halo 3 names a gunner by
//   the vehicle carrying the gun. Any other vehicle goes by "custom_" and its identity in hex, so each keeps an entry,
//   a seat memory and a note of its own.
//
//   ROLE. From the seat's own flags word as the retail module uses it (halo4_seat_converge_logic.h): bit 2 the driver
//   (the control dispatcher's own test), bit 3 the gunner; a seat with neither is a passenger's. A seat on a gun
//   riding a carrier fires the gun and never drives the carrier. Flags that could not be read leave a seat on a
//   carrier the gunner's and any other seat a passenger's.
//
//   THE GAME'S OWN THIRD-PERSON CAMERA in a seat (the following camera; halo4_seat_converge_logic.h has the static
//   reading of the retail module): it stands at pivot [+ the seat's track offset] - distance * aim and looks along the
//   aim, so its line runs back through the pivot -- the seat's camera marker, a point fixed on the vehicle. The mod
//   does not read that marker. It finds the point the camera's lines meet in the vehicle's own frame, from the frames
//   it sees (PivotFinder), and measures the camera's distance from there. The lines cross only once the aim has
//   turned in the vehicle's frame; until then the point of the line nearest the vehicle's origin stands in.
//
// Pure: no engine.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "vehicle_boom_logic.h"
#include "vehicle_camera_file.h"

namespace halo4_vehicle_camera
{
// ---- names --------------------------------------------------------------------------------------------------------

// A render model identity the mod has seen in game, and the stock vehicle's word it goes by.
struct KnownVehicle
{
    uint64_t identity;
    const char* name;
};

// ADDR-HYGIENE: structural. Not addresses: render model identities (node count << 32 | runtime import checksum) read
// in game from the stock vehicles; an identity not listed is a vehicle of its own ("custom_..."), never refused.
inline constexpr KnownVehicle kKnownVehicles[] = {
    {0x0000001d1a19101eull, "broadsword"}, // the Broadsword boost camera fix's identity (game.cpp)
};

// The name of a measured identity; null for any other.
inline const char* IdentityName(uint64_t identity) noexcept
{
    if (!identity)
        return nullptr;
    for (const KnownVehicle& known : kKnownVehicles)
        if (known.identity == identity)
            return known.name;
    return nullptr;
}

inline constexpr std::size_t kNameCapacity = 40;

// "custom_" and the identity's 16 hex digits; "custom_unread" when it could not be read.
inline void CustomName(uint64_t identity, char (&out)[kNameCapacity]) noexcept
{
    if (!identity)
        std::snprintf(out, kNameCapacity, "custom_unread");
    else
        std::snprintf(out, kNameCapacity, "custom_%016llx", static_cast<unsigned long long>(identity));
}

// The seat's vehicle as the camera file names it. `onCarrier`: the seated object rides another object (a mounted
// gun); `carrierIdentity` is that carrier's (0 when it rides none or it could not be read).
inline const char* SeatVehicleName(uint64_t identity, bool onCarrier, uint64_t carrierIdentity,
                                   char (&buffer)[kNameCapacity]) noexcept
{
    if (onCarrier)
        if (const char* carrier = IdentityName(carrierIdentity))
            return carrier;
    if (const char* name = IdentityName(identity))
        return name;
    CustomName(identity, buffer);
    return buffer;
}

// ---- roles --------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kSeatDriverBit = 1u << 2;
inline constexpr uint32_t kSeatGunnerBit = 1u << 3;

// The seat's role as the camera file's SeatBits.
inline uint8_t SeatBits(bool flagsKnown, uint32_t seatFlags, bool onCarrier) noexcept
{
    const bool driver = flagsKnown && !onCarrier && (seatFlags & kSeatDriverBit) != 0;
    const bool gunner = onCarrier || (flagsKnown && (seatFlags & kSeatGunnerBit) != 0);
    return vehicle_camera_file::SeatBits(driver, gunner);
}

// ---- the game's own third-person camera ---------------------------------------------------------------------------

// Where the game's camera looks level, as an elevation above level from its pivot, in whole degrees: the camera
// ORBITS the pivot, so that is the elevation it stood at plus the look it had then (the same geometry Reach's camera
// was measured to have). `rise` is its height above the pivot, `distance` how far it stands, `lookDeg` its look (up
// is positive). Unset (vehicle_boom::kBoomPitchUnset) when the two do not make an angle.
inline float OrbitLevelElevationDeg(float rise, float distance, float lookDeg) noexcept
{
    if (!std::isfinite(rise) || !std::isfinite(distance) || !std::isfinite(lookDeg) || !(distance > 1.0e-3f) ||
        std::fabs(rise) > distance)
        return vehicle_boom::kBoomPitchUnset;
    const float degrees = std::asin(rise / distance) / vehicle_boom::kRadiansPerDegree + lookDeg;
    return std::isfinite(degrees) ? std::round(degrees) : vehicle_boom::kBoomPitchUnset;
}

// THE BOOM'S RANGE in Halo 4, as look pitches for vehicle_boom's orbit (the camera's elevation is minus the pitch):
// the camera track is not read, so the boom orbits the pivot over a range of its own. Reach's measured range (10 deg
// below to 60 deg above level: the whole flat look range put an orbiting camera straight above the vehicle or under
// the ground) until Halo 4's is measured.
inline constexpr float kBoomLowestDeg = -10.0f;
inline constexpr float kBoomHighestDeg = 60.0f;
inline vehicle_boom::Range BoomRange() noexcept
{
    return vehicle_boom::Range{-kBoomHighestDeg * vehicle_boom::kRadiansPerDegree,
                               -kBoomLowestDeg * vehicle_boom::kRadiansPerDegree};
}

// The point of the line (origin `o`, unit direction `d`) nearest `p`.
inline void NearestOnLine(const float o[3], const float d[3], const float p[3], float out[3]) noexcept
{
    const float along = (p[0] - o[0]) * d[0] + (p[1] - o[1]) * d[1] + (p[2] - o[2]) * d[2];
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = o[axis] + d[axis] * along;
}

// THE PIVOT FINDER. Each sample is a line, the game's camera and the way it looks, in the vehicle's own frame (world
// units). The point nearest all of them, in the least-squares sense, is where they cross:
//   A = sum (I - d d^T),  b = sum (I - d d^T) o,  A p = b.
// A is well conditioned only when the directions spread: its smallest eigenvalue, per sample, is about
// sin^2(spread / 2) for two directions `spread` apart. The answer is given once that passes kMinSpread and the lines
// pass within kMaxResidualWu of the point on average: a camera track shifts the line a little off the pivot, and
// lines that miss by more than that are not the following camera's.
struct PivotFinder
{
    // The spread the lines' directions must have (sin^2 of half of about 8 degrees), and how near the point the lines
    // must pass, root mean square (about 1.5 m).
    static constexpr double kMinSpread = 0.0049;
    static constexpr double kMaxResidualWu = 0.5;
    // Samples are not added past this many: the point is fixed on the vehicle, and plain sums would otherwise grow
    // without end (they are kept, not decayed).
    static constexpr uint32_t kMaxSamples = 4000;

    double a[6]{}; // xx, xy, xz, yy, yz, zz
    double b[3]{};
    double c = 0.0; // sum o^T (I - d d^T) o, for the residual
    uint32_t samples = 0;

    void Reset() noexcept { *this = PivotFinder{}; }

    // False (nothing added) for a sample that is not finite, a direction that is not a unit vector, or a full finder.
    bool Add(const float o[3], const float d[3]) noexcept
    {
        if (samples >= kMaxSamples)
            return false;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(o[axis]) || !std::isfinite(d[axis]))
                return false;
        const double dx = d[0], dy = d[1], dz = d[2];
        const double length2 = dx * dx + dy * dy + dz * dz;
        if (std::fabs(length2 - 1.0) > 0.02)
            return false;
        const double m[6] = {1.0 - dx * dx, -dx * dy, -dx * dz, 1.0 - dy * dy, -dy * dz, 1.0 - dz * dz};
        const double ox = o[0], oy = o[1], oz = o[2];
        const double mo[3] = {m[0] * ox + m[1] * oy + m[2] * oz, m[1] * ox + m[3] * oy + m[4] * oz,
                              m[2] * ox + m[4] * oy + m[5] * oz};
        for (int k = 0; k < 6; ++k)
            a[k] += m[k];
        for (int k = 0; k < 3; ++k)
            b[k] += mo[k];
        c += ox * mo[0] + oy * mo[1] + oz * mo[2];
        ++samples;
        return true;
    }

    // The smallest eigenvalue of A per sample: how far the directions spread (0 = all one way).
    double Spread() const noexcept
    {
        if (!samples)
            return 0.0;
        const double n = static_cast<double>(samples);
        const double m00 = a[0] / n, m01 = a[1] / n, m02 = a[2] / n, m11 = a[3] / n, m12 = a[4] / n, m22 = a[5] / n;
        const double p1 = m01 * m01 + m02 * m02 + m12 * m12;
        if (p1 <= 1.0e-18)
        {
            double low = m00 < m11 ? m00 : m11;
            return low < m22 ? low : m22;
        }
        const double q = (m00 + m11 + m22) / 3.0;
        const double p2 = (m00 - q) * (m00 - q) + (m11 - q) * (m11 - q) + (m22 - q) * (m22 - q) + 2.0 * p1;
        const double p = std::sqrt(p2 / 6.0);
        if (!(p > 1.0e-12))
            return q;
        const double b00 = (m00 - q) / p, b11 = (m11 - q) / p, b22 = (m22 - q) / p;
        const double b01 = m01 / p, b02 = m02 / p, b12 = m12 / p;
        const double det = b00 * (b11 * b22 - b12 * b12) - b01 * (b01 * b22 - b12 * b02) + b02 * (b01 * b12 - b11 * b02);
        double r = det / 2.0;
        r = r < -1.0 ? -1.0 : (r > 1.0 ? 1.0 : r);
        const double phi = std::acos(r) / 3.0;
        const double kTwoThirdsPi = 2.0943951023931957;
        return q + 2.0 * p * std::cos(phi + kTwoThirdsPi);
    }

    // The point the lines cross at, and how far (rms, world units) they pass from it. False until they spread.
    bool Solve(float out[3], float* residualWu = nullptr) const noexcept
    {
        if (samples < 2 || Spread() < kMinSpread)
            return false;
        const double m00 = a[0], m01 = a[1], m02 = a[2], m11 = a[3], m12 = a[4], m22 = a[5];
        const double c00 = m11 * m22 - m12 * m12, c01 = m02 * m12 - m01 * m22, c02 = m01 * m12 - m02 * m11;
        const double det = m00 * c00 + m01 * c01 + m02 * c02;
        if (!std::isfinite(det) || std::fabs(det) < 1.0e-12)
            return false;
        const double c11 = m00 * m22 - m02 * m02, c12 = m01 * m02 - m00 * m12, c22 = m00 * m11 - m01 * m01;
        const double p[3] = {(c00 * b[0] + c01 * b[1] + c02 * b[2]) / det, (c01 * b[0] + c11 * b[1] + c12 * b[2]) / det,
                             (c02 * b[0] + c12 * b[1] + c22 * b[2]) / det};
        const double ap[3] = {m00 * p[0] + m01 * p[1] + m02 * p[2], m01 * p[0] + m11 * p[1] + m12 * p[2],
                              m02 * p[0] + m12 * p[1] + m22 * p[2]};
        double sum = p[0] * ap[0] + p[1] * ap[1] + p[2] * ap[2] - 2.0 * (p[0] * b[0] + p[1] * b[1] + p[2] * b[2]) + c;
        sum = sum < 0.0 ? 0.0 : sum;
        const double residual = std::sqrt(sum / static_cast<double>(samples));
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || !(residual <= kMaxResidualWu))
            return false;
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = static_cast<float>(p[axis]);
        if (residualWu)
            *residualWu = static_cast<float>(residual);
        return true;
    }
};
} // namespace halo4_vehicle_camera
