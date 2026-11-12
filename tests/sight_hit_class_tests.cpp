// What the intent ray can hit (src/common/sight_hit_class_logic.h): each title's trace flags decoded with that
// title's own bit layout into structure / bipeds / vehicles / other objects; the class of one trace result; which
// parts of our own vehicle a seated trace looks past; and Halo 2's projectile-flags value agreement.
//
// THE TABLE BELOW IS THE REQUIREMENT: every title's intent ray must stop on a character and on a vehicle, or the
// converged aim reads the wall behind the target. The controls are each engine's own LINE-OF-SIGHT flags, whose
// object mask leaves bipeds out -- the decode must say so, or it would pass a trace that sees through enemies.
#include "../src/common/sight_hit_class_logic.h"
#include <cstdio>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using sight_hit::Engine;
using sight_hit::HitClass;

struct Shipped
{
    Engine engine;
    uint64_t flags;
    unsigned typeCount;
    const char* what;
};
// The flags each title's intent ray is traced with: the value each title's resolver value-checks for (the dll
// static_asserts its own constants against the same decode where it defines them), and each title's fallback.
constexpr Shipped kShipped[]{
    {Engine::HaloCE, 0x1000E9ull, 12, "Halo CE: _collision_test_for_projectiles_flags"},
    {Engine::Halo2, 0x2490000Full, 13, "Halo 2: the projectile flight query's flags"},
    {Engine::Halo2, 0x2480000Full, 13, "Halo 2: the world-collision flags (the fallback)"},
    {Engine::Halo3, 0x100226809ull, 14, "Halo 3: the crosshair trace's global | 0x202000"},
    {Engine::ODST, 0x100226809ull, 15, "ODST: the crosshair trace's global | 0x202000"},
    {Engine::Reach, 0x100226809ull, 14, "Reach: the crosshair trace's global | 0x202000"},
    {Engine::Halo4, 0x10044D409ull, 16, "Halo 4: the shot aim-assist query's global | 0x404400"},
    {Engine::Halo4, 0x100049009ull, 16, "Halo 4: the melee query's flags (the fallback)"},
};

// Each engine's own "does anything block the view" flags: objects, with a type mask that leaves bipeds out.
constexpr Shipped kLineOfSight[]{
    {Engine::HaloCE, 0xC2ADull, 12, "Halo CE: _collision_test_for_line_of_sight_flags"},
    {Engine::ODST, 0x530500000000ull, 15, "ODST: the sight-blocking object mask 0x5305"},
    {Engine::Reach, 0x29850001F809ull, 14, "Reach: ai_test_line_of_sight"},
    {Engine::Halo4, 0x51850002D009ull, 16, "Halo 4: the clear-line wrapper's filter"},
};

// Compile-time: the decode is usable in constant expressions, and the headline facts hold.
static_assert(sight_hit::HitsUnitsAndVehicles(sight_hit::DecodeTraceFlags(Engine::Halo3, 0x100226809ull)));
static_assert(!sight_hit::DecodeTraceFlags(Engine::Reach, 0x29850001F809ull).bipeds);
static_assert(sight_hit::Halo2ProjectileFlagsAgree(0x2490000Fu, 0x2480000Fu));
} // namespace

int main()
{
    for (const Shipped& s : kShipped)
    {
        const auto c = sight_hit::DecodeTraceFlags(s.engine, s.flags);
        char what[160];
        std::snprintf(what, sizeof(what), "%s stops on structure", s.what);
        Check(c.structure, what);
        std::snprintf(what, sizeof(what), "%s stops on bipeds", s.what);
        Check(c.bipeds, what);
        std::snprintf(what, sizeof(what), "%s stops on vehicles", s.what);
        Check(c.vehicles, what);
        std::snprintf(what, sizeof(what), "%s stops on other objects", s.what);
        Check(c.otherObjects, what);
        std::snprintf(what, sizeof(what), "%s names no type, so the engine tests every type", s.what);
        Check(c.objects && c.allTypes && c.typeMask == (1u << s.typeCount) - 1u, what);
        std::snprintf(what, sizeof(what), "%s meets the convergence requirement", s.what);
        Check(sight_hit::HitsUnitsAndVehicles(c), what);
        Check(sight_hit::Layout(s.engine).typeCount == s.typeCount, "the layout's type count is the engine's");
    }

    for (const Shipped& s : kLineOfSight)
    {
        const auto c = sight_hit::DecodeTraceFlags(s.engine, s.flags);
        char what[160];
        std::snprintf(what, sizeof(what), "%s tests objects by a type mask", s.what);
        Check(c.objects && !c.allTypes, what);
        std::snprintf(what, sizeof(what), "%s passes straight through bipeds", s.what);
        Check(!c.bipeds, what);
        std::snprintf(what, sizeof(what), "%s still stops on vehicles", s.what);
        Check(c.vehicles, what);
        std::snprintf(what, sizeof(what), "%s FAILS the convergence requirement", s.what);
        Check(!sight_hit::HitsUnitsAndVehicles(c), what);
    }

    // No objects bit: nothing but the level, whatever type bits are set.
    {
        const auto c = sight_hit::DecodeTraceFlags(Engine::Halo3, 0x24809ull);
        Check(c.structure && !c.objects && !c.bipeds && !c.vehicles && !c.otherObjects && !c.typeMask,
              "collision flags without the objects bit stop on structure only");
        const auto h2 = sight_hit::DecodeTraceFlags(Engine::Halo2, 0x24800007ull | (1ull << 4));
        Check(!h2.objects && !h2.bipeds, "a Halo 2 type bit without the objects bit tests no object");
        const auto ce = sight_hit::DecodeTraceFlags(Engine::HaloCE, 0x100069ull);
        Check(ce.structure && !ce.objects && !ce.vehicles, "CE's flags without bit 7 test no object");
    }

    // A named type mask is taken as it is: bit (type + first) per title.
    {
        const auto bipedsOnly = sight_hit::DecodeTraceFlags(Engine::ODST, (1ull << 32) | (1ull << 33) | 1ull);
        Check(bipedsOnly.bipeds && !bipedsOnly.vehicles && !bipedsOnly.otherObjects && bipedsOnly.typeMask == 1u,
              "ODST object flags 0x3 = objects, bipeds only");
        const auto vehiclesOnly = sight_hit::DecodeTraceFlags(Engine::Halo2, 0xFull | (1ull << 5));
        Check(!vehiclesOnly.bipeds && vehiclesOnly.vehicles && !vehiclesOnly.otherObjects,
              "Halo 2 bit 5 = vehicles only");
        const auto ceBipeds = sight_hit::DecodeTraceFlags(Engine::HaloCE, 0x20C3A0ull);
        Check(ceBipeds.bipeds && ceBipeds.vehicles && ceBipeds.otherObjects && !ceBipeds.allTypes,
              "CE's living-biped flags name bipeds, vehicles, scenery and machines");
    }

    // The culls in force are reported, not treated as a missing class.
    {
        const auto h2 = sight_hit::DecodeTraceFlags(Engine::Halo2, 0x2490000Full);
        Check(h2.cullsDecoded && h2.bipedCulls == (1ull << 20) && !h2.objectCulls && !h2.vehicleCulls,
              "Halo 2's projectile flags carry the dead-biped cull and no other");
        const auto h2world = sight_hit::DecodeTraceFlags(Engine::Halo2, 0x2480000Full);
        Check(!h2world.bipedCulls && !h2world.objectCulls, "Halo 2's world-collision flags carry no cull");
        const auto h3 = sight_hit::DecodeTraceFlags(Engine::Halo3, 0x100226809ull);
        Check(h3.objectCulls == (1ull << 21) && !h3.bipedCulls, "Halo 3's crosshair flags carry object cull bit 21 only");
        const auto h4 = sight_hit::DecodeTraceFlags(Engine::Halo4, 0x10044D409ull);
        Check(h4.objectCulls == (1ull << 22) && h4.bipedCulls == (1ull << 10) && !h4.vehicleCulls,
              "Halo 4's shot flags carry object cull bit 22 and biped cull bit 10");
        const auto ce = sight_hit::DecodeTraceFlags(Engine::HaloCE, 0x1000E9ull);
        Check(ce.cullsDecoded && !ce.objectCulls && !ce.bipedCulls, "CE's projectile flags cull nothing");
        Check(!sight_hit::DecodeTraceFlags(Engine::Reach, 0x100226809ull).cullsDecoded,
              "Reach's per-object culls are marked as not decoded");
    }

    // One trace result.
    Check(sight_hit::ClassifyHit(false, false, -1) == HitClass::Miss, "no hit is a miss");
    Check(sight_hit::ClassifyHit(false, true, 0) == HitClass::Miss, "a miss is a miss whatever the result names");
    Check(sight_hit::ClassifyHit(true, false, -1) == HitClass::Structure, "a hit that names no object is structure");
    Check(sight_hit::ClassifyHit(true, false, 0) == HitClass::Structure, "structure, even with a stale type");
    Check(sight_hit::ClassifyHit(true, true, 0) == HitClass::Biped, "object type 0 is a biped");
    Check(sight_hit::ClassifyHit(true, true, 1) == HitClass::Vehicle, "object type 1 is a vehicle");
    Check(sight_hit::ClassifyHit(true, true, 11) == HitClass::OtherObject, "any other type is another object");
    Check(sight_hit::ClassifyHit(true, true, -1) == HitClass::UnknownObject, "an unreadable type is counted apart");
    for (int c = 0; c < static_cast<int>(HitClass::Count); ++c)
        Check(sight_hit::HitClassName(static_cast<HitClass>(c))[0] != '?', "every class has a name");
    for (int e = 0; e < static_cast<int>(Engine::Count); ++e)
        Check(sight_hit::EngineName(static_cast<Engine>(e))[0] != '?' &&
              sight_hit::Layout(static_cast<Engine>(e)).typeCount != 0, "every engine has a name and a layout");

    // The seated look-past: our own vehicle's parts, never a boarder, never anything else.
    Check(!sight_hit::LookPastOwnHierarchy(false, false, 0), "an enemy on foot is a target");
    Check(!sight_hit::LookPastOwnHierarchy(false, false, 1), "another vehicle is a target");
    Check(!sight_hit::LookPastOwnHierarchy(false, false, -1), "an object of another hierarchy is a target");
    Check(sight_hit::LookPastOwnHierarchy(true, false, 1), "our own vehicle (or its turret) is looked past");
    Check(sight_hit::LookPastOwnHierarchy(true, false, 2), "a weapon on our vehicle is looked past");
    Check(sight_hit::LookPastOwnHierarchy(true, false, -1), "a part of ours with an unreadable type is looked past");
    Check(sight_hit::LookPastOwnHierarchy(true, true, 0), "our own unit is looked past");
    Check(!sight_hit::LookPastOwnHierarchy(true, false, 0),
          "a biped on our vehicle that is not us is a boarder: a target");

    // Halo 2: the projectile's flags are taken only as the lane's own flags plus the dead-biped bit.
    Check(sight_hit::Halo2ProjectileFlagsAgree(0x2490000Fu, 0x2480000Fu), "the measured pair agrees");
    Check(!sight_hit::Halo2ProjectileFlagsAgree(0x2480000Fu, 0x2480000Fu), "the same value twice is not the projectile's");
    Check(!sight_hit::Halo2ProjectileFlagsAgree(0x2490000Bu, 0x2480000Fu), "a value that lost a bit is refused");
    Check(!sight_hit::Halo2ProjectileFlagsAgree(0x24D0000Fu, 0x2480000Fu), "a value with a second new bit is refused");
    Check(!sight_hit::Halo2ProjectileFlagsAgree(0x24900007u, 0x24800007u),
          "a pair that agrees but tests no object is refused");
    Check(!sight_hit::Halo2ProjectileFlagsAgree(0x2490000Fu, 0x2490000Fu),
          "a lane value that already carries the bit is refused (nothing independent agrees)");
    Check(!sight_hit::Halo2ProjectileFlagsAgree(0u, 0x2480000Fu), "no immediate is refused");

    // The dev window: nearest, farthest, count, last type.
    {
        sight_hit::HitWindowClass w{};
        sight_hit::NoteHit(w, 5.0f, 0);
        sight_hit::NoteHit(w, 2.0f, 0);
        sight_hit::NoteHit(w, 9.0f, 3);
        Check(w.count == 3 && w.nearWu == 2.0f && w.farWu == 9.0f && w.lastType == 3,
              "the window keeps the count, the nearest, the farthest and the last type");
    }

    std::printf("sight_hit_class_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
