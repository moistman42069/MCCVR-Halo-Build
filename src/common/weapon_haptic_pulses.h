#pragma once
#include "runtime_types.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// XR-thread publication; native workers retain a token for the whole authored
// envelope. Tracking/focus/menu cancellation invalidates existing voices too.
class WeaponHapticAdmission
{
    std::atomic<uint64_t> gates_[2]{2,2},at_[2]{};
public:
    void Publish(int hand,bool available,uint64_t now) noexcept {
        if(hand<0||hand>1)return;
        if(!available) {
            if(gates_[hand].fetch_and(~uint64_t{1},std::memory_order_acq_rel)&1)
                gates_[hand].fetch_add(2,std::memory_order_release);
            at_[hand].store(0,std::memory_order_release);
        } else {
            const auto previous=at_[hand].load(std::memory_order_acquire);
            if(previous&&(now<previous||now-previous>100))Publish(hand,false,0);
            at_[hand].store(now,std::memory_order_release);
            gates_[hand].fetch_or(1,std::memory_order_release);
        }
    }
    void Invalidate() noexcept {Publish(0,false,0);Publish(1,false,0);}
    uint64_t Token(int hand,uint64_t now) const noexcept {
        if(hand<0||hand>1)return 0;
        const auto gate=gates_[hand].load(std::memory_order_acquire);
        const auto at=at_[hand].load(std::memory_order_acquire);
        return (gate&1)&&at&&now>=at&&now-at<=100&&
            gate==gates_[hand].load(std::memory_order_acquire)?gate:0;
    }
};

// One-shot native weapon envelopes. Separate title/generation banks prevent
// a retiring engine's callback from becoming another title's recoil. Channels
// are secondary, primary and primary's coupled support, never motor bands.
class WeaponHapticPulses
{
    struct Peak {
        std::atomic<uint64_t> revision{},value{},at{},source{},support{};
    } peaks_[7][3];
    // One bounded publication attempt, never a wait/spin lock. An in-flight
    // writer may outlive XR cancellation, so each coherent packet carries its
    // original admission epochs all the way to the consumer.
    static bool Reserve(Peak& peak,uint64_t& revision) noexcept {
        revision=peak.revision.load(std::memory_order_acquire);
        return !(revision&1)&&peak.revision.compare_exchange_strong(revision,revision+1,
            std::memory_order_acq_rel,std::memory_order_acquire);
    }
public:
    bool Raise(GameTitle title,uint32_t generation,int channel,float amplitude,uint64_t now,
        uint64_t sourceToken=0,uint64_t supportToken=0) noexcept
    {
        const unsigned slot=static_cast<unsigned>(title);
        if(!slot||slot>=7||!generation||channel<0||channel>=3||
           !std::isfinite(amplitude)||amplitude<0)return false;
        if(amplitude>1)amplitude=1;
        auto& peak=peaks_[slot][channel];
        uint64_t revision{};
        if(!Reserve(peak,revision))return false;
        const uint64_t before=peak.value.load(std::memory_order_relaxed);
        const uint64_t at=peak.at.load(std::memory_order_relaxed);
        const uint64_t previousSource=peak.source.load(std::memory_order_relaxed);
        const uint64_t previousSupport=peak.support.load(std::memory_order_relaxed);
        uint16_t value=uint16_t(amplitude*65535.0f+.5f);
        if(uint32_t(before>>32)>generation ||
            (uint32_t(before>>32)==generation&&sourceToken&&previousSource&&
             (sourceToken<previousSource||(sourceToken==previousSource&&supportToken<previousSupport)))) {
            peak.revision.store(revision+2,std::memory_order_release);return false;
        }
        if(uint32_t(before>>32)==generation&&now>=at&&now-at<=100&&
            previousSource==sourceToken&&previousSupport==supportToken)
            value=std::max(value,uint16_t(before));
        peak.value.store((uint64_t(generation)<<32)|value,std::memory_order_relaxed);
        peak.at.store(now,std::memory_order_relaxed);
        peak.source.store(sourceToken,std::memory_order_relaxed);
        peak.support.store(supportToken,std::memory_order_relaxed);
        peak.revision.store(revision+2,std::memory_order_release);return true;
    }
    float Read(GameTitle title,uint32_t generation,int channel,bool consume,uint64_t now,
        uint64_t sourceToken=0,uint64_t supportToken=0) noexcept
    {
        const unsigned slot=static_cast<unsigned>(title);
        if(!slot||slot>=7||!generation||channel<0||channel>=3)return 0;
        auto& peak=peaks_[slot][channel];
        uint64_t revision=peak.revision.load(std::memory_order_acquire);
        if(consume) {if(!Reserve(peak,revision))return 0;}
        else if(revision&1)return 0;
        const uint64_t at=peak.at.load(std::memory_order_relaxed);
        const uint64_t value=peak.value.load(std::memory_order_relaxed);
        const uint64_t source=peak.source.load(std::memory_order_relaxed);
        const uint64_t support=peak.support.load(std::memory_order_relaxed);
        bool valid=now>=at&&now-at<=100&&uint32_t(value>>32)==generation&&
            (!source||(source==sourceToken&&support==supportToken));
        if(consume) {
            // Consume even an obsolete epoch; a resumed hand must never pick
            // up a late writer's pre-cancellation cue.
            peak.value.store(value&~uint64_t{0xffff},std::memory_order_relaxed);
            peak.revision.store(revision+2,std::memory_order_release);
        } else {
            std::atomic_thread_fence(std::memory_order_acquire);
            valid=valid&&revision==peak.revision.load(std::memory_order_relaxed);
        }
        const float amplitude=valid?uint16_t(value)*(1.0f/65535.0f):0.0f;
        return amplitude;
    }
    void Clear() noexcept
    {
        for(auto& title:peaks_)for(auto& peak:title) {
            uint64_t revision{};
            if(!Reserve(peak,revision))continue;
            peak.value.store(0,std::memory_order_relaxed);peak.at.store(0,std::memory_order_relaxed);
            peak.source.store(0,std::memory_order_relaxed);peak.support.store(0,std::memory_order_relaxed);
            peak.revision.store(revision+2,std::memory_order_release);
        }
    }
};
