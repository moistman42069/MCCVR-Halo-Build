#pragma once
#include "vr_action_mapping.h"
namespace manual_weapon_input
{
inline uint32_t Blocked(bool gameplay,bool reload,bool holsters) noexcept
{
    return gameplay?((reload?(1u<<vr_mapping::Reload):0u)|
        (holsters?(1u<<vr_mapping::SwitchWeapon):0u)):0u;
}
// Gesture packets may use either a semantic bit or the configured native
// transport. Decode ONLY the gesture packet, never the physical pad state.
inline uint32_t Gestures(uint32_t buttons,uint32_t reload,uint32_t swap) noexcept
{
    uint32_t actions=buttons>>18;
    if(reload&&(buttons&reload))actions|=1u<<vr_mapping::Reload;
    if(swap&&(buttons&swap))actions|=1u<<vr_mapping::SwitchWeapon;
    return actions;
}
inline uint32_t Publish(uint32_t physical,uint32_t gestures,uint32_t blocked) noexcept
{return (physical&~blocked)|gestures;}
}
