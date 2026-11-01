#pragma once

// WHAT THE INTENT RAY CAN HIT. The convergence range is only as good as the trace that measures
// it: a query whose flags leave out bipeds or vehicles still returns plausible ranges off the wall BEHIND the
// target, and the aim then converges far while the enemy stands at 5 m (Reach's most frequent caller is AI
// line of sight, whose object mask omits bipeds). This header is the pure half of the answer:
//
//   * each title's collision-flag LAYOUT, decoded from that title's own collision code, so a flags value can be
//     turned into "structure / bipeds / vehicles / other objects" (DecodeTraceFlags) -- for the one-time log line
//     every build prints, and for the unit test that holds each title's shipped flags to "bipeds and vehicles";
//   * the class of one trace result (ClassifyHit), for the dev counters;
//   * which of our own vehicle's parts a seated trace may look past (LookPastOwnHierarchy);
//   * Halo 2's projectile-flags value agreement (Halo2ProjectileFlagsAgree).
//
// Nothing here chooses what a title traces with: the flags stay each title's own resolved value. The layouts
// are read-only descriptions for logs and tests, so a wrong one mislabels a line and never moves an aim.
//
// THE LAYOUTS, and where each is read (static disassembly of the retail modules; the RVAs are only
// where the code sits in that build, recorded for the next reader -- nothing dereferences them):
//
//   Halo CE (halo1.dll collision test 0xB913FC, its object test 0xB9105C; the names are HCEEK's collisions.h):
//     one dword. bit 5 structure, bit 7 objects, bits 8-19 the 12 object types (bit = type + 8). No type bit set
//     means every type: `mov ecx,0xFFF00; or r14d,ecx; and eax,ecx; cmovne r14d,edi` at 0xB91854. The object
//     test skips the ignored object WITH its children, a hidden object, and a type whose bit is clear
//     (`bt flags, type+8`). It has no dead-biped test: a body is hit, as the game's own projectiles hit it.
//   Halo 2 (halo2.dll collision_test_vector 0x75B850, per-object test 0x75C500):
//     one dword. bit 0 structure, bit 3 objects, bits 4-16 the 13 object types (bit = type + 4). No type bit
//     set means every type: `test edi,0x1FFF0; jne; or esi,0x1FFF0` at 0x75BB12. Per object: bit 17 skips
//     children; bit 19 / 18 / 30 test object flags; for a biped, bit 20 skips one whose data +0x10A has bit 2
//     (by position and by CE's source the damage flags' dead bit; the name is inferred) and bit 22 one whose
//     +0x390 has bit 3.
//     The ignored objects are skipped WITH their children.
//   Halo 3 (halo3.dll collision_test_vector 0x1FD748, ray callback 0x39D314, per-object predicate 0x1FC430),
//   ODST (halo3odst.dll 0x22F80C, 0x3E1A84, 0x22E4F4), Halo 4 (halo4.dll PhysicsRayCast 0x1C1D4C, its
//   collector conversion 0x1C15A8, predicate 0x1BFC24), Reach (haloreach.dll 0x12969C; HREK names the halves):
//     a qword: low dword the collision flags (bit 0 structure), high dword the object flags. Object-flags bit 0
//     is "objects"; bit (type + 1) is one object type. No type bit set means every type: the function ORs in a
//     CRT-built "all objects" value -- Halo 3 {0, 0x7FFE} (initializer 0x575C), ODST {0, 0xFFFE} (0x5A1C),
//     Reach {0, 0x7FFE} (0x4804), Halo 4 {0, 0x1FFFE} (0x1D734) -- so 14, 15, 14 and 16 types.
//     The predicate then tests `bt object_flags, type + 1`, and these collision-flag bits cull objects:
//       Halo 3 / ODST: 6, 7, 8, 18, 20, 21 (object flags); 9 a biped (data +0x110 / +0x104 bit 2); 10 and 19
//                      one other type each; 5 skips children.
//       Halo 4:        6, 7, 8, 19, 21, 22, 27 (object flags); 9 and 10 a biped; 28 a vehicle; 11 and 20 one
//                      other type each.
//       Reach:         its object predicate is not decoded (the collision code is laid out differently);
//                      the flags are HREK's and hit a marine 65/65 (REACH-AIM-CONVERGENCE-2026-09-28.md).
//     THE IGNORED OBJECTS (Halo 3, ODST, Halo 4; all three slots / every list entry are compared alike): an
//     object whose parent chain reaches an ignored object is skipped too -- the held weapon, an attached
//     turret, the other occupants -- EXCEPT a biped, vehicle or giant sitting in a seat of its parent whose
//     definition names a boarding seat (seat +0x42 in Halo 3 / ODST, +0x4A in Halo 4): a BOARDER stays
//     hittable, so a player can shoot a hijacker off his own vehicle.
//
// Object type 0 is a biped and type 1 a vehicle in every one of the six: each predicate applies its biped tests
// to type 0 (Halo 2, Halo 3, ODST, Halo 4), CE's applies vehicle physics to type 1 and Halo 4's its vehicle
// test, and the Halo 3 family's boarder exception names types 0, 1 and the giant (mask 0x1003 / 0x2003).
// ADDR-HYGIENE: structural (bit positions inside the engine's own flags words, read from the code that tests
// them; used for log text and unit tests only -- no address, no write, no trace input depends on them).

#include <cstdint>

namespace sight_hit
{
enum class Engine : uint8_t { HaloCE = 0, Halo2, Halo3, ODST, Reach, Halo4, Count };

inline constexpr const char* EngineName(Engine engine)
{
    switch (engine)
    {
    case Engine::HaloCE: return "Halo CE";
    case Engine::Halo2: return "Halo 2";
    case Engine::Halo3: return "Halo 3";
    case Engine::ODST: return "ODST";
    case Engine::Reach: return "Reach";
    case Engine::Halo4: return "Halo 4";
    default: return "?";
    }
}

inline constexpr int kObjectTypeBiped = 0;
inline constexpr int kObjectTypeVehicle = 1;

struct FlagLayout
{
    uint8_t structureBit;       // in the whole flags value (a qword for the Halo 3 family)
    uint8_t objectsBit;
    uint8_t firstTypeBit;       // the bit of object type 0
    uint8_t typeCount;
    uint64_t objectCullBits;    // collision-flag bits that skip objects of any type by an object flag
    uint64_t bipedCullBits;     // ... that skip some bipeds (dead, passing through, ...)
    uint64_t vehicleCullBits;   // ... that skip some vehicles
    bool cullsDecoded;          // false: the predicate was not read for this title (Reach)
};

inline constexpr FlagLayout Layout(Engine engine)
{
    switch (engine)
    {
    case Engine::HaloCE:
        return {5, 7, 8, 12, 0, 0, 0, true};
    case Engine::Halo2:
        return {0, 3, 4, 13, (1ull << 18) | (1ull << 19) | (1ull << 30), (1ull << 20) | (1ull << 22), 0, true};
    case Engine::Halo3:
        return {0, 32, 33, 14, (1ull << 6) | (1ull << 7) | (1ull << 8) | (1ull << 18) | (1ull << 20) | (1ull << 21),
                1ull << 9, 0, true};
    case Engine::ODST:
        return {0, 32, 33, 15, (1ull << 6) | (1ull << 7) | (1ull << 8) | (1ull << 18) | (1ull << 20) | (1ull << 21),
                1ull << 9, 0, true};
    case Engine::Reach:
        return {0, 32, 33, 14, 0, 0, 0, false};
    case Engine::Halo4:
        return {0, 32, 33, 16,
                (1ull << 6) | (1ull << 7) | (1ull << 8) | (1ull << 19) | (1ull << 21) | (1ull << 22) | (1ull << 27),
                (1ull << 9) | (1ull << 10), 1ull << 28, true};
    default:
        return {};
    }
}

// What a flags value lets a trace hit.
struct FlagClasses
{
    uint64_t flags = 0;
    bool structure = false;
    bool objects = false;        // the "objects" bit
    bool allTypes = false;       // objects with no type bit: the engine widens it to every type
    uint32_t typeMask = 0;       // after that widening: bit n = object type n is tested
    bool bipeds = false;
    bool vehicles = false;
    bool otherObjects = false;   // at least one type that is neither
    // The culls in force (raw flag bits): some objects of a tested class are still passed through.
    uint64_t objectCulls = 0, bipedCulls = 0, vehicleCulls = 0;
    bool cullsDecoded = false;
};

inline constexpr FlagClasses DecodeTraceFlags(Engine engine, uint64_t flags)
{
    const FlagLayout layout = Layout(engine);
    FlagClasses out{};
    out.flags = flags;
    if (!layout.typeCount)
        return out;
    const uint32_t every = (1u << layout.typeCount) - 1u;
    out.structure = ((flags >> layout.structureBit) & 1u) != 0;
    out.objects = ((flags >> layout.objectsBit) & 1u) != 0;
    const uint32_t named = static_cast<uint32_t>(flags >> layout.firstTypeBit) & every;
    out.allTypes = out.objects && !named;
    out.typeMask = out.objects ? (named ? named : every) : 0u;
    out.bipeds = ((out.typeMask >> kObjectTypeBiped) & 1u) != 0;
    out.vehicles = ((out.typeMask >> kObjectTypeVehicle) & 1u) != 0;
    out.otherObjects = (out.typeMask & ~((1u << kObjectTypeBiped) | (1u << kObjectTypeVehicle))) != 0;
    out.cullsDecoded = layout.cullsDecoded;
    out.objectCulls = flags & layout.objectCullBits;
    out.bipedCulls = flags & layout.bipedCullBits;
    out.vehicleCulls = flags & layout.vehicleCullBits;
    return out;
}

// The convergence requirement: a ray traced with these flags stops on the world, on a character and on a
// vehicle. (Culls are the engine's own exceptions inside a class and do not fail it.)
inline constexpr bool HitsUnitsAndVehicles(const FlagClasses& classes)
{
    return classes.structure && classes.bipeds && classes.vehicles;
}

// ---- one trace result -------------------------------------------------------------------------------------
enum class HitClass : uint8_t
{
    Miss = 0,        // nothing within the trace: the far end
    Structure,       // the level: BSP, instanced geometry, water
    Biped,           // a character, enemy or ally, alive or -- where the engine's own query hits them -- dead
    Vehicle,
    OtherObject,     // any other object type: a crate, scenery, a machine, a weapon on the ground, a giant
    UnknownObject,   // an object whose type could not be read (a stale handle)
    Count
};

inline constexpr const char* HitClassName(HitClass hit)
{
    switch (hit)
    {
    case HitClass::Miss: return "miss";
    case HitClass::Structure: return "structure";
    case HitClass::Biped: return "biped";
    case HitClass::Vehicle: return "vehicle";
    case HitClass::OtherObject: return "other object";
    case HitClass::UnknownObject: return "object of unknown type";
    default: return "?";
    }
}

// `objectHit`: the result names an object (the title's own "object" result type, or a valid object index).
// `objectType`: that object's type from the title's object table, or -1 when it could not be read.
inline constexpr HitClass ClassifyHit(bool hit, bool objectHit, int objectType)
{
    if (!hit)
        return HitClass::Miss;
    if (!objectHit)
        return HitClass::Structure;
    if (objectType == kObjectTypeBiped)
        return HitClass::Biped;
    if (objectType == kObjectTypeVehicle)
        return HitClass::Vehicle;
    return objectType < 0 ? HitClass::UnknownObject : HitClass::OtherObject;
}

// ---- a seated trace's look-past ---------------------------------------------------------------------------
// A seated ray starts at the rendered eye INSIDE our vehicle, so a hit on a part of our own vehicle is not a
// target and the trace continues past it. Halo 3, ODST and Halo 4 already skip an ignored object's whole
// hierarchy inside the engine (above) with ONE exception the engine makes on purpose: a boarder. So a BIPED the
// engine returns from our own hierarchy is an enemy hanging on our vehicle at arm's length -- the nearest target
// there is -- and looking past it converges the aim on whatever stands behind him. It is kept as the hit.
// `sameRoot`: the hit object's parent chain ends at our vehicle's root (or it is one of the seat's own handles);
// `isOwnUnit`: it is the local unit itself; `objectType`: -1 unknown.
inline constexpr bool LookPastOwnHierarchy(bool sameRoot, bool isOwnUnit, int objectType)
{
    if (!sameRoot)
        return false;
    if (isOwnUnit)
        return true;
    return objectType != kObjectTypeBiped;
}

// ---- Halo 2: the projectile's own flags -------------------------------------------------------------------
// Halo 2's projectiles test their flight through one small wrapper (halo2.dll 0x75A110) that passes an immediate
// to collision_test_vector; the engine-ray census saw it on every shot (caller +0x75A18A, flags 0x2490000F,
// ALL-TITLES-AIM-2026-09-29.md). That is the world-collision / melee query's 0x2480000F plus bit 20 alone: the
// bipeds that bit skips (a dead one, by the layout above) are passed through, as the game's own rounds pass. The
// trace uses the value read from that wrapper only when it is exactly this: the lane's own flags plus that one bit.
// Anything else (a patch that re-meant a bit, a different wrapper matched) keeps the lane's flags.
// ADDR-HYGIENE: guarded (the value is read from the matched instruction and must agree with a second,
// independently pinned value -- the world-collision flags -- up to the one decoded bit).
inline constexpr uint32_t kHalo2DeadBipedCullBit = 1u << 20;
inline constexpr bool Halo2ProjectileFlagsAgree(uint32_t projectileFlags, uint32_t worldCollisionFlags)
{
    return projectileFlags == (worldCollisionFlags | kHalo2DeadBipedCullBit) &&
           !(worldCollisionFlags & kHalo2DeadBipedCullBit) &&
           HitsUnitsAndVehicles(DecodeTraceFlags(Engine::Halo2, projectileFlags));
}

// ---- the dev window (one writer: the title's trace thread; the cold worker drains it) ---------------------
struct HitWindowClass
{
    uint32_t count = 0;
    float nearWu = 0.0f, farWu = 0.0f;
    int lastType = -1;   // the object type of the last hit of this class (objects only)
};

inline constexpr void NoteHit(HitWindowClass& window, float rangeWu, int objectType)
{
    if (!window.count || rangeWu < window.nearWu)
        window.nearWu = rangeWu;
    if (!window.count || rangeWu > window.farWu)
        window.farWu = rangeWu;
    ++window.count;
    window.lastType = objectType;
}
} // namespace sight_hit
