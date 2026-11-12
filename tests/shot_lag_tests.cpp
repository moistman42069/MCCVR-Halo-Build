// Shot lag (src/common/shot_lag_logic.h): the intent published as a world point and recomposed against each
// shot's own origin, the fail-closed rules, the eye's track and extrapolation, the frame-coherent pair, and the
// dev witness's geometry.
#include "shot_lag_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

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
bool Near(float a, float b, float tolerance = 1.0e-4f) { return std::fabs(a - b) <= tolerance; }
constexpr float kWuPerM = shot_lag::kWuPerMetre;

// Distance (wu) from `point` to the line (origin, direction).
float LineMiss(const float origin[3], const float direction[3], const float point[3])
{
    const float l = shot_lag::Length(direction);
    const float d[3]{direction[0] / l, direction[1] / l, direction[2] / l};
    const float w[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const float t = shot_lag::Dot(w, d);
    const float m[3]{w[0] - d[0] * t, w[1] - d[1] * t, w[2] - d[2] * t};
    return shot_lag::Length(m);
}

// A sig::Find-style pattern ("48 8B ?? ...") against exactly `size` bytes: every token matches and none is left over.
bool PatternMatches(const char* pattern, const uint8_t* bytes, size_t size)
{
    size_t at = 0;
    for (const char* c = pattern; *c;)
    {
        if (*c == ' ')
        {
            ++c;
            continue;
        }
        if (at >= size || !c[1])
            return false;
        if (c[0] != '?')
        {
            char token[3]{c[0], c[1], 0};
            if (static_cast<uint8_t>(std::strtoul(token, nullptr, 16)) != bytes[at])
                return false;
        }
        ++at;
        c += 2;
    }
    return at == size;
}
} // namespace

int main()
{
    using namespace shot_lag;

    // ---- re-aim through the point ----
    {
        const float start[3]{0.0f, 0.0f, 0.0f}, vector[3]{40.0f, 0.0f, 0.0f}, point[3]{5.0f, 0.5f, -0.25f};
        float out[3]{};
        float bend = -1.0f;
        const Reaim r = ReaimThroughPoint(start, vector, point, ReaimRails{}, out, &bend);
        Check(r == Reaim::Applied, "a point ahead and near the line is applied");
        Check(Near(Length(out), 40.0f, 1.0e-3f), "the query keeps its length");
        Check(LineMiss(start, out, point) < 1.0e-5f, "the new query passes through the point");
        Check(bend > 0.0f && bend < 7.0f, "the bend is reported");
    }
    {
        // THE LAG ITSELF: angles solved from the render-time eye E0 toward P, fired from the tick's eye E1 = E0 + v dt.
        // The angles leave a parallel line off by v dt across the shot; re-aiming from E1 through P hits P exactly.
        const float e0[3]{0.0f, 0.0f, 0.0f}, p[3]{10.0f * kWuPerM, 0.0f, 0.0f};
        const float v[3]{0.0f, 4.0f * kWuPerM, 0.0f};   // a 4 m/s strafe, across the shot
        const float dt = 0.035f;
        const float e1[3]{e0[0] + v[0] * dt, e0[1] + v[1] * dt, e0[2] + v[2] * dt};
        const float solved[3]{p[0] - e0[0], p[1] - e0[1], p[2] - e0[2]};
        Check(Near(LineMiss(e1, solved, p) * kMetresPerWu * 1000.0f, 140.0f, 0.5f),
              "the stale angles miss by v*dt = 140 mm at a 4 m/s strafe over 35 ms");
        float out[3]{};
        Check(ReaimThroughPoint(e1, solved, p, ReaimRails{}, out) == Reaim::Applied &&
                  LineMiss(e1, out, p) * kMetresPerWu * 1000.0f < 0.01f,
              "the re-aim from the shot's own origin through P misses by nothing");
    }
    {
        const float start[3]{0, 0, 0}, vector[3]{10, 0, 0};
        float out[3]{7, 7, 7};
        const float behind[3]{-3.0f, 0.1f, 0.0f};
        Check(ReaimThroughPoint(start, vector, behind, ReaimRails{}, out) == Reaim::PointBehind,
              "a point behind the start is refused");
        Check(out[0] == 7 && out[1] == 7 && out[2] == 7, "a refusal leaves the output untouched");
        const float beside[3]{0.0f, 1.0f, 0.0f};
        Check(ReaimThroughPoint(start, vector, beside, ReaimRails{}, out) == Reaim::PointBehind,
              "a point exactly beside the start is not ahead");
        const float close[3]{0.05f, 0.0f, 0.0f};   // 15 cm
        Check(ReaimThroughPoint(start, vector, close, ReaimRails{}, out) == Reaim::PointTooClose,
              "a point within 30 cm defines no direction");
        // NO MAGNITUDE LIMIT: a big bend is applied however big, as long as P is ahead and defined.
        const float far[3]{5.0f, 6.6f, 0.0f};      // 20 m off the line 15 m ahead (52.9 deg)
        float farOut[3]{};
        Check(ReaimThroughPoint(start, vector, far, ReaimRails{}, farOut) == Reaim::Applied &&
                  LineMiss(start, farOut, far) < 1.0e-5f,
              "a point 20 m off the line 15 m ahead is applied, through P");
        const float steep[3]{0.4f, 1.0f, 0.0f};    // 3 m off 1.2 m ahead (68.2 deg)
        float steepOut[3]{};
        float bend = -1.0f;
        Check(ReaimThroughPoint(start, vector, steep, ReaimRails{}, steepOut, &bend) == Reaim::Applied &&
                  LineMiss(start, steepOut, steep) < 1.0e-5f && Near(bend, 68.2f, 0.05f),
              "a 68 deg bend up close is applied, through P");
        const float nearlyBeside[3]{0.01f, 1.0f, 0.0f};   // 89.4 deg, still ahead
        Check(ReaimThroughPoint(start, vector, nearlyBeside, ReaimRails{}, steepOut) == Reaim::Applied,
              "a point just ahead of beside the start is applied (89 deg)");
        const float onLine[3]{5.0f, 0.002f, 0.0f};   // 6 mm off the line
        Check(ReaimThroughPoint(start, vector, onLine, ReaimRails{}, out) == Reaim::AlreadyThrough,
              "a query already through P (within 10 mm) is left exactly as the game built it");
        const float justOff[3]{5.0f, 0.004f, 0.0f};  // 12 mm off the line
        float moved[3]{};
        Check(ReaimThroughPoint(start, vector, justOff, ReaimRails{}, moved) == Reaim::Applied,
              "12 mm off is corrected");
        const float zero[3]{0, 0, 0};
        const float p[3]{5, 0, 0};
        Check(ReaimThroughPoint(start, zero, p, ReaimRails{}, out) == Reaim::Degenerate, "a zero query is refused");
        const float nan3[3]{std::nanf(""), 0, 0};
        Check(ReaimThroughPoint(start, vector, nan3, ReaimRails{}, out) == Reaim::NonFinite &&
                  ReaimThroughPoint(nan3, vector, p, ReaimRails{}, out) == Reaim::NonFinite,
              "non-finite input is refused");
        Check(out[0] == 7 && out[1] == 7 && out[2] == 7, "no refusal wrote the output");
    }

    {
        // FAR POINTS: "already through" must hold at range. distance^2 - ahead^2 in float cancels to noise beyond
        // ~20 m (48 mm at 100 m, ~0.5 m at 1 km), so a query exactly through P would read as off it and be bent. Oblique
        // directions from a start at map-sized coordinates, a few of them so no lucky rounding hides the noise.
        const float start[3]{123.456f, -78.9f, 12.3f};
        const float dirs[4][3]{{0.6f, 0.7f, 0.3f}, {-0.31f, 0.88f, -0.12f}, {0.577f, -0.577f, 0.577f},
                               {0.99f, 0.05f, -0.13f}};
        const float ranges[3]{100.0f, 300.0f, 1000.0f};
        for (const float metres : ranges)
        {
            const float rangeWu = metres * kWuPerM;
            bool through = true, near = true, applied = true, hits = true;
            for (const auto& raw : dirs)
            {
                const float l = Length(raw);
                const float d[3]{raw[0] / l, raw[1] / l, raw[2] / l};
                // A unit vector across the line.
                float e[3]{d[1], -d[0], 0.0f};
                const float el = Length(e);
                e[0] /= el; e[1] /= el;
                const float vector[3]{d[0] * 40.0f, d[1] * 40.0f, d[2] * 40.0f};   // the query's own length
                const auto pointOff = [&](float offMm, float out[3]) {
                    const float off = offMm / kMmPerWu;
                    for (int axis = 0; axis < 3; ++axis)
                        out[axis] = start[axis] + d[axis] * rangeWu + e[axis] * off;
                };
                float p[3]{}, out[3]{};
                pointOff(0.0f, p);
                through = through && ReaimThroughPoint(start, vector, p, ReaimRails{}, out) == Reaim::AlreadyThrough;
                pointOff(5.0f, p);
                near = near && ReaimThroughPoint(start, vector, p, ReaimRails{}, out) == Reaim::AlreadyThrough;
                pointOff(15.0f, p);
                float bend = -1.0f;
                const bool ok = ReaimThroughPoint(start, vector, p, ReaimRails{}, out, &bend) == Reaim::Applied;
                applied = applied && ok;
                hits = hits && ok && LineMiss(start, out, p) * kMmPerWu < 2.0f &&
                       Near(bend, std::atan2(15.0f, metres * 1000.0f) * kRadToDeg, 2.0e-4f);
            }
            char what[160];
            std::snprintf(what, sizeof(what), "at %.0f m a query exactly through P is already through", double(metres));
            Check(through, what);
            std::snprintf(what, sizeof(what), "at %.0f m a query 5 mm off P is already through", double(metres));
            Check(near, what);
            std::snprintf(what, sizeof(what), "at %.0f m a query 15 mm off P is re-aimed", double(metres));
            Check(applied, what);
            std::snprintf(what, sizeof(what), "at %.0f m the re-aimed query passes P and reports its true bend",
                          double(metres));
            Check(hits, what);
        }
    }

    {
        // THE HORNET (Halo 3): the seat's crosshair query leaves the record by an ANGLE, ~4-5 deg at
        // a fast strafe. A fixed 3 m rail refuses it from ~40 m out, so the shots miss P by metres. A constant
        // angle must be corrected at every range, from an oblique start at map coordinates.
        const float start[3]{30.5f, 23.4f, -15.9f};
        const float raw[3]{0.34f, -0.91f, -0.29f};
        const float l = Length(raw);
        const float d[3]{raw[0] / l, raw[1] / l, raw[2] / l};
        float e[3]{d[1], -d[0], 0.0f};   // across the line
        const float el = Length(e);
        e[0] /= el; e[1] /= el;
        const float vector[3]{d[0] * 40.0f, d[1] * 40.0f, d[2] * 40.0f};
        const auto pointAt = [&](float metres, float angleDeg, float parallelM, float p[3]) {
            const float ahead = metres * kWuPerM;
            const float off = ahead * std::tan(angleDeg / kRadToDeg) + parallelM * kWuPerM;
            for (int axis = 0; axis < 3; ++axis)
                p[axis] = start[axis] + d[axis] * ahead + e[axis] * off;
        };
        for (const float metres : {20.0f, 50.0f, 100.0f, 200.0f})
        {
            float p[3]{}, out[3]{};
            float bend = -1.0f;
            pointAt(metres, 4.5f, 0.0f, p);
            const bool ok = ReaimThroughPoint(start, vector, p, ReaimRails{}, out, &bend) == Reaim::Applied;
            char what[160];
            std::snprintf(what, sizeof(what), "a constant 4.5 deg camera angle at %.0f m is re-aimed through P",
                          double(metres));
            Check(ok && LineMiss(start, out, p) * kMmPerWu < 2.0f && Near(bend, 4.5f, 1.0e-2f), what);
        }
        {
            // A measured case: 3757 mm off the game's line at 53.28 m (4.03 deg), which a 3 m rail refuses.
            float p[3]{}, out[3]{};
            pointAt(53.28f, std::atan2(3.757f, 53.28f) * kRadToDeg, 0.0f, p);
            Check(ReaimThroughPoint(start, vector, p, ReaimRails{}, out) == Reaim::Applied,
                  "the logged Hornet query, 3.76 m off at 53 m, is re-aimed");
        }
        // NO CLIFF: every strafe to 30 m/s (angle atan(v / 200 m/s), the Hornet's fit, plus v over the full 150 ms
        // a point may live and a 2.4 m camera start) at every range -- and big bends beyond any of that.
        bool all = true;
        for (float speed = 0.0f; speed <= 30.0f; speed += 2.5f)
            for (const float metres : {0.5f, 1.0f, 3.0f, 10.0f, 40.0f, 100.0f, 400.0f, 1000.0f})
            {
                float p[3]{}, out[3]{};
                pointAt(metres, std::atan2(speed, 200.0f) * kRadToDeg, speed * 0.150f + 2.4f, p);
                all = all && ReaimThroughPoint(start, vector, p, ReaimRails{}, out) == Reaim::Applied &&
                      LineMiss(start, out, p) * kMmPerWu < 5.0f;
            }
        Check(all, "every strafe to 30 m/s at every range from 0.5 m to 1 km is re-aimed: no speed or range cliff");
        for (const float metres : {50.0f, 300.0f, 1000.0f})
        {
            float p[3]{}, out[3]{};
            float bend = -1.0f;
            pointAt(metres, 12.0f, 0.0f, p);
            char what[160];
            std::snprintf(what, sizeof(what), "a 12 deg bend at %.0f m is re-aimed through P", double(metres));
            Check(ReaimThroughPoint(start, vector, p, ReaimRails{}, out, &bend) == Reaim::Applied &&
                      LineMiss(start, out, p) * kMmPerWu < 5.0f && Near(bend, 12.0f, 1.0e-2f), what);
        }
        {
            float p[3]{}, out[3]{};
            float bend = -1.0f;
            pointAt(1.0f, 0.0f, 2.5f, p);   // 68.2 deg: 2.5 m of travel beside a point 1 m ahead
            Check(ReaimThroughPoint(start, vector, p, ReaimRails{}, out, &bend) == Reaim::Applied &&
                      LineMiss(start, out, p) * kMmPerWu < 1.0f && bend > 60.0f,
                  "a 68 deg bend 1 m ahead is re-aimed through P");
        }
    }

    // ---- the published intent: when the shot site may use it ----
    {
        IntentPoint p{};
        p.generation = 3;
        p.stampUs = 1000000;
        p.rangeWu = 5.0f;
        p.point[0] = 5.0f;
        p.dir[0] = 1.0f;
        Check(ShouldUseIntent(true, p, 3, 1050000, 200000, true, false) == IntentUse::Use, "a fresh point is used");
        Check(ShouldUseIntent(false, p, 3, 1050000, 200000, true, false) == IntentUse::Missing,
              "nothing published");
        Check(ShouldUseIntent(true, p, 4, 1050000, 200000, true, false) == IntentUse::Missing,
              "another generation's point");
        Check(ShouldUseIntent(true, p, 3, 1300001, 200000, true, false) == IntentUse::Stale, "a stale point");
        Check(ShouldUseIntent(true, p, 3, 999999, 200000, true, false) == IntentUse::Stale, "a point from the future");
        Check(ShouldUseIntent(true, p, 3, 1050000, 200000, false, false) == IntentUse::NotConverged,
              "not converged");
        Check(ShouldUseIntent(true, p, 3, 1050000, 200000, true, true) == IntentUse::NaturalHeld,
              "a natural hold (seat stop, turret slew) leaves the query alone");
        IntentPoint held = p;
        held.railHeld = true;
        Check(ShouldUseIntent(true, held, 3, 1050000, 200000, true, false) == IntentUse::RailHeld,
              "a rail-held solve does not pass through P");
        IntentPoint bad = p;
        bad.point[1] = std::nanf("");
        Check(ShouldUseIntent(true, bad, 3, 1050000, 200000, true, false) == IntentUse::Bad, "a non-finite point");
        bad = p;
        bad.rangeWu = 0.0f;
        Check(ShouldUseIntent(true, bad, 3, 1050000, 200000, true, false) == IntentUse::Bad, "no range");
    }

    // ---- the crosshair query's gate ----
    {
        Check(GateCrosshairQuery(true, kFireLocal, true, false) == QueryGate::Try,
              "the local player's query at the site, in his fire, is tried");
        Check(GateCrosshairQuery(false, kFireLocal, true, false) == QueryGate::NotSite,
              "a query elsewhere is never ours (and not counted)");
        Check(GateCrosshairQuery(true, kFireLocal, false, false) == QueryGate::NotOurQuery,
              "another unit's query in our fire is never ours, counted");
        Check(GateCrosshairQuery(true, kFireLocal, true, true) == QueryGate::OwnShotLine,
              "an off-hand or barrel shot's own line is left alone (never the aim hand's point)");
        Check(GateCrosshairQuery(true, kFireLocal, false, true) == QueryGate::NotOurQuery,
              "another unit's query is not counted as an own-line skip");
        Check(std::strcmp(IntentUseName(IntentUse::OwnShotLine), "own shot line") == 0, "the own-line skip is named");
        // THE LOCAL PLAYER'S FIRE: a query that passes every check of its own is still left alone in another
        // player's fire (co-op, the next seat), or outside any fire we opened.
        Check(GateCrosshairQuery(true, kFireOther, true, false) == QueryGate::OtherFire,
              "a co-op player's fire is never re-aimed, whatever its query looks like");
        Check(GateCrosshairQuery(true, kFireNone, true, false) == QueryGate::OutsideFire,
              "a query outside any native fire is never re-aimed");
        Check(GateCrosshairQuery(true, kFireOther, true, true) == QueryGate::OtherFire,
              "another player's barrel shot is not counted as ours");
        // EVERY counted verdict has its own logged reason; Try and NotSite log none.
        Check(QueryGateSkip(QueryGate::OutsideFire) == IntentUse::OutsideFire &&
                  QueryGateSkip(QueryGate::OtherFire) == IntentUse::OtherFire &&
                  QueryGateSkip(QueryGate::NotOurQuery) == IntentUse::NotOurQuery &&
                  QueryGateSkip(QueryGate::OwnShotLine) == IntentUse::OwnShotLine &&
                  QueryGateSkip(QueryGate::Try) == IntentUse::Use && QueryGateSkip(QueryGate::NotSite) == IntentUse::Use,
              "each gate refusal maps to its own skip reason");
        bool named = true;
        for (size_t r = 0; r < static_cast<size_t>(IntentUse::Count); ++r)
            named = named && std::strcmp(IntentUseName(static_cast<IntentUse>(r)), "?") != 0;
        for (size_t r = 0; r < static_cast<size_t>(Reaim::Count); ++r)
            named = named && std::strcmp(ReaimName(static_cast<Reaim>(r)), "?") != 0;
        Check(named, "every skip and refusal reason has a name for the SHOT REAIM line");
    }
    {
        // The tag's lifetime: a fire opens Other; its shot line names the shooter; outside a fire nothing marks.
        Check(FireMark(kFireNone, true) == kFireNone, "a shot line outside a fire never tags (no fire was opened)");
        Check(FireMark(kFireOther, true) == kFireLocal, "the local unit's shot line tags the fire local");
        Check(FireMark(kFireOther, false) == kFireOther, "another unit's shot line leaves it another's");
        Check(FireMark(kFireLocal, false) == kFireOther, "a later projectile by another shooter is not ours");
        Check(FireMark(kFireLocal, true) == kFireLocal, "a second projectile of our own stays ours");
    }
    {
        // The query's own evidence, BY IDENTITY (on foot: the unit; seated: the vehicle's root or the seat's
        // parent). Where the query starts is not evidence: the Halo 3 Banshee's starts 2.3-2.4 m from our eye.
        Check(QueryIsLocal(false, 42, 42, -1, -1), "on foot: our unit is ours");
        Check(!QueryIsLocal(false, 43, 42, -1, -1), "on foot: another unit is not");
        Check(!QueryIsLocal(false, -1, -1, -1, -1), "no unit on either side is nobody's");
        Check(QueryIsLocal(true, 900, -1, 800, 900), "seated: the vehicle's root");
        Check(QueryIsLocal(true, 800, -1, 800, 900), "seated: the seat's parent");
        Check(QueryIsLocal(true, 800, -1, 800, -1), "seated, one level: the parent");
        Check(!QueryIsLocal(true, 700, -1, 800, 900), "seated: another vehicle is not");
        Check(!QueryIsLocal(true, -1, -1, 800, 900), "seated: no ignore is nobody's");
        Check(!QueryIsLocal(true, 900, -1, -1, -1), "seated without a published seat chain: nobody's");
        // Another occupant names the same vehicle: his query passes this, his FIRE never tags local.
        Check(QueryIsLocal(true, 900, -1, 800, 900) &&
                  GateCrosshairQuery(true, kFireOther, true, false) == QueryGate::OtherFire,
              "an AI gunner or co-op player in our vehicle: his query is the vehicle's, his fire is not ours");
    }

    // ---- the eye's track ----
    {
        EyeTrack track;
        const float v[3]{2.0f * kWuPerM, -1.0f * kWuPerM, 0.0f};
        for (int i = 0; i <= 10; ++i)
        {
            const float t = float(i) * 0.011f;
            const float e[3]{1.0f + v[0] * t, 2.0f + v[1] * t, 3.0f};
            track.Push(1000000 + uint64_t(i) * 11000, e);
        }
        float out[3]{};
        Check(track.Velocity(100000, 30000, out) && Near(out[0], v[0], 1.0e-3f) && Near(out[1], v[1], 1.0e-3f) &&
                  Near(out[2], 0.0f, 1.0e-3f),
              "a constant pace reads back");
        float none[3]{};
        EyeTrack empty;
        Check(!empty.Velocity(100000, 30000, none), "no track, no velocity");
        EyeTrack two;
        const float a[3]{0, 0, 0}, b[3]{0.001f, 0, 0};
        two.Push(1000, a);
        two.Push(11000, b);
        Check(!two.Velocity(100000, 30000, none), "a 10 ms span is too short to trust");
    }
    {
        // A player who stops: the eye repeats; repeats inside 40 ms are not news, after it they read as standing.
        EyeTrack track;
        const float moving[3]{0.0f, 0.0f, 0.0f}, moved[3]{0.1f, 0.0f, 0.0f};
        track.Push(1000000, moving);
        track.Push(1050000, moved);
        track.Push(1060000, moved);   // a repeat 10 ms later: ignored
        Check(track.count == 2, "a repeat of the last render's eye inside 40 ms is no sample");
        track.Push(1100000, moved);   // 50 ms of standing still
        track.Push(1150000, moved);
        track.Push(1200000, moved);
        float out[3]{};
        Check(track.Velocity(100000, 30000, out) && Near(out[0], 0.0f, 1.0e-6f),
              "after 100 ms standing still the eye reads 0, not the last pace");
    }
    {
        EyeTrack track;
        const float a[3]{0, 0, 0}, b[3]{0.01f, 0, 0}, c[3]{500.0f, 0, 0}, d[3]{500.01f, 0, 0};
        track.Push(1000000, a);
        track.Push(1011000, b);
        track.Push(1022000, c);   // 500 wu in 11 ms: a teleport
        Check(track.count == 1, "a jump faster than any vehicle starts afresh");
        track.Push(1400000, d);   // a 378 ms gap
        Check(track.count == 1, "a gap starts afresh");
        track.Push(1300000, a);   // the clock ran backwards
        Check(track.count == 1, "a clock running backwards starts afresh");
        const float nan3[3]{std::nanf(""), 0, 0};
        track.Push(1310000, nan3);
        Check(track.count == 1, "a non-finite eye is never a sample");
    }

    // ---- the game-eye proof ----
    {
        using S = EyeProof::State;
        const auto feed = [](EyeProof& p, int n, uint32_t gen, int32_t unit, bool eligible, bool rest, float mm) {
            for (int i = 0; i < n; ++i)
                p.Note(gen, unit, eligible, rest, mm);
        };
        EyeProof p;
        feed(p, int(EyeProof::kAgreeToProve) - 1, 1, 7, true, true, 10.0f);
        Check(p.state == S::Unproven, "29 agreeing samples do not prove it");
        p.Note(1, 7, true, true, 10.0f);
        Check(p.state == S::Proven, "30 agreeing at-rest samples on foot prove it");

        // THE FINDING: seated / third person / turret / death camera samples are no evidence at all.
        EyeProof seated;
        feed(seated, 200, 1, 7, false, true, 2000.0f);
        Check(seated.state == S::Unproven && seated.disagree == 0, "ineligible samples never disprove");
        feed(p, 200, 1, 7, false, true, 2000.0f);
        Check(p.state == S::Proven, "nor do they unprove a proven eye");

        // A single at-rest outlier (a respawn frame, a teleport) does not latch a disproof.
        EyeProof q;
        q.Note(1, 7, true, true, 900.0f);
        Check(q.state == S::Unproven, "one disagreeing sample proves nothing");
        feed(q, int(EyeProof::kDisagreeToDisprove) - 2, 1, 7, true, true, 900.0f);
        q.Note(1, 7, true, true, 100.0f);   // a sample between the bands breaks the streak
        feed(q, int(EyeProof::kDisagreeToDisprove) - 1, 1, 7, true, true, 900.0f);
        Check(q.state == S::Unproven, "a broken streak does not disprove");
        q.Note(1, 7, true, false, 900.0f);  // moving: not judged, the streak stands
        q.Note(1, 7, true, true, 900.0f);
        Check(q.state == S::Disproven, "15 consecutive at-rest disagreeing samples disprove it");
        feed(q, 40, 1, 7, true, true, 10.0f);
        Check(q.state == S::Disproven, "a disproof holds for the stint");

        // ...and only for the stint: a seat in between, or a new unit, re-opens it.
        q.Note(1, 7, false, true, 0.0f);
        q.Note(1, 7, true, true, 10.0f);
        Check(q.state == S::Unproven, "the next on-foot stint re-opens the proof");
        feed(q, int(EyeProof::kAgreeToProve), 1, 7, true, true, 10.0f);
        Check(q.state == S::Proven, "and it can be proven again");
        feed(q, int(EyeProof::kDisagreeToDisprove), 1, 7, true, true, 900.0f);
        Check(q.state == S::Disproven, "a proven eye can still be disproven by a streak");
        q.Note(1, 8, true, true, 10.0f);
        Check(q.state == S::Unproven && q.agree == 1, "a new unit (respawn) re-opens the proof");
        q.Note(2, 8, true, true, 10.0f);
        Check(q.state == S::Unproven && q.agree == 1 && q.generation == 2, "a new generation starts over");
        EyeProof nanProof;
        feed(nanProof, 40, 1, 7, true, true, std::nanf(""));
        Check(nanProof.state == S::Unproven, "a non-finite offset is no evidence");
    }

    // ---- extrapolation, angles, identity ----
    {
        const float e[3]{1, 2, 3}, v[3]{10, 0, -5};
        float out[3]{};
        Check(ExtrapolateEye(e, v, 0.02f, 0.1f, out) && Near(out[0], 1.2f) && Near(out[2], 2.9f), "E + v dt");
        Check(ExtrapolateEye(e, v, 5.0f, 0.1f, out) && Near(out[0], 2.0f), "dt clamps at its maximum");
        Check(ExtrapolateEye(e, v, -1.0f, 0.1f, out) && Near(out[0], 1.0f), "a negative dt is 0");
        const float nan3[3]{std::nanf(""), 0, 0};
        Check(!ExtrapolateEye(e, nan3, 0.02f, 0.1f, out), "no velocity, no extrapolation");
    }
    {
        const float o[3]{0, 0, 0}, p[3]{1, 1, std::sqrt(2.0f)};
        float yaw = 9.0f, pitch = 9.0f;
        Check(AnglesTowardPoint(o, p, 0.01f, 1.45f, yaw, pitch) && Near(yaw, 0.785398f) && Near(pitch, 0.785398f),
              "the angles of P - O in the game basis");
        const float up[3]{0.0f, 0.0f, 5.0f};
        Check(AnglesTowardPoint(o, up, 0.01f, 1.45f, yaw, pitch) && Near(pitch, 1.45f), "pitch clamps at the rail");
        const float back[3]{-1.0f, -0.0f, 0.0f};
        Check(AnglesTowardPoint(o, back, 0.01f, 1.45f, yaw, pitch) && Near(std::fabs(yaw), 3.141593f),
              "yaw is atan2's");
        const float near0[3]{0.001f, 0, 0};
        yaw = 9.0f;
        Check(!AnglesTowardPoint(o, near0, 0.01f, 1.45f, yaw, pitch) && yaw == 9.0f, "too close: refused, untouched");
        Check(SameAngles(1.0f, 0.1f, 1.0f + 6.2831853f, 0.1f, 1.0e-4f), "identity is wrap-insensitive (a full turn)");
        Check(SameAngles(3.14155f, 0.0f, -3.14155f, 0.0f, 1.0e-3f), "and across the +-pi seam");
        Check(!SameAngles(0.0f, 0.0f, 0.001f, 0.0f, 1.0e-4f), "a different solve is not the same");
        Check(!SameAngles(std::nanf(""), 0.0f, 0.0f, 0.0f), "non-finite angles never match");
    }

    // ---- the record recomposed through the point (Halo 3 on foot) ----
    {
        // The solve from the render-time eye E0 toward P 10 m out; the game's eye at the tick is E1, 140 mm along a
        // 4 m/s strafe, moving on at that pace for one 33 ms tick more.
        const float p[3]{10.0f * kWuPerM, 0.0f, 0.0f};
        const float v[3]{0.0f, 4.0f * kWuPerM, 0.0f};
        RecomposeInput base{};
        base.onFoot = true;
        base.intentRead = true;
        base.intent.generation = 5;
        base.intent.stampUs = 1000000;
        base.intent.point[0] = p[0];
        base.intent.rangeWu = p[0];
        base.intent.dir[0] = 1.0f;
        base.intent.solvedYaw = 0.0f;
        base.intent.solvedPitch = 0.0f;
        base.setYaw = 0.0f;
        base.setPitch = 0.0f;
        base.generation = 5;
        base.nowUs = 1040000;
        base.maxAgeUs = 150000;
        base.firstPersonEye = true;
        base.eyeRead = true;
        base.eye[1] = 0.14f * kWuPerM;
        base.eyeVel[1] = v[1];
        base.eyeVelValid = true;
        base.eyeProven = true;
        base.tickS = 0.033f;
        float yaw = 9.0f, pitch = 9.0f, bend = -1.0f;
        Check(RecomposeRecord(base, yaw, pitch, bend) == Recompose::Recomposed, "a fresh point and a proven eye");
        // The line from the eye one tick on passes through P.
        const float fire[3]{0.0f, (0.14f + 4.0f * 0.033f) * kWuPerM, 0.0f};
        const float dir[3]{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        Check(LineMiss(fire, dir, p) * kMmPerWu < 0.5f, "the record passes P from the game's eye one tick on");
        Check(bend > 1.0f && bend < 2.0f, "and reports the bend from the solve (~1.6 deg at 10 m)");

        const auto refused = [&](RecomposeInput in, Recompose expected, const char* what) {
            float y = 7.0f, pt = 7.0f, b = 7.0f;
            Check(RecomposeRecord(in, y, pt, b) == expected && y == 7.0f && pt == 7.0f && b == 7.0f, what);
        };
        RecomposeInput in = base;
        in.onFoot = false;
        refused(in, Recompose::NotOnFoot, "not on foot: kept, untouched");
        in = base;
        in.intentRead = false;
        refused(in, Recompose::NoIntent, "no intent: kept");
        in = base;
        in.setYaw = 0.01f;   // the fixed-distance steer, a seat author: not the solve that built P
        refused(in, Recompose::OtherSolve, "another path's setpoint is never replaced");
        in = base;
        in.setYaw = 6.2831853f;
        Check(RecomposeRecord(in, yaw, pitch, bend) == Recompose::Recomposed,
              "the same solve a full turn round is the same solve");
        in = base;
        in.intent.generation = 4;
        refused(in, Recompose::PointUnusable, "another generation's point");
        in = base;
        in.nowUs = base.intent.stampUs + 150001;
        refused(in, Recompose::PointUnusable, "a stale point");
        in = base;
        in.nowUs = base.intent.stampUs - 1;
        refused(in, Recompose::PointUnusable, "a point from the future");
        in = base;
        in.intent.seated = true;
        refused(in, Recompose::PointUnusable, "a seated point");
        in = base;
        in.intent.railHeld = true;
        refused(in, Recompose::PointUnusable, "a rail-held solve");
        in = base;
        in.generation = 0;
        refused(in, Recompose::PointUnusable, "no generation");
        in = base;
        in.intent.point[2] = std::nanf("");
        refused(in, Recompose::PointUnusable, "a non-finite point");
        in = base;
        in.firstPersonEye = false;
        refused(in, Recompose::NotFirstPerson, "not our unit's first-person eye (the Monitor, an orbit camera): kept");
        in.intent.generation = 4;
        refused(in, Recompose::PointUnusable, "an unusable point is named before the eye's identity");
        in = base;
        in.firstPersonEye = false;
        in.eyeRead = false;
        in.eyeProven = false;
        refused(in, Recompose::NotFirstPerson, "the identity is named before the eye's freshness and proof");
        in = base;
        in.firstPersonEye = false;
        in.eyeVelValid = false;
        in.eye[1] = 8.0f * kWuPerM;   // a camera 8 m beside a point 10 m out: the 38.7 deg a wrong eye would bend
        refused(in, Recompose::NotFirstPerson, "a big bend from a non-first-person eye is never applied");
        in = base;
        in.eyeRead = false;
        refused(in, Recompose::NoGameEye, "no game eye at the tick");
        in = base;
        in.eyeProven = false;
        refused(in, Recompose::EyeUnproven, "an unproven eye");
        in = base;
        in.eyeVelValid = false;
        in.eye[0] = p[0] - 0.05f;   // 15 cm short of P, on the line
        in.eye[1] = 0.0f;
        refused(in, Recompose::PointTooClose, "P within 30 cm of the eye defines no direction");
        in = base;
        in.eyeVelValid = false;
        in.eye[0] = p[0] + 1.0f * kWuPerM;   // the eye a metre past P
        refused(in, Recompose::PointBehind, "P behind the eye along the setpoint: no forward line");
        // NO MAGNITUDE LIMIT: big legitimate bends are recomposed, through P.
        const auto through = [&](const RecomposeInput& rin, float minBend, const char* what) {
            float y = 0.0f, pt = 0.0f, b = 0.0f;
            const bool ok = RecomposeRecord(rin, y, pt, b) == Recompose::Recomposed;
            const float d[3]{std::cos(pt) * std::cos(y), std::cos(pt) * std::sin(y), std::sin(pt)};
            Check(ok && LineMiss(rin.eye, d, p) * kMmPerWu < 0.5f && b > minBend, what);
        };
        in = base;
        in.eyeVelValid = false;
        in.eye[1] = 8.0f * kWuPerM;   // 8 m beside a point 10 m out: 38.7 deg
        through(in, 38.0f, "8 m beside a point 10 m out (38.7 deg) is recomposed through P");
        in = base;
        in.eyeVelValid = false;
        in.eye[0] = p[0] - 1.0f * kWuPerM;   // close in: 2 m beside a point 1 m ahead (63.4 deg)
        in.eye[1] = 2.0f * kWuPerM;
        through(in, 60.0f, "a 63 deg bend up close is recomposed through P");
        in = base;
        in.eyeVelValid = false;
        Check(RecomposeRecord(in, yaw, pitch, bend) == Recompose::Recomposed &&
                  std::fabs(std::atan2(-0.14f, 10.0f) - yaw) < 1.0e-4f,
              "no velocity: from the eye itself, not carried");
        in = base;
        in.tickS = 5.0f;   // a stalled tick measure: the carry is clamped to 50 ms
        Check(RecomposeRecord(in, yaw, pitch, bend) == Recompose::Recomposed &&
                  std::fabs(std::atan2(-(0.14f + 4.0f * 0.05f), 10.0f) - yaw) < 1.0e-4f,
              "the carry is clamped to 50 ms");
    }

    // ---- P transported with the eye (4a, dev key shot_point_transport) ----
    {
        // P 10 m down +X from the camera sample's eye E_P (the origin); a 4 m/s strafe along +Y; the fire 15.6 ms
        // after that sample, the game's eye there 60 mm along the strafe.
        const float p[3]{10.0f * kWuPerM, 0.0f, 0.0f};
        const float v[3]{0.0f, 4.0f * kWuPerM, 0.0f};
        TransportInput base{};
        std::memcpy(base.point, p, sizeof(p));
        base.pointEyeValid = true;
        base.pointEyeStampUs = 1000000;
        base.velValid = true;
        std::memcpy(base.vel, v, sizeof(v));
        base.nowUs = 1015600;
        base.maxAgeUs = 250000;
        float out[3]{}, shift[3]{};

        TransportInput in = base;
        in.fireEyeValid = true;
        in.fireEye[1] = 0.060f * kWuPerM;
        Check(TransportPoint(in, out, shift) == Transport::Measured, "a fire-tick eye: measured");
        Check(Near(out[1] * kMmPerWu, 60.0f, 0.01f) && Near(shift[1] * kMmPerWu, 60.0f, 0.01f) && out[0] == p[0],
              "P' = P + (E_fire - E_P), measured over the velocity's estimate (62.4 mm)");
        {
            // From E_fire the line through P' is the solved line from E_P moved, parallel: what flat Halo fires.
            const float a[3]{out[0] - in.fireEye[0], out[1] - in.fireEye[1], out[2] - in.fireEye[2]};
            const float b[3]{p[0] - in.pointEye[0], p[1] - in.pointEye[1], p[2] - in.pointEye[2]};
            const float la = Length(a), lb = Length(b);
            Check(Near(a[0] / la, b[0] / lb, 1.0e-6f) && Near(a[1] / la, b[1] / lb, 1.0e-6f),
                  "the line from the fire eye through P' is parallel to the solved line");
        }
        in = base;
        Check(TransportPoint(in, out, shift) == Transport::Extrapolated &&
                  Near(shift[1] * kMmPerWu, 62.4f, 0.01f) && Near(out[1] * kMmPerWu, 62.4f, 0.01f),
              "no fire eye: E_P + v * age (4 m/s x 15.6 ms = 62.4 mm)");
        in = base;
        in.fireEyeValid = true;
        in.fireEye[0] = std::nanf("");
        Check(TransportPoint(in, out, shift) == Transport::Extrapolated, "a non-finite fire eye is not measured");

        const auto refused = [&](const TransportInput& rin, Transport expected, const char* what) {
            float o[3]{7.0f, 7.0f, 7.0f}, s[3]{7.0f, 7.0f, 7.0f};
            Check(TransportPoint(rin, o, s) == expected && o[0] == 7.0f && s[0] == 7.0f && !TransportApplied(expected),
                  what);
        };
        in = base;
        in.pointEyeValid = false;
        refused(in, Transport::NoPointEye, "P's camera sample unknown: P as published, untouched");
        in = base;
        in.pointEyeStampUs = 0;
        in.fireEyeValid = true;
        refused(in, Transport::NoPointEye, "a camera sample with no stamp is unknown even with a fire eye");
        in = base;
        in.pointEye[2] = std::nanf("");
        refused(in, Transport::NoPointEye, "a non-finite camera sample");
        in = base;
        in.velValid = false;
        refused(in, Transport::NoFireEye, "no fire eye and no velocity");
        in = base;
        in.nowUs = base.pointEyeStampUs - 1;
        refused(in, Transport::StaleEye, "extrapolating from a camera sample in the future");
        in = base;
        in.nowUs = base.pointEyeStampUs + 250001;
        refused(in, Transport::StaleEye, "extrapolating from a camera sample older than maxAgeUs");
        in.fireEyeValid = true;
        Check(TransportPoint(in, out, shift) == Transport::Measured,
              "a measured eye needs no extrapolation, so the sample's age does not refuse it");
        in = base;
        in.point[1] = std::nanf("");
        refused(in, Transport::Bad, "a non-finite P");
        Check(TransportPoint(base, nullptr, shift) == Transport::Bad, "no output: bad");
        in = base;
        in.fireEyeValid = true;
        in.fireEye[1] = 5.0f * kWuPerM;   // NO MAGNITUDE LIMIT: a 5 m move is transported as measured
        Check(TransportPoint(in, out, shift) == Transport::Measured && Near(shift[1], 5.0f * kWuPerM, 1.0e-5f),
              "no size limit: a 5 m eye move is carried whole");

        // The fire-tick eye's gate: identity, then this tick.
        Check(FireEyeGate(true, true, 42, 42, 1000000, 1001000, 33333) == FireEye::Measured, "our proven eye, 1 ms old");
        Check(FireEyeGate(false, true, 42, 42, 1000000, 1001000, 33333) == FireEye::NotRead, "nothing read");
        Check(FireEyeGate(true, false, 42, 42, 1000000, 1001000, 33333) == FireEye::Unproven, "unproven");
        Check(FireEyeGate(true, true, 41, 42, 1000000, 1001000, 33333) == FireEye::OtherUnit, "another unit's eye");
        Check(FireEyeGate(true, true, -1, -1, 1000000, 1001000, 33333) == FireEye::OtherUnit, "no unit is nobody's");
        Check(FireEyeGate(true, true, 42, 42, 1000000, 1000000 + 24998, 33333) == FireEye::Measured &&
                  FireEyeGate(true, true, 42, 42, 1000000, 1000000 + 24999, 33333) == FireEye::NotThisTick,
              "three quarters of the measured tick (33.3 ms -> 25 ms) is this tick");
        Check(FireEyeGate(true, true, 42, 42, 1000000, 1000000 + 13000, 16667) == FireEye::NotThisTick &&
                  FireEyeGate(true, true, 42, 42, 1000000, 1000000 + 12000, 16667) == FireEye::Measured,
              "at 60 Hz (16.7 ms -> 12.5 ms) a 13 ms old eye is the previous tick's");
        Check(FireEyeGate(true, true, 42, 42, 1000000, 1000000 + 24999, 0) == FireEye::Measured &&
                  FireEyeGate(true, true, 42, 42, 1000000, 1000000 + 25000, 0) == FireEye::NotThisTick,
              "an unmeasured tick: 25 ms");
        Check(FireEyeGate(true, true, 42, 42, 1000000, 999999, 33333) == FireEye::NotThisTick &&
                  FireEyeGate(true, true, 42, 42, 0, 1000, 33333) == FireEye::NotThisTick,
              "an eye from the future or unstamped");

        // FLAT PARITY: the acquisition's eye is the eye BEFORE the tick's move -- the eye the last render
        // showed, E_P -- so used as E_fire it moves P by nothing (the muzzle transport reads 0 mm on every moving shot).
        // The usable fire eyes, and the acquisition's eye carried one tick.
        Check(FireEyeUsable(FireEye::Measured) && FireEyeUsable(FireEye::CarriedTick) &&
                  !FireEyeUsable(FireEye::NotThisTick) && !FireEyeUsable(FireEye::NoSource) &&
                  !FireEyeUsable(FireEye::Seated) && !FireEyeUsable(FireEye::Count),
              "flat parity: measured and carried-a-tick are usable fire eyes, nothing else");
        {
            // Strafing +Y at 6 m/s; the render showed E_P, the acquisition at the next tick answers the same eye.
            const float acquisition[3]{1.0f, 2.0f, 0.5f};
            const float strafe[3]{0.0f, 6.0f * kWuPerM, 0.0f};
            float carried[3]{};
            Check(CarryTickEye(acquisition, strafe, 0.016667f, carried) &&
                      Near((carried[1] - acquisition[1]) * kMmPerWu, 100.0f, 0.1f) && carried[0] == acquisition[0],
                  "flat parity: the acquisition's eye carried one 60 Hz tick at 6 m/s moves 100 mm along the strafe");
            float rest[3]{9.0f, 9.0f, 9.0f};
            Check(!CarryTickEye(acquisition, nullptr, 0.016667f, rest) && rest[0] == acquisition[0] &&
                      rest[1] == acquisition[1] && rest[2] == acquisition[2],
                  "flat parity: no velocity -> the eye itself (at rest that is the carried eye)");
            float clamped[3]{};
            Check(CarryTickEye(acquisition, strafe, 0.5f, clamped) &&
                      Near((clamped[1] - acquisition[1]) * kMmPerWu, 300.0f, 0.1f),
                  "flat parity: the carry is clamped to 50 ms");
            // P' through the carried eye: 100 mm along the strafe, and the line from the carried eye through P' is
            // parallel to the solved line from E_P through P -- the line flat Halo fires.
            TransportInput t{};
            t.point[0] = 1.0f + 10.0f * kWuPerM;
            t.point[1] = 2.0f;
            t.point[2] = 0.5f;
            t.pointEyeValid = true;
            std::memcpy(t.pointEye, acquisition, sizeof(t.pointEye));
            t.pointEyeStampUs = 1000000;
            t.fireEyeValid = true;
            std::memcpy(t.fireEye, carried, sizeof(t.fireEye));
            t.nowUs = 1015700;
            float pt[3]{}, sh[3]{};
            Check(TransportPoint(t, pt, sh) == Transport::Measured && Near(sh[1] * kMmPerWu, 100.0f, 0.1f),
                  "flat parity: P moves one tick of strafe ahead");
            const float lineNew[3]{pt[0] - carried[0], pt[1] - carried[1], pt[2] - carried[2]};
            const float lineOld[3]{t.point[0] - acquisition[0], t.point[1] - acquisition[1], t.point[2] - acquisition[2]};
            Check(Near(lineNew[1] / Length(lineNew), lineOld[1] / Length(lineOld), 1.0e-6f) &&
                      Near(lineNew[0] / Length(lineNew), lineOld[0] / Length(lineOld), 1.0e-6f),
                  "flat parity: from the fire eye the line through P' is the solved line, moved with the body");
            // The bug this replaces: the uncarried acquisition eye as E_fire moves P by nothing.
            std::memcpy(t.fireEye, acquisition, sizeof(t.fireEye));
            Check(TransportPoint(t, pt, sh) == Transport::Measured && Near(Length(sh), 0.0f, 1.0e-9f),
                  "flat parity: the uncarried acquisition eye moves P by 0 mm (the muzzle bug)");
        }

        {
            bool named = true;
            for (int i = 0; i < static_cast<int>(Transport::Count); ++i)
                named = named && std::strcmp(TransportName(static_cast<Transport>(i)), "?") != 0;
            for (int i = 0; i < static_cast<int>(FireEye::Count); ++i)
                named = named && std::strcmp(FireEyeName(static_cast<FireEye>(i)), "?") != 0;
            Check(named && std::strcmp(FireEyeName(FireEye::Count), "?") == 0, "every verdict has a name for the log");
        }

        // The report window.
        TransportWindow w;
        const float dir[3]{1.0f, 0.0f, 0.0f};
        const float ahead[3]{0.0f, 0.060f * kWuPerM, 0.0f};
        w.Add(Transport::Measured, FireEye::Measured, ahead, dir, v, 1.5f, 15.6f);
        const float ahead2[3]{0.0f, 0.080f * kWuPerM, 0.0f};
        w.Add(Transport::Extrapolated, FireEye::NotThisTick, ahead2, dir, v, -1.0f, 20.0f);
        const float still[3]{0.0f, 0.0f, 0.0f};
        const float tiny[3]{0.002f * kWuPerM, 0.0f, 0.0f};
        w.Add(Transport::Measured, FireEye::Measured, tiny, dir, still, 0.5f, 10.0f);
        w.Add(Transport::NoPointEye, FireEye::Count, still, dir, nullptr, -1.0f, -1.0f);
        w.Add(Transport::NoFireEye, FireEye::Seated, still, dir, nullptr, -1.0f, -1.0f);
        Check(w.Total() == 5 && w.applied == 3 && w.travel == 2 && w.rest == 1, "window: counts");
        Check(Near(w.alongSum / 2.0f, 70.0f, 0.01f) && Near(w.alongMax, 80.0f, 0.01f),
              "window: the shift along travel (+ = ahead)");
        char line[768]{};
        const size_t used = w.Format("SHOT LAG Halo 3 transport [muzzle] (dev)", line, sizeof(line));
        Check(used > 0 && std::strstr(line, "SHOT LAG Halo 3 transport [muzzle] (dev): transported 3 (measured 2, "
                                            "extrapolated 1), P as published 2 (no camera sample 1, no fire eye or "
                                            "velocity 1)") != nullptr,
              "window: what was used, and why not");
        Check(std::strstr(line, "along travel +70 mm (max +80, 2 moving)") && std::strstr(line, "at rest |2| mm (1)"),
              "window: the shift along travel, and at rest (a cross-source offset would show here)");
        Check(std::strstr(line, "camera sample 15.2 ms old") && std::strstr(line, "fire eye 1.0 ms old") &&
                  std::strstr(line, "fire eye: measured 2, seated 1, not this tick 1"),
              "window: the ages and the fire-eye verdicts");
        TransportWindow empty;
        Check(empty.Format("x", line, sizeof(line)) == 0, "window: nothing to report is no line");
        char small[24]{};
        Check(w.Format("SHOT LAG Halo 3 transport", small, sizeof(small)) < sizeof(small) &&
                  small[sizeof(small) - 1] == 0,
              "window: a short buffer truncates safely");
    }

    // ---- the record through P' (4a with 4b) ----
    {
        // As the record test above: E_P (P's camera sample) at the origin, P 10 m out, E' 272 mm along a 4 m/s strafe
        // (140 mm at the tick plus one 33 ms tick carried). Transported, the record from E' is the solved line moved.
        const float p[3]{10.0f * kWuPerM, 0.0f, 0.0f};
        RecomposeInput in{};
        in.onFoot = true;
        in.intentRead = true;
        in.intent.generation = 5;
        in.intent.stampUs = 1000000;
        std::memcpy(in.intent.point, p, sizeof(p));
        in.intent.rangeWu = p[0];
        in.intent.dir[0] = 1.0f;
        in.intent.pointEyeValid = true;
        in.intent.pointEyeStampUs = 990000;
        in.generation = 5;
        in.nowUs = 1040000;
        in.maxAgeUs = 150000;
        in.firstPersonEye = true;
        in.eyeRead = true;
        in.eye[1] = 0.14f * kWuPerM;
        in.eyeVel[1] = 4.0f * kWuPerM;
        in.eyeVelValid = true;
        in.eyeProven = true;
        in.tickS = 0.033f;
        float yaw = 9.0f, pitch = 9.0f, bend = -1.0f, shift[3]{};
        Transport how = Transport::Bad;
        in.transport = true;
        Check(RecomposeRecord(in, yaw, pitch, bend, &how, shift) == Recompose::Recomposed && how == Transport::Measured,
              "transported: recomposed, measured from the game's eye");
        Check(std::fabs(yaw) < 1.0e-5f && std::fabs(pitch) < 1.0e-5f && bend < 1.0e-3f,
              "the record is the solved direction (straight down +X): a body-fixed aim, as flat Halo");
        Check(Near(shift[1] * kMmPerWu, (0.14f + 4.0f * 0.033f) * 1000.0f, 0.05f), "the shift is E' - E_P");
        in.intent.pointEyeValid = false;
        how = Transport::Bad;
        Check(RecomposeRecord(in, yaw, pitch, bend, &how, shift) == Recompose::Recomposed &&
                  how == Transport::NoPointEye &&
                  std::fabs(std::atan2(-(0.14f + 4.0f * 0.033f), 10.0f) - yaw) < 1.0e-4f,
              "no camera sample: through P as published, and said so");
        in.transport = false;
        how = Transport::Bad;
        Check(RecomposeRecord(in, yaw, pitch, bend, &how) == Recompose::Recomposed && how == Transport::Count,
              "the key off: no transport asked");
    }

    // ---- the game's eye when the acquisition did not publish one (4c) ----
    {
        TickEyeFallbackInput in{};
        in.hooked = true;
        in.sameGeneration = true;
        in.unit = 0x00CA;
        in.firstPersonEye = true;
        in.acquisition = TickEyeAcquisition::Skipped;
        Check(TickEyeFallbackGate(in) == TickEyeMiss::FromEvaluator,
              "our first-person eye, the game skipped its acquisition, hooks live: ask the evaluator");
        TickEyeFallbackInput x = in;
        x.hooked = false;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::NoHook, "no evaluator hook: not asked, counted");
        x = in;
        x.sameGeneration = false;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::NoHook, "hooks from another generation: not asked");
        x = in;
        x.unit = -1;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::NoUnit, "no local unit: not asked");
        x = in;
        x.firstPersonEye = false;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::NotFirstPersonEye,
              "not a first-person eye (a seat, the Monitor, an orbit camera): not asked");
        x = in;
        x.acquisition = TickEyeAcquisition::RanWithoutEvaluator;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::GameDeclined,
              "the acquisition ran and declined the unit (an unarmed Monitor): not asked");
        x.firstPersonEye = false;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::NotFirstPersonEye, "the identity is checked before the decline");
        x = in;
        x.hooked = false;
        x.unit = -1;
        x.firstPersonEye = false;
        Check(TickEyeFallbackGate(x) == TickEyeMiss::NoHook, "the hooks are checked first");
        Check(EvaluatorTookFirstPersonPath(0) && !EvaluatorTookFirstPersonPath(1) && !EvaluatorTookFirstPersonPath(17),
              "the evaluator's 0 is the first-person update; anything else the chase camera");
        Check(ClassifyTickEyeAcquisition(0, 5000000, 16667) == TickEyeAcquisition::Skipped,
              "never ran: the game skipped its acquisition");
        Check(ClassifyTickEyeAcquisition(4990000, 5000000, 16667) == TickEyeAcquisition::RanWithoutEvaluator,
              "ran since the previous stick call without publishing: it never reached the evaluator");
        Check(ClassifyTickEyeAcquisition(4950000, 5000000, 16667) == TickEyeAcquisition::Skipped,
              "last ran three ticks ago: skipped this tick");
        Check(ClassifyTickEyeAcquisition(5100000, 5000000, 16667) == TickEyeAcquisition::Skipped,
              "a stamp from the future is not a run");
        // This tick's eye only.
        Check(TickEyeFromThisTick(true, 5000100, 5000000), "published after the previous stick call: this tick's");
        Check(!TickEyeFromThisTick(true, 4990000, 5000000),
              "published before the previous stick call: held over (the 100 ms window let one stand six ticks)");
        Check(!TickEyeFromThisTick(true, 5000000, 5000000), "published at the previous stick call: not this tick's");
        Check(!TickEyeFromThisTick(false, 5000100, 5000000), "not read: not this tick's");
        Check(!TickEyeFromThisTick(true, 0, 0), "no stamp: never");
        Check(TickEyeFromThisTick(true, 1, 0), "the first stick call takes any fresh eye");
        bool named = true;
        for (size_t r = 0; r < static_cast<size_t>(TickEyeMiss::Count); ++r)
            named = named && std::strcmp(TickEyeMissName(static_cast<TickEyeMiss>(r)), "?") != 0;
        Check(named, "every game-eye miss has a name for the SHOT RECORD line");
    }

    // ---- the first-person eye identity (4d) ----
    {
        FirstPersonEyeInput in{};
        in.onFootControlled = true;
        in.directorRead = true;
        in.mode = kHalo3CameraModeFirstPerson;
        in.perspective = kHalo3PerspectiveFirstPerson;
        Check(ClassifyFirstPersonEye(in) == FirstPersonEye::Yes, "on foot in control, the first-person camera: ours");
        for (int32_t mode = 0; mode < kHalo3CameraModeCount; ++mode)
        {
            if (mode == kHalo3CameraModeFirstPerson)
                continue;
            FirstPersonEyeInput x = in;
            x.mode = mode;
            char what[96];
            std::snprintf(what, sizeof(what), "the director's %s camera is not a first-person eye",
                          Halo3CameraModeName(mode));
            Check(ClassifyFirstPersonEye(x) == FirstPersonEye::NotFirstPersonCamera, what);
        }
        FirstPersonEyeInput x = in;
        x.mode = 1;   // the orbit the Forge director switches to on the object its flying camera hits (0x21259B)
        x.perspective = 1;
        Check(ClassifyFirstPersonEye(x) == FirstPersonEye::NotFirstPersonCamera, "Forge's orbit camera: refused");
        x = in;
        x.perspective = 3;   // a neutral perspective on the first-person camera
        Check(ClassifyFirstPersonEye(x) == FirstPersonEye::NotFirstPersonCamera,
              "the first-person camera from another perspective: refused");
        x = in;
        x.directorRead = false;
        Check(ClassifyFirstPersonEye(x) == FirstPersonEye::DirectorUnread, "an unread director: no identity, refused");
        x = in;
        x.mode = 8;
        Check(ClassifyFirstPersonEye(x) == FirstPersonEye::DirectorUnread, "a mode out of range is not a reading");
        x = in;
        x.perspective = -1;
        Check(ClassifyFirstPersonEye(x) == FirstPersonEye::DirectorUnread, "a perspective out of range neither");
        x = in;
        x.onFootControlled = false;
        x.directorRead = false;
        Check(ClassifyFirstPersonEye(x) == FirstPersonEye::NotOnFootControlled, "a seat or a cinematic is named first");
        Check(DirectorCameraPlausible(0, 0) && DirectorCameraPlausible(7, 3) && !DirectorCameraPlausible(8, 0) &&
                  !DirectorCameraPlausible(0, 4) && !DirectorCameraPlausible(-1, 0),
              "the director's mode is 0..7 and its perspective 0..3");
        Check(std::strcmp(Halo3CameraModeName(1), "orbiting") == 0 &&
                  std::strcmp(Halo3CameraModeName(3), "first person") == 0 &&
                  std::strcmp(Halo3CameraModeName(9), "?") == 0,
              "camera modes are named as set_camera_mode's switch numbers them");
        bool named = true;
        for (size_t r = 0; r < static_cast<size_t>(FirstPersonEye::Count); ++r)
            named = named && std::strcmp(FirstPersonEyeName(static_cast<FirstPersonEye>(r)), "?") != 0;
        Check(named, "every identity verdict has a name for the SHOT RECORD line");

        // The two sites exactly as the pinned halo3.dll (B209D845...EFB63) has them: the first-person model cache's
        // gate at +0x28AE09 and set_camera_mode's store at +0x131C44.
        const uint8_t cache[kDirectorCacheSiteBytes]{
            0xBD, 0x88, 0x01, 0x00, 0x00, 0x3B, 0xC6, 0x75, 0x4C, 0x3B, 0xDE, 0x74, 0x48, 0x8B, 0x0D, 0x80, 0xF1,
            0x7A, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x14, 0xC8, 0x48, 0x8D,
            0x0C, 0x5B, 0x48, 0x8B, 0x04, 0x2A, 0x83, 0xBC, 0x88, 0x04, 0x06, 0x00, 0x00, 0x00, 0x75};
        const uint8_t setMode[kDirectorSetModeSiteBytes]{
            0x65, 0x48, 0x8B, 0x0C, 0x25, 0x58, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x88, 0x01, 0x00, 0x00, 0x4A,
            0x8B, 0x0C, 0xF9, 0x49, 0x8B, 0x14, 0x08, 0x48, 0x63, 0x8E, 0x58, 0x01, 0x00, 0x00, 0x4C, 0x8D,
            0x04, 0x49, 0x42, 0x39, 0x84, 0x82, 0x04, 0x06, 0x00, 0x00, 0x75, 0x0A, 0x42, 0x39, 0xAC, 0x82,
            0x08, 0x06, 0x00, 0x00, 0x74, 0x32, 0x42, 0x89, 0x84, 0x82, 0x04, 0x06, 0x00, 0x00};
        Check(PatternMatches(kHalo3DirectorCacheSitePattern, cache, sizeof(cache)),
              "the cache-site pattern matches the pinned bytes, byte for byte");
        Check(PatternMatches(kHalo3DirectorSetModeSitePattern, setMode, sizeof(setMode)),
              "the set_camera_mode pattern matches the pinned bytes, byte for byte");
        DirectorCameraSites sites{};
        Check(DecodeDirectorCameraSites(cache, setMode, sites) && sites.blockOffset == 0x188 &&
                  sites.perspectiveOffset == 0x604 && sites.modeOffset == 0x608,
              "decoded: block TLS +0x188, perspective +0x604, mode +0x608");
        Check(0x28AE09u + static_cast<uint32_t>(kDirectorCacheTlsNext) + static_cast<uint32_t>(sites.tlsIndexDisp) ==
                  0xA39F9Cu,
              "the TLS index decodes to +0xA39F9C, the engine TLS index every other Halo 3 lane binds");
        const auto refusedSites = [&](size_t at, bool inCache, const char* what) {
            uint8_t c[kDirectorCacheSiteBytes], m[kDirectorSetModeSiteBytes];
            std::memcpy(c, cache, sizeof(c));
            std::memcpy(m, setMode, sizeof(m));
            (inCache ? c : m)[at] ^= 0x08;
            DirectorCameraSites out{};
            Check(!DecodeDirectorCameraSites(c, m, out) && out.blockOffset == 0, what);
        };
        refusedSites(kDirectorCacheBlock, true, "the two sites' blocks disagree: nothing bound");
        refusedSites(kDirectorCachePerspective, true, "the cache's perspective field disagrees: nothing bound");
        refusedSites(kDirectorSetModePerspective, false, "set_camera_mode's compare disagrees: nothing bound");
        refusedSites(kDirectorSetModePerspectiveStore, false, "set_camera_mode's store disagrees: nothing bound");
        refusedSites(kDirectorSetModeMode, false, "the mode is not the field after the perspective: nothing bound");
        DirectorCameraSites none{};
        Check(!DecodeDirectorCameraSites(nullptr, setMode, none) && !DecodeDirectorCameraSites(cache, nullptr, none),
              "a missing site binds nothing");
    }

    // ---- the frame-coherent pair ----
    {
        EyeFrame eyes[3]{};
        HandFrame hands[3]{};
        const float k1[3]{0.1f, 1.6f, 0.2f}, k2[3]{0.11f, 1.6f, 0.2f}, k3[3]{0.12f, 1.6f, 0.2f};
        eyes[0] = {true, {k1[0], k1[1], k1[2]}, {}, {}, 1000, 1};
        eyes[1] = {true, {k2[0], k2[1], k2[2]}, {}, {}, 12000, 2};
        hands[0] = {true, {k1[0], k1[1], k1[2]}, {}, 2000, 1};
        hands[1] = {true, {k2[0], k2[1], k2[2]}, {}, 13000, 2};
        hands[2] = {true, {k3[0], k3[1], k3[2]}, {}, 24000, 3};   // its frame has not rendered yet
        size_t e = 9, h = 9;
        Check(SelectCoherentPair(eyes, 3, hands, 3, 100000, e, h) && e == 1 && h == 1,
              "the newest hand whose frame has rendered pairs with that render");
        Check(!SelectCoherentPair(eyes, 3, hands + 2, 1, 100000, e, h), "no rendered frame for the hand: no pair");
        Check(!SelectCoherentPair(eyes, 3, hands, 3, 500, e, h), "a key matched across a long skew is refused");
        // A frozen head: the same key on two renders -- the newest render.
        eyes[2] = {true, {k2[0], k2[1], k2[2]}, {}, {}, 13500, 2};
        Check(SelectCoherentPair(eyes, 3, hands, 3, 100000, e, h) && e == 2 && h == 1,
              "several renders under one head sample: the newest");
        EyeFrame invalid[1]{};
        invalid[0] = eyes[0];
        invalid[0].valid = false;
        Check(!SelectCoherentPair(invalid, 1, hands, 1, 100000, e, h), "an invalid slot never pairs");
        // A RENDER STALL: the newest hand whose frame rendered is old by now. Bounded by the hand's age at `now`.
        Check(SelectCoherentPair(eyes, 3, hands, 3, 100000, e, h, 13000 + 40000, 50000) && h == 1,
              "a hand 40 ms old pairs");
        Check(!SelectCoherentPair(eyes, 3, hands, 3, 100000, e, h, 13000 + 60000, 50000),
              "a hand 60 ms old is the aim of that moment, not of now: no pair (the poll's own pair stands)");
        Check(SelectCoherentPair(eyes, 3, hands, 3, 100000, e, h, 2000 + 40000, 50000) && h == 1,
              "...while it is the newest within the bound, the newest still wins");
        Check(!SelectCoherentPair(eyes, 3, hands, 1, 100000, e, h, 1000, 50000),
              "a hand stamped after `now` is never paired");
        Check(SelectCoherentPair(eyes, 3, hands, 3, 100000, e, h, 13000 + 60000, 0) && h == 1,
              "a zero bound is unbounded (the old behaviour)");
        HandFrame staleNewest[2]{hands[0], hands[1]};
        staleNewest[0].stampUs = 30000;   // hand k1 re-stamped newer than hand k2: only it is within the bound
        Check(SelectCoherentPair(eyes, 2, staleNewest, 2, 100000, e, h, 13000 + 60000, 50000) && h == 0,
              "the bound picks the newest hand within it, never an older one past it");
    }

    // ---- the witness ----
    {
        // Strafing right (+y) at 4 m/s while firing along +x at a point 10 m out, from an eye 140 mm further along
        // the strafe than the solve's: a parallel line ahead of P along the travel by 140 mm, nothing across.
        const float p[3]{10.0f * kWuPerM, 0.0f, 0.0f};
        const float e0[3]{0.0f, 0.0f, 0.0f};
        const float o[3]{0.0f, 0.14f * kWuPerM, 0.0f};
        const float d[3]{1.0f, 0.0f, 0.0f};
        const float v[3]{0.0f, 4.0f * kWuPerM, 0.0f};
        const ShotLagSample s = MeasureShotLag(o, d, p, e0, v);
        Check(s.valid && Near(s.missMm, 140.0f, 0.5f), "the miss is the parallel offset");
        Check(Near(s.alongMm, 140.0f, 0.5f) && Near(s.acrossMm, 0.0f, 0.5f), "...all of it along the travel, ahead");
        Check(s.stale && Near(s.staleAlongMm, 140.0f, 0.5f) && Near(s.impliedDtMs, 35.0f, 0.2f),
              "the stale eye is 140 mm along the travel: 35 ms at 4 m/s");
        Check(s.bucket == 2 && Near(s.speedMps, 4.0f, 1.0e-3f), "4 m/s is the 4-10 bucket");
        const ShotLagSample back = MeasureShotLag(o, d, p, e0, nullptr);
        Check(back.valid && !back.velocityKnown && back.bucket == -1 && Near(back.acrossMm, 140.0f, 0.5f) &&
                  !back.stale,
              "without a velocity the miss is all 'across' and nothing is signed");
        // The fixed shot: from the moved origin through P.
        const float fixed[3]{p[0] - o[0], p[1] - o[1], p[2] - o[2]};
        const ShotLagSample good = MeasureShotLag(o, fixed, p, e0, v);
        Check(good.valid && good.missMm < 0.01f && good.stale && Near(good.impliedDtMs, 35.0f, 0.2f),
              "a shot through P misses by nothing, whatever the stale eye says");
        // Moving along the shot: no travel axis across it.
        const float vAlong[3]{5.0f * kWuPerM, 0.0f, 0.0f};
        const ShotLagSample ahead = MeasureShotLag(o, d, p, e0, vAlong);
        Check(ahead.valid && !ahead.travel && !ahead.stale && ahead.bucket == 2,
              "running straight at the point has no across-shot travel");
        Check(ShotLagBucket(0.5f) == 0 && ShotLagBucket(1.0f) == 1 && ShotLagBucket(3.99f) == 1 &&
                  ShotLagBucket(4.0f) == 2 && ShotLagBucket(10.0f) == 3 && ShotLagBucket(-1.0f) == -1,
              "the speed buckets");
        const float zero[3]{0, 0, 0};
        Check(!MeasureShotLag(o, zero, p, e0, v).valid, "a zero shot direction is unmeasured");

        ShotLagWindow window;
        window.Add(s, false, -1);
        window.Add(good, false, -1);
        window.Add(back, true, 7);
        window.Add(ShotLagSample{}, true, 7);
        char line[1024]{};
        const size_t n = window.Format("Halo 3", line, sizeof(line));
        Check(n > 0 && std::strstr(line, "SHOT LAG Halo 3 (dev): 3 shots (+1 unmeasured") != nullptr,
              "the line counts measured and unmeasured shots");
        Check(std::strstr(line, "4-10: 2, miss 70 mm (along +70, across 0)") != nullptr,
              "per-bucket mean miss, along and across");
        Check(std::strstr(line, "speed unknown: 1, miss 140 mm") != nullptr, "shots without a velocity apart");
        Check(std::strstr(line, "mean lag axis along travel +70 mm") != nullptr, "the mean along-travel bias");
        Check(std::strstr(line, "stale eye along travel +140 mm (implied dt 35 ms)") != nullptr,
              "the stale-eye term and its dt");
        Check(std::strstr(line, "seated=1 seat=7") != nullptr, "the last shot's seat");
        ShotLagWindow emptyWindow;
        Check(emptyWindow.Format("Reach", line, sizeof(line)) == 0, "an empty window logs nothing");
        char tiny[16]{};
        Check(window.Format("Halo 3", tiny, sizeof(tiny)) < sizeof(tiny) && tiny[sizeof(tiny) - 1] == 0,
              "a short buffer truncates safely");
    }

    // ---- flat parity in the other titles ---------------------------------------------------------------------------
    {
        // THE RULE THAT MAKES A CONSUMER IMMUNE: a DIRECTION solved from E_P, applied at the fire's own eye, already is
        // flat parity -- its line passes through P' = P + (E_fire - E_P), the transported point. A WORLD point aimed
        // through from the fire (the muzzle, a re-aimed query) is not: it misses P' by the eye's travel.
        const float eP[3]{0.0f, 0.0f, 0.0f};
        const float p[3]{7.0f * kWuPerM, 0.5f * kWuPerM, 0.2f * kWuPerM};
        const float eFire[3]{0.0f, 0.1f * kWuPerM, 0.0f};   // one 60 Hz tick of a 6 m/s strafe
        TransportInput in{};
        std::memcpy(in.point, p, sizeof(p));
        in.pointEyeValid = true;
        std::memcpy(in.pointEye, eP, sizeof(eP));
        in.pointEyeStampUs = 1000;
        in.fireEyeValid = true;
        std::memcpy(in.fireEye, eFire, sizeof(eFire));
        in.nowUs = 5000;
        in.maxAgeUs = 250000;
        float pMoved[3]{}, shift[3]{};
        Check(TransportPoint(in, pMoved, shift) == Transport::Measured, "a measured fire eye transports P");
        const float solved[3]{p[0] - eP[0], p[1] - eP[1], p[2] - eP[2]};
        Check(LineMiss(eFire, solved, pMoved) * kMmPerWu < 0.01f,
              "the solved DIRECTION applied at the fire eye passes through P': direction consumers are immune");
        const float throughP[3]{p[0] - eFire[0], p[1] - eFire[1], p[2] - eFire[2]};
        Check(Near(LineMiss(eFire, throughP, pMoved) * kMmPerWu, 100.0f, 2.0f),
              "a line from the fire eye through the world point P misses P' by the tick's travel: point consumers trail");
    }
    {
        // EvaluatorEyeProof: a camera evaluator asked at the fire (Halo 2, CE) is E_fire only once proven at rest,
        // and only on the perspective it answered while agreeing.
        EvaluatorEyeProof proof;
        Check(proof.Verdict(0, true) == FireEye::Unproven, "an unproven evaluator is no fire eye");
        proof.Note(5, 9, true, true, 400.0f, 0);
        Check(!proof.pathKnown, "a disagreeing sample latches no perspective");
        for (int i = 0; i < 29; ++i)
            proof.Note(5, 9, true, true, 10.0f, 0);
        Check(proof.pathKnown && proof.path == 0 && proof.Verdict(0, true) == FireEye::Unproven,
              "the perspective latches on the first agreeing sample; 29 agreeing samples are not yet a proof");
        proof.Note(5, 9, true, true, 10.0f, 2);   // another camera path: ends the stint, says nothing about path 0
        proof.Note(5, 9, true, true, 10.0f, 0);
        Check(proof.Verdict(0, true) == FireEye::Unproven, "another perspective breaks the streak");
        for (int i = 0; i < 60; ++i)
            proof.Note(5, 9, true, i % 2 == 0, i % 2 == 0 ? 10.0f : 900.0f, 0);   // moving samples are no evidence
        Check(proof.Verdict(0, true) == FireEye::Measured, "30 agreeing at-rest samples prove it (moving ones ignored)");
        Check(proof.Verdict(1, true) == FireEye::ChasePath, "a proven evaluator on another perspective is not the eye");
        Check(proof.Verdict(0, false) == FireEye::Faulted, "a non-finite answer is a fault");
        proof.Note(5, 9, true, false, 5000.0f, 0);
        Check(proof.Verdict(0, true) == FireEye::Measured, "no size limit: a moving eye far from E_P stays proven");
        for (int i = 0; i < 15; ++i)
            proof.Note(5, 9, true, true, 400.0f, 0);
        Check(proof.Verdict(0, true) == FireEye::Unproven, "a streak of disagreeing at-rest samples disproves it");
        proof.Note(6, 9, true, true, 10.0f, 3);
        Check(proof.path == 3 && proof.Verdict(3, true) == FireEye::Unproven,
              "a new title generation starts over, perspective included");
    }
    {
        // ChooseSampleFireEye: the evaluator's eye when measured; else E_P carried ONE TICK (SampleCarrySeconds) --
        // never by the newest render's few ms of age alone, which would make the A/B arm move nothing
        // (shot-placement.md 2).
        const float eP[3]{1.0f, 2.0f, 3.0f};
        const float asked[3]{1.5f, 2.0f, 3.0f};
        const float vel[3]{0.0f, 6.0f * kWuPerM, 0.0f};
        float out[3]{};
        Check(ChooseSampleFireEye(FireEye::Measured, asked, true, eP, vel, 1.0f / 60.0f, out) == FireEye::Measured &&
                  out[0] == 1.5f,
              "a measured evaluator answer is E_fire");
        Check(ChooseSampleFireEye(FireEye::Unproven, asked, true, eP, vel, 1.0f / 60.0f, out) == FireEye::CarriedTick &&
                  Near((out[1] - eP[1]) * kMmPerWu, 100.0f, 0.5f) && out[0] == 1.0f,
              "unproven: E_P carried one 60 Hz tick of a 6 m/s strafe = 100 mm, non-zero by construction");
        Check(ChooseSampleFireEye(FireEye::Count, nullptr, true, eP, vel, 1.0f / 30.0f, out) == FireEye::CarriedTick &&
                  Near((out[1] - eP[1]) * kMmPerWu, 200.0f, 0.5f),
              "not asked (CE at 30 Hz): 200 mm");
        Check(ChooseSampleFireEye(FireEye::Unproven, asked, true, eP, nullptr, 1.0f / 60.0f, out) ==
                      FireEye::CarriedTick &&
                  out[0] == 1.0f && out[1] == 2.0f && out[2] == 3.0f,
              "no velocity: the eye itself (a 0 mm transport at rest)");
        Check(ChooseSampleFireEye(FireEye::ChasePath, asked, false, eP, vel, 1.0f / 60.0f, out) == FireEye::ChasePath,
              "no camera sample: the evaluator's own refusal");
        Check(ChooseSampleFireEye(FireEye::Count, nullptr, false, eP, vel, 1.0f / 60.0f, out) == FireEye::NotRead,
              "nothing at all: not read");
        Check(ChooseSampleFireEye(FireEye::Unproven, asked, true, eP, vel, -1.0f, out) == FireEye::NotThisTick,
              "a stale camera sample is no fire eye (freshness)");
        // How far: one tick past the render the player aimed with, or the sample's own age when that is longer.
        Check(Near(SampleCarrySeconds(1000000, 1004000, 1.0f / 60.0f, 250000), 1.0f / 60.0f),
              "a 4 ms old sample from the newest render is carried one tick, not 4 ms");
        Check(Near(SampleCarrySeconds(1000000, 1040000, 1.0f / 60.0f, 250000), 0.040f),
              "a 40 ms old sample (renders stalled) is carried its age");
        Check(SampleCarrySeconds(0, 1040000, 1.0f / 60.0f, 250000) < 0.0f &&
                  SampleCarrySeconds(2000000, 1040000, 1.0f / 60.0f, 250000) < 0.0f &&
                  SampleCarrySeconds(1000000, 1300000, 1.0f / 60.0f, 250000) < 0.0f,
              "no stamp, a future stamp or an older-than-250 ms sample is refused");
        // The tick period it carries by.
        uint32_t period = 0;
        period = SmoothTickPeriod(period, 16667);
        Check(period == 16667, "the first gap is the period");
        period = SmoothTickPeriod(period, 33333);
        Check(period == 16667 + (33333 - 16667) / 8, "then smoothed by 1/8");
        Check(SmoothTickPeriod(period, 1500) == period && SmoothTickPeriod(period, 300000) == period,
              "another unit in the same tick, and a stall, are not periods");
    }
    {
        // 4f, Reach: MEASURED OR NOTHING. ChooseMeasuredFireEye never carries or extrapolates.
        const float asked[3]{1.5f, 2.0f, 3.0f};
        const float bad[3]{1.0f, NAN, 3.0f};
        float out[3]{9.0f, 9.0f, 9.0f};
        Check(ChooseMeasuredFireEye(FireEye::Measured, asked, out) == FireEye::Measured && out[0] == 1.5f,
              "a proven answer asked in this fire is E_fire");
        out[0] = 9.0f;
        Check(ChooseMeasuredFireEye(FireEye::Unproven, asked, out) == FireEye::Unproven && out[0] == 9.0f,
              "unproven: no eye at all (nothing carried), out untouched");
        Check(ChooseMeasuredFireEye(FireEye::Measured, bad, out) == FireEye::Faulted && out[0] == 9.0f,
              "a non-finite answer is a fault");
        Check(ChooseMeasuredFireEye(FireEye::Count, nullptr, out) == FireEye::NotRead, "not asked: not read");
        Check(ChooseMeasuredFireEye(FireEye::OtherUnit, asked, out) == FireEye::OtherUnit &&
                  !FireEyeUsable(ChooseMeasuredFireEye(FireEye::ChasePath, asked, out)),
              "every other verdict passes through, unusable");
    }
    {
        // FireEyeScope: asked once per fire, for one unit.
        FireEyeScope scope;
        float out[3]{};
        Check(scope.For(7, out) == FireEye::NotRead, "a fresh fire has asked nothing");
        const float eye[3]{4.0f, 5.0f, 6.0f};
        scope.Store(7, true, eye);
        Check(scope.For(7, out) == FireEye::Measured && out[0] == 4.0f && out[2] == 6.0f,
              "the fire's answer for its unit");
        Check(scope.For(8, out) == FireEye::OtherUnit && scope.For(-1, out) == FireEye::OtherUnit,
              "never another unit's");
        scope.Store(7, false, nullptr);
        Check(scope.For(7, out) == FireEye::Faulted, "an evaluator that could not be asked is a fault");
        const float nonFinite[3]{INFINITY, 0.0f, 0.0f};
        scope.Store(7, true, nonFinite);
        Check(scope.For(7, out) == FireEye::Faulted, "a non-finite answer is a fault");
        scope = FireEyeScope{};
        Check(scope.For(7, out) == FireEye::NotRead, "the next fire opens empty");
    }
    {
        // EyeAtRest and PointLineDistance (the proof's rest bar; the dev native-line witness).
        const float slow[3]{0.2f * kWuPerM, 0.0f, 0.0f}, fast[3]{0.3f * kWuPerM, 0.0f, 0.0f};
        Check(EyeAtRest(true, slow) && !EyeAtRest(true, fast) && !EyeAtRest(false, slow),
              "at rest: a known velocity under 0.25 m/s");
        const float o[3]{0.0f, 0.0f, 0.0f}, d[3]{10.0f, 0.0f, 0.0f}, q[3]{3.0f, 0.04f, 0.03f};
        Check(Near(PointLineDistance(q, o, d), 0.05f, 1.0e-6f), "a point's distance from a line of any length");
        const float zero[3]{};
        Check(std::isnan(PointLineDistance(q, o, zero)), "no line, no distance");
    }
    {
        // The Reach transport end to end, with a measured-only sample (velocity never used to extrapolate).
        // Strafing 6 m/s along +Y, aiming down +X at a wall 7 m away; the fire is one 60 Hz tick after P's render.
        const float eP[3]{0.0f, 0.0f, 0.0f};
        const float p[3]{7.0f * kWuPerM, 0.0f, 0.0f};
        const float eFire[3]{0.0f, 0.1f * kWuPerM, 0.0f};   // 100 mm along travel
        TransportInput in{};
        std::memcpy(in.point, p, sizeof(p));
        in.pointEyeValid = true;
        std::memcpy(in.pointEye, eP, sizeof(eP));
        in.pointEyeStampUs = 1000000;
        in.nowUs = 1016667;
        in.maxAgeUs = 250000;
        in.velValid = false;   // measuredOnly: the velocity is not given to TransportPoint
        float pMoved[3]{}, shift[3]{};
        Check(TransportPoint(in, pMoved, shift) == Transport::NoFireEye,
              "Reach without a measured fire eye: P as published (no extrapolation, no carry)");
        in.fireEyeValid = true;
        std::memcpy(in.fireEye, eFire, sizeof(eFire));
        Check(TransportPoint(in, pMoved, shift) == Transport::Measured && Near(shift[1] * kMmPerWu, 100.0f, 0.01f),
              "with it, P moves by exactly E_fire - E_P");
        // The native shot leaves from E_fire's line along the record's DIRECTION, solved from E_P toward P: re-aimed
        // through P' it is already through (left as built); re-aimed through P it would bend back by the travel.
        const float solved[3]{p[0] - eP[0], p[1] - eP[1], p[2] - eP[2]};
        const float native[3]{eFire[0] + 0.88f * kWuPerM, eFire[1], eFire[2]};   // the muzzle projected on that line
        float reaimed[3]{};
        Check(ReaimThroughPoint(native, solved, pMoved, ReaimRails{}, reaimed) == Reaim::AlreadyThrough,
              "through P' the native line is left exactly as Reach built it (flat parity)");
        Check(ReaimThroughPoint(native, solved, p, ReaimRails{}, reaimed) == Reaim::Applied &&
                  Near(LineMiss(native, solved, p) * kMmPerWu, 100.0f, 0.5f),
              "through P it is bent back onto a point 100 mm behind along travel (the trail)");
        // If Reach fires from the eye the render showed, the measured shift is zero: a no-op, not an over-correction.
        std::memcpy(in.fireEye, eP, sizeof(eP));
        Check(TransportPoint(in, pMoved, shift) == Transport::Measured && Near(Length(shift), 0.0f, 1.0e-9f) &&
                  pMoved[0] == p[0] && pMoved[1] == p[1],
              "E_fire == E_P: P' == P");
    }

    std::printf("shot_lag_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
