#pragma once
#include <cstdint>
#include "vr_action_mapping.h"
#include "virtual_stock_settings.h"

struct ConfigVehicleModelTrim
{
    GameTitle title=GameTitle::None;
    uint64_t identity{};
    int16_t seat=-1;
    float value[3]{};
    bool set[3]{};
};
inline constexpr unsigned kVehicleModelTrimCapacity=128;

// Every supported MCC title shares one halomccvr.cfg next to the DLL, as plain
// "key = value" text. These are portable user preferences; title-specific
// camera, weapon, skeleton, and HUD calibration belongs in the title adapter.
// The in-headset menu edits the same values live and saves them back. Saving
// rewrites the whole file, so hand-written comments do not survive (values do).

// MCC's VR raster size at resolution_scale 1.00, and the range a hand-edited
// scale may take. The launcher turns these into -ResX/-ResY; the DLL upscales
// the finished eye into the unchanged full-size OpenXR projection. Both the
// launcher and the config clamp read these so the two can never disagree.
inline constexpr int kNativeRenderWidth = 2912;
inline constexpr int kNativeRenderHeight = 2100;
inline constexpr float kResolutionScaleMin = 0.35f;
inline constexpr float kPhysicalMeleeSpeedMin = 0.3f;
inline constexpr float kPhysicalMeleeSpeedMax = 10.0f;
inline constexpr float kPhysicalMeleeSpeedDefault = 5.0f;
// 2.75 renders ~8008x5775 (8K-class width). The "Keith David" F1 tier lands on
// true 8K width (7680, scale ~2.64); this ceiling leaves a little headroom above
// it. Everything is a uniform multiplier, so 2912:2100 (Halo's VR aspect) is
// preserved at every value. Above ~1.76 (5k-class) is extremely heavy; the F1
// menu warns there (see menu.cpp) but nothing is blocked.
inline constexpr float kResolutionScaleMax = 2.75f;
// F1/help "past this is very heavy" threshold: ~5k-class width (5120/2912).
inline constexpr float kResolutionScaleHeavy = 1.76f;
// Draw-distance trim: fraction of the engine's stock render far-clip distance
// (stock 10240 world units). 1.00 = full stock draw distance. The floor reaches
// deep enough to pull the far plane INTO the playable scene (~512 units), so low
// settings cull real terrain/objects, not just the distant skybox. Most levels
// only start visibly culling below ~0.25 because nearby geometry is closer than
// the far plane; the lowest settings clip near geometry (hard pop-in) for frames.
inline constexpr float kDrawDistanceMin = 0.05f;
inline constexpr float kDrawDistanceMax = 1.00f;
inline constexpr float kHudCurvatureMin = 0.00f;
inline constexpr float kHudCurvatureMax = 1.00f;
inline constexpr float kHudAspectMin = 0.50f;
inline constexpr float kHudAspectMax = 2.00f;
inline constexpr float kHudHeightMin = -300.0f;
inline constexpr float kHudHeightMax = 300.0f;

// Where the F1 menu panel sits in the headset, in meters. These four values are
// the ONLY description of the panel's placement: both the composition quad and
// the laser-pointer raycast read them, so the two can never disagree. Until
// 2026-07-29 each carried its own copy of the same literals (1.2 / 1.1 / -0.08),
// which meant moving the panel would have left the pointer behind.
inline constexpr float kMenuDistanceMin = 0.40f;
inline constexpr float kMenuDistanceMax = 6.00f;
inline constexpr float kMenuWidthMin = 0.40f;
inline constexpr float kMenuWidthMax = 4.00f;
// Lateral/vertical travel of the grab handle, measured from straight ahead.
inline constexpr float kMenuOffsetLimit = 3.00f;

// Per-SEAT seat-trim overrides (C13; C12 keyed these per vehicle, which the
// user rejected: "i need it per seat"). The vehicle axis is Halo3VehicleId - 1
// and the seat axis is the seat slot below. Config stays decoupled from the
// enum header, so the order here mirrors it by contract and game.cpp
// static_asserts the two never drift. "prowler" is the Mauler enum entry (its
// user-facing name); "turret" is every walk-up turret.
inline constexpr int kVehicleTrimCount = 10;
inline constexpr const char* kVehicleTrimNames[kVehicleTrimCount] = {
    "scorpion", "warthog", "mongoose", "ghost", "wraith",
    "prowler",  "banshee", "hornet",   "chopper", "turret"};

// Seat slots within one vehicle: the three authored passenger indices plus
// the mounted-turret gunner, which shares seat index 0 with the driver and so
// needs a slot of its own. Halo 3 authors no vehicle with more than three
// non-turret seats (kHalo3SeatPoints).
inline constexpr int kVehicleSeatSlots = 4;
inline constexpr int kVehicleGunnerSlot = 3;
inline constexpr const char* kVehicleSeatNames[kVehicleSeatSlots] = {
    "driver", "passenger", "passenger2", "gunner"};
inline constexpr int kVehicleTrimSlots = kVehicleTrimCount * kVehicleSeatSlots;

// ODST keeps its own bank of per-seat trims (user directive, 2026-08-03) under
// `_odst_`-prefixed keys, so tuning a warthog in ODST never moves the Halo 3
// warthog and vice versa. It needs a wider table than Halo 3's for two
// evidence-backed reasons (docs/ODST-VEHICLE-EVIDENCE.md O-E1): ODST clears the
// scorpion riders' "invalid for player" bit, so seat indices 1-4 are all
// enterable, and ODST adds the shade as a drivable vehicle Halo 3 never
// exposed. The Halo 3 names, slots and key spellings above are untouched, so no
// existing config line changes meaning.
inline constexpr int kOdstVehicleTrimCount = 11;
inline constexpr const char* kOdstVehicleTrimNames[kOdstVehicleTrimCount] = {
    "scorpion", "warthog", "mongoose", "ghost",   "wraith",  "prowler",
    "banshee",  "hornet",  "chopper",  "turret",  "shade"};
inline constexpr int kOdstVehicleSeatSlots = 6;
inline constexpr int kOdstVehicleGunnerSlot = 5;
inline constexpr const char*
    kOdstVehicleSeatNames[kOdstVehicleSeatSlots] = {
        "driver", "passenger", "passenger2", "passenger3", "passenger4",
        "gunner"};
inline constexpr int kOdstVehicleTrimSlots =
    kOdstVehicleTrimCount * kOdstVehicleSeatSlots;

// Reach keeps a third, independent trim bank under `_reach_`-prefixed keys.
// The vehicle order is the ReachVehicleId runtime contract. Every name below
// is a stable top-level vehicle identity present in the pinned official HREK
// tag tree. Attached child tags stay distinct when HREK gives them their own
// model/seat parent (Wraith gunner, Warthog weapons and Shade weapons); these
// are not guessed Halo 3 mounted-gunner variants. Falcon authors eleven seats,
// so the bank reserves sixteen raw indices without assigning roles to unused
// slots. Keep the keys role-neutral: seat 0 is not always a
// driver and later assigning a role must not change an existing config key.
// Rows 21-34 are the 2026-08-04 full official census
// (out/hrek-evidence/reach-vehicle-census/): every remaining player-usable
// HREK vehicle tag. Their slots ship unset, so they follow the universal trim
// until tuned live in F1. Rows 1-20 carry the user's completed 25-camera
// Blender lineup through kConfigReachShippedSeatTrims below. Existing keys keep
// their exact spelling.
// R-V25: row 35 is not an identity. Before it existed, sitting in a vehicle
// whose tuple did not resolve left the F1 sliders bound to the UNIVERSAL trim,
// so a user adjusting that seat silently rewrote the base every unadjusted
// Halo 3, ODST and Reach seat follows. The 2026-08-06 session did exactly that
// (universal 0.10/0.05 -> -0.765/0.894 while seated in an unmatched turret).
// Halo 3's contract is "sitting in a seat always adjusts that seat alone", so
// every unmatched Reach seat now shares this one row instead.
inline constexpr int kReachVehicleIdentityTrimCount = 34;
inline constexpr int kReachVehicleTrimCount =
    kReachVehicleIdentityTrimCount + 1;
inline constexpr int kReachUnmatchedVehicleTrimId = kReachVehicleTrimCount;
inline constexpr const char* kReachVehicleTrimNames[kReachVehicleTrimCount] = {
    "banshee", "space_banshee", "ghost", "revenant", "wraith",
    "wraith_gunner", "mongoose", "warthog", "warthog_chaingun",
    "warthog_gauss", "warthog_rocket", "falcon", "sabre", "scorpion",
    "forklift", "cart", "shade_plasma", "shade_flak", "plasma_turret",
    "machinegun", "mac_cannon", "scorpion_anti_infantry", "seraph",
    "pelican", "pelican_chin_gun", "corvette_cannon",
    "space_phantom_chin_gun", "space_phantom_beam_turret", "cargo_truck",
    "military_truck", "oni_van", "pickup", "truck_cab_large",
    "squad_drop_pod", "unmatched"};
inline constexpr int kReachVehicleSeatSlots = 16;
inline constexpr const char*
    kReachVehicleSeatNames[kReachVehicleSeatSlots] = {
        "seat0",  "seat1",  "seat2",  "seat3",
        "seat4",  "seat5",  "seat6",  "seat7",
        "seat8",  "seat9",  "seat10", "seat11",
        "seat12", "seat13", "seat14", "seat15"};
inline constexpr int kReachVehicleTrimSlots =
    kReachVehicleTrimCount * kReachVehicleSeatSlots;

// Vehicle placement controls are authored in metres, then converted to Halo
// world units at the same boundary as the Blender seat point. Forward/height
// have a little more travel than the original -0.5..1.0 sliders; lateral trim
// is symmetric because it moves across the width of the seat.
inline constexpr float kVehicleCamForwardMin = -1.00f;
inline constexpr float kVehicleCamForwardMax = 1.50f;
inline constexpr float kVehicleCamUpMin = -1.00f;
inline constexpr float kVehicleCamUpMax = 1.50f;
inline constexpr float kVehicleCamRightMin = -1.00f;
inline constexpr float kVehicleCamRightMax = 1.00f;
// The shipped universal trim. Named so the F1 panel can offer a one-click
// reset: this one triplet is shared by Halo 3, ODST and every Reach seat with
// no line of its own, so a wrong value here moves seats in all three titles.
inline constexpr float kVehicleCamForwardDefault = 0.10f;
inline constexpr float kVehicleCamUpDefault = 0.05f;
inline constexpr float kVehicleCamRightDefault = 0.0f;

// Reach's HREK marker seeds are not consistently authored at the player's
// eye. The validated 25-seat Blender lineup therefore contains legitimate
// marker-to-eye deltas much larger than the close-range trims used by Halo 3
// and ODST (the Sabre is the largest). Keep this title-specific so the
// established sliders and clamps for the other engines do not change.
inline constexpr float kReachVehicleCamForwardMin = -64.0f;
inline constexpr float kReachVehicleCamForwardMax = 64.0f;
inline constexpr float kReachVehicleCamUpMin = -16.0f;
inline constexpr float kReachVehicleCamUpMax = 16.0f;
inline constexpr float kReachVehicleCamRightMin = -16.0f;
inline constexpr float kReachVehicleCamRightMax = 16.0f;

// The storage slot for one seat, or -1 when the seat cannot be keyed (on
// foot, unidentified vehicle, or a seat index Halo 3 never authors).
inline constexpr int ConfigSeatTrimSlot(int vehicleId, int seatIndex,
                                        bool mountedTurret)
{
    const int v = vehicleId - 1;
    if (v < 0 || v >= kVehicleTrimCount)
        return -1;
    if (mountedTurret)
        return v * kVehicleSeatSlots + kVehicleGunnerSlot;
    // A rider seat may only take a rider slot: the gunner slot is reserved,
    // so a fourth authored seat keys nothing rather than silently sharing the
    // mounted gunner's trim.
    if (seatIndex < 0 || seatIndex >= kVehicleGunnerSlot)
        return -1;
    return v * kVehicleSeatSlots + seatIndex;
}

// The same mapping over ODST's wider table. `vehicleId` is the raw
// OdstVehicleId value (1 = scorpion .. 11 = shade), mirroring
// kOdstVehicleTrimNames by contract; game.cpp static_asserts the two.
inline constexpr int ConfigOdstSeatTrimSlot(int vehicleId, int seatIndex,
                                            bool mountedTurret)
{
    const int v = vehicleId - 1;
    if (v < 0 || v >= kOdstVehicleTrimCount)
        return -1;
    if (mountedTurret)
        return v * kOdstVehicleSeatSlots + kOdstVehicleGunnerSlot;
    if (seatIndex < 0 || seatIndex >= kOdstVehicleGunnerSlot)
        return -1;
    return v * kOdstVehicleSeatSlots + seatIndex;
}

// Reach indexes the adapter's stable HREK parent identity and seat index
// directly. Distinct child seat parents already have distinct identities, so
// no Halo 3-style synthetic mounted flag is needed here.
inline constexpr int ConfigReachSeatTrimSlot(int vehicleId, int seatIndex)
{
    const int v = vehicleId - 1;
    if (v < 0 || v >= kReachVehicleTrimCount ||
        seatIndex < 0 || seatIndex >= kReachVehicleSeatSlots)
        return -1;
    return v * kReachVehicleSeatSlots + seatIndex;
}

// Seat placements the maintainer tuned in the headset and which therefore ship
// as the product's own defaults, exactly as the released ZIP ships their live
// config. Written 2026-08-01 from the Game Pass session that accepted the
// first-person vehicle work. Config parsing performs no migration, so a user
// keeping an older file silently falls back to these built-ins — baking them in
// is what stops that fallback from being untuned.
//
// Each entry names its vehicle by Halo3VehicleId value (1 = scorpion .. 10 =
// turret, mirroring kVehicleTrimNames) and its seat by index, or by the mounted
// flag for a turret gunner. Only the axes the maintainer actually moved are
// marked set, so every other axis still follows the universal trim.
struct ConfigShippedSeatTrim
{
    int vehicleId;
    int seatIndex;
    bool mountedTurret;
    float forward;
    float up;
    float right;
    bool hasForward;
    bool hasUp;
    bool hasRight;
};

inline constexpr ConfigShippedSeatTrim kConfigShippedSeatTrims[] = {
    // vehicle, seat, mounted,  forward,  up,     right,  F,     U,     R
    {1, 0, false,  0.29f,  1.50f, -0.03f, true,  true,  true},  // scorpion driver
    {2, 0, false, -0.04f,  0.00f,  0.00f, true,  false, false}, // warthog driver
    {2, 1, false, -0.28f,  0.55f, -0.10f, true,  true,  true},  // warthog passenger
    {6, 0, true,   0.00f,  0.30f,  0.00f, false, true,  false}, // prowler gunner
    {8, 0, false, -0.23f, -0.09f, -0.03f, true,  true,  true},  // hornet driver
    {9, 0, false,  0.12f, -0.06f,  0.00f, true,  true,  false}, // chopper driver
};

// ODST's shipped bank. Seeded from the Halo 3 values above for every seat the
// tag diff proved geometrically identical (O-E1: same nodes, same markers
// within 1e-4 wu, same seat flags), so ODST feels right the first time it is
// driven instead of starting from an untuned universal trim. Tuning a seat in
// ODST afterwards writes only the ODST bank. Vehicle ids follow
// kOdstVehicleTrimNames (1 = scorpion .. 11 = shade); the ODST-only seats
// (scorpion riders, shade) ship unset and follow the universal trim until the
// headset says otherwise.
inline constexpr ConfigShippedSeatTrim kConfigOdstShippedSeatTrims[] = {
    // vehicle, seat, mounted,  forward,  up,     right,  F,     U,     R
    {1, 0, false,  0.29f,  1.50f, -0.03f, true,  true,  true},  // scorpion driver
    {2, 0, false, -0.04f,  0.00f,  0.00f, true,  false, false}, // warthog driver
    {2, 1, false, -0.28f,  0.55f, -0.10f, true,  true,  true},  // warthog passenger
    {6, 0, true,   0.00f,  0.30f,  0.00f, false, true,  false}, // prowler gunner
    {8, 0, false, -0.23f, -0.09f, -0.03f, true,  true,  true},  // hornet driver
    {9, 0, false,  0.12f, -0.06f,  0.00f, true,  true,  false}, // chopper driver
};

// The user's completed Reach Blender placement export, converted by the kit's
// documented authored = seed + {forward, -right, up} equation and persisted at
// halomccvr.cfg's two-decimal precision. These are user-authored,
// headset-unverified camera placements, not guessed HREK eye points.
// all_cameras_marked_placed is true
// for all 25 rows in reach_vehicle_camera_points.json, so all three axes are
// intentional (including zero) and ship set. Vehicle ids mirror
// kReachVehicleTrimNames (1 = banshee .. 20 = machinegun).
struct ConfigReachShippedSeatTrim
{
    int vehicleId;
    int seatIndex;
    float forward;
    float up;
    float right;
};

inline constexpr ConfigReachShippedSeatTrim kConfigReachShippedSeatTrims[] = {
    // vehicle, seat, forward, up, right
    { 1, 0,   1.94f, -1.17f,  0.00f}, // banshee
    { 2, 0,  11.65f, -3.07f,  0.00f}, // space_banshee
    { 3, 0,   0.00f,  1.13f,  0.00f}, // ghost
    { 4, 0,   0.48f,  1.12f,  0.00f}, // revenant
    { 4, 1,   0.00f,  0.35f, -0.73f}, // revenant
    { 5, 0,  -3.70f,  3.52f,  0.00f}, // wraith
    { 6, 0,   0.00f,  0.00f,  0.00f}, // wraith_gunner
    { 7, 0,   0.45f,  0.86f,  0.00f}, // mongoose
    { 7, 1,  -0.12f, -0.21f,  0.00f}, // mongoose
    { 8, 0,   0.13f,  0.79f,  0.00f}, // warthog
    { 8, 1,  -0.21f, -0.22f, -0.23f}, // warthog
    { 9, 0,  -1.29f,  0.72f,  0.00f}, // warthog_chaingun
    {10, 0,  -1.21f,  1.10f,  0.00f}, // warthog_gauss
    {11, 0,  -1.37f,  0.69f,  0.00f}, // warthog_rocket
    {12, 0,  -0.09f,  0.87f,  0.00f}, // falcon
    {12, 3,   0.00f,  0.24f,  0.21f}, // falcon
    {12, 4,  -0.06f,  0.33f,  0.00f}, // falcon
    {13, 0,  42.43f, -8.18f,  0.00f}, // sabre
    {14, 0,   0.00f,  2.69f,  0.00f}, // scorpion
    {15, 0,   0.33f,  0.49f,  0.00f}, // forklift
    {16, 0,   0.00f,  0.00f,  0.00f}, // cart
    {17, 0,   0.00f,  2.41f,  0.00f}, // shade_plasma
    {18, 0,   0.11f,  2.27f,  0.00f}, // shade_flak
    {19, 0,   0.00f,  0.24f,  0.00f}, // plasma_turret
    {20, 0,   0.00f,  0.26f,  0.00f}, // machinegun
};
inline constexpr int kConfigReachShippedSeatTrimCount =
    sizeof(kConfigReachShippedSeatTrims) /
    sizeof(kConfigReachShippedSeatTrims[0]);

// Seats named by the vehicle's stable model identity (ConfigVehicleModelTrim): the games whose seats have no
// named bank above. A row is that seat's built-in camera offset from its occupant's head, in metres, all three
// axes set. The seat's sliders start from it and its reset returns to it.
//
// Halo 4's Mantis. The pilot's head is inside a closed cockpit: a camera there sees the cockpit's walls and the
// dashboard, and nothing of the world (the game's own camera for the seat is third person). The cockpit roof is
// cleared at about 0.4 m above the head. The row puts the eye above the roof and a little forward, with the
// roof and both weapon arms below it. Seen headless on one multiplayer map; not seen in a headset.
struct ConfigShippedVehicleModelTrim
{
    GameTitle title;
    uint64_t identity;
    int16_t seat;
    float forward;
    float up;
    float right;
    const char* name;
};

inline constexpr ConfigShippedVehicleModelTrim kConfigShippedVehicleModelTrims[] = {
    // game,            model identity,        seat, forward, up,    right, name
    {GameTitle::Halo4, 0x000000551b05181eull, 0,    0.30f,   0.80f, 0.00f, "Mantis"},
};

inline const ConfigShippedVehicleModelTrim* ConfigShippedVehicleModelTrimFor(
    GameTitle title, uint64_t identity, int seat)
{
    for (const ConfigShippedVehicleModelTrim& trim : kConfigShippedVehicleModelTrims)
        if (trim.title == title && trim.identity == identity && trim.seat == seat)
            return &trim;
    return nullptr;
}

// C-TITLE-1: the weapon/hand/HUD/crosshair tunables every title keeps its
// own copy of - one struct so a profile is copied whole. Defaults mirror
// the shared fields' defaults so an untouched profile behaves identically.
inline constexpr int kTitleProfileCount = 7;
// Reach's slot among the profiles (cfg prefix reach_). The crosshair's three tunables and the HUD layout's four
// follow and edit this profile while no game is running, so the menu always shows one game's own values.
inline constexpr int kTitleProfileReach = 2;
inline constexpr int kTitleProfileHalo4 = 3;
// Each game draws its own crosshair art, which fills more or less of the crosshair's quad than the next game's
// (Reach's ring about half as much as Halo 3's), so one size shows it larger in one game than in another. These
// are each game's starting size and opacity, headset-tuned so the games' crosshairs look alike; the shared
// defaults are TitleTunables' (4 degrees, 1.25). A profile that names no size or opacity starts at its game's
// own start where that differs from the shared default, and from the shared key otherwise: a config written
// before the games had their own holds one size for every game (10.1 degrees by default in older builds), and
// no multiple of one size suits them all.
struct CrosshairStart
{
    float size_deg;
    float opacity;
};
inline constexpr CrosshairStart kCrosshairStarts[kTitleProfileCount] = {
    {6.00f, 1.25f},  // Halo 3
    {6.00f, 1.25f},  // Halo 3: ODST
    {10.30f, 0.69f}, // Halo: Reach
    {4.00f, 1.25f},  // Halo 4
    {8.00f, 1.25f},  // Halo 2 Anniversary
    {8.00f, 1.25f},  // Halo 2 Classic
    {7.20f, 1.25f},  // Halo: Combat Evolved
};
// Each game's starting HUD layout (size, width / aspect, curvature, height), the same way: where it differs from
// TitleTunables' shared default, a profile that names none starts at its game's own. Every game's is the tester's,
// headset-tuned (Halo 2's Anniversary layout used for Classic too): laid out narrower or smaller, and well below the
// shared height.
struct HudStart
{
    float size;
    float aspect;
    float curvature;
    float vertical_offset;
};
inline constexpr HudStart kHudStarts[kTitleProfileCount] = {
    {0.40f, 1.22f, 0.48f, -200.0f}, // Halo 3
    {0.46f, 1.22f, 0.48f, -150.0f}, // Halo 3: ODST
    {0.45f, 0.80f, 0.48f, -202.0f}, // Halo: Reach
    {0.41f, 1.00f, 0.48f, -117.0f}, // Halo 4
    {0.34f, 1.00f, 0.48f, -200.0f}, // Halo 2 Anniversary
    {0.34f, 1.00f, 0.48f, -200.0f}, // Halo 2 Classic
    {0.50f, 0.75f, 0.48f, -125.0f}, // Halo: Combat Evolved
};

struct TitleTunables
{
    float world_scale = .33f;
    float gun_scale = 0.96f;
    float left_hand_scale = 0.96f;
    // Visible hand meshes only, in each hand's local frame (metres).
    float left_hand_mesh_x_m = 0.0f;
    float left_hand_mesh_y_m = 0.0f;
    float left_hand_mesh_z_m = 0.0f;
    float right_hand_mesh_x_m = 0.0f;
    float right_hand_mesh_y_m = 0.0f;
    float right_hand_mesh_z_m = 0.0f;

    float gun_pitch_deg = -3.0f;
    float gun_yaw_deg = 0.0f;
    float gun_roll_deg = 0.0f;
    float gun_forward_m = -0.14f;
    float gun_right_m = 0.0f;
    float gun_up_m = 0.0f;
    float left_gun_forward_m = -0.16f;
    float left_gun_right_m = 0.04f;
    float left_gun_up_m = -0.06f;
    float left_hand_forward_m = 0.0f;
    // The HUD layout. With no game running these four follow and edit Reach's profile rather than the shared
    // defaults (kTitleProfileReach), like the crosshair's below.
    float hud_size = 0.43f;
    float hud_aspect = 1.22f;
    float hud_curvature = 0.48f;
    float hud_vertical_offset = 16.0f;
    // Mesh-only barrel trim: rotates the visible gun (and the hands on it)
    // about the controller WITHOUT moving the crosshair or the shot ray.
    // Per game, like everything else here. 0 = untouched.
    float barrel_pitch_deg = 0.0f;
    float barrel_yaw_deg = 0.0f;
    float barrel_roll_deg = 0.0f;
    // The crosshair's size, opacity and brightness (Config::crosshair_size_deg, _opacity, _brightness). Each game
    // draws its own crosshair art, larger, fainter or brighter than the next game's. With no game running these
    // three follow and edit Reach's profile rather than the shared defaults (kTitleProfileReach).
    float crosshair_size_deg = 4.00f;
    float crosshair_opacity = 1.25f;
    float crosshair_brightness = 1.25f;
    // Whole-game brightness is stored independently for each title.
    float game_brightness = 1.11f;
};

struct Config
{
    float world_scale = .33f;
    // Stamps the Halo 3, ODST and Reach shipped seat tables into their
    // independent per-seat arrays. Every other member keeps its default member
    // initializer, so Config{} remains the one description of a fresh
    // configuration.
    Config();

    int config_version = 7;

    // Portable OpenXR feedback and pose stabilization. Headset smoothing is a
    // deliberately tiny previous-frame blend (0.03 shipped default, 10% hard maximum)
    // so users may remove micro-jitter without turning head motion into a laggy camera.
    // Aim stabilization affects only the floating VR crosshair; weapon aim and
    // bullets continue to use the current controller pose (the steadied one when
    // hand_smoothing is on).
    float haptic_intensity = 0.86f;
    float headset_smoothing = 0.03f;
    float aim_stabilization = 0.48f;
    // HAND SMOOTHING (hand_smoothing.h; ported from the Halo: Campaign Evolved VR mod; its defaults but for the
    // two steadiness cutoffs: 1.3 Hz here, 1.0 Hz there). A 1 Euro
    // filter on the tracked controllers where their poses enter the mod, so aim and shots, the arms, two-handed
    // holds, holsters, reload and the menu pointer all read the same steadied hands. A still hand is held
    // steady; a fast move passes through with little lag. Swing melee and arm-swing running read the raw
    // controller velocities, as before. The headset is not touched (headset_smoothing is its own).
    //   hand_smoothing            1 = on, 0 = the hands exactly as the headset tracks them.
    //   hand_smoothing_hands      0 both, 1 only the hand that aims, 2 only the other hand.
    //   ..._pos_min_hz / ..._rot_min_hz   steadiness while still: lower is steadier and slower to follow a slow
    //                             move. 0 = that half is not smoothed.
    //   ..._pos_beta / ..._rot_beta       how quickly the smoothing lets go as the hand moves (per m/s) or
    //                             turns (per rad/s) faster.
    //   ..._speed_cutoff_hz       smoothing of the speed estimate itself; rarely needs changing.
    // All read live.
    bool hand_smoothing = true;
    int hand_smoothing_hands = 0;
    float hand_smoothing_pos_min_hz = 1.3f;
    float hand_smoothing_pos_beta = 500.0f;
    float hand_smoothing_rot_min_hz = 1.3f;
    float hand_smoothing_rot_beta = 5.0f;
    float hand_smoothing_speed_cutoff_hz = 10.0f;

    float screen_width_m = 4.0f;    // width of the virtual screen, in meters
    float screen_distance_m = 2.4f; // how far away the screen floats, in meters

    // MONO RENDERING (mono_view_logic.h; every title, Halo CE on its Classic graphics only): 1 = the world is drawn
    // once a frame, from between the eyes, and shown to both: no depth between the eyes, one pass of drawing instead
    // of two. For maps with so much in view that two passes do not fit the headset's frame. Refused, with a log
    // line, on canted displays, in the cutscene theatre, while DLSS is on, and in a title whose renderer turns
    // single passes down. Halo CE's Anniversary graphics have no single-pass path and are drawn in stereo; Halo 2
    // waits until it has learned where the game leaves each eye's picture, which compares two eyes. Followed live.
    // (default 0)
    bool mono_rendering = false;
    // HOOK COST (hook_cost_logic.h). N = one presented frame in N is timed: every Direct3D call the mod hooks
    // reads the cycle counter around the mod's own work and around the game's real call, and every ten seconds a
    // `timing:` line gives the mod's own work a frame, by call. The timed frame itself is slower by the timer's
    // readings (milliseconds in a scene with much in it). 0 = off; then a hook pays one flag test. Least 8.
    // Followed live. (default 0)
    int hook_cost_frames = 0;

    // A host that draws its own UI over the game's frame (Project Reclaimer; reclaimer_host_logic.h, "the host's UI
    // as a panel"): 1 = that UI is shown as a panel in front of the stereo picture, where the flat screen would
    // be; 0 = the picture goes flat while one of the host's screens is up, and its overlays during play are not
    // seen. Nothing in MCC. Followed live.
    bool host_ui_panel = true;
    float host_ui_panel_width_m = 2.8f;    // the panel's width, in meters (16:9 at 2.4 m: about 60 degrees across)
    float host_ui_panel_distance_m = 2.4f; // how far in front of the eyes it floats, in meters
    // Project Reclaimer only: how its Hornet-class flyers (a Sparrowhawk, Falcon or Vulture on a mod map too)
    // change height. Halo 3 reads that from a player preference MCC fills in and Reclaimer leaves at Halo 3's
    // default, classic (aim up or down to climb or dive; the buttons do nothing). 1 = MCC's Ascend and Descend
    // buttons (right A and the left grip); 0 = classic. Nothing in MCC. Followed live. (reclaimer_flight.inl)
    bool reclaimer_flight_buttons = true;

    // Universal cutscene-only stereo theatre. A title enters it only after its
    // adapter proves the engine owns an authored camera and player camera
    // control is unavailable. Future title adapters inherit the same settings.
    bool cutscene_theater_enabled = true;
    float cutscene_theater_depth = 1.0f; // 0 flat, 1 runtime IPD, 2 double
    bool cutscene_theater_flip_depth = false;
    float cutscene_theater_width_m = 6.0f;
    float cutscene_theater_distance_m = 4.0f;
    // Black cine bars over the parts of the authored frame a monitor never
    // shows. The cutscene is rasterized into the headset's render shape, which
    // is taller than 16:9, so without them the theatre exposes scene above and
    // below the intended shot. 0 disables the bars.
    float cutscene_theater_matte_aspect = 16.0f / 9.0f;
    // Slides the retained window up (positive) or down inside that frame, as a
    // fraction of the frame height. The window never changes size.
    float cutscene_theater_matte_offset = 0.0f;
    // Put the title's own subtitle text back on the theatre screen. Subtitles
    // are interface text drawn after both eye captures, so they reach the
    // monitor and never the headset without this.
    bool cutscene_theater_subtitles = true;
    // How much of the bottom of the title's finished frame is searched for that
    // text, as a fraction of its height. Text drawn above this is not found.
    float cutscene_theater_subtitle_band = 0.30f;
    // Diagnostic: paint that strip onto the theatre screen exactly as captured,
    // with no glyph selection at all, to show what the mod is actually reading.
    bool cutscene_theater_subtitle_debug = false;

    // F1 menu panel placement. The panel is its own composition quad, separate
    // from the virtual screen above. The defaults are the fixed constants the
    // panel used before it became movable. The grab handle at the top of the
    // panel writes these live and saves on release; "Reset panel position" in
    // the Advanced category puts them back.
    float menu_distance_m = 1.2f;   // how far in front of you the panel floats
    float menu_width_m = 1.1f;      // panel width; height follows the 4:3 texture
    float menu_height_m = -0.08f;   // vertical offset, negative = below eye line
    float menu_side_m = 0.0f;       // sideways offset, positive = to your right
    bool game_menu_pointer = true; // controller mouse in native menus
    // OFXR Bridge, an OpenXR frame-generation layer, has crashed the mod; off, the mod sets OFXR's own off switch
    // (XRFG_DISABLE_OFXR_BRIDGE) in the game's process before OpenXR starts, so OFXR stays out of this game only.
    bool allow_ofxr_bridge = false;
    // Project Reclaimer: its Native render resolution is never given a size larger than the monitor
    // (reclaimer_host::AnswerNativeRenderSize). A picture larger than the screen is suspected of stopping the graphics
    // card on some PCs; off gives it the Picture panel's full size. Exclusive fullscreen always gets the real size.
    // From 0.10, which sizes its picture from its window, the picture keeps the Picture panel's size and the window is
    // fitted inside the monitor instead (d3d11_hook.cpp, D3D_HostFitWindow); off leaves the window that size.
    bool reclaimer_fit_to_display = true;
    // Project Reclaimer: a title it starts after the first (ODST, after Halo 3's menu) gets the headset session's D3D11
    // device instead of a new one, which the headset could not show (present_owner_logic.h DecideDeviceShare).
    // Off: that title freezes in the headset, as before.
    bool reclaimer_share_device = true;

    // Show the welcome page automatically, once, at the start of each launch.
    // The mod has no other way to tell a player anything before they play: the
    // README is outside the game and this file's comments are only read by
    // people who open it. Ticking "Don't show this again" on that page clears
    // this. It suppresses only the AUTOMATIC appearance -- the Welcome page
    // stays in the F1 menu so the message can be read again at any time.
    bool show_welcome = true;

    // M3 VR controller turning (right Sense stick).
    bool roomscale_movement = true; // physical horizontal steps drive native walking
    // Roomscale A/B, live from F1: 1 = hysteresis (3 cm start / 1.5 cm stop, brake by release, owed distance
    // kept through interruptions), 0 = threshold (walks from 1.5 cm,
    // brakes by reversing), verbatim as it shipped in Alpha 0.6.0. (default 1)
    int roomscale_follow_law = 1;
    // ROOMSCALE VIEW RECENTER (roomscale_recenter_logic.h). In the air and while the stick moves the
    // character, the body cannot be walked under the head, so the view sits away from the game's own
    // camera -- the eye the shot is solved from. Each option slides the play area (position only,
    // horizontal: never a turn, never the height) so the view comes back over the character while that
    // motion hides it; what is left afterwards the body follow walks as before. All on by default.
    // They need roomscale_movement.
    bool roomscale_recenter_jump = true;           // a quick linear blend when a jump leaves the ground
    float roomscale_recenter_jump_s = 0.20f;       // its duration, seconds (0.05 to 1.0)
    bool roomscale_recenter_move_start = true;     // the same blend when the movement stick is first pushed
    float roomscale_recenter_move_start_s = 0.20f; // its duration, seconds (0.05 to 1.0)
    bool roomscale_recenter_moving = true;         // while the stick is held: close the offset at a constant speed
    float roomscale_recenter_moving_speed = 0.25f; // metres of offset closed per second (0.1 to 3.0)
    bool physical_running = false;
    float physical_running_speed = 1.0f; // fraction of native maximum locomotion
    float physical_running_sensitivity = 1.0f;
    // Lowering the head by the depth crouches, on foot. It holds MCC's crouch while the head is down, so it
    // needs MCC's hold-to-crouch setting: toggle crouch is not overridden.
    bool physical_crouch = true;
    // The right stick's crouch and sprint, now inputs on the rows (vr_mapping::kStickCrouch, kStickUp): read only from a
    // file or preset written before them, to convert.
    bool right_stick_crouch = true;
    float physical_crouch_depth_m = 0.22f;
    // AUTOMATED TESTING ONLY (default 0): let roomscale and physical crouch accept a head pose OpenXR reports
    // VALID but not TRACKED. A simulated runtime can report an overridden head exactly that way, so
    // without this neither feature can be exercised without a headset. Never enable it in a
    // headset: an untracked (inertial) head is precisely what both features must ignore.
    bool test_accept_untracked_head = false;
    // THE LEASH BELOW THE CHARACTER'S EYE (Halo: Campaign Evolved's heightleash; on foot in Reach, Halo 3, ODST, Halo 4 and Halo 2): the
    // most the view may drop below the game camera -- the character's own eye -- so a deep physical
    // duck cannot take it inside his chest and legs. Game-space metres. It rides the camera: when the
    // character crouches his eye drops, and the limit drops with him. The hands and the reticle ray are
    // lifted with the view, so holding the view does not make them sink away from your eyes. <0 = off.
    float head_leash_below_eye_m = 0.10f;
    // THE VIEW'S NEUTRAL HEIGHT FOLLOWS YOUR STANDING HEIGHT UP (standing_height_follow.h). With the physical
    // crouch on, a recenter that catches the player low before their standing height is known (the automatic
    // one at a level's start, for instance) leaves them standing that much above their character.
    // 1 = once the standing height is confirmed, the neutral height glides up to it (Halo 3, ODST, Reach and
    // Halo 4; on foot). 0 = it stays where the recenter put it until the next recenter. Read live.
    bool view_follows_standing_height = true;
    // HEAD-RELATIVE HUD AND SOUND (head_relative_logic.h). The player aims with the hand and looks with the
    // head, so the game's own camera heading -- the aim -- is the wrong one for anything read against where
    // the player LOOKS. tracker_follows_head: the motion tracker places its contacts by the head's heading
    // (yaw only) instead of the aim's, so sweeping the gun moves nothing and turning the head turns them.
    // audio_follows_head: the sound listener takes the head's orientation. Both are read live. A title that
    // has not bound its seam keeps the game's own behaviour (log: TRACKER HEAD / AUDIO HEAD <title>).
    bool tracker_follows_head = true;
    bool audio_follows_head = true;
    // THE MOTION TRACKER'S BLIPS MOVE WITH THE HUD HEIGHT (halo3_tracker_hud_height_logic.h). Halo 3's HUD height
    // raises the tracker's art (by hud_vertical_offset, like every HUD element) and cannot reach the blips,
    // which the game draws around a centre it keeps elsewhere: every dot, the player's own included, sat below
    // the tracker (a quarter of its radius at the default height). 1 = for the length of the blip draw that
    // centre gets the art's move. 0 = the game's own centre. Read live. Halo 3 only. A title that has not
    // bound this keeps its own behaviour (log: TRACKER HUD HEIGHT <title>).
    bool tracker_blips_follow_hud_height = true;
    // MCC'S RELOAD OF A GAME DLL WAITS FOR OUR TEARDOWN (host_unload_gate.cpp). A game's DLL unloads when the
    // game ends and MCC loads a fresh one for its menus. A title core that keeps its own reference on that DLL
    // while its hooks are in it (Halo: Reach) keeps it loaded past that reload, and the next game of the title,
    // started in the old image, bounces back to the lobby once. 1 = the reload waits (6 s at most) for the
    // core to tear down and let go, so the old image unloads first. 0 = no wait. Read live.
    bool title_unload_wait = true;
    // NAVIGATION MARKERS ARE PROJECTED WITH THE EYE'S FIELD OF VIEW (reach_nav_marker_logic.h). Halo: Reach's
    // HUD projects a world point to the screen with a camera it builds itself; 1 = that camera gets the vertical
    // field of view the eye was rendered with (it already gets the eye's position, forward and up). 0 = the
    // game's own flat-screen field of view: a marker is then drawn too far from the eye's axis by the
    // ratio of the two (2.155 measured). Read live. Nothing else changes: the HUD height is not involved.
    // Exercised in game with one marker only (a King of the Hill hill marker); marked players and name tags
    // are not. A title that has not bound this keeps its own behaviour (log: NAV MARKERS <title>).
    bool nav_markers_on_target = true;
    // MARKERS FOLLOW THE HEAD, NOT THE GUN (reach_nav_marker_logic.h, part 3). Whether an objective marker is on
    // screen (its marker) or off it (its arrow) is decided by the game outside the picture each eye is drawn
    // with, from the game's own camera, which this mod points down the controller: a marker then showed only
    // while the gun pointed at its objective. 1 = those decisions are made from the head of the last frame.
    // 0 = the game's own camera, as before. Halo: Reach; Halo 4 (halo4_nav_marker_logic.h: its HUD places markers
    // in its interface pass with the aim camera, and the head's is used there once that camera is proven to be
    // the aim's at run time). Read live.
    bool nav_markers_follow_head = true;
    // THE OFF-SCREEN ARROW STAYS IN VIEW (reach_nav_projection.inl). 1 = when a navpoint's target (an objective
    // marker, a marked player: by a static read) is out of view, the game's arrow for it is drawn on a ring about
    // the HEAD's axis, at most 30 degrees out, as one point in the world for both eyes. 0 = the game's own ring,
    // which with the eye's field of view lies past what the eye on the other side of the nose sees: the arrow is
    // then in one eye only.
    // NOT TOUCHED: name tags, fireteam markers, vehicle / lock-on reticles, damage arrows (the projection's
    // other callers keep the game's own ring). Needs nav_markers_on_target. Read when a level loads.
    // Default 1. Exercised in game with a King of the Hill hill marker; the other marker kinds are not.
    bool nav_marker_arrow_in_view = true;
    // HOW FAR OUT THE OFF-SCREEN ARROWS SIT: the ring about the head, as a fraction of the field both eyes see
    // (0.6 = the game's own 0.3 of the whole field), never more than 30 degrees. Lower brings the arrows
    // closer to the centre of the view. A player setting. The default is 0.75; on a headset whose shared
    // field is wide enough that is the 30 degree cap (log: "ring 30.0 x 30.0 deg"). It acts on the
    // object-anchored markers' arrows. Read live, every projection.
    // Acts only with nav_marker_arrow_in_view = 1.
    float nav_marker_ring = 0.75f;
    // PLAYER TAGS: the service tag and arrow Halo 3 draws over each teammate (halo3_player_tags.inl). show 0 =
    // none; scale and opacity multiply the game's own size and alpha; max_distance_m hides tags farther than that
    // from your eye (fading over the last fifth, at most 5 m), 0 = no limit. Objective markers keep the game's own;
    // every player's tag follows them, a flag, ball or bomb carrier's too. Halo 3, in MCC and in Project Reclaimer.
    // Read live, every HUD draw.
    bool player_tags_show = true;
    float player_tags_scale = 0.45f;           // 0.25 to 2
    float player_tags_opacity = 0.25f;         // 0 to 1
    float player_tags_max_distance_m = 0.0f;   // 0 to 300
    // DEV BUILDS ONLY (parsed and saved there; a player build always has the default).
    // nav_marker_witness: 1 = report the pixel the game puts each marker on against the pixel the eye's own
    // picture shows its target at. Live.
    int nav_marker_witness = 0;
    // Live safety values for the tracker's yaw correction. Parsed and saved in DEV BUILDS ONLY; every build
    // runs with them, so a player build always has these defaults. sign: 1, or -1 if a title's seam turns
    // out mirrored. offset: degrees added after the sign.
    int tracker_head_sign = 1;
    float tracker_head_offset_deg = 0.0f;
    // Halo 4's head-relative sound (halo4_sound_listener_logic.h), which of its writes are made while
    // audio_follows_head is on: 1 = the copy handed to the audio middleware, 2 = the game's own listener record,
    // 4 = the listener is also moved to the head. Parsed and saved in DEV BUILDS ONLY (a listening test can then
    // say which of them the ear follows); every build runs with it, so a player build always has the default.
    // THE DEFAULT IS 3: turned to the head, NOT moved, which is what Reach's and Halo 3's lanes do. 7 is
    // measured to move the listener (0 cm from the head; the game's own sits 10 cm away) but is not confirmed
    // by ear, and a moved listener changes the direction of every sound the game keeps at the player's own
    // position. Live.
    int audio_head_halo4_parts = 3;
    bool turn_smooth = true;           // false = snap turn, true = smooth turn
    bool vehicle_smooth_turn = false; // temporary smooth VR turning while seated
    float turn_snap_deg = 30.0f;       // degrees per snap
    float turn_smooth_deg_s = 120.0f;  // smooth turn speed, degrees/second

    // Y and B together sent Start, the pause fallback for titles whose controller Menu button the OpenXR runtime keeps.
    // Now the Pause row's inputs (vr_mod_bindings): read only from a file or preset written before them, to convert.
    bool y_b_start_chord = true;
    // The mod's own rows (VR Mappings, every game): Pause, Y + B either way round to start.
    vr_mapping::ModBindings vr_mod_bindings = vr_mapping::kModDefaults;

    // Halo 2 only: the gamepad's Back/View button is MCC's instant
    // Classic <-> Anniversary graphics switch. 0 = the mod swallows that
    // button in Halo 2 so a physical pad (Steam Controller etc.) cannot flip
    // the renderer mid-game; 1 = pass it through as the game expects.
    bool halo2_gamepad_graphics_switch = false;

    // Halo 2 only: the game's camera shake, from a weapon's fire animation and from damage, explosions and weapon
    // effects (halo2_camera_shake_logic.h). 0 = the headset view keeps still, as Halo 3's always does; 1 = the
    // game's own shake.
    bool halo2_camera_shake = false;

    // Which controller, held next to the head, turns the left stick into the
    // D-pad (UEVR-style gesture): 0 = left controller, 1 = right controller.
    int dpad_hand = 0;
    // Head-gesture activation radius in metres; the existing reach is 30 cm.
    float dpad_head_radius = 0.20f;
    bool dpad_keep_moving = true; // a hand raised while walking leaves the stick walking until it centres
    // Project Reclaimer's keys at the head, always on: X with the left controller presses the talk key, Y the
    // scoreboard key, and A and B with the right controller the vote keys.
    int reclaimer_talk_key = 0x56; // its Windows virtual-key code: V, the host's default for Push to talk
    int reclaimer_scoreboard_key = 0x09; // its Windows virtual-key code: Tab, the host's default
    int reclaimer_vote_yes_key = 0x74; // Windows virtual-key codes: F5 and F6, the host's defaults
    int reclaimer_vote_no_key = 0x75;
    // Quest 3 alternate D-pad: touch the physical left thumb rest, then use
    // the physical right stick. Handedness never swaps these controls.
    bool quest_thumbrest_dpad = false;
    bool disable_flashlight_input = false;
    // Match each campaign's MCC layout; same title order as gesture buttons.
    int flashlight_button[6]{15,15,15,15,15,15}; // legacy migration / unverified-title fallback
    vr_mapping::Overrides vr_bindings[6]{};
    // Each row's held-first controls, mode and further inputs (VR Mappings), per game, in the games' order above.
    vr_mapping::Inputs vr_inputs[6]{vr_mapping::DefaultInputs(GameTitle::Halo3),
        vr_mapping::DefaultInputs(GameTitle::Halo3ODST),vr_mapping::DefaultInputs(GameTitle::HaloReach),
        vr_mapping::DefaultInputs(GameTitle::Halo4),vr_mapping::DefaultInputs(GameTitle::HaloCE),
        vr_mapping::DefaultInputs(GameTitle::Halo2)};
    // The seat's rows (VR Mappings): each game's Banshee bomb and trick, inputs as a row has.
    vr_mapping::ExtraBindings vr_extra_bindings[6]{vr_mapping::ExtraDefaults(GameTitle::Halo3),
        vr_mapping::ExtraDefaults(GameTitle::Halo3ODST),vr_mapping::ExtraDefaults(GameTitle::HaloReach),
        vr_mapping::ExtraDefaults(GameTitle::Halo4),vr_mapping::ExtraDefaults(GameTitle::HaloCE),
        vr_mapping::ExtraDefaults(GameTitle::Halo2)};
    // Automatic VR action routing (F1 > Controls): a VR button reaches the game as the action bound to it. Off by
    // default for now -- the plain route, each VR button as the gamepad button it stands for, is the one that works;
    // the automatic one has known faults.
    bool vr_action_mapping = false;
    // input_preset.h; empty: a scheme no preset named. The struct's scheme is Legacy Default; a new player starts on
    // Default (default_controls.h).
    char input_preset[64] = "Legacy Default";
    // Settings the rows' inputs took over (vr_mapping::ConvertSettingsToInputs), read only from a file or preset written
    // before them: the support trigger gripping air used equipment, and with the hand on the gun zoomed.
    bool trigger_air_equipment = false;
    bool grip_contextual_trigger = false;
    bool flashlight_suppress_on_two_hand = true;
    // Optional temporal upscaler; ordinary eye resolve remains the default.
    int upscaler = 0;
    int dlss_mode = 1;
    bool dlss_jitter = true;
    int dlss_preset = 0;
    bool dlss_debug_view = false;
    bool vr_gameplay_subtitles = true;
    float vr_gameplay_subtitle_scale = 1.f;
    float vr_gameplay_subtitle_x = 0.f, vr_gameplay_subtitle_y = 0.f;
    int vr_gameplay_subtitle_anchor = 0;
    float vr_theatre_subtitle_scale = 1.f;
    float vr_theatre_subtitle_x = 0.f, vr_theatre_subtitle_y = 0.f;
    int vr_theatre_subtitle_anchor = 0;

    // Aim crosshair (stereo only): a small reticle floating where the weapon
    // actually shoots. Drawn as a compositor quad layer, so it costs no game
    // First-person vehicles (currently Halo 3; other titles ignore these
    // until they publish a vehicle state). The camera sits at the engine's
    // Blender-authored point instead of the third-person chase position; head
    // look and leaning stay fully live. 0 restores the stock chase view.
    bool vehicle_first_person = true;
    // + = toward the windshield / raise out of the seat / vehicle's right.
    float vehicle_cam_forward_m = kVehicleCamForwardDefault;
    float vehicle_cam_up_m = kVehicleCamUpDefault;
    float vehicle_cam_right_m = kVehicleCamRightDefault;
    // Per-game fallback values. Unset axes inherit the legacy global trim so
    // existing cfg files retain their exact camera position until edited.
    float vehicle_cam_game[6][3]{}; // title order; forward/up/right
    bool vehicle_cam_game_set[6][3]{};
    ConfigVehicleModelTrim vehicle_model_trims[kVehicleModelTrimCapacity]{};
    // Per-SEAT overrides of the three trims above. An entry only exists once the
    // user adjusts the F1 sliders while SITTING IN that seat (or writes the
    // config line by hand); every other seat keeps following the universal
    // trim, including live edits to it. The F1 sliders are the same three
    // widgets always — they simply bind to whichever seat is under you.
    float vehicle_cam_forward_v[kVehicleTrimSlots] = {};
    float vehicle_cam_up_v[kVehicleTrimSlots] = {};
    float vehicle_cam_right_v[kVehicleTrimSlots] = {};
    bool vehicle_cam_forward_set[kVehicleTrimSlots] = {};
    bool vehicle_cam_up_set[kVehicleTrimSlots] = {};
    bool vehicle_cam_right_set[kVehicleTrimSlots] = {};
    // ODST's own bank, written as vehicle_cam_*_m_odst_<vehicle>_<seat>. The
    // universal trims above are still the fallback for an unset ODST seat, so
    // one universal nudge continues to move every seat in both titles.
    float odst_vehicle_cam_forward_v[kOdstVehicleTrimSlots] = {};
    float odst_vehicle_cam_up_v[kOdstVehicleTrimSlots] = {};
    float odst_vehicle_cam_right_v[kOdstVehicleTrimSlots] = {};
    bool odst_vehicle_cam_forward_set[kOdstVehicleTrimSlots] = {};
    bool odst_vehicle_cam_up_set[kOdstVehicleTrimSlots] = {};
    bool odst_vehicle_cam_right_set[kOdstVehicleTrimSlots] = {};
    // Reach's own bank, written as vehicle_cam_*_m_reach_<vehicle>_<seat>.
    // The first 20 identities ship the user's headset-unverified Blender
    // lineup; later census rows remain unset. F1's explicit universal reset is
    // serialized separately so absence of a numeric key continues to mean
    // "use the shipped placement", not "discard it".
    float reach_vehicle_cam_forward_v[kReachVehicleTrimSlots] = {};
    float reach_vehicle_cam_up_v[kReachVehicleTrimSlots] = {};
    float reach_vehicle_cam_right_v[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_forward_set[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_up_set[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_right_set[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_use_universal[kReachVehicleTrimSlots] = {};
    // Optional vehicle-frame view: ON follows ground-vehicle yaw and pitch
    // while keeping roll out of the horizon; aircraft stay yaw-only. OFF keeps
    // the world-locked view Alpha 0.3.1 shipped; it remains the default.
    bool vehicle_view_follow = false;
    // Use the same interpolated seat/attachment node the visible vehicle uses,
    // plus occupant-head motion relative to the settled pose in that seat.
    // 0 = raw node matrices as a diagnostic A/B.
    bool vehicle_cam_smoothing = true;
    // Tell Halo the occupied seat is a FIRST-PERSON seat while the VR vehicle
    // camera owns the view, so the engine stops drawing your own character
    // where your head is. Only the currently occupied loaded seat tag is
    // touched, and the original value is restored on seat exit; the map file
    // is never modified. Ignored entirely when vehicle_first_person = 0, so
    // third-person driving is unaffected.
    bool vehicle_hide_body = true;
    // Hang the first-person arms and gun off the SEAT rather than off Halo's
    // seated camera, which is the occupant's own head marker. ON keeps them
    // riding the vehicle while your head turns freely; OFF restores the
    // head-anchored origin. Only applies inside a first-person vehicle seat.
    bool vehicle_hands_follow_body = true;
    // How much of the seat's occupant bounce reaches the view, 0..1. 1 is the
    // engine's full travel (about 24 cm) and reads as too much in the headset;
    // 0 bolts the view to the seat. This is a strength control, not a filter -
    // no smoothing is applied at any setting.
    float vehicle_bounce = 0.35f;
    // Re-neutralise the room-space origin on a settled seat entry AND exit, so
    // physical movement made on foot or in the seat is not carried across the
    // boundary as a standing offset. Position only: it never snaps your facing.
    bool vehicle_recenter_on_seat = true;

    // Hold the engine's automatic exposure steady while a first-person seat
    // owns the view. Looking down at a vehicle's dashboard fills the game's
    // luminance measurement with a big dark surface, so its eye adaptation
    // ramps the whole scene's brightness -- something flat play never provokes
    // because nobody looks at the dash. Uses the engine's own exposure lock,
    // engaged only in the seat, so ordinary play keeps its adaptation and
    // authored cinematic exposure changes still work. ODST for now.
    bool vehicle_steady_exposure = true;

    // Motion steering in look-steered ground driver seats (Halo 3, ODST and
    // Reach): grab an invisible wheel with both grips and turn it. 0 = the
    // right stick steers. The wheel and nothing else: Halo CE's and Halo 2's
    // VR seat controls have their own key, vehicle_vr_controls.
    bool vehicle_motion = true;
    // VR controls in Halo CE and Halo 2 vehicle seats. Halo CE: the right
    // stick snap/smooth-turns the seat view (pitch is the head's), and the
    // view follows the hull with vehicle_view_follow. Halo 2: the hand aims the
    // seat, and the seat keeps a view of its own that the aim does not swing.
    // 0 = both games' seats keep the plain right stick, as on a flat screen.
    // The other games' seats are not affected (their aim settings govern them).
    bool vehicle_vr_controls = true;
    // Vehicle cameras: in a vehicle seat, step through the cameras listed for that seat in
    // halomccvr_vehicle_cameras.json (the mod's folder: written on first use, the player's to edit, read
    // again when it changes). The built-in list starts with the seat view itself, so switching this on
    // changes nothing until another camera is chosen (F1 > Vehicles). A camera the mod places shows the
    // seat from outside, with the player's character drawn in it. Halo 3 for now, and only with
    // vehicle_first_person on.
    bool vehicle_cameras = true;
    // A short note when the vehicle camera, its tethering mode or the seat's controls change, and on getting in:
    // "Warthog - Third Person, Tethered - Motion". 0 = none; 1 = in the game's own HUD messages, in the HUD's font
    // (every game; the mod's panel when the game's HUD cannot take it, and in a shared co-op simulation);
    // 2 = on the mod's own panel in the headset.
    int vehicle_camera_note = 1;
    // With vehicle cameras on: the first time you sit in a vehicle the camera file has no entry of its own for (it
    // rides on "default" -- a mod map's own vehicle, or a stock one the file is older than), entries for it are
    // added to the file: the built-in ones where there are any, else one by the vehicle's name with the default
    // entry's cameras, so nothing changes until you edit it. Only added to, never rewritten; the file as it was is
    // kept once as halomccvr_vehicle_cameras.json.before-autoadd. false = the file is never written to.
    bool vehicle_camera_autoadd = true;
    // With vehicle cameras on, in a chase camera whose "aimMarker" in the camera file asks for it (the built-in
    // cameras: their Tethered modes; a file that never says: the modes whose view turns with the vehicle): a purple
    // ring where the VEHICLE is aiming, beside the crosshair that shows where the hand points -- Halo Campaign
    // Evolved's vehicle aim marker. In a mode that turns the view with the vehicle it is where the view comes to
    // rest: aim at it to keep going straight. 0 = never shown, whatever the file says.
    bool vehicle_aim_marker = true;
    // How a vehicle seat is controlled, apart from how the view turns (vehicle_control_logic.h):
    // 1 = Motion, the hand aims every seat, and a look-steered vehicle (Warthog, Mongoose, Ghost, Prowler,
    // Chopper, Banshee, Hornet) turns toward it; 2 = Stick, the right stick aims and steers in every seat, as
    // on a flat screen, and the hand aims nothing; 3 = Wheel, the invisible wheel is in your hands from the
    // moment you sit in a look-steered ground vehicle (an aircraft takes the stick for its heading), and every
    // other seat is the hand's. 0 = as before this choice existed: the hand aims every seat, except that the
    // stick or wheel steers a look-steered driver whose view turns with the vehicle. Halo 3; ODST and Reach with
    // the vehicle cameras on (Reach: not Stick); in the other games the stick or wheel still steers only a view that
    // follows the vehicle.
    int vehicle_control = 1;
    // Where the hand steers a look-steered driver's heading (Motion, or 0 with a view the world holds still), a
    // point the aim resolves to that is BEHIND the vehicle and NEAR it turns nothing: the vehicle keeps its forward,
    // and the aim's pitch is left as it is (vehicle_aim_reject_logic.h). Measured in the vehicle's own ground plane:
    // near = no further from its origin than vehicle_aim_reject_metres (0 = off, at most 20), behind = the 2D dot of
    // the point's direction with the vehicle's forward below vehicle_aim_reject_dot (-0.5: more than 120 deg off the
    // nose; -1 refuses nothing). For a camera placed outside the vehicle, whose hand ray easily lands on the ground
    // just behind it. Every camera, the seat view included. Halo 3 for now. Live.
    float vehicle_aim_reject_metres = 20.0f;
    float vehicle_aim_reject_dot = -0.5f;
    // THE THIRD-PERSON BOOM (vehicle_boom_logic.h): a vehicle camera that stands where the game's own third-person
    // camera does ("origin": "thirdperson") is raised and lowered by the right stick's Y in a seat where that axis
    // aims nothing (Motion, the wheel), the way the flat game's stick moves its camera, inside the range the game
    // gives the look. vehicle_boom_rate_deg_s: degrees of look a second at full push (5 to 360). vehicle_boom_invert:
    // -1 = as the game's own look inversion (its flight inversion for an aircraft's pilot; not inverted where it
    // cannot be read), 0 = not inverted, 1 = inverted. Halo 3, ODST and Reach (Reach's own inversion is not read).
    // Live.
    float vehicle_boom_rate_deg_s = 60.0f;
    int vehicle_boom_invert = -1;
    float vehicle_wheel_max_deg = 75.0f;      // wheel angle at full lock
    float vehicle_wheel_deadzone_deg = 6.0f;  // slack around centre

    // Halo 4 first-person hands (C-H4-11). Places the engine's own
    // first-person weapon assembly at the controller instead of at the head,
    // so the gun and arms follow your hand. The trims are metres, applied in
    // the controller's own frame before the transform reaches the engine, so
    // they read the same way the vehicle seat trims do.
    bool halo4_hands = true;
    float halo4_hand_forward_m = 0.00f;   // + pushes the gun away from you
    float halo4_hand_vertical_m = 0.00f;  // + raises it
    float halo4_hand_lateral_m = 0.00f;   // + moves it toward your right
    // Left-handed: mirrors the placement across the forward axis.
    bool halo4_hands_mirrored = false;
    // Halo 4's authored Mjolnir helmet/visor overlay. This is deliberately
    // separate from HUD scale/curvature so players can keep the complete HUD
    // while hiding only the helmet frame.
    // Off by default. The frame is opaque art the game lights like a model, authored to sit at the edges of a
    // flat screen. The HUD's scale brings it into the middle of the view with the rest of the HUD: in the
    // campaign it covers a band above the HUD and a larger one below it, and each eye sees it cut off at a
    // different place. The visor's thin outline is drawn by another shader and stays.
    bool halo4_helmet = false;
    bool hide_hud = false;
    bool hud_reveal_near_head = false;
    float hud_reveal_radius_m = .22f;
    // Optional CE Anniversary flare workaround; scene lighting stays native.
    bool ce_anniversary_disable_lens_flares = false;
    // Optional per-controller firing for an owned native dual-wield pair.
    bool independent_dual_aim = false;
    bool gun_barrel_aim = false;
    // SHOTS FROM THE MUZZLE (default on, live): each on-foot barrel shot leaves the visible muzzle, as the
    // barrel aim's does, but flies through the point the converged aim is bent through (the reticle's
    // point) instead of along the barrel: it lands where the shot from the eye would have, and its tracer
    // and projectile come out of the gun. Only while the converged aim is in effect; a muzzle the game's
    // own obstruction check pulls back (the gun through a wall the eye sees past) and every other refusal
    // leave from the eye as before. gun_barrel_aim, when on, wins. sight_layer.h: Sight_AimMuzzleShot.
    bool shots_from_muzzle = true;
    // CO-OP SAFE SHOTS (default 2 = AUTOMATIC, live; F1 > Advanced): 1 = ALWAYS: nothing changes a shot as it is fired. It leaves as the game
    // builds it from the aim the hand writes: not from the muzzle or along the barrel, not re-aimed through the
    // newest intent point, not from a second gun's own controller and, in a seat, not redirected or restarted
    // from the eye. In Halo 2 the character's aim and the aim-assist target are also left to the game.
    // A campaign co-op session runs one simulation on every machine from shared input. The written aim is part
    // of that input; a change made inside one machine's simulation is not, and the machines then disagree.
    // Contact melee (physical_melee) does not apply its own damage either: its swing presses the game's melee.
    // Never off by default because nothing tells a player his session is shared before his first shot ends it:
    // 0 is for solo play, and brings back what the switch stands down in every session, co-op included.
    // 2 = AUTOMATIC: stood down unless the game itself says nothing can disagree -- the player is alone in the
    // session, or the session is host-authoritative (Firefight, custom games) -- as the title's simulation thread
    // reads it (session_read_logic.h); anything unread counts as shared. Halo 3, ODST, Reach, Halo 4 and Halo 2;
    // Halo CE's session is not read, so there 2 behaves as 1. The default. A file without
    // coop_safe_shots_version = 2 was written when 1 was the only "on" and the default, not a choice between always
    // and automatic, so its 1 is read as 2 (config.cpp); its 0 stays 0, and a 1 written since is a chosen "always".
    // sight_layer.h: Sight_FireTimeChangesAllowed.
    int coop_safe_shots = 2;
    // AIM LEAD (default 1 tick, live; 0 = off, up to 3): the aim written into the game's record on foot (Halo 3,
    // ODST, Reach) is carried ahead by the setpoint's own rate times this many simulation ticks. A record
    // written at one control pass is first used by the next tick, so a shot leaves along a hand sample about a
    // tick old; the lead writes where the aim will be then. Only the record is led -- the reticle, the gun and
    // the scope are drawn from the hand as before -- and the record is shared input, so every machine of a
    // co-op session fires the same shot. A snap turn, a recentre, a stop and an aim at rest are written
    // unchanged (aim_lead_logic.h). The default is the tick the record recomposition already carries the game's
    // eye by, not a measured optimum: the "AIM LEAD" and "CO-OP SAFE SHOTS" / "SHOT REAIM" log lines say what it
    // did and what is left. Seats are not led. sight_layer.h: Sight_LeadWrittenAim.
    float aim_lead_ticks = 1.0f;

    // EXPERIMENTAL (dev): route this title's aim through the game-agnostic aim-solve core + per-game
    // provider (src/common/aim_solve_logic.h, aim_provider.h). 0 = off (shipped path untouched).
    // Reach only. The provider's StickLoop rung IS the shipped servo (Game_ComputeAimStick), so on its
    // own this aims exactly as before and logs the provider's decisions (AIMPROVIDER ...); its
    // WriteState rung is the direct drive, enabled separately by reach_direct_drive=3 (which routes
    // Reach through the provider by itself).
    int aim_provider = 0;

    // DEV BUILDS ONLY (not parsed, saved or compiled into a player build): the Reach direct-drive
    // research tools below -- the aim probe, the write tests (reach_direct_drive 1, 2 and 4) and the
    // watchpoint locator. A player build accepts reach_direct_drive 0 or 3 and nothing else.
    // EXPERIMENTAL (Halo Reach, READ-ONLY diagnostic): confirm the desired-aiming struct offset for
    // the future direct-drive rung, WITHOUT writing anything and WITHOUT needing co-op. When on, a
    // value-agreement probe logs the candidate DESIRED aim vector (unit+reach_desired_aim_offset)
    // against the known CURRENT aim (unit+0x214): they match at rest and the desired LEADS while you
    // turn. Does not change aim. 0 = off.
    int reach_aim_probe = 0;
    // Candidate byte offset of Reach's unit DESIRED-aiming vec3 (x,y,z floats). 0x208 is the
    // reverse-engineered value: the desired->current integrator (retail 0x480280) reads unit+0x208
    // and writes current aim unit+0x214. Config-settable so the offset can be swept in-headset;
    // parsed as hex (0x...) or decimal. Only consulted by reach_aim_probe.
    int reach_desired_aim_offset = 0x208;

    // EXPERIMENTAL (Halo Reach) direct-drive MECHANISM TEST. The read-only probe shows 0x208 holds a
    // valid aim vector but on foot cannot tell the desired input from a copy; this writes it to learn
    // whether it STEERS aim. Engine thread, on foot, SEH + value-agreement guarded; suppresses the aim
    // stick while active so the write is the only aim influence. 0 = off.
    //   1 = on activation, capture the current aim and a target = that aim swung reach_dd_test_deg
    //       degrees about world-up, then write that FIXED target into unit+reach_desired_aim_offset
    //       every tick. If the field drives aim, the current aim (unit+0x214) converges on the target
    //       -- the view swings by that angle and holds, and the log's err falls to ~0. If it is only a
    //       copy, nothing moves and err stays at the swing angle. Toggle off then on to re-arm.
    //   2 = LIVE OFFSET (no stick suppression): each tick, write the current look (unit+0x214) rotated
    //       reach_dd_test_deg degrees about world-up into the desired-aim field, while you aim
    //       normally. If that field drives the SHOT, bullets land reach_dd_test_deg off your reticle
    //       (proof with no suppression confound); if not, shots go to the reticle as usual.
    //   3 = ACTIVE DIRECT DRIVE (the real thing): hooks the per-user control update haloreach+0x6375C
    //       (the desired_angles ACCUMULATOR) and, on the sim thread, writes the aim
    //       into player_control.desired_angles (yaw/pitch) -- upstream of the derived aim vector and,
    //       by its position, of replication (co-op host-follow not confirmed). Requires a relaunch to
    //       install the hook. Actuated ONLY by the aim provider's WriteState rung (Reach is routed
    //       through the provider automatically): writes start once an arm-gate validates player_control
    //       (render TLS -> ring A master == sim TLS, plus ValueAgreement with the unit aim; policy in
    //       aim_write::ArmGate), on foot and player-controlled only, with the aim stick held neutral
    //       while it drives. Vehicles, cinematics and menus keep the stick servo.
    //       (reach_dd_test_deg is ignored in mode 3.)
    // Modes 1/2 do not test co-op host-follow (whether the write REPLICATES).
    //   4 = PERSISTENCE TEST of the sim's MASTER player_control.desired_angles (ring A master
    //       +0x2417C, found by reach_dd_watchpoint=3): holds the aim stick neutral, checks the pair
    //       against the master unit aim (no write if they disagree), writes yaw+reach_dd_test_deg and
    //       pitch+-10deg ONCE, logs whether it persisted and steered the aim (REACHDDP ...), then
    //       writes the original back and releases the stick. One-shot per session.
    int reach_direct_drive = 0;
    // Halo 3, ODST and Halo 4, ON FOOT: DIRECT DRIVE (1), the same as reach_direct_drive = 3 for the three
    // sibling engines. Your aim writes the game's own control angles instead of steering the right stick
    // (the stick can only turn the view at the game's turn rate). Installs on the next title load, then
    // arms itself once the angles it found agree with the game's aim (log: DRIVE ... armed=1). Vehicles,
    // cutscenes and menus keep the stick. 0 = off: the stick servo aims. Derived from aim_direct_drive by
    // Config_ApplyAimModes. (default 1, as aim_direct_drive)
    int legacy_direct_drive = 1;
    // EVERY TITLE THAT HAS IT (Reach, Halo 3, ODST, Halo 4), live from F1 > Weapon & aim: 1 = DIRECT DRIVE,
    // your aim writes the game's own aim instead of steering the right stick; 0 = the stick aims. Halo 2 on
    // foot: 1 also writes player control's desired angles (the body faces the aim, the network carries it,
    // and the right stick turns the view's own heading; halo2_direct_drive.inl); 0 keeps Halo 2's earlier
    // aim, written into the unit only. Halo CE always writes its aim directly. The per-lane fields
    // reach_direct_drive (3/0) and legacy_direct_drive (1/0) are DERIVED from this by Config_ApplyAimModes;
    // old files that set those are still read, as aliases that can only turn it on. (default 1: direct drive is
    // the aim path everything else builds on; no release ever shipped this key, so no saved file holds an old 0)
    int aim_direct_drive = 1;
    // EVERY TITLE WITH DIRECT DRIVE: 1 = the address cache (src/common/drive_known.h). When a drive had to search
    // its game DLL for its code -- a game build the mod's tables were not measured on -- what it found is written
    // to HaloMCCVR-drive-addresses.txt in the mod's folder, and the next launch on that build verifies those
    // addresses instead of searching again. The file is a cache, not a setting: every address is verified
    // before it is used, another game build or another version of the mod's tables makes an entry dead, and
    // deleting it costs one search. 0 = the file is neither read nor written. (default 1)
    int drive_address_cache = 1;
    // EVERY TITLE, live from F1 > Weapon & aim > "Converged aim" (Reach's also needs direct drive; with it off
    // Reach keeps its own convergence), on foot and in Halo 3, ODST and Reach seats (reach_vehicle_aim):
    // CONVERGED AIM (1). The game traces the ray from your eye along the controller and bends the aim so the
    // shot, which leaves from the character's eye, passes through the point that ray hits; the reticle is
    // drawn on that point. 0 = the fixed-distance aim: the controller's ray converges on one fixed distance
    // (crosshair_distance_m), and no trace runs. (default 1)
    int converged_aim = 1;
    // Swing angle in degrees for the reach_direct_drive=1 test (about world-up). (default 45)
    int reach_dd_test_deg = 45;
    // DEV write-watchpoint locator (default 0 = off). Hardware write-watchpoints armed across all threads;
    // logs the writing instruction + x64 unwind chain. Read-only diagnostic: it never changes aim.
    //   1 = watch unit+0x214 as the render thread sees it. Finds only the sim's snapshot-publish memcpy:
    //       the render thread reads a SNAPSHOT copy of the game state, not the sim's live block.
    //   2 = watch player_control at haloreach+0xC026F0 (+i*0xB8+0x94). REFUTED: reads all-zero in play.
    //   3 = VALUE-SCAN (use this): finds desired_angles by value in the sim's MASTER game state (then
    //       everywhere else), narrows it across 2-3 still aims >=3 deg apart, then watches the survivors
    //       with DR0-DR3 in batches and logs each one's writer -- the direct-drive hook target.
    // Leave at 0 for play.
    int reach_dd_watchpoint = 0;

    // Halo Reach, ON FOOT, direct drive (reach_direct_drive = 3): TRACE-AND-REAIM CONVERGENCE, the
    // Halo: Campaign Evolved aim. The shot keeps leaving from where Reach fires it; the intent ray (the
    // controller/reticle ray the player points) is traced into the world with the game's own
    // collision_test_vector and the game's own crosshair-trace flags (resolved from the build and
    // value-checked against HREK at install; see src/dll/reach_sight_trace.inl), and the aim is bent so
    // the engine's crosshair ray passes through the point the intent ray hits:
    // aim = normalize((ray origin - engine eye) + range * ray direction). The ray is traced 1 km; a miss
    // converges on its far end; range is smoothed. It converges whenever it can
    // resolve; the older on-foot hand shot ray (which MOVED the projectile origin to the hand) is only
    // the worst-case fallback and stands down while it is live. No key enables it: it follows direct
    // drive.
    // DEV BUILDS ONLY, test-only (never saved): trace length override in metres for a cost A/B.
    // 0 = the shipped 1 km.
    float reach_sight_trace_length_m = 0.0f;
    // DEV BUILDS ONLY (not parsed otherwise): where the trace is made, for an A/B. -1 = by the rule (inside the
    // unit's update when alone in the game, at the control pass when the simulation is shared); 0 = always inside
    // the unit's update; 1 = always at the control pass.
    int reach_sight_trace_site = -1;
    // DEV BUILDS ONLY (not parsed otherwise): when Reach moves an effect's place onto the gun, for an A/B. -1 = by
    // the rule (always when alone in the game; only outside a simulation tick when the simulation is shared);
    // 0 = always; 1 = only outside a tick; 2 = never; 3 = only the player's own first-person weapon effects.
    int reach_effect_move_rule = -1;
    // DEV BUILDS ONLY (not parsed otherwise): replace the resolved flags for an experiment. 0 = use them.
    unsigned long long reach_sight_trace_flags = 0;
    int reach_sight_trace_mode = 0;
    // DEV BUILDS ONLY (compiled out otherwise): fault injection proving the trace's fallbacks run, each
    // through the real path. Bit 0 = raise inside the guarded native call (the genuine fault handler);
    // bit 1 = withhold the resolved flags; bit 2 = the resolver's value check refuses at install;
    // bit 3 = the cold worker's periodic re-check withdraws the flags.
    int reach_sight_fault = 0;
    // DEV BUILDS ONLY (not parsed and compiled out otherwise): fault injection proving each rung of the
    // first-person ladder (roomscale's admission, two-handed aim's exit) takes over through the real path,
    // in every title. Bit 0 = the first-person view model reads "not drawn" (an unarmed player, without
    // one: Reach has no unarmed state an automated run can reach); bit 1 = the game camera rung reads "not
    // bound" (as if the title had no such rung); bit 2 = the Forge Monitor answer reads "unknown"; bit 3 =
    // a first-person game camera reads "not first person" (the value agreement with the view model must
    // then withdraw the rung). Read at launch; 0 for every other run.
    int first_person_rung_fault = 0;
    // DEV BUILDS ONLY (not parsed and compiled out otherwise; never saved): fault injection for the direct drives'
    // address resolution, every title (src/common/drive_resolve.h, src/dll/drive_cascade.cpp). Each bit forces one
    // thing that only happens on a game build the tables were not measured on, so the rung below it -- or the
    // refusal -- can be watched working through the real install path. Bit 0 (0x01) = every signature search
    // finds nothing: no drive is hooked, each says what was not found. Bit 1 (0x02) = every signature search
    // finds one match too many: each is taken at its recorded address, and says so. Bit 2 (0x04) = every recorded
    // RVA is wrong: the search must win, and each address is reported MOVED. Bits 1 and 2 together = ambiguous
    // with no record to settle it: no drive is hooked. Bit 3 (0x08) = the module's PE TLS directory disagrees
    // with the instruction a drive anchors on: no drive that reads the engine TLS index is hooked. Bit 4 (0x10)
    // = a hook's watchdog is told the hook never ran: it must say so once, after its ten seconds of possible
    // time. Bit 5 (0x20) = no short field anchor stands at its recorded offset in its function: each must be the
    // one match inside that function. Bit 6 (0x40) = the addresses saved from an earlier launch are stale: the
    // resolution behaves as on a build that is not the recorded one, verifying them fails, and the search must
    // find everything. Bit 7 (0x80) = a self-test, not a fault: where the known addresses verified, the search
    // is run as well and the log says whether the two agree; what is used does not change. With bits 0, 1, 2
    // or 5 the known addresses fail first, so the whole path shows: not verified, searched, reported. While
    // ANY bit is set the address cache's file is neither read nor written, so nothing a launch with this key
    // in it found can reach a later launch (with bit 7 the self-test is therefore of the recorded addresses,
    // the only known ones left). Read when a drive is installed; 0 for every other run.
    int drive_fault = 0;
    // Diagnostic: log the distinct (caller, flags, mode) of Reach's own collision_test_vector calls,
    // to identify the flags a bullet's flight test uses. Leave at 0 for play.
    int reach_collision_census = 0;
    // Diagnostic: bin the active title's own collision queries by caller and flag the ones that start at the
    // engine eye and run along the converged aim (the crosshair / shot query candidates; Halo 4 and Halo 2).
    // Leave at 0 for play.
    int sight_ray_census = 0;
    // Halo 4 SEATED CONVERGENCE (every build; F1 > Vehicles): 1 = a Halo 4 seat converges -- its
    // intent ray is traced, and the aim is bent so the line the SIMULATION aims the seat along passes through the
    // point that ray hits; the reticle sits on that point. The line is sampled from the game's own aim-view call
    // for the seat: with coop_safe_shots on that is the game's own seat camera (the following camera in a seat the
    // game aims in third person), on every machine, and nothing in the simulation is changed -- only the aim
    // written into the control record and the stick. Converged is then claimed as on foot and in a Halo 3 seat:
    // the native assist's cap, a seat's stop and a gun still turning are natural and never switch it off. Needs
    // converged_aim, reach_vehicle_aim > 0 and the first-person seat view. 0 = Halo 4 seats keep the
    // fixed-distance aim. (src/dll/halo4_seat_converge.inl; default 1)
    int halo4_seat_converge = 1;
    // DEV BUILD ONLY (parsed and saved only in dev builds): 1 = a Halo 3 SEATED local shot whose round will inherit
    // its vehicle's velocity (the fire routine's body-velocity branch, 0x369419) re-aims its crosshair query through
    // P' -- the point that makes the round's own velocity, d * Vp + the inherited sideways velocity, point at P --
    // instead of through P (shot_impact::SolveInheritedAim; game.cpp halo3_impact_witness.inl). Needs the round's
    // speed, learned and value-proven from the weapon's earlier rounds, so the first shots of a weapon are not
    // compensated. 0 = re-aim through P. An A/B switch. (default 0)
    int seated_inherit_compensation = 0;
    // DEV BUILD ONLY (parsed, saved and in the menu only in dev builds; a player build always uses 1): FLAT PARITY --
    // THE SHOT POINT MOVED WITH THE EYE (shot_lag_logic.h 4a; sight_layer.h Sight_ShotPoint). 1 = an on-foot local
    // shot that aims through a world point -- Halo 3 / ODST: the crosshair-assist re-aim, the muzzle shot and Halo 3's
    // on-foot record; Halo 2, CE and Halo 4: the muzzle shot; CE: the player assist ray; Reach: the unit-adjust and
    // crosshair-query re-aims and the muzzle shot, ONLY with its measured fire eye (shot_lag_logic.h 4f) -- aims through
    // P' = P + (the eye at the fire - the eye of P's camera sample) instead of the world point P: what stock flat Halo
    // fires, one tick of strafe ahead of the crosshair the player aimed with (measured with the flat probe: impacts land
    // ON the crosshair of the frame that shows them). 0 = through P as published (the A/B: rounds trail a strafe by one
    // tick). The reticle stays on P either way. Live (read per shot). (default 1)
    int shot_point_transport = 1;
    // DEV BUILD ONLY (parsed, saved and in the menu only in dev builds; a player build always uses 1): with
    // shot_point_transport on, 1 = seated shots too (extrapolated from the eye's velocity; a seat has no fire-tick
    // eye). 0 = seats keep P, the A/B (a seated round also inherits its vehicle's velocity:
    // seated_inherit_compensation). A Halo 3 seat whose shots follow the game's own crosshair line is not re-aimed at
    // the fire at all, so the key changes nothing there. (default 1)
    int shot_point_transport_seated = 1;
    // DEV BUILD ONLY (a player build always uses 1), Halo 2 on foot, with shot_point_transport on: 1 = once the
    // firing assist's camera evaluator is PROVEN to be the engine eye (halo2_fire_eye.inl; its proof is fed at rest
    // from the unit's own tick), a muzzle shot's eye at the fire is that evaluator asked inside the fire --
    // the raw post-move eye, (2 - interpolation factor) ticks past the publication P was built from. 0 = always the
    // publication's eye carried one measured tick (the A/B: short by the observer's interpolation lag). The dev
    // HALO 2 FIRE EYE line measures both on every shot either way. Live. (default 1)
    int halo2_measured_fire_eye = 1;
    // DEV BUILD ONLY (a player build always uses 1), Halo 4 on foot, with shot_point_transport on: 1 = once the unit
    // camera position the fire itself asks is PROVEN to be the engine eye at rest (halo4_fire_eye.inl; fed from the
    // unit's own tick while it stands still), a muzzle shot's eye at the fire is that evaluator asked inside the fire --
    // the raw post-move eye, (2 - interpolation factor) ticks past the controller root P was built from. 0 = always the
    // controller root's eye carried one measured tick (the A/B: short by Halo 4's camera interpolation, as Halo 2's).
    // The dev HALO 4 FIRE EYE line measures both on every shot either way. Live. (default 1)
    int halo4_measured_fire_eye = 1;
    // DEV BUILD ONLY (parsed and saved only in dev builds; a player build always uses 1), Halo 3 SEATS: 1 = the seat's
    // record (the angles a multiplayer host fires from) is chosen at the stick call so the game's OWN crosshair query
    // at the fire tick -- from this pass's camera, along the occupant's aim with the seat's learned velocity term --
    // passes through the point, and the host computes the same shot (src/common/seat_upstream_logic.h). The fire-site
    // re-aim stays as the local backstop. Gun-slaved seats (turret, Gauss) keep their own gun-offset path.
    // 2 = the same, solved for the camera an UNMODDED host queries the seat from: in a seat whose stock tag is third
    // person its CHASE camera (this machine's first-person seat patch clears that bit locally), asked of the
    // evaluator's own chase path; this machine's own stock line then sits off the point by the chase offset and the
    // fire-site re-aim carries the local shot. For multiplayer A/B (the dev SHOT UPSTREAM line measures both lines).
    // 0 = the record keeps the render-time solve, as before: the A/B arm for the dev SHOT UPSTREAM line. (default 1)
    int seat_upstream_aim = 1;
    // DEV BUILD ONLY (parsed and saved only in dev builds; the hooks are compiled out of player builds): 1 = log
    // the type-13 pilot controllers, Reach's Sabre/Seraph (haloreach+0x64828) and Halo 4's Broadsword
    // (halo4+0xAA524) -- src/dll/type13_flight_log.inl, docs/TYPE13-FLIGHT-CONTROLLERS-2026-10-01.md Phase B.
    // The hooks write nothing to the game; at 0 they are not installed at all (no detour: a true control run).
    // Read at startup. Leave at 0 for play.
    int type13_log = 0;
    // DEV BUILD ONLY (parsed and saved only in dev builds; a player build always uses 1), Halo 3 / ODST / Reach
    // seats (in Reach: R-V27's `allows weapons` passenger seats): 1 = skip the game's vehicle-tilt
    // rebase of a player's aim (halo3+0x355EFA / halo3odst+0x39A6C4 / haloreach+0x48AE8F) while the
    // first-person seat patch is enabled, so seats aim in world angles as in stock (src/dll/seat_aim_world_frame.inl).
    // 0 = leave the game's rebase in place: the A/B arm for the dev "frame check" line. Followed live. (default 1)
    int seat_aim_world_frame = 1;
    // DEV BUILD ONLY (a player build always uses 0), Halo 3 seats: how the game is made to treat the occupied seat
    // as first person. 0 = the camera director alone is answered (src/dll/halo3_seat_view.inl): the vehicle's
    // loaded data and everything the simulation reads stay stock. 1 = the earlier way, for an A/B: clear the seat's
    // `third person camera` flag in the loaded vehicle definition while seated (C20), which the simulation reads
    // too -- a co-op campaign client goes out of step with its host in a seat. Followed live. (default 0)
    int seat_view_tag_write = 0;
    // DEV BUILD ONLY, for automated tests: 1 = the synthetic gamepad. Scripts dropped as halomccvr_pad.txt in the
    // mod folder press its buttons (tools/Send-MccPad.ps1; the language is src/common/dev_pad_script.h).
    // Read once at startup. Leave at 0 to play.
    int dev_synthetic_pad = 0;
    // DEV BUILD ONLY (not parsed otherwise), for automated tests: 1 = every Halo 3 vehicle is taken as one the mod
    // cannot name -- no identity, so no tuned eye point and no built-in camera preset -- the way a mod map's own
    // vehicles are, so that path can be run on a stock map. Its control class (Halo3ControlClass) is kept. Takes
    // effect for each vehicle the first time it is resolved on a map. Not written to the file.
    int dev_vehicle_unnamed = 0;
    // DEV BUILD ONLY (not parsed otherwise): 1 = in a seat whose camera shows the headless body, a model whose head the
    // mod does not know has its regions hidden one at a time, about 6 s each, each logged ("DEV seat body probe"), so
    // a picture taken in each window shows which region is the head. Not written to the file.
    int dev_seat_body_probe = 0;
    // DEV BUILD ONLY (not parsed and compiled out otherwise), Halo: Reach (src/dll/reach_sim_trace.inl).
    // dev_sim_trace: 1 = every object's state at each simulation tick goes to HaloMCCVR-simtrace-<stamp>.bin beside
    // the log (about 35 MB a minute), to compare two replays of one saved film, or a game with its own film.
    // dev_sim_probe: make one of the mod's own calls while a film replays. Bit 0 = the aim trace inside the tick (a
    // 1 km collision test along the unit's aim, once a tick, in the unit's own update); bit 1 = the unit
    // camera-position evaluator alone, inside the tick; bit 2 = that aim trace between ticks instead (when the game
    // builds the unit's camera); bit 3 = the collision test alone, inside the tick (no evaluator). Both keys can be
    // set live with the dev pad (`set dev_sim_probe 1`).
    // dev_sim_minimal (needs dev_sim_trace, read at startup): the reference. Nothing of the mod is installed in
    // Reach but the recorder -- no VR view, no aim, no other hook -- so the trace is the game's own.
    // dev_sim_bisect: each bit makes one group of the mod's Reach detours a plain pass-through (the game's own
    // function and nothing else): 1 the per-unit update, 2 the collision test, 4 the unit camera position, 8 the
    // unit adjust, 16 the effect location, 32 the muzzle lane's four, 64 the melee damage, 128 the weapon haptics'
    // five, 256 the tracker and the sound listener. For finding which one changes a replay.
    int dev_sim_trace = 0;
    int dev_sim_probe = 0;
    int dev_sim_minimal = 0;
    int dev_sim_bisect = 0;
    // Dev builds: the pixel shader census (src/dll/dev_ps_census.inl). 1 = in Halo 4, each pixel shader's first
    // small draw (a full-screen pass or a HUD quad) is logged with its target, blend state and textures, its
    // disassembly is written beside the log, and once a second the log says which of them drew and which copies
    // touched the game's scene colour or an eye's image. Read at startup (it creates three more hooks); the pad's
    // `set dev_ps_census` pauses and resumes it. (default 0)
    int dev_ps_census = 0;

    // Halo Reach, SEATED, direct drive (reach_direct_drive = 3): the same trace-and-reaim aim in vehicles.
    // The write drives the seat's look (desired_angles), and Reach aims every seat's weapon from it: it
    // traces its crosshair from its OWN camera along that look and fires the barrel at what the trace
    // hits (HREK aim_assist). So the aim is bent to put that camera line through the point the intent ray
    // hits. A seat's own limits (a turret's pitch stop, a passenger's cone) are respected: the write holds
    // at the limit and the converged aim stays claimed, so the barrel fires along its stop as in flat Reach
    // (the fixed-distance shot redirect does not take over there), and the reticle stops at
    // the edge with it (seat_limit_reticle_logic.h; air vehicles have none). A seat whose stick is
    // steering (a vehicle_view_follow driver) always keeps the stick.
    //   0 = off: vehicles keep the stick servo and the fixed-distance shot redirect.
    //   1 = gunner and passenger seats, and drivers whose gun turns apart from the hull (Scorpion,
    //       Wraith); a driver whose look steers the vehicle (Warthog, Ghost, Banshee ...) keeps the stick.
    //   2 = every seat the hand aims, drivers included. (default)
    // Every title's seated direct drive follows the same rule (Halo 3, ODST and Halo 4 too:
    // seat_drive::Decide); Halo 3 and ODST's seated converged aim uses it as well (0 = their old seat aim).
    // F1 > Vehicles > Aim in seats > "Drivers aim with the hand" switches it between 2 and 1.
    int reach_vehicle_aim = 2;
    // SEATED DIRECT DRIVE, every title with direct drive (Reach, Halo 3, ODST, Halo 4, Halo 2): 1 = with
    // aim_direct_drive = 1, a seat's aim is written as on foot -- in the seats reach_vehicle_aim names, axis by
    // axis once that seat's own aim record has checked out -- instead of the stick steering it at the game's
    // turn rate. The seat's limits (a passenger's cone, a gun's pitch stop) still hold: the write stops at the
    // limit and the gun fires where it can, as in flat Halo (a natural limit never switches
    // convergence or direct drive off), and the reticle stops at the edge where the gun is held
    // (seat_limit_reticle_logic.h; a gun still slewing toward the hand, or an air vehicle, keeps it on the
    // converged point). A driver whose stick or wheel steers
    // keeps it for the heading. Every
    // title needs its first-person seat view (vehicle_first_person; Halo 3 / ODST also vehicle_hide_body, whose
    // native seat patch holds the camera; Halo 2 also vehicle_vr_controls, and nothing is written there with
    // vehicle_view_follow on). Halo 4 and Halo 2 seat roles are not mapped, so every seat there counts as a
    // driver whose look steers (written at 2, the stick at 1), and Halo 4 writes the heading only. 0 keeps
    // every seat on the stick.
    // F1 > Vehicles > Aim in seats > "Direct drive in seats". (default 1)
    int seat_direct_drive = 1;
    // A SEAT'S GUN IS AIMED THROUGH THE POINT BY ITS BARREL (Halo 2; seat_barrel_logic.h). A vehicle gun's shot
    // leaves its muzzle and the engine turns it at the crosshair's point only inside the weapon's deviation angle
    // about the barrel, so a seat aimed from its camera through the point lands its shots beside a NEAR point, by
    // the barrel's distance from the camera (a Scorpion's barrel is two metres above its driver: under some 25 m).
    // 1 = the aim written for the seat is the one for which the barrel's line runs through the point; the
    // barrel's place is learned from the seat's own fires and remembered for that kind of vehicle (a Scorpion's is
    // known beforehand). 0 = the camera's line always. Only the written aim changes (what the title replicates).
    // Needs seat_direct_drive and converged_aim. Read live.
    bool seat_aim_through_barrel = true;
    // A SEATED FIRE'S CROSSHAIR TEST RUNS ALONG THE PLAYER'S LINE (Halo 2, halo2_seat_fire.inl; Halo 3, the stock
    // seat's crosshair query, legacy_sight_trace.inl LegacySightSeatLineFromEye: its camera is the seat's third-person
    // camera metres behind the vehicle, so anything behind the player could take his shot). The engine traces
    // the crosshair from the seat's own camera, which is not where a player's eye is (a head anywhere in the room
    // but at the seat's camera): aiming over a low obstacle, the seat camera's line can hit the obstacle, and the
    // engine then turns the shot at it. 1 = at a local seated fire that test starts at the rendered eye the
    // crosshair's point was traced from and runs through that point. 0 = from the seat's camera through the
    // point. Local to the fire (as the re-aim it replaces is), and only where fire-time changes are allowed (never
    // in co-op). Needs converged_aim. Read live.
    bool seat_fire_from_eye = true;

    // rendering. The game's own HUD reticle sits at head-center and is wrong
    // whenever hand aim is on; this one is the truth.
    bool crosshair = true;
    float crosshair_distance_m = 50.0f; // how far along the aim ray it floats
    float crosshair_size_deg = 4.00f;   // apparent (angular) size; saved per game (title_profiles)
    // The crosshair's opacity and brightness, as a scale on its own headset layer (1 = as the game draws it).
    // Opacity scales the art's alpha: below 1 it is more see-through, above 1 a faint crosshair becomes more
    // solid. Brightness scales its colour: below 1 dimmer, above 1 brighter (towards white). 0 to 2 each.
    // Every title's crosshair, the game's own art and the mod's. Where the OpenXR runtime offers layer colour
    // scaling (XR_KHR_composition_layer_color_scale_bias) the compositor applies them; where it does not
    // (SteamVR) the mod applies them to the crosshair's own image. Log: CROSSHAIR TINT. Read live.
    // The defaults are 1.25 and 1.25: a little more solid and a little brighter than the game draws it.
    // Saved per game (title_profiles).
    float crosshair_opacity = 1.25f;
    float crosshair_brightness = 1.25f;

    // EVERY TITLE, while the aim converges (Reach through its own trace, the others through the sight
    // layer -- vr.cpp's traced_reticle_placement): Halo: Campaign Evolved's reticle placement
    // (its aimreticulemaxdist / surfaceoff / minscale knobs; its defaults but for the maximum distance,
    // which is 50 m here so the crosshair stays on a far target). The crosshair sits on the
    // surface the aim ray hits, pulled back reticle_surface_offset_m; it parks at reticle_max_dist_m
    // beyond that and on a miss; and it shrinks away as the surface comes closer than
    // reticle_min_scale_dist_m (to reticle_min_scale there). The ray starts at the player's eye, so
    // parking it short never moves it off the thing being aimed at. It holds crosshair_size_deg at the
    // cap. crosshair_distance_m still applies whenever the aim is not converged (the fallback aim).
    float reticle_max_dist_m = 50.0f;
    float reticle_surface_offset_m = 0.5f;
    float reticle_min_scale_dist_m = 0.7f;
    float reticle_min_scale = 0.001f;

    // DEV BUILD ONLY (parsed, saved and in the F1 menu only in dev builds): an ASSESSMENT aid -- a second marker
    // showing where the game's shot lands; your normal reticle is unchanged. A small magenta ring and dot, where the
    // game's shot lands, traced from the game's own shot origin along its aim, not from the controller
    // (src/common/assess_reticle_logic.h; Reach on foot: the unit's projectile aim, unit +0x214, not the kicking
    // camera) -- to judge by eye how far the game trails the hand. 0 = off, 1 = on foot only, 2 = everywhere
    // (seats and turrets too). It draws that marker and nothing else: the player's reticle, the converged mark,
    // the shots, the aim write and the drive mode never read it (converge::AssessDrawsGameAim;
    // tools/check_assess_isolation.py). The retired key honest_reticle_onfoot is ignored with a log line:
    // its player-facing honest reticle was a cue for judging the stick drive, and with convergence and
    // direct drive in place the game's own tracers do that job. (default 0)
    int honest_reticle_assess = 0;

    // How often the VR crosshair re-reads the game's own animated crosshair
    // art, in displayed frames. Halo 3's authored crosshair kicks when the
    // weapon fires and turns red on a hostile / green on a friendly, so a
    // crosshair that is read once and held looks frozen. Lower = more
    // responsive, higher = cheaper. 0 holds one image and never animates,
    // which is the cheapest setting. Halo 3 only; ODST and Reach keep their
    // own proven cadence. Live in the F1 menu.
    int crosshair_animation_frames = 6;

    // Crosshair color (0-1 per channel). Default approximates Halo 3's own
    // light CHUD blue. File-only: there is no F1 widget for these.
    float reticle_r = 0.62f;
    float reticle_g = 0.87f;
    float reticle_b = 1.0f;

    // Hand-anchored first-person weapon: uniform size multiplier applied to
    // the RIGHT wrist subtree (hand + weapon) around the wrist. Under the true
    // world projection the authored viewmodels read oversized. 0.96 is the
    // headset-tuned release default. Home/End adjust live.
    float gun_scale = 0.96f;

    // Same trim for the LEFT wrist subtree: the support hand, and the second
    // gun when dual-wielding. Independent of gun_scale because the left hand
    // holds no weapon most of the time. 1.00 = authored size, which is what
    // the left hand has always rendered at — until 2026-07-20 the trim loop
    // used the RIGHT wrist's bone mask for both sides, so no left-hand scale
    // value ever reached a bone.
    float left_hand_scale = 0.96f;
    // Visible hand meshes only, in each hand's local frame (metres).
    float left_hand_mesh_x_m = 0.0f;
    float left_hand_mesh_y_m = 0.0f;
    float left_hand_mesh_z_m = 0.0f;
    float right_hand_mesh_x_m = 0.0f;
    float right_hand_mesh_y_m = 0.0f;
    float right_hand_mesh_z_m = 0.0f;


    // (gun_length_scale removed 2026-07-19: a barrel-only squash is not
    // expressible in the engine's uniform-scale bone format; moving bone
    // origins just translated the rigid gun mesh.)

    // Shared controller aim calibration (degrees). ComputeAimPose applies it
    // to the aim ray, reticle and weapon together. Keep the historical keys
    // and saved calibration; use barrel_* below for visual-only alignment.
    float gun_pitch_deg = -3.0f;
    float gun_yaw_deg = 0.0f;
    float gun_roll_deg = 0.0f;

    // Mesh-only barrel trim (degrees): rotates the visible gun and the hands
    // holding it about the controller, leaving the crosshair and the shot
    // ray exactly where they are. Use it to lay the drawn barrel on the
    // crosshair line. Saved per game (see title_profiles). 0 = untouched.
    float barrel_pitch_deg = 0.0f;
    float barrel_yaw_deg = 0.0f;
    float barrel_roll_deg = 0.0f;

    // Push the whole arms+gun assembly along the controller's forward axis,
    // in meters. 0 = anchored at the controller; negative seats the gun back
    // into/behind your fist (the practical "gun feels too long" trim);
    // positive moves it out of your face. Never touches aim.
    float gun_forward_m = -0.14f;

    // Gun-stock calibration: lateral/vertical translation along the visible
    // weapon's own post-mount-rotation right/up axes. Shared by every supported
    // VR title. Visual only: aim, bullets, and the VR reticle stay on the
    // controller ray. 0 = previous placement.
    float gun_right_m = 0.0f;
    float gun_up_m = 0.0f;
    float left_gun_forward_m = -0.16f;
    float left_gun_right_m = 0.04f;
    float left_gun_up_m = -0.06f;

    // Halo 2 CLASSIC ONLY: extra rotation and offset applied to the visible
    // gun AND hands together, so Classic can be lined up with what
    // Anniversary shows. Anniversary is never touched by these; every other
    // title ignores them. The Halo 2 Classic baseline is yaw +1.0 and pitch
    // -9.5; saved config values still win and remain LIVE-editable in F1.
    // Switch to Anniversary to compare without applying this extra trim.
    // Degrees, about the held weapon's own axes (pitch raises/lowers the
    // muzzle), then meters along its own forward/right/up.
    float halo2_classic_gun_pitch_deg = -9.5f;
    float halo2_classic_gun_yaw_deg = 1.0f;
    float halo2_classic_gun_roll_deg = 0.0f;
    float halo2_classic_gun_forward_m = 0.0f;
    float halo2_classic_gun_right_m = 0.0f;
    float halo2_classic_gun_up_m = 0.0f;

    // Raise the muzzle EFFECT origin — the flash and the point bullets appear
    // to leave — along the gun's own up axis, in meters. Reach only; Halo 3 and
    // ODST resolve their muzzle markers on the visible weapon already.
    //
    // Reach's effect markers resolve against the stock, head-anchored weapon,
    // so the mod re-parents them onto the controller-driven gun. That transfer
    // is translation-only and preserves the authored marker offset exactly,
    // which lands the origin on the barrel line but at the authored height —
    // reported in-headset as sitting a few inches low. This trims that, and
    // ONLY that: it is applied after the projectile's own origin and direction
    // are resolved, so where rounds actually land is untouched.
    //
    // 0 = the authored marker height (previous behavior). Positive = up along
    // the barrel, so it rolls with the gun instead of staying world-vertical.
    // ~0.11 is the reported "four or five inches". Tune LIVE in the F1 menu.
    float muzzle_height_m = 0.0f;
    // Independent title preferences. H2 Original/H4 retain the existing
    // suppression by default; other titles need their own proven hide paths.
    bool hide_muzzle_flash[6]{false,false,false,true,false,true};
    // Per-game stock bloom. Verified optional suppression: H3, ODST, Reach.
    bool bloom_enabled[6]{true,true,true,true,true,true};
    // Set once the player picks bloom for that game in the menu. Until then ODST and Reach draw
    // without bloom while VR is on, and bloom_enabled above keeps whatever the file held: nothing
    // is rewritten, so outside VR (or without the mod) the player's own setting stands.
    bool bloom_chosen[6]{};
    bool disable_autoexposure_odst = false;

    // Experimental gun-mounted VR zoom screen. R3 is isolated from Halo's
    // native zoom so the full VR gun/body remain visible; scope_zoom is the
    // fixed 4:3 lens restored on every activation before right-stick adjustment.
    bool scope_enabled = true;
    // SCOPE CONVERGENCE, live (F1 > Scope): 1 = the zoom camera LOOKS AT the point your converged aim sends
    // the shot to (Halo: Campaign Evolved's scope rule), from your eye, so its centre is the reticle's point
    // however far you stand from the character. 0 = each title's earlier zoom camera, which looks along the
    // game's aim and so runs parallel to your sight line, off by the distance between you and the character.
    // Needs Converged aim, and the aim WRITTEN (direct drive; in a seat, both axes): with Converged aim off, no
    // fresh trace, or the stick servo turning the aim, the zoom is the earlier one either way. A seat whose
    // reticle is clamped at a land vehicle's angular stop shows the game's own (clamped) aim instead
    // (the hand's point lies past the stop the gun is held at). (default 1)
    int scope_converge = 1;
    float scope_zoom = 24.0f;
    // AIM FRAME ZOOM (live; F1 > Scope; Halo 3, in MCC and in Project Reclaimer, ODST, Reach and Halo 4). While the game itself is
    // zoomed -- its own zoom level, never the button -- the headset shows one flat picture the game draws along the
    // aim at its own zoomed field of view, as a frame centred on the aim, in place of the immersive view. The zoom
    // button then reaches the game, so the gun-mounted lens is not offered there while this is on. Per zoom
    // level (index 0: the first; 1: the second and higher): the frame's width in degrees, and a strength that
    // narrows the game's zoomed view further (1 = the game's own). The move time eases the frame in from, and back
    // out to, the game's own angles (0 = at once). game.cpp: AimFrameThisFrame; vr.cpp: the aim frame quad.
    // On by default, with a 100 degree frame at strength 1 for both levels (the game's own zoomed view, which the tester
    // found feels like the flat game's): the tester's settings (2026-10-30).
    bool aim_frame_zoom = true;
    // 3D inside the frame: the two eye passes are drawn along the aim and each eye sees its own picture in the
    // frame (the cost of the ordinary 3D view). 0 = one flat picture for both eyes, drawn once. Off by default: the
    // depth adds little to a zoomed view, and one picture keeps the zoom at the mono view's frame rate (2026-11-11).
    bool aim_frame_stereo = false;
    // How far apart the frame's two eyes are, as a multiple of your own eye spacing (binoculars set their lenses
    // wider for the same reason): far targets show little depth at real spacing. The two pictures meet at the
    // point you aim at, so it and the game's crosshair sit at the frame's own depth. 1 to 10.
    float aim_frame_3d_strength = 6.0f;
    float aim_frame_size_deg[2]{100.0f, 100.0f};
    float aim_frame_strength[2]{1.0f, 1.0f};
    float aim_frame_move_s = 0.12f;
    // The HUD inside the frame, apart from the HUD layout (hud_size and the rest) and the Halo 4 helmet frame: 1 =
    // the game's own layout, as on a flat screen, its edge art at the frame's edges. Size scales it whole (0.5 to
    // 1.25); width and height stretch one axis each (0.5 to 1.5). The game's crosshair and zoom art scale with it.
    // aim_frame_logic.h HudScale; game.cpp HudLayoutWanted and Halo4ApplyNativeHudAffine.
    float aim_frame_hud_size = 1.0f;
    float aim_frame_hud_width_scale = 1.0f;
    float aim_frame_hud_height_scale = 1.0f;
    float scope_screen_width_m = 0.159f;
    float scope_screen_right_m = -0.058f;
    float scope_screen_up_m = 0.216f;
    float scope_screen_forward_m = 0.050f;
    int scope_refresh_divisor = 1; // legacy key; lens now refreshes every frame

    // (show_hud / hud_ammo / hud_health / hud_motion / hud_grenades retired
    // 2026-07-19 evening: their chud+0x144..0x14A byte writes used a
    // headset-disproven offset map and suppressed the whole HUD except the
    // objective text. The native HUD is fully game-managed now; the only
    // element control is the reticle kill below.)

    // Hide every native CHUD widget collection marked scripting class
    // crosshair. The head-locked game cursor is redundant with our VR reticle.
    bool kill_reticle = true;

    // Game brightness / gamma, applied to the screen colour constant every title
    // publishes: halo3+0x278EE0, halo3odst+0x2A6308, haloreach+0x252E28. 1.0 =
    // the game's own brightness; higher = brighter, lower = darker. NOT a HUD
    // control — the function once thought to size the HUD adjusts brightness.
    // Live value follows the selected title profile; the shared key seeds older configs.
    float game_brightness = 1.11f;

    // Halo's internal raster scale, applied by the launcher on the next game
    // start. ANY value from kResolutionScaleMin to kResolutionScaleMax is
    // honored exactly; the named tiers (potato .50, low .75, medium 1.00,
    // high 1.30, ultra 1.80, keith david 2.64 = 8K width) are only F1 shortcuts.
    // The OpenXR projection remains at the headset's full size.
    float resolution_scale = 1.0f;

    // Fit the desktop game window to your monitor while the HEADSET keeps
    // rendering at the full resolution_scale size. On a monitor smaller than the
    // render (e.g. an 8K render on a 1080p panel), MCC's window overflows the
    // screen and its menu buttons ("Halo 3", Quit) land off the edge where the
    // mouse can't reach them. With this on, MCC still draws the full-size frame
    // (so the headset picture and the gun alignment never change) but the visible
    // window is shrunk to fit and the GPU downscales the picture into it for free
    // -- no extra render pass, no measurable cost. V5 ships this ON; when
    // disabled the desktop window behaves exactly as it always has. Like
    // resolution_scale, this takes effect on the next game start.
    bool fit_desktop_window = true;
    // Halo 3 and ODST: one eye of the headset's picture on the monitor while in VR, which otherwise stays on its last
    // flat frame (for anyone watching or streaming the window). One extra full-screen draw a frame.
    bool desktop_mirror = true;

    // The VR frame is submitted inside MCC's desktop Present, so MCC's own
    // V-Sync would pace the HEADSET at the DESKTOP monitor's refresh -- a 60 Hz
    // desktop capping a 120 Hz headset. With this on we present the desktop
    // mirror unlocked and let the OpenXR runtime's reported display period be
    // the only clock, so 72/90/120/144 Hz headsets each pace themselves. The
    // desktop mirror may tear; the headset never does (the compositor owns it).
    // ON by default -- the headset must never inherit the desktop's refresh.
    bool desktop_present_unlocked = true;

    // Dormant diagnostic: measure how long the mod holds MCC's render thread
    // inside Present and log the run-up whenever a level is torn down. Keep the
    // config surface and evidence implementation for targeted future runs; the
    // production Present path is compile-time disabled in d3d11_hook.cpp.
    bool coop_probe = false;

    // Image quality (mod-owned, applied live when each eye is expanded into the
    // headset — universal to every title, no restart). These replace/augment the
    // plain linear upscale that made edges shimmer even at high resolution.
    //
    // Upscale/resolve filter: 0 = linear (the old behavior), 1 = sharp
    // (Keys bicubic, a=-0.75). Sharp is the default and the single biggest clarity
    // win, since the game usually renders BELOW the headset's per-eye resolution
    // and the mod upscales the difference.
    int upscale_filter = 1;

    // RCAS-based 2x-overdrive sharpening, 0.00 = off, 1.00 = max. Same five
    // texture loads/one pass; the intentionally aggressive top can ring or clip.
    float sharpness = 0.30f;

    // Anti-aliasing on the finished eye: 0 off, 1 FXAA, 2 FXAA Strong, 3 genuine
    // SMAA 1x, 4 SMAA 1x followed by FXAA Strong. SMAA modes are optional and
    // heavier; Off/FXAA do not run their passes or retain the third eye target.
    int aa_mode = 0;

    // Render draw-distance trim, applied live to every title's shared
    // render_far_clip_distance debug var (stock 10240 world units), resolved by
    // name — no hardcoded addresses. 1.00 = full stock draw distance (no
    // change); lower culls distant geometry earlier for CPU headroom in stereo.
    // Halo 3, ODST, and Reach all honor it. Takes effect immediately, no restart.
    float draw_distance = 1.0f;

    // Weather, off by default because both effects read as a soft, washed-out
    // image in a headset. Each drives the engine's own switch for that effect,
    // asserted at the top of every VR eye render; turning a setting back on
    // restores the title's stock value live, with no restart.
    //
    // Universal settings, like every key here, but only Halo: Reach binds them
    // today - that is where the effects were reported and where the evidence is
    // (HREK's own "Fog and Weather" debug menu plus the pinned haloreach.dll
    // gates; see src/common/reach_render_logic.h). A title with no proven
    // binding stays stock for that effect and says so in the log, exactly as an
    // unresolved draw-distance var does.
    //
    // Rain drives Reach's `render_rain` boolean, resolved by name. Clearing it
    // gates out the rain particle renderer that draws streaks across the view.
    bool rain = false;
    // Atmospheric fog drives bit 0x04 of Reach's render flags - the exact bit
    // HREK's `render_atmosphere_fog` command writes. Clearing it skips the
    // atmosphere fog upload, which is the distance haze that flattens contrast
    // on far terrain. This is NOT the screen-aligned patchy fog, which the mod
    // has always suppressed per eye.
    bool atmospheric_fog = false;

    // (The 0x2EEFC8 placement-slider experiment is retired: measured 2026-07-19,
    // that struct holds colors/alpha/animation only — Halo's HUD has no position
    // data to edit. The HUD panel below is the real fix.)

    // (HUD sizing experiments before 2026-07-19 PM are retired: the capture-diff
    // panel was headset-disproven and the hud_zoom [view+0x2B0]+0x174 poke never
    // resized anything. hud_size below is the one that works — it is DATA, not
    // code: Halo's own layout input.)

    // HUD size: the fraction of the view the HUD lays out into. This drives
    // Halo's own "global safe frame" floats inside the loaded chud_globals tag
    // data (located at runtime by their immutable byte neighborhood; proven in
    // H3EK tag_test on desktop AND live in MCC, 2026-07-19 probe: the engine
    // re-lays the HUD out the same frame the floats change). 0.87 = the game's
    // stock value (mod applies nothing); smaller pulls shields/radar/ammo
    // toward the screen center where both VR eyes can see them.
    float hud_size = 0.43f;

    // Extra horizontal trim after the runtime headset-aspect correction.
    // 1 = automatic shape, lower = narrower, higher = wider.
    float hud_aspect = 1.22f;

    // Normalized curvature: 0 = flat (+0.30 destination-Z delta), 1 = fully
    // curved (-0.30 delta), and 0.5 retains each HUD skin's authored value.
    float hud_curvature = 0.48f;

    // Vertical HUD translation in Halo virtual-screen pixels. Positive raises
    // the complete HUD and negative lowers it; the authored reticle is excluded
    // because VR renders it separately on the controller aim ray.
    float hud_vertical_offset = 16.0f;

    // Automatically enter VR (head tracking + stereo) when a level loads, and
    // drop back to the flat menu screen when you leave — no F2/F11 needed.
    bool auto_vr = true;

    // Main weapon/aim/trigger use the physical left controller. Movement,
    // turning and face buttons keep their existing physical bindings.
    bool left_handed = false;
    // Preserve released controller-role positioning unless explicitly opted in.
    bool experimental_hand_alignment = false;

    // Optional body gestures; each title keeps its own MCC controller layout.
    bool manual_reload = false;
    // Opt-in: while manual_reload or weapon_holsters is on, withhold the game's own reload and
    // weapon-switch bindings (buttons and keyboard) so only the gestures do them. Off keeps
    // every native binding working beside the gestures. Needs the native VR action bridge.
    bool gesture_only_weapon_input = false;
    bool manual_reload_disable_auto = false;
    bool manual_reload_skip_animations = false;
    bool manual_reload_shortened_animation = false;
    bool weapon_holsters = true;
    float weapon_pouch_down_m = 0.50f;
    int weapon_pouch_location = 0; // 0 support-side front hip, 1 behind support shoulder
    float weapon_pouch_offset_x_m = 0.0f;
    float weapon_pouch_offset_y_m = 0.0f;
    float weapon_pouch_offset_z_m = 0.0f;
    float weapon_body_zone_radius_m = 0.20f;
    float weapon_holster_radius_m = 0.30f;
    float weapon_insert_radius_m = 0.18f;
    float weapon_holster_draw_m = 0.25f;
    bool weapon_holster_slide = true;
    bool weapon_holster_click = true;
    bool weapon_needler_shake = false;
    bool dual_wield_shake_reload = true;
    bool weapon_unknown_reload_visual = true;
    float weapon_shake_travel_m = 0.10f;
    // 0 shoulder, 1 hip, 2 over the shoulder (Campaign Evolved's spot), 3 custom (the three values below).
    int weapon_holster_location = 3;
    // THE CUSTOM HOLSTER SPOT (location 3): metres from the head, to the weapon side, up, forward, in the level
    // frame of the way the head faces. Set by hand: F1 > Controls > Reload and holsters > "Calibrate Holster Location"
    // arms it, and the next squeeze of the weapon grip records where that hand is. The default is a spot
    // behind the weapon-side shoulder: 20 cm to the side, 9 cm above the head, 28 cm behind it. Each within
    // 0.80.
    float weapon_holster_custom_x_m = 0.198f;
    float weapon_holster_custom_y_m = 0.085f;
    float weapon_holster_custom_z_m = -0.284f;
    // X, RB, LB, B, Y, A, LT, RT. H3, ODST, Reach, H4, CE, H2. In Project Reclaimer, Halo 3's X means RB
    // (weapon_interaction::ReloadButton).
    int weapon_reload_button[6]{0,0,0,0,0,0};
    int weapon_switch_button[6]{4,4,4,4,4,4};

    // Two-handed weapon aiming: with the support hand on the gun front, aim
    // along the two-hand line instead of the primary wrist alone. Engages by
    // support-grip acquisition in the barrel grab zone: Toggle mode (default)
    // flips engaged on/off per grip click, Hold mode engages only while the
    // grip is held. The primary grip still cycles grenades.
    bool two_handed_aim = true;
    // Coherent committed-sample aim (A+B, runtime-validated) is the default.
    // Configs saved before v6 with this key off are migrated on once. In a v6
    // config, set two_hand_coherent_aim = 0 for the hidden legacy rollback/debug
    // escape; the F1 control is retired.
    bool two_hand_coherent_aim = true;
    // Virtual stock: with two-hand engaged, the base aim orientation follows
    // the configured rear-reference blend toward the raw support controller.
    // OFF preserves the released behavior. Changes only the base engaged
    // two-hand orientation; base position/roll stay primary-controller owned
    // and grip acquisition/latching are unchanged. Default off.
    bool virtual_stock = kVirtualStockEnabledDefault;
    // Standard and Plus remember independent rear-reference influence. The
    // active mode supplies one effective value to the existing solver seam.
    float virtual_stock_standard_strength = kVirtualStockStandardStrengthDefault;
    float virtual_stock_plus_strength = kVirtualStockPlusStrengthDefault;
    // Gravity-aligned OpenXR LOCAL-space height added to the head target.
    float virtual_stock_rear_height_m = kVirtualStockRearHeightDefaultM;
    // Rear reference selector: 0 = Head, 1 = Shoulder, 2 = Chest,
    // 3 = Hybrid.
    int virtual_stock_rear_reference = kVirtualStockProductRearReferenceDefault;
    float virtual_stock_shoulder_back_m = kVirtualStockShoulderBackDefaultM;
    float virtual_stock_shoulder_side_m = kVirtualStockShoulderSideDefaultM;
    float virtual_stock_chest_height_m = kVirtualStockChestHeightDefaultM;
    float virtual_stock_chest_back_m = kVirtualStockChestBackDefaultM;
    float virtual_stock_chest_side_m = kVirtualStockChestSideDefaultM;
    float virtual_stock_adaptive_top_height_m =
        kVirtualStockAdaptiveTopHeightDefaultM;
    float virtual_stock_adaptive_bottom_height_m =
        kVirtualStockAdaptiveBottomHeightDefaultM;
    float virtual_stock_adaptive_top_half_width_m =
        kVirtualStockAdaptiveTopHalfWidthDefaultM;
    float virtual_stock_adaptive_bottom_half_width_m =
        kVirtualStockAdaptiveBottomHalfWidthDefaultM;
    // Hybrid mode-3 direction authority and finite-segment seating controls.
    float virtual_stock_hybrid_offhand_influence =
        kVirtualStockHybridOffhandInfluenceDefault;
    int virtual_stock_hybrid_ads_reference =
        kVirtualStockHybridAdsReferenceDefault; // 0 Head, 1 Shoulder.
    float virtual_stock_hybrid_seat_full_m =
        kVirtualStockProductHybridSeatFullDefaultM;
    float virtual_stock_hybrid_seat_release_m =
        kVirtualStockProductHybridSeatReleaseDefaultM;
    bool virtual_stock_hybrid_horizontal_release =
        kVirtualStockProductHybridHorizontalReleaseDefault;
    float virtual_stock_hybrid_horizontal_full_m =
        kVirtualStockProductHybridHorizontalFullDefaultM;
    float virtual_stock_hybrid_horizontal_release_m =
        kVirtualStockProductHybridHorizontalReleaseDefaultM;
    // Optional stateless release as the primary controller moves away from
    // the full rear target. Disabled preserves the current stock-strength
    // behavior without reading the proximity thresholds.
    bool virtual_stock_proximity_release = kVirtualStockProximityReleaseDefault;
    float virtual_stock_proximity_full_m = kVirtualStockProximityFullDefaultM;
    float virtual_stock_proximity_release_m =
        kVirtualStockProximityReleaseDefaultM;
    // Reduce support-hand rotation by using the grip-pose position as the
    // support endpoint. Position still steers two-handed aim.
    bool two_hand_support_grip_pose = kTwoHandSupportGripPoseDefault;
    // Durable owner-bound support grip. New configurations enable it; parsing
    // an existing explicit value keeps that user's choice without migration.
    bool persistent_support_grip = true;
    // Pavlov-inspired controller-input smoothing strength: the fixed speed-25
    // response with the quaternion-native MCC rotational implementation is
    // unchanged, and this value wet/dry mixes its output over raw directional
    // input copies. 0 = off/raw; 25 = the full fixed filter; intermediate
    // values blend. Consumed by the two-hand directional solver with Virtual
    // Stock on or off (the VS-ON solve takes the same directional copies; the
    // VS-OFF-only product geometry and Lab keep their own gates). Legacy
    // boolean key two_hand_smoothing migrates to 0 or 25.
    float two_hand_smoothing_strength = kTwoHandSmoothingStrengthDefault;
    // Free two-hand (VS-OFF) support-steering authority: how much the support
    // (offhand) controller's directional aim steers the presented aim line
    // while a free two-hand hold is active. 0 = the primary controller's own
    // aim is authoritative; 1 = equal authority. Consumed only by the VS-OFF
    // free two-hand solver; Virtual Stock keeps its own offhand influence.
    float two_hand_offhand_influence = kTwoHandOffhandInfluenceDefault;
    // Retired 2026-09-29: the fixed 200 ms VS-OFF acquire/release transition
    // continuity is internal product behaviour with no player control. This
    // legacy field is still parsed so historical files load quietly, but it is
    // dormant: no product or telemetry resolution may read it. The product
    // truth is TwoHandTransitionContinuityEnabled().
    bool two_hand_transition_smoothing = true;
    // Persistent-grip weapon-switch option (PG ON only). When on, a proven
    // weapon switch while the grip is engaged rebinds the two-hand hold to the
    // new weapon immediately, with no re-orientation. Off: the switched weapon
    // becomes grippable again through the normal spatial admission without
    // releasing the grip button.
    bool two_hand_switch_inherit = false;
    // Two-handed aim follows the weapon's grip zone instead of how far apart the hands read each frame, so it does not
    // switch during play (authored_grab::GripZoneAuthority). Weapons with no grip zone keep the distance test.
    bool two_hand_grip_zone_authority = true;
    // One-handed aim range, metres. With the grip zone deciding: a weapon whose grip is within this of the main hand
    // is pistol-style (authored_grab::GripZoneKindMemory). With the distance deciding: the side measure of the volume in
    // which the hands count as close (authored_grab::CloseHandAuthority), the others scaling with it.
    float two_hand_one_hand_range_m = 0.24f;
    // Two-hand engage style: true = toggle (click left grip on/off), false =
    // hold (engaged only while the left grip is held).
    bool two_hand_toggle = true;
    // Wrist-to-palm correction for the left controller's support-hand seating
    // (IK/hand placement). Excluded from the two-hand aim line, which uses
    // raw tracked controller points. Negative seats the hand back toward the
    // wrist; -0.063 is the headset-tuned release default.
    float left_hand_forward_m = 0.0f;
    // Sideways nudge of the two-hand grab zone along the right controller's +X
    // (positive = toward the player's right) so the grab line sits on the
    // visible barrel. Headset request 2026-07-19: the AR's barrel sat right of
    // the zone and the left hand reached past it.
    float two_hand_zone_right_m = 0.08f; // Menu zero represents this physical baseline.
    // Support-controller-to-palm depth for two-hand grab acquisition only,
    // along support forward. Moves the grab sample from the tracked
    // controller toward the visible palm. Never enters aim geometry,
    // rendered-hand seating, or shots. Headset-confirmed 2026-07-19:
    // two-hand grab described as perfect with this.
    float left_grip_forward_m = 0.097f;

    // VRIK stage A1: show the player's real body (game-animated) by flipping
    // the engine's director/viewmodel switches. Experimental gate for the
    // upper-body VRIK plan (docs/VRIK-ROADMAP.md).
    bool body_wip = false;
    // Separate opt-in tracked avatar; unsupported rigs retain first-person hands.
    bool experimental_body_ik = false;
    bool body_ik_hide_lower = false;

    // VRIK arm IK: bend the first-person arm (shoulder planted, elbow solved,
    // hand+gun to the controller) instead of rigid-parenting the whole
    // assembly. ON = articulated arm (the accepted, working Halo 3 / ODST
    // behavior); OFF = the previous rigid parent. Required on Halo 3 / ODST, so
    // it stays ON by default. Reach's first-person weapon RENDERING is still
    // experimental (its FP layer warps), but that is a Reach render-path issue,
    // not the IK math — do not disable IK globally to work around it.
    bool arm_ik = true;

    // Floating-hands presentation (ON by default). Shows only the hands and the
    // guns they hold; the upper arms and forearms are hidden. This is a pure
    // render filter layered ON TOP of the untouched VRIK solve: the hands are
    // still tracked to the controllers exactly as before, and every arm/aim/
    // dual-wield calculation is unchanged. It only collapses the non-hand,
    // non-gun bones in the final visible palette so their geometry disappears.
    bool floating_hands = true;

    // Opt-in physical contact between the tracked hands / held weapon and
    // engine world geometry. The setting is shared so each title can adopt
    // it without another config migration; Halo 4 and Halo 2 implement it.
    bool world_collision = false;

    // Physical contact melee and gesture input are independently optional.
    // Higher required speed makes both less sensitive; existing saves persist.
    bool physical_melee = false;
    bool gesture_melee = false;
    // What counts as a swing (gesture_melee_logic.h). A swing presses the game's own melee input, nothing more.
    float gesture_melee_speed = 1.5f;      // m/s, relative to the head
    float gesture_melee_extension = 1.5f;  // m/s the arm must be reaching away from the head
    float gesture_melee_travel = 0.40f;    // m an off-hand chop must cover instead; 0 = extension only
    float gesture_melee_reach = 0.30f;     // m the hand must be from the head
    bool gesture_melee_require_grip = false; // off: in the game's own layout the grips are buttons of their own
    bool gesture_melee_off_hand = true;    // the hand without the gun punches too
    bool gesture_melee_directional = true; // the aim follows the swing while it lands
    int gesture_melee_aim_hold_ms = 250;
    int gesture_melee_aim_ramp_ms = 150;
    int gesture_melee_press_ms = 80;
    int gesture_melee_cooldown_ms = 500;
    int gesture_melee_holster_veto_ms = 400; // no swing this soon after a reload or holster gesture
    float gesture_melee_smoothing_ms = 20.0f;
    float gesture_melee_max_speed = 12.0f; // faster, or further than max_reach, is tracking jumping
    float gesture_melee_max_reach = 1.2f;
    bool gesture_melee_smoothed_hands = false; // read the hands after Hand smoothing instead of as they moved
    bool gesture_melee_log = false;
    float physical_melee_swing_speed = kPhysicalMeleeSpeedDefault;

    // Lower the RIGHT (weapon) shoulder so Master Chief's arm doesn't clip up
    // into your face — drops the shoulder anchor along your view-down axis.
    // 0 = the game's authored (high) shoulder; higher = lower shoulder. Tune
    // to match your left shoulder. In world units (~1 wu = 3 m).
    float right_shoulder_drop = 0.06f;

    // Keep the IK shoulders LEVEL with the horizon instead of pitching with your
    // head — anchor the arms to a torso frame that turns with your heading (yaw)
    // but not pitch/roll, so looking up/down no longer swings the shoulder into
    // your face. The gravity/up axis is now MEASURED from the engine's camera-up
    // (the first attempt hardcoded the wrong axis). ON by default; OFF = the old
    // behavior (shoulders ride the camera). Hand and gun are unaffected either way.
    bool shoulder_level = true;

    // Push BOTH shoulder anchors back toward your torso, along your (leveled)
    // heading. Some titles (e.g. ODST) plant the first-person shoulders in front
    // of you; raise this until they sit at your body. 0 = the game's authored
    // shoulder position (Halo 3's confirmed default); higher = further back. In
    // world units (~1 wu = 3 m). Negative pushes them forward.
    float shoulder_back_m = 0.0f;

    // Halo 3's camera motion blur. In two-render stereo its "previous frame"
    // camera is the other eye's, smearing bright content into repeated echoes
    // (the long-standing first-eye ghost). Off is also the VR comfort
    // standard. 0 = title-native blur amount suppressed (default), 1 = engine
    // values.
    bool motion_blur = false;

    // (bullet_snap retired: the composed-wrist snap spun the right hand and
    // sent bullets stage-left; reverted. The real fix is a runtime fire hook —
    // see docs and the bullet_probe diagnostic below.)

    // DIAGNOSTIC (off by default). Ignores the controller and shoves the whole
    // composed first-person assembly a fixed distance to the left. Answers one
    // binary question that the disassembly cannot: does the visible gun MESH
    // consume the bone matrices we edit? See docs/CONTINUATION.md 2026-07-15.
    bool weapon_probe = false;

    // DIAGNOSTIC (off by default). Logs the CHUD state-byte window whenever it
    // changes, to locate the reticle "on target" (enemy red) state and the
    // per-element visibility flags. Log-only; changes nothing.
    bool hud_probe = false;

    // DIAGNOSTIC (off by default; set fsr_probe=1 in the cfg to investigate
    // MCC's built-in FSR). Log-only: on each distinct slot-0 render target MCC
    // binds, records its width/height/format/bind-flags and the current
    // viewport. When MCC's FSR is toggled on/off, a new line reveals whether FSR
    // makes the scene render into a SMALLER target (so the mod must capture the
    // pre-upscale image) or changes the VIEWPORT rect (so the mod's projection
    // assumptions break). Changes nothing; it only observes. See
    // docs/RESOLUTION-FSR-INVESTIGATION.md.
    bool fsr_probe = false;

    // DIAGNOSTIC (off by default; set bullet_probe=1 in the cfg for the
    // fire-hook hunt). On each shot, logs the camera (where Halo spawns your
    // bullet) vs the gun muzzle world position, to measure the "bullets from
    // thin air" gap. Log-only; the actual origin-move needs a runtime fire hook.
    bool bullet_probe = false;

    // Ghosting diagnostic, and the one reliable way to reproduce the open
    // left-eye ghost bug on demand: render the right eye first and the trails
    // move to the right lens. See docs/CONTINUATION.md "KNOWN MAJOR BUG".
    bool right_eye_first = false;

    // INSTALL SEARCH MEMORY (sigscan.h, ImageMemoScope; Halo 3): 1 = within one load of the game's DLL, an install
    // of the hooks answers the searches an earlier install already made from what that one found, and searches
    // only for what is new. Halo 3's hooks are installed again at each new level in one load of the DLL, and an
    // install asks over two hundred searches of it (measured: 226, about 2 s of the worker thread). 0 = every
    // install searches again. Read when an install begins. (default 1)
    int install_search_memory = 1;
    // Dev builds: every remembered answer is searched for as well, and the install's line counts the ones the
    // search did not confirm. The proof of the memory against a running game; it costs every search. (default 0)
    int dev_install_search_check = 0;
    // INSTALL SEARCH CACHE (install_search_cache.h; Halo 3): 1 = what an install's searches found is also saved,
    // per build of the game's DLL, to HaloMCCVR-install-searches.bin beside this file, and the first install of the
    // next launch starts from it: every saved match is compared again in place before it is used, and searched for
    // again where it no longer stands. A game patch, another store's DLL or Project Reclaimer's each get their own
    // entry. 0 = nothing is read or saved; every launch's first install searches. Needs install_search_memory = 1.
    // Read when an install begins. (default 1)
    int install_search_cache = 1;
    // INSTALL HOOKS TOGETHER (minhook_lifecycle.h, MCCVR_EnableHook; Halo 3): 1 = the optional features' hooks an
    // install enables go live together with the core's, in the one pass the core already makes. Each separate enable
    // suspends every thread of the game in turn; one install made about twenty of them, each 0.3-1 s at a level's
    // start, which was most of the wait for VR. 0 = each feature enables its hooks on its own, as before. Read when
    // an install begins. (default 1)
    int install_hooks_together = 1;

    // C-TITLE-1: per-title weapon/hand/HUD/crosshair profiles. Every supported
    // game - and Halo 2's Anniversary and Classic renderers separately - keeps
    // its OWN copy of the tunables in TitleTunables, exactly like the per-vehicle
    // seat trims. The gun_*/left_hand_*/hud_*/crosshair_* fields ABOVE are the live
    // values the engine reads; the title worker copies the active game's
    // profile into them when the game (or Halo 2 renderer) changes, and the
    // F1 sliders edit the active game's profile. `base_tunables` holds the
    // shared defaults a new profile starts from. Stored in the one
    // halomccvr.cfg as halo3_/odst_/reach_/halo4_/halo2a_/halo2c_ keys.
    TitleTunables base_tunables{};
    TitleTunables title_profiles[kTitleProfileCount]{};
    bool per_gun_alignment = false;
};

inline bool VirtualStockUsesPlusMode(const Config& config) noexcept
{
    return config.virtual_stock_rear_reference == 3;
}

inline float& VirtualStockActiveStrength(Config& config) noexcept
{
    return VirtualStockUsesPlusMode(config)
        ? config.virtual_stock_plus_strength
        : config.virtual_stock_standard_strength;
}

inline float VirtualStockActiveStrength(const Config& config) noexcept
{
    return VirtualStockUsesPlusMode(config)
        ? config.virtual_stock_plus_strength
        : config.virtual_stock_standard_strength;
}

inline bool VirtualStockUsesShoulderReference(const Config& config) noexcept
{
    return VirtualStockUsesPlusMode(config)
        ? config.virtual_stock_hybrid_ads_reference == 1
        : config.virtual_stock_rear_reference == 1;
}

// Diagnostic-only Q0 capture family. Head-turn sway correction was retired
// from the product on 2026-09-27, so product resolution can never activate it
// (VirtualStockAimSettingsFromConfig always resolves inverse-neck off). It
// survives for isolated diagnostic profiles and their neutral-capture
// experiments. When it is somehow active it is preserved across
// mode/reference/tuning changes; presentation recenter does not invalidate;
// real epoch transitions do via the existing state machine.
inline bool VirtualStockHeadTurnCorrectionFamilyActive(
    const VirtualStockAimSettings& settings) noexcept
{
    return settings.virtualStockEnabled && settings.hybridInverseNeckEnabled;
}

inline void SetVirtualStockPlusMode(Config& config, bool plus) noexcept
{
    if (plus)
    {
        if (!VirtualStockUsesPlusMode(config))
        {
            config.virtual_stock_hybrid_ads_reference =
                config.virtual_stock_rear_reference == 1 ? 1 : 0;
            config.virtual_stock_rear_reference = 3;
        }
    }
    else if (VirtualStockUsesPlusMode(config))
    {
        config.virtual_stock_rear_reference =
            config.virtual_stock_hybrid_ads_reference == 1 ? 1 : 0;
    }
}

inline void SetVirtualStockShoulderReference(
    Config& config, bool shoulder) noexcept
{
    const int reference = shoulder ? 1 : 0;
    if (VirtualStockUsesPlusMode(config))
        config.virtual_stock_hybrid_ads_reference = reference;
    else
        config.virtual_stock_rear_reference = reference;
}

inline void ResetVirtualStockSettings(Config& config) noexcept
{
    const bool enabled = config.virtual_stock;
    config.virtual_stock = kVirtualStockEnabledDefault;
    config.virtual_stock_standard_strength = kVirtualStockStandardStrengthDefault;
    config.virtual_stock_plus_strength = kVirtualStockPlusStrengthDefault;
    config.virtual_stock_rear_height_m = kVirtualStockRearHeightDefaultM;
    config.virtual_stock_rear_reference = kVirtualStockProductRearReferenceDefault;
    config.virtual_stock_shoulder_back_m = kVirtualStockShoulderBackDefaultM;
    config.virtual_stock_shoulder_side_m = kVirtualStockShoulderSideDefaultM;
    config.virtual_stock_chest_height_m = kVirtualStockChestHeightDefaultM;
    config.virtual_stock_chest_back_m = kVirtualStockChestBackDefaultM;
    config.virtual_stock_chest_side_m = kVirtualStockChestSideDefaultM;
    config.virtual_stock_adaptive_top_height_m =
        kVirtualStockAdaptiveTopHeightDefaultM;
    config.virtual_stock_adaptive_bottom_height_m =
        kVirtualStockAdaptiveBottomHeightDefaultM;
    config.virtual_stock_adaptive_top_half_width_m =
        kVirtualStockAdaptiveTopHalfWidthDefaultM;
    config.virtual_stock_adaptive_bottom_half_width_m =
        kVirtualStockAdaptiveBottomHalfWidthDefaultM;
    config.virtual_stock_hybrid_offhand_influence =
        kVirtualStockHybridOffhandInfluenceDefault;
    config.virtual_stock_hybrid_ads_reference =
        kVirtualStockHybridAdsReferenceDefault;
    config.virtual_stock_hybrid_seat_full_m =
        kVirtualStockProductHybridSeatFullDefaultM;
    config.virtual_stock_hybrid_seat_release_m =
        kVirtualStockProductHybridSeatReleaseDefaultM;
    config.virtual_stock_hybrid_horizontal_release =
        kVirtualStockProductHybridHorizontalReleaseDefault;
    config.virtual_stock_hybrid_horizontal_full_m =
        kVirtualStockProductHybridHorizontalFullDefaultM;
    config.virtual_stock_hybrid_horizontal_release_m =
        kVirtualStockProductHybridHorizontalReleaseDefaultM;
    config.virtual_stock_proximity_release =
        kVirtualStockProximityReleaseDefault;
    config.virtual_stock_proximity_full_m = kVirtualStockProximityFullDefaultM;
    config.virtual_stock_proximity_release_m =
        kVirtualStockProximityReleaseDefaultM;
    config.virtual_stock = enabled;
}

inline VirtualStockAimSettings VirtualStockAimSettingsFromConfig(
    const Config& config, HybridDiagnosticOverride diagnosticOverride) noexcept
{
    VirtualStockAimSettings settings{};
    settings.virtualStockEnabled = config.virtual_stock;
    settings.virtualStockStrength = VirtualStockActiveStrength(config);
    settings.virtualStockRearHeightM = config.virtual_stock_rear_height_m;
    settings.virtualStockRearReference = config.virtual_stock_rear_reference;
    settings.virtualStockShoulderBackM = config.virtual_stock_shoulder_back_m;
    settings.virtualStockShoulderSideM = config.virtual_stock_shoulder_side_m;
    settings.virtualStockChestHeightM = config.virtual_stock_chest_height_m;
    settings.virtualStockChestBackM = config.virtual_stock_chest_back_m;
    settings.virtualStockChestSideM = config.virtual_stock_chest_side_m;
    settings.virtualStockAdaptiveTopHeightM =
        config.virtual_stock_adaptive_top_height_m;
    settings.virtualStockAdaptiveBottomHeightM =
        config.virtual_stock_adaptive_bottom_height_m;
    settings.virtualStockAdaptiveTopHalfWidthM =
        config.virtual_stock_adaptive_top_half_width_m;
    settings.virtualStockAdaptiveBottomHalfWidthM =
        config.virtual_stock_adaptive_bottom_half_width_m;
    settings.virtualStockHybridOffhandInfluence =
        config.virtual_stock_hybrid_offhand_influence;
    settings.virtualStockHybridAdsReference =
        config.virtual_stock_hybrid_ads_reference;
    settings.virtualStockHybridSeatFullM =
        config.virtual_stock_hybrid_seat_full_m;
    settings.virtualStockHybridSeatReleaseM =
        config.virtual_stock_hybrid_seat_release_m;
    settings.hybridHorizontalRearReleaseEnabled =
        config.virtual_stock_hybrid_horizontal_release;
    settings.hybridHorizontalRearReleaseFullM =
        config.virtual_stock_hybrid_horizontal_full_m;
    settings.hybridHorizontalRearReleaseReleaseM =
        config.virtual_stock_hybrid_horizontal_release_m;
    settings.hybridDiagnosticOverride = NormalizeHybridDiagnosticOverride(
        static_cast<uint8_t>(diagnosticOverride));
    // Planted Standard: normal product Centre/Shoulder ignores ordinary legacy
    // proximity attenuation. The persisted keys stay compatible/dormant; the
    // effective resolver forces disabled for rear 0/1 only. Plus (3) already
    // ignores ordinary proximity via its seat/horizontal path; Chest (2) keeps
    // persisted behaviour as dormant research.
    const bool standardProduct =
        settings.virtualStockRearReference == 0 ||
        settings.virtualStockRearReference == 1;
    settings.virtualStockProximityRelease = standardProduct
        ? false
        : config.virtual_stock_proximity_release;
    settings.virtualStockProximityFullM = config.virtual_stock_proximity_full_m;
    settings.virtualStockProximityReleaseM =
        config.virtual_stock_proximity_release_m;
    // Head-turn sway correction was retired from the product (2026-09-27).
    // Normal Standard/Plus resolution is the former sway=0 path: inverse-neck
    // disabled with zeroed knobs. The generic NK capability that remains is
    // reachable only through isolated diagnostic profiles; product resolution
    // can never enable it here.
    settings.hybridInverseNeckEnabled = false;
    settings.hybridInverseNeckStrength =
        kVirtualStockHybridInverseNeckStrengthDefault;
    settings.hybridInverseNeckForwardM =
        kVirtualStockHybridInverseNeckForwardDefaultM;
    settings.hybridInverseNeckUpM = kVirtualStockHybridInverseNeckUpDefaultM;
    settings.hybridInverseNeckLateralM =
        kVirtualStockHybridInverseNeckLateralDefaultM;
    return settings;
}

extern Config g_config;

// Gesture-only weapon input is in force: chosen, and something it applies to is on.
inline bool Config_GestureOnlyWeaponInput() noexcept
{ return g_config.gesture_only_weapon_input && (g_config.manual_reload || g_config.weapon_holsters); }

// Bloom wanted in VR for a game (weapon_interaction title index: 0 H3, 1 ODST, 2 Reach): the player's
// own choice once made; until then ODST and Reach default to no bloom, every other game to its value.
inline bool Config_BloomWanted(unsigned title) noexcept
{
    if (title >= 6) return true;
    if (g_config.bloom_chosen[title]) return g_config.bloom_enabled[title];
    return (title == 1 || title == 2) ? false : g_config.bloom_enabled[title];
}

// True when the HOST decides the size the game is drawn at and the mod cannot move it. False in MCC, where
// the game draws at its swapchain's size and the mod sets that from resolution_scale. Project Reclaimer draws
// the game through a viewport of its own "render resolution" setting whatever the swapchain's size: a larger
// swapchain shows the game in its top-left corner, and a smaller one shows only the top-left part of the game's
// frame, stretched over each eye like a wrong field of view (the dev SCENEPROBE line then reads a render target
// smaller than its viewport, "RT 2970x2142 ... viewport 3840x2160"). So there the mod forces nothing and takes
// the game's own swapchain as the VR raster; resolution_scale does nothing.
// Set once at start (dllmain.cpp), before the D3D11 hooks are installed.
extern bool g_hostOwnsRenderSize;
// The game runs in Project Reclaimer (dllmain.cpp): what is Reclaimer's alone keys on this, not on the render size.
extern bool g_hostIsProjectReclaimer;

// C-TITLE-1 profile control. `profile` is 0..5 (Halo 3, ODST, Reach,
// Halo 4, Halo 2 Anniversary, Halo 2 Classic) or -1 for the shared
// defaults. Apply copies that profile into the live tunables (no-op when
// already active); Store copies the live tunables back into the active
// profile (the F1 save path). Both are cheap and safe on any thread.
const char* Config_TitleProfileName(int profile);
int Config_ActiveTitleProfile();
// The profile whose crosshair and HUD layout tunables are live now, and which the menu names beside them: the
// running game's, or Reach's while no game is running.
int Config_ShownTitleProfile();
void Config_ApplyTitleProfile(int profile);
void Config_StoreLiveTunables();
// The other way: the live fields take the active game's profile again, after something (a controls preset) wrote it.
void Config_RefreshLiveTunables();
// Cold config/UI path only. Identity comes from the admitted primary weapon
// palette, never a native object/tag handle. Unknown models use title settings.
void Config_ApplyWeaponProfile(uint64_t identity);
void Config_RefreshWeaponProfile();
const char* Config_ActiveWeaponProfileName();

// The trim a given SEAT actually uses: its own override when one has been
// set, the universal trim otherwise. `slot` comes from ConfigSeatTrimSlot;
// -1 (on foot, unknown vehicle, unauthored seat) reads the universal trim.
inline float ConfigGameVehicleCam(const Config& c, GameTitle title, unsigned axis)
{
    const int index=static_cast<int>(title)-1;
    if(axis>2)return 0.f;
    if(index>=0&&index<6&&c.vehicle_cam_game_set[index][axis])
        return c.vehicle_cam_game[index][axis];
    return axis==0?c.vehicle_cam_forward_m:axis==1?c.vehicle_cam_up_m:c.vehicle_cam_right_m;
}
inline int ConfigVehicleModelTrimSlot(const Config& c,GameTitle title,uint64_t identity,int seat)
{
    if(!identity||seat<0||seat>31||title<GameTitle::Halo3||title>GameTitle::Halo2)return -1;
    for(unsigned i=0;i<kVehicleModelTrimCapacity;++i)
        if(c.vehicle_model_trims[i].identity==identity&&c.vehicle_model_trims[i].title==title&&
            c.vehicle_model_trims[i].seat==seat)return static_cast<int>(i);
    return -1;
}
inline int ConfigEnsureVehicleModelTrim(Config& c,GameTitle title,uint64_t identity,int seat)
{
    const int existing=ConfigVehicleModelTrimSlot(c,title,identity,seat);
    if(existing>=0)return existing;
    if(!identity||seat<0||seat>31||title<GameTitle::Halo3||title>GameTitle::Halo2)return -1;
    for(unsigned i=0;i<kVehicleModelTrimCapacity;++i)if(!c.vehicle_model_trims[i].identity)
    {c.vehicle_model_trims[i]={title,identity,static_cast<int16_t>(seat)};return static_cast<int>(i);}
    return -1;
}
// Hands a seat back: to its built-in offset where one ships, and to its game's base otherwise.
inline void ConfigResetVehicleModelTrim(Config& c,int slot)
{
    if(slot<0||slot>=static_cast<int>(kVehicleModelTrimCapacity))return;
    ConfigVehicleModelTrim& trim=c.vehicle_model_trims[slot];
    const ConfigShippedVehicleModelTrim* shipped=ConfigShippedVehicleModelTrimFor(trim.title,trim.identity,trim.seat);
    if(!shipped){trim={};return;}
    trim.value[0]=shipped->forward;trim.value[1]=shipped->up;trim.value[2]=shipped->right;
    for(bool& axis:trim.set)axis=true;
}
inline float ConfigVehicleModelCam(const Config& c,GameTitle title,uint64_t identity,int seat,unsigned axis)
{
    const int slot=ConfigVehicleModelTrimSlot(c,title,identity,seat);
    if(axis<3&&slot>=0&&c.vehicle_model_trims[slot].set[axis])return c.vehicle_model_trims[slot].value[axis];
    return ConfigGameVehicleCam(c,title,axis);
}
inline float ConfigSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots &&
        c.vehicle_cam_forward_set[slot])
        return c.vehicle_cam_forward_v[slot];
    return ConfigGameVehicleCam(c,GameTitle::Halo3,0);
}

inline float ConfigSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_up_set[slot])
        return c.vehicle_cam_up_v[slot];
    return ConfigGameVehicleCam(c,GameTitle::Halo3,1);
}

inline float ConfigSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_right_set[slot])
        return c.vehicle_cam_right_v[slot];
    return ConfigGameVehicleCam(c,GameTitle::Halo3,2);
}

// ODST's bank. `slot` comes from ConfigOdstSeatTrimSlot; an unset ODST seat
// falls back to the same universal trim Halo 3 uses, so the universal slider
// still means "every seat in every title".
inline float ConfigOdstSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots &&
        c.odst_vehicle_cam_forward_set[slot])
        return c.odst_vehicle_cam_forward_v[slot];
    return ConfigGameVehicleCam(c,GameTitle::Halo3ODST,0);
}

inline float ConfigOdstSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots &&
        c.odst_vehicle_cam_up_set[slot])
        return c.odst_vehicle_cam_up_v[slot];
    return ConfigGameVehicleCam(c,GameTitle::Halo3ODST,1);
}

inline float ConfigOdstSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots &&
        c.odst_vehicle_cam_right_set[slot])
        return c.odst_vehicle_cam_right_v[slot];
    return ConfigGameVehicleCam(c,GameTitle::Halo3ODST,2);
}

// R-V25: the Blender lineup is the BASE of a Reach seat, so a slot that has an
// authored row must never fall through to the shared universal trim. The
// 2026-08-06 session proved why: a polluted universal (-0.765 / +0.894) reached
// the Warthog driver through its `use_universal` tombstone and moved the seat
// almost a metre. `outHasBase` reports whether the row exists so the F1 slider
// can hang Halo 3's own travel off it.
inline constexpr bool ConfigReachSeatAuthoredBase(
    int slot, float* outForward, float* outUp, float* outRight)
{
    if (outForward)
        *outForward = 0.0f;
    if (outUp)
        *outUp = 0.0f;
    if (outRight)
        *outRight = 0.0f;
    if (slot < 0 || slot >= kReachVehicleTrimSlots)
        return false;
    for (const ConfigReachShippedSeatTrim& trim : kConfigReachShippedSeatTrims)
    {
        if (ConfigReachSeatTrimSlot(trim.vehicleId, trim.seatIndex) != slot)
            continue;
        if (outForward)
            *outForward = trim.forward;
        if (outUp)
            *outUp = trim.up;
        if (outRight)
            *outRight = trim.right;
        return true;
    }
    return false;
}

// Reach's bank follows the same universal fallback contract without sharing
// storage with Halo 3 or ODST. A serialized seat tombstone returns the seat to
// its authored Blender base; only a seat that never had one follows the
// universal trim.
inline float ConfigReachSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kReachVehicleTrimSlots)
    {
        if (!c.reach_vehicle_cam_use_universal[slot] &&
            c.reach_vehicle_cam_forward_set[slot])
            return c.reach_vehicle_cam_forward_v[slot];
        float authored = 0.0f;
        if (ConfigReachSeatAuthoredBase(slot, &authored, nullptr, nullptr))
            return authored;
    }
    return ConfigGameVehicleCam(c,GameTitle::HaloReach,0);
}

inline float ConfigReachSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kReachVehicleTrimSlots)
    {
        if (!c.reach_vehicle_cam_use_universal[slot] &&
            c.reach_vehicle_cam_up_set[slot])
            return c.reach_vehicle_cam_up_v[slot];
        float authored = 0.0f;
        if (ConfigReachSeatAuthoredBase(slot, nullptr, &authored, nullptr))
            return authored;
    }
    return ConfigGameVehicleCam(c,GameTitle::HaloReach,1);
}

inline float ConfigReachSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kReachVehicleTrimSlots)
    {
        if (!c.reach_vehicle_cam_use_universal[slot] &&
            c.reach_vehicle_cam_right_set[slot])
            return c.reach_vehicle_cam_right_v[slot];
        float authored = 0.0f;
        if (ConfigReachSeatAuthoredBase(slot, nullptr, nullptr, &authored))
            return authored;
    }
    return ConfigGameVehicleCam(c,GameTitle::HaloReach,2);
}

inline void ConfigReachSeatUseUniversalTrim(Config& c, int slot)
{
    if (slot < 0 || slot >= kReachVehicleTrimSlots)
        return;
    c.reach_vehicle_cam_forward_set[slot] = false;
    c.reach_vehicle_cam_up_set[slot] = false;
    c.reach_vehicle_cam_right_set[slot] = false;
    c.reach_vehicle_cam_use_universal[slot] = true;
}

// The first F1 move after an explicit reset converts the whole displayed
// triplet into a seat override before changing one axis. It must capture
// exactly what the sliders are showing, which is the authored Blender row when
// the seat has one and the universal trim only when it does not.
inline void ConfigReachSeatBeginTrimEdit(Config& c, int slot)
{
    if (slot < 0 || slot >= kReachVehicleTrimSlots ||
        !c.reach_vehicle_cam_use_universal[slot])
        return;
    c.reach_vehicle_cam_forward_v[slot] = ConfigReachSeatCamForward(c, slot);
    c.reach_vehicle_cam_up_v[slot] = ConfigReachSeatCamUp(c, slot);
    c.reach_vehicle_cam_right_v[slot] = ConfigReachSeatCamRight(c, slot);
    c.reach_vehicle_cam_forward_set[slot] = true;
    c.reach_vehicle_cam_up_set[slot] = true;
    c.reach_vehicle_cam_right_set[slot] = true;
    c.reach_vehicle_cam_use_universal[slot] = false;
}

void ConfigLoad(const wchar_t* path);
// Derive each lane's own direct-drive field from aim_direct_drive (call after changing it live).
void Config_ApplyAimModes(); // missing file -> file is created with defaults
void ConfigLoadMigrating(const wchar_t* primaryPath, const wchar_t* legacyPath);
void ConfigSave();
const wchar_t* Config_Path();
void Config_SetReadOnly(bool readOnly); // the desktop settings window, while the game owns the file
