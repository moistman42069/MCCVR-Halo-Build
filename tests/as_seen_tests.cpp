// As seen (src/common/as_seen_logic.h): an impact against the crosshair drawn in the first frame that shows it --
// the axes across a line, the offset at the impact's depth, the bins, the window's line, the camera census and the
// flat art size. The same header serves the VR dev witness and the flat probe, so these numbers are both hosts'.
#include "as_seen_logic.h"

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
bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
constexpr float kWuPerM = shot_lag::kWuPerMetre;
} // namespace

int main()
{
    using namespace as_seen;

    // ---- bins -----------------------------------------------------------------------------------------------------
    Check(RangeBin(0.0f) == 0 && RangeBin(4.99f) == 0 && RangeBin(5.0f) == 1 && RangeBin(12.0f) == 2 &&
              RangeBin(25.0f) == 3 && RangeBin(40.0f) == 4 && RangeBin(500.0f) == 4,
          "bins: range edges 5/10/20/40 m");
    Check(RangeBin(NAN) == kRangeBins - 1, "bins: a non-finite range is the far bin");
    Check(SpeedBin(0.0f) == 0 && SpeedBin(0.99f) == 0 && SpeedBin(1.0f) == 1 && SpeedBin(3.9f) == 1 &&
              SpeedBin(4.0f) == 2 && SpeedBin(NAN) == 0,
          "bins: speed edges 1/4 m/s");

    // ---- the axes across a line -----------------------------------------------------------------------------------
    {
        const float dir[3]{2, 0, 0};
        const float vel[3]{0, 5 * kWuPerM, 0};   // strafing +Y at 5 m/s
        const Across a = AxesAcross(dir, vel);
        Check(a.valid && a.travel && Near(a.speedMps, 5.0f, 1e-3f), "axes: travel across the line, its speed");
        Check(Near(a.t[1], 1.0f, 1e-6f) && Near(a.n[2], 1.0f, 1e-6f), "axes: travel +Y, up +Z");
        const float velDown[3]{3 * kWuPerM, 0, 0};   // moving along the line only
        const Across b = AxesAcross(dir, velDown);
        Check(b.valid && !b.travel && Near(b.t[1], -1.0f, 1e-6f) && Near(b.n[2], 1.0f, 1e-6f),
              "axes: no travel -> the horizontal right of the line, up stays up");
        const float zero[3]{0, 0, 0};
        Check(!AxesAcross(zero, vel).valid, "axes: a zero line is refused");
        const float vertical[3]{0, 0, 1};
        Check(AxesAcross(vertical, nullptr).valid, "axes: a vertical line still has axes");
    }

    // ---- the offset at the impact's depth -------------------------------------------------------------------------
    {
        // The eye at the origin looks +X; strafing +Y at 6 m/s; the impact 10 m out, 0.30 m BEHIND (-Y), 0.05 m up.
        const float origin[3]{0, 0, 0};
        const float dir[3]{1, 0, 0};
        const float vel[3]{0, 6 * kWuPerM, 0};
        const float impact[3]{10 * kWuPerM, -0.30f * kWuPerM, 0.05f * kWuPerM};
        const Offset o = Measure(impact, origin, dir, vel);
        Check(o.valid && o.travel, "offset: valid, with travel");
        Check(Near(o.alongMm, -300.0f, 0.5f) && Near(o.upMm, 50.0f, 0.5f), "offset: along (behind) and up, in mm");
        Check(Near(o.depthM, 10.0f, 1e-3f) && Near(o.rangeM, std::sqrt(100.0f + 0.09f + 0.0025f), 1e-3f),
              "offset: depth along the line and range from the eye");
        Check(Near(o.alongDeg, -std::atan2(0.30f, 10.0f) * 57.29578f, 1e-3f) &&
                  Near(o.upDeg, std::atan2(0.05f, 10.0f) * 57.29578f, 1e-3f),
              "offset: the components as angles from the eye");
        Check(Near(o.angleDeg, std::atan2(std::sqrt(0.09f + 0.0025f), 10.0f) * 57.29578f, 1e-3f),
              "offset: the total angle off the line");
        Check(Near(o.speedMps, 6.0f, 1e-3f), "offset: the strafe speed across the line");
        // Strafing the other way flips the sign: the same point is now AHEAD.
        const float back[3]{0, -6 * kWuPerM, 0};
        Check(Near(Measure(impact, origin, dir, back).alongMm, 300.0f, 0.5f), "offset: along is signed by travel");
        // A point behind the eye is refused (a crosshair line never shows it).
        const float behindEye[3]{-1 * kWuPerM, 0, 0};
        Check(!Measure(behindEye, origin, dir, vel).valid, "offset: a point behind the eye is refused");
        // Without a velocity: no travel, along = horizontal right of the line (-Y for +X), still measured.
        const Offset still = Measure(impact, origin, dir, nullptr);
        Check(still.valid && !still.travel && Near(still.alongMm, 300.0f, 0.5f), "offset: no velocity, right axis");
    }

    // ---- the eye's own move along the travel ----------------------------------------------------------------------
    {
        const float dir[3]{1, 0, 0};
        const float vel[3]{0, 6 * kWuPerM, 0};
        const float delta[3]{0.2f * kWuPerM, 0.25f * kWuPerM, 0};
        bool known = false;
        Check(Near(AlongTravelMm(delta, dir, vel, &known), 250.0f, 0.5f) && known, "eye moved: along travel, mm");
        AlongTravelMm(delta, dir, nullptr, &known);
        Check(!known, "eye moved: unknown without a travel axis");
    }

    // ---- the window ---------------------------------------------------------------------------------------------
    {
        Window w{};
        char line[4096]{};
        Check(!w.Format("Halo 3 VR", line, sizeof(line)), "window: empty says nothing");
        // Two impacts at ~10 m while strafing 6 m/s, each 300 mm behind the reticle the frame drew.
        const float origin[3]{0, 0, 0}, dir[3]{1, 0, 0}, vel[3]{0, 6 * kWuPerM, 0};
        const float impact[3]{12 * kWuPerM, -0.30f * kWuPerM, 0};
        Sample s{};
        s.seen = Measure(impact, origin, dir, vel);
        s.stockValid = true;
        s.stock = s.seen;
        s.atFireValid = true;
        s.atFire = Measure(impact, origin, dir, vel);
        s.aimCamUs = 1000000;
        s.fireUs = 1015700;
        s.impactUs = 1050000;
        s.frameCamUs = 1055000;
        s.presentUs = 1060000;
        s.ticksFireToImpact = 2;
        s.ticksAimToFrame = 3;
        s.eyeMovedKnown = true;
        s.eyeMovedAlongMm = 300.0f;
        w.Add(s, 5.05f);
        w.Add(s, 5.05f);
        const Bin& b = w.bin[SpeedBin(6.0f)][RangeBin(12.0f)];
        Check(w.impacts == 2 && b.n == 2, "window: binned by strafe speed and range");
        Check(Near(static_cast<float>(b.impliedMs / b.nImplied), 50.0f, 0.1f),
              "window: 300 mm behind at 6 m/s is 50 ms of strafe");
        Check(Near(static_cast<float>(b.eyeMovedMs / b.nEye), 50.0f, 0.1f), "window: the eye's move as time");
        Check(Near(static_cast<float>(b.aimToFireMs / b.nAimFire), 15.7f, 0.01f) &&
                  Near(static_cast<float>(b.fireToImpactMs / b.nFireImpact), 34.3f, 0.01f) &&
                  Near(static_cast<float>(b.impactToFrameMs / b.nImpactFrame), 5.0f, 0.01f) &&
                  Near(static_cast<float>(b.frameToPresentMs / b.nFramePresent), 5.0f, 0.01f),
              "window: the clock intervals");
        Check(Near(static_cast<float>(b.seenAlongRadii / b.nRadii), s.seen.alongDeg / 5.05f, 1e-4f),
              "window: radii are the along angle over the reticle radius");
        Sample bad{};
        w.Add(bad, 5.05f);
        Check(w.refused == 1 && w.impacts == 2, "window: an invalid sample is refused, not binned");
        w.unframed = 3;
        Check(w.Format("Halo 3 VR", line, sizeof(line)), "window: formats");
        Check(std::strstr(line, "AS SEEN Halo 3 VR (dev): 2 impacts shown") != nullptr &&
                  std::strstr(line, "[4+ m/s, 10-20 m] 2 at 12.0 m") != nullptr &&
                  std::strstr(line, "SEEN along -300 mm") != nullptr &&
                  std::strstr(line, "= 50 ms of strafe") != nullptr && std::strstr(line, "(2.0 ticks)") != nullptr &&
                  std::strstr(line, "3 never reached a frame") != nullptr &&
                  std::strstr(line, "ticks aim cam->frame 3.0") != nullptr,
              "window: the line names the bin, the offset, its time and the clocks");
        // A tiny buffer truncates without overrunning.
        char tiny[32];
        std::memset(tiny, 'x', sizeof(tiny));
        w.Format("Halo 3 VR", tiny, sizeof(tiny));
        Check(std::memchr(tiny, '\0', sizeof(tiny)) != nullptr, "window: truncation keeps a terminator");
    }

    // ---- the camera census --------------------------------------------------------------------------------------
    {
        // Interpolated: moves 6 m/s every 11.1 ms frame.
        CameraCensus smooth{};
        float p[3]{0, 0, 0};
        uint64_t t = 1000000;
        for (int i = 0; i < 90; ++i)
        {
            smooth.Add(p, t);
            p[1] += 6.0f * kWuPerM * 0.0111f;
            t += 11100;
        }
        Check(smooth.moving > 80 && smooth.repeated == 0 && smooth.uneven == 0, "census: interpolated never repeats");
        Check(Near(static_cast<float>(smooth.stepRatioSum / smooth.stepN), 1.0f, 0.02f),
              "census: interpolated steps one frame's worth");
        // Tick-stepped: the eye advances 16.7 ms of travel at each 60 Hz tick, sampled by 90 Hz frames.
        CameraCensus stepped{};
        float q[3]{0, 0, 0};
        for (int i = 0; i < 180; ++i)
        {
            const uint64_t frameUs = 1000000 + static_cast<uint64_t>(i) * 11111;
            const uint64_t ticks = (frameUs - 1000000) / 16667;
            q[1] = static_cast<float>(ticks) * 6.0f * kWuPerM * 0.016667f;
            stepped.Add(q, frameUs);
        }
        const float repeatShare = static_cast<float>(stepped.repeated) / static_cast<float>(stepped.moving);
        Check(stepped.moving > 150 && Near(repeatShare, 1.0f / 3.0f, 0.05f),
              "census: a 60 Hz camera at 90 Hz repeats on a third of the frames");
        Check(stepped.uneven > stepped.moving / 2, "census: and its steps are uneven");
        char clause[512]{};
        Check(stepped.Format(clause, sizeof(clause)) && std::strstr(clause, "REPEATED") != nullptr,
              "census: formats");
        stepped.ResetCounts();
        Check(!stepped.Format(clause, sizeof(clause)), "census: counts reset, history kept");
        // At rest nothing counts as moving.
        CameraCensus rest{};
        const float still[3]{1, 2, 3};
        for (int i = 0; i < 30; ++i)
            rest.Add(still, 1000000 + static_cast<uint64_t>(i) * 11111);
        Check(rest.frames == 30 && rest.moving == 0, "census: at rest nothing moves");
    }

    // ---- the census with the tick counter (Halo 2) ---------------------------------------------------------------
    {
        // A camera drawn at lerp(previous tick, newest tick, alpha) at 90 Hz over a 60 Hz simulation: a third of the
        // frames run no tick, and the eye still moves on every one of them.
        CameraCensus lerp{};
        float p[3]{0, 0, 0};
        for (int i = 0; i < 180; ++i)
        {
            const int64_t tick = (i * 2) / 3;   // 60 Hz ticks under 90 Hz frames
            const float alpha = static_cast<float>((i * 2) % 3) / 3.0f;
            p[1] = (static_cast<float>(tick) - 1.0f + alpha) / 60.0f * 6.0f * kWuPerM;
            lerp.Add(p, 1000000 + static_cast<uint64_t>(i) * 11111, tick, alpha);
        }
        Check(lerp.ticked > 170 && lerp.ticks0 > 50 && lerp.ticks2 == 0,
              "census ticks: at 90 Hz over 60 Hz a third of the frames run no tick");
        Check(lerp.movingNoTick > 40 && lerp.movedNoTick == lerp.movingNoTick && lerp.repeatedWithTick == 0,
              "census ticks: an interpolated eye moves on every tick-less frame");
        Check(lerp.alphaN == 180 && Near(static_cast<float>(lerp.alphaSum / lerp.alphaN), 0.33f, 0.05f),
              "census ticks: the alpha mean");
        char clause[1024]{};
        Check(lerp.Format(clause, sizeof(clause)) && std::strstr(clause, "sim ticks per frame 0/1/2+") != nullptr &&
                  std::strstr(clause, "interpolation alpha mean") != nullptr,
              "census ticks: formats the tick and alpha clauses");
        // A tick-stepped eye repeats on exactly the tick-less frames.
        CameraCensus step{};
        float q[3]{0, 0, 0};
        for (int i = 0; i < 180; ++i)
        {
            const int64_t tick = (i * 2) / 3;
            q[1] = static_cast<float>(tick) / 60.0f * 6.0f * kWuPerM;
            step.Add(q, 1000000 + static_cast<uint64_t>(i) * 11111, tick);
        }
        Check(step.movingNoTick > 40 && step.movedNoTick == 0 && step.alphaN == 0,
              "census ticks: a tick-stepped eye repeats on every tick-less frame");
        // An unknown tick breaks the chain instead of counting a jump.
        CameraCensus gap{};
        const float r[3]{0, 0, 0};
        gap.Add(r, 1000000, 10);
        gap.Add(r, 1011000, -1);
        gap.Add(r, 1022000, 12);
        Check(gap.ticked == 0, "census ticks: an unknown tick between two frames counts nothing");
    }

    // ---- which collision query is a round's -----------------------------------------------------------------------
    {
        const float spawn[3]{10, 20, 1}, fwd[3]{0, 2, 0};
        const float start[3]{10.02f, 25, 1}, along[3]{0, 30, 0.5f};
        float miss = -1.0f;
        Check(QueryOnRound(spawn, fwd, start, along, &miss) && Near(miss, 0.02f * shot_lag::kMetresPerWu, 1e-4f),
              "query: on the spawn line, along the forward");
        const float off[3]{10.1f, 25, 1};   // 0.1 wu = 30 cm off the line
        Check(!QueryOnRound(spawn, fwd, off, along), "query: 30 cm off the line is another object's");
        const float sideways[3]{3, 30, 0};   // ~5.7 deg off the forward
        Check(!QueryOnRound(spawn, fwd, start, sideways), "query: over 5 deg off the forward is not the round's");
        const float behind[3]{10, 19.9f, 1};   // 30 cm behind the spawn
        Check(!QueryOnRound(spawn, fwd, behind, along), "query: starting behind the spawn is not the round's");
        const float zero[3]{0, 0, 0};
        Check(!QueryOnRound(spawn, zero, start, along) && !QueryOnRound(spawn, fwd, start, zero),
              "query: a zero forward or vector is refused");
    }

    // ---- the window's tick and alpha columns ------------------------------------------------------------------------
    {
        Window w{};
        const float origin[3]{0, 0, 0}, dir[3]{1, 0, 0}, vel[3]{0, 6 * kWuPerM, 0};
        const float impact[3]{12 * kWuPerM, 0.05f * kWuPerM, 0};
        Sample s{};
        s.seen = Measure(impact, origin, dir, vel);
        s.ticksImpactToFrame = 1;
        s.frameAlpha = 0.25f;
        w.Add(s, 0.0f);
        s.ticksImpactToFrame = 2;
        s.frameAlpha = 0.75f;
        w.Add(s, 0.0f);
        char line[4096]{};
        Check(w.Format("Halo 2 FLAT", line, sizeof(line)) &&
                  std::strstr(line, "ticks impact->frame 1.5") != nullptr &&
                  std::strstr(line, "frame camera alpha 0.50") != nullptr && std::strstr(line, "radii") == nullptr,
              "window: the impact->frame ticks and the frame camera's alpha; no radii column without a radius");
    }

    // ---- the flat art size ----------------------------------------------------------------------------------------
    {
        // A 5242-wide VR HUD and a flat 78 deg horizontal FOV (tan 39 deg).
        const float half = FlatArtHalfAngleDeg(5242.0f, std::tan(39.0f / 57.29578f));
        Check(Near(half, std::atan(128.0f / 5242.0f * std::tan(39.0f / 57.29578f)) * 57.29578f, 1e-4f) &&
                  half > 1.0f && half < 1.3f,
              "flat art: the captured region is ~1.1 deg of a 78 deg flat view");
        Check(std::isnan(FlatArtHalfAngleDeg(0.0f, 0.8f)) && std::isnan(FlatArtHalfAngleDeg(5242.0f, -1.0f)),
              "flat art: refuses nonsense");
    }

    // ---- 8. MCC's frame clock, decoded from the pinned retail bytes ------------------------------------------------
    {
        // haloreach.dll: the fraction leaf 0x5C430, the factor writer 0x344A5, the tick increment 0x57A5A.
        const uint8_t reachLeaf[]{0x8b, 0x0d, 0xe2, 0xb6, 0xbb, 0x00, 0x0f, 0x57, 0xe4, 0x65, 0x48, 0x8b,
                                  0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xba, 0xa0, 0x00, 0x00, 0x00, 0x48,
                                  0x8b, 0x04, 0xc8, 0x48, 0x8b, 0x0c, 0x10, 0xf3, 0x0f, 0x10, 0x59, 0x14};
        const uint8_t reachWriter[]{0xe8, 0x86, 0x7f, 0x02, 0x00, 0xb8, 0x70, 0x00, 0x00, 0x00,
                                    0x4a, 0x8b, 0x04, 0x20, 0xf3, 0x0f, 0x11, 0x40, 0x04};
        const uint8_t reachInc[]{0xb8, 0xa0, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x04, 0x18, 0xff, 0x40, 0x0c};
        FrameClockLayout reach{};
        Check(DecodeFrameClock(reachLeaf, sizeof(reachLeaf), 0x5C430, reachWriter, sizeof(reachWriter), 0x344A5,
                               reachInc, sizeof(reachInc), reach),
              "clock: Reach decodes");
        Check(reach.tlsIndexRva == 0xC17B18 && reach.gameTimeSlot == 0xA0 && reach.fractionOffset == 0x14 &&
                  reach.tickOffset == 0x0C && reach.interpSlot == 0x70 && reach.factorOffset == 0x04,
              "clock: Reach -- TLS index +0xC17B18 (the one the contact lane reads), game_time TLS+0xA0 (+0x14 "
              "fraction, +0x0C tick), interpolator TLS+0x70 (+4 factor)");
        // halo4.dll: 0xA0458, 0x61172, 0x9ADBF.
        const uint8_t h4Leaf[]{0x8b, 0x0d, 0xba, 0x6d, 0xfb, 0x00, 0x0f, 0x57, 0xe4, 0x65, 0x48, 0x8b,
                               0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xba, 0x90, 0x00, 0x00, 0x00, 0x48,
                               0x8b, 0x04, 0xc8, 0x48, 0x8b, 0x0c, 0x10, 0xf3, 0x0f, 0x10, 0x59, 0x14};
        const uint8_t h4Writer[]{0xe8, 0xe1, 0xf2, 0x03, 0x00, 0xb8, 0x78, 0x00, 0x00, 0x00,
                                 0x4a, 0x8b, 0x04, 0x30, 0xf3, 0x0f, 0x11, 0x40, 0x04};
        const uint8_t h4Inc[]{0xb8, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x0c, 0x38, 0x44, 0x01, 0x79, 0x0c};
        FrameClockLayout h4{};
        Check(DecodeFrameClock(h4Leaf, sizeof(h4Leaf), 0xA0458, h4Writer, sizeof(h4Writer), 0x61172, h4Inc,
                               sizeof(h4Inc), h4),
              "clock: Halo 4 decodes");
        Check(h4.tlsIndexRva == 0x1057218 && h4.gameTimeSlot == 0x90 && h4.fractionOffset == 0x14 &&
                  h4.tickOffset == 0x0C && h4.interpSlot == 0x78 && h4.factorOffset == 0x04,
              "clock: Halo 4 -- game_time TLS+0x90, interpolator TLS+0x78");
        // A writer whose call lands anywhere but the leaf, a changed byte, a tick slot of another object: refused.
        FrameClockLayout bad{};
        Check(!DecodeFrameClock(reachLeaf, sizeof(reachLeaf), 0x5C430, reachWriter, sizeof(reachWriter), 0x344A6,
                                reachInc, sizeof(reachInc), bad) &&
                  !bad.valid,
              "clock: the factor writer must call the fraction leaf");
        uint8_t changed[sizeof(reachLeaf)];
        std::memcpy(changed, reachLeaf, sizeof(changed));
        changed[31] = 0xF2;
        Check(!DecodeFrameClock(changed, sizeof(changed), 0x5C430, reachWriter, sizeof(reachWriter), 0x344A5,
                                reachInc, sizeof(reachInc), bad),
              "clock: a changed fixed byte refuses");
        Check(!DecodeFrameClock(reachLeaf, sizeof(reachLeaf), 0x5C430, reachWriter, sizeof(reachWriter), 0x344A5,
                                h4Inc, sizeof(h4Inc), bad),
              "clock: a tick increment on another slot refuses");
        Check(!DecodeFrameClock(reachLeaf, 20, 0x5C430, reachWriter, sizeof(reachWriter), 0x344A5, reachInc,
                                sizeof(reachInc), bad),
              "clock: short code refuses");
        // The tick loop's left-over time: seconds per tick at game_time +8 (Reach 0x57568, Halo 4 0x9A4DD).
        const uint8_t reachLeft[]{0xb8, 0xa0, 0x00, 0x00, 0x00, 0x66, 0x0f, 0x6e, 0xc7, 0x48, 0x8b,
                                  0x04, 0x30, 0x0f, 0x5b, 0xc0, 0xf3, 0x0f, 0x59, 0x40, 0x08};
        const uint8_t h4Left[]{0xb8, 0x90, 0x00, 0x00, 0x00, 0x66, 0x0f, 0x6e, 0xc3, 0x48, 0x8b,
                               0x04, 0x30, 0x0f, 0x5b, 0xc0, 0xf3, 0x0f, 0x59, 0x40, 0x08};
        Check(DecodeSecondsPerTick(reachLeft, sizeof(reachLeft), reach) && reach.secondsPerTickOffset == 8 &&
                  DecodeSecondsPerTick(h4Left, sizeof(h4Left), h4) && h4.secondsPerTickOffset == 8,
              "clock: seconds per tick at game_time +8 in both");
        FrameClockLayout other = reach;
        other.secondsPerTickOffset = 0;
        Check(!DecodeSecondsPerTick(h4Left, sizeof(h4Left), other) && other.secondsPerTickOffset == 0,
              "clock: seconds per tick on another slot refuses");
        Check(Near(PredictedFireLeadTicks(0.0f), 2.0f, 1e-6f) && Near(PredictedFireLeadTicks(0.75f), 1.25f, 1e-6f) &&
                  std::isnan(PredictedFireLeadTicks(1.5f)) && std::isnan(PredictedFireLeadTicks(NAN)),
              "clock: the (2 - factor) prediction");
    }

    // ---- 9. the fire eye both ways ------------------------------------------------------------------------------
    {
        FireEyeBothWays b{};
        const float eye[3]{0, 0, 0}, vel[3]{0, 6 * kWuPerM, 0};   // 6 m/s along +Y
        // The fire leaves from (2 - 0.5) ticks of 16.67 ms ahead (150 mm); the carry moves one tick (100 mm).
        const float measured[3]{0, 0.150f * kWuPerM, 0}, carried[3]{0, 0.100f * kWuPerM, 0};
        b.Add(eye, vel, measured, carried, 0.5f, 16.667f);
        const float still[3]{0, 0, 0};
        b.Add(eye, still, measured, carried, 0.5f, 16.667f);   // at rest: counted, not measured along travel
        Check(b.fires == 2 && b.moving == 1 && b.measuredN == 1 && b.carriedN == 1 && b.predictedN == 1,
              "both ways: a still fire counts but measures nothing along travel");
        Check(Near(static_cast<float>(b.measuredMm), 150.0f, 0.5f) && Near(static_cast<float>(b.measuredMs), 25.0f, 0.1f) &&
                  Near(static_cast<float>(b.carriedMs), 16.667f, 0.1f) &&
                  Near(static_cast<float>(b.predictedMs), 25.0f, 0.01f),
              "both ways: mm and ms along travel, the (2 - factor) prediction");
        char clause[512]{};
        Check(b.Format(clause, sizeof(clause)) && std::strstr(clause, "FIRES 2 (1 moving") != nullptr &&
                  std::strstr(clause, "+150 mm") != nullptr,
              "both ways: formats");
    }

    // ---- 10. the VR witness core ---------------------------------------------------------------------------------
    {
        // The player strafes +Y at 6 m/s looking +X; the world is Blam's (Z up). The headset looks straight ahead in
        // tracking space (identity head orientation), so the reticle drawn straight ahead (-Z) maps onto the centre
        // camera's forward.
        Witness w{};
        w.radiusDeg = 1.0f;
        const uint64_t t0 = 1000000;
        WitnessAim aim{};
        aim.valid = true;
        const float head[3]{0, 0, 0}, dirX[3]{1, 0, 0};
        std::memcpy(aim.head, head, sizeof(aim.head));
        std::memcpy(aim.dir, dirX, sizeof(aim.dir));
        std::memcpy(aim.eye, head, sizeof(aim.eye));
        aim.camUs = t0;
        aim.camTick = 100;
        aim.velValid = true;
        aim.vel[1] = 6 * kWuPerM;
        const float spawn[3]{0.1f, 0.03f * kWuPerM, -0.05f}, fwd[3]{1, 0, 0}, speed[3]{300, 0, 0};
        w.Spawn(true, aim, spawn, fwd, speed, t0 + 20000, 101);
        w.Spawn(false, aim, spawn, fwd, speed, t0 + 20000, 101);
        WitnessAim none{};
        w.Spawn(true, none, spawn, fwd, speed, t0 + 20000, 101);
        Check(w.spawns == 3 && w.ours == 1 && w.notOurs == 1 && w.noAim == 1 && w.live == 1,
              "witness: ours, another's and one without an aim line");
        // Its query: from the spawn along the forward, hitting a wall 10 m out.
        const float qStart[3]{0.1f, 0.03f * kWuPerM, -0.05f}, qVec[3]{20 * kWuPerM, 0, 0};
        const float hit[3]{10 * kWuPerM, 0.03f * kWuPerM, -0.05f};
        const float other[3]{0.1f, 5, 0};
        Check(!w.Query(other, qVec, true, hit, t0 + 21000, 101), "witness: a query off every round is not ours");
        Check(w.Query(qStart, qVec, true, hit, t0 + 21000, 101) && w.matched == 1 && w.live == 1,
              "witness: the round's query lands -> pending");
        // A frame of the world BEFORE the hit (tick 101) does not show it; the next (tick 102) does.
        WitnessFrame f{};
        f.valid = true;
        f.serial = 7;
        f.tick = 101;
        f.factor = 0.5f;
        f.cameraUs = t0 + 22000;
        f.eyeValid = true;
        f.eye[1] = 0.12f * kWuPerM;
        std::memcpy(f.head, f.eye, sizeof(f.head));
        std::memcpy(f.fwd, dirX, sizeof(f.fwd));
        f.up[2] = 1;
        f.quat[3] = 1;
        std::memcpy(f.aimFwd, dirX, sizeof(f.aimFwd));
        w.Frame(f);
        Check(!w.pending[0].framed && !w.pending[1].framed && w.impacts == 0, "witness: a frame of the hit's tick does not show it");
        WitnessFrame g = f;
        g.serial = 8;
        g.tick = 102;
        g.cameraUs = t0 + 33000;
        g.eye[1] = 0.20f * kWuPerM;
        std::memcpy(g.head, g.eye, sizeof(g.head));
        w.Frame(g);
        // The reticle of serial 8 straight ahead.
        const float ahead[3]{0, 0, -1};
        w.Reticle(8, t0 + 34000, ahead);
        Check(w.impacts == 1 && w.live == 0 && w.window.impacts == 1, "witness: framed + its reticle -> one sample");
        // The impact sits 3 cm toward +Y; the frame's eye 20 cm +Y: SEEN along travel = 3 - 20 = -170 mm.
        const Bin& b = w.window.bin[SpeedBin(6.0f)][RangeBin(10.0f)];
        Check(b.n == 1 && Near(static_cast<float>(b.seenAlongMm), -170.0f, 2.0f) &&
                  Near(static_cast<float>(b.atFireAlongMm), 30.0f, 2.0f) && b.nTicksImpact == 1 &&
                  Near(static_cast<float>(b.ticksImpactToFrame), 1.0f, 1e-6f),
              "witness: SEEN against the frame's reticle, AT FIRE against the aim line, ticks impact->frame");
        // A reticle that never comes for a framed impact: dropped as no line after three serials.
        w.Spawn(true, aim, spawn, fwd, speed, t0 + 40000, 103);
        (void)w.Query(qStart, qVec, true, hit, t0 + 41000, 103);
        WitnessFrame h = g;
        h.serial = 20;
        h.tick = 104;
        h.cameraUs = t0 + 50000;
        w.Frame(h);
        w.Reticle(24, t0 + 60000, ahead);
        Check(w.noReticle == 1 && w.live == 0, "witness: a frame that drew no reticle drops its impact, counted");
        // An impact no frame shows within half a second expires.
        w.Spawn(true, aim, spawn, fwd, speed, t0 + 70000, 200);
        (void)w.Query(qStart, qVec, true, hit, t0 + 71000, 200);
        w.Expire(t0 + 700000);
        Check(w.live == 0 && w.window.unframed >= 1, "witness: an unframed impact expires, counted");
        char counts[512]{};
        Check(w.FormatCounts(counts, sizeof(counts)) && std::strstr(counts, "impacts judged 1") != nullptr,
              "witness: the counters clause");
        w.ResetCounts();
        Check(w.spawns == 0 && !w.window.Any(), "witness: counters reset");
        // A turned head: the reticle straight ahead in tracking space with the head yawed 90 deg left maps onto the
        // centre camera's forward all the same (the camera carries the head).
        WitnessFrame turned = f;
        const float s = std::sqrt(0.5f);
        turned.quat[0] = 0;
        turned.quat[1] = s;   // +90 deg about +Y (OpenXR up): forward -Z turns to -X
        turned.quat[2] = 0;
        turned.quat[3] = s;
        const float trackingAhead[3]{-1, 0, 0};
        float world[3]{};
        Check(WitnessReticleWorld(turned, trackingAhead, world) && Near(world[0], 1.0f, 1e-5f) &&
                  Near(world[1], 0.0f, 1e-5f),
              "witness: the reticle maps through the head orientation onto the centre camera");
    }

    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
