#pragma once

// AS SEEN -- an impact against the crosshair the player is LOOKING AT when the impact first appears (dev).
//
// The SHOT IMPACT witness (shot_impact_logic.h) proves where the rounds go: onto P, the point the player meant at the
// fire. That is not what the player judges. He judges the impact against the crosshair ON SCREEN when the impact
// appears, and by then he has strafed on. This file is that measurement, written once and used by two hosts so the
// two numbers are computed identically:
//
//   * the VR dev build (game.cpp halo3_impact_witness.inl): the reticle quad the compositor drew in the first
//     prepared frame whose render follows the impact, mapped into the world through that frame's own camera; and the
//     game's own (stock) crosshair line in that frame: the engine camera's position and aim forward;
//   * the flat probe (src/probe/flat_probe.cpp, HaloMCCFlatProbe.dll): stock Halo 3 with nothing of ours in the
//     game, the camera of the first presented frame rendered after the impact, along its forward.
//
// For each impact:
//   SEEN    the impact against the crosshair line of that frame, from that frame's eye: the offset off the line at
//           the impact's own depth, split ALONG the travel (the eye's velocity across the line; + = ahead of where
//           the player is going) and UP (the rest of it, + = up), in mm and in degrees, and in RETICLE RADII (the
//           host's own reticle size, see Window::Add);
//   STOCK   the same against the stock crosshair line of that frame (in flat it IS the seen line);
//   AT FIRE the same against the aim line at the fire (VR: P's line, its rendered eye through P; flat: the crosshair
//           of the newest frame rendered before the fire) -- where the round went against what was aimed at;
//   CLOCKS  the aim's camera sample -> the fire -> the impact -> the first frame's camera -> its present, in ms of one
//           clock, the sim ticks between the fire and the impact, and how far the CAMERA EYE moved along the travel
//           between the aim's camera sample and the frame's camera. When the seen offset is the camera's own advance,
//           "eye moved" is minus "along", and the ms say which clock difference made it.
//
// Every metric is binned by strafe speed and by range, so flat and VR are compared bin for bin without anyone
// knowing their distance to the wall.
//
// Pure functions and plain structs only: unit-tested in tests/as_seen_tests.cpp. World units are Blam's (1 wu =
// 3.048 m, Z up); velocities wu/s; times microseconds of the host's one clock.

#include "shot_lag_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace as_seen
{
using shot_lag::Dot;
using shot_lag::Finite3;
using shot_lag::kMetresPerWu;
using shot_lag::kMmPerWu;
using shot_lag::kRadToDeg;
using shot_lag::kWuPerMetre;
using shot_lag::Length;

// ---- 1. the bins -------------------------------------------------------------------------------------------------
inline constexpr int kRangeBins = 5;
inline constexpr float kRangeEdgesM[kRangeBins - 1]{5.0f, 10.0f, 20.0f, 40.0f};
inline constexpr int kSpeedBins = 3;
inline constexpr float kSpeedEdgesMps[kSpeedBins - 1]{1.0f, 4.0f};

inline int RangeBin(float metres)
{
    if (!std::isfinite(metres))
        return kRangeBins - 1;
    int bin = 0;
    while (bin < kRangeBins - 1 && metres >= kRangeEdgesM[bin])
        ++bin;
    return bin;
}
inline int SpeedBin(float mps)
{
    if (!std::isfinite(mps))
        return 0;
    int bin = 0;
    while (bin < kSpeedBins - 1 && mps >= kSpeedEdgesMps[bin])
        ++bin;
    return bin;
}
inline const char* RangeLabel(int bin)
{
    static const char* const kLabels[kRangeBins]{"0-5 m", "5-10 m", "10-20 m", "20-40 m", "40+ m"};
    return bin >= 0 && bin < kRangeBins ? kLabels[bin] : "?";
}
inline const char* SpeedLabel(int bin)
{
    static const char* const kLabels[kSpeedBins]{"under 1 m/s", "1-4 m/s", "4+ m/s"};
    return bin >= 0 && bin < kSpeedBins ? kLabels[bin] : "?";
}

// ---- 2. the axes across a line -----------------------------------------------------------------------------------
// The plane across `dir`: TRAVEL (the velocity's part across the line, when it is over 5 cm/s; else the horizontal
// right of the line, and `travel` false) and UP (dir x travel, turned to point up). Halo's world is Z up.
struct Across
{
    bool valid = false;
    bool travel = false;
    float d[3]{};      // the line's unit direction
    float t[3]{};      // the travel axis (or the horizontal right)
    float n[3]{};      // the up axis across the line
    float speedMps = 0.0f;   // the velocity across the line (game metres per second); 0 without travel
};

inline Across AxesAcross(const float dir[3], const float* velocity)
{
    Across a{};
    if (!Finite3(dir))
        return a;
    const float dl = Length(dir);
    if (!std::isfinite(dl) || !(dl > 1.0e-6f))
        return a;
    for (int axis = 0; axis < 3; ++axis)
        a.d[axis] = dir[axis] / dl;
    bool haveT = false;
    if (velocity && Finite3(velocity))
    {
        const float along = Dot(velocity, a.d);
        const float vp[3]{velocity[0] - a.d[0] * along, velocity[1] - a.d[1] * along, velocity[2] - a.d[2] * along};
        const float vpl = Length(vp);
        if (std::isfinite(vpl) && vpl * kMetresPerWu > 0.05f)
        {
            for (int axis = 0; axis < 3; ++axis)
                a.t[axis] = vp[axis] / vpl;
            a.speedMps = vpl * kMetresPerWu;
            a.travel = true;
            haveT = true;
        }
    }
    if (!haveT)
    {
        // The horizontal right of the line: d x up.
        const float r[3]{a.d[1], -a.d[0], 0.0f};
        const float rl = Length(r);
        if (!(rl > 1.0e-6f))
        {
            a.t[0] = 1.0f;   // looking straight up or down: any horizontal axis
            a.t[1] = a.t[2] = 0.0f;
        }
        else
            for (int axis = 0; axis < 3; ++axis)
                a.t[axis] = r[axis] / rl;
    }
    // n = d x t, then made to point up (or, for a vertical travel axis, kept as it is).
    a.n[0] = a.d[1] * a.t[2] - a.d[2] * a.t[1];
    a.n[1] = a.d[2] * a.t[0] - a.d[0] * a.t[2];
    a.n[2] = a.d[0] * a.t[1] - a.d[1] * a.t[0];
    if (a.n[2] < 0.0f)
        for (float& c : a.n)
            c = -c;
    a.valid = Finite3(a.t) && Finite3(a.n);
    return a;
}

// ---- 3. a point against a drawn line -----------------------------------------------------------------------------
struct Offset
{
    bool valid = false;
    bool travel = false;
    float rangeM = 0.0f;     // |point - origin|
    float depthM = 0.0f;     // along the line
    float angleDeg = 0.0f;   // between (point - origin) and the line
    float alongMm = 0.0f;    // off the line at the point's depth, along the travel (+ = ahead of the player's motion)
    float upMm = 0.0f;       // off the line at the point's depth, along the up axis across it (+ = up)
    float alongDeg = 0.0f;   // atan(along / depth)
    float upDeg = 0.0f;      // atan(up / depth)
    float speedMps = 0.0f;   // the eye's velocity across the line
};

// `point` (the impact), the line from `origin` (the eye that drew it) along `dir`, `velocity` the eye's (wu/s; null
// = no travel axis: "along" is then the horizontal right).
inline Offset Measure(const float point[3], const float origin[3], const float dir[3], const float* velocity)
{
    Offset o{};
    if (!Finite3(point) || !Finite3(origin))
        return o;
    const Across a = AxesAcross(dir, velocity);
    if (!a.valid)
        return o;
    const float w[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    const float range = Length(w);
    const float depth = Dot(w, a.d);
    if (!std::isfinite(range) || !(range > 1.0e-4f) || !std::isfinite(depth))
        return o;
    const float lateral[3]{w[0] - a.d[0] * depth, w[1] - a.d[1] * depth, w[2] - a.d[2] * depth};
    const float along = Dot(lateral, a.t), up = Dot(lateral, a.n);
    o.travel = a.travel;
    o.speedMps = a.speedMps;
    o.rangeM = range * kMetresPerWu;
    o.depthM = depth * kMetresPerWu;
    o.angleDeg = std::atan2(Length(lateral), depth) * kRadToDeg;
    o.alongMm = along * kMmPerWu;
    o.upMm = up * kMmPerWu;
    o.alongDeg = std::atan2(along, depth) * kRadToDeg;
    o.upDeg = std::atan2(up, depth) * kRadToDeg;
    o.valid = std::isfinite(o.angleDeg) && std::isfinite(o.alongMm) && std::isfinite(o.upMm) && depth > 0.0f;
    return o;
}

// A displacement (wu) along the travel axis of the line `dir` with `velocity`, in mm (0 when there is no travel).
inline float AlongTravelMm(const float delta[3], const float dir[3], const float* velocity, bool* known = nullptr)
{
    const Across a = AxesAcross(dir, velocity);
    const bool ok = a.valid && a.travel && delta && Finite3(delta);
    if (known)
        *known = ok;
    return ok ? Dot(delta, a.t) * kMmPerWu : 0.0f;
}

// ---- 4. one impact -----------------------------------------------------------------------------------------------
struct Sample
{
    Offset seen{};          // vs the crosshair drawn in the first frame showing the impact
    bool stockValid = false;
    Offset stock{};         // vs the stock crosshair line of that frame
    bool atFireValid = false;
    Offset atFire{};        // vs the aim line at the fire
    // One clock (us); 0 = unknown.
    uint64_t aimCamUs = 0, fireUs = 0, impactUs = 0, frameCamUs = 0, presentUs = 0;
    int32_t ticksFireToImpact = -1;   // sim ticks from the fire to the impact
    int32_t ticksAimToFrame = -1;     // sim ticks from the aim's camera sample to the frame's camera
    int32_t ticksImpactToFrame = -1;  // sim ticks from the impact to the frame's camera (a tick counter; -1 unknown)
    // The frame camera's interpolation fraction (Halo 2: MCC renders the camera at lerp(previous tick, newest tick,
    // alpha), alpha the part of a tick accumulated past the newest one); NaN = unknown or not interpolated.
    float frameAlpha = NAN;
    bool eyeMovedKnown = false;
    float eyeMovedAlongMm = 0.0f;     // the camera eye, aim sample -> frame, along `seen`'s travel axis
};

inline float MsBetween(uint64_t fromUs, uint64_t toUs)
{
    if (!fromUs || !toUs)
        return NAN;
    return toUs >= fromUs ? float(toUs - fromUs) / 1000.0f : -float(fromUs - toUs) / 1000.0f;
}

// ---- 5. the window: per speed bin and range bin ------------------------------------------------------------------
struct Bin
{
    uint32_t n = 0;
    double rangeM = 0, speedMps = 0;
    double seenAlongMm = 0, seenUpMm = 0, seenAlongDeg = 0, seenUpDeg = 0, seenAngleDeg = 0;
    uint32_t nRadii = 0;
    double seenAlongRadii = 0, seenAngleRadii = 0;
    uint32_t nImplied = 0;
    double impliedMs = 0;              // -seen along / the travel speed: the offset as time at the strafe speed
    uint32_t nStock = 0;
    double stockAlongMm = 0, stockAlongDeg = 0, stockUpMm = 0;
    uint32_t nAtFire = 0;
    double atFireAlongMm = 0, atFireUpMm = 0;
    uint32_t nEye = 0;
    double eyeMovedMm = 0, eyeMovedMs = 0;
    uint32_t nAimFire = 0, nFireImpact = 0, nImpactFrame = 0, nFramePresent = 0;
    double aimToFireMs = 0, fireToImpactMs = 0, impactToFrameMs = 0, frameToPresentMs = 0;
    uint32_t nTicksFire = 0, nTicksAim = 0, nTicksImpact = 0;
    double ticksFireToImpact = 0, ticksAimToFrame = 0, ticksImpactToFrame = 0;
    uint32_t nAlpha = 0;
    double frameAlpha = 0;
};

struct Window
{
    Bin bin[kSpeedBins][kRangeBins]{};
    uint32_t impacts = 0;       // impacts that reached a frame
    uint32_t unframed = 0;      // impacts no frame showed in time (host counts)
    uint32_t noLine = 0;        // impacts whose frame had no crosshair line (host counts)
    uint32_t refused = 0;       // a sample that did not measure

    // `radiusDeg`: the host's reticle radius as drawn (<= 0: no radii column).
    void Add(const Sample& s, float radiusDeg)
    {
        if (!s.seen.valid)
        {
            ++refused;
            return;
        }
        ++impacts;
        Bin& b = bin[SpeedBin(s.seen.travel ? s.seen.speedMps : 0.0f)][RangeBin(s.seen.rangeM)];
        ++b.n;
        b.rangeM += s.seen.rangeM;
        b.speedMps += s.seen.speedMps;
        b.seenAlongMm += s.seen.alongMm;
        b.seenUpMm += s.seen.upMm;
        b.seenAlongDeg += s.seen.alongDeg;
        b.seenUpDeg += s.seen.upDeg;
        b.seenAngleDeg += s.seen.angleDeg;
        if (std::isfinite(radiusDeg) && radiusDeg > 0.0f)
        {
            ++b.nRadii;
            b.seenAlongRadii += s.seen.alongDeg / radiusDeg;
            b.seenAngleRadii += s.seen.angleDeg / radiusDeg;
        }
        if (s.seen.travel && s.seen.speedMps > 0.5f)
        {
            ++b.nImplied;
            b.impliedMs += -s.seen.alongMm / s.seen.speedMps;   // mm / (m/s) = ms
        }
        if (s.stockValid && s.stock.valid)
        {
            ++b.nStock;
            b.stockAlongMm += s.stock.alongMm;
            b.stockAlongDeg += s.stock.alongDeg;
            b.stockUpMm += s.stock.upMm;
        }
        if (s.atFireValid && s.atFire.valid)
        {
            ++b.nAtFire;
            b.atFireAlongMm += s.atFire.alongMm;
            b.atFireUpMm += s.atFire.upMm;
        }
        if (s.eyeMovedKnown && std::isfinite(s.eyeMovedAlongMm))
        {
            ++b.nEye;
            b.eyeMovedMm += s.eyeMovedAlongMm;
            if (s.seen.travel && s.seen.speedMps > 0.5f)
                b.eyeMovedMs += s.eyeMovedAlongMm / s.seen.speedMps;
        }
        const auto add = [](uint32_t& n, double& sum, float ms) {
            if (std::isfinite(ms))
            {
                ++n;
                sum += ms;
            }
        };
        add(b.nAimFire, b.aimToFireMs, MsBetween(s.aimCamUs, s.fireUs));
        add(b.nFireImpact, b.fireToImpactMs, MsBetween(s.fireUs, s.impactUs));
        add(b.nImpactFrame, b.impactToFrameMs, MsBetween(s.impactUs, s.frameCamUs));
        add(b.nFramePresent, b.frameToPresentMs, MsBetween(s.frameCamUs, s.presentUs));
        if (s.ticksFireToImpact >= 0)
        {
            ++b.nTicksFire;
            b.ticksFireToImpact += s.ticksFireToImpact;
        }
        if (s.ticksAimToFrame >= 0)
        {
            ++b.nTicksAim;
            b.ticksAimToFrame += s.ticksAimToFrame;
        }
        if (s.ticksImpactToFrame >= 0)
        {
            ++b.nTicksImpact;
            b.ticksImpactToFrame += s.ticksImpactToFrame;
        }
        if (std::isfinite(s.frameAlpha))
        {
            ++b.nAlpha;
            b.frameAlpha += s.frameAlpha;
        }
    }

    bool Any() const { return impacts || unframed || noLine || refused; }

    // One line: "AS SEEN <label> (dev): ..." with every non-empty bin. False when there is nothing to say.
    bool Format(const char* label, char* out, size_t size) const
    {
        if (!out || !size)
            return false;
        out[0] = '\0';
        if (!Any())
            return false;
        size_t used = 0;
        const auto put = [&](const char* fmt, auto... args) {
            if (used + 1 >= size)
                return;
            const int w = std::snprintf(out + used, size - used, fmt, args...);
            if (w > 0)
                used = used + static_cast<size_t>(w) >= size ? size - 1 : used + static_cast<size_t>(w);
        };
        const auto mean = [](double sum, uint32_t n) { return n ? sum / static_cast<double>(n) : 0.0; };
        put("AS SEEN %s (dev): %u impacts shown", label ? label : "?", impacts);
        if (unframed || noLine || refused)
            put(" (%u never reached a frame, %u frames without a crosshair line, %u refused)", unframed, noLine,
                refused);
        for (int s = 0; s < kSpeedBins; ++s)
            for (int r = 0; r < kRangeBins; ++r)
            {
                const Bin& b = bin[s][r];
                if (!b.n)
                    continue;
                put(" | [%s, %s] %u at %.1f m, strafe %.1f m/s: SEEN along %+.0f mm (%+.2f deg", SpeedLabel(s),
                    RangeLabel(r), b.n, mean(b.rangeM, b.n), mean(b.speedMps, b.n), mean(b.seenAlongMm, b.n),
                    mean(b.seenAlongDeg, b.n));
                if (b.nRadii)
                    put(", %+.2f radii", mean(b.seenAlongRadii, b.nRadii));
                if (b.nImplied)
                    put(", = %.0f ms of strafe", mean(b.impliedMs, b.nImplied));
                put(") up %+.0f mm (%+.2f deg), total %.2f deg", mean(b.seenUpMm, b.n), mean(b.seenUpDeg, b.n),
                    mean(b.seenAngleDeg, b.n));
                if (b.nRadii)
                    put(" (%.2f radii)", mean(b.seenAngleRadii, b.nRadii));
                if (b.nStock)
                    put("; STOCK along %+.0f mm (%+.2f deg) up %+.0f mm", mean(b.stockAlongMm, b.nStock),
                        mean(b.stockAlongDeg, b.nStock), mean(b.stockUpMm, b.nStock));
                if (b.nAtFire)
                    put("; AT FIRE along %+.0f up %+.0f mm", mean(b.atFireAlongMm, b.nAtFire),
                        mean(b.atFireUpMm, b.nAtFire));
                if (b.nEye)
                    put("; camera eye aim->frame %+.0f mm (%+.0f ms)", mean(b.eyeMovedMm, b.nEye),
                        mean(b.eyeMovedMs, b.nEye));
                put("; clocks aim cam->fire %.1f, fire->impact %.1f", mean(b.aimToFireMs, b.nAimFire),
                    mean(b.fireToImpactMs, b.nFireImpact));
                if (b.nTicksFire)
                    put(" (%.1f ticks)", mean(b.ticksFireToImpact, b.nTicksFire));
                put(", impact->frame %.1f", mean(b.impactToFrameMs, b.nImpactFrame));
                if (b.nFramePresent)
                    put(", frame->present %.1f", mean(b.frameToPresentMs, b.nFramePresent));
                put(" ms");
                if (b.nTicksAim)
                    put("; ticks aim cam->frame %.1f", mean(b.ticksAimToFrame, b.nTicksAim));
                if (b.nTicksImpact)
                    put("; ticks impact->frame %.1f", mean(b.ticksImpactToFrame, b.nTicksImpact));
                if (b.nAlpha)
                    put("; frame camera alpha %.2f (drawn that far from the previous tick to the newest)",
                        mean(b.frameAlpha, b.nAlpha));
            }
        return true;
    }
};

// ---- 6. is the camera interpolated? (hypothesis a) ---------------------------------------------------------------
// Per drawn frame: did the camera eye move since the previous frame, while the player is moving? A camera rendered
// straight from the 60 Hz simulation repeats its position on every frame drawn between two ticks (at 90 Hz about one
// frame in three) and steps by whole ticks; an interpolated camera moves on every frame by about speed x frame time.
// `stepRatio` = |step| / (speed x frame time): ~1 every frame when interpolated, 0 and ~1.5 alternating when not.
//
// With the simulation's TICK COUNTER at each frame (Halo 2) the question is answered directly instead of
// from the step pattern: per frame, how many ticks ran since the previous frame (0 / 1 / 2+), and on the frames where
// NO tick ran while the player moved, did the eye still move (interpolated between ticks) or repeat? A frame rate
// below the tick rate never has a tick-less frame, so the step ratio is the evidence there. `alpha`: the frame
// camera's interpolation fraction where the host can read it (mean reported).
struct CameraCensus
{
    static constexpr int kHistory = 16;
    float pos[kHistory][3]{};
    uint64_t us[kHistory]{};
    int count = 0, head = 0;   // ring: head = the next write
    int64_t lastTick = -1;     // the previous frame's tick (-1 unknown)
    // Counts (reset by TakeCounts / a window swap).
    uint32_t frames = 0, moving = 0, repeated = 0, uneven = 0, stepN = 0;
    double stepRatioSum = 0, speedSum = 0, frameMsSum = 0;
    // With ticks: frames that ran 0 / 1 / 2+ ticks since the previous frame; of the moving frames that ran none, how
    // many still moved the eye (and how many repeated it); and the moving frames that DID run a tick but repeated.
    uint32_t ticked = 0, ticks0 = 0, ticks1 = 0, ticks2 = 0;
    uint32_t movingNoTick = 0, movedNoTick = 0, repeatedWithTick = 0;
    uint32_t alphaN = 0;
    double alphaSum = 0;

    void Add(const float p[3], uint64_t nowUs, int64_t tick = -1, float alpha = NAN)
    {
        if (!Finite3(p) || !nowUs)
            return;
        ++frames;
        int ran = -1;   // ticks since the previous frame (-1 unknown)
        if (tick >= 0)
        {
            if (lastTick >= 0 && tick >= lastTick)
            {
                ran = static_cast<int>(tick - lastTick > 1000 ? 1000 : tick - lastTick);
                ++ticked;
                if (ran == 0)
                    ++ticks0;
                else if (ran == 1)
                    ++ticks1;
                else
                    ++ticks2;
            }
            lastTick = tick;
        }
        else
            lastTick = -1;
        if (std::isfinite(alpha))
        {
            ++alphaN;
            alphaSum += alpha;
        }
        if (count)
        {
            const int prev = (head + kHistory - 1) % kHistory;
            if (nowUs > us[prev])
            {
                // The speed over the history (at least 40 ms back), independent of this one step.
                int oldest = -1;
                for (int k = 1; k <= count; ++k)
                {
                    const int i = (head + kHistory - k) % kHistory;
                    if (nowUs - us[i] >= 40000)
                    {
                        oldest = i;
                        break;
                    }
                }
                if (oldest >= 0)
                {
                    const float span[3]{p[0] - pos[oldest][0], p[1] - pos[oldest][1], p[2] - pos[oldest][2]};
                    const float speedMps = Length(span) * kMetresPerWu / (float(nowUs - us[oldest]) * 1.0e-6f);
                    if (std::isfinite(speedMps) && speedMps > 1.0f && speedMps < 50.0f)
                    {
                        ++moving;
                        const float step[3]{p[0] - pos[prev][0], p[1] - pos[prev][1], p[2] - pos[prev][2]};
                        const bool same = step[0] == 0.0f && step[1] == 0.0f && step[2] == 0.0f;
                        if (same)
                            ++repeated;
                        if (ran == 0)
                        {
                            ++movingNoTick;
                            if (!same)
                                ++movedNoTick;
                        }
                        else if (ran > 0 && same)
                            ++repeatedWithTick;
                        const float dt = float(nowUs - us[prev]) * 1.0e-6f;
                        const float ratio = Length(step) * kMetresPerWu / (speedMps * dt);
                        if (std::isfinite(ratio))
                        {
                            ++stepN;
                            stepRatioSum += ratio;
                            speedSum += speedMps;
                            frameMsSum += dt * 1000.0f;
                            if (ratio < 0.75f || ratio > 1.25f)
                                ++uneven;
                        }
                    }
                }
            }
        }
        std::memcpy(pos[head], p, sizeof(pos[head]));
        us[head] = nowUs;
        head = (head + 1) % kHistory;
        if (count < kHistory)
            ++count;
    }

    void ResetCounts()
    {
        frames = moving = repeated = uneven = stepN = 0;
        stepRatioSum = speedSum = frameMsSum = 0;
        ticked = ticks0 = ticks1 = ticks2 = movingNoTick = movedNoTick = repeatedWithTick = alphaN = 0;
        alphaSum = 0;
    }

    // One clause, or false while nothing moved.
    bool Format(char* out, size_t size) const
    {
        if (!out || !size)
            return false;
        out[0] = '\0';
        if (!moving || !stepN)
            return false;
        int used = std::snprintf(out, size,
                                 "%u frames, %u while moving (mean %.1f m/s, frame %.1f ms): the camera eye REPEATED on "
                                 "%u (%.0f%%), step / (speed x frame time) mean %.2f, outside 0.75-1.25 on %u (%.0f%%)",
                                 frames, moving, speedSum / stepN, frameMsSum / stepN, repeated,
                                 100.0 * repeated / moving, stepRatioSum / stepN, uneven, 100.0 * uneven / stepN);
        if (ticked && used > 0 && static_cast<size_t>(used) < size)
            used += std::snprintf(out + used, size - static_cast<size_t>(used),
                                  "; sim ticks per frame 0/1/2+: %u/%u/%u; moving frames with NO new tick: %u, the "
                                  "eye moved on %u (= interpolated between ticks) and repeated on %u; moving frames "
                                  "with a tick that repeated: %u",
                                  ticks0, ticks1, ticks2, movingNoTick, movedNoTick, movingNoTick - movedNoTick,
                                  repeatedWithTick);
        if (alphaN && used > 0 && static_cast<size_t>(used) < size)
            std::snprintf(out + used, size - static_cast<size_t>(used),
                          "; interpolation alpha mean %.2f (%u frames)", alphaSum / alphaN, alphaN);
        return true;
    }
};

// ---- 6b. which collision query is a tracked round's ---------------------------------------------------------------
// A projectile's own collision query starts ON its spawn line (within 15 cm of it, not more than 2 cm behind its spawn)
// and runs ALONG its forward (within 5 deg). True with `missM` (its start's distance off the line, game metres) when it
// does; no object table is read, so the flat probe and the VR witness match by the same geometry.
inline bool QueryOnRound(const float spawn[3], const float forward[3], const float start[3], const float vector[3],
                         float* missM = nullptr)
{
    if (!Finite3(spawn) || !Finite3(forward) || !Finite3(start) || !Finite3(vector))
        return false;
    const float fl = Length(forward), vl = Length(vector);
    if (!(fl > 1.0e-6f) || !(vl > 1.0e-6f))
        return false;
    const float f[3]{forward[0] / fl, forward[1] / fl, forward[2] / fl};
    const float cosine = Dot(vector, f) / vl;
    if (!(cosine >= std::cos(5.0f / kRadToDeg)))
        return false;
    const float w[3]{start[0] - spawn[0], start[1] - spawn[1], start[2] - spawn[2]};
    const float along = Dot(w, f);
    if (along < -0.02f * kWuPerMetre)
        return false;
    const float m[3]{w[0] - f[0] * along, w[1] - f[1] * along, w[2] - f[2] * along};
    const float miss = Length(m) * kMetresPerWu;
    if (!(miss <= 0.15f))
        return false;
    if (missM)
        *missM = miss;
    return true;
}

// ---- 7. the flat reticle's size, art for art ---------------------------------------------------------------------
// The VR reticle shows the game's own crosshair art captured from the central +-(128 / hudWidth) of the HUD's NDC
// width (vr.cpp: Halo 3's capture viewport is the HUD viewport x4, centred in a 512 texture) on a quad
// crosshair_size_deg wide, so its "radius" (the quad's half-angle) frames exactly that art. The same art in flat
// spans atan(that NDC half-width x the flat projection's horizontal tangent). Radii in both hosts are then fractions
// of the same piece of art -- the assumption being that the HUD lays the crosshair out in NDC (it scales with the
// raster), which the VR capture already relies on.
inline float FlatArtHalfAngleDeg(float vrHudWidthPx, float flatTanHalfX)
{
    if (!std::isfinite(vrHudWidthPx) || !(vrHudWidthPx > 0.0f) || !std::isfinite(flatTanHalfX) ||
        !(flatTanHalfX > 0.0f))
        return NAN;
    return std::atan(128.0f / vrHudWidthPx * flatTanHalfX) * kRadToDeg;
}

// ---- 8. MCC's frame clock in Halo Reach and Halo 4 (decoded from code) --------------------------------------------
// Both pinned modules run the same frame loop (static analysis of the retail haloreach.dll / halo4.dll): the tick loop
// (Reach 0x57434, Halo 4 0x9A380) sets the frame interpolator's IN-TICK byte (state +2 / +3) before every tick and
// rotates its two banks, the post-tick record (Reach 0xCE100, Halo 4 0x133318) records each user's camera and clears
// that byte, then the frame function writes the BLEND FACTOR -- clamp(game_time + 0x14, 0, 1), the part of a tick
// accumulated past the newest one (Reach 0x5C430, Halo 4 0xA0458) -- into state +4 (Reach 0x344A5, Halo 4 0x61172)
// right BEFORE the director update (Reach 0xC4458, Halo 4 0x10A448) builds every camera. Outside a tick the unit's
// camera position (Reach 0x48A1C0 -> 0x4A2D50, Halo 4 0x5F8A1C -> 0x6320D0) reads the object's origin back as
// lerp(previous tick, newest tick, factor) (Reach 0x4719C8, Halo 4 0x5D7E08) and its camera height the same way
// (Reach 0xCF408, Halo 4 0x1343E4); inside a tick it answers that tick's raw values and records the height (Reach
// 0xCF354, Halo 4 0x134330). The game_time counter at +0x0C steps once at the END of each tick (Reach 0x57A63, Halo 4
// 0x9ADC8). Every location below is DECODED from those instructions' own bytes, never assumed, so the hosts read the
// clock the engine writes. The bytes are the pinned retail sequences (each matched exactly once in its module).
//
//   fraction leaf   8B 0D <rel32 tls index> 0F 57 E4 65 48 8B 04 25 58 00 00 00 BA <imm32 game_time slot>
//                   48 8B 04 C8 48 8B 0C 10 F3 0F 10 59 <disp8 fraction>
//   factor writer   E8 <rel32 fraction leaf> B8 <imm32 interpolator slot> 4A 8B 04 ?? F3 0F 11 40 <disp8 factor>
//   tick increment  Reach: B8 <imm32 game_time slot> 48 8B 04 18 FF 40 <disp8 tick>
//                   Halo 4: B8 <imm32 game_time slot> 48 8B 0C 38 44 01 79 <disp8 tick>
struct FrameClockLayout
{
    bool valid = false;
    uint32_t tlsIndexRva = 0;    // the module's implicit TLS index (gs:[0x58][index] is this thread's TLS block)
    uint32_t gameTimeSlot = 0;   // TLS block + this: game_time_globals*
    uint32_t fractionOffset = 0; // game_time + this: the tick fraction (float)
    uint32_t tickOffset = 0;     // game_time + this: the tick counter (int32; steps at the end of a tick)
    uint32_t interpSlot = 0;     // TLS block + this: the frame interpolator's state*
    uint32_t factorOffset = 0;   // state + this: the blend factor the frame's cameras were built at (float)
    uint32_t secondsPerTickOffset = 0;   // game_time + this: seconds per tick (float); 0 = not decoded (optional)
};

inline int32_t ReadRel32(const uint8_t* p)
{
    int32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline uint32_t ReadU32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// `code` holds the bytes at module RVA `rva`; false unless every fixed byte matches.
inline bool DecodeFractionLeaf(const uint8_t* code, size_t size, uint32_t rva, FrameClockLayout& out)
{
    static constexpr uint8_t kMid[]{0x0F, 0x57, 0xE4, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xBA};
    static constexpr uint8_t kTail[]{0x48, 0x8B, 0x04, 0xC8, 0x48, 0x8B, 0x0C, 0x10, 0xF3, 0x0F, 0x10, 0x59};
    if (!code || size < 36 || code[0] != 0x8B || code[1] != 0x0D || std::memcmp(code + 6, kMid, sizeof(kMid)) ||
        std::memcmp(code + 23, kTail, sizeof(kTail)))
        return false;
    out.tlsIndexRva = static_cast<uint32_t>(static_cast<int64_t>(rva) + 6 + ReadRel32(code + 2));
    out.gameTimeSlot = ReadU32(code + 19);
    out.fractionOffset = code[35];   // the disp8 after F3 0F 10 59
    return out.gameTimeSlot && out.gameTimeSlot < 0x1000 && out.fractionOffset;
}

// The factor writer: its call must land on `fractionLeafRva`.
inline bool DecodeFactorWriter(const uint8_t* code, size_t size, uint32_t rva, uint32_t fractionLeafRva,
                               FrameClockLayout& out)
{
    static constexpr uint8_t kStore[]{0xF3, 0x0F, 0x11, 0x40};
    if (!code || size < 19 || code[0] != 0xE8 || code[5] != 0xB8 || code[10] != 0x4A || code[11] != 0x8B ||
        code[12] != 0x04 || std::memcmp(code + 14, kStore, sizeof(kStore)))
        return false;
    if (static_cast<uint32_t>(static_cast<int64_t>(rva) + 5 + ReadRel32(code + 1)) != fractionLeafRva)
        return false;
    out.interpSlot = ReadU32(code + 6);
    out.factorOffset = code[18];
    return out.interpSlot && out.interpSlot < 0x1000;
}

// The tick increment (either title's form): its slot must be the fraction leaf's game_time slot.
inline bool DecodeTickIncrement(const uint8_t* code, size_t size, FrameClockLayout& out)
{
    static constexpr uint8_t kReach[]{0x48, 0x8B, 0x04, 0x18, 0xFF, 0x40};
    static constexpr uint8_t kHalo4[]{0x48, 0x8B, 0x0C, 0x38, 0x44, 0x01, 0x79};
    if (!code || size < 12 || code[0] != 0xB8 || ReadU32(code + 1) != out.gameTimeSlot || !out.gameTimeSlot)
        return false;
    if (!std::memcmp(code + 5, kReach, sizeof(kReach)))
        out.tickOffset = code[11];
    else if (size >= 13 && !std::memcmp(code + 5, kHalo4, sizeof(kHalo4)))
        out.tickOffset = code[12];
    else
        return false;
    return out.tickOffset != 0;
}

// The tick loop's left-over time (optional): B8 <imm32 game_time slot> 66 0F 6E ?? 48 8B 04 30 0F 5B C0 F3 0F 59 40
// <disp8 seconds per tick> -- `ticks not run * seconds per tick` (Reach 0x57568, Halo 4 0x9A4DD).
inline bool DecodeSecondsPerTick(const uint8_t* code, size_t size, FrameClockLayout& out)
{
    static constexpr uint8_t kMid[]{0x48, 0x8B, 0x04, 0x30, 0x0F, 0x5B, 0xC0, 0xF3, 0x0F, 0x59, 0x40};
    if (!code || size < 21 || code[0] != 0xB8 || !out.gameTimeSlot || ReadU32(code + 1) != out.gameTimeSlot ||
        code[5] != 0x66 || code[6] != 0x0F || code[7] != 0x6E || std::memcmp(code + 9, kMid, sizeof(kMid)))
        return false;
    out.secondsPerTickOffset = code[20];
    return out.secondsPerTickOffset != 0;
}

// All three, in order; `valid` only when every one decoded and the offsets are distinct.
inline bool DecodeFrameClock(const uint8_t* leaf, size_t leafSize, uint32_t leafRva, const uint8_t* writer,
                             size_t writerSize, uint32_t writerRva, const uint8_t* increment, size_t incrementSize,
                             FrameClockLayout& out)
{
    out = FrameClockLayout{};
    out.valid = DecodeFractionLeaf(leaf, leafSize, leafRva, out) &&
                DecodeFactorWriter(writer, writerSize, writerRva, leafRva, out) &&
                DecodeTickIncrement(increment, incrementSize, out) && out.tickOffset != out.fractionOffset &&
                out.interpSlot != out.gameTimeSlot;
    return out.valid;
}

// The interpolator's prediction for a shot that leaves from the eye AFTER its tick's move: the render showed
// lerp(previous tick, newest tick, alpha) -- (1 - alpha) of a tick behind the newest tick's eye -- and the firing tick
// is the next one, so E_fire - E_P = (2 - alpha) ticks of travel. A fire from the eye BEFORE its tick's move would be
// (1 - alpha). NaN for a non-finite or out-of-range alpha.
inline float PredictedFireLeadTicks(float alpha)
{
    return std::isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f ? 2.0f - alpha : NAN;
}

// ---- 9. the fire eye measured both ways (dev; Reach and Halo 4) ----------------------------------------------------
// Per fire: the evaluator's answer (the eye the round leaves from) and the carried eye (E_P one tick on) against E_P,
// along the eye's travel, in mm and as time at that speed, beside the interpolator's (2 - alpha) prediction. Turns "is
// a one-tick carry short?" into a number. Plain sums, owned by the host's lock.
struct FireEyeBothWays
{
    uint32_t fires = 0, moving = 0;
    uint32_t measuredN = 0, carriedN = 0, predictedN = 0, alphaN = 0, tickN = 0;
    double measuredMm = 0, measuredMs = 0, carriedMm = 0, carriedMs = 0, predictedMs = 0, alphaSum = 0, tickMsSum = 0;

    // `eyeP` E_P, `vel` its velocity (wu/s), `measured` / `carried` (null: none), `alpha` the factor E_P was drawn at
    // (NaN unknown), `tickMs` the game's tick (NaN unknown). Moving = over 1 m/s.
    void Add(const float eyeP[3], const float* vel, const float* measured, const float* carried, float alpha,
             float tickMs)
    {
        ++fires;
        if (std::isfinite(alpha))
        {
            ++alphaN;
            alphaSum += alpha;
        }
        if (std::isfinite(tickMs) && tickMs > 0.0f)
        {
            ++tickN;
            tickMsSum += tickMs;
        }
        if (!eyeP || !Finite3(eyeP) || !vel || !Finite3(vel))
            return;
        const float speed = Length(vel);
        if (!(speed * kMetresPerWu >= 1.0f))
            return;
        ++moving;
        const float u[3]{vel[0] / speed, vel[1] / speed, vel[2] / speed};
        const auto along = [&](const float* e, uint32_t& n, double& mm, double& ms) {
            if (!e || !Finite3(e))
                return;
            const float d[3]{e[0] - eyeP[0], e[1] - eyeP[1], e[2] - eyeP[2]};
            const float wu = Dot(d, u);
            ++n;
            mm += wu * kMmPerWu;
            ms += wu / speed * 1000.0f;
        };
        along(measured, measuredN, measuredMm, measuredMs);
        along(carried, carriedN, carriedMm, carriedMs);
        const float lead = PredictedFireLeadTicks(alpha);
        if (std::isfinite(lead) && std::isfinite(tickMs) && tickMs > 0.0f)
        {
            ++predictedN;
            predictedMs += lead * tickMs;
        }
    }

    // One clause: "FIRES n (m moving over 1 m/s): along the eye's travel from E_P the evaluator answered ... ".
    bool Format(char* out, size_t size) const
    {
        if (!out || !size)
            return false;
        out[0] = '\0';
        if (!fires)
            return false;
        const auto mean = [](double sum, uint32_t n) { return n ? sum / static_cast<double>(n) : 0.0; };
        std::snprintf(out, size,
                      "FIRES %u (%u moving over 1 m/s): along the eye's travel from E_P the evaluator answered %+.0f mm "
                      "(%+.1f ms, %u), the one-tick carry %+.0f mm (%+.1f ms, %u); the interpolator predicts (2 - "
                      "factor) ticks = %.1f ms (%u; factor avg %.2f over %u, game tick %.2f ms over %u) -- (1 - factor) "
                      "would mean the fire leaves BEFORE its tick's move",
                      fires, moving, mean(measuredMm, measuredN), mean(measuredMs, measuredN), measuredN,
                      mean(carriedMm, carriedN), mean(carriedMs, carriedN), carriedN, mean(predictedMs, predictedN),
                      predictedN, mean(alphaSum, alphaN), alphaN, mean(tickMsSum, tickN), tickN);
        return true;
    }
};

// ---- 10. the VR witness core (dev; Reach and Halo 4) --------------------------------------------------------------
// The VR AS SEEN witness without its engine: rounds (the local player's projectiles, each with the aim line it was
// fired through), their impacts (matched to a round by QueryOnRound), the prepared frames a stereo core noted (its
// centre camera, the head orientation its views were submitted with, the engine eye and stock aim, the tick and the
// interpolation factor of the world it shows) and the reticle the compositor drew for each frame. An impact is judged
// against the reticle of the FIRST frame whose world is past the hit. The host owns the lock and the clock; everything
// here is plain data, unit-tested (tests/as_seen_tests.cpp). Halo 2 and Halo 3 keep their own (older) copies.
struct WitnessAim
{
    bool valid = false;
    float head[3]{}, dir[3]{}, eye[3]{};   // the aim line at the fire (H_P along u) and its engine eye E_P
    uint64_t camUs = 0;
    int64_t camTick = -1;
    float camFactor = NAN;
    bool velValid = false;
    float vel[3]{};
};
struct WitnessRound
{
    bool used = false;
    uint64_t fireUs = 0;
    int64_t fireTick = -1;
    float spawn[3]{}, fwd[3]{};
    uint32_t queries = 0;
    WitnessAim aim{};
};
struct WitnessFrame
{
    bool valid = false;
    uint64_t serial = 0, stampUs = 0, cameraUs = 0;
    int64_t tick = -1;    // the tick counter of the world this frame shows (it steps at the END of a tick)
    float factor = NAN;   // the interpolation factor its camera was built at
    float head[3]{}, fwd[3]{}, up[3]{}, quat[4]{};   // the centre camera (world) and the head orientation (tracking)
    bool eyeValid = false;
    float eye[3]{}, aimFwd[3]{};   // the engine eye and the stock aim forward of this frame
};
struct WitnessPending
{
    bool used = false, framed = false;
    float impact[3]{};
    uint64_t impactUs = 0;
    int64_t impactTick = -1;
    WitnessRound round{};
    WitnessFrame frame{};
};
struct WitnessReticle
{
    bool valid = false;
    uint64_t serial = 0, stampUs = 0;
    float dir[3]{};   // the reticle's tracking-space direction as the compositor drew it
};

// The reticle's tracking direction as the compositor shows it over `frame`: into the head's frame (OpenXR: +X right,
// +Y up, -Z forward) with the head orientation the views were submitted with, then out through the frame's centre
// camera (forward, up, left = up x forward). As Halo 2's and Halo 3's witnesses do.
inline bool WitnessReticleWorld(const WitnessFrame& frame, const float trackingDir[3], float out[3])
{
    const float ql = std::sqrt(frame.quat[0] * frame.quat[0] + frame.quat[1] * frame.quat[1] +
                               frame.quat[2] * frame.quat[2] + frame.quat[3] * frame.quat[3]);
    if (!std::isfinite(ql) || ql < 0.5f || !trackingDir || !Finite3(trackingDir) || !Finite3(frame.fwd) ||
        !Finite3(frame.up))
        return false;
    const float qv[3]{-frame.quat[0] / ql, -frame.quat[1] / ql, -frame.quat[2] / ql};
    const float w = frame.quat[3] / ql;
    const float* v = trackingDir;
    const float t[3]{2.0f * (qv[1] * v[2] - qv[2] * v[1]), 2.0f * (qv[2] * v[0] - qv[0] * v[2]),
                     2.0f * (qv[0] * v[1] - qv[1] * v[0])};
    const float local[3]{v[0] + w * t[0] + (qv[1] * t[2] - qv[2] * t[1]),
                         v[1] + w * t[1] + (qv[2] * t[0] - qv[0] * t[2]),
                         v[2] + w * t[2] + (qv[0] * t[1] - qv[1] * t[0])};
    const float* f = frame.fwd;
    const float* u = frame.up;
    const float left[3]{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = -local[2] * f[axis] + local[1] * u[axis] - local[0] * left[axis];
    const float l = Length(out);
    if (!std::isfinite(l) || l < 1.0e-6f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] /= l;
    return true;
}

struct Witness
{
    static constexpr int kRounds = 16, kPending = 16, kFrames = 32, kReticles = 16;
    WitnessRound rounds[kRounds]{};
    WitnessPending pending[kPending]{};
    WitnessFrame frames[kFrames]{};
    uint32_t frameNext = 0;
    WitnessReticle reticles[kReticles]{};
    uint32_t reticleNext = 0;
    Window window{};
    CameraCensus census{};
    float radiusDeg = 0.0f;   // the reticle's half-angle as drawn (radii column; <= 0 none)
    uint32_t live = 0;        // rounds + pendings in use (the host may mirror it to skip its lock)
    // Window counters (ResetCounts).
    uint32_t spawns = 0, ours = 0, notOurs = 0, noAim = 0, evicted = 0, lost = 0, matched = 0, ambiguous = 0;
    uint32_t impacts = 0, framesNoted = 0, framesNoEye = 0, reticlesNoted = 0, noReticle = 0, speedN = 0;
    double speedSum = 0.0;

    // A round the fire created. `ours`: the host's identity proof for the shooter; `aim` the line it was fired through.
    void Spawn(bool isOurs, const WitnessAim& aim, const float spawn[3], const float fwd[3], const float* velocity,
               uint64_t nowUs, int64_t tick)
    {
        ++spawns;
        if (!isOurs)
        {
            ++notOurs;
            return;
        }
        if (!aim.valid || !spawn || !fwd || !Finite3(spawn) || !Finite3(fwd))
        {
            ++noAim;
            return;
        }
        ++ours;
        if (velocity && Finite3(velocity))
        {
            ++speedN;
            speedSum += double(Length(velocity)) * kMetresPerWu;
        }
        WitnessRound* slot = nullptr;
        for (auto& r : rounds)
            if (!r.used)
            {
                slot = &r;
                break;
            }
        if (!slot)
        {
            slot = &rounds[0];
            for (auto& r : rounds)
                if (r.fireUs < slot->fireUs)
                    slot = &r;
            ++evicted;
        }
        else
            ++live;
        *slot = WitnessRound{};
        slot->used = true;
        slot->fireUs = nowUs;
        slot->fireTick = tick;
        std::memcpy(slot->spawn, spawn, sizeof(slot->spawn));
        std::memcpy(slot->fwd, fwd, sizeof(slot->fwd));
        slot->aim = aim;
    }

    // A collision query (start, vector); `landed` with its hit point. True when it was a tracked round's.
    bool Query(const float start[3], const float vector[3], bool landed, const float point[3], uint64_t nowUs,
               int64_t tick)
    {
        if (!live || !start || !vector || !Finite3(start) || !Finite3(vector))
            return false;
        WitnessRound* best = nullptr;
        float bestMiss = 0.0f;
        bool tie = false;
        for (auto& r : rounds)
        {
            if (!r.used)
                continue;
            if (nowUs > r.fireUs + 3000000)
            {
                ++lost;
                r = WitnessRound{};
                --live;
                continue;
            }
            float miss = 0.0f;
            if (!QueryOnRound(r.spawn, r.fwd, start, vector, &miss))
                continue;
            if (best && std::fabs(miss - bestMiss) < 0.01f)
            {
                tie = true;
                if (r.fireUs < best->fireUs)
                {
                    best = &r;
                    bestMiss = miss;
                }
            }
            else if (!best || miss < bestMiss)
            {
                best = &r;
                bestMiss = miss;
            }
        }
        if (!best)
            return false;
        ++matched;
        if (tie)
            ++ambiguous;
        ++best->queries;
        if (landed && point && Finite3(point))
        {
            WitnessPending* slot = nullptr;
            for (auto& p : pending)
                if (!p.used)
                {
                    slot = &p;
                    break;
                }
            if (!slot)
            {
                slot = &pending[0];
                for (auto& p : pending)
                    if (p.impactUs < slot->impactUs)
                        slot = &p;
                ++window.unframed;
                --live;
            }
            *slot = WitnessPending{};
            slot->used = true;
            std::memcpy(slot->impact, point, sizeof(slot->impact));
            slot->impactUs = nowUs;
            slot->impactTick = tick;
            slot->round = *best;
            *best = WitnessRound{};   // its count moves to the pending: `live` unchanged
        }
        return true;
    }

    // Does `frame` show an impact? Its world is past the hit's tick when both ticks are known (the counter steps at the
    // end of each tick, so the hit's tick reads one less than any later frame's), else by the camera's time.
    static bool Shows(const WitnessFrame& frame, const WitnessPending& p)
    {
        if (frame.tick >= 0 && p.impactTick >= 0)
            return frame.tick > p.impactTick;
        return p.impactUs <= frame.cameraUs;
    }

    // The engine eye's velocity at `frame` from the ring (50-200 ms back), else `fallback`.
    bool EyeVelocity(const WitnessFrame& frame, const float* fallback, float out[3]) const
    {
        const WitnessFrame* older = nullptr;
        for (const auto& f : frames)
        {
            if (!f.valid || !f.eyeValid || f.cameraUs >= frame.cameraUs)
                continue;
            const uint64_t dt = frame.cameraUs - f.cameraUs;
            if (dt < 50000 || dt > 200000)
                continue;
            if (!older || f.cameraUs > older->cameraUs)
                older = &f;
        }
        if (older && frame.eyeValid)
        {
            const float dt = float(frame.cameraUs - older->cameraUs) * 1.0e-6f;
            for (int axis = 0; axis < 3; ++axis)
                out[axis] = (frame.eye[axis] - older->eye[axis]) / dt;
            if (Finite3(out))
                return true;
        }
        if (fallback && Finite3(fallback))
        {
            std::memcpy(out, fallback, sizeof(float) * 3);
            return true;
        }
        return false;
    }

    void Release(WitnessPending& p)
    {
        if (p.used && live)
            --live;
        p = WitnessPending{};
    }

    // A framed impact against the reticle drawn for its frame: one sample.
    void Evaluate(const WitnessPending& p, const WitnessReticle& r)
    {
        const WitnessFrame& frame = p.frame;
        float drawn[3]{};
        if (!WitnessReticleWorld(frame, r.dir, drawn))
        {
            ++window.noLine;
            return;
        }
        float vel[3]{};
        const bool velKnown = EyeVelocity(frame, p.round.aim.velValid ? p.round.aim.vel : nullptr, vel);
        const float* v = velKnown ? vel : nullptr;
        Sample sample{};
        sample.seen = Measure(p.impact, frame.head, drawn, v);
        sample.stockValid = frame.eyeValid && Finite3(frame.aimFwd);
        if (sample.stockValid)
            sample.stock = Measure(p.impact, frame.eye, frame.aimFwd, v);
        sample.atFireValid = p.round.aim.valid;
        if (sample.atFireValid)
            sample.atFire = Measure(p.impact, p.round.aim.head, p.round.aim.dir, v);
        sample.aimCamUs = p.round.aim.camUs;
        sample.fireUs = p.round.fireUs;
        sample.impactUs = p.impactUs;
        sample.frameCamUs = frame.cameraUs;
        sample.presentUs = r.stampUs;   // VR: the compositor building this serial's layers, not a photon
        sample.ticksFireToImpact = p.round.queries ? static_cast<int32_t>(p.round.queries - 1) : -1;
        if (frame.tick >= 0 && p.round.aim.camTick >= 0 && frame.tick >= p.round.aim.camTick)
            sample.ticksAimToFrame = static_cast<int32_t>(frame.tick - p.round.aim.camTick);
        if (frame.tick >= 0 && p.impactTick >= 0 && frame.tick >= p.impactTick)
            sample.ticksImpactToFrame = static_cast<int32_t>(frame.tick - p.impactTick);
        sample.frameAlpha = frame.factor;
        if (p.round.aim.valid && frame.eyeValid)
        {
            const float moved[3]{frame.eye[0] - p.round.aim.eye[0], frame.eye[1] - p.round.aim.eye[1],
                                 frame.eye[2] - p.round.aim.eye[2]};
            sample.eyeMovedAlongMm = AlongTravelMm(moved, drawn, v, &sample.eyeMovedKnown);
        }
        ++impacts;
        window.Add(sample, radiusDeg);
    }

    // A prepared frame: every pending impact it is the first to show takes it; its reticle may already be here.
    void Frame(const WitnessFrame& f)
    {
        if (!f.valid || !f.serial)
            return;
        ++framesNoted;
        if (!f.eyeValid)
            ++framesNoEye;
        frames[frameNext++ % kFrames] = f;
        if (f.eyeValid)
            census.Add(f.eye, f.cameraUs, f.tick, f.factor);
        if (!live)
            return;
        for (auto& p : pending)
        {
            if (!p.used || p.framed || !Shows(f, p))
                continue;
            p.framed = true;
            p.frame = f;
            for (const auto& r : reticles)
                if (r.valid && r.serial == f.serial)
                {
                    Evaluate(p, r);
                    Release(p);
                    break;
                }
        }
    }

    // The reticle the compositor drew for `serial`.
    void Reticle(uint64_t serial, uint64_t stampUs, const float dir[3])
    {
        if (!serial || !dir || !Finite3(dir))
            return;
        ++reticlesNoted;
        WitnessReticle& kept = reticles[reticleNext++ % kReticles];
        kept.valid = true;
        kept.serial = serial;
        kept.stampUs = stampUs;
        std::memcpy(kept.dir, dir, sizeof(kept.dir));
        if (!live)
            return;
        for (auto& p : pending)
        {
            if (!p.used || !p.framed)
                continue;
            if (p.frame.serial == serial)
            {
                Evaluate(p, kept);
                Release(p);
            }
            else if (p.frame.serial + 3 < serial)
            {
                ++noReticle;   // its frame drew no traced reticle (off, a seat, a gate)
                ++window.noLine;
                Release(p);
            }
        }
    }

    // An impact no frame showed within half a second is dropped (counted).
    void Expire(uint64_t nowUs)
    {
        for (auto& p : pending)
            if (p.used && nowUs > p.impactUs + 500000)
            {
                ++window.unframed;
                Release(p);
            }
    }

    // The window's counters and census back to zero (the rings and in-flight rounds stay).
    void ResetCounts()
    {
        window = Window{};
        census.ResetCounts();
        spawns = ours = notOurs = noAim = evicted = lost = matched = ambiguous = 0;
        impacts = framesNoted = framesNoEye = reticlesNoted = noReticle = speedN = 0;
        speedSum = 0.0;
    }

    // Forget everything (a new title generation).
    void Clear()
    {
        const float radius = radiusDeg;
        *this = Witness{};
        radiusDeg = radius;
    }

    // The counters clause: "rounds from the fire routine ...".
    bool FormatCounts(char* out, size_t size) const
    {
        if (!out || !size)
            return false;
        out[0] = '\0';
        if (!spawns && !framesNoted)
            return false;
        std::snprintf(out, size,
                      "rounds from the fire routine %u: ours %u (mean speed %.0f m/s), not ours %u, ours but no aim line "
                      "%u, evicted %u, lost %u; their queries matched %u (%u ambiguous); impacts judged %u; frames noted "
                      "%u (without an engine eye %u); reticles noted %u, frames whose reticle never came %u",
                      spawns, ours, speedN ? speedSum / speedN : 0.0, notOurs, noAim, evicted, lost, matched, ambiguous,
                      impacts, framesNoted, framesNoEye, reticlesNoted, noReticle);
        return true;
    }
};
} // namespace as_seen
