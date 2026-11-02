#pragma once
#include <cmath>
#include <cstdint>

// Telling a strike from a fast aim. Everything is measured relative to the head, which removes walking, turning and
// vehicle motion before any derivative is taken: those move hand and head together.
//   speed      a floor: the hand has to be moving fast.
//   extension  the discriminator: how fast the arm is reaching away from the head. A thrust grows it; an aim turn orbits
//              the body at constant radius and reads about zero.
//   reach      the hand must already be out, so a twitch near the chest is not a punch.
//   travel     the off hand only: a chop arcs around the shoulder and barely extends, so distance covered also counts.
// A swing fires once, and the hand must slow down before it can fire again.
namespace gesture_melee
{
inline constexpr float kRestSpeed = 0.35f;
inline constexpr float kReleaseRatio = 0.55f;
inline constexpr float kMaxStepSeconds = 0.25f;

struct Settings
{
    float speed = 1.5f;
    float extension = 1.5f;
    float travel = 0.40f; // 0: extension only
    float reach = 0.30f;
    bool requireGrip = false;
    float smoothingMs = 20.0f;
    // No arm does this: tracking jumped. The sample after a jump comes from the same bad position, so the history goes.
    float maxSpeed = 12.0f;
    float maxReach = 1.2f;
};

struct Swing
{
    float direction[3]{}; // where the hand was going, in the tracking space the aim is in
    float speed = 0.0f, extension = 0.0f, reach = 0.0f, travel = 0.0f;
};

class Detector
{
public:
    void Reset() noexcept { *this = Detector{}; }

    // `standDown`: the hand is doing a job of its own. The motion is still measured, and nothing fires.
    bool Step(bool valid, const float hand[3], const float head[3], bool gripHeld, bool offHand, bool standDown,
              float dtSeconds, const Settings& settings, Swing* swing = nullptr) noexcept
    {
        if (!valid || !(dtSeconds > 0.0f) || dtSeconds > kMaxStepSeconds)
        {
            Forget();
            return false;
        }
        const float rel[3]{hand[0] - head[0], hand[1] - head[1], hand[2] - head[2]};
        const float reach = Length(rel);
        if (!std::isfinite(reach) || reach > settings.maxReach)
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
        const float blend = settings.smoothingMs > 0.0f ? 1.0f - std::exp(-dtSeconds * 1000.0f / settings.smoothingMs) : 1.0f;
        for (int i = 0; i < 3; ++i)
        {
            vel_[i] += ((rel[i] - prev_[i]) / dtSeconds - vel_[i]) * blend;
            prev_[i] = rel[i];
        }
        ext_ += ((reach - prevReach_) / dtSeconds - ext_) * blend;
        prevReach_ = reach;
        const float speed = Length(vel_);
        if (!std::isfinite(speed) || speed > settings.maxSpeed)
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
        if (latched_ || standDown || speed < settings.speed || reach < settings.reach)
            return false;
        if (settings.requireGrip && !gripHeld)
            return false;
        const bool chop = offHand && settings.travel > 0.0f && travelled >= settings.travel;
        if (ext_ < settings.extension && !chop)
            return false;
        latched_ = true;
        if (swing)
        {
            for (int i = 0; i < 3; ++i) swing->direction[i] = vel_[i] / speed;
            swing->speed = speed;
            swing->extension = ext_;
            swing->reach = reach;
            swing->travel = travelled;
        }
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

// Halo lunges along the aim, and during a swing the aim is the flailing hand, so a strike at something straight ahead
// lands wherever the hand happened to point. The aim follows the swing until `holdUntilMs`, then blends back to the live
// hand over `rampMs`, because the hand has travelled a long way by then and a handback would snap. Returns whether it
// changed `aim`. Yaw takes the short way round.
inline bool ApplyAimHold(float aim[3], const float swing[3], uint64_t nowMs, uint64_t holdUntilMs, uint64_t rampMs) noexcept
{
    if (!holdUntilMs || nowMs >= holdUntilMs + rampMs)
        return false;
    const float weight = nowMs <= holdUntilMs || !rampMs
        ? 1.0f
        : 1.0f - static_cast<float>(nowMs - holdUntilMs) / static_cast<float>(rampMs);
    constexpr float kPi = 3.14159265f;
    const auto pitchOf = [](const float v[3]) { return std::asin(std::fmax(-1.0f, std::fmin(1.0f, v[1]))); };
    const auto yawOf = [](const float v[3]) { return std::atan2(v[0], -v[2]); };
    float yawError = yawOf(swing) - yawOf(aim);
    while (yawError > kPi) yawError -= 2.0f * kPi;
    while (yawError < -kPi) yawError += 2.0f * kPi;
    const float yaw = yawOf(aim) + yawError * weight;
    const float pitch = pitchOf(aim) + (pitchOf(swing) - pitchOf(aim)) * weight;
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    aim[0] = std::sin(yaw) * std::cos(pitch);
    aim[1] = std::sin(pitch);
    aim[2] = -std::cos(yaw) * std::cos(pitch);
    return true;
}
}
