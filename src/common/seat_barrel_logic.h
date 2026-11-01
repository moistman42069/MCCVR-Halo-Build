#pragma once

// AIM A SEAT'S GUN THROUGH THE POINT -- the pure half (Halo 2; halo2_direct_drive.inl has the seat's write).
//
// A vehicle gun's shot leaves its MUZZLE. The fire turns it at the point the crosshair test hit, but in a seat only
// inside the weapon's deviation angle about the barrel (seat_converge_logic.h; six degrees for a Halo 2 Scorpion's
// cannon). A seat's aim bent to run from the seat's CAMERA through the point leaves the barrel PARALLEL to that
// line, beside the point by however far the barrel sits from the camera (metres, for a tank's driver). Where that
// gap fits inside the angle the engine closes it and the shot lands on the point; nearer, the shot is held at the
// cone's edge and lands beside the point. So the aim written for the seat is the direction for which the BARREL's
// line runs through the point: the engine's turn is then next to nothing at any range, and what this model gets
// wrong the engine still closes, up to the weapon's angle.
//
// The barrel is not read from the engine between fires. Each local fire shows its line -- the muzzle and the
// direction the fire was given, BEFORE the engine turns the shot: the axis of the engine's cone -- and the gun's
// geometry is learned from those lines:
//
//     a point of the barrel's line for an aim d  =  eye + H c + r(d) o_r + u(d) o_u
//
// where H is the vehicle's frame, c a vector fixed in the vehicle (the turret's pivot from the seat's eye),
// (r(d), u(d)) the right and up of the aim (from d and the vehicle's up), and (o_r, o_u) how far the barrel sits
// beside and above its pivot. Each fire measures the line's offset from the eye across its own direction: two
// numbers, linear in the five unknowns. A recursive least-squares fit starts from "no offset", is exact for the
// direction of the first fire, and separates the pivot from the barrel's own offset as fires at other headings
// arrive. Nothing here knows the vehicle: a fixed gun, a turret on the hull and a child turret all fit, and so
// does a seat's eye that is set off from the occupant's head along the head's own axes.
//
// A fit can be handed on (State, Seed): a gun learned once aims its barrel from the first shot of the next seat in
// that kind of vehicle.
//
// The aim is then solved by a few fixed-point steps (the anchor turns with the aim). The model says nothing about
// whether the gun can REACH the aim: a turret still turning, or a gun at its stop, fires where it points, which is
// the seat's natural limit and is left to the engine.
//
// Only the aim's direction comes out of this, and the caller writes it into the record the title replicates.
//
// Zero engine dependencies; unit-tested in tests/seat_barrel_tests.cpp.

#include <cmath>
#include <cstdint>

namespace seat_barrel
{

// Lengths are the title's world units (Halo 2: one is about three metres).
// A barrel further than this from the seat's eye is not a gun of this seat's vehicle (a bad read).
inline constexpr double kMaxOffsetWu = 4.0;
// The point the barrel is aimed through is never taken nearer than this many times the barrel's offset from the
// eye, nor nearer than kMinRangeWu: a nearer point is served as if it stood that far out on the eye's own line.
// Nearer than that the two lines differ by tens of degrees and the model's error with them, and holding the range
// (and not handing the aim back to the eye's line) keeps the aim continuous while a point crosses it. The solved
// aim so never leaves the eye's line by more than asin(1 / kMinRangeFactor), 19.5 degrees.
inline constexpr double kMinRangeFactor = 3.0;
inline constexpr double kMinRangeWu = 0.5;
// The fit's priors (world units) and its measurement noise: the pivot may be metres from the eye, the barrel sits
// close to its pivot, and a fire's line is exact to millimetres.
inline constexpr double kPivotPriorWu = 1.5;
inline constexpr double kBarrelPriorWu = 0.15;
inline constexpr double kMeasureNoiseWu = 0.005;
// A fit known beforehand (Seed) is held this loosely: right to a hand's width or so (a seat camera set a little
// differently moves it), and corrected by the seat's first fires.
inline constexpr double kSeedPivotWu = 0.15;
inline constexpr double kSeedBarrelWu = 0.05;

struct Frame
{
    double f[3]{1, 0, 0}, l[3]{0, 1, 0}, u[3]{0, 0, 1};   // forward, left, up: orthonormal, world
};

inline double Dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void Cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline bool Normalize(double v[3])
{
    const double n = std::sqrt(Dot(v, v));
    if (!std::isfinite(n) || !(n > 1.0e-9))
        return false;
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
    return true;
}
inline bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// The vehicle's frame from its forward and up as the engine keeps them (unit, nearly orthogonal).
inline bool MakeFrame(const float forward[3], const float up[3], Frame& out)
{
    if (!Finite3(forward) || !Finite3(up))
        return false;
    double f[3]{forward[0], forward[1], forward[2]}, u[3]{up[0], up[1], up[2]}, l[3]{};
    if (!Normalize(f) || !Normalize(u))
        return false;
    Cross(u, f, l);
    if (!Normalize(l))
        return false;
    Cross(f, l, u);
    for (int i = 0; i < 3; ++i)
    {
        out.f[i] = f[i];
        out.l[i] = l[i];
        out.u[i] = u[i];
    }
    return true;
}

// The aim's right and up for a direction d (unit): right = d x up(vehicle), up = right x d. False when d runs along
// the vehicle's up (no right there).
inline bool AimAxes(const Frame& hull, const double d[3], double right[3], double up[3])
{
    Cross(d, hull.u, right);
    if (!(Dot(right, right) > 1.0e-6) || !Normalize(right))   // within a twentieth of a degree of the up
        return false;
    Cross(right, d, up);
    return Normalize(up);
}

class Model
{
public:
    Model() { Reset(); }

    void Reset()
    {
        for (int i = 0; i < 5; ++i)
        {
            x_[i] = 0.0;
            for (int j = 0; j < 5; ++j)
                p_[i][j] = 0.0;
        }
        const double pivot = kPivotPriorWu * kPivotPriorWu, barrel = kBarrelPriorWu * kBarrelPriorWu;
        p_[0][0] = p_[1][1] = p_[2][2] = pivot;
        p_[3][3] = p_[4][4] = barrel;
        fires_ = 0;
        seeded_ = false;
    }

    // Start from a fit known beforehand (State of a model that has learned this gun): the model then answers
    // before its first fire, and its own fires go on correcting it.
    void Seed(const double fit[5])
    {
        Reset();
        for (int i = 0; i < 5; ++i)
            x_[i] = fit[i];
        const double pivot = kSeedPivotWu * kSeedPivotWu, barrel = kSeedBarrelWu * kSeedBarrelWu;
        p_[0][0] = p_[1][1] = p_[2][2] = pivot;
        p_[3][3] = p_[4][4] = barrel;
        seeded_ = true;
    }

    uint32_t fires() const { return fires_; }
    // The model can answer: it has learned from a fire, or was given a fit.
    bool known() const { return fires_ != 0 || seeded_; }

    // The fit as it stands (world units): the pivot from the eye in the vehicle's frame (forward, left, up), then
    // the barrel beside (right) and above (up) its pivot.
    void State(double out[5]) const
    {
        for (int i = 0; i < 5; ++i)
            out[i] = x_[i];
    }

    // One fire: the seat's eye, the barrel's line as the fire took it, the vehicle's frame. False (nothing learned):
    // a value that is not a number, a direction that is not one, a line further from the eye than a gun can be.
    bool Learn(const Frame& hull, const float eye[3], const float origin[3], const float direction[3])
    {
        if (!Finite3(eye) || !Finite3(origin) || !Finite3(direction))
            return false;
        double d[3]{direction[0], direction[1], direction[2]};
        const double length = std::sqrt(Dot(d, d));
        if (!(length > 0.5) || !(length < 2.0) || !Normalize(d))
            return false;
        double right[3]{}, up[3]{};
        if (!AimAxes(hull, d, right, up))
            return false;
        const double a[3]{double(origin[0]) - eye[0], double(origin[1]) - eye[1], double(origin[2]) - eye[2]};
        const double along = Dot(a, d);
        const double w[3]{a[0] - along * d[0], a[1] - along * d[1], a[2] - along * d[2]};
        if (!(std::sqrt(Dot(w, w)) <= kMaxOffsetWu))
            return false;
        const double h1[5]{Dot(right, hull.f), Dot(right, hull.l), Dot(right, hull.u), 1.0, 0.0};
        const double h2[5]{Dot(up, hull.f), Dot(up, hull.l), Dot(up, hull.u), 0.0, 1.0};
        Update(h1, Dot(w, right));
        Update(h2, Dot(w, up));
        if (fires_ < UINT32_MAX)
            ++fires_;
        return true;
    }

    // A point of the barrel's line for an aim along d (unit), by the model. False: nothing known of the gun yet, or
    // d has no right.
    bool Anchor(const Frame& hull, const float eye[3], const double d[3], double anchor[3]) const
    {
        double right[3]{}, up[3]{};
        if (!known() || !AimAxes(hull, d, right, up))
            return false;
        for (int i = 0; i < 3; ++i)
            anchor[i] = double(eye[i]) + hull.f[i] * x_[0] + hull.l[i] * x_[1] + hull.u[i] * x_[2] +
                        right[i] * x_[3] + up[i] * x_[4];
        return true;
    }

    // The aim for which the barrel's line runs through `point` (or through the eye's line at the nearest range
    // served, for a nearer point). False (the caller keeps the eye's line, `direction` untouched): nothing known
    // of the gun yet, a value that is not a number, or a barrel the fit puts further from the eye than a gun can
    // be.
    bool Aim(const Frame& hull, const float eye[3], const float point[3], float direction[3]) const
    {
        if (!known() || !Finite3(eye) || !Finite3(point))
            return false;
        double fromEye[3]{double(point[0]) - eye[0], double(point[1]) - eye[1], double(point[2]) - eye[2]};
        const double range = std::sqrt(Dot(fromEye, fromEye));
        if (!Normalize(fromEye))
            return false;
        double d[3]{fromEye[0], fromEye[1], fromEye[2]};
        for (int step = 0; step < 4; ++step)
        {
            double anchor[3]{};
            if (!Anchor(hull, eye, d, anchor))
                return false;
            const double offset[3]{anchor[0] - eye[0], anchor[1] - eye[1], anchor[2] - eye[2]};
            const double offsetLength = std::sqrt(Dot(offset, offset));
            if (!(offsetLength <= kMaxOffsetWu))
                return false;
            const double reach = std::fmax(range, std::fmax(kMinRangeWu, kMinRangeFactor * offsetLength));
            double next[3]{eye[0] + fromEye[0] * reach - anchor[0], eye[1] + fromEye[1] * reach - anchor[1],
                           eye[2] + fromEye[2] * reach - anchor[2]};
            if (!Normalize(next))
                return false;
            d[0] = next[0];
            d[1] = next[1];
            d[2] = next[2];
        }
        const float out[3]{static_cast<float>(d[0]), static_cast<float>(d[1]), static_cast<float>(d[2])};
        if (!Finite3(out))
            return false;
        direction[0] = out[0];
        direction[1] = out[1];
        direction[2] = out[2];
        return true;
    }

private:
    // One scalar measurement z = h . x (+ noise): the recursive least-squares step.
    void Update(const double h[5], double z)
    {
        double ph[5]{};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                ph[i] += p_[i][j] * h[j];
        double s = kMeasureNoiseWu * kMeasureNoiseWu, predicted = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            s += h[i] * ph[i];
            predicted += h[i] * x_[i];
        }
        if (!std::isfinite(s) || !(s > 0.0) || !std::isfinite(z))
            return;
        const double innovation = z - predicted;
        for (int i = 0; i < 5; ++i)
            x_[i] += ph[i] / s * innovation;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                p_[i][j] -= ph[i] * ph[j] / s;
        // A floor under the variances: a later fire always moves the fit a little, so one bad line cannot lock it.
        for (int i = 0; i < 5; ++i)
            p_[i][i] += 1.0e-6;
    }

    double x_[5]{};       // pivot from the eye in the vehicle's frame (f, l, u), then the barrel's right and up
    double p_[5][5]{};
    uint32_t fires_ = 0;
    bool seeded_ = false;
};

// How far a line passes from a point (world units): the dev witness's measure, and the tests'.
inline double LineMiss(const double origin[3], const double direction[3], const double point[3])
{
    const double a[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const double along = Dot(a, direction);
    const double w[3]{a[0] - along * direction[0], a[1] - along * direction[1], a[2] - along * direction[2]};
    return std::sqrt(Dot(w, w));
}

}   // namespace seat_barrel
