// Aiming a seat's gun through the point (src/common/seat_barrel_logic.h): the barrel's geometry learned from the
// lines of the seat's own fires, and the aim for which the barrel's line runs through a point. The fixture is a
// synthetic vehicle: a seat's eye fixed in the hull, a turret pivot elsewhere in the hull, a barrel beside and
// above its pivot, turning with the aim -- and, for the shots, an engine that turns each one at the point inside
// a cone about the barrel, as the title does in a seat.
#include "seat_barrel_logic.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

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

using namespace seat_barrel;

struct Vehicle
{
    Frame hull{};
    float eye[3]{};
    double pivot[3]{};         // from the eye, in the hull's frame (forward, left, up)
    double beside = 0, above = 0;   // the barrel from its pivot, in the aim's frame (right, up)

    // The barrel's line when the gun points along `d` (unit, world): a point of it and the direction.
    void Barrel(const double d[3], float origin[3], float direction[3], double along = 1.7) const
    {
        double right[3]{}, up[3]{};
        AimAxes(hull, d, right, up);
        for (int i = 0; i < 3; ++i)
        {
            origin[i] = static_cast<float>(eye[i] + hull.f[i] * pivot[0] + hull.l[i] * pivot[1] +
                                           hull.u[i] * pivot[2] + right[i] * beside + up[i] * above + d[i] * along);
            direction[i] = static_cast<float>(d[i]);
        }
    }
    // The gun fires along `d`.
    bool Fire(Model& model, const double d[3]) const
    {
        float origin[3]{}, direction[3]{};
        Barrel(d, origin, direction);
        return model.Learn(hull, eye, origin, direction);
    }
    // How far the barrel's line passes from `point` when the gun follows the aim the model solves for it; -1 when
    // the model declines.
    double MissAt(const Model& model, const float point[3]) const
    {
        float aim[3]{};
        if (!model.Aim(hull, eye, point, aim))
            return -1.0;
        const double d[3]{aim[0], aim[1], aim[2]};
        float origin[3]{}, direction[3]{};
        Barrel(d, origin, direction);
        const double o[3]{origin[0], origin[1], origin[2]}, p[3]{point[0], point[1], point[2]};
        return LineMiss(o, d, p);
    }
    // The same for the eye's line (what a seat aimed from its camera does): the barrel parallel to it.
    double EyeLineMissAt(const float point[3]) const
    {
        double d[3]{double(point[0]) - eye[0], double(point[1]) - eye[1], double(point[2]) - eye[2]};
        Normalize(d);
        float origin[3]{}, direction[3]{};
        Barrel(d, origin, direction);
        const double o[3]{origin[0], origin[1], origin[2]}, p[3]{point[0], point[1], point[2]};
        return LineMiss(o, d, p);
    }
};

void Heading(const Frame& hull, double yawDeg, double pitchDeg, double d[3])
{
    const double yaw = yawDeg * 0.017453292519943295, pitch = pitchDeg * 0.017453292519943295;
    const double f = std::cos(pitch) * std::cos(yaw), l = std::cos(pitch) * std::sin(yaw), u = std::sin(pitch);
    for (int i = 0; i < 3; ++i)
        d[i] = hull.f[i] * f + hull.l[i] * l + hull.u[i] * u;
}

void PointAlong(const Vehicle& v, double yawDeg, double pitchDeg, double range, float point[3])
{
    double d[3]{};
    Heading(v.hull, yawDeg, pitchDeg, d);
    for (int i = 0; i < 3; ++i)
        point[i] = static_cast<float>(v.eye[i] + d[i] * range);
}

Vehicle Tank()
{
    Vehicle v{};
    // A hull pitched and rolled on a slope, heading somewhere that is no axis.
    const float forward[3]{0.80f, 0.58f, 0.15f}, up[3]{-0.10f, -0.12f, 0.98f};
    MakeFrame(forward, up, v.hull);
    v.eye[0] = 31.5f;
    v.eye[1] = -12.25f;
    v.eye[2] = 4.0f;
    v.pivot[0] = -0.90;   // the turret's pivot 0.9 wu behind the driver's eye, a little left and above
    v.pivot[1] = 0.12;
    v.pivot[2] = 0.35;
    v.beside = 0.05;
    v.above = 0.02;
    return v;
}

void TestFrame()
{
    Frame h{};
    const float forward[3]{2.0f, 0.0f, 0.2f}, up[3]{0.0f, 0.0f, 3.0f};
    Check(MakeFrame(forward, up, h), "a frame from a forward and an up that are neither unit nor square");
    Check(std::fabs(Dot(h.f, h.l)) < 1e-12 && std::fabs(Dot(h.f, h.u)) < 1e-12 && std::fabs(Dot(h.l, h.u)) < 1e-12,
          "its axes are orthogonal");
    Check(std::fabs(Dot(h.f, h.f) - 1.0) < 1e-12 && std::fabs(Dot(h.u, h.u) - 1.0) < 1e-12, "and unit");
    Check(h.l[1] > 0.99, "left is up x forward");
    const float bad[3]{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, zero[3]{};
    Check(!MakeFrame(bad, up, h) && !MakeFrame(forward, zero, h) && !MakeFrame(forward, forward, h),
          "not a number, no up, or forward along up: no frame");
    double right[3]{}, upAxis[3]{};
    Check(!AimAxes(h, h.u, right, upAxis), "an aim along the vehicle's up has no right");
}

void TestNoFire()
{
    const Vehicle v = Tank();
    Model model;
    float point[3]{};
    PointAlong(v, 10.0, 2.0, 20.0, point);
    float aim[3]{9.0f, 9.0f, 9.0f};
    Check(model.fires() == 0 && !model.Aim(v.hull, v.eye, point, aim) && aim[0] == 9.0f,
          "before the first fire the model declines and leaves the aim alone");
    Check(v.EyeLineMissAt(point) > 0.2, "fixture: the eye's line leaves this barrel well off the point");
}

void TestOneFire()
{
    const Vehicle v = Tank();
    Model model;
    double d[3]{};
    Heading(v.hull, 25.0, 3.0, d);
    Check(v.Fire(model, d) && model.fires() == 1, "one fire is learned");
    // A point the fired barrel pointed at, at any range: exact from the first fire (the gun turns back to that
    // heading), where the eye's line leaves the barrel beside it.
    float fired[3]{}, firedDirection[3]{};
    v.Barrel(d, fired, firedDirection);
    for (double range : {8.0, 20.0, 60.0})
    {
        const float point[3]{fired[0] + float(d[0] * range), fired[1] + float(d[1] * range),
                             fired[2] + float(d[2] * range)};
        const double miss = v.MissAt(model, point);
        Check(miss >= 0.0 && miss < 0.01, "after one fire the barrel's line runs through a point on the fired line");
        Check(v.EyeLineMissAt(point) > 30.0 * (miss + 1e-4), "and the eye's line would have missed it by far more");
    }
    // A heading 20 degrees away: better than the eye's line, not yet exact.
    float point[3]{};
    PointAlong(v, 45.0, 3.0, 20.0, point);
    const double miss = v.MissAt(model, point);
    Check(miss >= 0.0 && miss < v.EyeLineMissAt(point), "another heading already improves on the eye's line");
}

void TestManyFires()
{
    const Vehicle v = Tank();
    Model model;
    const double yaws[]{-70.0, -30.0, 0.0, 20.0, 55.0, 90.0}, pitches[]{-4.0, 6.0, 1.0, 9.0, -2.0, 4.0};
    for (int i = 0; i < 6; ++i)
    {
        double d[3]{};
        Heading(v.hull, yaws[i], pitches[i], d);
        Check(v.Fire(model, d), "a fire at another heading is learned");
    }
    // Headings that were never fired on, both sides, near and far.
    double worst = 0.0, worstEye = 0.0;
    for (double yaw : {-85.0, -50.0, -10.0, 35.0, 75.0, 110.0})
        for (double pitch : {-5.0, 2.0, 8.0})
            for (double range : {6.0, 25.0, 90.0})
            {
                float point[3]{};
                PointAlong(v, yaw, pitch, range, point);
                const double miss = v.MissAt(model, point);
                Check(miss >= 0.0, "the model answers for a heading it never fired on");
                if (miss > worst)
                    worst = miss;
                const double eyeMiss = v.EyeLineMissAt(point);
                if (eyeMiss > worstEye)
                    worstEye = eyeMiss;
            }
    Check(worst < 0.02, "after fires around the hull the barrel's line is within 0.02 wu of every point");
    // The fit is the vehicle: its pivot is the pivot up to a slide along the barrel (which no line can show), so
    // what it must reproduce is the barrel's line -- checked above -- and the barrel's own small offset.
    double state[5]{};
    model.State(state);
    Check(std::fabs(state[3] - v.beside) < 0.02 && std::fabs(state[4] - v.above) < 0.02,
          "and the fit has found how far the barrel sits beside and above its pivot");
    Check(worstEye > 0.8, "fixture: the eye's line misses by most of the pivot's distance somewhere");
}

void TestChildTurret()
{
    // A gun whose seat's frame turns WITH the gun (a child turret): the eye is the gun's own camera, the barrel a
    // fixed step beside it. One fire fits it for every heading, because the frame carries the heading.
    Model model;
    double worst = 0.0;
    for (int pass = 0; pass < 2; ++pass)
        for (double yaw : {0.0, 40.0, 130.0, -95.0})
        {
            Vehicle v{};
            const double yawRad = yaw * 0.017453292519943295;
            const float forward[3]{float(std::cos(yawRad)), float(std::sin(yawRad)), 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
            MakeFrame(forward, up, v.hull);
            v.eye[0] = 5.0f;
            v.eye[1] = 6.0f;
            v.eye[2] = 1.0f;
            v.pivot[1] = -0.14;   // the barrel 0.14 wu to the right of the camera, in the turret's own frame
            v.pivot[2] = -0.05;
            double d[3]{v.hull.f[0], v.hull.f[1], v.hull.f[2]};
            if (pass == 0 && yaw == 0.0)
                Check(v.Fire(model, d), "the child turret's first fire is learned");
            float point[3]{};
            PointAlong(v, 0.0, 0.0, 9.0, point);
            const double miss = v.MissAt(model, point);
            Check(miss >= 0.0, "the model answers in the turret's frame");
            if (miss > worst)
                worst = miss;
        }
    Check(worst < 0.01, "one fire fits a gun whose frame turns with it");
}

void TestRefusals()
{
    const Vehicle v = Tank();
    Model model;
    double d[3]{};
    Heading(v.hull, 0.0, 0.0, d);
    float origin[3]{}, direction[3]{};
    v.Barrel(d, origin, direction);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    float badOrigin[3]{origin[0], nan, origin[2]}, zeroDir[3]{}, longDir[3]{direction[0] * 5, direction[1] * 5, direction[2] * 5};
    float farOrigin[3]{origin[0] + 40.0f, origin[1] + 40.0f, origin[2]};
    float alongUp[3]{float(v.hull.u[0]), float(v.hull.u[1]), float(v.hull.u[2])};
    Check(!model.Learn(v.hull, v.eye, badOrigin, direction), "a line that is not a number is not learned");
    Check(!model.Learn(v.hull, v.eye, origin, zeroDir) && !model.Learn(v.hull, v.eye, origin, longDir),
          "a direction that is not a direction is not learned");
    Check(!model.Learn(v.hull, v.eye, farOrigin, direction), "a line further away than a gun can be is not learned");
    Check(!model.Learn(v.hull, v.eye, origin, alongUp), "a line along the vehicle's up is not learned");
    Check(model.fires() == 0, "none of them counted");

    Check(v.Fire(model, d), "a good fire");
    float near[3]{}, bad[3]{nan, 0.0f, 0.0f}, aim[3]{7.0f, 7.0f, 7.0f};
    PointAlong(v, 0.0, 0.0, 1.0, near);
    Check(!model.Aim(v.hull, v.eye, bad, aim) && !model.Aim(v.hull, bad, near, aim) && aim[0] == 7.0f,
          "a point or an eye that is not a number is declined and the aim left alone");
    Check(!model.Aim(v.hull, v.eye, v.eye, aim), "the eye itself is no point to aim at");

    model.Reset();
    float point[3]{};
    PointAlong(v, 0.0, 0.0, 30.0, point);
    Check(model.fires() == 0 && !model.Aim(v.hull, v.eye, point, aim), "a reset forgets the gun");
}

void TestBadFireDoesNotLock()
{
    // One wrong line (a fire while the barrel was elsewhere) is outvoted by the fires after it.
    const Vehicle v = Tank();
    Model model;
    double d[3]{};
    Heading(v.hull, 10.0, 0.0, d);
    float origin[3]{}, direction[3]{};
    v.Barrel(d, origin, direction);
    float wrong[3]{origin[0] + 0.6f, origin[1] - 0.4f, origin[2] + 0.3f};
    Check(model.Learn(v.hull, v.eye, wrong, direction), "a plausible but wrong line is learned");
    for (int round = 0; round < 6; ++round)
        for (double yaw : {-60.0, -20.0, 10.0, 50.0})
        {
            Heading(v.hull, yaw, 2.0, d);
            v.Fire(model, d);
        }
    float point[3]{};
    PointAlong(v, 30.0, 2.0, 30.0, point);
    const double miss = v.MissAt(model, point);
    Check(miss >= 0.0 && miss < 0.03, "and the fit recovers after the good fires that follow");
}

void TestSeed()
{
    // A fit handed on: the model answers before its first fire, as well as the model it was taken from.
    const Vehicle v = Tank();
    Model learned;
    for (double yaw : {-70.0, -30.0, 0.0, 20.0, 55.0, 90.0})
    {
        double d[3]{};
        Heading(v.hull, yaw, yaw * 0.1, d);
        v.Fire(learned, d);
    }
    double fit[5]{};
    learned.State(fit);
    Model seeded;
    Check(!seeded.known(), "a new model knows nothing");
    seeded.Seed(fit);
    Check(seeded.known() && seeded.fires() == 0, "a seeded model is known before any fire");
    float point[3]{};
    PointAlong(v, 35.0, 3.0, 12.0, point);
    const double miss = v.MissAt(seeded, point);
    Check(miss >= 0.0 && miss < 0.02, "and puts the barrel on a point at once");
    // A seed that is a little wrong (another seat camera) is corrected by the seat's own fires.
    double off[5]{fit[0] + 0.10, fit[1] - 0.08, fit[2] + 0.06, fit[3], fit[4]};
    Model wrong;
    wrong.Seed(off);
    const double before = v.MissAt(wrong, point);
    for (double yaw : {-40.0, 10.0, 60.0})
    {
        double d[3]{};
        Heading(v.hull, yaw, 2.0, d);
        v.Fire(wrong, d);
    }
    const double after = v.MissAt(wrong, point);
    Check(before > 0.05 && after < 0.02, "a seed a hand's width off is put right by three fires");
    wrong.Reset();
    Check(!wrong.known(), "and a reset forgets a seed too");
}

void TestNearPoint()
{
    // A point nearer than the model serves is aimed at as if it stood at the nearest range served, on the eye's own
    // line: the aim is the same for every point inside that range, and nothing jumps as a point crosses it.
    const Vehicle v = Tank();
    Model model;
    double d[3]{};
    Heading(v.hull, 0.0, 0.0, d);
    Check(v.Fire(model, d), "a fire");
    float veryNear[3]{}, near[3]{}, aimA[3]{}, aimB[3]{};
    PointAlong(v, 0.0, 0.0, 0.2, veryNear);   // the barrel sits about 0.4 wu off the eye across this heading
    PointAlong(v, 0.0, 0.0, 0.9, near);
    Check(model.Aim(v.hull, v.eye, veryNear, aimA) && model.Aim(v.hull, v.eye, near, aimB),
          "a point nearer than the minimum range is still served");
    Check(std::fabs(aimA[0] - aimB[0]) < 1e-5 && std::fabs(aimA[1] - aimB[1]) < 1e-5 && std::fabs(aimA[2] - aimB[2]) < 1e-5,
          "every point inside the nearest range served gets the same aim");
    double worstStep = 0.0, worstTurn = 0.0;
    float last[3]{aimA[0], aimA[1], aimA[2]};
    for (double range = 0.2; range < 4.0; range += 0.01)
    {
        float point[3]{}, aim[3]{};
        PointAlong(v, 0.0, 0.0, range, point);
        Check(model.Aim(v.hull, v.eye, point, aim), "every range is served");
        const double a[3]{aim[0], aim[1], aim[2]}, b[3]{last[0], last[1], last[2]};
        const double step = std::acos(std::fmin(1.0, Dot(a, b))) * 57.29577951308232;
        const double turn = std::acos(std::fmin(1.0, Dot(a, d))) * 57.29577951308232;
        if (step > worstStep)
            worstStep = step;
        if (turn > worstTurn)
            worstTurn = turn;
        last[0] = aim[0];
        last[1] = aim[1];
        last[2] = aim[2];
    }
    Check(worstStep < 0.2, "the aim moves by a fraction of a degree per hundredth of a world unit: no jump at any range");
    Check(worstTurn < 19.5, "and never leaves the eye's line by more than asin(1 / kMinRangeFactor)");
}

// A turret whose barrel settles a fixed step off the aim it is given (right and up, radians), and an engine that
// turns the shot at the point inside a cone about the barrel (radians): the title's seat fire.
struct Turret
{
    Vehicle v{};
    double errorRight = 0.0, errorUp = 0.0, cone = 0.0;

    // The fire for a written aim: the barrel's own line (muzzle and direction), then the shot the engine makes of it.
    void Fire(const float aim[3], const float point[3], float muzzle[3], float barrelDirection[3], float shot[3]) const
    {
        double d[3]{aim[0], aim[1], aim[2]}, right[3]{}, up[3]{};
        Normalize(d);
        AimAxes(v.hull, d, right, up);
        double barrel[3]{d[0] + errorRight * right[0] + errorUp * up[0], d[1] + errorRight * right[1] + errorUp * up[1],
                         d[2] + errorRight * right[2] + errorUp * up[2]};
        Normalize(barrel);
        v.Barrel(barrel, muzzle, barrelDirection);
        double wanted[3]{double(point[0]) - muzzle[0], double(point[1]) - muzzle[1], double(point[2]) - muzzle[2]};
        Normalize(wanted);
        const double angle = std::acos(std::fmin(1.0, Dot(barrel, wanted)));
        double out[3]{wanted[0], wanted[1], wanted[2]};
        if (angle > cone)
        {
            // Turned by exactly the cone, in the plane of the barrel and the point.
            double across[3]{wanted[0] - barrel[0] * Dot(barrel, wanted), wanted[1] - barrel[1] * Dot(barrel, wanted),
                             wanted[2] - barrel[2] * Dot(barrel, wanted)};
            Normalize(across);
            for (int i = 0; i < 3; ++i)
                out[i] = barrel[i] * std::cos(cone) + across[i] * std::sin(cone);
        }
        for (int i = 0; i < 3; ++i)
            shot[i] = static_cast<float>(out[i]);
    }
    // One shot at the point with whatever the model knows, the barrel's own line learned from it. Returns how far
    // the SHOT passed from the point; `turnedDeg` (optional): how far the engine turned it off the barrel.
    double Shoot(Model& model, const float point[3], double* turnedDeg = nullptr) const
    {
        float aim[3]{float(point[0] - v.eye[0]), float(point[1] - v.eye[1]), float(point[2] - v.eye[2])};
        model.Aim(v.hull, v.eye, point, aim);   // declined before the first fire: the eye's line
        float muzzle[3]{}, barrel[3]{}, shot[3]{};
        Fire(aim, point, muzzle, barrel, shot);
        model.Learn(v.hull, v.eye, muzzle, barrel);
        const double o[3]{muzzle[0], muzzle[1], muzzle[2]}, s[3]{shot[0], shot[1], shot[2]},
            b[3]{barrel[0], barrel[1], barrel[2]}, p[3]{point[0], point[1], point[2]};
        if (turnedDeg)
            *turnedDeg = std::acos(std::fmin(1.0, Dot(s, b))) * 57.29577951308232;
        return LineMiss(o, s, p);
    }
};

// A driver whose eye is raised well off the gun: the barrel's line runs 0.89 wu (2.7 m) below and to the right of
// the eye's, the muzzle 1.2 wu ahead of the eye.
Vehicle RaisedDriver()
{
    Vehicle v = Tank();
    v.pivot[0] = -0.50;
    v.pivot[1] = -0.52;
    v.pivot[2] = -0.72;
    v.beside = 0.0;
    v.above = 0.0;
    return v;
}

void TestEngineCone()
{
    const double kDeg = 0.017453292519943295;
    Turret t{};
    t.v = RaisedDriver();
    t.cone = 6.0 * kDeg;
    float nearPoint[3]{}, farPoint[3]{};
    PointAlong(t.v, 15.0, -3.0, 8.0, nearPoint);    // inside the range where the gap fits the cone
    PointAlong(t.v, 15.0, -3.0, 40.0, farPoint);    // far outside it
    {
        // The eye's line: the engine closes the gap far away and cannot near.
        Model none;
        double turned = 0.0;
        Check(t.Shoot(none, farPoint, &turned) < 0.01 && turned > 0.5 && turned < 6.0,
              "fixture: far away the engine's turn alone lands the eye-line shot on the point");
        Model none2;
        const double miss = t.Shoot(none2, nearPoint, &turned);
        Check(miss > 0.1 && std::fabs(turned - 6.0) < 0.01,
              "fixture: near, the eye-line shot is held at the cone's edge and lands beside the point");
    }
    {
        // After one fire the barrel is aimed through the point: the engine has next to nothing left to turn.
        Model model;
        t.Shoot(model, nearPoint);
        double turned = 0.0;
        const double second = t.Shoot(model, nearPoint, &turned);
        Check(second < 0.005 && turned < 1.5, "the second shot at a near point lands on it, the barrel already on the point");
        Check(t.Shoot(model, farPoint, &turned) < 0.01 && turned < 1.5, "and so does a far one");
        // Another heading and range, never fired on: inside the cone at once.
        float other[3]{};
        PointAlong(t.v, -40.0, 2.0, 6.0, other);
        Check(t.Shoot(model, other, &turned) < 0.01 && turned < 6.0,
              "a heading never fired on is inside the cone from the fires elsewhere, and the engine closes the rest");
    }
    {
        // A barrel that settles a degree off the aim it is given: the cone takes that up, at every range.
        Turret off = t;
        off.errorRight = 0.9 * kDeg;
        off.errorUp = -0.4 * kDeg;
        Model model;
        off.Shoot(model, nearPoint);
        double worst = 0.0;
        for (double range : {5.0, 8.0, 20.0, 60.0})
        {
            float point[3]{};
            PointAlong(off.v, 15.0, -3.0, range, point);
            const double miss = off.Shoot(model, point);
            if (miss > worst)
                worst = miss;
        }
        Check(worst < 0.01, "a barrel a degree off its aim still lands every shot on the point");
    }
    {
        // One fire at a far point already puts the barrel near a point 30 wu nearer on the same line, where the
        // aim has to turn several degrees from the fired heading: well inside the cone.
        Model model;
        float aim[3]{float(farPoint[0] - t.v.eye[0]), float(farPoint[1] - t.v.eye[1]), float(farPoint[2] - t.v.eye[2])};
        float muzzle[3]{}, barrel[3]{}, shot[3]{};
        t.Fire(aim, farPoint, muzzle, barrel, shot);
        Check(model.Learn(t.v.hull, t.v.eye, muzzle, barrel), "the far fire's barrel line is learned");
        Check(t.v.MissAt(model, nearPoint) < 0.1 * t.v.EyeLineMissAt(nearPoint),
              "and the barrel then passes a near point ten times closer than the eye's line leaves it");
    }
}
}   // namespace

int main()
{
    TestFrame();
    TestNoFire();
    TestOneFire();
    TestManyFires();
    TestChildTurret();
    TestRefusals();
    TestBadFireDoesNotLock();
    TestSeed();
    TestNearPoint();
    TestEngineCone();
    std::printf("seat_barrel_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
