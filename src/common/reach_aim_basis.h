#pragma once

// Reach <-> aim-solve core conversions, engine-free so a unit test can prove the frame
// (tests/aim_solve_tests.cpp). The core cannot validate its caller's basis (aim_solve_logic.h's
// PRECONDITION), so the mapping lives here rather than inline in src/dll/reach_aim_provider.cpp.
//
// Blam's world basis IS the core's basis -- Z up, x forward, yaw about +Z from +X -- so the mapping is
// units only: a Reach world unit is 10 ft (304.8 cm), and the game's angles are radians where the
// core's are degrees.

#include "aim_provider.h"
#include "reach_render_logic.h"   // kReachWorldUnitsPerMeter

#include <cmath>

namespace reach_aim_basis {

inline constexpr float kCmPerWorldUnit = 100.0f / kReachWorldUnitsPerMeter;   // 304.8
inline constexpr float kDegToRad = 0.017453292519943295f;

// A world-unit point or vector, in the core's centimetres.
inline aim_solve::Vec3 CmFromWorld(const float wu[3])
{
    return aim_solve::Vec3{wu[0] * kCmPerWorldUnit, wu[1] * kCmPerWorldUnit, wu[2] * kCmPerWorldUnit};
}

// `to - from` (world units), in the core's centimetres: the ray-origin-minus-engine-eye delta.
inline aim_solve::Vec3 DeltaCm(const float to[3], const float from[3])
{
    return aim_solve::Vec3{(to[0] - from[0]) * kCmPerWorldUnit, (to[1] - from[1]) * kCmPerWorldUnit,
                           (to[2] - from[2]) * kCmPerWorldUnit};
}

// The unit direction of a Reach yaw/pitch pair (radians), in the core's basis.
inline aim_solve::Vec3 DirFromGameAngles(float yawRad, float pitchRad)
{
    const float cp = std::cos(pitchRad);
    return aim_solve::Vec3{cp * std::cos(yawRad), cp * std::sin(yawRad), std::sin(pitchRad)};
}

// The core's converged setpoint (degrees) as the radians player_control.desired_angles holds.
inline void GameAnglesFromCore(const aim_solve::Angles& angles, float& yawRad, float& pitchRad)
{
    yawRad = angles.yaw_deg * kDegToRad;
    pitchRad = angles.pitch_deg * kDegToRad;
}

} // namespace reach_aim_basis
