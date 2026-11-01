// HEAD-RELATIVE HUD AND SOUND: the pure half (no game, engine or OpenXR includes).
//
// In this mod the player AIMS with the hand and LOOKS with the head. A flat game has one heading for both, so its
// motion tracker and its sound listener take "the camera's forward" -- which here is the aim. Sweep the gun and
// every contact on the tracker swings; turn the head and nothing moves. Anything the player reads against where
// they look must be handed the HEAD's heading instead. This header holds what every title's seam shares: the
// heading of a forward vector, the yaw between two headings, "the game's forward with the head's yaw", the head
// sample one frame publishes for another thread, and the verdict on using it.
//
// Rules the callers keep (the title files, src/dll/<title>_head_relative.inl):
//   * the substitution reaches the tracker (or the listener) ONLY -- never the aim record, the camera evaluators the
//     shots use, or the rendered view;
//   * refusals are by identity and freshness, never by size, and each one is counted by reason; a refusal is the
//     game's own heading for that frame;
//   * the head sample crosses threads, so it carries a clock and a generation (the two-clocks rule).
#pragma once

#include <cmath>
#include <cstdint>

namespace head_relative
{
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;
// A forward vector steeper than this has no usable heading: looking straight up or down, its horizontal part is
// noise. HeadingHold keeps the last good one through that.
inline constexpr float kMaxElevationDeg = 80.0f;
// A head sample older than this is not this frame's head.
inline constexpr uint64_t kMaxSampleAgeUs = 100000;

inline bool Finite3(const float v[3])
{
    return v && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
inline float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline float Length(const float v[3]) { return std::sqrt(Dot(v, v)); }
inline void Cross(const float a[3], const float b[3], float out[3])
{
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}
inline bool UnitLength(const float v[3]) { return Finite3(v) && std::fabs(Length(v) - 1.0f) <= 1.0e-3f; }

// The horizontal heading of `forward` about the unit `worldUp`: forward with its part along worldUp removed,
// normalised. False (out untouched) when an input is not finite, worldUp is not unit length, forward is zero, or
// forward is steeper than kMaxElevationDeg.
inline bool Heading(const float forward[3], const float worldUp[3], float out[3])
{
    if (!Finite3(forward) || !UnitLength(worldUp) || !out)
        return false;
    const float length = Length(forward);
    if (!(length > 1.0e-6f))
        return false;
    const float along = Dot(forward, worldUp);
    const float flat[3]{forward[0] - worldUp[0] * along, forward[1] - worldUp[1] * along,
                        forward[2] - worldUp[2] * along};
    const float flatLength = Length(flat);
    // cos(elevation) = flatLength / length
    if (!(flatLength > length * std::cos(kMaxElevationDeg * kDegToRad)))
        return false;
    out[0] = flat[0] / flatLength;
    out[1] = flat[1] / flatLength;
    out[2] = flat[2] / flatLength;
    return true;
}

// The signed angle, in radians in (-pi, pi], that turns heading `from` onto heading `to` about worldUp by the
// right-hand rule (worldUp pointing at the viewer, positive is counter-clockwise). Both must be headings (unit,
// perpendicular to worldUp); anything else gives the angle between their directions as given.
inline float SignedYaw(const float from[3], const float to[3], const float worldUp[3])
{
    float cross[3];
    Cross(from, to, cross);
    return std::atan2(Dot(cross, worldUp), Dot(from, to));
}

// v turned about the unit `axis` by `angle` radians (right-hand rule). out may alias v.
inline void RotateAbout(const float v[3], const float axis[3], float angle, float out[3])
{
    const float c = std::cos(angle), s = std::sin(angle);
    float cross[3];
    Cross(axis, v, cross);
    const float along = Dot(axis, v) * (1.0f - c);
    const float x = v[0] * c + cross[0] * s + axis[0] * along;
    const float y = v[1] * c + cross[1] * s + axis[1] * along;
    const float z = v[2] * c + cross[2] * s + axis[2] * along;
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// The yaw, in radians, that takes the game's heading onto the head's: sign * (native -> head) + offsetRad.
// `sign` is 1 or -1 (anything negative counts as -1): a live safety value, for a seam whose handedness turned
// out mirrored. False when either vector has no heading.
inline bool YawToHead(const float nativeForward[3], const float headForward[3], const float worldUp[3], int sign,
                      float offsetRad, float& yawRad)
{
    float nativeHeading[3], headHeading[3];
    if (!Heading(nativeForward, worldUp, nativeHeading) || !Heading(headForward, worldUp, headHeading))
        return false;
    if (!std::isfinite(offsetRad))
        offsetRad = 0.0f;
    yawRad = (sign < 0 ? -1.0f : 1.0f) * SignedYaw(nativeHeading, headHeading, worldUp) + offsetRad;
    return true;
}

// THE GAME'S FORWARD WITH THE HEAD'S YAW: nativeForward turned about worldUp so its heading is the head's. It
// keeps the native vector's length and its part along worldUp (its pitch), so a consumer that also reads pitch
// sees what it saw before. False (out untouched) when either vector has no heading. out may alias nativeForward.
inline bool WithHeadYaw(const float nativeForward[3], const float headForward[3], const float worldUp[3], int sign,
                        float offsetRad, float out[3], float* appliedYawRad = nullptr)
{
    float yaw = 0.0f;
    if (!out || !YawToHead(nativeForward, headForward, worldUp, sign, offsetRad, yaw))
        return false;
    RotateAbout(nativeForward, worldUp, yaw, out);
    if (appliedYawRad)
        *appliedYawRad = yaw;
    return true;
}

// Where a contact sits on a tracker whose "up" is `heading`: the signed angle from the heading to the contact's
// horizontal direction, right-hand about worldUp (so a contact to the LEFT of the heading is positive). This is
// the model of what a tracker draws; the tests use it for the acceptance cases. False when the offset has no
// horizontal part.
inline bool Bearing(const float contactOffset[3], const float heading[3], const float worldUp[3], float& bearingRad)
{
    float flat[3];
    if (!Finite3(contactOffset) || !UnitLength(worldUp))
        return false;
    const float along = Dot(contactOffset, worldUp);
    flat[0] = contactOffset[0] - worldUp[0] * along;
    flat[1] = contactOffset[1] - worldUp[1] * along;
    flat[2] = contactOffset[2] - worldUp[2] * along;
    const float length = Length(flat);
    if (!(length > 1.0e-6f))
        return false;
    flat[0] /= length;
    flat[1] /= length;
    flat[2] /= length;
    bearingRad = SignedYaw(heading, flat, worldUp);
    return true;
}

// Looking straight up or down leaves no heading. Keep the last defined one so the tracker holds still through
// it instead of snapping to the game's. Not thread-safe: one per consumer.
struct HeadingHold
{
    float heading[3]{};
    bool have = false;
};
// The head's heading now, or the last one it had. False only before any heading was ever defined.
inline bool HeldHeading(HeadingHold& hold, const float headForward[3], const float worldUp[3], float out[3])
{
    float heading[3];
    if (Heading(headForward, worldUp, heading))
    {
        hold.heading[0] = heading[0];
        hold.heading[1] = heading[1];
        hold.heading[2] = heading[2];
        hold.have = true;
    }
    if (!hold.have)
        return false;
    out[0] = hold.heading[0];
    out[1] = hold.heading[1];
    out[2] = hold.heading[2];
    return true;
}

// The head's orientation in the game's world, as the frame that rendered with it published it.
struct HeadSample
{
    float forward[3]{};
    float up[3]{};
    uint64_t stampUs = 0;     // the publisher's clock (the sight clock, Sight_NowUs) when the frame was built
    uint32_t generation = 0;  // the title generation the frame belongs to
    bool valid = false;       // the mod's head view was live for the local user in that frame
};

// May this consumer use the head now? Identity and freshness only; every reason has a name for the log.
enum class Use : uint8_t
{
    Ok,
    SwitchOff,        // the player's switch is off
    NotLocalUser,     // not output user 0
    NotGameplay,      // a cinematic, a menu, no player-controlled unit
    NoSample,         // no frame has published a head yet, or the head view was not live in it
    OtherGeneration,  // the sample belongs to another load of the title
    FromTheFuture,    // the sample's clock is ahead of the consumer's
    Stale,            // older than the age limit
    NoHeading,        // non-finite, or no heading was ever defined
    Count
};
inline const char* UseName(Use use)
{
    switch (use)
    {
    case Use::Ok: return "used";
    case Use::SwitchOff: return "switch off";
    case Use::NotLocalUser: return "not the local user";
    case Use::NotGameplay: return "not gameplay";
    case Use::NoSample: return "no head sample";
    case Use::OtherGeneration: return "another generation";
    case Use::FromTheFuture: return "sample from the future";
    case Use::Stale: return "stale head sample";
    case Use::NoHeading: return "no heading";
    default: return "?";
    }
}
// The sample half of the verdict: SwitchOff, NotLocalUser and NotGameplay are the caller's own tests.
inline Use SampleUse(const HeadSample& sample, uint32_t generation, uint64_t nowUs,
                     uint64_t maxAgeUs = kMaxSampleAgeUs)
{
    if (!sample.valid || !sample.stampUs)
        return Use::NoSample;
    if (!generation || sample.generation != generation)
        return Use::OtherGeneration;
    if (sample.stampUs > nowUs)
        return Use::FromTheFuture;
    if (nowUs - sample.stampUs > maxAgeUs)
        return Use::Stale;
    if (!Finite3(sample.forward) || !Finite3(sample.up))
        return Use::NoHeading;
    return Use::Ok;
}
} // namespace head_relative
