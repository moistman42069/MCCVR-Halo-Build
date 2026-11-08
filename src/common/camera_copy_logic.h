#pragma once

// A CAMERA COPY THAT IS NOT A VIEW (game.cpp CamCopyHook). Halo 3's camera copy runs once a frame for the player's view,
// and the mod takes the player's aim, head look, zoom and motion-tracker heading from it. After ODST's map screen on
// Project Reclaimer 0.10 the game went on copying two more cameras a frame (about 2,100 copies in 30 s became 6,000,
// 2026-11-07), one of them with no direction at all: the mod's aim read yaw 0 and pitch 0 from it for minutes, so
// walking turned with the gun instead of the head and the compass followed the gun. A view's forward is a unit vector
// that is never exactly the default +X; a copy whose forward is not one is left to the game.
// Pure logic; unit-tested in tests/core_tests.cpp.

#include <cmath>

namespace camera_copy
{
inline bool ForwardIsView(float x, float y, float z) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    const float lengthSquared = x * x + y * y + z * z;
    if (lengthSquared < 0.81f || lengthSquared > 1.21f)
        return false;
    return !(y == 0.0f && z == 0.0f);
}
} // namespace camera_copy
