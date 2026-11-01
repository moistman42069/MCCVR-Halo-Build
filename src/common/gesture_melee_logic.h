#pragma once
#include <cmath>
#include <cstdint>

// Telling a strike from a fast aim. Everything is measured relative to the head, which removes walking, turning and
// vehicle motion before any derivative is taken: those move hand and head together.
//   speed      a floor: the hand has to be moving fast.
//   extension  the discriminator: how fast the arm is reaching away from the head. A thrust grows it; an aim turn orbits
//              the body at constant radius and reads about zero.
//   reach      the hand must already be out, so a twitch near the chest is not a punch.
//   travel     a chop arcs around the shoulder and barely extends, so distance covered also counts, but only with the
//              grip held: without a closed fist, a long fast aim sweep looks exactly the same.
// A swing fires once, and the hand must slow down before it can fire again.
namespace gesture_melee
{
inline constexpr uint64_t kPulseMs = 120;
inline constexpr uint64_t kCooldownMs = 500; // the game's melee cannot be interrupted, so one swing is one press
inline constexpr float kRestSpeed = 0.35f;
inline constexpr float kReleaseRatio = 0.55f;
inline constexpr float kSmoothingMs = 20.0f;
// No arm does this: tracking jumped. The sample after a jump comes from the same bad position, so the history goes too.
inline constexpr float kMaxSpeed = 12.0f;
inline constexpr float kMaxReach = 1.2f;
inline constexpr float kMaxStepSeconds = 0.25f;

struct Settings
{
    float speed = 3.0f;
    float extension = 1.5f;
    float travel = 0.40f; // 0: extension only
    float reach = 0.30f;
    bool requireGrip = false;
};

class Detector
{
public:
    void Reset() noexcept { *this = Detector{}; }

    bool Step(bool valid, const float hand[3], const float head[3], bool gripHeld, float dtSeconds,
              const Settings& settings) noexcept
    {
        if (!valid || !(dtSeconds > 0.0f) || dtSeconds > kMaxStepSeconds)
        {
            Forget();
            return false;
        }
        const float rel[3]{hand[0] - head[0], hand[1] - head[1], hand[2] - head[2]};
        const float reach = Length(rel);
        if (!std::isfinite(reach) || reach > kMaxReach)
        {
            Forget();
            return false;
        }
        if (!have_)
        {
            for (int i = 0; i < 3; ++i) prev_[i] = rel[i];
            prevReach_ = reach;
            have_ = true;
            return false;
        }
        const float blend = 1.0f - std::exp(-dtSeconds * 1000.0f / kSmoothingMs);
        for (int i = 0; i < 3; ++i)
        {
            vel_[i] += ((rel[i] - prev_[i]) / dtSeconds - vel_[i]) * blend;
            prev_[i] = rel[i];
        }
        ext_ += ((reach - prevReach_) / dtSeconds - ext_) * blend;
        prevReach_ = reach;
        const float speed = Length(vel_);
        if (!std::isfinite(speed) || speed > kMaxSpeed)
        {
            Forget();
            return false;
        }

        float travelled = 0.0f;
        if (speed > kRestSpeed)
        {
            if (!inSwing_)
                for (int i = 0; i < 3; ++i) start_[i] = rel[i];
            inSwing_ = true;
            const float moved[3]{rel[0] - start_[0], rel[1] - start_[1], rel[2] - start_[2]};
            travelled = Length(moved);
        }
        else
            inSwing_ = false;

        if (speed <= settings.speed * kReleaseRatio)
            latched_ = false;
        if (latched_ || speed < settings.speed || reach < settings.reach)
            return false;
        if (settings.requireGrip && !gripHeld)
            return false;
        const bool chop = settings.travel > 0.0f && gripHeld && travelled >= settings.travel;
        if (ext_ < settings.extension && !chop)
            return false;
        latched_ = true;
        return true;
    }

private:
    static float Length(const float v[3]) noexcept { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
    void Forget() noexcept
    {
        const bool latched = latched_;
        *this = Detector{};
        latched_ = latched; // a tracking jump in the middle of a swing does not make it a second swing
    }
    float prev_[3]{}, vel_[3]{}, start_[3]{};
    float prevReach_ = 0.0f, ext_ = 0.0f;
    bool have_ = false, inSwing_ = false, latched_ = false;
};
}
