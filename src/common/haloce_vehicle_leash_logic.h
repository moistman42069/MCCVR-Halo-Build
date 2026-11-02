#pragma once
// THE LEASH IN HALO CE ("leashMin" / "leashMax", vehicle_camera_frame::LeashToBasis): a chase camera's box the
// player's head stays inside. CE builds each eye from the frame's native camera plus the head's movement from the
// tracking reference (haloce_render_logic.h BuildEye), so the leash moves that reference, for this frame only, by
// whatever of the head's movement crosses a limit: past a limit the view stops following the head that way, the
// other axes untouched, and the eyes, the hands and the aim all keep agreeing because they are built from the same
// reference.
//
// Pure: no engine.

#include <algorithm>
#include <cmath>

#include "haloce_render_logic.h"
#include "vehicle_camera_frame_logic.h"

namespace haloce_vehicle_leash
{
// `reference` moved so the head's displacement from `native` (the camera's point), in world units, lies inside `box`
// along `carrier`. False (and `reference` untouched) when the head is inside the box, positional tracking is off, or
// an input is not usable.
inline bool LeashReference(const halo_ce::Camera& native, const halo_ce::Tracking& tracking, float unitsPerMeter,
                           bool positional, const vehicle_camera_frame::Basis& carrier,
                           const vehicle_camera_frame::LeashBox& box, halo_ce::Reference& reference) noexcept
{
    using namespace halo_ce;
    if (!positional || !Valid(native) || !Finite(tracking.headPosition) || !Finite(reference.position) ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0f || !vehicle_camera_frame::Usable(carrier))
        return false;
    Camera frame{};
    Quat inverse{};
    if (!BuildTrackingFrame(native, reference, frame, inverse))
        return false;
    // The head's displacement as BuildEye lays it into the world.
    Vec3 delta = tracking.headPosition - reference.position;
    delta = {std::clamp(delta.x, -4.0f, 4.0f), std::clamp(delta.y, -4.0f, 4.0f), std::clamp(delta.z, -4.0f, 4.0f)};
    const Vec3 world = ToNative(frame, Rotate(inverse, delta)) * unitsPerMeter;
    const float head[3]{world.x, world.y, world.z};
    float shift[3];
    vehicle_camera_frame::LeashShift(head, carrier, box, shift);
    if (shift[0] == 0.0f && shift[1] == 0.0f && shift[2] == 0.0f)
        return false;
    // Back into the tracking space: ToNative's inverse, then the frame's yaw turned back.
    const Vec3 s{shift[0] / unitsPerMeter, shift[1] / unitsPerMeter, shift[2] / unitsPerMeter};
    const Vec3 right = Cross(frame.forward, frame.up);
    const Vec3 local{Dot(s, right), Dot(s, frame.up), -Dot(s, frame.forward)};
    const Vec3 moved = reference.position + Rotate(Conjugate(inverse), local);
    if (!Finite(moved))
        return false;
    reference.position = moved;
    return true;
}
} // namespace haloce_vehicle_leash
