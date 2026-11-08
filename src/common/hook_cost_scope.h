#pragma once

// The timers of the hook cost measurement (hook_cost_logic.h): the scope a hook stands in, the mark around the
// game's real call, and the sums they add to. Included by every file that holds a Direct3D context hook.
//
// A hook writes:
//
//     HookCostScope cost(hook_cost::Site::DrawIndexed);
//     ... the mod's own work ...
//     { HookCostNative native(cost); original(context, ...); }
//     ... the mod's own work ...
//
// While no frame is being timed the scope is one flag test and the mark is one test of a local.

#include "hook_cost_logic.h"
#include <atomic>
#include <intrin.h>

namespace hook_cost
{
// True while a frame is being timed. Set and cleared by the presenting thread; read by every hooked call.
inline std::atomic<bool> g_timing{false};
// How many timed hooks this thread is inside.
inline thread_local int g_depth = 0;
struct Cell
{
    std::atomic<uint64_t> calls{0}, ownTicks{0}, nativeTicks{0};
};
inline Cell g_cells[kSites];

class Scope
{
public:
    explicit Scope(Site site) noexcept
    {
        if (!g_timing.load(std::memory_order_relaxed))
            return;
        // A hooked call the mod makes from inside a hook is the outer hook's own work, hook and real call both.
        if (g_depth++ != 0)
        {
            state_ = State::Inner;
            return;
        }
        state_ = State::Timed;
        site_ = site;
        begin_ = __rdtsc();
    }
    ~Scope()
    {
        if (state_ == State::Off)
            return;
        --g_depth;
        if (state_ != State::Timed)
            return;
        const uint64_t whole = __rdtsc() - begin_;
        Cell& cell = g_cells[static_cast<size_t>(site_)];
        // A load and a store, no lock: a sum lost to a race between two threads is a sample, not a fault.
        cell.calls.store(cell.calls.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        cell.ownTicks.store(cell.ownTicks.load(std::memory_order_relaxed) + (whole > native_ ? whole - native_ : 0),
                            std::memory_order_relaxed);
        cell.nativeTicks.store(cell.nativeTicks.load(std::memory_order_relaxed) + native_,
                               std::memory_order_relaxed);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void NativeBegin() noexcept
    {
        if (state_ == State::Timed)
            nativeBegin_ = __rdtsc();
    }
    void NativeEnd() noexcept
    {
        if (state_ == State::Timed)
            native_ += __rdtsc() - nativeBegin_;
    }

private:
    enum class State : uint8_t { Off, Timed, Inner } state_ = State::Off;
    Site site_ = Site::Count;
    uint64_t begin_ = 0, nativeBegin_ = 0, native_ = 0;
};

// The game's real call inside a hook: its time is Direct3D's and the driver's, not the mod's.
class Native
{
public:
    explicit Native(Scope& scope) noexcept : scope_(scope) { scope_.NativeBegin(); }
    ~Native() { scope_.NativeEnd(); }
    Native(const Native&) = delete;
    Native& operator=(const Native&) = delete;

private:
    Scope& scope_;
};

// The sums so far, and a fresh start.
inline void TakeTotals(Totals out[kSites]) noexcept
{
    for (size_t site = 0; site < kSites; ++site)
    {
        out[site].calls = g_cells[site].calls.exchange(0, std::memory_order_relaxed);
        out[site].ownTicks = g_cells[site].ownTicks.exchange(0, std::memory_order_relaxed);
        out[site].nativeTicks = g_cells[site].nativeTicks.exchange(0, std::memory_order_relaxed);
    }
}

// What an empty hook reads: the timer's own cost, taken off every call. The least of sixteen runs, so that an
// interrupt in one of them is not in the figure. Called outside any hook.
inline uint64_t TimerTicks() noexcept
{
    Cell& cell = g_cells[static_cast<size_t>(Site::Calibration)];
    const bool was = g_timing.exchange(true, std::memory_order_relaxed);
    uint64_t least = UINT64_MAX;
    for (int run = 0; run < 16; ++run)
    {
        cell.calls.store(0, std::memory_order_relaxed);
        cell.ownTicks.store(0, std::memory_order_relaxed);
        cell.nativeTicks.store(0, std::memory_order_relaxed);
        for (int call = 0; call < 512; ++call)
        {
            Scope scope(Site::Calibration);
            Native native(scope);
        }
        const uint64_t calls = cell.calls.load(std::memory_order_relaxed);
        const uint64_t each = calls ? cell.ownTicks.load(std::memory_order_relaxed) / calls : UINT64_MAX;
        if (each < least)
            least = each;
    }
    g_timing.store(was, std::memory_order_relaxed);
    cell.calls.store(0, std::memory_order_relaxed);
    cell.ownTicks.store(0, std::memory_order_relaxed);
    cell.nativeTicks.store(0, std::memory_order_relaxed);
    return least == UINT64_MAX ? 0 : least;
}
} // namespace hook_cost

using HookCostScope = hook_cost::Scope;
using HookCostNative = hook_cost::Native;
