#pragma once

// THE MOD'S OWN COST INSIDE THE DIRECT3D CALLS IT HOOKS.
//
// The mod stands in front of the game's Draw, DrawIndexed, pixel-shader, shader-resource and render-target calls.
// A scene makes ten to twenty thousand draws a frame and several of the other calls for each, so a hook that
// spends forty nanoseconds on its own checks is a millisecond of every frame; and a frame's length follows its
// draw count. This measures that cost in place, so that what is trimmed is what was found to cost.
//
// HOW. One frame in N (hook_cost_frames) is timed. In that frame every hooked call reads the processor's cycle
// counter four times: on entry, around the game's real call (the "native" part: Direct3D and the driver), and on
// exit. What is left after taking the native part out of the whole is the mod's own work in that call. A hooked
// call the mod itself makes from inside a hook is not timed again: all of it, hook and real call, is the outer
// hook's own work, which is what it costs the game.
//
// THE TIMER'S OWN COST. Reading the counter takes cycles too, and part of every reading falls inside what it
// measures. An empty hook is timed when the option is first used (Site::Calibration) and that much is taken off
// every call. The other frames pay one flag test a hooked call.
//
// NOT IN THE FIGURE: the jump into the hook and back (a few cycles a call), and what the hooks do to the game's
// caches and branch predictor.
//
// No Windows calls here; the unit test is tests/hook_cost_tests.cpp.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace hook_cost
{
enum class Site : uint8_t
{
    DrawIndexed,
    Draw,
    DrawOther,        // instanced, auto and indirect draws
    PixelShader,      // PSSetShader
    ShaderResources,  // PSSetShaderResources
    Samplers,         // PSSetSamplers
    RenderTargets,    // OMSetRenderTargets, with and without unordered access views
    Viewports,        // RSSetViewports
    Scissors,         // RSSetScissorRects
    CopyResource,
    StateReset,       // ClearState, ExecuteCommandList, SwapDeviceContextState
    Calibration,      // an empty hook: the timer's own cost. Never reported.
    Count
};
inline constexpr size_t kSites = static_cast<size_t>(Site::Count);
inline const char* SiteName(Site s)
{
    switch (s)
    {
    case Site::DrawIndexed: return "DrawIndexed";
    case Site::Draw: return "Draw";
    case Site::DrawOther: return "other draws";
    case Site::PixelShader: return "PSSetShader";
    case Site::ShaderResources: return "PSSetShaderResources";
    case Site::Samplers: return "PSSetSamplers";
    case Site::RenderTargets: return "OMSetRenderTargets";
    case Site::Viewports: return "RSSetViewports";
    case Site::Scissors: return "RSSetScissorRects";
    case Site::CopyResource: return "CopyResource";
    case Site::StateReset: return "state resets";
    case Site::Calibration: return "calibration";
    default: return "?";
    }
}

// A timed frame costs the game the timer's readings in every hooked call (milliseconds, in a scene with much in
// it), so they are kept apart: never more often than one frame in kMinEveryFrames.
inline constexpr uint32_t kMinEveryFrames = 8;
inline constexpr uint32_t kMaxEveryFrames = 100000;
// Whether presented frame `frame` (counted from 1) is timed. `every` 0 = the measurement is off.
inline bool TimeThisFrame(uint64_t frame, uint32_t every)
{
    if (!every)
        return false;
    if (every < kMinEveryFrames)
        every = kMinEveryFrames;
    return frame % every == 0;
}

// What the timed frames added up to at one hook. Ticks are the processor's cycle counter.
struct Totals
{
    uint64_t calls = 0;
    uint64_t ownTicks = 0;      // the whole call less its native part, the timer's own cost still in it
    uint64_t nativeTicks = 0;
};

struct Row
{
    Site site = Site::Count;
    double callsAFrame = 0.0;
    double ownNsACall = 0.0;
    double ownMsAFrame = 0.0;
};
struct Summary
{
    bool valid = false;
    uint64_t frames = 0;
    double frameMs = 0.0;         // a timed frame, present to present
    double callsAFrame = 0.0;
    double ownMsAFrame = 0.0;     // the mod's own work in all its hooks
    double nativeMsAFrame = 0.0;  // the calls themselves
    double ownShare = 0.0;        // ownMsAFrame / frameMs
    double timerNsACall = 0.0;    // taken off every call
    Row rows[kSites]{};           // most costly first; only hooks that were called
    size_t rowCount = 0;
};

// `frames` timed frames lasted `frameTicks` in all; `ticksPerNs` is the counter's rate; `timerTicks` is what an
// empty hook reads. Not valid: nothing was timed, or the counter's rate is not known.
inline Summary Summarise(const Totals totals[kSites], uint64_t frames, uint64_t frameTicks, double ticksPerNs,
                         uint64_t timerTicks)
{
    Summary out;
    if (!frames || !(ticksPerNs > 0.0))
        return out;
    const double perFrame = 1.0 / static_cast<double>(frames);
    const double msATick = 1.0e-6 / ticksPerNs;
    out.frames = frames;
    out.frameMs = static_cast<double>(frameTicks) * msATick * perFrame;
    out.timerNsACall = static_cast<double>(timerTicks) / ticksPerNs;
    for (size_t index = 0; index < kSites; ++index)
    {
        const Site site = static_cast<Site>(index);
        const Totals& t = totals[index];
        if (site == Site::Calibration || !t.calls)
            continue;
        // The timer's own cost comes off every call; a hook that does nothing reads none, never less.
        const uint64_t timer = t.calls * timerTicks;
        const uint64_t own = t.ownTicks > timer ? t.ownTicks - timer : 0;
        Row row;
        row.site = site;
        row.callsAFrame = static_cast<double>(t.calls) * perFrame;
        row.ownNsACall = static_cast<double>(own) / ticksPerNs / static_cast<double>(t.calls);
        row.ownMsAFrame = static_cast<double>(own) * msATick * perFrame;
        out.callsAFrame += row.callsAFrame;
        out.ownMsAFrame += row.ownMsAFrame;
        out.nativeMsAFrame += static_cast<double>(t.nativeTicks) * msATick * perFrame;
        size_t at = out.rowCount++;
        for (; at > 0 && out.rows[at - 1].ownMsAFrame < row.ownMsAFrame; --at)
            out.rows[at] = out.rows[at - 1];
        out.rows[at] = row;
    }
    out.ownShare = out.frameMs > 0.0 ? out.ownMsAFrame / out.frameMs : 0.0;
    out.valid = out.rowCount > 0;
    return out;
}

// The log line. Returns the length written (0: nothing to say).
inline int FormatSummary(char* out, size_t size, const Summary& s, uint32_t every)
{
    if (!out || !size)
        return 0;
    out[0] = 0;
    if (!s.valid)
        return 0;
    int length = std::snprintf(
        out, size,
        "timing: the mod's own work inside the Direct3D calls it hooks, from %llu timed frame%s (one in %u): "
        "%.2f ms of a %.2f ms frame (%.1f%%), over %.0f hooked calls a frame; the calls themselves took %.2f ms. "
        "By call, a frame:",
        static_cast<unsigned long long>(s.frames), s.frames == 1 ? "" : "s", every, s.ownMsAFrame, s.frameMs,
        s.ownShare * 100.0, s.callsAFrame, s.nativeMsAFrame);
    if (length < 0)
        return 0;
    for (size_t index = 0; index < s.rowCount && static_cast<size_t>(length) < size; ++index)
    {
        const Row& row = s.rows[index];
        const int more = std::snprintf(out + length, size - static_cast<size_t>(length),
                                       "%s %s %.0f x %.0f ns = %.3f ms", index ? ";" : "", SiteName(row.site),
                                       row.callsAFrame, row.ownNsACall, row.ownMsAFrame);
        if (more < 0)
            break;
        length += more;
    }
    if (static_cast<size_t>(length) < size)
    {
        const int more = std::snprintf(out + length, size - static_cast<size_t>(length),
                                       ". The timer's own %.0f ns a call is taken off.", s.timerNsACall);
        if (more > 0)
            length += more;
    }
    return static_cast<size_t>(length) < size ? length : static_cast<int>(size - 1);
}
} // namespace hook_cost
