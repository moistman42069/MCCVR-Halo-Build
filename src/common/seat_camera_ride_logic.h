#pragma once
// A seat camera has to ride the vehicle that is DRAWN, not the one that is simulated.
//
// The simulation moves a vehicle one step per tick; the renderer draws it between the last two ticks (the frame
// interpolator). A camera point taken from the simulation's state sits at the newest tick, up to a whole tick's
// travel ahead of the hull on screen (12 cm at 7 m/s), and the gap changes with every frame's place between the
// ticks: the hull slides against the view, and the camera itself advances in whole ticks whatever the frame took.
//
// Given one node of the vehicle twice -- as simulated and as drawn this frame -- CarryPoint moves a point held in
// the simulated frame to where the drawn frame puts it: the same place on the hull, in the hull that is on screen.
// When the two frames are the same (the renderer is not interpolating the object) the point does not move.
//
// A frame is the engine's node matrix: scale, forward, left, up, position (13 floats).
#include <cmath>

namespace seat_camera_ride
{
constexpr int kFrameFloats = 13;
// Farther than this between the simulated and the drawn node is not one frame's interpolation (the engine itself
// stops interpolating an object that moved 30 world units in a tick); the point is left alone.
constexpr float kMaxCarryWu = 1.5f;

inline bool UsableFrame(const float m[kFrameFloats]) noexcept
{
    for (int i = 0; i < kFrameFloats; ++i)
        if (!std::isfinite(m[i]))
            return false;
    if (m[0] < 0.01f || m[0] > 100.0f)
        return false;
    const float* f = m + 1;
    const float* l = m + 4;
    const float* u = m + 7;
    const auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    return std::fabs(dot(f, f) - 1.0f) <= 0.05f && std::fabs(dot(l, l) - 1.0f) <= 0.05f &&
           std::fabs(dot(u, u) - 1.0f) <= 0.05f && std::fabs(dot(f, l)) <= 0.05f && std::fabs(dot(f, u)) <= 0.05f &&
           std::fabs(dot(l, u)) <= 0.05f;
}

// False (and the point untouched) unless both frames are usable, of one scale, and close enough to be one
// frame's interpolation of the same node.
inline bool CarryPoint(const float simulated[kFrameFloats], const float drawn[kFrameFloats], float point[3]) noexcept
{
    if (!UsableFrame(simulated) || !UsableFrame(drawn) || std::fabs(simulated[0] - drawn[0]) > 1.0e-3f * simulated[0])
        return false;
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2]))
        return false;
    const float d[3]{point[0] - simulated[10], point[1] - simulated[11], point[2] - simulated[12]};
    float local[3]{};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float* v = simulated + 1 + axis * 3;
        local[axis] = d[0] * v[0] + d[1] * v[1] + d[2] * v[2];
    }
    float carried[3]{drawn[10], drawn[11], drawn[12]};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float* v = drawn + 1 + axis * 3;
        for (int k = 0; k < 3; ++k)
            carried[k] += v[k] * local[axis];
    }
    const float move[3]{carried[0] - point[0], carried[1] - point[1], carried[2] - point[2]};
    const float moved2 = move[0] * move[0] + move[1] * move[1] + move[2] * move[2];
    if (!std::isfinite(moved2) || moved2 > kMaxCarryWu * kMaxCarryWu)
        return false;
    point[0] = carried[0];
    point[1] = carried[1];
    point[2] = carried[2];
    return true;
}
}
