#pragma once
// THE THIRD-PERSON BOOM: where a camera of origin "thirdperson" (vehicle_camera_file.h) stands from the game's own
// pivot, as an angle the player raises and lowers with the right stick, apart from the aim (vehicle_cameras.inl
// runs it; halo3_vehicle_boom.inl reads the game's side).
//
// HALO 3'S OWN THIRD-PERSON CAMERA IN A SEAT IS NOT AN ORBIT. The following camera (halo3 +0x213CF4; RVAs of the
// pinned module, docs/HALO3-VEHICLE-EVIDENCE.md) hangs it off the seat camera's pivot by the seat's CAMERA TRACK
// ('trak'), evaluated at the player's LOOK PITCH:
//   t      = the track parameter whose control points' orientation looks at that pitch (+0x214A54, a bisection);
//   offset = the track's control-point positions at t (+0x2146C0, a cubic through four points, +0x214D80),
// laid along the look's heading: forward, left, and the world's up. Its distance from the pivot is the length of
// that offset, so the distance changes with the pitch as well as the height: looking down raises the camera,
// looking up lowers it. The flat game holds the look pitch inside the seat's range (+0xF7DF0): the seat camera's
// own where either end of it is set, each end held to +-85.5 deg; else +-85.5 deg.
//
// THE BOOM keeps a look pitch of its own -- never the aim's -- and stands the camera where the game's would stand
// at that pitch, inside that range. The stick moves it the way the flat game's stick moves the look: pushing up
// raises the pitch and so lowers the camera; the game's look inversion turns that round. What the player sees and
// keeps is the camera's ELEVATION: the angle above level at which it stands from the pivot.
//
// Without the track (a seat whose tags could not be read) the boom is a plain orbit of the pivot: the elevation is
// minus the pitch, at a distance the caller measures.
//
// Pure: no engine, no clock, no allocation.

#include <cmath>
#include <cstdint>

namespace vehicle_boom
{
inline constexpr float kRadiansPerDegree = 0.0174532925f;
// The flat game's own look limit, either way (+0xF7DF0's two constants, read from the module where it can be).
inline constexpr float kFlatLookLimit = 1.49225652f; // 85.5 deg
// The following camera's own hold on the pitch it evaluates the track at (+0x214052).
inline constexpr float kTrackPitchLimit = 1.57079633f;

// ---- the flat game's look range ------------------------------------------------------------------------------

struct Range
{
    float min = -kFlatLookLimit;
    float max = kFlatLookLimit;
};

// +0xF8334: the seat camera's range (radians) where either end is non-zero, each end held to the flat limits;
// else the flat limits. Limits that are not a range are the flat game's own +-85.5 deg.
inline Range LookRange(float cameraMin, float cameraMax, float flatMin = -kFlatLookLimit,
                       float flatMax = kFlatLookLimit) noexcept
{
    if (!std::isfinite(flatMin) || !std::isfinite(flatMax) || !(flatMin < flatMax))
    {
        flatMin = -kFlatLookLimit;
        flatMax = kFlatLookLimit;
    }
    Range range{flatMin, flatMax};
    if (std::isfinite(cameraMin) && std::isfinite(cameraMax) && (cameraMin != 0.0f || cameraMax != 0.0f))
    {
        const auto hold = [&](float v) { return v < flatMin ? flatMin : (v > flatMax ? flatMax : v); };
        range.min = hold(cameraMin);
        range.max = hold(cameraMax);
        if (range.min > range.max)
        {
            const float swap = range.min;
            range.min = range.max;
            range.max = swap;
        }
    }
    return range;
}

inline float Hold(float pitch, const Range& range) noexcept
{
    if (!std::isfinite(pitch))
        return 0.0f < range.min ? range.min : (0.0f > range.max ? range.max : 0.0f);
    return pitch < range.min ? range.min : (pitch > range.max ? range.max : pitch);
}

// ---- the game's camera track ---------------------------------------------------------------------------------

// One control point as the tag holds it: a position (world units, along the look's forward, left and up) and an
// orientation (i, j, k, w).
struct TrackPoint
{
    float position[3];
    float orientation[4];
};
inline constexpr int kMinTrackPoints = 4; // +0x2146C0 evaluates nothing with fewer
inline constexpr int kMaxTrackPoints = 32;
struct Track
{
    int count = 0;
    TrackPoint points[kMaxTrackPoints]{};
};

// The track's parameter is solved to this (+0x214A54's own constants: from 0 to 1, halving to 1e-4).
inline constexpr float kSolveSpan = 1.0f;
inline constexpr float kSolveHalf = 0.5f;
inline constexpr float kSolveEpsilon = 1.0e-4f;
// +0x214D80's divisor for the third difference.
inline constexpr float kCubicThree = 3.0f;

// +0x214D80: the cubic through (t0, p0), (t0 + h, p1), (t0 + 2h, p2), (t0 + 3h, p3), by Newton's forward
// differences, in the order the game evaluates it.
inline float UniformCubic(float p0, float p1, float p2, float p3, float t0, float h, float t) noexcept
{
    const float d1 = p1 - p0;
    const float d2 = (p2 - p1) - d1;
    const float d3 = ((p3 - p2) - (p2 - p1)) - d2;
    float v = (t - (t0 + 2.0f * h)) * d3 / (kCubicThree * h) + d2;
    v = v * (t - (t0 + h)) / (2.0f * h) + d1;
    return v * (t - t0) / h + p0;
}

// +0xA10EC: `v` turned by the quaternion `q` (i, j, k, w).
inline void Rotate(const float q[4], const float v[3], float out[3]) noexcept
{
    const float w = q[3];
    const float dot2 = 2.0f * (v[0] * q[0] + v[1] * q[1] + v[2] * q[2]);
    const float scale = 2.0f * w * w - 1.0f;
    const float cross[3]{q[1] * v[2] - q[2] * v[1], q[2] * v[0] - q[0] * v[2], q[0] * v[1] - q[1] * v[0]};
    for (int k = 0; k < 3; ++k)
        out[k] = q[k] * dot2 + v[k] * scale + cross[k] * 2.0f * w;
}

inline bool Usable(const Track& track) noexcept
{
    if (track.count < kMinTrackPoints || track.count > kMaxTrackPoints)
        return false;
    for (int i = 0; i < track.count; ++i)
    {
        const TrackPoint& point = track.points[i];
        float norm = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            if (!std::isfinite(point.orientation[k]))
                return false;
            norm += point.orientation[k] * point.orientation[k];
        }
        if (!(std::fabs(norm - 1.0f) <= 0.05f))
            return false;
        for (int k = 0; k < 3; ++k)
            if (!std::isfinite(point.position[k]) || std::fabs(point.position[k]) > 100.0f)
                return false;
    }
    return true;
}

// +0x2146C0: the track at parameter `t` (0..1). `position` and `forward` may each be null. The four points it
// evaluates through are those the game picks: one before the frame `t` falls in, never past the last four.
inline bool TrackAt(const Track& track, float t, float* position, float* forward) noexcept
{
    if (track.count < kMinTrackPoints || track.count > kMaxTrackPoints || !std::isfinite(t))
        return false;
    const int count = track.count;
    const float h = 1.0f / static_cast<float>(count - 1);
    const int frame = static_cast<int>(static_cast<float>(count - 1) * t);
    int index = frame;
    while (index > 0 && (index + 4 > count || index > frame - 1))
        --index;
    if (index < 0)
        index = 0;
    const float t0 = static_cast<float>(index) * h;
    const TrackPoint* p = &track.points[index];
    if (forward)
    {
        static constexpr float kAhead[3]{1.0f, 0.0f, 0.0f};
        float along[4][3];
        for (int i = 0; i < 4; ++i)
            Rotate(p[i].orientation, kAhead, along[i]);
        for (int k = 0; k < 3; ++k)
            forward[k] = UniformCubic(along[0][k], along[1][k], along[2][k], along[3][k], t0, h, t);
    }
    if (position)
        for (int k = 0; k < 3; ++k)
            position[k] = UniformCubic(p[0].position[k], p[1].position[k], p[2].position[k], p[3].position[k], t0, h,
                                       t);
    return true;
}

// +0x214A54: the parameter whose forward looks at `pitch` (radians), halving from [0, 1] to kSolveEpsilon. A pitch
// past the track's own ends gives that end.
inline float TrackParameterForPitch(const Track& track, float pitch) noexcept
{
    float low = 0.0f, high = kSolveSpan, middle = 0.0f;
    for (int guard = 0; guard < 64; ++guard)
    {
        middle = (high + low) * kSolveHalf;
        float forward[3]{1.0f, 0.0f, 0.0f};
        if (!TrackAt(track, middle, nullptr, forward))
            return middle;
        const float along = std::atan2(forward[2], std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]));
        if (pitch > along)
            low = middle;
        else
            high = middle;
        if (!(high - low > kSolveEpsilon))
            break;
    }
    return middle;
}

// Where the game's camera stands from its pivot at look pitch `pitch`: forward, left, up, in world units.
inline bool TrackOffset(const Track& track, float pitch, float out[3]) noexcept
{
    const float held = pitch < -kTrackPitchLimit ? -kTrackPitchLimit : (pitch > kTrackPitchLimit ? kTrackPitchLimit : pitch);
    return TrackAt(track, TrackParameterForPitch(track, held), out, nullptr);
}

// ---- the boom's shape for a seat -----------------------------------------------------------------------------

// The camera's place across the look range, worked out once per seat so a frame costs a lookup: kShapeSamples
// look pitches from range.min to range.max, each with its offset (forward, left, up). From the track: world units.
// An orbit: per unit of distance.
inline constexpr int kShapeSamples = 65;
struct Shape
{
    bool valid = false;
    bool track = false;
    Range range{};
    float offset[kShapeSamples][3]{};
};

inline float SamplePitch(const Shape& shape, int index) noexcept
{
    return shape.range.min +
           (shape.range.max - shape.range.min) * static_cast<float>(index) / static_cast<float>(kShapeSamples - 1);
}

inline bool BuildTrackShape(const Track& track, const Range& range, Shape& out) noexcept
{
    out = Shape{};
    if (!Usable(track) || !std::isfinite(range.min) || !std::isfinite(range.max) || range.min > range.max)
        return false;
    out.range = range;
    for (int i = 0; i < kShapeSamples; ++i)
    {
        float place[3];
        if (!TrackOffset(track, SamplePitch(out, i), place) || !std::isfinite(place[0]) || !std::isfinite(place[1]) ||
            !std::isfinite(place[2]))
        {
            out = Shape{};
            return false;
        }
        for (int k = 0; k < 3; ++k)
            out.offset[i][k] = place[k];
    }
    out.track = true;
    out.valid = true;
    return true;
}

// An orbit: the camera behind the pivot along the look, at elevation -pitch.
inline void BuildOrbitShape(const Range& range, Shape& out) noexcept
{
    out = Shape{};
    out.range = std::isfinite(range.min) && std::isfinite(range.max) && range.min <= range.max ? range : Range{};
    for (int i = 0; i < kShapeSamples; ++i)
    {
        const float pitch = SamplePitch(out, i);
        out.offset[i][0] = -std::cos(pitch);
        out.offset[i][1] = 0.0f;
        out.offset[i][2] = -std::sin(pitch);
    }
    out.valid = true;
}

// The offset at `pitch` (held to the range), between the samples either side.
inline void OffsetAt(const Shape& shape, float pitch, float out[3]) noexcept
{
    out[0] = out[1] = out[2] = 0.0f;
    if (!shape.valid)
        return;
    const float span = shape.range.max - shape.range.min;
    if (!(span > 1.0e-6f))
    {
        for (int k = 0; k < 3; ++k)
            out[k] = shape.offset[0][k];
        return;
    }
    const float u = (Hold(pitch, shape.range) - shape.range.min) / span * static_cast<float>(kShapeSamples - 1);
    int index = static_cast<int>(u);
    index = index < 0 ? 0 : (index > kShapeSamples - 2 ? kShapeSamples - 2 : index);
    const float along = u - static_cast<float>(index);
    for (int k = 0; k < 3; ++k)
        out[k] = shape.offset[index][k] + (shape.offset[index + 1][k] - shape.offset[index][k]) * along;
}

inline float ElevationOf(const float offset[3]) noexcept
{
    return std::atan2(offset[2], std::sqrt(offset[0] * offset[0] + offset[1] * offset[1]));
}

// The camera's elevation above level from the pivot at `pitch`, radians.
inline float ElevationAt(const Shape& shape, float pitch) noexcept
{
    float offset[3];
    OffsetAt(shape, pitch, offset);
    return ElevationOf(offset);
}

// The look pitch at which the camera stands at `elevation` (radians above level): of the pitches that reach it, the
// one nearest a level look; where none does, the end of the range that comes nearest.
inline float PitchForElevation(const Shape& shape, float elevation) noexcept
{
    if (!shape.valid)
        return 0.0f;
    if (!std::isfinite(elevation))
        return Hold(0.0f, shape.range);
    float best = 0.0f, bestMiss = 1.0e9f, bestFound = 1.0e9f;
    bool found = false;
    float previous = ElevationOf(shape.offset[0]);
    for (int i = 0; i < kShapeSamples; ++i)
    {
        const float here = ElevationOf(shape.offset[i]);
        const float miss = std::fabs(here - elevation);
        if (!found && miss < bestMiss)
        {
            bestMiss = miss;
            best = SamplePitch(shape, i);
        }
        if (i > 0 && (elevation - previous) * (elevation - here) <= 0.0f)
        {
            const float rise = here - previous;
            const float along = std::fabs(rise) > 1.0e-7f ? (elevation - previous) / rise : 0.0f;
            const float low = SamplePitch(shape, i - 1), high = SamplePitch(shape, i);
            const float pitch = low + (high - low) * along;
            if (!found || std::fabs(pitch) < bestFound)
            {
                bestFound = std::fabs(pitch);
                best = pitch;
                found = true;
            }
        }
        previous = here;
    }
    return Hold(best, shape.range);
}

// Where the camera stands from the pivot at `pitch`: the shape's offset, scaled by `orbitDistance` (world units)
// for an orbit. Forward, left, up.
inline void Offset(const Shape& shape, float pitch, float orbitDistance, float out[3]) noexcept
{
    OffsetAt(shape, pitch, out);
    if (shape.valid && !shape.track)
    {
        const float scale = std::isfinite(orbitDistance) && orbitDistance > 0.0f ? orbitDistance : 0.0f;
        for (int k = 0; k < 3; ++k)
            out[k] *= scale;
    }
}

// ---- where it starts ------------------------------------------------------------------------------------------

// The camera file's "boomPitch" left out. A camera where the game's own rests then starts where the game's camera
// stands looking level -- from its track, else as measured headless in a Mongoose: 474 cm from the pivot, 151 cm of
// that above it, asin(151 / 474) -- and kDefaultRaiseDeg higher: the game's place can feel low from a seat.
inline constexpr float kBoomPitchUnset = -1000.0f;
inline constexpr float kMaxBoomPitchDeg = 89.0f;
inline constexpr float kMeasuredLevelElevationDeg = 18.6f;
inline constexpr float kDefaultRaiseDeg = 4.0f;

inline bool BoomPitchSet(float degrees) noexcept
{
    return std::isfinite(degrees) && degrees >= -kMaxBoomPitchDeg && degrees <= kMaxBoomPitchDeg;
}

// The elevation (radians) a camera starts at: the file's, else the game's level look raised a little.
inline float DefaultElevation(const Shape& shape, float fileDegrees) noexcept
{
    if (BoomPitchSet(fileDegrees))
        return fileDegrees * kRadiansPerDegree;
    if (shape.valid && shape.track)
        return ElevationAt(shape, 0.0f) + kDefaultRaiseDeg * kRadiansPerDegree;
    return (kMeasuredLevelElevationDeg + kDefaultRaiseDeg) * kRadiansPerDegree;
}

// ---- the stick ------------------------------------------------------------------------------------------------

// The turn stick's own deadzone; past it the push is rescaled to start from nothing.
inline constexpr float kStickDeadzone = 0.15f;
inline constexpr float kMaxStepSeconds = 0.1f; // a longer frame moves the boom as this long
inline constexpr float kDefaultRateDegPerSecond = 60.0f;
inline constexpr float kMinRateDegPerSecond = 5.0f;
inline constexpr float kMaxRateDegPerSecond = 360.0f;

inline float StickAxis(float y) noexcept
{
    if (!std::isfinite(y))
        return 0.0f;
    const float size = std::fabs(y);
    if (size <= kStickDeadzone)
        return 0.0f;
    const float push = (size > 1.0f ? 1.0f : size) - kStickDeadzone;
    return (y < 0.0f ? -push : push) / (1.0f - kStickDeadzone);
}

// The flat game's inversion for the right stick's Y (+0x1871BC, +0xF42D8): an aircraft's pilot takes the flight
// inversion, every other seat the look inversion.
inline constexpr bool GameInverts(bool lookInverted, bool flightInverted, bool piloting) noexcept
{
    return piloting ? flightInverted : lookInverted;
}

// vehicle_boom_invert: -1 = as the game (not inverted when its setting could not be read), 0 = no, 1 = yes.
inline constexpr bool Inverted(int setting, bool gameKnown, bool gameInverts) noexcept
{
    return setting == 1 || (setting != 0 && gameKnown && gameInverts);
}

// One step of the boom's look pitch: the stick's Y over `seconds` at `degreesPerSecond`, held to the range.
// Pushing up raises the pitch -- the camera goes down, as the flat game's does -- unless `inverted`.
inline float Step(float pitch, float stickY, float seconds, float degreesPerSecond, bool inverted,
                  const Range& range) noexcept
{
    const float held = Hold(pitch, range);
    if (!std::isfinite(seconds) || !(seconds > 0.0f) || !std::isfinite(degreesPerSecond))
        return held;
    const float step = seconds > kMaxStepSeconds ? kMaxStepSeconds : seconds;
    const float rate = degreesPerSecond < kMinRateDegPerSecond ? kMinRateDegPerSecond
                     : degreesPerSecond > kMaxRateDegPerSecond ? kMaxRateDegPerSecond
                                                                : degreesPerSecond;
    const float axis = StickAxis(stickY);
    return Hold(held + (inverted ? -axis : axis) * rate * kRadiansPerDegree * step, range);
}
} // namespace vehicle_boom
