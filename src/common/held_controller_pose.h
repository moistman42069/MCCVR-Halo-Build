#pragma once
#include <cmath>
#include <cstring>

// The last tracked pose of one controller, for the frames the runtime gives none: the arm solve keeps the hand
// where it was instead of handing the arm back to the game's own animation. The pose is in tracking space, so the
// body still carries the held hand; it is dropped when the weapon hand swaps, since it then names the other hand.
namespace held_controller_pose
{
struct Pose
{
    float orientation[4]{};
    float position[3]{};
};

class Hold
{
public:
    void Store(const float orientation[4], const float position[3], bool leftHanded) noexcept
    {
        for (int i = 0; i < 4; ++i) if (!std::isfinite(orientation[i])) return;
        for (int i = 0; i < 3; ++i) if (!std::isfinite(position[i])) return;
        std::memcpy(pose_.orientation, orientation, sizeof(pose_.orientation));
        std::memcpy(pose_.position, position, sizeof(pose_.position));
        leftHanded_ = leftHanded;
        valid_ = true;
    }
    bool Load(float orientation[4], float position[3], bool leftHanded) const noexcept
    {
        if (!valid_ || leftHanded != leftHanded_) return false;
        std::memcpy(orientation, pose_.orientation, sizeof(pose_.orientation));
        std::memcpy(position, pose_.position, sizeof(pose_.position));
        return true;
    }
    void Clear() noexcept { valid_ = false; }

private:
    Pose pose_{};
    bool leftHanded_ = false;
    bool valid_ = false;
};
}
