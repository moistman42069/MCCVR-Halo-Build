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

// One game's rows in the default scheme. Every row is on a named control or none, never Automatic: Automatic copies the
// player's flat MCC layout onto the VR controls, so two players' "default" would differ. Automatic is a player's own
// choice ("Follow my MCC layout"). Each control is where Automatic put the action on MCC's standard layout, so a player
// on that layout sees no change; the button pressed is still the game's own for the action, whatever the layout.
inline void Rows(GameTitle game, vr_mapping::Overrides& rows, vr_mapping::Inputs& inputs,
    vr_mapping::ExtraBindings& extra) noexcept
{
    using namespace vr_mapping;
    inputs = DefaultInputs(game);
    rows = {};
    rows[Fire] = PrimaryTrigger;
    rows[Grenade] = SupportTrigger;
    rows[Jump] = A;
    rows[Melee] = RightClick;
    rows[Reload] = B;
    rows[Interact] = B;
    rows[SwitchWeapon] = Y;
    rows[SwitchGrenade] = LeftClick;
    rows[Equipment] = X;
    // Halo 4's flashlight row is its d-pad left; ODST's is VISR, on X (ODST has no equipment).
    rows[Flashlight] = game == GameTitle::Halo4 ? DpadLeft : game == GameTitle::Halo3ODST ? X : DpadUp;
    rows[Sprint] = Unbound;
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

    // The extra rows, each named too. A row whose game has no such action, or whose standard button has no VR control
    // (Back's scoreboard; Halo 3's, ODST's, Halo CE's and Halo 2's seat switch on left Y, the vehicle cameras' there),
    // is on none. So is Fire left weapon: its game button is Grenade's left trigger, which Grenade's row already presses
    // (the left gun fires while dual wielding), and an extra row of its own on the support trigger pressed it under the
    // gun-gripped zoom chord too -- Halo 2's Aim Frame Zoom threw a grenade.
    const auto on = [](int press) { return ExtraRow{Input{kNoneHeld, press}, Input{}, Input{}, Input{}}; };
    const ExtraRow none = on(Unbound);
    extra = ExtraDefaults(game);
    for (auto& row : extra)
        if (row == kAutomaticRow) row = none;
    const auto has = [game](ExtraAction action) { return NativeExtraAction(game, action) != kNoAction; };
    // Halo 3's left gun reloads, and a second gun is picked up, on one button: left Y.
    if (has(ReloadLeft)) extra[ReloadLeft] = on(Y);
    if (game == GameTitle::Halo3) extra[DualWield] = on(Y);
    if (has(ActivateWaypoint)) extra[ActivateWaypoint] = on(DpadUp);
    if (has(VehicleSecondary)) extra[VehicleSecondary] = on(SupportTrigger);
    // A seat's second trick or brake on the support grip, and the flying seats' Fly up on A, Fly down on the grip.
    if (has(VehicleTrick2)) extra[VehicleTrick2] = on(SupportGrip);
    if (has(FlyUp)) extra[FlyUp] = on(A);
    if (has(FlyDown)) extra[FlyDown] = on(SupportGrip);
    if (has(SpecialAction)) extra[SpecialAction] = on(DpadUp);
}

// Turns `config`'s control scheme into the default one; every other setting is left alone.
inline void Apply(Config& config) noexcept
{
    config.vr_action_mapping = true;
    config.gesture_melee = true;
    config.gesture_melee_require_grip = true;
    config.two_hand_toggle = false;
    config.weapon_holster_slide = false;
    config.weapon_holster_location = 2;
    for (auto& profile : config.title_profiles)
        profile.gun_pitch_deg = -13.0f;
    for (unsigned title = 0; title < std::size(config.vr_bindings); ++title)
        Rows(static_cast<GameTitle>(title + 1), config.vr_bindings[title], config.vr_inputs[title],
            config.vr_extra_bindings[title]);
    config.vr_mod_bindings = vr_mapping::kModDefaults;
}

// Rows left on Automatic from a file written when the default scheme left them there (vr_inputs_version below 3): each
// takes the default's control. A player who chose Automatic since is moved too; the file cannot tell them apart.
// Returns the rows moved.
inline unsigned FillAutomatic(Config& config) noexcept
{
    using namespace vr_mapping;
    unsigned moved = 0;
    for (unsigned title = 0; title < std::size(config.vr_bindings); ++title)
    {
        Overrides rows{};
        Inputs inputs{};
        ExtraBindings extra{};
        Rows(static_cast<GameTitle>(title + 1), rows, inputs, extra);
        for (unsigned action = 0; action < Count; ++action)
            if (config.vr_bindings[title][action] == Automatic)
            {
                config.vr_bindings[title][action] = rows[action];
                config.vr_inputs[title][action].held = inputs[action].held;
                config.vr_inputs[title][action].mode = inputs[action].mode;
                ++moved;
            }
        for (unsigned action = 0; action < ExtraCount; ++action)
            if (config.vr_extra_bindings[title][action] == kAutomaticRow)
            {
                config.vr_extra_bindings[title][action] = extra[action];
                ++moved;
            }
    }
    return moved;
}
}
