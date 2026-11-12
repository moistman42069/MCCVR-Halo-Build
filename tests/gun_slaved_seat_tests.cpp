// Gun-slaved seats (src/common/gun_slaved_seat_logic.h): the seat flag, the occupant-aim binding's agreement rules,
// the servo's feedback choice, the pacer that stops a servo closing on a slewing gun from nodding, the gun-settle
// gate of the crosshair witness, the slew rate and the nod witness; and the gun camera's own offset from the
// occupant's aim -- its measurement in the turret frame, the stillness gate, the learner, the per-definition book, the
// feed-forward shift. The offset half is MUTATION-CHECKED by tools/mutate_vehicle_gun_aim.py (15 of 15 caught).
#include "gun_slaved_seat_logic.h"
#include "aim_servo_logic.h"

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

using namespace gun_slaved;

void TestSeatFlag()
{
    Check(CameraSlavedToGun(0x00101198u), "the Warthog turret gunner's seat (0x00101198) is gun-slaved");
    Check(!CameraSlavedToGun(0x00101118u), "the same seat without bit 7 is not");
    Check(!CameraSlavedToGun(0x00000040u) && !CameraSlavedToGun(0x00000100u), "bits 6 and 8 are not bit 7");
}

UnitAimSites Halo3Sites()
{
    UnitAimSites s;
    s.cameraForward = 0x1AC; s.cameraForwardZ = 0x1B4; s.cameraSeatStride = 0xD4; s.cameraFlagShift = 7;
    s.controlForward = 0x1AC; s.controlForwardZ = 0x1B4; s.controlPitchWrite = 0xD8; s.controlYawWrite = 0xD4;
    return s;
}

void TestBinding()
{
    const char* why = nullptr;
    Check(ResolveUnitAimOffset(Halo3Sites(), 0xD4, 0xD4, why) == 0x1AC && !why, "Halo 3's two consumers agree: +0x1AC");
    UnitAimSites odst = Halo3Sites();
    odst.cameraForward = odst.controlForward = 0x1A4;
    odst.cameraForwardZ = odst.controlForwardZ = 0x1AC;
    Check(ResolveUnitAimOffset(odst, 0xD4, 0xD4, why) == 0x1A4, "ODST's agree on its own offset, +0x1A4");

    UnitAimSites s = Halo3Sites();
    s.cameraFlagShift = 6;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why) && why, "a camera that tests another bit binds nothing");
    s = Halo3Sites(); s.cameraSeatStride = 0xD8;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why), "a seat stride that is not the seat block's binds nothing");
    s = Halo3Sites(); s.controlForward = 0x1B8; s.controlForwardZ = 0x1C0;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why), "consumers reading neighbouring vectors bind nothing");
    s = Halo3Sites(); s.cameraForwardZ = 0x1B0;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why), "a z that is not beside x/y binds nothing");
    s = Halo3Sites(); s.controlYawWrite = 0x138; s.controlPitchWrite = 0x13C;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why), "a reset that writes another record's angles binds nothing");
    s = Halo3Sites();
    Check(!ResolveUnitAimOffset(s, 0xD4, 0x138, why), "nor against a title whose record is elsewhere");
    s = Halo3Sites(); s.cameraForward = s.controlForward = 0x1AD; s.cameraForwardZ = s.controlForwardZ = 0x1B5;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why), "a misaligned offset binds nothing");
    s = Halo3Sites(); s.cameraForward = s.controlForward = 0x40; s.cameraForwardZ = s.controlForwardZ = 0x48;
    Check(!ResolveUnitAimOffset(s, 0xD4, 0xD4, why), "an offset in the object header binds nothing");
}

void TestAngles()
{
    float yaw = 0, pitch = 0;
    const float level[3]{0.0f, 1.0f, 0.0f};
    Check(AnglesFromVector(level, yaw, pitch) && std::fabs(yaw - 1.5707963f) < 1e-5f && std::fabs(pitch) < 1e-6f,
          "a unit vector along +y is yaw 90 deg, level");
    const float up[3]{0.7071068f, 0.0f, 0.7071068f};
    Check(AnglesFromVector(up, yaw, pitch) && std::fabs(pitch - 0.7853982f) < 1e-5f, "45 deg up");
    const float shortVec[3]{0.5f, 0.0f, 0.0f};
    Check(!AnglesFromVector(shortVec, yaw, pitch), "a vector that is not a normal is not this field");
    const float zero[3]{0.0f, 0.0f, 0.0f};
    Check(!AnglesFromVector(zero, yaw, pitch), "zeroes are no sample");
    const float bad[3]{std::nanf(""), 0.0f, 1.0f};
    Check(!AnglesFromVector(bad, yaw, pitch), "a NaN is no sample");
    Check(std::fabs(AngleBetweenDeg(0.0f, 0.0f, 0.0f, 0.2268928f) - 13.0f) < 0.01f, "13 deg of pitch apart");
    Check(std::fabs(AngleBetweenDeg(3.13f, 0.0f, -3.13f, 0.0f) - 1.3284f) < 0.01f, "yaw across the wrap");
    Check(std::fabs(AngleBetweenDeg(0.0f, 1.5f, 3.0f, 1.5f) - 8.08f) < 0.1f, "near vertical, yaw counts by cos(pitch)");
    Check(std::isnan(AngleBetweenDeg(std::nanf(""), 0.0f, 0.0f, 0.0f)), "NaN in, NaN out");
}

void TestOffsetWatch()
{
    {   // a gun lagging the record while it moves, then catching up: never revoked
        OffsetWatch w;
        float gun = 0.0f;
        for (int i = 0; i < 200; ++i)
        {
            const float record = i < 60 ? 0.005f * i : 0.3f;   // the hand sweeps 17 deg, then holds
            gun += std::fabs(record - gun) < 0.018f ? record - gun : (record > gun ? 0.018f : -0.018f);
            w.step(record, gun);
        }
        Check(!w.revoked && w.count == 0, "a gun that lags and then lands is no offset");
    }
    {   // a gun that comes to rest 13 deg off the record: revoked after 30 still samples
        OffsetWatch w;
        int at = -1;
        for (int i = 0; i < 100 && at < 0; ++i)
            if (w.step(0.3f, 0.3f - 0.227f)) at = i;
        Check(at == 30, "a gun at rest 13 deg off the record revokes the occupant's proof on the 30th still sample");
        Check(w.step(0.3f, 0.3f), "and the revocation holds for the stint");
    }
    {   // a gap while the gun still moves is no evidence; one in-band sample at rest restarts the count
        OffsetWatch w;
        for (int i = 0; i < 25; ++i) w.step(0.3f, 0.0f);
        Check(w.count == 24, "(setup) 24 still disagreements");
        w.step(0.3f, 0.02f);
        Check(w.count == 24, "a moving gun neither counts nor resets");
        w.step(0.3f, 0.29f);
        w.step(0.3f, 0.29f);
        Check(w.count == 0 && !w.revoked, "back within 3 deg at rest: the count restarts");
    }
    {   // yaw wraps
        OffsetWatch w;
        w.wrap = true;
        for (int i = 0; i < 60; ++i) w.step(6.28f, -0.002f);
        Check(!w.revoked, "a record at 2pi and a gun just below 0 are the same heading");
    }
    {   // NaN: no sample
        OffsetWatch w;
        for (int i = 0; i < 60; ++i) w.step(std::nanf(""), 0.0f);
        Check(!w.revoked && !w.have, "a NaN is no sample");
    }
}

void TestServoFeedback()
{
    Check(ServoFeedback(true, true, kServoTrustSamples) == Feedback::Occupant,
          "a gun-slaved seat whose occupant has agreed closes on the occupant");
    Check(ServoFeedback(true, true, kServoTrustSamples - 1) == Feedback::Gun, "not yet trusted: the gun");
    Check(ServoFeedback(true, false, 30) == Feedback::Gun, "no fresh occupant sample: the gun");
    Check(ServoFeedback(false, true, 30) == Feedback::Gun, "any other seat: the camera, as before");
}

// A simulated turret axis: the stick integrates the record (rate per poll at full deflection, with ToRawStick's 27.5%
// floor), the gun slews toward the record at a capped rate, and the servo sees its feedback `latency` polls late
// (the input path, the control update, the tick). `occupant`: the feedback is the record's twin (the occupant's aim)
// rather than the gun.
struct TurretSim
{
    float stickRate = 0.05f;   // rad per poll at full deflection (~340 deg/s at 120 Hz)
    float gunRate = 0.018f;    // rad per poll (~125 deg/s)
    int latency = 2;
    bool occupant = false;
    float record = 0.0f, gun = 0.0f;
    float gunSeen[8]{}, recordSeen[8]{};
    float feedback() const { return occupant ? recordSeen[0] : gunSeen[0]; }
    void poll(float command)
    {
        float c = command;
        if (c != 0.0f && std::fabs(c) < kAimServoStickFloor) c = c > 0 ? kAimServoStickFloor : -kAimServoStickFloor;
        record += c * stickRate;
        const float toward = record - gun;
        gun += std::fabs(toward) <= gunRate ? toward : (toward > 0 ? gunRate : -gunRate);
        for (int i = 0; i < latency; ++i) { gunSeen[i] = gunSeen[i + 1]; recordSeen[i] = recordSeen[i + 1]; }
        gunSeen[latency] = gun;
        recordSeen[latency] = record;
    }
};

struct ServoRun
{
    unsigned gunReversals = 0, handReversals = 0;
    float finalErrDeg = 0.0f, maxTrackErrDeg = 0.0f;
};

// `paced`: the gun-slaved pacer; otherwise the parking servo exactly as the turret seat has always run it.
ServoRun RunServo(TurretSim sim, bool paced, bool movingHand)
{
    AimServoAxis axis;
    ServoPacer pacer;
    NodWitness gunNod, handNod;
    float lastCommand = 0.0f;
    ServoRun run;
    const int polls = 900;
    for (int i = 0; i < polls; ++i)
    {
        const float target = movingHand ? 0.35f + 0.2f * std::sin(i * 0.02f) : 0.35f;
        const float feedback = sim.feedback();
        AimServoObserve(axis, feedback, lastCommand);
        const float error = target - feedback;
        float command = AimServoCommand(axis, error, 12.0f, paced ? pacer.enter(kAimServoRestEnterRadians)
                                                                  : kAimServoRestEnterRadians,
                                        paced ? pacer.exit(kAimServoRestExitRadians) : kAimServoRestExitRadians);
        if (paced)
            command = pacer.pace(feedback, error, command, kAimServoStickFloor);
        sim.poll(command);
        lastCommand = std::fabs(command);
        if (i >= polls / 3)
        {
            gunNod.step(sim.gun);
            handNod.step(target);
            const float e = std::fabs(target - sim.gun) * kRadToDeg;
            if (e > run.maxTrackErrDeg) run.maxTrackErrDeg = e;
        }
        run.finalErrDeg = std::fabs(target - sim.gun) * kRadToDeg;
    }
    float swing = 0.0f;
    gunNod.take(run.gunReversals, swing);
    handNod.take(run.handReversals, swing);
    return run;
}

void TestPacer()
{
    {   // THE FAULT, reproduced: the parking servo closing on a gun it sees two polls late nods without end
        TurretSim sim;
        const ServoRun r = RunServo(sim, false, false);
        Check(r.gunReversals >= 10, "(the fault) a parking servo closing on a lagging gun nods: it keeps reversing");
    }
    // The pacer lands a 20 deg flick and stays, at every stick rate and latency tried, on either feedback.
    const float rates[]{0.02f, 0.03f, 0.05f, 0.08f, 0.12f};
    const int latencies[]{1, 2, 4, 6};
    bool gunAll = true, occupantAll = true;
    for (float rate : rates)
        for (int latency : latencies)
        {
            TurretSim sim;
            sim.stickRate = rate;
            sim.latency = latency;
            const ServoRun g = RunServo(sim, true, false);
            gunAll = gunAll && g.gunReversals == 0 && g.finalErrDeg < 1.0f;
            sim.occupant = true;
            const ServoRun o = RunServo(sim, true, false);
            occupantAll = occupantAll && o.gunReversals == 0 && o.finalErrDeg < 1.0f;
        }
    Check(gunAll, "paced on the gun: it lands within 1 deg and never reverses, at every rate and latency");
    Check(occupantAll, "paced on the occupant's aim: the same");
    {   // a hand that moves: the gun follows it, reversing only where the hand does
        TurretSim sim;
        const ServoRun r = RunServo(sim, true, true);
        Check(r.gunReversals <= r.handReversals && r.maxTrackErrDeg < 3.0f,
              "paced, the gun follows a moving hand within 3 deg and reverses only with it");
    }

    ServoPacer p;
    Check(p.pace(0.0f, 0.3f, 1.0f, kAimServoStickFloor) == 1.0f && p.waiting, "a command goes out as one pulse");
    Check(p.pace(0.0f, 0.3f, 1.0f, kAimServoStickFloor) == 0.0f && p.holds == 1, "then it waits for an answer");
    Check(p.pace(0.02f, 0.28f, 1.0f, kAimServoStickFloor) == 0.0f, "the feedback moving: still waiting");
    Check(p.pace(0.0202f, 0.2798f, 1.0f, kAimServoStickFloor) == 1.0f, "moved and at rest: the next pulse");
    ServoPacer t;
    t.timeoutPolls = 3;
    (void)t.pace(0.0f, 0.3f, 1.0f, kAimServoStickFloor);
    for (int i = 0; i < 2; ++i) (void)t.pace(0.0f, 0.3f, 1.0f, kAimServoStickFloor);
    Check(t.pace(0.0f, 0.3f, 1.0f, kAimServoStickFloor) == 1.0f, "a feedback that never answers is asked again");
    ServoPacer q;
    (void)q.pace(0.0f, 0.01f, 0.12f, kAimServoStickFloor);    // a floor pulse from rest
    (void)q.pace(0.005f, 0.005f, 0.0f, kAimServoStickFloor);  // it moves 0.29 deg
    (void)q.pace(0.005f, 0.005f, 0.0f, kAimServoStickFloor);  // and rests: that is the quantum
    Check(std::fabs(q.quantum - 0.005f) < 1e-6f, "a floor pulse's measured effect is the quantum");
    Check(q.enter(0.001f) == 0.003f && q.exit(0.001f) == 0.0075f, "the rest band widens to it");
    Check(q.enter(0.01f) == 0.01f, "never narrower than the servo's own band");
    ServoPacer w;
    w.wrap = true;
    (void)w.pace(3.13f, 0.1f, 0.0f, kAimServoStickFloor);
    Check(w.pace(-3.13f, 0.1f, 1.0f, kAimServoStickFloor) == 0.0f, "yaw wraps: +pi to -pi is arriving, not leaving");
    ServoPacer n;
    Check(n.pace(std::nanf(""), 0.1f, 0.5f, kAimServoStickFloor) == 0.5f, "no feedback: the command as it was");
}

void TestGunSettle()
{
    GunSettle s;
    Check(!s.step(0.05f, 1000), "3 deg off: unsettled");
    Check(!s.step(0.01f, 1100), "inside 1 deg: not yet 120 ms");
    Check(s.episodeDone && s.lastSettleMs == 100, "the gun took 100 ms to land");
    Check(s.step(0.012f, 1220), "120 ms inside: settled");
    Check(!s.step(0.02f, 1230), "it leaves the band: unsettled at once");
    Check(!s.step(std::nanf(""), 1240), "nothing written to compare: unsettled");
    s.episodeDone = false;
    (void)s.step(0.001f, 1300);
    Check(!s.episodeDone, "no episode is timed across a gap with no write");
    s.reset();
    Check(!s.settled && !s.inBand && s.tol == 0.01745f, "reset keeps the band");
}

void TestSlewRate()
{
    SlewRate r;
    r.step(0.0f, 0.0f, 1000);
    const float rate = r.step(0.0f, 0.0349066f, 1016);   // 2 deg in 16 ms
    Check(std::fabs(rate - 125.0f) < 1.0f && std::fabs(r.peakDegPerS - 125.0f) < 1.0f, "2 deg in 16 ms: 125 deg/s");
    Check(r.step(0.0f, 0.0f, 2000) == 0.0f, "a gap over 100 ms is no rate");
    Check(r.step(0.0f, 0.0f, 2000) == 0.0f, "a repeated timestamp is no rate");
}

void TestNodWitness()
{
    NodWitness n;
    for (int i = 0; i < 300; ++i)
        n.step(0.1f * std::sin(i * 0.1f));   // a 11.5 deg peak-to-peak nod, period ~63 polls
    unsigned rev = 0;
    float swing = 0.0f;
    n.take(rev, swing);
    Check(rev >= 8 && rev <= 10, "a sine nod reverses twice a period");
    Check(std::fabs(swing - 0.2f) < 0.01f, "its swing is peak to peak");
    n.take(rev, swing);
    Check(rev == 0 && swing == 0.0f, "take empties the window");
    NodWitness q;
    for (int i = 0; i < 300; ++i)
        q.step(0.3f + 0.001f * ((i % 2) ? 1.0f : -1.0f));   // 0.1 deg of chatter
    q.take(rev, swing);
    Check(rev == 0, "chatter inside the deadband is no nod");
    NodWitness ramp;
    for (int i = 0; i < 100; ++i) ramp.step(i * 0.01f);
    ramp.take(rev, swing);
    Check(rev == 0, "a steady climb is no reversal");
}

// ---- the gun's own offset -----------------------------------------------------------------------------------
constexpr float kD = 0.017453292519943295f;

// A turret frame tilted 15 deg nose-up and 10 deg to the side (a Warthog on a slope), yawed 40 deg.
seat_frame::Frame TiltedFrame()
{
    const float up[3]{std::sin(15.0f * kD), std::sin(10.0f * kD), 1.0f};
    seat_frame::Frame raw{};
    seat_frame::BuildFrame(up, raw);
    // Turn the frame about its up by 40 deg so forward is not the world's.
    seat_frame::Frame f = raw;
    const float c = std::cos(40.0f * kD), s = std::sin(40.0f * kD);
    const float ul = std::sqrt(seat_frame::Dot(raw.u, raw.u)), ll = std::sqrt(seat_frame::Dot(raw.l, raw.l));
    for (int i = 0; i < 3; ++i)
    {
        f.u[i] = raw.u[i] / ul;
        f.f[i] = c * raw.f[i] + s * raw.l[i] / ll;
        f.l[i] = -s * raw.f[i] + c * raw.l[i] / ll;
    }
    return f;
}

bool NearRad(float a, float b, float tol) { return std::fabs(std::remainder(a - b, 6.28318531f)) <= tol; }

// The angle between two (yaw, pitch) directions, degrees, exact at small angles (AngleBetweenDeg's acos is not).
float SepDeg(float yawA, float pitchA, float yawB, float pitchB)
{
    float a[3], b[3];
    seat_frame::Forward(yawA, pitchA, a);
    seat_frame::Forward(yawB, pitchB, b);
    return seat_frame::SeparationDeg(a, b);
}

void TestOffsetInFrame()
{
    const seat_frame::Frame level{};
    float dy = 0.0f, dp = 0.0f, sep = 0.0f;
    Check(OffsetInFrame(level, 0.30f, 0.10f, 0.29f, 0.06f, dy, dp, sep) && std::fabs(dy - 0.01f) < 1e-5f &&
              std::fabs(dp - 0.04f) < 1e-5f,
          "a level frame: the offset is the plain angle difference");
    Check(std::fabs(sep - AngleBetweenDeg(0.30f, 0.10f, 0.29f, 0.06f) / kRadToDeg) < 1e-4f,
          "the separation is the angle between the two directions");
    Check(OffsetInFrame(level, 0.01f, 0.0f, 6.27f, 0.0f, dy, dp, sep) && std::fabs(dy - (0.01f + 6.28318531f - 6.27f)) < 1e-4f,
          "the yaw offset wraps across the seam");
    // The same fixed rotation of the marker, measured on a tilted hull, comes back as the same frame offset at every
    // gun direction; the world-angle difference does not.
    const seat_frame::Frame tilt = TiltedFrame();
    bool same = true, worldDiffers = false;
    for (float gy = -0.6f; gy <= 0.6f; gy += 0.3f)
        for (float gp = -0.2f; gp <= 0.5f; gp += 0.35f)
        {
            seat_frame::Angles occFrame{gy, gp}, gunFrame{gy + 0.004f, gp + 0.040f};
            seat_frame::Angles occW{}, gunW{};
            seat_frame::WorldFromRecord(tilt, occFrame.yaw, occFrame.pitch, occW);
            seat_frame::WorldFromRecord(tilt, gunFrame.yaw, gunFrame.pitch, gunW);
            float y = 0.0f, p = 0.0f, s = 0.0f;
            same = same && OffsetInFrame(tilt, gunW.yaw, gunW.pitch, occW.yaw, occW.pitch, y, p, s) &&
                   std::fabs(y - 0.004f) < 2e-4f && std::fabs(p - 0.040f) < 2e-4f;
            worldDiffers = worldDiffers || std::fabs(std::remainder(gunW.yaw - occW.yaw, 6.28318531f) - 0.004f) > 2e-3f;
        }
    Check(same, "a marker's fixed rotation reads the same in the turret frame across the gun's range");
    Check(worldDiffers, "...while in world angles it moves with the tilt (the frame matters)");
    Check(!OffsetInFrame(level, std::nanf(""), 0.0f, 0.0f, 0.0f, dy, dp, sep), "a non-finite gun is no sample");
}

void TestShiftInFrame()
{
    const seat_frame::Frame tilt = TiltedFrame();
    float y = 0.0f, p = 0.0f, back = 0.0f, backP = 0.0f;
    Check(ShiftInFrame(tilt, 1.0f, 0.2f, 0.01f, -0.04f, y, p) && ShiftInFrame(tilt, y, p, -0.01f, 0.04f, back, backP) &&
              NearRad(back, 1.0f, 2e-5f) && std::fabs(backP - 0.2f) < 2e-5f,
          "shifting by delta and back is the identity");
    seat_frame::Angles a{}, b{};
    AnglesInFrame(tilt, 1.0f, 0.2f, 0.0f, a);
    AnglesInFrame(tilt, y, p, 0.0f, b);
    Check(NearRad(b.yaw - a.yaw, 0.01f, 2e-5f) && std::fabs((b.pitch - a.pitch) + 0.04f) < 2e-5f,
          "the shift is applied in the frame's own angles");
    const seat_frame::Frame level{};
    Check(ShiftInFrame(level, 6.27f, 0.0f, 0.03f, 0.0f, y, p) && std::fabs(y - 6.30f) < 1e-4f,
          "the result stays on the input's yaw branch (no 2pi jump)");
    Check(ShiftInFrame(level, 0.0f, 1.5f, 0.0f, 0.2f, y, p) && p <= 1.55f + 1e-5f && NearRad(y, 0.0f, 1e-3f),
          "pitch is held short of the pole (never over it: the yaw does not flip)");
    Check(!ShiftInFrame(level, 0.0f, 0.0f, std::nanf(""), 0.0f, y, p), "a non-finite shift is refused");
}

void TestStillness()
{
    Stillness s;
    float v[6]{0.1f, 0.2f, 0.1f, 0.18f, 0.5f, 0.3f};
    Check(!s.step(v, 1000), "the first sample anchors");
    Check(!s.step(v, 1050), "50 ms still is not yet still");
    v[0] += 0.001f;   // 0.06 deg of gun: inside 0.1
    v[4] += 0.006f;   // 0.34 deg of hand: inside 0.5
    Check(s.step(v, 1100), "100 ms within the tolerances is still");
    v[1] += 0.003f;   // the gun moves 0.17 deg
    Check(!s.step(v, 1110), "a gun that moves re-anchors");
    Check(!s.step(v, 1200), "...and waits its 100 ms again");
    Check(s.step(v, 1210), "...from where it moved to");
    v[5] += 0.01f;    // the hand moves 0.57 deg
    Check(!s.step(v, 1220), "a hand that moves re-anchors");
    v[2] += 6.28318531f;   // the occupant's yaw on another branch is the same direction
    Check(!s.step(v, 1300), "(re-anchored by the hand)");
    Check(s.step(v, 1330), "a yaw on another 2pi branch is not motion");
    v[3] = std::nanf("");
    Check(!s.step(v, 1400), "a non-finite angle is never still");
}

void TestOffsetLearner()
{
    OffsetLearner l;
    Check(!l.usable(), "nothing learned yet");
    for (unsigned i = 0; i < l.ready - 1; ++i)
        l.step(0.004f, 0.041f);
    Check(!l.usable(), "one sample short of ready");
    l.step(0.004f, 0.041f);
    Check(l.usable() && std::fabs(l.pitch - 0.041f) < 1e-6f, "ready after `ready` samples, at the sample's value");
    for (int i = 0; i < 200; ++i)
        l.step(0.002f, 0.039f);
    Check(std::fabs(l.pitch - 0.039f) < 1e-4f && std::fabs(l.yaw - 0.002f) < 1e-4f,
          "the average converges on a new steady offset");
    OffsetLearner noisy;
    for (int i = 0; i < 400; ++i)
        noisy.step(0.0f, (i % 2) ? 0.042f : 0.036f);
    Check(std::fabs(noisy.pitch - 0.039f) < 4e-4f, "alternating samples average to their mean, not the last one");
    // Refused by identity only, never by size (there is no "over 5 deg" refusal).
    const unsigned samples = l.samples;
    Check(!l.step(0.0f, 0.02f, OffsetRefusal::HeldAtStop) && l.refused == 1 &&
              l.refusedBy[static_cast<unsigned>(OffsetRefusal::HeldAtStop)] == 1 && l.samples == samples &&
              std::fabs(l.pitch - 0.039f) < 1e-4f,
          "a sample with the seat held at its stop is refused, counted by reason, and averages nothing in");
    Check(!l.step(0.0f, 0.02f, OffsetRefusal::OccupantUnproven) &&
              l.refusedBy[static_cast<unsigned>(OffsetRefusal::OccupantUnproven)] == 1 && l.refused == 2,
          "a sample before the occupant's aim is proven is refused by its own reason");
    Check(!l.step(std::nanf(""), 0.02f) && l.refusedBy[static_cast<unsigned>(OffsetRefusal::NonFinite)] == 1 &&
              l.refused == 3 && l.samples == samples,
          "a non-finite sample is counted as one, never averaged");
    Check(l.step(0.0f, 0.10f) && l.samples == samples + 1, "a 5.7 deg sample is averaged in: no size limit");
    Check(l.step(0.0f, 0.30f), "...nor at 17 deg");
    l.reset();
    Check(!l.refused && !l.refusedBy[static_cast<unsigned>(OffsetRefusal::HeldAtStop)], "a reset clears the counts");
    Check(OffsetSampleRefusal(true, false) == OffsetRefusal::None, "proven and free: a sample");
    Check(OffsetSampleRefusal(false, false) == OffsetRefusal::OccupantUnproven, "unproven occupant: refused");
    Check(OffsetSampleRefusal(true, true) == OffsetRefusal::HeldAtStop, "held at a stop: refused");
    Check(OffsetSampleRefusal(false, true) == OffsetRefusal::HeldAtStop, "held wins: the stop explains the gap");
    OffsetLearner s;
    s.seed(0.001f, 0.04f, 100);
    Check(s.usable() && s.samples == s.ready, "seeded from the definition: usable at once");
    OffsetLearner n;
    n.seed(0.0f, 0.0f, 0);
    Check(!n.have, "a seed of no samples seeds nothing");
}

void TestOffsetBook()
{
    OffsetBook b;
    Check(!b.find(0x069C, 3), "an empty book has nothing");
    b.store(0x069C, 3, 0.001f, 0.041f, 40);
    const auto* e = b.find(0x069C, 3);
    Check(e && std::fabs(e->pitch - 0.041f) < 1e-7f && e->samples == 40, "stored per definition");
    Check(!b.find(0x069C, 4), "another generation is another tag set");
    Check(!b.find(0x0697, 3), "another definition is another gun");
    b.store(0x069C, 3, 0.002f, 0.042f, 60);
    Check(b.find(0x069C, 3)->samples == 60, "a definition's entry is updated in place");
    for (uint32_t d = 0; d < 8; ++d)
        b.store(0x100 + d, 3, 0.0f, 0.0f, 1);
    Check(!b.find(0x069C, 3), "the oldest entry gives way");
    b.store(0xFFFFFFFFu, 3, 0.0f, 0.0f, 5);
    Check(!b.find(0xFFFFFFFFu, 3), "an unknown definition is never stored");
}

void TestOffsetRange()
{
    OffsetRange r;
    r.note(0.01f, 0.04f, -0.5f, 0.1f);
    r.note(0.00f, 0.05f, 0.4f, -0.2f);
    r.note(std::nanf(""), 0.0f, 0.0f, 0.0f);
    Check(r.samples == 2, "a non-finite sample is not counted");
    Check(r.minYaw == 0.0f && r.maxYaw == 0.01f && r.minPitch == 0.04f && r.maxPitch == 0.05f, "the deltas' range");
    Check(r.gunMinYaw == -0.5f && r.gunMaxYaw == 0.4f && r.gunMinPitch == -0.2f && r.gunMaxPitch == 0.1f,
          "the gun's range");
}

// End to end: a gun whose camera sits a fixed rotation off the occupant's aim, on a tilted hull. Learn it from still
// samples, then feed it forward: the camera lands on the ray it is meant to (the residual is the learning error).
void TestFeedForward()
{
    const seat_frame::Frame tilt = TiltedFrame();
    const float trueYaw = 0.0035f, truePitch = 0.0410f;   // ~0.2 and 2.35 deg (an example: larger than any measured)
    OffsetLearner learner;
    Stillness still;
    unsigned long long now = 0;
    for (int pose = 0; pose < 6; ++pose)
    {
        const float occYaw = -0.4f + 0.15f * pose, occPitch = -0.1f + 0.08f * pose;
        seat_frame::Angles occF{}, occW{}, gunW{};
        AnglesInFrame(tilt, occYaw, occPitch, 0.0f, occF);
        seat_frame::WorldFromRecord(tilt, occF.yaw, occF.pitch, occW);
        seat_frame::WorldFromRecord(tilt, occF.yaw + trueYaw, occF.pitch + truePitch, gunW);
        for (int i = 0; i < 30; ++i, now += 8)
        {
            const float v[6]{gunW.yaw, gunW.pitch, occW.yaw, occW.pitch, occW.yaw, occW.pitch};
            if (!still.step(v, now))
                continue;
            float dy = 0.0f, dp = 0.0f, sep = 0.0f;
            if (OffsetInFrame(tilt, gunW.yaw, gunW.pitch, occW.yaw, occW.pitch, dy, dp, sep))
                learner.step(dy, dp, OffsetSampleRefusal(true, false));
        }
    }
    Check(learner.usable() && std::fabs(learner.yaw - trueYaw) < 1e-4f && std::fabs(learner.pitch - truePitch) < 1e-4f,
          "the offset is learned from still samples across the gun's range");
    // The ray through the point (what the solve hands the drive), and the record written for it.
    const float rayYaw = 0.25f, rayPitch = -0.12f;
    float recYaw = 0.0f, recPitch = 0.0f, camYaw = 0.0f, camPitch = 0.0f;
    Check(ShiftInFrame(tilt, rayYaw, rayPitch, -learner.yaw, -learner.pitch, recYaw, recPitch),
          "the record is the ray minus delta");
    // The game: the occupant's aim is the record; the camera is that plus the true offset.
    ShiftInFrame(tilt, recYaw, recPitch, trueYaw, truePitch, camYaw, camPitch);
    Check(SepDeg(camYaw, camPitch, rayYaw, rayPitch) < 0.01f, "the gun's camera lands on the ray (< 0.01 deg)");
    Check(SepDeg(recYaw, recPitch, rayYaw, rayPitch) > 2.0f, "...where the uncorrected record missed by ~2.4 deg");
    // The gun as a stand-in for the record: the gun minus delta is the occupant's aim it implies.
    float impliedYaw = 0.0f, impliedPitch = 0.0f;
    ShiftInFrame(tilt, camYaw, camPitch, -learner.yaw, -learner.pitch, impliedYaw, impliedPitch);
    Check(SepDeg(impliedYaw, impliedPitch, recYaw, recPitch) < 0.01f,
          "the corrected gun reads as the record (the camera check and the outcome guard see no offset)");
}
} // namespace

// The pitch check's quiet axis: held while an attempt collects evidence, bounded per attempt, held again on a retry.
void TestCheckHold()
{
    CheckHold h;
    const uint32_t seat = 0x1234;
    Check(!h.step(0, true, true, 0, 1000), "no seat: nothing is held");
    Check(!h.step(seat, true, false, 0, 1000), "not checking (proven or failed): the servo keeps the pitch");
    Check(!h.step(seat, false, true, 0, 1010), "no fresh occupant aim: nothing is held");
    Check(h.step(seat, true, true, 0, 1020), "an attempt collecting evidence holds the pitch stick");
    Check(h.step(seat, true, true, 0, 1020 + kCheckHoldMs), "held through the whole bound");
    Check(!h.step(seat, true, true, 0, 1021 + kCheckHoldMs), "past the bound the servo takes the pitch back");
    Check(!h.step(seat, true, true, 0, 9000), "and the same attempt is never held again");
    // The attempt failed (the count of failed attempts stepped) and waits for its retry: not checking.
    Check(!h.step(seat, true, false, 1, 9100), "a failed attempt waiting for its retry is not held");
    Check(h.step(seat, true, true, 1, 14100), "the retry is a new attempt: held from its own first poll");
    Check(h.step(seat, true, true, 1, 14100 + kCheckHoldMs) && !h.step(seat, true, true, 1, 14101 + kCheckHoldMs),
          "the retry's bound counts from the retry, not from the failure");
    // A stale poll inside an attempt neither holds nor restarts the bound.
    CheckHold g;
    Check(g.step(seat, true, true, 0, 100), "held");
    Check(!g.step(seat, false, true, 0, 600), "a stale poll is not held");
    Check(g.step(seat, true, true, 0, 1100) && !g.step(seat, true, true, 0, 101 + kCheckHoldMs),
          "the bound still counts from the attempt's first held poll");
    // Another seat starts afresh; no seat forgets everything.
    Check(g.step(seat + 1, true, true, 0, 5000), "another seat's first attempt is held");
    Check(!g.step(0, true, true, 0, 5010) && g.step(seat + 1, true, true, 0, 9000),
          "leaving the seat forgets the attempt: the same seat entered again is held again");
}

int main()
{
    TestCheckHold();
    TestSeatFlag();
    TestBinding();
    TestAngles();
    TestOffsetWatch();
    TestServoFeedback();
    TestPacer();
    TestGunSettle();
    TestSlewRate();
    TestNodWitness();
    TestOffsetInFrame();
    TestShiftInFrame();
    TestStillness();
    TestOffsetLearner();
    TestOffsetBook();
    TestOffsetRange();
    TestFeedForward();
    std::printf("Gun-slaved seats: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
