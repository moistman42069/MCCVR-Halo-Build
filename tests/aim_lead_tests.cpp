// Aim lead (src/common/aim_lead_logic.h): the written aim carried ahead by the setpoint's own rate -- a steady
// sweep is led to where it will be, a step, a gap, a stop and an aim at rest are written unchanged, the lead is
// capped, and the noise the lead adds stays inside its stated bound.
#include "aim_lead_logic.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
using namespace aim_lead;

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

constexpr float kDeg = 1.0f / kRadToDeg;   // radians in a degree
constexpr uint64_t kPass90 = 11111;        // microseconds between write passes at 90 a second
constexpr uint64_t kPass60 = 16667;
constexpr uint64_t kPass120 = 8333;
constexpr float kTick = 0.016667f;         // one 60 Hz simulation tick, seconds
constexpr uint64_t kStart = 5000000;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool NearAngle(float a, float b, float tolerance) { return std::fabs(Wrap(a - b)) <= tolerance; }
bool Same(float a, float b) { return a == b; }

// A steady sweep from (yaw0, pitch0) at (yawRate, pitchRate), one new sample per write pass: Step() hands the
// filter the next setpoint, its yaw wrapped into [0, 2*pi) as a record keeps it.
struct Sweep
{
    Filter filter;
    float yaw0 = 1.0f, pitch0 = 0.0f, yawRate = 0.0f, pitchRate = 0.0f;
    uint64_t passUs = kPass90;
    uint64_t us = kStart;
    uint64_t key = 1;
    int pass = 0;
    float fedYaw = 0.0f, fedPitch = 0.0f;   // the last setpoint handed over
    float Yaw() const { return yaw0 + yawRate * float(pass) * float(passUs) * 1.0e-6f; }
    float Pitch() const { return pitch0 + pitchRate * float(pass) * float(passUs) * 1.0e-6f; }
    Result Step(float leadS = kTick)
    {
        float wrapped = std::fmod(Yaw(), kTwoPi);
        if (wrapped < 0.0f)
            wrapped += kTwoPi;
        fedYaw = wrapped;
        fedPitch = Pitch();
        const Result r = filter.Step(us, key, fedYaw, fedPitch, leadS);
        ++pass;
        ++key;
        us += passUs;
        return r;
    }
};

// Uniform noise in [-1, 1): a fixed sequence, so the test is the same on every run.
struct Noise
{
    uint32_t state = 0x2F6E2B1u;
    float Next()
    {
        state = state * 1664525u + 1013904223u;
        return float(static_cast<int32_t>(state)) / 2147483648.0f;
    }
};
} // namespace

int main()
{
    // ---- the setting ---------------------------------------------------------------------------------------
    Check(ClampTicks(1.0f, 0.5f) == 1.0f && ClampTicks(-2.0f, 0.5f) == 0.0f && ClampTicks(9.0f, 0.5f) == kMaxTicks,
          "the setting is held inside 0 to 3 ticks");
    Check(ClampTicks(std::numeric_limits<float>::quiet_NaN(), 0.5f) == 0.5f &&
              ClampTicks(std::numeric_limits<float>::infinity(), 0.5f) == 0.5f,
          "a non-finite setting reads as the fallback");
    Check(LeadSeconds(0.0f, 16667) == 0.0f && LeadSeconds(-1.0f, 16667) == 0.0f &&
              LeadSeconds(std::numeric_limits<float>::quiet_NaN(), 16667) == 0.0f,
          "no lead time when the setting is off, negative or not a number");
    Check(Near(LeadSeconds(1.0f, 16667), 0.016667f, 1.0e-7f) && Near(LeadSeconds(1.5f, 33333), 0.05f, 1.0e-6f),
          "the lead time is the setting times the measured tick");
    Check(Near(LeadSeconds(1.0f, 0), 0.016667f, 1.0e-7f) && Near(LeadSeconds(1.0f, 900), 0.016667f, 1.0e-7f) &&
              Near(LeadSeconds(1.0f, 400000), 0.016667f, 1.0e-7f),
          "an unmeasured tick, or one that is not a tick, reads as 60 Hz");
    Check(Minmod(2.0f, 3.0f) == 2.0f && Minmod(-2.0f, -3.0f) == -2.0f && Minmod(2.0f, -3.0f) == 0.0f &&
              Minmod(0.0f, 3.0f) == 0.0f,
          "the rate in effect is the smaller of two that agree in sign, and zero when they do not");
    Check(Shorten(3.0f, 1.0f) == 2.0f && Shorten(-3.0f, 1.0f) == -2.0f && Shorten(3.0f, 5.0f) == 0.0f &&
              Shorten(-3.0f, 5.0f) == 0.0f && Shorten(3.0f, 0.0f) == 3.0f && Shorten(3.0f, -1.0f) == 3.0f,
          "a deceleration's term shortens a lead toward zero, never past it and never the other way");

    // ---- a steady sweep is led to where it will be -----------------------------------------------------------
    {
        Sweep s;
        s.pitch0 = 0.2f;
        s.yawRate = 30.0f * kDeg;
        s.pitchRate = -12.0f * kDeg;
        int firstLed = -1;
        bool exact = true, allLed = true;
        for (int i = 0; i < 60; ++i)
        {
            const float yaw = s.Yaw(), pitch = s.Pitch();
            const Result r = s.Step();
            if (r.outcome == Outcome::Led)
            {
                if (firstLed < 0)
                    firstLed = i;
                // Where the sweep is one tick later.
                exact = exact && NearAngle(r.yaw, yaw + s.yawRate * kTick, 5.0e-6f) &&
                    Near(r.pitch, pitch + s.pitchRate * kTick, 5.0e-6f) && !r.capped && !r.repeated;
            }
            else if (firstLed >= 0)
                allLed = false;
            else
                exact = exact && Same(r.pitch, pitch) && r.outcome == Outcome::Priming && r.leadRad == 0.0f;
        }
        Check(firstLed == 2, "the lead starts with the third sample (three samples spanning 22 ms at 90 passes a second)");
        Check(exact, "a steady sweep is written where it will be one tick later, and unchanged while the filter primes");
        Check(allLed, "once started, every pass of a steady sweep is led");
    }
    {
        // Across the yaw seam: the record's yaw jumps by 2*pi there, the aim does not.
        Sweep s;
        s.yaw0 = kTwoPi - 0.05f;
        s.yawRate = 45.0f * kDeg;
        bool exact = true;
        int led = 0, jumps = 0;
        for (int i = 0; i < 40; ++i)
        {
            const float yaw = s.Yaw();
            const Result r = s.Step();
            jumps += r.restart == Restart::Jump ? 1 : 0;
            if (r.outcome == Outcome::Led)
            {
                ++led;
                exact = exact && NearAngle(r.yaw, yaw + s.yawRate * kTick, 5.0e-6f);
            }
        }
        Check(led == 38 && exact && jumps == 0, "the 2*pi seam of the yaw is not a jump: the sweep is led across it");
    }
    {
        // Backwards too, and at other pass rates.
        const uint64_t passes[3]{kPass60, kPass90, kPass120};
        const int expectFirst[3]{2, 2, 3};   // three samples span 33, 22 and 17 ms: the last needs a fourth
        bool ok = true;
        for (int p = 0; p < 3; ++p)
        {
            Sweep s;
            s.passUs = passes[p];
            s.yawRate = -80.0f * kDeg;
            s.pitchRate = 25.0f * kDeg;
            int firstLed = -1;
            for (int i = 0; i < 30; ++i)
            {
                const float yaw = s.Yaw(), pitch = s.Pitch();
                const Result r = s.Step();
                if (r.outcome != Outcome::Led)
                    continue;
                if (firstLed < 0)
                    firstLed = i;
                ok = ok && NearAngle(r.yaw, yaw + s.yawRate * kTick, 1.0e-5f) &&
                    Near(r.pitch, pitch + s.pitchRate * kTick, 1.0e-5f);
            }
            ok = ok && firstLed == expectFirst[p];
        }
        Check(ok, "a sweep is led alike at 60, 90 and 120 passes a second, in either direction");
    }
    {
        // The lead time scales the lead, and is itself capped.
        Sweep a, b;
        a.yawRate = b.yawRate = 20.0f * kDeg;
        Result ra{}, rb{};
        for (int i = 0; i < 10; ++i)
        {
            ra = a.Step(0.5f * kTick);
            rb = b.Step(10.0f);
        }
        Check(ra.outcome == Outcome::Led && Near(ra.leadRad, 20.0f * kDeg * 0.5f * kTick, 2.0e-6f),
              "half a tick of lead adds half as much");
        Check(rb.outcome == Outcome::Led && Near(rb.leadRad, 20.0f * kDeg * Rails{}.maxLeadS, 2.0e-6f),
              "a lead time past its cap is held at the cap (60 ms)");
    }

    // ---- an aim at rest is written unchanged -------------------------------------------------------------------
    {
        Sweep slow, half;
        slow.yawRate = 1.0f * kDeg;
        half.yawRate = 4.0f * kDeg;   // halfway between the rest speed (2 deg/s) and the full lead (6 deg/s)
        bool untouched = true;
        Result r{};
        for (int i = 0; i < 20; ++i)
        {
            const float pitch = slow.Pitch();
            float wrapped = std::fmod(slow.Yaw(), kTwoPi);
            const Result q = slow.Step();
            untouched = untouched && Same(q.yaw, wrapped) && Same(q.pitch, pitch) && q.outcome != Outcome::Led;
            r = half.Step();
        }
        Check(untouched && slow.filter.primed, "slower than the rest speed the setpoint is written bit for bit");
        Check(r.outcome == Outcome::Led && Near(r.leadRad, 0.5f * 4.0f * kDeg * kTick, 2.0e-6f),
              "between the rest speed and the full-lead speed the lead fades in");
    }

    // ---- a step is not led ---------------------------------------------------------------------------------------
    {
        // A snap turn during a sweep: 30 degrees between two samples.
        Sweep s;
        s.yawRate = 30.0f * kDeg;
        for (int i = 0; i < 12; ++i)
            (void)s.Step();
        s.yaw0 += 30.0f * kDeg;
        const float yaw = std::fmod(s.Yaw(), kTwoPi), pitch = s.Pitch();
        const Result jump = s.Step();
        Check(jump.restart == Restart::Jump && jump.outcome == Outcome::Priming && Same(jump.yaw, yaw) &&
                  Same(jump.pitch, pitch) && s.filter.count == 1,
              "a 30 degree step is a jump: written as it is, and the filter starts afresh from it");
        const Result second = s.Step();
        const float yawThird = s.Yaw();
        const Result third = s.Step();
        Check(second.outcome == Outcome::Priming && second.leadRad == 0.0f && third.outcome == Outcome::Led &&
                  NearAngle(third.yaw, yawThird + s.yawRate * kTick, 5.0e-6f),
              "after the jump the sweep is led again from the third sample, at the sweep's rate and not the step's");
    }
    {
        // The smallest snap turn (5 degrees), from rest, and the same step while the filter is still priming.
        Sweep s;
        for (int i = 0; i < 12; ++i)
            (void)s.Step();
        s.yaw0 += 5.0f * kDeg;
        const Result jump = s.Step();
        bool quiet = true;
        for (int i = 0; i < 12; ++i)
            quiet = quiet && s.Step().outcome != Outcome::Led;
        Check(jump.restart == Restart::Jump && jump.leadRad == 0.0f && quiet,
              "a 5 degree step from rest is a jump, and nothing after it is led while the aim stays put");

        Sweep early;
        (void)early.Step();
        early.yaw0 += 5.0f * kDeg;   // the second sample: no motion yet to judge it by, so it is taken...
        const Result taken = early.Step();
        const Result caught = early.Step();   // ...and the third, which does not continue it, exposes it
        bool never = taken.outcome != Outcome::Led && caught.outcome != Outcome::Led;
        for (int i = 0; i < 8; ++i)
            never = never && early.Step().outcome != Outcome::Led;
        Check(taken.restart == Restart::None && caught.restart == Restart::Jump && never,
              "a step between the first two samples is exposed by the third and never becomes a lead");

        Sweep big;
        (void)big.Step();
        big.yaw0 += 20.0f * kDeg;
        Check(big.Step().restart == Restart::Jump, "a step of more than 12 degrees between the first two samples is a jump at once");
    }
    {
        // A pitch step, and a step that arrives without a new sample (the same key).
        Sweep s;
        s.pitchRate = 15.0f * kDeg;
        for (int i = 0; i < 10; ++i)
            (void)s.Step();
        s.pitch0 -= 8.0f * kDeg;
        const float pitch = s.Pitch();
        const Result r = s.Step();
        Check(r.restart == Restart::Jump && Same(r.pitch, pitch), "a pitch step is a jump too");

        Filter f;
        Result q{};
        for (int i = 0; i < 10; ++i)
            q = f.Step(kStart + uint64_t(i) * kPass90, uint64_t(i + 1), 1.0f + 0.01f * float(i), 0.0f, kTick);
        Check(q.outcome == Outcome::Led, "(a led sweep to interrupt)");
        const Result held = f.Step(kStart + 10 * kPass90, 10, 1.0f + 0.09f + 0.3f, 0.0f, kTick);
        Check(held.restart == Restart::Jump && Same(held.yaw, 1.0f + 0.09f + 0.3f) && !held.repeated,
              "a setpoint that jumps under an unchanged sample is still a jump");
    }

    // ---- a gap in time starts afresh -------------------------------------------------------------------------------
    {
        Sweep s;
        s.yawRate = 40.0f * kDeg;
        for (int i = 0; i < 12; ++i)
            (void)s.Step();
        s.us += 80000;   // 80 ms with no write pass
        const float yaw = std::fmod(s.Yaw(), kTwoPi);
        const Result gap = s.Step();
        Check(gap.restart == Restart::Gap && gap.outcome == Outcome::Priming && Same(gap.yaw, yaw) &&
                  s.filter.count == 1,
              "after 80 ms without a sample the setpoint is written as it is and the filter starts afresh");
        (void)s.Step();
        Check(s.Step().outcome == Outcome::Led, "...and leads again from its third sample");

        // The clock running backwards is a gap, not a negative interval.
        Sweep back;
        back.yawRate = 40.0f * kDeg;
        for (int i = 0; i < 6; ++i)
            (void)back.Step();
        back.us -= 500000;
        Check(back.Step().restart == Restart::Gap, "a clock that runs backwards starts the filter afresh");

        // A sample that stops arriving while the write passes go on: the rate is held only as long as a gap.
        Filter f;
        Result r{};
        for (int i = 0; i < 8; ++i)
            r = f.Step(kStart + uint64_t(i) * kPass90, uint64_t(i + 1), 1.0f + 0.01f * float(i), 0.0f, kTick);
        const float heldYaw = 1.07f;
        bool kept = true;
        for (int i = 8; i < 13; ++i)   // five more passes of the same sample: 55 ms
        {
            r = f.Step(kStart + uint64_t(i) * kPass90, 8, heldYaw, 0.0f, kTick);
            kept = kept && r.repeated && r.outcome == Outcome::Led;
        }
        r = f.Step(kStart + 13 * kPass90, 8, heldYaw, 0.0f, kTick);
        Check(kept && r.restart == Restart::Gap && Same(r.yaw, heldYaw),
              "a repeated sample keeps its lead for up to the gap time, then is written as it is");
    }

    // ---- a stop is not carried past ----------------------------------------------------------------------------------
    {
        Sweep s;
        s.yawRate = 100.0f * kDeg;
        for (int i = 0; i < 12; ++i)
            (void)s.Step();
        // The hand stops dead: the same aim from now on (new samples of it). At 90 passes a second the newest
        // stretch is two samples long, so the first sample of the stop still holds one moving step: the lead is
        // already cut to a fraction by the deceleration it shows, and gone with the second.
        const float moving = 100.0f * kDeg * kTick;
        const float rest = s.fedYaw;
        bool unchanged = true;
        Result first{};
        for (int i = 0; i < 10; ++i)
        {
            const Result r = s.filter.Step(s.us, s.key++, rest, s.Pitch(), kTick);
            s.us += s.passUs;
            if (i == 0)
                first = r;
            else
                unchanged = unchanged && Same(r.yaw, rest);
        }
        Check(first.restart == Restart::None && first.leadRad <= 0.25f * moving && first.yaw >= rest && unchanged,
              "a dead stop ends the lead: a quarter of it at most on the first sample, none from the second");

        // At 60 passes a second the newest stretch is one sample: the first sample of the stop ends the lead.
        Sweep slow;
        slow.passUs = kPass60;
        slow.yawRate = 100.0f * kDeg;
        for (int i = 0; i < 12; ++i)
            (void)slow.Step();
        const Result halted = slow.filter.Step(slow.us, slow.key, slow.fedYaw, slow.Pitch(), kTick);
        Check(halted.outcome == Outcome::Stopping && Same(halted.yaw, slow.fedYaw),
              "at 60 passes a second the first sample of a stop is written as it is");

        // A sample that turns back: the newest stretch has all but stopped, and what is left of its lead is less than
        // the deceleration takes off.
        Sweep t;
        t.yawRate = 60.0f * kDeg;
        for (int i = 0; i < 12; ++i)
            (void)t.Step();
        const float turned = t.fedYaw - 0.3f * kDeg;
        const Result r = t.filter.Step(t.us, t.key, turned, t.Pitch(), kTick);
        Check(r.outcome == Outcome::Stopping && Same(r.yaw, turned), "a sample that turns back is written as it is");
        // ...and one that turns back further than the stretch came is against the window outright.
        const float reversed = t.fedYaw - 1.5f * kDeg;
        Sweep back = t;
        const Result rr = back.filter.Step(back.us + back.passUs, back.key + 1, reversed, back.Pitch(), kTick);
        Check(rr.outcome == Outcome::Stopping && Same(rr.yaw, reversed) && back.filter.rateYaw == 0.0f,
              "a stretch that runs against the window has no rate at all");

        // A hard stop from a fast sweep is further than the motion predicts: a jump.
        Sweep fast;
        fast.yawRate = 400.0f * kDeg;
        for (int i = 0; i < 12; ++i)
            (void)fast.Step();
        const float stopped = fast.fedYaw;
        const Result hard = fast.filter.Step(fast.us, fast.key, stopped, fast.Pitch(), kTick);
        Check(hard.restart == Restart::Jump && Same(hard.yaw, stopped), "a dead stop from 400 deg/s is written as it is");
    }
    {
        // A steady deceleration to rest: 200 deg/s to nothing in 100 ms. A straight line from the slowing aim would
        // pass the resting aim by a * lead^2 / 2 (0.28 deg); with the deceleration's term the written aim does not
        // pass it, and stays close to the aim one tick ahead all the way down (the unled aim is 2.7 degrees or more
        // behind it). The 0.4 degrees is the two samples it takes to see that the slowing began.
        const float decel = 2000.0f * kDeg;   // rad/s^2
        const float stopS = 0.1f;
        const uint64_t passes[3]{kPass60, kPass90, kPass120};
        bool close = true, insideLine = true;
        for (int p = 0; p < 3; ++p)
        {
            const float dt = float(passes[p]) * 1.0e-6f;
            const float v0 = decel * stopS;
            const float before = 0.2f;   // steady motion first, so the filter is primed
            const auto aim = [&](float t) {
                const float d = t - before;
                if (d <= 0.0f)
                    return v0 * t;
                if (d < stopS)
                    return v0 * before + v0 * d - 0.5f * decel * d * d;
                return v0 * before + 0.5f * v0 * stopS;
            };
            const float finalYaw = aim(1.0f);
            Filter f;
            float pastTheEnd = 0.0f, worst = 0.0f;
            for (int i = 0; float(i) * dt < 0.45f; ++i)
            {
                const float t = float(i) * dt;
                const Result r = f.Step(kStart + uint64_t(i) * passes[p], uint64_t(i + 1), aim(t), 0.0f, kTick);
                pastTheEnd = std::fmax(pastTheEnd, r.yaw - finalYaw);
                if (t > before)
                    worst = std::fmax(worst, std::fabs(r.yaw - aim(t + kTick)));
            }
            insideLine = insideLine && pastTheEnd <= 0.01f * kDeg;
            close = close && worst <= 0.4f * kDeg;
        }
        Check(insideLine, "a steady deceleration is not written past the resting aim");
        Check(close, "...and through it the written aim stays within 0.4 degrees of the aim one tick ahead");
    }
    {
        // A flick: 40 degrees in 150 ms, speed rising and falling as a raised cosine (533 deg/s at its peak). Over its
        // last 40 ms and the 30 ms after it -- where a flick shot is fired -- the unled aim is 3 degrees behind the
        // aim one tick ahead; the led one stays within 1.2 degrees of it and never runs a degree past the end.
        const double twoPi = 6.283185307179586, length = 0.15, sweep = 40.0 * double(kDeg);
        const auto aim = [&](double t) {
            if (t <= 0.0)
                return 0.0;
            if (t >= length)
                return sweep;
            return sweep * (t / length - std::sin(twoPi * t / length) / twoPi);
        };
        const uint64_t passes[2]{kPass90, kPass120};
        double ledLate = 0.0, unledLate = 0.0, pastTheEnd = 0.0;
        for (int p = 0; p < 2; ++p)
            for (int phase = 0; phase < 8; ++phase)   // where the passes fall against the flick
            {
                const double dt = double(passes[p]) * 1.0e-6;
                Filter f;
                for (int i = 0; i < 60; ++i)
                {
                    const double t = double(i) * dt - 0.2 + double(phase) * dt / 8.0;
                    const Result r = f.Step(kStart + uint64_t(i) * passes[p], uint64_t(i + 1), float(1.0 + aim(t)), 0.0f,
                                            kTick);
                    const double ahead = aim(t + double(kTick));
                    pastTheEnd = std::fmax(pastTheEnd, double(r.yaw) - 1.0 - sweep);
                    if (t > length - 0.04 && t < length + 0.03)
                    {
                        ledLate = std::fmax(ledLate, std::fabs(double(r.yaw) - 1.0 - ahead));
                        unledLate = std::fmax(unledLate, std::fabs(aim(t) - ahead));
                    }
                }
            }
        Check(unledLate > 3.0 * double(kDeg) && ledLate < 1.2 * double(kDeg),
              "at the end of a flick the led aim is within 1.2 degrees of the aim one tick ahead (unled: 3 behind)");
        Check(pastTheEnd < 1.0 * double(kDeg), "...and is never written a degree past where the flick ends");
    }
    {
        // An artefact that alternates from one write pass to the next (0.05 degrees either way, at 120 passes a
        // second) cancels in the newest stretch: every pass of the sweep under it is still led, and by its rate.
        const float rate = 30.0f * kDeg;
        const float dt = float(kPass120) * 1.0e-6f;
        Filter f;
        int led = 0, total = 0;
        bool byItsRate = true;
        for (int i = 0; i < 200; ++i)
        {
            const float yaw = 1.0f + rate * float(i) * dt + ((i & 1) ? 0.05f : -0.05f) * kDeg;
            const Result r = f.Step(kStart + uint64_t(i) * kPass120, uint64_t(i + 1), yaw, 0.0f, kTick);
            if (i < 8)
                continue;
            ++total;
            if (r.outcome == Outcome::Led)
            {
                ++led;
                byItsRate = byItsRate && Near(r.leadRad, rate * kTick, 0.2f * rate * kTick);
            }
        }
        Check(led == total && byItsRate, "a pass-to-pass alternation does not cost the sweep its lead");
    }
    {
        // An acceleration is under-led, never over-led: the written aim stays behind the aim one tick ahead.
        const float accel = 1500.0f * kDeg;
        const float dt = float(kPass90) * 1.0e-6f;
        Filter f;
        bool behind = true;
        int led = 0;
        for (int i = 0; i < 20; ++i)
        {
            const float t = float(i) * dt;
            const float yaw = 0.5f * accel * t * t;
            const float ahead = 0.5f * accel * (t + kTick) * (t + kTick);
            const Result r = f.Step(kStart + uint64_t(i) * kPass90, uint64_t(i + 1), yaw, 0.0f, kTick);
            if (r.outcome == Outcome::Led)
            {
                ++led;
                behind = behind && r.yaw > yaw && r.yaw < ahead;
            }
        }
        Check(led > 10 && behind, "under an acceleration the lead is short of the aim one tick ahead, never past it");
    }

    // ---- the cap ------------------------------------------------------------------------------------------------------
    {
        Sweep s;
        s.yawRate = 400.0f * kDeg;   // one tick of this is 6.7 degrees
        Result r{};
        float yaw = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            yaw = s.Yaw();
            r = s.Step();
        }
        Check(r.outcome == Outcome::Led && r.capped && Near(r.leadRad, Rails{}.maxLeadRad, 1.0e-6f) &&
                  NearAngle(r.yaw, yaw + Rails{}.maxLeadRad, 5.0e-6f),
              "the lead is capped at 3 degrees");

        // The cap, the jump and the rest speed are true angles: near the vertical a large change of yaw is a small
        // change of aim.
        Sweep steep;
        steep.pitch0 = 80.0f * kDeg;
        steep.yawRate = 60.0f * kDeg;   // 10.4 deg/s of aim at this pitch
        for (int i = 0; i < 12; ++i)
        {
            yaw = steep.Yaw();
            r = steep.Step();
        }
        const float across = std::cos(80.0f * kDeg);
        Check(r.outcome == Outcome::Led && !r.capped && Near(r.leadRad, 60.0f * kDeg * kTick * across, 2.0e-6f) &&
                  NearAngle(r.yaw, yaw + 60.0f * kDeg * kTick, 5.0e-6f),
              "at a steep pitch the yaw is led by its own rate and counted by the angle it makes");
        Sweep steepFast;
        steepFast.pitch0 = 80.0f * kDeg;
        steepFast.yawRate = 2000.0f * kDeg;
        for (int i = 0; i < 12; ++i)
        {
            yaw = steepFast.Yaw();
            r = steepFast.Step();
        }
        Check(r.outcome == Outcome::Led && r.capped && NearAngle(r.yaw, yaw + Rails{}.maxLeadRad / across, 2.0e-4f),
              "...and capped by that angle, both axes scaled alike");
        Sweep both;
        both.yawRate = 300.0f * kDeg;
        both.pitchRate = 300.0f * kDeg;
        float pitch = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            yaw = both.Yaw();
            pitch = both.Pitch();
            r = both.Step();
        }
        Check(r.capped && Near((r.pitch - pitch) / Wrap(r.yaw - yaw), 1.0f, 0.02f),
              "a capped lead keeps its direction");
    }

    // ---- samples and write passes -----------------------------------------------------------------------------------
    {
        // The write pass sees one sample twice and then skips one. With keys, neither reads as a change of speed.
        const float rate = 50.0f * kDeg;
        const float dt = float(kPass90) * 1.0e-6f;
        const int sampleAt[12]{0, 1, 2, 3, 4, 4, 6, 7, 8, 9, 10, 11};
        Filter keyed, unkeyed;
        bool keyedExact = true, repeatSeen = false, unkeyedSafe = true;
        float unkeyedAtRepeat = 1.0f;
        for (int i = 0; i < 12; ++i)
        {
            const float yaw = 1.0f + rate * float(sampleAt[i]) * dt;
            const uint64_t us = kStart + uint64_t(i) * kPass90;
            const Result k = keyed.Step(us, uint64_t(sampleAt[i] + 1), yaw, 0.0f, kTick);
            const Result u = unkeyed.Step(us, 0, yaw, 0.0f, kTick);
            if (i >= 2)
                keyedExact = keyedExact && k.outcome == Outcome::Led && Near(k.yaw, yaw + rate * kTick, 5.0e-6f);
            repeatSeen = repeatSeen || k.repeated;
            // Without a key the repeat is a sample that did not move -- the aim slowing -- so that pass is led
            // short; and no pass is led further than the skipped sample's step explains.
            if (i == 5)
                unkeyedAtRepeat = u.yaw - yaw;
            unkeyedSafe = unkeyedSafe && u.yaw - yaw <= rate * kTick * 1.35f && u.yaw >= yaw;
        }
        Check(keyedExact && repeatSeen,
              "with sample keys a repeated sample keeps its lead and a skipped one is timed from the last new one");
        Check(unkeyedAtRepeat < 0.5f * rate * kTick && unkeyedSafe,
              "without keys a repeated sample costs that pass most of its lead and nothing is led too far");
    }
    {
        // Two passes closer than a millisecond are one sample.
        Filter f;
        Result r{};
        for (int i = 0; i < 6; ++i)
            r = f.Step(kStart + uint64_t(i) * kPass90, uint64_t(i + 1), 1.0f + 0.01f * float(i), 0.0f, kTick);
        const int before = f.count;
        r = f.Step(kStart + 5 * kPass90 + 300, 7, 1.0502f, 0.0f, kTick);
        Check(r.repeated && f.count == before && r.outcome == Outcome::Led, "a pass within a millisecond of the last sample adds none");
    }

    // ---- off, and bad input -------------------------------------------------------------------------------------------
    {
        Sweep s;
        s.yawRate = 30.0f * kDeg;
        for (int i = 0; i < 10; ++i)
            (void)s.Step();
        const float yaw = std::fmod(s.Yaw(), kTwoPi), pitch = s.Pitch();
        const Result off = s.Step(0.0f);
        Check(off.outcome == Outcome::Off && Same(off.yaw, yaw) && Same(off.pitch, pitch) && s.filter.count == 0 &&
                  !s.filter.primed,
              "with no lead time the setpoint is written as it is and nothing is kept");
        Check(s.Step().restart == Restart::First && s.Step().outcome == Outcome::Priming && s.Step().outcome == Outcome::Led,
              "switched back on, the filter starts from nothing and leads from its third sample");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const Result bad = s.filter.Step(s.us, s.key, nan, 0.0f, kTick);
        Check(bad.outcome == Outcome::Off && std::isnan(bad.yaw) && s.filter.count == 0,
              "a non-finite setpoint is handed back untouched and forgets the motion before it");
        const Result badLead = s.filter.Step(s.us, s.key, 1.0f, 0.0f, nan);
        Check(badLead.outcome == Outcome::Off && Same(badLead.yaw, 1.0f), "a non-finite lead time is no lead");
        Filter f;
        Result r{};
        for (int i = 0; i < 10; ++i)
            r = f.Step(kStart + uint64_t(i) * kPass90, uint64_t(i + 1), 1.0f + 0.01f * float(i), 0.0f, kTick);
        f.Reset();
        r = f.Step(kStart + 10 * kPass90, 11, 1.1f, 0.0f, kTick);
        Check(r.restart == Restart::First && r.outcome == Outcome::Priming && Same(r.yaw, 1.1f), "Reset() starts afresh");
    }
    {
        // A long turn in one direction: the filter's own yaw stays small (whole turns come off), so the lead
        // keeps its precision.
        Sweep s;
        s.yawRate = 170.0f * kDeg;
        bool exact = true;
        float largest = 0.0f;
        for (int i = 0; i < 3000; ++i)   // 33 s: more than fifteen turns
        {
            const float yaw = s.Yaw();
            const Result r = s.Step();
            if (r.outcome == Outcome::Led)
                exact = exact && NearAngle(r.yaw, yaw + s.yawRate * kTick, 4.0e-4f);
            for (int k = 0; k < s.filter.count; ++k)
                largest = std::fmax(largest, std::fabs(s.filter.samples[k].yaw));
        }
        Check(exact && largest < kTwoPi, "fifteen turns in one direction: still led, and the kept yaw never grows past one turn");
    }

    // ---- noise ---------------------------------------------------------------------------------------------------------
    {
        // A held aim with tracking noise: below the rest speed, so the written aim IS the setpoint (gain exactly 1).
        Filter f;
        Noise noise;
        bool identical = true;
        for (int i = 0; i < 2000; ++i)
        {
            const float yaw = 2.0f + 0.02f * kDeg * noise.Next(), pitch = -0.3f + 0.02f * kDeg * noise.Next();
            const Result r = f.Step(kStart + uint64_t(i) * kPass90, uint64_t(i + 1), yaw, pitch, kTick);
            identical = identical && Same(r.yaw, yaw) && Same(r.pitch, pitch);
        }
        Check(identical, "a held aim with 0.02 degrees of noise is written bit for bit: the lead adds nothing at rest");
    }
    {
        // A 30 deg/s sweep with the same noise. Against the noise-free sweep one tick ahead, the written aim's error
        // is under three times the noise at one tick of lead (2.2 to 2.7 as measured), and the lead itself is never
        // larger than the lead time times the newest stretch's rate, nor against it.
        const float rate = 30.0f * kDeg, amplitude = 0.02f * kDeg;
        const uint64_t passes[3]{kPass60, kPass90, kPass120};
        bool bounded = true, insideStretch = true;
        for (int p = 0; p < 3; ++p)
        {
            const float dt = float(passes[p]) * 1.0e-6f;
            Filter f;
            Noise noise;
            double sumNoise = 0.0, sumError = 0.0;
            int n = 0;
            for (int i = 0; i < 4000; ++i)
            {
                const float clean = rate * float(i) * dt;
                const float e = amplitude * noise.Next();
                const float yaw = clean + e;
                const Result r = f.Step(kStart + uint64_t(i) * passes[p], uint64_t(i + 1), yaw, 0.0f, kTick);
                if (r.outcome != Outcome::Led)
                    continue;
                const float lead = r.yaw - yaw;
                insideStretch = insideStretch && lead > 0.0f && lead <= f.recentYaw * kTick * 1.0001f + 1.0e-7f;
                const float error = r.yaw - (clean + rate * kTick);
                sumNoise += double(e) * double(e);
                sumError += double(error) * double(error);
                ++n;
            }
            const double gain = n ? std::sqrt(sumError / sumNoise) : 1.0e9;
            bounded = bounded && n > 3900 && gain > 1.0 && gain < 3.0;
        }
        Check(bounded, "on a noisy sweep the written aim's error stays under three times the noise (60, 90, 120 passes)");
        Check(insideStretch, "the lead never exceeds the lead time times the newest stretch's rate, and never opposes it");
    }

    std::printf("aim_lead_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
