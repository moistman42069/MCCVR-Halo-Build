// The hook cost measurement: its arithmetic (src/common/hook_cost_logic.h: which frames are timed, how the
// timer's own cost comes off, how the timed frames become a frame's figures, the log line) and its timers
// (src/common/hook_cost_scope.h: what a hook's scope counts, and what it leaves alone).
#include "../src/common/hook_cost_scope.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;
void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }
} // namespace

int main()
{
    using namespace hook_cost;

    // Which frames are timed.
    {
        Check(!TimeThisFrame(250, 0) && !TimeThisFrame(0, 0), "off: no frame is timed");
        Check(TimeThisFrame(251, 251) && TimeThisFrame(502, 251), "every 251st frame");
        Check(!TimeThisFrame(250, 251) && !TimeThisFrame(252, 251), "and no other");
        int timed = 0;
        for (uint64_t frame = 1; frame <= 80; ++frame)
            timed += TimeThisFrame(frame, 1) ? 1 : 0;
        Check(timed == 80 / static_cast<int>(kMinEveryFrames), "never more often than the least spacing");
    }

    // Every hook has a name; the calibration hook has one too and is never reported.
    for (size_t index = 0; index < kSites; ++index)
        Check(SiteName(static_cast<Site>(index))[0] != '?', "every hook has a name");

    // A counter at 4 ticks a nanosecond (4 GHz). Two timed frames of 20 ms each.
    const double ticksPerNs = 4.0;
    const uint64_t frames = 2;
    const uint64_t frameTicks = static_cast<uint64_t>(2 * 20.0e6 * ticksPerNs);
    const uint64_t timerTicks = 60;   // an empty hook reads 15 ns
    {
        Totals totals[kSites]{};
        // 15,000 indexed draws a frame, 55 ns measured each (40 ns the mod's, 15 the timer's), 500 ns native.
        totals[static_cast<size_t>(Site::DrawIndexed)] = {30000, 30000ull * 220, 30000ull * 2000};
        // 20,000 shader binds a frame, 25 ns measured (10 the mod's), 100 ns native.
        totals[static_cast<size_t>(Site::PixelShader)] = {40000, 40000ull * 100, 40000ull * 400};
        // 1,000 render-target binds a frame that read LESS than the timer's own cost: nothing, never less.
        totals[static_cast<size_t>(Site::RenderTargets)] = {2000, 2000ull * 40, 2000ull * 800};
        // The calibration hook's own totals are never part of the figures.
        totals[static_cast<size_t>(Site::Calibration)] = {8192, 8192ull * 60, 0};

        const Summary s = Summarise(totals, frames, frameTicks, ticksPerNs, timerTicks);
        Check(s.valid && s.frames == 2 && s.rowCount == 3, "three hooks were called");
        Check(Near(s.frameMs, 20.0, 1.0e-9), "a timed frame's length");
        Check(Near(s.timerNsACall, 15.0, 1.0e-9), "the timer's own cost in nanoseconds");
        Check(s.rows[0].site == Site::DrawIndexed && s.rows[1].site == Site::PixelShader &&
                  s.rows[2].site == Site::RenderTargets,
              "most costly first");
        Check(Near(s.rows[0].callsAFrame, 15000.0, 1.0e-9) && Near(s.rows[0].ownNsACall, 40.0, 1.0e-9) &&
                  Near(s.rows[0].ownMsAFrame, 0.6, 1.0e-9),
              "15,000 draws at 40 ns are 0.6 ms a frame");
        Check(Near(s.rows[1].ownNsACall, 10.0, 1.0e-9) && Near(s.rows[1].ownMsAFrame, 0.2, 1.0e-9),
              "20,000 binds at 10 ns are 0.2 ms a frame");
        Check(s.rows[2].ownNsACall == 0.0 && s.rows[2].ownMsAFrame == 0.0,
              "a hook that reads less than the timer costs nothing, not less than nothing");
        Check(Near(s.ownMsAFrame, 0.8, 1.0e-9) && Near(s.callsAFrame, 36000.0, 1.0e-9), "the frame's totals");
        Check(Near(s.nativeMsAFrame, 7.5 + 2.0 + 0.2, 1.0e-9), "the calls themselves");
        Check(Near(s.ownShare, 0.04, 1.0e-9), "0.8 ms of a 20 ms frame is 4%");

        char line[1024];
        const int length = FormatSummary(line, sizeof(line), s, 251);
        Check(length > 0 && static_cast<size_t>(length) == std::strlen(line), "the line's length");
        Check(std::strstr(line, "from 2 timed frames (one in 251)") != nullptr, "the line says what it is from");
        Check(std::strstr(line, "0.80 ms of a 20.00 ms frame (4.0%)") != nullptr, "the line's headline figure");
        Check(std::strstr(line, "DrawIndexed 15000 x 40 ns = 0.600 ms; PSSetShader 20000 x 10 ns = 0.200 ms") !=
                  nullptr,
              "the line's rows, most costly first");
        Check(std::strstr(line, "calibration") == nullptr, "the calibration hook is not in the line");
        Check(std::strstr(line, "The timer's own 15 ns a call is taken off.") != nullptr, "the line says so");

        // A buffer too small for the line is filled and ended, never overrun.
        char small[64];
        std::memset(small, 'x', sizeof(small));
        const int cut = FormatSummary(small, 48, s, 251);
        Check(cut == 47 && small[47] == 0 && small[48] == 'x', "a short buffer is ended inside its size");
    }

    // Nothing timed, or no counter rate: nothing to say.
    {
        Totals totals[kSites]{};
        totals[static_cast<size_t>(Site::Draw)] = {10, 1000, 1000};
        Check(!Summarise(totals, 0, 0, ticksPerNs, timerTicks).valid, "no timed frame");
        Check(!Summarise(totals, 1, 1000, 0.0, timerTicks).valid, "no counter rate");
        Totals none[kSites]{};
        const Summary empty = Summarise(none, 3, 1000, ticksPerNs, timerTicks);
        Check(!empty.valid, "timed frames in which no hook was called");
        char line[64] = "unchanged";
        Check(FormatSummary(line, sizeof(line), empty, 251) == 0 && line[0] == 0, "and no line");
    }

    // The timers. Off: a scope and its mark count nothing.
    {
        Totals totals[kSites]{};
        TakeTotals(totals);
        {
            Scope scope(Site::Draw);
            Native native(scope);
        }
        TakeTotals(totals);
        Check(totals[static_cast<size_t>(Site::Draw)].calls == 0 && g_depth == 0, "off: nothing is counted");

        // On: one call, and the game's part of it is the game's. The "real call" here spins; the hook does not.
        g_timing.store(true);
        volatile uint64_t spin = 0;
        {
            Scope scope(Site::Draw);
            Native native(scope);
            for (int turn = 0; turn < 20000; ++turn)
                spin = spin + static_cast<uint64_t>(turn);
        }
        TakeTotals(totals);
        const Totals draw = totals[static_cast<size_t>(Site::Draw)];
        Check(draw.calls == 1 && draw.nativeTicks > 0, "on: the call is counted, with its native part");
        Check(draw.nativeTicks > draw.ownTicks * 4, "a hook that only forwards costs far less than the call");

        // A hooked call made from inside a hook is the outer hook's own work: not counted by itself, and its
        // real call is not the outer hook's native part.
        {
            Scope outer(Site::DrawIndexed);
            {
                Scope inner(Site::Viewports);
                Native native(inner);
                for (int turn = 0; turn < 20000; ++turn)
                    spin = spin + static_cast<uint64_t>(turn);
            }
            Check(g_depth == 1, "inside the outer hook");
        }
        Check(g_depth == 0, "and out of it");
        TakeTotals(totals);
        const Totals& indexed = totals[static_cast<size_t>(Site::DrawIndexed)];
        Check(totals[static_cast<size_t>(Site::Viewports)].calls == 0, "the inner call is not counted by itself");
        Check(indexed.calls == 1 && indexed.nativeTicks == 0 && indexed.ownTicks > draw.ownTicks * 4,
              "all of it is the outer hook's own work");

        // A frame that stops being timed while a hook is in it: the hook finishes what it began.
        {
            Scope scope(Site::Draw);
            g_timing.store(false);
        }
        TakeTotals(totals);
        Check(totals[static_cast<size_t>(Site::Draw)].calls == 1 && g_depth == 0, "a hook ends as it began");

        // The timer's own cost: something, not much, and it leaves the flag and the sums as they were.
        const uint64_t timer = TimerTicks();
        Check(timer > 0 && timer < 100000, "an empty hook reads a few cycles");
        Check(!g_timing.load(), "the flag is as it was");
        TakeTotals(totals);
        Check(totals[static_cast<size_t>(Site::Calibration)].calls == 0, "and nothing is left in the sums");

        // For the reader of the test's output: what a hooked call pays on this machine, timed and not.
        const int turns = 2000000;
        uint64_t begin = __rdtsc();
        for (int turn = 0; turn < turns; ++turn)
        {
            Scope scope(Site::Draw);
            Native native(scope);
            spin = spin + 1;
        }
        const uint64_t untimed = (__rdtsc() - begin) / turns;
        g_timing.store(true);
        begin = __rdtsc();
        for (int turn = 0; turn < turns; ++turn)
        {
            Scope scope(Site::Draw);
            Native native(scope);
            spin = spin + 1;
        }
        const uint64_t timed = (__rdtsc() - begin) / turns;
        g_timing.store(false);
        TakeTotals(totals);
        std::printf("info: a hooked call pays about %llu cycles in a frame that is not timed and %llu in one that "
                    "is (the loop's own work included); an empty hook reads %llu\n",
                    static_cast<unsigned long long>(untimed), static_cast<unsigned long long>(timed),
                    static_cast<unsigned long long>(timer));
    }

    if (failures)
        std::printf("%d check(s) failed\n", failures);
    else
        std::printf("hook cost: all checks passed\n");
    return failures ? 1 : 0;
}
