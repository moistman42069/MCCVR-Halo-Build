// Halo 4 seated convergence (src/common/halo4_seat_converge_logic.h): whose camera answers the sampled call, the
// solve's start and its carry, the fixed point over a model of the game's following camera and the rate it is
// reached at, the start held across a gap in the samples, the reasons a poll does not solve, and the witness's
// line-against-point measure.
#include "halo4_seat_converge_logic.h"

#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

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

using namespace halo4_seat_converge;
constexpr float kDeg = 0.017453293f;
constexpr float kM = shot_lag::kWuPerMetre;   // one metre in world units

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

struct V
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Len(V a) { return std::sqrt(Dot(a, a)); }
V Unit(V a)
{
    const float l = Len(a);
    return l > 0.0f ? a * (1.0f / l) : a;
}
float AngleRad(V a, V b)
{
    const V c{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return std::atan2(Len(c), Dot(a, b));
}
void Put(V a, float out[3])
{
    out[0] = a.x;
    out[1] = a.y;
    out[2] = a.z;
}
V Get(const float in[3]) { return {in[0], in[1], in[2]}; }

// THE GAME'S FOLLOWING CAMERA, as 0x293A84 builds it with no camera track: the camera sits `distance` behind the
// pivot along the aim, and the aim view (0x1DC9E4) hands out, beside the camera, a trace start pushed along the
// line to the plane through the controlled unit's origin that faces the aim flattened against the unit's up,
// plus 0.5 wu.
struct Chase
{
    V pivot{};
    float distance = 0.0f;
    V unitOrigin{};
    V up{0.0f, 0.0f, 1.0f};
    bool push = true;

    CameraSample View(V aim, uint64_t stampUs = 1000000) const
    {
        const V d = Unit(aim);
        const V camera = pivot - d * distance;
        V eye = camera;
        if (push)
        {
            const V flat = d - up * Dot(d, up);
            if (Len(flat) > 1.0e-4f)
            {
                const V n = Unit(flat);
                const float t = Dot(unitOrigin - camera, n) / Dot(d, n);
                if (t > 0.0f)
                    eye = camera + d * (t + 0.5f);
            }
        }
        CameraSample s{};
        s.valid = true;
        s.stampUs = stampUs;
        Put(camera, s.camera);
        Put(d, s.dir);
        Put(eye, s.eye);
        return s;
    }
};

void TestSampledCamera()
{
    Check(SampleWithGameCamera(true, false), "seats converge, one-machine changes not allowed: the game's own camera");
    Check(!SampleWithGameCamera(true, true), "one-machine changes allowed: the override answers, as in the tick");
    Check(!SampleWithGameCamera(false, false), "the key off: the call is left exactly as it was");
    Check(!SampleWithGameCamera(false, true), "the key off, changes allowed: left as it was");
}

void TestCarry()
{
    const float tick = 1.0f / 60.0f;
    Check(Near(CarrySeconds(1000000, 1000000, tick), 2.0f * tick, 1.0e-6f), "a sample read at once: two ticks");
    Check(Near(CarrySeconds(1000000, 1010000, tick), 0.010f + 2.0f * tick, 1.0e-6f), "10 ms old: its age and two ticks");
    Check(Near(CarrySeconds(1000000, 1140000, tick, kSampleHoldUs), kMaxCarryS, 1.0e-6f), "held at the longest carry");
    Check(CarrySeconds(1000000, 1000000 + kSampleFreshUs + 1, tick) < 0.0f, "past the fresh window: not carried");
    {
        // The oldest sample still used is carried its whole age and the ticks to the fire: the cap never cuts it.
        const float oldest = CarrySeconds(1000000, 1000000 + kSampleFreshUs, tick);
        Check(Near(oldest, float(kSampleFreshUs) * 1.0e-6f + kCarryTicks * tick, 1.0e-6f) && oldest < kMaxCarryS,
              "the oldest fresh sample is carried in full, under the longest carry");
    }
    Check(CarrySeconds(1000000, 999999, tick) < 0.0f, "a stamp from the future: refused");
    Check(CarrySeconds(0, 1000000, tick) < 0.0f, "no stamp: refused");
    Check(CarrySeconds(1000000, 1000000, 0.0f) < 0.0f, "no tick period: refused");
    Check(CarrySeconds(1000000, 3000000, tick, kSampleHoldUs) > 0.0f, "a caller's own window is honoured");
}

void TestPushAndStart()
{
    Chase chase{};
    chase.pivot = {10.0f, 5.0f, 2.0f};
    chase.distance = 4.5f * kM;
    chase.unitOrigin = {10.0f, 5.0f, 1.5f};
    const V aim = Unit({1.0f, 0.2f, -0.1f});
    CameraSample s = chase.View(aim);
    const float push = PushAlongLine(s);
    // The plane goes through the unit's origin under the pivot: the camera's distance over the flattened aim's
    // share of the line, then 0.5 wu past it.
    Check(push > chase.distance && Near(push, Len(Get(s.eye) - Get(s.camera)), 1.0e-4f),
          "the push is the eye's distance along the line, past the pivot");

    // No point yet: the start is the camera.
    Start none = SolveStart(s, nullptr, 0.0f);
    Check(none.valid && Near(Len(Get(none.pos) - Get(s.camera)), 0.0f, 1.0e-5f) && none.leadWu == 0.0f,
          "no point: the solve starts at the camera");

    // A far point, the game's trace start 6 m out: half of that would pass the cap, so the cap.
    float far[3]{};
    Put(Get(s.camera) + aim * (60.0f * kM), far);
    Start lead = SolveStart(s, far, 0.0f);
    Check(0.5f * push > kLeadCapWu && lead.valid && Near(lead.leadWu, kLeadCapWu, 1.0e-5f),
          "a far point, a long push: never further along the line than the cap");
    Check(Near(Len(Get(lead.pos) - (Get(s.camera) + aim * kLeadCapWu)), 0.0f, 1.0e-4f), "...along the line");

    // A short push (the trace start 2 m out): half of the way to it.
    CameraSample shortPush = s;
    Put(Get(s.camera) + aim * (2.0f * kM), shortPush.eye);
    Start half = SolveStart(shortPush, far, 0.0f);
    Check(half.valid && Near(half.pushWu, 2.0f * kM, 1.0e-4f) && Near(half.leadWu, 1.0f * kM, 1.0e-4f),
          "a short push: half of the way to the game's trace start");

    // A point nearer than the push: never more than half of the way to it.
    float nearPoint[3]{};
    Put(Get(s.camera) + aim * (1.0f * kM), nearPoint);
    Start capped = SolveStart(shortPush, nearPoint, 0.0f);
    Check(capped.valid && Near(capped.leadWu, 0.5f * kM, 1.0e-4f), "a near point: half of the way to the point");

    // A point behind the camera: no lead.
    float behind[3]{};
    Put(Get(s.camera) - aim * 2.0f, behind);
    Check(SolveStart(s, behind, 0.0f).leadWu == 0.0f, "a point behind the camera: the camera itself");

    // An eye that is not on the camera's line is not the view's push.
    CameraSample off = s;
    off.eye[2] += 0.2f;
    Check(PushAlongLine(off) == 0.0f, "an eye off the line: no push");
    CameraSample back = s;
    Put(Get(s.camera) - aim * 1.0f, back.eye);
    Check(PushAlongLine(back) == 0.0f, "an eye behind the camera: no push");
    CameraSample wild = s;
    Put(Get(s.camera) + aim * (kMaxPushWu + 1.0f), wild.eye);
    Check(PushAlongLine(wild) == 0.0f, "an implausibly long push: none");

    // First person: the eye is the camera.
    chase.push = false;
    CameraSample fp = chase.View(aim);
    Check(PushAlongLine(fp) == 0.0f && SolveStart(fp, far, 0.0f).leadWu == 0.0f,
          "a first-person sample: the solve starts at the eye itself");

    // The carry: along the eye's velocity, only with one.
    s.eyeVel[0] = 6.0f;
    s.eyeVel[1] = -3.0f;
    Check(SolveStart(s, far, 0.05f).carryS == 0.0f, "no valid velocity: not carried");
    s.eyeVelValid = true;
    Start carried = SolveStart(s, far, 0.05f);
    Check(carried.valid && Near(carried.carryS, 0.05f, 1.0e-6f) &&
              Near(carried.pos[0] - lead.pos[0], 0.30f, 1.0e-4f) && Near(carried.pos[1] - lead.pos[1], -0.15f, 1.0e-4f),
          "carried by the eye's velocity for the carry time");
    Check(Near(SolveStart(s, far, 1.0f).carryS, kMaxCarryS, 1.0e-6f), "never carried past the longest carry");
    Check(SolveStart(s, far, -1.0f).carryS == 0.0f, "a refused carry time: not carried");

    CameraSample invalid{};
    Check(!SolveStart(invalid, far, 0.0f).valid, "no sample: no start");
    CameraSample noDir = s;
    noDir.dir[0] = noDir.dir[1] = noDir.dir[2] = 0.0f;
    Check(!SolveStart(noDir, far, 0.0f).valid, "a degenerate direction: no start");
}

// One round trip: the aim written from the sample of the camera `delay` steps back.
struct Loop
{
    Chase chase{};
    V point{};
    bool lead = true;
    bool fromTraceStart = false;   // solve from the view's own trace start (what must NOT be done)
    V Step(V sampledAim) const
    {
        const CameraSample s = chase.View(sampledAim);
        float p[3]{};
        Put(point, p);
        V start{};
        if (fromTraceStart)
            start = Get(s.eye);
        else
        {
            const Start st = SolveStart(s, lead ? p : nullptr, 0.0f);
            start = Get(st.pos);
        }
        return Unit(point - start);
    }
    V Answer() const { return Unit(point - chase.pivot); }
};

void TestFixedPointRate()
{
    Loop loop{};
    loop.chase.pivot = {0.0f, 0.0f, 0.6f};
    loop.chase.distance = 4.5f * kM;
    loop.chase.unitOrigin = {0.0f, 0.0f, 0.0f};
    loop.chase.push = false;   // the camera alone: the eye is the camera
    loop.lead = false;

    // THE RATE from the camera itself: e * D / (L + D), for near and far points.
    for (const float rangeM : {3.0f, 10.0f, 30.0f, 100.0f})
    {
        loop.point = loop.chase.pivot + Unit({1.0f, 0.3f, 0.05f}) * (rangeM * kM);
        const V answer = loop.Answer();
        // Start 2 degrees off, in heading.
        const V right = Unit({-answer.y, answer.x, 0.0f});
        V aim = Unit(answer * std::cos(2.0f * kDeg) + right * std::sin(2.0f * kDeg));
        const float before = AngleRad(aim, answer);
        aim = loop.Step(aim);
        const float after = AngleRad(aim, answer);
        const float expected = ContractionFactor(loop.chase.distance, rangeM * kM);
        char what[160];
        std::snprintf(what, sizeof(what), "from the camera, %.0f m: one step multiplies the error by D / (L + D) = %.3f",
                      double(rangeM), double(expected));
        Check(Near(after / before, expected, 0.01f), what);
    }
    Check(Near(ContractionFactor(4.5f * kM, 30.0f * kM), 0.1304f, 0.001f), "D 4.5 m, L 30 m: 0.13 a step");
    Check(Near(ContractionFactor(4.5f * kM, 3.0f * kM), 0.6f, 0.001f), "D 4.5 m, L 3 m: 0.6 a step");

    // FROM ANY START: 120 degrees off, a near point, it still closes, and every step is nearer than the last.
    loop.point = loop.chase.pivot + Unit({1.0f, 0.0f, 0.0f}) * (3.0f * kM);
    {
        const V answer = loop.Answer();
        V aim = Unit({std::cos(120.0f * kDeg), std::sin(120.0f * kDeg), 0.0f});
        float last = AngleRad(aim, answer);
        bool monotone = true;
        for (int step = 0; step < 40; ++step)
        {
            aim = loop.Step(aim);
            const float e = AngleRad(aim, answer);
            monotone = monotone && e < last + 1.0e-6f;
            last = e;
        }
        Check(monotone, "from 120 degrees off a point 3 m away: the error never grows");
        Check(last < 0.01f * kDeg, "...and it is closed within 40 steps");
    }

    // WITH THE GAME'S OWN TRACE START IN HAND: carried toward it (here to the 2 m cap), and the factor falls
    // and stays positive (the aim approaches from one side: nothing hunts).
    loop.chase.push = true;
    loop.lead = true;
    for (const float rangeM : {3.0f, 10.0f, 30.0f})
    {
        loop.point = loop.chase.pivot + Unit({1.0f, 0.3f, 0.0f}) * (rangeM * kM);
        const V answer = loop.Answer();
        const V right = Unit({-answer.y, answer.x, 0.0f});
        V aim = Unit(answer * std::cos(2.0f * kDeg) + right * std::sin(2.0f * kDeg));
        const float cameraOnly = ContractionFactor(loop.chase.distance, rangeM * kM);
        const float before = AngleRad(aim, answer);
        const V next = loop.Step(aim);
        const float after = AngleRad(next, answer);
        const bool sameSide = Dot(next - answer, right) > 0.0f;
        const float expected = ContractionFactor(loop.chase.distance - kLeadCapWu, rangeM * kM);
        char what[160];
        std::snprintf(what, sizeof(what), "with the lead, %.0f m: (D - 2 m) / (L + D - 2 m) = %.3f, under the "
                      "camera's %.3f, same side", double(rangeM), double(expected), double(cameraOnly));
        Check(Near(after / before, expected, 0.01f) && expected < cameraOnly * 0.8f && sameSide, what);
    }

    // WHY NOT THE TRACE START ITSELF: it lies past the pivot (0.5 wu beyond the unit's plane), so each step
    // lands on the other side, and at short range it does not close.
    loop.fromTraceStart = true;
    loop.point = loop.chase.pivot + Unit({1.0f, 0.0f, 0.0f}) * (3.0f * kM);
    {
        const V answer = loop.Answer();
        const V right = Unit({-answer.y, answer.x, 0.0f});
        V aim = Unit(answer * std::cos(2.0f * kDeg) + right * std::sin(2.0f * kDeg));
        const float before = AngleRad(aim, answer);
        const V next = loop.Step(aim);
        const float after = AngleRad(next, answer);
        Check(Dot(next - answer, right) < 0.0f, "from the game's trace start: the aim lands on the other side");
        Check(after / before > 0.8f, "...and at 3 m it barely closes: the gun would hunt");
    }
    loop.fromTraceStart = false;

    // A LOOP THAT ANSWERS TWO STEPS LATE (the write is consumed a tick later, the sample a pass after that) still
    // closes, from the camera and with the lead.
    for (const bool lead : {false, true})
    {
        loop.lead = lead;
        loop.chase.push = lead;
        loop.point = loop.chase.pivot + Unit({0.7f, -0.7f, 0.1f}) * (5.0f * kM);
        const V answer = loop.Answer();
        std::vector<V> aims(3, Unit({0.0f, 1.0f, 0.0f}));   // 45+ degrees off
        float worst = 0.0f;
        for (int step = 0; step < 90; ++step)
        {
            const V next = loop.Step(aims[aims.size() - 3]);
            aims.push_back(next);
            if (step >= 60)
                worst = std::fmax(worst, AngleRad(next, answer));
        }
        Check(worst < 0.01f * kDeg, lead ? "two steps late, with the lead: closed" : "two steps late, from the camera: closed");
    }
}

void TestMovingVehicle()
{
    // The vehicle drives across the aim at 20 m/s. The sample shows the simulation one tick late (the
    // interpolated pivot); the aim written from it is consumed one tick after the sample. Carried two ticks, the
    // fire's own line passes through the point; carried one tick short, the witness reads the missing tick.
    const float tick = 1.0f / 60.0f;
    const V velocity{0.0f, 20.0f * kM, 0.0f};
    Chase chase{};
    chase.distance = 4.5f * kM;
    const V point{30.0f * kM, 0.0f, 0.5f};
    const auto pivotAt = [&](float t) { return V{0.0f, 0.0f, 0.6f} + velocity * t; };
    for (const float carryTicks : {2.0f, 1.0f})
    {
        V aim = Unit({1.0f, 0.0f, 0.0f});
        LineMiss miss{};
        for (int k = 0; k < 60; ++k)
        {
            const float now = float(k) * tick;
            // The sample: the state one tick ago, with the aim in effect then.
            chase.pivot = pivotAt(now - tick);
            chase.unitOrigin = chase.pivot - V{0.0f, 0.0f, 0.6f};
            CameraSample s = chase.View(aim);
            Put(velocity, s.eyeVel);
            s.eyeVelValid = true;
            float p[3]{};
            Put(point, p);
            // The lane's own carry for a sample read at once (CarrySeconds), or one tick: a tick short of it.
            const float carry = carryTicks == 2.0f ? CarrySeconds(1000000, 1000000, tick) : carryTicks * tick;
            const Start start = SolveStart(s, p, carry);
            const V written = Unit(point - Get(start.pos));
            // The fire: the next tick's state, along the written aim.
            chase.pivot = pivotAt(now + tick);
            chase.unitOrigin = chase.pivot - V{0.0f, 0.0f, 0.6f};
            const CameraSample fire = chase.View(written);
            float v[3]{};
            Put(velocity, v);
            miss = MeasureLine(fire.camera, fire.dir, p, v);
            aim = written;
        }
        if (carryTicks == 2.0f)
        {
            // What is left is the fixed point following an answer that turns as the vehicle passes the point
            // (0.64 deg a tick here): a few hundredths of a degree.
            Check(miss.valid && miss.mm < 20.0f && miss.deg < 0.03f,
                  "carried as the lane carries it: the fire's own line passes within 2 cm of a point 30 m away");
            Check(miss.travel && std::fabs(miss.impliedMs) < 1.0f, "...and the witness reads no time left over");
        }
        else
        {
            // One tick short at 20 m/s: the fire's camera is a third of a metre further along than the solve
            // carried it, the line is parallel to the one meant, and the point is left behind it by that travel's
            // part across the aim.
            Check(miss.valid && miss.mm > 200.0f && miss.mm < 400.0f,
                  "carried one tick short: the line misses by that tick of travel across the aim");
            Check(miss.travel && miss.alongMm < 0.0f && miss.impliedMs < -9.0f && miss.impliedMs > -17.0f,
                  "...and the witness says so: negative, most of a tick (its part across the aim)");
        }
    }
}

void TestStartHold()
{
    StartHold hold{};
    float out[3]{};
    const float head[3]{1.0f, 2.0f, 3.0f}, start[3]{0.0f, 2.5f, 3.4f};
    const float ahead[3]{1.0f, 0.0f, 0.0f};
    const auto turned = [](float deg, float v[3]) {
        v[0] = std::cos(deg * kDeg);
        v[1] = std::sin(deg * kDeg);
        v[2] = 0.0f;
    };
    float quarter[3]{}, four[3]{}, six[3]{};
    turned(90.0f, quarter);
    turned(4.0f, four);
    turned(6.0f, six);

    // A FIRST-PERSON EYE's start: held through any turn.
    Check(!hold.Get(1000, head, ahead, out), "nothing held yet");
    hold.Note(1000, start, head, ahead, false);
    Check(hold.Get(1000, head, ahead, out) && Near(out[0], 0.0f, 1e-6f) && Near(out[1], 2.5f, 1e-6f),
          "held: the start itself");
    const float moved[3]{11.0f, 2.0f, 3.0f};
    Check(hold.Get(1000 + 500000, moved, ahead, out) && Near(out[0], 10.0f, 1e-5f) && Near(out[2], 3.4f, 1e-5f),
          "half a second on, the vehicle 10 wu further: the start rode with the head");
    Check(hold.Get(1000 + 500000, moved, quarter, out), "an eye's start is held through a quarter turn of the hand");
    Check(!hold.Get(1000 + kSampleHoldUs + 1, moved, ahead, out), "past the hold: nothing");
    Check(!hold.Get(999, moved, ahead, out), "a clock running backwards: nothing");
    hold.Reset();
    Check(!hold.Get(2000, head, ahead, out), "reset: nothing");
    const float bad[3]{std::nanf(""), 0.0f, 0.0f};
    hold.Note(3000, bad, head, ahead, false);
    Check(!hold.have, "a non-finite start is never held");
    hold.Note(3000, start, head, bad, false);
    Check(!hold.have, "nor one taken with no direction of the hand");

    // A FOLLOWING CAMERA's start: held only while the hand still points where it did.
    hold.Note(4000, start, head, ahead, true);
    Check(hold.Get(4000 + 500000, moved, four, out) && Near(out[0], 10.0f, 1e-5f),
          "a following camera's start: held while the hand has turned 4 degrees");
    Check(!hold.Get(4000 + 500000, moved, six, out), "...and given up once it has turned 6");
    Check(!hold.Get(4000 + 500000, moved, nullptr, out) && !hold.Get(4000 + 500000, moved, bad, out),
          "...or when where the hand points is not known");
    Check(hold.Get(4000 + 900000, moved, ahead, out), "...and held again when the hand comes back");

    // WHY: the model's following camera, the hand turned t degrees with no sample since. The aim solved from the
    // held start is off by about atan(r sin t / (L + r cos t)); at the limit that is under a degree for a point
    // 10 m out, and it would be several at 20.
    Chase chase{};
    chase.pivot = {0.0f, 0.0f, 0.6f};
    chase.distance = 2.5f * kM;
    chase.push = false;
    const auto offAfterTurn = [&](float deg) {
        const V before = Unit({1.0f, 0.0f, 0.0f});
        const V after = Unit({std::cos(deg * kDeg), std::sin(deg * kDeg), 0.0f});
        const V point = chase.pivot + after * (10.0f * kM);
        const V heldStart = chase.pivot - before * chase.distance;   // where the camera was
        return AngleRad(Unit(point - heldStart), after) / kDeg;
    };
    Check(offAfterTurn(kHoldTurnDeg) < 1.1f, "at the turn limit a held following start leaves the aim about a degree off");
    Check(offAfterTurn(20.0f) > 3.5f, "...where 20 degrees would leave it several off: why there is a limit");
}

void TestWhy()
{
    PollFacts f{};
    Check(DecidePoll(f) == Why::AimOff, "nothing holds: the switches first");
    f.aimOn = true;
    Check(DecidePoll(f) == Why::ViewNotOurs, "then the seat view");
    f.viewOurs = true;
    Check(DecidePoll(f) == Why::NoHead, "then the rendered eye");
    f.headKnown = true;
    Check(DecidePoll(f) == Why::TraceOff, "then the trace");
    f.traceArmed = true;
    Check(DecidePoll(f) == Why::NoAimCamera, "then the aim camera's anchors");
    f.anchors = true;
    Check(DecidePoll(f) == Why::NoSample, "then a sample");
    f.start = true;
    Check(DecidePoll(f) == Why::NoControlUpdate, "then the control update");
    f.controlUpdate = true;
    f.traceFaulted = true;
    Check(DecidePoll(f) == Why::TraceFaulted, "a faulted trace");
    f.traceFaulted = false;
    Check(DecidePoll(f) == Why::OtherStance, "then the trace's stance");
    f.stanceOurs = true;
    Check(DecidePoll(f) == Why::Solved, "every fact holds: the bend is tried");

    using converge::ConvergeOff;
    Check(FromBend(ConvergeOff::None) == Why::Solved, "the bend resolved");
    Check(FromBend(ConvergeOff::TraceStale) == Why::NoTrace, "no fresh trace");
    Check(FromBend(ConvergeOff::NoRange) == Why::NoRange, "no range");
    Check(FromBend(ConvergeOff::TraceFaulted) == Why::TraceFaulted, "a faulted trace");
    Check(FromBend(ConvergeOff::AimOff) == Why::AimOff, "the aim switched off under the poll");
    for (unsigned i = 0; i < static_cast<unsigned>(Why::Count); ++i)
    {
        const Why why = static_cast<Why>(i);
        Check(WhyName(why)[0] != '?', "every reason has a name");
        Check((OffReason(why) == ConvergeOff::None) == (why == Why::Solved), "only a solved poll reports no reason");
    }
    Check(OffReason(Why::ViewNotOurs) == ConvergeOff::SeatViewNotOwned, "the view's reason carries over");
}

void TestMeasureLine()
{
    const float start[3]{0.0f, 0.0f, 0.0f};
    const float dir[3]{1.0f, 0.0f, 0.0f};
    const float on[3]{10.0f, 0.0f, 0.0f};
    LineMiss m = MeasureLine(start, dir, on);
    Check(m.valid && Near(m.deg, 0.0f, 1e-3f) && Near(m.mm, 0.0f, 1e-2f) && !m.travel, "a point on the line: no miss");
    Check(Near(m.rangeM, 30.48f, 0.01f), "the range is in metres");

    // The point one degree to the left (+yaw) and level.
    const float left[3]{10.0f * std::cos(1.0f * kDeg), 10.0f * std::sin(1.0f * kDeg), 0.0f};
    m = MeasureLine(start, dir, left);
    Check(m.valid && Near(m.yawDeg, 1.0f, 1e-3f) && Near(m.pitchDeg, 0.0f, 1e-3f) && Near(m.deg, 1.0f, 1e-3f),
          "one degree of heading");
    Check(Near(m.mm, 10.0f * std::sin(1.0f * kDeg) * shot_lag::kMmPerWu, 0.5f), "...and its miss at the point");

    // The point two degrees above.
    const float above[3]{10.0f * std::cos(2.0f * kDeg), 0.0f, 10.0f * std::sin(2.0f * kDeg)};
    m = MeasureLine(start, dir, above);
    Check(m.valid && Near(m.pitchDeg, 2.0f, 1e-3f) && Near(m.yawDeg, 0.0f, 1e-3f), "two degrees of elevation");

    // Heading across the seam.
    const float lineBack[3]{-1.0f, 0.001f, 0.0f}, pointBack[3]{-10.0f, -0.01f, 0.0f};
    m = MeasureLine(start, lineBack, pointBack);
    Check(m.valid && std::fabs(m.yawDeg) < 0.2f, "a heading difference across +-pi wraps");

    // Along the travel: the point 0.1 wu to +y of the line, the start moving +y at 10 m/s.
    const float aside[3]{10.0f, 0.1f, 0.0f};
    const float velocity[3]{0.0f, 10.0f * kM, 0.0f};
    m = MeasureLine(start, dir, aside, velocity);
    Check(m.valid && m.travel && Near(m.alongMm, 304.8f, 0.5f), "the miss along the travel, signed");
    Check(Near(m.impliedMs, 30.48f, 0.1f), "...over the speed: the time it stands for");
    const float slow[3]{0.0f, 0.2f * kM, 0.0f};
    Check(!MeasureLine(start, dir, aside, slow).travel, "under 1 m/s there is no travel to split along");

    const float close[3]{0.05f, 0.0f, 0.0f};
    Check(!MeasureLine(start, dir, close).valid, "a point within 30 cm of the start: no direction");
    const float zero[3]{};
    Check(!MeasureLine(start, zero, on).valid, "a degenerate direction: nothing");
}

void TestRole()
{
    Check(std::string_view(RoleName(false, 0xFF)) == "unread", "unread flags");
    Check(std::string_view(RoleName(true, 0)) == "passenger", "no role bit: a passenger");
    Check(std::string_view(RoleName(true, kFlagDriver)) == "driver", "bit 2: the driver");
    Check(std::string_view(RoleName(true, kFlagGunner)) == "gunner", "bit 3: the gunner");
    Check(std::string_view(RoleName(true, kFlagDriver | kFlagGunner)) == "driver+gunner", "both");
    Check(std::string_view(RoleName(true, kFlagGunner | kFlagCameraOnGun)) == "gunner, camera on the gun", "bit 7");
}

void TestGunCameraLatch()
{
    GunCameraLatch latch{};
    Check(!latch.Turret(), "a new stint: not a turret until the seat's flags have been read");
    latch.Note(true, false, kFlagCameraOnGun);
    Check(!latch.Turret(), "a control update without the flags says nothing");
    latch.Note(false, true, kFlagCameraOnGun);
    Check(!latch.Turret(), "a control update that was not read says nothing");
    latch.Note(true, true, kFlagGunner | kFlagCameraOnGun);
    Check(latch.Turret(), "the flags read with bit 7: a turret");
    latch.Note(false, false, 0);
    Check(latch.Turret(), "a poll whose read collided keeps the turret");
    latch.Note(true, false, 0);
    Check(latch.Turret(), "an update without the flags keeps the turret");
    latch.Note(true, true, kFlagGunner);
    Check(!latch.Turret(), "the flags read without bit 7: no longer a turret");
    latch.Note(true, false, kFlagCameraOnGun);
    Check(!latch.Turret(), "and an update without the flags keeps that too");
    Check(!GunCameraLatch{}.Turret(), "a new stint starts a new latch");
}
} // namespace

int main()
{
    TestSampledCamera();
    TestCarry();
    TestPushAndStart();
    TestFixedPointRate();
    TestMovingVehicle();
    TestStartHold();
    TestWhy();
    TestMeasureLine();
    TestRole();
    TestGunCameraLatch();
    std::printf("halo4_seat_converge_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
