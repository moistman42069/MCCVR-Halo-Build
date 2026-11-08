// Halo 4 seated aim (src/common/halo4_seat_aim_logic.h): the seat class table, the eye agreement, the seated
// trace's ignores and look-past, the shot line, the barrel claim and the record-frame gate.
#include "halo4_seat_aim_logic.h"
#include <cmath>
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

using namespace halo4_seat_aim;
constexpr float kDeg = 0.017453293f;

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
bool NearV(Vec3 a, Vec3 b, float tol) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }

void TestClass()
{
    const int32_t unit = 0x10001, parent = 0x20002, turret = 0x30003;
    Check(ClassifyControlled(unit, unit, parent) == Controlled::Self, "0x5F9694 returning the occupant: self");
    Check(ClassifyControlled(parent, unit, parent) == Controlled::Parent, "returning the vehicle: parent");
    Check(ClassifyControlled(turret, unit, parent) == Controlled::Child, "returning another unit: a turret child");
    Check(ClassifyControlled(-1, unit, parent) == Controlled::Unknown, "no answer: unknown");
    Check(ClassifyControlled(parent, unit, -1) == Controlled::Child, "no parent named: never Parent");

    // Every unknown input is Unknown, whatever else holds.
    Check(ClassifySeat(false, kSeatFlagGunner, Controlled::Parent, true, true) == SeatClass::Unknown,
          "flags unknown: unknown");
    Check(ClassifySeat(true, kSeatFlagGunner, Controlled::Unknown, true, true) == SeatClass::Unknown,
          "controlled unknown: unknown");
    Check(ClassifySeat(true, kSeatFlagGunner, Controlled::Parent, false, true) == SeatClass::Unknown,
          "weapon unknown: unknown");
    // A seat that fires nothing.
    Check(ClassifySeat(true, kSeatFlagDriver, Controlled::Self, true, false) == SeatClass::NoWeapon,
          "a Warthog driver (no weapon): no weapon");
    Check(ClassifySeat(true, kSeatFlagSlavedTurret, Controlled::Child, true, false) == SeatClass::NoWeapon,
          "no weapon wins over the turret bit");
    // Bit 7 is a slaved turret whoever is controlled.
    Check(ClassifySeat(true, kSeatFlagSlavedTurret, Controlled::Child, true, true) == SeatClass::SlavedTurret,
          "bit 7: slaved turret");
    Check(ClassifySeat(true, kSeatFlagSlavedTurret | kSeatFlagGunner, Controlled::Parent, true, true) ==
              SeatClass::SlavedTurret, "bit 7 with bit 3: still a slaved turret");
    Check(ClassifySeat(true, kSeatFlagDriver | kSeatFlagWorldAngles, Controlled::Parent, true, true) ==
              SeatClass::VehicleWeapon, "a Ghost/Banshee driver controlling the vehicle: vehicle weapon");
    Check(ClassifySeat(true, kSeatFlagGunner, Controlled::Child, true, true) == SeatClass::VehicleWeapon,
          "a gunner controlling a turret child (no bit 7): vehicle weapon");
    Check(ClassifySeat(true, kSeatFlagAllowsWeapons, Controlled::Self, true, true) == SeatClass::PersonalWeapon,
          "a passenger with their own gun: personal weapon");
    Check(ClassifySeat(true, 0, Controlled::Self, true, true) == SeatClass::PersonalWeapon,
          "the class follows the controlled unit, not the inferred bit 5");
    Check(SeatClassName(SeatClass::SlavedTurret)[0] == 's' && SeatClassName(SeatClass::Unknown)[0] == 'u',
          "class names");
}

void TestEye()
{
    Check(Near(EyeToleranceM(0.0f), 0.30f, 1e-6f), "at rest: 30 cm");
    Check(Near(EyeToleranceM(20.0f), 1.30f, 1e-5f), "20 m/s: 30 cm + a tick and a half");
    Check(Near(EyeToleranceM(400.0f), 2.30f, 1e-5f), "speed clamped to 40 m/s");
    Check(Near(EyeToleranceM(-5.0f), 0.30f, 1e-6f), "negative speed is rest");
    Check(Near(EyeToleranceM(std::nanf("")), 2.30f, 1e-5f), "the rail alone: NaN is the widest, not a crash (JudgeEye never asks it with NaN)");
    Check(Near(EyeToleranceM(20.0f, 0.1f), 3.30f, 1e-5f), "a sample 100 ms old: its travel over that age too");
    Check(Near(EyeToleranceM(20.0f, 5.0f), 4.30f, 1e-5f), "the age is capped at the freshness window");
    Check(Near(EyeToleranceM(20.0f, -1.0f), 1.30f, 1e-5f), "a negative age is none");

    Check(JudgeEye(1, true, 0.1f, 0.0f) == EyeEvidence::Agree, "first person, close: agree");
    Check(JudgeEye(1, true, 0.5f, 0.0f) == EyeEvidence::Disagree, "first person, 50 cm at rest: disagree");
    Check(JudgeEye(1, true, 0.5f, 10.0f) == EyeEvidence::Agree, "50 cm at 10 m/s: within the speed rail");
    Check(JudgeEye(1, true, 1.5f, 20.0f) == EyeEvidence::Disagree, "1.5 m at 20 m/s, judged fresh: disagree");
    Check(JudgeEye(1, true, 1.5f, 20.0f, 0.033f) == EyeEvidence::Agree, "...but read a tick late it is that tick's travel");
    Check(JudgeEye(0, true, 0.0f, 0.0f) == EyeEvidence::Chase, "the selector proved the chase camera");
    Check(JudgeEye(-1, true, 0.0f, 0.0f) == EyeEvidence::Disagree, "no witness proves nothing");
    Check(JudgeEye(1, false, 0.0f, 0.0f) == EyeEvidence::Disagree, "another seat proves nothing");
    Check(JudgeEye(1, true, std::nanf(""), 0.0f) == EyeEvidence::Disagree, "a non-finite gap proves nothing");
    // An unmeasured speed: the gap is no evidence (not the widest 2.3 m rail), but the witness still is.
    Check(JudgeEye(1, true, 0.1f, std::nanf("")) == EyeEvidence::None, "unknown speed, close: no evidence");
    Check(JudgeEye(1, true, 2.0f, std::nanf("")) == EyeEvidence::None,
          "unknown speed, 2 m off: no evidence, never agreement on the widest rail");
    Check(JudgeEye(0, true, 0.0f, std::nanf("")) == EyeEvidence::Chase, "unknown speed: a chase sample still unproves");
    Check(JudgeEye(-1, true, 0.0f, std::nanf("")) == EyeEvidence::Disagree, "unknown speed: no witness still disagrees");
    Check(JudgeEye(1, false, 0.0f, std::nanf("")) == EyeEvidence::Disagree, "unknown speed: another seat disagrees");
    // A boost past the cap: no evidence either way (judged on the capped rail it would wrongly fail).
    Check(JudgeEye(1, true, 3.0f, 55.0f) == EyeEvidence::None, "55 m/s, 3 m apart: a boost is no evidence");
    Check(JudgeEye(1, true, 0.1f, 55.0f) == EyeEvidence::None, "55 m/s, close: no agreement on an unjudgeable rail");
    Check(JudgeEye(0, true, 0.0f, 55.0f) == EyeEvidence::Chase, "55 m/s: a chase sample still unproves");

    SeatEyeAgreement a;
    unsigned long long t = 1000;
    for (int i = 0; i < 9; ++i, t += 33)
        a.step(EyeEvidence::Agree, t);
    Check(!a.proven, "nine samples are not ten");
    a.step(EyeEvidence::Agree, t);
    t += 33;
    Check(a.proven, "proven on the tenth consecutive agreeing sample");
    a.step(EyeEvidence::None, t + 5000);
    Check(a.proven, "no new sample is not evidence");
    // A WRONG EYE still drops the proof: it disagrees on every judged sample, ~250 ms at 30 Hz.
    {
        SeatEyeAgreement w = a;
        unsigned long long u = t;
        int samples = 0;
        while (w.proven && samples < 100)
        {
            w.step(EyeEvidence::Disagree, u);
            u += 33;
            ++samples;
        }
        Check(!w.proven && samples >= 8 && samples <= 9, "a wrong eye at 30 Hz is unproven in ~250 ms");
    }
    a.step(EyeEvidence::Disagree, t);
    Check(a.proven, "one disagreement does not drop a proof");
    a.step(EyeEvidence::Disagree, t + 400);
    Check(a.proven, "a hitch: two disagreements 400 ms apart are two samples, not a wrong eye");
    for (int i = 0; i < 4; ++i)
        a.step(EyeEvidence::Disagree, t + 433 + 33 * i);
    Check(!a.proven, "six disagreeing samples over 250 ms unprove");
    t += 600;
    for (int i = 0; i < 10; ++i, t += 33)
        a.step(EyeEvidence::Agree, t);
    Check(a.proven, "a fresh run of ten proves again");
    // Two seconds of boost past the cap (every sample no evidence), then a few samples judged at their age.
    {
        SeatEyeAgreement b = a;
        unsigned long long u = t;
        for (int i = 0; i < 60; ++i, u += 33)
            b.step(JudgeEye(1, true, 2.5f, 48.0f), u);
        Check(b.proven, "a boost past 40 m/s keeps the proof");
        for (int i = 0; i < 6; ++i, u += 33)
            b.step(JudgeEye(1, true, 1.4f, 30.0f, 0.02f), u);
        Check(b.proven && b.bad_count == 0, "slowing through 30 m/s, judged at the samples' age: agreement");
    }
    a.step(EyeEvidence::Chase, t);
    Check(!a.proven && a.count == 0, "a chase sample unproves at once");
    for (int i = 0; i < 5; ++i, t += 33)
        a.step(EyeEvidence::Agree, t);
    a.step(EyeEvidence::Disagree, t);
    t += 33;
    for (int i = 0; i < 9; ++i, t += 33)
        a.step(EyeEvidence::Agree, t);
    Check(!a.proven, "agreement must be consecutive");
    a.step(EyeEvidence::Agree, t);
    Check(a.proven, "and then it is");
    a.reset();
    Check(!a.proven && !a.count && !a.bad_since_ms && !a.bad_count, "reset forgets the stint");
    // Agreement interrupted, then restored within 250 ms, keeps the proof and clears the timer.
    for (int i = 0; i < 10; ++i, t += 33)
        a.step(EyeEvidence::Agree, t);
    a.step(EyeEvidence::Disagree, t);
    a.step(EyeEvidence::Agree, t + 100);
    Check(a.bad_count == 0 && !a.bad_since_ms, "an agreement clears the disagreement run");
    for (int i = 0; i < 5; ++i)
        a.step(EyeEvidence::Disagree, t + 133 + 33 * i);
    Check(a.proven, "the disagreement timer restarts after an agreement");
}

void TestIgnores()
{
    int32_t out[4]{};
    int n = BuildSeatIgnores(0x10, 0x20, 0x10, 0x30, out);
    Check(n == 3 && out[0] == 0x10 && out[1] == 0x20 && out[2] == 0x30,
          "root first, the parent is the root, the turret last");
    n = BuildSeatIgnores(0x40, 0x20, 0x10, 0x10, out);
    Check(n == 3 && out[0] == 0x40 && out[1] == 0x20 && out[2] == 0x10, "a carried vehicle: root, unit, parent");
    n = BuildSeatIgnores(0x10, 0x20, 0x10, 0x20, out);
    Check(n == 2 && out[0] == 0x10 && out[1] == 0x20, "a passenger controlling itself: no duplicate");
    n = BuildSeatIgnores(-1, 0x20, -1, -1, out);
    Check(n == 1 && out[0] == 0x20, "unknown handles are skipped");
    n = BuildSeatIgnores(-1, -1, -1, -1, out);
    Check(n == 0, "nothing to ignore");
}

struct FakeWorld
{
    // One pass per entry: whether it hits, at which fraction, which object.
    TracePass passes[4]{};
    int calls = 0;
    float along[4]{}, remaining[4]{};
};

void TestLookPast()
{
    const int32_t root = 0x10, unit = 0x20, turret = 0x30, other = 0x99, wall = -1;
    const int32_t chain[]{root, unit, turret};
    auto rootOf = [&](int32_t object) -> int32_t {
        if (object == unit || object == turret || object == root) return root;
        if (object == other) return other;
        return -1;
    };
    const float length = 100.0f, skip = 0.1f, step = 0.2f;
    {   // the first pass hits a wall: done
        FakeWorld w;
        w.passes[0] = {true, true, 0.25f, wall};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float a, float rem) { w.along[w.calls] = a; w.remaining[w.calls] = rem; return w.passes[w.calls++]; },
            rootOf);
        Check(r.completed && r.hit && r.passes == 1 && !r.selfHits && Near(r.rangeWu, 0.1f + 0.25f * 100.0f, 1e-4f),
              "a wall: the range from the ray's origin");
    }
    {   // own turret, then an enemy: looked past once
        FakeWorld w;
        w.passes[0] = {true, true, 0.01f, turret};
        w.passes[1] = {true, true, 0.5f, other};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float a, float rem) { w.along[w.calls] = a; w.remaining[w.calls] = rem; return w.passes[w.calls++]; },
            rootOf);
        const float firstHit = 0.1f + 0.01f * 100.0f;
        const float along1 = firstHit + step;
        const float rem1 = length - (along1 - skip);
        Check(r.hit && r.passes == 2 && r.selfHits == 1 && Near(w.along[1], along1, 1e-4f) &&
              Near(w.remaining[1], rem1, 1e-4f) && Near(r.rangeWu, along1 + 0.5f * rem1, 1e-3f),
              "our own turret is looked past, 5 cm on, and the enemy's range counts from the ray's origin");
    }
    {   // three passes of our own vehicle: a MISS (the far end), counted as exhausted
        FakeWorld w;
        for (auto& p : w.passes) p = {true, true, 0.001f, unit};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float a, float rem) { w.along[w.calls] = a; w.remaining[w.calls] = rem; return w.passes[w.calls++]; },
            rootOf);
        Check(r.completed && !r.hit && r.exhausted && r.selfHits == 3 && r.passes == 3 && w.calls == 3,
              "three own-vehicle passes publish a miss");
    }
    {   // a miss after looking past: the far end, not exhausted
        FakeWorld w;
        w.passes[0] = {true, true, 0.01f, root};
        w.passes[1] = {true, false, 0.0f, -1};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float a, float rem) { w.along[w.calls] = a; w.remaining[w.calls] = rem; return w.passes[w.calls++]; },
            rootOf);
        Check(!r.hit && !r.exhausted && r.selfHits == 1 && r.passes == 2, "past our vehicle into the sky: a miss");
    }
    {   // a hit whose chain cannot be walked: judged by the chain's handles
        FakeWorld w;
        w.passes[0] = {true, true, 0.01f, 0x77};   // rootOf(0x77) = -1, not in the chain: a target
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float, float) { return w.passes[w.calls++]; }, rootOf);
        Check(r.hit && !r.selfHits, "an unwalkable object outside the chain is a target");
        Check(IsOwnVehicleHit(turret, -1, root, chain, 3), "an unwalkable chain handle is ours");
        Check(!IsOwnVehicleHit(-1, -1, root, chain, 3), "the structure is never ours");
        Check(IsOwnVehicleHit(0x55, root, root, chain, 3), "anything rooted at our root is ours");
        Check(!IsOwnVehicleHit(unit, other, root, chain, 3), "a walk that names another root wins over the chain");
    }
    {   // a faulting pass stops everything
        FakeWorld w;
        w.passes[0] = {false, true, 0.5f, other};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float, float) { return w.passes[w.calls++]; }, rootOf);
        Check(!r.completed && !r.hit && r.passes == 1, "a fault is reported, never a range");
    }
    {   // a non-finite fraction is a miss
        FakeWorld w;
        w.passes[0] = {true, true, std::nanf(""), other};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float, float) { return w.passes[w.calls++]; }, rootOf);
        Check(r.completed && !r.hit, "a non-finite fraction is a miss");
    }
    {   // a self hit at the very end leaves no ray: a miss, without a fourth pass
        FakeWorld w;
        w.passes[0] = {true, true, 1.0f, unit};
        auto r = TraceLookingPast(length, skip, step, 3, root, chain, 3,
            [&](float, float) { return w.passes[w.calls++]; }, rootOf);
        Check(!r.hit && w.calls == 1 && r.exhausted, "nothing left past our vehicle: a miss");
    }
}

void TestBarrel()
{
    const Vec3 origin{0, 0, 0};
    {   // straight ahead: claimed, the shot is the point's direction
        const auto v = JudgeBarrel({1, 0, 0}, origin, {10, 0, 0}, true);
        Check(v.valid && v.claim && Near(v.angleRad, 0.0f, 1e-4f) && NearV(v.shot, {1, 0, 0}, 1e-5f),
              "on the barrel: claimed");
    }
    {   // 18 deg off: inside 21 - 2
        const auto v = JudgeBarrel({1, 0, 0}, origin, {std::cos(18 * kDeg), std::sin(18 * kDeg), 0}, true);
        Check(v.claim && NearV(v.shot, {std::cos(18 * kDeg), std::sin(18 * kDeg), 0}, 1e-5f),
              "18 deg: claimed, the assist reaches it");
    }
    {   // 20 deg off: the assist still reaches it, but it is not claimed (inside the 2 deg margin)
        const auto v = JudgeBarrel({1, 0, 0}, origin, {std::cos(20 * kDeg), std::sin(20 * kDeg), 0}, true);
        Check(v.valid && !v.claim && NearV(v.shot, {std::cos(20 * kDeg), std::sin(20 * kDeg), 0}, 1e-5f),
              "20 deg: reached, not claimed");
    }
    {   // 40 deg off: the shot is capped at 21 deg toward the point
        const auto v = JudgeBarrel({1, 0, 0}, origin, {std::cos(40 * kDeg), 0, std::sin(40 * kDeg)}, true);
        Check(v.valid && !v.claim && Near(v.angleRad, 40 * kDeg, 1e-4f), "40 deg: not claimed");
        Check(NearV(v.shot, {std::cos(kSeatAssistCapRad), 0, std::sin(kSeatAssistCapRad)}, 1e-5f),
              "the capped shot turns 21 deg toward the point, in its plane");
    }
    {   // a weapon whose own angle widens the cap (seen turning 30 deg): judged against what was seen
        const float cap = SeatAssistCap(30 * kDeg);
        Check(Near(cap, 30 * kDeg, 1e-6f), "an observed 30 deg turn widens the cap to 30 deg");
        Check(Near(SeatAssistCap(10 * kDeg), kSeatAssistCapRad, 1e-7f), "a narrower turn never narrows it");
        Check(Near(SeatAssistCap(std::nanf("")), kSeatAssistCapRad, 1e-7f), "nothing seen: 21 deg");
        const auto v = JudgeBarrel({1, 0, 0}, origin, {std::cos(25 * kDeg), std::sin(25 * kDeg), 0}, true, cap);
        Check(v.valid && v.claim && NearV(v.shot, {std::cos(25 * kDeg), std::sin(25 * kDeg), 0}, 1e-5f),
              "25 deg with a 30 deg cap: reached and claimed");
        const auto w = JudgeBarrel({1, 0, 0}, origin, {std::cos(40 * kDeg), 0, std::sin(40 * kDeg)}, true, cap);
        Check(!w.claim && NearV(w.shot, {std::cos(30 * kDeg), 0, std::sin(30 * kDeg)}, 1e-5f),
              "40 deg with a 30 deg cap: the honest line turns the 30 deg seen, not 21");
    }
    {   // unassisted (the aim unit carries no player): along the barrel, never claimed
        const auto v = JudgeBarrel({0, 2, 0}, origin, {0, 10, 0}, false);
        Check(v.valid && !v.claim && NearV(v.shot, {0, 1, 0}, 1e-5f), "no assist: the barrel's own line");
    }
    {   // near: measured from the barrel origin, not the eye
        const auto v = JudgeBarrel({1, 0, 0}, {0, 1, 0}, {2, 0, 0}, true);   // 26.6 deg from the muzzle
        Check(v.valid && !v.claim && Near(v.angleRad, std::atan2(1.0f, 2.0f), 1e-4f),
              "a near point is judged from the barrel origin");
    }
    {   // degenerate inputs
        Check(!JudgeBarrel({0, 0, 0}, origin, {1, 0, 0}, true).valid, "no barrel forward");
        Check(!JudgeBarrel({1, 0, 0}, origin, origin, true).valid, "the point at the muzzle");
        Check(!JudgeBarrel({std::nanf(""), 0, 0}, origin, {1, 0, 0}, true).valid, "non-finite barrel");
        const auto back = JudgeBarrel({1, 0, 0}, origin, {-5, 0, 0}, true);
        Check(back.valid && !back.claim && NearV(back.shot, {1, 0, 0}, 1e-5f),
              "a point behind the barrel: no plane, the barrel's own line");
    }
    {   // the honest reticle: from the eye toward where the real shot is at the point's range
        Vec3 dir{};
        Check(ShotReticleDirection({0, 0, 0}, {0, 0, -1}, {1, 0, 0}, {10, 0, -1}, dir) &&
                  NearV(dir, Scale(Vec3{10, 0, -1}, 1.0f / std::sqrt(101.0f)), 1e-5f),
              "the reticle looks from the eye at the barrel's point at that range");
        Check(!ShotReticleDirection({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {10, 0, 0}, dir), "no shot direction");
    }
}

void TestShotLine()
{
    const uint32_t gen = 7, weapon = 0x40004, biped = 0x10001, vehicle = 0x20002;
    ShotForm fire{};
    Check(ResolveShotLine(fire, gen, weapon, biped, biped) == ShotLine::Unknown, "no fire yet: unknown");
    // A Warthog passenger's own gun: the biped owns and aims it, and the fire handed the unit-aim flag.
    fire = {true, gen, weapon, biped, biped, true, false};
    Check(ResolveShotLine(fire, gen, weapon, biped, biped) == ShotLine::UnitAim,
          "the flag the fire was handed: the aiming unit's aim");
    fire.unitAim = false;
    Check(ResolveShotLine(fire, gen, weapon, biped, biped) == ShotLine::Barrel, "no flag: the barrel's forward");
    fire.substituted = true;
    Check(ResolveShotLine(fire, gen, weapon, biped, biped) == ShotLine::Substituted,
          "the mod's own muzzle shot is its own line, never the barrel's");
    fire.substituted = false;
    // Any other fire says nothing about this sample.
    Check(ResolveShotLine(fire, gen + 1, weapon, biped, biped) == ShotLine::Unknown, "another level: unknown");
    Check(ResolveShotLine(fire, gen, weapon + 1, biped, biped) == ShotLine::Unknown, "another weapon: unknown");
    Check(ResolveShotLine(fire, gen, weapon, vehicle, biped) == ShotLine::Unknown,
          "another owner (the flag reads owner+0x694): unknown");
    Check(ResolveShotLine(fire, gen, weapon, biped, vehicle) == ShotLine::Unknown,
          "another aiming unit (whose aim vector it would be): unknown");
    Check(ResolveShotLine(fire, 0, weapon, biped, biped) == ShotLine::Unknown, "no generation: unknown");
    fire.weapon = UINT32_MAX;
    Check(ResolveShotLine(fire, gen, UINT32_MAX, biped, biped) == ShotLine::Unknown, "no weapon: unknown");
    fire.known = false;
    fire.weapon = weapon;
    Check(ResolveShotLine(fire, gen, weapon, biped, biped) == ShotLine::Unknown, "an unrecorded form: unknown");
    Check(ShotLineName(ShotLine::UnitAim)[4] == 'a' && ShotLineName(ShotLine::Unknown)[0] == 'u', "line names");
    // The claim judges the line the assist turns: a unit-aim line pointing at the point is claimed even with
    // the barrel marker 30 deg off (the marker never enters it).
    const auto v = JudgeBarrel({0, 1, 0}, {0, 0, 0}, {0, 50, 0}, true);
    Check(v.valid && v.claim && Near(v.angleRad, 0.0f, 1e-4f), "the shot line, not the marker, is judged");
}

void TestRecordFrame()
{
    Check(!SeatRecordIsWorld(false, kSeatFlagWorldAngles, true, {0, 0, 1}), "flags unknown: never");
    Check(SeatRecordIsWorld(true, kSeatFlagWorldAngles, false, {}), "bit 4: world angles at any tilt");
    Check(SeatRecordIsWorld(true, 0, true, {0, 0, 1}), "level: R is the identity");
    Check(SeatRecordIsWorld(true, 0, true, {std::sin(1.5f * kDeg), 0, std::cos(1.5f * kDeg)}), "1.5 deg: level");
    Check(!SeatRecordIsWorld(true, 0, true, {std::sin(3.0f * kDeg), 0, std::cos(3.0f * kDeg)}), "3 deg: tilted");
    Check(!SeatRecordIsWorld(true, 0, true, {0, 0, -1}), "upside down is a half turn, not the identity");
    Check(!SeatRecordIsWorld(true, 0, false, {0, 0, 1}), "no up: not proven level");
    Check(!SeatRecordIsWorld(true, 0, true, {0, 0, 0}), "a zero up proves nothing");
    Check(SeatRecordIsWorld(true, 0, true, {0, 0, 5}), "an unnormalised up is normalised");

    // THE GATE: a tilt is natural and does not refuse the solve; only our code's proofs do.
    Check(!SeatRecordSolvable(false, kSeatFlagWorldAngles, true, {0, 0, 1}), "solvable: flags unknown, never");
    Check(SeatRecordSolvable(true, kSeatFlagWorldAngles, false, {}), "solvable: bit 4 at any tilt");
    Check(SeatRecordSolvable(true, 0, true, {0, 0, 1}), "solvable: level");
    Check(SeatRecordSolvable(true, 0, true, {std::sin(3.0f * kDeg), 0, std::cos(3.0f * kDeg)}),
          "solvable: a 3 deg slope no longer switches the seat's convergence off");
    Check(SeatRecordSolvable(true, 0, true, {std::sin(30.0f * kDeg), 0, std::cos(30.0f * kDeg)}),
          "solvable: nor does a 30 deg one");
    Check(SeatRecordSolvable(true, 0, true, {0, 0, -1}), "solvable: even upturned (the steer has the same error)");
    Check(!SeatRecordSolvable(true, 0, false, {0, 0, 1}), "solvable: no up read, the anchor is unproven");
    Check(!SeatRecordSolvable(true, 0, true, {0, 0, 0}), "solvable: a zero up proves nothing");
    Check(!SeatRecordSolvable(true, 0, true, {std::nanf(""), 0, 1}), "solvable: a NaN up proves nothing");
}
} // namespace

int main()
{
    TestClass();
    TestEye();
    TestIgnores();
    TestLookPast();
    TestBarrel();
    TestShotLine();
    TestRecordFrame();
    std::printf("Halo 4 seated aim: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
