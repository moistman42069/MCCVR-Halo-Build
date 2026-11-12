// Seated aim upstream of replication (src/common/seat_upstream_logic.h): the stock query model and its exact inverse,
// the gain learned from the game's own answers, the write that was in effect at a tick, the seated recomposition's
// fail-closed rules, and the stock-line witness.
#include "seat_upstream_logic.h"

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
constexpr float kWuPerM = shot_lag::kWuPerMetre;

float LineMissWu(const float origin[3], const float direction[3], const float point[3])
{
    const float l = shot_lag::Length(direction);
    const float d[3]{direction[0] / l, direction[1] / l, direction[2] / l};
    const float w[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const float t = shot_lag::Dot(w, d);
    const float m[3]{w[0] - d[0] * t, w[1] - d[1] * t, w[2] - d[2] * t};
    return shot_lag::Length(m);
}

void TestModelAndInverse()
{
    const float r[3]{1.0f, 0.0f, 0.0f};
    const float still[3]{};
    float q[3]{};
    Check(seat_upstream::StockQuery(r, still, 0.016f, q) && std::fabs(q[0] - 1.0f) < 1e-6f,
          "at rest the stock query is the record");
    // A Hornet-like strafe: 17 m/s sideways, 1/g = 190 m/s -> the query leaves the record by atan(17/190) ~ 5.1 deg,
    // against the travel.
    const float v[3]{0.0f, 17.0f * kWuPerM, 0.0f};
    const float g = 1.0f / (190.0f * kWuPerM);
    Check(seat_upstream::StockQuery(r, v, g, q), "the model answers");
    const float dev = seat_upstream::AngleDeg(r, q);
    Check(std::fabs(dev - std::atan(17.0f / 190.0f) * shot_lag::kRadToDeg) < 0.02f,
          "a 17 m/s strafe at 1/g = 190 m/s moves the query ~5.1 deg");
    Check(q[1] < 0.0f, "the query leaves the record against the travel");

    // The inverse is exact: the record it returns maps back onto the wanted direction.
    const float wants[][3]{{1.0f, 0.0f, 0.0f}, {0.6f, 0.7f, -0.2f}, {0.1f, -0.9f, 0.4f}, {-0.3f, 0.2f, 0.93f}};
    const float vels[][3]{{0, 0, 0}, {0, 17.0f * kWuPerM, 0}, {12.0f * kWuPerM, -9.0f * kWuPerM, 3.0f * kWuPerM},
                          {40.0f * kWuPerM, 0, 0}};
    const float gains[]{0.0f, g, 1.0f / (440.0f * kWuPerM), 1.0f / (60.0f * kWuPerM)};
    for (const auto& want : wants)
        for (const auto& vel : vels)
            for (const float gain : gains)
            {
                float rec[3]{}, back[3]{};
                const auto why = seat_upstream::RecordForQuery(want, vel, gain, rec);
                if (why != seat_upstream::Invert::Solved)
                {
                    // Only possible when the velocity across the line is at least 1/g.
                    float w[3]{want[0], want[1], want[2]};
                    seat_upstream::Normalize(w);
                    const float along = shot_lag::Dot(w, vel);
                    const float across2 = shot_lag::Dot(vel, vel) - along * along;
                    Check(gain * gain * across2 >= 0.999f, "the inverse refuses only an unreachable query");
                    continue;
                }
                Check(std::fabs(shot_lag::Length(rec) - 1.0f) < 1e-5f, "the inverse's record is a unit vector");
                Check(seat_upstream::StockQuery(rec, vel, gain, back), "its stock query answers");
                float w[3]{want[0], want[1], want[2]};
                seat_upstream::Normalize(w);
                Check(seat_upstream::AngleDeg(w, back) < 1.0e-3f, "the inverse's record maps exactly onto the query");
            }
    float out[3]{9, 9, 9};
    const float zero[3]{};
    Check(seat_upstream::RecordForQuery(zero, v, g, out) == seat_upstream::Invert::Degenerate && out[0] == 9.0f,
          "a zero query is refused and writes nothing");
    const float nan[3]{NAN, 0, 0};
    Check(seat_upstream::RecordForQuery(nan, v, g, out) == seat_upstream::Invert::NonFinite, "non-finite refused");
    // 1/g below the speed across the line: no record reaches it (the game could not aim there either).
    const float fast[3]{0.0f, 300.0f * kWuPerM, 0.0f};
    Check(seat_upstream::RecordForQuery(r, fast, g, out) == seat_upstream::Invert::NoSolution,
          "a query the model cannot reach is refused");
}

void TestGainFit()
{
    // The game's answers under a known gain, at varied velocities and records: the fit recovers it.
    const float g = 1.0f / (190.0f * kWuPerM);
    seat_upstream::GainFit fit;
    Check(!fit.Learned(seat_upstream::kGainMinWeight), "nothing learned before a sample");
    unsigned seed = 12345u;
    const auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float((seed >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
    };
    for (int i = 0; i < 200; ++i)
    {
        float rec[3]{1.0f, 0.3f * rnd(), 0.2f * rnd()};
        seat_upstream::Normalize(rec);
        const float vel[3]{6.0f * rnd() * kWuPerM, 18.0f * rnd() * kWuPerM, 3.0f * rnd() * kWuPerM};
        float q[3]{};
        seat_upstream::StockQuery(rec, vel, g, q);
        fit.Add(rec, q, vel, seat_upstream::kGainMinCrossWuPerS, seat_upstream::kGainDecay);
    }
    Check(fit.Learned(seat_upstream::kGainMinWeight), "learned after a strafe");
    Check(std::fabs(fit.Gain() / g - 1.0f) < 1e-3f, "the fit recovers the game's gain");

    // At rest no sample informs it: none used, nothing learned.
    seat_upstream::GainFit still;
    const float rec[3]{1, 0, 0}, q[3]{1, 0, 0}, slow[3]{0.0f, 1.0f * kWuPerM, 0.0f};
    for (int i = 0; i < 50; ++i)
        still.Add(rec, q, slow, seat_upstream::kGainMinCrossWuPerS, seat_upstream::kGainDecay);
    Check(!still.Learned(seat_upstream::kGainMinWeight) && still.used == 0 && still.ignored == 50,
          "slow samples (under 2 m/s across) carry no information");

    // A query that ignores velocity (a passenger whose aim is the record) fits g ~ 0.
    seat_upstream::GainFit none;
    const float fastAcross[3]{0.0f, 15.0f * kWuPerM, 0.0f};
    for (int i = 0; i < 60; ++i)
        none.Add(rec, rec, fastAcross, seat_upstream::kGainMinCrossWuPerS, seat_upstream::kGainDecay);
    Check(none.Learned(seat_upstream::kGainMinWeight) && std::fabs(none.Gain()) < 1e-7f, "no velocity term fits 0");

    // A weapon change re-learns: the recent answers dominate after ~1/(1 - decay) samples.
    seat_upstream::GainFit change;
    const float g2 = 1.0f / (440.0f * kWuPerM);
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < 300; ++i)
        {
            float r2[3]{1.0f, 0.2f * rnd(), 0.1f * rnd()};
            seat_upstream::Normalize(r2);
            const float vel[3]{0.0f, 16.0f * kWuPerM * (i % 2 ? 1.0f : -1.0f), 0.0f};
            float q2[3]{};
            seat_upstream::StockQuery(r2, vel, pass ? g2 : g, q2);
            change.Add(r2, q2, vel, seat_upstream::kGainMinCrossWuPerS, seat_upstream::kGainDecay);
        }
    Check(std::fabs(change.Gain() / g2 - 1.0f) < 0.01f, "a new gain replaces the old within a few hundred samples");
}

void TestWriteRing()
{
    seat_upstream::WriteRing ring;
    const float a[3]{1, 0, 0}, b[3]{0, 1, 0}, c[3]{0, 0, 1};
    Check(!ring.InEffect(10, 7), "empty: nothing in effect");
    ring.Note(10, 7, a);
    Check(!ring.InEffect(10, 7), "a write no tick has consumed is not in effect");
    Check(ring.InEffect(11, 7) && ring.InEffect(11, 7)->dir[0] == 1.0f, "consumed by the next tick");
    ring.Note(11, 7, b);   // a pass with no tick in between...
    ring.Note(11, 7, c);   // ...then another write at the same count
    Check(ring.InEffect(11, 7)->dir[0] == 1.0f, "writes of a pass with no tick yet leave the older one in effect");
    Check(ring.InEffect(12, 7)->dir[2] == 1.0f, "after a tick the newest write is in effect");
    Check(!ring.InEffect(12, 8), "another seat's write is never this seat's");
    ring.Note(12, 7, nullptr);   // a stick call that wrote nothing (disarmed, unproven axis): the record is unknown
    Check(!ring.InEffect(13, 7), "a call that left a record we did not write blocks the pairing");
    ring.Note(13, 7, a);
    Check(ring.InEffect(14, 7) && ring.InEffect(14, 7)->dir[0] == 1.0f, "the next write pairs again");
    // Wrap-safe tick counts.
    seat_upstream::WriteRing wrap;
    wrap.Note(0xFFFFFFFFu, 3, a);
    Check(wrap.InEffect(0u, 3) != nullptr, "the tick counter's wrap is handled");
}

seat_upstream::SeatRecomposeInput HornetInput()
{
    seat_upstream::SeatRecomposeInput in{};
    in.enabled = true;
    in.seated = true;
    in.intentRead = true;
    in.generation = 5;
    in.nowUs = 1'000'000;
    in.maxAgeUs = 150'000;
    shot_lag::IntentPoint& p = in.intent;
    p.generation = 5;
    p.stampUs = 990'000;
    p.seated = true;
    // P 53 m ahead of a render eye at the origin; the solve published the angles from that eye.
    const float point[3]{53.0f * kWuPerM, 0.0f, 0.0f};
    std::memcpy(in.point, point, sizeof(point));
    p.solvedYaw = 0.0f;
    p.solvedPitch = 0.0f;
    in.setYaw = 0.0f;
    in.setPitch = 0.0f;
    // The tick's camera 0.8 m ahead and 0.5 m right of the render eye, strafing at 17 m/s.
    in.cameraRead = true;
    in.camera[0] = 0.8f * kWuPerM;
    in.camera[1] = -0.5f * kWuPerM;
    in.camera[2] = 0.0f;
    in.cameraVel[1] = 17.0f * kWuPerM;
    in.cameraVelValid = true;
    in.tickS = 1.0f / 60.0f;
    in.gain = 1.0f / (190.0f * kWuPerM);
    return in;
}

void TestRecomposeSeat()
{
    seat_upstream::SeatRecomposeInput in = HornetInput();
    float yaw = 0.0f, pitch = 0.0f, bend = 0.0f;
    Check(seat_upstream::RecomposeSeat(in, yaw, pitch, bend) == seat_upstream::SeatRecompose::Recomposed,
          "a fresh seated intent with this pass's camera recomposes");
    // The stock query of the new record, from the camera a tick on, passes through P.
    float rec[3]{}, q[3]{};
    seat_upstream::DirFromAngles(yaw, pitch, rec);
    seat_upstream::StockQuery(rec, in.cameraVel, in.gain, q);
    const float cam[3]{in.camera[0], in.camera[1] + in.cameraVel[1] * in.tickS, in.camera[2]};
    Check(LineMissWu(cam, q, in.point) * shot_lag::kMmPerWu < 1.0f, "the stock line from the tick's camera hits P");
    // Before: the published angles' stock line missed by metres (the Hornet's 3-4 m at 53 m).
    float set[3]{}, q0[3]{};
    seat_upstream::DirFromAngles(in.setYaw, in.setPitch, set);
    seat_upstream::StockQuery(set, in.cameraVel, in.gain, q0);
    Check(LineMissWu(cam, q0, in.point) * shot_lag::kMetresPerWu > 3.0f, "the published angles missed by metres");
    Check(bend > 4.0f && bend < 7.0f, "the bend is the gain's ~5 deg plus the camera offset");

    // With no learned gain the camera alone is still solved from: the identity model's line hits P.
    in.gain = 0.0f;
    Check(seat_upstream::RecomposeSeat(in, yaw, pitch, bend) == seat_upstream::SeatRecompose::Recomposed,
          "unlearned: still recomposed from the camera");
    seat_upstream::DirFromAngles(yaw, pitch, rec);
    Check(LineMissWu(cam, rec, in.point) * shot_lag::kMmPerWu < 1.0f, "unlearned: the record's own line hits P");

    // Fail closed, each reason, and nothing written.
    const auto refuses = [](seat_upstream::SeatRecomposeInput i, seat_upstream::SeatRecompose why, const char* what) {
        float y = 7.0f, p = 7.0f, b = 7.0f;
        Check(seat_upstream::RecomposeSeat(i, y, p, b) == why && y == 7.0f && p == 7.0f && b == 7.0f, what);
    };
    auto i = HornetInput();
    i.enabled = false;
    refuses(i, seat_upstream::SeatRecompose::Off, "off: kept");
    i = HornetInput();
    i.seated = false;
    refuses(i, seat_upstream::SeatRecompose::NotSeated, "not seated: kept");
    i = HornetInput();
    i.gunSlaved = true;
    refuses(i, seat_upstream::SeatRecompose::GunSlaved, "a gun-slaved seat: kept (the gun's line is not modelled)");
    i = HornetInput();
    i.intentRead = false;
    refuses(i, seat_upstream::SeatRecompose::NoIntent, "no intent: kept");
    i = HornetInput();
    i.setYaw = 0.01f;
    refuses(i, seat_upstream::SeatRecompose::OtherSolve, "a setpoint from another author: kept");
    i = HornetInput();
    i.intent.stampUs = 800'000;
    refuses(i, seat_upstream::SeatRecompose::PointUnusable, "a stale point: kept");
    i = HornetInput();
    i.intent.seated = false;
    refuses(i, seat_upstream::SeatRecompose::PointUnusable, "an on-foot point: kept");
    i = HornetInput();
    i.intent.railHeld = true;
    refuses(i, seat_upstream::SeatRecompose::PointUnusable, "held at the pitch rail: kept");
    i = HornetInput();
    i.intent.generation = 4;
    refuses(i, seat_upstream::SeatRecompose::PointUnusable, "another generation: kept");
    i = HornetInput();
    i.cameraRead = false;
    refuses(i, seat_upstream::SeatRecompose::NoCamera, "no camera this pass: kept");
    i = HornetInput();
    i.point[0] = i.camera[0] + 0.1f * kWuPerM;
    i.point[1] = i.camera[1] + i.cameraVel[1] * i.tickS;
    refuses(i, seat_upstream::SeatRecompose::PointTooClose, "P within 30 cm: kept");
    i = HornetInput();
    i.point[0] = -10.0f * kWuPerM;
    refuses(i, seat_upstream::SeatRecompose::PointBehind, "P behind: kept");
    i = HornetInput();
    i.cameraVel[1] = 300.0f * kWuPerM;
    i.point[1] = 0.0f;
    refuses(i, seat_upstream::SeatRecompose::NoSolution, "an unreachable query: kept");

    // No magnitude limit: a 25 deg bend (a camera far off the solve's eye) is recomposed like any other.
    i = HornetInput();
    i.gain = 0.0f;
    i.camera[1] = -20.0f * kWuPerM;
    i.camera[0] = 10.0f * kWuPerM;
    Check(seat_upstream::RecomposeSeat(i, yaw, pitch, bend) == seat_upstream::SeatRecompose::Recomposed && bend > 20.0f,
          "no size limit on the bend");
}

void TestStockLineWindow()
{
    const float start[3]{0, 0, 0}, vector[3]{1, 0, 0};
    const float point[3]{50.0f * kWuPerM, 1.0f * kWuPerM, 0.0f};
    const float vel[3]{0.0f, -10.0f * kWuPerM, 0.0f};
    const auto s = seat_upstream::MeasureStockLine(start, vector, point, vel);
    Check(s.valid && std::fabs(s.missMm - 1000.0f) < 1.0f, "the miss of P off the stock line");
    Check(std::fabs(s.angleDeg - std::atan(1.0f / 50.0f) * shot_lag::kRadToDeg) < 0.01f, "its angle at the start");
    Check(s.travel && std::fabs(s.alongMm - 1000.0f) < 1.0f,
          "split along the travel (+ = the line runs ahead of P along the travel, shot_lag's convention)");
    seat_upstream::StockLineWindow w;
    w.tag = 1;
    w.Add(s);
    w.Add(s);
    char line[1024]{};
    Check(w.Format("Halo 3", "hornet driver", line, sizeof(line)) > 0 && std::strstr(line, "mean 1000 mm") &&
              std::strstr(line, "SHOT UPSTREAM Halo 3 seat hornet driver"),
          "the dev line reads the stock line's miss");
    seat_upstream::StockLineWindow empty;
    Check(empty.Format("Halo 3", "x", line, sizeof(line)) == 0, "an empty window prints nothing");
    // An unmodded host's chase line, measured beside ours (a stock third-person seat).
    const float chaseStart[3]{-4.0f * kWuPerM, 0.0f, 1.5f * kWuPerM};
    w.AddHost(seat_upstream::MeasureStockLine(chaseStart, vector, point, nullptr));
    w.hostUnreproducible = 3;
    Check(w.Format("Halo 3", "hornet 0 (driver)", line, sizeof(line)) > 0 &&
              std::strstr(line, "an UNMODDED host's chase line") && std::strstr(line, "over 1") &&
              std::strstr(line, "3 queries of a stock third-person seat"),
          "the dev line carries the unmodded host's chase line and the unreproducible count");
}

// A stock seat's line start (section 4b): the choice, the carried query start, the ask's agreement with the game,
// and the claim the solve rests on -- solved each step against the camera the last record produced, the error
// shrinks by R / (L + R).
void TestStockStart()
{
    using seat_upstream::StockStart;
    seat_upstream::StockStartInput in{};
    Check(seat_upstream::ChooseStockStart(in).source == StockStart::None, "nothing in hand: no start");

    in.query = true;
    in.queryPos[0] = 10.0f * kWuPerM;
    in.queryAgeUs = 100000;
    auto c = seat_upstream::ChooseStockStart(in);
    Check(c.source == StockStart::Query && !c.velValid && c.pos[0] == in.queryPos[0],
          "a query start with no seat velocity is used where it was");
    in.seatVelValid = true;
    in.seatVel[1] = 10.0f * kWuPerM;
    c = seat_upstream::ChooseStockStart(in);
    Check(c.source == StockStart::Query && c.velValid && std::fabs(c.pos[1] - 1.0f * kWuPerM) < 1.0e-4f &&
              c.vel[1] == in.seatVel[1],
          "a 100 ms old query start in a seat moving 10 m/s is carried 1 m");
    in.queryAgeUs = seat_upstream::kStockQueryMaxAgeUs + 1;
    Check(seat_upstream::ChooseStockStart(in).source == StockStart::None, "a query start past its age is refused");
    in.queryAgeUs = 100000;
    in.queryPos[2] = std::nanf("");
    Check(seat_upstream::ChooseStockStart(in).source == StockStart::None, "a non-finite query start is refused");
    in.queryPos[2] = 0.0f;

    in.asked = true;
    in.askedPos[0] = 20.0f * kWuPerM;
    c = seat_upstream::ChooseStockStart(in);
    Check(c.source == StockStart::Asked && c.pos[0] == in.askedPos[0] && !c.velValid,
          "the asked camera comes before the query's start");
    in.askedVelValid = true;
    in.askedVel[0] = std::nanf("");
    Check(!seat_upstream::ChooseStockStart(in).velValid, "a non-finite velocity is no velocity");
    in.askedVel[0] = 3.0f;
    Check(seat_upstream::ChooseStockStart(in).velValid, "...and a finite one is carried");

    in.evaluator = true;
    in.evaluatorPos[0] = 30.0f * kWuPerM;
    in.evaluatorVelValid = true;
    c = seat_upstream::ChooseStockStart(in);
    Check(c.source == StockStart::Evaluator && c.pos[0] == in.evaluatorPos[0] && c.velValid,
          "this pass's own camera comes first");
    in.evaluatorPos[1] = std::nanf("");
    Check(seat_upstream::ChooseStockStart(in).source == StockStart::Asked, "a non-finite one falls through");

    // The ask against the game's own query LINE. The query starts some metres along the camera's line, so the two
    // points are far apart when the camera is exactly where the game's is.
    {
        seat_upstream::AskAgreement agreement;
        Check(agreement.Usable() && agreement.Lead() == 0.0f, "never compared: the ask is used, with no lead");
        const float camera[3]{0.0f, 0.0f, 0.0f}, forward[3]{1.0f, 0.0f, 0.0f};
        const float onLine[3]{6.4f * kWuPerM, 0.0f, 0.0f};                      // 6.4 m ahead, on the line
        const float nearLine[3]{6.4f * kWuPerM, 0.4f * kWuPerM, 0.0f};          // 0.4 m aside
        const float offLine[3]{6.4f * kWuPerM, 0.0f, 3.0f * kWuPerM};           // 3 m aside
        auto reading = agreement.Note(camera, forward, onLine, forward);
        Check(reading.compared && seat_upstream::AskAgreement::Agrees(reading) && reading.asideWu < 1.0e-4f &&
                  std::fabs(reading.aheadWu - 6.4f * kWuPerM) < 1.0e-4f,
              "a camera 6.4 m behind the query's start, on its line, agrees");
        Check(std::fabs(agreement.Lead() - 6.4f * kWuPerM) < 1.0e-4f, "...and the lead is how far ahead the query starts");
        reading = agreement.Note(camera, forward, nearLine, forward);
        Check(seat_upstream::AskAgreement::Agrees(reading) && std::fabs(reading.asideWu - 0.4f * kWuPerM) < 1.0e-4f,
              "40 cm aside is on the line");
        reading = agreement.Note(camera, forward, offLine, forward);
        Check(reading.compared && !seat_upstream::AskAgreement::Agrees(reading) && agreement.Usable(),
              "one comparison off the line proves nothing");
        agreement.Note(camera, forward, offLine, forward);
        Check(agreement.Usable(), "nor do two");
        agreement.Note(camera, forward, offLine, forward);
        Check(!agreement.Usable(), "three in a row: the ask is not the game's camera");
        agreement.Note(camera, forward, onLine, forward);
        Check(agreement.Usable(), "the first one on the line brings it back");
        agreement.Note(camera, forward, offLine, forward);
        agreement.Note(camera, forward, offLine, forward);
        agreement.Note(camera, forward, onLine, forward);
        agreement.Note(camera, forward, offLine, forward);
        agreement.Note(camera, forward, offLine, forward);
        Check(agreement.Usable(), "comparisons off the line that are not in a row do not refuse it");
        // Another occupant shooting at the same target 40 m off from a seat 3 m aside: his line is 4 deg from ours.
        const float partner[3]{std::cos(0.07f), std::sin(0.07f), 0.0f};
        reading = agreement.Note(camera, forward, offLine, partner);
        Check(!reading.compared, "a query along another aim is not this seat's");
        // The aim turned between the query and the ask: no evidence either way.
        const float turned[3]{std::cos(0.1f), std::sin(0.1f), 0.0f};
        const float before = agreement.Lead();
        reading = agreement.Note(camera, turned, offLine, forward);
        Check(!reading.compared && agreement.Usable() && agreement.Lead() == before,
              "a comparison across a turn of the aim is no evidence");
        const float bad[3]{std::nanf(""), 0.0f, 0.0f};
        reading = agreement.Note(camera, forward, bad, forward);
        Check(!reading.compared && !seat_upstream::AskAgreement::Agrees(reading), "a non-finite line is no comparison");
        const float nowhere[3]{};
        reading = agreement.Note(camera, nowhere, onLine, forward);
        Check(!reading.compared, "a camera with no direction is no comparison");
        // The lead follows a change slowly (a quarter of the way a comparison).
        seat_upstream::AskAgreement slow;
        slow.Note(camera, forward, onLine, forward);
        const float further[3]{10.4f * kWuPerM, 0.0f, 0.0f};
        slow.Note(camera, forward, further, forward);
        Check(std::fabs(slow.Lead() - 7.4f * kWuPerM) < 1.0e-3f, "the lead is smoothed");
        agreement.Reset();
        Check(agreement.Usable() && agreement.Lead() == 0.0f, "a new occupant starts unjudged");

        // The asked start carried by the lead.
        seat_upstream::StockStartInput led{};
        led.asked = true;
        led.askedPos[0] = 20.0f * kWuPerM;
        led.askedDir[1] = 2.0f;                         // not unit length
        led.askedLeadWu = 6.0f * kWuPerM;
        Check(seat_upstream::ChooseStockStart(led).pos[1] == 0.0f, "with no point to solve through there is no lead");
        led.pointValid = true;
        led.point[0] = led.askedPos[0];
        led.point[1] = 50.0f * kWuPerM;
        auto start = seat_upstream::ChooseStockStart(led);
        Check(start.source == StockStart::Asked && start.pos[0] == led.askedPos[0] &&
                  std::fabs(start.pos[1] - 3.0f * kWuPerM) < 1.0e-4f,
              "the asked camera is carried along its own direction by half the lead");
        led.point[1] = 4.0f * kWuPerM;
        start = seat_upstream::ChooseStockStart(led);
        Check(std::fabs(start.pos[1] - 2.0f * kWuPerM) < 1.0e-4f,
              "...but never more than half the way to the point");
        led.point[1] = -8.0f * kWuPerM;
        Check(seat_upstream::ChooseStockStart(led).pos[1] == 0.0f, "a point behind the camera: no lead");
        led.point[1] = 50.0f * kWuPerM;
        led.askedLeadWu = -3.0f * kWuPerM;
        Check(seat_upstream::ChooseStockStart(led).pos[1] == 0.0f, "a query that starts behind the camera: no lead");
        led.askedLeadWu = 6.0f * kWuPerM;
        led.askedDir[1] = 0.0f;
        Check(seat_upstream::ChooseStockStart(led).pos[1] == 0.0f, "with no direction it stays at the camera");
    }

    // The limit keeps every arm converging. A lead of four camera arms would start the solve an arm past the pivot;
    // with the point nearer the pivot than that the error would grow each step. Limited to half the way to the
    // point, it shrinks every step.
    {
        const float R = 6.0f * kWuPerM, L = 5.0f * kWuPerM;
        const float pivot[3]{};
        const float exact[3]{1.0f, 0.0f, 0.0f};
        const float point[3]{L, 0.0f, 0.0f};
        float yaw = 15.0f / shot_lag::kRadToDeg, pitch = 0.0f;
        float errorDeg = 15.0f;
        bool shrinking = true;
        for (int pass = 0; pass < 6; ++pass)
        {
            float record[3]{};
            seat_upstream::DirFromAngles(yaw, pitch, record);
            seat_upstream::StockStartInput from{};
            from.asked = true;
            from.pointValid = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                from.askedPos[axis] = pivot[axis] - record[axis] * R;
                from.askedDir[axis] = record[axis];
                from.point[axis] = point[axis];
            }
            from.askedLeadWu = 4.0f * R;
            const auto origin = seat_upstream::ChooseStockStart(from);
            seat_upstream::SeatRecomposeInput step = HornetInput();
            step.gain = 0.0f;
            step.cameraVelValid = false;
            std::memcpy(step.point, point, sizeof(point));
            std::memcpy(step.camera, origin.pos, sizeof(step.camera));
            step.setYaw = step.intent.solvedYaw = yaw;
            step.setPitch = step.intent.solvedPitch = pitch;
            float bend = 0.0f;
            if (seat_upstream::RecomposeSeat(step, yaw, pitch, bend) != seat_upstream::SeatRecompose::Recomposed)
            {
                shrinking = false;
                break;
            }
            seat_upstream::DirFromAngles(yaw, pitch, record);
            const float now = seat_upstream::AngleDeg(record, exact);
            shrinking = shrinking && now < errorDeg;
            errorDeg = now;
        }
        Check(shrinking && errorDeg < 0.05f,
              "a lead past the pivot and a point beside the vehicle: the limited lead still converges");
    }

    // The fixed point with the lead: a query that starts at the pivot. Each step closes more of the error than a
    // step from the camera does, and from the same side (the record never swings past the answer).
    {
        const float R = 6.5f * kWuPerM, L = 5.0f * kWuPerM;
        const float pivot[3]{1.0f * kWuPerM, -2.0f * kWuPerM, 0.5f * kWuPerM};
        const float exact[3]{0.8f, 0.6f, 0.0f};
        const float point[3]{pivot[0] + exact[0] * L, pivot[1] + exact[1] * L, pivot[2] + exact[2] * L};
        seat_upstream::SeatRecomposeInput step = HornetInput();
        step.gain = 0.0f;
        step.cameraVelValid = false;
        std::memcpy(step.point, point, sizeof(point));
        float setYaw = 0.0f, setPitch = 0.0f;
        Check(seat_upstream::AnglesFromDir(exact, setYaw, setPitch), "the exact line has angles");
        setYaw += 20.0f / shot_lag::kRadToDeg;
        step.setYaw = step.intent.solvedYaw = setYaw;
        step.setPitch = step.intent.solvedPitch = setPitch;
        float record[3]{};
        seat_upstream::DirFromAngles(setYaw, setPitch, record);
        // The camera the record in effect produces, carried R along its own direction: the pivot.
        seat_upstream::StockStartInput from{};
        from.asked = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            from.askedPos[axis] = pivot[axis] - record[axis] * R;
            from.askedDir[axis] = record[axis];
        }
        from.askedLeadWu = R;
        from.pointValid = true;
        std::memcpy(from.point, point, sizeof(point));
        const auto origin = seat_upstream::ChooseStockStart(from);
        std::memcpy(step.camera, origin.pos, sizeof(step.camera));
        float yaw = 0.0f, pitch = 0.0f, bend = 0.0f;
        Check(seat_upstream::RecomposeSeat(step, yaw, pitch, bend) == seat_upstream::SeatRecompose::Recomposed,
              "the step from the led start recomposes");
        float led[3]{};
        seat_upstream::DirFromAngles(yaw, pitch, led);
        // The same step from the camera itself.
        std::memcpy(step.camera, from.askedPos, sizeof(step.camera));
        Check(seat_upstream::RecomposeSeat(step, yaw, pitch, bend) == seat_upstream::SeatRecompose::Recomposed,
              "the step from the camera recomposes");
        float plain[3]{};
        seat_upstream::DirFromAngles(yaw, pitch, plain);
        const float ledDeg = seat_upstream::AngleDeg(led, exact), plainDeg = seat_upstream::AngleDeg(plain, exact);
        Check(ledDeg < plainDeg * 0.7f && plainDeg < 20.0f, "the led start closes more of the error in a step");
        // Same side: the led record lies between the one in effect and the exact line.
        Check(seat_upstream::AngleDeg(record, led) < 20.0f && seat_upstream::AngleDeg(record, led) + ledDeg < 20.01f,
              "...and does not swing past the answer");
    }

    // The fixed point. A chase camera R behind a pivot along the record; the point L from the pivot.
    const float R = 2.0f * kWuPerM, L = 30.0f * kWuPerM;
    const float pivot[3]{1.0f * kWuPerM, -2.0f * kWuPerM, 0.5f * kWuPerM};
    float exact[3]{0.8f, 0.6f, 0.0f};
    const float point[3]{pivot[0] + exact[0] * L, pivot[1] + exact[1] * L, pivot[2] + exact[2] * L};
    seat_upstream::SeatRecomposeInput step = HornetInput();
    step.gain = 0.0f;
    step.cameraVelValid = false;
    std::memcpy(step.point, point, sizeof(point));
    // The published setpoint (what the solve from the render eye asked for): 10 degrees off the exact line.
    float setYaw = 0.0f, setPitch = 0.0f;
    Check(seat_upstream::AnglesFromDir(exact, setYaw, setPitch), "the exact line has angles");
    setYaw += 10.0f / shot_lag::kRadToDeg;
    step.setYaw = step.intent.solvedYaw = setYaw;
    step.setPitch = step.intent.solvedPitch = setPitch;
    float record[3]{};
    seat_upstream::DirFromAngles(setYaw, setPitch, record);
    float error = seat_upstream::AngleDeg(record, exact), firstRatio = 0.0f;
    bool everyStep = true;
    for (int k = 0; k < 4; ++k)
    {
        // The camera the record in effect produces; the next record is solved from it.
        for (int axis = 0; axis < 3; ++axis)
            step.camera[axis] = pivot[axis] - record[axis] * R;
        float yaw = 0.0f, pitch = 0.0f, bend = 0.0f;
        everyStep = everyStep &&
            seat_upstream::RecomposeSeat(step, yaw, pitch, bend) == seat_upstream::SeatRecompose::Recomposed;
        seat_upstream::DirFromAngles(yaw, pitch, record);
        const float next = seat_upstream::AngleDeg(record, exact);
        if (k == 0)
            firstRatio = next / error;
        error = next;
    }
    Check(everyStep, "every step of the fixed point recomposes");
    Check(std::fabs(firstRatio - R / (L + R)) < 0.01f, "one step shrinks the error by R / (L + R)");
    // From the camera the last record produces, its own line passes within a few millimetres of the point.
    const float camera[3]{pivot[0] - record[0] * R, pivot[1] - record[1] * R, pivot[2] - record[2] * R};
    Check(LineMissWu(camera, record, point) * shot_lag::kMmPerWu < 5.0f, "four steps on, the stock line hits the point");
}

void TestStockCamera()
{
    using seat_upstream::StockCamera;
    Check(seat_upstream::StockCameraFor(0u) == StockCamera::SameAsHere, "a first-person seat: the same camera");
    Check(seat_upstream::StockCameraFor(1u << 7) == StockCamera::SameAsHere, "gun-slaved alone: not third person");
    Check(seat_upstream::StockCameraFor(1u << 4) == StockCamera::NotReproducible,
          "third person by bit 4 alone: the chase path here would read the unit's own track");
    Check(seat_upstream::StockCameraFor((1u << 4) | 1u) == StockCamera::Chase, "bit 0 keeps the seat's track");
    Check(seat_upstream::StockCameraFor((1u << 4) | (1u << 2)) == StockCamera::Chase, "bit 2 keeps the seat's track");
}
} // namespace

int main()
{
    TestModelAndInverse();
    TestGainFit();
    TestWriteRing();
    TestRecomposeSeat();
    TestStockLineWindow();
    TestStockStart();
    TestStockCamera();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
