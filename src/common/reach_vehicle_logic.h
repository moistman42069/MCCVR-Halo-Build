#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

// Reach vehicle identities are an explicit contract shared by the runtime,
// per-seat config bank and Blender authoring kit. The order mirrors the
// official HREK tag census; zero always means that identity could not be
// proven, in which case the seat camera runs generically on universal trims.
// The first 20 rows are the original Blender-authored lineup; the rows from
// MacCannon on are the 2026-08-04 full-census sweep of every official HREK
// .vehicle tag (out/hrek-evidence/reach-vehicle-census/), whose pipeline
// reproduced all 20 known tuples, types and the 25-seat census bit-exactly
// before any new row was trusted. Aliases resolve to one row on purpose:
// the four falcon side turrets author plasma_turret's exact tuple+type,
// shade_anti_air_cannon authors shade_flak's, seraph covers
// seraph_in_atmosphere, and no row exists for tags whose every seat is
// authored invalid-for-player (phantoms, troop-hog bed, AI emplacements).
enum class ReachVehicleId : uint8_t
{
    Unknown = 0,
    Banshee,
    SpaceBanshee,
    Ghost,
    Revenant,
    Wraith,
    WraithGunner,
    Mongoose,
    Warthog,
    WarthogChaingun,
    WarthogGauss,
    WarthogRocket,
    Falcon,
    Sabre,
    Scorpion,
    Forklift,
    Cart,
    ShadePlasma,
    ShadeFlak,
    PlasmaTurret,
    Machinegun,
    MacCannon,
    ScorpionAntiInfantry,
    Seraph,
    Pelican,
    PelicanChinGun,
    CorvetteCannon,
    SpacePhantomChinGun,
    SpacePhantomBeamTurret,
    CargoTruck,
    MilitaryTruck,
    OniVan,
    Pickup,
    TruckCabLarge,
    SquadDropPod,
};

inline constexpr int kReachVehicleIdentityCount =
    static_cast<int>(ReachVehicleId::SquadDropPod);

// The fingerprint table carries MORE rows than identities: retail-compiled
// maps hold model bounding spheres from a different HREK import generation
// than the shipped source tags. The exact retail tuples observed for Warthog,
// Mongoose, Shade Flak, Shade Plasma, Scorpion, Machinegun, Sabre and Cart are
// one or two ULPs away in individual words. HREK's own test_fbx_mongoose
// re-import of identical geometry authors the retail Mongoose word bit-for-bit,
// proving this class of last-bit difference is import rounding noise, not
// different content. Each retail tuple is an explicit full-tuple alias of its
// identity row; there is deliberately no tolerant comparison, and the
// physics-type gate still has to agree.
inline constexpr int kReachVehicleFingerprintCount =
    kReachVehicleIdentityCount + 8;

// Reach's vehicle postprocess retains a positive vehicle-authored bounding
// sphere, otherwise it copies the referenced model's exact runtime sphere into
// these four early vehicle-definition fields. The values below follow that
// official HREK branch; each complete tuple is unique and compares bit-for-bit.
// Unknown or modified content keeps only this optional feature stock.
struct ReachVehicleFingerprint
{
    uint32_t radius = 0;
    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    uint32_t offsetZ = 0;
};

struct ReachVehicleFingerprintEntry
{
    ReachVehicleId identity = ReachVehicleId::Unknown;
    ReachVehicleFingerprint fingerprint{};
};

inline constexpr std::array<ReachVehicleFingerprintEntry,
                            kReachVehicleFingerprintCount>
    kReachVehicleFingerprints{{
        {ReachVehicleId::Banshee,
         {0x3FE3DDB8, 0xBEAA6EFF, 0xBAE51357, 0x3F13532A}},
        {ReachVehicleId::SpaceBanshee,
         {0x3FB33333, 0x00000000, 0x00000000, 0x00000000}},
        {ReachVehicleId::Ghost,
         {0x3F7C19A7, 0xBCD7C712, 0x38612D0C, 0x3E1972D5}},
        {ReachVehicleId::Revenant,
         {0x3F59999A, 0xBDCCCCCD, 0x00000000, 0xBDCCCCCD}},
        {ReachVehicleId::Wraith,
         {0x3FE66666, 0x00000000, 0x00000000, 0x00000000}},
        {ReachVehicleId::WraithGunner,
         {0x3F4CCCCD, 0x00000000, 0x00000000, 0x3F000000}},
        {ReachVehicleId::Mongoose,
         {0x3F2ABABB, 0x39815C03, 0x39CE6FFD, 0x3E7776E0}},
        {ReachVehicleId::Warthog,
         {0x3F978072, 0xBD406A82, 0xBAD03632, 0x3EC25783}},
        {ReachVehicleId::WarthogChaingun,
         {0x3F1CC397, 0x3E86AFD5, 0xB3A6AFD5, 0x3E9C5885}},
        {ReachVehicleId::WarthogGauss,
         {0x3F28C016, 0x3E8E27FC, 0xBC86EDEB, 0x3EA9A1F3}},
        {ReachVehicleId::WarthogRocket,
         {0x3F12852A, 0x3DC5E04F, 0x3BE1BCFB, 0x3EA9708D}},
        {ReachVehicleId::Falcon,
         {0x4037DF8F, 0xBE781BAB, 0x363BE06E, 0x3F2D5046}},
        {ReachVehicleId::Sabre,
         {0x40A69CFE, 0xBF19A963, 0xBB2A0199, 0x3F8AF1EF}},
        {ReachVehicleId::Scorpion,
         {0x400D30E2, 0x3D70D852, 0x3D28430C, 0x3EBBF788}},
        {ReachVehicleId::Forklift,
         {0x3F971D82, 0x3E707E5B, 0xBC9C748D, 0x3EF3BE41}},
        {ReachVehicleId::Cart,
         {0x3F786DCF, 0x3C8C975D, 0xBC8111A2, 0x3EE5C245}},
        {ReachVehicleId::ShadePlasma,
         {0x3F7A9153, 0x3E1B9697, 0xBC935063, 0x3F1EF5FC}},
        {ReachVehicleId::ShadeFlak,
         {0x3F73045B, 0x3DD8D24F, 0xBC93505D, 0x3F1EA4E9}},
        {ReachVehicleId::PlasmaTurret,
         {0x3F000000, 0x00000000, 0x00000000, 0x3DCCCCCD}},
        {ReachVehicleId::Machinegun,
         {0x3EE99B18, 0x3D9811F2, 0xB9914460, 0x3E8589DB}},
        {ReachVehicleId::MacCannon,
         {0x41200000, 0x00000000, 0x00000000, 0x00000000}},
        {ReachVehicleId::ScorpionAntiInfantry,
         {0x3ED28405, 0x3DB8AD5D, 0xB8A6B78A, 0x3D8A1BD8}},
        {ReachVehicleId::Seraph,
         {0x410F9AAB, 0xC02DAE74, 0xB625B5D0, 0x3F8295A1}},
        {ReachVehicleId::Pelican,
         {0x40B33333, 0x00000000, 0x00000000, 0x00000000}},
        {ReachVehicleId::PelicanChinGun,
         {0x3F2BC474, 0x3EC52CD6, 0xB3C52CD6, 0xBE7D391F}},
        {ReachVehicleId::CorvetteCannon,
         {0x40400000, 0x00000000, 0x00000000, 0x00000000}},
        {ReachVehicleId::SpacePhantomChinGun,
         {0x3F666666, 0x3F666666, 0x00000000, 0x00000000}},
        {ReachVehicleId::SpacePhantomBeamTurret,
         {0x3F906FE2, 0x3F8275C2, 0x374C2C51, 0x3BA3D9C0}},
        {ReachVehicleId::CargoTruck,
         {0x3FAE222F, 0x3C709BBD, 0xBC1C81E3, 0x3F19FF3B}},
        {ReachVehicleId::MilitaryTruck,
         {0x3FD4EDB9, 0xBDAC5C6F, 0x3BA35FAA, 0x3F39CE19}},
        {ReachVehicleId::OniVan,
         {0x3FD46CE8, 0xBDB33F17, 0x3BA3612C, 0x3F39C761}},
        {ReachVehicleId::Pickup,
         {0x3F9FCD09, 0xBDA6FD71, 0xB329BF5D, 0x3ECB6694}},
        {ReachVehicleId::TruckCabLarge,
         {0x3FE2557B, 0xBD4C676F, 0x3ACF7265, 0x3F6831B4}},
        {ReachVehicleId::SquadDropPod,
         {0x3FDAB04B, 0xBD31519F, 0x390792C5, 0x3F912A02}},
        // Exact retail-map aliases of the HREK identities above. Warthog and
        // Mongoose came from R-V17; the remaining six are the complete
        // accepted-partial 619644c miss census.
        {ReachVehicleId::Warthog,
         {0x3F978071, 0xBD406A82, 0xBAD03632, 0x3EC25783}},
        {ReachVehicleId::Mongoose,
         {0x3F2ABABB, 0x39815C02, 0x39CE6FFD, 0x3E7776E0}},
        {ReachVehicleId::ShadeFlak,
         {0x3F73045A, 0x3DD8D24E, 0xBC93505D, 0x3F1EA4E9}},
        {ReachVehicleId::ShadePlasma,
         {0x3F7A9153, 0x3E1B9697, 0xBC935062, 0x3F1EF5FC}},
        {ReachVehicleId::Scorpion,
         {0x400D30E2, 0x3D70D852, 0x3D28430B, 0x3EBBF788}},
        {ReachVehicleId::Machinegun,
         {0x3EE99B18, 0x3D9811F2, 0xB991445F, 0x3E8589DB}},
        {ReachVehicleId::Sabre,
         {0x40A69CFE, 0xBF19A963, 0xBB2A0198, 0x3F8AF1EF}},
        {ReachVehicleId::Cart,
         {0x3F786DCE, 0x3C8C975D, 0xBC8111A1, 0x3EE5C245}},
    }};

inline constexpr bool ReachVehicleFingerprintEqual(
    const ReachVehicleFingerprint& a, const ReachVehicleFingerprint& b)
{
    return a.radius == b.radius && a.offsetX == b.offsetX &&
        a.offsetY == b.offsetY && a.offsetZ == b.offsetZ;
}

inline constexpr ReachVehicleId ReachResolveVehicleFingerprint(
    const ReachVehicleFingerprint& fingerprint)
{
    ReachVehicleId result = ReachVehicleId::Unknown;
    for (const auto& entry : kReachVehicleFingerprints)
    {
        if (!ReachVehicleFingerprintEqual(entry.fingerprint, fingerprint))
            continue;
        if (result != ReachVehicleId::Unknown)
            return ReachVehicleId::Unknown;
        result = entry.identity;
    }
    return result;
}

inline constexpr uintptr_t kReachVehicleBoundsRadiusOffset = 0x0C;
inline constexpr uintptr_t kReachVehicleBoundsOffsetX = 0x10;
inline constexpr uintptr_t kReachVehicleBoundsOffsetY = 0x14;
inline constexpr uintptr_t kReachVehicleBoundsOffsetZ = 0x18;
inline constexpr uint8_t kReachObjectKindVehicle = 1;
inline constexpr uint32_t kReachVehicleGroupTag = 0x76656869;

// Raw seat indices come from each Reach unit tag. Do not replace these with
// inferred roles: Falcon's player seats are 0, 3 and 4, and Pelican's are
// its four passenger benches - its driver seat is authored invalid-for-player.
// Since the 2026-08-04 generic-coverage change this census no longer gates
// the camera; it names which seats the official tags author player-valid so
// the worker can log an out-of-census seat precisely.
inline constexpr bool ReachVehicleSeatIsPlayer(ReachVehicleId id, int seat)
{
    if (seat < 0 || seat >= 16)
        return false;
    switch (id)
    {
    case ReachVehicleId::Revenant:
    case ReachVehicleId::Mongoose:
    case ReachVehicleId::Warthog:
    case ReachVehicleId::CargoTruck:
    case ReachVehicleId::MilitaryTruck:
    case ReachVehicleId::OniVan:
    case ReachVehicleId::Pickup:
    case ReachVehicleId::TruckCabLarge:
        return seat == 0 || seat == 1;
    case ReachVehicleId::Falcon:
        return seat == 0 || seat == 3 || seat == 4;
    case ReachVehicleId::Pelican:
        return seat == 4 || seat == 5 || seat == 9 || seat == 10;
    case ReachVehicleId::SquadDropPod:
        return seat >= 0 && seat <= 3;
    case ReachVehicleId::Banshee:
    case ReachVehicleId::SpaceBanshee:
    case ReachVehicleId::Ghost:
    case ReachVehicleId::Wraith:
    case ReachVehicleId::WraithGunner:
    case ReachVehicleId::WarthogChaingun:
    case ReachVehicleId::WarthogGauss:
    case ReachVehicleId::WarthogRocket:
    case ReachVehicleId::Sabre:
    case ReachVehicleId::Scorpion:
    case ReachVehicleId::Forklift:
    case ReachVehicleId::Cart:
    case ReachVehicleId::ShadePlasma:
    case ReachVehicleId::ShadeFlak:
    case ReachVehicleId::PlasmaTurret:
    case ReachVehicleId::Machinegun:
    case ReachVehicleId::MacCannon:
    case ReachVehicleId::ScorpionAntiInfantry:
    case ReachVehicleId::Seraph:
    case ReachVehicleId::PelicanChinGun:
    case ReachVehicleId::CorvetteCannon:
    case ReachVehicleId::SpacePhantomChinGun:
    case ReachVehicleId::SpacePhantomBeamTurret:
        return seat == 0;
    default:
        return false;
    }
}

inline constexpr bool ReachVehicleIsAircraft(ReachVehicleId id)
{
    return id == ReachVehicleId::Banshee ||
        id == ReachVehicleId::SpaceBanshee ||
        id == ReachVehicleId::Falcon || id == ReachVehicleId::Sabre ||
        id == ReachVehicleId::Seraph || id == ReachVehicleId::Pelican;
}

// SquadDropPod is a scripted prop, not a turret, but it claims no hull-follow
// frame either, which is exactly this class's behavior. A walk-up tag can
// also ship mounted on a moving carrier (the falcon side guns author
// plasma_turret's exact tuple); the frame builder detects that through the
// engine's own ultimate-parent chain and grants the attached-weapon frame.
inline constexpr bool ReachVehicleIsWalkUpTurret(ReachVehicleId id)
{
    return id == ReachVehicleId::ShadePlasma ||
        id == ReachVehicleId::ShadeFlak ||
        id == ReachVehicleId::PlasmaTurret ||
        id == ReachVehicleId::Machinegun ||
        id == ReachVehicleId::MacCannon ||
        id == ReachVehicleId::CorvetteCannon ||
        id == ReachVehicleId::SquadDropPod;
}

inline constexpr bool ReachVehicleIsAttachedWeapon(ReachVehicleId id)
{
    return id == ReachVehicleId::WraithGunner ||
        id == ReachVehicleId::WarthogChaingun ||
        id == ReachVehicleId::WarthogGauss ||
        id == ReachVehicleId::WarthogRocket ||
        id == ReachVehicleId::ScorpionAntiInfantry ||
        id == ReachVehicleId::PelicanChinGun ||
        id == ReachVehicleId::SpacePhantomChinGun ||
        id == ReachVehicleId::SpacePhantomBeamTurret;
}

// Reach's official vehicle tags put the controllable seat at raw seat 0. An
// attached gun and a walk-up turret also expose seat 0, but that seat aims the
// gun instead of steering a carrier, so neither may take hull steering.
inline constexpr bool ReachVehicleSeatIsDriver(ReachVehicleId id, int seat)
{
    return seat == 0 && id != ReachVehicleId::Unknown &&
        !ReachVehicleIsAttachedWeapon(id) && !ReachVehicleIsWalkUpTurret(id) &&
        id != ReachVehicleId::WraithGunner;
}

// HREK's Reach vehicle definitions divide the same way the native controller
// path does: these drivers turn their carrier through the view/turn channel.
// Wraith and Scorpion keep an independently aimed turret and therefore retain
// the closed-loop hand aim while their hull follows the move stick.
inline constexpr bool ReachVehicleIsLookSteered(ReachVehicleId id)
{
    switch (id)
    {
    case ReachVehicleId::Banshee:
    case ReachVehicleId::SpaceBanshee:
    case ReachVehicleId::Ghost:
    case ReachVehicleId::Revenant:
    case ReachVehicleId::Mongoose:
    case ReachVehicleId::Warthog:
    case ReachVehicleId::Falcon:
    case ReachVehicleId::Sabre:
    case ReachVehicleId::Forklift:
    case ReachVehicleId::Cart:
    case ReachVehicleId::Seraph:
    case ReachVehicleId::CargoTruck:
    case ReachVehicleId::MilitaryTruck:
    case ReachVehicleId::OniVan:
    case ReachVehicleId::Pickup:
    case ReachVehicleId::TruckCabLarge:
        return true;
    default:
        return false;
    }
}

// Hull follow is keyed on the identity's policy class, not on census
// membership: a proven identity can expose more seats than the 25-seat
// census (campaign variants), and those seats ride the same hull. Unknown
// identities claim no hull frame at all.
inline constexpr bool ReachVehicleSeatFollowsHull(
    ReachVehicleId id, int seat)
{
    return id != ReachVehicleId::Unknown && seat >= 0 && seat < 16 &&
        !ReachVehicleIsWalkUpTurret(id);
}

inline constexpr bool ReachVehicleSeatFollowsPitch(
    ReachVehicleId id, int seat)
{
    return ReachVehicleSeatFollowsHull(id, seat) &&
        !ReachVehicleIsAircraft(id);
}

inline constexpr bool ReachVehicleSeatAuthorsSteering(
    ReachVehicleId id, int seat, bool followEnabled)
{
    return followEnabled && ReachVehicleSeatIsDriver(id, seat) &&
        ReachVehicleIsLookSteered(id);
}

inline constexpr bool ReachVehicleUsesWheel(ReachVehicleId id)
{
    return ReachVehicleIsLookSteered(id) && !ReachVehicleIsAircraft(id);
}

// Whether the direct-drive hook's seat-limit latch may read the game's PITCH moves as a limit, by the same
// rule Halo 3 / ODST use (seat_drive::PitchLatches): a look-steered driver's pitch is levelled by the game's
// own update as the vehicle moves (autolevel), which the latch would take for a limit and hold the write
// short of the hand, following the autolevel instead. So that seat's pitch is written as the hand's, never
// latched; its yaw still latches. Every other seat -- gunners, passengers, a tank driver aiming a turret with a
// real pitch stop (Scorpion, Wraith: not look-steered) -- latches both axes. An unresolved seat latches both.
inline constexpr bool ReachSeatPitchLatches(ReachVehicleId id, int seat)
{
    return !(ReachVehicleSeatIsDriver(id, seat) && ReachVehicleIsLookSteered(id));
}

inline constexpr int ReachVehicleExpectedPhysicsType(ReachVehicleId id)
{
    switch (id)
    {
    case ReachVehicleId::Scorpion: return 0;
    case ReachVehicleId::Mongoose:
    case ReachVehicleId::Warthog:
    case ReachVehicleId::Forklift:
    case ReachVehicleId::Cart:
    case ReachVehicleId::CargoTruck:
    case ReachVehicleId::MilitaryTruck:
    case ReachVehicleId::OniVan:
    case ReachVehicleId::Pickup:
    case ReachVehicleId::TruckCabLarge: return 1;
    case ReachVehicleId::Pelican: return 2;
    case ReachVehicleId::Ghost:
    case ReachVehicleId::Revenant:
    case ReachVehicleId::Wraith: return 4;
    case ReachVehicleId::Banshee:
    case ReachVehicleId::SpaceBanshee: return 5;
    case ReachVehicleId::CorvetteCannon: return 6;
    case ReachVehicleId::Falcon: return 8;
    case ReachVehicleId::Sabre:
    case ReachVehicleId::Seraph: return 13;
    case ReachVehicleId::WraithGunner:
    case ReachVehicleId::WarthogChaingun:
    case ReachVehicleId::WarthogGauss:
    case ReachVehicleId::WarthogRocket:
    case ReachVehicleId::ShadePlasma:
    case ReachVehicleId::ShadeFlak:
    case ReachVehicleId::PlasmaTurret:
    case ReachVehicleId::Machinegun:
    case ReachVehicleId::MacCannon:
    case ReachVehicleId::ScorpionAntiInfantry:
    case ReachVehicleId::PelicanChinGun:
    case ReachVehicleId::SpacePhantomChinGun:
    case ReachVehicleId::SpacePhantomBeamTurret:
    case ReachVehicleId::SquadDropPod: return 16;
    default: return -1;
    }
}

// The HREK accessor returns the first populated one of its fifteen physics
// blocks, or 16 when none is authored. Retail's compiled maps report the
// official type-turret block (index 6) for five HREK type-16 weapon identities.
// Tuple and type form one proof: the three changed retail tuples pair only with
// 6, their canonical HREK tuples pair only with 16, and the two bit-identical
// tuples may pair with either evidenced representation.
inline constexpr bool ReachVehiclePhysicsTypeMatches(
    ReachVehicleId id, const ReachVehicleFingerprint& fingerprint,
    int observedType)
{
    switch (id)
    {
    case ReachVehicleId::ShadePlasma:
        return (ReachVehicleFingerprintEqual(
                    fingerprint,
                    {0x3F7A9153, 0x3E1B9697, 0xBC935063, 0x3F1EF5FC}) &&
                observedType == 16) ||
            (ReachVehicleFingerprintEqual(
                    fingerprint,
                    {0x3F7A9153, 0x3E1B9697, 0xBC935062, 0x3F1EF5FC}) &&
                observedType == 6);
    case ReachVehicleId::ShadeFlak:
        return (ReachVehicleFingerprintEqual(
                    fingerprint,
                    {0x3F73045B, 0x3DD8D24F, 0xBC93505D, 0x3F1EA4E9}) &&
                observedType == 16) ||
            (ReachVehicleFingerprintEqual(
                    fingerprint,
                    {0x3F73045A, 0x3DD8D24E, 0xBC93505D, 0x3F1EA4E9}) &&
                observedType == 6);
    case ReachVehicleId::Machinegun:
        return (ReachVehicleFingerprintEqual(
                    fingerprint,
                    {0x3EE99B18, 0x3D9811F2, 0xB9914460, 0x3E8589DB}) &&
                observedType == 16) ||
            (ReachVehicleFingerprintEqual(
                    fingerprint,
                    {0x3EE99B18, 0x3D9811F2, 0xB991445F, 0x3E8589DB}) &&
                observedType == 6);
    case ReachVehicleId::PlasmaTurret:
        return ReachVehicleFingerprintEqual(
                   fingerprint,
                   {0x3F000000, 0x00000000, 0x00000000, 0x3DCCCCCD}) &&
            (observedType == 16 || observedType == 6);
    case ReachVehicleId::ScorpionAntiInfantry:
        return ReachVehicleFingerprintEqual(
                   fingerprint,
                   {0x3ED28405, 0x3DB8AD5D, 0xB8A6B78A, 0x3D8A1BD8}) &&
            (observedType == 16 || observedType == 6);
    default:
    {
        const int expectedType = ReachVehicleExpectedPhysicsType(id);
        if (expectedType < 0 ||
            ReachResolveVehicleFingerprint(fingerprint) != id)
        {
            return false;
        }
        // R-V25, measured at retail: the 2026-08-06 Steam session reported
        // `unmatched vehicle tuple 3F12852A/3DC5E04F/3BE1BCFB/3EA9708D type 6`,
        // which is the canonical HREK WarthogRocket row above, bit for bit.
        // Retail's compiled maps bucket a mounted weapon into the official
        // type-turret block (6) instead of HREK's authored none (16); five
        // identities already carried that exception one at a time. The tuple
        // stays the identity proof - it matches exactly one row - so accepting
        // either evidenced representation cannot misfile one vehicle onto
        // another, and it stops the two Warthog turret variants and every other
        // mounted gun falling to the no-follow/no-steer universal fallback.
        if (expectedType == 16)
            return observedType == 16 || observedType == 6;
        return observedType == expectedType;
    }
    }
}

inline constexpr uint32_t kReachSeatThirdPersonCameraBit = 1u << 4;
// HREK unit_camera_flags_definition bit 2: "hides player-unit from camera".
// The controlling player's camera omits its unit; every other camera sees it.
inline constexpr uint16_t kReachUnitCameraHidePlayerBit = 0x0004;

// R-V20's between-frame seat-bit lease executed but did not hide the Reach
// world biped in the headset, and the user reported vehicle shaking. Body
// hiding is not this bit's job in Reach - R-V22's unit-camera hide-player bit
// owns that and is headset-independent of this one.
inline constexpr bool kReachR_V20SeatBitLeaseEnabled = false;

// R-V26: the same bit, with Halo 3's ACCEPTED C20 lifetime instead of R-V20's.
// It is armed for one job only, the one C20 proved it does: put the occupied
// seat in the engine's own first-person state so Reach builds the occupant's
// first-person arms and weapon. C20's own headset result records exactly that
// - "it also brings up the first-person weapon in `allows weapons` passenger
// seats". Cleared once per occupation and left clear, never toggled per frame.
inline constexpr bool kReachSeatFirstPersonPresentationEnabled = true;

// Body hiding owns one complete seat-flags word between two proven outer
// render callbacks. The runtime keeps this key and the two exact words, never
// a tag pointer: loaded tag storage can move while the Reach module generation
// remains unchanged.
struct ReachSeatLeaseKey
{
    uint32_t generation = 0;
    int32_t unitHandle = -1;
    int32_t directParent = -1;
    uint32_t definitionDatum = 0xFFFFFFFFu;
    int32_t seatIndex = -1;
};

struct ReachSeatLeasePayload
{
    ReachSeatLeaseKey key{};
    uint32_t originalFlags = 0;
    uint32_t writtenFlags = 0;
};

enum class ReachSeatLeaseState : uint32_t
{
    Empty = 0,
    Active,
    RestorePending,
    External,
    Installing,
    CleanupLocked,
};

enum class ReachSeatLeaseRestoreDisposition : uint8_t
{
    AlreadyOriginal = 0,
    RestoreOriginal,
    ExternalWrite,
};

inline constexpr bool ReachSeatLeaseKeyValid(const ReachSeatLeaseKey& key)
{
    return key.generation != 0 && key.unitHandle != -1 &&
        key.directParent != -1 && key.definitionDatum != 0xFFFFFFFFu &&
        key.seatIndex >= 0 && key.seatIndex < 16;
}

inline constexpr bool ReachSeatLeaseKeyEqual(
    const ReachSeatLeaseKey& a, const ReachSeatLeaseKey& b)
{
    return a.generation == b.generation &&
        a.unitHandle == b.unitHandle &&
        a.directParent == b.directParent &&
        a.definitionDatum == b.definitionDatum &&
        a.seatIndex == b.seatIndex;
}

// Native camera-info proves the occupation before its seat-camera pointer is
// usable. A null/torn seatCamera blocks that frame's immersive camera only; it
// must not turn a valid parent+seat relationship into a false OnFoot sample.
inline constexpr bool ReachSeatOccupationObserved(
    int32_t directParent, int32_t seatIndex, int32_t seatLimit) noexcept
{
    return directParent != -1 && seatLimit > 0 &&
        seatIndex >= 0 && seatIndex < seatLimit;
}

// One-shot policy for a full play-space re-centre at a concrete Reach seat
// boundary. The runtime supplies its already-debounced Vehicle state and the
// complete full-salt seat key. A missing/torn frame deliberately leaves the
// last applied key intact, so a transient sample failure cannot repeatedly
// re-centre the player. Commit is separate from NeedsApply: a caller that
// cannot apply the re-centre this frame retries on the next valid frame.
struct ReachSeatRecenterLatch
{
    ReachSeatLeaseKey applied{};
    bool valid = false;

    constexpr void Clear()
    {
        applied = {};
        valid = false;
    }

    constexpr bool NeedsApply(
        bool enabled, bool stableVehicle,
        const ReachSeatLeaseKey* current)
    {
        if (!enabled || !stableVehicle)
        {
            Clear();
            return false;
        }
        if (!current || !ReachSeatLeaseKeyValid(*current))
            return false;
        return !valid || !ReachSeatLeaseKeyEqual(applied, *current);
    }

    constexpr bool Commit(const ReachSeatLeaseKey& current)
    {
        if (!ReachSeatLeaseKeyValid(current))
            return false;
        applied = current;
        valid = true;
        return true;
    }
};

// The entry reset aligns horizontal facing only. A render-matched root/carrier
// can be temporarily missing, non-finite, or nearly vertical; those samples do
// not consume the exact-seat latch and the next valid frame retries.
inline bool ReachVehicleEntryYaw(
    const float forward[3], float& yaw) noexcept
{
    yaw = 0.0f;
    if (!forward || !std::isfinite(forward[0]) ||
        !std::isfinite(forward[1]) || !std::isfinite(forward[2]))
    {
        return false;
    }
    const float horizontalLengthSquared =
        forward[0] * forward[0] + forward[1] * forward[1];
    if (!std::isfinite(horizontalLengthSquared) ||
        horizontalLengthSquared <= 1.0e-8f)
    {
        return false;
    }
    yaw = std::atan2(forward[1], forward[0]);
    return std::isfinite(yaw);
}

// The render thread publishes both yaw references to input as one 64-bit
// value. This prevents a controller poll from combining the new vehicle yaw
// with the previous headset yaw (or vice versa) at an exact-seat reset.
struct ReachYawReferencePair
{
    float gameYaw = 0.0f;
    float headYaw = 0.0f;
};

inline constexpr uint64_t ReachPackYawReferencePair(
    float gameYaw, float headYaw) noexcept
{
    return static_cast<uint64_t>(std::bit_cast<uint32_t>(gameYaw)) |
        (static_cast<uint64_t>(std::bit_cast<uint32_t>(headYaw)) << 32);
}

inline constexpr ReachYawReferencePair ReachUnpackYawReferencePair(
    uint64_t packed) noexcept
{
    return {
        std::bit_cast<float>(static_cast<uint32_t>(packed)),
        std::bit_cast<float>(static_cast<uint32_t>(packed >> 32))};
}

inline constexpr uint64_t kReachInvalidYawReferencePair =
    ReachPackYawReferencePair(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN());

inline bool ReachYawReferencePairValid(
    const ReachYawReferencePair& pair) noexcept
{
    return std::isfinite(pair.gameYaw) && std::isfinite(pair.headYaw);
}

// Installing and CleanupLocked are opposing claims on the SAME atomic. The
// worker must retain the hook for Installing/Active/RestorePending; once it
// wins CleanupLocked, no callback can begin a new mutation and hook removal
// may proceed or retry. Only Active and RestorePending assert that this feature
// currently owns the written word.
inline constexpr bool ReachSeatLeaseRetainsHook(ReachSeatLeaseState state)
{
    return state == ReachSeatLeaseState::Installing ||
        state == ReachSeatLeaseState::Active ||
        state == ReachSeatLeaseState::RestorePending;
}

inline constexpr bool ReachSeatLeaseCanAcquireCleanupLock(
    ReachSeatLeaseState state)
{
    return state == ReachSeatLeaseState::Empty ||
        state == ReachSeatLeaseState::External;
}

inline constexpr bool ReachSeatLeaseCleanupLocked(
    ReachSeatLeaseState state)
{
    return state == ReachSeatLeaseState::CleanupLocked;
}

inline constexpr bool ReachSeatLeaseOwnsMutation(ReachSeatLeaseState state)
{
    return state == ReachSeatLeaseState::Active ||
        state == ReachSeatLeaseState::RestorePending;
}

inline constexpr bool ReachSeatLeaseBlocksKey(
    ReachSeatLeaseState state, const ReachSeatLeaseKey& held,
    const ReachSeatLeaseKey& candidate)
{
    return ReachSeatLeaseCleanupLocked(state) ||
        ReachSeatLeaseRetainsHook(state) ||
        (state == ReachSeatLeaseState::External &&
         ReachSeatLeaseKeyEqual(held, candidate));
}

inline constexpr ReachSeatLeaseRestoreDisposition
ReachClassifySeatLeaseRestore(
    uint32_t currentFlags, uint32_t originalFlags, uint32_t writtenFlags)
{
    if (currentFlags == originalFlags)
        return ReachSeatLeaseRestoreDisposition::AlreadyOriginal;
    if (currentFlags == writtenFlags)
        return ReachSeatLeaseRestoreDisposition::RestoreOriginal;
    return ReachSeatLeaseRestoreDisposition::ExternalWrite;
}

inline constexpr uint16_t ReachUnitCameraHidePlayerFlags(uint16_t flags)
{
    return static_cast<uint16_t>(flags | kReachUnitCameraHidePlayerBit);
}

inline constexpr uint16_t ReachUnitCameraRestorePlayerFlags(
    uint16_t currentFlags, uint16_t originalFlags)
{
    return static_cast<uint16_t>(
        (currentFlags & ~kReachUnitCameraHidePlayerBit) |
        (originalFlags & kReachUnitCameraHidePlayerBit));
}

// R-V1: seat-flags bit 5 is Reach's authored "allows weapons". Only such a
// seat fires the occupant's own weapon from the camera ray; every other seat
// fires from a vehicle barrel and its shot origin must not be re-aimed.
inline constexpr uint32_t kReachSeatAllowsWeaponsBit = 1u << 5;
inline constexpr int kReachVehicleSeatLimit = 16;

// HREK weapons.cpp reads the ordinary unit aiming vector from the live unit
// object at +0x214 when it builds the projectile line (R-V12/R-V18). This is
// engine aim state, not a camera marker or the compact render camera.
inline constexpr uintptr_t kReachUnitAimingVectorOffset = 0x214;

// R-V15's raw-node re-resolve was based on a stale reading of Halo 3 C14.
// Halo 3's accepted follow actually consumes the same live rendered node basis
// as its seat placement. Keeping Reach's simulation-stepped rotation beside
// rendered placement produced the headset-reported View Follow judder in
// 619644c.
// Retain the disproven path behind an explicit dormant switch.
inline constexpr bool kReachR_V15RawHullFollowEnabled = false;

enum class ReachAimFeedbackSource : uint8_t
{
    OnFootCompact = 0,
    SeatedUnitAim,
    SeatedCompactFallback,
};

// View follow is deliberately an input here and deliberately does not select
// the aim source. Both view modes close on the same native unit aim; the option
// controls only how the rendered view follows the carrier.
inline constexpr ReachAimFeedbackSource ReachSelectAimFeedbackSource(
    bool seated, bool unitAimValid, bool /*viewFollowEnabled*/)
{
    if (!seated)
        return ReachAimFeedbackSource::OnFootCompact;
    return unitAimValid ? ReachAimFeedbackSource::SeatedUnitAim
                        : ReachAimFeedbackSource::SeatedCompactFallback;
}

// Only the native unit aiming vector is an engine-authored firing direction.
// The compact seated camera is a useful controller-loop fallback, but must not
// move a reticle that promises where a vehicle weapon is actually pointing.
inline constexpr bool ReachAimFeedbackCanDriveReticle(
    ReachAimFeedbackSource source)
{
    return source == ReachAimFeedbackSource::SeatedUnitAim;
}

// unit+0x214 is the seated UNIT's aim. HREK proves it as the projectile
// direction only when the seat's authored allows-weapons bit makes that unit
// fire its personal weapon. A driver, mounted gunner, attached turret or
// walk-up turret fires from a separate vehicle barrel; using the occupant's
// unit aim as that barrel's reticle line is an unsupported (and headset-
// disproven) substitution.
inline constexpr bool ReachSeatAimCanDriveReticle(
    ReachAimFeedbackSource source, uint32_t seatFlags)
{
    return ReachAimFeedbackCanDriveReticle(source) &&
        (seatFlags & kReachSeatAllowsWeaponsBit) != 0;
}

// R-V25: PROVEN INERT, never armed again without new evidence. The 12a7ee4
// headset run installed this predicate detour at the exact
// render_first_person_view call site and finished a whole session - including
// a Warthog passenger seat - with ZERO admissions and no passenger hands. The
// substitution can only act when the stock word is not NONE, so either the
// word was already NONE or the wrapper is never reached for a seated player;
// both mean the first-person models are suppressed somewhere upstream of this
// call. Retained as evidence, kept out of the render hot path.
inline constexpr bool kReachR_V24PassengerRenderAdmissionEnabled = false;

// Reach's render_first_person_view wrapper skips both native first-person
// passes when its output-user predicate is not NONE.  A VR-owned
// allows-weapons passenger needs that rendering, but no driver, mounted gun,
// on-foot frame, camera/control consumer, or already-admitted stock frame does.
inline constexpr bool ReachPassengerNeedsFirstPersonRenderAdmission(
    bool ownerActive, bool vehicleViewApplied, bool vehicleFrameActive,
    bool vehicleFirstPerson, uint32_t seatFlags, uint16_t stockGate)
{
    return ownerActive && vehicleViewApplied && vehicleFrameActive &&
        vehicleFirstPerson && stockGate != 0xFFFFu &&
        (seatFlags & kReachSeatAllowsWeaponsBit) != 0;
}

// R-V22 deliberately keeps the floating controller reticle for every Reach
// vehicle seat. The occupant's +0x214 aim is not the selected vehicle barrel's
// line; the native unit-adjust transaction redirects the local barrel's
// central pre-spread direction through the presented controller sight instead.
inline constexpr bool kReachR_V22NativeVehicleReticleEnabled = false;
inline constexpr uint64_t kReachShotDirectionFreshMs = 50;
inline constexpr float kReachShotDirectionMaxDistanceSq =
    2048.0f * 2048.0f;

struct ReachShotDirectionSample
{
    uint32_t currentGeneration = 0;
    uint64_t nowMs = 0;
    uint64_t occupationSampleMs = 0;
    uint64_t targetSampleMs = 0;
    bool active = false;
    int32_t firingUnitIndex = -1;
    ReachSeatLeaseKey occupation{};
    ReachSeatLeaseKey targetKey{};
    float origin[3]{};
    float target[3]{};
};

inline bool ReachShotDirectionSampleAdmitted(
    const ReachShotDirectionSample& sample)
{
    if (!sample.currentGeneration || !sample.active ||
        sample.firingUnitIndex == -1 ||
        !ReachSeatLeaseKeyValid(sample.occupation) ||
        !ReachSeatLeaseKeyValid(sample.targetKey) ||
        !ReachSeatLeaseKeyEqual(sample.occupation, sample.targetKey) ||
        sample.occupation.generation != sample.currentGeneration ||
        sample.firingUnitIndex != sample.occupation.unitHandle ||
        !sample.occupationSampleMs || !sample.targetSampleMs ||
        sample.nowMs < sample.occupationSampleMs ||
        sample.nowMs < sample.targetSampleMs ||
        sample.nowMs - sample.occupationSampleMs >
            kReachShotDirectionFreshMs ||
        sample.nowMs - sample.targetSampleMs >
            kReachShotDirectionFreshMs)
    {
        return false;
    }

    float distanceSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(sample.origin[axis]) ||
            !std::isfinite(sample.target[axis]))
        {
            return false;
        }
        const float delta = sample.target[axis] - sample.origin[axis];
        distanceSquared += delta * delta;
    }
    return std::isfinite(distanceSquared) &&
        distanceSquared > 1.0e-8f &&
        distanceSquared <= kReachShotDirectionMaxDistanceSq;
}

inline bool ReachNormalizeUnitAimingVector(
    const float input[3], float output[3])
{
    if (!input || !output)
        return false;
    float lengthSquared = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(input[i]))
            return false;
        lengthSquared += input[i] * input[i];
    }
    if (!std::isfinite(lengthSquared) ||
        lengthSquared < 0.25f || lengthSquared > 4.0f)
    {
        return false;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    for (int i = 0; i < 3; ++i)
    {
        output[i] = input[i] * inverseLength;
        if (!std::isfinite(output[i]))
            return false;
    }
    return true;
}

// Convert Reach world aim into the FINAL compact-camera basis captured for a
// completed stereo pair. OpenXR camera-local axes are +X right, +Y up and -Z
// forward. Reach stores world forward and up, so forward x up is the matching
// +X right axis. This removes every yaw-reference, pitch-trim and view-follow
// inversion from the compositor hand-off: those choices are already present
// in the final camera basis.
inline bool ReachWorldAimToCameraLocal(
    const float worldAim[3], const float cameraForward[3],
    const float cameraUp[3], float outLocal[3])
{
    if (!worldAim || !cameraForward || !cameraUp || !outLocal)
        return false;

    float normalizedAim[3]{};
    if (!ReachNormalizeUnitAimingVector(worldAim, normalizedAim))
        return false;

    float forwardLengthSquared = 0.0f;
    float upLengthSquared = 0.0f;
    float forwardUpDot = 0.0f;
    for (int component = 0; component < 3; ++component)
    {
        if (!std::isfinite(cameraForward[component]) ||
            !std::isfinite(cameraUp[component]))
        {
            return false;
        }
        forwardLengthSquared +=
            cameraForward[component] * cameraForward[component];
        upLengthSquared += cameraUp[component] * cameraUp[component];
        forwardUpDot += cameraForward[component] * cameraUp[component];
    }
    // Validate the same kind of normalized, orthogonal basis the compact
    // camera validator admits. This helper remains self-contained so a future
    // caller cannot bypass those preconditions accidentally.
    constexpr float kAxisTolerance = 0.002f;
    if (!std::isfinite(forwardLengthSquared) ||
        !std::isfinite(upLengthSquared) || !std::isfinite(forwardUpDot) ||
        std::fabs(forwardLengthSquared - 1.0f) >= kAxisTolerance ||
        std::fabs(upLengthSquared - 1.0f) >= kAxisTolerance ||
        std::fabs(forwardUpDot) >= kAxisTolerance)
    {
        return false;
    }

    const float right[3] = {
        cameraForward[1] * cameraUp[2] -
            cameraForward[2] * cameraUp[1],
        cameraForward[2] * cameraUp[0] -
            cameraForward[0] * cameraUp[2],
        cameraForward[0] * cameraUp[1] -
            cameraForward[1] * cameraUp[0]};
    const float mapped[3] = {
        normalizedAim[0] * right[0] +
            normalizedAim[1] * right[1] +
            normalizedAim[2] * right[2],
        normalizedAim[0] * cameraUp[0] +
            normalizedAim[1] * cameraUp[1] +
            normalizedAim[2] * cameraUp[2],
        -(normalizedAim[0] * cameraForward[0] +
          normalizedAim[1] * cameraForward[1] +
          normalizedAim[2] * cameraForward[2])};
    return ReachNormalizeUnitAimingVector(mapped, outLocal);
}

// Plain-data identity and direction for the reticle publication. Every
// completed pair writes one of these, including an on-foot/fallback sample
// which deliberately revokes native reticle ownership for that exact serial.
struct ReachReticleAimSample
{
    uint32_t generation = 0;
    uint64_t preparedSerial = 0;
    ReachAimFeedbackSource source = ReachAimFeedbackSource::OnFootCompact;
    ReachSeatLeaseKey occupation{};
    float cameraLocalDirection[3]{0.0f, 0.0f, -1.0f};
};

inline bool ReachReticleAimSampleAdmitted(
    const ReachReticleAimSample& sample, uint32_t currentGeneration,
    uint64_t expectedSerial)
{
    if (!currentGeneration || !expectedSerial ||
        sample.generation != currentGeneration ||
        sample.preparedSerial != expectedSerial ||
        !ReachAimFeedbackCanDriveReticle(sample.source) ||
        !ReachSeatLeaseKeyValid(sample.occupation) ||
        sample.occupation.generation != sample.generation)
    {
        return false;
    }
    float normalized[3]{};
    return ReachNormalizeUnitAimingVector(
        sample.cameraLocalDirection, normalized);
}

// A failed/partial retry of one prepared serial must leave the earlier
// completed pair intact. A later completed pair always replaces it, even when
// its source is a fallback: otherwise a same-serial seat/source transition
// could resurrect native aim from the earlier pair.
inline constexpr ReachReticleAimSample ReachReticleAimSampleAfterAttempt(
    const ReachReticleAimSample& current,
    const ReachReticleAimSample& candidate,
    bool completedStereoPair)
{
    return completedStereoPair ? candidate : current;
}

// A personal weapon may borrow the completed rendered eye only while the
// current seat publication and completed-eye publication describe the same
// full salted occupation. Keep this policy platform-neutral and testable: the
// firing detour itself then performs only coherent seqlock reads and stores.
inline constexpr uint64_t kReachShotOriginFreshMs = 100;

struct ReachShotOriginSample
{
    uint32_t currentGeneration = 0;
    uint64_t nowMs = 0;
    uint64_t occupationSampleMs = 0;
    uint64_t renderedEyeSampleMs = 0;
    uint64_t renderedEyePreparedSerial = 0;
    bool active = false;
    bool allowsWeapons = false;
    int32_t firingUnitIndex = -1;
    ReachSeatLeaseKey occupation{};
    ReachSeatLeaseKey renderedEye{};
    float renderedEyePosition[3]{};
};

inline bool ReachShotOriginSampleAdmitted(
    const ReachShotOriginSample& sample)
{
    if (!sample.currentGeneration || !sample.active ||
        !sample.allowsWeapons ||
        !ReachSeatLeaseKeyValid(sample.occupation) ||
        !ReachSeatLeaseKeyValid(sample.renderedEye) ||
        !ReachSeatLeaseKeyEqual(sample.occupation, sample.renderedEye) ||
        sample.occupation.generation != sample.currentGeneration ||
        sample.renderedEye.generation != sample.currentGeneration ||
        !sample.renderedEyePreparedSerial || !sample.occupationSampleMs ||
        !sample.renderedEyeSampleMs ||
        sample.nowMs < sample.occupationSampleMs ||
        sample.nowMs < sample.renderedEyeSampleMs ||
        sample.nowMs - sample.occupationSampleMs >
            kReachShotOriginFreshMs ||
        sample.nowMs - sample.renderedEyeSampleMs >
            kReachShotOriginFreshMs ||
        ((sample.occupation.unitHandle ^ sample.firingUnitIndex) & 0xFFFF) != 0)
    {
        return false;
    }
    return std::isfinite(sample.renderedEyePosition[0]) &&
        std::isfinite(sample.renderedEyePosition[1]) &&
        std::isfinite(sample.renderedEyePosition[2]);
}

struct ReachStereoCenterPose
{
    // Component order matches OpenXR: x, y, z, w.
    float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float position[3]{};
};

// Build the pose used for BOTH native-aim rotation and reticle-ray origin.
// Quaternion sign is aligned before averaging because q and -q are the same
// rotation. Eye orientations are normalized first; invalid/ambiguous input
// fails open to the existing controller/clamp reticle.
inline bool ReachBuildStereoCenterPose(
    const float leftQuaternion[4], const float leftPosition[3],
    const float rightQuaternion[4], const float rightPosition[3],
    ReachStereoCenterPose& out)
{
    if (!leftQuaternion || !leftPosition ||
        !rightQuaternion || !rightPosition)
    {
        return false;
    }

    float leftLengthSquared = 0.0f;
    float rightLengthSquared = 0.0f;
    for (int component = 0; component < 3; ++component)
    {
        if (!std::isfinite(leftPosition[component]) ||
            !std::isfinite(rightPosition[component]))
        {
            return false;
        }
        out.position[component] =
            (leftPosition[component] + rightPosition[component]) * 0.5f;
        if (!std::isfinite(out.position[component]))
            return false;
    }
    for (int component = 0; component < 4; ++component)
    {
        if (!std::isfinite(leftQuaternion[component]) ||
            !std::isfinite(rightQuaternion[component]))
        {
            return false;
        }
        leftLengthSquared +=
            leftQuaternion[component] * leftQuaternion[component];
        rightLengthSquared +=
            rightQuaternion[component] * rightQuaternion[component];
    }
    if (!std::isfinite(leftLengthSquared) ||
        !std::isfinite(rightLengthSquared) ||
        leftLengthSquared <= 1.0e-8f || rightLengthSquared <= 1.0e-8f)
    {
        return false;
    }

    const float inverseLeftLength = 1.0f / std::sqrt(leftLengthSquared);
    const float inverseRightLength = 1.0f / std::sqrt(rightLengthSquared);
    float left[4]{};
    float right[4]{};
    float dot = 0.0f;
    for (int component = 0; component < 4; ++component)
    {
        left[component] =
            leftQuaternion[component] * inverseLeftLength;
        right[component] =
            rightQuaternion[component] * inverseRightLength;
        dot += left[component] * right[component];
    }
    if (!std::isfinite(dot) || std::fabs(dot) <= 1.0e-6f)
        return false;

    const float rightSign = dot < 0.0f ? -1.0f : 1.0f;
    float centerLengthSquared = 0.0f;
    for (int component = 0; component < 4; ++component)
    {
        out.orientation[component] =
            left[component] + right[component] * rightSign;
        centerLengthSquared +=
            out.orientation[component] * out.orientation[component];
    }
    if (!std::isfinite(centerLengthSquared) ||
        centerLengthSquared <= 1.0e-8f)
    {
        return false;
    }
    const float inverseCenterLength =
        1.0f / std::sqrt(centerLengthSquared);
    for (float& component : out.orientation)
    {
        component *= inverseCenterLength;
        if (!std::isfinite(component))
            return false;
    }
    return true;
}

// HREK's generic datum collection and tag-block layouts, matched to the pinned
// retail homologs. These bounds are consumed only from Reach's proven render
// thread; any failed validation leaves the optional vehicle frame stock.
inline constexpr uintptr_t kReachTlsObjectCollectionOffset = 0x10;
inline constexpr uintptr_t kReachDataCollectionStrideOffset = 0x20;
inline constexpr uintptr_t kReachDataCollectionInitializedOffset = 0x31;
inline constexpr uintptr_t kReachDataCollectionCountOffset = 0x44;
inline constexpr uintptr_t kReachDataCollectionEntriesOffset = 0x50;
inline constexpr size_t kReachObjectEntryStride = 0x18;
inline constexpr uintptr_t kReachObjectEntrySaltOffset = 0x00;
inline constexpr uintptr_t kReachObjectEntryKindOffset = 0x04;
inline constexpr uintptr_t kReachObjectEntryDataOffset = 0x10;
inline constexpr uintptr_t kReachObjectDefinitionDatumOffset = 0x00;
inline constexpr uintptr_t kReachVehicleSeatsBlockOffset = 0x444;
inline constexpr size_t kReachVehicleSeatStride = 0x12C;
inline constexpr uintptr_t kReachSeatFlagsOffset = 0x00;
inline constexpr uintptr_t kReachSeatAttachmentMarkerOffset = 0x08;
inline constexpr uintptr_t kReachSeatCameraOffset = 0x70;
inline constexpr uintptr_t kReachSeatCameraMarkerOffset = 0x74;
inline constexpr uintptr_t kReachUnitCameraFlagsOffset = 0x00;
// The pinned retail consumer at +0x1DD3B2 tests this exact byte mask.
inline constexpr uint32_t kReachOccupantHeadMarkerStringId = 0xC2;

struct ReachSeatCameraBasis
{
    float scale = 1.0f;
    float forward[3]{};
    float left[3]{};
    float up[3]{};
};

inline bool ReachSeatCameraBasisValid(const ReachSeatCameraBasis& basis)
{
    if (!std::isfinite(basis.scale) || basis.scale <= 1.0e-5f ||
        basis.scale > 1.0e3f)
    {
        return false;
    }
    for (const float* axis : {basis.forward, basis.left, basis.up})
    {
        float lengthSquared = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(axis[i]))
                return false;
            lengthSquared += axis[i] * axis[i];
        }
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.25f ||
            lengthSquared > 4.0f)
            return false;
    }
    return true;
}

// Config axes match the authoring kit: +forward, +right, +up. Reach exposes a
// left axis in its real_matrix4x3, so right is the negated left column.
inline bool ReachComposeSeatCameraPoint(
    const float base[3], const ReachSeatCameraBasis& basis,
    float forwardUnits, float rightUnits, float upUnits, float out[3])
{
    if (!base || !out || !ReachSeatCameraBasisValid(basis) ||
        !std::isfinite(forwardUnits) || !std::isfinite(rightUnits) ||
        !std::isfinite(upUnits))
        return false;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(base[i]))
            return false;
        out[i] = base[i] + basis.scale *
            (basis.forward[i] * forwardUnits -
             basis.left[i] * rightUnits + basis.up[i] * upUnits);
        if (!std::isfinite(out[i]))
            return false;
    }
    return true;
}

// Menu readers need only identity and raw seat; render-thread transforms stay
// in their bounded same-thread transaction. The generation in the upper half
// prevents a stale Reach module from naming a seat after title teardown.
// An unmatched (generic) vehicle publishes the sentinel identity code so the
// consumer chain (FpActive, recenter, body hide) still sees a live seat while
// every identity-keyed decoder fail-closes: ReachCurrentSeat rejects codes
// above kReachVehicleIdentityCount, which keeps wheel/steering off.
// R-V25: ReachVehicleTrimSnapshotSlot no longer fails closed. It maps the
// sentinel onto one dedicated unmatched trim row, because binding the F1
// sliders to the shared universal trim while the player was sitting in a seat
// is what let one session's turret tuning move every other seat in all three
// titles.
inline constexpr uint8_t kReachVehicleGenericIdentityCode = 0xFF;

inline constexpr uint64_t ReachVehicleTrimSnapshot(
    uint32_t generation, ReachVehicleId id, int seat)
{
    return generation && seat >= 0 && seat < 16
        ? (static_cast<uint64_t>(generation) << 32) |
              (static_cast<uint64_t>(
                   id != ReachVehicleId::Unknown
                       ? static_cast<uint8_t>(id)
                       : kReachVehicleGenericIdentityCode) << 8) |
              static_cast<uint8_t>(seat + 1)
        : 0;
}

inline constexpr int ReachVehicleTrimSnapshotSlot(
    uint64_t snapshot, uint32_t generation, int seatsPerVehicle)
{
    if (!generation || static_cast<uint32_t>(snapshot >> 32) != generation ||
        seatsPerVehicle <= 0)
        return -1;
    const int id = static_cast<int>((snapshot >> 8) & 0xFFu);
    const int seat = static_cast<int>(snapshot & 0xFFu) - 1;
    if (seat < 0 || seat >= seatsPerVehicle)
        return -1;
    if (id == static_cast<int>(kReachVehicleGenericIdentityCode))
        return kReachVehicleIdentityCount * seatsPerVehicle + seat;
    if (id <= 0 || id > kReachVehicleIdentityCount)
        return -1;
    return (id - 1) * seatsPerVehicle + seat;
}
