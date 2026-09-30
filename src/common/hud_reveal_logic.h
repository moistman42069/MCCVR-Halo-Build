#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "runtime_types.h"

namespace hud_reveal {
// Deliberate, empty support hand near the headset. Never consumes a button or
// changes the saved Hide HUD preference. Tracking/menu/interaction interruptions
// require the hand to leave before another reveal can begin.
class Gesture {
    uint64_t last_=0, entered_=0, epoch_=0;
    float radius_=0;
    bool armed_=false, active_=false;
public:
    bool Update(uint64_t now,uint64_t epoch,bool ready,float distance,float radius) noexcept {
        if(!now||!epoch||!ready||!std::isfinite(distance)||distance<0||!std::isfinite(radius)) {
            last_=entered_=0;armed_=active_=false;return false;
        }
        radius=std::clamp(radius,.10f,.40f);
        if(epoch_!=epoch||radius_!=radius||!last_||now<last_||now-last_>100) {
            armed_=active_=false;entered_=0;
        }
        epoch_=epoch;radius_=radius;last_=now;
        if(distance>radius+.04f) {
            armed_=true;active_=false;entered_=0;return false;
        }
        if(!armed_) return false;
        if(active_) return true; // 4 cm release hysteresis
        if(distance>radius) {entered_=0;return false;}
        if(!entered_) entered_=now;
        active_=now-entered_>=200;
        return active_;
    }
};

// One atomic carries title, generation and low-clock timestamp. Consumers never
// wait on input and cannot revive a reveal after a stalled tracker or title hop.
class Publication {
    std::atomic<uint64_t> stamp_{0};
    static constexpr uint32_t kClockMask=(1u<<29)-1;
public:
    void Publish(bool active,GameTitle title,uint32_t generation,uint64_t now) noexcept {
        const auto id=static_cast<unsigned>(title);
        stamp_.store(active&&id>0&&id<7&&generation&&now?
            (uint64_t(generation)<<32)|(uint64_t(id)<<29)|(uint32_t(now)&kClockMask):0,
            std::memory_order_release);
    }
    bool Active(GameTitle title,uint32_t generation,uint64_t now) const noexcept {
        const uint64_t value=stamp_.load(std::memory_order_acquire);
        return value&&generation&&uint32_t(value>>32)==generation&&
            ((value>>29)&7)==static_cast<unsigned>(title)&&
            ((uint32_t(now)-uint32_t(value))&kClockMask)<=100;
    }
};
}
