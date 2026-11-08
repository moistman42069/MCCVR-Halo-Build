#pragma once
#include "config.h"
#include <iterator>

// THE DEFAULT CONTROL SCHEME: what a new player starts on and the built-in "Default" controls preset. The struct's own
// values are the scheme from before it, kept as the "Legacy Default" preset (the game's own layout, VR Mappings off), so a
// file that leaves a key out still means what it meant. Built for responsive zooming and few stick clicks: the support
// trigger zooms with the support hand on the gun, uses equipment with the grip closed on nothing, and throws a grenade
// otherwise; gesture melee only with the grip held.
namespace default_controls
{
inline constexpr const char* kDescription =
    "Built for responsive zooming and minimizing stick clicks via multiple inputs for certain actions. "
    "Be aware that gesture melee is on, but only engages when you punch a controller in a direction while holding "
    "its grip input. "
    "Trigger presses are contextual to your grip, zooming when you have your weapon support gripped, activating "
    "equipment when you are gripping with nothing in hand, and throwing grenades when you aren't gripping at all.";

// Turns `config`'s control scheme into the default one; every other setting is left alone.
inline void Apply(Config& config) noexcept
{
    using namespace vr_mapping;
    config.vr_action_mapping = true;
    config.gesture_melee = true;
    config.gesture_melee_require_grip = true;
    config.two_hand_toggle = false;
    config.weapon_holster_slide = false;
    config.weapon_holster_location = 2;
    for (auto& profile : config.title_profiles)
        profile.gun_pitch_deg = -13.0f;

    for (unsigned title = 0; title < std::size(config.vr_bindings); ++title)
    {
        const GameTitle game = static_cast<GameTitle>(title + 1);
        auto& rows = config.vr_bindings[title];
        auto& inputs = config.vr_inputs[title];
        inputs = DefaultInputs(game);
        rows = {};
        rows[Grenade] = SupportTrigger;
        rows[Melee] = RightClick;
        rows[Reload] = B;
        rows[Interact] = B;
        rows[SwitchGrenade] = LeftClick;
        rows[Equipment] = X;
        // The right stick's inputs are the rows' own: down toggles a crouch, and in Halo 4 up sprints.
        rows[Crouch] = RightStickDown;
        inputs[Crouch] = RowInputs{kNoneHeld, InputMode::Toggle, {}};
        if (game == GameTitle::Halo4)
        {
            rows[Sprint] = RightStickUp;
            inputs[Sprint] = RowInputs{};
        }
        // The support trigger with the support hand on the gun zooms; with the grip closed on nothing it uses equipment
        // (Halo 3's, Reach's and Halo 4's, which have equipment or an armor ability on it).
        rows[Zoom] = SupportTrigger;
        inputs[Zoom] = RowInputs{{HandOnGun, Unbound}, InputMode::Hold, {}};
        if (game == GameTitle::Halo3 || game == GameTitle::HaloReach)
            inputs[Equipment].more = {kStickUp, kAirEquipment, Input{}};
        else if (game == GameTitle::Halo4)
            inputs[Equipment].more = {kAirEquipment, Input{}, Input{}};

        // A seat's second trick or brake on the support grip, and the flying seats' Fly down on it too.
        auto& extra = config.vr_extra_bindings[title];
        extra = ExtraDefaults(game);
        const ExtraRow supportGrip{Input{kNoneHeld, SupportGrip}, Input{}, Input{}, Input{}};
        if (game != GameTitle::HaloCE)
            extra[VehicleTrick2] = supportGrip;
        if (game == GameTitle::Halo3 || game == GameTitle::Halo3ODST || game == GameTitle::HaloReach ||
            game == GameTitle::Halo4)
            extra[FlyDown] = supportGrip;
        if (game == GameTitle::Halo3)
            extra[DualWield] = ExtraRow{Input{kNoneHeld, Y}, Input{}, Input{}, Input{}};
    }
    config.vr_mod_bindings = kModDefaults;
}
}
