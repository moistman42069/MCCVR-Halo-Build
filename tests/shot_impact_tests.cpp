// Shot impact (src/common/shot_impact_logic.h): the dev witness of where a round ACTUALLY goes -- the offset split in
// the player's frame, the per-shot proof of the object fields, what the game made of our shot line (the native's turn,
// the inherited velocity, the round's own line against P), the projectile's own collision query, the tracker, the
// windows' lines, the trigger latch, and the reticle's tracking-to-world map.
#include "shot_impact_logic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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
bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
constexpr float kWuPerM = shot_lag::kWuPerMetre;
} // namespace

int main()
{
    using namespace shot_impact;

    // ---- Decompose ------------------------------------------------------------------------------------------------
    {
        const float view[3]{1, 0, 0};
        const float vel[3]{0, 5 * kWuPerM, 0};   // travelling +Y at 5 m/s
        const float ahead[3]{0, 0.1f * kWuPerM, 0};
        Axes a = Decompose(ahead, view, vel);
        Check(a.valid && a.travel, "decompose: valid with a travel axis");
        Check(Near(a.alongMm, 100.0f, 0.5f) && Near(a.acrossMm, 0.0f, 0.5f) && Near(a.depthMm, 0.0f, 0.5f),
              "decompose: an offset along the travel is all 'along', signed ahead");
        const float behind[3]{0.2f * kWuPerM, -0.05f * kWuPerM, 0.03f * kWuPerM};
        a = Decompose(behind, view, vel);
        Check(Near(a.depthMm, 200.0f, 0.5f) && Near(a.alongMm, -50.0f, 0.5f) && Near(a.acrossMm, 30.0f, 0.5f),
              "decompose: depth along the view, along travel signed behind, across the rest");
        a = Decompose(behind, view, nullptr);
        Check(a.valid && !a.travel && Near(a.alongMm, 0.0f, 1e-3f) &&
                  Near(a.acrossMm, std::sqrt(50.0f * 50.0f + 30.0f * 30.0f), 0.5f),
              "decompose: without a velocity the whole lateral offset is 'across'");
        const float forwardOnly[3]{5 * kWuPerM, 0, 0};   // moving along the view: no travel axis across it
        a = Decompose(behind, view, forwardOnly);
        Check(a.valid && !a.travel, "decompose: a velocity along the view gives no travel axis");
        const float zero[3]{0, 0, 0};
        Check(!Decompose(behind, zero, vel).valid, "decompose: a zero view is refused");
    }

    // ---- SpawnProven ----------------------------------------------------------------------------------------------
    {
        const float pos[3]{10.0f, -3.5f, 2.25f};
        const float fwd[3]{0.6f, 0.8f, 0.0f};
        const float placementVel[3]{0.6f * 2.0f, 0.8f * 2.0f, 0.0f};
        const float constructed[3]{placementVel[0] + 0.6f * 300.0f, placementVel[1] + 0.8f * 300.0f, 0.0f};
        Check(SpawnProven(pos, fwd, placementVel, pos, fwd, constructed),
              "spawn proof: same position and forward, velocity plus forward x speed");
        Check(SpawnProven(pos, fwd, placementVel, pos, fwd, placementVel),
              "spawn proof: a velocity the constructor has not touched yet");
        const float sideways[3]{constructed[0] + 0.5f, constructed[1] - 0.2f, constructed[2] + 3.0f};
        Check(!SpawnProven(pos, fwd, placementVel, pos, fwd, sideways),
              "spawn proof: a velocity change off the forward is not the constructor's");
        const float backwards[3]{placementVel[0] - 0.6f * 300.0f, placementVel[1] - 0.8f * 300.0f, 0.0f};
        Check(!SpawnProven(pos, fwd, placementVel, pos, fwd, backwards),
              "spawn proof: a velocity change against the forward is not the constructor's");
        float moved[3]{pos[0], pos[1], std::nextafter(pos[2], 10.0f)};
        Check(!SpawnProven(pos, fwd, placementVel, moved, fwd, constructed),
              "spawn proof: the position must match bitwise (a neighbouring field never does)");
        const float otherFwd[3]{0.8f, 0.6f, 0.0f};
        Check(!SpawnProven(pos, fwd, placementVel, pos, otherFwd, constructed), "spawn proof: the forward must match");
        const float tiny[3]{0.01f, 0.0f, 0.0f};
        Check(!SpawnProven(pos, tiny, placementVel, pos, tiny, placementVel),
              "spawn proof: a forward that is not a unit vector is no forward");
        const float nan3[3]{NAN, 0, 0};
        Check(!SpawnProven(nan3, fwd, placementVel, nan3, fwd, constructed), "spawn proof: non-finite refused");
    }

    // ---- MeasureSpawn ---------------------------------------------------------------------------------------------
    const float spawn[3]{0, 0, 0};
    const float point[3]{10 * kWuPerM, 0, 0};          // P 10 m ahead along +X
    const float eyeVel[3]{0, 5 * kWuPerM, 0};          // the player strafing +Y at 5 m/s
    const float speed = 300 * kWuPerM;                 // 300 m/s
    {
        const float d[3]{1, 0, 0};
        const float v[3]{speed, 0, 0};
        const Spawn s = MeasureSpawn(d, d, v, spawn, point, eyeVel);
        Check(s.valid, "spawn: valid");
        Check(Near(s.line.missMm, 0.0f, 0.5f) && Near(s.turnDeg, 0.0f, 1e-4f) && Near(s.lateralMps, 0.0f, 1e-3f),
              "spawn: a round along our line through P misses by nothing, turned by nothing, inherits nothing");
        Check(Near(s.rangeM, 10.0f, 1e-3f) && Near(s.speedMps, 300.0f, 0.01f) && Near(s.tofMs, 33.333f, 0.01f),
              "spawn: range, speed and flight time");
        Check(Near(s.requestedMissMm, 0.0f, 0.5f), "spawn: our line through P (the circular check)");
        Check(s.line.bucket == 2, "spawn: binned by the eye's speed (4-10 m/s)");
    }
    {
        // Full inheritance: the round carries the player's sideways 5 m/s. It drifts AHEAD of the travel by
        // range * 5 / 300 = 167 mm at P, and the witness must see that our line still passes through P.
        const float d[3]{1, 0, 0};
        const float v[3]{speed, 5 * kWuPerM, 0};
        const Spawn s = MeasureSpawn(d, d, v, spawn, point, eyeVel);
        Check(s.valid && Near(s.lateralMps, 5.0f, 1e-3f) && Near(s.lateralAlongMps, 5.0f, 1e-3f),
              "spawn: inherited velocity off the forward, along the travel");
        Check(s.inheritKnown && Near(s.inheritRatio, 1.0f, 1e-3f), "spawn: all of the eye's speed inherited");
        Check(s.line.travel && Near(s.line.alongMm, 166.7f, 1.0f) && Near(s.line.acrossMm, 0.0f, 1.0f),
              "spawn: the round's own line passes P 167 mm ahead of the travel");
        Check(Near(s.requestedMissMm, 0.0f, 0.5f), "spawn: while the line we handed the game passes through P");
        Check(s.deviationTravel && Near(s.deviationAlongMps, 5.0f, 0.01f),
              "spawn: velocity minus our direction x speed, along travel = the inherited 5 m/s");
        Check(Near(s.turnDeg, 0.0f, 1e-4f), "spawn: inheritance is not a turn");
    }
    {
        // The native turned the round 0.5 deg BEHIND the travel (toward -Y): no inheritance.
        const float d[3]{1, 0, 0};
        const float a = 0.5f / shot_lag::kRadToDeg;
        const float f[3]{std::cos(a), -std::sin(a), 0};
        const float v[3]{f[0] * speed, f[1] * speed, 0};
        const Spawn s = MeasureSpawn(d, f, v, spawn, point, eyeVel);
        Check(s.valid && Near(s.turnDeg, 0.5f, 1e-3f), "spawn: the native's turn after our line");
        Check(s.turn.travel && Near(s.turn.alongMm, -10000.0f * std::sin(a), 1.0f),
              "spawn: that turn, along travel at P's range (behind)");
        Check(Near(s.lateralMps, 0.0f, 1e-2f), "spawn: a turned forward with a velocity along it inherits nothing");
        Check(s.line.travel && s.line.alongMm < -80.0f, "spawn: the round's line passes P behind the travel");
    }
    {
        const float d[3]{1, 0, 0};
        const float zero[3]{0, 0, 0};
        Check(!MeasureSpawn(d, d, zero, spawn, point, eyeVel).valid, "spawn: a zero velocity is unmeasured");
        Check(!MeasureSpawn(d, d, d, spawn, spawn, eyeVel).valid, "spawn: P at the spawn is unmeasured");
        const float v[3]{speed, 0, 0};
        const Spawn s = MeasureSpawn(d, d, v, spawn, point, nullptr);
        Check(s.valid && !s.inheritKnown && !s.lateralTravel && !s.deviationTravel && s.line.bucket == -1,
              "spawn: without the eye's velocity there is no travel axis and no bucket");
        const float slow[3]{0, 0.5f * kWuPerM, 0};
        const Spawn t = MeasureSpawn(d, d, v, spawn, point, slow);
        Check(t.valid && t.lateralTravel && !t.inheritKnown, "spawn: no inheritance ratio under 1 m/s of travel");
    }

    // ---- OffLineMm, QueryIsProjectiles ----------------------------------------------------------------------------
    {
        const float o[3]{0, 0, 0}, d[3]{2, 0, 0}, p[3]{5, 0.01f, 0};
        Check(Near(OffLineMm(o, d, p), 0.01f * shot_lag::kMmPerWu, 0.01f), "off line: perpendicular distance");
        const float z[3]{0, 0, 0};
        Check(std::isnan(OffLineMm(o, z, p)), "off line: a zero direction is NaN");
        const float pos[3]{1, 2, 3}, vel[3]{100, 0, 0};
        const float startAt[3]{1.001f, 2, 3}, along[3]{3.3f, 0.05f, 0};
        Check(QueryIsProjectiles(pos, vel, startAt, along, 0.01f, 5.0f), "query: starts at the object, runs along it");
        const float far[3]{1.1f, 2, 3};
        Check(!QueryIsProjectiles(pos, vel, far, along, 0.01f, 5.0f), "query: a start elsewhere is not the round's");
        const float across[3]{0, 3.3f, 0};
        Check(!QueryIsProjectiles(pos, vel, startAt, across, 0.01f, 5.0f), "query: another direction is not the round's");
    }

    // ---- Tracker --------------------------------------------------------------------------------------------------
    {
        Tracker t;
        for (int i = 0; i < Tracker::kSlots; ++i)
            t.Arm(1000 + i).handle = static_cast<uint32_t>(i);
        Check(t.Armed() == Tracker::kSlots && t.evicted == 0, "tracker: fills every slot");
        Tracked& next = t.Arm(5000);
        Check(t.evicted == 1 && next.armed && next.armedUs == 5000 && next.handle == UINT32_MAX,
              "tracker: full -> the oldest is evicted and reset");
        bool oldestGone = true;
        for (const auto& s : t.slot)
            if (s.armed && s.armedUs == 1000)
                oldestGone = false;
        Check(oldestGone, "tracker: it was the oldest");
        t.slot[3].armed = false;
        Tracked& reuse = t.Arm(6000);
        Check(&reuse == &t.slot[3] && t.evicted == 1, "tracker: a free slot is used before evicting");
    }

    // ---- Window -------------------------------------------------------------------------------------------------
    {
        Window w;
        char line[1536]{};
        Check(w.Empty() && w.Format("Halo 3 [muzzle; predicted]", line, sizeof(line)) == 0,
              "window: nothing to report is no line");
        const float d[3]{1, 0, 0};
        const float v[3]{speed, 5 * kWuPerM, 0};
        w.AddFire(1);
        w.AddFire(0);
        w.AddSpawn(MeasureSpawn(d, d, v, spawn, point, eyeVel));
        w.AddSpawn(Spawn{});
        shot_lag::ShotLagSample tick = shot_lag::MeasureShotLag(spawn, v, point, nullptr, eyeVel);
        w.AddTick(tick, 3.0f);
        const float hitOffset[3]{0.0f, 0.17f * kWuPerM, 0.0f};
        w.AddImpact(Decompose(hitOffset, d, eyeVel), 0x38AAC7);
        ++w.goneWithoutImpact;
        Check(w.fires == 2 && w.projectiles == 1 && w.maxPerFire == 1 && w.firesWithout == 1, "window: fire counts");
        Check(w.spawnShots == 1 && w.unmeasured == 1 && w.n[2] == 1 && w.inheritShots == 1, "window: spawn counts");
        Check(w.impacts == 1 && w.impactTravel == 1 && Near(w.impactAlongSum, 170.0f, 0.5f), "window: impact sums");
        const size_t used = w.Format("Halo 3 [muzzle; predicted]", line, sizeof(line));
        Check(used > 0 && used < sizeof(line), "window: one line");
        Check(std::strstr(line, "SHOT IMPACT Halo 3 [muzzle; predicted] (dev): 2 fires, 1 projectiles") != nullptr,
              "window: the label and fire counts");
        Check(std::strstr(line, "ROUND LINE vs P") && std::strstr(line, "4-10: 1, miss 167 mm (along +167"),
              "window: the round line per speed bucket");
        Check(std::strstr(line, "1.00 of the eye's") && std::strstr(line, "along travel +5.00 m/s"),
              "window: the inheritance");
        Check(std::strstr(line, "IMPACT vs P 1") && std::strstr(line, "along travel +170 mm") &&
                  std::strstr(line, "(query +0x38AAC7)"),
              "window: the impact and the query that found it");
        Check(std::strstr(line, "later ticks 1") && std::strstr(line, "gone without a matched hit 1"),
              "window: ticks and the retired");
        char small[40]{};
        const size_t cut = w.Format("Halo 3 [muzzle; predicted]", small, sizeof(small));
        Check(cut < sizeof(small) && small[sizeof(small) - 1] == '\0', "window: a short buffer truncates safely");
    }

    // ---- vs P' (P moved with the eye; shot_lag 4a) -----------------------------------------------------------------
    {
        // A 4 m/s strafe along +Y, P 10 m down +X, P' 62 mm ahead of it along the travel. A round that flies through
        // P misses P' by 62 mm BEHIND (the key-off arm); one through P' misses P by 62 mm ahead (the key-on arm).
        const float view[3]{1, 0, 0};
        const float eyeVel[3]{0, 4 * kWuPerM, 0};
        const float p[3]{10 * kWuPerM, 0, 0};
        const float pT[3]{10 * kWuPerM, 0.062f * kWuPerM, 0};
        const float shift[3]{0, 0.062f * kWuPerM, 0};
        const float spawn[3]{0, 0, 0};
        const float velThroughP[3]{900 * kWuPerM, 0, 0};
        Window w;
        w.AddFire(1);
        w.AddTransported(shot_lag::Transport::Measured, shot_lag::MeasureShotLag(spawn, velThroughP, pT, nullptr, eyeVel),
                         Decompose(shift, view, eyeVel));
        w.AddTransported(shot_lag::Transport::Extrapolated,
                         shot_lag::MeasureShotLag(spawn, velThroughP, pT, nullptr, eyeVel),
                         Decompose(shift, view, eyeVel));
        w.AddTransported(shot_lag::Transport::NoPointEye, shot_lag::ShotLagSample{}, Axes{});
        const float hit[3]{p[0] - pT[0], p[1] - pT[1], p[2] - pT[2]};   // the round landed on P
        w.AddImpactTransported(Decompose(hit, view, eyeVel));
        Check(w.tMeasured == 1 && w.tExtrapolated == 1 && w.tNone == 1 && w.tShots == 2 && w.tTravel == 2,
              "vs P': counted by how P' was had; no P' is counted and never measured");
        Check(Near(w.tAlongSum / 2.0f, -62.0f, 0.5f) && Near(w.tShiftAlongSum / 2.0f, 62.0f, 0.5f),
              "vs P': a round through P sits 62 mm behind P' along travel, and P' is 62 mm ahead of P");
        Check(w.tImpacts == 1 && Near(w.tImpactAlongSum, -62.0f, 0.5f), "vs P': the impact against P'");
        char line[2048]{};
        Check(w.Format("Halo 3 [native; authoritative]", line, sizeof(line)) > 0 &&
                  std::strstr(line, "vs P' (P moved with the eye: measured 1, extrapolated 1, none 1): ROUND LINE miss "
                                    "62 mm, along travel -62 mm; P'-P along travel +62 mm; IMPACT vs P' 1: across 0 mm, "
                                    "along travel -62 mm") != nullptr,
              "vs P': the line's columns");
        Window none;
        none.AddFire(1);
        Check(none.Format("Halo 3", line, sizeof(line)) > 0 && !std::strstr(line, "vs P'"),
              "vs P': a window that never asked prints no columns");
        Tracked t{};
        Check(!t.pointTValid, "a tracked round has no P' until it is given one");
    }

    // ---- ReticleWindow, TriggerLatch -------------------------------------------------------------------------------
    {
        ReticleWindow r;
        char line[1024]{};
        Check(r.Format("Halo 3", line, sizeof(line)) == 0, "reticle: nothing to report is no line");
        r.fires = 3;
        r.noPoint = 1;
        const float view[3]{1, 0, 0};
        const float off[3]{0.02f * kWuPerM, -0.04f * kWuPerM, 0.0f};
        r.AddOffset(Decompose(off, view, eyeVel), 12.0f, 3.0f);
        r.AddPointAge(8.0f, true, 20.0f, 1);
        r.AddPointAge(4.0f, false, 0.0f, 0);
        r.AddTrigger(40.0f);
        r.AddTrigger(60.0f);
        Check(r.Format("Halo 3", line, sizeof(line)) > 0, "reticle: one line");
        Check(std::strstr(line, "SHOT RETICLE Halo 3 (dev): 3 local fires") && std::strstr(line, "along travel -40 mm") &&
                  std::strstr(line, "depth +20 mm"),
              "reticle: the reticle against P");
        Check(std::strstr(line, "hand 6.0 ms old (max 8.0)") && std::strstr(line, "camera 20.0 ms old") &&
                  std::strstr(line, "older than the reticle's 1 (by 1.0 frames)"),
              "reticle: P's ages and frame");
        Check(std::strstr(line, "-> fire 2: mean 50.0 max 60.0 ms") != nullptr, "reticle: the trigger latency");

        TriggerLatch t;
        Check(t.Fire(100) < 0.0f, "trigger: a fire with no press has no latency");
        t.Note(true, 1000);
        t.Note(true, 9000);   // held: not a new edge
        Check(Near(t.Fire(26000), 25.0f, 1e-3f), "trigger: edge to the first fire");
        Check(t.Fire(40000) < 0.0f, "trigger: an automatic weapon's later rounds are no latency");
        t.Note(false, 50000);
        t.Note(true, 60000);
        t.Note(false, 70000);   // released before any fire
        Check(t.Fire(80000) < 0.0f, "trigger: a press released before any fire opens nothing");
    }

    // ---- TrackingDirToWorld --------------------------------------------------------------------------------------
    {
        float out[3]{};
        const float forward[3]{0, 0, -1};
        Check(TrackingDirToWorld(forward, 0.0f, 0.0f, -1.0f, 1.0f, out) && Near(out[0], 1.0f, 1e-5f) &&
                  Near(out[1], 0.0f, 1e-5f) && Near(out[2], 0.0f, 1e-5f),
              "map: tracking forward is the game's yaw reference");
        const float right[3]{1, 0, 0};
        Check(TrackingDirToWorld(right, 0.0f, 0.0f, -1.0f, 1.0f, out) && Near(out[1], -1.0f, 1e-5f),
              "map: tracking right is Blam's -Y with the default yaw sign");
        // The aim path's own formula, with references and signs off their defaults.
        const float dir[3]{0.3f, 0.2f, -0.9f};
        const float gameYaw = 1.1f, headYaw = -0.4f, yawSign = 1.0f, pitchSign = -1.0f;
        const float l = shot_lag::Length(dir);
        const float yaw = gameYaw + yawSign * WrapPi(std::atan2(dir[0] / l, -dir[2] / l) - headYaw);
        const float pitch = pitchSign * std::asin(dir[1] / l);
        Check(TrackingDirToWorld(dir, gameYaw, headYaw, yawSign, pitchSign, out) &&
                  Near(out[0], std::cos(pitch) * std::cos(yaw), 1e-5f) &&
                  Near(out[1], std::cos(pitch) * std::sin(yaw), 1e-5f) && Near(out[2], std::sin(pitch), 1e-5f),
              "map: the same as the aim path's yaw/pitch map");
        const float up[3]{0, 1, 0};
        Check(TrackingDirToWorld(up, 0.0f, 0.0f, -1.0f, 1.0f, out) && Near(out[2], std::sin(1.45f), 1e-5f),
              "map: pitch clamped to the aim path's 1.45 rad");
        const float zero[3]{0, 0, 0};
        Check(!TrackingDirToWorld(zero, 0, 0, -1, 1, out), "map: a zero direction is refused");
        Check(Near(WrapPi(3.5f), 3.5f - 6.2831853f, 1e-5f) && Near(WrapPi(-3.5f), -3.5f + 6.2831853f, 1e-5f),
              "map: wrap");
    }

    // ---- the inherited velocity and the aim that allows for it ------------------------------------------------------
    {
        const float speed2 = 100.0f;   // object units: only the ratios matter
        // A Warthog-like 15 units/s sideways (and some forward) under a 100 units/s round.
        const float vb[3]{4.0f, 15.0f, 0.5f};
        const float vo[3]{4.0f, 15.0f, 0.5f};
        const float e[3]{1.0f, 0.0f, 0.0f};
        float d[3]{}, residual = NAN;
        int iterations = 0;
        Check(SolveInheritedAim(e, vb, vo, speed2, d, &residual, &iterations), "solve: a solution");
        float u[3]{};
        RoundVelocity(d, vb, vo, speed2, u);
        Check(residual <= 1.0e-3f && AngleDeg(u, e) <= 1.0e-3f, "solve: the round's velocity points at P");
        Check(iterations > 0 && iterations <= 16, "solve: converges in a few steps");
        Check(d[1] < 0.0f && Near(Length(d), 1.0f, 1e-5f), "solve: aims against the inherited sideways velocity");
        // The sideways part across e is 15 (and 0.5 up); the round's sideways velocity d_perp * s must cancel it.
        float drifted[3]{};
        RoundVelocity(e, vb, vo, speed2, drifted);
        Check(AngleDeg(drifted, e) > 8.0f, "solve: without it the round flies off P");
        // No inheritance: the direction is e itself.
        const float zero[3]{0, 0, 0};
        Check(SolveInheritedAim(e, zero, vo, speed2, d) && AngleDeg(d, e) < 1.0e-4f, "solve: nothing to allow for");
        // Arbitrary geometry, a body velocity with a large part along the shot (the branch removes it).
        const float e2[3]{0.3f, -0.8f, 0.2f};
        const float vb2[3]{-20.0f, 30.0f, 5.0f};
        Check(SolveInheritedAim(e2, vb2, nullptr, 400.0f, d, &residual), "solve: general geometry");
        RoundVelocity(d, vb2, nullptr, 400.0f, u);
        Check(AngleDeg(u, e2) <= 1.0e-3f, "solve: general geometry reaches P");
        // Refusals.
        const float huge[3]{0, 200.0f, 0};
        Check(!SolveInheritedAim(e, huge, nullptr, speed2, d), "solve: a sideways velocity over the round's speed");
        Check(!SolveInheritedAim(e, vb, vo, 0.0f, d), "solve: no speed");
        const float nan3[3]{NAN, 0, 0};
        Check(!SolveInheritedAim(nan3, vb, vo, speed2, d) && !SolveInheritedAim(e, nan3, vo, speed2, d),
              "solve: non-finite");
        Check(!SolveInheritedAim(zero, vb, vo, speed2, d), "solve: no direction");
        // The compensated point: same range from the origin, along the solved direction.
        const float origin[3]{1.0f, 2.0f, 3.0f};
        const float p[3]{11.0f, 2.0f, 3.0f};
        float shifted[3]{};
        Check(CompensatedPoint(origin, p, vb, vo, speed2, shifted), "point: compensated");
        const float to[3]{shifted[0] - origin[0], shifted[1] - origin[1], shifted[2] - origin[2]};
        const float tl = Length(to);
        Check(Near(tl, 10.0f, 1e-4f), "point: at P's range");
        const float dn[3]{to[0] / tl, to[1] / tl, to[2] / tl};
        RoundVelocity(dn, vb, vo, speed2, u);
        const float toP[3]{p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
        Check(AngleDeg(u, toP) <= 1.0e-3f, "point: a round aimed through it flies at P");
        Check(!CompensatedPoint(origin, origin, vb, vo, speed2, shifted), "point: P at the origin");
    }

    // ---- LateralAlongTravel -------------------------------------------------------------------------------------------
    {
        const float fwd[3]{1, 0, 0};
        const float vel[3]{5.0f, 2.0f * kWuPerM, 0.0f};
        float mps = 0.0f;
        Check(LateralAlongTravel(vel, fwd, eyeVel, 1.0f, mps) && Near(mps, 2.0f, 1e-3f),
              "lateral: the sideways part along travel");
        Check(LateralAlongTravel(vel, fwd, eyeVel, 30.0f, mps) && Near(mps, 60.0f, 1e-2f), "lateral: scaled by unit");
        const float along[3]{3.0f, 0, 0};
        Check(!LateralAlongTravel(vel, fwd, along, 1.0f, mps), "lateral: no travel across the forward");
        Check(!LateralAlongTravel(vel, fwd, nullptr, 1.0f, mps), "lateral: no eye velocity");
    }

    // ---- UnitsTrack, SpeedAgrees, SpeedTable, SeatWindows ------------------------------------------------------------
    {
        UnitsTrack units;
        Check(!units.Known() && units.Scale() == 1.0f, "units: unknown reads as 1");
        // Stored per tick: a round of 0.5 stored units moves 0.5 wu in each 1/60 s tick.
        for (int i = 0; i < 8; ++i)
            units.Add(0.5f, 1.0f / 60.0f, 0.5f);
        Check(units.Known() && Near(units.Scale(), 60.0f, 1e-2f), "units: a per-tick velocity reads 60 wu/s per unit");
        UnitsTrack perSecond;
        for (int i = 0; i < 8; ++i)
            perSecond.Add(5.0f, 0.016f, 5.0f / 0.016f);
        Check(Near(perSecond.Scale(), 1.0f, 1e-4f), "units: a per-second velocity reads 1");
        perSecond.Add(1.0f, 0.001f, 1.0f);   // under 5 ms: noise
        perSecond.Add(1.0f, 0.5f, 1.0f);     // over 100 ms: noise
        Check(perSecond.samples == 8, "units: noisy steps ignored");

        Check(SpeedAgrees(300.0f, 1.0f, 300.0f) && SpeedAgrees(150.0f, 0.5f, 300.0f), "speed: scale x tag");
        Check(!SpeedAgrees(300.0f, 1.0f, 290.0f) && !SpeedAgrees(0.0f, 1.0f, 0.0f), "speed: disagreement refused");

        SpeedTable table;
        float v = 0.0f;
        table.Note(7, true, 300.0f, 1);
        Check(!table.Lookup(7, v), "speed table: one round is not proof");
        table.Note(7, true, 300.0f, 2);
        Check(table.Lookup(7, v) && v == 300.0f, "speed table: two agreeing rounds are");
        table.Note(7, true, 250.0f, 3);
        Check(!table.Lookup(7, v), "speed table: a new speed starts the proof over");
        table.Note(7, true, 250.0f, 4);
        Check(table.Lookup(7, v) && v == 250.0f, "speed table: and proves again");
        table.Note(7, false, 0.0f, 5);
        Check(!table.Lookup(7, v), "speed table: a disagreeing round forgets the weapon");
        table.Note(9, false, 100.0f, 6);
        Check(!table.Lookup(9, v), "speed table: never learned from a disagreeing round");
        Check(!table.Lookup(UINT32_MAX, v), "speed table: no weapon");

        SeatWindows seats;
        const SeatKey a{1, 2, false}, b{3, 4, true};
        const int ia = seats.Index(a, 10);
        const int ib = seats.Index(b, 11);
        Check(ia != ib && seats.Index(a, 12) == ia && seats.dropped == 0, "seats: one window per seat");
        seats.Index(SeatKey{5, 6, false}, 13);
        seats.Index(SeatKey{7, 8, false}, 14);
        const int ie = seats.Index(SeatKey{9, 9, false}, 15);
        Check(seats.dropped == 1 && ie == ib, "seats: full -> the least recently used seat is dropped");
    }

    // ---- the seated window's line ------------------------------------------------------------------------------------
    {
        Window w;
        w.AddFire(1);
        w.AddInheritance(true, true, true, 4.5f);
        w.AddInheritance(false, true, false, 0.0f);
        w.speedProven = 2;
        w.fromAssist = 1;
        w.compensated = 1;
        char line[1536]{};
        Check(w.Format("Halo 3 SEATED [x]", line, sizeof(line)) > 0 &&
                  std::strstr(line, "INHERITANCE branch taken 1/2 (owner flag 2), placed sideways along travel +4.50") &&
                  std::strstr(line, "judged against the assist's output 1") &&
                  std::strstr(line, "round speed = scale x tag initial speed 2, off 0") &&
                  std::strstr(line, "compensated 1"),
              "seated window: the inheritance, speed and compensation fields");
    }

    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
