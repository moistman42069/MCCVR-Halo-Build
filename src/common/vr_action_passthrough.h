#pragma once
#include "vr_action_mapping.h"

// Native button passthrough beside the direct VR action bridge.
//
// Halo 2 hangs several native actions off its context button, and its own
// abstraction updater (halo2.dll +0x6D8AB0) times each of them from the real
// button's held milliseconds: a tap reloads, a hold picks up a weapon, picks
// up a dual wield, takes a weapon, enters, boards, flips or evicts from a
// vehicle. Those are separate native ids (reload 0x2E, touch_device 0x23,
// flip_vehicle 0x22, enter_vehicle 0x24, board_vehicle 0x25, evict 0x26,
// take_weapon 0x27, pickup_weapon 0x28, pickup_dual_wield 0x2A), each with its
// own player-control packet bit. The direct bridge can only name ONE id per
// semantic action, so routing Reload/Interact through it (0x2E/0x23) while
// withholding the real button removed every other context action: no weapon
// pick-up and no vehicle entry. For those actions the real native button is
// the only complete transport, so it keeps reaching the game and the bridge
// leaves the action alone.
//
// Every other title, and every other Halo 2 action, is unchanged.
namespace vr_mapping
{
inline constexpr uint32_t DirectPassthroughActions(GameTitle title) noexcept
{
    return title==GameTitle::Halo2 ? (1u<<Reload)|(1u<<Interact) : 0u;
}

struct DirectRoute
{
    uint32_t transports = 0;  // real native buttons the game must still see
    uint32_t bridgeMask = ~0u; // semantic actions the bridge may publish
};

// `actions` is the mapper's semantic output for this poll; `native` holds the
// REAL native transports read from the game's layout (never the bridge's
// gesture bits). An action with no native binding stays on the bridge, which
// is the only route that can deliver it at all. Outside direct routing the
// existing transport route already carries every action, so nothing changes.
inline DirectRoute SplitDirectRoute(GameTitle title,bool direct,uint32_t actions,
    const Transports& native) noexcept
{
    DirectRoute route{};
    if(!direct) return route;
    const uint32_t passthrough=DirectPassthroughActions(title);
    for(unsigned a=0;a<Count;++a)
    {
        const uint32_t bit=1u<<a;
        if(!(passthrough&bit)||!native[a]) continue;
        route.bridgeMask&=~bit;
        if(actions&bit) route.transports|=native[a];
    }
    return route;
}

// The whole per-poll decision both input routes use: which native buttons the
// game receives and which semantic actions the bridge publishes. `mapped` is
// the mapper output (its transports are the bridge's gesture bits in direct
// mode); `native` is the real layout.
struct RoutedActions
{
    uint32_t buttons = 0;   // XInput transports to OR into the game's pad
    uint32_t published = 0; // semantic actions for NativeVrActions_Publish
};
inline RoutedActions RouteActions(GameTitle title,bool direct,const MappedActions& mapped,
    const Transports& native) noexcept
{
    const DirectRoute route=SplitDirectRoute(title,direct,mapped.actions,native);
    return {direct?route.transports:mapped.transports,mapped.actions&route.bridgeMask};
}

// Halo 2's index-5 native action is touch_device, not a generic use/pick-up.
inline constexpr const char* ActionName(GameTitle title,Action action) noexcept
{
    if(action>=Count) return "";
    if(title==GameTitle::Halo2&&action==Interact) return "Use device / context action";
    return kNames[action];
}
}
