// THE SOUND LISTENER WITNESS: the pure half (no game, engine or OpenXR includes). DEV BUILDS ONLY: the runtime
// that includes this sits inside #if HALOMCCVR_DEV_DIAGNOSTICS, and nothing here changes anything a game reads.
//
// In this mod the player aims with the hand and looks with the head. The game's sound listener should be
// oriented by the HEAD and sit at the head. For Halo 2 and Halo CE that is a claim from reading the code, not a
// measurement (docs/HALO2-HEAD-RELATIVE-2026-10-07.md and docs/HALOCE-HEAD-RELATIVE-2026-10-07.md, their
// sound sections). The witness is the measurement: every ~2 s one log line that says, from numbers alone,
// whether the listener the game built is oriented by the head, by the aim or by something else, and where it is.
//
// This header holds what both titles share: the angle between two directions, the test that three rows are a
// Blam rotation, one measure's window, the window a sampler fills and a reporter prints, the refusals by name,
// and the verdict. A title's own header decodes its listener (src/common/halo2_sound_listener_logic.h; Halo CE's
// packet is src/common/haloce_audio_logic.h).
//
// THREADS. A Window is plain data. One sampler fills it (the title's runtime keeps that to one thread at a time
// with a flag that is tried and never waited for) and hands a finished copy to the reporter under a sequence
// lock. Nothing here allocates, logs, locks or waits.
#pragma once

#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace listener_witness
{
inline constexpr double kRadToDeg = 57.295779513082320876798154814105;
// A window in which every sample's angle stayed within this of a reference "follows" it. (A threshold of ours:
// it locates nothing in a game. Float rows that were copied, not recomputed, measure 0.00.)
inline constexpr float kFollowsDeg = 0.5f;
// A listener within this of the head's position is "at the head" (world units; a threshold of ours).
inline constexpr float kAtDistance = 0.005f;
// The sampler closes its window after this long, and the reporter prints at this pace.
inline constexpr uint64_t kWindowMs = 2000;

inline bool Finite3(const float v[3])
{
    return v && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// The angle between two directions, in degrees in [0, 180]. atan2(|a x b|, a . b), in double: exact near 0 and
// near 180, where acos of a float dot product is not (a tenth of a degree is the whole question here). Neither
// has to be unit length. False (out untouched) when either is not finite or is shorter than 1e-6.
inline bool AngleDeg(const float a[3], const float b[3], float& out)
{
    if (!Finite3(a) || !Finite3(b))
        return false;
    const double ax = a[0], ay = a[1], az = a[2], bx = b[0], by = b[1], bz = b[2];
    const double la = std::sqrt(ax * ax + ay * ay + az * az), lb = std::sqrt(bx * bx + by * by + bz * bz);
    if (!(la > 1.0e-6) || !(lb > 1.0e-6))
        return false;
    const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    const double angle = std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz);
    if (!std::isfinite(angle))
        return false;
    out = static_cast<float>(angle * kRadToDeg);
    return true;
}

// The distance between two points. False (out untouched) when either is not finite.
inline bool Distance(const float a[3], const float b[3], float& out)
{
    if (!Finite3(a) || !Finite3(b))
        return false;
    const double dx = static_cast<double>(a[0]) - b[0], dy = static_cast<double>(a[1]) - b[1],
                 dz = static_cast<double>(a[2]) - b[2];
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance))
        return false;
    out = static_cast<float>(distance);
    return true;
}

// The forward of a yaw and a pitch in Blam's angles: yaw from world +X toward +Y, pitch up from the horizon
// (the convention this repository's direct drives write with: cameraYaw = atan2(f.y, f.x), cameraPitch =
// atan2(f.z, hypot(f.x, f.y))). False when either angle is not finite.
inline bool ForwardOfAngles(float yaw, float pitch, float out[3])
{
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !out)
        return false;
    const float c = std::cos(pitch);
    out[0] = c * std::cos(yaw);
    out[1] = c * std::sin(yaw);
    out[2] = std::sin(pitch);
    return true;
}

// Three rows are a Blam rotation: each unit length, mutually perpendicular, and LEFT = UP x FORWARD (the second
// row both titles' matrix builders compute: Halo 2 +0x729BA0, Halo CE +0xBA24EC). The tolerance is on lengths,
// dot products and the cross product's difference.
inline bool IsBlamBasis(const float forward[3], const float left[3], const float up[3], float tolerance = 2.0e-3f)
{
    if (!Finite3(forward) || !Finite3(left) || !Finite3(up))
        return false;
    using head_relative::Cross;
    using head_relative::Dot;
    using head_relative::Length;
    if (std::fabs(Length(forward) - 1.0f) > tolerance || std::fabs(Length(left) - 1.0f) > tolerance ||
        std::fabs(Length(up) - 1.0f) > tolerance)
        return false;
    if (std::fabs(Dot(forward, up)) > tolerance || std::fabs(Dot(forward, left)) > tolerance ||
        std::fabs(Dot(left, up)) > tolerance)
        return false;
    float cross[3];
    Cross(up, forward, cross);
    return std::fabs(cross[0] - left[0]) <= tolerance && std::fabs(cross[1] - left[1]) <= tolerance &&
           std::fabs(cross[2] - left[2]) <= tolerance;
}

// A position and an orientation in the game's world.
struct Pose
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
};

// One measure over a window. Plain: the sampler's alone until the window is handed over.
struct Measure
{
    uint32_t count = 0;
    double sum = 0.0;
    float max = 0.0f;
    float last = 0.0f;
    void Add(float value)
    {
        if (!std::isfinite(value))
            return;
        ++count;
        sum += value;
        if (count == 1 || value > max)
            max = value;
        last = value;
    }
    float Mean() const { return count ? static_cast<float>(sum / count) : 0.0f; }
};

// What a window measures. "Listener" is the listener the GAME built; "handed" is what reached the game's sound
// backend after this mod's own hook, where a title has one (Halo CE).
enum class M : uint8_t
{
    HeadForward,      // listener forward against the head's forward (degrees)
    HeadUp,           // listener up against the head's up (degrees)
    HeadDistance,     // listener position to the head's (centre eye) position (world units)
    AimForward,       // listener forward against the weapon aim's forward (degrees)
    CameraForward,    // listener forward against the game's own camera's forward (degrees)
    CameraUp,
    CameraDistance,
    RootForward,      // listener forward against the record the game builds the listener from, as it stood
    RootUp,
    RootDistance,
    HeadAimApart,     // the head's forward against the aim's: how far apart the two candidates were
    HandedHeadForward,  // what was handed to the backend, against the head's forward
    HandedHeadUp,
    Count
};
inline constexpr size_t kMeasures = static_cast<size_t>(M::Count);

// Why a sample measured nothing. Each one is a count in the line.
enum class Refusal : uint8_t
{
    NotArmed,     // the title's camera core is not armed in a live level, or this is another load of the title
    NoGlobals,    // the pointer to the game's sound state is null
    Unreadable,   // a read faulted
    NotPlaying,   // the game's own test says the local user is not playing: it leaves that listener as it was
    NeverBuilt,   // the listener's "valid" byte is clear: the game has not built it
    Torn,         // two reads of the record a moment apart differ: it was being rewritten
    NotABasis,    // the matrix is not a rotation with scale 1 (not finite, not unit, not perpendicular)
    Overlapped,   // another sample was in progress on another thread
    Count
};
inline constexpr size_t kRefusals = static_cast<size_t>(Refusal::Count);
inline const char* RefusalName(Refusal why)
{
    switch (why)
    {
    case Refusal::NotArmed: return "the camera core is not armed in a live level";
    case Refusal::NoGlobals: return "the sound state pointer is null";
    case Refusal::Unreadable: return "a pointer was not readable";
    case Refusal::NotPlaying: return "user 0 has no listener (not playing)";
    case Refusal::NeverBuilt: return "the record is not valid (never built)";
    case Refusal::Torn: return "the record changed between two reads";
    case Refusal::NotABasis: return "the matrix is not a rotation";
    case Refusal::Overlapped: return "another sample was in progress";
    default: return "?";
    }
}

inline constexpr size_t kHeadUses = static_cast<size_t>(head_relative::Use::Count);

// One window: what one sampler measured between `openedMs` and `closedMs`.
struct Window
{
    uint64_t serial = 0;          // 0 = nothing published yet
    uint64_t openedMs = 0, closedMs = 0;
    uint32_t taken = 0;           // samples in which the game's listener was read and decoded
    uint32_t refused[kRefusals]{};
    // Of the taken samples: the verdict on the head sample each one found (head_relative::Use; Ok = measured).
    uint32_t head[kHeadUses]{};
    uint32_t aimMissing = 0;      // taken samples with no weapon aim to hold the listener against
    uint32_t rootMissing = 0;     // taken samples in which the listener's source record did not read
    // The player's state in the taken samples (a title fills what it knows; the line names each).
    uint32_t seated = 0, firstPerson = 0, perspectiveUnknown = 0, controlLive = 0;
    uint32_t handedHead = 0, handedStock = 0;   // Halo CE: what this mod's own listener hook handed on
    uint32_t thread = 0, threadChanges = 0;     // the sampler's thread, and how often it was not the one before
    Measure m[kMeasures]{};

    Measure& operator[](M measure) { return m[static_cast<size_t>(measure)]; }
    const Measure& operator[](M measure) const { return m[static_cast<size_t>(measure)]; }
    uint32_t Refused() const
    {
        uint32_t total = 0;
        for (uint32_t count : refused)
            total += count;
        return total;
    }
    uint32_t HeadMissing() const
    {
        uint32_t total = 0;
        for (size_t i = 0; i < kHeadUses; ++i)
            if (i != static_cast<size_t>(head_relative::Use::Ok))
                total += head[i];
        return total;
    }
};

// What the window says the listener's ORIENTATION followed. Decided on the window's maxima, so one sample off
// the reference is enough to lose "follows": a window that straddles a pose step reads Neither, and the line's
// means say which way it leant.
enum class Verdict : uint8_t
{
    NoSamples,     // nothing was taken (every sample refused, or none arrived)
    NoReference,   // samples were taken but there was neither a head nor an aim to hold them against
    Head,          // within kFollowsDeg of the head's forward and up in every sample, and not of the aim
    Aim,           // within kFollowsDeg of the aim's forward in every sample, and not of the head
    Together,      // within kFollowsDeg of both: the head and the aim pointed the same way, so this window
                   // cannot tell them apart (step one of them)
    Camera,        // neither, but within kFollowsDeg of the game's own camera
    Neither        // none of them
};
inline bool Follows(const Measure& forward, const Measure* up, float tolerance)
{
    if (!forward.count || forward.max > tolerance)
        return false;
    return !up || !up->count || up->max <= tolerance;
}
inline Verdict Orientation(const Window& window, float tolerance = kFollowsDeg)
{
    if (!window.taken)
        return Verdict::NoSamples;
    const Measure& headForward = window[M::HeadForward];
    const Measure& aimForward = window[M::AimForward];
    if (!headForward.count && !aimForward.count && !window[M::CameraForward].count)
        return Verdict::NoReference;
    const bool head = Follows(headForward, &window[M::HeadUp], tolerance);
    const bool aim = Follows(aimForward, nullptr, tolerance);
    if (head && aim)
        return Verdict::Together;
    if (head)
        return Verdict::Head;
    if (aim)
        return Verdict::Aim;
    if (Follows(window[M::CameraForward], &window[M::CameraUp], tolerance))
        return Verdict::Camera;
    return Verdict::Neither;
}
inline const char* VerdictName(Verdict verdict)
{
    switch (verdict)
    {
    case Verdict::NoSamples: return "NO SAMPLES";
    case Verdict::NoReference: return "NO REFERENCE (no head sample and no aim)";
    case Verdict::Head: return "ORIENTED BY THE HEAD";
    case Verdict::Aim: return "ORIENTED BY THE AIM";
    case Verdict::Together: return "HEAD AND AIM TOGETHER (they pointed the same way: step one of them to tell)";
    case Verdict::Camera: return "ORIENTED BY THE GAME'S OWN CAMERA (neither the head nor the aim)";
    case Verdict::Neither: return "NEITHER THE HEAD NOR THE AIM";
    default: return "?";
    }
}

// Where the listener sat against the head's position over the window.
enum class Place : uint8_t
{
    Unknown,    // no sample had a head position
    AtHead,     // within kAtDistance of it in every sample
    Away        // not
};
inline Place Position(const Window& window, float tolerance = kAtDistance)
{
    const Measure& distance = window[M::HeadDistance];
    if (!distance.count)
        return Place::Unknown;
    return distance.max <= tolerance ? Place::AtHead : Place::Away;
}
inline const char* PlaceName(Place place)
{
    switch (place)
    {
    case Place::AtHead: return "AT THE HEAD";
    case Place::Away: return "NOT AT THE HEAD";
    default: return "POSITION UNKNOWN";
    }
}

// One sample of a listener against its references, into a window. A null reference is "not available this
// sample": the caller has already counted why. `listener` must be a decoded, valid listener.
struct References
{
    const Pose* head = nullptr;     // the head camera (centre eye)
    const float* aim = nullptr;     // the weapon aim's forward
    const Pose* camera = nullptr;   // the game's own camera, before any head was applied
    const Pose* root = nullptr;     // the record the game builds the listener from, as it stood
    const Pose* handed = nullptr;   // what was handed to the backend (a title with a hook of its own)
};
inline void MeasureAngle(Window& window, M measure, const float a[3], const float b[3])
{
    float value = 0.0f;
    if (AngleDeg(a, b, value))
        window[measure].Add(value);
}
inline void MeasureDistance(Window& window, M measure, const float a[3], const float b[3])
{
    float value = 0.0f;
    if (Distance(a, b, value))
        window[measure].Add(value);
}
inline void AddSample(Window& window, const Pose& listener, const References& refs)
{
    ++window.taken;
    if (refs.head)
    {
        MeasureAngle(window, M::HeadForward, listener.forward, refs.head->forward);
        MeasureAngle(window, M::HeadUp, listener.up, refs.head->up);
        MeasureDistance(window, M::HeadDistance, listener.position, refs.head->position);
    }
    if (refs.aim)
        MeasureAngle(window, M::AimForward, listener.forward, refs.aim);
    else
        ++window.aimMissing;
    if (refs.camera)
    {
        MeasureAngle(window, M::CameraForward, listener.forward, refs.camera->forward);
        MeasureAngle(window, M::CameraUp, listener.up, refs.camera->up);
        MeasureDistance(window, M::CameraDistance, listener.position, refs.camera->position);
    }
    if (refs.root)
    {
        MeasureAngle(window, M::RootForward, listener.forward, refs.root->forward);
        MeasureAngle(window, M::RootUp, listener.up, refs.root->up);
        MeasureDistance(window, M::RootDistance, listener.position, refs.root->position);
    }
    if (refs.head && refs.aim)
        MeasureAngle(window, M::HeadAimApart, refs.head->forward, refs.aim);
    if (refs.handed && refs.head)
    {
        MeasureAngle(window, M::HandedHeadForward, refs.handed->forward, refs.head->forward);
        MeasureAngle(window, M::HandedHeadUp, refs.handed->up, refs.head->up);
    }
}

// The window's refusals as "name=count, name=count" into `out` (always terminated; "none" when there are none).
// snprintf only: no allocation. Returns the length written.
inline int RefusalText(const Window& window, char* out, size_t capacity)
{
    if (!out || !capacity)
        return 0;
    out[0] = 0;
    int length = 0;
    for (size_t i = 0; i < kRefusals; ++i)
    {
        if (!window.refused[i] || static_cast<size_t>(length) + 80 >= capacity)
            continue;
        const int wrote = std::snprintf(out + length, capacity - length, "%s%s=%u", length ? ", " : "",
                                        RefusalName(static_cast<Refusal>(i)), window.refused[i]);
        if (wrote > 0)
            length += wrote;
    }
    if (!length)
        length = std::snprintf(out, capacity, "none");
    return length;
}
// The same for the head sample's verdicts on the taken samples (the ones that were not usable).
inline int HeadMissingText(const Window& window, char* out, size_t capacity)
{
    if (!out || !capacity)
        return 0;
    out[0] = 0;
    int length = 0;
    for (size_t i = 0; i < kHeadUses; ++i)
    {
        if (i == static_cast<size_t>(head_relative::Use::Ok) || !window.head[i] ||
            static_cast<size_t>(length) + 48 >= capacity)
            continue;
        const int wrote = std::snprintf(out + length, capacity - length, "%s%s=%u", length ? ", " : "",
                                        head_relative::UseName(static_cast<head_relative::Use>(i)), window.head[i]);
        if (wrote > 0)
            length += wrote;
    }
    if (!length)
        length = std::snprintf(out, capacity, "none");
    return length;
}
} // namespace listener_witness
