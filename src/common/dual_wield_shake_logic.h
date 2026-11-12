#pragma once
#include "weapon_interaction_logic.h"
#include <array>

// Independent logical-hand gestures; native owner-bound requests are consumed
// only on each title's admitted simulation update.
namespace dual_wield_shake {
// Motion derivatives must use the timestamp at which the poses were located.
// GetTickCount64 may repeat at VR frame rates and would reset SingleNeedleShake.
// Keep wall-clock receipt expiry separate from this pose-domain clock.
inline uint64_t PoseMilliseconds(int64_t timeNs) noexcept
{return timeNs>0?uint64_t(timeNs)/1000000u:0;}

struct Sample {
    GameTitle title=GameTitle::None;
    uint32_t generation=0,unit=UINT32_MAX;
    uint64_t space=0,now=0;
    bool enabled=false,ready=false,dualWield=false,leftHanded=false;
    weapon_interaction::Vec head{};
    // Logical native weapon slots: primary and secondary. The input adapter
    // must map physical controllers through the current handedness first.
    std::array<weapon_interaction::Vec,2> hands{};
    std::array<uint32_t,2> weapons{UINT32_MAX,UINT32_MAX};
    std::array<bool,2> busy{};
    float travel=0.10f;
};
class State {
public:
    void Reset() noexcept { *this=State{}; }
    // One-shot slot bits. The input thread turns each into a press of that slot's reload
    // binding (shared input); never a simulation call or an ammo write.
    unsigned Update(const Sample& s) noexcept {
        const bool supported=s.title==GameTitle::Halo2||s.title==GameTitle::Halo3;
        if(!s.enabled||!s.ready||!s.dualWield||!supported||!s.generation||!s.space||!s.now||
            s.weapons[0]==UINT32_MAX||s.weapons[1]==UINT32_MAX||s.weapons[0]==s.weapons[1]||
            !weapon_interaction::Finite(s.head)) {Reset();return 0;}
        const bool changed=title_!=s.title||generation_!=s.generation||space_!=s.space||
            left_!=s.leftHanded||weapons_!=s.weapons||unit_!=s.unit;
        if(changed) {
            Reset();title_=s.title;generation_=s.generation;space_=s.space;
            left_=s.leftHanded;weapons_=s.weapons;unit_=s.unit;
        }
        unsigned requests=0;
        for(unsigned slot=0;slot<2;++slot) {
            if(s.busy[slot]) {shakes_[slot].Reset();continue;}
            if(shakes_[slot].Update(s.hands[slot]-s.head,s.now,s.travel)) requests|=1u<<slot;
        }
        return requests;
    }
private:
    std::array<weapon_interaction::SingleNeedleShake,2> shakes_{};
    GameTitle title_=GameTitle::None;
    uint32_t generation_=0,unit_=UINT32_MAX;
    uint64_t space_=0;
    bool left_=false;
    std::array<uint32_t,2> weapons_{UINT32_MAX,UINT32_MAX};
};

// The buttons one frame's shaken slots press: `reload` is the game's Reload binding, `secondaryReload` Halo 3's own
// left-weapon reload binding (0 where unbound or unread). Halo 2 has no left-gun reload control: its Reload starts
// both guns' reloads together (HALO2-DUAL-SHAKE-RELOAD-2026-11-06.md), so either hand's shake presses Reload there.
struct ReloadPress { uint32_t buttons=0; bool secondaryUnbound=false; };
inline ReloadPress ReloadButtons(GameTitle title,unsigned slots,uint32_t reload,uint32_t secondaryReload) noexcept
{
    ReloadPress press{};
    if(slots&1u) press.buttons|=reload;
    if(slots&2u) {
        const uint32_t left=title==GameTitle::Halo2?reload:secondaryReload;
        if(left) press.buttons|=left; else press.secondaryUnbound=true;
    }
    return press;
}
}
