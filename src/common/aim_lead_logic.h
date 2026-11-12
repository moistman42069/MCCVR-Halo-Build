#pragma once

// AIM LEAD -- the written aim, carried ahead to the tick that fires along it.
//
// The hand's aim is written into the game's aim record once per control pass, and the simulation fires each shot
// along that record. A record written at one pass is first consumed by the next tick, so the shot leaves along a
// hand sample about one frame to one tick old: while the aim turns, it goes where the hand pointed then, not where
// the reticle is drawn now. The record is the one place this can be corrected so that every machine of a co-op
// session computes the same shot: it is shared input; a change made inside one machine's fire routine is not.
//
// So the WRITTEN aim -- and nothing drawn -- is extrapolated: written = setpoint + rate * lead. `rate` is the
// setpoint's own angular rate, measured from the setpoints the write site has seen. It is not taken from the
// controller's angular velocity, because the setpoint is not the controller: it is the aim solve's output (a
// two-hand line, a steadied hand, the virtual stock), turned by the player's stick turn and bent from the game's
// eye through the traced point, and its rate is the rate of all of those together.
//
// THE RATE is, per axis, the smaller of two estimates that agree in sign, and zero where they do not:
//   * the least-squares slope over the samples of the last `windowUs`: smooth, but half a window late;
//   * the newest stretch alone -- the newest sample against the latest one at least `recentSpanUs` older (one
//     sample back at 60 write passes a second, two at 90 and 120, so an artefact that alternates from pass to pass
//     cancels in it): noisier, but only half a stretch late.
// Taking the smaller turns every error of the estimate into an UNDER-lead: the lead grows a window behind an
// acceleration, and falls with the newest stretch when the aim slows.
//
// A DECELERATION is carried too. A straight line drawn from a slowing aim runs past where the aim will be -- by
// half the deceleration times the lead squared, 1.4 deg near the end of a 40 deg flick made in 150 ms -- which is
// exactly where a flick shot is fired. Where the newest stretch is the slower of the two estimates, their
// difference over the time between them is the deceleration, and the lead is shortened by its term (never
// lengthened, never turned back). Over the last 40 ms of that flick the written aim then stays within 1 deg of the
// aim one tick ahead at 90 and 120 write passes a second, against 3 deg behind it without any lead. (At 60 passes a
// flick that violent is a jump by the rule below, and is written unled.) An under-lead is the behaviour without
// this filter.
//
// WHAT IS WRITTEN UNCHANGED:
//   * a JUMP: a sample further than `jumpRad` from where the motion so far puts it (a snap turn, a recentre, a seat
//     entry). The filter starts afresh from that sample, so the step is never read as a rate;
//   * after a GAP: no new sample for `gapUs` (a stall, a pause, tracking lost);
//   * the first samples after either, and after Reset(): until three samples span `minSpanUs`;
//   * an aim at REST: slower than `restRadPerS` nothing is added, and the lead fades in up to `fullRadPerS`, so a
//     held aim is written bit for bit as it is without the filter;
//   * an aim that has stopped or turned back over the newest stretch.
// The lead is capped at `maxLeadRad`. Angles between two aims are true angles: a yaw difference counts by
// cos(pitch).
//
// A SAMPLE is one setpoint the aim path produced, named by `sampleKey` (the tracking frame it was solved from).
// The write site may run more than once per sample, or miss one: a repeated key adds no sample and keeps the rate,
// and the next new key is timed from the previous new one, so neither a repeat nor a skip reads as a change of
// speed. Key 0 means "unknown": every call is then a sample.
//
// NOISE. The lead is never larger than the lead time times the newest stretch's rate, and never against it. White
// noise on the setpoints of a steady sweep reaches the written aim 2.2 to 2.7 times as large at one 60 Hz tick of
// lead (measured at 60, 90 and 120 write passes a second; the unit test holds it under 3). Below the rest speed
// the gain is exactly 1. An oscillation faster than the window follows (a 10 Hz tremor of 0.2 deg) is led out of
// phase, and its error is about a sixth larger than without the lead.
//
// Pure logic with no engine or runtime dependency: unit-tested in tests/aim_lead_tests.cpp. Angles are radians (yaw
// wraps at 2*pi, pitch does not); times are microseconds of one monotonic clock.

#include <cmath>
#include <cstdint>

namespace aim_lead
{
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kRadToDeg = 57.295779513082320f;
// The setting's range, in simulation ticks.
inline constexpr float kMaxTicks = 3.0f;

// The setting as it is used: a finite value inside [0, kMaxTicks], else `fallback`.
inline float ClampTicks(float ticks, float fallback)
{
    if (!std::isfinite(ticks))
        return fallback;
    return ticks < 0.0f ? 0.0f : (ticks > kMaxTicks ? kMaxTicks : ticks);
}

// The lead in seconds: `ticks` simulation ticks of `tickUs` microseconds each. A period outside 5 to 50 ms is not
// a tick (not measured yet, a stall, a burst) and reads as 60 Hz. 0 when the setting is off.
inline float LeadSeconds(float ticks, uint32_t tickUs)
{
    const float clamped = ClampTicks(ticks, 0.0f);
    if (!(clamped > 0.0f))
        return 0.0f;
    const uint32_t period = tickUs >= 5000u && tickUs <= 50000u ? tickUs : 16667u;
    return clamped * float(period) * 1.0e-6f;
}

struct Rails
{
    uint64_t windowUs = 60000;      // the slope's window: three samples fit down to 34 write passes a second
    uint64_t minSpanUs = 20000;     // ...and it needs three samples spanning at least this
    uint64_t recentSpanUs = 15000;  // the newest stretch reaches back at least this far
    uint64_t gapUs = 60000;         // no new sample for this long: the motion before it says nothing about now
    uint64_t minStepUs = 1000;      // a sample nearer than this to the last one is not a new one
    float jumpRad = 0.0436f;        // 2.5 deg off where the motion so far puts a sample: a jump
    float firstStepRad = 0.2094f;   // 12 deg between the first two samples: a jump (no motion yet to judge it by)
    float restRadPerS = 0.0349f;    // 2 deg/s: slower than this nothing is added
    float fullRadPerS = 0.1047f;    // 6 deg/s: from here the whole lead
    float maxLeadRad = 0.0524f;     // 3 deg: the lead's cap
    float maxLeadS = 0.060f;        // the lead time's cap
};

// What one step did to the setpoint.
enum class Outcome : uint8_t
{
    Led = 0,   // the lead was added
    Off,       // no lead time, or a non-finite input: nothing kept, nothing added
    Priming,   // too few samples since the filter (re)started
    Resting,   // the aim moves slower than the rest speed
    Stopping,  // over the newest stretch the aim stood still or turned back, or its deceleration ends the lead
    Count
};

// Why the filter started afresh at this step, if it did.
enum class Restart : uint8_t
{
    None = 0,
    First,     // the first sample since Reset()
    Jump,
    Gap,
    Count
};

struct Result
{
    Outcome outcome = Outcome::Off;
    Restart restart = Restart::None;
    bool repeated = false;   // the same sample as the last step's: no new one was taken
    bool capped = false;     // the lead was held at maxLeadRad
    float leadRad = 0.0f;    // the angle added (0 unless Led)
    float yaw = 0.0f, pitch = 0.0f;   // the aim to write: the setpoint itself unless Led
};

inline float Wrap(float angle)
{
    return std::remainder(angle, kTwoPi);
}

// The one of two rates nearer zero when they agree in sign; zero when they do not.
inline float Minmod(float a, float b)
{
    if (a > 0.0f && b > 0.0f)
        return a < b ? a : b;
    if (a < 0.0f && b < 0.0f)
        return a > b ? a : b;
    return 0.0f;
}

// `value` brought `by` (>= 0) nearer zero, and no further than zero.
inline float Shorten(float value, float by)
{
    if (!(by > 0.0f))
        return value;
    const float left = std::fabs(value) - by;
    return left > 0.0f ? (value > 0.0f ? left : -left) : 0.0f;
}

// One writer's filter: every member belongs to the thread that writes the record.
struct Filter
{
    static constexpr int kCapacity = 8;
    struct Sample
    {
        uint64_t us = 0;
        float yaw = 0.0f;   // continuous: each on its predecessor's 2*pi branch
        float pitch = 0.0f;
    };
    Sample samples[kCapacity]{};   // oldest first
    int count = 0;
    uint64_t key = 0;
    // The last step's setpoint: a repeated sample is still watched for a jump.
    float lastYaw = 0.0f, lastPitch = 0.0f;
    // Rates in rad/s (yaw as the angle about the vertical): the newest stretch's, the window's, and the one in
    // effect; and, where the aim is slowing, its deceleration in rad/s^2 (0 otherwise).
    float recentYaw = 0.0f, recentPitch = 0.0f;
    float windowYaw = 0.0f, windowPitch = 0.0f;
    float rateYaw = 0.0f, ratePitch = 0.0f;
    float slowingYaw = 0.0f, slowingPitch = 0.0f;
    float recentHalfS = 0.0f;   // half the newest stretch, seconds: how late its rate is
    bool primed = false;

    void Reset()
    {
        count = 0;
        key = 0;
        recentYaw = recentPitch = windowYaw = windowPitch = rateYaw = ratePitch = 0.0f;
        slowingYaw = slowingPitch = recentHalfS = 0.0f;
        primed = false;
    }

    // One write pass: the setpoint (yaw, pitch) at `nowUs`, solved from sample `sampleKey`, to be led by `leadS`
    // seconds. Returns the aim to write and what was done.
    Result Step(uint64_t nowUs, uint64_t sampleKey, float yaw, float pitch, float leadS, const Rails& rails = Rails{})
    {
        Result r{};
        r.yaw = yaw;
        r.pitch = pitch;
        if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(leadS) || !(leadS > 0.0f))
        {
            Reset();
            return r;
        }
        const float cosPitch = std::cos(pitch);
        const auto angle = [cosPitch](float dYaw, float dPitch) {
            const float across = dYaw * cosPitch;
            return std::sqrt(across * across + dPitch * dPitch);
        };
        if (count == 0)
        {
            r.restart = Restart::First;
        }
        else
        {
            const Sample& newest = samples[count - 1];
            if (nowUs < newest.us || nowUs - newest.us > rails.gapUs)
            {
                Reset();
                r.restart = Restart::Gap;
            }
            else if (sampleKey != 0 && sampleKey == key)
            {
                // The same sample again: nothing to learn, but a setpoint that moved this far without one jumped.
                if (angle(Wrap(yaw - lastYaw), pitch - lastPitch) > rails.jumpRad)
                {
                    Reset();
                    r.restart = Restart::Jump;
                }
                else
                {
                    r.repeated = true;
                }
            }
            else if (nowUs - newest.us < rails.minStepUs)
            {
                r.repeated = true;
            }
            else
            {
                // Against where the newest stretch's rate puts this sample; with one sample there is no rate yet.
                const float dt = float(nowUs - newest.us) * 1.0e-6f;
                const float dYaw = Wrap(yaw - newest.yaw), dPitch = pitch - newest.pitch;
                const bool jump = count >= 2
                    ? angle(dYaw - recentYaw * dt, dPitch - recentPitch * dt) > rails.jumpRad
                    : angle(dYaw, dPitch) > rails.firstStepRad;
                if (jump)
                {
                    Reset();
                    r.restart = Restart::Jump;
                }
            }
        }
        if (!r.repeated)
        {
            Push(nowUs, yaw, pitch, rails);
            key = sampleKey;
            Estimate(rails);
        }
        lastYaw = yaw;
        lastPitch = pitch;
        if (!primed)
        {
            r.outcome = Outcome::Priming;
            return r;
        }
        const float span = rails.fullRadPerS - rails.restRadPerS;
        const float over = angle(windowYaw, windowPitch) - rails.restRadPerS;
        const float gain = !(over > 0.0f) ? 0.0f : (!(span > 0.0f) || over >= span ? 1.0f : over / span);
        if (!(gain > 0.0f))
        {
            r.outcome = Outcome::Resting;
            return r;
        }
        // The straight line over the lead time, less a deceleration's own term: the newest stretch's rate is half a
        // stretch old, and the aim goes on slowing through the lead, so the line overshoots by
        // deceleration * lead * (half a stretch + half the lead).
        const float lead = leadS < rails.maxLeadS ? leadS : rails.maxLeadS;
        const float slowed = lead * (recentHalfS + 0.5f * lead);
        float leadYaw = gain * Shorten(rateYaw * lead, slowingYaw * slowed);
        float leadPitch = gain * Shorten(ratePitch * lead, slowingPitch * slowed);
        float added = angle(leadYaw, leadPitch);
        if (!std::isfinite(added) || !(added > 0.0f))
        {
            r.outcome = Outcome::Stopping;
            return r;
        }
        if (added > rails.maxLeadRad)
        {
            const float scale = rails.maxLeadRad / added;
            leadYaw *= scale;
            leadPitch *= scale;
            added = rails.maxLeadRad;
            r.capped = true;
        }
        r.outcome = Outcome::Led;
        r.leadRad = added;
        r.yaw = yaw + leadYaw;
        r.pitch = pitch + leadPitch;
        return r;
    }

  private:
    void Push(uint64_t us, float yaw, float pitch, const Rails& rails)
    {
        const float continuous = count > 0 ? samples[count - 1].yaw + Wrap(yaw - samples[count - 1].yaw) : yaw;
        // What has left the window goes, and the oldest when the ring is full.
        int first = 0;
        while (first < count && us - samples[first].us > rails.windowUs)
            ++first;
        if (count - first >= kCapacity)
            first = count - kCapacity + 1;
        if (first > 0)
        {
            for (int i = first; i < count; ++i)
                samples[i - first] = samples[i];
            count -= first;
        }
        samples[count].us = us;
        samples[count].yaw = continuous;
        samples[count].pitch = pitch;
        ++count;
        // Whole turns come off every sample, so the continuous yaw stays small however long the player turns.
        const float turns = std::round(continuous / kTwoPi);
        if (turns != 0.0f)
            for (int i = 0; i < count; ++i)
                samples[i].yaw -= turns * kTwoPi;
    }

    void Estimate(const Rails& rails)
    {
        primed = false;
        recentYaw = recentPitch = windowYaw = windowPitch = rateYaw = ratePitch = 0.0f;
        slowingYaw = slowingPitch = recentHalfS = 0.0f;
        if (count < 2)
            return;
        const Sample& newest = samples[count - 1];
        // The newest stretch: back to the latest sample at least recentSpanUs older (the oldest, while none is).
        int from = count - 2;
        while (from > 0 && newest.us - samples[from].us < rails.recentSpanUs)
            --from;
        const Sample& before = samples[from];
        const float dt = float(newest.us - before.us) * 1.0e-6f;
        if (!(dt > 0.0f))
            return;
        recentYaw = (newest.yaw - before.yaw) / dt;
        recentPitch = (newest.pitch - before.pitch) / dt;
        if (count < 3 || newest.us - samples[0].us < rails.minSpanUs)
            return;
        // Least squares about the newest sample: times and angles are differences from it, so they stay small.
        double sumT = 0.0, sumTT = 0.0, sumYaw = 0.0, sumTYaw = 0.0, sumPitch = 0.0, sumTPitch = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double t = -double(newest.us - samples[i].us) * 1.0e-6;
            const double y = double(samples[i].yaw) - double(newest.yaw);
            const double p = double(samples[i].pitch) - double(newest.pitch);
            sumT += t;
            sumTT += t * t;
            sumYaw += y;
            sumTYaw += t * y;
            sumPitch += p;
            sumTPitch += t * p;
        }
        const double n = double(count);
        const double spread = n * sumTT - sumT * sumT;
        if (!(spread > 0.0))
            return;
        windowYaw = float((n * sumTYaw - sumT * sumYaw) / spread);
        windowPitch = float((n * sumTPitch - sumT * sumPitch) / spread);
        if (!std::isfinite(windowYaw) || !std::isfinite(windowPitch) || !std::isfinite(recentYaw) ||
            !std::isfinite(recentPitch))
        {
            windowYaw = windowPitch = 0.0f;
            return;
        }
        rateYaw = Minmod(windowYaw, recentYaw);
        ratePitch = Minmod(windowPitch, recentPitch);
        // A deceleration, per axis: the window's slope is the rate at its samples' mean time, the stretch's the rate
        // at its middle. Where the stretch is the slower, the rate fell by their difference over the time between.
        recentHalfS = 0.5f * dt;
        const float between = float(-sumT / n) - recentHalfS;
        if (between > 0.002f)
        {
            if (rateYaw != 0.0f && std::fabs(recentYaw) < std::fabs(windowYaw))
                slowingYaw = (std::fabs(windowYaw) - std::fabs(recentYaw)) / between;
            if (ratePitch != 0.0f && std::fabs(recentPitch) < std::fabs(windowPitch))
                slowingPitch = (std::fabs(windowPitch) - std::fabs(recentPitch)) / between;
        }
        primed = true;
    }
};
} // namespace aim_lead
