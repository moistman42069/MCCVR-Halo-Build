#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Tracking-space height only: native walking, slopes and moving platforms do
// not alter this reference. Recenter/enable samples the player's normal height.
class PhysicalCrouch
{
public:
    bool Update(float height,uint64_t epoch,uint64_t now,bool available,float depth) noexcept
    {
        const float threshold=std::isfinite(depth)?std::clamp(depth,.08f,.65f):.22f;
        if(!available||!std::isfinite(height)||!epoch||!now)
        {active_=false;suspended_=true;last_=0;return false;}
        if(!initialized_||epoch_!=epoch||threshold_!=threshold)
        {
            initialized_=true;epoch_=epoch;threshold_=threshold;baseline_=height;
            active_=false;suspended_=false;last_=now;return false;
        }
        if(last_&&(now<last_||now-last_>250)) {active_=false;suspended_=true;}
        last_=now;
        const float down=baseline_-height;
        const float release=threshold*.70f;
        // Resume must never replay a crouch that began in a menu or vehicle.
        // Stand up first; toggling/recentering explicitly calibrates anew.
        if(suspended_)
        {if(down<=release)suspended_=false;return false;}
        if(active_)
        {if(down<=release)active_=false;}
        else if(down>=threshold) active_=true;
        return active_;
    }
    void Reset() noexcept {*this=PhysicalCrouch{};}
private:
    uint64_t epoch_{},last_{};
    float baseline_{},threshold_{};
    bool initialized_{},active_{},suspended_{};
};
