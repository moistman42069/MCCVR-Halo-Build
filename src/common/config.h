#pragma once

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

struct Config
{
    // Stamps the Halo 3, ODST and Reach shipped seat tables into their
    // independent per-seat arrays. Every other member keeps its default member
    // initializer, so Config{} remains the one description of a fresh
    // configuration.
    Config();

    int config_version = 5;

    // Portable OpenXR feedback and pose stabilization. Headset smoothing is a
    // deliberately tiny previous-frame blend (0.03 shipped default, 10% hard maximum)
    // so users may remove micro-jitter without turning head motion into a laggy camera.
    // Aim stabilization affects only the floating VR crosshair; weapon aim and
    // bullets continue to use the current raw controller pose.
    float haptic_intensity = 0.86f;
    float headset_smoothing = 0.03f;
    float aim_stabilization = 0.48f;

    float screen_width_m = 4.0f;    // width of the virtual screen, in meters
    float screen_distance_m = 2.4f; // how far away the screen floats, in meters

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

    // Show the welcome page automatically, once, at the start of each launch.
    // The mod has no other way to tell a player anything before they play: the
    // README is outside the game and this file's comments are only read by
    // people who open it. Ticking "Don't show this again" on that page clears
    // this. It suppresses only the AUTOMATIC appearance -- the Welcome page
    // stays in the F1 menu so the message can be read again at any time.
    bool show_welcome = true;

    // M3 VR controller turning (right Sense stick).
    bool turn_smooth = true;           // false = snap turn, true = smooth turn
    float turn_snap_deg = 30.0f;       // degrees per snap
    float turn_smooth_deg_s = 120.0f;  // smooth turn speed, degrees/second

    // Send Start when Y and B are pressed together. This is the pause fallback
    // for titles whose controller Menu button is reserved by the OpenXR runtime.
    bool y_b_start_chord = true;

    // Which controller, held next to the head, turns the left stick into the
    // D-pad (UEVR-style gesture): 0 = left controller, 1 = right controller.
    int dpad_hand = 0;

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

    // Motion steering in look-steered ground driver seats: grab an invisible
    // wheel with both grips and turn it. 0 = the right stick steers.
    bool vehicle_motion = true;
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

    // rendering. The game's own HUD reticle sits at head-center and is wrong
    // whenever hand aim is on; this one is the truth.
    bool crosshair = true;
    float crosshair_distance_m = 41.0f; // how far along the aim ray it floats
    float crosshair_size_deg = 10.10f;  // apparent (angular) size

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

    // (gun_length_scale removed 2026-07-19: a barrel-only squash is not
    // expressible in the engine's uniform-scale bone format; moving bone
    // origins just translated the rigid gun mesh.)

    // Fixed mounting rotation between the weapon bone's authored frame and
    // the controller (degrees). Rotates ONLY the visible gun + muzzle flash;
    // the cursor/bullet ray stays fixed on the controller, so tune these
    // until the barrel lies on the cursor line. Tune LIVE in the F1 menu;
    // save keeps your calibration.
    float gun_pitch_deg = -3.0f;
    float gun_yaw_deg = 0.0f;
    float gun_roll_deg = 0.0f;

    // Push the whole arms+gun assembly along the controller's forward axis,
    // in meters. 0 = anchored at the controller; negative seats the gun back
    // into/behind your fist (the practical "gun feels too long" trim);
    // positive moves it out of your face. Never touches aim.
    float gun_forward_m = -0.14f;

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

    // Experimental gun-mounted VR zoom screen. R3 is isolated from Halo's
    // native zoom so the full VR gun/body remain visible; scope_zoom is the
    // fixed 4:3 lens restored on every activation before right-stick adjustment.
    bool scope_enabled = true;
    float scope_zoom = 24.0f;
    float scope_screen_width_m = 0.159f;
    float scope_screen_right_m = -0.058f;
    float scope_screen_up_m = 0.216f;
    float scope_screen_forward_m = 0.050f;
    int scope_refresh_divisor = 3;

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
    // One value for all three titles, so a change is felt everywhere.
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
    // -- no extra render pass, no measurable cost. OFF by default; when off the
    // desktop window behaves exactly as it always has. Like resolution_scale,
    // this takes effect on the next game start (close MCC and relaunch).
    bool fit_desktop_window = false;

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
    float hud_size = 0.38f;

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

    // Two-handed weapon aiming: when you bring your left hand up to the gun
    // (support-hand grip), aim along the line from the right hand to the left
    // hand instead of the right wrist alone — steadier, and the barrel points
    // exactly where you look down the gun. Auto-engages by hand pose; drops
    // when you lower the support hand. The right grip still cycles grenades.
    bool two_handed_aim = true;
    // Two-hand engage style: true = toggle (click left grip on/off), false =
    // hold (engaged only while the left grip is held).
    bool two_hand_toggle = true;
    // Wrist-to-palm correction for the left controller. This same point drives
    // the rendered support hand and the two-hand aiming line so they stay
    // aligned. Negative seats the hand back toward the wrist; -0.063 is the
    // headset-tuned release default.
    float left_hand_forward_m = -0.063f;
    // Sideways nudge of the two-hand grab zone along the right controller's +X
    // (positive = toward the player's right) so the grab line sits on the
    // visible barrel. Headset request 2026-07-19: the AR's barrel sat right of
    // the zone and the left hand reached past it.
    float two_hand_zone_right_m = 0.03f;
    // Wrist-bone-to-palm distance of the rendered left hand, along the left
    // controller forward. Extends the two-hand grab line/zone sample out to
    // the VISIBLE palm (the hand target anchors the wrist bone). Headset-
    // confirmed 2026-07-19: two-hand grab described as perfect with this.
    float left_grip_forward_m = 0.097f;

    // VRIK stage A1: show the player's real body (game-animated) by flipping
    // the engine's director/viewmodel switches. Experimental gate for the
    // upper-body VRIK plan (docs/VRIK-ROADMAP.md).
    bool body_wip = false;

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
};

extern Config g_config;

// The trim a given SEAT actually uses: its own override when one has been
// set, the universal trim otherwise. `slot` comes from ConfigSeatTrimSlot;
// -1 (on foot, unknown vehicle, unauthored seat) reads the universal trim.
inline float ConfigSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots &&
        c.vehicle_cam_forward_set[slot])
        return c.vehicle_cam_forward_v[slot];
    return c.vehicle_cam_forward_m;
}

inline float ConfigSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_up_set[slot])
        return c.vehicle_cam_up_v[slot];
    return c.vehicle_cam_up_m;
}

inline float ConfigSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_right_set[slot])
        return c.vehicle_cam_right_v[slot];
    return c.vehicle_cam_right_m;
}

// ODST's bank. `slot` comes from ConfigOdstSeatTrimSlot; an unset ODST seat
// falls back to the same universal trim Halo 3 uses, so the universal slider
// still means "every seat in every title".
inline float ConfigOdstSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots &&
        c.odst_vehicle_cam_forward_set[slot])
        return c.odst_vehicle_cam_forward_v[slot];
    return c.vehicle_cam_forward_m;
}

inline float ConfigOdstSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots &&
        c.odst_vehicle_cam_up_set[slot])
        return c.odst_vehicle_cam_up_v[slot];
    return c.vehicle_cam_up_m;
}

inline float ConfigOdstSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots &&
        c.odst_vehicle_cam_right_set[slot])
        return c.odst_vehicle_cam_right_v[slot];
    return c.vehicle_cam_right_m;
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
    return c.vehicle_cam_forward_m;
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
    return c.vehicle_cam_up_m;
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
    return c.vehicle_cam_right_m;
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

void ConfigLoad(const wchar_t* path); // missing file -> file is created with defaults
void ConfigLoadMigrating(const wchar_t* primaryPath, const wchar_t* legacyPath);
void ConfigSave();
