#pragma once
#include "runtime_types.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace dual_wield_shake {
struct Owner {
    GameTitle title=GameTitle::None;
    uint32_t generation=0,unit=UINT32_MAX;
    std::array<uint32_t,2> weapons{UINT32_MAX,UINT32_MAX};
    uint64_t space=0;
    bool leftHanded=false;
    bool operator==(const Owner&) const = default;
    bool Valid() const noexcept {
        return (title==GameTitle::Halo2||title==GameTitle::Halo3)&&generation&&space&&
            unit!=UINT32_MAX&&weapons[0]!=UINT32_MAX&&weapons[1]!=UINT32_MAX&&weapons[0]!=weapons[1];
    }
};
// Try once, never spin or wait on the input/simulation threads. A missed poll
// is harmless; every receipt/request expires and carries both salted weapons.
class Channel {
public:
    unsigned ObserveAndConsume(const Owner& owner,uint64_t now) noexcept {
        if(busy_.test_and_set(std::memory_order_acquire))return 0;
        unsigned result=0;
        if(owner.Valid()&&owner_==owner)
            for(unsigned slot=0;slot<2;++slot) if(Fresh(now,requestAt_[slot]))result|=pending_&(1u<<slot);
        pending_=0;requestAt_={};owner_=owner;observedAt_=owner.Valid()?now:0;
        busy_.clear(std::memory_order_release);return result;
    }
    bool Read(uint64_t now,Owner& owner) noexcept {
        if(busy_.test_and_set(std::memory_order_acquire))return false;
        const bool ok=owner_.Valid()&&Fresh(now,observedAt_);
        if(ok)owner=owner_;
        busy_.clear(std::memory_order_release);return ok;
    }
    bool Request(const Owner& owner,uint64_t now,unsigned slots) noexcept {
        if(busy_.test_and_set(std::memory_order_acquire))return false;
        const bool ok=owner.Valid()&&owner_==owner&&Fresh(now,observedAt_)&&(slots&3u);
        if(ok){pending_|=slots&3u;for(unsigned slot=0;slot<2;++slot)if(slots&(1u<<slot))requestAt_[slot]=now;}
        busy_.clear(std::memory_order_release);return ok;
    }
    void CancelSlots(unsigned slots) noexcept {
        if(busy_.test_and_set(std::memory_order_acquire))return;
        pending_&=~slots;
        busy_.clear(std::memory_order_release);
    }
    void Cancel() noexcept {
        if(busy_.test_and_set(std::memory_order_acquire))return;
        pending_=0;requestAt_={};observedAt_=0;owner_={};
        busy_.clear(std::memory_order_release);
    }
private:
    static bool Fresh(uint64_t now,uint64_t at) noexcept {return at&&now>=at&&now-at<=150;}
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    Owner owner_{};
    uint64_t observedAt_=0;
    std::array<uint64_t,2> requestAt_{};
    unsigned pending_=0;
};
}
