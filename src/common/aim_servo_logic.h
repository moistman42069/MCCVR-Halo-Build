#pragma once

#include <cmath>
#include <cstdint>

// Closed-loop aim against a TURRET seat.
//
// The shared aim loop emits a right-stick deflection proportional to the
// angular error between the game's aim and the controller ray, and the engine
// integrates it through its own turn-rate path. That works on foot, where the
// aim is effectively unbounded and the loop's authority is continuous. A
// turret seat breaks BOTH of those assumptions, which is the 2026-08-06 report
// "in both halo 3 and odst the turrets wiggle like crazy up and down trying to
// keep up with the vr cross hair".
//
// Evidence (H3EK, exported with `tool export-tag-to-xml`, seat blocks):
//
//   objects\vehicles\warthog\turrets\chaingun  seat `warthog_g`
//       yaw rate bounds   60,60      pitch rate bounds  0,0
//       pitch range      -19,35
//   objects\vehicles\shade                     seat `shade_d`
//       yaw rate bounds   15,15      pitch rate bounds  15,15
//       pitch range      -45,45
//
// Two distinct consequences, one per reported symptom:
//
// 1. WIGGLE. The Warthog turret's YAW is capped at 60 deg/s but its PITCH
//    carries no cap at all, and the pitch axis is the one the user sees
//    oscillate. The loop cannot damp it, because our XInput mapping cannot
//    express a small correction: ToRawStick floors every non-zero command at
//    9000/32767 = 27.5% deflection so that it clears MCC's inner deadzone.
//    The engine therefore only ever receives "stop" or "at least 27.5%", and
//    a pure proportional command is essentially never exactly zero. On an
//    uncapped axis that quantised authority steps straight past the target,
//    the error changes sign, and the loop drives back: a limit cycle whose
//    amplitude is one minimum step. Yaw hides it because 60 deg/s absorbs the
//    step; pitch has nothing to absorb it with.
//
// 2. SHOTS THAT DO NOT FOLLOW. The Shade slews at 15 deg/s on both axes. A
//    hand can cross its whole 90 deg pitch range in a flick that the gun needs
//    six seconds to follow, so the barrel spends most of its life far from the
//    hand ray the reticle is drawn along. The honest-crosshair path already
//    existed for a seat's angular LIMIT, but it waits for the error to STALL,
//    and a 15 deg/s gun is not stalled - it is converging, one degree at a
//    time, which actively resets the stall timer every frame. So the reticle
//    kept promising the hand's direction for seconds on end.
//
// This header holds the pure, testable half of the fix. Both parts are scoped
// to turret seats: no other seat, title, or on-foot case reaches them.

// ---- 1. Rest hysteresis --------------------------------------------------
//
// The cure for chatter against a quantised actuator is hysteresis, not gain.
// Once the gun is inside `restEnterRadians` of the hand the loop parks and
// commands exactly zero, and it only takes authority again once the error
// grows past `restExitRadians`. Parking is what the proportional form can
// never do on its own, because 27.5% is its smallest non-zero word.
//
// The band must also swallow the actuator's own resolution, or the loop
// re-commands the moment it parks and the cycle resumes one notch wider.
//
// That resolution is the MINIMUM step, not the largest: the smallest angle the
// engine can be made to move. Our command reaches it through ToRawStick, which
// floors anything below 9000/32767 at exactly that deflection, so every
// near-target frame moves the aim by one fixed quantum - look sensitivity
// times the floor times a frame. It is not a constant we can know offline
// (the player owns the sensitivity, and the seat's authored cap may bind
// first), so the servo measures it, sampling ONLY the frames where our
// requested command was inside the floor region and the engine therefore ran
// at minimum authority.
//
// Given a quantum S, an unparked loop lands on a lattice straddling the
// target, so within one cycle the error must come within S/2 of it. A rest
// band of 0.6*S is therefore guaranteed to catch it, and 1.5*S is guaranteed
// to hold it. Both floors below still apply, so a fine actuator parks tightly
// rather than inheriting a band it does not need.
inline constexpr float kAimServoStickFloor = 9000.0f / 32767.0f; // 0.2747

// Direction from the engine's head/camera origin through the point the
// controller ray reaches at the configured crosshair distance. Keep this
// small calculation shared with its numeric fixture so the applied parallax
// target is proven to use the same-sample head supplied by the caller.
struct AimServoParallaxRay
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float distance = 0.0f;
};

inline AimServoParallaxRay AimServoParallaxRayFromHead(
    const float primaryAimPosition[3], const float primaryForward[3],
    const float headPosition[3], float crosshairDistance) noexcept
{
    AimServoParallaxRay result{
        primaryAimPosition[0] + primaryForward[0] * crosshairDistance -
            headPosition[0],
        primaryAimPosition[1] + primaryForward[1] * crosshairDistance -
            headPosition[1],
        primaryAimPosition[2] + primaryForward[2] * crosshairDistance -
            headPosition[2],
        0.0f};
    result.distance = sqrtf(result.x * result.x + result.y * result.y +
        result.z * result.z);
    if (result.distance > 1.0e-3f)
    {
        result.x /= result.distance;
        result.y /= result.distance;
        result.z /= result.distance;
    }
    return result;
}

struct AimServoAxis
{
    bool resting = false;
    bool haveLastAim = false;
    float lastAim = 0.0f;
    float minStep = 0.0f; // radians per frame at floor deflection
};

// Feeds the axis the engine's current aim (radians) and the magnitude of the
// command we issued for the frame that produced it. Call once per axis per
// frame, before AimServoCommand.
inline void AimServoObserve(AimServoAxis& axis, float aimRadians,
                            float previousCommandMagnitude)
{
    if (!std::isfinite(aimRadians))
    {
        axis.haveLastAim = false;
        return;
    }
    if (axis.haveLastAim && previousCommandMagnitude >= 1.0e-3f &&
        previousCommandMagnitude <= kAimServoStickFloor)
    {
        float step = aimRadians - axis.lastAim;
        // Wrap so a yaw axis crossing +/-pi does not report a full turn.
        while (step > 3.14159265f) step -= 6.28318531f;
        while (step < -3.14159265f) step += 6.28318531f;
        step = std::fabs(step);
        // Rise immediately, decay slowly: understating the quantum is what
        // leaves a residual cycle, while a sensitivity drop is still followed.
        axis.minStep = step > axis.minStep
            ? step
            : axis.minStep * 0.99f + step * 0.01f;
    }
    axis.lastAim = aimRadians;
    axis.haveLastAim = true;
}

// Returns the deflection for this axis, or exactly 0 while parked.
inline float AimServoCommand(AimServoAxis& axis, float error, float gain,
                             float restEnterRadians, float restExitRadians)
{
    if (!std::isfinite(error) || !std::isfinite(gain))
        return 0.0f;
    const float magnitude = std::fabs(error);
    const float quantumEnter = axis.minStep * 0.6f;
    const float quantumExit = axis.minStep * 1.5f;
    const float enter = restEnterRadians > quantumEnter
        ? restEnterRadians : quantumEnter;
    const float exit = restExitRadians > quantumExit
        ? restExitRadians : quantumExit;
    if (axis.resting)
    {
        if (magnitude < exit)
            return 0.0f;
        axis.resting = false;
    }
    else if (magnitude <= enter)
    {
        axis.resting = true;
        return 0.0f;
    }
    float command = error * gain;
    if (command > 1.0f) command = 1.0f;
    if (command < -1.0f) command = -1.0f;
    return command;
}

inline void AimServoReset(AimServoAxis& axis)
{
    axis = AimServoAxis{};
}

// A turret is worth parking within about a third of a degree of the hand: far
// inside the reticle at any crosshair distance, and far below what a player
// can perceive as the gun "not following". The exit band is three times that,
// and is widened further by the measured step.
inline constexpr float kAimServoRestEnterRadians = 0.0060f; // ~0.34 deg
inline constexpr float kAimServoRestExitRadians = 0.0180f;  // ~1.03 deg

// ---- 2. Honest reticle on a rate-limited barrel ---------------------------
//
// A turret's shots leave the BARREL, never the occupant's eye, so no origin
// substitution can make them follow a reticle the barrel is not pointing at.
// The only honest reticle for a turret is the one that rides the gun: the
// hand steers, the crosshair reports. That is unconditional in a turret seat
// rather than stall-gated, because a slow gun converges instead of stalling.
inline constexpr bool kAimServoTurretReticleRidesBarrel = true;
