// Seat drive policy (src/common/seat_drive_policy.h): which axes of a vehicle seat the direct-drive hook may
// write, each seat stint's per-axis check, and the honest-reticle gate by outcome.
#include "seat_drive_policy.h"
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

using seat_drive::Axes;
using seat_drive::Decide;
using seat_drive::Seat;

Seat Passenger() { Seat s; s.identified = s.view_owned = true; return s; }
Seat LookDriver() { Seat s = Passenger(); s.driver = s.look_steered = true; return s; }
Seat TankDriver() { Seat s = Passenger(); s.driver = true; return s; }

void TestPolicy()
{
    // Off switches and unknowns: nothing written.
    Check(!Decide(Passenger(), 2, false).any(), "seat_direct_drive=0 writes no seat");
    Check(!Decide(Passenger(), 0, true).any(), "reach_vehicle_aim=0 writes no seat");
    Seat s = Passenger(); s.identified = false;
    Check(!Decide(s, 2, true).any(), "an unidentified seat is never written");
    s = Passenger(); s.view_owned = false;
    Check(!Decide(s, 2, true).any(), "a seat whose view is not ours is never written");

    // Passengers, gunners, turrets and tank drivers: both axes, at levels 1 and 2.
    for (int level = 1; level <= 2; ++level)
    {
        Axes a = Decide(Passenger(), level, true);
        Check(a.yaw && a.pitch, "a passenger is written on both axes");
        a = Decide(TankDriver(), level, true);
        Check(a.yaw && a.pitch, "a driver whose look aims a turret is written on both axes");
    }

    // A passenger or gunner of a look-steered vehicle (a Warthog's): both axes at levels 1 and 2, latched.
    {
        Seat gunner = Passenger(); gunner.look_steered = true;
        Check(Decide(gunner, 1, true).yaw && Decide(gunner, 1, true).pitch && Decide(gunner, 2, true).pitch,
              "a look-steered vehicle's gunner is written on both axes at levels 1 and 2");
        Check(seat_drive::PitchLatches(gunner), "and its pitch latches");
    }

    // Look-steered drivers.
    Check(!Decide(LookDriver(), 1, true).any(), "level 1 keeps a look-steered driver on the servo");
    Axes a = Decide(LookDriver(), 2, true);
    Check(a.yaw && a.pitch, "level 2: a look-steered driver (follow off) is written on both axes");
    s = LookDriver(); s.authors_steering = true;
    a = Decide(s, 2, true);
    Check(!a.yaw && a.pitch, "the stick steers: the heading is the author's, the climb follows the hand");
    s.wheel = true;
    Check(!Decide(s, 2, true).any(), "both hands on the wheel: nothing is written");
    s.wheel = false;
    Check(!Decide(s, 1, true).any(), "level 1, stick steering: still the servo");

    // The latch reads no driver pitch (autolevel); every other seat latches.
    Check(!seat_drive::PitchLatches(LookDriver()), "a look-steered driver's pitch never latches (autolevel)");
    Check(seat_drive::PitchLatches(TankDriver()), "a tank driver's pitch latches at the turret's stop");
    Check(seat_drive::PitchLatches(Passenger()), "a passenger's pitch latches");

    // Seat keys change with the seat, the vehicle and the mount.
    Check(seat_drive::SeatKey(0x10002, 0, false) != seat_drive::SeatKey(0x10002, 1, false), "seat index keys");
    Check(seat_drive::SeatKey(0x10002, 0, false) != seat_drive::SeatKey(0x20003, 0, false), "vehicle keys");
    Check(seat_drive::SeatKey(0x10002, 0, false) != seat_drive::SeatKey(0x10002, 0, true), "mount keys");
}

void TestStintCheck()
{
    using seat_drive::StintCheck;
    {   // agreeing record at rest: yaw then pitch pass on their 30th SETTLED sample (the first has no previous)
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 30; ++i, t += 16)
            c.step(true, 0.5f, 0.1f, 0.51f, 0.11f, t);
        Check(!c.yaw_ok && !c.pitch_ok && c.yaw_count == 29, "30 updates are 29 settled samples: not enough");
        c.step(true, 0.5f, 0.1f, 0.51f, 0.11f, t);
        Check(c.yaw_ok && c.pitch_ok && !c.pitch_failed, "yaw and pitch pass on 30 settled agreeing samples");
    }
    {   // yaw wraps across +-pi
        StintCheck c;
        for (int i = 0; i < 31; ++i)
            c.step(true, 3.13f, 0.0f, -3.13f, 0.0f, 1000 + 16 * i);
        Check(c.yaw_ok, "a yaw agreement across the wrap counts");
    }
    {   // THE REAL OFFSET STILL FAILS: a Halo 3 driver+gunner's record sits 13 deg off the gun -- settled, and
        // disagreeing on every sample. Yaw passes, pitch fails at the budget and stays on the servo.
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 400 && !c.pitch_failed; ++i, t += 16)
            c.step(true, 0.2f, 0.0f, 0.2f, 0.23f, t);
        Check(c.yaw_ok && !c.pitch_ok && c.pitch_failed && c.pitch_fails == 1, "a pitch that disagrees stays on the servo");
        // yaw passed on its 31st update (480 ms in); pitch then had three seconds of settled samples
        Check(t > 1000 + 480 + 3000 && t < 1000 + 480 + 3200, "the pitch budget is three settled seconds after yaw");
        // ...and every retry five seconds later fails again: the offset is never written
        for (int i = 0; i < 1000; ++i, t += 16)
            c.step(true, 0.2f, 0.0f, 0.2f, 0.23f, t);
        Check(!c.pitch_ok && c.pitch_fails >= 2, "a retry of a real offset fails again");
    }
    {   // a failed pitch RETRIES: the record comes to hold the game's pitch after the budget ran out
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 400 && !c.pitch_failed; ++i, t += 16)
            c.step(true, 0.0f, 0.0f, 0.0f, 0.3f, t);
        Check(c.pitch_failed, "(setup) the pitch failed its first attempt");
        const unsigned long long failedAt = c.elapsed_ms;
        for (int i = 0; i < 400 && c.pitch_failed; ++i, t += 16)
            c.step(true, 0.0f, 0.3f, 0.0f, 0.3f, t);
        Check(!c.pitch_failed && c.elapsed_ms - failedAt >= 5000 && c.elapsed_ms - failedAt < 5100,
              "the check retries five seconds of updates after it failed");
        for (int i = 0; i < 31; ++i, t += 16)
            c.step(true, 0.0f, 0.3f, 0.0f, 0.3f, t);
        Check(c.pitch_ok, "and the retry passes on 30 settled agreeing samples");
    }
    {   // a hand that keeps aiming: the record a sample behind the moving aim AGREES (the gap is the motion), so
        // pitch passes while the hand is still moving -- it no longer has to wait for the hand to settle
        StintCheck d;
        unsigned long long t = 1000;
        for (int i = 0; i < 31; ++i, t += 16)
            d.step(true, 0.0f, 0.4f, 0.0f, 0.0f, t);   // yaw proven; the record's pitch is somewhere else
        Check(d.yaw_ok && !d.pitch_ok, "(setup) yaw proven, pitch still checking");
        float p = 0.0f;
        int passedAt = -1;
        for (int i = 0; i < 250 && passedAt < 0; ++i, t += 16)   // the servo chasing a moving hand
        {
            p += 0.004f * ((i / 50) % 2 ? -1.0f : 1.0f);
            d.step(true, 0.0f, p - 0.004f, 0.0f, p, t);         // the record one sample behind
            if (d.pitch_ok) passedAt = i;
        }
        Check(passedAt >= 0 && passedAt <= 35 && !d.pitch_failed, "a hand that keeps aiming proves pitch in motion");
    }
    {   // a yaw dithering a sample behind (a gun slewing onto the hand) agrees: yaw and pitch pass together
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 40; ++i, t += 16)
            c.step(true, (i % 2) ? 0.05f : 0.0f, 0.1f, 0.0f, 0.1f, t);
        Check(c.yaw_ok && c.pitch_ok && !c.pitch_failed, "a record moving with the gun is no disagreement");
    }
    {   // THE BANSHEE: a flying vehicle is never still. Aim and record turn and climb continuously, the record a
        // sample behind. The check must pass in about half a second, as at rest (the settled-only rule left this
        // seat on the stick servo for 14 s to forever).
        StintCheck c;
        unsigned long long t = 1000;
        float y = 6.0f, p = 0.0f;
        int passedAt = -1;
        for (int i = 0; i < 300 && passedAt < 0; ++i, t += 16)
        {
            const float dy = 0.03f * std::sin(i * 0.05f), dp = 0.02f * std::cos(i * 0.07f);
            const float ry = y, rp = p;            // the record holds last sample's aim
            y += dy; p += dp;
            c.step(true, ry, rp, std::remainder(y, 6.28318531f), p, t);   // the aim reported wrapped
            if (c.yaw_ok && c.pitch_ok) passedAt = i;
        }
        Check(passedAt >= 0 && passedAt <= 32, "a Banshee in constant flight proves yaw and pitch in ~0.5 s");
    }
    {   // ...but a REAL offset still fails in flight: the record 13 deg off in pitch while flying never passes
        StintCheck c;
        unsigned long long t = 1000;
        float y = 1.0f, p = 0.0f;
        for (int i = 0; i < 2000; ++i, t += 16)
        {
            const float ry = y, rp = p;
            y += 0.03f * std::sin(i * 0.05f); p += 0.02f * std::cos(i * 0.07f);
            c.step(true, ry, rp + 0.23f, y, p, t);
        }
        Check(c.yaw_ok && !c.pitch_ok && c.pitch_fails >= 2, "a 13 deg pitch offset never passes, in flight either");
    }
    {   // a SETTLED disagreement restarts the count; a one-sample jump of the record is motion, not evidence
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 25; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        Check(c.yaw_count == 24, "(setup) 24 settled agreements");
        c.step(true, 0.2f, 0.0f, 0.0f, 0.0f, t); t += 16;   // the record jumps: unsettled
        Check(c.yaw_count == 24, "a record that just moved is no disagreement");
        c.step(true, 0.2f, 0.0f, 0.0f, 0.0f, t); t += 16;   // it stays there: settled, and it disagrees
        Check(c.yaw_count == 0 && !c.yaw_ok, "a settled disagreement restarts the yaw count");
    }
    {   // ...and the same for PITCH: an impulse that moves only the RECORD (fire kicks the gun before the stick call
        // reads it) is motion, not a settled disagreement. Pitch settling on the game's aim alone would reset the
        // count here and keep a gunner seat on the servo under fire.
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 25; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        Check(c.pitch_count == 24, "(setup) 24 settled pitch agreements");
        c.step(true, 0.0f, 0.2f, 0.0f, 0.0f, t); t += 16;   // only the record's pitch jumps: unsettled
        Check(c.pitch_count == 24, "a record whose pitch just moved is no pitch disagreement");
        c.step(true, 0.0f, 0.2f, 0.0f, 0.0f, t); t += 16;   // it stays there: settled, and it disagrees
        Check(c.pitch_count == 0 && !c.pitch_ok, "a settled pitch disagreement restarts the pitch count");
    }
    {   // under fire: the record's pitch kicks on every other sample and recovers, the aim holds still. No kick is
        // ever settled, so none resets the count, and the settled samples between them still prove the pitch.
        StintCheck d;
        unsigned long long t = 1000;
        unsigned resets = 0, before = 0;
        for (int i = 0; i < 400 && !d.pitch_ok; ++i, t += 16) {
            const float kick = (i % 3 == 1) ? 0.08f : 0.0f;   // a 4.6 deg kick, recovered the next sample
            before = d.pitch_count;
            d.step(true, 0.0f, 0.1f + kick, 0.0f, 0.1f, t);
            if (before > 0 && d.pitch_count == 0) ++resets;
        }
        Check(resets == 0, "a recoil kick of the record never resets the pitch count");
        Check(d.yaw_ok && d.pitch_ok && !d.pitch_failed, "pitch proves through recoil on its settled samples");
    }
    {   // a turning vehicle: the aim and the record move together, a sample apart -- agreement, so it passes mid-turn
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 20; ++i, t += 16) c.step(true, 1.0f, 0.0f, 1.0f, 0.0f, t);
        float yaw = 1.0f;
        for (int i = 0; i < 12; ++i, t += 16)
        {
            yaw += 0.04f;                                     // 2.3 deg per sample; the record leads by one
            c.step(true, yaw + 0.04f, 0.0f, yaw, 0.0f, t);
        }
        Check(c.yaw_ok, "a turn keeps counting: the check passes mid-turn");
    }
    {   // a jump bigger than max_motion in one sample (a seat snap, a big hit) is no evidence either way
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 21; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        c.step(true, 0.5f, 0.0f, 0.0f, 0.0f, t); t += 16;
        Check(c.yaw_count == 20, "a 29 deg jump of the record neither counts nor resets");
    }
    {   // a repeated aim sample neither counts nor resets
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 21; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        for (int i = 0; i < 20; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t, false);
        Check(c.yaw_count == 20, "a repeated agreeing sample does not count");
        c.step(true, 0.2f, 0.2f, 0.0f, 0.0f, t, false); t += 16;
        Check(c.yaw_count == 20 && c.pitch_count == 20, "a repeated disagreeing sample does not reset");
        for (int i = 0; i < 10; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        Check(c.yaw_ok, "new samples finish the count");
    }
    {   // a slewing barrel whose record trails by several samples (rate-capped): never reset by the lag
        StintCheck d;
        unsigned long long t = 1000;
        float p = 0.0f;
        for (int i = 0; i < 25; ++i, t += 16)
            d.step(true, 0.0f, p, 0.0f, p, t);
        Check(!d.pitch_ok && d.pitch_count == 24, "(setup) pitch is still counting");
        unsigned resets = 0;
        for (int i = 0; i < 20; ++i, t += 16)
        {
            p += 0.01f;                       // the barrel slews 0.6 deg per update; record lags 2.9 deg
            const unsigned before = d.pitch_count;
            d.step(true, 0.0f, p - 0.05f, 0.0f, p, t);
            if (before && !d.pitch_count && !d.pitch_ok) ++resets;
        }
        Check(resets == 0, "a slewing barrel's lag is not a disagreement");
        for (int i = 0; i < 8; ++i, t += 16) d.step(true, 0.0f, p, 0.0f, p, t);
        Check(d.pitch_ok, "pitch passes once the record holds the aim");
    }
    {   // a pause after yaw passed: the gap adds at most 100 ms to pitch's budget
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 31; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.3f, t);
        Check(c.yaw_ok && !c.pitch_failed, "(setup) yaw passed, pitch disagreeing");
        const unsigned long long spent = c.pitch_settled_ms;
        t += 60000;
        c.step(true, 0.0f, 0.0f, 0.0f, 0.3f, t);
        Check(!c.pitch_failed && c.pitch_settled_ms - spent <= 100, "a minute paused is not pitch's budget");
    }
    {   // a stale or non-finite sample neither counts nor resets
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 21; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        c.step(false, 0.5f, 0.5f, 0.0f, 0.0f, t); t += 16;
        c.step(true, 0.0f, std::nanf(""), 0.0f, 0.0f, t); t += 16;
        Check(c.yaw_count == 20 && c.pitch_count == 20, "a stale or non-finite sample leaves both counts");
    }
    {   // a pause: the gap adds at most 100 ms of updating time
        StintCheck c;
        c.step(true, 0.0f, 0.0f, 0.0f, 0.3f, 1000);
        c.step(true, 0.0f, 0.0f, 0.0f, 0.3f, 61000);
        Check(!c.pitch_failed && c.elapsed_ms == 100, "a minute in a menu is not three seconds of seat");
    }
    {   // stale aim: nothing counts
        StintCheck c;
        for (int i = 0; i < 40; ++i)
            c.step(false, 0.0f, 0.0f, 0.0f, 0.0f, 1000 + 16 * i);
        Check(!c.yaw_ok && !c.pitch_ok, "a stale aim proves nothing");
        c.reset();
        Check(!c.yaw_ok && !c.have_last && !c.elapsed_ms && !c.pitch_fails, "reset forgets the stint");
    }
}

// Gun-slaved seats (seat flag bit 7): the camera check runs against the GUN, which trails the record; the occupant
// check runs against the occupant's own aim, which the record drives. Either proves an axis.
void TestCombineStints()
{
    using seat_drive::CombineStints;
    using seat_drive::StintCheck;
    {   // THE WARTHOG TURRET: the hand sweeps the pitch up and down, the record follows within a sample, and the gun
        // trails it by a steady 13-17 deg (its slew). The camera check never passes pitch; the occupant's does.
        StintCheck camera, occupant;
        unsigned long long t = 1000;
        float record = 0.0f;
        int pitchAt = -1, yawAt = -1;
        for (int i = 0; i < 400; ++i, t += 16)
        {
            record = 0.4f * std::sin(i * 0.03f);                  // the record (and the occupant's aim with it)
            const float gun = 0.4f * std::sin((i - 9) * 0.03f) - 0.25f * std::cos(i * 0.03f);   // lagging, off
            camera.step(true, 1.0f, record, 1.0f, gun, t);
            occupant.step(true, 1.0f, record, 1.0f, record, t);
            const auto v = CombineStints(camera, occupant, true);
            if (yawAt < 0 && v.yaw_ok) yawAt = i;
            if (pitchAt < 0 && v.pitch_ok) pitchAt = i;
        }
        const auto v = CombineStints(camera, occupant, true);
        Check(!camera.pitch_ok, "(setup) the camera check never proves the turret's pitch against the gun");
        Check(yawAt >= 0 && yawAt <= 31 && pitchAt >= 0 && pitchAt <= 31,
              "the occupant's aim proves a turret's yaw and pitch in ~0.5 s");
        Check(v.pitch_ok && v.pitch_by_occupant && !v.pitch_failed, "and the pitch is written, proven by the occupant");
        // the same seat with the occupant's vector unbound: exactly the camera check, pitch failed on the servo
        const auto off = CombineStints(camera, occupant, false);
        Check(off.yaw_ok == camera.yaw_ok && !off.pitch_ok && off.pitch_failed == camera.pitch_failed &&
                  !off.yaw_by_occupant && !off.pitch_by_occupant,
              "unbound, a gun-slaved seat proves exactly as before (fail closed)");
    }
    {   // a turret whose YAW slews too (the Warthog's 60 deg/s yaw cap): the hand sweeps, the gun trails 10+ deg in
        // yaw and pitch; only the occupant proves the yaw, and the pitch with it
        StintCheck camera, occupant;
        unsigned long long t = 1000;
        for (int i = 0; i < 60; ++i, t += 16)
        {
            const float yaw = 1.0f + 0.5f * std::sin(i * 0.05f), pitch = 0.3f * std::sin(i * 0.05f);
            const float gunYaw = 1.0f + 0.5f * std::sin((i - 12) * 0.05f) - 0.2f;
            const float gunPitch = 0.3f * std::sin((i - 12) * 0.05f) - 0.2f;
            camera.step(true, yaw, pitch, gunYaw, gunPitch, t);
            occupant.step(true, yaw, pitch, yaw, pitch, t);
        }
        const auto v = CombineStints(camera, occupant, true);
        Check(!camera.yaw_ok && v.yaw_ok && v.yaw_by_occupant && v.pitch_ok && v.pitch_by_occupant,
              "a slewing yaw: the occupant proves both axes the camera cannot");
        Check(!CombineStints(camera, occupant, true, true, false).yaw_ok,
              "with its yaw proof revoked the slewing yaw is unproven again");
    }
    {   // the occupant's vector holding something else (a wrong field): it never passes, the camera check decides
        StintCheck camera, occupant;
        unsigned long long t = 1000;
        for (int i = 0; i < 400; ++i, t += 16)
        {
            camera.step(true, 0.5f, 0.1f, 0.5f, 0.1f, t);
            occupant.step(true, 0.5f, 0.1f, 1.5f, -0.4f, t);
        }
        const auto v = CombineStints(camera, occupant, true);
        Check(v.yaw_ok && v.pitch_ok && !v.yaw_by_occupant && !v.pitch_by_occupant,
              "a disagreeing occupant vector proves nothing; the camera check still passes as before");
        Check(occupant.agreeing(0) == 0 && occupant.agreeing(1) == 0, "and it never earns the servo's trust");
    }
    {   // the outcome guard REVOKED the occupant's pitch: the camera's check alone decides it again
        StintCheck camera, occupant;
        unsigned long long t = 1000;
        for (int i = 0; i < 400; ++i, t += 16)
        {
            camera.step(true, 0.0f, 0.1f, 0.0f, 0.33f, t);      // the gun 13 deg off the record, at rest
            occupant.step(true, 0.0f, 0.1f, 0.0f, 0.1f, t);     // the occupant's aim on the record
        }
        Check(CombineStints(camera, occupant, true).pitch_by_occupant, "(setup) the occupant proved the pitch");
        const auto v = CombineStints(camera, occupant, true, false, true);
        Check(v.yaw_ok && !v.pitch_ok && v.pitch_failed == camera.pitch_failed && !v.pitch_by_occupant,
              "revoked: the pitch is the camera check's again (a real offset stays on the servo)");
        const auto y = CombineStints(camera, occupant, true, true, false);
        Check(y.yaw_ok && !y.yaw_by_occupant, "a revoked yaw the camera also proved stays proven");
    }
    {   // a pitch fails only when every active check has failed it
        StintCheck camera, occupant;
        unsigned long long t = 1000;
        for (int i = 0; i < 400 && !camera.pitch_failed; ++i, t += 16)
        {
            camera.step(true, 0.0f, 0.0f, 0.0f, 0.3f, t);
            occupant.step(true, 0.0f, 0.0f, 0.0f, 0.3f, t);
        }
        Check(camera.pitch_failed && occupant.pitch_failed, "(setup) both checks failed the pitch");
        Check(CombineStints(camera, occupant, true).pitch_failed, "both failed: the pitch waits for its retry");
        StintCheck fresh;
        for (int i = 0; i < 31; ++i, t += 16) fresh.step(true, 0.0f, 0.0f, 0.0f, 0.3f, t);
        Check(!CombineStints(camera, fresh, true).pitch_failed, "an occupant check still running is not a failure");
    }
    {   // agreeing() counts the judged agreements, saturates when proven, and reads 0 while a failed pitch waits
        StintCheck c;
        unsigned long long t = 1000;
        for (int i = 0; i < 10; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        Check(c.agreeing(0) == 9 && c.agreeing(1) == 9, "nine judged agreements on each axis");
        for (int i = 0; i < 30; ++i, t += 16) c.step(true, 0.0f, 0.0f, 0.0f, 0.0f, t);
        Check(c.agreeing(0) == c.needed && c.agreeing(1) == c.needed, "proven axes read as fully agreeing");
        // a pitch that agrees in runs of 18 and disagrees (settled) between them never reaches 30: it fails its budget
        // mid-run, with agreements still counted
        StintCheck f;
        for (int i = 0; i < 400 && !f.pitch_failed; ++i, t += 16)
            f.step(true, 0.0f, 0.0f, 0.0f, (i % 21 < 2) ? 0.3f : 0.0f, t);
        Check(f.pitch_failed && f.pitch_count > 0, "(setup) the pitch failed mid-run");
        Check(f.agreeing(1) == 0, "a failed pitch reads no agreement until its retry");
    }
}

void TestOutcomeGate()
{
    using seat_drive::OutcomeGate;
    {
        OutcomeGate g;
        Check(!g.step(0.01f, false, 1000), "on target: the target's reticle");
        Check(!g.step(0.05f, false, 1016), "a miss must persist (hand flick lag)");
        Check(!g.step(0.05f, false, 1100), "not yet 120 ms");
        Check(g.step(0.05f, false, 1140), "a persistent miss shows the game's aim");
        Check(g.step(0.025f, false, 1160), "between exit and enter: stays honest");
        Check(!g.step(0.01f, false, 1180), "back under one degree: the target again");
    }
    {
        OutcomeGate g;
        Check(g.step(0.0f, true, 1000), "a latch hold is honest at once");
        Check(!g.step(0.0f, false, 1016), "and releases as soon as the aim is on the target");
        Check(g.step(std::nanf(""), false, 1032), "a non-finite miss is never claimed as converged");
    }
    {
        OutcomeGate g;
        g.step(0.05f, false, 1000);
        g.step(0.01f, false, 1050);           // dipped back inside: the hold restarts
        Check(!g.step(0.05f, false, 1130), "the hold restarts after the miss closes");
        Check(g.step(0.05f, false, 1260), "and then completes");
    }
}
} // namespace

int main()
{
    TestPolicy();
    TestStintCheck();
    TestCombineStints();
    TestOutcomeGate();
    std::printf("Seat drive policy: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
