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
// leaves the action alone. Switch weapon is the same: a tap switches, and a
// hold of its real button picks up a second gun to dual wield.
//
// In a vehicle seat, Grenade's real button (the left trigger) reaches the game too: a seat throws no grenade, and the
// vehicle reads that button for itself -- Halo 4's boost is read for the seat's vehicle by the builder's ability helper
// (0xA21D0), not as the bridge's grenade.
//
// Reach's Jump reaches the game as its real button: MCC's jump-in-the-air-for-the-jetpack reads it outside the
// player-control builder the bridge serves.
//
// Every other title, and every other action, is unchanged.
namespace vr_mapping
{
inline constexpr uint32_t DirectPassthroughActions(GameTitle title,bool seated=false) noexcept
{
    return (title==GameTitle::Halo2 ? (1u<<Reload)|(1u<<Interact)|(1u<<SwitchWeapon) : 0u)|
        (title==GameTitle::HaloReach ? 1u<<Jump : 0u)|(seated ? 1u<<Grenade : 0u);
}

struct DirectRoute
{
    uint32_t transports = 0;  // real native buttons the game must still see
    uint32_t bridgeMask = ~0u; // semantic actions the bridge may publish
};

// The actions only the bridge can carry: one the game has no button for, or one whose button the game also gives another
// action that is on a different VR control (Reload and Use apart on one native button: the bridge's reason to be).
// Every other action's real button carries it whole, to every reader in the game -- not only the player-control builder
// the bridge serves, but Forge, a vehicle's boost, MCC's jetpack jump, Halo 2's held context buttons. An action blocked
// for gesture-only weapon input stays masked by the bridge at its reads, real button or not.
inline uint32_t BridgeOnlyActions(const Transports& native,const Overrides& bindings) noexcept
{
    uint32_t only=0;
    for(unsigned a=0;a<Count;++a)
    {
        if(!native[a]) {only|=1u<<a;continue;}
        const int source=Resolve(static_cast<Action>(a),bindings[a]);
        for(unsigned b=0;b<Count;++b)
            if(b!=a&&(native[b]&native[a])&&Resolve(static_cast<Action>(b),bindings[b])!=source) {only|=1u<<a;break;}
    }
    return only;
}

// The real button that carries each action whole. Halo 2's layout gives Use (touch_device) no button of its own: its
// updater derives it, as it derives picking up a weapon and boarding, from how long the reload/action button is held.
// With Use on Reload's VR control that real button carries it; the bridge's own touch_device on the same press kept a
// held B from picking up or boarding (Y, which the bridge does not carry, held fine). Use on a control of its own
// keeps the bridge, its only route then.
inline Transports CarryingTransports(GameTitle title,const Transports& native,const Overrides* bindings) noexcept
{
    Transports carrying=native;
    if(title==GameTitle::Halo2&&bindings&&!carrying[Interact]&&
       Resolve(Interact,(*bindings)[Interact])==Resolve(Reload,(*bindings)[Reload]))
        carrying[Interact]=carrying[Reload];
    return carrying;
}

// `actions` is the mapper's semantic output for this poll; `native` holds the
// REAL native transports read from the game's layout (never the bridge's
// gesture bits). An action with no native binding stays on the bridge, which
// is the only route that can deliver it at all. Outside direct routing the
// existing transport route already carries every action, so nothing changes.
// `bindings` (the effective rows) adds every action BridgeOnlyActions leaves to its real button.
inline DirectRoute SplitDirectRoute(GameTitle title,bool direct,uint32_t actions,
    const Transports& native,bool seated=false,const Overrides* bindings=nullptr) noexcept
{
    DirectRoute route{};
    if(!direct) return route;
    const Transports carrying=CarryingTransports(title,native,bindings);
    const uint32_t passthrough=DirectPassthroughActions(title,seated)|
        (bindings?~BridgeOnlyActions(carrying,*bindings):0u);
    for(unsigned a=0;a<Count;++a)
    {
        const uint32_t bit=1u<<a;
        if(!(passthrough&bit)||!carrying[a]) continue;
        route.bridgeMask&=~bit;
        if(actions&bit) route.transports|=carrying[a];
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
    const Transports& native,bool seated=false,const Overrides* bindings=nullptr) noexcept
{
    const DirectRoute route=SplitDirectRoute(title,direct,mapped.actions,native,seated,bindings);
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
