#pragma once
// Dev witness for a seat camera placed on a marker of the seated unit: is the camera, each frame, where that
// frame's interpolation puts the vehicle it rides?
//
// The simulation moves a vehicle one step per tick. A frame draws it between the last two ticks, at the frame
// interpolator's factor (0 = the tick before, 1 = the newest). A camera carried by the vehicle has to sit at the
// same fraction of that step: where it does not, the hull is drawn displaced against the view for that frame, by
// the difference times the step (11 cm for a whole tick at 7 m/s), and the world steps by the same amount.
//
// A sample holds where the camera's marker was placed, where the simulation holds that marker at the newest
// tick, the root vehicle's node as simulated and as drawn this frame, and the clock. The summary measures the
// camera's own fraction of the newest tick's step, the drawn hull's, and how far apart the two put them.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace seat_head_trace
{
constexpr unsigned kRing = 1024;
constexpr float kMmPerWu = 3048.0f;
// A vehicle slower than this (30 mm a tick, 1.8 m/s) gives no step to measure a fraction of.
constexpr float kMinStepWu = 0.01f;

struct Sample
{
    uint64_t us = 0;
    int64_t tick = -1;
    float factor = NAN;
    float placed[3]{};      // the marker where the camera takes it
    float atTick[3]{};      // the marker as the simulation holds it
    float hull[3]{};        // the root vehicle's node as the simulation holds it
    float hullDrawn[3]{};   // ...and as this frame draws it
    uint32_t thread = 0;
    bool hullValid = false;
    bool carried = false;   // the camera's marker was carried by the drawn vehicle
};

// What one window hands the next: its last sample, and the step of the tick that sample sat in.
struct Carry
{
    bool valid = false, stepValid = false, hullStepValid = false;
    Sample last{};
    float step[3]{}, hullStep[3]{};
};

inline float Dot(const float a[3], const float b[3]) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// The camera's fraction of the newest tick's step (0 = the tick before, 1 = the newest tick), and how far off the
// line between the two it sits. False while the step is too small to measure against.
inline bool StepFraction(const float at[3], const float newest[3], const float step[3], float& fraction,
                         float& offLineWu) noexcept
{
    const float length2 = Dot(step, step);
    if (!std::isfinite(length2) || length2 < kMinStepWu * kMinStepWu)
        return false;
    const float d[3]{at[0] - (newest[0] - step[0]), at[1] - (newest[1] - step[1]), at[2] - (newest[2] - step[2])};
    fraction = Dot(d, step) / length2;
    const float off[3]{d[0] - fraction * step[0], d[1] - fraction * step[1], d[2] - fraction * step[2]};
    offLineWu = std::sqrt(Dot(off, off));
    return std::isfinite(fraction) && std::isfinite(offLineWu);
}
inline bool CameraFraction(const Sample& s, const float step[3], float& fraction, float& offLineWu) noexcept
{
    return StepFraction(s.placed, s.atTick, step, fraction, offLineWu);
}

struct Window
{
    uint32_t frames = 0, ticks0 = 0, ticks1 = 0, ticks2 = 0, moving = 0;
    uint32_t on = 0, behindPart = 0, behindTick = 0, aheadPart = 0, aheadTick = 0;
    uint32_t repeated = 0, plainMovedNoTick = 0, threads = 0;
    uint32_t threadIds[4]{};
    uint32_t carried = 0, hullMeasured = 0, hullAtFactor = 0;
    double errSum = 0, stepSumMm = 0, aheadSumMm = 0;
    float aheadMaxMm = 0, aheadMinMm = 0;
    float worst = 0, worstDtMs = 0, worstFactor = 0, worstPrevFactor = 0, worstFraction = 0;
    int worstTicks = 0;
    float offLineMaxMm = 0, headOnHullMaxMm = 0;

    void Add(const Sample& s, Carry& carry) noexcept
    {
        ++frames;
        if (s.carried)
            ++carried;
        bool known = false;
        for (uint32_t i = 0; i < threads; ++i)
            known = known || threadIds[i] == s.thread;
        if (!known && threads < 4)
            threadIds[threads++] = s.thread;
        const bool clock = s.tick >= 0 && std::isfinite(s.factor);
        if (!clock || !carry.valid || carry.last.tick < 0)
        {
            carry.valid = clock;
            carry.stepValid = false;
            carry.last = s;
            return;
        }
        const Sample& prev = carry.last;
        const int64_t ran = s.tick - prev.tick;
        if (ran == 0)
        {
            ++ticks0;
            if (std::memcmp(s.atTick, prev.atTick, sizeof(s.atTick)) != 0)
                ++plainMovedNoTick;
        }
        else if (ran >= 1 && ran <= 4)
        {
            ran == 1 ? ++ticks1 : ++ticks2;
            for (int a = 0; a < 3; ++a)
            {
                carry.step[a] = (s.atTick[a] - prev.atTick[a]) / float(ran);
                carry.hullStep[a] = (s.hull[a] - prev.hull[a]) / float(ran);
            }
            carry.stepValid = true;
            carry.hullStepValid = s.hullValid && prev.hullValid;
        }
        else
            carry.stepValid = carry.hullStepValid = false;
        float fraction = 0, offLine = 0;
        if (carry.stepValid && CameraFraction(s, carry.step, fraction, offLine))
        {
            ++moving;
            stepSumMm += double(std::sqrt(Dot(carry.step, carry.step))) * kMmPerWu;
            const float err = fraction - s.factor;
            errSum += err;
            if (std::fabs(err) <= 0.2f)
                ++on;
            else if (err <= -0.75f)
                ++behindTick;
            else if (err < 0.0f)
                ++behindPart;
            else if (err >= 0.75f)
                ++aheadTick;
            else
                ++aheadPart;
            if (std::memcmp(s.placed, prev.placed, sizeof(s.placed)) == 0)
                ++repeated;
            if (std::fabs(err) > std::fabs(worst))
            {
                worst = err;
                worstDtMs = float(s.us - prev.us) * 0.001f;
                worstTicks = int(ran);
                worstFactor = s.factor;
                worstPrevFactor = prev.factor;
                worstFraction = fraction;
            }
            if (offLine * kMmPerWu > offLineMaxMm)
                offLineMaxMm = offLine * kMmPerWu;
            // The drawn hull's own fraction of its step, and how far ahead of it the camera's marker sits.
            float hullFraction = 0, hullOff = 0;
            if (s.hullValid && carry.hullStepValid &&
                StepFraction(s.hullDrawn, s.hull, carry.hullStep, hullFraction, hullOff))
            {
                const float aheadMm = (fraction - hullFraction) * float(std::sqrt(Dot(carry.step, carry.step))) * kMmPerWu;
                if (!hullMeasured || aheadMm > aheadMaxMm)
                    aheadMaxMm = aheadMm;
                if (!hullMeasured || aheadMm < aheadMinMm)
                    aheadMinMm = aheadMm;
                ++hullMeasured;
                aheadSumMm += aheadMm;
                if (std::fabs(hullFraction - s.factor) <= 0.05f)
                    ++hullAtFactor;
            }
            if (s.hullValid && prev.hullValid && ran == 0)
            {
                // Between frames of one tick the simulation holds both still: any change is a query that moved.
                const float d[3]{(s.atTick[0] - s.hull[0]) - (prev.atTick[0] - prev.hull[0]),
                                 (s.atTick[1] - s.hull[1]) - (prev.atTick[1] - prev.hull[1]),
                                 (s.atTick[2] - s.hull[2]) - (prev.atTick[2] - prev.hull[2])};
                const float mm = std::sqrt(Dot(d, d)) * kMmPerWu;
                if (mm > headOnHullMaxMm)
                    headOnHullMaxMm = mm;
            }
        }
        carry.last = s;
    }

    bool Format(char* out, size_t size) const
    {
        if (!out || !size)
            return false;
        out[0] = '\0';
        if (!frames)
            return false;
        int used = std::snprintf(out, size, "%u frames on %u thread(s), ticks per frame 0/1/2+ %u/%u/%u", frames,
                                 threads, ticks0, ticks1, ticks2);
        if (used <= 0 || static_cast<size_t>(used) >= size)
            return true;
        if (!moving)
        {
            std::snprintf(out + used, size - static_cast<size_t>(used), "; the vehicle never moved 30 mm in a tick");
            return true;
        }
        if (hullMeasured)
        {
            const int more = std::snprintf(
                out + used, size - static_cast<size_t>(used),
                "; the camera's marker sat ahead of the DRAWN hull by %.0f mm on average, between %+.0f and %+.0f mm "
                "(%u frames; the hull was drawn at the frame's factor on %u; the marker was carried by the drawn "
                "vehicle on %u of %u)",
                aheadSumMm / hullMeasured, double(aheadMinMm), double(aheadMaxMm), hullMeasured, hullAtFactor, carried,
                frames);
            if (more > 0 && static_cast<size_t>(used + more) < size)
                used += more;
        }
        std::snprintf(out + used, size - static_cast<size_t>(used),
                      "; %u while the vehicle moved (mean %.0f mm a tick): the camera sat at the frame's factor "
                      "between the last two ticks on %u (within 0.2 of a tick), behind it by under 0.75 of a tick "
                      "on %u and by more on %u, ahead by under 0.75 on %u and by more on %u; camera fraction minus "
                      "factor mean %+.2f, worst %+.2f (that frame: %.1f ms after the one before, %d tick(s), "
                      "factor %.2f after %.2f, camera at %+.2f); the camera repeated the frame before on %u; off "
                      "the line between the ticks by at most %.0f mm; the plain marker moved without a tick on %u "
                      "(against the root vehicle by at most %.0f mm)",
                      moving, stepSumMm / moving, on, behindPart, behindTick, aheadPart, aheadTick, errSum / moving,
                      double(worst), double(worstDtMs), worstTicks, double(worstFactor), double(worstPrevFactor),
                      double(worstFraction), repeated, double(offLineMaxMm), plainMovedNoTick,
                      double(headOnHullMaxMm));
        return true;
    }
};

inline bool Summarise(const Sample* rows, unsigned count, Carry& carry, char* out, size_t size)
{
    Window w{};
    for (unsigned k = 0; k < count; ++k)
        w.Add(rows[k], carry);
    return w.Format(out, size);
}
}
